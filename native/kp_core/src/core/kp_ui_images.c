/*
 * The HD interface's image log (not the original's: StarCraft: Remastered's art, ADR 0013 R4d): the frames of the
 * interface's files drawn on its surfaces (a GRP frame, a theme frame), with the pixels each wrote, carried along
 * blits; when the canvas composes a surface, each pixel still holding what an image wrote is that image's, and the
 * render draws the image's HD frame there. The log only watches the drawing: the surfaces' pixels are the original's.
 */
#include "kp_ui.h"

#include <stdlib.h>
#include <string.h>

#define IMAGES_MAX 4096 /* past it the oldest images are dropped */

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
	if (!on) kp_ui_image_log_free(&ui->image_log);
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

static int is_wireframe(const KpUiFile *f)
{
	/* TODO: the HD wireframes are layers coloured by the parts' hit points and shields (StarCraft: Remastered's
	 * wirefram.dds.grp: four hit point parts, two shield outlines); they stay SD until drawn so */
	size_t k;
	for (k = 0; k + 8 <= f->path_len; k++) {
		if (memcmp(f->path + k, "wirefram", 8) == 0) return 1;
	}
	return 0;
}

static KpUiImage *add(KpUiImageLog *log)
{
	if (log->count == IMAGES_MAX) drop(log, 0);
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
	if (t == NULL || t->pixels == NULL || f == NULL || w <= 0 || h <= 0 || is_wireframe(f)) return NULL;
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
	im->item[0] = x;
	im->item[1] = y;
	im->item[2] = w;
	im->item[3] = h;
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
	if (tinted) { /* the four indices it wrote most */
		uint32_t counts[256];
		size_t p;
		int s;
		memset(counts, 0, sizeof counts);
		for (p = 0; p < (size_t)im->w * (size_t)im->h; p++) {
			if (im->mask[p]) counts[im->values[p]]++;
		}
		for (s = 0; s < 4; s++) {
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

void kp_ui_image_blit(KpUi *ui, const KpUiSurface *src, const KpUiSurface *t, int32_t sx, int32_t sy,
	const int32_t rect[4])
{
	KpUiImageLog *log = &ui->image_log;
	int32_t k, n = 0, dx = rect[0] - sx, dy = rect[1] - sy;
	uint32_t *serials;
	if (!ui->hd || src == NULL || t == NULL || t->pixels == NULL || log->count == 0) return;
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
		b = log->images[at]; /* the array may have moved */
		im->item[0] = b.item[0] + dx;
		im->item[1] = b.item[1] + dy;
		im->item[2] = b.item[2];
		im->item[3] = b.item[3];
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
	return kp_ui_canvas_add_item(c, &it);
}

const uint16_t *kp_ui_image_owners(KpUi *ui, KpUiCanvas *c, const KpUiSurface *s, const uint8_t *src, int32_t x,
	int32_t y)
{
	KpUiImageLog *log = &ui->image_log;
	size_t n = (size_t)s->w * (size_t)s->h;
	int32_t k;
	uint16_t *items;
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
				size_t at = (size_t)j * (size_t)im->w + (size_t)i, p;
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
	/* the items: a writer's; over a writer, a later image whose rectangle covers the pixel (an icon over its
	 * button: the button's item under the icon's) */
	items = (uint16_t *)calloc((size_t)log->count + 1, sizeof *items);
	if (items == NULL) return NULL;
	for (k = 0; k < log->count; k++) {
		KpUiImage *im = &log->images[k];
		int32_t i, j, pass;
		uint16_t under = 0;
		if (im->surface != s->pixels) continue;
		for (pass = 0; pass < 2; pass++) { /* the item under it first (the first earlier image under its rectangle) */
			for (j = 0; j < im->h; j++) {
				for (i = 0; i < im->w; i++) {
					int32_t sx = im->x + i, sy = im->y + j, w;
					size_t p;
					if (sx < 0 || sy < 0 || sx >= s->w || sy >= s->h) continue;
					p = (size_t)sy * (size_t)s->w + (size_t)sx;
					w = log->writers[p] - 1;
					if (w < 0 || w > k || (w < k && !items[w + 1])) continue; /* not an image's, or a later one's */
					if (pass == 0) {
						if (w < k && !under) under = items[w + 1];
						continue;
					}
					if (!items[k + 1] && !(items[k + 1] = add_item(c, im, x, y, under))) goto full;
					log->owners[p] = items[k + 1];
				}
			}
		}
	}
full:
	free(items);
	return log->owners;
}
