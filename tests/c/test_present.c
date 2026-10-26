/* kp_present (plan phase S, stage S2): the draw list on hand-built worlds (no retail data), the lookup rows on
 * synthetic palettes, the palette's cycles, gamma and fades, the health bars against the original's DrawHealthBar, the
 * image data's slots and uploads and the layouts on the aspect ratios and resolutions of plan §7 phase 9. */
#include "kp_test.h"

#include <stdio.h>
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
	kp_t_defer(T, (void (*)(void *))kp_draw_list_free, l);
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
	kp_t_defer(T, (void (*)(void *))kp_dat_free, &d);
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
	kp_t_defer(T, (void (*)(void *))kp_image_data_free, d);
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
	int32_t row, rows;
	kp_image_data_begin(d);
	kp_image_data_put(d, &it, 0, 0);
	kp_image_data_finish(d, windows, 1, 0);
	kp_t_eq(T, d->plan.count, 1, NULL);
	kp_t_eq(T, kp_image_data_take(d, &row, &rows), 1, "the write frame uploads");
	kp_t_eq(T, row, 0, "the slot's row");
	kp_t_eq(T, rows, 1, "one row");
	kp_t_eq(T, kp_image_data_take(d, &row, &rows), 0, "once");
	kp_t_eq(T, rows, 0, NULL);
	kp_image_data_begin(d);
	kp_image_data_put(d, &it, 0, 0); /* unchanged: nothing to upload */
	kp_image_data_finish(d, windows, 1, 0);
	kp_t_eq(T, kp_image_data_take(d, &row, &rows), 0, "nothing to upload");
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

