/* The 1.17 replay file (kp_rep.h): the blocks, the header, the KPRX tail, every refusal, and the file the original
 * wrote in the emulator when it is present (koprulu-private, not in the repository). */
#include "kp_test.h"

#include <stdlib.h>
#include <string.h>

#include "kp_record.h"
#include "kp_rep.h"
#include "kp_session.h"

#define ORIGINAL_REPLAY "koprulu-private/emu/rp/rp-terran01/replays/01-m02.rep"

static void le32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
	p[2] = (uint8_t)(v >> 16);
	p[3] = (uint8_t)(v >> 24);
}

/* A replay of a made-up map: a 20,000-byte CHK (text that implodes, then noise that does not), two frames. */
static void synthetic(KpT *T, KpRep *r)
{
	static const uint8_t record[] = { 3, 0, 0, 0, 10, 0, 9, 2, 1, 0, 2, 0, 0, 0x1a, 0, 9, 0, 0, 0, 3, 0, 0x0f, 6 };
	uint32_t x = 12345;
	size_t i;
	KpRepPlayer p;
	memset(r, 0, sizeof *r);
	r->header[KP_REP_PRESENT] = 1;
	r->header[KP_REP_CAMPAIGN] = 2;
	r->header[KP_REP_SPEED] = 4;
	kp_rep_set_frames(r, 100);
	kp_rep_set_seed(r, 0x386d4392u);
	memset(&p, 0, sizeof p);
	p.slot = 1;
	p.type = 2;
	p.race = 1;
	p.force = 1;
	strcpy(p.name, "Anonymous");
	kp_rep_set_player(r, 1, &p);
	r->record_size = sizeof record;
	r->record = (uint8_t *)malloc(sizeof record);
	r->chk_size = 20000;
	r->chk = (uint8_t *)malloc(r->chk_size);
	kp_t_ok(T, r->record && r->chk, "memory");
	memcpy(r->record, record, sizeof record);
	for (i = 0; i < r->chk_size; i++) {
		x = x * 1103515245u + 12345u;
		r->chk[i] = i < 12000 ? (uint8_t)"VER TYPE IVE2 MTXM "[i % 19] : (uint8_t)(x >> 16);
	}
}

static void free_rep(void *p)
{
	kp_rep_free((KpRep *)p);
	free(p);
}

/* A replay freed when the test ends (the cleanups run after the test function returned). */
static KpRep *rep_new(KpT *T)
{
	KpRep *r = (KpRep *)calloc(1, sizeof *r);
	kp_t_ok(T, r != NULL, "memory");
	kp_t_defer(T, free_rep, r);
	return r;
}

static uint8_t *written(KpT *T, const KpRep *r, size_t *size)
{
	uint8_t *out = NULL;
	KpError err;
	kp_t_ok(T, kp_rep_write(r, &out, size, &err) == 0, "write: %s", err.msg);
	kp_t_defer(T, free, out);
	return out;
}

static void file_round_trip(KpT *T)
{
	KpRep *rp = rep_new(T), *bp = rep_new(T);
	KpRepPlayer p;
	KpRepRefusal why;
	KpError err;
	size_t size;
	uint8_t *file;
	synthetic(T, rp);
	file = written(T, rp, &size);
	kp_t_ok(T, size < rp->chk_size, "the map's text imploded: %zu bytes", size);
	kp_t_ok(T, kp_rep_read(bp, file, size, &why, &err) == 0, "read: %s", err.msg);
	kp_t_eq(T, why, KP_REP_OK, "no refusal");
	kp_t_ok(T, memcmp(bp->header, rp->header, KP_REP_HEADER_SIZE) == 0, "the header");
	kp_t_eq(T, (int64_t)bp->record_size, (int64_t)rp->record_size, "the record's size");
	kp_t_ok(T, memcmp(bp->record, rp->record, rp->record_size) == 0, "the record");
	kp_t_ok(T, bp->chk_size == rp->chk_size && memcmp(bp->chk, rp->chk, rp->chk_size) == 0, "the map");
	kp_t_eq(T, kp_rep_frames(bp), 100, "frames");
	kp_t_eq(T, kp_rep_campaign(bp), 2, "campaign row");
	kp_t_eq(T, kp_rep_seed(bp), 0x386d4392u, "seed");
	kp_t_eq(T, kp_rep_speed(bp), 4, "speed");
	kp_rep_player(bp, 1, &p);
	kp_t_eq_str(T, p.name, "Anonymous", "player 1's name");
	kp_t_eq(T, p.race, 1, "player 1's race");
	kp_t_eq(T, bp->has_tail, 0, "no tail");
	kp_t_eq(T, (int64_t)bp->extra_size, 0, "nothing after the map");
}

