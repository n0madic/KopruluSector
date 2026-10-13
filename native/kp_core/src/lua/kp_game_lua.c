/*
 * kp_core.game: the hybrid game of ADR 0008 stages N.2-N.5. The Lua game (game/simulation/
 * game.lua) keeps its table and its Lua methods; kp_core.game.install(Game) replaces the
 * methods already ported with C functions over a KpGame (kp_game.h), created on the first
 * call and kept as the game's `native` field. The C state is served to the Lua code with the
 * shape of the Lua tables it replaces:
 *   - g.u[field][n] (array proxies, 1..1700), g.lists.<name>.head/tail, g.player_units[p];
 *   - g.count_all/count_completed/count_kills/count_deaths[i], g.pstat[a][p];
 *   - scalar fields (units_allocated, error_code) through the Game metatable.
 * The C code calls the methods still in Lua by name and reads the DAT values from g.dat, so
 * Lua code (and tests) that change those tables are seen at once. Failures of the C code and
 * errors of the Lua code it calls come back to the calling method as Lua errors.
 */
#include "kp_lua_bridge.h"

#include <stdlib.h>
#include <string.h>

#include "../core/kp_game.h"
#include "../core/kp_images_internal.h"

#define NATIVE_META "kp_core.game.native"
#define GAMES "kp_core.game.games" /* registry: weak table, light userdata KpGame* -> game table */

typedef struct {
	KpGame *g;
	lua_State *L;
	int lua_error;
} Native;

static const char *const LIST_NAMES[KP_LIST_COUNT] = { "active", "pending", "free", "hidden", "special" };

/* --- the Lua side of the C code --- */

static void push_game(lua_State *L, KpGame *g)
{
	lua_getfield(L, LUA_REGISTRYINDEX, GAMES);
	lua_pushlightuserdata(L, g);
	lua_rawget(L, -2);
	lua_remove(L, -2);
}

static int64_t lua_value(KpGame *g, lua_State *L, int k, const char *what)
{
	lua_Number v;
	if (lua_isnil(L, k)) return KP_NIL;
	if (lua_isboolean(L, k)) return lua_toboolean(L, k);
	v = lua_tonumber(L, k);
	if (lua_type(L, k) != LUA_TNUMBER || !kp_integral64(v)) {
		kp_game_fail(g, "game: %s gave a %s, not an integer", what, luaL_typename(L, k));
	}
	return (int64_t)v;
}

static void call_lua(KpGame *g, const char *method, int argc, const int64_t *args, int nres, int64_t *res)
{
	Native *nv = (Native *)g->ctx;
	lua_State *L = nv->L;
	int top = lua_gettop(L), i, failed;
	KpTry *handler = g->w->try_;
	push_game(L, g);
	lua_getfield(L, -1, method);
	if (lua_isnil(L, -1)) {
		lua_settop(L, top);
		kp_game_fail(g, "game: no method %s", method);
	}
	lua_pushvalue(L, -2);
	for (i = 0; i < argc; i++) lua_pushnumber(L, (lua_Number)args[i]);
	failed = lua_pcall(L, argc + 1, nres, 0) != 0;
	/* A Lua error raised directly inside a nested C method unwinds past its handler. */
	g->w->try_ = handler;
	nv->L = L;
	kp_images_lua_enter(g->w, L);
	if (failed) {
		nv->lua_error = 1;
		kp_images_rethrow(g->w);
	}
	for (i = 0; i < nres; i++) res[i] = lua_value(g, L, top + 2 + i, method);
	lua_settop(L, top);
}

static int64_t read_dat(KpGame *g, const char *table, const char *column, int64_t index)
{
	lua_State *L = ((Native *)g->ctx)->L;
	int top = lua_gettop(L);
	int64_t v;
	push_game(L, g);
	lua_getfield(L, -1, "dat");
	if (lua_istable(L, -1)) lua_getfield(L, -1, table);
	if (lua_istable(L, -1)) lua_getfield(L, -1, column);
	if (!lua_istable(L, -1)) {
		lua_settop(L, top);
		kp_game_fail(g, "game: no DAT column %s.%s", table, column);
	}
	lua_rawgeti(L, -1, (int)index);
	if (lua_type(L, -1) != LUA_TNUMBER) {
		lua_settop(L, top);
		kp_game_fail(g, "game: no DAT value %s.%s[%lld]", table, column, (long long)index);
	}
	v = (int64_t)lua_tonumber(L, -1);
	lua_settop(L, top);
	return v;
}

