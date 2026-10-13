/*
 * The image, sprite and thingy state of the simulation and the IScript VM (stage N.1 of
 * ADR 0008): the C counterpart of game/images/{core,world,image,sprite,iscript,selection}.lua,
 * ported line by line. The Lua modules are the specification; comments name the original's
 * functions as they do.
 *
 * Records live in fixed arrays indexed by native record numbers, with the Lua world's key
 * space: images main 0..4999, health bars 5000..5011, selection circles 5012..5091, placement
 * ghosts 5092..5155, placement boxes 5156..5219; sprites 0..2499; lone thingies and fog ghosts
 * 0..499. KP_NONE (-1) is the null link. Every field is an int32_t holding exactly the number
 * the Lua world holds (the explicit wraps of the Lua code are kept), so the two can be compared
 * field by field.
 *
 * The world calls the unit, order and combat code through one dispatcher (KpImagesHooks):
 * until those move to C (stages N.2-N.5) the Lua binding implements it with the Lua hooks.
 * Errors where the Lua code raises one go through kp_images_fail, which jumps to the innermost
 * handler of the caller (kp_images_begin); the world is then in the partial state the Lua code leaves.
 */
#ifndef KP_IMAGES_H
#define KP_IMAGES_H

#include <setjmp.h>
#include <stdint.h>

#include "kp_dat.h"
#include "kp_grpmeta.h"
#include "kp_iscript.h"
#include "kp_kpf.h"
#include "kp_rng.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KP_NONE (-1)
#define KP_IMG_MAIN 5000
#define KP_IMG_HEALTH 12
#define KP_IMG_CIRCLES 80
#define KP_IMG_GHOSTS 64
#define KP_IMG_BOXES 64
#define KP_IMG_HEALTH_FIRST KP_IMG_MAIN
#define KP_IMG_CIRCLES_FIRST (KP_IMG_HEALTH_FIRST + KP_IMG_HEALTH)
#define KP_IMG_GHOSTS_FIRST (KP_IMG_CIRCLES_FIRST + KP_IMG_CIRCLES)
#define KP_IMG_BOXES_FIRST (KP_IMG_GHOSTS_FIRST + KP_IMG_GHOSTS)
#define KP_IMAGES (KP_IMG_BOXES_FIRST + KP_IMG_BOXES)
#define KP_SPRITES 2500
#define KP_LONE 500
#define KP_FOG 500
#define KP_ROWS 256
#define KP_TILE 32
#define KP_GRP_VIRTUAL (-1) /* image grp of the health-bar and box records */

/* Image flags (image +0x0c) and sprite flags (sprite +0x0e). */
#define KP_F_MIRROR 0x02
#define KP_F_HOLD_Y 0x04
#define KP_F_TURNS 0x08
#define KP_F_FULL_ISCRIPT 0x10
#define KP_F_CLICKABLE 0x20
#define KP_F_HIDDEN 0x40
#define KP_F_FOLLOW 0x80
#define KP_SF_CIRCLE 0x01
#define KP_SF_SELECTED 0x08
#define KP_SF_DEPTH 0x10
#define KP_SF_HIDDEN 0x20
#define KP_SF_NOBRK 0x80

typedef struct {
	int32_t head, tail;
} KpList;

/* The image record fields, in the order of the Lua world's IMAGE_FIELDS plus the links. */
#define KP_IMAGE_FIELDS(X) \
	X(id) X(method) X(dir) X(flags) X(xoff) X(yoff) X(header) X(pc) X(ret) X(anim) X(wait) \
	X(frameset) X(frame) X(x) X(y) X(grp) X(param) X(sprite) X(prev) X(next)
#define KP_SPRITE_FIELDS(X) \
	X(id) X(owner) X(selection) X(visibility) X(elevation) X(flags) X(sel_timer) X(index) \
	X(width) X(height) X(x) X(y) X(main) X(prev) X(next)
