/*
 * The world images' data (ADR 0002, composition by depth rank in ADR 0006): once per tick the draw list goes into one
 * RGBA32F texture that every family's pass reads (WORLD_VS_COMMON in native/kp_gfx/kp_gfx_shaders.c), each image with
 * the remap or table row of its draw method:
 *   data[2 slot]     = (x, y, x - previous x, y - previous y) in 1/256 world pixels
 *   data[2 slot + 1] = ((frame * KINDS + kind) * 2 + hflip, rank, row, layer)
 * Each family owns a fixed slot range; an image keeps its slot while it stays in the family, so a still image
 * changes nothing. A slot not written during a tick is freed and draws nothing (the empty frame, layer 0).
 */
#include "kp_present.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kp_game.h"

#define WARP_TEXTURE_IMAGE 210 /* the image whose GRP method 12 draws through (native pointer slot 0x543a40) */
#define HEALTH_BAR_METHOD 11

const int32_t KP_FAMILY_BASE[KP_FAMILIES] = { 0, KP_FAMILY_CAPACITY_OVERWRITE,
	KP_FAMILY_CAPACITY_OVERWRITE + KP_FAMILY_CAPACITY_DEST };
const int32_t KP_FAMILY_CAPACITY[KP_FAMILIES] = { KP_FAMILY_CAPACITY_OVERWRITE, KP_FAMILY_CAPACITY_DEST,
	KP_FAMILY_CAPACITY_DISTORT };

int kp_image_data_init(KpImageData *d, int32_t empty, KpError *err)
{
	int32_t f, s;
	memset(d, 0, sizeof *d);
	d->empty = empty;
	d->height = (KP_IMAGE_SLOTS * KP_TEXELS_PER_SLOT + KP_IMAGE_DATA_WIDTH - 1) / KP_IMAGE_DATA_WIDTH;
	d->data = (float *)calloc((size_t)KP_IMAGE_DATA_WIDTH * (size_t)d->height * 4, sizeof *d->data);
	d->seen = (uint32_t *)calloc(KP_IMAGE_SLOTS, sizeof *d->seen);
	d->key_of = (int32_t *)malloc(sizeof *d->key_of * KP_IMAGE_SLOTS);
	for (f = 0; f < KP_FAMILIES; f++) {
		d->slot_of[f] = (int32_t *)malloc(sizeof *d->slot_of[f] * KP_IMAGES);
		d->free[f] = (int32_t *)malloc(sizeof *d->free[f] * (size_t)KP_FAMILY_CAPACITY[f]);
		if (d->slot_of[f]) {
			for (s = 0; s < KP_IMAGES; s++) d->slot_of[f][s] = -1;
		}
	}
	if (!d->data || !d->seen || !d->key_of || !d->slot_of[0] || !d->slot_of[1] || !d->slot_of[2] || !d->free[0]
		|| !d->free[1] || !d->free[2]) {
		kp_image_data_free(d);
		snprintf(err->msg, sizeof err->msg, "image data: out of memory");
		return -1;
	}
	/* a free slot draws nothing: the zero-size frame */
	for (s = 0; s < KP_IMAGE_SLOTS; s++) {
		d->data[(size_t)s * 8 + 4] = (float)(empty * KP_DRAW_KINDS * 2);
		d->key_of[s] = -1;
	}
	return 0;
}

void kp_image_data_free(KpImageData *d)
{
	int f;
	free(d->data);
	free(d->seen);
	free(d->key_of);
	for (f = 0; f < KP_FAMILIES; f++) {
		free(d->slot_of[f]);
		free(d->free[f]);
	}
	memset(d, 0, sizeof *d);
}

