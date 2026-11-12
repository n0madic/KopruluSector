/* The HD world from StarCraft: Remastered's own files (ADR 0019, stage C5): the HD packs' packer, an anim's head and
 * frames, a page's blocks laid out from the anims' sheets, and over the install KP_TEST_SCR names (skipped without it)
 * every pack's FRMS and pages, the tilesets and the water's normal maps equal to what the Python converter wrote before
 * stage C5 deleted it (tests/golden/hd/converter.json: their sections' SHA-256); with KP_TEST_HD_REPACK
 * the same of a repack's HD sets (kp_convert repack --hd hd2,hd: the packs opened from their heads in the core). */
#include "kp_test_app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kp_data.h"
#include "kp_grpmeta.h"
#include "kp_hd_scr.h"
#include "kp_json.h"
#include "kp_mission.h"
#include "kp_present.h"
#include "kp_sha256.h"

#define SCR "KP_TEST_SCR"
#define REPACK "KP_TEST_HD_REPACK" /* a repack whose BC sets (hd2, hd) are compared */
#define CONVERTER "tests/golden/hd/converter.json" /* converter hd's output, recorded before stage C5 */
#define PACK_SHARDS 12

/* --- the packer --- */

static void packer_keeps_a_group_together(KpT *T)
{
	/* in blocks: [[2 x 2, 1 x 1], [4 x 3], [1 x 1]] on pages of 4 x 4 */
	static const uint16_t SIZES[] = { 2, 2, 1, 1, 4, 3, 1, 1 };
	static const uint32_t FIRST[] = { 0, 2, 3, 4 };
	static const uint16_t BIG[] = { 8, 1 };
	static const uint32_t ONE[] = { 0, 1 };
	KpPagePlace at[4];
	KpError e;
	int pages = 0;
	kp_t_eq(T, kp_pages_pack_groups(SIZES, FIRST, 3, 4, KP_HD_OPEN_PAGES, at, &pages, &e), 0, "packed");
	kp_t_eq(T, pages, 2, "two pages");
	kp_t_ok(T, at[0].page == 0 && at[0].x == 0 && at[0].y == 0 && at[1].page == 0 && at[1].x == 2 && at[1].y == 0,
		"the first group side by side");
	kp_t_ok(T, at[2].page == 1 && at[2].x == 0 && at[2].y == 0, "no room left on the first page's floor: a new page");
	kp_t_ok(T, at[3].page == 0 && at[3].x == 3 && at[3].y == 0, "an earlier page still open takes the small one");
	kp_t_eq(T, kp_pages_pack_groups(BIG, ONE, 1, 4, KP_HD_OPEN_PAGES, at, &pages, &e), -1, "larger than a page");
	/* the second group's offset into sizes: out[k - first[0]] */
	kp_t_eq(T, kp_pages_pack_groups(SIZES, FIRST + 2, 1, 4, KP_HD_OPEN_PAGES, at, &pages, &e), 0, "a later group alone");
	kp_t_ok(T, pages == 1 && at[0].page == 0 && at[0].x == 0 && at[0].y == 0, "its place in out[0]");
}

static void packer_fills_only_the_open_pages(KpT *T)
{
	/* six 4 x 4 pages filled whole, then a small one: only the last open_pages (2 here) are looked at */
	uint16_t sizes[14];
	uint32_t first[8];
	KpPagePlace at[7];
	KpError e;
	int pages = 0, k;
	for (k = 0; k < 7; k++) {
		sizes[2 * k] = k == 6 ? 1 : 4;
		sizes[2 * k + 1] = k == 6 ? 1 : k == 0 ? 3 : 4; /* the first page keeps a row free */
		first[k] = (uint32_t)k;
	}
	first[7] = 7;
	kp_t_eq(T, kp_pages_pack_groups(sizes, first, 7, 4, 2, at, &pages, &e), 0, "packed");
	kp_t_eq(T, pages, 7, "the first page's free row is closed by then: a page of its own");
	kp_t_ok(T, at[6].page == 6, "on page %d", (int)at[6].page);
}

/* --- an anim --- */

