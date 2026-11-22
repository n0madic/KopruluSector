/* test_lobby_trace: the multiplayer lobby (kp_netgame.h) replayed against the network oracle's recordings (phase N,
 * stage N3; tools/net_check.py writes tests/golden/oracle/net/ from koprulu-private/emu/net-n2). For each player of
 * each recorded game, KpNetGame runs over an SNet that answers from the recording: every SNet call the lobby makes
 * must be the original's next one, with the original's arguments and data byte for byte, and it gets the original's
 * results; the events the original's SNet dispatched inside a call are dispatched inside it, and the calls the
 * handlers made must be the handlers' here. The clock is the emulator's: each read charges it 50 us first, and before
 * each step of the lobby it is set so that the step's last read before its first call is that call's time in the
 * recording (the 250 ms gates then pass or wait where the original's did). The chat room's screens are the driver: it
 * creates or joins, polls where the original polled, and plays the player's lobby actions (net.py's `actions`: chat
 * lines, slot actions) and the host's Start where the recording shows their first call; the screens' own calls (the
 * latency bars' SNetGetPlayerCaps, SNetGetNumPlayers, the game list's SNetEnumGamesEx, the password's SNetGetGameInfo)
 * are skipped. A resumed game's lobby finds its save among the player's preloaded files (the driver's copies in the
 * guest's maps folder).
 *
 * After the start the game is replayed too (stage N4): the game's caps (ComputeFramesPerTurn 0x445470), then a
 * KpSession of the lobby's setup, seed and Storm ID over the network game (KP_TURNS_NET), whose turns must be the
 * original's: every SNetSendTurn byte for byte, its sync command 0x37 among them (the units' or the resources' hash, the
 * sprites' and the minimap's statistics of the seat's view). The driver runs ReceiveMessages where the original drained
 * its messages and the frames until one polls where it polled; it plays the player's in-game actions where the
 * recording shows them: the local commands (the bytes after a sent turn's 0x37), the chat lines (SNetSendMessage),
 * net.py's memory poke at a frame's end (desync), Drop Players once timeout.bin's countdown ended (drop) and the menu's
 * quit (SNetLeaveGame), where a player that saw the score screen has Koprulu's score data compared with the original's
 * (the results by Storm ID, the roster, the arrays and the colours; net_check.py's "score"). The replay ends at the end
 * of the recording (a kicked or banned player, the host that kicked it,
 * a game the driver stopped), or at the start of a resumed game: its world is the original's save, which Koprulu does
 * not load (the plan leaves a mixed resume out), so RestoreMultiplayerSaveSlots 0x4790b0 is checked on Koprulu's own
 * saves (test_netgame). */
#include "kp_test.h"

#include <stdarg.h>

#include "kp_data.h"
#include "kp_hud.h"
#include "kp_ids.h"
#include "kp_json.h"
#include "kp_net.h"
#include "kp_netgame.h"
#include "kp_netsave.h"
#include "kp_oracle_net.h"
#include "kp_oracle_ui.h"
#include "kp_session.h"
#include "kp_sha256.h"

#define INDEX "tests/golden/oracle/net/index.json"
#define GOLDEN_VERSION KP_ORACLE_NET_VERSION
#define GOLDEN_DIR "tests/golden/oracle/net/"
#define TIME_QUERY_COST_US 50 /* what the emulator's GetTickCount charges the clock before it reads it */
#define POLL_READS 2       /* PollLobbyNetwork 0x46f3c0 and ReceiveTurns 0x475ac0 read the clock before SNetReceiveTurns */
#define FRAMES 8           /* calls inside calls (an event handler's) */
#define DATA_MAX 0x10000
#define VIRTUAL_MAPS "maps\\"          /* where the replay's map files are, for the transfer's paths */
#define VIRTUAL_DOWNLOAD "maps\\download\\"
#define NETWORK_TBL "rez\\network.tbl"
#define SCORE_STATE_FIRST 14 /* the score screens' glue states: race * 2 + 14, + 1 for a victory (RunScoreScreen) */

typedef struct {
	const KpJson *items;
	int next, len;
} Frame;

/* The phases of a player's replay: the menus, the chat room, the ready room, the start (the caps the game asks), the
 * game's first turn (RunGame 0x445310's), the game, back in the menus */
enum { PHASE_MENU, PHASE_ROOM, PHASE_READY_ROOM, PHASE_STARTED, PHASE_GAME_FIRST, PHASE_GAME, PHASE_LEFT };
#define POLL_FRAMES 16 /* the most frames a poll of the turns may wait for (the frames per turn) */

typedef struct {
	KpT *T;
	const char *who;
	const KpJson *player, *actions, *preload;
	int next_action;
	int expansion;
	Frame frames[FRAMES];
	int depth;
	int phase, inited;
	KpNetGame ng;
	int failed;
	char why[512];
	uint32_t error; /* the failed call's error (SErrGetLastError) */
	int64_t last_t;
	int64_t now_us; /* the virtual clock (microseconds; GetTickCount is TICK_BASE + now_us / 1000) */
	uint32_t seed;  /* the host's start information's (its time(NULL)) */
	int leave;      /* the room's leave reason, -1 none */
	/* the map */
	const uint8_t *map;
	size_t map_size;
	uint32_t map_crc;
	char map_name[KP_TRANSFER_NAME];
	char map_path[KP_TRANSFER_PATH]; /* maps\<name> */
	int has_map, downloaded;
	const uint8_t *tbl;
	size_t tbl_size;
	long calls;
	/* the game */
	KpSession *session;
	KpDataMap game_map;
	int map_open;
	long commands_queued;
	uint32_t game_done; /* the in-game actions played (bit k: action k) */
	int scored;         /* the score screen's data equal to the original's */
	uint8_t message[DATA_MAX];
	uint8_t turns[KP_NETGAME_PLAYERS][KP_NETGAME_TURN_MAX];
} Replay;

static int fail(Replay *r, const char *fmt, ...) KP_T_PRINTF(2, 3);
static int fail(Replay *r, const char *fmt, ...)
{
	char msg[400];
	va_list ap;
	if (r->failed++) return 0;
	va_start(ap, fmt);
	vsnprintf(msg, sizeof msg, fmt, ap);
	va_end(ap);
	snprintf(r->why, sizeof r->why, "%s at %lld ms, call %ld: %s", r->who, (long long)(r->last_t / 1000), r->calls, msg);
	return 0;
}

/* The screens' calls and Storm's own passes: not the lobby's */
static int skipped(const KpJson *c)
{
	static const char *const SCREENS[] = { "pump", "join_pass", "SNetDestroy", "SNetInitializeProvider",
		"SNetGetNumPlayers", "SNetGetPlayerCaps", "SNetEnumGamesEx", NULL };
	int k;
	for (k = 0; SCREENS[k]; k++)
		if (kp_oracle_net_is(c, SCREENS[k])) return 1;
	return kp_oracle_net_is(c, "SNetGetGameInfo") && kp_oracle_net_arg(c, 0) == KP_SNET_INFO_PASSWORD;
}

/* The next record of the innermost frame that is the lobby's (NULL: the frame has no more) */
static const KpJson *peek(Replay *r)
{
	Frame *f = &r->frames[r->depth];
	while (f->next < f->len && skipped(kp_json_at(f->items, f->next))) f->next++;
	return f->next < f->len ? kp_json_at(f->items, f->next) : NULL;
}

