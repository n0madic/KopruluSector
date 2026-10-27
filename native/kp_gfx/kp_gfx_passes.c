/*
 * kp_gfx's passes (plan phase S, stage S4; stage S8 removed the 1:1 port's limits):
 *   1. the terrain, then the world images into the index target, composed by depth rank (ADR 0006): the overwriting
 *      images write their rank as depth with test GREATER;
 *      the destination images test without moving the rank, one layer at a time, each layer reading a fresh copy of
 *      the index target; each distortion image reads a copy of everything ranked before it;
 *      then the fog of war darkens the composed view in index space, reading a copy of the index target;
 *      then the interface's canvas, its opaque texels written into the index target (ADR 0011);
 *   2. the palette pass: index -> RGB through the 256 x 1 palette texture;
 *   3. the scaling to the window: "integer" (nearest, a whole factor) or "sharp" (a fractional factor, the
 *      sharp-bilinear upscale).
 * The index target holds the palette index (R8) and the images' ranks as its depth, kept across the copies (the 1:1
 * port of stage S4 restored the target from the copy and kept the rank in G and B; stage S8 removed that under the
 * oracles).
 */
#include "kp_gfx_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FULLSCREEN 6

struct KpGfx {
	KpGfxApi api;
	KpGfxBackend *b;
	int world, tables, terrain, fog, canvas, blend, cursor;
	float trans50, bright;
	int32_t map_w, map_h, sheet_columns;
	float fog_map[2], fog_origin[4]; /* the fog layer's map point, its first block and size */
	const uint8_t *fog_dark; /* the dark rows uploaded (a tileset's, unchanged while it is the same) */
	float cursor_rect[4];
	uint8_t blend_row[256]; /* the blend row uploaded */
	uint8_t palette[256 * 4]; /* RGBA, uploaded on the next frame when changed */
	int palette_dirty;
	int32_t world_w, world_h;
	int32_t text_quads;       /* the vector text's quads uploaded (0: none drawn) */
	/* the HD world (ADR 0013): its art is up (pages, frame table, team colours), drawn instead of the index world */
	int hd_pages, hd_slots, hd_frames, hd_colors, hd_terrain, hd_on;
	int32_t hd_page_size;
	int hd_page_blocks; /* the slots hold BC blocks (else decoded texels) */
	float hd_scale, hd_shadow;
	int32_t hd_columns, hd_tile, hd_map_w, hd_map_h;
};

KpGfx *kp_gfx_new(const KpGfxApi *api, char *err, size_t err_size)
{
	KpGfx *g = (KpGfx *)calloc(1, sizeof *g);
	int k;
	if (!g) {
		snprintf(err, err_size, "gfx: out of memory");
		return NULL;
	}
	g->api = *api;
	if (!(g->b = kp_gb_new(api, err, err_size))) {
		free(g);
		return NULL;
	}
	for (k = 0; k < 256; k++) g->palette[4 * k + 3] = 255;
	g->palette_dirty = 1;
	return g;
}

void kp_gfx_free(KpGfx *g)
{
	if (!g) return;
	kp_gb_free(g->b, 0);
	free(g);
}

int kp_gfx_reset(KpGfx *g, char *err, size_t err_size)
{
	uint8_t palette[256 * 4];
	KpGfxApi api = g->api;
	memcpy(palette, g->palette, sizeof palette);
	kp_gb_free(g->b, 1);
	memset(g, 0, sizeof *g);
	g->api = api;
	memcpy(g->palette, palette, sizeof palette);
	g->palette_dirty = 1;
	g->b = kp_gb_new(&g->api, err, err_size);
	return g->b ? 0 : -1;
}

/* --- resources --- */

void kp_gfx_lookups(KpGfx *g, const KpLookups *l)
{
	if (!g->b) return; /* a failed reset: no backend */
	kp_gb_texture(g->b, KP_TEX_REMAP, KP_FMT_R8, 256, l->remap_rows, 0);
	kp_gb_upload(g->b, KP_TEX_REMAP, 0, 0, 0, 256, l->remap_rows, l->remap, 0);
	kp_gb_texture(g->b, KP_TEX_TABLES, KP_FMT_R8, 256, l->table_rows, 0);
	kp_gb_upload(g->b, KP_TEX_TABLES, 0, 0, 0, 256, l->table_rows, l->tables, 0);
	g->trans50 = (float)l->trans50;
	g->bright = (float)l->bright;
	g->tables = 1;
}

void kp_gfx_palette(KpGfx *g, const uint8_t *rgb)
{
	int k;
	for (k = 0; k < 256; k++) {
		g->palette[4 * k] = rgb[3 * k];
		g->palette[4 * k + 1] = rgb[3 * k + 1];
		g->palette[4 * k + 2] = rgb[3 * k + 2];
	}
	g->palette_dirty = 1;
}

const uint8_t *kp_gfx_palette_rgba(const KpGfx *g) { return g->palette; }

void kp_gfx_pages(KpGfx *g, int count, int32_t w, int32_t h, const uint8_t *const *pages)
{
	int k;
	if (!g->b) return;
	kp_gb_texture(g->b, KP_TEX_PAGES, KP_FMT_R8, w, h, count > 0 ? count : 1);
	for (k = 0; k < count; k++) kp_gb_upload(g->b, KP_TEX_PAGES, k, 0, 0, w, h, pages[k], 0);
}

void kp_gfx_frame_table(KpGfx *g, const KpFrameTable *t)
{
	if (!g->b) return;
	kp_gb_texture(g->b, KP_TEX_FRAMES, KP_FMT_RGBA32F, KP_FRAME_TABLE_WIDTH, t->height, 0);
	kp_gb_upload(g->b, KP_TEX_FRAMES, 0, 0, 0, KP_FRAME_TABLE_WIDTH, t->height, t->texels, 0);
}

