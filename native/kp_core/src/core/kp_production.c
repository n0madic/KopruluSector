/* Production (kp_game.h), ported from the atlas (P02, P05, P06, P15; docs/research/native-training.md,
 * native-training-lifecycle.md): the price cache and the checks of supply and stock (CheckSupplyForUnit 0x4084d0,
 * CheckCachedCostAgainstStock 0x4085e0, CheckUnitCost 0x4086b0), the debit and the refunds (0x408650, 0x408460),
 * the five-slot queue (EnqueueUnit 0x41b4b0, CancelQueuedAt 0x41b810, CancelLastQueued 0x41b6f0, CancelAllQueued
 * 0x41b5c0), the secondary order Train 38 (0x41b060) with the creation of the queued unit (0x41bc30), its build
 * progress (0x41a280, 0x41b290), the cancel of an unfinished unit (CancelConstruction 0x41a340), the rally
 * (SendToRally 0x41bd70) and the ready voice (0x446650).
 *
 * The queue is the words queue0..queue4 (+0x98) with the head byte queue_slot (+0xa4); 228 is an empty slot. The
 * producer's current child is build_unit (+0xec) and its Train state secondary_state (+0xe2). */
#include "kp_game_internal.h"

#define NO_TYPE 228
#define QUEUE_SLOTS 5
#define DAT_BUILDING 0x1
#define DAT_DOUBLE_SUPPLY 0x400
#define DAT_NO_SUPPLY 0x1000000
#define STATUS_COMPLETED 0x1
#define STATUS_GROUNDED_BUILDING 0x2
#define CHEAT_CWAL 0x2
#define CHEAT_FOOD 0x20000
#define CWAL_FACTOR 16
#define LURKER 103
#define HYDRALISK 38
#define MUTALISK 43
#define EGG 36
#define COCOON 59
#define LURKER_EGG 97
#define NUCLEAR_MISSILE 14
#define INTERCEPTOR 73
#define SCARAB 85
#define GUARDIAN 44
#define DEVOURER 62
#define NYDUS_CANAL 134
#define INFESTED_COMMAND_CENTER 130
#define CARRIER 72
#define GANTRITHOR 82
#define REAVER 83
#define WARBRINGER 81
#define RACE_ZERG 0
#define SIGNAL_BIRTH 0x4
#define ANIM_SPECIAL2 14
#define ORDER_ZERG_BIRTH 41
#define CONTROLLER_COMPUTER 1 /* the runtime player type */
#define RACE_TERRAN 1
#define ORDER_FOLLOW 49
#define ORDER_MOVE 6
#define SECONDARY_NOTHING 23
#define ANIM_IS_WORKING 19
#define ANIM_WORKING_TO_IDLE 20
#define STRING_SUPPLY_CAP 0x34f   /* + race: the supply cap is reached */
#define STRING_SUPPLY 0x34c       /* + race: more supply is needed */
#define STRING_MINERALS 0x352
#define STRING_GAS 0x353
#define SOUND_SUPPLY_CAP 1        /* + race */
#define SOUND_SUPPLY 0x99         /* + race */
#define SOUND_MINERALS 0x93       /* + the local player's race */
#define SOUND_GAS 0x96

static int64_t dat_flags(KpGame *g, int64_t t) { return KP_DAT(g, units, special_ability_flags, t); }

static int64_t *slot_of(KpGame *g, int64_t n, int64_t slot) { return kp_queue_entry(g, n, kp_mod(slot, QUEUE_SLOTS)); }
static int64_t queued_at(KpGame *g, int64_t n, int64_t slot) { return kp_band(*slot_of(g, n, slot), 0xffff); }

static void check_player(KpGame *g, int64_t p)
{
	if (p < 0 || p >= KP_PLAYERS) kp_game_fail(g, "production: player %lld", (long long)p);
}

/* The race of a supply type for the messages (0x414550): Zerg 0, Terran 1, Protoss 2, else 4. */
static int64_t message_race(KpGame *g, int64_t t)
{
	int64_t group = KP_DAT(g, units, staredit_group_flags, t);
	if (kp_band(group, 1) != 0) return 0;
	if (kp_band(group, 2) != 0) return 1;
	return kp_band(group, 4) != 0 ? 2 : 4;
}

