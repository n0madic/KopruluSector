/*
 * The cinematics' player (native/kp_app/kp_cine.c, ADR 0015; ADR 0019, stage C4): the index of the data's movies, the
 * colour, the frames of the fixture's WebM (converter.kpf.fixtures: a 4:4:4 sRGB movie as the SD ones are and a 4:2:0
 * studio-range one as the HD2 ones, 64 x 48, 12 frames, each with the frames FFmpeg decodes from it, the second with
 * subtitles) and the refusals of a movie or a packet that is not right; a run of the movies over a host that loads
 * them (at once, or after it has waited for them) and records what it is asked.
 */
#include "kp_test.h"

#include "kp_cine.h"
#include "kp_webm.h"

#include <math.h>
#include <stddef.h>

#define FIXTURES "tests/fixtures/generated/kpf/"
#define WEBM_SRGB FIXTURES "cine0.webm"
#define WEBM_YCC FIXTURES "cine1.webm"
#define SRT_YCC FIXTURES "cine1.srt" /* the second movie's subtitles */
#define FRAMES_SRGB FIXTURES "cine0.rgba"
#define FRAMES_YCC FIXTURES "cine1.rgba"
#define PLANES_YCC FIXTURES "cine1.yuv"
#define W 64
#define H 48
#define FRAME_BYTES (W * H * 4)
#define FRAME_COUNT 12
#define YCC_MEAN_LIMIT 4 /* FFmpeg upsamples the chroma with a filter, the player copies a sample: the mean of the error */
#define MOVIES 2 /* the fixture's: cinematic 0 (blizzard, sRGB) and 1 (starintr, YCbCr, with subtitles) */

static const char *const MOVIE_FILES[] = { WEBM_SRGB, WEBM_YCC, SRT_YCC, NULL };
static const char *const SRGB_FILES[] = { WEBM_SRGB, FRAMES_SRGB, NULL };
static const char *const YCC_FILES[] = { WEBM_YCC, SRT_YCC, FRAMES_YCC, NULL };
static const char *const YCC_PLANE_FILES[] = { WEBM_YCC, SRT_YCC, PLANES_YCC, NULL };
static const char *const MOVIE_NAMES[MOVIES] = { "smk\\blizzard", "smk\\starintr" };

static void free_movie(void *m) { kp_cine_movie_free((KpCineMovie *)m); }

/* The index's clip of fixture movie k */
static KpCineClip named(int k)
{
	KpCineClip c;
	memset(&c, 0, sizeof c);
	snprintf(c.name, sizeof c.name, "%s", MOVIE_NAMES[k]);
	c.id = (uint8_t)k;
	return c;
}

/* Fixture movie k from the WebM bytes given (the second with its subtitles): NULL with err. */
static KpCineMovie *movie_from(KpT *T, int k, const uint8_t *webm, size_t size, KpError *err)
{
	KpCineClip c = named(k);
	KpCineMovie *m = kp_cine_movie_webm(webm, size, &c, err);
	if (m && k == 1) {
		size_t srt_size;
		const uint8_t *srt = kp_t_read(T, SRT_YCC, &srt_size);
		if (kp_cine_movie_subtitles(m, srt, srt_size, err) != 0) {
			kp_cine_movie_free(m);
			return NULL;
		}
	}
	return m;
}

/* Fixture movie k, freed when the test ends. */
static KpCineMovie *movie_of(KpT *T, int k)
{
	static const char *const PATHS[MOVIES] = { WEBM_SRGB, WEBM_YCC };
	size_t size;
	const uint8_t *webm = kp_t_read(T, PATHS[k], &size);
	KpError err;
	KpCineMovie *m = movie_from(T, k, webm, size, &err);
	if (!m) kp_t_fail(T, "%s: %s", PATHS[k], err.msg);
	kp_t_defer(T, free_movie, m);
	return m;
}

/* A copy of a fixture the test may damage. */
static uint8_t *copy_of(KpT *T, const char *path, size_t *size)
{
	const uint8_t *data = kp_t_read(T, path, size);
	uint8_t *copy = (uint8_t *)malloc(*size);
	if (!copy) kp_t_fail(T, "out of memory");
	kp_t_defer(T, free, copy);
	memcpy(copy, data, *size);
	return copy;
}

/* An EBML number at p (an element's size: its marker bit dropped; an ID: kept), its bytes in *len; -1 when it does
 * not read. */
