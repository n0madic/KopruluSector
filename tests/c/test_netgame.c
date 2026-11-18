/* test_netgame: the multiplayer lobby (kp_netgame.h) between Koprulu seats (phase N, stage N3). The stat string's
 * round trip and its refusals, ValidateNetString's classes, and lobbies of whole stacks (KpNetGame over kp_snet over the
 * UDP provider) on the in-process LAN (kp_net_lobby.h): a host and its joiners chat, whisper, ignore, change races,
 * download the map, are kicked and banned, and reach the start; every seat that starts holds the same seed, slots and
 * template. A resumed save (kp_netsave.h): its file read back and refused when malformed, the host's lobby rebuilt from
 * it (melee, Use Map Settings, team), its joiners placed by their saved records and refused without the save. The
 * original's side of the same lobbies is test_lobby_trace's. */
#include "kp_test.h"
#include "kp_test_ui.h"

#include <stdlib.h>

#include "kp_game.h"
#include "kp_gametype.h"
#include "kp_ids.h"
#include "kp_kpf.h"
#include "kp_net.h"
#include "kp_net_lobby.h"
#include "kp_netsave.h"
#include "kp_start.h"
#include "kp_transfer.h"

#define SETTLE_MS 3000 /* a lobby message's round: a few 250 ms passes and Storm's latency */
#define START_MS 30000 /* the countdown (24 turns at 250 ms), the start information, the ready room */
#define RACE_ZERG 0
#define RACE_TERRAN 1
#define RACE_PROTOSS 2
#define SAVE_ID KP_NET_LOBBY_SAVE_ID
#define SAVE_NAME "Saved Test"
#define NO_SAVED 8            /* a lobby slot without a saved record */
#define TEAM_TYPE 0x0b        /* the test's team template */
#define TEAM_SUBTYPE 2
#define TEAM_THREE_TEAMS 3   /* three teams of three (GetTeamSize 0x4b2b10) */

enum { MENU = KP_NET_LOBBY_MENU, ROOM = KP_NET_LOBBY_ROOM, READY_ROOM = KP_NET_LOBBY_READY_ROOM,
	STARTED = KP_NET_LOBBY_STARTED, LEFT = KP_NET_LOBBY_LEFT };

static void lobby_free(void *p)
{
	kp_net_lobby_free((KpNetLobby *)p);
	free(p);
}

static KpNetLobby *lobby_new(KpT *T, uint64_t seed, int seats, int lossy)
{
	KpNetLobby *l = (KpNetLobby *)calloc(1, sizeof *l);
	kp_t_ok(T, l != NULL, "out of memory");
	kp_t_defer(T, lobby_free, l);
	kp_net_lobby_init(l, T, kp_t_fail_text, T, seed, seats, lossy); /* a failure ends the test */
	return l;
}

static void settle(KpNetLobby *l) { kp_net_lobby_advance(l, SETTLE_MS); }
static int slot_of(const KpNetLobby *l, int view, int k) { return kp_net_lobby_slot_of(l, view, k); }

static int has_line(const KpNetLobbySeat *s, const char *text)
{
	int k;
	for (k = 0; k < s->line_count; k++)
		if (strcmp(s->lines[k], text) == 0) return 1;
	return 0;
}

static void clear_lines(KpNetLobby *l)
{
	int k;
	for (k = 0; k < l->n.count; k++) l->seat[k].line_count = 0;
}

static void host(KpT *T, KpNetLobby *l, int template_index)
{
	kp_t_ok(T, kp_net_lobby_host(l, 0, template_index, NULL), "seat 0 hosts the game");
}

static void join(KpT *T, KpNetLobby *l, int k, int has_map)
{
	kp_t_ok(T, kp_net_lobby_join(l, k, has_map, NULL), "seat %d joins (%d games listed)", k,
		l->seat[k].game.listed_count);
}

/* The game a seat starts (kp_netgame_game_setup, kp_netgame_map_path) is the host's: its template, players and races
 * by slot, on the host's map or on the map the seat found or downloaded */
static void same_game(KpT *T, const KpNetLobby *l, int k, const KpGameSetup *a)
{
	const KpNetLobbySeat *seat = &l->seat[k];
	KpGameSetup b;
	int s;
	kp_netgame_game_setup(&seat->game, &b);
	kp_t_ok(T, b.multiplayer == 1 && b.slots == a->slots, "seat %d's game is multiplayer of %d slots (%d)", k,
		(int)a->slots, (int)b.slots);
	kp_t_ok(T, memcmp(b.template_record, a->template_record, KP_TEMPLATE_SIZE) == 0, "seat %d's game template", k);
	kp_t_ok(T, memcmp(b.user_select, a->user_select, sizeof a->user_select) == 0, "seat %d's user select races", k);
	for (s = 0; s < KP_NETGAME_PLAYERS; s++)
		kp_t_ok(T, b.storm[s] == a->storm[s] && b.type[s] == a->type[s] && b.race[s] == a->race[s] &&
				b.force[s] == a->force[s] && strcmp(b.name[s], a->name[s]) == 0,
			"seat %d's game slot %d", k, s);
	if (k == 0 || seat->has_map)
		kp_t_eq_str(T, kp_netgame_map_path(&seat->game), KP_NET_LOBBY_MAP_PATH, "seat %d plays its own map", k);
	else
		kp_t_eq_str(T, kp_netgame_map_path(&seat->game), KP_NET_LOBBY_DOWNLOADS KP_NET_LOBBY_MAP_NAME,
			"seat %d plays the map it downloaded", k);
}

/* Every seat that started has the host's start: the seed, the start information, the player slots and the template
 * (slots 8..11 never travel: a joiner's stay InitJoinedLobby 0x474c60's zeros, as the original's do) */
static void same_start(KpT *T, const KpNetLobby *l)
{
	const KpNetGame *a = &l->seat[0].game;
	KpGameSetup setup;
	int k, s;
	kp_netgame_game_setup(a, &setup);
	same_game(T, l, 0, &setup);
	kp_t_eq(T, l->seat[0].phase, STARTED, "the host starts");
	kp_t_eq(T, a->lobby_state, KP_LOBBY_START, "the host's lobby state");
	kp_t_ok(T, a->seed != 0, "the seed is set");
	for (k = 1; k < l->n.count; k++) {
		const KpNetGame *b = &l->seat[k].game;
		if (l->seat[k].phase == LEFT || l->seat[k].phase == MENU) continue;
		kp_t_eq(T, l->seat[k].phase, STARTED, "seat %d starts", k);
		kp_t_eq(T, b->seed, a->seed, "seat %d's seed", k);
		kp_t_ok(T, memcmp(b->start_info, a->start_info, sizeof a->start_info) == 0, "seat %d's start information", k);
		kp_t_ok(T, memcmp(b->header.template_record, a->header.template_record, KP_TEMPLATE_SIZE) == 0,
			"seat %d's template", k);
		for (s = 0; s < KP_NETGAME_PLAYERS; s++) {
			const KpNetSlot *x = &a->slots[s], *y = &b->slots[s];
			kp_t_ok(T, x->type == y->type && x->race == y->race && x->force == y->force && x->storm == y->storm &&
					strcmp(x->name, y->name) == 0,
				"seat %d's slot %d: type %u race %u force %u storm %d \"%s\", the host's %u %u %u %d \"%s\"", k, s,
				y->type, y->race, y->force, (int)y->storm, y->name, x->type, x->race, x->force, (int)x->storm, x->name);
		}
		same_game(T, l, k, &setup);
		kp_t_eq(T, l->seat[k].errors, 0, "seat %d's errors", k);
	}
	kp_t_eq(T, l->seat[0].errors, 0, "the host's errors");
}

/* --- the tests --- */

/* BuildGameStatString 0x46f470 and ParseGameStatString 0x46f6f0: a header's fields survive the round trip (the
 * defaults as bare commas), and malformed or invalid strings are refused. */
