/* The network branches of kp_core (phase M stage M5, ADR 0025): the commands among the players a replay plays without
 * the multiplayer byte (the alliances 0x0e, the shared vision 0x0d, the order Nothing 0x1d, a player leaving 0x57),
 * RetirePlayer's game types and the defeat's message; the triggers' branches of a network game (the multiplayer byte
 * 0x672664: Transmission's time, Center View, the pauses, the result dialog, the skips, the cheats, the trigger lists)
 * and its commands' (the speed, the cheats, the pause limit, the saves and the loads, the latency). Each case follows
 * the disassembly (the atlas's P25, P26 and P40 functions); the public replays that send the replay's commands are
 * compared with the original in tools/research/state_check.py --kind public, a network game's branches have no
 * observed original (ADR 0025). Runs on terran01's game; needs the converter's generated data and is skipped without
 * it. */
#include "kp_test.h"
#include "kp_game_internal.h"
#include "kp_gametype.h"
#include "kp_ids.h"

#include <string.h>

#define MAP "missions/terran/terran01"
#define SEED 946684819u
#define ALLIANCE_COMMAND 0x0e
#define VISION_COMMAND 0x0d
#define ORDER_NOTHING_COMMAND 0x1d
#define LEAVE_COMMAND 0x57
#define SELECT_COMMAND 0x09
#define SAVE_COMMAND 0x06
#define LOAD_COMMAND 0x07
#define SPEED_COMMAND 0x0f
#define PAUSE_COMMAND 0x10
#define RESUME_COMMAND 0x11
#define CHEAT_COMMAND 0x12
#define LATENCY_COMMAND 0x55
#define LEFT_GAME_STRING 0x4b
#define ELIMINATED_STRING 0x35
#define PLAYER_ELIMINATED_STRING 0x36
#define PASS_STEPS 30
#define CIRCLE_OWN 0
#define CIRCLE_ALLY 1
#define CIRCLE_ENEMY 2
#define LATENCY_STRING 0x45
#define WAIT_FOREVER 0xffffffffLL
#define TRANSMISSION_MS 1000
#define RESULT_VICTORY 3
#define RESULT_DEFEAT 2
#define RESULT_STEPS 45
#define PAUSES 3
#define SPEED_FASTEST 6
#define SCREEN_HALF_W 320
#define SCREEN_HALF_H 200

static const char *const FILES[] = { KP_T_DATA, NULL };

static KpGame *game(KpT *T)
{
	KpOracleOpts o;
	kp_oracle_opts(&o);
	o.host.events = 1;
	return kp_t_game(T, MAP, SEED, 0, &o)->g;
}

/* A completed unit of type t for owner, placed near the local player's first unit (terran01's open ground); 0 when it
 * cannot be. */
static int64_t unit(KpGame *g, int64_t t, int64_t owner)
{
	int64_t first = g->player_units[g->local_player], n;
	int32_t s;
	if (first == 0) return 0;
	s = kp_unit_sprite(g, first);
	n = kp_unit_create(g, t, g->w->spr.x[s], g->w->spr.y[s], owner);
	if (n == 0) return 0;
	return kp_unit_complete(g, n) ? n : 0;
}

/* The events of a kind after sequence `since`: how many, the last one's a, b and c. */
static int events(KpGame *g, int kind, int64_t since, int64_t *a, int64_t *b, int64_t *c)
{
	KpEvents *e = &g->events;
	int64_t i;
	int n = 0;
	for (i = 0; i < e->count; i++) {
		int64_t slot = (e->first + i) % KP_EVENTS;
		if (e->kind[slot] != kind || e->seq[slot] <= since) continue;
		*a = e->a[slot];
		*b = e->b[slot];
		*c = e->c[slot];
		n++;
	}
	return n;
}

/* The player message events after sequence `since`: how many, the last one's string, player and number. */
static int messages(KpGame *g, int64_t since, int64_t *string, int64_t *player, int64_t *number)
{
	return events(g, KP_EVENT_PLAYER_MESSAGE, since, string, player, number);
}

static int count_events(KpGame *g, int kind, int64_t since)
{
	int64_t a, b, c;
	return events(g, kind, since, &a, &b, &c);
}

static void alliance_command(KpGame *g, int64_t player, uint32_t stances)
{
	uint8_t b[5] = { ALLIANCE_COMMAND, (uint8_t)stances, (uint8_t)(stances >> 8), (uint8_t)(stances >> 16),
		(uint8_t)(stances >> 24) };
	kp_execute_command(g, player, b, sizeof b);
}

static void vision_command(KpGame *g, int64_t player, int64_t mask)
{
	uint8_t b[3] = { VISION_COMMAND, (uint8_t)mask, (uint8_t)(mask >> 8) };
	kp_execute_command(g, player, b, sizeof b);
}

/* player's selection as the select command makes it */
static void select_units(KpGame *g, int64_t player, const int64_t *units, int count)
{
	uint8_t b[2 + 2 * 12];
	int k;
	b[0] = SELECT_COMMAND;
	b[1] = (uint8_t)count;
	for (k = 0; k < count; k++) {
		int64_t ref = kp_unit_ref(g, units[k]);
		b[2 + 2 * k] = (uint8_t)ref;
		b[3 + 2 * k] = (uint8_t)(ref >> 8);
	}
	kp_execute_command(g, player, b, (size_t)(2 + 2 * count));
}

/* ExecAlliance 0x4731d0: when the game allows alliances (allies 1, no tournament), the issuer's twelve stances from two
 * bits each, itself included; the circle colours follow the local player's row; an allied target that is no longer an
 * enemy is dropped (ClearAlliedOrderTargets 0x4721c0). Without alliances the command changes nothing. */
static void alliance_command_sets_the_issuers_stances(KpT *T)
{
	KpGame *g;
	int64_t own, theirs, me, them, victory, enemy;
	uint32_t stances;
	if (!kp_t_ready(T, "test_multiplayer", FILES)) return;
	g = game(T);
	me = g->local_player;
	them = me == 0 ? 2 : 0;
	victory = me == 4 ? 5 : 4;
	enemy = me == 3 ? 6 : 3;
	stances = (1u << (2 * me)) | (1u << (2 * them)) | (2u << (2 * victory)) | (3u << 22); /* 11: 3 */
	kp_t_ok(T, kp_setup_alliances_allowed(&g->setup), "a campaign's template allows alliances");
	own = unit(g, KP_TYPE_MARINE, me);
	theirs = unit(g, KP_TYPE_MARINE, them);
	kp_t_ok(T, own != 0 && theirs != 0, "the units");
	if (own == 0 || theirs == 0) return;
	kp_set_alliance(g, me, them, 0);
	kp_set_alliance(g, them, me, 0);
	g->u.order[own] = KP_ORD_ATTACK_UNIT;
	g->u.order_unit[own] = theirs;
	alliance_command(g, me, stances);
	kp_t_ok(T, kp_alliance(g, me, me) == 1 && kp_alliance(g, me, them) == 1 && kp_alliance(g, me, victory) == 2
		&& kp_alliance(g, me, enemy) == 0 && kp_alliance(g, me, 11) == 3, "the issuer's row");
	kp_t_eq(T, kp_alliance(g, them, me), 0, "the other row stays");
	kp_t_ok(T, g->circle_colors[me] == CIRCLE_OWN && g->circle_colors[them] == CIRCLE_ALLY
		&& g->circle_colors[enemy] == CIRCLE_ENEMY, "the circles' colours");
	kp_t_eq(T, g->w->circle_colors[them], CIRCLE_ALLY, "the images world's copy");
	kp_t_eq(T, g->u.order_unit[own], 0, "the ally's unit is no longer a target");
	g->setup.template_record[KP_TEMPLATE_ALLIES] = 0;
	alliance_command(g, me, 0);
	kp_t_eq(T, kp_alliance(g, me, them), 1, "no alliances in the game: nothing changes");
	g->setup.template_record[KP_TEMPLATE_ALLIES] = 1;
	g->setup.template_record[KP_TEMPLATE_TOURNAMENT] = 1;
	alliance_command(g, me, 0);
	kp_t_eq(T, kp_alliance(g, me, them), 1, "a tournament: nothing changes");
}

