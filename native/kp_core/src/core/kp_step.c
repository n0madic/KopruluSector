/* The object step and the game frame (kp_game.h). */
#include "kp_game_internal.h"

#define SECONDARY_TRAIN_FIGHTER 0x3f /* TrainFighter 0x41c630, a hidden Carrier's or Reaver's */
#define TILE_PERIOD 100
#define ORDER_SPREAD 8
#define ORDER_SPREAD_PERIOD 0x96
#define SECONDARY_SPREAD 30
#define SECONDARY_SPREAD_PERIOD 300
#define DETECTION_PERIOD 0x1e
#define STATUS_CLOAKED 0x300
#define STATUS_AIR 0x4
#define STATUS_HALTED 0x8000
#define STATUS_GROUNDED_BUILDING 0x2
#define STATUS_BURROWED 0x10
#define STATUS_REQUIRES_DETECTION 0x100
#define CLOAK_SOUND 273
#define DECLOAK_SOUND 274
#define STATUS_FREE_CLOAK 0x800
#define SECONDARY_CLOAK 0x6d
#define SPRITE_HIDDEN 0x20
#define DAT_SUBUNIT 0x10
#define RACE_PROTOSS 2
#define CONTROLLER_RESCUE 3
#define DISRUPTION_WEB 0x69
#define NEUTRAL 11

void kp_step_init(KpGame *g)
{
	kp_scalar_set(g, KP_TILE_COUNTDOWN, 1);
	kp_scalar_set(g, KP_UPDATE_TILES, 0);
	kp_scalar_set(g, KP_MOVE_REVEAL, 0);
	kp_scalar_set(g, KP_ORDER_SPREAD, 10);
	kp_scalar_set(g, KP_SECONDARY_SPREAD, ORDER_SPREAD_PERIOD);
	kp_scalar_set(g, KP_INTERCEPTOR_ALERTED, 0);
	kp_scalar_set(g, KP_HIT_SLOT, 0); /* the hit history of the slots (0x540768) belongs to combat */
	kp_scalar_set(g, KP_FRAME, 0);
	kp_scalar_set(g, KP_TURN, 0);
}

void kp_set_context(KpGame *g, int64_t n)
{
	g->w->unit = g->w->flingy = (int32_t)n;
	g->w->has_unit = g->w->has_flingy = 1;
}

static void clear_context(KpGame *g) { g->w->has_unit = g->w->has_flingy = 0; }


/* WebCoverCallback 0x43a880: a ground unit under a web (and its cargo or turret) cannot attack. */
static void web_covers(KpGame *g, int64_t c)
{
	KpUnits *u = &g->u;
	int k;
	if (kp_band(u->status[c], STATUS_AIR) != 0) return;
	u->status[c] = kp_bor(u->status[c], STATUS_HALTED);
	if (kp_is_transport(g, c)) {
		for (k = 0; k < 8; k++) {
			int64_t p = kp_cargo_unit(g, c, k);
			if (p != 0) u->status[p] = kp_bor(u->status[p], STATUS_HALTED);
		}
	} else if (u->subunit[c] != 0) {
		u->status[u->subunit[c]] = kp_bor(u->status[u->subunit[c]], STATUS_HALTED);
	}
}

/* UpdateDisruptionWebCoverage 0x438cd0. */
static void disruption_web(KpGame *g)
{
	KpUnits *u = &g->u;
	int64_t n = g->lists[KP_LIST_ACTIVE].head;
	while (n != 0) {
		int64_t status = u->status[n];
		if (kp_band(status, STATUS_AIR) == 0 || kp_band(status, STATUS_HALTED) != 0) {
			u->status[n] = kp_band(status, kp_bnot(STATUS_HALTED));
			if (kp_is_transport(g, n)) {
				int k;
				for (k = 0; k < 8; k++) {
					int64_t c = kp_cargo_unit(g, n, k);
					if (c != 0) u->status[c] = kp_band(u->status[c], kp_bnot(STATUS_HALTED));
				}
			} else if (u->subunit[n] != 0) {
				int64_t sub = u->subunit[n];
				u->status[sub] = kp_band(u->status[sub], kp_bnot(STATUS_HALTED));
			}
		}
		n = u->next[n];
	}
	if (g->count_completed[DISRUPTION_WEB * 12 + NEUTRAL] == 0) return;
	for (n = g->player_units[NEUTRAL]; n != 0; n = u->player_next[n]) {
		int32_t s;
		int64_t x, y, *found;
		int count, k;
		if (u->type[n] != DISRUPTION_WEB) continue;
		s = kp_unit_sprite(g, n);
		x = kp_s16v(g->w->spr.x[s]);
		y = kp_s16v(g->w->spr.y[s]);
#define EXT(i) KP_DAT(g, units, unit_extents, DISRUPTION_WEB * 4 + (i))
		found = kp_query_rect(g, kp_s16v(x - EXT(0)), kp_s16v(y - EXT(1)), kp_s16v(x + EXT(2)), kp_s16v(y + EXT(3)), &count);
#undef EXT
		for (k = 0; k < count; k++) web_covers(g, found[k]); /* 0x43a880 */
		kp_query_release(g, found);
	}
}

