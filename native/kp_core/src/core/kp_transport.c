/* Transports and Bunkers (kp_game.h), ported from the atlas (P12; docs/research/native-transport.md): the boarding
 * test 0x437440, the orders Enter 92 (0x4376d0), PickupIdle 93 (0x437b50), Pickup 94 (0x4377a0), Unload 111
 * (0x437ba0) and MoveUnload 112 (0x437890), loading 0x437c70, unloading 0x437e00, the detach 0x437320, the cargo
 * at a death (0x425d10) and the muzzle flash of a unit firing from a Bunker (0x420520). */
#include "kp_game_internal.h"

#define CARGO_SLOTS 8
#define NO_TYPE 228
#define DAT_BUILDING 0x1
#define DAT_SUBUNIT 0x10
#define DAT_CAN_MOVE 0x8000000 /* units.dat: the status bit 0x20000 it restores (0x4249e0) */
#define STATUS_COMPLETED 0x1
#define STATUS_GROUNDED_BUILDING 0x2
#define STATUS_BURROWED 0x10
#define STATUS_IN_BUILDING 0x20
#define STATUS_IN_TRANSPORT 0x40
#define STATUS_DISABLED 0x400
#define STATUS_CAN_MOVE 0x20000
#define STATUS_HALLUCINATION 0x40000000
#define ORDER_ENTER 0x5c
#define ORDER_PICKUP_IDLE 0x5d
#define ORDER_PICKUP 0x5e
#define ORDER_AI_TRANSPORT 0x60
#define ORDER_UNLOAD 0x6f
#define ORDER_COMPUTER_AI 0x9c
#define PICKUP_PERIOD 7
#define UNLOAD_DELAY 15
#define UNLOAD_COOLDOWN 30
#define UNLOAD_RANGE 16
#define RACE_TERRAN 1
#define LOAD_SOUND 40
#define UNLOAD_SOUND 43
#define ANIM_WALKING_TO_IDLE 12
#define REAVER 83
#define BUNKER 125
#define SCV 7
#define NO_WEAPON 130
#define FIREBAT 0x20
#define GUI_MONTAG 0x0a
#define FLASH 377
#define FLAME_FLASH 378

static int64_t flags(KpGame *g, int64_t n) { return KP_DAT(g, units, special_ability_flags, g->u.type[n]); }

static int64_t space_required(KpGame *g, int64_t n) { return KP_DAT(g, units, space_required, g->u.type[n]); }

static int64_t space_provided(KpGame *g, int64_t n) { return KP_DAT(g, units, space_provided, g->u.type[n]); }

static int subunit(KpGame *g, int64_t n)
{
	int64_t sub = g->u.subunit[n];
	return sub != 0 && kp_band(flags(g, sub), DAT_SUBUNIT) != 0 ? (int)sub : 0;
}

/* 0x4249e0: the status bit 0x20000 again from units.dat. */
static void restore_can_move(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	u->status[n] = kp_bxor(u->status[n], kp_band(kp_bxor(kp_shrv(flags(g, n), 10), u->status[n]), STATUS_CAN_MOVE));
}

static int disabled(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	return kp_band(u->status[n], STATUS_DISABLED) != 0 || u->lockdown_timer[n] != 0 || u->stasis_timer[n] != 0
		|| u->maelstrom_timer[n] != 0;
}

/* PrependOrNewOrderOnUnit 0x429bc0 with ActivateNextOrder 0x42a610. */
static void prepend_on_unit(KpGame *g, int64_t n, int64_t order, int64_t target)
{
	int32_t s = kp_unit_sprite(g, target);
	int64_t x = g->w->spr.x[s], y = g->w->spr.y[s], head = g->u.queue_head[n];
	if (head != KP_NONE) kp_order_insert_before(g, n, order, x, y, target, NO_TYPE, head);
	else kp_order_queue(g, n, order, x, y, target, NO_TYPE);
	kp_order_activate(g, n);
}

/* PrependOrNewOrder 0x429b20 with ActivateNextOrder 0x42a610. */
static void prepend(KpGame *g, int64_t n, int64_t order)
{
	int64_t head = g->u.queue_head[n];
	if (head != KP_NONE) kp_order_insert_before(g, n, order, 0, 0, 0, NO_TYPE, head);
	else kp_order_queue(g, n, order, 0, 0, 0, NO_TYPE);
	kp_order_activate(g, n);
}

