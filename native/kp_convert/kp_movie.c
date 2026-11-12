#include "kp_movie.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vpx/vp8cx.h>
#include <vpx/vpx_encoder.h>

#include "kp_cine.h"
#include "kp_ogg.h"
#include "kp_smk.h"
#include "kp_wav.h"
#include "kp_webm.h"
#include "kp_webm_write.h"

#define NS_PER_S 1000000000ull
#define DRIFT_FRAMES 0.5 /* the sound's length from the video's at most, in frames (emit/cinematics.py PTS_DRIFT_FRAMES) */
#define OGG_PAGE_HEAD 27 /* an Ogg page's header before its segment table */

static int fail(char *err, size_t err_size, const char *fmt, const char *a)
{
	snprintf(err, err_size, fmt, a);
	return -1;
}

void kp_movie_video_free(KpMovieVideo *v)
{
	free(v->packets);
	free(v->sizes);
	memset(v, 0, sizeof *v);
}

/* The encoder's packets so far appended to out: 1 when one came, 0 none, -1 more than frames or out of memory. */
static int take_packets(vpx_codec_ctx_t *enc, KpMovieVideo *out, size_t *cap, uint32_t frames)
{
	vpx_codec_iter_t it = NULL;
	const vpx_codec_cx_pkt_t *pkt;
	int got = 0;
	while ((pkt = vpx_codec_get_cx_data(enc, &it)) != NULL) {
		size_t sz = pkt->data.frame.sz;
		if (pkt->kind != VPX_CODEC_CX_FRAME_PKT) continue;
		if (out->count >= frames || sz == 0 || sz > 0x7fffffffu) return -1;
		if (out->size + sz > *cap) {
			size_t grown_cap = *cap ? *cap : 1u << 20;
			uint8_t *grown;
			while (grown_cap < out->size + sz) grown_cap *= 2;
			if (!(grown = (uint8_t *)realloc(out->packets, grown_cap))) return -1;
			out->packets = grown;
			*cap = grown_cap;
		}
		memcpy(out->packets + out->size, pkt->data.frame.buf, sz);
		out->size += sz;
		out->sizes[out->count++] = (uint32_t)sz | ((pkt->data.frame.flags & VPX_FRAME_IS_KEY) ? KP_WEBM_KEY : 0u);
		got = 1;
	}
	return got;
}

int kp_movie_vp9(uint32_t width, uint32_t height, uint32_t rate_num, uint32_t rate_den, uint32_t frames, KpMovieFrame frame,
	void *ctx, KpMovieVideo *out, char *err, size_t err_size)
{
	vpx_codec_ctx_t enc;
	vpx_codec_enc_cfg_t cfg;
	vpx_image_t img;
	size_t cap = 0;
	uint32_t f;
	int got, rc = -1;
	memset(out, 0, sizeof *out);
	if (frames == 0 || width == 0 || height == 0 || rate_num == 0 || rate_den == 0 || rate_num > 0x7fffffffu
		|| rate_den > 0x7fffffffu)
		return fail(err, err_size, "%s", "a movie of no frames, size or rate");
	if (!(out->sizes = (uint32_t *)malloc((size_t)frames * sizeof *out->sizes))) return fail(err, err_size, "%s", "out of memory");
	if (vpx_codec_enc_config_default(vpx_codec_vp9_cx(), &cfg, 0) != VPX_CODEC_OK) {
		kp_movie_video_free(out);
		return fail(err, err_size, "%s", "the VP9 encoder has no defaults");
	}
	cfg.g_w = width;
	cfg.g_h = height;
	cfg.g_profile = 1; /* 4:4:4 */
	cfg.g_threads = 1;
	cfg.g_timebase.num = (int)rate_den; /* a frame a tick */
	cfg.g_timebase.den = (int)rate_num;
	cfg.rc_end_usage = VPX_Q;
	cfg.rc_target_bitrate = 0;
	cfg.kf_max_dist = KP_MOVIE_KEY_INTERVAL;
	if (vpx_codec_enc_init(&enc, vpx_codec_vp9_cx(), &cfg, 0) != VPX_CODEC_OK) {
		kp_movie_video_free(out);
		return fail(err, err_size, "the VP9 encoder does not start: %s", vpx_codec_error(&enc));
	}
	vpx_codec_control(&enc, VP8E_SET_CPUUSED, KP_MOVIE_CPU_USED);
	vpx_codec_control(&enc, VP8E_SET_CQ_LEVEL, KP_MOVIE_CQ);
	vpx_codec_control(&enc, VP8E_SET_STATIC_THRESHOLD, 0);
	vpx_codec_control(&enc, VP9E_SET_COLOR_SPACE, VPX_CS_SRGB);
	vpx_codec_control(&enc, VP9E_SET_COLOR_RANGE, VPX_CR_STUDIO_RANGE);
	for (f = 0; f <= frames; f++) {
		if (f < frames) {
			const uint8_t *planes[3];
			int strides[3], k;
			if (frame(ctx, f, planes, strides) != 0) {
				snprintf(err, err_size, "frame %u cannot be had", (unsigned)f);
				goto done;
			}
			if (f == 0) {
				vpx_img_wrap(&img, VPX_IMG_FMT_I444, width, height, 1, (unsigned char *)(uintptr_t)planes[0]);
				img.cs = VPX_CS_SRGB;
			}
			for (k = 0; k < 3; k++) {
				img.planes[k] = (unsigned char *)(uintptr_t)planes[k];
				img.stride[k] = strides[k];
			}
		}
		do { /* past the last frame: flushed until no packet comes */
			if (vpx_codec_encode(&enc, f < frames ? &img : NULL, f, 1, 0, VPX_DL_GOOD_QUALITY) != VPX_CODEC_OK) {
				snprintf(err, err_size, "frame %u does not encode: %s", (unsigned)f,
					vpx_codec_error_detail(&enc) ? vpx_codec_error_detail(&enc) : vpx_codec_error(&enc));
				goto done;
			}
			if ((got = take_packets(&enc, out, &cap, frames)) < 0) {
				snprintf(err, err_size, "more packets than frames, or out of memory");
				goto done;
			}
		} while (f == frames && got);
	}
	if (out->count != frames || !(out->sizes[0] & KP_WEBM_KEY)) {
		snprintf(err, err_size, "%u packets for %u frames, or the first no key frame", (unsigned)out->count, (unsigned)frames);
		goto done;
	}
	rc = 0;
done:
	vpx_codec_destroy(&enc);
	if (rc != 0) kp_movie_video_free(out);
	return rc;
}