#define KP_THINGY_FIELDS(X) X(sprite) X(kind) X(prev) X(next)
#define KP_FIELD_ARRAY(name, count) int32_t name[count];
#define KP_IMAGE_FIELD(name) KP_FIELD_ARRAY(name, KP_IMAGES)
#define KP_SPRITE_FIELD(name) KP_FIELD_ARRAY(name, KP_SPRITES)
#define KP_LONE_FIELD(name) KP_FIELD_ARRAY(name, KP_LONE)
#define KP_FOG_FIELD(name) KP_FIELD_ARRAY(name, KP_FOG)

typedef struct {
	KP_IMAGE_FIELDS(KP_IMAGE_FIELD)
} KpImageRecords;

typedef struct {
	KP_SPRITE_FIELDS(KP_SPRITE_FIELD)
	KpList images[KP_SPRITES];
} KpSpriteRecords;

typedef struct {
	KP_THINGY_FIELDS(KP_LONE_FIELD)
} KpLoneRecords;

typedef struct {
	KP_THINGY_FIELDS(KP_FOG_FIELD)
} KpFogRecords;

/* Calls into the unit, order and combat code (game/images/iscript.lua's hook list). */
typedef enum {
	KP_HOOK_UNIT_STATUS, KP_HOOK_UNIT_OWNER, KP_HOOK_UNIT_TARGET, KP_HOOK_UNIT_SPRITE, KP_HOOK_UNIT_RESOURCES,
	KP_HOOK_UNIT_HEADING, KP_HOOK_UNIT_TYPE, KP_HOOK_SELECTION_UNIT, KP_HOOK_TURN_CCW, KP_HOOK_TURN_CW,
	KP_HOOK_SET_SPAWN_FRAME, KP_HOOK_ATTACK_WITH, KP_HOOK_ORDER_WEAPON, KP_HOOK_SPELL_TARGET_INVALID,
	KP_HOOK_FIRE_WEAPON, KP_HOOK_USE_WEAPON, KP_HOOK_MOVE_SPEED, KP_HOOK_SET_MOVE_SPEED,
	KP_HOOK_CLEAR_REPEAT_ATTACK, KP_HOOK_NOBRK_START, KP_HOOK_NOBRK_END, KP_HOOK_IGNORE_REST_IDLE,
	KP_HOOK_SET_FORWARD_OFFSET, KP_HOOK_SET_FLINGY_DIRECTION, KP_HOOK_SET_TOP_SPEED, KP_HOOK_TARGET_POINT,
	KP_HOOK_WITHIN_DISTANCE, KP_HOOK_TARGET_HEADING, KP_HOOK_ANGLE_DIFF, KP_HOOK_ATTACK_MELEE,
	KP_HOOK_MISSILE_DAMAGE, KP_HOOK_SPROL_GOLIATH, KP_HOOK_ORDER_SIGNAL, KP_HOOK_SET_CLOAK_STATE,
	KP_HOOK_UNIT_FITS, KP_HOOK_CREATE_UNSTABLE_FLINGY,
	KP_HOOK_HIDE_ORDER_MARKER, /* optional: a missing one is a no-op */
	KP_HOOK_SOUND, /* the world's sound callback (id, x, y); optional */
	KP_HOOK_PLAY_PROBE, /* (image, animation) first in kp_image_play when has_play_probe (tests) */
	KP_HOOK_COUNT
} KpHook;

/* What a hook returns: nothing, an integer, an integer or nothing (nil), a truth value, or
 * a point (two integers). */
typedef enum { KP_RESULT_NONE, KP_RESULT_INT, KP_RESULT_OPTIONAL, KP_RESULT_BOOL, KP_RESULT_POINT } KpHookResult;

typedef struct KpImages KpImages;

/* Calls hook `hook` (opcode `op` of the calling IScript instruction, -1 outside the VM) with
 * `argc` arguments. Fills out[0] (and out[1] for a point) and returns 1, or returns 0 for an
 * optional result that is absent (nil). Failures go through kp_images_fail. */
typedef int (*KpHookCall)(KpImages *w, void *ctx, KpHook hook, int op, const int64_t *args, int argc,
	int64_t *out);

/* The tile flag DWORD at index ty * width + tx (0x666454); returns 0 for an index the
 * simulation's tile table lacks. */
typedef int (*KpTileFlags)(KpImages *w, void *ctx, int32_t index, uint32_t *flags);

