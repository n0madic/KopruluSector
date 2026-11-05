/*
 * kp_hd_etc2: an HD pack (HDPK), HD tileset (HDTS) or the water's normal maps (HDWA) of BC blocks written again in
 * ETC2 for phones (ADR 0013, stage R4f; tools/product.sh runs it when it stages the HD art for Android and iOS): each
 * page's BC3 diffuse and BC1 team mask decoded and encoded as ETC2 RGBA8 (EAC alpha) and EAC R11, a lit page's lighting
 * layers as EAC RG11 and ETC2 RGB8 (kp_hd_light_to_etc2), each tile's and normal map's BC1 as ETC2 RGB8 (kp_etc.c); the
 * frames, the water masks, creep's edges (RGBA8) and the header's other fields stay. The output is written beside its
 * path and renamed into it once whole, so an interrupted run leaves no partial file that tools/product.sh would take
 * for current.
 *
 * TODO: ASTC (the phones that have it: smaller pages at ETC2's quality), beside ETC2.
 *
 *   kp_hd_etc2 IN OUT
 */
#include "kp_present.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
	fprintf(stderr, "kp_hd_etc2: %s: %s\n", what, msg);
	return 1;
}

/* The pages of an HD pack: diffuse and team blocks transcoded into the out buffers (the same sizes). */
static int pack_pages(const KpHdPack *p, const uint8_t *file, uint8_t *diffuse, uint8_t *team)
{
	size_t pixels = (size_t)p->page_size * (size_t)p->page_size;
	uint8_t *rgba = (uint8_t *)malloc(pixels * 4), *mask = (uint8_t *)malloc(pixels);
	int32_t k;
	if (!rgba || !mask) {
		free(rgba);
		free(mask);
		return -1;
	}
	for (k = 0; k < p->page_count; k++) {
		uint32_t d, t;
		kp_hd_page_offsets(p, k, &d, &t, NULL);
		if (kp_hd_page_decode(KP_HD_FORMAT_BC, file + d, file + t, p->page_size, rgba, mask) != 0) break;
		kp_etc2_encode(rgba, p->page_size, p->page_size, diffuse + kp_hd_layer_bytes(KP_HD_FORMAT_ETC2, KP_HD_LAYER_DIFFUSE, p->page_size) * (size_t)k);
		kp_eac_r11_encode(mask, p->page_size, p->page_size, team + kp_hd_layer_bytes(KP_HD_FORMAT_ETC2, KP_HD_LAYER_TEAM, p->page_size) * (size_t)k);
	}
	free(rgba);
	free(mask);
	return k == p->page_count ? 0 : -1;
}

