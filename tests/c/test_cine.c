/*
 * The cinematics' pack and its player (native/kp_app/kp_cine.c, ADR 0015): the index, the
 * colour, the frames of a pack the converter writes (converter.kpf.fixtures: a 4:4:4 sRGB movie as the SD ones are and a
 * 4:2:0 studio-range one as the HD2 ones, 64 x 48, 12 frames, each with the frames FFmpeg decodes from the same WebM) and
 * the refusals of a pack or a packet that is not right; a run of its movies over a host that records what it is asked.
 */
#include "kp_test.h"

#include "kp_cine.h"
#include "kp_webm.h"
#include "kp_kpf.h"

#include <math.h>
#include <stddef.h>

#define PACK "tests/fixtures/generated/kpf/cine.kpd"
#define FRAMES_SRGB "tests/fixtures/generated/kpf/cine0.rgba"
#define FRAMES_YCC "tests/fixtures/generated/kpf/cine1.rgba"
#define PLANES_YCC "tests/fixtures/generated/kpf/cine1.yuv"
#define W 64
#define H 48
#define FRAME_BYTES (W * H * 4)
#define FRAME_COUNT 12
#define YCC_MEAN_LIMIT 4 /* FFmpeg upsamples the chroma with a filter, the player copies a sample: the mean of the error */

static const char *const SRGB_FILES[] = { PACK, FRAMES_SRGB, NULL };
static const char *const YCC_FILES[] = { PACK, FRAMES_YCC, NULL };
static const char *const YCC_PLANE_FILES[] = { PACK, PLANES_YCC, NULL };

typedef struct {
	const uint8_t *data;
	size_t size;
	int reads;
	int fail_read; /* this read (from 1) fails; 0 none */
} Memory;

static int read_memory(void *ctx, uint64_t offset, uint8_t *out, size_t size)
{
	Memory *m = (Memory *)ctx;
	m->reads++;
	if (m->fail_read != 0 && m->reads == m->fail_read) return -1;
	if (offset > m->size || size > m->size - offset) return -1;
	memcpy(out, m->data + offset, size);
	return 0;
}

static void put_u32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
	p[2] = (uint8_t)(v >> 16);
	p[3] = (uint8_t)(v >> 24);
}

static int index_of(const uint8_t *data, size_t size, Memory *m, KpCineIndex *ix, KpError *err)
{
	m->data = data;
	m->size = size;
	m->reads = 0;
	m->fail_read = 0;
	return kp_cine_index(ix, data, size < KP_CINE_HEAD ? size : KP_CINE_HEAD, size, read_memory, m, err);
}

/* A copy of the pack the test may damage. */
static uint8_t *copy_of(KpT *T, const uint8_t *data, size_t size)
{
	uint8_t *copy = (uint8_t *)malloc(size);
	if (!copy) kp_t_fail(T, "out of memory");
	kp_t_defer(T, free, copy);
	memcpy(copy, data, size);
	return copy;
}

static const KpKpfSection *section(KpT *T, KpKpf *c, const uint8_t *data, size_t size, const char *name)
{
	KpError err;
	const KpKpfSection *s;
	if (kp_kpf_parse_prefix(c, data, size < KP_CINE_HEAD ? size : KP_CINE_HEAD, size, "cinematics", &err) != 0)
		kp_t_fail(T, "%s", err.msg);
	s = kp_kpf_find(c, name);
	if (!s) kp_t_fail(T, "no %s", name);
	return s;
}

static void index_follows_the_pack(KpT *T)
{
	size_t size;
	const uint8_t *pack;
	Memory m;
	KpCineIndex ix;
	KpError err;
	if (!kp_t_has(T, PACK)) {
		kp_t_log(T, "SKIP: no %s (python3 -m converter.kpf.fixtures)", PACK);
		return;
	}
	pack = kp_t_read(T, PACK, &size);
	kp_t_eq(T, index_of(pack, size, &m, &ix, &err), 0, "%s", err.msg);
	kp_t_eq(T, ix.variant, 0, "an SD pack");
	kp_t_eq(T, ix.count, 2, "two clips");
	kp_t_eq(T, kp_cine_find(&ix, 0), 0, NULL);
	kp_t_eq(T, kp_cine_find(&ix, 1), 1, NULL);
	kp_t_eq(T, kp_cine_find(&ix, 5), -1, "a cinematic the pack lacks");
	kp_t_eq_str(T, ix.clips[0].name, "smk\\blizzard", NULL);
	kp_t_eq_str(T, ix.clips[1].name, "smk\\starintr", NULL);
	kp_t_eq(T, ix.clips[0].width, W, NULL);
	kp_t_eq(T, ix.clips[0].shown_w, W, "a movie not coded 320 wide is shown as coded");
	kp_t_eq(T, ix.clips[0].shown_h, H, NULL);
	kp_t_eq(T, ix.clips[0].rate_num, 15, NULL);
	kp_t_eq(T, ix.clips[0].rate_den, 1, NULL);
	kp_t_eq(T, ix.clips[0].frames, FRAME_COUNT, NULL);
	kp_t_ok(T, ix.clips[0].audio_size > 0 && memcmp(pack + ix.clips[0].audio_offset, "OggS", 4) == 0, "the sound is an Ogg stream");
	kp_t_ok(T, ix.clips[1].video_offset >= ix.clips[0].audio_offset + ix.clips[0].audio_size, "the clips do not overlap");
	kp_t_ok(T, ix.prelude_count > 0 && kp_cine_prelude(&ix, ix.preludes[0][0]) >= 0, "the preludes' table");
	kp_t_eq_str(T, ix.subtitles, "enUS", "the subtitles' locale");
	kp_t_eq(T, ix.clips[0].cue_count, 0, "blizzard has no subtitles");
	kp_t_eq(T, ix.clips[1].cue_first, 0, NULL);
	kp_t_eq(T, ix.clips[1].cue_count, 2, "starintr has two");
}

/* A clip's subtitles from the pack (converter.kpf.fixtures' CINE_SUBTITLES) and the one shown at each time: none
 * before the first, each from its start to its end (excluded), none after the last; one that outlasts the next is
 * shown again when the next ends. */
static void subtitles_follow_the_movies_time(KpT *T)
{
	size_t size;
	const uint8_t *pack;
	Memory m;
	KpCineIndex ix;
	KpCineSubs *subs = (KpCineSubs *)calloc(1, sizeof *subs);
	KpError err;
	if (!subs) kp_t_fail(T, "out of memory");
	kp_t_defer(T, free, subs);
	if (!kp_t_has(T, PACK)) return;
	pack = kp_t_read(T, PACK, &size);
	kp_t_eq(T, index_of(pack, size, &m, &ix, &err), 0, "%s", err.msg);
	kp_t_eq(T, kp_cine_subs_load(&ix, 0, read_memory, &m, subs, &err), 0, "%s", err.msg);
	kp_t_eq(T, subs->count, 0, "a clip without subtitles");
	kp_t_eq(T, kp_cine_subs_at(subs, 0), -1, NULL);
	kp_t_eq(T, kp_cine_subs_load(&ix, 1, read_memory, &m, subs, &err), 0, "%s", err.msg);
	kp_t_eq(T, subs->count, 2, NULL);
	kp_t_ok(T, subs->cues[0].start_ms == 100 && subs->cues[0].end_ms == 300, "the first's times");
	kp_t_ok(T, subs->cues[1].size == strlen("Вторая\nстрока")
		&& memcmp(subs->text + subs->cues[1].at, "Вторая\nстрока", subs->cues[1].size) == 0, "the second's lines");
	kp_t_ok(T, subs->cues[0].size == strlen("Первая") && memcmp(subs->text + subs->cues[0].at, "Первая", 12) == 0, NULL);
	kp_t_eq(T, kp_cine_subs_at(subs, 99), -1, "before the first");
	kp_t_eq(T, kp_cine_subs_at(subs, 100), 0, NULL);
	kp_t_eq(T, kp_cine_subs_at(subs, 299), 0, NULL);
	kp_t_eq(T, kp_cine_subs_at(subs, 300), 1, "the first's end is the second's start");
	kp_t_eq(T, kp_cine_subs_at(subs, 699), 1, NULL);
	kp_t_eq(T, kp_cine_subs_at(subs, 700), -1, "after the last");
	subs->cues[0].end_ms = 900; /* the first outlasts the second */
	kp_t_eq(T, kp_cine_subs_at(subs, 500), 1, "the one begun last");
	kp_t_eq(T, kp_cine_subs_at(subs, 800), 0, "the first again once the second ended");
}

