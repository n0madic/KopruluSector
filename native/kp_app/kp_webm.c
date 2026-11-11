/* A WebM movie's frames as RGBA, and libvpx's pictures' colours (kp_webm.h). */
#include "kp_webm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nestegg/nestegg.h"
#include "vpx/vp8dx.h"
#include "vpx/vpx_decoder.h"

#define FIXED 65536.0 /* the colour matrices' 16.16 fixed point */
#define ALPHA_ID 1    /* Matroska's BlockAddID of the alpha stream */

/* --- the colour --- */

typedef struct {
	int32_t y_scale, y_low, cr_r, cb_g, cr_g, cb_b;
} YuvMatrix;

static void yuv_matrix(YuvMatrix *m, int matrix, int full_range)
{
	double kr = matrix == 601 ? 0.299 : 0.2126, kb = matrix == 601 ? 0.114 : 0.0722, kg = 1.0 - kr - kb;
	double ys = full_range ? 1.0 : 255.0 / 219.0, cs = full_range ? 1.0 : 255.0 / 224.0;
	m->y_scale = (int32_t)(ys * FIXED + 0.5);
	m->y_low = full_range ? 0 : 16;
	m->cr_r = (int32_t)(2.0 * (1.0 - kr) * cs * FIXED + 0.5);
	m->cb_b = (int32_t)(2.0 * (1.0 - kb) * cs * FIXED + 0.5);
	m->cb_g = (int32_t)(2.0 * kb * (1.0 - kb) / kg * cs * FIXED + 0.5);
	m->cr_g = (int32_t)(2.0 * kr * (1.0 - kr) / kg * cs * FIXED + 0.5);
}

static void image_matrix(YuvMatrix *m, const vpx_image_t *im)
{
	yuv_matrix(m, im->cs == VPX_CS_BT_709 ? 709 : 601, im->range == VPX_CR_FULL_RANGE);
}

static int is_srgb(const vpx_image_t *im) { return im->fmt == VPX_IMG_FMT_I444 && im->cs == VPX_CS_SRGB; }

static uint8_t clamp8(int32_t v) { return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v); }

static void yuv_pixel(const YuvMatrix *m, int y, int cb, int cr, uint8_t *rgb)
{
	int32_t yy = (y - m->y_low) * m->y_scale + 32768, u = cb - 128, v = cr - 128;
	rgb[0] = clamp8((yy + m->cr_r * v) >> 16);
	rgb[1] = clamp8((yy - m->cb_g * u - m->cr_g * v) >> 16);
	rgb[2] = clamp8((yy + m->cb_b * u) >> 16);
}

void kp_vpx_yuv_rgb(int y, int cb, int cr, int matrix, int full_range, uint8_t rgb[3])
{
	YuvMatrix m;
	yuv_matrix(&m, matrix, full_range);
	yuv_pixel(&m, y, cb, cr, rgb);
}

int kp_vpx_usable(const void *image, uint32_t width, uint32_t height)
{
	const vpx_image_t *im = (const vpx_image_t *)image;
	return im && im->bit_depth == 8 && !(im->fmt & VPX_IMG_FMT_HIGHBITDEPTH) && im->d_w == width && im->d_h == height
		&& (im->fmt == VPX_IMG_FMT_I420 || im->fmt == VPX_IMG_FMT_I422 || im->fmt == VPX_IMG_FMT_I444
			|| im->fmt == VPX_IMG_FMT_I440);
}

