/*
 * kp_hd_phone: the HD art of BC blocks written again for phones (ADR 0013, stage R4f; tools/product.sh runs it when it
 * stages the HD art for Android and iOS).
 *
 *   kp_hd_phone etc2 IN OUT   an HD pack (HDPK), HD tileset (HDTS) or the water's normal maps (HDWA) in ETC2 and EAC:
 *                             a page's diffuse in ETC2 RGBA8 (EAC alpha), its team mask in EAC R11, a lit page's
 *                             lighting layers in EAC RG11 and ETC2 RGB8 (kp_hd_light_to_etc2), each tile's and normal
 *                             map's BC1 in ETC2 RGB8 (kp_etc.c)
 *   kp_hd_phone astc IN OUT   an HD pack in ASTC (kp_hd_layer_codec's blocks, the Arm encoder), its normal maps and
 *                             occlusion in EAC as etc2's (no ASTC block reaches them)
 *   kp_hd_phone --report IN PAGE   each of a pack page's layers in ASTC's candidate blocks and in ETC2: bytes, encoding
 *                             time, PSNR against its BC blocks decoded (and in the frames' rims), the diffuse's clear
 *                             texels read back with alpha: what the blocks of KP_HD_FORMAT_ASTC were chosen by
 *   kp_hd_phone --selftest DIR   small packs through etc2 and astc into DIR, opened and read back as the product does
 *
 * A pack's pages are encoded on KP_HD_PHONE_THREADS threads (default 1; with -DKP_ASTC=ON, else one).
 * The frames, the water masks, creep's edges (RGBA8) and the header's other fields stay. The output is written beside
 * its path and renamed into it once whole, so an interrupted run leaves no partial file that tools/product.sh would
 * take for current. The ASTC modes need -DKP_ASTC=ON; the product itself has no ASTC codec: it uploads the blocks.
 *
 * TODO: the tiles and the water's normal maps in ASTC too: a 64-pixel tile is a whole number of blocks only at 4 x 4
 * (twice ETC2's bytes) and 8 x 8 (2 bits a pixel), which kp_gfx's terrain atlas needs.
 */
#include "kp_present.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef KP_ASTC
#include <pthread.h>

#include "astcenc.h"
#endif

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

static int write_file(const char *path, const uint8_t *data, size_t size)
{
	char part[4096];
	FILE *f;
	int ok;
	if ((size_t)snprintf(part, sizeof part, "%s.part", path) >= sizeof part || !(f = fopen(part, "wb"))) return -1;
	ok = fwrite(data, 1, size, f) == size;
	ok = fclose(f) == 0 && ok && rename(part, path) == 0;
	if (!ok) remove(part);
	return ok ? 0 : -1;
}

/* Says what failed; 1, the tool's exit status. */
static int fail(const char *what, const char *msg)
{
	fprintf(stderr, "kp_hd_phone: %s: %s\n", what, msg);
	return 1;
}

static int layer_is_bc3(int layer) { return kp_hd_layer_codec(KP_HD_FORMAT_BC, layer) == KP_HD_CODEC_BC3; }

/* The file offset of a page's layer (0: a page not lit has no lighting). */
static uint32_t layer_offset(const KpHdPack *p, int32_t page, int layer)
{
	uint32_t d, t, light[KP_HD_LIGHTS];
	kp_hd_page_offsets(p, page, &d, &t, light);
	return layer == KP_HD_LAYER_DIFFUSE ? d : layer == KP_HD_LAYER_TEAM ? t : light[layer - KP_HD_LAYER_LIGHT];
}

/* --- ETC2 and EAC --- */