/* UpdateUnit 0x43a720. */
static void update_unit(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int subunit = kp_band(KP_DAT(g, units, special_ability_flags, u->type[n]), DAT_SUBUNIT) != 0;
	int64_t sub;
	kp_flash_selection(g, n);
	kp_update_unit_timers(g, n);
	kp_dispatch_main_order(g, n);
	kp_dispatch_secondary_order(g, n);
	sub = u->subunit[n];
	if (sub != 0 && !subunit) {
		g->w->unit = (int32_t)sub;
		g->w->has_unit = 1;
		update_unit(g, sub);
		g->w->unit = (int32_t)n;
	}
	kp_step_unit_sprite(g, n);
}

/* UpdateHiddenUnit 0x43a770. */
static void update_hidden_unit(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t sub = u->subunit[n];
	if (sub != 0 && kp_band(KP_DAT(g, units, special_ability_flags, u->type[n]), DAT_SUBUNIT) == 0) {
		g->w->unit = (int32_t)sub;
		g->w->has_unit = 1;
		update_hidden_unit(g, sub);
		g->w->unit = (int32_t)n;
	}
	kp_execute_movement(g, n);
	kp_update_unit_timers(g, n);
	kp_dispatch_hidden_order(g, n);
	if (u->secondary_order[n] == SECONDARY_TRAIN_FIGHTER) kp_order_train_fighter(g, n); /* TrainFighter 0x41c630 */
	else kp_secondary_cloak_order(g, n, u->secondary_order[n]);
	kp_step_unit_sprite(g, n);
}

/* UpdateCloakList 0x434310. */
static void update_cloak_list(KpGame *g)
{
	KpUnits *u = &g->u;
	int64_t n = g->scalar[KP_CLOAKED_HEAD];
	while (n != 0) {
		int64_t next = u->burrow_next[kp_game_unit(g, n)], status;
		if (kp_cloak_list_remove(g, n)) {
			kp_decloak(g, n, DECLOAK_SOUND);
			n = next;
			continue;
		}
		status = u->status[n];
		if (kp_band(status, STATUS_BURROWED) == 0 && u->secondary_order[n] == SECONDARY_CLOAK && u->is_cloaked[n] == 1
			&& kp_band(status, STATUS_FREE_CLOAK) != 0) {
			u->status[n] = status - STATUS_FREE_CLOAK;
		}
		if (kp_band(u->status[n], STATUS_REQUIRES_DETECTION) == 0) kp_begin_cloak(g, n, CLOAK_SOUND);
		n = next;
	}
}


