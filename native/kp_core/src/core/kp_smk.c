/* The Smacker video decoder (kp_smk.h). */
#include "kp_smk.h"
#include "kp_kpf.h"

#include <stdlib.h>
#include <string.h>

#define HEADER_SIZE 104
#define KPW_ALPHA_CUT 128 /* a pixel below this alpha is transparent (index 0) */
#define SMK_NODE 0x80000000u
#define TYPE_PALETTE 1u
#define SMALL_VALUES 256
#define SMALL_NODES 512
#define MAX_TREE_SIZE (1u << 24) /* a header tree's declared bytes: beyond it the file is refused */

enum { TREE_MMAP, TREE_MCLR, TREE_FULL, TREE_TYPE };
enum { BLOCK_MONO, BLOCK_FULL, BLOCK_SKIP, BLOCK_FILL };

static const uint32_t BLOCK_RUNS[64] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22,
	23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51,
	52, 53, 54, 55, 56, 57, 58, 59, 128, 256, 512, 1024, 2048 };

/* The bit reader: least significant bit of each byte first. */
typedef struct {
	const uint8_t *p;
	size_t bits, at;
	int error;
} Bits;

static uint32_t bit(Bits *b)
{
	uint32_t v;
	if (b->at >= b->bits) {
		b->error = 1;
		return 0;
	}
	v = (b->p[b->at >> 3] >> (b->at & 7)) & 1u;
	b->at++;
	return v;
}

static uint32_t bits(Bits *b, int n)
{
	uint32_t v = 0;
	int i;
	for (i = 0; i < n; i++) v |= bit(b) << i;
	return v;
}

/* An 8-bit tree as nodes: node k < 0x10000 has its two children at left[k], right[k] (-1 for a leaf with value[k]). */
typedef struct {
	int16_t left[SMALL_NODES], right[SMALL_NODES];
	uint8_t value[SMALL_NODES];
	int count, present;
} Small;

static int small_tree(Bits *b, Small *t, int depth)
{
	int k;
	if (t->count >= SMALL_NODES || depth > 32) return -1;
	k = t->count++;
	if (!bit(b)) {
		t->left[k] = t->right[k] = -1;
		t->value[k] = (uint8_t)bits(b, 8);
		return k;
	}
	{
		int l = small_tree(b, t, depth + 1), r;
		if (l < 0) return -1;
		r = small_tree(b, t, depth + 1);
		if (r < 0) return -1;
		t->left[k] = (int16_t)l;
		t->right[k] = (int16_t)r;
		t->value[k] = 0;
	}
	return k;
}

static uint32_t small_get(Bits *b, const Small *t)
{
	int k = 0;
	if (!t->present) return 0;
	while (t->left[k] >= 0) k = bit(b) ? t->right[k] : t->left[k];
	return t->value[k];
}

typedef struct {
	const Small *low, *high;
	uint32_t escapes[3];
	int32_t *last;
} BigContext;

static int big_tree(Bits *b, KpSmkTree *t, int32_t capacity, const BigContext *c, int depth)
{
	if (t->count + 1 >= capacity || depth > 64) return -1;
	if (!bit(b)) {
		uint32_t lo = small_get(b, c->low), hi = small_get(b, c->high), v = lo | hi << 8;
		if (v == c->escapes[0]) {
			c->last[0] = t->count;
			v = 0;
		} else if (v == c->escapes[1]) {
			c->last[1] = t->count;
			v = 0;
		} else if (v == c->escapes[2]) {
			c->last[2] = t->count;
			v = 0;
		}
		t->values[t->count++] = (int32_t)v;
		return 1;
	} else {
		int32_t node = t->count++, r, r2;
		r = big_tree(b, t, capacity, c, depth + 1);
		if (r < 0) return -1;
		t->values[node] = (int32_t)(SMK_NODE | (uint32_t)r);
		r++;
		r2 = big_tree(b, t, capacity, c, depth + 1);
		if (r2 < 0) return -1;
		return r + r2;
	}
}

