/*
 * A pack's pages built from its GRPs (ADR 0018, stage D5): converter/emit/pages.py's skyline packer and
 * converter/kpf/resources.py build_pack in C, so a mission's packs need no converter. The atlases of the pack in
 * grpmeta's order, each GRP entry's frames decoded (kp_grp_decode), equal frames sharing one rectangle, the rectangles
 * packed by height, then width (both descending), then their first appearance, each on the first page where it fits at
 * the skyline's lowest top edge (ties: the lowest x). The same inputs give the converter's placements and page bytes.
 */
#include "kp_present_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kp_grp.h"

#define FRAME_RECORD 10 /* FRMS: page, x, y, width, height */

/* --- the skyline packer (pages.py pack) --- */

typedef struct {
	int32_t x, y, w; /* a segment of the top edge of a page's occupied area, left to right */
} Segment;

typedef struct {
	Segment *s;
	int n, cap;
} Skyline;

/* (segment, x, y) of the lowest top edge where a w x h rectangle fits: 1, else 0. */
static int sky_find(const Skyline *k, int32_t w, int32_t h, int32_t size, int *at, int32_t *x_out, int32_t *y_out)
{
	int i, found = 0;
	int32_t best_top = 0, best_x = 0;
	for (i = 0; i < k->n; i++) {
		int32_t x = k->s[i].x, y = 0, rest = w;
		int j = i;
		if (x + w > size) break;
		while (rest > 0 && j < k->n) {
			if (k->s[j].y > y) y = k->s[j].y;
			rest -= k->s[j].w;
			j++;
		}
		if (y + h > size) continue;
		if (!found || y + h < best_top || (y + h == best_top && x < best_x)) {
			found = 1;
			best_top = y + h;
			best_x = x;
			*at = i;
			*x_out = x;
			*y_out = y;
		}
	}
	return found;
}

static void sky_remove(Skyline *k, int i)
{
	memmove(k->s + i, k->s + i + 1, (size_t)(k->n - i - 1) * sizeof *k->s);
	k->n--;
}

static int sky_place(Skyline *k, int i, int32_t x, int32_t y, int32_t w, int32_t h)
{
	int32_t end = x + w;
	int j;
	if (k->n == k->cap) {
		int cap = k->cap ? 2 * k->cap : 64;
		Segment *g = (Segment *)realloc(k->s, (size_t)cap * sizeof *g);
		if (!g) return -1;
		k->s = g;
		k->cap = cap;
	}
	memmove(k->s + i + 1, k->s + i, (size_t)(k->n - i) * sizeof *k->s);
	k->s[i].x = x;
	k->s[i].y = y + h;
	k->s[i].w = w;
	k->n++;
	j = i + 1;
	while (j < k->n && k->s[j].x < end) {
		int32_t cut = end - k->s[j].x;
		if (k->s[j].w <= cut) {
			sky_remove(k, j);
			continue;
		}
		k->s[j].x += cut;
		k->s[j].w -= cut;
		break;
	}
	for (j = 0; j + 1 < k->n;) {
		if (k->s[j].y == k->s[j + 1].y) {
			k->s[j].w += k->s[j + 1].w;
			sky_remove(k, j + 1);
		} else {
			j++;
		}
	}
	return 0;
}

static int sky_init(Skyline *k, int32_t size)
{
	memset(k, 0, sizeof *k);
	if (!(k->s = (Segment *)malloc(64 * sizeof *k->s))) return -1;
	k->cap = 64;
	k->n = 1;
	k->s[0].x = k->s[0].y = 0;
	k->s[0].w = size;
	return 0;
}

/* The packing order as one ascending key: height, then width (both descending), then the input position. */
static uint64_t order_key(uint16_t w, uint16_t h, uint32_t i)
{
	return (uint64_t)(0xffffu - h) << 48 | (uint64_t)(0xffffu - w) << 32 | i;
}

static int by_key(const void *a, const void *b)
{
	uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
	return x < y ? -1 : x > y;
}

