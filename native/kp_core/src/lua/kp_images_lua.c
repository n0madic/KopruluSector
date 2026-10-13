/*
 * kp_core.images: the C world of kp_images.h behind a Lua API (stage N.1 of ADR 0008), for
 * worlds without a game (game/images/native.lua) and a native game's world (g.img):
 *   - the world is a userdata with every World method and the world's fields;
 *   - w.img, w.spr, w.thg and w.fog are tables of field arrays; each array is a proxy
 *     over the C records (index and assign like the Lua arrays);
 *   - the free and active lists, the sprite image lists and the rows are proxies with head
 *     and tail; w.vgrp[i] is the virtual GRP header of a health-bar or box record or nil;
 *   - the hooks are the Lua functions of w.hooks, called with the world first; w.sound,
 *     w.tile_flags and w.play_probe are read from the world's fields;
 *   - other fields Lua code stores on the world live in the world's environment table.
 * Every call into the C world runs under a failure handler: a C failure or an error raised by
 * a hook comes back to the method and is raised as a Lua error with the same value.
 *
 *   kp_core.images.data(dat, grp, iscript) -> data: the C copies of the Lua data tables, in the
 *       shapes game/data/dat.lua, grpmeta.lua and scripts.lua (parse_iscript) return; synthetic
 *       tables of the same shape work too (unit tests). The DAT columns come from dat.layout.
 *   kp_core.images.new(data, opts) -> world (opts: map_width, map_height, tileset, expansion,
 *       local_player, replay, replay_reveal, local_vision; opts.rng must be a kp_core.rng.game
 *       generator, shared with the caller)
 */
#include "kp_lua_bridge.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../core/kp_images.h"
#include "../core/kp_int.h"

#define DATA_META "kp_core.images.data"
#define WORLD_META "kp_core.images.world"
#define VGRP_META "kp_core.images.vgrp"
#define VGRP_ENTRY_META "kp_core.images.vgrp_entry"

typedef struct {
	KpDat dat;
	KpGrpMeta grp;
	KpIScript script;
} Data;

typedef struct {
	KpImages *w;
	lua_State *L;
	int lua_error; /* the failure is a Lua error value left on the stack */
	int owned; /* the userdata frees the world (a native game's world belongs to the game) */
	int32_t *draw; /* draw_collect's buffer, allocated on first use */
} World;

typedef struct {
	World *world;
	int32_t i;
} VgrpEntry;

/* --- values --- */


static int32_t arg_int(World *lw, lua_State *L, int k)
{
	if (!lua_isnumber(L, k) || !kp_integral(lua_tonumber(L, k))) {
		kp_images_fail(lw->w, "images: argument %d is not a 32-bit integer", k - 1);
	}
	return kp_to_i32(lua_tonumber(L, k));
}

static int32_t arg_record(World *lw, lua_State *L, int k, int32_t count, const char *what)
{
	int32_t v = arg_int(lw, L, k);
	if (v < 0 || v >= count) kp_images_fail(lw->w, "images: %s record %d out of range", what, (int)v);
	return v;
}

#define IMG(k) arg_record(lw, L, k, KP_IMAGES, "image")
#define SPR(k) arg_record(lw, L, k, KP_SPRITES, "sprite")
#define LONE(k) arg_record(lw, L, k, KP_LONE, "lone thingy")
#define FOG(k) arg_record(lw, L, k, KP_FOG, "fog ghost")
#define INT(k) arg_int(lw, L, k)
#define BOOL(k) lua_toboolean(L, k)

static World *check_world(lua_State *L, int k) { return (World *)luaL_checkudata(L, k, WORLD_META); }

static int32_t *vgrp_field(KpImages *w, const char *k, int32_t i)
{
	if (!k) return NULL;
	if (strcmp(k, "width") == 0) return &w->vgrp_width[i];
	if (strcmp(k, "height") == 0) return &w->vgrp_height[i];
	if (strcmp(k, "fw") == 0) return &w->vgrp_fw[i];
	if (strcmp(k, "fh") == 0) return &w->vgrp_fh[i];
	return NULL;
}

static int vgrp_entry_index(lua_State *L)
{
	VgrpEntry *e = (VgrpEntry *)luaL_checkudata(L, 1, VGRP_ENTRY_META);
	int32_t *f = vgrp_field(e->world->w, lua_tostring(L, 2), e->i);
	if (!f) {
		lua_pushnil(L);
		return 1;
	}
	return kp_push_int(L, *f);
}

static int vgrp_entry_newindex(lua_State *L)
{
	VgrpEntry *e = (VgrpEntry *)luaL_checkudata(L, 1, VGRP_ENTRY_META);
	int32_t *f = vgrp_field(e->world->w, lua_tostring(L, 2), e->i);
	lua_Number v = lua_tonumber(L, 3);
	if (!f) return luaL_error(L, "images: a virtual GRP header has width, height, fw and fh");
	if (lua_type(L, 3) != LUA_TNUMBER || !kp_integral(v)) return luaL_error(L, "images: GRP sizes are integers");
	*f = kp_to_i32(v);
	return 0;
}

static int vgrp_index(lua_State *L)
{
	World *lw;
	lua_Number k = lua_tonumber(L, 2);
	VgrpEntry *e;
	luaL_checkudata(L, 1, VGRP_META);
	kp_bridge_push_owner(L, 1);
	lw = check_world(L, -1);
	if (lua_type(L, 2) != LUA_TNUMBER || k != floor(k) || k < 0 || k >= KP_IMAGES || !lw->w->has_vgrp[(int32_t)k]) {
		lua_pushnil(L);
		return 1;
	}
	e = (VgrpEntry *)kp_bridge_proxy(L, sizeof *e, VGRP_ENTRY_META, lua_gettop(L));
	e->world = lw;
	e->i = (int32_t)k;
	return 1;
}

static int vgrp_newindex(lua_State *L)
{
	World *lw;
	lua_Number k = lua_tonumber(L, 2);
	int32_t i;
	luaL_checkudata(L, 1, VGRP_META);
	kp_bridge_push_owner(L, 1);
	lw = check_world(L, -1);
	if (lua_type(L, 2) != LUA_TNUMBER || k != floor(k) || k < 0 || k >= KP_IMAGES) {
		return luaL_error(L, "images: virtual GRP header outside the image records");
	}
	i = (int32_t)k;
	if (lua_isnil(L, 3)) {
		lw->w->has_vgrp[i] = 0;
		return 0;
	}
	luaL_checktype(L, 3, LUA_TTABLE);
	lw->w->has_vgrp[i] = 1;
	lua_getfield(L, 3, "width");
	lw->w->vgrp_width[i] = kp_to_i32(lua_tonumber(L, -1));
	lua_getfield(L, 3, "height");
	lw->w->vgrp_height[i] = kp_to_i32(lua_tonumber(L, -1));
	lua_getfield(L, 3, "fw");
	lw->w->vgrp_fw[i] = kp_to_i32(lua_tonumber(L, -1));
	lua_getfield(L, 3, "fh");
	lw->w->vgrp_fh[i] = kp_to_i32(lua_tonumber(L, -1));
	return 0;
}

/* --- hooks --- */


static void hook_value(World *lw, lua_State *L, int k, KpHook hook, int64_t *out)
{
	if (lua_type(L, k) != LUA_TNUMBER || !kp_integral(lua_tonumber(L, k))) {
		kp_images_fail(lw->w, "images: hook %s returned a %s, not an integer", kp_hook_name(hook), luaL_typename(L, k));
	}
	*out = (int64_t)lua_tonumber(L, k);
}

/* The environment table of the world at stack index k. */
static void push_env(lua_State *L, int k) { lua_getfenv(L, k); }

#define WORLDS "kp_core.images.worlds" /* registry: weak table, light userdata World* -> world */

/* Pushes the world userdata of lw: the hooks run while a method of the world or of a game
 * that drives it is active, with either at stack index 1. */