/* CheckSupplyForUnit 0x4084d0: the supply type t takes (caching it) fits under the cap and in the free supply. */
int kp_check_supply(KpGame *g, int64_t p, int64_t t, int feedback)
{
	int64_t need, race;
	check_player(g, p);
	need = KP_DAT(g, units, supply_required, t);
	if (kp_band(dat_flags(g, t), DAT_DOUBLE_SUPPLY) != 0) need *= 2;
	g->cost_supply[p] = need;
	if (need == 0 || kp_band(dat_flags(g, t), DAT_NO_SUPPLY) != 0) return 1;
	if (t == LURKER) g->cost_supply[p] = kp_u32v(g->cost_supply[p] - KP_DAT(g, units, supply_required, HYDRALISK));
	race = message_race(g, t);
	if (kp_s32v(kp_supply_max(g, t, p)) < kp_s32v(kp_supply_used(g, race, p) + g->cost_supply[p])) {
		if (feedback) kp_report_error_sound(g, p, SOUND_SUPPLY_CAP + race, STRING_SUPPLY_CAP + race);
		return 0;
	}
	if (kp_s32v(kp_supply_free(g, t, p, kp_band(kp_cheats(g), CHEAT_FOOD) != 0) - g->cost_supply[p]) < 0) {
		if (feedback) kp_report_error_sound(g, p, SOUND_SUPPLY + race, STRING_SUPPLY + race);
		return 0;
	}
	return 1;
}

/* A price against valid player p's stock with the local race's feedback (signed comparisons). */
static int stock_covers(KpGame *g, int64_t p, int64_t minerals, int64_t gas, int feedback)
{
	int64_t local_race = g->players.race[g->local_player];
	if (kp_s32v(g->players.minerals[p]) < kp_s32v(minerals)) {
		if (feedback) kp_report_error_sound(g, p, SOUND_MINERALS + local_race, STRING_MINERALS);
		return 0;
	}
	if (kp_s32v(g->players.gas[p]) < kp_s32v(gas)) {
		if (feedback) kp_report_error_sound(g, p, SOUND_GAS + local_race, STRING_GAS);
		return 0;
	}
	return 1;
}

/* A price against the player's stock (0x41ba50, 0x41bab0). */
int kp_check_stock_of(KpGame *g, int64_t p, int64_t minerals, int64_t gas, int feedback)
{
	check_player(g, p);
	return stock_covers(g, p, minerals, gas, feedback);
}

/* CheckCachedCostAgainstStock 0x4085e0. */
int kp_check_stock(KpGame *g, int64_t p, int feedback)
{
	check_player(g, p); /* before the cached price is read */
	return stock_covers(g, p, g->cost_minerals[p], g->cost_gas[p], feedback);
}

/* CheckUnitCost 0x4086b0: caches the price of type t, checks its supply when asked, then the stock. */
int kp_check_unit_cost(KpGame *g, int64_t p, int64_t t, int supply, int feedback)
{
	check_player(g, p);
	g->cost_minerals[p] = kp_band(KP_DAT(g, units, mineral_cost, t), 0xffff);
	g->cost_gas[p] = kp_band(KP_DAT(g, units, vespene_cost, t), 0xffff);
	if (supply && !kp_check_supply(g, p, t, feedback)) return 0;
	return stock_covers(g, p, g->cost_minerals[p], g->cost_gas[p], feedback);
}

/* DeductCachedCost 0x408650. */
void kp_deduct_cost(KpGame *g, int64_t p)
{
	check_player(g, p);
	g->players.minerals[p] = kp_u32v(g->players.minerals[p] - g->cost_minerals[p]);
	g->players.gas[p] = kp_u32v(g->players.gas[p] - g->cost_gas[p]);
}

/* RefundUnitCost 0x408460: the full price of type t back to the stock. */
void kp_refund_unit(KpGame *g, int64_t p, int64_t t)
{
	check_player(g, p);
	g->players.minerals[p] = kp_u32v(g->players.minerals[p] + kp_band(KP_DAT(g, units, mineral_cost, t), 0xffff));
	g->players.gas[p] = kp_u32v(g->players.gas[p] + kp_band(KP_DAT(g, units, vespene_cost, t), 0xffff));
}

/* RefundUnitCostPartial 0x408490: three quarters of type t's price (each u16 price times 3, divided by 4). */
void kp_refund_unit_partial(KpGame *g, int64_t p, int64_t t)
{
	check_player(g, p);
	g->players.minerals[p] = kp_u32v(g->players.minerals[p] + kp_band(KP_DAT(g, units, mineral_cost, t), 0xffff) * 3 / 4);
	g->players.gas[p] = kp_u32v(g->players.gas[p] + kp_band(KP_DAT(g, units, vespene_cost, t), 0xffff) * 3 / 4);
}

