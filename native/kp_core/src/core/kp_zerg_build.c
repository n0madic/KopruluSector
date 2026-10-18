/* The Zerg construction (kp_game.h), ported from the atlas (P12, P13; docs/research/native-zerg-construction.md,
 * native-extractor.md, native-nydus.md): a Drone's DroneStartBuild 25 (0x43ba90), DroneLand 70 (0x43c970) and
 * DroneBuild 26 (0x43b900) that pays and turns it into the building in its own unit (ConvertDroneToBuilding 0x43c000)
 * or into an Extractor over a Geyser (BeginExtractorConstruction 0x43bf80), the common start InitZergConstruction
 * 0x43bf10, a building's ZergBuildSelf 45 (0x43b6e0), the building morphs (command 0x35 0x473ac0, order
 * ZergBuildingMorph 43 0x43be80), the Nydus Canal's exit (BuildNydusExit 46 0x43bc50) and the cancel of an
 * unfinished Zerg building or building morph (CancelZergBuilding 0x43b380, 0x43b500). */
#include "kp_game_internal.h"

#define NO_TYPE 228
#define DRONE 41
#define CONTROLLER_COMPUTER 1
#define EXTRACTOR 149
#define NYDUS_CANAL 134
#define LAIR 132
#define HIVE 133
#define GREATER_SPIRE 137
#define SPORE_COLONY 144
#define SUNKEN_COLONY 146
#define FIRST_BUILDING_MORPH 130
#define LAST_BUILDING_MORPH 152
#define DAT_WORKER 0x8
#define STATUS_COMPLETED 0x1
#define STATUS_COLLIDES 0x100000
#define STATUS_IGNORE_TILE_COLLISION 0x800000
#define STATUS_ORDER_LOCK 0x1000
#define STATUS_ORDER_LOCKS 0x3000
#define SIGNAL_BIRTH 0x4
#define ORDER_DRONE_BUILD 26
#define ORDER_BUILDING_MORPH 43
#define ORDER_BUILD_SELF 45
#define ORDER_DRONE_LAND 70
#define ORDER_RESET_COLLISION 150
#define ANIM_SPECIAL1 13
#define ANIM_SPECIAL2 14
#define ANIM_ALMOST_BUILT 15
#define BUTTONS_ZERG_CONSTRUCTION 233
#define BUTTONS_ZERG_RALLY_CONSTRUCTION 234
#define BUTTONS_BUILDING_MORPH 236
#define BUTTONS_NYDUS_EXIT 228
#define IMAGE_GAS_CONSTRUCTION 345
#define SPRITE_ZERG_CANCEL 182
#define SOUND_ZERG_CANCEL 4
#define STRING_CANNOT_BUILD 0x358
#define SHADOW_LANDED 7
#define LAND_HEIGHT 7
#define LAND_REACH 128
#define GEYSER_REACH 32
#define MIN_MORPH_HP 0x100
#define HP_SHARE 10

static int64_t sprite_x(KpGame *g, int64_t n) { return kp_s16v(g->w->spr.x[kp_unit_sprite(g, n)]); }
static int64_t sprite_y(KpGame *g, int64_t n) { return kp_s16v(g->w->spr.y[kp_unit_sprite(g, n)]); }

/* IsAdvancedZergBuildingType 0x43c320: the building morphs' targets. */
static int building_morph(int64_t t)
{
	return t == HIVE || t == LAIR || t == GREATER_SPIRE || t == SPORE_COLONY || t == SUNKEN_COLONY;
}

/* The DroneBuild queued after the current order goes (GetQueueHeadOrder 0x429d10, RemoveQueueHead 0x42a140). */
static void drop_queued_build(KpGame *g, int64_t n)
{
	int64_t head = g->u.queue_head[n];
	if (head != KP_NONE && g->orders.order[head] == ORDER_DRONE_BUILD) kp_order_remove(g, n, head);
}

/* The Drone back on the ground: the shadow under it again, then ResetCollision and the next order. */
static void reset_drone(KpGame *g, int64_t n)
{
	int32_t s = kp_unit_sprite(g, n);
	kp_sprite_set_shadow_offset(g->w, s, SHADOW_LANDED); /* 0x417260 */
	kp_sprite_refresh_bounds(g->w, s);                   /* 0x416520 */
	kp_order_prepend(g, n, ORDER_RESET_COLLISION);       /* 0x429b20 */
	kp_order_unlock_done(g, n);                          /* 0x429d00 */
}