static void write_value(KpImageData *d, size_t o, float v)
{
	int32_t row, slot = (int32_t)(o / 8);
	int f = slot >= KP_FAMILY_BASE[2] ? 2 : slot >= KP_FAMILY_BASE[1] ? 1 : 0;
	if (d->data[o] == v) return;
	d->data[o] = v;
	d->writes++;
	row = (int32_t)(o / 4 / KP_IMAGE_DATA_WIDTH);
	if (d->dirty_lo[f] >= d->dirty_hi[f]) {
		d->dirty_lo[f] = row;
		d->dirty_hi[f] = row + 1;
	} else if (row < d->dirty_lo[f]) {
		d->dirty_lo[f] = row;
	} else if (row >= d->dirty_hi[f]) {
		d->dirty_hi[f] = row + 1;
	}
}

void kp_image_data_begin(KpImageData *d)
{
	d->writes = 0;
	d->tick++;
}

/* The global slot of key in family for this tick (allocated when new), or -1 when the family is full. */
static int32_t data_slot(KpImageData *d, int family, int32_t key)
{
	int32_t slot = d->slot_of[family][key], global;
	if (slot < 0) {
		if (d->free_count[family] > 0) slot = d->free[family][--d->free_count[family]];
		else if (d->next_slot[family] < KP_FAMILY_CAPACITY[family]) slot = d->next_slot[family]++;
		else return -1;
		d->slot_of[family][key] = slot;
		d->key_of[KP_FAMILY_BASE[family] + slot] = key;
	}
	global = KP_FAMILY_BASE[family] + slot;
	d->seen[global] = d->tick;
	return global;
}

int kp_image_data_put(KpImageData *d, const KpDrawItem *it, int32_t row, int32_t layer)
{
	int32_t slot = data_slot(d, it->family, it->key);
	size_t o;
	if (slot < 0) return 0;
	o = (size_t)slot * 8;
	write_value(d, o, (float)it->x256);
	write_value(d, o + 1, (float)it->y256);
	write_value(d, o + 2, (float)(it->x256 - it->px256));
	write_value(d, o + 3, (float)(it->y256 - it->py256));
	write_value(d, o + 4, (float)((it->frame * KP_DRAW_KINDS + it->kind) * 2 + (it->flip ? 1 : 0)));
	write_value(d, o + 5, (float)it->rank);
	write_value(d, o + 6, (float)row);
	write_value(d, o + 7, (float)layer);
	return 1;
}

static void copy_plan(KpPlan *to, const KpWindow *windows, int32_t count, int32_t layers)
{
	if (count > KP_PLAN_WINDOWS) count = KP_PLAN_WINDOWS;
	if (count > 0) memmove(to->windows, windows, sizeof *windows * (size_t)count);
	to->count = count;
	to->layers = layers;
}

/* Ends the tick: slots not written this tick are emptied and freed; windows[0..count-1] and layers are the tick's
 * plan. */
void kp_image_data_finish(KpImageData *d, const KpWindow *windows, int32_t count, int32_t layers)
{
	int f;
	for (f = 0; f < KP_FAMILIES; f++) {
		int32_t slot;
		for (slot = 0; slot < d->next_slot[f]; slot++) {
			int32_t global = KP_FAMILY_BASE[f] + slot, key = d->key_of[global];
			size_t o;
			if (key < 0 || d->seen[global] == d->tick) continue;
			d->slot_of[f][key] = -1;
			d->key_of[global] = -1;
			d->free[f][d->free_count[f]++] = slot;
			/* a freed slot draws nothing: the zero-size frame, and layer 0 (an overwriting slot's nonzero layer is a
			 * virtual frame size) */
			o = (size_t)global * 8;
			write_value(d, o + 4, (float)(d->empty * KP_DRAW_KINDS * 2));
			write_value(d, o + 7, 0);
		}
	}
	copy_plan(&d->plan, windows, count, layers);
	for (f = 0; f < KP_FAMILIES; f++) d->plan.slots[f] = d->next_slot[f];
	if (d->writes > 0) d->pending = 1;
}

