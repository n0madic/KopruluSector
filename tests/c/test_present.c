/* kp_present (plan phase S, stage S2): the draw list on hand-built worlds (no retail data), the lookup rows on
 * synthetic palettes, the palette's cycles, gamma and fades, the health bars against the original's DrawHealthBar, the
 * image data's slots and uploads and the layouts on the aspect ratios and resolutions of plan §7 phase 9. */
#include "kp_test.h"

#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "kp_present.h"
#include "kp_dat.h"

/* --- a hand-built world --- */

/* One GRP entry, 64 x 64 canvas, three frames: (x, y, w, h). */
static const uint8_t FRAMES[3][4] = { { 10, 20, 8, 6 }, { 30, 4, 8, 6 }, { 0, 0, 0, 0 } };

typedef struct {
	KpGrpMeta grp;
	KpIScript script;
	KpRngGame rng;
	KpImages *w;
} World;

static void free_world(void *p)
{
	World *w = (World *)p;
	if (w->w) kp_images_free(w->w);
	kp_grpmeta_free(&w->grp);
	free(w);
}

/* The deferred cleanups take a void pointer; a call through another function type is undefined. */
static void free_draw_list(void *p) { kp_draw_list_free((KpDrawList *)p); }
static void free_dat(void *p) { kp_dat_free((KpDat *)p); }
static void free_image_data(void *p) { kp_image_data_free((KpImageData *)p); }
static void free_presenter(void *p) { kp_presenter_free((KpImagesPresenter *)p); }

static KpImages *new_world(KpT *T)
{
	World *w = (World *)calloc(1, sizeof *w);
	KpError err;
	int k;
	if (!w) kp_t_fail(T, "out of memory");
	if (kp_grpmeta_alloc(&w->grp, 0, 1, 0, 4 + 8 * 3, &err) != 0) kp_t_fail(T, "%s", err.msg);
	for (k = 0; k < 3; k++) memcpy(w->grp.blob + 4 + 8 * k, FRAMES[k], 4);
	w->grp.frms = w->grp.blob;
	w->grp.frms_size = (uint32_t)w->grp.size;
	w->grp.grps[1].frames = 3;
	w->grp.grps[1].width = 64;
	w->grp.grps[1].height = 64;
	w->grp.grps[1].atlas = 1;
	w->w = kp_images_new(NULL, &w->grp, &w->script, &w->rng, &err);
	kp_t_defer(T, free_world, w);
	if (!w->w) kp_t_fail(T, "%s", err.msg);
	kp_images_set_map(w->w, 16, 16);
	kp_images_init_pools(w->w);
	return w->w;
}

typedef struct {
	int32_t method, frame, flags;
} ImageDef;

typedef struct {
	int32_t elevation, visibility, owner; /* 0: the defaults 4, 0xff, 0 */
	int set_visibility;
} SpriteOpts;

/* A sprite at (x, y) with images given head to tail; keys get the images' records. */
static int32_t sprite(KpImages *w, int32_t x, int32_t y, const ImageDef *defs, int n, const SpriteOpts *o,
	int32_t *keys)
{
	int32_t s = w->free_sprites.head, k;
	kp_list_unlink(w->spr.prev, w->spr.next, &w->free_sprites, s);
	w->spr.x[s] = x;
	w->spr.y[s] = y;
	w->spr.elevation[s] = o && o->elevation ? o->elevation : 4;
	w->spr.visibility[s] = o && o->set_visibility ? o->visibility : 0xff;
	w->spr.owner[s] = o ? o->owner : 0;
	kp_list_insert_after_head(w->spr.prev, w->spr.next, &w->rows[kp_images_sprite_row(w, s)], s);
	for (k = 0; k < n; k++) {
		int32_t i = kp_image_alloc(w);
		KpList *l = &w->spr.images[s];
		w->img.id[i] = 101 + k;
		w->img.grp[i] = 1;
		w->img.frame[i] = defs[k].frame;
		w->img.method[i] = defs[k].method;
		w->img.flags[i] = defs[k].flags;
		w->img.sprite[i] = s;
		if (l->head == KP_NONE) kp_list_insert_after_head(w->img.prev, w->img.next, l, i);
		else kp_list_insert_after(w->img.prev, w->img.next, l, l->tail, i);
		if (k == 0) w->spr.main[s] = i;
		if (keys) keys[k] = i;
	}
	return s;
}

#define OUT_MAX 512
typedef struct {
	KpDrawItem d[OUT_MAX];
	int n;
} Out;

static int32_t identity_frame(void *ud, int32_t grp, int32_t frame)
{
	(void)ud;
	(void)grp;
	return frame;
}

static void collect(void *ud, const KpDrawItem *d)
{
	Out *o = (Out *)ud;
	if (o->n < OUT_MAX) o->d[o->n++] = *d;
}

static KpDrawList *list(KpT *T)
{
	KpDrawList *l = kp_draw_list_new();
	if (!l) kp_t_fail(T, "out of memory");
	kp_t_defer(T, free_draw_list, l);
	return l;
}

/* The end of a simulation tick (the world's bounds refresh), then the draw list. */
static int32_t build(KpDrawList *l, KpImages *w, const KpRect *view, Out *out, int32_t *layers)
{
	out->n = 0;
	kp_images_refresh_bounds(w);
	return kp_draw_list_build(l, w, identity_frame, collect, out, view, 0, layers);
}

static KpRect rect(int32_t l, int32_t t, int32_t r, int32_t b)
{
	KpRect v;
	v.left = l;
	v.top = t;
	v.right = r;
	v.bottom = b;
	return v;
}

static const ImageDef PLAIN = { 0, 0, 0 };

static void draw_native_order_tail_to_head_and_ranks(KpT *T)
{
	KpImages *w = new_world(T);
	ImageDef low_defs[2] = { { 0, 0, 0 }, { 10, 0, 0 } };
	int32_t low[2], high[1], drawn, k;
	static Out out;
	sprite(w, 200, 100, low_defs, 2, NULL, low); /* y 100: drawn after */
	sprite(w, 100, 50, &PLAIN, 1, NULL, high);
	drawn = build(list(T), w, NULL, &out, NULL);
	kp_t_eq(T, drawn, 3, NULL);
	kp_t_eq(T, out.d[0].key, high[0], NULL);
	kp_t_eq(T, out.d[1].key, low[1], "tail (shadow) before the head");
	kp_t_eq(T, out.d[2].key, low[0], NULL);
	for (k = 0; k < 3; k++) kp_t_eq(T, out.d[k].rank, k + 1, NULL);
	kp_t_eq(T, out.d[1].family, KP_FAMILY_DEST, NULL);
}

static void draw_bounds_follow_the_current_frame(KpT *T)
{
	KpImages *w = new_world(T);
	KpDrawList *l = list(T);
	int32_t keys[1];
	KpRect v = rect(0, 0, 640, 480);
	static Out out;
	sprite(w, 100, 100, &PLAIN, 1, NULL, keys);
	build(l, w, NULL, &out, NULL);
	kp_t_eq(T, out.d[0].left, 100 + 10 - 32, NULL);
	kp_t_eq(T, out.d[0].top, 100 + 20 - 32, NULL);
	w->img.frame[keys[0]] = 1; /* playfram: a frame with another rectangle */
	build(l, w, NULL, &out, NULL);
	kp_t_eq(T, out.d[0].left, 100 + 30 - 32, NULL);
	kp_t_eq(T, out.d[0].top, 100 + 4 - 32, NULL);
	kp_t_eq(T, w->img.x[keys[0]], 100 + 30 - 32, "the tick's refresh writes the bounds like the original");
	/* the draw list only reads them: a replan for any view leaves the records alone */
	w->img.frame[keys[0]] = 0;
	out.n = 0;
	kp_draw_list_build(l, w, identity_frame, collect, &out, &v, 1, NULL);
	kp_t_eq(T, w->img.x[keys[0]], 100 + 30 - 32, NULL);
}

static void draw_overlapping_destination_images_get_layers(KpT *T)
{
	KpImages *w = new_world(T);
	ImageDef shadow = { 10, 0, 0 };
	KpDrawList *l;
	int32_t layers;
	static Out out;
	sprite(w, 100, 100, &shadow, 1, NULL, NULL);
	sprite(w, 104, 101, &shadow, 1, NULL, NULL); /* overlaps the first */
	sprite(w, 400, 300, &shadow, 1, NULL, NULL); /* apart */
	build(list(T), w, NULL, &out, &layers);
	kp_t_eq(T, layers, 2, NULL);
	kp_t_eq(T, out.d[0].layer, 1, NULL);
	kp_t_eq(T, out.d[1].layer, 2, NULL);
	kp_t_eq(T, out.d[2].layer, 1, NULL);
	/* cells are reused on the next tick: the same scene gives the same layers */
	l = list(T);
	build(l, w, NULL, &out, NULL);
	build(l, w, NULL, &out, &layers);
	kp_t_eq(T, layers, 2, NULL);
	kp_t_eq(T, out.d[2].layer, 1, NULL);
}

static void draw_distortion_images_close_windows(KpT *T)
{
	KpImages *w = new_world(T);
	ImageDef shadow = { 10, 0, 0 }, a[2] = { { 3, 0, 0 }, { 10, 0, 0 } }, b[2] = { { 10, 0, 0 }, { 8, 0, 0 } };
	KpDrawList *l = list(T);
	const KpWindow *win;
	int32_t drawn, layers, count;
	static Out out;
	sprite(w, 100, 100, &shadow, 1, NULL, NULL);
	sprite(w, 100, 102, a, 2, NULL, NULL); /* shadow, then the cloaked main */
	sprite(w, 100, 104, b, 2, NULL, NULL);
	drawn = build(l, w, NULL, &out, &layers);
	win = kp_draw_list_windows(l, &count);
	kp_t_eq(T, drawn, 5, NULL);
	kp_t_eq(T, count, 2, NULL);
	kp_t_eq(T, win[0].rank, 3, NULL);
	kp_t_eq(T, win[0].layers, 2, "two overlapping shadows before the first distortion");
	kp_t_eq(T, out.d[2].family, KP_FAMILY_DISTORT, NULL);
	kp_t_eq(T, out.d[2].kind, KP_KIND_BRIGHT, NULL);
	kp_t_eq(T, out.d[2].window, 1, NULL);
	kp_t_eq(T, out.d[3].family, KP_FAMILY_DISTORT, "the EMP image (tail) closes window 2 at once");
	kp_t_eq(T, out.d[3].kind, KP_KIND_RAW, NULL);
	kp_t_eq(T, win[1].rank, 4, NULL);
	kp_t_eq(T, win[1].layers, 0, NULL);
	kp_t_eq(T, out.d[4].window, 3, NULL);
	kp_t_eq(T, out.d[4].layer, 1, "a fresh overlap grid in the new window");
	kp_t_eq(T, layers, 1, NULL);
	/* the next tick without distortions has no windows */
	w->img.method[out.d[2].key] = 0;
	w->img.method[out.d[3].key] = 0;
	build(l, w, NULL, &out, NULL);
	kp_draw_list_windows(l, &count);
	kp_t_eq(T, count, 0, NULL);
}

/* With the view known, a distortion reads the current copy when nothing drawn since that copy meets the pixels it
 * reads and those stay inside the view. */
static void draw_distortions_share_a_copy_when_nothing_between_meets_them(KpT *T)
{
	KpImages *w = new_world(T);
	ImageDef cloak = { 2, 0, 0 }, emp = { 8, 0, 0 };
	KpRect view = rect(0, 0, 700, 480), narrow = rect(0, 0, 420, 480), left = rect(0, 0, 200, 480);
	KpDrawList *l = list(T);
	const KpWindow *win;
	int32_t count;
	static Out out;
	sprite(w, 100, 100, &cloak, 1, NULL, NULL);
	sprite(w, 300, 101, &cloak, 1, NULL, NULL); /* apart from the first: shares its copy */
	sprite(w, 108, 102, &PLAIN, 1, NULL, NULL); /* meets the next one's read */
	sprite(w, 104, 103, &cloak, 1, NULL, NULL);
	sprite(w, 400, 104, &emp, 1, NULL, NULL); /* EMP: reads 255 pixels ahead, meets nothing */
	build(l, w, &view, &out, NULL);
	win = kp_draw_list_windows(l, &count);
	kp_t_eq(T, count, 2, NULL);
	kp_t_eq(T, win[0].rank, 1, NULL);
	kp_t_eq(T, win[0].last, 2, NULL);
	kp_t_eq(T, out.d[1].window, 1, NULL);
	kp_t_eq(T, win[1].rank, 4, NULL);
	kp_t_eq(T, win[1].last, 5, NULL);
	kp_t_eq(T, out.d[2].window, 2, "the plain image after copy 1");
	/* reading past the view's right edge (wrap) takes a copy of its own */
	l = list(T);
	build(l, w, &narrow, &out, NULL);
	kp_draw_list_windows(l, &count);
	kp_t_eq(T, count, 3, NULL);
	/* without a view every distortion takes its own copy */
	l = list(T);
	build(l, w, NULL, &out, NULL);
	kp_draw_list_windows(l, &count);
	kp_t_eq(T, count, 4, NULL);
	/* outside the view a distortion reads whatever copy there is */
	l = list(T);
	build(l, w, &left, &out, NULL);
	kp_draw_list_windows(l, &count);
	kp_t_eq(T, count, 2, NULL);
}

/* Before any copy, a distortion out of view changes nothing seen: it opens no window (no copy of the target) and
 * draws in none. */
static void draw_distortions_out_of_view_take_no_copy(KpT *T)
{
	KpImages *w = new_world(T);
	ImageDef cloak = { 2, 0, 0 }, emp = { 8, 0, 0 };
	KpRect left = rect(0, 0, 200, 480);
	KpDrawList *l = list(T);
	const KpWindow *win;
	int32_t count, distortions;
	static Out out;
	sprite(w, 300, 100, &cloak, 1, NULL, NULL);
	sprite(w, 400, 101, &emp, 1, NULL, NULL);
	kp_t_eq(T, build(l, w, &left, &out, NULL), 2, "both drawn");
	kp_draw_list_windows(l, &count);
	kp_draw_list_counts(l, &distortions, NULL);
	kp_t_eq(T, count, 0, "no window");
	kp_t_eq(T, distortions, 2, "still counted");
	kp_t_ok(T, out.d[0].window == 0 && out.d[1].window == 0, "in no window");
	sprite(w, 100, 102, &cloak, 1, NULL, NULL); /* one in view after them opens the first */
	build(l, w, &left, &out, NULL);
	win = kp_draw_list_windows(l, &count);
	kp_t_eq(T, count, 1, NULL);
	kp_t_eq(T, win[0].rank, 3, "the one in view's");
}

static void draw_replan_keeps_the_tick_interpolation(KpT *T)
{
	KpImages *w = new_world(T);
	ImageDef cloak = { 2, 0, 0 };
	KpRect view = rect(0, 0, 640, 480);
	KpDrawList *l = list(T);
	int32_t s = sprite(w, 100, 100, &cloak, 1, NULL, NULL), px, distortions;
	static Out out;
	build(l, w, &view, &out, NULL);
	w->spr.x[s] = 110;
	build(l, w, &view, &out, NULL);
	kp_draw_list_counts(l, &distortions, NULL);
	kp_t_eq(T, distortions, 1, NULL);
	px = out.d[0].px256;
	kp_t_ok(T, px != out.d[0].x256, "moved");
	build(l, w, NULL, &out, NULL); /* an ordinary build starts a new tick */
	kp_t_eq(T, out.d[0].px256, out.d[0].x256, NULL);
	/* a replan of the same tick keeps the start position */
	l = list(T);
	w->spr.x[s] = 100;
	build(l, w, NULL, &out, NULL);
	w->spr.x[s] = 110;
	build(l, w, NULL, &out, NULL);
	kp_images_refresh_bounds(w);
	out.n = 0;
	kp_draw_list_build(l, w, identity_frame, collect, &out, &view, 1, NULL);
	kp_t_eq(T, out.d[0].px256, px, NULL);
	{ /* an image the tick's first build left out (rows above its view) moves from the previous tick's place */
		KpRect low = rect(0, 320, 640, 800); /* the first row drawn: 320 / 32 - 4 = 6, below the sprite's row 3 */
		int32_t from;
		l = list(T);
		w->spr.x[s] = 100;
		build(l, w, &view, &out, NULL);
		from = out.d[0].x256;
		w->spr.x[s] = 110;
		kp_t_eq(T, build(l, w, &low, &out, NULL), 0, "left out");
		kp_images_refresh_bounds(w);
		out.n = 0;
		kp_draw_list_build(l, w, identity_frame, collect, &out, &view, 1, NULL);
		kp_t_eq(T, out.n, 1, NULL);
		kp_t_eq(T, out.d[0].px256, from, "from the previous tick");
		kp_t_ok(T, out.d[0].x256 != from, "to this one's");
	}
}

static void draw_destination_layers_only_near_the_view(KpT *T)
{
	KpImages *w = new_world(T);
	ImageDef shadow = { 10, 0, 0 };
	KpRect view = rect(0, 0, 320, 240);
	int32_t layers;
	static Out out;
	sprite(w, 100, 100, &shadow, 1, NULL, NULL);
	sprite(w, 104, 101, &shadow, 1, NULL, NULL);
	sprite(w, 1000, 900, &shadow, 1, NULL, NULL); /* far from the view */
	build(list(T), w, &view, &out, &layers);
	kp_t_eq(T, layers, 2, NULL);
	kp_t_eq(T, out.d[2].layer, 0, "not drawn, no copy spent");
	build(list(T), w, NULL, &out, NULL);
	kp_t_eq(T, out.d[2].layer, 1, NULL);
}

/* 0x416bd0: a sprite whose main image draws with method 5..7 sets the trans50 colour rows for the images drawn after
 * it, across ticks; sprites far above the view are not drawn. */
