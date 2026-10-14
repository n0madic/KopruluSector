/* Siege Mode (kp_game.h), ported from the atlas (P12, P25): the commands 0x26 and 0x25 and the orders Sieging 98 and
 * Unsieging 99 of the Siege Tank and Edmund Duke, whose type changes in place (kp_morph.c). */
#include "kp_game_internal.h"

#define SIEGE_MODE 5 /* the tech */
#define TANK 5
#define TANK_SIEGED 30
#define DUKE 23
#define DUKE_SIEGED 25
#define ORDER_SIEGING 98
#define ORDER_UNSIEGING 99
#define ORDER_WATCH_TARGET 100
#define ORDER_TURRET_FOLLOW 24
#define MOVING 0x2
#define STATUS_GROUNDED_BUILDING 0x2
#define STATUS_NOBRK 0x2000
#define STATUS_NO_COLLIDE 0x200000
#define SIGNAL_DONE 0x1
#define ANIM_SPECIAL1 13
#define ANIM_SPECIAL2 14
#define NO_TYPE 0xe4

/* PlaySubunitAnimation 0x42a1b0: the subunit's animation with the subunit as the current unit. */
static void play_turret(KpGame *g, int64_t n, int64_t anim)
{
	int64_t sub = g->u.subunit[n];
	kp_ctx_unit(g, sub);
	kp_sprite_play(g->w, kp_unit_sprite(g, sub), (int32_t)anim, 1);
	kp_ctx_unit(g, n);
}

/* The turret follows the tank (order 24) and turns to its start direction. */
static void turret_to_start(KpGame *g, int64_t n)
{
	int64_t sub = g->u.subunit[n];
	kp_issue_order_target(g, sub, ORDER_TURRET_FOLLOW, n);
	kp_flingy_set_spawn_frame(g, sub, KP_DAT(g, units, unit_direction, g->u.type[sub]));
}

/* KillUnitOverlappingBuilding 0x435a70: a unit whose extents overlap a grounded building dies (callback 0x4372c0). */
static int dies_on_building(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int32_t s = kp_unit_sprite(g, n);
	int64_t x = g->w->spr.x[s], y = g->w->spr.y[s], t = u->type[n], *found;
	int count, k, hit = 0;
#define EXT(k) KP_DAT(g, units, unit_extents, t * 4 + (k))
	found = kp_query_overlap(g, kp_s16v(x - EXT(0)), kp_s16v(y - EXT(1)), kp_s16v(x + EXT(2)), kp_s16v(y + EXT(3)), &count);
#undef EXT
	for (k = 0; k < count; k++) {
		int64_t c = found[k];
		if (c != n && kp_band(u->status[c], STATUS_GROUNDED_BUILDING) != 0 && kp_band(u->status[c], STATUS_NO_COLLIDE) == 0) {
			kp_unit_kill(g, n);
			hit = 1;
			break;
		}
	}
	kp_query_release(g, found);
	return hit;
}

/* ResumeQueueOrIdle 0x4372f0. */
static void resume(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	if (u->queue_head[n] == KP_NONE) kp_order_append(g, n, KP_DAT(g, units, return_to_idle, u->type[n]), 0, 0, 0, NO_TYPE, 0);
	kp_order_unlock_done(g, n);
}

/* OrderSieging 0x436a80 (order 98). */
void kp_order_sieging(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t sub = u->subunit[n];
	switch (u->order_state[n]) {
	case 0:
		if (u->type[n] != TANK && u->type[n] != DUKE) {
			kp_order_done(g, n);
			return;
		}
		if (kp_band(u->movement_flags[n], MOVING) != 0) kp_stop_and_face(g, n);
		u->order_state[n] = 1;
		/* fall through */
	case 1:
		if (kp_band(u->movement_flags[n], MOVING) != 0 || kp_band(u->movement_flags[sub], MOVING) != 0
			|| kp_band(u->status[sub], STATUS_NOBRK) != 0) {
			return;
		}
		kp_flingy_set_spawn_frame(g, n, KP_DAT(g, units, unit_direction, u->type[n]));
		turret_to_start(g, n);
		play_turret(g, n, ANIM_SPECIAL1);
		/* fall through */
	case 2:
		u->order_state[n] = 2;
		if (dies_on_building(g, n)) return;
		if (!kp_facing_target_waypoint(g, n) || !kp_facing_target_waypoint(g, sub)) return;
		kp_change_unit_type(g, n, u->type[n] == TANK ? TANK_SIEGED : DUKE_SIEGED);
		u->order_state[n] = 3;
		/* fall through */
	case 3:
		if (kp_band(u->order_signal[n], SIGNAL_DONE) == 0) return;
		u->order_signal[n] = kp_band(u->order_signal[n], kp_bnot(SIGNAL_DONE));
		sub = u->subunit[n];
		if (u->queue_head[n] != KP_NONE && g->orders.order[u->queue_head[n]] != ORDER_WATCH_TARGET) {
			kp_issue_order(g, n, KP_DAT(g, units, return_to_idle, u->type[n]));
			kp_order_unlock_done(g, n);
			resume(g, sub);
		} else {
			resume(g, n);
			resume(g, sub);
		}
		return;
	default:
		return;
	}
}

/* OrderUnsieging 0x436be0 (order 99). */
void kp_order_unsieging(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t sub = u->subunit[n];
	switch (u->order_state[n]) {
	case 0:
		if (u->type[n] != TANK_SIEGED && u->type[n] != DUKE_SIEGED) {
			kp_order_done(g, n);
			return;
		}
		if (kp_band(u->status[sub], STATUS_NOBRK) != 0) return;
		turret_to_start(g, n);
		play_turret(g, n, ANIM_SPECIAL1);
		u->order_state[n] = 2;
		/* fall through */
	case 2:
		if (!kp_facing_target_waypoint(g, sub)) return;
		kp_sprite_play(g->w, kp_unit_sprite(g, n), ANIM_SPECIAL2, 1);
		play_turret(g, n, ANIM_SPECIAL2);
		u->order_signal[n] = kp_band(u->order_signal[n], kp_bnot(SIGNAL_DONE));
		u->order_state[n] = 3;
		/* fall through */
	case 3:
		if (kp_band(u->order_signal[n], SIGNAL_DONE) == 0) return;
		u->order_signal[n] = kp_band(u->order_signal[n], kp_bnot(SIGNAL_DONE));
		kp_change_unit_type(g, n, u->type[n] == DUKE_SIEGED ? DUKE : TANK);
		resume(g, n);
		resume(g, u->subunit[n]);
		return;
	default:
		return;
	}
}

/* ExecSiege 0x473330 (command 0x26) and ExecUnsiege 0x473930 (command 0x25) for one selected unit. */
int64_t kp_siege_command_order(KpGame *g, int64_t n, int siege)
{
	int64_t t = g->u.type[n], order = g->u.order[n];
	if (siege) return (t == TANK || t == DUKE) && order != ORDER_SIEGING ? ORDER_SIEGING : 0;
	return (t == TANK_SIEGED || t == DUKE_SIEGED) && order != ORDER_UNSIEGING ? ORDER_UNSIEGING : 0;
}
