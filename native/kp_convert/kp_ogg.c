#include "kp_ogg.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vorbis/vorbisenc.h>

#include "kp_kpf.h"
#include "kp_wav.h"

#define WAV_PCM 1
#define FEED 1024 /* the samples given to the encoder at a time (oggenc's READSIZE) */

typedef struct {
	uint8_t *data;
	size_t size, cap;
	int failed;
} Buffer;

static void put(Buffer *b, const void *data, size_t size)
{
	if (b->failed) return;
	if (b->size + size > b->cap) {
		size_t cap = b->cap ? b->cap : 65536;
		uint8_t *grown;
		while (cap < b->size + size) cap *= 2;
		if (!(grown = (uint8_t *)realloc(b->data, cap))) {
			b->failed = 1;
			return;
		}
		b->data = grown;
		b->cap = cap;
	}
	memcpy(b->data + b->size, data, size);
	b->size += size;
}

static void put_page(Buffer *b, const ogg_page *og)
{
	put(b, og->header, (size_t)og->header_len);
	put(b, og->body, (size_t)og->body_len);
}

uint32_t kp_ogg_serial(const char *normalized_name)
{
	return kp_crc32((const uint8_t *)normalized_name, strlen(normalized_name)) & 0x7fffffffu;
}

/* Sample k of channel c as oggenc reads it: 8-bit unsigned around 128, 16-bit signed little-endian. */
static float sample(const uint8_t *pcm, int bits, int channels, size_t k, int c)
{
	size_t at = k * (size_t)channels + (size_t)c;
	if (bits == 8) return (float)((int)pcm[at] - 128) / 128.0f;
	return (float)(int16_t)(uint16_t)(pcm[2 * at] | pcm[2 * at + 1] << 8) / 32768.0f;
}

uint8_t *kp_ogg_from_wav(const uint8_t *wav, size_t size, float quality, uint32_t serial, size_t *out_size, char *err,
	size_t err_size)
{
	KpWavInfo w;
	vorbis_info vi;
	vorbis_comment vc;
	vorbis_dsp_state vd;
	vorbis_block vb;
	ogg_stream_state os;
	ogg_page og;
	ogg_packet op, head, comment, code;
	Buffer b;
	const uint8_t *pcm;
	size_t frames, k;
	int bytes, eos = 0;
	if (kp_wav_info(wav, size, size, &w) != 0) {
		snprintf(err, err_size, "not a WAV the game reads");
		return NULL;
	}
	if (w.format_tag != WAV_PCM || (w.bits_per_sample != 8 && w.bits_per_sample != 16) || w.channels < 1 || w.channels > 2
		|| w.sample_rate == 0) {
		snprintf(err, err_size, "a WAV of format %u, %u bits, %u channels: not PCM the encoder takes", w.format_tag,
			w.bits_per_sample, w.channels);
		return NULL;
	}
	bytes = w.bits_per_sample / 8;
	pcm = wav + w.data_offset;
	frames = w.data_length / ((size_t)bytes * w.channels);
	vorbis_info_init(&vi);
	if (vorbis_encode_init_vbr(&vi, w.channels, (long)w.sample_rate, quality) != 0) {
		vorbis_info_clear(&vi);
		snprintf(err, err_size, "the encoder refuses %u Hz, %u channels", w.sample_rate, w.channels);
		return NULL;
	}
	memset(&b, 0, sizeof b);
	vorbis_comment_init(&vc);
	vorbis_analysis_init(&vd, &vi);
	vorbis_block_init(&vd, &vb);
	ogg_stream_init(&os, (int)serial);
	vorbis_analysis_headerout(&vd, &vc, &head, &comment, &code);
	ogg_stream_packetin(&os, &head);
	ogg_stream_packetin(&os, &comment);
	ogg_stream_packetin(&os, &code);
	while (ogg_stream_flush(&os, &og)) put_page(&b, &og); /* the headers end their page */
	for (k = 0; !eos; k += FEED) {
		size_t n = k < frames ? (frames - k < FEED ? frames - k : FEED) : 0, i;
		int c;
		if (n == 0) {
			vorbis_analysis_wrote(&vd, 0); /* the end: the last block is encoded and the last page marked */
		} else {
			float **buf = vorbis_analysis_buffer(&vd, (int)n);
			for (c = 0; c < w.channels; c++)
				for (i = 0; i < n; i++) buf[c][i] = sample(pcm, w.bits_per_sample, w.channels, k + i, c);
			vorbis_analysis_wrote(&vd, (int)n);
		}
		while (vorbis_analysis_blockout(&vd, &vb) == 1) {
			vorbis_analysis(&vb, NULL);
			vorbis_bitrate_addblock(&vb);
			while (vorbis_bitrate_flushpacket(&vd, &op)) {
				ogg_stream_packetin(&os, &op);
				while (ogg_stream_pageout(&os, &og)) put_page(&b, &og);
			}
		}
		eos = n == 0;
	}
	while (ogg_stream_flush(&os, &og)) put_page(&b, &og);
	ogg_stream_clear(&os);
	vorbis_block_clear(&vb);
	vorbis_dsp_clear(&vd);
	vorbis_comment_clear(&vc);
	vorbis_info_clear(&vi);
	if (b.failed) {
		free(b.data);
		snprintf(err, err_size, "out of memory");
		return NULL;
	}
	*out_size = b.size;
	return b.data;
}