/* A chunk's bytes are its input's alone: pklib's implode reads window bytes it never wrote, so a work buffer left as
 * malloc gave it changed the imploded bytes of the same header between two runs of a script (play_4 and
 * play_4_original's LastReplay.rep). Each pass fills the work buffer with another pattern. */
static void chunk_bytes_depend_on_the_input_alone(KpT *T)
{
	KpRep *rp = rep_new(T);
	uint8_t *work = (uint8_t *)malloc(kp_rep_work_size()), *scratch = (uint8_t *)malloc(KP_REP_CHUNK);
	uint8_t first[KP_REP_HEADER_SIZE];
	size_t first_n = 0;
	int pass;
	kp_t_defer(T, free, work);
	kp_t_defer(T, free, scratch);
	kp_t_ok(T, work != NULL && scratch != NULL, "out of memory");
	synthetic(T, rp);
	for (pass = 0; pass < 3; pass++) {
		const uint8_t *out;
		size_t n;
		memset(work, (0x00aa55 >> (8 * pass)) & 0xff, kp_rep_work_size()); /* 0x55, 0xaa, 0x00 */
		n = kp_rep_chunk(rp->header, KP_REP_HEADER_SIZE, scratch, work, &out);
		kp_t_ok(T, out == scratch && n < KP_REP_HEADER_SIZE, "pass %d: imploded, %zu bytes", pass, n);
		if (pass == 0) {
			memcpy(first, out, n);
			first_n = n;
		} else {
			kp_t_ok(T, n == first_n && memcmp(out, first, n) == 0, "pass %d: the same bytes as pass 0", pass);
		}
	}
}

static void empty_record_and_tail(KpT *T)
{
	KpRep *rp = rep_new(T), *bp = rep_new(T), *pp = rep_new(T);
	KpRepTail tail;
	KpError err;
	size_t size;
	uint8_t *file, *more;
	synthetic(T, rp);
	free(rp->record);
	rp->record = NULL;
	rp->record_size = 0;
	memset(&tail, 0, sizeof tail);
	tail.version = KP_REP_TAIL_VERSION;
	tail.sim_revision = KP_REP_SIM_REVISION;
	tail.data_hash[0] = 0xab;
	tail.has_carry = 1;
	tail.carry.count_all[100] = -7;
	tail.carry.ai_reach_source[7] = 42;
	tail.cheats = 0x20;
	tail.objective_count = 2;
	tail.objectives[1] = 9;
	rp->tail = &tail;
	file = written(T, rp, &size);
	rp->tail = NULL;
	kp_t_ok(T, kp_rep_read(bp, file, size, NULL, &err) == 0, "read: %s", err.msg);
	kp_t_eq(T, (int64_t)bp->record_size, 0, "an empty record");
	kp_t_ok(T, bp->has_tail && bp->tail, "the tail");
	kp_t_ok(T, memcmp(bp->tail, &tail, sizeof tail) == 0, "the tail's fields");
	/* bytes that are no KPRX tail stay unread */
	more = (uint8_t *)malloc(size + 3); /* larger than the file without a tail */
	kp_t_ok(T, more != NULL, "memory");
	kp_t_defer(T, free, more);
	{
		size_t psize;
		uint8_t *pfile;
		rp->tail = NULL;
		pfile = written(T, rp, &psize);
		memcpy(more, pfile, psize);
		memcpy(more + psize, "abc", 3);
		kp_t_ok(T, kp_rep_read(pp, more, psize + 3, NULL, &err) == 0, "read with junk: %s", err.msg);
		kp_t_eq(T, pp->has_tail, 0, "junk is no tail");
		kp_t_eq(T, (int64_t)pp->extra_size, 3, "the junk's size");
	}
}