/* The entries a header tree of the declared size may hold (t->values has room for them: tree_capacity). */
static int32_t tree_capacity(uint32_t size) { return (int32_t)((size + 3) >> 2) + 4; }

/* The two 8-bit trees, the escapes and the big tree of one header tree (into the scratch trees low, high). */
static int read_big(Bits *b, KpSmkTree *t, uint32_t size, Small *low, Small *high)
{
	BigContext c;
	int32_t capacity = tree_capacity(size);
	if (bit(b)) {
		if (small_tree(b, low, 0) < 0) return -1;
		low->present = 1;
		(void)bit(b);
	}
	if (bit(b)) {
		if (small_tree(b, high, 0) < 0) return -1;
		high->present = 1;
		(void)bit(b);
	}
	c.low = low;
	c.high = high;
	c.escapes[0] = bits(b, 16);
	c.escapes[1] = bits(b, 16);
	c.escapes[2] = bits(b, 16);
	t->last[0] = t->last[1] = t->last[2] = -1;
	c.last = t->last;
	t->count = 0;
	if (big_tree(b, t, capacity, &c, 0) < 0) return -1;
	(void)bit(b);
	if (t->last[0] == -1) t->last[0] = t->count++;
	if (t->last[1] == -1) t->last[1] = t->count++;
	if (t->last[2] == -1) t->last[2] = t->count++;
	return b->error || t->count > capacity ? -1 : 0;
}

static int header_tree(Bits *b, KpSmkTree *t, uint32_t size)
{
	Small scratch[2];
	if (!bit(b)) { /* no tree: every code is 0 (the values are zero already) */
		t->count = 1;
		t->last[0] = t->last[1] = t->last[2] = 0;
		return 0;
	}
	memset(scratch, 0, sizeof scratch);
	return read_big(b, t, size, scratch, scratch + 1);
}

/* The one block a movie keeps (the portraits open one at every change, so not a block per tree): the four header
 * trees' entries, then the frame's width * height indices, all zero. The trees are given their declared sizes (none
 * for a KPW1). */
static int alloc_mem(KpSmk *s, const uint32_t tree_sizes[KP_SMK_TREES])
{
	size_t offsets[KP_SMK_TREES], at = 0, pixels = (size_t)s->width * s->height;
	int i;
	for (i = 0; i < KP_SMK_TREES; i++) {
		offsets[i] = at;
		at += tree_sizes ? (size_t)tree_capacity(tree_sizes[i]) * sizeof(int32_t) : 0;
	}
	s->mem = (uint8_t *)calloc(at + pixels, 1);
	if (s->mem == NULL) return -1;
	for (i = 0; i < KP_SMK_TREES; i++) s->trees[i].values = (int32_t *)(void *)(s->mem + offsets[i]);
	s->pixels = s->mem + at;
	return 0;
}

static uint32_t code(Bits *b, KpSmkTree *t)
{
	int32_t *table = t->values, *recode = t->values;
	int32_t v;
	while ((uint32_t)*table & SMK_NODE) {
		if (bit(b)) table += (uint32_t)*table & ~SMK_NODE;
		table++;
		if (table >= t->values + t->count) {
			b->error = 1;
			return 0;
		}
	}
	v = *table;
	if (v != recode[t->last[0]]) {
		recode[t->last[2]] = recode[t->last[1]];
		recode[t->last[1]] = recode[t->last[0]];
		recode[t->last[0]] = v;
	}
	return (uint32_t)v;
}

/* --- the palette records (libavformat's smacker demuxer documents them) --- */

/* A 6-bit colour level as 8 bits (Smacker's table: i * 4 + i / 16) */
static uint8_t level(uint32_t v) { return (uint8_t)((v & 0x3f) << 2 | (v & 0x3f) >> 4); }

