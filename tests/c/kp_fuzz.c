/* The fuzz targets' entries (kp_fuzz.h; phase T, stage T2, ADR 0022). */
#include "kp_fuzz.h"
#include "kp_fuzz_game.h"
#include "kp_fuzz_lobby.h"
#include "kp_fuzz_net.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif

#include "kp_ids.h"
#include "kp_kpf.h"
#include "kp_record.h"
#include "kp_runner.h"
#include "kp_session.h"
#include "kp_turns.h"
#include "kp_watch.h"
#include "../../native/kp_core/src/core/kp_chk.h"
#include "../../native/kp_core/src/core/kp_mission.h"

#define ROOT "."
#define PROFILE "bw-1.17.0.1"
#define SEED 0x4b505a46u          /* the games' seed */
#define REP_FRAMES 30000          /* a replay's frames the feed runs at most (an input's header may say 2^32) */
#define COMMAND_CHUNKS 32         /* a commands input's chunks, at most */
#define COMMAND_STEPS 8           /* and the steps they run */
#define SAVE_MAP "scenarios/build_terran"
#define SAVE_STEPS 24             /* the save target's game at its snapshot */
#define SEED_FILE_MAX (1u << 20)  /* a CHK seed's bytes, at most */
#define EDIT_HEADER 5             /* a save edit's {u8 section, u24 offset, u8 n} before its n bytes */
#define EDIT_SEED_MAX (256u << 10) /* a native save seed's edits, at most */
#define SAVE_SEED_STEPS 20        /* the steps between the save target's seed points (KP_FUZZ_SAVE_SEED_POINTS) */
#define SAVE_LOAD_STEPS 8         /* the steps a loaded save's game runs */

/* the commands target's scenarios (the first byte picks one): workers and buildings, a fight, a computer's spells */
static const char *const COMMAND_MAPS[] = { "scenarios/build_terran", "scenarios/bench_combat", "scenarios/ai_queen" };
#define COMMAND_MAP_COUNT ((int)(sizeof COMMAND_MAPS / sizeof COMMAND_MAPS[0]))
#define COMMAND_NETWORK 1         /* the commands' games: a single player's (0), a network game's */
#define COMMAND_KINDS 2

static const char *const NAMES[KP_FUZZ_TARGETS] = { "rep", "chk", "save", "commands", "turns", "udp", "snet", "lobby",
	"transfer", "game", "watch" };

/* the seeds' plain orders: stop and hold position */
static const uint8_t STOP[2] = { 0x1a, 0 }, HOLD[2] = { 0x2b, 0 };

static KpFuzzStats stats;

/* A game of the fuzzing: a session of a scenario started once, its state at its snapshot. */
typedef struct {
	KpSession *s;
	uint8_t *snap;
	size_t snap_size;
} FuzzGame;

static const KpData *data;
static KpSaveIdentity identity;
static FuzzGame save_game, command_games[COMMAND_KINDS][COMMAND_MAP_COUNT];
/* the save target's game's own native save at its snapshot and its sections: what a native input edits */
static uint8_t *save_base;
static size_t save_base_size;
static KpSaveSection save_sections[KP_SAVE_SECTIONS_MAX];
static int save_section_count;

#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 3, 4)))
#endif
static int failed(char *why, size_t size, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(why, size, fmt, ap);
	va_end(ap);
	return -1;
}

const char *kp_fuzz_name(int target) { return target >= 0 && target < KP_FUZZ_TARGETS ? NAMES[target] : "?"; }

int kp_fuzz_target(const char *name)
{
	int k;
	for (k = 0; k < KP_FUZZ_TARGETS; k++) {
		if (strcmp(name, NAMES[k]) == 0) return k;
	}
	return -1;
}

int kp_fuzz_needs_data(int target) { return target == KP_FUZZ_SAVE || target == KP_FUZZ_COMMANDS || target == KP_FUZZ_GAME; }

const KpFuzzStats *kp_fuzz_stats(void) { return &stats; }

static void game_free(FuzzGame *fg)
{
	if (fg->s) kp_session_free(fg->s);
	free(fg->s);
	free(fg->snap);
	memset(fg, 0, sizeof *fg);
}

void kp_fuzz_free(void)
{
	int k, net;
	game_free(&save_game);
	free(save_base);
	save_base = NULL;
	save_base_size = 0;
	save_section_count = 0;
	for (net = 0; net < COMMAND_KINDS; net++) {
		for (k = 0; k < COMMAND_MAP_COUNT; k++) game_free(&command_games[net][k]);
	}
	kp_fuzz_game_free();
	memset(&stats, 0, sizeof stats);
}

static int need_data(char *why, size_t size)
{
	KpError err;
	if (data) return 0;
	if (!(data = kp_oracle_data(ROOT, &err))) return failed(why, size, "the game's data: %s", err.msg);
	kp_save_identity(&identity, &data->files, &data->dat);
	return 0;
}

/* The session of a scenario (recorded or not) after `steps` steps, with its snapshot; with multiplayer, the game's
 * rules a network game's (test_multiplayer's network game: every seat runs every seat's commands by them). */
static int game_start(FuzzGame *fg, const char *key, int record, int steps, int multiplayer, char *why, size_t size)
{
	KpSessionStart st;
	KpMission m;
	KpError err;
	int k;
	if (fg->s) return 0;
	if (need_data(why, size) != 0) return -1;
	if (!(fg->s = (KpSession *)calloc(1, sizeof *fg->s))) return failed(why, size, "out of memory");
	if (!kp_oracle_mission(ROOT, key, &m, &err)) {
		game_free(fg);
		return failed(why, size, "%s: %s", key, err.msg);
	}
	if (kp_session_load_from(fg->s, &data->shared, &data->dat, &m, key, &data->files) != 0) {
		failed(why, size, "%s: %s", key, fg->s->error);
		game_free(fg);
		return -1;
	}
	kp_session_start_defaults(&st);
	st.seed = SEED;
	st.mission_index = kp_oracle_mission_index(ROOT, key);
	st.record = record;
	if (kp_session_start(fg->s, &st) != 0) {
		failed(why, size, "%s: %s", key, fg->s->error);
		game_free(fg);
		return -1;
	}
	for (k = 0; k < steps; k++) {
		if (!kp_session_step(fg->s, NULL)) {
			failed(why, size, "%s: %s", key, fg->s->error);
			game_free(fg);
			return -1;
		}
	}
	if (multiplayer) fg->s->g->setup.multiplayer = 1;
	/* game_reset empties the record: a recorded game's snapshot comes before any command */
	if (fg->s->rec && fg->s->rec->used != 0) {
		game_free(fg);
		return failed(why, size, "%s: commands recorded before the snapshot", key);
	}
	fg->snap_size = kp_snapshot_size(fg->s->g);
	if (!(fg->snap = (uint8_t *)malloc(fg->snap_size)) || kp_snapshot(fg->s->g, fg->snap, fg->snap_size, &err) != 0) {
		game_free(fg);
		return failed(why, size, "%s: no snapshot", key);
	}
	return 0;
}

/* The game back at its snapshot: its state, no failure, no queued commands, no events, a recorded game's record empty
 * as at the snapshot (a load restores the save's, the steps after it record its queued commands). */