static int64_t ebml_number(const uint8_t *p, size_t left, int keep_marker, int *len)
{
	int n = 1, k;
	int64_t v;
	if (left == 0 || p[0] == 0) return -1;
	while (!(p[0] & (0x80 >> (n - 1)))) n++;
	if ((size_t)n > left) return -1;
	v = keep_marker ? p[0] : (p[0] & (0xff >> n));
	for (k = 1; k < n; k++) v = v << 8 | p[k];
	*len = n;
	return v;
}

#define EBML_CLUSTER 0x1f43b675
#define EBML_SIMPLE_BLOCK 0xa3
#define VIDEO_TRACK 0x81 /* the fixtures' first track (FFmpeg's video), as a 1-byte number */

/* The offset of the flags byte of the video's SimpleBlock `nth` (from 0; its packet follows it, its 16-bit time before
 * it) in a WebM: the clusters walked from the first one, the elements in them by their sizes. */
static size_t video_block(KpT *T, const uint8_t *webm, size_t size, int nth)
{
	static const uint8_t CLUSTER[4] = { 0x1f, 0x43, 0xb6, 0x75 };
	size_t at = 0;
	while (at + 4 <= size && memcmp(webm + at, CLUSTER, 4) != 0) at++;
	while (at < size) {
		int id_len, size_len;
		int64_t id = ebml_number(webm + at, size - at, 1, &id_len), n;
		if (id < 0) break;
		n = ebml_number(webm + at + id_len, size - at - (size_t)id_len, 0, &size_len);
		if (n < 0) break;
		at += (size_t)(id_len + size_len);
		if (id == EBML_CLUSTER) continue; /* into its blocks */
		if (id == EBML_SIMPLE_BLOCK && at + 4 <= size && webm[at] == VIDEO_TRACK && nth-- == 0) return at + 3;
		at += (size_t)n;
	}
	kp_t_fail(T, "no video block %d in the WebM", nth);
	return 0;
}

/* The index of the data's movies: a clip per movie of the catalog the data has, by its name in lower case and its
 * number in the catalog; the catalog's preludes. */
static int has_not_second(void *ctx, int k)
{
	(void)ctx;
	return k != 1;
}

static void index_follows_the_catalog(KpT *T)
{
	KpCineCatalog c;
	KpCineIndex ix;
	memset(&c, 0, sizeof c);
	snprintf(c.movies[0].name, KP_CINE_NAME, "Blizzard");
	snprintf(c.movies[1].name, KP_CINE_NAME, "starintr");
	snprintf(c.movies[2].name, KP_CINE_NAME, "Terran1");
	c.count = 3;
	c.preludes[0][0] = 2;
	c.preludes[0][1] = 0;
	c.prelude_count = 1;
	kp_cine_index_movies(&ix, 1, &c, has_not_second, NULL);
	kp_t_eq(T, ix.variant, 1, NULL);
	kp_t_eq(T, ix.count, 2, "the movies the data has");
	kp_t_eq_str(T, ix.clips[0].name, "smk\\blizzard", "lower case");
	kp_t_eq_str(T, ix.clips[1].name, "smk\\terran1", NULL);
	kp_t_eq(T, ix.clips[1].id, 2, "the catalog's number");
	kp_t_eq(T, kp_cine_find(&ix, 2), 1, NULL);
	kp_t_eq(T, kp_cine_find(&ix, 1), -1, "a movie the data lacks");
	kp_t_eq(T, ix.clips[0].frames, 0, "the movie loaded gives its frames");
	kp_t_eq(T, ix.prelude_count, 1, NULL);
	kp_t_eq(T, kp_cine_prelude(&ix, 2), 0, "the catalog's preludes");
}

/* A WebM loaded as a movie: its clip (the index's name and number, the frames' size, rate and count), its sound as an
 * Ogg stream, its subtitles. */
static void movie_follows_its_webm(KpT *T)
{
	const KpCineClip *c;
	size_t sound_size;
	const uint8_t *sound;
	int k;
	if (!kp_t_ready(T, "movie_follows_its_webm", MOVIE_FILES)) return;
	for (k = 0; k < MOVIES; k++) {
		KpCineMovie *m = movie_of(T, k);
		c = kp_cine_movie_clip(m);
		kp_t_eq_str(T, c->name, MOVIE_NAMES[k], NULL);
		kp_t_eq(T, c->id, k, NULL);
		kp_t_eq(T, c->width, W, NULL);
		kp_t_eq(T, c->height, H, NULL);
		kp_t_eq(T, c->shown_w, W, "a movie not coded 320 wide is shown as coded");
		kp_t_eq(T, c->shown_h, H, NULL);
		kp_t_eq(T, c->rate_num, 15, NULL);
		kp_t_eq(T, c->rate_den, 1, NULL);
		kp_t_eq(T, c->frames, FRAME_COUNT, NULL);
		sound = kp_cine_movie_sound(m, &sound_size);
		kp_t_ok(T, sound && sound_size == c->audio_size && memcmp(sound, "OggS", 4) == 0, "the sound is an Ogg stream");
		kp_t_eq(T, c->cue_count, k == 1 ? 2 : 0, "starintr's two subtitles");
		kp_t_eq(T, kp_cine_movie_subs(m)->count, (int)c->cue_count, NULL);
	}
}