/* RefreshAllianceColors 0x41d7b0 in a replay: every player's circle 0 but player 11's 1; a selected unit of another
 * player keeps its circle, drawn again. */
static void replay_circles_are_one_colour(KpT *T)
{
	KpGame *g;
	int64_t theirs, p;
	int32_t s;
	if (!kp_t_ready(T, "test_multiplayer", FILES)) return;
	g = game(T);
	theirs = unit(g, KP_TYPE_MARINE, g->local_player == 0 ? 2 : 0);
	kp_t_ok(T, theirs != 0, "the unit");
	if (theirs == 0) return;
	kp_select_locally(g, &theirs, 1);
	g->w->replay = 1;
	kp_refresh_alliance_colors(g);
	for (p = 0; p < 11 && g->circle_colors[p] == CIRCLE_OWN; p++) {
	}
	kp_t_eq(T, p, 11, "0 for players 0..10");
	kp_t_eq(T, g->circle_colors[11], CIRCLE_ALLY, "1 for player 11");
	s = kp_unit_sprite(g, theirs);
	kp_t_ok(T, (g->w->spr.flags[s] & 0x09) == 0x09, "the selected unit's circle drawn again");
	kp_t_eq(T, g->local_selection[0], theirs, "in its slot");
}

/* ExecSharedVision 0x473280: the issuer's vision goes to the slots of the mask's low byte, its high byte kept and its
 * own bit set, when it may share with each (CanShareVisionWith 0x447600): in a replay always; an observer shares none;
 * every player may share with an observer; else when the game allows alliances. */
static void vision_command_shares_the_issuers_vision(KpT *T)
{
	KpGame *g;
	if (!kp_t_ready(T, "test_multiplayer", FILES)) return;
	g = game(T);
	g->players.shared_vision[0] = 0x301;
	vision_command(g, 0, 0xff06);
	kp_t_eq(T, g->players.shared_vision[0], 0x307, "slots 1 and 2, the high byte kept, its own bit");
	g->setup.template_record[KP_TEMPLATE_ALLIES] = 0;
	vision_command(g, 0, 0x08);
	kp_t_eq(T, g->players.shared_vision[0], 0x307, "no alliances: refused");
	g->players.controller[3] = KP_CTRL_OBSERVER;
	vision_command(g, 0, 0x08);
	kp_t_eq(T, g->players.shared_vision[0], 0x309, "with an observer: shared");
	vision_command(g, 0, 0x0c);
	kp_t_eq(T, g->players.shared_vision[0], 0x309, "one refused slot refuses the mask");
	g->setup.template_record[KP_TEMPLATE_ALLIES] = 1;
	g->players.controller[0] = KP_CTRL_OBSERVER;
	vision_command(g, 0, 0x02);
	kp_t_eq(T, g->players.shared_vision[0], 0x309, "an observer shares none");
	g->w->replay = 1;
	g->setup.template_record[KP_TEMPLATE_ALLIES] = 0;
	vision_command(g, 0, 0x02);
	kp_t_eq(T, g->players.shared_vision[0], 0x303, "a replay: always");
}

/* ExecOrderNothing 0x472b40: each selected unit that can take it gets the order Nothing. */
static void order_nothing_command(KpT *T)
{
	static const uint8_t b[1] = { ORDER_NOTHING_COMMAND };
	KpGame *g;
	int64_t marine;
	if (!kp_t_ready(T, "test_multiplayer", FILES)) return;
	g = game(T);
	marine = unit(g, KP_TYPE_MARINE, g->local_player);
	kp_t_ok(T, marine != 0, "the unit");
	if (marine == 0) return;
	kp_issue_order(g, marine, KP_ORD_HOLD_POSITION);
	select_units(g, g->local_player, &marine, 1);
	kp_execute_command(g, g->local_player, b, sizeof b);
	kp_t_eq(T, g->u.order[marine], KP_ORD_NOTHING, "the order Nothing");
}

/* ExecLeaveGame 0x472da0 runs in a replay only (HandlePlayerLeftGame 0x477a60 with the replay's -1): the message
 * "%s has left the game." (the reason a byte, never the dropped one), the player's trigger list goes and it takes the
 * result 2 and retires (PlayerLeftGame 0x4be3e0, RetirePlayer 0x4bd7a0: a defeated controller, its units player
 * 11's, out of the replay's vision), its selection empties (0x49c7d0); a second leave changes nothing more. */
static void leave_command_retires_the_player_in_a_replay(KpT *T)
{
	static const uint8_t b[2] = { LEAVE_COMMAND, 0x06 };
	KpGame *g;
	int64_t marine, string = 0, player = -1, number = -1, since;
	if (!kp_t_ready(T, "test_multiplayer", FILES)) return;
	g = game(T);
	g->players.controller[1] = KP_CTRL_HUMAN;
	marine = unit(g, KP_TYPE_MARINE, 1);
	kp_t_ok(T, marine != 0, "the unit");
	if (marine == 0) return;
	select_units(g, 1, &marine, 1);
	kp_t_eq(T, g->selections[1][0], marine, "selected");
	since = kp_event_sequence(g);
	kp_execute_command(g, 1, b, sizeof b);
	kp_t_ok(T, g->results[1] == 0 && g->u.owner[marine] == 1, "outside a replay: nothing");
	kp_t_eq(T, messages(g, since, &string, &player, &number), 0, "no message");
	g->w->replay = 1;
	g->local_vision = g->w->local_vision = 0xff;
	kp_execute_command(g, 1, b, sizeof b);
	kp_t_eq(T, messages(g, since, &string, &player, &number), 1, "one message");
	kp_t_ok(T, string == LEFT_GAME_STRING && player == 1, "left, the player: 0x%llx %lld", (long long)string,
		(long long)player);
	kp_t_eq(T, g->results[1], 2, "a defeat");
	kp_t_eq(T, g->trig.head[1], -1, "no trigger list");
	kp_t_eq(T, g->players.controller[1], KP_CTRL_DEFEATED_HUMAN, "a defeated human");
	kp_t_eq(T, g->u.owner[marine], 11, "its unit player 11's");
	kp_t_eq(T, g->selections[1][0], 0, "its selection empty");
	kp_t_ok(T, g->local_vision == 0xfd && g->w->local_vision == 0xfd, "out of the replay's vision");
	g->results[1] = 3;
	kp_execute_command(g, 1, b, sizeof b);
	kp_t_eq(T, g->results[1], 3, "a player with a result keeps it");
}

