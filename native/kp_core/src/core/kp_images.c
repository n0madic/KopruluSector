/* World, list helpers and image record operations (kp_images.h; game/images/world.lua,
 * core.lua and image.lua). */
#include "kp_images_internal.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const LO_COLUMNS[5] = { "attack_overlay", "damage_overlay", "special_overlay",
	"landing_dust_overlay", "lift_off_dust_overlay" };

void kp_images_fail(KpImages *w, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(w->error, sizeof w->error, fmt, ap);
	va_end(ap);
	kp_images_rethrow(w);
}

void kp_images_rethrow(KpImages *w)
{
	KpTry *t = w->try_;
	if (!t) {
		fprintf(stderr, "kp_images: unhandled failure: %s\n", w->error);
		abort();
	}
	longjmp(t->buf, 1);
}

int kp_images_hook_opt(KpImages *w, KpHook hook, int op, const int64_t *args, int argc, int64_t *out)
{
	return w->hook(w, w->ctx, hook, op, args, argc, out);
}

int64_t kp_images_hook(KpImages *w, KpHook hook, int op, const int64_t *args, int argc)
{
	int64_t out[2] = { 0, 0 };
	w->hook(w, w->ctx, hook, op, args, argc, out);
	return out[0];
}

int32_t kp_check_image(KpImages *w, int32_t i)
{
	if (i < 0 || i >= KP_IMAGES) kp_images_fail(w, "images: image record %d out of range", (int)i);
	return i;
}

int32_t kp_check_sprite(KpImages *w, int32_t s)
{
	if (s < 0 || s >= KP_SPRITES) kp_images_fail(w, "images: sprite record %d out of range", (int)s);
	return s;
}

uint32_t kp_images_dat(KpImages *w, const KpDatColumn *c, int32_t i)
{
	if (!c) kp_images_fail(w, "images: dat.kpd lacks a column the world reads");
	if (i < 0 || !kp_dat_has(c, (uint32_t)i)) kp_images_fail(w, "images: %s.%s has no entry %d", c->table, c->name, (int)i);
	return kp_dat_value(c, (uint32_t)i);
}

/* --- list helpers (game/images/core.lua) --- */

void kp_list_unlink(int32_t *prev, int32_t *next, KpList *list, int32_t node)
{
	if (list->head == node) list->head = next[node];
	if (list->tail == node) list->tail = prev[node];
	if (prev[node] != KP_NONE) next[prev[node]] = next[node];
	if (next[node] != KP_NONE) prev[next[node]] = prev[node];
	next[node] = KP_NONE;
	prev[node] = KP_NONE;
}

void kp_list_insert_after(int32_t *prev, int32_t *next, KpList *list, int32_t existing, int32_t node)
{
	if (list->tail == existing) list->tail = node;
	prev[node] = existing;
	next[node] = next[existing];
	if (next[existing] != KP_NONE) prev[next[existing]] = node;
	next[existing] = node;
}

void kp_list_insert_after_head(int32_t *prev, int32_t *next, KpList *list, int32_t node)
{
	if (list->head == KP_NONE) {
		list->head = node;
		list->tail = node;
		return;
	}
	kp_list_insert_after(prev, next, list, list->head, node);
}

void kp_list_insert_before(int32_t *prev, int32_t *next, KpList *list, int32_t existing, int32_t node)
{
	if (list->head == existing) list->head = node;
	prev[node] = prev[existing];
	next[node] = existing;
	if (prev[existing] != KP_NONE) next[prev[existing]] = node;
	prev[existing] = node;
}

/* --- world.lua --- */

static const KpDatColumn *column(const KpDat *dat, const char *table, const char *name)
{
	return dat ? kp_dat_column(dat, table, name) : NULL;
}