static const KpJson *take(Replay *r)
{
	const KpJson *c = peek(r);
	if (c) {
		r->frames[r->depth].next++;
		r->last_t = r->now_us = kp_json_field_int(c, "t", r->last_t);
	}
	return c;
}

/* The clock: each read charges TIME_QUERY_COST_US first, as the emulator's GetTickCount does */
static uint32_t clock_now(void *ud)
{
	Replay *r = (Replay *)ud;
	r->now_us += TIME_QUERY_COST_US;
	return KP_ORACLE_NET_TICK(r->now_us);
}

/* The clock before a step of the lobby that reads it `reads` times before its first call, the original's next record:
 * the last read is that record's time (the time between two passes of the screens is theirs) */
static void preset(Replay *r, int reads)
{
	const KpJson *c = peek(r);
	if (c) r->now_us = kp_json_field_int(c, "t", 0) - (int64_t)TIME_QUERY_COST_US * reads;
}

/* A call of the lobby: the original's next record must be fn (NULL and the replay failed otherwise) */
static const KpJson *begin(Replay *r, const char *fn)
{
	const KpJson *c;
	if (r->failed) return NULL;
	c = peek(r);
	if (!kp_oracle_net_is(c, fn)) {
		fail(r, "%s called, the original's next %s %.120s", fn, c ? kp_oracle_net_fn(c) : "(none)",
			c ? kp_json_field_str(c, "given", kp_json_field_str(c, "data", "")) : "");
		return NULL;
	}
	r->calls++;
	return take(r);
}

/* What happened inside the original's call: its events dispatched to the lobby, each followed by its handler's calls */
static void inside(Replay *r, const KpJson *c)
{
	const KpJson *inner = kp_json_get(c, "inner"), *e;
	if (!inner || r->failed) return;
	if (r->depth + 1 >= FRAMES) {
		fail(r, "calls nested deeper than %d", FRAMES);
		return;
	}
	r->depth++;
	r->frames[r->depth].items = inner;
	r->frames[r->depth].next = 0;
	r->frames[r->depth].len = kp_json_len(inner);
	while (!r->failed && (e = take(r)) != NULL) {
		if (!kp_oracle_net_event(e, &r->ng)) {
			fail(r, "the original's handler called %s, this one did not", kp_oracle_net_fn(e));
			break;
		}
	}
	r->depth--;
}

static int result_of(Replay *r, const KpJson *c)
{
	int result = (int)kp_json_field_int(c, "result", 0);
	if (!result) r->error = KP_SNET_ERROR_NO_DATA;
	return result;
}

/* --- the recorded SNet --- */

static int s_create(void *ud, const char *name, const char *password, const char *stats, uint32_t game_type,
	uint32_t ladder_type, uint32_t mode, const void *tmpl, uint32_t tmpl_size, uint32_t max_players,
	const char *player_name, const char *player_desc, int *player_id)
{
	Replay *r = (Replay *)ud;
	const KpJson *c = begin(r, "SNetCreateLadderGame");
	const char *rec_password;
	if (!c) return 0;
	rec_password = kp_oracle_net_string(c, 1);
	if (strcmp(name, kp_oracle_net_string(c, 0) ? kp_oracle_net_string(c, 0) : "") != 0 ||
		strcmp(password ? password : "", rec_password ? rec_password : "") != 0 ||
		strcmp(stats, kp_oracle_net_string(c, 2) ? kp_oracle_net_string(c, 2) : "") != 0)
		return fail(r, "SNetCreateLadderGame(\"%s\", \"%s\"), the original's (\"%s\", \"%s\")", name, stats,
			kp_oracle_net_string(c, 0) ? kp_oracle_net_string(c, 0) : "", kp_oracle_net_string(c, 2) ? kp_oracle_net_string(c, 2) : "");
	if (game_type != kp_oracle_net_arg(c, 3) || ladder_type != kp_oracle_net_arg(c, 4) || mode != kp_oracle_net_arg(c, 5) || tmpl_size != kp_oracle_net_arg(c, 7) ||
		max_players != kp_oracle_net_arg(c, 8) || !kp_oracle_net_same_hex((const uint8_t *)tmpl, tmpl_size, kp_json_field_str(c, "template", "")))
		return fail(r, "SNetCreateLadderGame: type %#x ladder %u mode %#x %u players, the original's %#x %u %#x %u",
			game_type, ladder_type, mode, max_players, kp_oracle_net_arg(c, 3), kp_oracle_net_arg(c, 4), kp_oracle_net_arg(c, 5), kp_oracle_net_arg(c, 8));
	if (strcmp(player_name, kp_oracle_net_string(c, 9) ? kp_oracle_net_string(c, 9) : "") != 0 ||
		strcmp(player_desc, kp_oracle_net_string(c, 10) ? kp_oracle_net_string(c, 10) : "") != 0)
		return fail(r, "SNetCreateLadderGame: player \"%s\", the original's \"%s\"", player_name,
			kp_oracle_net_string(c, 9) ? kp_oracle_net_string(c, 9) : "");
	*player_id = (int)kp_json_field_int(c, "player_id", -1);
	return result_of(r, c);
}

static int s_join(void *ud, uint32_t game_id, const char *name, const char *password, const char *player_name,
	const char *player_desc, int *player_id)
{
	Replay *r = (Replay *)ud;
	const KpJson *c = begin(r, "SNetJoinGame");
	if (!c) return 0;
	if (game_id != kp_oracle_net_arg(c, 0) || strcmp(name, kp_oracle_net_string(c, 1) ? kp_oracle_net_string(c, 1) : "") != 0 ||
		strcmp(password, kp_oracle_net_string(c, 2) ? kp_oracle_net_string(c, 2) : "") != 0 ||
		strcmp(player_name, kp_oracle_net_string(c, 3) ? kp_oracle_net_string(c, 3) : "") != 0 ||
		strcmp(player_desc, kp_oracle_net_string(c, 4) ? kp_oracle_net_string(c, 4) : "") != 0)
		return fail(r, "SNetJoinGame(%u, \"%s\"), the original's (%u, \"%s\")", game_id, player_name, kp_oracle_net_arg(c, 0),
			kp_oracle_net_string(c, 3) ? kp_oracle_net_string(c, 3) : "");
	inside(r, c);
	*player_id = (int)kp_json_field_int(c, "player_id", -1);
	return result_of(r, c);
}

static int s_leave(void *ud, uint32_t reason)
{
	Replay *r = (Replay *)ud;
	const KpJson *c = begin(r, "SNetLeaveGame");
	if (!c) return 0;
	if (reason != kp_oracle_net_arg(c, 0)) return fail(r, "SNetLeaveGame(%#x), the original's %#x", reason, kp_oracle_net_arg(c, 0));
	return result_of(r, c);
}

static int s_enum(void *ud, int (*fn)(void *fn_ud, const KpSnetGame *g), void *fn_ud, uint32_t *hint)
{
	(void)fn;
	(void)fn_ud;
	(void)hint;
	return fail((Replay *)ud, "SNetEnumGamesEx: the replay's screens never list games");
}

