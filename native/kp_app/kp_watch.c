/* A network game's viewer (kp_watch.h). */
#include "kp_watch.h"

#include <stdlib.h>
#include <string.h>

#include "kp_kpf.h"

#define DATAGRAM_MAX (KP_WATCH_DATA_HEADER + KP_WATCH_CHUNK)

static void put32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
	p[2] = (uint8_t)(v >> 16);
	p[3] = (uint8_t)(v >> 24);
}

static uint32_t get32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

static int is_watch(const uint8_t *d, size_t size, uint8_t type, size_t min)
{
	return size >= min && memcmp(d, KP_WATCH_MAGIC, KP_WATCH_MAGIC_SIZE) == 0 && d[KP_WATCH_AT_TYPE] == type;
}

/* --- the server --- */

int kp_watch_server_init(KpWatchServer *s, uint32_t game, const uint8_t *start, size_t start_size, uint32_t delay)
{
	memset(s, 0, sizeof *s);
	if (!(s->start = (uint8_t *)malloc(start_size ? start_size : 1))) return -1;
	if (start_size) memcpy(s->start, start, start_size);
	s->start_size = start_size;
	s->game = game ? game : 1;
	s->delay = delay;
	return 0;
}

void kp_watch_server_free(KpWatchServer *s)
{
	free(s->start);
	free(s->record);
	free(s->marks);
	memset(s, 0, sizeof *s);
}

/* The record up to `used` published as holding the frames before `frames`: the bytes since the last publication
 * copied, the open block's size byte set as its close will set it (its frame is over: no command joins it). */
static int publish(KpWatchServer *s, const KpRecorder *rec, size_t used, uint32_t frames)
{
	if (used < s->record_size || used > rec->used) return 0; /* not a prefix of what was published: kept as it was */
	if (used > s->record_cap) {
		size_t cap = s->record_cap ? s->record_cap : KP_RECORD_START;
		uint8_t *g;
		while (cap < used) cap *= 2;
		if (!(g = (uint8_t *)realloc(s->record, cap))) return -1;
		s->record = g;
		s->record_cap = cap;
	}
	/* nothing new before the first block: no buffer yet, and NULL + 0 is undefined before C2y (clang 18's UBSan) */
	if (used > s->record_size) memcpy(s->record + s->record_size, rec->buf + s->record_size, used - s->record_size);
	if (rec->open && rec->open <= used) s->record[rec->open - 1] = (uint8_t)(used - rec->open);
	s->record_size = used;
	if (frames > s->frames) s->frames = frames;
	return 0;
}

int kp_watch_server_mark(KpWatchServer *s, const KpRecorder *rec, uint32_t frames)
{
	KpWatchMark *m;
	int64_t due;
	if (s->final) return 0;
	if (s->marked && frames < s->last_mark) { /* a save's load took the frame back: the game ends here */
		s->final = 1;
		s->mark_count = 0;
		s->mark_head = 0;
		return 0;
	}
	if (s->marked && frames == s->last_mark) return 0; /* a paused game's step: no frame ran */
	s->marked = 1;
	s->last_mark = frames;
	if (s->mark_head + s->mark_count == s->mark_cap) {
		if (s->mark_head) { /* the published marks' room taken back */
			memmove(s->marks, s->marks + s->mark_head, s->mark_count * sizeof *s->marks);
			s->mark_head = 0;
		} else {
			size_t cap = s->mark_cap ? s->mark_cap * 2 : 256;
			if (!(m = (KpWatchMark *)realloc(s->marks, cap * sizeof *m))) return -1;
			s->marks = m;
			s->mark_cap = cap;
		}
	}
	m = &s->marks[s->mark_head + s->mark_count++];
	m->frames = frames;
	m->used = rec->used;
	due = (int64_t)frames - (int64_t)s->delay;
	while (s->mark_count && (int64_t)s->marks[s->mark_head].frames <= due) {
		const KpWatchMark *p = &s->marks[s->mark_head];
		/* every mark up to the due one: only the last needs its bytes, the open block's size byte is its own */
		if (s->mark_count == 1 || (int64_t)s->marks[s->mark_head + 1].frames > due) {
			if (publish(s, rec, p->used, p->frames) != 0) return -1;
		}
		s->mark_head++;
		s->mark_count--;
	}
	if (!s->mark_count) s->mark_head = 0;
	return 0;
}

int kp_watch_server_end(KpWatchServer *s, const KpRecorder *rec, uint32_t frames)
{
	if (s->final) return 0;
	if (rec && publish(s, rec, rec->used, frames) != 0) return -1;
	s->final = 1;
	s->mark_count = 0;
	s->mark_head = 0;
	return 0;
}