KpImages *kp_images_new(const KpDat *dat, const KpGrpMeta *grp, const KpIScript *script, KpRngGame *rng,
	KpError *err)
{
	KpImages *w = (KpImages *)calloc(1, sizeof *w);
	int k;
	if (!w) {
		snprintf(err->msg, sizeof err->msg, "images: out of memory");
		return NULL;
	}
	w->dat = dat;
	w->grp = grp;
	w->script = script;
	w->rng = rng;
	w->col.grp_file = column(dat, "images", "grp_file");
	w->col.gfx_turns = column(dat, "images", "gfx_turns");
	w->col.clickable = column(dat, "images", "clickable");
	w->col.draw_function = column(dat, "images", "draw_function");
	w->col.remapping = column(dat, "images", "remapping");
	w->col.use_full_iscript = column(dat, "images", "use_full_iscript");
	w->col.iscript_id = column(dat, "images", "iscript_id");
	w->col.draw_if_cloaked = column(dat, "images", "draw_if_cloaked");
	for (k = 0; k < 5; k++) w->col.lo[k] = column(dat, "images", LO_COLUMNS[k]);
	w->col.sprite_image = column(dat, "sprites", "image");
	w->col.sprite_is_visible = column(dat, "sprites", "is_visible");
	w->col.circle_image = column(dat, "sprites", "selection_circle_image");
	w->col.circle_offset = column(dat, "sprites", "selection_circle_offset");
	w->col.health_bar = column(dat, "sprites", "health_bar");
	w->col.placement_size = column(dat, "units", "staredit_placement_size");
	w->col.special_ability_flags = column(dat, "units", "special_ability_flags");
	w->col.shield_enabled = column(dat, "units", "shield_enabled");
	w->local_vision = 0xff;
	w->step_limit = -1;
	kp_images_set_map(w, 256, 256);
	kp_images_clear_records(w);
	return w;
}

void kp_images_free(KpImages *w) { free(w); }

void kp_images_set_map(KpImages *w, int32_t width, int32_t height)
{
	w->map_w = width;
	w->map_h = height;
	w->map_w_px = width * KP_TILE;
	w->map_h_px = height * KP_TILE;
}

static void fill(int32_t *a, int n, int32_t v)
{
	int i;
	for (i = 0; i < n; i++) a[i] = v;
}

static void clear_list(KpList *l) { l->head = l->tail = KP_NONE; }

void kp_images_clear_records(KpImages *w)
{
	int r;
	memset(&w->img, 0, sizeof w->img);
	fill(w->img.sprite, KP_IMAGES, KP_NONE);
	fill(w->img.prev, KP_IMAGES, KP_NONE);
	fill(w->img.next, KP_IMAGES, KP_NONE);
	memset(&w->spr, 0, sizeof w->spr);
	fill(w->spr.main, KP_SPRITES, KP_NONE);
	fill(w->spr.prev, KP_SPRITES, KP_NONE);
	fill(w->spr.next, KP_SPRITES, KP_NONE);
	for (r = 0; r < KP_SPRITES; r++) clear_list(&w->spr.images[r]);
	memset(&w->thg, 0, sizeof w->thg);
	fill(w->thg.sprite, KP_LONE, KP_NONE);
	fill(w->thg.prev, KP_LONE, KP_NONE);
	fill(w->thg.next, KP_LONE, KP_NONE);
	memset(&w->fog, 0, sizeof w->fog);
	fill(w->fog.sprite, KP_FOG, KP_NONE);
	fill(w->fog.prev, KP_FOG, KP_NONE);
	fill(w->fog.next, KP_FOG, KP_NONE);
	clear_list(&w->free_images);
	clear_list(&w->free_health);
	clear_list(&w->free_circles);
	clear_list(&w->free_ghosts);
	clear_list(&w->free_boxes);
	clear_list(&w->free_sprites);
	clear_list(&w->free_lone);
	clear_list(&w->active_lone);
	clear_list(&w->free_fog);
	clear_list(&w->active_fog);
	for (r = 0; r < KP_ROWS; r++) clear_list(&w->rows[r]);
	memset(w->has_vgrp, 0, sizeof w->has_vgrp);
	memset(w->vgrp_width, 0, sizeof w->vgrp_width);
	memset(w->vgrp_height, 0, sizeof w->vgrp_height);
	memset(w->vgrp_fw, 0, sizeof w->vgrp_fw);
	memset(w->vgrp_fh, 0, sizeof w->vgrp_fh);
	memset(w->overlay_slots, 0xff, sizeof w->overlay_slots);
	w->cursor = KP_NONE;
}

