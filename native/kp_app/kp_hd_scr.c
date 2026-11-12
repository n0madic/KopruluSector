/*
 * The HD world from a StarCraft: Remastered install (ADR 0019, stage C5; converter/emit/hd.py until then): which
 * image's anim draws each GRP (images.dat, SD/images.rel), a pack's anims read from their headers and frame records
 * (kp_anim.c lays the pack out), the HD tilesets (TileSet/<name>.dds.vr4, .tmsk, _mask.dds.grp, creep's edges in
 * .dds.grp) and the water's normal maps (effect/water_normal_1.dds.grp, _2) as their containers, HDTS and HDWA. The
 * runtime builds them when a mission starts from an install; kp_convert repack writes them, with each pack's HDPK
 * whole, into the repack's HD groups.
 */
#include "kp_hd_scr.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kp_grp.h"
#include "kp_scr.h"

#define MEGATILE_VF4 32      /* bytes of a megatile's flags in .vf4 */
#define DDS_HEADER 128
#define DDS_PF_FOURCC 0x4
#define BLOCK 4
#define TEAM_BLOCK 8          /* bytes of a BC1 block */
#define DIFFUSE_BLOCK 16      /* of a BC3 block */
#define WATER_SETS 2

/* The variants' prefixes in the install and their scale */
static const struct {
	const char *name, *prefix;
	int32_t scale;
} VARIANTS[KP_HD_SCR_VARIANTS] = { { "hd2", "HD2/", 2 }, { "hd", "", 4 } };

/* The tilesets' file names in TileSet/, by KP_TILESET_NAMES' order */
static const char *const TILESET_FILES[KP_TILESETS] = { "badlands", "platform", "install", "AshWorld", "Jungle", "Desert",
	"Ice", "Twilight" };

/* A variant's sets in a repack by their names' suffixes: its pages as BC, as ASTC, as ETC2 */
static const struct {
	const char *suffix;
	int format;
} SETS[] = { { "", KP_HD_FORMAT_BC }, { "-astc", KP_HD_FORMAT_ASTC }, { "-etc2", KP_HD_FORMAT_ETC2 } };

const char *kp_hd_scr_variant_name(int variant) { return VARIANTS[variant].name; }
int32_t kp_hd_scr_variant_scale(int variant) { return VARIANTS[variant].scale; }
const char *kp_hd_scr_variant_prefix(int variant) { return VARIANTS[variant].prefix; }

int kp_hd_scr_variant_of(const char *name)
{
	int v;
	for (v = 0; v < KP_HD_SCR_VARIANTS; v++) {
		if (strcmp(name, VARIANTS[v].name) == 0) return v;
	}
	return -1;
}

const char *kp_hd_scr_set_suffix(int k, int *format)
{
	if (k < 0 || k >= (int)(sizeof SETS / sizeof SETS[0])) return NULL;
	if (format) *format = SETS[k].format;
	return SETS[k].suffix;
}

int kp_hd_scr_set(const char *set, int *variant, int *format)
{
	const char *suffix;
	int v, k;
	for (v = 0; v < KP_HD_SCR_VARIANTS; v++) {
		size_t n = strlen(VARIANTS[v].name);
		if (strncmp(set, VARIANTS[v].name, n) != 0) continue;
		for (k = 0; (suffix = kp_hd_scr_set_suffix(k, format)) != NULL; k++) {
			if (strcmp(set + n, suffix) == 0) {
				*variant = v;
				return 0;
			}
		}
	}
	return -1;
}

static int fail(KpError *err, const char *what, const char *why)
{
	snprintf(err->msg, sizeof err->msg, "hd: %s: %s", what, why);
	return -1;
}

int kp_hd_scr_anim_name(int variant, int32_t image, char *out, size_t size)
{
	int n = snprintf(out, size, "%sanim/main_%03d.anim", VARIANTS[variant].prefix, (int)image);
	return n > 0 && (size_t)n < size ? 0 : -1;
}

int kp_hd_scr_held(KpRes *r, int variant)
{
	char name[64];
	kp_hd_scr_anim_name(variant, 0, name, sizeof name);
	return kp_res_install(r) == KP_RES_SOURCE_CASC && kp_res_raw_held(r, name) && kp_res_raw_held(r, KP_HD_SCR_RELATIONS);
}

