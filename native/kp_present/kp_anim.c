/*
 * The HD packs from StarCraft: Remastered's own files (ADR 0019, stage C5): an image's anim (anim/main_NNN.anim,
 * HD2/anim/main_NNN.anim) read from its header and frame records, the frames of an SD pack's GRPs packed into HD pages
 * as the Python converter packed them (each image's frames together, the lit images' first), and a page's blocks laid
 * out from the anims' sheets when it is asked for. The same inputs give the FRMS and pages the converter wrote before
 * stage C5, byte for byte (tests/c/test_hd.c, tests/golden/hd/converter.json); the runtime builds a mission's packs at its start (kp_app_world.c) and their pages on the HD
 * loader's thread, kp_convert writes them whole into the repack's HD groups.
 *
 * An anim: "ANIM", u8 scale (4 HD, 2 HD2, 1 SD), u8 type (2: one image), u16 0, u16 layers, u16 entries, ten 32-byte
 * layer names; the entry: u16 frames, u16 0xffff, u16 width and height of the image's box, u32 offset of its frame
 * records, then per layer u32 offset, u32 size, u16 width, u16 height of its DDS sheet (offset 0: an empty layer). A
 * frame record (16 bytes): u16 x, y in the sheet, i16 x, y in the box, u16 width, height, u32 1; the box and the
 * records are in HD (x4) pixels in HD2's files too, whose sheets are half the size.
 */
#include "kp_present_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ANIM_HEADER 12        /* "ANIM", u8 scale, u8 type, u16 0, u16 layers, u16 entries */
#define ANIM_NAME 32
#define ANIM_NAMES 10
#define ANIM_ENTRY 12         /* u16 frames, u16 0xffff, u16 box width, height, u32 frame records' offset */
#define ANIM_LAYER 12         /* u32 offset, u32 size, u16 width, height */
#define ANIM_TYPE_IMAGE 2
#define ANIM_RECORD_SCALE 4   /* the box and the records are in HD pixels */
#define DDS_HEADER 128
#define DDS_PF_FOURCC 0x4
#define BLOCK 4
#define DIFFUSE_BLOCK 16      /* bytes of a BC3 block */
#define TEAM_BLOCK 8          /* of a BC1 block */

/* The anim's layers the HD pages take, in KP_HD_LAYER_* order (after the diffuse and team), with
 * their blocks */
static const struct {
	const char *name;
	const char *fourcc;
} LAYERS[KP_HD_LAYERS] = { { "diffuse", "DXT5" }, { "teamcolor", "DXT1" }, { "normal", "DXT5" }, { "ao_depth", "DXT5" },
	{ "specular", "DXT1" }, { "emissive", "DXT1" } };

static int fail(KpError *err, const char *name, const char *msg) { return kp_present_fail(err, name, msg); }