static void chain(KpImages *w, KpList *l, int32_t first, int32_t count)
{
	int32_t i;
	l->head = l->tail = first;
	for (i = first + 1; i < first + count; i++) kp_list_insert_after(w->img.prev, w->img.next, l, first, i);
}

void kp_images_init_pools(KpImages *w)
{
	int32_t i;
	kp_images_clear_records(w);
	chain(w, &w->free_images, 0, KP_IMG_MAIN);
	chain(w, &w->free_health, KP_IMG_HEALTH_FIRST, KP_IMG_HEALTH);
	chain(w, &w->free_circles, KP_IMG_CIRCLES_FIRST, KP_IMG_CIRCLES);
	chain(w, &w->free_ghosts, KP_IMG_GHOSTS_FIRST, KP_IMG_GHOSTS);
	chain(w, &w->free_boxes, KP_IMG_BOXES_FIRST, KP_IMG_BOXES);
	if (w->dat) kp_images_init_selection_records(w);
	w->free_sprites.head = w->free_sprites.tail = 0;
	for (i = 1; i < KP_SPRITES; i++) {
		w->spr.index[i] = i;
		kp_list_insert_after(w->spr.prev, w->spr.next, &w->free_sprites, 0, i);
	}
}

KpList *kp_images_free_list_of(KpImages *w, int32_t i)
{
	if (i < KP_IMG_HEALTH_FIRST) return &w->free_images;
	if (i < KP_IMG_CIRCLES_FIRST) return &w->free_health;
	if (i < KP_IMG_GHOSTS_FIRST) return &w->free_circles;
	if (i < KP_IMG_BOXES_FIRST) return &w->free_ghosts;
	return &w->free_boxes;
}

static uint32_t tile_flags(KpImages *w, int32_t tx, int32_t ty)
{
	uint32_t v = 0;
	if (!w->tile_flags || !w->tile_flags(w, w->ctx, ty * w->map_w + tx, &v)) return 0;
	return v;
}

int32_t kp_images_hidden_at(KpImages *w, int32_t tx, int32_t ty) { return (int32_t)(tile_flags(w, tx, ty) & 0xffu); }

int32_t kp_images_unexplored_at(KpImages *w, int32_t tx, int32_t ty)
{
	return (int32_t)((tile_flags(w, tx, ty) >> 8) & 0xffu);
}

int32_t kp_images_sprite_row(const KpImages *w, int32_t s)
{
	int32_t r = kp_idiv(kp_s16((uint32_t)w->spr.y[s]), KP_TILE);
	if (r < 0) return 0;
	if (r >= w->map_h) return w->map_h - 1;
	return r;
}

/* --- image.lua --- */

static void install_method_state(KpImages *w, int32_t i)
{
	if (w->img.method[i] == KP_METHOD_WARP_FIRE) {
		uint32_t p = (uint32_t)w->img.param[i];
		w->img.param[i] = (int32_t)((p & 0xffff0000u) + KP_WARP_FIRE_PARAM);
	}
}

int32_t kp_images_grp_of(KpImages *w, int32_t id)
{
	return (int32_t)kp_grpmeta_grp_of_tbl(w->grp, kp_images_dat(w, w->col.grp_file, id));
}

int32_t kp_images_grp_frames(KpImages *w, int32_t i)
{
	int32_t g = w->img.grp[i];
	if (g == KP_GRP_VIRTUAL) return 1;
	if (g <= 0 || (uint32_t)g > w->grp->grp_count) kp_images_fail(w, "images: image %d has no GRP", (int)i);
	return w->grp->grps[g].frames;
}