/* An anim of one diffuse layer: frames' records (in HD pixels) at its end, its sheet's DDS before them. */
static uint8_t *anim_file(int scale, const int16_t (*records)[6], int frames, size_t *size)
{
	size_t head = KP_ANIM_HEAD_READ, sheet = 128 + 16, n = head + sheet + (size_t)KP_ANIM_FRAME_RECORD * (size_t)frames;
	uint8_t *b = (uint8_t *)calloc(1, n);
	int f;
	memcpy(b, "ANIM", 4);
	b[4] = (uint8_t)scale;
	b[5] = 2;
	kp_le_put16(b + 8, 1);
	kp_le_put16(b + 10, 1);
	memcpy(b + 12, "diffuse", 7);
	kp_le_put16(b + 332, (uint32_t)frames);
	kp_le_put16(b + 334, 0xffff);
	kp_le_put16(b + 336, 128);
	kp_le_put16(b + 338, 96);
	kp_le_put32(b + 340, (uint32_t)(head + sheet));
	kp_le_put32(b + 344, (uint32_t)head);
	kp_le_put32(b + 348, (uint32_t)sheet);
	for (f = 0; f < frames; f++) {
		uint8_t *r = b + head + sheet + (size_t)KP_ANIM_FRAME_RECORD * (size_t)f;
		int k;
		for (k = 0; k < 6; k++) kp_le_put16(r + 2 * k, (uint32_t)(uint16_t)records[f][k]);
		kp_le_put32(r + 12, 1);
	}
	*size = n;
	return b;
}

static void anim_head_and_frames(KpT *T)
{
	static const int16_t RECORDS[2][6] = { { 8, 4, -6, 10, 40, 36 }, { 0, 0, 5, -1, 0, 0 } };
	size_t n;
	uint8_t *file = anim_file(2, RECORDS, 2, &n);
	KpAnim a;
	KpError e;
	kp_t_eq(T, kp_anim_open(&a, file, KP_ANIM_HEAD_READ, n, "a", &e), 0, "opened");
	kp_t_ok(T, a.scale == 2 && a.frame_count == 2 && a.box_w == 64 && a.box_h == 48, "HD2: the box halved");
	kp_t_ok(T, a.layer_offset[KP_HD_LAYER_DIFFUSE] == KP_ANIM_HEAD_READ && a.layer_offset[KP_HD_LAYER_TEAM] == 0,
		"the diffuse sheet; no team colour");
	kp_t_ok(T, !kp_anim_lit(&a), "not lit");
	kp_t_eq(T, kp_anim_frames(&a, file + a.frames_at, 2 * KP_ANIM_FRAME_RECORD, "a", &e), 0, "its frames");
	kp_t_ok(T, a.frames[0] == 4 && a.frames[1] == 2 && a.frames[2] == -3 && a.frames[3] == 5 && a.frames[4] == 20
		&& a.frames[5] == 18, "halved");
	kp_t_ok(T, a.frames[8] == 2 && a.frames[9] == -1, "a negative offset floored, as Python's // does: %d, %d",
		(int)a.frames[8], (int)a.frames[9]);
	kp_anim_free(&a);
	kp_t_eq(T, kp_anim_open(&a, file, KP_ANIM_HEAD_READ, n - 1, "a", &e), -1, "frame records past the end");
	file[5] = 1;
	kp_t_eq(T, kp_anim_open(&a, file, KP_ANIM_HEAD_READ, n, "a", &e), -1, "the SD table's type");
	free(file);
}

static void relations_of_images(KpT *T)
{
	uint8_t rel[16];
	int32_t *owners;
	uint32_t count;
	KpError e;
	kp_le_put32(rel, 0);
	kp_le_put32(rel + 4, 0xffffffffu);
	kp_le_put32(rel + 8, 1);
	kp_le_put32(rel + 12, 7);
	kp_t_eq(T, kp_anim_relations(rel, sizeof rel, &owners, &count, &e), 0, "read");
	kp_t_ok(T, count == 2 && owners[0] == -1 && owners[1] == 7, "its own, then image 7's");
	free(owners);
	kp_t_eq(T, kp_anim_relations(rel, 12, &owners, &count, &e), -1, "not whole records");
}

/* --- a page's blocks --- */

/* Two images' sheets of 8 x 4 texels (2 x 1 blocks), each layer's blocks filled with one byte. */
typedef struct {
	uint8_t files[2][4096];
} Sheets;