void kp_gfx_image_data(KpGfx *g, const KpImageData *d)
{
	if (!g->b) return;
	kp_gb_texture(g->b, KP_TEX_IMAGE_DATA, KP_FMT_RGBA32F, KP_IMAGE_DATA_WIDTH, d->height, 0);
	kp_gb_upload(g->b, KP_TEX_IMAGE_DATA, 0, 0, 0, KP_IMAGE_DATA_WIDTH, d->height, d->data, 0);
}

void kp_gfx_image_rows(KpGfx *g, const KpImageData *d, int32_t row, int32_t rows)
{
	if (!g->b) return;
	if (!kp_gb_has_texture(g->b, KP_TEX_IMAGE_DATA, NULL, NULL)) {
		kp_gfx_image_data(g, d);
		return;
	}
	if (rows <= 0) return;
	kp_gb_upload(g->b, KP_TEX_IMAGE_DATA, 0, 0, row, KP_IMAGE_DATA_WIDTH, rows,
		d->data + (size_t)row * KP_IMAGE_DATA_WIDTH * 4, 0);
}

void kp_gfx_bars(KpGfx *g, const KpBars *b)
{
	if (!g->b) return;
	kp_gb_texture(g->b, KP_TEX_BARS, KP_FMT_RG8, KP_BAR_WIDTH, KP_BAR_ROWS * KP_BAR_SLOTS, 0);
	kp_gb_upload(g->b, KP_TEX_BARS, 0, 0, 0, KP_BAR_WIDTH, KP_BAR_ROWS * KP_BAR_SLOTS, b->pixels, 0);
}

void kp_gfx_bar_slots(KpGfx *g, const KpBars *b)
{
	int32_t w, h;
	if (!g->b || b->dirty_hi <= b->dirty_lo) return;
	if (!kp_gb_has_texture(g->b, KP_TEX_BARS, &w, &h) || w != KP_BAR_WIDTH || h != KP_BAR_ROWS * KP_BAR_SLOTS) {
		kp_gfx_bars(g, b);
		return;
	}
	kp_gb_upload(g->b, KP_TEX_BARS, 0, 0, b->dirty_lo * KP_BAR_ROWS, KP_BAR_WIDTH, (b->dirty_hi - b->dirty_lo) * KP_BAR_ROWS,
		b->pixels + (size_t)b->dirty_lo * KP_BAR_ROWS * KP_BAR_WIDTH * 2, 0);
}

void kp_gfx_world(KpGfx *g, int on)
{
	if (!g->b) return;
	g->world = on && kp_gb_has_texture(g->b, KP_TEX_PAGES, NULL, NULL) && kp_gb_has_texture(g->b, KP_TEX_FRAMES, NULL, NULL)
		&& kp_gb_has_texture(g->b, KP_TEX_IMAGE_DATA, NULL, NULL);
	if (g->world && !kp_gb_has_texture(g->b, KP_TEX_BARS, NULL, NULL)) {
		static const uint8_t none[2] = { 0, 0 };
		kp_gb_texture(g->b, KP_TEX_BARS, KP_FMT_RG8, 1, 1, 0);
		kp_gb_upload(g->b, KP_TEX_BARS, 0, 0, 0, 1, 1, none, 0);
	}
}

void kp_gfx_terrain(KpGfx *g, const uint8_t *sheet, int32_t sheet_w, int32_t sheet_h, int32_t columns,
	const uint16_t *ids, int32_t map_w, int32_t map_h)
{
	if (!g->b) return;
	g->terrain = 0;
	if (!ids) return;
	if (sheet) {
		kp_gb_texture(g->b, KP_TEX_SHEET, KP_FMT_R8, sheet_w, sheet_h, 0);
		kp_gb_upload(g->b, KP_TEX_SHEET, 0, 0, 0, sheet_w, sheet_h, sheet, 0);
		g->sheet_columns = columns;
	}
	kp_gb_texture(g->b, KP_TEX_MAP, KP_FMT_R16UI, map_w, map_h, 0);
	kp_gb_upload(g->b, KP_TEX_MAP, 0, 0, 0, map_w, map_h, ids, 0);
	g->map_w = map_w;
	g->map_h = map_h;
	g->terrain = kp_gb_has_texture(g->b, KP_TEX_SHEET, NULL, NULL);
}

void kp_gfx_fog(KpGfx *g, KpFogLayer *f, const uint8_t *dark)
{
	int32_t w, h;
	if (!g->b) return;
	if (!f || !f->on || f->width <= 0) {
		g->fog = 0;
		return;
	}
	if (dark && (dark != g->fog_dark || !kp_gb_has_texture(g->b, KP_TEX_FOG_DARK, NULL, NULL))) {
		kp_gb_texture(g->b, KP_TEX_FOG_DARK, KP_FMT_R8, 256, KP_DARK_ROWS, 0);
		kp_gb_upload(g->b, KP_TEX_FOG_DARK, 0, 0, 0, 256, KP_DARK_ROWS, dark, 0);
		g->fog_dark = dark;
	}
	if (!kp_gb_has_texture(g->b, KP_TEX_FOG_LEVELS, &w, &h) || w != f->width || h != f->height) {
		kp_gb_texture(g->b, KP_TEX_FOG_LEVELS, KP_FMT_R8, f->width, f->height, 0);
		f->dirty[0] = 0;
		f->dirty[1] = 0;
		f->dirty[2] = f->width;
		f->dirty[3] = f->height;
	}
	if (f->dirty[2] > f->dirty[0] && f->dirty[3] > f->dirty[1]) {
		kp_gb_upload(g->b, KP_TEX_FOG_LEVELS, 0, f->dirty[0], f->dirty[1], f->dirty[2] - f->dirty[0],
			f->dirty[3] - f->dirty[1], f->view.levels + (size_t)f->dirty[1] * (size_t)f->width + (size_t)f->dirty[0],
			(size_t)f->width);
	}
	kp_fog_layer_taken(f);
	g->fog_map[0] = (float)f->map_x;
	g->fog_map[1] = (float)f->map_y;
	g->fog_origin[0] = (float)f->block_x;
	g->fog_origin[1] = (float)f->block_y;
	g->fog_origin[2] = (float)f->width;
	g->fog_origin[3] = (float)f->height;
	g->fog = kp_gb_has_texture(g->b, KP_TEX_FOG_DARK, NULL, NULL);
}