void kp_images_frame_rect(KpImages *w, int32_t i, int32_t frame, int32_t rect[6])
{
	int32_t g = w->img.grp[i];
	const KpGrpEntry *e;
	int x, y, fw, fh;
	if (frame < 0) frame = w->img.frame[i];
	if (g == KP_GRP_VIRTUAL) {
		rect[0] = rect[1] = 0;
		rect[2] = w->vgrp_fw[i];
		rect[3] = w->vgrp_fh[i];
		rect[4] = w->vgrp_width[i];
		rect[5] = w->vgrp_height[i];
		return;
	}
	if (g <= 0 || (uint32_t)g > w->grp->grp_count) kp_images_fail(w, "images: image %d has no GRP", (int)i);
	e = &w->grp->grps[g];
	if (frame >= e->frames) {
		kp_images_fail(w, "images: frame %d of image %d (id %d) beyond its %d frames", (int)frame, (int)i,
			(int)w->img.id[i], (int)e->frames);
	}
	kp_grpmeta_frame(w->grp, (uint32_t)g, (uint32_t)frame, &x, &y, &fw, &fh);
	rect[0] = x;
	rect[1] = y;
	rect[2] = fw;
	rect[3] = fh;
	rect[4] = e->width;
	rect[5] = e->height;
}

/* The LO entry of image id's overlay column `kind`, NULL for none. */
static const KpLoEntry *image_lo(KpImages *w, int32_t kind, int32_t id)
{
	uint32_t lo;
	if (kind < 0 || kind > 4) kp_images_fail(w, "images: image id %d has no LO of kind %d", (int)id, (int)kind);
	lo = kp_grpmeta_lo_of_tbl(w->grp, kp_images_dat(w, w->col.lo[kind], id));
	return lo ? &w->grp->los[lo] : NULL;
}

static void lo_point(KpImages *w, const KpLoEntry *lo, int32_t frame, int32_t point, int32_t *x, int32_t *y)
{
	int px, py;
	if (frame < 0 || !kp_grpmeta_lo_point(lo, (uint32_t)frame, point, &px, &py)) {
		kp_images_fail(w, "images: LO point %d of frame %d outside its file", (int)point, (int)frame);
	}
	*x = px;
	*y = py;
}

void kp_images_lo_offset(KpImages *w, int32_t i, int32_t kind, int32_t point, int32_t *x, int32_t *y)
{
	int32_t id = w->img.id[i];
	const KpLoEntry *lo = image_lo(w, kind, id);
	if (!lo) kp_images_fail(w, "images: image id %d has no LO of kind %d", (int)id, (int)kind);
	lo_point(w, lo, w->img.frame[i], point, x, y);
	if (w->img.flags[i] & KP_F_MIRROR) *x = -*x;
}

int32_t kp_images_damage_overlay_slots(KpImages *w, int32_t id)
{
	const KpLoEntry *lo;
	int32_t v = 0, count = 0, point;
	if (id >= 0 && id < 65536 && w->overlay_slots[id] >= 0) return w->overlay_slots[id];
	lo = image_lo(w, KP_LO_DAMAGE, id);
	if (lo) {
		for (point = 0; point < KP_DAMAGE_SLOTS; point++) {
			if ((uint32_t)point < lo->points) {
				int32_t x, y;
				lo_point(w, lo, 0, point, &x, &y);
				if (x != KP_LO_UNUSED || y != KP_LO_UNUSED) count++;
			}
		}
		v = (count * 2) & 0xff;
	}
	if (id >= 0 && id < 65536) w->overlay_slots[id] = (int16_t)v;
	return v;
}

/* Special-LO point 0 of the sprite's main image, x negated when the main image is mirrored
 * (0x40f280, 0x40f3a0, 0x410380). */
static void follow_point(KpImages *w, int32_t i, int32_t *x, int32_t *y)
{
	int32_t main = w->spr.main[kp_check_sprite(w, w->img.sprite[i])];
	const KpLoEntry *lo;
	kp_check_image(w, main);
	lo = image_lo(w, KP_LO_SPECIAL, w->img.id[main]);
	if (!lo) kp_images_fail(w, "images: image id %d has no special LO", (int)w->img.id[main]);
	lo_point(w, lo, w->img.frame[main], 0, x, y);
	if (w->img.flags[main] & KP_F_MIRROR) *x = kp_s8((uint32_t)-*x);
}

