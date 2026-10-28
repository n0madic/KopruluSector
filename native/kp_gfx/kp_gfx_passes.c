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

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FULLSCREEN 6

struct KpGfx {
	KpGfxApi api;
	KpGfxBackend *b;
	int world, tables, terrain, fog, canvas, blend, cursor;
	float trans50, bright;
	int32_t map_w, map_h, sheet_columns, edge_base;
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
	int hd_water, hd_normals;    /* the map's water masks and the normal maps are up (stage R4c) */
	int32_t hd_normal_frames[2];
	int hd_tone, hd_tone_dirty;  /* the palette's tone for the HD world: its base set, to be made again */
	uint8_t hd_tone_base[768], hd_tone_skip[256];
	int32_t ui_items;            /* the HD interface's items uploaded (0: the canvas in SD) */
	float ui_cursor[9];          /* the cursor's HD frame: rectangle, texels, layer; ui_cursor_on */
	int ui_cursor_on;
	int ui_atlas, ui_ids;        /* its atlas made, the canvas's item plane uploaded */
	int32_t sd_scale;            /* stage R5: the original's art in RGBA at this scale (0: off), kp_gfx_effects */
	int fog_smooth, water_on;
	float gamma;                 /* the gamma option's exponent on the HD interface's art (1: none), kp_gfx_gamma */
};

/* The effects' defaults of a new gfx: the HD art's. */
static void effects_defaults(KpGfx *g)
{
	g->fog_smooth = 1;
	g->water_on = 1;
}

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
	g->gamma = 1.0f;
	effects_defaults(g);
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
	float gamma = g->gamma;
	memcpy(palette, g->palette, sizeof palette);
	kp_gb_free(g->b, 1);
	memset(g, 0, sizeof *g);
	g->api = api;
	memcpy(g->palette, palette, sizeof palette);
	g->palette_dirty = 1;
	g->gamma = gamma;
	effects_defaults(g);
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