/* --- a Smacker movie --- */

typedef struct {
	KpCinePlayer *player;
	const KpCineClip *clip;
	KpError e;
} Frames;

/* Frame n as the player shows it: decoded at its time, one after the other. */
static int smk_frame(void *ctx, uint32_t n, const uint8_t *planes[3], int strides[3])
{
	Frames *f = (Frames *)ctx;
	KpVideoPicture pic;
	int k;
	if (kp_cine_advance(f->player, kp_cine_frame_ms(f->clip, n), &f->e) != KP_CINE_FRAME
		|| kp_cine_decoded(f->player) != n + 1 || kp_cine_picture(f->player, &pic) != 0 || !pic.srgb)
		return -1;
	for (k = 0; k < 3; k++) {
		planes[k] = pic.planes[k];
		strides[k] = pic.strides[k];
	}
	return 0;
}

/* The granule of an Ogg stream's last page, or -1 when its pages do not add up. */
static int64_t ogg_last_granule(const uint8_t *ogg, size_t size)
{
	size_t at = 0;
	int64_t granule = -1;
	while (at + OGG_PAGE_HEAD <= size && memcmp(ogg + at, "OggS", 4) == 0) {
		size_t segments = ogg[at + 26], body = 0, k;
		int b;
		if (at + OGG_PAGE_HEAD + segments > size) return -1;
		for (k = 0; k < segments; k++) body += ogg[at + OGG_PAGE_HEAD + k];
		granule = 0;
		for (b = 7; b >= 0; b--) granule = granule << 8 | ogg[at + 6 + b];
		at += OGG_PAGE_HEAD + segments + body;
	}
	return at == size ? granule : -1;
}

/* Whether the WebM gives back what was written: the packets, their key frames, the frame's duration, the sound's
 * length. 0, or -1 with the reason. */
static int read_back(const uint8_t *webm, size_t size, const KpMovieVideo *v, uint64_t frame_ns, const KpVorbisPackets *s,
	char *err, size_t err_size)
{
	KpWebmStreams st;
	char why[160];
	uint32_t n;
	int rc = -1;
	if (kp_webm_streams(webm, size, 0, &st, why, sizeof why) != 0) return fail(err, err_size, "the WebM written does not read back: %s", why);
	if (st.count != v->count || st.packets_size != v->size || memcmp(st.packets, v->packets, v->size) != 0
		|| st.frame_ns != frame_ns) {
		fail(err, err_size, "%s", "the WebM written gives back other packets or another frame rate");
		goto done;
	}
	for (n = 0; n < st.count; n++)
		if (kp_le_u32(st.sizes + 4u * n) != v->sizes[n]) {
			fail(err, err_size, "%s", "the WebM written gives back other key frames");
			goto done;
		}
	if (s ? !st.ogg || ogg_last_granule(st.ogg, st.ogg_size) != s->granules[s->count - 1] : st.ogg != NULL) {
		fail(err, err_size, "%s", "the WebM written gives back another length of sound");
		goto done;
	}
	rc = 0;
done:
	kp_webm_streams_free(&st);
	return rc;
}

