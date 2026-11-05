/* The images world of the native core (kp_images.h; plan phase S, stage S7), driven directly:
 *   - without retail data: the list helpers, the new-game pool order, the depth key, the cloak initialisers and
 *     callbacks, the sprite rows, and a turret move from a sprite without a main image;
 *   - on the converted data (skipped without generated/data): the selection records, health bars (0x40eb60, 0x40f6e0,
 *     0x40f540) and selection circles (0x40ec90, 0x411460);
 *   - the IScript VM against the original's code: every animation of every image of the retail iscript.bin run on a
 *     lone sprite and compared with the hashes the original-code probe (tools/research/probe_native_iscript_117.py)
 *     recorded in tests/golden/oracle/iscript/cases.json, and the probe's synthetic programs.
 * A world's hooks are a dispatcher of the test's (kp_images.h KpHookCall); a hook the test does not give fails the
 * call. */
#include "kp_test.h"

#include <string.h>

#define DATA KP_ORACLE_DATA

/* --- worlds --- */

typedef struct Scene Scene;
typedef int (*SceneHook)(Scene *sc, KpHook hook, const int64_t *args, int argc, int64_t *out);

struct Scene {
	KpDat dat;
	KpGrpMeta grp;
	KpIScript script;
	KpRngGame rng;
	KpImages *w;
	int has_dat, has_grp, has_script;
	SceneHook hook;
	/* test_images: the cloak states the unit was given */
	int64_t cloak_states[8];
	int cloak_count;
	/* test_selection: the units of the sprites (selection_unit), their types and statuses */
	int64_t selected[KP_SPRITES], type[16], status[16];
	int unit_count;
	/* the IScript trace: the events of a step */
	int64_t events[4096];
	int event_count;
};

/* The world's dispatcher: the scene's hooks; the optional ones (hide_order_marker, sound, play_probe) do nothing
 * without one, any other fails the call. */
static int dispatch(KpImages *w, void *ctx, KpHook hook, int op, const int64_t *args, int argc, int64_t *out)
{
	Scene *sc = (Scene *)ctx;
	if (sc->hook && sc->hook(sc, hook, args, argc, out)) return 1;
	if (hook == KP_HOOK_HIDE_ORDER_MARKER || hook == KP_HOOK_SOUND || hook == KP_HOOK_PLAY_PROBE) return 0;
	if (op >= 0) kp_images_fail(w, "iscript: opcode 0x%02x needs hook %s", op, kp_hook_name(hook));
	kp_images_fail(w, "images: needs hook %s", kp_hook_name(hook));
}

static void free_scene(void *p)
{
	Scene *sc = (Scene *)p;
	if (sc->w) kp_images_free(sc->w);
	if (sc->has_dat) kp_dat_free(&sc->dat);
	if (sc->has_grp) kp_grpmeta_free(&sc->grp);
	if (sc->has_script) kp_iscript_free(&sc->script);
	free(sc);
}

static Scene *new_scene(KpT *T)
{
	Scene *sc = (Scene *)calloc(1, sizeof *sc);
	kp_t_ok(T, sc != NULL, "out of memory");
	kp_t_defer(T, free_scene, sc);
	return sc;
}

/* The scene's world over its data (dat NULL when the scene has none). */
static KpImages *open_world(KpT *T, Scene *sc, int32_t map_w, int32_t map_h)
{
	KpError err;
	sc->w = kp_images_new(sc->has_dat ? &sc->dat : NULL, &sc->grp, &sc->script, &sc->rng, &err);
	kp_t_ok(T, sc->w != NULL, "%s", err.msg);
	sc->w->hook = dispatch;
	sc->w->ctx = sc;
	kp_images_set_map(sc->w, map_w, map_h);
	return sc->w;
}

/* A world without retail data (64 x 48 tiles). */
static KpImages *bare_world(KpT *T, Scene **out)
{
	Scene *sc = new_scene(T);
	*out = sc;
	return open_world(T, sc, 64, 48);
}

/* The generated data's dat.kpd, grpmeta.kpd and iscript.kpd into the scene. */
static void load_data(KpT *T, Scene *sc)
{
	KpError err;
	size_t size;
	uint8_t *data = kp_t_read(T, DATA "dat.kpd", &size);
	kp_t_ok(T, kp_dat_load(&sc->dat, data, size, "dat.kpd", NULL, &err) == 0, "%s", err.msg);
	sc->has_dat = 1;
	data = kp_t_read(T, DATA "grpmeta.kpd", &size);
	kp_t_ok(T, kp_grpmeta_load(&sc->grp, data, size, "grpmeta.kpd", NULL, &err) == 0, "%s", err.msg);
	sc->has_grp = 1;
	data = kp_t_read(T, DATA "iscript.kpd", &size);
	kp_t_ok(T, kp_iscript_load(&sc->script, data, size, "iscript.kpd", NULL, &err) == 0, "%s", err.msg);
	sc->has_script = 1;
}

static const KpDatColumn *column(KpT *T, const KpDat *d, const char *table, const char *name)
{
	const KpDatColumn *c = kp_dat_column(d, table, name);
	kp_t_ok(T, c != NULL, "no column %s.%s", table, name);
	return c;
}

