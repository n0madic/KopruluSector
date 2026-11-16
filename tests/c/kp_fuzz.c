/* The fuzz targets' entries (kp_fuzz.h; phase T, stage T2, ADR 0022). */
#include "kp_fuzz.h"

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
#include "kp_runner.h"
#include "kp_session.h"
#include "kp_turns.h"
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

/* the commands target's scenarios (the first byte picks one): workers and buildings, a fight, a computer's spells */
static const char *const COMMAND_MAPS[] = { "scenarios/build_terran", "scenarios/bench_combat", "scenarios/ai_queen" };
#define COMMAND_MAP_COUNT ((int)(sizeof COMMAND_MAPS / sizeof COMMAND_MAPS[0]))

static const char *const NAMES[KP_FUZZ_TARGETS] = { "rep", "chk", "save", "commands", "turns" };

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
static FuzzGame save_game, command_games[COMMAND_MAP_COUNT];
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

int kp_fuzz_needs_data(int target) { return target == KP_FUZZ_SAVE || target == KP_FUZZ_COMMANDS; }

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
	int k;
	game_free(&save_game);
	free(save_base);
	save_base = NULL;
	save_base_size = 0;
	save_section_count = 0;
	for (k = 0; k < COMMAND_MAP_COUNT; k++) game_free(&command_games[k]);
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

/* The session of a scenario (recorded or not) after `steps` steps, with its snapshot. */
static int game_start(FuzzGame *fg, const char *key, int record, int steps, char *why, size_t size)
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
	fg->snap_size = kp_snapshot_size(fg->s->g);
	if (!(fg->snap = (uint8_t *)malloc(fg->snap_size)) || kp_snapshot(fg->s->g, fg->snap, fg->snap_size, &err) != 0) {
		game_free(fg);
		return failed(why, size, "%s: no snapshot", key);
	}
	return 0;
}

/* The game back at its snapshot: its state, no failure, no queued commands, no events. */
static int game_reset(FuzzGame *fg, char *why, size_t size)
{
	KpSession *s = fg->s;
	KpEvent e;
	KpError err;
	if (kp_restore(s->g, fg->snap, fg->snap_size, &err) != 0) return failed(why, size, "the snapshot's restore: %s", err.msg);
	s->failed = 0;
	s->error[0] = 0;
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
	if (game_start(&save_game, SAVE_MAP, 1, SAVE_STEPS, why, size) != 0) return -1;
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
	int k;
	switch (target) {
	case KP_FUZZ_REP: return 0;
	case KP_FUZZ_TURNS: return 0;
	case KP_FUZZ_CHK: (void)need_data(why, why_size); why[0] = 0; return 0; /* with the data when present */
	case KP_FUZZ_SAVE: return save_start(why, why_size);
	case KP_FUZZ_COMMANDS:
		for (k = 0; k < COMMAND_MAP_COUNT; k++) {
			if (game_start(&command_games[k], COMMAND_MAPS[k], 0, 0, why, why_size) != 0) return -1;
		}
		return 0;
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

static int run_save(const uint8_t *d, size_t size, char *why, size_t why_size)
{
	KpSession *s = save_game.s;
	KpError err;
	uint8_t *copy;
	size_t n;
	int loaded = 0;
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
	/* TODO: step the game a few frames after a load (a refusal counted, a crash a finding): kp_load checks the indexes
	 * and the links it reads, not every value's range, so a state the walk accepts may hold values the simulation
	 * never makes (ADR 0022, stage T2). */
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

static int run_commands(const uint8_t *d, size_t size, char *why, size_t why_size)
{
	FuzzGame *fg;
	Submit sb;
	size_t at = 1;
	int chunks = 0, steps = 0, stopped = 0;
	if (size == 0) return 0;
	fg = &command_games[d[0] % COMMAND_MAP_COUNT];
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
	if (stopped && fg->s->failed) note_refusal(fg->s->error);
	return game_reset(fg, why, why_size);
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

/* The synthetic seeds: a replay of a made-up header, record and map bytes (no data's bytes in it), and plain orders
 * of the commands target (stop, hold, the speed, a cheat, a pause and resume). */
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
 * DAT read gives KP_NIL and the requirements' range check stops the game, a refusal (test_fuzz.data_seeds_run). */
static void unit_seeds(Seeds *sd, int map)
{
	KpGame *g = command_games[map].s->g;
	uint8_t buf[256], select[2 + 2 * KP_SLOTS], click[10] = { 0x14, 0, 0, 0, 0, 0, 0, 0xe4, 0, 0 };
	uint8_t past[11] = { 0x15, 0, 0, 0, 0, 0, 0, 0xe4, 0, 0xff, 0 };
	int64_t p, n;
	int first = 1;
	for (p = 0; p < 8; p++) {
		int count = 0;
		size_t at;
		char name[48];
		for (n = g->lists[KP_LIST_ACTIVE].head; n != 0 && count < KP_SLOTS; n = g->u.next[n]) {
			if (g->u.owner[n] != p) continue;
			kp_le_put16(select + 2 + 2 * count, (uint16_t)kp_unit_ref(g, n));
			count++;
		}
		if (count == 0) continue;
		select[0] = 0x09;
		select[1] = (uint8_t)count;
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
			memcpy(past + 1, click + 1, 4);
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
 * steps on; and the replay of a recorded game with commands. */
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
