/*
 * The HD interface (ADR 0013, stage R4d): StarCraft: Remastered's art for the canvas's items (kp_ui's console pieces,
 * frames and pictures), from generated/hd/<variant>/ui.kpd of the variant --graphics and the scale pick. Each frame
 * the canvas's items are resolved to their HD frame and its rectangle on the logical screen, the frames not yet in the
 * atlas are read, decoded and placed there, and the item table goes to kp_gfx, which draws an item's image where the
 * canvas's item plane names it. A movie's item (a portrait, stage R4e) is drawn from its clip in portraits.kpd,
 * decoded on the CPU into a place of its own in the atlas.
 */
#include "kp_app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define UI_ATLAS_LAYERS 4
#define UI_ATLAS_SIZE_HD 4096  /* an atlas layer's side for the HD art (x4) */
#define UI_ATLAS_SIZE_HD2 2048 /* and for HD2 (x2) */
#define UI_LUMA_MIN (8.0f / 255.0f)
#define UI_LIGHT_SHARE 0.1  /* a tinted HD image's light texels: its brightest tenth */
#define UI_TINT_SHARE 0.01  /* a tinted SD frame's colours: those of at least 1% of its pixels */

static void clip_free(KpAppHdClip *cl)
{
	kp_fetch_free(cl->fetch);
	kp_smk_close(&cl->smk);
	free(cl->bytes);
	free(cl->rgba);
	memset(cl, 0, sizeof *cl);
}

void kp_hd_ui_free(KpApp *a)
{
	KpAppHdUi *u = &a->hd_ui;
	int failed[2], k;
	memcpy(failed, u->failed, sizeof failed); /* a variant not imported stays so */
	for (k = 0; k < KP_APP_HD_CLIPS; k++) clip_free(&u->clips[k]);
	for (k = 0; k < KP_APP_HD_UI_FETCHES; k++) kp_fetch_free(u->fetches[k]);
	free(u->failed_frames);
	free(u->video_head);
	free(u->head);
	free(u->items);
	kp_hd_ui_atlas_free(&u->atlas);
	memset(u, 0, sizeof *u);
	memcpy(u->failed, failed, sizeof failed);
}

/* portraits.kpd of a variant opened when it is there (the HD portraits). */
static void open_video(KpApp *a, const char *variant)
{
	KpAppHdUi *u = &a->hd_ui;
	char path[256];
	size_t n, file_size, need;
	uint8_t *first;
	KpError e;
	snprintf(path, sizeof path, "generated/hd/%s/portraits.kpd", variant);
	if (!(first = kp_app_read_head(a, path, KP_HD_PACK_FIRST_READ, &n, &file_size))) return;
	need = kp_hd_video_head_size(first, n, file_size);
	free(first);
	e.msg[0] = 0;
	if (need == 0 || !(u->video_head = kp_app_read_head(a, path, need, &n, &file_size)) || n != need
		|| kp_hd_video_open(&u->video, u->video_head, n, file_size, path, &e) != 0) {
		SDL_Log("hd ui: %s: %s", path, need ? e.msg : "not an HD portraits file");
		free(u->video_head);
		u->video_head = NULL;
		return;
	}
	kp_app_path(a, path, u->video_path, sizeof u->video_path);
}

/* ui.kpd of a variant opened, its atlas made; -1 when it is not there. */
static int open_variant(KpApp *a, const char *variant)
{
	KpAppHdUi *u = &a->hd_ui;
	char path[256];
	size_t n, file_size, need;
	uint8_t *first;
	KpError e;
	e.msg[0] = 0;
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
	if (kp_hd_ui_atlas_init(&u->atlas, u->index.scale == 4 ? UI_ATLAS_SIZE_HD : UI_ATLAS_SIZE_HD2, UI_ATLAS_LAYERS,
			u->index.frame_count + KP_APP_HD_CLIPS) != 0 /* the frames, then a place for each clip */
		|| !(u->failed_frames = (uint8_t *)calloc((size_t)(u->index.frame_count > 0 ? u->index.frame_count : 1), 1))) {
		return -1;
	}
	open_video(a, variant);
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
	if (a->ui && a->ui->hd != on) { /* the interface's image log; the cursor's frame drawn again into it */
		kp_ui_set_hd(a->ui, on);
		a->cursor.key = -1;
	}
	if (c && (c->canvas.ids != NULL) != on) kp_ui_canvas_hd(&c->canvas, on);
	if (c) c->hd_ui = on ? &u->index : NULL;
}