void kp_gfx_canvas(KpGfx *g, const KpCanvas *c)
{
	int32_t w, h;
	if (!g->b) return;
	if (!c || c->w <= 0) {
		g->canvas = 0;
		return;
	}
	if (!kp_gb_has_texture(g->b, KP_TEX_CANVAS, &w, &h) || w != c->w || h != c->h) {
		kp_gb_texture(g->b, KP_TEX_CANVAS, KP_FMT_RG8, c->w, c->h, 0);
		kp_gb_upload(g->b, KP_TEX_CANVAS, 0, 0, 0, c->w, c->h, c->canvas.pixels, 0);
	} else if (c->dirty[2] > c->dirty[0] && c->dirty[3] > c->dirty[1]) {
		kp_gb_upload(g->b, KP_TEX_CANVAS, 0, c->dirty[0], c->dirty[1], c->dirty[2] - c->dirty[0], c->dirty[3] - c->dirty[1],
			c->canvas.pixels + 2 * ((size_t)c->dirty[1] * (size_t)c->w + (size_t)c->dirty[0]), 2 * (size_t)c->w);
	}
	g->blend = c->blend_valid;
	if (g->blend && (!kp_gb_has_texture(g->b, KP_TEX_BLEND, NULL, NULL) || memcmp(g->blend_row, c->blend_row, 256) != 0)) {
		kp_gb_texture(g->b, KP_TEX_BLEND, KP_FMT_R8, 256, 1, 0);
		kp_gb_upload(g->b, KP_TEX_BLEND, 0, 0, 0, 256, 1, c->blend_row, 0);
		memcpy(g->blend_row, c->blend_row, 256);
	}
	g->canvas = 1;
}

void kp_gfx_text(KpGfx *g, KpVText *v)
{
	int32_t rows, full, h;
	int made = 0;
	if (!g->b) return;
	g->text_quads = 0;
	if (!v || !v->atlas || v->quad_count <= 0) return;
	if (v->atlas_changed || !kp_gb_has_texture(g->b, KP_TEX_TEXT_ATLAS, NULL, NULL)) {
		kp_gb_texture(g->b, KP_TEX_TEXT_ATLAS, KP_FMT_R8, v->atlas_w, v->atlas_h, 0);
		kp_gb_upload(g->b, KP_TEX_TEXT_ATLAS, 0, 0, 0, v->atlas_w, v->atlas_h, v->atlas, 0);
		v->atlas_changed = 0;
	}
	rows = (v->quad_count + KP_TEXT_QUADS_PER_ROW - 1) / KP_TEXT_QUADS_PER_ROW;
	if (!kp_gb_has_texture(g->b, KP_TEX_TEXT_QUADS, NULL, &h) || h < rows) {
		kp_gb_texture(g->b, KP_TEX_TEXT_QUADS, KP_FMT_RGBA32F, 4 * KP_TEXT_QUADS_PER_ROW, rows, 0);
		made = 1;
	}
	if (made || v->quads_changed) { /* the quads change with the canvas's glyphs, not every frame */
		full = v->quad_count / KP_TEXT_QUADS_PER_ROW;
		if (full > 0) kp_gb_upload(g->b, KP_TEX_TEXT_QUADS, 0, 0, 0, 4 * KP_TEXT_QUADS_PER_ROW, full, v->quads, 0);
		if (v->quad_count > full * KP_TEXT_QUADS_PER_ROW) {
			kp_gb_upload(g->b, KP_TEX_TEXT_QUADS, 0, 0, full, 4 * (v->quad_count - full * KP_TEXT_QUADS_PER_ROW), 1,
				v->quads + (size_t)full * KP_TEXT_QUADS_PER_ROW * KP_VTEXT_QUAD_FLOATS, 0);
		}
		v->quads_changed = 0;
	}
	g->text_quads = v->quad_count;
}

void kp_gfx_cursor(KpGfx *g, KpCursor *c, int32_t x, int32_t y)
{
	if (!g->b) return;
	if (!c || !c->shown) {
		g->cursor = 0;
		return;
	}
	if (c->changed || !kp_gb_has_texture(g->b, KP_TEX_CURSOR, NULL, NULL)) {
		kp_gb_texture(g->b, KP_TEX_CURSOR, KP_FMT_R8, KP_CURSOR_SIZE, KP_CURSOR_SIZE, 0);
		kp_gb_upload(g->b, KP_TEX_CURSOR, 0, 0, 0, c->w, c->h, c->pixels, 0);
		c->changed = 0;
	}
	g->cursor_rect[0] = (float)x;
	g->cursor_rect[1] = (float)y;
	g->cursor_rect[2] = (float)c->w;
	g->cursor_rect[3] = (float)c->h;
	g->cursor = 1;
}

/* --- the HD world's resources (ADR 0013) --- */

