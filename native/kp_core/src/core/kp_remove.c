/* Unit removal and recycling (kp_game.h). */
#include "kp_game_internal.h"

#define SPRITE_SELECTED 0x08
#define SPRITE_HIDDEN 0x20
#define SPRITE_DASHED 0x06
#define DAT_BUILDING 0x01
#define DAT_FLYING_BUILDING 0x20
#define DAT_WORKER 0x08
#define DAT_SUBUNIT 0x10
#define STATUS_COMPLETED 0x1
#define STATUS_GROUNDED_BUILDING 0x2
#define STATUS_REQUIRES_DETECTION 0x100
#define STATUS_CREEP_RETIRE 0x4000

#define STATUS_AIR 0x4

static int revealer(int64_t t) { return t == KP_TYPE_SPELL_SCANNER_SWEEP || t == KP_TYPE_MAP_REVEALER; }
static int hangar_parent(int64_t t)
{
	return t == KP_TYPE_CARRIER || t == KP_TYPE_WARBRINGER || t == KP_TYPE_GANTRITHOR || t == KP_TYPE_REAVER;
}

#define DECLOAK_SOUND 274


void kp_clear_references(KpGame *g, int64_t n, int full)
{
	KpUnits *u = &g->u;
	int64_t m = g->lists[KP_LIST_ACTIVE].head;
	while (m != 0) {
		kp_forget_target(g, m, n);
		m = u->next[m];
	}
	if (!full) return;
	m = g->lists[KP_LIST_HIDDEN].head;
	while (m != 0) {
		kp_forget_target(g, m, n);
		m = u->next[m];
	}
	if (kp_band(KP_DAT(g, units, special_ability_flags, u->type[n]), DAT_WORKER) != 0 && u->union_0xd0[n] != 0
		&& kp_band(u->union_units[n], 16) != 0) {
		kp_harvest_cancel(g, u->union_0xd0[n], n); /* 0x424170 */
	}
}

void kp_remove_from_selections(KpGame *g, int64_t n)
{
	int p;
	for (p = 0; p <= 7; p++) {
		int64_t *row = g->selections[p];
		int count = 0, at = -1, k;
		while (count < KP_SLOTS && row[count] != 0) {
			if (row[count] == n) at = count;
			count++;
		}
		if (at >= 0) {
			for (k = at; k <= count - 2; k++) row[k] = row[k + 1];
			row[count - 1] = 0;
		}
	}
	if (kp_band(g->w->spr.flags[kp_unit_sprite(g, n)], SPRITE_DASHED) != 0) {
		kp_game_fail(g, "remove: dashed selection circles of a team game (0x416cd0)");
	}
}

/* RemoveUnitFromSelection 0x4a7b80: the local selection without unit n; its selection command is the interface's (the
 * replays have it in their command log). */
void kp_leave_local_selection(KpGame *g, int64_t n)
{
	int64_t rest[KP_SLOTS];
	int count = 0, k;
	for (k = 0; k < KP_SLOTS && g->local_selection[k] != 0; k++) {
		if (g->local_selection[k] != n) rest[count++] = g->local_selection[k];
	}
	kp_select_locally(g, rest, count);
	if (count == 0) kp_placement_cancel(g); /* CancelTargetingUi 0x423120 (the targeting cursor is the interface's) */
}

void kp_remove_from_local_selection(KpGame *g, int64_t n)
{
	int32_t s = kp_unit_sprite(g, n);
	int64_t slot, rest[KP_SLOTS];
	int count = 0, k;
	if (kp_band(g->w->spr.flags[s], SPRITE_SELECTED) == 0) return;
	slot = g->w->spr.selection[s];
	for (k = 0; k < KP_SLOTS; k++) {
		int64_t m = g->local_selection[k];
		if (m == 0) break;
		if (k != slot) rest[count++] = m;
	}
	kp_select_locally(g, rest, count);
	if (count == 0) kp_placement_cancel(g); /* CancelTargetingUi 0x423120 (the targeting cursor is the interface's) */
}