/* g[k1][i1][k2][i2] (KpLuaGet). */
static int64_t get_lua(KpGame *g, const char *k1, int64_t i1, const char *k2, int64_t i2)
{
	lua_State *L = ((Native *)g->ctx)->L;
	int top = lua_gettop(L);
	int64_t v;
	push_game(L, g);
	if (k1) lua_getfield(L, -1, k1);
	if (i1 != KP_NIL && lua_istable(L, -1)) lua_rawgeti(L, -1, (int)i1);
	if (k2 && lua_istable(L, -1)) lua_getfield(L, -1, k2);
	if (i2 != KP_NIL && lua_istable(L, -1)) lua_rawgeti(L, -1, (int)i2);
	v = lua_value(g, L, -1, k2 ? k2 : k1);
	lua_settop(L, top);
	return v;
}

static const char *const SCALARS[] = { "units_allocated", "error_code", "cloaked_head", NULL };

static int64_t *scalar(KpGame *g, const char *k)
{
	if (!k) return NULL;
	if (strcmp(k, "units_allocated") == 0) return &g->units_allocated;
	if (strcmp(k, "error_code") == 0) return &g->error_code;
	if (strcmp(k, "cloaked_head") == 0) return &g->cloaked_head;
	return NULL;
}

static int is_scalar(const char *k)
{
	int i;
	if (!k) return 0;
	for (i = 0; SCALARS[i]; i++) {
		if (strcmp(k, SCALARS[i]) == 0) return 1;
	}
	return 0;
}

/* g.on_text(player, text) when the game has the callback. */
static void text_lua(KpGame *g, int64_t player, const char *text)
{
	Native *nv = (Native *)g->ctx;
	lua_State *L = nv->L;
	int top = lua_gettop(L), failed;
	KpTry *handler = g->w->try_;
	push_game(L, g);
	lua_getfield(L, -1, "on_text");
	if (lua_isnil(L, -1)) {
		lua_settop(L, top);
		return;
	}
	lua_pushnumber(L, (lua_Number)player);
	lua_pushstring(L, text);
	failed = lua_pcall(L, 2, 0, 0) != 0;
	g->w->try_ = handler;
	nv->L = L;
	kp_images_lua_enter(g->w, L);
	if (failed) {
		nv->lua_error = 1;
		kp_images_rethrow(g->w);
	}
	lua_settop(L, top);
}

/* --- creation --- */

static int native_gc(lua_State *L)
{
	Native *nv = (Native *)lua_touserdata(L, 1);
	kp_game_free(nv->g);
	nv->g = NULL;
	return 0;
}

static int64_t raw_number(lua_State *L, int t, const char *key, int64_t fallback)
{
	int64_t v = fallback;
	lua_getfield(L, t, key); /* the game's own fields, set by game.new */
	if (lua_type(L, -1) == LUA_TNUMBER) v = (int64_t)lua_tonumber(L, -1);
	lua_pop(L, 1);
	return v;
}

