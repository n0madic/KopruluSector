/* The original files' reader (kp_files.h). */
#include "kp_files.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint8_t *kp_files_need(const KpFiles *f, const char *name, size_t *size, KpError *err)
{
	char why[KP_ERROR_SIZE];
	uint8_t *b;
	why[0] = 0;
	*size = 0;
	b = f->read(f->ctx, name, size, why, sizeof why);
	if (!b) snprintf(err->msg, sizeof err->msg, "%s: %s", name, why[0] ? why : "not in the data");
	return b;
}

uint8_t *kp_files_head(const KpFiles *f, const char *name, size_t want, size_t *got, uint64_t *total, KpError *err)
{
	char why[KP_ERROR_SIZE];
	uint8_t *b;
	why[0] = 0;
	err->msg[0] = 0;
	*got = 0;
	*total = 0;
	if (f->head) {
		b = f->head(f->ctx, name, want, got, total, why, sizeof why);
	} else if ((b = f->read(f->ctx, name, got, why, sizeof why)) != NULL) {
		*total = *got;
		if (*got > want) *got = want;
	}
	if (!b && why[0]) snprintf(err->msg, sizeof err->msg, "%s: %s", name, why);
	return b;
}

#define WAV_HEAD 4096 /* the header bytes read first; a WAV whose data chunk starts later is read whole */

int kp_files_wav(const KpFiles *f, const char *name, KpWavInfo *w, KpError *err)
{
	size_t got;
	uint64_t total;
	uint8_t *b = kp_files_head(f, name, WAV_HEAD, &got, &total, err);
	int rc;
	if (!b) return err->msg[0] ? -2 : -1;
	if ((rc = kp_wav_info(b, got, total, w)) == -2) {
		free(b);
		if (!(b = kp_files_need(f, name, &got, err))) return -2;
		rc = kp_wav_info(b, got, got, w);
	}
	free(b);
	return rc == 0 ? 1 : 0;
}