void kp_unit_hide(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	KpImages *w = g->w;
	int32_t s = kp_unit_sprite(g, n);
	int64_t sub;
	if (kp_band(w->spr.flags[s], SPRITE_HIDDEN) != 0) return;
	kp_clear_references(g, n, 0);
	kp_clear_bullet_references(g, n);
	kp_remove_from_selections(g, n);
	kp_remove_from_local_selection(g, n);
	kp_unit_deactivate_spatial(g, n);
	kp_reset_movement(g, n);
	kp_list64_unlink(u->prev, u->next, &g->lists[KP_LIST_ACTIVE], n, 0);
	kp_list64_insert_after_head(u->prev, u->next, &g->lists[KP_LIST_HIDDEN], n, 0);
	w->spr.flags[s] = (int32_t)kp_bor(w->spr.flags[s], SPRITE_HIDDEN);
	kp_sprite_set_visibility(w, s, 0);
	sub = u->subunit[n];
	if (sub != 0 && kp_band(KP_DAT(g, units, special_ability_flags, u->type[sub]), DAT_SUBUNIT) != 0) {
		int32_t ss = kp_unit_sprite(g, sub);
		w->spr.flags[ss] = (int32_t)kp_bor(w->spr.flags[ss], SPRITE_HIDDEN);
		kp_sprite_set_visibility(w, ss, 0);
	}
}

static void unlink_owner(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t prev = u->player_prev[n], next = u->player_next[n];
	if (prev != 0) u->player_next[prev] = next;
	if (next != 0) u->player_prev[next] = prev;
	if (g->player_units[u->owner[n]] == n) g->player_units[u->owner[n]] = next;
	u->player_prev[n] = u->player_next[n] = 0;
}

/* RemoveSpecialUnitBody 0x425270: true when handled. */
static int remove_special_body(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t t = u->type[n];
	if (kp_band(KP_DAT(g, units, special_ability_flags, t), DAT_SUBUNIT) != 0) {
		kp_list64_insert_after_head(u->prev, u->next, &g->lists[KP_LIST_PENDING], n, 0);
		unlink_owner(g, n);
		kp_account_live(g, n, -1);
		u->subunit[n] = 0;
		return 1;
	}
	if (kp_band(g->w->spr.flags[kp_unit_sprite(g, n)], SPRITE_HIDDEN) != 0) return 0;
	if (!kp_gas_building(t)) return 0;
	/* a gas building turns back into the geyser of player 11 */
	if (t == KP_TYPE_EXTRACTOR) {
		int32_t s = kp_unit_sprite(g, n);
		kp_creep_clear_area(g, KP_TYPE_EXTRACTOR, kp_s16v(g->w->spr.x[s]), kp_s16v(g->w->spr.y[s]));
	}
	if (kp_band(u->status[n], STATUS_COMPLETED) != 0) kp_account_completed(g, n, -1, 0);
	u->status[n] = kp_band(u->status[n], kp_bnot(STATUS_COMPLETED));
	kp_change_unit_type(g, n, KP_TYPE_GEYSER);
	kp_set_order_no_target(g, n, KP_DAT(g, units, human_ai_idle, KP_TYPE_GEYSER));
	u->hp[n] = KP_DAT(g, units, hit_points, KP_TYPE_GEYSER);
	kp_change_owner(g, n, KP_PLAYER_NEUTRAL, 0);
	kp_set_sprite_owner(g, n, KP_PLAYER_NEUTRAL);
	u->status[n] = kp_bor(u->status[n], STATUS_COMPLETED);
	kp_account_completed(g, n, 1, 1);
	/* the ghost a gas building's placement puts on the new Geyser (AddGhostToNewGeyser 0x47d8e0) is the presentation's */
	return 1;
}

