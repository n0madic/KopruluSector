/* StarCraft: Remastered's own formats brought back to 1.17's files (kp_scr.h). */
#include "kp_scr.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kp_grp.h"
#include "kp_kpf.h"
#include "kp_present.h"
#include "kp_text.h"

#define DDS_HEADER 128
#define DDS_PF_FOURCC 0x4u
#define DDS_PF_RGB 0x40u
#define DDS_MAX_SIDE 16384
#define CONTAINER_HEADER 8
#define CONTAINER_ENTRY 12
#define PCX_HEADER 128
#define PCX_MARKER 0x0c
#define PCX_RUN_MAX 0x3f
#define PCX_RUN 0xc0
#define GRP_SKIP 0x80
#define GRP_REPEAT 0x40
#define GRP_RUN_MAX 0x7f
#define GRP_COUNT_MAX 0x3f
#define GRP_REPEAT_MIN 3
#define BYTE_MAX 0xff
#define REPLACEMENT_CHARACTER 0xfffd /* U+FFFD: what Python's decoding with errors="replace" gives */
#define TBL_OFFSET_MAX 0xffff
#define WPE_ENTRY 4

static int fail(char *err, size_t err_size, const char *what)
{
	if (err_size) snprintf(err, err_size, "%s", what);
	return -1;
}

/* --- DDS --- */

int kp_scr_dds(const uint8_t *data, size_t size, KpScrImage *out, char *err, size_t err_size)
{
	uint32_t w, h, flags;
	const uint8_t *fourcc, *body;
	size_t body_size, pixels;
	memset(out, 0, sizeof *out);
	if (size < DDS_HEADER || memcmp(data, "DDS ", 4) != 0) return fail(err, err_size, "not a DDS file");
	h = kp_le_u32(data + 12);
	w = kp_le_u32(data + 16);
	flags = kp_le_u32(data + 80);
	fourcc = data + 84;
	if (w == 0 || h == 0 || w > DDS_MAX_SIDE || h > DDS_MAX_SIDE) return fail(err, err_size, "a DDS of no size or too large");
	body = data + DDS_HEADER;
	body_size = size - DDS_HEADER;
	pixels = (size_t)w * h;
	if (flags & DDS_PF_FOURCC) {
		int bc3;
		size_t need;
		if (memcmp(fourcc, "DXT1", 4) == 0) bc3 = 0;
		else if (memcmp(fourcc, "DXT5", 4) == 0) bc3 = 1;
		else return fail(err, err_size, "an unsupported DDS format");
		need = (size_t)((w + 3) / 4) * ((h + 3) / 4) * (bc3 ? 16u : 8u);
		if (body_size < need) return fail(err, err_size, "DXT data truncated");
		if (!(out->rgba = (uint8_t *)malloc(pixels * 4))) return fail(err, err_size, "out of memory");
		kp_bcn_decode(body, (int32_t)w, (int32_t)h, bc3, out->rgba);
	} else {
		if (!(flags & DDS_PF_RGB)) return fail(err, err_size, "unsupported DDS pixel format flags");
		if (kp_le_u32(data + 88) != 32 || kp_le_u32(data + 92) != 0xffu || kp_le_u32(data + 96) != 0xff00u || kp_le_u32(data + 100) != 0xff0000u
			|| kp_le_u32(data + 104) != 0xff000000u)
			return fail(err, err_size, "an unsupported uncompressed DDS");
		if (body_size < pixels * 4) return fail(err, err_size, "pixel data truncated");
		if (!(out->rgba = (uint8_t *)malloc(pixels * 4))) return fail(err, err_size, "out of memory");
		memcpy(out->rgba, body, pixels * 4);
	}
	out->width = w;
	out->height = h;
	return 0;
}

void kp_scr_image_free(KpScrImage *im)
{
	free(im->rgba);
	memset(im, 0, sizeof *im);
}

