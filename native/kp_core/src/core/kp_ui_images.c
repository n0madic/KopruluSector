/*
 * The HD interface's image log (not the original's: StarCraft: Remastered's art, ADR 0013 R4d): the frames of the
 * interface's files drawn on its surfaces (a GRP frame, a theme frame), with the pixels each wrote, carried along
 * blits; when the canvas composes a surface, each pixel still holding what an image wrote is that image's, and the
 * render draws the image's HD frame there. The log only watches the drawing: the surfaces' pixels are the original's.
 */
#include "kp_ui.h"

#include <stdlib.h>
#include <string.h>


void kp_ui_image_log_free(KpUiImageLog *log)
{
	int32_t k;
	for (k = 0; k < log->count; k++) {
		free(log->images[k].values);
		free(log->images[k].mask);
	}
	free(log->images);
	free(log->owners);
	free(log->writers);
	memset(log, 0, sizeof *log);
}

void kp_ui_set_hd(KpUi *ui, int on)
{
	int32_t k;
	int was = ui->hd;
	if (!on) { /* the images go, the pictures (decoded once) stay */
		KpUiPicture pictures[KP_UI_PICTURES];
		int32_t count = ui->image_log.picture_count;
		memcpy(pictures, ui->image_log.pictures, sizeof pictures);
		kp_ui_image_log_free(&ui->image_log);
		memcpy(ui->image_log.pictures, pictures, sizeof pictures);
		ui->image_log.picture_count = count;
	}
	ui->hd = on;
	if (!on || was) return;
	/* the images drawn before: the console's parts captured again, the visible controls painted again */
	for (k = 0; k < KP_UI_CONTROLS; k++) {
		if (ui->controls[k].console_capture && ui->controls[k].image) kp_ui_capture_console(ui, k);
	}
	for (k = 0; k < KP_UI_CONTROLS; k++) {
		if (ui->controls[k].flags & KP_UI_VISIBLE) kp_ui_mark_dirty(ui, k);
	}
}

static void drop(KpUiImageLog *log, int32_t k)
{
	free(log->images[k].values);
	free(log->images[k].mask);
	memmove(&log->images[k], &log->images[k + 1], sizeof *log->images * (size_t)(log->count - k - 1));
	log->count--;
}

/* The interface file whose bytes start at data. */
static const KpUiFile *file_of(const KpUi *ui, const uint8_t *data)
{
	int k;
	for (k = 0; k < ui->grp_count; k++) {
		if (ui->grps[k].data == data) return &ui->grps[k];
	}
	for (k = 0; k < ui->glue_file_count; k++) {
		if (ui->glue_files[k].data == data) return &ui->glue_files[k];
	}
	for (k = 0; k < ui->pcx_count; k++) {
		if (ui->pcx[k].data == data) return &ui->pcx[k];
	}
	return NULL;
}

static KpUiImage *add(KpUiImageLog *log)
{
	if (log->count == KP_UI_IMAGES_MAX) drop(log, 0);
	if (log->count == log->capacity) {
		int32_t cap = log->capacity ? 2 * log->capacity : 64;
		KpUiImage *grown = (KpUiImage *)realloc(log->images, sizeof *grown * (size_t)cap);
		if (grown == NULL) return NULL;
		log->images = grown;
		log->capacity = cap;
	}
	memset(&log->images[log->count], 0, sizeof *log->images);
	return &log->images[log->count++];
}

static KpUiImage *open_image(KpUi *ui, const KpUiSurface *t, const KpUiFile *f, int32_t frame, int32_t x, int32_t y,
	int32_t w, int32_t h)
{
	KpUiImageLog *log = &ui->image_log;
	KpUiImage *im;
	if (t == NULL || t->pixels == NULL || f == NULL || w <= 0 || h <= 0) return NULL;
	if ((im = add(log)) == NULL) return NULL;
	im->values = (uint8_t *)malloc((size_t)w * (size_t)h);
	im->mask = (uint8_t *)calloc((size_t)w * (size_t)h, 1);
	if (im->values == NULL || im->mask == NULL) {
		drop(log, log->count - 1);
		return NULL;
	}
	im->surface = t->pixels;
	im->surface_w = t->w;
	im->surface_h = t->h;
	im->path = f->path;
	im->path_len = f->path_len;
	im->frame = frame;
	im->x = x;
	im->y = y;
	im->w = w;
	im->h = h;
	im->item[0] = im->canvas[0] = x;
	im->item[1] = im->canvas[1] = y;
	im->item[2] = im->canvas[2] = w;
	im->item[3] = im->canvas[3] = h;
	im->serial = ++log->serial;
	return im;
}

