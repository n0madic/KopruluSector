/*
 * kp_hd_phone: the HD art of BC blocks written again for phones (ADR 0013, stage R4f; tools/product.sh runs it when it
 * stages the HD art for Android and iOS).
 *
 *   kp_hd_phone etc2 IN OUT   an HD pack (HDPK), HD tileset (HDTS) or the water's normal maps (HDWA) in ETC2 and EAC:
 *                             a page's diffuse in ETC2 RGBA8 (EAC alpha), its team mask in EAC R11, a lit page's
 *                             lighting layers in EAC RG11 and ETC2 RGB8 (kp_hd_light_to_etc2), each tile's and normal
 *                             map's BC1 in ETC2 RGB8 (kp_etc.c)
 *   kp_hd_phone astc IN OUT   an HD pack in ASTC (kp_hd_layer_block's blocks, the Arm encoder, one thread), its normal
 *                             maps and occlusion in EAC RG11 as etc2's (no ASTC block reaches them)
 *   kp_hd_phone --report IN PAGE   each of a pack page's layers in ASTC's candidate blocks and in ETC2: bytes, encoding
 *                             time and PSNR against its BC blocks decoded, what the blocks of KP_HD_FORMAT_ASTC were
 *                             chosen by
 *   kp_hd_phone --selftest    the ASTC encoder's channels through the encoder and decoder on a small page
 *
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

static int fail(const char *what, const char *msg)
{
	fprintf(stderr, "kp_hd_phone: %s: %s\n", what, msg);
	return 1;
}

/* A layer's BC blocks: BC3 (else BC1). */
static int layer_is_bc3(int layer)
{
	return layer == KP_HD_LAYER_DIFFUSE || layer == KP_HD_LAYER_LIGHT + KP_HD_LIGHT_NORMAL
		|| layer == KP_HD_LAYER_LIGHT + KP_HD_LIGHT_AO;
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
/* The encoder's effort: its quality levels' "thorough" (encoding all of HD2 takes minutes, once). */
#define ASTC_QUALITY ASTCENC_PRE_THOROUGH

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

static int astc_context(int layer, int block_w, int block_h, float quality, struct astcenc_context **ctx,
	struct astcenc_swizzle *swizzle)
{
	struct astcenc_config config;
	unsigned flags;
	astc_input(layer, swizzle, &flags);
	if (astcenc_config_init(ASTCENC_PRF_LDR, (unsigned)block_w, (unsigned)block_h, 1, quality, flags, &config)
		!= ASTCENC_SUCCESS) {
		return -1;
	}
	return astcenc_context_alloc(&config, 1, ctx, NULL) == ASTCENC_SUCCESS ? 0 : -1;
}

/* RGBA8 of size squared into blocks of block_w x block_h (out_size bytes) as the layer's input; -1 failed. */
static int astc_encode(int layer, uint8_t *rgba, int32_t size, int block_w, int block_h, float quality, uint8_t *out,
	size_t out_size)
{
	struct astcenc_context *ctx = NULL;
	struct astcenc_swizzle swizzle;
	struct astcenc_image image;
	void *planes[1];
	int rc = -1;
	if (astc_context(layer, block_w, block_h, quality, &ctx, &swizzle) != 0) return -1;
	planes[0] = rgba;
	image.dim_x = image.dim_y = (unsigned)size;
	image.dim_z = 1;
	image.data_type = ASTCENC_TYPE_U8;
	image.data = planes;
	if (astcenc_compress_image(ctx, &image, &swizzle, out, out_size, 0) == ASTCENC_SUCCESS) rc = 0;
	astcenc_context_free(ctx);
	return rc;
}

/* The blocks back into RGBA8, as the GPU reads them (no swizzle); -1 failed. */
static int astc_decode(int layer, const uint8_t *blocks, size_t blocks_size, int32_t size, int block_w, int block_h,
	uint8_t *rgba)
{
	static const struct astcenc_swizzle IDENTITY = { ASTCENC_SWZ_R, ASTCENC_SWZ_G, ASTCENC_SWZ_B, ASTCENC_SWZ_A };
	struct astcenc_context *ctx = NULL;
	struct astcenc_swizzle swizzle;
	struct astcenc_image image;
	void *planes[1];
	int rc = -1;
	if (astc_context(layer, block_w, block_h, ASTCENC_PRE_FASTEST, &ctx, &swizzle) != 0) return -1;
	planes[0] = rgba;
	image.dim_x = image.dim_y = (unsigned)size;
	image.dim_z = 1;
	image.data_type = ASTCENC_TYPE_U8;
	image.data = planes;
	if (astcenc_decompress_image(ctx, blocks, blocks_size, &image, &IDENTITY, 0) == ASTCENC_SUCCESS) rc = 0;
	astcenc_context_free(ctx);
	return rc;
}

static size_t astc_bytes(int32_t size, int block_w, int block_h)
{
	return (size_t)((size + block_w - 1) / block_w) * (size_t)((size + block_h - 1) / block_h) * 16;
}

/* Whether a layer goes in ASTC blocks in KP_HD_FORMAT_ASTC (else as in ETC2's). */
static int astc_has_layer(int layer)
{
	return layer != KP_HD_LAYER_LIGHT + KP_HD_LIGHT_NORMAL && layer != KP_HD_LAYER_LIGHT + KP_HD_LIGHT_AO;
}

static int astc_layer(int layer, const uint8_t *bc, int32_t size, uint8_t *out)
{
	uint8_t *rgba;
	int block_w, block_h, rc;
	if (!astc_has_layer(layer)) return etc2_layer(layer, bc, size, out);
	if (!(rgba = (uint8_t *)malloc((size_t)size * (size_t)size * 4))) return -1;
	kp_hd_layer_block(KP_HD_FORMAT_ASTC, layer, &block_w, &block_h);
	kp_bcn_decode(bc, size, size, layer_is_bc3(layer), rgba);
	rc = astc_encode(layer, rgba, size, block_w, block_h, ASTC_QUALITY, out, astc_bytes(size, block_w, block_h));
	free(rgba);
	return rc;
}

/* --- the blocks' choice --- */

static double seconds(void) { return (double)clock() / CLOCKS_PER_SEC; }

static double psnr(double squared, double weight)
{
	return squared <= 0 || weight <= 0 ? 99.0 : 10.0 * log10(255.0 * 255.0 * weight / squared);
}

/* A layer's PSNR, by what the shader reads: the channels of its source (a) against the ones read back (b) (in b, a
 * team mask's and occlusion's grey in red, ETC2's normal map's y in alpha by report_etc2): the colour, weighted by the
 * diffuse's alpha, and alpha, or the normal map's x and y. */
static void layer_psnr(int layer, const uint8_t *a, const uint8_t *b, size_t pixels, double out[2])
{
	double e0 = 0, w0 = 0, e1 = 0;
	size_t i;
	int c;
	for (i = 0; i < pixels; i++) {
		const uint8_t *p = a + 4 * i, *q = b + 4 * i;
		double d;
		switch (layer) {
		case KP_HD_LAYER_DIFFUSE: {
			double w = p[3] / 255.0;
			for (c = 0; c < 3; c++) {
				d = (double)p[c] - q[c];
				e0 += w * d * d;
			}
			w0 += 3 * w;
			d = (double)p[3] - q[3];
			e1 += d * d;
			break;
		}
		case KP_HD_LAYER_TEAM:
			d = (double)p[0] - q[0];
			e0 += d * d;
			w0 += 1;
			break;
		case KP_HD_LAYER_LIGHT + KP_HD_LIGHT_NORMAL:
			d = (double)p[0] - q[0];
			e0 += d * d;
			w0 += 1;
			d = (double)p[3] - q[3];
			e1 += d * d;
			break;
		case KP_HD_LAYER_LIGHT + KP_HD_LIGHT_AO:
			d = (double)p[1] - q[0];
			e0 += d * d;
			w0 += 1;
			break;
		default:
			for (c = 0; c < 3; c++) {
				d = (double)p[c] - q[c];
				e0 += d * d;
			}
			w0 += 3;
			break;
		}
	}
	out[0] = psnr(e0, w0);
	out[1] = psnr(e1, (double)pixels);
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
	r = (uint8_t *)malloc(pixels);
	g = (uint8_t *)malloc(pixels);
	if (!r || !g) {
		free(r);
		free(g);
		memset(rgba, 0, pixels * 4);
		return;
	}
	if (layer == KP_HD_LAYER_TEAM) {
		kp_eac_r11_decode(blocks, size, size, r);
		memcpy(g, r, pixels);
	} else {
		kp_eac_rg11_decode(blocks, size, size, r, g);
	}
	for (i = 0; i < pixels; i++) {
		rgba[4 * i] = layer == KP_HD_LAYER_LIGHT + KP_HD_LIGHT_AO ? g[i] : r[i];
		rgba[4 * i + 1] = g[i];
		rgba[4 * i + 2] = 0;
		rgba[4 * i + 3] = g[i];
	}
	free(r);
	free(g);
}

static int report(const char *in, int32_t page)
{
	static const char *const NAMES[KP_HD_LAYERS] = { "diffuse", "team", "normal", "occlusion", "specular", "emissive" };
	static const int BLOCKS[][2] = { { 4, 4 }, { 5, 5 }, { 6, 6 }, { 8, 8 } };
	static const float QUALITIES[] = { ASTCENC_PRE_MEDIUM, ASTCENC_PRE_THOROUGH };
	size_t size = 0;
	uint8_t *file = read_file(in, &size);
	KpHdPack p;
	KpError e;
	uint32_t offset[KP_HD_LAYERS], light[KP_HD_LIGHTS];
	int layer, rc = 1;
	if (!file) return fail(in, "cannot read");
	if (kp_hd_pack_open(&p, file, size, size, in, &e) != 0 || p.format != KP_HD_FORMAT_BC) {
		fail(in, "not a pack of BC pages");
	} else if (page < 0 || page >= p.page_count) {
		fail(in, "no such page");
	} else {
		size_t pixels = (size_t)p.page_size * (size_t)p.page_size;
		uint8_t *rgba = (uint8_t *)malloc(pixels * 4), *back = (uint8_t *)malloc(pixels * 4);
		uint8_t *blocks = (uint8_t *)malloc(pixels * 2);
		kp_hd_page_offsets(&p, page, &offset[KP_HD_LAYER_DIFFUSE], &offset[KP_HD_LAYER_TEAM], light);
		for (layer = KP_HD_LAYER_LIGHT; layer < KP_HD_LAYERS; layer++) offset[layer] = light[layer - KP_HD_LAYER_LIGHT];
		rc = rgba && back && blocks ? 0 : 1;
		for (layer = 0; layer < KP_HD_LAYERS && rc == 0; layer++) {
			double q[2], t;
			size_t k, j;
			if (offset[layer] == 0) continue; /* an unlit page's lighting */
			kp_bcn_decode(file + offset[layer], p.page_size, p.page_size, layer_is_bc3(layer), rgba);
			t = seconds();
			rc = etc2_layer(layer, file + offset[layer], p.page_size, blocks);
			if (rc != 0) break;
			t = seconds() - t;
			etc2_read_back(layer, blocks, p.page_size, back);
			layer_psnr(layer, rgba, back, pixels, q);
			printf("%s page %d %-9s etc2     %8zu bytes %7.1f s  %6.2f dB %6.2f dB\n", in, (int)page, NAMES[layer],
				kp_hd_layer_bytes(KP_HD_FORMAT_ETC2, layer, p.page_size), t, q[0], q[1]);
			for (k = 0; k < sizeof BLOCKS / sizeof BLOCKS[0] && rc == 0; k++) {
				size_t n = astc_bytes(p.page_size, BLOCKS[k][0], BLOCKS[k][1]);
				for (j = 0; j < sizeof QUALITIES / sizeof QUALITIES[0] && rc == 0; j++) {
					t = seconds();
					rc = astc_encode(layer, rgba, p.page_size, BLOCKS[k][0], BLOCKS[k][1], QUALITIES[j], blocks, n);
					if (rc != 0) break;
					t = seconds() - t;
					rc = astc_decode(layer, blocks, n, p.page_size, BLOCKS[k][0], BLOCKS[k][1], back);
					if (rc != 0) break;
					layer_psnr(layer, rgba, back, pixels, q);
					printf("%s page %d %-9s astc %dx%d %s %8zu bytes %7.1f s  %6.2f dB %6.2f dB\n", in, (int)page, NAMES[layer],
						BLOCKS[k][0], BLOCKS[k][1], j ? "thorough" : "medium", n, t, q[0], q[1]);
				}
			}
			fflush(stdout);
		}
		if (rc != 0) fail(in, "the encoder failed");
		free(rgba);
		free(back);
		free(blocks);
	}
	free(file);
	return rc;
}

/* A small page of each kind through the encoder and decoder: the channels the shader reads come back. */
static int selftest(void)
{
	enum { S = 64 };
	static uint8_t rgba[S * S * 4], back[S * S * 4], blocks[S * S * 2];
	int layer;
	for (layer = 0; layer < KP_HD_LAYERS; layer++) {
		double q[2];
		int k, block_w = 6, block_h = 6; /* the normal map and occlusion by the block --report tries */
		size_t n;
		for (k = 0; k < S * S; k++) {
			int x = k % S, y = k / S;
			rgba[4 * k] = (uint8_t)(x * 4);
			rgba[4 * k + 1] = (uint8_t)(y * 4);
			rgba[4 * k + 2] = (uint8_t)(x * 2 + y * 2);
			rgba[4 * k + 3] = (uint8_t)(255 - y * 4); /* a ramp: an independent channel */
		}
		if (astc_has_layer(layer)) kp_hd_layer_block(KP_HD_FORMAT_ASTC, layer, &block_w, &block_h);
		n = astc_bytes(S, block_w, block_h);
		if (astc_has_layer(layer) && n != kp_hd_layer_bytes(KP_HD_FORMAT_ASTC, layer, S)) {
			return fail("selftest", "a layer's bytes");
		}
		if (astc_encode(layer, rgba, S, block_w, block_h, ASTC_QUALITY, blocks, n) != 0
			|| astc_decode(layer, blocks, n, S, block_w, block_h, back) != 0) {
			return fail("selftest", "the encoder failed");
		}
		layer_psnr(layer, rgba, back, S * S, q);
		printf("selftest layer %d (%d x %d): %.2f dB, %.2f dB\n", layer, block_w, block_h, q[0], q[1]);
		if (q[0] < 35.0 || q[1] < 35.0) return fail("selftest", "a channel did not come back");
	}
	return 0;
}
#else
static int astc_layer(int layer, const uint8_t *bc, int32_t size, uint8_t *out)
{
	(void)layer, (void)bc, (void)size, (void)out;
	return fail("astc", "built without -DKP_ASTC=ON") ? -1 : 0;
}

static int report(const char *in, int32_t page)
{
	(void)page;
	return fail(in, "built without -DKP_ASTC=ON");
}

static int selftest(void) { return fail("selftest", "built without -DKP_ASTC=ON"); }
#endif

/* --- the files --- */

/* A pack's sections in another format; 0 done, else an error said. */
static int pack_sections(const KpHdPack *p, const uint8_t *file, int format, KpKpfOut *sections, uint8_t **buffers, int *count,
	const char *in)
{
	static const char *const SECTIONS[KP_HD_LAYERS] = { "DIFF", "TEAM", "NRML", "AODP", "SPEC", "EMIS" };
	int layer;
	for (layer = 0; layer < KP_HD_LAYERS; layer++) {
		int32_t pages = layer >= KP_HD_LAYER_LIGHT ? p->lit_pages : p->page_count, k;
		size_t to = kp_hd_layer_bytes(format, layer, p->page_size);
		if (pages == 0) continue;
		if (!(buffers[layer] = (uint8_t *)malloc(to * (size_t)pages))) return fail(in, "out of memory");
		for (k = 0; k < pages; k++) {
			uint32_t d, t, light[KP_HD_LIGHTS], offset;
			kp_hd_page_offsets(p, k, &d, &t, light);
			offset = layer == KP_HD_LAYER_DIFFUSE ? d : layer == KP_HD_LAYER_TEAM ? t : light[layer - KP_HD_LAYER_LIGHT];
			if ((format == KP_HD_FORMAT_ETC2 ? etc2_layer(layer, file + offset, p->page_size, buffers[layer] + to * (size_t)k)
				: astc_layer(layer, file + offset, p->page_size, buffers[layer] + to * (size_t)k)) != 0) {
				return fail(in, "cannot encode a page");
			}
		}
		sections[*count].fourcc = SECTIONS[layer];
		sections[*count].payload = buffers[layer];
		sections[*count].size = to * (size_t)pages;
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
			head[9] = (uint8_t)format;
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

int main(int argc, char **argv)
{
	if (argc == 2 && strcmp(argv[1], "--selftest") == 0) return selftest();
	if (argc == 4 && strcmp(argv[1], "--report") == 0) return report(argv[2], (int32_t)atoi(argv[3]));
	if (argc == 4 && strcmp(argv[1], "etc2") == 0) return transcode(KP_HD_FORMAT_ETC2, argv[2], argv[3]);
	if (argc == 4 && strcmp(argv[1], "astc") == 0) return transcode(KP_HD_FORMAT_ASTC, argv[2], argv[3]);
	fprintf(stderr, "usage: kp_hd_phone etc2|astc IN OUT | --report IN PAGE | --selftest\n");
	return 2;
}
