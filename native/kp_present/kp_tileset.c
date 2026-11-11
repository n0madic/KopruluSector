/*
 * The tileset container (tilesets/<name>.kpd, kind TSET; docs/formats/kpd.md) for the presentation: the palette (WPE),
 * the colour cycles (CYCL) and the indices they cycle (the interface's cycled colours), the CV5 groups and megatile
 * flags, the minimap's MMNI, the megatile sheet (SHET) the terrain pass reads and creep's edge frames (EDGE); and the
 * native terrain import of a mission's MTXM into megatile ids.
 */
#include "kp_present.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kp_derive.h"
#include "kp_grp.h"
#include "kp_terrain.h"

#define TSET_SCHEMA 1
#define CV5_RECORD KP_DERIVE_CV5_GROUP

int kp_tileset_load(KpTileset *t, const uint8_t *data, size_t size, const char *name, KpError *err)
{
	KpKpf c;
	KpKpfExpect expect;
	const KpKpfSection *cv5, *mflg, *wpe, *cycl, *mmni, *shet, *edge;
	int k, i, rc = -1;
	memset(t, 0, sizeof *t);
	if (kp_kpf_parse(&c, data, size, name, 0, err) != 0) return -1;
	memset(&expect, 0, sizeof expect);
	expect.kind = KP_TILESET_KIND;
	expect.schema_version = TSET_SCHEMA;
	if (kp_kpf_check(&c, &expect, err) != 0) goto done;
	if (!(cv5 = kp_kpf_require(&c, "CV5 ", err)) || !(mflg = kp_kpf_require(&c, "MFLG", err))
		|| !(wpe = kp_kpf_require(&c, "WPE ", err)) || !(cycl = kp_kpf_require(&c, "CYCL", err))) {
		goto done;
	}
	if (wpe->size < 1024 || cycl->size < 2 || cycl->size < 2 + 2 * (size_t)cycl->data[0]) {
		snprintf(err->msg, sizeof err->msg, "%s: WPE or CYCL truncated", name);
		goto done;
	}
	for (k = 0; k < 256; k++) memcpy(&t->rgb[3 * k], wpe->data + 4 * k, 3);
	t->cycles.count = cycl->data[0] < KP_PALETTE_RECORDS ? cycl->data[0] : KP_PALETTE_RECORDS;
	t->cycles.speed = cycl->data[1];
	for (k = 0; k < t->cycles.count; k++) {
		t->cycles.low[k] = cycl->data[2 + 2 * k];
		t->cycles.high[k] = cycl->data[3 + 2 * k];
		for (i = t->cycles.low[k]; i <= t->cycles.high[k]; i++) t->cycled[i] = 1;
	}
	t->cv5 = cv5->data;
	t->groups = cv5->size / CV5_RECORD;
	t->mflags = mflg->data;
	t->megatiles = mflg->size / 4;
	if ((mmni = kp_kpf_find(&c, "MMNI")) != NULL) {
		t->mmni = mmni->data;
		t->mmni_size = mmni->size;
	}
	if ((shet = kp_kpf_find(&c, "SHET")) != NULL && shet->size >= 4) {
		t->sheet_count = (uint16_t)kp_le_u16(shet->data);
		t->sheet_columns = (uint16_t)kp_le_u16(shet->data + 2);
		t->sheet_png = shet->data + 4;
		t->sheet_size = shet->size - 4;
	}
	if ((edge = kp_kpf_find(&c, "EDGE")) != NULL && edge->size >= 2) {
		t->edge_count = (uint16_t)kp_le_u16(edge->data);
		t->edge_png = edge->data + 2;
		t->edge_size = edge->size - 2;
	}
	rc = 0;
done:
	kp_kpf_free(&c);
	return rc;
}

/* --- a tileset from the original files (ADR 0018, stage D3) --- */

#define MEGATILE 32
#define MINITILE 8
#define VX4_RECORD 32
#define VR4_RECORD 64
#define VF4_RECORD 32
#define WPE_SIZE 1024
#define SHEET_COLUMNS 128 /* kpd.SHEET_COLUMNS: megatiles per sheet row */
#define MINIMAP_PIXEL (6 * 8 + 7) /* kpd.MINIMAP_PIXEL: VR4 byte 0x37, x 7, y 6 */

static int tileset_fail(KpError *err, const char *name, const char *what)
{
	snprintf(err->msg, sizeof err->msg, "tileset %s: %s", name, what);
	return -1;
}

/* kpd.creep_edges: tileset\<name>.grp's frames, each in a 32x32 cell at its offset, frame k at (k * 32, 0), 0 where it
 * draws nothing; a frame that leaves its cell or draws index 0 refuses the tileset. */
