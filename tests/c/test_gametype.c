/* The game types' start and victory (phase M stage M2, ADR 0024; kp_gametype.h, kp_ctf.c): the lobby's custom races,
 * the Play Custom lobby's records, the music rotation the start draws, the Opponents condition, the actions Modify Unit Resource Amount and Modify Unit
 * Hangar Count, Capture the Flag's beacon orders and messages, and the colour rows the presentation draws with. The
 * starts themselves are compared with the original's in test_scenarios.custom_games_equal_the_original. Runs on
 * terran01's game; needs the converter's generated data and is skipped without it. */
#include "kp_test.h"
#include "kp_game_internal.h"
#include "kp_gametype.h"
#include "kp_ids.h"
#include "kp_ui.h"

#include <string.h>

#define MAP "missions/terran/terran01"
#define SEED 946684819u
#define BEACON_TERRAN 198
#define INTERCEPTORS_UNUPGRADED 4

static const char *const FILES[] = { KP_T_DATA, NULL };

/* The CRT generator's k-th value (1-based) from a seed (rand 0x4d14d4). */
static int64_t crt_value(uint32_t seed, int k)
{
	uint32_t state = seed;
	int i;
	for (i = 0; i < k; i++) state = state * 214013u + 2531011u;
	return (int64_t)((state >> 16) & 0x7fff);
}

static KpGame *game(KpT *T, int64_t mission_index, const KpCarry *carry)
{
	KpOracleOpts o;
	kp_oracle_opts(&o);
	o.host.events = 1;
	if (mission_index != KP_NIL) o.host.mission_index = mission_index;
	o.host.carry = carry;
	return kp_t_game(T, MAP, SEED, 0, &o)->g;
}

/* ApplyCampaignSlotRaces 0x443b90 over a lobby's records: outside Use Map Settings the lobby computers open, the local
 * slot takes its race, each custom race turns the last open slot into a computer of that race; Use Map Settings keeps
 * the records. */
static void custom_races_fill_the_last_open_slots(KpT *T)
{
	static const int64_t RACES[KP_CUSTOM_RACES] = { 0, 2, KP_RACE_NONE, 1, KP_RACE_NONE, KP_RACE_NONE, KP_RACE_NONE,
		KP_RACE_NONE };
	KpGameSetup s, ums;
	int p;
	memset(&s, 0, sizeof s);
	s.template_record[KP_TEMPLATE_TYPE] = 2; /* Melee: victory 1, starting units 1 (not Use Map Settings) */
	s.template_record[KP_TEMPLATE_VICTORY] = KP_VICTORY_MELEE;
	s.template_record[KP_TEMPLATE_STARTING_UNITS] = 2;
	for (p = 0; p < 8; p++) s.type[p] = KP_CTRL_OPEN, s.race[p] = KP_RACE_RANDOM;
	s.type[0] = KP_CTRL_HUMAN;
	s.type[2] = KP_CTRL_LOBBY_COMPUTER;
	s.type[7] = KP_CTRL_CLOSED;
	ums = s;
	memset(ums.template_record, 0, KP_TEMPLATE_SIZE);
	ums.template_record[KP_TEMPLATE_TYPE] = 0x0a;
	kp_setup_custom_races(&s, 0, 1, RACES);
	kp_t_eq(T, s.race[0], 1, "the local slot's race");
	/* the open slots from the top: 6, 5, 4 (the lobby computer of slot 2 opened, never reached) */
	kp_t_ok(T, s.type[6] == KP_CTRL_LOBBY_COMPUTER && s.race[6] == 0, "the first race in the last open slot");
	kp_t_ok(T, s.type[5] == KP_CTRL_LOBBY_COMPUTER && s.race[5] == 2, "the second in the one before");
	kp_t_ok(T, s.type[4] == KP_CTRL_LOBBY_COMPUTER && s.race[4] == 1, "KP_RACE_NONE skipped");
	kp_t_eq(T, s.type[2], KP_CTRL_OPEN, "the lobby's computer opened");
	kp_t_eq(T, s.type[7], KP_CTRL_CLOSED, "a closed slot stays");
	kp_setup_custom_races(&ums, 0, 1, RACES);
	kp_t_ok(T, ums.type[2] == KP_CTRL_LOBBY_COMPUTER && ums.race[0] == KP_RACE_RANDOM && ums.type[6] == KP_CTRL_OPEN,
		"Use Map Settings keeps the records");
}