int kp_hd_scr_owners(KpRes *r, const KpGrpMeta *g, const KpDat *dat, int variant, int32_t **owner_of, KpError *err)
{
	const KpDatColumn *grp_file = kp_dat_column(dat, "images", "grp_file");
	char why[256], name[64];
	uint8_t *rel;
	size_t size;
	int32_t *related = NULL, *out;
	uint32_t count = 0, i;
	*owner_of = NULL;
	if (!grp_file) return fail(err, "images.dat", "no grp_file column");
	if (!(rel = kp_res_raw_read(r, KP_HD_SCR_RELATIONS, &size, why, sizeof why))) {
		return fail(err, KP_HD_SCR_RELATIONS, why[0] ? why : "not in the data");
	}
	if (kp_anim_relations(rel, size, &related, &count, err) != 0) {
		free(rel);
		return -1;
	}
	free(rel);
	if (count != (uint32_t)grp_file->count) {
		snprintf(err->msg, sizeof err->msg, "hd: images.rel has %u entries, images.dat %d", (unsigned)count, (int)grp_file->count);
		free(related);
		return -1;
	}
	if (!(out = (int32_t *)malloc(sizeof *out * ((size_t)g->grp_count + 1)))) {
		free(related);
		return fail(err, "owners", "out of memory");
	}
	for (i = 0; i <= g->grp_count; i++) out[i] = -1;
	/* each entry's first image whose own anim, else whose related image's, the variant has */
	for (i = 0; i < count; i++) {
		uint32_t ref = kp_dat_value(grp_file, (uint32_t)grp_file->first_id * grp_file->per_id + i);
		uint32_t entry = kp_grpmeta_grp_of_tbl(g, ref);
		int32_t owner = (int32_t)i;
		if (!entry || out[entry] >= 0) continue;
		kp_hd_scr_anim_name(variant, owner, name, sizeof name);
		if (!kp_res_raw_held(r, name)) owner = related[i];
		if (owner < 0) continue;
		kp_hd_scr_anim_name(variant, owner, name, sizeof name);
		if (kp_res_raw_held(r, name)) out[entry] = owner;
	}
	free(related);
	*owner_of = out;
	return 0;
}

/* Image's anim read through h from its head and frame records into a (kept when read before). */
static int read_anim(KpRes *r, KpResReader *h, int variant, int32_t image, KpAnim *a, KpError *err)
{
	char name[64];
	uint8_t head[KP_ANIM_HEAD_READ], *records;
	size_t n;
	uint64_t total;
	KpResPlace p;
	if (a->frames) return 0;
	kp_hd_scr_anim_name(variant, image, name, sizeof name);
	if (!kp_res_raw_place(r, name, &p) || kp_res_reader_size(h, &p, &total) != 0) return fail(err, name, "not in the data");
	n = total < sizeof head ? (size_t)total : sizeof head;
	if (kp_res_reader_read(h, &p, 0, head, n) != 0) return fail(err, name, "cannot be read");
	if (kp_anim_open(a, head, n, total, name, err) != 0) return -1;
	n = (size_t)KP_ANIM_FRAME_RECORD * a->frame_count;
	if (!(records = (uint8_t *)malloc(n + 1))) return fail(err, name, "out of memory");
	if (kp_res_reader_read(h, &p, a->frames_at, records, n) != 0) {
		free(records);
		kp_anim_free(a);
		return fail(err, name, "cannot be read");
	}
	if (kp_anim_frames(a, records, n, name, err) != 0) {
		free(records);
		kp_anim_free(a);
		return -1;
	}
	free(records);
	return 0;
}

int kp_hd_scr_sheets_read(void *ctx, int32_t image, uint64_t offset, void *out, size_t size)
{
	KpHdScrSheets *s = (KpHdScrSheets *)ctx;
	KpResPlace p = s->install;
	if (kp_hd_scr_anim_name(s->variant, image, p.name, sizeof p.name) != 0) return -1;
	return kp_res_reader_read(s->reader, &p, offset, out, size);
}