/* The frame's palette record (its bytes p[0..n): the first counts 4-byte units) applied to s->palette: a byte with bit
 * 7 skips (b & 0x7f) + 1 entries, with bit 6 copies (b & 0x3f) + 1 entries of the palette before the record from the
 * next byte's index, else a new entry of three 6-bit levels (b and the next two bytes). -1 for a record that runs past
 * its end or copies past the palette's. */
static int palette_record(KpSmk *s, const uint8_t *p, size_t n)
{
	uint8_t old[768];
	size_t at = 1;
	uint32_t entry = 0;
	memcpy(old, s->palette, sizeof old);
	while (entry < 256) {
		uint32_t t;
		if (at >= n) return -1;
		t = p[at++];
		if (t & 0x80) {
			entry += (t & 0x7f) + 1;
		} else if (t & 0x40) {
			uint32_t from, count = (t & 0x3f) + 1;
			if (at >= n) return -1;
			from = p[at++];
			if (from + count > 256) return -1;
			while (count-- && entry < 256) {
				memcpy(s->palette + 3 * entry, old + 3 * from, 3);
				entry++;
				from++;
			}
		} else {
			if (n - at < 2) return -1;
			s->palette[3 * entry] = level(t);
			s->palette[3 * entry + 1] = level(p[at]);
			s->palette[3 * entry + 2] = level(p[at + 1]);
			at += 2;
			entry++;
		}
	}
	return 0;
}


static const KpSmkVideo *video_codec;

void kp_smk_set_video(const KpSmkVideo *codec) { video_codec = codec; }

void kp_smk_close(KpSmk *s)
{
	if (s->video) video_codec->close(s->video);
	kp_index_map_free(&s->map);
	free(s->mem);
	memset(s, 0, sizeof *s);
}

static int kpw_open(KpSmk *s, const uint8_t *data, size_t size)
{
	uint32_t w = 0, h = 0;
	size_t webm_at = KP_SMK_KPW_HEADER;
	s->kpw = 1;
	s->width = kp_le_u32(data + 4);
	s->height = kp_le_u32(data + 8);
	s->frames = kp_le_u32(data + 12);
	s->frame_rate = (int32_t)kp_le_u32(data + 16);
	s->flags = kp_le_u32(data + 20);
	s->ring = (s->flags & KP_SMK_RING) ? 1u : 0u;
	if (s->flags & KP_SMK_KPW_SHARED) webm_at += KP_INDEX_USABLE_SIZE;
	if (!video_codec || s->width == 0 || s->height == 0 || s->width > 4096 || s->height > 4096 || s->frames == 0
		|| s->frames > 65536 || size < webm_at)
		return -1;
	if (!(s->video = video_codec->open(data + webm_at, size - webm_at, &w, &h)) || w != s->width || h != s->height)
		return -1;
	if (alloc_mem(s, NULL) != 0 || kp_index_map_init(&s->map, data + 24) != 0) return -1;
	if (s->flags & KP_SMK_KPW_SHARED) kp_index_map_only(&s->map, data + KP_SMK_KPW_HEADER);
	kp_smk_rewind(s);
	return 0;
}

/* A KPW1 frame: the WebM's frame of the same number (its last past its end), each pixel its colour's index in the
 * screen's palette (kp_index_map.h), alpha below KPW_ALPHA_CUT transparent */
static int kpw_frame(KpSmk *s)
{
	uint8_t *pixels = s->target ? s->target : s->pixels;
	size_t i, n = (size_t)s->width * s->height;
	const uint8_t *rgba = video_codec->frame(s->video, s->frame);
	if (!rgba) return -1;
	for (i = 0; i < n; i++, rgba += 4)
		pixels[i] = kp_index_map(&s->map, rgba[0], rgba[1], rgba[2], rgba[3] >= KPW_ALPHA_CUT ? KP_INDEX_OPAQUE : 0);
	return 0;
}