static int64_t dat_value(KpT *T, const KpDat *d, const char *table, const char *name, int64_t i)
{
	const KpDatColumn *c = column(T, d, table, name);
	kp_t_ok(T, i >= 0 && kp_dat_has(c, (uint32_t)i), "%s.%s has no entry %lld", table, name, (long long)i);
	return kp_dat_value(c, (uint32_t)i);
}

/* Runs fn(T, w, ud) under the world's failure handler: a C failure fails the test with the world's message. */
typedef void (*WorldFn)(KpT *T, KpImages *w, void *ud);

static void guarded(KpT *T, KpImages *w, WorldFn fn, void *ud)
{
	KpTry t;
	kp_images_begin(w, &t);
	if (setjmp(t.buf)) {
		kp_images_end(w, &t);
		kp_t_fail(T, "%s", w->error);
	}
	fn(T, w, ud);
	kp_images_end(w, &t);
}

/* --- without retail data --- */

static void walk(const int32_t *next, const KpList *l, char *out, size_t size)
{
	size_t at = 0;
	int32_t i = l->head;
	out[0] = '\0';
	while (i != KP_NONE && at < size) {
		at += (size_t)snprintf(out + at, size - at, "%s%d", at ? "," : "", (int)i);
		i = next[i];
	}
}

static void list_helpers_follow_the_native_ones(KpT *T)
{
	int32_t prev[8], next[8];
	KpList l, other;
	char got[64];
	int k;
	for (k = 0; k < 8; k++) prev[k] = next[k] = KP_NONE;
	l.head = l.tail = KP_NONE;
	kp_list_insert_after_head(prev, next, &l, 0);
	kp_t_ok(T, l.head == 0 && l.tail == 0, "a first node is head and tail");
	kp_list_insert_after_head(prev, next, &l, 1); /* after the head */
	kp_list_insert_after_head(prev, next, &l, 2);
	walk(next, &l, got, sizeof got);
	kp_t_eq_str(T, got, "0,2,1", "after the head");
	kp_t_eq(T, l.tail, 1, "the tail");
	kp_list_insert_before(prev, next, &l, 0, 3); /* a new head */
	walk(next, &l, got, sizeof got);
	kp_t_eq_str(T, got, "3,0,2,1", "before the head");
	kp_t_eq(T, l.head, 3, "the new head");
	kp_list_insert_after(prev, next, &l, 1, 4); /* a new tail */
	kp_t_eq(T, l.tail, 4, "the new tail");
	kp_list_unlink(prev, next, &l, 3);
	walk(next, &l, got, sizeof got);
	kp_t_eq_str(T, got, "0,2,1,4", "the head unlinked");
	kp_t_ok(T, prev[3] == KP_NONE && next[3] == KP_NONE, "an unlinked node's links");
	kp_list_unlink(prev, next, &l, 4);
	kp_t_eq(T, l.tail, 1, "the tail unlinked");
	/* unlinking a node that is not in the list only rewires its neighbours (the native code compares the head and tail
	 * with the node) */
	other.head = other.tail = KP_NONE;
	kp_list_unlink(prev, next, &other, 2);
	walk(next, &l, got, sizeof got);
	kp_t_eq_str(T, got, "0,1", "the neighbours rewired");
}

static void pools_body(KpT *T, KpImages *w, void *ud)
{
	(void)ud;
	kp_images_init_pools(w);
	kp_t_eq(T, kp_image_alloc(w), 0, "the first image");
	kp_t_eq(T, kp_image_alloc(w), 4999, "then the last");
	kp_t_eq(T, kp_image_alloc(w), 4998, "then descending");
	kp_t_eq(T, w->free_sprites.head, 0, "the free sprites' head");
	kp_t_eq(T, w->spr.next[0], 2499, "then the last sprite");
	kp_t_eq(T, w->free_sprites.tail, 1, "the free sprites' tail");
	kp_t_eq(T, w->spr.index[0], 0, "sprite 0's index");
	kp_t_eq(T, w->spr.index[2499], 2499, "sprite 2499's index");
	kp_t_eq(T, w->free_health.head, KP_IMG_HEALTH_FIRST, "the free health bars' head");
	kp_t_eq(T, w->img.next[KP_IMG_HEALTH_FIRST], KP_IMG_HEALTH_FIRST + KP_IMG_HEALTH - 1, "then the last bar");
	kp_t_eq(T, w->free_boxes.tail, KP_IMG_BOXES_FIRST + 1, "the free boxes' tail");
	/* a second initialisation zeroes and rebuilds everything */
	w->img.id[17] = 99;
	kp_images_init_pools(w);
	kp_t_eq(T, w->img.id[17], 0, "a record zeroed");
	kp_t_eq(T, kp_image_alloc(w), 0, "the first image again");
}

static void new_game_pools_hand_out_record_0_then_descending(KpT *T)
{
	Scene *sc;
	KpImages *w = bare_world(T, &sc);
	guarded(T, w, pools_body, NULL);
}

