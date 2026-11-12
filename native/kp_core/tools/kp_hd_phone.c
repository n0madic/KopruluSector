/*
 * kp_hd_phone: the measures of the HD art's blocks for phones (ADR 0013, stage R4f), through kp_convert's codecs
 * (kp_hd_transcode.h: kp_convert repack writes the phones' sets).
 *
 *   kp_hd_phone --report IN PAGE   each of a pack page's layers in ASTC's candidate blocks and in ETC2: bytes, encoding
 *                             time, PSNR against its BC blocks decoded (and in the frames' rims), the diffuse's clear
 *                             texels read back with alpha: what the blocks of KP_HD_FORMAT_ASTC were chosen by
 *   kp_hd_phone --report-tiles IN [DUMP.ppm]   an HD tileset's tiles or the water's normal maps in ETC2 and ASTC's blocks,
 *                             each image alone as the atlas holds it: bytes, PSNR against its BC1 decoded, overall, in
 *                             the worst image and on the images' edges, and how much of its detail stays; DUMP: BC1,
 *                             ETC2 and ASTC 8 x 8 side by side, at their size
 *   kp_hd_phone --selftest   small packs through etc2 and astc (kp_hd_transcode), opened and read back as the
 *                             product does
 *
 * The ASTC modes need -DKP_ASTC=ON; the product itself has no ASTC codec: it uploads the blocks.
 */
#include "kp_hd_transcode.h"
#include "kp_present.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef KP_ASTC
#include "astcenc.h"
#endif

/* Says what failed; 1, the tool's exit status. */
static int fail(const char *what, const char *msg)
{
	fprintf(stderr, "kp_hd_phone: %s: %s\n", what, msg);
	return 1;
}

#ifdef KP_ASTC
/* The encoder's effort kp_hd_transcode encodes with (its quality levels' "thorough") */
#define ASTC_QUALITY ASTCENC_PRE_THOROUGH
#endif

/* --- the blocks' choice --- */

static double psnr(double squared, double weight)
{
	return squared <= 0 || weight <= 0 ? 99.0 : 10.0 * log10(255.0 * 255.0 * weight / squared);
}

/* A layer's PSNR, by what the shader reads: the channels of its source (a) against the ones read back (b) (in b, a
 * team mask's and occlusion's grey in red, ETC2's normal map's y in alpha by report_etc2): the colour, weighted by the
 * diffuse's alpha, and alpha, or the normal map's x and y; out[2]: the first of them on the rim's texels alone (rim
 * NULL: on none). */
static void layer_psnr(int layer, const uint8_t *a, const uint8_t *b, const uint8_t *rim, size_t pixels, double out[3])
{
	double e0 = 0, w0 = 0, e1 = 0, er = 0, wr = 0;
	size_t i;
	int c;
	for (i = 0; i < pixels; i++) {
		const uint8_t *p = a + 4 * i, *q = b + 4 * i;
		double d, e = 0, w = 1;
		switch (layer) {
		case KP_HD_LAYER_DIFFUSE:
			w = p[3] / 255.0;
			for (c = 0; c < 3; c++) {
				d = (double)p[c] - q[c];
				e += w * d * d;
			}
			w *= 3;
			d = (double)p[3] - q[3];
			e1 += d * d;
			break;
		case KP_HD_LAYER_TEAM:
			d = (double)p[0] - q[0];
			e = d * d;
			break;
		case KP_HD_LAYER_LIGHT + KP_HD_LIGHT_NORMAL:
			d = (double)p[0] - q[0];
			e = d * d;
			d = (double)p[3] - q[3];
			e1 += d * d;
			break;
		case KP_HD_LAYER_LIGHT + KP_HD_LIGHT_AO:
			d = (double)p[1] - q[0];
			e = d * d;
			break;
		default:
			for (c = 0; c < 3; c++) {
				d = (double)p[c] - q[c];
				e += d * d;
			}
			w = 3;
			break;
		}
		e0 += e;
		w0 += w;
		if (rim && rim[i]) {
			er += e;
			wr += w;
		}
	}
	out[0] = psnr(e0, w0);
	out[1] = psnr(e1, (double)pixels);
	out[2] = psnr(er, wr);
}