KpUiImage *kp_ui_image_begin(KpUi *ui, const uint8_t *file_data, int32_t frame, int32_t x, int32_t y, int32_t w,
	int32_t h)
{
	if (!ui->hd) return NULL;
	ui->image_log.open = open_image(ui, ui->text.target, file_of(ui, file_data), frame, x, y, w, h);
	return ui->image_log.open;
}

void kp_ui_image_whole(KpUi *ui, KpUiSurface *s, const KpUiFile *f, int32_t frame, const int32_t item[4], int keyed)
{
	KpUiImage *im;
	int32_t i, j;
	if (!ui->hd || s == NULL || s->pixels == NULL) return;
	if ((im = open_image(ui, s, f, frame, 0, 0, s->w, s->h)) == NULL) return;
	memcpy(im->item, item, sizeof im->item);
	for (j = 0; j < s->h; j++) {
		for (i = 0; i < s->w; i++) {
			uint8_t v = s->pixels[(size_t)j * (size_t)s->w + (size_t)i];
			if (!keyed || v != 0) kp_ui_image_put(im, i, j, v);
		}
	}
	kp_ui_image_end(ui, im, 0);
}

void kp_ui_video_image(KpUi *ui, const KpUiVideo *v, KpUiSurface *s)
{
	KpUiFile f;
	int32_t item[4];
	if (!ui->hd || v->path == NULL || v->shown < 0 || s == NULL) return;
	memset(&f, 0, sizeof f);
	f.path = v->path;
	f.path_len = v->path_len;
	item[0] = item[1] = 0;
	item[2] = s->w;
	item[3] = s->h;
	kp_ui_image_whole(ui, s, &f, v->shown, item, 0);
}

#define SAMPLE_STEP 61 /* the pixels a picture's hash reads: one in so many */

static uint64_t sample(const uint8_t *pixels, int32_t w, int32_t h)
{
	uint64_t hash = 1469598103934665603ull;
	size_t k, n = (size_t)w * (size_t)h;
	for (k = 0; k < n; k += SAMPLE_STEP) hash = (hash ^ pixels[k]) * 1099511628211ull;
	return hash;
}

void kp_ui_picture(KpUi *ui, const uint8_t *pixels, int32_t w, int32_t h, const KpUiFile *f)
{
	KpUiImageLog *log = &ui->image_log;
	int32_t k;
	if (pixels == NULL || f == NULL || w <= 0 || h <= 0) return;
	for (k = 0; k < log->picture_count && log->pictures[k].pixels != pixels; k++) {}
	if (k == log->picture_count) {
		if (log->picture_count == KP_UI_PICTURES) { /* the oldest forgotten */
			memmove(log->pictures, log->pictures + 1, sizeof *log->pictures * (KP_UI_PICTURES - 1));
			k = KP_UI_PICTURES - 1;
		} else {
			log->picture_count++;
		}
	}
	log->pictures[k].pixels = pixels;
	log->pictures[k].w = w;
	log->pictures[k].h = h;
	log->pictures[k].sample = sample(pixels, w, h);
	log->pictures[k].file = f;
}

/* A blit's source as a whole image when it is a picture (a decoded PCX) or a frame in a GRP file's bytes: its file,
 * frame and the item's rectangle on the source; 0 none. */