static void depth_body(KpT *T, KpImages *w, void *ud)
{
	int32_t s = 5;
	(void)ud;
	kp_images_init_pools(w);
	w->spr.index[s] = 300;
	w->spr.elevation[s] = 4;
	w->spr.flags[s] = 0x10 + 0x20;
	w->spr.y[s] = 100;
	/* ((((4 << 13) | 100) << 5) | 0x10) << 9 | 300 */
	kp_t_eq(T, kp_sprite_depth_key(w, s), ((4 * 8192 + 100) * 32 + 16) * 512 + 300, "elevation 4");
	w->spr.elevation[s] = 5; /* y ignored above elevation 4 */
	kp_t_eq(T, kp_sprite_depth_key(w, s), ((5 * 8192) * 32 + 16) * 512 + 300, "elevation 5");
	w->spr.elevation[s] = 0;
	w->spr.flags[s] = 0;
	w->spr.y[s] = 65535; /* y = -1 sign-extends */
	kp_t_eq(T, kp_sprite_depth_key(w, s), (uint32_t)(0xffffffffu * 32u * 512u + 300u), "y -1");
}

static void depth_key(KpT *T)
{
	Scene *sc;
	KpImages *w = bare_world(T, &sc);
	guarded(T, w, depth_body, NULL);
}

static int cloak_hooks(Scene *sc, KpHook hook, const int64_t *args, int argc, int64_t *out)
{
	(void)out;
	if (hook != KP_HOOK_SET_CLOAK_STATE || argc < 2) return 0;
	if (sc->cloak_count < 8) sc->cloak_states[sc->cloak_count++] = args[1];
	return 1;
}

/* 0x40ed00/0x40ed30 write the low parameter WORD (value, timer 3) and keep the high WORD; the cloak callbacks then step
 * the value every fourth update. */
static void cloak_body(KpT *T, KpImages *w, void *ud)
{
	Scene *sc = (Scene *)ud;
	int32_t i;
	int k;
	kp_images_init_pools(w);
	i = kp_image_alloc(w);
	w->img.method[i] = 0;
	w->img.param[i] = 0x12340000 + 0xffff;
	kp_image_begin_cloak(w, i);
	kp_t_eq(T, w->img.method[i], 2, "cloaking");
	kp_t_eq(T, w->img.param[i], 0x12340000 + 0x300, "the parameter's low WORD");
	w->unit = 1;
	w->has_unit = 1;
	sc->hook = cloak_hooks;
	for (k = 0; k < 4 * 8 - 1; k++) kp_image_update(w, i);
	kp_t_eq(T, w->img.method[i], 2, "still cloaking");
	kp_t_eq(T, w->img.param[i] & 0xff, 7, "the value");
	kp_image_update(w, i);
	kp_t_eq(T, w->img.method[i], 3, "value 8: method 2 -> 3");
	kp_t_ok(T, sc->cloak_count >= 1 && sc->cloak_states[0] == 3, "the unit's cloak state 3");
	kp_image_begin_decloak(w, i);
	kp_t_eq(T, w->img.method[i], 4, "below 4 -> 4");
	kp_t_eq(T, w->img.param[i] & 0xffff, 0x308, "the decloak's low WORD");
	for (k = 0; k < 4 * 8; k++) kp_image_update(w, i);
	kp_t_eq(T, w->img.method[i], 4, "still decloaking");
	kp_t_eq(T, w->img.param[i] & 0xff, 0, "the value down to 0");
	for (k = 0; k < 4; k++) kp_image_update(w, i);
	kp_t_eq(T, w->img.method[i], 0, "switches when the old value was zero");
	kp_t_eq(T, w->img.param[i] & 0xff, 0xff, "leaving 0xff");
	kp_t_ok(T, sc->cloak_count >= 2 && sc->cloak_states[1] == 0, "the unit's cloak state 0");
	w->img.method[i] = 6;
	kp_image_begin_decloak(w, i);
	kp_t_eq(T, w->img.method[i], 7, "4 and above -> 7");
}

static void cloak_initialisers_and_callbacks(KpT *T)
{
	Scene *sc;
	KpImages *w = bare_world(T, &sc);
	guarded(T, w, cloak_body, sc);
}

static void sprite_rows_truncate_and_clamp(KpT *T)
{
	static const int32_t CASES[7][2] = { { 0, 0 }, { 31, 0 }, { 32, 1 }, { 65535, 0 }, { 65536 - 33, 0 }, { 47 * 32, 47 },
		{ 60 * 32, 47 } };
	Scene *sc;
	KpImages *w = bare_world(T, &sc);
	int k;
	for (k = 0; k < 7; k++) {
		w->spr.y[1] = CASES[k][0];
		kp_t_eq(T, kp_images_sprite_row(w, 1), CASES[k][1], "the row of y %d", (int)CASES[k][0]);
	}
}

static int sound_hooks(Scene *sc, KpHook hook, const int64_t *args, int argc, int64_t *out)
{
	(void)args;
	(void)argc;
	(void)out;
	if (hook != KP_HOOK_SOUND) return 0;
	sc->event_count++;
	return 1;
}

static void take_main_body(KpT *T, KpImages *w, void *ud)
{
	Scene *sc = (Scene *)ud;
	(void)T;
	kp_images_init_pools(w);
	kp_sprite_take_main(w, KP_NONE, 5); /* sprite 5 has no main image: nothing happens */
	sc->hook = sound_hooks;
	w->has_sound = 0;
	kp_image_sound(w, 0, 1); /* a world whose sound callback is off plays nothing */
}