/* A language's SUBS over the pack's (ADR 0016): one row per movie of the full table, the clips find theirs by id. */
static void subtitles_of_a_language_replace_the_packs(KpT *T)
{
	static const char TEXT[] = "\xd0\xa2\xd0\xb5\xd0\xba\xd1\x81\xd1\x82";
	enum { CLIPS = 3, CUE = 16, HEAD = 12, ROW = 8, TEXT_SIZE = sizeof TEXT - 1 };
	uint8_t subs_section[HEAD + CLIPS * ROW + CUE + TEXT_SIZE], *row = subs_section + HEAD, *cue = row + CLIPS * ROW;
	size_t size;
	const uint8_t *pack;
	Memory m;
	KpCineIndex ix;
	KpCineSubs *subs = (KpCineSubs *)calloc(1, sizeof *subs);
	KpError err;
	if (!subs) kp_t_fail(T, "out of memory");
	kp_t_defer(T, free, subs);
	if (!kp_t_has(T, PACK)) return;
	pack = kp_t_read(T, PACK, &size);
	kp_t_eq(T, index_of(pack, size, &m, &ix, &err), 0, "%s", err.msg);
	memset(subs_section, 0, sizeof subs_section);
	memcpy(subs_section, "ruRU", 4);
	put_u32(subs_section + 4, 1);
	put_u32(subs_section + 8, TEXT_SIZE);
	put_u32(row + ROW, 0); /* clip 1: the only cue */
	put_u32(row + ROW + 4, 1);
	put_u32(row + 2 * ROW, 1); /* the table's last, past the pack's clips */
	put_u32(cue, 50);
	put_u32(cue + 4, 250);
	put_u32(cue + 12, TEXT_SIZE);
	memcpy(cue + CUE, TEXT, TEXT_SIZE);
	kp_t_eq(T, kp_cine_subs_overlay(&ix, subs_section, sizeof subs_section, &err), 0, "%s", err.msg);
	kp_t_eq_str(T, ix.subtitles, "ruRU", "the language's");
	kp_t_eq(T, ix.clips[0].cue_count, 0, "clip 0's row");
	kp_t_eq(T, ix.clips[1].cue_count, 1, "clip 1's row");
	kp_t_eq(T, kp_cine_subs_load(&ix, 1, read_memory, &m, subs, &err), 0, "%s", err.msg);
	kp_t_eq(T, subs->count, 1, NULL);
	kp_t_ok(T, subs->cues[0].start_ms == 50 && subs->cues[0].end_ms == 250, "its times");
	kp_t_ok(T, subs->cues[0].size == TEXT_SIZE && memcmp(subs->text + subs->cues[0].at, TEXT, TEXT_SIZE) == 0, "its text");
	/* refusals */
	{
		static const struct { size_t at; uint32_t value; const char *why; } BAD[] = {
			{ 8, 1000, "does not add up" }, /* more text than the section has */
			{ HEAD + ROW, 1, "out of place" },
			{ 4, 2, "cues do not add up" }, /* a cue more: its record leaves a row less, whose cues are fewer */
		};
		size_t k;
		for (k = 0; k < sizeof BAD / sizeof BAD[0]; k++) {
			uint8_t bad[sizeof subs_section];
			memcpy(bad, subs_section, sizeof bad);
			put_u32(bad + BAD[k].at, BAD[k].value);
			kp_t_eq(T, kp_cine_subs_overlay(&ix, bad, sizeof bad, &err), -1, "%s", BAD[k].why);
			kp_t_ok(T, strstr(err.msg, BAD[k].why) != NULL, "%s", err.msg);
		}
		/* a row less than the pack's clips: only clip 0's */
		put_u32(subs_section + 4, 0);
		put_u32(subs_section + 8, 0);
		memset(row, 0, ROW);
		kp_t_eq(T, kp_cine_subs_overlay(&ix, subs_section, HEAD + ROW, &err), -1, "no row for a clip");
		kp_t_ok(T, strstr(err.msg, "no row") != NULL, "%s", err.msg);
		kp_t_eq(T, kp_cine_subs_overlay(&ix, subs_section, 4, &err), -1, "a cut head");
		kp_t_eq(T, kp_cine_subs_overlay(&ix, NULL, 0, &err), -1, "none");
	}
	/* a refusal leaves what was read */
	kp_t_eq(T, ix.clips[1].cue_count, 1, "clip 1's row stays");
}

/* A pack of schema 1 (before SUBS) is read with no subtitles; a SUBS that does not add up, or cues out of order, are
 * refused. */
static void subtitles_refused_when_not_right(KpT *T)
{
	size_t size;
	const uint8_t *pack;
	uint8_t *bad;
	KpKpf c;
	const KpKpfSection *s;
	uint64_t at;
	Memory m;
	KpCineIndex ix;
	KpCineSubs *subs = (KpCineSubs *)calloc(1, sizeof *subs);
	KpError err;
	if (!subs) kp_t_fail(T, "out of memory");
	kp_t_defer(T, free, subs);
	if (!kp_t_has(T, PACK)) return;
	pack = kp_t_read(T, PACK, &size);
	bad = copy_of(T, pack, size);
	put_u32(bad + 16, 1); /* the header's schema */
	kp_t_eq(T, index_of(bad, size, &m, &ix, &err), 0, "%s", err.msg);
	kp_t_ok(T, ix.subtitles[0] == 0 && ix.clips[1].cue_count == 0, "schema 1: no subtitles");
	kp_t_eq(T, kp_cine_subs_load(&ix, 1, read_memory, &m, subs, &err), 0, NULL);
	kp_t_eq(T, subs->count, 0, NULL);
	put_u32(bad + 16, KP_CINE_SCHEMA + 1);
	kp_t_eq(T, index_of(bad, size, &m, &ix, &err), -1, "a later schema");
	kp_t_ok(T, strstr(err.msg, "schema") != NULL, "%s", err.msg);
	memcpy(bad, pack, size);
	s = section(T, &c, bad, size, "SUBS");
	at = s->offset;
	kp_kpf_free(&c);
	bad[at + 8]++; /* a text byte more than SUBS holds */
	kp_t_eq(T, index_of(bad, size, &m, &ix, &err), -1, "a SUBS whose size does not add up");
	memcpy(bad, pack, size);
	bad[at + 1] = 0; /* "e\0US" */
	kp_t_eq(T, index_of(bad, size, &m, &ix, &err), -1, "a locale cut short");
	kp_t_ok(T, strstr(err.msg, "locale") != NULL, "%s", err.msg);
	bad[at + 1] = '^'; /* between the capitals and the small letters */
	kp_t_eq(T, index_of(bad, size, &m, &ix, &err), -1, "a locale of other bytes than letters");
	memcpy(bad, pack, size);
	bad[at + 2] = 'u'; /* "enuS" */
	kp_t_eq(T, index_of(bad, size, &m, &ix, &err), -1, "a locale whose region is not in capitals");
	memcpy(bad, pack, size);
	put_u32(bad + at + 12 + 8, 1); /* the second clip's cues from 1, after none */
	kp_t_eq(T, index_of(bad, size, &m, &ix, &err), -1, "a clip's cues out of place");
	memcpy(bad, pack, size);
	put_u32(bad + at + 12 + 16 + 16, 50); /* the second cue starts before the first */
	kp_t_eq(T, index_of(bad, size, &m, &ix, &err), 0, "%s", err.msg);
	kp_t_eq(T, kp_cine_subs_load(&ix, 1, read_memory, &m, subs, &err), -1, "cues out of order");
	kp_t_eq(T, subs->count, 0, "and none kept");
	memcpy(bad, pack, size);
	put_u32(bad + at + 12 + 16 + 8, 1000); /* the first's text past SUBS's */
	kp_t_eq(T, index_of(bad, size, &m, &ix, &err), 0, "%s", err.msg);
	kp_t_eq(T, kp_cine_subs_load(&ix, 1, read_memory, &m, subs, &err), -1, "a text past SUBS");
}

static void prelude_is_the_last_entry(KpT *T)
{
	KpCineIndex ix;
	memset(&ix, 0, sizeof ix);
	ix.preludes[0][0] = 3;
	ix.preludes[0][1] = 2;
	ix.preludes[1][0] = 3;
	ix.preludes[1][1] = 5;
	ix.preludes[2][0] = 7;
	ix.preludes[2][1] = 1;
	ix.prelude_count = 3;
	kp_t_eq(T, kp_cine_prelude(&ix, 3), 5, "PlayCinematicWithPrelude scans its table from the last entry");
	kp_t_eq(T, kp_cine_prelude(&ix, 7), 1, NULL);
	kp_t_eq(T, kp_cine_prelude(&ix, 9), -1, NULL);
}

static void frames_are_due_at_their_times(KpT *T)
{
	KpCineClip c;
	memset(&c, 0, sizeof c);
	c.rate_num = 15;
	c.rate_den = 1;
	kp_t_eq(T, (int64_t)kp_cine_frame_ms(&c, 0), 0, NULL);
	kp_t_eq(T, (int64_t)kp_cine_frame_ms(&c, 1), 67, "rounded up");
	kp_t_eq(T, (int64_t)kp_cine_frame_ms(&c, 15), 1000, NULL);
	c.rate_num = 30000;
	c.rate_den = 1001;
	kp_t_eq(T, (int64_t)kp_cine_frame_ms(&c, 30), 1001, "the NTSC rate");
	kp_t_eq(T, (int64_t)kp_cine_frame_ms(&c, 2082), 69470, "rounded up");
}

static void colour_follows_the_matrices(KpT *T)
{
	uint8_t rgb[3];
	kp_vpx_yuv_rgb(16, 128, 128, 601, 0, rgb);
	kp_t_ok(T, rgb[0] == 0 && rgb[1] == 0 && rgb[2] == 0, "studio black");
	kp_vpx_yuv_rgb(235, 128, 128, 601, 0, rgb);
	kp_t_ok(T, rgb[0] == 255 && rgb[1] == 255 && rgb[2] == 255, "studio white");
	kp_vpx_yuv_rgb(0, 128, 128, 601, 1, rgb);
	kp_t_ok(T, rgb[0] == 0 && rgb[1] == 0 && rgb[2] == 0, "full-range black");
	kp_vpx_yuv_rgb(255, 128, 128, 709, 1, rgb);
	kp_t_ok(T, rgb[0] == 255 && rgb[1] == 255 && rgb[2] == 255, "full-range white");
	kp_vpx_yuv_rgb(81, 90, 240, 601, 0, rgb); /* BT.601 studio red */
	kp_t_ok(T, rgb[0] >= 253 && rgb[1] <= 2 && rgb[2] <= 2, "red: %d %d %d", rgb[0], rgb[1], rgb[2]);
	kp_vpx_yuv_rgb(63, 102, 240, 709, 0, rgb); /* BT.709 studio red */
	kp_t_ok(T, rgb[0] >= 253 && rgb[1] <= 2 && rgb[2] <= 2, "red 709: %d %d %d", rgb[0], rgb[1], rgb[2]);
	kp_vpx_yuv_rgb(81, 90, 240, 709, 0, rgb);
	kp_t_ok(T, rgb[1] > 2, "the matrices differ");
}

/* Plays clip k frame by frame at the times of its frames; frames are FFmpeg's pictures; the mean error per byte must not
 * pass limit (0: the same bytes). */
