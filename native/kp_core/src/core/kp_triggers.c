/* The trigger engine (plan phase 5, docs/phase5.md): the state of InitTriggerState 0x4be240, the owner expansion of
 * the TRIG records (0x4bdb00), the scheduler of a frame (RunTriggerScheduler 0x4be500) with its clocks (0x4be340,
 * 0x4be410), the list runner (0x4bdfd0), the condition evaluator (0x4bded0), the action runner (0x4bdf10), the
 * pending results (0x4be020), and the conditions and actions of the TRIG tables 0x50e058 and 0x50df68 (atlas P40,
 * P41; docs/research/native-triggers.md). A copy keeps its whole 2400-byte record, as the original's node does: its
 * execution flags and continuation byte and the flags of its actions change in place. */
#include "kp_game_internal.h"

#include <stdlib.h>
#include <string.h>

/* condition fields */
#define C_LOCATION 0x00
#define C_PLAYER 0x04
#define C_NUMBER 0x08
#define C_UNIT 0x0c
#define C_COMPARISON 0x0e
#define C_TYPE 0x0f
#define C_RESOURCE 0x10
#define C_FLAGS 0x11
/* execution flags (BWAPI's names) */
#define EXEC_RUNNING 0x01
#define EXEC_IGNORE_DEFEAT 0x02
#define EXEC_PRESERVE 0x04
#define EXEC_DISABLED 0x08
#define EXEC_SKIPPING 0x10
#define EXEC_PAUSED_GAME 0x20
#define EXEC_NO_SKIP 0x40
#define EXEC_RESET (EXEC_RUNNING | EXEC_SKIPPING | EXEC_NO_SKIP) /* 0x51: cleared when a preserved trigger ends */
/* the flags of a condition or an action */
#define ENTRY_IGNORED 0x02
#define ACTION_WAITING 0x01
#define ACTION_ALWAYS_TEXT 0x04
/* the comparisons of a condition */
#define AT_LEAST 0
#define AT_MOST 1
#define SWITCH_SET 2
#define SWITCH_CLEARED 3
#define EXACTLY 10
/* the briefing's actions (table 0x503908) that the first tick stops at or keeps */
#define BRIEFING_WAIT 1
#define BRIEFING_SET_OBJECTIVES 4
#define BRIEFING_TRANSMISSION 8

/* The milliseconds of a frame at each game speed (0x503360, indexed by the speed byte 0x6d3130). */
static const int64_t SPEED_MS[] = { 167, 111, 83, 67, 56, 48, 42, 501 };
#define SPEEDS ((int64_t)(sizeof SPEED_MS / sizeof SPEED_MS[0]))
#define WAIT_FOREVER 0xffffffffLL
#define TEXT_MIN_MS 4000
#define TEXT_MS_PER_CHAR 50
#define TRANSMISSION_FLASH 45 /* the speaker's selection flash (sprite +0x0f) */
#define PING_COLOR 0x11
/* the 640 x 400 game screen the Center View pan frames (0x4b6350) */
#define SCREEN_W 640
#define SCREEN_H 400
#define SCREEN_HALF_W 320
#define SCREEN_HALF_H 200
#define SECOND_STEPS 15
#define PASS_STEPS 30
#define RESULT_STEPS 45
#define LEADERBOARD_STEPS 15
#define COUNTDOWN_MAX 359999
#define SWITCHES 256
#define LEADERBOARD_NONE 0xffffffffLL /* the entries' 0xff fill */
#define RNG_RANDOM_SWITCH 57
/* the Escape skip works in the original campaigns only (mission index 0x526524 < 32, 0x4bda80) */
#define SKIPPABLE_MISSIONS 32
/* the results */
#define RESULT_DEFEAT 2
#define RESULT_VICTORY 3
#define RESULT_IGNORE_DEFEAT 4
#define RESULT_DRAW 5
/* the cheats of ApplyPendingResults 0x4be020: "there is no cow level", "game over man", "staying alive" */
#define CHEAT_VICTORY 0x80
#define CHEAT_DEFEAT 0x40
#define CHEAT_NO_RESULT 0x100

/* The handlers of the condition table 0x50e058 and the action table 0x50df68 (the addresses a failure names). */
static const uint32_t CONDITION_HANDLERS[] = {
	0x4b8e70, 0x4b8e90, 0x4b8ed0, 0x4b8f20, 0x4b8f80, 0x4b8fd0, 0x4b9070, 0x4b90e0, 0x4b9160, 0x4b91c0, 0x4b9220, 0x4b9280,
	0x4b92e0, 0x4b8e80, 0x4b9320, 0x4b9360, 0x4b93b0, 0x4b9420, 0x4b94a0, 0x4b9510, 0x4b9580, 0x4b9020, 0x4b8e70, 0x4b8e80,
};
static const uint32_t ACTION_HANDLERS[] = {
	0x4b8e70, 0x4b5ef0, 0x4b5f20, 0x4b5fc0, 0x4b5fd0, 0x4b6020, 0x4b6080, 0x4b60e0, 0x4b6240, 0x4b6300, 0x4b6350, 0x4b64d0,
	0x4b6670, 0x4b6690, 0x4b6720, 0x4b6760, 0x4b6780, 0x4b67c0, 0x4b67c0, 0x4b67c0, 0x4b67c0, 0x4b67c0, 0x4b6840, 0x4b6870,
	0x4b68c0, 0x4b68f0, 0x4b6940, 0x4b6990, 0x4b69e0, 0x4b5620, 0x4b5660, 0x4b5690, 0x4b56b0, 0x4b67c0, 0x4b67c0, 0x4b67c0,
	0x4b67c0, 0x4b67c0, 0x4b56f0, 0x4b5840, 0x4b67c0, 0x4b5900, 0x4b5930, 0x4b59c0, 0x4b64d0, 0x4b6550, 0x4b65a0, 0x4b8e70,
	0x4b5a50, 0x4b5b00, 0x4b5bb0, 0x4b5c60, 0x4b5d10, 0x4b5dc0, 0x4b5e70, 0x4b5e80, 0x4b5f70, 0x4b5e90, 0x4b5ed0, 0x4b5ee0,
};
/* the tables cover every id */
typedef char kp_condition_table_check[sizeof CONDITION_HANDLERS / sizeof CONDITION_HANDLERS[0] == KP_COND_TYPES ? 1 : -1];
typedef char kp_action_table_check[sizeof ACTION_HANDLERS / sizeof ACTION_HANDLERS[0] == KP_ACT_TYPES ? 1 : -1];

static uint8_t *record(KpGame *g, int64_t copy) { return g->trig.records + (size_t)copy * KP_TRIGGER_RECORD; }
static uint32_t u32_at(const uint8_t *p) { return kp_le_u32(p); }
static void put_u32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
	p[2] = (uint8_t)(v >> 16);
	p[3] = (uint8_t)(v >> 24);
}
static uint32_t exec_flags(KpGame *g, int64_t copy) { return u32_at(record(g, copy) + KP_TRIG_EXECUTION_FLAGS); }
static void set_exec_flags(KpGame *g, int64_t copy, uint32_t v) { put_u32(record(g, copy) + KP_TRIG_EXECUTION_FLAGS, v); }

/* The current trigger (0x6ca790) and its execution flags. */
static uint32_t current_flags(KpGame *g) { return exec_flags(g, g->trig.current); }
static void current_flags_or(KpGame *g, uint32_t bits) { set_exec_flags(g, g->trig.current, current_flags(g) | bits); }
static void current_flags_clear(KpGame *g, uint32_t bits) { set_exec_flags(g, g->trig.current, current_flags(g) & ~bits); }
/* IsCurrentTriggerSkipping 0x4bd560 */
static int skipping(KpGame *g) { return (current_flags(g) & EXEC_SKIPPING) != 0; }

/* IsNonHumanActiveSlotType 0x479580: computer, rescuable, unused and neutral controllers. */
static int computer_like(KpGame *g, int64_t p)
{
	int64_t c = kp_controller(g, p);
	return c == KP_CTRL_COMPUTER || c == KP_CTRL_RESCUE || c == KP_CTRL_RESCUE_ACTIVE || c == KP_CTRL_NEUTRAL;
}

/* The local player's slot when it is one of the eight trigger players, else -1. */
static int64_t local_slot(KpGame *g)
{
	int64_t p = g->local_player;
	return p >= 0 && p < KP_TRIGGER_PLAYERS ? p : -1;
}

/* The test of FirstActivePlayer 0x4b3160 / NextActivePlayer 0x4b3170 of a single-player game: a slot 0-7 of
 * controller 1, 2, 3, 4 or 7 that is not finished (0x4bd590). */
