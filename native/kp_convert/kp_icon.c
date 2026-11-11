/* The resampling's sums are rounded as written (CMakeLists.txt compiles this file with -ffp-contract=off): the icons
 * come out the same on every desktop, and the same as the Python converter's did. */
#include "kp_icon.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kp_fs.h"
#include "kp_json.h"
#include "kp_kpf.h"
#include "zlib.h"

#define RT_ICON 3
#define RT_GROUP_ICON 14
#define RESOURCE_DIRECTORY 2 /* the data directory of the resource table */
#define SUBDIRECTORY 0x80000000u
#define ANY_ENTRY 0xffffffffu /* a directory's first entry: a resource's first language */
#define ICON_MAX 1024         /* the largest DIB taken */
#define PNG_LEVEL 6
#define PATH 1024
#define MADE_MAX 32

/* The opaque ground of the icons that cannot be transparent (iOS's, the Android adaptive icon's, the touch icon), a
 * dark steel like the shadow's colour in the art. */
static const uint8_t BACKGROUND[3] = { 16, 20, 28 };
/* The Android adaptive icon's foreground fills this part of its canvas (the launcher masks everything outside the
 * middle 66 of 108 dp). */
#define ADAPTIVE_FILL 0.66
/* iOS rounds the corners of its square: the art keeps this much of it so that the wing tips stay inside. */
#define IOS_FILL 0.88
#define ANDROID_LEGACY_DP 48
#define ANDROID_ADAPTIVE_DP 108
#define WINDOW_ICON_SIZE 256 /* the one the Linux build loads for its window (native/kp_app/sdl/kp_app_main.c) */

static const int ICO_SIZES[] = { 16, 32, 48, 64, 128, 256 };
static const int FAVICON_ICO_SIZES[] = { 16, 32, 48 };
/* macOS's icns, all PNG: ic11/ic12/ic13/ic14 are the 16, 32, 128 and 256 pixel images at 2x. */
static const struct {
	char type[5];
	int size;
} ICNS_TYPES[] = { { "icp4", 16 }, { "icp5", 32 }, { "icp6", 64 }, { "ic07", 128 }, { "ic08", 256 }, { "ic09", 512 },
	{ "ic11", 32 }, { "ic12", 64 }, { "ic13", 256 }, { "ic14", 512 } };
/* iOS's loose icons (CFBundleIconFiles). */
static const struct {
	const char *name;
	int size;
} IOS_ICONS[] = { { "AppIcon60x60@2x", 120 }, { "AppIcon60x60@3x", 180 }, { "AppIcon76x76@2x", 152 },
	{ "AppIcon83.5x83.5@2x", 167 } };
/* Android's launcher icons by density: the legacy square icon (48 dp), the adaptive foreground's canvas (108 dp). */
static const struct {
	const char *name;
	double scale;
} ANDROID_DENSITIES[] = { { "mdpi", 1.0 }, { "hdpi", 1.5 }, { "xhdpi", 2.0 }, { "xxhdpi", 3.0 }, { "xxxhdpi", 4.0 } };
static const struct {
	const char *name;
	int size, opaque;
} WEB_ICONS[] = { { "favicon-16x16", 16, 0 }, { "favicon-32x32", 32, 0 }, { "apple-touch-icon", 180, 1 } };

static const char ADAPTIVE_XML[] = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
				   "<adaptive-icon xmlns:android=\"http://schemas.android.com/apk/res/android\">\n"
				   "    <background android:drawable=\"@color/ic_launcher_background\"/>\n"
				   "    <foreground android:drawable=\"@mipmap/ic_launcher_foreground\"/>\n"
				   "</adaptive-icon>\n";

#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

/* --- the executable's resources (PE32; read only, never run) --- */

typedef struct {
	const uint8_t *data;
	size_t size, table;
	uint32_t sections, resource_rva;
} Pe;

static int in_bounds(const Pe *pe, size_t at, size_t n)
{
	return at <= pe->size && n <= pe->size - at;
}

