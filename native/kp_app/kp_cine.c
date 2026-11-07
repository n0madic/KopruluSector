/* The cinematics' pack, its player and a run of movies (kp_cine.h). */
#include "kp_cine.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vpx/vp8dx.h"
#include "vpx/vpx_decoder.h"

#define CLIP_RECORD 108 /* docs/formats/packs.md, CINE's CLIP */
#define SUBS_HEAD 12 /* SUBS: the locale, u32 cues, u32 text bytes */
#define SUBS_CLIP 8 /* per clip u32 first cue, u32 cues */
#define CUE_RECORD 16 /* per cue u32 start, end, text offset, size */
#define NAME_SIZE 64
#define KEY_BIT 0x80000000u
#define WINDOW 262144 /* the packets are read from the file a window at a time */
#define MAX_PACKETS_PER_CALL 8 /* a movie behind the clock catches up over several calls */
#define FIXED 65536.0 /* the colour matrices' 16.16 fixed point */
#define NS_PER_MS 1000000u
#define LOG_LINE 512

static int fail(KpError *err, const char *what)
{
	snprintf(err->msg, sizeof err->msg, "cinematics: %s", what);
	return -1;
}

/* --- the index --- */

static int clip_record(KpCineIndex *ix, const uint8_t *r, uint64_t data_at, uint32_t data_size, uint64_t frms_at,
	uint32_t frms_size, uint32_t frames_so_far, KpError *err)
{
	KpCineClip *c = &ix->clips[ix->count];
	uint32_t video_at, audio_at, first;
	int j;
	memcpy(c->name, r, NAME_SIZE);
	c->name[NAME_SIZE] = 0;
	c->id = r[64];
	c->width = (uint16_t)kp_le_u16(r + 68);
	c->height = (uint16_t)kp_le_u16(r + 70);
	c->shown_w = (uint16_t)kp_le_u16(r + 72);
	c->shown_h = (uint16_t)kp_le_u16(r + 74);
	c->rate_num = kp_le_u32(r + 76);
	c->rate_den = kp_le_u32(r + 80);
	c->frames = kp_le_u32(r + 84);
	video_at = kp_le_u32(r + 88);
	c->video_size = kp_le_u32(r + 92);
	first = kp_le_u32(r + 96);
	audio_at = kp_le_u32(r + 100);
	c->audio_size = kp_le_u32(r + 104);
	if (c->width == 0 || c->height == 0 || c->width > KP_CINE_MAX_SIDE || c->height > KP_CINE_MAX_SIDE
		|| c->shown_w == 0 || c->shown_h == 0)
		return fail(err, "a clip's size is 0 or too large");
	if (c->rate_num == 0 || c->rate_den == 0 || c->frames == 0) return fail(err, "a clip has no rate or no frames");
	if (c->rate_num > KP_CINE_MAX_RATE_TERM || c->rate_den > KP_CINE_MAX_RATE_TERM) return fail(err, "a clip's rate is out of range");
	if (c->id >= KP_CINE_CLIPS) return fail(err, "a clip's number is out of range");
	for (j = 0; j < ix->count; j++) {
		if (ix->clips[j].id == c->id) return fail(err, "a clip's number repeats");
	}
	if ((uint64_t)video_at + c->video_size > data_size || (uint64_t)audio_at + c->audio_size > data_size)
		return fail(err, "a clip runs past DATA");
	if ((uint64_t)first + c->frames > frms_size / 4 || first != frames_so_far)
		return fail(err, "a clip's frame table is out of place");
	c->video_offset = data_at + video_at;
	c->audio_offset = data_at + audio_at;
	c->frame_table = frms_at + 4ull * first;
	ix->count++;
	return 0;
}

/* size bytes at offset `at` of section s: from the head when it holds them, else read. */
static int section_bytes(const KpKpfSection *s, uint64_t at, uint8_t *out, size_t size, KpCineRead read, void *ctx)
{
	if (s->data) {
		memcpy(out, s->data + at, size);
		return 0;
	}
	return read(ctx, s->offset + at, out, size);
}

/* SUBS's head and clip table (s, clips of the index read): each clip's cues, the cues' and the text's offsets. */
static int subs_index(KpCineIndex *ix, const KpKpfSection *s, KpCineRead read, void *ctx, KpError *err)
{
	uint8_t h[SUBS_HEAD + KP_CINE_CLIPS * SUBS_CLIP];
	size_t table = SUBS_HEAD + (size_t)ix->count * SUBS_CLIP;
	uint32_t total, text, sum = 0;
	int k;
	if (s->size < table || section_bytes(s, 0, h, table, read, ctx) != 0) return fail(err, "cannot read SUBS");
	total = kp_le_u32(h + 4);
	text = kp_le_u32(h + 8);
	if ((uint64_t)table + (uint64_t)total * CUE_RECORD + text != s->size) return fail(err, "SUBS's size does not add up");
	for (k = 0; k < 4; k++) { /* the converter's: two small letters, two capitals ("ruRU") */
		if (k < 2 ? (h[k] < 'a' || h[k] > 'z') : (h[k] < 'A' || h[k] > 'Z')) return fail(err, "SUBS's locale is not one");
		ix->subtitles[k] = (char)h[k];
	}
	ix->subtitles[4] = 0;
	for (k = 0; k < ix->count; k++) {
		KpCineClip *c = &ix->clips[k];
		c->cue_first = kp_le_u32(h + SUBS_HEAD + SUBS_CLIP * k);
		c->cue_count = kp_le_u32(h + SUBS_HEAD + SUBS_CLIP * k + 4);
		if (c->cue_first != sum || c->cue_count > KP_CINE_CUES) return fail(err, "a clip's subtitles are out of place");
		sum += c->cue_count;
	}
	if (sum != total) return fail(err, "SUBS's cues do not add up");
	ix->text_size = text;
	ix->cues_at = s->offset + table;
	ix->text_at = ix->cues_at + (uint64_t)total * CUE_RECORD;
	return 0;
}