void kp_image_set_method(KpImages *w, int32_t i, int32_t method)
{
	w->img.method[i] = method;
	install_method_state(w, i);
}

void kp_image_set_header(KpImages *w, int32_t i, int32_t script_id)
{
	int32_t h = script_id < 0 ? KP_ISCRIPT_NO_HEADER : kp_iscript_header(w->script, (uint32_t)script_id);
	if (h == KP_ISCRIPT_NO_HEADER) kp_images_fail(w, "iscript: script %d does not exist", (int)script_id);
	w->img.header[i] = h;
}

void kp_image_init_record(KpImages *w, int32_t i, int32_t id, int32_t sprite, int32_t xoff, int32_t yoff)
{
	KpImageRecords *img = &w->img;
	int32_t fn;
	img->id[i] = id;
	img->grp[i] = kp_images_grp_of(w, id);
	img->flags[i] = (int32_t)((kp_images_dat(w, w->col.gfx_turns, id) & 1u) * KP_F_TURNS
		+ (kp_images_dat(w, w->col.clickable, id) & 1u) * KP_F_CLICKABLE);
	img->frameset[i] = img->frame[i] = img->dir[i] = 0;
	img->xoff[i] = kp_s8((uint32_t)xoff);
	img->yoff[i] = kp_s8((uint32_t)yoff);
	img->sprite[i] = sprite;
	img->param[i] = 0;
	img->header[i] = img->pc[i] = img->ret[i] = img->anim[i] = img->wait[i] = 0;
	fn = (int32_t)kp_images_dat(w, w->col.draw_function, id);
	if (fn == KP_METHOD_PLAYER_COLOR) img->param[i] = w->spr.owner[kp_check_sprite(w, sprite)];
	if (fn == KP_METHOD_REMAP) img->param[i] = (int32_t)kp_images_dat(w, w->col.remapping, id);
}

void kp_image_init_script(KpImages *w, int32_t i)
{
	KpImageRecords *img = &w->img;
	int32_t id = img->id[i];
	img->method[i] = (int32_t)kp_images_dat(w, w->col.draw_function, id);
	install_method_state(w, i);
	img->flags[i] = kp_flag(img->flags[i], KP_F_FULL_ISCRIPT, kp_images_dat(w, w->col.use_full_iscript, id) & 1u);
	kp_image_set_header(w, i, (int32_t)kp_images_dat(w, w->col.iscript_id, id));
}

void kp_image_init_drawing(KpImages *w, int32_t i)
{
	kp_image_init_script(w, i);
	kp_image_play(w, i, KP_ANIM_INIT);
	kp_image_bounds(w, i);
}

/* The animation's entry point: u16 at header + 8 + 2 * anim. */
int32_t kp_images_animation_pc(KpImages *w, int32_t i, int32_t anim)
{
	return (int32_t)kp_code_u16(w, (int64_t)w->img.header[i] + 8 + 2 * (int64_t)anim);
}

void kp_image_play(KpImages *w, int32_t i, int32_t anim)
{
	KpImageRecords *img = &w->img;
	int32_t cur;
	if (w->has_play_probe) {
		int64_t args[2];
		args[0] = i;
		args[1] = anim;
		w->hook(w, w->ctx, KP_HOOK_PLAY_PROBE, -1, args, 2, NULL);
	}
	cur = img->anim[i];
	if (anim == KP_ANIM_DEATH && cur == KP_ANIM_DEATH) return;
	if (!(img->flags[i] & KP_F_FULL_ISCRIPT) && anim != KP_ANIM_INIT && anim != KP_ANIM_DEATH) return;
	if (anim == cur && (anim == KP_ANIM_WALKING || anim == KP_ANIM_IS_WORKING)) return;
	if (anim == KP_ANIM_GND_ATTK_RPT) {
		if (cur != KP_ANIM_GND_ATTK_RPT && cur != KP_ANIM_GND_ATTK_INIT) anim = KP_ANIM_GND_ATTK_INIT;
	} else if (anim == KP_ANIM_AIR_ATTK_RPT) {
		if (cur != KP_ANIM_AIR_ATTK_RPT && cur != KP_ANIM_AIR_ATTK_INIT) anim = KP_ANIM_AIR_ATTK_INIT;
	}
	img->anim[i] = anim;
	img->pc[i] = kp_images_animation_pc(w, i, anim);
	img->wait[i] = img->ret[i] = 0;
	kp_images_run_iscript(w, i, NULL, 0, 0, NULL);
}