int kp_scr_dds_container(const uint8_t *data, size_t size, KpScrDdsEntry **entries, uint32_t *count)
{
	uint32_t n, i;
	size_t at = CONTAINER_HEADER;
	*entries = NULL;
	*count = 0;
	if (size < CONTAINER_HEADER || kp_le_u32(data) != size) return -1;
	n = kp_le_u16(data + 4);
	if (!(*entries = (KpScrDdsEntry *)calloc(n ? n : 1, sizeof **entries))) return -1;
	for (i = 0; i < n; i++) {
		uint32_t length;
		if (size - at < CONTAINER_ENTRY) goto bad;
		length = kp_le_u32(data + at + 8);
		(*entries)[i].width = kp_le_u16(data + at + 4);
		(*entries)[i].height = kp_le_u16(data + at + 6);
		at += CONTAINER_ENTRY;
		if (length > size - at) goto bad;
		(*entries)[i].dds = data + at;
		(*entries)[i].size = length;
		at += length;
	}
	*count = n;
	return 0;
bad:
	free(*entries);
	*entries = NULL;
	return -1;
}

/* --- fitting and indices --- */

enum { FIT_NONE, FIT_SAMPLE, FIT_CROP };

static int fit_mode(uint32_t w, uint32_t h, uint32_t width, uint32_t height)
{
	if (w > width && w % width == 0 && h % height == 0 && w / width == h / height) return FIT_SAMPLE;
	if (width <= w && w < width + 4 && h < height + 4) return FIT_CROP;
	return FIT_NONE;
}

int kp_scr_fits(uint32_t w, uint32_t h, uint32_t width, uint32_t height)
{
	return width && height && fit_mode(w, h, width, height) != FIT_NONE;
}

int kp_scr_fit(KpScrImage *im, uint32_t width, uint32_t height, uint32_t *above)
{
	uint32_t w = im->width, h = im->height, x, y;
	int mode;
	if (!width || !height) return -1;
	mode = fit_mode(w, h, width, height);
	if (mode == FIT_SAMPLE) {
		uint32_t k = w / width;
		for (y = 0; y < height; y++)
			for (x = 0; x < width; x++)
				memmove(im->rgba + ((size_t)y * width + x) * 4, im->rgba + ((size_t)(y * k) * w + x * k) * 4, 4);
		w = width;
		h = height;
	} else if (mode == FIT_CROP) {
		uint32_t rows = h < height ? h : height;
		for (y = 0; y < rows; y++) memmove(im->rgba + (size_t)y * width * 4, im->rgba + (size_t)y * w * 4, (size_t)width * 4);
		w = width;
		h = rows;
	}
	if (w != width || h > height) return -1;
	im->width = w;
	im->height = h;
	*above = height - h;
	return 0;
}

void kp_scr_indices(const KpScrImage *im, KpIndexMap *map, uint8_t *out)
{
	size_t i, n = (size_t)im->width * im->height;
	for (i = 0; i < n; i++) {
		const uint8_t *p = im->rgba + 4 * i;
		out[i] = kp_index_map(map, p[0], p[1], p[2], p[3]);
	}
}

/* --- PCX --- */

uint8_t *kp_scr_pcx(uint32_t width, uint32_t height, const uint8_t *pixels, const uint8_t palette[768],
	const uint8_t header[6], size_t *size)
{
	/* the RLE is at most two bytes a pixel */
	size_t cap = PCX_HEADER + 2 * (size_t)width * height + 1 + 768 + 1, at = PCX_HEADER;
	uint8_t *out = (uint8_t *)calloc(cap, 1);
	uint32_t row;
	if (!out || width == 0 || height == 0 || width > 0x10000u || height > 0x10000u) {
		free(out);
		return NULL;
	}
	out[0] = 0x0a;
	out[1] = 5;
	out[2] = 1;
	out[3] = 8;
	kp_le_put16(out + 8, width - 1);
	kp_le_put16(out + 10, height - 1);
	memcpy(out + 12, header, 4);
	out[65] = 1;
	kp_le_put16(out + 66, width);
	memcpy(out + 68, header + 4, 2);
	for (row = 0; row < height; row++) {
		const uint8_t *line = pixels + (size_t)row * width;
		uint32_t i = 0;
		while (i < width) {
			uint32_t run = 1;
			while (i + run < width && run < PCX_RUN_MAX && line[i + run] == line[i]) run++;
			if (run > 1 || line[i] >= PCX_RUN) out[at++] = (uint8_t)(PCX_RUN | run);
			out[at++] = line[i];
			i += run;
		}
	}
	out[at++] = PCX_MARKER;
	memcpy(out + at, palette, 768);
	*size = at + 768;
	out[*size] = 0;
	return out;
}

