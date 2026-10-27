/*
 * The HD interface (ADR 0013, stage R4d): StarCraft: Remastered's art for the canvas's items (kp_ui's console pieces,
 * frames and pictures), from generated/hd/<variant>/ui.kpd of the variant --graphics and the scale pick. Each frame
 * the canvas's items are resolved to their HD frame and its rectangle on the logical screen, the frames not yet in the
 * atlas are read, decoded and placed there, and the item table goes to kp_gfx, which draws an item's image where the
 * canvas's item plane names it.
 */
#include "kp_app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define UI_ATLAS_LAYERS 4
#define UI_LUMA_MIN (8.0f / 255.0f)

void kp_hd_ui_free(KpApp *a)
{
	KpAppHdUi *u = &a->hd_ui;
	int failed[2];
	memcpy(failed, u->failed, sizeof failed); /* a variant not imported stays so */
	free(u->head);
	free(u->items);
	kp_hd_ui_atlas_free(&u->atlas);
	memset(u, 0, sizeof *u);
	memcpy(u->failed, failed, sizeof failed);
}

/* ui.kpd of a variant opened, its atlas made; -1 when it is not there. */
static int open_variant(KpApp *a, const char *variant)
{
	KpAppHdUi *u = &a->hd_ui;
	char path[256];
	size_t n, file_size, need;
	uint8_t *first;
	KpError e;
	snprintf(path, sizeof path, "generated/hd/%s/ui.kpd", variant);
	if (!(first = kp_app_read_head(a, path, KP_HD_PACK_FIRST_READ, &n, &file_size))) return -1;
	need = kp_hd_ui_head_size(first, n, file_size);
	free(first);
	if (need == 0 || !(u->head = kp_app_read_head(a, path, need, &n, &file_size)) || n != need
		|| kp_hd_ui_open(&u->index, u->head, n, file_size, path, &e) != 0) {
		SDL_Log("hd ui: %s: %s", path, need ? e.msg : "not an HD interface file");
		return -1;
	}
	kp_app_path(a, path, u->path, sizeof u->path);
	if (kp_hd_ui_atlas_init(&u->atlas, u->index.scale == 4 ? 4096 : 2048, UI_ATLAS_LAYERS, u->index.frame_count) != 0) {
		return -1;
	}
	snprintf(u->variant, sizeof u->variant, "%s", variant);
	u->gfx_generation = 0; /* kp_gfx's atlas is made on the first frame */
	SDL_Log("hd ui: StarCraft: Remastered's %s interface art", variant);
	return 0;
}

static int variant_index(const char *v) { return strcmp(v, "hd") == 0 ? 0 : 1; }

void kp_hd_ui_mode(KpApp *a, KpCanvas *c)
{
	KpAppHdUi *u = &a->hd_ui;
	const char *want = NULL, *v;
	int k, on;
	for (k = 0; (v = kp_app_hd_variant(a, k)) != NULL; k++) { /* the first variant open or not known missing */
		if ((u->head && strcmp(v, u->variant) == 0) || !u->failed[variant_index(v)]) {
			want = v;
			break;
		}
	}
	if (want && (!u->head || strcmp(want, u->variant) != 0)) {
		kp_hd_ui_free(a);
		if (open_variant(a, want) != 0) {
			kp_hd_ui_free(a);
			u->failed[variant_index(want)] = 1; /* not imported: not tried again */
		}
	} else if (!want && u->head) {
		kp_hd_ui_free(a);
	}
	on = u->head != NULL;
	if (a->ui && a->ui->hd != on) kp_ui_set_hd(a->ui, on); /* the interface's image log */
	if (c && (c->canvas.ids != NULL) != on) kp_ui_canvas_hd(&c->canvas, on);
	if (c) c->hd_ui = on ? &u->index : NULL;
}