int kp_gfx_hd_slots(KpGfx *g, int32_t count, int32_t page_size, int blocks)
{
	if (!g->b) return -1;
	g->hd_pages = g->hd_slots = 0;
	if (count <= 0) return 0;
	blocks = blocks && kp_gb_compressed(g->b);
	g->hd_page_blocks = blocks;
	kp_gb_texture(g->b, KP_TEX_HD_DIFFUSE, blocks ? KP_FMT_BC3 : KP_FMT_RGBA8, page_size, page_size, count);
	kp_gb_texture(g->b, KP_TEX_HD_TEAM, blocks ? KP_FMT_BC1 : KP_FMT_R8, page_size, page_size, count);
	g->hd_page_size = page_size;
	g->hd_pages = 1;
	return 0;
}

int kp_gfx_hd_blocks(KpGfx *g) { return g->b && kp_gb_compressed(g->b); }

void kp_gfx_hd_page(KpGfx *g, int32_t slot, const uint8_t *diffuse, const uint8_t *team)
{
	int32_t s = g->hd_page_size;
	size_t pixels = (size_t)s * (size_t)s;
	if (!g->b || !g->hd_pages) return;
	if (g->hd_page_blocks) {
		kp_gb_upload_blocks(g->b, KP_TEX_HD_DIFFUSE, slot, s, s, diffuse, pixels);
		kp_gb_upload_blocks(g->b, KP_TEX_HD_TEAM, slot, s, s, team, pixels / 2);
	} else { /* no S3TC (a phone's GLES, a browser without it): decoded by the caller */
		kp_gb_upload(g->b, KP_TEX_HD_DIFFUSE, slot, 0, 0, s, s, diffuse, 0);
		kp_gb_upload(g->b, KP_TEX_HD_TEAM, slot, 0, 0, s, s, team, 0);
	}
}

void kp_gfx_hd_page_slots(KpGfx *g, const int32_t *slot_of, int32_t pages)
{
	int32_t rows = (pages + KP_HD_SLOT_TABLE_WIDTH - 1) / KP_HD_SLOT_TABLE_WIDTH, k;
	float *texels;
	if (!g->b) return;
	if (rows < 1) rows = 1;
	if (!(texels = (float *)malloc(sizeof *texels * 4 * (size_t)KP_HD_SLOT_TABLE_WIDTH * (size_t)rows))) return;
	for (k = 0; k < KP_HD_SLOT_TABLE_WIDTH * rows; k++) {
		texels[4 * k] = k < pages ? (float)slot_of[k] : -1.0f;
		texels[4 * k + 1] = texels[4 * k + 2] = texels[4 * k + 3] = 0;
	}
	kp_gb_texture(g->b, KP_TEX_HD_SLOTS, KP_FMT_RGBA32F, KP_HD_SLOT_TABLE_WIDTH, rows, 0);
	kp_gb_upload(g->b, KP_TEX_HD_SLOTS, 0, 0, 0, KP_HD_SLOT_TABLE_WIDTH, rows, texels, 0);
	free(texels);
	g->hd_slots = 1;
}

void kp_gfx_hd_frame_table(KpGfx *g, const KpFrameTable *t, int scale)
{
	if (!g->b) return;
	kp_gb_texture(g->b, KP_TEX_HD_FRAMES, KP_FMT_RGBA32F, KP_FRAME_TABLE_WIDTH, t->height, 0);
	kp_gb_upload(g->b, KP_TEX_HD_FRAMES, 0, 0, 0, KP_FRAME_TABLE_WIDTH, t->height, t->texels, 0);
	g->hd_scale = (float)scale;
	g->hd_frames = 1;
}

void kp_gfx_hd_colors(KpGfx *g, const KpLookups *l, const uint8_t *rgb)
{
	uint8_t row[KP_LOOKUP_MAX_REMAP * 4];
	int32_t r, i;
	if (!g->b) return;
	for (r = 0; r < l->remap_rows; r++) { /* white: no team colour */
		uint8_t *o = row + 4 * r;
		o[0] = o[1] = o[2] = o[3] = 255;
		if (r >= 1 && r <= KP_LOOKUP_PLAYER_ROWS) { /* a player's colour: the brightest of its tunit entries */
			int best = -1;
			for (i = 0; i < KP_LOOKUP_PLAYER_COLORS; i++) {
				int c = l->remap[r * 256 + KP_LOOKUP_PLAYER_FIRST + i];
				int luma = 77 * rgb[3 * c] + 151 * rgb[3 * c + 1] + 28 * rgb[3 * c + 2];
				if (luma > best) {
					best = luma;
					memcpy(o, rgb + 3 * c, 3);
				}
			}
		}
		if (r == l->shift_row) o[3] = 0; /* the hallucination row */
	}
	kp_gb_texture(g->b, KP_TEX_HD_COLORS, KP_FMT_RGBA8, l->remap_rows, 1, 0);
	kp_gb_upload(g->b, KP_TEX_HD_COLORS, 0, 0, 0, l->remap_rows, 1, row, 0);
	g->hd_shadow = (float)(2 * l->shadow);
	g->hd_colors = 1;
}

void kp_gfx_hd_terrain(KpGfx *g, const KpHdTerrain *a)
{
	if (!g->b) return;
	g->hd_terrain = 0;
	if (!a || !a->blocks) return;
	if (kp_gb_compressed(g->b)) {
		kp_gb_texture(g->b, KP_TEX_HD_SHEET, KP_FMT_BC1, a->w, a->h, 1);
		kp_gb_upload_blocks(g->b, KP_TEX_HD_SHEET, 0, a->w, a->h, a->blocks, a->size);
	} else {
		uint8_t *rgba = (uint8_t *)malloc((size_t)a->w * (size_t)a->h * 4);
		if (!rgba) return;
		kp_bcn_decode(a->blocks, a->w, a->h, 0, rgba);
		kp_gb_texture(g->b, KP_TEX_HD_SHEET, KP_FMT_RGBA8, a->w, a->h, 1);
		kp_gb_upload(g->b, KP_TEX_HD_SHEET, 0, 0, 0, a->w, a->h, rgba, 0);
		free(rgba);
	}
	kp_gb_texture(g->b, KP_TEX_HD_MAP, KP_FMT_R16UI, a->map_w, a->map_h, 0);
	kp_gb_upload(g->b, KP_TEX_HD_MAP, 0, 0, 0, a->map_w, a->map_h, a->slots, 0);
	g->hd_columns = a->columns;
	g->hd_tile = a->tile;
	g->hd_map_w = a->map_w;
	g->hd_map_h = a->map_h;
	g->hd_terrain = 1;
}

