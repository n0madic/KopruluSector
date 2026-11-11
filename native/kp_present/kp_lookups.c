/*
 * The palettes' tables (kp_palettes_from_files) and the lookup rows of the world image materials (ADR 0006): the remap
 * texture (256 x rows, row k = what each index maps to) and the destination table texture, built for one tileset, and
 * the rows of the images' draw methods (kp_draw_row_of).
 *
 * Remap rows: identity; one per player colour (tunit, indices 8..15); shift; one per selection colour (tselect,
 * indices 1..8); the brightness of every palette index (0x445f30: (77 R + 151 G + 28 B + 4096) >> 13, 0..8), read by
 * the cloak threshold passes.
 * Table rows: every blend bank spans BANK_ROWS rows: the decoded PCX rows, then identity rows (the original reads
 * unrelated heap memory past the decoded rows; identity keeps the destination); then the shadow row (dark.pcx row 18).
 */
#include "kp_present_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --- palettes from the original files (ADR 0018, stage D3) --- */

static const char *const REMAP_NAMES[KP_REMAP_TABLES] = { "ofire", "gfire", "bfire", "bexpl", "trans50", "red", "green" };
#define GAME_TABLES 4
static const struct {
	const char *path;
	size_t size;
} GAME_TABLE_FILES[GAME_TABLES] = {
	{ "game\\tunit.pcx", 0x60 },    /* 12 player colour rows of 8 indices (0x41d570) */
	{ "game\\tminimap.pcx", 0x0c }, /* 12 minimap player colours (0x41d570) */
	{ "game\\tselect.pcx", 24 },    /* selection-circle colours (0x40fef0) */
	{ "game\\thpbar.pcx", 19 },     /* health-bar colours */
};

int kp_palettes_from_files(KpPalettes *p, const KpFiles *files, KpError *err)
{
	const uint8_t **game[GAME_TABLES];
	size_t *game_size[GAME_TABLES];
	uint8_t *tables[KP_TILESETS][KP_REMAP_TABLES];
	int32_t heights[KP_TILESETS][KP_REMAP_TABLES];
	size_t total = 0, at = 0;
	int t, k, rc = -1;
	char path[80];
	memset(p, 0, sizeof *p);
	memset(tables, 0, sizeof tables);
	game[0] = &p->tunit, game[1] = &p->tminimap, game[2] = &p->tselect, game[3] = &p->thpbar;
	game_size[0] = &p->tunit_size, game_size[1] = &p->tminimap_size, game_size[2] = &p->tselect_size,
	game_size[3] = &p->thpbar_size;
	for (t = 0; t < KP_TILESETS; t++)
		for (k = 0; k < KP_REMAP_TABLES; k++) {
			int32_t w;
			snprintf(path, sizeof path, "tileset\\%s\\%s.pcx", KP_TILESET_NAMES[t], REMAP_NAMES[k]);
			if (!(tables[t][k] = kp_files_pcx(files, path, &w, &heights[t][k], err))) goto done;
			if (w != 256 || heights[t][k] >= 0xffff) {
				snprintf(err->msg, sizeof err->msg, "%s is %d x %d; the game's table is 256 wide", path, (int)w,
					(int)heights[t][k]);
				goto done;
			}
			total += ((size_t)heights[t][k] + 1) * 256;
		}
	total += KP_TILESETS * (KP_DARK_SIZE + 256);
	for (k = 0; k < GAME_TABLES; k++) total += GAME_TABLE_FILES[k].size;
	if (!(p->owned = (uint8_t *)calloc(total, 1))) {
		kp_present_fail(err, "palettes", "out of memory");
		goto done;
	}
	for (t = 0; t < KP_TILESETS; t++) {
		for (k = 0; k < KP_REMAP_TABLES; k++) { /* a zero row, then the PCX's (kpd.tileset_palette_tables) */
			p->tilesets[t].remap[k].rows = (uint16_t)(heights[t][k] + 1);
			p->tilesets[t].remap[k].data = p->owned + at;
			memcpy(p->owned + at + 256, tables[t][k], (size_t)heights[t][k] * 256);
			at += ((size_t)heights[t][k] + 1) * 256;
		}
		snprintf(path, sizeof path, "tileset\\%s\\dark.pcx", KP_TILESET_NAMES[t]);
		if (kp_files_pcx_prefix(files, path, p->owned + at, KP_DARK_SIZE, err) != 0) goto done;
		p->tilesets[t].dark = p->owned + at;
		at += KP_DARK_SIZE;
		snprintf(path, sizeof path, "tileset\\%s\\shift.pcx", KP_TILESET_NAMES[t]);
		if (kp_files_pcx_prefix(files, path, p->owned + at, 256, err) != 0) goto done;
		p->tilesets[t].shift = p->owned + at;
		at += 256;
	}
	for (k = 0; k < GAME_TABLES; k++) {
		if (kp_files_pcx_prefix(files, GAME_TABLE_FILES[k].path, p->owned + at, GAME_TABLE_FILES[k].size, err) != 0) goto done;
		*game[k] = p->owned + at;
		*game_size[k] = GAME_TABLE_FILES[k].size;
		at += GAME_TABLE_FILES[k].size;
	}
	rc = 0;
done:
	for (t = 0; t < KP_TILESETS; t++)
		for (k = 0; k < KP_REMAP_TABLES; k++) free(tables[t][k]);
	if (rc != 0) kp_palettes_free(p);
	return rc;
}