/* OrderDroneStartBuild 0x43ba90 (order 25): DroneLand to the site, DroneBuild after it. */
void kp_order_drone_start_build(KpGame *g, int64_t n)
{
	int64_t x = g->u.order_x[n], y = g->u.order_y[n];
	kp_issue_order_at(g, n, ORDER_DRONE_LAND, x, y);                  /* 0x42a340 */
	kp_order_append(g, n, ORDER_DRONE_BUILD, x, y, 0, NO_TYPE, 0); /* QueueOrderAtPoint 0x429ee0 */
}

/* OrderDroneLand 0x43c970 (order 70): the Drone flies to 7 pixels above the site, checks it (an Extractor's Geyser
 * within reach) and lands on it. */
void kp_order_drone_land(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t ox = kp_s16v(u->order_x[n]), oy = kp_s16v(u->order_y[n]), r, t, tx, ty, p, geyser;
	int32_t s = kp_unit_sprite(g, n);
	if (u->ai[n] != 0) u->status[n] = kp_bor(u->status[n], STATUS_IGNORE_TILE_COLLISION);
	switch (u->order_state[n]) {
	case 0:
		if (kp_request_move_to_point(g, n, ox, oy - LAND_HEIGHT)) u->order_state[n] = 1; /* 0x43a070 */
		return;
	case 1:
		r = kp_move_arrival(g, n);
		if (r == 0) return;
		t = kp_queued_type(g, n);
		kp_site_tile(g, t, ox, oy, &tx, &ty);
		p = kp_validate_placement(g, n, u->owner[n], tx, ty, t, 0, 1, 1);
		if (t == EXTRACTOR && r == 1) {
			if (p != 0) break;
			geyser = kp_geyser_at(g, ox, oy);
			if (geyser != 0 && kp_unit_distance(g, n, geyser) <= GEYSER_REACH) { /* 0x4229a0 */
				u->status[n] = kp_band(u->status[n], kp_bnot(STATUS_COLLIDES));
				g->w->spr.elevation[s] = (int32_t)KP_DAT(g, units, elevation_level, u->type[n]) + 1;
				kp_order_append(g, n, ORDER_RESET_COLLISION, 0, 0, 0, NO_TYPE, 0);
				kp_order_append(g, n, KP_DAT(g, units, return_to_idle, u->type[n]), 0, 0, 0, NO_TYPE, 0);
				if (!kp_request_move_to_point(g, n, ox, oy)) return;
				u->status[n] = kp_bor(u->status[n], STATUS_ORDER_LOCK);
				u->order_state[n] = 2;
				return;
			}
		} else if (p != 0 || r == 2) {
			break;
		}
		if (!kp_within_distance(g, n, LAND_REACH, ox, oy)) break;
		if (!kp_request_move_to_point(g, n, ox, oy) && kp_band(u->status[n], STATUS_ORDER_LOCKS) == 0) return;
		kp_sprite_hold_shadow(g->w, s, 1);
		u->status[n] = kp_bor(u->status[n], STATUS_ORDER_LOCK);
		u->order_state[n] = 2;
		return;
	case 2:
		r = kp_move_arrival(g, n);
		if (r == 0) return;
		kp_sprite_set_shadow_offset(g->w, s, 0);
		kp_sprite_hold_shadow(g->w, s, 0);
		if (r == 2) {
			kp_cannot_land_voice(g, n);
			drop_queued_build(g, n);
			reset_drone(g, n);
		} else if (u->queue_head[n] != KP_NONE && g->orders.order[u->queue_head[n]] == ORDER_DRONE_BUILD) {
			kp_unit_relocate(g, n, ox, oy); /* 0x43a270 */
			kp_order_unlock_done(g, n);
		} else {
			reset_drone(g, n);
		}
		return;
	default:
		return;
	}
	kp_cannot_land_voice(g, n);
	drop_queued_build(g, n);
	kp_issue_order(g, n, KP_DAT(g, units, return_to_idle, u->type[n]));
}

/* InitZergConstruction 0x43bf10: the creep under it stops receding, the construction images and buttons, a tenth of
 * the hit points, ZergBuildSelf and an empty queue slot. */
static void init_construction(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	kp_creep_restore_provider(g, n); /* 0x4b4dd0 */
	kp_set_button_set(g, n, kp_rally_producer(u->type[n]) ? BUTTONS_ZERG_RALLY_CONSTRUCTION : BUTTONS_ZERG_CONSTRUCTION);
	kp_replace_unit_images(g, n, 1); /* 0x423330 */
	u->hp[n] = kp_udiv64(g, KP_DAT(g, units, hit_points, u->type[n]), HP_SHARE);
	kp_issue_order(g, n, ORDER_BUILD_SELF);
	*kp_queue_head(g, n) = NO_TYPE;
}