static int game_reset(FuzzGame *fg, char *why, size_t size)
{
	KpSession *s = fg->s;
	KpEvent e;
	KpError err;
	if (kp_restore(s->g, fg->snap, fg->snap_size, &err) != 0) return failed(why, size, "the snapshot's restore: %s", err.msg);
	if (s->rec && kp_record_restore(s->rec, NULL, 0, -1) != 0) return failed(why, size, "the record's reset: out of memory");
	s->failed = 0;
	s->error[0] = 0;
	s->rec_lost = 0;
	kp_turns_done(&s->turns);
	while (kp_event_take(s->g, &e)) {
	}
	return 0;
}

/* The save target's game and its own native save, the base of the native inputs' edits. */
static int save_start(char *why, size_t size)
{
	KpSession *s;
	KpError err;
	if (save_base) return 0;
	if (game_start(&save_game, SAVE_MAP, 1, SAVE_STEPS, 0, why, size) != 0) return -1;
	s = save_game.s;
	if (kp_save(s->g, s->host.name, s->host.identity, &save_base, &save_base_size, &err) != 0) {
		return failed(why, size, "%s: %s", SAVE_MAP, err.msg);
	}
	save_section_count = kp_save_sections(s->g, save_base, save_base_size, save_sections, KP_SAVE_SECTIONS_MAX);
	if (save_section_count <= 0 || save_section_count > KP_SAVE_SECTIONS_MAX) {
		free(save_base);
		save_base = NULL;
		return failed(why, size, "%s: its own save's sections not found", SAVE_MAP);
	}
	return 0;
}

int kp_fuzz_init(int target, char *why, size_t why_size)
{
	int k, net;
	switch (target) {
	case KP_FUZZ_REP: return 0;
	case KP_FUZZ_TURNS: return 0;
	case KP_FUZZ_UDP: return 0;
	case KP_FUZZ_SNET: return 0;
	case KP_FUZZ_LOBBY: return 0;
	case KP_FUZZ_TRANSFER: return 0;
	case KP_FUZZ_WATCH: return 0;
	case KP_FUZZ_CHK: (void)need_data(why, why_size); why[0] = 0; return 0; /* with the data when present */
	case KP_FUZZ_SAVE: return save_start(why, why_size);
	case KP_FUZZ_COMMANDS:
		for (net = 0; net < COMMAND_KINDS; net++) {
			for (k = 0; k < COMMAND_MAP_COUNT; k++) {
				if (game_start(&command_games[net][k], COMMAND_MAPS[k], 0, 0, net == COMMAND_NETWORK, why, why_size) != 0)
				return -1;
			}
		}
		return 0;
	case KP_FUZZ_GAME: return need_data(why, why_size) != 0 ? -1 : kp_fuzz_game_init(data, why, why_size);
	default: return failed(why, why_size, "no target %d", target);
	}
}

/* --- rep --- */

static int run_rep(const uint8_t *d, size_t size, char *why, size_t why_size)
{
	KpRep rep, back;
	KpSessionStart st;
	KpError err;
	uint8_t *out = NULL;
	size_t out_size = 0;
	int rc = 0;
	memset(&rep, 0, sizeof rep);
	if (kp_rep_read(&rep, d, size, NULL, &err) != 0) return 0;
	kp_session_start_defaults(&st);
	if (kp_session_replay_start(&st, &rep) == 0) {
		KpTurns *t = (KpTurns *)calloc(1, sizeof *t);
		if (t && kp_turns_replay(t, &rep) == 0) {
			int64_t f, frames = (int64_t)kp_turns_frames(t);
			const KpCommand *list;
			int count;
			kp_turns_begin(t, 0);
			for (f = 0; f <= frames && f < REP_FRAMES; f++) {
				if (kp_turns_frame(t, f, &list, &count) == KP_TURNS_NO_MEMORY) break;
				kp_turns_done(t);
			}
		}
		if (t) kp_turns_free(t);
		free(t);
	}
	/* a file read is written back, and that reads again */
	if (kp_rep_write(&rep, &out, &out_size, &err) == 0) {
		memset(&back, 0, sizeof back);
		if (kp_rep_read(&back, out, out_size, NULL, &err) != 0) rc = failed(why, why_size, "a replay written back does not read: %s", err.msg);
		kp_rep_free(&back);
		free(out);
	}
	kp_rep_free(&rep);
	return rc;
}

/* --- chk --- */

static int run_chk(const uint8_t *d, size_t size)
{
	const KpDefsProfile *profile = kp_defs_profile(PROFILE);
	KpChk c;
	char e[KP_ERROR_SIZE];
	(void)kp_chk_read(&c, d, size, profile, 0, e, sizeof e);
	(void)kp_chk_read(&c, d, size, profile, 1, e, sizeof e);
	if (kp_chk_load(&c, d, size, profile, 1, KP_CHK_MELEE, e, sizeof e) == 0) kp_chk_free(&c);
	if (kp_chk_load(&c, d, size, profile, 1, KP_CHK_UMS, e, sizeof e) == 0) kp_chk_free(&c);
	if (data) {
		KpMission m;
		KpError err;
		uint8_t *copy = (uint8_t *)malloc(size ? size : 1);
		if (copy && size) memcpy(copy, d, size);
		if (kp_mission_from_chk(&m, &data->files, copy, size, "fuzz", NULL, NULL, 0, &err) == 0) {
			(void)kp_mission_check_ai(&m, &data->shared.data.ai, &err);
		}
		kp_mission_free(&m);
		{
			static const uint8_t NO_PACKS[1] = { 0 };
			static const KpChkLoad LOADS[] = { KP_CHK_MELEE, KP_CHK_UMS };
			int k;
			for (k = 0; k < 2; k++) {
				copy = (uint8_t *)malloc(size ? size : 1);
				if (copy && size) memcpy(copy, d, size);
				if (kp_mission_from_map(&m, &data->files, copy, size, "fuzz", LOADS[k], NO_PACKS, 1, &err) == 0)
					kp_mission_free(&m);
			}
		}
	}
	return 0;
}

/* --- save --- */

/* The CRC-32s of a save file's sections made right (a container that parses), so the input reaches what follows. */
static void reseal_kpf(uint8_t *d, size_t size)
{
	KpKpf c;
	KpError err;
	int k;
	if (kp_kpf_parse(&c, d, size, "fuzz", 0, &err) != 0) return;
	for (k = 0; k < c.section_count; k++) {
		uint8_t *e = d + KP_KPF_HEADER_SIZE + (size_t)KP_KPF_ENTRY_SIZE * (size_t)k;
		kp_le_put32(e + 20, kp_crc32(c.sections[k].data, c.sections[k].size));
	}
	kp_kpf_free(&c);
}

/* The base save with an input's edits: {u8 section (modulo their count), u24 offset (modulo its size), u8 n, n bytes}
 * written over the section's bytes, cut at its end; a native save is 8 MB, past libFuzzer's inputs, and edits by
 * section reach a small section's counts as often as a large one's arrays. NULL out of memory. */
static uint8_t *edit_save(const uint8_t *d, size_t size)
{
	uint8_t *b = (uint8_t *)malloc(save_base_size);
	size_t at = 0;
	if (!b) return NULL;
	memcpy(b, save_base, save_base_size);
	while (at + EDIT_HEADER <= size) {
		const KpSaveSection *sec = &save_sections[d[at] % save_section_count];
		size_t off = (size_t)d[at + 1] | (size_t)d[at + 2] << 8 | (size_t)d[at + 3] << 16, n = d[at + 4];
		at += EDIT_HEADER;
		if (n > size - at) n = size - at;
		if (sec->size > 0) {
			size_t m = n;
			off %= sec->size;
			if (m > sec->size - off) m = sec->size - off;
			memcpy(b + sec->start + off, d + at, m);
		}
		at += n;
	}
	return b;
}

