/* The cinematics' pack, its player and a run of movies (kp_cine.h). */
#include "kp_cine.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kp_sha256.h"
#include "kp_smk.h"
#include "kp_utf8.h"
#include "kp_webm.h"
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
#define NS_PER_MS 1000000u
#define LOG_LINE 512
#define NS_PER_S 1000000000ull

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
	if (memcmp(c.kind, KP_CINE_KIND, 4) != 0 || c.schema_version != KP_CINE_SCHEMA) {
		rc = fail(err, "a language's pack is of another kind or schema");
		goto done;
	}
	if (s == NULL || s->size < SUBS_HEAD) {
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

/* --- the profile's movies (CINM) --- */

#define CINM_DIGEST 32

int kp_cine_catalog(KpCineCatalog *c, const uint8_t *t, size_t size)
{
	size_t at = 4;
	int k;
	memset(c, 0, sizeof *c);
	if (size < 4) return -1;
	c->count = (int)kp_le_u16(t);
	c->prelude_count = (int)kp_le_u16(t + 2);
	if (c->count > KP_CINE_CLIPS || c->prelude_count > KP_CINE_PRELUDES) return -1;
	for (k = 0; k < c->count; k++) {
		size_t n;
		if (at >= size || (n = t[at]) == 0 || n >= KP_CINE_NAME || size - at - 1 < n + 4 + CINM_DIGEST) return -1;
		memcpy(c->movies[k].name, t + at + 1, n);
		c->movies[k].name[n] = 0;
		at += 1 + n;
		c->movies[k].size = kp_le_u32(t + at);
		memcpy(c->movies[k].sha256, t + at + 4, CINM_DIGEST);
		at += 4 + CINM_DIGEST;
	}
	if (size - at != 2u * (size_t)c->prelude_count) return -1;
	for (k = 0; k < c->prelude_count; k++) {
		c->preludes[k][0] = t[at + 2 * (size_t)k];
		c->preludes[k][1] = t[at + 2 * (size_t)k + 1];
		if (c->preludes[k][0] >= c->count || c->preludes[k][1] >= c->count) return -1;
	}
	return 0;
}

int kp_cine_catalog_check(const KpCineCatalog *c, int k, const uint8_t *data, size_t size)
{
	uint8_t d[KP_SHA256_SIZE];
	if (k < 0 || k >= c->count || size != c->movies[k].size) return -1;
	kp_sha256(data, size, d);
	return memcmp(d, c->movies[k].sha256, sizeof d) == 0 ? 0 : -1;
}

void kp_cine_index_original(KpCineIndex *ix, int variant, const KpCineCatalog *c, int (*has)(void *ctx, int k), void *ctx)
{
	int k;
	memset(ix, 0, sizeof *ix);
	ix->original = 1;
	ix->variant = variant;
	for (k = 0; k < c->count; k++) {
		KpCineClip *clip = &ix->clips[ix->count];
		size_t i;
		if (!has(ctx, k)) continue;
		snprintf(clip->name, sizeof clip->name, "smk\\%s", c->movies[k].name);
		for (i = 0; clip->name[i]; i++) {
			if (clip->name[i] >= 'A' && clip->name[i] <= 'Z') clip->name[i] = (char)(clip->name[i] - 'A' + 'a');
		}
		clip->id = (uint8_t)k;
		ix->count++;
	}
	for (k = 0; k < c->prelude_count; k++) {
		ix->preludes[k][0] = c->preludes[k][0];
		ix->preludes[k][1] = c->preludes[k][1];
	}
	ix->prelude_count = c->prelude_count;
}

/* --- SubRip subtitles (converter/formats/srt.py parse) --- */

static int ascii_space(uint8_t c) { return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r'; }

/* A span of the normalized text */
typedef struct {
	const char *p;
	size_t n;
} Span;

static Span span_strip(Span s, int all) /* all: ASCII whitespace, else '\n' only */
{
	while (s.n && (all ? ascii_space((uint8_t)s.p[0]) : s.p[0] == '\n')) {
		s.p++;
		s.n--;
	}
	while (s.n && (all ? ascii_space((uint8_t)s.p[s.n - 1]) : s.p[s.n - 1] == '\n')) s.n--;
	return s;
}

/* Digits at *p (exactly want of them, any count when want is 0): their value, -1 when none or too many. */
static int64_t srt_digits(const char **p, const char *end, int want)
{
	int64_t v = 0;
	int n = 0;
	while (*p < end && **p >= '0' && **p <= '9' && (want == 0 || n < want)) {
		if (v > 100000000) return -1;
		v = v * 10 + (**p - '0');
		(*p)++;
		n++;
	}
	return n == 0 || (want && n != want) ? -1 : v;
}

/* HH:MM:SS,mmm (or '.') in ms; -1 when it does not read or a minute or second passes 59. */
static int64_t srt_time(const char **p, const char *end)
{
	int64_t h = srt_digits(p, end, 0), m, s, ms;
	if (h < 0 || *p >= end || *(*p)++ != ':' || (m = srt_digits(p, end, 2)) < 0 || *p >= end || *(*p)++ != ':'
		|| (s = srt_digits(p, end, 2)) < 0 || *p >= end || (**p != ',' && **p != '.'))
		return -1;
	(*p)++;
	if ((ms = srt_digits(p, end, 3)) < 0 || m > 59 || s > 59) return -1;
	return ((h * 60 + m) * 60 + s) * 1000 + ms;
}

/* The times line "start --> end" (stripped): 0, -1. */
static int srt_times(Span line, int64_t *start, int64_t *end)
{
	const char *p = line.p, *e = line.p + line.n;
	if ((*start = srt_time(&p, e)) < 0) return -1;
	while (p < e && ascii_space((uint8_t)*p)) p++;
	if (e - p < 3 || memcmp(p, "-->", 3) != 0) return -1;
	p += 3;
	while (p < e && ascii_space((uint8_t)*p)) p++;
	if ((*end = srt_time(&p, e)) < 0) return -1;
	return p == e ? 0 : -1;
}

typedef struct {
	uint32_t start, end;
	Span lines;  /* the block's lines from its third, '\n' apart, each right-stripped below */
	int order;
} SrtCue;

int kp_cine_subs_srt(const uint8_t *data, size_t size, KpCineSubs *s, KpError *err)
{
	char *text;
	size_t n = 0, i, at = 0;
	SrtCue cues[KP_CINE_CUES];
	int count = 0, k;
	Span all;
	s->count = 0;
	if (size >= 3 && data[0] == 0xef && data[1] == 0xbb && data[2] == 0xbf) {
		data += 3;
		size -= 3;
	}
	for (i = 0; i < size;) { /* UTF-8 throughout */
		if (kp_utf8_next(data, size, &i) < 0) return fail(err, "subtitles: not UTF-8");
	}
	if (!(text = (char *)malloc(size + 1))) return fail(err, "out of memory");
	for (i = 0; i < size; i++) { /* CRLF and CR as LF */
		if (data[i] == '\r') {
			text[n++] = '\n';
			if (i + 1 < size && data[i + 1] == '\n') i++;
		} else {
			text[n++] = (char)data[i];
		}
	}
	all.p = text;
	all.n = n;
	all = span_strip(all, 1);
	at = 0;
	while (at <= all.n && all.n) {
		/* the block runs to the next "\n[ \t]*\n" */
		size_t stop = all.n, next = all.n + 1, j;
		Span block, line[2], rest;
		int64_t start, end;
		for (j = at; j < all.n; j++) {
			size_t q = j + 1;
			if (all.p[j] != '\n') continue;
			while (q < all.n && (all.p[q] == ' ' || all.p[q] == '\t')) q++;
			if (q < all.n && all.p[q] == '\n') {
				stop = j;
				next = q + 1;
				break;
			}
		}
		block.p = all.p + at;
		block.n = stop - at;
		at = next;
		if (span_strip(block, 1).n == 0) continue;
		block = span_strip(block, 0);
		for (k = 0; k < 2; k++) { /* its number and its times */
			const char *nl = (const char *)memchr(block.p, '\n', block.n);
			if (!nl && k == 0) {
				free(text);
				return fail(err, "subtitles: a block without its number or times");
			}
			line[k].p = block.p;
			line[k].n = nl ? (size_t)(nl - block.p) : block.n; /* the times may end the block: no text */
			block.p += line[k].n + (nl ? 1 : 0);
			block.n -= line[k].n + (nl ? 1 : 0);
			line[k] = span_strip(line[k], 1);
		}
		rest = block;
		{
			const char *p = line[0].p;
			if (srt_digits(&p, line[0].p + line[0].n, 0) < 0 || p != line[0].p + line[0].n) {
				free(text);
				return fail(err, "subtitles: a block without its number");
			}
		}
		if (srt_times(line[1], &start, &end) != 0 || end < start || end > UINT32_MAX) {
			free(text);
			return fail(err, "subtitles: a block's times do not read, or it ends before it starts");
		}
		if (span_strip(rest, 1).n == 0) continue; /* a cue with no text shows nothing */
		if (count == KP_CINE_CUES) {
			free(text);
			return fail(err, "subtitles: more cues than a movie keeps");
		}
		cues[count].start = (uint32_t)start;
		cues[count].end = (uint32_t)end;
		cues[count].lines = rest;
		cues[count].order = count;
		count++;
	}
	for (k = 1; k < count; k++) { /* by their start, the file's order among equal starts */
		SrtCue c = cues[k];
		int j = k;
		while (j > 0 && cues[j - 1].start > c.start) {
			cues[j] = cues[j - 1];
			j--;
		}
		cues[j] = c;
	}
	n = 0;
	for (k = 0; k < count; k++) { /* each text: its lines right-stripped, '\n' apart, blank edges dropped */
		Span r = cues[k].lines;
		size_t first = n;
		while (r.n) {
			const char *nl = (const char *)memchr(r.p, '\n', r.n);
			size_t len = nl ? (size_t)(nl - r.p) : r.n;
			Span ln;
			ln.p = r.p;
			ln.n = len;
			while (ln.n && ascii_space((uint8_t)ln.p[ln.n - 1])) ln.n--;
			if (n + ln.n + 1 > KP_CINE_CUE_TEXT) {
				free(text);
				return fail(err, "subtitles: more text than a movie keeps");
			}
			memcpy(s->text + n, ln.p, ln.n);
			n += ln.n;
			s->text[n++] = '\n';
			r.p += len + (nl ? 1 : 0);
			r.n -= len + (nl ? 1 : 0);
		}
		while (n > first && s->text[n - 1] == '\n') n--;
		{
			size_t lead = first;
			while (lead < n && s->text[lead] == '\n') lead++;
			memmove(s->text + first, s->text + lead, n - lead);
			n -= lead - first;
		}
		s->cues[k].start_ms = cues[k].start;
		s->cues[k].end_ms = cues[k].end;
		s->cues[k].at = (uint32_t)first;
		s->cues[k].size = (uint32_t)(n - first);
	}
	s->count = count;
	free(text);
	return 0;
}

/* --- the movies of an install (ADR 0018, stage D7) --- */

#define SMK_SHOWN_CODED 320 /* cinematics.py shown_size: a movie coded 320 wide is shown 640 wide */
#define SMK_SHOWN_WIDTH 640
#define SMK_DEFAULT_TIME 10000u /* a frame's time when the header gives none: 10 frames a second (10 us units) */
#define WAV_HEADER 44

struct KpCineMovie {
	KpCineClip clip;      /* the movie's: the index's name and number, its own size, rate, frames and sound */
	uint8_t *bytes;       /* an SMK's file (its frames are decoded from it), or a WebM's laid out as a pack's clip:
	                       * the packets, the frame table, the Ogg Vorbis (read by movie_read) */
	size_t size;
	int smk;              /* an SMK: kp_smk plays it */
	KpSmk decoder;
	uint8_t *wav;         /* an SMK's sound as a WAV file, NULL with none */
	size_t wav_size;
	KpCineSubs subs;      /* its subtitles (kp_cine_movie_subtitles), none at first */
};

int kp_cine_movie_read(void *ctx, uint64_t offset, uint8_t *out, size_t size)
{
	const KpCineMovie *m = (const KpCineMovie *)ctx;
	if (offset > m->size || size > m->size - offset) return -1;
	memcpy(out, m->bytes + offset, size);
	return 0;
}

void kp_cine_movie_free(KpCineMovie *m)
{
	if (!m) return;
	if (m->smk) kp_smk_close(&m->decoder);
	free(m->bytes);
	free(m->wav);
	free(m);
}

static void clip_named(KpCineClip *c, const KpCineClip *named, uint32_t width, uint32_t height)
{
	memcpy(c->name, named->name, sizeof c->name);
	c->id = named->id;
	c->width = (uint16_t)width;
	c->height = (uint16_t)height;
	c->shown_w = (uint16_t)(width == SMK_SHOWN_CODED ? SMK_SHOWN_WIDTH : width);
	c->shown_h = (uint16_t)height;
}

static uint64_t gcd64(uint64_t a, uint64_t b)
{
	while (b) {
		uint64_t t = a % b;
		a = b;
		b = t;
	}
	return a;
}

/* The fraction num / den with terms up to KP_CINE_MAX_RATE_TERM closest to a / b by its continued fraction's last
 * convergent that fits (a frame of frame_ns: 1e9 / 66,660,000 is 50000 / 3333). */
static void rate_fraction(uint64_t a, uint64_t b, uint32_t *num, uint32_t *den)
{
	uint64_t p0 = 0, q0 = 1, p1 = 1, q1 = 0;
	while (b) {
		uint64_t t = a / b, r = a % b, p2, q2;
		if (t > KP_CINE_MAX_RATE_TERM) t = KP_CINE_MAX_RATE_TERM + 1; /* past the limit either way */
		p2 = t * p1 + p0;
		q2 = t * q1 + q0;
		if (p2 > KP_CINE_MAX_RATE_TERM || q2 > KP_CINE_MAX_RATE_TERM) break;
		p0 = p1;
		q0 = q1;
		p1 = p2;
		q1 = q2;
		a = b;
		b = r;
	}
	*num = (uint32_t)p1;
	*den = (uint32_t)q1;
}

/* A WAV file of PCM samples (8 bits unsigned or 16 bits little endian, interleaved); NULL out of memory. */
static uint8_t *wav_file(const uint8_t *pcm, size_t size, const KpSmkSoundFormat *f, size_t *out_size)
{
	uint8_t *w;
	uint32_t block = (uint32_t)f->channels * (uint32_t)(f->bits / 8);
	if (size > 0xffffffffu - WAV_HEADER || !(w = (uint8_t *)malloc(WAV_HEADER + size))) return NULL;
	memcpy(w, "RIFF", 4);
	kp_le_put32(w + 4, (uint32_t)(36 + size));
	memcpy(w + 8, "WAVEfmt ", 8);
	kp_le_put32(w + 16, 16);
	kp_le_put16(w + 20, 1); /* PCM */
	kp_le_put16(w + 22, (uint16_t)f->channels);
	kp_le_put32(w + 24, f->rate);
	kp_le_put32(w + 28, f->rate * block);
	kp_le_put16(w + 32, (uint16_t)block);
	kp_le_put16(w + 34, (uint16_t)f->bits);
	memcpy(w + 36, "data", 4);
	kp_le_put32(w + 40, (uint32_t)size);
	if (size) memcpy(w + WAV_HEADER, pcm, size);
	*out_size = WAV_HEADER + size;
	return w;
}

KpCineMovie *kp_cine_movie_smk(uint8_t *data, size_t size, const KpCineClip *named, KpError *err)
{
	KpCineMovie *m = (KpCineMovie *)calloc(1, sizeof *m);
	KpSmkSoundFormat f;
	uint64_t t, g;
	int track;
	if (!m) {
		free(data);
		fail(err, "out of memory");
		return NULL;
	}
	m->bytes = data;
	m->size = size;
	if (kp_smk_open(&m->decoder, data, size) != 0 || m->decoder.kpw) {
		kp_cine_movie_free(m);
		fail(err, "not a Smacker movie");
		return NULL;
	}
	m->smk = 1;
	if (m->decoder.width > KP_CINE_MAX_SIDE || m->decoder.height > KP_CINE_MAX_SIDE) {
		kp_cine_movie_free(m);
		fail(err, "the movie is larger than the player takes");
		return NULL;
	}
	clip_named(&m->clip, named, m->decoder.width, m->decoder.height);
	t = kp_smk_frame_time(&m->decoder);
	if (t == 0) t = SMK_DEFAULT_TIME;
	g = gcd64(100000, t); /* 10 us units: 100,000 a second */
	rate_fraction(100000 / g, t / g, &m->clip.rate_num, &m->clip.rate_den);
	m->clip.frames = m->decoder.frames; /* a ring movie's extra frame leads back to its start: not shown here */
	for (track = 0; track < KP_SMK_TRACKS; track++) { /* the first sound track */
		uint8_t *pcm;
		size_t n;
		if (kp_smk_sound_format(&m->decoder, track, &f) != 0) continue;
		if (kp_smk_sound(&m->decoder, track, &f, &pcm, &n) != 0) {
			kp_cine_movie_free(m);
			fail(err, "the movie's sound does not decode");
			return NULL;
		}
		m->wav = wav_file(pcm, n, &f, &m->wav_size);
		free(pcm);
		if (!m->wav) {
			kp_cine_movie_free(m);
			fail(err, "out of memory");
			return NULL;
		}
		m->clip.audio_size = (uint32_t)(m->wav_size > 0xffffffffu ? 0xffffffffu : m->wav_size);
		break;
	}
	return m;
}

KpCineMovie *kp_cine_movie_webm(const uint8_t *data, size_t size, const KpCineClip *named, KpError *err)
{
	KpCineMovie *m;
	KpWebmStreams s;
	char why[160];
	uint64_t frame_ns, table, total;
	uint32_t n;
	if (kp_webm_streams(data, size, kp_crc32((const uint8_t *)named->name, strlen(named->name)), &s, why, sizeof why) != 0) {
		snprintf(err->msg, sizeof err->msg, "cinematics: %s", why);
		return NULL;
	}
	frame_ns = s.frame_ns;
	if (!frame_ns && s.count > 1 && s.times[s.count - 1] > s.times[0])
		frame_ns = (s.times[s.count - 1] - s.times[0]) / (s.count - 1);
	table = s.packets_size;
	total = table + 4ull * s.count + s.ogg_size;
	if (!frame_ns || s.packets_size > 0xffffffffu || s.ogg_size > 0xffffffffu || total > SIZE_MAX
		|| !(m = (KpCineMovie *)calloc(1, sizeof *m))) {
		kp_webm_streams_free(&s);
		fail(err, frame_ns ? "out of memory or a movie too large" : "the movie has no frame rate");
		return NULL;
	}
	if (!(m->bytes = (uint8_t *)malloc((size_t)total ? (size_t)total : 1))) {
		kp_webm_streams_free(&s);
		free(m);
		fail(err, "out of memory");
		return NULL;
	}
	m->size = (size_t)total;
	memcpy(m->bytes, s.packets, (size_t)s.packets_size); /* total, and so each part, fits a size_t */
	memcpy(m->bytes + table, s.sizes, 4u * (size_t)s.count);
	if (s.ogg_size) memcpy(m->bytes + table + 4ull * s.count, s.ogg, s.ogg_size);
	clip_named(&m->clip, named, s.width, s.height);
	rate_fraction(NS_PER_S, frame_ns, &m->clip.rate_num, &m->clip.rate_den);
	m->clip.frames = s.count;
	m->clip.video_offset = 0;
	m->clip.video_size = (uint32_t)s.packets_size;
	m->clip.frame_table = table;
	m->clip.audio_offset = table + 4ull * s.count;
	m->clip.audio_size = (uint32_t)s.ogg_size;
	for (n = 0; n < s.count; n++) { /* the pack keeps the rate, not the times: each within half a frame of it */
		double drift = (double)(s.times[n] - s.times[0]) / 1e9 * m->clip.rate_num / m->clip.rate_den - n;
		if (drift > 0.5 || drift < -0.5) {
			kp_webm_streams_free(&s);
			kp_cine_movie_free(m);
			snprintf(err->msg, sizeof err->msg, "cinematics: frame %u is %+.2f frames from its time", (unsigned)n, drift);
			return NULL;
		}
	}
	kp_webm_streams_free(&s);
	return m;
}

const KpCineClip *kp_cine_movie_clip(const KpCineMovie *m) { return &m->clip; }

const uint8_t *kp_cine_movie_sound(const KpCineMovie *m, size_t *size)
{
	if (m->smk) {
		*size = m->wav ? m->wav_size : 0;
		return m->wav;
	}
	*size = m->clip.audio_size;
	return m->clip.audio_size ? m->bytes + m->clip.audio_offset : NULL;
}

int kp_cine_movie_subtitles(KpCineMovie *m, const uint8_t *srt, size_t size, KpError *err)
{
	int rc = kp_cine_subs_srt(srt, size, &m->subs, err);
	m->clip.cue_count = (uint32_t)m->subs.count;
	return rc;
}

const KpCineSubs *kp_cine_movie_subs(const KpCineMovie *m) { return &m->subs; }

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
	KpSmk *smk; /* an SMK movie's decoder (the movie's), else the VP9 packets are the clip's */
	uint8_t *planes; /* its picture: G, B and R planes of the clip's size */
	int smk_picture; /* planes hold the last KP_CINE_FRAME's picture */
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

/* Whether the picture is one the player shows (kp_vpx_usable). */
static int check_image(const KpCinePlayer *p, const vpx_image_t *im, KpError *err)
{
	if (!kp_vpx_usable(im, p->clip->width, p->clip->height))
		return fail(err, "a picture is not 8-bit planar YCbCr of the clip's size");
	return 0;
}

/* A player of a clip's VP9 packets read through read. */
static KpCinePlayer *player_open(const KpCineClip *clip, KpCineRead read, void *ctx, KpError *err)
{
	KpCinePlayer *p;
	vpx_codec_dec_cfg_t cfg;
	uint64_t sum = 0;
	uint32_t n;
	p = (KpCinePlayer *)calloc(1, sizeof *p);
	if (!p) {
		fail(err, "out of memory");
		return NULL;
	}
	p->clip = clip;
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

KpCinePlayer *kp_cine_open(const KpCineIndex *ix, int k, KpCineRead read, void *ctx, KpError *err)
{
	if (k < 0 || k >= ix->count) {
		fail(err, "no such clip");
		return NULL;
	}
	return player_open(&ix->clips[k], read, ctx, err);
}

KpCinePlayer *kp_cine_open_movie(KpCineMovie *m, KpError *err)
{
	KpCinePlayer *p;
	if (!m->smk) return player_open(&m->clip, kp_cine_movie_read, m, err);
	p = (KpCinePlayer *)calloc(1, sizeof *p);
	if (!p || !(p->planes = (uint8_t *)malloc(3u * m->clip.width * m->clip.height))) {
		free(p);
		fail(err, "out of memory");
		return NULL;
	}
	p->clip = &m->clip;
	p->smk = &m->decoder;
	kp_smk_rewind(p->smk);
	return p;
}

void kp_cine_free(KpCinePlayer *p)
{
	if (!p) return;
	if (p->codec_open) vpx_codec_destroy(&p->codec);
	free(p->sizes);
	free(p->window);
	free(p->rgba);
	free(p->planes);
	free(p);
}

/* --- the player of a Smacker movie: kp_smk's frames through their palettes as G, B, R planes --- */

/* The frame decoded last as its picture's planes */
static void smk_planes(KpCinePlayer *p)
{
	const KpSmk *s = p->smk;
	size_t n = (size_t)s->width * s->height, i;
	uint8_t *g = p->planes, *b = g + n, *r = b + n;
	for (i = 0; i < n; i++) {
		const uint8_t *c = s->palette + 3 * (size_t)s->pixels[i];
		r[i] = c[0];
		g[i] = c[1];
		b[i] = c[2];
	}
}

/* kp_cine_advance of a Smacker movie: the frames due decoded (a few a call), the last one's picture kept. */
static int smk_advance(KpCinePlayer *p, uint64_t time_ms, KpError *err)
{
	const KpCineClip *c = p->clip;
	uint64_t due = time_ms * c->rate_num / (1000ull * c->rate_den) + 1;
	int budget = MAX_PACKETS_PER_CALL, got = 0;
	if (due > c->frames) due = c->frames;
	while (p->next < due && budget-- > 0) {
		if (kp_smk_decode(p->smk) != 0) {
			snprintf(err->msg, sizeof err->msg, "cinematics: frame %u does not decode", (unsigned)p->next);
			return -1;
		}
		p->next++;
		got = 1;
	}
	if (got) {
		smk_planes(p);
		p->smk_picture = 1;
		p->rgba_done = 0;
		return KP_CINE_FRAME;
	}
	if (p->next >= c->frames && time_ms >= kp_cine_frame_ms(c, c->frames)) return KP_CINE_END;
	return KP_CINE_IDLE;
}

int kp_cine_advance(KpCinePlayer *p, uint64_t time_ms, KpError *err)
{
	const KpCineClip *c = p->clip;
	uint64_t due = time_ms * c->rate_num / (1000ull * c->rate_den) + 1; /* frame 0 is due at 0 */
	int budget = MAX_PACKETS_PER_CALL, got = 0, shown = 1, waiting = 0;
	if (p->smk) return smk_advance(p, time_ms, err);
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
	if (p->smk) {
		size_t n = (size_t)p->clip->width * p->clip->height;
		int k;
		if (!p->smk_picture) return -1;
		memset(out, 0, sizeof *out);
		for (k = 0; k < 3; k++) {
			out->planes[k] = p->planes + (size_t)k * n;
			out->strides[k] = p->clip->width;
		}
		out->width = out->chroma_w = p->clip->width;
		out->height = out->chroma_h = p->clip->height;
		out->srgb = 1;
		out->y_scale = 1.0f;
		return 0;
	}
	if (!p->image) return -1;
	kp_vpx_picture(p->image, out);
	return 0;
}

const uint8_t *kp_cine_rgba(KpCinePlayer *p)
{
	size_t n = (size_t)p->clip->width * p->clip->height, i;
	if (p->smk ? !p->smk_picture : !p->image) return NULL;
	if (!p->rgba && !(p->rgba = (uint8_t *)malloc(4 * n))) return NULL;
	if (!p->rgba_done) {
		if (!p->smk) {
			kp_vpx_rgba(p->image, p->rgba);
		} else {
			for (i = 0; i < n; i++) { /* the planes are G, B, R */
				p->rgba[4 * i] = p->planes[2 * n + i];
				p->rgba[4 * i + 1] = p->planes[i];
				p->rgba[4 * i + 2] = p->planes[n + i];
				p->rgba[4 * i + 3] = 255;
			}
		}
	}
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

/* The run's end: its file closed (none for an install's movies), the host's input its own again. */
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
		if (k >= 0 && !r->index.original
			&& (r->player = kp_cine_open(&r->index, k, r->host.read, r->file, &err)) != NULL)
			return 1;
		if (k >= 0 && r->index.original && (r->movie = r->host.load(r->host.ctx, &r->index.clips[k], &err)) != NULL) {
			if ((r->player = kp_cine_open_movie(r->movie, &err)) != NULL) return 1;
			kp_cine_movie_free(r->movie);
			r->movie = NULL;
		}
		say(r, "cinematics: %d: %s", id, k < 0 ? "not in the pack" : err.msg);
	}
	return 0;
}

/* The movie opened begins: its sound and its clock. */
static void begin_movie(KpCineRun *r)
{
	const KpCineClip *clip = kp_cine_clip(r->player);
	size_t sound_size = 0;
	const uint8_t *sound = r->movie ? kp_cine_movie_sound(r->movie, &sound_size) : NULL;
	int audio = clip->audio_size > 0 && r->host.sound(r->host.ctx, clip, sound, sound_size);
	KpError err;
	kp_cine_clock_begin(&r->clock, now_ms(r), audio);
	r->begun_ms = now_ms(r);
	r->drawn_ns = 0;
	r->press_ns = 0;
	r->shown = 0;
	r->time_ms = 0;
	r->waiting = 0;
	r->movies++;
	if (r->movie) r->subs = *kp_cine_movie_subs(r->movie);
	else if (kp_cine_subs_load(&r->index, kp_cine_find(&r->index, clip->id), r->host.read, r->file, &r->subs, &err) != 0)
		say(r, "%s: %s plays without them", err.msg, clip->name);
	say(r, "cinematics: %s %dx%d, %u frames", clip->name, clip->width, clip->height, (unsigned)clip->frames);
}

/* The movie playing ended (its sound released before its bytes): the next of the queue begins, or the run ends. */
static void end_movie(KpCineRun *r)
{
	if (r->player) {
		const KpCineClip *clip = kp_cine_clip(r->player);
		say(r, "cinematics: %s ended after %llu ms of the wall, %llu of its own; %u of %u frames decoded, %u pictures shown",
			clip->name, (unsigned long long)(now_ms(r) - r->begun_ms), (unsigned long long)r->clock.last_ms,
			(unsigned)kp_cine_decoded(r->player), (unsigned)clip->frames, (unsigned)r->shown);
	}
	r->host.sound_stop(r->host.ctx);
	kp_cine_free(r->player);
	kp_cine_movie_free(r->movie);
	r->player = NULL;
	r->movie = NULL;
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
	if (!r->index.original && !(r->file = r->host.open(r->host.ctx))) return 0;
	r->queue_pos = -1;
	if (!open_next(r)) { /* none of them opens: nothing changed */
		if (r->file) r->host.close(r->host.ctx, r->file);
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