int kp_trigger_player_active(KpGame *g, int64_t p)
{
	int64_t c;
	if (p < 0 || p >= 8) return 0;
	c = kp_controller(g, p);
	return (c == KP_CTRL_COMPUTER || c == KP_CTRL_HUMAN || c == KP_CTRL_RESCUE || c == KP_CTRL_RESCUE_ACTIVE
		|| c == KP_CTRL_NEUTRAL) && !kp_player_finished(g, p);
}

/* The next active slot after *cursor, 8 at the end. */
static int64_t next_active(KpGame *g, int64_t *cursor)
{
	while (++*cursor < 8) {
		if (kp_trigger_player_active(g, *cursor)) return *cursor;
	}
	return 8;
}
static int64_t first_active(KpGame *g, int64_t *cursor)
{
	*cursor = -1;
	return next_active(g, cursor);
}

/* The comparison of a condition (unsigned 32-bit): 0 at least, 1 at most, 10 exactly, anything else false. */
static int compare(int64_t value, const uint8_t *c)
{
	uint32_t v = (uint32_t)value, n = u32_at(c + C_NUMBER);
	switch (c[C_COMPARISON]) {
	case AT_LEAST: return v >= n;
	case AT_MOST: return v <= n;
	case EXACTLY: return v == n;
	default: return 0;
	}
}

/* --- the state and the owner expansion --- */

/* Whether owner byte i of a record names active player p: a direct slot 0-12, All Players or p's force. */
static int owner_names(KpGame *g, int i, int64_t p)
{
	return i == p || i == KP_SEL_ALL
		|| (i >= KP_SEL_FORCE_FIRST && i <= KP_SEL_FORCE_LAST && g->players.force[p] == i - KP_SEL_ALL);
}

static int64_t copies_of(KpGame *g, const uint8_t *raw)
{
	int64_t count = 0, cursor, p;
	int i;
	for (i = 0; i < KP_TRIG_OWNERS; i++) {
		if (raw[KP_TRIG_OWNERS_AT + i] == 0) continue;
		for (p = first_active(g, &cursor); p < 8; p = next_active(g, &cursor)) count += owner_names(g, i, p);
	}
	return count;
}

/* ExpandTriggerOwners 0x4bdb00: for each owner byte in order, a copy appended to each active player it names (a
 * direct slot 0-12, All Players or a force). */
static void expand(KpGame *g, const uint8_t *raw, int64_t *tail, int64_t *at)
{
	KpTriggers *t = &g->trig;
	int64_t cursor, p;
	int i;
	for (i = 0; i < KP_TRIG_OWNERS; i++) {
		if (raw[KP_TRIG_OWNERS_AT + i] == 0) continue;
		for (p = first_active(g, &cursor); p < 8; p = next_active(g, &cursor)) {
			if (!owner_names(g, i, p)) continue;
			memcpy(record(g, *at), raw, KP_TRIGGER_RECORD);
			t->next[*at] = -1;
			t->place[*at] = tail[p] < 0 ? 0 : t->place[tail[p]] + 1;
			if (tail[p] < 0) t->head[p] = *at;
			else t->next[tail[p]] = *at;
			tail[p] = *at;
			++*at;
		}
	}
}

/* The objectives the briefing sets for the local player when Start is pressed at once (the emulator's runs): the ready
 * room's first tick (0x460ea0) runs the first copy of the local player's briefing list (the owner expansion 0x4607e0:
 * its slot, All Players, its force) when its first condition is Mission Briefing (0x44fee0), up to the first Wait or
 * Transmission, which suspend it; each Set Mission Objectives (0x44fdc0) replaces the last. 0 for none. */
static int64_t briefing_lead(KpGame *g, const KpBytes *mbrf)
{
	int64_t local = local_slot(g), lead = 0;
	uint32_t k, records = mbrf->size / KP_TRIGGER_RECORD;
	int i;
	if (local < 0) return 0;
	for (k = 0; k < records; k++) {
		const uint8_t *r = mbrf->data + (size_t)k * KP_TRIGGER_RECORD;
		for (i = 0; i < KP_TRIG_OWNERS; i++) {
			if (r[KP_TRIG_OWNERS_AT + i] != 0 && owner_names(g, i, local)) break;
		}
		if (i == KP_TRIG_OWNERS) continue;
		if (r[C_TYPE] != KP_COND_MISSION_BRIEFING) return 0;
		for (i = 0; i < KP_TRIG_ACTIONS; i++) {
			const uint8_t *a = r + KP_TRIG_ACTIONS_AT + KP_TRIG_ACTION_SIZE * i;
			if ((a[KP_ACTION_FLAGS] & ENTRY_IGNORED) != 0) continue;
			if (a[KP_ACTION_TYPE] == 0 || a[KP_ACTION_TYPE] == BRIEFING_WAIT || a[KP_ACTION_TYPE] == BRIEFING_TRANSMISSION) break;
			if (a[KP_ACTION_TYPE] == BRIEFING_SET_OBJECTIVES) lead = u32_at(a + KP_ACTION_STRING);
		}
		return lead;
	}
	return 0;
}

void kp_triggers_init(KpGame *g, const KpMission *m)
{
	KpTriggers *t = &g->trig;
	int64_t cursor, p, at = 0, tail[KP_TRIGGER_PLAYERS], count = 0;
	uint32_t k, records = m->triggers.size / KP_TRIGGER_RECORD;
	int i;
	if (m->triggers.size % KP_TRIGGER_RECORD != 0) {
		kp_game_fail(g, "triggers: a TRIG section of %u bytes (0x468de0)", (unsigned)m->triggers.size);
	}
	/* InitTriggerState 0x4be240 of a new game */
	kp_scalar_set(g, KP_ELAPSED, 0);
	t->countdown = t->countdown_paused = 0;
	memset(t->switches, 0, sizeof t->switches);
	t->lb_computers = 1;
	t->lb_action = t->lb_location = t->lb_type = 0;
	t->lb_goal = 0;
	t->lb_label = 0;
	for (p = 0; p < 8; p++) {
		g->results[p] = 0;
		t->participants[p] = 0;
		t->wait_active[p] = t->wait_remaining[p] = 0;
		t->pending[p] = 0;
		t->objectives[p] = 0;
		t->lb_entries[p][0] = t->lb_entries[p][1] = LEADERBOARD_NONE;
		t->head[p] = -1;
		tail[p] = -1;
	}
	for (p = first_active(g, &cursor); p < 8; p = next_active(g, &cursor)) t->participants[p] = 1;
	t->paused = 0;
	t->result_dialog = 0;
	t->second = SECOND_STEPS;
	t->leaderboard = LEADERBOARD_STEPS;
	t->pass = 1;
	t->result = 0;
	t->player = 0;
	t->current = -1;
	t->quiet = 0;
	memset(t->next_scenario, 0, sizeof t->next_scenario);
	kp_scalar_set(g, KP_PAUSED, 0);
	/* MRGN: 64 records (version 59) or 255 (0x468d50, 0x468d80); the others stay 0 */
	memset(t->locations, 0, sizeof t->locations);
	for (k = 0; k < m->locations.size / 20 && k < KP_LOCATIONS; k++) {
		const uint8_t *r = m->locations.data + 20 * k;
		for (i = 0; i < 4; i++) t->locations[k][i] = (int32_t)kp_le_u32(r + 4 * i);
		t->locations[k][KP_LOC_NAME] = kp_le_u16(r + 16);
		t->locations[k][KP_LOC_ELEVATION] = kp_le_u16(r + 18);
	}
	/* UPRP: 64 records (0x468db0) */
	memset(t->properties, 0, sizeof t->properties);
	memcpy(t->properties, m->properties.data,
		m->properties.size < sizeof t->properties ? m->properties.size : sizeof t->properties);
	t->strings = (uint8_t *)malloc(m->strings.size ? m->strings.size : 1);
	if (!t->strings) kp_game_fail(g, "triggers: out of memory");
	memcpy(t->strings, m->strings.data, m->strings.size);
	t->strings_size = m->strings.size;
	/* LoadTriggerSection 0x468de0: the records in order */
	for (k = 0; k < records; k++) count += copies_of(g, m->triggers.data + (size_t)k * KP_TRIGGER_RECORD);
	t->records = (uint8_t *)malloc((size_t)(count ? count : 1) * KP_TRIGGER_RECORD);
	t->next = (int64_t *)malloc((size_t)(count ? count : 1) * sizeof *t->next);
	t->place = (int64_t *)malloc((size_t)(count ? count : 1) * sizeof *t->place);
	if (!t->records || !t->next || !t->place) kp_game_fail(g, "triggers: out of memory");
	t->count = count;
	for (k = 0; k < records; k++) expand(g, m->triggers.data + (size_t)k * KP_TRIGGER_RECORD, tail, &at);
	t->lead_objectives = briefing_lead(g, &m->briefing);
}