/* Only the texel rows a tick changed go up: from the lowest to the highest row written. */
static void images_upload_the_changed_rows(KpT *T)
{
	KpImageData *d = image_data(T, 7);
	KpDrawItem a = item(KP_FAMILY_OVERWRITE, 1, 1, 1), b = item(KP_FAMILY_DISTORT, 2, 1, 2);
	int32_t row, rows, slots_per_row = KP_IMAGE_DATA_WIDTH / KP_TEXELS_PER_SLOT;
	kp_image_data_begin(d);
	kp_image_data_put(d, &a, 0, 0);
	kp_image_data_put(d, &b, 0, 0);
	kp_image_data_finish(d, NULL, 0, 0);
	kp_t_eq(T, kp_image_data_take(d, &row, &rows), 1, NULL);
	kp_t_eq(T, row, 0, "the overwriting family's first slot");
	kp_t_eq(T, rows, KP_FAMILY_BASE[KP_FAMILY_DISTORT] / slots_per_row + 1, "through the distortion family's first slot");
	b.x256 += 256; /* the distortion image alone moves */
	kp_image_data_begin(d);
	kp_image_data_put(d, &a, 0, 0);
	kp_image_data_put(d, &b, 0, 0);
	kp_image_data_finish(d, NULL, 0, 0);
	kp_t_eq(T, kp_image_data_take(d, &row, &rows), 1, NULL);
	kp_t_eq(T, row, KP_FAMILY_BASE[KP_FAMILY_DISTORT] / slots_per_row, "its row");
	kp_t_eq(T, rows, 1, "alone");
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
	{ 800, 600 } };
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
			kp_layout_screen(WINDOWS[wi][0], WINDOWS[wi][1], MODES[mi], 0, 0, &m, &scale, &w, &h);
			snprintf(where, sizeof where, "%dx%d mode %d", (int)WINDOWS[wi][0], (int)WINDOWS[wi][1], m);
			kp_t_ok(T, w >= KP_LAYOUT_MIN_W && h >= KP_LAYOUT_MIN_H, "%s: smaller than 640 x 480", where);
			kp_t_ok(T, scale > 0 && w * scale <= WINDOWS[wi][0] + scale && h * scale <= WINDOWS[wi][1] + scale,
				"%s: scale", where);
			for (li = 0; li < 3; li++) {
				KpArrangement a;
				int32_t placed[LAYOUT_PANEL_COUNT][4], spans[4][2], n = 0, k, j;
				int32_t filler_top;
				kp_layout_arrange(&a, w, h, LAYOUTS[li]);
				place_all(T, &a, w, h, where, 1, placed);
				/* the text line, a backdrop dialog under the console art, never lies under a filler's opaque column */
				filler_top = a.dy + 290;
				for (k = 0; k < a.filler_count; k++) {
					const KpLayoutFiller *f = &a.fillers[k];
					if (placed[TEXTBOX][0] < f->x + f->w && placed[TEXTBOX][2] >= f->x) {
						kp_t_ok(T, placed[TEXTBOX][3] < filler_top, "%s %s: the text line under a filler", where,
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
			kp_layout_screen(WINDOWS[wi][0], WINDOWS[wi][1], MODES[mi], min_w, min_h, &m, &scale, &w, &h);
			snprintf(where, sizeof where, "%dx%d mode %d", (int)WINDOWS[wi][0], (int)WINDOWS[wi][1], m);
			kp_t_ok(T, w >= min_w && h >= min_h, "%s: smaller than its minimum", where);
			kp_layout_minimum(WINDOWS[wi][0], WINDOWS[wi][1], 1, &min_w, &min_h);
			kp_layout_screen(WINDOWS[wi][0], WINDOWS[wi][1], MODES[mi], min_w, min_h, &m, &scale, &fw, &fh);
			kp_t_ok(T, fw >= KP_LAYOUT_MIN_W && fh >= KP_LAYOUT_MIN_H, "%s: a fixed screen's logical screen", where);
			kp_layout_arrange(&a, w, h, -1);
			if (a.layout == KP_LAYOUT_NARROW) {
				int32_t placed[LAYOUT_PANEL_COUNT][4];
				narrowed++;
				place_all(T, &a, w, h, where, 0, placed);
				kp_t_eq(T, a.groups[1].left, 0, NULL);
				kp_t_eq(T, a.groups[0].left, 0, NULL);
				kp_t_eq(T, a.filler_count, 0, NULL);
				kp_t_ok(T, a.groups[1].right <= w, "%s: the console leaves the screen", where);
			}
		}
	}
	kp_t_ok(T, narrowed > 0, "no portrait window narrowed");
}

static void layouts_wide_pins_the_groups(KpT *T)
{
	KpArrangement a, c;
	int32_t x, y;
	kp_layout_arrange(&a, 1920, 480, KP_LAYOUT_WIDE);
	kp_t_eq(T, a.groups[0].left, 0, NULL);
	kp_t_eq(T, a.groups[1].right, 1920, NULL);
	kp_t_eq(T, a.filler_count, 1, NULL);
	kp_t_eq(T, a.fillers[0].x, 138, NULL);
	kp_layout_arrange(&c, 640, 480, KP_LAYOUT_CLASSIC);
	kp_t_eq(T, c.filler_count, 0, NULL);
	kp_layout_place(&c, "rez\\stat_f10.bin", LAYOUT_PANELS[3].r, &x, &y);
	kp_t_eq(T, x, 408, NULL);
	kp_t_eq(T, y, 388, "the classic layout of a 640 x 480 screen is the original's");
}

static const KpTest TESTS[] = {
	{ "draw_native_order_tail_to_head_and_ranks", draw_native_order_tail_to_head_and_ranks, 0 },
	{ "draw_bounds_follow_the_current_frame", draw_bounds_follow_the_current_frame, 0 },
	{ "draw_overlapping_destination_images_get_layers", draw_overlapping_destination_images_get_layers, 0 },
	{ "draw_distortion_images_close_windows", draw_distortion_images_close_windows, 0 },
	{ "draw_distortions_share_a_copy_when_nothing_between_meets_them",
		draw_distortions_share_a_copy_when_nothing_between_meets_them, 0 },
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
	{ "palette_fade_shows_the_target_after_its_wait", palette_fade_shows_the_target_after_its_wait, 0 },
	{ "health_bars_equal_original", health_bars_equal_original, 0 },
	{ "images_released_slots_draw_nothing", images_released_slots_draw_nothing, 0 },
	{ "images_full_family_leaves_images_out", images_full_family_leaves_images_out, 0 },
	{ "images_upload_on_the_write_frame", images_upload_on_the_write_frame, 0 },
	{ "images_upload_the_changed_rows", images_upload_the_changed_rows, 0 },
	{ "packs_refuse_pages_of_another_size", packs_refuse_pages_of_another_size, 0 },
	{ "images_slots_stable_released_and_reused", images_slots_stable_released_and_reused, 0 },
	{ "layouts_fit_every_screen", layouts_fit_every_screen, 0 },
	{ "layouts_narrow_fits_portrait_screens", layouts_narrow_fits_portrait_screens, 0 },
	{ "layouts_wide_pins_the_groups", layouts_wide_pins_the_groups, 0 },
};

KP_SUITE(kp_suite_present, "test_present", TESTS);