void kp_update_units(KpGame *g)
{
	KpUnits *u = &g->u;
	int64_t n, v;
	kp_scalar_set(g, KP_INTERCEPTOR_ALERTED, 0);
	kp_scalar_set(g, KP_ORDER_SPREAD, kp_scalar(g, KP_ORDER_SPREAD) - 1);
	if (kp_scalar(g, KP_ORDER_SPREAD) == 0) {
		v = 0;
		for (n = g->lists[KP_LIST_ACTIVE].head; n != 0; n = u->next[n]) {
			u->queue_timer[n] = v;
			v = (v + 1) % ORDER_SPREAD;
		}
		kp_scalar_set(g, KP_ORDER_SPREAD, ORDER_SPREAD_PERIOD);
	}
	kp_scalar_set(g, KP_SECONDARY_SPREAD, kp_scalar(g, KP_SECONDARY_SPREAD) - 1);
	if (kp_scalar(g, KP_SECONDARY_SPREAD) == 0) {
		v = 0;
		for (n = g->lists[KP_LIST_ACTIVE].head; n != 0; n = u->next[n]) {
			u->secondary_timer[n] = v;
			v = (v + 1) % SECONDARY_SPREAD;
		}
		kp_scalar_set(g, KP_SECONDARY_SPREAD, SECONDARY_SPREAD_PERIOD);
	}
	disruption_web(g);
	kp_update_pending_units(g);
	for (n = g->lists[KP_LIST_ACTIVE].head; n != 0; n = u->next[n]) {
		kp_set_context(g, n);
		kp_prepare_movement(g, n);
	}
	if (kp_scalar(g, KP_UPDATE_TILES) == 1) {
		n = g->lists[KP_LIST_SPECIAL].head;
		while (n != 0) {
			int64_t m = u->next[n];
			kp_set_context(g, n);
			kp_unit_update_vision(g, n);
			n = m;
		}
	}
	for (n = g->lists[KP_LIST_ACTIVE].head; n != 0; n = u->next[n]) {
		kp_unit_update_sprite_visibility(g, n);
		if (kp_band(u->status[n], STATUS_CLOAKED) != 0) {
			u->is_cloaked[n] = 0;
			if (u->secondary_timer[n] == 0) {
				kp_update_detection(g, n);
				u->secondary_timer[n] = DETECTION_PERIOD;
			} else {
				u->secondary_timer[n] = u->secondary_timer[n] - 1;
			}
		}
	}
	n = g->lists[KP_LIST_ACTIVE].head;
	while (n != 0) {
		int64_t m = u->next[n];
		kp_set_context(g, n);
		update_unit(g, n);
		n = m;
	}
	n = g->lists[KP_LIST_HIDDEN].head;
	while (n != 0) {
		int64_t m = u->next[n];
		if (kp_band(u->status[n], STATUS_CLOAKED) != 0) u->is_cloaked[n] = 0;
		kp_set_context(g, n);
		update_hidden_unit(g, n);
		n = m;
	}
	update_cloak_list(g);
	kp_update_power(g);
	kp_advance_hit_history(g);
	n = g->lists[KP_LIST_SPECIAL].head;
	while (n != 0) {
		int64_t m = u->next[n];
		kp_set_context(g, n);
		update_unit(g, n);
		n = m;
	}
	clear_context(g);
}

void kp_step_objects(KpGame *g)
{
	kp_rng_game_set_enabled(g->rng, 1);
	kp_creep_recede_tick(g);
	kp_ai_step(g);
	if (kp_scalar(g, KP_TILE_COUNTDOWN) == 0) kp_scalar_set(g, KP_TILE_COUNTDOWN, TILE_PERIOD);
	kp_scalar_set(g, KP_TILE_COUNTDOWN, kp_scalar(g, KP_TILE_COUNTDOWN) - 1);
	kp_scalar_set(g, KP_UPDATE_TILES, kp_scalar(g, KP_TILE_COUNTDOWN) == 0);
	if (kp_scalar(g, KP_UPDATE_TILES) == 1) {
		kp_refresh_remembered_terrain(g);
		kp_reset_tile_visibility(g);
	}
	kp_update_units(g);
	kp_bullets_step(g);
	kp_images_update_thingies(g->w);
	kp_rng_game_set_enabled(g->rng, 0);
}

void kp_prime(KpGame *g)
{
	kp_step_objects(g);
	kp_step_objects(g);
}

void kp_step_frame(KpGame *g)
{
	kp_step_objects(g);
	kp_rng_game_set_enabled(g->rng, 1);
	kp_trigger_step(g);
	kp_rng_game_set_enabled(g->rng, 0);
}

/* ExecuteGameTurn 0x4768a0: the game RNG runs during the commands (an order's animation may draw from it). */
void kp_run_turn(KpGame *g, const KpCommand *commands, int count)
{
	int k;
	kp_rng_game_set_enabled(g->rng, 1);
	for (k = 0; k < count; k++) kp_execute_command(g, commands[k].player, commands[k].bytes, commands[k].size);
	kp_rng_game_set_enabled(g->rng, 0);
	kp_scalar_set(g, KP_TURN, kp_scalar(g, KP_TURN) + 1);
}