void kp_gfx_hd(KpGfx *g, int on) { g->hd_on = on; }

int kp_gfx_hd_ready(const KpGfx *g)
{
	return g->b && g->hd_on && g->hd_pages && g->hd_slots && g->hd_frames && g->hd_colors;
}

/* --- the frame --- */

/* The world images' constants of a pass: the composition window. */
static void window(KpGfx *g, int prog, int32_t lo, int32_t hi, int32_t layer, int only_layer)
{
	kp_gb_uniform(g->b, prog, KP_U_WINDOW, (float)lo, (float)hi, (float)layer, (float)only_layer);
}

/* Copies the index target into the copy target, the source of the destination and distortion passes (a bound target
 * cannot be sampled); the index target keeps its contents and its depth (the ranks) across the switch. */
static void copy_target(KpGfx *g)
{
	kp_gb_copy(g->b, KP_TARGET_INDEX, KP_TARGET_DEST);
	kp_gb_bind(g->b, KP_TARGET_INDEX, 0, 0, g->world_w, g->world_h);
}

static const KpGfxBinding IMAGES[5] = { { KP_S_IMAGE_DATA, KP_TEX_IMAGE_DATA }, { KP_S_FRAME_TABLE, KP_TEX_FRAMES },
	{ KP_S_ATLAS, KP_TEX_PAGES }, { KP_S_REMAP, KP_TEX_REMAP }, { KP_S_BARS, KP_TEX_BARS } };
static const KpGfxBinding DEST[6] = { { KP_S_IMAGE_DATA, KP_TEX_IMAGE_DATA }, { KP_S_FRAME_TABLE, KP_TEX_FRAMES },
	{ KP_S_ATLAS, KP_TEX_PAGES }, { KP_S_REMAP, KP_TEX_REMAP }, { KP_S_DEST, KP_SAMPLE_TARGET + KP_TARGET_DEST },
	{ KP_S_TABLES, KP_TEX_TABLES } };
static const KpGfxBinding DISTORT[5] = { { KP_S_IMAGE_DATA, KP_TEX_IMAGE_DATA }, { KP_S_FRAME_TABLE, KP_TEX_FRAMES },
	{ KP_S_ATLAS, KP_TEX_PAGES }, { KP_S_REMAP, KP_TEX_REMAP }, { KP_S_DEST, KP_SAMPLE_TARGET + KP_TARGET_DEST } };

static void draw_family(KpGfx *g, int prog, const KpGfxBinding *bind, int count, int family)
{
	kp_gb_uniform(g->b, prog, KP_U_SLOT_BASE, (float)KP_FAMILY_BASE[family], 0, 0, 0);
	/* the rank is the depth: a destination image maps what is under it and leaves its rank (no depth write) */
	kp_gb_draw(g->b, prog, KP_FAMILY_CAPACITY[family] * 6, bind, count, KP_DEPTH_GREATER, family != KP_FAMILY_DEST);
}

/* One composition window: the overwriting images ranked lo..hi-1, then each of its destination layers after a fresh
 * copy of the index target. */
static void draw_window(KpGfx *g, int32_t lo, int32_t hi, int32_t layers)
{
	int32_t layer;
	window(g, KP_PROG_IMAGES, lo, hi, 0, 0);
	draw_family(g, KP_PROG_IMAGES, IMAGES, 5, KP_FAMILY_OVERWRITE);
	for (layer = 1; layer <= layers; layer++) {
		copy_target(g);
		window(g, KP_PROG_DEST, lo, hi, layer, 1);
		draw_family(g, KP_PROG_DEST, DEST, 6, KP_FAMILY_DEST);
	}
}

/* The world images by depth rank along the composition plan of the uploaded image data: each window, then the
 * distortion images reading the copy made after it, then the last window. */
static void draw_images(KpGfx *g, const KpGfxFrame *f)
{
	static const KpPlan empty;
	const KpPlan *plan = f->plan ? f->plan : &empty;
	float view[4];
	int32_t lo = 0, k;
	int progs[3] = { KP_PROG_IMAGES, KP_PROG_DEST, KP_PROG_DISTORT }, p;
	/* the world point at the target's bottom-left: the view's top-left map pixel in world coordinates, less the height */
	view[0] = (float)f->cam_x;
	view[1] = (float)(KP_WORLD_TOP - f->cam_y - f->world_h);
	view[2] = (float)f->world_w;
	view[3] = (float)f->world_h;
	for (p = 0; p < 3; p++) {
		kp_gb_uniform(g->b, progs[p], KP_U_VIEW, view[0], view[1], view[2], view[3]);
		kp_gb_uniform(g->b, progs[p], KP_U_INTERP, (float)f->interp, 0, 0, 0);
		kp_gb_uniform(g->b, progs[p], KP_U_LOOKUPS, g->trans50, g->bright, 0, 0);
	}
	for (k = 0; k < plan->count; k++) {
		const KpWindow *w = &plan->windows[k];
		draw_window(g, lo, w->rank, g->tables ? w->layers : 0);
		copy_target(g);
		window(g, KP_PROG_DISTORT, w->rank, w->last + 1, 0, 0);
		draw_family(g, KP_PROG_DISTORT, DISTORT, 5, KP_FAMILY_DISTORT);
		lo = w->rank + 1;
	}
	draw_window(g, lo, KP_RANK_RANGE, g->tables ? plan->layers : 0);
}

