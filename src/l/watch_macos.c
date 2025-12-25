/*
 * watch_macos.c - FSEvents-based filesystem watcher for macOS
 */

#ifdef __APPLE__

#include "watch.h"
#include <CoreServices/CoreServices.h>
#include <dispatch/dispatch.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

static FSEventStreamRef g_stream = NULL;
static dispatch_queue_t g_queue = NULL;
static dispatch_semaphore_t g_semaphore = NULL;
static watch_callback g_callback = NULL;
static void *g_ctx = NULL;
static volatile sig_atomic_t g_running = 0;

static void fsevents_callback(
    ConstFSEventStreamRef streamRef,
    void *clientCallBackInfo,
    size_t numEvents,
    void *eventPaths,
    const FSEventStreamEventFlags eventFlags[],
    const FSEventStreamEventId eventIds[])
{
    (void)streamRef;
    (void)clientCallBackInfo;
    (void)eventFlags;
    (void)eventIds;

    char **paths = (char **)eventPaths;
    for (size_t i = 0; i < numEvents; i++) {
        if (g_callback) {
            g_callback(paths[i], g_ctx);
        }
    }
}

int watch_init(const char *root, watch_callback cb, void *ctx) {
    g_callback = cb;
    g_ctx = ctx;

    CFStringRef path = CFStringCreateWithCString(NULL, root, kCFStringEncodingUTF8);
    CFArrayRef paths = CFArrayCreate(NULL, (const void **)&path, 1, &kCFTypeArrayCallBacks);

    FSEventStreamContext context = {0, NULL, NULL, NULL, NULL};

    g_stream = FSEventStreamCreate(
        NULL,
        fsevents_callback,
        &context,
        paths,
        kFSEventStreamEventIdSinceNow,
        1.0,  /* 1 second latency (coalesce events) */
        kFSEventStreamCreateFlagFileEvents | kFSEventStreamCreateFlagNoDefer
    );

    CFRelease(paths);
    CFRelease(path);

    if (!g_stream) {
        return -1;
    }

    return 0;
}

void watch_run(void) {
    if (!g_stream) return;

    g_queue = dispatch_queue_create("com.zu.l.fsevents", DISPATCH_QUEUE_SERIAL);
    g_semaphore = dispatch_semaphore_create(0);

    FSEventStreamSetDispatchQueue(g_stream, g_queue);
    FSEventStreamStart(g_stream);

    g_running = 1;
    while (g_running) {
        dispatch_semaphore_wait(g_semaphore, dispatch_time(DISPATCH_TIME_NOW, 1 * NSEC_PER_SEC));
    }
}

void watch_stop(void) {
    g_running = 0;
    if (g_semaphore) {
        dispatch_semaphore_signal(g_semaphore);
    }
}

void watch_cleanup(void) {
    if (g_stream) {
        FSEventStreamStop(g_stream);
        FSEventStreamInvalidate(g_stream);
        FSEventStreamRelease(g_stream);
        g_stream = NULL;
    }
    if (g_queue) {
        dispatch_release(g_queue);
        g_queue = NULL;
    }
    if (g_semaphore) {
        dispatch_release(g_semaphore);
        g_semaphore = NULL;
    }
    g_callback = NULL;
    g_ctx = NULL;
}

#endif /* __APPLE__ */
