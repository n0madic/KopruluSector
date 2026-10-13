/* The object step and the game frame (kp_game.h; game/simulation/step.lua, the specification). */
#include "kp_game_internal.h"

#define TILE_PERIOD 100
#define ORDER_SPREAD 8
#define ORDER_SPREAD_PERIOD 0x96
#define SECONDARY_SPREAD 30
#define SECONDARY_SPREAD_PERIOD 300
#define DETECTION_PERIOD 0x1e
#define HIT_SLOTS 32
#define STATUS_CLOAKED 0x300
#define STATUS_AIR 0x4
#define STATUS_HALTED 0x8000
#define STATUS_GROUNDED_BUILDING 0x2
#define STATUS_BURROWED 0x10
#define STATUS_REQUIRES_DETECTION 0x100
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
				if (u->loaded0[n] != 0 || u->loaded1[n] != 0 || u->loaded2[n] != 0 || u->loaded3[n] != 0
					|| u->loaded4[n] != 0 || u->loaded5[n] != 0 || u->loaded6[n] != 0 || u->loaded7[n] != 0) {
					kp_game_fail(g, "step: web coverage of loaded units (0x4381d0)");
				}
			} else if (u->subunit[n] != 0) {
				int64_t sub = u->subunit[n];
				u->status[sub] = kp_band(u->status[sub], kp_bnot(STATUS_HALTED));
			}
		}
		n = u->next[n];
	}
	if (g->count_completed[DISRUPTION_WEB * 12 + NEUTRAL] != 0) kp_game_fail(g, "step: disruption web coverage (0x43a880)");
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
	if (u->secondary_order[n] == 0x3f) kp_game_fail(g, "step: secondary order 0x3f of a hidden unit (0x41c630)");
	kp_secondary_cloak_order(g, n, u->secondary_order[n]);
	kp_step_unit_sprite(g, n);
}

/* UpdateCloakList 0x434310. */
static void update_cloak_list(KpGame *g)
{
	KpUnits *u = &g->u;
	int64_t n = g->scalar[KP_CLOAKED_HEAD];
	while (n != 0) {
		int64_t next = u->burrow_next[kp_game_unit(g, n)], status;
		if (kp_cloak_list_remove(g, n)) kp_game_fail(g, "step: decloaking a unit without cloak source (0x41d360)");
		status = u->status[n];
		if (kp_band(status, STATUS_BURROWED) == 0 && u->secondary_order[n] == SECONDARY_CLOAK && u->is_cloaked[n] == 1
			&& kp_band(status, STATUS_FREE_CLOAK) != 0) {
			u->status[n] = status - STATUS_FREE_CLOAK;
		}
		if (kp_band(u->status[n], STATUS_REQUIRES_DETECTION) == 0) {
			kp_game_fail(g, "step: cloaking a unit of the cloaked list (0x41d200)");
		}
		n = next;
	}
}

