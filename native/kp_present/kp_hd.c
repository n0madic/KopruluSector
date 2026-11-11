/*
 * StarCraft: Remastered's HD containers (ADR 0013, stages R3 and R4a; converter/emit/hd.py): the HD packs (kind HDPK)
 * beside the SD packs, frame for frame, opened from their first bytes (their pages are read one at a time,
 * kp_hd_cache.c), their layers' codecs and blocks; the HD tilesets (kind HDTS) and the water's normal maps. What the
 * renderer makes of them is kp_hd_render.c's: kp_hd_phone, which transcodes the phones' packs, links this file and the
 * codecs alone (kp_bcn.c, kp_etc.c), and the phones' transcoding caches are stamped with its SHA-256 (tools/product.sh).
 */
#include "kp_present_internal.h"

#include <stdlib.h>
#include <string.h>

/* The codecs' blocks (KP_HD_CODEC_*): the side in texels, the bytes. */
static const struct {
	uint8_t side, bytes;
} HD_CODEC_BLOCKS[KP_HD_CODECS] = { { 4, 8 }, { 4, 16 }, { 4, 8 }, { 4, 16 }, { 4, 8 }, { 4, 16 }, { 4, 16 }, { 6, 16 },
	{ 8, 16 } };

/* Each pack format's layers' blocks. ASTC's by kp_hd_phone --report on HD2's pages (ADR 0013): the diffuse in 4 x 4, on
 * the grid of the BC3 blocks it comes from, at ETC2's bytes, its alpha weighted up towards EAC's (5 x 5 is below ETC2's
 * colour and leaves ten times the alpha on clear texels); the team masks, 0 and 255, in 8 x 8; the colours in 6 x 6. No
 * ASTC block reached EAC on the normal maps and the occlusion, so they stay in it. */
static const uint8_t HD_LAYER_CODECS[KP_HD_FORMATS][KP_HD_LAYERS] = {
	{ KP_HD_CODEC_BC3, KP_HD_CODEC_BC1, KP_HD_CODEC_BC3, KP_HD_CODEC_BC3, KP_HD_CODEC_BC1, KP_HD_CODEC_BC1 },
	{ KP_HD_CODEC_ETC2_RGBA8, KP_HD_CODEC_EAC_R11, KP_HD_CODEC_EAC_RG11, KP_HD_CODEC_EAC_R11, KP_HD_CODEC_ETC2_RGB8,
		KP_HD_CODEC_ETC2_RGB8 },
	{ KP_HD_CODEC_ASTC_4X4, KP_HD_CODEC_ASTC_8X8, KP_HD_CODEC_EAC_RG11, KP_HD_CODEC_EAC_R11, KP_HD_CODEC_ASTC_6X6,
		KP_HD_CODEC_ASTC_6X6 }
};

int kp_hd_layer_codec(int format, int layer) { return HD_LAYER_CODECS[format][layer]; }

void kp_hd_codec_block(int codec, int *side, int *bytes)
{
	*side = HD_CODEC_BLOCKS[codec].side;
	*bytes = HD_CODEC_BLOCKS[codec].bytes;
}

size_t kp_hd_layer_bytes(int format, int layer, int32_t page_size)
{
	int side, bytes;
	size_t blocks;
	kp_hd_codec_block(kp_hd_layer_codec(format, layer), &side, &bytes);
	blocks = (size_t)((page_size + side - 1) / side);
	return blocks * blocks * (size_t)bytes;
}

