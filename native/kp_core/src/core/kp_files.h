/*
 * The original files a data set is built from (ADR 0018, stage D3): the reader the host gives the core, by MPQ path
 * ("arr\\units.dat", any case, '\\' or '/'), and the profile the files are of. The app reads them through kp_res
 * (native/kp_app/kp_res.h: an install or a repack, each file checked against the profile's input manifest); the core
 * never sees an archive. Each structure's *_from_files builds what its *_load reads from the converter's container.
 */
#ifndef KP_FILES_H
#define KP_FILES_H

#include <stddef.h>
#include <stdint.h>

#include "kp_defs.h"
#include "kp_kpf.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	/* a malloc'd copy of the file (NUL terminated past *size), or NULL: err empty when no source has it, the reason
	 * otherwise */
	uint8_t *(*read)(void *ctx, const char *name, size_t *size, char *err, size_t err_size);
	/* the first `want` bytes of a file (fewer when it is shorter) and its size, without reading the rest (the sounds'
	 * and the music's headers; the size checked against the manifest, not the digest), NULL as read; NULL: read whole */
	uint8_t *(*head)(void *ctx, const char *name, size_t want, size_t *got, uint64_t *total, char *err, size_t err_size);
	void *ctx;
	const KpDefsProfile *profile;
} KpFiles;

/* A file the data needs: its bytes, or NULL with "<name>: not in the data" or the reader's reason in err. */
uint8_t *kp_files_need(const KpFiles *f, const char *name, size_t *size, KpError *err);
/* A file's first bytes and size (KpFiles.head, else the whole file): NULL with err empty when no source has it. */
uint8_t *kp_files_head(const KpFiles *f, const char *name, size_t want, size_t *got, uint64_t *total, KpError *err);
/* A PCX the data needs, its pixels decoded (kp_ui_pcx), malloc'd, *w by *h: NULL with the reason. */
uint8_t *kp_files_pcx(const KpFiles *f, const char *name, int32_t *w, int32_t *h, KpError *err);
/* SBmpLoadImage into a `want`-byte buffer (kpd._pcx_prefix): a PCX's first `want` pixel bytes, row-major, into out; 0,
 * or -1 with the reason (a PCX of fewer pixels too). */
int kp_files_pcx_prefix(const KpFiles *f, const char *name, uint8_t *out, size_t want, KpError *err);

#ifdef __cplusplus
}
#endif

#endif
