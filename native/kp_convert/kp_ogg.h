/*
 * A WAV as Ogg Vorbis (ADR 0019): libvorbisenc at oggenc's settings, so that the repack's sounds are what oggenc
 * wrote (stage C0 measured: the same pages, granules and packets); or as those packets alone (the movies' sound,
 * stage C4).
 */
#ifndef KP_OGG_H
#define KP_OGG_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* oggenc -q 3: the repack's quality (VBR, libvorbisenc's 0.3) */
#define KP_OGG_QUALITY 0.3f

/* The Ogg stream serial of a normalized name ("sound/misc/buzz.wav"): its CRC-32 with the top bit clear (oggenc's
 * -s; it picks a random one otherwise). */
uint32_t kp_ogg_serial(const char *normalized_name);

/* The Ogg Vorbis of a WAV the original reads (kp_wav_info) whose samples are 8-bit unsigned or 16-bit or 24-bit signed
 * PCM, mono or stereo, as oggenc reads them: malloc'd bytes and *out_size, or NULL with the reason (another format; out of memory).
 * The same input gives the same bytes. */
uint8_t *kp_ogg_from_wav(const uint8_t *wav, size_t size, float quality, uint32_t serial, size_t *out_size, char *err,
	size_t err_size);

/* A WAV's sound as the Vorbis packets kp_ogg_from_wav puts in its pages, for a container of its own (the movies' WebM,
 * kp_webm_write.h): the three headers, then the sound's, each with its granule position (the samples decoded once it
 * is: the last one's is the sound's length) and its block size. */
typedef struct {
	uint8_t *data;      /* the packets one after the other */
	size_t size;
	size_t *ends;       /* packet k is data[k ? ends[k - 1] : 0, ends[k]) */
	int64_t *granules;  /* per packet: -1 a header's */
	uint32_t *blocks;   /* per packet its block's samples (vorbis_packet_blocksize): 0 a header's */
	int count;          /* the headers too */
	uint32_t rate;
	int channels;
} KpVorbisPackets;

#define KP_VORBIS_HEADERS 3

/* 0, or -1 with the reason (and nothing kept) as kp_ogg_from_wav. The same input gives the same packets. */
int kp_vorbis_from_wav(const uint8_t *wav, size_t size, float quality, KpVorbisPackets *out, char *err, size_t err_size);
void kp_vorbis_packets_free(KpVorbisPackets *p);

/* A language's speech as the repack keeps it (ADR 0019, stage C2; the rate of the 1.17 speech): a WAV kp_ogg_from_wav
 * takes or an Ogg Vorbis, decoded, its channels averaged, twice KP_OGG_SPEECH_RATE halved to it (another rate stays),
 * encoded as kp_ogg_from_wav does. malloc'd bytes and *out_size, or NULL with the reason. */
#define KP_OGG_SPEECH_RATE 22050
uint8_t *kp_ogg_speech(const uint8_t *data, size_t size, float quality, uint32_t serial, size_t *out_size, char *err,
	size_t err_size);

#ifdef __cplusplus
}
#endif

#endif
