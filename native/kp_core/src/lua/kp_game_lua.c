/*
 * kp_core.game: the games of the native core (ADR 0008). kp_core.game.new(blobs, options) starts a game
 * (kp_start.c) and returns its table, whose methods are the C functions over the KpGame (kp_game.h) and whose
 * fields serve the C state with the shape the Lua tools, tests and the viewer read:
 *   - g.u[field][n] (array proxies, 1..1700), g.lists.<name>.head/tail, g.player_units[p];
 *   - g.count_all/count_completed/count_kills/count_deaths[i], g.pstat[a][p];
 *   - g.orders, g.ai, g.guards, g.trigger_clock, g.spatial, g.mv, g.recede (record proxies), g.results, the
 *     selections, the tile arrays, the players, g.dat (the game's own DAT, read and written);
 *   - the scalar fields of KP_SCALARS (numbers and booleans, nil before they are set) through the metatable;
 *   - g.img (the game's images world), g.rng, g.crt (the generators).
 * Failures of the C code come back to the calling method as Lua errors.
 */
#include "kp_lua_bridge.h"

#include <stdlib.h>
#include <string.h>

#include "../core/kp_game.h"
#include "../core/kp_images_internal.h"
#include "../core/kp_game_data.h"

#define NATIVE_META "kp_core.game.native"
#define GAMES "kp_core.game.games" /* registry: weak table, light userdata KpGame* -> game table */

typedef struct {
	KpGame *g;
	lua_State *L;
	int lua_error;
	KpGameData data; /* the game's data (the generators are the g.rng and g.crt userdata) */
} Native;

static const char *const LIST_NAMES[KP_LIST_COUNT] = { "active", "pending", "free", "hidden", "special" };

/* The scalar named k, or -1. */
static int scalar_of(const char *k)
{
	int i;
	if (!k) return -1;
	for (i = 0; i < KP_SCALAR_COUNT; i++) {
		if (strcmp(k, KP_SCALARS[i].name) == 0) return i;
	}
	return -1;
}

static void push_scalar(lua_State *L, KpGame *g, int i)
{
	int64_t v = kp_scalar(g, (KpScalar)i);
	if (v == KP_NIL) lua_pushnil(L);
	else if (KP_SCALARS[i].boolean) lua_pushboolean(L, v != 0);
	else kp_push_int(L, v);
}

/* Sets scalar i from the Lua value at k (nil clears it); false when the value does not fit. */
static int set_scalar(lua_State *L, KpGame *g, int i, int k)
{
	if (lua_isnil(L, k)) {
		kp_scalar_clear(g, (KpScalar)i);
	} else if (lua_isboolean(L, k)) {
		kp_scalar_set(g, (KpScalar)i, lua_toboolean(L, k));
	} else {
		lua_Number v = lua_tonumber(L, k);
		if (lua_type(L, k) != LUA_TNUMBER || !kp_integral64(v)) return 0;
		kp_scalar_set(g, (KpScalar)i, (int64_t)v);
	}
	return 1;
}

/* --- creation --- */

static int native_gc(lua_State *L)
{
	Native *nv = (Native *)lua_touserdata(L, 1);
	kp_game_free(nv->g);
	nv->g = NULL;
	kp_game_data_free(&nv->data);
	return 0;
}

#define FIELD_INT(rec, name) { #name, KP_FIELD_INT, &(rec).name, 0, 0 }
#define FIELD_ARRAY(rec, name, count) { #name, KP_FIELD_ARRAY64, (rec).name, 0, count }

/* The state of N.3: the order pool, the selections, the AI, the guards and the trigger clock. */
static void push_n3_proxies(lua_State *L, KpGame *g, int native, int t)
{
	int p, k;
	KpField orders[9], ai[9], guards[12], clock[7];
	orders[0] = (KpField)FIELD_ARRAY(g->orders, prev, KP_ORDER_NODES);
	orders[1] = (KpField)FIELD_ARRAY(g->orders, next, KP_ORDER_NODES);
	orders[2] = (KpField)FIELD_ARRAY(g->orders, order, KP_ORDER_NODES);
	orders[3] = (KpField)FIELD_ARRAY(g->orders, type, KP_ORDER_NODES);
	orders[4] = (KpField)FIELD_ARRAY(g->orders, x, KP_ORDER_NODES);
	orders[5] = (KpField)FIELD_ARRAY(g->orders, y, KP_ORDER_NODES);
	orders[6] = (KpField)FIELD_ARRAY(g->orders, unit, KP_ORDER_NODES);
	orders[7].name = "free";
	orders[7].kind = KP_FIELD_LIST;
	orders[7].ptr = &g->orders.free;
	orders[7].first = orders[7].count = 0;
	orders[8] = (KpField)FIELD_INT(g->orders, allocated);
	kp_bridge_push_record(L, native, orders, 9);
	lua_setfield(L, t, "orders");
	ai[0] = (KpField)FIELD_INT(g->ai, terrain_changed);
	ai[1] = (KpField)FIELD_INT(g->ai, period);
	ai[2] = (KpField)FIELD_INT(g->ai, cycle);
	ai[3] = (KpField)FIELD_INT(g->ai, rounds);
	ai[4] = (KpField)FIELD_INT(g->ai, region_player);
	ai[5] = (KpField)FIELD_INT(g->ai, region_cursor);
	ai[6] = (KpField)FIELD_INT(g->ai, spending_player);
	ai[7] = (KpField)FIELD_INT(g->ai, town_player);
	ai[8] = (KpField)FIELD_INT(g->ai, scripts);
	kp_bridge_push_record(L, native, ai, 9);
	lua_setfield(L, t, "ai");
	guards[0] = (KpField)FIELD_ARRAY(g->guards, next, KP_GUARDS);
	guards[1] = (KpField)FIELD_ARRAY(g->guards, prev, KP_GUARDS);
	guards[2] = (KpField)FIELD_ARRAY(g->guards, kind, KP_GUARDS);
	guards[3] = (KpField)FIELD_ARRAY(g->guards, unit, KP_GUARDS);
	guards[4] = (KpField)FIELD_ARRAY(g->guards, type, KP_GUARDS);
	guards[5] = (KpField)FIELD_ARRAY(g->guards, x1, KP_GUARDS);
	guards[6] = (KpField)FIELD_ARRAY(g->guards, y1, KP_GUARDS);
	guards[7] = (KpField)FIELD_ARRAY(g->guards, x2, KP_GUARDS);
	guards[8] = (KpField)FIELD_ARRAY(g->guards, y2, KP_GUARDS);
	guards[9] = (KpField)FIELD_ARRAY(g->guards, flag9, KP_GUARDS);
	guards[10] = (KpField)FIELD_ARRAY(g->guards, heads, 8);
	guards[11] = (KpField)FIELD_INT(g->guards, free);
	kp_bridge_push_record(L, native, guards, 12);
	lua_setfield(L, t, "guards");
	clock[0] = (KpField)FIELD_INT(g->trigger_clock, second);
	clock[1] = (KpField)FIELD_INT(g->trigger_clock, pass);
	clock[2] = (KpField)FIELD_INT(g->trigger_clock, result);
	clock[3] = (KpField)FIELD_INT(g->trigger_clock, leaderboard);
	clock[4] = (KpField)FIELD_INT(g->trigger_clock, game_timer);
	clock[5] = (KpField)FIELD_INT(g->trigger_clock, countdowns);
	clock[6] = (KpField)FIELD_INT(g->trigger_clock, leaderboard_kind);
	kp_bridge_push_record(L, native, clock, 7);
	lua_setfield(L, t, "trigger_clock");
	kp_bridge_push_array64(L, native, g->results, 0, 8);
	lua_setfield(L, t, "results");
	kp_bridge_push_array64(L, native, g->local_selection, 0, KP_SLOTS);
	lua_setfield(L, t, "local_selection");
	kp_bridge_push_array64(L, native, g->circle_colors, 0, KP_PLAYERS);
	lua_setfield(L, t, "circle_colors");
	lua_newtable(L);
	for (p = 0; p < KP_SELECTION_PLAYERS; p++) {
		kp_bridge_push_array64(L, native, g->selections[p], 0, KP_SLOTS);
		lua_rawseti(L, -2, p);
	}
	lua_setfield(L, t, "selections");
	lua_newtable(L);
	for (p = 0; p < KP_SELECTION_PLAYERS; p++) {
		kp_bridge_push_array64(L, native, g->recent_stamps[p], 0, KP_RECENT);
		lua_rawseti(L, -2, p);
	}
	lua_setfield(L, t, "recent_stamps");
	lua_newtable(L);
	for (p = 0; p < KP_SELECTION_PLAYERS; p++) {
		lua_newtable(L);
		for (k = 0; k < KP_GROUPS; k++) {
			kp_bridge_push_array64(L, native, g->groups[p][k], 0, KP_SLOTS);
			lua_rawseti(L, -2, k);
		}
		lua_rawseti(L, -2, p);
	}
	lua_setfield(L, t, "groups");
}