/* The fog of war over the composed index target: a copy is the pass's source, the pass writes every texel back. */
static void draw_fog(KpGfx *g)
{
	static const KpGfxBinding FOG[3] = { { KP_S_DEST, KP_SAMPLE_TARGET + KP_TARGET_DEST },
		{ KP_S_FOG_LEVELS, KP_TEX_FOG_LEVELS }, { KP_S_FOG_DARK, KP_TEX_FOG_DARK } };
	kp_gb_copy(g->b, KP_TARGET_INDEX, KP_TARGET_DEST);
	kp_gb_bind(g->b, KP_TARGET_INDEX, 0, 0, g->world_w, g->world_h);
	kp_gb_uniform(g->b, KP_PROG_FOG, KP_U_FOG, g->fog_map[0], g->fog_map[1], (float)g->world_h, 0);
	kp_gb_uniform(g->b, KP_PROG_FOG, KP_U_FOG_ORIGIN, g->fog_origin[0], g->fog_origin[1], g->fog_origin[2],
		g->fog_origin[3]);
	kp_gb_draw(g->b, KP_PROG_FOG, FULLSCREEN, FOG, 3, KP_DEPTH_OFF, 0);
}

/* The interface's canvas over the world in index space; its translucent texels map the world's index through the
 * blend row, reading a copy of the index target. */
static void draw_canvas(KpGfx *g)
{
	KpGfxBinding bind[4];
	int n = 0;
	if (g->blend) kp_gb_copy(g->b, KP_TARGET_INDEX, KP_TARGET_DEST);
	kp_gb_bind(g->b, KP_TARGET_INDEX, 0, 0, g->world_w, g->world_h);
	bind[n].sampler = KP_S_UI_CANVAS;
	bind[n++].texture = KP_TEX_CANVAS;
	bind[n].sampler = KP_S_UI_CURSOR;
	bind[n++].texture = g->cursor ? KP_TEX_CURSOR : KP_TEX_CANVAS;
	if (g->blend) {
		bind[n].sampler = KP_S_DEST;
		bind[n++].texture = KP_SAMPLE_TARGET + KP_TARGET_DEST;
		bind[n].sampler = KP_S_UI_BLEND;
		bind[n++].texture = KP_TEX_BLEND;
	} else {
		/* without translucent texels both read nothing: bound to the canvas */
		bind[n].sampler = KP_S_DEST;
		bind[n++].texture = KP_TEX_CANVAS;
		bind[n].sampler = KP_S_UI_BLEND;
		bind[n++].texture = KP_TEX_CANVAS;
	}
	if (g->cursor) {
		kp_gb_uniform(g->b, KP_PROG_UI, KP_U_CURSOR, g->cursor_rect[0], g->cursor_rect[1], g->cursor_rect[2],
			g->cursor_rect[3]);
	} else {
		kp_gb_uniform(g->b, KP_PROG_UI, KP_U_CURSOR, 0, 0, 0, 0);
	}
	kp_gb_uniform(g->b, KP_PROG_UI, KP_U_BLEND, g->blend ? 1.0f : 0.0f, 0, 0, 0);
	kp_gb_draw(g->b, KP_PROG_UI, FULLSCREEN, bind, n, KP_DEPTH_OFF, 0);
}

/* The vector text over the scaled frame, in the frame's viewport (bound by the caller). */
static void draw_text(KpGfx *g, const KpGfxFrame *f)
{
	KpGfxBinding bind[4];
	bind[0].sampler = KP_S_TEXT_QUADS;
	bind[0].texture = KP_TEX_TEXT_QUADS;
	bind[1].sampler = KP_S_TEXT_ATLAS;
	bind[1].texture = KP_TEX_TEXT_ATLAS;
	bind[2].sampler = KP_S_PALETTE;
	bind[2].texture = KP_TEX_PALETTE;
	bind[3].sampler = KP_S_UI_CURSOR;
	bind[3].texture = g->cursor ? KP_TEX_CURSOR : KP_TEX_TEXT_ATLAS;
	kp_gb_uniform(g->b, KP_PROG_TEXT, KP_U_SOURCE_SIZE, (float)f->world_w, (float)f->world_h, 0, 0);
	if (g->cursor) {
		kp_gb_uniform(g->b, KP_PROG_TEXT, KP_U_CURSOR, g->cursor_rect[0], g->cursor_rect[1], g->cursor_rect[2],
			g->cursor_rect[3]);
	} else {
		kp_gb_uniform(g->b, KP_PROG_TEXT, KP_U_CURSOR, 0, 0, 0, 0);
	}
	kp_gb_blend(g->b, 1);
	kp_gb_draw(g->b, KP_PROG_TEXT, g->text_quads * 6, bind, 4, KP_DEPTH_OFF, 0);
	kp_gb_blend(g->b, 0);
}

/* The HD world (ADR 0013) into the HD target: the terrain, the images' families with premultiplied colour by depth
 * rank (the overwriting ones writing their rank, the destination and distortion ones over them), the fog. */
