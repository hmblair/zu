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
#include <limits.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/stat.h>

#define CACHE_MAGIC      0x4C53495A  /* "LSIZ" */
#define CACHE_VERSION    3           /* Bumped for checksum support */
#define CACHE_CAPACITY   65536       /* ~2MB file */
#define MAX_PROBE_DEPTH  32          /* Max slots to probe in hash table */

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
    uint32_t checksum;     /* CRC32 of entries array (at end to preserve v2 layout) */
} SizeCache;

/* CRC32 implementation (IEEE polynomial) */
static inline uint32_t cache_crc32(const void *data, size_t len) {
    static const uint32_t table[256] = {
        0x00000000, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419, 0x706af48f,
        0xe963a535, 0x9e6495a3, 0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988,
        0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91, 0x1db71064, 0x6ab020f2,
        0xf3b97148, 0x84be41de, 0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7,
        0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec, 0x14015c4f, 0x63066cd9,
        0xfa0f3d63, 0x8d080df5, 0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172,
        0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b, 0x35b5a8fa, 0x42b2986c,
        0xdbbbc9d6, 0xacbcf940, 0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59,
        0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116, 0x21b4f4b5, 0x56b3c423,
        0xcfba9599, 0xb8bda50f, 0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924,
        0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d, 0x76dc4190, 0x01db7106,
        0x98d220bc, 0xefd5102a, 0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433,
        0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818, 0x7f6a0dbb, 0x086d3d2d,
        0x91646c97, 0xe6635c01, 0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e,
        0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457, 0x65b0d9c6, 0x12b7e950,
        0x8bbeb8ea, 0xfcb9887c, 0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65,
        0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2, 0x4adfa541, 0x3dd895d7,
        0xa4d1c46d, 0xd3d6f4fb, 0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0,
        0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7822, 0x5005713c, 0x270241aa,
        0xbe0b1010, 0xc90c2086, 0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
        0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4, 0x59b33d17, 0x2eb40d81,
        0xb7bd5c3b, 0xc0ba6cad, 0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a,
        0xead54739, 0x9dd277af, 0x04db2615, 0x73dc1683, 0xe3630b12, 0x94643b84,
        0x0d6d6a3e, 0x7a6a5aa8, 0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1,
        0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe, 0xf762575d, 0x806567cb,
        0x196c3671, 0x6e6b06e7, 0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc,
        0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5, 0xd6d6a3e8, 0xa1d1937e,
        0x38d8c2c4, 0x4fdff252, 0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b,
        0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60, 0xdf60efc3, 0xa867df55,
        0x316e8eef, 0x4669be79, 0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236,
        0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f, 0xc5ba3bbe, 0xb2bd0b28,
        0x2bb45a92, 0x5cb36a04, 0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d,
        0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a, 0x9c0906a9, 0xeb0e363f,
        0x72076785, 0x05005713, 0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38,
        0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21, 0x86d3d2d4, 0xf1d4e242,
        0x68ddb3f8, 0x1fda836e, 0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777,
        0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c, 0x8f659eff, 0xf862ae69,
        0x616bffd3, 0x166ccf45, 0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2,
        0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db, 0xaed16a4a, 0xd9d65adc,
        0x40df0b66, 0x37d83bf0, 0xa9bcae53, 0xdede86c5, 0x47d7897f, 0x30d0b4e9,
        0xe0e80bb2, 0x97ef6bf6, 0x0ee70b4c, 0x79e01bda, 0xe767dc79, 0x9060dcef,
        0x09697c55, 0x7e6e5cc3, 0xeedf0052, 0x99d83050, 0x00d130ea, 0x77d6a07c,
        0xe81f3cdf, 0x9f188e49, 0x06112ff3, 0x71166f65
    };
    const uint8_t *buf = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) {
        crc = table[(crc ^ buf[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFF;
}

/* FNV-1a 64-bit hash */
static inline uint64_t fnv1a_hash(const char *s) {
    uint64_t h = 14695981039346656037ULL;
    while (*s) {
        h ^= (uint8_t)*s++;
        h *= 1099511628211ULL;
    }
    return h;
}

/* Get cache file path. Buffer should be at least PATH_MAX bytes. */
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
    char path[PATH_MAX];
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

    /* Validate header */
    if (g_cache->magic != CACHE_MAGIC) {
        munmap(g_cache, sizeof(SizeCache));
        g_cache = NULL;
        close(g_cache_fd);
        g_cache_fd = -1;
        return;
    }

    /* Accept current version or previous version (without checksum) */
    if (g_cache->version != CACHE_VERSION && g_cache->version != 2) {
        munmap(g_cache, sizeof(SizeCache));
        g_cache = NULL;
        close(g_cache_fd);
        g_cache_fd = -1;
        return;
    }

    /* Validate checksum for version 3+ */
    if (g_cache->version >= 3) {
        uint32_t computed = cache_crc32(g_cache->entries, sizeof(g_cache->entries));
        if (computed != g_cache->checksum) {
            /* Cache corrupted - reject it */
            munmap(g_cache, sizeof(SizeCache));
            g_cache = NULL;
            close(g_cache_fd);
            g_cache_fd = -1;
            return;
        }
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

/* Lookup a cache entry by path. Returns pointer to entry or NULL if not found. */
static inline const CacheEntry *cache_lookup_entry(const char *path) {
    if (!g_cache) return NULL;

    uint64_t h = fnv1a_hash(path);
    uint32_t idx = h % CACHE_CAPACITY;

    for (int i = 0; i < MAX_PROBE_DEPTH; i++) {
        const CacheEntry *e = &g_cache->entries[(idx + i) % CACHE_CAPACITY];
        if (e->size == -1) return NULL;  /* Empty slot = not found */
        if (e->path_hash == h) {
            return e;
        }
    }
    return NULL;
}

/* Lookup size for a path in the cache. Returns size or -1 if not found. */
static inline int64_t cache_lookup(const char *path) {
    const CacheEntry *e = cache_lookup_entry(path);
    return e ? e->size : -1;
}

/* Lookup file count for a path in the cache. Returns count or -1 if not found. */
static inline int64_t cache_lookup_count(const char *path) {
    const CacheEntry *e = cache_lookup_entry(path);
    return e ? e->file_count : -1;
}

/* ============================================================================
 * Daemon-side functions (for ld.c)
 * ============================================================================ */

#ifdef CACHE_DAEMON

static SizeCache *d_cache = NULL;

static inline int cache_init(void) {
    char path[PATH_MAX], dir[PATH_MAX];
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
            if (d_cache && read(fd, d_cache, sizeof(SizeCache)) == sizeof(SizeCache)) {
                /* Accept version 2 or 3 */
                if (d_cache->magic == CACHE_MAGIC &&
                    (d_cache->version == CACHE_VERSION || d_cache->version == 2)) {
                    /* Validate checksum for v3+ */
                    if (d_cache->version >= 3) {
                        uint32_t computed = cache_crc32(d_cache->entries, sizeof(d_cache->entries));
                        if (computed != d_cache->checksum) {
                            /* Corrupted - start fresh */
                            free(d_cache);
                            d_cache = NULL;
                            close(fd);
                            goto create_new;
                        }
                    }
                    /* Upgrade v2 to v3 */
                    d_cache->version = CACHE_VERSION;
                    close(fd);
                    return 0;
                }
            }
            free(d_cache);
            d_cache = NULL;
        }
        close(fd);
    }

create_new:
    /* Create new cache */
    d_cache = calloc(1, sizeof(SizeCache));
    if (!d_cache) return -1;

    d_cache->magic = CACHE_MAGIC;
    d_cache->version = CACHE_VERSION;
    d_cache->capacity = CACHE_CAPACITY;
    d_cache->checksum = 0;

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

/* Store or update a cache entry. Returns 1 on success, 0 if table full. */
static inline int cache_store(const char *path, int64_t size, int64_t file_count) {
    if (!d_cache) return 0;

    uint64_t h = fnv1a_hash(path);
    uint32_t idx = h % CACHE_CAPACITY;

    /* Find existing or empty slot */
    for (int i = 0; i < MAX_PROBE_DEPTH; i++) {
        CacheEntry *e = &d_cache->entries[(idx + i) % CACHE_CAPACITY];
        if (e->size == -1 || e->path_hash == h) {
            if (e->size == -1) d_cache->count++;
            e->path_hash = h;
            e->size = size;
            e->file_count = file_count;
            e->timestamp = time(NULL);
            return 1;
        }
    }
    /* Table full in this region */
    return 0;
}

/* Save cache to disk (atomic write with checksum) */
static inline int cache_save(void) {
    if (!d_cache) return -1;

    /* Compute checksum before writing */
    d_cache->checksum = cache_crc32(d_cache->entries, sizeof(d_cache->entries));

    char path[PATH_MAX], tmp[PATH_MAX];
    cache_path(path, sizeof(path));
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);

    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return -1;

    ssize_t written = write(fd, d_cache, sizeof(SizeCache));
    close(fd);

    if (written != (ssize_t)sizeof(SizeCache)) {
        unlink(tmp);
        return -1;
    }

    return rename(tmp, path);
}

#endif /* CACHE_DAEMON */

#endif /* CACHE_H */