int kp_pages_pack(const uint16_t *sizes, uint32_t n, int32_t page_size, KpPagePlace *out, int *page_count, KpError *err)
{
	Skyline *pages = NULL;
	uint64_t *order;
	uint32_t i, m = 0;
	int count = 0, cap = 0, p, rc = -1;
	*page_count = 0;
	memset(out, 0, (size_t)n * sizeof *out);
	if (!(order = (uint64_t *)malloc((size_t)(n ? n : 1) * sizeof *order))) return kp_present_fail(err, "pages", "out of memory");
	for (i = 0; i < n; i++)
		if (sizes[2 * i] && sizes[2 * i + 1]) order[m++] = order_key(sizes[2 * i], sizes[2 * i + 1], i);
	qsort(order, m, sizeof *order, by_key);
	for (i = 0; i < m; i++) {
		uint32_t k = (uint32_t)(order[i] & 0xffffffffu);
		int32_t w = sizes[2 * k], h = sizes[2 * k + 1], x = 0, y = 0;
		int at = 0;
		if (w > page_size || h > page_size) {
			snprintf(err->msg, sizeof err->msg, "pages: rectangle %dx%d larger than a %dx%d page", (int)w, (int)h,
				(int)page_size, (int)page_size);
			goto done;
		}
		for (p = 0; p < count; p++)
			if (sky_find(&pages[p], w, h, page_size, &at, &x, &y)) break;
		if (p == count) {
			if (count == cap) {
				int c = cap ? 2 * cap : 8;
				Skyline *g = (Skyline *)realloc(pages, (size_t)c * sizeof *g);
				if (!g) goto oom;
				pages = g;
				cap = c;
			}
			if (sky_init(&pages[count], page_size) != 0) goto oom;
			count++;
			sky_find(&pages[p], w, h, page_size, &at, &x, &y);
		}
		if (sky_place(&pages[p], at, x, y, w, h) != 0) goto oom;
		out[k].page = (uint16_t)p;
		out[k].x = (uint16_t)x;
		out[k].y = (uint16_t)y;
	}
	*page_count = count;
	rc = 0;
	goto done;
oom:
	kp_present_fail(err, "pages", "out of memory");
done:
	for (p = 0; p < count; p++) free(pages[p].s);
	free(pages);
	free(order);
	return rc;
}

int kp_pages_pack_groups(const uint16_t *sizes, const uint32_t *first, uint32_t groups, int32_t page_size, int32_t open_pages,
	KpPagePlace *out, int *page_count, KpError *err)
{
	Skyline *pages = NULL;
	uint64_t *order = NULL;
	uint32_t g, i, n = first[groups] - first[0];
	int count = 0, cap = 0, p, rc = -1;
	*page_count = 0;
	memset(out, 0, (size_t)n * sizeof *out);
	if (!(order = (uint64_t *)malloc((size_t)(n ? n : 1) * sizeof *order))) return kp_present_fail(err, "pages", "out of memory");
	for (g = 0; g < groups; g++) {
		uint32_t m = 0;
		for (i = first[g]; i < first[g + 1]; i++) order[m++] = order_key(sizes[2 * i], sizes[2 * i + 1], i);
		qsort(order, m, sizeof *order, by_key);
		for (i = 0; i < m; i++) {
			uint32_t k = (uint32_t)(order[i] & 0xffffffffu);
			int32_t w = sizes[2 * k], h = sizes[2 * k + 1], x = 0, y = 0, best_x = 0, best_y = 0;
			int at = 0, best_at = 0, best = -1;
			if (w > page_size || h > page_size) {
				snprintf(err->msg, sizeof err->msg, "pages: rectangle %dx%d larger than a %dx%d page", (int)w, (int)h,
					(int)page_size, (int)page_size);
				goto done;
			}
			/* the lowest place, then the leftmost, on the last open_pages pages (the earliest of equal ones) */
			for (p = count > open_pages ? count - open_pages : 0; p < count; p++) {
				if (sky_find(&pages[p], w, h, page_size, &at, &x, &y)
					&& (best < 0 || y < best_y || (y == best_y && x < best_x))) {
					best = p;
					best_at = at;
					best_x = x;
					best_y = y;
				}
			}
			if (best < 0) {
				if (count == cap) {
					int c = cap ? 2 * cap : 8;
					Skyline *grown = (Skyline *)realloc(pages, (size_t)c * sizeof *grown);
					if (!grown) goto oom;
					pages = grown;
					cap = c;
				}
				if (sky_init(&pages[count], page_size) != 0) goto oom;
				best = count++;
				sky_find(&pages[best], w, h, page_size, &best_at, &best_x, &best_y);
			}
			if (sky_place(&pages[best], best_at, best_x, best_y, w, h) != 0) goto oom;
			out[k - first[0]].page = (uint16_t)best;
			out[k - first[0]].x = (uint16_t)best_x;
			out[k - first[0]].y = (uint16_t)best_y;
		}
	}
	*page_count = count;
	rc = 0;
	goto done;
oom:
	kp_present_fail(err, "pages", "out of memory");
done:
	for (p = 0; p < count; p++) free(pages[p].s);
	free(pages);
	free(order);
	return rc;
}

/* --- a pack built (resources.build_pack without its sounds) --- */

typedef struct {
	uint16_t w, h;
	uint32_t unique; /* the distinct image it shows (w and h nonzero) */
} Image;

typedef struct {
	uint16_t w, h;
	size_t at;     /* its indices in the arena */
	uint32_t hash;
} Distinct;

