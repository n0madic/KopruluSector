/* Nuclear strikes (kp_game.h), ported from the atlas (P07, P11, P12, P25; docs/research/native-nuke.md): the Silo's
 * NukeTrain 124 (0x4356c0) over the missile it trains, FindReadyNukeSilo 0x436f60, a Ghost's NukePaint 126 (0x4366f0),
 * NukeUnit 127 (0x41eba0), NukeGround 128 (0x41ea40) and NukeTrack 129 (0x436720), the missile's NukeLaunch 125
 * (0x436540) and the command Cancel Nuke 0x2e (0x4724f0). The missile's strike itself is its script's (castspell with
 * weapon 31). A Silo keeps its missile in union +0xd0 and whether it is ready in +0xd4; a Ghost keeps its target dot
 * (a lone thingy, index + 1) in +0xd0 and its missile in +0x80, the missile its Silo, then its Ghost, in +0x80. */
#include "kp_game_internal.h"

#define STATUS_COMPLETED 0x1
#define FLINGY_MOVING 0x2
#define SIGNAL_LAUNCH 0x2
#define SIGNAL_STRIKE 0x1
#define BUTTONS_NUKE_TRACK 237
#define DOT_SPRITE 231
#define SOUND_LAUNCH 84
#define SOUND_WARNING 127 /* + the local race */
#define SOUND_NUKE_GROUND 0xef
#define STRING_NUKE_WARNING 0x417
#define LAUNCH_TIME 90
#define WARNING_TIME 45
#define DESCENT_HEIGHT 320
#define DESCENT_HEADING 128
#define STRIKE_REACH 10
#define AIM_TOLERANCE 1

static int64_t sprite_x(KpGame *g, int64_t n) { return kp_s16v(g->w->spr.x[kp_unit_sprite(g, n)]); }
static int64_t sprite_y(KpGame *g, int64_t n) { return kp_s16v(g->w->spr.y[kp_unit_sprite(g, n)]); }
static int64_t missile_of(KpGame *g, int64_t silo) { return g->u.union_0xd0[silo]; }

static void set_missile(KpGame *g, int64_t silo, int64_t m) { kp_set_union_unit(g, silo, KP_UNION_PEER, m); }

/* CreateNukeInSilo 0x493450 (create_nuke): an empty Silo gets a completed, hidden missile for free, which joins its
 * AI (AssignCompletedUnitAi 0x48d320). */
void kp_silo_create_nuke(KpGame *g, int64_t silo)
{
	KpUnits *u = &g->u;
	int32_t s = kp_unit_sprite(g, silo);
	int64_t m;
	if (u->type[silo] != KP_TYPE_NUCLEAR_SILO || u->union_0xd4[silo] != 0) return;
	m = kp_unit_create(g, KP_TYPE_NUCLEAR_MISSILE, kp_s16v(g->w->spr.x[s]), kp_s16v(g->w->spr.y[s]), u->owner[silo]); /* 0x424900 */
	if (m == 0) return;
	kp_unit_complete_in_place(g, m); /* 0x424da0 */
	kp_unit_hide(g, m); /* 0x422ba0 */
	set_missile(g, silo, m);
	u->union_0xd4[silo] = 1;
	u->connected[m] = silo;
	kp_ai_assign_completed(g, silo, m);
}

/* FindReadyNukeSilo 0x436f60: the first Silo of n's owner with a ready missile (with check_link, one whose missile
 * is free, the requester's or still the Silo's). */
int64_t kp_find_ready_silo(KpGame *g, int64_t n, int check_link, int64_t requester)
{
	KpUnits *u = &g->u;
	int64_t s;
	for (s = g->player_units[u->owner[n]]; s != 0; s = u->player_next[s]) {
		int64_t m, c;
		if (u->type[s] != KP_TYPE_NUCLEAR_SILO || u->union_0xd4[s] == 0) continue;
		if (!check_link) return s;
		m = missile_of(g, s);
		c = u->connected[m];
		if (c == 0 || c == requester || u->type[c] == KP_TYPE_NUCLEAR_SILO) return s;
	}
	return 0;
}

/* OrderNukeTrain 0x4356c0 (order 124): the Silo takes the missile it trains, and is ready when it is done. */
void kp_order_nuke_train(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t m;
	if (u->order_state[n] == 0) {
		m = u->build_unit[n];
		if (m == 0) return;
		set_missile(g, n, m);
		u->connected[m] = n;
		u->order_state[n] = 1;
	} else if (u->order_state[n] == 1) {
		m = missile_of(g, n);
		if (m == 0) {
			u->order_state[n] = 0;
			return;
		}
		if (kp_band(u->status[m], STATUS_COMPLETED) == 0) return;
		u->union_0xd4[n] = 1;
		u->order_state[n] = 0;
		if (u->ai[n] != 0) kp_issue_order(g, n, KP_ORD_COMPUTER_AI);
	}
}

