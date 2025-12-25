/*
 * cache.h - Shared size cache structures and functions
 *
 * Used by both l (client) and ld (daemon) for directory size caching.
 */

#ifndef CACHE_H
#define CACHE_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>

#define CACHE_MAGIC    0x4C53495A  /* "LSIZ" */
#define CACHE_VERSION  2
#define CACHE_CAPACITY 65536       /* ~2MB file */

typedef struct {
    uint64_t path_hash;    /* FNV-1a hash of absolute path */
    int64_t  size;         /* Cached size (-1 = empty) */
    int64_t  file_count;   /* Cached file count (-1 = not computed) */
    int64_t  timestamp;    /* Unix time when computed */
} CacheEntry;              /* 32 bytes */

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t count;        /* Number of valid entries */
    uint32_t capacity;
    CacheEntry entries[CACHE_CAPACITY];
} SizeCache;

/* FNV-1a 64-bit hash */
static inline uint64_t fnv1a_hash(const char *s) {
    uint64_t h = 14695981039346656037ULL;
    while (*s) {
        h ^= (uint8_t)*s++;
        h *= 1099511628211ULL;
    }
    return h;
}

/* Get cache file path */
static inline void cache_path(char *buf, size_t len) {
    const char *home = getenv("HOME");
    snprintf(buf, len, "%s/.cache/l/sizes.bin", home ? home : "/tmp");
}

/* ============================================================================
 * Client-side functions (for l.c)
 * ============================================================================ */

static SizeCache *g_cache = NULL;
static int g_cache_fd = -1;

static inline void cache_load(void) {
    char path[1024];
    cache_path(path, sizeof(path));

    g_cache_fd = open(path, O_RDONLY);
    if (g_cache_fd < 0) return;

    struct stat st;
    if (fstat(g_cache_fd, &st) < 0 || st.st_size < (off_t)sizeof(SizeCache)) {
        close(g_cache_fd);
        g_cache_fd = -1;
        return;
    }

    g_cache = mmap(NULL, sizeof(SizeCache), PROT_READ, MAP_PRIVATE, g_cache_fd, 0);
    if (g_cache == MAP_FAILED) {
        g_cache = NULL;
        close(g_cache_fd);
        g_cache_fd = -1;
        return;
    }

    if (g_cache->magic != CACHE_MAGIC || g_cache->version != CACHE_VERSION) {
        munmap(g_cache, sizeof(SizeCache));
        g_cache = NULL;
        close(g_cache_fd);
        g_cache_fd = -1;
    }
}

static inline void cache_unload(void) {
    if (g_cache) {
        munmap(g_cache, sizeof(SizeCache));
        g_cache = NULL;
    }
    if (g_cache_fd >= 0) {
        close(g_cache_fd);
        g_cache_fd = -1;
    }
}

/* Lookup size for a path in the cache. Returns size or -1 if not found. */
static inline int64_t cache_lookup(const char *path) {
    if (!g_cache) return -1;

    uint64_t h = fnv1a_hash(path);
    uint32_t idx = h % CACHE_CAPACITY;

    /* Linear probe up to 32 slots */
    for (int i = 0; i < 32; i++) {
        const CacheEntry *e = &g_cache->entries[(idx + i) % CACHE_CAPACITY];
        if (e->size == -1) return -1;  /* Empty slot = not found */
        if (e->path_hash == h) {
            return e->size;
        }
    }
    return -1;
}

/* Lookup file count for a path in the cache. Returns count or -1 if not found. */
static inline int64_t cache_lookup_count(const char *path) {
    if (!g_cache) return -1;

    uint64_t h = fnv1a_hash(path);
    uint32_t idx = h % CACHE_CAPACITY;

    /* Linear probe up to 32 slots */
    for (int i = 0; i < 32; i++) {
        const CacheEntry *e = &g_cache->entries[(idx + i) % CACHE_CAPACITY];
        if (e->size == -1) return -1;  /* Empty slot = not found */
        if (e->path_hash == h) {
            return e->file_count;
        }
    }
    return -1;
}

/* ============================================================================
 * Daemon-side functions (for ld.c)
 * ============================================================================ */

#ifdef CACHE_DAEMON

static SizeCache *d_cache = NULL;

static inline int cache_init(void) {
    char path[1024], dir[1024];
    cache_path(path, sizeof(path));

    /* Ensure directory exists */
    const char *home = getenv("HOME");
    snprintf(dir, sizeof(dir), "%s/.cache/l", home ? home : "/tmp");
    mkdir(dir, 0755);

    /* Try to load existing cache */
    int fd = open(path, O_RDONLY);
    if (fd >= 0) {
        struct stat st;
        if (fstat(fd, &st) == 0 && st.st_size >= (off_t)sizeof(SizeCache)) {
            d_cache = malloc(sizeof(SizeCache));
            if (read(fd, d_cache, sizeof(SizeCache)) == sizeof(SizeCache)) {
                if (d_cache->magic == CACHE_MAGIC && d_cache->version == CACHE_VERSION) {
                    close(fd);
                    return 0;
                }
            }
            free(d_cache);
            d_cache = NULL;
        }
        close(fd);
    }

    /* Create new cache */
    d_cache = calloc(1, sizeof(SizeCache));
    d_cache->magic = CACHE_MAGIC;
    d_cache->version = CACHE_VERSION;
    d_cache->capacity = CACHE_CAPACITY;

    /* Initialize all entries as empty */
    for (uint32_t i = 0; i < CACHE_CAPACITY; i++) {
        d_cache->entries[i].size = -1;
        d_cache->entries[i].file_count = -1;
    }

    return 0;
}

static inline void cache_free(void) {
    free(d_cache);
    d_cache = NULL;
}

/* Store or update a cache entry */
static inline void cache_store(const char *path, int64_t size, int64_t file_count) {
    if (!d_cache) return;

    uint64_t h = fnv1a_hash(path);
    uint32_t idx = h % CACHE_CAPACITY;

    /* Find existing or empty slot */
    for (int i = 0; i < 32; i++) {
        CacheEntry *e = &d_cache->entries[(idx + i) % CACHE_CAPACITY];
        if (e->size == -1 || e->path_hash == h) {
            if (e->size == -1) d_cache->count++;
            e->path_hash = h;
            e->size = size;
            e->file_count = file_count;
            e->timestamp = time(NULL);
            return;
        }
    }
    /* Table full in this region, skip */
}

/* Save cache to disk (atomic write) */
static inline int cache_save(void) {
    if (!d_cache) return -1;

    char path[1024], tmp[1024];
    cache_path(path, sizeof(path));
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);

    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return -1;

    ssize_t written = write(fd, d_cache, sizeof(SizeCache));
    close(fd);

    if (written != sizeof(SizeCache)) {
        unlink(tmp);
        return -1;
    }

    return rename(tmp, path);
}

#endif /* CACHE_DAEMON */

#endif /* CACHE_H */
