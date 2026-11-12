#include "kp_webm_write.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kp_json.h"
#include "kp_webm.h"

/* Matroska's element IDs (their marker bits included) */
#define ID_EBML 0x1A45DFA3u
#define ID_EBML_VERSION 0x4286u
#define ID_EBML_READ_VERSION 0x42F7u
#define ID_EBML_MAX_ID_LENGTH 0x42F2u
#define ID_EBML_MAX_SIZE_LENGTH 0x42F3u
#define ID_DOC_TYPE 0x4282u
#define ID_DOC_TYPE_VERSION 0x4287u
#define ID_DOC_TYPE_READ_VERSION 0x4285u
#define ID_SEGMENT 0x18538067u
#define ID_INFO 0x1549A966u
#define ID_TIMESTAMP_SCALE 0x2AD7B1u
#define ID_DURATION 0x4489u
#define ID_MUXING_APP 0x4D80u
#define ID_WRITING_APP 0x5741u
#define ID_TRACKS 0x1654AE6Bu
#define ID_TRACK_ENTRY 0xAEu
#define ID_TRACK_NUMBER 0xD7u
#define ID_TRACK_UID 0x73C5u
#define ID_TRACK_TYPE 0x83u
#define ID_FLAG_LACING 0x9Cu
#define ID_DEFAULT_DURATION 0x23E383u
#define ID_CODEC_ID 0x86u
#define ID_CODEC_PRIVATE 0x63A2u
#define ID_VIDEO 0xE0u
#define ID_PIXEL_WIDTH 0xB0u
#define ID_PIXEL_HEIGHT 0xBAu
#define ID_DISPLAY_WIDTH 0x54B0u
#define ID_DISPLAY_HEIGHT 0x54BAu
#define ID_AUDIO 0xE1u
#define ID_SAMPLING_FREQUENCY 0xB5u
#define ID_CHANNELS 0x9Fu
#define ID_CLUSTER 0x1F43B675u
#define ID_TIMESTAMP 0xE7u
#define ID_SIMPLE_BLOCK 0xA3u
#define ID_BLOCK_GROUP 0xA0u
#define ID_BLOCK 0xA1u
#define ID_DISCARD_PADDING 0x75A2u

#define DOC_TYPE_VERSION 4 /* DiscardPadding */
#define DOC_TYPE_READ_VERSION 2
#define TIMESTAMP_SCALE 1000000u /* a millisecond */
#define TRACK_VIDEO 1
#define TRACK_AUDIO 2
#define TYPE_VIDEO 1
#define TYPE_AUDIO 2
#define BLOCK_KEY 0x80
#define NS_PER_MS 1000000ull
#define NS_PER_S 1000000000ull

static void put_id(KpBuf *b, uint32_t id)
{
	uint8_t bytes[4];
	int n = id > 0xFFFFFFu ? 4 : id > 0xFFFFu ? 3 : id > 0xFFu ? 2 : 1, k;
	for (k = 0; k < n; k++) bytes[k] = (uint8_t)(id >> (8 * (n - 1 - k)));
	kp_buf_add(b, bytes, (size_t)n);
}

/* An element's size as the shortest EBML variable-length integer (all its value bits set is reserved: unknown). */
static void put_size(KpBuf *b, uint64_t size)
{
	uint8_t bytes[8];
	int n = 1, k;
	while (n < 8 && size >= (1ull << (7 * n)) - 1) n++;
	for (k = 0; k < n; k++) bytes[k] = (uint8_t)(size >> (8 * (n - 1 - k)));
	bytes[0] |= (uint8_t)(0x80u >> (n - 1));
	kp_buf_add(b, bytes, (size_t)n);
}

static void put_element(KpBuf *b, uint32_t id, const void *payload, size_t size)
{
	put_id(b, id);
	put_size(b, size);
	kp_buf_add(b, payload, size);
}

