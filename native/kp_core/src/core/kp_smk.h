/* A Smacker decoder (plan phase 9B): the portraits and the briefing rooms' SMK files, decoded frame by frame into
 * palette indices the way Smackw32's SmackDoFrame/SmackToBuffer leave them, each frame's palette record applied to the
 * movie's palette (the screen's palette is the game's own for the portraits; the cinematics show the movie's, ADR 0018
 * stage D7), and a sound track decoded whole (kp_smk_sound: PCM as stored, or Smacker's Huffman-coded deltas).
 * Format: the SMK2/SMK4 header, the four header trees (MMAP, MCLR, FULL, TYPE: two 8-bit Huffman trees recoding a 16-bit
 * big tree with three escape values and the last-three cache), the frames' chunks (a palette record, a chunk per sound
 * track, the video); libavcodec's smacker decoder and demuxer and its smackaud decoder document the same bit streams.
 *
 * It also plays KPW1, the stand-in for an SMK that only StarCraft: Remastered's WebM copy has (ADR 0018, stage D6;
 * kp_res and converter/formats/kpw.py make it): "KPW1", u32 width, height, frames, i32 frame rate, u32 flags (the SMK
 * header's: 1 a ring movie, whose extra last frame leads back to frame 1), the 768 bytes of its screen's palette, then
 * the WebM. Frame n is the WebM's frame n (its last past its end), each pixel its colour's index in that palette
 * (kp_index_map.h), transparent (0) below alpha 128. The WebM is decoded by the codec the host gives
 * (kp_smk_set_video: libvpx, which the core does not link); without one a KPW1 does not open. */
#ifndef KP_SMK_H
#define KP_SMK_H

#include <stddef.h>
#include <stdint.h>

#include "kp_index_map.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KP_SMK_TREES 4
#define KP_SMK_TRACKS 7 /* the sound tracks a movie may have */

typedef struct {
	int32_t *values; /* SMK_NODE | right offset for a node, else the value */
	int32_t count;
	int32_t last[3];
} KpSmkTree;

typedef struct {
	const uint8_t *data;
	size_t size;
	int v4;
	int kpw;                          /* a KPW1 file */
	uint32_t width, height, frames, flags, ring;
	int32_t frame_rate;               /* the header's: > 0 ms, < 0 in 10 microsecond units, 0 the default */
	const uint8_t *frame_types;
	size_t frames_at, sizes_at, types_at;
	KpSmkTree trees[KP_SMK_TREES];
	uint32_t frame;                   /* the next frame to decode */
	size_t frame_pos;                 /* its data's offset */
	uint8_t *mem;                     /* the one allocation: the trees' entries, then the pixels */
	uint8_t *pixels;                  /* width * height indices, kept between frames */
	uint8_t *target;                  /* kp_smk_set_target: frames decode there instead (SmackToBuffer) */
	uint8_t palette[768];             /* the frame's palette as 8-bit R, G, B (black before a record; a KPW1's: none) */
	void *video;                      /* a KPW1's: the codec's movie */
	KpIndexMap map;
} KpSmk;

/* The WebM codec a KPW1 is decoded with (the host's): open a WebM in memory (it outlives the movie; NULL: cannot) with
 * its size, frame n as RGBA (width * height * 4, the last frame past the end; NULL: malformed), close. */
typedef struct {
	void *(*open)(const uint8_t *data, size_t size, uint32_t *width, uint32_t *height);
	const uint8_t *(*frame)(void *movie, uint32_t n);
	void (*close)(void *movie);
} KpSmkVideo;
/* The codec of every KPW1 opened after (set once at start; NULL: none). */
void kp_smk_set_video(const KpSmkVideo *codec);

/* Opens an SMK in memory (the bytes must outlive it): 0, or -1 for a malformed file. */
int kp_smk_open(KpSmk *s, const uint8_t *data, size_t size);
void kp_smk_close(KpSmk *s);
/* Decodes the next frame into s->pixels (SmackDoFrame) and advances (SmackNextFrame: after the last frame frame 0, or
 * for a ring movie its extra frame and then frame 1). Returns 0 or -1. */
int kp_smk_decode(KpSmk *s);
/* SmackToBuffer: the frames decode into target (width * height bytes; NULL: the decoder's own pixels). */
void kp_smk_set_target(KpSmk *s, uint8_t *target);
/* SmackGoto(0): back to the first frame, the pixels cleared. */
void kp_smk_rewind(KpSmk *s);
/* The time of a frame in 10 microsecond units (SmackOpen's +0x424). */
uint32_t kp_smk_frame_time(const KpSmk *s);

/* A sound track's format (the header's AudioRate word): its rate, 1 or 2 channels, 8 (unsigned) or 16 (signed) bits
 * a sample, whether its chunks are Huffman-coded deltas. */
typedef struct {
	uint32_t rate;
	int channels, bits, packed;
} KpSmkSoundFormat;
/* Track k's format: 0, or -1 when the movie has no such track (a rate of 0), it is Bink's sound, or a KPW1. */
int kp_smk_sound_format(const KpSmk *s, int track, KpSmkSoundFormat *out);
/* Track k's sound, every frame's chunk in turn (a ring movie's extra frame too), as interleaved PCM in *pcm (malloc'ed,
 * the caller's; 16-bit samples little endian): 0, or -1 (and none) when the track is not there or a chunk is cut short
 * or malformed. Reads the file only: the decoder's frame is left where it is. */
int kp_smk_sound(const KpSmk *s, int track, KpSmkSoundFormat *format, uint8_t **pcm, size_t *size);

#ifdef __cplusplus
}
#endif

#endif
