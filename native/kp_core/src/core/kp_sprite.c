/* Sprites, lone thingies, fog ghosts and selection images (kp_images.h;
 * game/images/sprite.lua and selection.lua, the specification). */
#include "kp_images_internal.h"

#define CURSOR_SPRITE 318
#define THINGY_LONE 1
#define ELEVATION_DEFAULT 4
#define VISIBLE_ALL 0xff
#define EXPLORED_LAST 0x81
#define EXPLORED_RANGE_FIRST 0x182
#define EXPLORED_RANGE_LAST 0x1e0
#define SF_UNTARGETED 0x40 /* sprite flag read by the cloak drawing (0x417400, 0x4161c0) */
#define HEALTH_IMAGE 0x230
#define BOX_IMAGE 0x247
#define CIRCLE_FIRST 0x231
#define CIRCLE_LAST 0x23a
#define BAR_SEGMENT 3
#define BAR_MIN_WIDTH 19
#define BAR_ROWS 5
#define BAR_SHIELD_ROWS 7
#define BAR_ENERGY_ROWS 6
#define BAR_Y_GAP 8
#define SPELLCASTER 0x200000
#define BROODLING 40

/* Floor division for a positive divisor (Lua's math.floor(a / b)). */
static int32_t floordiv(int32_t a, int32_t b)
{
	int32_t q = a / b;
	return (a % b != 0 && a < 0) ? q - 1 : q;
}

static int32_t s16(int32_t v) { return kp_s16((uint32_t)v); }

/* --- image creation in a sprite's list --- */

typedef enum { ABOVE_MAIN, ON_TOP, BELOW_MAIN } Where;

static int32_t create_image(KpImages *w, int32_t s, int32_t id, int32_t xoff, int32_t yoff, int32_t dir, Where where,
	int copy)
{
	KpImageRecords *img = &w->img;
	int32_t i = kp_image_alloc(w);
	KpList *lst;
	if (i == KP_NONE) return KP_NONE;
	lst = &w->spr.images[s];
	if (lst->head == KP_NONE) {
		w->spr.main[s] = i;
		kp_list_insert_after_head(img->prev, img->next, lst, i);
	} else if (where == ON_TOP) {
		kp_list_insert_before(img->prev, img->next, lst, lst->head, i);
	} else {
		int32_t main = kp_check_image(w, w->spr.main[s]);
		if (where == ABOVE_MAIN) kp_list_insert_before(img->prev, img->next, lst, main, i);
		else kp_list_insert_after(img->prev, img->next, lst, main, i);
	}
	kp_image_init_record(w, i, id, s, xoff, yoff);
	if (copy) kp_image_init_script(w, i);
	else kp_image_init_drawing(w, i);
	kp_image_set_direction(w, i, dir);
	return i;
}

int32_t kp_sprite_image_above_main(KpImages *w, int32_t s, int32_t id, int32_t xoff, int32_t yoff, int32_t dir)
{
	return create_image(w, s, id, xoff, yoff, dir, ABOVE_MAIN, 0);
}

int32_t kp_sprite_image_on_top(KpImages *w, int32_t s, int32_t id, int32_t xoff, int32_t yoff, int32_t dir)
{
	return create_image(w, s, id, xoff, yoff, dir, ON_TOP, 0);
}

int32_t kp_sprite_image_below_main(KpImages *w, int32_t s, int32_t id, int32_t xoff, int32_t yoff, int32_t dir)
{
	return create_image(w, s, id, xoff, yoff, dir, BELOW_MAIN, 0);
}

int32_t kp_sprite_image_copy_on_top(KpImages *w, int32_t s, int32_t id, int32_t xoff, int32_t yoff, int32_t dir)
{
	return create_image(w, s, id, xoff, yoff, dir, ON_TOP, 1);
}

static void release_all(KpImages *w, int32_t s)
{
	int32_t i = w->spr.images[s].head;
	while (i != KP_NONE) {
		int32_t n = w->img.next[i];
		kp_image_release(w, i);
		i = n;
	}
}

