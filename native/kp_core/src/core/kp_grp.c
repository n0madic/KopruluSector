/* GRP files (kp_grp.h). */
#include "kp_grp.h"

#include <stdlib.h>
#include <string.h>

#include "kp_kpf.h"

int kp_grp_header(const uint8_t *grp, size_t size, uint32_t *frames, uint32_t *width, uint32_t *height)
{
	if (size < KP_GRP_HEADER) return -1;
	*frames = kp_le_u16(grp);
	*width = kp_le_u16(grp + 2);
	*height = kp_le_u16(grp + 4);
	return (uint64_t)KP_GRP_HEADER + (uint64_t)KP_GRP_RECORD * *frames > size ? -1 : 0;
}

void kp_grp_frame(const uint8_t *grp, uint32_t frame, KpGrpFrame *f)
{
	const uint8_t *r = grp + KP_GRP_HEADER + (size_t)KP_GRP_RECORD * frame;
	f->x = r[0];
	f->y = r[1];
	f->w = r[2];
	f->h = r[3];
	f->offset = ((uint32_t)r[4] | (uint32_t)r[5] << 8 | (uint32_t)r[6] << 16 | (uint32_t)r[7] << 24) & 0x7fffffffu;
}

int kp_grp_decode(const uint8_t *grp, size_t size, uint32_t frame, uint8_t *indices, uint8_t *mask, int *opaque_zero)
{
	KpGrpFrame f;
	uint32_t row;
	if (opaque_zero) *opaque_zero = 0;
	kp_grp_frame(grp, frame, &f);
	if ((uint64_t)f.offset + 2u * f.h > size) return -1;
	if (indices) memset(indices, 0, (size_t)f.w * f.h);
	if (mask) memset(mask, 0, (size_t)f.w * f.h);
	for (row = 0; row < f.h; row++) {
		size_t pos = (size_t)f.offset + kp_le_u16(grp + f.offset + 2 * row), out = (size_t)row * f.w;
		uint32_t px = 0;
		while (px < f.w) {
			uint32_t packet, n, k;
			if (pos >= size) return -1;
			packet = grp[pos++];
			if (packet & 0x80) {
				px += packet & 0x7f;
				continue;
			}
			if (packet & 0x40) {
				n = packet & 0x3f;
				if (n > f.w - px) n = f.w - px;
				if (pos >= size) return -1;
				if (n && grp[pos] == 0 && opaque_zero) *opaque_zero = 1;
				if (indices) memset(indices + out + px, grp[pos], n);
				pos++;
			} else {
				n = packet < f.w - px ? packet : f.w - px;
				if (pos + packet > size) return -1;
				for (k = 0; opaque_zero && k < n; k++)
					if (grp[pos + k] == 0) *opaque_zero = 1;
				if (indices) memcpy(indices + out + px, grp + pos, n);
				pos += packet;
			}
			if (n == 0) return -1;
			if (mask) memset(mask + out + px, 0xff, n);
			px += n;
		}
	}
	return 0;
}

/* One row's canonical runs (grp.py canonical_runs over row_packets): the packets' classes merged, empty ones dropped,
 * runs split at KP_GRP_RUN_MAX; a leading empty packet of the other class kept as a run of length 0 (the native loop
 * returns the first packet's class at x 0). Returns the bytes written (counted past cap), -1 when malformed. */
static int64_t row_runs(const uint8_t *grp, size_t size, const KpGrpFrame *f, uint32_t row, uint8_t *out, size_t cap)
{
	size_t pos;
	uint32_t px = 0, run = 0;
	int64_t n_out = 0;
	int first = -1, cls = -1; /* the first packet's class, the open run's (1 transparent) */
	if ((uint64_t)f->offset + 2u * row + 2 > size) return -1;
	pos = (size_t)f->offset + kp_le_u16(grp + f->offset + 2 * row);
#define EMIT(b) do { if ((size_t)n_out < cap && out) out[n_out] = (uint8_t)(b); n_out++; } while (0)
	while (px < f->w) {
		uint32_t packet, n;
		int t;
		if (pos >= size) return -1;
		packet = grp[pos++];
		if (packet & 0x80) {
			n = packet & 0x7f;
			t = 1;
		} else if (packet & 0x40) {
			n = packet & 0x3f;
			t = 0;
			pos++;
		} else {
			n = packet;
			t = 0;
			pos += n;
		}
		if (pos > size) return -1;
		if (first < 0) first = t;
		px += n;
		if (n == 0) continue;
		if (cls == t) {
			run += n;
			continue;
		}
		if (cls < 0 && t != first) EMIT(first ? KP_GRP_RUN_TRANSPARENT : 0);
		while (run > 0) {
			uint32_t chunk = run < KP_GRP_RUN_MAX ? run : KP_GRP_RUN_MAX;
			EMIT((cls ? KP_GRP_RUN_TRANSPARENT : 0) | chunk);
			run -= chunk;
		}
		cls = t;
		run = n;
	}
	if (cls < 0 && first >= 0) EMIT(first ? KP_GRP_RUN_TRANSPARENT : 0); /* only empty packets */
	while (run > 0) {
		uint32_t chunk = run < KP_GRP_RUN_MAX ? run : KP_GRP_RUN_MAX;
		EMIT((cls ? KP_GRP_RUN_TRANSPARENT : 0) | chunk);
		run -= chunk;
	}
#undef EMIT
	return n_out;
}