static void draw_hd_world(KpGfx *g, const KpGfxFrame *f)
{
	static const KpGfxBinding TERRAIN[2] = { { KP_S_HD_MAP, KP_TEX_HD_MAP }, { KP_S_HD_SHEET, KP_TEX_HD_SHEET } };
	/* the SD frame table, atlas and remap rows draw an image whose HD page is not resident */
	static const KpGfxBinding HD_IMAGES[11] = { { KP_S_IMAGE_DATA, KP_TEX_IMAGE_DATA }, { KP_S_HD_FRAMES, KP_TEX_HD_FRAMES },
		{ KP_S_HD_DIFFUSE, KP_TEX_HD_DIFFUSE }, { KP_S_HD_TEAM, KP_TEX_HD_TEAM }, { KP_S_HD_COLORS, KP_TEX_HD_COLORS },
		{ KP_S_BARS, KP_TEX_BARS }, { KP_S_PALETTE, KP_TEX_PALETTE }, { KP_S_HD_SLOTS, KP_TEX_HD_SLOTS },
		{ KP_S_FRAME_TABLE, KP_TEX_FRAMES }, { KP_S_ATLAS, KP_TEX_PAGES }, { KP_S_REMAP, KP_TEX_REMAP } };
	static const KpGfxBinding FOG[1] = { { KP_S_FOG_LEVELS, KP_TEX_FOG_LEVELS } };
	static const int FAMILIES[3] = { KP_FAMILY_OVERWRITE, KP_FAMILY_DEST, KP_FAMILY_DISTORT };
	float s = g->hd_scale;
	int k;
	kp_gb_bind(g->b, KP_TARGET_HD, 0, 0, (int32_t)((float)f->world_w * s), (int32_t)((float)f->world_h * s));
	kp_gb_clear(g->b, 0, 0, 0, 1, 1);
	if (g->hd_terrain) {
		kp_gb_uniform(g->b, KP_PROG_HD_TERRAIN, KP_U_TERRAIN, (float)f->cam_x, (float)f->cam_y, (float)f->world_h,
			(float)g->hd_columns);
		kp_gb_uniform(g->b, KP_PROG_HD_TERRAIN, KP_U_TERRAIN_SIZE, (float)g->hd_map_w, (float)g->hd_map_h,
			(float)g->hd_tile, s);
		kp_gb_draw(g->b, KP_PROG_HD_TERRAIN, FULLSCREEN, TERRAIN, 2, KP_DEPTH_OFF, 0);
	}
	if (g->world) {
		kp_gb_uniform(g->b, KP_PROG_HD_IMAGES, KP_U_VIEW, (float)f->cam_x, (float)(KP_WORLD_TOP - f->cam_y - f->world_h),
			(float)f->world_w, (float)f->world_h);
		kp_gb_uniform(g->b, KP_PROG_HD_IMAGES, KP_U_INTERP, (float)f->interp, 0, 0, 0);
		kp_gb_blend_premultiplied(g->b);
		for (k = 0; k < 3; k++) {
			int family = FAMILIES[k];
			/* window.w: 0 the overwriting family (virtual frames), 2 every layer of the others */
			kp_gb_uniform(g->b, KP_PROG_HD_IMAGES, KP_U_WINDOW, 0, (float)KP_RANK_RANGE, 0, k == 0 ? 0.0f : 2.0f);
			kp_gb_uniform(g->b, KP_PROG_HD_IMAGES, KP_U_HD, s, (float)k, g->hd_shadow, 0);
			kp_gb_uniform(g->b, KP_PROG_HD_IMAGES, KP_U_SLOT_BASE, (float)KP_FAMILY_BASE[family], 0, 0, 0);
			kp_gb_draw(g->b, KP_PROG_HD_IMAGES, KP_FAMILY_CAPACITY[family] * 6, HD_IMAGES, 11, KP_DEPTH_GREATER,
				family == KP_FAMILY_OVERWRITE);
		}
		kp_gb_blend(g->b, 0);
	}
	if (g->fog) {
		kp_gb_uniform(g->b, KP_PROG_HD_FOG, KP_U_FOG, g->fog_map[0], g->fog_map[1], (float)f->world_h, 0);
		kp_gb_uniform(g->b, KP_PROG_HD_FOG, KP_U_FOG_ORIGIN, g->fog_origin[0], g->fog_origin[1], g->fog_origin[2],
			g->fog_origin[3]);
		kp_gb_uniform(g->b, KP_PROG_HD_FOG, KP_U_HD, s, 0, 0, 0);
		kp_gb_blend_premultiplied(g->b);
		kp_gb_draw(g->b, KP_PROG_HD_FOG, FULLSCREEN, FOG, 1, KP_DEPTH_OFF, 0);
		kp_gb_blend(g->b, 0);
	}
}

/* The window's picture over the HD world: the canvas and the cursor at the window's resolution, then the vector
 * text. */
static void draw_hd_frame(KpGfx *g, const KpGfxFrame *f, int32_t sw, int32_t sh)
{
	KpGfxBinding bind[4];
	int canvas = g->canvas && !f->world_only;
	draw_hd_world(g, f);
	bind[0].sampler = KP_S_HD_TARGET;
	bind[0].texture = KP_SAMPLE_TARGET + KP_TARGET_HD;
	bind[1].sampler = KP_S_UI_CANVAS;
	bind[1].texture = canvas ? KP_TEX_CANVAS : KP_TEX_PALETTE;
	bind[2].sampler = KP_S_UI_CURSOR;
	bind[2].texture = g->cursor ? KP_TEX_CURSOR : KP_TEX_PALETTE;
	bind[3].sampler = KP_S_PALETTE;
	bind[3].texture = KP_TEX_PALETTE;
	if (g->cursor && !f->world_only) {
		kp_gb_uniform(g->b, KP_PROG_HD_COMPOSITE, KP_U_CURSOR, g->cursor_rect[0], g->cursor_rect[1], g->cursor_rect[2],
			g->cursor_rect[3]);
	} else {
		kp_gb_uniform(g->b, KP_PROG_HD_COMPOSITE, KP_U_CURSOR, 0, 0, 0, 0);
	}
	kp_gb_uniform(g->b, KP_PROG_HD_COMPOSITE, KP_U_SOURCE_SIZE, (float)f->world_w, (float)f->world_h,
		canvas ? 1.0f : 0.0f, 0);
	if (f->mode == KP_SCALE_INTEGER) kp_gb_bind(g->b, KP_TARGET_WINDOW, 0, 0, sw, sh);
	else kp_gb_bind(g->b, KP_TARGET_WINDOW, 0, 0, f->win_w, f->win_h);
	kp_gb_draw(g->b, KP_PROG_HD_COMPOSITE, FULLSCREEN, bind, 4, KP_DEPTH_OFF, 0);
	if (g->text_quads > 0 && canvas) draw_text(g, f);
}