static void push_world(lua_State *L, World *lw)
{
	lua_getfield(L, LUA_REGISTRYINDEX, WORLDS);
	lua_pushlightuserdata(L, lw);
	lua_rawget(L, -2);
	lua_remove(L, -2);
}

int kp_images_lua_hook(KpImages *w, void *ctx, KpHook hook, int op, const int64_t *args, int argc, int64_t *out)
{
	World *lw = (World *)ctx;
	lua_State *L = lw->L;
	int top = lua_gettop(L), results, i, present = 1, world;
	KpHookResult kind = kp_hook_result(hook);
	push_world(L, lw);
	world = lua_gettop(L);
	push_env(L, world);
	if (hook == KP_HOOK_SOUND) {
		lua_getfield(L, -1, "sound");
		if (lua_isnil(L, -1)) {
			lua_settop(L, top);
			return 0;
		}
	} else if (hook == KP_HOOK_PLAY_PROBE) {
		lua_getfield(L, -1, "play_probe");
		if (lua_isnil(L, -1)) {
			lua_settop(L, top);
			return 0;
		}
		lua_pushvalue(L, world);
	} else {
		lua_getfield(L, -1, "hooks");
		if (lua_istable(L, -1)) lua_getfield(L, -1, kp_hook_name(hook));
		else lua_pushnil(L);
		if (lua_isnil(L, -1)) {
			lua_settop(L, top);
			if (hook == KP_HOOK_HIDE_ORDER_MARKER) return 0;
			if (op >= 0) kp_images_fail(w, "iscript: opcode 0x%02x needs hook %s", op, kp_hook_name(hook));
			kp_images_fail(w, "images: needs hook %s", kp_hook_name(hook));
		}
		lua_pushvalue(L, world);
	}
	for (i = 0; i < argc; i++) lua_pushnumber(L, (lua_Number)args[i]);
	results = kind == KP_RESULT_POINT ? 2 : kind == KP_RESULT_NONE ? 0 : 1;
	{
		/* A Lua error raised directly inside a nested world method (not through a failure
		 * handler) unwinds past that method's handler: restore ours before going on. */
		KpTry *handler = w->try_;
		lua_State *state = lw->L;
		int failed = lua_pcall(L, argc + (hook == KP_HOOK_SOUND ? 0 : 1), results, 0) != 0;
		w->try_ = handler;
		lw->L = state;
		if (failed) {
			lw->lua_error = 1;
			kp_images_rethrow(w);
		}
	}
	switch (kind) {
	case KP_RESULT_NONE:
		break;
	case KP_RESULT_INT:
		hook_value(lw, L, -1, hook, &out[0]);
		break;
	case KP_RESULT_OPTIONAL:
		if (lua_isnil(L, -1)) present = 0;
		else hook_value(lw, L, -1, hook, &out[0]);
		break;
	case KP_RESULT_BOOL:
		out[0] = lua_toboolean(L, -1);
		break;
	case KP_RESULT_POINT:
		hook_value(lw, L, -2, hook, &out[0]);
		hook_value(lw, L, -1, hook, &out[1]);
		break;
	}
	lua_settop(L, top);
	return present;
}

static int read_tile_flags(KpImages *w, void *ctx, int32_t index, uint32_t *flags)
{
	World *lw = (World *)ctx;
	lua_State *L = lw->L;
	int top = lua_gettop(L), present = 0;
	(void)w;
	push_world(L, lw);
	push_env(L, -1);
	lua_getfield(L, -1, "tile_flags");
	if (lua_istable(L, -1)) {
		lua_rawgeti(L, -1, index);
		if (lua_type(L, -1) == LUA_TNUMBER) {
			*flags = (uint32_t)kp_to_i32(lua_tonumber(L, -1));
			present = 1;
		}
	}
	lua_settop(L, top);
	return present;
}

/* --- methods --- */

typedef int (*Method)(lua_State *L, World *lw, KpImages *w);

static int m_hook(lua_State *L, World *lw, KpImages *w)
{
	const char *name = lua_tostring(L, 2);
	(void)lw;
	push_env(L, 1);
	lua_getfield(L, -1, "hooks");
	if (lua_istable(L, -1) && name) lua_getfield(L, -1, name);
	else lua_pushnil(L);
	if (lua_isnil(L, -1)) {
		if (!lua_isnoneornil(L, 3)) kp_images_fail(w, "iscript: opcode 0x%02x needs hook %s", (int)lua_tonumber(L, 3), name);
		kp_images_fail(w, "images: needs hook %s", name ? name : "?");
	}
	return 1;
}

static int m_flingy_signal(lua_State *L, World *lw, KpImages *w)
{
	kp_images_flingy_signal(w, INT(2), INT(3), lua_isnoneornil(L, 4) ? -1 : INT(4));
	return 0;
}

static int m_unit_set_cloak_state(lua_State *L, World *lw, KpImages *w)
{
	kp_images_unit_set_cloak_state(w, INT(2));
	return 0;
}

static int m_image_sound(lua_State *L, World *lw, KpImages *w)
{
	kp_image_sound(w, IMG(2), INT(3));
	return 0;
}

static int32_t state_field(World *lw, lua_State *L, const char *key)
{
	int32_t v;
	lua_getfield(L, 3, key);
	v = lua_isnil(L, -1) ? 0 : arg_int(lw, L, lua_gettop(L));
	lua_pop(L, 1);
	return v;
}

static int m_run_iscript(lua_State *L, World *lw, KpImages *w)
{
	int32_t i = IMG(2);
	KpScriptState state, before;
	int64_t result = 0;
	int has_result;
	if (lua_isnoneornil(L, 3)) {
		has_result = kp_images_run_iscript(w, i, NULL, BOOL(4), BOOL(5), &result);
	} else {
		if (!lua_istable(L, 3)) kp_images_fail(w, "images: run_iscript state is not a table");
		state.pc = state_field(lw, L, "pc");
		state.ret = state_field(lw, L, "ret");
		state.wait = state_field(lw, L, "wait");
		state.header = state_field(lw, L, "header");
		before = state;
		has_result = kp_images_run_iscript(w, i, &state, BOOL(4), BOOL(5), &result);
		if (state.pc != before.pc) {
			kp_push_int(L, state.pc);
			lua_setfield(L, 3, "pc");
		}
		if (state.ret != before.ret) {
			kp_push_int(L, state.ret);
			lua_setfield(L, 3, "ret");
		}
		if (state.wait != before.wait) {
			kp_push_int(L, state.wait);
			lua_setfield(L, 3, "wait");
		}
	}
	if (!has_result) return 0;
	return kp_push_int(L, result);
}

static int m_grp_of(lua_State *L, World *lw, KpImages *w) { return kp_push_int(L, kp_images_grp_of(w, INT(2))); }
static int m_grp_frames(lua_State *L, World *lw, KpImages *w) { return kp_push_int(L, kp_images_grp_frames(w, IMG(2))); }

static int m_frame_rect(lua_State *L, World *lw, KpImages *w)
{
	int32_t r[6], k;
	kp_images_frame_rect(w, IMG(2), lua_isnoneornil(L, 3) ? -1 : INT(3), r);
	for (k = 0; k < 6; k++) kp_push_int(L, r[k]);
	return 6;
}

static int m_lo_offset(lua_State *L, World *lw, KpImages *w)
{
	int32_t x, y;
	kp_images_lo_offset(w, IMG(2), INT(3), INT(4), &x, &y);
	kp_push_int(L, x);
	return kp_push_int(L, y) + 1;
}

static int m_damage_overlay_slots(lua_State *L, World *lw, KpImages *w)
{
	return kp_push_int(L, kp_images_damage_overlay_slots(w, INT(2)));
}

#define VOID_I(name, fn) \
	static int name(lua_State *L, World *lw, KpImages *w) \
	{ \
		fn(w, IMG(2)); \
		return 0; \
	}
