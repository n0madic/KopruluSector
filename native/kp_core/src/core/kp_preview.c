/* The chat room's map preview world (kp_preview.h). */
#include "kp_preview.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kp_derive.h"
#include "kp_game.h"
#include "kp_ids.h"
#include "kp_start.h"

#define UNIT_RECORD 36         /* LoadUnitSection 0x468f30 */
#define THG2_RECORD 10         /* LoadDoodadSection 0x468ba0 */
#define THG2_SPRITE 0x1000
#define FIRST_NEUTRAL 8
#define DAT_BUILDING 0x1
#define DAT_SUBUNIT 0x10
#define EXPANSION_ONLY 0x200   /* the StarEdit availability flag kp_create_map_unit tests */
#define TYPE_DARK_SWARM 202    /* the minimap's skipped types (DrawNeutralMinimapUnits 0x46e5b0) */
#define TYPE_FIRST_DOOR 203
#define TYPE_LAST_TRAP 213
#define TYPE_DISRUPTION_WEB 105

static uint16_t u16(const uint8_t *b) { return (uint16_t)(b[0] | b[1] << 8); }

int kp_preview_tileset(const KpChk *chk)
{
	const KpChkSection *dim = kp_chk_section(chk, "DIM "), *era = kp_chk_section(chk, "ERA ");
	if (dim == NULL || dim->size < 4 || era == NULL || era->size < 2) return -1;
	return u16(era->data) & 7;
}

typedef struct {
	const KpDatColumn *placement, *flags, *availability;
	const uint8_t *template_record;
	int expansion;
} Types;

static uint32_t value(const KpDatColumn *c, uint32_t i) { return kp_dat_has(c, i) ? kp_dat_value(c, i) : 0; }

/* A created unit the preview's minimap draws: a type units.dat has, created (CreateMapUnit's expansion test), a
 * neutral player's, and none of the minimap's skipped types. */
static int shown(const Types *t, int64_t type, int64_t owner)
{
	uint32_t flags;
	if (owner < FIRST_NEUTRAL || owner > KP_PLAYER_NEUTRAL || type < 0 || !kp_dat_has(t->flags, (uint32_t)type)) return 0;
	if ((value(t->availability, (uint32_t)type) & EXPANSION_ONLY) != 0 && !t->expansion) return 0;
	flags = value(t->flags, (uint32_t)type);
	if ((flags & DAT_SUBUNIT) != 0) return 0;
	return !((type >= TYPE_FIRST_DOOR && type <= TYPE_LAST_TRAP) || type == TYPE_DARK_SWARM || type == TYPE_DISRUPTION_WEB);
}

static void add(KpPreview *p, const Types *t, int64_t type, uint16_t x, uint16_t y, int64_t owner)
{
	KpPreviewUnit *u = &p->units[p->unit_count++];
	u->x = x;
	u->y = y;
	u->w = (uint8_t)value(t->placement, (uint32_t)(type * 2));
	u->h = (uint8_t)value(t->placement, (uint32_t)(type * 2 + 1));
	u->building = (value(t->flags, (uint32_t)type) & DAT_BUILDING) != 0;
	u->resource = kp_resource_type(type) != 0;
	u->owner = (uint8_t)owner;
}

/* The units in creation order (THG2 then UNIT, as the melee load's table runs them) */
static void created(KpPreview *p, const Types *t, const KpChkSection *thg2, const KpChkSection *unit)
{
	size_t at;
	for (at = 0; thg2 != NULL && at + THG2_RECORD <= thg2->size; at += THG2_RECORD) {
		const uint8_t *r = thg2->data + at;
		int64_t type = u16(r), owner = r[6];
		if ((u16(r + 8) & THG2_SPRITE) != 0) continue; /* a lone sprite, no unit */
		if (type >= KP_TYPE_UPPER_LEVEL_DOOR && type <= KP_TYPE_RIGHT_PIT_DOOR) owner = KP_PLAYER_NEUTRAL;
		if (shown(t, type, owner)) add(p, t, type, u16(r + 2), u16(r + 4), owner);
	}
	for (at = 0; unit != NULL && at + UNIT_RECORD <= unit->size; at += UNIT_RECORD) {
		const uint8_t *r = unit->data + at;
		int64_t type = u16(r + 8), owner = r[16];
		if (type == KP_TYPE_START_LOCATION || kp_template_unit_excluded(t->template_record, type, owner)) continue;
		if (shown(t, type, owner)) add(p, t, type, u16(r + 4), u16(r + 6), owner);
	}
}