void kp_gfx_frame(KpGfx *g, const KpGfxFrame *f)
{
	static const KpGfxBinding TERRAIN[2] = { { KP_S_TERRAIN_MAP, KP_TEX_MAP }, { KP_S_TERRAIN_SHEET, KP_TEX_SHEET } };
	static const KpGfxBinding PALETTE[2] = { { KP_S_INDEX_TARGET, KP_SAMPLE_TARGET + KP_TARGET_INDEX },
		{ KP_S_PALETTE, KP_TEX_PALETTE } };
	static const KpGfxBinding SHARP[1] = { { KP_S_RGB_TARGET, KP_SAMPLE_TARGET + KP_TARGET_RGB } };
	int32_t sw, sh;
	if (!g->b) return;
	g->world_w = f->world_w;
	g->world_h = f->world_h;
	if (kp_gb_targets(g->b, f->world_w, f->world_h) != 0) return; /* incomplete targets: tried again next frame */
	if (g->palette_dirty) {
		kp_gb_texture(g->b, KP_TEX_PALETTE, KP_FMT_RGBA8, 256, 1, 0);
		kp_gb_upload(g->b, KP_TEX_PALETTE, 0, 0, 0, 256, 1, g->palette, 0);
		g->palette_dirty = 0;
	}
	sw = (int32_t)(f->world_w * f->scale + 0.5);
	sh = (int32_t)(f->world_h * f->scale + 0.5);
	if (kp_gfx_hd_ready(g) && !f->world_only
		&& kp_gb_hd_target(g->b, (int32_t)((float)f->world_w * g->hd_scale), (int32_t)((float)f->world_h * g->hd_scale))
			== 0) {
		draw_hd_frame(g, f, sw, sh);
		return;
	}
	/* 1. the terrain and the world images into the index target */
	kp_gb_bind(g->b, KP_TARGET_INDEX, 0, 0, f->world_w, f->world_h);
	kp_gb_clear(g->b, 0, 0, 0, 0, 1);
	if (g->terrain) {
		kp_gb_uniform(g->b, KP_PROG_TERRAIN, KP_U_TERRAIN, (float)f->cam_x, (float)f->cam_y, (float)f->world_h,
			(float)g->sheet_columns);
		kp_gb_uniform(g->b, KP_PROG_TERRAIN, KP_U_TERRAIN_SIZE, (float)g->map_w, (float)g->map_h, 0, 0);
		kp_gb_draw(g->b, KP_PROG_TERRAIN, FULLSCREEN, TERRAIN, 2, KP_DEPTH_OFF, 0);
	}
	if (g->world) draw_images(g, f);
	if (g->fog) draw_fog(g);
	if (g->canvas && !f->world_only) draw_canvas(g);
	/* 2. and 3. the palette pass and the scaling */
	kp_gb_uniform(g->b, KP_PROG_PALETTE, KP_U_SOURCE_SIZE, (float)f->world_w, (float)f->world_h, (float)f->scale,
		(float)f->scale);
	if (f->mode == KP_SCALE_INTEGER) {
		kp_gb_bind(g->b, KP_TARGET_WINDOW, 0, 0, sw, sh);
		kp_gb_draw(g->b, KP_PROG_PALETTE, FULLSCREEN, PALETTE, 2, KP_DEPTH_OFF, 0);
		if (g->text_quads > 0 && g->canvas && !f->world_only) draw_text(g, f);
	} else {
		kp_gb_bind(g->b, KP_TARGET_RGB, 0, 0, f->world_w, f->world_h);
		kp_gb_draw(g->b, KP_PROG_PALETTE, FULLSCREEN, PALETTE, 2, KP_DEPTH_OFF, 0);
		kp_gb_bind(g->b, KP_TARGET_WINDOW, 0, 0, f->win_w, f->win_h);
		kp_gb_uniform(g->b, KP_PROG_SHARP, KP_U_SOURCE_SIZE, (float)f->world_w, (float)f->world_h, (float)f->scale,
			(float)f->scale);
		kp_gb_draw(g->b, KP_PROG_SHARP, FULLSCREEN, SHARP, 1, KP_DEPTH_OFF, 0);
		if (g->text_quads > 0 && g->canvas && !f->world_only) draw_text(g, f);
	}
}

int kp_gfx_read_index(KpGfx *g, int32_t x, int32_t y, int32_t w, int32_t h, uint8_t *out)
{
	uint8_t *rgba;
	size_t k;
	int rc;
	if (!g->b || !(rgba = (uint8_t *)malloc((size_t)w * (size_t)h * 4))) return -1;
	rc = kp_gb_read(g->b, KP_TARGET_INDEX, x, y, w, h, rgba);
	for (k = 0; k < (size_t)w * (size_t)h; k++) out[k] = rgba[4 * k];
	free(rgba);
	return rc;
}

int kp_gfx_read_window(KpGfx *g, int32_t w, int32_t h, uint8_t *out)
{
	return g->b ? kp_gb_read(g->b, KP_TARGET_WINDOW, 0, 0, w, h, out) : -1;
}

int kp_gfx_lost(const KpGfx *g) { return g->b && !kp_gb_alive(g->b); }