/* Every cut of the file is refused as damaged; a block with a wrong checksum, chunk count or length too. */
static void refusals(KpT *T)
{
	KpRep *rp = rep_new(T), *bp = rep_new(T);
	KpRepRefusal why;
	KpError err;
	size_t size, cut;
	uint8_t *file, *bad;
	synthetic(T, rp);
	file = written(T, rp, &size);
	for (cut = 0; cut < size; cut++) {
		why = KP_REP_OK;
		kp_t_ok(T, kp_rep_read(bp, file, cut, &why, &err) != 0, "a file cut at %zu", cut);
		kp_t_eq(T, why, KP_REP_DAMAGED, "the cut's reason");
	}
	bad = (uint8_t *)malloc(size);
	kp_t_ok(T, bad != NULL, "memory");
	kp_t_defer(T, free, bad);
	memcpy(bad, file, size);
	bad[0] ^= 1; /* the magic's checksum */
	kp_t_ok(T, kp_rep_read(bp, bad, size, &why, &err) != 0, "a bad checksum");
	memcpy(bad, file, size);
	le32(bad + 4, 2); /* the magic block's chunk count */
	kp_t_ok(T, kp_rep_read(bp, bad, size, &why, &err) != 0, "two chunks for 4 bytes");
	memcpy(bad, file, size);
	le32(bad + 8, 5); /* the magic's chunk length */
	kp_t_ok(T, kp_rep_read(bp, bad, size, &why, &err) != 0, "a chunk longer than the block");
	{
		uint8_t word[4] = { 0x72, 0x65, 0x52, 0x54 }, *b = NULL;
		size_t n = 0, cap = 0;
		kp_t_ok(T, kp_rep_block_write(&b, &n, &cap, word, 4) == 0, "a block");
		kp_t_defer(T, free, b);
		kp_t_ok(T, kp_rep_read(bp, b, n, &why, &err) != 0 && why == KP_REP_DAMAGED, "another magic");
	}
}

/* A player record with a Storm ID plays its commands as its slot's player: a file whose slot is no game player (0..7)
 * is refused before the playback starts, as the feed would give its commands that player. */
static void player_slot_past_the_players_refused(KpT *T)
{
	KpRep *rp = rep_new(T);
	KpRepPlayer p;
	KpSessionStart st;
	int base;
	synthetic(T, rp);
	kp_rep_player(rp, 1, &p);
	p.storm = 0;
	kp_rep_set_player(rp, 1, &p);
	kp_session_start_defaults(&st);
	base = kp_session_replay_start(&st, rp);
	kp_t_eq(T, base, 0, "slot 1 plays");
	p.slot = KP_SESSION_REPLAY_SLOTS;
	kp_rep_set_player(rp, 1, &p);
	kp_session_start_defaults(&st);
	kp_t_eq(T, kp_session_replay_start(&st, rp), -1, "slot %u refused", (unsigned)p.slot);
	p.slot = 0xffffffffu;
	kp_rep_set_player(rp, 1, &p);
	kp_t_eq(T, kp_session_replay_start(&st, rp), -1, "slot 0xffffffff refused");
	p.storm = -1; /* a record without a Storm ID plays nothing: its slot is not read */
	kp_rep_set_player(rp, 1, &p);
	kp_t_eq(T, kp_session_replay_start(&st, rp), 0, "no Storm ID");
}

/* A network game's command (the leave 0x57, the chat 0x5c) and every id the recorder never writes (a crafted file's
 * save 0x06 or load 0x07 would reach the player's saves) are refused before the playback starts; a recorded command
 * plays, and so does the sync 0x37, whose 1-byte entry the dispatcher (7 bytes) never runs. One block of one entry:
 * the command alone. */