int kp_scr_pcx_palette(const uint8_t *data, size_t size, uint8_t palette[768])
{
	if (size < PCX_HEADER || data[0] != 0x0a || size < 769 || data[size - 769] != PCX_MARKER) return -1;
	memcpy(palette, data + size - 768, 768);
	return 0;
}

/* --- GRP --- */

uint8_t *kp_scr_plain_grp(uint32_t box_w, uint32_t box_h, const KpScrGrpRecord *records, uint32_t count,
	uint8_t *const *frames, size_t size)
{
	uint8_t *out;
	uint32_t k, j;
	if ((size_t)KP_GRP_HEADER + (size_t)KP_GRP_RECORD * count > size || !(out = (uint8_t *)calloc(size + 1, 1))) return NULL;
	kp_le_put16(out, count);
	kp_le_put16(out + 2, box_w);
	kp_le_put16(out + 4, box_h);
	for (k = 0; k < count; k++) {
		const KpScrGrpRecord *r = &records[k];
		size_t n = (size_t)r->w * r->h;
		uint8_t *rec = out + KP_GRP_HEADER + (size_t)KP_GRP_RECORD * k;
		rec[0] = r->x;
		rec[1] = r->y;
		rec[2] = r->w;
		rec[3] = r->h;
		kp_le_put32(rec + 4, r->offset);
		for (j = 0; j < k && records[j].offset != r->offset; j++) {
		}
		if (j < k) continue; /* its rows are an earlier frame's */
		if (r->offset > size || n > size - r->offset) {
			free(out);
			return NULL;
		}
		memcpy(out + r->offset, frames[k], n);
	}
	return out;
}

/* grp.py _run: the opaque pixels from i equal to it, at most limit */
static uint32_t grp_run(const uint8_t *idx, const uint8_t *mask, uint32_t w, uint32_t i, uint32_t limit)
{
	uint32_t j = i;
	while (j < w && j - i < limit && mask[j] && idx[j] == idx[i]) j++;
	return j - i;
}

/* grp.py _encode_row into out (at most 2 bytes a pixel): its size */
static size_t grp_row(const uint8_t *idx, const uint8_t *mask, uint32_t w, uint8_t *out)
{
	size_t at = 0;
	uint32_t i = 0, j, n;
	while (i < w) {
		j = i;
		if (!mask[i]) {
			while (j < w && !mask[j] && j - i < GRP_RUN_MAX) j++;
			out[at++] = (uint8_t)(GRP_SKIP | (j - i));
		} else if ((n = grp_run(idx, mask, w, i, GRP_COUNT_MAX)) >= GRP_REPEAT_MIN) {
			j = i + n;
			out[at++] = (uint8_t)(GRP_REPEAT | n);
			out[at++] = idx[i];
		} else {
			while (j < w && mask[j] && j - i < GRP_COUNT_MAX && (j == i || grp_run(idx, mask, w, j, GRP_REPEAT_MIN) < GRP_REPEAT_MIN))
				j++;
			out[at++] = (uint8_t)(j - i);
			memcpy(out + at, idx + i, j - i);
			at += j - i;
		}
		i = j;
	}
	return at;
}