/* A frame's image read and decoded to RGBA8 (w x h), or NULL. */
static uint8_t *frame_texels(KpAppHdUi *u, const KpHdUiFrame *f)
{
	SDL_IOStream *io = SDL_IOFromFile(u->path, "rb");
	uint8_t *data = (uint8_t *)malloc(f->size ? f->size : 1), *rgba = NULL;
	if (!io || !data || SDL_SeekIO(io, (Sint64)f->offset, SDL_IO_SEEK_SET) < 0 || SDL_ReadIO(io, data, f->size) != f->size) {
		if (io) SDL_CloseIO(io);
		free(data);
		return NULL;
	}
	SDL_CloseIO(io);
	if (f->format == KP_HD_UI_PNG) {
		SDL_Surface *s = SDL_LoadPNG_IO(SDL_IOFromConstMem(data, f->size), true), *c;
		int32_t r;
		if (s && (c = SDL_ConvertSurface(s, SDL_PIXELFORMAT_RGBA32)) != NULL) {
			if (c->w == f->w && c->h == f->h && (rgba = (uint8_t *)malloc((size_t)f->w * (size_t)f->h * 4)) != NULL) {
				for (r = 0; r < c->h; r++) {
					memcpy(rgba + (size_t)r * (size_t)f->w * 4, (const uint8_t *)c->pixels + (size_t)r * (size_t)c->pitch,
						(size_t)f->w * 4);
				}
			}
			SDL_DestroySurface(c);
		}
		if (s) SDL_DestroySurface(s);
	} else { /* BC blocks over the size rounded up to 4, cropped */
		int32_t w4 = (f->w + 3) / 4 * 4, h4 = (f->h + 3) / 4 * 4, r;
		size_t need = (size_t)(w4 / 4) * (size_t)(h4 / 4) * (f->format == KP_HD_UI_BC3 ? 16 : 8);
		uint8_t *full = (uint8_t *)malloc((size_t)w4 * (size_t)h4 * 4);
		if (full && f->size >= need && (rgba = (uint8_t *)malloc((size_t)f->w * (size_t)f->h * 4)) != NULL) {
			kp_bcn_decode(data, w4, h4, f->format == KP_HD_UI_BC3, full);
			for (r = 0; r < f->h; r++) memcpy(rgba + (size_t)r * (size_t)f->w * 4, full + (size_t)r * (size_t)w4 * 4, (size_t)f->w * 4);
		}
		free(full);
	}
	free(data);
	return rgba;
}

/* The mean luminance of an image's opaque texels (what a tinted frame's colour is divided by). */
static uint8_t mean_luma(const uint8_t *rgba, int32_t w, int32_t h)
{
	uint64_t sum = 0, n = 0;
	size_t k;
	for (k = 0; k < (size_t)w * (size_t)h; k++) {
		const uint8_t *p = rgba + 4 * k;
		if (p[3] < 128) continue;
		sum += (uint64_t)(77 * p[0] + 151 * p[1] + 28 * p[2]) >> 8;
		n++;
	}
	return (uint8_t)(n ? sum / n : 0);
}

/* An item's HD frame and its image's rectangle on the logical screen and texels in the frame (x0, y0, x1, y1); 0 when
 * it has no HD art. */
static int resolve(const KpAppHdUi *u, const KpUiCanvasItem *it, int32_t canvas_w, int32_t *frame, float rect[4],
	float src[4], int *replaces)
{
	int32_t first, count;
	int flags;
	KpHdUiFrame f;
	float s;
	if (kp_hd_ui_asset(&u->index, it->path, it->path_len, &first, &count, &flags) < 0 || it->frame < 0
		|| it->frame >= count) {
		return 0;
	}
	*frame = first + it->frame;
	kp_hd_ui_frame(&u->index, *frame, &f);
	s = (float)u->index.scale;
	src[0] = 0;
	src[1] = 0;
	src[2] = (float)f.w;
	src[3] = (float)f.h;
	*replaces = (flags & KP_HD_UI_CONSOLE) != 0;
	if (flags & KP_HD_UI_CONSOLE) { /* a console piece where it matched the SD console, its own silhouette */
		rect[0] = (float)(it->x + f.x);
		rect[1] = (float)(it->y + f.y);
		rect[2] = (float)f.w / s;
		rect[3] = (float)f.h / s;
		if (it->column >= 0) { /* a filler: the piece's column, stretched across the screen */
			src[0] = (float)(it->column - f.x) * s;
			src[2] = src[0] + 1;
			rect[0] = 0;
			rect[2] = (float)canvas_w;
		}
		return src[0] >= 0 && src[2] <= (float)f.w;
	}
	/* a frame or a picture: its HD image centred on the SD one */
	rect[2] = (float)f.w / s;
	rect[3] = (float)f.h / s;
	rect[0] = (float)it->x + ((float)it->w - rect[2]) / 2;
	rect[1] = (float)it->y + ((float)it->h - rect[3]) / 2;
	return 1;
}

