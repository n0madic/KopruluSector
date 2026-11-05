/* Transports and Bunkers (kp_game.h), ported from the atlas (P12; docs/research/native-transport.md): the boarding
 * test 0x437440, the orders Enter 92 (0x4376d0), PickupIdle 93 (0x437b50), Pickup 94 (0x4377a0), PickupBunker 95
 * (0x437850), Unload 111 (0x437ba0) and MoveUnload 112 (0x437890), loading 0x437c70, unloading 0x437e00, the detach 0x437320, the cargo
 * at a death (0x425d10) and the muzzle flash of a unit firing from a Bunker (0x420520); Nydus Canals: the entry test
 * 0x43c360, EnterNydusCanal 47 (0x43ccc0) and the travel 0x43d290. */
#include "kp_game_internal.h"
#include "kp_terrain.h"

#define CARGO_SLOTS 8
#define DAT_WORKER 0x8
#define DAT_BUILDING 0x1
#define DAT_SUBUNIT 0x10
#define DAT_CAN_MOVE 0x8000000 /* units.dat: the status bit 0x20000 it restores (0x4249e0) */
#define STATUS_COMPLETED 0x1
#define STATUS_GROUNDED_BUILDING 0x2
#define STATUS_AIR 0x4
#define STATUS_BURROWED 0x10
#define STATUS_IN_BUILDING 0x20
#define STATUS_IN_TRANSPORT 0x40
#define STATUS_DISABLED 0x400
#define STATUS_CAN_MOVE 0x20000
#define STATUS_HALLUCINATION 0x40000000
#define PICKUP_PERIOD 7
#define UNLOAD_DELAY 15
#define UNLOAD_COOLDOWN 30
#define UNLOAD_RANGE 16
#define LOAD_SOUND 40
#define UNLOAD_SOUND 43
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
	kp_order_prepend_target(g, n, order, target);
	kp_order_activate(g, n);
}

/* PrependOrNewOrder 0x429b20 with ActivateNextOrder 0x42a610. */
static void prepend(KpGame *g, int64_t n, int64_t order)
{
	kp_order_prepend(g, n, order);
	kp_order_activate(g, n);
}

/* GetUsedCargoSpace 0x438350: the space its passengers of all the slots take. */
int64_t kp_cargo_space(KpGame *g, int64_t n)
{
	int64_t used = 0;
	int k;
	for (k = 0; k < CARGO_SLOTS; k++) {
		int64_t c = kp_cargo_unit(g, n, k);
		if (c != 0) used += space_required(g, c);
	}
	return used;
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
	if (kp_band(flags(g, t), DAT_BUILDING) != 0 && (kp_unit_race(g, p) != KP_RACE_TERRAN || space_required(g, p) > 1)) return 0;
	provided = space_provided(g, t);
	for (k = 0; k < provided && k < CARGO_SLOTS; k++) {
		int64_t c = kp_cargo_unit(g, t, k);
		if (c != 0) used += space_required(g, c);
	}
	return kp_u32v(provided - used) >= kp_u32v(space_required(g, p));
}

static void load(KpGame *g, int64_t p, int64_t t);


/* OrderTransportRequest 0x437a00 (order 96): an AI passenger waits for a transport; next to it it boards (the
 * transport takes it to its order point; an AI transport then comes back), else the transport comes to pick it up. */