/* The lobby a single-player custom game leaves (kp_setup_lobby): the map's OWNR and SIDE records, a User Select race
 * as Random with its slot flagged, the force plus one of a slot in the game, the local human in the first open slot
 * with the Storm ID 0 and every other -1; a Use Map Settings local race resolves the random races at once
 * (SetLocalRaceAndResolve 0x479c90), none keeps them for the start; a melee template applies the custom races; a map
 * without an open slot has no lobby. The originals' records of real maps: test_scenarios.lobby_records_are_the_originals. */
static void lobby_takes_the_first_open_slot(KpT *T)
{
	static const int64_t RACES[KP_CUSTOM_RACES] = { 2, KP_RACE_NONE, KP_RACE_NONE, KP_RACE_NONE, KP_RACE_NONE,
		KP_RACE_NONE, KP_RACE_NONE, KP_RACE_NONE };
	uint8_t ums[KP_TEMPLATE_SIZE] = { 0 }, melee[KP_TEMPLATE_SIZE] = { 0 };
	KpMission m;
	KpGameSetup s;
	int p, storms = 0;
	memset(&m, 0, sizeof m);
	ums[KP_TEMPLATE_TYPE] = 0x0a;
	melee[KP_TEMPLATE_TYPE] = 2;
	melee[KP_TEMPLATE_VICTORY] = KP_VICTORY_MELEE;
	melee[KP_TEMPLATE_STARTING_UNITS] = 2;
	m.owners[0] = 3, m.races[0] = 1;                                   /* rescuable */
	m.owners[1] = KP_CTRL_OPEN, m.races[1] = KP_RACE_USER_SELECT;      /* the local human's */
	m.owners[2] = KP_CTRL_LOBBY_COMPUTER, m.races[2] = KP_RACE_RANDOM; /* a computer */
	m.owners[3] = KP_CTRL_OPEN, m.races[3] = 1;
	m.force_player[0] = 0, m.force_player[1] = 1, m.force_player[2] = 1, m.force_player[3] = 3;
	kp_t_eq(T, kp_setup_lobby(&s, &m, ums, "Somebody", KP_RACE_NONE, NULL, 0), 0, "a lobby");
	kp_t_ok(T, s.type[1] == KP_CTRL_HUMAN && strcmp(s.name[1], "Somebody") == 0 && s.storm[1] == 0,
		"the first open slot's the local human's");
	kp_t_ok(T, s.race[1] == KP_RACE_RANDOM && s.user_select[1] && !s.user_select[2], "User Select as Random, flagged");
	kp_t_ok(T, s.type[3] == KP_CTRL_OPEN && s.type[0] == 3 && s.type[2] == KP_CTRL_LOBBY_COMPUTER, "the others' types");
	kp_t_ok(T, s.force[0] == 1 && s.force[1] == 2 && s.force[3] == 4 && s.force[4] == 0, "the force plus one in the game");
	kp_t_eq(T, s.slots, 4, "the slots in the game");
	for (p = 0; p < KP_SETUP_SLOTS; p++) storms += s.storm[p] == -1;
	kp_t_eq(T, storms, KP_SETUP_SLOTS - 1, "every other Storm ID -1");
	kp_t_ok(T, s.type[8] == 0 && s.type[11] == 0, "slots 8..11 empty");
	kp_t_eq(T, s.race[2], KP_RACE_RANDOM, "no local race: the random races left for the start");
	kp_t_eq(T, memcmp(s.template_record, ums, KP_TEMPLATE_SIZE), 0, "the template's record");

	kp_t_eq(T, kp_setup_lobby(&s, &m, ums, "Somebody", 0, NULL, 12345u), 0, "a lobby with the local race");
	kp_t_ok(T, s.race[1] == 0 && s.race[2] >= 0 && s.race[2] <= 2, "the local race, the random races resolved");

	kp_t_eq(T, kp_setup_lobby(&s, &m, melee, "Somebody", 1, RACES, 0), 0, "a melee lobby");
	kp_t_ok(T, s.race[1] == 1 && s.type[3] == KP_CTRL_LOBBY_COMPUTER && s.race[3] == 2 && s.type[2] == KP_CTRL_OPEN,
		"the custom races: the local race, the opponent in the last open slot, the map's computer opened");

	m.owners[1] = m.owners[3] = KP_CTRL_LOBBY_COMPUTER;
	kp_t_eq(T, kp_setup_lobby(&s, &m, ums, "Somebody", KP_RACE_NONE, NULL, 0), -1, "no open slot: no lobby");
}