void kp_watch_server_receive(KpWatchServer *s, const uint8_t *data, size_t size, KpWatchSend send, void *ud)
{
	uint8_t out[DATAGRAM_MAX];
	uint32_t game, offset;
	const uint8_t *bytes;
	size_t total, n = 0, room;
	int part;
	if (!is_watch(data, size, KP_WATCH_REQUEST, KP_WATCH_DATA_HEADER)) return; /* too small for even an empty answer */
	game = get32(data + KP_WATCH_AT_GAME);
	part = data[KP_WATCH_AT_PART];
	offset = get32(data + KP_WATCH_AT_OFFSET);
	if (part == KP_WATCH_PART_START) {
		bytes = s->start;
		total = s->start_size;
	} else if (part == KP_WATCH_PART_RECORD) {
		bytes = s->record;
		total = s->record_size;
	} else
		return;
	room = (size < DATAGRAM_MAX ? size : DATAGRAM_MAX) - KP_WATCH_DATA_HEADER; /* never more than the request */
	if ((game == 0 || game == s->game) && offset < total) n = total - offset < room ? total - offset : room;
	memcpy(out, KP_WATCH_MAGIC, KP_WATCH_MAGIC_SIZE);
	out[KP_WATCH_AT_TYPE] = KP_WATCH_DATA;
	put32(out + KP_WATCH_AT_GAME, s->game);
	out[KP_WATCH_AT_PART] = (uint8_t)part;
	put32(out + KP_WATCH_AT_OFFSET, offset);
	put32(out + KP_WATCH_AT_SIZE, (uint32_t)total);
	put32(out + KP_WATCH_AT_FRAMES, part == KP_WATCH_PART_RECORD ? s->frames : 0);
	out[KP_WATCH_AT_FINAL] = (uint8_t)(part == KP_WATCH_PART_RECORD ? s->final : 1);
	out[KP_WATCH_AT_COUNT] = (uint8_t)n;
	out[KP_WATCH_AT_COUNT + 1] = (uint8_t)(n >> 8);
	put32(out + KP_WATCH_AT_CRC, kp_crc32(n ? bytes + offset : out, n));
	if (n) memcpy(out + KP_WATCH_DATA_HEADER, bytes + offset, n);
	s->answers++;
	send(ud, out, KP_WATCH_DATA_HEADER + n);
}

/* --- the viewer --- */

void kp_watch_viewer_init(KpWatchViewer *v, uint32_t now)
{
	memset(v, 0, sizeof *v);
	v->status = KP_WATCH_FETCHING;
	v->last_heard = now;
}

void kp_watch_viewer_free(KpWatchViewer *v)
{
	free(v->start);
	free(v->record);
	memset(v, 0, sizeof *v);
}

static int stopped(const KpWatchViewer *v) { return v->status == KP_WATCH_GONE || v->status == KP_WATCH_FAILED; }

static void request(KpWatchViewer *v, int part, size_t offset, size_t want, KpWatchSend send, void *ud)
{
	uint8_t out[DATAGRAM_MAX];
	size_t size = KP_WATCH_DATA_HEADER + want;
	memset(out, 0, size);
	memcpy(out, KP_WATCH_MAGIC, KP_WATCH_MAGIC_SIZE);
	out[KP_WATCH_AT_TYPE] = KP_WATCH_REQUEST;
	put32(out + KP_WATCH_AT_GAME, v->game);
	out[KP_WATCH_AT_PART] = (uint8_t)part;
	put32(out + KP_WATCH_AT_OFFSET, (uint32_t)offset);
	send(ud, out, size);
}

void kp_watch_viewer_poll(KpWatchViewer *v, uint32_t now, KpWatchSend send, void *ud)
{
	int part, k;
	size_t got, known;
	if (stopped(v) || v->status == KP_WATCH_ENDED) return;
	if ((uint32_t)(now - v->last_heard) >= (v->game ? KP_WATCH_TIMEOUT_MS : KP_WATCH_FIRST_TIMEOUT_MS)) {
		v->status = v->status == KP_WATCH_FETCHING ? KP_WATCH_FAILED : KP_WATCH_GONE;
		return;
	}
	if (v->asked && !v->fresh && (uint32_t)(now - v->last_request) < KP_WATCH_POLL_MS) return;
	part = v->status == KP_WATCH_FETCHING ? KP_WATCH_PART_START : KP_WATCH_PART_RECORD;
	got = part == KP_WATCH_PART_START ? v->start_got : v->record_got;
	known = v->known[part];
	if (known > got) { /* bytes wait: whole chunks, one after the other */
		for (k = 0; k < KP_WATCH_BURST && got + (size_t)k * KP_WATCH_CHUNK < known; k++)
			request(v, part, got + (size_t)k * KP_WATCH_CHUNK, KP_WATCH_CHUNK, send, ud);
	} else
		request(v, part, got, part == KP_WATCH_PART_START && !v->asked ? KP_WATCH_CHUNK : KP_WATCH_SMALL, send, ud);
	v->asked = 1;
	v->fresh = 0;
	v->last_request = now;
}

