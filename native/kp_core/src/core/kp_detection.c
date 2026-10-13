/* Cloaking and detection of units (kp_terrain.h; cloak.cpp). A
 * unit's +0xe4 (visibility) holds the players that detect it (0x80000000 = not yet computed). The rectangle
 * queries of the detection code are in kp_spatial.c. */
#include "kp_game_internal.h"

#define UNDETERMINED 0x80000000LL
#define DETECTION_RADIUS 0x160
#define DETECTOR 0x8000 /* units.dat flag */
#define STATUS_CLOAKED 0x100
#define STATUS_REQUIRES_DETECTION 0x200
#define CLOAK_TIMER 0x1e
#define BUILDING_DETECTION_RANGE 0xe0
#define QUERY_MAX (KP_UNITS + 1)

/* IsDetector 0x431a90. */
int kp_is_detector(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	return kp_band(KP_DAT(g, units, special_ability_flags, u->type[n]), DETECTOR) != 0 && kp_band(u->status[n], 1) != 0
		&& kp_band(u->status[n], 0x400) == 0 && u->lockdown_timer[n] == 0 && u->stasis_timer[n] == 0
		&& u->maelstrom_timer[n] == 0 && u->blind[n] == 0;
}

/* HasStatusEffect 0x434ac0. */
int kp_has_status_effect(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	return u->matrix_damage[n] != 0 || u->lockdown_timer[n] != 0 || u->maelstrom_timer[n] != 0
		|| u->irradiate_timer[n] != 0 || u->ensnare_timer[n] != 0 || u->stasis_timer[n] != 0
		|| u->plague_timer[n] != 0 || u->acid_spores[n] != 0;
}

/* DetectorCallback 0x4318f0: detector d of another player that sees `target`. */
static int64_t detect(KpGame *g, int64_t d, int64_t target, int64_t mask)
{
	KpUnits *u = &g->u;
	int64_t range;
	if (kp_band(u->status[d], 0x40000000) != 0 || !kp_is_detector(g, d) || d == target) return mask;
	if (kp_band(g->w->spr.visibility[kp_unit_sprite(g, target)], (int64_t)1 << u->owner[d]) == 0) return mask;
	if (kp_band(u->status[d], 2) == 0) range = kp_unit_sight(g, d, 0) * 32;
	else range = BUILDING_DETECTION_RANGE;
	if (kp_unit_box_within(g, d, range, target)) {
		mask = kp_bor(kp_bor(kp_bor(mask, u->parasites[d]), kp_shared_vision(g, u->owner[d])), (int64_t)1 << u->owner[d]);
	}
	return mask;
}

/* ComputeDetection 0x4316b0: the players that detect unit n. */
int64_t kp_detection_mask(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t x, y, mask = 0, c, owner, parasites, p, near[QUERY_MAX];
	int32_t s;
	int count, k;
	if (kp_has_status_effect(g, n) || kp_unit_has_visible_cargo(g, n)) return 0xff;
	s = kp_unit_sprite(g, n);
	x = kp_s16v(g->w->spr.x[s]);
	y = kp_s16v(g->w->spr.y[s]);
	count = kp_units_in_rect(g, kp_s16v(x - DETECTION_RADIUS), kp_s16v(y - DETECTION_RADIUS), kp_s16v(x + DETECTION_RADIUS),
		kp_s16v(y + DETECTION_RADIUS), near, QUERY_MAX);
	for (k = 0; k < count; k++) mask = detect(g, near[k], n, mask);
	c = g->lists[KP_LIST_SPECIAL].head;
	while (c != 0) {
		mask = detect(g, c, n, mask);
		c = u->next[c];
	}
	owner = u->owner[n];
	parasites = u->parasites[n];
	mask = kp_bor(kp_bor(kp_bor(mask, (int64_t)1 << owner), kp_shared_vision(g, owner)), parasites);
	for (p = 0; p <= 11; p++) {
		if (kp_band(parasites, (int64_t)1 << p) != 0) mask = kp_bor(mask, kp_shared_vision(g, p));
	}
	return mask;
}

/* UpdateDetection 0x4325a0. */
void kp_update_detection(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	KpImages *w = g->w;
	int64_t mask = kp_detection_mask(g, n), old = u->visibility[n], changed, lost;
	if (old == mask) return;
	if (old == UNDETERMINED) old = kp_band(kp_bnot(mask), 0xff);
	changed = kp_bxor(old, mask);
	u->visibility[n] = mask;
	lost = kp_band(kp_bnot(mask), changed);
	if (lost != 0) kp_clear_lost_targets(g, n, lost);
	if (kp_band(changed, g->local_vision) == 0) return;
	if (kp_band(mask, g->local_vision) == 0) {
		int32_t s = kp_unit_sprite(g, n);
		if (kp_band(w->spr.flags[s], 0x0f) != 0) kp_game_fail(g, "detection: selected sprite losing detection (0x4a7b80/0x416d50)");
		kp_sprite_cloak_undetected(w, s);
		if (u->subunit[n] != 0) kp_sprite_cloak_undetected(w, kp_unit_sprite(g, u->subunit[n]));
	} else {
		kp_sprite_cloak_detected(w, kp_unit_sprite(g, n));
		if (u->subunit[n] != 0) kp_sprite_cloak_detected(w, kp_unit_sprite(g, u->subunit[n]));
	}
}