/* A layer's ETC2 blocks read back as RGBA8, the channels where layer_psnr looks for them. */
static void etc2_read_back(int layer, const uint8_t *blocks, int32_t size, uint8_t *rgba)
{
	size_t pixels = (size_t)size * (size_t)size, i;
	uint8_t *r, *g;
	if (layer == KP_HD_LAYER_DIFFUSE) {
		kp_etc2_decode(blocks, size, size, rgba);
		return;
	}
	if (layer >= KP_HD_LAYER_LIGHT + KP_HD_LIGHT_SPECULAR) {
		kp_etc2_rgb_decode(blocks, size, size, rgba);
		return;
	}
	r = (uint8_t *)malloc(pixels); /* the single channels in red, as astc's grey */
	g = (uint8_t *)malloc(pixels);
	if (!r || !g) {
		free(r);
		free(g);
		memset(rgba, 0, pixels * 4);
		return;
	}
	if (layer == KP_HD_LAYER_LIGHT + KP_HD_LIGHT_NORMAL) {
		kp_eac_rg11_decode(blocks, size, size, r, g);
	} else {
		kp_eac_r11_decode(blocks, size, size, r);
		memcpy(g, r, pixels);
	}
	for (i = 0; i < pixels; i++) {
		rgba[4 * i] = r[i];
		rgba[4 * i + 1] = g[i];
		rgba[4 * i + 2] = 0;
		rgba[4 * i + 3] = g[i];
	}
	free(r);
	free(g);
}

#ifdef KP_ASTC
static uint8_t *read_file(const char *path, size_t *size)
{
	FILE *f = fopen(path, "rb");
	uint8_t *data = NULL;
	long n;
	if (!f) return NULL;
	if (fseek(f, 0, SEEK_END) == 0 && (n = ftell(f)) >= 0 && fseek(f, 0, SEEK_SET) == 0
		&& (data = (uint8_t *)malloc((size_t)n + 1)) != NULL && fread(data, 1, (size_t)n, f) != (size_t)n) {
		free(data);
		data = NULL;
	}
	if (data) *size = (size_t)n;
	fclose(f);
	return data;
}

static double seconds(void) { return (double)clock() / CLOCKS_PER_SEC; }

/* The texels of a page within RIM of its frames' edges, where a block across two frames would show. */
static void frame_rims(const KpHdPack *p, int32_t page, uint8_t *rim)
{
	enum { RIM = 3 };
	size_t size = (size_t)p->page_size;
	uint32_t f;
	memset(rim, 0, size * size);
	for (f = 0; f < p->frame_count; f++) {
		const uint8_t *r = p->frames + (size_t)f * 14;
		int32_t x = (int32_t)kp_le_u16(r + 2), y = (int32_t)kp_le_u16(r + 4), w = (int32_t)kp_le_u16(r + 6);
		int32_t h = (int32_t)kp_le_u16(r + 8), i, j;
		if (w == 0 || (int32_t)kp_le_u16(r) != page) continue;
		for (j = 0; j < h; j++) {
			for (i = 0; i < w; i++) {
				if (i < RIM || j < RIM || i >= w - RIM || j >= h - RIM) rim[(size_t)(y + j) * size + (size_t)(x + i)] = 1;
			}
		}
	}
}

/* The diffuse's clear texels read back with alpha of 16 or more: on a clear edge of a frame a line where BC has none. */
static size_t clear_texels_with_alpha(const uint8_t *a, const uint8_t *b, size_t pixels)
{
	size_t i, n = 0;
	for (i = 0; i < pixels; i++) n += a[4 * i + 3] == 0 && b[4 * i + 3] >= 16;
	return n;
}