/* Floor division by a positive divisor (Python's //, which the converter's coordinates were computed with). */
static int32_t floor_div(int32_t a, int32_t b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

int kp_anim_open(KpAnim *a, const uint8_t *head, size_t n, uint64_t file_size, const char *name, KpError *err)
{
	uint32_t layers, entries, k, j;
	memset(a, 0, sizeof *a);
	if (n < ANIM_HEADER + ANIM_NAME * ANIM_NAMES + ANIM_ENTRY || memcmp(head, "ANIM", 4) != 0) {
		return fail(err, name, "not an anim file");
	}
	a->scale = head[4];
	layers = kp_le_u16(head + 8);
	entries = kp_le_u16(head + 10);
	if (head[5] != ANIM_TYPE_IMAGE || entries != 1 || (a->scale != 1 && a->scale != 2 && a->scale != 4) || layers > ANIM_NAMES) {
		return fail(err, name, "an anim of another scale, type, entry count or layer count");
	}
	if (n < ANIM_HEADER + ANIM_NAME * ANIM_NAMES + ANIM_ENTRY + ANIM_LAYER * (size_t)layers) {
		return fail(err, name, "anim header truncated");
	}
	{
		const uint8_t *entry = head + ANIM_HEADER + ANIM_NAME * ANIM_NAMES;
		int32_t div = ANIM_RECORD_SCALE / a->scale;
		a->frame_count = kp_le_u16(entry);
		a->box_w = (int32_t)kp_le_u16(entry + 4) / div;
		a->box_h = (int32_t)kp_le_u16(entry + 6) / div;
		a->frames_at = kp_le_u32(entry + 8);
	}
	for (k = 0; k < layers; k++) {
		const uint8_t *nm = head + ANIM_HEADER + ANIM_NAME * k;
		const uint8_t *l = head + ANIM_HEADER + ANIM_NAME * ANIM_NAMES + ANIM_ENTRY + ANIM_LAYER * k;
		uint32_t offset = kp_le_u32(l), size = kp_le_u32(l + 4);
		size_t len = 0;
		while (len < ANIM_NAME && nm[len]) len++;
		if (!offset) continue;
		if ((uint64_t)offset + size > file_size) return fail(err, name, "a layer runs past the end");
		for (j = 0; j < KP_HD_LAYERS; j++) {
			if (strlen(LAYERS[j].name) == len && memcmp(LAYERS[j].name, nm, len) == 0) {
				a->layer_offset[j] = offset;
				a->layer_size[j] = size;
			}
		}
	}
	if ((uint64_t)a->frames_at + (uint64_t)KP_ANIM_FRAME_RECORD * a->frame_count > file_size) {
		return fail(err, name, "frame records run past the end");
	}
	return 0;
}

int kp_anim_frames(KpAnim *a, const uint8_t *records, size_t n, const char *name, KpError *err)
{
	int32_t div = ANIM_RECORD_SCALE / a->scale;
	uint32_t f;
	free(a->frames);
	a->frames = NULL;
	if (n < (size_t)KP_ANIM_FRAME_RECORD * a->frame_count) return fail(err, name, "frame records truncated");
	if (!(a->frames = (int32_t *)malloc(sizeof *a->frames * 6 * (size_t)(a->frame_count ? a->frame_count : 1)))) {
		return fail(err, name, "out of memory");
	}
	for (f = 0; f < a->frame_count; f++) {
		const uint8_t *r = records + (size_t)KP_ANIM_FRAME_RECORD * f;
		int32_t *o = a->frames + 6 * (size_t)f;
		o[0] = (int32_t)kp_le_u16(r) / div;
		o[1] = (int32_t)kp_le_u16(r + 2) / div;
		o[2] = floor_div((int16_t)kp_le_u16(r + 4), div);
		o[3] = floor_div((int16_t)kp_le_u16(r + 6), div);
		o[4] = (int32_t)kp_le_u16(r + 8) / div;
		o[5] = (int32_t)kp_le_u16(r + 10) / div;
	}
	return 0;
}

void kp_anim_free(KpAnim *a)
{
	free(a->frames);
	memset(a, 0, sizeof *a);
}

int kp_anim_lit(const KpAnim *a)
{
	return a->layer_offset[KP_HD_LAYER_LIGHT + KP_HD_LIGHT_NORMAL] != 0 && a->layer_offset[KP_HD_LAYER_LIGHT + KP_HD_LIGHT_AO] != 0;
}

int kp_anim_relations(const uint8_t *rel, size_t n, int32_t **owners, uint32_t *count, KpError *err)
{
	uint32_t k;
	*owners = NULL;
	*count = 0;
	if (n % KP_ANIM_RELATIONS) return fail(err, "SD/images.rel", "not whole records");
	if (!(*owners = (int32_t *)malloc(sizeof **owners * (n / KP_ANIM_RELATIONS ? n / KP_ANIM_RELATIONS : 1)))) {
		return fail(err, "SD/images.rel", "out of memory");
	}
	for (k = 0; k < n / KP_ANIM_RELATIONS; k++) (*owners)[k] = (int32_t)kp_le_u32(rel + (size_t)KP_ANIM_RELATIONS * k + 4);
	*count = (uint32_t)(n / KP_ANIM_RELATIONS);
	return 0;
}

/* --- the layout --- */

typedef struct {
	int32_t x, y, w, h; /* a frame's rectangle in its sheet widened to whole blocks */
} Rect;

static Rect block_rect(const int32_t *f)
{
	Rect r;
	r.x = f[0] / BLOCK * BLOCK;
	r.y = f[1] / BLOCK * BLOCK;
	r.w = (f[0] + f[4] + BLOCK - 1) / BLOCK * BLOCK - r.x;
	r.h = (f[1] + f[5] + BLOCK - 1) / BLOCK * BLOCK - r.y;
	return r;
}

/* The images' order: the lit first, then by image */
typedef struct {
	int32_t image;
	int lit;
} Owner;

static int by_lit_then_image(const void *a, const void *b)
{
	const Owner *x = (const Owner *)a, *y = (const Owner *)b;
	if (x->lit != y->lit) return y->lit - x->lit;
	return x->image < y->image ? -1 : x->image > y->image;
}

void kp_hd_layout_free(KpHdLayout *l)
{
	free(l->frames);
	free(l->pieces);
	free(l->page_first);
	free(l->images);
	free(l->layers);
	memset(l, 0, sizeof *l);
}

int kp_hd_layout_build(KpHdLayout *l, const KpGrpMeta *g, const char *name, const int32_t *owner_of, const KpAnim *anims,
	int32_t image_count, int32_t scale, int32_t page_size, KpError *err)
{
	uint32_t a, e, f, k, *entries = NULL, entry_count = 0, *first = NULL, rect_count = 0, group_count = 0;
	Owner *owners = NULL;
	Rect *rects = NULL;
	uint16_t *sizes = NULL;
	KpPagePlace *places = NULL;
	int32_t *index_of = NULL, lit_groups = 0, lit_pages = 0, other_pages = 0;
	int32_t *frame_rect = NULL; /* per owner's frame: its rectangle's index, -1 none */
	uint32_t *frame_first = NULL;
	int rc = -1, pages = 0;
	memset(l, 0, sizeof *l);
	l->scale = scale;
	l->page_size = page_size;
	/* the pack's entries in grpmeta's atlas order (resources.py pack_entries), the images drawing them */
	if (!(entries = (uint32_t *)malloc(sizeof *entries * (g->grp_count + 1)))
		|| !(index_of = (int32_t *)malloc(sizeof *index_of * (size_t)(image_count ? image_count : 1)))
		|| !(owners = (Owner *)malloc(sizeof *owners * (size_t)(image_count ? image_count : 1)))) {
		goto oom;
	}
	for (k = 0; k < (uint32_t)image_count; k++) index_of[k] = -1;
	for (a = 1; a <= g->atlas_count; a++) {
		if (!kp_label_is(&g->atlases[a].pack, name)) continue;
		for (e = 1; e <= g->grp_count; e++) {
			int32_t o;
			if (g->grps[e].atlas != a) continue;
			entries[entry_count++] = e;
			o = owner_of[e];
			if (o < 0 || o >= image_count || !anims[o].frames) continue;
			if (anims[o].frame_count != g->grps[e].frames) {
				snprintf(err->msg, sizeof err->msg, "hd: image %d: %u HD frames, its GRP has %u", (int)o,
					(unsigned)anims[o].frame_count, (unsigned)g->grps[e].frames);
				goto done;
			}
			if (anims[o].scale != scale) {
				snprintf(err->msg, sizeof err->msg, "hd: image %d: scale %d, the variant's %d", (int)o, (int)anims[o].scale,
					(int)scale);
				goto done;
			}
			if (index_of[o] < 0) {
				index_of[o] = 0;
				owners[l->image_count].image = o;
				owners[l->image_count].lit = kp_anim_lit(&anims[o]);
				l->image_count++;
			}
		}
	}
	qsort(owners, (size_t)l->image_count, sizeof *owners, by_lit_then_image);
	/* each image's distinct block rectangles, in the order of its frames */
	if (!(first = (uint32_t *)malloc(sizeof *first * ((size_t)l->image_count + 1)))
		|| !(frame_first = (uint32_t *)malloc(sizeof *frame_first * ((size_t)l->image_count + 1)))) {
		goto oom;
	}
	{
		size_t frames = 0;
		for (k = 0; k < (uint32_t)l->image_count; k++) frames += anims[owners[k].image].frame_count;
		if (!(rects = (Rect *)malloc(sizeof *rects * (frames ? frames : 1)))
			|| !(frame_rect = (int32_t *)malloc(sizeof *frame_rect * (frames ? frames : 1)))) {
			goto oom;
		}
	}
	for (k = 0; k < (uint32_t)l->image_count; k++) {
		const KpAnim *an = &anims[owners[k].image];
		uint32_t base = (k ? frame_first[k - 1] + anims[owners[k - 1].image].frame_count : 0);
		index_of[owners[k].image] = (int32_t)k;
		lit_groups += owners[k].lit;
		first[k] = rect_count;
		frame_first[k] = base;
		for (f = 0; f < an->frame_count; f++) {
			Rect r;
			uint32_t j;
			frame_rect[base + f] = -1;
			if (!an->frames[6 * (size_t)f + 4]) continue;
			r = block_rect(an->frames + 6 * (size_t)f);
			for (j = first[k]; j < rect_count; j++) {
				if (rects[j].x == r.x && rects[j].y == r.y && rects[j].w == r.w && rects[j].h == r.h) break;
			}
			if (j == rect_count) rects[rect_count++] = r;
			frame_rect[base + f] = (int32_t)j;
		}
	}
	first[l->image_count] = rect_count;
	group_count = (uint32_t)l->image_count;
	/* the lit images' groups on pages of their own, the others' after them, in blocks */
	if (!(sizes = (uint16_t *)malloc(sizeof *sizes * 2 * (rect_count ? rect_count : 1)))
		|| !(places = (KpPagePlace *)malloc(sizeof *places * (rect_count ? rect_count : 1)))) {
		goto oom;
	}
	for (k = 0; k < rect_count; k++) {
		if (rects[k].w / BLOCK > 0xffff || rects[k].h / BLOCK > 0xffff) {
			fail(err, name, "hd: a frame larger than a page");
			goto done;
		}
		sizes[2 * k] = (uint16_t)(rects[k].w / BLOCK);
		sizes[2 * k + 1] = (uint16_t)(rects[k].h / BLOCK);
	}
	if (kp_pages_pack_groups(sizes, first, (uint32_t)lit_groups, page_size / BLOCK, KP_HD_OPEN_PAGES, places, &pages, err) != 0) {
		goto done;
	}
	lit_pages = pages;
	{
		uint32_t base = first[lit_groups];
		uint32_t *rest = NULL;
		if (!(rest = (uint32_t *)malloc(sizeof *rest * (group_count - (uint32_t)lit_groups + 1)))) goto oom;
		for (k = (uint32_t)lit_groups; k <= group_count; k++) rest[k - (uint32_t)lit_groups] = first[k] - base;
		rc = kp_pages_pack_groups(sizes + 2 * (size_t)base, rest, group_count - (uint32_t)lit_groups, page_size / BLOCK,
			KP_HD_OPEN_PAGES, places + base, &pages, err);
		free(rest);
		if (rc != 0) goto done;
		rc = -1;
		other_pages = pages;
		for (k = base; k < rect_count; k++) places[k].page = (uint16_t)(places[k].page + lit_pages);
	}
	if (lit_pages + other_pages > 0xffff) {
		fail(err, name, "hd: more than 65535 pages");
		goto done;
	}
	l->page_count = lit_pages + other_pages;
	l->lit_pages = lit_pages;
	/* the frame records, entry after entry */
	for (k = 0; k < entry_count; k++) l->frame_count += g->grps[entries[k]].frames;
	if (!(l->frames = (uint8_t *)calloc((size_t)(l->frame_count ? l->frame_count : 1), KP_HD_FRAME_RECORD))) goto oom;
	{
		uint8_t *out = l->frames;
		for (k = 0; k < entry_count; k++) {
			const KpGrpEntry *ge = &g->grps[entries[k]];
			int32_t o = owner_of[entries[k]], i = o >= 0 && o < image_count ? index_of[o] : -1;
			for (f = 0; f < ge->frames; f++, out += KP_HD_FRAME_RECORD) {
				int gx, gy, gw, gh;
				const int32_t *hf;
				int32_t ri, dx2, dy2;
				const Rect *r;
				const KpPagePlace *p;
				if (i < 0) continue;
				kp_grpmeta_frame(g, entries[k], f, &gx, &gy, &gw, &gh);
				hf = anims[o].frames + 6 * (size_t)f;
				if (!gw || !hf[4]) continue;
				ri = frame_rect[frame_first[i] + f];
				r = &rects[ri];
				p = &places[ri];
				dx2 = 2 * hf[2] + hf[4] - anims[o].box_w - scale * (2 * gx + gw - (int32_t)ge->width);
				dy2 = 2 * hf[3] + hf[5] - anims[o].box_h - scale * (2 * gy + gh - (int32_t)ge->height);
				kp_le_put16(out, p->page);
				kp_le_put16(out + 2, (uint32_t)(p->x * BLOCK + hf[0] - r->x));
				kp_le_put16(out + 4, (uint32_t)(p->y * BLOCK + hf[1] - r->y));
				kp_le_put16(out + 6, (uint32_t)hf[4]);
				kp_le_put16(out + 8, (uint32_t)hf[5]);
				kp_le_put16(out + 10, (uint32_t)(uint16_t)(int16_t)dx2);
				kp_le_put16(out + 12, (uint32_t)(uint16_t)(int16_t)dy2);
			}
		}
	}
	/* the pieces page after page, each image's in its order, and the images' layers */
	if (!(l->page_first = (int32_t *)calloc((size_t)l->page_count + 1, sizeof *l->page_first))
		|| !(l->pieces = (KpHdPiece *)malloc(sizeof *l->pieces * (rect_count ? rect_count : 1)))
		|| !(l->images = (int32_t *)malloc(sizeof *l->images * (size_t)(l->image_count ? l->image_count : 1)))
		|| !(l->layers = (KpHdLayers *)malloc(sizeof *l->layers * (size_t)(l->image_count ? l->image_count : 1)))) {
		goto oom;
	}
	for (k = 0; k < (uint32_t)l->image_count; k++) {
		int j;
		l->images[k] = owners[k].image;
		for (j = 0; j < KP_HD_LAYERS; j++) {
			l->layers[k].offset[j] = anims[owners[k].image].layer_offset[j];
			l->layers[k].size[j] = anims[owners[k].image].layer_size[j];
		}
	}
	for (k = 0; k < rect_count; k++) l->page_first[places[k].page + 1]++;
	for (k = 0; k < (uint32_t)l->page_count; k++) l->page_first[k + 1] += l->page_first[k];
	{
		int32_t *at = NULL;
		uint32_t owner = 0;
		if (!(at = (int32_t *)malloc(sizeof *at * ((size_t)l->page_count + 1)))) goto oom;
		memcpy(at, l->page_first, sizeof *at * ((size_t)l->page_count + 1));
		for (k = 0; k < rect_count; k++) {
			KpHdPiece *p;
			while (k >= first[owner + 1]) owner++;
			p = &l->pieces[at[places[k].page]++];
			p->owner = (int32_t)owner;
			p->sx = (uint16_t)rects[k].x;
			p->sy = (uint16_t)rects[k].y;
			p->w = (uint16_t)rects[k].w;
			p->h = (uint16_t)rects[k].h;
			p->x = (uint16_t)(places[k].x * BLOCK);
			p->y = (uint16_t)(places[k].y * BLOCK);
		}
		free(at);
	}
	rc = 0;
	goto done;
oom:
	fail(err, name, "out of memory");
done:
	if (rc != 0) kp_hd_layout_free(l);
	free(entries);
	free(first);
	free(owners);
	free(rects);
	free(sizes);
	free(places);
	free(index_of);
	free(frame_rect);
	free(frame_first);
	return rc;
}

/* --- a page's blocks --- */

/* The w x h blocks at (sx, sy) of a band of sheet rows (its first row at band_y, stride bytes a row of blocks) into a
 * page's layer at (x, y). */
static void blit(uint8_t *page, int32_t page_size, int32_t x, int32_t y, int32_t w, int32_t h, const uint8_t *band,
	int32_t band_y, size_t stride, int32_t sx, int32_t sy, int step)
{
	size_t columns = (size_t)(page_size / BLOCK), row = (size_t)(w / BLOCK) * (size_t)step;
	int32_t by;
	for (by = 0; by < h / BLOCK; by++) {
		size_t to = (((size_t)(y / BLOCK + by)) * columns + (size_t)(x / BLOCK)) * (size_t)step;
		size_t at = (size_t)((sy - band_y) / BLOCK + by) * stride + (size_t)(sx / BLOCK) * (size_t)step;
		memcpy(page + to, band + at, row);
	}
}

int kp_hd_layout_page(const KpHdLayout *l, int32_t page, KpHdAnimRead read, void *ctx, uint8_t *diffuse, uint8_t *team,
	uint8_t *const *light, KpError *err)
{
	uint8_t *out[KP_HD_LAYERS];
	uint8_t *band = NULL;
	size_t band_cap = 0;
	int32_t first, end, i, j;
	int layer, lit = page < l->lit_pages && light != NULL, rc = -1;
	char name[64];
	if (page < 0 || page >= l->page_count) return fail(err, "hd", "no such page");
	out[KP_HD_LAYER_DIFFUSE] = diffuse;
	out[KP_HD_LAYER_TEAM] = team;
	for (layer = 0; layer < KP_HD_LIGHTS; layer++) out[KP_HD_LAYER_LIGHT + layer] = lit ? light[layer] : NULL;
	for (layer = 0; layer < KP_HD_LAYERS; layer++) {
		if (out[layer]) memset(out[layer], 0, kp_hd_layer_bytes(KP_HD_FORMAT_BC, layer, l->page_size));
	}
	first = l->page_first[page];
	end = l->page_first[page + 1];
	for (i = first; i < end; i = j) {
		int32_t owner = l->pieces[i].owner, row0 = 0x7fffffff, row1 = 0, sheet_w = 0, sheet_h = 0;
		const KpHdLayers *ly = &l->layers[owner];
		for (j = i; j < end && l->pieces[j].owner == owner; j++) {
			if (l->pieces[j].sy < row0) row0 = l->pieces[j].sy;
			if (l->pieces[j].sy + l->pieces[j].h > row1) row1 = l->pieces[j].sy + l->pieces[j].h;
		}
		snprintf(name, sizeof name, "hd: image %d", (int)l->images[owner]);
		for (layer = 0; layer < KP_HD_LAYERS; layer++) {
			uint8_t dds[DDS_HEADER];
			int32_t w, h, step, k;
			size_t stride, blocks, need;
			if (!out[layer]) continue;
			if (!ly->offset[layer]) {
				if (layer == KP_HD_LAYER_DIFFUSE) {
					fail(err, name, "no diffuse layer");
					goto done;
				}
				continue; /* zero blocks where an image has none */
			}
			if (ly->size[layer] < DDS_HEADER || read(ctx, l->images[owner], ly->offset[layer], dds, DDS_HEADER) != 0) {
				fail(err, name, "a sheet cannot be read");
				goto done;
			}
			if (memcmp(dds, "DDS ", 4) != 0 || !(kp_le_u32(dds + 80) & DDS_PF_FOURCC)
				|| memcmp(dds + 84, LAYERS[layer].fourcc, 4) != 0) {
				snprintf(err->msg, sizeof err->msg, "%s: %s is not %s blocks", name, LAYERS[layer].name, LAYERS[layer].fourcc);
				goto done;
			}
			h = (int32_t)kp_le_u32(dds + 12);
			w = (int32_t)kp_le_u32(dds + 16);
			if (layer == KP_HD_LAYER_DIFFUSE) {
				sheet_w = w;
				sheet_h = h;
			} else if (w != sheet_w || h != sheet_h) {
				snprintf(err->msg, sizeof err->msg, "%s: %s %d x %d, the diffuse %d x %d", name, LAYERS[layer].name, (int)w,
					(int)h, (int)sheet_w, (int)sheet_h);
				goto done;
			}
			step = LAYERS[layer].fourcc[3] == '1' ? TEAM_BLOCK : DIFFUSE_BLOCK;
			stride = (size_t)((w + BLOCK - 1) / BLOCK) * (size_t)step;
			blocks = (size_t)((h + BLOCK - 1) / BLOCK) * stride;
			if (ly->size[layer] - DDS_HEADER < blocks) {
				snprintf(err->msg, sizeof err->msg, "%s: %s truncated", name, LAYERS[layer].name);
				goto done;
			}
			if (row1 > (h + BLOCK - 1) / BLOCK * BLOCK) {
				snprintf(err->msg, sizeof err->msg, "%s: a frame outside its sheet", name);
				goto done;
			}
			need = (size_t)((row1 - row0 + BLOCK - 1) / BLOCK) * stride;
			if (need > band_cap) {
				uint8_t *grown = (uint8_t *)realloc(band, need);
				if (!grown) {
					fail(err, name, "out of memory");
					goto done;
				}
				band = grown;
				band_cap = need;
			}
			if (read(ctx, l->images[owner], (uint64_t)ly->offset[layer] + DDS_HEADER + (size_t)(row0 / BLOCK) * stride, band,
					need) != 0) {
				fail(err, name, "a sheet cannot be read");
				goto done;
			}
			for (k = i; k < j; k++) {
				const KpHdPiece *p = &l->pieces[k];
				if (p->sx + p->w > ((w + BLOCK - 1) / BLOCK) * BLOCK) {
					snprintf(err->msg, sizeof err->msg, "%s: a frame outside its sheet", name);
					goto done;
				}
				blit(out[layer], l->page_size, p->x, p->y, p->w, p->h, band, row0, stride, p->sx, p->sy, step);
			}
		}
	}
	rc = 0;
done:
	free(band);
	return rc;
}

void kp_hd_layout_pack(const KpHdLayout *l, KpHdPack *p)
{
	memset(p, 0, sizeof *p);
	p->scale = l->scale;
	p->page_size = l->page_size;
	p->page_count = l->page_count;
	p->lit_pages = l->lit_pages;
	p->frame_count = l->frame_count;
	p->format = KP_HD_FORMAT_BC;
	p->frames = l->frames;
}

/* --- the whole pack --- */

int kp_hd_pack_build(const KpHdLayout *l, KpHdAnimRead read, void *ctx, const char *profile_id, uint8_t **out,
	size_t *size, KpError *err)
{
	/* HEAD, FRMS, then each layer's pages (the four lighting sections even when no page is lit) */
	static const char *const SECTIONS[2 + KP_HD_LAYERS] = { "HEAD", "FRMS", "DIFF", "TEAM", "NRML", "AODP", "SPEC", "EMIS" };
	KpKpfOut sections[2 + KP_HD_LAYERS];
	size_t offsets[2 + KP_HD_LAYERS], bytes[KP_HD_LAYERS];
	uint8_t *b, *light[KP_HD_LIGHTS];
	int32_t page;
	int k;
	*out = NULL;
	*size = 0;
	memset(sections, 0, sizeof sections);
	for (k = 0; k < 2 + KP_HD_LAYERS; k++) sections[k].fourcc = SECTIONS[k];
	sections[0].size = KP_HD_PACK_HEAD_BYTES;
	sections[1].size = (size_t)l->frame_count * KP_HD_FRAME_RECORD;
	for (k = 0; k < KP_HD_LAYERS; k++) {
		bytes[k] = kp_hd_layer_bytes(KP_HD_FORMAT_BC, k, l->page_size);
		sections[2 + k].size = bytes[k] * (size_t)(k >= KP_HD_LAYER_LIGHT ? l->lit_pages : l->page_count);
	}
	if (kp_kpf_layout(KP_HD_PACK_KIND, KP_HD_PACK_SCHEMA, 0, profile_id, NULL, sections, 2 + KP_HD_LAYERS, &b, size,
			offsets) != 0) {
		return fail(err, "hd", "out of memory, or a pack past 4 GB");
	}
	b[offsets[0]] = (uint8_t)l->scale;
	kp_le_put16(b + offsets[0] + 1, (uint32_t)l->page_size);
	kp_le_put16(b + offsets[0] + 3, (uint32_t)l->page_count);
	kp_le_put32(b + offsets[0] + 5, l->frame_count);
	b[offsets[0] + KP_HD_PACK_HEAD_FORMAT] = KP_HD_FORMAT_BC;
	kp_le_put16(b + offsets[0] + 10, (uint32_t)l->lit_pages);
	if (l->frame_count) memcpy(b + offsets[1], l->frames, sections[1].size);
	for (page = 0; page < l->page_count; page++) {
		for (k = 0; k < KP_HD_LIGHTS; k++) light[k] = b + offsets[2 + KP_HD_LAYER_LIGHT + k] + bytes[KP_HD_LAYER_LIGHT + k] * (size_t)page;
		if (kp_hd_layout_page(l, page, read, ctx, b + offsets[2] + bytes[KP_HD_LAYER_DIFFUSE] * (size_t)page,
				b + offsets[3] + bytes[KP_HD_LAYER_TEAM] * (size_t)page, page < l->lit_pages ? light : NULL, err) != 0) {
			free(b);
			*size = 0;
			return -1;
		}
	}
	kp_kpf_seal(b);
	*out = b;
	return 0;
}