int kp_preview_build(KpPreview *p, const KpChk *chk, const uint8_t *cv5, size_t cv5_size, const KpDat *dat,
	const uint8_t *template_record, int expansion, char *err, size_t err_size)
{
	const KpChkSection *dim = kp_chk_section(chk, "DIM "), *mtxm = kp_chk_section(chk, "MTXM");
	const KpChkSection *thg2 = kp_chk_section(chk, "THG2"), *unit = kp_chk_section(chk, "UNIT");
	KpPreviewUnit *order;
	Types t;
	size_t cells, most;
	int64_t bad = 0;
	int k, n, owner;
	memset(p, 0, sizeof *p);
	if (dim == NULL || dim->size < 4 || (p->tileset = kp_preview_tileset(chk)) < 0) {
		snprintf(err, err_size, "preview: no DIM or ERA");
		return -1;
	}
	p->width = u16(dim->data);
	p->height = u16(dim->data + 2);
	cells = (size_t)p->width * p->height;
	if (cells == 0 || mtxm == NULL || mtxm->size < 2 * cells) {
		snprintf(err, err_size, "preview: MTXM of %u bytes for %ux%u", mtxm ? (unsigned)mtxm->size : 0u,
			(unsigned)p->width, (unsigned)p->height);
		return -1;
	}
	t.placement = kp_dat_column(dat, "units", "staredit_placement_size");
	t.flags = kp_dat_column(dat, "units", "special_ability_flags");
	t.availability = kp_dat_column(dat, "units", "staredit_availability_flags");
	t.template_record = template_record;
	t.expansion = expansion;
	if (t.placement == NULL || t.flags == NULL || t.availability == NULL) {
		snprintf(err, err_size, "preview: no units.dat placement, flags or availability");
		return -1;
	}
	most = (thg2 ? thg2->size / THG2_RECORD : 0) + (unit ? unit->size / UNIT_RECORD : 0);
	p->megatiles = (uint16_t *)malloc(cells * sizeof *p->megatiles);
	p->units = (KpPreviewUnit *)malloc((most ? most : 1) * sizeof *p->units);
	order = (KpPreviewUnit *)malloc((most ? most : 1) * sizeof *order);
	if (p->megatiles == NULL || p->units == NULL || order == NULL) {
		free(order);
		kp_preview_free(p);
		snprintf(err, err_size, "preview: out of memory");
		return -1;
	}
	if (kp_derive_import(mtxm->data, p->width, p->height, cv5, cv5_size, NULL, 0, p->megatiles, NULL, &bad) != 0) {
		free(order);
		kp_preview_free(p);
		snprintf(err, err_size, "preview: tile %lld selects a missing CV5 group", (long long)bad);
		return -1;
	}
	created(p, &t, thg2, unit);
	/* each player's unit list takes its new units at its head (0x4256f0's walk), the players from 11 down */
	n = 0;
	for (owner = KP_PLAYER_NEUTRAL; owner >= FIRST_NEUTRAL; owner--) {
		for (k = p->unit_count - 1; k >= 0; k--) {
			if (p->units[k].owner == owner) order[n++] = p->units[k];
		}
	}
	free(p->units);
	p->units = order;
	return 0;
}

void kp_preview_free(KpPreview *p)
{
	free(p->megatiles);
	free(p->units);
	memset(p, 0, sizeof *p);
}