/* The WorkerAI of a computer's Drone becomes a BuildingAI (0x486ba0) or the other way back (0x487900). */
static void town_ai(KpGame *g, int64_t n, const char *what)
{
	/* TODO: the Town's worker and building records once the AIScript VM of plan phase 7 gives the computer towns */
	if (g->u.ai[n] != 0 && kp_controller(g, g->u.owner[n]) == CONTROLLER_COMPUTER) {
		kp_game_fail(g, "zerg build: the Town of a computer's %s", what);
	}
}

/* BeginExtractorConstruction 0x43bf80: the Geyser at the site becomes the Extractor and the Drone goes into it. */
static void begin_extractor(KpGame *g, int64_t n)
{
	int64_t b = kp_convert_geyser(g, n, EXTRACTOR);
	int32_t s = kp_unit_sprite(g, n);
	if (b == 0) {
		kp_sprite_set_shadow_offset(g->w, s, SHADOW_LANDED);
		kp_sprite_hold_shadow(g->w, s, 1);
		kp_flingy_set_target_waypoint(g, n, sprite_x(g, n), sprite_y(g, n) - LAND_HEIGHT); /* 0x40cbd0 */
		kp_order_prepend(g, n, ORDER_RESET_COLLISION);
		kp_order_unlock_done(g, n);
		return;
	}
	kp_builder_town(g, n); /* 0x486200 */
	kp_kill_flagged(g, n); /* 0x425700 */
	init_construction(g, b);
	kp_sprite_image_below_main(g->w, kp_unit_sprite(g, b), IMAGE_GAS_CONSTRUCTION, 0, 0, 0);
}

/* OrderDroneBuild 0x43b900 (order 26): on the site, the Drone pays and becomes the building, dropping a powerup it
 * carries at the powerup's home. */
void kp_order_drone_build(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t t, tx, ty, powerup;
	if (sprite_x(g, n) == kp_s16v(u->order_x[n]) && sprite_y(g, n) == kp_s16v(u->order_y[n])) {
		t = kp_queued_type(g, n);
		kp_site_tile(g, t, sprite_x(g, n), sprite_y(g, n), &tx, &ty);
		if (kp_validate_placement(g, n, u->owner[n], tx, ty, t, 0, 1, 1) != 0) {
			kp_worker_error(g, n, STRING_CANNOT_BUILD);
		} else if (kp_check_unit_cost(g, u->owner[n], t, 1, 1)) {
			kp_deduct_cost(g, u->owner[n]);
			powerup = kp_band(KP_DAT(g, units, special_ability_flags, u->type[n]), DAT_WORKER) != 0 ? u->union_0xc0[n] : 0;
			if (powerup != 0) kp_drop_powerup(g, n);
			if (t == EXTRACTOR) {
				begin_extractor(g, n);
			} else { /* ConvertDroneToBuilding 0x43c000 */
				kp_account_completed(g, n, -1, 0); /* 0x414580 */
				u->status[n] = kp_band(u->status[n], kp_bnot(STATUS_COMPLETED));
				kp_change_unit_type(g, n, t);
				town_ai(g, n, "Drone becoming a building (0x486ba0)");
				init_construction(g, n);
			}
			if (powerup != 0) {
				int64_t home = kp_union_get(g, powerup, 0xd0, 4);
				kp_unit_relocate(g, powerup, kp_s16v(home), kp_s16v(kp_shrv(home, 16)));
			}
			return;
		}
	}
	reset_drone(g, n);
}

/* OrderZergBuildSelf 0x43b6e0 (order 45): the stages of the construction animation at three quarters, half and none
 * of the build time left; a new building gains hit points each step; on the last signal a morph takes its new type
 * (the hit points it lacked against the new maximum added, at least one) and the building completes with its creep. */