typedef struct KpTry {
	jmp_buf buf;
	struct KpTry *outer;
} KpTry;

struct KpImages {
	/* Data (not owned) and the dat columns the world reads. */
	const KpDat *dat;
	const KpGrpMeta *grp;
	const KpIScript *script;
	KpRngGame *rng;
	struct {
		const KpDatColumn *grp_file, *gfx_turns, *clickable, *draw_function, *remapping, *use_full_iscript,
			*iscript_id, *draw_if_cloaked, *lo[5];
		const KpDatColumn *sprite_image, *sprite_is_visible, *circle_image, *circle_offset, *health_bar;
		const KpDatColumn *placement_size, *special_ability_flags, *shield_enabled;
	} col;
	KpHookCall hook;
	KpTileFlags tile_flags;
	void *tile_ctx; /* the context of tile_flags */
	void *ctx;

	/* Settings (game/images/world.lua M.new opts). */
	int32_t map_w, map_h, map_w_px, map_h_px;
	int32_t tileset, expansion, local_player, replay, replay_reveal, local_vision;
	int32_t step_limit; /* -1: none (harness guard against scripts looping without a wait) */
	int has_sound; /* the hook dispatcher implements KP_HOOK_SOUND */
	int has_play_probe; /* ... and KP_HOOK_PLAY_PROBE */

	/* IScript contexts 0x543300/0x543304/0x543308; has_* 0 for none. */
	int32_t unit, flingy, bullet;
	int has_unit, has_flingy, has_bullet;

	KpImageRecords img;
	KpSpriteRecords spr;
	KpLoneRecords thg;
	KpFogRecords fog;
	/* Virtual GRP headers of the health-bar and box records (has_vgrp marks them). */
	int32_t vgrp_width[KP_IMAGES], vgrp_height[KP_IMAGES], vgrp_fw[KP_IMAGES], vgrp_fh[KP_IMAGES];
	uint8_t has_vgrp[KP_IMAGES];
	KpList free_images, free_health, free_circles, free_ghosts, free_boxes;
	KpList free_sprites, rows[KP_ROWS];
	KpList free_lone, active_lone, free_fog, active_fog;
	int32_t cursor;
	int32_t circle_colors[256]; /* 0x52904a by owner; has_circle_colors 0 until set */
	int has_circle_colors;
	int16_t overlay_slots[65536]; /* damage_overlay_slots cache, -1 unknown */

	KpTry *try_;
	char error[KP_ERROR_SIZE];
};

/* The world allocated with every record zeroed and every list empty (clear_records); NULL
 * when out of memory. The data must outlive it. */
KpImages *kp_images_new(const KpDat *dat, const KpGrpMeta *grp, const KpIScript *script, KpRngGame *rng,
	KpError *err);
void kp_images_free(KpImages *w);
void kp_images_set_map(KpImages *w, int32_t width, int32_t height);

/* Error handling. A caller pushes a handler and calls setjmp in its own frame:
 *   KpTry t;
 *   kp_images_begin(w, &t);
 *   if (setjmp(t.buf)) { kp_images_end(w, &t); ... the message is in w->error ... }
 *   ... calls into the world ...
 *   kp_images_end(w, &t);
 * A failure with no handler aborts. */
static inline void kp_images_begin(KpImages *w, KpTry *t)
{
	t->outer = w->try_;
	w->try_ = t;
}
static inline void kp_images_end(KpImages *w, KpTry *t) { w->try_ = t->outer; }
#if defined(__GNUC__) || defined(__clang__)
__attribute__((noreturn, format(printf, 2, 3)))
#endif
void kp_images_fail(KpImages *w, const char *fmt, ...);
/* Fails with the message already in w->error. */
#if defined(__GNUC__) || defined(__clang__)
__attribute__((noreturn))
#endif
void kp_images_rethrow(KpImages *w);

/* Hook calls: the integer result (a missing optional one fails), the optional one
 * (returns 0 when absent), the truth value. */
int64_t kp_images_hook(KpImages *w, KpHook hook, int op, const int64_t *args, int argc);
int kp_images_hook_opt(KpImages *w, KpHook hook, int op, const int64_t *args, int argc, int64_t *out);