static void note_refusal(const char *msg);

static int run_save(const uint8_t *d, size_t size, char *why, size_t why_size)
{
	KpSession *s = save_game.s;
	KpError err;
	uint8_t *copy;
	size_t n;
	int loaded = 0, k;
	if (!s || size == 0) return 0;
	n = size - 1;
	if (!(copy = (uint8_t *)malloc(n ? n : 1))) return 0;
	if (n) memcpy(copy, d + 1, n);
	if ((d[0] & 1) == 0) {
		reseal_kpf(copy, n);
		if (kp_save_check(copy, n, &identity, &err) == KP_SAVE_LOADABLE
			&& kp_save_load(s, copy, n, &identity, NULL, NULL, &err) == 0) {
			loaded = 1;
		}
	} else {
		uint8_t *edited, *sealed;
		size_t sealed_size;
		if ((edited = edit_save(copy, n))) {
			if (kp_save_seal(s->g, edited, save_base_size, &sealed, &sealed_size) == 0) {
				loaded = kp_load(s->g, sealed, sealed_size, s->host.name, s->host.identity, &err) == 0;
				free(sealed);
			}
			free(edited);
		}
	}
	free(copy);
	if (!loaded) return 0;
	stats.loaded++;
	/* kp_load checks the indexes and the links it reads, not every value's range, so a state the walk accepts may hold
	 * values the simulation never makes: the loaded game runs a few frames (with the save's queued commands), a stop
	 * by the game's own checks counted as a refusal, a crash a finding */
	for (k = 0; k < SAVE_LOAD_STEPS; k++) {
		if (!kp_session_step(s, NULL)) {
			if (s->failed) note_refusal(s->error);
			break;
		}
	}
	return game_reset(&save_game, why, why_size);
}

/* --- commands --- */

typedef struct {
	KpSession *s;
	int64_t player;
} Submit;

static void submit_command(void *ud, const uint8_t *cmd, size_t n)
{
	const Submit *sb = (const Submit *)ud;
	(void)kp_session_command(sb->s, cmd, n, sb->player);
}

static int is_hex(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }

/* A refusal counted under its reason: its numbers as '#' but a byte's 0xNN (a command's id); past KP_FUZZ_REASONS
 * reasons, as others. */
static void note_refusal(const char *msg)
{
	char key[KP_FUZZ_REASON_SIZE];
	size_t k = 0, i = 0;
	int r;
	while (msg[i] && k + 1 < sizeof key) {
		if (msg[i] == '0' && msg[i + 1] == 'x') {
			size_t n = 0;
			while (is_hex(msg[i + 2 + n])) n++;
			if (n <= 2 && k + 2 + n < sizeof key) {
				memcpy(key + k, msg + i, 2 + n);
				k += 2 + n;
				i += 2 + n;
				continue;
			}
			key[k++] = '#';
			i += 2 + n;
			continue;
		}
		if (msg[i] >= '0' && msg[i] <= '9') {
			key[k++] = '#';
			while (msg[i] >= '0' && msg[i] <= '9') i++;
			continue;
		}
		key[k++] = msg[i++];
	}
	key[k] = 0;
	stats.refused++;
	for (r = 0; r < stats.reasons && strcmp(stats.reason[r], key) != 0; r++) {
	}
	if (r == KP_FUZZ_REASONS) {
		stats.reason_others++;
		return;
	}
	if (r == stats.reasons) memcpy(stats.reason[stats.reasons++], key, sizeof key);
	stats.reason_count[r]++;
}

/* The input's commands run on one game (a single player's or a network game's, kind), back at its snapshot after. */
static int play_commands(int kind, const uint8_t *d, size_t size, char *why, size_t why_size)
{
	FuzzGame *fg = &command_games[kind][d[0] % COMMAND_MAP_COUNT];
	Submit sb;
	size_t at = 1;
	int chunks = 0, steps = 0, stopped = 0;
	if (!fg->s) return 0;
	sb.s = fg->s;
	while (at + 2 <= size && chunks < COMMAND_CHUNKS && !stopped) {
		uint8_t ctl = d[at];
		size_t n = d[at + 1];
		int run = (ctl >> 3) & 3;
		at += 2;
		if (n > size - at) n = size - at;
		sb.player = ctl & 7;
		(void)kp_record_split(d + at, n, submit_command, &sb);
		at += n;
		chunks++;
		while (run-- > 0 && steps < COMMAND_STEPS && !stopped) {
			stopped = !kp_session_step(fg->s, NULL);
			steps++;
		}
	}
	if (!stopped) stopped = !kp_session_step(fg->s, NULL);
	if (stopped && fg->s->failed && kind == COMMAND_NETWORK) {
		char error[sizeof fg->s->error];
		memcpy(error, fg->s->error, sizeof error);
		(void)game_reset(fg, why, why_size);
		return failed(why, why_size, "a network game refused a seat's commands: %s", error);
	}
	if (stopped && fg->s->failed) note_refusal(fg->s->error);
	return game_reset(fg, why, why_size);
}

/* Each input in both games. A single player's refusal is the game's own check: counted. A network game refuses no
 * seat's commands: every seat runs every seat's, so a refusal is one seat (a modified one) stopping the game for all
 * (ADR 0026's review addition), a finding. */
static int run_commands(const uint8_t *d, size_t size, char *why, size_t why_size)
{
	int kind;
	if (size == 0) return 0;
	for (kind = 0; kind < COMMAND_KINDS; kind++) {
		if (play_commands(kind, d, size, why, why_size) != 0) return -1;
	}
	return 0;
}

/* --- turns --- */

#define TURN_WINDOW 64 /* the turns whose lists are compared: past the most two seats can be apart (2 * 14) */

typedef struct {
	int64_t turn[TURN_WINDOW];
	uint64_t digest[TURN_WINDOW];
} TurnDigests;

static uint64_t list_digest(const KpCommand *list, int count)
{
	uint64_t h = 0xcbf29ce484222325ULL;
	int k;
	size_t j;
	for (k = 0; k < count; k++) {
		h = (h ^ (uint64_t)list[k].player) * 0x100000001b3ULL;
		for (j = 0; j < list[k].size; j++) h = (h ^ list[k].bytes[j]) * 0x100000001b3ULL;
		h = (h ^ 0xff) * 0x100000001b3ULL;
	}
	return h;
}

/* A turn the seat ran: the same list as every other seat's turn of that number, by the Storm IDs from 7 down. */
static int turn_ran(KpTurnHub *h, TurnDigests *td, int seat, const KpCommand *list, int count, char *why, size_t why_size)
{
	int64_t turn = h->received[seat] - 1;
	uint64_t d = list_digest(list, count);
	int k, at = (int)(turn % TURN_WINDOW);
	int32_t last = KP_HUB_SEATS;
	for (k = 0; k < count; k++) {
		int32_t storm = -1;
		int j;
		for (j = 0; j < h->seats; j++) {
			if (h->record[j] == list[k].player) storm = h->storm[j];
		}
		if (storm < 0 || storm > last) return failed(why, why_size, "turn %lld: Storm ID %d after %d", (long long)turn, storm, last);
		last = storm;
	}
	if (td->turn[at] == turn) {
		if (td->digest[at] != d) return failed(why, why_size, "turn %lld: seat %d's list differs", (long long)turn, seat);
	} else {
		td->turn[at] = turn;
		td->digest[at] = d;
	}
	return 0;
}

