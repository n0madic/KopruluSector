/*
 * The encoder kp_convert's movies will build on (ADR 0019, stage C0; C4 makes the movies of it): libvpx's VP9 encoder
 * with the cinematics' settings (ADR 0015: profile 1, 4:4:4 sRGB planes G, B, R, constant quality 32, good, cpu-used 2,
 * a key frame at most every 128 frames, one thread) run over made-up frames and decoded back: what it writes decodes to
 * the input's frames, and the same frames give the same bytes (the cache of converted files relies on it). The Ogg
 * Vorbis half is kp_convert's own since stage C1 (kp_ogg.h, test_convert.c). Built with KP_ENCODERS only (the host's
 * tools, CMakeLists.txt); elsewhere its test skips.
 */
#include "kp_test.h"

#ifdef KP_ENCODERS
#include <math.h>

#include "vpx/vp8cx.h"
#include "vpx/vp8dx.h"
#include "vpx/vpx_decoder.h"
#include "vpx/vpx_encoder.h"

#define W 64
#define H 48
#define PLANE (W * H)
#define MOVIE_FRAMES 10
#define VP9_CQ 32
#define VP9_CPU_USED 2
#define VP9_KEY_INTERVAL 128
#define VP9_PSNR_FLOOR 45.0 /* 50.4 dB measured on this picture (macOS arm64; Terran2 at the same settings 41.4, ADR 0019) */

typedef struct {
	uint8_t *data;
	size_t size, cap;
	uint32_t frames;
	size_t at[MOVIE_FRAMES + 1]; /* each frame packet's offset; the last entry the end */
} Out;

static void out_free(void *p)
{
	free(((Out *)p)->data);
	free(p);
}

static Out *out_new(KpT *T)
{
	Out *o = (Out *)calloc(1, sizeof *o);
	kp_t_ok(T, o != NULL, "memory");
	kp_t_defer(T, out_free, o);
	return o;
}

static void out_put(KpT *T, Out *o, const void *data, size_t size)
{
	if (o->size + size > o->cap) {
		size_t cap = o->cap ? o->cap : 4096;
		uint8_t *grown;
		while (cap < o->size + size) cap *= 2;
		grown = (uint8_t *)realloc(o->data, cap);
		kp_t_ok(T, grown != NULL, "memory");
		o->data = grown;
		o->cap = cap;
	}
	memcpy(o->data + o->size, data, size);
	o->size += size;
}

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

static void vp9_encode(KpT *T, const uint8_t *frames, Out *o)
{
	vpx_codec_ctx_t enc;
	vpx_codec_enc_cfg_t cfg;
	vpx_image_t img;
	uint32_t f;
	kp_t_eq(T, vpx_codec_enc_config_default(vpx_codec_vp9_cx(), &cfg, 0), VPX_CODEC_OK, "the encoder's defaults");
	cfg.g_w = W;
	cfg.g_h = H;
	cfg.g_profile = 1;
	cfg.g_threads = 1;
	cfg.g_timebase.num = 3333; /* the 1.17 movies' rate: 50000/3333 frames a second */
	cfg.g_timebase.den = 50000;
	cfg.rc_end_usage = VPX_Q;
	cfg.kf_max_dist = VP9_KEY_INTERVAL;
	kp_t_eq(T, vpx_codec_enc_init(&enc, vpx_codec_vp9_cx(), &cfg, 0), VPX_CODEC_OK, "the encoder opens");
	vpx_codec_control(&enc, VP8E_SET_CPUUSED, VP9_CPU_USED);
	vpx_codec_control(&enc, VP8E_SET_CQ_LEVEL, VP9_CQ);
	vpx_codec_control(&enc, VP9E_SET_COLOR_SPACE, VPX_CS_SRGB);
	vpx_img_wrap(&img, VPX_IMG_FMT_I444, W, H, 1, (unsigned char *)frames);
	img.cs = VPX_CS_SRGB;
	for (f = 0; f <= MOVIE_FRAMES; f++) {
		int got;
		if (f < MOVIE_FRAMES) {
			const uint8_t *p = frames + (size_t)f * 3 * PLANE;
			img.planes[0] = (unsigned char *)p;
			img.planes[1] = (unsigned char *)p + PLANE;
			img.planes[2] = (unsigned char *)p + 2 * PLANE;
			img.stride[0] = img.stride[1] = img.stride[2] = W;
		}
		do { /* past the last frame the encoder is drained: it holds frames back for its alternate references */
			const vpx_codec_cx_pkt_t *pkt;
			vpx_codec_iter_t it = NULL;
			kp_t_eq(T, vpx_codec_encode(&enc, f < MOVIE_FRAMES ? &img : NULL, f, 1, 0, VPX_DL_GOOD_QUALITY),
				VPX_CODEC_OK, "frame %u encodes", f);
			got = 0;
			while ((pkt = vpx_codec_get_cx_data(&enc, &it)) != NULL) {
				if (pkt->kind != VPX_CODEC_CX_FRAME_PKT) continue;
				kp_t_ok(T, o->frames < MOVIE_FRAMES, "no more packets than frames");
				o->at[o->frames++] = o->size;
				out_put(T, o, pkt->data.frame.buf, pkt->data.frame.sz);
				got = 1;
			}
		} while (f == MOVIE_FRAMES && got);
	}
	o->at[o->frames] = o->size;
	vpx_codec_destroy(&enc);
}

static void vp9_round_trip(KpT *T)
{
	uint8_t *frames = (uint8_t *)malloc((size_t)MOVIE_FRAMES * 3 * PLANE);
	Out *a = out_new(T), *b = out_new(T);
	vpx_codec_ctx_t dec;
	double sse = 0, psnr;
	uint32_t f, decoded = 0;
	kp_t_ok(T, frames != NULL, "memory");
	kp_t_defer(T, free, frames);
	for (f = 0; f < MOVIE_FRAMES; f++) movie_frame(f, frames + (size_t)f * 3 * PLANE);
	vp9_encode(T, frames, a);
	vp9_encode(T, frames, b);
	kp_t_eq(T, a->frames, MOVIE_FRAMES, "a packet a frame");
	kp_t_ok(T, a->size == b->size && memcmp(a->data, b->data, a->size) == 0, "the same frames give the same bytes");
	kp_t_eq(T, vpx_codec_dec_init(&dec, vpx_codec_vp9_dx(), NULL, 0), VPX_CODEC_OK, "the decoder opens");
	for (f = 0; f < a->frames; f++) {
		vpx_codec_iter_t it = NULL;
		vpx_image_t *im;
		kp_t_eq(T, vpx_codec_decode(&dec, a->data + a->at[f], (unsigned)(a->at[f + 1] - a->at[f]), NULL, 0),
			VPX_CODEC_OK, "packet %u decodes", f);
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