static int report(const char *in, int32_t page)
{
	static const char *const NAMES[KP_HD_LAYERS] = { "diffuse", "team", "normal", "occlusion", "specular", "emissive" };
	static const int SIDES[] = { 4, 5, 6, 8 };
	const float qualities[] = { ASTCENC_PRE_MEDIUM, ASTC_QUALITY };
	size_t size = 0;
	uint8_t *file = read_file(in, &size);
	KpHdPack p;
	KpError e;
	int layer, rc = 1;
	if (!file) return fail(in, "cannot read");
	if (kp_hd_pack_open(&p, file, size, size, in, &e) != 0 || p.format != KP_HD_FORMAT_BC) {
		fail(in, "not a pack of BC pages");
	} else if (page < 0 || page >= p.page_count) {
		fail(in, "no such page");
	} else {
		size_t pixels = (size_t)p.page_size * (size_t)p.page_size;
		uint8_t *rgba = (uint8_t *)malloc(pixels * 4), *back = (uint8_t *)malloc(pixels * 4);
		uint8_t *blocks = (uint8_t *)malloc(pixels * 2), *rim = (uint8_t *)malloc(pixels);
		rc = rgba && back && blocks && rim ? 0 : 1;
		if (rc == 0) frame_rims(&p, page, rim);
		for (layer = 0; layer < KP_HD_LAYERS && rc == 0; layer++) {
			uint32_t offset = kp_hd_transcode_offset(&p, page, layer);
			double q[3], t;
			size_t k, j;
			if (offset == 0) continue; /* an unlit page's lighting */
			kp_bcn_decode(file + offset, p.page_size, p.page_size, kp_hd_transcode_bc3(layer), rgba);
			t = seconds();
			rc = kp_hd_transcode_etc2_layer(layer, file + offset, p.page_size, blocks);
			if (rc != 0) break;
			t = seconds() - t;
			etc2_read_back(layer, blocks, p.page_size, back);
			layer_psnr(layer, rgba, back, rim, pixels, q);
			printf("%s page %d %-9s etc2     %8zu bytes %7.1f s  %6.2f dB %6.2f dB rim %6.2f dB clear %zu\n", in, (int)page, NAMES[layer],
				kp_hd_layer_bytes(KP_HD_FORMAT_ETC2, layer, p.page_size), t, q[0], q[1], q[2],
				layer == KP_HD_LAYER_DIFFUSE ? clear_texels_with_alpha(rgba, back, pixels) : 0);
			for (k = 0; k < sizeof SIDES / sizeof SIDES[0] && rc == 0; k++) {
				size_t n = kp_hd_astc_bytes(p.page_size, SIDES[k]);
				for (j = 0; j < sizeof qualities / sizeof qualities[0] && rc == 0; j++) {
					KpHdAstc *c = kp_hd_astc_open(layer, SIDES[k], qualities[j]);
					rc = c ? 0 : -1;
					if (rc == 0) {
						t = seconds();
						rc = kp_hd_astc_encode(c, rgba, p.page_size, blocks, n);
						t = seconds() - t;
					}
					if (rc == 0) rc = kp_hd_astc_decode(c, blocks, n, p.page_size, back);
					kp_hd_astc_close(c);
					if (rc != 0) break;
					layer_psnr(layer, rgba, back, rim, pixels, q);
					printf("%s page %d %-9s astc %dx%d %s %8zu bytes %7.1f s  %6.2f dB %6.2f dB rim %6.2f dB clear %zu\n", in, (int)page, NAMES[layer],
						SIDES[k], SIDES[k], j ? "thorough" : "medium", n, t, q[0], q[1], q[2],
						layer == KP_HD_LAYER_DIFFUSE ? clear_texels_with_alpha(rgba, back, pixels) : 0);
				}
			}
			fflush(stdout);
		}
		if (rc != 0) fail(in, "the encoder failed");
		free(rgba);
		free(back);
		free(blocks);
		free(rim);
	}
	free(file);
	return rc;
}

/* What a coder did to a set of images. */
typedef struct {
	double error, edge_error, source_detail, back_detail, worst;
	size_t weight, edge_weight;
} TileScore;

/* One image's RGBA8 back against its source: the squared error, on the texels within TILE_EDGE of its edges too (where
 * a seam between two images would show), and the squared differences between neighbours, which blur takes away. */