static int s_send_turn(void *ud, const void *data, uint32_t size)
{
	Replay *r = (Replay *)ud;
	const KpJson *c = begin(r, "SNetSendTurn");
	if (!c) return 0;
	if (!kp_oracle_net_same_hex((const uint8_t *)data, size, kp_json_field_str(c, "given", "")))
		return fail(r, "SNetSendTurn: %u bytes from %02x, the original's %.120s", size, size ? *(const uint8_t *)data : 0,
			kp_json_field_str(c, "given", ""));
	return result_of(r, c);
}

static int s_receive_turns(void *ud, int first, int count, const uint8_t **data, uint32_t *sizes, uint32_t *status)
{
	Replay *r = (Replay *)ud;
	const KpJson *c = begin(r, "SNetReceiveTurns");
	int k;
	if (!c) return 0;
	if ((uint32_t)first != kp_oracle_net_arg(c, 0) || (uint32_t)count != kp_oracle_net_arg(c, 1) || count > KP_NETGAME_PLAYERS)
		return fail(r, "SNetReceiveTurns(%d, %d), the original's (%u, %u)", first, count, kp_oracle_net_arg(c, 0), kp_oracle_net_arg(c, 1));
	inside(r, c);
	for (k = 0; k < count; k++) {
		data[k] = NULL;
		sizes[k] = status[k] = 0;
	}
	if (!result_of(r, c)) {
		kp_oracle_net_status(c, count, status);
		return 0;
	}
	if (!kp_oracle_net_turns(c, count, r->turns, data, sizes, status)) return fail(r, "SNetReceiveTurns: the data");
	return 1;
}

static int s_in_transit(void *ud, uint32_t *turns)
{
	Replay *r = (Replay *)ud;
	const KpJson *c = begin(r, "SNetGetTurnsInTransit");
	if (!c) return 0;
	*turns = (uint32_t)kp_json_field_int(c, "in_transit", 0);
	return result_of(r, c);
}

static int s_send_message(void *ud, int target, const void *data, uint32_t size)
{
	Replay *r = (Replay *)ud;
	const KpJson *c = begin(r, "SNetSendMessage");
	const uint8_t *m = (const uint8_t *)data;
	const char *given;
	int same;
	if (!c) return 0;
	given = kp_json_field_str(c, "given", "");
	same = kp_oracle_net_same_message(m, size, given);
	if (target != (int)(int32_t)kp_oracle_net_arg(c, 0) || !same)
		return fail(r, "SNetSendMessage(%d): %u bytes from %02x, the original's (%d) %.120s", target, size,
			size ? m[0] : 0, (int)(int32_t)kp_oracle_net_arg(c, 0), given);
	return result_of(r, c);
}

static int s_receive_message(void *ud, int *sender, const uint8_t **data, uint32_t *size)
{
	Replay *r = (Replay *)ud;
	const KpJson *c = begin(r, "SNetReceiveMessage");
	long n;
	if (!c) return 0;
	inside(r, c);
	if (!result_of(r, c)) return 0;
	n = kp_oracle_net_unhex(kp_json_field_str(c, "data", ""), r->message, sizeof r->message);
	if (n < 0) return fail(r, "SNetReceiveMessage: the original's data");
	*sender = (int)kp_json_field_int(c, "sender", -1);
	*data = r->message;
	*size = (uint32_t)n;
	return 1;
}

static int s_game_info(void *ud, int type, void *buf, uint32_t size, uint32_t *written)
{
	Replay *r = (Replay *)ud;
	const KpJson *c = begin(r, "SNetGetGameInfo");
	long n;
	if (!c) return 0;
	if ((uint32_t)type != kp_oracle_net_arg(c, 0) || size != kp_oracle_net_arg(c, 2))
		return fail(r, "SNetGetGameInfo(%d, %u), the original's (%u, %u)", type, size, kp_oracle_net_arg(c, 0), kp_oracle_net_arg(c, 2));
	n = kp_oracle_net_unhex(kp_json_field_str(c, "info", ""), (uint8_t *)buf, size);
	if (n < 0) return fail(r, "SNetGetGameInfo: the original's %u bytes", size);
	if (written) *written = (uint32_t)n;
	return result_of(r, c);
}

static int s_set_mode(void *ud, uint32_t mode, int make_private)
{
	Replay *r = (Replay *)ud;
	const KpJson *c = begin(r, "SNetSetGameMode");
	if (!c) return 0;
	if (mode != kp_oracle_net_arg(c, 0) || (uint32_t)make_private != kp_oracle_net_arg(c, 1))
		return fail(r, "SNetSetGameMode(%#x, %d), the original's (%#x, %u)", mode, make_private, kp_oracle_net_arg(c, 0), kp_oracle_net_arg(c, 1));
	return result_of(r, c);
}

static int s_player_name(void *ud, int id, char *buf, uint32_t size)
{
	Replay *r = (Replay *)ud;
	const KpJson *c = begin(r, "SNetGetPlayerName");
	if (!c) return 0;
	if ((uint32_t)id != kp_oracle_net_arg(c, 0) || size != kp_oracle_net_arg(c, 2))
		return fail(r, "SNetGetPlayerName(%d, %u), the original's (%u, %u)", id, size, kp_oracle_net_arg(c, 0), kp_oracle_net_arg(c, 2));
	if (size) snprintf(buf, size, "%s", kp_json_field_str(c, "name", ""));
	return result_of(r, c);
}

static int s_provider_caps(void *ud, KpSnetCaps *caps)
{
	Replay *r = (Replay *)ud;
	const KpJson *c = begin(r, "SNetGetProviderCaps");
	if (!c) return 0;
	if (!kp_oracle_net_caps(c, caps)) return fail(r, "SNetGetProviderCaps: the original's caps");
	return result_of(r, c);
}

static int s_drop(void *ud, int id, uint32_t reason)
{
	Replay *r = (Replay *)ud;
	const KpJson *c = begin(r, "SNetDropPlayer");
	if (!c) return 0;
	if ((uint32_t)id != kp_oracle_net_arg(c, 0) || reason != kp_oracle_net_arg(c, 1))
		return fail(r, "SNetDropPlayer(%d, %#x), the original's (%u, %#x)", id, reason, kp_oracle_net_arg(c, 0), kp_oracle_net_arg(c, 1));
	return result_of(r, c);
}

static int s_disconnect(void *ud, uint32_t reason)
{
	Replay *r = (Replay *)ud;
	const KpJson *c = begin(r, "SNetDisconnectAll");
	if (!c) return 0;
	if (reason != kp_oracle_net_arg(c, 0)) return fail(r, "SNetDisconnectAll(%#x), the original's %#x", reason, kp_oracle_net_arg(c, 0));
	return result_of(r, c);
}

static uint32_t s_error(void *ud) { return ((Replay *)ud)->error; }

/* --- the screens' side --- */

static uint32_t h_time(void *ud) { return ((Replay *)ud)->seed; }

/* rez\network.tbl's string `index` (1-based offsets after the count) */
static const char *h_text(void *ud, int index)
{
	Replay *r = (Replay *)ud;
	uint32_t at;
	if (!r->tbl || r->tbl_size < 2 || index < 1 || (uint32_t)index > kp_net_get16(r->tbl) ||
		(size_t)(2 * index + 2) > r->tbl_size)
		return NULL;
	at = kp_net_get16(r->tbl + 2 * index);
	return at < r->tbl_size ? (const char *)r->tbl + at : NULL;
}

