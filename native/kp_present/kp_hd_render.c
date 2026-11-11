/*
 * What the renderer makes of the HD containers (kp_hd.c; ADR 0013): the pack format a context can take, a page decoded
 * where its blocks cannot be uploaded (kp_bcn.c, kp_etc.c), the HD frame table, whose global frames are the SD table's,
 * and the atlas of the megatiles a map uses. kp_gfx uploads a page's blocks as they are where the context takes them.
 */
#include "kp_present_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int kp_hd_format_decodable(int format) { return format == KP_HD_FORMAT_BC || format == KP_HD_FORMAT_ETC2; }

int kp_hd_format_usable(int format, int decode, int takes)
{
	if (format < 0 || format >= KP_HD_FORMATS) return 0;
	return kp_hd_format_decodable(format) || (!decode && takes);
}

int kp_hd_pack_format(const uint8_t *first, size_t n, size_t file_size)
{
	KpKpf c;
	KpError e;
	const KpKpfSection *head;
	int format = -1;
	if (kp_kpf_parse_prefix(&c, first, n, file_size, "HD pack", &e) != 0) return -1;
	if (strcmp(c.kind, KP_HD_PACK_KIND) == 0 && (head = kp_kpf_find(&c, "HEAD")) != NULL && head->data
		&& head->size >= KP_HD_PACK_HEAD_BYTES) {
		format = head->data[KP_HD_PACK_HEAD_FORMAT];
		if (format >= KP_HD_FORMATS) format = -1;
	}
	kp_kpf_free(&c);
	return format;
}

size_t kp_hd_pack_head_size(const uint8_t *first, size_t n, size_t file_size)
{
	static const char *const NAMES[2] = { "HEAD", "FRMS" };
	return kp_present_head_size(first, n, file_size, NAMES, 2);
}

int kp_hd_page_decode(int format, const uint8_t *diffuse, const uint8_t *team, int32_t size, uint8_t *rgba,
	uint8_t *mask)
{
	size_t pixels = (size_t)size * (size_t)size, i;
	uint8_t *team_rgba;
	if (!kp_hd_format_decodable(format)) return -1;
	if (format == KP_HD_FORMAT_ETC2) {
		kp_etc2_decode(diffuse, size, size, rgba);
		kp_eac_r11_decode(team, size, size, mask);
		return 0;
	}
	if (!(team_rgba = (uint8_t *)malloc(pixels * 4))) return -1;
	kp_bcn_decode(diffuse, size, size, 1, rgba);
	kp_bcn_decode(team, size, size, 0, team_rgba);
	for (i = 0; i < pixels; i++) mask[i] = team_rgba[4 * i];
	free(team_rgba);
	return 0;
}

int kp_hd_frame_table_build(KpFrameTable *hd, const KpFrameTable *sd, const KpGrpMeta *g, const KpPack *const *packs,
	const KpHdPack *const *hd_packs, const char *const *pack_names, int pack_count, KpError *err)
{
	int32_t pages = 0, *page_base;
	uint32_t a;
	int k;
	memset(hd, 0, sizeof *hd);
	if (sd->pack_count != pack_count) { /* atlas_in names packs of the SD table's list */
		snprintf(err->msg, sizeof err->msg, "HD frame table: %d packs, the SD table's %d", pack_count, sd->pack_count);
		return -1;
	}
	hd->height = sd->height;
	hd->empty = sd->empty;
	hd->texels = (float *)calloc((size_t)KP_FRAME_TABLE_WIDTH * (size_t)sd->height * 4, sizeof *hd->texels);
	page_base = (int32_t *)malloc(sizeof *page_base * (size_t)(pack_count ? pack_count : 1));
	if (!hd->texels || !page_base) {
		free(page_base);
		kp_frame_table_free(hd);
		snprintf(err->msg, sizeof err->msg, "HD frame table: out of memory");
		return -1;
	}
	for (k = 0; k < pack_count; k++) {
		page_base[k] = pages;
		pages += hd_packs[k] ? hd_packs[k]->page_count : 0;
		if (hd_packs[k] && hd_packs[k]->frame_count != packs[k]->frame_count) {
			free(page_base);
			kp_frame_table_free(hd);
			snprintf(err->msg, sizeof err->msg, "HD pack %s: %u frames, its SD pack %u", pack_names[k],
				hd_packs[k]->frame_count, packs[k]->frame_count);
			return -1;
		}
	}
	hd->page_count = pages;
	/* the SD table's global frames: atlas_base[a] + f of the pack frame atlas.first + f */
	for (a = 1; a <= g->atlas_count && (int32_t)a <= sd->atlas_count; a++) {
		const KpPackAtlas *pa;
		uint32_t f;
		if (sd->atlas_base[a] < 0 || !hd_packs[k = sd->atlas_in[2 * a]]) continue;
		pa = &packs[k]->atlases[sd->atlas_in[2 * a + 1]];
		for (f = 0; f < pa->count; f++) {
			const uint8_t *r = hd_packs[k]->frames + (size_t)(pa->first + f) * KP_HD_FRAME_RECORD;
			float *o = &hd->texels[(size_t)(sd->atlas_base[a] + (int32_t)f) * 8];
			o[0] = (float)kp_le_u16(r + 2);
			o[1] = (float)kp_le_u16(r + 4);
			o[2] = (float)kp_le_u16(r + 6);
			o[3] = (float)kp_le_u16(r + 8);
			o[4] = (float)(page_base[k] + (int32_t)kp_le_u16(r));
			o[5] = (float)(int16_t)kp_le_u16(r + 10);
			o[6] = (float)(int16_t)kp_le_u16(r + 12);
			o[7] = (float)hd_packs[k]->scale;
		}
	}
	free(page_base);
	return 0;
}