static void tile_score(TileScore *t, const uint8_t *a, const uint8_t *b, int32_t size)
{
	enum { TILE_EDGE = 2 };
	double image = 0;
	int32_t x, y;
	int c;
	for (y = 0; y < size; y++) {
		for (x = 0; x < size; x++) {
			const uint8_t *p = a + 4 * ((size_t)y * (size_t)size + (size_t)x), *q = b + 4 * ((size_t)y * (size_t)size + (size_t)x);
			int edge = x < TILE_EDGE || y < TILE_EDGE || x >= size - TILE_EDGE || y >= size - TILE_EDGE;
			for (c = 0; c < 3; c++) {
				double d = (double)p[c] - q[c];
				image += d * d;
				if (edge) t->edge_error += d * d;
				if (x + 1 < size) {
					double da = (double)p[c] - p[4 + c], db = (double)q[c] - q[4 + c];
					t->source_detail += da * da;
					t->back_detail += db * db;
				}
				if (y + 1 < size) {
					double da = (double)p[c] - p[4 * size + c], db = (double)q[c] - q[4 * size + c];
					t->source_detail += da * da;
					t->back_detail += db * db;
				}
			}
			if (edge) t->edge_weight += 3;
		}
	}
	t->error += image;
	t->weight += (size_t)size * (size_t)size * 3;
	image = psnr(image, (double)size * size * 3);
	if (image < t->worst) t->worst = image;
}

/* The tiles of a dump: runs of neighbours in a tile group, as a map lays them (a group is 16 megatiles). */
enum { DUMP_ROWS = 8, DUMP_COLUMNS = 8, DUMP_PANELS = 3 };

static void dump_tile(uint8_t *dump, int32_t size, int panel, int row, int column, const uint8_t *rgba)
{
	size_t width = (size_t)size * DUMP_COLUMNS * DUMP_PANELS;
	int32_t y;
	for (y = 0; y < size; y++) {
		uint8_t *to = dump + 3 * (((size_t)(row * size + y)) * width + (size_t)(panel * size * DUMP_COLUMNS + column * size));
		const uint8_t *from = rgba + 4 * (size_t)y * (size_t)size;
		int32_t x;
		for (x = 0; x < size; x++) {
			to[3 * x] = from[4 * x];
			to[3 * x + 1] = from[4 * x + 1];
			to[3 * x + 2] = from[4 * x + 2];
		}
	}
}

static int write_dump(const char *path, const uint8_t *dump, int32_t size)
{
	FILE *f = fopen(path, "wb");
	size_t width = (size_t)size * DUMP_COLUMNS * DUMP_PANELS, height = (size_t)size * DUMP_ROWS;
	int rc;
	if (!f) return fail(path, "cannot write");
	fprintf(f, "P6\n%zu %zu\n255\n", width, height);
	rc = fwrite(dump, 3, width * height, f) == width * height ? 0 : fail(path, "cannot write");
	fclose(f);
	return rc;
}

/* Which of a set's images a dump shows at (row, column): index, or -1. */
static int32_t dump_index(int32_t count, int row, int column)
{
	int32_t group = count / DUMP_ROWS / 16 * row * 16, k = group + column;
	return k < count ? k : -1;
}

