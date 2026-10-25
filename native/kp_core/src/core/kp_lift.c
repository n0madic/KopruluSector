/* Terran add-ons and flying buildings (kp_game.h), ported from the atlas (P11, P12, P25, P27, E01): the add-on's
 * placement (ExecPlaceAddon 0x47d620, OrderPlaceAddon 36 0x435730), its construction (secondary BuildAddon 37
 * 0x4355e0) and cancel (CancelIncompleteBuildUnit 0x4355c0, command 0x34 0x4734f0), the lift-off (command 0x2f
 * 0x4728e0, BuildingLiftOff 72 0x435f00, LiftingOff 74 0x436030) and the landing (BuildingLand 71 0x435bf0, which
 * crushes what may not stay under it, 0x435ae0, and lifts again over what may not be crushed, 0x4370e0). */
#include "kp_game_internal.h"

#define DAT_BUILDING 0x1
#define DAT_ADDON 0x2
#define DAT_WORKER 0x8
#define DAT_SUBUNIT 0x10
#define DAT_FLYING_BUILDING 0x20
#define STATUS_COMPLETED 0x1
#define STATUS_GROUNDED_BUILDING 0x2
#define STATUS_AIR 0x4
#define STATUS_BURROWED 0x10
#define STATUS_ORDER_LOCK 0x1000
#define STATUS_CAN_TURN 0x10000
#define STATUS_CAN_MOVE 0x20000
#define SPRITE_SELECTED 0x08
#define SIGNAL_LANDED 0x10
#define STRING_CANNOT_BUILD 0x358
#define LIFT_HEIGHT 42
#define WORKER_LIFT_HEIGHT 7
#define AIR_ELEVATION 12
#define GROUND_ELEVATION 4
#define FLYING_SPEED 256
#define U_ADDON_TYPE 0xc4
#define U_LANDING 0xcb

static int64_t dat_flags(KpGame *g, int64_t t) { return KP_DAT(g, units, special_ability_flags, t); }
static int64_t sprite_x(KpGame *g, int64_t n) { return kp_s16v(g->w->spr.x[kp_unit_sprite(g, n)]); }
static int64_t sprite_y(KpGame *g, int64_t n) { return kp_s16v(g->w->spr.y[kp_unit_sprite(g, n)]); }
static int64_t place_w(KpGame *g, int64_t t) { return kp_s16v(KP_DAT(g, units, staredit_placement_size, t * 2)); }
static int64_t place_h(KpGame *g, int64_t t) { return kp_s16v(KP_DAT(g, units, staredit_placement_size, t * 2 + 1)); }
static int64_t div32(KpGame *g, int64_t v) { return kp_idiv64(g, v, 32); }

/* The signed vertical offset of the sprite's last image, its shadow (0x416060). */
static int64_t shadow_offset(KpGame *g, int64_t n)
{
	KpImages *w = g->w;
	int32_t tail = w->spr.images[kp_unit_sprite(g, n)].tail;
	return tail == KP_NONE ? 0 : w->img.yoff[tail]; /* a signed byte */
}

/* The flyer moves straight toward its waypoint, or stands on it (the turn the lift-off and the landing wait for). */
static int heading_to_waypoint(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	if (u->target_waypoint_x[n] == u->x[n] && u->target_waypoint_y[n] == u->y[n]) return 1;
	return kp_band(u->velocity_direction[n], 0xff)
		== kp_direction_to(g, sprite_x(g, n), sprite_y(g, n), u->target_waypoint_x[n], u->target_waypoint_y[n]);
}

/* CancelIncompleteBuildUnit 0x4355c0: the unfinished unit the building makes (an add-on) is cancelled. */
void kp_cancel_incomplete_build_unit(KpGame *g, int64_t n)
{
	int64_t b = g->u.build_unit[n];
	if (b != 0 && kp_band(g->u.status[b], STATUS_COMPLETED) == 0) kp_cancel_construction(g, b);
}