int kp_smk_open(KpSmk *s, const uint8_t *data, size_t size)
{
	uint32_t tree_size, tree_sizes[KP_SMK_TREES], count, i;
	Bits b;
	memset(s, 0, sizeof *s);
	if (size >= KP_SMK_KPW_HEADER && memcmp(data, "KPW1", 4) == 0) {
		s->data = data;
		s->size = size;
		if (kpw_open(s, data, size) != 0) {
			kp_smk_close(s);
			return -1;
		}
		return 0;
	}
	if (size < HEADER_SIZE || memcmp(data, "SMK", 3) != 0 || (data[3] != '2' && data[3] != '4')) return -1;
	s->data = data;
	s->size = size;
	s->v4 = data[3] == '4';
	s->width = kp_le_u32(data + 4);
	s->height = kp_le_u32(data + 8);
	s->frames = kp_le_u32(data + 12);
	s->frame_rate = (int32_t)kp_le_u32(data + 16);
	s->flags = kp_le_u32(data + 20);
	s->ring = (s->flags & KP_SMK_RING) ? 1u : 0u;
	tree_size = kp_le_u32(data + 52);
	for (i = 0; i < KP_SMK_TREES; i++) tree_sizes[i] = kp_le_u32(data + 56 + 4 * i);
	if (s->width == 0 || s->height == 0 || s->width > 4096 || s->height > 4096 || s->frames == 0 || s->frames > 65536) {
		return -1;
	}
	for (i = 0; i < KP_SMK_TREES; i++) {
		if (tree_sizes[i] > MAX_TREE_SIZE) return -1;
	}
	count = s->frames + s->ring;
	s->sizes_at = HEADER_SIZE;
	s->types_at = s->sizes_at + 4 * (size_t)count;
	/* compared without sums that could wrap where size_t is 32-bit (the web) */
	if (tree_size > KP_SMK_TREES * MAX_TREE_SIZE || s->types_at > size || count > size - s->types_at
		|| tree_size > size - s->types_at - count) {
		return -1;
	}
	s->frame_types = data + s->types_at;
	if (alloc_mem(s, tree_sizes) != 0) return -1;
	b.p = data + s->types_at + count;
	b.bits = (size_t)tree_size * 8;
	b.at = 0;
	b.error = 0;
	for (i = 0; i < KP_SMK_TREES; i++) {
		if (header_tree(&b, &s->trees[i], tree_sizes[i]) != 0) {
			kp_smk_close(s);
			return -1;
		}
	}
	s->frames_at = s->types_at + count + tree_size;
	kp_smk_rewind(s);
	return 0;
}

void kp_smk_rewind(KpSmk *s)
{
	s->frame = 0;
	s->frame_pos = s->frames_at;
	memset(s->pixels, 0, (size_t)s->width * s->height);
	memset(s->palette, 0, sizeof s->palette);
}

uint32_t kp_smk_frame_time(const KpSmk *s)
{
	if (s->frame_rate > 0) return (uint32_t)s->frame_rate * 100u;
	if (s->frame_rate < 0) return (uint32_t)-s->frame_rate;
	return 0;
}

static void last_reset(KpSmkTree *t)
{
	t->values[t->last[0]] = 0;
	t->values[t->last[1]] = 0;
	t->values[t->last[2]] = 0;
}

void kp_smk_set_target(KpSmk *s, uint8_t *target) { s->target = target; }

