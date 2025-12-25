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
#include "watch.h"

#define UPDATE_INTERVAL 300  /* 5 minutes */
#define DEPTH_THRESHOLD 3    /* Cache directories at depth <= this */
#define MAX_DIRTY_PATHS 65536
#define MAX_PROBE_DEPTH 32   /* Hash table probe limit */

/* Dirty paths that need recalculation */
static char *g_dirty_paths[MAX_DIRTY_PATHS];
static size_t g_dirty_count = 0;
static pthread_mutex_t g_dirty_lock = PTHREAD_MUTEX_INITIALIZER;

/* Cache mutex for thread-safe access */
static pthread_mutex_t g_cache_lock = PTHREAD_MUTEX_INITIALIZER;

/* Use atomic for shutdown flag (async-signal-safe) */
static atomic_int g_shutdown = 0;
static char g_root[PATH_MAX];

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

/* Count slashes to determine depth */
static int path_depth(const char *path, const char *root) {
    size_t root_len = strlen(root);
    if (strncmp(path, root, root_len) != 0) return -1;

    const char *rest = path + root_len;
    int depth = 0;
    while (*rest) {
        if (*rest == '/') depth++;
        rest++;
    }
    return depth;
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

    /* Mark all ancestors at depth <= DEPTH_THRESHOLD as dirty */
    char buf[PATH_MAX];
    strncpy(buf, path, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    /* Walk up to root, marking cached ancestors as dirty */
    int depth;
    while ((depth = path_depth(buf, g_root)) > 0) {
        if (depth <= DEPTH_THRESHOLD) {
            mark_dirty(buf);
        }
        parent_path(buf, buf, sizeof(buf));
    }
}

/* Compute both size and file count in one pass */
static void get_dir_stats(const char *path, off_t *size_out, long *count_out) {
    DIR *dir = opendir(path);
    if (!dir) {
        *size_out = -1;
        *count_out = -1;
        return;
    }

    off_t total_size = 0;
    long total_count = 0;
    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.' &&
            (entry->d_name[1] == '\0' ||
             (entry->d_name[1] == '.' && entry->d_name[2] == '\0'))) {
            continue;
        }

        char full[PATH_MAX];
        snprintf(full, sizeof(full), "%s/%s", path, entry->d_name);

        struct stat st;
        if (lstat(full, &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            off_t sub_size;
            long sub_count;
            get_dir_stats(full, &sub_size, &sub_count);
            total_size += sub_size;
            total_count += sub_count;
        } else {
            total_size += st.st_size;
            total_count++;
        }
    }

    closedir(dir);
    *size_out = total_size;
    *count_out = total_count;
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
            get_dir_stats(paths[i], &sizes[i], &counts[i]);
            valid[i] = 1;
        } else {
            valid[i] = 0;
        }
    }

    /* Lock cache for batch update */
    size_t stored = 0, failed = 0;
    pthread_mutex_lock(&g_cache_lock);
    for (size_t i = 0; i < count; i++) {
        if (valid[i]) {
            if (cache_store(paths[i], sizes[i], counts[i])) {
                stored++;
            } else {
                failed++;
            }
        }
        free(paths[i]);
    }
    if (cache_save() != 0) {
        log_error("failed to save cache to disk");
    }
    pthread_mutex_unlock(&g_cache_lock);

    log_info("processed %zu paths (stored: %zu, failed: %zu)", count, stored, failed);
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

/* Initial scan to populate cache for cold start */
static void initial_scan(const char *path, int depth) {
    if (atomic_load(&g_shutdown)) return;
    if (depth > DEPTH_THRESHOLD) return;  /* Don't scan beyond threshold */

    DIR *dir = opendir(path);
    if (!dir) return;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.' &&
            (entry->d_name[1] == '\0' ||
             (entry->d_name[1] == '.' && entry->d_name[2] == '\0'))) {
            continue;
        }

        char full[PATH_MAX];
        snprintf(full, sizeof(full), "%s/%s", path, entry->d_name);

        struct stat st;
        if (lstat(full, &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            /* Check if already cached (lock not needed - single-threaded at this point) */
            uint64_t h = fnv1a_hash(full);
            int found = 0;
            if (d_cache) {
                uint32_t idx = h % CACHE_CAPACITY;
                for (int i = 0; i < MAX_PROBE_DEPTH && !found; i++) {
                    CacheEntry *e = &d_cache->entries[(idx + i) % CACHE_CAPACITY];
                    if (e->size == -1) break;
                    if (e->path_hash == h) found = 1;
                }
            }
            if (!found) {
                mark_dirty(full);
            }
            initial_scan(full, depth + 1);
        }
    }
    closedir(dir);
}

static void usage(const char *prog) {
    fprintf(stderr, "Usage: %s [root]\n", prog);
    fprintf(stderr, "  root: Directory to watch (default: $HOME)\n");
}

int main(int argc, char *argv[]) {
    /* Determine root directory */
    const char *root = getenv("HOME");
    if (argc > 1) {
        if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
            usage(argv[0]);
            return 0;
        }
        root = argv[1];
    }
    if (!root) {
        log_error("no home directory");
        return 1;
    }

    /* Resolve to absolute path */
    if (!realpath(root, g_root)) {
        log_error("realpath failed: %s", root);
        return 1;
    }

    log_info("watching %s", g_root);

    /* Initialize cache */
    if (cache_init() != 0) {
        log_error("failed to initialize cache");
        return 1;
    }

    /* Initial scan to populate cache */
    log_info("scanning for uncached directories...");
    initial_scan(g_root, 0);
    if (g_dirty_count > 0) {
        log_info("found %zu uncached directories, processing...", g_dirty_count);
        process_dirty_paths();
    } else {
        log_info("cache is up to date");
    }

    /* Set up signal handlers using sigaction for portability */
    struct sigaction sa;
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    /* Initialize watcher */
    if (watch_init(g_root, on_change, NULL) != 0) {
        log_error("failed to initialize watcher");
        cache_free();
        return 1;
    }

    /* Start timer thread */
    pthread_t timer;
    pthread_create(&timer, NULL, timer_thread, NULL);

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