/* A turret move from a sprite without a main image does nothing, and a disabled sound callback is not called. */
static void sprite_without_main_image_and_disabled_sound(KpT *T)
{
	Scene *sc;
	KpImages *w = bare_world(T, &sc);
	guarded(T, w, take_main_body, sc);
	kp_t_eq(T, w->free_images.head, 0, "the pools untouched");
	kp_t_eq(T, sc->event_count, 0, "no sound");
}

/* --- selection images on the converted data --- */

#define MARINE 0
#define GHOST 1
#define DRAGOON 66
#define HEALTH_IMAGE 0x230
#define CIRCLE_FIRST 0x231
#define METHOD_HEALTH 11
#define METHOD_BOX 15
#define METHOD_SELECTION 13
#define METHOD_SHADOW 10
#define BAR_MIN_WIDTH 19
#define HALLUCINATION 0x40000000

static const char *const SELECTION_FILES[] = { DATA "dat.kpd", DATA "grpmeta.kpd", DATA "iscript.kpd", NULL };

static int selection_hooks(Scene *sc, KpHook hook, const int64_t *args, int argc, int64_t *out)
{
	int64_t u = argc > 0 ? args[0] : -1;
	switch (hook) {
	case KP_HOOK_SELECTION_UNIT:
		out[0] = u >= 0 && u < KP_SPRITES ? sc->selected[u] : 0;
		return 1;
	case KP_HOOK_UNIT_TYPE:
	case KP_HOOK_UNIT_STATUS:
		if (u < 1 || u > sc->unit_count) return 0;
		out[0] = hook == KP_HOOK_UNIT_TYPE ? sc->type[u] : sc->status[u];
		return 1;
	default:
		return 0;
	}
}

/* A scene over the converted data with the selection hooks (64 x 64 tiles, the game RNG seeded 1); NULL without the
 * data. */
static Scene *selection_scene(KpT *T)
{
	Scene *volatile sc; /* volatile: read after the longjmp, GCC's -Wclobbered cannot tell it is not modified */
	KpTry t;
	if (!kp_t_ready(T, "test_images", SELECTION_FILES)) return NULL;
	sc = new_scene(T);
	load_data(T, sc);
	kp_rng_game_init(&sc->rng, 1);
	kp_rng_game_seed(&sc->rng, 1);
	kp_rng_game_set_enabled(&sc->rng, 1);
	open_world(T, sc, 64, 64);
	sc->hook = selection_hooks;
	kp_images_begin(sc->w, &t);
	if (setjmp(t.buf)) {
		kp_images_end(sc->w, &t);
		kp_t_fail(T, "%s", sc->w->error);
	}
	kp_images_init_pools(sc->w);
	kp_images_end(sc->w, &t);
	return sc;
}

/* A unit of type t (status) at (512, 512): its sprite, selected as the unit. */
static int32_t unit(KpT *T, Scene *sc, int64_t t, int64_t status)
{
	int64_t flingy = dat_value(T, &sc->dat, "units", "graphics", t);
	int32_t s = kp_sprite_create(sc->w, (int32_t)dat_value(T, &sc->dat, "flingy", "sprite", flingy), 512, 512, 0);
	kp_t_ok(T, s != KP_NONE && sc->unit_count < 15, "a sprite for unit type %lld", (long long)t);
	sc->unit_count++;
	sc->type[sc->unit_count] = t;
	sc->status[sc->unit_count] = status;
	sc->selected[s] = sc->unit_count;
	return s;
}

static void pool_records(KpT *T)
{
	Scene *sc = selection_scene(T);
	int32_t i = KP_IMG_HEALTH_FIRST + 3;
	KpImages *w;
	if (!sc) return;
	w = sc->w;
	kp_t_eq(T, w->img.id[i], HEALTH_IMAGE, "a health bar's image");
	kp_t_eq(T, w->img.method[i], METHOD_HEALTH, "its method");
	kp_t_eq(T, w->img.grp[i], KP_GRP_VIRTUAL, "its virtual GRP");
	kp_t_ok(T, w->img.header[i] > 0, "its script header");
	kp_t_eq(T, w->img.method[KP_IMG_BOXES_FIRST], METHOD_BOX, "a box's method");
}