void kp_triggers_free(KpGame *g)
{
	free(g->trig.records);
	free(g->trig.next);
	free(g->trig.place);
	free(g->trig.strings);
	g->trig.records = g->trig.strings = NULL;
	g->trig.next = g->trig.place = NULL;
	g->trig.count = 0;
}

const char *kp_map_string(KpGame *g, int64_t id)
{
	const KpTriggers *t = &g->trig;
	uint32_t n = (uint32_t)(id & 0xffff), count, offset;
	if (n == 0) return NULL;
	if (t->strings_size < 2) return "";
	count = kp_le_u16(t->strings);
	if (n > count || 2 * n + 2 > t->strings_size) return "";
	offset = kp_le_u16(t->strings + 2 * n);
	if (offset >= t->strings_size || memchr(t->strings + offset, 0, t->strings_size - offset) == NULL) {
		kp_game_fail(g, "triggers: map string %u is not terminated inside STR", (unsigned)n);
	}
	return (const char *)t->strings + offset;
}

/* --- the conditions (table 0x50e058) --- */

static int condition(KpGame *g, const uint8_t *c)
{
	KpTriggers *t = &g->trig;
	int type = c[C_TYPE];
	switch (type) {
	case KP_COND_NONE: case KP_COND_ALWAYS: /* ReturnTrue 0x4b8e70: none, Always */
		return 1;
	case KP_COND_MISSION_BRIEFING: case KP_COND_NEVER: /* ReturnFalse 0x4b8e80: Mission Briefing, Never */
		return 0;
	case KP_COND_COUNTDOWN: /* TrigCondCountdownTimer 0x4b8e90 */
		return compare(t->countdown, c);
	case KP_COND_SWITCH: { /* TrigCondSwitch 0x4b9280 */
		int s = c[C_RESOURCE];
		int64_t bit = t->switches[s >> 5] & ((int64_t)1 << (s & 31));
		if (c[C_COMPARISON] == SWITCH_SET) return bit != 0;
		if (c[C_COMPARISON] == SWITCH_CLEARED) return bit == 0;
		return 0;
	}
	case KP_COND_ELAPSED_TIME: /* TrigCondElapsedTime 0x4b92e0 */
		return compare(kp_scalar(g, KP_ELAPSED), c);
	case KP_COND_COMMAND: /* TrigCondCommand 0x4b8ed0: at most counts all units, the others completed ones */
		return compare(kp_trigger_unit_count(g, -1, u32_at(c + C_PLAYER), kp_le_u16(c + C_UNIT),
			c[C_COMPARISON] != AT_MOST), c);
	case KP_COND_BRING: /* TrigCondBring 0x4b8f20 */
		if (c[C_LOCATION] == 0) return 0;
		return compare(kp_trigger_unit_count(g, c[C_LOCATION] - 1, u32_at(c + C_PLAYER), kp_le_u16(c + C_UNIT),
			c[C_COMPARISON] != AT_MOST), c);
	case KP_COND_ACCUMULATE: /* TrigCondAccumulate 0x4b8f80 */
		return compare(kp_trigger_resources(g, u32_at(c + C_PLAYER), c[C_RESOURCE]), c);
	case KP_COND_KILL: /* TrigCondKill 0x4b8fd0 */
		return compare(kp_trigger_kills(g, u32_at(c + C_PLAYER), kp_le_u16(c + C_UNIT)), c);
	case KP_COND_DEATHS: /* TrigCondDeaths 0x4b9360 */
		return compare(kp_trigger_deaths(g, u32_at(c + C_PLAYER), kp_le_u16(c + C_UNIT)), c);
	case KP_COND_SCORE: /* TrigCondScore 0x4b9020 */
		return compare(kp_trigger_score(g, u32_at(c + C_PLAYER), c[C_RESOURCE]), c);
	case KP_COND_COMMAND_THE_MOST: case KP_COND_COMMAND_THE_LEAST: /* TrigCondCommandTheMost 0x4b9070, TrigCondCommandTheLeast 0x4b93b0 */
		return kp_trigger_unit_ranking(g, -1, kp_le_u16(c + C_UNIT), type == KP_COND_COMMAND_THE_MOST);
	case KP_COND_COMMANDS_THE_MOST_AT: case KP_COND_COMMAND_THE_LEAST_AT: /* TrigCondCommandsTheMostAt 0x4b90e0, TrigCondCommandTheLeastAt 0x4b9420 */
		if (c[C_LOCATION] == 0) return 0;
		return kp_trigger_unit_ranking(g, c[C_LOCATION] - 1, kp_le_u16(c + C_UNIT), type == KP_COND_COMMANDS_THE_MOST_AT);
	case KP_COND_MOST_KILLS: case KP_COND_LEAST_KILLS: /* TrigCondMostKills 0x4b9160, TrigCondLeastKills 0x4b94a0 */
		return kp_trigger_kills_ranking(g, kp_le_u16(c + C_UNIT), type == KP_COND_MOST_KILLS);
	case KP_COND_HIGHEST_SCORE: case KP_COND_LOWEST_SCORE: /* TrigCondHighestScore 0x4b91c0, TrigCondLowestScore 0x4b9510 */
		return kp_trigger_score_ranking(g, c[C_RESOURCE], type == KP_COND_HIGHEST_SCORE);
	case KP_COND_MOST_RESOURCES: case KP_COND_LEAST_RESOURCES: /* TrigCondMostResources 0x4b9220, TrigCondLeastResources 0x4b9580 */
		return kp_trigger_resources_ranking(g, c[C_RESOURCE], type == KP_COND_MOST_RESOURCES);
	default:
		if (type >= KP_COND_TYPES) kp_game_fail(g, "triggers: condition type %d (table 0x50e058)", type);
		/* TODO: Opponents (CountOpponents 0x4ba060) when a map that needs it comes into scope; outside the V1 profile
		 * the converter rejects it (converter/analyze/triggers.py UNSUPPORTED_CONDITIONS) */
		kp_game_fail(g, "triggers: condition %d (0x%x)", type, (unsigned)CONDITION_HANDLERS[type]);
		return 0;
	}
}

/* EvaluateTriggerConditions 0x4bded0: up to 16 conditions; an ignored one is skipped, type 0 ends them true. */
static int conditions_hold(KpGame *g, int64_t copy)
{
	int k;
	for (k = 0; k < KP_TRIG_CONDITIONS; k++) {
		const uint8_t *c = record(g, copy) + KP_TRIG_CONDITION_SIZE * k;
		if ((c[C_FLAGS] & ENTRY_IGNORED) != 0) continue;
		if (c[C_TYPE] == 0) return 1;
		if (!condition(g, c)) return 0;
	}
	return 1;
}

/* --- the actions (table 0x50df68) --- */

/* SetPendingResult 0x4bd900: only for a computer or a human. */
static void set_pending(KpGame *g, int64_t p, int64_t v)
{
	int64_t c = kp_controller(g, p);
	if (c == KP_CTRL_COMPUTER || c == KP_CTRL_HUMAN) g->trig.pending[p] = v;
}

/* The result dialog's delay after an outcome: 1 when a trigger paused the game (0x4be1c0), else 45 (0x4be3d0). */
static void result_delay(KpGame *g) { g->trig.result = g->trig.paused ? 1 : RESULT_STEPS; }

/* TrigActDefeat 0x4b5f20 and TrigActDraw 0x4b5f70: not over a pending victory or ignore-defeat. */
static int lose_or_draw(KpGame *g, int64_t result)
{
	int64_t p = g->trig.player, now = g->trig.pending[p];
	if (now == RESULT_VICTORY || now == RESULT_IGNORE_DEFEAT) return 0;
	set_pending(g, p, result);
	result_delay(g);
	return 1;
}

/* TrigActWait 0x4b5fd0 on the player's single timer. */
static int wait_action(KpGame *g, uint8_t *a)
{
	KpTriggers *t = &g->trig;
	int64_t p = t->player;
	if (t->wait_active[p] != 0) return 0;
	if ((a[KP_ACTION_FLAGS] & ACTION_WAITING) != 0) {
		a[KP_ACTION_FLAGS] &= (uint8_t)~ACTION_WAITING;
		return 1;
	}
	if (skipping(g)) return 1;
	/* StartPlayerWait 0x4bda60 */
	t->wait_active[p] = 1;
	t->wait_remaining[p] = u32_at(a + KP_ACTION_TIME);
	a[KP_ACTION_FLAGS] |= ACTION_WAITING;
	return 0;
}