int kp_cine_index(KpCineIndex *ix, const uint8_t *head, size_t head_size, size_t file_size, KpCineRead read, void *ctx,
	KpError *err)
{
	static const KpKpfExpect EXPECT = { KP_CINE_KIND, 0, NULL, NULL, NULL, 0 }; /* the schema: the range below */
	KpKpf c;
	const KpKpfSection *sh, *sc, *sf, *sp, *sd, *ss;
	uint8_t h[6], recs[KP_CINE_CLIPS * CLIP_RECORD], pairs[KP_CINE_PRELUDES * 2];
	uint32_t clips, preludes, frames = 0;
	int k, rc = -1;
	memset(ix, 0, sizeof *ix);
	if (kp_kpf_parse_prefix(&c, head, head_size, file_size, "cinematics", err) != 0) return -1;
	sh = kp_kpf_find(&c, "HEAD");
	sc = kp_kpf_find(&c, "CLIP");
	sf = kp_kpf_find(&c, "FRMS");
	sp = kp_kpf_find(&c, "PREL");
	sd = kp_kpf_find(&c, "DATA");
	if (kp_kpf_check(&c, &EXPECT, err) != 0) {
		rc = -1;
	} else if (c.schema_version < KP_CINE_SCHEMA_OLDEST || c.schema_version > KP_CINE_SCHEMA) {
		snprintf(err->msg, sizeof err->msg, "cinematics: schema version %u, this build reads %u to %u; regenerate the pack",
			(unsigned)c.schema_version, (unsigned)KP_CINE_SCHEMA_OLDEST, (unsigned)KP_CINE_SCHEMA);
	} else if (!sh || !sc || !sf || !sp || !sd) {
		rc = fail(err, "a section is missing");
	} else if (sh->size != sizeof h || section_bytes(sh, 0, h, sizeof h, read, ctx) != 0) {
		rc = fail(err, "cannot read HEAD");
	} else if ((clips = kp_le_u16(h + 2)) > KP_CINE_CLIPS || (preludes = kp_le_u16(h + 4)) > KP_CINE_PRELUDES
		|| h[0] > 1) {
		rc = fail(err, "HEAD is out of range");
	} else if (sc->size != clips * CLIP_RECORD || sp->size != preludes * 2 || sf->size % 4 != 0) {
		rc = fail(err, "a section's size does not fit HEAD");
	} else if (section_bytes(sc, 0, recs, sc->size, read, ctx) != 0) {
		rc = fail(err, "cannot read the clips");
	} else if (section_bytes(sp, 0, pairs, sp->size, read, ctx) != 0) {
		rc = fail(err, "cannot read the preludes");
	} else {
		rc = 0;
		ix->variant = h[0];
		for (k = 0; k < (int)clips && rc == 0; k++) {
			if ((rc = clip_record(ix, recs + (size_t)k * CLIP_RECORD, sd->offset, sd->size, sf->offset, sf->size,
				frames, err)) == 0) {
				frames += ix->clips[ix->count - 1].frames;
			}
		}
		for (k = 0; k < (int)preludes && rc == 0; k++) {
			ix->preludes[k][0] = pairs[2 * k];
			ix->preludes[k][1] = pairs[2 * k + 1];
			ix->prelude_count++;
		}
		if (rc == 0 && c.schema_version >= 2 && (ss = kp_kpf_find(&c, "SUBS")) != NULL) rc = subs_index(ix, ss, read, ctx, err);
	}
	kp_kpf_free(&c);
	if (rc != 0) memset(ix, 0, sizeof *ix);
	return rc;
}

int kp_cine_find(const KpCineIndex *ix, int id)
{
	int k;
	for (k = 0; k < ix->count; k++) {
		if (ix->clips[k].id == id) return k;
	}
	return -1;
}

int kp_cine_prelude(const KpCineIndex *ix, int id)
{
	int k;
	for (k = ix->prelude_count - 1; k >= 0; k--) {
		if (ix->preludes[k][0] == id) return ix->preludes[k][1];
	}
	return -1;
}