/* The bounds: no seat with more turns in transit than QueueTurnCommand lets it have, no sender past a turn a seat has
 * not received (the stream's ring keeps KP_HUB_RING). */
static int hub_bounds(const KpTurnHub *h, char *why, size_t why_size)
{
	int64_t least = h->received[0];
	int k;
	for (k = 1; k < h->seats; k++) {
		if (h->received[k] < least) least = h->received[k];
	}
	for (k = 0; k < h->seats; k++) {
		if (kp_hub_in_transit(h, k) > KP_HUB_TRANSIT_LIMIT) return failed(why, why_size, "seat %d: %lld turns in transit", k, (long long)kp_hub_in_transit(h, k));
		if (h->sent_count[k] - least > KP_HUB_RING) return failed(why, why_size, "seat %d's stream past the ring", k);
	}
	return 0;
}

/* {u8 seats - 1 | (frames per turn - 1) << 3, u8 the first Storm ID}, then operations {u8 seat | kind << 3}: kind 0 a
 * command {u8 n, n bytes} of the seat, 1 a frame, 2 the first turn, 3 up to 8 frames (bits 5-7 + 1). */
static int run_turns(const uint8_t *d, size_t size, char *why, size_t why_size)
{
	static TurnDigests td;
	KpTurnHub *h;
	size_t at = 2;
	int k, rc = 0;
	if (size < 2) return 0;
	if (!(h = kp_hub_new(1 + (d[0] & 7), 1 + ((d[0] >> 3) & 3)))) return failed(why, why_size, "out of memory");
	for (k = 0; k < h->seats; k++) (void)kp_hub_seat(h, k, k, (int32_t)((d[1] + k) & 7));
	if (kp_hub_begin(h) != 0) rc = failed(why, why_size, "the hub does not start");
	for (k = 0; k < TURN_WINDOW; k++) td.turn[k] = -1;
	while (rc == 0 && at < size) {
		uint8_t op = d[at++];
		int seat = (op & 7) % h->seats, kind = (op >> 3) & 3, n = 1, got;
		const KpCommand *list;
		int count;
		if (kind == 0) {
			size_t len = at < size ? d[at++] : 0;
			if (len > size - at) len = size - at;
			(void)kp_hub_submit(h, seat, d + at, len);
			at += len;
		} else if (kind == 2) {
			if (kp_hub_first_turn(h, seat, &list, &count) == KP_HUB_TURN) rc = turn_ran(h, &td, seat, list, count, why, why_size);
		} else {
			if (kind == 3) n = 1 + (op >> 5);
			for (k = 0; k < n && rc == 0; k++) {
				got = kp_hub_frame(h, seat, &list, &count);
				if (got == KP_HUB_NO_MEMORY) rc = failed(why, why_size, "out of memory");
				else if (got == KP_HUB_TURN) rc = turn_ran(h, &td, seat, list, count, why, why_size);
			}
		}
		if (rc == 0) rc = hub_bounds(h, why, why_size);
	}
	kp_hub_free(h);
	return rc;
}

/* --- watch: a network game's server and viewer (kp_watch.h) --- */

#define WATCH_DATAGRAM (KP_WATCH_DATA_HEADER + KP_WATCH_CHUNK)
#define WATCH_QUEUE 64        /* the datagrams in transit each way, at most (the rest lost) */
#define WATCH_COMMAND_MAX 32  /* a recorded command's bytes, at most */

typedef struct {
	uint8_t bytes[WATCH_DATAGRAM];
	size_t size;
} WatchDatagram;

typedef struct {
	WatchDatagram up[WATCH_QUEUE], down[WATCH_QUEUE]; /* the viewer's requests, the server's answers */
	int ups, downs;
	size_t request_size; /* the request the server answers now */
	int bad;
	char why[KP_ERROR_SIZE];
} WatchLink;

static void watch_bad(WatchLink *l, const char *msg, size_t size)
{
	if (!l->bad) snprintf(l->why, sizeof l->why, "watch: %s (%u bytes)", msg, (unsigned)size);
	l->bad = 1;
}

/* A viewer's REQUEST: the magic, the type, the size of the answer it asks for at most. */
static void watch_up(void *ud, const uint8_t *bytes, size_t size)
{
	WatchLink *l = (WatchLink *)ud;
	if (size < KP_WATCH_REQUEST_HEADER || size > WATCH_DATAGRAM || memcmp(bytes, KP_WATCH_MAGIC, KP_WATCH_MAGIC_SIZE) != 0
		|| bytes[KP_WATCH_AT_TYPE] != KP_WATCH_REQUEST) {
		watch_bad(l, "a malformed request", size);
		return;
	}
	if (l->ups == WATCH_QUEUE) return;
	memcpy(l->up[l->ups].bytes, bytes, size);
	l->up[l->ups++].size = size;
}

/* A server's DATA: never larger than its request, its bytes within the part's size and their CRC right (an answer of no
 * bytes past the part's end only tells the size and the frames). */
static void watch_down(void *ud, const uint8_t *bytes, size_t size)
{
	WatchLink *l = (WatchLink *)ud;
	size_t n;
	if (size > l->request_size) {
		watch_bad(l, "an answer larger than its request", size);
		return;
	}
	if (size < KP_WATCH_DATA_HEADER || memcmp(bytes, KP_WATCH_MAGIC, KP_WATCH_MAGIC_SIZE) != 0
		|| bytes[KP_WATCH_AT_TYPE] != KP_WATCH_DATA) {
		watch_bad(l, "a malformed answer", size);
		return;
	}
	n = (size_t)bytes[KP_WATCH_AT_COUNT] | (size_t)bytes[KP_WATCH_AT_COUNT + 1] << 8;
	if (size != KP_WATCH_DATA_HEADER + n || n > KP_WATCH_CHUNK
		|| (n && (uint64_t)kp_le_u32(bytes + KP_WATCH_AT_OFFSET) + n > kp_le_u32(bytes + KP_WATCH_AT_SIZE))
		|| kp_crc32(n ? bytes + KP_WATCH_DATA_HEADER : bytes, n) != kp_le_u32(bytes + KP_WATCH_AT_CRC)) {
		watch_bad(l, "an answer's count, bounds or CRC", size);
		return;
	}
	if (l->downs == WATCH_QUEUE) return;
	memcpy(l->down[l->downs].bytes, bytes, size);
	l->down[l->downs++].size = size;
}

static void watch_serve(WatchLink *l, KpWatchServer *s, const uint8_t *bytes, size_t size)
{
	l->request_size = size;
	kp_watch_server_receive(s, bytes, size, watch_down, l);
}