uint8_t *kp_movie_smk_webm(const uint8_t *smk, size_t size, const char *name, size_t *out_size, KpMovieInfo *info,
	char *err, size_t err_size)
{
	KpSmk header;
	KpCineClip named;
	KpCineMovie *m = NULL;
	Frames frames;
	KpMovieVideo video;
	KpVorbisPackets sound;
	KpWebmMovie wm;
	const KpCineClip *clip;
	const uint8_t *wav;
	size_t wav_size = 0;
	uint8_t *copy, *webm = NULL;
	uint64_t frame_ns;
	char why[256];
	int has_sound = 0;
	memset(info, 0, sizeof *info);
	memset(&frames, 0, sizeof frames);
	memset(&video, 0, sizeof video);
	memset(&sound, 0, sizeof sound);
	if (kp_smk_open(&header, smk, size) != 0) {
		fail(err, err_size, "%s: not a Smacker movie", name);
		return NULL;
	}
	if (header.kpw || header.ring) {
		kp_smk_close(&header);
		fail(err, err_size, "%s: not a Smacker movie that plays once", name);
		return NULL;
	}
	kp_smk_close(&header);
	memset(&named, 0, sizeof named);
	snprintf(named.name, sizeof named.name, "%s", name);
	if (!(copy = (uint8_t *)malloc(size ? size : 1))) {
		fail(err, err_size, "%s: out of memory", name);
		return NULL;
	}
	memcpy(copy, smk, size);
	if (!(m = kp_cine_movie_smk(copy, size, &named, &frames.e))) { /* the copy is the movie's, or freed */
		snprintf(err, err_size, "%s: %s", name, frames.e.msg);
		return NULL;
	}
	clip = kp_cine_movie_clip(m);
	if (!(frames.player = kp_cine_open_movie(m, &frames.e))) {
		snprintf(err, err_size, "%s: %s", name, frames.e.msg);
		goto done;
	}
	frames.clip = clip;
	frame_ns = (NS_PER_S * clip->rate_den + clip->rate_num / 2) / clip->rate_num;
	if ((wav = kp_cine_movie_sound(m, &wav_size)) != NULL) {
		KpWavInfo w;
		double heard, shown;
		if (kp_wav_info(wav, wav_size, wav_size, &w) != 0 || w.block_align == 0 || w.sample_rate == 0) {
			fail(err, err_size, "%s: its sound does not read", name);
			goto done;
		}
		heard = (double)w.data_length / w.block_align / w.sample_rate;
		shown = (double)clip->frames * clip->rate_den / clip->rate_num;
		if ((heard > shown ? heard - shown : shown - heard) * clip->rate_num / clip->rate_den > DRIFT_FRAMES) {
			snprintf(err, err_size, "%s: %.3f s of sound to %.3f s of video", name, heard, shown);
			goto done;
		}
		if (kp_vorbis_from_wav(wav, wav_size, KP_OGG_QUALITY, &sound, why, sizeof why) != 0) {
			snprintf(err, err_size, "%s: its sound: %s", name, why);
			goto done;
		}
		has_sound = 1;
	}
	if (kp_movie_vp9(clip->width, clip->height, clip->rate_num, clip->rate_den, clip->frames, smk_frame, &frames, &video, why,
			sizeof why) != 0) {
		snprintf(err, err_size, "%s: %s%s%s", name, why, frames.e.msg[0] ? ": " : "", frames.e.msg);
		goto done;
	}
	memset(&wm, 0, sizeof wm);
	wm.width = clip->width;
	wm.height = clip->height;
	wm.shown_w = clip->shown_w;
	wm.shown_h = clip->shown_h;
	wm.frame_ns = frame_ns;
	wm.packets = video.packets;
	wm.sizes = video.sizes;
	wm.frames = video.count;
	wm.sound = has_sound ? &sound : NULL;
	if (!(webm = kp_webm_write(&wm, out_size, why, sizeof why))) {
		snprintf(err, err_size, "%s: %s", name, why);
		goto done;
	}
	if (read_back(webm, *out_size, &video, frame_ns, wm.sound, why, sizeof why) != 0) {
		snprintf(err, err_size, "%s: %s", name, why);
		free(webm);
		webm = NULL;
		goto done;
	}
	info->width = clip->width;
	info->height = clip->height;
	info->frames = clip->frames;
	info->rate_num = clip->rate_num;
	info->rate_den = clip->rate_den;
	info->video_bytes = video.size;
	info->sound_bytes = has_sound ? sound.size : 0;
	info->samples = has_sound ? sound.granules[sound.count - 1] : 0;
done:
	kp_cine_free(frames.player);
	kp_cine_movie_free(m);
	kp_movie_video_free(&video);
	if (has_sound) kp_vorbis_packets_free(&sound);
	return webm;
}