int32_t kp_sprite_clone(KpImages *w, int32_t src)
{
	KpSpriteRecords *spr = &w->spr;
	KpImageRecords *img = &w->img;
	int32_t s = kp_sprite_create(w, spr->id[src], s16(spr->x[src]), s16(spr->y[src]), spr->owner[src]), i;
	if (s == KP_NONE) return KP_NONE;
	release_all(w, s);
	spr->main[s] = KP_NONE;
	i = spr->images[src].tail;
	while (i != KP_NONE) {
		int32_t m = img->method[i];
		if (m != KP_METHOD_HEALTH && m != KP_METHOD_SELECTION) {
			int32_t c = kp_sprite_image_copy_on_top(w, s, img->id[i], img->xoff[i] & 0xff, img->yoff[i] & 0xff,
				img->dir[i]);
			if (c == KP_NONE) {
				kp_sprite_destroy(w, s);
				return KP_NONE;
			}
			img->frameset[c] = img->frameset[i];
			img->frame[c] = img->frame[i];
			kp_image_set_mirror(w, c, (img->flags[i] & KP_F_MIRROR) ? 1 : 0);
			kp_image_bounds(w, c);
			if (i == spr->main[src]) spr->main[s] = c;
		}
		i = img->prev[i];
	}
	return s;
}

void kp_sprite_take_main(KpImages *w, int32_t dst, int32_t src)
{
	KpSpriteRecords *spr = &w->spr;
	KpImageRecords *img = &w->img;
	int32_t i;
	KpList *lst;
	if (src == KP_NONE) return;
	i = spr->main[src];
	if (i == KP_NONE) return;
	kp_check_sprite(w, dst);
	spr->main[src] = KP_NONE;
	kp_list_unlink(img->prev, img->next, &spr->images[src], i);
	lst = &spr->images[dst];
	/* The original inserts before the head without a null check. */
	if (lst->head == KP_NONE) kp_images_fail(w, "images: turret image moved to a sprite without images (0x415540)");
	kp_list_insert_before(img->prev, img->next, lst, lst->head, i);
	img->sprite[i] = dst;
	img->flags[i] &= 0xfff7;
}

void kp_sprite_set_visibility(KpImages *w, int32_t s, int32_t mask) { w->spr.visibility[s] = mask & 0xff; }

int kp_sprite_init(KpImages *w, int32_t s, int32_t id, int32_t x, int32_t y, int32_t owner)
{
	KpSpriteRecords *spr = &w->spr;
	int32_t r[6];
	x &= 0xffff;
	y &= 0xffff;
	if (x >= w->map_w_px || y >= w->map_h_px) return 0;
	spr->owner[s] = owner & 0xff;
	spr->id[s] = id;
	spr->elevation[s] = ELEVATION_DEFAULT;
	spr->flags[s] = 0;
	spr->x[s] = x;
	spr->y[s] = y;
	spr->visibility[s] = VISIBLE_ALL;
	spr->sel_timer[s] = 0;
	if (kp_images_dat(w, w->col.sprite_is_visible, id) == 0) {
		spr->flags[s] = KP_SF_HIDDEN;
		kp_sprite_set_visibility(w, s, 0);
	}
	if (kp_sprite_image_above_main(w, s, (int32_t)kp_images_dat(w, w->col.sprite_image, id), 0, 0, 0) == KP_NONE) return 0;
	kp_images_frame_rect(w, kp_check_image(w, spr->main[s]), 0, r);
	spr->width[s] = r[4] < 255 ? r[4] : 255;
	spr->height[s] = r[5] < 255 ? r[5] : 255;
	return 1;
}

int32_t kp_sprite_create(KpImages *w, int32_t id, int32_t x, int32_t y, int32_t owner)
{
	KpSpriteRecords *spr = &w->spr;
	int32_t s = w->free_sprites.head;
	if (s == KP_NONE) return KP_NONE;
	kp_list_unlink(spr->prev, spr->next, &w->free_sprites, s);
	if (!kp_sprite_init(w, s, id, x, y, owner)) {
		kp_list_insert_after_head(spr->prev, spr->next, &w->free_sprites, s);
		return KP_NONE;
	}
	kp_list_insert_after_head(spr->prev, spr->next, &w->rows[kp_images_sprite_row(w, s)], s);
	return s;
}

static void return_sprite(KpImages *w, int32_t s)
{
	kp_list_unlink(w->spr.prev, w->spr.next, &w->rows[kp_images_sprite_row(w, s)], s);
	kp_list_insert_after_head(w->spr.prev, w->spr.next, &w->free_sprites, s);
}

void kp_sprite_destroy(KpImages *w, int32_t s)
{
	release_all(w, s);
	return_sprite(w, s);
}

