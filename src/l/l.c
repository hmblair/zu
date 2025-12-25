/*
 * l - Enhanced directory listing with tree view
 *
 * A fast, portable directory listing tool with tree visualization,
 * git integration, icons, and colors.
 *
 * Build: cc -O2 -Wall -Wextra -std=c99 -o l l.c
 */

/* ============================================================================
 * Platform Detection & Includes
 * ============================================================================ */

#define _DEFAULT_SOURCE  /* For DT_* constants on Linux */
#define _BSD_SOURCE      /* For compatibility */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <limits.h>
#include <ctype.h>
#include <time.h>
#include <omp.h>
#include "cache.h"
#include "dir_stats.h"

#if defined(__APPLE__) && defined(__MACH__)
    #define PLATFORM_MACOS 1
    #define GET_MTIME(st) ((st).st_mtimespec.tv_sec)
#elif defined(__linux__)
    #define PLATFORM_LINUX 1
    #define GET_MTIME(st) ((st).st_mtim.tv_sec)
#elif defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) || defined(__DragonFly__)
    #define PLATFORM_BSD 1
    #define GET_MTIME(st) ((st).st_mtimespec.tv_sec)
#else
    #define PLATFORM_GENERIC 1
    #define GET_MTIME(st) ((st).st_mtime)
#endif

#ifndef PATH_MAX
    #define PATH_MAX 4096
#endif

/* ============================================================================
 * Constants
 * ============================================================================ */

#define MAX_DEPTH 50
#define MAX_ICON_LEN 16
#define MAX_EXT_LEN 16
#define MAX_EXT_ICONS 256
#define HASH_SIZE 4096
#define INITIAL_FILE_CAPACITY 64
#define LINE_COUNT_LIMIT 10000
#define LINE_COUNT_EXCEEDED -2
#define READ_BUFFER_SIZE 65536
#define BINARY_CHECK_SIZE 512

/* Tree drawing characters (UTF-8) */
#define TREE_VERT   "│  "
#define TREE_BRANCH "├─ "
#define TREE_LAST   "└─ "
#define TREE_SPACE  "   "

/* ============================================================================
 * ANSI Color Codes
 * ============================================================================ */

static const char *COLOR_RESET      = "\033[0m";
static const char *COLOR_RED        = "\033[0;31m";
static const char *COLOR_GREEN      = "\033[0;32m";
static const char *COLOR_YELLOW     = "\033[0;33m";
static const char *COLOR_BLUE       = "\033[0;34m";
static const char *COLOR_CYAN       = "\033[0;36m";
static const char *COLOR_GREY       = "\033[90m";
static const char *COLOR_WHITE      = "\033[0;37m";
static const char *COLOR_YELLOW_BOLD = "\033[1;33m";
static const char *STYLE_ITALIC     = "\033[3m";

/* ============================================================================
 * Data Structures
 * ============================================================================ */

typedef enum {
    SORT_NONE,
    SORT_SIZE,
    SORT_TIME,
    SORT_NAME
} SortMode;

typedef enum {
    FTYPE_UNKNOWN,
    FTYPE_DIR,
    FTYPE_FILE,
    FTYPE_EXEC,
    FTYPE_SYMLINK,
    FTYPE_SYMLINK_DIR,
    FTYPE_SYMLINK_EXEC,
    FTYPE_SYMLINK_BROKEN
} FileType;

typedef struct {
    int max_depth;
    int show_hidden;
    int long_format;
    int expand_all;
    int list_mode;
    int no_icons;
    int sort_reverse;
    int is_tty;
    SortMode sort_by;
    /* Environment paths */
    char cwd[PATH_MAX];
    char home[PATH_MAX];
    char script_dir[PATH_MAX];
} Config;

/* Forward declaration for Column formatter */
typedef struct FileEntry FileEntry;

/* Column definition for long format output */
typedef void (*ColumnFormatter)(const FileEntry *fe, char *buf, size_t len);

typedef struct {
    const char *name;        /* Column identifier */
    int width;               /* Current max width (computed during tree build) */
    ColumnFormatter format;  /* Function to format value */
} Column;

/* Number of columns in long format */
#define NUM_COLUMNS 3
#define COL_SIZE  0
#define COL_LINES 1
#define COL_TIME  2

typedef struct {
    char ext[MAX_EXT_LEN];
    char icon[MAX_ICON_LEN];
} ExtIcon;

typedef struct {
    char default_icon[MAX_ICON_LEN];
    char symlink[MAX_ICON_LEN];
    char symlink_dir[MAX_ICON_LEN];
    char symlink_exec[MAX_ICON_LEN];
    char symlink_file[MAX_ICON_LEN];
    char symlink_broken[MAX_ICON_LEN];
    char directory[MAX_ICON_LEN];
    char current_dir[MAX_ICON_LEN];
    char locked_dir[MAX_ICON_LEN];
    char executable[MAX_ICON_LEN];
    char file[MAX_ICON_LEN];
    char git_modified[MAX_ICON_LEN];
    char git_untracked[MAX_ICON_LEN];
    char git_staged[MAX_ICON_LEN];
    char git_deleted[MAX_ICON_LEN];
    char readonly[MAX_ICON_LEN];
    ExtIcon ext_icons[MAX_EXT_ICONS];
    int ext_count;
} Icons;

/*
 * FileEntry - Represents a file system entry.
 *
 * Ownership:
 *   - path: Owned by this struct, freed by file_entry_free()
 *   - name: Pointer into path, not separately freed
 *   - symlink_target: Owned by this struct, freed by file_entry_free()
 *
 * Lifecycle:
 *   - Created by read_directory() or build_tree()
 *   - Freed by file_entry_free() or tree_node_free()
 *   - When moving from FileList to TreeNode, ownership transfers (don't call file_list_free)
 */
struct FileEntry {
    char *path;              /* Full absolute path (owned) */
    char *name;              /* Pointer to basename within path (not owned) */
    char *symlink_target;    /* Resolved symlink target (owned, NULL if not symlink) */
    mode_t mode;
    off_t size;
    long file_count;         /* -1 if not computed */
    time_t mtime;
    int line_count;          /* -1 if not computed */
    int is_ignored;
    char git_status[3];      /* e.g., "??", " M", "A " */
    FileType type;
};

typedef struct {
    FileEntry *entries;
    size_t count;
    size_t capacity;
} FileList;

typedef struct TreeNode {
    FileEntry entry;
    struct TreeNode *children;
    size_t child_count;
} TreeNode;

typedef struct GitStatusNode {
    char *path;
    char status[3];
    struct GitStatusNode *next;
} GitStatusNode;

typedef struct {
    GitStatusNode *buckets[HASH_SIZE];
    char *git_root;
    int is_git_repo;
} GitCache;

typedef struct {
    GitCache *git;
    const Icons *icons;
    const Config *cfg;
    Column *columns;         /* Array of NUM_COLUMNS columns (NULL if not long format) */
    int *continuation;
} PrintContext;

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

static void die(const char *msg) {
    int tty = isatty(STDERR_FILENO);
    fprintf(stderr, "%sError:%s %s\n",
            tty ? COLOR_RED : "", tty ? COLOR_RESET : "", msg);
    exit(1);
}

/* Duplicate string, die on failure. Returns: Newly allocated string (caller must free). */
static char *xstrdup(const char *s) {
    char *dup = strdup(s);
    if (!dup) die("Out of memory");
    return dup;
}