/* One page's layer of BC blocks in ETC2's format; -1 out of memory. */
static int etc2_layer(int layer, const uint8_t *bc, int32_t size, uint8_t *out)
{
	size_t pixels = (size_t)size * (size_t)size, i;
	uint8_t *rgba;
	if (layer >= KP_HD_LAYER_LIGHT) return kp_hd_light_to_etc2(layer - KP_HD_LAYER_LIGHT, bc, size, out);
	if (!(rgba = (uint8_t *)malloc(pixels * 4))) return -1;
	kp_bcn_decode(bc, size, size, layer_is_bc3(layer), rgba);
	if (layer == KP_HD_LAYER_DIFFUSE) {
		kp_etc2_encode(rgba, size, size, out);
	} else { /* the team mask's red, in place */
		for (i = 0; i < pixels; i++) rgba[i] = rgba[4 * i];
		kp_eac_r11_encode(rgba, size, size, out);
	}
	free(rgba);
	return 0;
}

/* count images of size squared, BC1 blocks, into ETC2 RGB8 ones (the same size); -1 out of memory. */
static int bc1_to_etc2(const uint8_t *in, int32_t count, int32_t size, uint8_t *out)
{
	size_t image = (size_t)size * (size_t)size / 2;
	uint8_t *rgba = (uint8_t *)malloc((size_t)size * (size_t)size * 4);
	int32_t k;
	if (!rgba) return -1;
	for (k = 0; k < count; k++) {
		kp_bcn_decode(in + image * (size_t)k, size, size, 0, rgba);
		kp_etc2_rgb_encode(rgba, size, size, out + image * (size_t)k);
	}
	free(rgba);
	return 0;
}

/* --- ASTC --- */

#ifdef KP_ASTC
/* The encoder's effort: its quality levels' "thorough" (encoding all of HD2 takes a minute, once). */
#define ASTC_QUALITY ASTCENC_PRE_THOROUGH
/* The diffuse's alpha against its colour, which USE_ALPHA_WEIGHT weights by alpha: where the worst of its alpha and its
 * colour on the frames' rims falls least below ETC2's (kp_hd_phone --report on HD2; ADR 0013). */
#define ASTC_DIFFUSE_ALPHA_WEIGHT 16.0f

/* An encoder and decoder of a layer's blocks, for one thread (which then needs no reset between pages). */
typedef struct {
	struct astcenc_context *context;
	struct astcenc_swizzle swizzle;
} AstcCodec;

/* What the encoder sees of a layer decoded from BC, as the shader reads it (kp_gfx_shaders.c): the diffuse with its
 * alpha's weight on the colour; the team mask's red and the occlusion's green as grey; the normal map's x (red) and y
 * (alpha) in the encoder's normal mode, in the luminance and alpha of its blocks; the colours opaque. */
static void astc_input(int layer, struct astcenc_swizzle *s, unsigned *flags)
{
	static const struct astcenc_swizzle DIFFUSE = { ASTCENC_SWZ_R, ASTCENC_SWZ_G, ASTCENC_SWZ_B, ASTCENC_SWZ_A };
	static const struct astcenc_swizzle TEAM = { ASTCENC_SWZ_R, ASTCENC_SWZ_R, ASTCENC_SWZ_R, ASTCENC_SWZ_1 };
	static const struct astcenc_swizzle NORMAL = { ASTCENC_SWZ_R, ASTCENC_SWZ_R, ASTCENC_SWZ_R, ASTCENC_SWZ_A };
	static const struct astcenc_swizzle AO = { ASTCENC_SWZ_G, ASTCENC_SWZ_G, ASTCENC_SWZ_G, ASTCENC_SWZ_1 };
	static const struct astcenc_swizzle COLOUR = { ASTCENC_SWZ_R, ASTCENC_SWZ_G, ASTCENC_SWZ_B, ASTCENC_SWZ_1 };
	*flags = 0;
	switch (layer) {
	case KP_HD_LAYER_DIFFUSE:
		*s = DIFFUSE;
		*flags = ASTCENC_FLG_USE_ALPHA_WEIGHT;
		break;
	case KP_HD_LAYER_TEAM: *s = TEAM; break;
	case KP_HD_LAYER_LIGHT + KP_HD_LIGHT_NORMAL:
		*s = NORMAL;
		*flags = ASTCENC_FLG_MAP_NORMAL;
		break;
	case KP_HD_LAYER_LIGHT + KP_HD_LIGHT_AO: *s = AO; break;
	default: *s = COLOUR; break;
	}
}