uint8_t *kp_scr_remap_grp(const uint8_t *grp, size_t size, const uint8_t *wpe, size_t wpe_size, KpIndexMap *map,
	size_t *out_size)
{
	uint32_t frames, width, height, f;
	size_t cap, at, k;
	uint8_t *out = NULL, *idx = NULL, *mask = NULL, *payload_at;
	if (wpe_size < 256 * WPE_ENTRY || kp_grp_header(grp, size, &frames, &width, &height) != 0) return NULL;
	/* each frame: its row table and at most 2 bytes a pixel and a byte a row more */
	cap = KP_GRP_HEADER + (size_t)KP_GRP_RECORD * frames;
	for (f = 0; f < frames; f++) {
		KpGrpFrame r;
		kp_grp_frame(grp, f, &r);
		cap += 2u * r.h + (size_t)r.h * (2u * r.w + 1u);
	}
	if (!(out = (uint8_t *)malloc(cap + 1)) || !(idx = (uint8_t *)malloc(256 * 256)) || !(mask = (uint8_t *)malloc(256 * 256)))
		goto fail;
	kp_le_put16(out, frames);
	kp_le_put16(out + 2, width);
	kp_le_put16(out + 4, height);
	at = KP_GRP_HEADER + (size_t)KP_GRP_RECORD * frames;
	for (f = 0; f < frames; f++) {
		KpGrpFrame r;
		uint8_t *rec = out + KP_GRP_HEADER + (size_t)KP_GRP_RECORD * f;
		uint32_t row;
		size_t start = at;
		kp_grp_frame(grp, f, &r);
		if (kp_grp_decode(grp, size, f, idx, mask, NULL) != 0) goto fail;
		for (k = 0; k < (size_t)r.w * r.h; k++) {
			const uint8_t *c = wpe + WPE_ENTRY * idx[k];
			idx[k] = mask[k] ? kp_index_map(map, c[0], c[1], c[2], KP_INDEX_OPAQUE) : 0;
		}
		rec[0] = r.x;
		rec[1] = r.y;
		rec[2] = r.w;
		rec[3] = r.h;
		kp_le_put32(rec + 4, (uint32_t)start);
		payload_at = out + start;
		at += 2u * r.h;
		for (row = 0; row < r.h; row++) {
			if (at - start > TBL_OFFSET_MAX) goto fail;
			kp_le_put16(payload_at + 2 * row, (uint32_t)(at - start));
			at += grp_row(idx + (size_t)row * r.w, mask + (size_t)row * r.w, r.w, out + at);
		}
	}
	free(idx);
	free(mask);
	out[at] = 0;
	*out_size = at;
	return out;
fail:
	free(out);
	free(idx);
	free(mask);
	return NULL;
}

/* --- strings --- */

static int is_space(uint8_t c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }

static const uint8_t *find(const uint8_t *from, const uint8_t *end, const char *what)
{
	size_t n = strlen(what);
	const uint8_t *p;
	for (p = from; p + n <= end; p++)
		if (memcmp(p, what, n) == 0) return p;
	return NULL;
}

/* `\s*` then `what` at p: the position past it, or NULL */
static const uint8_t *after_spaces(const uint8_t *p, const uint8_t *end, const char *what)
{
	size_t n = strlen(what);
	while (p < end && is_space(*p)) p++;
	return (size_t)(end - p) >= n && memcmp(p, what, n) == 0 ? p + n : NULL;
}

/* The 5 code points Python's cp1252 codec has no byte for (it leaves 0x81, 0x8d, 0x8f, 0x90 and 0x9d undefined) */
static int python_cp1252(int32_t cp)
{
	int b;
	if (cp == 0x81 || cp == 0x8d || cp == 0x8f || cp == 0x90 || cp == 0x9d) return '?';
	b = kp_text_byte(cp);
	return b < 0 ? '?' : b;
}

/* Python's cp1252 decoding with errors="replace": the 5 bytes it leaves undefined are U+FFFD */
static int32_t python_cp1252_char(uint8_t b)
{
	return b == 0x81 || b == 0x8d || b == 0x8f || b == 0x90 || b == 0x9d ? REPLACEMENT_CHARACTER : kp_text_from_byte(b);
}