int kp_cine_subs_overlay(KpCineIndex *ix, const uint8_t *data, size_t size, KpError *err)
{
	KpKpf c;
	const KpKpfSection *s;
	uint32_t total, text, clips, sum = 0, k;
	uint64_t fixed;
	int j, rc = -1;
	if (kp_kpf_parse(&c, data, size, "language's cinematics", 1, err) != 0) return -1;
	s = kp_kpf_find(&c, "SUBS");
	if (memcmp(c.kind, KP_CINE_KIND, 4) != 0 || s == NULL || s->size < SUBS_HEAD) {
		rc = fail(err, "a language's pack has no SUBS");
		goto done;
	}
	total = kp_le_u32(s->data + 4);
	text = kp_le_u32(s->data + 8);
	fixed = (uint64_t)SUBS_HEAD + (uint64_t)total * CUE_RECORD + text;
	if (fixed > s->size || (s->size - fixed) % SUBS_CLIP != 0) {
		rc = fail(err, "a language's SUBS does not add up");
		goto done;
	}
	clips = (uint32_t)((s->size - fixed) / SUBS_CLIP);
	for (k = 0; k < clips; k++) { /* the rows lie in order; a row names its cues by number */
		if (kp_le_u32(s->data + SUBS_HEAD + SUBS_CLIP * k) != sum || kp_le_u32(s->data + SUBS_HEAD + SUBS_CLIP * k + 4) > KP_CINE_CUES) {
			rc = fail(err, "a language's subtitles are out of place");
			goto done;
		}
		sum += kp_le_u32(s->data + SUBS_HEAD + SUBS_CLIP * k + 4);
	}
	if (sum != total) {
		rc = fail(err, "a language's cues do not add up");
		goto done;
	}
	for (j = 0; j < ix->count; j++) {
		if ((uint32_t)ix->clips[j].id >= clips) {
			rc = fail(err, "a language's SUBS has no row for a clip");
			goto done;
		}
	}
	for (j = 0; j < ix->count; j++) {
		KpCineClip *clip = &ix->clips[j];
		clip->cue_first = kp_le_u32(s->data + SUBS_HEAD + SUBS_CLIP * clip->id);
		clip->cue_count = kp_le_u32(s->data + SUBS_HEAD + SUBS_CLIP * clip->id + 4);
	}
	memcpy(ix->subtitles, s->data, 4);
	ix->subtitles[4] = 0;
	ix->subs_overlay = s->data;
	ix->cues_at = (uint64_t)SUBS_HEAD + (uint64_t)clips * SUBS_CLIP;
	ix->text_at = ix->cues_at + (uint64_t)total * CUE_RECORD;
	ix->text_size = text;
	rc = 0;
done:
	kp_kpf_free(&c);
	return rc;
}

/* size bytes of the subtitles' at offset: the language's in memory, else the pack's. */
static int subs_bytes(const KpCineIndex *ix, KpCineRead read, void *ctx, uint64_t offset, uint8_t *out, size_t size)
{
	if (ix->subs_overlay == NULL) return read(ctx, offset, out, size);
	memcpy(out, ix->subs_overlay + offset, size);
	return 0;
}

int kp_cine_subs_load(const KpCineIndex *ix, int k, KpCineRead read, void *ctx, KpCineSubs *s, KpError *err)
{
	uint8_t rec[KP_CINE_CUES * CUE_RECORD];
	const KpCineClip *c = &ix->clips[k];
	uint32_t lo = UINT32_MAX, hi = 0, n = c->cue_count, j;
	s->count = 0;
	if (n == 0) return 0;
	if (n > KP_CINE_CUES || subs_bytes(ix, read, ctx, ix->cues_at + (uint64_t)c->cue_first * CUE_RECORD, rec, (size_t)n * CUE_RECORD) != 0)
		return fail(err, "cannot read a clip's subtitles");
	for (j = 0; j < n; j++) {
		KpCineCue *q = &s->cues[j];
		const uint8_t *r = rec + (size_t)j * CUE_RECORD;
		q->start_ms = kp_le_u32(r);
		q->end_ms = kp_le_u32(r + 4);
		q->at = kp_le_u32(r + 8);
		q->size = kp_le_u32(r + 12);
		if (q->end_ms < q->start_ms || (j > 0 && q->start_ms < s->cues[j - 1].start_ms))
			return fail(err, "a clip's subtitles are out of order");
		if ((uint64_t)q->at + q->size > ix->text_size) return fail(err, "a subtitle's text runs past SUBS");
		if (q->at < lo) lo = q->at;
		if (q->at + q->size > hi) hi = q->at + q->size;
	}
	if (hi < lo) hi = lo; /* every text empty */
	if (hi - lo > KP_CINE_CUE_TEXT) return fail(err, "a clip's subtitles have too much text");
	if (hi > lo && subs_bytes(ix, read, ctx, ix->text_at + lo, (uint8_t *)s->text, hi - lo) != 0) return fail(err, "cannot read a clip's subtitles");
	for (j = 0; j < n; j++) s->cues[j].at -= lo;
	s->count = (int)n;
	return 0;
}

int kp_cine_subs_at(const KpCineSubs *s, uint64_t ms)
{
	int lo = 0, hi = s->count, k;
	while (lo < hi) { /* the first cue that starts after ms */
		int mid = (lo + hi) / 2;
		if (s->cues[mid].start_ms <= ms) lo = mid + 1;
		else hi = mid;
	}
	for (k = lo - 1; k >= 0; k--) { /* the last begun that has not ended (one may outlast the ones after it) */
		if (ms < s->cues[k].end_ms) return k;
	}
	return -1;
}

uint64_t kp_cine_frame_ms(const KpCineClip *clip, uint32_t n)
{
	return ((uint64_t)n * 1000u * clip->rate_den + clip->rate_num - 1) / clip->rate_num;
}

