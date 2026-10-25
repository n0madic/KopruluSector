/* The Smacker video decoder (kp_smk.h). */
#include "kp_smk.h"

#include <stdlib.h>
#include <string.h>

#define HEADER_SIZE 104
#define SMK_NODE 0x80000000u
#define FLAG_RING 1u
#define TYPE_PALETTE 1u
#define SMALL_VALUES 256
#define SMALL_NODES 512

enum { TREE_MMAP, TREE_MCLR, TREE_FULL, TREE_TYPE };
enum { BLOCK_MONO, BLOCK_FULL, BLOCK_SKIP, BLOCK_FILL };

static const uint32_t BLOCK_RUNS[64] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22,
	23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51,
	52, 53, 54, 55, 56, 57, 58, 59, 128, 256, 512, 1024, 2048 };

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

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

/* The two 8-bit trees, the escapes and the big tree of one header tree (into the scratch trees low, high). */
static int read_big(Bits *b, KpSmkTree *t, uint32_t size, Small *low, Small *high)
{
	BigContext c;
	int32_t capacity;
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
	capacity = (int32_t)((size + 3) >> 2) + 4;
	t->values = (int32_t *)calloc((size_t)capacity, sizeof(int32_t));
	if (t->values == NULL) return -1;
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
	Small *scratch;
	int r;
	if (!bit(b)) { /* no tree: every code is 0 */
		t->values = (int32_t *)calloc(4, sizeof(int32_t));
		if (t->values == NULL) return -1;
		t->count = 1;
		t->last[0] = t->last[1] = t->last[2] = 0;
		return 0;
	}
	scratch = (Small *)calloc(2, sizeof(Small));
	if (scratch == NULL) return -1;
	r = read_big(b, t, size, scratch, scratch + 1);
	free(scratch);
	return r;
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

void kp_smk_close(KpSmk *s)
{
	int i;
	for (i = 0; i < KP_SMK_TREES; i++) free(s->trees[i].values);
	free(s->pixels);
	memset(s, 0, sizeof *s);
}

int kp_smk_open(KpSmk *s, const uint8_t *data, size_t size)
{
	uint32_t tree_size, tree_sizes[KP_SMK_TREES], count, i;
	Bits b;
	memset(s, 0, sizeof *s);
	if (size < HEADER_SIZE || memcmp(data, "SMK", 3) != 0 || (data[3] != '2' && data[3] != '4')) return -1;
	s->data = data;
	s->size = size;
	s->v4 = data[3] == '4';
	s->width = rd32(data + 4);
	s->height = rd32(data + 8);
	s->frames = rd32(data + 12);
	s->frame_rate = (int32_t)rd32(data + 16);
	s->flags = rd32(data + 20);
	s->ring = (s->flags & FLAG_RING) ? 1u : 0u;
	tree_size = rd32(data + 52);
	for (i = 0; i < KP_SMK_TREES; i++) tree_sizes[i] = rd32(data + 56 + 4 * i);
	if (s->width == 0 || s->height == 0 || s->width > 4096 || s->height > 4096 || s->frames == 0 || s->frames > 65536) {
		return -1;
	}
	count = s->frames + s->ring;
	s->sizes_at = HEADER_SIZE;
	s->types_at = s->sizes_at + 4 * (size_t)count;
	if (s->types_at + count + tree_size > size) return -1;
	s->frame_types = data + s->types_at;
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
	s->pixels = (uint8_t *)calloc((size_t)s->width * s->height, 1);
	if (s->pixels == NULL) {
		kp_smk_close(s);
		return -1;
	}
	kp_smk_rewind(s);
	return 0;
}

void kp_smk_rewind(KpSmk *s)
{
	s->frame = 0;
	s->frame_pos = s->frames_at;
	memset(s->pixels, 0, (size_t)s->width * s->height);
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

int kp_smk_decode(KpSmk *s)
{
	uint32_t frame_size, type, i;
	size_t pos = s->frame_pos, end;
	if (s->frame >= s->frames + s->ring) kp_smk_rewind(s);
	pos = s->frame_pos;
	frame_size = rd32(s->data + s->sizes_at + 4 * (size_t)s->frame) & ~3u;
	type = s->frame_types[s->frame];
	end = pos + frame_size;
	if (end > s->size) return -1;
	if (type & TYPE_PALETTE) { /* the palette record: its first byte counts 4-byte units */
		if (pos >= end) return -1;
		pos += (size_t)s->data[pos] * 4;
	}
	for (i = 1; i < 8; i++) { /* the audio tracks: a u32 size (itself included) each */
		if (type & (1u << i)) {
			uint32_t n;
			if (pos + 4 > end) return -1;
			n = rd32(s->data + pos);
			if (n < 4 || pos + n > end) return -1;
			pos += n;
		}
	}
	if (pos > end || decode_video(s, s->data + pos, end - pos) != 0) return -1;
	s->frame_pos = end;
	s->frame++;
	if (s->ring && s->frame == s->frames + 1) {
		/* SmackNextFrame after a ring movie's extra frame (header flag 1, the delta back to frame 0): frame 1 */
		s->frame = 1;
		s->frame_pos = s->frames_at + (rd32(s->data + s->sizes_at) & ~3u);
	} else if (!s->ring && s->frame >= s->frames) {
		s->frame = 0;
		s->frame_pos = s->frames_at;
	}
	return 0;
}