static int decode_video(KpSmk *s, const uint8_t *p, size_t n)
{
	uint32_t bw = s->width / 4, blocks = bw * (s->height / 4), blk = 0, stride = s->width;
	uint8_t *pixels = s->target ? s->target : s->pixels;
	Bits b;
	int i;
	b.p = p;
	b.bits = n * 8;
	b.at = 0;
	b.error = 0;
	for (i = 0; i < KP_SMK_TREES; i++) last_reset(&s->trees[i]);
	while (blk < blocks && !b.error) {
		uint32_t type = code(&b, &s->trees[TREE_TYPE]), run = BLOCK_RUNS[(type >> 2) & 0x3f], mode;
		switch (type & 3) {
		case BLOCK_MONO:
			while (run-- && blk < blocks) {
				uint32_t clr = code(&b, &s->trees[TREE_MCLR]), map = code(&b, &s->trees[TREE_MMAP]), k;
				uint8_t hi = (uint8_t)(clr >> 8), lo = (uint8_t)clr, *out = pixels + (blk / bw) * stride * 4 + (blk % bw) * 4;
				for (k = 0; k < 4; k++) {
					out[0] = (map & 1) ? hi : lo;
					out[1] = (map & 2) ? hi : lo;
					out[2] = (map & 4) ? hi : lo;
					out[3] = (map & 8) ? hi : lo;
					map >>= 4;
					out += stride;
				}
				blk++;
			}
			break;
		case BLOCK_FULL:
			mode = 0;
			if (s->v4) {
				if (bit(&b)) mode = 1;
				else if (bit(&b)) mode = 2;
			}
			while (run-- && blk < blocks) {
				uint8_t *out = pixels + (blk / bw) * stride * 4 + (blk % bw) * 4;
				uint32_t k, pix;
				switch (mode) {
				case 0:
					for (k = 0; k < 4; k++) {
						pix = code(&b, &s->trees[TREE_FULL]);
						out[2] = (uint8_t)pix;
						out[3] = (uint8_t)(pix >> 8);
						pix = code(&b, &s->trees[TREE_FULL]);
						out[0] = (uint8_t)pix;
						out[1] = (uint8_t)(pix >> 8);
						out += stride;
					}
					break;
				case 1: /* SMK4: one value per 2x2 */
					pix = code(&b, &s->trees[TREE_FULL]);
					out[0] = out[1] = (uint8_t)pix;
					out[2] = out[3] = (uint8_t)(pix >> 8);
					out += stride;
					out[0] = out[1] = (uint8_t)pix;
					out[2] = out[3] = (uint8_t)(pix >> 8);
					out += stride;
					pix = code(&b, &s->trees[TREE_FULL]);
					out[0] = out[1] = (uint8_t)pix;
					out[2] = out[3] = (uint8_t)(pix >> 8);
					out += stride;
					out[0] = out[1] = (uint8_t)pix;
					out[2] = out[3] = (uint8_t)(pix >> 8);
					break;
				default: /* SMK4: two rows each */
					for (k = 0; k < 2; k++) {
						uint32_t p1 = code(&b, &s->trees[TREE_FULL]), p2 = code(&b, &s->trees[TREE_FULL]);
						out[2] = (uint8_t)p1;
						out[3] = (uint8_t)(p1 >> 8);
						out[0] = (uint8_t)p2;
						out[1] = (uint8_t)(p2 >> 8);
						out += stride;
						out[2] = (uint8_t)p1;
						out[3] = (uint8_t)(p1 >> 8);
						out[0] = (uint8_t)p2;
						out[1] = (uint8_t)(p2 >> 8);
						out += stride;
					}
					break;
				}
				blk++;
			}
			break;
		case BLOCK_SKIP:
			while (run-- && blk < blocks) blk++;
			break;
		default: /* BLOCK_FILL */
			mode = type >> 8;
			while (run-- && blk < blocks) {
				uint8_t *out = pixels + (blk / bw) * stride * 4 + (blk % bw) * 4;
				uint32_t k;
				for (k = 0; k < 4; k++) {
					memset(out, (int)(uint8_t)mode, 4);
					out += stride;
				}
				blk++;
			}
			break;
		}
	}
	return b.error ? -1 : 0;
}

static int kpw_decode(KpSmk *s)
{
	if (kpw_frame(s) != 0) return -1;
	s->frame++;
	if (s->ring && s->frame == s->frames + 1) { /* as an SMK ring movie: its extra frame, then frame 1 */
		s->frame = 1;
	} else if (!s->ring && s->frame >= s->frames) {
		s->frame = 0;
	}
	return 0;
}

