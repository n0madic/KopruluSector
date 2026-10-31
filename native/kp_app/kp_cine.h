/*
 * The cinematics' pack (generated/video/cinematics-<sd|hd2>.kpd, kind CINE, docs/formats/packs.md; ADR 0015) and its
 * player: the index read from the file's head and its small sections, and a player that decodes one movie's VP9
 * packets with libvpx into RGBA frames at the times the host asks for. Headless: the host reads the file through a
 * callback and plays the movie's Ogg Vorbis (kp_cine_audio) with its own audio; it asks for the picture by the clock
 * (the sound's position). Built only with KP_CINEMATICS (libvpx, `tools/toolchain/fetch.sh vpx`).
 */
#ifndef KP_CINE_H
#define KP_CINE_H

#include <stddef.h>
#include <stdint.h>

#include "kp_kpf.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KP_CINE_KIND "CINE"
#define KP_CINE_SCHEMA 1 /* converter/emit/cinematics.py's SCHEMA */
#define KP_CINE_CLIPS 32
#define KP_CINE_PRELUDES 16
#define KP_CINE_HEAD 4096 /* the file's first bytes the index needs: the header and the section table */
#define KP_CINE_MAX_SIDE 4096 /* the largest coded width or height a clip may have */
#define KP_CINE_MAX_RATE_TERM 1000000u /* the largest frame rate numerator or denominator (the times' sums stay in 64 bits) */

typedef struct {
	char name[65]; /* "smk\<name>", lower case */
	uint8_t id; /* the original's cinematic number (0x508da8's index, 0..27) */
	uint16_t width, height; /* the coded frames */
	uint16_t shown_w, shown_h; /* the size they are shown at (an SD movie coded 320 wide is shown 640: a pixel of 2:1) */
	uint32_t rate_num, rate_den; /* frames per second */
	uint32_t frames;
	uint64_t video_offset; /* the packets, one after the other, in the file */
	uint32_t video_size;
	uint64_t frame_table; /* the file offset of the frames' u32 sizes (bit 31: a key frame) */
	uint64_t audio_offset; /* the movie's Ogg Vorbis */
	uint32_t audio_size; /* 0: no sound */
} KpCineClip;

typedef struct {
	int variant; /* 0 SD, 1 HD2 */
	KpCineClip clips[KP_CINE_CLIPS];
	int count;
	uint8_t preludes[KP_CINE_PRELUDES][2]; /* {cinematic, prelude}: the prelude is played before the cinematic */
	int prelude_count;
} KpCineIndex;

/* Reads size bytes at offset of the file into out; 0 on success. */
typedef int (*KpCineRead)(void *ctx, uint64_t offset, uint8_t *out, size_t size);

/* The index of the pack of file_size bytes whose first head_size are in head (the sections there are taken from it,
 * the others read); -1 with err (and no clips) when it is not a CINE container of KP_CINE_SCHEMA, a section is missing
 * or cut short, a clip lies outside DATA or FRMS, has a side of 0 or above KP_CINE_MAX_SIDE, a rate term of 0 or above
 * KP_CINE_MAX_RATE_TERM, or a clip number repeats. */
int kp_cine_index(KpCineIndex *ix, const uint8_t *head, size_t head_size, size_t file_size, KpCineRead read, void *ctx,
	KpError *err);

/* The clip of cinematic id (the original's number), or -1. */
int kp_cine_find(const KpCineIndex *ix, int id);

/* The prelude of cinematic id (PlayCinematicWithPrelude 0x458fc0 scans its table from the last entry to the first), or -1. */
int kp_cine_prelude(const KpCineIndex *ix, int id);

/* The time frame n is due at, in milliseconds (rounded up: at that time kp_cine_advance has decoded frame n). */
uint64_t kp_cine_frame_ms(const KpCineClip *clip, uint32_t n);

/* The movies cinematics ids[0..n) play, one after the other, into queue (at most cap): each cinematic the index has,
 * after its prelude (kp_cine_prelude) when preludes is set and the index has that too; their count. */
int kp_cine_queue(const KpCineIndex *ix, const int *ids, int n, int preludes, int *queue, int cap);

#define KP_CINE_AUDIO_WAIT_MS 300 /* a sound that has not begun by then leaves the movie to the wall clock until it does */
#define KP_CINE_CLOCK_GAP_MS 250  /* a longer pause between two frames (the app in the background) is not the movie's time */