void kp_palettes_free(KpPalettes *p)
{
	free(p->owned);
	memset(p, 0, sizeof *p);
}

static void identity_row(uint8_t *row)
{
	int i;
	for (i = 0; i < 256; i++) row[i] = (uint8_t)i;
}

/* The blend over a destination colour that best fits a table row (a destination index d becomes row[d]): the least
 * squares line out = colour + keep * destination over the palette's colours 1..255, one `keep` shared by the three
 * channels, clamped to what a premultiplied over blend does (0..1; a brightening row keeps everything and adds). The
 * rows of the glows, flames and explosions are a colour added with a coverage: the fit is within about 10 of 255 over
 * every destination of the stock tables. out[] = the premultiplied colour, then the coverage (1 - keep) in 0..255. */
static void fit_table_row(const uint8_t *row, const uint8_t *rgb, uint8_t out[4])
{
	double sum_in[3] = { 0, 0, 0 }, sum_out[3] = { 0, 0, 0 }, cross = 0, square = 0, keep, n = 255.0;
	int d, c;
	for (d = 1; d < 256; d++) {
		for (c = 0; c < 3; c++) {
			double in = rgb[3 * d + c], res = rgb[3 * row[d] + c];
			sum_in[c] += in;
			sum_out[c] += res;
			cross += in * res;
			square += in * in;
		}
	}
	for (c = 0; c < 3; c++) {
		cross -= sum_in[c] * sum_out[c] / n;
		square -= sum_in[c] * sum_in[c] / n;
	}
	keep = square > 0 ? cross / square : 1.0;
	keep = keep < 0 ? 0 : keep > 1 ? 1 : keep;
	for (c = 0; c < 3; c++) {
		double colour = (sum_out[c] - keep * sum_in[c]) / n;
		out[c] = (uint8_t)(colour < 0 ? 0 : colour > 255 ? 255 : colour + 0.5);
	}
	out[3] = (uint8_t)((1.0 - keep) * 255.0 + 0.5);
}

int kp_lookups_build(KpLookups *l, const KpPalettes *p, int tileset, const uint8_t *rgb, int32_t outline_color,
	KpError *err)
{
	const KpTilesetPalettes *ts = &p->tilesets[tileset & 7];
	int32_t rows = 0, k, i, n;
	memset(l, 0, sizeof *l);
	identity_row(l->remap);
	rows = 1;
	/* 0x416bd0 copies trans50 rows in place; the draw list's colour-mapped read equals it only while no player colour
	 * is itself one of the rows 8..15 (none in stock data) */
	for (k = 0; k < (int32_t)p->tunit_size; k++) {
		uint8_t c = p->tunit[k];
		if (c >= KP_LOOKUP_PLAYER_FIRST && c < KP_LOOKUP_PLAYER_FIRST + KP_LOOKUP_PLAYER_COLORS) {
			return kp_present_fail(err, "world_lookups", "a tunit colour inside the player rows");
		}
	}
	for (k = 0; k < KP_LOOKUP_PLAYER_ROWS; k++) {
		uint8_t *row = &l->remap[rows++ * 256];
		identity_row(row);
		for (i = 0; i < KP_LOOKUP_PLAYER_COLORS && (size_t)(k * KP_LOOKUP_PLAYER_COLORS + i) < p->tunit_size; i++) {
			row[KP_LOOKUP_PLAYER_FIRST + i] = p->tunit[k * KP_LOOKUP_PLAYER_COLORS + i];
		}
	}
	l->shift_row = rows;
	memcpy(&l->remap[rows++ * 256], ts->shift, 256);
	l->selection_base = rows;
	l->selections = (int32_t)(p->tselect_size / KP_LOOKUP_SELECTION_COLORS);
	if (rows + l->selections + 1 > KP_LOOKUP_MAX_REMAP) {
		return kp_present_fail(err, "world_lookups", "too many remap rows");
	}
	for (k = 0; k < l->selections; k++) {
		uint8_t *row = &l->remap[rows++ * 256];
		identity_row(row);
		for (i = 0; i < KP_LOOKUP_SELECTION_COLORS; i++) {
			row[KP_LOOKUP_SELECTION_FIRST + i] = p->tselect[k * KP_LOOKUP_SELECTION_COLORS + i];
		}
	}
	l->bright = rows;
	{
		uint8_t *row = &l->remap[rows++ * 256];
		for (i = 0; i < 256; i++) {
			int32_t v = 77 * rgb[3 * i] + 151 * rgb[3 * i + 1] + 28 * rgb[3 * i + 2] + 4096;
			row[i] = (uint8_t)(v / 8192);
		}
	}
	l->remap_rows = rows;

	l->tables = (uint8_t *)malloc((size_t)KP_LOOKUP_TABLE_ROWS * 256);
	if (!l->tables) return kp_present_fail(err, "world_lookups", "out of memory");
	n = 0;
	for (k = 0; k < KP_REMAP_TABLES; k++) {
		const KpRemapTable *t = &ts->remap[k];
		if (t->rows > KP_LOOKUP_BANK_ROWS) {
			kp_lookups_free(l);
			return kp_present_fail(err, "world_lookups", "a blend bank is taller than a bank");
		}
		l->bank_base[k] = n;
		memcpy(&l->tables[(size_t)n * 256], t->data, (size_t)t->rows * 256);
		n += t->rows;
		for (i = t->rows; i < KP_LOOKUP_BANK_ROWS; i++) identity_row(&l->tables[(size_t)n++ * 256]);
	}
	l->shadow = n;
	memcpy(&l->tables[(size_t)n++ * 256], ts->dark + KP_LOOKUP_SHADOW_DARK_ROW * 256, 256);
	l->table_rows = n;
	l->glow = (uint8_t *)malloc((size_t)n * 4);
	if (!l->glow) {
		kp_lookups_free(l);
		return kp_present_fail(err, "world_lookups", "out of memory");
	}
	for (k = 0; k < n; k++) fit_table_row(&l->tables[(size_t)k * 256], rgb, &l->glow[(size_t)k * 4]);
	l->outline_color = outline_color;
	l->trans50 = kp_lookups_remap_code(l, KP_DRAW_TRANS50);
	return 0;
}