/* Allocate memory, die on failure. Returns: Newly allocated memory (caller must free). */
static void *xmalloc(size_t size) {
    void *p = malloc(size);
    if (!p) die("Out of memory");
    return p;
}

/* Reallocate memory, die on failure. Returns: Reallocated memory (caller must free). */
static void *xrealloc(void *ptr, size_t size) {
    void *p = realloc(ptr, size);
    if (!p) die("Out of memory");
    return p;
}

/* Simple djb2 hash */
static unsigned int hash_string(const char *str) {
    unsigned int hash = 5381;
    int c;
    while ((c = *str++))
        hash = ((hash << 5) + hash) + c;
    return hash % HASH_SIZE;
}

/* Get absolute path, resolving symlinks */
static void get_realpath(const char *path, char *resolved, const Config *cfg) {
    char *rp = realpath(path, resolved);
    if (!rp) {
        /* If realpath fails, try to at least get the absolute path */
        if (path[0] == '/') {
            strncpy(resolved, path, PATH_MAX - 1);
            resolved[PATH_MAX - 1] = '\0';
            return;
        }
        snprintf(resolved, PATH_MAX, "%s/%s", cfg->cwd, path);
    }
}

/* Get absolute path WITHOUT resolving symlinks, but normalize . and .. */
static void get_abspath(const char *path, char *resolved, const Config *cfg) {
    char tmp[PATH_MAX];

    /* Make absolute */
    if (path[0] == '/') {
        strncpy(tmp, path, PATH_MAX - 1);
        tmp[PATH_MAX - 1] = '\0';
    } else {
        snprintf(tmp, PATH_MAX, "%s/%s", cfg->cwd, path);
    }

    /* Normalize . and .. components */
    char *components[PATH_MAX / 2];
    int depth = 0;
    int max_depth = PATH_MAX / 2;

    char *p = tmp;
    while (*p) {
        while (*p == '/') p++;  /* Skip slashes */
        if (*p == '\0') break;

        char *start = p;
        while (*p && *p != '/') p++;  /* Find end of component */

        size_t len = (size_t)(p - start);
        if (len == 1 && start[0] == '.') {
            /* Skip . */
            continue;
        } else if (len == 2 && start[0] == '.' && start[1] == '.') {
            /* Go up for .. */
            if (depth > 0) depth--;
        } else if (depth < max_depth) {
            /* Save component (with bounds check) */
            components[depth] = start;
            if (*p) *p++ = '\0';  /* Null-terminate */
            depth++;
        }
    }

    /* Rebuild path with bounds checking */
    if (depth == 0) {
        strcpy(resolved, "/");
    } else {
        size_t pos = 0;
        for (int i = 0; i < depth && pos < PATH_MAX - 1; i++) {
            size_t comp_len = strlen(components[i]);
            if (pos + 1 + comp_len >= PATH_MAX) break;
            resolved[pos++] = '/';
            memcpy(resolved + pos, components[i], comp_len);
            pos += comp_len;
        }
        resolved[pos] = '\0';
        if (pos == 0) strcpy(resolved, "/");
    }
}

/* Abbreviate home directory with ~ */
static void abbreviate_home(const char *path, char *buf, size_t len, const Config *cfg) {
    size_t home_len = strlen(cfg->home);
    if (strncmp(path, cfg->home, home_len) == 0 &&
        (path[home_len] == '/' || path[home_len] == '\0')) {
        snprintf(buf, len, "~%s", path + home_len);
    } else {
        strncpy(buf, path, len - 1);
        buf[len - 1] = '\0';
    }
}

/* Resolve executable path to find source directory */
static void resolve_source_dir(const char *argv0, char *src_dir, size_t len) {
    char exe_abs[PATH_MAX];
    int found = 0;

    if (strchr(argv0, '/')) {
        /* Has path component - resolve it */
        if (realpath(argv0, exe_abs) != NULL) found = 1;
    } else {
        /* No path - search PATH */
        char *path_env = getenv("PATH");
        if (path_env) {
            char *path_copy = xstrdup(path_env);
            char *dir = strtok(path_copy, ":");
            while (dir) {
                char try_path[PATH_MAX];
                snprintf(try_path, sizeof(try_path), "%s/%s", dir, argv0);
                if (access(try_path, X_OK) == 0 && realpath(try_path, exe_abs) != NULL) {
                    found = 1;
                    break;
                }
                dir = strtok(NULL, ":");
            }
            free(path_copy);
        }
    }

    if (found) {
        char *slash = strrchr(exe_abs, '/');
        if (slash) {
            *slash = '\0';
            snprintf(src_dir, len, "%s/../src/l", exe_abs);
            return;
        }
    }
    strncpy(src_dir, "../src/l", len - 1);
    src_dir[len - 1] = '\0';
}

/* Format size in human-readable form */
static void format_size(off_t bytes, char *buf, size_t len) {
    const char *units[] = {"B", "K", "M", "G", "T", "P"};
    int unit_idx = 0;
    double size = (double)bytes;

    while (size >= 1024 && unit_idx < 5) {
        size /= 1024;
        unit_idx++;
    }

    if (unit_idx == 0) {
        snprintf(buf, len, "%lld%s", (long long)bytes, units[0]);
    } else if (size < 10) {
        snprintf(buf, len, "%.1f%s", size, units[unit_idx]);
    } else {
        snprintf(buf, len, "%.0f%s", size, units[unit_idx]);
    }
}

/* Format time as relative or short date */
static void format_relative_time(time_t mtime, char *buf, size_t len) {
    time_t now = time(NULL);
    long diff = (long)(now - mtime);

    if (diff < 60) {
        snprintf(buf, len, "now");
    } else if (diff < 3600) {
        snprintf(buf, len, "%ldm ago", diff / 60);
    } else if (diff < 86400) {
        snprintf(buf, len, "%ldh ago", diff / 3600);
    } else if (diff < 604800) {
        snprintf(buf, len, "%ldd ago", diff / 86400);
    } else {
        struct tm *tm = localtime(&mtime);
        strftime(buf, len, "%b %d", tm);
    }
}

/* ============================================================================
 * Column Formatters
 * ============================================================================ */

static void col_format_size(const FileEntry *fe, char *buf, size_t len) {
    if (fe->size < 0) {
        snprintf(buf, len, "-");
    } else {
        format_size(fe->size, buf, len);
    }
}

static void col_format_lines(const FileEntry *fe, char *buf, size_t len) {
    if (fe->file_count >= 0) {
        /* Directory: show file count */
        if (fe->file_count >= 1000000) {
            snprintf(buf, len, "%.1fM", fe->file_count / 1000000.0);
        } else if (fe->file_count >= 1000) {
            snprintf(buf, len, "%.1fK", fe->file_count / 1000.0);
        } else {
            snprintf(buf, len, "%ld", fe->file_count);
        }
    } else if (fe->line_count == LINE_COUNT_EXCEEDED) {
        snprintf(buf, len, ">10K");
    } else if (fe->line_count >= 0) {
        snprintf(buf, len, "%d", fe->line_count);
    } else {
        snprintf(buf, len, "-");
    }
}

static void col_format_time(const FileEntry *fe, char *buf, size_t len) {
    format_relative_time(fe->mtime, buf, len);
}