static void health_bars_body(KpT *T, KpImages *w, void *ud)
{
	Scene *sc = (Scene *)ud;
	const KpDatColumn *bars = column(T, &sc->dat, "sprites", "health_bar");
	int32_t s = unit(T, sc, MARINE, 0), head = w->spr.images[s].head, i, sid, circle, z, bar, expected;
	int cx, cy, cw, ch;
	i = kp_health_bar_create(w, s);
	kp_t_eq(T, i, KP_IMG_HEALTH_FIRST, "the pool's head");
	kp_t_eq(T, w->spr.images[s].head, i, "before the old head: drawn last");
	kp_t_eq(T, w->img.next[i], head, "the old head follows");
	sid = w->spr.id[s];
	bar = (int32_t)dat_value(T, &sc->dat, "sprites", "health_bar", sid);
	expected = bar - (bar - 1) % 3;
	kp_t_eq(T, w->vgrp_fw[i], expected > BAR_MIN_WIDTH ? expected : BAR_MIN_WIDTH, "the bar's width");
	kp_t_eq(T, w->vgrp_fh[i], 5, "no shields, no energy");
	kp_t_eq(T, w->img.param[i], 1, "the bar's unit");
	circle = kp_images_grp_of(w, CIRCLE_FIRST + (int32_t)dat_value(T, &sc->dat, "sprites", "selection_circle_image", sid));
	kp_t_ok(T, circle > 0, "the selection circle's GRP");
	kp_grpmeta_frame(&sc->grp, (uint32_t)circle, 0, &cx, &cy, &cw, &ch);
	kp_t_eq(T, w->img.yoff[i], ch / 2 + 8 + (int32_t)dat_value(T, &sc->dat, "sprites", "selection_circle_offset", sid),
		"the bar below the circle");
	kp_t_eq(T, w->vgrp_fh[kp_health_bar_create(w, unit(T, sc, DRAGOON, 0))], 7, "shields");
	kp_t_eq(T, w->vgrp_fh[kp_health_bar_create(w, unit(T, sc, GHOST, 0))], 5 + 6, "energy");
	kp_t_eq(T, w->vgrp_fh[kp_health_bar_create(w, unit(T, sc, MARINE, HALLUCINATION))], 5 + 6, "the timer");
	/* a zero bar width: the C remainder -1, width 1, then the minimum */
	kp_dat_set(&sc->dat, bars, (uint32_t)sid, 0);
	z = kp_health_bar_create(w, unit(T, sc, MARINE, 0));
	kp_dat_set(&sc->dat, bars, (uint32_t)sid, (uint32_t)bar);
	kp_t_eq(T, w->vgrp_fw[z], BAR_MIN_WIDTH, "a zero width");
	kp_health_bar_release(w, i);
	kp_t_eq(T, w->spr.images[s].head, head, "the bar released");
	/* allocated 5000, 5011, 5010, 5009, 5008; the released record goes right after the free head */
	kp_t_eq(T, w->free_health.head, KP_IMG_HEALTH_FIRST + KP_IMG_HEALTH - 5, "the free head");
	kp_t_eq(T, w->img.next[w->free_health.head], i, "the released record");
}

static void health_bars(KpT *T)
{
	Scene *sc = selection_scene(T);
	if (!sc) return;
	guarded(T, sc->w, health_bars_body, sc);
}

static void circles_body(KpT *T, KpImages *w, void *ud)
{
	Scene *sc = (Scene *)ud;
	int32_t s = unit(T, sc, DRAGOON, 0), tail = w->spr.images[s].tail, c, i; /* a shadow tail: the circle goes before it */
	int found = 0;
	kp_t_eq(T, w->img.method[tail], METHOD_SHADOW, "the dragoon's shadow is its tail");
	c = kp_selection_circle_create(w, s, 2, CIRCLE_FIRST);
	kp_t_eq(T, c, KP_IMG_CIRCLES_FIRST, "the pool's first circle");
	kp_t_eq(T, w->img.next[c], tail, "before the shadow");
	kp_t_eq(T, w->img.id[c], CIRCLE_FIRST + dat_value(T, &sc->dat, "sprites", "selection_circle_image", w->spr.id[s]),
		"the circle's image");
	kp_t_eq(T, w->img.method[c], METHOD_SELECTION, "its method");
	kp_t_eq(T, w->img.param[c], 2, "its colour");
	for (i = w->spr.images[s].head; i != KP_NONE; i = w->img.next[i]) found |= i == c;
	kp_t_ok(T, found, "the circle is among the sprite's images");
}

static void selection_circles(KpT *T)
{
	Scene *sc = selection_scene(T);
	if (!sc) return;
	guarded(T, sc->w, circles_body, sc);
}

/* --- the IScript VM against the original ---
 *
 * One image animation run on a lone sprite without unit, flingy or bullet context, every frame stepped by
 * UpdateThingies, as the probe runs it:
 *   1. new-game pools (InitImagePools' main pool, InitSpritePool, InitThingyPools with the placement cursor);
 *   2. a lone thingy of sprite ROOT_SPRITE whose sprites.dat image is for the while the case's image, at ROOT_X,
 *      ROOT_Y, owner = image id % 8, tileset = image id % 8;
 *   3. for an animation > 0, PlaySpriteAnimation(animation, force);
 *   4. FRAMES calls of UpdateThingies.
 * After step 3 and after every step the state is folded into a 32-bit hash h = h * 31 + v (mod 2^32) over the
 * integers below (signed values as two's complement): the step's events (a sound: SOUND, id, x, y; a unit fit
 * check: FIT, type, x, y); the frame, the RNG's state, total and source checksum (sum of counter[i] * (i + 1)); the
 * free heads of the images, sprites and lone thingies; per active lone thingy (head to tail) the thingy, its sprite,
 * the sprite's fields and image count, and per image (head to tail) its record and fields with flags & ~1 (no dirty
 * bit). A failure ends the case: the frame counts as faulted. */

#define TRACE_FRAMES 80
#define ROOT_SPRITE 0
#define ROOT_X 4096
#define ROOT_Y 4096
#define TRACE_MAP 256
#define TRACE_SEED 0x2A2A2A2Au
#define EVENT_SOUND 0x534e44
#define EVENT_FIT 0x464954
#define STEP_LIMIT 100000 /* opcodes one VM run may execute before the case counts as hung */
#define GOLDEN "tests/golden/oracle/iscript/cases.json"

static const char *const TRACE_FILES[] = { DATA "dat.kpd", DATA "grpmeta.kpd", DATA "iscript.kpd", GOLDEN, NULL };

