/*
 * kp_hd_etc2: an HD pack (HDPK), HD tileset (HDTS) or the water's normal maps (HDWA) of BC blocks written again in
 * ETC2 for phones (ADR 0013, stage R4f; tools/product.sh runs it when it stages the HD art for Android and iOS): each
 * page's BC3 diffuse and BC1 team mask decoded and encoded as ETC2 RGBA8 (EAC alpha) and EAC R11, each tile's and
 * normal map's BC1 as ETC2 RGB8 (kp_etc.c); the frames, the water masks and the header's other fields stay.
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
	FILE *f = fopen(path, "wb");
	int ok;
	if (!f) return -1;
	ok = fwrite(data, 1, size, f) == size;
	return fclose(f) == 0 && ok ? 0 : -1;
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
		kp_hd_page_offsets(p, k, &d, &t);
		if (kp_hd_page_decode(KP_HD_FORMAT_BC, file + d, file + t, p->page_size, rgba, mask) != 0) break;
		kp_etc2_encode(rgba, p->page_size, p->page_size, diffuse + KP_HD_PAGE_DIFFUSE_BYTES(p->page_size) * (size_t)k);
		kp_eac_r11_encode(mask, p->page_size, p->page_size, team + KP_HD_PAGE_TEAM_BYTES(p->page_size) * (size_t)k);
	}
	free(rgba);
	free(mask);
	return k == p->page_count ? 0 : -1;
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
	uint8_t *file = read_file(in, &size), *built = NULL, *a = NULL, *b = NULL, head[16];
	KpKpf c;
	KpError e;
	KpKpfOut sections[4];
	int count = 0, rc = 1;
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
		} else if (!(a = (uint8_t *)malloc(KP_HD_PAGE_DIFFUSE_BYTES(p.page_size) * (size_t)p.page_count + 1))
			|| !(b = (uint8_t *)malloc(KP_HD_PAGE_TEAM_BYTES(p.page_size) * (size_t)p.page_count + 1))
			|| pack_pages(&p, file, a, b) != 0) {
			fail(in, "out of memory");
		} else {
			memcpy(head, h->data, 10);
			head[9] = KP_HD_FORMAT_ETC2;
			sections[0].fourcc = "HEAD", sections[0].payload = head, sections[0].size = 10;
			sections[1].fourcc = "FRMS", sections[1].payload = f->data, sections[1].size = f->size;
			sections[2].fourcc = "DIFF", sections[2].payload = a;
			sections[2].size = KP_HD_PAGE_DIFFUSE_BYTES(p.page_size) * (size_t)p.page_count;
			sections[3].fourcc = "TEAM", sections[3].payload = b;
			sections[3].size = KP_HD_PAGE_TEAM_BYTES(p.page_size) * (size_t)p.page_count;
			count = 4;
		}
	} else if (strcmp(c.kind, KP_HD_TILESET_KIND) == 0) {
		KpHdTileset ts;
		const KpKpfSection *h = kp_kpf_find(&c, "HEAD"), *wmap = kp_kpf_find(&c, "WMAP"), *wmsk = kp_kpf_find(&c, "WMSK");
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
		int k;
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
