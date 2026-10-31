/*
 * The cinematics' pack and its player (native/kp_app/kp_cine.c, ADR 0015), built with KP_CINEMATICS: the index, the
 * colour, the frames of a pack the converter writes (converter.kpf.fixtures: a 4:4:4 sRGB movie as the SD ones are and a
 * 4:2:0 studio-range one as the HD2 ones, 64 x 48, 12 frames, each with the frames FFmpeg decodes from the same WebM) and
 * the refusals of a pack or a packet that is not right.
 */
#include "kp_test.h"

#include "kp_cine.h"

#include <math.h>

#define PACK "tests/fixtures/generated/kpf/cine.kpd"
#define FRAMES_SRGB "tests/fixtures/generated/kpf/cine0.rgba"
#define FRAMES_YCC "tests/fixtures/generated/kpf/cine1.rgba"
#define W 64
#define H 48
#define FRAME_BYTES (W * H * 4)
#define FRAME_COUNT 12
#define YCC_MEAN_LIMIT 4 /* FFmpeg upsamples the chroma with a filter, the player copies a sample: the mean of the error */

static const char *const SRGB_FILES[] = { PACK, FRAMES_SRGB, NULL };
static const char *const YCC_FILES[] = { PACK, FRAMES_YCC, NULL };

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
	kp_cine_yuv_rgb(16, 128, 128, 601, 0, rgb);
	kp_t_ok(T, rgb[0] == 0 && rgb[1] == 0 && rgb[2] == 0, "studio black");
	kp_cine_yuv_rgb(235, 128, 128, 601, 0, rgb);
	kp_t_ok(T, rgb[0] == 255 && rgb[1] == 255 && rgb[2] == 255, "studio white");
	kp_cine_yuv_rgb(0, 128, 128, 601, 1, rgb);
	kp_t_ok(T, rgb[0] == 0 && rgb[1] == 0 && rgb[2] == 0, "full-range black");
	kp_cine_yuv_rgb(255, 128, 128, 709, 1, rgb);
	kp_t_ok(T, rgb[0] == 255 && rgb[1] == 255 && rgb[2] == 255, "full-range white");
	kp_cine_yuv_rgb(81, 90, 240, 601, 0, rgb); /* BT.601 studio red */
	kp_t_ok(T, rgb[0] >= 253 && rgb[1] <= 2 && rgb[2] <= 2, "red: %d %d %d", rgb[0], rgb[1], rgb[2]);
	kp_cine_yuv_rgb(63, 102, 240, 709, 0, rgb); /* BT.709 studio red */
	kp_t_ok(T, rgb[0] >= 253 && rgb[1] <= 2 && rgb[2] <= 2, "red 709: %d %d %d", rgb[0], rgb[1], rgb[2]);
	kp_cine_yuv_rgb(81, 90, 240, 709, 0, rgb);
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

/* The queue: each cinematic the pack has, after its prelude (when asked and the pack has it), as many as fit. */
static void queue_takes_the_preludes_the_pack_has(KpT *T)
{
	KpCineIndex ix;
	int queue[8], ids[3] = { 6, 9, 7 };
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
	ix.prelude_count = 2;
	kp_t_eq(T, kp_cine_queue(&ix, ids, 3, 1, queue, 8), 3, "9 is not in the pack, nor 7's prelude");
	kp_t_eq(T, queue[0], 2, "6 after its prelude");
	kp_t_eq(T, queue[1], 6, NULL);
	kp_t_eq(T, queue[2], 7, NULL);
	kp_t_eq(T, kp_cine_queue(&ix, ids, 3, 0, queue, 8), 2, "no preludes asked");
	kp_t_eq(T, queue[0], 6, NULL);
	kp_t_eq(T, queue[1], 7, NULL);
	kp_t_eq(T, kp_cine_queue(&ix, ids, 3, 1, queue, 1), 1, "as many as fit");
	kp_t_eq(T, queue[0], 2, NULL);
	kp_t_eq(T, kp_cine_queue(&ix, ids + 1, 1, 1, queue, 8), 0, "none the pack has");
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
	size_t size;
	const uint8_t *pack;
	Memory m;
	KpCineIndex ix;
	KpError err;
	KpCinePlayer *p;
	KpCinePicture pic;
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
			kp_cine_yuv_rgb(pic.planes[0][0], pic.planes[1][0], pic.planes[2][0], 601, 0, rgb);
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
	kp_t_eq(T, m.reads, 3, "HEAD, CLIP and PREL read once each");
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

static const KpTest TESTS[] = {
	{ "index_follows_the_pack", index_follows_the_pack, 0 },
	{ "prelude_is_the_last_entry", prelude_is_the_last_entry, 0 },
	{ "frames_are_due_at_their_times", frames_are_due_at_their_times, 0 },
	{ "colour_follows_the_matrices", colour_follows_the_matrices, 0 },
	{ "srgb_movie_is_ffmpegs_picture", srgb_movie_is_ffmpegs_picture, 0 },
	{ "ycc_movie_is_ffmpegs_picture_within_a_filter", ycc_movie_is_ffmpegs_picture_within_a_filter, 0 },
	{ "a_late_movie_catches_up", a_late_movie_catches_up, 0 },
	{ "open_refuses_a_table_that_does_not_fit", open_refuses_a_table_that_does_not_fit, 0 },
	{ "a_packet_that_does_not_decode_ends_with_an_error", a_packet_that_does_not_decode_ends_with_an_error, 0 },
	{ "index_refuses_a_pack_that_is_not_right", index_refuses_a_pack_that_is_not_right, 0 },
	{ "queue_takes_the_preludes_the_pack_has", queue_takes_the_preludes_the_pack_has, 0 },
	{ "clock_follows_the_sound_then_the_wall", clock_follows_the_sound_then_the_wall, 0 },
	{ "picture_gives_the_planes_and_the_matrix", picture_gives_the_planes_and_the_matrix, 0 },
	{ "open_refuses_an_empty_packet", open_refuses_an_empty_packet, 0 },
};

KP_SUITE(kp_suite_cine, "test_cine", TESTS);