static void stat_string_round_trip(KpT *T)
{
	static const struct { const char *stats; const char *why; } BAD[] = {
		{ ",,,,,,,,,,,Alpha\r", "no title" },
		{ ",,,,,,,,,,,\rTest Map\r", "no creator" },
		{ ",,,,,,,,,,,Alpha\rTest Map\rx", "bytes past the title" },
		{ ",,,,,,,,,,,Alpha\rTest Map", "the title unterminated" },
		{ ",,,,,,,,,,Alpha\rTest Map\r", "a field short" },
		{ "1g,,,,,,,,,,,Alpha\rTest Map\r", "a field not hex" },
		{ ",,,7,,,,,,,,Alpha\rTest Map\r", "speed 7" },
		{ ",,19,,,,,,,,,Alpha\rTest Map\r", "players above the maximum" },
		{ ",,9,,,,,,,,,Alpha\rTest Map\r", "9 players" },
		{ ",,,,7,,,,,,,Alpha\rTest Map\r", "map class 7" },
		{ ",,,,,,,,,8,,Alpha\rTest Map\r", "tileset 8" },
		{ ",,,,,,,,,,,Alpha\rTest\tMap\r", "a tab (formatting) in the title" },
		{ ",,,,,,,,,,,Alpha Alpha Alpha Alpha Alpha\rTest Map\r", "a creator of 29 characters" },
	};
	KpNetGameHeader h, back;
	char stats[KP_NETGAME_STATS];
	size_t k;
	memset(&h, 0, sizeof h);
	h.save_id = 0x1234;
	h.width = 96;
	h.height = 192;
	h.players = 3;
	h.max_players = 6;
	h.speed = 5;
	h.map_class = 2;
	h.type = 0x0f;
	h.sub = 1;
	h.subtype = 3;
	h.key_hash = 0xdeadbeefu;
	h.tileset = 4;
	h.replay = 1;
	snprintf(h.creator, sizeof h.creator, "Alpha");
	snprintf(h.title, sizeof h.title, "Test Map");
	kp_netgame_stat_build(&h, stats, sizeof stats);
	kp_t_eq_str(T, stats, "1234,36,36,5,2,f,1,3,deadbeef,4,1,Alpha\rTest Map\r", "the stat string");
	memset(&back, 0, sizeof back);
	kp_t_ok(T, kp_netgame_stat_parse(stats, &back), "it parses");
	kp_t_ok(T, back.save_id == h.save_id && back.width == h.width && back.height == h.height &&
			back.players == h.players && back.max_players == h.max_players && back.speed == h.speed &&
			back.map_class == h.map_class && back.type == h.type && back.sub == h.sub && back.subtype == h.subtype &&
			back.key_hash == h.key_hash && back.tileset == h.tileset && back.replay == h.replay,
		"the fields back");
	kp_t_eq_str(T, back.creator, "Alpha", "the creator back");
	kp_t_eq_str(T, back.title, "Test Map", "the title back");
	/* the defaults (0, 0x88: 256x256, 0x18: 1 of 8, speed 4) as bare commas */
	memset(&h, 0, sizeof h);
	h.width = h.height = 256;
	h.players = 1;
	h.max_players = 8;
	h.speed = 4;
	snprintf(h.creator, sizeof h.creator, "Beta");
	snprintf(h.title, sizeof h.title, "Map Two");
	kp_netgame_stat_build(&h, stats, sizeof stats);
	kp_t_eq_str(T, stats, ",,,,,,,,,,,Beta\rMap Two\r", "the defaults");
	memset(&back, 0, sizeof back);
	kp_t_ok(T, kp_netgame_stat_parse(stats, &back) && back.width == 256 && back.height == 256 && back.players == 1 &&
			back.max_players == 8 && back.speed == 4,
		"the defaults back");
	/* a CR in a name becomes LF (AppendStatLine 0x46f690), which the title's check refuses: such a game is not listed */
	snprintf(h.title, sizeof h.title, "Map\rTwo");
	kp_netgame_stat_build(&h, stats, sizeof stats);
	kp_t_eq_str(T, stats, ",,,,,,,,,,,Beta\rMap\nTwo\r", "a CR in the title");
	kp_t_ok(T, !kp_netgame_stat_parse(stats, &back), "a title with a LF is refused");
	/* the colour codes (controls 1..8) are a title's own */
	kp_t_ok(T, kp_netgame_stat_parse(",,,,,,,,,,,Alpha\r\x03Test Map\r", &back), "a coloured title");
	for (k = 0; k < sizeof BAD / sizeof BAD[0]; k++) {
		memset(&back, 0, sizeof back);
		kp_t_ok(T, !kp_netgame_stat_parse(BAD[k].stats, &back), "refused: %s", BAD[k].why);
	}
}

/* ValidateNetString 0x476e20: the length with the NUL within the bound, the formatting characters (9..13, 18..20, 26)
 * and the other controls refused unless allowed, an unterminated string refused. */
static void net_string_classes(KpT *T)
{
	kp_t_eq(T, kp_netgame_net_string("Alpha", 32, 32, 0, 0), 6, "a name");
	kp_t_eq(T, kp_netgame_net_string("", 32, 32, 0, 0), 1, "an empty string");
	kp_t_eq(T, kp_netgame_net_string("Alpha", 5, 32, 0, 0), 0, "no NUL within the maximum");
	kp_t_eq(T, kp_netgame_net_string("Alpha", 32, 5, 0, 0), 0, "no NUL within the bytes available");
	kp_t_eq(T, kp_netgame_net_string("Alpha", 6, 6, 0, 0), 6, "the NUL at the bound");
	kp_t_eq(T, kp_netgame_net_string("a\tb", 32, 32, 0, 0), 0, "a tab without formatting");
	kp_t_eq(T, kp_netgame_net_string("a\tb", 32, 32, 1, 0), 4, "a tab with formatting");
	kp_t_eq(T, kp_netgame_net_string("a\x12" "b", 32, 32, 1, 0), 4, "18 is formatting");
	kp_t_eq(T, kp_netgame_net_string("a\x1a" "b", 32, 32, 0, 1), 0, "26 is formatting, not a control");
	kp_t_eq(T, kp_netgame_net_string("a\x01" "b", 32, 32, 1, 0), 0, "a control without controls");
	kp_t_eq(T, kp_netgame_net_string("a\x01" "b", 32, 32, 0, 1), 4, "a control with controls");
	kp_t_eq(T, kp_netgame_net_string("a\x1b" "b", 32, 32, 1, 0), 0, "27 is a control");
	kp_t_eq(T, kp_netgame_net_string("a\xe9" "b", 32, 32, 0, 0), 4, "a byte above 0x7f is text");
}

/* Four seats over a LAN (a lossy one too): three join, the third without the map downloads it; a chat line reaches
 * the others, a whisper only its target, an ignored player's lines stop; a race change shows on every seat; the host
 * starts and every seat starts with the host's seed, slots and template. The same seed gives the same datagrams. */
static uint64_t four_seats(KpT *T, uint64_t seed, int lossy)
{
	KpNetLobby *l = lobby_new(T, seed, 4, lossy);
	KpNetLobbySeat *s = l->seat;
	int k, j;
	host(T, l, KP_NET_LOBBY_MELEE);
	for (k = 1; k < 4; k++) join(T, l, k, k != 2);
	settle(l);
	for (k = 0; k < 4; k++) {
		kp_t_eq(T, s[k].phase, ROOM, "seat %d in the room", k);
		kp_t_eq(T, s[k].game.lobby_state, KP_LOBBY_IN, "seat %d's lobby state", k);
		for (j = 0; j < 4; j++) {
			kp_t_ok(T, slot_of(l, k, j) >= 0, "seat %d sees seat %d's slot", k, j);
			kp_t_eq(T, slot_of(l, k, j), slot_of(l, 0, j), "seat %d's slot of seat %d", k, j);
		}
	}
	kp_t_eq(T, kp_netgame_participants(&s[0].game), 4, "the host's participants");
	settle(l);
	kp_t_ok(T, s[2].download != NULL && s[2].download_size == KP_NET_LOBBY_MAP_SIZE &&
			memcmp(s[2].download, l->map, KP_NET_LOBBY_MAP_SIZE) == 0,
		"seat 2 downloaded the host's map");
	kp_t_ok(T, s[1].download == NULL && s[3].download == NULL, "the seats with the map download nothing");
	/* chat, whisper, ignore */
	clear_lines(l);
	kp_netgame_chat(&s[1].game, "hello from Beta");
	kp_netgame_chat(&s[2].game, "/w Alpha a secret");
	settle(l);
	for (k = 0; k < 4; k++)
		if (k != 1) kp_t_ok(T, has_line(&s[k], "Beta: hello from Beta"), "seat %d reads Beta's line", k);
	kp_t_ok(T, has_line(&s[0], "From Gamma: a secret"), "the whisper reaches Alpha");
	kp_t_ok(T, has_line(&s[2], "To Alpha: a secret"), "the whisperer's own line");
	kp_t_ok(T, !has_line(&s[1], "From Gamma: a secret") && !has_line(&s[3], "From Gamma: a secret"),
		"nobody else reads the whisper");
	clear_lines(l);
	kp_netgame_chat(&s[3].game, "/ignore Beta");
	kp_t_ok(T, has_line(&s[3], "Ignoring Beta"), "the ignore's line");
	kp_netgame_chat(&s[1].game, "again");
	settle(l);
	kp_t_ok(T, has_line(&s[0], "Beta: again"), "the host reads Beta");
	kp_t_ok(T, !has_line(&s[3], "Beta: again"), "Delta ignores Beta");
	/* a race */
	kp_t_ok(T, kp_netgame_request_race(&s[3].game, s[3].game.local_slot, RACE_PROTOSS), "Delta asks for Protoss");
	settle(l);
	for (k = 0; k < 4; k++)
		kp_t_eq(T, s[k].game.slots[slot_of(l, k, 3)].race, RACE_PROTOSS, "seat %d sees Delta's race", k);
	/* the start */
	kp_t_eq(T, kp_netgame_start_check(&s[0].game), 0, "the host may start");
	kp_t_ok(T, kp_netgame_start(&s[0].game), "the host starts");
	kp_net_lobby_run_to_start(l, START_MS);
	same_start(T, l);
	return l->n.trace ^ (uint64_t)l->n.datagrams;
}