/* ExecPlaceAddon 0x47d620: the add-on's site and its building's next to it must both be free; the building then
 * gets the placement order at the building's site (the error message of a refusal is the interface's). */
void kp_command_place_addon(KpGame *g, int64_t player, int64_t order, int64_t tx, int64_t ty, int64_t t, int64_t n)
{
	int64_t px, py, bt = g->u.type[n];
	if (kp_validate_placement(g, n, player, tx, ty, t, 0, 0, 0) != 0) return;
	px = kp_s16v(tx) - div32(g, kp_s16v(KP_DAT(g, units, addon_position, t * 2)));
	py = kp_s16v(ty) - div32(g, kp_s16v(KP_DAT(g, units, addon_position, t * 2 + 1)));
	if (kp_validate_placement(g, n, player, px, py, bt, 0, 0, 0) != 0) return;
	kp_issue_placement(g, order, n, kp_s16v(place_w(g, bt) / 2 + px * 32), kp_s16v(place_h(g, bt) / 2 + py * 32), t);
}

/* OrderPlaceAddon 0x435730 (order 36): the building lands at (or lifts off and lands again at) its site, then pays
 * for the add-on and starts it next to itself. */
void kp_order_place_addon(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t t = kp_union_get(g, n, U_ADDON_TYPE, 2), bt = u->type[n], ox, oy, a, tx, ty;
	if (u->order_state[n] == 0) {
		if (kp_band(dat_flags(g, t), DAT_ADDON) == 0) return;
		ox = kp_s16v(u->order_x[n]);
		oy = kp_s16v(u->order_y[n]);
		if (kp_band(u->status[n], STATUS_GROUNDED_BUILDING) != 0 && kp_within_distance(g, n, 0, ox, oy)) {
			if (!kp_request_move_to_point(g, n, ox, oy)) return;
			u->order_state[n] = 1;
		} else {
			kp_issue_order_at(g, n, kp_band(u->status[n], STATUS_GROUNDED_BUILDING) != 0 ? KP_ORD_BUILDING_LIFTOFF
				: KP_ORD_BUILDING_LAND, ox, oy);
			kp_order_append(g, n, KP_ORD_PLACE_ADDON, kp_s16v(u->order_x[n]), kp_s16v(u->order_y[n]), 0, KP_TYPE_NONE, 0);
			return;
		}
	}
	if (u->order_state[n] != 1 || u->move_x[n] != sprite_x(g, n) || u->move_y[n] != sprite_y(g, n)) return;
	/* the order point becomes the add-on's center */
	u->order_x[n] = kp_s16v(u->order_x[n] - place_w(g, bt) / 2 + place_w(g, t) / 2 + KP_DAT(g, units, addon_position, t * 2));
	u->order_y[n] = kp_s16v(u->order_y[n] - place_h(g, bt) / 2 + place_h(g, t) / 2
		+ KP_DAT(g, units, addon_position, t * 2 + 1));
	if (!kp_check_unit_cost(g, u->owner[n], t, 1, 1)) {
		kp_order_done(g, n);
		return;
	}
	tx = div32(g, kp_s16v(u->order_x[n])) - kp_idiv64(g, div32(g, place_w(g, t)), 2);
	ty = div32(g, kp_s16v(u->order_y[n])) - kp_idiv64(g, div32(g, place_h(g, t)), 2);
	if (kp_validate_placement(g, n, u->owner[n], tx, ty, t, 0, 1, 0) != 0) {
		kp_worker_error(g, n, STRING_CANNOT_BUILD);
		kp_order_done(g, n);
		return;
	}
	a = kp_unit_create(g, t, kp_s16v(u->order_x[n]), kp_s16v(u->order_y[n]), u->owner[n]);
	if (a == 0) {
		kp_report_creation_error(g, u->owner[n]); /* 0x425020; the order stays */
		return;
	}
	kp_deduct_cost(g, u->owner[n]);
	kp_issue_order(g, n, KP_DAT(g, units, return_to_idle, bt));
	kp_set_secondary_order(g, n, KP_ORD_BUILD_ADDON);
	u->build_unit[n] = a;
	kp_issue_order(g, a, KP_ORD_INCOMPLETE_BUILDING);
	kp_ai_add_building(g, n, a); /* AddBuildingToBuilderTown 0x486200 */
	u->connected[a] = n;
	g->interface_refresh++; /* RefreshStatusUi 0x4a7ed0, for the interface */
}