void kp_vpx_rgba(const void *image, uint8_t *out)
{
	const vpx_image_t *im = (const vpx_image_t *)image;
	uint32_t x, y, width = im->d_w, height = im->d_h;
	int xs = (int)im->x_chroma_shift, ys = (int)im->y_chroma_shift;
	if (is_srgb(im)) { /* planes G, B, R */
		for (y = 0; y < height; y++) {
			const uint8_t *g = im->planes[0] + (size_t)y * (size_t)im->stride[0];
			const uint8_t *b = im->planes[1] + (size_t)y * (size_t)im->stride[1];
			const uint8_t *r = im->planes[2] + (size_t)y * (size_t)im->stride[2];
			for (x = 0; x < width; x++, out += 4) {
				out[0] = r[x];
				out[1] = g[x];
				out[2] = b[x];
				out[3] = 255;
			}
		}
		return;
	}
	{
		YuvMatrix m;
		image_matrix(&m, im);
		for (y = 0; y < height; y++) {
			const uint8_t *yp = im->planes[0] + (size_t)y * (size_t)im->stride[0];
			const uint8_t *up = im->planes[1] + (size_t)(y >> ys) * (size_t)im->stride[1];
			const uint8_t *vp = im->planes[2] + (size_t)(y >> ys) * (size_t)im->stride[2];
			for (x = 0; x < width; x++, out += 4) {
				yuv_pixel(&m, yp[x], up[x >> xs], vp[x >> xs], out);
				out[3] = 255;
			}
		}
	}
}

void kp_vpx_picture(const void *image, KpVideoPicture *out)
{
	const vpx_image_t *im = (const vpx_image_t *)image;
	YuvMatrix m;
	int k;
	memset(out, 0, sizeof *out);
	for (k = 0; k < 3; k++) {
		out->planes[k] = im->planes[k];
		out->strides[k] = im->stride[k];
	}
	out->width = (int32_t)im->d_w;
	out->height = (int32_t)im->d_h;
	out->chroma_w = (int32_t)((im->d_w + (1u << im->x_chroma_shift) - 1) >> im->x_chroma_shift);
	out->chroma_h = (int32_t)((im->d_h + (1u << im->y_chroma_shift) - 1) >> im->y_chroma_shift);
	out->srgb = is_srgb(im);
	image_matrix(&m, im);
	out->y_low = (float)m.y_low / 255.0f;
	out->y_scale = (float)(m.y_scale / FIXED);
	out->cr_r = (float)(m.cr_r / FIXED);
	out->cb_g = (float)(m.cb_g / FIXED);
	out->cr_g = (float)(m.cr_g / FIXED);
	out->cb_b = (float)(m.cb_b / FIXED);
}

/* --- the movie --- */

struct KpWebm {
	const uint8_t *data;
	size_t size, at;          /* the reader's position */
	nestegg *demux;
	unsigned int track;
	int codec_id;              /* NESTEGG_CODEC_VP8 or _VP9 */
	uint32_t width, height;
	vpx_codec_ctx_t codec, alpha;
	int codec_open, alpha_open; /* alpha_open: the track has an alpha stream */
	uint8_t *rgba;             /* the frame decoded last */
	uint32_t decoded;          /* frames decoded since the start */
	int ended;                 /* the stream's end met: rgba holds the last frame */
};

/* nestegg's read: the bytes read, short at the end of the stream, 0 past it */
static int64_t io_read(void *buffer, size_t length, void *ctx)
{
	KpWebm *w = (KpWebm *)ctx;
	if (length > w->size - w->at) length = w->size - w->at;
	memcpy(buffer, w->data + w->at, length);
	w->at += length;
	return (int64_t)length;
}

static int io_seek(int64_t offset, int whence, void *ctx)
{
	KpWebm *w = (KpWebm *)ctx;
	int64_t base = whence == NESTEGG_SEEK_SET ? 0 : whence == NESTEGG_SEEK_CUR ? (int64_t)w->at : (int64_t)w->size;
	if (offset > (int64_t)w->size - base || base + offset < 0) return -1;
	w->at = (size_t)(base + offset);
	return 0;
}

static int64_t io_tell(void *ctx) { return (int64_t)((KpWebm *)ctx)->at; }

static void say(char *err, size_t err_size, const char *what)
{
	if (err_size) snprintf(err, err_size, "webm: %s", what);
}