void kp_lookups_free(KpLookups *l)
{
	free(l->tables);
	free(l->glow);
	l->tables = NULL;
	l->glow = NULL;
}

static int32_t player_of(int32_t owner)
{
	return ((owner % KP_LOOKUP_PLAYER_ROWS) + KP_LOOKUP_PLAYER_ROWS) % KP_LOOKUP_PLAYER_ROWS;
}

int32_t kp_lookups_owner(const KpLookups *l, int32_t owner)
{
	int32_t p = player_of(owner);
	return l->team_row[p] != 0 ? l->team_row[p] : p + 1;
}

int32_t kp_lookups_owner_wrapped(const KpLookups *l, int32_t owner, int32_t wraps)
{
	int32_t p = player_of(owner);
	/* the setter maps the owner (a row 1 + p, as above); each restore maps that row's colour, now as a player, again */
	for (; wraps > 0 && l->team_row[p] != 0; wraps--) p = player_of(l->team_row[p] - 1);
	return kp_lookups_owner(l, p);
}

int kp_lookups_set_player_rows(KpLookups *l, const uint8_t *rows)
{
	int32_t k, changed = 0;
	for (k = 0; k < KP_LOOKUP_PLAYER_ROWS; k++) {
		uint8_t row = rows != NULL && rows[k] < KP_LOOKUP_PLAYER_ROWS ? (uint8_t)(rows[k] + 1) : 0;
		if (l->team_row[k] != row) changed = 1;
		l->team_row[k] = row;
	}
	return changed;
}

/* A blend bank's table row code: 2 * the bank's first row + 1 (method 9 blends by the source). */
int32_t kp_lookups_remap_code(const KpLookups *l, int32_t index)
{
	if (index < 1 || index > KP_REMAP_TABLES) return -1;
	return 2 * l->bank_base[index - 1] + 1;
}

int32_t kp_draw_row_of(const KpLookups *l, int32_t method, int32_t param, int32_t owner, int32_t trans50,
	int32_t warp_frame)
{
	return kp_draw_row_wrapped(l, method, param, owner, 0, trans50, warp_frame);
}

int32_t kp_draw_row_wrapped(const KpLookups *l, int32_t method, int32_t param, int32_t owner, int32_t wraps,
	int32_t trans50, int32_t warp_frame)
{
	int32_t own = kp_lookups_owner_wrapped(l, owner, wraps);
	int32_t colour = trans50 >= 0 ? kp_lookups_owner(l, trans50) : 0;
	int32_t byte = ((param % 256) + 256) % 256;
	switch (method) {
	case KP_DRAW_PLAYER_COLOUR: return kp_lookups_owner(l, byte); /* the colour row of player colour `param` */
	case 13:
		/* the selection wrapper 0x40e900 copies row `param` of 0x594054 (tselect); the original has no other rows */
		if (byte >= l->selections) return -1;
		return l->selection_base + byte;
	case 16: return l->shift_row;
	case 9: return param == KP_DRAW_TRANS50 ? colour : kp_lookups_remap_code(l, param);
	case 6: return colour;
	case 10: return 2 * l->shadow;
	case 17: return 2 * (l->bank_base[0] + byte); /* ofire */
	case 12: return warp_frame;
	case 15: return l->outline_color; /* the global line colour 0x6d237a of its consumer */
	case 3: case 8: return 0;
	case 5: case 7:
		return colour * KP_DRAW_COLOR_SPAN + own * KP_DRAW_PARAM_SPAN + byte;
	case 2: case 4:
		return own * KP_DRAW_PARAM_SPAN + byte;
	default: return own;
	}
}