/* RemoveBuildingState 0x4251c0: a grounded building's addon, research, upgrade, queue and creep. */
static void remove_building_state(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t entry;
	if (kp_band(u->status[n], STATUS_GROUNDED_BUILDING) == 0) return;
	if (u->secondary_order[n] == KP_ORD_BUILD_ADDON) kp_cancel_incomplete_build_unit(g, n); /* 0x4355c0 */
	if (u->union_0xc0[n] != 0) kp_detach_addon(g, n);
	kp_refund_research(g, n, 0); /* 0x43adf0 */
	kp_refund_upgrade(g, n, 0);  /* 0x43ae60 */
	entry = *kp_queue_head(g, n);
	if (kp_band(entry, 0xffff) <= KP_TYPE_LAST_UNIT) kp_cancel_all_queued(g, n);
	/* the placement's ghosts it refreshes (RefreshPlacementGhosts 0x47d530) are the presentation's */
	if (kp_unit_race(g, n) == KP_RACE_ZERG) {
		if (kp_is_morphing_zerg_building(g, n)) u->status[n] = kp_bor(u->status[n], STATUS_COMPLETED);
		/* RemoveCreepProvider 0x4b47f0; with the pool full the unit tries again while it is pending (0x4b4a10) */
		if (!kp_creep_remove_provider(g, u->type[n], u->x[n], u->y[n], kp_band(u->status[n], STATUS_COMPLETED) != 0)) {
			u->status[n] = kp_bor(u->status[n], STATUS_CREEP_RETIRE);
		}
	}
}

/* MoveUnitToPendingList 0x426530. */
static void move_to_pending(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int from;
	unlink_owner(g, n);
	if (revealer(u->type[n])) from = KP_LIST_SPECIAL;
	else if (kp_band(g->w->spr.flags[kp_unit_sprite(g, n)], SPRITE_HIDDEN) != 0) from = KP_LIST_HIDDEN;
	else from = KP_LIST_ACTIVE;
	kp_list64_unlink(u->prev, u->next, &g->lists[from], n, 0);
	kp_list64_insert_after_head(u->prev, u->next, &g->lists[KP_LIST_PENDING], n, 0);
}

void kp_remove_body(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t t = u->type[n], flags = KP_DAT(g, units, special_ability_flags, t), child, second;
	int32_t s;
	if (kp_band(flags, DAT_BUILDING) != 0) g->ai.terrain_changed = 1; /* 0x48dd50 */
	if (t == KP_TYPE_INTERCEPTOR || t == KP_TYPE_SCARAB) {
		if (kp_band(u->status[n], STATUS_COMPLETED) != 0) kp_hangar_unlink(g, n);
	} else if (hangar_parent(t)) {
		kp_hangar_release(g, n);
	} else if (t == KP_TYPE_NYDUS_CANAL) {
		int64_t exit = u->union_0xd0[n];
		if (exit != 0) {
			kp_set_union_unit(g, n, KP_UNION_PEER, 0);
			kp_set_union_unit(g, exit, KP_UNION_PEER, 0);
			kp_unit_kill(g, exit);
		}
	} else if (t == KP_TYPE_PYLON) {
		kp_power_remove_provider(g, n);
	} else if (kp_ghost_type(t)) {
		kp_ghost_drop_dot(g, n);
	} else if (kp_nuke_linked_type(t)) {
		kp_nuke_teardown(g, n);
	} else if (kp_mineral_field(t) || (kp_gas_building(t) && kp_band(u->status[n], STATUS_COMPLETED) != 0)) {
		kp_harvest_detach_all(g, n);
	}
	kp_cargo_at_removal(g, n);
	kp_clear_references(g, n, 1);
	kp_clear_bullet_references(g, n);
	kp_remove_unit_ai(g, n, 1);
	if (remove_special_body(g, n)) return;
	kp_stop_at_target(g, n);
	s = kp_unit_sprite(g, n);
	if (kp_band(g->w->spr.flags[s], SPRITE_HIDDEN) == 0) kp_unit_deactivate_spatial(g, n);
	if (kp_band(flags, DAT_BUILDING) == 0) {
		u->is_cloaked[n] = 0;
		kp_cloak_list_remove(g, n);
		if (kp_band(u->status[n], STATUS_REQUIRES_DETECTION) != 0) kp_decloak(g, n, DECLOAK_SOUND);
	}
	remove_building_state(g, n);
	if (kp_band(flags, DAT_FLYING_BUILDING) != 0 && kp_shrv(u->union_0xc8[n], 24) != 0) {
		/* a building dying while it lands lets go of the site it reserved (BuildingLand 71, union +0xcb) */
		kp_building_occupancy_at(g, t, kp_band(u->order_x[n], 0xffff), kp_band(u->order_y[n], 0xffff), 0); /* 0x41a190 */
	}
	kp_path_free(g, n);
	child = u->build_unit[n];
	second = u->secondary_order[n];
	if (child != 0 && (second == KP_ORD_TRAIN || second == KP_ORD_TRAIN_FIGHTER)) kp_unit_kill(g, child);
	kp_set_secondary_order(g, n, KP_ORD_NOTHING);
	if (kp_band(flags, DAT_WORKER) != 0) kp_drop_powerup(g, n);
	kp_remove_from_selections(g, n);
	kp_remove_from_local_selection(g, n);
	kp_sprite_remove_circle(g->w, s);
	kp_account_live(g, n, -1);
	if (kp_band(u->status[n], STATUS_COMPLETED) != 0) kp_account_completed(g, n, -1, 0);
	if (!g->w->replay && u->owner[n] == g->local_player) kp_placement_recheck(g); /* 0x47db60, 0x47d290(1) */
	move_to_pending(g, n);
}