/* TrigActSetSwitch 0x4b6690 */
static void set_switch(KpGame *g, const uint8_t *a)
{
	uint32_t s = u32_at(a + KP_ACTION_NUMBER);
	int64_t *word, bit;
	if (s >= SWITCHES) return;
	word = &g->trig.switches[s >> 5];
	bit = (int64_t)1 << (s & 31);
	switch (a[KP_ACTION_MODIFIER]) {
	case KP_MOD_SET: *word |= bit; break;
	case KP_MOD_CLEAR: *word &= ~bit; break;
	case KP_MOD_TOGGLE: *word ^= bit; break;
	case KP_MOD_RANDOMIZE:
		if ((kp_rng_game_raw(g->rng, RNG_RANDOM_SWITCH) & 0x80) != 0) *word |= bit;
		else *word &= ~bit;
		break;
	default: break;
	}
}

/* TrigActSetCountdownTimer 0x4b6720: 32-bit arithmetic, clamped to [0, 359999] as a signed value. */
static void set_countdown(KpGame *g, const uint8_t *a)
{
	uint32_t v = (uint32_t)g->trig.countdown, time = u32_at(a + KP_ACTION_TIME);
	int32_t s;
	switch (a[KP_ACTION_MODIFIER]) {
	case KP_MOD_SET_TO: v = time; break;
	case KP_MOD_ADD: v += time; break;
	case KP_MOD_SUBTRACT: v -= time; break;
	default: break;
	}
	s = (int32_t)v;
	g->trig.countdown = s < 0 ? 0 : s > COUNTDOWN_MAX ? COUNTDOWN_MAX : s;
}

/* TrigActLeaderboardComputerPlayers 0x4b56b0 */
static void leaderboard_computers(KpGame *g, const uint8_t *a)
{
	switch (a[KP_ACTION_MODIFIER]) {
	case KP_MOD_SET: g->trig.lb_computers = 1; break;
	case KP_MOD_CLEAR: g->trig.lb_computers = 0; break;
	case KP_MOD_TOGGLE: g->trig.lb_computers ^= 1; break;
	default: break;
	}
}

/* TrigActSetNextScenario 0x4b5900: SStrCopy of the string into the 32-byte name 0x526526. */
static void set_next_scenario(KpGame *g, const uint8_t *a)
{
	const char *s = kp_map_string(g, u32_at(a + KP_ACTION_STRING));
	size_t n;
	if (s == NULL) return; /* no string: an id whose low WORD is 0 */
	n = strlen(s);
	if (n > KP_NEXT_SCENARIO - 1) n = KP_NEXT_SCENARIO - 1;
	memset(g->trig.next_scenario, 0, sizeof g->trig.next_scenario);
	memcpy(g->trig.next_scenario, s, n);
}

/* TrigActMoveLocation 0x4b56f0: the destination location (1-255) centred on the first matching unit at the source
 * location, or on the source's center, then pushed back inside the map. */
static void move_location(KpGame *g, const uint8_t *a)
{
	uint32_t dest = u32_at(a + KP_ACTION_NUMBER);
	int64_t source = a[KP_ACTION_LOCATION], x, y, unit, *l, dx, dy, w = g->map_w_px, h = g->map_h_px;
	if (source == 0 || dest < 1 || dest > KP_LOCATIONS) return;
	/* GetTriggerUnitOrLocationCenter 0x4b7330 */
	unit = kp_trigger_unit_at(g, source - 1, u32_at(a + KP_ACTION_PLAYER), kp_le_u16(a + KP_ACTION_UNIT));
	if (unit != 0) {
		int32_t s = kp_unit_sprite(g, unit);
		x = kp_s16v(g->w->spr.x[s]);
		y = kp_s16v(g->w->spr.y[s]);
	} else {
		kp_location_center(g, source - 1, &x, &y);
	}
	l = g->trig.locations[dest - 1];
	dx = kp_s32v(x - kp_s32v(kp_s32v(l[KP_LOC_LEFT] + l[KP_LOC_RIGHT]) / 2));
	l[KP_LOC_RIGHT] = kp_s32v(l[KP_LOC_RIGHT] + dx);
	l[KP_LOC_LEFT] = kp_s32v(l[KP_LOC_LEFT] + dx);
	dy = kp_s32v(y - kp_s32v(kp_s32v(l[KP_LOC_TOP] + l[KP_LOC_BOTTOM]) / 2));
	l[KP_LOC_TOP] = kp_s32v(l[KP_LOC_TOP] + dy);
	l[KP_LOC_BOTTOM] = kp_s32v(l[KP_LOC_BOTTOM] + dy);
	if (l[KP_LOC_LEFT] < 0) {
		l[KP_LOC_RIGHT] = kp_s32v(l[KP_LOC_RIGHT] - l[KP_LOC_LEFT]);
		l[KP_LOC_LEFT] = 0;
	}
	if (l[KP_LOC_TOP] < 0) {
		l[KP_LOC_BOTTOM] = kp_s32v(l[KP_LOC_BOTTOM] - l[KP_LOC_TOP]);
		l[KP_LOC_TOP] = 0;
	}
	if (l[KP_LOC_RIGHT] >= w) {
		l[KP_LOC_LEFT] = kp_s32v(l[KP_LOC_LEFT] + w - l[KP_LOC_RIGHT] - 1);
		l[KP_LOC_RIGHT] = w - 1;
	}
	if (l[KP_LOC_BOTTOM] >= h) {
		l[KP_LOC_TOP] = kp_s32v(l[KP_LOC_TOP] + h - l[KP_LOC_BOTTOM] - 1);
		l[KP_LOC_BOTTOM] = h - 1;
	}
	kp_trigger_invalidate_counts(g);
}

/* The leaderboard's getters of one slot (GetUnitCount 0x4ba680 of completed units from the prepared cache, GetResources
 * 0x4ba410, GetKills 0x4b9d30, GetScore 0x4ba510). */
enum { LB_COUNT, LB_RESOURCES, LB_KILLS, LB_SCORE };
static int64_t lb_get(KpGame *g, int getter, int64_t p, int64_t type)
{
	switch (getter) {
	case LB_COUNT: return kp_trigger_cached_count(g, p, type, 1);
	case LB_RESOURCES: return kp_trigger_resources(g, p, type);
	case LB_KILLS: return kp_trigger_kills(g, p, type);
	default: return kp_trigger_score(g, p, type);
	}
}

/* The players a leaderboard shows: eligible ones that are human or, with computers included (0x5349e8), any. */
static int lb_shows(KpGame *g, int64_t p) { return kp_controller(g, p) == KP_CTRL_HUMAN || g->trig.lb_computers != 0; }

/* FillLeaderboardValues 0x4bab80 */
static void lb_fill(KpGame *g, int getter)
{
	KpTriggers *t = &g->trig;
	int64_t cursor, p;
	for (p = first_active(g, &cursor); p < 8; p = next_active(g, &cursor)) {
		if (!lb_shows(g, p)) continue;
		t->lb_entries[p][0] = p;
		t->lb_entries[p][1] = lb_get(g, getter, p, t->lb_type);
	}
}

/* AddLeaderboardGoalShortfall 0x4ba890: what each shown player lacks of the goal (unsigned, at least 0) is added. */
static void lb_shortfall(KpGame *g, int getter, int64_t type)
{
	KpTriggers *t = &g->trig;
	int64_t cursor, p, goal = kp_u32v(t->lb_goal);
	for (p = first_active(g, &cursor); p < 8; p = next_active(g, &cursor)) {
		int64_t v;
		if (!lb_shows(g, p)) continue;
		v = lb_get(g, getter, p, type);
		t->lb_entries[p][1] = kp_u32v(t->lb_entries[p][1] + (v < goal ? goal - v : 0));
		t->lb_entries[p][0] = p;
	}
}

/* CompareLeaderboardDescending 0x4bac90 and Ascending 0x4bacd0: empty entries (player -1) last. */
static int lb_compare(const int64_t *a, const int64_t *b, int ascending)
{
	if (a[0] == -1) return b[0] != -1;
	if (b[0] == -1) return -1;
	if (a[1] == b[1]) return 0;
	if (ascending) return b[1] > a[1] ? -1 : 1;
	return a[1] > b[1] ? -1 : 1;
}

/* qsort 0x4d4490 of the eight entries: eight elements take the CRT's shortsort 0x4d47e0, which moves the maximum
 * (a later element only when strictly greater) to the end, repeatedly. */