/* An iteration of RunPendingFrames 0x444e30: a paused game runs its turn, the flashes and the triggers only. */
void kp_run_frame(KpGame *g, const KpCommand *commands, int count)
{
	if (kp_scalar(g, KP_PAUSED) == 1) {
		kp_paused_tick(g, commands, count);
		return;
	}
	kp_run_turn(g, commands, count);
	kp_scalar_set(g, KP_FRAME, kp_scalar(g, KP_FRAME) + 1);
	kp_step_frame(g);
}

/* --- the AI step and the guard records --- */

#define REGION_STEPS 0x14d
#define REGION_CYCLE 0x9c4
#define AI_PLAYERS 8
#define UNDER_ATTACK_PERIOD 300
#define UNDER_ATTACK_ROUNDS 6
#define STATUS_UNDER_ATTACK 0x80
#define SPENDING_EARLY 7
#define SPENDING_LATE 37
#define EARLY_SECONDS 0x1194
#define DAT_RESOURCE_CONTAINER 0x2000

void kp_ai_init(KpGame *g)
{
	KpAi *ai = &g->ai;
	ai->terrain_changed = 0;
	ai->period = 0x3c;
	ai->cycle = 0xf;
	ai->rounds = 6;
	ai->region_player = ai->region_cursor = 0;
	ai->spending_player = ai->town_player = 0;
	ai->spell_reactive = 0;
	ai->migrating = 0;
	kp_aiscript_init(g); /* LoadAiScriptBins 0x493800 */
	kp_towns_init(g); /* InitTownAiPools 0x486a20 */
}

static int computer(KpGame *g, int64_t p) { return kp_controller(g, p) == KP_CTRL_COMPUTER; }

/* UpdateAiRegions 0x4b0d70: 333 visits of the region cursor, which steps the computer players' regions. */
static void update_regions(KpGame *g)
{
	KpAi *ai = &g->ai;
	int64_t count = (int64_t)g->regions.count;
	int k;
	for (k = 1; k <= REGION_STEPS; k++) {
		if (ai->region_cursor < count && computer(g, ai->region_player)) {
			kp_ai_step_region(g, ai->region_player, ai->region_cursor);
		}
		ai->region_cursor++;
		if (ai->region_cursor == REGION_CYCLE) {
			ai->region_player = (ai->region_player + 1) % AI_PLAYERS;
			ai->region_cursor = 1;
			kp_ai_rebuild_enemy_strength(g);
			if (computer(g, ai->region_player) && !kp_ai_campaign(g, ai->region_player)) {
				kp_game_fail(g, "ai: an expansion attack plan (0x4aa8f0)");
			}
		}
	}
}

/* StepAiTimers 0x48df50. */
static void step_timers(KpGame *g)
{
	KpAi *ai = &g->ai;
	int due;
	if (ai->cycle == 0) {
		int again = ai->rounds != 0;
		ai->cycle = UNDER_ATTACK_PERIOD;
		if (again) ai->rounds--;
		if (!again || ai->terrain_changed != 0) {
			int64_t n = g->lists[KP_LIST_ACTIVE].head;
			ai->rounds = UNDER_ATTACK_ROUNDS;
			ai->terrain_changed = 0;
			while (n != 0) {
				g->u.status[n] = kp_band(g->u.status[n], kp_bnot(STATUS_UNDER_ATTACK));
				n = g->u.next[n];
			}
		}
	} else {
		ai->cycle--;
	}
	due = ai->period == 0;
	ai->period = kp_mod(ai->period - 1, 4294967296LL);
	if (!due) return;
	ai->period = kp_scalar(g, KP_ELAPSED) < EARLY_SECONDS ? SPENDING_EARLY : SPENDING_LATE;
	ai->spending_player = (ai->spending_player + 1) % AI_PLAYERS; /* RunAiSpendingPass 0x483400 */
	if (computer(g, ai->spending_player) && kp_band(g->pai.flags[ai->spending_player], KP_AIF_SCRIPT_STARTED) != 0) {
		kp_ai_spending_pass(g, ai->spending_player);
	}
	ai->town_player = (ai->town_player + 1) % AI_PLAYERS;
	kp_ai_maintain_towns(g, ai->town_player); /* MaintainNextPlayerTowns 0x486c80 */
	kp_ai_evaluate_attacks(g); /* EvaluateGlobalAttackOpportunity 0x4acdf0 */
}

void kp_ai_step(KpGame *g)
{
	kp_aiscript_run(g);
	update_regions(g);
	kp_ai_areas_refresh(g); /* RefreshResourceAreas 0x496c80 */
	step_timers(g);
}