int kp_image_data_take(KpImageData *d, KpRowRange ranges[KP_FAMILIES], int *count)
{
	int pending = d->pending, f;
	*count = 0;
	if (pending) {
		/* each family's rows are one run, and the families lie in slot order: a run that touches the one before joins it */
		for (f = 0; f < KP_FAMILIES; f++) {
			if (d->dirty_lo[f] >= d->dirty_hi[f]) continue;
			if (*count > 0 && d->dirty_lo[f] <= ranges[*count - 1].row + ranges[*count - 1].rows) {
				int32_t end = d->dirty_hi[f], last_end = ranges[*count - 1].row + ranges[*count - 1].rows;
				ranges[*count - 1].rows = (end > last_end ? end : last_end) - ranges[*count - 1].row;
			} else {
				ranges[*count].row = d->dirty_lo[f];
				ranges[*count].rows = d->dirty_hi[f] - d->dirty_lo[f];
				(*count)++;
			}
			d->dirty_lo[f] = d->dirty_hi[f] = 0;
		}
	}
	d->pending = 0;
	return pending;
}

/* --- the presentation of a world --- */

int kp_presenter_init(KpImagesPresenter *p, KpImages *w, const KpGrpMeta *grp, const KpFrameTable *frames,
	const KpLookups *lookups, KpBars *bars, struct KpGame *game, KpError *err)
{
	uint32_t k;
	memset(p, 0, sizeof *p);
	p->w = w;
	p->grp = grp;
	p->frames = frames;
	p->lookups = lookups;
	p->bars = bars;
	p->game = game;
	p->list = kp_draw_list_new();
	p->first_frame = (int32_t *)malloc(sizeof *p->first_frame * (grp->grp_count + 1));
	if (!p->list || !p->first_frame) {
		kp_presenter_free(p);
		snprintf(err->msg, sizeof err->msg, "images: out of memory");
		return -1;
	}
	for (k = 0; k <= grp->grp_count; k++) p->first_frame[k] = -2;
	p->warp_texture = kp_images_grp_of(w, WARP_TEXTURE_IMAGE);
	return 0;
}

void kp_presenter_free(KpImagesPresenter *p)
{
	kp_draw_list_free(p->list);
	free(p->first_frame);
	free(p->collected);
	memset(p, 0, sizeof *p);
}

/* The global frame of a GRP entry's frame, cached per entry; -1 when its atlas is not loaded. */
static int32_t frame_of(void *ud, int32_t g, int32_t frame)
{
	KpImagesPresenter *p = (KpImagesPresenter *)ud;
	int32_t first;
	if (g == KP_GRP_VIRTUAL) return p->frames->empty;
	if (g < 1 || (uint32_t)g > p->grp->grp_count) return -1;
	first = p->first_frame[g];
	if (first == -2) {
		first = kp_frame_table_frame(p->frames, p->grp, g, 0);
		p->first_frame[g] = first;
	}
	return first < 0 ? -1 : first + frame;
}

static void collect(KpImagesPresenter *p, const KpDrawItem *it, int32_t row)
{
	KpDrawn *c;
	if (p->collected_count >= p->collected_cap) {
		int32_t cap = p->collected_cap ? p->collected_cap * 2 : 1024;
		KpDrawn *grown = (KpDrawn *)realloc(p->collected, sizeof *grown * (size_t)cap);
		if (!grown) {
			p->failed = 1;
			snprintf(p->error.msg, sizeof p->error.msg, "images: out of memory");
			return;
		}
		p->collected = grown;
		p->collected_cap = cap;
	}
	c = &p->collected[p->collected_count++];
	c->key = it->key;
	c->left = it->left;
	c->top = it->top;
	c->w = it->w;
	c->h = it->h;
	c->grp = it->grp;
	c->frame = it->grp_frame;
	c->global = it->frame;
	c->flip = it->flip;
	c->method = it->method;
	c->rank = it->rank;
	c->row = row;
	c->layer = it->layer;
	c->window = it->window;
	c->owner = it->owner;
	c->param = it->param;
	c->virtual_ = it->virtual_;
}