static void play_clip(KpT *T, const uint8_t *pack, size_t size, int k, const char *frames_path, int limit)
{
	size_t ref_size;
	const uint8_t *ref = kp_t_read(T, frames_path, &ref_size);
	Memory m;
	KpCineIndex ix;
	KpError err;
	KpCinePlayer *p;
	uint32_t n;
	int worst = 0;
	kp_t_eq(T, index_of(pack, size, &m, &ix, &err), 0, "%s", err.msg);
	kp_t_eq(T, (int64_t)ref_size, (int64_t)FRAME_BYTES * FRAME_COUNT, "FFmpeg's frames");
	p = kp_cine_open(&ix, k, read_memory, &m, &err);
	if (!p) kp_t_fail(T, "%s", err.msg);
	for (n = 0; n < FRAME_COUNT; n++) {
		uint64_t t = kp_cine_frame_ms(&ix.clips[k], n), sum = 0;
		const uint8_t *got;
		int rc = kp_cine_advance(p, t, &err), i;
		if (rc != KP_CINE_FRAME) {
			kp_cine_free(p);
			kp_t_fail(T, "frame %u: %d (%s)", (unsigned)n, rc, err.msg);
		}
		got = kp_cine_rgba(p);
		for (i = 0; i < FRAME_BYTES; i++) {
			int d = got[i] - ref[(size_t)n * FRAME_BYTES + (size_t)i];
			if (d < 0) d = -d;
			if (d > worst) worst = d;
			sum += (uint64_t)d;
		}
		if (limit == 0 && sum != 0) {
			kp_cine_free(p);
			kp_t_fail(T, "frame %u differs from FFmpeg's (sum of errors %d)", (unsigned)n, (int)sum);
		}
		if (sum > (uint64_t)limit * FRAME_BYTES) {
			kp_cine_free(p);
			kp_t_fail(T, "frame %u: mean error %.2f of limit %d", (unsigned)n, (double)sum / FRAME_BYTES, limit);
		}
		kp_t_eq(T, kp_cine_advance(p, t, &err), KP_CINE_IDLE, "the same time shows the same picture");
	}
	kp_t_eq(T, kp_cine_advance(p, kp_cine_frame_ms(&ix.clips[k], FRAME_COUNT - 1) + 1, &err), KP_CINE_IDLE, "the last frame is held");
	kp_t_eq(T, kp_cine_advance(p, kp_cine_frame_ms(&ix.clips[k], FRAME_COUNT), &err), KP_CINE_END, "the movie ends");
	kp_t_eq(T, (int64_t)kp_cine_decoded(p), FRAME_COUNT, NULL);
	kp_cine_free(p);
	kp_t_log(T, "clip %d: worst error %d", k, worst);
}

static void srgb_movie_is_ffmpegs_picture(KpT *T)
{
	size_t size;
	const uint8_t *pack;
	if (!kp_t_ready(T, "srgb_movie_is_ffmpegs_picture", SRGB_FILES)) return;
	pack = kp_t_read(T, PACK, &size);
	play_clip(T, pack, size, 0, FRAMES_SRGB, 0);
}

static void ycc_movie_is_ffmpegs_picture_within_a_filter(KpT *T)
{
	size_t size;
	const uint8_t *pack;
	if (!kp_t_ready(T, "ycc_movie_is_ffmpegs_picture_within_a_filter", YCC_FILES)) return;
	pack = kp_t_read(T, PACK, &size);
	play_clip(T, pack, size, 1, FRAMES_YCC, YCC_MEAN_LIMIT);
}

/* The 4:2:0 movie's planes are FFmpeg's decode byte for byte, as VP9 requires of every decoder: libvpx's C and its
 * SIMD (NEON on arm64) alike. The RGBA test above allows for the chroma's filter; this one allows nothing. */
static void ycc_movie_planes_are_ffmpegs(KpT *T)
{
	size_t size, ref_size, at = 0;
	const uint8_t *pack, *ref;
	Memory m;
	KpCineIndex ix;
	KpError err;
	KpCinePlayer *p;
	KpVideoPicture pic;
	uint32_t n;
	int i, y;
	if (!kp_t_ready(T, "ycc_movie_planes_are_ffmpegs", YCC_PLANE_FILES)) return;
	pack = kp_t_read(T, PACK, &size);
	ref = kp_t_read(T, PLANES_YCC, &ref_size);
	kp_t_eq(T, (int64_t)ref_size, (int64_t)(W * H + 2 * (W / 2) * (H / 2)) * FRAME_COUNT, "FFmpeg's planes");
	kp_t_eq(T, index_of(pack, size, &m, &ix, &err), 0, "%s", err.msg);
	p = kp_cine_open(&ix, 1, read_memory, &m, &err);
	if (!p) kp_t_fail(T, "%s", err.msg);
	for (n = 0; n < FRAME_COUNT; n++) {
		int rc = kp_cine_advance(p, kp_cine_frame_ms(&ix.clips[1], n), &err);
		if (rc != KP_CINE_FRAME || kp_cine_picture(p, &pic) != 0) {
			kp_cine_free(p);
			kp_t_fail(T, "frame %u: %d (%s)", (unsigned)n, rc, err.msg);
		}
		kp_t_ok(T, !pic.srgb && pic.width == W && pic.height == H && pic.chroma_w == W / 2 && pic.chroma_h == H / 2,
			"frame %u: a 4:2:0 picture", (unsigned)n);
		for (i = 0; i < 3; i++) {
			int32_t w = i ? pic.chroma_w : pic.width, h = i ? pic.chroma_h : pic.height;
			for (y = 0; y < h; y++, at += (size_t)w) {
				if (memcmp(pic.planes[i] + (size_t)y * (size_t)pic.strides[i], ref + at, (size_t)w) != 0) {
					kp_cine_free(p);
					kp_t_fail(T, "frame %u: plane %d, row %d differs from FFmpeg's", (unsigned)n, i, y);
				}
			}
		}
	}
	kp_cine_free(p);
}

/* A movie behind the clock decodes a few packets a call and shows the picture of the time when it has caught up. */
static void a_late_movie_catches_up(KpT *T)
{
	size_t size, ref_size;
	const uint8_t *pack, *ref;
	Memory m;
	KpCineIndex ix;
	KpError err;
	KpCinePlayer *p;
	int calls = 0, rc;
	if (!kp_t_ready(T, "a_late_movie_catches_up", SRGB_FILES)) return;
	pack = kp_t_read(T, PACK, &size);
	ref = kp_t_read(T, FRAMES_SRGB, &ref_size);
	kp_t_eq(T, index_of(pack, size, &m, &ix, &err), 0, "%s", err.msg);
	p = kp_cine_open(&ix, 0, read_memory, &m, &err);
	if (!p) kp_t_fail(T, "%s", err.msg);
	do {
		rc = kp_cine_advance(p, kp_cine_frame_ms(&ix.clips[0], FRAME_COUNT - 1), &err);
		calls++;
	} while (rc == KP_CINE_FRAME && kp_cine_decoded(p) < FRAME_COUNT && calls < 10);
	kp_t_eq(T, (int64_t)kp_cine_decoded(p), FRAME_COUNT, "all the packets decoded");
	kp_t_ok(T, calls >= 2, "a few packets a call (%d calls)", calls);
	kp_t_ok(T, memcmp(kp_cine_rgba(p), ref + (size_t)(FRAME_COUNT - 1) * FRAME_BYTES, FRAME_BYTES) == 0, "the last picture");
	kp_cine_free(p);
}

static void open_refuses_a_table_that_does_not_fit(KpT *T)
{
	size_t size;
	const uint8_t *pack;
	uint8_t *bad;
	KpKpf c;
	const KpKpfSection *frms;
	Memory m;
	KpCineIndex ix;
	KpError err;
	if (!kp_t_has(T, PACK)) return;
	pack = kp_t_read(T, PACK, &size);
	bad = copy_of(T, pack, size);
	frms = section(T, &c, bad, size, "FRMS");
	bad[frms->offset + 3] &= 0x7f; /* the first packet is not a key frame */
	kp_kpf_free(&c);
	kp_t_eq(T, index_of(bad, size, &m, &ix, &err), 0, "%s", err.msg);
	kp_t_ok(T, kp_cine_open(&ix, 0, read_memory, &m, &err) == NULL, "a first packet that is not a key frame");
	kp_t_ok(T, strstr(err.msg, "key frame") != NULL, "%s", err.msg);
	memcpy(bad, pack, size);
	frms = section(T, &c, bad, size, "FRMS");
	bad[frms->offset + 4] ^= 1; /* a size that no longer adds up */
	kp_kpf_free(&c);
	kp_t_eq(T, index_of(bad, size, &m, &ix, &err), 0, "%s", err.msg);
	kp_t_ok(T, kp_cine_open(&ix, 0, read_memory, &m, &err) == NULL, "sizes that do not add up to the video");
	kp_t_ok(T, kp_cine_open(&ix, 9, read_memory, &m, &err) == NULL, "no such clip");
}

