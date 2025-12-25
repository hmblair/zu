/*
 * watch_macos.c - FSEvents-based filesystem watcher for macOS
 */

#ifdef __APPLE__

#include "watch.h"
#include <CoreServices/CoreServices.h>
#include <dispatch/dispatch.h>
#include <stdlib.h>
#include <string.h>

static FSEventStreamRef g_stream = NULL;
static CFRunLoopRef g_runloop = NULL;
static watch_callback g_callback = NULL;
static void *g_ctx = NULL;
static volatile int g_running = 0;

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

    g_runloop = CFRunLoopGetCurrent();
    FSEventStreamScheduleWithRunLoop(g_stream, g_runloop, kCFRunLoopDefaultMode);
    FSEventStreamStart(g_stream);

    g_running = 1;
    while (g_running) {
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 1.0, true);
    }
}

void watch_stop(void) {
    g_running = 0;
    if (g_runloop) {
        CFRunLoopStop(g_runloop);
    }
}

void watch_cleanup(void) {
    if (g_stream) {
        FSEventStreamStop(g_stream);
        FSEventStreamInvalidate(g_stream);
        FSEventStreamRelease(g_stream);
        g_stream = NULL;
    }
    g_runloop = NULL;
    g_callback = NULL;
    g_ctx = NULL;
}

#endif /* __APPLE__ */