/* LoadConsoleSkinAndPickMusic 0x443680 at the start: a custom game (row 0) advances the music rotation 0x503354, the
 * session's first drawn as the CRT's first value % 3 after the start's seed; a later game adds 1 and draws nothing; a
 * campaign row keeps it. The next game takes it from the carry (its value plus one). */
static void music_rotation_is_the_starts(KpT *T)
{
	KpCarry carry;
	KpGame *g;
	int64_t first = crt_value(SEED, 1);
	if (!kp_t_ready(T, "test_gametype", FILES)) return;
	g = game(T, 0, NULL);
	kp_t_eq(T, g->music_rotation, (first % 3 + 1) % 3, "the session's first: the draw plus 1");
	kp_t_eq(T, (int64_t)kp_rng_crt_rand(g->crt), crt_value(SEED, 2), "one draw");
	kp_carry_take(g, &carry);
	kp_t_eq(T, carry.music_rotation, g->music_rotation + 1, "the carry keeps it");
	kp_t_eq(T, carry.rng_state, (int64_t)g->rng->state, "and the game RNG's state");
	memset(&carry, 0, sizeof carry);
	carry.music_rotation = 2 + 1;
	g = game(T, 0, &carry);
	kp_t_eq(T, g->music_rotation, 0, "the next game's: plus 1, wrapping at 3");
	kp_t_eq(T, (int64_t)kp_rng_crt_rand(g->crt), crt_value(SEED, 1), "no draw");
	g = game(T, KP_NIL, &carry);
	kp_t_eq(T, g->music_rotation, 2, "a campaign row keeps it");
	g = game(T, KP_NIL, NULL);
	kp_t_eq(T, g->music_rotation, -1, "none yet");
}

/* CountOpponents 0x4ba060: a direct owner's coalition (itself and its mutual allies) marks every other active player
 * that is not a member's mutual ally, rescuable and neutral players left out; the count of the marks (not their sum
 * over the members). A rescuable or neutral owner has none. */
static void opponents_count_a_coalitions_marks(KpT *T)
{
	static const int64_t CONTROLLERS[8] = { KP_CTRL_HUMAN, KP_CTRL_COMPUTER, KP_CTRL_COMPUTER, KP_CTRL_COMPUTER,
		KP_CTRL_COMPUTER, KP_CTRL_COMPUTER, KP_CTRL_RESCUE, KP_CTRL_NEUTRAL };
	KpGame *g;
	KpPlayers *pl;
	int p, q;
	if (!kp_t_ready(T, "test_gametype", FILES)) return;
	g = game(T, KP_NIL, NULL);
	pl = &g->players;
	for (p = 0; p < 8; p++) {
		pl->controller[p] = CONTROLLERS[p];
		for (q = 0; q < 12; q++) pl->alliance[p][q] = p == q;
	}
	pl->alliance[0][1] = pl->alliance[1][0] = 2;
	pl->alliance[2][0] = 2; /* one way: not a member */
	pl->alliance[3][4] = pl->alliance[4][3] = 2;
	pl->alliance[4][5] = pl->alliance[5][4] = 2; /* 5 is 4's ally, not 3's */
	kp_t_eq(T, kp_trigger_opponents(g, 0), 4, "0 and 1 mark 2, 3, 4 and 5");
	kp_t_eq(T, kp_trigger_opponents(g, 3), 4, "3 marks 0, 1, 2, 5 and 4 the same three: four marks");
	kp_t_eq(T, kp_trigger_opponents(g, 5), 4, "5 and 4: 0, 1, 2 and 3");
	kp_t_eq(T, kp_trigger_opponents(g, 6), 0, "a rescuable owner");
	kp_t_eq(T, kp_trigger_opponents(g, 7), 0, "a neutral owner");
	kp_t_eq(T, kp_trigger_opponents(g, 8), 0, "a slot of 8 or more");
	pl->controller[2] = KP_CTRL_NONE;
	kp_t_eq(T, kp_trigger_opponents(g, 0), 3, "an empty slot is no opponent");
}

/* An action record of type `type` at location 1 for the selector, unit type, number and quantity. */
static void action(uint8_t a[32], int type, int64_t player, int64_t unit, int64_t number, int quantity)
{
	memset(a, 0, 32);
	a[KP_ACTION_LOCATION] = 1;
	a[KP_ACTION_PLAYER] = (uint8_t)player;
	a[KP_ACTION_NUMBER] = (uint8_t)number;
	a[KP_ACTION_NUMBER + 1] = (uint8_t)(number >> 8);
	a[KP_ACTION_NUMBER + 2] = (uint8_t)(number >> 16);
	a[KP_ACTION_UNIT] = (uint8_t)unit;
	a[KP_ACTION_UNIT + 1] = (uint8_t)(unit >> 8);
	a[KP_ACTION_TYPE] = (uint8_t)type;
	a[KP_ACTION_MODIFIER] = (uint8_t)quantity;
}