static int sheets_read(void *ctx, int32_t image, uint64_t offset, void *out, size_t size)
{
	Sheets *s = (Sheets *)ctx;
	if (image < 0 || image > 1 || offset + size > sizeof s->files[0]) return -1;
	memcpy(out, s->files[image] + offset, size);
	return 0;
}

/* A DDS sheet of fill at offset (not 0: no layer) of image's file: its layers' offset and size. */
static void sheet(Sheets *s, KpHdLayers *l, int32_t image, int layer, uint32_t offset, const char *fourcc, uint8_t fill)
{
	uint8_t *d = s->files[image] + offset;
	size_t blocks = (size_t)2 * (fourcc[3] == '5' ? 16 : 8);
	memcpy(d, "DDS ", 4);
	kp_le_put32(d + 12, 4);
	kp_le_put32(d + 16, 8);
	kp_le_put32(d + 80, 4);
	memcpy(d + 84, fourcc, 4);
	memset(d + 128, fill, blocks);
	l->offset[layer] = offset;
	l->size[layer] = (uint32_t)(128 + blocks);
}

static void page_keeps_the_lit_pages_layers(KpT *T)
{
	static Sheets s;
	KpHdLayers layers[2];
	KpHdPiece pieces[2];
	int32_t page_first[3] = { 0, 1, 2 }, images[2] = { 0, 1 };
	KpHdLayout l;
	uint8_t diffuse[16 * 16], team[16 * 16 / 2], nrml[16 * 16], aodp[16 * 16], spec[16 * 16 / 2], emis[16 * 16 / 2];
	uint8_t *light[KP_HD_LIGHTS] = { nrml, aodp, spec, emis };
	KpError e;
	memset(&s, 0, sizeof s);
	memset(layers, 0, sizeof layers);
	sheet(&s, &layers[0], 0, KP_HD_LAYER_DIFFUSE, 64, "DXT5", 1);
	sheet(&s, &layers[0], 0, KP_HD_LAYER_TEAM, 576, "DXT1", 2);
	sheet(&s, &layers[0], 0, KP_HD_LAYER_LIGHT + KP_HD_LIGHT_NORMAL, 1088, "DXT5", 3);
	sheet(&s, &layers[0], 0, KP_HD_LAYER_LIGHT + KP_HD_LIGHT_AO, 1600, "DXT5", 4);
	sheet(&s, &layers[0], 0, KP_HD_LAYER_LIGHT + KP_HD_LIGHT_SPECULAR, 2112, "DXT1", 5); /* no emissive: zero */
	sheet(&s, &layers[1], 1, KP_HD_LAYER_DIFFUSE, 64, "DXT5", 6);
	pieces[0].owner = 0, pieces[0].sx = 0, pieces[0].sy = 0, pieces[0].w = 8, pieces[0].h = 4, pieces[0].x = 8, pieces[0].y = 0;
	pieces[1].owner = 1, pieces[1].sx = 0, pieces[1].sy = 0, pieces[1].w = 8, pieces[1].h = 4, pieces[1].x = 0, pieces[1].y = 4;
	memset(&l, 0, sizeof l);
	l.scale = 2, l.page_size = 16, l.page_count = 2, l.lit_pages = 1, l.pieces = pieces, l.page_first = page_first;
	l.image_count = 2, l.images = images, l.layers = layers;
	memset(nrml, 0xee, sizeof nrml);
	kp_t_eq(T, kp_hd_layout_page(&l, 0, sheets_read, &s, diffuse, team, light, &e), 0, "the lit page: %s", e.msg);
	kp_t_ok(T, diffuse[2 * 16] == 1 && diffuse[3 * 16 + 15] == 1 && diffuse[0] == 0, "the diffuse at (8, 0)");
	kp_t_ok(T, team[2 * 8] == 2 && nrml[3 * 16] == 3 && aodp[2 * 16] == 4 && spec[2 * 8] == 5, "each layer's blocks");
	kp_t_ok(T, nrml[0] == 0, "zero outside the image");
	{
		size_t k, any = 0;
		for (k = 0; k < sizeof emis; k++) any |= emis[k];
		kp_t_ok(T, !any, "no emissive sheet: zero");
	}
	kp_t_eq(T, kp_hd_layout_page(&l, 1, sheets_read, &s, diffuse, team, NULL, &e), 0, "the unlit page");
	kp_t_ok(T, diffuse[4 * 16] == 6 && diffuse[4 * 16 + 31] == 6 && diffuse[2 * 16] == 0, "the diffuse at (0, 4)");
	kp_t_eq(T, kp_hd_layout_page(&l, 2, sheets_read, &s, diffuse, team, NULL, &e), -1, "no such page");
	sheet(&s, &layers[0], 0, KP_HD_LAYER_LIGHT + KP_HD_LIGHT_NORMAL, 1088, "DXT1", 3);
	kp_t_eq(T, kp_hd_layout_page(&l, 0, sheets_read, &s, diffuse, team, light, &e), -1, "a normal map in BC1: %s", e.msg);
}