/* A frame's image (its bytes, freed) decoded to RGBA8 (w x h), or NULL. */
static uint8_t *decode_frame(const KpHdUiFrame *f, uint8_t *data)
{
	uint8_t *rgba = NULL;
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

/* A frame's image read (fetched on the web) and decoded to RGBA8 (*rgba): 1, 0 while it is on its way, -1 when it
 * cannot be read (not asked for again). A fetch no draw asked for since the last one (its frame no longer shown) gives
 * its place to a new one. */
static int frame_texels(KpAppHdUi *u, int32_t frame, const KpHdUiFrame *f, uint8_t **rgba)
{
	int32_t k, slot = -1;
	uint8_t *data;
	*rgba = NULL;
	if (u->failed_frames[frame]) return -1;
	for (k = 0; k < KP_APP_HD_UI_FETCHES && slot < 0; k++) {
		if (u->fetches[k] && u->fetch_frames[k] == frame) slot = k;
	}
	for (k = 0; k < KP_APP_HD_UI_FETCHES && slot < 0; k++) {
		if (u->fetches[k] && u->fetch_asked[k] + 1 < u->draws) { /* its frame no longer shown: dropped */
			kp_fetch_free(u->fetches[k]);
			u->fetches[k] = NULL;
		}
		if (!u->fetches[k] && (u->fetches[k] = kp_fetch_start(u->path, f->offset, f->size)) != NULL) {
			u->fetch_frames[k] = frame;
			slot = k;
		}
	}
	if (slot >= 0) u->fetch_asked[slot] = u->draws;
	if (slot < 0 || kp_fetch_state(u->fetches[slot]) == KP_FETCH_PENDING) return 0; /* asked for later, or on its way */
	data = kp_fetch_take(u->fetches[slot], NULL);
	u->fetches[slot] = NULL;
	if (!data || !(*rgba = decode_frame(f, data))) {
		SDL_Log("hd ui: frame %d could not be read", (int)frame);
		u->failed_frames[frame] = 1;
		return -1;
	}
	return 1;
}

/* A colour's luminance (0 .. 255). */
static uint32_t luma(const uint8_t *rgb) { return (uint32_t)(77 * rgb[0] + 151 * rgb[1] + 28 * rgb[2]) >> 8; }

/* The luminance of an image's light texels: the UI_LIGHT_SHARE brightest of its opaque ones are at least this bright
 * (what a tinted frame's colour is divided by). */
static uint8_t light_luma(const uint8_t *rgba, int32_t w, int32_t h)
{
	uint64_t hist[256], n = 0, seen = 0;
	size_t k;
	int v;
	memset(hist, 0, sizeof hist);
	for (k = 0; k < (size_t)w * (size_t)h; k++) {
		const uint8_t *p = rgba + 4 * k;
		if (p[3] < 128) continue;
		hist[luma(p)]++;
		n++;
	}
	for (v = 255; v > 0; v--) {
		seen += hist[v];
		if ((double)seen >= UI_LIGHT_SHARE * (double)n) break;
	}
	return (uint8_t)v;
}

/* An item's HD frame and its image's rectangle on the logical screen and texels in the frame (x0, y0, x1, y1); 0 when
 * it has no HD art. */
static int resolve(const KpAppHdUi *u, const KpUiCanvasItem *it, int32_t canvas_w, int32_t *frame, float rect[4],
	float src[4], int *replaces, int *layered)
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
	*layered = 0;
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
	if (f.w == KP_HD_UI_WIRE_LAYERS * f.h) { /* a wireframe: its parts' layers side by side, drawn on the whole GRP */
		src[2] = (float)f.h;
		rect[2] = rect[3] = (float)f.h / s;
		rect[0] = (float)it->canvas[0] + ((float)it->canvas[2] - rect[2]) / 2;
		rect[1] = (float)it->canvas[1] + ((float)it->canvas[3] - rect[3]) / 2;
		*layered = 1;
		return 1;
	}
	/* a frame or a picture: its HD image centred on the SD one */
	rect[2] = (float)f.w / s;
	rect[3] = (float)f.h / s;
	rect[0] = (float)it->x + ((float)it->w - rect[2]) / 2;
	rect[1] = (float)it->y + ((float)it->h - rect[3]) / 2;
	return 1;
}