#define VOID_II(name, fn) \
	static int name(lua_State *L, World *lw, KpImages *w) \
	{ \
		fn(w, IMG(2), INT(3)); \
		return 0; \
	}
#define VOID_S(name, fn) \
	static int name(lua_State *L, World *lw, KpImages *w) \
	{ \
		fn(w, SPR(2)); \
		return 0; \
	}
#define VOID_SI(name, fn) \
	static int name(lua_State *L, World *lw, KpImages *w) \
	{ \
		fn(w, SPR(2), INT(3)); \
		return 0; \
	}

VOID_II(m_image_set_method, kp_image_set_method)
VOID_II(m_image_set_header, kp_image_set_header)
VOID_I(m_image_init_script, kp_image_init_script)
VOID_I(m_image_init_drawing, kp_image_init_drawing)
VOID_II(m_image_play, kp_image_play)
VOID_I(m_image_bounds, kp_image_bounds)
VOID_I(m_image_sync_frame, kp_image_sync_frame)
VOID_II(m_image_set_frameset, kp_image_set_frameset)
VOID_I(m_image_refresh_follow, kp_image_refresh_follow)
VOID_II(m_image_set_direction, kp_image_set_direction)
VOID_II(m_image_direction_from_angle, kp_image_direction_from_angle)
VOID_II(m_image_set_mirror, kp_image_set_mirror)
VOID_II(m_image_set_follow, kp_image_set_follow)
VOID_I(m_image_hide, kp_image_hide)
VOID_I(m_image_show, kp_image_show)
VOID_I(m_image_release, kp_image_release)
VOID_I(m_image_begin_cloak, kp_image_begin_cloak)
VOID_I(m_image_begin_decloak, kp_image_begin_decloak)
VOID_I(m_image_update, kp_image_update)
VOID_I(m_image_update_cloaking, kp_image_update_cloaking)
VOID_I(m_image_update_decloaking, kp_image_update_decloaking)
VOID_I(m_image_update_warp, kp_image_update_warp)
VOID_I(m_health_bar_release, kp_health_bar_release)
VOID_I(m_selection_circle_release, kp_selection_circle_release)
VOID_SI(m_sprite_set_visibility, kp_sprite_set_visibility)
VOID_S(m_sprite_destroy, kp_sprite_destroy)
VOID_S(m_sprite_update, kp_sprite_update)
VOID_SI(m_sprite_set_direction, kp_sprite_set_direction)
VOID_SI(m_sprite_set_facing, kp_sprite_set_facing)
VOID_S(m_sprite_refresh_bounds, kp_sprite_refresh_bounds)
VOID_S(m_sprite_hide_uncloakable, kp_sprite_hide_uncloakable)
VOID_S(m_sprite_begin_cloak, kp_sprite_begin_cloak)
VOID_S(m_sprite_cloak_detected, kp_sprite_cloak_detected)
VOID_S(m_sprite_cloak_undetected, kp_sprite_cloak_undetected)
VOID_SI(m_sprite_flash, kp_sprite_flash)
VOID_S(m_sprite_remove_circle, kp_sprite_remove_circle)
VOID_SI(m_sprite_add_circle, kp_sprite_add_circle)
VOID_S(m_sprite_remove_health_bar, kp_sprite_remove_health_bar)

static int m_image_init_record(lua_State *L, World *lw, KpImages *w)
{
	kp_image_init_record(w, IMG(2), INT(3), SPR(4), INT(5), INT(6));
	return 0;
}

static int m_image_set_offset(lua_State *L, World *lw, KpImages *w)
{
	kp_image_set_offset(w, IMG(2), INT(3), INT(4));
	return 0;
}

static int m_image_alloc(lua_State *L, World *lw, KpImages *w)
{
	(void)lw;
	return kp_push_int(L, kp_image_alloc(w));
}

static int m_image_copy_cloak(lua_State *L, World *lw, KpImages *w)
{
	kp_image_copy_cloak(w, IMG(2), IMG(3));
	return 0;
}

static int m_image_create_relative(lua_State *L, World *lw, KpImages *w)
{
	return kp_push_int(L, kp_image_create_relative(w, IMG(2), INT(3), INT(4), INT(5), BOOL(6)));
}

#define CREATE_IMAGE(name, fn) \
	static int name(lua_State *L, World *lw, KpImages *w) \
	{ \
		return kp_push_int(L, fn(w, SPR(2), INT(3), INT(4), INT(5), INT(6))); \
	}
CREATE_IMAGE(m_sprite_image_above_main, kp_sprite_image_above_main)
CREATE_IMAGE(m_sprite_image_on_top, kp_sprite_image_on_top)
CREATE_IMAGE(m_sprite_image_below_main, kp_sprite_image_below_main)
CREATE_IMAGE(m_sprite_image_copy_on_top, kp_sprite_image_copy_on_top)

static int m_sprite_clone(lua_State *L, World *lw, KpImages *w) { return kp_push_int(L, kp_sprite_clone(w, SPR(2))); }

static int m_sprite_take_main(lua_State *L, World *lw, KpImages *w)
{
	int32_t src = INT(3);
	if (src != KP_NONE) src = SPR(3);
	kp_sprite_take_main(w, INT(2), src);
	return 0;
}

static int m_sprite_init(lua_State *L, World *lw, KpImages *w)
{
	return kp_push_bool(L, kp_sprite_init(w, SPR(2), INT(3), INT(4), INT(5), INT(6)));
}

static int m_sprite_create(lua_State *L, World *lw, KpImages *w)
{
	return kp_push_int(L, kp_sprite_create(w, INT(2), INT(3), INT(4), INT(5)));
}

static int m_sprite_move(lua_State *L, World *lw, KpImages *w)
{
	kp_sprite_move(w, SPR(2), INT(3), INT(4));
	return 0;
}

static int m_sprite_play(lua_State *L, World *lw, KpImages *w)
{
	kp_sprite_play(w, SPR(2), INT(3), BOOL(4));
	return 0;
}

static int m_refresh_bounds(lua_State *L, World *lw, KpImages *w)
{
	(void)L;
	(void)lw;
	kp_images_refresh_bounds(w);
	return 0;
}

static int m_sprite_depth_key(lua_State *L, World *lw, KpImages *w)
{
	return kp_push_int(L, kp_sprite_depth_key(w, SPR(2)));
}

static int m_init_thingies(lua_State *L, World *lw, KpImages *w)
{
	(void)L;
	(void)lw;
	kp_images_init_thingies(w);
	return 0;
}

static int m_lone_create(lua_State *L, World *lw, KpImages *w)
{
	return kp_push_int(L, kp_lone_create(w, INT(2), INT(3), INT(4), INT(5)));
}

static int m_thingy_visibility(lua_State *L, World *lw, KpImages *w)
{
	kp_thingy_visibility(w, LONE(2));
	return 0;
}

static int m_thingy_visibility_explored(lua_State *L, World *lw, KpImages *w)
{
	kp_thingy_visibility_explored(w, LONE(2));
	return 0;
}

static int m_sprite_set_cloak_mode(lua_State *L, World *lw, KpImages *w)
{
	kp_sprite_set_cloak_mode(w, SPR(2), INT(3), INT(4), INT(5));
	return 0;
}

static int m_sprite_visibility_rect(lua_State *L, World *lw, KpImages *w)
{
	int32_t s = INT(2);
	if (s != KP_NONE) s = SPR(2);
	return kp_push_bool(L, kp_sprite_visibility_rect(w, s, INT(3), INT(4)));
}

static int m_thingy_visibility_rect(lua_State *L, World *lw, KpImages *w)
{
	return kp_push_bool(L, kp_thingy_visibility_rect(w, LONE(2), INT(3), INT(4)));
}

static int m_lone_update(lua_State *L, World *lw, KpImages *w)
{
	kp_lone_update(w, LONE(2));
	return 0;
}

