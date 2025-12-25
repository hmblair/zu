/*
 * watch.h - Platform-agnostic filesystem watcher interface
 *
 * Abstracts FSEvents (macOS) and inotify (Linux) for watching directory trees.
 */

#ifndef WATCH_H
#define WATCH_H

/*
 * Callback invoked when a path changes.
 * path: The absolute path that changed
 * ctx: User context passed to watch_init
 */
typedef void (*watch_callback)(const char *path, void *ctx);

/*
 * Initialize the watcher for a root directory.
 * root: The directory tree to watch (e.g., "/Users/foo")
 * cb: Callback invoked on filesystem events
 * ctx: User context passed to callback
 * Returns 0 on success, -1 on error.
 */
int watch_init(const char *root, watch_callback cb, void *ctx);

/*
 * Run the watcher event loop. Blocks until watch_stop() is called.
 * Events trigger the callback registered in watch_init().
 */
void watch_run(void);

/*
 * Stop the watcher and exit the event loop.
 * Safe to call from signal handlers.
 */
void watch_stop(void);

/*
 * Clean up watcher resources.
 */
void watch_cleanup(void);

#endif /* WATCH_H */
