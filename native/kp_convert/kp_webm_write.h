/*
 * A movie written as WebM (ADR 0019, stage C4): the 1.17 cinematics kp_convert repack transcodes, VP9 and Vorbis in
 * Matroska's WebM profile, which the player reads back as it reads Remastered's (kp_webm_streams, over nestegg).
 *
 * The file: the EBML header (DocType webm, version 4 for DiscardPadding, read version 2), one Segment of known size
 * holding Info (TimestampScale 1 ms, Duration, the writing application), Tracks (1: V_VP9, its coded size, the shown
 * size when it differs, its frames' DefaultDuration; 2: A_VORBIS, its three headers Xiph-laced as CodecPrivate, its rate
 * and channels) and the Clusters: a cluster from the first block, from each key frame and wherever a block's time would
 * pass KP_WEBM_CLUSTER_MS from its cluster's; one packet a block in time order (a frame's time its number times the
 * frame's duration, rounded to the millisecond; a sound packet's the samples its blocks decode before it), a frame
 * before the sound at the same time. Every block is a SimpleBlock (key: a video key frame, every sound packet) but the
 * sound's last when its blocks decode past the sound's length: a BlockGroup whose DiscardPadding is that excess. No
 * Cues or SeekHead: the player reads the file whole, from its start. The same movie gives the same bytes (no dates, no
 * random UIDs).
 */
#ifndef KP_WEBM_WRITE_H
#define KP_WEBM_WRITE_H

#include <stddef.h>
#include <stdint.h>

#include "kp_ogg.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KP_WEBM_CLUSTER_MS 30000 /* a block's time from its cluster's at most (Matroska's: a signed 16-bit) */
#define KP_WEBM_APP "kp_convert"

typedef struct {
	uint32_t width, height;       /* the coded frames */
	uint32_t shown_w, shown_h;    /* the size they are shown at, 0 the coded */
	uint64_t frame_ns;            /* a frame's duration, above 0 */
	const uint8_t *packets;       /* the VP9 packets one after the other */
	const uint32_t *sizes;        /* per frame its packet's size, KP_WEBM_KEY on a key frame */
	uint32_t frames;
	const KpVorbisPackets *sound; /* NULL: none */
} KpWebmMovie;

/* The movie as WebM: malloc'd bytes and *out_size, or NULL with the reason (a frame of no bytes, the first frame not a
 * key frame, a sound of fewer than its headers, out of memory). */
uint8_t *kp_webm_write(const KpWebmMovie *m, size_t *out_size, char *err, size_t err_size);

#ifdef __cplusplus
}
#endif

#endif