static void h_leave(void *ud, int reason) { ((Replay *)ud)->leave = reason; }

/* FindMultiplayerSaveById 0x4995e0 over the player's preloaded files: the save of the game's extension whose header
 * has the ID, read (ReadSaveHeaderAndGameBlock 0x499c50) */
static int h_find_save(void *ud, uint32_t id, KpNetGameSave *out)
{
	Replay *r = (Replay *)ud;
	const char *ext = r->expansion ? KP_NETSAVE_EXT_BW : KP_NETSAVE_EXT;
	int k;
	for (k = 0; k < kp_json_len(r->preload); k++) {
		const KpJson *p = kp_json_at(r->preload, k);
		const char *guest = kp_json_field_str(p, "guest", ""), *base = strrchr(guest, '\\');
		KpNetGameHeader h;
		const uint8_t *bytes;
		size_t size = 0, n;
		base = base ? base + 1 : guest;
		n = strlen(base);
		if (n < strlen(ext) || kp_compare_nocase(base + n - strlen(ext), ext) != 0) continue;
		bytes = kp_t_read(r->T, kp_json_field_str(p, "file", ""), &size);
		if (kp_netsave_probe(bytes, size, base, &h, NULL) == KP_NETSAVE_VALID && h.save_id == id)
			return kp_netsave_read(bytes, size, r->expansion, out);
	}
	return 0;
}

static void h_error(void *ud, int index, const char *detail, int fatal, int in_game)
{
	Replay *r = (Replay *)ud;
	(void)in_game;
	kp_t_log(r->T, "%s: network.tbl %#x reported (%s%s)", r->who, (unsigned)index, detail ? detail : "",
		fatal ? ", fatal" : "");
}

static uint8_t *io_read(void *ud, const char *path, size_t *size)
{
	Replay *r = (Replay *)ud;
	uint8_t *copy;
	if (strcmp(path, r->map_path) != 0 || !r->map) return NULL;
	if (!(copy = (uint8_t *)malloc(r->map_size))) return NULL;
	memcpy(copy, r->map, r->map_size);
	*size = r->map_size;
	return copy;
}

static int io_find(void *ud, const char *name, uint32_t size, uint32_t crc, char *out, size_t out_size)
{
	Replay *r = (Replay *)ud;
	if (!r->has_map || strcmp(name, r->map_name) != 0 || size != r->map_size || crc != r->map_crc) return 0;
	snprintf(out, out_size, "%s", r->map_path);
	return 1;
}

static int io_save(void *ud, const char *name, const uint8_t *data, size_t size, char *out, size_t out_size)
{
	Replay *r = (Replay *)ud;
	if (strcmp(name, r->map_name) != 0 || size != r->map_size || memcmp(data, r->map, size) != 0)
		return fail(r, "the download \"%s\" (%lu bytes) is not the host's map", name, (unsigned long)size);
	r->downloaded = 1;
	snprintf(out, out_size, VIRTUAL_DOWNLOAD "%s", name);
	return 1;
}

/* --- the game's records --- */

typedef struct {
	const KpJson *golden;
	const char *game;
	const KpJson *player;
	const KpJson *host;      /* the host's create record */
	const KpData *data;
	KpMapHeader map;
	const uint8_t *map_bytes;
	size_t map_size;
	char map_name[KP_TRANSFER_NAME];
	uint32_t seed;
	const uint8_t *tbl;
	size_t tbl_size;
	int play; /* the game after the lobby replayed too (not a resumed save's: its world is the original's) */
} Job;

static const KpJson *first_call(const KpJson *calls, const char *fn)
{
	int k;
	for (k = 0; k < kp_json_len(calls); k++)
		if (kp_oracle_net_is(kp_json_at(calls, k), fn)) return kp_json_at(calls, k);
	return NULL;
}

/* The next lobby action of the player of a kind ("chat", "slot"), NULL none */
static const KpJson *next_action(Replay *r, const char *kind)
{
	while (r->next_action < kp_json_len(r->actions)) {
		const KpJson *a = kp_json_at(r->actions, r->next_action++);
		const KpJson *at = kp_json_get(a, "at");
		if (at && kp_json_is(at, KP_JSON_STRING) && strcmp(kp_json_str(at, ""), "lobby") == 0 &&
			strcmp(kp_json_field_str(a, "kind", ""), kind) == 0)
			return a;
	}
	return NULL;
}

/* The other human's slot (the "remote:" slot of an action) */
static int remote_slot(const KpNetGame *ng)
{
	int s;
	for (s = 0; s < 8; s++)
		if (ng->slots[s].type == KP_CTRL_HUMAN && ng->slots[s].storm != ng->local_storm) return s;
	return -1;
}

static void slot_action(Replay *r)
{
	const KpJson *a = next_action(r, "slot");
	const char *v = a ? kp_json_field_str(a, "value", "") : "";
	int s = remote_slot(&r->ng);
	if (!a || strncmp(v, "remote:", 7) != 0 || s < 0) {
		fail(r, "the original's slot action has no action \"remote:...\" left");
		return;
	}
	v += 7;
	if (strcmp(v, "Ban Player!") == 0) kp_netgame_ban(&r->ng, s);
	else if (strcmp(v, "Open") == 0) kp_netgame_slot_action(&r->ng, s, KP_SLOT_OPEN);
	else if (strcmp(v, "Close") == 0) kp_netgame_slot_action(&r->ng, s, KP_SLOT_CLOSE);
	else if (strcmp(v, "Computer") == 0) kp_netgame_slot_action(&r->ng, s, KP_SLOT_COMPUTER);
	else fail(r, "the slot action \"%s\" is not the replay's", v);
}

/* Whether the poll starting at the top frame's next record sends `command` alone in a turn before the poll after it
 * (the briefing's Start: its 0x54 goes out with the poll's turn) */
static int poll_sends(Replay *r, uint8_t command)
{
	const Frame *f = &r->frames[0];
	char hex[3];
	int k;
	snprintf(hex, sizeof hex, "%02x", command);
	for (k = f->next + 1; k < f->len; k++) {
		const KpJson *c = kp_json_at(f->items, k);
		if (kp_oracle_net_is(c, "SNetReceiveTurns") || kp_oracle_net_is(c, "SNetReceiveMessage")) return 0;
		if (kp_oracle_net_is(c, "SNetSendTurn") && strcmp(kp_json_field_str(c, "given", ""), hex) == 0) return 1;
	}
	return 0;
}

static void host_create(Replay *r, const Job *job, const KpJson *c)
{
	KpNetGameHeader h;
	KpNetMap m;
	if (!kp_oracle_net_header(c, &h)) {
		fail(r, "the create record's stat string \"%s\"", kp_oracle_net_string(c, 2) ? kp_oracle_net_string(c, 2) : "");
		return;
	}
	if (!h.save_id) { /* a resumed game's map and slots are the save's (CreateGameFromEntry 0x45bfc0 reads no map) */
		kp_netgame_map(&m, &job->map, r->map_path, &r->ng.hooks);
		snprintf(m.title, sizeof m.title, "%s", h.title);
		kp_netgame_set_map(&r->ng, &m);
	}
	if (!kp_netgame_create(&r->ng, &h, kp_oracle_net_string(c, 1)) || r->failed) {
		fail(r, "kp_netgame_create failed");
		return;
	}
	preset(r, 1); /* ResetLobbyPollClock 0x46f460 */
	if (!kp_netgame_enter(&r->ng)) {
		fail(r, "kp_netgame_enter failed (leave %d)", r->leave);
		return;
	}
	r->ng.in_chat_room = 1;
	r->phase = PHASE_ROOM;
}

