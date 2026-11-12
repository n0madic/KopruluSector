#include "kp_hd_transcode.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kp_work.h"

#ifdef KP_ASTC
#include "astcenc.h"
#endif

static uint8_t *fail(KpError *err, const char *msg)
{
	snprintf(err->msg, sizeof err->msg, "HD transcoding: %s", msg);
	return NULL;
}

int kp_hd_transcode_bc3(int layer) { return kp_hd_layer_codec(KP_HD_FORMAT_BC, layer) == KP_HD_CODEC_BC3; }

uint32_t kp_hd_transcode_offset(const KpHdPack *p, int32_t page, int layer)
{
	uint32_t d, t, light[KP_HD_LIGHTS];
	kp_hd_page_offsets(p, page, &d, &t, light);
	return layer == KP_HD_LAYER_DIFFUSE ? d : layer == KP_HD_LAYER_TEAM ? t : light[layer - KP_HD_LAYER_LIGHT];
}

/* --- ETC2 and EAC --- */

int kp_hd_transcode_etc2_layer(int layer, const uint8_t *bc, int32_t size, uint8_t *out)
{
	size_t pixels = (size_t)size * (size_t)size, i;
	uint8_t *rgba;
	if (layer >= KP_HD_LAYER_LIGHT) return kp_hd_light_to_etc2(layer - KP_HD_LAYER_LIGHT, bc, size, out);
	if (!(rgba = (uint8_t *)malloc(pixels * 4))) return -1;
	kp_bcn_decode(bc, size, size, kp_hd_transcode_bc3(layer), rgba);
	if (layer == KP_HD_LAYER_DIFFUSE) {
		kp_etc2_encode(rgba, size, size, out);
	} else { /* the team mask's red, in place */
		for (i = 0; i < pixels; i++) rgba[i] = rgba[4 * i];
		kp_eac_r11_encode(rgba, size, size, out);
	}
	free(rgba);
	return 0;
}

int kp_hd_transcode_bc1_etc2(const uint8_t *in, int32_t count, int32_t size, uint8_t *out)
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

struct KpHdAstc {
	struct astcenc_context *context;
	struct astcenc_swizzle swizzle;
};

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

KpHdAstc *kp_hd_astc_open(int layer, int side, float quality)
{
	struct astcenc_config config;
	unsigned flags;
	KpHdAstc *c = (KpHdAstc *)calloc(1, sizeof *c);
	if (!c) return NULL;
	astc_input(layer, &c->swizzle, &flags);
	if (astcenc_config_init(ASTCENC_PRF_LDR, (unsigned)side, (unsigned)side, 1, quality, flags, &config) != ASTCENC_SUCCESS) {
		free(c);
		return NULL;
	}
	if (layer == KP_HD_LAYER_DIFFUSE) config.cw_a_weight = ASTC_DIFFUSE_ALPHA_WEIGHT;
	if (astcenc_context_alloc(&config, 1, &c->context, NULL) != ASTCENC_SUCCESS) {
		free(c);
		return NULL;
	}
	return c;
}

void kp_hd_astc_close(KpHdAstc *c)
{
	if (!c) return;
	if (c->context) astcenc_context_free(c->context);
	free(c);
}

static void astc_image(struct astcenc_image *image, void **plane, uint8_t *rgba, int32_t size)
{
	plane[0] = rgba;
	image->dim_x = image->dim_y = (unsigned)size;
	image->dim_z = 1;
	image->data_type = ASTCENC_TYPE_U8;
	image->data = plane;
}

int kp_hd_astc_encode(KpHdAstc *c, uint8_t *rgba, int32_t size, uint8_t *out, size_t out_size)
{
	struct astcenc_image image;
	void *plane[1];
	astc_image(&image, plane, rgba, size);
	return astcenc_compress_image(c->context, &image, &c->swizzle, out, out_size, 0) == ASTCENC_SUCCESS ? 0 : -1;
}

int kp_hd_astc_decode(KpHdAstc *c, const uint8_t *blocks, size_t blocks_size, int32_t size, uint8_t *rgba)
{
	static const struct astcenc_swizzle IDENTITY = { ASTCENC_SWZ_R, ASTCENC_SWZ_G, ASTCENC_SWZ_B, ASTCENC_SWZ_A };
	struct astcenc_image image;
	void *plane[1];
	astc_image(&image, plane, rgba, size);
	return astcenc_decompress_image(c->context, blocks, blocks_size, &image, &IDENTITY, 0) == ASTCENC_SUCCESS ? 0 : -1;
}

size_t kp_hd_astc_bytes(int32_t size, int side)
{
	return (size_t)((size + side - 1) / side) * (size_t)((size + side - 1) / side) * 16;
}

int kp_hd_transcode_astc(void) { return 1; }
#else
int kp_hd_transcode_astc(void) { return 0; }
#endif

