/* Unit removal and recycling (kp_game.h; game/units/remove.lua, the specification). */
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
#define STATUS_IN_TRANSPORT 0x40
#define STATUS_REQUIRES_DETECTION 0x100
#define STATUS_CREEP_RETIRE 0x4000
#define RESOURCE_FLAG 0x2000
#define SECONDARY_NOTHING 0x17
#define SECONDARY_TRAIN 0x26
#define SECONDARY_BUILD_ADDON 0x3f

static int revealer(int64_t t) { return t == 0x21 || t == 0x65; }
static int gas_building(int64_t t) { return t == 0x6e || t == 0x95 || t == 0x9d; }
static int linked_type(int64_t t)
{
	return t == 72 || t == 81 || t == 82 || t == 83 || t == 1 || t == 16 || t == 99 || t == 100 || t == 104 || t == 14
		|| t == 73 || t == 85 || t == 108 || t == 134 || t == 156;
}

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
		kp_game_fail(g, "remove: a worker leaves its resource's gatherers (0x424170)");
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
}

void kp_unit_hide(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	KpImages *w = g->w;
	int32_t s = kp_unit_sprite(g, n);
	int64_t sub;
	if (kp_band(w->spr.flags[s], SPRITE_HIDDEN) != 0) return;
	kp_clear_references(g, n, 0);
	if (kp_scalar(g, KP_BULLETS_ACTIVE) == 1) kp_game_fail(g, "remove: bullets aimed at a hidden unit (0x40a5a0)");
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
	if (gas_building(t)) kp_game_fail(g, "remove: a gas building reverts to a geyser (0x425270)");
	return 0;
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
	if (linked_type(t) || kp_band(flags, RESOURCE_FLAG) != 0) {
		kp_game_fail(g, "remove: links of a removed unit of type %lld (0x4253a0)", (long long)t);
	}
	/* 0x425d10: a unit inside a transport leaves it; a transport kills its cargo. */
	if (kp_band(u->status[n], STATUS_IN_TRANSPORT) != 0) kp_game_fail(g, "remove: a unit leaves its transport (0x437320)");
	if (kp_is_transport(g, n)) {
		if (u->loaded0[n] != 0 || u->loaded1[n] != 0 || u->loaded2[n] != 0 || u->loaded3[n] != 0 || u->loaded4[n] != 0
			|| u->loaded5[n] != 0 || u->loaded6[n] != 0 || u->loaded7[n] != 0) {
			kp_game_fail(g, "remove: a transport's cargo dies (0x425d10)");
		}
	}
	kp_clear_references(g, n, 1);
	if (kp_scalar(g, KP_BULLETS_ACTIVE) == 1) kp_game_fail(g, "remove: bullets aimed at a removed unit (0x40a5a0)");
	kp_remove_unit_ai(g, n, 1);
	if (remove_special_body(g, n)) return;
	kp_stop_at_target(g, n);
	s = kp_unit_sprite(g, n);
	if (kp_band(g->w->spr.flags[s], SPRITE_HIDDEN) == 0) kp_unit_deactivate_spatial(g, n);
	if (kp_band(flags, DAT_BUILDING) == 0) {
		u->is_cloaked[n] = 0;
		kp_cloak_list_remove(g, n);
		if (kp_band(u->status[n], STATUS_REQUIRES_DETECTION) != 0) kp_game_fail(g, "remove: decloaking a removed unit (0x41d360)");
	}
	if (kp_band(u->status[n], STATUS_GROUNDED_BUILDING) != 0) kp_game_fail(g, "remove: building teardown (0x4251c0)");
	if (kp_band(flags, DAT_FLYING_BUILDING) != 0 && kp_shrv(u->union_0xc8[n], 24) != 0) {
		kp_game_fail(g, "remove: landing occupancy of a lifted building (0x41a190)");
	}
	kp_path_free(g, n);
	child = u->build_unit[n];
	second = u->secondary_order[n];
	if (child != 0 && (second == SECONDARY_TRAIN || second == SECONDARY_BUILD_ADDON)) {
		kp_game_fail(g, "remove: the child of a training or building unit (0x42a4d0)");
	}
	kp_set_secondary_order(g, n, SECONDARY_NOTHING);
	if (kp_band(flags, DAT_WORKER) != 0 && u->union_0xc0[n] != 0 && kp_band(u->union_units[n], 1) != 0) {
		kp_game_fail(g, "remove: a worker drops what it carries (0x4272c0)");
	}
	kp_remove_from_selections(g, n);
	kp_remove_from_local_selection(g, n);
	kp_sprite_remove_circle(g->w, s);
	kp_account_live(g, n, -1);
	if (kp_band(u->status[n], STATUS_COMPLETED) != 0) kp_account_completed(g, n, -1, 0);
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
		kp_set_context(g, n);
		s = u->sprite[n];
		if (s != KP_NONE && kp_band(g->w->spr.flags[kp_check_sprite(g->w, (int32_t)s)], SPRITE_HIDDEN) != 0) {
			kp_sprite_destroy(g->w, (int32_t)s);
			u->sprite[n] = KP_NONE;
		}
		kp_step_unit_sprite(g, n);
		if (kp_band(u->status[n], STATUS_CREEP_RETIRE) != 0) kp_game_fail(g, "remove: creep retirement of a pending unit (0x4b4a10)");
		if (u->sprite[n] == KP_NONE) recycle(g, n);
		else kp_game_fail(g, "remove: the sprite of a pending unit in view (0x46bbb0, 0x438c20)");
		n = next;
	}
}