/* A set of images in BC1, each alone, in ETC2 and ASTC's blocks. */
static int report_images(const char *in, const char *name, const uint8_t *bc1, int32_t count, int32_t size, const char *dump_path)
{
	static const int SIDES[] = { 0, 4, 5, 6, 8 }; /* 0: ETC2 */
	enum { DUMP_SIDE = 8 };
	size_t pixels = (size_t)size * (size_t)size, image = pixels / 2, k;
	uint8_t *rgba = (uint8_t *)malloc(pixels * 4), *back = (uint8_t *)malloc(pixels * 4), *blocks = (uint8_t *)malloc(pixels * 2);
	uint8_t *dump = dump_path ? (uint8_t *)calloc(pixels * DUMP_ROWS * DUMP_COLUMNS * DUMP_PANELS, 3) : NULL;
	int rc = rgba && back && blocks && (dump || !dump_path) ? 0 : fail(in, "out of memory");
	for (k = 0; k < sizeof SIDES / sizeof SIDES[0] && rc == 0; k++) {
		int side = SIDES[k], row, column;
		size_t n = side ? kp_hd_astc_bytes(size, side) : image;
		TileScore t;
		KpHdAstc *c = NULL;
		int32_t i;
		double t0 = seconds();
		memset(&t, 0, sizeof t);
		t.worst = 99.0;
		if (side && !(c = kp_hd_astc_open(KP_HD_LAYER_LIGHT + KP_HD_LIGHT_SPECULAR, side, ASTC_QUALITY))) rc = fail(in, "the encoder failed");
		for (i = 0; i < count && rc == 0; i++) {
			kp_bcn_decode(bc1 + image * (size_t)i, size, size, 0, rgba);
			if (side) {
				if (kp_hd_astc_encode(c, rgba, size, blocks, n) != 0 || kp_hd_astc_decode(c, blocks, n, size, back) != 0) rc = fail(in, "the encoder failed");
			} else {
				kp_etc2_rgb_encode(rgba, size, size, blocks);
				kp_etc2_rgb_decode(blocks, size, size, back);
			}
			if (rc != 0) break;
			tile_score(&t, rgba, back, size);
			for (row = 0; dump && row < DUMP_ROWS; row++) {
				for (column = 0; column < DUMP_COLUMNS; column++) {
					if (dump_index(count, row, column) != i) continue;
					if (side == 0) dump_tile(dump, size, 0, row, column, rgba);
					if (side == 0 || side == DUMP_SIDE) dump_tile(dump, size, side ? 2 : 1, row, column, back);
				}
			}
		}
		kp_hd_astc_close(c);
		if (rc == 0) {
			char label[16];
			if (side) snprintf(label, sizeof label, "astc %dx%d", side, side);
			else snprintf(label, sizeof label, "etc2");
			printf("%s %s %d x %d px: %-8s %7zu bytes each  %6.2f dB, worst %6.2f dB, edges %6.2f dB, detail %5.1f %%, %6.1f s\n", in, name, (int)count, (int)size, label, n,
				psnr(t.error, (double)t.weight), t.worst, psnr(t.edge_error, (double)t.edge_weight),
				t.source_detail > 0 ? 100.0 * t.back_detail / t.source_detail : 100.0, seconds() - t0);
			fflush(stdout);
		}
	}
	if (rc == 0 && dump) rc = write_dump(dump_path, dump, size);
	free(rgba);
	free(back);
	free(blocks);
	free(dump);
	return rc;
}

static int report_tiles(const char *in, const char *dump)
{
	size_t size = 0;
	uint8_t *file = read_file(in, &size);
	KpKpf c;
	KpError e;
	int rc = 1;
	if (!file) return fail(in, "cannot read");
	if (kp_kpf_parse(&c, file, size, in, 1, &e) != 0) {
		free(file);
		return fail(in, e.msg);
	}
	if (strcmp(c.kind, KP_HD_TILESET_KIND) == 0) {
		KpHdTileset ts;
		if (kp_hd_tileset_load(&ts, file, size, in, &e) != 0) rc = fail(in, e.msg);
		else if (ts.format != KP_HD_FORMAT_BC) rc = fail(in, "not BC tiles");
		else rc = report_images(in, "tiles", ts.tiles, ts.count, ts.size, dump);
	} else if (strcmp(c.kind, KP_HD_WATER_KIND) == 0) {
		KpHdWater w;
		if (kp_hd_water_load(&w, file, size, in, &e) != 0) rc = fail(in, e.msg);
		else if (w.format != KP_HD_FORMAT_BC) rc = fail(in, "not BC normals");
		else {
			rc = report_images(in, "normals a", w.blocks[0], w.frames[0], w.size[0], dump);
			if (rc == 0) rc = report_images(in, "normals b", w.blocks[1], w.frames[1], w.size[1], NULL);
		}
	} else {
		fail(in, "neither an HD tileset nor the water's normals");
	}
	kp_kpf_free(&c);
	free(file);
	return rc;
}
#else
static int report(const char *in, int32_t page)
{
	(void)page;
	return fail(in, "built without -DKP_ASTC=ON");
}

static int report_tiles(const char *in, const char *dump)
{
	(void)dump;
	return fail(in, "built without -DKP_ASTC=ON");
}
#endif

/* --- the self test --- */