/* --- a pack's layers --- */

/* A layer's pages, encoded by `threads` workers: worker t takes pages t, t + threads, ... */
typedef struct {
	const KpHdPack *p;
	const uint8_t *file;
	int format, layer, threads;
	size_t page_bytes;
	uint8_t *out;
	int32_t pages;
	int failed[64];
} LayerJob;

static void layer_worker(void *ctx, int t)
{
	LayerJob *j = (LayerJob *)ctx;
	int codec = kp_hd_layer_codec(j->format, j->layer), rc = 0;
	int32_t page, size = j->p->page_size;
	/* EAC's and ETC2's codec is etc2_layer's by the layer: a layer in another of them is a table's error (kp_hd_phone's
	 * selftest) */
	int astc = codec >= KP_HD_CODEC_ASTC_4X4;
#ifdef KP_ASTC
	KpHdAstc *c = NULL;
	uint8_t *rgba = NULL;
	if (astc) {
		int side, bytes;
		kp_hd_codec_block(codec, &side, &bytes);
		rgba = (uint8_t *)malloc((size_t)size * (size_t)size * 4);
		rc = rgba && (c = kp_hd_astc_open(j->layer, side, ASTC_QUALITY)) != NULL ? 0 : -1;
	}
#else
	rc = astc ? -1 : 0; /* built without the encoder */
#endif
	for (page = t; page < j->pages && rc == 0; page += j->threads) {
		const uint8_t *bc = j->file + kp_hd_transcode_offset(j->p, page, j->layer);
		uint8_t *out = j->out + j->page_bytes * (size_t)page;
#ifdef KP_ASTC
		if (astc) {
			kp_bcn_decode(bc, size, size, kp_hd_transcode_bc3(j->layer), rgba);
			rc = kp_hd_astc_encode(c, rgba, size, out, j->page_bytes);
			continue;
		}
#endif
		rc = kp_hd_transcode_etc2_layer(j->layer, bc, size, out);
	}
#ifdef KP_ASTC
	kp_hd_astc_close(c);
	free(rgba);
#endif
	j->failed[t] = rc != 0;
}

/* A pack's sections in another format (a pack without lit pages has no lighting's, one without pages DIFF and TEAM
 * empty, as kp_hd_pack_open asks): 0, or -1 with err. */
static int pack_sections(const KpHdPack *p, const uint8_t *file, int format, int threads, KpKpfOut *sections,
	uint8_t **buffers, int *count, KpError *err)
{
	static const char *const SECTIONS[KP_HD_LAYERS] = { "DIFF", "TEAM", "NRML", "AODP", "SPEC", "EMIS" };
	int layer, t;
	for (layer = 0; layer < KP_HD_LAYERS; layer++) {
		LayerJob j;
		memset(&j, 0, sizeof j);
		j.p = p;
		j.file = file;
		j.format = format;
		j.layer = layer;
		j.page_bytes = kp_hd_layer_bytes(format, layer, p->page_size);
		j.pages = layer >= KP_HD_LAYER_LIGHT ? p->lit_pages : p->page_count;
		j.threads = threads < 1 ? 1 : threads > (int)(sizeof j.failed / sizeof j.failed[0]) ? (int)(sizeof j.failed / sizeof j.failed[0]) : threads;
		if (j.threads > j.pages) j.threads = j.pages > 0 ? j.pages : 1;
		if (layer >= KP_HD_LAYER_LIGHT && j.pages == 0) continue;
		if (!(buffers[layer] = (uint8_t *)malloc(j.page_bytes * (size_t)j.pages + 1))) {
			fail(err, "out of memory");
			return -1;
		}
		j.out = buffers[layer];
		kp_work_run(layer_worker, &j, j.threads, j.threads);
		for (t = 0; t < j.threads; t++) {
			if (j.failed[t]) {
				fail(err, kp_hd_layer_codec(format, layer) >= KP_HD_CODEC_ASTC_4X4 && !kp_hd_transcode_astc()
					? "ASTC needs the encoder (built with -DKP_ASTC=ON)" : "a page does not encode");
				return -1;
			}
		}
		sections[*count].fourcc = SECTIONS[layer];
		sections[*count].payload = buffers[layer];
		sections[*count].size = j.page_bytes * (size_t)j.pages;
		(*count)++;
	}
	return 0;
}