int64_t kp_grp_opacity(const uint8_t *grp, size_t size, uint32_t frame, uint8_t *out, size_t cap)
{
	KpGrpFrame f;
	uint32_t row;
	int64_t offset;
	kp_grp_frame(grp, frame, &f);
	offset = 2 * (int64_t)f.h;
	for (row = 0; row < f.h; row++) {
		int64_t n;
		if (offset > 0xffff) return -1;
		if (out && (size_t)(2 * row + 2) <= cap) {
			out[2 * row] = (uint8_t)offset;
			out[2 * row + 1] = (uint8_t)(offset >> 8);
		}
		n = row_runs(grp, size, &f, row, out && (size_t)offset < cap ? out + offset : NULL,
			out && (size_t)offset < cap ? cap - (size_t)offset : 0);
		if (n < 0) return -1;
		offset += n;
	}
	return offset;
}

/* The end of a row's packets that cover w pixels (grp.py _row_bytes), or 0 when they run past the file. */
static size_t row_end(const uint8_t *grp, size_t size, size_t pos, uint32_t w)
{
	uint32_t px = 0;
	while (px < w) {
		uint32_t packet;
		if (pos >= size) return 0;
		packet = grp[pos++];
		if (packet & 0x80) {
			px += packet & 0x7f;
		} else if (packet & 0x40) {
			px += packet & 0x3f;
			pos++;
		} else {
			px += packet;
			pos += packet;
		}
	}
	return pos > size ? 0 : pos;
}

uint8_t *kp_grp_single_frame(const uint8_t *grp, size_t size, uint32_t frame, size_t *out_size)
{
	uint32_t frames, width, height, row;
	KpGrpFrame f;
	size_t total, at;
	uint8_t *out;
	if (kp_grp_header(grp, size, &frames, &width, &height) != 0 || frame >= frames) return NULL;
	kp_grp_frame(grp, frame, &f);
	if ((uint64_t)f.offset + 2u * f.h > size) return NULL;
	total = 2u * f.h;
	for (row = 0; row < f.h; row++) {
		size_t start = (size_t)f.offset + kp_le_u16(grp + f.offset + 2 * row), end = row_end(grp, size, start, f.w);
		if (!end || total > 0xffff) return NULL; /* a row's offset is a u16 */
		total += end - start;
	}
	if (!(out = (uint8_t *)malloc(KP_GRP_HEADER + KP_GRP_RECORD + total))) return NULL;
	out[0] = 1;
	out[1] = 0;
	out[2] = (uint8_t)width;
	out[3] = (uint8_t)(width >> 8);
	out[4] = (uint8_t)height;
	out[5] = (uint8_t)(height >> 8);
	out[6] = f.x;
	out[7] = f.y;
	out[8] = f.w;
	out[9] = f.h;
	out[10] = KP_GRP_HEADER + KP_GRP_RECORD;
	out[11] = out[12] = out[13] = 0;
	at = 2u * f.h;
	for (row = 0; row < f.h; row++) {
		size_t start = (size_t)f.offset + kp_le_u16(grp + f.offset + 2 * row), end = row_end(grp, size, start, f.w);
		uint8_t *p = out + KP_GRP_HEADER + KP_GRP_RECORD;
		p[2 * row] = (uint8_t)at;
		p[2 * row + 1] = (uint8_t)(at >> 8);
		memcpy(p + at, grp + start, end - start);
		at += end - start;
	}
	*out_size = KP_GRP_HEADER + KP_GRP_RECORD + total;
	return out;
}