void kp_gfx_gamma(KpGfx *g, int32_t brightness)
{
	/* kp_palette_gamma_table's exponent (the palette's colours have it; the HD interface's art takes it here) */
	if (brightness < KP_PALETTE_BRIGHTNESS_MIN || brightness > KP_PALETTE_BRIGHTNESS_MAX) {
		brightness = KP_PALETTE_BRIGHTNESS_DEFAULT;
	}
	g->gamma = (float)kp_palette_gamma_exponent(brightness);
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
	g->hd_tone_dirty = 1;
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

void kp_gfx_image_rows(KpGfx *g, const KpImageData *d, const KpRowRange *ranges, int count)
{
	int k;
	if (!g->b) return;
	if (!kp_gb_has_texture(g->b, KP_TEX_IMAGE_DATA, NULL, NULL)) {
		kp_gfx_image_data(g, d);
		return;
	}
	for (k = 0; k < count; k++) {
		if (ranges[k].rows <= 0) continue;
		kp_gb_upload(g->b, KP_TEX_IMAGE_DATA, 0, 0, ranges[k].row, KP_IMAGE_DATA_WIDTH, ranges[k].rows,
			d->data + (size_t)ranges[k].row * KP_IMAGE_DATA_WIDTH * 4, 0);
	}
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

void kp_gfx_terrain(KpGfx *g, const uint8_t *sheet, int32_t sheet_w, int32_t sheet_h, int32_t columns, int32_t edge_base,
	const uint16_t *cells, int32_t map_w, int32_t map_h)
{
	if (!g->b) return;
	g->terrain = 0;
	if (!cells) return;
	if (sheet) {
		kp_gb_texture(g->b, KP_TEX_SHEET, KP_FMT_R8, sheet_w, sheet_h, 0);
		kp_gb_upload(g->b, KP_TEX_SHEET, 0, 0, 0, sheet_w, sheet_h, sheet, 0);
		g->sheet_columns = columns;
		g->edge_base = edge_base;
	}
	kp_gb_texture(g->b, KP_TEX_MAP, KP_FMT_RG16UI, map_w, map_h, 0);
	kp_gb_upload(g->b, KP_TEX_MAP, 0, 0, 0, map_w, map_h, cells, 0);
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
	if (c->canvas.ids) { /* the HD interface's item plane, with the canvas's rectangle */
		if (!kp_gb_has_texture(g->b, KP_TEX_UI_IDS, &w, &h) || w != c->w || h != c->h || !g->ui_ids) {
			kp_gb_texture(g->b, KP_TEX_UI_IDS, KP_FMT_R16UI, c->w, c->h, 0);
			kp_gb_upload(g->b, KP_TEX_UI_IDS, 0, 0, 0, c->w, c->h, c->canvas.ids, 0);
		} else if (c->dirty[2] > c->dirty[0] && c->dirty[3] > c->dirty[1]) {
			kp_gb_upload(g->b, KP_TEX_UI_IDS, 0, c->dirty[0], c->dirty[1], c->dirty[2] - c->dirty[0],
				c->dirty[3] - c->dirty[1], c->canvas.ids + (size_t)c->dirty[1] * (size_t)c->w + (size_t)c->dirty[0],
				2 * (size_t)c->w);
		}
		g->ui_ids = 1;
	} else {
		g->ui_ids = 0;
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

/* The texture formats of a pack format's diffuse and team blocks. */
static void hd_formats(int format, int *diffuse, int *team)
{
	*diffuse = format == KP_HD_FORMAT_ETC2 ? KP_FMT_ETC2_RGBA8 : KP_FMT_BC3;
	*team = format == KP_HD_FORMAT_ETC2 ? KP_FMT_EAC_R11 : KP_FMT_BC1;
}

int kp_gfx_hd_slots(KpGfx *g, int32_t count, int32_t page_size, int format, int blocks)
{
	int diffuse, team;
	if (!g->b) return -1;
	g->hd_pages = g->hd_slots = 0;
	if (count <= 0) return 0;
	blocks = blocks && kp_gfx_hd_takes(g, format);
	g->hd_page_blocks = blocks;
	hd_formats(format, &diffuse, &team);
	kp_gb_texture(g->b, KP_TEX_HD_DIFFUSE, blocks ? diffuse : KP_FMT_RGBA8, page_size, page_size, count);
	kp_gb_texture(g->b, KP_TEX_HD_TEAM, blocks ? team : KP_FMT_R8, page_size, page_size, count);
	g->hd_page_size = page_size;
	g->hd_pages = 1;
	return 0;
}

int kp_gfx_hd_takes(KpGfx *g, int format)
{
	int diffuse, team;
	hd_formats(format, &diffuse, &team);
	return g->b && kp_gb_takes_blocks(g->b, diffuse) && kp_gb_takes_blocks(g->b, team);
}

void kp_gfx_hd_page(KpGfx *g, int32_t slot, const uint8_t *diffuse, const uint8_t *team)
{
	int32_t s = g->hd_page_size;
	size_t pixels = (size_t)s * (size_t)s;
	if (!g->b || !g->hd_pages) return;
	if (g->hd_page_blocks) {
		kp_gb_upload_blocks(g->b, KP_TEX_HD_DIFFUSE, slot, s, s, diffuse, pixels);
		kp_gb_upload_blocks(g->b, KP_TEX_HD_TEAM, slot, s, s, team, pixels / 2);
	} else { /* blocks the context does not take: decoded by the caller */
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

void kp_gfx_hd_colors(KpGfx *g, const KpLookups *l, const uint8_t *rgb, const uint8_t *minimap, size_t minimap_size)
{
	uint8_t row[KP_LOOKUP_MAX_REMAP * 4];
	int32_t r, i;
	if (!g->b) return;
	for (r = 0; r < l->remap_rows; r++) { /* white: no team colour; alpha: 255 a team colour, KP_GFX_HD_SOLID_ALPHA solid, 0 */
		uint8_t *o = row + 4 * r;
		o[0] = o[1] = o[2] = o[3] = 255;
		if (r >= 1 && r <= KP_LOOKUP_PLAYER_ROWS && (size_t)(r - 1) < minimap_size) {
			/* a player's colour: its minimap colour (tminimap.pcx), the colour that names the player, as StarCraft:
			 * Remastered's team colours do */
			memcpy(o, rgb + 3 * minimap[r - 1], 3);
		} else if (r >= 1 && r <= KP_LOOKUP_PLAYER_ROWS) { /* else the brightest of its tunit entries */
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
		if (r >= l->selection_base && r < l->selection_base + l->selections) {
			/* a selection row: StarCraft: Remastered's selection circles are grey art without a team mask, drawn in
			 * one solid colour; the colour the original's circles show (green, yellow, red) */
			memcpy(o, rgb + 3 * l->remap[r * 256 + KP_LOOKUP_SELECTION_CIRCLE], 3);
			o[3] = KP_GFX_HD_SOLID_ALPHA;
		}
		if (r == l->shift_row) o[3] = 0; /* the hallucination row */
	}
	kp_gb_texture(g->b, KP_TEX_HD_COLORS, KP_FMT_RGBA8, l->remap_rows, 1, 0);
	kp_gb_upload(g->b, KP_TEX_HD_COLORS, 0, 0, 0, l->remap_rows, 1, row, 0);
	g->hd_shadow = (float)(2 * l->shadow);
	g->hd_colors = 1;
}

/* The HD terrain's atlas (its tiles, water masks and the map's slots) and, with edges, the tileset's creep edges. */
static void hd_terrain(KpGfx *g, const KpHdTerrain *a, int edges)
{
	int format;
	if (!g->b) return;
	g->hd_terrain = 0;
	if (!a) return;
	format = a->format == KP_HD_FORMAT_ETC2 ? KP_FMT_ETC2_RGB8 : KP_FMT_BC1;
	if (!a->blocks) { /* the slots and water masks alone (the original's art drawn): no tiles */
		static const uint8_t none[4] = { 0, 0, 0, 0 };
		kp_gb_texture(g->b, KP_TEX_HD_SHEET, KP_FMT_RGBA8, 1, 1, 1);
		kp_gb_upload(g->b, KP_TEX_HD_SHEET, 0, 0, 0, 1, 1, none, 0);
	} else if (kp_gb_takes_blocks(g->b, format)) {
		kp_gb_texture(g->b, KP_TEX_HD_SHEET, format, a->w, a->h, 1);
		kp_gb_upload_blocks(g->b, KP_TEX_HD_SHEET, 0, a->w, a->h, a->blocks, a->size);
	} else {
		uint8_t *rgba = (uint8_t *)malloc((size_t)a->w * (size_t)a->h * 4);
		if (!rgba) return;
		if (a->format == KP_HD_FORMAT_ETC2) kp_etc2_rgb_decode(a->blocks, a->w, a->h, rgba);
		else kp_bcn_decode(a->blocks, a->w, a->h, 0, rgba);
		kp_gb_texture(g->b, KP_TEX_HD_SHEET, KP_FMT_RGBA8, a->w, a->h, 1);
		kp_gb_upload(g->b, KP_TEX_HD_SHEET, 0, 0, 0, a->w, a->h, rgba, 0);
		free(rgba);
	}
	/* the slots' water masks; none: a texel of dry land */
	kp_gb_texture(g->b, KP_TEX_HD_WATER, KP_FMT_R8, a->water ? a->w : 1, a->water ? a->h : 1, 0);
	if (a->water) {
		kp_gb_upload(g->b, KP_TEX_HD_WATER, 0, 0, 0, a->w, a->h, a->water, 0);
	} else {
		static const uint8_t dry = 0;
		kp_gb_upload(g->b, KP_TEX_HD_WATER, 0, 0, 0, 1, 1, &dry, 0);
	}
	g->hd_water = a->water != NULL;
	/* creep's edges, a layer each; none: a texel the terrain's shader takes for none */
	if (edges && a->edges && a->edge_count > 0) {
		int32_t k;
		kp_gb_texture(g->b, KP_TEX_HD_EDGES, KP_FMT_RGBA8, a->tile, a->tile, a->edge_count);
		for (k = 0; k < a->edge_count; k++) {
			kp_gb_upload(g->b, KP_TEX_HD_EDGES, k, 0, 0, a->tile, a->tile,
				a->edges + (size_t)k * (size_t)a->tile * (size_t)a->tile * 4, 0);
		}
	} else if (edges) {
		static const uint8_t none[4] = { 0, 0, 0, 0 };
		kp_gb_texture(g->b, KP_TEX_HD_EDGES, KP_FMT_RGBA8, 1, 1, 1);
		kp_gb_upload(g->b, KP_TEX_HD_EDGES, 0, 0, 0, 1, 1, none, 0);
	}
	kp_gb_texture(g->b, KP_TEX_HD_MAP, KP_FMT_R16UI, a->map_w, a->map_h, 0);
	kp_gb_upload(g->b, KP_TEX_HD_MAP, 0, 0, 0, a->map_w, a->map_h, a->slots, 0);
	g->hd_columns = a->columns;
	g->hd_tile = a->tile;
	g->hd_map_w = a->map_w;
	g->hd_map_h = a->map_h;
	g->hd_terrain = 1;
}

void kp_gfx_hd_terrain(KpGfx *g, const KpHdTerrain *a) { hd_terrain(g, a, 1); }

void kp_gfx_hd_terrain_atlas(KpGfx *g, const KpHdTerrain *a) { hd_terrain(g, a, 0); }

void kp_gfx_hd_terrain_slots(KpGfx *g, const KpHdTerrain *a)
{
	if (!g->b || !g->hd_terrain || a->map_w != g->hd_map_w || a->map_h != g->hd_map_h) return;
	kp_gb_upload(g->b, KP_TEX_HD_MAP, 0, 0, 0, a->map_w, a->map_h, a->slots, 0);
}

void kp_gfx_hd_water(KpGfx *g, const KpHdWater *w)
{
	static const int TEX[2] = { KP_TEX_HD_NORMAL_A, KP_TEX_HD_NORMAL_B };
	int k;
	if (!g->b) return;
	g->hd_normals = 0;
	for (k = 0; k < 2; k++) {
		int32_t size = w ? w->size[k] : 4, frames = w ? w->frames[k] : 1, f;
		int format = w && w->format == KP_HD_FORMAT_ETC2 ? KP_FMT_ETC2_RGB8 : KP_FMT_BC1;
		size_t image = (size_t)size * (size_t)size / 2;
		if (w && kp_gb_takes_blocks(g->b, format)) {
			kp_gb_texture(g->b, TEX[k], format, size, size, frames);
			for (f = 0; f < frames; f++) kp_gb_upload_blocks(g->b, TEX[k], f, size, size, w->blocks[k] + image * (size_t)f, image);
		} else { /* decoded; none: one flat normal */
			uint8_t *rgba = (uint8_t *)malloc((size_t)size * (size_t)size * 4);
			if (!rgba) return;
			kp_gb_texture(g->b, TEX[k], KP_FMT_RGBA8, size, size, frames);
			for (f = 0; f < frames; f++) {
				if (!w) memset(rgba, 128, (size_t)size * (size_t)size * 4);
				else if (w->format == KP_HD_FORMAT_ETC2) kp_etc2_rgb_decode(w->blocks[k] + image * (size_t)f, size, size, rgba);
				else kp_bcn_decode(w->blocks[k] + image * (size_t)f, size, size, 0, rgba);
				kp_gb_upload(g->b, TEX[k], f, 0, 0, size, size, rgba, 0);
			}
			free(rgba);
		}
		g->hd_normal_frames[k] = frames;
	}
	g->hd_normals = w != NULL;
}

void kp_gfx_hd_tone_base(KpGfx *g, const uint8_t rgb[768], const uint8_t skip[256])
{
	memcpy(g->hd_tone_base, rgb, 768);
	memcpy(g->hd_tone_skip, skip, 256);
	g->hd_tone = 1;
	g->hd_tone_dirty = 1;
}

/* The tone of the palette uploaded over its base (kp_palette_tone), made again when the palette changed. */
static void hd_tone(KpGfx *g)
{
	uint8_t current[768], tone[1024];
	int k;
	if (!g->hd_tone_dirty && kp_gb_has_texture(g->b, KP_TEX_HD_TONE, NULL, NULL)) return;
	if (g->hd_tone && g->sd_scale == 0) { /* the original's art reads the palette as it is: no tone */
		for (k = 0; k < 256; k++) memcpy(current + 3 * k, g->palette + 4 * k, 3);
		kp_palette_tone(g->hd_tone_base, current, g->hd_tone_skip, tone);
	} else {
		for (k = 0; k < 256; k++) tone[4 * k] = tone[4 * k + 1] = tone[4 * k + 2] = (uint8_t)k, tone[4 * k + 3] = 0;
	}
	kp_gb_texture(g->b, KP_TEX_HD_TONE, KP_FMT_RGBA8, 256, 1, 0);
	kp_gb_upload(g->b, KP_TEX_HD_TONE, 0, 0, 0, 256, 1, tone, 0);
	g->hd_tone_dirty = 0;
}

void kp_gfx_hd(KpGfx *g, int on) { g->hd_on = on; }

void kp_gfx_effects(KpGfx *g, int32_t sd_scale, int smooth, int water)
{
	if ((sd_scale > 0) != (g->sd_scale > 0)) g->hd_tone_dirty = 1; /* the original's art is in its palette already */
	g->sd_scale = sd_scale > 0 ? sd_scale : 0;
	g->fog_smooth = smooth;
	g->water_on = water;
}

/* The target's pixels a logical pixel of the RGBA world: the original's art's scale, else the HD art's. */
static float world_scale(const KpGfx *g) { return g->sd_scale > 0 ? (float)g->sd_scale : g->hd_scale; }

void kp_gfx_ui_atlas(KpGfx *g, int32_t size, int32_t layers)
{
	if (!g->b) return;
	kp_gb_texture(g->b, KP_TEX_UI_ATLAS, KP_FMT_RGBA8, size, size, layers);
	g->ui_atlas = 1;
}

void kp_gfx_ui_atlas_put(KpGfx *g, int32_t layer, int32_t x, int32_t y, int32_t w, int32_t h, const uint8_t *rgba)
{
	if (g->b && g->ui_atlas) kp_gb_upload(g->b, KP_TEX_UI_ATLAS, layer, x, y, w, h, rgba, 0);
}

void kp_gfx_ui_cursor(KpGfx *g, const float rect[4], const float src[4], float layer)
{
	g->ui_cursor_on = rect != NULL;
	if (!rect) return;
	memcpy(g->ui_cursor, rect, 4 * sizeof *rect);
	memcpy(g->ui_cursor + 4, src, 4 * sizeof *src);
	g->ui_cursor[8] = layer;
}

void kp_gfx_ui_items(KpGfx *g, const float *items, int32_t count)
{
	if (!g->b) return;
	g->ui_items = 0;
	if (count <= 0 || !g->ui_atlas) return;
	kp_gb_texture(g->b, KP_TEX_UI_ITEMS, KP_FMT_RGBA32F, KP_GFX_UI_ITEM_FLOATS / 4, count + 1, 0);
	kp_gb_upload(g->b, KP_TEX_UI_ITEMS, 0, 0, 0, KP_GFX_UI_ITEM_FLOATS / 4, count + 1, items, 0);
	g->ui_items = count;
}

int kp_gfx_hd_ready(const KpGfx *g)
{
	return g->b && g->hd_on && (g->sd_scale > 0 || (g->hd_pages && g->hd_slots && g->hd_frames && g->hd_colors));
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

/* A texture of one zero texel where none is up (a sampler of a pass that does not read it: WebGL checks them all). */
static void placeholder(KpGfx *g, int texture, int format, int layers)
{
	static const uint8_t zero[16] = { 0 };
	if (kp_gb_has_texture(g->b, texture, NULL, NULL)) return;
	kp_gb_texture(g->b, texture, format, 1, 1, layers);
	kp_gb_upload(g->b, texture, 0, 0, 0, 1, 1, zero, 0);
}

/* The RGBA world into the HD target (ADR 0013): the terrain, the images' families with premultiplied colour by depth
 * rank (the overwriting ones writing their rank, the destination and distortion ones over them), the fog. The HD
 * art's, or the original's at sd_scale (stage R5: its sheet and frames in the palette's colours, the HD art's textures
 * the passes do not read as placeholders, the original's fog per fragment unless the fog is smooth). */
static void draw_hd_world(KpGfx *g, const KpGfxFrame *f)
{
	static const KpGfxBinding TERRAIN[11] = { { KP_S_HD_MAP, KP_TEX_HD_MAP }, { KP_S_HD_SHEET, KP_TEX_HD_SHEET },
		{ KP_S_HD_WATER, KP_TEX_HD_WATER }, { KP_S_HD_NORMAL_A, KP_TEX_HD_NORMAL_A },
		{ KP_S_HD_NORMAL_B, KP_TEX_HD_NORMAL_B }, { KP_S_TERRAIN_MAP, KP_TEX_MAP }, { KP_S_TERRAIN_SHEET, KP_TEX_SHEET },
		{ KP_S_PALETTE, KP_TEX_PALETTE }, { KP_S_FOG_LEVELS, KP_TEX_FOG_LEVELS }, { KP_S_FOG_DARK, KP_TEX_FOG_DARK },
		{ KP_S_HD_EDGES, KP_TEX_HD_EDGES } };
	/* the SD frame table, atlas and remap rows draw an image whose HD page is not resident */
	static const KpGfxBinding HD_IMAGES[13] = { { KP_S_IMAGE_DATA, KP_TEX_IMAGE_DATA }, { KP_S_HD_FRAMES, KP_TEX_HD_FRAMES },
		{ KP_S_HD_DIFFUSE, KP_TEX_HD_DIFFUSE }, { KP_S_HD_TEAM, KP_TEX_HD_TEAM }, { KP_S_HD_COLORS, KP_TEX_HD_COLORS },
		{ KP_S_BARS, KP_TEX_BARS }, { KP_S_PALETTE, KP_TEX_PALETTE }, { KP_S_HD_SLOTS, KP_TEX_HD_SLOTS },
		{ KP_S_FRAME_TABLE, KP_TEX_FRAMES }, { KP_S_ATLAS, KP_TEX_PAGES }, { KP_S_REMAP, KP_TEX_REMAP },
		{ KP_S_FOG_LEVELS, KP_TEX_FOG_LEVELS }, { KP_S_FOG_DARK, KP_TEX_FOG_DARK } };
	static const KpGfxBinding FOG[1] = { { KP_S_FOG_LEVELS, KP_TEX_FOG_LEVELS } };
	static const int FAMILIES[3] = { KP_FAMILY_OVERWRITE, KP_FAMILY_DEST, KP_FAMILY_DISTORT };
	float s = world_scale(g), sd = g->sd_scale > 0 ? 1.0f : 0.0f;
	int dark = g->sd_scale > 0 && g->fog && !g->fog_smooth, ground = g->sd_scale > 0 ? g->terrain : g->hd_terrain, k;
	kp_gb_bind(g->b, KP_TARGET_HD, 0, 0, (int32_t)((float)f->world_w * s), (int32_t)((float)f->world_h * s));
	kp_gb_clear(g->b, 0, 0, 0, 1, 1);
	placeholder(g, KP_TEX_FOG_LEVELS, KP_FMT_R8, 0);
	placeholder(g, KP_TEX_FOG_DARK, KP_FMT_R8, 0);
	placeholder(g, KP_TEX_HD_EDGES, KP_FMT_RGBA8, 1);
	if (g->sd_scale > 0) { /* the HD art's textures, unless its water masks are up */
		placeholder(g, KP_TEX_HD_MAP, KP_FMT_R16UI, 0);
		placeholder(g, KP_TEX_HD_SHEET, KP_FMT_RGBA8, 1);
		placeholder(g, KP_TEX_HD_WATER, KP_FMT_R8, 0);
		placeholder(g, KP_TEX_HD_FRAMES, KP_FMT_RGBA32F, 0);
		placeholder(g, KP_TEX_HD_SLOTS, KP_FMT_RGBA32F, 0);
		placeholder(g, KP_TEX_HD_DIFFUSE, KP_FMT_RGBA8, 1);
		placeholder(g, KP_TEX_HD_TEAM, KP_FMT_R8, 1);
		placeholder(g, KP_TEX_HD_COLORS, KP_FMT_RGBA8, 0);
	}
	if (ground) {
		kp_gb_uniform(g->b, KP_PROG_HD_TERRAIN, KP_U_TERRAIN, (float)f->cam_x, (float)f->cam_y, (float)f->world_h,
			(float)g->hd_columns);
		kp_gb_uniform(g->b, KP_PROG_HD_TERRAIN, KP_U_TERRAIN_SIZE, (float)g->map_w, (float)g->map_h,
			(float)(g->hd_tile > 0 ? g->hd_tile : 32), s);
		if (!kp_gb_has_texture(g->b, KP_TEX_HD_NORMAL_A, NULL, NULL)) kp_gfx_hd_water(g, NULL);
		kp_gb_uniform(g->b, KP_PROG_HD_TERRAIN, KP_U_WATER, (float)fmod(f->time, KP_HD_WATER_PERIOD),
			g->hd_terrain && g->hd_water && g->hd_normals && g->water_on ? 1.0f : 0.0f, (float)g->hd_normal_frames[0],
			(float)g->hd_normal_frames[1]);
		kp_gb_uniform(g->b, KP_PROG_HD_TERRAIN, KP_U_ART, sd, (float)g->sheet_columns, dark ? 1.0f : 0.0f,
			(float)g->edge_base);
		kp_gb_uniform(g->b, KP_PROG_HD_TERRAIN, KP_U_FOG, g->fog_map[0], g->fog_map[1], (float)f->world_h, 0);
		kp_gb_uniform(g->b, KP_PROG_HD_TERRAIN, KP_U_FOG_ORIGIN, g->fog_origin[0], g->fog_origin[1], g->fog_origin[2],
			g->fog_origin[3]);
		kp_gb_draw(g->b, KP_PROG_HD_TERRAIN, FULLSCREEN, TERRAIN, 11, KP_DEPTH_OFF, 0);
	}
	if (g->world) {
		kp_gb_uniform(g->b, KP_PROG_HD_IMAGES, KP_U_VIEW, (float)f->cam_x, (float)(KP_WORLD_TOP - f->cam_y - f->world_h),
			(float)f->world_w, (float)f->world_h);
		kp_gb_uniform(g->b, KP_PROG_HD_IMAGES, KP_U_INTERP, (float)f->interp, 0, 0, 0);
		kp_gb_uniform(g->b, KP_PROG_HD_IMAGES, KP_U_ART, sd, 0, dark ? 1.0f : 0.0f, 0);
		kp_gb_uniform(g->b, KP_PROG_HD_IMAGES, KP_U_FOG, g->fog_map[0], g->fog_map[1], (float)f->world_h, 0);
		kp_gb_uniform(g->b, KP_PROG_HD_IMAGES, KP_U_FOG_ORIGIN, g->fog_origin[0], g->fog_origin[1], g->fog_origin[2],
			g->fog_origin[3]);
		kp_gb_blend_premultiplied(g->b);
		for (k = 0; k < 3; k++) {
			int family = FAMILIES[k];
			/* window.w: 0 the overwriting family (virtual frames), 2 every layer of the others */
			kp_gb_uniform(g->b, KP_PROG_HD_IMAGES, KP_U_WINDOW, 0, (float)KP_RANK_RANGE, 0, k == 0 ? 0.0f : 2.0f);
			kp_gb_uniform(g->b, KP_PROG_HD_IMAGES, KP_U_HD, s, (float)k, g->hd_shadow, sd);
			kp_gb_uniform(g->b, KP_PROG_HD_IMAGES, KP_U_SLOT_BASE, (float)KP_FAMILY_BASE[family], 0, 0, 0);
			kp_gb_draw(g->b, KP_PROG_HD_IMAGES, KP_FAMILY_CAPACITY[family] * 6, HD_IMAGES, 13, KP_DEPTH_GREATER,
				family == KP_FAMILY_OVERWRITE);
		}
		kp_gb_blend(g->b, 0);
	}
	if (g->fog && !dark) {
		kp_gb_uniform(g->b, KP_PROG_HD_FOG, KP_U_FOG, g->fog_map[0], g->fog_map[1], (float)f->world_h, 0);
		kp_gb_uniform(g->b, KP_PROG_HD_FOG, KP_U_FOG_ORIGIN, g->fog_origin[0], g->fog_origin[1], g->fog_origin[2],
			g->fog_origin[3]);
		kp_gb_uniform(g->b, KP_PROG_HD_FOG, KP_U_HD, s, 0, 0, 0);
		kp_gb_blend_premultiplied(g->b);
		kp_gb_draw(g->b, KP_PROG_HD_FOG, FULLSCREEN, FOG, 1, KP_DEPTH_OFF, 0);
		kp_gb_blend(g->b, 0);
	}
}

/* The HD interface's textures when it is off: one texel each of the samplers' types (WebGL checks them all). */
static void ui_placeholders(KpGfx *g)
{
	static const uint16_t id = 0;
	static const float item[KP_GFX_UI_ITEM_FLOATS] = { 0 };
	static const uint8_t texel[4] = { 0 };
	if (!kp_gb_has_texture(g->b, KP_TEX_UI_IDS, NULL, NULL)) {
		kp_gb_texture(g->b, KP_TEX_UI_IDS, KP_FMT_R16UI, 1, 1, 0);
		kp_gb_upload(g->b, KP_TEX_UI_IDS, 0, 0, 0, 1, 1, &id, 0);
	}
	if (!kp_gb_has_texture(g->b, KP_TEX_UI_ITEMS, NULL, NULL)) {
		kp_gb_texture(g->b, KP_TEX_UI_ITEMS, KP_FMT_RGBA32F, KP_GFX_UI_ITEM_FLOATS / 4, 1, 0);
		kp_gb_upload(g->b, KP_TEX_UI_ITEMS, 0, 0, 0, KP_GFX_UI_ITEM_FLOATS / 4, 1, item, 0);
	}
	if (!kp_gb_has_texture(g->b, KP_TEX_UI_ATLAS, NULL, NULL)) {
		kp_gb_texture(g->b, KP_TEX_UI_ATLAS, KP_FMT_RGBA8, 1, 1, 1);
		kp_gb_upload(g->b, KP_TEX_UI_ATLAS, 0, 0, 0, 1, 1, texel, 0);
	}
}

/* The window's picture over the HD world: the canvas (its HD interface items from their art) and the cursor at the
 * window's resolution, then the vector text. */
static void draw_hd_frame(KpGfx *g, const KpGfxFrame *f, int32_t sw, int32_t sh)
{
	KpGfxBinding bind[8];
	int canvas = g->canvas && !f->world_only, ui = canvas && g->ui_items > 0 && g->ui_ids;
	if (kp_gfx_hd_ready(g)) {
		draw_hd_world(g, f);
	} else { /* the interface alone (a glue screen): no world under it */
		kp_gb_bind(g->b, KP_TARGET_HD, 0, 0, f->world_w, f->world_h);
		kp_gb_clear(g->b, 0, 0, 0, 1, 1);
	}
	hd_tone(g);
	bind[4].sampler = KP_S_HD_TONE;
	bind[4].texture = KP_TEX_HD_TONE;
	if (!ui) ui_placeholders(g);
	bind[5].sampler = KP_S_UI_IDS;
	bind[5].texture = KP_TEX_UI_IDS;
	bind[6].sampler = KP_S_UI_ITEMS;
	bind[6].texture = KP_TEX_UI_ITEMS;
	bind[7].sampler = KP_S_UI_ATLAS;
	bind[7].texture = KP_TEX_UI_ATLAS;
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
		canvas ? 1.0f : 0.0f, ui ? 1.0f : 0.0f);
	{ /* the cursor's HD frame, over everything (its own alpha) */
		int on = g->ui_cursor_on && g->cursor && !f->world_only && g->ui_atlas;
		kp_gb_uniform(g->b, KP_PROG_HD_COMPOSITE, KP_U_HD_CURSOR_RECT, g->ui_cursor[0], g->ui_cursor[1], g->ui_cursor[2],
			g->ui_cursor[3]);
		kp_gb_uniform(g->b, KP_PROG_HD_COMPOSITE, KP_U_HD_CURSOR_SRC, g->ui_cursor[4], g->ui_cursor[5], g->ui_cursor[6],
			g->ui_cursor[7]);
		kp_gb_uniform(g->b, KP_PROG_HD_COMPOSITE, KP_U_HD_CURSOR_LAYER, g->ui_cursor[8], on ? 1.0f : 0.0f, 0, 0);
	}
	kp_gb_uniform(g->b, KP_PROG_HD_COMPOSITE, KP_U_UI_GAMMA, g->gamma, 0, 0, 0);
	if (f->mode == KP_SCALE_INTEGER) kp_gb_bind(g->b, KP_TARGET_WINDOW, f->win_x, f->win_y, sw, sh);
	else kp_gb_bind(g->b, KP_TARGET_WINDOW, f->win_x, f->win_y, f->win_w, f->win_h);
	kp_gb_draw(g->b, KP_PROG_HD_COMPOSITE, FULLSCREEN, bind, 8, KP_DEPTH_OFF, 0);
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
	if (f->win_x > 0 || f->win_y > 0 || f->full_w > f->win_w || f->full_h > f->win_h) {
		/* the drawable past the picture (a phone's system bars) black */
		kp_gb_bind(g->b, KP_TARGET_WINDOW, 0, 0, f->full_w, f->full_h);
		kp_gb_clear(g->b, 0, 0, 0, 1, 0);
	}
	/* the HD world, or the HD interface alone (a glue screen's items over no world) */
	if (!f->world_only && (kp_gfx_hd_ready(g) || (g->ui_items > 0 && !g->world))
		&& kp_gb_hd_target(g->b, kp_gfx_hd_ready(g) ? (int32_t)((float)f->world_w * world_scale(g)) : f->world_w,
			kp_gfx_hd_ready(g) ? (int32_t)((float)f->world_h * world_scale(g)) : f->world_h) == 0) {
		draw_hd_frame(g, f, sw, sh);
		return;
	}
	/* 1. the terrain and the world images into the index target */
	kp_gb_bind(g->b, KP_TARGET_INDEX, 0, 0, f->world_w, f->world_h);
	kp_gb_clear(g->b, 0, 0, 0, 0, 1);
	if (g->terrain) {
		kp_gb_uniform(g->b, KP_PROG_TERRAIN, KP_U_TERRAIN, (float)f->cam_x, (float)f->cam_y, (float)f->world_h,
			(float)g->sheet_columns);
		kp_gb_uniform(g->b, KP_PROG_TERRAIN, KP_U_TERRAIN_SIZE, (float)g->map_w, (float)g->map_h, (float)g->edge_base, 0);
		kp_gb_draw(g->b, KP_PROG_TERRAIN, FULLSCREEN, TERRAIN, 2, KP_DEPTH_OFF, 0);
	}
	if (g->world) draw_images(g, f);
	if (g->fog) draw_fog(g);
	if (g->canvas && !f->world_only) draw_canvas(g);
	/* 2. and 3. the palette pass and the scaling */
	kp_gb_uniform(g->b, KP_PROG_PALETTE, KP_U_SOURCE_SIZE, (float)f->world_w, (float)f->world_h, (float)f->scale,
		(float)f->scale);
	if (f->mode == KP_SCALE_INTEGER) {
		kp_gb_bind(g->b, KP_TARGET_WINDOW, f->win_x, f->win_y, sw, sh);
		kp_gb_draw(g->b, KP_PROG_PALETTE, FULLSCREEN, PALETTE, 2, KP_DEPTH_OFF, 0);
		if (g->text_quads > 0 && g->canvas && !f->world_only) draw_text(g, f);
	} else {
		kp_gb_bind(g->b, KP_TARGET_RGB, 0, 0, f->world_w, f->world_h);
		kp_gb_draw(g->b, KP_PROG_PALETTE, FULLSCREEN, PALETTE, 2, KP_DEPTH_OFF, 0);
		kp_gb_bind(g->b, KP_TARGET_WINDOW, f->win_x, f->win_y, f->win_w, f->win_h);
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

int kp_gfx_read_window(KpGfx *g, int32_t x, int32_t y, int32_t w, int32_t h, uint8_t *out)
{
	return g->b ? kp_gb_read(g->b, KP_TARGET_WINDOW, x, y, w, h, out) : -1;
}

int kp_gfx_lost(const KpGfx *g) { return g->b && !kp_gb_alive(g->b); }
