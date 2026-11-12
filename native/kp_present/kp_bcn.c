/*
 * BC1 (DXT1) and BC3 (DXT5) blocks decoded to RGBA8, for a context without S3TC (ADR 0013, stage R3): the HD art keeps
 * its blocks (kp_anim.c, kp_hd_scr.c); kp_gfx uploads them as they are where it can, else these images.
 */
#include "kp_present.h"

#include <string.h>

static void rgb565(uint32_t c, uint8_t *o)
{
	uint32_t r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
	o[0] = (uint8_t)((r << 3) | (r >> 2));
	o[1] = (uint8_t)((g << 2) | (g >> 4));
	o[2] = (uint8_t)((b << 3) | (b >> 2));
	o[3] = 255;
}

/* The 16 texels of a colour block; four: the four-colour mode whatever the endpoints' order (BC3's colours). */
static void colour_block(const uint8_t *b, int four, uint8_t texels[16][4])
{
	uint32_t c0 = (uint32_t)b[0] | (uint32_t)b[1] << 8, c1 = (uint32_t)b[2] | (uint32_t)b[3] << 8;
	uint32_t bits = (uint32_t)b[4] | (uint32_t)b[5] << 8 | (uint32_t)b[6] << 16 | (uint32_t)b[7] << 24;
	uint8_t p[4][4];
	int i;
	rgb565(c0, p[0]);
	rgb565(c1, p[1]);
	for (i = 0; i < 3; i++) {
		if (four || c0 > c1) {
			p[2][i] = (uint8_t)((2 * p[0][i] + p[1][i]) / 3);
			p[3][i] = (uint8_t)((p[0][i] + 2 * p[1][i]) / 3);
		} else {
			p[2][i] = (uint8_t)((p[0][i] + p[1][i]) / 2);
			p[3][i] = 0;
		}
	}
	p[2][3] = 255;
	p[3][3] = (four || c0 > c1) ? 255 : 0;
	for (i = 0; i < 16; i++) memcpy(texels[i], p[(bits >> (2 * i)) & 3], 4);
}

static void alpha_block(const uint8_t *b, uint8_t alpha[16])
{
	uint32_t a0 = b[0], a1 = b[1], t[8], i;
	uint64_t bits = 0;
	for (i = 0; i < 6; i++) bits |= (uint64_t)b[2 + i] << (8 * i);
	t[0] = a0;
	t[1] = a1;
	if (a0 > a1) {
		for (i = 1; i < 7; i++) t[i + 1] = ((7 - i) * a0 + i * a1) / 7;
	} else {
		for (i = 1; i < 5; i++) t[i + 1] = ((5 - i) * a0 + i * a1) / 5;
		t[6] = 0;
		t[7] = 255;
	}
	for (i = 0; i < 16; i++) alpha[i] = (uint8_t)t[(bits >> (3 * i)) & 7];
}

void kp_bcn_decode(const uint8_t *blocks, int32_t w, int32_t h, int bc3, uint8_t *rgba)
{
	int32_t bw = (w + 3) / 4, bh = (h + 3) / 4, bx, by, i;
	size_t step = bc3 ? 16 : 8;
	for (by = 0; by < bh; by++) {
		for (bx = 0; bx < bw; bx++) {
			const uint8_t *b = blocks + ((size_t)by * (size_t)bw + (size_t)bx) * step;
			uint8_t texels[16][4], alpha[16];
			colour_block(b + (bc3 ? 8 : 0), bc3, texels);
			if (bc3) {
				alpha_block(b, alpha);
				for (i = 0; i < 16; i++) texels[i][3] = alpha[i];
			}
			for (i = 0; i < 16; i++) {
				int32_t x = bx * 4 + (i & 3), y = by * 4 + (i >> 2);
				if (x < w && y < h) memcpy(rgba + ((size_t)y * (size_t)w + (size_t)x) * 4, texels[i], 4);
			}
		}
	}
}