void kp_order_zerg_build_self(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t t = kp_queued_type(g, n), time, hp;
	int32_t s = kp_unit_sprite(g, n);
	int morph = building_morph(t);
	if (!morph) t = u->type[n];
	time = KP_DAT(g, units, build_time, t);
	switch (u->order_state[n]) {
	case 0:
		if (kp_band(u->build_time[n], 0xffff) < kp_shrv(time * 3, 2)) u->order_state[n] = 1;
		break;
	case 1:
		if (u->type[n] == EXTRACTOR) {
			int32_t i = kp_sprite_find_image(g->w, s, IMAGE_GAS_CONSTRUCTION, IMAGE_GAS_CONSTRUCTION); /* 0x416940 */
			if (i != KP_NONE) kp_image_release(g->w, i);                                            /* 0x40fe10 */
		}
		kp_sprite_play(g->w, s, ANIM_SPECIAL1, 1);
		u->order_state[n] = 2;
		break;
	case 2:
		if (kp_band(u->build_time[n], 0xffff) < kp_shrv(time, 1)) u->order_state[n] = 3;
		break;
	case 3:
		kp_sprite_play(g->w, s, ANIM_SPECIAL2, 1);
		u->order_state[n] = 4;
		break;
	case 4:
		if (kp_band(u->build_time[n], 0xffff) == 0) u->order_state[n] = 5;
		break;
	case 5:
		kp_sprite_play(g->w, s, ANIM_ALMOST_BUILT, 1);
		u->order_state[n] = 6;
		kp_construction_voice(g, n);
		break;
	case 6:
		if (kp_band(u->order_signal[n], SIGNAL_BIRTH) == 0) return;
		u->order_signal[n] = kp_band(u->order_signal[n], kp_bnot(SIGNAL_BIRTH));
		if (morph) {
			u->status[n] = kp_bor(u->status[n], STATUS_COMPLETED);
			hp = u->hp[n];
			kp_account_completed(g, n, -1, 0); /* 0x414580 */
			u->status[n] = kp_band(u->status[n], kp_bnot(STATUS_COMPLETED));
			kp_change_unit_type(g, n, t);
			hp = kp_s32v(KP_DAT(g, units, hit_points, t) - KP_DAT(g, units, hit_points, u->previous_type[n]) + hp);
			kp_set_unit_hp(g, n, hp < MIN_MORPH_HP ? MIN_MORPH_HP : hp);
			u->build_time[n] = 0;
		}
		kp_unit_complete_in_place(g, n); /* 0x424da0 */
		kp_creep_restore_provider(g, n);
		kp_sprite_play(g->w, kp_unit_sprite(g, n), ANIM_ALMOST_BUILT, 1); /* a morph's sprite is a new one */
		kp_creep_stamp_building(g, t, sprite_x(g, n), sprite_y(g, n)); /* 0x43c220 */
		return;
	}
	kp_tick_build_timer(g, n); /* 0x41a540 */
	if (!morph) kp_set_unit_hp(g, n, u->hp[n] + kp_build_hp_gain(g, n)); /* AddUnitHp 0x41b900 */
}

/* ExecBuildingMorph 0x473ac0 (command 0x35): the only selected Zerg building queues the morph its gate allows and
 * starts ZergBuildingMorph unless already in it. */
void kp_command_building_morph(KpGame *g, int64_t player, int64_t n, int64_t t)
{
	KpUnits *u = &g->u;
	if (n == 0 || u->type[n] < FIRST_BUILDING_MORPH || u->type[n] > LAST_BUILDING_MORPH || t < FIRST_BUILDING_MORPH
		|| t > LAST_BUILDING_MORPH) {
		return;
	}
	if (kp_can_train_unit(g, t, player, n) != 1) return;
	if (kp_enqueue_unit(g, n, t) && u->order[n] != ORDER_BUILDING_MORPH) kp_issue_order(g, n, ORDER_BUILDING_MORPH);
}

/* OrderZergBuildingMorph 0x43be80 (order 43): the building pays for the queued morph and starts to build itself;
 * without the money it goes back to idle after this order. */
void kp_order_zerg_building_morph(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t t = kp_queued_type(g, n);
	if (!kp_check_unit_cost(g, u->owner[n], t, 1, 1)) {
		kp_order_append(g, n, KP_DAT(g, units, return_to_idle, u->type[n]), 0, 0, 0, NO_TYPE, 0); /* 0x429eb0 */
		return;
	}
	kp_deduct_cost(g, u->owner[n]);
	u->status[n] = kp_band(u->status[n], kp_bnot(STATUS_COMPLETED));
	kp_set_button_set(g, n, BUTTONS_BUILDING_MORPH);
	kp_replace_unit_images(g, n, 1);
	u->build_time[n] = KP_DAT(g, units, build_time, t);
	kp_issue_order(g, n, ORDER_BUILD_SELF);
	kp_sprite_play(g->w, kp_unit_sprite(g, n), ANIM_SPECIAL1, 1);
}