static int pe_open(Pe *pe, const uint8_t *data, size_t size, char *err, size_t err_size)
{
	size_t at, opt;
	pe->data = data;
	pe->size = size;
	if (size < 0x40 || data[0] != 'M' || data[1] != 'Z') {
		snprintf(err, err_size, "missing MZ header");
		return -1;
	}
	at = kp_le_u32(data + 0x3c);
	if (!in_bounds(pe, at, 24) || memcmp(data + at, "PE\0\0", 4) != 0) {
		snprintf(err, err_size, "missing PE signature");
		return -1;
	}
	pe->sections = kp_le_u16(data + at + 6);
	opt = at + 24;
	if (!in_bounds(pe, opt, 96 + 8 * (RESOURCE_DIRECTORY + 1)) || kp_le_u16(data + opt) != 0x10b) {
		snprintf(err, err_size, "not a PE32 image");
		return -1;
	}
	pe->resource_rva = kp_le_u32(data + opt + 96 + 8 * RESOURCE_DIRECTORY);
	pe->table = opt + kp_le_u16(data + at + 20);
	if (!in_bounds(pe, pe->table, 40 * (size_t)pe->sections)) {
		snprintf(err, err_size, "truncated section table");
		return -1;
	}
	return 0;
}

/* The file offset of n bytes at rva, all in a section's raw data: or SIZE_MAX. */
static size_t pe_offset(const Pe *pe, uint32_t rva, size_t n)
{
	uint32_t i;
	for (i = 0; i < pe->sections; i++) {
		const uint8_t *s = pe->data + pe->table + 40 * (size_t)i;
		uint32_t vsize = kp_le_u32(s + 8), va = kp_le_u32(s + 12), rsize = kp_le_u32(s + 16), raw = kp_le_u32(s + 20);
		size_t at;
		if (rva < va || rva - va >= (vsize > rsize ? vsize : rsize)) continue;
		if (rva - va >= rsize || n > rsize - (rva - va)) return SIZE_MAX; /* the section's uninitialised part */
		at = (size_t)raw + (rva - va);
		return in_bounds(pe, at, n) ? at : SIZE_MAX;
	}
	return SIZE_MAX;
}

/* The target of a resource directory's entry by its id (ANY_ENTRY: the first): or -1. */
static int64_t pe_entry(const Pe *pe, size_t dir, uint32_t id)
{
	uint32_t i, entries;
	if (!in_bounds(pe, dir, 16)) return -1;
	entries = kp_le_u16(pe->data + dir + 12) + kp_le_u16(pe->data + dir + 14); /* named, then by id */
	for (i = 0; i < entries; i++) {
		size_t e = dir + 16 + 8 * (size_t)i;
		uint32_t key;
		if (!in_bounds(pe, e, 8)) return -1;
		key = kp_le_u32(pe->data + e);
		if (id == ANY_ENTRY || (!(key & SUBDIRECTORY) && key == id)) return kp_le_u32(pe->data + e + 4);
	}
	return -1;
}

/* Resource (type, id)'s bytes in its first language: or NULL with the reason. */
static const uint8_t *pe_resource(const Pe *pe, uint32_t type, uint32_t id, size_t *size, char *err, size_t err_size)
{
	size_t root = pe_offset(pe, pe->resource_rva, 16), at;
	int64_t t = -1, n = -1, leaf = -1;
	uint32_t rva;
	if (root != SIZE_MAX && (t = pe_entry(pe, root, type)) >= 0 && (t & SUBDIRECTORY))
		n = pe_entry(pe, root + (t & ~SUBDIRECTORY), id);
	if (n >= 0 && (n & SUBDIRECTORY)) leaf = pe_entry(pe, root + (n & ~SUBDIRECTORY), ANY_ENTRY);
	if (leaf < 0 || (leaf & SUBDIRECTORY) || !in_bounds(pe, root + (size_t)leaf, 8)) {
		snprintf(err, err_size, "resource %u/%u not found", (unsigned)type, (unsigned)id);
		return NULL;
	}
	rva = kp_le_u32(pe->data + root + leaf);
	*size = kp_le_u32(pe->data + root + leaf + 4);
	if ((at = pe_offset(pe, rva, *size)) == SIZE_MAX) {
		snprintf(err, err_size, "resource %u/%u lies outside the image's data", (unsigned)type, (unsigned)id);
		return NULL;
	}
	return pe->data + at;
}

