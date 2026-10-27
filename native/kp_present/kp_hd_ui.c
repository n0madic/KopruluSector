/*
 * The interface's HD art (ADR 0013, stage R4d; converter/emit/hd_ui.py): ui.kpd (HDUI) opened from its first bytes,
 * its assets found by the SD file's path, and the atlas where the frames the canvas shows are placed as they are first
 * needed (kp_app reads and decodes them, kp_gfx holds the texels).
 */
#include "kp_present.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ASSET_RECORD (KP_HD_UI_PATH + 8)
#define FRAME_RECORD 18
#define ATLAS_GAP 2 /* texels between images: the linear filter does not reach a neighbour */

static int fail(KpError *err, const char *name, const char *msg)
{
	snprintf(err->msg, sizeof err->msg, "%s: %s", name, msg);
	return -1;
}

size_t kp_hd_ui_head_size(const uint8_t *first, size_t n, size_t file_size)
{
	KpKpf c;
	KpError e;
	const KpKpfSection *s[3];
	size_t end = 0;
	int k;
	if (kp_kpf_parse_prefix(&c, first, n, file_size, "", &e) != 0) return 0;
	s[0] = kp_kpf_find(&c, "HEAD");
	s[1] = kp_kpf_find(&c, "ASET");
	s[2] = kp_kpf_find(&c, "FRMS");
	for (k = 0; k < 3; k++) {
		if (!s[k]) {
			end = 0;
			break;
		}
		if ((size_t)s[k]->offset + s[k]->size > end) end = (size_t)s[k]->offset + s[k]->size;
	}
	kp_kpf_free(&c);
	return end;
}

int kp_hd_ui_open(KpHdUi *u, const uint8_t *head_bytes, size_t n, size_t file_size, const char *name, KpError *err)
{
	KpKpf c;
	KpKpfExpect expect;
	const KpKpfSection *head, *aset, *frms, *imgs;
	int rc = -1;
	int32_t k;
	memset(u, 0, sizeof *u);
	if (kp_kpf_parse_prefix(&c, head_bytes, n, file_size, name, err) != 0) return -1;
	memset(&expect, 0, sizeof expect);
	expect.kind = KP_HD_UI_KIND;
	expect.schema_version = KP_HD_SCHEMA;
	if (kp_kpf_check(&c, &expect, err) != 0) goto done;
	if (!(head = kp_kpf_require(&c, "HEAD", err)) || !(aset = kp_kpf_require(&c, "ASET", err))
		|| !(frms = kp_kpf_require(&c, "FRMS", err)) || !(imgs = kp_kpf_require(&c, "IMGS", err))) {
		goto done;
	}
	if (!head->data || !aset->data || !frms->data || head->size < 7) {
		fail(err, name, "HEAD, ASET or FRMS past the bytes read");
		goto done;
	}
	u->scale = head->data[0];
	u->asset_count = (int32_t)kp_le_u16(head->data + 1);
	u->frame_count = (int32_t)kp_le_u32(head->data + 3);
	if ((u->scale != 2 && u->scale != 4) || aset->size != (size_t)u->asset_count * ASSET_RECORD
		|| frms->size != (size_t)u->frame_count * FRAME_RECORD) {
		fail(err, name, "HEAD, ASET or FRMS size");
		goto done;
	}
	for (k = 0; k < u->asset_count; k++) {
		const uint8_t *r = aset->data + (size_t)k * ASSET_RECORD;
		if (r[KP_HD_UI_PATH - 1] != 0 || kp_le_u32(r + KP_HD_UI_PATH) + kp_le_u16(r + KP_HD_UI_PATH + 4)
			> (uint32_t)u->frame_count) {
			fail(err, name, "an asset past FRMS");
			goto done;
		}
	}
	for (k = 0; k < u->frame_count; k++) {
		const uint8_t *r = frms->data + (size_t)k * FRAME_RECORD;
		if ((uint64_t)kp_le_u32(r) + kp_le_u32(r + 4) > imgs->size || r[12] > KP_HD_UI_PNG) {
			fail(err, name, "a frame past IMGS");
			goto done;
		}
	}
	u->assets = aset->data;
	u->frames = frms->data;
	u->images = imgs->offset;
	rc = 0;
done:
	kp_kpf_free(&c);
	return rc;
}