/* An image left out, not drawn: its slot is not written and is freed at the tick's end. */
static void skip(KpImagesPresenter *p, const char *why)
{
	if (p->skipped++ == 0) snprintf(p->skip.msg, sizeof p->skip.msg, "%s", why);
}

static void emit(void *ud, const KpDrawItem *it)
{
	KpImagesPresenter *p = (KpImagesPresenter *)ud;
	int32_t row, layer = it->layer;
	if (p->failed) return;
	if (it->method == HEALTH_BAR_METHOD) {
		/* the bar of the unit the image's parameter names */
		KpBar bar;
		if (!p->bars || !p->game) {
			p->failed = 1;
			snprintf(p->error.msg, sizeof p->error.msg, "images: a health bar needs the bars and a game");
			return;
		}
		kp_bar_of_unit(p->game, it->param, &bar);
		row = kp_bars_put(p->bars, it->key, &bar, it->w, it->h);
		if (row < 0) {
			p->failed = 1;
			snprintf(p->error.msg, sizeof p->error.msg, "bars: more bars than health-bar records");
			return;
		}
	} else {
		int32_t warp = -1;
		if (it->method == 12) {
			warp = frame_of(p, p->warp_texture, it->param);
			if (warp < 0) {
				skip(p, "images: warp texture frame is not loaded");
				return;
			}
		}
		row = kp_draw_row_of(p->lookups, it->method, it->param, it->owner, it->trans50, warp);
		if (row < 0) {
			char msg[sizeof p->skip.msg];
			snprintf(msg, sizeof msg, "images: no row for method %d parameter %d", (int)it->method, (int)it->param);
			skip(p, msg);
			return;
		}
	}
	if (it->virtual_) layer = it->w * KP_VIRTUAL_SIZE + it->h;
	if (!kp_image_data_put(p->data, it, row, layer)) p->overflow++; /* the family's slots are full: not drawn */
	if (p->collect) collect(p, it, row);
}

int32_t kp_presenter_sync(KpImagesPresenter *p, KpImageData *d, const KpRect *view, int replan, int collect_)
{
	int32_t drawn, layers = 0, count;
	const KpWindow *windows;
	p->data = d;
	p->overflow = 0;
	p->skipped_before = p->skipped;
	p->skipped = 0;
	p->collect = collect_;
	p->collected_count = 0;
	p->failed = 0;
	kp_image_data_begin(d);
	if (p->bars) kp_bars_begin(p->bars);
	drawn = kp_draw_list_build(p->list, p->w, frame_of, emit, p, view, replan, &layers);
	if (drawn < 0 || p->failed) {
		if (!p->failed) snprintf(p->error.msg, sizeof p->error.msg, "draw list: out of memory");
		/* the slots written before the failure go up too: the uploaded texture stays the CPU copy that the next
		 * tick's writes compare against */
		if (d->writes > 0) d->pending = 1;
		p->data = NULL;
		return -1;
	}
	if (p->bars) kp_bars_finish(p->bars);
	windows = kp_draw_list_windows(p->list, &count);
	kp_image_data_finish(d, windows, count, layers);
	p->data = NULL;
	p->drawn = drawn;
	if (view) {
		p->planned = 1;
		p->planned_view = *view;
	} else {
		p->planned = 0;
	}
	return drawn;
}

void kp_presenter_restart(KpImagesPresenter *p) { kp_draw_list_restart(p->list); }

int kp_presenter_needs_replan(const KpImagesPresenter *p, const KpRect *view)
{
	const KpRect *q = &p->planned_view;
	int32_t near, m = KP_DRAW_LAYER_MARGIN;
	if (view == NULL || !p->planned) return 0;
	kp_draw_list_counts(p->list, NULL, &near);
	if (near > 0 && (view->left != q->left || view->top != q->top)) return 1;
	return view->left < q->left - m || view->top < q->top - m || view->right > q->right + m
		|| view->bottom > q->bottom + m;
}