/* Location 1 as the box of 64 pixels about (x, y), every elevation. */
static void location_about(KpGame *g, int64_t x, int64_t y)
{
	int64_t *l = g->trig.locations[0];
	l[KP_LOC_LEFT] = x - 64;
	l[KP_LOC_TOP] = y - 64;
	l[KP_LOC_RIGHT] = x + 64;
	l[KP_LOC_BOTTOM] = y + 64;
	l[KP_LOC_ELEVATION] = 0;
}

/* TrigActModifyResourceAmount 0x4b5d10 (every unit type: a resource container takes the amount's low 16 bits) and
 * TrigActModifyHangarCount 0x4b5dc0 (a Carrier gets Interceptors up to its hangar's limit, docked and completed). */
static void modify_resources_and_hangar(KpT *T)
{
	enum { X = 1200, Y = 1200 };
	uint8_t a[32];
	KpGame *g;
	int64_t field, carrier, marine, k, docked, c;
	if (!kp_t_ready(T, "test_gametype", FILES)) return;
	g = game(T, KP_NIL, NULL);
	location_about(g, X, Y);
	field = kp_unit_create(g, KP_TYPE_FIRST_MINERAL, X, Y, 11);
	marine = kp_unit_create(g, KP_TYPE_MARINE, X + 20, Y, 0);
	carrier = kp_unit_create(g, KP_TYPE_CARRIER, X - 20, Y, 0);
	kp_t_ok(T, field != 0 && marine != 0 && carrier != 0, "the units");
	(void)kp_unit_complete(g, field);
	(void)kp_unit_complete(g, marine);
	(void)kp_unit_complete(g, carrier);
	action(a, KP_ACT_MODIFY_RESOURCES, 11, KP_TYPE_MARINE, 0x12345, 0);
	kp_trigger_modify(g, a);
	kp_t_eq(T, kp_band(g->u.union_0xd0[field], 0xffff), 0x2345, "the amount's low 16 bits, whatever the action's type");
	action(a, KP_ACT_MODIFY_HANGAR, 0, KP_TYPE_CARRIER, 10, 0);
	kp_trigger_modify(g, a);
	docked = kp_band(g->u.union_0xc8[carrier], 0xff);
	kp_t_eq(T, docked, INTERCEPTORS_UNUPGRADED, "Interceptors up to the hangar's limit");
	for (k = 0, c = g->lists[KP_LIST_ACTIVE].head; c != 0; c = g->u.next[c]) k += g->u.type[c] == KP_TYPE_INTERCEPTOR;
	kp_t_eq(T, k, 0, "docked: none in the active list");
	action(a, KP_ACT_MODIFY_HANGAR, 0, KP_TYPE_MARINE, 3, 0);
	kp_trigger_modify(g, a);
	kp_t_eq(T, kp_band(g->u.union_0xc8[carrier], 0xff), INTERCEPTORS_UNUPGRADED, "a Marine has no hangar");
}

/* The flag message events after sequence `since`: how many, the last one's string and player. */
static int flag_messages(KpGame *g, int64_t since, int64_t *string, int64_t *player)
{
	KpEvents *e = &g->events;
	int64_t i;
	int n = 0;
	for (i = 0; i < e->count; i++) {
		int64_t slot = (e->first + i) % KP_EVENTS;
		if (e->kind[slot] != KP_EVENT_PLAYER_MESSAGE || e->seq[slot] <= since) continue;
		*string = e->a[slot];
		*player = e->b[slot];
		n++;
	}
	return n;
}

/* Capture the Flag's beacon: CtfCopInit 0x421440 makes its owner's Flag on it from the tenth minute and starts the
 * scans (CtfCopStarted 0x4213c0, every 16 visits): another player's Flag brought into the box dies with the captured
 * message (string 0x81, the Flag's owner), the beacon's own Flag stays. The messages come only in a Capture the Flag
 * game (victory 4). */
