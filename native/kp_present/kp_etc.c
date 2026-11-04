/*
 * ETC2 RGBA8 (EAC alpha) and EAC R11 blocks for the HD pages of phones (ADR 0013, stage R4f): GLES 3.0 takes them
 * everywhere (S3TC is an extension phones rarely have), at the BC blocks' sizes (16 and 8 bytes a 4 x 4 block), so a
 * page of BC blocks is decoded (kp_bcn.c) and encoded again here (tools/product.sh, kp_hd_etc2). The colour is
 * encoded in ETC1's individual and differential modes (each half block a base colour and an intensity table, the
 * halves side by side or one above the other), its error weighted by the texel's alpha (a transparent texel's colour
 * is not seen: the pages are premultiplied when drawn); the alpha and the R11 channel by EAC's tables. The decoder
 * reads what the encoder writes and ETC2's planar mode; the T and H modes, which the encoder does not write, decode
 * as black.
 */
#include "kp_present.h"

#include <string.h>

/* ETC1's intensity modifiers per table codeword: the pixel index's (msb, lsb) 0 1 2 3 add a, b, -a, -b. */
static const int ETC_TABLES[8][2] = { { 2, 8 }, { 5, 17 }, { 9, 29 }, { 13, 42 }, { 18, 60 }, { 24, 80 }, { 33, 106 },
	{ 47, 183 } };
/* EAC's modifier tables (alpha and R11). */
static const int EAC_TABLES[16][8] = { { -3, -6, -9, -15, 2, 5, 8, 14 }, { -3, -7, -10, -13, 2, 6, 9, 12 },
	{ -2, -5, -8, -13, 1, 4, 7, 12 }, { -2, -4, -6, -13, 1, 3, 5, 12 }, { -3, -6, -8, -12, 2, 5, 7, 11 },
	{ -3, -7, -9, -11, 2, 6, 8, 10 }, { -4, -7, -8, -11, 3, 6, 7, 10 }, { -3, -5, -8, -11, 2, 4, 7, 10 },
	{ -2, -6, -8, -10, 1, 5, 7, 9 }, { -2, -5, -8, -10, 1, 4, 7, 9 }, { -2, -4, -8, -10, 1, 3, 7, 9 },
	{ -2, -5, -7, -10, 1, 4, 6, 9 }, { -3, -4, -7, -10, 2, 3, 6, 9 }, { -1, -2, -3, -10, 0, 1, 2, 9 },
	{ -4, -6, -8, -9, 3, 5, 7, 8 }, { -3, -5, -7, -9, 2, 4, 6, 8 } };

static int clamp255(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }

static void put_be64(uint8_t *out, uint64_t v)
{
	int k;
	for (k = 7; k >= 0; k--) {
		out[k] = (uint8_t)(v & 0xff);
		v >>= 8;
	}
}

static uint64_t get_be64(const uint8_t *in)
{
	uint64_t v = 0;
	int k;
	for (k = 0; k < 8; k++) v = v << 8 | in[k];
	return v;
}

/* --- the colour (ETC1 modes of ETC2) --- */

/* Texel k of a block in ETC's order (column-major: k = x * 4 + y) and whether it is in the second half block. */
static int second_half(int k, int flip) { return flip ? (k % 4) >= 2 : (k / 4) >= 2; }

/* The error of a half block's texels (rgba[k], weights w[k]) against base with table t, each texel's best modifier
 * into idx[k]. */
static long half_error(const int (*rgba)[4], const int *w, int flip, int half, const int base[3], int t, int *idx)
{
	long total = 0;
	int k;
	for (k = 0; k < 16; k++) {
		long best = -1;
		int m, pick = 0;
		if (second_half(k, flip) != half) continue;
		for (m = 0; m < 4; m++) {
			int mod = m & 2 ? -ETC_TABLES[t][m & 1] : ETC_TABLES[t][m & 1];
			long e = 0;
			int c;
			for (c = 0; c < 3; c++) {
				int d = clamp255(base[c] + mod) - rgba[k][c];
				e += (long)d * d;
			}
			if (best < 0 || e < best) {
				best = e;
				pick = m;
			}
		}
		total += best * w[k];
		idx[k] = pick;
	}
	return total;
}

/* The best table of a half block for base: its error, its table in *table, the texels' indices in idx. */
static long best_table(const int (*rgba)[4], const int *w, int flip, int half, const int base[3], int *table, int *idx)
{
	long best = -1;
	int t, tmp[16], k;
	for (t = 0; t < 8; t++) {
		long e = half_error(rgba, w, flip, half, base, t, tmp);
		if (best < 0 || e < best) {
			best = e;
			*table = t;
			for (k = 0; k < 16; k++) {
				if (second_half(k, flip) == half) idx[k] = tmp[k];
			}
		}
	}
	return best;
}