static int m_fog_create(lua_State *L, World *lw, KpImages *w) { return kp_push_int(L, kp_fog_create(w, INT(2), SPR(3))); }

static int m_fog_at_point(lua_State *L, World *lw, KpImages *w)
{
	return kp_push_int(L, kp_fog_at_point(w, INT(2), INT(3)));
}

static int m_fog_update(lua_State *L, World *lw, KpImages *w)
{
	kp_fog_update(w, FOG(2));
	return 0;
}

static int m_update_thingies(lua_State *L, World *lw, KpImages *w)
{
	(void)L;
	(void)lw;
	kp_images_update_thingies(w);
	return 0;
}

static int m_image_spawn_sprite(lua_State *L, World *lw, KpImages *w)
{
	return kp_push_int(L, kp_image_spawn_sprite(w, IMG(2), INT(3), INT(4), INT(5), INT(6)));
}

static int m_init_selection_records(lua_State *L, World *lw, KpImages *w)
{
	(void)L;
	(void)lw;
	kp_images_init_selection_records(w);
	return 0;
}

static int m_health_bar_create(lua_State *L, World *lw, KpImages *w)
{
	return kp_push_int(L, kp_health_bar_create(w, SPR(2)));
}

static int m_selection_circle_create(lua_State *L, World *lw, KpImages *w)
{
	return kp_push_int(L, kp_selection_circle_create(w, SPR(2), INT(3), INT(4)));
}

static int m_clear_records(lua_State *L, World *lw, KpImages *w)
{
	(void)L;
	(void)lw;
	kp_images_clear_records(w);
	return 0;
}

static int map_size_ok(int32_t width, int32_t height)
{
	return width >= 1 && width <= KP_ROWS && height >= 1 && height <= KP_ROWS;
}

static int m_set_map(lua_State *L, World *lw, KpImages *w)
{
	int32_t width = INT(2), height = INT(3);
	if (!map_size_ok(width, height)) kp_images_fail(w, "images: map size %dx%d outside 1..256", (int)width, (int)height);
	kp_images_set_map(w, width, height);
	push_env(L, 1);
	if (lua_isnoneornil(L, 4)) lua_newtable(L);
	else lua_pushvalue(L, 4);
	lua_setfield(L, -2, "tile_flags");
	return 0;
}

static int m_init_pools(lua_State *L, World *lw, KpImages *w)
{
	(void)L;
	(void)lw;
	kp_images_init_pools(w);
	return 0;
}

static const char *list_name(KpImages *w, const KpList *l)
{
	if (l == &w->free_images) return "free_images";
	if (l == &w->free_health) return "free_health";
	if (l == &w->free_circles) return "free_circles";
	if (l == &w->free_ghosts) return "free_ghosts";
	return "free_boxes";
}

static int m_free_list_of(lua_State *L, World *lw, KpImages *w)
{
	push_env(L, 1);
	lua_getfield(L, -1, list_name(w, kp_images_free_list_of(w, IMG(2))));
	return 1;
}

static int m_hidden_at(lua_State *L, World *lw, KpImages *w) { return kp_push_int(L, kp_images_hidden_at(w, INT(2), INT(3))); }

static int m_unexplored_at(lua_State *L, World *lw, KpImages *w)
{
	return kp_push_int(L, kp_images_unexplored_at(w, INT(2), INT(3)));
}

/* draw_collect(first_row, methods, out): the draw list's collection (kp_draw_collect) into out[1..n]; returns n. */
static int m_draw_collect(lua_State *L, World *lw, KpImages *w)
{
	int32_t max = KP_SPRITES * KP_DRAW_SPRITE + KP_IMAGES * KP_DRAW_IMAGE, n, k;
	if (!lua_istable(L, 4)) kp_images_fail(w, "draw: argument 3 is not a table");
	if (!lw->draw) lw->draw = (int32_t *)malloc((size_t)max * sizeof *lw->draw);
	if (!lw->draw) kp_images_fail(w, "draw: out of memory");
	n = kp_draw_collect(w, INT(2), (uint32_t)INT(3), lw->draw, max);
	if (n < 0) kp_images_fail(w, "draw: the collection overflows");
	for (k = 0; k < n; k++) {
		lua_pushnumber(L, lw->draw[k]);
		lua_rawseti(L, 4, k + 1);
	}
	lua_pushnumber(L, n);
	return 1;
}

static int m_sprite_row(lua_State *L, World *lw, KpImages *w) { return kp_push_int(L, kp_images_sprite_row(w, SPR(2))); }

/* set_dat(table, column, index, value): the DAT value in the world's data (shared by every
 * world over it) and in the Lua tables of w.dat. */
static int m_set_dat(lua_State *L, World *lw, KpImages *w)
{
	const char *table = lua_tostring(L, 2), *column = lua_tostring(L, 3);
	int32_t i = INT(4), v = INT(5);
	const KpDatColumn *c;
	Data *d;
	push_env(L, 1);
	lua_getfield(L, -1, "data");
	d = (Data *)lua_touserdata(L, -1);
	c = table && column ? kp_dat_column(&d->dat, table, column) : NULL;
	if (!c || i < 0 || !kp_dat_set(&d->dat, c, (uint32_t)i, (uint32_t)v)) {
		kp_images_fail(w, "images: no DAT value %s.%s[%d]", table ? table : "?", column ? column : "?", (int)i);
	}
	lua_getfield(L, -2, "dat");
	if (lua_istable(L, -1)) {
		lua_getfield(L, -1, table);
		if (lua_istable(L, -1)) {
			lua_getfield(L, -1, column);
			if (lua_istable(L, -1)) {
				lua_pushvalue(L, 5);
				lua_rawseti(L, -2, i);
			}
		}
	}
	return 0;
}