/* Initialize column definitions */
static void columns_init(Column *cols) {
    cols[COL_SIZE].name = "size";
    cols[COL_SIZE].width = 1;
    cols[COL_SIZE].format = col_format_size;

    cols[COL_LINES].name = "lines";
    cols[COL_LINES].width = 1;
    cols[COL_LINES].format = col_format_lines;

    cols[COL_TIME].name = "time";
    cols[COL_TIME].width = 1;
    cols[COL_TIME].format = col_format_time;
}

/* Update column widths from a file entry */
static void columns_update_widths(Column *cols, const FileEntry *fe) {
    char buf[32];
    for (int i = 0; i < NUM_COLUMNS; i++) {
        cols[i].format(fe, buf, sizeof(buf));
        int len = (int)strlen(buf);
        if (len > cols[i].width) cols[i].width = len;
    }
}

/* Cache lookup wrapper for dir_stats.h */
static int cache_lookup_wrapper(const char *path, off_t *size, long *count) {
    const CacheEntry *cached = cache_lookup_entry(path);
    if (cached && cached->size >= 0 && cached->file_count >= 0) {
        *size = (off_t)cached->size;
        *count = (long)cached->file_count;
        return 1;
    }
    return 0;
}

/* Get directory stats with cache lookup */
static DirStats get_dir_stats(const char *path) {
    /* Resolve symlinks for cache lookup (cache stores real paths) */
    char resolved[PATH_MAX];
    const char *lookup_path = path;
    if (realpath(path, resolved) != NULL) {
        lookup_path = resolved;
    }

    /* Check cache first for the top-level call */
    off_t size;
    long count;
    if (cache_lookup_wrapper(lookup_path, &size, &count)) {
        return (DirStats){size, count};
    }
    return dir_stats_get(path, cache_lookup_wrapper);
}


/* ============================================================================
 * File List Management
 * ============================================================================ */

static void file_list_init(FileList *list) {
    list->entries = NULL;
    list->count = 0;
    list->capacity = 0;
}

static void file_list_add(FileList *list, FileEntry *entry) {
    if (list->count >= list->capacity) {
        list->capacity = list->capacity ? list->capacity * 2 : INITIAL_FILE_CAPACITY;
        list->entries = xrealloc(list->entries, list->capacity * sizeof(FileEntry));
    }
    list->entries[list->count++] = *entry;
}

static void file_entry_free(FileEntry *entry) {
    free(entry->path);
    free(entry->symlink_target);
}

static void file_list_free(FileList *list) {
    for (size_t i = 0; i < list->count; i++) {
        file_entry_free(&list->entries[i]);
    }
    free(list->entries);
    list->entries = NULL;
    list->count = 0;
    list->capacity = 0;
}

/* ============================================================================
 * File Type Detection
 * ============================================================================ */

static FileType detect_file_type(const char *path, struct stat *st,
                                  char **symlink_target) {
    struct stat lst;
    *symlink_target = NULL;

    /* Use lstat to detect symlinks */
    if (lstat(path, &lst) != 0) {
        return FTYPE_UNKNOWN;
    }

    *st = lst;

    if (S_ISLNK(lst.st_mode)) {
        /* Read symlink target */
        char target[PATH_MAX];
        ssize_t len = readlink(path, target, sizeof(target) - 1);
        if (len > 0) {
            target[len] = '\0';

            /* Resolve to absolute path and normalize */
            char abs_target[PATH_MAX];
            if (realpath(path, abs_target) != NULL) {
                /* realpath resolves the symlink to its final target */
                *symlink_target = xstrdup(abs_target);
            } else {
                /* Fallback: manually build absolute path */
                if (target[0] != '/') {
                    char dir[PATH_MAX];
                    strncpy(dir, path, PATH_MAX - 1);
                    dir[PATH_MAX - 1] = '\0';
                    char *slash = strrchr(dir, '/');
                    if (slash) {
                        slash[1] = '\0';
                        snprintf(abs_target, PATH_MAX, "%s%s", dir, target);
                    } else {
                        strncpy(abs_target, target, PATH_MAX - 1);
                        abs_target[PATH_MAX - 1] = '\0';
                    }
                } else {
                    strncpy(abs_target, target, PATH_MAX - 1);
                    abs_target[PATH_MAX - 1] = '\0';
                }
                *symlink_target = xstrdup(abs_target);
            }

            /* Check target type and use target's stat for display */
            struct stat target_st;
            if (stat(path, &target_st) == 0) {
                *st = target_st;  /* Use target's stats for size display */
                if (S_ISDIR(target_st.st_mode)) {
                    return FTYPE_SYMLINK_DIR;
                } else if (target_st.st_mode & S_IXUSR) {
                    return FTYPE_SYMLINK_EXEC;
                } else {
                    return FTYPE_SYMLINK;
                }
            } else {
                return FTYPE_SYMLINK_BROKEN;
            }
        } else {
            return FTYPE_SYMLINK_BROKEN;
        }
    } else if (S_ISDIR(lst.st_mode)) {
        return FTYPE_DIR;
    } else if (!S_ISREG(lst.st_mode)) {
        /* Device files, sockets, FIFOs, etc. */
        return FTYPE_UNKNOWN;
    } else if (lst.st_mode & S_IXUSR) {
        return FTYPE_EXEC;
    } else {
        return FTYPE_FILE;
    }
}

/* Get color for file type */
static const char *get_file_color(FileType type, int is_cwd, int is_ignored,
                                   const Config *cfg) {
    if (!cfg->is_tty) return "";

    if (is_cwd) return COLOR_YELLOW_BOLD;
    if (is_ignored) return COLOR_GREY;

    switch (type) {
        case FTYPE_DIR:           return COLOR_BLUE;
        case FTYPE_EXEC:          return COLOR_GREEN;
        case FTYPE_SYMLINK:       return COLOR_WHITE;
        case FTYPE_SYMLINK_DIR:   return COLOR_CYAN;
        case FTYPE_SYMLINK_EXEC:  return COLOR_GREEN;
        case FTYPE_SYMLINK_BROKEN: return COLOR_RED;
        default:                  return COLOR_WHITE;
    }
}

/* ============================================================================
 * Line Counting
 * ============================================================================ */

static int has_binary_extension(const char *path) {
    const char *dot = strrchr(path, '/');
    dot = dot ? strrchr(dot, '.') : strrchr(path, '.');
    if (!dot) return 0;
    dot++;  /* skip the dot */

    /* Common binary extensions */
    static const char *exts[] = {
        "pdf", "png", "jpg", "jpeg", "gif", "bmp", "ico", "webp", "svg",
        "mp3", "mp4", "wav", "flac", "ogg", "avi", "mkv", "mov", "webm",
        "zip", "tar", "gz", "bz2", "xz", "7z", "rar", "dmg", "iso",
        "exe", "dll", "so", "dylib", "o", "a", "class", "pyc",
        "ttf", "otf", "woff", "woff2", "eot",
        "doc", "docx", "xls", "xlsx", "ppt", "pptx", "odt", "ods",
        "sqlite", "db", "bin", "dat",
        NULL
    };
    for (const char **e = exts; *e; e++) {
        if (strcasecmp(dot, *e) == 0) return 1;
    }
    return 0;
}

static int is_binary_file(FILE *f) {
    unsigned char buf[BINARY_CHECK_SIZE];
    size_t n = fread(buf, 1, sizeof(buf), f);
    rewind(f);
    for (size_t i = 0; i < n; i++) {
        if (buf[i] == '\0') return 1;
    }
    return 0;
}