/* A movie's subtitles (converter.kpf.fixtures' CINE_SUBTITLES) and the one shown at each time: none before the first,
 * each from its start to its end (excluded), none after the last; one that outlasts the next is shown again when the
 * next ends. */
static void subtitles_follow_the_movies_time(KpT *T)
{
	KpCineSubs *subs = (KpCineSubs *)calloc(1, sizeof *subs);
	if (!subs) kp_t_fail(T, "out of memory");
	kp_t_defer(T, free, subs);
	if (!kp_t_ready(T, "subtitles_follow_the_movies_time", MOVIE_FILES)) return;
	kp_t_eq(T, kp_cine_movie_subs(movie_of(T, 0))->count, 0, "a movie without subtitles");
	*subs = *kp_cine_movie_subs(movie_of(T, 1));
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

/* A player of fixture movie k (the test's to free). */
static KpCinePlayer *player_of(KpT *T, int k)
{
	KpError err;
	KpCinePlayer *p = kp_cine_open_movie(movie_of(T, k), &err);
	if (!p) kp_t_fail(T, "%s", err.msg);
	return p;
}

/* Plays movie k frame by frame at the times of its frames; frames are FFmpeg's pictures; the mean error per byte must
 * not pass limit (0: the same bytes). */
static void play_movie(KpT *T, int k, const char *frames_path, int limit)
{
	size_t ref_size;
	const uint8_t *ref = kp_t_read(T, frames_path, &ref_size);
	KpError err;
	KpCinePlayer *p;
	const KpCineClip *clip;
	uint32_t n;
	int worst = 0;
	kp_t_eq(T, (int64_t)ref_size, (int64_t)FRAME_BYTES * FRAME_COUNT, "FFmpeg's frames");
	p = player_of(T, k);
	clip = kp_cine_clip(p);
	for (n = 0; n < FRAME_COUNT; n++) {
		uint64_t t = kp_cine_frame_ms(clip, n), sum = 0;
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
	kp_t_eq(T, kp_cine_advance(p, kp_cine_frame_ms(clip, FRAME_COUNT - 1) + 1, &err), KP_CINE_IDLE, "the last frame is held");
	kp_t_eq(T, kp_cine_advance(p, kp_cine_frame_ms(clip, FRAME_COUNT), &err), KP_CINE_END, "the movie ends");
	kp_t_eq(T, (int64_t)kp_cine_decoded(p), FRAME_COUNT, NULL);
	kp_cine_free(p);
	kp_t_log(T, "movie %d: worst error %d", k, worst);
}

static void srgb_movie_is_ffmpegs_picture(KpT *T)
{
	if (!kp_t_ready(T, "srgb_movie_is_ffmpegs_picture", SRGB_FILES)) return;
	play_movie(T, 0, FRAMES_SRGB, 0);
}

static void ycc_movie_is_ffmpegs_picture_within_a_filter(KpT *T)
{
	if (!kp_t_ready(T, "ycc_movie_is_ffmpegs_picture_within_a_filter", YCC_FILES)) return;
	play_movie(T, 1, FRAMES_YCC, YCC_MEAN_LIMIT);
}

/* The 4:2:0 movie's planes are FFmpeg's decode byte for byte, as VP9 requires of every decoder: libvpx's C and its
 * SIMD (NEON on arm64) alike. The RGBA test above allows for the chroma's filter; this one allows nothing. */
static void ycc_movie_planes_are_ffmpegs(KpT *T)
{
	size_t ref_size, at = 0;
	const uint8_t *ref;
	KpError err;
	KpCinePlayer *p;
	KpVideoPicture pic;
	uint32_t n;
	int i, y;
	if (!kp_t_ready(T, "ycc_movie_planes_are_ffmpegs", YCC_PLANE_FILES)) return;
	ref = kp_t_read(T, PLANES_YCC, &ref_size);
	kp_t_eq(T, (int64_t)ref_size, (int64_t)(W * H + 2 * (W / 2) * (H / 2)) * FRAME_COUNT, "FFmpeg's planes");
	p = player_of(T, 1);
	for (n = 0; n < FRAME_COUNT; n++) {
		int rc = kp_cine_advance(p, kp_cine_frame_ms(kp_cine_clip(p), n), &err);
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
	size_t ref_size;
	const uint8_t *ref;
	KpError err;
	KpCinePlayer *p;
	int calls = 0, rc;
	if (!kp_t_ready(T, "a_late_movie_catches_up", SRGB_FILES)) return;
	ref = kp_t_read(T, FRAMES_SRGB, &ref_size);
	p = player_of(T, 0);
	do {
		rc = kp_cine_advance(p, kp_cine_frame_ms(kp_cine_clip(p), FRAME_COUNT - 1), &err);
		calls++;
	} while (rc == KP_CINE_FRAME && kp_cine_decoded(p) < FRAME_COUNT && calls < 10);
	kp_t_eq(T, (int64_t)kp_cine_decoded(p), FRAME_COUNT, "all the packets decoded");
	kp_t_ok(T, calls >= 2, "a few packets a call (%d calls)", calls);
	kp_t_ok(T, memcmp(kp_cine_rgba(p), ref + (size_t)(FRAME_COUNT - 1) * FRAME_BYTES, FRAME_BYTES) == 0, "the last picture");
	kp_cine_free(p);
}

/* A movie that is not right: not a WebM, cut short, a frame off its time by more than half a frame (the player keeps
 * the rate, not the times) are refused when it loads; a first frame that is not a key frame when it opens; a packet
 * that does not decode ends it with an error. */
static void a_movie_that_is_not_right_is_refused(KpT *T)
{
	size_t size;
	uint8_t *bad;
	size_t first, second, sixth;
	KpCineMovie *m;
	KpCinePlayer *p;
	KpError err;
	if (!kp_t_ready(T, "a_movie_that_is_not_right_is_refused", MOVIE_FILES)) return;
	bad = copy_of(T, WEBM_SRGB, &size);
	first = video_block(T, bad, size, 0);
	second = video_block(T, bad, size, 1);
	sixth = video_block(T, bad, size, 5);
	kp_t_ok(T, movie_from(T, 0, bad, 4, &err) == NULL, "a cut header");
	kp_t_ok(T, movie_from(T, 0, bad, first - 3, &err) == NULL, "no frame");
	memcpy(bad, "KPRF", 4);
	kp_t_ok(T, movie_from(T, 0, bad, size, &err) == NULL, "not a WebM");
	kp_t_ok(T, strstr(err.msg, "cinematics") == err.msg, "%s", err.msg);
	memcpy(bad, kp_t_read(T, WEBM_SRGB, &size), size);
	bad[sixth - 2] = 0x40; /* frame 5 at 16 s of its cluster */
	kp_t_ok(T, movie_from(T, 0, bad, size, &err) == NULL, "a frame off its time");
	kp_t_ok(T, strstr(err.msg, "from its time") != NULL, "%s", err.msg);
	memcpy(bad, kp_t_read(T, WEBM_SRGB, &size), size);
	bad[first] &= 0x7f; /* the first frame is not a key frame */
	m = movie_from(T, 0, bad, size, &err);
	if (!m) kp_t_fail(T, "%s", err.msg);
	kp_t_defer(T, free_movie, m);
	kp_t_ok(T, kp_cine_open_movie(m, &err) == NULL, "a first packet that is not a key frame");
	kp_t_ok(T, strstr(err.msg, "key frame") != NULL, "%s", err.msg);
	memcpy(bad, kp_t_read(T, WEBM_SRGB, &size), size);
	memset(bad + second + 1, 0, 16); /* the second packet loses its frame marker */
	m = movie_from(T, 0, bad, size, &err);
	if (!m) kp_t_fail(T, "%s", err.msg);
	kp_t_defer(T, free_movie, m);
	p = kp_cine_open_movie(m, &err);
	if (!p) kp_t_fail(T, "%s", err.msg);
	kp_t_eq(T, kp_cine_advance(p, 0, &err), KP_CINE_FRAME, "the key frame decodes");
	kp_t_eq(T, kp_cine_advance(p, kp_cine_frame_ms(kp_cine_clip(p), 1), &err), -1, "a packet that is not VP9");
	kp_t_ok(T, strstr(err.msg, "cinematics") == err.msg, "%s", err.msg);
	kp_cine_free(p);
}

/* The queue: each cinematic the data has, after its prelude (when asked and the data has it; not one without its
 * cinematic), as many as fit, a cinematic and its prelude both or neither, the count all of them. */
static void queue_takes_the_preludes_the_data_has(KpT *T)
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
	ix.preludes[1][1] = 5; /* a prelude the data lacks */
	ix.preludes[2][0] = 9;
	ix.preludes[2][1] = 3; /* the prelude of a cinematic the data lacks */
	ix.prelude_count = 3;
	kp_t_eq(T, kp_cine_queue(&ix, ids, 3, 1, queue, 8, &wanted), 3, "9 is not in the data (nor its prelude 3), nor 7's prelude");
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
	kp_t_eq(T, kp_cine_queue(&ix, ids + 1, 1, 1, queue, 8, NULL), 0, "none the data has");
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
}

/* The decoder's picture for the GPU: its planes and their sizes, sRGB's planes as G, B, R, and YCbCr's matrix; no
 * picture before the first frame. */
static void picture_gives_the_planes_and_the_matrix(KpT *T)
{
	KpError err;
	KpCinePlayer *p;
	KpVideoPicture pic;
	int k, ch;
	if (!kp_t_ready(T, "picture_gives_the_planes_and_the_matrix", MOVIE_FILES)) return;
	for (k = 0; k < MOVIES; k++) {
		const uint8_t *rgba;
		uint8_t rgb[3];
		p = player_of(T, k);
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

/* --- a run of movies (KpCineRun) over a host that loads them and records what it is asked, on a clock the test
 * moves --- */

#define RUN_STEP_NS 10000000u /* a frame of the host: 10 ms */
#define RUN_STEP_MS 10
#define RUN_FRAMES 400        /* more than a fixture movie's 800 ms in host frames */
#define RUN_WAIT_FRAMES 100   /* a second of the host's frames: past a fixture movie's end on the wall clock */
#define SOUND_AT_MS 300       /* a sound's position that holds the movie inside it */
#define WAIT_ALWAYS 1000000   /* the load waits for good */

typedef struct {
	KpT *T;
	KpCineRun r;
	const uint8_t *webm[MOVIES];
	size_t webm_size[MOVIES];
	uint64_t now;
	int loads, begins, ends, sounds, sound_reads, sound_stops;
	int waits;         /* the loads that answer KP_CINE_LOAD_WAIT (the movie on its way) before one gives it */
	int fail_id;       /* this cinematic does not load (-1 none) */
	int sound_ok;      /* a movie's sound begins */
	int sound_playing; /* the sound plays at sound_ms */
	int64_t sound_ms;
	int passed;        /* the movies of cinematic 0 passed over (logged) */
	char last_log[512];
	char last_ended[512]; /* the log's last line of a movie's end */
} RunHost;

static uint64_t run_now(void *ctx) { return ((RunHost *)ctx)->now; }

static void run_begin(void *ctx) { ((RunHost *)ctx)->begins++; }

static void run_end(void *ctx) { ((RunHost *)ctx)->ends++; }

static int run_load(void *ctx, const KpCineClip *clip, KpCineMovie **movie, KpError *err)
{
	RunHost *h = (RunHost *)ctx;
	h->loads++;
	if (h->waits > 0) {
		h->waits--;
		return KP_CINE_LOAD_WAIT;
	}
	if (clip->id == h->fail_id || clip->id >= MOVIES) {
		snprintf(err->msg, sizeof err->msg, "not loaded");
		return -1;
	}
	*movie = movie_from(h->T, clip->id, h->webm[clip->id], h->webm_size[clip->id], err);
	return *movie ? 0 : -1;
}

static int run_sound(void *ctx, const KpCineClip *clip, const uint8_t *data, size_t size)
{
	RunHost *h = (RunHost *)ctx;
	(void)clip;
	h->sounds += data != NULL && size > 4 && memcmp(data, "OggS", 4) == 0; /* the movie's sound, from memory */
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

static void run_log(void *ctx, const char *line)
{
	RunHost *h = (RunHost *)ctx;
	snprintf(h->last_log, sizeof h->last_log, "%s", line);
	if (strncmp(line, "cinematics: 0: ", 15) == 0) h->passed++;
	if (strstr(line, " ended after ")) snprintf(h->last_ended, sizeof h->last_ended, "%s", line);
}

static int has_all(void *ctx, int k)
{
	(void)ctx;
	return k < MOVIES;
}

/* A run's host over the fixture's movies (NULL: none, the SKIP line logged). */
static RunHost *run_host(KpT *T)
{
	static const char *const PATHS[MOVIES] = { WEBM_SRGB, WEBM_YCC };
	RunHost *h;
	KpCineHost *c;
	KpCineCatalog cat;
	int k;
	if (!kp_t_ready(T, T->test, MOVIE_FILES)) return NULL;
	h = (RunHost *)calloc(1, sizeof *h);
	if (!h) kp_t_fail(T, "out of memory");
	kp_t_defer(T, free, h);
	h->T = T;
	for (k = 0; k < MOVIES; k++) h->webm[k] = kp_t_read(T, PATHS[k], &h->webm_size[k]);
	memset(&cat, 0, sizeof cat);
	snprintf(cat.movies[0].name, KP_CINE_NAME, "blizzard");
	snprintf(cat.movies[1].name, KP_CINE_NAME, "starintr");
	cat.count = MOVIES;
	kp_cine_index_movies(&h->r.index, 0, &cat, has_all, NULL);
	h->r.have = 1;
	h->fail_id = -1;
	h->now = RUN_STEP_NS;
	c = &h->r.host;
	c->ctx = h;
	c->now_ns = run_now;
	c->begin = run_begin;
	c->end = run_end;
	c->load = run_load;
	c->sound = run_sound;
	c->sound_ms = run_sound_ms;
	c->sound_stop = run_sound_stop;
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

/* The cinematic the run is at: the one playing or the one on its way, -1 none. */
static int queued_id(const RunHost *h) { return h->r.active ? h->r.queue[h->r.queue_pos] : -1; }

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

/* The queue plays one movie after the other, each loaded as it begins and played to its end with every picture, then
 * the run ends: the host's end once; a movie that begins has no picture before its own first one. */
static void run_plays_its_movies_one_after_the_other(KpT *T)
{
	static const int IDS[] = { 0, 1 };
	RunHost *h = run_host(T);
	int k, shown = 0;
	if (!h) return;
	kp_t_eq(T, kp_cine_run_start(&h->r, IDS, 2, 0), 1, "the run begins");
	kp_t_ok(T, h->r.active && h->loads == 1 && h->begins == 1, "the first movie loaded, the host's begin");
	kp_t_eq(T, h->r.queue_len, 2, NULL);
	kp_t_eq(T, h->sounds, 1, "the first movie's sound given");
	kp_t_eq(T, run_movie(T, h, 0), FRAME_COUNT, "every picture of the first");
	kp_t_eq(T, h->loads, 2, "the second loaded as it begins");
	kp_t_eq(T, h->r.drawn_ns, 0, "the second movie has shown none yet");
	kp_t_eq(T, h->r.shown, 0, NULL);
	kp_t_eq(T, h->sound_stops, 1, "the first movie's sound released");
	kp_t_eq(T, h->r.subs.count, 2, "the second movie's subtitles");
	kp_t_eq(T, kp_cine_run_cue(&h->r), -1, "none before the first's start");
	for (k = 0; k < RUN_FRAMES && h->r.active && h->r.time_ms < 150; k++) shown += run_frame(h);
	kp_t_ok(T, h->r.time_ms >= 150 && h->r.time_ms < 300, "%llu ms", (unsigned long long)h->r.time_ms);
	kp_t_eq(T, kp_cine_run_cue(&h->r), 0, "the first subtitle, by the movie's time");
	kp_t_eq(T, h->r.movies, 2, NULL);
	kp_t_eq(T, shown + run_movie(T, h, 1), FRAME_COUNT, "every picture of the second");
	kp_t_eq(T, kp_cine_run_cue(&h->r), -1, "none after the run");
	kp_t_ok(T, !h->r.active && h->r.player == NULL && h->r.movie == NULL, "the run ended, its movies freed");
	kp_t_eq(T, h->ends, 1, "the host's end once");
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
	kp_t_ok(T, !h->r.active && h->ends == 1, "the last movie's press ends the run");
	kp_cine_run_skip(&h->r, h->now);
	kp_t_eq(T, h->ends, 1, "a press after the run: nothing");
}

/* A movie on its way (the web's fetch: KP_CINE_LOAD_WAIT) is asked for each frame, black meanwhile and its sound not
 * begun; it begins when it has come, its clock from then; a press after the wait began ends it (not one of that
 * moment); a movie that does not come is passed over for the next. */
static void run_waits_for_its_movie(KpT *T)
{
	static const int IDS[] = { 0, 1 };
	RunHost *h = run_host(T);
	uint64_t begun, waited;
	int k;
	if (!h) return;
	h->waits = 3;
	begun = h->now;
	kp_t_eq(T, kp_cine_run_start(&h->r, IDS, 2, 0), 1, "a run begins with its first movie on its way");
	kp_t_ok(T, h->r.active && h->r.loading && h->begins == 1, "the host's begin");
	kp_t_ok(T, h->r.player == NULL && h->sounds == 0, "no player, no sound yet");
	kp_t_eq(T, queued_id(h), 0, NULL);
	kp_t_eq(T, run_frame(h), 0, "no picture");
	kp_t_eq(T, run_frame(h), 0, NULL);
	kp_t_eq(T, h->loads, 3, "asked again each frame");
	kp_t_eq(T, kp_cine_run_cue(&h->r), -1, "no subtitle");
	kp_cine_run_skip(&h->r, begun);
	kp_t_eq(T, queued_id(h), 0, "a press of the moment the wait began");
	waited = h->now;
	kp_t_eq(T, run_frame(h), 0, "the movie comes: its sound and clock begin, a picture next frame");
	kp_t_ok(T, !h->r.loading && playing_id(h) == 0 && h->sounds == 1, "it plays");
	kp_t_eq(T, h->r.press_ns, 0, "a press from its first picture on");
	until_picture(T, h);
	kp_t_ok(T, h->r.time_ms <= (h->now - waited) / 1000000u, "its time from when it came (%llu ms)",
		(unsigned long long)h->r.time_ms);
	kp_cine_run_stop(&h->r);

	/* a press during the wait ends the movie on its way: the next one begins (on its way too) */
	h = run_host(T);
	if (!h) return;
	h->waits = WAIT_ALWAYS;
	kp_t_eq(T, kp_cine_run_start(&h->r, IDS, 2, 0), 1, NULL);
	for (k = 0; k < RUN_WAIT_FRAMES; k++) kp_t_eq(T, run_frame(h), 0, NULL);
	kp_t_eq(T, (int64_t)h->r.time_ms, 0, "the movie's time stands");
	kp_cine_run_skip(&h->r, h->now);
	kp_t_eq(T, queued_id(h), 1, "the next");
	kp_t_ok(T, h->r.loading && h->sound_stops == 1, "on its way; nothing to stop but the host's sound");
	kp_cine_run_skip(&h->r, h->now);
	kp_t_eq(T, queued_id(h), 1, "the same moment's other press does not end it");
	kp_cine_run_skip(&h->r, h->now + 1);
	kp_t_ok(T, !h->r.active && h->ends == 1, "a later press ends the run");
	kp_t_eq(T, h->sounds, 0, "no movie began");

	/* a movie that does not come after its wait is passed over (logged) */
	h = run_host(T);
	if (!h) return;
	h->waits = 2;
	h->fail_id = 0;
	kp_t_eq(T, kp_cine_run_start(&h->r, IDS, 2, 0), 1, NULL);
	run_frame(h);
	kp_t_eq(T, queued_id(h), 0, "still on its way");
	run_frame(h);
	kp_t_eq(T, h->passed, 1, "passed over");
	kp_t_eq(T, playing_id(h), 1, "the next plays");
	kp_cine_run_stop(&h->r);
}

/* Data the host gave up on (have 0): no more movies, none of them tried. */
static void run_ends_when_its_data_is_gone(KpT *T)
{
	static const int IDS[] = { 0, 1 };
	RunHost *h = run_host(T);
	if (!h) return;
	kp_t_eq(T, kp_cine_run_start(&h->r, IDS, 2, 0), 1, NULL);
	until_picture(T, h);
	h->r.have = 0;
	kp_cine_run_skip(&h->r, h->now);
	kp_t_ok(T, !h->r.active, "the movie queued is not loaded");
	kp_t_eq(T, h->loads, 1, NULL);
	kp_t_eq(T, h->ends, 1, NULL);
	kp_t_ok(T, strstr(h->last_log, "ended after") != NULL, "%s", h->last_log);
}

/* Stop ends the run at once (the movies queued after the one playing or on its way left), once. */
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
	kp_t_ok(T, !h->r.active && h->r.player == NULL && h->r.movie == NULL, "the run ended");
	kp_t_eq(T, h->ends, 1, NULL);
	kp_t_eq(T, h->loads, 1, "only the first movie loaded");
	kp_t_eq(T, h->sound_stops, 1, NULL);
	kp_cine_run_stop(&h->r);
	kp_t_eq(T, h->ends, 1, "once");
	h->waits = WAIT_ALWAYS;
	kp_t_eq(T, kp_cine_run_start(&h->r, IDS, 2, 0), 1, NULL);
	kp_cine_run_stop(&h->r);
	kp_t_ok(T, !h->r.active && !h->r.loading && h->ends == 2, "a movie on its way stopped");
}

/* A start while movies play queues after them; a movie that does not load is passed over (logged) for the next; a
 * start that loads none, or without the data's movies, changes nothing. */
static void run_start_queues_and_passes_over_what_does_not_load(KpT *T)
{
	static const int FIRST[] = { 0 }, SECOND[] = { 1 }, BOTH[] = { 0, 1 }, NONE[] = { 5 };
	RunHost *h = run_host(T);
	if (!h) return;
	kp_t_eq(T, kp_cine_run_start(&h->r, NONE, 1, 0), 0, "a cinematic the data lacks");
	kp_t_eq(T, h->loads, 0, "none loaded for it");
	kp_t_eq(T, kp_cine_run_start(&h->r, FIRST, 1, 0), 1, NULL);
	kp_t_eq(T, kp_cine_run_start(&h->r, SECOND, 1, 0), 1, "queued after the one playing");
	kp_t_ok(T, h->r.queue_len == 2 && h->begins == 1 && h->loads == 1, "the same run");
	kp_t_eq(T, playing_id(h), 0, NULL);
	kp_cine_run_stop(&h->r);
	h->fail_id = 0;
	kp_t_eq(T, kp_cine_run_start(&h->r, BOTH, 2, 0), 1, "the second movie");
	kp_t_eq(T, playing_id(h), 1, "the first passed over");
	kp_t_eq(T, h->passed, 1, "logged");
	kp_cine_run_stop(&h->r);
	kp_t_eq(T, kp_cine_run_start(&h->r, FIRST, 1, 0), 0, "none loads");
	kp_t_eq(T, h->passed, 2, "logged");
	kp_t_ok(T, !h->r.active && h->begins == 2, "nothing changed");
	h->fail_id = -1;
	h->r.have = 0;
	kp_t_eq(T, kp_cine_run_start(&h->r, FIRST, 1, 0), 0, "no movies");
	kp_t_eq(T, kp_cine_run_has(&h->r, 0), 0, NULL);
	h->r.have = 1;
	kp_t_ok(T, kp_cine_run_has(&h->r, 0) && !kp_cine_run_has(&h->r, 5), "the data's movies");
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
	{ "index_follows_the_catalog", index_follows_the_catalog, 0 },
	{ "movie_follows_its_webm", movie_follows_its_webm, 0 },
	{ "subtitles_follow_the_movies_time", subtitles_follow_the_movies_time, 0 },
	{ "prelude_is_the_last_entry", prelude_is_the_last_entry, 0 },
	{ "frames_are_due_at_their_times", frames_are_due_at_their_times, 0 },
	{ "colour_follows_the_matrices", colour_follows_the_matrices, 0 },
	{ "srgb_movie_is_ffmpegs_picture", srgb_movie_is_ffmpegs_picture, 0 },
	{ "ycc_movie_is_ffmpegs_picture_within_a_filter", ycc_movie_is_ffmpegs_picture_within_a_filter, 0 },
	{ "ycc_movie_planes_are_ffmpegs", ycc_movie_planes_are_ffmpegs, 0 },
	{ "a_late_movie_catches_up", a_late_movie_catches_up, 0 },
	{ "a_movie_that_is_not_right_is_refused", a_movie_that_is_not_right_is_refused, 0 },
	{ "queue_takes_the_preludes_the_data_has", queue_takes_the_preludes_the_data_has, 0 },
	{ "clock_follows_the_sound_then_the_wall", clock_follows_the_sound_then_the_wall, 0 },
	{ "picture_gives_the_planes_and_the_matrix", picture_gives_the_planes_and_the_matrix, 0 },
	{ "run_plays_its_movies_one_after_the_other", run_plays_its_movies_one_after_the_other, 0 },
	{ "run_press_ends_the_movie_shown", run_press_ends_the_movie_shown, 0 },
	{ "run_waits_for_its_movie", run_waits_for_its_movie, 0 },
	{ "run_ends_when_its_data_is_gone", run_ends_when_its_data_is_gone, 0 },
	{ "run_stop_ends_every_movie", run_stop_ends_every_movie, 0 },
	{ "run_start_queues_and_passes_over_what_does_not_load", run_start_queues_and_passes_over_what_does_not_load, 0 },
	{ "run_reads_the_sound_while_it_keeps_the_movie", run_reads_the_sound_while_it_keeps_the_movie, 0 },
};

KP_SUITE(kp_suite_cine, "test_cine", TESTS);