static const struct {
	const char *name;
	Method fn;
} METHODS[] = {
	{ "hook", m_hook }, { "flingy_signal", m_flingy_signal }, { "unit_set_cloak_state", m_unit_set_cloak_state },
	{ "image_sound", m_image_sound }, { "run_iscript", m_run_iscript }, { "grp_of", m_grp_of },
	{ "grp_frames", m_grp_frames }, { "frame_rect", m_frame_rect }, { "lo_offset", m_lo_offset },
	{ "damage_overlay_slots", m_damage_overlay_slots }, { "image_set_method", m_image_set_method },
	{ "image_set_header", m_image_set_header }, { "image_init_record", m_image_init_record },
	{ "image_init_script", m_image_init_script }, { "image_init_drawing", m_image_init_drawing },
	{ "image_play", m_image_play }, { "image_bounds", m_image_bounds }, { "image_sync_frame", m_image_sync_frame },
	{ "image_set_frameset", m_image_set_frameset }, { "image_refresh_follow", m_image_refresh_follow },
	{ "image_set_direction", m_image_set_direction }, { "image_direction_from_angle", m_image_direction_from_angle },
	{ "image_set_mirror", m_image_set_mirror }, { "image_set_follow", m_image_set_follow },
	{ "image_set_offset", m_image_set_offset }, { "image_hide", m_image_hide }, { "image_show", m_image_show },
	{ "image_alloc", m_image_alloc }, { "image_release", m_image_release }, { "image_begin_cloak", m_image_begin_cloak },
	{ "image_begin_decloak", m_image_begin_decloak }, { "image_copy_cloak", m_image_copy_cloak },
	{ "image_create_relative", m_image_create_relative }, { "image_update", m_image_update },
	{ "image_update_cloaking", m_image_update_cloaking }, { "image_update_decloaking", m_image_update_decloaking },
	{ "image_update_warp", m_image_update_warp }, { "sprite_image_above_main", m_sprite_image_above_main },
	{ "sprite_image_on_top", m_sprite_image_on_top }, { "sprite_image_below_main", m_sprite_image_below_main },
	{ "sprite_image_copy_on_top", m_sprite_image_copy_on_top }, { "sprite_clone", m_sprite_clone },
	{ "sprite_take_main", m_sprite_take_main }, { "sprite_set_visibility", m_sprite_set_visibility },
	{ "sprite_init", m_sprite_init }, { "sprite_create", m_sprite_create }, { "sprite_destroy", m_sprite_destroy },
	{ "sprite_move", m_sprite_move }, { "sprite_update", m_sprite_update }, { "sprite_play", m_sprite_play },
	{ "sprite_set_direction", m_sprite_set_direction }, { "sprite_set_facing", m_sprite_set_facing },
	{ "sprite_refresh_bounds", m_sprite_refresh_bounds }, { "refresh_bounds", m_refresh_bounds },
	{ "sprite_depth_key", m_sprite_depth_key }, { "init_thingies", m_init_thingies },
	{ "lone_create", m_lone_create }, { "thingy_visibility", m_thingy_visibility },
	{ "thingy_visibility_explored", m_thingy_visibility_explored },
	{ "sprite_hide_uncloakable", m_sprite_hide_uncloakable }, { "sprite_begin_cloak", m_sprite_begin_cloak },
	{ "sprite_set_cloak_mode", m_sprite_set_cloak_mode }, { "sprite_cloak_detected", m_sprite_cloak_detected },
	{ "sprite_cloak_undetected", m_sprite_cloak_undetected }, { "sprite_visibility_rect", m_sprite_visibility_rect },
	{ "thingy_visibility_rect", m_thingy_visibility_rect }, { "lone_update", m_lone_update },
	{ "fog_create", m_fog_create }, { "fog_at_point", m_fog_at_point }, { "sprite_flash", m_sprite_flash },
	{ "fog_update", m_fog_update }, { "update_thingies", m_update_thingies },
	{ "image_spawn_sprite", m_image_spawn_sprite }, { "init_selection_records", m_init_selection_records },
	{ "health_bar_create", m_health_bar_create }, { "health_bar_release", m_health_bar_release },
	{ "selection_circle_release", m_selection_circle_release }, { "sprite_remove_circle", m_sprite_remove_circle },
	{ "sprite_add_circle", m_sprite_add_circle }, { "sprite_remove_health_bar", m_sprite_remove_health_bar },
	{ "selection_circle_create", m_selection_circle_create }, { "clear_records", m_clear_records },
	{ "set_map", m_set_map }, { "init_pools", m_init_pools }, { "free_list_of", m_free_list_of },
	{ "hidden_at", m_hidden_at }, { "unexplored_at", m_unexplored_at }, { "sprite_row", m_sprite_row },
	{ "set_dat", m_set_dat }, { "draw_collect", m_draw_collect },
};

#define METHOD_COUNT ((int)(sizeof METHODS / sizeof METHODS[0]))

/* Raises the failure of a call into the world: the hook's Lua error value, or the message. */
static int raise_failure(lua_State *L, World *lw)
{
	if (!lw->lua_error) lua_pushstring(L, lw->w->error);
	lw->lua_error = 0;
	return lua_error(L);
}

/* Every method: METHODS[upvalue 1] under a failure handler. */
static int world_call(lua_State *L)
{
	int index = (int)lua_tonumber(L, lua_upvalueindex(1)), r;
	World *lw = check_world(L, 1);
	lua_State *saved = lw->L;
	KpTry t;
	lw->L = L;
	lw->lua_error = 0;
	kp_images_begin(lw->w, &t);
	if (setjmp(t.buf)) {
		kp_images_end(lw->w, &t);
		lw->L = saved;
		return raise_failure(L, lw);
	}
	r = METHODS[index].fn(L, lw, lw->w);
	kp_images_end(lw->w, &t);
	lw->L = saved;
	return r;
}

/* --- world fields --- */

static int scalar(KpImages *w, const char *k, int32_t **field)
{
	if (strcmp(k, "map_w") == 0) *field = &w->map_w;
	else if (strcmp(k, "map_h") == 0) *field = &w->map_h;
	else if (strcmp(k, "map_w_px") == 0) *field = &w->map_w_px;
	else if (strcmp(k, "map_h_px") == 0) *field = &w->map_h_px;
	else if (strcmp(k, "tileset") == 0) *field = &w->tileset;
	else if (strcmp(k, "expansion") == 0) *field = &w->expansion;
	else if (strcmp(k, "local_player") == 0) *field = &w->local_player;
	else if (strcmp(k, "replay") == 0) *field = &w->replay;
	else if (strcmp(k, "local_vision") == 0) *field = &w->local_vision;
	else if (strcmp(k, "cursor") == 0) *field = &w->cursor;
	else return 0;
	return 1;
}

/* Context fields (nil for none): unit, flingy, bullet. */
static int context(KpImages *w, const char *k, int32_t **value, int **present)
{
	if (strcmp(k, "unit") == 0) {
		*value = &w->unit;
		*present = &w->has_unit;
	} else if (strcmp(k, "flingy") == 0) {
		*value = &w->flingy;
		*present = &w->has_flingy;
	} else if (strcmp(k, "bullet") == 0) {
		*value = &w->bullet;
		*present = &w->has_bullet;
	} else {
		return 0;
	}
	return 1;
}

/* Methods, then the environment (proxies and Lua fields), then the C fields. */
static int world_index(lua_State *L)
{
	World *lw = (World *)lua_touserdata(L, 1);
	KpImages *w = lw->w;
	const char *k = lua_tostring(L, 2);
	int32_t *field;
	int *present;
	lua_pushvalue(L, 2);
	lua_rawget(L, lua_upvalueindex(1));
	if (!lua_isnil(L, -1)) return 1;
	lua_pop(L, 1);
	push_env(L, 1);
	lua_pushvalue(L, 2);
	lua_rawget(L, -2);
	if (!lua_isnil(L, -1)) return 1;
	lua_pop(L, 2);
	if (lua_type(L, 2) == LUA_TSTRING) {
		if (scalar(w, k, &field)) return kp_push_int(L, *field);
		if (context(w, k, &field, &present)) {
			if (!*present) lua_pushnil(L);
			else kp_push_int(L, *field);
			return 1;
		}
		if (strcmp(k, "replay_reveal") == 0) return kp_push_bool(L, w->replay_reveal);
		if (strcmp(k, "step_limit") == 0) {
			if (w->step_limit < 0) lua_pushnil(L);
			else kp_push_int(L, w->step_limit);
			return 1;
		}
	}
	lua_pushnil(L);
	return 1;
}

static int32_t field_value(lua_State *L, const char *k)
{
	lua_Number v = lua_tonumber(L, 3);
	if (lua_type(L, 3) != LUA_TNUMBER || !kp_integral(v)) luaL_error(L, "images: world field %s takes integers", k);
	return kp_to_i32(v);
}

static const char *const PROXIES[] = { "img", "spr", "thg", "fog", "vgrp", "rows", "free_images", "free_health",
	"free_circles", "free_ghosts", "free_boxes", "free_sprites", "free_lone", "active_lone", "free_fog", "active_fog",
	"data", NULL };