/* RetirePlayer 0x4bd7a0 in Sudden Death (and Capture the Flag): the player's units die instead of going to player 11. */
static void retire_kills_in_sudden_death(KpT *T)
{
	static const uint8_t b[2] = { LEAVE_COMMAND, 0 };
	KpGame *g;
	int64_t marine;
	if (!kp_t_ready(T, "test_multiplayer", FILES)) return;
	g = game(T);
	g->setup.template_record[KP_TEMPLATE_VICTORY] = KP_VICTORY_SUDDEN_DEATH;
	g->players.controller[1] = KP_CTRL_COMPUTER;
	marine = unit(g, KP_TYPE_MARINE, 1);
	kp_t_ok(T, marine != 0, "the unit");
	if (marine == 0) return;
	g->w->replay = 1;
	kp_execute_command(g, 1, b, sizeof b);
	kp_t_eq(T, g->players.controller[1], KP_CTRL_DEFEATED_COMPUTER, "a defeated computer");
	kp_t_ok(T, g->u.owner[marine] == 1 && g->u.order[marine] == KP_ORD_DIE, "its unit dies as its own: owner %lld, order %lld",
		(long long)g->u.owner[marine], (long long)g->u.order[marine]);
}

/* A trigger of player p: Always, then Defeat. */
static void defeat_trigger(uint8_t *r, int64_t p)
{
	memset(r, 0, KP_TRIGGER_RECORD);
	r[0x0f] = KP_COND_ALWAYS;
	r[KP_TRIG_ACTIONS_AT + KP_ACTION_TYPE] = KP_ACT_DEFEAT;
	r[KP_TRIG_OWNERS_AT + p] = 1;
}

/* AnnouncePlayerDefeat 0x4bd4a0 before RetirePlayer (ApplyPendingResults 0x4be020): outside Use Map Settings a human's
 * name ("%s was eliminated.") or a computer's number ("Player %d was eliminated."); nothing in Use Map Settings. */
static void defeat_is_announced_outside_use_map_settings(KpT *T)
{
	static uint8_t records[2 * KP_TRIGGER_RECORD];
	KpGame *g;
	int64_t string = 0, player = -1, number = -1, since;
	int k, ums;
	if (!kp_t_ready(T, "test_multiplayer", FILES)) return;
	for (ums = 0; ums < 2; ums++) {
		g = game(T);
		g->players.controller[1] = KP_CTRL_COMPUTER;
		g->players.controller[2] = KP_CTRL_HUMAN;
		if (!ums) g->setup.template_record[KP_TEMPLATE_VICTORY] = KP_VICTORY_MELEE;
		kp_t_eq(T, kp_setup_ums(&g->setup), ums, "the template");
		defeat_trigger(records, 1);
		defeat_trigger(records + KP_TRIGGER_RECORD, 2);
		kp_triggers_replace(g, records, sizeof records);
		since = kp_event_sequence(g);
		for (k = 0; k <= PASS_STEPS && g->results[2] == 0; k++) kp_trigger_step(g);
		kp_t_ok(T, g->results[1] == 2 && g->results[2] == 2, "both defeated");
		if (ums) {
			kp_t_eq(T, messages(g, since, &string, &player, &number), 0, "Use Map Settings: no message");
			continue;
		}
		kp_t_eq(T, messages(g, since, &string, &player, &number), 2, "two messages");
		kp_t_ok(T, string == ELIMINATED_STRING && player == 2, "the human's name last");
		g = game(T);
		g->players.controller[1] = KP_CTRL_COMPUTER;
		g->setup.template_record[KP_TEMPLATE_VICTORY] = KP_VICTORY_MELEE;
		kp_triggers_replace(g, records, KP_TRIGGER_RECORD);
		since = kp_event_sequence(g);
		for (k = 0; k <= PASS_STEPS && g->results[1] == 0; k++) kp_trigger_step(g);
		kp_t_eq(T, messages(g, since, &string, &player, &number), 1, "the computer's");
		kp_t_ok(T, string == PLAYER_ELIMINATED_STRING && player == 1 && number == 2, "its number: %lld %lld",
			(long long)string, (long long)number);
	}
}

/* Action k of a trigger record. */
static uint8_t *action(uint8_t *r, int k, int type)
{
	uint8_t *a = r + KP_TRIG_ACTIONS_AT + k * KP_TRIG_ACTION_SIZE;
	a[KP_ACTION_TYPE] = (uint8_t)type;
	return a;
}

/* Player p's trigger: Always, then `type` (its location 1, time TRANSMISSION_MS), then Set Switch 0, which tells the
 * action ended. */
static void switch_trigger(uint8_t *r, int64_t p, int type)
{
	uint8_t *a;
	memset(r, 0, KP_TRIGGER_RECORD);
	r[0x0f] = KP_COND_ALWAYS;
	r[KP_TRIG_OWNERS_AT + p] = 1;
	a = action(r, 0, type);
	a[KP_ACTION_LOCATION] = 1;
	a[KP_ACTION_TIME] = (uint8_t)TRANSMISSION_MS;
	a[KP_ACTION_TIME + 1] = (uint8_t)(TRANSMISSION_MS >> 8);
	a = action(r, 1, KP_ACT_SET_SWITCH);
	a[KP_ACTION_MODIFIER] = KP_MOD_SET;
}

static int switched(KpGame *g)
{
	return (g->trig.switches[0] & 1) != 0;
}

/* steps of the trigger scheduler until switch 0 is set or `limit` ran; how many ran */
static int run_until_switched(KpGame *g, int limit)
{
	int k;
	for (k = 0; k < limit && !switched(g); k++) kp_trigger_step(g);
	return k;
}

/* TrigActTransmission 0x4b60e0: a network game's player waits the action's time (0x4b616e picks it over 0xffffffff)
 * and the clocks end the wait on every machine; the portrait's end releases none (0x4a7300). A single player's waits
 * until the interface releases it. */