/* CanLoadIntoTransport 0x437440: passenger p fits into transport t now. */
int kp_can_load(KpGame *g, int64_t p, int64_t t)
{
	KpUnits *u = &g->u;
	int64_t provided, used = 0;
	int k;
	if (kp_band(u->status[t], STATUS_COMPLETED) == 0 || disabled(g, t) || kp_band(u->status[p], STATUS_BURROWED) != 0
		|| u->owner[p] != u->owner[t]) {
		return 0;
	}
	if (kp_band(flags(g, t), DAT_BUILDING) != 0 && (kp_unit_race(g, p) != RACE_TERRAN || space_required(g, p) > 1)) return 0;
	provided = space_provided(g, t);
	for (k = 0; k < provided && k < CARGO_SLOTS; k++) {
		int64_t c = kp_cargo_unit(g, t, k);
		if (c != 0) used += space_required(g, c);
	}
	return kp_u32v(provided - used) >= kp_u32v(space_required(g, p));
}

/* LoadUnit 0x437c70: passenger p boards transport t. */
static void load(KpGame *g, int64_t p, int64_t t)
{
	KpUnits *u = &g->u;
	int64_t provided = space_provided(g, t), sub;
	int k = 0;
	int32_t ts;
	while (k < provided && kp_cargo_unit(g, t, k) != 0) k++;
	kp_play_unit_sound(g, LOAD_SOUND + kp_unit_race(g, t), t);
	/* ClearTownRepairScv 0x484f10: only the towns of the AI scripts (plan phase 7) have repair SCVs */
	if (u->type[p] == SCV && u->ai[p] != 0 && kp_ai_unit_record_type(g, p) != 1) {
		kp_game_fail(g, "transport: a town's repair SCV boards (0x484f10)");
	}
	u->connected[p] = t;
	kp_cargo_slots(g, k)[t] = kp_unit_ref(g, p);
	u->status[p] = kp_bor(u->status[p], STATUS_IN_TRANSPORT);
	kp_unit_hide(g, p);
	kp_sprite_play(g->w, kp_unit_sprite(g, p), ANIM_WALKING_TO_IDLE, 1);
	/* the interface refresh 0x4a7ed0 is the presentation's */
	if (kp_band(u->status[t], STATUS_GROUNDED_BUILDING) == 0) return;
	ts = kp_unit_sprite(g, t);
	u->status[p] = kp_bor(kp_band(u->status[p], kp_bnot(STATUS_CAN_MOVE)), STATUS_IN_BUILDING);
	kp_reset_movement(g, p);
	sub = subunit(g, p);
	if (sub != 0) {
		kp_unit_relocate(g, sub, kp_mod(g->w->spr.x[ts], 65536), kp_mod(g->w->spr.y[ts], 65536));
		u->status[sub] = kp_bor(kp_band(u->status[sub], kp_bnot(STATUS_CAN_MOVE)), STATUS_IN_BUILDING);
		kp_reset_movement(g, sub);
		return;
	}
	kp_unit_relocate(g, p, kp_mod(g->w->spr.x[ts], 65536), kp_mod(g->w->spr.y[ts], 65536));
}

/* DetachFromTransport 0x437320: p leaves its transport (at a death, without showing it). */
void kp_detach_from_transport(KpGame *g, int64_t p, int death)
{
	KpUnits *u = &g->u;
	int64_t sub;
	if (kp_band(u->status[p], STATUS_IN_TRANSPORT) == 0) return;
	if (death && u->connected[p] == 0) {
		u->status[p] = kp_band(u->status[p], kp_bnot(STATUS_IN_TRANSPORT | STATUS_IN_BUILDING));
		restore_can_move(g, p);
		kp_reset_movement(g, p);
	} else {
		int64_t t = u->connected[p], ref = kp_unit_ref(g, p), provided = space_provided(g, t);
		int k = 0;
		while (k < provided && kp_cargo_slots(g, k)[t] != ref) k++;
		if (k >= CARGO_SLOTS) kp_game_fail(g, "transport: a passenger missing from its transport (0x437320)");
		if (!death) {
			kp_play_unit_sound(g, UNLOAD_SOUND + kp_unit_race(g, t), t);
			kp_unit_show(g, p);
		} else {
			int32_t ts = kp_unit_sprite(g, t);
			kp_unit_relocate(g, p, kp_mod(g->w->spr.x[ts], 65536), kp_mod(g->w->spr.y[ts], 65536));
		}
		kp_cargo_slots(g, k)[t] = 0;
		u->status[p] = kp_band(u->status[p], kp_bnot(STATUS_IN_TRANSPORT | STATUS_IN_BUILDING));
		u->connected[p] = 0;
		restore_can_move(g, p);
		kp_reset_movement(g, p);
	}
	sub = subunit(g, p);
	if (sub != 0) {
		u->status[sub] = kp_band(u->status[sub], kp_bnot(STATUS_IN_BUILDING));
		restore_can_move(g, sub);
		kp_reset_movement(g, sub);
	}
}