static int count_file_lines(const char *path) {
    if (has_binary_extension(path)) return -1;

    FILE *f = fopen(path, "r");
    if (!f) return -1;

    if (is_binary_file(f)) {
        fclose(f);
        return -1;
    }

    int count = 0;
    char buf[READ_BUFFER_SIZE];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        for (size_t i = 0; i < n; i++) {
            if (buf[i] == '\n') {
                count++;
                if (count > LINE_COUNT_LIMIT) {
                    fclose(f);
                    return LINE_COUNT_EXCEEDED;
                }
            }
        }
    }
    fclose(f);
    return count;
}

/* ============================================================================
 * Icons (TOML Parser)
 * ============================================================================ */

static void icons_init_defaults(Icons *icons) {
    memset(icons, 0, sizeof(Icons));
    /* Default icons (Nerd Font) */
    strcpy(icons->default_icon, "");
    strcpy(icons->symlink, "");
    strcpy(icons->directory, "");
    strcpy(icons->current_dir, "");
    strcpy(icons->locked_dir, "󰉐");
    strcpy(icons->file, "");
    strcpy(icons->executable, "");
    strcpy(icons->symlink_dir, "");
    strcpy(icons->symlink_exec, "");
    strcpy(icons->symlink_file, "");
    strcpy(icons->symlink_broken, "");
    strcpy(icons->git_modified, "");
    strcpy(icons->git_untracked, "󰛑");
    strcpy(icons->git_staged, "");
    strcpy(icons->git_deleted, "");
    strcpy(icons->readonly, "");
    icons->ext_count = 0;
}

static int parse_toml_line(const char *line, char *key, size_t key_len,
                           char *value, size_t value_len) {
    const char *p = line;

    /* Skip whitespace */
    while (*p && isspace(*p)) p++;

    /* Skip comments and empty lines */
    if (*p == '#' || *p == '\0') return 0;

    /* Parse key */
    const char *key_start = p;
    while (*p && *p != '=' && !isspace(*p)) p++;
    size_t klen = p - key_start;
    if (klen == 0 || klen >= key_len) return 0;

    memcpy(key, key_start, klen);
    key[klen] = '\0';

    /* Skip to = */
    while (*p && isspace(*p)) p++;
    if (*p != '=') return 0;
    p++;

    /* Skip to value */
    while (*p && isspace(*p)) p++;
    if (*p != '"') return 0;
    p++;

    /* Parse quoted value */
    const char *val_start = p;
    while (*p && *p != '"') p++;
    size_t vlen = p - val_start;
    if (vlen >= value_len) return 0;

    memcpy(value, val_start, vlen);
    value[vlen] = '\0';

    return 1;
}

static void icons_load(Icons *icons, const char *script_dir) {
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/icons.toml", script_dir);

    FILE *f = fopen(path, "r");
    if (!f) return;

    char line[256];
    char key[64], value[MAX_ICON_LEN];
    int in_extensions = 0;

    while (fgets(line, sizeof(line), f)) {
        /* Check for section header */
        const char *p = line;
        while (*p && isspace(*p)) p++;
        if (*p == '[') {
            in_extensions = (strncmp(p, "[extensions]", 12) == 0);
            continue;
        }

        if (!parse_toml_line(line, key, sizeof(key), value, sizeof(value)))
            continue;

        if (in_extensions) {
            /* Add extension icon */
            if (icons->ext_count < MAX_EXT_ICONS) {
                strncpy(icons->ext_icons[icons->ext_count].ext, key, MAX_EXT_LEN - 1);
                icons->ext_icons[icons->ext_count].ext[MAX_EXT_LEN - 1] = '\0';
                strncpy(icons->ext_icons[icons->ext_count].icon, value, MAX_ICON_LEN - 1);
                icons->ext_icons[icons->ext_count].icon[MAX_ICON_LEN - 1] = '\0';
                icons->ext_count++;
            }
        } else {
            if (strcmp(key, "default") == 0) strcpy(icons->default_icon, value);
            else if (strcmp(key, "directory") == 0) strcpy(icons->directory, value);
            else if (strcmp(key, "current_dir") == 0) strcpy(icons->current_dir, value);
            else if (strcmp(key, "locked_dir") == 0) strcpy(icons->locked_dir, value);
            else if (strcmp(key, "file") == 0) strcpy(icons->file, value);
            else if (strcmp(key, "executable") == 0) strcpy(icons->executable, value);
            else if (strcmp(key, "symlink") == 0) strcpy(icons->symlink, value);
            else if (strcmp(key, "symlink_dir") == 0) strcpy(icons->symlink_dir, value);
            else if (strcmp(key, "symlink_exec") == 0) strcpy(icons->symlink_exec, value);
            else if (strcmp(key, "symlink_file") == 0) strcpy(icons->symlink_file, value);
            else if (strcmp(key, "symlink_broken") == 0) strcpy(icons->symlink_broken, value);
            else if (strcmp(key, "git_modified") == 0) strcpy(icons->git_modified, value);
            else if (strcmp(key, "git_untracked") == 0) strcpy(icons->git_untracked, value);
            else if (strcmp(key, "git_staged") == 0) strcpy(icons->git_staged, value);
            else if (strcmp(key, "git_deleted") == 0) strcpy(icons->git_deleted, value);
            else if (strcmp(key, "readonly") == 0) strcpy(icons->readonly, value);
        }
    }

    fclose(f);
}

static const char *get_ext_icon(const Icons *icons, const char *name) {
    const char *dot = strrchr(name, '.');
    if (!dot || dot == name) return NULL;

    const char *ext = dot + 1;
    for (int i = 0; i < icons->ext_count; i++) {
        if (strcmp(ext, icons->ext_icons[i].ext) == 0) {
            return icons->ext_icons[i].icon;
        }
    }
    return NULL;
}

static const char *get_icon(const Icons *icons, FileType type, int is_cwd,
                            int is_unreadable, const char *name) {
    switch (type) {
        case FTYPE_DIR:
            if (is_unreadable) return icons->locked_dir;
            return is_cwd ? icons->current_dir : icons->directory;
        case FTYPE_FILE: {
            const char *ext_icon = get_ext_icon(icons, name);
            return ext_icon ? ext_icon : icons->file;
        }
        case FTYPE_EXEC:
            return icons->executable;
        case FTYPE_SYMLINK:
            return icons->symlink_file[0] ? icons->symlink_file : icons->symlink;
        case FTYPE_SYMLINK_DIR:
            return icons->symlink_dir[0] ? icons->symlink_dir : icons->symlink;
        case FTYPE_SYMLINK_EXEC:
            return icons->symlink_exec[0] ? icons->symlink_exec : icons->symlink;
        case FTYPE_SYMLINK_BROKEN:
            return icons->symlink_broken[0] ? icons->symlink_broken : icons->symlink;
        case FTYPE_UNKNOWN:
        default:
            return icons->default_icon;
    }
}

/* ============================================================================
 * Git Integration
 * ============================================================================ */

/*
 * Escape a path for safe use in shell single quotes: ' -> '\''
 *
 * Returns: Newly allocated string (caller must free).
 */
