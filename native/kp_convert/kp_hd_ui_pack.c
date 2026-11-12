/*
 * ui.kpd from a variant's layout (kp_hd_ui_pack.h): HEAD and ASET as the layout's, FRMS's offsets and sizes those of
 * each frame's image in IMGS (each image's start 4-aligned, as hd_ui.py pads them).
 */
#include "kp_hd_ui_pack.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kp_icon.h"
#include "kp_kpf.h"

#define FRAME_RECORD 18

uint8_t *kp_hd_ui_pack(const KpHdUiScr *s, KpResReader *h, const char *profile_id, size_t *size, KpError *err)
{
	size_t aset = s->frms_at - s->aset_at, frms = (size_t)s->index.frame_count * FRAME_RECORD, n = 0, cap = 0;
	uint8_t *records = (uint8_t *)malloc(frms + 1), *imgs = NULL, *out = NULL;
	int32_t k;
	if (!records) goto oom;
	memcpy(records, s->head + s->frms_at, frms);
	for (k = 0; k < s->index.frame_count; k++) {
		KpHdUiFrame f;
		size_t got, pad;
		uint8_t *image, *body, *grown;
		kp_hd_ui_frame(&s->index, k, &f);
		if (!(image = kp_hd_ui_scr_image(s, h, k, &got, err))) goto failed;
		body = image;
		if (f.format == KP_HD_UI_PNG) { /* RGBA rows as the PNG hd_ui.py's encode_png writes */
			body = kp_icon_png(image, f.w, f.h, 4, &got);
			free(image);
			if (!body) goto oom;
		}
		pad = (4 - (n + got) % 4) % 4;
		if (n + got + pad > cap) {
			size_t c = cap ? cap : 1u << 20;
			while (c < n + got + pad) c *= 2;
			if (!(grown = (uint8_t *)realloc(imgs, c))) {
				free(body);
				goto oom;
			}
			imgs = grown;
			cap = c;
		}
		if (n > UINT32_MAX || got > UINT32_MAX) {
			free(body);
			snprintf(err->msg, sizeof err->msg, "hd ui: the images exceed 4 GB");
			goto failed;
		}
		kp_le_put32(records + (size_t)k * FRAME_RECORD, (uint32_t)n);
		kp_le_put32(records + (size_t)k * FRAME_RECORD + 4, (uint32_t)got);
		memcpy(imgs + n, body, got);
		memset(imgs + n + got, 0, pad);
		n += got + pad;
		free(body);
	}
	{
		KpKpfOut sections[4] = { { "HEAD", NULL, 0, 0 }, { "ASET", NULL, 0, 0 }, { "FRMS", NULL, 0, 0 }, { "IMGS", NULL, 0, 0 } };
		sections[0].payload = s->head;
		sections[0].size = s->aset_at;
		sections[1].payload = s->head + s->aset_at;
		sections[1].size = aset;
		sections[2].payload = records;
		sections[2].size = frms;
		sections[3].payload = imgs;
		sections[3].size = n;
		if (kp_kpf_build(KP_HD_UI_KIND, KP_HD_UI_SCHEMA, 0, profile_id, NULL, sections, 4, &out, size) != 0) goto oom;
	}
	free(records);
	free(imgs);
	return out;
oom:
	snprintf(err->msg, sizeof err->msg, "hd ui: out of memory");
failed:
	free(records);
	free(imgs);
	return NULL;
}