int kp_hd_pack_open(KpHdPack *p, const uint8_t *head_bytes, size_t n, size_t file_size, const char *name, KpError *err)
{
	static const char *const LIGHTS[KP_HD_LIGHTS] = { "NRML", "AODP", "SPEC", "EMIS" };
	KpKpf c;
	KpKpfExpect expect;
	const KpKpfSection *head, *frms, *diff, *team;
	uint32_t f;
	int rc = -1, k;
	memset(p, 0, sizeof *p);
	if (kp_kpf_parse_prefix(&c, head_bytes, n, file_size, name, err) != 0) return -1;
	memset(&expect, 0, sizeof expect);
	expect.kind = KP_HD_PACK_KIND;
	expect.schema_version = KP_HD_PACK_SCHEMA;
	if (kp_kpf_check(&c, &expect, err) != 0) goto done;
	if (!(head = kp_kpf_require(&c, "HEAD", err)) || !(frms = kp_kpf_require(&c, "FRMS", err))
		|| !(diff = kp_kpf_require(&c, "DIFF", err)) || !(team = kp_kpf_require(&c, "TEAM", err))) {
		goto done;
	}
	if (!head->data || !frms->data) {
		kp_present_fail(err, name, "HEAD or FRMS past the bytes read");
		goto done;
	}
	if (head->size < KP_HD_PACK_HEAD_BYTES) {
		kp_present_fail(err, name, "HEAD truncated");
		goto done;
	}
	p->scale = head->data[0];
	p->page_size = (int32_t)kp_le_u16(head->data + 1);
	p->page_count = (int32_t)kp_le_u16(head->data + 3);
	p->frame_count = kp_le_u32(head->data + 5);
	p->format = head->data[KP_HD_PACK_HEAD_FORMAT];
	p->lit_pages = (int32_t)kp_le_u16(head->data + 10);
	if ((p->scale != 2 && p->scale != 4) || p->page_size <= 0 || p->page_size % 4
		|| p->format < 0 || p->format >= KP_HD_FORMATS || p->lit_pages > p->page_count) {
		kp_present_fail(err, name, "HEAD invalid");
		goto done;
	}
	if ((uint64_t)p->frame_count * KP_HD_FRAME_RECORD != frms->size) {
		kp_present_fail(err, name, "FRMS size");
		goto done;
	}
	for (f = 0; f < p->frame_count; f++) { /* as the SD pack's (kp_pack_load); width 0: no HD frame */
		const uint8_t *r = frms->data + (size_t)f * KP_HD_FRAME_RECORD;
		int32_t w = (int32_t)kp_le_u16(r + 6), x = (int32_t)kp_le_u16(r + 2), y = (int32_t)kp_le_u16(r + 4);
		if ((w && (int32_t)kp_le_u16(r) >= p->page_count) || x + w > p->page_size
			|| y + (int32_t)kp_le_u16(r + 8) > p->page_size) {
			kp_present_fail(err, name, "a frame outside its page");
			goto done;
		}
	}
	if (diff->size != kp_hd_layer_bytes(p->format, KP_HD_LAYER_DIFFUSE, p->page_size) * (size_t)p->page_count
		|| team->size != kp_hd_layer_bytes(p->format, KP_HD_LAYER_TEAM, p->page_size) * (size_t)p->page_count) {
		kp_present_fail(err, name, "DIFF or TEAM size");
		goto done;
	}
	for (k = 0; k < KP_HD_LIGHTS && p->lit_pages > 0; k++) {
		const KpKpfSection *s = kp_kpf_require(&c, LIGHTS[k], err);
		if (!s) goto done;
		if (s->size != kp_hd_layer_bytes(p->format, KP_HD_LAYER_LIGHT + k, p->page_size) * (size_t)p->lit_pages) {
			kp_present_fail(err, name, "a lighting layer's size");
			goto done;
		}
		p->light_offset[k] = s->offset;
	}
	p->frames = frms->data;
	p->diffuse_offset = diff->offset;
	p->team_offset = team->offset;
	rc = 0;
done:
	kp_kpf_free(&c);
	return rc;
}

int kp_hd_light_to_etc2(int light, const uint8_t *bc, int32_t size, uint8_t *out)
{
	size_t pixels = (size_t)size * (size_t)size, i;
	uint8_t *rgba = (uint8_t *)malloc(pixels * 4), *r = NULL, *g = NULL;
	int rc = -1;
	if (!rgba) return -1;
	if (light == KP_HD_LIGHT_SPECULAR || light == KP_HD_LIGHT_EMISSIVE) {
		kp_bcn_decode(bc, size, size, 0, rgba);
		kp_etc2_rgb_encode(rgba, size, size, out);
		rc = 0;
	} else if ((r = (uint8_t *)malloc(pixels)) != NULL && (g = (uint8_t *)malloc(pixels)) != NULL) {
		kp_bcn_decode(bc, size, size, 1, rgba);
		for (i = 0; i < pixels; i++) {
			r[i] = rgba[4 * i + (light == KP_HD_LIGHT_NORMAL ? 0 : 1)]; /* x, or the occlusion's green */
			g[i] = rgba[4 * i + 3];                                      /* y */
		}
		if (light == KP_HD_LIGHT_NORMAL) {
			kp_eac_rg11_encode(r, g, size, size, out);
		} else {
			kp_eac_r11_encode(r, size, size, out);
		}
		rc = 0;
	}
	free(rgba);
	free(r);
	free(g);
	return rc;
}

void kp_hd_page_offsets(const KpHdPack *p, int32_t page, uint32_t *diffuse, uint32_t *team,
	uint32_t light[KP_HD_LIGHTS])
{
	int k;
	*diffuse = p->diffuse_offset + (uint32_t)(kp_hd_layer_bytes(p->format, KP_HD_LAYER_DIFFUSE, p->page_size) * (size_t)page);
	*team = p->team_offset + (uint32_t)(kp_hd_layer_bytes(p->format, KP_HD_LAYER_TEAM, p->page_size) * (size_t)page);
	for (k = 0; k < KP_HD_LIGHTS && light; k++) {
		light[k] = page < p->lit_pages
			? p->light_offset[k] + (uint32_t)(kp_hd_layer_bytes(p->format, KP_HD_LAYER_LIGHT + k, p->page_size) * (size_t)page) : 0;
	}
}