#define GUARD_NONE (-1)
#define GUARD_LOSSES 3
#define GUARD_LOSS_RETRY 300
#define GUARD_RETRY 5
#define GUARD_RETRY_DIVISOR 15
#define STATUS_HALLUCINATION 0x40000000

void kp_guard_init(KpGame *g)
{
	KpGuards *gd = &g->guards;
	int64_t k;
	int p;
	for (k = 0; k < KP_GUARDS; k++) {
		gd->next[k] = k + 1 < KP_GUARDS ? k + 1 : GUARD_NONE;
		gd->prev[k] = k > 0 ? k - 1 : GUARD_NONE;
		gd->kind[k] = gd->unit[k] = gd->type[k] = gd->deaths[k] = gd->time[k] = 0;
		gd->x1[k] = gd->y1[k] = gd->x2[k] = gd->y2[k] = 0;
	}
	gd->free = 0;
	for (p = 0; p <= 7; p++) gd->heads[p] = GUARD_NONE;
}

/* A guard record leaves its player's list for the pool's free list (payload kept). */
static void guard_recycle(KpGuards *gd, int64_t p, int64_t k)
{
	kp_pool_release(gd->next, gd->prev, &gd->heads[p], &gd->free, k);
}

/* A free guard record at the head of player p's list (GUARD_NONE when the pool is empty). */
static int64_t guard_take(KpGuards *gd, int64_t p)
{
	return kp_pool_take(gd->next, gd->prev, &gd->free, &gd->heads[p]);
}

/* FreeParentlessGuards 0x4b21f0: player p's guard records without a unit return to the pool's free list (payload
 * kept). */
void kp_guard_free_parentless(KpGame *g, int64_t p)
{
	KpGuards *gd = &g->guards;
	int64_t k = gd->heads[p];
	while (k != GUARD_NONE && gd->heads[p] != GUARD_NONE) {
		int64_t next = gd->next[k];
		if (gd->unit[k] == 0) guard_recycle(gd, p, k);
		k = next;
	}
}

/* The build time of a guard's type that its replacement waits on: a sieged tank as a tank, a morph with its parent's. */
static int64_t guard_build_time(KpGame *g, int64_t t)
{
	int64_t time, parent = -1;
	if (t == KP_TYPE_SIEGE_TANK_SIEGED) t = KP_TYPE_SIEGE_TANK;
	time = kp_band(KP_DAT(g, units, build_time, t), 0xffff);
	switch (t) {
	case KP_TYPE_GUARDIAN: case KP_TYPE_DEVOURER: parent = KP_TYPE_MUTALISK; break;
	case KP_TYPE_DARK_ARCHON: parent = KP_TYPE_DARK_TEMPLAR; break;
	case KP_TYPE_ARCHON: parent = KP_TYPE_HIGH_TEMPLAR; break;
	case KP_TYPE_LURKER: parent = KP_TYPE_HYDRALISK; break;
	}
	if (parent >= 0) time += kp_band(KP_DAT(g, units, build_time, parent), 0xffff);
	return time;
}

/* RebuildGuards 0x4b2320: a campaign AI forgets a guard lost three times, a hallucination's guard goes; a parentless
 * guard past its retry deadline (300 after a loss, else 5 + build time / 15) in an unthreatened region takes a spare
 * Zergling or Scourge, else requests its unit again. */
void kp_guard_rebuild(KpGame *g, int64_t p)
{
	KpGuards *gd = &g->guards;
	int64_t k = gd->heads[p];
	while (k != GUARD_NONE && gd->heads[p] != GUARD_NONE) {
		int64_t next = gd->next[k], n = gd->unit[k];
		if (kp_ai_campaign(g, p) && gd->deaths[k] >= GUARD_LOSSES) {
			guard_recycle(gd, p, k);
		} else if (n != 0) {
			if (kp_band(g->u.status[n], STATUS_HALLUCINATION) != 0) guard_recycle(gd, p, k);
		} else {
			int64_t t = gd->time[k], ok = 1;
			if (t != 0) {
				int64_t deadline = gd->deaths[k] != 0 ? t + GUARD_LOSS_RETRY
					: t + GUARD_RETRY + kp_u32v(guard_build_time(g, gd->type[k])) / GUARD_RETRY_DIVISOR;
				ok = kp_u32v(kp_scalar(g, KP_ELAPSED)) > kp_u32v(deadline);
			}
			if (ok && kp_ai_region_unthreatened(g, p, kp_region_at(g, kp_s16v(gd->x2[k]), kp_s16v(gd->y2[k])))) {
				int64_t spare = 0;
				if (gd->type[k] == KP_TYPE_ZERGLING || gd->type[k] == KP_TYPE_SCOURGE) {
					spare = kp_ai_spare_military(g, p, gd->type[k], kp_s16v(gd->x2[k]), kp_s16v(gd->y2[k]));
				}
				if (spare != 0) {
					kp_remove_unit_ai(g, spare, 0);
					gd->x1[k] = gd->x2[k];
					gd->y1[k] = gd->y2[k];
					gd->unit[k] = spare;
					g->u.ai[spare] = KP_AI_REF(KP_AI_GUARD, k);
					kp_issue_order(g, spare, KP_ORD_COMPUTER_AI);
				} else {
					kp_ai_request_guard(g, p, k);
					gd->time[k] = kp_u32v(kp_scalar(g, KP_ELAPSED));
				}
			}
		}
		k = next;
	}
}