static int trace_hooks(Scene *sc, KpHook hook, const int64_t *args, int argc, int64_t *out)
{
	if (argc < 3 || (hook != KP_HOOK_SOUND && hook != KP_HOOK_UNIT_FITS)) return 0;
	if (sc->event_count + 4 > (int)(sizeof sc->events / sizeof sc->events[0])) {
		kp_images_fail(sc->w, "iscript trace: too many events in a step");
	}
	if (hook == KP_HOOK_SOUND) {
		sc->events[sc->event_count++] = EVENT_SOUND;
		sc->events[sc->event_count++] = args[0];
		sc->events[sc->event_count++] = args[1];
		sc->events[sc->event_count++] = args[2];
		return 1;
	}
	/* the probe records the WORDs the original passes (EDX and the stack word) */
	sc->events[sc->event_count++] = EVENT_FIT;
	sc->events[sc->event_count++] = args[0];
	sc->events[sc->event_count++] = args[1] & 0xffff;
	sc->events[sc->event_count++] = args[2] & 0xffff;
	out[0] = 1;
	return 1;
}

static uint32_t fold(uint32_t h, int64_t v) { return h * 31u + (uint32_t)v; }

/* The frame's integers folded into h (the events taken). */
static uint32_t frame_hash(Scene *sc, int32_t frame, uint32_t h)
{
	KpImages *w = sc->w;
	const KpRngGame *r = w->rng;
	uint32_t sources = 0;
	int32_t t;
	int k;
	for (k = 0; k < sc->event_count; k++) h = fold(h, sc->events[k]);
	sc->event_count = 0;
	h = fold(h, frame);
	h = fold(h, r->state);
	h = fold(h, r->total);
	for (k = 0; k < KP_RNG_SOURCES; k++) sources += r->counters[k] * (uint32_t)(k + 1);
	h = fold(h, sources);
	h = fold(h, w->free_images.head);
	h = fold(h, w->free_sprites.head);
	h = fold(h, w->free_lone.head);
	for (t = w->active_lone.head; t != KP_NONE; t = w->thg.next[t]) {
		int32_t s = w->thg.sprite[t], i, count = 0;
		h = fold(h, t);
		h = fold(h, s);
#define SPRITE_VALUE(name) h = fold(h, w->spr.name[s]);
		SPRITE_VALUE(id) SPRITE_VALUE(owner) SPRITE_VALUE(selection) SPRITE_VALUE(visibility) SPRITE_VALUE(elevation)
		SPRITE_VALUE(flags) SPRITE_VALUE(sel_timer) SPRITE_VALUE(index) SPRITE_VALUE(width) SPRITE_VALUE(height)
		SPRITE_VALUE(x) SPRITE_VALUE(y) SPRITE_VALUE(main)
#undef SPRITE_VALUE
		for (i = w->spr.images[s].head; i != KP_NONE; i = w->img.next[i]) count++;
		h = fold(h, count);
		for (i = w->spr.images[s].head; i != KP_NONE; i = w->img.next[i]) {
			h = fold(h, i);
#define IMAGE_VALUE(name) h = fold(h, w->img.name[i]);
			IMAGE_VALUE(id) IMAGE_VALUE(method) IMAGE_VALUE(dir)
			h = fold(h, w->img.flags[i] & ~1);
			IMAGE_VALUE(xoff) IMAGE_VALUE(yoff) IMAGE_VALUE(header) IMAGE_VALUE(pc) IMAGE_VALUE(ret) IMAGE_VALUE(anim)
			IMAGE_VALUE(wait) IMAGE_VALUE(frameset) IMAGE_VALUE(frame) IMAGE_VALUE(x) IMAGE_VALUE(y) IMAGE_VALUE(param)
#undef IMAGE_VALUE
		}
	}
	return h;
}

typedef struct {
	int32_t frames; /* the frames completed after the setup frame 0 (-1: the setup faulted) */
	uint32_t hash;
	char fault[KP_ERROR_SIZE];
} TraceResult;

typedef struct {
	const KpDatColumn *sprite_image, *iscript_id;
} TraceColumns;

/* The world of a trace scene, reused by its cases (the pool initializers zero every record). */
static Scene *trace_scene(KpT *T, const uint8_t *code, size_t code_size, TraceColumns *cols)
{
	Scene *sc = new_scene(T);
	load_data(T, sc);
	if (code) {
		/* a bare iscript.bin: the headers from its index (SetImageScriptHeader 0x40f340's walk) */
		KpError err;
		uint32_t at = kp_le_u32(code), ids = 0, a;
		for (a = at; a + 4 <= code_size && kp_le_u16(code + a) != 0xffff; a += 4) {
			if (kp_le_u16(code + a) + 1u > ids) ids = kp_le_u16(code + a) + 1u;
		}
		kp_iscript_free(&sc->script);
		kp_t_ok(T, kp_iscript_from_code(&sc->script, code, code_size, ids, &err) == 0, "%s", err.msg);
		for (a = at; a + 4 <= code_size && kp_le_u16(code + a) != 0xffff; a += 4) {
			sc->script.headers[kp_le_u16(code + a)] = (int32_t)kp_le_u16(code + a + 2);
		}
	}
	open_world(T, sc, TRACE_MAP, TRACE_MAP);
	sc->w->has_sound = 1;
	sc->hook = trace_hooks;
	cols->sprite_image = column(T, &sc->dat, "sprites", "image");
	cols->iscript_id = column(T, &sc->dat, "images", "iscript_id");
	return sc;
}