/* The spatial index, the prepared step and the air density. */
static void push_n4_state(lua_State *L, KpGame *g, int native, int t)
{
	KpField spatial[10], mv[11];
	spatial[0] = (KpField)FIELD_ARRAY(g->spatial, x_unit, KP_SPATIAL_CAPACITY);
	spatial[1] = (KpField)FIELD_ARRAY(g->spatial, x_at, KP_SPATIAL_CAPACITY);
	spatial[2] = (KpField)FIELD_ARRAY(g->spatial, y_unit, KP_SPATIAL_CAPACITY);
	spatial[3] = (KpField)FIELD_ARRAY(g->spatial, y_at, KP_SPATIAL_CAPACITY);
	spatial[4] = (KpField)FIELD_INT(g->spatial, count);
	spatial[5] = (KpField)FIELD_INT(g->spatial, max_w);
	spatial[6] = (KpField)FIELD_INT(g->spatial, max_h);
	spatial[7] = (KpField)FIELD_ARRAY(g->spatial, mark, KP_UNITS + 1);
	spatial[8] = (KpField)FIELD_ARRAY(g->spatial, seen, KP_UNITS + 1);
	spatial[9] = (KpField)FIELD_INT(g->spatial, stamp);
	kp_bridge_push_record(L, native, spatial, 10);
	lua_setfield(L, t, "spatial");
	mv[0] = (KpField)FIELD_INT(g->mv, next_x);
	mv[1] = (KpField)FIELD_INT(g->mv, next_y);
	mv[2] = (KpField)FIELD_INT(g->mv, old_x);
	mv[3] = (KpField)FIELD_INT(g->mv, old_y);
	mv[4] = (KpField)FIELD_INT(g->mv, next_halt_x);
	mv[5] = (KpField)FIELD_INT(g->mv, next_halt_y);
	mv[6] = (KpField)FIELD_INT(g->mv, step);
	mv[7] = (KpField)FIELD_INT(g->mv, walk_start);
	mv[8] = (KpField)FIELD_INT(g->mv, walk_stop);
	mv[9] = (KpField)FIELD_INT(g->mv, flags_before);
	mv[10] = (KpField)FIELD_INT(g->mv, flags_after);
	kp_bridge_push_record(L, native, mv, 11);
	lua_setfield(L, t, "mv");
	kp_bridge_push_array64(L, native, g->collision.density, 0, KP_DENSITY_CELLS);
	lua_setfield(L, t, "density");
}

/* g[key] over the tile array a[0..KP_TILES-1]. */
static void tile_array(lua_State *L, int native, int t, const char *key, int64_t *a)
{
	kp_bridge_push_array64(L, native, a, 0, KP_TILES);
	lua_setfield(L, t, key);
}

/* The tile arrays and the receding creep record. */
static void push_n5_state(lua_State *L, KpGame *g, int native, int t)
{
	KpTerrain *tr = &g->terrain;
	KpField recede[4];
	tile_array(L, native, t, "tiles", tr->tiles);
	tile_array(L, native, t, "megatiles", tr->megatiles);
	tile_array(L, native, t, "raw_tiles", tr->raw_tiles);
	tile_array(L, native, t, "creep_backup", tr->creep_backup);
	tile_array(L, native, t, "creep_border", tr->creep_border);
	recede[0].name = "timer";
	recede[0].kind = KP_FIELD_INT;
	recede[0].ptr = &tr->recede_timer;
	recede[1].name = "free";
	recede[1].kind = KP_FIELD_INT;
	recede[1].ptr = &tr->recede_free;
	recede[2].name = "retry";
	recede[2].kind = KP_FIELD_INT;
	recede[2].ptr = &tr->recede_retry;
	recede[3].name = "buckets";
	recede[3].kind = KP_FIELD_ARRAY64;
	recede[3].ptr = tr->recede_buckets;
	recede[0].first = recede[0].count = recede[1].first = recede[1].count = recede[2].first = recede[2].count = 0;
	recede[3].first = 0;
	recede[3].count = KP_RECEDE_BUCKETS;
	kp_bridge_push_record(L, native, recede, 4);
	lua_setfield(L, t, "recede");
}

/* The players (controllers, races, forces, alliances, shared vision, upgrades, techs) as Lua sees them. */
static void push_players(lua_State *L, KpGame *g, int native, int t)
{
	KpPlayers *pl = &g->players;
	int p;
	lua_newtable(L);
	for (p = 0; p < KP_PLAYERS; p++) {
		KpField f[3];
		f[0].name = "controller";
		f[0].ptr = &pl->controller[p];
		f[1].name = "race";
		f[1].ptr = &pl->race[p];
		f[2].name = "force";
		f[2].ptr = &pl->force[p];
		f[0].kind = f[1].kind = f[2].kind = KP_FIELD_INT;
		f[0].first = f[0].count = f[1].first = f[1].count = f[2].first = f[2].count = 0;
		kp_bridge_push_record(L, native, f, 3);
		lua_rawseti(L, -2, p);
	}
	lua_setfield(L, t, "players");
	lua_newtable(L);
	for (p = 0; p < KP_PLAYERS; p++) {
		kp_bridge_push_array64(L, native, pl->alliance[p], 0, KP_PLAYERS);
		lua_rawseti(L, -2, p);
	}
	lua_setfield(L, t, "alliance");
	kp_bridge_push_array64(L, native, pl->shared_vision, 0, KP_PLAYERS);
	lua_setfield(L, t, "shared_vision");
	lua_newtable(L);
	for (p = 0; p < KP_PLAYERS; p++) {
		kp_bridge_push_array64(L, native, pl->upgrades[p], 0, KP_UPGRADE_IDS);
		lua_rawseti(L, -2, p);
	}
	lua_setfield(L, t, "upgrades");
	lua_newtable(L);
	for (p = 0; p < KP_PLAYERS; p++) {
		lua_createtable(L, 0, 2);
		kp_bridge_push_array64(L, native, pl->tech_available[p], 0, KP_TECH_IDS);
		lua_setfield(L, -2, "available");
		kp_bridge_push_array64(L, native, pl->tech_researched[p], 0, KP_TECH_IDS);
		lua_setfield(L, -2, "researched");
		lua_rawseti(L, -2, p);
	}
	lua_setfield(L, t, "techs");
}