int kp_smk_decode(KpSmk *s)
{
	uint32_t frame_size, type, i;
	size_t pos = s->frame_pos, end;
	if (s->frame >= s->frames + s->ring) kp_smk_rewind(s);
	if (s->kpw) return kpw_decode(s);
	pos = s->frame_pos;
	frame_size = kp_le_u32(s->data + s->sizes_at + 4 * (size_t)s->frame) & ~3u;
	type = s->frame_types[s->frame];
	if (pos > s->size || frame_size > s->size - pos) return -1;
	end = pos + frame_size;
	if (type & TYPE_PALETTE) { /* the palette record: its first byte counts 4-byte units */
		size_t n;
		if (pos >= end || (n = (size_t)s->data[pos] * 4) == 0 || n > end - pos
			|| palette_record(s, s->data + pos, n) != 0)
			return -1;
		pos += n;
	}
	for (i = 1; i < 8; i++) { /* the audio tracks: a u32 size (itself included) each */
		if (type & (1u << i)) {
			uint32_t n;
			if (pos > end || end - pos < 4) return -1; /* the palette's count may have passed the frame's end */
			n = kp_le_u32(s->data + pos);
			if (n < 4 || n > end - pos) return -1;
			pos += n;
		}
	}
	if (pos > end || decode_video(s, s->data + pos, end - pos) != 0) return -1;
	s->frame_pos = end;
	s->frame++;
	if (s->ring && s->frame == s->frames + 1) {
		/* SmackNextFrame after a ring movie's extra frame (header flag 1, the delta back to frame 0): frame 1 */
		s->frame = 1;
		s->frame_pos = s->frames_at + (kp_le_u32(s->data + s->sizes_at) & ~3u);
	} else if (!s->ring && s->frame >= s->frames) {
		s->frame = 0;
		s->frame_pos = s->frames_at;
	}
	return 0;
}

/* --- the sound (libavcodec's smackaud decoder documents its bit stream) --- */

#define SOUND_PACKED 0x80000000u
#define SOUND_16BITS 0x20000000u
#define SOUND_STEREO 0x10000000u
#define SOUND_BINK 0x0c000000u /* Bink's RDFT or DCT sound, which Smacker 2 and 4 movies do not use */
#define SOUND_RATE 0x00ffffffu
#define SOUND_CHUNK_MAX (1u << 24) /* a packed chunk's unpacked bytes at most */

int kp_smk_sound_format(const KpSmk *s, int track, KpSmkSoundFormat *out)
{
	uint32_t v;
	if (s->kpw || track < 0 || track >= KP_SMK_TRACKS) return -1;
	v = kp_le_u32(s->data + 72 + 4 * (size_t)track);
	if (!(v & SOUND_RATE) || (v & SOUND_BINK)) return -1;
	out->rate = v & SOUND_RATE;
	out->channels = (v & SOUND_STEREO) ? 2 : 1;
	out->bits = (v & SOUND_16BITS) ? 16 : 8;
	out->packed = (v & SOUND_PACKED) != 0;
	return 0;
}

/* A packed chunk (u32 unpacked bytes, then the bits) appended to out at *at: a bit for data, stereo and 16 bits as the
 * header says, a Huffman tree of byte values per channel (two for 16 bits: low, high), the first samples (16 bits big
 * endian, the right channel's first), then each sample the last one of its channel plus the next delta, wrapping. */