static int astc_open(AstcCodec *c, int layer, int side, float quality)
{
	struct astcenc_config config;
	unsigned flags;
	c->context = NULL;
	astc_input(layer, &c->swizzle, &flags);
	if (astcenc_config_init(ASTCENC_PRF_LDR, (unsigned)side, (unsigned)side, 1, quality, flags, &config) != ASTCENC_SUCCESS) {
		return -1;
	}
	if (layer == KP_HD_LAYER_DIFFUSE) config.cw_a_weight = ASTC_DIFFUSE_ALPHA_WEIGHT;
	return astcenc_context_alloc(&config, 1, &c->context, NULL) == ASTCENC_SUCCESS ? 0 : -1;
}

static void astc_close(AstcCodec *c)
{
	if (c->context) astcenc_context_free(c->context);
	c->context = NULL;
}

static void astc_image(struct astcenc_image *image, void **plane, uint8_t *rgba, int32_t size)
{
	plane[0] = rgba;
	image->dim_x = image->dim_y = (unsigned)size;
	image->dim_z = 1;
	image->data_type = ASTCENC_TYPE_U8;
	image->data = plane;
}

/* RGBA8 of size squared into blocks (out_size bytes); -1 failed. */
static int astc_encode(AstcCodec *c, uint8_t *rgba, int32_t size, uint8_t *out, size_t out_size)
{
	struct astcenc_image image;
	void *plane[1];
	astc_image(&image, plane, rgba, size);
	return astcenc_compress_image(c->context, &image, &c->swizzle, out, out_size, 0) == ASTCENC_SUCCESS ? 0 : -1;
}

/* The blocks back into RGBA8, as the GPU reads them (no swizzle); -1 failed. */
static int astc_decode(AstcCodec *c, const uint8_t *blocks, size_t blocks_size, int32_t size, uint8_t *rgba)
{
	static const struct astcenc_swizzle IDENTITY = { ASTCENC_SWZ_R, ASTCENC_SWZ_G, ASTCENC_SWZ_B, ASTCENC_SWZ_A };
	struct astcenc_image image;
	void *plane[1];
	astc_image(&image, plane, rgba, size);
	return astcenc_decompress_image(c->context, blocks, blocks_size, &image, &IDENTITY, 0) == ASTCENC_SUCCESS ? 0 : -1;
}