static int source_image(KpUi *ui, const KpUiSurface *src, const KpUiFile **file, int32_t *frame, int32_t item[4])
{
	KpUiImageLog *log = &ui->image_log;
	int32_t k;
	for (k = 0; k < log->picture_count; k++) {
		const KpUiPicture *p = &log->pictures[k];
		if (p->pixels == src->pixels && p->w == src->w && p->h == src->h && p->sample == sample(src->pixels, src->w,
			src->h)) {
			*file = p->file;
			*frame = 0;
			item[0] = item[1] = 0;
			item[2] = src->w;
			item[3] = src->h;
			return 1;
		}
	}
	for (k = 0; k < ui->grp_count; k++) { /* an uncompressed frame in a GRP's bytes (kp_ui_grp_bitmap) */
		const KpUiFile *f = &ui->grps[k];
		uint32_t count, j;
		if (src->pixels < f->data || src->pixels >= f->data + f->size || f->size < 6) continue;
		count = (uint32_t)(f->data[0] | f->data[1] << 8) & 0x7fff;
		for (j = 0; j < count && 6 + 8 * (size_t)(j + 1) <= f->size; j++) {
			const uint8_t *r = f->data + 6 + 8 * (size_t)j;
			uint32_t off = (uint32_t)(r[4] | r[5] << 8 | r[6] << 16 | (uint32_t)r[7] << 24) & 0x7fffffffu;
			if (f->data + off == src->pixels && r[2] == src->w && r[3] == src->h) {
				*file = f;
				*frame = (int32_t)j;
				item[0] = item[1] = 0;
				item[2] = src->w;
				item[3] = src->h;
				return 1;
			}
		}
	}
	return 0;
}

void kp_ui_image_put(KpUiImage *im, int32_t x, int32_t y, uint8_t value)
{
	int32_t i = x - im->x, j = y - im->y;
	if (i < 0 || j < 0 || i >= im->w || j >= im->h) return;
	im->values[(size_t)j * (size_t)im->w + (size_t)i] = value;
	im->mask[(size_t)j * (size_t)im->w + (size_t)i] = 1;
	im->written = 1;
}

/* Whether every pixel b wrote is one a (drawn later) wrote over. */
static int covered(const KpUiImage *a, const KpUiImage *b)
{
	int32_t i, j;
	for (j = 0; j < b->h; j++) {
		for (i = 0; i < b->w; i++) {
			int32_t x = b->x + i - a->x, y = b->y + j - a->y;
			if (!b->mask[(size_t)j * (size_t)b->w + (size_t)i]) continue;
			if (x < 0 || y < 0 || x >= a->w || y >= a->h || !a->mask[(size_t)y * (size_t)a->w + (size_t)x]) return 0;
		}
	}
	return 1;
}

void kp_ui_image_end(KpUi *ui, KpUiImage *im, int tinted)
{
	KpUiImageLog *log = &ui->image_log;
	int32_t k, index = (int32_t)(im - log->images);
	if (log->open == im) log->open = NULL;
	if (index < 0 || index >= log->count) return;
	if (!im->written) {
		drop(log, index);
		return;
	}
	if (tinted) { /* the indices it wrote most */
		uint32_t counts[256];
		size_t p;
		int s;
		memset(counts, 0, sizeof counts);
		for (p = 0; p < (size_t)im->w * (size_t)im->h; p++) {
			if (im->mask[p]) counts[im->values[p]]++;
		}
		for (s = 0; s < KP_UI_TINTS; s++) {
			int best = -1, v;
			for (v = 0; v < 256; v++) {
				if (counts[v] && (best < 0 || counts[v] > counts[best])) best = v;
			}
			if (best < 0) break;
			im->tint_index[s] = (uint8_t)best;
			im->tint_count[s] = counts[best];
			counts[best] = 0;
		}
		im->tinted = 1;
	}
	/* the images of the same surface it wrote over whole are gone */
	for (k = index - 1; k >= 0; k--) {
		KpUiImage *b = &log->images[k];
		if (b->surface == im->surface && b->x < im->x + im->w && im->x < b->x + b->w && b->y < im->y + im->h
			&& im->y < b->y + b->h && covered(im, b)) {
			drop(log, k);
			index--;
			im = &log->images[index];
		}
	}
}

