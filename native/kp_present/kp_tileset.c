/*
 * The tileset container (tilesets/<name>.kpd, kind TSET; docs/formats/kpd.md) for the presentation: the palette (WPE),
 * the colour cycles (CYCL) and the indices they cycle (the interface's cycled colours), the CV5 groups and megatile
 * flags, the minimap's MMNI and the megatile sheet (SHET) the terrain pass reads; and the native terrain import of a
 * mission's MTXM into megatile ids.
 */
#include "kp_present.h"

#include <stdio.h>
#include <string.h>

#define TSET_SCHEMA 1
#define CV5_RECORD 52

int kp_tileset_load(KpTileset *t, const uint8_t *data, size_t size, const char *name, KpError *err)
{
	KpKpf c;
	KpKpfExpect expect;
	const KpKpfSection *cv5, *mflg, *wpe, *cycl, *mmni, *shet;
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
	rc = 0;
done:
	kp_kpf_free(&c);
	return rc;
}

int kp_terrain_import(const KpMission *m, const KpTileset *t, uint16_t *ids, int64_t *bad_cell)
{
	int64_t i, n = (int64_t)m->width * (int64_t)m->height;
	if (m->tiles.size < (uint32_t)(2 * n)) {
		*bad_cell = 0;
		return -1;
	}
	for (i = 0; i < n; i++) {
		uint32_t raw = kp_le_u16(m->tiles.data + 2 * i), group = (raw >> 4) & 0x7ff, mega;
		if (group >= t->groups) {
			*bad_cell = i;
			return -1;
		}
		mega = kp_le_u16(t->cv5 + group * CV5_RECORD + 20 + (raw & 15) * 2);
		if (raw >= 0x8000) mega |= 0x8000;
		ids[i] = (uint16_t)mega;
	}
	return 0;
}
