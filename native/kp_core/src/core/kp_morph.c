/* Changing an existing unit's type in place (kp_game.h), ported from the atlas (P08, P09): ChangeUnitType 0x424ad0 with
 * ReinitializeUnitTypeKeepPresentation 0x424bd0, ChangeSubunitType 0x424a00 and ReinitializeMorphedUnit 0x426750. */
#include "kp_game_internal.h"

#define NO_UNIT 228
#define EGG 0x24
#define DAT_BUILDING 0x1
#define STATUS_COMPLETED 0x1
#define STATUS_GROUNDED_BUILDING 0x2
#define STATUS_CLOAK_BITS 0x300
#define STATUS_DISABLED 0x400
#define STATUS_NOT_INTERRUPTIBLE 0x1000
#define STATUS_NOBRK 0x2000
#define SPRITE_SELECTED 0x08
#define SPRITE_CIRCLE 0x01
#define SPRITE_DASHED 0x06
#define SPRITE_HIDDEN 0x20
#define SPRITE_SUBUNIT 0x10
#define SECONDARY_NOTHING 0x17
#define TURRET_LO 2

/* The cloak drawing of sprite s: its mode (status bits 0x100, 0x200) and the main image's two coloring bytes
 * (GetMainImageColorBytes 0x416030). */
typedef struct {
	int32_t mode, a, b;
} Cloak;

static Cloak save_cloak(KpGame *g, int64_t n)
{
	KpImages *w = g->w;
	Cloak c;
	c.mode = (int32_t)kp_band(kp_shrv(g->u.status[n], 8), 3);
	c.a = c.b = 0;
	if (c.mode != 0) {
		int32_t main = kp_check_image(w, w->spr.main[kp_unit_sprite(g, n)]);
		c.a = w->img.param[main] & 0xff;
		c.b = (w->img.param[main] >> 8) & 0xff;
	}
	return c;
}

/* The cloak drawing again (0x415e50), detected by the local player through `detector`'s detection (0x417400). */
static void restore_cloak(KpGame *g, int64_t n, Cloak c, int64_t detector)
{
	KpImages *w = g->w;
	int32_t s = kp_unit_sprite(g, n);
	if (c.mode == 0) return;
	kp_sprite_set_cloak_mode(w, s, c.mode, c.a, c.b);
	if (kp_band(g->u.visibility[detector], g->local_vision) != 0) kp_sprite_cloak_detected(w, s);
}

/* The build queue entry of the current slot (+0x98 + 2 * +0xa4). */
static int64_t *queue_slot(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	switch (u->queue_slot[n]) {
	case 0: return &u->queue0[n];
	case 1: return &u->queue1[n];
	case 2: return &u->queue2[n];
	case 3: return &u->queue3[n];
	case 4: return &u->queue4[n];
	}
	kp_game_fail(g, "morph: build queue slot %lld", (long long)u->queue_slot[n]);
}

/* ReinitializeMorphedUnit 0x426750: the type's initialization over a new sprite, hit points in proportion, the order
 * kept. */
static void reinitialize(KpGame *g, int64_t n, int64_t t)
{
	KpUnits *u = &g->u;
	KpImages *w = g->w;
	int64_t order = u->order[n], state = u->order_state[n], ox = u->order_x[n], oy = u->order_y[n];
	int64_t target = u->order_unit[n], hp = kp_sarv(kp_s32v(u->hp[n]), 8), x, y, owner, prev, h;
	int32_t s = kp_unit_sprite(g, n);
	u->previous_type[n] = u->type[n];
	u->previous_hp[n] = kp_mod(hp, 65536) != 0 ? kp_mod(hp, 65536) : 1;
	owner = w->spr.owner[s];
	x = kp_s16v(w->spr.x[s]);
	y = kp_s16v(w->spr.y[s]);
	if (w->spr.images[s].head != KP_NONE) {
		kp_sprite_destroy(w, s);
		u->sprite[n] = KP_NONE;
	}
	if (u->path[n] != 0) kp_path_free(g, n);
	if (!kp_unit_initialize_type(g, n, t, x, y, u->owner[n])) kp_game_fail(g, "morph: no sprite for unit type %lld", (long long)t);
	u->status[n] = kp_band(u->status[n], kp_bnot(STATUS_DISABLED));
	h = kp_s32v(kp_sarv(kp_s32v(KP_DAT(g, units, hit_points, t)), 8) * hp);
	prev = kp_sarv(kp_s32v(KP_DAT(g, units, hit_points, u->previous_type[n])), 8);
	h = prev != 0 ? kp_s32v(h / prev) : 1;
	h = kp_s32v(kp_shlv(h, 8));
	u->hp[n] = h != 0 ? h : 1;
	kp_refresh_unit_strength(g, n);
	u->status[n] = kp_band(u->status[n], kp_bnot(STATUS_NOT_INTERRUPTIBLE | STATUS_NOBRK));
	if (KP_DAT(g, orders, interruptable, order) == 0) u->status[n] = kp_bor(u->status[n], STATUS_NOT_INTERRUPTIBLE);
	w->spr.owner[kp_unit_sprite(g, n)] = (int32_t)owner; /* SetSpriteOwner 0x417160 */
	if (kp_band(u->status[n], STATUS_GROUNDED_BUILDING) != 0) *queue_slot(g, n) = NO_UNIT;
	u->order[n] = order;
	u->order_state[n] = state;
	u->order_x[n] = ox;
	u->order_y[n] = oy;
	u->order_unit[n] = target;
	kp_set_button_set(g, n, kp_band(u->status[n], STATUS_COMPLETED) != 0 ? t : NO_UNIT);
}