static int packed_chunk(const KpSmkSoundFormat *f, const uint8_t *p, size_t n, uint8_t *out, size_t *at)
{
	Small trees[4];
	Bits b;
	uint32_t size, i, count, stereo = f->channels == 2, wide = f->bits == 16, frame_bytes = (uint32_t)f->channels * (wide ? 2u : 1u);
	if (n < 4) return -1;
	size = kp_le_u32(p);
	if (size > SOUND_CHUNK_MAX || size % frame_bytes) return -1;
	b.p = p + 4;
	b.bits = (n - 4) * 8;
	b.at = 0;
	b.error = 0;
	if (!bit(&b)) return b.error ? -1 : 0; /* no data in this chunk */
	if (bit(&b) != stereo || bit(&b) != wide) return -1;
	count = 1u << (wide + stereo);
	memset(trees, 0, sizeof trees);
	for (i = 0; i < count; i++) {
		(void)bit(&b);
		if (small_tree(&b, &trees[i], 0) < 0) return -1;
		trees[i].present = 1;
		(void)bit(&b);
	}
	if (b.error || size == 0) return b.error ? -1 : 0;
	if (wide) {
		uint8_t *o = out + *at;
		uint16_t pred[2];
		uint32_t samples = size / 2;
		for (i = stereo + 1; i-- > 0;) {
			uint32_t v = bits(&b, 16);
			pred[i] = (uint16_t)((v >> 8) | (v & 0xff) << 8);
		}
		for (i = 0; i <= stereo && i < samples; i++) kp_le_put16(o + 2 * (size_t)i, pred[i]);
		for (; i < samples; i++) {
			uint32_t c = i & stereo, lo = small_get(&b, &trees[2 * c]), hi = small_get(&b, &trees[2 * c + 1]);
			pred[c] = (uint16_t)(pred[c] + (lo | hi << 8));
			kp_le_put16(o + 2 * (size_t)i, pred[c]);
		}
	} else {
		uint8_t *o = out + *at, pred[2];
		for (i = stereo + 1; i-- > 0;) pred[i] = (uint8_t)bits(&b, 8);
		for (i = 0; i <= stereo && i < size; i++) o[i] = pred[i];
		for (; i < size; i++) {
			uint32_t c = i & stereo;
			pred[c] = (uint8_t)(pred[c] + small_get(&b, &trees[c]));
			o[i] = pred[c];
		}
	}
	if (b.error) return -1;
	*at += size;
	return 0;
}

int kp_smk_sound(const KpSmk *s, int track, KpSmkSoundFormat *format, uint8_t **pcm, size_t *size)
{
	uint32_t frame, count = s->frames + s->ring, pass;
	size_t total = 0;
	uint8_t *out = NULL;
	*pcm = NULL;
	*size = 0;
	if (kp_smk_sound_format(s, track, format) != 0) return -1;
	for (pass = 0; pass < 2; pass++) { /* the bytes counted, then decoded */
		size_t pos = s->frames_at, at = 0;
		for (frame = 0; frame < count; frame++) {
			uint32_t frame_size = kp_le_u32(s->data + s->sizes_at + 4 * (size_t)frame) & ~3u, type = s->frame_types[frame], i;
			size_t end;
			if (pos > s->size || frame_size > s->size - pos) goto fail;
			end = pos + frame_size;
			if (type & TYPE_PALETTE) {
				if (pos >= end) goto fail;
				pos += (size_t)s->data[pos] * 4;
			}
			for (i = 0; i < KP_SMK_TRACKS; i++) {
				uint32_t n;
				if (!(type & (2u << i))) continue;
				if (pos > end || end - pos < 4) goto fail;
				n = kp_le_u32(s->data + pos);
				if (n < 4 || n > end - pos) goto fail;
				if (i == (uint32_t)track) {
					const uint8_t *chunk = s->data + pos + 4;
					size_t chunk_size = n - 4;
					if (!format->packed) {
						if (pass && chunk_size) memcpy(out + at, chunk, chunk_size);
						at += chunk_size;
					} else if (!pass) {
						if (chunk_size >= 4) at += kp_le_u32(chunk) <= SOUND_CHUNK_MAX ? kp_le_u32(chunk) : 0;
					} else if (packed_chunk(format, chunk, chunk_size, out, &at) != 0) {
						goto fail;
					}
				}
				pos += n;
			}
			pos = end;
		}
		if (!pass) {
			total = at;
			if (!(out = (uint8_t *)malloc(total ? total : 1))) return -1;
		} else {
			total = at; /* a packed chunk without data adds nothing */
		}
	}
	*pcm = out;
	*size = total;
	return 0;
fail:
	free(out);
	return -1;
}