static void encode_colour(const int (*rgba)[4], const int *w, uint8_t out[8])
{
	long best = -1;
	uint64_t best_bits = 0;
	int flip;
	for (flip = 0; flip < 2; flip++) {
		int avg[2][3], half, c, mode;
		for (half = 0; half < 2; half++) { /* each half's average colour, weighted by alpha */
			long sum[3] = { 0, 0, 0 }, weight = 0;
			int k;
			for (k = 0; k < 16; k++) {
				if (second_half(k, flip) != half) continue;
				for (c = 0; c < 3; c++) sum[c] += (long)rgba[k][c] * w[k];
				weight += w[k];
			}
			for (c = 0; c < 3; c++) avg[half][c] = weight ? (int)((sum[c] + weight / 2) / weight) : 0;
		}
		for (mode = 0; mode < 2; mode++) { /* 0 individual (4 bits a channel), 1 differential (5 bits and a delta) */
			/* idx: each half's best_table writes its texels', the two halves all 16 (zeroed: GCC's -O3 cannot see it) */
			int q[2][3], base[2][3], table[2] = { 0, 0 }, idx[16] = { 0 }, fits = 1, k;
			long e = 0;
			uint64_t bits;
			for (half = 0; half < 2; half++) {
				for (c = 0; c < 3; c++) {
					if (mode == 0) {
						q[half][c] = (avg[half][c] * 15 + 127) / 255;
						base[half][c] = q[half][c] << 4 | q[half][c];
					} else {
						q[half][c] = (avg[half][c] * 31 + 127) / 255;
					}
				}
			}
			if (mode == 1) {
				for (c = 0; c < 3; c++) {
					int d = q[1][c] - q[0][c];
					if (d < -4 || d > 3) fits = 0;
				}
				if (!fits) continue;
				for (half = 0; half < 2; half++) {
					for (c = 0; c < 3; c++) base[half][c] = q[half][c] << 3 | q[half][c] >> 2;
				}
			}
			for (half = 0; half < 2; half++) e += best_table(rgba, w, flip, half, base[half], &table[half], idx);
			if (best >= 0 && e >= best) continue;
			best = e;
			if (mode == 0) {
				bits = (uint64_t)q[0][0] << 60 | (uint64_t)q[1][0] << 56 | (uint64_t)q[0][1] << 52
					| (uint64_t)q[1][1] << 48 | (uint64_t)q[0][2] << 44 | (uint64_t)q[1][2] << 40;
			} else {
				bits = (uint64_t)q[0][0] << 59 | (uint64_t)((q[1][0] - q[0][0]) & 7) << 56 | (uint64_t)q[0][1] << 51
					| (uint64_t)((q[1][1] - q[0][1]) & 7) << 48 | (uint64_t)q[0][2] << 43
					| (uint64_t)((q[1][2] - q[0][2]) & 7) << 40 | (uint64_t)1 << 33;
			}
			bits |= (uint64_t)table[0] << 37 | (uint64_t)table[1] << 34 | (uint64_t)flip << 32;
			for (k = 0; k < 16; k++) bits |= (uint64_t)(idx[k] >> 1) << (16 + k) | (uint64_t)(idx[k] & 1) << k;
			best_bits = bits;
		}
	}
	put_be64(out, best_bits);
}

static int expand4(int v) { return v << 4 | v; }
static int expand5(int v) { return v << 3 | v >> 2; }
static int expand6(int v) { return v << 2 | v >> 4; }
static int expand7(int v) { return v << 1 | v >> 6; }