/* Some bytes of solid BC blocks: a page's 4 x 4 blocks, a ramp over it (red and alpha across, green and blue down). */
static void selftest_blocks(uint8_t *out, int bc3, int32_t size)
{
	int32_t bx, by;
	for (by = 0; by < size / 4; by++) {
		for (bx = 0; bx < size / 4; bx++) {
			unsigned red = (unsigned)(bx * 31 * 4 / size), green = (unsigned)(by * 63 * 4 / size);
			unsigned colour = red << 11 | green << 5 | (green >> 1);
			uint8_t alpha = (uint8_t)(255 - bx * 255 * 4 / size);
			uint8_t *block = out + (size_t)(by * (size / 4) + bx) * (bc3 ? 16 : 8);
			memset(block, 0, bc3 ? 16 : 8);
			if (bc3) {
				block[0] = block[1] = alpha;
				block += 8;
			}
			block[0] = block[2] = (uint8_t)(colour & 0xff);
			block[1] = block[3] = (uint8_t)(colour >> 8);
		}
	}
}

/* A BC pack of pages pages, the first lit_pages lit, and one frame; NULL out of memory. */
static uint8_t *selftest_pack(int32_t size, int32_t pages, int32_t lit_pages, size_t *file_size)
{
	static const char *const SECTIONS[KP_HD_LAYERS] = { "DIFF", "TEAM", "NRML", "AODP", "SPEC", "EMIS" };
	uint8_t head[KP_HD_PACK_HEAD_BYTES] = { 2, 0, 0, 0, 0, 1, 0, 0, 0, KP_HD_FORMAT_BC, 0, 0 };
	uint8_t frame[14] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	KpKpfOut sections[KP_HD_LAYERS + 2];
	uint8_t *buffers[KP_HD_LAYERS] = { NULL }, *file = NULL;
	int count = 2, layer, ok = 1;
	int32_t k;
	head[1] = (uint8_t)size, head[2] = (uint8_t)(size >> 8);
	head[3] = (uint8_t)pages, head[10] = (uint8_t)lit_pages;
	frame[6] = frame[8] = pages > 0 ? 4 : 0;
	sections[0].fourcc = "HEAD", sections[0].payload = head, sections[0].size = sizeof head;
	sections[1].fourcc = "FRMS", sections[1].payload = frame, sections[1].size = sizeof frame;
	for (layer = 0; layer < KP_HD_LAYERS; layer++) {
		int32_t n = layer >= KP_HD_LAYER_LIGHT ? lit_pages : pages;
		size_t bytes = kp_hd_layer_bytes(KP_HD_FORMAT_BC, layer, size);
		if (layer >= KP_HD_LAYER_LIGHT && n == 0) continue;
		if (!(buffers[layer] = (uint8_t *)malloc(bytes * (size_t)n + 1))) {
			ok = 0;
			break;
		}
		for (k = 0; k < n; k++) selftest_blocks(buffers[layer] + bytes * (size_t)k, kp_hd_transcode_bc3(layer), size);
		sections[count].fourcc = SECTIONS[layer];
		sections[count].payload = buffers[layer];
		sections[count].size = bytes * (size_t)n;
		count++;
	}
	for (k = 0; k < count; k++) sections[k].schema_version = 0;
	if (ok && kp_kpf_build(KP_HD_PACK_KIND, KP_HD_PACK_SCHEMA, 0, "selftest", NULL, sections, count, &file, file_size) != 0) file = NULL;
	for (layer = 0; layer < KP_HD_LAYERS; layer++) free(buffers[layer]);
	return file;
}

/* A layer's blocks in a format read back as RGBA8; -1 failed. */
static int selftest_read_back(int format, int layer, const uint8_t *blocks, int32_t size, uint8_t *rgba)
{
	int codec = kp_hd_layer_codec(format, layer);
	if (codec < KP_HD_CODEC_ASTC_4X4) {
		etc2_read_back(layer, blocks, size, rgba);
		return 0;
	}
#ifdef KP_ASTC
	{
		KpHdAstc *c;
		int side, bytes, rc;
		kp_hd_codec_block(codec, &side, &bytes);
		c = kp_hd_astc_open(layer, side, ASTCENC_PRE_FASTEST);
		rc = c ? kp_hd_astc_decode(c, blocks, kp_hd_astc_bytes(size, side), size, rgba) : -1;
		kp_hd_astc_close(c);
		return rc;
	}
#else
	return -1;
#endif
}