void kp_ui_image_erase(KpUi *ui, const KpUiSurface *t, const int32_t rect[4], const KpUiSurface *src, int32_t sx,
	int32_t sy)
{
	KpUiImageLog *log = &ui->image_log;
	int32_t k;
	if (!ui->hd || t == NULL || t->pixels == NULL) return;
	for (k = log->count - 1; k >= 0; k--) {
		KpUiImage *im = &log->images[k];
		int32_t x0 = rect[0], y0 = rect[1], x1 = rect[2], y1 = rect[3], i, j, erased = 0;
		size_t p, n = (size_t)im->w * (size_t)im->h;
		if (im == log->open || im->surface != t->pixels || im->surface_w != t->w || im->surface_h != t->h) continue;
		if (x0 < im->x) x0 = im->x;
		if (y0 < im->y) y0 = im->y;
		if (x1 > im->x + im->w) x1 = im->x + im->w;
		if (y1 > im->y + im->h) y1 = im->y + im->h;
		if (x0 < 0) x0 = 0;
		if (y0 < 0) y0 = 0;
		if (x1 > t->w) x1 = t->w;
		if (y1 > t->h) y1 = t->h;
		for (j = y0; j < y1; j++) {
			for (i = x0; i < x1; i++) {
				uint8_t *m = &im->mask[(size_t)(j - im->y) * (size_t)im->w + (size_t)(i - im->x)];
				if (!*m) continue;
				if (src != NULL && src->pixels[(size_t)(sy + j - rect[1]) * (size_t)src->w + (size_t)(sx + i - rect[0])] == 0) {
					continue; /* index 0 of a keyed source writes nothing */
				}
				*m = 0;
				erased = 1;
			}
		}
		if (!erased || log->open != NULL) continue; /* an image being drawn holds its place in the log */
		for (p = 0; p < n && !im->mask[p]; p++) {}
		if (p == n) drop(log, k);
	}
}

void kp_ui_image_blit(KpUi *ui, const KpUiSurface *src, const KpUiSurface *t, int32_t sx, int32_t sy,
	const int32_t rect[4], int keyed)
{
	KpUiImageLog *log = &ui->image_log;
	int32_t k, n = 0, dx = rect[0] - sx, dy = rect[1] - sy, frame, item[4];
	uint32_t *serials;
	const KpUiFile *file;
	if (!ui->hd || src == NULL || src->pixels == NULL || t == NULL || t->pixels == NULL) return;
	/* the pixels the blit wrote are no longer the target's images': the source's own come along with them */
	kp_ui_image_erase(ui, t, rect, keyed ? src : NULL, sx, sy);
	if (source_image(ui, src, &file, &frame, item)) { /* a picture or a GRP frame: an image of its file */
		KpUiImage *im = open_image(ui, t, file, frame, rect[0], rect[1], rect[2] - rect[0], rect[3] - rect[1]);
		int32_t i, j;
		if (im == NULL) return;
		im->item[0] = item[0] + dx;
		im->item[1] = item[1] + dy;
		im->item[2] = item[2];
		im->item[3] = item[3];
		for (j = rect[1]; j < rect[3]; j++) {
			for (i = rect[0]; i < rect[2]; i++) {
				if (i < 0 || j < 0 || i >= t->w || j >= t->h) continue;
				if (src->pixels[(size_t)(j - dy) * (size_t)src->w + (size_t)(i - dx)] == 0
					&& t->pixels[(size_t)j * (size_t)t->w + (size_t)i] != 0) {
					continue; /* index 0 not written by a keyed blit */
				}
				kp_ui_image_put(im, i, j, t->pixels[(size_t)j * (size_t)t->w + (size_t)i]);
			}
		}
		kp_ui_image_end(ui, im, 0);
		return;
	}
	if (log->count == 0) return;
	/* the source's images in the blit, by serial: carrying them drops the target's images they cover */
	if ((serials = (uint32_t *)malloc(sizeof *serials * (size_t)log->count)) == NULL) return;
	for (k = 0; k < log->count; k++) {
		const KpUiImage *b = &log->images[k];
		if (b->surface != src->pixels || b->surface_w != src->w || b->surface_h != src->h) continue;
		if (b->x + dx >= rect[2] || b->x + b->w + dx <= rect[0] || b->y + dy >= rect[3] || b->y + b->h + dy <= rect[1]) {
			continue;
		}
		serials[n++] = b->serial;
	}
	while (n-- > 0) {
		KpUiImage b, *im;
		KpUiFile f;
		int32_t i, j, at;
		for (at = 0; at < log->count && log->images[at].serial != serials[n]; at++) {}
		if (at == log->count) continue;
		b = log->images[at];
		f.path = b.path;
		f.path_len = b.path_len;
		f.data = NULL;
		f.size = 0;
		if ((im = open_image(ui, t, &f, b.frame, b.x + dx, b.y + dy, b.w, b.h)) == NULL) continue;
		for (at = 0; at < log->count && log->images[at].serial != serials[n]; at++) {}
		if (at == log->count) { /* the source was the oldest image, dropped for the new one (the log full) */
			kp_ui_image_end(ui, im, 0);
			continue;
		}
		b = log->images[at]; /* the array may have moved */
		im->item[0] = b.item[0] + dx;
		im->item[1] = b.item[1] + dy;
		im->item[2] = b.item[2];
		im->item[3] = b.item[3];
		im->canvas[0] = b.canvas[0] + dx;
		im->canvas[1] = b.canvas[1] + dy;
		im->canvas[2] = b.canvas[2];
		im->canvas[3] = b.canvas[3];
		memcpy(im->parts, b.parts, sizeof im->parts);
		for (j = 0; j < b.h; j++) {
			for (i = 0; i < b.w; i++) {
				int32_t x = b.x + dx + i, y = b.y + dy + j;
				if (!b.mask[(size_t)j * (size_t)b.w + (size_t)i] || x < rect[0] || y < rect[1] || x >= rect[2]
					|| y >= rect[3]) {
					continue;
				}
				kp_ui_image_put(im, x, y, t->pixels[(size_t)y * (size_t)t->w + (size_t)x]);
			}
		}
		im->tinted = b.tinted;
		memcpy(im->tint_index, b.tint_index, sizeof im->tint_index);
		memcpy(im->tint_count, b.tint_count, sizeof im->tint_count);
		kp_ui_image_end(ui, im, 0);
	}
	free(serials);
}