/* --- over the install, against the converter (KP_TEST_SCR) --- */

typedef struct {
	KpRes *res;
	KpData data;
	KpGrpMeta grp;
} Scr;

static void free_scr(void *p)
{
	Scr *s = (Scr *)p;
	kp_grpmeta_free(&s->grp);
	kp_data_free(&s->data);
	free(s);
}

/* The install's data and grpmeta; skips without it. */
static Scr *scr(KpT *T)
{
	Scr *s = (Scr *)calloc(1, sizeof *s);
	KpError e;
	if (!s) kp_t_fail(T, "out of memory");
	s->res = kp_t_install(T, SCR);
	if (kp_data_build(&s->data, s->res, NULL, 0, NULL, 0, &e) != 0) {
		free(s);
		kp_t_fail(T, "the install's data: %s", e.msg);
	}
	if (kp_grpmeta_from_files(&s->grp, &s->data.files, &s->data.dat, &e) != 0) {
		kp_data_free(&s->data);
		free(s);
		kp_t_fail(T, "grpmeta: %s", e.msg);
	}
	kp_t_defer(T, free_scr, s);
	return s;
}

static void sha_hex(const void *data, size_t size, char hex[2 * KP_SHA256_SIZE + 1])
{
	uint8_t digest[KP_SHA256_SIZE];
	kp_sha256(data, size, digest);
	kp_sha256_hex(digest, hex);
}

static void same_sha(KpT *T, const void *data, size_t size, const KpJson *golden, const char *what)
{
	char hex[2 * KP_SHA256_SIZE + 1];
	sha_hex(data, size, hex);
	kp_t_eq_str(T, hex, kp_json_str(golden, "(none)"), "%s", what);
}

static void free_reader(void *p) { kp_res_reader_free((KpResReader *)p); }
static void free_ptr(void *p) { free(p); }