/* OrderNukePaint 0x4366f0 (order 126): NukeUnit on a target unit, else NukeGround at the point. */
void kp_order_nuke_paint(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	if (u->order_unit[n] != 0) kp_issue_order_target(g, n, KP_ORD_NUKE_UNIT, u->order_unit[n]);
	else kp_issue_order_at(g, n, KP_ORD_CAST_NUCLEAR_STRIKE, kp_s16v(u->order_x[n]), kp_s16v(u->order_y[n]));
}

/* Order_NukeGround 0x41ea40 (order 128): the Ghost comes within sight of the target, turns to it and launches the
 * missile of a ready Silo of its owner. */
void kp_order_nuke_ground(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t ox = kp_s16v(u->order_x[n]), oy = kp_s16v(u->order_y[n]), sight, silo, m;
	kp_flingy_set_target_waypoint(g, n, ox, oy); /* 0x40cbd0 */
	sight = kp_unit_sight(g, n, 0) * 32;
	if (u->ai[n] != 0 && kp_within_distance(g, n, sight * 3, ox, oy)) kp_ai_try_cloak(g, n); /* 0x4b0210 */
	if (!kp_within_distance(g, n, sight, ox, oy)) {
		kp_move_to_point_throttled(g, n, ox, oy); /* 0x438b80 */
		return;
	}
	kp_stop_movement(g, n); /* 0x43a490 */
	if (u->target_waypoint_x[n] != u->x[n] || u->target_waypoint_y[n] != u->y[n]) {
		int64_t d = kp_band(u->heading[n], 0xff)
			- kp_direction_to(g, sprite_x(g, n), sprite_y(g, n), u->target_waypoint_x[n], u->target_waypoint_y[n]);
		if (d < -AIM_TOLERANCE || d > AIM_TOLERANCE) return;
	}
	silo = kp_find_ready_silo(g, n, 0, 0);
	if (silo == 0) {
		kp_order_done(g, n);
		return;
	}
	m = missile_of(g, silo);
	set_missile(g, silo, 0);
	u->union_0xd4[silo] = 0;
	kp_play_unit_sound(g, SOUND_NUKE_GROUND, n);
	kp_issue_order_at(g, m, KP_ORD_NUKE_LAUNCH, ox, oy);
	u->connected[m] = n;
	kp_sprite_set_direction(g->w, kp_unit_sprite(g, m), 0); /* 0x415f00 */
	kp_unit_show(g, m);                                        /* 0x422f60 */
	u->connected[n] = m;
	kp_issue_order_at(g, n, KP_ORD_NUKE_TRACK, sprite_x(g, n), sprite_y(g, n));
	kp_set_button_set(g, n, BUTTONS_NUKE_TRACK);
}

/* Order_NukeUnit 0x41eba0 (order 127): NukeGround at the target unit's position, or a stop without one. */
void kp_order_nuke_unit(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t t = u->order_unit[n];
	if (t == 0) {
		kp_stop_movement(g, n);
		kp_order_done(g, n);
		return;
	}
	u->order_x[n] = sprite_x(g, t);
	u->order_y[n] = sprite_y(g, t);
	kp_order_nuke_ground(g, n);
}

/* OrderNukeTrack 0x436720 (order 129, before the disabled check): the Ghost channels with a dot on the target until
 * its missile starts its descent or is lost; then it idles again and the dot goes. */
void kp_order_nuke_track(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	KpImages *w = g->w;
	int64_t m = u->connected[n];
	int32_t s = kp_unit_sprite(g, n), dot;
	if (u->order_state[n] == 0) {
		kp_sprite_play(w, s, KP_ANIM_SPECIAL1, 1);
		dot = kp_lone_create(w, DOT_SPRITE, (int32_t)kp_s16v(u->order_x[m]), (int32_t)kp_s16v(u->order_y[m]),
			(int32_t)u->owner[n]);
		u->union_0xd0[n] = dot == KP_NONE ? 0 : dot + 1;
		if (dot != KP_NONE) {
			w->spr.elevation[w->thg.sprite[dot]] = (w->spr.elevation[s] + 1) & 0xff;
			kp_thingy_visibility(w, dot); /* 0x417e50 */
		}
		u->order_state[n] = 6;
		return;
	}
	if (u->order_state[n] != 6 || (m != 0 && u->order_state[m] != 5)) return;
	if (u->queue_head[n] == KP_NONE) kp_order_append(g, n, KP_DAT(g, units, return_to_idle, u->type[n]), 0, 0, 0, KP_TYPE_NONE, 0);
	kp_order_unlock_done(g, n);
	kp_set_button_set(g, n, u->type[n]);
	kp_sprite_play(w, s, KP_ANIM_WALKING_TO_IDLE, 1);
	kp_ghost_drop_dot(g, n);
	kp_ai_return_to_base(g, n); /* 0x4afd40 */
}

/* The Ghost's target dot plays its death and is let go (NukeTrack, TeardownUnitTypeLinks 0x4253a0). */
void kp_ghost_drop_dot(KpGame *g, int64_t n)
{
	int64_t dot = g->u.union_0xd0[n];
	if (dot == 0) return;
	kp_sprite_play(g->w, g->w->thg.sprite[dot - 1], KP_ANIM_DEATH, 1);
	g->u.union_0xd0[n] = 0;
}