/* A lit page's lighting layers, each section's blocks transcoded into its out buffer. */
static int pack_lights(const KpHdPack *p, const uint8_t *file, uint8_t *const out[KP_HD_LIGHTS])
{
	int32_t k;
	int layer;
	for (layer = 0; layer < KP_HD_LIGHTS; layer++) {
		size_t from = kp_hd_layer_bytes(KP_HD_FORMAT_BC, KP_HD_LAYER_LIGHT + layer, p->page_size);
		size_t to = kp_hd_layer_bytes(KP_HD_FORMAT_ETC2, KP_HD_LAYER_LIGHT + layer, p->page_size);
		for (k = 0; k < p->lit_pages; k++) {
			if (kp_hd_light_to_etc2(layer, file + p->light_offset[layer] + from * (size_t)k, p->page_size,
				out[layer] + to * (size_t)k) != 0) {
				return -1;
			}
		}
	}
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

static int transcode(const char *in, const char *out)
{
	size_t size = 0, out_size = 0;
	uint8_t *file = read_file(in, &size), *built = NULL, *a = NULL, *b = NULL, *lights[KP_HD_LIGHTS] = { NULL }, head[16];
	KpKpf c;
	KpError e;
	KpKpfOut sections[8];
	int count = 0, rc = 1, lit_ok = 1, k;
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
		} else if (!(a = (uint8_t *)malloc(kp_hd_layer_bytes(KP_HD_FORMAT_ETC2, KP_HD_LAYER_DIFFUSE, p.page_size) * (size_t)p.page_count + 1))
			|| !(b = (uint8_t *)malloc(kp_hd_layer_bytes(KP_HD_FORMAT_ETC2, KP_HD_LAYER_TEAM, p.page_size) * (size_t)p.page_count + 1))
			|| pack_pages(&p, file, a, b) != 0) {
			fail(in, "out of memory");
		} else {
			memcpy(head, h->data, KP_HD_PACK_HEAD_BYTES);
			head[9] = KP_HD_FORMAT_ETC2;
			sections[0].fourcc = "HEAD", sections[0].payload = head, sections[0].size = KP_HD_PACK_HEAD_BYTES;
			sections[1].fourcc = "FRMS", sections[1].payload = f->data, sections[1].size = f->size;
			sections[2].fourcc = "DIFF", sections[2].payload = a;
			sections[2].size = kp_hd_layer_bytes(KP_HD_FORMAT_ETC2, KP_HD_LAYER_DIFFUSE, p.page_size) * (size_t)p.page_count;
			sections[3].fourcc = "TEAM", sections[3].payload = b;
			sections[3].size = kp_hd_layer_bytes(KP_HD_FORMAT_ETC2, KP_HD_LAYER_TEAM, p.page_size) * (size_t)p.page_count;
			count = 4;
			for (k = 0; k < KP_HD_LIGHTS && p.lit_pages > 0; k++) {
				static const char *const LIGHTS[KP_HD_LIGHTS] = { "NRML", "AODP", "SPEC", "EMIS" };
				size_t bytes = kp_hd_layer_bytes(KP_HD_FORMAT_ETC2, KP_HD_LAYER_LIGHT + k, p.page_size) * (size_t)p.lit_pages;
				lit_ok = lit_ok && (lights[k] = (uint8_t *)malloc(bytes)) != NULL;
				sections[count].fourcc = LIGHTS[k], sections[count].payload = lights[k], sections[count].size = bytes;
				count++;
			}
			if (!lit_ok || (p.lit_pages > 0 && pack_lights(&p, file, lights) != 0)) {
				fail(in, "out of memory");
				count = 0;
			}
		}
	} else if (strcmp(c.kind, KP_HD_TILESET_KIND) == 0) {
		KpHdTileset ts;
		const KpKpfSection *h = kp_kpf_find(&c, "HEAD"), *wmap = kp_kpf_find(&c, "WMAP"), *wmsk = kp_kpf_find(&c, "WMSK");
		const KpKpfSection *edge = kp_kpf_find(&c, "EDGE");
		if (kp_hd_tileset_load(&ts, file, size, in, &e) != 0) {
			fail(in, e.msg);
		} else if (ts.format != KP_HD_FORMAT_BC) {
			fail(in, "not BC tiles");
		} else if (!(a = (uint8_t *)malloc((size_t)ts.size * (size_t)ts.size / 2 * (size_t)ts.count + 1))
			|| bc1_to_etc2(ts.tiles, ts.count, ts.size, a) != 0) {
			fail(in, "out of memory");
		} else {
			memcpy(head, h->data, 8);
			memcpy(head + 4, "ETC2", 4);
			sections[0].fourcc = "HEAD", sections[0].payload = head, sections[0].size = 8;
			sections[1].fourcc = "TILE", sections[1].payload = a;
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
		} else if (!(a = (uint8_t *)malloc((na = (size_t)w.size[0] * (size_t)w.size[0] / 2 * (size_t)w.frames[0]) + 1))
			|| !(b = (uint8_t *)malloc((nb = (size_t)w.size[1] * (size_t)w.size[1] / 2 * (size_t)w.frames[1]) + 1))
			|| bc1_to_etc2(w.blocks[0], w.frames[0], w.size[0], a) != 0
			|| bc1_to_etc2(w.blocks[1], w.frames[1], w.size[1], b) != 0) {
			fail(in, "out of memory");
		} else {
			memcpy(head, h->data, 12);
			memcpy(head + 8, "ETC2", 4);
			sections[0].fourcc = "HEAD", sections[0].payload = head, sections[0].size = 12;
			sections[1].fourcc = "NRMA", sections[1].payload = a, sections[1].size = na;
			sections[2].fourcc = "NRMB", sections[2].payload = b, sections[2].size = nb;
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
	free(a);
	free(b);
	for (k = 0; k < KP_HD_LIGHTS; k++) free(lights[k]);
	kp_kpf_free(&c);
	free(file);
	return rc;
}

int main(int argc, char **argv)
{
	if (argc != 3) {
		fprintf(stderr, "usage: kp_hd_etc2 IN OUT\n");
		return 2;
	}
	return transcode(argv[1], argv[2]);
}