/* Each pack of the golden's variants (a shard's), its owners, HEAD, FRMS and each page's layers. */
static void packs_equal_the_converters(KpT *T)
{
	static const char *const SECTIONS[KP_HD_LAYERS] = { "DIFF", "TEAM", "NRML", "AODP", "SPEC", "EMIS" };
	const KpJson *golden;
	Scr *s;
	int variant, job = 0;
	if (!getenv(SCR)) kp_t_skip(T, "%s not set", SCR);
	golden = kp_t_golden(T, CONVERTER);
	s = scr(T);
	for (variant = 0; variant < KP_HD_SCR_VARIANTS; variant++) {
		const KpJson *v = kp_json_get(golden, kp_hd_scr_variant_name(variant)), *packs = kp_json_get(v, "packs");
		const KpDatColumn *grp_file = kp_dat_column(&s->data.dat, "images", "grp_file");
		int32_t *owner_of = NULL, images = grp_file ? (int32_t)grp_file->count : 0;
		KpAnim *anims;
		KpResReader *h;
		KpHdScrSheets sheets;
		KpError e;
		uint32_t entry, held = 0;
		int k;
		kp_t_ok(T, v != NULL && kp_json_len(packs) > 0, "the golden's %s", kp_hd_scr_variant_name(variant));
		if (!kp_hd_scr_held(s->res, variant)) kp_t_skip(T, "the install holds no %s anims", kp_hd_scr_variant_name(variant));
		kp_t_eq(T, kp_hd_scr_owners(s->res, &s->grp, &s->data.dat, variant, &owner_of, &e), 0, "owners: %s", e.msg);
		kp_t_defer(T, free_ptr, owner_of);
		for (entry = 1; entry <= s->grp.grp_count; entry++) { /* the golden's GRP entries count from 0 */
			char key[16];
			snprintf(key, sizeof key, "%u", (unsigned)(entry - 1));
			kp_t_eq(T, owner_of[entry], kp_json_int(kp_json_get(kp_json_get(v, "owners"), key), -1), "entry %u's image",
				(unsigned)entry);
			held += owner_of[entry] >= 0;
		}
		kp_t_eq(T, (int64_t)held, kp_json_len(kp_json_get(v, "owners")), "the GRPs with HD frames");
		if (!(anims = (KpAnim *)calloc((size_t)(images ? images : 1), sizeof *anims)) || !(h = kp_res_reader_new())) {
			kp_t_fail(T, "out of memory");
		}
		kp_t_defer(T, free_ptr, anims);
		kp_t_defer(T, free_reader, h);
		sheets.reader = h;
		sheets.variant = variant;
		kp_t_ok(T, kp_res_raw_place(s->res, KP_HD_SCR_RELATIONS, &sheets.install) && sheets.install.kind == KP_RES_SOURCE_CASC,
			"the install's place");
		for (k = 0; k < kp_json_len(packs); k++) { /* the golden's packs (keys in document order) */
			const char *pack = packs->keys[k];
			const KpJson *p = packs->items[k], *pages;
			KpHdLayout l;
			uint8_t head[KP_HD_PACK_HEAD_BYTES], *layer[KP_HD_LAYERS];
			int32_t page;
			int j;
			if (!kp_t_mine(T, job++)) continue;
			kp_t_log(T, "%s %s", kp_hd_scr_variant_name(variant), pack);
			if (kp_hd_scr_layout(s->res, h, &s->grp, owner_of, variant, pack, anims, images, &l, &e) != 0) {
				kp_t_fail(T, "%s %s: %s", kp_hd_scr_variant_name(variant), pack, e.msg);
			}
			kp_t_eq(T, (int64_t)l.frame_count, kp_json_field_int(p, "frames", -1), "%s's frames", pack);
			kp_t_eq(T, l.page_count, kp_json_field_int(p, "pages", -1), "%s's pages", pack);
			kp_t_eq(T, l.lit_pages, kp_json_field_int(p, "lit_pages", -1), "%s's lit pages", pack);
			head[0] = (uint8_t)l.scale;
			kp_le_put16(head + 1, (uint32_t)l.page_size);
			kp_le_put16(head + 3, (uint32_t)l.page_count);
			kp_le_put32(head + 5, l.frame_count);
			head[KP_HD_PACK_HEAD_FORMAT] = KP_HD_FORMAT_BC;
			kp_le_put16(head + 10, (uint32_t)l.lit_pages);
			same_sha(T, head, sizeof head, kp_json_get(p, "HEAD"), "HEAD");
			same_sha(T, l.frames, (size_t)l.frame_count * 14, kp_json_get(p, "FRMS"), "FRMS");
			pages = kp_json_get(p, "page");
			for (j = 0; j < KP_HD_LAYERS; j++) {
				if (!(layer[j] = (uint8_t *)malloc(kp_hd_layer_bytes(KP_HD_FORMAT_BC, j, l.page_size)))) kp_t_fail(T, "out of memory");
			}
			for (page = 0; page < l.page_count; page++) {
				const KpJson *g = kp_json_at(pages, page);
				char what[64];
				if (kp_hd_layout_page(&l, page, kp_hd_scr_sheets_read, &sheets, layer[KP_HD_LAYER_DIFFUSE], layer[KP_HD_LAYER_TEAM],
						page < l.lit_pages ? layer + KP_HD_LAYER_LIGHT : NULL, &e) != 0) {
					kp_t_fail(T, "%s page %d: %s", pack, (int)page, e.msg);
				}
				for (j = 0; j < KP_HD_LAYERS; j++) {
					if (j >= KP_HD_LAYER_LIGHT && page >= l.lit_pages) continue;
					snprintf(what, sizeof what, "%s page %d's %s", pack, (int)page, SECTIONS[j]);
					same_sha(T, layer[j], kp_hd_layer_bytes(KP_HD_FORMAT_BC, j, l.page_size), kp_json_get(g, SECTIONS[j]), what);
				}
			}
			for (j = 0; j < KP_HD_LAYERS; j++) free(layer[j]);
			kp_hd_layout_free(&l);
		}
		for (k = 0; k < images; k++) kp_anim_free(&anims[k]);
	}
}

