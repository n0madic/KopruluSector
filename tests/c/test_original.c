/* The data built from the original files (ADR 0018, stage D3: each structure's *_from_files over kp_res) equals what the
 * converter's containers give (*_load over generated/data, which the converter makes the same from either install):
 * the installs of KP_TEST_ORIGINAL (1.17) and KP_TEST_SCR (StarCraft: Remastered; its interface from the stage D6
 * rebuilds, compared part by part), skipped without them or the converted data. */
#include "kp_test_app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "kp_files.h"
#include "kp_flow.h"
#include "kp_hud.h"
#include "kp_present.h"
#include "kp_session.h"
#include "kp_ui_data.h"
#include "../../native/kp_core/src/core/kp_aiscript.h"
#include "../../native/kp_core/src/core/kp_campaign.h"
#include "../../native/kp_core/src/core/kp_dat.h"
#include "../../native/kp_core/src/core/kp_grpmeta.h"
#include "../../native/kp_core/src/core/kp_iscript.h"
#include "../../native/kp_core/src/core/kp_mission.h"

#define DATA "generated/data/"

/* The files of an install through kp_res, or a skip without it or the converted file to compare with. */
static void files_of(KpT *T, const char *env, const char *converted, KpFiles *f)
{
	if (!kp_t_has(T, converted)) kp_t_skip(T, "no %s", converted);
	kp_res_files(kp_t_install(T, env), f);
}

static int bytes_eq(const uint8_t *a, size_t an, const uint8_t *b, size_t bn)
{
	return an == bn && (an == 0 || memcmp(a, b, an) == 0);
}