int kp_cine_queue(const KpCineIndex *ix, const int *ids, int n, int preludes, int *queue, int cap, int *wanted)
{
	int k, count = 0, all = 0;
	for (k = 0; k < n; k++) {
		int prelude = preludes ? kp_cine_prelude(ix, ids[k]) : -1, with = 0;
		if (kp_cine_find(ix, ids[k]) < 0) continue; /* a prelude leads into its cinematic only */
		if (prelude >= 0 && kp_cine_find(ix, prelude) >= 0) with = 1;
		if (all == count && count + with < cap) { /* the two together or neither, and none after one left out */
			if (with) queue[count] = prelude;
			queue[count + with] = ids[k];
			count += with + 1;
		}
		all += with + 1;
	}
	if (wanted) *wanted = all;
	return count;
}

/* --- the clock --- */

void kp_cine_clock_begin(KpCineClock *c, uint64_t ticks, int audio)
{
	memset(c, 0, sizeof *c);
	c->t0 = c->last_ticks = c->wall_base_ticks = ticks;
	c->audio = audio;
}

uint64_t kp_cine_clock_ms(KpCineClock *c, uint64_t ticks, int playing, int64_t sound_ms)
{
	uint64_t wall;
	if (ticks - c->last_ticks > KP_CINE_CLOCK_GAP_MS) c->wall_base_ticks += ticks - c->last_ticks;
	c->last_ticks = ticks;
	if (c->audio) {
		if (playing && sound_ms > 0) {
			if ((uint64_t)sound_ms > c->last_ms) c->last_ms = (uint64_t)sound_ms;
			c->wall_base_ms = c->last_ms;
			c->wall_base_ticks = ticks;
			return c->last_ms;
		}
		if (!playing) {
			c->audio = 0; /* it ended, or never began */
		} else if (ticks - c->t0 < KP_CINE_AUDIO_WAIT_MS) {
			c->wall_base_ticks = ticks; /* its start awaited: the wall clock begins when the wait ends */
			return c->last_ms;
		}
	}
	wall = c->wall_base_ms + (ticks - c->wall_base_ticks);
	if (wall > c->last_ms) c->last_ms = wall;
	return c->last_ms;
}

void kp_cine_clock_hold(KpCineClock *c, uint64_t ticks)
{
	uint64_t held = ticks - c->last_ticks;
	c->t0 += held;
	c->wall_base_ticks += held;
	c->last_ticks = ticks;
}

/* --- the pack's file over fetched chunks --- */

static void slot_clear(KpCineStream *s, int k)
{
	if (s->slots[k].request) s->fetcher.drop(s->fetcher.ctx, s->slots[k].request);
	free(s->slots[k].data);
	memset(&s->slots[k], 0, sizeof s->slots[k]);
}

static size_t chunk_bytes(const KpCineStream *s, uint64_t chunk)
{
	uint64_t at = chunk * KP_CINE_STREAM_CHUNK, left = s->file_size - at;
	return left < KP_CINE_STREAM_CHUNK ? (size_t)left : KP_CINE_STREAM_CHUNK;
}

/* The slot of a chunk, its request started when it had none: one is free, the slots outlive only the chunks kept. */
static int slot_of(KpCineStream *s, uint64_t chunk)
{
	int k, free_slot = -1;
	for (k = 0; k < KP_CINE_STREAM_SLOTS; k++) {
		if (s->slots[k].used && s->slots[k].chunk == chunk) return k;
		if (!s->slots[k].used && free_slot < 0) free_slot = k;
	}
	k = free_slot;
	s->slots[k].used = 1;
	s->slots[k].chunk = chunk;
	s->slots[k].request = s->fetcher.start(s->fetcher.ctx, chunk * KP_CINE_STREAM_CHUNK, (uint32_t)chunk_bytes(s, chunk));
	s->slots[k].failed = s->slots[k].request == NULL;
	return k;
}

/* A request of the slot that has come or failed is ended. */
static void slot_poll(KpCineStream *s, int k)
{
	int st;
	size_t n = 0;
	if (!s->slots[k].request) return;
	st = s->fetcher.state(s->fetcher.ctx, s->slots[k].request);
	if (st == 0) return;
	if (st > 0) {
		s->slots[k].data = s->fetcher.take(s->fetcher.ctx, s->slots[k].request, &n);
		if (s->slots[k].data && n != chunk_bytes(s, s->slots[k].chunk)) { /* not the range asked for */
			free(s->slots[k].data);
			s->slots[k].data = NULL;
		}
		s->slots[k].failed = s->slots[k].data == NULL;
		if (s->slots[k].data) s->chunk_came = 1;
	} else {
		s->fetcher.drop(s->fetcher.ctx, s->slots[k].request);
		s->slots[k].failed = 1;
	}
	s->slots[k].request = NULL;
}