static void a_packet_that_does_not_decode_ends_with_an_error(KpT *T)
{
	size_t size;
	const uint8_t *pack;
	uint8_t *bad;
	KpKpf c;
	const KpKpfSection *frms;
	Memory m;
	KpCineIndex ix;
	KpError err;
	KpCinePlayer *p;
	uint32_t first;
	if (!kp_t_has(T, PACK)) return;
	pack = kp_t_read(T, PACK, &size);
	bad = copy_of(T, pack, size);
	frms = section(T, &c, bad, size, "FRMS");
	first = kp_le_u32(bad + frms->offset) & 0x7fffffffu;
	kp_kpf_free(&c);
	kp_t_eq(T, index_of(bad, size, &m, &ix, &err), 0, "%s", err.msg);
	memset(bad + ix.clips[0].video_offset + first, 0, 16); /* the second packet loses its frame marker */
	p = kp_cine_open(&ix, 0, read_memory, &m, &err);
	if (!p) kp_t_fail(T, "%s", err.msg);
	kp_t_eq(T, kp_cine_advance(p, 0, &err), KP_CINE_FRAME, "the key frame decodes");
	kp_t_eq(T, kp_cine_advance(p, kp_cine_frame_ms(&ix.clips[0], 1), &err), -1, "a packet that is not VP9");
	kp_t_ok(T, strstr(err.msg, "cinematics") == err.msg, "%s", err.msg);
	kp_cine_free(p);
	/* a read that fails */
	p = kp_cine_open(&ix, 0, read_memory, &m, &err);
	if (!p) kp_t_fail(T, "%s", err.msg);
	m.reads = 0;
	m.fail_read = 1;
	kp_t_eq(T, kp_cine_advance(p, 0, &err), -1, "a packet that cannot be read");
	kp_cine_free(p);
	m.fail_read = 1;
	m.reads = 0;
	kp_t_ok(T, kp_cine_open(&ix, 0, read_memory, &m, &err) == NULL, "a frame table that cannot be read");
}

/* The queue: each cinematic the pack has, after its prelude (when asked and the pack has it; not one without its
 * cinematic), as many as fit, a cinematic and its prelude both or neither, the count all of them. */
static void queue_takes_the_preludes_the_pack_has(KpT *T)
{
	KpCineIndex ix;
	int queue[8], ids[3] = { 6, 9, 7 }, wanted;
	memset(&ix, 0, sizeof ix);
	ix.count = 4;
	ix.clips[0].id = 6;
	ix.clips[1].id = 2;
	ix.clips[2].id = 7;
	ix.clips[3].id = 3;
	ix.preludes[0][0] = 6;
	ix.preludes[0][1] = 2;
	ix.preludes[1][0] = 7;
	ix.preludes[1][1] = 5; /* a prelude the pack lacks */
	ix.preludes[2][0] = 9;
	ix.preludes[2][1] = 3; /* the prelude of a cinematic the pack lacks */
	ix.prelude_count = 3;
	kp_t_eq(T, kp_cine_queue(&ix, ids, 3, 1, queue, 8, &wanted), 3, "9 is not in the pack (nor its prelude 3), nor 7's prelude");
	kp_t_eq(T, wanted, 3, NULL);
	kp_t_eq(T, queue[0], 2, "6 after its prelude");
	kp_t_eq(T, queue[1], 6, NULL);
	kp_t_eq(T, queue[2], 7, NULL);
	kp_t_eq(T, kp_cine_queue(&ix, ids, 3, 0, queue, 8, NULL), 2, "no preludes asked");
	kp_t_eq(T, queue[0], 6, NULL);
	kp_t_eq(T, queue[1], 7, NULL);
	kp_t_eq(T, kp_cine_queue(&ix, ids, 3, 1, queue, 2, &wanted), 2, "as many as fit");
	kp_t_eq(T, wanted, 3, "all of them");
	kp_t_eq(T, queue[0], 2, NULL);
	kp_t_eq(T, queue[1], 6, NULL);
	kp_t_eq(T, kp_cine_queue(&ix, ids, 3, 1, queue, 1, &wanted), 0,
		"a prelude without the room for its cinematic is not queued, nor what comes after");
	kp_t_eq(T, wanted, 3, NULL);
	kp_t_eq(T, kp_cine_queue(&ix, ids + 1, 1, 1, queue, 8, NULL), 0, "none the pack has");
}

/* The clock: the sound's position while it plays, the wall clock while it is awaited past KP_CINE_AUDIO_WAIT_MS and
 * after it ends, never back, a pause past KP_CINE_CLOCK_GAP_MS not counted. */
static void clock_follows_the_sound_then_the_wall(KpT *T)
{
	KpCineClock c;
	kp_cine_clock_begin(&c, 1000, 1);
	kp_t_eq(T, (int64_t)kp_cine_clock_ms(&c, 1100, 1, 0), 0, "the sound's start awaited");
	kp_t_eq(T, (int64_t)kp_cine_clock_ms(&c, 1200, 1, 40), 40, "the sound's position");
	kp_t_eq(T, (int64_t)kp_cine_clock_ms(&c, 1300, 1, 30), 40, "never back");
	kp_t_eq(T, (int64_t)kp_cine_clock_ms(&c, 1400, 1, 240), 240, NULL);
	kp_t_eq(T, (int64_t)kp_cine_clock_ms(&c, 1500, 0, -1), 340, "the sound ended: the wall from where it left");
	kp_t_eq(T, c.audio, 0, NULL);
	kp_t_eq(T, (int64_t)kp_cine_clock_ms(&c, 1600, 1, 900), 440, "no longer read");
	kp_t_eq(T, (int64_t)kp_cine_clock_ms(&c, 5600, 0, -1), 440, "a pause of the background is not the movie's");
	kp_t_eq(T, (int64_t)kp_cine_clock_ms(&c, 5700, 0, -1), 540, NULL);
	/* a sound that never begins */
	kp_cine_clock_begin(&c, 0, 1);
	kp_t_eq(T, (int64_t)kp_cine_clock_ms(&c, KP_CINE_AUDIO_WAIT_MS - 10, 1, 0), 0, "awaited");
	kp_t_eq(T, (int64_t)kp_cine_clock_ms(&c, KP_CINE_AUDIO_WAIT_MS + 100, 1, 0), 110,
		"the wall from the wait's last frame");
	kp_t_eq(T, (int64_t)kp_cine_clock_ms(&c, KP_CINE_AUDIO_WAIT_MS + 200, 1, 50), 110,
		"a late sound behind the wall holds the picture");
	kp_t_eq(T, (int64_t)kp_cine_clock_ms(&c, KP_CINE_AUDIO_WAIT_MS + 300, 1, 400), 400, "then its position");
	/* no sound */
	kp_cine_clock_begin(&c, 0, 0);
	kp_t_eq(T, (int64_t)kp_cine_clock_ms(&c, 120, 0, -1), 120, "the wall from the start");
	/* a wait for the data: none of it is the movie's, whether long or short */
	kp_cine_clock_begin(&c, 0, 0);
	kp_t_eq(T, (int64_t)kp_cine_clock_ms(&c, 100, 0, -1), 100, NULL);
	kp_cine_clock_hold(&c, 150);
	kp_cine_clock_hold(&c, 900);
	kp_t_eq(T, (int64_t)kp_cine_clock_ms(&c, 910, 0, -1), 110, "the wall goes on from before the wait");
	kp_cine_clock_begin(&c, 0, 1);
	kp_cine_clock_hold(&c, 5000);
	kp_t_eq(T, (int64_t)kp_cine_clock_ms(&c, 5100, 1, 0), 0, "a sound's start awaited from after the wait");
	kp_t_eq(T, c.audio, 1, NULL);
}

/* A packet that has not come: the player waits, loses nothing, and goes on from it. */
typedef struct {
	Memory m;
	int wait_from;  /* the read (from 1) that begins the wait; 0 none */
	int waits_left; /* answers of KP_CINE_READ_WAIT from there on */
} LateMemory;

static int read_late(void *ctx, uint64_t offset, uint8_t *out, size_t size)
{
	LateMemory *l = (LateMemory *)ctx;
	if (l->wait_from && l->m.reads + 1 >= l->wait_from && l->waits_left > 0) {
		l->waits_left--;
		return KP_CINE_READ_WAIT;
	}
	return read_memory(&l->m, offset, out, size);
}

static void a_packet_not_come_waits_and_loses_nothing(KpT *T)
{
	size_t size, ref_size;
	const uint8_t *pack, *ref;
	LateMemory l;
	KpCineIndex ix;
	KpError err;
	KpCinePlayer *p;
	uint32_t n;
	if (!kp_t_ready(T, "a_packet_not_come_waits_and_loses_nothing", SRGB_FILES)) return;
	pack = kp_t_read(T, PACK, &size);
	ref = kp_t_read(T, FRAMES_SRGB, &ref_size);
	memset(&l, 0, sizeof l);
	kp_t_eq(T, index_of(pack, size, &l.m, &ix, &err), 0, "%s", err.msg);
	l.m.reads = 0;
	l.wait_from = 2; /* the first packets' window, after the frame table */
	l.waits_left = 3;
	p = kp_cine_open(&ix, 0, read_late, &l, &err);
	if (!p) kp_t_fail(T, "%s", err.msg);
	kp_t_eq(T, kp_cine_advance(p, 0, &err), KP_CINE_WAIT, "the first packet");
	kp_t_eq(T, kp_cine_advance(p, 0, &err), KP_CINE_WAIT, NULL);
	kp_t_eq(T, kp_cine_advance(p, 0, &err), KP_CINE_WAIT, NULL);
	kp_t_eq(T, (int64_t)kp_cine_decoded(p), 0, "none decoded");
	for (n = 0; n < FRAME_COUNT; n++) {
		kp_t_eq(T, kp_cine_advance(p, kp_cine_frame_ms(&ix.clips[0], n), &err), KP_CINE_FRAME, "picture %u", (unsigned)n);
		kp_t_ok(T, memcmp(kp_cine_rgba(p), ref + (size_t)n * FRAME_BYTES, FRAME_BYTES) == 0, "picture %u", (unsigned)n);
	}
	kp_cine_free(p);
}

/* --- the pack's file over fetched chunks --- */

typedef struct {
	uint64_t offset;
	uint32_t size;
	int state; /* 0 out, 1 come, -1 failed */
	int dropped, taken, wrong_size;
} FakeRequest;

#define FAKE_REQUESTS 32
typedef struct {
	FakeRequest requests[FAKE_REQUESTS];
	int count;
	int refuse; /* start answers NULL */
} FakeFetcher;

static uint8_t fake_byte(uint64_t at) { return (uint8_t)(at * 31 + (at >> 16)); }

static void *fake_start(void *ctx, uint64_t offset, uint32_t size)
{
	FakeFetcher *f = (FakeFetcher *)ctx;
	FakeRequest *r;
	if (f->refuse || f->count == FAKE_REQUESTS) return NULL;
	r = &f->requests[f->count++];
	memset(r, 0, sizeof *r);
	r->offset = offset;
	r->size = size;
	return r;
}