/* The demuxer at the start of the stream, its first video track found: 0, -1. */
static int demux_open(KpWebm *w, char *err, size_t err_size)
{
	nestegg_io io;
	unsigned int tracks = 0, k;
	io.read = io_read;
	io.seek = io_seek;
	io.tell = io_tell;
	io.userdata = w;
	w->at = 0;
	if (nestegg_init(&w->demux, io, NULL, -1) != 0) {
		w->demux = NULL;
		say(err, err_size, "not a WebM file");
		return -1;
	}
	if (nestegg_track_count(w->demux, &tracks) != 0) tracks = 0;
	for (k = 0; k < tracks; k++) {
		if (nestegg_track_type(w->demux, k) == NESTEGG_TRACK_VIDEO) {
			w->track = k;
			return 0;
		}
	}
	say(err, err_size, "no video track");
	return -1;
}

static int codec_init(vpx_codec_ctx_t *c, int codec_id, uint32_t width, uint32_t height)
{
	vpx_codec_dec_cfg_t cfg;
	memset(&cfg, 0, sizeof cfg);
	cfg.threads = 1;
	cfg.w = width;
	cfg.h = height;
	return vpx_codec_dec_init(c, codec_id == NESTEGG_CODEC_VP8 ? vpx_codec_vp8_dx() : vpx_codec_vp9_dx(), &cfg, 0)
		== VPX_CODEC_OK ? 0 : -1;
}

KpWebm *kp_webm_open(const uint8_t *data, size_t size, char *err, size_t err_size)
{
	KpWebm *w = (KpWebm *)calloc(1, sizeof *w);
	nestegg_video_params params;
	if (!w) {
		say(err, err_size, "out of memory");
		return NULL;
	}
	w->data = data;
	w->size = size;
	if (demux_open(w, err, err_size) != 0) goto fail;
	w->codec_id = nestegg_track_codec_id(w->demux, w->track);
	if (w->codec_id != NESTEGG_CODEC_VP8 && w->codec_id != NESTEGG_CODEC_VP9) {
		say(err, err_size, "the video is neither VP8 nor VP9");
		goto fail;
	}
	if (nestegg_track_video_params(w->demux, w->track, &params) != 0 || params.width == 0 || params.height == 0
		|| params.width > KP_WEBM_MAX_SIDE || params.height > KP_WEBM_MAX_SIDE) {
		say(err, err_size, "the video's size is 0 or too large");
		goto fail;
	}
	w->width = params.width;
	w->height = params.height;
	if (!(w->rgba = (uint8_t *)malloc((size_t)w->width * w->height * 4))) {
		say(err, err_size, "out of memory");
		goto fail;
	}
	if (codec_init(&w->codec, w->codec_id, w->width, w->height) != 0) {
		say(err, err_size, "libvpx cannot decode the video");
		goto fail;
	}
	w->codec_open = 1;
	if (params.alpha_mode) {
		if (codec_init(&w->alpha, w->codec_id, w->width, w->height) != 0) {
			say(err, err_size, "libvpx cannot decode the alpha stream");
			goto fail;
		}
		w->alpha_open = 1;
	}
	return w;
fail:
	kp_webm_close(w);
	return NULL;
}

int kp_webm_size(const uint8_t *data, size_t size, uint32_t *width, uint32_t *height)
{
	KpWebm w;
	nestegg_video_params params;
	int ok;
	memset(&w, 0, sizeof w);
	w.data = data;
	w.size = size;
	if (demux_open(&w, NULL, 0) != 0) {
		if (w.demux) nestegg_destroy(w.demux);
		return -1;
	}
	ok = nestegg_track_video_params(w.demux, w.track, &params) == 0;
	nestegg_destroy(w.demux);
	if (!ok) return -1;
	*width = params.width;
	*height = params.height;
	return 0;
}

void kp_webm_close(KpWebm *w)
{
	if (!w) return;
	if (w->demux) nestegg_destroy(w->demux);
	if (w->codec_open) vpx_codec_destroy(&w->codec);
	if (w->alpha_open) vpx_codec_destroy(&w->alpha);
	free(w->rgba);
	free(w);
}