/* An RGBA image from a 32-bit icon DIB (a BITMAPINFOHEADER whose height counts the colour and the mask rows; rows
 * stored bottom up, pixels as BGRA): malloc'd, its size in *n; or NULL with the reason. */
static uint8_t *dib_rgba(const uint8_t *dib, size_t size, int *n, char *err, size_t err_size)
{
	uint32_t header, bits, compression, planes;
	int32_t width, height2;
	size_t stride;
	uint8_t *rgba;
	int x, y;
	if (size < 40) {
		snprintf(err, err_size, "truncated icon DIB");
		return NULL;
	}
	header = kp_le_u32(dib);
	width = (int32_t)kp_le_u32(dib + 4);
	height2 = (int32_t)kp_le_u32(dib + 8);
	planes = kp_le_u16(dib + 12);
	bits = kp_le_u16(dib + 14);
	compression = kp_le_u32(dib + 16);
	if (header < 40 || bits != 32 || compression != 0 || planes != 1 || width <= 0 || width > ICON_MAX
		|| height2 != 2 * width) {
		snprintf(err, err_size, "not a 32-bit square icon DIB (%dx%d, %u bits, compression %u)", (int)width, (int)height2,
			(unsigned)bits, (unsigned)compression);
		return NULL;
	}
	stride = (size_t)width * 4;
	if (header > size || stride * (size_t)width > size - header) {
		snprintf(err, err_size, "truncated icon DIB");
		return NULL;
	}
	if (!(rgba = (uint8_t *)malloc(stride * (size_t)width))) {
		snprintf(err, err_size, "out of memory");
		return NULL;
	}
	for (y = 0; y < width; y++) {
		const uint8_t *row = dib + header + (size_t)(width - 1 - y) * stride;
		uint8_t *out = rgba + (size_t)y * stride;
		for (x = 0; x < width; x++) {
			out[4 * x] = row[4 * x + 2];
			out[4 * x + 1] = row[4 * x + 1];
			out[4 * x + 2] = row[4 * x];
			out[4 * x + 3] = row[4 * x + 3];
		}
	}
	*n = width;
	return rgba;
}

int kp_icon_read(const uint8_t *exe, size_t exe_size, int group, KpIconImage *images, int max, char *err, size_t err_size)
{
	const uint8_t *g, *dib;
	size_t g_size, dib_size;
	int i, k, entries, count = 0;
	Pe pe;
	if (pe_open(&pe, exe, exe_size, err, err_size) != 0) return -1;
	if (!(g = pe_resource(&pe, RT_GROUP_ICON, (uint32_t)group, &g_size, err, err_size))) return -1;
	if (g_size < 6 || kp_le_u16(g) != 0 || kp_le_u16(g + 2) != 1 || g_size < 6 + 14 * (size_t)kp_le_u16(g + 4)) {
		snprintf(err, err_size, "resource %d is not an icon group", group);
		return -1;
	}
	entries = (int)kp_le_u16(g + 4);
	for (i = 0; i < entries; i++) {
		const uint8_t *e = g + 6 + 14 * i;
		int width = e[0] ? e[0] : 256, n = 0;
		uint8_t *rgba;
		if (kp_le_u16(e + 6) != 32) continue;
		if (!(dib = pe_resource(&pe, RT_ICON, kp_le_u16(e + 12), &dib_size, err, err_size))
			|| !(rgba = dib_rgba(dib, dib_size, &n, err, err_size)))
			goto failed;
		if (n != width) {
			snprintf(err, err_size, "icon group %d: its %d-pixel entry holds a %d-pixel image", group, width, n);
			free(rgba);
			goto failed;
		}
		for (k = 0; k < count && images[k].size != n; k++) {}
		if (k == count && count == max) {
			snprintf(err, err_size, "icon group %d holds more than %d images", group, max);
			free(rgba);
			goto failed;
		}
		if (k < count) free(images[k].rgba);
		else count++;
		images[k].size = n;
		images[k].rgba = rgba;
	}
	if (count == 0) {
		snprintf(err, err_size, "icon group %d holds no 32-bit image", group);
		return -1;
	}
	return count;
failed:
	for (k = 0; k < count; k++) free(images[k].rgba);
	return -1;
}