static void unrecorded_commands_refused(KpT *T)
{
	enum { BLOCK = 5, LONGEST = 82 };
	static const uint8_t refused[] = { 0x57, 0x5c, 0x05, 0x06, 0x07, 0x08, 0x10, 0x11, 0x38, 0x39, 0x3a, 0x3b, 0x55 };
	static const uint8_t played[] = { 0x13, 0x37 }; /* a hotkey, recorded; the sync, never run */
	KpRep *rp = rep_new(T);
	KpSessionStart st;
	uint8_t *rec;
	size_t k;
	synthetic(T, rp);
	rec = (uint8_t *)realloc(rp->record, BLOCK + 1 + LONGEST);
	kp_t_ok(T, rec != NULL, "memory");
	if (rec == NULL) return;
	rp->record = rec;
	for (k = 0; k < sizeof refused + sizeof played; k++) {
		uint8_t id = k < sizeof refused ? refused[k] : played[k - sizeof refused];
		int len = kp_record_command_length(&id, 1);
		kp_t_ok(T, len > 0 && len <= LONGEST, "0x%02x's length %d", id, len);
		if (len <= 0 || len > LONGEST) continue;
		memset(rec, 0, BLOCK + 1 + (size_t)len);
		rec[0] = 3;
		rec[4] = (uint8_t)(1 + len);
		rec[BLOCK + 1] = id;
		rp->record_size = BLOCK + 1 + (size_t)len;
		kp_session_start_defaults(&st);
		kp_t_eq(T, kp_session_replay_start(&st, rp), k < sizeof refused ? -1 : 0, "0x%02x", id);
	}
}

/* Random byte flips and cuts never read past the file (ASan) and either refuse or read a whole replay. */
static void mutations(KpT *T)
{
	KpRep *rp = rep_new(T), *bp = rep_new(T);
	KpError err;
	size_t size, k;
	uint8_t *file, *m;
	uint32_t x = 0x9e3779b9u;
	int accepted = 0;
	synthetic(T, rp);
	file = written(T, rp, &size);
	for (k = 0; k < 3000; k++) {
		size_t n = size, flips, f;
		x = x * 1664525u + 1013904223u;
		if (x % 4 == 0) n = (x >> 8) % size;
		m = (uint8_t *)malloc(n ? n : 1);
		kp_t_ok(T, m != NULL, "memory");
		memcpy(m, file, n);
		flips = 1 + (x >> 4) % 4;
		for (f = 0; f < flips && n; f++) {
			x = x * 1664525u + 1013904223u;
			m[(x >> 8) % n] ^= (uint8_t)(1u << (x % 8));
		}
		if (kp_rep_read(bp, m, n, NULL, &err) == 0) {
			accepted++;
			kp_t_ok(T, bp->chk_size <= KP_REP_MAX_CHK && bp->record_size <= KP_REP_MAX_RECORD, "bounded sizes");
			kp_rep_free(bp);
		}
		free(m);
	}
	kp_t_log(T, "%d of 3000 mutated files read", accepted);
}

/* The file the original wrote at the end of terran01 in the emulator (chain rp-terran01): its fields, its one
 * recorded command, and our writer's file read back equal. */
static void reads_the_originals_file(KpT *T)
{
	KpRep *rp = rep_new(T), *bp = rep_new(T);
	KpRepPlayer p;
	KpError err;
	size_t size, wsize;
	uint8_t *file, *again;
	static const uint8_t record[] = { 0x37, 1, 0, 0, 6, 0, 0x12, 0x80, 2, 0, 0 };
	if (!kp_t_has(T, ORIGINAL_REPLAY)) kp_t_skip(T, "%s: a private file", ORIGINAL_REPLAY);
	file = kp_t_read(T, ORIGINAL_REPLAY, &size);
	kp_t_ok(T, kp_rep_read(rp, file, size, NULL, &err) == 0, "read: %s", err.msg);
	kp_t_eq(T, kp_rep_campaign(rp), 2, "campaign row (terran01)");
	kp_t_eq(T, kp_rep_brood_war(rp), 0, "not Brood War");
	kp_t_eq(T, kp_rep_frames(rp), 369, "frames");
	kp_t_eq(T, kp_rep_speed(rp), 4, "speed");
	kp_rep_player(rp, 1, &p);
	kp_t_eq_str(T, p.name, "Anonymous", "the human's name");
	kp_t_eq(T, p.storm, 0, "the human's Storm ID");
	kp_rep_player(rp, 2, &p);
	kp_t_eq(T, p.storm, -1, "a computer's Storm ID");
	kp_t_eq(T, (int64_t)rp->record_size, (int64_t)sizeof record, "the record's size");
	kp_t_ok(T, memcmp(rp->record, record, sizeof record) == 0, "the cheat at frame 311 from Storm ID 0");
	kp_t_ok(T, rp->chk_size == 88628 && memcmp(rp->chk, "VER ", 4) == 0, "the map");
	kp_t_eq(T, (int64_t)rp->extra_size, 0, "nothing after the map");
	again = written(T, rp, &wsize);
	kp_t_ok(T, kp_rep_read(bp, again, wsize, NULL, &err) == 0, "our file: %s", err.msg);
	kp_t_ok(T, bp->chk_size == rp->chk_size && memcmp(bp->chk, rp->chk, rp->chk_size) == 0, "our file's map");
	kp_t_log(T, "the original's file %zu bytes, ours %zu, %s", size, wsize,
		size == wsize && memcmp(file, again, size) == 0 ? "equal" : "different");
}