/* An unsigned integer element: its value big-endian in as few bytes as hold it (one at least). */
static void put_uint(KpBuf *b, uint32_t id, uint64_t v)
{
	uint8_t bytes[8];
	int n = 1, k;
	while (n < 8 && (v >> (8 * n)) != 0) n++;
	for (k = 0; k < n; k++) bytes[k] = (uint8_t)(v >> (8 * (n - 1 - k)));
	put_element(b, id, bytes, (size_t)n);
}

/* A signed integer element: two's complement big-endian in as few bytes as keep its sign. */
static void put_int(KpBuf *b, uint32_t id, int64_t v)
{
	uint8_t bytes[8];
	int n = 1, k;
	while (n < 8 && !(v >= -(1ll << (8 * n - 1)) && v < (1ll << (8 * n - 1)))) n++;
	for (k = 0; k < n; k++) bytes[k] = (uint8_t)((uint64_t)v >> (8 * (n - 1 - k)));
	put_element(b, id, bytes, (size_t)n);
}

/* A float element as an 8-byte IEEE double, big-endian. */
static void put_float(KpBuf *b, uint32_t id, double v)
{
	uint8_t bytes[8];
	uint64_t bits;
	int k;
	memcpy(&bits, &v, sizeof bits);
	for (k = 0; k < 8; k++) bytes[k] = (uint8_t)(bits >> (8 * (7 - k)));
	put_element(b, id, bytes, sizeof bytes);
}

static void put_string(KpBuf *b, uint32_t id, const char *s) { put_element(b, id, s, strlen(s)); }

/* A master element: its children's bytes (freed) after its ID and size. */
static void put_master(KpBuf *b, uint32_t id, KpBuf *children)
{
	put_element(b, id, children->data, children->size);
	kp_buf_free(children);
	kp_buf_init(children);
}

/* A Xiph lacing's size: as many 255 as it holds, then the rest. */
static void put_xiph(KpBuf *b, size_t n)
{
	uint8_t full = 255, rest;
	for (; n >= 255; n -= 255) kp_buf_add(b, &full, 1);
	rest = (uint8_t)n;
	kp_buf_add(b, &rest, 1);
}

static size_t packet_at(const KpVorbisPackets *s, int k) { return k ? s->ends[k - 1] : 0; }

static void header(KpBuf *b)
{
	KpBuf e;
	kp_buf_init(&e);
	put_uint(&e, ID_EBML_VERSION, 1);
	put_uint(&e, ID_EBML_READ_VERSION, 1);
	put_uint(&e, ID_EBML_MAX_ID_LENGTH, 4);
	put_uint(&e, ID_EBML_MAX_SIZE_LENGTH, 8);
	put_string(&e, ID_DOC_TYPE, "webm");
	put_uint(&e, ID_DOC_TYPE_VERSION, DOC_TYPE_VERSION);
	put_uint(&e, ID_DOC_TYPE_READ_VERSION, DOC_TYPE_READ_VERSION);
	put_master(b, ID_EBML, &e);
}