#define UNIT_ARRAY(name) \
	kp_bridge_push_array64(L, native, g->u.name, 1, KP_UNITS + 1); \
	lua_setfield(L, -2, #name);

/* Creates the KpGame of the game table at index t and its proxies. */
static KpGame *create(lua_State *L, int t)
{
	KpImages *w;
	KpRngGame *rng;
	KpGame *g;
	Native *nv;
	int native, k;
	lua_getfield(L, t, "img");
	w = kp_images_lua_check(L, -1);
	lua_getfield(L, t, "rng");
	rng = (KpRngGame *)luaL_checkudata(L, -1, KP_LUA_RNG_GAME);
	lua_pop(L, 2);
	g = kp_game_new(w, rng);
	if (!g) luaL_error(L, "game: out of memory");
	nv = (Native *)lua_newuserdata(L, sizeof *nv);
	nv->g = g;
	nv->L = L;
	nv->lua_error = 0;
	native = lua_gettop(L);
	luaL_getmetatable(L, NATIVE_META);
	lua_setmetatable(L, native);
	g->lua = call_lua;
	g->dat = read_dat;
	g->get = get_lua;
	g->text = text_lua;
	g->ctx = nv;
	g->map_w = raw_number(L, t, "map_w", 256);
	g->map_h = raw_number(L, t, "map_h", 256);
	g->map_w_px = raw_number(L, t, "map_w_px", g->map_w * 32);
	g->map_h_px = raw_number(L, t, "map_h_px", g->map_h * 32);
	g->local_player = raw_number(L, t, "local_player", 0);
	g->local_vision = raw_number(L, t, "local_vision", 1);
	g->expansion = raw_number(L, t, "expansion", 0);

	lua_newtable(L);
	KP_UNIT_FIELDS(UNIT_ARRAY)
	lua_setfield(L, t, "u");
	lua_newtable(L);
	for (k = 0; k < KP_LIST_COUNT; k++) {
		kp_bridge_push_list(L, native, &g->lists[k]);
		lua_setfield(L, -2, LIST_NAMES[k]);
	}
	lua_setfield(L, t, "lists");
	kp_bridge_push_array64(L, native, g->player_units, 0, KP_PLAYERS);
	lua_setfield(L, t, "player_units");
	kp_bridge_push_array64(L, native, g->count_all, 0, KP_UNIT_TYPES * KP_PLAYERS);
	lua_setfield(L, t, "count_all");
	kp_bridge_push_array64(L, native, g->count_completed, 0, KP_UNIT_TYPES * KP_PLAYERS);
	lua_setfield(L, t, "count_completed");
	kp_bridge_push_array64(L, native, g->count_kills, 0, KP_UNIT_TYPES * KP_PLAYERS);
	lua_setfield(L, t, "count_kills");
	kp_bridge_push_array64(L, native, g->count_deaths, 0, KP_UNIT_TYPES * KP_PLAYERS);
	lua_setfield(L, t, "count_deaths");
	lua_newtable(L);
	for (k = 0; k < KP_PSTAT_ARRAYS; k++) {
		kp_bridge_push_array64(L, native, g->pstat[k], 0, KP_PLAYERS);
		lua_rawseti(L, -2, k);
	}
	lua_setfield(L, t, "pstat");
	lua_newtable(L);
	for (k = 0; k < 2; k++) {
		kp_bridge_push_array64(L, native, g->strength[k], 0, KP_UNIT_TYPES);
		lua_rawseti(L, -2, k);
	}
	lua_setfield(L, t, "strength");
	/* Fields that are C scalars now leave the table (Game.__index serves them). */
	for (k = 0; SCALARS[k]; k++) {
		lua_pushstring(L, SCALARS[k]);
		lua_rawget(L, t);
		if (lua_type(L, -1) == LUA_TNUMBER) *scalar(g, SCALARS[k]) = (int64_t)lua_tonumber(L, -1);
		lua_pop(L, 1);
		lua_pushstring(L, SCALARS[k]);
		lua_pushnil(L);
		lua_rawset(L, t);
	}

	lua_pushvalue(L, native);
	lua_setfield(L, t, "native");
	lua_getfield(L, LUA_REGISTRYINDEX, GAMES);
	lua_pushlightuserdata(L, g);
	lua_pushvalue(L, t);
	lua_rawset(L, -3);
	lua_settop(L, native - 1);
	return g;
}

/* The KpGame of the game table at index t (created on first use). */
static KpGame *native_of(lua_State *L, int t)
{
	Native *nv;
	lua_getfield(L, t, "native");
	nv = (Native *)lua_touserdata(L, -1);
	lua_pop(L, 1);
	if (nv) return nv->g;
	return create(L, t);
}

/* --- methods --- */

typedef int (*Method)(lua_State *L, KpGame *g);

static int64_t arg(KpGame *g, lua_State *L, int k)
{
	lua_Number v = lua_tonumber(L, k);
	if (lua_type(L, k) != LUA_TNUMBER || !kp_integral64(v)) kp_game_fail(g, "game: argument %d is not an integer", k - 1);
	return (int64_t)v;
}

#define INT(k) arg(g, L, k)
#define UNIT(k) kp_game_unit(g, arg(g, L, k))
#define BOOL(k) lua_toboolean(L, k)

#define V0(name, call) \
	static int m_##name(lua_State *L, KpGame *g) \
	{ \
		(void)L; \
		call; \
		return 0; \
	}
#define V(name, call) \
	static int m_##name(lua_State *L, KpGame *g) \
	{ \
		call; \
		return 0; \
	}
#define I(name, expr) \
	static int m_##name(lua_State *L, KpGame *g) { return kp_push_int(L, expr); }
#define B(name, expr) \
	static int m_##name(lua_State *L, KpGame *g) { return kp_push_bool(L, expr); }

static int union_word(KpGame *g, lua_State *L, int k)
{
	static const char *const NAMES[] = { "union_0xc0", "union_0xc4", "union_0xc8", "union_0xcc", "union_0xd0",
		"union_0xd4", "union_0xd8" };
	const char *name = lua_tostring(L, k);
	int i;
	for (i = 0; name && i < 7; i++) {
		if (strcmp(name, NAMES[i]) == 0) return i;
	}
	kp_game_fail(g, "game: no union word %s", name ? name : "?");
}

#define SPRITE(k) kp_check_sprite(g->w, (int32_t)arg(g, L, k))
#define KIND(k) (arg(g, L, k) == 0 ? 0 : 1)

V0(units_init, kp_units_init(g))
V0(counts_init, kp_counts_init(g))
V(account_live, kp_account_live(g, UNIT(2), INT(3)))
V(account_completed, kp_account_completed(g, UNIT(2), INT(3), BOOL(4)))
V(set_union_unit, kp_set_union_unit(g, UNIT(2), union_word(g, L, 3), INT(4)))
B(unit_is_dead, kp_unit_is_dead(g, UNIT(2)))
I(unit_from_ref, kp_unit_from_ref(g, INT(2)))
B(unit_fits_map, kp_unit_fits_map(g, INT(2), INT(3), INT(4)))
I(max_energy, kp_max_energy(g, UNIT(2)))
V(set_button_set, kp_set_button_set(g, UNIT(2), INT(3)))
I(damage_overlay_slots, kp_unit_damage_overlay_slots(g, UNIT(2)))
B(unit_initialize_type, kp_unit_initialize_type(g, UNIT(2), INT(3), INT(4), INT(5), INT(6)))
B(thingy_init, kp_thingy_init(g, UNIT(2), INT(3), INT(4), INT(5), INT(6), INT(7)))
V(step_unit_sprite, kp_step_unit_sprite(g, UNIT(2)))
B(unit_initialize, kp_unit_initialize(g, UNIT(2), INT(3), INT(4), INT(5), INT(6)))
I(unit_allocate, kp_unit_allocate(g, INT(2), INT(3), INT(4), INT(5)))
I(unit_create, kp_unit_create(g, INT(2), INT(3), INT(4), INT(5)))
V(replace_unit_images, kp_replace_unit_images(g, UNIT(2), BOOL(3)))
V(sprite_replace_main, kp_sprite_replace_main(g, SPRITE(2), INT(3), INT(4)))
V(rebuild_status_overlays, kp_rebuild_status_overlays(g, UNIT(2)))
V(unit_activate_spatial, kp_unit_activate_spatial(g, UNIT(2)))
V(add_building_occupancy, kp_add_building_occupancy(g, UNIT(2)))
V(remove_building_occupancy, kp_remove_building_occupancy(g, UNIT(2)))
V(unit_deactivate_spatial, kp_unit_deactivate_spatial(g, UNIT(2)))
V(reset_movement, kp_reset_movement(g, UNIT(2)))
V(reset_to_idle_animation, kp_reset_to_idle_animation(g, UNIT(2)))
V(unit_show, kp_unit_show(g, UNIT(2)))
V(unit_prefinish, kp_unit_prefinish(g, UNIT(2)))
V(unit_relocate, kp_unit_relocate(g, UNIT(2), INT(3), INT(4)))
B(set_move_target_point, kp_set_move_target_point(g, UNIT(2), INT(3), INT(4)))
B(set_move_target_unit, kp_set_move_target_unit(g, UNIT(2), INT(3)))
B(unit_place, kp_unit_place(g, UNIT(2)))
V(unit_finish_completion, kp_unit_finish_completion(g, UNIT(2)))
B(unit_complete, kp_unit_complete(g, UNIT(2)))
V(unit_set_cloak_state, kp_unit_set_cloak_state(g, UNIT(2), INT(3)))
V(set_unit_hp, kp_set_unit_hp(g, UNIT(2), INT(3)))
V(update_damage_overlays, kp_update_damage_overlays(g, UNIT(2)))
V(damage_overlay_add, kp_damage_overlay_add(g, UNIT(2)))
V(damage_overlay_remove, kp_damage_overlay_remove(g, UNIT(2)))
V0(strength_init, kp_strength_init(g))
I(unit_type_strength, kp_unit_type_strength(g, UNIT(2), KIND(3)))
I(current_life, kp_current_life(g, UNIT(2)))
I(max_hit_points, kp_max_hit_points(g, UNIT(2)))
I(max_life, kp_max_life(g, UNIT(2)))
I(unit_current_strength, kp_unit_current_strength(g, UNIT(2), KIND(3)))
V(refresh_unit_strength, kp_refresh_unit_strength(g, UNIT(2)))

I(create_map_unit, kp_create_map_unit(g, INT(2), INT(3), INT(4), INT(5)))
I(hangar_capacity, kp_hangar_capacity(g, UNIT(2)))
V(create_hangar_units, kp_create_hangar_units(g, UNIT(2), INT(3)))
V(hangar_add, kp_hangar_add(g, UNIT(2), UNIT(3)))
I(cloak_tech, kp_cloak_tech(g, UNIT(2)))
V(cloak_list_add, kp_cloak_list_add(g, UNIT(2)))
B(cloak_list_remove, kp_cloak_list_remove(g, UNIT(2)))
V(cloak_hidden, kp_cloak_hidden(g, UNIT(2)))
V(set_burrowed, kp_set_burrowed(g, UNIT(2)))
I(unit_race, kp_unit_race(g, UNIT(2)))
V(attach_addon, kp_attach_addon(g, UNIT(2), UNIT(3)))
V(lift_off_initial, kp_lift_off_initial(g, UNIT(2)))
V(update_mineral_animation, kp_update_mineral_animation(g, UNIT(2)))
V(unit_disable_doodad, kp_unit_disable_doodad(g, UNIT(2)))

static int64_t record_field(KpGame *g, lua_State *L, const char *key)
{
	int64_t v;
	lua_getfield(L, 3, key);
	v = arg(g, L, lua_gettop(L));
	lua_pop(L, 1);
	return v;
}

static int m_apply_unit_record(lua_State *L, KpGame *g)
{
	int64_t n = UNIT(2);
	KpUnitRecord r;
	if (!lua_istable(L, 3)) kp_game_fail(g, "game: a unit record is a table");
	r.valid_states = record_field(g, L, "valid_states");
	r.valid_properties = record_field(g, L, "valid_properties");
	r.hp_percent = record_field(g, L, "hp_percent");
	r.shield_percent = record_field(g, L, "shield_percent");
	r.energy_percent = record_field(g, L, "energy_percent");
	r.resources = record_field(g, L, "resources");
	r.hangar = record_field(g, L, "hangar");
	r.states = record_field(g, L, "states");
	kp_apply_unit_record(g, n, &r);
	return 0;
}

static int m_clamp_to_map(lua_State *L, KpGame *g)
{
	int64_t x = INT(3), y = INT(4);
	kp_clamp_to_map(g, INT(2), &x, &y);
	kp_push_int(L, x);
	return kp_push_int(L, y) + 1;
}

#define M(name) { #name, m_##name }
static const struct {
	const char *name;
	Method fn;
} METHODS[] = {
	M(units_init), M(counts_init), M(account_live), M(account_completed), M(set_union_unit), M(unit_is_dead),
	M(unit_from_ref), M(unit_fits_map), M(max_energy), M(set_button_set), M(damage_overlay_slots),
	M(unit_initialize_type), M(thingy_init), M(step_unit_sprite), M(unit_initialize), M(unit_allocate),
	M(unit_create), M(replace_unit_images), M(sprite_replace_main), M(rebuild_status_overlays),
	M(unit_activate_spatial), M(add_building_occupancy), M(remove_building_occupancy), M(unit_deactivate_spatial),
	M(reset_movement), M(reset_to_idle_animation), M(unit_show), M(unit_prefinish), M(clamp_to_map),
	M(unit_relocate), M(set_move_target_point), M(set_move_target_unit), M(unit_place), M(unit_finish_completion),
	M(unit_complete), M(unit_set_cloak_state), M(set_unit_hp), M(update_damage_overlays), M(damage_overlay_add),
	M(damage_overlay_remove), M(strength_init), M(unit_type_strength), M(current_life), M(max_hit_points),
	M(max_life), M(unit_current_strength), M(refresh_unit_strength), M(create_map_unit), M(apply_unit_record),
	M(hangar_capacity), M(create_hangar_units), M(hangar_add), M(cloak_tech), M(cloak_list_add), M(cloak_list_remove),
	M(cloak_hidden), M(set_burrowed), M(unit_race), M(attach_addon), M(lift_off_initial), M(update_mineral_animation),
	M(unit_disable_doodad),
};
#undef M

#define METHOD_COUNT ((int)(sizeof METHODS / sizeof METHODS[0]))

static int raise_failure(lua_State *L, KpGame *g)
{
	Native *nv = (Native *)g->ctx;
	if (nv->lua_error) {
		nv->lua_error = 0;
		return lua_error(L);
	}
	return kp_images_lua_raise(g->w, L);
}

/* Every method: METHODS[upvalue 1] on the game table at index 1, under a failure handler. */
static int game_call(lua_State *L)
{
	int index = (int)lua_tonumber(L, lua_upvalueindex(1)), r;
	KpGame *g;
	Native *nv;
	lua_State *saved, *saved_world;
	KpTry t;
	luaL_checktype(L, 1, LUA_TTABLE);
	g = native_of(L, 1);
	nv = (Native *)g->ctx;
	saved = nv->L;
	nv->L = L;
	nv->lua_error = 0;
	saved_world = kp_images_lua_enter(g->w, L);
	kp_images_begin(g->w, &t);
	if (setjmp(t.buf)) {
		kp_images_end(g->w, &t);
		nv->L = saved;
		kp_images_lua_leave(g->w, saved_world);
		return raise_failure(L, g);
	}
	r = METHODS[index].fn(L, g);
	kp_images_end(g->w, &t);
	nv->L = saved;
	kp_images_lua_leave(g->w, saved_world);
	return r;
}

/* --- the Game metatable --- */

/* __index: the C scalars, then the Game table (upvalue 1). */
static int game_index(lua_State *L)
{
	const char *k = lua_type(L, 2) == LUA_TSTRING ? lua_tostring(L, 2) : NULL;
	if (is_scalar(k)) {
		Native *nv;
		lua_getfield(L, 1, "native");
		nv = (Native *)lua_touserdata(L, -1);
		if (!nv) {
			lua_pushnil(L);
			return 1;
		}
		return kp_push_int(L, *scalar(nv->g, k));
	}
	lua_pushvalue(L, 2);
	lua_rawget(L, lua_upvalueindex(1));
	return 1;
}

static int game_newindex(lua_State *L)
{
	const char *k = lua_type(L, 2) == LUA_TSTRING ? lua_tostring(L, 2) : NULL;
	if (is_scalar(k)) {
		KpGame *g = native_of(L, 1);
		lua_Number v = lua_tonumber(L, 3);
		if (lua_type(L, 3) != LUA_TNUMBER || !kp_integral64(v)) return luaL_error(L, "game: %s takes integers", k);
		*scalar(g, k) = (int64_t)v;
		return 0;
	}
	lua_rawset(L, 1);
	return 0;
}

/* install(Game): the C methods and the metatable functions on the Game table. */
static int game_install(lua_State *L)
{
	int k;
	luaL_checktype(L, 1, LUA_TTABLE);
	for (k = 0; k < METHOD_COUNT; k++) {
		lua_pushnumber(L, k);
		lua_pushcclosure(L, game_call, 1);
		lua_setfield(L, 1, METHODS[k].name);
	}
	lua_pushvalue(L, 1);
	lua_pushcclosure(L, game_index, 1);
	lua_setfield(L, 1, "__index");
	lua_pushcfunction(L, game_newindex);
	lua_setfield(L, 1, "__newindex");
	return 0;
}

void kp_game_lua_push(lua_State *L)
{
	luaL_newmetatable(L, NATIVE_META);
	lua_pushcfunction(L, native_gc);
	lua_setfield(L, -2, "__gc");
	lua_pop(L, 1);
	lua_newtable(L);
	lua_newtable(L);
	lua_pushstring(L, "v");
	lua_setfield(L, -2, "__mode");
	lua_setmetatable(L, -2);
	lua_setfield(L, LUA_REGISTRYINDEX, GAMES);
	lua_newtable(L);
	lua_pushcfunction(L, game_install);
	lua_setfield(L, -2, "install");
}
