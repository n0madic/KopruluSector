/* A network game's viewer (kp_watch.h): the growing record against the whole one, the protocol over a lossy link. */
#include "kp_test.h"

#include <stdlib.h>
#include <string.h>

#include "kp_record.h"
#include "kp_rep.h"
#include "kp_session.h"
#include "kp_turns.h"
#include "kp_watch.h"

static const uint8_t STOP[2] = { 0x1a, 0 }, HOLD[2] = { 0x2b, 0 };

#define GAME_FRAMES 80

/* A game's commands at frame f: RP0's loss game (frame 20 more than the feed's 0x200 static bytes) and a few more */
static void record_frame(KpRecorder *r, int64_t f)
{
	int k;
	if (f == 10)
		for (k = 0; k < 3; k++) kp_record_command(r, f, 0, STOP, 2);
	if (f == 20) {
		for (k = 0; k < 160; k++) kp_record_command(r, f, 0, STOP, 2);
		for (k = 0; k < 80; k++) kp_record_command(r, f, 0, HOLD, 2);
	}
	if (f == 21 || f == 30 || f == 47) kp_record_command(r, f, 1, HOLD, 2);
	if (f == 40 || f == 41 || f == 55) kp_record_command(r, f, 0, STOP, 2);
	if (f == 70) kp_record_command(r, f, 1, STOP, 2);
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

static void free_server(void *p)
{
	kp_watch_server_free((KpWatchServer *)p);
	free(p);
}

static KpWatchServer *server_new(KpT *T, const uint8_t *start, size_t size, uint32_t delay)
{
	KpWatchServer *s = (KpWatchServer *)malloc(sizeof *s);
	if (!s || kp_watch_server_init(s, 0x1234, start, size, delay) != 0) kp_t_fail(T, "out of memory");
	kp_t_defer(T, free_server, s);
	return s;
}

static void free_turns(void *p)
{
	kp_turns_free((KpTurns *)p);
	free(p);
}

static KpTurns *turns_new(KpT *T)
{
	KpTurns *t = (KpTurns *)calloc(1, sizeof *t);
	if (!t) kp_t_fail(T, "out of memory");
	kp_t_defer(T, free_turns, t);
	return t;
}

/* What a playback dispatched, frame by frame: {frame, player, size, bytes} of each command */
typedef struct {
	uint8_t bytes[16384];
	size_t size;
	int holds_at_20;
} Trace;

static void trace_frame(KpT *T, Trace *tr, int64_t f, const KpCommand *list, int count)
{
	int k;
	for (k = 0; k < count; k++) {
		kp_t_ok(T, tr->size + 4 + list[k].size <= sizeof tr->bytes, "the trace's room");
		tr->bytes[tr->size++] = (uint8_t)f;
		tr->bytes[tr->size++] = (uint8_t)(f >> 8);
		tr->bytes[tr->size++] = (uint8_t)list[k].player;
		tr->bytes[tr->size++] = (uint8_t)list[k].size;
		memcpy(tr->bytes + tr->size, list[k].bytes, list[k].size);
		tr->size += list[k].size;
		if (f == 20 && list[k].size == 2 && list[k].bytes[0] == HOLD[0]) tr->holds_at_20++;
	}
}

/* The viewer's playback as far as its record lets it run: the frames it ran */
static int64_t play_on(KpT *T, KpTurns *t, Trace *tr, int64_t f, int *ended)
{
	for (;;) {
		const KpCommand *list;
		int count, status = kp_turns_feed(t, f, &list, &count);
		kp_t_ok(T, status != KP_TURNS_NO_MEMORY, "out of memory");
		if (status == KP_TURNS_WAIT) return f;
		if (status == KP_TURNS_ENDED || kp_turns_ended(t)) {
			*ended = 1;
			return f;
		}
		trace_frame(T, tr, f, list, count);
		f++;
	}
}

/* The record grown by what the server published since, its frames and whether it is final */
static size_t grow(KpT *T, KpTurns *t, const KpWatchServer *s, size_t taken)
{
	/* nothing published yet: no buffer, and NULL + 0 is undefined before C2y (clang 18's UBSan) */
	const uint8_t *more = s->record_size > taken ? s->record + taken : NULL;
	kp_t_eq(T, kp_turns_watch_grow(t, more, s->record_size - taken, s->frames, s->final), 0, "the record grows");
	return s->record_size;
}

/* A viewer's record grown as the server publishes it, `delay` frames behind the game, the playback run as far as each
 * growth lets it: the commands it dispatches are the file's, frame 20's loss included (its read one call early), and
 * the published bytes are the closed record's. */
static void watched(KpT *T, uint32_t delay)
{
	KpRecorder *live = recorder_new(T), *whole = recorder_new(T);
	KpWatchServer *s = server_new(T, NULL, 0, delay);
	KpTurns *viewer = turns_new(T), *file = turns_new(T);
	Trace *a = (Trace *)calloc(1, sizeof *a), *b = (Trace *)calloc(1, sizeof *b);
	KpRep rep;
	const uint8_t *bytes;
	size_t size, taken = 0;
	int64_t c, vf = 0, ff = 0;
	int ended = 0, file_ended = 0;
	kp_t_ok(T, a && b, "out of memory");
	kp_t_defer(T, free, a);
	kp_t_defer(T, free, b);
	memset(&rep, 0, sizeof rep);
	kp_t_eq(T, kp_turns_watch(viewer, &rep), 0, "the viewer's source");
	for (c = 0; c < GAME_FRAMES; c++) {
		record_frame(live, c);
		kp_t_eq(T, kp_watch_server_mark(s, live, (uint32_t)(c + 1)), 0, "frame %lld's mark", (long long)c);
		kp_t_ok(T, s->frames == (c + 1 >= (int64_t)delay ? (uint32_t)(c + 1 - delay) : 0), "frame %lld: %u frames published",
			(long long)c, s->frames);
		if (s->record_size != taken || s->frames != kp_turns_frames(viewer)) taken = grow(T, viewer, s, taken);
		vf = play_on(T, viewer, a, vf, &ended);
		kp_t_ok(T, !ended && (s->frames < 2 || vf == (int64_t)s->frames - 1), "frame %lld: the viewer at %lld of %u",
			(long long)c, (long long)vf, s->frames);
	}
	kp_t_eq(T, kp_watch_server_end(s, live, GAME_FRAMES), 0, "the end");
	taken = grow(T, viewer, s, taken);
	vf = play_on(T, viewer, a, vf, &ended);
	kp_t_ok(T, ended && vf == GAME_FRAMES, "the viewer's end at %lld", (long long)vf);
	bytes = kp_record_bytes(live, &size);
	kp_t_eq(T, (int64_t)s->record_size, (int64_t)size, "the published record's size");
	kp_t_ok(T, memcmp(s->record, bytes, size) == 0, "the published record is the closed one");
	/* the file played alone, with an empty block's header after its last block, which the playback then runs (as the
	 * viewer runs it: kp_record_grow) */
	{
		uint8_t *with_end = (uint8_t *)malloc(size + 5);
		kp_t_ok(T, with_end != NULL, "out of memory");
		memcpy(with_end, bytes, size);
		with_end[size] = GAME_FRAMES;
		with_end[size + 1] = with_end[size + 2] = with_end[size + 3] = with_end[size + 4] = 0;
		kp_t_ok(T, kp_record_restore(whole, with_end, size + 5, -1) == 0, "out of memory");
		free(with_end);
	}
	rep.record = whole->buf;
	rep.record_size = whole->used;
	kp_rep_set_frames(&rep, GAME_FRAMES);
	kp_t_eq(T, kp_turns_replay(file, &rep), 0, "the file's source");
	ff = play_on(T, file, b, 0, &file_ended);
	kp_t_ok(T, file_ended && ff == GAME_FRAMES, "the file's end at %lld", (long long)ff);
	kp_t_ok(T, b->holds_at_20 < 80, "the file loses frame 20's holds (%d of 80 run)", b->holds_at_20);
	kp_t_eq(T, (int64_t)a->size, (int64_t)b->size, "the viewer's commands' bytes");
	kp_t_ok(T, memcmp(a->bytes, b->bytes, a->size) == 0, "the viewer dispatched the file's commands");
}

static void a_growing_record_plays_as_the_file(KpT *T)
{
	watched(T, 0);
	watched(T, 1);
	watched(T, 7);
	watched(T, 25);
}

/* --- the protocol over a link --- */

#define LINK_SLOTS 256

typedef struct {
	uint8_t data[LINK_SLOTS][KP_WATCH_DATA_HEADER + KP_WATCH_CHUNK];
	size_t size[LINK_SLOTS];
	int count;
} Queue;

typedef struct {
	KpT *T;
	Queue up, down; /* viewer to server, server to viewer */
	uint32_t rng;
	int loss_percent;
	size_t request_size; /* the request being answered */
	long sent, lost;
} Link;

static int lose(Link *l)
{
	l->rng = l->rng * 1103515245u + 12345u;
	return (int)((l->rng >> 16) % 100) < l->loss_percent;
}

static void push(Link *l, Queue *q, const uint8_t *data, size_t size)
{
	l->sent++;
	if (lose(l)) {
		l->lost++;
		return;
	}
	kp_t_ok(l->T, q->count < LINK_SLOTS && size <= sizeof q->data[0], "the link's room");
	memcpy(q->data[q->count], data, size);
	q->size[q->count++] = size;
}

static void to_server(void *ud, const uint8_t *data, size_t size)
{
	Link *l = (Link *)ud;
	push(l, &l->up, data, size);
}

static void to_viewer(void *ud, const uint8_t *data, size_t size)
{
	Link *l = (Link *)ud;
	kp_t_ok(l->T, size <= l->request_size, "an answer of %zu bytes to a request of %zu", size, l->request_size);
	push(l, &l->down, data, size);
}

/* What waits on the link delivered: the requests answered, the answers taken */
static void deliver(Link *l, KpWatchServer *s, KpWatchViewer *v, uint32_t now)
{
	int k;
	Queue up = l->up;
	l->up.count = 0;
	for (k = 0; k < up.count; k++) {
		l->request_size = up.size[k];
		kp_watch_server_receive(s, up.data[k], up.size[k], to_viewer, l);
	}
	for (k = 0; k < l->down.count; k++) kp_watch_viewer_receive(v, l->down.data[k], l->down.size[k], now);
	l->down.count = 0;
}

static void free_viewer(void *p)
{
	kp_watch_viewer_free((KpWatchViewer *)p);
	free(p);
}

static void free_link(void *p) { free(p); }

/* A start file of 5000 bytes, a game of GAME_FRAMES frames at 10 ms each with a quarter of the datagrams lost: the
 * viewer gets the start file and then the whole record, its frames known only where the record holds them. */
static void the_viewer_fetches_through_a_lossy_link(KpT *T)
{
	KpRecorder *live = recorder_new(T);
	uint8_t start[5000];
	KpWatchServer *s;
	KpWatchViewer *v = (KpWatchViewer *)calloc(1, sizeof *v);
	Link *l = (Link *)calloc(1, sizeof *l);
	const uint8_t *got, *bytes;
	size_t size, n;
	uint32_t now = 1000, frames, last_frames = 0;
	int64_t c = 0;
	int whole, k, ticks;
	kp_t_ok(T, v && l, "out of memory");
	kp_t_defer(T, free_viewer, v);
	kp_t_defer(T, free_link, l);
	for (k = 0; k < (int)sizeof start; k++) start[k] = (uint8_t)(k * 7 + 3);
	s = server_new(T, start, sizeof start, 3);
	l->T = T;
	l->rng = 99;
	l->loss_percent = 25;
	kp_watch_viewer_init(v, now);
	for (ticks = 0; ticks < 20000 && v->status != KP_WATCH_ENDED; ticks++, now += 10) {
		if (c < GAME_FRAMES) {
			record_frame(live, c);
			kp_t_eq(T, kp_watch_server_mark(s, live, (uint32_t)(c + 1)), 0, "the mark");
			if (++c == GAME_FRAMES) kp_t_eq(T, kp_watch_server_end(s, live, GAME_FRAMES), 0, "the end");
		}
		kp_watch_viewer_poll(v, now, to_server, l);
		deliver(l, s, v, now);
		kp_t_ok(T, v->status != KP_WATCH_FAILED && v->status != KP_WATCH_GONE, "tick %d: the viewer stopped (%d)", ticks,
			(int)v->status);
		if (kp_watch_viewer_take(v, &got, &n, &frames, &whole)) {
			kp_t_ok(T, frames >= last_frames, "the frames never go back");
			last_frames = frames;
		}
	}
	kp_t_eq(T, v->status, KP_WATCH_ENDED, "the viewer has the whole record (%d ticks)", ticks);
	kp_t_ok(T, l->lost > 0, "datagrams were lost (%ld of %ld)", l->lost, l->sent);
	got = kp_watch_viewer_start(v, &n);
	kp_t_ok(T, got && n == sizeof start && memcmp(got, start, n) == 0, "the start file");
	bytes = kp_record_bytes(live, &size);
	kp_t_eq(T, (int64_t)v->record_got, (int64_t)size, "the record's size");
	kp_t_ok(T, memcmp(v->record, bytes, size) == 0, "the record");
	kp_t_eq(T, last_frames, GAME_FRAMES, "the frames");
	kp_t_eq(T, v->taken_whole, 1, "whole");
}

/* The server never answers with more than it was sent: no answer to a request smaller than an empty answer, the
 * bytes cut to the request's size, at most a chunk. */
static void an_answer_is_never_larger_than_its_request(KpT *T)
{
	static uint8_t start[3000];
	KpWatchServer *s = server_new(T, start, sizeof start, 0);
	Link *l = (Link *)calloc(1, sizeof *l);
	uint8_t req[KP_WATCH_DATA_HEADER + KP_WATCH_CHUNK + 500];
	static const size_t SIZES[] = { KP_WATCH_REQUEST_HEADER, KP_WATCH_DATA_HEADER - 1, KP_WATCH_DATA_HEADER, 100,
		sizeof req };
	static const size_t ANSWERS[] = { 0, 0, KP_WATCH_DATA_HEADER, 100, KP_WATCH_DATA_HEADER + KP_WATCH_CHUNK };
	size_t k;
	kp_t_ok(T, l != NULL, "out of memory");
	kp_t_defer(T, free_link, l);
	l->T = T;
	memset(req, 0, sizeof req);
	memcpy(req, KP_WATCH_MAGIC, KP_WATCH_MAGIC_SIZE);
	req[KP_WATCH_AT_TYPE] = KP_WATCH_REQUEST;
	req[KP_WATCH_AT_PART] = KP_WATCH_PART_START;
	for (k = 0; k < sizeof SIZES / sizeof SIZES[0]; k++) {
		size_t want = ANSWERS[k];
		l->down.count = 0;
		l->request_size = SIZES[k];
		/* past the start file's end: no bytes */
		req[KP_WATCH_AT_OFFSET] = req[KP_WATCH_AT_OFFSET + 1] = k == 2 ? 0xff : 0;
		kp_watch_server_receive(s, req, SIZES[k], to_viewer, l);
		kp_t_eq(T, l->down.count, want ? 1 : 0, "a request of %zu bytes: answers", SIZES[k]);
		if (want) kp_t_eq(T, (int64_t)l->down.size[0], (int64_t)want, "a request of %zu bytes: the answer's size", SIZES[k]);
	}
}

/* A viewer stops when its server goes on to another game or stops answering: before the start file it failed, after
 * it the record is whole at what came. */
static void a_viewer_stops_when_its_server_goes(KpT *T)
{
	KpRecorder *live = recorder_new(T);
	static uint8_t start[100];
	KpWatchServer *s = server_new(T, start, sizeof start, 0);
	KpWatchViewer *v = (KpWatchViewer *)calloc(1, sizeof *v);
	Link *l = (Link *)calloc(1, sizeof *l);
	const uint8_t *bytes;
	size_t n;
	uint32_t now = 0, frames;
	int whole, k;
	kp_t_ok(T, v && l, "out of memory");
	kp_t_defer(T, free_viewer, v);
	kp_t_defer(T, free_link, l);
	l->T = T;
	/* no server at all */
	kp_watch_viewer_init(v, now);
	for (k = 0; k < KP_WATCH_FIRST_TIMEOUT_MS / 1000; k++, now += 1000) {
		kp_watch_viewer_poll(v, now, to_server, l);
		l->up.count = 0; /* nobody reads them */
	}
	kp_t_eq(T, v->status, KP_WATCH_FETCHING, "no answer yet: still asking (the host's game may not have started)");
	kp_watch_viewer_poll(v, now, to_server, l);
	kp_t_eq(T, v->status, KP_WATCH_FAILED, "no answer at all: failed");
	kp_watch_viewer_free(v);
	/* a game, then the server's next game */
	for (k = 0; k < 12; k++) {
		record_frame(live, 40 + k);
		kp_t_eq(T, kp_watch_server_mark(s, live, (uint32_t)(41 + k)), 0, "the mark");
	}
	l->up.count = 0;
	kp_watch_viewer_init(v, now);
	for (k = 0; k < 10; k++, now += 100) {
		kp_watch_viewer_poll(v, now, to_server, l);
		deliver(l, s, v, now);
	}
	kp_t_eq(T, v->status, KP_WATCH_LIVE, "the viewer follows the game");
	kp_t_ok(T, kp_watch_viewer_take(v, &bytes, &n, &frames, &whole) == 1 && frames == 52 && !whole, "52 frames, not whole");
	s->game++;
	for (k = 0; k < 3; k++, now += 100) {
		kp_watch_viewer_poll(v, now, to_server, l);
		deliver(l, s, v, now);
	}
	kp_t_eq(T, v->status, KP_WATCH_GONE, "another game: gone");
	kp_t_ok(T, kp_watch_viewer_take(v, &bytes, &n, &frames, &whole) == 1 && n == 0 && frames == 52 && whole,
		"whole at what came");
	kp_t_eq(T, kp_watch_viewer_take(v, &bytes, &n, &frames, &whole), 0, "nothing more");
}

/* A server whose record grows past what a replay holds (KP_WATCH_RECORD_MAX): the viewer asks for none of it and the
 * record is whole at what came, told apart from a server gone. */
static void a_viewer_takes_no_record_past_a_replays(KpT *T)
{
	KpRecorder *live = recorder_new(T);
	static uint8_t start[100];
	KpWatchServer *s = server_new(T, start, sizeof start, 0);
	KpWatchViewer *v = (KpWatchViewer *)calloc(1, sizeof *v);
	Link *l = (Link *)calloc(1, sizeof *l);
	uint8_t forged[KP_WATCH_DATA_HEADER];
	const uint8_t *bytes;
	size_t n, cap;
	uint32_t now = 0, frames;
	int whole, k;
	kp_t_ok(T, v && l, "out of memory");
	kp_t_defer(T, free_viewer, v);
	kp_t_defer(T, free_link, l);
	l->T = T;
	for (k = 0; k < 4; k++) {
		record_frame(live, k);
		kp_t_eq(T, kp_watch_server_mark(s, live, (uint32_t)(k + 1)), 0, "the mark");
	}
	kp_watch_viewer_init(v, now);
	for (k = 0; k < 10; k++, now += 100) {
		kp_watch_viewer_poll(v, now, to_server, l);
		deliver(l, s, v, now);
	}
	kp_t_eq(T, v->status, KP_WATCH_LIVE, "the viewer follows the game");
	cap = v->record_cap;
	/* the server's answer of no bytes that tells a record one byte past a replay's */
	memset(forged, 0, sizeof forged);
	memcpy(forged, KP_WATCH_MAGIC, KP_WATCH_MAGIC_SIZE);
	forged[KP_WATCH_AT_TYPE] = KP_WATCH_DATA;
	kp_le_put32(forged + KP_WATCH_AT_GAME, s->game);
	forged[KP_WATCH_AT_PART] = KP_WATCH_PART_RECORD;
	kp_le_put32(forged + KP_WATCH_AT_OFFSET, (uint32_t)v->record_got);
	kp_le_put32(forged + KP_WATCH_AT_SIZE, (uint32_t)KP_WATCH_RECORD_MAX + 1);
	kp_le_put32(forged + KP_WATCH_AT_FRAMES, 4);
	kp_le_put32(forged + KP_WATCH_AT_CRC, kp_crc32(forged, 0));
	kp_watch_viewer_receive(v, forged, sizeof forged, now);
	kp_t_ok(T, v->status == KP_WATCH_GONE && v->oversize, "past a replay's record: gone, oversize");
	kp_t_ok(T, v->known[KP_WATCH_PART_RECORD] <= KP_WATCH_RECORD_MAX && v->record_cap == cap, "no room taken for it");
	kp_t_ok(T, kp_watch_viewer_take(v, &bytes, &n, &frames, &whole) == 1 && whole, "whole at what came");
}

/* A save's load in the game takes its frame back: the server ends the game at what it published. */
static void a_load_ends_the_published_game(KpT *T)
{
	KpRecorder *live = recorder_new(T);
	KpWatchServer *s = server_new(T, NULL, 0, 2);
	int64_t c;
	size_t size;
	for (c = 0; c < 30; c++) {
		record_frame(live, c);
		kp_t_eq(T, kp_watch_server_mark(s, live, (uint32_t)(c + 1)), 0, "the mark");
	}
	kp_t_eq(T, s->frames, 28, "28 frames published");
	size = s->record_size;
	kp_t_eq(T, kp_watch_server_mark(s, live, 12), 0, "the frame after a load");
	kp_t_ok(T, s->final && s->frames == 28 && s->record_size == size, "final at what was published");
	kp_t_eq(T, kp_watch_server_mark(s, live, 13), 0, "a mark after the end");
	kp_t_eq(T, s->frames, 28, "nothing more");
}

/* With no delay every mark is published at once: a load is seen all the same, the loaded record's bytes never put
 * behind the published ones. */
static void a_load_ends_the_game_without_a_delay(KpT *T)
{
	KpRecorder *live = recorder_new(T);
	KpWatchServer *s = server_new(T, NULL, 0, 0);
	int64_t c;
	size_t size;
	for (c = 0; c < 30; c++) {
		record_frame(live, c);
		kp_t_eq(T, kp_watch_server_mark(s, live, (uint32_t)(c + 1)), 0, "the mark");
	}
	kp_t_ok(T, s->frames == 30 && s->mark_count == 0, "everything published (%u frames)", s->frames);
	size = s->record_size;
	kp_t_eq(T, kp_watch_server_mark(s, live, 12), 0, "the frame after a load");
	kp_t_ok(T, s->final && s->frames == 30 && s->record_size == size, "final at what was published");
}

/* A paused game's steps run no frame: they mark nothing, the game goes on being published, and what the pause recorded
 * (its frame the next one) comes with the frame after it. */
static void a_pause_marks_nothing(KpT *T)
{
	KpRecorder *live = recorder_new(T);
	KpWatchServer *s = server_new(T, NULL, 0, 2);
	const uint8_t *bytes;
	size_t size, before;
	int64_t c;
	int k;
	for (c = 0; c < 25; c++) {
		record_frame(live, c);
		kp_t_eq(T, kp_watch_server_mark(s, live, (uint32_t)(c + 1)), 0, "the mark");
	}
	kp_t_eq(T, s->frames, 23, "23 frames published");
	before = live->used;
	for (k = 0; k < 10; k++) { /* paused at frame 25: its turns record at 25, the frame count stays */
		kp_record_command(live, 25, 0, HOLD, 2);
		kp_t_eq(T, kp_watch_server_mark(s, live, 25), 0, "a paused step's mark");
	}
	kp_t_ok(T, !s->final && s->frames == 23, "still publishing at 23 frames (final %d)", s->final);
	kp_t_ok(T, live->used > before, "the pause recorded at frame 25");
	for (c = 25; c < 40; c++) {
		record_frame(live, c);
		kp_t_eq(T, kp_watch_server_mark(s, live, (uint32_t)(c + 1)), 0, "the mark");
	}
	kp_t_eq(T, s->frames, 38, "the game goes on: 38 frames published");
	kp_t_eq(T, kp_watch_server_end(s, live, 40), 0, "the end");
	bytes = kp_record_bytes(live, &size);
	kp_t_ok(T, s->record_size == size && memcmp(s->record, bytes, size) == 0, "the published record is the closed one");
}

/* A forged record (a host's): an entry cut after its Storm ID, then before its count, then a save after it, is walked
 * once the count comes and refused, as is an id the recorder never writes; the feed itself never dispatches a save. */
static void free_bare_session(void *p)
{
	KpSession *s = (KpSession *)p;
	kp_turns_free(&s->turns);
	free(s->g);
	free(s);
}

static KpSession *bare_viewer(KpT *T)
{
	KpSession *s = (KpSession *)calloc(1, sizeof *s);
	KpRep rep;
	if (!s || !(s->g = (KpGame *)calloc(1, sizeof *s->g))) kp_t_fail(T, "out of memory");
	kp_t_defer(T, free_bare_session, s);
	memset(&rep, 0, sizeof rep);
	kp_t_eq(T, kp_turns_watch(&s->turns, &rep), 0, "the viewer's source");
	return s;
}

static void a_forged_record_dispatches_no_save(KpT *T)
{
	/* frame 0's block: {Storm 0, 0x09 count 1, 2 bytes}, {Storm 0, 0x06 save, 32 bytes}: 5 + 34 bytes */
	uint8_t block[5 + 39], unknown[5 + 2] = { 0, 0, 0, 0, 2, 0, 0x3c };
	KpSession *s = bare_viewer(T), *u = bare_viewer(T);
	KpTurns *t = turns_new(T);
	KpRep rep;
	const KpCommand *list;
	int count, k;
	memset(block, 0, sizeof block);
	block[4] = 39;
	block[6] = 0x09;
	block[7] = 1;
	block[11] = 0x06;
	kp_t_eq(T, kp_session_watch_grow(s, block, 6, 1, 0), 0, "an entry cut after its Storm ID waits for its id");
	kp_t_eq(T, kp_session_watch_grow(s, block + 6, 1, 1, 0), 0, "the cut command waits for its count");
	kp_t_eq(T, kp_session_watch_grow(s, block + 7, sizeof block - 7, 2, 0), -1, "the save after it is refused");
	kp_t_ok(T, s->failed, "the viewer fails");
	kp_t_eq(T, kp_session_watch_grow(u, unknown, sizeof unknown, 1, 0), -1, "an unknown id is refused");
	/* the feed alone, past no walk: frame 0's {Storm 0, 0x1a 0}, {Storm 0, 0x06 save} */
	memset(&rep, 0, sizeof rep);
	kp_t_eq(T, kp_turns_watch(t, &rep), 0, "the feed's source");
	memset(block, 0, sizeof block);
	block[4] = 37;
	block[6] = STOP[0];
	block[9] = 0x06;
	kp_t_eq(T, kp_turns_watch_grow(t, block, 5 + 37, 1, 1), 0, "the record grows");
	kp_t_eq(T, kp_turns_feed(t, 0, &list, &count), KP_TURNS_READY, "frame 0");
	kp_t_eq(T, count, 1, "one command dispatched");
	for (k = 0; k < count; k++) kp_t_ok(T, list[k].bytes[0] == STOP[0], "the save is dropped");
}

static const KpTest TESTS[] = {
	{ "a_growing_record_plays_as_the_file", a_growing_record_plays_as_the_file, 0 },
	{ "the_viewer_fetches_through_a_lossy_link", the_viewer_fetches_through_a_lossy_link, 0 },
	{ "an_answer_is_never_larger_than_its_request", an_answer_is_never_larger_than_its_request, 0 },
	{ "a_viewer_stops_when_its_server_goes", a_viewer_stops_when_its_server_goes, 0 },
	{ "a_viewer_takes_no_record_past_a_replays", a_viewer_takes_no_record_past_a_replays, 0 },
	{ "a_load_ends_the_published_game", a_load_ends_the_published_game, 0 },
	{ "a_load_ends_the_game_without_a_delay", a_load_ends_the_game_without_a_delay, 0 },
	{ "a_pause_marks_nothing", a_pause_marks_nothing, 0 },
	{ "a_forged_record_dispatches_no_save", a_forged_record_dispatches_no_save, 0 },
};

KP_SUITE(kp_suite_watch, "test_watch", TESTS);
