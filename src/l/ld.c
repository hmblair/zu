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
#include <signal.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <time.h>
#include <pthread.h>
#include <limits.h>

#include "cache.h"
#include "watch.h"

#define UPDATE_INTERVAL 300  /* 5 minutes */
#define DEPTH_THRESHOLD 3    /* Cache directories at depth <= this */
#define MAX_DIRTY_PATHS 65536

/* Dirty paths that need recalculation */
static char *g_dirty_paths[MAX_DIRTY_PATHS];
static size_t g_dirty_count = 0;
static pthread_mutex_t g_dirty_lock = PTHREAD_MUTEX_INITIALIZER;

static volatile int g_shutdown = 0;
static char g_root[PATH_MAX];

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
        g_dirty_paths[g_dirty_count++] = strdup(path);
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

    /* Process each path */
    for (size_t i = 0; i < count; i++) {
        struct stat st;
        if (stat(paths[i], &st) == 0 && S_ISDIR(st.st_mode)) {
            off_t size;
            long file_count;
            get_dir_stats(paths[i], &size, &file_count);
            cache_store(paths[i], size, file_count);
        }
        free(paths[i]);
    }

    /* Save cache */
    cache_save();

    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    fprintf(stderr, "[%02d:%02d:%02d] Processed %zu paths\n",
            tm->tm_hour, tm->tm_min, tm->tm_sec, count);
}

/* Timer thread */
static void *timer_thread(void *arg) {
    (void)arg;

    while (!g_shutdown) {
        sleep(UPDATE_INTERVAL);
        if (!g_shutdown) {
            process_dirty_paths();
        }
    }
    return NULL;
}

/* Signal handler */
static void handle_signal(int sig) {
    (void)sig;
    g_shutdown = 1;
    watch_stop();
}

/* Initial scan to populate cache for cold start */
static void initial_scan(const char *path, int depth) {
    if (g_shutdown) return;
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
            /* Check if already cached */
            uint64_t h = fnv1a_hash(full);
            int found = 0;
            if (d_cache) {
                uint32_t idx = h % CACHE_CAPACITY;
                for (int i = 0; i < 32 && !found; i++) {
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
        fprintf(stderr, "Error: No home directory\n");
        return 1;
    }

    /* Resolve to absolute path */
    if (!realpath(root, g_root)) {
        perror("realpath");
        return 1;
    }

    fprintf(stderr, "l-cached: watching %s\n", g_root);

    /* Initialize cache */
    if (cache_init() != 0) {
        fprintf(stderr, "Error: Failed to initialize cache\n");
        return 1;
    }

    /* Initial scan to populate cache */
    fprintf(stderr, "l-cached: scanning for uncached directories...\n");
    initial_scan(g_root, 0);
    if (g_dirty_count > 0) {
        fprintf(stderr, "l-cached: found %zu uncached directories, processing...\n", g_dirty_count);
        process_dirty_paths();
    } else {
        fprintf(stderr, "l-cached: cache is up to date\n");
    }

    /* Set up signal handlers */
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    /* Initialize watcher */
    if (watch_init(g_root, on_change, NULL) != 0) {
        fprintf(stderr, "Error: Failed to initialize watcher\n");
        cache_free();
        return 1;
    }

    /* Start timer thread */
    pthread_t timer;
    pthread_create(&timer, NULL, timer_thread, NULL);

    /* Run watcher (blocks until watch_stop) */
    watch_run();

    /* Cleanup */
    g_shutdown = 1;
    pthread_join(timer, NULL);

    /* Final save */
    process_dirty_paths();

    watch_cleanup();
    cache_free();

    fprintf(stderr, "l-cached: shutdown complete\n");
    return 0;
}