/* The viewer's and the server's state within their bounds. */
static int watch_bounds(const KpWatchServer *s, const KpRecorder *rec, const KpWatchViewer *v, char *why, size_t why_size)
{
	if (v->start_got > v->start_size || v->start_size > KP_WATCH_START_MAX)
		return failed(why, why_size, "watch: the start file's %u of %u bytes", (unsigned)v->start_got, (unsigned)v->start_size);
	if (v->record_got > v->record_cap || v->record_got > KP_WATCH_RECORD_MAX || v->known[KP_WATCH_PART_RECORD] > KP_WATCH_RECORD_MAX)
		return failed(why, why_size, "watch: the record's %u bytes (room %u, known %u)", (unsigned)v->record_got,
			(unsigned)v->record_cap, (unsigned)v->known[KP_WATCH_PART_RECORD]);
	if (v->ready_size > v->record_got || v->taken > v->ready_size)
		return failed(why, why_size, "watch: ready %u, taken %u of %u", (unsigned)v->ready_size, (unsigned)v->taken,
			(unsigned)v->record_got);
	if (s->record_size > rec->used || s->record_size > s->record_cap)
		return failed(why, why_size, "watch: published %u of %u", (unsigned)s->record_size, (unsigned)rec->used);
	return 0;
}

/* {u8 start size}, the start file's bytes, then operations {u8 kind | arg << 3}: 0 a DATA to the viewer {u8 flags, u32
 * offset, u32 size, u32 frames, u16 n, n bytes} (flags: 1 the record's part, 2 final, 4 its CRC wrong, 8 at what the
 * viewer holds, 16 the size from the offset), 1 a datagram to the viewer and 2 to the server {u16 n, n bytes}, 3 the
 * clock {u16 ms} and the viewer's turn over the link (its requests served, the answers taken when arg's bit 0 is 0),
 * 4 commands recorded {u8 count, each u8 storm, u8 n, n bytes} and a mark arg + 1 frames on, 5 the game's end, 6 the
 * playback's take, 7 a mark arg frames back (a load). */
static int run_watch(const uint8_t *d, size_t size, char *why, size_t why_size)
{
	static WatchLink link;
	static KpRecorder rec;
	KpWatchServer s;
	KpWatchViewer v;
	size_t at = 1, start = size ? d[0] : 0;
	uint32_t now = 0;
	int64_t frame = 0;
	int rc = 0, k;
	if (start > size - at) start = size - at;
	memset(&link, 0, sizeof link);
	if (kp_record_init(&rec) != 0) return failed(why, why_size, "out of memory");
	if (kp_watch_server_init(&s, 7, d + at, start, 4) != 0) {
		kp_record_free(&rec);
		return failed(why, why_size, "out of memory");
	}
	at += start;
	kp_watch_viewer_init(&v, now);
	while (rc == 0 && at < size) {
		uint8_t op = d[at++];
		int arg = op >> 3;
		switch (op & 7) {
		case 0: {
			uint8_t out[WATCH_DATAGRAM];
			uint8_t flags = at < size ? d[at++] : 0;
			int part = (flags & 1) ? KP_WATCH_PART_RECORD : KP_WATCH_PART_START;
			uint32_t offset = at + 4 <= size ? kp_le_u32(d + at) : 0, total = 0, frames = 0;
			size_t n = 0;
			at = at + 4 <= size ? at + 4 : size;
			if (at + 4 <= size) total = kp_le_u32(d + at);
			at = at + 4 <= size ? at + 4 : size;
			if (at + 4 <= size) frames = kp_le_u32(d + at);
			at = at + 4 <= size ? at + 4 : size;
			if (at + 2 <= size) n = kp_le_u16(d + at) % (KP_WATCH_CHUNK + 1);
			at = at + 2 <= size ? at + 2 : size;
			if (n > size - at) n = size - at;
			if (flags & 8) offset = (uint32_t)(part == KP_WATCH_PART_RECORD ? v.record_got : v.start_got);
			if (flags & 16) total = offset + (uint32_t)n + (total & 0xff);
			memcpy(out, KP_WATCH_MAGIC, KP_WATCH_MAGIC_SIZE);
			out[KP_WATCH_AT_TYPE] = KP_WATCH_DATA;
			kp_le_put32(out + KP_WATCH_AT_GAME, s.game);
			out[KP_WATCH_AT_PART] = (uint8_t)part;
			kp_le_put32(out + KP_WATCH_AT_OFFSET, offset);
			kp_le_put32(out + KP_WATCH_AT_SIZE, total);
			kp_le_put32(out + KP_WATCH_AT_FRAMES, frames);
			out[KP_WATCH_AT_FINAL] = (uint8_t)((flags >> 1) & 1);
			kp_le_put16(out + KP_WATCH_AT_COUNT, (uint16_t)n);
			if (n) memcpy(out + KP_WATCH_DATA_HEADER, d + at, n);
			kp_le_put32(out + KP_WATCH_AT_CRC, kp_crc32(n ? out + KP_WATCH_DATA_HEADER : out, n) ^ ((flags & 4) ? 1u : 0u));
			at += n;
			kp_watch_viewer_receive(&v, out, KP_WATCH_DATA_HEADER + n, now);
			break;
		}
		case 1:
		case 2: {
			size_t n = at + 2 <= size ? kp_le_u16(d + at) : 0;
			at = at + 2 <= size ? at + 2 : size;
			if (n > size - at) n = size - at;
			if ((op & 7) == 1) kp_watch_viewer_receive(&v, d + at, n, now);
			else watch_serve(&link, &s, d + at, n);
			at += n;
			break;
		}
		case 3:
			now += at + 2 <= size ? (uint32_t)kp_le_u16(d + at) : 100u;
			at = at + 2 <= size ? at + 2 : size;
			link.ups = 0;
			kp_watch_viewer_poll(&v, now, watch_up, &link);
			link.downs = 0;
			for (k = 0; k < link.ups; k++) watch_serve(&link, &s, link.up[k].bytes, link.up[k].size);
			if (!(arg & 1)) {
				for (k = 0; k < link.downs; k++) kp_watch_viewer_receive(&v, link.down[k].bytes, link.down[k].size, now);
			}
			break;
		case 4: {
			int count = at < size ? d[at++] : 0;
			for (k = 0; k < count && at + 2 <= size; k++) {
				uint8_t storm = d[at] & 7;
				size_t n = 1 + d[at + 1] % WATCH_COMMAND_MAX;
				at += 2;
				if (n > size - at) break;
				kp_record_command(&rec, frame, storm, d + at, n);
				at += n;
			}
			frame += arg + 1;
			if (kp_watch_server_mark(&s, &rec, (uint32_t)frame) != 0) rc = failed(why, why_size, "out of memory");
			break;
		}
		case 5:
			if (kp_watch_server_end(&s, &rec, (uint32_t)frame) != 0) rc = failed(why, why_size, "out of memory");
			break;
		case 6: {
			const uint8_t *bytes;
			size_t n;
			uint32_t frames;
			int whole;
			if (kp_watch_viewer_take(&v, &bytes, &n, &frames, &whole) && n && !bytes)
				rc = failed(why, why_size, "watch: %u bytes taken from no record", (unsigned)n);
			break;
		}
		default:
			if (kp_watch_server_mark(&s, &rec, (uint32_t)(frame > arg ? frame - arg : 0)) != 0)
				rc = failed(why, why_size, "out of memory");
			break;
		}
		if (rc == 0 && link.bad) rc = failed(why, why_size, "%s", link.why);
		if (rc == 0) rc = watch_bounds(&s, &rec, &v, why, why_size);
	}
	kp_watch_viewer_free(&v);
	kp_watch_server_free(&s);
	kp_record_free(&rec);
	return rc;
}