void kp_sprite_move(KpImages *w, int32_t s, int32_t x, int32_t y)
{
	KpSpriteRecords *spr = &w->spr;
	int32_t old, now;
	x &= 0xffff;
	y &= 0xffff;
	if (spr->x[s] == x && spr->y[s] == y) return;
	old = kp_images_sprite_row(w, s);
	spr->x[s] = x;
	spr->y[s] = y;
	now = kp_images_sprite_row(w, s);
	if (now != old) {
		kp_list_unlink(spr->prev, spr->next, &w->rows[old], s);
		kp_list_insert_after_head(spr->prev, spr->next, &w->rows[now], s);
	}
}

void kp_sprite_update(KpImages *w, int32_t s)
{
	KpList *lst = &w->spr.images[s];
	int32_t i = lst->head;
	while (i != KP_NONE) {
		int32_t n = w->img.next[i];
		kp_image_update(w, i);
		kp_images_run_iscript(w, i, NULL, 0, 0, NULL);
		i = n;
		if (lst->head == KP_NONE) return_sprite(w, s);
	}
}

void kp_sprite_play(KpImages *w, int32_t s, int32_t anim, int force)
{
	int32_t i = w->spr.images[s].head;
	while (i != KP_NONE) {
		if (force || !(w->spr.flags[s] & KP_SF_NOBRK)) kp_image_play(w, i, anim);
		i = w->img.next[i];
	}
}

void kp_sprite_set_direction(KpImages *w, int32_t s, int32_t d)
{
	int32_t i = w->spr.images[kp_check_sprite(w, s)].head;
	while (i != KP_NONE) {
		kp_image_set_direction(w, i, d);
		i = w->img.next[i];
	}
}

void kp_sprite_set_facing(KpImages *w, int32_t s, int32_t heading)
{
	int32_t i = w->spr.images[s].head;
	while (i != KP_NONE) {
		kp_image_direction_from_angle(w, i, heading);
		i = w->img.next[i];
	}
}

void kp_sprite_refresh_bounds(KpImages *w, int32_t s)
{
	int32_t i = w->spr.images[s].head;
	while (i != KP_NONE) {
		int32_t g = w->img.grp[i];
		if ((g > 0 && (uint32_t)g <= w->grp->grp_count && w->img.frame[i] < w->grp->grps[g].frames)
			|| (g == KP_GRP_VIRTUAL && w->has_vgrp[i])) {
			kp_image_bounds(w, i);
		}
		i = w->img.next[i];
	}
}

void kp_images_refresh_bounds(KpImages *w)
{
	int32_t r;
	for (r = 0; r < w->map_h && r < KP_ROWS; r++) {
		int32_t s = w->rows[r].head;
		while (s != KP_NONE) {
			kp_sprite_refresh_bounds(w, s);
			s = w->spr.next[s];
		}
	}
}

uint32_t kp_sprite_depth_key(const KpImages *w, int32_t s)
{
	uint32_t elev = (uint32_t)w->spr.elevation[s];
	uint32_t key = kp_shl(elev, 13);
	if (elev <= 4) key |= kp_u32(kp_s16((uint32_t)w->spr.y[s]));
	key = kp_shl(key, 5) | ((uint32_t)w->spr.flags[s] & KP_SF_DEPTH);
	return kp_shl(key, 9) | (uint32_t)w->spr.index[s];
}

/* --- lone thingies and fog ghosts --- */

void kp_images_init_thingies(KpImages *w)
{
	KpLoneRecords *thg = &w->thg;
	int32_t t, s;
	w->free_lone.head = w->free_lone.tail = 0;
	for (t = 1; t < KP_LONE; t++) kp_list_insert_after(thg->prev, thg->next, &w->free_lone, 0, t);
	w->free_fog.head = w->free_fog.tail = 0;
	for (t = 1; t < KP_FOG; t++) kp_list_insert_after(w->fog.prev, w->fog.next, &w->free_fog, 0, t);
	t = w->free_lone.head;
	kp_list_unlink(thg->prev, thg->next, &w->free_lone, t);
	thg->kind[t] = THINGY_LONE;
	s = kp_sprite_create(w, CURSOR_SPRITE, 0, 0, 0);
	thg->sprite[t] = s;
	if (s == KP_NONE) {
		kp_list_insert_after_head(thg->prev, thg->next, &w->free_lone, t);
		kp_images_fail(w, "images: no sprite for the placement cursor");
	}
	kp_list_insert_after_head(thg->prev, thg->next, &w->active_lone, t);
	w->cursor = t;
	w->spr.flags[s] |= KP_SF_HIDDEN;
	kp_sprite_set_visibility(w, s, 0);
}