static void lb_sort(KpGame *g, int ascending)
{
	int64_t (*e)[2] = g->trig.lb_entries;
	int hi, k;
	for (hi = KP_TRIGGER_PLAYERS - 1; hi > 0; hi--) {
		int max = 0;
		int64_t swap[2];
		for (k = 1; k <= hi; k++) {
			if (lb_compare(e[k], e[max], ascending) > 0) max = k;
		}
		swap[0] = e[max][0];
		swap[1] = e[max][1];
		e[max][0] = e[hi][0];
		e[max][1] = e[hi][1];
		e[hi][0] = swap[0];
		e[hi][1] = swap[1];
	}
}

/* UpdateLeaderboard 0x4ba910: every entry {-1, 0}, then the ranking of the leaderboard's type. */
static void update_leaderboard(KpGame *g)
{
	KpTriggers *t = &g->trig;
	int p, ascending = 1;
	for (p = 0; p < KP_TRIGGER_PLAYERS; p++) {
		t->lb_entries[p][0] = -1;
		t->lb_entries[p][1] = 0;
	}
	switch (t->lb_action) {
	case KP_ACT_LB_CONTROL: case KP_ACT_LB_CONTROL_AT: /* Control (At) */
		kp_trigger_prepare_counts(g, t->lb_action == KP_ACT_LB_CONTROL_AT ? t->lb_location : -1);
		lb_fill(g, LB_COUNT);
		ascending = 0;
		break;
	case KP_ACT_LB_RESOURCES: lb_fill(g, LB_RESOURCES); ascending = 0; break;
	case KP_ACT_LB_KILLS: lb_fill(g, LB_KILLS); ascending = 0; break;
	case KP_ACT_LB_POINTS: lb_fill(g, LB_SCORE); ascending = 0; break;
	case KP_ACT_LB_GOAL_CONTROL: case KP_ACT_LB_GOAL_CONTROL_AT: /* Goal Control (At) */
		kp_trigger_prepare_counts(g, t->lb_action == KP_ACT_LB_GOAL_CONTROL_AT ? t->lb_location : -1);
		lb_shortfall(g, LB_COUNT, t->lb_type);
		break;
	case KP_ACT_LB_GOAL_RESOURCES: /* Goal Resources: both are the minerals' and the gas' shortfalls */
		if (t->lb_type == KP_RES_BOTH) {
			lb_shortfall(g, LB_RESOURCES, KP_RES_MINERALS);
			lb_shortfall(g, LB_RESOURCES, KP_RES_GAS);
		} else {
			lb_shortfall(g, LB_RESOURCES, t->lb_type);
		}
		break;
	case KP_ACT_LB_GOAL_KILLS: lb_shortfall(g, LB_KILLS, t->lb_type); break;
	case KP_ACT_LB_GOAL_POINTS: /* Goal Points (AddScoreGoalShortfalls 0x4babf0 for the sums of components) */
		if (t->lb_type == KP_SCORE_TOTAL) {
			static const int64_t PARTS[] = { KP_SCORE_CUSTOM, KP_SCORE_UNITS, KP_SCORE_BUILDINGS, KP_SCORE_KILLS, KP_SCORE_RAZINGS };
			size_t k;
			for (k = 0; k < sizeof PARTS / sizeof PARTS[0]; k++) lb_shortfall(g, LB_SCORE, PARTS[k]);
		} else if (t->lb_type == KP_SCORE_UNITS_BUILDINGS) {
			lb_shortfall(g, LB_SCORE, KP_SCORE_UNITS);
			lb_shortfall(g, LB_SCORE, KP_SCORE_BUILDINGS);
		} else if (t->lb_type == KP_SCORE_KILLS_RAZINGS) {
			lb_shortfall(g, LB_SCORE, KP_SCORE_KILLS);
			lb_shortfall(g, LB_SCORE, KP_SCORE_RAZINGS);
		} else {
			lb_shortfall(g, LB_SCORE, t->lb_type);
		}
		break;
	case KP_ACT_LB_GREED: /* Greed */
		lb_shortfall(g, LB_RESOURCES, KP_RES_MINERALS);
		lb_shortfall(g, LB_RESOURCES, KP_RES_GAS);
		break;
	default:
		return;
	}
	lb_sort(g, ascending);
}

/* TrigActLeaderboard 0x4b67c0 (actions 17-21, 33-37, 40): the board's type, unit or resource type, goal and label;
 * the ranking is refreshed on the second clock tick (0x4bdde0). */
static void set_leaderboard(KpGame *g, const uint8_t *a)
{
	KpTriggers *t = &g->trig;
	int type = a[KP_ACTION_TYPE];
	t->leaderboard = 1;
	if (type == KP_ACT_LB_CONTROL_AT || type == KP_ACT_LB_GOAL_CONTROL_AT) {
		if (a[KP_ACTION_LOCATION] == 0) return;
		t->lb_location = a[KP_ACTION_LOCATION] - 1;
	}
	if (type == KP_ACT_LB_GREED) {
		t->lb_action = type;
		t->lb_goal = u32_at(a + KP_ACTION_NUMBER);
		t->lb_type = KP_RES_BOTH;
		return;
	}
	t->lb_action = type;
	t->lb_type = kp_le_u16(a + KP_ACTION_UNIT);
	t->lb_goal = u32_at(a + KP_ACTION_NUMBER);
	t->lb_label = u32_at(a + KP_ACTION_STRING);
}

/* TrigActPauseGame 0x4b6020 of a single-player game: PauseGame 0x47cbb0 under a trigger (the accelerators and the
 * cursor layer are the interface's). */
static void pause_game(KpGame *g)
{
	KpTriggers *t = &g->trig;
	if (t->paused != 0 || computer_like(g, t->player) || skipping(g)) return;
	if (kp_scalar(g, KP_PAUSED) != 1) kp_scalar_set(g, KP_PAUSED, 1);
	current_flags_or(g, EXEC_PAUSED_GAME);
	t->paused = 1;
	kp_event(g, KP_EVENT_TRIGGER_PAUSE, 1, 0, 0, NULL);
}

/* TrigActUnpauseGame 0x4b6080, which the runner also calls when a trigger that paused the game ends. */
static void unpause_game(KpGame *g)
{
	KpTriggers *t = &g->trig;
	if (t->paused == 0 || computer_like(g, t->player)) return;
	current_flags_clear(g, EXEC_PAUSED_GAME);
	t->paused = 0;
	kp_scalar_set(g, KP_PAUSED, 0); /* UnpauseGame 0x47cc10 without discounting the paused time */
	kp_event(g, KP_EVENT_TRIGGER_PAUSE, 0, 0, 0, NULL);
}

/* GetTextDisplayDuration 0x4b6fe0: max(4000, 50 per character) milliseconds. */
static int64_t text_duration(const char *text)
{
	int64_t ms;
	if (text == NULL) return 0;
	ms = TEXT_MS_PER_CHAR * (int64_t)strlen(text);
	return ms > TEXT_MIN_MS ? ms : TEXT_MIN_MS;
}

/* DisplayTriggerText 0x46aec0 of an action's string for the local player. */
static void show_text(KpGame *g, const uint8_t *a, int64_t ms)
{
	uint32_t id = u32_at(a + KP_ACTION_STRING);
	if (kp_map_string(g, id) == NULL) return;
	kp_event(g, KP_EVENT_TRIGGER_TEXT, id, ms, (a[KP_ACTION_FLAGS] & ACTION_ALWAYS_TEXT) != 0, NULL);
}

/* TrigActPlayWav 0x4b6240 (not in a replay): the local player's streamed sound, unless skipping. */
static void play_wav(KpGame *g, const uint8_t *a)
{
	uint32_t wav = u32_at(a + KP_ACTION_WAV);
	if (g->trig.player != g->local_player || wav == 0 || skipping(g)) return;
	kp_event(g, KP_EVENT_TRIGGER_WAV, wav, 0, 0, NULL);
}

/* TrigActDisplayTextMessage 0x4b6300 */
static void display_text(KpGame *g, const uint8_t *a)
{
	if (g->trig.player != g->local_player || skipping(g)) return;
	show_text(g, a, text_duration(kp_map_string(g, u32_at(a + KP_ACTION_STRING))));
}

/* TrigActTransmission 0x4b60e0 of a single-player game: the player waits until the interface ends the portrait
 * (kp_trigger_release_wait) or Escape skips it; the second visit ends the action. */
