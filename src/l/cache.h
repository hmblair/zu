/*
 * cache.h - SQLite-based directory size cache
 *
 * Uses SQLite for:
 * - ACID transactions (crash-safe)
 * - WAL mode (readers don't block writers)
 * - Automatic schema management
 * - Easy debugging with sqlite3 CLI
 */

#ifndef CACHE_H
#define CACHE_H

#include <sqlite3.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>

/* Cache entry structure */
typedef struct {
    int64_t size;
    int64_t file_count;
    int64_t dir_mtime;
} CacheEntry;

/* Get cache database path */
static inline void cache_path(char *buf, size_t len) {
    const char *home = getenv("HOME");
    snprintf(buf, len, "%s/.cache/l/sizes.db", home ? home : "/tmp");
}

/* ============================================================================
 * Client-side functions (for l.c) - read-only access
 * ============================================================================ */

#include <pthread.h>

static sqlite3 *g_db = NULL;
static sqlite3_stmt *g_lookup_stmt = NULL;
static pthread_mutex_t g_db_lock = PTHREAD_MUTEX_INITIALIZER;

/* Load cache (read-only, but needs write for WAL recovery) */
static inline int cache_load(void) {
    char path[PATH_MAX];
    cache_path(path, sizeof(path));

    /* Use READWRITE to allow WAL recovery, but we only read */
    int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_NOMUTEX;
    if (sqlite3_open_v2(path, &g_db, flags, NULL) != SQLITE_OK) {
        /* Fall back to readonly if file doesn't exist */
        if (sqlite3_open_v2(path, &g_db, SQLITE_OPEN_READONLY, NULL) != SQLITE_OK) {
            g_db = NULL;
            return -1;
        }
    }

    /* Set busy timeout to avoid conflicts with daemon */
    sqlite3_busy_timeout(g_db, 1000);

    /* Prepare lookup statement */
    const char *sql = "SELECT size, file_count, dir_mtime FROM sizes WHERE path = ?";
    if (sqlite3_prepare_v2(g_db, sql, -1, &g_lookup_stmt, NULL) != SQLITE_OK) {
        sqlite3_close(g_db);
        g_db = NULL;
        return -1;
    }

    return 0;
}

/* Look up a path in the cache (thread-safe) */
static inline int cache_lookup(const char *path, CacheEntry *out) {
    if (!g_db || !g_lookup_stmt) return 0;

    pthread_mutex_lock(&g_db_lock);

    sqlite3_reset(g_lookup_stmt);
    sqlite3_bind_text(g_lookup_stmt, 1, path, -1, SQLITE_STATIC);

    int found = 0;
    if (sqlite3_step(g_lookup_stmt) == SQLITE_ROW) {
        out->size = sqlite3_column_int64(g_lookup_stmt, 0);
        out->file_count = sqlite3_column_int64(g_lookup_stmt, 1);
        out->dir_mtime = sqlite3_column_int64(g_lookup_stmt, 2);
        found = 1;
    }

    pthread_mutex_unlock(&g_db_lock);
    return found;
}

/* Wrapper that returns pointer (for compatibility) */
static inline const CacheEntry *cache_lookup_entry(const char *path) {
    static __thread CacheEntry entry;  /* Thread-local storage */
    if (cache_lookup(path, &entry)) {
        return &entry;
    }
    return NULL;
}

/* Close the cache */
static inline void cache_unload(void) {
    if (g_lookup_stmt) {
        sqlite3_finalize(g_lookup_stmt);
        g_lookup_stmt = NULL;
    }
    if (g_db) {
        sqlite3_close(g_db);
        g_db = NULL;
    }
}

/* ============================================================================
 * Daemon-side functions (for ld.c) - read-write access
 * ============================================================================ */

#ifdef CACHE_DAEMON

static sqlite3 *d_db = NULL;
static sqlite3_stmt *d_insert_stmt = NULL;
static sqlite3_stmt *d_lookup_stmt = NULL;