/* kp_rep_header from the fields of the original's terran01 header (and its players) gives its 633 bytes. */
static void builds_the_originals_header(KpT *T)
{
	KpRep *rp = rep_new(T), *bp = rep_new(T);
	KpRepGame game;
	KpError err;
	size_t size;
	uint8_t *file;
	int k;
	if (!kp_t_has(T, ORIGINAL_REPLAY)) kp_t_skip(T, "%s: a private file", ORIGINAL_REPLAY);
	file = kp_t_read(T, ORIGINAL_REPLAY, &size);
	kp_t_ok(T, kp_rep_read(rp, file, size, NULL, &err) == 0, "read: %s", err.msg);
	memset(&game, 0, sizeof game);
	game.frames = 369;
	game.campaign = 2;
	game.seed = kp_rep_seed(rp);
	game.player_name = "Anonymous";
	game.map_w = game.map_h = 64;
	game.tileset = 0;
	game.speed = 4;
	game.map_name = "T1) Wasteland";
	for (k = 0; k < 8; k++) kp_rep_player(rp, k, &game.players[k]);
	kp_rep_header(bp, &game);
	for (k = 0; k < KP_REP_HEADER_SIZE; k++) {
		if (bp->header[k] != rp->header[k]) kp_t_fail(T, "header +0x%x: ours 0x%02x, the original's 0x%02x", k,
			bp->header[k], rp->header[k]);
	}
}


/* --- the record (kp_record.h) --- */

static void count_command(void *ud, const uint8_t *cmd, size_t n)
{
	(void)ud;
	(void)cmd;
	(void)n;
}

/* Commands of RP0's synthetic replay: frame 10 three stops, frame 20 160 stops then 80 holds, frame 30 a hold, frame
 * 40 a stop. */
static void record_loss_game(KpRecorder *r)
{
	static const uint8_t STOP[2] = { 0x1a, 0 }, HOLD[2] = { 0x2b, 0 };
	int k;
	for (k = 0; k < 3; k++) kp_record_command(r, 10, 0, STOP, 2);
	for (k = 0; k < 160; k++) kp_record_command(r, 20, 0, STOP, 2);
	for (k = 0; k < 80; k++) kp_record_command(r, 20, 0, HOLD, 2);
	kp_record_command(r, 30, 0, HOLD, 2);
	kp_record_command(r, 40, 0, STOP, 2);
}

/* The record of RP0's synthetic replay as its script wrote it (koprulu-private/emu/rp/synth/loss.rep): those commands
 * with frame 20 in three blocks of 80 entries. */
static size_t loss_record(uint8_t *out)
{
	static const uint8_t STOP[2] = { 0x1a, 0 }, HOLD[2] = { 0x2b, 0 };
	static const struct {
		uint32_t frame;
		int count;
		const uint8_t *cmd;
	} BLOCKS[] = { { 10, 3, STOP }, { 20, 80, STOP }, { 20, 80, STOP }, { 20, 80, HOLD }, { 30, 1, HOLD }, { 40, 1, STOP } };
	size_t at = 0, b;
	int k;
	for (b = 0; b < sizeof BLOCKS / sizeof BLOCKS[0]; b++) {
		out[at] = (uint8_t)BLOCKS[b].frame;
		out[at + 1] = out[at + 2] = out[at + 3] = 0;
		out[at + 4] = (uint8_t)(3 * BLOCKS[b].count);
		at += 5;
		for (k = 0; k < BLOCKS[b].count; k++) {
			out[at++] = 0;
			out[at++] = BLOCKS[b].cmd[0];
			out[at++] = BLOCKS[b].cmd[1];
		}
	}
	return at;
}