/* UpdateProtossPower 0x430670. */
static void update_power(KpGame *g)
{
	KpUnits *u = &g->u;
	int64_t n;
	if (kp_scalar(g, KP_POWER_DIRTY) != 1) return;
	n = g->lists[KP_LIST_ACTIVE].head;
	while (n != 0) {
		if (kp_band(u->status[n], STATUS_GROUNDED_BUILDING) != 0 && kp_unit_race(g, n) == RACE_PROTOSS
			&& KP_GET(g, "players", u->owner[n], "controller", KP_NIL) != CONTROLLER_RESCUE) {
			kp_game_fail(g, "step: Protoss power (0x430000)");
		}
		n = u->next[n];
	}
	kp_scalar_set(g, KP_POWER_DIRTY, 0);
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
	update_power(g);
	kp_scalar_set(g, KP_HIT_SLOT, (kp_scalar(g, KP_HIT_SLOT) + 1) % HIT_SLOTS); /* AdvanceHitHistorySlot 0x40a880 */
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
	if (kp_scalar(g, KP_BULLETS_ACTIVE) == 1) kp_game_fail(g, "step: bullets (0x40a5f0)");
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

void kp_run_turn(KpGame *g, const KpCommand *commands, int count)
{
	int k;
	for (k = 0; k < count; k++) kp_execute_command(g, commands[k].player, commands[k].bytes, commands[k].size);
	kp_scalar_set(g, KP_TURN, kp_scalar(g, KP_TURN) + 1);
}

void kp_run_frame(KpGame *g, const KpCommand *commands, int count)
{
	kp_run_turn(g, commands, count);
	kp_scalar_set(g, KP_FRAME, kp_scalar(g, KP_FRAME) + 1);
	kp_step_frame(g);
}

/* --- the trigger clocks (game/triggers/clock.lua) --- */

#define SECOND_STEPS 0xf
#define TRIGGER_PERIOD 0x1e
#define RESULT_PERIOD 0x2d
#define LEADERBOARD_PERIOD 0xf
#define RESULT_VICTORY 2
#define RESULT_DEFEAT 3
#define RESULT_DRAW 5

void kp_trigger_clock_init(KpGame *g, int has_triggers)
{
	KpTriggerClock *c = &g->trigger_clock;
	int p;
	kp_scalar_set(g, KP_ELAPSED, 0);
	c->second = SECOND_STEPS;
	c->pass = 1;
	c->result = 0;
	c->leaderboard = LEADERBOARD_PERIOD;
	c->game_timer = 0;
	c->countdowns = 0;
	c->leaderboard_kind = 0;
	kp_scalar_set(g, KP_HAS_TRIGGERS, has_triggers != 0);
	for (p = 0; p <= 7; p++) g->results[p] = 0;
}

/* A counter tested before its decrement: true when it was 0. */
static int expired(int64_t *c, int64_t period, int64_t bits)
{
	int64_t was = *c;
	*c = kp_mod(was - 1, bits);
	if (was == 0) {
		*c = period;
		return 1;
	}
	return 0;
}

void kp_trigger_step(KpGame *g)
{
	KpTriggerClock *c = &g->trigger_clock;
	/* TickResultDialogTimer 0x4be340 (not in a replay). */
	if (expired(&c->result, RESULT_PERIOD, 65536)) {
		int64_t r = g->local_player >= 0 && g->local_player <= 7 ? g->results[g->local_player] : KP_NIL;
		if (r == RESULT_VICTORY || r == RESULT_DEFEAT || r == RESULT_DRAW) {
			kp_game_fail(g, "triggers: game result dialog (0x4bcf80, 0x4bcff0)");
		}
	}
	/* TickTriggerClocks 0x4be410. */
	if (c->countdowns != 0) kp_game_fail(g, "triggers: player countdowns (0x4be410)");
	if (expired(&c->leaderboard, LEADERBOARD_PERIOD, 65536) && c->leaderboard_kind != 0) {
		kp_game_fail(g, "triggers: leaderboard (0x4ba910)");
	}
	if (expired(&c->second, SECOND_STEPS, 65536)) {
		kp_scalar_set(g, KP_ELAPSED, kp_scalar(g, KP_ELAPSED) + 1);
		if (c->game_timer != 0) kp_game_fail(g, "triggers: game countdown timer (0x4bde30)");
	}
	if (expired(&c->pass, TRIGGER_PERIOD, 65536) && kp_scalar(g, KP_HAS_TRIGGERS) == 1) {
		kp_game_fail(g, "triggers: trigger pass (0x4bdfd0)");
	}
}

/* --- the AI step (game/ai/step.lua) and the guard records (game/ai/guard.lua) --- */

#define CONTROLLER_COMPUTER 1
#define REGION_STEPS 0x14d
#define REGION_CYCLE 0x9c4
#define AI_PLAYERS 8
#define UNDER_ATTACK_PERIOD 300
#define UNDER_ATTACK_ROUNDS 6
#define STATUS_UNDER_ATTACK 0x80
#define SPENDING_EARLY 7
#define SPENDING_LATE 37
#define EARLY_SECONDS 0x1194
#define ATTACK_EVALUATION_SECONDS 0x167
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
	ai->scripts = 0;
}

static int computer(KpGame *g, int64_t p) { return KP_GET(g, "players", p, "controller", KP_NIL) == CONTROLLER_COMPUTER; }