/* OrderBuildNydusExit 0x43bc50 (order 46): the Nydus Canal's exit at the order point, paired with it both ways. */
void kp_order_build_nydus_exit(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t ox = kp_s16v(u->order_x[n]), oy = kp_s16v(u->order_y[n]), tx, ty, e;
	kp_site_tile(g, NYDUS_CANAL, ox, oy, &tx, &ty);
	if (kp_validate_placement(g, n, u->owner[n], tx, ty, NYDUS_CANAL, 0, 1, 0) != 0) {
		kp_worker_error(g, n, STRING_CANNOT_BUILD);
		kp_order_done(g, n);
		return;
	}
	e = kp_unit_create(g, NYDUS_CANAL, ox, oy, u->owner[n]);
	if (e == 0) {
		kp_report_creation_error(g, u->owner[n]); /* 0x425020 */
		kp_order_done(g, n);
		return;
	}
	kp_replace_unit_images(g, e, 1);
	u->hp[e] = kp_udiv64(g, KP_DAT(g, units, hit_points, u->type[e]), HP_SHARE);
	kp_issue_order(g, e, ORDER_BUILD_SELF);
	kp_set_button_set(g, e, BUTTONS_NYDUS_EXIT);
	kp_creep_restore_provider(g, e);
	kp_set_union_unit(g, n, KP_UNION_PEER, e);
	kp_set_union_unit(g, e, KP_UNION_PEER, n);
	kp_order_done(g, n);
}

/* CancelZergBuilding 0x43b380: an unfinished building morph back to the old building (0x43b500), else three quarters
 * of the price back and the Drone again (in the same unit, with its whole hit points from before; out of an
 * Extractor a new one) without the score its production gave. */
void kp_cancel_zerg_building(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t p = u->owner[n], hp, d, old;
	int32_t s = kp_unit_sprite(g, n), c;
	if (kp_band(u->status[n], STATUS_COMPLETED) == 0 && building_morph(kp_queued_type(g, n))) {
		kp_refund_unit_partial(g, p, kp_queued_type(g, n));
		*kp_queue_head(g, n) = NO_TYPE;
		hp = u->hp[n];
		u->status[n] = kp_bor(u->status[n], STATUS_COMPLETED);
		kp_account_completed(g, n, -1, 1);
		u->status[n] = kp_band(u->status[n], kp_bnot(STATUS_COMPLETED));
		kp_unit_complete_in_place(g, n);
		kp_set_unit_hp(g, n, hp);
		kp_play_unit_sound(g, SOUND_ZERG_CANCEL, n);
		kp_sprite_play(g->w, s, ANIM_ALMOST_BUILT, 1);
		return;
	}
	hp = kp_shlv(kp_band(u->previous_hp[n], 0xffff), 8);
	kp_refund_unit_partial(g, p, u->type[n]); /* 0x408490 */
	kp_unscore_produced(g, p, DRONE);
	if (u->type[n] == EXTRACTOR) {
		d = kp_unit_create(g, DRONE, sprite_x(g, n), sprite_y(g, n), p);
		if (d == 0) {
			kp_report_creation_error(g, p);
		} else {
			kp_unit_complete_in_place(g, d);
			kp_set_unit_hp(g, d, hp);
		}
		kp_unit_kill(g, n); /* 0x42a4d0 */
		return;
	}
	c = kp_lone_create(g->w, SPRITE_ZERG_CANCEL, (int32_t)sprite_x(g, n), (int32_t)sprite_y(g, n), 0); /* 0x417b80 */
	if (c != KP_NONE) {
		g->w->spr.elevation[g->w->thg.sprite[c]] = (g->w->spr.elevation[s] + 1) & 0xff;
		kp_thingy_visibility(g->w, c); /* 0x417e50 */
	}
	kp_play_unit_sound(g, SOUND_ZERG_CANCEL, n);
	old = u->type[n];
	kp_change_unit_type(g, n, DRONE);
	town_ai(g, n, "cancelled building back to a Drone (0x487900)");
	kp_unit_complete_in_place(g, n);
	s = kp_unit_sprite(g, n);
	kp_sprite_set_shadow_offset(g->w, s, SHADOW_LANDED);
	kp_sprite_refresh_bounds(g->w, s);
	kp_issue_order(g, n, ORDER_RESET_COLLISION);
	kp_order_append(g, n, KP_DAT(g, units, return_to_idle, DRONE), 0, 0, 0, NO_TYPE, 0);
	kp_set_unit_hp(g, n, hp);
	/* RemoveCreepProviderAt 0x4b46e0 (completed 0); with the pool full the creep stays */
	kp_creep_remove_provider(g, old, sprite_x(g, n), sprite_y(g, n), 0);
}