void kp_unit_destroy(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t sub = u->subunit[n];
	if (sub != 0 && kp_band(KP_DAT(g, units, special_ability_flags, u->type[sub]), DAT_SUBUNIT) != 0) {
		kp_unit_destroy(g, sub);
		u->subunit[n] = 0;
	}
	kp_remove_body(g, n);
	kp_sprite_destroy(g->w, kp_unit_sprite(g, n));
	u->sprite[n] = KP_NONE;
}

/* RecyclePendingUnit 0x4256b0. */
static void recycle(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	kp_scalar_set(g, KP_UNITS_ALLOCATED, kp_scalar(g, KP_UNITS_ALLOCATED) - 1);
	kp_order_clear_queue(g, n);
	u->sprite[n] = KP_NONE;
	kp_list64_unlink(u->prev, u->next, &g->lists[KP_LIST_PENDING], n, 0);
	kp_list64_insert_after_head(u->prev, u->next, &g->lists[KP_LIST_FREE], n, 0);
}

void kp_update_pending_units(KpGame *g)
{
	KpUnits *u = &g->u;
	int64_t n = g->lists[KP_LIST_PENDING].head;
	while (n != 0) {
		int64_t next = u->next[n], s;
		int keep;
		kp_set_context(g, n);
		s = u->sprite[n];
		if (s != KP_NONE && kp_band(g->w->spr.flags[kp_check_sprite(g->w, (int32_t)s)], SPRITE_HIDDEN) != 0) {
			kp_sprite_destroy(g->w, (int32_t)s);
			u->sprite[n] = KP_NONE;
		}
		kp_step_unit_sprite(g, n);
		/* a dead creep provider whose creep found no room in the receding pool keeps trying before it is recycled */
		keep = kp_band(u->status[n], STATUS_CREEP_RETIRE) != 0 && !kp_creep_retry_provider(g, n);
		if (u->sprite[n] == KP_NONE) {
			if (!keep) recycle(g, n);
		} else {
			if (kp_scalar(g, KP_UPDATE_TILES) == 1 && kp_controller(g, u->owner[n]) == KP_CTRL_HUMAN) {
				int32_t vs = kp_unit_sprite(g, n);
				kp_reveal_sight(g, kp_s16v(g->w->spr.x[vs]), kp_s16v(g->w->spr.y[vs]), kp_band(u->status[n], STATUS_AIR) != 0,
					kp_shared_vision(g, u->owner[n]), 1);
			}
			kp_unit_update_sprite_visibility(g, n);
		}
		n = next;
	}
}