static char *shell_escape(const char *path) {
    /* Count single quotes to determine buffer size */
    size_t quotes = 0;
    for (const char *p = path; *p; p++) {
        if (*p == '\'') quotes++;
    }

    /* Allocate: original length + 3 extra chars per quote + null */
    size_t len = strlen(path) + quotes * 3 + 1;
    char *escaped = xmalloc(len);
    char *out = escaped;

    for (const char *p = path; *p; p++) {
        if (*p == '\'') {
            /* End quote, escaped quote, start quote: '\'' */
            *out++ = '\'';
            *out++ = '\\';
            *out++ = '\'';
            *out++ = '\'';
        } else {
            *out++ = *p;
        }
    }
    *out = '\0';
    return escaped;
}

static void git_cache_init(GitCache *cache) {
    memset(cache->buckets, 0, sizeof(cache->buckets));
    cache->git_root = NULL;
    cache->is_git_repo = 0;
}

static void git_cache_free(GitCache *cache) {
    for (int i = 0; i < HASH_SIZE; i++) {
        GitStatusNode *node = cache->buckets[i];
        while (node) {
            GitStatusNode *next = node->next;
            free(node->path);
            free(node);
            node = next;
        }
        cache->buckets[i] = NULL;
    }
    free(cache->git_root);
    cache->git_root = NULL;
    cache->is_git_repo = 0;
}

static void git_cache_add(GitCache *cache, const char *path, const char *status) {
    unsigned int h = hash_string(path);
    GitStatusNode *node = xmalloc(sizeof(GitStatusNode));
    node->path = xstrdup(path);
    strncpy(node->status, status, 2);
    node->status[2] = '\0';
    node->next = cache->buckets[h];
    cache->buckets[h] = node;
}

static const char *git_cache_get(GitCache *cache, const char *path) {
    unsigned int h = hash_string(path);
    GitStatusNode *node = cache->buckets[h];
    while (node) {
        if (strcmp(node->path, path) == 0) {
            return node->status;
        }
        node = node->next;
    }
    return NULL;
}

static void git_detect_repo(GitCache *cache, const char *dir) {
    char *escaped = shell_escape(dir);
    char cmd[PATH_MAX * 2 + 64];
    snprintf(cmd, sizeof(cmd), "git -C '%s' rev-parse --show-toplevel 2>/dev/null", escaped);
    free(escaped);

    FILE *fp = popen(cmd, "r");
    if (!fp) return;

    char root[PATH_MAX];
    if (fgets(root, sizeof(root), fp)) {
        /* Remove trailing newline */
        size_t len = strlen(root);
        if (len > 0 && root[len - 1] == '\n') root[len - 1] = '\0';
        cache->git_root = xstrdup(root);
        cache->is_git_repo = 1;
    }

    pclose(fp);
}

static void git_populate_status(GitCache *cache) {
    if (!cache->is_git_repo) return;

    char *escaped = shell_escape(cache->git_root);
    char cmd[PATH_MAX * 2 + 64];
    snprintf(cmd, sizeof(cmd), "git -C '%s' status --porcelain --ignored 2>/dev/null", escaped);
    free(escaped);

    FILE *fp = popen(cmd, "r");
    if (!fp) return;

    char line[PATH_MAX + 8];
    while (fgets(line, sizeof(line), fp)) {
        /* Remove trailing newline */
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') line[len - 1] = '\0';

        if (len < 4) continue;  /* Need at least "XY path" */

        char status[3] = {line[0], line[1], '\0'};
        const char *path = line + 3;

        /* Handle renamed files: "R  old -> new" */
        const char *arrow = strstr(path, " -> ");
        if (arrow) {
            path = arrow + 4;
        }

        /* Remove trailing slash from directories */
        char full_path[PATH_MAX];
        snprintf(full_path, sizeof(full_path), "%s/%s", cache->git_root, path);
        len = strlen(full_path);
        if (len > 0 && full_path[len - 1] == '/') full_path[len - 1] = '\0';

        git_cache_add(cache, full_path, status);
    }

    pclose(fp);
}

static const char *get_git_indicator(GitCache *cache, const char *path,
                                      const Icons *icons, const Config *cfg) {
    static char indicator[32];
    indicator[0] = '\0';

    if (!cache->is_git_repo || cfg->no_icons) return indicator;

    const char *status = git_cache_get(cache, path);
    if (!status) return indicator;

    if (strcmp(status, "!!") == 0) {
        /* Ignored - no indicator */
        return indicator;
    } else if (strcmp(status, "??") == 0) {
        snprintf(indicator, sizeof(indicator), "%s%s%s ",
                 cfg->is_tty ? COLOR_RED : "", icons->git_untracked,
                 cfg->is_tty ? COLOR_RESET : "");
    } else if (status[0] != ' ' && status[0] != '?' && status[0] != '!') {
        /* Staged */
        snprintf(indicator, sizeof(indicator), "%s%s%s ",
                 cfg->is_tty ? COLOR_YELLOW : "", icons->git_staged,
                 cfg->is_tty ? COLOR_RESET : "");
    } else if (status[1] == 'M') {
        /* Modified */
        snprintf(indicator, sizeof(indicator), "%s%s%s ",
                 cfg->is_tty ? COLOR_RED : "", icons->git_modified,
                 cfg->is_tty ? COLOR_RESET : "");
    } else if (status[1] == 'D') {
        /* Deleted */
        snprintf(indicator, sizeof(indicator), "%s%s%s ",
                 cfg->is_tty ? COLOR_RED : "", icons->git_deleted,
                 cfg->is_tty ? COLOR_RESET : "");
    }

    return indicator;
}

/* ============================================================================
 * Directory Reading
 * ============================================================================ */

static int entry_cmp_name(const void *a, const void *b) {
    const FileEntry *ea = (const FileEntry *)a;
    const FileEntry *eb = (const FileEntry *)b;
    return strcasecmp(ea->name, eb->name);
}

static int entry_cmp_size(const void *a, const void *b) {
    const FileEntry *ea = (const FileEntry *)a;
    const FileEntry *eb = (const FileEntry *)b;
    if (eb->size > ea->size) return 1;
    if (eb->size < ea->size) return -1;
    return 0;
}

static int entry_cmp_time(const void *a, const void *b) {
    const FileEntry *ea = (const FileEntry *)a;
    const FileEntry *eb = (const FileEntry *)b;
    if (eb->mtime > ea->mtime) return 1;
    if (eb->mtime < ea->mtime) return -1;
    return 0;
}

static void sort_file_list(FileList *list, const Config *cfg) {
    if (list->count == 0) return;

    int (*cmp)(const void *, const void *) = NULL;

    switch (cfg->sort_by) {
        case SORT_NAME: cmp = entry_cmp_name; break;
        case SORT_SIZE: cmp = entry_cmp_size; break;
        case SORT_TIME: cmp = entry_cmp_time; break;
        default: return;
    }

    qsort(list->entries, list->count, sizeof(FileEntry), cmp);

    if (cfg->sort_reverse) {
        /* Reverse in place */
        for (size_t i = 0; i < list->count / 2; i++) {
            FileEntry tmp = list->entries[i];
            list->entries[i] = list->entries[list->count - 1 - i];
            list->entries[list->count - 1 - i] = tmp;
        }
    }
}

