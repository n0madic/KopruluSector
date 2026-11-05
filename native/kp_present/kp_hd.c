/*
 * StarCraft: Remastered's HD art for the HD renderer (ADR 0013, stages R3 and R4a; converter/emit/hd.py): the HD packs
 * (kind HDPK) beside the SD packs, frame for frame, opened from their first bytes (their pages are read one at a time,
 * kp_hd_cache.c); the HD frame table, whose global frames are the SD table's; the HD tilesets (kind HDTS) and the atlas
 * of the megatiles a map uses. The blocks stay BC1/BC3 here; kp_gfx uploads them as they are where the context has
 * S3TC and decodes them (kp_bcn.c) where it has not.
 */
#include "kp_present_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HD_FRAME_RECORD 14
#define HD_TILE_BC1(s) ((size_t)(s) * (size_t)(s) / 2)

/* Each pack format's layers: 16 bytes a 4 x 4 block a byte a pixel, 8 half a byte. */
static const uint8_t HD_LAYER_BLOCK_BYTES[2][KP_HD_LAYERS] = { { 16, 8, 16, 16, 8, 8 }, { 16, 8, 16, 16, 8, 8 } };

int kp_hd_pack_format(const uint8_t *first, size_t n, size_t file_size)
{
	KpKpf c;
	KpError e;
	const KpKpfSection *head;
	int format = -1;
	if (kp_kpf_parse_prefix(&c, first, n, file_size, "HD pack", &e) != 0) return -1;
	if (strcmp(c.kind, KP_HD_PACK_KIND) == 0 && (head = kp_kpf_find(&c, "HEAD")) != NULL && head->data
		&& head->size >= KP_HD_PACK_HEAD_BYTES) {
		format = head->data[9];
	}
	kp_kpf_free(&c);
	return format;
}

size_t kp_hd_layer_bytes(int format, int layer, int32_t page_size)
{
	size_t blocks = (size_t)((page_size + 3) / 4);
	return blocks * blocks * HD_LAYER_BLOCK_BYTES[format][layer];
}

size_t kp_hd_pack_head_size(const uint8_t *first, size_t n, size_t file_size)
{
	static const char *const NAMES[2] = { "HEAD", "FRMS" };
	return kp_present_head_size(first, n, file_size, NAMES, 2);
}

int kp_hd_pack_open(KpHdPack *p, const uint8_t *head_bytes, size_t n, size_t file_size, const char *name, KpError *err)
{
	static const char *const LIGHTS[KP_HD_LIGHTS] = { "NRML", "AODP", "SPEC", "EMIS" };
	KpKpf c;
	KpKpfExpect expect;
	const KpKpfSection *head, *frms, *diff, *team;
	uint32_t f;
	int rc = -1, k;
	memset(p, 0, sizeof *p);
	if (kp_kpf_parse_prefix(&c, head_bytes, n, file_size, name, err) != 0) return -1;
	memset(&expect, 0, sizeof expect);
	expect.kind = KP_HD_PACK_KIND;
	expect.schema_version = KP_HD_PACK_SCHEMA;
	if (kp_kpf_check(&c, &expect, err) != 0) goto done;
	if (!(head = kp_kpf_require(&c, "HEAD", err)) || !(frms = kp_kpf_require(&c, "FRMS", err))
		|| !(diff = kp_kpf_require(&c, "DIFF", err)) || !(team = kp_kpf_require(&c, "TEAM", err))) {
		goto done;
	}
	if (!head->data || !frms->data) {
		kp_present_fail(err, name, "HEAD or FRMS past the bytes read");
		goto done;
	}
	if (head->size < KP_HD_PACK_HEAD_BYTES) {
		kp_present_fail(err, name, "HEAD truncated");
		goto done;
	}
	p->scale = head->data[0];
	p->page_size = (int32_t)kp_le_u16(head->data + 1);
	p->page_count = (int32_t)kp_le_u16(head->data + 3);
	p->frame_count = kp_le_u32(head->data + 5);
	p->format = head->data[9];
	p->lit_pages = (int32_t)kp_le_u16(head->data + 10);
	if ((p->scale != 2 && p->scale != 4) || p->page_size <= 0 || p->page_size % 4
		|| (p->format != KP_HD_FORMAT_BC && p->format != KP_HD_FORMAT_ETC2) || p->lit_pages > p->page_count) {
		kp_present_fail(err, name, "HEAD invalid");
		goto done;
	}
	if ((uint64_t)p->frame_count * HD_FRAME_RECORD != frms->size) {
		kp_present_fail(err, name, "FRMS size");
		goto done;
	}
	for (f = 0; f < p->frame_count; f++) { /* as the SD pack's (kp_pack_load); width 0: no HD frame */
		const uint8_t *r = frms->data + (size_t)f * HD_FRAME_RECORD;
		int32_t w = (int32_t)kp_le_u16(r + 6), x = (int32_t)kp_le_u16(r + 2), y = (int32_t)kp_le_u16(r + 4);
		if ((w && (int32_t)kp_le_u16(r) >= p->page_count) || x + w > p->page_size
			|| y + (int32_t)kp_le_u16(r + 8) > p->page_size) {
			kp_present_fail(err, name, "a frame outside its page");
			goto done;
		}
	}
	if (diff->size != kp_hd_layer_bytes(p->format, KP_HD_LAYER_DIFFUSE, p->page_size) * (size_t)p->page_count
		|| team->size != kp_hd_layer_bytes(p->format, KP_HD_LAYER_TEAM, p->page_size) * (size_t)p->page_count) {
		kp_present_fail(err, name, "DIFF or TEAM size");
		goto done;
	}
	for (k = 0; k < KP_HD_LIGHTS && p->lit_pages > 0; k++) {
		const KpKpfSection *s = kp_kpf_require(&c, LIGHTS[k], err);
		if (!s) goto done;
		if (s->size != kp_hd_layer_bytes(p->format, KP_HD_LAYER_LIGHT + k, p->page_size) * (size_t)p->lit_pages) {
			kp_present_fail(err, name, "a lighting layer's size");
			goto done;
		}
		p->light_offset[k] = s->offset;
	}
	p->frames = frms->data;
	p->diffuse_offset = diff->offset;
	p->team_offset = team->offset;
	rc = 0;
done:
	kp_kpf_free(&c);
	return rc;
}