static int transmission(KpGame *g, uint8_t *a)
{
	KpTriggers *t = &g->trig;
	int64_t p = t->player, ms = u32_at(a + KP_ACTION_TIME), number = u32_at(a + KP_ACTION_NUMBER), unit;
	if (a[KP_ACTION_LOCATION] == 0 || computer_like(g, p)) return 1;
	if (t->wait_active[p] != 0) return 0;
	if ((a[KP_ACTION_FLAGS] & ACTION_WAITING) != 0) {
		a[KP_ACTION_FLAGS] &= (uint8_t)~ACTION_WAITING;
		return 1;
	}
	if (skipping(g)) return 1;
	switch (a[KP_ACTION_MODIFIER]) {
	case KP_MOD_SET_TO: ms = number; break;
	case KP_MOD_ADD: ms = kp_u32v(ms + number); break;
	case KP_MOD_SUBTRACT: ms = number > ms ? 0 : ms - number; break;
	default: break;
	}
	a[KP_ACTION_FLAGS] |= ACTION_WAITING;
	t->wait_active[p] = 1;
	t->wait_remaining[p] = WAIT_FOREVER;
	if (p != g->local_player) return 0;
	play_wav(g, a);
	unit = kp_trigger_unit_at(g, a[KP_ACTION_LOCATION] - 1, KP_SEL_ALL, kp_le_u16(a + KP_ACTION_UNIT));
	if (unit != 0) {
		int32_t s = kp_unit_sprite(g, unit);
		g->w->spr.sel_timer[s] = TRANSMISSION_FLASH;
		kp_event(g, KP_EVENT_MINIMAP_PING, kp_s16v(g->w->spr.x[s]), kp_s16v(g->w->spr.y[s]), PING_COLOR, NULL);
	}
	kp_event(g, KP_EVENT_PORTRAIT, kp_le_u16(a + KP_ACTION_UNIT), ms, unit, NULL);
	{
		int64_t text = text_duration(kp_map_string(g, u32_at(a + KP_ACTION_STRING)));
		show_text(g, a, ms > text ? ms : text);
	}
	return 0;
}

/* TrigActCenterView 0x4b6350 of a single-player game: the local player's camera pans to the location and the trigger
 * waits until it arrives (the interface's kp_trigger_release_wait). */
static int center_view(KpGame *g, uint8_t *a)
{
	KpTriggers *t = &g->trig;
	int64_t p = t->player, x, y;
	if (a[KP_ACTION_LOCATION] == 0 || kp_scalar(g, KP_PAUSED) == 1 || computer_like(g, p)) return 1;
	if (t->wait_active[p] != 0) return 0;
	if ((a[KP_ACTION_FLAGS] & ACTION_WAITING) != 0) {
		a[KP_ACTION_FLAGS] &= (uint8_t)~ACTION_WAITING;
		kp_event(g, KP_EVENT_CAMERA, 0, 0, 1, NULL);
		return 1;
	}
	if (skipping(g)) return 1;
	if (p == g->local_player) {
		kp_location_center(g, a[KP_ACTION_LOCATION] - 1, &x, &y);
		x -= SCREEN_HALF_W;
		if (x < 0) x = 0;
		else if (x + SCREEN_W >= g->map_w_px) x = g->map_w_px - SCREEN_W - 1;
		y -= SCREEN_HALF_H;
		if (y < 0) y = 0;
		else if (y + SCREEN_H >= g->map_h_px) y = g->map_h_px - SCREEN_H - 1;
		kp_event(g, KP_EVENT_CAMERA, x, y, 0, NULL);
	}
	a[KP_ACTION_FLAGS] |= ACTION_WAITING;
	t->wait_active[p] = 1;
	t->wait_remaining[p] = WAIT_FOREVER;
	return 0;
}

/* TrigActMinimapPing 0x4b69e0 */
static void minimap_ping(KpGame *g, const uint8_t *a)
{
	int64_t x, y;
	if (a[KP_ACTION_LOCATION] == 0 || g->trig.player != g->local_player || skipping(g)) return;
	kp_location_center(g, a[KP_ACTION_LOCATION] - 1, &x, &y);
	kp_event(g, KP_EVENT_MINIMAP_PING, x, y, PING_COLOR, NULL);
}

/* TrigActTalkingPortrait 0x4b5620: no wait, but the portrait's end releases one (0x4a7300). */
static void talking_portrait(KpGame *g, const uint8_t *a)
{
	if (g->trig.player != g->local_player || skipping(g)) return;
	kp_event(g, KP_EVENT_PORTRAIT, kp_le_u16(a + KP_ACTION_UNIT), u32_at(a + KP_ACTION_TIME), 0, NULL);
}