static int read_directory(const char *dir_path, FileList *list, const Config *cfg) {
    DIR *dir = opendir(dir_path);
    if (!dir) return -1;

    /* Phase 1: Collect all entries (fast, sequential) */
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        /* Skip . and .. */
        if (ds_is_dot_or_dotdot(entry->d_name))
            continue;

        /* Skip hidden files unless -a flag */
        if (!cfg->show_hidden && entry->d_name[0] == '.')
            continue;

        /* Build full path (avoid double slash when dir_path is "/") */
        char full_path[PATH_MAX];
        size_t dir_len = strlen(dir_path);
        if (dir_len > 0 && dir_path[dir_len - 1] == '/') {
            snprintf(full_path, sizeof(full_path), "%s%s", dir_path, entry->d_name);
        } else {
            snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);
        }

        FileEntry fe;
        memset(&fe, 0, sizeof(fe));
        fe.path = xstrdup(full_path);
        fe.name = strrchr(fe.path, '/');
        fe.name = fe.name ? fe.name + 1 : fe.path;
        fe.line_count = -1;
        fe.file_count = -1;
        fe.git_status[0] = '\0';

        struct stat st;
        fe.type = detect_file_type(full_path, &st, &fe.symlink_target);
        fe.mode = st.st_mode;
        fe.mtime = GET_MTIME(st);
        fe.size = st.st_size;  /* Default to stat size, may be updated below */

        file_list_add(list, &fe);
    }

    closedir(dir);

    /* Phase 2: Compute expensive data in parallel (sizes/counts, line counts) */
    if (cfg->long_format && list->count > 0) {
        #pragma omp parallel for schedule(dynamic)
        for (size_t i = 0; i < list->count; i++) {
            FileEntry *fe = &list->entries[i];
            if (fe->type == FTYPE_DIR || fe->type == FTYPE_SYMLINK_DIR) {
                /* Get size and count in single pass (stat follows symlinks) */
                DirStats stats = get_dir_stats(fe->path);
                fe->size = stats.size;
                fe->file_count = stats.file_count;
            } else if (fe->type == FTYPE_FILE || fe->type == FTYPE_EXEC ||
                       fe->type == FTYPE_SYMLINK || fe->type == FTYPE_SYMLINK_EXEC) {
                /* Count lines in file (stat follows symlinks for size) */
                fe->line_count = count_file_lines(fe->path);
            }
        }
    }

    /* Always sort alphabetically by default, then apply user sort */
    qsort(list->entries, list->count, sizeof(FileEntry), entry_cmp_name);

    /* Apply user-requested sort on top */
    if (cfg->sort_by != SORT_NONE && cfg->sort_by != SORT_NAME) {
        sort_file_list(list, cfg);
    } else if (cfg->sort_reverse) {
        /* Reverse the default alphabetical sort */
        for (size_t i = 0; i < list->count / 2; i++) {
            FileEntry tmp = list->entries[i];
            list->entries[i] = list->entries[list->count - 1 - i];
            list->entries[list->count - 1 - i] = tmp;
        }
    }

    return 0;
}

/* ============================================================================
 * Tree Printing
 * ============================================================================ */

static void print_prefix(int depth, int *continuation, const Config *cfg) {
    if (cfg->list_mode) {
        /* No prefix in list mode */
        return;
    }

    if (!cfg->is_tty) {
        /* Simple indentation for non-TTY */
        for (int i = 0; i < depth; i++) {
            printf("  ");
        }
        return;
    }

    printf("%s", COLOR_GREY);
    for (int i = 0; i < depth - 1; i++) {
        printf("%s", continuation[i] ? TREE_VERT : TREE_SPACE);
    }
    if (depth > 0) {
        printf("%s", continuation[depth - 1] ? TREE_BRANCH : TREE_LAST);
    }
    printf("%s", COLOR_RESET);
}

static void print_entry(const FileEntry *fe, int depth, const PrintContext *ctx) {
    char abs_path[PATH_MAX];
    get_realpath(fe->path, abs_path, ctx->cfg);

    int is_cwd = (strcmp(abs_path, ctx->cfg->cwd) == 0);
    int is_hidden = (fe->name[0] == '.');

    /* Long format: print all columns before tree prefix */
    if (ctx->cfg->long_format && ctx->columns) {
        char buf[32];
        for (int i = 0; i < NUM_COLUMNS; i++) {
            ctx->columns[i].format(fe, buf, sizeof(buf));
            printf("%s%*s%s  ", ctx->cfg->is_tty ? COLOR_GREY : "",
                   ctx->columns[i].width, buf,
                   ctx->cfg->is_tty ? COLOR_RESET : "");
        }
    }

    /* Print tree prefix */
    print_prefix(depth, ctx->continuation, ctx->cfg);

    /* Readonly indicator */
    if (!ctx->cfg->no_icons && access(fe->path, W_OK) != 0 && access(fe->path, R_OK) == 0) {
        printf("%s%s%s ", ctx->cfg->is_tty ? COLOR_YELLOW : "",
               ctx->icons->readonly, ctx->cfg->is_tty ? COLOR_RESET : "");
    }

    /* Git indicator */
    const char *git_ind = get_git_indicator(ctx->git, abs_path, ctx->icons, ctx->cfg);
    printf("%s", git_ind);

    /* Color and style */
    int is_unreadable = (fe->type == FTYPE_DIR && fe->size < 0);
    const char *color = is_unreadable && ctx->cfg->is_tty ? COLOR_RED :
                        get_file_color(fe->type, is_cwd, fe->is_ignored, ctx->cfg);
    const char *style = (is_hidden && ctx->cfg->is_tty) ? STYLE_ITALIC : "";

    /* Icon */
    if (!ctx->cfg->no_icons) {
        printf("%s%s%s ", color, get_icon(ctx->icons, fe->type, is_cwd, is_unreadable, fe->name), COLOR_RESET);
    }

    /* Filename (full path in list mode) */
    if (ctx->cfg->list_mode) {
        char abbrev[PATH_MAX];
        abbreviate_home(abs_path, abbrev, sizeof(abbrev), ctx->cfg);
        printf("%s%s%s%s", color, style, abbrev, ctx->cfg->is_tty ? COLOR_RESET : "");
    } else {
        printf("%s%s%s%s", color, style, fe->name, ctx->cfg->is_tty ? COLOR_RESET : "");
    }

    /* Symlink target */
    if (fe->symlink_target) {
        char abbrev[PATH_MAX];
        abbreviate_home(fe->symlink_target, abbrev, sizeof(abbrev), ctx->cfg);
        printf(" %s %s%s%s",
               ctx->icons->symlink, color, abbrev, ctx->cfg->is_tty ? COLOR_RESET : "");
    }

    printf("\n");
}

static int should_skip_dir(const char *name, int is_ignored, const Config *cfg) {
    if (cfg->expand_all) return 0;
    if (is_ignored) return 1;
    if (strcmp(name, ".git") == 0) return 1;
    return 0;
}

/* ============================================================================
 * Tree Building (single pass: read + compute widths)
 * ============================================================================ */

static void tree_node_free(TreeNode *node) {
    if (!node) return;
    for (size_t i = 0; i < node->child_count; i++) {
        tree_node_free(&node->children[i]);
    }
    free(node->children);
    file_entry_free(&node->entry);
}

/*
 * Build child nodes for a parent TreeNode.
 *
 * Ownership: FileEntry ownership transfers from FileList to TreeNode.
 * The FileList's entries array is freed, but individual entries are NOT freed
 * (they are now owned by the tree nodes).
 */