static void transmission_waits_its_time_in_a_network_game(KpT *T)
{
	static uint8_t record[KP_TRIGGER_RECORD];
	KpGame *g;
	int64_t p, ms;
	int steps;
	if (!kp_t_ready(T, "test_multiplayer", FILES)) return;
	g = game(T);
	p = g->local_player;
	g->setup.multiplayer = 1;
	switch_trigger(record, p, KP_ACT_TRANSMISSION);
	kp_triggers_replace(g, record, sizeof record);
	for (steps = 0; steps <= PASS_STEPS && g->trig.wait_active[p] == 0; steps++) kp_trigger_step(g);
	kp_t_ok(T, g->trig.wait_active[p] == 1 && g->trig.wait_remaining[p] == TRANSMISSION_MS, "the action's time: %lld",
		(long long)g->trig.wait_remaining[p]);
	kp_trigger_release_wait(g);
	kp_t_eq(T, g->trig.wait_active[p], 1, "no release");
	kp_trigger_step(g);
	ms = TRANSMISSION_MS - g->trig.wait_remaining[p];
	kp_t_ok(T, ms > 0 && g->trig.wait_active[p] == 1, "the clocks count it down: %lld ms a step", (long long)ms);
	steps = run_until_switched(g, 4 * PASS_STEPS);
	kp_t_ok(T, switched(g), "the action ends by its time");
	/* the wait ends at the step its time runs out (the step counted above among them), the next step's pass runs on */
	kp_t_ok(T, steps * ms >= TRANSMISSION_MS && (steps - 1) * ms < TRANSMISSION_MS, "at its time: %d steps of %lld ms",
		steps, (long long)ms);
	g = game(T);
	switch_trigger(record, p, KP_ACT_TRANSMISSION);
	kp_triggers_replace(g, record, sizeof record);
	for (steps = 0; steps <= PASS_STEPS && g->trig.wait_active[p] == 0; steps++) kp_trigger_step(g);
	kp_t_eq(T, g->trig.wait_remaining[p], WAIT_FOREVER, "a single player's waits for the portrait");
	kp_t_eq(T, run_until_switched(g, 4 * PASS_STEPS), 4 * PASS_STEPS, "until released");
	kp_trigger_release_wait(g);
	kp_t_ok(T, run_until_switched(g, 4 * PASS_STEPS) < 4 * PASS_STEPS && switched(g), "released");
}

/* TrigActCenterView 0x4b6350 in a network game: the local player's screen moves to the location at once (the camera
 * event's KP_CAMERA_SET) and the action ends without a wait; another player's moves nothing here. */
static void center_view_moves_the_screen_at_once_in_a_network_game(KpT *T)
{
	static uint8_t record[KP_TRIGGER_RECORD];
	KpGame *g;
	int64_t x, y, a = 0, b = 0, c = -1, since;
	int other;
	if (!kp_t_ready(T, "test_multiplayer", FILES)) return;
	for (other = 0; other < 2; other++) {
		int64_t p;
		g = game(T);
		p = other ? (g->local_player == 1 ? 2 : 1) : g->local_player;
		g->players.controller[p] = KP_CTRL_HUMAN;
		g->setup.multiplayer = 1;
		switch_trigger(record, p, KP_ACT_CENTER_VIEW);
		kp_triggers_replace(g, record, sizeof record);
		since = kp_event_sequence(g);
		run_until_switched(g, PASS_STEPS + 1);
		kp_t_ok(T, switched(g), "no wait");
		kp_t_eq(T, g->trig.wait_active[p], 0, "no wait active");
		if (other) {
			kp_t_eq(T, count_events(g, KP_EVENT_CAMERA, since), 0, "another player's: no camera");
			continue;
		}
		kp_location_center(g, 0, &x, &y);
		kp_t_eq(T, events(g, KP_EVENT_CAMERA, since, &a, &b, &c), 1, "one camera event");
		kp_t_ok(T, a == x - SCREEN_HALF_W && b == y - SCREEN_HALF_H && c == KP_CAMERA_SET, "the screen set: %lld %lld %lld",
			(long long)a, (long long)b, (long long)c);
	}
}

/* TrigActPauseGame 0x4b6020 and TrigActUnpauseGame 0x4b6080 (which the runner calls as the pausing trigger ends) do
 * nothing in a network game. */
static void pause_actions_do_nothing_in_a_network_game(KpT *T)
{
	static uint8_t record[KP_TRIGGER_RECORD];
	KpGame *g;
	int64_t since;
	int multiplayer;
	if (!kp_t_ready(T, "test_multiplayer", FILES)) return;
	for (multiplayer = 0; multiplayer < 2; multiplayer++) {
		g = game(T);
		g->setup.multiplayer = multiplayer;
		switch_trigger(record, g->local_player, KP_ACT_PAUSE_GAME);
		kp_triggers_replace(g, record, sizeof record);
		since = kp_event_sequence(g);
		run_until_switched(g, PASS_STEPS + 1);
		kp_t_ok(T, switched(g), "the trigger ran");
		kp_t_eq(T, count_events(g, KP_EVENT_TRIGGER_PAUSE, since), multiplayer ? 0 : 2,
			multiplayer ? "a network game goes on" : "paused, and unpaused as the trigger ends");
		kp_t_ok(T, kp_scalar(g, KP_PAUSED) == 0 && g->trig.paused == 0, "not paused after");
	}
}

/* ShowVictoryDialog 0x4bcff0's OpenModelessDialog 0x46ecc0 pauses a single-player game only. */
static void result_dialog_does_not_pause_a_network_game(KpT *T)
{
	KpGame *g;
	int multiplayer, k;
	if (!kp_t_ready(T, "test_multiplayer", FILES)) return;
	for (multiplayer = 0; multiplayer < 2; multiplayer++) {
		g = game(T);
		g->setup.multiplayer = multiplayer;
		kp_triggers_replace(g, NULL, 0);
		g->results[g->local_player] = RESULT_VICTORY;
		for (k = 0; k <= RESULT_STEPS && g->trig.result_dialog == 0; k++) kp_trigger_step(g);
		kp_t_eq(T, g->trig.result_dialog, 1, "the dialog");
		kp_t_eq(T, kp_scalar(g, KP_PAUSED), multiplayer ? 0 : 1, multiplayer ? "a network game goes on" : "paused");
	}
}

/* ApplyPendingResults 0x4be020 in a network game: the cheats decide nothing; a player with a result (other than Ignore
 * Defeat) loses its trigger list (FreeTriggerList 0x4609c0). Escape's skip finds no trigger (0x4bda80). */
static void results_free_the_trigger_lists_in_a_network_game(KpT *T)
{
	static uint8_t records[2 * KP_TRIGGER_RECORD];
	KpGame *g;
	int multiplayer, k;
	if (!kp_t_ready(T, "test_multiplayer", FILES)) return;
	for (multiplayer = 0; multiplayer < 2; multiplayer++) {
		int64_t me, other;
		g = game(T);
		me = g->local_player;
		other = me == 1 ? 2 : 1;
		g->players.controller[other] = KP_CTRL_COMPUTER;
		g->setup.multiplayer = multiplayer;
		kp_scalar_set(g, KP_CHEATS, KP_CHEAT_NO_COW_LEVEL);
		defeat_trigger(records, other);
		switch_trigger(records + KP_TRIGGER_RECORD, me, KP_ACT_COMMENT);
		kp_triggers_replace(g, records, sizeof records);
		for (k = 0; k <= PASS_STEPS && g->results[other] == 0; k++) kp_trigger_step(g);
		kp_t_eq(T, g->results[other], RESULT_DEFEAT, "the defeat");
		kp_t_eq(T, g->results[me], multiplayer ? 0 : RESULT_VICTORY, multiplayer ? "no cheat" : "the cheat's victory");
		kp_t_ok(T, (g->trig.head[other] == -1) == (multiplayer != 0), "the defeated player's list: %lld",
			(long long)g->trig.head[other]);
		kp_t_ok(T, g->trig.head[me] >= 0, "a player without a result keeps its list");
		if (multiplayer) kp_t_eq(T, kp_trigger_skippable(g), 0, "no skip");
	}
}