static int world_newindex(lua_State *L)
{
	World *lw = check_world(L, 1);
	KpImages *w = lw->w;
	const char *k = lua_tostring(L, 2);
	int32_t *field;
	int *present, i;
	if (lua_type(L, 2) == LUA_TSTRING) {
		for (i = 0; PROXIES[i]; i++) {
			if (strcmp(k, PROXIES[i]) == 0) return luaL_error(L, "images: world field %s cannot be replaced", k);
		}
		if (scalar(w, k, &field)) {
			int32_t v = field_value(L, k);
			if ((field == &w->map_w || field == &w->map_h) && (v < 1 || v > KP_ROWS)) {
				return luaL_error(L, "images: %s outside 1..256", k);
			}
			*field = v;
			return 0;
		}
		if (context(w, k, &field, &present)) {
			*present = !lua_isnil(L, 3);
			*field = *present ? field_value(L, k) : 0;
			return 0;
		}
		if (strcmp(k, "replay_reveal") == 0) {
			w->replay_reveal = lua_toboolean(L, 3);
			return 0;
		}
		if (strcmp(k, "step_limit") == 0) {
			w->step_limit = lua_isnil(L, 3) ? -1 : field_value(L, k);
			return 0;
		}
		if (strcmp(k, "sound") == 0) w->has_sound = lua_toboolean(L, 3);
		if (strcmp(k, "play_probe") == 0) w->has_play_probe = lua_toboolean(L, 3);
		if (strcmp(k, "rng") == 0) w->rng = lua_isnil(L, 3) ? NULL : (KpRngGame *)luaL_checkudata(L, 3, KP_LUA_RNG_GAME);
		if (strcmp(k, "circle_colors") == 0) {
			w->has_circle_colors = lua_istable(L, 3);
			for (i = 0; i < 256; i++) {
				w->circle_colors[i] = -1;
				if (!w->has_circle_colors) continue;
				lua_rawgeti(L, 3, i);
				if (lua_type(L, -1) == LUA_TNUMBER) w->circle_colors[i] = kp_to_i32(lua_tonumber(L, -1));
				lua_pop(L, 1);
			}
		}
	}
	push_env(L, 1);
	lua_pushvalue(L, 2);
	lua_pushvalue(L, 3);
	lua_rawset(L, -3);
	return 0;
}

static int world_gc(lua_State *L)
{
	World *lw = check_world(L, 1);
	if (lw->owned) kp_images_free(lw->w);
	lw->w = NULL;
	free(lw->draw);
	lw->draw = NULL;
	return 0;
}

static int data_gc(lua_State *L)
{
	Data *d = (Data *)luaL_checkudata(L, 1, DATA_META);
	kp_dat_free(&d->dat);
	kp_grpmeta_free(&d->grp);
	kp_iscript_free(&d->script);
	return 0;
}

/* Number field `key` of the table at index t, `fallback` when absent. */
static lua_Number num_field(lua_State *L, int t, const char *key, lua_Number fallback)
{
	lua_Number v;
	lua_getfield(L, t, key);
	v = lua_isnumber(L, -1) ? lua_tonumber(L, -1) : fallback;
	lua_pop(L, 1);
	return v;
}

/* Calls each(L, table_name, column_name, layout_index) for every column of dat.layout (the
 * layout entry is on the stack top); returns the number of columns. */
typedef void (*ColumnFn)(lua_State *L, const char *table, const char *column, void *ud);

static int each_column(lua_State *L, int dat, ColumnFn fn, void *ud)
{
	int n = 0;
	lua_getfield(L, dat, "layout");
	if (!lua_istable(L, -1)) {
		lua_pop(L, 1);
		return 0;
	}
	lua_pushnil(L);
	while (lua_next(L, -2)) {
		const char *table = lua_tostring(L, -2);
		if (lua_type(L, -2) == LUA_TSTRING && lua_istable(L, -1)) {
			lua_getfield(L, -1, "columns");
			if (lua_istable(L, -1)) {
				lua_pushnil(L);
				while (lua_next(L, -2)) {
					if (lua_type(L, -2) == LUA_TSTRING && lua_istable(L, -1)) {
						fn(L, table, lua_tostring(L, -2), ud);
						n++;
					}
					lua_pop(L, 1);
				}
			}
			lua_pop(L, 1);
		}
		lua_pop(L, 1);
	}
	lua_pop(L, 1);
	return n;
}

typedef struct {
	size_t bytes;
	KpDat *dat;
	int values; /* stack index of the dat table */
} DatBuild;

static void count_column(lua_State *L, const char *table, const char *column, void *ud)
{
	DatBuild *b = (DatBuild *)ud;
	int t = lua_gettop(L);
	(void)table;
	(void)column;
	b->bytes += (size_t)num_field(L, t, "size", 0) * (size_t)num_field(L, t, "count", 0);
}

static void fill_column(lua_State *L, const char *table, const char *column, void *ud)
{
	DatBuild *b = (DatBuild *)ud;
	int t = lua_gettop(L);
	KpDatColumn *c = &b->dat->columns[b->dat->column_count];
	uint32_t k, base;
	size_t len = strlen(column);
	c->table = kp_dat_table_name(table);
	if (!c->table) return;
	if (len >= sizeof c->name) len = sizeof c->name - 1;
	memcpy(c->name, column, len);
	c->name[len] = '\0';
	c->size = (uint8_t)num_field(L, t, "size", 0);
	c->per_id = (uint8_t)num_field(L, t, "per_id", 1);
	c->count = (uint16_t)num_field(L, t, "count", 0);
	c->first_id = (uint16_t)num_field(L, t, "first_id", 0);
	c->data = b->dat->blob + b->bytes;
	b->bytes += (size_t)c->size * c->count;
	b->dat->column_count++;
	base = (uint32_t)c->first_id * c->per_id;
	lua_getfield(L, b->values, table);
	if (lua_istable(L, -1)) {
		lua_getfield(L, -1, column);
		if (lua_istable(L, -1)) {
			for (k = 0; k < c->count; k++) {
				lua_rawgeti(L, -1, (int)(base + k));
				if (lua_isnumber(L, -1)) kp_dat_set(b->dat, c, base + k, (uint32_t)kp_to_i32(lua_tonumber(L, -1)));
				lua_pop(L, 1);
			}
		}
		lua_pop(L, 1);
	}
	lua_pop(L, 1);
}

static int dat_from_table(lua_State *L, int t, KpDat *dat, KpError *err)
{
	DatBuild b;
	int columns;
	b.bytes = 0;
	b.dat = dat;
	b.values = t;
	columns = each_column(L, t, count_column, &b);
	if (kp_dat_alloc(dat, b.bytes, columns, err) != 0) return -1;
	b.bytes = 0;
	each_column(L, t, fill_column, &b);
	return 0;
}

/* The length of string field `key` (0 when absent) of the table at index t. */
static size_t string_size(lua_State *L, int t, const char *key)
{
	size_t n = 0;
	lua_getfield(L, t, key);
	if (lua_isstring(L, -1)) lua_tolstring(L, -1, &n);
	lua_pop(L, 1);
	return n;
}

static uint32_t table_length(lua_State *L, int t, const char *key)
{
	uint32_t n = 0;
	lua_getfield(L, t, key);
	if (lua_istable(L, -1)) n = (uint32_t)lua_objlen(L, -1);
	lua_pop(L, 1);
	return n;
}

/* The largest positive integer key of map field `key` (images.tbl indices). */
static uint32_t max_key(lua_State *L, int t, const char *key)
{
	uint32_t n = 0;
	lua_getfield(L, t, key);
	if (lua_istable(L, -1)) {
		lua_pushnil(L);
		while (lua_next(L, -2)) {
			lua_Number k = lua_tonumber(L, -2);
			if (lua_type(L, -2) == LUA_TNUMBER && k >= 1 && k <= 65535 && (uint32_t)k > n) n = (uint32_t)k;
			lua_pop(L, 1);
		}
	}
	lua_pop(L, 1);
	return n;
}

static void fill_tbl_map(lua_State *L, int t, const char *key, uint16_t *map, uint32_t tbl_count, uint32_t entries)
{
	lua_getfield(L, t, key);
	if (lua_istable(L, -1)) {
		lua_pushnil(L);
		while (lua_next(L, -2)) {
			lua_Number k = lua_tonumber(L, -2), v = lua_tonumber(L, -1);
			if (lua_type(L, -2) == LUA_TNUMBER && k >= 1 && k <= tbl_count && v >= 1 && v <= entries) {
				map[(uint32_t)k] = (uint16_t)v;
			}
			lua_pop(L, 1);
		}
	}
	lua_pop(L, 1);
}