/* ClearTargetReferences 0x426d30: unit m (and its turret) forgets target n. */
void kp_forget_target(KpGame *g, int64_t m, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t sub, node;
#define FORGET(f) if (u->f[m] == n) u->f[m] = 0;
	FORGET(build_unit) FORGET(order_unit) FORGET(move_unit) FORGET(auto_target) FORGET(connected) FORGET(irradiated_by)
#undef FORGET
#define WORD_IS(f, bit) (kp_band(u->union_units[m], bit) != 0 && u->f[m] == n)
	if (kp_band(KP_DAT(g, units, special_ability_flags, u->type[m]), 8) == 0) {
		if (kp_band(u->status[m], 2) != 0 && WORD_IS(union_0xc0, 1)) {
			u->union_0xc0[m] = 0;
			u->union_units[m] = kp_band(u->union_units[m], kp_bnot(1));
		}
	} else {
		if (WORD_IS(union_0xc8, 4)) {
			u->union_0xc8[m] = 0;
			u->union_units[m] = kp_band(u->union_units[m], kp_bnot(4));
		}
		if (kp_mod(kp_shrv(u->union_0xcc[m], 16), 256) != 0 && WORD_IS(union_0xd0, 16)) {
			u->union_0xcc[m] = kp_band(u->union_0xcc[m], 0xff00ffff);
			u->union_0xd0[m] = 0;
			u->union_units[m] = kp_band(u->union_units[m], kp_bnot(16));
		}
	}
#undef WORD_IS
	if (kp_rally_producer(u->type[m]) && u->rally_unit[m] == n) {
		int32_t s = kp_unit_sprite(g, n);
		u->rally_unit[m] = 0;
		u->rally_x[m] = kp_s16v(g->w->spr.x[s]);
		u->rally_y[m] = kp_s16v(g->w->spr.y[s]);
	}
	sub = u->subunit[m];
	if (sub != 0 && kp_band(KP_DAT(g, units, special_ability_flags, u->type[kp_game_unit(g, sub)]), 0x10) != 0) {
		kp_forget_target(g, sub, n);
	}
	node = u->queue_head[m];
	while (node != -1) {
		int64_t next = g->orders.next[kp_order_node(g, node)];
		if (g->orders.unit[kp_order_node(g, node)] == n) kp_order_remove(g, m, node);
		node = next;
	}
}

/* ClearLostTargets 0x426b80: units of the players that lost sight of n forget it. */
void kp_clear_lost_targets(KpGame *g, int64_t n, int64_t players)
{
	int64_t p;
	for (p = 0; p <= 11; p++) {
		if (kp_band(players, (int64_t)1 << p) != 0) {
			int64_t m = g->player_units[p];
			while (m != 0) {
				kp_forget_target(g, m, n);
				m = g->u.player_next[m];
			}
		}
	}
}

/* BeginCloak 0x41d200 (permanent cloak at completion, the Cloak order): an unburrowed unit plays `sound` at itself
 * (0x446940) first. The local player's interface refresh (0x4a7ed0) is the presentation's. */
void kp_begin_cloak(KpGame *g, int64_t n, int64_t sound)
{
	KpUnits *u = &g->u;
	KpImages *w = g->w;
	if (kp_band(u->status[n], 0x10) == 0) {
		int64_t sub = u->subunit[n];
		kp_play_unit_sound(g, sound, n);
		kp_sprite_begin_cloak(w, kp_unit_sprite(g, n));
		u->visibility[n] = UNDETERMINED;
		u->status[n] = kp_bor(kp_band(u->status[n], kp_bnot(STATUS_REQUIRES_DETECTION)), STATUS_CLOAKED);
		if (sub != 0) {
			kp_sprite_begin_cloak(w, kp_unit_sprite(g, sub));
			u->visibility[sub] = UNDETERMINED;
			u->status[sub] = kp_bor(kp_band(u->status[sub], kp_bnot(STATUS_REQUIRES_DETECTION)), STATUS_CLOAKED);
		}
	} else if (kp_band(u->status[n], STATUS_CLOAKED) == 0) {
		kp_game_fail(g, "detection: cloaking a burrowed unit (0x4340d0)");
	}
	kp_update_detection(g, n);
	u->secondary_timer[n] = CLOAK_TIMER;
}