int kp_hd_scr_layout(KpRes *r, KpResReader *h, const KpGrpMeta *g, const int32_t *owner_of, int variant, const char *pack,
	KpAnim *anims, int32_t image_count, KpHdLayout *out, KpError *err)
{
	uint32_t a, e;
	for (a = 1; a <= g->atlas_count; a++) {
		if (g->atlases[a].pack.size != strlen(pack) || memcmp(g->atlases[a].pack.text, pack, g->atlases[a].pack.size) != 0) continue;
		for (e = 1; e <= g->grp_count; e++) {
			int32_t o = owner_of[e];
			if (g->grps[e].atlas != a || o < 0) continue;
			if (o >= image_count) return fail(err, pack, "an image past images.dat");
			if (read_anim(r, h, variant, o, &anims[o], err) != 0) return -1;
		}
	}
	return kp_hd_layout_build(out, g, pack, owner_of, anims, image_count, VARIANTS[variant].scale, KP_HD_PAGE_SIZE, err);
}

/* --- the tilesets and the water --- */

/* A DDS file's first level of BC1 or BC3 blocks: its FourCC and blocks, 0; -1 with err. */
static int dds_blocks(const KpScrDdsEntry *d, const char *what, char fourcc[5], const uint8_t **blocks, size_t *n, int32_t *w,
	int32_t *h, KpError *err)
{
	size_t step;
	if (d->size < DDS_HEADER || memcmp(d->dds, "DDS ", 4) != 0) return fail(err, what, "not a DDS file");
	if (!(kp_le_u32(d->dds + 80) & DDS_PF_FOURCC) || (memcmp(d->dds + 84, "DXT1", 4) != 0 && memcmp(d->dds + 84, "DXT5", 4) != 0)) {
		return fail(err, what, "an HD sheet in other blocks than BC1 or BC3");
	}
	memcpy(fourcc, d->dds + 84, 4);
	fourcc[4] = 0;
	*h = (int32_t)kp_le_u32(d->dds + 12);
	*w = (int32_t)kp_le_u32(d->dds + 16);
	step = fourcc[3] == '1' ? TEAM_BLOCK : DIFFUSE_BLOCK;
	*n = (size_t)((*w + BLOCK - 1) / BLOCK) * (size_t)((*h + BLOCK - 1) / BLOCK) * step;
	if (d->size - DDS_HEADER < *n) return fail(err, what, "HD sheet truncated");
	*blocks = d->dds + DDS_HEADER;
	return 0;
}

/* A container of DDS images read from the install, its entries; NULL with err. */
static uint8_t *dds_file(KpRes *r, const char *name, KpScrDdsEntry **entries, uint32_t *count, KpError *err)
{
	char why[256];
	size_t size;
	uint8_t *data = kp_res_raw_read(r, name, &size, why, sizeof why);
	if (!data) {
		fail(err, name, why[0] ? why : "not in the data");
		return NULL;
	}
	if (kp_scr_dds_container(data, size, entries, count) != 0) {
		free(data);
		fail(err, name, "not a DDS container");
		return NULL;
	}
	return data;
}

/* The red channel (a byte a texel, rows top down) of w x h BC1 blocks, as the Python converter reckoned it. */
static void bc1_red(const uint8_t *body, int32_t w, int32_t h, uint8_t *out)
{
	int32_t columns = w / BLOCK, b, i;
	for (b = 0; b < columns * (h / BLOCK); b++) {
		const uint8_t *p = body + (size_t)b * TEAM_BLOCK;
		uint32_t c0 = kp_le_u16(p), c1 = kp_le_u16(p + 2), bits = kp_le_u32(p + 4);
		int32_t r0 = (int32_t)(c0 >> 11) * 255 / 31, r1 = (int32_t)(c1 >> 11) * 255 / 31, reds[4];
		int32_t x0 = (b % columns) * BLOCK, y0 = (b / columns) * BLOCK;
		reds[0] = r0;
		reds[1] = r1;
		if (c0 > c1) {
			reds[2] = (2 * r0 + r1) / 3;
			reds[3] = (r0 + 2 * r1) / 3;
		} else {
			reds[2] = (r0 + r1) / 2;
			reds[3] = 0;
		}
		for (i = 0; i < 16; i++) out[(size_t)(y0 + i / 4) * (size_t)w + (size_t)(x0 + i % 4)] = (uint8_t)reds[bits >> (2 * i) & 3];
	}
}

typedef struct {
	uint8_t *data;
	size_t size, cap;
} Buf;
#define BUF_EMPTY { NULL, 0, 0 }

static int put(Buf *b, const void *data, size_t n)
{
	if (b->size + n > b->cap) {
		size_t cap = b->cap ? b->cap : 1 << 16;
		uint8_t *grown;
		while (cap < b->size + n) cap *= 2;
		if (!(grown = (uint8_t *)realloc(b->data, cap))) return -1;
		b->data = grown;
		b->cap = cap;
	}
	if (n) memcpy(b->data + b->size, data, n);
	b->size += n;
	return 0;
}

