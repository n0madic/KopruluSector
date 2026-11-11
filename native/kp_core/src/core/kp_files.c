/* The original files' reader (kp_files.h). */
#include "kp_files.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kp_ui.h"

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

uint8_t *kp_files_pcx(const KpFiles *f, const char *name, int32_t *w, int32_t *h, KpError *err)
{
	size_t size;
	uint8_t *data = kp_files_need(f, name, &size, err), *px = NULL;
	if (!data) return NULL;
	if (kp_ui_pcx(data, size, w, h, NULL, 0) != 0 || *w <= 0 || *h <= 0
		|| !(px = (uint8_t *)malloc((size_t)*w * (size_t)*h))
		|| kp_ui_pcx(data, size, w, h, px, (size_t)*w * (size_t)*h) != 0) {
		snprintf(err->msg, sizeof err->msg, "%s: not a PCX the game reads", name);
		free(px);
		px = NULL;
	}
	free(data);
	return px;
}

int kp_files_pcx_prefix(const KpFiles *f, const char *name, uint8_t *out, size_t want, KpError *err)
{
	int32_t w, h;
	uint8_t *px = kp_files_pcx(f, name, &w, &h, err);
	if (!px) return -1;
	if ((size_t)w * (size_t)h < want) {
		snprintf(err->msg, sizeof err->msg, "%s: %d pixels, the game's buffer takes %u", name, (int)(w * h), (unsigned)want);
		free(px);
		return -1;
	}
	memcpy(out, px, want);
	free(px);
	return 0;
}