/* UnloadPassenger 0x437e00: 1 when p left its transport for a free place next to it. */
int kp_unload_unit(KpGame *g, int64_t p)
{
	KpUnits *u = &g->u;
	int64_t t = u->connected[p], x, y, w;
	int building;
	int32_t ts;
	if (t == 0) return 0;
	building = kp_band(u->status[t], STATUS_GROUNDED_BUILDING) != 0;
	if ((u->order_timer[t] != 0 && !building) || disabled(g, t)) return 0;
	ts = kp_unit_sprite(g, t);
	x = kp_mod(g->w->spr.x[ts], 65536);
	y = kp_mod(g->w->spr.y[ts], 65536);
	kp_unit_relocate(g, p, x, y);
	if (!kp_find_unit_placement(g, p, kp_s16v(x), kp_s16v(y), NULL, 0, 0, &x, &y)) return 0;
	u->order_timer[t] = UNLOAD_DELAY;
	kp_unit_relocate(g, p, x, y);
	if (u->subunit[p] != 0) kp_unit_relocate(g, u->subunit[p], x, y);
	kp_reset_to_idle_animation(g, p);
	kp_detach_from_transport(g, p, 0);
	kp_issue_order(g, p, u->ai[p] != 0 ? ORDER_COMPUTER_AI : KP_DAT(g, units, return_to_idle, u->type[p]));
	if (building) return 1;
	if (u->type[p] == REAVER) {
		u->order_timer[p] = UNLOAD_COOLDOWN;
		return 1;
	}
	w = kp_ground_weapon(g, p);
	if (w != NO_WEAPON) u->ground_cooldown[p] = kp_mod(kp_weapon_cooldown(g, p, w), 256);
	w = KP_DAT(g, units, air_weapon, u->type[p]);
	if (w != NO_WEAPON) u->air_cooldown[p] = kp_mod(kp_weapon_cooldown(g, p, w), 256);
	u->spell_cooldown[p] = UNLOAD_COOLDOWN;
	return 1;
}

/* The cargo when a unit is removed (0x425d10): a passenger leaves its transport; a transport's passengers die, a
 * Bunker's get out where they can. */
void kp_cargo_at_removal(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int k;
	if (kp_band(u->status[n], STATUS_IN_TRANSPORT) != 0) {
		kp_detach_from_transport(g, n, 1);
		return;
	}
	if (!kp_is_transport(g, n)) return;
	for (k = 0; k < CARGO_SLOTS; k++) {
		int64_t c = kp_cargo_unit(g, n, k);
		if (c == 0 || u->sprite[c] == KP_NONE || u->order[c] == 0) continue; /* IsUnitInactive 0x41fe40 */
		if (u->type[n] == BUNKER && kp_unload_unit(g, c)) continue;
		kp_unit_kill(g, c);
	}
	for (k = 0; k < CARGO_SLOTS; k++) kp_cargo_slots(g, k)[n] = 0;
}

/* Order_EnterTransport 0x4376d0 (order 92): the passenger walks to its transport and boards it on contact. */
void kp_order_enter_transport(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t t = u->order_unit[n];
	int32_t s;
	if (t == 0 || !kp_is_transport(g, t) || !kp_can_load(g, n, t)) {
		kp_order_done(g, n);
		return;
	}
	if (u->order_state[n] == 0 && u->order[t] != ORDER_PICKUP) {
		if (kp_band(u->status[t], STATUS_CAN_MOVE) != 0) prepend_on_unit(g, t, ORDER_PICKUP, n);
		u->order_state[n] = 1;
	}
	s = kp_unit_sprite(g, t);
	kp_flingy_set_target_waypoint(g, n, kp_s16v(g->w->spr.x[s]), kp_s16v(g->w->spr.y[s]));
	kp_move_toward_target(g, n, t);
	if (kp_unit_box_within(g, n, 1, t)) load(g, n, t);
}