static void build_tree_children(TreeNode *parent, int depth, Column *cols,
                                 GitCache *git, const Config *cfg) {
    if (depth >= cfg->max_depth) return;
    if (access(parent->entry.path, R_OK) != 0) return;

    FileList list;
    file_list_init(&list);

    if (read_directory(parent->entry.path, &list, cfg) != 0) {
        file_list_free(&list);
        return;
    }

    if (list.count == 0) {
        file_list_free(&list);
        return;
    }

    parent->children = xmalloc(list.count * sizeof(TreeNode));
    parent->child_count = list.count;

    for (size_t i = 0; i < list.count; i++) {
        TreeNode *child = &parent->children[i];
        memset(child, 0, sizeof(TreeNode));

        /* Move entry from list to tree node (transfer ownership) */
        child->entry = list.entries[i];

        /* Set is_ignored */
        const char *git_status = git_cache_get(git, child->entry.path);
        child->entry.is_ignored = (git_status && strcmp(git_status, "!!") == 0) ||
                                   strcmp(child->entry.name, ".git") == 0;

        /* Update column widths */
        if (cfg->long_format && cols) {
            columns_update_widths(cols, &child->entry);
        }

        /* Recurse into directories (including symlinks to directories) */
        if ((child->entry.type == FTYPE_DIR || child->entry.type == FTYPE_SYMLINK_DIR) &&
            !should_skip_dir(child->entry.name, child->entry.is_ignored, cfg)) {
            build_tree_children(child, depth + 1, cols, git, cfg);

            /* Sum children's sizes instead of using get_dir_size result (only if showing all) */
            if (cfg->long_format && cfg->show_hidden && child->child_count > 0) {
                off_t total_size = 0;
                long total_count = 0;
                for (size_t j = 0; j < child->child_count; j++) {
                    total_size += child->children[j].entry.size;
                    FileType t = child->children[j].entry.type;
                    if (t == FTYPE_FILE || t == FTYPE_EXEC ||
                        t == FTYPE_SYMLINK || t == FTYPE_SYMLINK_EXEC) {
                        total_count++;
                    } else if (child->children[j].entry.file_count >= 0) {
                        total_count += child->children[j].entry.file_count;
                    }
                }
                child->entry.size = total_size;
                child->entry.file_count = total_count;
            }
        }
    }

    /* Don't call file_list_free - we transferred ownership of entries */
    free(list.entries);
}

static TreeNode *build_tree(const char *path, Column *cols,
                            GitCache *git, const Config *cfg) {
    char abs_path[PATH_MAX];
    get_abspath(path, abs_path, cfg);  /* Don't resolve symlinks for root */

    struct stat st;
    char *symlink_target = NULL;
    FileType type = detect_file_type(abs_path, &st, &symlink_target);

    TreeNode *root = xmalloc(sizeof(TreeNode));
    memset(root, 0, sizeof(TreeNode));

    /* Build root entry */
    root->entry.path = xstrdup(abs_path);
    root->entry.name = strrchr(root->entry.path, '/');
    root->entry.name = root->entry.name ? root->entry.name + 1 : root->entry.path;
    root->entry.type = type;
    root->entry.symlink_target = symlink_target;
    root->entry.mode = st.st_mode;
    root->entry.mtime = GET_MTIME(st);
    root->entry.line_count = -1;
    root->entry.file_count = -1;

    if (cfg->long_format && (type == FTYPE_FILE || type == FTYPE_EXEC ||
                             type == FTYPE_SYMLINK || type == FTYPE_SYMLINK_EXEC)) {
        root->entry.line_count = count_file_lines(abs_path);
    }

    root->entry.size = st.st_size;
    /* Size/count computed after children if show_hidden, else compute now */
    if (cfg->long_format && (type == FTYPE_DIR || type == FTYPE_SYMLINK_DIR) && !cfg->show_hidden) {
        DirStats stats = get_dir_stats(abs_path);
        root->entry.size = stats.size;
        root->entry.file_count = stats.file_count;
    }

    /* Set is_ignored for root */
    const char *git_status = git_cache_get(git, abs_path);
    root->entry.is_ignored = (git_status && strcmp(git_status, "!!") == 0) ||
                              strcmp(root->entry.name, ".git") == 0;

    /* Update column widths from root */
    if (cfg->long_format && cols) {
        columns_update_widths(cols, &root->entry);
    }

    /* Build children if directory (including symlinks to directories) */
    if (type == FTYPE_DIR || type == FTYPE_SYMLINK_DIR) {
        build_tree_children(root, 0, cols, git, cfg);

        /* Sum children's sizes/counts to get root's total (only if showing all) */
        if (cfg->long_format && cfg->show_hidden) {
            off_t total_size = 0;
            long total_count = 0;
            for (size_t i = 0; i < root->child_count; i++) {
                total_size += root->children[i].entry.size;
                FileType t = root->children[i].entry.type;
                if (t == FTYPE_FILE || t == FTYPE_EXEC ||
                    t == FTYPE_SYMLINK || t == FTYPE_SYMLINK_EXEC) {
                    /* Count files directly */
                    total_count++;
                } else if (root->children[i].entry.file_count >= 0) {
                    /* Add subdirectory file counts */
                    total_count += root->children[i].entry.file_count;
                }
            }
            root->entry.size = total_size;
            root->entry.file_count = total_count;
            /* Update column widths now that we have the real value */
            if (cols) {
                columns_update_widths(cols, &root->entry);
            }
        }
    }

    return root;
}

/* ============================================================================
 * Tree Printing (no I/O, just walks the built tree)
 * ============================================================================ */

static void print_tree_node(const TreeNode *node, int depth, PrintContext *ctx);

static void print_tree_children(const TreeNode *parent, int depth, PrintContext *ctx) {
    for (size_t i = 0; i < parent->child_count; i++) {
        const TreeNode *child = &parent->children[i];
        int is_last = (i == parent->child_count - 1);

        ctx->continuation[depth] = !is_last;

        print_entry(&child->entry, depth + 1, ctx);

        /* Recurse into directories that have children */
        if (child->child_count > 0) {
            print_tree_children(child, depth + 1, ctx);
        }
    }
}

static void print_tree_node(const TreeNode *node, int depth, PrintContext *ctx) {
    /* In list mode, skip printing root directory - just show contents */
    if (ctx->cfg->list_mode && node->entry.type == FTYPE_DIR) {
        /* Adjust depth for list mode */
        for (size_t i = 0; i < node->child_count; i++) {
            const TreeNode *child = &node->children[i];
            int is_last = (i == node->child_count - 1);
            ctx->continuation[depth - 1] = !is_last;

            print_entry(&child->entry, depth, ctx);

            if (child->child_count > 0) {
                print_tree_children(child, depth, ctx);
            }
        }
        return;
    }

    /* Print root entry */
    print_entry(&node->entry, depth, ctx);

    /* Print children */
    if (node->child_count > 0) {
        print_tree_children(node, depth, ctx);
    }
}

/* ============================================================================
 * Argument Parsing
 * ============================================================================ */

static void print_usage(void) {
    printf("Usage: l [OPTIONS] [FILE ...]\n");
    printf("\n");
    printf("Options:\n");
    printf("  -a              Show hidden files\n");
    printf("  -s, --short     Short format (no size, lines, time)\n");
    printf("  -t, --tree      Show full tree (depth %d)\n", MAX_DEPTH);
    printf("  -d, --depth INT Limit tree depth\n");
    printf("  --expand-all    Expand all directories (ignore skip list)\n");
    printf("  --list          Flat list output (no tree structure)\n");
    printf("  --no-icons      Hide file/folder/git icons\n");
    printf("\n");
    printf("Sorting:\n");
    printf("  -S              Sort by size (largest first)\n");
    printf("  -T              Sort by modification time (newest first)\n");
    printf("  -N              Sort by name (alphabetical)\n");
    printf("  -r              Reverse sort order\n");
    printf("\n");
    printf("  -h, --help      Show this help message\n");
}

