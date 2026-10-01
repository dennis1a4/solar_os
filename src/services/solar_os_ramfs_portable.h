#pragma once
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "solar_os_ramfs.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Direct backend for non-ESP file routers. The caller serializes ALL mount,
 * lookup and I/O operations with its storage lock, including handle lifetime.
 * Paths passed to operations are relative to the resolved mount, starting '/'. */
typedef struct {
    ssize_t (*write_p)(void *, int, const void *, size_t);
    off_t (*lseek_p)(void *, int, off_t, int);
    ssize_t (*read_p)(void *, int, void *, size_t);
    int (*open_p)(void *, const char *, int, int);
    int (*close_p)(void *, int);
    int (*fstat_p)(void *, int, struct stat *);
    int (*fsync_p)(void *, int);
    int (*stat_p)(void *, const char *, struct stat *);
    int (*unlink_p)(void *, const char *);
    int (*rename_p)(void *, const char *, const char *);
    DIR *(*opendir_p)(void *, const char *);
    struct dirent *(*readdir_p)(void *, DIR *);
    int (*readdir_r_p)(void *, DIR *, struct dirent *, struct dirent **);
    long (*telldir_p)(void *, DIR *);
    void (*seekdir_p)(void *, DIR *, long);
    int (*closedir_p)(void *, DIR *);
    int (*mkdir_p)(void *, const char *, mode_t);
    int (*rmdir_p)(void *, const char *);
    int (*access_p)(void *, const char *, int);
    int (*truncate_p)(void *, const char *, off_t);
    int (*ftruncate_p)(void *, int, off_t);
} solar_os_ramfs_ops_t;
const solar_os_ramfs_ops_t *solar_os_ramfs_ops(void);
void *solar_os_ramfs_context(const char *path);
#ifdef __cplusplus
}
#endif