static void tracks(KpBuf *seg, const KpWebmMovie *m)
{
	KpBuf all, entry, sub;
	int k;
	kp_buf_init(&all);
	kp_buf_init(&entry);
	kp_buf_init(&sub);
	put_uint(&entry, ID_TRACK_NUMBER, TRACK_VIDEO);
	put_uint(&entry, ID_TRACK_UID, TRACK_VIDEO);
	put_uint(&entry, ID_TRACK_TYPE, TYPE_VIDEO);
	put_uint(&entry, ID_FLAG_LACING, 0);
	put_uint(&entry, ID_DEFAULT_DURATION, m->frame_ns);
	put_string(&entry, ID_CODEC_ID, "V_VP9");
	put_uint(&sub, ID_PIXEL_WIDTH, m->width);
	put_uint(&sub, ID_PIXEL_HEIGHT, m->height);
	if ((m->shown_w && m->shown_w != m->width) || (m->shown_h && m->shown_h != m->height)) {
		put_uint(&sub, ID_DISPLAY_WIDTH, m->shown_w ? m->shown_w : m->width);
		put_uint(&sub, ID_DISPLAY_HEIGHT, m->shown_h ? m->shown_h : m->height);
	}
	put_master(&entry, ID_VIDEO, &sub);
	put_master(&all, ID_TRACK_ENTRY, &entry);
	if (m->sound) {
		const KpVorbisPackets *s = m->sound;
		KpBuf priv;
		kp_buf_init(&priv);
		kp_buf_char(&priv, (char)(KP_VORBIS_HEADERS - 1));
		for (k = 0; k < KP_VORBIS_HEADERS - 1; k++) put_xiph(&priv, s->ends[k] - packet_at(s, k));
		kp_buf_add(&priv, s->data, s->ends[KP_VORBIS_HEADERS - 1]);
		put_uint(&entry, ID_TRACK_NUMBER, TRACK_AUDIO);
		put_uint(&entry, ID_TRACK_UID, TRACK_AUDIO);
		put_uint(&entry, ID_TRACK_TYPE, TYPE_AUDIO);
		put_uint(&entry, ID_FLAG_LACING, 0);
		put_string(&entry, ID_CODEC_ID, "A_VORBIS");
		put_master(&entry, ID_CODEC_PRIVATE, &priv);
		put_float(&sub, ID_SAMPLING_FREQUENCY, (double)s->rate);
		put_uint(&sub, ID_CHANNELS, (uint64_t)s->channels);
		put_master(&entry, ID_AUDIO, &sub);
		put_master(&all, ID_TRACK_ENTRY, &entry);
	}
	put_master(seg, ID_TRACKS, &all);
}

/* A block's head: its track, its time from its cluster's, its flags. */
static void block_head(KpBuf *b, int track, int64_t relative, uint8_t flags)
{
	uint8_t bytes[4];
	bytes[0] = (uint8_t)(0x80 | track);
	bytes[1] = (uint8_t)((uint16_t)(int16_t)relative >> 8);
	bytes[2] = (uint8_t)relative;
	bytes[3] = flags;
	kp_buf_add(b, bytes, sizeof bytes);
}