/* The secondary BuildAddon 0x4355e0 (37): a grounded building builds its add-on and attaches it when done; a lifted
 * one cancels it; then the building forgets the add-on's type and the secondary order ends. */
void kp_secondary_build_addon(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t a = u->build_unit[n];
	if (kp_unit_disabled(g, n)) return;
	if (a != 0) {
		if (kp_band(u->status[n], STATUS_GROUNDED_BUILDING) == 0) {
			if (kp_band(u->status[a], STATUS_COMPLETED) == 0) kp_cancel_construction(g, a);
		} else {
			kp_progress_construction(g, a, kp_build_hp_gain(g, a), 0); /* 0x41a280 */
			if (kp_band(u->status[a], STATUS_COMPLETED) == 0) return;
			kp_construction_voice(g, n); /* 0x446280 */
			kp_attach_addon(g, n, a);    /* 0x4359c0 */
		}
	}
	kp_union_set(g, n, U_ADDON_TYPE, 2, KP_TYPE_NONE);
	if (u->ai[n] != 0) kp_issue_order(g, n, KP_DAT(g, units, comp_ai_idle, u->type[n]));
	kp_set_secondary_order(g, n, KP_ORD_NOTHING);
}

/* ExecCancelAddon 0x4734f0 (command 0x34): the only selected completed building of the player's that builds an
 * add-on cancels it. */
void kp_command_cancel_addon(KpGame *g, int64_t player, int64_t n)
{
	KpUnits *u = &g->u;
	if (n == 0 || !kp_constructing_addon(g, n) || kp_band(u->status[n], STATUS_COMPLETED) == 0 || u->owner[n] != player) return;
	kp_cancel_incomplete_build_unit(g, n);
	g->interface_refresh++; /* RefreshStatusUi 0x4a7ed0, for the interface */
}

/* ExecLiftOff 0x4728e0 (command 0x2f): the only selected idle grounded building that can fly lifts off toward the
 * point (signed: a negative one passes) and then idles 42 pixels above it. */
void kp_command_lift_off(KpGame *g, int64_t player, int64_t n, int64_t x, int64_t y)
{
	KpUnits *u = &g->u;
	if (n == 0 || kp_unit_occupied(g, n) /* 0x41b300, 0x435990, 0x42f240, +0xc8, +0xc9 */
		|| kp_band(u->status[n], STATUS_GROUNDED_BUILDING) == 0 || kp_band(dat_flags(g, u->type[n]), DAT_FLYING_BUILDING) == 0) {
		return;
	}
	if (kp_can_issue_order(g, KP_ORD_BUILDING_LIFTOFF, player, n) != 1) return; /* the refusal's message is the interface's */
	if (x >= g->map_w_px || y >= g->map_h_px) return;
	kp_issue_order_at(g, n, KP_ORD_BUILDING_LIFTOFF, x, y);
	kp_order_append(g, n, KP_DAT(g, units, return_to_idle, u->type[n]), x, kp_s16v(y - LIFT_HEIGHT), 0, KP_TYPE_NONE, 0);
}

/* OrderBuildingLiftOff 0x435f00 (order 72): the grounded building leaves its add-on and the ground and becomes a
 * flyer, then rises (LiftingOff). */