static void free_recorder(void *p)
{
	kp_record_free((KpRecorder *)p);
	free(p);
}

static KpRecorder *recorder_new(KpT *T)
{
	KpRecorder *r = (KpRecorder *)malloc(sizeof *r);
	if (!r || kp_record_init(r) != 0) kp_t_fail(T, "out of memory");
	kp_t_defer(T, free_recorder, r);
	return r;
}

/* The blocks RecordCommand 0x478340 writes: 0xf0 bytes of entries at most (a 0xff payload never), a new block for a
 * new frame, the excluded ids and a command of 0x100 bytes not recorded. */
static void record_blocks(KpT *T)
{
	KpRecorder *r = recorder_new(T);
	static const uint8_t SYNC[7] = { 0x37 }, PAUSE[1] = { 0x10 };
	static uint8_t big[0x100];
	const uint8_t *b;
	size_t size;
	big[0] = 0x0c;
	record_loss_game(r);
	kp_record_command(r, 41, 0, SYNC, sizeof SYNC);
	kp_record_command(r, 41, 0, PAUSE, sizeof PAUSE);
	kp_record_command(r, 41, 0, big, sizeof big);
	b = kp_record_bytes(r, &size);
	/* 10: 5 + 9; 20: 84 + 84 + 72 entries (a block closes when payload + 1 + 2 would reach 0xff); 30, 40: 5 + 3 */
	kp_t_eq(T, (int64_t)size, 14 + (5 + 252) + (5 + 252) + (5 + 216) + 8 + 8, "the record's size");
	kp_t_eq(T, b[4], 9, "frame 10's payload");
	kp_t_eq(T, b[14 + 4], 252, "frame 20's first block");
	kp_t_eq(T, b[14 + 257 + 4], 252, "frame 20's second block");
	kp_t_eq(T, b[14 + 514 + 4], 216, "frame 20's third block");
	kp_t_eq(T, b[14 + 257 + 5 + 76 * 3 + 1], 0x2b, "the first hold, entry 160");
}

/* 1.17's command loss (FeedReplayCommands 0x4769b0), as the original played RP0's synthetic replay in the emulator
 * (koprulu-private/emu/rp/play-loss/dispatch.jsonl): frame 20, read early at frame 11 into a heap record the early
 * return freed, dispatches its 160 stops from the static buffers and then 80 empty buffers; frame 40, the last block,
 * never; the end at the header's 60 frames. */
static void feed_loses_commands_as_the_original(KpT *T)
{
	KpRecorder *r = recorder_new(T);
	const KpRecordEntry *e;
	static uint8_t record[1024];
	size_t size = loss_record(record);
	int f, n, k, stops = 0, empty = 0, holds = 0, at10 = 0, at30 = 0, other = 0;
	kp_t_eq(T, (int64_t)size, 765, "the synthetic record's size");
	kp_t_ok(T, kp_record_load(r, record, size) == 0, "load");
	for (f = 0; f < 60; f++) {
		kp_t_eq(T, kp_record_feed(r, f, 60, &e, &n), 0, "frame %d: not the end", f);
		for (k = 0; k < n; k++) {
			if (f == 20 && e[k].size == 2 && e[k].bytes[0] == 0x1a) stops++;
			else if (f == 20 && e[k].size == 0) empty++;
			else if (f == 20) holds++;
			else if (f == 10 && e[k].size == 2 && e[k].bytes[0] == 0x1a) at10++;
			else if (f == 30 && e[k].size == 2 && e[k].bytes[0] == 0x2b) at30++;
			else other++;
		}
	}
	kp_t_eq(T, at10, 3, "frame 10");
	kp_t_eq(T, stops, 160, "frame 20's stops");
	kp_t_eq(T, empty, 80, "frame 20's lost holds, empty buffers");
	kp_t_eq(T, holds, 0, "no hold at frame 20");
	kp_t_eq(T, at30, 1, "frame 30");
	kp_t_eq(T, other, 0, "frame 40's block never");
	kp_t_eq(T, kp_record_feed(r, 60, 60, &e, &n), 1, "the end");
}