static void command(KpGame *g, int64_t player, uint8_t id, uint8_t b1)
{
	uint8_t b[2] = { id, b1 };
	kp_execute_command(g, player, b, sizeof b);
}

/* ExecGameSpeed 0x473310 and ApplyCheatFlags 0x40d9d0 are a single player's; ExecCheat 0x472530 runs only when the
 * template allows cheats (AreCheatsEnabled 0x447640). */
static void speed_and_cheats_are_single_players(KpT *T)
{
	uint8_t cheat[5] = { CHEAT_COMMAND, KP_CHEAT_SHOW_ME_THE_MONEY, 0, 0, 0 };
	KpGame *g;
	int64_t me, speed, minerals;
	if (!kp_t_ready(T, "test_multiplayer", FILES)) return;
	g = game(T);
	me = g->local_player;
	speed = kp_scalar(g, KP_SPEED) == SPEED_FASTEST ? 0 : SPEED_FASTEST;
	command(g, me, SPEED_COMMAND, (uint8_t)speed);
	kp_t_eq(T, kp_scalar(g, KP_SPEED), speed, "a single player's speed");
	minerals = g->players.minerals[me];
	kp_execute_command(g, me, cheat, sizeof cheat);
	kp_t_eq(T, g->players.minerals[me], minerals + 10000, "a single player's cheat");
	g->setup.template_record[KP_TEMPLATE_CHEATS] = 0;
	minerals = g->players.minerals[me];
	kp_execute_command(g, me, cheat, sizeof cheat);
	kp_t_eq(T, g->players.minerals[me], minerals, "the template's cheats off: nothing");
	g->setup.template_record[KP_TEMPLATE_CHEATS] = 1;
	g->setup.multiplayer = 1;
	kp_execute_command(g, me, cheat, sizeof cheat);
	kp_t_eq(T, g->players.minerals[me], minerals, "a network game: nothing");
	command(g, me, SPEED_COMMAND, (uint8_t)(SPEED_FASTEST - speed));
	kp_t_eq(T, kp_scalar(g, KP_SPEED), speed, "a network game's speed stays");
}

/* ExecPauseGame 0x472c70: in a network game a human with pauses left (CanPlayerPause 0x47cb20) pauses and spends one
 * (0x47cc60; 3 at the start, 0x47cb90), the line 0x39 shows the pauses left; any player resumes (ExecResumeGame
 * 0x472be0); both cancel the cursor modes at the turn's end (0x672985 in ExecuteGameTurn 0x4768a0). A single player's
 * pauses are never spent. */
static void pauses_are_limited_in_a_network_game(KpT *T)
{
	static const uint8_t pause[1] = { PAUSE_COMMAND }, resume[1] = { RESUME_COMMAND };
	KpCommand c[1];
	KpGame *g;
	int64_t me, computer, a = 0, b = -1, n = -1, since;
	int k;
	if (!kp_t_ready(T, "test_multiplayer", FILES)) return;
	g = game(T);
	me = g->local_player;
	computer = me == 1 ? 2 : 1;
	g->players.controller[me] = KP_CTRL_HUMAN;
	g->players.controller[computer] = KP_CTRL_COMPUTER;
	for (k = 0; k < 8; k++) kp_t_eq(T, g->pauses[k], PAUSES, "3 pauses at the start");
	c[0].player = me;
	c[0].bytes = pause;
	c[0].size = sizeof pause;
	kp_run_turn(g, c, 1);
	kp_t_ok(T, kp_scalar(g, KP_PAUSED) == 1 && g->pauses[me] == PAUSES, "a single player pauses for free");
	kp_execute_command(g, me, resume, sizeof resume);
	g->turn_cancels_cursor = 0;
	g->setup.multiplayer = 1;
	kp_execute_command(g, computer, pause, sizeof pause);
	kp_t_eq(T, kp_scalar(g, KP_PAUSED), 0, "a computer has no pause");
	for (k = PAUSES - 1; k >= 0; k--) {
		since = kp_event_sequence(g);
		kp_run_turn(g, c, 1);
		kp_t_ok(T, kp_scalar(g, KP_PAUSED) == 1 && g->pauses[me] == k, "a pause spent: %lld left", (long long)g->pauses[me]);
		kp_t_eq(T, events(g, KP_EVENT_COMMAND_PAUSE, since, &a, &b, &n), 1, "the pause's event");
		kp_t_ok(T, a == 1 && b == me && n == k, "paused by the player, the pauses left: %lld", (long long)n);
		kp_t_ok(T, count_events(g, KP_EVENT_CURSOR_MODES, since) == 1 && g->turn_cancels_cursor == 0,
			"the turn's end cancels the cursor modes");
		kp_execute_command(g, computer, resume, sizeof resume);
		kp_t_eq(T, kp_scalar(g, KP_PAUSED), 0, "any player resumes");
		kp_t_eq(T, g->turn_cancels_cursor, 1, "the resume cancels them too");
		g->turn_cancels_cursor = 0;
	}
	kp_execute_command(g, me, pause, sizeof pause);
	kp_t_eq(T, kp_scalar(g, KP_PAUSED), 0, "no pauses left");
}

/* ExecSaveGame 0x472f50: never in a tournament (0x447d00); in a network game only the lowest Storm ID's command saves
 * (0x4771e0). ExecLoadGame 0x4729e0 is a single player's. */
static void saves_and_loads_in_a_network_game(KpT *T)
{
	static const uint8_t save[6] = { SAVE_COMMAND, 0, 0, 0, 0, 'a' }, load[6] = { LOAD_COMMAND, 0, 0, 0, 0, 'a' };
	KpGame *g;
	int64_t since;
	int k;
	if (!kp_t_ready(T, "test_multiplayer", FILES)) return;
	g = game(T);
	since = kp_event_sequence(g);
	kp_execute_command(g, 0, save, sizeof save);
	kp_execute_command(g, 0, load, sizeof load);
	kp_t_ok(T, count_events(g, KP_EVENT_SAVE_GAME, since) == 1 && count_events(g, KP_EVENT_LOAD_GAME, since) == 1,
		"a single player saves and loads");
	g->setup.template_record[KP_TEMPLATE_TOURNAMENT] = 1;
	since = kp_event_sequence(g);
	kp_execute_command(g, 0, save, sizeof save);
	kp_t_eq(T, count_events(g, KP_EVENT_SAVE_GAME, since), 0, "a tournament: no save");
	g->setup.template_record[KP_TEMPLATE_TOURNAMENT] = 0;
	g->setup.multiplayer = 1;
	for (k = 0; k < KP_SETUP_SLOTS; k++) g->setup.storm[k] = -1;
	g->setup.type[0] = g->setup.type[1] = KP_CTRL_HUMAN;
	g->setup.storm[0] = 1;
	g->setup.storm[1] = 0;
	since = kp_event_sequence(g);
	kp_execute_command(g, 0, save, sizeof save);
	kp_t_eq(T, count_events(g, KP_EVENT_SAVE_GAME, since), 0, "Storm ID 1: no save");
	kp_execute_command(g, 1, save, sizeof save);
	kp_t_eq(T, count_events(g, KP_EVENT_SAVE_GAME, since), 1, "Storm ID 0 saves");
	kp_execute_command(g, 1, load, sizeof load);
	kp_t_eq(T, count_events(g, KP_EVENT_LOAD_GAME, since), 0, "no load");
}