int kp_hd_page_decode(int format, const uint8_t *diffuse, const uint8_t *team, int32_t size, uint8_t *rgba,
	uint8_t *mask)
{
	size_t pixels = (size_t)size * (size_t)size, i;
	uint8_t *team_rgba;
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

int kp_hd_light_to_etc2(int light, const uint8_t *bc, int32_t size, uint8_t *out)
{
	size_t pixels = (size_t)size * (size_t)size, i;
	uint8_t *rgba = (uint8_t *)malloc(pixels * 4), *r = NULL, *g = NULL;
	int rc = -1;
	if (!rgba) return -1;
	if (light == KP_HD_LIGHT_SPECULAR || light == KP_HD_LIGHT_EMISSIVE) {
		kp_bcn_decode(bc, size, size, 0, rgba);
		kp_etc2_rgb_encode(rgba, size, size, out);
		rc = 0;
	} else if ((r = (uint8_t *)calloc(pixels, 1)) != NULL && (g = (uint8_t *)malloc(pixels)) != NULL) {
		kp_bcn_decode(bc, size, size, 1, rgba);
		for (i = 0; i < pixels; i++) {
			if (light == KP_HD_LIGHT_NORMAL) {
				r[i] = rgba[4 * i];
				g[i] = rgba[4 * i + 3];
			} else {
				g[i] = rgba[4 * i + 1];
			}
		}
		kp_eac_rg11_encode(r, g, size, size, out);
		rc = 0;
	}
	free(rgba);
	free(r);
	free(g);
	return rc;
}

void kp_hd_page_offsets(const KpHdPack *p, int32_t page, uint32_t *diffuse, uint32_t *team,
	uint32_t light[KP_HD_LIGHTS])
{
	int k;
	*diffuse = p->diffuse_offset + (uint32_t)(kp_hd_layer_bytes(p->format, KP_HD_LAYER_DIFFUSE, p->page_size) * (size_t)page);
	*team = p->team_offset + (uint32_t)(kp_hd_layer_bytes(p->format, KP_HD_LAYER_TEAM, p->page_size) * (size_t)page);
	for (k = 0; k < KP_HD_LIGHTS && light; k++) {
		light[k] = page < p->lit_pages
			? p->light_offset[k] + (uint32_t)(kp_hd_layer_bytes(p->format, KP_HD_LAYER_LIGHT + k, p->page_size) * (size_t)page) : 0;
	}
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
			const uint8_t *r = hd_packs[k]->frames + (size_t)(pa->first + f) * HD_FRAME_RECORD;
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

int kp_hd_tileset_load(KpHdTileset *t, const uint8_t *data, size_t size, const char *name, KpError *err)
{
	KpKpf c;
	KpKpfExpect expect;
	const KpKpfSection *head, *tile;
	int rc = -1;
	memset(t, 0, sizeof *t);
	if (kp_kpf_parse(&c, data, size, name, 0, err) != 0) return -1;
	memset(&expect, 0, sizeof expect);
	expect.kind = KP_HD_TILESET_KIND;
	expect.schema_version = KP_HD_SCHEMA;
	if (kp_kpf_check(&c, &expect, err) != 0) goto done;
	if (!(head = kp_kpf_require(&c, "HEAD", err)) || !(tile = kp_kpf_require(&c, "TILE", err))) goto done;
	if (head->size < 8 || (memcmp(head->data + 4, "DXT1", 4) != 0 && memcmp(head->data + 4, "ETC2", 4) != 0)) {
		kp_present_fail(err, name, "HEAD: BC1 or ETC2 tiles expected");
		goto done;
	}
	t->format = memcmp(head->data + 4, "ETC2", 4) == 0 ? KP_HD_FORMAT_ETC2 : KP_HD_FORMAT_BC;
	t->count = (int32_t)kp_le_u16(head->data);
	t->size = (int32_t)kp_le_u16(head->data + 2);
	if (t->count <= 0 || t->size <= 0 || t->size % 4 || tile->size != HD_TILE_BC1(t->size) * (size_t)t->count) {
		kp_present_fail(err, name, "TILE size");
		goto done;
	}
	t->tiles = tile->data;
	{
		const KpKpfSection *wmap = kp_kpf_find(&c, "WMAP"), *wmsk = kp_kpf_find(&c, "WMSK");
		size_t mask = (size_t)t->size * (size_t)t->size;
		if (wmap && wmsk) {
			int32_t k;
			if (wmap->size != 2 * (size_t)t->count || wmsk->size % mask) {
				kp_present_fail(err, name, "WMAP or WMSK size");
				goto done;
			}
			t->water_mask_count = (int32_t)(wmsk->size / mask);
			for (k = 0; k < t->count; k++) {
				if (kp_le_u16(wmap->data + 2 * k) > (uint32_t)t->water_mask_count) {
					kp_present_fail(err, name, "WMAP names a mask past WMSK");
					goto done;
				}
			}
			t->water_map = wmap->data;
			t->water_masks = wmsk->data;
		}
	}
	{
		const KpKpfSection *edge = kp_kpf_find(&c, "EDGE");
		if (edge) {
			size_t texels = (size_t)t->size * (size_t)t->size * 4;
			if (edge->size < 2 || edge->size != 2 + texels * kp_le_u16(edge->data)) {
				kp_present_fail(err, name, "EDGE size");
				goto done;
			}
			t->edge_count = (int32_t)kp_le_u16(edge->data);
			t->edges = edge->data + 2;
		}
	}
	rc = 0;
done:
	kp_kpf_free(&c);
	return rc;
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
	size_t tile_bytes = HD_TILE_BC1(t->size), row_blocks, layer_bytes;
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

int kp_hd_water_load(KpHdWater *w, const uint8_t *data, size_t size, const char *name, KpError *err)
{
	KpKpf c;
	KpKpfExpect expect;
	const KpKpfSection *head, *sets[2];
	int rc = -1, k;
	memset(w, 0, sizeof *w);
	if (kp_kpf_parse(&c, data, size, name, 0, err) != 0) return -1;
	memset(&expect, 0, sizeof expect);
	expect.kind = KP_HD_WATER_KIND;
	expect.schema_version = KP_HD_SCHEMA;
	if (kp_kpf_check(&c, &expect, err) != 0) goto done;
	if (!(head = kp_kpf_require(&c, "HEAD", err)) || !(sets[0] = kp_kpf_require(&c, "NRMA", err))
		|| !(sets[1] = kp_kpf_require(&c, "NRMB", err))) {
		goto done;
	}
	if (head->size < 12 || (memcmp(head->data + 8, "DXT1", 4) != 0 && memcmp(head->data + 8, "ETC2", 4) != 0)) {
		kp_present_fail(err, name, "HEAD: BC1 or ETC2 normals expected");
		goto done;
	}
	w->format = memcmp(head->data + 8, "ETC2", 4) == 0 ? KP_HD_FORMAT_ETC2 : KP_HD_FORMAT_BC;
	for (k = 0; k < 2; k++) {
		w->frames[k] = (int32_t)kp_le_u16(head->data + 4 * k);
		w->size[k] = (int32_t)kp_le_u16(head->data + 4 * k + 2);
		if (w->frames[k] <= 0 || w->size[k] <= 0 || w->size[k] % 4
			|| sets[k]->size != HD_TILE_BC1(w->size[k]) * (size_t)w->frames[k]) {
			kp_present_fail(err, name, "NRMA or NRMB size");
			goto done;
		}
		w->blocks[k] = sets[k]->data;
	}
	rc = 0;
done:
	kp_kpf_free(&c);
	return rc;
}