/* An action of the current trigger: 1 advances, 0 suspends the trigger at it. */
static int action(KpGame *g, uint8_t *a)
{
	KpTriggers *t = &g->trig;
	int type = a[KP_ACTION_TYPE];
	switch (type) {
	case KP_ACT_NONE: case KP_ACT_COMMENT: /* ReturnTrue 0x4b8e70: none, Comment */
		return 1;
	case KP_ACT_VICTORY: /* TrigActVictory 0x4b5ef0 */
		set_pending(g, t->player, RESULT_VICTORY);
		result_delay(g);
		return 1;
	case KP_ACT_DEFEAT: /* TrigActDefeat 0x4b5f20 */
		return lose_or_draw(g, RESULT_DEFEAT);
	case KP_ACT_DRAW: /* TrigActDraw 0x4b5f70 */
		return lose_or_draw(g, RESULT_DRAW);
	case KP_ACT_PRESERVE_TRIGGER: /* TrigActPreserveTrigger 0x4b5fc0 */
		current_flags_or(g, EXEC_PRESERVE);
		return 1;
	case KP_ACT_WAIT:
		return wait_action(g, a);
	case KP_ACT_SET_MISSION_OBJECTIVES: /* TrigActSetMissionObjectives 0x4b6670 */
		t->objectives[t->player] = u32_at(a + KP_ACTION_STRING);
		return 1;
	case KP_ACT_SET_SWITCH:
		set_switch(g, a);
		return 1;
	case KP_ACT_SET_COUNTDOWN:
		set_countdown(g, a);
		return 1;
	case KP_ACT_MUTE_UNIT_SPEECH: /* TrigActMuteUnitSpeech 0x4b5660: EnterQuietMode 0x446930 for the local player */
		if (t->player == g->local_player && !skipping(g)) {
			t->quiet = 1;
			kp_event(g, KP_EVENT_QUIET, 1, 0, 0, NULL);
		}
		return 1;
	case KP_ACT_UNMUTE_UNIT_SPEECH: /* TrigActUnmuteUnitSpeech 0x4b5690: LeaveQuietMode 0x446fd0 */
		if (t->player == g->local_player) {
			t->quiet = 0;
			kp_event(g, KP_EVENT_QUIET, 0, 0, 0, NULL);
		}
		return 1;
	case KP_ACT_PAUSE_GAME:
		pause_game(g);
		return 1;
	case KP_ACT_UNPAUSE_GAME:
		unpause_game(g);
		return 1;
	case KP_ACT_TRANSMISSION:
		return transmission(g, a);
	case KP_ACT_PLAY_WAV:
		play_wav(g, a);
		return 1;
	case KP_ACT_DISPLAY_TEXT:
		display_text(g, a);
		return 1;
	case KP_ACT_CENTER_VIEW:
		return center_view(g, a);
	case KP_ACT_MINIMAP_PING:
		minimap_ping(g, a);
		return 1;
	case KP_ACT_TALKING_PORTRAIT:
		talking_portrait(g, a);
		return 1;
	case KP_ACT_LB_COMPUTER_PLAYERS:
		leaderboard_computers(g, a);
		return 1;
	case KP_ACT_LB_CONTROL: case KP_ACT_LB_CONTROL_AT: case KP_ACT_LB_RESOURCES: case KP_ACT_LB_KILLS: case KP_ACT_LB_POINTS: case KP_ACT_LB_GOAL_CONTROL: case KP_ACT_LB_GOAL_CONTROL_AT: case KP_ACT_LB_GOAL_RESOURCES: case KP_ACT_LB_GOAL_KILLS: case KP_ACT_LB_GOAL_POINTS: case KP_ACT_LB_GREED:
		set_leaderboard(g, a);
		return 1;
	case KP_ACT_SET_NEXT_SCENARIO:
		set_next_scenario(g, a);
		return 1;
	case KP_ACT_SET_RESOURCES: /* TrigActSetResources 0x4b6940 */
		kp_trigger_set_resources(g, u32_at(a + KP_ACTION_PLAYER), kp_le_u16(a + KP_ACTION_UNIT), a[KP_ACTION_MODIFIER], u32_at(a + KP_ACTION_NUMBER));
		return 1;
	case KP_ACT_SET_SCORE: /* TrigActSetScore 0x4b6990 */
		kp_trigger_set_score(g, u32_at(a + KP_ACTION_PLAYER), kp_le_u16(a + KP_ACTION_UNIT), a[KP_ACTION_MODIFIER], u32_at(a + KP_ACTION_NUMBER));
		return 1;
	case KP_ACT_SET_DEATHS: /* TrigActSetDeaths 0x4b6550 */
		kp_trigger_set_deaths(g, u32_at(a + KP_ACTION_PLAYER), kp_le_u16(a + KP_ACTION_UNIT), a[KP_ACTION_MODIFIER], u32_at(a + KP_ACTION_NUMBER));
		return 1;
	case KP_ACT_MOVE_LOCATION:
		move_location(g, a);
		return 1;
	case KP_ACT_CREATE_UNIT_WITH_PROPERTIES: case KP_ACT_CREATE_UNIT:
		kp_trigger_create(g, a);
		return 1;
	case KP_ACT_KILL_UNIT: case KP_ACT_REMOVE_UNIT: /* TrigActKillUnit 0x4b6840, TrigActRemoveUnit 0x4b68c0 */
		kp_trigger_kill(g, a, type == KP_ACT_REMOVE_UNIT);
		return 1;
	case KP_ACT_KILL_UNIT_AT: case KP_ACT_REMOVE_UNIT_AT: /* TrigActKillUnitAtLocation 0x4b6870, TrigActRemoveUnitAtLocation 0x4b68f0 */
		kp_trigger_kill_at(g, a, type == KP_ACT_REMOVE_UNIT_AT);
		return 1;
	case KP_ACT_ORDER:
		kp_trigger_order(g, a);
		return 1;
	case KP_ACT_MOVE_UNIT:
		kp_trigger_move_units(g, a);
		return 1;
	case KP_ACT_GIVE_UNITS:
		kp_trigger_give(g, a);
		return 1;
	case KP_ACT_SET_INVINCIBILITY:
		kp_trigger_invincibility(g, a);
		return 1;
	case KP_ACT_MODIFY_HP: case KP_ACT_MODIFY_ENERGY: case KP_ACT_MODIFY_SHIELDS:
		kp_trigger_modify(g, a);
		return 1;
	case KP_ACT_SET_DOODAD_STATE:
		kp_trigger_doodad(g, a);
		return 1;
	case KP_ACT_SET_ALLIANCE:
		kp_trigger_alliance(g, a);
		return 1;
	case KP_ACT_RUN_AI_SCRIPT: /* TrigActRunAiScript 0x4b6760 */
		kp_aiscript_launch(g, kp_le_u32(a + KP_ACTION_NUMBER), g->trig.player, NULL);
		return 1;
	case KP_ACT_RUN_AI_SCRIPT_AT: { /* TrigActRunAiScriptAtLocation 0x4b6780: the location's rectangle */
		int64_t loc = a[KP_ACTION_LOCATION];
		if (loc != 0) {
			const int64_t *l = t->locations[loc - 1];
			int64_t rect[4];
			rect[0] = l[KP_LOC_LEFT];
			rect[1] = l[KP_LOC_TOP];
			rect[2] = l[KP_LOC_RIGHT];
			rect[3] = l[KP_LOC_BOTTOM];
			kp_aiscript_launch(g, kp_le_u32(a + KP_ACTION_NUMBER), g->trig.player, rect);
		}
		return 1;
	}
	case KP_ACT_PAUSE_TIMER: /* TrigActPauseTimer 0x4b5e70 */
		t->countdown_paused = 1;
		return 1;
	case KP_ACT_UNPAUSE_TIMER: /* TrigActUnpauseTimer 0x4b5e80 */
		t->countdown_paused = 0;
		return 1;
	case KP_ACT_DISABLE_DEBUG_MODE: /* TrigActDisableDebugMode 0x4b5ed0 (SetTriggerNoSkipFlag 0x4bd460) */
		current_flags_or(g, EXEC_NO_SKIP);
		return 1;
	case KP_ACT_ENABLE_DEBUG_MODE: /* TrigActEnableDebugMode 0x4b5ee0 (ClearTriggerNoSkipFlag 0x4bd470) */
		current_flags_clear(g, EXEC_NO_SKIP);
		return 1;
	default:
		if (type >= KP_ACT_TYPES) kp_game_fail(g, "triggers: action type %d (table 0x50df68)", type);
		/* TODO: Modify Unit Resource Amount 0x4b5d10 and Modify Unit Hangar Count 0x4b5dc0 when a map that needs
		 * them comes into scope; outside the V1 profile the converter rejects them (converter/analyze/triggers.py
		 * UNSUPPORTED_ACTIONS) */
		kp_game_fail(g, "triggers: action %d (0x%x)", type, (unsigned)ACTION_HANDLERS[type]);
		return 0;
	}
}


/* The trigger trace's event of an action that ran (KP_EVENT_TRIGGER, a game that traces); returns the result. */
static int traced(KpGame *g, int64_t copy, const uint8_t *a, int result)
{
	if (!g->events.trace) return result;
	kp_event(g, KP_EVENT_TRIGGER, g->trig.player, g->trig.place[copy],
		(int64_t)record(g, copy)[KP_TRIG_CONTINUATION] << 16 | (int64_t)a[KP_ACTION_TYPE] << 8 | result, NULL);
	return result;
}

/* RunTriggerActions 0x4bdf10 */
static void run_actions(KpGame *g, int64_t copy)
{
	uint8_t *r = record(g, copy);
	set_exec_flags(g, copy, exec_flags(g, copy) | EXEC_RUNNING);
	if ((exec_flags(g, copy) & EXEC_IGNORE_DEFEAT) != 0) g->trig.pending[g->trig.player] = RESULT_IGNORE_DEFEAT;
	while (r[KP_TRIG_CONTINUATION] < KP_TRIG_ACTIONS) {
		uint8_t *a = r + KP_TRIG_ACTIONS_AT + KP_TRIG_ACTION_SIZE * r[KP_TRIG_CONTINUATION];
		if ((a[KP_ACTION_FLAGS] & ENTRY_IGNORED) != 0) {
			r[KP_TRIG_CONTINUATION]++;
			continue;
		}
		if (a[KP_ACTION_TYPE] == 0) {
			r[KP_TRIG_CONTINUATION] = KP_TRIG_ACTIONS;
			break;
		}
		if (!traced(g, copy, a, action(g, a))) break;
		r[KP_TRIG_CONTINUATION]++;
	}
	if (r[KP_TRIG_CONTINUATION] < KP_TRIG_ACTIONS) return;
	if ((exec_flags(g, copy) & EXEC_PAUSED_GAME) != 0) unpause_game(g);
	if ((exec_flags(g, copy) & EXEC_PRESERVE) != 0) {
		r[KP_TRIG_CONTINUATION] = 0;
		set_exec_flags(g, copy, exec_flags(g, copy) & ~(uint32_t)EXEC_RESET);
	} else {
		set_exec_flags(g, copy, exec_flags(g, copy) | EXEC_DISABLED);
	}
}

/* RunPlayerTriggerList 0x4bdfd0 */
static void run_list(KpGame *g, int64_t p)
{
	int64_t copy;
	for (copy = g->trig.head[p]; copy >= 0; copy = g->trig.next[copy]) {
		uint32_t flags = exec_flags(g, copy);
		if ((flags & EXEC_DISABLED) != 0) continue;
		g->trig.current = copy;
		if ((flags & EXEC_RUNNING) == 0) {
			if (!conditions_hold(g, copy)) continue;
			record(g, copy)[KP_TRIG_CONTINUATION] = 0;
		}
		run_actions(g, copy);
	}
}

/* --- the pending results --- */

/* RetirePlayer 0x4bd7a0 of a single-player game (neither Capture the Flag nor Sudden Death): the slot's controller
 * becomes defeated and each of its units goes to player 11 (MakeAddonNeutral 0x428db0). */
static void retire(KpGame *g, int64_t p)
{
	int64_t n, next;
	if (g->players.controller[p] == KP_CTRL_COMPUTER) g->players.controller[p] = KP_CTRL_DEFEATED_COMPUTER;
	else if (g->players.controller[p] == KP_CTRL_HUMAN) g->players.controller[p] = KP_CTRL_DEFEATED_HUMAN;
	for (n = g->player_units[p]; n != 0; n = next) {
		next = g->u.player_next[n];
		kp_make_neutral(g, n);
	}
}