/* A movie's clock: the sound's position while it plays (also when it began late: the pictures wait for it), else the
 * wall clock from where the sound left the movie or from its begin; never backwards. */
typedef struct {
	uint64_t t0;                            /* the ticks when the movie began */
	int audio;                              /* the movie has a sound that may keep its clock */
	uint64_t wall_base_ms, wall_base_ticks; /* the wall clock's start: where the sound left the movie, or its begin */
	uint64_t last_ms, last_ticks;           /* the movie's time and the ticks of the frame that asked for it */
} KpCineClock;

void kp_cine_clock_begin(KpCineClock *c, uint64_t ticks, int audio);
/* The movie's time at ticks (milliseconds): playing, whether the sound plays, and sound_ms its position (0 or less:
 * none yet); read only while c->audio is set (it is cleared once the sound has ended or never began). A pause of more
 * than KP_CINE_CLOCK_GAP_MS since the last call does not count. */
uint64_t kp_cine_clock_ms(KpCineClock *c, uint64_t ticks, int playing, int64_t sound_ms);

/* The studio-range (limited: 16..235, chroma 16..240) or full-range YCbCr of a pixel as 8-bit RGB; matrix 601 or 709. */
void kp_cine_yuv_rgb(int y, int cb, int cr, int matrix, int full_range, uint8_t rgb[3]);

typedef struct KpCinePlayer KpCinePlayer;

enum { KP_CINE_IDLE, KP_CINE_FRAME, KP_CINE_END };

/* A player of clip k of the index: the frame table is read, the decoder started. NULL with err when the clip cannot be
 * read, its table disagrees with its size (the packets do not add up to video_size, the first is not a key frame), or the
 * decoder does not start. The index and the callback must outlive it. */
KpCinePlayer *kp_cine_open(const KpCineIndex *ix, int k, KpCineRead read, void *ctx, KpError *err);
void kp_cine_free(KpCinePlayer *p);

/* Brings the picture to time_ms: decodes the packets due (all of them, a little at a time: a movie behind the clock
 * catches up over several calls), KP_CINE_FRAME when a new picture is there (kp_cine_picture, kp_cine_rgba),
 * KP_CINE_IDLE when it is not, KP_CINE_END once the last frame's time has passed, -1 with err on a packet that cannot
 * be read or decoded or a picture that is not 8-bit planar: 4:4:4 (sRGB or YCbCr), 4:4:0, 4:2:2 or 4:2:0. */
int kp_cine_advance(KpCinePlayer *p, uint64_t time_ms, KpError *err);

/* The decoder's picture as its planes, for a host that converts it itself (the GPU): Y, Cb, Cr, or G, B, R for an
 * sRGB picture (the SD movies' 4:4:4), each row 0 on top. */
typedef struct {
	const uint8_t *planes[3];
	int32_t strides[3];
	int32_t width, height;           /* the luma plane's: the clip's size */
	int32_t chroma_w, chroma_h;      /* the other two planes' */
	int srgb;                        /* the planes are G, B, R */
	/* YCbCr in 0..1 (a sample / 255) to RGB: l = (y - y_low) * y_scale, then r = l + cr_r * (cr - 128/255),
	 * g = l - cb_g * (cb - 128/255) - cr_g * (cr - 128/255), b = l + cb_b * (cb - 128/255); kp_cine_rgba's matrix */
	float y_low, y_scale, cr_r, cb_g, cr_g, cb_b;
} KpCinePicture;

/* The picture of the last KP_CINE_FRAME into out, valid until the next kp_cine_advance; -1 when a packet decoded
 * since gave none. */
int kp_cine_picture(const KpCinePlayer *p, KpCinePicture *out);
/* The same picture as RGBA (width x height, 4 bytes a pixel, opaque) converted on the CPU at the first call (the checks'
 * reference and pictures); NULL when there is none. */
const uint8_t *kp_cine_rgba(KpCinePlayer *p);
const KpCineClip *kp_cine_clip(const KpCinePlayer *p);
uint32_t kp_cine_decoded(const KpCinePlayer *p); /* the packets decoded so far */

#ifdef __cplusplus
}
#endif

#endif