static void four_seats_reach_one_start(KpT *T)
{
	uint64_t a = four_seats(T, 7, 0);
	kp_t_ok(T, four_seats(T, 7, 0) == a, "the same seed, the same datagrams");
}

static void four_seats_reach_one_start_on_a_lossy_lan(KpT *T) { four_seats(T, 11, 1); }

/* The host kicks one joiner (Open on its slot) and bans another: each leaves with its reason, the others see the slot
 * open; the kicked player joins again, the banned one is refused again; the rest start together. */
static void kick_and_ban(KpT *T)
{
	KpNetLobby *l = lobby_new(T, 3, 3, 0);
	KpNetLobbySeat *s = l->seat;
	int k, open = 0;
	host(T, l, KP_NET_LOBBY_MELEE);
	for (k = 1; k < 3; k++) join(T, l, k, 1);
	settle(l);
	kp_t_ok(T, kp_netgame_slot_action(&s[0].game, slot_of(l, 0, 1), KP_SLOT_OPEN), "the host opens Beta's slot");
	settle(l);
	kp_t_eq(T, s[1].leave, KP_LEAVE_BOOTED, "Beta is booted");
	kp_t_eq(T, s[1].phase, LEFT, "Beta's room closed");
	kp_t_eq(T, slot_of(l, 0, 1), -1, "the host has no slot of Beta's");
	kp_t_eq(T, kp_netgame_participants(&s[0].game), 2, "the host's participants after the kick");
	for (k = 0; k < KP_NET_LOBBY_MAP_SLOTS; k++) open += s[2].game.slots[k].type == KP_CTRL_OPEN;
	kp_t_eq(T, open, 2, "Gamma sees two open slots");
	kp_t_ok(T, kp_netgame_ban(&s[0].game, slot_of(l, 0, 2)), "the host bans Gamma");
	settle(l);
	kp_t_eq(T, s[2].leave, KP_LEAVE_BANNED, "Gamma is banned");
	kp_t_eq(T, s[2].phase, LEFT, "Gamma's room closed");
	kp_t_eq(T, s[0].game.ban_count, 1, "the host's ban list");
	kp_t_eq_str(T, s[0].game.bans[0], "Gamma", "the banned name");
	join(T, l, 2, 1); /* Storm takes the join; the lobby refuses it */
	settle(l);
	kp_t_eq(T, s[2].leave, KP_LEAVE_BANNED, "Gamma is refused again");
	kp_t_eq(T, s[2].phase, LEFT, "Gamma's room closed again");
	join(T, l, 1, 1);
	settle(l);
	kp_t_eq(T, s[1].phase, ROOM, "Beta is in the room again");
	kp_t_eq(T, s[1].game.lobby_state, KP_LOBBY_IN, "Beta's lobby state");
	kp_t_ok(T, slot_of(l, 0, 1) >= 0, "the host has Beta's slot again");
	kp_t_eq(T, kp_netgame_start_check(&s[0].game), 0, "the host may start");
	kp_t_ok(T, kp_netgame_start(&s[0].game), "the host starts");
	kp_net_lobby_run_to_start(l, START_MS);
	same_start(T, l);
	kp_t_eq(T, s[2].phase, LEFT, "Gamma never starts");
}

/* A Use Map Settings game: the joiner gets the map's force names, the slots stay the map's forces, and after the
 * countdown both seats wait in the ready room until both pressed Start, then start together. */
static void ums_ready_room(KpT *T)
{
	KpNetLobby *l = lobby_new(T, 5, 2, 0);
	KpNetLobbySeat *s = l->seat;
	host(T, l, KP_NET_LOBBY_UMS);
	kp_t_ok(T, kp_netgame_ums(&s[0].game), "the host's game is Use Map Settings");
	join(T, l, 1, 1);
	settle(l);
	kp_t_eq_str(T, s[1].game.force_names[0], "Left Team", "Beta's force 1 name");
	kp_t_eq_str(T, s[1].game.force_names[1], "Right Team", "Beta's force 2 name");
	kp_t_eq(T, s[1].game.slots[slot_of(l, 1, 1)].force, s[0].game.slots[slot_of(l, 0, 1)].force,
		"Beta's force as the host has it");
	kp_t_ok(T, kp_netgame_start(&s[0].game), "the host starts");
	kp_net_lobby_run_to_start(l, START_MS);
	kp_t_eq(T, s[0].phase, READY_ROOM, "the host waits in the ready room");
	kp_t_eq(T, s[1].phase, READY_ROOM, "Beta waits in the ready room");
	kp_t_eq(T, s[0].game.seed, s[1].game.seed, "the seed before the ready room");
	s[0].press_start = 1;
	settle(l);
	kp_t_eq(T, s[0].phase, READY_ROOM, "the host waits for Beta's Start");
	kp_t_eq(T, s[1].phase, READY_ROOM, "Beta has not pressed Start");
	s[1].press_start = 1;
	kp_net_lobby_run_to_start(l, START_MS);
	same_start(T, l);
}


/* --- a resumed save --- */

static void same_save(KpT *T, const KpNetGameSave *a, const KpNetGameSave *b)
{
	uint8_t x[KP_NETGAME_HEADER], y[KP_NETGAME_HEADER];
	int k;
	kp_netgame_header_write(&a->header, x);
	kp_netgame_header_write(&b->header, y);
	kp_t_ok(T, memcmp(x, y, sizeof x) == 0, "the game header");
	kp_t_eq(T, b->rng_state, a->rng_state, "the RNG state");
	for (k = 0; k < KP_NETGAME_SLOTS; k++)
		kp_t_ok(T, a->players[k].slot == b->players[k].slot && a->players[k].storm == b->players[k].storm &&
				a->players[k].type == b->players[k].type && a->players[k].race == b->players[k].race &&
				a->players[k].force == b->players[k].force && strcmp(a->players[k].name, b->players[k].name) == 0,
			"player record %d", k);
	kp_t_eq(T, b->local_slot, a->local_slot, "the local slot");
	kp_t_ok(T, memcmp(a->types, b->types, sizeof a->types) == 0, "the lobby's types");
	kp_t_ok(T, memcmp(a->races, b->races, sizeof a->races) == 0, "the lobby's races");
	kp_t_ok(T, memcmp(a->forces, b->forces, sizeof a->forces) == 0, "the forces");
	for (k = 0; k < KP_NETGAME_FORCES; k++) kp_t_eq_str(T, b->force_names[k], a->force_names[k], "force %d's name", k);
	kp_t_ok(T, b->width == a->width && b->height == a->height && b->tileset == a->tileset, "the map's size, tileset");
	kp_t_eq(T, b->local_player, a->local_player, "the local player");
}