int kp_cine_stream_read(void *ctx, uint64_t offset, uint8_t *out, size_t size)
{
	KpCineStream *s = (KpCineStream *)ctx;
	uint64_t first, last, chunk, end_chunk, chunks;
	int k, come = 1;
	if (size == 0 || size > s->file_size || offset > s->file_size - size) return -1;
	if (size <= s->head_size && offset <= s->head_size - size) {
		memcpy(out, s->head + offset, size);
		return 0;
	}
	first = offset / KP_CINE_STREAM_CHUNK;
	last = (offset + size - 1) / KP_CINE_STREAM_CHUNK;
	if (last - first >= KP_CINE_STREAM_SLOTS) return -1;
	chunks = (s->file_size + KP_CINE_STREAM_CHUNK - 1) / KP_CINE_STREAM_CHUNK;
	end_chunk = !s->chunk_came ? last + 1 : first + KP_CINE_STREAM_SLOTS < chunks ? first + KP_CINE_STREAM_SLOTS : chunks;
	for (k = 0; k < KP_CINE_STREAM_SLOTS; k++) {
		if (s->slots[k].used && (s->slots[k].chunk < first || s->slots[k].chunk >= end_chunk)) slot_clear(s, k);
	}
	for (chunk = first; chunk < end_chunk; chunk++) {
		k = slot_of(s, chunk);
		slot_poll(s, k);
		if (chunk > last) continue; /* ahead of the read: only asked for */
		if (s->slots[k].failed) return -1;
		if (!s->slots[k].data) come = 0;
	}
	if (!come) return KP_CINE_READ_WAIT;
	for (chunk = first; chunk <= last; chunk++) {
		uint64_t at = chunk * KP_CINE_STREAM_CHUNK, from = offset > at ? offset : at, to = offset + size;
		if (to > at + KP_CINE_STREAM_CHUNK) to = at + KP_CINE_STREAM_CHUNK;
		k = slot_of(s, chunk);
		memcpy(out + (from - offset), s->slots[k].data + (from - at), (size_t)(to - from));
	}
	return 0;
}

void kp_cine_stream_free(KpCineStream *s)
{
	int k;
	for (k = 0; k < KP_CINE_STREAM_SLOTS; k++) slot_clear(s, k);
}

/* --- the colour --- */

typedef struct {
	int32_t y_scale, y_low, cr_r, cb_g, cr_g, cb_b;
} YuvMatrix;

static void yuv_matrix(YuvMatrix *m, int matrix, int full_range)
{
	double kr = matrix == 601 ? 0.299 : 0.2126, kb = matrix == 601 ? 0.114 : 0.0722, kg = 1.0 - kr - kb;
	double ys = full_range ? 1.0 : 255.0 / 219.0, cs = full_range ? 1.0 : 255.0 / 224.0;
	m->y_scale = (int32_t)(ys * FIXED + 0.5);
	m->y_low = full_range ? 0 : 16;
	m->cr_r = (int32_t)(2.0 * (1.0 - kr) * cs * FIXED + 0.5);
	m->cb_b = (int32_t)(2.0 * (1.0 - kb) * cs * FIXED + 0.5);
	m->cb_g = (int32_t)(2.0 * kb * (1.0 - kb) / kg * cs * FIXED + 0.5);
	m->cr_g = (int32_t)(2.0 * kr * (1.0 - kr) / kg * cs * FIXED + 0.5);
}

/* The matrix of a picture: BT.709 when it says so, else BT.601 (unspecified: FFmpeg's guess too). */
static void image_matrix(YuvMatrix *m, const vpx_image_t *im)
{
	yuv_matrix(m, im->cs == VPX_CS_BT_709 ? 709 : 601, im->range == VPX_CR_FULL_RANGE);
}

static uint8_t clamp8(int32_t v) { return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v); }

static void yuv_pixel(const YuvMatrix *m, int y, int cb, int cr, uint8_t *rgb)
{
	int32_t yy = (y - m->y_low) * m->y_scale + 32768, u = cb - 128, v = cr - 128;
	rgb[0] = clamp8((yy + m->cr_r * v) >> 16);
	rgb[1] = clamp8((yy - m->cb_g * u - m->cr_g * v) >> 16);
	rgb[2] = clamp8((yy + m->cb_b * u) >> 16);
}

void kp_cine_yuv_rgb(int y, int cb, int cr, int matrix, int full_range, uint8_t rgb[3])
{
	YuvMatrix m;
	yuv_matrix(&m, matrix, full_range);
	yuv_pixel(&m, y, cb, cr, rgb);
}

/* --- the player --- */

struct KpCinePlayer {
	const KpCineClip *clip;
	KpCineRead read;
	void *ctx;
	vpx_codec_ctx_t codec;
	int codec_open;
	uint32_t *sizes; /* the frame table, one u32 a frame */
	uint8_t *window; /* the packets read last */
	size_t window_cap; /* WINDOW, or the largest packet if it is larger */
	uint64_t window_at; /* the window's offset in the packets */
	size_t window_size;
	uint64_t packet_at; /* the next packet's offset in the packets */
	uint32_t next; /* the next frame to decode */
	const vpx_image_t *image; /* the picture of the last KP_CINE_FRAME (NULL: a packet decoded since gave none) */
	uint8_t *rgba; /* image as RGBA once kp_cine_rgba asked for it */
	int rgba_done;
};

/* The next packet: 0, KP_CINE_READ_WAIT when it has not come, -1 with err. */
static int read_packet(KpCinePlayer *p, uint32_t size, const uint8_t **out, KpError *err)
{
	uint64_t at = p->packet_at;
	int rc;
	if (at + size > p->clip->video_size) return fail(err, "a packet runs past the clip's video");
	if (at < p->window_at || at + size > p->window_at + p->window_size) {
		uint64_t left = p->clip->video_size - at;
		size_t want = left < WINDOW ? (size_t)left : WINDOW;
		if (want < size) want = size;
		if (want > p->window_cap) return fail(err, "a packet is larger than the read window");
		rc = p->read(p->ctx, p->clip->video_offset + at, p->window, want);
		if (rc == KP_CINE_READ_WAIT) return KP_CINE_READ_WAIT;
		if (rc != 0) return fail(err, "cannot read a packet");
		p->window_at = at;
		p->window_size = want;
	}
	*out = p->window + (at - p->window_at);
	return 0;
}

