/* What SaveGameFile 0x498d20 leaves in the game it saves (kp_game.h kp_save_game_effects): the writers convert the
 * pools' pointers to indices and back, and some conversions are lossy or go through the spatial index again. The
 * order is the original's: the Pylons unlinked (0x430150) and the selection overlays stripped (0x416da0) before the
 * writers, the units' spatial round trip and their pointer repairs (SaveUnits 0x431400), the bullets' dead
 * references (SaveBullets 0x40a720), the local selection's overlays rebuilt (0x416e20) and the Pylons relinked
 * (0x4301f0), the selection rows' dead units (0x6a5ef0), the AI pools' free records (SaveAiSaveData 0x48ddb0). The
 * wait box's pause (ShowWaitDialog 0x47b030, CloseWaitDialog 0x47b090) is the interface's (kp_ui_saving). */
#include "kp_game_internal.h"

#include <string.h>

#define STATUS_COMPLETED 0x1
#define STATUS_GROUNDED_BUILDING 0x2
#define DAT_SUBUNIT 0x10
#define SPRITE_DASHED 0x06
#define NO_UPGRADE 61
#define UNION_UPGRADE 0xc9    /* a building's upgrade in research (U_UPGRADE) */
#define UNION_NEXT_LEVEL 0xcd /* its level to come (U_NEXT_LEVEL) */
#define FIRST_PATH 1          /* the path record at the pool's offset 0 */

static int completed_pylon(KpGame *g, int64_t n)
{
	return g->u.type[n] == KP_TYPE_PYLON && kp_band(g->u.status[n], STATUS_COMPLETED) != 0;
}

/* UnlinkPylonsForSave 0x430150: every completed Pylon loses its Psi field and leaves the provider list (the power
 * stays clean: no MarkPowerDirty). */
static void unlink_pylons(KpGame *g)
{
	KpUnits *u = &g->u;
	int64_t n;
	for (n = g->lists[KP_LIST_ACTIVE].head; n != 0; n = u->next[n]) {
		int64_t prev, next;
		if (!completed_pylon(g, n)) continue;
		if (u->union_0xd0[n] != 0) kp_sprite_destroy(g->w, (int32_t)(u->union_0xd0[n] - 1));
		u->union_0xd0[n] = 0;
		prev = g->power.prev[n];
		next = u->rally_unit[n];
		if (prev != 0) u->rally_unit[prev] = next;
		if (next != 0) g->power.prev[next] = prev;
		if (g->power_head == n) g->power_head = next;
		u->rally_unit[n] = g->power.prev[n] = 0;
	}
}

/* RelinkPylonsAfterSaveLoad 0x4301f0: each completed Pylon joins the provider list again (Order_InitializePsiProvider
 * 0x42f5b0 with a new Psi field), a rescuable player's with the RescuePassive order. */
static void relink_pylons(KpGame *g)
{
	KpUnits *u = &g->u;
	int64_t n;
	for (n = g->lists[KP_LIST_ACTIVE].head; n != 0; n = u->next[n]) {
		if (!completed_pylon(g, n)) continue;
		kp_order_init_psi_provider(g, n);
		if (kp_controller(g, u->owner[n]) == KP_CTRL_RESCUE) kp_issue_order(g, n, KP_ORD_RESCUE_PASSIVE);
	}
}

/* StripSelectionOverlays 0x416da0: every sprite record's health bar and selection circle go. */
static void strip_overlays(KpGame *g)
{
	KpImages *w = g->w;
	int32_t s;
	for (s = 0; s < KP_SPRITES; s++) {
		if (w->spr.flags[s] & KP_SF_SELECTED) kp_sprite_remove_health_bar(w, s);
		else if (w->spr.flags[s] & KP_SF_CIRCLE) kp_sprite_remove_circle(w, s);
		/* the allies' dashed circles (0x40f570): team games only */
		if (w->spr.flags[s] & SPRITE_DASHED) kp_game_fail(g, "save: a dashed selection circle (0x416da0)");
	}
}

/* RestoreSelectionOverlays 0x416e20: the local selection's circles, a subunit's on its parent (0x422eb0), which takes
 * its slot. */
static void restore_overlays(KpGame *g)
{
	int64_t k;
	for (k = 0; k < KP_SLOTS && g->local_selection[k] != 0; k++) {
		int64_t n = g->local_selection[k];
		if (kp_band(KP_DAT(g, units, special_ability_flags, g->u.type[n]), DAT_SUBUNIT) != 0) {
			kp_add_selection_circle(g, g->u.subunit[n], k);
			g->local_selection[k] = g->u.subunit[n];
		} else {
			kp_add_selection_circle(g, n, k);
		}
	}
}

/* BuildUsedUnitSlotMap 0x431280: the units of the active, pending, hidden and special lists and their DAT subunits. */
static void used_slots(KpGame *g, uint8_t *used)
{
	static const int LISTS[] = { KP_LIST_ACTIVE, KP_LIST_PENDING, KP_LIST_HIDDEN, KP_LIST_SPECIAL };
	KpUnits *u = &g->u;
	size_t k;
	memset(used, 0, KP_UNITS + 1);
	for (k = 0; k < sizeof LISTS / sizeof LISTS[0]; k++) {
		int64_t n;
		for (n = g->lists[LISTS[k]].head; n != 0; n = u->next[n]) {
			int64_t sub = u->subunit[n];
			used[n] = 1;
			if (sub != 0 && kp_band(KP_DAT(g, units, special_ability_flags, u->type[sub]), DAT_SUBUNIT) != 0) used[sub] = 1;
		}
	}
}