/* Each section of a container's bytes against the golden's (its keys: the sections). */
static void sections_equal(KpT *T, const uint8_t *data, size_t size, const KpJson *golden, const char *what)
{
	KpKpf c;
	KpError e;
	int k;
	kp_t_eq(T, kp_kpf_parse(&c, data, size, what, 1, &e), 0, "%s parses: %s", what, e.msg);
	kp_t_eq(T, c.section_count, kp_json_len(golden), "%s's sections", what);
	for (k = 0; k < c.section_count; k++) {
		char label[96];
		snprintf(label, sizeof label, "%s's %s", what, c.sections[k].fourcc);
		same_sha(T, c.sections[k].data, c.sections[k].size, kp_json_get(golden, c.sections[k].fourcc), label);
	}
	kp_kpf_free(&c);
}

static void tilesets_and_water_equal_the_converters(KpT *T)
{
	const KpJson *golden;
	Scr *s;
	int variant, t;
	if (!getenv(SCR)) kp_t_skip(T, "%s not set", SCR);
	golden = kp_t_golden(T, CONVERTER);
	s = scr(T);
	for (variant = 0; variant < KP_HD_SCR_VARIANTS; variant++) {
		const KpJson *v = kp_json_get(golden, kp_hd_scr_variant_name(variant));
		uint8_t *b;
		size_t n;
		KpError e;
		char what[64];
		for (t = 0; t < KP_TILESETS; t++) {
			snprintf(what, sizeof what, "%s %s", kp_hd_scr_variant_name(variant), KP_TILESET_NAMES[t]);
			if (!(b = kp_hd_scr_tileset(s->res, variant, t, &n, &e))) kp_t_fail(T, "%s: %s", what, e.msg);
			sections_equal(T, b, n, kp_json_get(kp_json_get(v, "tilesets"), KP_TILESET_NAMES[t]), what);
			free(b);
		}
		snprintf(what, sizeof what, "%s water", kp_hd_scr_variant_name(variant));
		if (!(b = kp_hd_scr_water(s->res, variant, &n, &e))) kp_t_fail(T, "%s: %s", what, e.msg);
		sections_equal(T, b, n, kp_json_get(v, "water"), what);
		free(b);
	}
}

/* A repack's BC sets, each pack (a shard's) opened from its head in the core as the runtime opens it, its HEAD, FRMS
 * and every page's layers read from the set's group; the tilesets and the water (shard 0): all against the golden. */