static int is_srgb(const vpx_image_t *im) { return im->fmt == VPX_IMG_FMT_I444 && im->cs == VPX_CS_SRGB; }

/* Whether the picture is one the player shows: 8-bit planar of the clip's size. */
static int check_image(const KpCinePlayer *p, const vpx_image_t *im, KpError *err)
{
	if (im->bit_depth != 8 || (im->fmt & VPX_IMG_FMT_HIGHBITDEPTH) || im->d_w != p->clip->width
		|| im->d_h != p->clip->height)
		return fail(err, "a picture is not 8-bit or not the clip's size");
	if (im->fmt != VPX_IMG_FMT_I420 && im->fmt != VPX_IMG_FMT_I422 && im->fmt != VPX_IMG_FMT_I444
		&& im->fmt != VPX_IMG_FMT_I440)
		return fail(err, "a picture is not planar YCbCr");
	return 0;
}

/* The picture (check_image's) into p->rgba. */
static void convert(KpCinePlayer *p, const vpx_image_t *im)
{
	const KpCineClip *c = p->clip;
	uint32_t x, y;
	uint8_t *out = p->rgba;
	int xs = (int)im->x_chroma_shift, ys = (int)im->y_chroma_shift;
	if (is_srgb(im)) { /* planes G, B, R */
		for (y = 0; y < c->height; y++) {
			const uint8_t *g = im->planes[0] + (size_t)y * (size_t)im->stride[0];
			const uint8_t *b = im->planes[1] + (size_t)y * (size_t)im->stride[1];
			const uint8_t *r = im->planes[2] + (size_t)y * (size_t)im->stride[2];
			for (x = 0; x < c->width; x++, out += 4) {
				out[0] = r[x];
				out[1] = g[x];
				out[2] = b[x];
				out[3] = 255;
			}
		}
		return;
	}
	{
		YuvMatrix m;
		image_matrix(&m, im);
		for (y = 0; y < c->height; y++) {
			const uint8_t *yp = im->planes[0] + (size_t)y * (size_t)im->stride[0];
			const uint8_t *up = im->planes[1] + (size_t)(y >> ys) * (size_t)im->stride[1];
			const uint8_t *vp = im->planes[2] + (size_t)(y >> ys) * (size_t)im->stride[2];
			for (x = 0; x < c->width; x++, out += 4) {
				yuv_pixel(&m, yp[x], up[x >> xs], vp[x >> xs], out);
				out[3] = 255;
			}
		}
	}
}

KpCinePlayer *kp_cine_open(const KpCineIndex *ix, int k, KpCineRead read, void *ctx, KpError *err)
{
	KpCinePlayer *p;
	vpx_codec_dec_cfg_t cfg;
	uint64_t sum = 0;
	uint32_t n;
	if (k < 0 || k >= ix->count) {
		fail(err, "no such clip");
		return NULL;
	}
	p = (KpCinePlayer *)calloc(1, sizeof *p);
	if (!p) {
		fail(err, "out of memory");
		return NULL;
	}
	p->clip = &ix->clips[k];
	p->read = read;
	p->ctx = ctx;
	p->sizes = (uint32_t *)malloc(4u * p->clip->frames);
	if (!p->sizes) {
		kp_cine_free(p);
		fail(err, "out of memory");
		return NULL;
	}
	if (read(ctx, p->clip->frame_table, (uint8_t *)p->sizes, 4u * p->clip->frames) != 0) {
		kp_cine_free(p);
		fail(err, "cannot read the frame table");
		return NULL;
	}
	p->window_cap = WINDOW;
	for (n = 0; n < p->clip->frames; n++) { /* the little-endian words in place */
		p->sizes[n] = kp_le_u32((const uint8_t *)&p->sizes[n]);
		sum += p->sizes[n] & ~KEY_BIT;
		if ((p->sizes[n] & ~KEY_BIT) > p->window_cap) p->window_cap = p->sizes[n] & ~KEY_BIT;
		if ((p->sizes[n] & ~KEY_BIT) == 0) break; /* libvpx refuses an empty packet */
	}
	if (n < p->clip->frames || sum != p->clip->video_size || !(p->sizes[0] & KEY_BIT)) {
		kp_cine_free(p);
		fail(err, "the frame table does not fit the clip (its sizes, an empty packet, or a first frame that is not a "
			"key frame)");
		return NULL;
	}
	p->window = (uint8_t *)malloc(p->window_cap);
	if (!p->window) {
		kp_cine_free(p);
		fail(err, "out of memory");
		return NULL;
	}
	memset(&cfg, 0, sizeof cfg);
	cfg.threads = 1;
	cfg.w = p->clip->width;
	cfg.h = p->clip->height;
	if (vpx_codec_dec_init(&p->codec, vpx_codec_vp9_dx(), &cfg, 0) != VPX_CODEC_OK) {
		kp_cine_free(p);
		fail(err, "the VP9 decoder does not start");
		return NULL;
	}
	p->codec_open = 1;
	return p;
}

void kp_cine_free(KpCinePlayer *p)
{
	if (!p) return;
	if (p->codec_open) vpx_codec_destroy(&p->codec);
	free(p->sizes);
	free(p->window);
	free(p->rgba);
	free(p);
}