void kp_order_transport_request(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t t = u->order_unit[n];
	int32_t s;
	if (u->owner[n] == KP_PLAYER_NEUTRAL) {
		kp_issue_order(g, n, kp_band(u->status[n], STATUS_IN_TRANSPORT) != 0 ? (int64_t)KP_ORD_NOTHING
			: KP_DAT(g, units, return_to_idle, u->type[n]));
		return;
	}
	if (kp_band(u->status[n], STATUS_IN_TRANSPORT) != 0) return;
	if (kp_ai_update_attack_target(g, n, 0, 0, 1)) return; /* AiUpdateAttackTarget 0x41f6c0 */
	if (t == 0) {
		if (!kp_ai_find_transport(g, n, &u->order_unit[n])) { /* 0x437f80 */
			kp_order_done(g, n);
			return;
		}
		t = u->order_unit[n];
		if (t == 0) return;
	} else if (!kp_is_transport(g, t) || u->order_unit[t] != n) { /* IsTransport 0x4381f0 */
		u->order_unit[n] = 0;
		return;
	}
	if (!kp_can_load(g, n, t)) {
		u->order_unit[n] = 0;
		return;
	}
	if (kp_unit_box_within(g, n, 1, t)) { /* IsUnitWithinRange 0x4229a0 */
		load(g, n, t);
		kp_issue_order_at(g, t, KP_ORD_MOVE_UNLOAD, kp_s16v(u->order_x[n]), kp_s16v(u->order_y[n]));
		if (u->ai[t] != 0) { /* QueueOrderAtPoint 0x429ee0 */
			s = kp_unit_sprite(g, t);
			kp_order_append(g, t, KP_ORD_MOVE, kp_s16v(g->w->spr.x[s]), kp_s16v(g->w->spr.y[s]), 0, KP_TYPE_NONE, 0);
		}
		return;
	}
	prepend_on_unit(g, t, KP_ORD_PICKUP_TRANSPORT, n);
	s = kp_unit_sprite(g, t);
	if (!kp_ai_can_reach_point(g, n, kp_s16v(g->w->spr.x[s]), kp_s16v(g->w->spr.y[s]))) return; /* 0x4ab450 */
	kp_flingy_set_target_waypoint(g, n, kp_s16v(g->w->spr.x[s]), kp_s16v(g->w->spr.y[s])); /* 0x40cbd0 */
	kp_move_toward_target(g, n, t); /* MoveTowardTarget 0x438ba0 */
}