/* Runs one case; script_id >= 0 replaces the root image's images.dat IScript id while its sprite is created (the
 * synthetic programs). */
static void trace_run(Scene *sc, const TraceColumns *cols, int32_t image, int32_t anim, int32_t script_id, TraceResult *r)
{
	KpImages *w = sc->w;
	volatile uint32_t saved_image, saved_script; /* read after the longjmp (GCC's -Wclobbered) */
	int32_t t, f;
	KpTry outer, inner;
	r->frames = -1;
	r->hash = 0;
	r->fault[0] = '\0';
	sc->event_count = 0;
	kp_rng_game_init(&sc->rng, TRACE_SEED);
	kp_rng_game_seed(&sc->rng, TRACE_SEED);
	kp_rng_game_set_enabled(&sc->rng, 1);
	w->tileset = image % 8;
	w->step_limit = STEP_LIMIT;
	kp_images_begin(w, &outer);
	if (setjmp(outer.buf)) {
		kp_images_end(w, &outer);
		snprintf(r->fault, sizeof r->fault, "%s", w->error);
		return;
	}
	kp_images_init_pools(w);
	kp_images_init_thingies(w);
	if (!kp_dat_has(cols->sprite_image, ROOT_SPRITE) || image < 0 || !kp_dat_has(cols->iscript_id, (uint32_t)image)) {
		kp_images_fail(w, "images: no DAT value for image %d", (int)image);
	}
	saved_image = kp_dat_value(cols->sprite_image, ROOT_SPRITE);
	saved_script = kp_dat_value(cols->iscript_id, (uint32_t)image);
	kp_dat_set(&sc->dat, cols->sprite_image, ROOT_SPRITE, (uint32_t)image);
	if (script_id >= 0) kp_dat_set(&sc->dat, cols->iscript_id, (uint32_t)image, (uint32_t)script_id);
	kp_images_begin(w, &inner);
	if (setjmp(inner.buf)) {
		kp_images_end(w, &inner);
		kp_dat_set(&sc->dat, cols->sprite_image, ROOT_SPRITE, saved_image);
		kp_dat_set(&sc->dat, cols->iscript_id, (uint32_t)image, saved_script);
		kp_images_rethrow(w);
	}
	t = kp_lone_create(w, ROOT_SPRITE, ROOT_X, ROOT_Y, image % 8);
	kp_images_end(w, &inner);
	kp_dat_set(&sc->dat, cols->sprite_image, ROOT_SPRITE, saved_image);
	kp_dat_set(&sc->dat, cols->iscript_id, (uint32_t)image, saved_script);
	if (anim != 0 && t != KP_NONE) kp_sprite_play(w, w->thg.sprite[t], anim, 1);
	r->hash = frame_hash(sc, 0, r->hash);
	r->frames = 0;
	for (f = 1; f <= TRACE_FRAMES; f++) {
		kp_images_update_thingies(w);
		r->hash = frame_hash(sc, f, r->hash);
		r->frames = f;
	}
	kp_images_end(w, &outer);
}

static void scenario_matches_probe(KpT *T)
{
	const KpJson *g;
	if (!kp_t_ready(T, "test_images", TRACE_FILES + 3)) return;
	g = kp_t_golden(T, GOLDEN);
	kp_t_eq(T, kp_json_field_int(g, "frames", -1), TRACE_FRAMES, "frames");
	kp_t_eq(T, kp_json_field_int(g, "seed", -1), TRACE_SEED, "seed");
	kp_t_eq(T, kp_json_field_int(g, "root_sprite", -1), ROOT_SPRITE, "root sprite");
	kp_t_eq(T, kp_json_field_int(g, "root_x", -1), ROOT_X, "root x");
	kp_t_eq(T, kp_json_field_int(g, "root_y", -1), ROOT_Y, "root y");
	kp_t_eq(T, kp_json_field_int(g, "map", -1), TRACE_MAP, "map");
	kp_t_ok(T, kp_json_len(kp_json_get(g, "cases")) > 2900, "case count");
}

static void every_animation_equals_original(KpT *T)
{
	const KpJson *g, *cases;
	TraceColumns cols;
	TraceResult *r;
	Scene *sc;
	KpBuf *bad;
	int k, ran = 0, differ = 0;
	if (!kp_t_ready(T, "test_images", TRACE_FILES)) return;
	g = kp_t_golden(T, GOLDEN);
	cases = kp_json_get(g, "cases");
	sc = trace_scene(T, NULL, 0, &cols);
	r = (TraceResult *)malloc(sizeof *r);
	kp_t_ok(T, r != NULL, "out of memory");
	kp_t_defer(T, free, r);
	bad = kp_t_buf(T);
	for (k = 0; k < kp_json_len(cases); k++) {
		const KpJson *c = kp_json_at(cases, k);
		if (!kp_t_mine(T, k + 1)) continue;
		trace_run(sc, &cols, (int32_t)kp_json_int(kp_json_at(c, 0), -1), (int32_t)kp_json_int(kp_json_at(c, 1), -1), -1, r);
		ran++;
		if (r->frames != kp_json_int(kp_json_at(c, 2), -2) || r->hash != (uint32_t)kp_json_int(kp_json_at(c, 3), -1)) {
			if (differ++ < 20) {
				kp_buf_printf(bad, "\nimage %lld anim %lld: frames %d/%lld hash %u/%lld %s", (long long)kp_json_int(kp_json_at(c, 0), -1),
					(long long)kp_json_int(kp_json_at(c, 1), -1), (int)r->frames, (long long)kp_json_int(kp_json_at(c, 2), -1),
					(unsigned)r->hash, (long long)kp_json_int(kp_json_at(c, 3), -1), r->fault);
			}
		}
	}
	kp_t_ok(T, differ == 0, "%d of %d cases differ from the original:%s", differ, ran, kp_buf_text(bad));
}