static int grp_from_table(lua_State *L, int t, KpGrpMeta *g, KpError *err)
{
	uint32_t grps = table_length(L, t, "grps"), los = table_length(L, t, "los"), i, tbl;
	size_t bytes = string_size(L, t, "frms"), at;
	const char *p;
	tbl = (uint32_t)num_field(L, t, "tbl_count", 0);
	if (max_key(L, t, "grp_of_tbl") > tbl) tbl = max_key(L, t, "grp_of_tbl");
	if (max_key(L, t, "lo_of_tbl") > tbl) tbl = max_key(L, t, "lo_of_tbl");
	lua_getfield(L, t, "los");
	for (i = 1; i <= los; i++) {
		lua_rawgeti(L, -1, (int)i);
		bytes += string_size(L, lua_gettop(L), "data");
		lua_pop(L, 1);
	}
	lua_pop(L, 1);
	if (kp_grpmeta_alloc(g, tbl, grps, los, bytes, err) != 0) return -1;
	lua_getfield(L, t, "frms");
	p = lua_tolstring(L, -1, &at);
	if (p) memcpy(g->blob, p, at);
	else at = 0;
	lua_pop(L, 1);
	g->frms = g->blob;
	g->frms_size = (uint32_t)at;
	lua_getfield(L, t, "grps");
	for (i = 1; i <= grps; i++) {
		KpGrpEntry *e = &g->grps[i];
		int k;
		lua_rawgeti(L, -1, (int)i);
		k = lua_gettop(L);
		e->frames = (uint16_t)num_field(L, k, "frames", 0);
		e->width = (uint16_t)num_field(L, k, "width", 0);
		e->height = (uint16_t)num_field(L, k, "height", 0);
		e->atlas = (uint32_t)num_field(L, k, "atlas", 0);
		e->first_frame = (uint32_t)num_field(L, k, "first_frame", 0);
		lua_pop(L, 1);
	}
	lua_pop(L, 1);
	lua_getfield(L, t, "los");
	for (i = 1; i <= los; i++) {
		KpLoEntry *e = &g->los[i];
		size_t n = 0;
		lua_rawgeti(L, -1, (int)i);
		lua_getfield(L, -1, "data");
		p = lua_tolstring(L, -1, &n);
		if (p) memcpy(g->blob + at, p, n);
		e->data = g->blob + at;
		e->size = (uint32_t)n;
		at += n;
		lua_pop(L, 1);
		e->frames = (uint32_t)num_field(L, lua_gettop(L), "frames", 0);
		e->points = (uint32_t)num_field(L, lua_gettop(L), "points", 0);
		lua_pop(L, 1);
	}
	lua_pop(L, 1);
	fill_tbl_map(L, t, "grp_of_tbl", g->grp_of_tbl, tbl, grps);
	fill_tbl_map(L, t, "lo_of_tbl", g->lo_of_tbl, tbl, los);
	if (kp_grpmeta_check(g, err) != 0) {
		kp_grpmeta_free(g);
		return -1;
	}
	return 0;
}

static int script_from_table(lua_State *L, int t, KpIScript *script, KpError *err)
{
	size_t size = 0;
	const char *code;
	uint32_t ids = 0;
	lua_getfield(L, t, "code");
	code = lua_tolstring(L, -1, &size);
	if (!code) code = "";
	lua_getfield(L, t, "headers");
	if (!lua_istable(L, -1)) {
		lua_pop(L, 1);
		lua_newtable(L);
	}
	lua_pushnil(L);
	while (lua_next(L, -2)) {
		lua_Number id = lua_tonumber(L, -2);
		lua_pop(L, 1);
		if (lua_type(L, -1) != LUA_TNUMBER || id != floor(id) || id < 0 || id > 65535) {
			snprintf(err->msg, sizeof err->msg, "images: IScript header ids are 0..65535");
			return -1;
		}
		if ((uint32_t)id + 1 > ids) ids = (uint32_t)id + 1;
	}
	if (kp_iscript_from_code(script, (const uint8_t *)code, size, ids, err) != 0) return -1;
	lua_pushnil(L);
	while (lua_next(L, -2)) {
		script->headers[(uint32_t)lua_tonumber(L, -2)] = kp_to_i32(lua_tonumber(L, -1));
		lua_pop(L, 1);
	}
	lua_pop(L, 2);
	return 0;
}

static int images_data(lua_State *L)
{
	Data *d;
	KpError err;
	int failed;
	luaL_checktype(L, 1, LUA_TTABLE);
	luaL_checktype(L, 2, LUA_TTABLE);
	luaL_checktype(L, 3, LUA_TTABLE);
	d = (Data *)lua_newuserdata(L, sizeof *d);
	memset(d, 0, sizeof *d);
	failed = dat_from_table(L, 1, &d->dat, &err) != 0 || grp_from_table(L, 2, &d->grp, &err) != 0
		|| script_from_table(L, 3, &d->script, &err) != 0;
	if (failed) {
		kp_dat_free(&d->dat);
		kp_grpmeta_free(&d->grp);
		kp_iscript_free(&d->script);
		return luaL_error(L, "%s", err.msg);
	}
	luaL_getmetatable(L, DATA_META);
	lua_setmetatable(L, -2);
	return 1;
}

static int32_t opt_int(lua_State *L, int opts, const char *key, int32_t fallback)
{
	int32_t v = fallback;
	lua_getfield(L, opts, key);
	if (!lua_isnil(L, -1)) {
		if (lua_type(L, -1) != LUA_TNUMBER || !kp_integral(lua_tonumber(L, -1))) {
			luaL_error(L, "images: option %s takes an integer", key);
		}
		v = kp_to_i32(lua_tonumber(L, -1));
	}
	lua_pop(L, 1);
	return v;
}

/* env[key] = opts[key], or a new table when absent and `table` is set. */
static void copy_option(lua_State *L, int opts, int env, const char *from, const char *to, int table)
{
	lua_getfield(L, opts, from);
	if (lua_isnil(L, -1) && table) {
		lua_pop(L, 1);
		lua_newtable(L);
	}
	lua_setfield(L, env, to);
}

/* The environment of the world userdata at index `world` (its proxies and, for a world of images_new, the data at
 * index `data` and the options at index `opts`); leaves the world on the stack top. */