/* ReinitializeUnitTypeKeepPresentation 0x424bd0: the counts, the selection, its circles and the cloak survive. */
static void reinitialize_keeping(KpGame *g, int64_t n, int64_t t)
{
	KpUnits *u = &g->u;
	KpImages *w = g->w;
	int32_t s = kp_unit_sprite(g, n);
	int64_t visibility = w->spr.visibility[s], timer = w->spr.sel_timer[s], slot = 0, color;
	int selected = (w->spr.flags[s] & SPRITE_SELECTED) != 0, circle = (w->spr.flags[s] & SPRITE_CIRCLE) != 0;
	Cloak c;
	if (w->spr.flags[s] & SPRITE_DASHED) kp_game_fail(g, "morph: the dashed circles of a unit (0x416cd0, 0x415190)");
	if (selected) {
		slot = w->spr.selection[s];
		kp_remove_selection_circle(g, n);
	}
	color = kp_sprite_remove_circle(w, s);
	c = save_cloak(g, n);
	kp_account_live(g, n, -1);
	if (kp_band(u->status[n], STATUS_COMPLETED) != 0) kp_account_completed(g, n, -1, 1);
	reinitialize(g, n, t);
	kp_account_live(g, n, 1);
	if (kp_band(u->status[n], STATUS_COMPLETED) != 0) kp_account_completed(g, n, 1, 1);
	restore_cloak(g, n, c, n);
	s = kp_unit_sprite(g, n);
	if (selected) kp_add_selection_circle(g, n, slot);
	else if (circle) kp_sprite_add_circle(w, s, (int32_t)color);
	w->spr.sel_timer[s] = (int32_t)timer;
	kp_sprite_set_visibility(w, s, (int32_t)visibility);
}

/* ChangeSubunitType 0x424a00: a turret of the new type at its parent's turret point. */
static void change_subunit_type(KpGame *g, int64_t sub, int64_t t)
{
	KpUnits *u = &g->u;
	KpImages *w = g->w;
	int64_t parent = u->subunit[sub];
	int32_t s, ps;
	int32_t ox, oy;
	Cloak c = save_cloak(g, sub);
	reinitialize(g, sub, t);
	s = kp_unit_sprite(g, sub);
	w->spr.flags[s] |= SPRITE_SUBUNIT;
	restore_cloak(g, sub, c, parent);
	ps = kp_unit_sprite(g, parent);
	kp_images_lo_offset(w, kp_check_image(w, w->spr.main[ps]), TURRET_LO, 0, &ox, &oy);
	kp_image_set_offset(w, kp_check_image(w, w->spr.main[s]), ox & 0xff, oy & 0xff);
	kp_sprite_set_visibility(w, s, w->spr.visibility[ps]);
}

static int local_selection_count(KpGame *g)
{
	int k = 0;
	while (k < KP_SLOTS && g->local_selection[k] != 0) k++;
	return k;
}

/* ChangeUnitType 0x424ad0. */
void kp_change_unit_type(KpGame *g, int64_t n, int64_t t)
{
	KpUnits *u = &g->u;
	KpImages *w = g->w;
	int64_t x, y, sub;
	int32_t s = kp_unit_sprite(g, n);
	if ((w->spr.flags[s] & SPRITE_HIDDEN) == 0) kp_unit_deactivate_spatial(g, n);
	reinitialize_keeping(g, n, t);
	if (kp_band(KP_DAT(g, units, special_ability_flags, u->type[n]), DAT_BUILDING) != 0
		&& kp_band(u->status[n], STATUS_CLOAK_BITS) != 0) {
		kp_set_secondary_order(g, n, SECONDARY_NOTHING);
		kp_game_fail(g, "morph: a cloaked unit becomes a building (0x41d4c0)");
	}
	s = kp_unit_sprite(g, n);
	if ((w->spr.flags[s] & SPRITE_SELECTED) != 0 && !kp_multi_selectable(g, n) && local_selection_count(g) > 1 && t != EGG) {
		kp_game_fail(g, "morph: a unit leaves a group selection (0x4a7b80, 0x4268d0)");
	}
	x = kp_s16v(w->spr.x[s]);
	y = kp_s16v(w->spr.y[s]);
	kp_clamp_to_map(g, u->type[n], &x, &y);
	if (x != kp_s16v(w->spr.x[s]) || y != kp_s16v(w->spr.y[s])) kp_unit_relocate(g, n, x, y);
	if ((w->spr.flags[s] & SPRITE_HIDDEN) == 0) kp_unit_activate_spatial(g, n);
	sub = u->subunit[n];
	if (sub != 0) change_subunit_type(g, sub, KP_DAT(g, units, subunit1, t));
	kp_rebuild_status_overlays(g, n);
}