static int hex(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c - 'A' + 10; }

/* Synthetic programs appended to the retail script (opcodes and operand ranges no stock animation reaches without a
 * unit): the retail code with the probe's tail and index. */
static void synthetic_programs_equal_original(KpT *T)
{
	const KpJson *g, *tail, *synthetic;
	TraceColumns cols;
	TraceResult *r;
	Scene *sc;
	KpIScript retail;
	KpError err;
	size_t size, code_size, retail_size;
	uint8_t *data, *code;
	uint32_t index;
	int k;
	if (!kp_t_ready(T, "test_images", TRACE_FILES)) return;
	g = kp_t_golden(T, GOLDEN);
	data = kp_t_read(T, DATA "iscript.kpd", &size);
	kp_t_ok(T, kp_iscript_load(&retail, data, size, "iscript.kpd", NULL, &err) == 0, "%s", err.msg);
	if (retail.code_size != (uint32_t)kp_json_field_int(g, "retail_size", -1) || retail.code_size < 4) {
		kp_iscript_free(&retail);
		kp_t_fail(T, "the retail iscript.bin's size");
	}
	tail = kp_json_get(g, "synthetic_iscript");
	kp_t_ok(T, tail && tail->count % 2 == 0, "the synthetic tail");
	retail_size = retail.code_size;
	code_size = retail_size + (size_t)tail->count / 2;
	code = (uint8_t *)malloc(code_size);
	if (!code) {
		kp_iscript_free(&retail);
		kp_t_fail(T, "out of memory");
	}
	kp_t_defer(T, free, code);
	memcpy(code, retail.code, retail.code_size);
	kp_iscript_free(&retail);
	index = (uint32_t)kp_json_field_int(g, "synthetic_index", -1);
	code[0] = (uint8_t)(index & 0xff);
	code[1] = (uint8_t)(index >> 8 & 0xff);
	code[2] = (uint8_t)(index >> 16 & 0xff);
	code[3] = 0;
	for (k = 0; k < tail->count / 2; k++) {
		code[retail_size + (size_t)k] = (uint8_t)(hex(tail->s[2 * k]) * 16 + hex(tail->s[2 * k + 1]));
	}
	sc = trace_scene(T, code, code_size, &cols);
	r = (TraceResult *)malloc(sizeof *r);
	kp_t_ok(T, r != NULL, "out of memory");
	kp_t_defer(T, free, r);
	synthetic = kp_json_get(g, "synthetic");
	kp_t_ok(T, kp_json_len(synthetic) > 0, "the synthetic cases");
	for (k = 0; k < kp_json_len(synthetic); k++) {
		const KpJson *c = kp_json_at(synthetic, k);
		int64_t image = kp_json_int(kp_json_at(c, 0), -1), anim = kp_json_int(kp_json_at(c, 2), -1);
		trace_run(sc, &cols, (int32_t)image, (int32_t)anim, (int32_t)kp_json_int(kp_json_at(c, 1), -1), r);
		kp_t_eq(T, r->frames, kp_json_int(kp_json_at(c, 3), -2), "image %lld anim %lld frames (%s)", (long long)image,
			(long long)anim, r->fault);
		kp_t_eq(T, r->hash, kp_json_int(kp_json_at(c, 4), -1), "image %lld anim %lld hash", (long long)image, (long long)anim);
	}
}

static const KpTest TESTS[] = {
	{ "list_helpers_follow_the_native_ones", list_helpers_follow_the_native_ones, 0 },
	{ "new_game_pools_hand_out_record_0_then_descending", new_game_pools_hand_out_record_0_then_descending, 0 },
	{ "depth_key", depth_key, 0 },
	{ "cloak_initialisers_and_callbacks", cloak_initialisers_and_callbacks, 0 },
	{ "sprite_rows_truncate_and_clamp", sprite_rows_truncate_and_clamp, 0 },
	{ "sprite_without_main_image_and_disabled_sound", sprite_without_main_image_and_disabled_sound, 0 },
	{ "pool_records", pool_records, 0 },
	{ "health_bars", health_bars, 0 },
	{ "selection_circles", selection_circles, 0 },
	{ "scenario_matches_probe", scenario_matches_probe, 0 },
	{ "every_animation_equals_original", every_animation_equals_original, 6 },
	{ "synthetic_programs_equal_original", synthetic_programs_equal_original, 0 },
};
KP_SUITE(kp_suite_images, "test_images", TESTS);