/* ApplyPendingResults 0x4be020 of a single-player game. */
static void apply_pending(KpGame *g)
{
	KpTriggers *t = &g->trig;
	int64_t cheats = kp_cheats(g), local = local_slot(g), cursor, p, all = 0, won = 0;
	if (local >= 0) {
		if ((cheats & CHEAT_VICTORY) != 0) t->pending[local] = RESULT_VICTORY;
		else if ((cheats & CHEAT_DEFEAT) != 0) t->pending[local] = RESULT_DEFEAT;
		if ((cheats & CHEAT_NO_RESULT) != 0 && t->pending[local] >= RESULT_DEFEAT) t->pending[local] = 0;
	}
	for (p = first_active(g, &cursor); p < 8; p = next_active(g, &cursor)) {
		if (t->pending[p] >= RESULT_VICTORY) break;
	}
	if (p < 8) {
		for (p = first_active(g, &cursor); p < 8; p = next_active(g, &cursor)) {
			if (t->pending[p] != 0) continue;
			t->pending[p] = RESULT_DEFEAT;
			if (p == local) t->result = RESULT_STEPS;
		}
		for (p = 0; p < 8; p++) {
			if (t->participants[p] == 0) continue;
			all++;
			if (t->pending[p] >= RESULT_VICTORY) won++;
		}
		if ((all & 0xff) == (won & 0xff)) {
			for (p = 0; p < 8; p++) {
				if (t->participants[p] != 0) t->pending[p] = RESULT_DRAW;
			}
		}
	}
	for (p = 0; p < 8; p++) {
		if (t->pending[p] == 0 || g->results[p] != 0) continue;
		g->results[p] = t->pending[p];
		/* AnnouncePlayerDefeat 0x4bd4a0 shows nothing in a Use Map Settings game */
		if (t->pending[p] == RESULT_DEFEAT) retire(g, p);
	}
}

/* --- the clocks and the scheduler --- */

/* A u16 divider tested before its decrement: true when it was 0, and then reloaded. */
static int expired(int64_t *c, int64_t period)
{
	int64_t was = *c;
	*c = kp_mod(was - 1, 65536);
	if (was == 0) {
		*c = period;
		return 1;
	}
	return 0;
}

/* TickResultDialogTimer 0x4be340 (not in a replay). */
static void tick_result(KpGame *g)
{
	int64_t r, local = local_slot(g);
	if (!expired(&g->trig.result, RESULT_STEPS)) return;
	r = local >= 0 ? g->results[local] : 0;
	if ((r == RESULT_DEFEAT || r == RESULT_VICTORY || r == RESULT_DRAW) && g->trig.result_dialog == 0) {
		/* ShowDefeatDialog 0x4bcf80, ShowVictoryDialog 0x4bcff0: CancelCursorModes 0x448c20, then OpenModelessDialog
		 * 0x46ecc0 pauses a single-player game; the dialog itself and the game's end are the interface's */
		kp_placement_cancel(g); /* 0x47d580 when active; the targeting cursor is the interface's */
		g->trig.result_dialog = 1;
		kp_scalar_set(g, KP_PAUSED, 1);
		kp_event(g, KP_EVENT_GAME_RESULT, r, 0, 0, NULL);
	}
}


/* TickTriggerClocks 0x4be410 with the frame's milliseconds (the speed table 0x503360). */
static void tick_clocks(KpGame *g, int64_t ms)
{
	KpTriggers *t = &g->trig;
	int p;
	for (p = 0; p < 8; p++) {
		if (t->wait_active[p] == 0 || t->wait_remaining[p] == WAIT_FOREVER) continue;
		if (t->wait_remaining[p] <= ms) {
			t->wait_active[p] = t->wait_remaining[p] = 0;
			t->pass = 1;
		} else {
			t->wait_remaining[p] -= ms;
		}
	}
	if (kp_scalar(g, KP_PAUSED) == 1) return;
	if (expired(&t->leaderboard, LEADERBOARD_STEPS)) update_leaderboard(g);
	if (expired(&t->second, SECOND_STEPS)) {
		kp_scalar_set(g, KP_ELAPSED, kp_scalar(g, KP_ELAPSED) + 1);
		/* the countdown's text (0x4bde30) and its hiding (0x4a79a0) are the presentation's */
		if (t->countdown != 0 && t->countdown_paused == 0) {
			t->countdown--;
			if (t->countdown == 0) t->pass = 1;
		}
	}
}

static int64_t frame_ms(KpGame *g)
{
	int64_t speed = kp_scalar(g, KP_SPEED);
	if (speed < 0 || speed >= SPEEDS) kp_game_fail(g, "triggers: game speed %lld", (long long)speed);
	return SPEED_MS[speed];
}

void kp_trigger_step(KpGame *g)
{
	KpTriggers *t = &g->trig;
	int64_t cursor, p;
	int ran = 0;
	if (kp_scalar(g, KP_PAUSED) == 1 && t->paused == 0) return;
	tick_result(g);
	tick_clocks(g, frame_ms(g));
	if (!expired(&t->pass, PASS_STEPS)) return;
	for (p = 0; p < 8; p++) t->pending[p] = 0;
	kp_trigger_invalidate_counts(g);
	for (p = first_active(g, &cursor); p < 8; p = next_active(g, &cursor)) {
		t->player = p;
		if (t->head[p] < 0) continue;
		run_list(g, p);
		ran = 1;
	}
	t->player = p;
	if (ran) apply_pending(g);
}

/* A frame of a paused game (RunPendingFrames 0x444e30): the turn, TickPausedUnitFlashes 0x444de0 over the active
 * units, then the scheduler with the RNG on; the frame count stays. */
void kp_paused_tick(KpGame *g, const KpCommand *commands, int count)
{
	int64_t n;
	kp_run_turn(g, commands, count);
	for (n = g->lists[KP_LIST_ACTIVE].head; n != 0; n = g->u.next[n]) kp_flash_selection(g, n);
	kp_rng_game_set_enabled(g->rng, 1);
	kp_trigger_step(g);
	kp_rng_game_set_enabled(g->rng, 0);
}

/* ReleaseLocalTriggerWait 0x4be3a0: the local player's wait ends and a trigger check is forced. */
void kp_trigger_release_wait(KpGame *g)
{
	int64_t p = local_slot(g);
	if (p < 0) return;
	g->trig.wait_active[p] = g->trig.wait_remaining[p] = 0;
	g->trig.pass = 1;
}

/* The local player's copies an Escape marks (0x4bda80): executing ones (flag 0x01) without the no-skip flag and
 * without the `excluded` flags, in a campaign mission (index < 32); -1 when there are none. */
static int64_t next_skippable(KpGame *g, int64_t copy, uint32_t excluded)
{
	for (; copy >= 0; copy = g->trig.next[copy]) {
		uint32_t flags = exec_flags(g, copy);
		if ((flags & EXEC_RUNNING) != 0 && (flags & (EXEC_NO_SKIP | excluded)) == 0) return copy;
	}
	return -1;
}

static int64_t first_skippable(KpGame *g, uint32_t excluded)
{
	int64_t p = local_slot(g);
	if (p < 0 || kp_scalar(g, KP_MISSION_INDEX) >= SKIPPABLE_MISSIONS) return -1;
	return next_skippable(g, g->trig.head[p], excluded);
}

/* Whether an Escape would skip a trigger that still runs: a finished trigger that is not preserved keeps flag 0x01
 * next to the disabled flag, and the skip marks it too, but it has nothing left to skip. */
int kp_trigger_skippable(KpGame *g)
{
	return first_skippable(g, EXEC_DISABLED) >= 0;
}

/* SkipLocalTriggerWaits 0x4bda80: each marked copy gets the skipping flag, the local wait ends and a trigger check is
 * forced. */
void kp_trigger_skip(KpGame *g)
{
	KpTriggers *t = &g->trig;
	int64_t p = local_slot(g), copy;
	for (copy = first_skippable(g, 0); copy >= 0; copy = next_skippable(g, t->next[copy], 0)) {
		set_exec_flags(g, copy, exec_flags(g, copy) | EXEC_SKIPPING);
		t->pass = 1;
		t->wait_active[p] = t->wait_remaining[p] = 0;
		kp_event(g, KP_EVENT_TRIGGER_SKIP, 0, 0, 0, NULL);
	}
}

int64_t kp_briefing_lead(KpGame *g) { return g->trig.lead_objectives; }

void kp_briefing_objectives(KpGame *g, int64_t string)
{
	int64_t p = local_slot(g);
	if (p >= 0) g->trig.objectives[p] = kp_u32v(string);
}