uint32_t kp_webm_width(const KpWebm *w) { return w->width; }
uint32_t kp_webm_height(const KpWebm *w) { return w->height; }

/* Decodes a packet's data in c: its last picture, or NULL. */
static const vpx_image_t *decode(vpx_codec_ctx_t *c, const unsigned char *data, size_t size)
{
	vpx_codec_iter_t it = NULL;
	const vpx_image_t *im, *last = NULL;
	if (size == 0 || vpx_codec_decode(c, data, (unsigned int)size, NULL, 0) != VPX_CODEC_OK) return NULL;
	while ((im = vpx_codec_get_frame(c, &it)) != NULL) last = im;
	return last;
}

/* The next frame into w->rgba: 1, 0 after the last frame (rgba left as it was), -1 for a malformed movie. */
static int next_frame(KpWebm *w)
{
	uint8_t *rgba = w->rgba;
	if (!w->demux) return -1; /* a rewind that failed */
	for (;;) {
		nestegg_packet *pkt = NULL;
		unsigned int track = 0, count = 0, k;
		const vpx_image_t *im = NULL, *alpha = NULL;
		int rc = nestegg_read_packet(w->demux, &pkt);
		if (rc == 0) return 0;
		if (rc < 0) return -1;
		if (nestegg_packet_track(pkt, &track) != 0 || track != w->track) {
			nestegg_free_packet(pkt);
			continue;
		}
		if (nestegg_packet_count(pkt, &count) != 0) count = 0;
		for (k = 0; k < count; k++) {
			unsigned char *data = NULL;
			size_t size = 0;
			const vpx_image_t *got;
			if (nestegg_packet_data(pkt, k, &data, &size) != 0) break;
			if ((got = decode(&w->codec, data, size)) != NULL) im = got;
		}
		if (w->alpha_open) { /* the alpha stream's packet beside every picture's, shown or not, to keep it in step */
			unsigned char *data = NULL;
			size_t size = 0;
			if (nestegg_packet_additional_data(pkt, ALPHA_ID, &data, &size) == 0) alpha = decode(&w->alpha, data, size);
		}
		nestegg_free_packet(pkt);
		if (!im) continue; /* a picture not shown (an alternate reference) */
		if (!kp_vpx_usable(im, w->width, w->height)) return -1;
		kp_vpx_rgba(im, rgba);
		if (alpha && alpha->d_w == w->width && alpha->d_h == w->height && alpha->bit_depth == 8) {
			uint32_t x, y;
			for (y = 0; y < w->height; y++) {
				const uint8_t *row = alpha->planes[0] + (size_t)y * (size_t)alpha->stride[0];
				uint8_t *o = rgba + (size_t)y * w->width * 4 + 3;
				for (x = 0; x < w->width; x++, o += 4) *o = row[x];
			}
		}
		return 1;
	}
}

/* Back before the first frame (the decoders keep their state: the first packet is a key frame) */
static int rewind_movie(KpWebm *w)
{
	char err[8];
	nestegg_destroy(w->demux);
	w->demux = NULL;
	w->decoded = 0;
	w->ended = 0;
	return demux_open(w, err, sizeof err);
}

const uint8_t *kp_webm_frame(KpWebm *w, uint32_t n)
{
	uint64_t want = (uint64_t)n + 1; /* frames decoded when rgba holds frame n */
	if (w->decoded > want && rewind_movie(w) != 0) return NULL; /* an earlier frame */
	while (w->decoded < want && !w->ended) {
		int rc = next_frame(w);
		if (rc < 0) return NULL;
		if (rc == 0) w->ended = 1;
		else w->decoded++;
	}
	return w->decoded ? w->rgba : NULL;
}

/* --- kp_smk's codec --- */