void kp_hd_terrain_free(KpHdTerrain *a)
{
	free(a->blocks);
	free(a->slots);
	free(a->water);
	free(a->slot_of);
	memset(a, 0, sizeof *a);
}

int kp_hd_terrain_relink(KpHdTerrain *a, const uint16_t *ids)
{
	int32_t cell, n = a->map_w * a->map_h;
	if (!a->slot_of) return 1;
	for (cell = 0; cell < n; cell++) {
		int32_t id = ids[cell] & 0x7fff;
		if (id >= a->megatiles) id = 0;
		if (a->slot_of[id] < 0) return 1;
	}
	for (cell = 0; cell < n; cell++) {
		int32_t id = ids[cell] & 0x7fff;
		a->slots[cell] = (uint16_t)a->slot_of[id < a->megatiles ? id : 0];
	}
	return 0;
}

int kp_hd_terrain_build(KpHdTerrain *a, const KpHdTileset *t, const uint16_t *ids, int32_t map_w, int32_t map_h,
	KpError *err)
{
	int32_t *slot_of, used = 0, cell, rows, fit, per_layer;
	size_t tile_bytes = KP_HD_TILE_BC1(t->size), row_blocks, layer_bytes;
	int32_t tile_blocks = t->size / 4, y;
	memset(a, 0, sizeof *a);
	if (t->count <= 0) {
		snprintf(err->msg, sizeof err->msg, "HD terrain: a tileset without tiles");
		return -1;
	}
	slot_of = (int32_t *)malloc(sizeof *slot_of * (size_t)t->count);
	a->slots = (uint16_t *)malloc(sizeof *a->slots * (size_t)map_w * (size_t)map_h);
	if (!slot_of || !a->slots) goto oom;
	for (cell = 0; cell < t->count; cell++) slot_of[cell] = -1;
	for (cell = 0; cell < map_w * map_h; cell++) { /* each megatile the map uses gets a slot of the atlas */
		int32_t id = ids[cell] & 0x7fff;
		if (id >= t->count) id = 0; /* not one of the tileset's (a damaged map): the first */
		if (slot_of[id] < 0) slot_of[id] = used++;
		a->slots[cell] = (uint16_t)slot_of[id];
	}
	/* layers of at most SIDE x SIDE; one layer only as tall as its slots */
	fit = KP_HD_TERRAIN_SIDE / t->size > 0 ? KP_HD_TERRAIN_SIDE / t->size : 1;
	a->columns = fit;
	rows = used > 0 ? (used + fit - 1) / fit : 1;
	a->rows = rows < fit ? rows : fit;
	a->layers = (rows + a->rows - 1) / a->rows;
	if (a->layers > KP_HD_TERRAIN_LAYERS) {
		free(slot_of);
		kp_hd_terrain_free(a);
		snprintf(err->msg, sizeof err->msg, "HD terrain: %d megatiles are more than its layers hold", (int)used);
		return -1;
	}
	a->format = t->format;
	a->w = a->columns * t->size;
	a->h = a->rows * t->size;
	a->tile = t->size;
	per_layer = a->columns * a->rows;
	row_blocks = (size_t)(a->w / 4);
	layer_bytes = row_blocks * (size_t)(a->h / 4) * 8;
	a->size = t->tiles ? layer_bytes * (size_t)a->layers : 0;
	if (t->tiles && !(a->blocks = (uint8_t *)calloc(a->size, 1))) goto oom;
	if (t->water_map) { /* the masks of the slots' megatiles, at their places (dry: zero) */
		int wet = 0;
		for (cell = 0; cell < t->count && !wet; cell++) wet = slot_of[cell] >= 0 && kp_le_u16(t->water_map + 2 * cell);
		if (wet && !(a->water = (uint8_t *)calloc((size_t)a->w * (size_t)a->h * (size_t)a->layers, 1))) goto oom;
	}
	for (cell = 0; cell < t->count; cell++) { /* the tile's block rows into its slot's place */
		int32_t s = slot_of[cell], sx, sy, layer;
		const uint8_t *src = t->tiles ? t->tiles + tile_bytes * (size_t)cell : NULL;
		if (s < 0) continue;
		layer = s / per_layer;
		sx = (s % a->columns) * tile_blocks;
		sy = (s % per_layer / a->columns) * tile_blocks;
		if (a->water && kp_le_u16(t->water_map + 2 * cell)) {
			const uint8_t *m = t->water_masks + (size_t)(kp_le_u16(t->water_map + 2 * cell) - 1) * (size_t)t->size
				* (size_t)t->size;
			uint8_t *to = a->water + (size_t)layer * (size_t)a->w * (size_t)a->h;
			for (y = 0; y < t->size; y++) {
				memcpy(to + (size_t)(sy * 4 + y) * (size_t)a->w + (size_t)(sx * 4), m + (size_t)y * (size_t)t->size,
					(size_t)t->size);
			}
		}
		for (y = 0; src && y < tile_blocks; y++) {
			memcpy(a->blocks + layer_bytes * (size_t)layer + ((size_t)(sy + y) * row_blocks + (size_t)sx) * 8,
				src + (size_t)y * (size_t)tile_blocks * 8, (size_t)tile_blocks * 8);
		}
	}
	a->slot_of = slot_of; /* kept: kp_hd_terrain_relink */
	a->megatiles = t->count;
	a->edges = t->edges;
	a->edge_count = t->edge_count;
	a->map_w = map_w;
	a->map_h = map_h;
	return 0;
oom:
	free(slot_of);
	kp_hd_terrain_free(a);
	snprintf(err->msg, sizeof err->msg, "HD terrain: out of memory");
	return -1;
}