static void draw_trans50_colour_follows_the_drawn_sprites(KpT *T)
{
	KpImages *w = new_world(T);
	ImageDef blend = { 6, 0, 0 }, b_defs[2] = { { 0, 0, 0 }, { 6, 0, 0 } };
	SpriteOpts oa, ob;
	KpRect far = rect(0, 400, 640, 800);
	KpDrawList *l = list(T);
	int32_t a[1], b[2], k, found = 0;
	static Out out;
	memset(&oa, 0, sizeof oa);
	memset(&ob, 0, sizeof ob);
	oa.owner = 3;
	ob.owner = 5;
	sprite(w, 100, 100, &blend, 1, &oa, a);
	sprite(w, 100, 200, b_defs, 2, &ob, b); /* main image method 0 */
	build(l, w, NULL, &out, NULL);
	for (k = 0; k < out.n; k++) {
		if (out.d[k].key == a[0]) kp_t_eq(T, out.d[k].trans50, 3, NULL), found++;
		if (out.d[k].key == b[1]) kp_t_eq(T, out.d[k].trans50, 3, "the earlier sprite's colours stay"), found++;
	}
	kp_t_eq(T, found, 2, NULL);
	w->img.method[a[0]] = 0;
	build(l, w, NULL, &out, NULL);
	for (k = 0; k < out.n; k++) {
		if (out.d[k].key == b[1]) kp_t_eq(T, out.d[k].trans50, 3, "and across ticks");
	}
	kp_t_eq(T, build(list(T), w, &far, &out, NULL), 0, "rows 3 and 6: above row 12 - 4");
}

static void draw_rows_of_the_effect_methods(KpT *T)
{
	KpLookups l;
	int f, kind;
	memset(&l, 0, sizeof l);
	l.bank_base[2] = 50; /* remap(3) = 2 * 50 + 1 */
	l.outline_color = 77;
	kp_t_eq(T, kp_draw_row_of(&l, 6, 0, 0, -1, -1), 0, "trans50 rows unchanged");
	kp_t_eq(T, kp_draw_row_of(&l, 6, 0, 0, 4, -1), 5, "rows 8..15 of owner 4's colours");
	kp_t_eq(T, kp_draw_row_of(&l, 9, KP_DRAW_TRANS50, 0, 4, -1), 5, NULL);
	kp_t_eq(T, kp_draw_row_of(&l, 9, 3, 0, 4, -1), 101, NULL);
	kp_t_eq(T, kp_draw_row_of(&l, 5, 0x1305, 2, -1, -1), 3 * KP_DRAW_PARAM_SPAN + 5, "parameter byte only");
	kp_t_eq(T, kp_draw_row_of(&l, 7, 0x1305, 2, 1, -1), 2 * KP_DRAW_COLOR_SPAN + 3 * KP_DRAW_PARAM_SPAN + 5, NULL);
	kp_t_eq(T, kp_draw_row_of(&l, 2, 0xff, 0, -1, -1), KP_DRAW_PARAM_SPAN + 0xff, NULL);
	kp_t_eq(T, kp_draw_row_of(&l, 3, 7, 0, -1, -1), 0, NULL);
	kp_t_eq(T, kp_draw_row_of(&l, 12, 4, 0, -1, 504), 504, NULL);
	kp_t_eq(T, kp_draw_row_of(&l, 15, 0, 0, -1, -1), 77, NULL);
	kp_draw_method(4, &f, &kind);
	kp_t_eq(T, kind, KP_KIND_CLOAK, NULL);
	kp_draw_method(7, &f, &kind);
	kp_t_eq(T, kind, KP_KIND_CLOAK_BLEND, NULL);
	kp_draw_method(11, &f, &kind);
	kp_t_eq(T, kind, KP_KIND_BAR, NULL);
	kp_t_eq(T, kp_draw_method(18, NULL, NULL), 0, "no draw method 18");
}

static void draw_hidden_unsupported_empty_and_invisible_are_skipped(KpT *T)
{
	KpImages *w = new_world(T);
	ImageDef defs[4] = { { 0, 0, KP_F_HIDDEN }, { 18, 0, 0 }, { 0, 2, 0 }, { 0, 0, 0 } };
	SpriteOpts hidden;
	static Out out;
	memset(&hidden, 0, sizeof hidden);
	hidden.set_visibility = 1;
	sprite(w, 100, 100, defs, 4, NULL, NULL);
	sprite(w, 300, 100, &PLAIN, 1, &hidden, NULL);
	kp_t_eq(T, build(list(T), w, NULL, &out, NULL), 1, NULL);
}

static void draw_interpolation_only_from_the_same_image(KpT *T)
{
	KpImages *w = new_world(T);
	KpDrawList *l = list(T);
	int32_t keys[1], s = sprite(w, 100, 100, &PLAIN, 1, NULL, keys);
	static Out out;
	build(l, w, NULL, &out, NULL);
	w->spr.x[s] = 110;
	build(l, w, NULL, &out, NULL);
	kp_t_eq(T, out.d[0].px256, (100 + 10 - 32 + 4) * 256, NULL);
	kp_t_eq(T, out.d[0].x256, (110 + 10 - 32 + 4) * 256, NULL);
	w->spr.x[s] = 120;
	w->img.id[keys[0]] = 999; /* the record now holds another image */
	build(l, w, NULL, &out, NULL);
	kp_t_eq(T, out.d[0].px256, out.d[0].x256, NULL);
	/* after a restart (a loaded game) the same image does not move from its position before */
	w->spr.x[s] = 130;
	kp_draw_list_restart(l);
	build(l, w, NULL, &out, NULL);
	kp_t_eq(T, out.d[0].px256, out.d[0].x256, "no motion across a restart");
	w->spr.x[s] = 140;
	build(l, w, NULL, &out, NULL);
	kp_t_eq(T, out.d[0].px256, (130 + 10 - 32 + 4) * 256, "motion again on the next tick");
}

/* A distortion far from the view cannot meet it after a camera move inside the margin; one near it can. */
static void draw_distortions_near_the_view_are_counted(KpT *T)
{
	KpImages *w = new_world(T);
	ImageDef bright = { 3, 0, 0 };
	KpRect view = rect(0, 0, 320, 240);
	KpDrawList *l = list(T);
	int32_t distortions, near;
	static Out out;
	sprite(w, 100, 100, &bright, 1, NULL, NULL);
	sprite(w, 3000, 3000, &bright, 1, NULL, NULL); /* far below and right of the view */
	build(l, w, &view, &out, NULL);
	kp_draw_list_counts(l, &distortions, &near);
	kp_t_eq(T, distortions, 2, NULL);
	kp_t_eq(T, near, 1, NULL);
}

/* No command budget (stage S8): overlapping destination images each get their own layer, and distortions that each
 * need their own copy each open a window. */
static void draw_plans_have_no_command_budget(KpT *T)
{
	ImageDef shadow = { 10, 0, 0 }, pair[2] = { { 10, 0, 0 }, { 3, 0, 0 } };
	KpImages *w = new_world(T);
	KpDrawList *l = list(T);
	int32_t layers, count, k;
	static Out out;
	/* 200 shadows on one spot: 200 overlapping destination layers */
	for (k = 1; k <= 200; k++) sprite(w, 100, 100, &shadow, 1, NULL, NULL);
	build(l, w, NULL, &out, &layers);
	kp_t_eq(T, layers, 200, NULL);
	/* 200 distortions each needing its own copy (no view): 200 windows */
	w = new_world(T);
	l = list(T);
	for (k = 1; k <= 200; k++) sprite(w, 10 * k, 100, pair, 2, NULL, NULL);
	build(l, w, NULL, &out, &layers);
	kp_draw_list_windows(l, &count);
	kp_t_eq(T, count, 200, NULL);
}

/* --- the lookup rows --- */

static void lookups_brightness_follows_0x445f30(KpT *T)
{
	static KpPalettes p;
	static uint8_t remap[KP_REMAP_TABLES][256], dark[KP_DARK_SIZE], shift[256], rgb[768];
	KpLookups l;
	KpError err;
	int k;
	memset(&p, 0, sizeof p);
	for (k = 0; k < KP_REMAP_TABLES; k++) {
		p.tilesets[0].remap[k].rows = 1;
		p.tilesets[0].remap[k].data = remap[k];
	}
	p.tilesets[0].dark = dark;
	p.tilesets[0].shift = shift;
	for (k = 0; k < 768; k++) rgb[k] = k % 3 == 0 ? 255 : 0; /* pure red */
	if (kp_lookups_build(&l, &p, 0, rgb, 0, &err) != 0) kp_t_fail(T, "%s", err.msg);
	kp_t_eq(T, l.remap[l.bright * 256], (77 * 255 + 4096) / 8192, NULL);
	kp_lookups_free(&l);
	memset(rgb, 255, sizeof rgb);
	if (kp_lookups_build(&l, &p, 0, rgb, 0, &err) != 0) kp_t_fail(T, "%s", err.msg);
	kp_t_eq(T, l.remap[l.bright * 256], 8, "(256 * 255 + 4096) >> 13");
	kp_lookups_free(&l);
}

