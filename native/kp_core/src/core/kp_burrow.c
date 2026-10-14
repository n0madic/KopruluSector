/* Burrow and Unburrow (kp_game.h), ported from the atlas (P12, P13): the commands 0x2c and 0x2d, the orders
 * Burrowing 116 and Unburrowing 118, RequestUnburrow and the burrowed state of a unit's sprite. */
#include "kp_game_internal.h"

#define IRRADIATE_OVERLAY 380
#define ENSNARE_OVERLAY 384
#define PLAGUE_OVERLAY 388

#define BURROWING 11 /* the tech */
#define ORDER_BURROWING 0x74
#define ORDER_BURROWED 0x75
#define ORDER_UNBURROWING 0x76
#define SECONDARY_CLOAK 0x6d
#define SECONDARY_NOTHING 0x17
#define LURKER 0x67
#define DRONE 0x29
#define DEFILER 0x2e
#define UNCLEAN_ONE 0x34
#define STATUS_BURROWED 0x10
#define STATUS_BURROWED_STATE 0x810 /* burrowed, cloaked for free */
#define STATUS_NOT_INTERRUPTIBLE 0x1000
#define STATUS_NO_COLLIDE 0x200000
#define MOVING 0x2
#define SPRITE_BURROWED 0x40
#define SIGNAL_BURROW 0x4
#define UNDETERMINED 0x80000000LL
#define BURROWED_SPRITE 300
#define BURROWED_LURKER_SPRITE 504
#define ANIM_WALKING_TO_IDLE 12
#define ANIM_BURROW 25
#define ANIM_UNBURROW 26
#define SOUND_BURROW 16
#define SOUND_UNBURROW 17
#define FACE_BURROW 12
#define NO_TYPE 0xe4
#define CARRIED_FIRST 392
#define CARRIED_LAST 404
#define CARRIED2_FIRST 958
#define CARRIED2_LAST 959

/* ApplyBurrowedState 0x43d940. */
void kp_set_burrowed(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	KpImages *w = g->w;
	int32_t s;
	kp_reset_movement(g, n);
	u->status[n] = kp_bor(u->status[n], STATUS_BURROWED_STATE);
	s = kp_unit_sprite(g, n);
	w->spr.flags[s] = (int32_t)kp_bor(w->spr.flags[s], SPRITE_BURROWED);
	kp_sprite_change_id(w, s, u->type[n] == LURKER ? BURROWED_LURKER_SPRITE : BURROWED_SPRITE);
	w->spr.elevation[s] = 1;
}

/* ClearBurrowedState 0x43da00. */
void kp_clear_burrowed(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	kp_reset_movement(g, n);
	u->status[n] = kp_band(u->status[n], kp_bnot(STATUS_BURROWED_STATE));
	kp_sprite_change_id(g->w, kp_unit_sprite(g, n),
		(int32_t)KP_DAT(g, flingy, sprite, KP_DAT(g, units, graphics, u->type[n])));
}

/* The first carried-item image of sprite s: a mineral or gas chunk (392..404), else a flag (958..959). */
static int32_t carried_image(KpImages *w, int32_t s)
{
	int32_t i;
	for (i = w->spr.images[s].head; i != KP_NONE; i = w->img.next[i]) {
		if (w->img.id[i] >= CARRIED_FIRST && w->img.id[i] <= CARRIED_LAST) return i;
	}
	for (i = w->spr.images[s].head; i != KP_NONE; i = w->img.next[i]) {
		if (w->img.id[i] >= CARRIED2_FIRST && w->img.id[i] <= CARRIED2_LAST) return i;
	}
	return KP_NONE;
}

/* HideStatusOverlaysOnBurrow 0x43df40. */
static void hide_status_overlays(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	if (u->matrix_damage[n] != 0) {
		/* the matrix goes from sight but not from the unit (ReduceDefensiveMatrix 0x4384c0 by all it has left) */
		int64_t damage = u->matrix_damage[n], timer = u->matrix_timer[n];
		kp_reduce_matrix(g, n, damage);
		u->matrix_damage[n] = damage;
		u->matrix_timer[n] = timer;
	}
	if (u->irradiate_timer[n] != 0) kp_remove_status_overlay(g, n, IRRADIATE_OVERLAY, IRRADIATE_OVERLAY + 2);
	if (u->ensnare_timer[n] != 0) kp_remove_status_overlay(g, n, ENSNARE_OVERLAY, ENSNARE_OVERLAY + 2);
	if (u->plague_timer[n] != 0) kp_remove_status_overlay(g, n, PLAGUE_OVERLAY, PLAGUE_OVERLAY + 2);
}