static int fake_state(void *ctx, void *request)
{
	(void)ctx;
	return ((FakeRequest *)request)->state;
}

static uint8_t *fake_take(void *ctx, void *request, size_t *size)
{
	FakeRequest *r = (FakeRequest *)request;
	size_t n = r->wrong_size ? r->size / 2 : r->size, k;
	uint8_t *data = (uint8_t *)malloc(n ? n : 1);
	(void)ctx;
	r->taken++;
	if (!data) return NULL;
	for (k = 0; k < n; k++) data[k] = fake_byte(r->offset + k);
	*size = n;
	return data;
}

static void fake_drop(void *ctx, void *request)
{
	(void)ctx;
	((FakeRequest *)request)->dropped++;
}

static const FakeRequest *fake_find(const FakeFetcher *f, uint64_t chunk)
{
	int k;
	for (k = f->count - 1; k >= 0; k--) {
		if (f->requests[k].offset == chunk * KP_CINE_STREAM_CHUNK) return &f->requests[k];
	}
	return NULL;
}

static void fake_come(FakeFetcher *f, uint64_t chunk, int state)
{
	FakeRequest *r = (FakeRequest *)fake_find(f, chunk);
	if (r) r->state = state;
}

static void stream_init(KpCineStream *s, FakeFetcher *f, const uint8_t *head, size_t head_size, uint64_t file_size)
{
	memset(f, 0, sizeof *f);
	memset(s, 0, sizeof *s);
	s->fetcher.ctx = f;
	s->fetcher.start = fake_start;
	s->fetcher.state = fake_state;
	s->fetcher.take = fake_take;
	s->fetcher.drop = fake_drop;
	s->head = head;
	s->head_size = head_size;
	s->file_size = file_size;
}

static void stream_reads_the_head_at_hand_and_the_rest_by_chunks(KpT *T)
{
	enum { C = KP_CINE_STREAM_CHUNK };
	static const uint8_t HEAD[4] = { 1, 2, 3, 4 };
	KpCineStream s;
	FakeFetcher f;
	uint8_t *out = (uint8_t *)malloc(2 * C);
	uint64_t file = 10ull * C + 5, at;
	size_t k;
	int bad = 0;
	if (!out) kp_t_fail(T, "out of memory");
	kp_t_defer(T, free, out);
	stream_init(&s, &f, HEAD, sizeof HEAD, file);
	kp_t_eq(T, kp_cine_stream_read(&s, 1, out, 3), 0, "in the head");
	kp_t_eq(T, out[2], 4, NULL);
	kp_t_eq(T, f.count, 0, "no request for it");
	kp_t_eq(T, kp_cine_stream_read(&s, file - 1, out, 2), -1, "past the end");
	kp_t_eq(T, kp_cine_stream_read(&s, file, out, 1), -1, NULL);
	kp_t_eq(T, kp_cine_stream_read(&s, 0, out, 0), -1, "no bytes");
	kp_t_eq(T, f.count, 0, NULL);

	/* across two chunks, with the head's bytes before: nothing until both have come, in any order */
	at = C - 10;
	kp_t_eq(T, kp_cine_stream_read(&s, at, out, 20), KP_CINE_READ_WAIT, NULL);
	kp_t_eq(T, f.count, 2, "no chunk ahead before the ranges are answered");
	kp_t_eq(T, (int64_t)f.requests[0].size, C, "a whole chunk");
	fake_come(&f, 1, 1);
	kp_t_eq(T, kp_cine_stream_read(&s, at, out, 20), KP_CINE_READ_WAIT, "one of the two");
	fake_come(&f, 0, 1);
	kp_t_eq(T, kp_cine_stream_read(&s, at, out, 20), 0, NULL);
	kp_t_eq(T, f.count, KP_CINE_STREAM_SLOTS, "then the chunks ahead too");
	for (k = 0; k < 20; k++) bad += out[k] != fake_byte(at + k);
	kp_t_eq(T, bad, 0, "the bytes across the chunks");
	kp_t_eq(T, kp_cine_stream_read(&s, at, out, 20), 0, "again");
	kp_t_eq(T, f.count, KP_CINE_STREAM_SLOTS, "no second request");

	/* forward: the chunks before go, one more ahead; a chunk ahead that has failed fails only when it is read */
	fake_come(&f, 2, -1);
	kp_t_eq(T, kp_cine_stream_read(&s, C, out, 20), 0, "the chunk come");
	kp_t_eq(T, f.count, KP_CINE_STREAM_SLOTS + 1, "one more ahead");
	kp_t_eq(T, (int64_t)f.requests[f.count - 1].offset, (int64_t)4 * C, NULL);
	kp_t_eq(T, kp_cine_stream_read(&s, 2ull * C, out, 20), -1, "the chunk that failed");
	kp_t_eq(T, fake_find(&f, 2)->dropped, 1, "ended");

	/* a jump (the next movie): what is out is dropped, the last chunk is short */
	kp_t_eq(T, kp_cine_stream_read(&s, 10ull * C, out, 5), KP_CINE_READ_WAIT, NULL);
	kp_t_eq(T, fake_find(&f, 4)->dropped, 1, "the chunk out let go");
	kp_t_eq(T, (int64_t)fake_find(&f, 10)->size, 5, "the file's end");
	fake_come(&f, 10, 1);
	kp_t_eq(T, kp_cine_stream_read(&s, 10ull * C, out, 5), 0, NULL);
	kp_t_eq(T, out[4], fake_byte(10ull * C + 4), NULL);

	/* a read longer than the slots hold */
	kp_t_eq(T, kp_cine_stream_read(&s, 0, out, (size_t)KP_CINE_STREAM_SLOTS * C + 1), -1, NULL);
	kp_cine_stream_free(&s);

	/* a range that is not the one asked for, a request refused */
	stream_init(&s, &f, HEAD, sizeof HEAD, file);
	kp_t_eq(T, kp_cine_stream_read(&s, 100, out, 1), KP_CINE_READ_WAIT, NULL);
	f.requests[0].wrong_size = 1;
	fake_come(&f, 0, 1);
	kp_t_eq(T, kp_cine_stream_read(&s, 100, out, 1), -1, "a short answer");
	kp_cine_stream_free(&s);
	stream_init(&s, &f, HEAD, sizeof HEAD, file);
	f.refuse = 1;
	kp_t_eq(T, kp_cine_stream_read(&s, 100, out, 1), -1, "no request");
	kp_cine_stream_free(&s);

	/* what is out at the end is dropped */
	stream_init(&s, &f, HEAD, sizeof HEAD, file);
	kp_t_eq(T, kp_cine_stream_read(&s, 100, out, 1), KP_CINE_READ_WAIT, NULL);
	kp_cine_stream_free(&s);
	for (k = 0; k < (size_t)f.count; k++) bad += f.requests[k].dropped != 1;
	kp_t_eq(T, bad, 0, "every request ended");
}

/* The decoder's picture for the GPU: its planes and their sizes, sRGB's planes as G, B, R, and YCbCr's matrix; no
 * picture before the first frame. */
static void picture_gives_the_planes_and_the_matrix(KpT *T)
{
	size_t size;
	const uint8_t *pack;
	Memory m;
	KpCineIndex ix;
	KpError err;
	KpCinePlayer *p;
	KpVideoPicture pic;
	int k, ch;
	if (!kp_t_has(T, PACK)) return;
	pack = kp_t_read(T, PACK, &size);
	kp_t_eq(T, index_of(pack, size, &m, &ix, &err), 0, "%s", err.msg);
	for (k = 0; k < 2; k++) {
		const uint8_t *rgba;
		uint8_t rgb[3];
		p = kp_cine_open(&ix, k, read_memory, &m, &err);
		if (!p) kp_t_fail(T, "%s", err.msg);
		kp_t_eq(T, kp_cine_picture(p, &pic), -1, "no picture before the first frame");
		kp_t_ok(T, kp_cine_rgba(p) == NULL, "nor RGBA");
		kp_t_eq(T, kp_cine_advance(p, 0, &err), KP_CINE_FRAME, "%s", err.msg);
		kp_t_eq(T, kp_cine_picture(p, &pic), 0, NULL);
		kp_t_eq(T, pic.width, W, NULL);
		kp_t_eq(T, pic.height, H, NULL);
		kp_t_eq(T, pic.chroma_w, k == 0 ? W : W / 2, "4:4:4, then 4:2:0");
		kp_t_eq(T, pic.chroma_h, k == 0 ? H : H / 2, NULL);
		kp_t_eq(T, pic.srgb, k == 0, NULL);
		kp_t_ok(T, pic.strides[0] >= W && pic.strides[1] >= pic.chroma_w && pic.strides[2] >= pic.chroma_w, "strides");
		rgba = kp_cine_rgba(p);
		if (k == 0) {
			kp_t_eq(T, rgba[0], pic.planes[2][0], "R from the third plane");
			kp_t_eq(T, rgba[1], pic.planes[0][0], "G from the first");
			kp_t_eq(T, rgba[2], pic.planes[1][0], "B from the second");
		} else { /* the first pixel by the picture's matrix as floats, against the CPU's conversion */
			float y = pic.planes[0][0] / 255.0f, cb = pic.planes[1][0] / 255.0f - 128.0f / 255.0f,
				cr = pic.planes[2][0] / 255.0f - 128.0f / 255.0f, l = (y - pic.y_low) * pic.y_scale;
			float want[3];
			want[0] = l + pic.cr_r * cr;
			want[1] = l - pic.cb_g * cb - pic.cr_g * cr;
			want[2] = l + pic.cb_b * cb;
			kp_t_ok(T, pic.y_low > 0.06f && pic.y_low < 0.07f && pic.y_scale > 1.16f && pic.y_scale < 1.17f,
				"studio range (%.3f, %.3f)", pic.y_low, pic.y_scale);
			kp_vpx_yuv_rgb(pic.planes[0][0], pic.planes[1][0], pic.planes[2][0], 601, 0, rgb);
			kp_t_ok(T, memcmp(rgba, rgb, 3) == 0, "BT.601 when the picture names no matrix");
			for (ch = 0; ch < 3; ch++) {
				float v = want[ch] < 0 ? 0 : want[ch] > 1 ? 1 : want[ch];
				kp_t_ok(T, fabsf(v * 255.0f - rgba[ch]) <= 1.0f, "channel %d: %.2f and %d", ch, v * 255.0f, rgba[ch]);
			}
		}
		kp_cine_free(p);
	}
}