static int edges_from_grp(KpTileset *t, const uint8_t *grp, size_t size, const char *name, KpError *err)
{
	uint32_t frames, width, height, k;
	size_t row;
	if (kp_grp_header(grp, size, &frames, &width, &height) != 0) return tileset_fail(err, name, "its GRP is malformed");
	row = (size_t)frames * MEGATILE;
	if (!(t->owned_edges = (uint8_t *)calloc(row * MEGATILE + 1, 1))) return tileset_fail(err, name, "out of memory");
	for (k = 0; k < frames; k++) {
		KpGrpFrame f;
		uint8_t idx[MEGATILE * MEGATILE], mask[MEGATILE * MEGATILE];
		int x, y;
		kp_grp_frame(grp, k, &f);
		if (f.x + f.w > MEGATILE || f.y + f.h > MEGATILE || kp_grp_decode(grp, size, k, idx, mask, NULL) != 0) {
			snprintf(err->msg, sizeof err->msg, "tileset %s.grp: frame %u leaves its tile or is malformed", name, (unsigned)k);
			return -1;
		}
		for (y = 0; y < f.h; y++)
			for (x = 0; x < f.w; x++) {
				uint8_t v = idx[y * f.w + x];
				if (!mask[y * f.w + x]) continue;
				if (v == 0) {
					snprintf(err->msg, sizeof err->msg, "tileset %s.grp: frame %u draws index 0", name, (unsigned)k);
					return -1;
				}
				t->owned_edges[(size_t)(f.y + y) * row + (size_t)k * MEGATILE + (size_t)(f.x + x)] = v;
			}
	}
	if (frames > 0xffff) return tileset_fail(err, name, "its GRP has more frames than a u16 counts");
	t->edges = t->owned_edges;
	t->edge_count = (uint16_t)frames;
	return 0;
}

/* tileset.Tileset.render_megatile into the sheet: 16 minitiles of 8x8 VR4 indices, a VX4 word's bit 0 flipping one. */
static int sheet_megatile(KpTileset *t, const uint8_t *vx4, const uint8_t *vr4, size_t vr4_size, uint32_t m,
	const char *name, KpError *err)
{
	int cell;
	for (cell = 0; cell < 16; cell++) {
		uint32_t ref = kp_le_u16(vx4 + (size_t)m * VX4_RECORD + 2 * (size_t)cell), mini = ref >> 1, y, x;
		size_t ox = (size_t)(m % SHEET_COLUMNS) * MEGATILE + (size_t)(cell & 3) * MINITILE;
		size_t oy = (size_t)(m / SHEET_COLUMNS) * MEGATILE + (size_t)(cell >> 2) * MINITILE;
		if ((size_t)mini * VR4_RECORD + VR4_RECORD > vr4_size) {
			snprintf(err->msg, sizeof err->msg, "tileset %s: megatile %u references minitile %u out of range", name,
				(unsigned)m, (unsigned)mini);
			return -1;
		}
		for (y = 0; y < MINITILE; y++) {
			const uint8_t *src = vr4 + (size_t)mini * VR4_RECORD + (size_t)y * MINITILE;
			uint8_t *dst = t->owned_sheet + (oy + y) * (size_t)t->sheet_w + ox;
			for (x = 0; x < MINITILE; x++) dst[x] = src[(ref & 1) ? MINITILE - 1 - x : x];
		}
	}
	return 0;
}