static uint32_t be32(const uint8_t *p)
{
	return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

/* An 8-bit indexed or grey, non-interlaced PNG's indices (the converter's SHET and EDGE), w * h, released at the end. */
static uint8_t *png_indices(KpT *T, const uint8_t *png, size_t size, int32_t *w, int32_t *h)
{
	uint8_t *idat, *raw, *out;
	size_t idat_size = 0, at = 8, stride;
	uLongf raw_size;
	int32_t x, y;
	kp_t_ok(T, size > 33 && memcmp(png + 1, "PNG", 3) == 0 && memcmp(png + 12, "IHDR", 4) == 0, "a PNG");
	*w = (int32_t)be32(png + 16);
	*h = (int32_t)be32(png + 20);
	kp_t_ok(T, png[24] == 8 && (png[25] == 3 || png[25] == 0) && png[28] == 0, "8-bit indices, not interlaced");
	idat = (uint8_t *)malloc(size);
	kp_t_ok(T, idat != NULL, "out of memory");
	kp_t_defer(T, free, idat);
	while (at + 12 <= size) {
		uint32_t n = be32(png + at);
		kp_t_ok(T, n <= size - at - 12, "a chunk past the end");
		if (memcmp(png + at + 4, "IDAT", 4) == 0) {
			memcpy(idat + idat_size, png + at + 8, n);
			idat_size += n;
		}
		at += 12 + (size_t)n;
	}
	stride = (size_t)*w + 1;
	raw_size = (uLongf)(stride * (size_t)*h);
	raw = (uint8_t *)malloc(raw_size ? raw_size : 1);
	out = (uint8_t *)malloc((size_t)*w * (size_t)*h + 1);
	kp_t_ok(T, raw != NULL && out != NULL, "out of memory");
	kp_t_defer(T, free, raw);
	kp_t_defer(T, free, out);
	kp_t_ok(T, uncompress(raw, &raw_size, idat, (uLong)idat_size) == Z_OK && raw_size == stride * (size_t)*h,
		"the image data");
	for (y = 0; y < *h; y++) {
		const uint8_t *line = raw + stride * (size_t)y + 1;
		const uint8_t *up = y ? out + (size_t)*w * (size_t)(y - 1) : NULL;
		uint8_t *o = out + (size_t)*w * (size_t)y;
		for (x = 0; x < *w; x++) {
			int a = x ? o[x - 1] : 0, b = up ? up[x] : 0, c = x && up ? up[x - 1] : 0, p, pa, pb, pc;
			switch (line[-1]) {
			case 0: o[x] = line[x]; break;
			case 1: o[x] = (uint8_t)(line[x] + a); break;
			case 2: o[x] = (uint8_t)(line[x] + b); break;
			case 3: o[x] = (uint8_t)(line[x] + ((a + b) >> 1)); break;
			case 4:
				p = a + b - c;
				pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
				o[x] = (uint8_t)(line[x] + (pa <= pb && pa <= pc ? a : pb <= pc ? b : c));
				break;
			default: kp_t_fail(T, "row %d's filter %d", (int)y, line[-1]);
			}
		}
	}
	return out;
}

/* zeroed memory freed after the cleanups registered later (they run last first) */
static void *zalloc(KpT *T, size_t n)
{
	void *p = calloc(1, n);
	kp_t_ok(T, p != NULL, "out of memory");
	kp_t_defer(T, free, p);
	return p;
}

static int label_eq(KpLabel a, KpLabel b) { return a.size == b.size && memcmp(a.text, b.text, a.size) == 0; }

static void dat_free(void *p) { kp_dat_free((KpDat *)p); }

static void dat_equals(KpT *T, const char *env)
{
	KpFiles f;
	KpDat *a = (KpDat *)zalloc(T, sizeof *a), *b = (KpDat *)zalloc(T, sizeof *b);
	KpError err;
	size_t size;
	const uint8_t *kpd;
	int k;
	files_of(T, env, DATA "dat.kpd", &f);
	kpd = kp_t_read(T, DATA "dat.kpd", &size);
	kp_t_ok(T, kp_dat_load(a, kpd, size, "dat.kpd", NULL, &err) == 0, "%s", err.msg);
	kp_t_defer(T, dat_free, a);
	kp_t_ok(T, kp_dat_from_files(b, &f, &err) == 0, "%s", err.msg);
	kp_t_defer(T, dat_free, b);
	kp_t_eq(T, b->table_count, a->table_count, "tables");
	for (k = 0; k < a->table_count; k++) {
		kp_t_eq_str(T, b->tables[k].name, a->tables[k].name, "table %d", k);
		kp_t_eq(T, b->tables[k].records, a->tables[k].records, "%s's records", a->tables[k].name);
	}
	kp_t_eq(T, b->column_count, a->column_count, "columns");
	for (k = 0; k < a->column_count; k++) {
		const KpDatColumn *x = &a->columns[k], *y = &b->columns[k];
		kp_t_ok(T, strcmp(x->table, y->table) == 0 && strcmp(x->name, y->name) == 0 && x->size == y->size
			&& x->per_id == y->per_id && x->count == y->count && x->first_id == y->first_id
			&& memcmp(x->data, y->data, (size_t)x->size * x->count) == 0, "%s.%s", x->table, x->name);
	}
	for (k = 0; k < KP_DAT_REQUIREMENTS; k++) {
		const KpDatRequirements *x = &a->requirements[k], *y = &b->requirements[k];
		kp_t_ok(T, x->count == y->count && (x->count == 0 || memcmp(x->words, y->words, 2 * (size_t)x->count) == 0),
			"requirements %s", x->name);
	}
}

static void iscript_free(void *p) { kp_iscript_free((KpIScript *)p); }

static void iscript_equals(KpT *T, const char *env)
{
	KpFiles f;
	KpIScript *a = (KpIScript *)zalloc(T, sizeof *a), *b = (KpIScript *)zalloc(T, sizeof *b);
	KpError err;
	size_t size;
	const uint8_t *kpd;
	files_of(T, env, DATA "iscript.kpd", &f);
	kpd = kp_t_read(T, DATA "iscript.kpd", &size);
	kp_t_ok(T, kp_iscript_load(a, kpd, size, "iscript.kpd", NULL, &err) == 0, "%s", err.msg);
	kp_t_defer(T, iscript_free, a);
	kp_t_ok(T, kp_iscript_from_files(b, &f, &err) == 0, "%s", err.msg);
	kp_t_defer(T, iscript_free, b);
	kp_t_ok(T, a->code_size == b->code_size && memcmp(a->code, b->code, a->code_size) == 0, "the code");
	kp_t_eq(T, b->id_count, a->id_count, "ids");
	kp_t_ok(T, memcmp(a->headers, b->headers, a->id_count * sizeof *a->headers) == 0, "the headers");
	kp_t_ok(T, memcmp(a->slots, b->slots, sizeof a->slots) == 0, "the slots");
}

static void ai_free(void *p) { kp_ai_bins_free((KpAiBins *)p); }

static void ai_bins_equal(KpT *T, const char *env)
{
	KpFiles f;
	KpAiBins *a = (KpAiBins *)zalloc(T, sizeof *a), *b = (KpAiBins *)zalloc(T, sizeof *b);
	KpError err;
	size_t size;
	const uint8_t *kpd;
	int k;
	files_of(T, env, DATA "aiscript.kpd", &f);
	kpd = kp_t_read(T, DATA "aiscript.kpd", &size);
	kp_t_ok(T, kp_ai_bins_load(a, kpd, size, "aiscript.kpd", &err) == 0, "%s", err.msg);
	kp_t_defer(T, ai_free, a);
	kp_t_ok(T, kp_ai_bins_from_files(b, &f, &err) == 0, "%s", err.msg);
	kp_t_defer(T, ai_free, b);
	for (k = 0; k < KP_AI_BINS; k++)
		kp_t_ok(T, a->bin_size[k] == b->bin_size[k] && memcmp(a->bin[k], b->bin[k], a->bin_size[k]) == 0, "bin %d", k);
}

static void campaign_free(void *p) { kp_campaign_free((KpCampaign *)p); }

static void campaign_equals(KpT *T, const char *env)
{
	KpFiles f;
	KpCampaign *a = (KpCampaign *)zalloc(T, sizeof *a), *b = (KpCampaign *)zalloc(T, sizeof *b);
	KpError err;
	size_t size;
	const uint8_t *kpd;
	int k, i;
	files_of(T, env, DATA "campaign.kpd", &f);
	kpd = kp_t_read(T, DATA "campaign.kpd", &size);
	kp_t_ok(T, kp_campaign_load(a, kpd, size, "campaign.kpd", &err) == 0, "%s", err.msg);
	kp_t_defer(T, campaign_free, a);
	kp_t_ok(T, kp_campaign_from_files(b, &f, &err) == 0, "%s", err.msg);
	kp_t_defer(T, campaign_free, b);
	kp_t_eq(T, b->table_count, a->table_count, "tables");
	for (k = 0; k < a->table_count; k++) {
		const KpCampaignTable *x = &a->tables[k], *y = &b->tables[k];
		kp_t_ok(T, x->expansion == y->expansion && x->race == y->race && x->count == y->count, "table %d", k);
		for (i = 0; i < x->count; i++)
			kp_t_ok(T, memcmp(&x->rows[i], &y->rows[i], sizeof x->rows[i]) == 0, "table %d row %d", k, i);
	}
	kp_t_eq(T, b->directory_count, a->directory_count, "directories");
	for (k = 0; k < a->directory_count; k++) kp_t_eq_str(T, b->directories[k], a->directories[k], "directory %d", k);
	kp_t_eq(T, b->carried, a->carried, "the cheats carried");
}

static void grpmeta_free(void *p) { kp_grpmeta_free((KpGrpMeta *)p); }

static void grpmeta_equals(KpT *T, const char *env)
{
	KpFiles f;
	KpGrpMeta *a = (KpGrpMeta *)zalloc(T, sizeof *a), *b = (KpGrpMeta *)zalloc(T, sizeof *b);
	KpDat *dat = (KpDat *)zalloc(T, sizeof *dat);
	KpError err;
	size_t size;
	const uint8_t *kpd;
	uint32_t i;
	files_of(T, env, DATA "grpmeta.kpd", &f);
	kpd = kp_t_read(T, DATA "grpmeta.kpd", &size);
	kp_t_ok(T, kp_grpmeta_load(a, kpd, size, "grpmeta.kpd", NULL, &err) == 0, "%s", err.msg);
	kp_t_defer(T, grpmeta_free, a);
	kp_t_ok(T, kp_dat_from_files(dat, &f, &err) == 0, "%s", err.msg);
	kp_t_defer(T, dat_free, dat);
	kp_t_ok(T, kp_grpmeta_from_files(b, &f, dat, &err) == 0, "%s", err.msg);
	kp_t_defer(T, grpmeta_free, b);
	kp_t_eq(T, b->tbl_count, a->tbl_count, "images.tbl");
	kp_t_ok(T, memcmp(a->grp_of_tbl + 1, b->grp_of_tbl + 1, a->tbl_count * sizeof *a->grp_of_tbl) == 0
		&& memcmp(a->lo_of_tbl + 1, b->lo_of_tbl + 1, a->tbl_count * sizeof *a->lo_of_tbl) == 0, "the index tables");
	kp_t_eq(T, b->atlas_count, a->atlas_count, "atlases");
	for (i = 1; i <= a->atlas_count; i++)
		kp_t_ok(T, label_eq(a->atlases[i].name, b->atlases[i].name) && label_eq(a->atlases[i].pack, b->atlases[i].pack)
			&& a->atlases[i].mask == b->atlases[i].mask, "atlas %u", (unsigned)i);
	kp_t_eq(T, b->grp_count, a->grp_count, "GRPs");
	for (i = 1; i <= a->grp_count; i++) {
		const KpGrpEntry *x = &a->grps[i], *y = &b->grps[i];
		kp_t_ok(T, label_eq(x->path, y->path) && label_eq(x->animation, y->animation) && x->frames == y->frames
			&& x->width == y->width && x->height == y->height && x->atlas == y->atlas && x->first_frame == y->first_frame,
			"GRP %u (%.*s)", (unsigned)i, (int)x->path.size, (const char *)x->path.text);
	}
	kp_t_ok(T, a->frms_size == b->frms_size && memcmp(a->frms, b->frms, a->frms_size) == 0, "the frame records");
	kp_t_ok(T, a->opac_size == b->opac_size && memcmp(a->opac, b->opac, a->opac_size) == 0, "the opacity runs");
	kp_t_eq(T, b->lo_count, a->lo_count, "LO files");
	for (i = 1; i <= a->lo_count; i++) {
		const KpLoEntry *x = &a->los[i], *y = &b->los[i];
		kp_t_ok(T, label_eq(x->path, y->path) && x->size == y->size && memcmp(x->data, y->data, x->size) == 0,
			"LO %u", (unsigned)i);
	}
}

static void palettes_free(void *p) { kp_palettes_free((KpPalettes *)p); }

static void palettes_equal(KpT *T, const char *env)
{
	KpFiles f;
	KpPalettes *a = (KpPalettes *)zalloc(T, sizeof *a), *b = (KpPalettes *)zalloc(T, sizeof *b);
	KpError err;
	size_t size;
	const uint8_t *kpd;
	int t, k;
	files_of(T, env, DATA "palettes.kpd", &f);
	kpd = kp_t_read(T, DATA "palettes.kpd", &size);
	kp_t_ok(T, kp_palettes_load(a, kpd, size, &err) == 0, "%s", err.msg);
	kp_t_ok(T, kp_palettes_from_files(b, &f, &err) == 0, "%s", err.msg);
	kp_t_defer(T, palettes_free, b);
	for (t = 0; t < KP_TILESETS; t++) {
		const KpTilesetPalettes *x = &a->tilesets[t], *y = &b->tilesets[t];
		for (k = 0; k < KP_REMAP_TABLES; k++)
			kp_t_ok(T, x->remap[k].rows == y->remap[k].rows
				&& bytes_eq(x->remap[k].data, 256u * x->remap[k].rows, y->remap[k].data, 256u * y->remap[k].rows),
				"%s's remap %d", KP_TILESET_NAMES[t], k);
		kp_t_ok(T, bytes_eq(x->dark, KP_DARK_SIZE, y->dark, KP_DARK_SIZE), "%s's dark rows", KP_TILESET_NAMES[t]);
		kp_t_ok(T, bytes_eq(x->shift, 256, y->shift, 256), "%s's shift row", KP_TILESET_NAMES[t]);
	}
	kp_t_ok(T, bytes_eq(a->tunit, a->tunit_size, b->tunit, b->tunit_size), "tunit");
	kp_t_ok(T, bytes_eq(a->tminimap, a->tminimap_size, b->tminimap, b->tminimap_size), "tminimap");
	kp_t_ok(T, bytes_eq(a->tselect, a->tselect_size, b->tselect, b->tselect_size), "tselect");
	kp_t_ok(T, bytes_eq(a->thpbar, a->thpbar_size, b->thpbar, b->thpbar_size), "thpbar");
}

static void tileset_free(void *p) { kp_tileset_free((KpTileset *)p); }

/* CV5 groups are 52 bytes (kp_tileset.c) */
#define CV5_GROUP 52

static void tileset_equals(KpT *T, const char *env, int index)
{
	KpFiles f;
	KpTileset *a = (KpTileset *)zalloc(T, sizeof *a), *b = (KpTileset *)zalloc(T, sizeof *b);
	KpError err;
	char path[128];
	size_t size;
	const uint8_t *kpd, *pixels;
	int32_t w, h;
	snprintf(path, sizeof path, DATA "tilesets/%s.kpd", KP_TILESET_NAMES[index]);
	files_of(T, env, path, &f);
	kpd = kp_t_read(T, path, &size);
	kp_t_ok(T, kp_tileset_load(a, kpd, size, path, &err) == 0, "%s", err.msg);
	kp_t_ok(T, kp_tileset_from_files(b, &f, index, &err) == 0, "%s", err.msg);
	kp_t_defer(T, tileset_free, b);
	kp_t_ok(T, memcmp(a->rgb, b->rgb, sizeof a->rgb) == 0, "the palette");
	kp_t_ok(T, memcmp(a->cycled, b->cycled, sizeof a->cycled) == 0 && a->cycles.speed == b->cycles.speed
		&& a->cycles.count == b->cycles.count && memcmp(a->cycles.low, b->cycles.low, sizeof a->cycles.low) == 0
		&& memcmp(a->cycles.high, b->cycles.high, sizeof a->cycles.high) == 0, "the cycles");
	kp_t_eq(T, b->groups, a->groups, "CV5 groups");
	kp_t_ok(T, bytes_eq(a->cv5, (size_t)a->groups * CV5_GROUP, b->cv5, (size_t)b->groups * CV5_GROUP), "the CV5");
	kp_t_eq(T, b->megatiles, a->megatiles, "megatiles");
	kp_t_ok(T, bytes_eq(a->mflags, 4u * a->megatiles, b->mflags, 4u * b->megatiles), "the megatile flags");
	kp_t_ok(T, bytes_eq(a->mmni, a->mmni_size, b->mmni, b->mmni_size), "the minimap's MMNI");
	kp_t_ok(T, a->sheet_count == b->sheet_count && a->sheet_columns == b->sheet_columns, "the sheet's megatiles");
	pixels = png_indices(T, a->sheet_png, a->sheet_size, &w, &h);
	kp_t_ok(T, w == b->sheet_w && h == b->sheet_h && bytes_eq(pixels, (size_t)w * (size_t)h, b->sheet,
		(size_t)b->sheet_w * (size_t)b->sheet_h), "the sheet (%dx%d, built %dx%d)", (int)w, (int)h, (int)b->sheet_w,
		(int)b->sheet_h);
	kp_t_eq(T, b->edge_count, a->edge_count, "creep's edge frames");
	if (a->edge_count) {
		pixels = png_indices(T, a->edge_png, a->edge_size, &w, &h);
		kp_t_ok(T, w == 32 * b->edge_count && h == 32 && bytes_eq(pixels, (size_t)w * (size_t)h, b->edges,
			(size_t)w * (size_t)h), "the edges");
	}
}

static void pack_free(void *p) { kp_pack_free((KpPack *)p); }

typedef struct {
	uint8_t **pages;
	int count;
} BuiltPages;

static void pages_free(void *p)
{
	BuiltPages *b = (BuiltPages *)p;
	int k;
	for (k = 0; k < b->count; k++) free(b->pages[k]);
	free(b->pages);
}

/* Each pack the converter wrote (generated/packs/<name>.kpd) built from the install's GRPs (kp_pack_build, ADR 0018
 * stage D5): its atlases, frame records and pages' indices the same. */
static void packs_equal(KpT *T, const char *env)
{
	static const char *const PACKS[] = { "doodads", "effects", "neutral", "protoss", "terran", "zerg" };
	KpFiles f;
	KpGrpMeta *g = (KpGrpMeta *)zalloc(T, sizeof *g);
	KpDat *dat = (KpDat *)zalloc(T, sizeof *dat);
	KpError err;
	int k, checked = 0;
	files_of(T, env, DATA "grpmeta.kpd", &f);
	kp_t_ok(T, kp_dat_from_files(dat, &f, &err) == 0, "%s", err.msg);
	kp_t_defer(T, dat_free, dat);
	kp_t_ok(T, kp_grpmeta_from_files(g, &f, dat, &err) == 0, "%s", err.msg);
	kp_t_defer(T, grpmeta_free, g);
	for (k = 0; k < (int)(sizeof PACKS / sizeof PACKS[0]); k++) {
		KpPack *a = (KpPack *)zalloc(T, sizeof *a), *b = (KpPack *)zalloc(T, sizeof *b);
		BuiltPages *built = (BuiltPages *)zalloc(T, sizeof *built);
		char path[96];
		const uint8_t *kpd;
		size_t size;
		uint32_t i;
		int j;
		snprintf(path, sizeof path, "generated/packs/%s.kpd", PACKS[k]);
		if (!kp_t_has(T, path)) continue;
		kpd = kp_t_read(T, path, &size);
		kp_t_ok(T, kp_pack_load(a, kpd, size, path, &err) == 0, "%s", err.msg);
		kp_t_defer(T, pack_free, a);
		kp_t_ok(T, kp_pack_build(b, &built->pages, g, &f, PACKS[k], &err) == 0, "%s: %s", PACKS[k], err.msg);
		built->count = b->page_count;
		kp_t_defer(T, pages_free, built);
		kp_t_defer(T, pack_free, b);
		kp_t_eq(T, b->atlas_count, a->atlas_count, "%s: atlases", PACKS[k]);
		for (j = 0; j < a->atlas_count; j++)
			kp_t_ok(T, strcmp(a->atlases[j].name, b->atlases[j].name) == 0 && a->atlases[j].first == b->atlases[j].first
				&& a->atlases[j].count == b->atlases[j].count, "%s: atlas %s", PACKS[k], a->atlases[j].name);
		kp_t_eq(T, b->frame_count, a->frame_count, "%s: frames", PACKS[k]);
		for (i = 0; i < a->frame_count; i++)
			kp_t_ok(T, memcmp(a->frames + 10 * (size_t)i, b->frames + 10 * (size_t)i, 10) == 0, "%s: frame %u's record",
				PACKS[k], (unsigned)i);
		kp_t_ok(T, b->page_count == a->page_count && b->page_w == a->page_w && b->page_h == a->page_h,
			"%s: %d pages of %dx%d, built %d of %dx%d", PACKS[k], a->page_count, a->page_w, a->page_h, b->page_count,
			b->page_w, b->page_h);
		for (j = 0; j < a->page_count; j++) {
			int32_t w, h;
			const uint8_t *pixels = png_indices(T, a->pages[j].png, a->pages[j].size, &w, &h);
			kp_t_ok(T, w == b->page_w && h == b->page_h && memcmp(pixels, built->pages[j], (size_t)w * (size_t)h) == 0,
				"%s: page %d", PACKS[k], j);
		}
		checked++;
	}
	if (!checked) kp_t_skip(T, "no generated/packs");
}

static void files_eq(KpT *T, const KpUiFiles *x, const KpUiFiles *y, const char *what)
{
	int k;
	kp_t_eq(T, y->count, x->count, "%s: files", what);
	for (k = 0; k < x->count; k++)
		kp_t_ok(T, bytes_eq((const uint8_t *)x->files[k].path, x->files[k].path_len, (const uint8_t *)y->files[k].path,
			y->files[k].path_len) && bytes_eq(x->files[k].data, x->files[k].size, y->files[k].data, y->files[k].size),
			"%s: %.*s", what, (int)x->files[k].path_len, x->files[k].path);
}

static void part_eq(KpT *T, KpUiBytes x, KpUiBytes y, const char *what)
{
	kp_t_ok(T, bytes_eq(x.data, x.size, y.data, y.size), "%s (%u bytes, built %u)", what, (unsigned)x.size,
		(unsigned)y.size);
}

static void ui_free(void *p) { kp_ui_data_free((KpUiData *)p); }

static void ui_data_equals(KpT *T, const char *env)
{
	static const char *const NAMES[5] = { DATA "ui.kpd", DATA "fonts.kpd", DATA "strings.kpd", DATA "sounds.kpd",
		DATA "portraits.kpd" };
	KpFiles f;
	KpUiData *a = (KpUiData *)zalloc(T, sizeof *a), *b = (KpUiData *)zalloc(T, sizeof *b);
	KpDat *dat = (KpDat *)zalloc(T, sizeof *dat);
	KpError err;
	const uint8_t *kpd[5];
	size_t sizes[5];
	char what[32];
	int k;
	for (k = 0; k < 5; k++)
		if (!kp_t_has(T, NAMES[k])) kp_t_skip(T, "no %s", NAMES[k]);
	files_of(T, env, DATA "dat.kpd", &f);
	for (k = 0; k < 5; k++) kpd[k] = kp_t_read(T, NAMES[k], &sizes[k]);
	kp_t_ok(T, kp_ui_data_from_kpf(a, kpd, sizes, &err) == 0, "%s", err.msg);
	kp_t_defer(T, ui_free, a);
	kp_t_ok(T, kp_dat_from_files(dat, &f, &err) == 0, "%s", err.msg);
	kp_t_defer(T, dat_free, dat);
	kp_t_ok(T, kp_ui_data_from_files(b, &f, dat, a->vector_fonts.files, a->vector_fonts.count, &err) == 0, "%s",
		err.msg);
	kp_t_defer(T, ui_free, b);
	part_eq(T, a->dlgs, b->dlgs, "the dialogs' records");
	part_eq(T, a->pool, b->pool, "the dialogs' texts");
	files_eq(T, &a->grps, &b->grps, "DGRP");
	files_eq(T, &a->pcx, &b->pcx, "PCXS");
	files_eq(T, &a->glue, &b->glue, "GFIL");
	files_eq(T, &a->ghosts, &b->ghosts, "GHST");
	part_eq(T, a->theme_fonts, b->theme_fonts, "TFNT");
	part_eq(T, a->theme_effects, b->theme_effects, "TEFF");
	for (k = 0; k < KP_UI_FNT_FONTS; k++) {
		snprintf(what, sizeof what, "font %d", k);
		part_eq(T, a->fonts[k], b->fonts[k], what);
	}
	part_eq(T, a->font_rows, b->font_rows, "TFGM");
	for (k = 0; k < KP_UI_TBLS; k++) {
		snprintf(what, sizeof what, "TBL %d", k);
		part_eq(T, a->tbl[k], b->tbl[k], what);
	}
	part_eq(T, a->sounds, b->sounds, "SFX");
	part_eq(T, a->sound_packs, b->sound_packs, "PACK");
	part_eq(T, a->music_lengths, b->music_lengths, "MUSL");
	part_eq(T, a->portrait_rows, b->portrait_rows, "ROWS");
	files_eq(T, &a->portraits, &b->portraits, "FILE");
}

/* --- the product's parts over the files (ADR 0018, stage D5) --- */

static void interface_free(void *p) { kp_ui_free((KpUi *)p); }

static int ui_files_eq(const KpUiFile *x, int xn, const KpUiFile *y, int yn)
{
	int k;
	if (xn != yn) return 0;
	for (k = 0; k < xn; k++) {
		if (!bytes_eq((const uint8_t *)x[k].path, x[k].path_len, (const uint8_t *)y[k].path, y[k].path_len)
			|| !bytes_eq(x[k].data, x[k].size, y[k].data, y[k].size)) {
			return 0;
		}
	}
	return 1;
}

/* The interface over the converted containers (kp_interface_new) and the one over an install's files
 * (kp_interface_new_data), released at the end. */
static void interfaces(KpT *T, const char *env, KpUi **out_a, KpUi **out_b)
{
	uint8_t *files[KP_UI_FILES];
	size_t sizes[KP_UI_FILES];
	KpUiData *d = (KpUiData *)zalloc(T, sizeof *d), *kd;
	KpUi *a, *b;
	KpFiles f;
	KpDat *dat;
	KpError err;
	char why[256];
	int k;
	files_of(T, env, DATA "ui.kpd", &f);
	for (k = 0; k < KP_UI_FILES; k++) {
		char path[128];
		snprintf(path, sizeof path, DATA "%s", KP_UI_FILE_NAMES[k]);
		files[k] = kp_t_read(T, path, &sizes[k]);
	}
	a = kp_interface_new(files, sizes, NULL, why, sizeof why);
	kp_t_ok(T, a != NULL, "the containers' interface: %s", why);
	kp_t_defer(T, interface_free, a);
	kd = a->own_data;
	dat = (KpDat *)zalloc(T, sizeof *dat);
	kp_t_ok(T, kp_dat_from_files(dat, &f, &err) == 0, "%s", err.msg);
	kp_t_defer(T, dat_free, dat);
	kp_t_ok(T, kp_ui_data_from_files(d, &f, dat, kd->vector_fonts.files, kd->vector_fonts.count, &err) == 0, "%s",
		err.msg);
	kp_t_defer(T, ui_free, d);
	b = kp_interface_new_data(d, NULL, why, sizeof why);
	kp_t_ok(T, b != NULL, "the files' interface: %s", why);
	kp_t_defer(T, interface_free, b);
	*out_a = a;
	*out_b = b;
}

/* The interface over the files' data holds what the one over the containers does: the dialogs' records, the files, the
 * fonts, the colour rows, the tables, the strings, the sounds' records, the portraits, the cheats. */
static void interface_equals(KpT *T, const char *env)
{
	KpUi *a, *b;
	int k;
	interfaces(T, env, &a, &b);
	kp_t_eq(T, b->bin_count, a->bin_count, "dialogs");
	for (k = 0; k < a->bin_count; k++) {
		const KpUiBin *x = &a->bins[k], *y = &b->bins[k];
		kp_t_ok(T, bytes_eq((const uint8_t *)x->name, x->name_len, (const uint8_t *)y->name, y->name_len)
			&& x->widget_count == y->widget_count && x->smk_count == y->smk_count
			&& memcmp(x->widgets, y->widgets, (size_t)x->widget_count * KP_UI_WIDGET_SIZE) == 0,
			"dialog %d's records", k);
	}
	kp_t_ok(T, bytes_eq(a->pool, a->pool_size, b->pool, b->pool_size), "the dialogs' texts");
	kp_t_ok(T, ui_files_eq(a->grps, a->grp_count, b->grps, b->grp_count), "the dialogs' GRPs");
	kp_t_ok(T, ui_files_eq(a->pcx, a->pcx_count, b->pcx, b->pcx_count), "the PCXs");
	kp_t_ok(T, ui_files_eq(a->glue_files, a->glue_file_count, b->glue_files, b->glue_file_count), "the glue's files");
	kp_t_ok(T, ui_files_eq(a->ghosts, a->ghost_count, b->ghosts, b->ghost_count), "the ghosts");
	for (k = 0; k < KP_UI_FNT_FONTS; k++) {
		kp_t_ok(T, bytes_eq(a->font_files[k], a->font_file_sizes[k], b->font_files[k], b->font_file_sizes[k]), "font %d", k);
	}
	kp_t_ok(T, bytes_eq(a->game_rows, a->game_rows_size, b->game_rows, b->game_rows_size), "tFontGam's rows");
	kp_t_ok(T, memcmp(a->player_colours, b->player_colours, sizeof a->player_colours) == 0, "the players' colours");
	kp_t_eq(T, b->vector_font_count, a->vector_font_count, "the vector fonts");
	for (k = 0; k < KP_UI_TBLS; k++) {
		kp_t_ok(T, bytes_eq(a->tbl[k], a->tbl_size[k], b->tbl[k], b->tbl_size[k]) && a->tbl_wide[k] == b->tbl_wide[k],
			"string table %d", k);
	}
	kp_t_ok(T, a->sound_count == b->sound_count
		&& memcmp(a->sounds, b->sounds, (size_t)a->sound_count * KP_UI_SOUND_RECORD) == 0, "the sounds' records");
	kp_t_ok(T, a->portrait_row_count == b->portrait_row_count && a->portrait_row_count > 0
		&& memcmp(a->portrait_rows, b->portrait_rows, (size_t)a->portrait_row_count * 8) == 0, "the portraits' rows");
	kp_t_ok(T, ui_files_eq(a->portrait_files, a->portrait_file_count, b->portrait_files, b->portrait_file_count),
		"the portraits' movies");
	kp_t_ok(T, a->status_layout_count == b->status_layout_count && a->button_set_count == b->button_set_count
		&& bytes_eq(a->cheats, a->cheats_size, b->cheats, b->cheats_size)
		&& bytes_eq(a->establishing, a->establishing_size, b->establishing, b->establishing_size),
		"the executable's tables");
}

static int ui_paths_eq(const KpUiFile *x, int xn, const KpUiFile *y, int yn)
{
	int k;
	if (xn != yn) return 0;
	for (k = 0; k < xn; k++)
		if (!bytes_eq((const uint8_t *)x[k].path, x[k].path_len, (const uint8_t *)y[k].path, y[k].path_len)) return 0;
	return 1;
}

/* The interface over StarCraft: Remastered's files (stage D6: the dialogs' BINs and the TBLs rebuilt from its XML, the
 * PCXs and GRPs from its DDS, the portraits' SMKs as KPW1 over its WebM) has the 1.17 interface's parts: the same
 * dialogs with the same records' counts, the same files by name, the fonts, the sounds' records and the portraits'
 * rows equal (Remastered keeps those exactly), each portrait movie a KPW1. Its pictures and some of its texts are the
 * rebuilds' approximations (`kp_data_probe --digests` checks those against the converter's byte for byte). */
static void remastered_interface(KpT *T)
{
	KpUi *a, *b;
	int k;
	interfaces(T, "KP_TEST_SCR", &a, &b);
	kp_t_eq(T, b->bin_count, a->bin_count, "dialogs");
	for (k = 0; k < a->bin_count; k++) {
		const KpUiBin *x = &a->bins[k], *y = &b->bins[k];
		kp_t_ok(T, bytes_eq((const uint8_t *)x->name, x->name_len, (const uint8_t *)y->name, y->name_len)
			&& x->widget_count == y->widget_count && x->smk_count == y->smk_count, "dialog %d's records", k);
	}
	kp_t_ok(T, ui_paths_eq(a->grps, a->grp_count, b->grps, b->grp_count), "the dialogs' GRPs");
	kp_t_ok(T, ui_paths_eq(a->pcx, a->pcx_count, b->pcx, b->pcx_count), "the PCXs");
	kp_t_ok(T, ui_paths_eq(a->glue_files, a->glue_file_count, b->glue_files, b->glue_file_count), "the glue's files");
	kp_t_ok(T, ui_paths_eq(a->ghosts, a->ghost_count, b->ghosts, b->ghost_count), "the ghosts");
	for (k = 0; k < KP_UI_FNT_FONTS; k++) {
		kp_t_ok(T, bytes_eq(a->font_files[k], a->font_file_sizes[k], b->font_files[k], b->font_file_sizes[k]), "font %d", k);
	}
	kp_t_ok(T, a->sound_count == b->sound_count
		&& memcmp(a->sounds, b->sounds, (size_t)a->sound_count * KP_UI_SOUND_RECORD) == 0, "the sounds' records");
	kp_t_ok(T, a->portrait_row_count == b->portrait_row_count && a->portrait_row_count > 0
		&& memcmp(a->portrait_rows, b->portrait_rows, (size_t)a->portrait_row_count * 8) == 0, "the portraits' rows");
	kp_t_ok(T, ui_paths_eq(a->portrait_files, a->portrait_file_count, b->portrait_files, b->portrait_file_count),
		"the portraits' movies by name");
	for (k = 0; k < b->portrait_file_count; k++)
		kp_t_ok(T, b->portrait_files[k].size > 4 && memcmp(b->portrait_files[k].data, "KPW1", 4) == 0,
			"portrait movie %d is a KPW1", k);
	kp_t_ok(T, a->status_layout_count == b->status_layout_count && a->button_set_count == b->button_set_count
		&& bytes_eq(a->cheats, a->cheats_size, b->cheats, b->cheats_size)
		&& bytes_eq(a->establishing, a->establishing_size, b->establishing, b->establishing_size),
		"the executable's tables");
}

#define CATALOG_CAP 256 /* more than the campaign's missions */

/* The catalog of the original data (the campaign's mission directories by mapdata index) is the converted index's:
 * each mission's name, saves' name and campaign row, and its directory the campaign's. */
static void catalog_equals(KpT *T, const char *env)
{
	static KpAppMission a[CATALOG_CAP], b[CATALOG_CAP];
	KpCampaign *c = (KpCampaign *)zalloc(T, sizeof *c);
	KpFiles f;
	KpError err;
	size_t size;
	char *index;
	int n, m, k;
	files_of(T, env, "generated/missions/index.txt", &f);
	index = (char *)kp_t_read(T, "generated/missions/index.txt", &size);
	n = kp_flow_missions(a, CATALOG_CAP, index, size, NULL, 0);
	kp_t_ok(T, kp_campaign_from_files(c, &f, &err) == 0, "%s", err.msg);
	kp_t_defer(T, campaign_free, c);
	m = kp_flow_missions_campaign(b, CATALOG_CAP, c, NULL, 0);
	kp_t_eq(T, m, n, "the missions");
	kp_t_eq(T, n, 64, "the campaign's missions");
	for (k = 0; k < n && k < m; k++) {
		kp_t_ok(T, strcmp(a[k].name, b[k].name) == 0 && strcmp(a[k].kpm, b[k].kpm) == 0 && a[k].index == b[k].index
			&& b[k].catalog && strcmp(b[k].directory, c->directories[b[k].index]) == 0, "mission %d (%s, %s)", k,
			a[k].name, b[k].name);
		kp_t_ok(T, a[k].directory[0] == 0, "the index's mission %d has no directory", k);
	}
}

static void session_free(void *p)
{
	kp_session_free((KpSession *)p);
	free(p);
}

static void shared_free(void *p)
{
	kp_host_shared_free((KpHostShared *)p);
	free(p);
}

#define GAME_STEPS 480 /* 20 s at the fastest speed's 24 a second: the AI's first builds, the triggers' first actions */

/* terran01 from the files (its scenario, the data set's DAT copied, the tileset's CV5 and VF4) plays as it does from the
 * converted data (.kpm, dat.kpd, the tileset's container): the same state, category by category, at the start and after
 * GAME_STEPS steps. */
static void game_equals(KpT *T, const char *env, const char *name, const char *directory)
{
	char kpm[160], tileset[128];
	KpFiles f;
	KpError err;
	KpDat *dat = (KpDat *)zalloc(T, sizeof *dat);
	KpHostShared *sh = (KpHostShared *)calloc(1, sizeof *sh);
	KpSession *s[2];
	KpSessionStart start;
	KpStateCrcs crcs[2][2];
	KpMission m;
	uint8_t *mission, *tileset_bytes;
	size_t mission_size, tileset_size;
	const uint8_t *dat_kpd;
	size_t dat_size;
	int k, j, found;
	snprintf(kpm, sizeof kpm, "generated/missions/%s.kpm", name);
	files_of(T, env, kpm, &f);
	kp_t_ok(T, sh != NULL, "out of memory");
	kp_t_defer(T, shared_free, sh); /* after the sessions (the cleanups run in reverse) */
	kp_t_ok(T, kp_dat_from_files(dat, &f, &err) == 0, "%s", err.msg);
	kp_t_defer(T, dat_free, dat);
	kp_t_ok(T, kp_host_shared_from_files(sh, &f, dat, &err) == 0, "%s", err.msg);
	for (k = 0; k < 2; k++) {
		s[k] = (KpSession *)calloc(1, sizeof *s[k]);
		kp_t_ok(T, s[k] != NULL, "out of memory");
		kp_t_defer(T, session_free, s[k]);
	}
	memset(&m, 0, sizeof m);
	kp_t_ok(T, kp_mission_from_files(&m, &f, directory, &err) == 0, "%s", err.msg);
	kp_t_eq(T, kp_session_load_from(s[0], sh, dat, &m, kpm, &f), 0, "the files' game: %s", s[0]->error);
	dat_kpd = kp_t_read(T, DATA "dat.kpd", &dat_size);
	if (!(mission = kp_read_file(kpm, &mission_size))) kp_t_fail(T, "no %s", kpm);
	kp_t_eq(T, kp_session_load(s[1], kp_t_shared(T), dat_kpd, dat_size, mission, mission_size, kpm), 0,
		"the converted game: %s", s[1]->error);
	kp_session_start_defaults(&start);
	start.seed = 1;
	start.mission_index = kp_oracle_mission_index(".", kpm);
	kp_t_eq(T, kp_session_start(s[0], NULL, 0, &start), 0, "the files' game starts: %s", s[0]->error);
	kp_host_tileset_path(&s[1]->host, tileset, sizeof tileset);
	if (!(tileset_bytes = kp_read_file(tileset, &tileset_size))) kp_t_fail(T, "no %s", tileset);
	kp_t_eq(T, kp_session_start(s[1], tileset_bytes, tileset_size, &start), 0, "the converted game starts: %s",
		s[1]->error);
	for (k = 0; k < 2; k++) {
		kp_t_guard(T, s[k]->g);
		kp_t_ok(T, kp_state_crcs(s[k]->g, 0, &crcs[k][0]) == 0, "state CRCs");
		for (j = 0; j < GAME_STEPS; j++) kp_t_ok(T, kp_session_step(s[k], NULL), "step %d: %s", j, s[k]->error);
		kp_t_ok(T, kp_state_crcs(s[k]->g, 0, &crcs[k][1]) == 0, "state CRCs");
	}
	for (j = 0; j < 2; j++) {
		kp_t_eq(T, crcs[0][j].count, crcs[1][j].count, "categories");
		for (k = 0; k < crcs[1][j].count; k++) {
			kp_t_eq(T, kp_state_crc(&crcs[0][j], crcs[1][j].names[k], &found), crcs[1][j].crcs[k], "%s: %s",
				j ? "after the steps" : "at the start", crcs[1][j].names[k]);
		}
	}
}

/* Each trigger WAV a mission finds (WAVS: an index) is the file its string names in the scenario's directory, which
 * the data has; the ones it does not find name a file the data lacks. */
static void mission_sounds_found(KpT *T, const char *env, const char *name, const char *directory)
{
	char kpm[160], path[260];
	KpFiles f;
	KpError err;
	KpMission m;
	KpRes *res;
	int k, found = 0;
	snprintf(kpm, sizeof kpm, "generated/missions/%s.kpm", name);
	files_of(T, env, kpm, &f);
	res = kp_t_install(T, env);
	memset(&m, 0, sizeof m);
	kp_t_ok(T, kp_mission_from_files(&m, &f, directory, &err) == 0, "%s", err.msg);
	for (k = 0; k < m.sound_count; k++) {
		const KpMissionSound *s = &m.sounds[k];
		int has = kp_mission_sound_path(&m, directory, s->string_id, path, sizeof path) == 1;
		kp_t_ok(T, has || s->index == KP_MISSION_NO_SOUND, "WAV string %u names no file", (unsigned)s->string_id);
		if (has) kp_t_eq(T, kp_res_exists(res, path), s->index != KP_MISSION_NO_SOUND, "%s", path);
		found += s->index != KP_MISSION_NO_SOUND;
	}
	kp_mission_free(&m);
	kp_t_ok(T, found > 0, "%s plays WAVs", name);
}

static void original_interface(KpT *T) { interface_equals(T, "KP_TEST_ORIGINAL"); }
static void original_catalog(KpT *T) { catalog_equals(T, "KP_TEST_ORIGINAL"); }
static void original_game(KpT *T) { game_equals(T, "KP_TEST_ORIGINAL", "terran/terran01", "campaign\\terran\\terran01"); }
static void original_mission_sounds(KpT *T)
{
	mission_sounds_found(T, "KP_TEST_ORIGINAL", "terran/terran01", "campaign\\terran\\terran01");
}
static void remastered_catalog(KpT *T) { catalog_equals(T, "KP_TEST_SCR"); }
static void remastered_game(KpT *T) { game_equals(T, "KP_TEST_SCR", "zerg/zerg02", "campaign\\zerg\\zerg02"); }
static void remastered_mission_sounds(KpT *T)
{
	mission_sounds_found(T, "KP_TEST_SCR", "zerg/zerg02", "campaign\\zerg\\zerg02");
}

static void original_palettes(KpT *T) { palettes_equal(T, "KP_TEST_ORIGINAL"); }
static void original_ui_data(KpT *T) { ui_data_equals(T, "KP_TEST_ORIGINAL"); }
static void original_packs(KpT *T) { packs_equal(T, "KP_TEST_ORIGINAL"); }
static void original_tilesets(KpT *T)
{
	int k;
	for (k = 0; k < KP_TILESETS; k++) tileset_equals(T, "KP_TEST_ORIGINAL", k);
}

static void remastered_grpmeta(KpT *T) { grpmeta_equals(T, "KP_TEST_SCR"); }
static void remastered_packs(KpT *T) { packs_equal(T, "KP_TEST_SCR"); }
static void remastered_palettes(KpT *T) { palettes_equal(T, "KP_TEST_SCR"); }
static void remastered_tilesets(KpT *T)
{
	int k;
	for (k = 0; k < KP_TILESETS; k++) tileset_equals(T, "KP_TEST_SCR", k);
}
static void original_dat(KpT *T) { dat_equals(T, "KP_TEST_ORIGINAL"); }
static void original_iscript(KpT *T) { iscript_equals(T, "KP_TEST_ORIGINAL"); }
static void original_ai_bins(KpT *T) { ai_bins_equal(T, "KP_TEST_ORIGINAL"); }
static void original_campaign(KpT *T) { campaign_equals(T, "KP_TEST_ORIGINAL"); }
static void original_grpmeta(KpT *T) { grpmeta_equals(T, "KP_TEST_ORIGINAL"); }
static void remastered_dat(KpT *T) { dat_equals(T, "KP_TEST_SCR"); }
static void remastered_iscript(KpT *T) { iscript_equals(T, "KP_TEST_SCR"); }
static void remastered_ai_bins(KpT *T) { ai_bins_equal(T, "KP_TEST_SCR"); }
static void remastered_campaign(KpT *T) { campaign_equals(T, "KP_TEST_SCR"); }

static const KpTest TESTS[] = {
	{ "the_1_17_dat_equals_the_converted", original_dat, 0 },
	{ "the_1_17_iscript_equals_the_converted", original_iscript, 0 },
	{ "the_1_17_ai_scripts_equal_the_converted", original_ai_bins, 0 },
	{ "the_1_17_campaign_equals_the_converted", original_campaign, 0 },
	{ "the_1_17_grpmeta_equals_the_converted", original_grpmeta, 0 },
	{ "the_1_17_palettes_equal_the_converted", original_palettes, 0 },
	{ "the_1_17_tilesets_equal_the_converted", original_tilesets, 0 },
	{ "the_1_17_ui_data_equals_the_converted", original_ui_data, 0 },
	{ "the_1_17_packs_equal_the_converted", original_packs, 0 },
	{ "the_1_17_interface_equals_the_converted", original_interface, 0 },
	{ "the_1_17_catalog_equals_the_converted", original_catalog, 0 },
	{ "the_1_17_game_plays_as_the_converted", original_game, 0 },
	{ "the_1_17_mission_wavs_are_found", original_mission_sounds, 0 },
	{ "the_remastered_dat_equals_the_converted", remastered_dat, 0 },
	{ "the_remastered_iscript_equals_the_converted", remastered_iscript, 0 },
	{ "the_remastered_ai_scripts_equal_the_converted", remastered_ai_bins, 0 },
	{ "the_remastered_campaign_equals_the_converted", remastered_campaign, 0 },
	{ "the_remastered_grpmeta_equals_the_converted", remastered_grpmeta, 0 },
	{ "the_remastered_palettes_equal_the_converted", remastered_palettes, 0 },
	{ "the_remastered_packs_equal_the_converted", remastered_packs, 0 },
	{ "the_remastered_tilesets_equal_the_converted", remastered_tilesets, 0 },
	{ "the_remastered_catalog_equals_the_converted", remastered_catalog, 0 },
	{ "the_remastered_game_plays_as_the_converted", remastered_game, 0 },
	{ "the_remastered_mission_wavs_are_found", remastered_mission_sounds, 0 },
	{ "the_remastered_interface_has_the_1_17_parts", remastered_interface, 0 },
};
KP_SUITE(kp_suite_original, "test_original", TESTS);