int32_t kp_lone_create(KpImages *w, int32_t id, int32_t x, int32_t y, int32_t owner)
{
	KpLoneRecords *thg = &w->thg;
	int32_t t = w->free_lone.head, s;
	if (t == KP_NONE) return KP_NONE;
	kp_list_unlink(thg->prev, thg->next, &w->free_lone, t);
	thg->kind[t] = THINGY_LONE;
	s = kp_sprite_create(w, id, x, y, owner);
	thg->sprite[t] = s;
	if (s == KP_NONE) {
		kp_list_insert_after_head(thg->prev, thg->next, &w->free_lone, t);
		return KP_NONE;
	}
	kp_list_insert_after_head(thg->prev, thg->next, &w->active_lone, t);
	return t;
}

void kp_thingy_visibility(KpImages *w, int32_t t)
{
	int32_t s = w->thg.sprite[t], tx, ty, mask;
	if (s == KP_NONE || (w->spr.flags[s] & KP_SF_HIDDEN)) return;
	tx = kp_idiv(s16(w->spr.x[s]), 32);
	ty = kp_idiv(s16(w->spr.y[s]), 32);
	mask = 255 - kp_images_hidden_at(w, tx, ty);
	if (w->spr.visibility[s] != mask) kp_sprite_set_visibility(w, s, mask);
}

/* OR of complemented tile bytes over a tile rectangle (0x418d00 explored / 0x418d80 seen). */
static int32_t rect_mask(KpImages *w, int32_t (*reader)(KpImages *, int32_t, int32_t), int32_t x, int32_t y,
	int32_t width, int32_t height)
{
	int32_t mask = 0, row, col;
	for (row = y + height - 1; row >= y; row--) {
		for (col = x + width - 1; col >= x; col--) mask |= 255 - reader(w, col, row);
	}
	return mask & 0xff;
}

void kp_thingy_visibility_explored(KpImages *w, int32_t t)
{
	KpSpriteRecords *spr = &w->spr;
	int32_t s = kp_check_sprite(w, w->thg.sprite[t]), sw, sh, width, left, height, top, mask;
	if (spr->flags[s] & KP_SF_HIDDEN) return;
	sw = spr->width[s];
	sh = spr->height[s];
	width = floordiv(sw + 31, 32);
	left = kp_idiv(s16(spr->x[s]) - floordiv(sw, 2), 32);
	height = floordiv(sh + 31, 32);
	top = kp_idiv(s16(spr->y[s]) - floordiv(sh, 2), 32);
	if (left < 0) {
		width += left;
		left = 0;
	} else if (width + left > w->map_w) {
		width = w->map_w - left;
	}
	if (top < 0) {
		height += top;
		top = 0;
	} else if (height + top > w->map_h) {
		height = w->map_h - top;
	}
	if (w->replay != 0 && w->replay_reveal) mask = 0xff;
	else mask = rect_mask(w, kp_images_unexplored_at, left, top, width, height);
	if (spr->visibility[s] != mask) kp_sprite_set_visibility(w, s, mask);
}

void kp_sprite_hide_uncloakable(KpImages *w, int32_t s)
{
	int32_t i = w->spr.images[s].head;
	while (i != KP_NONE) {
		if (kp_images_dat(w, w->col.draw_if_cloaked, w->img.id[i]) == 0) kp_image_hide(w, i);
		i = w->img.next[i];
	}
}

void kp_sprite_begin_cloak(KpImages *w, int32_t s)
{
	int32_t i;
	kp_sprite_hide_uncloakable(w, s);
	i = w->spr.images[s].head;
	while (i != KP_NONE) {
		int32_t m = w->img.method[i];
		if (m == 0 || m == 4 || m == 7) kp_image_begin_cloak(w, i);
		i = w->img.next[i];
	}
}

void kp_sprite_set_cloak_mode(KpImages *w, int32_t s, int32_t mode, int32_t p0, int32_t p1)
{
	KpImageRecords *img = &w->img;
	int32_t i;
	kp_sprite_hide_uncloakable(w, s);
	i = w->spr.images[s].head;
	while (i != KP_NONE) {
		int32_t m = img->method[i];
		if ((mode == 1 || mode == 2) && m == 0) {
			kp_image_set_method(w, i, mode == 1 ? 2 : 4);
			img->param[i] = (int32_t)(((uint32_t)img->param[i] & 0xffff0000u) + (uint32_t)p0 + (uint32_t)p1 * 256);
		} else if (mode == 3 && (m == 0 || m == 2 || m == 4)) {
			kp_image_set_method(w, i, 3);
		}
		i = img->next[i];
	}
}