static void *smk_open(const uint8_t *data, size_t size, uint32_t *width, uint32_t *height)
{
	char err[8];
	KpWebm *w = kp_webm_open(data, size, err, sizeof err);
	if (w) {
		*width = w->width;
		*height = w->height;
	}
	return w;
}

static const uint8_t *smk_frame(void *movie, uint32_t n) { return kp_webm_frame((KpWebm *)movie, n); }
static void smk_close(void *movie) { kp_webm_close((KpWebm *)movie); }

const KpSmkVideo kp_webm_smk_video = { smk_open, smk_frame, smk_close };

/* --- a cinematic's streams (kp_webm_demux) --- */

#define OGG_SEGMENTS 255
#define NS_PER_S 1000000000u /* Matroska's times */

/* A growing buffer */
typedef struct {
	uint8_t *data;
	size_t size, cap;
	int failed;
} Buf;

static uint8_t *buf_grow(Buf *b, size_t n)
{
	if (b->failed) return NULL;
	if (n > b->cap - b->size) {
		size_t cap = b->cap ? b->cap : 65536;
		uint8_t *d;
		while (cap - b->size < n) {
			if (cap > SIZE_MAX / 2) {
				b->failed = 1;
				return NULL;
			}
			cap *= 2;
		}
		if (!(d = (uint8_t *)realloc(b->data, cap))) {
			b->failed = 1;
			return NULL;
		}
		b->data = d;
		b->cap = cap;
	}
	b->size += n;
	return b->data + b->size - n;
}

static void buf_put(Buf *b, const void *p, size_t n)
{
	uint8_t *o = buf_grow(b, n);
	if (o && n) memcpy(o, p, n);
}

static void put32(Buf *b, uint32_t v)
{
	uint8_t le[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) };
	buf_put(b, le, 4);
}

/* Ogg's CRC: polynomial 0x04c11db7, most significant bit first, no reflection, starting at 0 */
static uint32_t ogg_crc(const uint8_t *p, size_t n)
{
	static uint32_t table[256];
	static int ready;
	uint32_t crc = 0;
	size_t i;
	if (!ready) {
		uint32_t k, j;
		for (k = 0; k < 256; k++) {
			uint32_t r = k << 24;
			for (j = 0; j < 8; j++) r = (r & 0x80000000u) ? (r << 1) ^ 0x04c11db7u : r << 1;
			table[k] = r;
		}
		ready = 1;
	}
	for (i = 0; i < n; i++) crc = (crc << 8) ^ table[((crc >> 24) ^ p[i]) & 0xff];
	return crc;
}

/* An Ogg stream being written: a page's lacing and data gathered, then written whole. */
typedef struct {
	Buf out;
	uint32_t serial, sequence;
	uint8_t lacing[OGG_SEGMENTS];
	int segments;
	Buf body;
	int64_t granule;      /* the page's: the samples after its last packet that ends on it, -1 none ends there */
	int continued;        /* the page begins inside a packet */
	int first;
} Ogg;

static void ogg_page(Ogg *o, int last)
{
	uint8_t head[27];
	size_t at = o->out.size;
	int64_t g = o->granule;
	int k;
	memcpy(head, "OggS", 4);
	head[4] = 0;
	head[5] = (uint8_t)((o->continued ? 1 : 0) | (o->first ? 2 : 0) | (last ? 4 : 0));
	for (k = 0; k < 8; k++) head[6 + k] = (uint8_t)((uint64_t)g >> (8 * k));
	for (k = 0; k < 4; k++) head[14 + k] = (uint8_t)(o->serial >> (8 * k));
	for (k = 0; k < 4; k++) head[18 + k] = (uint8_t)(o->sequence >> (8 * k));
	memset(head + 22, 0, 4);
	head[26] = (uint8_t)o->segments;
	buf_put(&o->out, head, sizeof head);
	buf_put(&o->out, o->lacing, (size_t)o->segments);
	buf_put(&o->out, o->body.data, o->body.size);
	if (!o->out.failed) {
		uint32_t crc = ogg_crc(o->out.data + at, o->out.size - at);
		for (k = 0; k < 4; k++) o->out.data[at + 22 + k] = (uint8_t)(crc >> (8 * k));
	}
	o->sequence++;
	o->segments = 0;
	o->body.size = 0;
	o->granule = -1;
	o->first = 0;
}

