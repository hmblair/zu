/*
 * watch_linux.c - inotify-based filesystem watcher for Linux
 */

#ifdef __linux__

#include "watch.h"
#include <sys/inotify.h>
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>

#define EVENT_SIZE  (sizeof(struct inotify_event))
#define BUF_LEN     (1024 * (EVENT_SIZE + NAME_MAX + 1))
#define MAX_WATCHES 65536

static int g_inotify_fd = -1;
static watch_callback g_callback = NULL;
static void *g_ctx = NULL;
static volatile sig_atomic_t g_running = 0;

/* Map watch descriptors to paths */
static char *g_wd_to_path[MAX_WATCHES];

static void add_watch_recursive(const char *path);

static void add_single_watch(const char *path) {
    int wd = inotify_add_watch(g_inotify_fd, path,
        IN_CREATE | IN_DELETE | IN_MODIFY | IN_MOVED_FROM | IN_MOVED_TO);
    if (wd >= 0 && wd < MAX_WATCHES) {
        free(g_wd_to_path[wd]);
        g_wd_to_path[wd] = strdup(path);
    }
}

static void add_watch_recursive(const char *path) {
    add_single_watch(path);

    DIR *dir = opendir(path);
    if (!dir) return;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        if (entry->d_type != DT_DIR) continue;

        char subpath[PATH_MAX];
        snprintf(subpath, sizeof(subpath), "%s/%s", path, entry->d_name);
        add_watch_recursive(subpath);
    }
    closedir(dir);
}

int watch_init(const char *root, watch_callback cb, void *ctx) {
    g_callback = cb;
    g_ctx = ctx;

    g_inotify_fd = inotify_init();
    if (g_inotify_fd < 0) {
        return -1;
    }

    memset(g_wd_to_path, 0, sizeof(g_wd_to_path));
    add_watch_recursive(root);

    return 0;
}

void watch_run(void) {
    if (g_inotify_fd < 0) return;

    char buf[BUF_LEN];
    g_running = 1;

    while (g_running) {
        ssize_t len = read(g_inotify_fd, buf, BUF_LEN);
        if (len < 0) {
            if (errno == EINTR) continue;
            break;
        }

        char *ptr = buf;
        while (ptr < buf + len) {
            struct inotify_event *event = (struct inotify_event *)ptr;

            if (event->wd >= 0 && event->wd < MAX_WATCHES && g_wd_to_path[event->wd]) {
                char full_path[PATH_MAX];
                if (event->len > 0) {
                    snprintf(full_path, sizeof(full_path), "%s/%s",
                             g_wd_to_path[event->wd], event->name);
                } else {
                    strncpy(full_path, g_wd_to_path[event->wd], sizeof(full_path) - 1);
                }

                /* If a new directory was created, add watch for it */
                if ((event->mask & IN_CREATE) && (event->mask & IN_ISDIR)) {
                    add_watch_recursive(full_path);
                }

                if (g_callback) {
                    g_callback(full_path, g_ctx);
                }
            }

            ptr += EVENT_SIZE + event->len;
        }
    }
}

void watch_stop(void) {
    g_running = 0;
}

void watch_cleanup(void) {
    if (g_inotify_fd >= 0) {
        close(g_inotify_fd);
        g_inotify_fd = -1;
    }

    for (int i = 0; i < MAX_WATCHES; i++) {
        free(g_wd_to_path[i]);
        g_wd_to_path[i] = NULL;
    }

    g_callback = NULL;
    g_ctx = NULL;
}

#endif /* __linux__ */
