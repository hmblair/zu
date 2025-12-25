/*
 * ld - Directory size caching daemon
 *
 * Watches the home directory for changes and maintains a cache of
 * directory sizes for directories at depth >= 4.
 *
 * Build: cc -O2 -o ld ld.c watch_macos.c -framework CoreServices (macOS)
 *        cc -O2 -o ld ld.c watch_linux.c (Linux)
 */

#define CACHE_DAEMON  /* Enable daemon-side cache functions */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <signal.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>
#include <pthread.h>
#include <limits.h>
#include <stdatomic.h>

#include "cache.h"
#include "dir_stats.h"
#include "watch.h"

#ifdef _OPENMP
#include <omp.h>
#endif

#define UPDATE_INTERVAL 300  /* 5 minutes */
#define FILE_COUNT_THRESHOLD 1000  /* Cache directories with >= this many files */
#define MAX_DIRTY_PATHS 65536
#define MAX_PROBE_DEPTH 32   /* Hash table probe limit */
#define MAX_ROOTS 8          /* Maximum watched directories */

/* Dirty paths that need recalculation */
static char *g_dirty_paths[MAX_DIRTY_PATHS];
static size_t g_dirty_count = 0;
static pthread_mutex_t g_dirty_lock = PTHREAD_MUTEX_INITIALIZER;

/* Cache mutex for thread-safe access */
static pthread_mutex_t g_cache_lock = PTHREAD_MUTEX_INITIALIZER;

/* Use atomic for shutdown flag (async-signal-safe) */
static atomic_int g_shutdown = 0;
static char g_roots[MAX_ROOTS][PATH_MAX];
static int g_root_count = 0;

/* Logging helpers */
static void log_msg(const char *level, const char *fmt, ...) {
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    fprintf(stderr, "[%02d:%02d:%02d] %s: ",
            tm->tm_hour, tm->tm_min, tm->tm_sec, level);
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
}

#define log_error(...) log_msg("ERROR", __VA_ARGS__)
#define log_warn(...)  log_msg("WARN", __VA_ARGS__)
#define log_info(...)  log_msg("INFO", __VA_ARGS__)

/* Check if path is under any watched root */
static int is_under_root(const char *path) {
    for (int i = 0; i < g_root_count; i++) {
        size_t root_len = strlen(g_roots[i]);
        if (strncmp(path, g_roots[i], root_len) == 0 &&
            (path[root_len] == '/' || path[root_len] == '\0')) {
            return 1;
        }
    }
    return 0;
}

/* Check if path is cached (has entry in cache) */
static int is_cached(const char *path) {
    if (!d_cache) return 0;
    uint64_t h = fnv1a_hash(path);
    uint32_t idx = h % CACHE_CAPACITY;
    for (int i = 0; i < MAX_PROBE_DEPTH; i++) {
        CacheEntry *e = &d_cache->entries[(idx + i) % CACHE_CAPACITY];
        if (e->size < 0 && e->path_hash == 0) break;  /* Empty slot */
        if (e->path_hash == h) return 1;
    }
    return 0;
}

/* Get parent directory path */
static void parent_path(const char *path, char *out, size_t len) {
    strncpy(out, path, len - 1);
    out[len - 1] = '\0';
    char *last = strrchr(out, '/');
    if (last && last != out) {
        *last = '\0';
    }
}

/* Add a path to the dirty list */
static void mark_dirty(const char *path) {
    pthread_mutex_lock(&g_dirty_lock);

    /* Check if already in list */
    for (size_t i = 0; i < g_dirty_count; i++) {
        if (strcmp(g_dirty_paths[i], path) == 0) {
            pthread_mutex_unlock(&g_dirty_lock);
            return;
        }
    }

    if (g_dirty_count < MAX_DIRTY_PATHS) {
        char *dup = strdup(path);
        if (dup) {
            g_dirty_paths[g_dirty_count++] = dup;
        } else {
            log_error("strdup failed for path: %s", path);
        }
    } else {
        log_warn("dirty path list full, dropping: %s", path);
    }

    pthread_mutex_unlock(&g_dirty_lock);
}