/* A packet laced into the pages (a page that fills goes out, the packet continued on the next); granule: the stream's
 * samples once it is decoded. */
static void ogg_packet(Ogg *o, const uint8_t *p, size_t n, int64_t granule)
{
	size_t at = 0;
	for (;;) {
		size_t seg = n - at < 255 ? n - at : 255;
		if (o->segments == OGG_SEGMENTS) {
			ogg_page(o, 0);
			o->continued = 1;
		}
		o->lacing[o->segments++] = (uint8_t)seg;
		buf_put(&o->body, p + at, seg);
		at += seg;
		if (seg < 255) break; /* the packet ends here (a 0 segment after a multiple of 255) */
	}
	o->continued = 0;
	o->granule = granule;
}

/* --- Vorbis: the samples a packet adds (libavcodec's vorbis_parser documents the reading backwards) --- */

typedef struct {
	uint32_t rate;
	uint32_t blocksize[2];
	int modes;
	uint8_t mode_long[64];
	uint32_t mode_mask;
	uint32_t previous;
} Vorbis;

/* The identification header's rate and block sizes: 0, -1. */
static int vorbis_ident(Vorbis *v, const uint8_t *p, size_t n)
{
	if (n < 30 || p[0] != 1 || memcmp(p + 1, "vorbis", 6) != 0) return -1;
	v->rate = (uint32_t)p[12] | (uint32_t)p[13] << 8 | (uint32_t)p[14] << 16 | (uint32_t)p[15] << 24;
	if (v->rate == 0) return -1;
	v->blocksize[0] = 1u << (p[28] & 15);
	v->blocksize[1] = 1u << (p[28] >> 4);
	if (v->blocksize[0] < 64 || v->blocksize[1] > 8192 || v->blocksize[0] > v->blocksize[1]) return -1;
	return 0;
}

/* The setup header's modes, read from its end: the framing bit, then each mode's 6-bit count candidates (40 bits a
 * mode: the window and transform types of 16 bits 0, the mapping of 8 bits) back to a count that matches. 0, -1. */
static int vorbis_setup(Vorbis *v, const uint8_t *p, size_t n)
{
	size_t total = n * 8, at = 0, framing = 0;
	int count = 0, found = 0, k;
#define BACK_BIT(i) ((p[n - 1 - ((i) >> 3)] >> (7 - ((i) & 7))) & 1u)
	if (n < 7 || p[0] != 5 || memcmp(p + 1, "vorbis", 6) != 0) return -1;
	while (total - at > 97) {
		uint32_t b = BACK_BIT(at);
		at++;
		if (b) {
			framing = at;
			break;
		}
	}
	if (!framing) return -1;
	while (total - at >= 97) {
		uint32_t mapping = 0, transform = 0, window = 0, modes = 0;
		size_t i;
		for (i = 0; i < 8; i++) mapping = mapping << 1 | BACK_BIT(at + i);
		for (i = 0; i < 16; i++) transform = transform << 1 | BACK_BIT(at + 8 + i);
		for (i = 0; i < 16; i++) window = window << 1 | BACK_BIT(at + 24 + i);
		if (mapping > 63 || transform || window) break;
		at += 41;
		if (++count > 64) break;
		for (i = 0; i < 6; i++) modes = modes << 1 | BACK_BIT(at + i);
		if ((int)modes + 1 == count) found = count;
	}
	if (!found || found > 63) return -1;
	v->modes = found;
	at = framing;
	for (k = found - 1; k >= 0; k--) {
		v->mode_long[k] = (uint8_t)BACK_BIT(at + 40);
		at += 41;
	}
#undef BACK_BIT
	{
		uint32_t bits = 0, m = (uint32_t)found - 1;
		while (m) {
			bits++;
			m >>= 1;
		}
		v->mode_mask = ((1u << bits) - 1u) << 1;
	}
	v->previous = 0;
	return 0;
}