/* rebuild.unescape_rc3 over value (n bytes) into out (no longer): its length, -1 for a code past a byte. language: a
 * code past ASCII is its windows-1252 character in UTF-8 (unescape_utf8's; "\\NNN" is longer than its 3 bytes). */
static long unescape_rc3(const uint8_t *v, size_t n, uint8_t *out, int language)
{
	size_t i = 0, o = 0;
	/* the codes first (\\N or \N, up to 3 digits), then line breaks and tabs: two passes as the converter's two
	 * substitutions, the second over the first's result */
	while (i < n) {
		size_t j = i + 1, d = 0;
		if (v[i] == '\\') {
			if (j < n && v[j] == '\\' && j + 1 < n && v[j + 1] >= '0' && v[j + 1] <= '9') j++;
			while (j + d < n && d < 3 && v[j + d] >= '0' && v[j + d] <= '9') d++;
		}
		if (d) {
			uint32_t code = 0, k;
			for (k = 0; k < d; k++) code = code * 10 + (uint32_t)(v[j + k] - '0');
			if (code > BYTE_MAX) return -1;
			if (language && code >= KP_TEXT_ASCII_END) o += kp_text_encode(python_cp1252_char((uint8_t)code), out + o);
			else out[o++] = (uint8_t)code;
			i = j + d;
		} else {
			out[o++] = v[i++];
		}
	}
	n = o;
	for (i = 0, o = 0; i < n;) {
		size_t j = i + 1;
		if (out[i] == '\\') {
			if (j < n && out[j] == '\\' && j + 1 < n && (out[j + 1] == 'n' || out[j + 1] == 't')) j++;
			if (j < n && (out[j] == 'n' || out[j] == 't')) {
				out[o++] = out[j] == 'n' ? '\n' : '\t';
				i = j + 1;
				continue;
			}
		}
		out[o++] = out[i++];
	}
	return (long)o;
}

/* rebuild.LANGUAGE_ESCAPES in place, left to right: four backslashes are one, a backslash's quote a quote; the length */
static size_t unescape_language(uint8_t *t, size_t n)
{
	size_t i = 0, o = 0;
	while (i < n) {
		if (n - i >= 4 && memcmp(t + i, "\\\\\\\\", 4) == 0) {
			t[o++] = '\\';
			i += 4;
		} else if (n - i >= 2 && t[i] == '\\' && t[i + 1] == '"') {
			t[o++] = '"';
			i += 2;
		} else {
			t[o++] = t[i++];
		}
	}
	return o;
}

/* A language's value (its entities undone, text t bytes, taken): RC3's escapes with its codes past ASCII in UTF-8, then
 * the language's own; one past a byte leaves the value as it is, bad. */
static int language_value(uint8_t *text, size_t t, KpScrString *s, char *err, size_t err_size)
{
	uint8_t *out = (uint8_t *)malloc(t + 1);
	long len;
	if (!out) {
		free(text);
		return fail(err, err_size, "out of memory");
	}
	if ((len = unescape_rc3(text, t, out, 1)) < 0) {
		free(out);
		text[t] = 0;
		s->value = text;
		s->size = t;
		s->bad = 1;
		return 0;
	}
	free(text);
	s->size = unescape_language(out, (size_t)len);
	out[s->size] = 0;
	s->value = out;
	return 0;
}

/* An XML value (its entities undone) from UTF-8 to windows-1252, then RC3's escapes (or a language's: language_value):
 * into s, -1 with err. */