static int finish_world(lua_State *L, World *lw, int world, int data, int opts)
{
	KpImages *w = lw->w;
	int env, i;
	lua_newtable(L);
	env = lua_gettop(L);
	if (opts) {
		lua_pushvalue(L, data);
		lua_setfield(L, env, "data");
		copy_option(L, opts, env, "rng", "rng", 0);
		copy_option(L, opts, env, "dat", "dat", 0);
		copy_option(L, opts, env, "grp", "grp", 0);
		copy_option(L, opts, env, "iscript", "script", 0);
		copy_option(L, opts, env, "hooks", "hooks", 1);
		copy_option(L, opts, env, "sound", "sound", 0);
		copy_option(L, opts, env, "tiles", "tile_flags", 1);
		lua_getfield(L, env, "sound");
		w->has_sound = lua_toboolean(L, -1);
		lua_pop(L, 1);
	}

#define FIELD(table, name, count) \
	kp_bridge_push_array(L, world, w->table.name, 0, count, 0); \
	lua_setfield(L, -2, #name);
#define IMAGE_FIELD(name) FIELD(img, name, KP_IMAGES)
#define SPRITE_FIELD(name) FIELD(spr, name, KP_SPRITES)
	lua_newtable(L);
	KP_IMAGE_FIELDS(IMAGE_FIELD)
	lua_setfield(L, env, "img");
	lua_newtable(L);
	KP_SPRITE_FIELDS(SPRITE_FIELD)
	kp_bridge_push_lists(L, world, w->spr.images, KP_SPRITES);
	lua_setfield(L, -2, "images");
	lua_setfield(L, env, "spr");
	lua_newtable(L);
	FIELD(thg, sprite, KP_LONE)
	FIELD(thg, kind, KP_LONE)
	FIELD(thg, prev, KP_LONE)
	FIELD(thg, next, KP_LONE)
	lua_setfield(L, env, "thg");
	lua_newtable(L);
	FIELD(fog, sprite, KP_FOG)
	FIELD(fog, kind, KP_FOG)
	FIELD(fog, prev, KP_FOG)
	FIELD(fog, next, KP_FOG)
	lua_setfield(L, env, "fog");
#undef IMAGE_FIELD
#undef SPRITE_FIELD
#undef FIELD
	lua_getfield(L, env, "img");
	kp_bridge_push_array(L, world, w->img.param, 0, KP_IMAGES, 1);
	lua_setfield(L, -2, "param");
	lua_pop(L, 1);
	kp_bridge_proxy(L, 1, VGRP_META, world);
	lua_setfield(L, env, "vgrp");
	kp_bridge_push_lists(L, world, w->rows, KP_ROWS);
	lua_setfield(L, env, "rows");
	{
		static const char *const NAMES[] = { "free_images", "free_health", "free_circles", "free_ghosts", "free_boxes",
			"free_sprites", "free_lone", "active_lone", "free_fog", "active_fog" };
		KpList *lists[10];
		lists[0] = &w->free_images;
		lists[1] = &w->free_health;
		lists[2] = &w->free_circles;
		lists[3] = &w->free_ghosts;
		lists[4] = &w->free_boxes;
		lists[5] = &w->free_sprites;
		lists[6] = &w->free_lone;
		lists[7] = &w->active_lone;
		lists[8] = &w->free_fog;
		lists[9] = &w->active_fog;
		for (i = 0; i < 10; i++) {
			kp_bridge_push_list(L, world, lists[i]);
			lua_setfield(L, env, NAMES[i]);
		}
	}
	lua_pushvalue(L, env);
	lua_setfenv(L, world);
	lua_getfield(L, LUA_REGISTRYINDEX, WORLDS);
	lua_pushlightuserdata(L, lw);
	lua_pushvalue(L, world);
	lua_rawset(L, -3);
	lua_pop(L, 1);
	lua_pushvalue(L, world);
	return 1;
}

static int images_new(lua_State *L)
{
	Data *d = (Data *)luaL_checkudata(L, 1, DATA_META);
	KpRngGame *rng = NULL;
	World *lw;
	KpImages *w;
	KpError err;
	int dat_present, world;
	luaL_checktype(L, 2, LUA_TTABLE);
	lua_getfield(L, 2, "rng");
	if (!lua_isnil(L, -1)) rng = (KpRngGame *)luaL_checkudata(L, -1, KP_LUA_RNG_GAME);
	lua_pop(L, 1);
	/* A world without DAT tables (unit tests) leaves the selection records zero. */
	lua_getfield(L, 2, "dat");
	dat_present = 0;
	if (lua_istable(L, -1)) {
		lua_getfield(L, -1, "images");
		dat_present = !lua_isnil(L, -1);
		lua_pop(L, 1);
	}
	lua_pop(L, 1);
	w = kp_images_new(dat_present ? &d->dat : NULL, &d->grp, &d->script, rng, &err);
	if (!w) return luaL_error(L, "%s", err.msg);
	lw = (World *)lua_newuserdata(L, sizeof *lw);
	lw->w = w;
	lw->L = L;
	lw->lua_error = 0;
	lw->owned = 1;
	lw->draw = NULL;
	world = lua_gettop(L);
	luaL_getmetatable(L, WORLD_META);
	lua_setmetatable(L, world);
	w->binding = lw;
	w->hook = kp_images_lua_hook;
	w->tile_flags = read_tile_flags;
	w->tile_ctx = lw;
	w->ctx = lw;
	w->tileset = opt_int(L, 2, "tileset", 0);
	w->expansion = opt_int(L, 2, "expansion", 0);
	w->local_player = opt_int(L, 2, "local_player", 0);
	w->replay = opt_int(L, 2, "replay", 0);
	w->local_vision = opt_int(L, 2, "local_vision", 0xff);
	lua_getfield(L, 2, "replay_reveal");
	w->replay_reveal = lua_toboolean(L, -1);
	lua_pop(L, 1);
	{
		int32_t width = opt_int(L, 2, "map_width", 256), height = opt_int(L, 2, "map_height", 256);
		if (!map_size_ok(width, height)) return luaL_error(L, "images: map size %dx%d outside 1..256", (int)width, (int)height);
		kp_images_set_map(w, width, height);
	}

	return finish_world(L, lw, world, 1, 2);
}

lua_State *kp_images_lua_enter(KpImages *w, lua_State *L)
{
	World *lw = (World *)w->binding;
	lua_State *saved = lw->L;
	lw->L = L;
	lw->lua_error = 0;
	return saved;
}

void kp_images_lua_leave(KpImages *w, lua_State *saved) { ((World *)w->binding)->L = saved; }

int kp_images_lua_raise(KpImages *w, lua_State *L)
{
	return raise_failure(L, (World *)w->binding);
}

KpImages *kp_images_lua_check(lua_State *L, int k) { return check_world(L, k)->w; }

void kp_images_lua_wrap(lua_State *L, KpImages *w, int anchor)
{
	World *lw;
	int world;
	if (anchor < 0 && anchor > LUA_REGISTRYINDEX) anchor = lua_gettop(L) + anchor + 1;
	lw = (World *)lua_newuserdata(L, sizeof *lw);
	lw->w = w;
	lw->L = L;
	lw->lua_error = 0;
	lw->owned = 0;
	lw->draw = NULL;
	world = lua_gettop(L);
	luaL_getmetatable(L, WORLD_META);
	lua_setmetatable(L, world);
	w->binding = lw;
	finish_world(L, lw, world, 0, 0);
	lua_getfenv(L, -1);
	lua_pushvalue(L, anchor);
	lua_setfield(L, -2, "owner");
	lua_pop(L, 1);
}

static void define(lua_State *L, const char *meta, lua_CFunction index, lua_CFunction newindex)
{
	luaL_newmetatable(L, meta);
	lua_pushcfunction(L, index);
	lua_setfield(L, -2, "__index");
	lua_pushcfunction(L, newindex);
	lua_setfield(L, -2, "__newindex");
	lua_pop(L, 1);
}

void kp_images_lua_push(lua_State *L)
{
	int k;
	luaL_newmetatable(L, DATA_META);
	lua_pushcfunction(L, data_gc);
	lua_setfield(L, -2, "__gc");
	lua_pop(L, 1);
	kp_bridge_register(L);
	lua_newtable(L);
	lua_newtable(L);
	lua_pushstring(L, "v");
	lua_setfield(L, -2, "__mode");
	lua_setmetatable(L, -2);
	lua_setfield(L, LUA_REGISTRYINDEX, WORLDS);
	define(L, VGRP_META, vgrp_index, vgrp_newindex);
	define(L, VGRP_ENTRY_META, vgrp_entry_index, vgrp_entry_newindex);
	luaL_newmetatable(L, WORLD_META);
	lua_newtable(L); /* methods */
	for (k = 0; k < METHOD_COUNT; k++) {
		lua_pushnumber(L, k);
		lua_pushcclosure(L, world_call, 1);
		lua_setfield(L, -2, METHODS[k].name);
	}
	lua_pushcclosure(L, world_index, 1);
	lua_setfield(L, -2, "__index");
	lua_pushcfunction(L, world_newindex);
	lua_setfield(L, -2, "__newindex");
	lua_pushcfunction(L, world_gc);
	lua_setfield(L, -2, "__gc");
	lua_pop(L, 1);

	lua_newtable(L);
	lua_pushcfunction(L, images_data);
	lua_setfield(L, -2, "data");
	lua_pushcfunction(L, images_new);
	lua_setfield(L, -2, "new");
}