/* An empty packet (libvpx refuses one) is the frame table's error at open. */
static void open_refuses_an_empty_packet(KpT *T)
{
	size_t size;
	const uint8_t *pack;
	uint8_t *bad;
	KpKpf c;
	const KpKpfSection *frms;
	Memory m;
	KpCineIndex ix;
	KpError err;
	uint32_t second, third;
	if (!kp_t_has(T, PACK)) return;
	pack = kp_t_read(T, PACK, &size);
	bad = copy_of(T, pack, size);
	frms = section(T, &c, bad, size, "FRMS");
	second = kp_le_u32(bad + frms->offset + 4);
	third = kp_le_u32(bad + frms->offset + 8);
	/* the second packet's bytes given to the third: the sizes still add up to the video */
	put_u32(bad + frms->offset + 4, second & 0x80000000u);
	put_u32(bad + frms->offset + 8, third + (second & 0x7fffffffu));
	kp_kpf_free(&c);
	kp_t_eq(T, index_of(bad, size, &m, &ix, &err), 0, "%s", err.msg);
	kp_t_ok(T, kp_cine_open(&ix, 0, read_memory, &m, &err) == NULL, "a packet of 0 bytes");
	kp_t_ok(T, strstr(err.msg, "empty packet") != NULL, "%s", err.msg);
}

static void index_refuses_a_pack_that_is_not_right(KpT *T)
{
	size_t size;
	const uint8_t *pack;
	uint8_t *bad;
	KpKpf c;
	const KpKpfSection *s;
	uint64_t at;
	Memory m;
	KpCineIndex ix;
	KpError err;
	if (!kp_t_has(T, PACK)) return;
	pack = kp_t_read(T, PACK, &size);
	bad = copy_of(T, pack, size);
	kp_t_eq(T, index_of(bad, size / 2, &m, &ix, &err), -1, "a pack cut short");
	kp_t_eq(T, ix.count, 0, "and no clips");
	s = section(T, &c, bad, size, "CLIP");
	at = s->offset;
	kp_kpf_free(&c);
	bad[at + 64] = bad[at + 108 + 64]; /* the second clip repeats the first's number */
	kp_t_eq(T, index_of(bad, size, &m, &ix, &err), -1, "a clip number that repeats");
	memcpy(bad, pack, size);
	bad[at + 68] = 0;
	bad[at + 69] = 0; /* a width of 0 */
	kp_t_eq(T, index_of(bad, size, &m, &ix, &err), -1, "a clip of no width");
	memcpy(bad, pack, size);
	bad[at + 76] = 0;
	bad[at + 77] = 0;
	bad[at + 78] = 0;
	bad[at + 79] = 0; /* a rate of 0 */
	kp_t_eq(T, index_of(bad, size, &m, &ix, &err), -1, "a clip of no rate");
	memcpy(bad, pack, size);
	bad[at + 95] = 0x7f; /* a video that runs past DATA */
	kp_t_eq(T, index_of(bad, size, &m, &ix, &err), -1, "a clip past DATA");
	memcpy(bad, pack, size);
	bad[at + 84 + 3] = 0x40; /* more frames than FRMS holds */
	kp_t_eq(T, index_of(bad, size, &m, &ix, &err), -1, "a clip past FRMS");
	memcpy(bad, pack, size);
	kp_t_eq(T, index_of(bad, size, &m, &ix, &err), 0, "%s", err.msg);
	kp_t_eq(T, m.reads, 0, "the small pack's sections all from its head");
	s = section(T, &c, bad, size, "HEAD");
	at = s->offset; /* a head of the section table alone: every section read */
	kp_kpf_free(&c);
	m.reads = 0;
	kp_t_eq(T, kp_cine_index(&ix, bad, (size_t)at, size, read_memory, &m, &err), 0, "%s", err.msg);
	kp_t_eq(T, m.reads, 4, "HEAD, CLIP, PREL and SUBS read once each");
	kp_t_eq(T, ix.count, 2, NULL);
	m.fail_read = 2;
	m.reads = 0;
	kp_t_eq(T, kp_cine_index(&ix, bad, (size_t)at, size, read_memory, &m, &err), -1, "clips that cannot be read");
	kp_t_eq(T, ix.count, 0, NULL);
	m.fail_read = 0;
	s = section(T, &c, bad, size, "CLIP");
	at = s->offset;
	kp_kpf_free(&c);
	bad[at + 80] = 0x41;
	bad[at + 81] = 0x42;
	bad[at + 82] = 0x0f; /* a rate denominator of 1,000,001 */
	kp_t_eq(T, index_of(bad, size, &m, &ix, &err), -1, "a rate out of range");
	memcpy(bad, pack, size);
	bad[16] ^= 0x7f; /* another schema (the header's u32 at 16) */
	kp_t_eq(T, index_of(bad, size, &m, &ix, &err), -1, "a pack of another schema");
	kp_t_ok(T, strstr(err.msg, "schema") != NULL, "%s", err.msg);
	memcpy(bad, pack, size);
	memcpy(bad, "KPRFMUSC", 8);
	kp_t_eq(T, index_of(bad, size, &m, &ix, &err), -1, "another container");
}

/* --- a run of movies (KpCineRun) over a host that records what it is asked, on a clock the test moves --- */

#define RUN_STEP_NS 10000000u /* a frame of the host: 10 ms */
#define RUN_STEP_MS 10
#define RUN_FRAMES 400        /* more than a fixture movie's 800 ms in host frames */
#define RUN_WAIT_FRAMES 100   /* a second of the host's frames: past a fixture movie's end on the wall clock */
#define RUN_SHORT_WAIT_FRAMES 10 /* 100 ms of them: under the clock's gap */
#define SOUND_AT_MS 300       /* a sound's position that holds the movie inside it */

typedef struct {
	Memory m;
	KpCineRun r;
	uint64_t now;
	int opens, closes, begins, ends, sounds, sound_reads, sound_stops;
	int fail_open;     /* the pack's file does not open */
	int sound_ok;      /* a movie's sound begins */
	int sound_playing; /* the sound plays at sound_ms */
	int64_t sound_ms;
	int passed;        /* the movies of cinematic 0 passed over (logged) */
	int sound_pauses, sound_resumes;
	int wait_reads;    /* run_read_late: the packets have not come */
	int fail_late;     /* run_read_late: the packets do not come */
	char last_log[512];
	char last_ended[512]; /* the log's last line of a movie's end */
} RunHost;

static uint64_t run_now(void *ctx) { return ((RunHost *)ctx)->now; }

static void *run_open(void *ctx)
{
	RunHost *h = (RunHost *)ctx;
	h->opens++;
	return h->fail_open ? NULL : &h->m;
}

static void run_close(void *ctx, void *file)
{
	RunHost *h = (RunHost *)ctx;
	if (file == &h->m) h->closes++;
}

static void run_begin(void *ctx) { ((RunHost *)ctx)->begins++; }

static void run_end(void *ctx) { ((RunHost *)ctx)->ends++; }

static int run_sound(void *ctx, const KpCineClip *clip, const uint8_t *data, size_t size)
{
	RunHost *h = (RunHost *)ctx;
	(void)clip, (void)size;
	h->sounds += data == NULL; /* a pack's sound is the host's to read */
	return h->sound_ok;
}

static int run_sound_ms(void *ctx, int64_t *ms)
{
	RunHost *h = (RunHost *)ctx;
	h->sound_reads++;
	*ms = h->sound_playing ? h->sound_ms : -1;
	return h->sound_playing; /* 0: the sound ended, or never began */
}

static void run_sound_stop(void *ctx) { ((RunHost *)ctx)->sound_stops++; }

static void run_sound_pause(void *ctx, int pause)
{
	RunHost *h = (RunHost *)ctx;
	if (pause) h->sound_pauses++;
	else h->sound_resumes++;
}

/* The reads of the packets (the frame tables and the subtitles lie before them) answer KP_CINE_READ_WAIT while
 * h->wait_reads is set, and fail while h->fail_late is. */
static int run_read_late(void *ctx, uint64_t offset, uint8_t *out, size_t size)
{
	RunHost *h = (RunHost *)(void *)((char *)ctx - offsetof(RunHost, m));
	if (offset >= h->r.index.clips[0].video_offset) {
		if (h->wait_reads) return KP_CINE_READ_WAIT;
		if (h->fail_late) return -1;
	}
	return read_memory(ctx, offset, out, size);
}

static void run_log(void *ctx, const char *line)
{
	RunHost *h = (RunHost *)ctx;
	snprintf(h->last_log, sizeof h->last_log, "%s", line);
	if (strncmp(line, "cinematics: 0: ", 15) == 0) h->passed++;
	if (strstr(line, " ended after ")) snprintf(h->last_ended, sizeof h->last_ended, "%s", line);
}