int kp_hd_tileset_load(KpHdTileset *t, const uint8_t *data, size_t size, const char *name, KpError *err)
{
	KpKpf c;
	KpKpfExpect expect;
	const KpKpfSection *head, *tile;
	int rc = -1;
	memset(t, 0, sizeof *t);
	if (kp_kpf_parse(&c, data, size, name, 0, err) != 0) return -1;
	memset(&expect, 0, sizeof expect);
	expect.kind = KP_HD_TILESET_KIND;
	expect.schema_version = KP_HD_SCHEMA;
	if (kp_kpf_check(&c, &expect, err) != 0) goto done;
	if (!(head = kp_kpf_require(&c, "HEAD", err)) || !(tile = kp_kpf_require(&c, "TILE", err))) goto done;
	if (head->size < 8 || (memcmp(head->data + 4, "DXT1", 4) != 0 && memcmp(head->data + 4, "ETC2", 4) != 0)) {
		kp_present_fail(err, name, "HEAD: BC1 or ETC2 tiles expected");
		goto done;
	}
	t->format = memcmp(head->data + 4, "ETC2", 4) == 0 ? KP_HD_FORMAT_ETC2 : KP_HD_FORMAT_BC;
	t->count = (int32_t)kp_le_u16(head->data);
	t->size = (int32_t)kp_le_u16(head->data + 2);
	if (t->count <= 0 || t->size <= 0 || t->size % 4 || tile->size != KP_HD_TILE_BC1(t->size) * (size_t)t->count) {
		kp_present_fail(err, name, "TILE size");
		goto done;
	}
	t->tiles = tile->data;
	{
		const KpKpfSection *wmap = kp_kpf_find(&c, "WMAP"), *wmsk = kp_kpf_find(&c, "WMSK");
		size_t mask = (size_t)t->size * (size_t)t->size;
		if (wmap && wmsk) {
			int32_t k;
			if (wmap->size != 2 * (size_t)t->count || wmsk->size % mask) {
				kp_present_fail(err, name, "WMAP or WMSK size");
				goto done;
			}
			t->water_mask_count = (int32_t)(wmsk->size / mask);
			for (k = 0; k < t->count; k++) {
				if (kp_le_u16(wmap->data + 2 * k) > (uint32_t)t->water_mask_count) {
					kp_present_fail(err, name, "WMAP names a mask past WMSK");
					goto done;
				}
			}
			t->water_map = wmap->data;
			t->water_masks = wmsk->data;
		}
	}
	{
		const KpKpfSection *edge = kp_kpf_find(&c, "EDGE");
		if (edge) {
			size_t texels = (size_t)t->size * (size_t)t->size * 4;
			if (edge->size < 2 || edge->size != 2 + texels * kp_le_u16(edge->data)) {
				kp_present_fail(err, name, "EDGE size");
				goto done;
			}
			t->edge_count = (int32_t)kp_le_u16(edge->data);
			t->edges = edge->data + 2;
		}
	}
	rc = 0;
done:
	kp_kpf_free(&c);
	return rc;
}

int kp_hd_water_load(KpHdWater *w, const uint8_t *data, size_t size, const char *name, KpError *err)
{
	KpKpf c;
	KpKpfExpect expect;
	const KpKpfSection *head, *sets[2];
	int rc = -1, k;
	memset(w, 0, sizeof *w);
	if (kp_kpf_parse(&c, data, size, name, 0, err) != 0) return -1;
	memset(&expect, 0, sizeof expect);
	expect.kind = KP_HD_WATER_KIND;
	expect.schema_version = KP_HD_SCHEMA;
	if (kp_kpf_check(&c, &expect, err) != 0) goto done;
	if (!(head = kp_kpf_require(&c, "HEAD", err)) || !(sets[0] = kp_kpf_require(&c, "NRMA", err))
		|| !(sets[1] = kp_kpf_require(&c, "NRMB", err))) {
		goto done;
	}
	if (head->size < 12 || (memcmp(head->data + 8, "DXT1", 4) != 0 && memcmp(head->data + 8, "ETC2", 4) != 0)) {
		kp_present_fail(err, name, "HEAD: BC1 or ETC2 normals expected");
		goto done;
	}
	w->format = memcmp(head->data + 8, "ETC2", 4) == 0 ? KP_HD_FORMAT_ETC2 : KP_HD_FORMAT_BC;
	for (k = 0; k < 2; k++) {
		w->frames[k] = (int32_t)kp_le_u16(head->data + 4 * k);
		w->size[k] = (int32_t)kp_le_u16(head->data + 4 * k + 2);
		if (w->frames[k] <= 0 || w->size[k] <= 0 || w->size[k] % 4
			|| sets[k]->size != KP_HD_TILE_BC1(w->size[k]) * (size_t)w->frames[k]) {
			kp_present_fail(err, name, "NRMA or NRMB size");
			goto done;
		}
		w->blocks[k] = sets[k]->data;
	}
	rc = 0;
done:
	kp_kpf_free(&c);
	return rc;
}