static int string_value(const uint8_t *v, size_t n, int mode, KpScrString *s, char *err, size_t err_size)
{
	uint8_t *text = (uint8_t *)malloc(n + 1), *out;
	size_t i = 0, t = 0, at = 0;
	long len;
	if (!text) return fail(err, err_size, "out of memory");
	while (i < n) { /* &lt; &gt; &amp; &quot; &apos; */
		static const struct {
			const char *name;
			uint8_t c;
		} ENTITIES[] = { { "&lt;", '<' }, { "&gt;", '>' }, { "&amp;", '&' }, { "&quot;", '"' }, { "&apos;", '\'' } };
		size_t e;
		for (e = 0; e < sizeof ENTITIES / sizeof ENTITIES[0]; e++) {
			size_t k = strlen(ENTITIES[e].name);
			if (n - i >= k && memcmp(v + i, ENTITIES[e].name, k) == 0) break;
		}
		if (e < sizeof ENTITIES / sizeof ENTITIES[0]) {
			text[t++] = ENTITIES[e].c;
			i += strlen(ENTITIES[e].name);
		} else {
			text[t++] = v[i++];
		}
	}
	if (mode == KP_SCR_XML_LANGUAGE) return language_value(text, t, s, err, err_size);
	if (!kp_text_valid_utf8(text, t)) {
		free(text);
		return fail(err, err_size, "a string is not UTF-8");
	}
	if (!(out = (uint8_t *)malloc(t + 1))) {
		free(text);
		return fail(err, err_size, "out of memory");
	}
	for (i = 0; i < t;) out[at++] = (uint8_t)python_cp1252(kp_text_next(text, t, &i));
	free(text);
	if (mode == KP_SCR_XML_RC3) {
		if ((len = unescape_rc3(out, at, out, 0)) < 0) {
			free(out);
			return fail(err, err_size, "an escape is past a byte");
		}
		at = (size_t)len;
	}
	out[at] = 0;
	s->value = out;
	s->size = at;
	return 0;
}

int kp_scr_xml_strings(const uint8_t *xml, size_t size, int mode, KpScrStrings *out, char *err, size_t err_size)
{
	const uint8_t *end = xml + size, *p = xml;
	uint32_t cap = 0;
	memset(out, 0, sizeof *out);
	/* <string>\s*<id>(.*?)</id>\s*<value>(.*?)</value>\s*</string>, the lazy groups taking the first ends after which
	 * the rest follows */
	while ((p = find(p, end, "<string>")) != NULL) {
		const uint8_t *id = after_spaces(p + 8, end, "<id>"), *id_end = NULL, *value = NULL, *value_end = NULL;
		const uint8_t *next = NULL, *ie, *v, *ve;
		for (ie = id ? find(id, end, "</id>") : NULL; ie && !next; ie = find(ie + 1, end, "</id>")) {
			if (!(v = after_spaces(ie + 5, end, "<value>"))) continue;
			for (ve = find(v, end, "</value>"); ve && !next; ve = find(ve + 1, end, "</value>")) {
				if ((next = after_spaces(ve + 8, end, "</string>")) != NULL) {
					id_end = ie;
					value = v;
					value_end = ve;
				}
			}
			if (!next) break; /* no "</value>" then "</string>" after this "<value>": none after a later one either */
		}
		if (!next) {
			p++;
			continue;
		}
		if (out->count == cap) {
			uint32_t c = cap ? cap * 2 : 256;
			KpScrString *g = (KpScrString *)realloc(out->items, c * sizeof *g);
			if (!g) {
				kp_scr_strings_free(out);
				return fail(err, err_size, "out of memory");
			}
			out->items = g;
			cap = c;
		}
		{
			KpScrString *s = &out->items[out->count];
			memset(s, 0, sizeof *s);
			if (!(s->id = (char *)malloc((size_t)(id_end - id) + 1))) {
				kp_scr_strings_free(out);
				return fail(err, err_size, "out of memory");
			}
			memcpy(s->id, id, (size_t)(id_end - id));
			s->id[id_end - id] = 0;
			out->count++;
			if (string_value(value, (size_t)(value_end - value), mode, s, err, err_size) != 0) {
				kp_scr_strings_free(out);
				return -1;
			}
		}
		p = next;
	}
	return 0;
}

void kp_scr_strings_free(KpScrStrings *s)
{
	uint32_t k;
	for (k = 0; k < s->count; k++) {
		free(s->items[k].id);
		free(s->items[k].value);
	}
	free(s->items);
	memset(s, 0, sizeof *s);
}