/* EnqueueUnit 0x41b4b0: the first empty slot from the head takes type t once it is paid for (the supply is checked for
 * the head's slot only; a building is not paid here). */
int kp_enqueue_unit(KpGame *g, int64_t n, int64_t t)
{
	KpUnits *u = &g->u;
	int64_t head = u->queue_slot[n], k, slot;
	for (k = 0; k < QUEUE_SLOTS; k++) {
		slot = kp_mod(head + k, QUEUE_SLOTS);
		if (queued_at(g, n, slot) != NO_TYPE) continue;
		if (!kp_check_unit_cost(g, u->owner[n], t, slot == head, 1)) return 0;
		*slot_of(g, n, slot) = kp_band(t, 0xffff);
		if (kp_band(dat_flags(g, t), DAT_BUILDING) == 0) kp_deduct_cost(g, u->owner[n]);
		return 1;
	}
	return 0;
}

/* The training tags of a computer's BuildingAI move with the queue (0x484f90) and clear (0x484f70). */
static void ai_queue_meta(KpGame *g, int64_t n, const char *what)
{
	/* TODO: the BuildingAI's training tags and contexts (0x484f70, 0x484f90) once the AIScript VM of plan phase 7 gives
	 * producers their AI records */
	if (g->u.ai[n] != 0) kp_game_fail(g, "production: the training tags of a computer's producer (%s)", what);
}

/* CancelConstruction 0x41a340: an unfinished unit's price back, an Egg or Cocoon of a morph back to its larva form,
 * anything else dies. */
void kp_cancel_construction(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t t = u->type[n];
	if (kp_unit_is_dead(g, n) || kp_band(u->status[n], STATUS_COMPLETED) != 0 || t == GUARDIAN || t == LURKER
		|| t == DEVOURER || t == MUTALISK || t == HYDRALISK || (t == NYDUS_CANAL && u->union_0xd0[n] != 0)) {
		return;
	}
	if (kp_band(u->status[n], STATUS_GROUNDED_BUILDING) == 0) {
		kp_refund_unit(g, u->owner[n], t == EGG || t == COCOON || t == LURKER_EGG ? queued_at(g, n, u->queue_slot[n]) : t);
	} else if (kp_unit_race(g, n) == RACE_ZERG) {
		kp_cancel_zerg_building(g, n);
		return;
	} else {
		kp_refund_unit_partial(g, u->owner[n], t);
	}
	if (t == COCOON || t == LURKER_EGG) {
		int32_t s;
		kp_change_unit_type(g, n, t == COCOON ? MUTALISK : HYDRALISK); /* 0x424ad0 */
		u->build_time[n] = 0;
		*slot_of(g, n, u->queue_slot[n]) = NO_TYPE;
		s = kp_unit_sprite(g, n);
		kp_sprite_replace_main(g, s, KP_DAT(g, sprites, image, KP_DAT(g, flingy, sprite, KP_DAT(g, units, graphics,
			u->previous_type[n]))), 0); /* 0x415590 */
		u->order_signal[n] = kp_band(u->order_signal[n], kp_bnot(SIGNAL_BIRTH));
		kp_sprite_play(g->w, s, ANIM_SPECIAL2, 1);
		kp_issue_order(g, n, ORDER_ZERG_BIRTH);
		return;
	}
	if (t == NUCLEAR_MISSILE && u->connected[n] != 0) {
		int64_t silo = u->connected[n];
		kp_set_union_unit(g, silo, KP_UNION_PEER, 0);
		u->order_state[silo] = 0;
	}
	kp_unit_kill(g, n); /* 0x42a4d0 */
}

/* CancelQueuedAt 0x41b810: relative slot r is refunded (the current child cancelled instead) and the later slots move
 * up; the head does not move. */
void kp_cancel_queued(KpGame *g, int64_t n, int64_t r)
{
	KpUnits *u = &g->u;
	int64_t s, t, k;
	if (kp_u32v(r) >= QUEUE_SLOTS) return;
	s = kp_mod(u->queue_slot[n] + r, QUEUE_SLOTS);
	t = queued_at(g, n, s);
	if (t == NO_TYPE) return;
	if (r == 0 && u->build_unit[n] != 0) kp_cancel_construction(g, u->build_unit[n]);
	else if (kp_band(dat_flags(g, t), DAT_BUILDING) == 0) kp_refund_unit(g, u->owner[n], t);
	for (k = 4 - r; k != 0 && queued_at(g, n, s) != NO_TYPE; k--) {
		int64_t next = kp_mod(s + 1, QUEUE_SLOTS);
		*slot_of(g, n, s) = *slot_of(g, n, next);
		ai_queue_meta(g, n, "0x484f90");
		s = next;
	}
	*slot_of(g, n, s) = NO_TYPE;
}