/* The samples an audio packet adds to the stream (the first adds none): (the previous block + this one) / 4. */
static uint32_t vorbis_samples(Vorbis *v, const uint8_t *p, size_t n)
{
	uint32_t mode, current, previous, added;
	if (n == 0 || (p[0] & 1)) return 0;
	mode = v->modes == 1 ? 0 : (p[0] & v->mode_mask) >> 1;
	if ((int)mode >= v->modes) return 0;
	current = v->blocksize[v->mode_long[mode]];
	previous = v->previous;
	added = previous ? (previous + current) / 4 : 0;
	v->previous = current;
	return added;
}


void kp_webm_streams_free(KpWebmStreams *s)
{
	free(s->packets);
	free(s->sizes);
	free(s->times);
	free(s->ogg);
	memset(s, 0, sizeof *s);
}

/* The sound's headers (Matroska's CodecPrivate, Xiph-laced: identification, comment, setup) on the first two pages. */
static int ogg_headers(Ogg *o, Vorbis *v, nestegg *demux, unsigned int track)
{
	unsigned int count = 0, k;
	unsigned char *h[3];
	size_t n[3];
	if (nestegg_track_codec_data_count(demux, track, &count) != 0 || count != 3) return -1;
	for (k = 0; k < 3; k++)
		if (nestegg_track_codec_data(demux, track, k, &h[k], &n[k]) != 0) return -1;
	if (vorbis_ident(v, h[0], n[0]) != 0 || vorbis_setup(v, h[2], n[2]) != 0) return -1;
	o->first = 1;
	o->granule = -1;
	ogg_packet(o, h[0], n[0], 0);
	ogg_page(o, 0);
	ogg_packet(o, h[1], n[1], 0);
	ogg_packet(o, h[2], n[2], 0);
	ogg_page(o, 0); /* the first sound packet begins a page of its own */
	return 0;
}