void kp_sprite_cloak_detected(KpImages *w, int32_t s)
{
	int32_t i = w->spr.images[s].head;
	if (w->spr.flags[s] & SF_UNTARGETED) {
		while (i != KP_NONE) {
			int32_t m = w->img.method[i];
			if (m > 1 && m < 8) kp_image_set_method(w, i, 0);
			i = w->img.next[i];
		}
		return;
	}
	while (i != KP_NONE) {
		int32_t m = w->img.method[i];
		if (m >= 2 && m <= 4) kp_image_set_method(w, i, m + 3);
		else if (m == 0) kp_image_set_method(w, i, 6);
		i = w->img.next[i];
	}
}

void kp_sprite_cloak_undetected(KpImages *w, int32_t s)
{
	int32_t i;
	if (w->spr.flags[s] & SF_UNTARGETED) {
		kp_sprite_set_cloak_mode(w, s, 3, 0, 0);
		return;
	}
	i = w->spr.images[s].head;
	while (i != KP_NONE) {
		int32_t m = w->img.method[i];
		if (m > 4 && m < 8) kp_image_set_method(w, i, m - 3);
		i = w->img.next[i];
	}
}

int kp_sprite_visibility_rect(KpImages *w, int32_t s, int32_t width_px, int32_t height_px)
{
	KpSpriteRecords *spr = &w->spr;
	int32_t width, left, height, top, mask, old;
	if (s == KP_NONE || (spr->flags[kp_check_sprite(w, s)] & KP_SF_HIDDEN)) return 0;
	width = floordiv(width_px + 31, 32);
	left = kp_idiv(s16(spr->x[s]) - kp_idiv(width_px, 2), 32);
	height = floordiv(height_px + 31, 32);
	top = kp_idiv(s16(spr->y[s]) - kp_idiv(height_px, 2), 32);
	if (left < 0) {
		width += left;
		if (width < 1) return 0;
		left = 0;
	}
	if (top < 0) {
		height += top;
		if (height < 1) return 0;
		top = 0;
	}
	if (width + left > w->map_w) {
		width = w->map_w - left;
		if (width <= 0) return 0;
	}
	if (height + top > w->map_h) {
		height = w->map_h - top;
		if (height <= 0) return 0;
	}
	mask = rect_mask(w, kp_images_hidden_at, left, top, width, height);
	old = spr->visibility[s];
	if (old == mask) return 0;
	kp_sprite_set_visibility(w, s, mask);
	return (old & w->local_vision) != 0 && (mask & w->local_vision) == 0;
}

int kp_thingy_visibility_rect(KpImages *w, int32_t t, int32_t width_px, int32_t height_px)
{
	return kp_sprite_visibility_rect(w, w->thg.sprite[t], width_px, height_px);
}

void kp_lone_update(KpImages *w, int32_t t)
{
	KpLoneRecords *thg = &w->thg;
	KpSpriteRecords *spr = &w->spr;
	int32_t s = kp_check_sprite(w, thg->sprite[t]), id = spr->id[s];
	if (id <= EXPLORED_LAST || (id >= EXPLORED_RANGE_FIRST && id <= EXPLORED_RANGE_LAST)) {
		kp_thingy_visibility_explored(w, t);
	} else {
		kp_thingy_visibility_rect(w, t, spr->width[s], spr->height[s]);
	}
	if (thg->sprite[t] != KP_NONE) {
		kp_sprite_update(w, thg->sprite[t]);
		if (spr->images[thg->sprite[t]].head == KP_NONE) thg->sprite[t] = KP_NONE;
	}
	if (thg->sprite[t] == KP_NONE) {
		kp_list_unlink(thg->prev, thg->next, &w->active_lone, t);
		kp_list_insert_after_head(thg->prev, thg->next, &w->free_lone, t);
	}
}

int32_t kp_fog_create(KpImages *w, int32_t unit_type, int32_t src)
{
	KpFogRecords *fog = &w->fog;
	int32_t t = w->free_fog.head, s;
	if (t == KP_NONE) return KP_NONE;
	kp_list_unlink(fog->prev, fog->next, &w->free_fog, t);
	s = kp_sprite_clone(w, src);
	fog->kind[t] = THINGY_LONE;
	fog->sprite[t] = s;
	if (s == KP_NONE) {
		kp_list_insert_after_head(fog->prev, fog->next, &w->free_fog, t);
		return KP_NONE;
	}
	fog->kind[t] = unit_type;
	kp_list_insert_after_head(fog->prev, fog->next, &w->active_fog, t);
	return t;
}