/* A clip's frame decoded into its pixels (frames read on from the last one, or from the first); 0 when it has none. */
static int decode_to(KpSmk *s, int32_t frame)
{
	uint32_t n;
	if (frame < 0 || (uint32_t)frame >= s->frames + s->ring) return 0;
	if (s->frame > (uint32_t)frame) kp_smk_rewind(s);
	for (n = 0; n <= s->frames + s->ring; n++) {
		uint32_t at = s->frame;
		if (kp_smk_decode(s) != 0) return 0;
		if (at == (uint32_t)frame) return 1;
	}
	return 0;
}

/* The atlas place of a movie's item with the item's frame of its HD clip in it, and the clip's rectangle on the
 * logical screen and texels: the clip's middle cut to the item's shape, filling it. NULL when the item is no movie
 * with a clip, or (*full set) the atlas has no room. */
static KpHdUiPlace *video(KpApp *a, const KpUiCanvasItem *it, float rect[4], float src[4], int *full)
{
	KpAppHdUi *u = &a->hd_ui;
	KpAppHdClip *cl = NULL;
	KpHdUiPlace *p;
	uint32_t offset, size;
	int32_t k, w, h;
	int fresh;
	float cut;
	if (!u->video_head || it->w <= 0 || it->h <= 0 || it->path_len >= sizeof cl->path
		|| kp_hd_video_clip(&u->video, it->path, it->path_len, &offset, &size) < 0) {
		return NULL;
	}
	for (k = 0; k < KP_APP_HD_CLIPS && !cl; k++) {
		if (strlen(u->clips[k].path) == it->path_len && memcmp(u->clips[k].path, it->path, it->path_len) == 0) {
			cl = &u->clips[k];
		}
	}
	if (!cl) { /* read into the slot shown longest ago (a clip that fails stays there unread) */
		cl = &u->clips[0];
		for (k = 1; k < KP_APP_HD_CLIPS; k++) {
			if (u->clips[k].used < cl->used) cl = &u->clips[k];
		}
		clip_free(cl);
		memcpy(cl->path, it->path, it->path_len);
		cl->uploaded = -1;
		cl->size = size;
		cl->fetch = kp_fetch_start(u->video_path, offset, size);
	}
	cl->used = ++u->clock;
	if (cl->fetch) { /* the clip on its way (fetched on the web) */
		if (kp_fetch_state(cl->fetch) == KP_FETCH_PENDING) return NULL;
		cl->bytes = kp_fetch_take(cl->fetch, NULL);
		cl->fetch = NULL;
		if (!cl->bytes || kp_smk_open(&cl->smk, cl->bytes, cl->size) != 0 || !kp_smk_palette(&cl->smk)
			|| cl->smk.width > KP_HD_VIDEO_MAX || cl->smk.height > KP_HD_VIDEO_MAX
			|| !(cl->rgba = (uint8_t *)malloc((size_t)cl->smk.width * cl->smk.height * 4))) {
			SDL_Log("hd ui: the HD portrait of %s could not be read", cl->path);
			kp_smk_close(&cl->smk);
			free(cl->bytes);
			cl->bytes = NULL;
		}
	}
	if (!cl->bytes) return NULL;
	w = (int32_t)cl->smk.width;
	h = (int32_t)cl->smk.height;
	if (!(p = kp_hd_ui_atlas_place(&u->atlas, u->index.frame_count + (int32_t)(cl - u->clips), KP_HD_VIDEO_MAX,
			  KP_HD_VIDEO_MAX, &fresh))) {
		*full = 1;
		return NULL;
	}
	if (fresh) cl->uploaded = -1;
	if (cl->uploaded != it->frame) {
		const uint8_t *pal = kp_smk_palette(&cl->smk);
		size_t i;
		if (!decode_to(&cl->smk, it->frame)) return NULL;
		for (i = 0; i < (size_t)w * (size_t)h; i++) {
			uint8_t v = cl->smk.pixels[i];
			cl->rgba[4 * i] = pal[3 * v];
			cl->rgba[4 * i + 1] = pal[3 * v + 1];
			cl->rgba[4 * i + 2] = pal[3 * v + 2];
			cl->rgba[4 * i + 3] = v ? 255 : 0; /* index 0: transparent */
		}
		kp_gfx_ui_atlas_put(a->gfx, p->layer, p->x, p->y, w, h, cl->rgba);
		cl->uploaded = it->frame;
	}
	rect[0] = (float)it->x;
	rect[1] = (float)it->y;
	rect[2] = (float)it->w;
	rect[3] = (float)it->h;
	src[0] = src[1] = 0;
	src[2] = (float)w;
	src[3] = (float)h;
	if ((float)w * (float)it->h > (float)h * (float)it->w) { /* wider than the item: its sides cut */
		cut = (float)h * (float)it->w / (float)it->h;
		src[0] = ((float)w - cut) / 2;
		src[2] = src[0] + cut;
	} else {
		cut = (float)w * (float)it->h / (float)it->w;
		src[1] = ((float)h - cut) / 2;
		src[3] = src[1] + cut;
	}
	return p;
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
	u->draws++;
	count = c->canvas.item_count;
	if (count + 1 > u->item_cap) {
		float *grown = (float *)realloc(u->items, sizeof *grown * KP_GFX_UI_ITEM_FLOATS * (size_t)(count + 1));
		if (!grown) { /* this frame's items unknown to kp_gfx: the canvas in SD */
			kp_gfx_ui_items(a->gfx, NULL, 0);
			return;
		}
		u->items = grown;
		u->item_cap = count + 1;
	}
	memset(u->items, 0, sizeof *u->items * KP_GFX_UI_ITEM_FLOATS * (size_t)(count + 1));
	for (k = 1; k <= count; k++) {
		const KpUiCanvasItem *it = &c->canvas.items[k];
		float *o = u->items + (size_t)k * KP_GFX_UI_ITEM_FLOATS, rect[4], src[4];
		int32_t frame;
		int fresh, replaces = 0, layered = 0;
		KpHdUiFrame f;
		KpHdUiPlace *p;
		uint8_t *rgba;
		o[2] = o[3] = 1; /* an unresolved item: a rectangle no point reaches past (t outside 0..1: SD) */
		o[0] = o[1] = -2;
		if (full) continue;
		if (!(p = video(a, it, rect, src, &full))) {
			if (full || !resolve(u, it, c->w, &frame, rect, src, &replaces, &layered)) continue;
			kp_hd_ui_frame(&u->index, frame, &f);
			rgba = NULL;
			if (u->atlas.places[frame].w == 0 && frame_texels(u, frame, &f, &rgba) <= 0) continue; /* SD meanwhile */
			if (!(p = kp_hd_ui_atlas_place(&u->atlas, frame, f.w, f.h, &fresh))) {
				free(rgba);
				full = 1; /* emptied for the next frame */
				continue;
			}
			if (fresh && rgba) {
				kp_gfx_ui_atlas_put(a->gfx, p->layer, p->x, p->y, f.w, f.h, rgba);
				p->tint[0] = light_luma(rgba, f.w, f.h);
			}
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
		if (it->tiled && !layered) { /* its image again every SD frame's size: all its tiles */
			o[13] = (float)it->w;
			o[14] = (float)it->h;
		}
		if (layered) { /* a wireframe: its parts' palette colours, three to a float (the outline: its brightest shade) */
			const uint8_t *pal = kp_gfx_palette_rgba(a->gfx);
			int s, outline = it->parts[KP_UI_WIRE_PARTS - 1];
			for (s = KP_UI_WIRE_PARTS; s < KP_UI_WIRE_COLOURS; s++) {
				if (luma(pal + 4 * it->parts[s]) > luma(pal + 4 * outline)) outline = it->parts[s];
			}
			o[9] = -2;
			o[13] = (float)(it->parts[0] | it->parts[1] << 8 | it->parts[2] << 16);
			o[14] = (float)(it->parts[3] | it->parts[4] << 8 | it->parts[5] << 16);
			o[15] = (float)outline;
		} else if (it->tinted) {
			/* the brightest colour the SD frame shows over the HD image's light texels: Remastered draws its grey
			 * icons through a colour ramp whose light end is the slice's bright colour (the SD frames are mostly its
			 * dark shades, so their mean colour made the icons dim) */
			const uint8_t *pal = kp_gfx_palette_rgba(a->gfx);
			float bright = (float)p->tint[0] / 255.0f;
			uint32_t total = 0;
			int s, ch, best = -1;
			for (s = 0; s < KP_UI_TINTS; s++) total += it->tint_count[s];
			for (s = 0; s < KP_UI_TINTS && it->tint_count[s]; s++) { /* a colour written more than in passing */
				if ((double)it->tint_count[s] < UI_TINT_SHARE * (double)total) continue;
				if (best < 0 || luma(pal + 4 * it->tint_index[s]) > luma(pal + 4 * it->tint_index[best])) best = s;
			}
			if (bright < UI_LUMA_MIN) bright = UI_LUMA_MIN;
			if (best >= 0) {
				for (ch = 0; ch < 3; ch++) o[9 + ch] = (float)pal[4 * it->tint_index[best] + ch] / 255.0f / bright;
			}
		}
	}
	if (full) kp_hd_ui_atlas_reset(&u->atlas);
	kp_gfx_ui_items(a->gfx, u->items, count);
}

void kp_hd_ui_cursor(KpApp *a, int drawn)
{
	KpAppHdUi *u = &a->hd_ui;
	const KpUiImage *im;
	KpUiCanvasItem it;
	KpHdUiFrame f;
	KpHdUiPlace *p;
	float rect[4], src[4], at[4];
	int32_t frame;
	int fresh, replaces, layered;
	uint8_t *rgba = NULL;
	if (!drawn || !u->head || !a->ui || !(im = kp_ui_image_last(a->ui, a->cursor.pixels))) {
		kp_gfx_ui_cursor(a->gfx, NULL, NULL, 0);
		return;
	}
	memset(&it, 0, sizeof it);
	it.path = im->path;
	it.path_len = im->path_len;
	it.frame = im->frame;
	it.x = a->mouse_lx + a->cursor.dx + im->item[0];
	it.y = a->mouse_ly + a->cursor.dy + im->item[1];
	it.w = im->item[2];
	it.h = im->item[3];
	it.column = -1;
	if (!resolve(u, &it, 0, &frame, rect, src, &replaces, &layered) || layered) {
		kp_gfx_ui_cursor(a->gfx, NULL, NULL, 0);
		return;
	}
	kp_hd_ui_frame(&u->index, frame, &f);
	if (u->gfx_generation != u->atlas.generation
		|| (u->atlas.places[frame].w == 0 && frame_texels(u, frame, &f, &rgba) <= 0)) {
		kp_gfx_ui_cursor(a->gfx, NULL, NULL, 0); /* the atlas is made on the next canvas frame, or the frame comes */
		return;
	}
	if (!(p = kp_hd_ui_atlas_place(&u->atlas, frame, f.w, f.h, &fresh))) {
		free(rgba);
		kp_gfx_ui_cursor(a->gfx, NULL, NULL, 0); /* emptied on the next canvas frame */
		return;
	}
	if (fresh && rgba) kp_gfx_ui_atlas_put(a->gfx, p->layer, p->x, p->y, f.w, f.h, rgba);
	free(rgba);
	at[0] = (float)p->x + src[0];
	at[1] = (float)p->y + src[1];
	at[2] = (float)p->x + src[2];
	at[3] = (float)p->y + src[3];
	kp_gfx_ui_cursor(a->gfx, rect, at, (float)p->layer);
}

void kp_hd_ui_lost(KpApp *a)
{
	if (a->hd_ui.head) kp_hd_ui_atlas_reset(&a->hd_ui.atlas); /* its texels are gone with the context */
}