int kp_fuzz_run(int target, const uint8_t *d, size_t size, char *why, size_t why_size)
{
	why[0] = 0;
	stats.inputs++;
	switch (target) {
	case KP_FUZZ_REP: return run_rep(d, size, why, why_size);
	case KP_FUZZ_CHK: return run_chk(d, size);
	case KP_FUZZ_SAVE: return run_save(d, size, why, why_size);
	case KP_FUZZ_COMMANDS: return run_commands(d, size, why, why_size);
	case KP_FUZZ_TURNS: return run_turns(d, size, why, why_size);
	case KP_FUZZ_UDP: return kp_fuzz_net_udp(d, size, why, why_size);
	case KP_FUZZ_SNET: return kp_fuzz_net_snet(d, size, why, why_size);
	case KP_FUZZ_LOBBY: return kp_fuzz_lobby_run(d, size, why, why_size);
	case KP_FUZZ_TRANSFER: return kp_fuzz_transfer_run(d, size, why, why_size);
	case KP_FUZZ_GAME: return kp_fuzz_game_run(d, size, why, why_size);
	case KP_FUZZ_WATCH: return run_watch(d, size, why, why_size);
	default: return failed(why, why_size, "no target %d", target);
	}
}

/* --- seeds --- */

static int make_dir(const char *path)
{
#ifdef _WIN32
	return _mkdir(path) == 0 || errno == EEXIST ? 0 : -1;
#else
	return mkdir(path, 0755) == 0 || errno == EEXIST ? 0 : -1;
#endif
}

typedef struct {
	const char *dir;
	int count, failed;
	char why[KP_ERROR_SIZE];
} Seeds;

/* A seed of target into dir/<target>/<name>: an optional first byte, then the bytes. */
static void seed(Seeds *sd, int target, const char *name, int first, const uint8_t *b, size_t n)
{
	char path[1024];
	FILE *f;
	snprintf(path, sizeof path, "%s/%s", sd->dir, NAMES[target]);
	if (make_dir(path) != 0) {
		snprintf(sd->why, sizeof sd->why, "%s: cannot be made", path);
		sd->failed = 1;
		return;
	}
	snprintf(path, sizeof path, "%s/%s/%s", sd->dir, NAMES[target], name);
	if (!(f = fopen(path, "wb"))) {
		snprintf(sd->why, sizeof sd->why, "%s: cannot be written", path);
		sd->failed = 1;
		return;
	}
	if (first >= 0) fputc(first, f);
	if (n && fwrite(b, 1, n, f) != n) sd->failed = 1;
	if (fclose(f) != 0) sd->failed = 1;
	if (sd->failed && !sd->why[0]) snprintf(sd->why, sizeof sd->why, "%s: a short write", path);
	sd->count++;
}

/* A command chunk {player | steps << 3, n, bytes} appended to buf. */
static size_t chunk(uint8_t *buf, size_t at, int player, int steps, const uint8_t *cmd, size_t n)
{
	buf[at] = (uint8_t)(player | steps << 3);
	buf[at + 1] = (uint8_t)n;
	memcpy(buf + at + 2, cmd, n);
	return at + 2 + n;
}

/* A made-up scenario's CHK: the sections every version requires, of the sizes the loader wants, a 64x64 map of
 * Brood War's version, no units. Its VCOD is zeros, not the executable's resource (Blizzard's bytes stay out of the
 * repository), so kp_chk_read refuses it at that section's digest: a seed of the chunks' walk and the tables' rules;
 * the data's CHKs (kp_fuzz_write_seeds with the data) go further. The caller frees it. */
static uint8_t *synthetic_chk(size_t *size)
{
	static const struct {
		const char *tag;
		uint32_t size;
	} SECTIONS[] = { { "VER ", 2 }, { "DIM ", 4 }, { "ERA ", 2 }, { "OWNR", 12 }, { "SIDE", 12 }, { "STR ", 6 },
		{ "SPRP", 4 }, { "FORC", 20 }, { "VCOD", 1040 } };
	uint8_t *b;
	size_t at = 0, total = 0;
	int k;
	for (k = 0; k < (int)(sizeof SECTIONS / sizeof SECTIONS[0]); k++) total += 8 + SECTIONS[k].size;
	if (!(b = (uint8_t *)calloc(1, total))) return NULL;
	for (k = 0; k < (int)(sizeof SECTIONS / sizeof SECTIONS[0]); k++) {
		uint8_t *p = b + at + 8;
		memcpy(b + at, SECTIONS[k].tag, 4);
		kp_le_put32(b + at + 4, SECTIONS[k].size);
		switch (k) {
		case 0: kp_le_put16(p, 205); break; /* Brood War */
		case 1: kp_le_put16(p, 64); kp_le_put16(p + 2, 64); break;
		case 3: memset(p, 5, 8); memset(p + 8, 7, 4); break; /* eight computers' slots, then neutral */
		case 4: memset(p, 4, 8); memset(p + 8, 7, 4); break; /* their races (user's choice), then neutral */
		case 5: kp_le_put16(p, 1); kp_le_put16(p + 2, 4); break; /* one string, empty */
		default: break;
		}
		at += 8 + SECTIONS[k].size;
	}
	*size = total;
	return b;
}

static void net_seed(void *ud, int snet, const char *name, const uint8_t *b, size_t n)
{
	seed((Seeds *)ud, snet ? KP_FUZZ_SNET : KP_FUZZ_UDP, name, -1, b, n);
}

static void game_seed(void *ud, const char *name, const uint8_t *b, size_t n)
{
	seed((Seeds *)ud, KP_FUZZ_GAME, name, -1, b, n);
}

static void lobby_seed(void *ud, int lobby, const char *name, const uint8_t *b, size_t n)
{
	seed((Seeds *)ud, lobby ? KP_FUZZ_LOBBY : KP_FUZZ_TRANSFER, name, -1, b, n);
}

/* The synthetic seeds: a replay of a made-up header, record and map bytes (no data's bytes in it), plain orders of
 * the commands target (stop, hold, the speed, a cheat, a pause and resume), the network targets' sessions. */