/* SaveUnits 0x431400: PackUnitPointers 0x427970 takes every used unit in the spatial index out (highest slot first),
 * UnpackUnitPointers 0x4275e0 puts them back in the same order (their overlap flags updated against the units already
 * back), drops the path at the pool's offset 0 (it packs to null and leaks) and repairs a building's level to come. */
static void round_trip_units(KpGame *g)
{
	KpUnits *u = &g->u;
	uint8_t used[KP_UNITS + 1], removed[KP_UNITS + 1];
	int64_t n;
	used_slots(g, used);
	memset(removed, 0, sizeof removed);
	for (n = KP_UNITS; n >= 1; n--) {
		if (!used[n] || u->bound_left[n] < 0) continue;
		kp_unit_deactivate_spatial(g, n); /* 0x41a210 */
		removed[n] = 1;                   /* the marker bound_top = 1 */
	}
	for (n = KP_UNITS; n >= 1; n--) {
		if (!used[n]) continue;
		if (removed[n]) kp_unit_activate_spatial(g, n); /* 0x41a050 */
		if (u->path[n] == FIRST_PATH) u->path[n] = 0;
		if (kp_band(u->status[n], STATUS_GROUNDED_BUILDING) != 0) {
			int64_t up = kp_union_get(g, n, UNION_UPGRADE, 1);
			if (up != NO_UPGRADE && kp_union_get(g, n, UNION_NEXT_LEVEL, 1) == 0) {
				kp_union_set(g, n, UNION_NEXT_LEVEL, 1, kp_band(kp_upgrade_level(g, u->owner[n], up) + 1, 0xff));
			}
		}
	}
}

/* UnitFromPackedRef 0x428b50 after UnitToPackedRef 0x428bd0: a dead unit becomes none. */
static int64_t live(KpGame *g, int64_t n) { return n != 0 && kp_unit_is_dead(g, n) ? 0 : n; }

/* SaveBullets 0x40a720 (0x409bb0): every bullet record's source and previous target. */
static void bullet_references(KpGame *g)
{
	KpBullets *b = &g->bullets;
	int64_t k;
	for (k = 1; k <= KP_BULLETS; k++) {
		b->source[k] = live(g, b->source[k]);
		b->previous_target[k] = live(g, b->previous_target[k]);
	}
}

/* The eight selection rows 0x6a5ef0 in place (no compaction; the local selection 0x6a6070 is not among them). */
static void selection_rows(KpGame *g)
{
	int p, k;
	for (p = 0; p < KP_SELECTION_PLAYERS; p++) {
		for (k = 0; k < KP_SLOTS; k++) g->selections[p][k] = live(g, g->selections[p][k]);
	}
}

/* SaveAiSaveData 0x48ddb0: each encoder zeroes the payload of its pool's free records (the decoders restore only
 * the links), and the military decoder 0x4a9e40 drops the region record of a record without a unit. */
static void ai_free_records(KpGame *g)
{
	int64_t k;
	{
		KpMilitaryAis *m = &g->military; /* 0x4a9f20 */
		for (k = m->free; k != -1; k = m->next[k]) {
			m->type[k] = m->harass[k] = m->unit[k] = 0;
			m->player[k] = m->region[k] = -1;
		}
		for (k = 0; k < KP_MILITARY_AIS; k++) {
			if (m->unit[k] == 0) m->player[k] = m->region[k] = -1;
		}
	}
	{
		KpGuards *gd = &g->guards; /* 0x4b1b70: the death byte +9 stays */
		for (k = gd->free; k != -1; k = gd->next[k]) {
			gd->kind[k] = gd->unit[k] = gd->type[k] = gd->time[k] = 0;
			gd->x1[k] = gd->y1[k] = gd->x2[k] = gd->y2[k] = 0;
		}
	}
	{
		KpWorkerAis *wk = &g->workers; /* 0x4842b0 */
		for (k = wk->free; k != -1; k = wk->next[k]) {
			wk->type[k] = wk->assignment[k] = wk->reassign[k] = wk->wait[k] = wk->time[k] = wk->unit[k] = 0;
			wk->town[k] = -1;
		}
	}
	{
		KpBuildingAis *bd = &g->buildings; /* 0x484140 */
		for (k = bd->free; k != -1; k = bd->next[k]) {
			bd->type[k] = bd->unit[k] = 0;
			bd->town[k] = -1;
			memset(bd->tags[k], 0, sizeof bd->tags[k]);
			memset(bd->contexts[k], 0, sizeof bd->contexts[k]);
		}
	}
	{
		KpTowns *t = &g->towns; /* 0x484200: bytes +0x18..+0x1cb */
		for (k = t->free; k != -1; k = t->next[k]) {
#define TOWN_ZERO(name) t->name[k] = 0;
			KP_TOWN_DATA_FIELDS(TOWN_ZERO)
#undef TOWN_ZERO
			memset(t->gas[k], 0, sizeof t->gas[k]);
			memset(t->requests[k], 0, sizeof t->requests[k]);
		}
	}
	{
		KpScripts *sc = &g->scripts; /* 0x493530: +8..+0x30 */
		for (k = sc->free; k != -1; k = sc->next[k]) {
			sc->pc[k] = sc->wait[k] = sc->player[k] = 0;
			sc->left[k] = sc->top[k] = sc->right[k] = sc->bottom[k] = sc->x[k] = sc->y[k] = 0;
			sc->town[k] = -1;
			sc->flags[k] = 0;
		}
	}
}

void kp_save_game_effects(KpGame *g)
{
	unlink_pylons(g);
	strip_overlays(g);
	round_trip_units(g);
	bullet_references(g);
	restore_overlays(g);
	relink_pylons(g);
	selection_rows(g);
	ai_free_records(g);
}