#define UNIT_ARRAY(name) \
	kp_bridge_push_array64(L, native, g->u.name, 1, KP_UNITS + 1); \
	lua_setfield(L, -2, #name);

/* Creates the KpGame of the game table at index t and its proxies. */
/* The views of the C state in the game table at t. */
static void push_views(lua_State *L, KpGame *g, int native, int t)
{
	int k;
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
	push_n3_proxies(L, g, native, t);
	push_n4_state(L, g, native, t);
	push_n5_state(L, g, native, t);
	push_players(L, g, native, t);
}

/* t.native and the registry entry of the game; pops everything above native - 1. */
static void register_game(lua_State *L, KpGame *g, int native, int t)
{
	lua_pushvalue(L, native);
	lua_setfield(L, t, "native");
	lua_getfield(L, LUA_REGISTRYINDEX, GAMES);
	lua_pushlightuserdata(L, g);
	lua_pushvalue(L, t);
	lua_rawset(L, -3);
	lua_settop(L, native - 1);
}

/* The KpGame of the game table at index t (created on first use). */
static KpGame *native_of(lua_State *L, int t)
{
	Native *nv;
	lua_getfield(L, t, "native");
	nv = (Native *)lua_touserdata(L, -1);
	lua_pop(L, 1);
	if (!nv || !nv->g) luaL_error(L, "game: not a kp_core game");
	return nv->g;
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

V0(orders_init, kp_orders_init(g))
V(order_queue, kp_order_queue(g, UNIT(2), INT(3), INT(4), INT(5), INT(6), INT(7)))
V(order_append, kp_order_append(g, UNIT(2), INT(3), INT(4), INT(5), INT(6), INT(7), BOOL(8)))
V(order_insert_after, kp_order_insert_after(g, UNIT(2), INT(3), INT(4), INT(5), INT(6), INT(7), INT(8)))
V(order_insert_before, kp_order_insert_before(g, UNIT(2), INT(3), INT(4), INT(5), INT(6), INT(7), INT(8)))
V(order_remove, kp_order_remove(g, UNIT(2), INT(3)))
V(order_clear_queue, kp_order_clear_queue(g, UNIT(2)))
V(set_order, kp_set_order(g, UNIT(2), INT(3)))
V(set_order_at, kp_set_order_at(g, UNIT(2), INT(3), INT(4), INT(5)))
V(set_order_fow, kp_set_order_fow(g, UNIT(2), INT(3), INT(4), INT(5), INT(6)))
V(set_order_target, kp_set_order_target(g, UNIT(2), INT(3), INT(4)))
V(set_secondary_order, kp_set_secondary_order(g, UNIT(2), INT(3)))
V(order_activate, kp_order_activate(g, UNIT(2)))
V(order_done, kp_order_done(g, UNIT(2)))
V(unit_kill, kp_unit_kill(g, UNIT(2)))
V(issue_order, kp_issue_order(g, UNIT(2), INT(3)))
V(issue_order_at, kp_issue_order_at(g, UNIT(2), INT(3), INT(4), INT(5)))
V(issue_order_fow, kp_issue_order_fow(g, UNIT(2), INT(3), INT(4), INT(5), INT(6)))
V(issue_order_target, kp_issue_order_target(g, UNIT(2), INT(3), INT(4)))
V(issue_order_packed, kp_issue_order_packed(g, UNIT(2), INT(3), INT(4), INT(5), INT(6)))
I(energy_cap, kp_energy_cap(g, UNIT(2)))
V(update_unit_energy, kp_update_unit_energy(g, UNIT(2)))
V(update_acid_spores, kp_update_acid_spores(g, UNIT(2)))
V(update_spell_timers, kp_update_spell_timers(g, UNIT(2)))
V(update_unit_timers, kp_update_unit_timers(g, UNIT(2)))
B(request_move_to_point, kp_request_move_to_point(g, UNIT(2), INT(3), INT(4)))
B(request_move_to_unit, kp_request_move_to_unit(g, UNIT(2), UNIT(3)))
V(set_cloak_bits, kp_set_cloak_bits(g, UNIT(2), INT(3)))
V(dispatch_main_order, kp_dispatch_main_order(g, UNIT(2)))
V(dispatch_hidden_order, kp_dispatch_hidden_order(g, UNIT(2)))
V(dispatch_secondary_order, kp_dispatch_secondary_order(g, UNIT(2)))
V(secondary_cloak_order, kp_secondary_cloak_order(g, UNIT(2), INT(3)))
B(is_enemy, kp_is_enemy(g, UNIT(2), UNIT(3)))
B(is_undetected_by, kp_is_undetected_by(g, UNIT(2), UNIT(3)))
I(ground_weapon, kp_ground_weapon(g, UNIT(2)))
I(ground_weapon_with_subunit, kp_ground_weapon_with_subunit(g, UNIT(2)))
I(air_weapon_with_subunit, kp_air_weapon_with_subunit(g, UNIT(2)))
I(seek_range, kp_seek_range(g, UNIT(2)))
B(can_attack, kp_can_attack(g, UNIT(2), INT(3), BOOL(4)))
I(acquire_target, kp_acquire_target(g, UNIT(2)))
I(acquire_random_target, kp_acquire_random_target(g, UNIT(2)))
B(tech_available, kp_tech_available(g, INT(2), INT(3)))
B(tech_researched, kp_tech_researched(g, INT(2), INT(3)))
I(right_click_action, kp_right_click_action(g, UNIT(2)))
B(unit_can_move, kp_unit_can_move(g, UNIT(2)))
B(unit_can_attack, kp_unit_can_attack(g, UNIT(2)))
I(required_parent_type, kp_required_parent_type(g, INT(2)))
B(unit_disabled, kp_unit_disabled(g, UNIT(2)))
I(can_issue_order, kp_can_issue_order(g, INT(2), INT(3), UNIT(4)))
I(can_use_tech, kp_can_use_tech(g, INT(2), INT(3), UNIT(4)))
V(remove_unit_ai, kp_remove_unit_ai(g, UNIT(2), INT(3)))
V(assign_unit_ai, kp_assign_unit_ai(g, UNIT(2)))
V(change_owner, kp_change_owner(g, UNIT(2), INT(3), BOOL(4)))
V(clear_references, kp_clear_references(g, UNIT(2), BOOL(3)))
V(remove_from_selections, kp_remove_from_selections(g, UNIT(2)))
V(remove_from_local_selection, kp_remove_from_local_selection(g, UNIT(2)))
V(unit_hide, kp_unit_hide(g, UNIT(2)))
V(remove_body, kp_remove_body(g, UNIT(2)))
V(unit_destroy, kp_unit_destroy(g, UNIT(2)))
V0(selection_init, kp_selection_init(g))
I(unit_ref, kp_unit_ref(g, INT(2) == 0 ? 0 : UNIT(2)))
V(add_selection_circle, kp_add_selection_circle(g, UNIT(2), INT(3)))
V(flash_selection, kp_flash_selection(g, UNIT(2)))
V(remove_selection_circle, kp_remove_selection_circle(g, UNIT(2)))
V(move_placement_cursor, kp_move_placement_cursor(g, INT(2), INT(3)))
V(mark_target, kp_mark_target(g, INT(2), INT(3), INT(4)))
B(multi_selectable, kp_multi_selectable(g, UNIT(2)))
V0(step_init, kp_step_init(g))
V0(update_units, kp_update_units(g))
V0(step_objects, kp_step_objects(g))
V0(prime, kp_prime(g))
V0(step_frame, kp_step_frame(g))
V(trigger_clock_init, kp_trigger_clock_init(g, BOOL(2)))
V0(trigger_step, kp_trigger_step(g))
V0(ai_init, kp_ai_init(g))
V0(ai_step, kp_ai_step(g))
V0(guard_init, kp_guard_init(g))
V(guard_attach, kp_guard_attach(g, UNIT(2)))
I(upgrade_level, kp_upgrade_level(g, INT(2), INT(3)))
B(is_morphing_zerg_building, kp_is_morphing_zerg_building(g, UNIT(2)))
V(apply_creation_upgrades, kp_apply_creation_upgrades(g, UNIT(2)))

/* A Lua list of integers at k (at most max). */
static int arg_list(KpGame *g, lua_State *L, int k, int64_t *out, int max)
{
	int count, i;
	if (!lua_istable(L, k)) kp_game_fail(g, "game: argument %d is not a list", k - 1);
	count = (int)lua_objlen(L, k);
	if (count > max) kp_game_fail(g, "game: a list of %d values (at most %d)", count, max);
	for (i = 0; i < count; i++) {
		lua_rawgeti(L, k, i + 1);
		out[i] = arg(g, L, lua_gettop(L));
		lua_pop(L, 1);
	}
	return count;
}

static int push_list(lua_State *L, const int64_t *v, int count)
{
	int i;
	lua_createtable(L, count, 0);
	for (i = 0; i < count; i++) {
		kp_push_int(L, v[i]);
		lua_rawseti(L, -2, i + 1);
	}
	return 1;
}

static int m_select_locally(lua_State *L, KpGame *g)
{
	int64_t units[KP_SLOTS + 1];
	int count = arg_list(g, L, 2, units, KP_SLOTS);
	kp_select_locally(g, units, count);
	return 0;
}

static int m_command_select(lua_State *L, KpGame *g)
{
	int64_t refs[256];
	int count;
	if (lua_istable(L, 3) && lua_objlen(L, 3) > KP_SLOTS) return 0; /* the command ignores longer lists */
	count = arg_list(g, L, 3, refs, 256);
	kp_command_select(g, INT(2), refs, count);
	return 0;
}

static int m_selected_units(lua_State *L, KpGame *g)
{
	int64_t units[KP_SLOTS];
	return push_list(L, units, kp_selected_units(g, INT(2), units));
}

static int m_execute_command(lua_State *L, KpGame *g)
{
	size_t size;
	const char *bytes = luaL_checklstring(L, 3, &size);
	kp_execute_command(g, INT(2), (const uint8_t *)bytes, size);
	return 0;
}

/* The command list {{player =, bytes =}, ...} at k as KpCommands (pointing into the Lua strings,
 * which the list keeps alive during the call). */
static int arg_commands(KpGame *g, lua_State *L, int k, KpCommand *out, int max)
{
	int count, i;
	if (lua_isnoneornil(L, k)) return 0;
	if (!lua_istable(L, k)) kp_game_fail(g, "game: the commands are not a list");
	count = (int)lua_objlen(L, k);
	if (count > max) kp_game_fail(g, "game: %d commands in a turn", count);
	for (i = 0; i < count; i++) {
		lua_rawgeti(L, k, i + 1);
		lua_getfield(L, -1, "player");
		out[i].player = arg(g, L, lua_gettop(L));
		lua_getfield(L, -2, "bytes");
		out[i].bytes = (const uint8_t *)lua_tolstring(L, -1, &out[i].size);
		if (!out[i].bytes) kp_game_fail(g, "game: a command without bytes");
		lua_pop(L, 3);
	}
	return count;
}

#define MAX_COMMANDS 1024

static int m_run_turn(lua_State *L, KpGame *g)
{
	KpCommand commands[MAX_COMMANDS];
	kp_run_turn(g, commands, arg_commands(g, L, 2, commands, MAX_COMMANDS));
	return 0;
}

static int m_run_frame(lua_State *L, KpGame *g)
{
	KpCommand commands[MAX_COMMANDS];
	kp_run_frame(g, commands, arg_commands(g, L, 2, commands, MAX_COMMANDS));
	return 0;
}

static void push_formation(lua_State *L, const KpFormation *f)
{
	lua_createtable(L, 0, 11);
#define F(name) kp_push_int(L, f->name); lua_setfield(L, -2, #name);
	F(x) F(y) F(goal_x) F(goal_y) F(ground_x) F(ground_y) F(dx) F(dy) F(area) F(radius) F(flags)
#undef F
}

static int m_group_formation(lua_State *L, KpGame *g)
{
	KpFormation f;
	kp_group_formation(g, INT(2), INT(3), BOOL(4), &f);
	push_formation(L, &f);
	return 1;
}

static int m_formation_point(lua_State *L, KpGame *g)
{
	KpFormation f;
	int64_t x, y;
	if (!lua_istable(L, 3)) kp_game_fail(g, "game: a formation is a table");
#define F(name) lua_getfield(L, 3, #name); f.name = arg(g, L, lua_gettop(L)); lua_pop(L, 1);
	F(x) F(y) F(goal_x) F(goal_y) F(ground_x) F(ground_y) F(dx) F(dy) F(area) F(radius) F(flags)
#undef F
	kp_formation_point(g, UNIT(2), &f, &x, &y);
	kp_push_int(L, x);
	return kp_push_int(L, y) + 1;
}

static int game_call(lua_State *L);
static int handler_method; /* METHODS index of __order_handler_run, set by install */

/* The handler of a ported order, called as handler(game, unit), or nil: the trampoline over
 * __order_handler_run with the order as its second upvalue. */
static int m_order_handler(lua_State *L, KpGame *g)
{
	int64_t order = INT(2);
	if (!kp_order_has_handler(order)) {
		lua_pushnil(L);
		return 1;
	}
	lua_pushnumber(L, handler_method);
	lua_pushnumber(L, (lua_Number)order);
	lua_pushcclosure(L, game_call, 2);
	return 1;
}

static int m___order_handler_run(lua_State *L, KpGame *g)
{
	kp_order_handler_run(g, (int64_t)lua_tonumber(L, lua_upvalueindex(2)), UNIT(2));
	return 0;
}

static int m_clamp_to_map(lua_State *L, KpGame *g)
{
	int64_t x = INT(3), y = INT(4);
	kp_clamp_to_map(g, INT(2), &x, &y);
	kp_push_int(L, x);
	return kp_push_int(L, y) + 1;
}

/* N.4: spatial index, flingy, flingy steps and movement states */
V0(spatial_clear, kp_spatial_clear(g))
V(spatial_insert, kp_spatial_insert(g, UNIT(2)))
V(spatial_remove, kp_spatial_remove(g, UNIT(2)))
V(spatial_move, kp_spatial_move(g, UNIT(2), INT(3), INT(4)))
B(spatial_overlap, kp_spatial_overlap(g, UNIT(2), UNIT(3)))
V(update_overlap_flags, kp_update_overlap_flags(g, UNIT(2)))
B(unit_box_within, kp_unit_box_within(g, UNIT(2), INT(3), UNIT(4)))
I(speed_modified, kp_speed_modified(g, UNIT(2), INT(3)))
I(top_speed_modified, kp_top_speed_modified(g, UNIT(2), INT(3)))
I(acceleration_modified, kp_acceleration_modified(g, UNIT(2)))
I(turn_radius_modified, kp_turn_radius_modified(g, UNIT(2)))
V(iscript_walk_speed, kp_iscript_walk_speed(g, UNIT(2)))
V(recalculate_speed, kp_recalculate_speed(g, UNIT(2)))
V(set_speed_upgrade, kp_set_speed_upgrade(g, UNIT(2)))
B(flingy_init, kp_flingy_init(g, UNIT(2), INT(3), INT(4), INT(5), INT(6), INT(7)))
V(flingy_set_heading, kp_flingy_set_heading(g, UNIT(2), INT(3)))
V(flingy_set_position, kp_flingy_set_position(g, UNIT(2), INT(3), INT(4)))
V(flingy_set_exact_position, kp_flingy_set_exact_position(g, UNIT(2), INT(3), INT(4)))
V(flingy_set_target_waypoint, kp_flingy_set_target_waypoint(g, UNIT(2), INT(3), INT(4)))
V(flingy_set_move_target, kp_flingy_set_move_target(g, UNIT(2), INT(3), INT(4)))
V(flingy_set_move_target_unit, kp_flingy_set_move_target_unit(g, UNIT(2), INT(3) == 0 ? 0 : UNIT(3)))
V(flingy_to_idle, kp_flingy_to_idle(g, UNIT(2)))
V(flingy_set_speed, kp_flingy_set_speed(g, UNIT(2), INT(3)))
V(flingy_turn, kp_flingy_turn(g, UNIT(2), INT(3)))
V(flingy_set_spawn_frame, kp_flingy_set_spawn_frame(g, UNIT(2), INT(3)))
V(nobrk_end, kp_nobrk_end(g, UNIT(2)))
I(approx_distance, kp_approx_distance(INT(2) - INT(4), INT(3) - INT(5)))
V0(move_context_init, kp_move_context_init(g))
V(flingy_set_current_speed, kp_flingy_set_current_speed(g, UNIT(2), INT(3)))
I(flingy_halt_distance, kp_flingy_halt_distance(g, UNIT(2)))
I(direction_to, kp_direction_to(g, INT(2), INT(3), INT(4), INT(5)))
V(flingy_turn_velocity, kp_flingy_turn_velocity(g, UNIT(2)))
V(flingy_update_desired, kp_flingy_update_desired(g, UNIT(2)))
V(flingy_progress_speed, kp_flingy_progress_speed(g, UNIT(2)))
V(flingy_update_walking, kp_flingy_update_walking(g, UNIT(2)))
V(prepare_flingy_movement, kp_prepare_flingy_movement(g, UNIT(2)))
V(prepare_movement_in_direction, kp_prepare_movement_in_direction(g, UNIT(2), INT(3)))
V(flingy_turn_heading, kp_flingy_turn_heading(g, UNIT(2)))
V(flingy_commit_blocked, kp_flingy_commit_blocked(g, UNIT(2)))
V(flingy_recompute_velocity, kp_flingy_recompute_velocity(g, UNIT(2)))
V(flingy_commit_turn, kp_flingy_commit_turn(g, UNIT(2)))
V(prepare_movement_toward, kp_prepare_movement_toward(g, UNIT(2), INT(3), INT(4)))
B(commit_unit_movement, kp_commit_unit_movement(g, UNIT(2)))
I(move_arrival, kp_move_arrival(g, UNIT(2)))
I(try_move_to_legal, kp_try_move_to_legal(g, UNIT(2)))
V(stop_at_target, kp_stop_at_target(g, UNIT(2)))
B(update_movement_state, kp_update_movement_state(g, UNIT(2), BOOL(3)))
V(execute_movement, kp_execute_movement(g, UNIT(2)))
V(stop_flingy, kp_stop_flingy(g, UNIT(2)))
V(stop_movement, kp_stop_movement(g, UNIT(2)))
V(prepare_movement, kp_prepare_movement(g, UNIT(2)))

#define QUERY_MAX (KP_UNITS + 1)

static int m_spatial_query(lua_State *L, KpGame *g)
{
	int64_t out[QUERY_MAX];
	return push_list(L, out, kp_spatial_query(g, INT(2), INT(3), INT(4), INT(5), out, QUERY_MAX));
}

static int m_units_in_rect(lua_State *L, KpGame *g)
{
	int64_t out[QUERY_MAX];
	return push_list(L, out, kp_units_in_rect(g, INT(2), INT(3), INT(4), INT(5), out, QUERY_MAX));
}

static int m_unit_rect(lua_State *L, KpGame *g)
{
	int64_t l, t, r, b;
	kp_unit_rect(g, UNIT(2), &l, &t, &r, &b);
	kp_push_int(L, l);
	kp_push_int(L, t);
	kp_push_int(L, r);
	return kp_push_int(L, b) + 3;
}

static int m_forecast_position(lua_State *L, KpGame *g)
{
	int64_t x, y;
	kp_forecast_position(g, UNIT(2), INT(3), &x, &y);
	kp_push_int(L, x);
	return kp_push_int(L, y) + 1;
}

/* N.4: path pool, region graph, placement, collision and air repulsion */
V0(paths_init, kp_paths_init(g))
V0(regions_init, kp_regions_init(g))
V0(local_geometry_init, kp_local_geometry_init(g))
I(path_u8, kp_path_u8(g, INT(2), INT(3)))
I(path_s8, kp_path_s8(g, INT(2), INT(3)))
I(path_u16, kp_path_u16(g, INT(2), INT(3)))
I(path_s16, kp_path_s16(g, INT(2), INT(3)))
I(path_u32, kp_path_u32(g, INT(2), INT(3)))
V(path_w8, kp_path_w8(g, INT(2), INT(3), INT(4)))
V(path_w16, kp_path_w16(g, INT(2), INT(3), INT(4)))
V(path_w32, kp_path_w32(g, INT(2), INT(3), INT(4)))
V(path_set_point, kp_path_set_point(g, INT(2), INT(3), INT(4), INT(5)))
I(path_alloc, kp_path_alloc(g, INT(2)))
V(path_release, kp_path_release(g, INT(2)))
V(path_free, kp_path_free(g, UNIT(2)))
V(path_mark_stale, kp_path_mark_stale(g, UNIT(2)))
V(path_set_retry, kp_path_set_retry(g, UNIT(2), INT(3)))
B(path_poll_retry, kp_path_poll_retry(g, UNIT(2)))
I(path_next_packed, kp_path_next_packed(g, INT(2)))
I(region_group_of, kp_region_group_of(g, INT(2)))
B(same_ground_group, kp_same_ground_group(g, UNIT(2), UNIT(3)))
B(region_inaccessible, kp_region_inaccessible(g, INT(2)))
I(reachable_substitute_region, kp_reachable_substitute_region(g, INT(2), INT(3), INT(4), INT(5)))
I(tile_region_first, kp_tile_region_first(g, INT(2), INT(3)))
V(path_set_direct, kp_path_set_direct(g, UNIT(2), INT(3), INT(4), INT(5), INT(6)))
V(move_to_intercept, kp_move_to_intercept(g, UNIT(2), INT(3), INT(4)))
B(path_keeps_waypoint, kp_path_keeps_waypoint(g, UNIT(2)))
V(set_path_waypoint, kp_set_path_waypoint(g, UNIT(2), INT(3), INT(4)))
B(advance_path_waypoint, kp_advance_path_waypoint(g, UNIT(2), INT(3), INT(4)))
B(path_obstacle, kp_path_obstacle(g, UNIT(2), UNIT(3)))
B(units_collide, kp_units_collide(g, UNIT(2), UNIT(3)))
I(region_group, kp_region_group(g, INT(2)))
V(contour_extents, kp_contour_extents(g, INT(2)))
B(contour_fits, kp_contour_fits(g, INT(2), INT(3)))
B(point_walkable, kp_point_walkable(g, INT(2), INT(3)))
B(unit_type_fits, kp_unit_type_fits(g, INT(2), INT(3), INT(4)))
B(box_fits, kp_box_fits(g, INT(2), INT(3), INT(4), INT(5)))
I(placement_blocker, kp_placement_blocker(g, UNIT(2), INT(3), INT(4), INT(5), INT(6)))
I(region_at, kp_region_at(g, INT(2), INT(3)))
B(placement_reachable, kp_placement_reachable(g, UNIT(2), INT(3), INT(4)))
V(air_density_add, kp_air_density_add(g, UNIT(2)))
V(air_density_remove, kp_air_density_remove(g, UNIT(2)))
B(air_repulsion, kp_air_repulsion(g, UNIT(2)))
V(air_repulsion_settle, kp_air_repulsion_settle(g, UNIT(2), BOOL(3)))
I(ground_unit_collision_any, kp_ground_unit_collision_any(g, UNIT(2)))
I(ground_unit_collision, kp_ground_unit_collision(g, UNIT(2)))
B(terrain_collision, kp_terrain_collision(g, UNIT(2)))
B(terrain_sweep_collision, kp_terrain_sweep_collision(g, UNIT(2), INT(3), INT(4)))
V(contract_safe_rect, kp_contract_safe_rect(g, UNIT(2), INT(3), INT(4)))
I(position_blocked, kp_position_blocked(g, UNIT(2), INT(3), INT(4)))
B(footprint_touches, kp_footprint_touches(g, UNIT(2), INT(3), INT(4), INT(5), INT(6), INT(7)))
I(cardinal_side, kp_cardinal_side(g, UNIT(2), UNIT(3)))
B(blocker_heading_away, kp_blocker_heading_away(g, UNIT(2), UNIT(3)))
I(terrain_slide_direction, kp_terrain_slide_direction(g, UNIT(2)))
I(slide_direction, kp_slide_direction(g, UNIT(2), UNIT(3)))
B(sweep_contours_clear, kp_sweep_contours_clear(g, INT(2), INT(3), INT(4), INT(5)))

static int m_path_point(lua_State *L, KpGame *g)
{
	int64_t x, y;
	kp_path_point(g, INT(2), INT(3), &x, &y);
	kp_push_int(L, x);
	return kp_push_int(L, y) + 1;
}

static int m_region_box(lua_State *L, KpGame *g)
{
	int64_t l, t, r, b;
	kp_region_box(g, INT(2), &l, &t, &r, &b);
	kp_push_int(L, l);
	kp_push_int(L, t);
	kp_push_int(L, r);
	return kp_push_int(L, b) + 3;
}

static int m_type_box(lua_State *L, KpGame *g)
{
	int64_t l, t, r, b;
	kp_type_box(g, INT(2), INT(3), INT(4), &l, &t, &r, &b);
	kp_push_int(L, l);
	kp_push_int(L, t);
	kp_push_int(L, r);
	return kp_push_int(L, b) + 3;
}

static int m_region_ground_neighbors(lua_State *L, KpGame *g)
{
	int64_t out[QUERY_MAX];
	return push_list(L, out, kp_region_ground_neighbors(g, INT(2), out, QUERY_MAX));
}

static int m_units_swept(lua_State *L, KpGame *g)
{
	return push_list(L, g->collision.swept,
		kp_units_swept(g, UNIT(2), INT(3), INT(4), g->collision.swept, KP_COLLISION_SWEPT_MAX));
}

static int m_units_adjacent(lua_State *L, KpGame *g)
{
	return push_list(L, g->collision.adjacent,
		kp_units_adjacent(g, UNIT(2), INT(3), INT(4), g->collision.adjacent, KP_COLLISION_ADJACENT_MAX));
}

static int m_moving_neighbors(lua_State *L, KpGame *g)
{
	int64_t out[QUERY_MAX];
	return push_list(L, out, kp_moving_neighbors(g, UNIT(2), INT(3), INT(4), out, QUERY_MAX));
}

static int m_units_at_point(lua_State *L, KpGame *g)
{
	int64_t out[QUERY_MAX];
	return push_list(L, out, kp_units_at_point(g, UNIT(2), INT(3), INT(4), out, QUERY_MAX));
}

static int m_next_path_point(lua_State *L, KpGame *g)
{
	int64_t x, y;
	if (!kp_next_path_point(g, UNIT(2), INT(3), INT(4), &x, &y)) {
		lua_pushnil(L);
		return 1;
	}
	kp_push_int(L, x);
	return kp_push_int(L, y) + 1;
}

/* find_unit_placement(n, x, y[, rect[, displace[, extra]]]): the place, or nil. */
static int m_find_unit_placement(lua_State *L, KpGame *g)
{
	int64_t rect[4], x, y;
	int k, found, has_rect = lua_istable(L, 5);
	if (has_rect) {
		for (k = 0; k < 4; k++) {
			lua_rawgeti(L, 5, k + 1);
			rect[k] = arg(g, L, lua_gettop(L));
			lua_pop(L, 1);
		}
	}
	found = kp_find_unit_placement(g, UNIT(2), INT(3), INT(4), has_rect ? rect : NULL, BOOL(6), BOOL(7), &x, &y);
	if (has_rect) { /* the rectangle is clamped in place */
		for (k = 0; k < 4; k++) {
			kp_push_int(L, rect[k]);
			lua_rawseti(L, 5, k + 1);
		}
	}
	if (!found) {
		lua_pushnil(L);
		return 1;
	}
	kp_push_int(L, x);
	return kp_push_int(L, y) + 1;
}

static int m_adjust_move_destination(lua_State *L, KpGame *g)
{
	int64_t x, y;
	kp_adjust_move_destination(g, UNIT(2), INT(3), INT(4), &x, &y);
	kp_push_int(L, x);
	return kp_push_int(L, y) + 1;
}

static int m_adjust_group_destination(lua_State *L, KpGame *g)
{
	int64_t x, y;
	kp_adjust_group_destination(g, INT(2), INT(3), INT(4), INT(5), INT(6), INT(7), &x, &y);
	kp_push_int(L, x);
	return kp_push_int(L, y) + 1;
}

/* N.5: tile flags, line of sight, detection and creep */
#define OPTINT(k) (lua_isnoneornil(L, k) ? 0 : INT(k))
V0(fog_init, kp_fog_init(g))
V0(visibility_bits_set, kp_visibility_bits_set(g))
V0(reveal_cache_flush, kp_reveal_cache_flush(g))
V(tiles_or, kp_tiles_or(g, INT(2), INT(3), INT(4), INT(5), INT(6)))
V(tiles_clear, kp_tiles_clear(g, INT(2), INT(3), INT(4), INT(5), INT(6)))
I(terrain_height, kp_terrain_height(g, INT(2), INT(3)))
V(reveal_sight, kp_reveal_sight(g, INT(2), INT(3), BOOL(4), INT(5), INT(6)))
V0(refresh_remembered_terrain, kp_refresh_remembered_terrain(g))
V0(reset_tile_visibility, kp_reset_tile_visibility(g))
I(unit_sight, kp_unit_sight(g, UNIT(2), BOOL(3)))
B(unit_has_visible_cargo, kp_unit_has_visible_cargo(g, UNIT(2)))
B(is_transport, kp_is_transport(g, UNIT(2)))
V(unit_update_vision, kp_unit_update_vision(g, UNIT(2)))
V(unit_update_sprite_visibility, kp_unit_update_sprite_visibility(g, UNIT(2)))
B(is_detector, kp_is_detector(g, UNIT(2)))
B(has_status_effect, kp_has_status_effect(g, UNIT(2)))
I(detection_mask, kp_detection_mask(g, UNIT(2)))
V(update_detection, kp_update_detection(g, UNIT(2)))
V(forget_target, kp_forget_target(g, UNIT(2), INT(3)))
V(clear_lost_targets, kp_clear_lost_targets(g, INT(2), INT(3)))
V(begin_cloak, kp_begin_cloak(g, UNIT(2), OPTINT(3)))
V0(creep_init, kp_creep_init(g))
V0(creep_recede_tick, kp_creep_recede_tick(g))
V(receding_neighbours, kp_receding_neighbours(g, INT(2), INT(3)))
V(creep_apply, kp_creep_apply(g, INT(2), INT(3), INT(4), OPTINT(5)))
V(creep_refresh_borders, kp_creep_refresh_borders(g, INT(2), INT(3)))
B(tile_cannot_have_creep, kp_tile_cannot_have_creep(g, INT(2), INT(3)))
I(creep_spread_once, kp_creep_spread_once(g, INT(2), INT(3), INT(4)))
V(creep_place_initial, kp_creep_place_initial(g, INT(2), INT(3), INT(4)))
I(megatile_at, kp_megatile_at(g, INT(2)))
I(cv5_megatile, kp_cv5_megatile(g, INT(2)))
I(minitile_flags, kp_minitile_flags(g, INT(2), INT(3), INT(4)))

static int m_creep_bounds(lua_State *L, KpGame *g)
{
	int64_t l, t, r, b;
	int large = kp_creep_bounds(g, INT(2), INT(3), INT(4), BOOL(5), &l, &t, &r, &b);
	kp_push_int(L, l);
	kp_push_int(L, t);
	kp_push_int(L, r);
	kp_push_int(L, b);
	return kp_push_bool(L, large) + 4;
}

/* events(out): the events since the last call, drained in emission order into the array out (reused by the caller,
 * no table per event): KP_EVENT_FIELDS values per event from out[1] (kind, frame, sequence, a, b, c, the text or
 * false); returns the number of events. */
#define KP_EVENT_FIELDS 7
static int m_events(lua_State *L, KpGame *g)
{
	KpEvent e;
	int at = 0, count = 0;
	luaL_checktype(L, 2, LUA_TTABLE);
	while (kp_event_take(g, &e)) {
		kp_push_int(L, e.kind);
		lua_rawseti(L, 2, ++at);
		kp_push_int(L, e.frame);
		lua_rawseti(L, 2, ++at);
		kp_push_int(L, e.seq);
		lua_rawseti(L, 2, ++at);
		kp_push_int(L, e.a);
		lua_rawseti(L, 2, ++at);
		kp_push_int(L, e.b);
		lua_rawseti(L, 2, ++at);
		kp_push_int(L, e.c);
		lua_rawseti(L, 2, ++at);
		if (e.text) lua_pushstring(L, e.text);
		else lua_pushboolean(L, 0);
		lua_rawseti(L, 2, ++at);
		count++;
	}
	lua_pushinteger(L, count);
	return 1;
}

typedef struct {
	lua_State *L;
	int count;
} DumpOut;

static void dump_line(void *ud, const char *line, size_t length)
{
	DumpOut *o = (DumpOut *)ud;
	lua_pushlstring(o->L, line, length);
	lua_rawseti(o->L, -2, ++o->count);
}

/* dump([drawn]): the state lines of tools/native_state.py (drawn leaves out the image x, y). */
static int m_dump(lua_State *L, KpGame *g)
{
	DumpOut o;
	int drawn = lua_toboolean(L, 2);
	o.L = L;
	o.count = 0;
	lua_newtable(L);
	if (kp_dump(g, drawn, dump_line, &o) != 0) kp_game_fail(g, "game: out of memory in dump");
	return 1;
}

/* save(mission, data): the save of the game (format version 1) as a string; mission names the mission and data the
 * identity of the converted data, both checked by load. */
static int m_save(lua_State *L, KpGame *g)
{
	uint8_t *bytes;
	size_t size;
	KpError err;
	const char *mission = luaL_checkstring(L, 2), *data = luaL_optstring(L, 3, "");
	if (kp_save(g, mission, data, &bytes, &size, &err) != 0) kp_game_fail(g, "%s", err.msg);
	lua_pushlstring(L, (const char *)bytes, size);
	free(bytes);
	return 1;
}

/* load(bytes, mission, data): restores a save into this game, of the same mission fresh from its start. */
static int m_load(lua_State *L, KpGame *g)
{
	size_t size;
	KpError err;
	const char *bytes = luaL_checklstring(L, 2, &size), *mission = luaL_checkstring(L, 3), *data = luaL_optstring(L, 4, "");
	if (kp_load(g, (const uint8_t *)bytes, size, mission, data, &err) != 0) kp_game_fail(g, "%s", err.msg);
	lua_settop(L, 1);
	return 1;
}

static void crc_field(void *ud, const char *category, uint32_t crc)
{
	lua_State *L = (lua_State *)ud;
	lua_pushnumber(L, (lua_Number)crc);
	lua_setfield(L, -2, category);
}

/* dump_crcs([drawn]): { [category] = CRC-32 } of the dump lines (as tools/lua/oracle.lua crcs of dump()). */
static int m_dump_crcs(lua_State *L, KpGame *g)
{
	int drawn = lua_toboolean(L, 2);
	lua_newtable(L);
	if (kp_dump_crcs(g, drawn, crc_field, L) != 0) kp_game_fail(g, "game: dump_crcs failed");
	return 1;
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
	M(unit_disable_doodad), M(orders_init), M(order_queue), M(order_append), M(order_insert_after),
	M(order_insert_before), M(order_remove), M(order_clear_queue), M(set_order), M(set_order_at), M(set_order_fow),
	M(set_order_target), M(set_secondary_order), M(order_activate), M(order_done), M(unit_kill), M(issue_order),
	M(issue_order_at), M(issue_order_fow), M(issue_order_target), M(issue_order_packed), M(energy_cap),
	M(update_unit_energy), M(update_acid_spores), M(update_spell_timers), M(update_unit_timers),
	M(request_move_to_point), M(request_move_to_unit), M(set_cloak_bits), M(dispatch_main_order),
	M(dispatch_hidden_order), M(dispatch_secondary_order), M(secondary_cloak_order), M(is_enemy),
	M(is_undetected_by), M(ground_weapon), M(ground_weapon_with_subunit), M(air_weapon_with_subunit),
	M(seek_range), M(can_attack), M(acquire_target), M(acquire_random_target), M(tech_available),
	M(tech_researched), M(right_click_action), M(unit_can_move), M(unit_can_attack), M(required_parent_type),
	M(unit_disabled), M(can_issue_order), M(can_use_tech), M(remove_unit_ai), M(assign_unit_ai), M(change_owner),
	M(clear_references), M(remove_from_selections), M(remove_from_local_selection), M(unit_hide), M(remove_body),
	M(unit_destroy), M(selection_init), M(unit_ref), M(add_selection_circle), M(flash_selection),
	M(remove_selection_circle), M(move_placement_cursor), M(mark_target), M(multi_selectable), M(step_init),
	M(update_units), M(step_objects), M(prime), M(step_frame), M(trigger_clock_init), M(trigger_step), M(ai_init),
	M(ai_step), M(guard_init), M(guard_attach), M(upgrade_level), M(is_morphing_zerg_building),
	M(apply_creation_upgrades), M(select_locally), M(command_select), M(selected_units), M(execute_command),
	M(run_turn), M(run_frame), M(group_formation), M(formation_point), M(order_handler),
	M(spatial_clear), M(spatial_insert), M(spatial_remove), M(spatial_move), M(spatial_overlap), M(update_overlap_flags), M(unit_box_within), M(speed_modified), M(top_speed_modified), M(acceleration_modified), M(turn_radius_modified), M(iscript_walk_speed), M(recalculate_speed), M(set_speed_upgrade), M(flingy_init), M(flingy_set_heading), M(flingy_set_position), M(flingy_set_exact_position), M(flingy_set_target_waypoint), M(flingy_set_move_target), M(flingy_set_move_target_unit), M(flingy_to_idle), M(flingy_set_speed), M(flingy_turn), M(flingy_set_spawn_frame), M(nobrk_end), M(approx_distance), M(move_context_init), M(flingy_set_current_speed), M(flingy_halt_distance), M(direction_to), M(flingy_turn_velocity), M(flingy_update_desired), M(flingy_progress_speed), M(flingy_update_walking), M(prepare_flingy_movement), M(prepare_movement_in_direction), M(flingy_turn_heading), M(flingy_commit_blocked), M(flingy_recompute_velocity), M(flingy_commit_turn), M(prepare_movement_toward), M(commit_unit_movement), M(move_arrival), M(try_move_to_legal), M(stop_at_target), M(update_movement_state), M(execute_movement), M(stop_flingy), M(stop_movement), M(prepare_movement), M(spatial_query), M(units_in_rect), M(unit_rect), M(forecast_position), M(paths_init), M(regions_init), M(local_geometry_init), M(path_u8), M(path_s8), M(path_u16), M(path_s16), M(path_u32), M(path_w8), M(path_w16), M(path_w32), M(path_set_point), M(path_alloc), M(path_release), M(path_free), M(path_mark_stale), M(path_set_retry), M(path_poll_retry), M(path_next_packed), M(region_group_of), M(same_ground_group), M(region_inaccessible), M(reachable_substitute_region), M(tile_region_first), M(path_set_direct), M(move_to_intercept), M(path_keeps_waypoint), M(set_path_waypoint), M(advance_path_waypoint), M(path_obstacle), M(units_collide), M(region_group), M(contour_extents), M(contour_fits), M(point_walkable), M(unit_type_fits), M(box_fits), M(placement_blocker), M(region_at), M(placement_reachable), M(air_density_add), M(air_density_remove), M(air_repulsion), M(air_repulsion_settle), M(ground_unit_collision_any), M(ground_unit_collision), M(terrain_collision), M(terrain_sweep_collision), M(contract_safe_rect), M(position_blocked), M(footprint_touches), M(cardinal_side), M(blocker_heading_away), M(terrain_slide_direction), M(slide_direction), M(sweep_contours_clear), M(path_point), M(region_box), M(type_box), M(region_ground_neighbors), M(units_swept), M(units_adjacent), M(moving_neighbors), M(units_at_point), M(next_path_point), M(find_unit_placement), M(adjust_move_destination), M(adjust_group_destination),
	M(fog_init), M(visibility_bits_set), M(reveal_cache_flush), M(tiles_or), M(tiles_clear), M(terrain_height), M(reveal_sight), M(refresh_remembered_terrain), M(reset_tile_visibility), M(unit_sight), M(unit_has_visible_cargo), M(is_transport), M(unit_update_vision), M(unit_update_sprite_visibility), M(is_detector), M(has_status_effect), M(detection_mask), M(update_detection), M(forget_target), M(clear_lost_targets), M(begin_cloak), M(creep_init), M(creep_recede_tick), M(receding_neighbours), M(creep_apply), M(creep_refresh_borders), M(tile_cannot_have_creep), M(creep_spread_once), M(creep_place_initial), M(megatile_at), M(cv5_megatile), M(minitile_flags), M(creep_bounds), M(events), M(dump), M(dump_crcs), M(save), M(load), M(__order_handler_run),
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
	int i = scalar_of(k);
	if (i >= 0) {
		Native *nv;
		lua_getfield(L, 1, "native");
		nv = (Native *)lua_touserdata(L, -1);
		if (!nv) {
			lua_pushnil(L);
			return 1;
		}
		push_scalar(L, nv->g, i);
		return 1;
	}
	if (k && strcmp(k, "local_player") == 0) { /* a native game's */
		Native *nv;
		lua_getfield(L, 1, "native");
		nv = (Native *)lua_touserdata(L, -1);
		if (nv) return kp_push_int(L, nv->g->local_player);
		lua_pop(L, 1);
	}
	lua_pushvalue(L, 2);
	lua_rawget(L, lua_upvalueindex(1));
	return 1;
}

static int game_newindex(lua_State *L)
{
	const char *k = lua_type(L, 2) == LUA_TSTRING ? lua_tostring(L, 2) : NULL;
	int i = scalar_of(k);
	if (i >= 0) {
		KpGame *g = native_of(L, 1);
		if (!set_scalar(L, g, i, 3)) return luaL_error(L, "game: %s takes integers or booleans", k);
		return 0;
	}
	/* A native game's local player of the error notifications (tests set it as a field of the game). */
	if (k && strcmp(k, "local_player") == 0) {
		lua_getfield(L, 1, "native");
		if (lua_touserdata(L, -1)) {
			KpGame *g = ((Native *)lua_touserdata(L, -1))->g;
			g->local_player = (int64_t)luaL_checknumber(L, 3);
			return 0;
		}
		lua_pop(L, 1);
	}
	lua_rawset(L, 1);
	return 0;
}

#define DAT_TABLE_META "kp_core.game.dat_table"
#define DAT_COLUMN_META "kp_core.game.dat_column"

/* The DAT view of a native game: g.dat.<table>.<column>[i] reads the game's own DAT (nil where the column has no
 * value) and writes it (tests patch tables as the original-code probes patch the loaded DAT). */
typedef struct {
	KpDat *dat;
	const char *table;
} DatTable;

typedef struct {
	KpDat *dat;
	const KpDatColumn *c;
} DatColumn;

static int dat_table_index(lua_State *L)
{
	DatTable *t = (DatTable *)luaL_checkudata(L, 1, DAT_TABLE_META);
	const KpDatColumn *c = kp_dat_column(t->dat, t->table, luaL_checkstring(L, 2));
	DatColumn *col;
	if (!c) {
		lua_pushnil(L);
		return 1;
	}
	col = (DatColumn *)kp_bridge_proxy(L, sizeof *col, DAT_COLUMN_META, 1);
	col->dat = t->dat;
	col->c = c;
	return 1;
}

static int dat_column_index(lua_State *L)
{
	DatColumn *col = (DatColumn *)luaL_checkudata(L, 1, DAT_COLUMN_META);
	lua_Number i = luaL_checknumber(L, 2);
	if (i < 0 || i > 4294967295.0 || i != (lua_Number)(uint32_t)i || !kp_dat_has(col->c, (uint32_t)i)) {
		lua_pushnil(L);
		return 1;
	}
	lua_pushnumber(L, (lua_Number)kp_dat_value(col->c, (uint32_t)i));
	return 1;
}

static int dat_column_newindex(lua_State *L)
{
	DatColumn *col = (DatColumn *)luaL_checkudata(L, 1, DAT_COLUMN_META);
	lua_Number i = luaL_checknumber(L, 2), v = luaL_checknumber(L, 3);
	if (i < 0 || i > 4294967295.0 || !kp_dat_set(col->dat, col->c, (uint32_t)i, (uint32_t)(int64_t)v)) {
		return luaL_error(L, "game: DAT column %s has no entry %f", col->c->name, i);
	}
	return 0;
}

/* g.dat of a native game: one table view per DAT table. */
static void push_dat_view(lua_State *L, KpDat *dat, int native)
{
	int k;
	lua_newtable(L);
	for (k = 0; k < dat->table_count; k++) {
		DatTable *t = (DatTable *)kp_bridge_proxy(L, sizeof *t, DAT_TABLE_META, native);
		t->dat = dat;
		t->table = dat->tables[k].name;
		lua_setfield(L, -2, dat->tables[k].name);
	}
}

/* The C methods and the metatable functions on the class table at index c. */
static void install_methods(lua_State *L, int c)
{
	int k;
	for (k = 0; k < METHOD_COUNT; k++) {
		if (strncmp(METHODS[k].name, "__", 2) == 0) {
			handler_method = k;
			continue;
		}
		lua_pushnumber(L, k);
		lua_pushcclosure(L, game_call, 1);
		lua_setfield(L, c, METHODS[k].name);
	}
	lua_pushvalue(L, c);
	lua_pushcclosure(L, game_index, 1);
	lua_setfield(L, c, "__index");
	lua_pushcfunction(L, game_newindex);
	lua_setfield(L, c, "__newindex");
}

#define NATIVE_CLASS "kp_core.game.class" /* registry: the class of the native games */

/* The string field `key` of the table at index t (an error when missing). */
static const char *blob(lua_State *L, int t, const char *key, size_t *size)
{
	const char *s;
	lua_getfield(L, t, key);
	s = lua_tolstring(L, -1, size);
	if (!s) luaL_error(L, "game: blobs.%s is not a string", key);
	lua_pop(L, 1); /* the blobs table keeps the string alive */
	return s;
}

/* kp_core.game.new(blobs, options): a native game at its first object step (kp_start).
 * blobs = { dat =, grp =, iscript =, mission =, tileset = } (the .kpd/.kpm file contents, tileset the mission's
 * tilesets/<name>.kpd); options = { seed =, expansion =, name =, no_units = } (no_units: tests start the map without
 * its preplaced units) (the game generator and the CRT seeded with
 * seed, as tools/lua/oracle.lua start_mission does). The game table serves the C state (see the top), g.img
 * the images world. */
static int game_new(lua_State *L)
{
	Native *nv;
	KpGameFiles files;
	KpStartInput in;
	KpRngGame *rng;
	KpRngCrt *crt;
	KpError err;
	KpGame *g;
	uint32_t seed = 0;
	int t, native, expansion = 0, no_units = 0;
	luaL_checktype(L, 1, LUA_TTABLE);
	memset(&files, 0, sizeof files);
	files.mission_name = "mission";
	if (lua_istable(L, 2)) {
		lua_getfield(L, 2, "seed");
		if (!lua_isnil(L, -1)) seed = (uint32_t)luaL_checknumber(L, -1);
		lua_getfield(L, 2, "name");
		if (lua_isstring(L, -1)) files.mission_name = lua_tostring(L, -1); /* the options table keeps it alive */
		lua_getfield(L, 2, "expansion");
		expansion = (int)luaL_optnumber(L, -1, 0);
		lua_getfield(L, 2, "no_units");
		no_units = lua_toboolean(L, -1);
		lua_pop(L, 4);
	}
	files.dat = (const uint8_t *)blob(L, 1, "dat", &files.dat_size);
	files.grp = (const uint8_t *)blob(L, 1, "grp", &files.grp_size);
	files.iscript = (const uint8_t *)blob(L, 1, "iscript", &files.iscript_size);
	files.mission = (const uint8_t *)blob(L, 1, "mission", &files.mission_size);
	files.tileset = (const uint8_t *)blob(L, 1, "tileset", &files.tileset_size);
	lua_newtable(L);
	t = lua_gettop(L);
	lua_getfield(L, LUA_REGISTRYINDEX, NATIVE_CLASS);
	lua_setmetatable(L, t);
	nv = (Native *)lua_newuserdata(L, sizeof *nv);
	memset(nv, 0, sizeof *nv);
	native = lua_gettop(L);
	luaL_getmetatable(L, NATIVE_META);
	lua_setmetatable(L, native);
	nv->L = L;
	if (kp_game_data_load(&nv->data, &files, &err) != 0) return luaL_error(L, "%s", err.msg);
	/* The generators are kp_core.rng userdata (g.rng, g.crt). */
	rng = (KpRngGame *)lua_newuserdata(L, sizeof *rng);
	luaL_getmetatable(L, KP_LUA_RNG_GAME);
	lua_setmetatable(L, -2);
	lua_setfield(L, t, "rng");
	crt = (KpRngCrt *)lua_newuserdata(L, sizeof *crt);
	luaL_getmetatable(L, KP_LUA_RNG_CRT);
	lua_setmetatable(L, -2);
	lua_setfield(L, t, "crt");
	kp_game_seed(rng, crt, seed);
	kp_game_start_input(&in, &nv->data, &files, rng, crt, expansion);
	in.no_units = no_units;
	g = kp_start(&in, &err);
	if (!g) return luaL_error(L, "%s", err.msg);
	nv->g = g;
	g->ctx = nv;
#define RAW(field) \
	lua_pushstring(L, #field); \
	kp_push_int(L, g->field); \
	lua_rawset(L, t);
	RAW(map_w) RAW(map_h) RAW(map_w_px) RAW(map_h_px) RAW(local_vision) RAW(expansion)
#undef RAW
	push_views(L, g, native, t);
	push_dat_view(L, &nv->data.dat, native);
	lua_setfield(L, t, "dat");
	kp_images_lua_wrap(L, g->w, native);
	lua_setfield(L, t, "img");
	g->lua_hook = kp_images_lua_hook;
	g->lua_hook_ctx = g->w->binding;
	register_game(L, g, native, t);
	return 1;
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
	luaL_newmetatable(L, DAT_TABLE_META);
	lua_pushcfunction(L, dat_table_index);
	lua_setfield(L, -2, "__index");
	lua_pop(L, 1);
	luaL_newmetatable(L, DAT_COLUMN_META);
	lua_pushcfunction(L, dat_column_index);
	lua_setfield(L, -2, "__index");
	lua_pushcfunction(L, dat_column_newindex);
	lua_setfield(L, -2, "__newindex");
	lua_pop(L, 1);
	lua_newtable(L);
	install_methods(L, lua_gettop(L));
	lua_setfield(L, LUA_REGISTRYINDEX, NATIVE_CLASS);
	lua_newtable(L);
	lua_pushcfunction(L, game_new);
	lua_setfield(L, -2, "new");
}