/* LoadUnit 0x437c70: passenger p boards transport t. */
static void load(KpGame *g, int64_t p, int64_t t)
{
	KpUnits *u = &g->u;
	int64_t provided = space_provided(g, t), sub;
	int k = 0;
	int32_t ts;
	while (k < provided && kp_cargo_unit(g, t, k) != 0) k++;
	if (k >= provided) kp_game_fail(g, "transport: boarding a full transport (0x437c70)");
	kp_play_unit_sound(g, LOAD_SOUND + kp_unit_race(g, t), t);
	kp_ai_clear_repair_scv(g, p); /* ClearTownRepairScv 0x484f10 */
	u->connected[p] = t;
	kp_cargo_slots(g, k)[t] = kp_unit_ref(g, p);
	u->status[p] = kp_bor(u->status[p], STATUS_IN_TRANSPORT);
	kp_unit_hide(g, p);
	kp_sprite_play(g->w, kp_unit_sprite(g, p), KP_ANIM_WALKING_TO_IDLE, 1);
	g->interface_refresh++; /* RefreshStatusUi 0x4a7ed0, for the interface */
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
		if (k >= provided) kp_game_fail(g, "transport: a passenger missing from its transport (0x437320)");
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
	if (!kp_find_unit_placement(g, p, kp_s16v(x), kp_s16v(y), NULL, 0, &x, &y)) return 0;
	u->order_timer[t] = UNLOAD_DELAY;
	kp_unit_relocate(g, p, x, y);
	if (u->subunit[p] != 0) kp_unit_relocate(g, u->subunit[p], x, y);
	kp_reset_to_idle_animation(g, p);
	kp_detach_from_transport(g, p, 0);
	kp_issue_order(g, p, u->ai[p] != 0 ? (int64_t)KP_ORD_COMPUTER_AI : KP_DAT(g, units, return_to_idle, u->type[p]));
	g->interface_refresh++; /* RefreshStatusUi 0x4a7ed0, for the interface */
	if (building) return 1;
	if (u->type[p] == KP_TYPE_REAVER) {
		u->order_timer[p] = UNLOAD_COOLDOWN;
		return 1;
	}
	w = kp_ground_weapon(g, p);
	if (w != KP_WEAPON_NONE) u->ground_cooldown[p] = kp_mod(kp_weapon_cooldown(g, p, w), 256);
	w = KP_DAT(g, units, air_weapon, u->type[p]);
	if (w != KP_WEAPON_NONE) u->air_cooldown[p] = kp_mod(kp_weapon_cooldown(g, p, w), 256);
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
		if (c == 0 || u->sprite[c] == KP_NONE || u->order[c] == KP_ORD_DIE) continue; /* IsUnitInactive 0x41fe40 */
		if (u->type[n] == KP_TYPE_BUNKER && kp_unload_unit(g, c)) continue;
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
	if (u->order_state[n] == 0 && u->order[t] != KP_ORD_PICKUP_TRANSPORT) {
		if (kp_band(u->status[t], STATUS_CAN_MOVE) != 0) prepend_on_unit(g, t, KP_ORD_PICKUP_TRANSPORT, n);
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
	if (p == 0 || (u->ai[p] != 0 && u->order[p] != KP_ORD_TRANSPORT_REQUEST)) {
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
	if (u->order[p] != KP_ORD_ENTER_TRANSPORT && u->order[p] != KP_ORD_TRANSPORT_REQUEST) kp_issue_order_target(g, p, KP_ORD_ENTER_TRANSPORT, n);
	if (u->ai[n] != 0) {
		u->order_state[n] = 1;
		return;
	}
	prepend_on_unit(g, n, KP_ORD_PICKUP_IDLE, p);
}

/* Order_PickupBunker 0x437850 (order 95, the Bunker's Load button): the Bunker has the unit it was told to load enter
 * it, if the unit fits, and the order ends. */
void kp_order_pickup_bunker(KpGame *g, int64_t n)
{
	int64_t p = g->u.order_unit[n];
	if (p != 0 && kp_can_load(g, p, n)) kp_issue_order_target(g, p, KP_ORD_ENTER_TRANSPORT, n); /* 0x42a380 */
	kp_order_done(g, n);
}

/* Order_PickupIdle 0x437b50 (order 93). */
void kp_order_pickup_idle(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t p = u->order_unit[n];
	if (p != 0 && kp_can_load(g, p, n)) {
		prepend_on_unit(g, n, KP_ORD_PICKUP_TRANSPORT, p);
		return;
	}
	if (u->ai[n] != 0) kp_issue_order(g, n, KP_ORD_COMPUTER_AI);
}

#define AI_UNLOAD_REACH 0xca0
#define AI_UNLOAD_NEAR 0x200

/* FindUnloadPoint 0x438180: passenger p, put at transport t's position, finds a place to stand. */
static int unload_point(KpGame *g, int64_t t, int64_t p)
{
	int32_t ts = kp_unit_sprite(g, t);
	int64_t x = kp_mod(g->w->spr.x[ts], 65536), y = kp_mod(g->w->spr.y[ts], 65536);
	kp_unit_relocate(g, p, x, y); /* 0x43a270 */
	return kp_find_unit_placement(g, p, kp_s16v(x), kp_s16v(y), NULL, 0, &x, &y); /* 0x47dc30 */
}

/* The computer's transport unloads on the way (0x437902): at the point's height, within 3232 of it, the point within
 * its reach, without a worker aboard, and every passenger with a place (a Reaver only within 512). */
static int ai_unload_early(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int32_t s = kp_unit_sprite(g, n);
	int64_t x = kp_s16v(u->order_x[n]), y = kp_s16v(u->order_y[n]), k;
	int near;
	if (kp_terrain_height(g, kp_s16v(g->w->spr.x[s]), kp_s16v(g->w->spr.y[s])) != kp_terrain_height(g, x, y)) return 0;
	if (!kp_within_distance(g, n, AI_UNLOAD_REACH, x, y) || !kp_ai_can_reach_point(g, n, x, y)) return 0;
	for (k = 0; k < CARGO_SLOTS; k++) { /* CargoHasWorker 0x438390 */
		int64_t c = kp_cargo_unit(g, n, (int)k);
		if (c != 0 && kp_band(flags(g, c), DAT_WORKER) != 0) return 0;
	}
	near = kp_within_distance(g, n, AI_UNLOAD_NEAR, x, y);
	for (k = 0; k < CARGO_SLOTS; k++) {
		int64_t c = kp_cargo_unit(g, n, (int)k);
		if (c == 0) continue;
		if (u->type[c] == KP_TYPE_REAVER && !near) return 0;
		if (!unload_point(g, n, c)) return 0;
	}
	return 1;
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
		prepend(g, n, KP_ORD_UNLOAD);
		return;
	}
	kp_request_move_to_point(g, n, kp_s16v(u->order_x[n]), kp_s16v(u->order_y[n]));
	if (u->ai[n] != 0 && ai_unload_early(g, n)) prepend(g, n, KP_ORD_UNLOAD);
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
		kp_ai_choose_unload(g, n); /* ChooseUnloadDestination 0x4af810 */
		return;
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
	if (u->type[n] == KP_TYPE_FIREBAT || u->type[n] == KP_TYPE_GUI_MONTAG) {
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

#define NYDUS_SOUND 19

/* CanEnterNydusCanal 0x43c360. */
int kp_can_enter_nydus(KpGame *g, int64_t n, int64_t canal)
{
	KpUnits *u = &g->u;
	int64_t exit;
	if (canal == 0 || kp_band(u->status[canal], STATUS_COMPLETED) == 0) return 0;
	exit = u->union_0xd0[canal];
	return exit != 0 && kp_band(u->status[exit], STATUS_COMPLETED) != 0 && u->owner[n] == u->owner[canal]
		&& kp_band(u->status[n], STATUS_AIR) == 0 && kp_unit_race(g, n) == KP_RACE_ZERG;
}

/* NydusTravel 0x43d290: n comes out next to the canal's exit, or stays where it was when no place fits. */
static void nydus_travel(KpGame *g, int64_t n, int64_t canal)
{
	KpImages *w = g->w;
	int64_t exit = g->u.union_0xd0[canal], x, y;
	int32_t s = kp_unit_sprite(g, n), es = kp_unit_sprite(g, exit);
	int64_t ox = kp_s16v(w->spr.x[s]), oy = kp_s16v(w->spr.y[s]), ex = kp_s16v(w->spr.x[es]), ey = kp_s16v(w->spr.y[es]);
	kp_unit_detach(g, n, 1);
	kp_unit_relocate(g, n, ex, ey);
	if (!kp_find_unit_placement(g, n, ex, ey, NULL, 0, &x, &y)) {
		kp_unit_relocate(g, n, ox, oy);
		kp_unit_reattach(g, n);
		return;
	}
	kp_play_unit_sound(g, NYDUS_SOUND, canal);
	kp_unit_relocate(g, n, x, y);
	kp_unit_reattach(g, n);
	kp_play_unit_sound(g, NYDUS_SOUND, exit);
	kp_order_done(g, n);
}

/* EnterNydusCanal 0x43ccc0 (order 47). */
void kp_order_enter_nydus(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t canal = u->order_unit[n];
	if (!kp_can_enter_nydus(g, n, canal)) {
		kp_order_done(g, n);
		return;
	}
	switch (u->order_state[n]) {
	case 0:
		if (kp_request_move_to_unit(g, n, canal)) u->order_state[n] = 1;
		return;
	case 1:
		switch (kp_move_arrival(g, n)) {
		case 0: return;
		case 1: break;
		default:
			kp_order_done(g, n);
			return;
		}
		u->order_state[n] = 2;
		/* fall through */
	case 2:
		nydus_travel(g, n, canal);
		return;
	default:
		return;
	}
}
