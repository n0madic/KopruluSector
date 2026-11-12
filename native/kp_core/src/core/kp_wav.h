/*
 * A WAV's header as the original reads it (ParseWaveFile 0x49d830, FindRiffChunk 0x49d7c0; scdata/formats/wav.py):
 * "fmt " from byte 12 (at least 16 bytes, inside the file), then "data" after it, whose size must fit; chunks are not
 * padded to even sizes and any format tag is accepted. The sounds' records and the music's lengths (ADR 0018, stage
 * D3) read it from a file's first bytes.
 */
#ifndef KP_WAV_H
#define KP_WAV_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	uint16_t format_tag, channels;
	uint32_t sample_rate, avg_bytes_per_sec;
	uint16_t block_align, bits_per_sample;
	uint32_t data_offset, data_length;
} KpWavInfo;

/* The header from the first `have` bytes of a `total`-byte file: 0; -1 when the original rejects the file; -2 when a
 * chunk header lies past the bytes read (read more). */
int kp_wav_info(const uint8_t *d, size_t have, uint64_t total, KpWavInfo *w);

#ifdef __cplusplus
}
#endif

#endif