void kp_image_bounds(KpImages *w, int32_t i)
{
	KpImageRecords *img = &w->img;
	int32_t s = kp_check_sprite(w, img->sprite[i]);
	int32_t r[6], half_w, half_h, x;
	kp_images_frame_rect(w, i, -1, r);
	half_w = r[4] / 2;
	half_h = r[5] / 2;
	x = kp_s16((uint32_t)w->spr.x[s]) + img->xoff[i];
	if (img->flags[i] & KP_F_MIRROR) x = x + half_w - r[2] - r[0];
	else x = x + r[0] - half_w;
	img->x[i] = kp_s16((uint32_t)x);
	if (!(img->flags[i] & KP_F_HOLD_Y)) {
		img->y[i] = kp_s16((uint32_t)(kp_s16((uint32_t)w->spr.y[s]) + img->yoff[i] + r[1] - half_h));
	}
}

void kp_image_sync_frame(KpImages *w, int32_t i)
{
	int32_t f = (w->img.frameset[i] + w->img.dir[i]) & 0xffff;
	if (w->img.frame[i] != f) w->img.frame[i] = f;
}

void kp_image_set_frameset(KpImages *w, int32_t i, int32_t f)
{
	KpImageRecords *img = &w->img;
	int32_t frame;
	if (img->frameset[i] == f) return;
	img->frameset[i] = f & 0xffff;
	frame = (img->dir[i] + img->frameset[i]) & 0xffff;
	if (img->frame[i] != frame) img->frame[i] = frame;
}

static void set_offsets(KpImageRecords *img, int32_t i, int32_t x, int32_t y)
{
	if (img->xoff[i] != x || img->yoff[i] != y) {
		img->xoff[i] = x;
		img->yoff[i] = y;
	}
}

void kp_image_refresh_follow(KpImages *w, int32_t i)
{
	int32_t x, y;
	follow_point(w, i, &x, &y);
	set_offsets(&w->img, i, x, y);
}

void kp_image_set_direction(KpImages *w, int32_t i, int32_t d)
{
	KpImageRecords *img = &w->img;
	int32_t flags = img->flags[i], frame;
	int mirror;
	if (!(flags & KP_F_TURNS)) return;
	d &= 0xff;
	mirror = d > 16;
	if (mirror) d = (32 - d) & 0xff;
	if (((flags & KP_F_MIRROR) != 0) == mirror && img->dir[i] == d) return;
	img->dir[i] = d;
	img->flags[i] = kp_flag(flags, KP_F_MIRROR, (uint32_t)mirror);
	install_method_state(w, i);
	frame = (img->frameset[i] + d) & 0xffff;
	if (img->frame[i] != frame) img->frame[i] = frame;
	if (img->flags[i] & KP_F_FOLLOW) kp_image_refresh_follow(w, i);
}

void kp_image_direction_from_angle(KpImages *w, int32_t i, int32_t angle)
{
	KpImageRecords *img = &w->img;
	int32_t q, frame;
	int mirror;
	if (img->flags[i] & KP_F_FOLLOW) kp_image_refresh_follow(w, i);
	if (!(img->flags[i] & KP_F_TURNS)) return;
	q = ((angle & 0xff) + 4) / 8;
	mirror = q > 16;
	if (mirror) q = 32 - q;
	if (((img->flags[i] & KP_F_MIRROR) != 0) == mirror && img->dir[i] == q) return;
	img->dir[i] = q;
	img->flags[i] = kp_flag(img->flags[i], KP_F_MIRROR, (uint32_t)mirror);
	install_method_state(w, i);
	frame = (img->frameset[i] + q) & 0xffff;
	if (img->frame[i] != frame) img->frame[i] = frame;
}