/* OnLatencyCommand 0x476d10: the line 0x45 + the extra turns, at most 2, with the player's name. */
static void latency_command_shows_its_line(KpT *T)
{
	KpGame *g;
	int64_t string = 0, player = -1, number = -1, since;
	if (!kp_t_ready(T, "test_multiplayer", FILES)) return;
	g = game(T);
	since = kp_event_sequence(g);
	command(g, 2, LATENCY_COMMAND, 1);
	kp_t_ok(T, messages(g, since, &string, &player, &number) == 1 && string == LATENCY_STRING + 1 && player == 2,
		"high latency: 0x%llx", (long long)string);
	command(g, 2, LATENCY_COMMAND, 7);
	kp_t_ok(T, messages(g, since, &string, &player, &number) == 2 && string == LATENCY_STRING + 2, "at most extra high");
}


/* --- team games (stage M5d) --- */

#define TEAM_MELEE 0x0b
#define DASHED_FIRST 0x23b
#define DASHED_LAST 0x244
#define REMOVE_COMMAND 0x0b

/* A Team Melee lobby on terran01 (whose map slots are 1 a human's, 2 a rescuable's and 4 a computer's): teams 4 (two
 * players a team), fixed positions, a town hall and four workers, no victory file; records 0 (Storm ID 1, race0) and 1
 * (Storm ID 0, race1) humans of force 1, record 2 the rescuable (Protoss) and record 4 a lobby computer (Terran) of
 * force 2, record 3 empty (Zerg). */
static void team_setup(KpGameSetup *s, int64_t race0, int64_t race1)
{
	static const int64_t RACES[KP_SETUP_SLOTS] = { 0, 0, KP_RACE_PROTOSS, KP_RACE_ZERG, KP_RACE_TERRAN };
	int p;
	memset(s, 0, sizeof *s);
	s->multiplayer = 1;
	s->template_record[KP_TEMPLATE_TYPE] = TEAM_MELEE;
	s->template_record[KP_TEMPLATE_STARTING_UNITS] = 2;
	s->template_record[KP_TEMPLATE_FIXED_POSITIONS] = 1;
	s->template_record[KP_TEMPLATE_TEAMS] = 4;
	for (p = 0; p < KP_SETUP_SLOTS; p++) {
		s->storm[p] = -1;
		s->race[p] = RACES[p];
	}
	s->race[0] = race0;
	s->race[1] = race1;
	s->type[0] = s->type[1] = KP_CTRL_HUMAN;
	s->storm[0] = 1;
	s->storm[1] = 0;
	s->force[0] = s->force[1] = 1;
	s->type[2] = KP_CTRL_RESCUE;
	s->type[4] = KP_CTRL_LOBBY_COMPUTER;
	s->force[2] = s->force[4] = 2;
	for (p = 0; p < 8; p++) s->color[p] = p;
}

/* The team game of a setup, the seat of Storm ID local_storm. */
static KpGame *team_game(KpT *T, const KpGameSetup *s, int64_t local_storm)
{
	KpOracleOpts o;
	kp_oracle_opts(&o);
	o.host.events = 1;
	o.host.mission_index = 0;
	o.host.setup = s;
	o.host.local_storm = local_storm;
	return kp_t_game(T, MAP, SEED, 0, &o)->g;
}

/* The units of type t (KP_TYPE_NONE: any) that player p owns. */
static int owned(KpGame *g, int64_t p, int64_t t)
{
	int64_t n;
	int k = 0;
	for (n = g->player_units[p]; n != 0; n = g->u.player_next[n]) k += t == KP_TYPE_NONE || g->u.type[n] == t;
	return k;
}

/* SetupTeamGameSlots 0x4b2db0: each force's leader (its lowest Storm ID; a computer leads a force without humans)
 * takes a slot the map offers in the forces' order (the first with fixed positions: the map's human slot, then its
 * rescuable's), the other records take the first free slots (the human slot 0, the rescuable slot 3), the map's
 * computer slot empties. The seat's record is its Storm ID's, the player it plays as its force's leader (0x508d50);
 * the lobby's races are kept in its order (RecordSlotSummary 0x47e290). A random placement draws one slot a leader
 * (RandRange src 0x20) among the map's offered slots. Only 2, 3 or 4 teams have a size. */