/* A colour block's 16 texels (rgba, column-major k = x * 4 + y; alpha left as it is). */
static void decode_colour(const uint8_t in[8], uint8_t (*rgba)[4])
{
	uint64_t b = get_be64(in);
	int k, c, base[2][3], table[2], flip = (int)(b >> 32 & 1);
	if (b >> 33 & 1) { /* differential, or ETC2's T, H and planar modes when a channel's sum overflows */
		int q[3], d[3], sum[3];
		for (c = 0; c < 3; c++) {
			q[c] = (int)(b >> (59 - 8 * c) & 31);
			d[c] = (int)(b >> (56 - 8 * c) & 7);
			if (d[c] >= 4) d[c] -= 8;
			sum[c] = q[c] + d[c];
		}
		if (sum[0] < 0 || sum[0] > 31 || sum[1] < 0 || sum[1] > 31) { /* T or H: not written by the encoder */
			for (k = 0; k < 16; k++) rgba[k][0] = rgba[k][1] = rgba[k][2] = 0;
			return;
		}
		if (sum[2] < 0 || sum[2] > 31) { /* planar: O, H, V in RGB676, interpolated */
			int o[3], h[3], v[3];
			o[0] = expand6((int)(b >> 57 & 63));
			o[1] = expand7((int)((b >> 56 & 1) << 6 | (b >> 49 & 63)));
			o[2] = expand6((int)((b >> 48 & 1) << 5 | (b >> 43 & 3) << 3 | (b >> 39 & 7)));
			h[0] = expand6((int)((b >> 34 & 31) << 1 | (b >> 32 & 1)));
			h[1] = expand7((int)(b >> 25 & 127));
			h[2] = expand6((int)(b >> 19 & 63));
			v[0] = expand6((int)(b >> 13 & 63));
			v[1] = expand7((int)(b >> 6 & 127));
			v[2] = expand6((int)(b & 63));
			for (k = 0; k < 16; k++) {
				int x = k / 4, y = k % 4;
				for (c = 0; c < 3; c++) rgba[k][c] = (uint8_t)clamp255((x * (h[c] - o[c]) + y * (v[c] - o[c]) + 4 * o[c] + 2) >> 2);
			}
			return;
		}
		for (c = 0; c < 3; c++) {
			base[0][c] = expand5(q[c]);
			base[1][c] = expand5(sum[c]);
		}
	} else {
		for (c = 0; c < 3; c++) {
			base[0][c] = expand4((int)(b >> (60 - 8 * c) & 15));
			base[1][c] = expand4((int)(b >> (56 - 8 * c) & 15));
		}
	}
	table[0] = (int)(b >> 37 & 7);
	table[1] = (int)(b >> 34 & 7);
	for (k = 0; k < 16; k++) {
		int half = second_half(k, flip), m = (int)((b >> (16 + k) & 1) << 1 | (b >> k & 1));
		int mod = m & 2 ? -ETC_TABLES[table[half]][m & 1] : ETC_TABLES[table[half]][m & 1];
		for (c = 0; c < 3; c++) rgba[k][c] = (uint8_t)clamp255(base[half][c] + mod);
	}
}

/* --- EAC (alpha, R11) --- */

/* An EAC value of base, multiplier and modifier: 8 bits (alpha), or R11 read back as 8 bits. */
static int eac_value(int base, int mult, int mod, int r11)
{
	if (r11) {
		int v = base * 8 + 4 + (mult ? mod * mult * 8 : mod);
		v = v < 0 ? 0 : v > 2047 ? 2047 : v;
		return (v * 255 + 1023) / 2047;
	}
	return clamp255(base + mod * mult);
}

static void encode_eac(const int *v, int r11, uint8_t out[8])
{
	int lo = 255, hi = 0, k, t, best_base = v[0], best_mult = 1, best_table = 13, best_idx[16];
	long best = -1;
	for (k = 0; k < 16; k++) {
		if (v[k] < lo) lo = v[k];
		if (v[k] > hi) hi = v[k];
		best_idx[k] = 4; /* table 13's 0 */
	}
	if (lo != hi) {
		for (t = 0; t < 16; t++) {
			int tlo = EAC_TABLES[t][3], thi = EAC_TABLES[t][7], span = thi - tlo, m0 = (hi - lo + span / 2) / span, dm;
			for (dm = -1; dm <= 1; dm++) {
				int mult = m0 + dm, db;
				if (mult < 1 || mult > 15) continue;
				for (db = -2; db <= 2; db++) {
					int base = clamp255((lo + hi) / 2 - mult * (tlo + thi) / 2 + db), idx[16];
					long e = 0;
					for (k = 0; k < 16 && (best < 0 || e < best); k++) {
						int j, pick = 0, pe = -1;
						for (j = 0; j < 8; j++) {
							int d = eac_value(base, mult, EAC_TABLES[t][j], r11) - v[k];
							if (pe < 0 || d * d < pe) {
								pe = d * d;
								pick = j;
							}
						}
						e += pe;
						idx[k] = pick;
					}
					if (best < 0 || e < best) {
						best = e;
						best_base = base;
						best_mult = mult;
						best_table = t;
						memcpy(best_idx, idx, sizeof idx);
					}
				}
			}
		}
	} else if (r11) {
		best_base = lo; /* (lo * 8 + 4) * 255 / 2047 reads back as lo */
	}
	{
		uint64_t bits = (uint64_t)best_base << 56 | (uint64_t)best_mult << 52 | (uint64_t)best_table << 48;
		for (k = 0; k < 16; k++) bits |= (uint64_t)best_idx[k] << (45 - 3 * k);
		put_be64(out, bits);
	}
}

static void decode_eac(const uint8_t in[8], int r11, int *v)
{
	uint64_t b = get_be64(in);
	int base = (int)(b >> 56), mult = (int)(b >> 52 & 15), t = (int)(b >> 48 & 15), k;
	for (k = 0; k < 16; k++) v[k] = eac_value(base, mult, EAC_TABLES[t][b >> (45 - 3 * k) & 7], r11);
}