static size_t astc_bytes(int32_t size, int side) { return (size_t)((size + side - 1) / side) * (size_t)((size + side - 1) / side) * 16; }

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
			uint32_t offset = layer_offset(&p, page, layer);
			double q[3], t;
			size_t k, j;
			if (offset == 0) continue; /* an unlit page's lighting */
			kp_bcn_decode(file + offset, p.page_size, p.page_size, layer_is_bc3(layer), rgba);
			t = seconds();
			rc = etc2_layer(layer, file + offset, p.page_size, blocks);
			if (rc != 0) break;
			t = seconds() - t;
			etc2_read_back(layer, blocks, p.page_size, back);
			layer_psnr(layer, rgba, back, rim, pixels, q);
			printf("%s page %d %-9s etc2     %8zu bytes %7.1f s  %6.2f dB %6.2f dB rim %6.2f dB clear %zu\n", in, (int)page, NAMES[layer],
				kp_hd_layer_bytes(KP_HD_FORMAT_ETC2, layer, p.page_size), t, q[0], q[1], q[2],
				layer == KP_HD_LAYER_DIFFUSE ? clear_texels_with_alpha(rgba, back, pixels) : 0);
			for (k = 0; k < sizeof SIDES / sizeof SIDES[0] && rc == 0; k++) {
				size_t n = astc_bytes(p.page_size, SIDES[k]);
				for (j = 0; j < sizeof qualities / sizeof qualities[0] && rc == 0; j++) {
					AstcCodec c;
					rc = astc_open(&c, layer, SIDES[k], qualities[j]);
					if (rc == 0) {
						t = seconds();
						rc = astc_encode(&c, rgba, p.page_size, blocks, n);
						t = seconds() - t;
					}
					if (rc == 0) rc = astc_decode(&c, blocks, n, p.page_size, back);
					astc_close(&c);
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
#else
static int report(const char *in, int32_t page)
{
	(void)page;
	return fail(in, "built without -DKP_ASTC=ON");
}
#endif

/* --- the files --- */

/* The pages of a layer, taken one at a time by the threads encoding them. */
typedef struct {
	const KpHdPack *p;
	const uint8_t *file;
	int format, layer;
	size_t page_bytes;
	uint8_t *out;
	int32_t pages, next;
	int failed;
#ifdef KP_ASTC
	pthread_mutex_t lock;
#endif
} LayerJob;

/* A page to encode and what the last one came to; -1 none left (or one failed). */
static int32_t job_next(LayerJob *j, int rc)
{
	int32_t page;
#ifdef KP_ASTC
	pthread_mutex_lock(&j->lock);
#endif
	if (rc != 0) j->failed = 1;
	page = j->failed || j->next >= j->pages ? -1 : j->next++;
#ifdef KP_ASTC
	pthread_mutex_unlock(&j->lock);
#endif
	return page;
}

static void *layer_worker(void *arg)
{
	LayerJob *j = (LayerJob *)arg;
	int codec = kp_hd_layer_codec(j->format, j->layer), rc = 0;
	int32_t page, size = j->p->page_size;
	/* EAC's and ETC2's codec is etc2_layer's by the layer: a layer in another of them is a table's error (the selftest) */
	int astc = codec >= KP_HD_CODEC_ASTC_4X4;
#ifdef KP_ASTC
	AstcCodec c;
	uint8_t *rgba = NULL;
	c.context = NULL;
	if (astc) {
		int side, bytes;
		kp_hd_codec_block(codec, &side, &bytes);
		rgba = (uint8_t *)malloc((size_t)size * (size_t)size * 4);
		rc = rgba && astc_open(&c, j->layer, side, ASTC_QUALITY) == 0 ? 0 : -1;
	}
#else
	rc = astc ? fail("astc", "built without -DKP_ASTC=ON") : 0;
#endif
	while ((page = job_next(j, rc)) >= 0) {
		const uint8_t *bc = j->file + layer_offset(j->p, page, j->layer);
		uint8_t *out = j->out + j->page_bytes * (size_t)page;
#ifdef KP_ASTC
		if (astc) {
			kp_bcn_decode(bc, size, size, layer_is_bc3(j->layer), rgba);
			rc = astc_encode(&c, rgba, size, out, j->page_bytes);
			continue;
		}
#endif
		rc = etc2_layer(j->layer, bc, size, out);
	}
#ifdef KP_ASTC
	astc_close(&c);
	free(rgba);
#endif
	return NULL;
}

#ifdef KP_ASTC
/* The threads a pack's pages are encoded on. */
static int encode_threads(void)
{
	enum { THREADS_MAX = 64 };
	const char *v = getenv("KP_HD_PHONE_THREADS");
	int n = v ? atoi(v) : 1;
	return n < 1 ? 1 : n > THREADS_MAX ? THREADS_MAX : n;
}

#endif

/* A layer's pages encoded into j->out; 0 done. */
static int encode_layer(LayerJob *j)
{
#ifdef KP_ASTC
	pthread_t *threads;
	int n = encode_threads(), started = 0, k;
	if (pthread_mutex_init(&j->lock, NULL) != 0) return -1;
	if ((threads = (pthread_t *)malloc(sizeof *threads * (size_t)n)) != NULL) {
		for (k = 1; k < n; k++) { /* fewer threads when the system has no more */
			if (pthread_create(&threads[started], NULL, layer_worker, j) != 0) break;
			started++;
		}
	}
	layer_worker(j);
	for (k = 0; k < started; k++) pthread_join(threads[k], NULL);
	free(threads);
	pthread_mutex_destroy(&j->lock);
#else
	layer_worker(j);
#endif
	return j->failed ? -1 : 0;
}

/* A pack's sections in another format (a pack without lit pages has no lighting's, one without pages DIFF and TEAM
 * empty, as kp_hd_pack_open asks); 0 done, else an error said. */
static int pack_sections(const KpHdPack *p, const uint8_t *file, int format, KpKpfOut *sections, uint8_t **buffers, int *count,
	const char *in)
{
	static const char *const SECTIONS[KP_HD_LAYERS] = { "DIFF", "TEAM", "NRML", "AODP", "SPEC", "EMIS" };
	int layer;
	for (layer = 0; layer < KP_HD_LAYERS; layer++) {
		LayerJob j;
		memset(&j, 0, sizeof j);
		j.p = p;
		j.file = file;
		j.format = format;
		j.layer = layer;
		j.page_bytes = kp_hd_layer_bytes(format, layer, p->page_size);
		j.pages = layer >= KP_HD_LAYER_LIGHT ? p->lit_pages : p->page_count;
		if (layer >= KP_HD_LAYER_LIGHT && j.pages == 0) continue;
		if (!(buffers[layer] = (uint8_t *)malloc(j.page_bytes * (size_t)j.pages + 1))) return fail(in, "out of memory");
		j.out = buffers[layer];
		if (encode_layer(&j) != 0) return fail(in, "cannot encode a page");
		sections[*count].fourcc = SECTIONS[layer];
		sections[*count].payload = buffers[layer];
		sections[*count].size = j.page_bytes * (size_t)j.pages;
		(*count)++;
	}
	return 0;
}

static int transcode(int format, const char *in, const char *out)
{
	size_t size = 0, out_size = 0;
	uint8_t *file = read_file(in, &size), *built = NULL, *buffers[KP_HD_LAYERS] = { NULL }, head[16];
	KpKpf c;
	KpError e;
	KpKpfOut sections[KP_HD_LAYERS + 2];
	int count = 0, rc = 1, k;
	if (!file) return fail(in, "cannot read");
	if (kp_kpf_parse(&c, file, size, in, 1, &e) != 0) {
		free(file);
		return fail(in, e.msg);
	}
	if (strcmp(c.kind, KP_HD_PACK_KIND) == 0) {
		KpHdPack p;
		const KpKpfSection *h = kp_kpf_find(&c, "HEAD"), *f = kp_kpf_find(&c, "FRMS");
		if (kp_hd_pack_open(&p, file, size, size, in, &e) != 0) {
			fail(in, e.msg);
		} else if (p.format != KP_HD_FORMAT_BC) {
			fail(in, "not BC pages");
		} else {
			memcpy(head, h->data, KP_HD_PACK_HEAD_BYTES);
			head[KP_HD_PACK_HEAD_FORMAT] = (uint8_t)format;
			sections[0].fourcc = "HEAD", sections[0].payload = head, sections[0].size = KP_HD_PACK_HEAD_BYTES;
			sections[1].fourcc = "FRMS", sections[1].payload = f->data, sections[1].size = f->size;
			count = 2;
			if (pack_sections(&p, file, format, sections, buffers, &count, in) != 0) count = 0;
		}
	} else if (format != KP_HD_FORMAT_ETC2) {
		fail(in, "only the HD packs go in ASTC");
	} else if (strcmp(c.kind, KP_HD_TILESET_KIND) == 0) {
		KpHdTileset ts;
		const KpKpfSection *h = kp_kpf_find(&c, "HEAD"), *wmap = kp_kpf_find(&c, "WMAP"), *wmsk = kp_kpf_find(&c, "WMSK");
		const KpKpfSection *edge = kp_kpf_find(&c, "EDGE");
		if (kp_hd_tileset_load(&ts, file, size, in, &e) != 0) {
			fail(in, e.msg);
		} else if (ts.format != KP_HD_FORMAT_BC) {
			fail(in, "not BC tiles");
		} else if (!(buffers[0] = (uint8_t *)malloc((size_t)ts.size * (size_t)ts.size / 2 * (size_t)ts.count + 1))
			|| bc1_to_etc2(ts.tiles, ts.count, ts.size, buffers[0]) != 0) {
			fail(in, "out of memory");
		} else {
			memcpy(head, h->data, 8);
			memcpy(head + 4, "ETC2", 4);
			sections[0].fourcc = "HEAD", sections[0].payload = head, sections[0].size = 8;
			sections[1].fourcc = "TILE", sections[1].payload = buffers[0];
			sections[1].size = (size_t)ts.size * (size_t)ts.size / 2 * (size_t)ts.count;
			count = 2;
			if (wmap && wmsk) { /* the water masks as they are */
				sections[2].fourcc = "WMAP", sections[2].payload = wmap->data, sections[2].size = wmap->size;
				sections[3].fourcc = "WMSK", sections[3].payload = wmsk->data, sections[3].size = wmsk->size;
				count = 4;
			}
			if (edge) { /* creep's edges as they are: RGBA8 */
				sections[count].fourcc = "EDGE", sections[count].payload = edge->data, sections[count].size = edge->size;
				count++;
			}
		}
	} else if (strcmp(c.kind, KP_HD_WATER_KIND) == 0) {
		KpHdWater w;
		const KpKpfSection *h = kp_kpf_find(&c, "HEAD");
		size_t na, nb;
		if (kp_hd_water_load(&w, file, size, in, &e) != 0) {
			fail(in, e.msg);
		} else if (w.format != KP_HD_FORMAT_BC) {
			fail(in, "not BC normals");
		} else if (!(buffers[0] = (uint8_t *)malloc((na = (size_t)w.size[0] * (size_t)w.size[0] / 2 * (size_t)w.frames[0]) + 1))
			|| !(buffers[1] = (uint8_t *)malloc((nb = (size_t)w.size[1] * (size_t)w.size[1] / 2 * (size_t)w.frames[1]) + 1))
			|| bc1_to_etc2(w.blocks[0], w.frames[0], w.size[0], buffers[0]) != 0
			|| bc1_to_etc2(w.blocks[1], w.frames[1], w.size[1], buffers[1]) != 0) {
			fail(in, "out of memory");
		} else {
			memcpy(head, h->data, 12);
			memcpy(head + 8, "ETC2", 4);
			sections[0].fourcc = "HEAD", sections[0].payload = head, sections[0].size = 12;
			sections[1].fourcc = "NRMA", sections[1].payload = buffers[0], sections[1].size = na;
			sections[2].fourcc = "NRMB", sections[2].payload = buffers[1], sections[2].size = nb;
			count = 3;
		}
	} else {
		fail(in, "neither an HD pack, an HD tileset nor the water's normals");
	}
	if (count > 0) {
		for (k = 0; k < count; k++) sections[k].schema_version = 0;
		if (kp_kpf_build(c.kind, c.schema_version, c.converter_version, c.profile_id, c.source_hash, sections, count,
			&built, &out_size) != 0) {
			fail(out, "cannot lay out the container");
		} else if (write_file(out, built, out_size) != 0) {
			fail(out, "cannot write");
		} else {
			rc = 0;
		}
	}
	free(built);
	for (k = 0; k < KP_HD_LAYERS; k++) free(buffers[k]);
	kp_kpf_free(&c);
	free(file);
	return rc;
}

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
		for (k = 0; k < n; k++) selftest_blocks(buffers[layer] + bytes * (size_t)k, layer_is_bc3(layer), size);
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
		AstcCodec c;
		int side, bytes, rc;
		kp_hd_codec_block(codec, &side, &bytes);
		rc = astc_open(&c, layer, side, ASTCENC_PRE_FASTEST);
		if (rc == 0) rc = astc_decode(&c, blocks, astc_bytes(size, side), size, rgba);
		astc_close(&c);
		return rc;
	}
#else
	return -1;
#endif
}