/* WMAP and WMSK of a tileset with water: 1 made, 0 the tileset has none, -1 with err. */
static int water_masks(KpRes *r, int variant, int tileset, uint32_t megatiles, int32_t size, Buf *wmap, Buf *wmsk, KpError *err)
{
	char name[96], mask_name[96], why[256];
	uint8_t *tmsk, *masks_file = NULL, *red = NULL;
	size_t n;
	uint16_t *map = NULL;
	uint32_t count, k, entries_n = 0, kept = 0;
	KpScrDdsEntry *entries = NULL;
	int rc = -1;
	snprintf(name, sizeof name, "%sTileSet/%s.tmsk", VARIANTS[variant].prefix, TILESET_FILES[tileset]);
	if (!kp_res_raw_held(r, name)) return 0;
	if (!(tmsk = kp_res_raw_read(r, name, &n, why, sizeof why))) return fail(err, name, why[0] ? why : "not in the data");
	if (n < 8 || memcmp(tmsk, "KSMT", 4) != 0) {
		fail(err, name, "not a KSMT file");
		goto done;
	}
	count = kp_le_u16(tmsk + 6);
	if (8 + 4 * (size_t)count > n) {
		fail(err, name, "truncated");
		goto done;
	}
	snprintf(mask_name, sizeof mask_name, "%sTileSet/%s_mask.dds.grp", VARIANTS[variant].prefix, TILESET_FILES[tileset]);
	if (!(masks_file = dds_file(r, mask_name, &entries, &entries_n, err))) goto done;
	if (!(map = (uint16_t *)calloc(megatiles ? megatiles : 1, sizeof *map))
		|| !(red = (uint8_t *)malloc((size_t)size * (size_t)size))) {
		fail(err, name, "out of memory");
		goto done;
	}
	for (k = 0; k < count; k++) {
		uint32_t megatile = kp_le_u16(tmsk + 8 + 4 * k), frame = kp_le_u16(tmsk + 10 + 4 * k), j;
		char fourcc[5];
		const uint8_t *body;
		size_t bytes, i;
		int32_t w, h;
		if (megatile >= megatiles) continue;
		if (frame >= entries_n) {
			fail(err, mask_name, "a frame past its images");
			goto done;
		}
		if ((int32_t)entries[frame].width != size || (int32_t)entries[frame].height != size) {
			fail(err, mask_name, "a water mask of another size than the tiles");
			goto done;
		}
		if (dds_blocks(&entries[frame], mask_name, fourcc, &body, &bytes, &w, &h, err) != 0) goto done;
		if (strcmp(fourcc, "DXT1") != 0) {
			fail(err, mask_name, "a water mask in other blocks than BC1");
			goto done;
		}
		bc1_red(body, size, size, red);
		for (i = 0; i < (size_t)size * (size_t)size && !red[i]; i++) {}
		if (i == (size_t)size * (size_t)size) continue; /* dry */
		for (j = 0; j < kept && memcmp(wmsk->data + (size_t)j * (size_t)size * (size_t)size, red, (size_t)size * (size_t)size) != 0; j++) {}
		if (j == kept) {
			if (put(wmsk, red, (size_t)size * (size_t)size) != 0) {
				fail(err, name, "out of memory");
				goto done;
			}
			kept++;
		}
		map[megatile] = (uint16_t)(1 + j);
	}
	for (k = 0; k < megatiles; k++) {
		uint8_t le[2];
		kp_le_put16(le, map[k]);
		if (put(wmap, le, 2) != 0) {
			fail(err, name, "out of memory");
			goto done;
		}
	}
	rc = 1;
done:
	free(tmsk);
	free(masks_file);
	free(entries);
	free(map);
	free(red);
	return rc;
}