static void lookups_rows_and_banks(KpT *T)
{
	static KpPalettes p;
	static uint8_t remap[KP_REMAP_TABLES][KP_REMAP_TABLES * 256], dark[KP_DARK_SIZE], shift[256], tunit[96],
		tselect[24], rgb[768];
	KpLookups l;
	KpError err;
	int32_t k, i, sel, trans50;
	memset(&p, 0, sizeof p);
	for (k = 0; k < KP_REMAP_TABLES; k++) {
		p.tilesets[0].remap[k].rows = (uint16_t)(k + 1);
		for (i = 0; i < (k + 1) * 256; i++) remap[k][i] = (uint8_t)((i + k + 1) % 256);
		p.tilesets[0].remap[k].data = remap[k];
	}
	for (i = 0; i < KP_DARK_SIZE; i++) dark[i] = (uint8_t)(((255 - i) % 256 + 256) % 256);
	for (i = 0; i < 256; i++) shift[i] = (uint8_t)(i + 1);
	for (i = 0; i < 96; i++) tunit[i] = (uint8_t)(100 + i);
	for (i = 0; i < 24; i++) tselect[i] = (uint8_t)(200 + i);
	p.tilesets[0].dark = dark;
	p.tilesets[0].shift = shift;
	p.tunit = tunit;
	p.tunit_size = 96;
	p.tselect = tselect;
	p.tselect_size = 24;
	memset(rgb, 255, sizeof rgb);
	if (kp_lookups_build(&l, &p, 0, rgb, 0, &err) != 0) kp_t_fail(T, "%s", err.msg);
	/* identity, 12 player rows, shift, 3 selection rows, brightness */
	kp_t_eq(T, l.remap_rows, 1 + KP_LOOKUP_PLAYER_ROWS + 1 + 3 + 1, NULL);
	kp_t_eq(T, kp_lookups_owner(&l, 13), 2, NULL);
	/* the minimap's colour mode 2: each player number draws as the row the interface gives (SetDrawColor 0x41d740) */
	{
		uint8_t rows[KP_LOOKUP_PLAYER_ROWS] = { 2, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
		kp_t_ok(T, !kp_lookups_set_player_rows(&l, NULL), "each its own: nothing changes");
		kp_t_ok(T, kp_lookups_set_player_rows(&l, rows), "the rows change");
		kp_t_eq(T, kp_lookups_owner(&l, 0), 3, "the viewer's class 2");
		kp_t_eq(T, kp_lookups_owner(&l, 1), 8, "player 1 an ally: class 7");
		kp_t_eq(T, kp_lookups_owner(&l, 13), 8, "13 is 1 modulo 12");
		kp_t_eq(T, kp_lookups_owner(&l, 5), 1, "the others: class 0");
		kp_t_eq(T, kp_draw_row_of(&l, 0, 0, 0, -1, -1), 3, "a sprite's image by its owner");
		kp_t_eq(T, kp_draw_row_of(&l, 14, 1, 0, -1, -1), 8, "an image of an explicit player colour by the mapped row");
		kp_t_eq(T, kp_draw_row_of(&l, 6, 0, 0, 1, -1), 8, "a cloak blend's trans50 owner by the mapped row");
		/* a wrapper's restore maps the saved, already mapped colour as a player again (0x40e950): 0 -> 2 -> 0 -> 2 */
		kp_t_eq(T, kp_lookups_owner_wrapped(&l, 0, 0), 3, "no wrapper before it");
		kp_t_eq(T, kp_lookups_owner_wrapped(&l, 0, 1), 1, "one restore: the class 2 as player 2 is class 0");
		kp_t_eq(T, kp_lookups_owner_wrapped(&l, 0, 2), 3, "two restores");
		kp_t_eq(T, kp_draw_row_wrapped(&l, 0, 0, 1, 1, -1, -1), 1, "an image after a wrapper: the owner's class 7 mapped again");
		kp_t_eq(T, kp_draw_row_wrapped(&l, 14, 1, 0, 1, -1, -1), 8, "an explicit colour image maps its own colour once");
		kp_t_ok(T, !kp_lookups_set_player_rows(&l, rows), "the same rows again: nothing changes");
		kp_t_ok(T, kp_lookups_set_player_rows(&l, NULL), "each its own again");
		kp_t_eq(T, kp_lookups_owner(&l, 13), 2, NULL);
	}
	kp_t_eq(T, l.remap[kp_lookups_owner(&l, 0) * 256 + 8], 100, "tunit on index 8");
	kp_t_eq(T, l.remap[kp_lookups_owner(&l, 0) * 256 + 7], 7, NULL);
	kp_t_eq(T, l.remap[l.shift_row * 256], 1, NULL);
	sel = kp_draw_row_of(&l, 13, 2, 0, -1, -1);
	kp_t_eq(T, l.remap[sel * 256 + 1], 200 + 16, "tselect row 2 on index 1");
	kp_t_eq(T, l.remap[sel * 256 + 9], 9, NULL);
	kp_t_eq(T, l.remap[l.bright * 256 + 255], 8, NULL);
	kp_t_eq(T, kp_draw_row_of(&l, 13, 3, 0, -1, -1), -1, "only three tselect rows");
	/* banks of BANK_ROWS rows, decoded rows first, then identity; then the shadow row */
	kp_t_eq(T, l.table_rows, KP_REMAP_TABLES * KP_LOOKUP_BANK_ROWS + 1, NULL);
	trans50 = (KP_DRAW_TRANS50 - 1) * KP_LOOKUP_BANK_ROWS;
	kp_t_eq(T, l.trans50, 2 * trans50 + 1, NULL);
	kp_t_eq(T, l.tables[trans50 * 256], KP_DRAW_TRANS50 % 256, NULL);
	kp_t_eq(T, l.tables[(trans50 + KP_DRAW_TRANS50) * 256], 0, "first padding row: identity");
	kp_t_eq(T, kp_draw_row_of(&l, 10, 0, 0, -1, -1), 2 * (l.table_rows - 1), NULL);
	kp_t_eq(T, l.tables[(l.table_rows - 1) * 256], ((255 - KP_LOOKUP_SHADOW_DARK_ROW * 256) % 256 + 256) % 256, NULL);
	kp_t_eq(T, kp_draw_row_of(&l, 15, 0, 0, -1, -1), 0, NULL);
	kp_lookups_free(&l);
}

/* --- the palette --- */

/* CRC-32 of the gamma tables of brightness 60..140 (256 bytes each): the byte predictions that the original 0x4cf760
 * matched for every level and input byte in the palette research batch. */
#define GAMMA_CRC 37115800u

static void ramp(uint8_t *rgb)
{
	int i;
	for (i = 0; i < 256; i++) {
		rgb[3 * i] = (uint8_t)i;
		rgb[3 * i + 1] = (uint8_t)((i * 3) % 256);
		rgb[3 * i + 2] = (uint8_t)(255 - i);
	}
}

static KpCycles cycles(int count, const uint8_t *ranges)
{
	KpCycles c;
	int k;
	memset(&c, 0, sizeof c);
	c.speed = 8;
	c.count = count;
	for (k = 0; k < count; k++) {
		c.low[k] = ranges[2 * k];
		c.high[k] = ranges[2 * k + 1];
	}
	return c;
}

static const uint8_t BADLANDS[] = { 1, 6, 7, 13, 248, 254 };
static const uint8_t DESERT[] = { 1, 13, 248, 254 };

static void palette_gamma_tables_match_the_original(KpT *T)
{
	uint8_t t[256];
	uint32_t crc = 0;
	int32_t v;
	for (v = KP_PALETTE_BRIGHTNESS_MIN; v <= KP_PALETTE_BRIGHTNESS_MAX; v++) {
		kp_palette_gamma_table(v, t);
		crc = kp_crc32_update(crc, t, 256);
	}
	kp_t_eq(T, crc, GAMMA_CRC, NULL);
	kp_palette_gamma_table(60, t);
	kp_t_eq(T, t[128], 97, NULL);
	kp_palette_gamma_table(140, t);
	kp_t_eq(T, t[1], 9, NULL);
	/* the exponent the HD interface's art takes (kp_gfx_gamma): the table's within a step */
	kp_t_ok(T, fabs(kp_palette_gamma_exponent(KP_PALETTE_BRIGHTNESS_DEFAULT) - 1.0) < 1e-12, "1 at the default");
	for (v = KP_PALETTE_BRIGHTNESS_MIN; v <= KP_PALETTE_BRIGHTNESS_MAX; v += 10) {
		int c, worst = 0;
		kp_palette_gamma_table(v, t);
		for (c = 0; c < 256; c++) {
			int d = (int)t[c] - (int)floor(255.0 * pow(c / 255.0, kp_palette_gamma_exponent(v)) + 0.5);
			if (abs(d) > worst) worst = abs(d);
		}
		kp_t_ok(T, worst <= 1, "brightness %d: the exponent within %d of the table", (int)v, worst);
	}
}

static void palette_brightness_option_and_output(KpT *T)
{
	static KpPalette p;
	uint8_t rgb[768], g[256];
	KpCycles none = cycles(0, NULL);
	ramp(rgb);
	kp_palette_init(&p, rgb, &none);
	kp_t_ok(T, memcmp(kp_palette_rgb(&p), rgb, 768) == 0, "identity at 100");
	kp_palette_set_brightness(&p, 141); /* out of range: 100 */
	kp_t_eq(T, p.brightness, 100, NULL);
	kp_palette_set_brightness(&p, 60);
	kp_palette_gamma_table(60, g);
	kp_t_eq(T, kp_palette_rgb(&p)[128 * 3], g[128], NULL);
	kp_palette_set_brightness(&p, 59);
	kp_t_ok(T, memcmp(kp_palette_rgb(&p), rgb, 768) == 0, "59: 100 again");
}

static void palette_badlands_cycles_rotate_every_eighth_driver_call(KpT *T)
{
	static KpPalette p;
	uint8_t rgb[768], cur[768];
	KpCycles c = cycles(3, BADLANDS);
	int k;
	ramp(rgb);
	kp_palette_init(&p, rgb, &c);
	for (k = 1; k <= 7; k++) kp_t_ok(T, !kp_palette_cycle(&p), "counters 8 -> 1 change nothing");
	kp_t_ok(T, kp_palette_cycle(&p), "the eighth rotates");
	kp_palette_current(&p, cur);
	/* 1..6 rotate right: entry 1 gets old entry 6, entry 2 old entry 1 */
	kp_t_eq(T, cur[1 * 3], 6, NULL);
	kp_t_eq(T, cur[2 * 3], 1, NULL);
	kp_t_eq(T, cur[6 * 3], 5, NULL);
	kp_t_eq(T, cur[7 * 3], 13, "7..13");
	kp_t_eq(T, cur[248 * 3], 254, "the high range in the second pass");
	kp_t_eq(T, cur[0], 0, NULL);
	kp_t_eq(T, cur[14 * 3], 14, NULL);
	kp_t_eq(T, cur[255 * 3], 255, NULL);
}

static void palette_desert_cycles_and_empty_tilesets(KpT *T)
{
	static KpPalette p, q;
	uint8_t rgb[768], cur[768];
	KpCycles c = cycles(2, DESERT), none = cycles(0, NULL);
	int k;
	ramp(rgb);
	kp_palette_init(&p, rgb, &c);
	for (k = 0; k < 8; k++) kp_palette_cycle(&p);
	kp_palette_current(&p, cur);
	kp_t_eq(T, cur[1 * 3], 13, NULL);
	kp_palette_init(&q, rgb, &none);
	for (k = 0; k < 16; k++) kp_t_ok(T, !kp_palette_cycle(&q), "no cycles");
	kp_palette_current(&q, cur);
	kp_t_ok(T, memcmp(cur, rgb, 768) == 0, "unchanged");
}

static void palette_advance_uses_the_tick_period_and_pauses(KpT *T)
{
	static KpPalette p;
	uint8_t rgb[768];
	KpCycles c = cycles(3, BADLANDS);
	int k;
	ramp(rgb);
	kp_palette_init(&p, rgb, &c);
	for (k = 0; k < 7; k++) kp_t_ok(T, !kp_palette_advance(&p, KP_PALETTE_CYCLE_PERIOD_MS, 0), "steps 1..7");
	kp_t_ok(T, !kp_palette_advance(&p, KP_PALETTE_CYCLE_PERIOD_MS, 1), "paused: the driver does not run");
	kp_t_ok(T, kp_palette_advance(&p, KP_PALETTE_CYCLE_PERIOD_MS, 0), "step 8");
}

/* A stall is not caught up: one advance runs at most MAX_CYCLES_PER_ADVANCE driver steps. */
static void palette_advance_after_a_stall_runs_a_bounded_number_of_steps(KpT *T)
{
	static KpPalette stalled, stepped;
	uint8_t rgb[768], a[768], b[768];
	KpCycles c = cycles(3, BADLANDS);
	int k;
	ramp(rgb);
	kp_palette_init(&stalled, rgb, &c);
	kp_palette_init(&stepped, rgb, &c);
	kp_palette_advance(&stalled, 600000, 0);
	for (k = 0; k < KP_PALETTE_MAX_CYCLES_PER_ADVANCE; k++) kp_palette_cycle(&stepped);
	kp_palette_current(&stalled, a);
	kp_palette_current(&stepped, b);
	kp_t_ok(T, memcmp(a, b, 768) == 0, "bounded");
	kp_t_ok(T, stalled.cycle_acc == 0, "the backlog dropped");
}

static void palette_grayscale_truncates_each_channel(KpT *T)
{
	static KpPalette p;
	uint8_t white[768], cur[768];
	KpCycles none = cycles(0, NULL);
	kp_t_eq(T, kp_palette_gray(255, 255, 255), 253, NULL);
	memset(white, 255, sizeof white);
	kp_palette_init(&p, white, &none);
	kp_palette_grayscale(&p);
	kp_palette_current(&p, cur);
	kp_t_eq(T, cur[0], 253, NULL);
	kp_t_eq(T, cur[2], 253, NULL);
}

static void palette_fade_shows_the_target_after_its_wait(KpT *T)
{
	static KpPalette p;
	uint8_t rgb[768], black[768], cur[768];
	KpCycles none = cycles(0, NULL);
	ramp(rgb);
	memset(black, 0, sizeof black);
	kp_palette_init(&p, rgb, &none);
	kp_palette_fade_to(&p, black, 4);
	kp_palette_current(&p, cur);
	kp_t_ok(T, memcmp(cur, rgb, 768) == 0, "from until the wait");
	kp_palette_advance(&p, KP_PALETTE_FADE_WAIT_MS - 1, 0);
	kp_palette_current(&p, cur);
	kp_t_ok(T, memcmp(cur, rgb, 768) == 0, "still from");
	kp_t_ok(T, kp_palette_advance(&p, 1, 0), "the fade ends");
	kp_palette_current(&p, cur);
	kp_t_ok(T, memcmp(cur, black, 768) == 0, "the target");
	kp_palette_fade_to(&p, rgb, 0);
	kp_palette_current(&p, cur);
	kp_t_ok(T, memcmp(cur, rgb, 768) == 0, "at once");
}

/* --- the health bars against the original DrawHealthBar 0x464d20 (golden from
 * tools/research/probe_native_health_bar_117.py) --- */

static const int32_t HP_LEVELS[] = { 1, 33, 34, 50, 66, 67, 100 };
static const int32_t SHIELD_LEVELS[] = { 0, 50, 100 };
static const int32_t ENERGY_LEVELS[] = { 0, 40, 100 };
/* left, top, width delta (0: none), height delta (0: none) */
static const int32_t CROPS[7][4] = { { 0, 0, 0, 0 }, { 1, 0, 0, 0 }, { 2, 0, 0, 0 }, { 4, 0, 0, 0 }, { 0, 0, -5, 0 },
	{ 0, 1, 0, 0 }, { 0, 0, 0, -6 } };

static uint32_t fold(uint32_t h, int64_t v) { return h * 31u + (uint32_t)v; }

typedef struct {
	int32_t w, h, mul, mod;
	int32_t *px;
} BarBitmap;

static void bitmap_hline(void *ud, int32_t x, int32_t y, int32_t n, uint8_t color)
{
	BarBitmap *b = (BarBitmap *)ud;
	int32_t k;
	for (k = 0; k < n; k++) b->px[y * b->w + x + k] = color;
}

static void bitmap_vline(void *ud, int32_t x, int32_t y, int32_t n, uint8_t color)
{
	BarBitmap *b = (BarBitmap *)ud;
	int32_t k;
	for (k = 0; k < n; k++) b->px[(y + k) * b->w + x] = color;
}

static int64_t dat_at(KpT *T, const KpDat *d, const char *table, const char *column, int64_t i)
{
	const KpDatColumn *c = kp_dat_column(d, table, column);
	if (!c) kp_t_fail(T, "no dat column %s.%s", table, column);
	return kp_dat_value(c, (uint32_t)i);
}

static void health_bars_equal_original(KpT *T)
{
	static const char *const FILES[] = { "generated/data/dat.kpd", "generated/data/palettes.kpd", NULL };
	const KpJson *g, *types;
	static KpDat d;
	KpPalettes pal;
	KpError err;
	BarBitmap bm;
	KpBarCanvas c;
	size_t size, psize;
	uint8_t *dat, *pdat;
	int32_t x, y, local, r, bad = 0;
	char bad_list[512] = "";
	if (!kp_t_ready(T, "health bars", FILES)) return;
	g = kp_t_golden(T, "tests/golden/oracle/health_bar/cases.json");
	dat = kp_t_read(T, "generated/data/dat.kpd", &size);
	pdat = kp_t_read(T, "generated/data/palettes.kpd", &psize);
	if (kp_dat_load(&d, dat, size, "dat.kpd", NULL, &err) != 0) kp_t_fail(T, "%s", err.msg);
	kp_t_defer(T, free_dat, &d);
	if (kp_palettes_load(&pal, pdat, psize, &err) != 0) kp_t_fail(T, "%s", err.msg);
	bm.w = (int32_t)kp_json_field_int(g, "width", 0);
	bm.h = (int32_t)kp_json_field_int(g, "height", 0);
	bm.mul = (int32_t)kp_json_field_int(g, "baseline_mul", 0);
	bm.mod = (int32_t)kp_json_field_int(g, "baseline_mod", 1);
	bm.px = (int32_t *)calloc((size_t)(bm.w * bm.h), sizeof *bm.px);
	kp_t_defer(T, free, bm.px);
	x = (int32_t)kp_json_field_int(g, "x", 0);
	y = (int32_t)kp_json_field_int(g, "y", 0);
	local = (int32_t)kp_json_field_int(g, "local_player", 0);
	c.hline = bitmap_hline;
	c.vline = bitmap_vline;
	c.ud = &bm;
	c.thpbar = pal.thpbar;
	types = kp_json_get(g, "types");
	for (r = 0; r < kp_json_len(types); r++) {
		const KpJson *row = kp_json_at(types, r);
		int64_t t = kp_json_int(kp_json_at(row, 0), 0), max_energy = kp_json_int(kp_json_at(row, 1), 0);
		int64_t count = kp_json_int(kp_json_at(row, 2), -1), expected = kp_json_int(kp_json_at(row, 3), -1);
		int64_t sprite_id = dat_at(T, &d, "flingy", "sprite", dat_at(T, &d, "units", "graphics", t));
		int64_t width = dat_at(T, &d, "sprites", "health_bar", sprite_id);
		int64_t hp = dat_at(T, &d, "units", "hit_points", t), sh = dat_at(T, &d, "units", "shield_amount", t);
		int64_t shields = dat_at(T, &d, "units", "shield_enabled", t);
		int64_t abilities = dat_at(T, &d, "units", "special_ability_flags", t);
		int32_t height = shields != 0 ? 13 : 11, n = 0, s, k, i;
		uint32_t h = 0;
		/* the probe's states: hp, shields, energy, timer, status, owner */
		int64_t st[64][6];
		int sc = 0;
		for (k = 0; k < 7; k++) {
			int64_t v = hp * HP_LEVELS[k] / 100;
			st[sc][0] = v > 1 ? v : 1;
			st[sc][1] = sh * 256;
			st[sc][2] = max_energy;
			st[sc][3] = 0;
			st[sc][4] = 0;
			st[sc++][5] = local;
		}
		if (shields != 0) {
			for (k = 0; k < 3; k++) {
				st[sc][0] = hp / 2;
				st[sc][1] = sh * SHIELD_LEVELS[k] / 100 * 256;
				st[sc][2] = 0;
				st[sc][3] = 0;
				st[sc][4] = 0;
				st[sc++][5] = local;
			}
		}
		if ((abilities & 0x200000) != 0) {
			for (k = 0; k < 3; k++) {
				int o;
				for (o = 0; o < 2; o++) {
					st[sc][0] = hp;
					st[sc][1] = 0;
					st[sc][2] = max_energy * ENERGY_LEVELS[k] / 100;
					st[sc][3] = 0;
					st[sc][4] = 0;
					st[sc++][5] = local + o;
				}
			}
		}
		st[sc][0] = hp;
		st[sc][1] = 0;
		st[sc][2] = 0;
		st[sc][3] = KP_BAR_TIMER_HALLUCINATION / 3;
		st[sc][4] = 0x40000000;
		st[sc++][5] = local;
		if (t == 40) {
			st[sc][0] = hp;
			st[sc][1] = 0;
			st[sc][2] = 0;
			st[sc][3] = KP_BAR_TIMER_BROODLING / 2;
			st[sc][4] = 0;
			st[sc++][5] = local;
		}
		for (s = 0; s < sc; s++) {
			KpBarUnit u;
			KpBar bar;
			u.type = t;
			u.hp = st[s][0];
			u.shields = st[s][1];
			u.energy = st[s][2];
			u.timer = st[s][3];
			u.status = st[s][4];
			u.owner = st[s][5];
			u.hit_points = hp;
			u.shield_enabled = shields;
			u.shield_amount = sh;
			u.abilities = abilities;
			u.width = width;
			u.max_energy = max_energy;
			kp_bar_from_unit(&u, local, &bar);
			for (k = 0; k < 7; k++) {
				int32_t crop[4];
				crop[0] = CROPS[k][0];
				crop[1] = CROPS[k][1];
				crop[2] = CROPS[k][2] ? (int32_t)width + CROPS[k][2] : (int32_t)width - crop[0];
				crop[3] = CROPS[k][3] ? height + CROPS[k][3] : height - crop[1];
				if (crop[2] > 0 && crop[3] > 0) {
					uint32_t bh = 0;
					for (i = 0; i < bm.w * bm.h; i++) bm.px[i] = i * bm.mul % bm.mod;
					kp_bar_draw(&c, x, y, crop, &bar);
					for (i = 0; i < bm.w * bm.h; i++) bh = fold(bh, bm.px[i]);
					h = fold(h, bh);
					n++;
				}
			}
		}
		if (n != count || h != (uint32_t)expected) {
			size_t len = strlen(bad_list);
			snprintf(bad_list + len, sizeof bad_list - len, "%s%lld", bad ? "," : "", (long long)t);
			bad++;
		}
	}
	kp_t_eq(T, bad, 0, "unit types differing from the original: %s", bad_list);
}

/* --- the image data --- */

static KpImageData *image_data(KpT *T, int32_t empty)
{
	KpImageData *d = (KpImageData *)calloc(1, sizeof *d);
	KpError err;
	if (!d || kp_image_data_init(d, empty, &err) != 0) kp_t_fail(T, "image data");
	kp_t_defer(T, free, d); /* the cleanups run last first */
	kp_t_defer(T, free_image_data, d);
	return d;
}

static KpDrawItem item(int family, int32_t key, int32_t frame, int32_t rank)
{
	KpDrawItem it;
	memset(&it, 0, sizeof it);
	it.family = family;
	it.key = key;
	it.frame = frame;
	it.rank = rank;
	return it;
}

/* A virtual image (health bar) carries its frame size in the layer field; once its slot is freed the slot must not
 * keep that size, or the vertex stage would still draw it. */
static void images_released_slots_draw_nothing(KpT *T)
{
	KpImageData *d = image_data(T, 7);
	KpDrawItem it = item(KP_FAMILY_OVERWRITE, 5000, 7, 1);
	int32_t size = 19 * 256 + 5;
	it.kind = 3;
	kp_image_data_begin(d);
	kp_t_ok(T, kp_image_data_put(d, &it, 4, size), "put");
	kp_image_data_finish(d, NULL, 0, 0);
	kp_t_eq(T, (int64_t)d->data[7], size, NULL);
	kp_image_data_begin(d); /* the bar is not drawn this tick */
	kp_image_data_finish(d, NULL, 0, 0);
	kp_t_eq(T, (int64_t)d->data[4], 7 * KP_DRAW_KINDS * 2, NULL);
	kp_t_eq(T, (int64_t)d->data[7], 0, NULL);
}

static void images_full_family_leaves_images_out(KpT *T)
{
	KpImageData *d = image_data(T, 7);
	KpDrawItem it;
	int32_t k;
	kp_image_data_begin(d);
	for (k = 0; k < KP_FAMILY_CAPACITY_DEST; k++) {
		it = item(KP_FAMILY_DEST, k, 1, k + 1);
		kp_t_ok(T, kp_image_data_put(d, &it, 0, 1), "dest %d", (int)k);
	}
	it = item(KP_FAMILY_DEST, KP_FAMILY_CAPACITY_DEST, 1, 1);
	kp_t_eq(T, kp_image_data_put(d, &it, 0, 1), 0, "the family is full");
	it = item(KP_FAMILY_OVERWRITE, 2, 1, 3);
	kp_t_ok(T, kp_image_data_put(d, &it, 0, 0), "other families unaffected");
	kp_image_data_finish(d, NULL, 0, 0);
}

static void images_upload_on_the_write_frame(KpT *T)
{
	KpImageData *d = image_data(T, 7);
	KpDrawItem it = item(KP_FAMILY_OVERWRITE, 1, 1, 1);
	KpWindow windows[1];
	it.x256 = 8 * 256;
	it.px256 = 4 * 256;
	windows[0].rank = 1;
	windows[0].last = 1;
	windows[0].layers = 0;
	KpRowRange ranges[KP_IMAGE_DATA_RUNS];
	int count;
	kp_image_data_begin(d);
	kp_image_data_put(d, &it, 0, 0);
	kp_image_data_finish(d, windows, 1, 0);
	kp_t_eq(T, d->plan.count, 1, NULL);
	kp_t_eq(T, kp_image_data_take(d, ranges, &count), 1, "the write frame uploads");
	kp_t_eq(T, count, 1, "one run");
	kp_t_eq(T, ranges[0].row, 0, "the slot's row");
	kp_t_eq(T, ranges[0].rows, 1, "one row");
	kp_t_eq(T, kp_image_data_take(d, ranges, &count), 0, "once");
	kp_t_eq(T, count, 0, NULL);
	kp_image_data_begin(d);
	kp_image_data_put(d, &it, 0, 0); /* unchanged: nothing to upload */
	kp_image_data_finish(d, windows, 1, 0);
	kp_t_eq(T, kp_image_data_take(d, ranges, &count), 0, "nothing to upload");
}

/* The plan carries each family's slots in use, the most ever taken (a freed slot below stays counted): the slots the
 * GPU's passes draw instead of the families' capacities. */
static void images_plan_counts_the_slots_in_use(KpT *T)
{
	KpImageData *d = image_data(T, 7);
	KpDrawItem a = item(KP_FAMILY_OVERWRITE, 1, 1, 1), b = item(KP_FAMILY_OVERWRITE, 2, 1, 2);
	KpDrawItem c = item(KP_FAMILY_DEST, 3, 1, 3);
	kp_image_data_begin(d);
	kp_image_data_finish(d, NULL, 0, 0);
	kp_t_eq(T, d->plan.slots[KP_FAMILY_OVERWRITE], 0, "none taken");
	kp_image_data_begin(d);
	kp_image_data_put(d, &a, 0, 0);
	kp_image_data_put(d, &b, 0, 0);
	kp_image_data_put(d, &c, 0, 1);
	kp_image_data_finish(d, NULL, 0, 0);
	kp_t_eq(T, d->plan.slots[KP_FAMILY_OVERWRITE], 2, NULL);
	kp_t_eq(T, d->plan.slots[KP_FAMILY_DEST], 1, NULL);
	kp_t_eq(T, d->plan.slots[KP_FAMILY_DISTORT], 0, NULL);
	kp_image_data_begin(d); /* the first image gone: its slot is free, the second keeps its own */
	kp_image_data_put(d, &b, 0, 0);
	kp_image_data_finish(d, NULL, 0, 0);
	kp_t_eq(T, d->plan.slots[KP_FAMILY_OVERWRITE], 2, "the highest slot taken");
	kp_t_eq(T, d->plan.slots[KP_FAMILY_DEST], 1, NULL);
}

/* A tick that fails before its plan (the draw list out of memory) uploads the slots it wrote and draws them: a slot
 * taken past the plan before would lie beyond the draws' count. */
static void images_abort_draws_the_slots_written(KpT *T)
{
	KpImageData *d = image_data(T, 7);
	KpDrawItem a = item(KP_FAMILY_OVERWRITE, 1, 1, 1), b = item(KP_FAMILY_OVERWRITE, 2, 1, 2);
	KpDrawItem c = item(KP_FAMILY_DISTORT, 3, 1, 3);
	kp_image_data_begin(d);
	kp_image_data_put(d, &a, 0, 0);
	kp_image_data_finish(d, NULL, 0, 0);
	kp_t_eq(T, d->plan.slots[KP_FAMILY_OVERWRITE], 1, NULL);
	d->pending = 0;
	kp_image_data_begin(d);
	kp_image_data_put(d, &a, 0, 0);
	kp_image_data_put(d, &b, 0, 0);
	kp_image_data_put(d, &c, 0, 0);
	kp_image_data_abort(d);
	kp_t_eq(T, d->plan.slots[KP_FAMILY_OVERWRITE], 2, "the new slot drawn");
	kp_t_eq(T, d->plan.slots[KP_FAMILY_DISTORT], 1, "the new family's too");
	kp_t_eq(T, d->pending, 1, "uploaded");
	kp_t_ok(T, d->slot_of[KP_FAMILY_OVERWRITE][1] >= 0, "the image before keeps its slot");
}

/* The HD frame table follows the SD table's packs (atlas_in): built over another count of packs it refuses. */
static void hd_frame_table_refuses_other_packs(KpT *T)
{
	KpFrameTable sd, hd;
	KpError e;
	memset(&sd, 0, sizeof sd);
	sd.pack_count = 1;
	kp_t_eq(T, kp_hd_frame_table_build(&hd, &sd, NULL, NULL, NULL, NULL, 2, &e), -1, "refused");
	kp_t_ok(T, strstr(e.msg, "2 packs, the SD table's 1") != NULL, "%s", e.msg);
	kp_t_ok(T, hd.texels == NULL, "nothing made");
}

/* An image without a row (a selection circle past tselect's rows) is left out, counted, and the tick goes on: the
 * images after it are drawn. */
static void images_without_a_row_are_left_out(KpT *T)
{
	KpImages *w = new_world(T);
	ImageDef defs[3] = { { 0, 0, 0 }, { 13, 0, 0 }, { 0, 1, 0 } };
	KpImageData *d = image_data(T, 99);
	KpImagesPresenter *pp = (KpImagesPresenter *)calloc(1, sizeof *pp);
	static KpFrameTable frames;
	static KpLookups lookups;
	static int32_t atlas_base[2] = { -1, 0 };
	static uint32_t afrm[1] = { 0 };
	int32_t keys[3], k;
	if (!pp) kp_t_fail(T, "out of memory");
	kp_t_defer(T, free, pp); /* the cleanups run last first */
	kp_t_defer(T, free_presenter, pp);
	memset(&frames, 0, sizeof frames);
	frames.atlas_base = atlas_base;
	frames.atlas_count = 1;
	frames.afrm = afrm;
	frames.grp_count = w->grp->grp_count;
	frames.empty = 99;
	memset(&lookups, 0, sizeof lookups); /* no selection rows */
	/* kp_presenter_init without the warp texture's image (a world without dat.kpd) */
	pp->w = w;
	pp->grp = w->grp;
	pp->frames = &frames;
	pp->lookups = &lookups;
	pp->list = kp_draw_list_new();
	pp->first_frame = (int32_t *)malloc(sizeof *pp->first_frame * (w->grp->grp_count + 1));
	if (!pp->list || !pp->first_frame) kp_t_fail(T, "out of memory");
	for (k = 0; k <= (int32_t)w->grp->grp_count; k++) pp->first_frame[k] = -2;
	sprite(w, 100, 100, defs, 3, NULL, keys);
	kp_images_refresh_bounds(w);
	kp_t_eq(T, kp_presenter_sync(pp, d, NULL, 0, 0), 3, "the tick goes on");
	kp_t_eq(T, pp->skipped, 1, NULL);
	kp_t_ok(T, strstr(pp->skip.msg, "no row for method 13") != NULL, "%s", pp->skip.msg);
	kp_t_ok(T, d->slot_of[KP_FAMILY_OVERWRITE][keys[0]] >= 0 && d->slot_of[KP_FAMILY_OVERWRITE][keys[2]] >= 0,
		"the others drawn");
	kp_t_eq(T, d->slot_of[KP_FAMILY_OVERWRITE][keys[1]], -1, "no slot");
	kp_t_eq(T, kp_presenter_sync(pp, d, NULL, 0, 0), 3, NULL);
	kp_t_ok(T, pp->skipped == 1 && pp->skipped_before == 1, "the same as the tick before");
}

/* A pack container with no frames or sounds whose pages are w x h. */
static uint8_t *pack_with_pages(KpT *T, int32_t w, int32_t h, size_t *size)
{
	static const uint8_t atls[1] = { 0 }, frms[4] = { 0 }, snds[2] = { 0 };
	uint8_t page[6] = { 0, 0, (uint8_t)w, (uint8_t)(w >> 8), (uint8_t)h, (uint8_t)(h >> 8) }, *out = NULL;
	KpKpfOut sections[4];
	memset(sections, 0, sizeof sections);
	sections[0].fourcc = "ATLS";
	sections[0].payload = atls;
	sections[0].size = sizeof atls;
	sections[1].fourcc = "FRMS";
	sections[1].payload = frms;
	sections[1].size = sizeof frms;
	sections[2].fourcc = "PAGE";
	sections[2].payload = page;
	sections[2].size = sizeof page;
	sections[3].fourcc = "SNDS";
	sections[3].payload = snds;
	sections[3].size = sizeof snds;
	if (kp_kpf_build(KP_PACK_KIND, KP_PACK_SCHEMA, 0, "test", NULL, sections, 4, &out, size) != 0) kp_t_fail(T, "build");
	kp_t_defer(T, free, out);
	return out;
}

/* Every page of every pack has the runtime's size: the loaded pages share one texture array of that size. */
static void packs_refuse_pages_of_another_size(KpT *T)
{
	KpPack p;
	KpError err;
	size_t size;
	uint8_t *ok;
	memset(&err, 0, sizeof err);
	ok = pack_with_pages(T, KP_PACK_PAGE_SIZE, KP_PACK_PAGE_SIZE, &size);
	kp_t_eq(T, kp_pack_load(&p, ok, size, "ok", &err), 0, "%s", err.msg);
	kp_pack_free(&p);
	ok = pack_with_pages(T, KP_PACK_PAGE_SIZE / 2, KP_PACK_PAGE_SIZE / 2, &size);
	kp_t_eq(T, kp_pack_load(&p, ok, size, "small", &err), -1, "smaller pages refused");
	kp_t_ok(T, strstr(err.msg, "PAGE") != NULL, "%s", err.msg);
}

/* Only the texel rows a tick changed go up: a run for each family that wrote, not the rows between the families. */
static void images_upload_the_changed_rows(KpT *T)
{
	KpImageData *d = image_data(T, 7);
	KpDrawItem a = item(KP_FAMILY_OVERWRITE, 1, 1, 1), b = item(KP_FAMILY_DISTORT, 2, 1, 2);
	KpRowRange ranges[KP_IMAGE_DATA_RUNS];
	int count;
	int32_t slots_per_row = KP_IMAGE_DATA_WIDTH / KP_TEXELS_PER_SLOT;
	kp_image_data_begin(d);
	kp_image_data_put(d, &a, 0, 0);
	kp_image_data_put(d, &b, 0, 0);
	kp_image_data_finish(d, NULL, 0, 0);
	kp_t_eq(T, kp_image_data_take(d, ranges, &count), 1, NULL);
	kp_t_eq(T, count, 2, "a run for each family");
	kp_t_eq(T, ranges[0].row, 0, "the overwriting family's first slot");
	kp_t_eq(T, ranges[0].rows, 1, "its row alone");
	kp_t_eq(T, ranges[1].row, KP_FAMILY_BASE[KP_FAMILY_DISTORT] / slots_per_row, "the distortion family's row");
	kp_t_eq(T, ranges[1].rows, 1, "alone");
	b.x256 += 256; /* the distortion image alone moves */
	kp_image_data_begin(d);
	kp_image_data_put(d, &a, 0, 0);
	kp_image_data_put(d, &b, 0, 0);
	kp_image_data_finish(d, NULL, 0, 0);
	kp_t_eq(T, kp_image_data_take(d, ranges, &count), 1, NULL);
	kp_t_eq(T, count, 1, NULL);
	kp_t_eq(T, ranges[0].row, KP_FAMILY_BASE[KP_FAMILY_DISTORT] / slots_per_row, "its row");
	kp_t_eq(T, ranges[0].rows, 1, "alone");
}

/* The last overwriting slot and the first destination slot share a texel row: their runs are one upload. */
static void images_join_the_runs_that_touch(KpT *T)
{
	KpImageData *d = image_data(T, 7);
	KpRowRange ranges[KP_IMAGE_DATA_RUNS];
	int count;
	int32_t key, slots_per_row = KP_IMAGE_DATA_WIDTH / KP_TEXELS_PER_SLOT;
	kp_image_data_begin(d);
	for (key = 0; key < KP_FAMILY_CAPACITY[KP_FAMILY_OVERWRITE]; key++) {
		KpDrawItem a = item(KP_FAMILY_OVERWRITE, key, 1, 1);
		kp_image_data_put(d, &a, 0, 0);
	}
	{
		KpDrawItem b = item(KP_FAMILY_DEST, 0, 1, 2);
		kp_image_data_put(d, &b, 0, 0);
	}
	kp_image_data_finish(d, NULL, 0, 0);
	kp_t_eq(T, kp_image_data_take(d, ranges, &count), 1, NULL);
	kp_t_eq(T, count, 1, "one run");
	kp_t_eq(T, ranges[0].row, 0, NULL);
	kp_t_eq(T, ranges[0].rows, KP_FAMILY_BASE[KP_FAMILY_DEST] / slots_per_row + 1, "through the shared row");
}

static void images_slots_stable_released_and_reused(KpT *T)
{
	KpImageData *d = image_data(T, 7);
	KpDrawItem a10 = item(KP_FAMILY_OVERWRITE, 10, 1, 1), a11 = item(KP_FAMILY_OVERWRITE, 11, 1, 2);
	KpDrawItem b10 = item(KP_FAMILY_DEST, 10, 1, 3), a12 = item(KP_FAMILY_OVERWRITE, 12, 1, 4);
	int32_t s10, s11;
	kp_image_data_begin(d);
	kp_image_data_put(d, &a10, 0, 0);
	kp_image_data_put(d, &a11, 0, 0);
	kp_image_data_put(d, &b10, 0, 0);
	s10 = d->slot_of[KP_FAMILY_OVERWRITE][10];
	s11 = d->slot_of[KP_FAMILY_OVERWRITE][11];
	kp_t_eq(T, s10, 0, NULL);
	kp_t_eq(T, s11, 1, NULL);
	kp_t_eq(T, KP_FAMILY_BASE[KP_FAMILY_DEST] + d->slot_of[KP_FAMILY_DEST][10], KP_FAMILY_CAPACITY_OVERWRITE, NULL);
	kp_image_data_finish(d, NULL, 0, 0);
	kp_image_data_begin(d);
	kp_image_data_put(d, &a11, 0, 0);
	kp_t_eq(T, d->slot_of[KP_FAMILY_OVERWRITE][11], s11, "kept while written every tick");
	kp_image_data_finish(d, NULL, 0, 0);
	kp_t_eq(T, d->slot_of[KP_FAMILY_OVERWRITE][10], -1, "key 10 released in both families");
	kp_t_eq(T, d->slot_of[KP_FAMILY_DEST][10], -1, NULL);
	kp_image_data_begin(d);
	kp_image_data_put(d, &a11, 0, 0);
	kp_image_data_put(d, &a12, 0, 0);
	kp_t_eq(T, d->slot_of[KP_FAMILY_OVERWRITE][12], s10, "reused");
	kp_image_data_finish(d, NULL, 0, 0);
}

/* The n-th slot family f draws in rank order (kp_present.h). */
static int32_t order_at(const KpImageData *d, int f, int32_t n)
{
	return (int32_t)d->data[(size_t)KP_IMAGE_ORDER_TEXEL * 4 + (size_t)(KP_FAMILY_BASE[f] + n)];
}

/* The HD world draws each family in rank order: a reused slot below an image's own must still come after it when its
 * rank is higher (a weld's spark, method 0, took a mineral's freed slot below the next mineral's and its translucent
 * glow, drawn first, cut that mineral out). Free slots come last; the order's row goes up when it changes. */
static void images_order_follows_the_ranks(KpT *T)
{
	KpImageData *d = image_data(T, 7);
	KpDrawItem a = item(KP_FAMILY_OVERWRITE, 1, 1, 1), mineral = item(KP_FAMILY_OVERWRITE, 2, 1, 2);
	KpDrawItem spark = item(KP_FAMILY_OVERWRITE, 3, 1, 3), shadow = item(KP_FAMILY_DEST, 4, 1, 4);
	KpDrawItem glow = item(KP_FAMILY_DEST, 5, 1, 2);
	KpRowRange ranges[KP_IMAGE_DATA_RUNS];
	int count;
	kp_image_data_begin(d);
	kp_image_data_put(d, &a, 0, 0);
	kp_image_data_put(d, &mineral, 0, 0);
	kp_image_data_put(d, &shadow, 0, 1);
	kp_image_data_put(d, &glow, 0, 1);
	kp_image_data_finish(d, NULL, 0, 0);
	kp_t_eq(T, order_at(d, KP_FAMILY_OVERWRITE, 0), 0, NULL);
	kp_t_eq(T, order_at(d, KP_FAMILY_OVERWRITE, 1), 1, NULL);
	kp_t_eq(T, order_at(d, KP_FAMILY_DEST, 0), 1, "the glow (rank 2) before the shadow (rank 4)");
	kp_t_eq(T, order_at(d, KP_FAMILY_DEST, 1), 0, NULL);
	kp_image_data_take(d, ranges, &count);
	kp_image_data_begin(d); /* the first image gone: its slot, 0, freed and drawn last */
	mineral.rank = 1;
	kp_image_data_put(d, &mineral, 0, 0);
	kp_image_data_finish(d, NULL, 0, 0);
	kp_t_eq(T, order_at(d, KP_FAMILY_OVERWRITE, 0), 1, NULL);
	kp_t_eq(T, order_at(d, KP_FAMILY_OVERWRITE, 1), 0, NULL);
	kp_t_eq(T, kp_image_data_take(d, ranges, &count), 1, NULL);
	kp_t_ok(T, count >= 1 && ranges[count - 1].row + ranges[count - 1].rows > KP_IMAGE_ORDER_TEXEL / KP_IMAGE_DATA_WIDTH
		&& ranges[count - 1].row <= KP_IMAGE_ORDER_TEXEL / KP_IMAGE_DATA_WIDTH, "the order's row uploaded");
	kp_image_data_begin(d); /* the spark takes slot 0, below the mineral's */
	spark.rank = 2;
	kp_image_data_put(d, &mineral, 0, 0);
	kp_image_data_put(d, &spark, 0, 0);
	kp_image_data_finish(d, NULL, 0, 0);
	kp_t_eq(T, d->slot_of[KP_FAMILY_OVERWRITE][3], 0, "the freed slot reused");
	kp_t_eq(T, order_at(d, KP_FAMILY_OVERWRITE, 0), 1, "the mineral first");
	kp_t_eq(T, order_at(d, KP_FAMILY_OVERWRITE, 1), 0, "the spark over it");
	kp_t_eq(T, order_at(d, KP_FAMILY_DEST, 0), 0, "the destination slots freed, in slot order");
	kp_t_eq(T, order_at(d, KP_FAMILY_DEST, 1), 1, NULL);
	kp_image_data_begin(d); /* the mineral gone: its slot, 1, free and last */
	kp_image_data_put(d, &spark, 0, 0);
	kp_image_data_finish(d, NULL, 0, 0);
	kp_t_eq(T, order_at(d, KP_FAMILY_OVERWRITE, 0), 0, NULL);
	kp_t_eq(T, order_at(d, KP_FAMILY_OVERWRITE, 1), 1, NULL);
}

/* --- the layouts --- */

/* The original's dialog rectangles of the panels the layouts place (a Protoss console, rez\*.bin as loaded). */
static const struct {
	const char *bin;
	int32_t r[4];
} LAYOUT_PANELS[] = {
	{ "rez\\minimap.bin", { 0, 315, 137, 479 } },
	{ "rez\\statdata.bin", { 138, 388, 407, 479 } },
	{ "rez\\statport.bin", { 408, 408, 495, 479 } },
	{ "rez\\stat_f10.bin", { 408, 388, 495, 407 } },
	{ "rez\\statbtnp.bin", { 496, 354, 639, 479 } },
	{ "rez\\statres.bin", { 220, 0, 639, 19 } },
	{ "rez\\statlb.bin", { 0, 0, 199, 111 } },
	{ "rez\\ptextbox.bin", { 190, 338, 447, 355 } },
};
#define LAYOUT_PANEL_COUNT ((int)(sizeof LAYOUT_PANELS / sizeof LAYOUT_PANELS[0]))
#define TEXTBOX 7

/* 4:3, 16:10, 16:9, 19.5:9, 21:9, 32:9, 9:16, 3:4 from small to 4K and 5120 x 1440 */
static const int32_t WINDOWS[][2] = { { 1024, 768 }, { 1600, 1200 }, { 1280, 800 }, { 1920, 1200 }, { 1280, 720 },
	{ 1366, 768 }, { 1920, 1080 }, { 2560, 1440 }, { 3840, 2160 }, { 2340, 1080 }, { 2560, 1080 }, { 3440, 1440 },
	{ 5120, 1440 }, { 3840, 1080 }, { 1080, 1920 }, { 720, 1280 }, { 768, 1024 }, { 1536, 2048 }, { 640, 480 },
	{ 800, 600 }, { 600, 400 }, { 320, 240 } }; /* the last two smaller than the screen (a desktop window) */
#define WINDOW_COUNT ((int)(sizeof WINDOWS / sizeof WINDOWS[0]))

static int overlap(const int32_t *a, const int32_t *b) { return !(a[2] < b[0] || b[2] < a[0] || a[3] < b[1] || b[3] < a[1]); }

static void place_all(KpT *T, const KpArrangement *a, int32_t w, int32_t h, const char *where, int textbox_overlaps,
	int32_t placed[][4])
{
	int k, j;
	for (k = 0; k < LAYOUT_PANEL_COUNT; k++) {
		const int32_t *r = LAYOUT_PANELS[k].r;
		int32_t x, y;
		kp_t_ok(T, kp_layout_place(a, LAYOUT_PANELS[k].bin, r, &x, &y), "%s: %s placed", where, LAYOUT_PANELS[k].bin);
		placed[k][0] = x;
		placed[k][1] = y;
		placed[k][2] = x + r[2] - r[0];
		placed[k][3] = y + r[3] - r[1];
		kp_t_ok(T, placed[k][0] >= 0 && placed[k][1] >= 0 && placed[k][2] < w && placed[k][3] < h,
			"%s %s: %s at %d,%d outside %dx%d", where, kp_layout_name(a->layout), LAYOUT_PANELS[k].bin, (int)x, (int)y,
			(int)w, (int)h);
		for (j = 0; j < k; j++) {
			if (textbox_overlaps && (k == TEXTBOX || j == TEXTBOX)) continue; /* the text line lies over the console */
			kp_t_ok(T, !overlap(placed[k], placed[j]), "%s %s: %s overlaps %s", where, kp_layout_name(a->layout),
				LAYOUT_PANELS[k].bin, LAYOUT_PANELS[j].bin);
		}
	}
}

static void layouts_fit_every_screen(KpT *T)
{
	static const int MODES[3] = { KP_SCALE_INTEGER, KP_SCALE_SHARP, KP_SCALE_AUTO };
	static const int LAYOUTS[3] = { KP_LAYOUT_CLASSIC, KP_LAYOUT_WIDE, -1 };
	int wi, mi, li;
	for (wi = 0; wi < WINDOW_COUNT; wi++) {
		for (mi = 0; mi < 3; mi++) {
			int m;
			double scale;
			int32_t w, h;
			char where[64];
			kp_layout_screen(WINDOWS[wi][0], WINDOWS[wi][1], MODES[mi], 0, 0, 0, &m, &scale, &w, &h);
			snprintf(where, sizeof where, "%dx%d mode %d", (int)WINDOWS[wi][0], (int)WINDOWS[wi][1], m);
			kp_t_ok(T, w >= KP_LAYOUT_MIN_W && h >= KP_LAYOUT_MIN_H, "%s: smaller than 640 x 480", where);
			kp_t_ok(T, scale > 0 && w * scale <= WINDOWS[wi][0] + scale && h * scale <= WINDOWS[wi][1] + scale,
				"%s: scale", where);
			for (li = 0; li < 3; li++) {
				KpArrangement a;
				int32_t placed[LAYOUT_PANEL_COUNT][4], spans[KP_LAYOUT_GROUPS + KP_LAYOUT_FILLERS][2], n = 0, k, j;
				kp_layout_arrange(&a, w, h, LAYOUTS[li]);
				place_all(T, &a, w, h, where, 1, placed);
				/* the text line, a backdrop dialog under the console art, never lies under a filler's opaque column */
				for (k = 0; k < a.filler_count; k++) {
					const KpLayoutFiller *f = &a.fillers[k];
					kp_t_ok(T, f->top >= a.dy + 290, "%s %s: a filler above the console", where, kp_layout_name(a.layout));
					if (placed[TEXTBOX][0] < f->x + f->w && placed[TEXTBOX][2] >= f->x) {
						kp_t_ok(T, placed[TEXTBOX][3] < f->top, "%s %s: the text line under a filler", where,
							kp_layout_name(a.layout));
					}
				}
				/* the console groups and fillers: one span without a gap, inside the screen */
				for (k = 0; k < KP_LAYOUT_GROUPS; k++) {
					spans[n][0] = a.groups[k].left;
					spans[n++][1] = a.groups[k].right;
				}
				for (k = 0; k < a.filler_count; k++) {
					spans[n][0] = a.fillers[k].x;
					spans[n++][1] = a.fillers[k].x + a.fillers[k].w;
				}
				for (k = 1; k < n; k++) { /* sort by start */
					for (j = k; j > 0 && spans[j][0] < spans[j - 1][0]; j--) {
						int32_t t0 = spans[j][0], t1 = spans[j][1];
						spans[j][0] = spans[j - 1][0];
						spans[j][1] = spans[j - 1][1];
						spans[j - 1][0] = t0;
						spans[j - 1][1] = t1;
					}
				}
				for (k = 1; k < n; k++) kp_t_eq(T, spans[k][0], spans[k - 1][1], "%s: a seam", where);
				kp_t_ok(T, spans[0][0] >= 0 && spans[n - 1][1] <= w, "%s: the console leaves the screen", where);
			}
		}
	}
}

/* A game on a portrait window narrows its logical screen to the narrow layout: the console in two rows, every panel
 * inside the screen and none overlapping; a fixed screen (glue) keeps the original's 640 x 480. */
static void layouts_narrow_fits_portrait_screens(KpT *T)
{
	static const int MODES[3] = { KP_SCALE_INTEGER, KP_SCALE_SHARP, KP_SCALE_AUTO };
	int wi, mi, narrowed = 0;
	for (wi = 0; wi < WINDOW_COUNT; wi++) {
		for (mi = 0; mi < 3; mi++) {
			int m;
			double scale;
			int32_t w, h, min_w, min_h, fw, fh;
			KpArrangement a;
			char where[64];
			kp_layout_minimum(WINDOWS[wi][0], WINDOWS[wi][1], 0, &min_w, &min_h);
			kp_layout_screen(WINDOWS[wi][0], WINDOWS[wi][1], MODES[mi], 0, min_w, min_h, &m, &scale, &w, &h);
			snprintf(where, sizeof where, "%dx%d mode %d", (int)WINDOWS[wi][0], (int)WINDOWS[wi][1], m);
			kp_t_ok(T, w >= min_w && h >= min_h, "%s: smaller than its minimum", where);
			kp_layout_minimum(WINDOWS[wi][0], WINDOWS[wi][1], 1, &min_w, &min_h);
			kp_layout_screen(WINDOWS[wi][0], WINDOWS[wi][1], MODES[mi], 1, min_w, min_h, &m, &scale, &fw, &fh);
			kp_t_ok(T, fw >= KP_LAYOUT_MIN_W && fh >= KP_LAYOUT_MIN_H, "%s: a fixed screen's logical screen", where);
			kp_layout_arrange(&a, w, h, -1);
			if (a.layout == KP_LAYOUT_NARROW) {
				int32_t placed[LAYOUT_PANEL_COUNT][4];
				narrowed++;
				place_all(T, &a, w, h, where, 0, placed);
				kp_t_eq(T, a.groups[1].left, 0, NULL);
				kp_t_eq(T, a.groups[2].left, a.groups[1].right, "the portrait and card beside the status panel");
				kp_t_eq(T, a.groups[0].left, 0, NULL);
				kp_t_eq(T, a.filler_count, 0, NULL);
				kp_t_ok(T, a.groups[2].right <= w, "%s: the console leaves the screen", where);
			}
		}
	}
	kp_t_ok(T, narrowed > 0, "no portrait window narrowed");
}

/* Auto fits a fixed screen (the glue's 640 x 480) whole with the sharp scale and keeps a game's at a whole scale: on
 * a 2184 x 1968 phone (the Fold7's inner screen, landscape) the menus drew at 3x, a margin on every side. */
static void layouts_auto_fits_a_fixed_screen(KpT *T)
{
	static const struct {
		int32_t win_w, win_h;
		int fixed, mode;
		double scale;
		int32_t w, h;
	} CASES[] = {
		{ 2184, 1968, 1, KP_SCALE_SHARP, 2184.0 / 640, 640, 577 },
		{ 2184, 1968, 0, KP_SCALE_INTEGER, 3, 728, 656 },
		{ 1920, 1080, 1, KP_SCALE_SHARP, 1080.0 / 480, 854, 480 },
		{ 1920, 1080, 0, KP_SCALE_INTEGER, 2, 960, 540 },
		{ 1280, 960, 1, KP_SCALE_SHARP, 2, 640, 480 },
		{ 1000, 700, 0, KP_SCALE_SHARP, 700.0 / 480, 686, 480 },
	};
	size_t k;
	for (k = 0; k < sizeof CASES / sizeof CASES[0]; k++) {
		int m;
		double scale;
		int32_t w, h, min_w, min_h;
		kp_layout_minimum(CASES[k].win_w, CASES[k].win_h, CASES[k].fixed, &min_w, &min_h);
		kp_layout_screen(CASES[k].win_w, CASES[k].win_h, KP_SCALE_AUTO, CASES[k].fixed, min_w, min_h, &m, &scale, &w,
			&h);
		kp_t_eq(T, m, CASES[k].mode, "%dx%d fixed %d: mode", (int)CASES[k].win_w, (int)CASES[k].win_h, CASES[k].fixed);
		kp_t_ok(T, fabs(scale - CASES[k].scale) < 1e-9, "%dx%d fixed %d: scale %f", (int)CASES[k].win_w,
			(int)CASES[k].win_h, CASES[k].fixed, scale);
		kp_t_eq(T, w, CASES[k].w, NULL);
		kp_t_eq(T, h, CASES[k].h, NULL);
	}
	{ /* an integer or sharp scale given keeps its mode on a fixed screen */
		int m;
		double scale;
		int32_t w, h;
		kp_layout_screen(2184, 1968, KP_SCALE_INTEGER, 1, 640, 480, &m, &scale, &w, &h);
		kp_t_eq(T, m, KP_SCALE_INTEGER, NULL);
		kp_t_ok(T, scale == 3, "integer: scale %f", scale);
	}
}

/* A fixed screen is drawn over the whole drawable, a game inside the safe area: on the Fold7's cover screen in landscape
 * (2520 x 1080, the cutout's 102 pixels on the left, the gesture zones' 77 above and 73 below) the menus fitted to the
 * safe area were 931 pixels tall with black bands; over the drawable they are 1080 at 2.25x. */
static void layouts_fixed_screen_fills_the_drawable(KpT *T)
{
	static const int32_t SAFE[4] = { 102, 73, 2418, 930 }, NONE[4] = { 0, 0, 0, 0 };
	int32_t p[4], min_w, min_h, w, h;
	int m;
	double scale;
	kp_layout_picture(1, 2520, 1080, SAFE, p);
	kp_t_ok(T, p[0] == 0 && p[1] == 0 && p[2] == 2520 && p[3] == 1080, "fixed: %d,%d %dx%d", (int)p[0], (int)p[1],
		(int)p[2], (int)p[3]);
	kp_layout_minimum(p[2], p[3], 1, &min_w, &min_h);
	kp_layout_screen(p[2], p[3], KP_SCALE_AUTO, 1, min_w, min_h, &m, &scale, &w, &h);
	kp_t_ok(T, fabs(scale - 2.25) < 1e-9, "fixed: scale %f", scale);
	kp_t_eq(T, h, 480, NULL);
	kp_layout_picture(0, 2520, 1080, SAFE, p);
	kp_t_ok(T, memcmp(p, SAFE, sizeof SAFE) == 0, "game: %d,%d %dx%d", (int)p[0], (int)p[1], (int)p[2], (int)p[3]);
	kp_layout_picture(0, 2520, 1080, NONE, p); /* no safe area known: the drawable */
	kp_t_ok(T, p[0] == 0 && p[1] == 0 && p[2] == 2520 && p[3] == 1080, "no safe area");
}

/* The wide layout cuts the console in three as StarCraft: Remastered's HD console: the minimap pinned left, the status
 * panel centred, the portrait and card pinned right, a filler in each gap. */
static void layouts_wide_pins_the_groups(KpT *T)
{
	KpArrangement a, c;
	int32_t x, y;
	kp_layout_arrange(&a, 1920, 480, KP_LAYOUT_WIDE);
	kp_t_eq(T, a.groups[0].left, 0, NULL);
	kp_t_eq(T, a.groups[1].left, (1920 - 270) / 2, "the status panel centred");
	kp_t_eq(T, a.groups[2].right, 1920, NULL);
	kp_t_eq(T, a.filler_count, 2, NULL);
	kp_t_eq(T, a.fillers[0].x, 138, NULL);
	kp_t_eq(T, a.fillers[0].w, a.groups[1].left - 138, NULL);
	kp_t_eq(T, a.fillers[0].column, 70, NULL);
	kp_t_eq(T, a.fillers[1].x, a.groups[1].right, NULL);
	kp_t_eq(T, a.fillers[1].w, a.groups[2].left - a.groups[1].right, NULL);
	kp_t_eq(T, a.fillers[1].column, 300, NULL);
	kp_layout_place(&a, "rez\\statdata.bin", LAYOUT_PANELS[1].r, &x, &y);
	kp_t_eq(T, x, a.groups[1].left, "the status panel moves with its group");
	kp_layout_place(&a, "rez\\statport.bin", LAYOUT_PANELS[2].r, &x, &y);
	kp_t_eq(T, x, a.groups[2].left, "the portrait moves with the card");
	/* too narrow to centre the status panel: the original's arrangement, no gap */
	kp_layout_arrange(&a, 700, 480, KP_LAYOUT_WIDE);
	kp_t_eq(T, a.groups[1].left, 138 + 60, NULL);
	kp_t_eq(T, a.groups[2].left, a.groups[1].right, NULL);
	kp_t_eq(T, a.filler_count, 1, NULL);
	kp_layout_arrange(&c, 640, 480, KP_LAYOUT_CLASSIC);
	kp_t_eq(T, c.filler_count, 0, NULL);
	kp_layout_place(&c, "rez\\stat_f10.bin", LAYOUT_PANELS[3].r, &x, &y);
	kp_t_eq(T, x, 408, NULL);
	kp_t_eq(T, y, 388, "the classic layout of a 640 x 480 screen is the original's");
	{ /* a BIN's name in another case is the same panel, as for the centred dialogs */
		int32_t ux, uy;
		kp_layout_place(&a, "rez\\statdata.bin", LAYOUT_PANELS[1].r, &x, &y);
		kp_t_eq(T, kp_layout_place(&a, "REZ\\StatData.BIN", LAYOUT_PANELS[1].r, &ux, &uy), 1, "placed");
		kp_t_ok(T, ux == x && uy == y, "the same place");
	}
}

/* --- the HD art (ADR 0013; kp_bcn.c, kp_hd.c) --- */

/* BC1: white and black endpoints in four-colour mode, the texels' indices 0..3 along the first row; BC3: an alpha
 * block 255 / 0 in eight-value mode before it. */
static void hd_blocks_decode(KpT *T)
{
	static const uint8_t bc1[8] = { 0xff, 0xff, 0x00, 0x00, 0xe4, 0x00, 0x00, 0x00 };
	static const uint8_t bc3[16] = { 255, 0, 0x08, 0, 0, 0, 0, 0, 0xff, 0xff, 0x00, 0x00, 0xe4, 0x00, 0x00, 0x00 };
	uint8_t rgba[4 * 4 * 4];
	kp_bcn_decode(bc1, 4, 4, 0, rgba);
	kp_t_ok(T, rgba[0] == 255 && rgba[3] == 255, "texel 0: colour 0, white");
	kp_t_ok(T, rgba[4] == 0 && rgba[7] == 255, "texel 1: colour 1, black");
	kp_t_eq(T, rgba[8], 170, "texel 2: two thirds of white");
	kp_t_eq(T, rgba[12], 85, "texel 3: a third");
	kp_t_ok(T, rgba[16] == 255, "row 1: index 0");
	kp_bcn_decode(bc3, 4, 4, 1, rgba);
	kp_t_eq(T, rgba[3], 255, "BC3 texel 0: alpha 0 (255)");
	kp_t_eq(T, rgba[7], 0, "BC3 texel 1: alpha index 1 (0)");
	kp_t_eq(T, rgba[11], 255, "BC3 texel 2: alpha index 0");
}

/* ETC's colour blocks decode by the spec's layout: an individual block (two 4-bit bases, tables, flip, the indices'
 * msb and lsb planes in column-major order) and a planar one (an overflowing blue sum). */
static void etc_blocks_decode_by_the_layout(KpT *T)
{
	/* individual, flip 0: left half base (15, 0, 0), right half (0, 0, 15); tables 0 and 7; texel (0, 0) index 1
	 * (+8), texel (3, 3) index 2 (-47), the rest 0 (+2 and +47) */
	static const uint8_t individual[8] = { 0xf0, 0x00, 0x0f, 0x1c, 0x80, 0x00, 0x00, 0x01 };
	/* planar: O = H = V = (63, 127, 63), the free bits making blue's differential sum overflow: white everywhere */
	uint8_t planar[8], rgba[4 * 4 * 4];
	uint64_t b = (uint64_t)63 << 57 | (uint64_t)1 << 56 | (uint64_t)63 << 49 | (uint64_t)1 << 48 | (uint64_t)3 << 43
		| (uint64_t)7 << 39 | (uint64_t)31 << 34 | (uint64_t)1 << 32 | (uint64_t)127 << 25 | (uint64_t)63 << 19
		| (uint64_t)63 << 13 | (uint64_t)127 << 6 | 63 | (uint64_t)1 << 33 | (uint64_t)7 << 45; /* blue's sum 31 + 3 */
	int k;
	for (k = 0; k < 8; k++) planar[k] = (uint8_t)(b >> (56 - 8 * k));
	kp_etc2_rgb_decode(individual, 4, 4, rgba);
	kp_t_ok(T, rgba[0] == 255 && rgba[1] == 8 && rgba[2] == 8, "texel (0, 0): red plus 8");
	kp_t_ok(T, rgba[4 * 4] == 255 && rgba[4 * 4 + 1] == 2, "texel (0, 1): red plus 2");
	kp_t_ok(T, rgba[4 * 3] == 47 && rgba[4 * 3 + 2] == 255, "texel (3, 0): the right half, blue plus 47");
	kp_t_ok(T, rgba[4 * 15] == 0 && rgba[4 * 15 + 2] == 208, "texel (3, 3): blue less 47");
	kp_t_eq(T, rgba[3], 255, "opaque");
	kp_etc2_rgb_decode(planar, 4, 4, rgba);
	kp_t_ok(T, rgba[0] == 255 && rgba[1] == 255 && rgba[2] == 255 && rgba[60] == 255, "planar: white");
}

/* Encoded and decoded again: a brightness ramp within ETC's error, EAC's flat alpha exactly, a two-colour mask exactly
 * in R11, a flat colour within 2. */
static void etc_round_trip(KpT *T)
{
	enum { W = 16, H = 16 };
	uint8_t rgba[W * H * 4], back[W * H * 4], blocks[(W / 4) * (H / 4) * 16], mask[W * H], mask_back[W * H];
	double err = 0;
	int k, worst = 0;
	for (k = 0; k < W * H; k++) {
		int x = k % W, y = k / W;
		rgba[4 * k] = (uint8_t)(x * 8 + y * 4 + 20); /* a ramp of brightness with a tint: what ETC's tables model */
		rgba[4 * k + 1] = (uint8_t)(x * 8 + y * 4);
		rgba[4 * k + 2] = (uint8_t)(x * 8 + y * 4 + 10);
		rgba[4 * k + 3] = (uint8_t)(y < 4 ? 0 : y < 8 ? 255 : 17 * (x % 16));
		mask[k] = (uint8_t)((x / 3 + y) % 2 ? 255 : 0);
	}
	kp_etc2_encode(rgba, W, H, blocks);
	kp_etc2_decode(blocks, W, H, back);
	for (k = 0; k < W * H; k++) {
		int c, a = abs(back[4 * k + 3] - rgba[4 * k + 3]);
		if (a > worst) worst = a;
		if (k / W < 4 || k / W >= 8) continue; /* the opaque rows' colour */
		for (c = 0; c < 3; c++) err += (double)(back[4 * k + c] - rgba[4 * k + c]) * (back[4 * k + c] - rgba[4 * k + c]);
	}
	kp_t_ok(T, err / (W * 4 * 3) < 40.0, "the gradient's colour within ETC's error");
	kp_t_ok(T, worst <= 8, "alpha within 8");
	kp_t_ok(T, back[3] == 0 && back[4 * (5 * W) + 3] == 255, "transparent and opaque alpha exact");
	kp_eac_r11_encode(mask, W, H, blocks);
	kp_eac_r11_decode(blocks, W, H, mask_back);
	kp_t_ok(T, memcmp(mask, mask_back, sizeof mask) == 0, "a 0 / 255 mask exact in R11");
	{ /* RG11: two independent channels, red's block first */
		uint8_t green[W * H], green_back[W * H];
		for (k = 0; k < W * H; k++) green[k] = (uint8_t)(255 - mask[k]);
		kp_eac_rg11_encode(mask, green, W, H, blocks);
		kp_eac_rg11_decode(blocks, W, H, mask_back, green_back);
		kp_t_ok(T, memcmp(mask, mask_back, sizeof mask) == 0, "RG11's red");
		kp_t_ok(T, memcmp(green, green_back, sizeof green) == 0, "RG11's green");
		kp_eac_r11_encode(mask, W, H, back);
		kp_t_ok(T, memcmp(back, blocks, 8) == 0, "RG11's first block is R11's");
	}
	memset(rgba, 0, sizeof rgba);
	for (k = 0; k < W * H; k++) {
		rgba[4 * k] = 200;
		rgba[4 * k + 1] = 100;
		rgba[4 * k + 2] = 50;
		rgba[4 * k + 3] = 255;
	}
	kp_etc2_rgb_encode(rgba, W, H, blocks);
	kp_etc2_rgb_decode(blocks, W, H, back);
	kp_t_ok(T, abs(back[0] - 200) <= 2 && abs(back[1] - 100) <= 2 && abs(back[2] - 50) <= 2, "a flat colour within 2");
}

/* The HD world's tone: what each base level became (a fade to half, the skipped entries not counted, levels between
 * and past the known ones), the identity, and a grey palette. */
static void palette_tone_follows_the_fades(KpT *T)
{
	uint8_t base[768], current[768], skip[256], tone[1024];
	int k;
	memset(skip, 0, sizeof skip);
	for (k = 0; k < 256; k++) {
		int v = k < 128 ? 2 * k : 255 - (k - 128); /* levels 0, 2, 4 ... and 128 ... 255: all but some odd ones */
		base[3 * k] = (uint8_t)v;
		base[3 * k + 1] = (uint8_t)(v / 2);
		base[3 * k + 2] = (uint8_t)(255 - v);
		current[3 * k] = (uint8_t)(v / 2);
		current[3 * k + 1] = (uint8_t)(v / 4);
		current[3 * k + 2] = (uint8_t)((255 - v) / 2);
	}
	base[3 * 7] = 100, current[3 * 7] = 250; /* a cycling entry */
	skip[7] = 1;
	kp_palette_tone(base, current, skip, tone);
	kp_t_eq(T, tone[4 * 100], 50, "red 100 halved");
	kp_t_eq(T, tone[4 * 254], 127, "red 254 halved");
	kp_t_ok(T, tone[4 * 41] == 20 || tone[4 * 41] == 21, "an odd level between two known ones");
	kp_t_eq(T, tone[4 * 255], 127, "the brightest");
	kp_t_eq(T, tone[4 * 200 + 2], 100, "blue 200 halved");
	kp_t_eq(T, tone[3], 0, "not grey");
	kp_palette_tone(base, base, skip, tone);
	kp_t_ok(T, tone[4 * 100] == 100 && tone[4 * 37 + 1] == 37 && tone[4 * 255 + 2] == 255, "the base itself: identity");
	for (k = 0; k < 256; k++) current[3 * k] = current[3 * k + 1] = current[3 * k + 2] = (uint8_t)k;
	kp_palette_tone(base, current, skip, tone);
	kp_t_eq(T, tone[3], 255, "a grey palette over a coloured base");
}

/* The water's normal maps load from water.kpd; the terrain atlas carries each slot's water mask (dry: zero). */
static void hd_water_masks_and_normals(KpT *T)
{
	uint8_t tiles[2 * 32], masks[16], wmap[4] = { 0, 0, 1, 0 }, head[12] = { 1, 0, 4, 0, 2, 0, 4, 0, 'D', 'X', 'T', '1' };
	uint8_t a[8], b[16];
	static const uint16_t ids[2] = { 1, 0 };
	KpKpfOut sections[3] = { { "HEAD", head, sizeof head, 0 }, { "NRMA", a, sizeof a, 0 }, { "NRMB", b, sizeof b, 0 } };
	KpHdTileset ts;
	KpHdTerrain at;
	KpHdWater w;
	KpError e;
	uint8_t *file;
	size_t size;
	memset(tiles, 0, sizeof tiles);
	memset(masks, 200, sizeof masks);
	memset(&ts, 0, sizeof ts);
	ts.count = 2;
	ts.size = 4;
	ts.tiles = tiles;
	ts.water_map = wmap;
	ts.water_masks = masks;
	ts.water_mask_count = 1;
	kp_t_eq(T, kp_hd_terrain_build(&at, &ts, ids, 2, 1, &e), 0, "built");
	kp_t_ok(T, at.water != NULL, "the map has water");
	kp_t_ok(T, at.water[0] == 200 && at.water[3 * at.w + 3] == 200, "slot 0 (megatile 1): its mask");
	kp_t_ok(T, at.water[4] == 0, "slot 1 (megatile 0): dry");
	kp_hd_terrain_free(&at);
	ts.tiles = NULL; /* the original's art with the water masks: the slots and masks alone */
	kp_t_eq(T, kp_hd_terrain_build(&at, &ts, ids, 2, 1, &e), 0, "built without tiles");
	kp_t_ok(T, at.blocks == NULL && at.size == 0 && at.slots[0] == 0 && at.water && at.water[0] == 200,
		"no blocks; the slots and masks");
	kp_hd_terrain_free(&at);
	memset(a, 1, sizeof a);
	memset(b, 2, sizeof b);
	kp_t_eq(T, kp_kpf_build(KP_HD_WATER_KIND, KP_HD_SCHEMA, 0, "test", NULL, sections, 3, &file, &size), 0, "built");
	kp_t_defer(T, free, file);
	kp_t_eq(T, kp_hd_water_load(&w, file, size, "water", &e), 0, "loaded");
	kp_t_ok(T, w.frames[0] == 1 && w.size[0] == 4 && w.frames[1] == 2 && w.format == KP_HD_FORMAT_BC, "HEAD");
	kp_t_ok(T, w.blocks[1][0] == 2, "NRMB's blocks");
}

/* ui.kpd's assets are found by the SD path (case and separator ignored) and its frames read; the atlas places a
 * frame once, along shelves and layers, and refuses one that fits nowhere until it is emptied. */
static void hd_ui_index_and_atlas(KpT *T)
{
	uint8_t head[7] = { 2, 2, 0, 3, 0, 0, 0 }, aset[2 * 72], frms[3 * 18], imgs[8];
	KpKpfOut sections[4] = { { "HEAD", head, sizeof head, 0 }, { "ASET", aset, sizeof aset, 0 },
		{ "FRMS", frms, sizeof frms, 0 }, { "IMGS", imgs, sizeof imgs, 0 } };
	uint8_t *file;
	size_t size, need;
	KpHdUi u;
	KpHdUiFrame f;
	KpHdUiAtlas a;
	KpHdUiPlace *p;
	KpError e;
	int32_t first, count;
	int flags, fresh;
	memset(aset, 0, sizeof aset);
	memset(frms, 0, sizeof frms);
	memset(imgs, 0x5a, sizeof imgs);
	memcpy(aset, "game\\tconsole.pcx", 17);
	aset[64] = 0, aset[68] = 2, aset[70] = KP_HD_UI_CONSOLE;
	memcpy(aset + 72, "unit\\cmdbtns\\cmdicons.grp", 25);
	aset[72 + 64] = 2, aset[72 + 68] = 1;
	frms[2 * 18 + 4] = 8, frms[2 * 18 + 8] = 4, frms[2 * 18 + 10] = 4, frms[2 * 18 + 12] = KP_HD_UI_BC1;
	frms[1 * 18 + 14] = 0xfe, frms[1 * 18 + 15] = 0xff; /* x -2 */
	kp_t_eq(T, kp_kpf_build(KP_HD_UI_KIND, KP_HD_UI_SCHEMA, 0, "test", NULL, sections, 4, &file, &size), 0, "built");
	kp_t_defer(T, free, file);
	need = kp_hd_ui_head_size(file, size, size);
	kp_t_ok(T, need > 0 && need < size, "the head ends before IMGS");
	kp_t_eq(T, kp_hd_ui_open(&u, file, need, size, "ui", &e), 0, "opened");
	kp_t_ok(T, kp_hd_ui_asset(&u, "Unit/CmdBtns/CMDICONS.grp", 25, &first, &count, &flags) == 1 && first == 2
		&& count == 1 && flags == 0, "an asset by its path");
	kp_t_ok(T, kp_hd_ui_asset(&u, "game\\tconsole.pcx", 17, &first, &count, &flags) == 0 && count == 2
		&& (flags & KP_HD_UI_CONSOLE), "the console's pieces");
	kp_t_eq(T, kp_hd_ui_asset(&u, "game\\tconsole.pc", 16, NULL, NULL, NULL), -1, "a prefix is no match");
	kp_hd_ui_frame(&u, 1, &f);
	kp_t_eq(T, f.x, -2, "a frame's signed offset");
	kp_hd_ui_frame(&u, 2, &f);
	kp_t_ok(T, f.size == 8 && f.w == 4 && file[f.offset] == 0x5a, "a frame's image in IMGS");
	kp_t_eq(T, kp_hd_ui_atlas_init(&a, 16, 2, 4), 0, "atlas");
	p = kp_hd_ui_atlas_place(&a, 0, 6, 6, &fresh);
	kp_t_ok(T, p && fresh && p->layer == 0 && p->x == 0, "the first frame");
	kp_t_ok(T, kp_hd_ui_atlas_place(&a, 0, 6, 6, &fresh) == p && !fresh, "placed once");
	p = kp_hd_ui_atlas_place(&a, 1, 6, 4, &fresh);
	kp_t_ok(T, p && p->x == 8 && p->y == 0, "beside it on its shelf, past the gap");
	p = kp_hd_ui_atlas_place(&a, 2, 12, 12, &fresh);
	kp_t_ok(T, p && p->layer == 1, "too tall for the rest of layer 0: layer 1");
	kp_t_ok(T, kp_hd_ui_atlas_place(&a, 3, 14, 14, &fresh) == NULL, "fits nowhere");
	kp_hd_ui_atlas_reset(&a);
	kp_t_ok(T, (p = kp_hd_ui_atlas_place(&a, 3, 14, 14, &fresh)) != NULL && fresh && p->layer == 0, "after a reset");
	kp_hd_ui_atlas_free(&a);
}

/* An HD frame lies at its offset from the SD frame it replaces, in HD pixels (the minimap's blink: frame 13 is 18 x 18
 * at (55, 55) in the SD art, 146 x 146 in HD2's from the canvas's origin); a wireframe is one of its square layers. */
static void hd_ui_frame_place(KpT *T)
{
	KpHdUiFrame blink = { 0, 0, 146, 146, KP_HD_UI_BC1, -110, -110 }, icon = { 0, 0, 72, 72, KP_HD_UI_BC1, -5, -4 };
	KpHdUiFrame wire = { 0, 0, KP_HD_UI_WIRE_LAYERS * 128, 128, KP_HD_UI_BC3, -26, 0 };
	float r[4];
	kp_hd_ui_frame_place(2, &blink, 100 + 55, 40 + 55, r);
	kp_t_ok(T, r[0] == 100 && r[1] == 40 && r[2] == 73 && r[3] == 73, "a blink frame at its canvas: %g,%g %gx%g",
		(double)r[0], (double)r[1], (double)r[2], (double)r[3]);
	kp_hd_ui_frame_place(2, &icon, 10, 20, r);
	kp_t_ok(T, r[0] == 7.5f && r[1] == 18 && r[2] == 36 && r[3] == 36, "a frame at a half-pixel offset");
	kp_hd_ui_frame_place(2, &wire, 13, 0, r);
	kp_t_ok(T, r[0] == 0 && r[1] == 0 && r[2] == 64 && r[3] == 64, "a wireframe's layer on its GRP canvas");
}

/* portraits.kpd (HDPV) opened from its head; a clip by its SD movie's path (case and separator ignored), at DATA's
 * offset plus its own. */
static void hd_video_index(KpT *T)
{
	uint8_t head[2] = { 2, 0 }, clips[2 * 72], data[12];
	KpKpfOut sections[3] = { { "HEAD", head, sizeof head, 0 }, { "CLIP", clips, sizeof clips, 0 },
		{ "DATA", data, sizeof data, 0 } };
	uint8_t *file;
	size_t size, need;
	uint32_t offset, n;
	KpHdVideo v;
	KpError e;
	memset(clips, 0, sizeof clips);
	memset(data, 0x11, 4);
	memset(data + 4, 0x22, 8);
	memcpy(clips, "portrait\\tmarine\\tmatlk00.smk", 29);
	clips[68] = 4;
	memcpy(clips + 72, "portrait\\zling\\zzefid00.smk", 27);
	clips[72 + 64] = 4, clips[72 + 68] = 8;
	kp_t_eq(T, kp_kpf_build(KP_HD_VIDEO_KIND, KP_HD_SCHEMA, 0, "test", NULL, sections, 3, &file, &size), 0, "built");
	kp_t_defer(T, free, file);
	need = kp_hd_video_head_size(file, size, size);
	kp_t_ok(T, need > 0 && need < size, "the head ends before DATA");
	kp_t_eq(T, kp_hd_video_open(&v, file, need, size, "portraits", &e), 0, "opened");
	kp_t_ok(T, kp_hd_video_clip(&v, "Portrait/ZLing/ZZEFID00.SMK", 27, &offset, &n) == 1 && n == 8
		&& file[offset] == 0x22 && file[offset + 7] == 0x22, "a clip by its path, in DATA");
	kp_t_ok(T, kp_hd_video_clip(&v, "portrait\\tmarine\\tmatlk00.smk", 29, &offset, &n) == 0 && n == 4
		&& file[offset] == 0x11, "the first clip");
	kp_t_eq(T, kp_hd_video_clip(&v, "portrait\\tmarine\\tmatlk0", 24, NULL, NULL), -1, "a prefix is no match");
	head[0] = 3; /* a clip more than CLIP holds */
	kp_t_eq(T, kp_kpf_build(KP_HD_VIDEO_KIND, KP_HD_SCHEMA, 0, "test", NULL, sections, 3, &file, &size), 0, "built");
	kp_t_defer(T, free, file);
	kp_t_eq(T, kp_hd_video_open(&v, file, size, size, "portraits", &e), -1, "HEAD against CLIP");
}

/* The map's megatiles into atlas slots in the order of their first cell; a tile's block rows at its slot. */
static void hd_terrain_atlas_slots(KpT *T)
{
	uint8_t tiles[3 * 8 * 4]; /* three 8 x 8 tiles: four BC1 blocks each, every byte its tile's number */
	static const uint16_t ids[4] = { 2, 0, 2, 0x8001 };
	KpHdTileset ts;
	KpHdTerrain a;
	KpError e;
	int k;
	memset(&ts, 0, sizeof ts);
	for (k = 0; k < 3; k++) memset(tiles + k * 32, k + 1, 32);
	ts.count = 3;
	ts.size = 8;
	ts.tiles = tiles;
	kp_t_eq(T, kp_hd_terrain_build(&a, &ts, ids, 2, 2, &e), 0, "built");
	kp_t_ok(T, a.slots[0] == 0 && a.slots[1] == 1 && a.slots[2] == 0 && a.slots[3] == 2, "slots by first use, bit 15 off");
	kp_t_eq(T, a.columns, KP_HD_TERRAIN_SIDE / 8, "slots a row");
	kp_t_eq(T, a.w, KP_HD_TERRAIN_SIDE, "the atlas's width");
	kp_t_eq(T, a.h, 8, "one row of slots");
	kp_t_ok(T, a.rows == 1 && a.layers == 1, "one layer");
	kp_t_eq(T, a.blocks[0], 3, "slot 0: tile 2");
	kp_t_eq(T, a.blocks[2 * 8], 1, "slot 1: tile 0");
	kp_t_eq(T, a.blocks[(size_t)(a.w / 4) * 8 + 4 * 8], 2, "slot 2's second block row: tile 1");
	{ /* the map's megatiles change (creep): the slots follow while the atlas has them */
		uint16_t shown[4] = { 0, 0x8002, 1, 1 };
		kp_t_eq(T, kp_hd_terrain_relink(&a, shown), 0, "relinked");
		kp_t_ok(T, a.slots[0] == 1 && a.slots[1] == 0 && a.slots[2] == 2 && a.slots[3] == 2, "the shown megatiles' slots");
		kp_hd_terrain_free(&a);
		ts.count = 3;
		kp_t_eq(T, kp_hd_terrain_build(&a, &ts, shown, 2, 2, &e), 0, "built");
		shown[3] = 2;
		kp_t_eq(T, kp_hd_terrain_relink(&a, shown), 0, "a megatile the atlas has");
		shown[3] = 0x7000; /* past the tileset: the first, which this atlas has */
		kp_t_eq(T, kp_hd_terrain_relink(&a, shown), 0, "a megatile past the tileset");
	}
	kp_hd_terrain_free(&a);
	{ /* one the atlas lacks: built again */
		static const uint16_t two[4] = { 2, 2, 2, 2 };
		const uint16_t more[4] = { 2, 2, 2, 1 };
		kp_t_eq(T, kp_hd_terrain_build(&a, &ts, two, 2, 2, &e), 0, "built");
		kp_t_eq(T, kp_hd_terrain_relink(&a, more), 1, "a megatile without a slot");
		kp_hd_terrain_free(&a);
	}
}

/* The atlas's layers are no larger than GLES 3.0's least texture size: a map's megatiles past a layer go on in the
 * next, every layer of the same size; past the layers it holds the atlas is refused. */
static void hd_terrain_atlas_layers(KpT *T)
{
	enum { TILE = 1024, TILES = 5, BLOCKS = TILE / 4 }; /* two by two slots a layer */
	static const uint16_t ids[TILES] = { 0, 1, 2, 3, 4 };
	size_t tile_bytes = (size_t)BLOCKS * BLOCKS * 8, layer_bytes;
	uint8_t *tiles = (uint8_t *)malloc(tile_bytes * TILES), *masks = (uint8_t *)malloc((size_t)TILE * TILE);
	uint8_t water_map[2 * TILES];
	uint16_t *many;
	KpHdTileset ts;
	KpHdTerrain a;
	KpError e;
	int k;
	kp_t_defer(T, free, tiles);
	kp_t_defer(T, free, masks);
	if (!tiles || !masks) kp_t_fail(T, "out of memory");
	for (k = 0; k < TILES; k++) memset(tiles + tile_bytes * (size_t)k, k + 1, tile_bytes);
	memset(masks, 255, (size_t)TILE * TILE);
	memset(water_map, 0, sizeof water_map);
	water_map[2 * 4] = 1; /* the last megatile is water */
	memset(&ts, 0, sizeof ts);
	ts.count = TILES;
	ts.size = TILE;
	ts.tiles = tiles;
	ts.water_map = water_map;
	ts.water_masks = masks;
	kp_t_eq(T, kp_hd_terrain_build(&a, &ts, ids, TILES, 1, &e), 0, "%s", e.msg);
	if (!a.blocks) kp_t_fail(T, "no atlas");
	kp_t_ok(T, a.columns == 2 && a.rows == 2 && a.layers == 2, "%d x %d slots, %d layers", (int)a.columns,
		(int)a.rows, (int)a.layers);
	kp_t_ok(T, a.w == KP_HD_TERRAIN_SIDE && a.h == KP_HD_TERRAIN_SIDE, "a layer %d x %d", (int)a.w, (int)a.h);
	layer_bytes = (size_t)(a.w / 4) * (size_t)(a.h / 4) * 8;
	kp_t_eq(T, (int64_t)a.size, (int64_t)(layer_bytes * 2), "two layers of blocks");
	kp_t_eq(T, a.blocks[((size_t)BLOCKS * (size_t)(a.w / 4) + BLOCKS) * 8], 4, "slot 3: the first layer's last place");
	kp_t_eq(T, a.blocks[layer_bytes], 5, "slot 4: the second layer's first place");
	kp_t_eq(T, a.blocks[layer_bytes + (size_t)BLOCKS * 8], 0, "the rest of the second layer empty");
	kp_t_ok(T, a.water && a.water[(size_t)a.w * (size_t)a.h] == 255 && a.water[0] == 0, "the water mask's layer");
	kp_hd_terrain_free(&a);
	{ /* more megatiles than the layers hold */
		int32_t n = 4 * KP_HD_TERRAIN_LAYERS + 1;
		KpHdTerrain b;
		many = (uint16_t *)malloc(sizeof *many * (size_t)n);
		kp_t_defer(T, free, many);
		if (!many) kp_t_fail(T, "out of memory");
		for (k = 0; k < n; k++) many[k] = (uint16_t)k;
		ts.count = n;
		ts.tiles = NULL; /* the slots alone */
		ts.water_map = NULL;
		kp_t_eq(T, kp_hd_terrain_build(&b, &ts, many, n, 1, &e), -1, "refused");
		kp_t_ok(T, strstr(e.msg, "layers") != NULL, "%s", e.msg);
		kp_t_ok(T, b.slots == NULL && b.slot_of == NULL, "nothing kept");
	}
}

/* Creep's edges: the frame the game draws over a tile (bit 15 of its megatile word and its border byte, kp_creep.c), an
 * HD tileset's edge tiles (EDGE) carried into the map's atlas. */
static void terrain_creep_edges(KpT *T)
{
	uint8_t head[8] = { 1, 0, 4, 0, 'D', 'X', 'T', '1' }, tile[8], edges[2 + 2 * 4 * 4 * 4];
	KpKpfOut sections[3] = { { "HEAD", head, sizeof head, 0 }, { "TILE", tile, sizeof tile, 0 },
		{ "EDGE", edges, sizeof edges, 0 } };
	static const uint16_t ids[1] = { 0 };
	uint8_t *file;
	size_t size;
	KpHdTileset ts;
	KpHdTerrain a;
	KpError e;
	kp_t_eq(T, kp_terrain_edge(0x8012, 3, 37), 3, "bit 15 and border 3: frame 2");
	kp_t_eq(T, kp_terrain_edge(0x0012, 3, 37), 0, "no bit 15: none (a creep tile keeps its stale border)");
	kp_t_eq(T, kp_terrain_edge(0x8012, 0, 37), 0, "border 0: none");
	kp_t_eq(T, kp_terrain_edge(0x8012, 38, 37), 0, "a frame past the tileset's: none");
	kp_t_eq(T, kp_terrain_edge(0x8012, 1, 0), 0, "a tileset without edges");
	memset(tile, 0, sizeof tile);
	memset(edges, 0, sizeof edges);
	edges[0] = 2;
	edges[2 + 4 * 4 * 4 + 3] = 255; /* the second edge's first texel opaque */
	kp_t_eq(T, kp_kpf_build(KP_HD_TILESET_KIND, KP_HD_SCHEMA, 0, "test", NULL, sections, 3, &file, &size), 0, "built");
	kp_t_defer(T, free, file);
	kp_t_eq(T, kp_hd_tileset_load(&ts, file, size, "test", &e), 0, "%s", e.msg);
	kp_t_ok(T, ts.edge_count == 2 && ts.edges && ts.edges[4 * 4 * 4 + 3] == 255, "two edge tiles of RGBA8");
	kp_t_eq(T, kp_hd_terrain_build(&a, &ts, ids, 1, 1, &e), 0, "built");
	kp_t_ok(T, a.edges == ts.edges && a.edge_count == 2, "the atlas carries the edges");
	kp_hd_terrain_free(&a);
	sections[2].size--;
	kp_t_eq(T, kp_kpf_build(KP_HD_TILESET_KIND, KP_HD_SCHEMA, 0, "test", NULL, sections, 3, &file, &size), 0, "built");
	kp_t_defer(T, free, file);
	kp_t_eq(T, kp_hd_tileset_load(&ts, file, size, "test", &e), -1, "EDGE short of its tiles refused");
	head[0] = 0; /* no tiles */
	sections[1].size = 0;
	sections[2].size++;
	kp_t_eq(T, kp_kpf_build(KP_HD_TILESET_KIND, KP_HD_SCHEMA, 0, "test", NULL, sections, 3, &file, &size), 0, "built");
	kp_t_defer(T, free, file);
	kp_t_eq(T, kp_hd_tileset_load(&ts, file, size, "test", &e), -1, "a tileset without tiles refused");
}

/* An HD pack opens from its header, section table and frames; its pages are found by their offsets. */
static void hd_pack_opens_from_its_head(KpT *T)
{
	/* scale 2, 4 x 4 pages, two pages, one frame, no lit pages */
	uint8_t head[KP_HD_PACK_HEAD_BYTES] = { 2, 4, 0, 2, 0, 1, 0, 0, 0, KP_HD_FORMAT_BC, 0, 0 };
	uint8_t frame[14] = { 1, 0, 0, 0, 0, 0, 4, 0, 4, 0, 0, 0, 0, 0 };
	uint8_t diffuse[2 * 16], team[2 * 8];
	KpKpfOut sections[4] = { { "HEAD", head, sizeof head, 0 }, { "FRMS", frame, sizeof frame, 0 },
		{ "DIFF", diffuse, sizeof diffuse, 0 }, { "TEAM", team, sizeof team, 0 } };
	uint8_t *file;
	size_t size, need;
	uint32_t d, m;
	KpHdPack p;
	KpError e;
	memset(diffuse, 0xd0, sizeof diffuse);
	memset(team, 0x7e, sizeof team);
	diffuse[16] = 0xd1;
	team[8] = 0x7f;
	kp_t_eq(T, kp_kpf_build(KP_HD_PACK_KIND, KP_HD_PACK_SCHEMA, 0, "test", NULL, sections, 4, &file, &size), 0, "built");
	kp_t_defer(T, free, file);
	need = kp_hd_pack_head_size(file, size < KP_HD_PACK_FIRST_READ ? size : KP_HD_PACK_FIRST_READ, size);
	kp_t_ok(T, need > 0 && need < size, "the head ends before the pages");
	kp_t_eq(T, kp_hd_pack_open(&p, file, need, size, "test", &e), 0, "opened from the head");
	kp_t_ok(T, p.scale == 2 && p.page_size == 4 && p.page_count == 2 && p.frame_count == 1 && p.format == KP_HD_FORMAT_BC,
		"HEAD");
	kp_t_eq(T, kp_le_u16(p.frames), 1, "the frame's page");
	kp_hd_page_offsets(&p, 1, &d, &m, NULL);
	kp_t_ok(T, file[d] == 0xd1 && file[m] == 0x7f, "page 1's blocks at its offsets");
	kp_t_eq(T, p.lit_pages, 0, "no lit pages");
	kp_t_ok(T, kp_hd_pack_open(&p, file, need - 1, size, "test", &e) != 0, "a head short of its frames refused");
	kp_kpf_build(KP_HD_PACK_KIND, KP_HD_SCHEMA, 0, "test", NULL, sections, 4, &file, &size);
	kp_t_defer(T, free, file);
	kp_t_ok(T, kp_hd_pack_open(&p, file, size, size, "test", &e) != 0, "a pack of the schema before the lit pages refused");
	sections[0].size = 10;
	kp_kpf_build(KP_HD_PACK_KIND, KP_HD_PACK_SCHEMA, 0, "test", NULL, sections, 4, &file, &size);
	kp_t_defer(T, free, file);
	kp_t_ok(T, kp_hd_pack_open(&p, file, size, size, "test", &e) != 0, "a HEAD without its lit pages refused");
	sections[0].size = sizeof head;
	head[9] = 7;
	kp_kpf_build(KP_HD_PACK_KIND, KP_HD_PACK_SCHEMA, 0, "test", NULL, sections, 4, &file, &size);
	kp_t_defer(T, free, file);
	kp_t_ok(T, kp_hd_pack_open(&p, file, size, size, "test", &e) != 0, "an unknown page format refused");
	kp_t_eq(T, kp_hd_pack_head_size(file, 64, size), 0, "fewer bytes than the section table: no size");
	{ /* frames outside their pages, as the SD pack's */
		static const struct {
			int at, value;
			const char *what;
		} BAD[] = { { 0, 2, "a page past the pack's" }, { 2, 1, "past the page's right" }, { 8, 5, "past its bottom" } };
		size_t k;
		head[9] = KP_HD_FORMAT_BC;
		for (k = 0; k < sizeof BAD / sizeof BAD[0]; k++) {
			uint8_t keep = frame[BAD[k].at];
			frame[BAD[k].at] = (uint8_t)BAD[k].value;
			kp_kpf_build(KP_HD_PACK_KIND, KP_HD_PACK_SCHEMA, 0, "test", NULL, sections, 4, &file, &size);
			kp_t_defer(T, free, file);
			kp_t_eq(T, kp_hd_pack_open(&p, file, size, size, "test", &e), -1, "%s refused", BAD[k].what);
			kp_t_ok(T, strstr(e.msg, "outside its page") != NULL, "%s", e.msg);
			frame[BAD[k].at] = keep;
		}
		frame[0] = 9; /* no HD frame (width 0): its page is not read */
		frame[6] = 0;
		kp_kpf_build(KP_HD_PACK_KIND, KP_HD_PACK_SCHEMA, 0, "test", NULL, sections, 4, &file, &size);
		kp_t_defer(T, free, file);
		kp_t_eq(T, kp_hd_pack_open(&p, file, size, size, "test", &e), 0, "%s", e.msg);
	}
}

/* A pack's first lit pages bring their lighting layers (NRML, AODP: BC3; SPEC, EMIS: BC1, a lit page each): a lit
 * page's offsets find them, an unlit page has none; a layer missing or of another size or more lit pages than pages are
 * refused. ETC2 pages have them at the same sizes. */
static void hd_pack_finds_its_lit_pages_layers(KpT *T)
{
	/* 4 x 4 pages, two pages, the first lit */
	uint8_t head[KP_HD_PACK_HEAD_BYTES] = { 2, 4, 0, 2, 0, 1, 0, 0, 0, KP_HD_FORMAT_BC, 1, 0 };
	uint8_t frame[14] = { 0, 0, 0, 0, 0, 0, 4, 0, 4, 0, 0, 0, 0, 0 };
	uint8_t diffuse[2 * 16], team[2 * 8], normal[16], ao[16], specular[8], emissive[8];
	KpKpfOut sections[8] = { { "HEAD", head, sizeof head, 0 }, { "FRMS", frame, sizeof frame, 0 },
		{ "DIFF", diffuse, sizeof diffuse, 0 }, { "TEAM", team, sizeof team, 0 }, { "NRML", normal, sizeof normal, 0 },
		{ "AODP", ao, sizeof ao, 0 }, { "SPEC", specular, sizeof specular, 0 }, { "EMIS", emissive, sizeof emissive, 0 } };
	static const uint8_t MARK[KP_HD_LIGHTS] = { 0xa1, 0xa2, 0xa3, 0xa4 };
	uint8_t *file;
	size_t size;
	uint32_t d, m, light[KP_HD_LIGHTS];
	KpHdPack p;
	KpError e;
	int k;
	memset(diffuse, 0, sizeof diffuse);
	memset(team, 0, sizeof team);
	memset(normal, MARK[KP_HD_LIGHT_NORMAL], sizeof normal);
	memset(ao, MARK[KP_HD_LIGHT_AO], sizeof ao);
	memset(specular, MARK[KP_HD_LIGHT_SPECULAR], sizeof specular);
	memset(emissive, MARK[KP_HD_LIGHT_EMISSIVE], sizeof emissive);
	kp_t_eq(T, kp_kpf_build(KP_HD_PACK_KIND, KP_HD_PACK_SCHEMA, 0, "test", NULL, sections, 8, &file, &size), 0, "built");
	kp_t_defer(T, free, file);
	kp_t_eq(T, kp_hd_pack_open(&p, file, size, size, "test", &e), 0, "%s", e.msg);
	kp_t_eq(T, p.lit_pages, 1, "one lit page");
	kp_hd_page_offsets(&p, 0, &d, &m, light);
	for (k = 0; k < KP_HD_LIGHTS; k++) {
		kp_t_ok(T, light[k] > 0 && file[light[k]] == MARK[k] && file[light[k] + kp_hd_layer_bytes(KP_HD_FORMAT_BC, KP_HD_LAYER_LIGHT + k, 4) - 1] == MARK[k],
			"lighting layer %d at its offset", k);
	}
	kp_hd_page_offsets(&p, 1, &d, &m, light);
	kp_t_ok(T, light[0] == 0 && light[1] == 0 && light[2] == 0 && light[3] == 0, "the unlit page has none");
	kp_kpf_build(KP_HD_PACK_KIND, KP_HD_PACK_SCHEMA, 0, "test", NULL, sections, 7, &file, &size);
	kp_t_defer(T, free, file);
	kp_t_eq(T, kp_hd_pack_open(&p, file, size, size, "test", &e), -1, "a layer missing refused");
	sections[4].size = 8;
	kp_kpf_build(KP_HD_PACK_KIND, KP_HD_PACK_SCHEMA, 0, "test", NULL, sections, 8, &file, &size);
	kp_t_defer(T, free, file);
	kp_t_eq(T, kp_hd_pack_open(&p, file, size, size, "test", &e), -1, "a layer of another size refused");
	sections[4].size = sizeof normal;
	head[10] = 3;
	kp_kpf_build(KP_HD_PACK_KIND, KP_HD_PACK_SCHEMA, 0, "test", NULL, sections, 8, &file, &size);
	kp_t_defer(T, free, file);
	kp_t_eq(T, kp_hd_pack_open(&p, file, size, size, "test", &e), -1, "more lit pages than pages refused");
	head[10] = 1;
	head[9] = KP_HD_FORMAT_ETC2;
	kp_kpf_build(KP_HD_PACK_KIND, KP_HD_PACK_SCHEMA, 0, "test", NULL, sections, 8, &file, &size);
	kp_t_defer(T, free, file);
	kp_t_eq(T, kp_hd_pack_open(&p, file, size, size, "test", &e), 0, "%s", e.msg);
	kp_t_ok(T, p.format == KP_HD_FORMAT_ETC2 && p.lit_pages == 1, "lit ETC2 pages");
	kp_hd_page_offsets(&p, 0, &d, &m, light);
	for (k = 0; k < KP_HD_LIGHTS; k++) {
		kp_t_ok(T, file[light[k]] == MARK[k] && file[light[k] + kp_hd_layer_bytes(KP_HD_FORMAT_ETC2, KP_HD_LAYER_LIGHT + k, 4) - 1] == MARK[k],
			"ETC2's lighting layer %d at its offset", k);
	}
	kp_t_eq(T, kp_hd_pack_format(file, size, size), KP_HD_FORMAT_ETC2, "the format from the first bytes");
	kp_t_eq(T, kp_hd_pack_format(file, 8, size), -1, "fewer bytes than the section table: none");
}

/* A lit page's layers of BC blocks in ETC2's formats: the normal map's x in red and y in green (BC3's red and alpha),
 * the occlusion in green (BC3's green; red 0), the colours in RGB. */
static void hd_light_layers_transcode_to_etc2(KpT *T)
{
	/* BC3: alpha endpoints 200 and 200 (all indices 0: alpha 200), colour endpoints: red 31 of 5 bits, green 8 of 6
	 * (0xf900 = 11111 001000 00000: 255, 32, 0), all indices 0 */
	uint8_t bc3[16] = { 200, 200, 0, 0, 0, 0, 0, 0, 0x00, 0xf9, 0x00, 0xf9, 0, 0, 0, 0 };
	uint8_t bc1[8] = { 0x00, 0xf9, 0x00, 0xf9, 0, 0, 0, 0 };
	uint8_t out[16], r[16], g[16], rgba[64];
	kp_t_eq(T, kp_hd_light_to_etc2(KP_HD_LIGHT_NORMAL, bc3, 4, out), 0, "normal");
	kp_eac_rg11_decode(out, 4, 4, r, g);
	kp_t_ok(T, r[0] >= 250 && abs(g[5] - 200) <= 3, "x from red, y from alpha: %d %d", r[0], g[5]);
	kp_t_eq(T, kp_hd_light_to_etc2(KP_HD_LIGHT_AO, bc3, 4, out), 0, "occlusion");
	kp_eac_rg11_decode(out, 4, 4, r, g);
	kp_t_ok(T, r[0] <= 3 && abs(g[5] - 32) <= 6, "red 0, the occlusion from green: %d %d", r[0], g[5]);
	kp_t_eq(T, kp_hd_light_to_etc2(KP_HD_LIGHT_SPECULAR, bc1, 4, out), 0, "specular");
	kp_etc2_rgb_decode(out, 4, 4, rgba);
	kp_t_ok(T, rgba[0] >= 240 && abs(rgba[1] - 32) <= 12 && rgba[2] <= 12, "the colour: %d %d %d", rgba[0], rgba[1], rgba[2]);
}

/* Pages load in the order wanted, take free slots, then the least recently wanted page's, never one wanted this
 * tick; a page refused a slot waits KP_HD_RETRY ticks; one wanted only by an earlier tick is not loaded; a failed page
 * is not asked for again; a reset empties the slots. */
static void hd_cache_slots_follow_the_wanted_pages(KpT *T)
{
	KpHdCache c;
	int32_t page, evicted, k;
	kp_t_eq(T, kp_hd_cache_init(&c, 5, 2), 0, "made");
	kp_hd_cache_tick(&c);
	kp_hd_cache_want(&c, 1);
	kp_hd_cache_want(&c, 0);
	kp_hd_cache_want(&c, 1);
	kp_t_ok(T, kp_hd_cache_take(&c, &page) && page == 1, "the first wanted first");
	kp_t_ok(T, kp_hd_cache_take(&c, &page) && page == 0, "each page once");
	kp_t_ok(T, !kp_hd_cache_take(&c, &page), "nothing more");
	kp_t_eq(T, kp_hd_cache_pending(&c), 2, "two loading");
	kp_hd_cache_take_dirty(&c);
	kp_t_eq(T, kp_hd_cache_place(&c, 1, &evicted), 0, "a free slot");
	kp_t_ok(T, kp_hd_cache_place(&c, 0, &evicted) == 1 && evicted == -1, "the other free slot, nothing evicted");
	kp_t_ok(T, kp_hd_cache_take_dirty(&c) && c.slot_of[0] == 1 && c.slot_of[1] == 0, "resident");
	kp_hd_cache_tick(&c);
	kp_hd_cache_want(&c, 0);
	kp_hd_cache_want(&c, 2);
	kp_t_ok(T, kp_hd_cache_take(&c, &page) && page == 2, "a new page");
	kp_t_ok(T, kp_hd_cache_place(&c, 2, &evicted) == 0 && evicted == 1, "page 1, not wanted this tick, evicted");
	kp_t_eq(T, c.state[1], KP_HD_ABSENT, "the evicted page absent");
	kp_hd_cache_tick(&c);
	kp_hd_cache_want(&c, 0);
	kp_hd_cache_want(&c, 2);
	kp_hd_cache_want(&c, 3);
	kp_t_ok(T, kp_hd_cache_take(&c, &page) && page == 3, "a third page");
	kp_t_eq(T, kp_hd_cache_place(&c, 3, &evicted), -1, "every slot wanted this tick: refused");
	for (k = 1; k < KP_HD_RETRY; k++) {
		kp_hd_cache_tick(&c);
		kp_hd_cache_want(&c, 3);
	}
	kp_t_ok(T, !kp_hd_cache_take(&c, &page), "a refused page waits");
	kp_hd_cache_tick(&c);
	kp_hd_cache_want(&c, 3);
	kp_t_ok(T, kp_hd_cache_take(&c, &page) && page == 3, "then is asked for again");
	kp_t_ok(T, kp_hd_cache_place(&c, 3, &evicted) >= 0 && evicted >= 0, "and takes an old page's slot");
	kp_hd_cache_tick(&c);
	kp_hd_cache_want(&c, 4);
	kp_hd_cache_tick(&c);
	kp_t_ok(T, !kp_hd_cache_take(&c, &page) && c.state[4] == KP_HD_ABSENT, "wanted only last tick: not loaded");
	kp_hd_cache_want(&c, 4);
	kp_t_ok(T, kp_hd_cache_take(&c, &page) && page == 4, "wanted again");
	kp_hd_cache_failed(&c, 4);
	kp_hd_cache_tick(&c);
	kp_hd_cache_want(&c, 4);
	kp_t_ok(T, !kp_hd_cache_take(&c, &page), "a failed page is not asked for again");
	kp_hd_cache_reset(&c);
	kp_t_ok(T, c.slot_of[3] == -1 && c.page_of[0] == -1 && c.state[3] == KP_HD_ABSENT, "a reset empties the slots");
	kp_t_eq(T, c.state[4], KP_HD_FAILED, "and keeps the failed pages");
	/* a page that was loading at the reset, wanted and queued again: the old load is not placed */
	kp_hd_cache_tick(&c);
	kp_hd_cache_want(&c, 0);
	kp_t_ok(T, kp_hd_cache_take(&c, &page) && page == 0, "loading");
	kp_hd_cache_reset(&c);
	kp_hd_cache_tick(&c);
	kp_hd_cache_want(&c, 0);
	kp_t_eq(T, kp_hd_cache_place(&c, 0, &evicted), -1, "a page queued, not loading: refused");
	kp_t_ok(T, c.state[0] == KP_HD_WANTED && c.slot_of[0] == -1, "it stays queued");
	kp_t_ok(T, kp_hd_cache_take(&c, &page) && page == 0 && kp_hd_cache_place(&c, 0, &evicted) == 0, "then loads");
	kp_t_eq(T, kp_hd_cache_place(&c, 0, &evicted), -1, "a resident page takes no second slot");
	kp_t_eq(T, c.page_of[1], -1, NULL);
	kp_hd_cache_free(&c);
}

/* With lit slots a lit page takes only them and another page only the others, each kind evicting its own. */
static void hd_cache_keeps_the_lit_pages_slots(KpT *T)
{
	static const uint8_t LIT[4] = { 1, 0, 1, 0 };
	KpHdCache c;
	int32_t page, evicted, k;
	kp_t_eq(T, kp_hd_cache_init(&c, 4, 3), 0, "made");
	kp_t_eq(T, kp_hd_cache_lit(&c, LIT, 0), -1, "no lit slots refused");
	kp_t_eq(T, kp_hd_cache_lit(&c, LIT, 4), -1, "more lit slots than slots refused");
	kp_t_eq(T, kp_hd_cache_lit(&c, LIT, 1), 0, "one lit slot");
	kp_hd_cache_tick(&c);
	for (k = 0; k < 4; k++) kp_hd_cache_want(&c, k);
	for (k = 0; k < 4; k++) kp_t_ok(T, kp_hd_cache_take(&c, &page) && page == k, "page %d taken", k);
	kp_t_eq(T, kp_hd_cache_place(&c, 1, &evicted), 1, "an unlit page past the lit slot");
	kp_t_eq(T, kp_hd_cache_place(&c, 0, &evicted), 0, "the lit page in the lit slot");
	kp_t_eq(T, kp_hd_cache_place(&c, 2, &evicted), -1, "the other lit page: the lit slot wanted this tick");
	kp_t_eq(T, kp_hd_cache_place(&c, 3, &evicted), 2, "the other unlit page in the last slot");
	for (k = 0; k < KP_HD_RETRY; k++) kp_hd_cache_tick(&c);
	kp_hd_cache_want(&c, 2);
	kp_hd_cache_want(&c, 1);
	kp_t_ok(T, kp_hd_cache_take(&c, &page) && page == 2, "the lit page again");
	kp_t_ok(T, kp_hd_cache_place(&c, 2, &evicted) == 0 && evicted == 0, "it evicts the lit page");
	kp_t_eq(T, c.slot_of[3], 2, "the unlit pages stay");
	kp_hd_cache_free(&c);
}

/* The budget's slots: every page one when they fit, else the lit and other pages' in proportion to their counts (a
 * lit slot costing its lighting layers too), at least one each, within the most slots and half of them lit. */
static void hd_slots_split_the_budget(KpT *T)
{
	static const struct {
		size_t budget, page, lit_page;
		int32_t pages, lit, max, slots, lit_slots;
		const char *what;
	} CASES[] = {
		{ 1000, 10, 30, 5, 2, 256, 5, 2, "every page fits" },
		{ 45, 10, 30, 5, 2, 256, 2, 1, "half the cost: half of each" },
		{ 25, 10, 30, 5, 0, 256, 2, 0, "no lit pages: the budget over a page's bytes" },
		{ 1, 10, 30, 5, 2, 256, 2, 1, "at least one each" },
		{ SIZE_MAX, 10, 30, 600, 300, 256, 256, 128, "the most slots, half of them lit" },
		{ SIZE_MAX, 10, 30, 3, 3, 256, 3, 3, "every page lit" },
		{ 0, 10, 30, 0, 0, 256, 1, 0, "no pages: one slot" },
	};
	size_t k;
	for (k = 0; k < sizeof CASES / sizeof CASES[0]; k++) {
		int32_t lit = -1, slots = kp_hd_slots_split(CASES[k].budget, CASES[k].page, CASES[k].lit_page, CASES[k].pages,
			CASES[k].lit, CASES[k].max, &lit);
		kp_t_ok(T, slots == CASES[k].slots && lit == CASES[k].lit_slots, "%s: %d slots, %d lit", CASES[k].what,
			(int)slots, (int)lit);
	}
}

/* The pages wanted are those of the frames the image data shows (frames without HD art want none). */
static void hd_cache_wants_the_shown_frames(KpT *T)
{
	static float data[KP_IMAGE_SLOTS * 8];
	static float texels[KP_FRAME_TABLE_WIDTH * 4];
	KpImageData d;
	KpFrameTable hd;
	KpHdCache c;
	int32_t page;
	memset(&d, 0, sizeof d);
	memset(&hd, 0, sizeof hd);
	d.data = data;
	d.next_slot[0] = 10; /* the overwriting family's slots used so far */
	hd.texels = texels;
	hd.height = 1;
	texels[1 * 8 + 2] = 16; /* frame 1: HD art on page 3 */
	texels[1 * 8 + 4] = 3;
	texels[2 * 8 + 4] = 2;  /* frame 2: none (zero width) */
	data[7 * 8 + 4] = (float)((1 * KP_DRAW_KINDS + 0) * 2 + 1);
	data[9 * 8 + 4] = (float)((2 * KP_DRAW_KINDS + 0) * 2);
	kp_t_eq(T, kp_hd_cache_init(&c, 4, 2), 0, "made");
	kp_hd_cache_tick(&c);
	kp_hd_cache_want_images(&c, &d, &hd);
	kp_t_ok(T, kp_hd_cache_take(&c, &page) && page == 3, "frame 1's page");
	kp_t_ok(T, !kp_hd_cache_take(&c, &page), "and no other");
	kp_hd_cache_free(&c);
}

static const KpTest TESTS[] = {
	{ "hd_blocks_decode", hd_blocks_decode, 0 },
	{ "hd_terrain_atlas_slots", hd_terrain_atlas_slots, 0 },
	{ "hd_terrain_atlas_layers", hd_terrain_atlas_layers, 0 },
	{ "terrain_creep_edges", terrain_creep_edges, 0 },
	{ "hd_water_masks_and_normals", hd_water_masks_and_normals, 0 },
	{ "hd_video_index", hd_video_index, 0 },
	{ "hd_ui_index_and_atlas", hd_ui_index_and_atlas, 0 },
	{ "hd_ui_frame_place", hd_ui_frame_place, 0 },
	{ "palette_tone_follows_the_fades", palette_tone_follows_the_fades, 0 },
	{ "hd_pack_opens_from_its_head", hd_pack_opens_from_its_head, 0 },
	{ "hd_pack_finds_its_lit_pages_layers", hd_pack_finds_its_lit_pages_layers, 0 },
	{ "etc_blocks_decode_by_the_layout", etc_blocks_decode_by_the_layout, 0 },
	{ "etc_round_trip", etc_round_trip, 0 },
	{ "hd_cache_slots_follow_the_wanted_pages", hd_cache_slots_follow_the_wanted_pages, 0 },
	{ "hd_cache_keeps_the_lit_pages_slots", hd_cache_keeps_the_lit_pages_slots, 0 },
	{ "hd_slots_split_the_budget", hd_slots_split_the_budget, 0 },
	{ "hd_cache_wants_the_shown_frames", hd_cache_wants_the_shown_frames, 0 },
	{ "draw_native_order_tail_to_head_and_ranks", draw_native_order_tail_to_head_and_ranks, 0 },
	{ "draw_bounds_follow_the_current_frame", draw_bounds_follow_the_current_frame, 0 },
	{ "draw_overlapping_destination_images_get_layers", draw_overlapping_destination_images_get_layers, 0 },
	{ "draw_distortion_images_close_windows", draw_distortion_images_close_windows, 0 },
	{ "draw_distortions_share_a_copy_when_nothing_between_meets_them",
		draw_distortions_share_a_copy_when_nothing_between_meets_them, 0 },
	{ "draw_distortions_out_of_view_take_no_copy", draw_distortions_out_of_view_take_no_copy, 0 },
	{ "draw_replan_keeps_the_tick_interpolation", draw_replan_keeps_the_tick_interpolation, 0 },
	{ "draw_destination_layers_only_near_the_view", draw_destination_layers_only_near_the_view, 0 },
	{ "draw_trans50_colour_follows_the_drawn_sprites", draw_trans50_colour_follows_the_drawn_sprites, 0 },
	{ "draw_rows_of_the_effect_methods", draw_rows_of_the_effect_methods, 0 },
	{ "draw_hidden_unsupported_empty_and_invisible_are_skipped", draw_hidden_unsupported_empty_and_invisible_are_skipped,
		0 },
	{ "draw_interpolation_only_from_the_same_image", draw_interpolation_only_from_the_same_image, 0 },
	{ "draw_distortions_near_the_view_are_counted", draw_distortions_near_the_view_are_counted, 0 },
	{ "draw_plans_have_no_command_budget", draw_plans_have_no_command_budget, 0 },
	{ "lookups_brightness_follows_0x445f30", lookups_brightness_follows_0x445f30, 0 },
	{ "lookups_rows_and_banks", lookups_rows_and_banks, 0 },
	{ "palette_gamma_tables_match_the_original", palette_gamma_tables_match_the_original, 0 },
	{ "palette_brightness_option_and_output", palette_brightness_option_and_output, 0 },
	{ "palette_badlands_cycles_rotate_every_eighth_driver_call", palette_badlands_cycles_rotate_every_eighth_driver_call,
		0 },
	{ "palette_desert_cycles_and_empty_tilesets", palette_desert_cycles_and_empty_tilesets, 0 },
	{ "palette_advance_uses_the_tick_period_and_pauses", palette_advance_uses_the_tick_period_and_pauses, 0 },
	{ "palette_advance_after_a_stall_runs_a_bounded_number_of_steps",
		palette_advance_after_a_stall_runs_a_bounded_number_of_steps, 0 },
	{ "palette_grayscale_truncates_each_channel", palette_grayscale_truncates_each_channel, 0 },
	{ "hd_light_layers_transcode_to_etc2", hd_light_layers_transcode_to_etc2, 0 },
	{ "palette_fade_shows_the_target_after_its_wait", palette_fade_shows_the_target_after_its_wait, 0 },
	{ "health_bars_equal_original", health_bars_equal_original, 0 },
	{ "images_released_slots_draw_nothing", images_released_slots_draw_nothing, 0 },
	{ "images_full_family_leaves_images_out", images_full_family_leaves_images_out, 0 },
	{ "images_upload_on_the_write_frame", images_upload_on_the_write_frame, 0 },
	{ "images_plan_counts_the_slots_in_use", images_plan_counts_the_slots_in_use, 0 },
	{ "images_abort_draws_the_slots_written", images_abort_draws_the_slots_written, 0 },
	{ "hd_frame_table_refuses_other_packs", hd_frame_table_refuses_other_packs, 0 },
	{ "images_without_a_row_are_left_out", images_without_a_row_are_left_out, 0 },
	{ "images_upload_the_changed_rows", images_upload_the_changed_rows, 0 },
	{ "images_join_the_runs_that_touch", images_join_the_runs_that_touch, 0 },
	{ "packs_refuse_pages_of_another_size", packs_refuse_pages_of_another_size, 0 },
	{ "images_slots_stable_released_and_reused", images_slots_stable_released_and_reused, 0 },
	{ "images_order_follows_the_ranks", images_order_follows_the_ranks, 0 },
	{ "layouts_fit_every_screen", layouts_fit_every_screen, 0 },
	{ "layouts_narrow_fits_portrait_screens", layouts_narrow_fits_portrait_screens, 0 },
	{ "layouts_auto_fits_a_fixed_screen", layouts_auto_fits_a_fixed_screen, 0 },
	{ "layouts_fixed_screen_fills_the_drawable", layouts_fixed_screen_fills_the_drawable, 0 },
	{ "layouts_wide_pins_the_groups", layouts_wide_pins_the_groups, 0 },
};

KP_SUITE(kp_suite_present, "test_present", TESTS);