/* The dispatcher's walk: several commands in one buffer, a name of 6/7, a stop at an unknown id. */
static void split_walks_as_the_dispatcher(KpT *T)
{
	static const uint8_t B[] = { 0x1a, 0, 0x09, 2, 1, 0, 2, 0, 0x06, 0, 0, 0, 0, 'a', 'b', 0, 0x16, 0x1a, 0 };
	kp_t_eq(T, kp_record_split(B, sizeof B, count_command, NULL), 3, "three, then 0x16 stops");
	kp_t_eq(T, kp_record_split(B, 3, count_command, NULL), 1, "a cut select is not given");
}

/* A cut last command: 0x475d30 subtracts the command's length first and stops the whole buffer when less is left,
 * so the cut command is never run and nothing past the buffer is read. A save's name is ValidateNetString 0x476e20
 * with no formatting and no control codes: a control code or no terminator within 0x1c bytes stops the buffer. */
static void split_stops_at_a_cut_last_command(KpT *T)
{
	static const uint8_t CLICK[] = { 0x1a, 0, 0x14, 1, 0, 2, 0, 0, 0, 0xe4, 0, 0 };
	static const uint8_t CONTROL[] = { 0x1a, 0, 0x06, 0, 0, 0, 0, 'a', 0x09, 0, 0x1a, 0 };
	static const uint8_t HIGH[] = { 0x06, 0, 0, 0, 0, 'a', 0xe9, 0 };
	uint8_t name[5 + 0x1d] = { 0x07 };
	kp_t_eq(T, kp_record_split(CLICK, sizeof CLICK, count_command, NULL), 2, "a whole right click of 10 bytes");
	kp_t_eq(T, kp_record_split(CLICK, sizeof CLICK - 1, count_command, NULL), 1, "a right click cut by one byte");
	kp_t_eq(T, kp_record_split(CLICK, 3, count_command, NULL), 1, "a right click's id alone");
	kp_t_eq(T, kp_record_split(CONTROL, sizeof CONTROL, count_command, NULL), 1, "a tab in a save's name stops");
	kp_t_eq(T, kp_record_split(HIGH, sizeof HIGH, count_command, NULL), 1, "bytes from 0x80 pass");
	kp_t_eq(T, kp_record_split(HIGH, sizeof HIGH - 1, count_command, NULL), 0, "a name with no terminator");
	memset(name + 5, 'n', 0x1b); /* the terminator as the 0x1c-th byte */
	kp_t_eq(T, kp_record_split(name, sizeof name, count_command, NULL), 1, "a name of 0x1b bytes");
	name[5 + 0x1b] = 'n'; /* the terminator as the 0x1d-th byte, past the bound */
	kp_t_eq(T, kp_record_split(name, sizeof name, count_command, NULL), 0, "a name of 0x1c bytes");
}

static const KpTest TESTS[] = {
	{ "file_round_trip", file_round_trip, 0 },
	{ "chunk_bytes_depend_on_the_input_alone", chunk_bytes_depend_on_the_input_alone, 0 },
	{ "empty_record_and_tail", empty_record_and_tail, 0 },
	{ "refusals", refusals, 0 },
	{ "player_slot_past_the_players_refused", player_slot_past_the_players_refused, 0 },
	{ "unrecorded_commands_refused", unrecorded_commands_refused, 0 },
	{ "mutations", mutations, 0 },
	{ "reads_the_originals_file", reads_the_originals_file, 0 },
	{ "builds_the_originals_header", builds_the_originals_header, 0 },
	{ "record_blocks", record_blocks, 0 },
	{ "feed_loses_commands_as_the_original", feed_loses_commands_as_the_original, 0 },
	{ "split_walks_as_the_dispatcher", split_walks_as_the_dispatcher, 0 },
	{ "split_stops_at_a_cut_last_command", split_stops_at_a_cut_last_command, 0 },
};

KP_SUITE(kp_suite_rep, "test_kp_rep", TESTS);