static void team_slots_place_the_leaders(KpT *T)
{
	KpGameSetup s;
	KpGame *g;
	int64_t leaders[2];
	if (!kp_t_ready(T, "test_multiplayer", FILES)) return;
	team_setup(&s, KP_RACE_ZERG, KP_RACE_TERRAN);
	g = team_game(T, &s, 1);
	kp_t_eq(T, g->setup.started, 1, "started");
	kp_t_eq(T, g->setup.leader[1], 1, "force 1's leader: Storm ID 0's record in the map's first offered slot");
	kp_t_eq(T, g->setup.leader[2], 2, "force 2's: the computer, in the next (the rescuable's)");
	kp_t_ok(T, g->setup.leader[3] == KP_NO_LEADER && g->setup.leader[4] == KP_NO_LEADER, "no leader of forces 3, 4");
	kp_t_ok(T, g->setup.type[1] == KP_CTRL_HUMAN && g->setup.storm[1] == 0 && g->setup.race[1] == KP_RACE_TERRAN,
		"slot 1: the leader's record");
	kp_t_ok(T, g->setup.type[0] == KP_CTRL_HUMAN && g->setup.storm[0] == 1 && g->setup.force[0] == 1
		&& g->setup.race[0] == KP_RACE_ZERG, "slot 0: the other human, the first free slot");
	kp_t_ok(T, g->setup.type[2] == KP_CTRL_COMPUTER && g->setup.race[2] == KP_RACE_TERRAN && g->setup.force[2] == 2,
		"slot 2: the computer");
	kp_t_ok(T, g->setup.type[3] == KP_CTRL_RESCUE && g->setup.race[3] == KP_RACE_PROTOSS, "slot 3: the rescuable moved");
	kp_t_eq(T, g->setup.type[4], KP_CTRL_NONE, "the map's computer slot empty");
	kp_t_ok(T, g->setup.race_summary[0] == KP_RACE_ZERG && g->setup.race_summary[1] == KP_RACE_TERRAN
		&& g->setup.race_summary[2] == KP_RACE_PROTOSS && g->setup.race_summary[4] == KP_RACE_TERRAN,
		"the lobby's races in its order");
	kp_t_eq(T, g->local_record, 0, "the seat's record: Storm ID 1's");
	kp_t_eq(T, g->local_player, 1, "it plays as its force's leader");
	kp_t_eq(T, g->local_vision, 2, "with the leader's vision");
	kp_t_eq(T, kp_setup_record_player(&g->setup, 0), 1, "a member's commands play as the leader");
	kp_t_eq(T, kp_setup_record_player(&g->setup, 1), 1, "the leader's as itself");
	kp_t_eq(T, kp_setup_record_player(&g->setup, 2), 2, "a computer's as itself");
	g = team_game(T, &s, 0);
	kp_t_ok(T, g->local_record == 1 && g->local_player == 1, "Storm ID 0's seat: the leader's record");

	s.template_record[KP_TEMPLATE_FIXED_POSITIONS] = 0;
	g = team_game(T, &s, 0);
	leaders[0] = g->setup.leader[1];
	leaders[1] = g->setup.leader[2];
	kp_t_ok(T, leaders[0] != leaders[1] && (leaders[0] == 1 || leaders[0] == 2 || leaders[0] == 4)
		&& (leaders[1] == 1 || leaders[1] == 2 || leaders[1] == 4), "random places among the offered slots: %lld %lld",
		(long long)leaders[0], (long long)leaders[1]);
	kp_t_ok(T, g->setup.type[leaders[0]] == KP_CTRL_HUMAN && g->setup.storm[leaders[0]] == 0
		&& g->setup.type[leaders[1]] == KP_CTRL_COMPUTER, "each leader's record in its slot");

	s.template_record[KP_TEMPLATE_TEAMS] = 5;
	kp_t_eq(T, kp_setup_team_size(&s), -1, "five teams: no team size (kp_start refuses it, error 0x5d)");
	s.multiplayer = 0;
	kp_t_eq(T, kp_setup_team_game(&s), 0, "no team game without the multiplayer byte");
}

/* CreateTeamStartingUnits 0x47eb00: each team's leader gets the town hall of its block's first lobby race (Zerg: the
 * Overlord too) and four workers of the block's races in turn at its start location; the members own nothing.
 * InitUnitCountsAndSupplyMax 0x414400: a leader's supply cap of a race is 400 times its block's lobby races of it, at
 * least 400; every other player's 0. */
static void team_units_and_supply_are_the_leaders(KpT *T)
{
	KpGameSetup s;
	KpGame *g;
	if (!kp_t_ready(T, "test_multiplayer", FILES)) return;
	team_setup(&s, KP_RACE_ZERG, KP_RACE_TERRAN);
	g = team_game(T, &s, 1);
	kp_t_eq(T, owned(g, 1, KP_TYPE_HATCHERY), 1, "team 1: the Hatchery of record 0's race");
	kp_t_eq(T, owned(g, 1, KP_TYPE_OVERLORD), 1, "and its Overlord");
	kp_t_ok(T, owned(g, 1, KP_TYPE_DRONE) == 2 && owned(g, 1, KP_TYPE_SCV) == 2, "two Drones, two SCVs");
	kp_t_eq(T, owned(g, 1, KP_TYPE_LARVA), 2, "the Hatchery's larvae");
	kp_t_eq(T, owned(g, 0, KP_TYPE_NONE), 0, "the member owns nothing");
	kp_t_eq(T, owned(g, 2, KP_TYPE_NEXUS), 1, "team 2: the Nexus of the rescuable's lobby race");
	kp_t_ok(T, owned(g, 2, KP_TYPE_PROBE) == 2 && owned(g, 2, KP_TYPE_DRONE) == 2, "Probes and the empty slot's Drones");
	kp_t_eq(T, owned(g, 3, KP_TYPE_NONE), 0, "the rescuable: the map's units left out");
	kp_t_ok(T, kp_supply_max(g, KP_TYPE_DRONE, 1) == 400 && kp_supply_max(g, KP_TYPE_SCV, 1) == 400
		&& kp_supply_max(g, KP_TYPE_PROBE, 1) == 400, "a leader's caps: at least 400");
	kp_t_ok(T, kp_supply_max(g, KP_TYPE_DRONE, 0) == 0 && kp_supply_max(g, KP_TYPE_SCV, 3) == 0, "the others' 0");
	team_setup(&s, KP_RACE_ZERG, KP_RACE_ZERG);
	g = team_game(T, &s, 1);
	kp_t_ok(T, kp_supply_max(g, KP_TYPE_DRONE, 1) == 800 && kp_supply_max(g, KP_TYPE_SCV, 1) == 400,
		"two Zerg in the block: 800");
	kp_t_eq(T, owned(g, 1, KP_TYPE_DRONE), 4, "four Drones");
}

/* NextActivePlayer 0x4b3170 in a team game: the teams' leaders in the forces' order, the members never (nor the
 * rescuable); a trigger of a member's slot has no copy (ExpandTriggerOwners 0x4bdb00), its force's goes to the leader;
 * the defeat names the force (AnnouncePlayerDefeat 0x4bd4a0, network.tbl 0x35 with GetForceName 0x4b2bf0). */
static void team_triggers_run_the_leaders(KpT *T)
{
	static uint8_t records[3 * KP_TRIGGER_RECORD];
	KpGameSetup s;
	KpGame *g;
	int64_t active[KP_PLAYERS], string = 0, force = -1, c, since;
	int n, k;
	if (!kp_t_ready(T, "test_multiplayer", FILES)) return;
	team_setup(&s, KP_RACE_ZERG, KP_RACE_TERRAN);
	g = team_game(T, &s, 1);
	n = kp_trigger_active_players(g, active);
	kp_t_ok(T, n == 2 && active[0] == 1 && active[1] == 2, "the leaders: %d", n);
	defeat_trigger(records, 0);
	defeat_trigger(records + KP_TRIGGER_RECORD, 2);
	memset(records + 2 * KP_TRIGGER_RECORD, 0, KP_TRIGGER_RECORD);
	records[2 * KP_TRIGGER_RECORD + KP_TRIG_OWNERS_AT + 18] = 1; /* Force 1 */
	kp_triggers_replace(g, records, sizeof records);
	kp_t_eq(T, g->trig.head[0], -1, "the member's slot: no copy");
	kp_t_ok(T, g->trig.head[1] >= 0 && g->trig.head[2] >= 0, "Force 1's copy the leader's");
	since = kp_event_sequence(g);
	for (k = 0; k <= PASS_STEPS && g->results[2] == 0; k++) kp_trigger_step(g);
	kp_t_eq(T, g->results[2], RESULT_DEFEAT, "the computer's team defeated");
	kp_t_eq(T, events(g, KP_EVENT_FORCE_MESSAGE, since, &string, &force, &c), 1, "one force message");
	kp_t_ok(T, string == ELIMINATED_STRING && force == 2, "its force's name: %lld %lld", (long long)string,
		(long long)force);
	kp_t_eq(T, events(g, KP_EVENT_PLAYER_MESSAGE, since, &string, &force, &c), 0, "no player's name");
	n = kp_trigger_active_players(g, active);
	kp_t_ok(T, n == 1 && active[0] == 1, "the defeated leader leaves the active players");
}