static void parse_args(int argc, char **argv, Config *cfg,
                       char ***dirs, int *dir_count) {
    static char *default_dirs[] = {"."};

    *dirs = NULL;
    *dir_count = 0;

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];

        if (arg[0] == '-' && arg[1] != '\0') {
            if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
                print_usage();
                exit(0);
            } else if (strcmp(arg, "-a") == 0) {
                cfg->show_hidden = 1;
            } else if (strcmp(arg, "-s") == 0 || strcmp(arg, "--short") == 0) {
                cfg->long_format = 0;
            } else if (strcmp(arg, "-t") == 0 || strcmp(arg, "--tree") == 0) {
                cfg->max_depth = MAX_DEPTH;
            } else if (strcmp(arg, "-d") == 0 || strcmp(arg, "--depth") == 0) {
                if (i + 1 >= argc) die("-d/--depth requires an argument");
                cfg->max_depth = atoi(argv[++i]);
                if (cfg->max_depth <= 0) die("-d/--depth requires a positive integer");
            } else if (strncmp(arg, "-d", 2) == 0 && arg[2] >= '0' && arg[2] <= '9') {
                cfg->max_depth = atoi(arg + 2);
                if (cfg->max_depth <= 0) die("-d requires a positive integer");
            } else if (strncmp(arg, "--depth=", 8) == 0) {
                cfg->max_depth = atoi(arg + 8);
                if (cfg->max_depth <= 0) die("--depth requires a positive integer");
            } else if (strcmp(arg, "--expand-all") == 0) {
                cfg->expand_all = 1;
            } else if (strcmp(arg, "--list") == 0) {
                cfg->list_mode = 1;
            } else if (strcmp(arg, "--no-icons") == 0) {
                cfg->no_icons = 1;
            } else if (strcmp(arg, "-S") == 0) {
                cfg->sort_by = SORT_SIZE;
            } else if (strcmp(arg, "-T") == 0) {
                cfg->sort_by = SORT_TIME;
            } else if (strcmp(arg, "-N") == 0) {
                cfg->sort_by = SORT_NAME;
            } else if (strcmp(arg, "-r") == 0) {
                cfg->sort_reverse = 1;
            } else if (arg[1] != '-') {
                /* Handle combined short flags like -as */
                for (int j = 1; arg[j]; j++) {
                    switch (arg[j]) {
                        case 'a': cfg->show_hidden = 1; break;
                        case 's': cfg->long_format = 0; break;
                        case 't': cfg->max_depth = MAX_DEPTH; break;
                        case 'S': cfg->sort_by = SORT_SIZE; break;
                        case 'T': cfg->sort_by = SORT_TIME; break;
                        case 'N': cfg->sort_by = SORT_NAME; break;
                        case 'r': cfg->sort_reverse = 1; break;
                        case 'h': print_usage(); exit(0);
                        default:
                            fprintf(stderr, "%sError:%s Unknown option: -%c\n",
                                    cfg->is_tty ? COLOR_RED : "", cfg->is_tty ? COLOR_RESET : "", arg[j]);
                            exit(1);
                    }
                }
            } else {
                fprintf(stderr, "%sError:%s Unknown option: %s\n",
                        cfg->is_tty ? COLOR_RED : "", cfg->is_tty ? COLOR_RESET : "", arg);
                exit(1);
            }
        } else {
            /* Directory argument */
            (*dir_count)++;
            *dirs = xrealloc(*dirs, *dir_count * sizeof(char *));
            (*dirs)[*dir_count - 1] = (char *)arg;
        }
    }

    /* Default to current directory */
    if (*dir_count == 0) {
        *dirs = default_dirs;
        *dir_count = 1;
    }
}

/* ============================================================================
 * Main
 * ============================================================================ */

int main(int argc, char **argv) {
    /* Initialize config with defaults */
    Config cfg = {
        .max_depth = 1,
        .show_hidden = 0,
        .long_format = 1,
        .expand_all = 0,
        .list_mode = 0,
        .no_icons = 0,
        .sort_reverse = 0,
        .is_tty = isatty(STDOUT_FILENO),
        .sort_by = SORT_NONE,
        .cwd = "",
        .home = "",
        .script_dir = ""
    };

    /* Initialize environment paths */
    if (!getcwd(cfg.cwd, sizeof(cfg.cwd))) {
        die("Cannot determine current directory");
    }

    const char *home = getenv("HOME");
    if (home) {
        strncpy(cfg.home, home, sizeof(cfg.home) - 1);
        cfg.home[sizeof(cfg.home) - 1] = '\0';
    }

    /* Get source directory (for icons.toml) */
    resolve_source_dir(argv[0], cfg.script_dir, sizeof(cfg.script_dir));

    /* Check if current directory exists */
    char *cwd_check = getcwd(NULL, 0);
    if (cwd_check == NULL) {
        fprintf(stderr, "%sError:%s Current directory no longer exists\n",
                cfg.is_tty ? COLOR_RED : "", cfg.is_tty ? COLOR_RESET : "");
        return 1;
    }
    free(cwd_check);

    /* Parse arguments */
    char **dirs;
    int dir_count;
    parse_args(argc, argv, &cfg, &dirs, &dir_count);

    /* Load icons */
    Icons icons;
    icons_init_defaults(&icons);
    icons_load(&icons, cfg.script_dir);

    /* Load size cache */
    cache_load();

    /* Validate all inputs first */
    for (int i = 0; i < dir_count; i++) {
        struct stat st;
        if (stat(dirs[i], &st) != 0) {
            fprintf(stderr, "%sError:%s '%s' does not exist\n",
                    cfg.is_tty ? COLOR_RED : "", cfg.is_tty ? COLOR_RESET : "",
                    dirs[i]);
            return 1;
        }
    }

    /* Process each directory */
    int continuation[MAX_DEPTH] = {0};

    for (int i = 0; i < dir_count; i++) {
        const char *dir = dirs[i];

        /* Initialize git cache (skip if --no-icons since we won't show indicators) */
        GitCache git;
        git_cache_init(&git);

        if (!cfg.no_icons) {
            char abs_dir[PATH_MAX];
            get_realpath(dir, abs_dir, &cfg);
            git_detect_repo(&git, abs_dir);
            git_populate_status(&git);
        }

        /* Initialize columns for long format */
        Column cols[NUM_COLUMNS];
        columns_init(cols);

        /* Build tree and compute column widths in single pass */
        TreeNode *tree = build_tree(dir, cfg.long_format ? cols : NULL, &git, &cfg);

        /* Print tree */
        PrintContext ctx = {
            .git = &git,
            .icons = &icons,
            .cfg = &cfg,
            .columns = cfg.long_format ? cols : NULL,
            .continuation = continuation
        };
        print_tree_node(tree, 0, &ctx);

        /* Cleanup */
        tree_node_free(tree);
        free(tree);
        git_cache_free(&git);
    }

    cache_unload();
    return 0;
}