/* 0x4bc730: unit types whose fog ghosts the interface never targets. */
static int untargeted_ghost(int32_t k)
{
	return k == 14 || k == 85 || k == 105 || k == 202 || k == 205 || k == 206 || k == 207 || k == 208;
}

int32_t kp_fog_at_point(KpImages *w, int32_t x, int32_t y)
{
	KpFogRecords *fog = &w->fog;
	KpSpriteRecords *spr = &w->spr;
	int32_t t = w->active_fog.head;
	while (t != KP_NONE) {
		int32_t k = fog->kind[t];
		if (!untargeted_ghost(k)) {
			int32_t s = kp_check_sprite(w, fog->sprite[t]);
			int32_t bw = s16((int32_t)kp_images_dat(w, w->col.placement_size, k * 2));
			int32_t bh = s16((int32_t)kp_images_dat(w, w->col.placement_size, k * 2 + 1));
			uint32_t dx = (uint32_t)(kp_idiv(bw, 2) + s16(spr->x[s]) - x);
			uint32_t dy = (uint32_t)(kp_idiv(bh, 2) + s16(spr->y[s]) - y);
			if (dx < (uint32_t)spr->width[s] && dy < (uint32_t)spr->height[s]) return t;
		}
		t = fog->next[t];
	}
	return KP_NONE;
}

void kp_sprite_flash(KpImages *w, int32_t s, int32_t color)
{
	KpSpriteRecords *spr = &w->spr;
	int32_t t;
	if (spr->sel_timer[s] == 0) return;
	t = spr->sel_timer[s] - 1;
	if ((spr->visibility[s] & w->local_vision) == 0) t = 0;
	spr->sel_timer[s] = t;
	if ((t & 8) != 0 || (t == 0 && (spr->flags[s] & KP_SF_SELECTED) != 0)) kp_sprite_add_circle(w, s, color);
	else kp_sprite_remove_circle(w, s);
}

/* AreaHiddenFromLocal 0x418eb0: no tile of the rectangle is seen by the local vision. */
static int hidden_from_local(KpImages *w, int32_t left, int32_t top, int32_t width, int32_t height)
{
	int32_t x, y;
	if (w->replay != 0) kp_images_fail(w, "images: fog ghosts in a replay (0x418eb0)");
	for (y = top + height - 1; y >= top; y--) {
		for (x = left + width - 1; x >= left; x--) {
			uint32_t flags = 0;
			int32_t index = y * w->map_w + x;
			if (!w->tile_flags || !w->tile_flags(w, w->ctx, index, &flags)) {
				kp_images_fail(w, "images: no tile flags at %d", (int)index);
			}
			if ((flags & (uint32_t)w->local_vision) == 0) return 0;
		}
	}
	return 1;
}

void kp_fog_update(KpImages *w, int32_t t)
{
	KpFogRecords *fog = &w->fog;
	KpSpriteRecords *spr = &w->spr;
	int32_t s = kp_check_sprite(w, fog->sprite[t]), k, bw, bh, left, top;
	if (!w->has_circle_colors) kp_images_fail(w, "images: fog ghosts without the circle colours");
	kp_sprite_flash(w, s, w->circle_colors[spr->owner[s] & 0xff]);
	k = fog->kind[t];
	bw = s16((int32_t)kp_images_dat(w, w->col.placement_size, k * 2));
	bh = s16((int32_t)kp_images_dat(w, w->col.placement_size, k * 2 + 1));
	left = kp_idiv(s16(spr->x[s]) - kp_idiv(bw, 2), 32);
	top = kp_idiv(s16(spr->y[s]) - kp_idiv(bh, 2), 32);
	if (hidden_from_local(w, left, top, kp_idiv(bw + 31, 32), kp_idiv(bh + 31, 32))) return;
	kp_sprite_remove_circle(w, s);
	kp_sprite_destroy(w, s);
	fog->sprite[t] = KP_NONE;
	kp_list_unlink(fog->prev, fog->next, &w->active_fog, t);
	kp_list_insert_after_head(fog->prev, fog->next, &w->free_fog, t);
}