/* A save's file, released when the test ends */
static uint8_t *save_file(KpT *T, const KpNetGameSave *s, int version, int expansion, size_t *size)
{
	uint8_t *data = kp_net_lobby_save_file(s, version, SAVE_NAME, expansion, size);
	kp_t_ok(T, data != NULL, "out of memory");
	kp_t_defer(T, free, data);
	return data;
}

/* ValidateSaveFile 0x499750 and ReadSaveHeaderAndGameBlock 0x499c50 over the test's own save files: a save reads back
 * whole; its display name is the file's base name; another version, a long name, another Brood War byte, a wrong tag,
 * a damaged block and every truncation are refused without reading past the file. */
static void save_file_reads_back(KpT *T)
{
	KpTemplates t;
	KpNetGameSave a, b;
	KpNetGameHeader h;
	char name[KP_NETSAVE_NAME];
	uint8_t *data;
	size_t size, n, at;
	kp_net_lobby_templates(&t);
	kp_net_lobby_melee_save(&a, &t, 2);
	a.forces[0] = a.forces[1] = 1;
	a.forces[2] = 2;
	data = save_file(T, &a, KP_NETSAVE_VERSION, 0, &size);
	kp_t_eq(T, kp_netsave_probe(data, size, "maps\\save\\" SAVE_NAME ".mlt", &h, name), KP_NETSAVE_VALID, "the probe");
	kp_t_eq_str(T, name, SAVE_NAME, "the display name");
	kp_t_eq(T, h.save_id, SAVE_ID, "the probed header's save ID");
	kp_t_eq_str(T, h.name, KP_NET_LOBBY_GAME, "the probed header's name");
	kp_t_eq(T, kp_netsave_probe(data, size, "maps/a.b.mlt", NULL, name), KP_NETSAVE_VALID, "a dotted name");
	kp_t_eq_str(T, name, "a.b", "the name cut at its last '.'");
	kp_t_eq(T, kp_netsave_probe(data, size, "123456789012345678901234567.mlt", NULL, NULL), KP_NETSAVE_VALID,
		"a name of 27 characters");
	kp_t_eq(T, kp_netsave_probe(data, size, "1234567890123456789012345678.mlt", NULL, NULL), KP_NETSAVE_UNREADABLE,
		"a name of 28 characters");
	kp_t_ok(T, kp_netsave_read(data, size, 0, &b), "the save reads");
	same_save(T, &a, &b);
	kp_t_ok(T, !kp_netsave_read(data, size, 1, &b), "a classic save in Brood War");
	data = save_file(T, &a, KP_NETSAVE_VERSION, 1, &size);
	kp_t_ok(T, kp_netsave_read(data, size, 1, &b) && !kp_netsave_read(data, size, 0, &b), "a Brood War save");
	data = save_file(T, &a, KP_NETSAVE_VERSION - 1, 0, &size);
	kp_t_eq(T, kp_netsave_probe(data, size, SAVE_NAME ".mlt", NULL, NULL), KP_NETSAVE_OTHER_VERSION, "version 104");
	at = strlen(KP_NET_LOBBY_SAVE_BANNER) + 1;
	data[at + 2] = 0x68; /* the tag's high WORD 0x68 */
	kp_t_eq(T, kp_netsave_probe(data, size, SAVE_NAME ".mlt", NULL, NULL), KP_NETSAVE_UNREADABLE, "a wrong tag");
	kp_t_ok(T, !kp_netsave_read(data, size, 0, &b), "a wrong tag does not read");
	data[at + 2] = 0x69;
	data[size - 8] ^= 0x55; /* the core block's last bytes */
	kp_t_ok(T, !kp_netsave_read(data, size, 0, &b), "a damaged block does not read");
	data[size - 8] ^= 0x55;
	for (n = 0; n < size; n++) { /* every truncation (a copy of its own: the sanitizer sees a read past it) */
		uint8_t *cut = (uint8_t *)malloc(n ? n : 1);
		kp_t_ok(T, cut != NULL, "out of memory");
		memcpy(cut, data, n);
		kp_t_ok(T, !kp_netsave_read(cut, n, 0, &b), "a save cut at %lu bytes does not read", (unsigned long)n);
		(void)kp_netsave_probe(cut, n, SAVE_NAME ".mlt", NULL, NULL);
		free(cut);
	}
	kp_t_eq(T, kp_netsave_probe(data, size, SAVE_NAME ".mlt", NULL, NULL), KP_NETSAVE_OTHER_VERSION, "restored");
}

/* The first place of text's bytes in data, NULL none */
static uint8_t *find_bytes(uint8_t *data, size_t size, const char *text)
{
	size_t n = strlen(text), k;
	for (k = 0; k + n <= size; k++)
		if (memcmp(data + k, text, n) == 0) return data + k;
	return NULL;
}

/* Koprulu's network save (kp_netsave.h): NETG reads back whole with its name and expansion byte, another schema is
 * another version and another size unreadable; a container of kind SAVE holding it reads as the original's probe does
 * (the display name from the file's name), refused for the other game, a long name, a damaged byte or no NETG; the
 * original's file is no container of Koprulu's. */
static void koprulu_save_reads_back(KpT *T)
{
	static const uint8_t hash[32] = { 1 };
	KpTemplates t;
	KpNetGameSave a, b;
	KpKpfOut section;
	uint8_t netg[KP_NETSAVE_NETG_SIZE], *data, *original;
	char name[KP_NETSAVE_NAME];
	size_t size, original_size;
	int expansion = -1;
	kp_net_lobby_templates(&t);
	kp_net_lobby_melee_save(&a, &t, 2);
	a.forces[0] = a.forces[1] = 1;
	a.forces[2] = 2;
	kp_netsave_netg_write(&a, 1, SAVE_NAME, netg);
	kp_t_eq(T, kp_netsave_netg_read(netg, sizeof netg, &b, &expansion, name), KP_NETSAVE_VALID, "NETG reads");
	same_save(T, &a, &b);
	kp_t_ok(T, expansion == 1 && strcmp(name, SAVE_NAME) == 0, "its expansion byte %d and name \"%s\"", expansion, name);
	kp_t_eq(T, kp_netsave_netg_read(netg, sizeof netg - 1, &b, NULL, NULL), KP_NETSAVE_UNREADABLE, "a short NETG");
	netg[0] = KP_NETSAVE_NETG_VERSION + 1;
	kp_t_eq(T, kp_netsave_netg_read(netg, sizeof netg, &b, NULL, NULL), KP_NETSAVE_OTHER_VERSION, "another schema");
	netg[0] = KP_NETSAVE_NETG_VERSION;
	memset(&section, 0, sizeof section);
	section.fourcc = KP_NETSAVE_NETG;
	section.schema_version = 1;
	section.payload = netg;
	section.size = sizeof netg;
	kp_t_eq(T, kp_kpf_build("SAVE", 2, 0, "test", hash, &section, 1, &data, &size), 0, "the container");
	kp_t_defer(T, free, data);
	kp_t_ok(T, kp_netsave_koprulu(data, size), "Koprulu's container");
	kp_t_eq(T, kp_netsave_koprulu_read(data, size, "maps/save/" SAVE_NAME ".mlx", 1, &b, name), KP_NETSAVE_VALID,
		"the save reads");
	same_save(T, &a, &b);
	kp_t_eq_str(T, name, SAVE_NAME, "the display name");
	kp_t_eq(T, kp_netsave_koprulu_read(data, size, SAVE_NAME ".mlt", 0, &b, NULL), KP_NETSAVE_UNREADABLE,
		"a Brood War save in the original game");
	kp_t_eq(T, kp_netsave_koprulu_read(data, size, SAVE_NAME ".mlt", -1, &b, NULL), KP_NETSAVE_VALID,
		"the probe does not ask the game");
	kp_t_eq(T, kp_netsave_koprulu_read(data, size, "1234567890123456789012345678.mlt", -1, &b, NULL),
		KP_NETSAVE_UNREADABLE, "a name of 28 characters");
	{
		/* a byte of NETG's name, which the section's CRC covers */
		uint8_t *at = find_bytes(data, size, SAVE_NAME);
		kp_t_ok(T, at != NULL, "NETG's name in the container");
		if (at) *at ^= 0x55;
		kp_t_eq(T, kp_netsave_koprulu_read(data, size, SAVE_NAME ".mlt", -1, &b, NULL), KP_NETSAVE_UNREADABLE,
			"a damaged byte (its CRC)");
		if (at) *at ^= 0x55;
	}
	section.fourcc = "XTRA";
	{
		uint8_t *other;
		size_t other_size;
		kp_t_eq(T, kp_kpf_build("SAVE", 2, 0, "test", hash, &section, 1, &other, &other_size), 0, "the container");
		kp_t_eq(T, kp_netsave_koprulu_read(other, other_size, SAVE_NAME ".mlt", -1, &b, NULL), KP_NETSAVE_UNREADABLE,
			"a container without NETG");
		free(other);
	}
	original = save_file(T, &a, KP_NETSAVE_VERSION, 1, &original_size);
	kp_t_ok(T, !kp_netsave_koprulu(original, original_size), "the original's save is not Koprulu's");
	kp_t_eq(T, kp_netsave_koprulu_read(original, original_size, SAVE_NAME ".mlt", -1, &b, NULL), KP_NETSAVE_UNREADABLE,
		"nor read as one");
}