static void join(Replay *r, const Job *job, const KpJson *c)
{
	KpNetGameHeader listed;
	if (!kp_oracle_net_header(job->host, &listed)) {
		fail(r, "no host's create record to list");
		return;
	}
	listed.id = kp_oracle_net_arg(c, 0);
	if (!kp_netgame_join(&r->ng, &listed) || r->failed) {
		fail(r, "kp_netgame_join failed");
		return;
	}
	preset(r, 1); /* ResetLobbyPollClock 0x46f460 */
	if (!kp_netgame_enter(&r->ng)) {
		fail(r, "kp_netgame_enter failed (leave %d)", r->leave);
		return;
	}
	r->ng.in_chat_room = 1;
	r->phase = PHASE_ROOM;
}

static void room_poll(Replay *r)
{
	int got;
	preset(r, kp_oracle_net_is(peek(r), "SNetReceiveTurns") ? POLL_READS : 1);
	got = kp_netgame_poll(&r->ng);
	if (r->failed || got != KP_CHAT_START) return;
	/* the chat room's dialog destroyed (ReleaseLobbySession), then the ready room or the game */
	r->ng.in_chat_room = 0;
	kp_netgame_release(&r->ng);
	if (kp_netgame_ready_room(&r->ng)) {
		kp_netgame_briefing_arm(&r->ng);
		r->phase = PHASE_READY_ROOM;
	} else {
		r->phase = PHASE_STARTED;
	}
}

/* --- the game --- */

/* RunGame 0x445310's first turn, polled where the original polled it */
static void game_first(Replay *r)
{
	int rc;
	preset(r, 1);
	rc = kp_session_net_begin(r->session);
	if (rc < 0) fail(r, "the game's first turn: %s", r->session->error);
	else if (rc == 1) r->phase = PHASE_GAME;
}

/* The game's session (StartGame 0x443f40 after the lobby): the map's mission for the template's load (melee or Use Map
 * Settings), the lobby's setup, seed, speed and Storm ID, its turns the network game's; then RunGame 0x445310's steps
 * 7 and 8 (kp_session_net_begin: the turns sent, the first turn polled). */
static void game_start(Replay *r, const Job *job)
{
	const KpData *d = job->data;
	KpSessionStart st;
	KpGameSetup setup;
	KpMission m;
	KpError e;
	char name[KP_TRANSFER_NAME + 8];
	kp_netgame_game_setup(&r->ng, &setup);
	if (kp_data_map_open(&r->game_map, &d->files, kp_json_field_str(job->golden, "map", ""), &e) != 0) {
		fail(r, "the game's map: %s", e.msg);
		return;
	}
	r->map_open = 1;
	if (kp_data_map_mission(&r->game_map, kp_template_ums(setup.template_record) ? KP_CHK_UMS : KP_CHK_MELEE, &m, &e)
		!= 0) {
		fail(r, "the game's mission: %s", e.msg);
		return;
	}
	if (!(r->session = (KpSession *)calloc(1, sizeof *r->session))) {
		kp_mission_free(&m);
		fail(r, "out of memory");
		return;
	}
	snprintf(name, sizeof name, "maps/%s", job->map_name);
	if (kp_session_load_from(r->session, &d->shared, &d->dat, &m, name, &r->game_map.files) != 0) {
		fail(r, "the game does not load: %s", r->session->error);
		return;
	}
	kp_session_start_defaults(&st);
	st.seed = r->ng.seed;
	st.expansion = r->expansion;
	st.speed = r->ng.header.speed + 1;
	st.has_setup = 1;
	st.setup = setup;
	st.local_storm = r->ng.local_storm;
	st.net = &r->ng;
	if (kp_session_start(r->session, &st) != 0) {
		fail(r, "the game does not start: %s", r->session->error);
		return;
	}
	kp_t_guard(r->T, r->session->g);
	r->phase = PHASE_GAME_FIRST;
	game_first(r);
}

/* The local commands of the poll's first turn sent: the recorded SendTurn's bytes past its 0x37, queued since the turn
 * before (QueueTurnCommand 0x476c80) */
static void queue_local(Replay *r)
{
	const Frame *f = &r->frames[0];
	int k;
	for (k = f->next + 1; k < f->len; k++) {
		const KpJson *c = kp_json_at(f->items, k);
		uint8_t b[KP_NETGAME_TURN_MAX];
		long n;
		if (kp_oracle_net_is(c, "SNetReceiveTurns") || kp_oracle_net_is(c, "SNetReceiveMessage")) return;
		if (!kp_oracle_net_is(c, "SNetSendTurn")) continue;
		n = kp_oracle_net_unhex(kp_json_field_str(c, "given", ""), b, sizeof b);
		if (n > KP_SYNC_SIZE && b[0] == KP_SYNC_ID) {
			if (kp_netgame_command(&r->ng, b + KP_SYNC_SIZE, (size_t)(n - KP_SYNC_SIZE)) != 0)
				fail(r, "the local commands %.60s do not queue", kp_json_field_str(c, "given", ""));
			r->commands_queued++;
		}
		return;
	}
}

/* The player's next in-game action of a kind (its "at" a frame; net.py's), NULL none; at >= 0 only one at that frame */
static const KpJson *game_action(Replay *r, const char *kind, int64_t at)
{
	int k;
	for (k = 0; k < kp_json_len(r->actions) && k < 32; k++) {
		const KpJson *a = kp_json_at(r->actions, k), *when = kp_json_get(a, "at");
		if ((r->game_done & (1u << k)) || !when || kp_json_is(when, KP_JSON_STRING) ||
			strcmp(kp_json_field_str(a, "kind", ""), kind) != 0 || (at >= 0 && kp_json_int(when, -1) != at))
			continue;
		r->game_done |= 1u << k;
		return a;
	}
	return NULL;
}

/* A memory write of net.py's at the end of a frame (after its triggers, FRAME_END_PC): the form the recordings use,
 * "*665AE0+8:HEX", the first active unit's hit points (CUnit +8) */
static void game_pokes(Replay *r)
{
	KpGame *g = r->session->g;
	const KpJson *a;
	while ((a = game_action(r, "poke", kp_scalar(g, KP_FRAME))) != NULL) {
		const char *v = kp_json_field_str(a, "value", "");
		uint8_t b[4];
		int64_t n = g->lists[KP_LIST_ACTIVE].head;
		if (strncmp(v, "*665AE0+8:", 10) != 0 || kp_hex_decode(v + 10, b, sizeof b) != 0 || n == 0) {
			fail(r, "the poke %s is not one the replay makes", v);
			return;
		}
		kp_set_unit_hp(g, n, (int64_t)kp_net_get32(b));
	}
}

