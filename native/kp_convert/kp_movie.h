/*
 * The 1.17 install's cinematics as the repack keeps them (ADR 0019, stage C4): each Smacker movie transcoded into a
 * WebM (kp_webm_write.h) as the converter's packs held them before (ADR 0015's classic source, emit/cinematics_smk.py
 * until stage C4), with libvpx and libvorbisenc instead of FFmpeg and oggenc:
 * - the video VP9 profile 1, 4:4:4 sRGB (the frames' palette colours as G, B, R planes, without loss before the
 *   encoder) at the movie's own size and exact frame rate, constant quality KP_MOVIE_CQ at --cpu-used KP_MOVIE_CPU_USED
 *   and a key frame at least each KP_MOVIE_KEY_INTERVAL frames, one thread: stage C0 measured the bytes FFmpeg's
 *   libvpx-vp9 wrote at these settings;
 * - the sound the movie's first track decoded (kp_smk) as Vorbis at oggenc's -q 3 (kp_ogg.h).
 * The frames are the player's own (kp_cine's player over the movie: what an install's SMK shows is what is encoded).
 */
#ifndef KP_MOVIE_H
#define KP_MOVIE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KP_MOVIE_CQ 32           /* FFmpeg's -crf 32 -b:v 0 */
#define KP_MOVIE_CPU_USED 2
#define KP_MOVIE_KEY_INTERVAL 128 /* libvpx's own longest distance between key frames, named so that it stays */
/* What a movie's WebM is kept in the cache by, past its SMK's SHA-256: the settings and the writer's version */
#define KP_MOVIE_SETTINGS "vp9-q32-c2-k128-vorbis-q3-webm2" /* webm2: a sound block's time its first sample's */

/* A movie's VP9 packets: one a frame. */
typedef struct {
	uint8_t *packets; /* one after the other */
	size_t size;
	uint32_t *sizes;  /* per frame its packet's size, KP_WEBM_KEY on a key frame */
	uint32_t count;
} KpMovieVideo;

/* The picture of frame n as its G, B and R planes (each of the movie's width and height, rows top down, strides in
 * bytes): 0, or -1 when it cannot be had. */
typedef int (*KpMovieFrame)(void *ctx, uint32_t n, const uint8_t *planes[3], int strides[3]);

/* frames pictures of width x height at rate_num / rate_den frames a second as VP9 at the cinematics' settings: 0, or -1
 * with the reason (the encoder refuses, a frame cannot be had, it gives another count of packets than of frames, the
 * first is no key frame). */
int kp_movie_vp9(uint32_t width, uint32_t height, uint32_t rate_num, uint32_t rate_den, uint32_t frames, KpMovieFrame frame,
	void *ctx, KpMovieVideo *out, char *err, size_t err_size);
void kp_movie_video_free(KpMovieVideo *v);

/* What a transcode wrote */
typedef struct {
	uint32_t width, height, frames, rate_num, rate_den;
	uint64_t video_bytes, sound_bytes; /* the VP9 packets', the Vorbis packets' (their headers too) */
	int64_t samples;                   /* the sound's (0: none) */
} KpMovieInfo;

/* A Smacker movie (name: its clip's, "smk\\blizzard", in the messages) as WebM: malloc'd bytes, *out_size and info, or
 * NULL with the reason: not a Smacker movie that plays once (a KPW1, a ring movie), its sound and video of lengths
 * more than half a frame apart, a frame or the sound that does not decode or encode, or a WebM that the player's
 * reader does not give back as written (its packets, its sound's length). */
uint8_t *kp_movie_smk_webm(const uint8_t *smk, size_t size, const char *name, size_t *out_size, KpMovieInfo *info,
	char *err, size_t err_size);

#ifdef __cplusplus
}
#endif

#endif