/* List helpers 0x4119a0/0x411900/0x411930/0x411970 over one record space's links. */
void kp_list_unlink(int32_t *prev, int32_t *next, KpList *list, int32_t node);
void kp_list_insert_after(int32_t *prev, int32_t *next, KpList *list, int32_t existing, int32_t node);
void kp_list_insert_after_head(int32_t *prev, int32_t *next, KpList *list, int32_t node);
void kp_list_insert_before(int32_t *prev, int32_t *next, KpList *list, int32_t existing, int32_t node);

/* world.lua */
void kp_images_clear_records(KpImages *w);
void kp_images_init_pools(KpImages *w);
KpList *kp_images_free_list_of(KpImages *w, int32_t i);
int32_t kp_images_hidden_at(KpImages *w, int32_t tx, int32_t ty);
int32_t kp_images_unexplored_at(KpImages *w, int32_t tx, int32_t ty);
int32_t kp_images_sprite_row(const KpImages *w, int32_t s);

/* image.lua */
int32_t kp_images_grp_of(KpImages *w, int32_t id);
int32_t kp_images_grp_frames(KpImages *w, int32_t i);
/* frame < 0: the image's current frame. */
void kp_images_frame_rect(KpImages *w, int32_t i, int32_t frame, int32_t rect[6]);
void kp_images_lo_offset(KpImages *w, int32_t i, int32_t kind, int32_t point, int32_t *x, int32_t *y);
int32_t kp_images_damage_overlay_slots(KpImages *w, int32_t id);
void kp_image_set_method(KpImages *w, int32_t i, int32_t method);
void kp_image_set_header(KpImages *w, int32_t i, int32_t script_id);
void kp_image_init_record(KpImages *w, int32_t i, int32_t id, int32_t sprite, int32_t xoff, int32_t yoff);
void kp_image_init_script(KpImages *w, int32_t i);
void kp_image_init_drawing(KpImages *w, int32_t i);
void kp_image_play(KpImages *w, int32_t i, int32_t anim);
void kp_image_bounds(KpImages *w, int32_t i);
void kp_image_sync_frame(KpImages *w, int32_t i);
void kp_image_set_frameset(KpImages *w, int32_t i, int32_t f);
void kp_image_refresh_follow(KpImages *w, int32_t i);
void kp_image_set_direction(KpImages *w, int32_t i, int32_t d);
void kp_image_direction_from_angle(KpImages *w, int32_t i, int32_t angle);
void kp_image_set_mirror(KpImages *w, int32_t i, int32_t f);
void kp_image_set_follow(KpImages *w, int32_t i, int32_t enable);
void kp_image_set_offset(KpImages *w, int32_t i, int32_t x, int32_t y);
void kp_image_hide(KpImages *w, int32_t i);
void kp_image_show(KpImages *w, int32_t i);
int32_t kp_image_alloc(KpImages *w);
void kp_image_release(KpImages *w, int32_t i);
void kp_image_begin_cloak(KpImages *w, int32_t i);
void kp_image_begin_decloak(KpImages *w, int32_t i);
void kp_image_copy_cloak(KpImages *w, int32_t i, int32_t source);
int32_t kp_image_create_relative(KpImages *w, int32_t ref, int32_t id, int32_t xoff, int32_t yoff, int before);
void kp_image_update(KpImages *w, int32_t i);
void kp_image_update_cloaking(KpImages *w, int32_t i);
void kp_image_update_decloaking(KpImages *w, int32_t i);
void kp_image_update_warp(KpImages *w, int32_t i);

/* iscript.lua. A separate script state (walking-speed sampling) runs instead of the
 * image's own when `state` is given. */
typedef struct {
	int32_t pc, ret, wait, header;
} KpScriptState;
void kp_images_flingy_signal(KpImages *w, int32_t set_mask, int32_t clear_mask, int op);
void kp_images_unit_set_cloak_state(KpImages *w, int32_t n);
void kp_image_sound(KpImages *w, int32_t i, int32_t id);
/* Returns 1 with the speed `move` wrote in *result when `out` and a move ran, else 0. */
int kp_images_run_iscript(KpImages *w, int32_t i, KpScriptState *state, int noop, int out, int64_t *result);