static uint16_t add_item(KpUiCanvas *c, const KpUiImage *im, int32_t x, int32_t y, uint16_t under)
{
	KpUiCanvasItem it;
	memset(&it, 0, sizeof it);
	it.path = im->path;
	it.path_len = im->path_len;
	it.frame = im->frame;
	it.x = x + im->item[0];
	it.y = y + im->item[1];
	it.w = im->item[2];
	it.h = im->item[3];
	it.column = -1;
	it.tinted = im->tinted;
	memcpy(it.tint_index, im->tint_index, sizeof it.tint_index);
	memcpy(it.tint_count, im->tint_count, sizeof it.tint_count);
	it.under = under;
	it.canvas[0] = x + im->canvas[0];
	it.canvas[1] = y + im->canvas[1];
	it.canvas[2] = im->canvas[2];
	it.canvas[3] = im->canvas[3];
	memcpy(it.parts, im->parts, sizeof it.parts);
	return kp_ui_canvas_add_item(c, &it);
}

/* An image's item, made (with the item it lies over) on its first use. */
static uint16_t item_of(KpUiImageLog *log, KpUiCanvas *c, uint16_t *items, const int32_t *under, int32_t k, int32_t x,
	int32_t y)
{
	if (!items[k + 1]) {
		uint16_t u = under[k] >= 0 && under[k] != k ? item_of(log, c, items, under, under[k], x, y) : 0;
		items[k + 1] = add_item(c, &log->images[k], x, y, u);
	}
	return items[k + 1];
}

static int32_t area(const KpUiImage *im) { return im->w * im->h; }

/* Whether two images of a surface are pieces of one picture: a dialog's background carried again by the blits of
 * partial redraws, each piece with only the pixels of its clip. */
static int same_art(const KpUiImage *a, const KpUiImage *b)
{
	return a->surface == b->surface && a->path_len == b->path_len && memcmp(a->path, b->path, a->path_len) == 0
		&& a->frame == b->frame && memcmp(a->item, b->item, sizeof a->item) == 0 && a->x == b->x && a->y == b->y
		&& a->w == b->w && a->h == b->h;
}