uint8_t *kp_webm_write(const KpWebmMovie *m, size_t *out_size, char *err, size_t err_size)
{
	const KpVorbisPackets *s = m->sound;
	KpBuf out, seg, info, cluster, block;
	uint64_t *starts = NULL, video_at = 0, decoded = 0, end_ms;
	int64_t cluster_ms = -1, padding = 0;
	uint32_t n = 0;
	int sound_packets = s ? s->count - KP_VORBIS_HEADERS : 0, j;
	uint8_t *bytes;
	if (m->frames == 0 || m->frame_ns == 0 || !(m->sizes[0] & KP_WEBM_KEY)) {
		snprintf(err, err_size, "no frames, or the first is not a key frame");
		return NULL;
	}
	for (n = 0; n < m->frames; n++)
		if ((m->sizes[n] & ~KP_WEBM_KEY) == 0) {
			snprintf(err, err_size, "frame %u has no bytes", (unsigned)n);
			return NULL;
		}
	if (s && (sound_packets < 1 || s->rate == 0 || s->channels < 1)) {
		snprintf(err, err_size, "the sound has no packets past its headers");
		return NULL;
	}
	/* each sound packet's first sample: what the blocks before it decode (Vorbis: a block's last quarter and the
	 * next's first quarter overlap, the first block gives none) */
	if (s) {
		if (!(starts = (uint64_t *)malloc((size_t)sound_packets * sizeof *starts))) {
			snprintf(err, err_size, "out of memory");
			return NULL;
		}
		for (j = 0; j < sound_packets; j++) {
			int k = KP_VORBIS_HEADERS + j;
			if (j > 0) decoded += s->blocks[k - 1] / 4 + s->blocks[k] / 4;
			starts[j] = decoded;
		}
		/* the end's excess: decoded past the last packet's granule (the sound's length), at most its block's */
		if (s->granules[s->count - 1] >= 0 && (uint64_t)s->granules[s->count - 1] < decoded)
			padding = (int64_t)(decoded - (uint64_t)s->granules[s->count - 1]);
	}
	kp_buf_init(&out);
	kp_buf_init(&seg);
	kp_buf_init(&info);
	kp_buf_init(&cluster);
	kp_buf_init(&block);
	header(&out);
	end_ms = m->frames * m->frame_ns / NS_PER_MS;
	if (s && s->granules[s->count - 1] > 0) {
		uint64_t sound_ms = (uint64_t)s->granules[s->count - 1] * 1000u / s->rate;
		if (sound_ms > end_ms) end_ms = sound_ms;
	}
	put_uint(&info, ID_TIMESTAMP_SCALE, TIMESTAMP_SCALE);
	put_float(&info, ID_DURATION, (double)end_ms);
	put_string(&info, ID_MUXING_APP, KP_WEBM_APP);
	put_string(&info, ID_WRITING_APP, KP_WEBM_APP);
	put_master(&seg, ID_INFO, &info);
	tracks(&seg, m);
	for (n = 0, j = 0; n < m->frames || j < sound_packets;) {
		uint64_t v_ms = n < m->frames ? (n * m->frame_ns + NS_PER_MS / 2) / NS_PER_MS : UINT64_MAX;
		uint64_t a_ms = j < sound_packets ? starts[j] * 1000u / s->rate : UINT64_MAX;
		int video = v_ms <= a_ms;
		uint64_t t = video ? v_ms : a_ms;
		int key = video && (m->sizes[n] & KP_WEBM_KEY) != 0;
		if (cluster_ms < 0 || key || (int64_t)t - cluster_ms > KP_WEBM_CLUSTER_MS) {
			if (cluster_ms >= 0) put_master(&seg, ID_CLUSTER, &cluster);
			cluster_ms = (int64_t)t;
			put_uint(&cluster, ID_TIMESTAMP, t);
		}
		if (video) {
			uint32_t size = m->sizes[n] & ~KP_WEBM_KEY;
			block_head(&block, TRACK_VIDEO, (int64_t)t - cluster_ms, (uint8_t)(key ? BLOCK_KEY : 0));
			kp_buf_add(&block, m->packets + video_at, size);
			put_master(&cluster, ID_SIMPLE_BLOCK, &block);
			video_at += size;
			n++;
		} else {
			int k = KP_VORBIS_HEADERS + j;
			size_t at = packet_at(s, k), size = s->ends[k] - at;
			if (j == sound_packets - 1 && padding > 0) { /* the last: how much of its block is past the sound's end */
				KpBuf group;
				kp_buf_init(&group);
				block_head(&block, TRACK_AUDIO, (int64_t)t - cluster_ms, 0);
				kp_buf_add(&block, s->data + at, size);
				put_master(&group, ID_BLOCK, &block);
				put_int(&group, ID_DISCARD_PADDING, (int64_t)(((uint64_t)padding * NS_PER_S + s->rate / 2) / s->rate));
				put_master(&cluster, ID_BLOCK_GROUP, &group);
			} else {
				block_head(&block, TRACK_AUDIO, (int64_t)t - cluster_ms, BLOCK_KEY);
				kp_buf_add(&block, s->data + at, size);
				put_master(&cluster, ID_SIMPLE_BLOCK, &block);
			}
			j++;
		}
	}
	put_master(&seg, ID_CLUSTER, &cluster);
	put_master(&out, ID_SEGMENT, &seg);
	free(starts);
	kp_buf_free(&block);
	*out_size = out.size;
	bytes = (uint8_t *)out.data; /* the buffer's own: taken */
	return bytes;
}