void kp_images_update_thingies(KpImages *w)
{
	int32_t t = w->active_lone.head;
	while (t != KP_NONE) {
		int32_t n = w->thg.next[t];
		kp_lone_update(w, t);
		t = n;
	}
	t = w->active_fog.head;
	while (t != KP_NONE) {
		int32_t n = w->fog.next[t];
		kp_fog_update(w, t);
		t = n;
	}
}

int32_t kp_image_spawn_sprite(KpImages *w, int32_t i, int32_t id, int32_t dx, int32_t dy, int32_t elevation)
{
	KpSpriteRecords *spr = &w->spr;
	int32_t s = kp_check_sprite(w, w->img.sprite[i]);
	int32_t x = s16(spr->x[s]) + w->img.xoff[i] + dx;
	int32_t y = s16(spr->y[s]) + w->img.yoff[i] + dy;
	int32_t t = kp_lone_create(w, id, x, y, 0);
	if (t == KP_NONE) return KP_NONE;
	spr->elevation[w->thg.sprite[t]] = elevation & 0xff;
	kp_thingy_visibility(w, t);
	return t;
}

/* --- selection.lua --- */

void kp_images_init_selection_records(KpImages *w)
{
	KpImageRecords *img = &w->img;
	int32_t i;
	for (i = KP_IMG_HEALTH_FIRST; i < KP_IMG_HEALTH_FIRST + KP_IMG_HEALTH; i++) {
		img->id[i] = HEALTH_IMAGE;
		img->method[i] = KP_METHOD_HEALTH;
		img->grp[i] = KP_GRP_VIRTUAL;
		w->has_vgrp[i] = 1;
		w->vgrp_width[i] = w->vgrp_height[i] = w->vgrp_fw[i] = w->vgrp_fh[i] = 0;
		kp_image_set_header(w, i, (int32_t)kp_images_dat(w, w->col.iscript_id, HEALTH_IMAGE));
	}
	for (i = KP_IMG_BOXES_FIRST; i < KP_IMG_BOXES_FIRST + KP_IMG_BOXES; i++) {
		img->id[i] = BOX_IMAGE;
		img->method[i] = KP_METHOD_BOX;
		img->grp[i] = KP_GRP_VIRTUAL;
		w->has_vgrp[i] = 1;
		w->vgrp_width[i] = w->vgrp_height[i] = w->vgrp_fw[i] = w->vgrp_fh[i] = 0;
	}
}

/* The bar below the hit points (0x40f6e0, 0x464d20): energy or timer bar, or none. */
static int has_lower_bar(int64_t ability_flags, int64_t status, int64_t unit_type)
{
	int hallucination = (status & KP_STATUS_HALLUCINATION) != 0;
	if ((ability_flags & SPELLCASTER) && !hallucination) return 1;
	return hallucination || unit_type == BROODLING;
}

int32_t kp_health_bar_create(KpImages *w, int32_t s)
{
	KpImageRecords *img = &w->img;
	KpList *lst;
	int32_t i = w->free_health.head, sid, circle, bar, width, height;
	int64_t sprite = s, unit, t, status;
	int cx, cy, cw, ch;
	if (i == KP_NONE) return KP_NONE;
	kp_list_unlink(img->prev, img->next, &w->free_health, i);
	lst = &w->spr.images[s];
	kp_list_insert_before(img->prev, img->next, lst, kp_check_image(w, lst->head), i);
	sid = w->spr.id[s];
	unit = kp_images_hook(w, KP_HOOK_SELECTION_UNIT, -1, &sprite, 1);
	t = kp_images_hook(w, KP_HOOK_UNIT_TYPE, -1, &unit, 1);
	status = kp_images_hook(w, KP_HOOK_UNIT_STATUS, -1, &unit, 1);
	circle = kp_images_grp_of(w, CIRCLE_FIRST + (int32_t)kp_images_dat(w, w->col.circle_image, sid));
	if (circle <= 0 || w->grp->grps[circle].frames == 0) {
		kp_images_fail(w, "images: selection circle of sprite id %d has no GRP frame", (int)sid);
	}
	kp_grpmeta_frame(w->grp, (uint32_t)circle, 0, &cx, &cy, &cw, &ch);
	img->yoff[i] = kp_s8((uint32_t)(ch / 2 + BAR_Y_GAP + (int32_t)kp_images_dat(w, w->col.circle_offset, sid)));
	bar = (int32_t)kp_images_dat(w, w->col.health_bar, sid);
	/* BYTE arithmetic with the C remainder (toward zero): a zero width gives 1, then 19. */
	width = (bar - (bar - 1) % BAR_SEGMENT) & 0xff;
	if (width < BAR_MIN_WIDTH) width = BAR_MIN_WIDTH;
	height = has_lower_bar(kp_images_dat(w, w->col.special_ability_flags, (int32_t)t), status, t) ? BAR_ENERGY_ROWS : 0;
	height += kp_images_dat(w, w->col.shield_enabled, (int32_t)t) != 0 ? BAR_SHIELD_ROWS : BAR_ROWS;
	w->vgrp_fw[i] = w->vgrp_width[i] = width;
	w->vgrp_fh[i] = w->vgrp_height[i] = height;
	img->flags[i] = 0;
	img->sprite[i] = s;
	img->param[i] = (int32_t)unit;
	kp_image_bounds(w, i);
	kp_image_play(w, i, KP_ANIM_INIT);
	return i;
}