/* Order_Pickup 0x4377a0 (order 94): the transport goes to its passenger and has it board. */
void kp_order_pickup(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t p = u->order_unit[n];
	if (p == 0 || (u->ai[p] != 0 && u->order[p] != ORDER_AI_TRANSPORT)) {
		kp_order_done(g, n);
		return;
	}
	if (u->order_state[n] != 0) return;
	if (!kp_can_load(g, p, n)) {
		kp_order_done(g, n);
		return;
	}
	if (u->order_timer[n] != 0) return;
	u->order_timer[n] = PICKUP_PERIOD;
	if (!kp_unit_box_within(g, n, 1, p)) {
		kp_request_move_to_unit(g, n, p);
		return;
	}
	kp_stop_and_face(g, n);
	if (u->order[p] != ORDER_ENTER && u->order[p] != ORDER_AI_TRANSPORT) kp_issue_order_target(g, p, ORDER_ENTER, n);
	if (u->ai[n] != 0) {
		u->order_state[n] = 1;
		return;
	}
	prepend_on_unit(g, n, ORDER_PICKUP_IDLE, p);
}

/* Order_PickupIdle 0x437b50 (order 93). */
void kp_order_pickup_idle(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t p = u->order_unit[n];
	if (p != 0 && kp_can_load(g, p, n)) {
		prepend_on_unit(g, n, ORDER_PICKUP, p);
		return;
	}
	if (u->ai[n] != 0) kp_issue_order(g, n, ORDER_COMPUTER_AI);
}

/* Order_MoveUnload 0x437890 (order 112): the transport flies to the point and unloads there. */
void kp_order_move_unload(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	if (u->order_state[n] == 0 && u->order_unit[n] != 0) {
		int32_t s = kp_unit_sprite(g, u->order_unit[n]);
		u->order_state[n] = 1;
		u->order_x[n] = kp_s16v(g->w->spr.x[s]);
		u->order_y[n] = kp_s16v(g->w->spr.y[s]);
		u->order_unit[n] = 0;
	}
	if (kp_within_distance(g, n, UNLOAD_RANGE, kp_s16v(u->order_x[n]), kp_s16v(u->order_y[n]))) {
		prepend(g, n, ORDER_UNLOAD);
		return;
	}
	kp_request_move_to_point(g, n, kp_s16v(u->order_x[n]), kp_s16v(u->order_y[n]));
	if (u->ai[n] != 0) kp_game_fail(g, "transport: the computer's unload on the way (0x437902)");
}

/* Order_Unload 0x437ba0 (order 111): one passenger at a time (all at once from a building). */
void kp_order_unload(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int building = kp_band(u->status[n], STATUS_GROUNDED_BUILDING) != 0, any = 0, done = 0, k;
	if (!building && u->order_timer[n] != 0) return;
	for (k = 0; k < CARGO_SLOTS; k++) {
		int64_t c = kp_cargo_unit(g, n, k);
		if (c == 0) continue;
		any = 1;
		done = kp_unload_unit(g, c);
		if (!building) break;
	}
	if (any && !done && u->ai[n] != 0 && !building) {
		kp_game_fail(g, "transport: the computer chooses where to unload (0x4af810)");
	}
	if (any && done && !building) {
		for (k = 0; k < CARGO_SLOTS; k++) {
			if (kp_cargo_unit(g, n, k) != 0) return;
		}
	}
	kp_order_done(g, n);
}

/* CreateBunkerMuzzleFlash 0x420520: the flash of unit n firing from its Bunker. */
void kp_bunker_muzzle_flash(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	KpImages *w = g->w;
	int64_t heading = kp_mod(u->heading[n], 256), k8 = kp_band(kp_sarv(heading + 16, 5), 7), id, dir;
	int32_t s = kp_unit_sprite(g, n), bunker = kp_unit_sprite(g, u->connected[n]), ox, oy, t;
	kp_images_lo_offset(w, kp_check_image(w, w->spr.main[bunker]), 0, (int32_t)k8, &ox, &oy);
	if (u->type[n] == FIREBAT || u->type[n] == GUI_MONTAG) {
		id = FLAME_FLASH;
		dir = kp_band(kp_sarv(heading + 8, 4), 15) << 4;
	} else {
		id = FLASH;
		dir = k8 << 5;
	}
	t = kp_lone_create(w, (int32_t)id, (int32_t)(kp_s16v(w->spr.x[s]) + ox), (int32_t)(kp_s16v(w->spr.y[s]) + oy), 0);
	if (t == KP_NONE) return;
	w->spr.elevation[w->thg.sprite[t]] = w->spr.elevation[s] + 1;
	kp_sprite_set_facing(w, w->thg.sprite[t], (int32_t)dir);
	kp_thingy_visibility(w, t);
}