typedef struct {
	Image *images;
	uint32_t image_count, image_cap;
	Distinct *distinct;
	uint32_t distinct_count, distinct_cap;
	uint32_t *slots; /* the distinct images by hash: index + 1, 0 empty */
	uint32_t slot_count;
	uint8_t *arena;
	size_t arena_size, arena_cap;
} Builder;

static uint32_t fnv(uint16_t w, uint16_t h, const uint8_t *p, size_t n)
{
	uint32_t x = 2166136261u;
	size_t k;
	x = (x ^ w) * 16777619u;
	x = (x ^ h) * 16777619u;
	for (k = 0; k < n; k++) x = (x ^ p[k]) * 16777619u;
	return x;
}

static int grow(void **p, uint32_t *cap, uint32_t need, size_t item)
{
	uint32_t c;
	void *g;
	if (need <= *cap) return 0;
	c = *cap ? *cap : 256;
	while (c < need) c *= 2;
	if (!(g = realloc(*p, (size_t)c * item))) return -1;
	*p = g;
	*cap = c;
	return 0;
}

static int rehash(Builder *b)
{
	uint32_t n = b->slot_count ? 2 * b->slot_count : 4096, k;
	uint32_t *s = (uint32_t *)calloc(n, sizeof *s);
	if (!s) return -1;
	for (k = 0; k < b->distinct_count; k++) {
		uint32_t at = b->distinct[k].hash & (n - 1);
		while (s[at]) at = (at + 1) & (n - 1);
		s[at] = k + 1;
	}
	free(b->slots);
	b->slots = s;
	b->slot_count = n;
	return 0;
}

/* The distinct image of w x h indices: an equal one seen before, else a new one. */
static int distinct_of(Builder *b, uint16_t w, uint16_t h, const uint8_t *pixels, uint32_t *out)
{
	size_t n = (size_t)w * h;
	uint32_t hash = fnv(w, h, pixels, n), at;
	Distinct *d;
	if (2 * (b->distinct_count + 1) > b->slot_count && rehash(b) != 0) return -1;
	for (at = hash & (b->slot_count - 1); b->slots[at]; at = (at + 1) & (b->slot_count - 1)) {
		d = &b->distinct[b->slots[at] - 1];
		if (d->hash == hash && d->w == w && d->h == h && memcmp(b->arena + d->at, pixels, n) == 0) {
			*out = b->slots[at] - 1;
			return 0;
		}
	}
	if (grow((void **)&b->distinct, &b->distinct_cap, b->distinct_count + 1, sizeof *b->distinct) != 0) return -1;
	if (b->arena_size + n > b->arena_cap) {
		size_t c = b->arena_cap ? 2 * b->arena_cap : (size_t)1 << 20;
		uint8_t *g;
		while (c < b->arena_size + n) c *= 2;
		if (!(g = (uint8_t *)realloc(b->arena, c))) return -1;
		b->arena = g;
		b->arena_cap = c;
	}
	memcpy(b->arena + b->arena_size, pixels, n);
	d = &b->distinct[b->distinct_count];
	d->w = w;
	d->h = h;
	d->at = b->arena_size;
	d->hash = hash;
	b->arena_size += n;
	b->slots[at] = b->distinct_count + 1;
	*out = b->distinct_count++;
	return 0;
}

/* The frames of GRP entry e read and added in order: 0, or -1 with the reason. */
static int add_entry(Builder *b, const KpGrpEntry *e, const KpFiles *files, uint8_t *scratch,
	KpError *err)
{
	char path[260];
	uint8_t *grp;
	size_t size;
	uint32_t frames, width, height, f;
	int rc = -1;
	if (e->path.size >= sizeof path) return kp_present_fail(err, "pack", "a GRP path too long");
	memcpy(path, e->path.text, e->path.size);
	path[e->path.size] = 0;
	if (!(grp = kp_files_need(files, path, &size, err))) return -1;
	if (kp_grp_header(grp, size, &frames, &width, &height) != 0 || frames != e->frames) {
		snprintf(err->msg, sizeof err->msg, "pack: %s is not the GRP grpmeta describes", path);
		goto done;
	}
	if (grow((void **)&b->images, &b->image_cap, b->image_count + frames, sizeof *b->images) != 0) goto oom;
	for (f = 0; f < frames; f++) {
		KpGrpFrame r;
		Image *im = &b->images[b->image_count++];
		int opaque_zero = 0;
		kp_grp_frame(grp, f, &r);
		memset(im, 0, sizeof *im);
		if (!r.w || !r.h) continue; /* an empty frame: a zero record */
		if (kp_grp_decode(grp, size, f, scratch, NULL, &opaque_zero) != 0) {
			snprintf(err->msg, sizeof err->msg, "pack: %s frame %u is malformed", path, (unsigned)f);
			goto done;
		}
		if (opaque_zero) {
			snprintf(err->msg, sizeof err->msg, "pack: %s draws opaque index 0; pack pages have no opacity mask", path);
			goto done;
		}
		im->w = r.w;
		im->h = r.h;
		if (distinct_of(b, r.w, r.h, scratch, &im->unique) != 0) goto oom;
	}
	rc = 0;
	goto done;
oom:
	kp_present_fail(err, "pack", "out of memory");
done:
	free(grp);
	return rc;
}