int kp_guard_vacancy(KpGame *g, int64_t p, int64_t t)
{
	const KpGuards *gd = &g->guards;
	int64_t k;
	for (k = gd->heads[p]; k != GUARD_NONE; k = gd->next[k]) {
		if (gd->unit[k] == 0 && gd->type[k] == t
			&& kp_ai_region_unthreatened(g, p, kp_region_at(g, kp_s16v(gd->x2[k]), kp_s16v(gd->y2[k])))) {
			return 1;
		}
	}
	return 0;
}

void kp_guard_reset_retry(KpGame *g, int64_t p, int64_t t)
{
	KpGuards *gd = &g->guards;
	int64_t k;
	for (k = gd->heads[p]; k != GUARD_NONE; k = gd->next[k]) {
		if (gd->unit[k] == 0 && gd->time[k] != 0 && (gd->type[k] == KP_TYPE_SIEGE_TANK_SIEGED ? KP_TYPE_SIEGE_TANK : gd->type[k]) == kp_band(t, 0xffff)) gd->time[k] = 1;
	}
}

void kp_guard_precreate(KpGame *g, int64_t p, int64_t t, int64_t x, int64_t y)
{
	KpGuards *gd = &g->guards;
	int64_t k = guard_take(gd, p);
	if (k == GUARD_NONE) return;
	gd->kind[k] = KP_AI_GUARD;
	gd->deaths[k] = 0;
	gd->unit[k] = 0;
	gd->type[k] = kp_band(t, 0xffff);
	gd->x1[k] = gd->x2[k] = kp_band(x, 0xffff);
	gd->y1[k] = gd->y2[k] = kp_band(y, 0xffff);
}

void kp_guard_attach_unit(KpGame *g, int64_t k, int64_t n)
{
	KpGuards *gd = &g->guards;
	gd->x1[k] = gd->x2[k];
	gd->y1[k] = gd->y2[k];
	gd->unit[k] = n;
	g->u.ai[n] = KP_AI_REF(KP_AI_GUARD, k);
}

void kp_guard_attach(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	KpGuards *gd = &g->guards;
	int64_t p = u->owner[n], t, x, y, k;
	int32_t s;
	if (!computer(g, p)) return;
	t = u->type[n];
	if (kp_band(KP_DAT(g, units, ai_internal, t), KP_AIINT_TOWN) != 0) return;
	if (p < 0 || p > 7) kp_game_fail(g, "guard: player %lld", (long long)p);
	s = kp_unit_sprite(g, n);
	x = kp_s16v(g->w->spr.x[s]);
	y = kp_s16v(g->w->spr.y[s]);
	k = gd->heads[p];
	while (k != GUARD_NONE) {
		if (gd->unit[k] == 0 && gd->type[k] == t && gd->x2[k] == x && gd->y2[k] == y) break;
		k = gd->next[k];
	}
	if (k == GUARD_NONE) {
		k = guard_take(gd, p);
		if (k == GUARD_NONE) return;
		gd->kind[k] = KP_AI_GUARD;
		gd->type[k] = t;
	}
	gd->unit[k] = n;
	gd->x1[k] = gd->x2[k] = x;
	gd->y1[k] = gd->y2[k] = y;
	gd->deaths[k] = 0;
	u->ai[n] = KP_AI_REF(KP_AI_GUARD, k);
}