void kp_image_set_mirror(KpImages *w, int32_t i, int32_t f)
{
	KpImageRecords *img = &w->img;
	int32_t cur = (img->flags[i] & KP_F_MIRROR) ? 1 : 0;
	if (cur == f) return;
	img->flags[i] = kp_flag(img->flags[i], KP_F_MIRROR, (uint32_t)(f & 1));
	install_method_state(w, i);
	if (img->flags[i] & KP_F_FOLLOW) kp_image_refresh_follow(w, i);
}

void kp_image_set_follow(KpImages *w, int32_t i, int32_t enable)
{
	KpImageRecords *img = &w->img;
	int32_t cur = (img->flags[i] & KP_F_FOLLOW) ? 1 : 0;
	if (cur == enable) return;
	img->flags[i] = kp_flag(img->flags[i], KP_F_FOLLOW, (uint32_t)(enable & 1));
	if (enable != 0) kp_image_refresh_follow(w, i);
}

void kp_image_set_offset(KpImages *w, int32_t i, int32_t x, int32_t y)
{
	set_offsets(&w->img, i, kp_s8((uint32_t)x), kp_s8((uint32_t)y));
}

void kp_image_hide(KpImages *w, int32_t i) { w->img.flags[i] |= KP_F_HIDDEN; }

void kp_image_show(KpImages *w, int32_t i) { w->img.flags[i] &= ~KP_F_HIDDEN; }

int32_t kp_image_alloc(KpImages *w)
{
	int32_t i = w->free_images.head;
	if (i == KP_NONE) return KP_NONE;
	kp_list_unlink(w->img.prev, w->img.next, &w->free_images, i);
	return i;
}

void kp_image_release(KpImages *w, int32_t i)
{
	KpImageRecords *img = &w->img;
	kp_list_unlink(img->prev, img->next, &w->spr.images[kp_check_sprite(w, img->sprite[i])], i);
	img->grp[i] = 0;
	kp_list_insert_after_head(img->prev, img->next, &w->free_images, i);
}

static void set_param_low(KpImageRecords *img, int32_t i, uint32_t low)
{
	img->param[i] = (int32_t)(((uint32_t)img->param[i] & 0xffff0000u) + low);
}

void kp_image_begin_cloak(KpImages *w, int32_t i)
{
	w->img.method[i] = KP_METHOD_CLOAK;
	set_param_low(&w->img, i, KP_CLOAK_START);
}

void kp_image_begin_decloak(KpImages *w, int32_t i)
{
	w->img.method[i] = w->img.method[i] < KP_METHOD_DECLOAK ? KP_METHOD_DECLOAK : KP_METHOD_DECLOAK_BLEND;
	set_param_low(&w->img, i, KP_DECLOAK_START);
}

void kp_image_copy_cloak(KpImages *w, int32_t i, int32_t source)
{
	int32_t m = w->img.method[source];
	if (m < 2 || m > 7) return;
	w->img.method[i] = m;
	set_param_low(&w->img, i, (uint32_t)w->img.param[source] & 0xffffu);
}