void kp_health_bar_release(KpImages *w, int32_t i)
{
	KpImageRecords *img = &w->img;
	kp_list_unlink(img->prev, img->next, &w->spr.images[kp_check_sprite(w, img->sprite[i])], i);
	kp_list_insert_after_head(img->prev, img->next, &w->free_health, i);
}

void kp_selection_circle_release(KpImages *w, int32_t i)
{
	KpImageRecords *img = &w->img;
	kp_list_unlink(img->prev, img->next, &w->spr.images[kp_check_sprite(w, img->sprite[i])], i);
	kp_list_insert_after_head(img->prev, img->next, &w->free_circles, i);
}

static int32_t circle_of(KpImages *w, int32_t s)
{
	int32_t i = w->spr.images[s].tail;
	while (i != KP_NONE) {
		if (w->img.id[i] >= CIRCLE_FIRST && w->img.id[i] <= CIRCLE_LAST) return i;
		i = w->img.prev[i];
	}
	return KP_NONE;
}

void kp_sprite_remove_circle(KpImages *w, int32_t s)
{
	int32_t i;
	if (!(w->spr.flags[s] & KP_SF_CIRCLE)) return;
	w->spr.flags[s] -= KP_SF_CIRCLE;
	i = circle_of(w, s);
	if (i != KP_NONE) kp_selection_circle_release(w, i);
}

void kp_sprite_add_circle(KpImages *w, int32_t s, int32_t color)
{
	if (w->spr.flags[s] & KP_SF_CIRCLE) return;
	if (kp_selection_circle_create(w, s, color, CIRCLE_FIRST) != KP_NONE) w->spr.flags[s] += KP_SF_CIRCLE;
}

void kp_sprite_remove_health_bar(KpImages *w, int32_t s)
{
	int32_t i;
	if (!(w->spr.flags[s] & KP_SF_SELECTED)) return;
	w->spr.flags[s] -= KP_SF_SELECTED;
	i = w->spr.images[s].head;
	while (i != KP_NONE) {
		if (w->img.method[i] == KP_METHOD_HEALTH) {
			kp_health_bar_release(w, i);
			break;
		}
		i = w->img.next[i];
	}
	kp_sprite_remove_circle(w, s);
}

int32_t kp_selection_circle_create(KpImages *w, int32_t s, int32_t color, int32_t first)
{
	KpImageRecords *img = &w->img;
	KpList *lst;
	int32_t i = w->free_circles.head, tail, sid, id;
	if (i == KP_NONE) return KP_NONE;
	kp_list_unlink(img->prev, img->next, &w->free_circles, i);
	lst = &w->spr.images[s];
	tail = kp_check_image(w, lst->tail);
	if (img->method[tail] == KP_METHOD_SHADOW) kp_list_insert_before(img->prev, img->next, lst, tail, i);
	else kp_list_insert_after(img->prev, img->next, lst, tail, i);
	sid = w->spr.id[s];
	id = (first + (int32_t)kp_images_dat(w, w->col.circle_image, sid)) & 0xffff;
	img->id[i] = id;
	img->grp[i] = kp_images_grp_of(w, id);
	img->sprite[i] = s;
	img->yoff[i] = kp_s8(kp_images_dat(w, w->col.circle_offset, sid));
	img->param[i] = color & 0xff;
	img->method[i] = KP_METHOD_SELECTION;
	kp_image_bounds(w, i);
	kp_image_set_header(w, i, (int32_t)kp_images_dat(w, w->col.iscript_id, id));
	kp_image_play(w, i, KP_ANIM_INIT);
	return i;
}