int kp_cine_advance(KpCinePlayer *p, uint64_t time_ms, KpError *err)
{
	const KpCineClip *c = p->clip;
	uint64_t due = time_ms * c->rate_num / (1000ull * c->rate_den) + 1; /* frame 0 is due at 0 */
	int budget = MAX_PACKETS_PER_CALL, got = 0, shown = 1, waiting = 0;
	if (due > c->frames) due = c->frames;
	/* a packet that shows no picture (a hidden frame not joined into a superframe) where the clock wants one: the
	 * picture the decoder gave before it is gone with its buffers, so the packets after it are decoded until one
	 * shows, within the budget */
	while (p->next < c->frames && budget > 0 && (p->next < due || !shown)) {
		uint32_t size = p->sizes[p->next] & ~KEY_BIT;
		const uint8_t *packet;
		vpx_codec_iter_t it = NULL;
		const vpx_image_t *im;
		int rc = read_packet(p, size, &packet, err);
		if (rc == KP_CINE_READ_WAIT) {
			waiting = 1;
			break;
		}
		if (rc != 0) return -1;
		budget--;
		if (vpx_codec_decode(&p->codec, packet, size, NULL, 0) != VPX_CODEC_OK) {
			snprintf(err->msg, sizeof err->msg, "cinematics: packet %u does not decode: %s", (unsigned)p->next,
				vpx_codec_error_detail(&p->codec) ? vpx_codec_error_detail(&p->codec) : vpx_codec_error(&p->codec));
			return -1;
		}
		p->packet_at += size;
		p->next++;
		im = vpx_codec_get_frame(&p->codec, &it);
		p->image = NULL; /* the decoder's buffers moved on */
		shown = im != NULL;
		if (im && (p->next >= due || budget == 0)) { /* only the picture the clock wants is kept */
			if (check_image(p, im, err) != 0) return -1;
			p->image = im;
			p->rgba_done = 0;
			got = 1;
		}
	}
	if (got) return KP_CINE_FRAME;
	if (waiting) return KP_CINE_WAIT;
	if (p->next >= c->frames && time_ms >= kp_cine_frame_ms(c, c->frames)) return KP_CINE_END;
	return KP_CINE_IDLE;
}

int kp_cine_picture(const KpCinePlayer *p, KpVideoPicture *out)
{
	const vpx_image_t *im = p->image;
	YuvMatrix m;
	int k;
	if (!im) return -1;
	memset(out, 0, sizeof *out);
	for (k = 0; k < 3; k++) {
		out->planes[k] = im->planes[k];
		out->strides[k] = im->stride[k];
	}
	out->width = (int32_t)im->d_w;
	out->height = (int32_t)im->d_h;
	out->chroma_w = (int32_t)((im->d_w + (1u << im->x_chroma_shift) - 1) >> im->x_chroma_shift);
	out->chroma_h = (int32_t)((im->d_h + (1u << im->y_chroma_shift) - 1) >> im->y_chroma_shift);
	out->srgb = is_srgb(im);
	image_matrix(&m, im);
	out->y_low = (float)m.y_low / 255.0f;
	out->y_scale = (float)(m.y_scale / FIXED);
	out->cr_r = (float)(m.cr_r / FIXED);
	out->cb_g = (float)(m.cb_g / FIXED);
	out->cr_g = (float)(m.cr_g / FIXED);
	out->cb_b = (float)(m.cb_b / FIXED);
	return 0;
}

const uint8_t *kp_cine_rgba(KpCinePlayer *p)
{
	if (!p->image) return NULL;
	if (!p->rgba && !(p->rgba = (uint8_t *)malloc(4u * p->clip->width * p->clip->height))) return NULL;
	if (!p->rgba_done) convert(p, p->image);
	p->rgba_done = 1;
	return p->rgba;
}
const KpCineClip *kp_cine_clip(const KpCinePlayer *p) { return p->clip; }
uint32_t kp_cine_decoded(const KpCinePlayer *p) { return p->next; }

/* --- a run of movies --- */

#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 2, 3)))
#endif
static void say(const KpCineRun *r, const char *fmt, ...)
{
	char line[LOG_LINE];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(line, sizeof line, fmt, ap);
	va_end(ap);
	r->host.log(r->host.ctx, line);
}

static uint64_t now_ms(const KpCineRun *r) { return r->host.now_ns(r->host.ctx) / NS_PER_MS; }

int kp_cine_run_has(const KpCineRun *r, int id) { return r->have && kp_cine_find(&r->index, id) >= 0; }

/* The movie's time: its clock at the ticks, the sound's position read while it may keep it. */
static uint64_t movie_ms(KpCineRun *r)
{
	int64_t ms = -1;
	int playing = r->clock.audio ? r->host.sound_ms(r->host.ctx, &ms) : 0;
	return kp_cine_clock_ms(&r->clock, now_ms(r), playing, ms);
}

/* The run's end: its file closed, the host's input its own again. */
static void finish(KpCineRun *r)
{
	if (r->file) r->host.close(r->host.ctx, r->file);
	r->file = NULL;
	r->active = 0;
	r->host.end(r->host.ctx);
}

/* The player of the next movie of the queue that opens, from queue_pos + 1 (the ones that do not are logged); 0 when
 * none does. */