static void ctf_beacon_makes_and_captures_flags(KpT *T)
{
	enum { X = 1200, Y = 1200 };
	KpGame *g;
	int64_t beacon, theirs, k, flags, string = 0, player = -1, since;
	if (!kp_t_ready(T, "test_gametype", FILES)) return;
	g = game(T, KP_NIL, NULL);
	g->setup.template_record[KP_TEMPLATE_VICTORY] = KP_VICTORY_CTF;
	beacon = kp_unit_create(g, BEACON_TERRAN, X, Y, 0);
	kp_t_ok(T, beacon != 0, "the beacon");
	(void)kp_unit_complete(g, beacon);
	kp_scalar_set(g, KP_ELAPSED, 599);
	kp_order_ctfcop_init(g, beacon);
	for (k = 0, flags = 0; k < KP_UNITS; k++) flags += g->u.type[k] == KP_TYPE_FLAG && g->u.sprite[k] != 0;
	kp_t_eq(T, flags, 0, "no Flag before the tenth minute");
	kp_scalar_set(g, KP_ELAPSED, 600);
	kp_order_ctfcop_init(g, beacon);
	kp_t_eq(T, g->u.order[beacon], KP_ORD_CTFCOP_STARTED, "the beacon scans");
	since = kp_event_sequence(g);
	theirs = kp_unit_create(g, KP_TYPE_FLAG, X + 8, Y + 8, 1);
	kp_t_ok(T, theirs != 0, "another player's Flag");
	kp_unit_show(g, theirs); /* in the unit finder, as CtfCopInit's Flag */
	(void)kp_unit_complete(g, theirs);
	kp_order_ctfcop_started(g, beacon);
	kp_t_eq(T, g->u.queue_timer[beacon], 15, "the next scan's timer");
	kp_t_eq(T, flag_messages(g, since, &string, &player), 1, "one message");
	kp_t_ok(T, string == 0x81 && player == 1, "captured, the Flag's owner: %lld %lld", (long long)string, (long long)player);
	for (k = 0, flags = 0; k < KP_UNITS; k++) flags += g->u.type[k] == KP_TYPE_FLAG && g->u.owner[k] == 0 && g->u.sprite[k] != 0;
	kp_t_eq(T, flags, 1, "the beacon's own Flag stays");
	since = kp_event_sequence(g);
	g->setup.template_record[KP_TEMPLATE_VICTORY] = KP_VICTORY_MELEE;
	kp_ctf_flag_taken(g, 0);
	kp_ctf_flag_returned(g, 0);
	kp_t_eq(T, flag_messages(g, since, &string, &player), 0, "no messages outside Capture the Flag");
	g->setup.template_record[KP_TEMPLATE_VICTORY] = KP_VICTORY_CTF;
	kp_ctf_flag_returned(g, 2);
	kp_t_ok(T, flag_messages(g, since, &string, &player) == 1 && string == 0x7f && player == 2, "returned");
}

/* The colour rows (ApplyColorSlotPermutation 0x41d5d0 then ApplyColorAssignmentToRows 0x41d690): player i < 8 draws with
 * the map's COLR entry of its colour, players 8..11 with their own rows; NULL tables are the identity. */
static void colour_rows_are_colr_over_the_colours(KpT *T)
{
	static const uint8_t COLR[12] = { 5, 1, 2, 3, 4, 0, 6, 7, 8, 9, 10, 11 };
	static const int64_t COLOURS[8] = { 2, 0, 1, 3, 4, 5, 7, 6 };
	uint8_t rows[12];
	int i;
	kp_ui_colour_rows(COLR, COLOURS, rows);
	kp_t_ok(T, rows[0] == 2 && rows[1] == 5 && rows[2] == 1 && rows[5] == 0 && rows[6] == 7 && rows[7] == 6,
		"COLR[colour]");
	kp_t_ok(T, rows[8] == 8 && rows[11] == 11, "8..11 their own");
	kp_ui_colour_rows(NULL, NULL, rows);
	for (i = 0; i < 12 && rows[i] == i; i++) {
	}
	kp_t_eq(T, i, 12, "the identity");
}

static const KpTest TESTS[] = {
	{ "custom_races_fill_the_last_open_slots", custom_races_fill_the_last_open_slots, 0 },
	{ "lobby_takes_the_first_open_slot", lobby_takes_the_first_open_slot, 0 },
	{ "music_rotation_is_the_starts", music_rotation_is_the_starts, 0 },
	{ "opponents_count_a_coalitions_marks", opponents_count_a_coalitions_marks, 0 },
	{ "modify_resources_and_hangar", modify_resources_and_hangar, 0 },
	{ "ctf_beacon_makes_and_captures_flags", ctf_beacon_makes_and_captures_flags, 0 },
	{ "colour_rows_are_colr_over_the_colours", colour_rows_are_colr_over_the_colours, 0 },
};
KP_SUITE(kp_suite_gametype, "test_gametype", TESTS);