/* A poll of the turns where the original's next record is its SNetReceiveTurns: the frames run until one polls (the
 * frames between turns make no call) */
static void game_poll(Replay *r)
{
	int at = r->frames[0].next, k;
	queue_local(r);
	for (k = 0; k < POLL_FRAMES && !r->failed && r->frames[0].next == at; k++) {
		preset(r, 1); /* ReceiveTurns 0x475ac0 reads the clock before SNetReceiveTurns */
		(void)kp_session_step(r->session, NULL);
		if (r->session->failed) fail(r, "the game's step: %s", r->session->error);
		else game_pokes(r);
	}
}

/* The player's chat line in game (SendTextboxChat 0x4b3d60 -> SendInGameChat 0x473be0): its recipients the Storm IDs
 * of the original's run of the same message */
static void game_chat(Replay *r)
{
	const Frame *f = &r->frames[0];
	const KpJson *a = game_action(r, "chat", -1);
	const char *given = kp_json_field_str(peek(r), "given", "");
	uint16_t recipients = 0;
	int k;
	if (!a) {
		fail(r, "the original's in-game message %.40s has no chat action left", given);
		return;
	}
	for (k = f->next; k < f->len; k++) {
		const KpJson *c = kp_json_at(f->items, k);
		if (skipped(c)) continue;
		if (!kp_oracle_net_is(c, "SNetSendMessage") || strcmp(kp_json_field_str(c, "given", ""), given) != 0) break;
		recipients |= (uint16_t)(1u << (kp_oracle_net_arg(c, 0) & 15));
	}
	kp_netgame_game_chat(&r->ng, kp_json_field_str(a, "value", ""), recipients);
}

/* The score screen's data (have: Koprulu's, want: the original's in score state state, its local result local): the
 * first difference fails the replay (fail keeps the first) */
static void score_compare(Replay *r, const KpUiScoreData *have, const KpUiScoreData *want, int state, int local)
{
	int k, j;
	if (have->result != local || have->race != (state - SCORE_STATE_FIRST) / 2 ||
		(have->result == KP_NETGAME_WIN) != ((state - SCORE_STATE_FIRST) % 2 == 1))
		fail(r, "the score's local result %d and race %d, the original's %d in state %d", have->result, have->race, local,
			state);
	for (k = 0; k < KP_NETGAME_PLAYERS; k++)
		if (have->storm_results[k] != want->storm_results[k])
			fail(r, "Storm player %d's result %d, the original's %d", k, have->storm_results[k], want->storm_results[k]);
	if (have->count != want->count) fail(r, "the roster's %d entries, the original's %d", have->count, want->count);
	for (k = 0; k < KP_UI_SCORE_SLOTS; k++) {
		const KpUiRosterEntry *h = &have->roster[k], *w = &want->roster[k];
		if (strcmp(h->name, w->name) != 0 || strcmp(h->force, w->force) != 0 || h->index != w->index ||
			h->storm != w->storm || h->type != w->type || h->race != w->race)
			fail(r, "the roster's slot %d: \"%s\" \"%s\" index %u storm %#x type %u race %u, the original's \"%s\" \"%s\" %u "
				"%#x %u %u", k, h->name, h->force, h->index, h->storm, h->type, h->race, w->name, w->force, w->index, w->storm,
				w->type, w->race);
	}
	for (k = 0; k < KP_UI_SCORE_ARRAYS; k++)
		for (j = 0; j < 12; j++)
			if (have->arrays[k][j] != want->arrays[k][j])
				fail(r, "the score's %s of player %d: %d, the original's %d", KP_ORACLE_SCORE_ARRAYS[k], j, have->arrays[k][j],
					want->arrays[k][j]);
	if (memcmp(have->colours, want->colours, sizeof have->colours) != 0)
		fail(r, "the score's colour indexes differ from the original's");
	if (memcmp(have->player_colours, want->player_colours, sizeof have->player_colours) != 0)
		fail(r, "the score's slot colours differ from the original's");
}

/* The score screen the player saw after leaving the game (net_check.py's "score": RunScoreScreen 0x461940's data
 * past its ComputeGameResults 0x477270) against Koprulu's where the player quits: the network results as the flow
 * takes them, while Storm's game still runs (kp_netgame_game_results), then the score screen's data (kp_glue_score_data)
 * over an interface that shows the game with its colours, as the flow's console does. The seconds are the interface's
 * clock (GetElapsedSeconds 0x47a780), not the game's: menus_check.py shows the original's. */
static void score_check(Replay *r, const Job *job)
{
	const KpJson *score = kp_json_get(r->player, "score");
	const KpData *d = job->data;
	const KpMission *mission = kp_session_mission(r->session);
	KpGame *g = r->session->g;
	KpUiScoreData have, want;
	uint8_t results[KP_NETGAME_PLAYERS] = { 0 };
	KpTileset tileset;
	KpHudMap map;
	KpError e;
	KpUi *ui;
	char why[KP_ERROR_SIZE];
	int local;
	if (!score || !kp_json_is(score, KP_JSON_OBJECT)) return;
	local = kp_netgame_game_results(&r->ng, g, results);
	if (kp_tileset_from_files(&tileset, &d->files, (int)(mission->tileset & 7), &e) != 0) {
		fail(r, "the score's tileset: %s", e.msg);
		return;
	}
	if (!(ui = kp_interface_new_data(&d->ui, NULL, why, sizeof why))) {
		kp_tileset_free(&tileset);
		fail(r, "the score's interface: %s", why);
		return;
	}
	ui->game = g;
	ui->menus.multiplayer = 1;
	memset(&map, 0, sizeof map);
	map.tileset = &tileset;
	map.palettes = &d->palettes;
	map.tileset_index = (int)(mission->tileset & 7);
	map.colors = mission->has_colors ? mission->colors : NULL;
	map.player_colors = g->setup.color; /* the game's colours (0x5264fc), as the flow's console_init gives them */
	if (kp_hud_colours(ui, &map) != 0) {
		fail(r, "the score's colours: %s", ui->error);
	} else {
		kp_glue_score_data(ui, g, 0, "", 0, 0, results, local, &have);
		kp_oracle_score_data(score, &want);
		score_compare(r, &have, &want, (int)kp_json_field_int(score, "glue_state", 0),
			(int)kp_json_field_int(score, "local_result", -1));
		r->scored = !r->failed;
	}
	ui->game = NULL;
	kp_ui_free(ui);
	kp_tileset_free(&tileset);
}

