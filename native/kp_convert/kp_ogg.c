#include "kp_ogg.h"

#include <math.h>
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

/* Sample k of channel c as oggenc reads it: 8-bit unsigned around 128, 16-bit and 24-bit signed little-endian. */
static float sample(const uint8_t *pcm, int bits, int channels, size_t k, int c)
{
	size_t at = k * (size_t)channels + (size_t)c;
	const uint8_t *p;
	if (bits == 8) return (float)((int)pcm[at] - 128) / 128.0f;
	if (bits == 16) return (float)(int16_t)(uint16_t)(pcm[2 * at] | pcm[2 * at + 1] << 8) / 32768.0f;
	p = pcm + 3 * at;
	return (float)((int32_t)((uint32_t)p[0] << 8 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 24) >> 8) / 8388608.0f;
}

/* The samples the encoder reads: n frames from frame `at`, a plane per channel */
typedef void (*Fill)(const void *ctx, float **buf, size_t at, size_t n, int channels);

/* frames of channels at rate as Ogg Vorbis, as oggenc writes them: malloc'd bytes and *out_size, or NULL with the
 * reason. */
static uint8_t *vorbis(int channels, long rate, size_t frames, Fill fill, const void *ctx, float quality, uint32_t serial,
	size_t *out_size, char *err, size_t err_size)
{
	vorbis_info vi;
	vorbis_comment vc;
	vorbis_dsp_state vd;
	vorbis_block vb;
	ogg_stream_state os;
	ogg_page og;
	ogg_packet op, head, comment, code;
	Buffer b;
	size_t k;
	int eos = 0;
	vorbis_info_init(&vi);
	if (vorbis_encode_init_vbr(&vi, channels, rate, quality) != 0) {
		vorbis_info_clear(&vi);
		snprintf(err, err_size, "the encoder refuses %ld Hz, %d channels", rate, channels);
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
		size_t n = k < frames ? (frames - k < FEED ? frames - k : FEED) : 0;
		if (n == 0) {
			vorbis_analysis_wrote(&vd, 0); /* the end: the last block is encoded and the last page marked */
		} else {
			fill(ctx, vorbis_analysis_buffer(&vd, (int)n), k, n, channels);
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

typedef struct {
	const uint8_t *pcm;
	int bits;
} Pcm;

static void fill_pcm(const void *ctx, float **buf, size_t at, size_t n, int channels)
{
	const Pcm *p = (const Pcm *)ctx;
	size_t i;
	int c;
	for (c = 0; c < channels; c++)
		for (i = 0; i < n; i++) buf[c][i] = sample(p->pcm, p->bits, channels, at + i, c);
}

/* A WAV the encoder takes (PCM, 8, 16 or 24 bits, mono or stereo: Remastered's deDE keeps a few voices at 24): its
 * header and samples; -1 with the reason. */
static int pcm_wav(const uint8_t *wav, size_t size, KpWavInfo *w, Pcm *p, size_t *frames, char *err, size_t err_size)
{
	if (kp_wav_info(wav, size, size, w) != 0) {
		snprintf(err, err_size, "not a WAV the game reads");
		return -1;
	}
	if (w->format_tag != WAV_PCM || (w->bits_per_sample != 8 && w->bits_per_sample != 16 && w->bits_per_sample != 24) || w->channels < 1
		|| w->channels > 2 || w->sample_rate == 0) {
		snprintf(err, err_size, "a WAV of format %u, %u bits, %u channels: not PCM the encoder takes", w->format_tag,
			w->bits_per_sample, w->channels);
		return -1;
	}
	p->pcm = wav + w->data_offset;
	p->bits = w->bits_per_sample;
	*frames = w->data_length / ((size_t)(p->bits / 8) * w->channels);
	return 0;
}

uint8_t *kp_ogg_from_wav(const uint8_t *wav, size_t size, float quality, uint32_t serial, size_t *out_size, char *err,
	size_t err_size)
{
	KpWavInfo w;
	Pcm p;
	size_t frames;
	if (pcm_wav(wav, size, &w, &p, &frames, err, err_size) != 0) return NULL;
	return vorbis(w.channels, (long)w.sample_rate, frames, fill_pcm, &p, quality, serial, out_size, err, err_size);
}

/* --- a language's speech: mono at 22,050 Hz (ADR 0019, stage C2) --- */

#define HALF_TAPS 32 /* the taps each side of the half-band low-pass's centre */
#define PI 3.14159265358979323846

/* Mono samples: the source's channels averaged. */
typedef struct {
	float *data;
	size_t frames, cap;
	int failed;
} Mono;

static float *mono_room(Mono *m, size_t n)
{
	if (m->failed) return NULL;
	if (m->frames + n > m->cap) {
		size_t cap = m->cap ? m->cap : 65536;
		float *grown;
		while (cap < m->frames + n) cap *= 2;
		if (!(grown = (float *)realloc(m->data, cap * sizeof *grown))) {
			m->failed = 1;
			return NULL;
		}
		m->data = grown;
		m->cap = cap;
	}
	return m->data + m->frames;
}

static void mono_planes(Mono *m, float **planes, int channels, size_t n)
{
	float *out = mono_room(m, n);
	size_t i;
	int c;
	if (!out) return;
	for (i = 0; i < n; i++) {
		float s = 0.0f;
		for (c = 0; c < channels; c++) s += planes[c][i];
		out[i] = s / (float)channels;
	}
	m->frames += n;
}

/* An Ogg Vorbis stream decoded (its first logical stream; libvorbis trims the end by its last granule) and mixed down:
 * 0 and its rate, or -1 with the reason. */
static int ogg_mono(const uint8_t *ogg, size_t size, Mono *m, uint32_t *rate, char *err, size_t err_size)
{
	ogg_sync_state oy;
	ogg_stream_state os;
	vorbis_info vi;
	vorbis_comment vc;
	vorbis_dsp_state vd;
	vorbis_block vb;
	ogg_page og;
	ogg_packet op;
	int headers = 0, stream = 0, rc = -1;
	char *buf;
	ogg_sync_init(&oy);
	vorbis_info_init(&vi);
	vorbis_comment_init(&vc);
	if (!(buf = ogg_sync_buffer(&oy, (long)size))) {
		snprintf(err, err_size, "out of memory");
		goto done;
	}
	memcpy(buf, ogg, size);
	ogg_sync_wrote(&oy, (long)size);
	while (ogg_sync_pageout(&oy, &og) == 1) {
		if (!stream) {
			ogg_stream_init(&os, ogg_page_serialno(&og));
			stream = 1;
		}
		if (ogg_stream_pagein(&os, &og) != 0) continue; /* another logical stream's page */
		while (ogg_stream_packetout(&os, &op) == 1) {
			float **pcm;
			int n;
			if (headers < 3) {
				if (vorbis_synthesis_headerin(&vi, &vc, &op) != 0) {
					snprintf(err, err_size, "not Ogg Vorbis");
					goto done;
				}
				if (++headers == 3) {
					vorbis_synthesis_init(&vd, &vi);
					vorbis_block_init(&vd, &vb);
				}
				continue;
			}
			if (vorbis_synthesis(&vb, &op) == 0) vorbis_synthesis_blockin(&vd, &vb);
			while ((n = vorbis_synthesis_pcmout(&vd, &pcm)) > 0) {
				mono_planes(m, pcm, vi.channels, (size_t)n);
				vorbis_synthesis_read(&vd, n);
			}
		}
	}
	if (headers < 3) {
		snprintf(err, err_size, "not Ogg Vorbis");
	} else if (m->failed) {
		snprintf(err, err_size, "out of memory");
	} else {
		*rate = (uint32_t)vi.rate;
		rc = 0;
	}
done:
	if (headers == 3) {
		vorbis_block_clear(&vb);
		vorbis_dsp_clear(&vd);
	}
	if (stream) ogg_stream_clear(&os);
	vorbis_comment_clear(&vc);
	vorbis_info_clear(&vi);
	ogg_sync_clear(&oy);
	return rc;
}

/* A WAV the encoder takes, mixed down: 0 and its rate, or -1 with the reason. */
static int wav_mono(const uint8_t *wav, size_t size, Mono *m, uint32_t *rate, char *err, size_t err_size)
{
	KpWavInfo w;
	Pcm p;
	size_t frames, i;
	float *out;
	int c;
	if (pcm_wav(wav, size, &w, &p, &frames, err, err_size) != 0) return -1;
	if (!(out = mono_room(m, frames ? frames : 1))) {
		snprintf(err, err_size, "out of memory");
		return -1;
	}
	for (i = 0; i < frames; i++) {
		float s = 0.0f;
		for (c = 0; c < w.channels; c++) s += sample(p.pcm, p.bits, w.channels, i, c);
		out[i] = s / (float)w.channels;
	}
	m->frames = frames;
	*rate = w.sample_rate;
	return 0;
}

/* Twice KP_OGG_SPEECH_RATE to it: a Blackman-windowed sinc low-pass at the new Nyquist frequency, every other sample
 * kept (the input past its ends silence). malloc'd, *out_frames; NULL out of memory. */
static float *halved(const float *in, size_t frames, size_t *out_frames)
{
	double h[2 * HALF_TAPS + 1], sum = 0.0;
	size_t n = (frames + 1) / 2, m;
	float *out;
	int k;
	for (k = -HALF_TAPS; k <= HALF_TAPS; k++) {
		double x = k / 2.0, sinc = k == 0 ? 1.0 : sin(PI * x) / (PI * x);
		double w = 0.42 + 0.5 * cos(PI * k / (HALF_TAPS + 1)) + 0.08 * cos(2.0 * PI * k / (HALF_TAPS + 1));
		sum += h[k + HALF_TAPS] = sinc * w;
	}
	for (k = 0; k <= 2 * HALF_TAPS; k++) h[k] /= sum; /* unit gain at 0 Hz */
	if (!(out = (float *)malloc((n ? n : 1) * sizeof *out))) return NULL;
	for (m = 0; m < n; m++) {
		double s = 0.0;
		for (k = -HALF_TAPS; k <= HALF_TAPS; k++) {
			long i = (long)(2 * m) - k;
			if (i >= 0 && (size_t)i < frames) s += h[k + HALF_TAPS] * in[i];
		}
		out[m] = (float)s;
	}
	*out_frames = n;
	return out;
}

static void fill_mono(const void *ctx, float **buf, size_t at, size_t n, int channels)
{
	(void)channels;
	memcpy(buf[0], (const float *)ctx + at, n * sizeof(float));
}

uint8_t *kp_ogg_speech(const uint8_t *data, size_t size, float quality, uint32_t serial, size_t *out_size, char *err,
	size_t err_size)
{
	Mono m;
	uint32_t rate = 0;
	float *half = NULL;
	const float *samples;
	size_t frames;
	uint8_t *out = NULL;
	memset(&m, 0, sizeof m);
	if ((size >= 4 && memcmp(data, "OggS", 4) == 0 ? ogg_mono(data, size, &m, &rate, err, err_size)
			: wav_mono(data, size, &m, &rate, err, err_size)) != 0)
		goto done;
	samples = m.data;
	frames = m.frames;
	if (rate == 2 * KP_OGG_SPEECH_RATE) {
		if (!(half = halved(m.data, m.frames, &frames))) {
			snprintf(err, err_size, "out of memory");
			goto done;
		}
		samples = half;
		rate = KP_OGG_SPEECH_RATE;
	}
	out = vorbis(1, (long)rate, frames, fill_mono, samples, quality, serial, out_size, err, err_size);
done:
	free(half);
	free(m.data);
	return out;
}