/* A run's host over the fixture's pack (NULL: no pack, the SKIP line logged). */
static RunHost *run_host(KpT *T)
{
	static const char *const FILES[] = { PACK, NULL };
	RunHost *h;
	KpCineHost *c;
	const uint8_t *pack;
	size_t size;
	KpError err;
	if (!kp_t_ready(T, T->test, FILES)) return NULL;
	pack = kp_t_read(T, PACK, &size);
	h = (RunHost *)calloc(1, sizeof *h);
	if (!h) kp_t_fail(T, "out of memory");
	kp_t_defer(T, free, h);
	kp_t_eq(T, index_of(pack, size, &h->m, &h->r.index, &err), 0, "%s", err.msg);
	h->r.have = 1;
	h->now = RUN_STEP_NS;
	c = &h->r.host;
	c->ctx = h;
	c->now_ns = run_now;
	c->read = read_memory;
	c->open = run_open;
	c->close = run_close;
	c->begin = run_begin;
	c->end = run_end;
	c->sound = run_sound;
	c->sound_ms = run_sound_ms;
	c->sound_stop = run_sound_stop;
	c->sound_pause = run_sound_pause;
	c->log = run_log;
	return h;
}

/* A frame of the host: the run's picture, shown when there is a new one; 1 then. */
static int run_frame(RunHost *h)
{
	KpVideoPicture pic;
	int fresh;
	h->now += RUN_STEP_NS;
	fresh = kp_cine_run_frame(&h->r, &pic);
	if (fresh) kp_cine_run_shown(&h->r);
	return fresh;
}

static int playing_id(const RunHost *h) { return h->r.player ? kp_cine_clip(h->r.player)->id : -1; }

/* Frames until the movie shows a picture. */
static void until_picture(KpT *T, RunHost *h)
{
	int k;
	for (k = 0; k < RUN_FRAMES; k++) {
		if (run_frame(h)) return;
	}
	kp_t_fail(T, "no picture of movie %d", playing_id(h));
}

/* The pictures of movie id shown until it ends (the next one, or the run's end). */
static int run_movie(KpT *T, RunHost *h, int id)
{
	int k, pictures = 0;
	kp_t_eq(T, playing_id(h), id, "the movie playing");
	for (k = 0; k < RUN_FRAMES && playing_id(h) == id; k++) pictures += run_frame(h);
	kp_t_ok(T, playing_id(h) != id, "movie %d ended", id);
	return pictures;
}

/* The queue plays one movie after the other, each to its end with every picture, then the run ends: its file closed,
 * the host's end once; a movie that begins has no picture before its own first one. */
static void run_plays_its_movies_one_after_the_other(KpT *T)
{
	static const int IDS[] = { 0, 1 };
	RunHost *h = run_host(T);
	int k, shown = 0;
	if (!h) return;
	kp_t_eq(T, kp_cine_run_start(&h->r, IDS, 2, 0), 1, "the run begins");
	kp_t_ok(T, h->r.active && h->opens == 1 && h->begins == 1, "the file opened, the host's begin");
	kp_t_eq(T, h->r.queue_len, 2, NULL);
	kp_t_eq(T, h->sounds, 1, "the first movie's sound asked for");
	kp_t_eq(T, run_movie(T, h, 0), FRAME_COUNT, "every picture of the first");
	kp_t_eq(T, h->r.drawn_ns, 0, "the second movie has shown none yet");
	kp_t_eq(T, h->r.shown, 0, NULL);
	kp_t_eq(T, h->sound_stops, 1, "the first movie's sound released");
	kp_t_eq(T, h->r.subs.count, 2, "the second movie's subtitles loaded");
	kp_t_eq(T, kp_cine_run_cue(&h->r), -1, "none before the first's start");
	for (k = 0; k < RUN_FRAMES && h->r.active && h->r.time_ms < 150; k++) shown += run_frame(h);
	kp_t_ok(T, h->r.time_ms >= 150 && h->r.time_ms < 300, "%llu ms", (unsigned long long)h->r.time_ms);
	kp_t_eq(T, kp_cine_run_cue(&h->r), 0, "the first subtitle, by the movie's time");
	kp_t_eq(T, h->r.movies, 2, NULL);
	kp_t_eq(T, shown + run_movie(T, h, 1), FRAME_COUNT, "every picture of the second");
	kp_t_eq(T, kp_cine_run_cue(&h->r), -1, "none after the run");
	kp_t_ok(T, !h->r.active && h->r.player == NULL, "the run ended");
	kp_t_eq(T, h->ends, 1, "the host's end once");
	kp_t_eq(T, h->closes, 1, "the file closed");
	kp_t_ok(T, h->r.file == NULL, NULL);
	kp_t_eq(T, h->sound_stops, 2, NULL);
	kp_t_eq(T, h->sound_reads, 0, "no sound: its position never read");
	kp_t_ok(T, strstr(h->last_log, "ended after") != NULL, "%s", h->last_log);
	kp_t_eq(T, run_frame(h), 0, "nothing after the run");
}

/* A press ends the movie shown, not one made before its first picture: neither the press that ended the movie before
 * it nor one of the same moment (the other fingers and buttons); the last movie's end ends the run. */
static void run_press_ends_the_movie_shown(KpT *T)
{
	static const int IDS[] = { 0, 1 };
	RunHost *h = run_host(T);
	uint64_t first;
	if (!h) return;
	kp_t_eq(T, kp_cine_run_start(&h->r, IDS, 2, 0), 1, NULL);
	kp_cine_run_skip(&h->r, h->now);
	kp_t_eq(T, playing_id(h), 0, "a press before the first picture");
	until_picture(T, h);
	first = h->r.drawn_ns;
	kp_t_eq(T, first, h->now, "the first picture's time");
	kp_cine_run_skip(&h->r, first - 1);
	kp_t_eq(T, playing_id(h), 0, "a press made before it was shown");
	kp_cine_run_skip(&h->r, first);
	kp_t_eq(T, playing_id(h), 1, "the press ends the movie shown");
	kp_cine_run_skip(&h->r, first);
	kp_t_eq(T, playing_id(h), 1, "the same moment's other press does not end the next");
	until_picture(T, h);
	kp_cine_run_skip(&h->r, h->r.drawn_ns + 1);
	kp_t_ok(T, !h->r.active && h->ends == 1 && h->closes == 1, "the last movie's press ends the run");
	kp_cine_run_skip(&h->r, h->now);
	kp_t_eq(T, h->ends, 1, "a press after the run: nothing");
}

/* A movie whose packets have not come holds its clock and its sound, then goes on from where it was; a press ends it
 * though it has no picture yet; packets that fail end it. */
static void run_waits_for_its_packets(KpT *T)
{
	static const int IDS[] = { 0, 1 };
	RunHost *h = run_host(T);
	uint64_t begun, movie_ms;
	unsigned wall_ms;
	char wall[64];
	int k;
	if (!h) return;
	h->r.host.read = run_read_late;
	h->sound_ok = 1;
	h->sound_playing = 1;
	kp_t_eq(T, kp_cine_run_start(&h->r, IDS, 2, 0), 1, NULL);
	/* the frame table and the subtitles are read, the first packets wait */
	h->wait_reads = 1;
	kp_t_eq(T, run_frame(h), 0, "no picture");
	kp_t_eq(T, h->sound_pauses, 1, "the sound held");
	kp_t_eq(T, h->r.waiting, 1, NULL);
	kp_t_eq(T, run_frame(h), 0, NULL);
	kp_t_eq(T, h->sound_pauses, 1, "once");
	for (k = 0; k < RUN_WAIT_FRAMES; k++) run_frame(h);
	kp_t_eq(T, (int64_t)h->r.time_ms, 0, "the movie's time stands");
	h->wait_reads = 0;
	kp_t_eq(T, run_frame(h), 1, "the picture when they come");
	kp_t_eq(T, h->sound_resumes, 1, "the sound goes on");
	kp_t_eq(T, h->r.waiting, 0, NULL);
	run_frame(h);
	run_frame(h);
	kp_t_eq(T, (int64_t)h->r.time_ms, 0, "its start still awaited: the wait took none of that time");
	kp_cine_run_stop(&h->r);

	/* a short wait is no part of the wall's time either, and the log's wall has it */
	h = run_host(T);
	if (!h) return;
	h->r.host.read = run_read_late;
	h->wait_reads = 1;
	kp_t_eq(T, kp_cine_run_start(&h->r, IDS, 1, 0), 1, NULL);
	begun = h->now;
	run_frame(h);
	kp_t_eq(T, h->r.waiting, 1, NULL);
	movie_ms = h->r.time_ms;
	for (k = 0; k < RUN_SHORT_WAIT_FRAMES; k++) run_frame(h);
	h->wait_reads = 0;
	run_frame(h);
	run_frame(h);
	kp_t_eq(T, (int64_t)h->r.time_ms, (int64_t)movie_ms + RUN_STEP_MS, "one host's frame on");
	wall_ms = (unsigned)((h->now - begun) / 1000000u);
	kp_cine_run_stop(&h->r);
	snprintf(wall, sizeof wall, "after %u ms of the wall", wall_ms);
	kp_t_ok(T, strstr(h->last_ended, wall) != NULL, "%s: %s", wall, h->last_ended);

	/* a press ends a movie that waits, with no picture shown */
	h = run_host(T);
	if (!h) return;
	h->r.host.read = run_read_late;
	kp_t_eq(T, kp_cine_run_start(&h->r, IDS, 2, 0), 1, NULL);
	h->wait_reads = 1;
	kp_t_eq(T, playing_id(h), 0, NULL);
	run_frame(h);
	kp_cine_run_skip(&h->r, h->now);
	kp_t_eq(T, playing_id(h), 1, "the next");
	kp_t_eq(T, h->r.waiting, 0, "it begins anew");

	/* a press from before its wait does not: the second finger of the press that ended the movie before it */
	kp_cine_run_skip(&h->r, h->now);
	kp_t_eq(T, playing_id(h), 1, "no wait yet");
	run_frame(h);
	kp_cine_run_skip(&h->r, h->now - 1);
	kp_t_eq(T, playing_id(h), 1, "from before the wait");

	/* packets that fail after the wait end the movie: the sound does not go on */
	kp_t_eq(T, h->r.waiting, 1, NULL);
	h->wait_reads = 0;
	h->fail_late = 1;
	run_frame(h);
	kp_t_ok(T, !h->r.active, "the last movie ended");
	kp_t_eq(T, h->sound_resumes, 0, NULL);
}