uint8_t *kp_hd_transcode(const uint8_t *file, size_t size, int format, int threads, size_t *out_size, KpError *err)
{
	uint8_t *built = NULL, *buffers[KP_HD_LAYERS] = { NULL }, head[16];
	KpKpf c;
	KpError e;
	KpKpfOut sections[KP_HD_LAYERS + 2];
	int count = 0, k;
	*out_size = 0;
	if (format != KP_HD_FORMAT_ETC2 && format != KP_HD_FORMAT_ASTC) return fail(err, "neither ETC2 nor ASTC");
	if (kp_kpf_parse(&c, file, size, "HD container", 1, &e) != 0) return fail(err, e.msg);
	if (strcmp(c.kind, KP_HD_PACK_KIND) == 0) {
		KpHdPack p;
		const KpKpfSection *h = kp_kpf_find(&c, "HEAD"), *f = kp_kpf_find(&c, "FRMS");
		if (kp_hd_pack_open(&p, file, size, size, "HD pack", &e) != 0) {
			fail(err, e.msg);
		} else if (p.format != KP_HD_FORMAT_BC) {
			fail(err, "not BC pages");
		} else {
			memcpy(head, h->data, KP_HD_PACK_HEAD_BYTES);
			head[KP_HD_PACK_HEAD_FORMAT] = (uint8_t)format;
			sections[0].fourcc = "HEAD", sections[0].payload = head, sections[0].size = KP_HD_PACK_HEAD_BYTES;
			sections[1].fourcc = "FRMS", sections[1].payload = f->data, sections[1].size = f->size;
			count = 2;
			if (pack_sections(&p, file, format, threads, sections, buffers, &count, err) != 0) count = 0;
		}
	} else if (strcmp(c.kind, KP_HD_TILESET_KIND) == 0) { /* the tiles in ETC2 for either format */
		KpHdTileset ts;
		const KpKpfSection *h = kp_kpf_find(&c, "HEAD"), *wmap = kp_kpf_find(&c, "WMAP"), *wmsk = kp_kpf_find(&c, "WMSK");
		const KpKpfSection *edge = kp_kpf_find(&c, "EDGE");
		size_t n;
		if (kp_hd_tileset_load(&ts, file, size, "HD tileset", &e) != 0) {
			fail(err, e.msg);
		} else if (ts.format != KP_HD_FORMAT_BC) {
			fail(err, "not BC tiles");
		} else if (!(buffers[0] = (uint8_t *)malloc((n = (size_t)ts.size * (size_t)ts.size / 2 * (size_t)ts.count) + 1))
			|| kp_hd_transcode_bc1_etc2(ts.tiles, ts.count, ts.size, buffers[0]) != 0) {
			fail(err, "out of memory");
		} else {
			memcpy(head, h->data, 8);
			memcpy(head + 4, "ETC2", 4);
			sections[0].fourcc = "HEAD", sections[0].payload = head, sections[0].size = 8;
			sections[1].fourcc = "TILE", sections[1].payload = buffers[0], sections[1].size = n;
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
	} else if (strcmp(c.kind, KP_HD_WATER_KIND) == 0) { /* the normal maps in ETC2 for either format */
		KpHdWater w;
		const KpKpfSection *h = kp_kpf_find(&c, "HEAD");
		size_t na = 0, nb = 0;
		if (kp_hd_water_load(&w, file, size, "HD water", &e) != 0) {
			fail(err, e.msg);
		} else if (w.format != KP_HD_FORMAT_BC) {
			fail(err, "not BC normals");
		} else if (!(buffers[0] = (uint8_t *)malloc((na = (size_t)w.size[0] * (size_t)w.size[0] / 2 * (size_t)w.frames[0]) + 1))
			|| !(buffers[1] = (uint8_t *)malloc((nb = (size_t)w.size[1] * (size_t)w.size[1] / 2 * (size_t)w.frames[1]) + 1))
			|| kp_hd_transcode_bc1_etc2(w.blocks[0], w.frames[0], w.size[0], buffers[0]) != 0
			|| kp_hd_transcode_bc1_etc2(w.blocks[1], w.frames[1], w.size[1], buffers[1]) != 0) {
			fail(err, "out of memory");
		} else {
			memcpy(head, h->data, 12);
			memcpy(head + 8, "ETC2", 4);
			sections[0].fourcc = "HEAD", sections[0].payload = head, sections[0].size = 12;
			sections[1].fourcc = "NRMA", sections[1].payload = buffers[0], sections[1].size = na;
			sections[2].fourcc = "NRMB", sections[2].payload = buffers[1], sections[2].size = nb;
			count = 3;
		}
	} else {
		fail(err, "neither an HD pack, an HD tileset nor the water's normals");
	}
	if (count > 0) {
		for (k = 0; k < count; k++) sections[k].schema_version = 0;
		if (kp_kpf_build(c.kind, c.schema_version, c.converter_version, c.profile_id, c.source_hash, sections, count,
				&built, out_size) != 0) {
			built = fail(err, "cannot lay out the container");
		}
	}
	for (k = 0; k < KP_HD_LAYERS; k++) free(buffers[k]);
	kp_kpf_free(&c);
	return built;
}