void kp_order_building_liftoff(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t t = u->type[n], status;
	int32_t s = kp_unit_sprite(g, n);
	if (kp_band(u->status[n], STATUS_GROUNDED_BUILDING) == 0 || kp_band(dat_flags(g, t), DAT_FLYING_BUILDING) == 0) {
		if (u->queue_head[n] == KP_NONE) kp_order_append(g, n, KP_DAT(g, units, return_to_idle, t), 0, 0, 0, KP_TYPE_NONE, 0);
		kp_order_unlock_done(g, n);
		return;
	}
	if (u->union_0xc0[n] != 0) kp_detach_addon(g, n); /* 0x435ba0 */
	if (kp_rally_producer(t)) u->rally_unit[n] = n;
	kp_unit_deactivate_spatial(g, n); /* 0x41a210 */
	status = kp_bor(kp_band(u->status[n], kp_bnot(STATUS_GROUNDED_BUILDING)), STATUS_CAN_TURN);
	status = kp_band(dat_flags(g, t), DAT_SUBUNIT) == 0 ? kp_bor(status, STATUS_CAN_MOVE)
		: kp_band(status, kp_bnot(STATUS_CAN_MOVE));
	u->status[n] = status;
	kp_reset_movement(g, n); /* 0x42c7e0 */
	g->w->spr.elevation[s] = AIR_ELEVATION;
	u->status[n] = kp_bor(u->status[n], STATUS_AIR);
	kp_reset_movement(g, n);
	kp_unit_activate_spatial(g, n); /* 0x41a050 */
	kp_sprite_hold_shadow(g->w, s, 1);
	g->interface_refresh++; /* RefreshStatusUi 0x4a7ed0, for the interface */
	kp_flingy_set_target_waypoint(g, n, sprite_x(g, n), sprite_y(g, n) - LIFT_HEIGHT); /* 0x40cbd0 */
	if (t == KP_TYPE_INFESTED_COMMAND_CENTER) {
		/* RemoveCreepProvider 0x4b47f0; with the pool full the creep stays */
		kp_creep_remove_provider(g, t, u->x[n], u->y[n], kp_band(u->status[n], STATUS_COMPLETED) != 0);
	}
	kp_placement_refresh_ghosts(g); /* 0x47d070, 0x47d530 */
	u->order[n] = KP_ORD_LIFTING_OFF;
}

/* OrderLiftingOff 0x436030 (order 74): facing its waypoint the building (or a worker) rises 42 (7) pixels, and on
 * its script's signal flies on. */
void kp_order_lifting_off(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t t = u->type[n], height = kp_band(dat_flags(g, t), DAT_WORKER) != 0 ? WORKER_LIFT_HEIGHT : LIFT_HEIGHT;
	int32_t s = kp_unit_sprite(g, n);
	switch (u->order_state[n]) {
	case 0:
		if (!heading_to_waypoint(g, n)) return;
		/* fall through */
	case 1:
		kp_request_move_to_point(g, n, sprite_x(g, n), sprite_y(g, n) + shadow_offset(g, n) - height);
		u->order_state[n] = 2;
		return;
	case 2:
		kp_sprite_play(g->w, s, KP_ANIM_LIFT_OFF, 1);
		u->order_state[n] = 3;
		kp_sprite_landing_dust(g->w, s, 1); /* 0x415d20 */
		u->top_speed[n] = FLYING_SPEED;
		kp_flingy_set_speed(g, n, FLYING_SPEED); /* 0x40c220 */
		/* fall through */
	case 3:
		if (u->move_x[n] != sprite_x(g, n) || u->move_y[n] != sprite_y(g, n) || kp_band(u->order_signal[n], SIGNAL_LANDED) == 0) {
			return;
		}
		u->order_signal[n] = kp_band(u->order_signal[n], kp_bnot(SIGNAL_LANDED));
		if (kp_band(dat_flags(g, t), DAT_BUILDING) == 0) {
			u->top_speed[n] = kp_top_speed_modified(g, n, KP_DAT(g, flingy, speed, u->flingy[n])); /* 0x428cc0 */
		}
		kp_sprite_set_shadow_offset(g->w, s, (int32_t)height);
		kp_sprite_hold_shadow(g->w, s, 0);
		kp_order_unlock_done(g, n);
		return;
	}
}