static void builder_free(Builder *b)
{
	free(b->images);
	free(b->distinct);
	free(b->slots);
	free(b->arena);
}

int kp_pack_build(KpPack *p, uint8_t ***pages_out, const KpGrpMeta *g, const KpFiles *files, const char *name,
	KpError *err)
{
	Builder b;
	uint16_t *sizes = NULL;
	KpPagePlace *places = NULL;
	uint8_t **pages = NULL, *scratch = NULL;
	uint32_t a, e, k;
	int page_count = 0, n = 0, rc = -1;
	memset(p, 0, sizeof *p);
	memset(&b, 0, sizeof b);
	*pages_out = NULL;
	for (a = 1; a <= g->atlas_count; a++) n += kp_label_is(&g->atlases[a].pack, name);
	if (!(p->atlases = (KpPackAtlas *)calloc((size_t)(n ? n : 1), sizeof *p->atlases))
		|| !(scratch = (uint8_t *)malloc(255 * 255))) {
		goto oom;
	}
	for (a = 1; a <= g->atlas_count; a++) {
		KpPackAtlas *at;
		if (!kp_label_is(&g->atlases[a].pack, name)) continue;
		at = &p->atlases[p->atlas_count++];
		if (g->atlases[a].name.size >= sizeof at->name) {
			kp_present_fail(err, name, "an atlas name too long");
			goto done;
		}
		memcpy(at->name, g->atlases[a].name.text, g->atlases[a].name.size);
		at->first = b.image_count;
		for (e = 1; e <= g->grp_count; e++)
			if (g->grps[e].atlas == a && add_entry(&b, &g->grps[e], files, scratch, err) != 0) goto done;
		at->count = b.image_count - at->first;
	}
	/* the distinct images packed, the frame records, the pages rendered */
	if (!(sizes = (uint16_t *)malloc(2 * (size_t)(b.distinct_count ? b.distinct_count : 1) * sizeof *sizes))
		|| !(places = (KpPagePlace *)malloc((size_t)(b.distinct_count ? b.distinct_count : 1) * sizeof *places))) {
		goto oom;
	}
	for (k = 0; k < b.distinct_count; k++) {
		sizes[2 * k] = b.distinct[k].w;
		sizes[2 * k + 1] = b.distinct[k].h;
	}
	if (kp_pages_pack(sizes, b.distinct_count, KP_PACK_PAGE_SIZE, places, &page_count, err) != 0) goto done;
	if (!(p->owned = (uint8_t *)malloc((size_t)(b.image_count ? b.image_count : 1) * FRAME_RECORD))) goto oom;
	for (k = 0; k < b.image_count; k++) {
		const Image *im = &b.images[k];
		const KpPagePlace *at = im->w ? &places[im->unique] : NULL;
		uint8_t *r = p->owned + (size_t)k * FRAME_RECORD;
		kp_le_put16(r, at ? at->page : 0);
		kp_le_put16(r + 2, at ? at->x : 0);
		kp_le_put16(r + 4, at ? at->y : 0);
		kp_le_put16(r + 6, im->w);
		kp_le_put16(r + 8, im->h);
	}
	p->frames = p->owned;
	p->frame_count = b.image_count;
	p->page_w = p->page_h = KP_PACK_PAGE_SIZE;
	if (!(pages = (uint8_t **)calloc((size_t)(page_count ? page_count : 1), sizeof *pages))) goto oom;
	p->page_count = page_count;
	for (k = 0; k < (uint32_t)page_count; k++)
		if (!(pages[k] = (uint8_t *)calloc((size_t)KP_PACK_PAGE_SIZE * KP_PACK_PAGE_SIZE, 1))) goto oom;
	for (k = 0; k < b.distinct_count; k++) {
		const Distinct *d = &b.distinct[k];
		uint8_t *page = pages[places[k].page];
		uint32_t row;
		for (row = 0; row < d->h; row++) {
			memcpy(page + ((size_t)places[k].y + row) * KP_PACK_PAGE_SIZE + places[k].x, b.arena + d->at + (size_t)row * d->w,
				d->w);
		}
	}
	*pages_out = pages;
	pages = NULL;
	rc = 0;
	goto done;
oom:
	kp_present_fail(err, name, "out of memory");
done:
	if (pages) {
		for (k = 0; k < (uint32_t)page_count; k++) free(pages[k]);
		free(pages);
	}
	if (rc != 0) kp_pack_free(p);
	free(sizes);
	free(places);
	free(scratch);
	builder_free(&b);
	return rc;
}