/* Stop ends the run at once (the movies queued after the one playing left), once. */
/* A pack the host gave up on (the web's server without Range requests): no more movies, none of them tried. */
static void run_ends_when_its_pack_is_gone(KpT *T)
{
	static const int IDS[] = { 0, 1 };
	RunHost *h = run_host(T);
	if (!h) return;
	kp_t_eq(T, kp_cine_run_start(&h->r, IDS, 2, 0), 1, NULL);
	until_picture(T, h);
	h->r.have = 0;
	kp_cine_run_skip(&h->r, h->now);
	kp_t_ok(T, !h->r.active, "the movie queued is not opened");
	kp_t_eq(T, h->ends, 1, NULL);
	kp_t_eq(T, h->closes, 1, NULL);
	kp_t_ok(T, strstr(h->last_log, "ended after") != NULL, "%s", h->last_log);
}

static void run_stop_ends_every_movie(KpT *T)
{
	static const int IDS[] = { 0, 1 };
	RunHost *h = run_host(T);
	if (!h) return;
	kp_cine_run_stop(&h->r);
	kp_t_eq(T, h->ends, 0, "no run: nothing to stop");
	kp_t_eq(T, kp_cine_run_start(&h->r, IDS, 2, 0), 1, NULL);
	run_frame(h);
	kp_cine_run_stop(&h->r);
	kp_t_ok(T, !h->r.active && h->r.player == NULL, "the run ended");
	kp_t_eq(T, h->ends, 1, NULL);
	kp_t_eq(T, h->closes, 1, NULL);
	kp_t_eq(T, h->sound_stops, 1, "only the first movie played");
	kp_cine_run_stop(&h->r);
	kp_t_eq(T, h->ends, 1, "once");
}

/* A start while movies play queues after them; a movie that does not open is passed over (logged) for the next; a
 * start that opens none, or whose file does not open, or without the pack, changes nothing. */
static void run_start_queues_and_passes_over_what_does_not_open(KpT *T)
{
	static const int FIRST[] = { 0 }, SECOND[] = { 1 }, BOTH[] = { 0, 1 }, NONE[] = { 5 };
	RunHost *h = run_host(T);
	if (!h) return;
	kp_t_eq(T, kp_cine_run_start(&h->r, NONE, 1, 0), 0, "a cinematic the pack lacks");
	kp_t_eq(T, h->opens, 0, "no file opened for it");
	kp_t_eq(T, kp_cine_run_start(&h->r, FIRST, 1, 0), 1, NULL);
	kp_t_eq(T, kp_cine_run_start(&h->r, SECOND, 1, 0), 1, "queued after the one playing");
	kp_t_ok(T, h->r.queue_len == 2 && h->begins == 1 && h->opens == 1, "the same run");
	kp_t_eq(T, playing_id(h), 0, NULL);
	kp_cine_run_stop(&h->r);
	h->m.reads = 0;
	h->m.fail_read = 1; /* the first movie's frame table */
	kp_t_eq(T, kp_cine_run_start(&h->r, BOTH, 2, 0), 1, "the second movie");
	kp_t_eq(T, playing_id(h), 1, "the first passed over");
	kp_t_eq(T, h->passed, 1, "logged");
	kp_cine_run_stop(&h->r);
	h->m.reads = 0;
	kp_t_eq(T, kp_cine_run_start(&h->r, FIRST, 1, 0), 0, "none opens");
	kp_t_eq(T, h->passed, 2, "logged");
	kp_t_ok(T, !h->r.active && h->r.file == NULL && h->begins == 2, "nothing changed, the file closed");
	h->m.fail_read = 0;
	h->fail_open = 1;
	kp_t_eq(T, kp_cine_run_start(&h->r, FIRST, 1, 0), 0, "the file does not open");
	kp_t_eq(T, h->begins, 2, NULL);
	h->fail_open = 0;
	h->r.have = 0;
	kp_t_eq(T, kp_cine_run_start(&h->r, FIRST, 1, 0), 0, "no pack");
	kp_t_eq(T, kp_cine_run_has(&h->r, 0), 0, NULL);
	h->r.have = 1;
	kp_t_ok(T, kp_cine_run_has(&h->r, 0) && !kp_cine_run_has(&h->r, 5), "the pack's movies");
}

/* A movie whose sound begins keeps to its position while it plays (the pictures wait for it, past the movie's end on
 * the wall clock); once the sound ends the wall clock goes on from where it left the movie; a sound that never began
 * leaves the movie to the wall clock at once. */
static void run_reads_the_sound_while_it_keeps_the_movie(KpT *T)
{
	static const int IDS[] = { 0 };
	RunHost *h = run_host(T);
	const KpCineClip *clip;
	uint32_t decoded;
	int pictures, k;
	if (!h) return;
	h->sound_ok = 1;
	h->sound_playing = 1;
	h->sound_ms = SOUND_AT_MS;
	kp_t_eq(T, kp_cine_run_start(&h->r, IDS, 1, 0), 1, NULL);
	kp_t_ok(T, h->r.clock.audio, "the sound keeps the clock");
	for (k = 0; k < RUN_WAIT_FRAMES; k++) run_frame(h);
	kp_t_eq(T, h->sound_reads, RUN_WAIT_FRAMES, "its position read each frame");
	kp_t_eq(T, h->r.clock.last_ms, SOUND_AT_MS, "the movie's time is the sound's");
	kp_t_eq(T, playing_id(h), 0, "the movie waits for its sound");
	clip = kp_cine_clip(h->r.player);
	decoded = kp_cine_decoded(h->r.player);
	kp_t_ok(T, decoded > 0 && kp_cine_frame_ms(clip, decoded - 1) <= SOUND_AT_MS
		&& kp_cine_frame_ms(clip, decoded) > SOUND_AT_MS, "the frames due at the sound's position, none after (%u)",
		(unsigned)decoded);
	h->sound_playing = 0; /* the sound ended */
	run_frame(h);
	kp_t_eq(T, h->r.clock.audio, 0, "then the wall clock's");
	kp_t_eq(T, h->r.clock.last_ms, SOUND_AT_MS + RUN_STEP_MS, "from where the sound left the movie");
	run_movie(T, h, 0);
	kp_t_eq(T, h->sound_reads, RUN_WAIT_FRAMES + 1, "no more reads");
	/* a sound that never began */
	h->sound_reads = 0;
	kp_t_eq(T, kp_cine_run_start(&h->r, IDS, 1, 0), 1, NULL);
	pictures = run_frame(h);
	kp_t_eq(T, h->sound_reads, 1, "its position read");
	kp_t_eq(T, h->r.clock.audio, 0, "it does not play: the wall clock's");
	pictures += run_movie(T, h, 0);
	kp_t_eq(T, pictures, FRAME_COUNT, "the movie to its end");
	kp_t_eq(T, h->sound_reads, 1, "no more reads");
}

static const KpTest TESTS[] = {
	{ "index_follows_the_pack", index_follows_the_pack, 0 },
	{ "subtitles_follow_the_movies_time", subtitles_follow_the_movies_time, 0 },
	{ "subtitles_of_a_language_replace_the_packs", subtitles_of_a_language_replace_the_packs, 0 },
	{ "subtitles_refused_when_not_right", subtitles_refused_when_not_right, 0 },
	{ "prelude_is_the_last_entry", prelude_is_the_last_entry, 0 },
	{ "frames_are_due_at_their_times", frames_are_due_at_their_times, 0 },
	{ "colour_follows_the_matrices", colour_follows_the_matrices, 0 },
	{ "srgb_movie_is_ffmpegs_picture", srgb_movie_is_ffmpegs_picture, 0 },
	{ "ycc_movie_is_ffmpegs_picture_within_a_filter", ycc_movie_is_ffmpegs_picture_within_a_filter, 0 },
	{ "ycc_movie_planes_are_ffmpegs", ycc_movie_planes_are_ffmpegs, 0 },
	{ "a_late_movie_catches_up", a_late_movie_catches_up, 0 },
	{ "a_packet_not_come_waits_and_loses_nothing", a_packet_not_come_waits_and_loses_nothing, 0 },
	{ "stream_reads_the_head_at_hand_and_the_rest_by_chunks", stream_reads_the_head_at_hand_and_the_rest_by_chunks, 0 },
	{ "open_refuses_a_table_that_does_not_fit", open_refuses_a_table_that_does_not_fit, 0 },
	{ "a_packet_that_does_not_decode_ends_with_an_error", a_packet_that_does_not_decode_ends_with_an_error, 0 },
	{ "index_refuses_a_pack_that_is_not_right", index_refuses_a_pack_that_is_not_right, 0 },
	{ "queue_takes_the_preludes_the_pack_has", queue_takes_the_preludes_the_pack_has, 0 },
	{ "clock_follows_the_sound_then_the_wall", clock_follows_the_sound_then_the_wall, 0 },
	{ "picture_gives_the_planes_and_the_matrix", picture_gives_the_planes_and_the_matrix, 0 },
	{ "open_refuses_an_empty_packet", open_refuses_an_empty_packet, 0 },
	{ "run_plays_its_movies_one_after_the_other", run_plays_its_movies_one_after_the_other, 0 },
	{ "run_press_ends_the_movie_shown", run_press_ends_the_movie_shown, 0 },
	{ "run_waits_for_its_packets", run_waits_for_its_packets, 0 },
	{ "run_ends_when_its_pack_is_gone", run_ends_when_its_pack_is_gone, 0 },
	{ "run_stop_ends_every_movie", run_stop_ends_every_movie, 0 },
	{ "run_start_queues_and_passes_over_what_does_not_open", run_start_queues_and_passes_over_what_does_not_open, 0 },
	{ "run_reads_the_sound_while_it_keeps_the_movie", run_reads_the_sound_while_it_keeps_the_movie, 0 },
};

KP_SUITE(kp_suite_cine, "test_cine", TESTS);
