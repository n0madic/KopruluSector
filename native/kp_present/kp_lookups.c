/*
 * palettes.kpd (kind PALS; docs/formats/kpd.md) and the lookup rows of the world image materials (ADR 0006): the remap
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

int kp_palettes_load(KpPalettes *p, const uint8_t *data, size_t size, KpError *err)
{
	static const char *const GAME[4] = { "TUNI", "TMIN", "TSEL", "THPB" };
	KpKpf c;
	KpKpfExpect expect;
	int t, k, rc = -1;
	memset(p, 0, sizeof *p);
	if (kp_kpf_parse(&c, data, size, "palettes.kpd", 0, err) != 0) return -1;
	memset(&expect, 0, sizeof expect);
	expect.kind = KP_PALETTES_KIND;
	expect.schema_version = KP_PALETTES_SCHEMA;
	if (kp_kpf_check(&c, &expect, err) != 0) goto done;
	for (t = 0; t < 8; t++) {
		char fourcc[5];
		const KpKpfSection *s;
		size_t pos = 0;
		snprintf(fourcc, sizeof fourcc, "PAL%d", t);
		if (!(s = kp_kpf_require(&c, fourcc, err))) goto done;
		for (k = 0; k < KP_REMAP_TABLES; k++) {
			uint16_t rows;
			if (pos + 2 > s->size) {
				kp_present_fail(err, "palettes", "tileset section truncated");
				goto done;
			}
			rows = (uint16_t)kp_le_u16(s->data + pos);
			if (pos + 2 + (size_t)rows * 256 > s->size) {
				kp_present_fail(err, "palettes", "tileset section truncated");
				goto done;
			}
			p->tilesets[t].remap[k].rows = rows;
			p->tilesets[t].remap[k].data = s->data + pos + 2;
			pos += 2 + (size_t)rows * 256;
		}
		if (pos + KP_DARK_SIZE + 256 > s->size) {
			kp_present_fail(err, "palettes", "tileset section truncated");
			goto done;
		}
		p->tilesets[t].dark = s->data + pos;
		p->tilesets[t].shift = s->data + pos + KP_DARK_SIZE;
	}
	for (k = 0; k < 4; k++) {
		const KpKpfSection *s = kp_kpf_require(&c, GAME[k], err);
		if (!s) goto done;
		switch (k) {
		case 0: p->tunit = s->data; p->tunit_size = s->size; break;
		case 1: p->tminimap = s->data; p->tminimap_size = s->size; break;
		case 2: p->tselect = s->data; p->tselect_size = s->size; break;
		default: p->thpbar = s->data; p->thpbar_size = s->size; break;
		}
	}
	rc = 0;
done:
	kp_kpf_free(&c);
	return rc;
}

static void identity_row(uint8_t *row)
{
	int i;
	for (i = 0; i < 256; i++) row[i] = (uint8_t)i;
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
	l->outline_color = outline_color;
	l->trans50 = kp_lookups_remap_code(l, KP_DRAW_TRANS50);
	return 0;
}

void kp_lookups_free(KpLookups *l)
{
	free(l->tables);
	l->tables = NULL;
}

int32_t kp_lookups_owner(const KpLookups *l, int32_t owner)
{
	(void)l;
	return ((owner % KP_LOOKUP_PLAYER_ROWS) + KP_LOOKUP_PLAYER_ROWS) % KP_LOOKUP_PLAYER_ROWS + 1;
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
	int32_t colour = trans50 >= 0 ? kp_lookups_owner(l, trans50) : 0;
	int32_t byte = ((param % 256) + 256) % 256;
	switch (method) {
	case 14: return kp_lookups_owner(l, byte); /* the colour row of player colour `param` */
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
		return colour * KP_DRAW_COLOR_SPAN + kp_lookups_owner(l, owner) * KP_DRAW_PARAM_SPAN + byte;
	case 2: case 4:
		return kp_lookups_owner(l, owner) * KP_DRAW_PARAM_SPAN + byte;
	default: return kp_lookups_owner(l, owner);
	}
}