/* sprite.lua */
int32_t kp_sprite_image_above_main(KpImages *w, int32_t s, int32_t id, int32_t xoff, int32_t yoff, int32_t dir);
int32_t kp_sprite_image_on_top(KpImages *w, int32_t s, int32_t id, int32_t xoff, int32_t yoff, int32_t dir);
int32_t kp_sprite_image_below_main(KpImages *w, int32_t s, int32_t id, int32_t xoff, int32_t yoff, int32_t dir);
int32_t kp_sprite_image_copy_on_top(KpImages *w, int32_t s, int32_t id, int32_t xoff, int32_t yoff, int32_t dir);
int32_t kp_sprite_clone(KpImages *w, int32_t src);
void kp_sprite_take_main(KpImages *w, int32_t dst, int32_t src);
void kp_sprite_set_visibility(KpImages *w, int32_t s, int32_t mask);
int kp_sprite_init(KpImages *w, int32_t s, int32_t id, int32_t x, int32_t y, int32_t owner);
int32_t kp_sprite_create(KpImages *w, int32_t id, int32_t x, int32_t y, int32_t owner);
void kp_sprite_destroy(KpImages *w, int32_t s);
void kp_sprite_move(KpImages *w, int32_t s, int32_t x, int32_t y);
void kp_sprite_update(KpImages *w, int32_t s);
void kp_sprite_play(KpImages *w, int32_t s, int32_t anim, int force);
void kp_sprite_set_direction(KpImages *w, int32_t s, int32_t d);
void kp_sprite_set_facing(KpImages *w, int32_t s, int32_t heading);
void kp_sprite_refresh_bounds(KpImages *w, int32_t s);
void kp_images_refresh_bounds(KpImages *w);
uint32_t kp_sprite_depth_key(const KpImages *w, int32_t s);
void kp_images_init_thingies(KpImages *w);
int32_t kp_lone_create(KpImages *w, int32_t id, int32_t x, int32_t y, int32_t owner);
void kp_thingy_visibility(KpImages *w, int32_t t);
void kp_thingy_visibility_explored(KpImages *w, int32_t t);
void kp_sprite_hide_uncloakable(KpImages *w, int32_t s);
void kp_sprite_begin_cloak(KpImages *w, int32_t s);
void kp_sprite_set_cloak_mode(KpImages *w, int32_t s, int32_t mode, int32_t p0, int32_t p1);
void kp_sprite_cloak_detected(KpImages *w, int32_t s);
void kp_sprite_cloak_undetected(KpImages *w, int32_t s);
int kp_sprite_visibility_rect(KpImages *w, int32_t s, int32_t width_px, int32_t height_px);
int kp_thingy_visibility_rect(KpImages *w, int32_t t, int32_t width_px, int32_t height_px);
void kp_lone_update(KpImages *w, int32_t t);
int32_t kp_fog_create(KpImages *w, int32_t unit_type, int32_t src);
int32_t kp_fog_at_point(KpImages *w, int32_t x, int32_t y);
void kp_sprite_flash(KpImages *w, int32_t s, int32_t color);
void kp_fog_update(KpImages *w, int32_t t);
void kp_images_update_thingies(KpImages *w);
int32_t kp_image_spawn_sprite(KpImages *w, int32_t i, int32_t id, int32_t dx, int32_t dy, int32_t elevation);

/* selection.lua */
void kp_images_init_selection_records(KpImages *w);
int32_t kp_health_bar_create(KpImages *w, int32_t s);
void kp_health_bar_release(KpImages *w, int32_t i);
void kp_selection_circle_release(KpImages *w, int32_t i);
void kp_sprite_remove_circle(KpImages *w, int32_t s);
void kp_sprite_add_circle(KpImages *w, int32_t s, int32_t color);
void kp_sprite_remove_health_bar(KpImages *w, int32_t s);
int32_t kp_selection_circle_create(KpImages *w, int32_t s, int32_t color, int32_t first);

#ifdef __cplusplus
}
#endif

#endif