/* One step of the screens at the top frame's next record; 0 when the replay ends */
static int step(Replay *r, const Job *job)
{
	const KpJson *c = peek(r);
	const char *fn;
	if (!c || r->failed || (r->phase == PHASE_STARTED && !job->play)) return 0;
	fn = kp_oracle_net_fn(c);
	if (strcmp(fn, "SNetGetProviderCaps") == 0 && !r->inited) {
		KpNetCalls calls;
		KpNetGameHooks hooks;
		KpNetGameConfig config;
		KpNetGameHeader h;
		memset(&calls, 0, sizeof calls);
		calls.create_game = s_create;
		calls.join_game = s_join;
		calls.leave_game = s_leave;
		calls.enum_games = s_enum;
		calls.send_turn = s_send_turn;
		calls.receive_turns = s_receive_turns;
		calls.turns_in_transit = s_in_transit;
		calls.send_message = s_send_message;
		calls.receive_message = s_receive_message;
		calls.game_info = s_game_info;
		calls.set_game_mode = s_set_mode;
		calls.player_name = s_player_name;
		calls.provider_caps = s_provider_caps;
		calls.drop_player = s_drop;
		calls.disconnect_all = s_disconnect;
		calls.error = s_error;
		calls.ud = r;
		memset(&hooks, 0, sizeof hooks);
		hooks.now = clock_now;
		hooks.time = h_time;
		hooks.text = h_text;
		hooks.leave = h_leave;
		hooks.error = h_error;
		hooks.find_save = h_find_save;
		hooks.ud = r;
		memset(&config, 0, sizeof config);
		snprintf(config.player_name, sizeof config.player_name, "%s", kp_json_field_str(r->player, "name", ""));
		config.key_hash = kp_oracle_net_header(job->host, &h) ? h.key_hash : 0;
		config.templates = &job->data->templates;
		config.transfer.read = io_read;
		config.transfer.find = io_find;
		config.transfer.save = io_save;
		config.transfer.ud = r;
		if (kp_netgame_init(&r->ng, &calls, &hooks, &config) != 0) return fail(r, "kp_netgame_init failed");
		r->inited = 1;
	} else if (!r->inited) {
		return fail(r, "the original's %s before its provider's caps", fn);
	} else if (strcmp(fn, "SNetCreateLadderGame") == 0 && r->phase == PHASE_MENU) {
		host_create(r, job, c);
	} else if (strcmp(fn, "SNetJoinGame") == 0 && r->phase == PHASE_MENU) {
		join(r, job, c);
	} else if ((strcmp(fn, "SNetReceiveTurns") == 0 || strcmp(fn, "SNetReceiveMessage") == 0) &&
		r->phase == PHASE_ROOM) {
		room_poll(r);
	} else if (strcmp(fn, "SNetReceiveTurns") == 0 && r->phase == PHASE_READY_ROOM) {
		if (poll_sends(r, KP_LOBBY_READY)) kp_netgame_briefing_start(&r->ng, 0);
		preset(r, POLL_READS); /* PollBriefingTurn 0x476b20, then ReceiveTurns 0x475ac0 */
		if (kp_netgame_briefing_poll(&r->ng)) r->phase = PHASE_STARTED;
	} else if (strcmp(fn, "SNetSendMessage") == 0 && r->phase == PHASE_ROOM) {
		/* a chat line, or a reject's 'N' (a slot action that boots the slot's player) */
		uint8_t first = 0;
		if (kp_hex_decode(kp_json_field_str(c, "given", ""), &first, 1) != 0) first = 0;
		if (first == KP_CHAT_TEXT || first == KP_CHAT_WHISPER) {
			const KpJson *a = next_action(r, "chat");
			if (!a) return fail(r, "the original's chat message has no chat action left");
			kp_netgame_chat(&r->ng, kp_json_field_str(a, "value", ""));
		} else if (first == KP_CHAT_LEAVE) {
			slot_action(r);
		} else {
			return fail(r, "the original's message %.40s outside a poll", kp_json_field_str(c, "given", ""));
		}
	} else if (strcmp(fn, "SNetGetGameInfo") == 0 && r->phase == PHASE_ROOM) {
		if (!kp_netgame_start(&r->ng)) return fail(r, "Start refused");
	} else if (strcmp(fn, "SNetGetProviderCaps") == 0 && r->phase == PHASE_STARTED) {
		if (kp_netgame_game_caps(&r->ng) != 0) return fail(r, "the game's caps");
	} else if (strcmp(fn, "SNetGetTurnsInTransit") == 0 && r->phase == PHASE_STARTED) {
		game_start(r, job);
	} else if (strcmp(fn, "SNetReceiveTurns") == 0 && r->phase == PHASE_GAME_FIRST) {
		game_first(r);
	} else if (strcmp(fn, "SNetReceiveMessage") == 0 && (r->phase == PHASE_GAME_FIRST || r->phase == PHASE_GAME)) {
		kp_session_net_messages(r->session);
		if (r->session->failed) return fail(r, "the game's messages: %s", r->session->error);
	} else if (strcmp(fn, "SNetReceiveTurns") == 0 && r->phase == PHASE_GAME) {
		game_poll(r);
	} else if (strcmp(fn, "SNetSendMessage") == 0 && r->phase == PHASE_GAME) {
		game_chat(r);
	} else if (strcmp(fn, "SNetDropPlayer") == 0 && r->phase == PHASE_GAME) {
		/* net.py's auto_drop: Drop Players pressed once the countdown ended */
		if (!kp_netgame_timeout_drop(&r->ng))
			return fail(r, "Drop Players is not enabled (%s, %u seconds left)", r->ng.lagging ? "waiting" : "no wait",
				r->ng.timeout_seconds);
	} else if (strcmp(fn, "SNetLeaveGame") == 0 && r->phase == PHASE_GAME) {
		score_check(r, job); /* the game menu's quit: RequestGameExit 0x444e00's results, then EndGame 0x443db0 */
		kp_netgame_game_end(&r->ng);
		r->phase = PHASE_LEFT;
	} else if (strcmp(fn, "SNetLeaveGame") == 0 && (r->phase == PHASE_ROOM || r->phase == PHASE_READY_ROOM)) {
		/* the room closed (the player left, or a leave reason's popup was dismissed) */
		kp_netgame_abort(&r->ng);
		r->ng.in_chat_room = 0;
		kp_netgame_release(&r->ng);
		r->phase = PHASE_LEFT;
	} else if (r->phase == PHASE_LEFT) {
		return 0; /* back in the menus */
	} else {
		return fail(r, "the original's %s (phase %d, lobby state %d): no step of the replay makes it", fn, r->phase,
			r->ng.lobby_state);
	}
	return !r->failed;
}

static void replay_free(void *p)
{
	Replay *r = (Replay *)p;
	if (r->session) {
		kp_session_free(r->session);
		free(r->session);
	}
	if (r->map_open) kp_data_map_close(&r->game_map);
	if (r->inited) kp_netgame_free(&r->ng);
	free(r);
}