static void slot_is(KpT *T, const KpNetGame *ng, int s, uint8_t type, uint8_t race, uint8_t force, uint8_t saved)
{
	const KpNetSlot *r = &ng->slots[s];
	kp_t_ok(T, r->type == type && (type == KP_CTRL_NONE || r->race == race) && r->force == force,
		"slot %d: type %u race %u force %u, expected %u %u %u", s, r->type, r->race, r->force, type, race, force);
	kp_t_eq(T, ng->saved_slots[s], saved, "slot %d's saved record", s);
}

/* A resumed melee save (RebuildLobbySlotsFromSave 0x479840): the host's lobby has the saved humans' slots open first
 * (their records' races), then the computer as a lobby computer and the rescuable, each holding its saved record; every seat reads the save's
 * header. A joiner takes the first open slot with its own record and race (OnLobbyJoinRequest 0x470af0); a joiner of
 * another RNG state is refused (reason 5), one without the save leaves the lobby at once ("Save Game File Not Found"),
 * one whose record is taken is refused (no slot). The start information carries the host's saved records. */
static void resumed_melee_save(KpT *T)
{
	KpNetLobby *l = lobby_new(T, 13, 5, 0);
	KpNetLobbySeat *s = l->seat;
	KpNetGameSave save[5];
	int k;
	kp_net_lobby_melee_save(&save[0], &l->templates, 0);
	kp_net_lobby_melee_save(&save[1], &l->templates, 2);
	save[2] = save[1];
	save[2].local_slot = 4;
	save[2].local_player = 4;
	save[2].rng_state ^= 1;
	save[4] = save[1];
	s[0].save = &save[0];
	s[1].save = &save[1];
	s[2].save = &save[2];
	s[4].save = &save[4];
	host(T, l, KP_NET_LOBBY_MELEE);
	settle(l);
	kp_t_ok(T, s[0].game.save_open, "the host resumes the save");
	kp_t_eq(T, s[0].game.header.save_id, SAVE_ID, "the host's game is the save's");
	slot_is(T, &s[0].game, 0, KP_CTRL_HUMAN, RACE_TERRAN, 0, 0);
	slot_is(T, &s[0].game, 1, KP_CTRL_OPEN, RACE_PROTOSS, 0, NO_SAVED); /* Beta's record, its race */
	slot_is(T, &s[0].game, 2, KP_CTRL_OPEN, RACE_ZERG, 0, NO_SAVED);     /* Gamma's */
	slot_is(T, &s[0].game, 3, KP_CTRL_LOBBY_COMPUTER, RACE_ZERG, 0, 1);
	slot_is(T, &s[0].game, 4, KP_CTRL_RESCUE, RACE_TERRAN, 0, 3);
	for (k = 5; k < 8; k++) slot_is(T, &s[0].game, k, KP_CTRL_NONE, 0, 0, NO_SAVED);
	kp_t_eq_str(T, s[0].game.slots[0].name, "Alpha", "the host's name");
	join(T, l, 1, 1);
	settle(l);
	kp_t_eq(T, s[1].phase, ROOM, "Beta is in the room");
	kp_t_ok(T, s[1].game.save_open, "Beta resumes the save");
	kp_t_eq(T, slot_of(l, 0, 1), 1, "Beta takes the first open slot");
	slot_is(T, &s[0].game, 1, KP_CTRL_HUMAN, RACE_PROTOSS, 0, 2);
	slot_is(T, &s[1].game, 1, KP_CTRL_HUMAN, RACE_PROTOSS, 0, NO_SAVED); /* the table travels in 0x48 only */
	join(T, l, 2, 1);
	settle(l);
	kp_t_eq(T, s[2].leave, KP_LEAVE_NO_SAVE, "Gamma's RNG state is refused");
	kp_t_eq(T, s[2].phase, LEFT, "Gamma's room closed");
	kp_t_ok(T, !kp_net_lobby_join(l, 3, 1, NULL), "Delta has no save");
	kp_t_eq(T, s[3].errors, 1, "Delta's error (Save Game File Not Found)");
	kp_t_eq(T, s[3].phase, MENU, "Delta never enters the room");
	join(T, l, 4, 1);
	settle(l);
	kp_t_eq(T, s[4].leave, KP_LEAVE_CLOSED, "Epsilon's record is Beta's");
	kp_t_eq(T, kp_netgame_participants(&s[0].game), 3, "the host's participants (two humans, the computer)");
	slot_is(T, &s[0].game, 2, KP_CTRL_OPEN, RACE_ZERG, 0, NO_SAVED);
	kp_t_ok(T, kp_netgame_start(&s[0].game), "the host starts");
	kp_net_lobby_run_to_start(l, START_MS);
	kp_t_eq(T, s[0].phase, STARTED, "the host starts");
	kp_t_eq(T, s[1].phase, STARTED, "Beta starts");
	kp_t_eq(T, s[1].game.seed, s[0].game.seed, "Beta's seed");
	kp_t_ok(T, memcmp(s[1].game.saved_slots, s[0].game.saved_slots, sizeof s[0].game.saved_slots) == 0,
		"Beta's saved records (the start information's)");
	kp_t_eq(T, s[0].errors + s[1].errors, 0, "the players' errors");
}

/* A resumed Use Map Settings save (RebuildSlotsFromSaveSummary 0x479ce0): the slots are the saved lobby's (humans open,
 * computers kept) in the saved forces; a defeated human's and a defeated computer's slots are closed, a computer keeps
 * its record. A joiner asks for its saved slot: a closed one is refused (Gamma's open slot keeps the game listed). */
static void resumed_ums_save(KpT *T)
{
	KpNetLobby *l = lobby_new(T, 17, 2, 0);
	KpNetLobbySeat *s = l->seat;
	KpNetGameSave save[2];
	int k;
	for (k = 0; k < 2; k++) {
		KpNetGameSave *v = &save[k];
		kp_net_lobby_save_init(v, &l->templates, KP_NET_LOBBY_UMS, k);
		kp_net_lobby_save_player(v, 0, KP_CTRL_HUMAN, RACE_TERRAN, 1, "Alpha");
		kp_net_lobby_save_player(v, 1, KP_CTRL_HUMAN, RACE_ZERG, 1, "Beta");
		kp_net_lobby_save_player(v, 2, KP_CTRL_COMPUTER, RACE_PROTOSS, 2, "Computer");
		kp_net_lobby_save_player(v, 3, KP_CTRL_COMPUTER, RACE_ZERG, 2, "Computer");
		kp_net_lobby_save_player(v, 4, KP_CTRL_HUMAN, RACE_PROTOSS, 2, "Gamma");
		v->players[1].type = KP_CTRL_DEFEATED_HUMAN;
		v->players[3].type = KP_CTRL_DEFEATED_COMPUTER;
		v->forces[0] = v->forces[1] = 1;
		v->forces[2] = v->forces[3] = v->forces[4] = 2;
		s[k].save = v;
	}
	host(T, l, KP_NET_LOBBY_UMS);
	settle(l);
	kp_t_ok(T, kp_netgame_ums(&s[0].game), "the save's game is Use Map Settings");
	slot_is(T, &s[0].game, 0, KP_CTRL_HUMAN, RACE_TERRAN, 1, 0);
	slot_is(T, &s[0].game, 1, KP_CTRL_CLOSED, RACE_ZERG, 1, NO_SAVED);
	slot_is(T, &s[0].game, 2, KP_CTRL_COMPUTER, RACE_PROTOSS, 2, 2);
	slot_is(T, &s[0].game, 3, KP_CTRL_CLOSED, RACE_ZERG, 2, NO_SAVED);
	slot_is(T, &s[0].game, 4, KP_CTRL_OPEN, RACE_PROTOSS, 2, NO_SAVED);
	kp_t_eq_str(T, s[0].game.force_names[1], "Right Team", "the save's force name");
	join(T, l, 1, 1);
	settle(l);
	kp_t_eq(T, s[1].leave, KP_LEAVE_CLOSED, "Beta's saved slot is closed (defeated)");
	kp_t_eq(T, s[1].phase, LEFT, "Beta's room closed");
}

