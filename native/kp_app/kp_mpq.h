/* MPQ archives through StormLib (ADR 0018): the 1.17 install's archives and the converter's repacks. StormLib.h is
 * included by kp_mpq.c alone: on Windows it includes windows.h, whose macros (near, far) would rename the project
 * headers' fields. An archive handle is not to be shared between threads: each thread opens its own. */
#ifndef KP_MPQ_H
#define KP_MPQ_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct KpMpq KpMpq;
typedef struct KpMpqFile KpMpqFile;

/* The compression of a file added to a new archive */
enum { KP_MPQ_STORE, KP_MPQ_ZLIB, KP_MPQ_BZIP2 };

/* An archive opened to read (check_crc: the files' sector CRCs checked as they are read), or NULL with the reason.
 * path is a file's, or a slice's name (kp_mpq_slice_name). */
KpMpq *kp_mpq_open(const char *path, int check_crc, char *err, size_t err_size);
/* The name kp_mpq_open opens an archive stored within another file by: length bytes at offset of the open file fd
 * (Android: an asset stored uncompressed in the APK, AAsset_openFileDescriptor64). POSIX only: StormLib's file streams
 * as tools/toolchain/patches/StormLib-9.40 patch them. An archive over it reads through a descriptor of its own; fd
 * stays the caller's, open while any archive over it is. 0, or -1 when out is too small. */
int kp_mpq_slice_name(int fd, uint64_t offset, uint64_t length, char *out, size_t size);
/* A new archive (replacing a file at path) for at most max_files files, with (listfile) and no (attributes). */
KpMpq *kp_mpq_create(const char *path, uint32_t max_files, char *err, size_t err_size);
/* A file added to a created archive (a name already there is replaced): 0, or -1 with the reason. */
int kp_mpq_add(KpMpq *m, const char *name, const void *data, size_t size, int compression, char *err, size_t err_size);
/* Closes the archive (a created one is written out first): 0, or -1 when it could not be written. NULL is fine. */
int kp_mpq_close(KpMpq *m);
int kp_mpq_has(KpMpq *m, const char *name);
/* A malloc'd copy of a file's bytes (NUL terminated past size), or NULL: err empty when the archive does not have it,
 * the reason otherwise. */
uint8_t *kp_mpq_read(KpMpq *m, const char *name, size_t *size, char *err, size_t err_size);
/* A file read in pieces (music, movies): its size, then reads at any offset; 0 or -1. */
KpMpqFile *kp_mpq_file_open(KpMpq *m, const char *name, uint64_t *size, char *err, size_t err_size);
int kp_mpq_file_read(KpMpqFile *f, uint64_t offset, void *out, size_t size);
void kp_mpq_file_close(KpMpqFile *f);

#ifdef __cplusplus
}
#endif

#endif