/* --- images --- */

uint8_t *kp_icon_resize(const uint8_t *rgba, int n, int size)
{
	size_t pixels = (size_t)n * (size_t)n;
	double *pre = (double *)malloc(pixels * 4 * sizeof *pre), *f = (double *)malloc((size_t)size * sizeof *f);
	int *i0 = (int *)malloc((size_t)size * sizeof *i0), *i1 = (int *)malloc((size_t)size * sizeof *i1);
	uint8_t *out = (uint8_t *)calloc((size_t)size * (size_t)size, 4);
	size_t i;
	int o, x, y, c;
	if (!pre || !f || !i0 || !i1 || !out) {
		free(out);
		out = NULL;
		goto done;
	}
	for (i = 0; i < pixels; i++) { /* premultiplied, 0..255 */
		int a = rgba[4 * i + 3];
		for (c = 0; c < 3; c++) pre[4 * i + c] = (double)(rgba[4 * i + c] * a) / 255;
		pre[4 * i + 3] = a;
	}
	for (o = 0; o < size; o++) { /* each output coordinate's two source indices and the weight of the second */
		double s = (o + 0.5) * n / size - 0.5;
		s = s < 0.0 ? 0.0 : s;
		s = s > n - 1.0 ? n - 1.0 : s;
		i0[o] = (int)s;
		i1[o] = i0[o] + 1 < n ? i0[o] + 1 : n - 1;
		f[o] = s - i0[o];
	}
	for (y = 0; y < size; y++) {
		for (x = 0; x < size; x++) {
			double fx = f[x], fy = f[y], w00 = (1 - fx) * (1 - fy), w01 = fx * (1 - fy), w10 = (1 - fx) * fy, w11 = fx * fy;
			size_t p00 = 4 * ((size_t)i0[y] * n + i0[x]), p01 = 4 * ((size_t)i0[y] * n + i1[x]);
			size_t p10 = 4 * ((size_t)i1[y] * n + i0[x]), p11 = 4 * ((size_t)i1[y] * n + i1[x]);
			uint8_t *px = out + 4 * ((size_t)y * size + x);
			double a = pre[p00 + 3] * w00 + pre[p01 + 3] * w01 + pre[p10 + 3] * w10 + pre[p11 + 3] * w11;
			if (a <= 0) continue;
			for (c = 0; c < 3; c++) {
				double v = pre[p00 + c] * w00 + pre[p01 + c] * w01 + pre[p10 + c] * w10 + pre[p11 + c] * w11;
				int q = (int)(v * 255 / a + 0.5);
				px[c] = (uint8_t)(q < 255 ? q : 255);
			}
			px[3] = (uint8_t)((int)(a + 0.5) < 255 ? (int)(a + 0.5) : 255);
		}
	}
done:
	free(pre);
	free(f);
	free(i0);
	free(i1);
	return out;
}

/* rgba (size n) over the opaque ground: malloc'd RGB. */
static uint8_t *flatten(const uint8_t *rgba, int n)
{
	size_t i, pixels = (size_t)n * (size_t)n;
	uint8_t *rgb = (uint8_t *)malloc(pixels * 3);
	int c;
	if (!rgb) return NULL;
	for (i = 0; i < pixels; i++) {
		int a = rgba[4 * i + 3];
		for (c = 0; c < 3; c++) rgb[3 * i + c] = (uint8_t)((rgba[4 * i + c] * a + BACKGROUND[c] * (255 - a) + 127) / 255);
	}
	return rgb;
}