/* One small pack through a format, opened and read back: the channels the shader reads come back. */
static int selftest_pack_through(const char *dir, int format, int32_t pages, int32_t lit_pages)
{
	enum { S = 64 };
	static uint8_t rgba[S * S * 4], back[S * S * 4];
	char in[4096], out[4096];
	size_t size = 0, read = 0;
	uint8_t *source = selftest_pack(S, pages, lit_pages, &size), *file = NULL;
	KpHdPack bc, p;
	KpError e;
	int rc = -1, layer;
	int32_t k;
	if ((size_t)snprintf(in, sizeof in, "%s/kp_hd_phone_selftest_in.kpd", dir) >= sizeof in
		|| (size_t)snprintf(out, sizeof out, "%s/kp_hd_phone_selftest_out.kpd", dir) >= sizeof out) {
		free(source);
		return fail(dir, "path too long");
	}
	if (!source || write_file(in, source, size) != 0) {
		fail(in, "cannot write");
	} else if (transcode(format, in, out) != 0) {
		fail(in, "cannot transcode");
	} else if (!(file = read_file(out, &read))) {
		fail(out, "cannot read");
	} else if (kp_hd_pack_open(&bc, source, size, size, in, &e) != 0 || kp_hd_pack_open(&p, file, read, read, out, &e) != 0) {
		fail(out, e.msg);
	} else if (p.format != format || p.page_count != pages || p.lit_pages != lit_pages || p.frame_count != 1) {
		fail(out, "the header changed");
	} else {
		rc = 0;
		for (k = 0; k < pages && rc == 0; k++) {
			for (layer = 0; layer < KP_HD_LAYERS && rc == 0; layer++) {
				double q[3];
				uint32_t from = layer_offset(&bc, k, layer), to = layer_offset(&p, k, layer);
				if ((from == 0) != (to == 0)) rc = fail(out, "a page's lighting");
				if (rc != 0 || from == 0) continue;
				kp_bcn_decode(source + from, S, S, layer_is_bc3(layer), rgba);
				if (selftest_read_back(format, layer, file + to, S, back) != 0) rc = fail(out, "cannot read a layer back");
				if (rc != 0) continue;
				layer_psnr(layer, rgba, back, NULL, S * S, q);
				printf("selftest format %d, %d pages, %d lit: page %d layer %d: %.2f dB, %.2f dB\n", format, (int)pages, (int)lit_pages,
					(int)k, layer, q[0], q[1]);
				if (q[0] < 30.0 || q[1] < 30.0) rc = fail(out, "a channel did not come back");
			}
		}
	}
	remove(in);
	remove(out);
	free(source);
	free(file);
	return rc;
}

static int selftest(const char *dir)
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
			if (selftest_pack_through(dir, FORMATS[f], PACKS[k][0], PACKS[k][1]) != 0) return 1;
		}
	}
	return 0;
}

int main(int argc, char **argv)
{
	if (argc == 3 && strcmp(argv[1], "--selftest") == 0) return selftest(argv[2]);
	if (argc == 4 && strcmp(argv[1], "--report") == 0) return report(argv[2], (int32_t)atoi(argv[3]));
	if (argc == 4 && strcmp(argv[1], "etc2") == 0) return transcode(KP_HD_FORMAT_ETC2, argv[2], argv[3]);
	if (argc == 4 && strcmp(argv[1], "astc") == 0) return transcode(KP_HD_FORMAT_ASTC, argv[2], argv[3]);
	fprintf(stderr, "usage: kp_hd_phone etc2|astc IN OUT | --report IN PAGE | --selftest DIR\n");
	return 2;
}