const uint16_t *kp_ui_image_owners(KpUi *ui, KpUiCanvas *c, const KpUiSurface *s, const uint8_t *src, int32_t x,
	int32_t y)
{
	KpUiImageLog *log = &ui->image_log;
	size_t n = (size_t)s->w * (size_t)s->h, p;
	int32_t k, *under = NULL, *art;
	uint16_t *items = NULL;
	int any = 0;
	if (!ui->hd || c->ids_next == NULL || s->pixels == NULL || n == 0) return NULL;
	for (k = 0; k < log->count && !any; k++) any = log->images[k].surface == s->pixels;
	if (!any) return NULL;
	if (log->owners_size < n) {
		uint16_t *grown = (uint16_t *)realloc(log->owners, n * sizeof *grown);
		int32_t *writers = (int32_t *)realloc(log->writers, n * sizeof *writers);
		if (grown) log->owners = grown;
		if (writers) log->writers = writers;
		if (grown == NULL || writers == NULL) return NULL;
		log->owners_size = n;
	}
	memset(log->owners, 0, n * sizeof *log->owners);
	memset(log->writers, 0, n * sizeof *log->writers);
	/* each pixel's writer: the last image (in the order drawn) whose pixel it still is */
	for (k = 0; k < log->count; k++) {
		KpUiImage *im = &log->images[k];
		int32_t i, j, owned = 0;
		if (im->surface != s->pixels) continue;
		if (im->surface_w != s->w || im->surface_h != s->h) { /* another surface at a freed one's address */
			drop(log, k--);
			continue;
		}
		for (j = 0; j < im->h; j++) {
			for (i = 0; i < im->w; i++) {
				size_t at = (size_t)j * (size_t)im->w + (size_t)i;
				int32_t sx = im->x + i, sy = im->y + j;
				if (!im->mask[at] || sx < 0 || sy < 0 || sx >= s->w || sy >= s->h) continue;
				p = (size_t)sy * (size_t)s->w + (size_t)sx;
				if (src[p] != im->values[at]) continue;
				log->writers[p] = k + 1;
				owned++;
			}
		}
		if (owned == 0) drop(log, k--); /* drawn over whole since */
	}
	/* each pixel's image: its writer, or a smaller image whose rectangle covers it and lies over the writer (the larger
	 * image that wrote most of its rectangle; a frame's piece whose rectangle reaches over a portrait takes none of it)
	 * when the writer was drawn first (an icon over its button: the HD icon over the button's pixels too) or the pixel
	 * still holds what the smaller one wrote (a wireframe under its panel painted again with the same pixels), or a
	 * tinted image over the plain background of its rectangle (its most common plain value); the pieces of one
	 * picture count as one image, the first of them (art) */
	items = (uint16_t *)calloc((size_t)log->count + 1, sizeof *items);
	under = (int32_t *)malloc(sizeof *under * (size_t)(log->count ? 2 * log->count : 1));
	if (items == NULL || under == NULL) {
		free(items);
		free(under);
		return NULL;
	}
	art = under + log->count;
	for (k = 0; k < log->count; k++) {
		int32_t j;
		under[k] = -1;
		for (j = 0; j < k && !same_art(&log->images[j], &log->images[k]); j++) {}
		art[k] = j;
	}
	for (p = 0; p < n; p++) log->writers[p] -= 1; /* image indexes, -1 none */
	{
		int32_t *best = (int32_t *)malloc(sizeof *best * n);
		uint32_t *written = (uint32_t *)calloc((size_t)(log->count ? log->count : 1), sizeof *written);
		if (best == NULL || written == NULL) {
			free(best);
			free(written);
			free(items);
			free(under);
			return NULL;
		}
		memcpy(best, log->writers, sizeof *best * n);
		for (k = 0; k < log->count; k++) {
			KpUiImage *im = &log->images[k];
			uint32_t counts[256];
			int32_t i, j, background = -1, v, w, px, py, r[4];
			if (im->surface != s->pixels) continue;
			/* the image it lies over: the larger one that wrote most of its rectangle's pixels (the lowest index of
			 * equals), counted over the rectangle, whose counts are zeroed again after */
			for (j = 0; j < im->h; j++) {
				for (i = 0; i < im->w; i++) {
					int32_t sx = im->x + i, sy = im->y + j, u = under[k];
					if (sx < 0 || sy < 0 || sx >= s->w || sy >= s->h) continue;
					w = log->writers[(size_t)sy * (size_t)s->w + (size_t)sx];
					if (w < 0 || art[w] == art[k] || area(im) >= area(&log->images[w])) continue;
					w = art[w];
					written[w]++;
					if (u < 0 || written[w] > written[u] || (written[w] == written[u] && w < u)) under[k] = w;
				}
			}
			for (j = 0; j < im->h; j++) {
				for (i = 0; i < im->w; i++) {
					int32_t sx = im->x + i, sy = im->y + j;
					if (sx < 0 || sy < 0 || sx >= s->w || sy >= s->h) continue;
					w = log->writers[(size_t)sy * (size_t)s->w + (size_t)sx];
					if (w >= 0) written[art[w]] = 0;
				}
			}
			if (im->tinted) {
				memset(counts, 0, sizeof counts);
				for (j = 0; j < im->h; j++) {
					for (i = 0; i < im->w; i++) {
						int32_t sx = im->x + i, sy = im->y + j;
						if (sx < 0 || sy < 0 || sx >= s->w || sy >= s->h) continue;
						p = (size_t)sy * (size_t)s->w + (size_t)sx;
						if (log->writers[p] < 0) counts[src[p]]++;
					}
				}
				for (v = 0; v < 256; v++) {
					if (counts[v] && (background < 0 || counts[v] > counts[background])) background = v;
				}
			}
			/* a tinted frame (a wireframe, an icon) over its whole GRP's rectangle too: its HD art, drawn there, may
			 * reach past the SD frame's */
			r[0] = im->x;
			r[1] = im->y;
			r[2] = im->x + im->w;
			r[3] = im->y + im->h;
			if (im->tinted && im->canvas[2] > 0 && im->canvas[3] > 0) {
				if (im->canvas[0] < r[0]) r[0] = im->canvas[0];
				if (im->canvas[1] < r[1]) r[1] = im->canvas[1];
				if (im->canvas[0] + im->canvas[2] > r[2]) r[2] = im->canvas[0] + im->canvas[2];
				if (im->canvas[1] + im->canvas[3] > r[3]) r[3] = im->canvas[1] + im->canvas[3];
			}
			for (py = r[1]; py < r[3]; py++) {
				for (px = r[0]; px < r[2]; px++) {
					int32_t b;
					int mine;
					if (px < 0 || py < 0 || px >= s->w || py >= s->h) continue;
					p = (size_t)py * (size_t)s->w + (size_t)px;
					w = log->writers[p];
					b = best[p];
					if (w == k) continue;
					i = px - im->x;
					j = py - im->y;
					mine = i >= 0 && j >= 0 && i < im->w && j < im->h && im->mask[(size_t)j * (size_t)im->w + (size_t)i]
						&& im->values[(size_t)j * (size_t)im->w + (size_t)i] == src[p];
					if (w >= 0 ? art[w] == under[k] && (log->images[w].serial < im->serial || mine)
							: im->tinted && (int)src[p] == background) {
						if (b < 0 || b == w || area(im) < area(&log->images[b])) best[p] = k;
					}
				}
			}
		}
		for (p = 0; p < n; p++) {
			if (best[p] >= 0 && !(log->owners[p] = item_of(log, c, items, under, best[p], x, y))) break; /* items full */
		}
		free(best);
		free(written);
	}
	free(items);
	free(under);
	return log->owners;
}

const KpUiImage *kp_ui_image_last(const KpUi *ui, const uint8_t *surface)
{
	int32_t k;
	for (k = ui->image_log.count - 1; k >= 0; k--) {
		if (ui->image_log.images[k].surface == surface) return &ui->image_log.images[k];
	}
	return NULL;
}