int kp_webm_streams(const uint8_t *data, size_t size, uint32_t serial, KpWebmStreams *s, char *err, size_t err_size)
{
	KpWebm w;
	nestegg_video_params params;
	Buf packets, sizes, times;
	Ogg ogg;
	Vorbis vorbis;
	nestegg_packet *pkt = NULL;
	unsigned int tracks = 0, k, audio = (unsigned int)-1;
	int64_t samples = 0, block_samples = 0, padding_ns = 0;
	uint64_t frame_ns = 0;
	const char *bad = NULL;
	int rc;
	memset(s, 0, sizeof *s);
	memset(&w, 0, sizeof w);
	memset(&packets, 0, sizeof packets);
	memset(&sizes, 0, sizeof sizes);
	memset(&times, 0, sizeof times);
	memset(&ogg, 0, sizeof ogg);
	memset(&vorbis, 0, sizeof vorbis);
	w.data = data;
	w.size = size;
	if (demux_open(&w, err, err_size) != 0) {
		if (w.demux) nestegg_destroy(w.demux);
		return -1;
	}
	if (nestegg_track_codec_id(w.demux, w.track) != NESTEGG_CODEC_VP9
		|| nestegg_track_video_params(w.demux, w.track, &params) != 0 || params.width == 0 || params.height == 0
		|| params.width > KP_WEBM_MAX_SIDE || params.height > KP_WEBM_MAX_SIDE) {
		bad = "the video is not VP9 of a size the player takes";
		goto done;
	}
	if (nestegg_track_default_duration(w.demux, w.track, &frame_ns) != 0) frame_ns = 0;
	if (nestegg_track_count(w.demux, &tracks) != 0) tracks = 0;
	for (k = 0; k < tracks && audio == (unsigned int)-1; k++)
		if (nestegg_track_type(w.demux, k) == NESTEGG_TRACK_AUDIO) audio = k;
	if (audio != (unsigned int)-1) {
		ogg.serial = serial;
		if (nestegg_track_codec_id(w.demux, audio) != NESTEGG_CODEC_VORBIS) {
			bad = "the sound is not Vorbis";
			goto done;
		}
		if (ogg_headers(&ogg, &vorbis, w.demux, audio) != 0) {
			bad = "the Vorbis headers do not read";
			goto done;
		}
	}
	while ((rc = nestegg_read_packet(w.demux, &pkt)) > 0) {
		unsigned int track = 0, count = 0, j;
		uint64_t ts = 0;
		int key = nestegg_packet_has_keyframe(pkt) == NESTEGG_PACKET_HAS_KEYFRAME_TRUE;
		if (nestegg_packet_track(pkt, &track) != 0 || nestegg_packet_count(pkt, &count) != 0
			|| nestegg_packet_tstamp(pkt, &ts) != 0) {
			bad = "a block does not read";
			break;
		}
		if (track == audio) { /* the end's DiscardPadding is the last block's */
			block_samples = 0;
			if (nestegg_packet_discard_padding(pkt, &padding_ns) != 0) padding_ns = 0;
		}
		for (j = 0; j < count && !bad; j++) {
			unsigned char *p = NULL;
			size_t n = 0;
			if (nestegg_packet_data(pkt, j, &p, &n) != 0) {
				bad = "a block does not read";
			} else if (track == w.track) { /* each frame of a laced block a packet, as FFmpeg's demuxer gives them */
				if (n == 0 || n > 0x7fffffffu || packets.size > 0xffffffffu - n) {
					bad = "a video packet is empty or too large";
					break;
				}
				buf_put(&packets, p, n);
				put32(&sizes, (uint32_t)n | (key && j == 0 ? KP_WEBM_KEY : 0u));
				buf_put(&times, &ts, sizeof ts);
				s->count++;
			} else if (track == audio) {
				uint32_t added = vorbis_samples(&vorbis, p, n);
				samples += added;
				block_samples += added;
				if (ogg.body.size >= KP_WEBM_OGG_PAGE) ogg_page(&ogg, 0); /* the last packet ends on the last page */
				ogg_packet(&ogg, p, n, samples);
			}
		}
		nestegg_free_packet(pkt);
		pkt = NULL;
		if (bad) break;
	}
	if (!bad && rc < 0) bad = "the stream is cut or malformed";
	if (!bad && s->count == 0) bad = "the video has no frames";
	if (!bad && audio != (unsigned int)-1) { /* the last page, with the sound's end */
		int64_t trim = padding_ns > 0 ? (int64_t)(((uint64_t)padding_ns * vorbis.rate + NS_PER_S / 2) / NS_PER_S) : 0;
		if (trim > block_samples) trim = block_samples; /* no more than the last block decodes */
		ogg.granule = samples - trim;
		ogg_page(&ogg, 1);
	}
	if (!bad && (packets.failed || sizes.failed || times.failed || ogg.out.failed || ogg.body.failed)) bad = "out of memory";
done:
	nestegg_destroy(w.demux);
	free(ogg.body.data);
	if (bad) {
		free(packets.data);
		free(sizes.data);
		free(times.data);
		free(ogg.out.data);
		memset(s, 0, sizeof *s);
		say(err, err_size, bad);
		return -1;
	}
	s->width = params.width;
	s->height = params.height;
	s->frame_ns = frame_ns;
	s->packets = packets.data;
	s->packets_size = packets.size;
	s->sizes = sizes.data;
	s->times = (uint64_t *)(void *)times.data;
	if (audio != (unsigned int)-1) {
		s->ogg = ogg.out.data;
		s->ogg_size = ogg.out.size;
		s->samples = samples;
	} else {
		free(ogg.out.data);
	}
	return 0;
}