void kp_hd_ui_draw(KpApp *a, KpCanvas *c)
{
	KpAppHdUi *u = &a->hd_ui;
	int32_t k, count;
	int full = 0;
	if (!c || !u->head || !c->canvas.ids) {
		kp_gfx_ui_items(a->gfx, NULL, 0);
		return;
	}
	if (u->gfx_generation != u->atlas.generation) { /* kp_gfx's atlas emptied with ours (or lost with a context) */
		kp_gfx_ui_atlas(a->gfx, u->atlas.size, u->atlas.layers);
		u->gfx_generation = u->atlas.generation;
	}
	count = c->canvas.item_count;
	if (count + 1 > u->item_cap) {
		float *grown = (float *)realloc(u->items, sizeof *grown * KP_GFX_UI_ITEM_FLOATS * (size_t)(count + 1));
		if (!grown) return;
		u->items = grown;
		u->item_cap = count + 1;
	}
	memset(u->items, 0, sizeof *u->items * KP_GFX_UI_ITEM_FLOATS * (size_t)(count + 1));
	for (k = 1; k <= count; k++) {
		const KpUiCanvasItem *it = &c->canvas.items[k];
		float *o = u->items + (size_t)k * KP_GFX_UI_ITEM_FLOATS, rect[4], src[4];
		int32_t frame;
		int fresh, replaces = 0;
		KpHdUiFrame f;
		KpHdUiPlace *p;
		o[2] = o[3] = 1; /* an unresolved item: a rectangle no point reaches past (t outside 0..1: SD) */
		o[0] = o[1] = -2;
		if (full || !resolve(u, it, c->w, &frame, rect, src, &replaces)) continue;
		kp_hd_ui_frame(&u->index, frame, &f);
		if (!(p = kp_hd_ui_atlas_place(&u->atlas, frame, f.w, f.h, &fresh))) {
			full = 1; /* emptied for the next frame */
			continue;
		}
		if (fresh) {
			uint8_t *rgba = frame_texels(u, &f);
			if (!rgba) {
				SDL_Log("hd ui: frame %d could not be read", (int)frame);
				p->w = 0;
				continue;
			}
			kp_gfx_ui_atlas_put(a->gfx, p->layer, p->x, p->y, f.w, f.h, rgba);
			p->tint[0] = mean_luma(rgba, f.w, f.h);
			free(rgba);
		}
		o[0] = rect[0];
		o[1] = rect[1];
		o[2] = rect[2];
		o[3] = rect[3];
		o[4] = (float)p->x + src[0];
		o[5] = (float)p->y + src[1];
		o[6] = (float)p->x + src[2];
		o[7] = (float)p->y + src[3];
		o[8] = replaces ? -1.0f - (float)p->layer : (float)p->layer;
		o[12] = (float)it->under;
		o[9] = -1;
		if (it->tinted) { /* the SD pixels' mean colour over the HD image's mean brightness */
			const uint8_t *pal = kp_gfx_palette_rgba(a->gfx);
			float luma = (float)p->tint[0] / 255.0f, sum[3] = { 0, 0, 0 }, n = 0;
			int s, ch;
			for (s = 0; s < 4; s++) {
				for (ch = 0; ch < 3; ch++) sum[ch] += (float)pal[4 * it->tint_index[s] + ch] * (float)it->tint_count[s];
				n += (float)it->tint_count[s];
			}
			if (luma < UI_LUMA_MIN) luma = UI_LUMA_MIN;
			if (n > 0) {
				for (ch = 0; ch < 3; ch++) o[9 + ch] = sum[ch] / n / 255.0f / luma;
			}
		}
	}
	if (full) kp_hd_ui_atlas_reset(&u->atlas);
	kp_gfx_ui_items(a->gfx, u->items, count);
}

void kp_hd_ui_lost(KpApp *a)
{
	if (a->hd_ui.head) kp_hd_ui_atlas_reset(&a->hd_ui.atlas); /* its texels are gone with the context */
}