/* OrderNukeLaunch 0x436540 (order 125): the missile rises out of sight with a warning, comes down over the target
 * from 320 pixels above and strikes on its script's signals; it dies without its Silo or Ghost before its descent. */
void kp_order_nuke_launch(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t x, y;
	if (u->order_state[n] < 5 && u->connected[n] == 0) {
		kp_unit_kill(g, n); /* 0x42a4d0 */
		return;
	}
	switch (u->order_state[n]) {
	case 0:
		kp_play_unit_sound(g, SOUND_LAUNCH, n);
		kp_request_move_to_point(g, n, sprite_x(g, n), kp_s16v(KP_DAT(g, units, unit_extents, KP_TYPE_NUCLEAR_MISSILE * 4 + 1)));
		u->order_timer[n] = LAUNCH_TIME;
		u->order_state[n] = 1;
		return;
	case 1:
		if (kp_band(u->order_timer[n], 0xff) > WARNING_TIME && kp_move_arrival(g, n) == 0) return;
		kp_play_unit_sound(g, SOUND_WARNING + g->players.race[g->local_player], 0);
		kp_report_error(g, g->local_player, STRING_NUKE_WARNING);
		u->order_state[n] = 2;
		return;
	case 2:
		if (u->order_timer[n] != 0 && kp_move_arrival(g, n) == 0) return;
		kp_unit_hide(g, n); /* 0x422ba0 */
		u->connected[u->connected[n]] = n;
		kp_stop_movement(g, n);
		u->order_state[n] = 3;
		return;
	case 3:
		if (kp_band(u->movement_flags[n], FLINGY_MOVING) != 0) return;
		kp_sprite_play(g->w, kp_unit_sprite(g, n), KP_ANIM_WARP_IN, 1);
		u->order_state[n] = 4;
		return;
	case 4:
		if (kp_band(u->order_signal[n], SIGNAL_LAUNCH) == 0) return;
		u->order_signal[n] = kp_band(u->order_signal[n], kp_bnot(SIGNAL_LAUNCH));
		y = kp_s16v(u->order_y[n]) - DESCENT_HEIGHT;
		if (y < kp_s16v(KP_DAT(g, units, unit_extents, KP_TYPE_NUCLEAR_MISSILE * 4 + 1))) y = kp_s16v(KP_DAT(g, units, unit_extents, KP_TYPE_NUCLEAR_MISSILE * 4 + 1));
		x = kp_s16v(u->order_x[n]);
		if (x < kp_s16v(KP_DAT(g, units, unit_extents, KP_TYPE_NUCLEAR_MISSILE * 4))) x = kp_s16v(KP_DAT(g, units, unit_extents, KP_TYPE_NUCLEAR_MISSILE * 4));
		kp_unit_relocate(g, n, x, y);
		kp_flingy_set_heading(g, n, DESCENT_HEADING); /* 0x40c1b0 */
		kp_request_move_to_point(g, n, kp_s16v(u->order_x[n]), kp_s16v(u->order_y[n]));
		kp_unit_show(g, n);
		u->order_state[n] = 5;
		return;
	case 5:
		if (!kp_within_distance(g, n, STRIKE_REACH, kp_s16v(u->move_x[n]), kp_s16v(u->move_y[n]))) return;
		u->order_unit[n] = n;
		kp_sprite_play(g->w, kp_unit_sprite(g, n), KP_ANIM_SPECIAL1, 1);
		u->order_state[n] = 6;
		return;
	case 6:
		if (kp_band(u->order_signal[n], SIGNAL_STRIKE) == 0) return;
		u->order_signal[n] = kp_band(u->order_signal[n], kp_bnot(SIGNAL_STRIKE));
		kp_kill_flagged(g, n); /* 0x425700 */
		return;
	}
}

/* ExecCancelNuke 0x4724f0 (command 0x2e): every selected Ghost lets go of its missile (0x435a40). */
void kp_command_cancel_nuke(KpGame *g, int64_t player)
{
	int64_t units[KP_SLOTS];
	int count = kp_selected_units(g, player, units), k;
	for (k = 0; k < count; k++) kp_ghost_release_nuke(g, units[k]);
}

int kp_nuke_linked_type(int64_t t) { return t == KP_TYPE_NUCLEAR_MISSILE || t == KP_TYPE_NUCLEAR_SILO; }

/* The links of TeardownUnitTypeLinks 0x4253a0 for a missile (its Silo forgets it) and a Silo (its missile dies). */
void kp_nuke_teardown(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t c = u->connected[n], m;
	if (u->type[n] == KP_TYPE_NUCLEAR_MISSILE) {
		if (c != 0 && u->type[c] == KP_TYPE_NUCLEAR_SILO) {
			set_missile(g, c, 0);
			u->union_0xd4[c] = 0;
		}
	} else if (u->type[n] == KP_TYPE_NUCLEAR_SILO) {
		m = missile_of(g, n);
		if (m != 0) {
			kp_unit_kill(g, m);
			set_missile(g, n, 0);
		}
	}
}