/* OrderBurrowing 0x43c6f0 (order 116). */
void kp_order_burrowing(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	KpImages *w = g->w;
	int32_t s = kp_unit_sprite(g, n);
	int64_t t = u->type[n], i;
	switch (u->order_state[n]) {
	case 0:
		kp_stop_movement(g, n);
		u->order_state[n] = 1;
		/* fall through */
	case 1:
		if (kp_band(u->movement_flags[n], MOVING) != 0) {
			if (u->speed2[n] == 0) kp_halt_at_target(g, n);
			return;
		}
		kp_sprite_play(w, s, ANIM_WALKING_TO_IDLE, 1);
		kp_flingy_set_spawn_frame(g, n, t == DEFILER || t == UNCLEAN_ONE ? FACE_BURROW - 1 : t == DRONE ? FACE_BURROW + 1 : FACE_BURROW);
		u->order_state[n] = 2;
		/* fall through */
	case 2:
		if (!kp_facing_target_waypoint(g, n)) return;
		kp_sprite_play(w, s, ANIM_BURROW, 1);
		u->order_state[n] = 3;
		kp_play_unit_sound(g, SOUND_BURROW, n);
		/* fall through */
	case 3:
		if (kp_band(u->order_signal[n], SIGNAL_BURROW) == 0) return;
		u->order_signal[n] = kp_band(u->order_signal[n], kp_bnot(SIGNAL_BURROW));
		kp_sprite_hide_uncloakable(w, s);
		/* HideCarriedItemOverlay 0x416290 */
		i = carried_image(w, s);
		if (i != KP_NONE) kp_image_hide(w, (int32_t)i);
		u->status[n] = kp_bor(u->status[n], STATUS_NO_COLLIDE);
		kp_set_burrowed(g, n);
		kp_set_secondary_order(g, n, SECONDARY_CLOAK);
		u->visibility[n] = UNDETERMINED;
		hide_status_overlays(g, n);
		kp_issue_order(g, n, ORDER_BURROWED);
		kp_order_unlock_done(g, n);
		return;
	default:
		return;
	}
}

/* OrderUnburrowing 0x43c880 (order 118). */
void kp_order_unburrowing(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	KpImages *w = g->w;
	int32_t s = kp_unit_sprite(g, n);
	int64_t i;
	switch (u->order_state[n]) {
	case 0:
		w->spr.elevation[s] = (int32_t)KP_DAT(g, units, elevation_level, u->type[n]);
		u->status[n] = kp_band(u->status[n], kp_bnot(STATUS_NO_COLLIDE));
		kp_update_overlap_flags(g, n);
		w->spr.flags[s] = (int32_t)kp_band(w->spr.flags[s], kp_bnot(SPRITE_BURROWED));
		kp_set_secondary_order(g, n, SECONDARY_NOTHING);
		kp_sprite_play(w, s, ANIM_UNBURROW, 1);
		kp_play_unit_sound(g, SOUND_UNBURROW, n);
		u->order_state[n] = 1;
		return;
	case 1:
		u->visibility[n] = UNDETERMINED;
		u->secondary_timer[n] = 0;
		u->order_state[n] = 2;
		/* fall through */
	case 2:
		if (kp_band(u->order_signal[n], SIGNAL_BURROW) == 0) return;
		u->order_signal[n] = kp_band(u->order_signal[n], kp_bnot(SIGNAL_BURROW));
		kp_clear_burrowed(g, n);
		kp_rebuild_status_overlays(g, n);
		/* ShowCarriedItemOverlay 0x4174d0 */
		i = carried_image(w, s);
		if (i != KP_NONE) kp_image_show(w, (int32_t)i);
		kp_sprite_set_facing(w, s, (int32_t)u->heading[n]);
		u->order_state[n] = 3;
		return;
	case 3:
		if (u->queue_head[n] == KP_NONE) {
			kp_order_append(g, n, KP_DAT(g, units, return_to_idle, u->type[n]), 0, 0, 0, NO_TYPE, 0);
		}
		kp_order_unlock_done(g, n);
		return;
	default:
		return;
	}
}

/* RequestUnburrow 0x43c560. */
void kp_request_unburrow(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t order = u->order[n];
	if (order != ORDER_BURROWING && order != ORDER_BURROWED && u->type[n] != LURKER) return;
	if (u->type[n] == LURKER) u->status[n] = kp_bor(u->status[n], STATUS_NOT_INTERRUPTIBLE);
	kp_issue_order(g, n, ORDER_UNBURROWING);
	kp_order_unlock_done(g, n);
}