static void synthetic_seeds(Seeds *sd)
{
	static const uint8_t SPEED[2] = { 0x0f, 3 };
	static const uint8_t SELECT_NONE[2] = { 0x09, 0 }, PAUSE[1] = { 0x10 }, RESUME[1] = { 0x11 };
	static const uint8_t CHEAT[5] = { 0x12, 0x01, 0, 0, 0 }, RIGHT_CLICK[10] = { 0x14, 0x40, 0, 0x40, 0, 0, 0, 0xe4, 0, 0 };
	uint8_t buf[256], *map;
	size_t at, map_size;
	int k;
	if (!(map = synthetic_chk(&map_size))) {
		snprintf(sd->why, sizeof sd->why, "out of memory");
		sd->failed = 1;
		return;
	}
	seed(sd, KP_FUZZ_CHK, "synthetic.chk", -1, map, map_size);
	{ /* three seats, 2 frames a turn: hotkeys and a latency, the first turns, frames of each */
		static const uint8_t TURNS[] = { 0x0a, 5, 0x00, 3, 0x13, 1, 0, 0x01, 2, 0x55, 2, 0x02, 3, 0x13, 2, 0, 0x10, 0x11, 0x12,
			0xf8, 0xf9, 0xfa, 0x00, 3, 0x13, 3, 0, 0xf8, 0xf9, 0xfa, 0xf8, 0xf9, 0xfa };
		seed(sd, KP_FUZZ_TURNS, "synthetic.turns", -1, TURNS, sizeof TURNS);
	}
	(void)kp_fuzz_game_seeds(game_seed, sd);
	{ /* a viewer that fetches the start file and a record of three marks over the link, a forged empty answer at
	   * what it holds, the game's end; and the same with the answers lost after the start */
		static const uint8_t WATCH_HEAD[] = { 8, 'K', 'P', 'R', 'E', 'P', 'L', 'A', 'Y',
			4, 2, 0, 1, 0x1a, 0, 1, 1, 0x2b, 0, /* two commands, a mark one frame on */
			4 | 3 << 3, 1, 2, 1, 0x2b, 0, /* one more, four frames on */
			4, 0 /* a mark with no command */ };
		static const uint8_t TICK[3] = { 3, 100, 0 }, LOST_TICK[3] = { 3 | 1 << 3, 100, 0 };
		static const uint8_t WATCH_TAIL[] = { 6, 0, 1 | 8 | 16, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5, 3, 100, 0, 3,
			100, 0, 6 };
		uint8_t watch[sizeof WATCH_HEAD + 16 * sizeof TICK + sizeof WATCH_TAIL];
		size_t w;
		int lost;
		for (lost = 0; lost < 2; lost++) {
			memcpy(watch, WATCH_HEAD, sizeof WATCH_HEAD);
			w = sizeof WATCH_HEAD;
			for (k = 0; k < 16; k++, w += sizeof TICK) memcpy(watch + w, lost && k >= 4 ? LOST_TICK : TICK, sizeof TICK);
			memcpy(watch + w, WATCH_TAIL, sizeof WATCH_TAIL);
			w += sizeof WATCH_TAIL;
			seed(sd, KP_FUZZ_WATCH, lost ? "synthetic-lossy.watch" : "synthetic.watch", -1, watch, w);
		}
	}
	if (kp_fuzz_net_seeds(net_seed, sd, sd->why, sizeof sd->why) < 0 ||
		kp_fuzz_lobby_seeds(lobby_seed, sd, sd->why, sizeof sd->why) < 0) {
		sd->failed = 1;
		return;
	}
	{
		KpRepGame game;
		KpRep rep;
		KpRecorder rec;
		KpError err;
		uint8_t *file;
		size_t size;
		memset(&game, 0, sizeof game);
		game.campaign = 1;
		game.seed = SEED;
		game.frames = 40;
		game.player_name = "Anonymous";
		game.map_w = game.map_h = 64;
		game.speed = 6;
		game.map_name = "fuzz";
		for (k = 0; k < 8; k++) {
			game.players[k].slot = (uint32_t)k;
			game.players[k].storm = k == 0 ? 0 : -1;
			game.players[k].type = (uint8_t)(k == 0 ? KP_CTRL_HUMAN : 1);
		}
		memset(&rep, 0, sizeof rep);
		kp_rep_header(&rep, &game);
		if (kp_record_init(&rec) == 0) {
			kp_record_command(&rec, 2, 0, SELECT_NONE, sizeof SELECT_NONE);
			kp_record_command(&rec, 2, 0, RIGHT_CLICK, sizeof RIGHT_CLICK);
			kp_record_command(&rec, 10, 0, STOP, sizeof STOP);
			kp_record_command(&rec, 10, 0, HOLD, sizeof HOLD);
			kp_record_command(&rec, 30, 0, STOP, sizeof STOP);
			rep.record = (uint8_t *)kp_record_bytes(&rec, &rep.record_size);
			rep.chk = map;
			rep.chk_size = map_size;
			if (kp_rep_write(&rep, &file, &size, &err) == 0) {
				seed(sd, KP_FUZZ_REP, "synthetic.rep", -1, file, size);
				free(file);
			}
			kp_record_free(&rec);
		}
	}
	for (k = 0; k < COMMAND_MAP_COUNT; k++) {
		char name[32];
		at = chunk(buf, 0, 0, 1, SELECT_NONE, sizeof SELECT_NONE);
		at = chunk(buf, at, 0, 1, RIGHT_CLICK, sizeof RIGHT_CLICK);
		at = chunk(buf, at, 1, 1, STOP, sizeof STOP);
		at = chunk(buf, at, 2, 1, HOLD, sizeof HOLD);
		at = chunk(buf, at, 0, 0, SPEED, sizeof SPEED);
		at = chunk(buf, at, 0, 1, CHEAT, sizeof CHEAT);
		at = chunk(buf, at, 0, 1, PAUSE, sizeof PAUSE);
		at = chunk(buf, at, 0, 1, RESUME, sizeof RESUME);
		snprintf(name, sizeof name, "orders-%d", k);
		seed(sd, KP_FUZZ_COMMANDS, name, k, buf, at);
	}
	free(map);
}

/* Commands on a game's own units: player p's first units selected (their refs), then a right click, a stop and a
 * hold; one seed a player that has units. And the first such player's units given a targeted order past orders.dat
 * (0xff): the original reads its tables past their end (ExecTargetedOrder 0x472270, tech 0x51b800[order]); Koprulu's
 * DAT read gives KP_NIL and the requirements' range check stops a single player's game, a refusal, while the input's
 * network game ignores the order (test_fuzz.data_seeds_run). */
/* The select command 0x09 of player p's first units (their refs): its count of them (0: none, no command). */
static int select_units(KpGame *g, int64_t p, uint8_t select[2 + 2 * KP_SLOTS])
{
	int64_t n;
	int count = 0;
	for (n = g->lists[KP_LIST_ACTIVE].head; n != 0 && count < KP_SLOTS; n = g->u.next[n]) {
		if (g->u.owner[n] != p) continue;
		kp_le_put16(select + 2 + 2 * count, (uint16_t)kp_unit_ref(g, n));
		count++;
	}
	select[0] = 0x09;
	select[1] = (uint8_t)count;
	return count;
}

/* The targeted order past orders.dat (0xff) at the first active unit plus (64, 32) */
static void order_past(KpGame *g, uint8_t past[11])
{
	static const uint8_t PAST[11] = { 0x15, 0, 0, 0, 0, 0, 0, 0xe4, 0, 0xff, 0 };
	int64_t n = g->lists[KP_LIST_ACTIVE].head;
	memcpy(past, PAST, sizeof PAST);
	kp_le_put16(past + 1, (uint16_t)(g->u.x[n] + 64));
	kp_le_put16(past + 3, (uint16_t)(g->u.y[n] + 32));
}