static void repack_equals_the_converters(KpT *T)
{
	static const char *const SECTIONS[KP_HD_LAYERS] = { "DIFF", "TEAM", "NRML", "AODP", "SPEC", "EMIS" };
	const KpJson *golden;
	KpRes *res;
	KpResReader *h;
	int variant, job = 0, sets = 0;
	if (!getenv(REPACK)) kp_t_skip(T, "%s not set", REPACK);
	golden = kp_t_golden(T, CONVERTER);
	res = kp_t_install(T, REPACK);
	kp_t_ok(T, (h = kp_res_reader_new()) != NULL, "out of memory");
	kp_t_defer(T, free_reader, h);
	for (variant = 0; variant < KP_HD_SCR_VARIANTS; variant++) {
		const char *set = kp_hd_scr_variant_name(variant);
		const KpJson *v = kp_json_get(golden, set), *packs = kp_json_get(v, "packs");
		char name[160], what[160];
		int k, t;
		snprintf(name, sizeof name, KP_HD_REPACK_WATER, set);
		if (!kp_res_raw_held(res, name)) continue;
		sets++;
		for (k = 0; k < kp_json_len(packs); k++) {
			const char *pack = packs->keys[k];
			const KpJson *p = packs->items[k], *pages = kp_json_get(p, "page");
			KpResPlace place;
			KpHdPack hd;
			KpKpf c;
			KpError e;
			uint8_t *head, *layer;
			size_t n;
			char why[256];
			int32_t page;
			int j;
			if (!kp_t_mine(T, job++)) continue;
			kp_t_log(T, "%s %s", set, pack);
			snprintf(name, sizeof name, KP_HD_REPACK_PACK, set, pack);
			kp_t_ok(T, kp_res_raw_place(res, name, &place) && place.kind == KP_RES_SOURCE_REPACK, "%s in an open group", name);
			snprintf(name, sizeof name, KP_HD_REPACK_HEAD, set, pack);
			kp_t_ok(T, (head = kp_res_raw_read(res, name, &n, why, sizeof why)) != NULL, "%s: %s", name, why);
			kp_t_defer(T, free_ptr, head);
			kp_t_eq(T, kp_hd_pack_open(&hd, head, n, (size_t)place.size, name, &e), 0, "%s opens: %s", name, e.msg);
			kp_t_eq(T, kp_kpf_parse_prefix(&c, head, n, (size_t)place.size, name, &e), 0, "%s parses: %s", name, e.msg);
			same_sha(T, kp_kpf_find(&c, "HEAD")->data, kp_kpf_find(&c, "HEAD")->size, kp_json_get(p, "HEAD"), "HEAD");
			same_sha(T, kp_kpf_find(&c, "FRMS")->data, kp_kpf_find(&c, "FRMS")->size, kp_json_get(p, "FRMS"), "FRMS");
			kp_kpf_free(&c);
			kp_t_eq(T, hd.page_count, kp_json_field_int(p, "pages", -1), "%s's pages", pack);
			kp_t_eq(T, hd.lit_pages, kp_json_field_int(p, "lit_pages", -1), "%s's lit pages", pack);
			kp_t_ok(T, (layer = (uint8_t *)malloc(kp_hd_layer_bytes(KP_HD_FORMAT_BC, KP_HD_LAYER_DIFFUSE, hd.page_size))) != NULL,
				"out of memory");
			kp_t_defer(T, free_ptr, layer);
			for (page = 0; page < hd.page_count; page++) {
				uint32_t offsets[KP_HD_LAYERS];
				kp_hd_page_offsets(&hd, page, &offsets[0], &offsets[1], offsets + KP_HD_LAYER_LIGHT);
				for (j = 0; j < KP_HD_LAYERS; j++) {
					size_t bytes = kp_hd_layer_bytes(KP_HD_FORMAT_BC, j, hd.page_size);
					if (j >= KP_HD_LAYER_LIGHT && page >= hd.lit_pages) continue;
					snprintf(what, sizeof what, "%s %s page %d's %s", set, pack, (int)page, SECTIONS[j]);
					kp_t_eq(T, kp_res_reader_read(h, &place, offsets[j], layer, bytes), 0, "%s read", what);
					same_sha(T, layer, bytes, kp_json_get(kp_json_at(pages, page), SECTIONS[j]), what);
				}
			}
		}
		if (!kp_t_mine(T, 0)) continue;
		for (t = 0; t <= KP_TILESETS; t++) { /* the tilesets, then the water */
			uint8_t *b;
			size_t n;
			char why[256];
			if (t < KP_TILESETS) snprintf(name, sizeof name, KP_HD_REPACK_TILESET, set, KP_TILESET_NAMES[t]);
			else snprintf(name, sizeof name, KP_HD_REPACK_WATER, set);
			kp_t_ok(T, (b = kp_res_raw_read(res, name, &n, why, sizeof why)) != NULL, "%s: %s", name, why);
			sections_equal(T, b, n, t < KP_TILESETS ? kp_json_get(kp_json_get(v, "tilesets"), KP_TILESET_NAMES[t])
				: kp_json_get(v, "water"), name);
			free(b);
		}
	}
	kp_t_ok(T, sets > 0, "the repack has an HD set");
}

static const KpTest TESTS[] = {
	{ "packer_keeps_a_group_together", packer_keeps_a_group_together, 0 },
	{ "packer_fills_only_the_open_pages", packer_fills_only_the_open_pages, 0 },
	{ "anim_head_and_frames", anim_head_and_frames, 0 },
	{ "relations_of_images", relations_of_images, 0 },
	{ "page_keeps_the_lit_pages_layers", page_keeps_the_lit_pages_layers, 0 },
	{ "packs_equal_the_converters", packs_equal_the_converters, PACK_SHARDS },
	{ "tilesets_and_water_equal_the_converters", tilesets_and_water_equal_the_converters, 0 },
	{ "repack_equals_the_converters", repack_equals_the_converters, PACK_SHARDS },
};

KP_SUITE(kp_suite_hd, "test_hd", TESTS);