int32_t kp_image_create_relative(KpImages *w, int32_t ref, int32_t id, int32_t xoff, int32_t yoff, int before)
{
	KpImageRecords *img = &w->img;
	int32_t i = w->free_images.head, s;
	KpList *lst;
	if (i == KP_NONE) return KP_NONE;
	kp_list_unlink(img->prev, img->next, &w->free_images, i);
	s = kp_check_sprite(w, img->sprite[ref]);
	lst = &w->spr.images[s];
	if (before) kp_list_insert_before(img->prev, img->next, lst, ref, i);
	else kp_list_insert_after(img->prev, img->next, lst, ref, i);
	kp_image_init_record(w, i, id, s, xoff, yoff);
	kp_image_init_drawing(w, i);
	if (img->method[i] == 0 && w->has_unit) {
		int64_t u = w->unit;
		if ((kp_images_hook(w, KP_HOOK_UNIT_STATUS, -1, &u, 1) & KP_STATUS_HALLUCINATION) != 0
			&& (w->replay != 0 || kp_images_hook(w, KP_HOOK_UNIT_OWNER, -1, &u, 1) == w->local_player)) {
			img->method[i] = KP_METHOD_HALLUCINATION;
			img->param[i] = 0;
		}
	}
	if (img->flags[i] & KP_F_TURNS) {
		int32_t d = img->dir[ref];
		if (img->flags[ref] & KP_F_MIRROR) d = 32 - d;
		kp_image_set_direction(w, i, d);
	}
	kp_image_sync_frame(w, i);
	if (w->has_unit) {
		int64_t u = w->unit;
		if (kp_images_hook(w, KP_HOOK_UNIT_STATUS, -1, &u, 1) & KP_STATUS_CLOAKED) {
			if (kp_images_dat(w, w->col.draw_if_cloaked, id) == 0) {
				if (!(img->flags[i] & KP_F_HIDDEN)) {
					img->flags[i] += KP_F_HIDDEN;
					return i;
				}
			} else if (img->method[i] == 0) {
				kp_image_copy_cloak(w, i, kp_check_image(w, w->spr.main[s]));
			}
		}
	}
	return i;
}

void kp_image_update(KpImages *w, int32_t i)
{
	int32_t m = w->img.method[i];
	if (m == 2 || m == 5) kp_image_update_cloaking(w, i);
	else if (m == 4 || m == 7) kp_image_update_decloaking(w, i);
	else if (m == KP_METHOD_WARP_FIRE) kp_image_update_warp(w, i);
}

/* The parameter's value byte, timer byte and high WORD. */
#define PARAM_BYTES(img, i, value, timer, high) \
	uint32_t value = (uint32_t)(img)->param[i] & 0xffu, timer = ((uint32_t)(img)->param[i] >> 8) & 0xffu, \
		high = (uint32_t)(img)->param[i] & 0xffff0000u

void kp_image_update_cloaking(KpImages *w, int32_t i)
{
	KpImageRecords *img = &w->img;
	PARAM_BYTES(img, i, value, timer, high);
	if (timer != 0) {
		img->param[i] = (int32_t)(high + (timer - 1) * 256 + value);
		return;
	}
	value = (value + 1) & 0xffu;
	img->param[i] = (int32_t)(high + 3 * 256 + value);
	if (value >= 8) {
		kp_image_set_method(w, i, img->method[i] + 1);
		kp_images_unit_set_cloak_state(w, 3);
	}
}

void kp_image_update_decloaking(KpImages *w, int32_t i)
{
	KpImageRecords *img = &w->img;
	PARAM_BYTES(img, i, value, timer, high);
	if (timer != 0) {
		img->param[i] = (int32_t)(high + (timer - 1) * 256 + value);
		return;
	}
	img->param[i] = (int32_t)(high + 3 * 256 + ((value - 1) & 0xffu));
	if (value == 0) {
		kp_image_set_method(w, i, 0);
		kp_images_unit_set_cloak_state(w, 0);
		img->param[i] = (int32_t)(((uint32_t)img->param[i] & 0xffffff00u) + 0xffu);
	}
}

void kp_image_update_warp(KpImages *w, int32_t i)
{
	KpImageRecords *img = &w->img;
	PARAM_BYTES(img, i, value, timer, high);
	if (timer != 0) {
		img->param[i] = (int32_t)(high + (timer - 1) * 256 + value);
		return;
	}
	img->param[i] = (int32_t)(high + 2 * 256 + value);
	if (value < 63) {
		img->param[i] = (int32_t)((uint32_t)img->param[i] + 1);
		return;
	}
	if (img->anim[i] != KP_ANIM_DEATH) {
		img->anim[i] = KP_ANIM_DEATH;
		img->pc[i] = kp_images_animation_pc(w, i, KP_ANIM_DEATH);
		img->wait[i] = img->ret[i] = 0;
		kp_images_run_iscript(w, i, NULL, 0, 0, NULL);
	}
	kp_images_flingy_signal(w, 1, 0, -1);
}