/* The units of the border query over the building's extents (0x442b60). */
static int64_t *under(KpGame *g, int64_t n, int *count)
{
	int64_t t = g->u.type[n], x = sprite_x(g, n), y = sprite_y(g, n);
	return kp_query_rect(g, kp_s16v(x - KP_DAT(g, units, unit_extents, t * 4)),
		kp_s16v(y - KP_DAT(g, units, unit_extents, t * 4 + 1)), kp_s16v(x + KP_DAT(g, units, unit_extents, t * 4 + 2)),
		kp_s16v(y + KP_DAT(g, units, unit_extents, t * 4 + 3)), count);
}

/* 0x435ae0 with LandingCrushCallback 0x435b50: the friendly units under the landed building that may not stay
 * there (armed and unable to move, or burrowed) die. */
static void crush(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int count, k;
	int64_t *found = under(g, n, &count);
	for (k = 0; k < count; k++) {
		int64_t c = found[k];
		if (c == n || kp_is_enemy(g, c, n)) continue;
		if ((!kp_unit_can_move(g, c) && kp_unit_has_weapon(g, c)) || kp_band(u->status[c], STATUS_BURROWED) != 0) {
			kp_unit_kill(g, c);
		}
	}
	kp_query_release(g, found);
}

/* 0x4370e0 with IsArmedImmobileUnderBuilding 0x436f20: an armed unit that cannot move (and is not dying) under the
 * landed building makes it lift off again. */
static void lift_over_armed(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int count, k, any = 0;
	int64_t *found = under(g, n, &count);
	for (k = 0; k < count && !any; k++) {
		int64_t c = found[k];
		any = c != n && !kp_unit_can_move(g, c) && kp_unit_has_weapon(g, c) && u->order[c] != KP_ORD_DIE;
	}
	kp_query_release(g, found);
	if (!any) return;
	kp_issue_order(g, n, KP_ORD_BUILDING_LIFTOFF);
	kp_order_append(g, n, KP_DAT(g, units, return_to_idle, u->type[n]), 0, 0, 0, KP_TYPE_NONE, 0);
}

/* The queue after a landing: a Move or Follow first gets a lift-off before it; else every order after a first
 * PlaceAddon (all of them without it) goes, and an emptied queue idles. */
static void landed_queue(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t head = u->queue_head[n], order, keep;
	if (head == KP_NONE) return;
	order = g->orders.order[head];
	if (order == KP_ORD_MOVE || order == KP_ORD_FOLLOW) {
		kp_order_insert_before(g, n, KP_ORD_BUILDING_LIFTOFF, 0, 0, 0, KP_TYPE_NONE, head); /* 0x429de0 */
		return;
	}
	keep = order == KP_ORD_PLACE_ADDON ? head : KP_NONE;
	while (u->queue_tail[n] != keep) kp_order_remove(g, n, u->queue_tail[n]); /* 0x429d20 */
	if (u->queue_head[n] == KP_NONE) kp_issue_order(g, n, KP_DAT(g, units, return_to_idle, u->type[n]));
}

/* OrderBuildingLand 0x435bf0 (order 71): the flyer goes above the site, reserves it when it is free, descends, and
 * on its script's signal becomes a grounded building again, crushing what may not stay under it. */