/* A resumed team save (RebuildSlotsFromSaveSummary 0x479ce0): the slots take the template's teams (three of three:
 * slots 0..2, 3..5, 6..7); a defeated computer closes its team's slots, a remaining computer's record goes to every slot
 * of its team; a joiner takes the slot it asks for (its saved one). */
static void resumed_team_save(KpT *T)
{
	KpNetLobby *l = lobby_new(T, 19, 2, 0);
	KpNetLobbySeat *s = l->seat;
	KpTemplate *team = &l->templates.list[KP_NET_LOBBY_TEMPLATES];
	KpNetGameSave save[2];
	int k;
	*team = l->templates.list[KP_NET_LOBBY_MELEE];
	snprintf(team->name, sizeof team->name, "Test Team Melee");
	team->record[KP_TEMPLATE_TYPE] = TEAM_TYPE;
	team->record[KP_TEMPLATE_SUBTYPE] = TEAM_SUBTYPE;
	team->record[KP_TEMPLATE_TEAMS] = TEAM_THREE_TEAMS;
	l->templates.count = KP_NET_LOBBY_TEMPLATES + 1;
	for (k = 0; k < 2; k++) {
		KpNetGameSave *v = &save[k];
		kp_net_lobby_save_init(v, &l->templates, KP_NET_LOBBY_TEMPLATES, k);
		kp_net_lobby_save_player(v, 0, KP_CTRL_HUMAN, RACE_TERRAN, 1, "Alpha");
		kp_net_lobby_save_player(v, 1, KP_CTRL_HUMAN, RACE_ZERG, 1, "Beta");
		kp_net_lobby_save_player(v, 3, KP_CTRL_COMPUTER, RACE_PROTOSS, 2, "Computer");
		kp_net_lobby_save_player(v, 6, KP_CTRL_COMPUTER, RACE_ZERG, 3, "Computer");
		v->players[6].type = KP_CTRL_DEFEATED_COMPUTER;
		s[k].save = v;
	}
	host(T, l, KP_NET_LOBBY_TEMPLATES);
	settle(l);
	kp_t_ok(T, kp_netgame_team_game(&s[0].game), "the save's game is a team game");
	slot_is(T, &s[0].game, 0, KP_CTRL_HUMAN, RACE_TERRAN, 1, 0);
	slot_is(T, &s[0].game, 1, KP_CTRL_OPEN, RACE_ZERG, 1, NO_SAVED);
	slot_is(T, &s[0].game, 2, KP_CTRL_NONE, 0, 1, NO_SAVED);
	slot_is(T, &s[0].game, 3, KP_CTRL_COMPUTER, RACE_PROTOSS, 2, 3);
	for (k = 4; k < 6; k++) slot_is(T, &s[0].game, k, KP_CTRL_NONE, 0, 2, 3);
	slot_is(T, &s[0].game, 6, KP_CTRL_CLOSED, RACE_ZERG, 3, NO_SAVED);
	slot_is(T, &s[0].game, 7, KP_CTRL_CLOSED, RACE_ZERG, 3, NO_SAVED);
	join(T, l, 1, 1);
	settle(l);
	kp_t_eq(T, s[1].phase, ROOM, "Beta is in the room");
	kp_t_eq(T, slot_of(l, 0, 1), 1, "Beta takes its saved slot");
	slot_is(T, &s[0].game, 1, KP_CTRL_HUMAN, RACE_ZERG, 1, 1);
}

/* One seat's transfer alone (kp_transfer.h): the seat holds a file the origin offered and uploads it to the receiver the
 * origin names. The acknowledgements and stops of another seat change nothing; the receiver's go on and end the upload. */
typedef struct {
	int sent, last_to;
	uint8_t last_type;
} SendLog;

enum { TX_ORIGIN = 0, TX_SELF = 1, TX_RECEIVER = 2, TX_STRANGER = 3, TX_ID = 5, TX_SIZE = 300, TX_CAPACITY = 137 };

static void tx_send(void *ud, int storm, const uint8_t *packet, size_t size)
{
	SendLog *log = (SendLog *)ud;
	(void)size;
	log->sent++;
	log->last_to = storm;
	log->last_type = packet[0];
}

static uint8_t *tx_read(void *ud, const char *path, size_t *size)
{
	(void)ud;
	(void)path;
	*size = TX_SIZE;
	return (uint8_t *)calloc(1, TX_SIZE);
}

static int tx_find(void *ud, const char *name, uint32_t size, uint32_t crc, char *out, size_t out_size)
{
	(void)ud;
	(void)name;
	(void)size;
	(void)crc;
	snprintf(out, out_size, "%s", "own.scm");
	return 1;
}

static void transfer_upload_obeys_only_its_receiver(KpT *T)
{
	KpTransfer t;
	KpTransferIo io;
	SendLog log;
	uint8_t offer[42], upload[7], ack[7], stop[3];
	int after_upload;
	memset(&t, 0, sizeof t); /* kp_transfer_begin frees what the structure holds */
	memset(&log, 0, sizeof log);
	memset(&io, 0, sizeof io);
	io.send = tx_send;
	io.read = tx_read;
	io.find = tx_find;
	io.ud = &log;
	kp_transfer_begin(&t, &io, TX_SELF, TX_CAPACITY);
	memset(offer, 0, sizeof offer);
	offer[0] = KP_TRANSFER_OFFER;
	offer[1] = TX_ID;
	kp_net_put32(offer + 2, TX_SIZE);
	memcpy(offer + 10, "own.scm", 8);
	kp_t_eq(T, kp_transfer_dispatch(&t, offer, sizeof offer, TX_ORIGIN), 1, "the offer of a file this seat holds");
	upload[0] = KP_TRANSFER_UPLOAD;
	upload[1] = TX_ID;
	upload[2] = TX_RECEIVER;
	kp_net_put32(upload + 3, 0);
	kp_t_eq(T, kp_transfer_dispatch(&t, upload, sizeof upload, TX_ORIGIN), 1, "the origin's upload request");
	after_upload = log.sent;
	kp_t_eq(T, log.last_type, KP_TRANSFER_DATA, "the upload starts with chunks");
	kp_t_eq(T, log.last_to, TX_RECEIVER, "to the receiver");
	ack[0] = KP_TRANSFER_ACK;
	ack[1] = TX_ID;
	ack[2] = TX_ORIGIN;
	kp_net_put32(ack + 3, 64);
	stop[0] = KP_TRANSFER_STOP;
	stop[1] = TX_ID;
	stop[2] = TX_ORIGIN;
	kp_t_eq(T, kp_transfer_dispatch(&t, ack, sizeof ack, TX_STRANGER), 0, "another seat's acknowledgement");
	kp_t_eq(T, log.sent, after_upload, "sends nothing");
	kp_t_eq(T, kp_transfer_dispatch(&t, stop, sizeof stop, TX_STRANGER), 0, "another seat's stop");
	kp_t_eq(T, log.sent, after_upload, "answers nothing");
	kp_t_eq(T, kp_transfer_dispatch(&t, ack, sizeof ack, TX_RECEIVER), 1, "the receiver's acknowledgement");
	kp_t_eq(T, log.sent, after_upload + 1, "sends the next chunk");
	kp_t_eq(T, log.last_to, TX_RECEIVER, "to the receiver");
	kp_t_eq(T, kp_transfer_dispatch(&t, stop, sizeof stop, TX_RECEIVER), 1, "the receiver's stop");
	kp_t_eq(T, log.last_type, KP_TRANSFER_STATUS, "answers with the status");
	kp_t_eq(T, log.last_to, TX_ORIGIN, "to the origin");
	kp_t_eq(T, kp_transfer_dispatch(&t, ack, sizeof ack, TX_RECEIVER), 0, "no acknowledgement of an ended upload");
	kp_transfer_reset(&t);
}