/* EDGE: creep's edge frames in tiles of size squared RGBA8 texels at the SD frames' offsets. */
static int creep_edges(KpRes *r, int variant, int tileset, int32_t size, Buf *edge, KpError *err)
{
	char name[96], sd_name[64], why[256];
	uint8_t *sd = NULL, *hd = NULL, *out = NULL, le[2];
	size_t sd_size, tile = (size_t)size * (size_t)size * 4;
	uint32_t frames, width, height, count = 0, k;
	KpScrDdsEntry *entries = NULL;
	int32_t scale = size / 32;
	int rc = -1;
	snprintf(sd_name, sizeof sd_name, "tileset\\%s.grp", KP_TILESET_NAMES[tileset]);
	if (!(sd = kp_res_read(r, sd_name, &sd_size, NULL, why, sizeof why))) return fail(err, sd_name, why[0] ? why : "not in the data");
	if (kp_grp_header(sd, sd_size, &frames, &width, &height) != 0) {
		fail(err, sd_name, "not a GRP");
		goto done;
	}
	snprintf(name, sizeof name, "%sTileSet/%s.dds.grp", VARIANTS[variant].prefix, TILESET_FILES[tileset]);
	if (!(hd = dds_file(r, name, &entries, &count, err))) goto done;
	if (count != frames) {
		fail(err, name, "another count of creep's edges than the GRP's frames");
		goto done;
	}
	if (frames > 0xffff || !(out = (uint8_t *)calloc(frames ? frames : 1, tile))) {
		fail(err, name, "out of memory");
		goto done;
	}
	for (k = 0; k < frames; k++) {
		KpGrpFrame f;
		KpScrImage im;
		int32_t x0, y0, y;
		kp_grp_frame(sd, k, &f);
		if (kp_scr_dds(entries[k].dds, entries[k].size, &im, why, sizeof why) != 0) {
			fail(err, name, why);
			goto done;
		}
		if ((int32_t)im.width > size || (int32_t)im.height > size) {
			kp_scr_image_free(&im);
			fail(err, name, "a creep edge larger than the tiles");
			goto done;
		}
		x0 = (int32_t)f.x * scale;
		y0 = (int32_t)f.y * scale;
		if (x0 > size - (int32_t)im.width) x0 = size - (int32_t)im.width;
		if (y0 > size - (int32_t)im.height) y0 = size - (int32_t)im.height;
		for (y = 0; y < (int32_t)im.height; y++) {
			memcpy(out + (size_t)k * tile + ((size_t)(y0 + y) * (size_t)size + (size_t)x0) * 4, im.rgba + (size_t)y * im.width * 4,
				(size_t)im.width * 4);
		}
		kp_scr_image_free(&im);
	}
	kp_le_put16(le, frames);
	if (put(edge, le, 2) != 0 || put(edge, out, (size_t)frames * tile) != 0) {
		fail(err, name, "out of memory");
		goto done;
	}
	rc = 0;
done:
	free(sd);
	free(hd);
	free(entries);
	free(out);
	return rc;
}

uint8_t *kp_hd_scr_tileset(KpRes *r, int variant, int tileset, size_t *size_out, KpError *err)
{
	char name[96], vf4_name[64], why[256], fourcc[5] = "";
	uint8_t *vf4 = NULL, *vr4 = NULL, *out = NULL, head[8];
	size_t vf4_size;
	uint32_t megatiles, count = 0, k;
	int32_t size = 0;
	KpScrDdsEntry *entries = NULL;
	Buf tile = BUF_EMPTY, wmap = BUF_EMPTY, wmsk = BUF_EMPTY, edge = BUF_EMPTY;
	KpKpfOut sections[5];
	int n = 0, wet;
	*size_out = 0;
	snprintf(vf4_name, sizeof vf4_name, "tileset\\%s.vf4", KP_TILESET_NAMES[tileset]);
	if (!(vf4 = kp_res_read(r, vf4_name, &vf4_size, NULL, why, sizeof why))) {
		fail(err, vf4_name, why[0] ? why : "not in the data");
		goto done;
	}
	megatiles = (uint32_t)(vf4_size / MEGATILE_VF4);
	snprintf(name, sizeof name, "%sTileSet/%s.dds.vr4", VARIANTS[variant].prefix, TILESET_FILES[tileset]);
	if (!(vr4 = dds_file(r, name, &entries, &count, err))) goto done;
	if (count < megatiles || megatiles == 0 || megatiles > 0xffff) {
		fail(err, name, "fewer HD tiles than megatiles");
		goto done;
	}
	size = (int32_t)entries[0].width;
	for (k = 0; k < megatiles; k++) {
		const uint8_t *body;
		size_t bytes;
		int32_t w, h;
		if ((int32_t)entries[k].width != size || (int32_t)entries[k].height != size) {
			fail(err, name, "tiles of more than one size");
			goto done;
		}
		if (dds_blocks(&entries[k], name, fourcc, &body, &bytes, &w, &h, err) != 0) goto done;
		if (put(&tile, body, bytes) != 0) {
			fail(err, name, "out of memory");
			goto done;
		}
	}
	kp_le_put16(head, megatiles);
	kp_le_put16(head + 2, (uint32_t)size);
	memcpy(head + 4, fourcc, 4);
	if ((wet = water_masks(r, variant, tileset, megatiles, size, &wmap, &wmsk, err)) < 0
		|| creep_edges(r, variant, tileset, size, &edge, err) != 0) {
		goto done;
	}
	sections[n].fourcc = "HEAD", sections[n].payload = head, sections[n].size = sizeof head, sections[n++].schema_version = 0;
	sections[n].fourcc = "TILE", sections[n].payload = tile.data, sections[n].size = tile.size, sections[n++].schema_version = 0;
	if (wet) {
		sections[n].fourcc = "WMAP", sections[n].payload = wmap.data, sections[n].size = wmap.size, sections[n++].schema_version = 0;
		sections[n].fourcc = "WMSK", sections[n].payload = wmsk.data, sections[n].size = wmsk.size, sections[n++].schema_version = 0;
	}
	sections[n].fourcc = "EDGE", sections[n].payload = edge.data, sections[n].size = edge.size, sections[n++].schema_version = 0;
	if (kp_kpf_build(KP_HD_TILESET_KIND, KP_HD_SCHEMA, 0, kp_res_profile(r)->id, NULL, sections, n, &out,
			size_out) != 0) {
		fail(err, name, "out of memory");
		out = NULL;
	}
done:
	free(vf4);
	free(vr4);
	free(entries);
	free(tile.data);
	free(wmap.data);
	free(wmsk.data);
	free(edge.data);
	return out;
}