/* CancelLastQueued 0x41b6f0. */
void kp_cancel_last_queued(KpGame *g, int64_t n)
{
	int64_t r;
	for (r = 4; r >= 0; r--) {
		if (queued_at(g, n, g->u.queue_slot[n] + r) != NO_TYPE) {
			kp_cancel_queued(g, n, r);
			return;
		}
	}
}

/* CancelAllQueued 0x41b5c0: every slot from the last, then an empty queue from slot 0. */
void kp_cancel_all_queued(KpGame *g, int64_t n)
{
	int64_t r;
	for (r = 4; r >= 0; r--) kp_cancel_queued(g, n, r);
	for (r = 0; r < QUEUE_SLOTS; r++) *slot_of(g, n, r) = NO_TYPE;
	g->u.queue_slot[n] = 0;
}

/* SetRallyPoint 0x41bbc0. */
void kp_set_rally_point(KpGame *g, int64_t n, int64_t x, int64_t y)
{
	g->u.rally_x[n] = kp_s16v(x);
	g->u.rally_y[n] = kp_s16v(y);
	g->u.rally_unit[n] = 0;
}

/* SetRallyUnit 0x41bbf0: the rally follows target (the producer itself for none) from its sprite's position. */
void kp_set_rally_unit(KpGame *g, int64_t n, int64_t target)
{
	int32_t s;
	if (target == 0) target = n;
	s = kp_unit_sprite(g, target);
	g->u.rally_unit[n] = target;
	g->u.rally_x[n] = kp_s16v(g->w->spr.x[s]);
	g->u.rally_y[n] = kp_s16v(g->w->spr.y[s]);
}

/* SendToRally 0x41bd70: a finished unit follows its producer's rally unit of its owner, or moves to its rally point. */
void kp_send_to_rally(KpGame *g, int64_t n, int64_t producer)
{
	KpUnits *u = &g->u;
	int64_t r;
	if (producer == 0 || !kp_rally_producer(u->type[producer])) return;
	r = u->rally_unit[producer];
	if (r == producer || u->rally_x[producer] == 0) return;
	if (r != 0 && u->owner[r] == u->owner[n]) kp_issue_order_target(g, n, ORDER_FOLLOW, r);
	else kp_issue_order_at(g, n, ORDER_MOVE, kp_s16v(u->rally_x[producer]), kp_s16v(u->rally_y[producer]));
}

/* PlayUnitReadyVoice 0x446650: the local owner hears a finished unit's ready sound unless quiet. */
void kp_ready_voice(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t t = u->type[n];
	if (u->owner[n] != g->local_player || g->w->replay || g->trig.quiet != 0 || t == INTERCEPTOR || t == SCARAB) return;
	kp_play_unit_sound(g, KP_DAT(g, units, ready_sound, t), n);
	/* the talking portrait it requests is the presentation's */
}

/* A WORD gain of the build progress, which the cwal cheat multiplies. */
static int64_t cwal_gain(KpGame *g, int64_t gain)
{
	gain = kp_band(gain, 0xffff);
	return kp_band(kp_band(kp_cheats(g), CHEAT_CWAL) != 0 ? gain * CWAL_FACTOR : gain, 0xffff);
}

/* GetBuildHpGain 0x41b290. */
int64_t kp_build_hp_gain(KpGame *g, int64_t n) { return cwal_gain(g, g->u.hp_gain[n]); }

/* The shields a warping Protoss building gains each step (Order_IncompleteWarping 0x42f020). */
int64_t kp_build_shield_gain(KpGame *g, int64_t n) { return cwal_gain(g, g->u.shield_gain[n]); }

/* TickBuildTimer 0x41a540: 1 when the build time was already 0, else it counts down (by 16 under cwal, a negative WORD
 * to 0). */
int kp_tick_build_timer(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t time = kp_s16v(u->build_time[n]);
	if (time == 0) return 1;
	if (kp_band(kp_cheats(g), CHEAT_CWAL) == 0) {
		u->build_time[n] = kp_mod(time - 1, 65536);
	} else {
		time = kp_s16v(time - CWAL_FACTOR);
		u->build_time[n] = time < 0 ? 0 : time;
	}
	return 0;
}