/* Whether sprite s has a dashed circle image. */
static int dashed(KpGame *g, int32_t s)
{
	int32_t i;
	for (i = g->w->spr.images[s].head; i != KP_NONE; i = g->w->img.next[i]) {
		if (g->w->img.id[i] >= DASHED_FIRST && g->w->img.id[i] <= DASHED_LAST) return 1;
	}
	return 0;
}

static int64_t allies(KpGame *g, int64_t n) { return (g->w->spr.flags[kp_unit_sprite(g, n)] & KP_SF_ALLIES) >> 1; }

/* ExecuteGameTurn 0x4768a0 in a team game: a seat's command plays as its force's leader (0x508d44: ownership and the
 * gates) from its own record (0x508d48: its selection row, groups, pauses), the turn's end gives the record back to
 * the local seat; an error shows on the issuing seat only (ShowLocalErrorMessage 0x46ae50). The allied human's row
 * gets the dashed circles (IsTeamAllySelectionRow 0x49c2b0, AddAllyCircle 0x415190: at most the team's size less one),
 * its clearing and removals take them back (0x49c7d0, 0x49c870, 0x49c8b0); the seat's own row has none. */
static void team_commands_play_as_the_leader(KpT *T)
{
	KpGameSetup s;
	KpGame *g;
	KpCommand none;
	int64_t workers[4], n;
	uint8_t pause[1] = { PAUSE_COMMAND }, remove[4];
	int count = 0;
	if (!kp_t_ready(T, "test_multiplayer", FILES)) return;
	team_setup(&s, KP_RACE_TERRAN, KP_RACE_TERRAN);
	g = team_game(T, &s, 1);
	for (n = g->player_units[1]; n != 0 && count < 4; n = g->u.player_next[n]) {
		if (g->u.type[n] == KP_TYPE_SCV) workers[count++] = n;
	}
	kp_t_eq(T, count, 4, "the leader's SCVs");
	select_units(g, 0, workers, 2);
	kp_t_eq(T, kp_scalar(g, KP_COMMAND_PLAYER), 1, "the seat's command plays as the leader");
	kp_t_eq(T, g->command_record, 0, "from its record");
	kp_t_ok(T, g->selections[0][0] == workers[0] && g->selections[0][1] == workers[1], "the record's row, the leader's units");
	kp_t_ok(T, allies(g, workers[0]) == 0 && !dashed(g, kp_unit_sprite(g, workers[0])), "the seat's own row: no dashed circle");
	kp_t_eq(T, kp_error_is_local(g, 1), 1, "the seat's error shows");

	select_units(g, 1, workers + 1, 2);
	kp_t_eq(T, g->command_record, 1, "the teammate's record");
	kp_t_eq(T, kp_error_is_local(g, 1), 0, "the teammate's error does not show here");
	kp_t_ok(T, allies(g, workers[1]) == 1 && dashed(g, kp_unit_sprite(g, workers[1])) && allies(g, workers[2]) == 1,
		"the ally's row: one dashed circle each");
	select_units(g, 1, workers + 1, 2);
	kp_t_ok(T, allies(g, workers[1]) == 1 && allies(g, workers[2]) == 1, "at most the team's size less one");
	remove[0] = REMOVE_COMMAND;
	remove[1] = 1;
	remove[2] = (uint8_t)kp_unit_ref(g, workers[1]);
	remove[3] = (uint8_t)(kp_unit_ref(g, workers[1]) >> 8);
	kp_execute_command(g, 1, remove, sizeof remove);
	kp_t_ok(T, allies(g, workers[1]) == 0 && !dashed(g, kp_unit_sprite(g, workers[1])), "removed: the circle goes");
	kp_t_eq(T, g->selections[1][0], workers[2], "the row without it");
	kp_remove_from_selections(g, workers[2]);
	kp_t_ok(T, allies(g, workers[2]) == 0 && !dashed(g, kp_unit_sprite(g, workers[2])), "out of every row: none");

	g->turn_cancels_cursor = 0;
	kp_execute_command(g, 0, pause, sizeof pause);
	kp_t_ok(T, g->pauses[0] == PAUSES - 1 && g->pauses[1] == PAUSES, "the pause spends the record's");
	memset(&none, 0, sizeof none);
	kp_run_turn(g, &none, 0);
	kp_t_eq(T, g->command_record, g->local_record, "the turn's end: the seat's record again");
}

static const KpTest TESTS[] = {
	{ "alliance_command_sets_the_issuers_stances", alliance_command_sets_the_issuers_stances, 0 },
	{ "replay_circles_are_one_colour", replay_circles_are_one_colour, 0 },
	{ "vision_command_shares_the_issuers_vision", vision_command_shares_the_issuers_vision, 0 },
	{ "order_nothing_command", order_nothing_command, 0 },
	{ "leave_command_retires_the_player_in_a_replay", leave_command_retires_the_player_in_a_replay, 0 },
	{ "retire_kills_in_sudden_death", retire_kills_in_sudden_death, 0 },
	{ "defeat_is_announced_outside_use_map_settings", defeat_is_announced_outside_use_map_settings, 0 },
	{ "transmission_waits_its_time_in_a_network_game", transmission_waits_its_time_in_a_network_game, 0 },
	{ "center_view_moves_the_screen_at_once_in_a_network_game", center_view_moves_the_screen_at_once_in_a_network_game, 0 },
	{ "pause_actions_do_nothing_in_a_network_game", pause_actions_do_nothing_in_a_network_game, 0 },
	{ "result_dialog_does_not_pause_a_network_game", result_dialog_does_not_pause_a_network_game, 0 },
	{ "results_free_the_trigger_lists_in_a_network_game", results_free_the_trigger_lists_in_a_network_game, 0 },
	{ "speed_and_cheats_are_single_players", speed_and_cheats_are_single_players, 0 },
	{ "pauses_are_limited_in_a_network_game", pauses_are_limited_in_a_network_game, 0 },
	{ "saves_and_loads_in_a_network_game", saves_and_loads_in_a_network_game, 0 },
	{ "latency_command_shows_its_line", latency_command_shows_its_line, 0 },
	{ "team_slots_place_the_leaders", team_slots_place_the_leaders, 0 },
	{ "team_units_and_supply_are_the_leaders", team_units_and_supply_are_the_leaders, 0 },
	{ "team_triggers_run_the_leaders", team_triggers_run_the_leaders, 0 },
	{ "team_commands_play_as_the_leader", team_commands_play_as_the_leader, 0 },
};

KP_SUITE(kp_suite_multiplayer, "test_multiplayer", TESTS);