void kp_order_building_land(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t t = u->type[n], ox = kp_s16v(u->order_x[n]), oy = kp_s16v(u->order_y[n]), tx, ty, a;
	int32_t s;
	if (kp_band(u->status[n], STATUS_GROUNDED_BUILDING) != 0) {
		kp_issue_order(g, n, KP_ORD_BUILDING_LIFTOFF);
		kp_order_append(g, n, KP_ORD_BUILDING_LAND, ox, oy, u->order_unit[n], KP_TYPE_NONE, 0); /* 0x42a0c0 */
		kp_order_append(g, n, KP_DAT(g, units, return_to_idle, t), 0, 0, 0, KP_TYPE_NONE, 0);
		return;
	}
	switch (u->order_state[n]) {
	case 0:
		kp_request_move_to_point(g, n, ox, oy - shadow_offset(g, n));
		u->order_state[n] = 1;
		return;
	case 1:
		if (kp_move_arrival(g, n) == 0) return;
		tx = div32(g, ox - place_w(g, t) / 2);
		ty = div32(g, oy - place_h(g, t) / 2);
		if (kp_validate_placement(g, n, u->owner[n], tx, ty, t, 0, 1, 0) != 0) {
			kp_cannot_land_voice(g, n);
			if (u->queue_head[n] != KP_NONE && g->orders.order[u->queue_head[n]] == KP_ORD_PLACE_ADDON) {
				kp_order_remove(g, n, u->queue_head[n]); /* 0x42a140 */
			}
			kp_order_done(g, n);
			return;
		}
		kp_building_occupancy_at(g, t, ox, oy, 1); /* 0x419fd0 */
		kp_union_set(g, n, U_LANDING, 1, 1);
		kp_request_move_to_point(g, n, ox, oy);
		kp_sprite_hold_shadow(g->w, kp_unit_sprite(g, n), 1);
		u->status[n] = kp_bor(u->status[n], STATUS_ORDER_LOCK);
		u->top_speed[n] = FLYING_SPEED;
		kp_flingy_set_speed(g, n, FLYING_SPEED); /* 0x40c220 */
		u->order_state[n] = 2;
		return;
	case 2:
		if (!heading_to_waypoint(g, n)) return;
		kp_sprite_play(g->w, kp_unit_sprite(g, n), KP_ANIM_LANDING, 1);
		u->order_state[n] = 3;
		/* fall through */
	case 3:
		if (kp_move_arrival(g, n) == 0 || kp_band(u->order_signal[n], SIGNAL_LANDED) == 0) return;
		u->order_signal[n] = kp_band(u->order_signal[n], kp_bnot(SIGNAL_LANDED));
		kp_building_occupancy_at(g, t, ox, oy, 0); /* 0x41a190 */
		kp_unit_deactivate_spatial(g, n);           /* 0x41a210 */
		u->status[n] = kp_bor(kp_band(u->status[n], kp_bnot(STATUS_CAN_TURN | STATUS_CAN_MOVE)), STATUS_GROUNDED_BUILDING);
		kp_reset_movement(g, n); /* 0x436fc0 */
		s = kp_unit_sprite(g, n);
		g->w->spr.elevation[s] = GROUND_ELEVATION;
		u->status[n] = kp_band(u->status[n], kp_bnot(STATUS_AIR));
		kp_reset_movement(g, n); /* 0x436fe0(0) */
		kp_unit_relocate(g, n, ox, oy);
		kp_unit_activate_spatial(g, n); /* 0x41a050 */
		if (kp_band(g->w->spr.flags[s], SPRITE_SELECTED) != 0 && kp_local_selection_count(g) > 1) kp_leave_local_selection(g, n);
		kp_sprite_set_shadow_offset(g->w, s, 0);
		kp_sprite_hold_shadow(g->w, s, 0);
		kp_sprite_landing_dust(g->w, s, 0); /* 0x415ce0 */
		g->interface_refresh++; /* RefreshStatusUi 0x4a7ed0, for the interface */
		kp_union_set(g, n, U_LANDING, 1, 0);
		crush(g, n);
		landed_queue(g, n);
		lift_over_armed(g, n);
		kp_order_unlock_done(g, n);
		a = kp_find_attachable_addon(g, n);
		if (a != 0) kp_attach_addon(g, n, a);
		kp_placement_refresh_ghosts(g); /* 0x47d070, 0x47d530 */
		kp_placement_recheck(g); /* 0x47db60, 0x47d300 */
		return;
	}
}