const KpScrString *kp_scr_string(const KpScrStrings *s, const char *id, size_t id_len)
{
	uint32_t k = s->count;
	while (k--)
		if (strlen(s->items[k].id) == id_len && memcmp(s->items[k].id, id, id_len) == 0) return &s->items[k];
	return NULL;
}

/* --- TBL --- */

int kp_scr_tbl_spans(const uint8_t *data, size_t size, KpScrSpan **spans, uint32_t *count)
{
	uint32_t n, k;
	*spans = NULL;
	*count = 0;
	if (size < 2) return -1;
	n = kp_le_u16(data);
	if (2 + 2 * (size_t)n > size || (n && kp_le_u16(data + 2) != 2 + 2 * n)) return -1;
	if (!(*spans = (KpScrSpan *)calloc(n ? n : 1, sizeof **spans))) return -1;
	for (k = 0; k < n; k++) {
		size_t a = kp_le_u16(data + 2 + 2 * k), b = k + 1 < n ? kp_le_u16(data + 4 + 2 * k) : size;
		if (k + 1 < n && b < a) { /* not in order */
			free(*spans);
			*spans = NULL;
			return -1;
		}
		if (a > size) a = size; /* past the end: empty, as a slice */
		if (b > size) b = size;
		if (b < a) b = a;
		if (b > a && data[b - 1] == 0) b--;
		(*spans)[k].data = data + a;
		(*spans)[k].size = b - a;
	}
	*count = n;
	return 0;
}

uint8_t *kp_scr_tbl(const KpScrSpan *spans, uint32_t count, size_t *size)
{
	size_t total = 2 + 2 * (size_t)count, at, k;
	uint8_t *out;
	for (k = 0; k < count; k++) total += spans[k].size + 1;
	if (count > TBL_OFFSET_MAX || !(out = (uint8_t *)malloc(total + 1))) return NULL;
	kp_le_put16(out, count);
	at = 2 + 2 * (size_t)count;
	for (k = 0; k < count; k++) {
		if (at > TBL_OFFSET_MAX) {
			free(out);
			return NULL;
		}
		kp_le_put16(out + 2 + 2 * k, (uint32_t)at);
		if (spans[k].size) memcpy(out + at, spans[k].data, spans[k].size);
		at += spans[k].size;
		out[at++] = 0;
	}
	out[at] = 0;
	*size = at;
	return out;
}

/* --- dialogs --- */

uint8_t *kp_scr_dialog(const uint8_t *skeleton, size_t size, const KpScrDialogText *texts, uint32_t count,
	size_t *out_size)
{
	size_t total = size, at, k;
	uint32_t i;
	uint8_t *out;
	for (i = 0; i < count; i++)
		if (texts[i].text_size != texts[i].length) total += texts[i].text_size + 1;
	if (!(out = (uint8_t *)malloc(total + 1))) return NULL;
	memcpy(out, skeleton, size);
	at = size;
	for (i = 0; i < count; i++) {
		const KpScrDialogText *t = &texts[i];
		uint8_t *dst;
		if (t->text_size == t->length) {
			if (t->offset > size || t->length > size - t->offset) goto bad;
			dst = out + t->offset;
		} else {
			if (at > 0xffffffffu) goto bad;
			for (k = 0; k < t->pointer_count; k++) {
				uint32_t p = kp_le_u32(t->pointers + 4 * k);
				if (p > size || size - p < 4) goto bad;
				kp_le_put32(out + p, (uint32_t)at);
			}
			dst = out + at;
			dst[t->text_size] = 0;
			at += t->text_size + 1;
		}
		if (t->text_size) memcpy(dst, t->text, t->text_size);
		/* stored upper case: the hotkey record's loader lowercases it (dialog.parse) */
		if (t->hotkey && t->text_size && dst[0] >= 'a' && dst[0] <= 'z') dst[0] = (uint8_t)(dst[0] - 'a' + 'A');
	}
	out[at] = 0;
	*out_size = at;
	return out;
bad:
	free(out);
	return NULL;
}