/* Initialize cache for daemon (read-write) */
static inline int cache_init(void) {
    char path[PATH_MAX];
    cache_path(path, sizeof(path));

    /* Ensure directory exists */
    char dir[PATH_MAX];
    snprintf(dir, sizeof(dir), "%s/.cache/l", getenv("HOME") ?: "/tmp");
    mkdir(dir, 0755);

    if (sqlite3_open(path, &d_db) != SQLITE_OK) {
        return -1;
    }

    /* Configure for safety and performance */
    sqlite3_exec(d_db, "PRAGMA journal_mode=WAL", NULL, NULL, NULL);
    sqlite3_exec(d_db, "PRAGMA synchronous=NORMAL", NULL, NULL, NULL);
    sqlite3_exec(d_db, "PRAGMA busy_timeout=5000", NULL, NULL, NULL);

    /* Create table if not exists */
    const char *create_sql =
        "CREATE TABLE IF NOT EXISTS sizes ("
        "  path TEXT PRIMARY KEY NOT NULL,"
        "  size INTEGER NOT NULL,"
        "  file_count INTEGER NOT NULL,"
        "  dir_mtime INTEGER NOT NULL"
        ") WITHOUT ROWID";
    if (sqlite3_exec(d_db, create_sql, NULL, NULL, NULL) != SQLITE_OK) {
        sqlite3_close(d_db);
        d_db = NULL;
        return -1;
    }

    /* Prepare statements */
    const char *insert_sql =
        "INSERT OR REPLACE INTO sizes (path, size, file_count, dir_mtime) "
        "VALUES (?, ?, ?, ?)";
    if (sqlite3_prepare_v2(d_db, insert_sql, -1, &d_insert_stmt, NULL) != SQLITE_OK) {
        sqlite3_close(d_db);
        d_db = NULL;
        return -1;
    }

    const char *lookup_sql = "SELECT size, file_count, dir_mtime FROM sizes WHERE path = ?";
    if (sqlite3_prepare_v2(d_db, lookup_sql, -1, &d_lookup_stmt, NULL) != SQLITE_OK) {
        sqlite3_finalize(d_insert_stmt);
        sqlite3_close(d_db);
        d_db = NULL;
        return -1;
    }

    return 0;
}

/* Store a cache entry */
static inline int cache_store(const char *path, off_t size, long file_count, time_t dir_mtime) {
    if (!d_db || !d_insert_stmt) return -1;

    sqlite3_reset(d_insert_stmt);
    sqlite3_bind_text(d_insert_stmt, 1, path, -1, SQLITE_STATIC);
    sqlite3_bind_int64(d_insert_stmt, 2, size);
    sqlite3_bind_int64(d_insert_stmt, 3, file_count);
    sqlite3_bind_int64(d_insert_stmt, 4, dir_mtime);

    return sqlite3_step(d_insert_stmt) == SQLITE_DONE ? 0 : -1;
}

/* Look up entry (daemon side) */
static inline const CacheEntry *dcache_lookup_entry(const char *path) {
    static CacheEntry entry;

    if (!d_db || !d_lookup_stmt) return NULL;

    sqlite3_reset(d_lookup_stmt);
    sqlite3_bind_text(d_lookup_stmt, 1, path, -1, SQLITE_STATIC);

    if (sqlite3_step(d_lookup_stmt) == SQLITE_ROW) {
        entry.size = sqlite3_column_int64(d_lookup_stmt, 0);
        entry.file_count = sqlite3_column_int64(d_lookup_stmt, 1);
        entry.dir_mtime = sqlite3_column_int64(d_lookup_stmt, 2);
        return &entry;
    }

    return NULL;
}

/* Checkpoint WAL to main database */
static inline int cache_save(void) {
    if (!d_db) return -1;
    sqlite3_wal_checkpoint_v2(d_db, NULL, SQLITE_CHECKPOINT_PASSIVE, NULL, NULL);
    return 0;
}

/* Get entry count (for status display) */
static inline int cache_count(void) {
    if (!d_db) return 0;
    sqlite3_stmt *stmt;
    int count = 0;
    if (sqlite3_prepare_v2(d_db, "SELECT COUNT(*) FROM sizes", -1, &stmt, NULL) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            count = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }
    return count;
}

/* Free cache resources */
static inline void cache_free(void) {
    if (d_insert_stmt) {
        sqlite3_finalize(d_insert_stmt);
        d_insert_stmt = NULL;
    }
    if (d_lookup_stmt) {
        sqlite3_finalize(d_lookup_stmt);
        d_lookup_stmt = NULL;
    }
    if (d_db) {
        /* Final checkpoint before close */
        sqlite3_wal_checkpoint_v2(d_db, NULL, SQLITE_CHECKPOINT_TRUNCATE, NULL, NULL);
        sqlite3_close(d_db);
        d_db = NULL;
    }
}

#endif /* CACHE_DAEMON */

#endif /* CACHE_H */