/* --- pages --- */

/* The page texel of texel k (ETC's column-major order) of block (bx, by) on a page w texels wide. */
static size_t texel_at(int32_t bx, int32_t by, int k, int32_t w)
{
	return (size_t)(by * 4 + k % 4) * (size_t)w + (size_t)(bx * 4 + k / 4);
}

void kp_etc2_encode(const uint8_t *rgba, int32_t w, int32_t h, uint8_t *blocks)
{
	int32_t bx, by;
	for (by = 0; by < h / 4; by++) {
		for (bx = 0; bx < w / 4; bx++) {
			int texels[16][4], weight[16], alpha[16], k;
			uint8_t *out = blocks + ((size_t)by * (size_t)(w / 4) + (size_t)bx) * 16;
			for (k = 0; k < 16; k++) {
				const uint8_t *p = rgba + 4 * texel_at(bx, by, k, w);
				int c;
				for (c = 0; c < 4; c++) texels[k][c] = p[c];
				alpha[k] = p[3];
				weight[k] = p[3] + 1; /* a transparent texel's colour counts little */
			}
			encode_eac(alpha, 0, out);
			encode_colour((const int (*)[4])texels, weight, out + 8);
		}
	}
}

void kp_etc2_rgb_encode(const uint8_t *rgba, int32_t w, int32_t h, uint8_t *blocks)
{
	int32_t bx, by;
	for (by = 0; by < h / 4; by++) {
		for (bx = 0; bx < w / 4; bx++) {
			int texels[16][4], weight[16], k, c;
			for (k = 0; k < 16; k++) {
				const uint8_t *p = rgba + 4 * texel_at(bx, by, k, w);
				for (c = 0; c < 4; c++) texels[k][c] = p[c];
				weight[k] = 1;
			}
			encode_colour((const int (*)[4])texels, weight, blocks + ((size_t)by * (size_t)(w / 4) + (size_t)bx) * 8);
		}
	}
}

void kp_eac_r11_encode(const uint8_t *r, int32_t w, int32_t h, uint8_t *blocks)
{
	int32_t bx, by;
	for (by = 0; by < h / 4; by++) {
		for (bx = 0; bx < w / 4; bx++) {
			int v[16], k;
			for (k = 0; k < 16; k++) v[k] = r[texel_at(bx, by, k, w)];
			encode_eac(v, 1, blocks + ((size_t)by * (size_t)(w / 4) + (size_t)bx) * 8);
		}
	}
}

void kp_etc2_decode(const uint8_t *blocks, int32_t w, int32_t h, uint8_t *rgba)
{
	int32_t bx, by;
	for (by = 0; by < h / 4; by++) {
		for (bx = 0; bx < w / 4; bx++) {
			const uint8_t *in = blocks + ((size_t)by * (size_t)(w / 4) + (size_t)bx) * 16;
			uint8_t texels[16][4];
			int alpha[16], k;
			decode_eac(in, 0, alpha);
			decode_colour(in + 8, texels);
			for (k = 0; k < 16; k++) {
				uint8_t *p = rgba + 4 * texel_at(bx, by, k, w);
				p[0] = texels[k][0];
				p[1] = texels[k][1];
				p[2] = texels[k][2];
				p[3] = (uint8_t)alpha[k];
			}
		}
	}
}

void kp_etc2_rgb_decode(const uint8_t *blocks, int32_t w, int32_t h, uint8_t *rgba)
{
	int32_t bx, by;
	for (by = 0; by < h / 4; by++) {
		for (bx = 0; bx < w / 4; bx++) {
			uint8_t texels[16][4];
			int k;
			decode_colour(blocks + ((size_t)by * (size_t)(w / 4) + (size_t)bx) * 8, texels);
			for (k = 0; k < 16; k++) {
				uint8_t *p = rgba + 4 * texel_at(bx, by, k, w);
				p[0] = texels[k][0];
				p[1] = texels[k][1];
				p[2] = texels[k][2];
				p[3] = 255;
			}
		}
	}
}

void kp_eac_r11_decode(const uint8_t *blocks, int32_t w, int32_t h, uint8_t *r)
{
	int32_t bx, by;
	for (by = 0; by < h / 4; by++) {
		for (bx = 0; bx < w / 4; bx++) {
			int v[16], k;
			decode_eac(blocks + ((size_t)by * (size_t)(w / 4) + (size_t)bx) * 8, 1, v);
			for (k = 0; k < 16; k++) r[texel_at(bx, by, k, w)] = (uint8_t)v[k];
		}
	}
}
