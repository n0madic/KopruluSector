/*
 * kp_convert's files (ADR 0019): folders made, whole files read and written, the files of a folder by their name's
 * prefix and suffix; on POSIX systems and on Windows alike.
 */
#ifndef KP_FS_H
#define KP_FS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The folder and its parents: 0, or -1 with the reason. */
int kp_fs_mkdirs(const char *path, char *err, size_t err_size);
/* A file's bytes, malloc'd (NUL terminated past *size), or NULL: err empty when there is no such file. */
uint8_t *kp_fs_read(const char *path, size_t *size, char *err, size_t err_size);
/* The bytes written to path through a file beside it renamed over it (a reader never sees half a file): 0, or -1. */
int kp_fs_write(const char *path, const void *data, size_t size, char *err, size_t err_size);
/* kp_fs_write unless path holds those bytes already: 1 written, 0 the same, -1 with the reason. */
int kp_fs_write_if_changed(const char *path, const void *data, size_t size, char *err, size_t err_size);
/* The names of dir's files (not its folders, nor a name starting with '.') that end with suffix, sorted (strcmp): a
 * malloc'd array of *count malloc'd names (free each and the array), or NULL with the reason (an empty folder gives an
 * array and 0). */
char **kp_fs_list(const char *dir, const char *suffix, int *count, char *err, size_t err_size);
/* Each file of dir whose name starts with prefix and ends with suffix removed: the count, or -1 with the reason. */
int kp_fs_remove_matching(const char *dir, const char *prefix, const char *suffix, char *err, size_t err_size);

#ifdef __cplusplus
}
#endif

#endif