static int open_next(KpCineRun *r)
{
	KpError err;
	for (r->queue_pos++; r->have && r->queue_pos < r->queue_len; r->queue_pos++) {
		int id = r->queue[r->queue_pos], k = kp_cine_find(&r->index, id);
		if (k >= 0 && (r->player = kp_cine_open(&r->index, k, r->host.read, r->file, &err)) != NULL) return 1;
		say(r, "cinematics: %d: %s", id, k < 0 ? "not in the pack" : err.msg);
	}
	return 0;
}

/* The movie opened begins: its sound and its clock. */
static void begin_movie(KpCineRun *r)
{
	const KpCineClip *clip = kp_cine_clip(r->player);
	int audio = clip->audio_size > 0 && r->host.sound(r->host.ctx, clip);
	KpError err;
	kp_cine_clock_begin(&r->clock, now_ms(r), audio);
	r->begun_ms = now_ms(r);
	r->drawn_ns = 0;
	r->press_ns = 0;
	r->shown = 0;
	r->time_ms = 0;
	r->waiting = 0;
	r->movies++;
	if (kp_cine_subs_load(&r->index, kp_cine_find(&r->index, clip->id), r->host.read, r->file, &r->subs, &err) != 0)
		say(r, "%s: %s plays without them", err.msg, clip->name);
	say(r, "cinematics: %s %dx%d, %u frames", clip->name, clip->width, clip->height, (unsigned)clip->frames);
}

/* The movie playing ended (its sound released): the next of the queue begins, or the run ends. */
static void end_movie(KpCineRun *r)
{
	if (r->player) {
		const KpCineClip *clip = kp_cine_clip(r->player);
		say(r, "cinematics: %s ended after %llu ms of the wall, %llu of its own; %u of %u frames decoded, %u pictures shown",
			clip->name, (unsigned long long)(now_ms(r) - r->begun_ms), (unsigned long long)r->clock.last_ms,
			(unsigned)kp_cine_decoded(r->player), (unsigned)clip->frames, (unsigned)r->shown);
		kp_cine_free(r->player);
	}
	r->player = NULL;
	r->host.sound_stop(r->host.ctx);
	if (open_next(r)) begin_movie(r);
	else finish(r);
}

/* The movies of ids[0..n) queued from queue_len on; how many (the ones past the queue's end logged). */
static int enqueue(KpCineRun *r, const int *ids, int n, int preludes)
{
	int wanted, count = kp_cine_queue(&r->index, ids, n, preludes, r->queue + r->queue_len,
		KP_CINE_RUN_QUEUE - r->queue_len, &wanted);
	if (count < wanted) say(r, "cinematics: %d of %d movies left out (a queue of %d)", wanted - count, wanted, KP_CINE_RUN_QUEUE);
	r->queue_len += count;
	return count;
}

int kp_cine_run_start(KpCineRun *r, const int *ids, int n, int preludes)
{
	if (!r->have) return 0;
	if (r->active) return enqueue(r, ids, n, preludes) > 0; /* after the ones playing */
	r->queue_len = 0;
	if (enqueue(r, ids, n, preludes) == 0) return 0;
	if (!(r->file = r->host.open(r->host.ctx))) return 0;
	r->queue_pos = -1;
	if (!open_next(r)) { /* none of them opens: nothing changed */
		r->host.close(r->host.ctx, r->file);
		r->file = NULL;
		return 0;
	}
	r->host.begin(r->host.ctx);
	r->active = 1;
	begin_movie(r);
	return 1;
}

int kp_cine_run_frame(KpCineRun *r, KpVideoPicture *pic)
{
	KpError err;
	int rc;
	if (!r->active || !r->player) return 0;
	if (r->waiting) kp_cine_clock_hold(&r->clock, now_ms(r));
	else r->time_ms = movie_ms(r);
	rc = kp_cine_advance(r->player, r->time_ms, &err);
	if (rc == KP_CINE_WAIT) {
		if (!r->waiting) {
			r->host.sound_pause(r->host.ctx, 1);
			if (!r->press_ns) r->press_ns = r->host.now_ns(r->host.ctx);
		}
		r->waiting = 1;
		return 0;
	}
	if (r->waiting) {
		r->waiting = 0;
		if (rc >= 0) r->host.sound_pause(r->host.ctx, 0);
	}
	if (rc < 0) {
		say(r, "%s: the movie ends", err.msg);
		end_movie(r);
	} else if (rc == KP_CINE_END) {
		end_movie(r);
	} else if (rc == KP_CINE_FRAME && kp_cine_picture(r->player, pic) == 0) {
		return 1;
	}
	return 0;
}

int kp_cine_run_cue(const KpCineRun *r) { return r->active && r->player ? kp_cine_subs_at(&r->subs, r->time_ms) : -1; }

void kp_cine_run_shown(KpCineRun *r)
{
	r->shown++;
	if (!r->drawn_ns) r->drawn_ns = r->host.now_ns(r->host.ctx);
	if (!r->press_ns) r->press_ns = r->drawn_ns;
}

void kp_cine_run_skip(KpCineRun *r, uint64_t when_ns)
{
	if (!r->active || !r->press_ns || when_ns < r->press_ns) return;
	end_movie(r);
}

void kp_cine_run_stop(KpCineRun *r)
{
	if (!r->active) return;
	r->queue_pos = r->queue_len - 1;
	end_movie(r);
}