/* Called when a filesystem event occurs */
static void on_change(const char *path, void *ctx) {
    (void)ctx;

    /* Mark cached ancestors as dirty */
    char buf[PATH_MAX];
    strncpy(buf, path, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    /* Walk up to root, marking cached ancestors as dirty */
    while (is_under_root(buf) && strlen(buf) > 1) {
        if (is_cached(buf)) {
            mark_dirty(buf);
        }
        parent_path(buf, buf, sizeof(buf));
    }
}


/* Process dirty paths and update cache */
static void process_dirty_paths(void) {
    pthread_mutex_lock(&g_dirty_lock);

    if (g_dirty_count == 0) {
        pthread_mutex_unlock(&g_dirty_lock);
        return;
    }

    /* Swap out the dirty list */
    char *paths[MAX_DIRTY_PATHS];
    size_t count = g_dirty_count;
    memcpy(paths, g_dirty_paths, count * sizeof(char *));
    g_dirty_count = 0;

    pthread_mutex_unlock(&g_dirty_lock);

    /* Process each path (stat/traverse without lock for better concurrency) */
    off_t sizes[MAX_DIRTY_PATHS];
    long counts[MAX_DIRTY_PATHS];
    int valid[MAX_DIRTY_PATHS];

    for (size_t i = 0; i < count; i++) {
        struct stat st;
        if (stat(paths[i], &st) == 0 && S_ISDIR(st.st_mode)) {
            /* Use shared traversal code, no cache lookup (we're building the cache) */
            DirStats ds = dir_stats_get(paths[i], NULL);
            sizes[i] = ds.size;
            counts[i] = ds.file_count;
            valid[i] = 1;
        } else {
            valid[i] = 0;
        }
    }

    /* Lock cache for batch update */
    size_t stored = 0, skipped = 0, failed = 0;
    pthread_mutex_lock(&g_cache_lock);
    for (size_t i = 0; i < count; i++) {
        if (valid[i]) {
            /* Only cache directories with >= FILE_COUNT_THRESHOLD files */
            if (counts[i] >= FILE_COUNT_THRESHOLD) {
                if (cache_store(paths[i], sizes[i], counts[i]) == 0) {
                    stored++;
                } else {
                    failed++;
                }
            } else {
                skipped++;
            }
        }
        free(paths[i]);
    }
    if (cache_save() != 0) {
        log_error("failed to save cache to disk");
    }
    pthread_mutex_unlock(&g_cache_lock);

    log_info("processed %zu paths (stored: %zu, skipped: %zu, failed: %zu)",
             count, stored, skipped, failed);
}

/* Timer thread */
static void *timer_thread(void *arg) {
    (void)arg;

    while (!atomic_load(&g_shutdown)) {
        sleep(UPDATE_INTERVAL);
        if (!atomic_load(&g_shutdown)) {
            process_dirty_paths();
        }
    }
    return NULL;
}

/* Signal handler - only uses async-signal-safe operations */
static void handle_signal(int sig) {
    (void)sig;
    atomic_store(&g_shutdown, 1);
    /* Note: watch_stop() should be async-signal-safe (just sets a flag) */
    watch_stop();
}

/* Check if path ends with .git */
static int is_git_dir(const char *path) {
    const char *name = strrchr(path, '/');
    name = name ? name + 1 : path;
    return strcmp(name, ".git") == 0;
}

/* Initial scan - single bottom-up pass that computes and caches in one traversal */
static DirStats initial_scan_impl(const char *path, dev_t root_dev) {
    DirStats result = {0, 0};
    if (atomic_load(&g_shutdown)) return result;

    /* For .git directories, don't count files (but still compute size) */
    int skip_file_count = is_git_dir(path);

    int dirfd = open(path, O_RDONLY | O_DIRECTORY);
    if (dirfd < 0) return (DirStats){-1, -1};

    /* Get device ID to detect filesystem boundaries */
    struct stat dir_st;
    if (fstat(dirfd, &dir_st) != 0) {
        close(dirfd);
        return (DirStats){-1, -1};
    }
    if (root_dev == 0) {
        root_dev = dir_st.st_dev;
    } else if (dir_st.st_dev != root_dev) {
        /* Different filesystem - skip to avoid double-counting */
        close(dirfd);
        return (DirStats){0, 0};
    }

    DIR *dir = fdopendir(dirfd);
    if (!dir) {
        close(dirfd);
        return (DirStats){-1, -1};
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (atomic_load(&g_shutdown)) break;

        if (entry->d_name[0] == '.' &&
            (entry->d_name[1] == '\0' ||
             (entry->d_name[1] == '.' && entry->d_name[2] == '\0'))) {
            continue;
        }

        int is_dir = 0;
        int is_file = 0;
        off_t file_size = 0;

#ifdef DT_DIR
        if (entry->d_type == DT_DIR) {
            is_dir = 1;
        } else if (entry->d_type == DT_REG) {
            is_file = 1;
        } else if (entry->d_type == DT_UNKNOWN) {
            struct stat st;
            if (fstatat(dirfd, entry->d_name, &st, AT_SYMLINK_NOFOLLOW) == 0) {
                is_dir = S_ISDIR(st.st_mode);
                is_file = S_ISREG(st.st_mode);
                file_size = st.st_size;
            }
        }
#else
        struct stat st;
        if (fstatat(dirfd, entry->d_name, &st, AT_SYMLINK_NOFOLLOW) == 0) {
            is_dir = S_ISDIR(st.st_mode);
            is_file = S_ISREG(st.st_mode);
            file_size = st.st_size;
        }
#endif

        if (is_file) {
            /* Need size for regular files */
            if (file_size == 0) {
                struct stat st;
                if (fstatat(dirfd, entry->d_name, &st, AT_SYMLINK_NOFOLLOW) == 0) {
                    file_size = st.st_size;
                }
            }
            result.size += file_size;
            if (!skip_file_count) result.file_count++;
        } else if (is_dir) {
            char full[PATH_MAX];
            /* Avoid double slash when path is "/" */
            size_t plen = strlen(path);
            if (plen > 0 && path[plen - 1] == '/') {
                snprintf(full, sizeof(full), "%s%s", path, entry->d_name);
            } else {
                snprintf(full, sizeof(full), "%s/%s", path, entry->d_name);
            }
            /* Skip paths that cause double-counting (macOS firmlinks) */
            if (ds_skip_path(full)) continue;
            DirStats sub = initial_scan_impl(full, root_dev);
            if (sub.size >= 0) result.size += sub.size;
            if (sub.file_count >= 0 && !skip_file_count) result.file_count += sub.file_count;
        }
    }
    closedir(dir);

    /* For .git dirs, set file_count to -1; cache if size-worthy or meets threshold */
    if (skip_file_count) {
        result.file_count = -1;
    } else if (result.file_count >= FILE_COUNT_THRESHOLD) {
        pthread_mutex_lock(&g_cache_lock);
        if (cache_store(path, result.size, result.file_count) == 0) {
            log_info("cached %s (%ld files)", path, result.file_count);
        }
        pthread_mutex_unlock(&g_cache_lock);
    }

    return result;
}

/* Public wrapper - starts with root_dev=0 to determine filesystem from path */
static DirStats initial_scan(const char *path) {
    return initial_scan_impl(path, 0);
}

static void usage(const char *prog) {
    fprintf(stderr, "Usage: %s [root...]\n", prog);
    fprintf(stderr, "  root: Directories to watch (default: / and $HOME)\n");
}

/* Add a root directory to watch */
static int add_root(const char *path) {
    if (g_root_count >= MAX_ROOTS) {
        log_error("too many roots (max %d)", MAX_ROOTS);
        return -1;
    }
    if (!realpath(path, g_roots[g_root_count])) {
        log_warn("realpath failed for %s, skipping", path);
        return -1;
    }
    /* Check for duplicates */
    for (int i = 0; i < g_root_count; i++) {
        if (strcmp(g_roots[i], g_roots[g_root_count]) == 0) {
            return 0;  /* Already added */
        }
    }
    log_info("adding root: %s", g_roots[g_root_count]);
    g_root_count++;
    return 0;
}

int main(int argc, char *argv[]) {
    /* Run single-threaded - OMP pragmas become no-ops */
    #ifdef _OPENMP
    omp_set_num_threads(1);
    #endif

    /* Parse arguments */
    if (argc > 1 && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)) {
        usage(argv[0]);
        return 0;
    }

    /* Add roots from arguments, or use defaults */
    if (argc > 1) {
        for (int i = 1; i < argc; i++) {
            add_root(argv[i]);
        }
    } else {
        /* Default: / and $HOME */
        add_root("/");
        const char *home = getenv("HOME");
        if (home) add_root(home);
    }

    if (g_root_count == 0) {
        log_error("no valid roots to watch");
        return 1;
    }

    /* Initialize cache */
    if (cache_init() != 0) {
        log_error("failed to initialize cache");
        return 1;
    }

    /* Initial scan of all roots */
    log_info("scanning for large directories (>=%d files)...", FILE_COUNT_THRESHOLD);
    for (int i = 0; i < g_root_count; i++) {
        log_info("scanning %s...", g_roots[i]);
        DirStats stats = initial_scan(g_roots[i]);
        log_info("  %s: %ld files, %lld bytes", g_roots[i], stats.file_count, (long long)stats.size);
    }
    log_info("initial scan complete");
    if (cache_save() != 0) {
        log_error("failed to save cache");
    }

    /* Set up signal handlers using sigaction for portability */
    struct sigaction sa;
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    /* Initialize watcher for first root (typically HOME for most activity) */
    /* Note: watching / would generate too many events, so we only watch HOME */
    const char *watch_root = g_root_count > 1 ? g_roots[1] : g_roots[0];
    log_info("watching %s for changes", watch_root);
    if (watch_init(watch_root, on_change, NULL) != 0) {
        log_error("failed to initialize watcher");
        cache_free();
        return 1;
    }

    /* Start timer thread */
    pthread_t timer;
    if (pthread_create(&timer, NULL, timer_thread, NULL) != 0) {
        log_error("failed to create timer thread");
        watch_cleanup();
        cache_free();
        return 1;
    }

    /* Run watcher (blocks until watch_stop) */
    watch_run();

    /* Cleanup */
    atomic_store(&g_shutdown, 1);
    pthread_join(timer, NULL);

    /* Final save */
    process_dirty_paths();

    watch_cleanup();
    cache_free();

    log_info("shutdown complete");
    return 0;
}