/* One small pack through a format (kp_hd_transcode), opened and read back: the channels the shader reads come back. */
static int selftest_pack_through(int format, int32_t pages, int32_t lit_pages)
{
	enum { S = 64 };
	static uint8_t rgba[S * S * 4], back[S * S * 4];
	size_t size = 0, read = 0;
	uint8_t *source = selftest_pack(S, pages, lit_pages, &size), *file = NULL;
	KpHdPack bc, p;
	KpError e;
	int rc = -1, layer;
	int32_t k;
	if (!source) {
		fail("selftest", "out of memory");
	} else if (!(file = kp_hd_transcode(source, size, format, 2, &read, &e))) {
		fail("selftest", e.msg);
	} else if (kp_hd_pack_open(&bc, source, size, size, "the BC pack", &e) != 0
		|| kp_hd_pack_open(&p, file, read, read, "the transcoded pack", &e) != 0) {
		fail("selftest", e.msg);
	} else if (p.format != format || p.page_count != pages || p.lit_pages != lit_pages || p.frame_count != 1) {
		fail("selftest", "the header changed");
	} else {
		rc = 0;
		for (k = 0; k < pages && rc == 0; k++) {
			for (layer = 0; layer < KP_HD_LAYERS && rc == 0; layer++) {
				double q[3];
				uint32_t from = kp_hd_transcode_offset(&bc, k, layer), to = kp_hd_transcode_offset(&p, k, layer);
				if ((from == 0) != (to == 0)) rc = fail("selftest", "a page's lighting");
				if (rc != 0 || from == 0) continue;
				kp_bcn_decode(source + from, S, S, kp_hd_transcode_bc3(layer), rgba);
				if (selftest_read_back(format, layer, file + to, S, back) != 0) rc = fail("selftest", "cannot read a layer back");
				if (rc != 0) continue;
				layer_psnr(layer, rgba, back, NULL, S * S, q);
				printf("selftest format %d, %d pages, %d lit: page %d layer %d: %.2f dB, %.2f dB\n", format, (int)pages, (int)lit_pages,
					(int)k, layer, q[0], q[1]);
				if (q[0] < 30.0 || q[1] < 30.0) rc = fail("selftest", "a channel did not come back");
			}
		}
	}
	free(source);
	free(file);
	return rc;
}

static int selftest(void)
{
	static const int32_t PACKS[][2] = { { 2, 1 }, { 1, 0 }, { 0, 0 } }; /* pages, lit pages */
	static const int FORMATS[] = {
		KP_HD_FORMAT_ETC2,
#ifdef KP_ASTC
		KP_HD_FORMAT_ASTC,
#endif
	};
	size_t f, k;
	int layer;
	/* etc2_layer writes each layer not in ASTC as KP_HD_FORMAT_ETC2 has it */
	for (layer = 0; layer < KP_HD_LAYERS; layer++) {
		int codec = kp_hd_layer_codec(KP_HD_FORMAT_ASTC, layer);
		if (codec < KP_HD_CODEC_ASTC_4X4 && codec != kp_hd_layer_codec(KP_HD_FORMAT_ETC2, layer)) {
			return fail("selftest", "a layer in EAC or ETC2 unlike KP_HD_FORMAT_ETC2's");
		}
	}
	for (f = 0; f < sizeof FORMATS / sizeof FORMATS[0]; f++) {
		for (k = 0; k < sizeof PACKS / sizeof PACKS[0]; k++) {
			if (selftest_pack_through(FORMATS[f], PACKS[k][0], PACKS[k][1]) != 0) return 1;
		}
	}
	return 0;
}

int main(int argc, char **argv)
{
	if (argc == 2 && strcmp(argv[1], "--selftest") == 0) return selftest();
	if (argc == 4 && strcmp(argv[1], "--report") == 0) return report(argv[2], (int32_t)atoi(argv[3]));
	if ((argc == 3 || argc == 4) && strcmp(argv[1], "--report-tiles") == 0) return report_tiles(argv[2], argc == 4 ? argv[3] : NULL);
	fprintf(stderr, "usage: kp_hd_phone --report IN PAGE | --report-tiles IN [DUMP.ppm] | --selftest\n");
	return 2;
}
