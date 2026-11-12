/* The cinematics' movies, their player and a run of them (kp_cine.h). */
#include "kp_cine.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kp_smk.h"
#include "kp_utf8.h"
#include "kp_webm.h"
#include "vpx/vp8dx.h"
#include "vpx/vpx_decoder.h"

#define KEY_BIT 0x80000000u
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

/* --- the profile's movies (CINM) --- */

#define CINM_DIGEST 32

const char *const KP_CINE_WEBM_FOLDER[2] = { "SD/smk/", "HD2/Smk/" };

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

int kp_cine_catalog_check(const KpCineCatalog *c, int k, size_t size)
{
	return k >= 0 && k < c->count && size == c->movies[k].size ? 0 : -1;
}

void kp_cine_index_movies(KpCineIndex *ix, int variant, const KpCineCatalog *c, int (*has)(void *ctx, int k), void *ctx)
{
	int k;
	memset(ix, 0, sizeof *ix);
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

/* --- SubRip subtitles --- */

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

/* --- the movies (ADR 0018, stage D7; ADR 0019, stage C4) --- */

#define SMK_SHOWN_CODED 320 /* a movie coded 320 wide is shown 640 wide */
#define SMK_SHOWN_WIDTH 640
#define SMK_DEFAULT_TIME 10000u /* a frame's time when the header gives none: 10 frames a second (10 us units) */
#define WAV_HEADER 44

struct KpCineMovie {
	KpCineClip clip;      /* the movie's: the index's name and number, its own size, rate, frames and sound */
	uint8_t *bytes;       /* an SMK's file (its frames are decoded from it) */
	size_t size;
	int smk;              /* an SMK: kp_smk plays it */
	KpSmk decoder;
	KpWebmStreams webm;   /* a WebM's packets, frame table and Ogg Vorbis */
	int webm_open;
	uint8_t *wav;         /* an SMK's sound as a WAV file, NULL with none */
	size_t wav_size;
	KpCineSubs subs;      /* its subtitles (kp_cine_movie_subtitles), none at first */
};

void kp_cine_movie_free(KpCineMovie *m)
{
	if (!m) return;
	if (m->smk) kp_smk_close(&m->decoder);
	if (m->webm_open) kp_webm_streams_free(&m->webm);
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
	KpCineMovie *m = (KpCineMovie *)calloc(1, sizeof *m);
	KpWebmStreams *s;
	char why[160];
	uint64_t frame_ns;
	uint32_t n;
	if (!m) {
		fail(err, "out of memory");
		return NULL;
	}
	s = &m->webm;
	if (kp_webm_streams(data, size, kp_crc32((const uint8_t *)named->name, strlen(named->name)), s, why, sizeof why) != 0) {
		free(m);
		snprintf(err->msg, sizeof err->msg, "cinematics: %s", why);
		return NULL;
	}
	m->webm_open = 1;
	frame_ns = s->frame_ns;
	if (!frame_ns && s->count > 1 && s->times[s->count - 1] > s->times[0])
		frame_ns = (s->times[s->count - 1] - s->times[0]) / (s->count - 1);
	if (!frame_ns || s->packets_size > 0xffffffffu || s->ogg_size > 0xffffffffu) {
		kp_cine_movie_free(m);
		fail(err, frame_ns ? "a movie too large" : "the movie has no frame rate");
		return NULL;
	}
	clip_named(&m->clip, named, s->width, s->height);
	rate_fraction(NS_PER_S, frame_ns, &m->clip.rate_num, &m->clip.rate_den);
	m->clip.frames = s->count;
	m->clip.audio_size = (uint32_t)s->ogg_size;
	for (n = 0; n < s->count; n++) { /* the player keeps the rate, not the times: each within half a frame of it */
		double drift = (double)(s->times[n] - s->times[0]) / 1e9 * m->clip.rate_num / m->clip.rate_den - n;
		if (drift > 0.5 || drift < -0.5) {
			kp_cine_movie_free(m);
			snprintf(err->msg, sizeof err->msg, "cinematics: frame %u is %+.2f frames from its time", (unsigned)n, drift);
			return NULL;
		}
	}
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
	return m->clip.audio_size ? m->webm.ogg : NULL;
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
	vpx_codec_ctx_t codec;
	int codec_open;
	const uint8_t *packets; /* a WebM's, one after the other (the movie's) */
	uint32_t *sizes; /* the frame table, one u32 a frame */
	uint64_t packet_at; /* the next packet's offset in the packets */
	uint32_t next; /* the next frame to decode */
	const vpx_image_t *image; /* the picture of the last KP_CINE_FRAME (NULL: a packet decoded since gave none) */
	uint8_t *rgba; /* image as RGBA once kp_cine_rgba asked for it */
	int rgba_done;
	KpSmk *smk; /* an SMK movie's decoder (the movie's), else the VP9 packets are the WebM's */
	uint8_t *planes; /* its picture: G, B and R planes of the clip's size */
	int smk_picture; /* planes hold the last KP_CINE_FRAME's picture */
};

/* Whether the picture is one the player shows (kp_vpx_usable). */
static int check_image(const KpCinePlayer *p, const vpx_image_t *im, KpError *err)
{
	if (!kp_vpx_usable(im, p->clip->width, p->clip->height))
		return fail(err, "a picture is not 8-bit planar YCbCr of the clip's size");
	return 0;
}

/* A player of a WebM movie's VP9 packets. */
static KpCinePlayer *player_open(KpCineMovie *m, KpError *err)
{
	const KpWebmStreams *s = &m->webm;
	KpCinePlayer *p = (KpCinePlayer *)calloc(1, sizeof *p);
	vpx_codec_dec_cfg_t cfg;
	uint64_t sum = 0;
	uint32_t n;
	if (!p || !(p->sizes = (uint32_t *)malloc(4u * (s->count ? s->count : 1)))) {
		kp_cine_free(p);
		fail(err, "out of memory");
		return NULL;
	}
	p->clip = &m->clip;
	p->packets = s->packets;
	for (n = 0; n < s->count; n++) {
		p->sizes[n] = kp_le_u32(s->sizes + 4u * n);
		sum += p->sizes[n] & ~KEY_BIT;
		if ((p->sizes[n] & ~KEY_BIT) == 0) break; /* libvpx refuses an empty packet */
	}
	if (s->count == 0 || n < s->count || sum != s->packets_size || !(p->sizes[0] & KEY_BIT)) {
		kp_cine_free(p);
		fail(err, "the frame table does not fit the packets (no frame, an empty packet, or a first frame that is not a "
			"key frame)");
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

KpCinePlayer *kp_cine_open_movie(KpCineMovie *m, KpError *err)
{
	KpCinePlayer *p;
	if (!m->smk) return player_open(m, err);
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
	int budget = MAX_PACKETS_PER_CALL, got = 0, shown = 1;
	if (p->smk) return smk_advance(p, time_ms, err);
	if (due > c->frames) due = c->frames;
	/* a packet that shows no picture (a hidden frame not joined into a superframe) where the clock wants one: the
	 * picture the decoder gave before it is gone with its buffers, so the packets after it are decoded until one
	 * shows, within the budget */
	while (p->next < c->frames && budget > 0 && (p->next < due || !shown)) {
		uint32_t size = p->sizes[p->next] & ~KEY_BIT;
		vpx_codec_iter_t it = NULL;
		const vpx_image_t *im;
		budget--;
		if (vpx_codec_decode(&p->codec, p->packets + p->packet_at, size, NULL, 0) != VPX_CODEC_OK) {
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

/* The run's end: the host's input its own again. */
static void finish(KpCineRun *r)
{
	r->active = 0;
	r->loading = 0;
	r->host.end(r->host.ctx);
}

/* What asking for a movie gave */
typedef enum {
	MOVIE_NONE,   /* it does not load or play (logged) */
	MOVIE_OPENED, /* its player */
	MOVIE_COMING  /* it is on its way (the host's KP_CINE_LOAD_WAIT) */
} MovieAsked;

/* The movie at queue_pos asked for. */
static MovieAsked load_movie(KpCineRun *r)
{
	int id = r->queue[r->queue_pos], k = kp_cine_find(&r->index, id), rc;
	KpError err;
	if (k < 0) {
		say(r, "cinematics: %d: not in the data", id);
		return MOVIE_NONE;
	}
	rc = r->host.load(r->host.ctx, &r->index.clips[k], &r->movie, &err);
	if (rc == KP_CINE_LOAD_WAIT) return MOVIE_COMING;
	if (rc == 0 && (r->player = kp_cine_open_movie(r->movie, &err)) != NULL) return MOVIE_OPENED;
	kp_cine_movie_free(r->movie);
	r->movie = NULL;
	say(r, "cinematics: %d: %s", id, err.msg);
	return MOVIE_NONE;
}

/* The next movie of the queue that loads, from queue_pos + 1 (the ones that do not are logged; loading set while it is
 * on its way); MOVIE_NONE when none is left. */
static MovieAsked open_next(KpCineRun *r)
{
	for (r->queue_pos++; r->have && r->queue_pos < r->queue_len; r->queue_pos++) {
		MovieAsked got = load_movie(r);
		r->loading = got == MOVIE_COMING;
		if (got != MOVIE_NONE) return got;
	}
	return MOVIE_NONE;
}

/* The movie opened begins: its sound and its clock. */
static void begin_movie(KpCineRun *r)
{
	const KpCineClip *clip = kp_cine_clip(r->player);
	size_t sound_size = 0;
	const uint8_t *sound = kp_cine_movie_sound(r->movie, &sound_size);
	int audio = sound != NULL && r->host.sound(r->host.ctx, clip, sound, sound_size);
	kp_cine_clock_begin(&r->clock, now_ms(r), audio);
	r->begun_ms = now_ms(r);
	r->drawn_ns = 0;
	r->press_ns = 0;
	r->shown = 0;
	r->time_ms = 0;
	r->movies++;
	r->subs = *kp_cine_movie_subs(r->movie);
	say(r, "cinematics: %s %dx%d, %u frames", clip->name, clip->width, clip->height, (unsigned)clip->frames);
}

/* A movie on its way: no picture until it has come, a press after now ends it (not one of this moment: the other
 * finger or button of the press that ended the movie before it). */
static void wait_movie(KpCineRun *r)
{
	r->drawn_ns = 0;
	r->press_ns = r->host.now_ns(r->host.ctx) + 1;
}

/* The movie playing ended (its sound released before its bytes): the next of the queue begins, or the run ends. */
static void end_movie(KpCineRun *r)
{
	MovieAsked next;
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
	if ((next = open_next(r)) == MOVIE_OPENED) begin_movie(r);
	else if (next == MOVIE_COMING) wait_movie(r);
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
	MovieAsked first;
	if (!r->have) return 0;
	if (r->active) return enqueue(r, ids, n, preludes) > 0; /* after the ones playing */
	r->queue_len = 0;
	if (enqueue(r, ids, n, preludes) == 0) return 0;
	r->queue_pos = -1;
	if ((first = open_next(r)) == MOVIE_NONE) return 0; /* none of them loads: nothing changed */
	r->host.begin(r->host.ctx);
	r->active = 1;
	if (first == MOVIE_OPENED) begin_movie(r);
	else wait_movie(r);
	return 1;
}

int kp_cine_run_frame(KpCineRun *r, KpVideoPicture *pic)
{
	KpError err;
	int rc;
	if (!r->active) return 0;
	if (r->loading) { /* asked again until it has come */
		MovieAsked got = load_movie(r);
		if (got == MOVIE_COMING) return 0;
		r->loading = 0;
		if (got == MOVIE_OPENED) begin_movie(r);
		else end_movie(r);
		return 0;
	}
	if (!r->player) return 0;
	r->time_ms = movie_ms(r);
	rc = kp_cine_advance(r->player, r->time_ms, &err);
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