static int append(uint8_t **buf, size_t *cap, size_t got, const uint8_t *bytes, size_t n)
{
	if (got + n > *cap) {
		size_t c = *cap ? *cap : KP_RECORD_START;
		uint8_t *g;
		while (c < got + n) c *= 2;
		if (!(g = (uint8_t *)realloc(*buf, c))) return -1;
		*buf = g;
		*cap = c;
	}
	memcpy(*buf + got, bytes, n);
	return 0;
}

void kp_watch_viewer_receive(KpWatchViewer *v, const uint8_t *data, size_t size, uint32_t now)
{
	uint32_t game, offset, total, frames;
	size_t n;
	int part, final;
	if (stopped(v) || !is_watch(data, size, KP_WATCH_DATA, KP_WATCH_DATA_HEADER)) return;
	game = get32(data + KP_WATCH_AT_GAME);
	part = data[KP_WATCH_AT_PART];
	offset = get32(data + KP_WATCH_AT_OFFSET);
	total = get32(data + KP_WATCH_AT_SIZE);
	frames = get32(data + KP_WATCH_AT_FRAMES);
	final = data[KP_WATCH_AT_FINAL] != 0;
	n = (size_t)data[KP_WATCH_AT_COUNT] | (size_t)data[KP_WATCH_AT_COUNT + 1] << 8;
	if (size != KP_WATCH_DATA_HEADER + n || n > KP_WATCH_CHUNK || part >= KP_WATCH_PARTS || (size_t)offset + n > total) return;
	if (kp_crc32(n ? data + KP_WATCH_DATA_HEADER : data, n) != get32(data + KP_WATCH_AT_CRC)) return;
	if (v->game && game != v->game) { /* the server went on to another game */
		v->status = v->status == KP_WATCH_FETCHING ? KP_WATCH_FAILED : KP_WATCH_GONE;
		return;
	}
	v->game = game;
	v->last_heard = now;
	if (part == KP_WATCH_PART_START) {
		if (v->status != KP_WATCH_FETCHING) return;
		if (total > KP_WATCH_START_MAX) {
			v->status = KP_WATCH_FAILED;
			return;
		}
		if (!v->start) {
			if (!(v->start = (uint8_t *)malloc(total ? total : 1))) {
				v->status = KP_WATCH_FAILED;
				return;
			}
			v->start_size = total;
		}
		if (total != v->start_size) return;
		v->known[part] = total;
		if (offset != v->start_got) return;
		if (n) memcpy(v->start + v->start_got, data + KP_WATCH_DATA_HEADER, n);
		v->start_got += n;
		if (n) v->fresh = 1;
		if (v->start_got == v->start_size) {
			v->status = KP_WATCH_LIVE;
			v->fresh = 1; /* the record at once */
		}
		return;
	}
	if (v->status == KP_WATCH_FETCHING) return;
	if (total > KP_WATCH_RECORD_MAX) { /* past what a replay holds: the game ends where it came to */
		v->status = KP_WATCH_GONE;
		v->oversize = 1;
		return;
	}
	if (total > v->known[part]) v->known[part] = total;
	if (offset != v->record_got) return;
	if (n) {
		if (append(&v->record, &v->record_cap, v->record_got, data + KP_WATCH_DATA_HEADER, n) != 0) {
			v->status = KP_WATCH_FAILED;
			return;
		}
		v->record_got += n;
		v->fresh = 1;
	}
	if (v->record_got == total && (total > v->ready_size || frames > v->ready_frames || final > v->ready_final) &&
		total >= v->ready_size && frames >= v->ready_frames) {
		v->ready_size = total;
		v->ready_frames = frames;
		v->ready_final = final;
		if (final) v->status = KP_WATCH_ENDED;
	}
}

const uint8_t *kp_watch_viewer_start(const KpWatchViewer *v, size_t *size)
{
	if (v->status == KP_WATCH_FETCHING || v->status == KP_WATCH_FAILED || !v->start) return NULL;
	*size = v->start_size;
	return v->start;
}

int kp_watch_viewer_take(KpWatchViewer *v, const uint8_t **bytes, size_t *size, uint32_t *frames, int *whole)
{
	int w = v->ready_final || v->status == KP_WATCH_GONE;
	if (v->status == KP_WATCH_FETCHING || v->status == KP_WATCH_FAILED) return 0;
	if (v->ready_size == v->taken && v->ready_frames == v->taken_frames && w == v->taken_whole) return 0;
	*bytes = v->record + v->taken;
	*size = v->ready_size - v->taken;
	*frames = v->ready_frames;
	*whole = w;
	v->taken = v->ready_size;
	v->taken_frames = v->ready_frames;
	v->taken_whole = w;
	return 1;
}

uint32_t kp_watch_delay_frames(double seconds, double step_ms)
{
	double frames;
	if (!(seconds > 0) || !(step_ms > 0)) return 0; /* NaN too */
	frames = seconds * 1000.0 / step_ms + 0.5;
	return frames >= (double)UINT32_MAX ? UINT32_MAX : (uint32_t)frames;
}