/* rgba (size n) in the middle of a transparent canvas: malloc'd RGBA. */
static uint8_t *centred(const uint8_t *rgba, int n, int canvas)
{
	uint8_t *out = (uint8_t *)calloc((size_t)canvas * (size_t)canvas, 4);
	int y, at = (canvas - n) / 2;
	if (!out) return NULL;
	for (y = 0; y < n; y++) memcpy(out + 4 * ((size_t)(at + y) * canvas + at), rgba + 4 * (size_t)y * n, 4 * (size_t)n);
	return out;
}

/* --- files --- */

static void put_be32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)(v >> 24);
	p[1] = (uint8_t)(v >> 16);
	p[2] = (uint8_t)(v >> 8);
	p[3] = (uint8_t)v;
}

static void png_chunk(KpBuf *b, const char *kind, const uint8_t *payload, size_t n)
{
	static const uint8_t none[1] = { 0 }; /* crc32 takes a NULL buffer for a query of its initial value */
	uint8_t head[8], tail[4];
	uLong crc;
	payload = payload ? payload : none;
	put_be32(head, (uint32_t)n);
	memcpy(head + 4, kind, 4);
	crc = crc32(crc32(0, (const Bytef *)kind, 4), payload, (uInt)n);
	put_be32(tail, (uint32_t)crc);
	kp_buf_add(b, head, sizeof head);
	kp_buf_add(b, payload, n);
	kp_buf_add(b, tail, sizeof tail);
}