/* UpdateAiRegions 0x4b0d70. */
static void update_regions(KpGame *g)
{
	KpAi *ai = &g->ai;
	int64_t count = (int64_t)g->regions.count;
	int k, p;
	for (k = 1; k <= REGION_STEPS; k++) {
		if (ai->region_cursor < count && computer(g, ai->region_player)) {
			kp_game_fail(g, "ai: region update of a computer player (0x4b0ce0)");
		}
		ai->region_cursor++;
		if (ai->region_cursor == REGION_CYCLE) {
			ai->region_player = (ai->region_player + 1) % AI_PLAYERS;
			ai->region_cursor = 1;
			/* RebuildEnemyStrengthMaps 0x4abf90 writes only the AI regions of computer players.
			 * TODO: the AI region data itself (phase 5); nothing before the AI reads it. */
			for (p = 0; p < AI_PLAYERS; p++) {
				if (computer(g, p)) kp_game_fail(g, "ai: enemy strength maps (0x4abf90)");
			}
			if (computer(g, ai->region_player)) kp_game_fail(g, "ai: region refresh (0x4831e0, 0x4aa8f0)");
		}
	}
}

/* RefreshResourceAreas 0x496c80 on the areas of GenerateResourceAreas 0x497a20. */
static void refresh_resource_areas(KpGame *g)
{
	if (kp_scalar(g, KP_RESOURCE_AREAS) == KP_NIL) {
		int64_t n = g->lists[KP_LIST_ACTIVE].head, found = 0;
		while (n != 0) {
			if (kp_band(KP_DAT(g, units, special_ability_flags, g->u.type[n]), DAT_RESOURCE_CONTAINER) != 0) found = 1;
			n = g->u.next[n];
		}
		kp_scalar_set(g, KP_RESOURCE_AREAS, found);
	}
	if (kp_scalar(g, KP_RESOURCE_AREAS) == 1) kp_game_fail(g, "ai: resource areas (0x497a20, 0x496c80)");
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
	if (computer(g, ai->spending_player)) kp_game_fail(g, "ai: spending pass (0x483400)");
	ai->town_player = (ai->town_player + 1) % AI_PLAYERS; /* MaintainNextPlayerTowns 0x486c80 */
	if (kp_scalar(g, KP_ELAPSED) > ATTACK_EVALUATION_SECONDS) kp_game_fail(g, "ai: attack evaluation (0x4acdf0)");
}

void kp_ai_step(KpGame *g)
{
	if (g->ai.scripts != 0) kp_game_fail(g, "ai: scripts (0x4938e0)");
	update_regions(g);
	refresh_resource_areas(g);
	step_timers(g);
}

#define GUARD_NONE (-1)
#define KIND_GUARD 1
#define AI_NO_GUARD 0x2

void kp_guard_init(KpGame *g)
{
	KpGuards *gd = &g->guards;
	int64_t k;
	int p;
	for (k = 0; k < KP_GUARDS; k++) {
		gd->next[k] = k + 1 < KP_GUARDS ? k + 1 : GUARD_NONE;
		gd->prev[k] = k > 0 ? k - 1 : GUARD_NONE;
		gd->kind[k] = gd->unit[k] = gd->type[k] = gd->flag9[k] = 0;
		gd->x1[k] = gd->y1[k] = gd->x2[k] = gd->y2[k] = 0;
	}
	gd->free = 0;
	for (p = 0; p <= 7; p++) gd->heads[p] = GUARD_NONE;
}

void kp_guard_attach(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	KpGuards *gd = &g->guards;
	int64_t p = u->owner[n], t, x, y, k;
	int32_t s;
	if (!computer(g, p)) return;
	t = u->type[n];
	if (kp_band(KP_DAT(g, units, ai_internal, t), AI_NO_GUARD) != 0) return;
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
		k = gd->free;
		if (k == GUARD_NONE) return;
		gd->free = gd->next[k];
		if (gd->free != GUARD_NONE) gd->prev[gd->free] = GUARD_NONE;
		gd->prev[k] = GUARD_NONE;
		gd->next[k] = gd->heads[p];
		if (gd->heads[p] != GUARD_NONE) gd->prev[gd->heads[p]] = k;
		gd->heads[p] = k;
		gd->kind[k] = KIND_GUARD;
		gd->type[k] = t;
	}
	gd->unit[k] = n;
	gd->x1[k] = gd->x2[k] = x;
	gd->y1[k] = gd->y2[k] = y;
	gd->flag9[k] = 0;
	u->ai[n] = k + 1;
}