/* --- the game's end (ComputeGameResults 0x477270) --- */

#define TERRAN01 "missions/terran/terran01"
#define ALPHA 1               /* terran01's human record (the seat) and its rescuable's, the network game's humans */
#define BETA 2
#define MELEE 0x02
#define USE_MAP_SETTINGS 0x0a
#define MELEE_VICTORY 1       /* the melee template's victory and starting units */
#define MELEE_STARTING_UNITS 2
#define COMMITTED_DEFEAT 2    /* the committed results 0x5349e0 */
#define COMMITTED_VICTORY 3

/* A network game's results by Storm ID: Alpha (the seat, Storm ID 0) and Beta (Storm ID 1), both active, no computer
 * in the lobby, of template type `type`. */
static KpNetGame *results_game(KpT *T, KpGame *g, int type)
{
	KpNetGame *ng = (KpNetGame *)calloc(1, sizeof *ng);
	int k;
	kp_t_ok(T, ng != NULL, "out of memory");
	kp_t_defer(T, free, ng);
	kp_t_net_game(T, g, ALPHA, BETA, 0);
	for (k = 0; k < KP_SETUP_SLOTS; k++) {
		if (k != ALPHA && k != BETA) g->setup.type[k] = 0;
	}
	g->setup.template_record[KP_TEMPLATE_TYPE] = (uint8_t)type;
	/* a melee's victory and starting units (Use Map Settings has neither: kp_template_ums) */
	g->setup.template_record[KP_TEMPLATE_VICTORY] = type == MELEE ? MELEE_VICTORY : 0;
	g->setup.template_record[KP_TEMPLATE_STARTING_UNITS] = type == MELEE ? MELEE_STARTING_UNITS : 0;
	memset(g->results, 0, sizeof g->results);
	ng->local_storm = 0;
	ng->players[0].state = ng->players[1].state = 1;
	return ng;
}

/* The committed results by Storm ID (1 left 4, 2 defeat 2, 3 victory 1, 5 draw 3), the seat's 0 a defeat; the last
 * other human standing without a result wins when no computer plays, after ResolveDesync's vote: 2 drops the seat (4)
 * and clears the flagged, 1 flags the others as left and clears the seat's; Use Map Settings is a draw for everyone
 * with the seat's own result returned. */
static void game_results_follow_the_original(KpT *T)
{
	static const char *const FILES[] = { KP_T_DATA, TERRAN01, NULL };
	KpGame *g;
	KpNetGame *ng;
	uint8_t out[KP_NETGAME_PLAYERS];
	if (!kp_t_ready(T, "test_netgame.game_results_follow_the_original", FILES)) return;
	g = kp_t_game(T, TERRAN01, 0, 0, NULL)->g;
	ng = results_game(T, g, MELEE);
	g->results[ALPHA] = COMMITTED_VICTORY;
	g->results[BETA] = COMMITTED_DEFEAT;
	kp_t_eq(T, kp_netgame_game_results(ng, g, out), KP_NETGAME_WIN, "the seat's victory");
	kp_t_ok(T, out[0] == KP_NETGAME_WIN && out[1] == KP_NETGAME_LOSS, "%d %d", out[0], out[1]);
	kp_t_ok(T, out[2] == 0 && out[7] == 0, "no other Storm ID");
	ng = results_game(T, g, MELEE);
	g->results[ALPHA] = COMMITTED_DEFEAT;
	kp_t_eq(T, kp_netgame_game_results(ng, g, out), KP_NETGAME_LOSS, "the seat's defeat");
	kp_t_eq(T, out[1], KP_NETGAME_WIN, "Beta standing alone wins");
	kp_t_eq(T, g->results[BETA], COMMITTED_VICTORY, "and its result is committed");
	ng = results_game(T, g, MELEE);
	g->setup.type[4] = KP_CTRL_LOBBY_COMPUTER;
	kp_t_eq(T, kp_netgame_game_results(ng, g, out), KP_NETGAME_LOSS, "no result: a defeat");
	kp_t_eq(T, out[1], 0, "a computer plays: nobody wins by standing");
	ng = results_game(T, g, MELEE);
	ng->drop_flags[1] = 1;
	ng->drop_vote = 2;
	kp_t_eq(T, kp_netgame_game_results(ng, g, out), KP_NETGAME_DISCONNECT, "the minority's seat left");
	kp_t_eq(T, out[1], KP_NETGAME_WIN, "the flagged cleared, then the last one standing");
	ng = results_game(T, g, MELEE);
	ng->drop_flags[1] = 1;
	ng->drop_vote = 2;
	ng->players[1].state = 2; /* dropped: no longer standing */
	(void)kp_netgame_game_results(ng, g, out);
	kp_t_eq(T, out[1], 0, "a dropped flagged one cleared");
	ng = results_game(T, g, MELEE);
	ng->drop_flags[1] = 1;
	ng->drop_vote = 1;
	ng->players[1].state = 2;
	kp_t_eq(T, kp_netgame_game_results(ng, g, out), 0, "an even vote clears the seat's");
	kp_t_eq(T, out[1], KP_NETGAME_DISCONNECT, "the flagged left");
	ng = results_game(T, g, USE_MAP_SETTINGS);
	g->results[ALPHA] = COMMITTED_VICTORY;
	kp_t_eq(T, kp_netgame_game_results(ng, g, out), KP_NETGAME_WIN, "Use Map Settings: the seat's own");
	kp_t_ok(T, out[0] == KP_NETGAME_DRAW && out[1] == KP_NETGAME_DRAW, "a draw for everyone");
}

/* What a network game's save keeps for its lobby (kp_netgame_save_record, SaveGame 0x498d20 in multiplayer): the live
 * header gets the save's ID, Storm's player count as players and maximum, the ID 0 and the computers of slots 0..7;
 * the player table is the started game's records, the summary the lobby's local slot and types with the races after
 * the random ones, the local player the seat's own record, the RNG state the game's. */
static void network_save_keeps_the_lobby_part(KpT *T)
{
	static const char *const FILES[] = { KP_T_DATA, TERRAN01, NULL };
	KpGame *g;
	KpNetGame *ng;
	KpNetGameSave rec;
	int k, computers = 0;
	if (!kp_t_ready(T, "test_netgame.network_save_keeps_the_lobby_part", FILES)) return;
	g = kp_t_game(T, TERRAN01, 0, 0, NULL)->g;
	ng = results_game(T, g, MELEE);
	g->setup.type[5] = KP_CTRL_COMPUTER;
	g->setup.race_summary[ALPHA] = RACE_PROTOSS;
	for (k = 0; k < 8; k++) computers += g->setup.type[k] == KP_CTRL_COMPUTER;
	ng->header.id = 0x77;
	ng->header.max_players = 4;
	ng->local_slot = ALPHA;
	ng->slots[ALPHA].type = KP_CTRL_HUMAN;
	ng->slots[5].type = KP_CTRL_LOBBY_COMPUTER;
	ng->forces[ALPHA] = 1;
	snprintf(ng->force_names[0], sizeof ng->force_names[0], "Force 1");
	ng->tileset = 4;
	ng->width = ng->height = 128;
	kp_netgame_save_record(ng, g, SAVE_ID, &rec);
	kp_t_ok(T, ng->header.save_id == SAVE_ID && ng->header.id == 0, "the live header's save ID and game ID");
	kp_t_ok(T, ng->header.players == 2 && ng->header.max_players == 2, "Storm's players: %d of %d",
		ng->header.players, ng->header.max_players);
	kp_t_eq(T, ng->header.save_players, computers, "the computers of slots 0..7");
	kp_t_eq(T, rec.header.save_id, SAVE_ID, "the save's header");
	kp_t_eq(T, rec.rng_state, (uint32_t)g->rng->state, "the game RNG's state");
	for (k = 0; k < KP_NETGAME_SLOTS; k++)
		kp_t_ok(T, rec.players[k].slot == k && rec.players[k].storm == g->setup.storm[k] &&
				rec.players[k].type == g->setup.type[k] && rec.players[k].race == g->setup.race[k] &&
				rec.players[k].force == g->setup.force[k],
			"player record %d: the started game's", k);
	kp_t_ok(T, strcmp(rec.players[ALPHA].name, "Alpha") == 0 && strcmp(rec.players[BETA].name, "Beta") == 0,
		"the players' names");
	kp_t_ok(T, rec.local_slot == ALPHA && rec.local_player == ALPHA, "the local slot %d and player %u", rec.local_slot,
		rec.local_player);
	kp_t_ok(T, rec.types[ALPHA] == KP_CTRL_HUMAN && rec.types[5] == KP_CTRL_LOBBY_COMPUTER, "the lobby's types");
	kp_t_eq(T, rec.races[ALPHA], RACE_PROTOSS, "the races after the random ones");
	kp_t_ok(T, rec.forces[ALPHA] == 1 && strcmp(rec.force_names[0], "Force 1") == 0, "the forces and their names");
	kp_t_ok(T, rec.tileset == 4 && rec.width == 128 && rec.height == 128, "the map's tileset and size");
}