int kp_tileset_from_files(KpTileset *t, const KpFiles *files, int index, KpError *err)
{
	static const char *const EXT[6] = { "cv5", "vx4", "vr4", "vf4", "wpe", "grp" };
	enum { CV5, VX4, VR4, VF4, WPE, GRP };
	uint8_t *data[6] = { 0 };
	size_t size[6] = { 0 };
	const char *name;
	char path[64], fourcc[5];
	const KpDefsTable *cycl;
	uint32_t *flags = NULL;
	uint32_t m, g, megas;
	int k, i, rc = -1;
	memset(t, 0, sizeof *t);
	if (index < 0 || index >= KP_TILESETS) {
		snprintf(err->msg, sizeof err->msg, "tileset %d: no such tileset", index);
		return -1;
	}
	name = KP_TILESET_NAMES[index];
	for (k = 0; k < 6; k++) {
		snprintf(path, sizeof path, "tileset\\%s.%s", name, EXT[k]);
		if (!(data[k] = kp_files_need(files, path, &size[k], err))) goto done;
	}
	/* tileset.decode's and build_tileset's checks */
	if (size[CV5] % CV5_RECORD || size[VR4] % VR4_RECORD || size[VX4] % VX4_RECORD || size[VF4] % VF4_RECORD) {
		tileset_fail(err, name, "a CV5, VX4, VR4 or VF4 size is not a multiple of its record");
		goto done;
	}
	if (size[VX4] / VX4_RECORD != size[VF4] / VF4_RECORD) {
		tileset_fail(err, name, "VF4 and VX4 have different record counts");
		goto done;
	}
	megas = (uint32_t)(size[VX4] / VX4_RECORD);
	t->groups = (uint32_t)(size[CV5] / CV5_RECORD);
	for (g = 0; g < t->groups; g++)
		for (k = 0; k < 16; k++) {
			uint32_t mega = kp_le_u16(data[CV5] + (size_t)g * CV5_RECORD + 20 + 2 * (size_t)k);
			if (mega >= megas) {
				snprintf(err->msg, sizeof err->msg, "tileset %s: CV5 references megatile %u of %u", name, (unsigned)mega,
					(unsigned)megas);
				goto done;
			}
		}
	if (size[WPE] != WPE_SIZE) {
		tileset_fail(err, name, "its WPE is not 1024 bytes");
		goto done;
	}
	if (megas > 0xffff) {
		tileset_fail(err, name, "more megatiles than the sheet's u16 count");
		goto done;
	}
	for (k = 0; k < 256; k++) memcpy(&t->rgb[3 * k], data[WPE] + 4 * k, 3);
	snprintf(fourcc, sizeof fourcc, "CYC%d", index);
	if (!(cycl = kp_defs_table(files->profile, fourcc)) || cycl->size < 2 || cycl->size < 2 + 2 * (size_t)cycl->data[0]) {
		snprintf(err->msg, sizeof err->msg, "tileset %s: no colour cycles compiled for profile \"%s\"", name,
			files->profile->id);
		goto done;
	}
	t->cycles.count = cycl->data[0] < KP_PALETTE_RECORDS ? cycl->data[0] : KP_PALETTE_RECORDS;
	t->cycles.speed = cycl->data[1];
	for (k = 0; k < t->cycles.count; k++) {
		t->cycles.low[k] = cycl->data[2 + 2 * k];
		t->cycles.high[k] = cycl->data[3 + 2 * k];
		for (i = t->cycles.low[k]; i <= t->cycles.high[k]; i++) t->cycled[i] = 1;
	}
	t->sheet_w = SHEET_COLUMNS * MEGATILE;
	t->sheet_h = (int32_t)((megas + SHEET_COLUMNS - 1) / SHEET_COLUMNS) * MEGATILE;
	flags = (uint32_t *)malloc(((size_t)megas + 1) * sizeof *flags);
	t->owned_flags = (uint8_t *)malloc((size_t)megas * 4 + 1);
	t->owned_mmni = (uint8_t *)malloc((size_t)megas * 4 + 1);
	t->owned_sheet = (uint8_t *)calloc((size_t)t->sheet_w * (size_t)t->sheet_h + 1, 1);
	if (!flags || !t->owned_flags || !t->owned_mmni || !t->owned_sheet) {
		tileset_fail(err, name, "out of memory");
		goto done;
	}
	kp_terrain_megatile_flags(data[VF4], size[VF4], flags);
	for (m = 0; m < megas; m++) {
		static const int CELLS[4] = { 0, 1, 4, 5 }; /* kpd.minimap_colours: the flip bit ignored */
		t->owned_flags[4 * m] = (uint8_t)flags[m];
		t->owned_flags[4 * m + 1] = (uint8_t)(flags[m] >> 8);
		t->owned_flags[4 * m + 2] = (uint8_t)(flags[m] >> 16);
		t->owned_flags[4 * m + 3] = (uint8_t)(flags[m] >> 24);
		for (k = 0; k < 4; k++) {
			uint32_t mini = (kp_le_u16(data[VX4] + (size_t)m * VX4_RECORD + 2 * (size_t)CELLS[k]) & 0xfffe) >> 1;
			size_t at = (size_t)mini * VR4_RECORD + MINIMAP_PIXEL;
			t->owned_mmni[4 * m + (uint32_t)k] = at < size[VR4] ? data[VR4][at] : 0;
		}
		if (sheet_megatile(t, data[VX4], data[VR4], size[VR4], m, name, err) != 0) goto done;
	}
	t->mflags = t->owned_flags;
	t->megatiles = megas;
	t->mmni = t->owned_mmni;
	t->mmni_size = (size_t)megas * 4;
	t->sheet = t->owned_sheet;
	t->sheet_count = (uint16_t)megas;
	t->sheet_columns = SHEET_COLUMNS;
	if (edges_from_grp(t, data[GRP], size[GRP], name, err) != 0) goto done;
	t->owned_cv5 = data[CV5];
	data[CV5] = NULL;
	t->cv5 = t->owned_cv5;
	rc = 0;
done:
	free(flags);
	for (k = 0; k < 6; k++) free(data[k]);
	if (rc != 0) kp_tileset_free(t);
	return rc;
}

void kp_tileset_free(KpTileset *t)
{
	free(t->owned_cv5);
	free(t->owned_flags);
	free(t->owned_mmni);
	free(t->owned_sheet);
	free(t->owned_edges);
	memset(t, 0, sizeof *t);
}

int kp_terrain_import(const KpMission *m, const KpTileset *t, uint16_t *ids, int64_t *bad_cell)
{
	int64_t n = (int64_t)m->width * (int64_t)m->height;
	if (m->tiles.size < (uint64_t)2 * (uint64_t)n) {
		*bad_cell = 0;
		return -1;
	}
	return kp_derive_import(m->tiles.data, m->width, m->height, t->cv5, (size_t)t->groups * CV5_RECORD, NULL, 0, ids,
		NULL, bad_cell);
}

uint16_t kp_terrain_edge(uint16_t word, int64_t border, int32_t edges)
{
	return (word & 0x8000) && border > 0 && border <= edges ? (uint16_t)border : 0;
}