uint8_t *kp_icon_png(const uint8_t *pixels, int width, int height, int channels, size_t *out_size)
{
	static const uint8_t SIGNATURE[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n' };
	size_t stride = (size_t)width * (size_t)channels, raw_size = (size_t)height * (stride + 1);
	uint8_t ihdr[13], *raw, *packed;
	uLongf packed_size;
	KpBuf b;
	int y;
	if (width <= 0 || height <= 0 || (channels != 3 && channels != 4)) return NULL;
	packed_size = compressBound((uLong)raw_size);
	raw = (uint8_t *)malloc(raw_size);
	packed = (uint8_t *)malloc(packed_size);
	if (!raw || !packed) {
		free(raw);
		free(packed);
		return NULL;
	}
	for (y = 0; y < height; y++) {
		raw[(size_t)y * (stride + 1)] = 0;
		memcpy(raw + (size_t)y * (stride + 1) + 1, pixels + (size_t)y * stride, stride);
	}
	if (compress2(packed, &packed_size, raw, (uLong)raw_size, PNG_LEVEL) != Z_OK) {
		free(raw);
		free(packed);
		return NULL;
	}
	free(raw);
	put_be32(ihdr, (uint32_t)width);
	put_be32(ihdr + 4, (uint32_t)height);
	ihdr[8] = 8;
	ihdr[9] = channels == 4 ? 6 : 2; /* RGBA, RGB */
	ihdr[10] = ihdr[11] = ihdr[12] = 0;
	kp_buf_init(&b);
	kp_buf_add(&b, SIGNATURE, sizeof SIGNATURE);
	png_chunk(&b, "IHDR", ihdr, sizeof ihdr);
	png_chunk(&b, "IDAT", packed, packed_size);
	png_chunk(&b, "IEND", NULL, 0);
	free(packed);
	*out_size = b.size;
	return (uint8_t *)b.data;
}

/* The sizes made of the executable's images, each once, and the files written. */
typedef struct {
	const KpIconImage *images, *largest;
	int count;
	KpIconImage made[MADE_MAX];
	int made_count;
	const char *out;
	int files, written;
	char *err;
	size_t err_size;
} Set;

/* The image at size pixels (an original, or made of the largest once): or NULL out of memory. */
static const uint8_t *at(Set *s, int size)
{
	int k;
	uint8_t *rgba;
	for (k = 0; k < s->count; k++)
		if (s->images[k].size == size) return s->images[k].rgba;
	for (k = 0; k < s->made_count; k++)
		if (s->made[k].size == size) return s->made[k].rgba;
	if (s->made_count == MADE_MAX || !(rgba = kp_icon_resize(s->largest->rgba, s->largest->size, size))) return NULL;
	s->made[s->made_count].size = size;
	s->made[s->made_count++].rgba = rgba;
	return rgba;
}

/* A file written under out (its folder made): 0, or -1 with the reason. */
static int put(Set *s, const char *name, const void *data, size_t size)
{
	char path[PATH];
	int changed;
	char *slash;
	if (!data) {
		snprintf(s->err, s->err_size, "%s: out of memory", name);
		return -1;
	}
	snprintf(path, sizeof path, "%s/%s", s->out, name);
	if ((slash = strrchr(path, '/')) != NULL) {
		*slash = 0;
		if (kp_fs_mkdirs(path, s->err, s->err_size) != 0) return -1;
		*slash = '/';
	}
	if ((changed = kp_fs_write_if_changed(path, data, size, s->err, s->err_size)) < 0) return -1;
	s->files++;
	s->written += changed;
	return 0;
}

/* A PNG of an image (channels 4) or of its flattened copy (3) written: 0, or -1. */
static int put_png(Set *s, const char *name, const uint8_t *rgba, int size, int opaque)
{
	uint8_t *rgb = rgba && opaque ? flatten(rgba, size) : NULL, *png = NULL;
	size_t png_size = 0;
	int rc;
	if (rgba && (!opaque || rgb)) png = kp_icon_png(opaque ? rgb : rgba, size, size, opaque ? 3 : 4, &png_size);
	rc = put(s, name, png, png_size);
	free(rgb);
	free(png);
	return rc;
}

/* An ICO of PNG images (Windows Vista on, and every browser) written: 0, or -1. */
static int put_ico(Set *s, const char *name, const int *sizes, int count)
{
	uint8_t *pngs[COUNT(ICO_SIZES)], head[16];
	size_t png_sizes[COUNT(ICO_SIZES)], offset = 6 + 16 * (size_t)count;
	int k, ok = 1, rc;
	KpBuf b;
	for (k = 0; k < count; k++) {
		const uint8_t *rgba = at(s, sizes[k]);
		pngs[k] = rgba ? kp_icon_png(rgba, sizes[k], sizes[k], 4, &png_sizes[k]) : NULL;
		ok = ok && pngs[k];
	}
	kp_buf_init(&b);
	kp_le_put16(head, 0);
	kp_le_put16(head + 2, 1);
	kp_le_put16(head + 4, (uint32_t)count);
	kp_buf_add(&b, head, 6);
	for (k = 0; ok && k < count; k++) {
		head[0] = head[1] = (uint8_t)(sizes[k] % 256);
		head[2] = head[3] = 0;
		kp_le_put16(head + 4, 1);
		kp_le_put16(head + 6, 32);
		kp_le_put32(head + 8, (uint32_t)png_sizes[k]);
		kp_le_put32(head + 12, (uint32_t)offset);
		kp_buf_add(&b, head, 16);
		offset += png_sizes[k];
	}
	for (k = 0; ok && k < count; k++) kp_buf_add(&b, pngs[k], png_sizes[k]);
	rc = put(s, name, ok ? b.data : NULL, b.size);
	for (k = 0; k < count; k++) free(pngs[k]);
	kp_buf_free(&b);
	return rc;
}

/* An Apple icon image of PNG entries written: 0, or -1. */
static int put_icns(Set *s, const char *name)
{
	uint8_t head[8];
	int k, ok = 1, rc;
	KpBuf b;
	kp_buf_init(&b);
	kp_buf_add(&b, "icns\0\0\0\0", 8);
	for (k = 0; ok && k < COUNT(ICNS_TYPES); k++) {
		const uint8_t *rgba = at(s, ICNS_TYPES[k].size);
		size_t png_size = 0;
		uint8_t *png = rgba ? kp_icon_png(rgba, ICNS_TYPES[k].size, ICNS_TYPES[k].size, 4, &png_size) : NULL;
		if (!(ok = png != NULL)) break;
		memcpy(head, ICNS_TYPES[k].type, 4);
		put_be32(head + 4, (uint32_t)(8 + png_size));
		kp_buf_add(&b, head, 8);
		kp_buf_add(&b, png, png_size);
		free(png);
	}
	put_be32((uint8_t *)b.data + 4, (uint32_t)b.size);
	rc = put(s, name, ok ? b.data : NULL, b.size);
	kp_buf_free(&b);
	return rc;
}

/* The image of art pixels in the middle of a canvas, flattened or not, written: 0, or -1. */
static int put_centred(Set *s, const char *name, int art, int canvas, int opaque)
{
	const uint8_t *rgba = at(s, art);
	uint8_t *in_canvas = rgba ? centred(rgba, art, canvas) : NULL;
	int rc = put_png(s, name, in_canvas, canvas, opaque);
	free(in_canvas);
	return rc;
}

int kp_icon_write_set(const KpIconImage *images, int count, const char *out, int *files, char *err, size_t err_size)
{
	char name[PATH], colour[256];
	int k, rc = 0;
	Set s;
	memset(&s, 0, sizeof s);
	s.images = s.largest = images;
	s.count = count;
	s.out = out;
	s.err = err;
	s.err_size = err_size;
	for (k = 1; k < count; k++)
		if (images[k].size > s.largest->size) s.largest = &images[k];
	for (k = 0; rc == 0 && k < count; k++) {
		snprintf(name, sizeof name, "icon-%d.png", images[k].size);
		if (images[k].size != WINDOW_ICON_SIZE) rc = put_png(&s, name, images[k].rgba, images[k].size, 0);
	}
	if (rc == 0) rc = put_png(&s, "icon-256.png", at(&s, WINDOW_ICON_SIZE), WINDOW_ICON_SIZE, 0);
	if (rc == 0) rc = put_ico(&s, "icon.ico", ICO_SIZES, COUNT(ICO_SIZES));
	if (rc == 0) rc = put_icns(&s, "icon.icns");
	for (k = 0; rc == 0 && k < COUNT(IOS_ICONS); k++) { /* iOS takes no transparency */
		snprintf(name, sizeof name, "ios/%s.png", IOS_ICONS[k].name);
		rc = put_centred(&s, name, (int)rint(IOS_ICONS[k].size * IOS_FILL), IOS_ICONS[k].size, 1);
	}
	if (rc == 0) rc = put_ico(&s, "web/favicon.ico", FAVICON_ICO_SIZES, COUNT(FAVICON_ICO_SIZES));
	for (k = 0; rc == 0 && k < COUNT(WEB_ICONS); k++) {
		snprintf(name, sizeof name, "web/%s.png", WEB_ICONS[k].name);
		rc = put_png(&s, name, at(&s, WEB_ICONS[k].size), WEB_ICONS[k].size, WEB_ICONS[k].opaque);
	}
	for (k = 0; rc == 0 && k < COUNT(ANDROID_DENSITIES); k++) {
		int legacy = (int)rint(ANDROID_LEGACY_DP * ANDROID_DENSITIES[k].scale);
		int canvas = (int)rint(ANDROID_ADAPTIVE_DP * ANDROID_DENSITIES[k].scale);
		snprintf(name, sizeof name, "android/res/mipmap-%s/ic_launcher.png", ANDROID_DENSITIES[k].name);
		rc = put_png(&s, name, at(&s, legacy), legacy, 0);
		snprintf(name, sizeof name, "android/res/mipmap-%s/ic_launcher_foreground.png", ANDROID_DENSITIES[k].name);
		if (rc == 0) rc = put_centred(&s, name, (int)rint(canvas * ADAPTIVE_FILL), canvas, 0);
	}
	if (rc == 0) rc = put(&s, "android/res/mipmap-anydpi-v26/ic_launcher.xml", ADAPTIVE_XML, sizeof ADAPTIVE_XML - 1);
	snprintf(colour, sizeof colour,
		"<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<resources>\n    <color name=\"ic_launcher_background\">"
		"#%02x%02x%02x</color>\n</resources>\n",
		BACKGROUND[0], BACKGROUND[1], BACKGROUND[2]);
	if (rc == 0) rc = put(&s, "android/res/values/ic_launcher_background.xml", colour, strlen(colour));
	for (k = 0; k < s.made_count; k++) free(s.made[k].rgba);
	*files = s.files;
	return rc == 0 ? s.written : -1;
}