/* RestoreMultiplayerSaveSlots 0x4790b0 (kp_netgame_restore_slots): the resumed lobby's humans (new Storm IDs, names)
 * back in the records 0x672608 names, a lobby computer as a computer, open slots empty, every other record cleared and
 * only the humans' Storm IDs kept; the lower lobby slot wins a record two name. In a team game a force whose saved
 * leader slot lost its human (and has no computer) gets the force's human of the lowest Storm ID there. */
static void restored_slots_follow_the_original(KpT *T)
{
	KpNetGame *ng = (KpNetGame *)calloc(1, sizeof *ng);
	KpGameSetup s;
	int k;
	kp_t_ok(T, ng != NULL, "out of memory");
	kp_t_defer(T, free, ng);
	memset(&s, 0, sizeof s);
	s.multiplayer = 1;
	for (k = 0; k < KP_SETUP_SLOTS; k++) {
		s.storm[k] = 5;
		s.type[k] = KP_CTRL_HUMAN;
		snprintf(s.name[k], sizeof s.name[k], "Saved %d", k);
	}
	memset(ng->saved_slots, NO_SAVED, sizeof ng->saved_slots);
	ng->slots[0] = (KpNetSlot){ 0, 1, KP_CTRL_HUMAN, RACE_ZERG, 0, "Beta" };
	ng->saved_slots[0] = 2;
	ng->slots[1] = (KpNetSlot){ 1, 0, KP_CTRL_HUMAN, RACE_TERRAN, 0, "Alpha" };
	ng->saved_slots[1] = 0;
	ng->slots[2] = (KpNetSlot){ 2, -1, KP_CTRL_LOBBY_COMPUTER, RACE_PROTOSS, 0, "Computer" };
	ng->saved_slots[2] = 4;
	ng->slots[3] = (KpNetSlot){ 3, -1, KP_CTRL_OPEN, RACE_ZERG, 0, "" };
	ng->saved_slots[3] = 6;
	ng->slots[4] = (KpNetSlot){ 4, 3, KP_CTRL_HUMAN, RACE_ZERG, 0, "Gamma" };
	ng->saved_slots[4] = 2; /* Beta's record too: Beta's slot is lower */
	kp_netgame_restore_slots(ng, &s);
	kp_t_ok(T, s.type[2] == KP_CTRL_HUMAN && s.storm[2] == 1 && strcmp(s.name[2], "Beta") == 0 && s.race[2] == RACE_ZERG,
		"Beta in its saved record 2 (Storm ID %lld, \"%s\")", (long long)s.storm[2], s.name[2]);
	kp_t_ok(T, s.type[0] == KP_CTRL_HUMAN && s.storm[0] == 0 && strcmp(s.name[0], "Alpha") == 0,
		"Alpha in its saved record 0");
	kp_t_ok(T, s.type[4] == KP_CTRL_COMPUTER && s.storm[4] == -1, "the lobby computer as a computer in record 4");
	for (k = 0; k < KP_SETUP_SLOTS; k++) {
		if (k == 0 || k == 2 || k == 4) continue;
		kp_t_ok(T, s.type[k] == KP_CTRL_NONE && s.storm[k] == -1 && s.name[k][0] == 0, "record %d empty", k);
	}
	kp_t_eq(T, kp_setup_storm_slot(&s, 1), 2, "Beta's Storm ID plays record 2");
	kp_t_eq(T, kp_setup_storm_slot(&s, 3), -1, "Gamma lost the record to Beta");
	/* a team game: force 1's saved leader slot 1 lost its human, its human of the lowest Storm ID takes it */
	memset(&s, 0, sizeof s);
	s.multiplayer = 1;
	s.template_record[KP_TEMPLATE_TEAMS] = 2;
	s.leader[1] = 1;
	s.leader[2] = 5;
	memset(ng->slots, 0, sizeof ng->slots);
	memset(ng->saved_slots, NO_SAVED, sizeof ng->saved_slots);
	ng->slots[0] = (KpNetSlot){ 0, 2, KP_CTRL_HUMAN, RACE_ZERG, 1, "Beta" };
	ng->saved_slots[0] = 3;
	ng->slots[1] = (KpNetSlot){ 1, 1, KP_CTRL_HUMAN, RACE_TERRAN, 1, "Alpha" };
	ng->saved_slots[1] = 2;
	ng->slots[2] = (KpNetSlot){ 2, 0, KP_CTRL_HUMAN, RACE_PROTOSS, 2, "Gamma" };
	ng->saved_slots[2] = 6;
	ng->slots[3] = (KpNetSlot){ 3, -1, KP_CTRL_LOBBY_COMPUTER, RACE_PROTOSS, 2, "Computer" };
	ng->saved_slots[3] = 7;
	kp_netgame_restore_slots(ng, &s);
	kp_t_ok(T, kp_setup_team_game(&s), "a team game");
	kp_t_ok(T, s.type[1] == KP_CTRL_HUMAN && s.storm[1] == 1 && strcmp(s.name[1], "Alpha") == 0,
		"Alpha (Storm ID 1, the force's lowest) in the leader's slot 1: \"%s\"", s.name[1]);
	kp_t_ok(T, s.type[2] == KP_CTRL_NONE && s.storm[2] == -1, "Alpha's saved record 2 empty");
	kp_t_ok(T, s.type[3] == KP_CTRL_HUMAN && s.storm[3] == 2, "Beta stays in record 3");
	kp_t_eq(T, kp_setup_record_player(&s, 3), 1, "Beta plays as its force's leader");
	kp_t_ok(T, s.type[6] == KP_CTRL_HUMAN && s.storm[6] == 0, "force 2 has a computer: Gamma stays in record 6");
	kp_t_eq(T, kp_setup_record_player(&s, 6), 5, "Gamma plays as force 2's saved leader");
}

static const KpTest TESTS[] = {
	{ "stat_string_round_trip", stat_string_round_trip, 0 },
	{ "net_string_classes", net_string_classes, 0 },
	{ "four_seats_reach_one_start", four_seats_reach_one_start, 0 },
	{ "four_seats_reach_one_start_on_a_lossy_lan", four_seats_reach_one_start_on_a_lossy_lan, 0 },
	{ "kick_and_ban", kick_and_ban, 0 },
	{ "ums_ready_room", ums_ready_room, 0 },
	{ "save_file_reads_back", save_file_reads_back, 0 },
	{ "koprulu_save_reads_back", koprulu_save_reads_back, 0 },
	{ "resumed_melee_save", resumed_melee_save, 0 },
	{ "resumed_ums_save", resumed_ums_save, 0 },
	{ "resumed_team_save", resumed_team_save, 0 },
	{ "transfer_upload_obeys_only_its_receiver", transfer_upload_obeys_only_its_receiver, 0 },
	{ "game_results_follow_the_original", game_results_follow_the_original, 0 },
	{ "network_save_keeps_the_lobby_part", network_save_keeps_the_lobby_part, 0 },
	{ "restored_slots_follow_the_original", restored_slots_follow_the_original, 0 },
};
KP_SUITE(kp_suite_netgame, "test_netgame", TESTS);