static void replay_player(KpT *T, void *ud)
{
	const Job *job = (const Job *)ud;
	Replay *r = (Replay *)calloc(1, sizeof *r);
	char who[96];
	const KpJson *next, *score;
	kp_t_ok(T, r != NULL, "out of memory");
	kp_t_defer(T, replay_free, r);
	snprintf(who, sizeof who, "%s/%s", job->game, kp_json_field_str(job->player, "name", "?"));
	r->T = T;
	r->who = kp_t_strdup(T, who);
	r->player = job->player;
	r->actions = kp_json_get(job->player, "actions");
	r->preload = kp_json_get(job->player, "preload");
	r->expansion = kp_json_truthy(kp_json_get(job->golden, "expansion"));
	r->frames[0].items = kp_json_get(job->player, "calls");
	r->frames[0].len = kp_json_len(r->frames[0].items);
	r->leave = -1;
	r->seed = job->seed;
	r->map = job->map_bytes;
	r->map_size = job->map_size;
	r->map_crc = job->map_bytes ? kp_transfer_crc(job->map_bytes, job->map_size) : 0;
	snprintf(r->map_name, sizeof r->map_name, "%s", job->map_name);
	snprintf(r->map_path, sizeof r->map_path, VIRTUAL_MAPS "%s", job->map_name);
	r->has_map = kp_json_truthy(kp_json_get(job->player, "has_map"));
	r->tbl = job->tbl;
	r->tbl_size = job->tbl_size;
	for (;;) {
		int at = r->frames[0].next;
		if (!step(r, job)) break;
		if (r->frames[0].next == at) { /* the original's next call was not made: the step polled too early */
			fail(r, "the original's %s, which the replay's step did not make (phase %d, lobby state %d)",
				kp_oracle_net_fn(peek(r)), r->phase, r->ng.lobby_state);
			break;
		}
	}
	next = peek(r);
	if (!r->failed && r->phase == PHASE_STARTED && !job->play && next &&
		(kp_oracle_net_is(next, "SNetReceiveMessage") || (kp_oracle_net_is(next, "SNetReceiveTurns") && kp_netgame_ums(&r->ng))))
		fail(r, "the lobby started a poll earlier than the original's (its next %s)", kp_oracle_net_fn(next));
	if (!r->failed && !r->has_map && r->phase == PHASE_STARTED && !r->downloaded)
		fail(r, "the player without the map started without downloading it");
	kp_t_log(T, "%s: %ld calls, phase %d, lobby state %d, seed %#x%s", r->who, r->calls, r->phase, r->ng.lobby_state,
		r->ng.seed, r->downloaded ? ", the map downloaded" : "");
	if (r->session && r->session->g)
		kp_t_log(T, "%s: the game at frame %lld, turn %lld, %ld turns with local commands", r->who,
			(long long)kp_scalar(r->session->g, KP_FRAME), (long long)kp_scalar(r->session->g, KP_TURN),
			r->commands_queued);
	score = kp_json_get(r->player, "score");
	if (!r->failed && score && kp_json_is(score, KP_JSON_OBJECT) && !r->scored)
		fail(r, "the original's score screen, where the replay never left the game");
	if (r->scored) kp_t_log(T, "%s: the score screen's data equal the original's", r->who);
	kp_t_ok(T, !r->failed, "%s", r->why);
}

/* The offered file's name in the host's first offer ('O' carrying a transfer offer: type 1, id, size, CRC, name) */
static int offered_name(const KpJson *calls, char *out, size_t size)
{
	int k;
	for (k = 0; k < kp_json_len(calls); k++) {
		const KpJson *c = kp_json_at(calls, k);
		uint8_t m[64];
		long n;
		if (!kp_oracle_net_is(c, "SNetSendMessage")) continue;
		n = kp_oracle_net_unhex(kp_json_field_str(c, "given", ""), m, sizeof m - 1);
		if (n > 3 + 10 && m[0] == KP_CHAT_TRANSFER && m[3] == KP_TRANSFER_OFFER) {
			m[n] = 0;
			snprintf(out, size, "%s", (const char *)m + 3 + 10);
			return 1;
		}
	}
	return 0;
}

static void replay_game(KpT *T, const KpData *data, const char *name, int *players, int *failed, char *first,
	size_t first_size)
{
	char path[256], why[KP_ERROR_SIZE], msg[512];
	const KpJson *golden, *list;
	Job job;
	KpDataMap map;
	KpError e;
	KpNetGameHeader h;
	uint8_t *chk;
	size_t chk_size;
	int k, host = -1;
	snprintf(path, sizeof path, GOLDEN_DIR "%s.json", name);
	golden = kp_t_golden(T, path);
	kp_t_ok(T, kp_json_field_int(golden, "version", 0) == GOLDEN_VERSION,
		"%s: version %d, the replay's %d (tools/net_check.py writes it anew)", path,
		(int)kp_json_field_int(golden, "version", 0), GOLDEN_VERSION);
	memset(&job, 0, sizeof job);
	job.golden = golden;
	job.game = name;
	job.data = data;
	list = kp_json_get(golden, "players");
	for (k = 0; k < kp_json_len(list) && host < 0; k++)
		if ((job.host = first_call(kp_json_get(kp_json_at(list, k), "calls"), "SNetCreateLadderGame")) != NULL) host = k;
	kp_t_ok(T, host >= 0 && kp_oracle_net_header(job.host, &h), "%s: no host's create record", name);
	if (!h.save_id) { /* a resumed game transfers no map: its players have the save */
		job.map_bytes = kp_t_read(T, kp_json_field_str(golden, "map", ""), &job.map_size);
		kp_t_ok(T, kp_data_map_open(&map, &data->files, kp_json_field_str(golden, "map", ""), &e) == 0, "%s: %s", name,
			e.msg);
		chk = kp_data_map_chk(&map, &chk_size, &e);
		kp_data_map_close(&map);
		kp_t_ok(T, chk != NULL, "%s: %s", name, e.msg);
		k = kp_map_header(&job.map, chk, chk_size, data->files.profile,
			kp_json_truthy(kp_json_get(golden, "expansion")), why, sizeof why);
		free(chk);
		kp_t_ok(T, k == 0, "%s: the map's header: %s", name, why);
		kp_t_ok(T, offered_name(kp_json_get(kp_json_at(list, host), "calls"), job.map_name, sizeof job.map_name),
			"%s: the host offered no map", name);
	}
	job.seed = kp_oracle_net_seed(kp_json_get(kp_json_at(list, host), "calls"));
	job.play = !h.save_id;
	job.tbl = kp_files_need(&data->files, NETWORK_TBL, &job.tbl_size, &e);
	kp_t_ok(T, job.tbl != NULL, "%s: %s", NETWORK_TBL, e.msg);
	kp_t_defer(T, free, (void *)job.tbl);
	for (k = 0; k < kp_json_len(list); k++) {
		int mark = kp_t_mark(T);
		job.player = kp_json_at(list, k);
		(*players)++;
		if (kp_t_pcall(T, replay_player, &job, msg, sizeof msg) != 0) {
			kp_t_log(T, "differs: %s", msg);
			if (!(*failed)++) snprintf(first, first_size, "%s", msg);
		}
		kp_t_release(T, mark);
	}
}

static void recordings_replay(KpT *T)
{
	const KpJson *index = kp_t_golden(T, INDEX), *games = kp_json_get(index, "games");
	const KpData *data;
	int g, players = 0, failed = 0;
	char first[512] = "";
	kp_t_ok(T, kp_json_field_int(index, "version", 0) == GOLDEN_VERSION,
		INDEX ": version %d, the replay's %d (tools/net_check.py writes it anew)", (int)kp_json_field_int(index, "version", 0),
		GOLDEN_VERSION);
	data = kp_t_data_117(T);
	for (g = 0; g < kp_json_len(games); g++)
		replay_game(T, data, kp_json_str(kp_json_at(games, g), ""), &players, &failed, first, sizeof first);
	kp_t_ok(T, failed == 0, "%d of %d players' lobbies differ; the first: %s", failed, players, first);
}

static const KpTest TESTS[] = {
	{ "recordings_replay", recordings_replay, 0 },
};

KP_SUITE(kp_suite_lobby_trace, "test_lobby_trace", TESTS);