static void unit_seeds(Seeds *sd, int map)
{
	KpGame *g = command_games[0][map].s->g;
	uint8_t buf[256], select[2 + 2 * KP_SLOTS], click[10] = { 0x14, 0, 0, 0, 0, 0, 0, 0xe4, 0, 0 }, past[11];
	int64_t p, n;
	int first = 1;
	for (p = 0; p < 8; p++) {
		int count = select_units(g, p, select);
		size_t at;
		char name[48];
		if (count == 0) continue;
		n = g->lists[KP_LIST_ACTIVE].head;
		kp_le_put16(click + 1, (uint16_t)(g->u.x[n] + 64));
		kp_le_put16(click + 3, (uint16_t)(g->u.y[n] + 32));
		at = chunk(buf, 0, (int)p, 1, select, (size_t)(2 + 2 * count));
		at = chunk(buf, at, (int)p, 2, click, sizeof click);
		at = chunk(buf, at, (int)p, 1, STOP, sizeof STOP);
		at = chunk(buf, at, (int)p, 1, HOLD, sizeof HOLD);
		snprintf(name, sizeof name, "units-%d-player-%d", map, (int)p);
		seed(sd, KP_FUZZ_COMMANDS, name, map, buf, at);
		if (first) {
			order_past(g, past);
			at = chunk(buf, 0, (int)p, 1, select, (size_t)(2 + 2 * count));
			at = chunk(buf, at, (int)p, 1, past, sizeof past);
			snprintf(name, sizeof name, "order-past-orders-dat-%d", map);
			seed(sd, KP_FUZZ_COMMANDS, name, map, buf, at);
			first = 0;
		}
	}
}

/* A native save of g as edits of the base save (edit_save's form): the runs of bytes that differ, section by section
 * (a section of another size left as the base's), up to EDIT_SEED_MAX. NULL when its sections are not found or out of
 * memory. */
static uint8_t *save_edits(KpGame *g, const uint8_t *save, size_t size, size_t *out_size)
{
	KpSaveSection secs[KP_SAVE_SECTIONS_MAX];
	uint8_t *e;
	size_t at = 0;
	int k, count = kp_save_sections(g, save, size, secs, KP_SAVE_SECTIONS_MAX);
	if (count != save_section_count || !(e = (uint8_t *)malloc(EDIT_SEED_MAX))) return NULL;
	for (k = 0; k < count; k++) {
		const uint8_t *a = save_base + save_sections[k].start, *b = save + secs[k].start;
		size_t i = 0, n = secs[k].size;
		if (secs[k].tag != save_sections[k].tag || n != save_sections[k].size) continue;
		while (i < n) {
			size_t run = 0;
			if (a[i] == b[i]) {
				i++;
				continue;
			}
			while (i + run < n && run < 255 && a[i + run] != b[i + run]) run++;
			if (at + EDIT_HEADER + run > EDIT_SEED_MAX || i > 0xffffff) break;
			e[at] = (uint8_t)k;
			e[at + 1] = (uint8_t)(i & 0xff);
			e[at + 2] = (uint8_t)(i >> 8 & 0xff);
			e[at + 3] = (uint8_t)(i >> 16 & 0xff);
			e[at + 4] = (uint8_t)run;
			memcpy(e + at + EDIT_HEADER, b + i, run);
			at += EDIT_HEADER + run;
			i += run;
		}
	}
	*out_size = at;
	return e;
}

/* The save target's seeds: its game's save file and native save (as edits of its base) at the snapshot and a few
 * steps on, and a save file whose queue holds a player's units selected and an order past orders.dat (the loaded
 * game's steps run it: a refusal); and the replay of a recorded game with commands. */
static void game_seeds(Seeds *sd)
{
	KpSession *s = save_game.s;
	KpError err;
	uint8_t *out;
	size_t size;
	int k, at;
	char why[KP_ERROR_SIZE];
	for (at = 0; at < KP_FUZZ_SAVE_SEED_POINTS; at++) {
		char name[48];
		if (kp_save_encode(s, &identity, NULL, NULL, &out, &size, &err) == 0) {
			snprintf(name, sizeof name, "file-%d.kpsave", at);
			seed(sd, KP_FUZZ_SAVE, name, 0, out, size);
			free(out);
		}
		if (kp_save(s->g, s->host.name, s->host.identity, &out, &size, &err) == 0) {
			uint8_t *edits;
			size_t edits_size;
			if ((edits = save_edits(s->g, out, size, &edits_size))) {
				snprintf(name, sizeof name, "native-%d", at);
				seed(sd, KP_FUZZ_SAVE, name, 1, edits, edits_size);
				free(edits);
			}
			free(out);
		}
		(void)kp_session_command(s, at == 0 ? STOP : HOLD, 2, -1);
		for (k = 0; k < SAVE_SEED_STEPS; k++) (void)kp_session_step(s, NULL);
	}
	{
		uint8_t select[2 + 2 * KP_SLOTS], past[11];
		int64_t p;
		for (p = 0; p < 8 && select_units(s->g, p, select) == 0; p++) {
		}
		order_past(s->g, past);
		if (p < 8 && kp_session_command(s, select, (size_t)(2 + 2 * select[1]), p) == 0
			&& kp_session_command(s, past, sizeof past, p) == 0
			&& kp_save_encode(s, &identity, NULL, NULL, &out, &size, &err) == 0) {
			seed(sd, KP_FUZZ_SAVE, "file-queued-order-past-orders-dat.kpsave", 0, out, size);
			free(out);
		}
		kp_turns_done(&s->turns);
	}
	{
		KpRep rep;
		if (kp_session_replay_file(s, NULL, &rep, &err) == 0) {
			if (kp_rep_write(&rep, &out, &size, &err) == 0) {
				seed(sd, KP_FUZZ_REP, "recorded.rep", -1, out, size);
				free(out);
			}
			kp_rep_free(&rep);
		}
	}
	(void)game_reset(&save_game, why, sizeof why);
}

/* The CHKs of the campaign's first missions and of a few scenarios. */
static void chk_seeds(Seeds *sd)
{
	static const char *const KEYS[] = { "missions/terran/terran01", "missions/zerg/zerg03", "missions/protoss/protoss05",
		"missions/expterran/terran04", "scenarios/build_terran", "scenarios/trigger_outcome", "scenarios/ai_queen" };
	int k;
	for (k = 0; k < (int)(sizeof KEYS / sizeof KEYS[0]); k++) {
		KpMission m;
		KpError err;
		char name[64];
		const char *slash;
		if (!kp_oracle_mission(ROOT, KEYS[k], &m, &err)) continue;
		slash = strrchr(KEYS[k], '/');
		snprintf(name, sizeof name, "%s.chk", slash ? slash + 1 : KEYS[k]);
		if (m.chk_size <= SEED_FILE_MAX) seed(sd, KP_FUZZ_CHK, name, -1, m.blob, m.chk_size);
		kp_mission_free(&m);
	}
}

int kp_fuzz_write_seeds(const char *dir, int with_data, char *why, size_t why_size)
{
	Seeds sd;
	int k;
	memset(&sd, 0, sizeof sd);
	sd.dir = dir;
	if (make_dir(dir) != 0) return failed(why, why_size, "%s: cannot be made", dir);
	synthetic_seeds(&sd);
	/* without the data, the synthetic seeds only; with it, a game that does not start fails (no silent skip) */
	if (with_data && !sd.failed && need_data(why, why_size) == 0) {
		if (kp_fuzz_init(KP_FUZZ_SAVE, why, why_size) != 0 || kp_fuzz_init(KP_FUZZ_COMMANDS, why, why_size) != 0) return -1;
		game_seeds(&sd);
		for (k = 0; k < COMMAND_MAP_COUNT; k++) unit_seeds(&sd, k);
		chk_seeds(&sd);
	}
	if (sd.failed) return failed(why, why_size, "%s", sd.why);
	return sd.count;
}