static int path_char(char c) { return c == '/' ? '\\' : tolower((unsigned char)c); }

int32_t kp_hd_ui_asset(const KpHdUi *u, const char *path, size_t len, int32_t *first, int32_t *count, int *flags)
{
	int32_t k;
	if (len >= KP_HD_UI_PATH) return -1;
	for (k = 0; k < u->asset_count; k++) {
		const uint8_t *r = u->assets + (size_t)k * ASSET_RECORD;
		size_t i;
		for (i = 0; i < len && r[i] == (uint8_t)path_char(path[i]); i++) {}
		if (i < len || r[len] != 0) continue;
		if (first) *first = (int32_t)kp_le_u32(r + KP_HD_UI_PATH);
		if (count) *count = (int32_t)kp_le_u16(r + KP_HD_UI_PATH + 4);
		if (flags) *flags = (int)kp_le_u16(r + KP_HD_UI_PATH + 6);
		return k;
	}
	return -1;
}

void kp_hd_ui_frame(const KpHdUi *u, int32_t frame, KpHdUiFrame *out)
{
	const uint8_t *r = u->frames + (size_t)frame * FRAME_RECORD;
	out->offset = u->images + kp_le_u32(r);
	out->size = kp_le_u32(r + 4);
	out->w = (int32_t)kp_le_u16(r + 8);
	out->h = (int32_t)kp_le_u16(r + 10);
	out->format = r[12];
	out->x = (int16_t)kp_le_u16(r + 14);
	out->y = (int16_t)kp_le_u16(r + 16);
}

/* --- the atlas --- */

int kp_hd_ui_atlas_init(KpHdUiAtlas *a, int32_t size, int32_t layers, int32_t frame_count)
{
	memset(a, 0, sizeof *a);
	if (layers < 1 || layers > KP_HD_UI_ATLAS_LAYERS || size <= 0) return -1;
	a->size = size;
	a->layers = layers;
	a->frame_count = frame_count;
	a->places = (KpHdUiPlace *)calloc((size_t)(frame_count > 0 ? frame_count : 1), sizeof *a->places);
	if (!a->places) return -1;
	kp_hd_ui_atlas_reset(a);
	return 0;
}

void kp_hd_ui_atlas_free(KpHdUiAtlas *a)
{
	free(a->places);
	memset(a, 0, sizeof *a);
}

void kp_hd_ui_atlas_reset(KpHdUiAtlas *a)
{
	int32_t k;
	for (k = 0; k < KP_HD_UI_ATLAS_LAYERS; k++) a->shelf_x[k] = a->shelf_y[k] = a->shelf_h[k] = 0;
	if (a->places) memset(a->places, 0, sizeof *a->places * (size_t)(a->frame_count > 0 ? a->frame_count : 1));
	a->generation++;
}

KpHdUiPlace *kp_hd_ui_atlas_place(KpHdUiAtlas *a, int32_t frame, int32_t w, int32_t h, int *fresh)
{
	KpHdUiPlace *p;
	int32_t k, gw = w + ATLAS_GAP, gh = h + ATLAS_GAP;
	*fresh = 0;
	if (!a->places || frame < 0 || frame >= a->frame_count || w <= 0 || h <= 0) return NULL;
	p = &a->places[frame];
	if (p->w > 0) return p;
	if (gw > a->size || gh > a->size) return NULL;
	for (k = 0; k < a->layers; k++) { /* the open shelf of a layer, else a new shelf under it */
		if (a->shelf_x[k] + gw <= a->size && gh <= a->shelf_h[k]) break;
		if (a->shelf_y[k] + a->shelf_h[k] + gh <= a->size) {
			a->shelf_y[k] += a->shelf_h[k];
			a->shelf_x[k] = 0;
			a->shelf_h[k] = gh;
			break;
		}
	}
	if (k == a->layers) return NULL;
	if (a->shelf_h[k] == 0) a->shelf_h[k] = gh;
	p->layer = k;
	p->x = a->shelf_x[k];
	p->y = a->shelf_y[k];
	p->w = w;
	p->h = h;
	a->shelf_x[k] += gw;
	*fresh = 1;
	return p;
}
