/*
 * dir_stats.h - Shared directory statistics computation
 *
 * Uses OpenMP for parallel directory traversal.
 * With OMP_NUM_THREADS=1 or omp_set_num_threads(1), runs single-threaded.
 */

#ifndef DIR_STATS_H
#define DIR_STATS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <limits.h>

#ifdef _OPENMP
#include <omp.h>
#endif

/* Result of directory statistics computation */
typedef struct {
    off_t size;        /* Total size in bytes (-1 if error) */
    long file_count;   /* Total file count (-1 if error) */
} DirStats;

/*
 * Cache lookup function type.
 * Returns 1 if found (and fills size/count), 0 if not found.
 * Pass NULL to disable cache lookups during traversal.
 */
typedef int (*dir_stats_cache_fn)(const char *path, off_t *size, long *count);

/* Check if entry is . or .. (efficient bitwise check) */
static inline int ds_is_dot_or_dotdot(const char *name) {
    return name[0] == '.' &&
           (name[1] == '\0' || (name[1] == '.' && name[2] == '\0'));
}

/* Internal: recursive task function */
static DirStats ds_get_stats_task(const char *path, dir_stats_cache_fn cache_fn) {
    DirStats result = {-1, -1};
    DIR *dir = opendir(path);
    if (!dir) return result;

    /* Collect entries first (sequential phase) */
    char **subdirs = NULL;
    size_t subdir_count = 0;
    size_t subdir_cap = 0;
    off_t file_size_total = 0;
    long file_count_total = 0;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (ds_is_dot_or_dotdot(entry->d_name))
            continue;

        char full_path[PATH_MAX];
        snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);

        struct stat st;
        if (lstat(full_path, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                if (subdir_count >= subdir_cap) {
                    subdir_cap = subdir_cap ? subdir_cap * 2 : 16;
                    char **new_subdirs = realloc(subdirs, subdir_cap * sizeof(char *));
                    if (!new_subdirs) {
                        /* Out of memory - clean up and return error */
                        for (size_t i = 0; i < subdir_count; i++) free(subdirs[i]);
                        free(subdirs);
                        closedir(dir);
                        return result;
                    }
                    subdirs = new_subdirs;
                }
                subdirs[subdir_count] = strdup(full_path);
                if (!subdirs[subdir_count]) {
                    for (size_t i = 0; i < subdir_count; i++) free(subdirs[i]);
                    free(subdirs);
                    closedir(dir);
                    return result;
                }
                subdir_count++;
            } else {
                file_size_total += st.st_size;
                file_count_total++;
            }
        }
    }
    closedir(dir);

    /* Process subdirectories (parallel phase with OMP tasks) */
    DirStats *sub_stats = NULL;
    if (subdir_count > 0) {
        sub_stats = malloc(subdir_count * sizeof(DirStats));
        if (!sub_stats) {
            for (size_t i = 0; i < subdir_count; i++) free(subdirs[i]);
            free(subdirs);
            return result;
        }

        for (size_t i = 0; i < subdir_count; i++) {
            /* Check cache if available */
            off_t cached_size;
            long cached_count;
            if (cache_fn && cache_fn(subdirs[i], &cached_size, &cached_count)) {
                sub_stats[i].size = cached_size;
                sub_stats[i].file_count = cached_count;
            } else {
                #pragma omp task shared(sub_stats) firstprivate(i)
                sub_stats[i] = ds_get_stats_task(subdirs[i], cache_fn);
            }
        }
        #pragma omp taskwait
    }

    /* Sum results */
    off_t dir_size_total = 0;
    long dir_count_total = 0;
    for (size_t i = 0; i < subdir_count; i++) {
        if (sub_stats[i].size >= 0) dir_size_total += sub_stats[i].size;
        if (sub_stats[i].file_count >= 0) dir_count_total += sub_stats[i].file_count;
        free(subdirs[i]);
    }
    free(subdirs);
    free(sub_stats);

    result.size = file_size_total + dir_size_total;
    result.file_count = file_count_total + dir_count_total;
    return result;
}

/*
 * Compute directory statistics (size and file count) recursively.
 *
 * Parameters:
 *   path     - Directory path to scan
 *   cache_fn - Optional cache lookup function (NULL to disable)
 *
 * Returns:
 *   DirStats with size and file_count (-1 for each on error)
 *
 * Note: Uses OpenMP for parallel subdirectory processing.
 *       Set omp_set_num_threads(1) for single-threaded operation.
 */
static inline DirStats dir_stats_get(const char *path, dir_stats_cache_fn cache_fn) {
    DirStats result;
    #pragma omp parallel
    #pragma omp single
    {
        result = ds_get_stats_task(path, cache_fn);
    }
    return result;
}

#endif /* DIR_STATS_H */