uint8_t *kp_hd_scr_water(KpRes *r, int variant, size_t *size_out, KpError *err)
{
	Buf sets[WATER_SETS] = { BUF_EMPTY, BUF_EMPTY };
	uint8_t head[12], *out = NULL;
	int k;
	*size_out = 0;
	for (k = 0; k < WATER_SETS; k++) {
		char name[96], fourcc[5];
		KpScrDdsEntry *entries = NULL;
		uint32_t count = 0, j;
		uint8_t *file;
		int32_t size;
		snprintf(name, sizeof name, "%seffect/water_normal_%d.dds.grp", VARIANTS[variant].prefix, k + 1);
		if (!(file = dds_file(r, name, &entries, &count, err))) goto done;
		size = count ? (int32_t)entries[0].width : 0;
		for (j = 0; j < count; j++) {
			const uint8_t *body;
			size_t bytes;
			int32_t w, h;
			if (dds_blocks(&entries[j], name, fourcc, &body, &bytes, &w, &h, err) != 0
				|| (int32_t)entries[j].width != size || (int32_t)entries[j].height != size || strcmp(fourcc, "DXT1") != 0) {
				if (!err->msg[0] || strstr(err->msg, name) == NULL) fail(err, name, "a frame of another size or other blocks");
				free(file);
				free(entries);
				goto done;
			}
			if (put(&sets[k], body, bytes) != 0) {
				fail(err, name, "out of memory");
				free(file);
				free(entries);
				goto done;
			}
		}
		if (count == 0 || count > 0xffff) {
			fail(err, name, "no frames");
			free(file);
			free(entries);
			goto done;
		}
		kp_le_put16(head + 4 * k, count);
		kp_le_put16(head + 4 * k + 2, (uint32_t)size);
		free(file);
		free(entries);
	}
	memcpy(head + 8, "DXT1", 4);
	{
		KpKpfOut sections[3] = { { "HEAD", head, sizeof head, 0 }, { "NRMA", NULL, 0, 0 }, { "NRMB", NULL, 0, 0 } };
		sections[1].payload = sets[0].data;
		sections[1].size = sets[0].size;
		sections[2].payload = sets[1].data;
		sections[2].size = sets[1].size;
		if (kp_kpf_build(KP_HD_WATER_KIND, KP_HD_SCHEMA, 0, kp_res_profile(r)->id, NULL, sections, 3, &out,
				size_out) != 0) {
			fail(err, "water", "out of memory");
			out = NULL;
		}
	}
done:
	free(sets[0].data);
	free(sets[1].data);
	return out;
}
