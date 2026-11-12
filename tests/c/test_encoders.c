/*
 * The encoder of kp_convert's movies (ADR 0019: stage C0 measured it, C4 makes the 1.17 movies with it, kp_movie.h):
 * libvpx's VP9 encoder at the cinematics' settings (ADR 0015: profile 1, 4:4:4 sRGB planes G, B, R, constant quality
 * 32, good, cpu-used 2, a key frame at most every 128 frames, one thread) run over made-up frames and decoded back:
 * what it writes decodes to the input's frames, and the same frames give the same bytes (the cache of converted files
 * relies on it). The Ogg Vorbis half is kp_convert's own since stage C1 (kp_ogg.h, test_convert.c). Built with
 * KP_ENCODERS only (the host's tools, CMakeLists.txt); elsewhere its test skips.
 */
#include "kp_test.h"

#ifdef KP_ENCODERS
#include <math.h>

#include "kp_movie.h"
#include "kp_webm.h"
#include "vpx/vp8dx.h"
#include "vpx/vpx_decoder.h"

#define W 64
#define H 48
#define PLANE (W * H)
#define MOVIE_FRAMES 10
#define RATE_NUM 50000 /* the 1.17 movies' rate: 50000/3333 frames a second */
#define RATE_DEN 3333
#define VP9_PSNR_FLOOR 45.0 /* 50.4 dB measured on this picture (macOS arm64; Terran2 at the same settings 41.4, ADR 0019) */

/* Frame f's planes G, B, R: a gradient, a square that moves, a few flat colours as a palette's would be. */
static void movie_frame(uint32_t f, uint8_t *gbr)
{
	uint32_t x, y;
	for (y = 0; y < H; y++)
		for (x = 0; x < W; x++) {
			size_t i = (size_t)y * W + x;
			int inside = x >= 8 + 3 * f && x < 24 + 3 * f && y >= 12 && y < 28;
			gbr[i] = (uint8_t)(inside ? 200 : 4 * x);
			gbr[PLANE + i] = (uint8_t)(inside ? 40 : 5 * y);
			gbr[2 * PLANE + i] = (uint8_t)(inside ? 90 : ((x / 16 + y / 16) & 1) * 128);
		}
}

static int frame_of(void *ctx, uint32_t n, const uint8_t *planes[3], int strides[3])
{
	const uint8_t *p = (const uint8_t *)ctx + (size_t)n * 3 * PLANE;
	int k;
	for (k = 0; k < 3; k++) {
		planes[k] = p + (size_t)k * PLANE;
		strides[k] = W;
	}
	return 0;
}

static void video_free(void *v)
{
	kp_movie_video_free((KpMovieVideo *)v);
	free(v);
}

static KpMovieVideo *vp9_encode(KpT *T, uint8_t *frames)
{
	KpMovieVideo *v = (KpMovieVideo *)calloc(1, sizeof *v);
	char err[256];
	kp_t_ok(T, v != NULL, "memory");
	kp_t_defer(T, video_free, v);
	kp_t_ok(T, kp_movie_vp9(W, H, RATE_NUM, RATE_DEN, MOVIE_FRAMES, frame_of, frames, v, err, sizeof err) == 0, "%s", err);
	return v;
}

static void vp9_round_trip(KpT *T)
{
	uint8_t *frames = (uint8_t *)malloc((size_t)MOVIE_FRAMES * 3 * PLANE);
	const KpMovieVideo *a, *b;
	vpx_codec_ctx_t dec;
	double sse = 0, psnr;
	uint32_t f, decoded = 0;
	size_t at = 0;
	kp_t_ok(T, frames != NULL, "memory");
	kp_t_defer(T, free, frames);
	for (f = 0; f < MOVIE_FRAMES; f++) movie_frame(f, frames + (size_t)f * 3 * PLANE);
	a = vp9_encode(T, frames);
	b = vp9_encode(T, frames);
	kp_t_eq(T, a->count, MOVIE_FRAMES, "a packet a frame");
	kp_t_ok(T, a->sizes[0] & KP_WEBM_KEY, "the first a key frame");
	kp_t_ok(T, a->size == b->size && memcmp(a->packets, b->packets, a->size) == 0, "the same frames give the same bytes");
	kp_t_eq(T, vpx_codec_dec_init(&dec, vpx_codec_vp9_dx(), NULL, 0), VPX_CODEC_OK, "the decoder opens");
	for (f = 0; f < a->count; f++) {
		uint32_t size = a->sizes[f] & ~KP_WEBM_KEY;
		vpx_codec_iter_t it = NULL;
		vpx_image_t *im;
		kp_t_eq(T, vpx_codec_decode(&dec, a->packets + at, size, NULL, 0), VPX_CODEC_OK, "packet %u decodes", f);
		at += size;
		while ((im = vpx_codec_get_frame(&dec, &it)) != NULL) {
			const uint8_t *src = frames + (size_t)decoded * 3 * PLANE;
			int p;
			uint32_t x, y;
			kp_t_ok(T, decoded < MOVIE_FRAMES, "no more frames than were encoded");
			kp_t_ok(T, im->fmt == VPX_IMG_FMT_I444 && im->cs == VPX_CS_SRGB && im->d_w == W && im->d_h == H,
				"frame %u is 4:4:4 sRGB at its size (format %d, colour space %d)", decoded, (int)im->fmt, (int)im->cs);
			for (p = 0; p < 3; p++)
				for (y = 0; y < H; y++)
					for (x = 0; x < W; x++) {
						int d = im->planes[p][(size_t)y * (size_t)im->stride[p] + x] - src[(size_t)p * PLANE + y * W + x];
						sse += d * d;
					}
			decoded++;
		}
	}
	vpx_codec_destroy(&dec);
	kp_t_eq(T, at, a->size, "the packets add up");
	kp_t_eq(T, decoded, MOVIE_FRAMES, "every frame decodes back");
	psnr = sse == 0 ? 99.0 : 10.0 * log10(255.0 * 255.0 * 3.0 * PLANE * MOVIE_FRAMES / sse);
	kp_t_ok(T, psnr >= VP9_PSNR_FLOOR, "PSNR %.2f dB at least %.1f", psnr, VP9_PSNR_FLOOR);
}

#else
static void vp9_round_trip(KpT *T) { kp_t_skip(T, "built without KP_ENCODERS"); }
#endif

static const KpTest TESTS[] = {
	{ "vp9_round_trip", vp9_round_trip, 0 },
};

KP_SUITE(kp_suite_encoders, "test_encoders", TESTS);