/* ProgressConstruction 0x41a280: the HP grow by the gain and the time counts down, or the unit finishes (placed first
 * when `place`); 0 only when that placement fails. */
int kp_progress_construction(KpGame *g, int64_t n, int64_t gain, int place)
{
	KpUnits *u = &g->u;
	kp_set_unit_hp(g, n, u->hp[n] + gain); /* 0x41bb50 */
	if (!kp_tick_build_timer(g, n)) return 1;
	if (!place) kp_unit_complete_in_place(g, n);
	else if (!kp_unit_complete(g, n)) return 0;
	if (kp_band(u->status[n], STATUS_GROUNDED_BUILDING) == 0) {
		kp_ready_voice(g, n);
		return 1;
	}
	if (kp_unit_race(g, n) == RACE_TERRAN) {
		int64_t a = kp_find_attachable_addon(g, n);
		if (a != 0) kp_attach_addon(g, n, a); /* AttachAddon 0x4359c0 */
	}
	return 1;
}

/* TryCreateQueuedUnit 0x41bc30: a grounded building (or a hangar unit, an Egg) creates its queued type at its sprite
 * once the supply allows (paid already at the queue). */
int64_t kp_create_queued(KpGame *g, int64_t n, int64_t t, int feedback)
{
	KpUnits *u = &g->u;
	int64_t pt = u->type[n], c;
	int32_t s;
	if (kp_band(u->status[n], STATUS_GROUNDED_BUILDING) == 0 && pt != CARRIER && pt != GANTRITHOR && pt != REAVER
		&& pt != WARBRINGER && pt != EGG) {
		return 0;
	}
	if (!kp_check_supply(g, u->owner[n], t, feedback)) return 0;
	s = kp_unit_sprite(g, n);
	c = kp_unit_create(g, t, kp_s16v(g->w->spr.x[s]), kp_s16v(g->w->spr.y[s]), u->owner[n]);
	if (c == 0) kp_report_creation_error(g, u->owner[n]); /* 0x425020 */
	return c;
}

/* SecondaryTrain 0x41b060 (secondary order 38). */
void kp_secondary_train(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t state = u->secondary_state[n], t, c;
	if (kp_unit_disabled(g, n) || (kp_unit_race(g, n) == RACE_ZERG && u->type[n] != INFESTED_COMMAND_CENTER)) return;
	if (state < 2) {
		t = queued_at(g, n, u->queue_slot[n]);
		if (t == NO_TYPE) {
			kp_set_secondary_order(g, n, SECONDARY_NOTHING);
			kp_sprite_play(g->w, kp_unit_sprite(g, n), ANIM_WORKING_TO_IDLE, 1);
			return;
		}
		c = kp_create_queued(g, n, t, state == 0);
		u->build_unit[n] = c;
		if (c == 0) {
			u->secondary_state[n] = 1;
			return;
		}
		u->secondary_state[n] = 2;
		kp_sprite_play(g->w, kp_unit_sprite(g, n), ANIM_IS_WORKING, 1);
		return;
	}
	if (state != 2) return;
	c = u->build_unit[n];
	if (c != 0) {
		if (!kp_progress_construction(g, c, kp_build_hp_gain(g, c), 1)) {
			kp_cancel_queued(g, n, 0);
		} else {
			if (kp_band(u->status[c], STATUS_COMPLETED) == 0) return;
			/* TODO: a computer's new unit joins its AI (0x48d320) once the AIScript VM of plan phase 7 exists */
			if (kp_controller(g, u->owner[c]) == CONTROLLER_COMPUTER) {
				kp_game_fail(g, "production: a computer's trained unit joins its AI (0x48d320)");
			}
			if (u->type[c] == NUCLEAR_MISSILE) kp_unit_hide(g, c); /* 0x422ba0 */
			else kp_send_to_rally(g, c, n);
			ai_queue_meta(g, n, "0x484f70");
			*slot_of(g, n, u->queue_slot[n]) = NO_TYPE;
			u->queue_slot[n] = kp_mod(u->queue_slot[n] + 1, QUEUE_SLOTS);
		}
	}
	/* the interface refresh 0x4a7ed0 is the presentation's */
	u->build_unit[n] = 0;
	u->secondary_state[n] = 0;
}
