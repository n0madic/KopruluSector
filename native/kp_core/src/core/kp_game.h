/*
 * The game state of the simulation beyond the image world (stages N.2-N.5 of ADR 0008): the
 * C counterpart of game/simulation/game.lua and the modules it installs, ported line by line.
 * The Lua modules are the specification.
 *
 * Every game field is an int64_t holding exactly the number the Lua game holds (the Lua code's
 * explicit wraps are kept), so the state model prints the same lines for both. Unit records
 * are 1..KP_UNITS (0 = none), as in the Lua game.
 *
 * Until every module is ported (N.6), the C code calls the methods still in Lua through
 * KpGame.lua by name, and reads the DAT tables through KpGame.dat, which the Lua bridge
 * serves from the Lua game's (mutable) tables. Failures go through the image world's handler
 * stack (kp_images_fail on KpGame.w).
 */
#ifndef KP_GAME_H
#define KP_GAME_H

#include <stddef.h>
#include <stdint.h>

#include "kp_images.h"
#include "kp_mission.h"
#include "kp_rng.h"
#include "kp_pathing.h"
#include "kp_collision.h"
#include "kp_terrain.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KP_UNITS 1700
#define KP_PLAYERS 12
#define KP_UNIT_TYPES 228
#define KP_PSTAT_ARRAYS 28
#define KP_NIL INT64_MIN /* a Lua call's nil result */

/* game/units/unit.lua M.FIELDS, in order; sprite, queue_head and queue_tail start at -1. */
#define KP_UNIT_FIELDS(X) \
	X(prev) X(next) X(hp) X(move_x) X(move_y) X(move_unit) X(waypoint_x) X(waypoint_y) X(target_waypoint_x) \
	X(target_waypoint_y) X(movement_flags) X(heading) X(turn_speed) X(velocity_direction) X(flingy) \
	X(flingy_0x26) X(movement_type) X(x) X(y) X(halt_x) X(halt_y) X(top_speed) X(speed1) X(speed2) X(speed_x) \
	X(speed_y) X(acceleration) X(heading2) X(velocity_direction2) X(owner) X(order) X(order_state) \
	X(order_signal) X(order_type) X(order_timer) X(ground_cooldown) X(air_cooldown) X(spell_cooldown) X(order_x) \
	X(order_y) X(order_unit) X(shields) X(type) X(player_prev) X(player_next) X(subunit) X(auto_target) \
	X(connected) X(queue_count) X(queue_timer) X(unknown_0x86) X(attack_notify_timer) X(previous_type) \
	X(last_event_timer) X(last_event_color) X(unknown_0x8c) X(rank) X(kills) X(last_attacker) X(secondary_timer) \
	X(ai_action) X(user_action) X(button_set) X(is_cloaked) X(movement_state) X(queue0) X(queue1) X(queue2) \
	X(queue3) X(queue4) X(energy) X(queue_slot) X(uniqueness) X(secondary_order) X(overlay_state) X(hp_gain) \
	X(shield_gain) X(build_time) X(previous_hp) X(loaded0) X(loaded1) X(loaded2) X(loaded3) X(loaded4) \
	X(loaded5) X(loaded6) X(loaded7) X(union_0xc0) X(union_0xc4) X(union_0xc8) X(union_0xcc) X(union_0xd0) \
	X(union_0xd4) X(union_0xd8) X(union_units) X(status) X(resource_type) X(wireframe) X(secondary_state) \
	X(recent_order_timer) X(visibility) X(secondary_x) X(secondary_y) X(build_unit) X(burrow_prev) \
	X(burrow_next) X(rally_x) X(rally_y) X(rally_unit) X(path) X(collision_interval) X(pathing_flags) \
	X(unknown_0x106) X(being_healed) X(contour_left) X(contour_top) X(contour_right) X(contour_bottom) \
	X(remove_timer) X(matrix_damage) X(matrix_timer) X(stim_timer) X(ensnare_timer) X(lockdown_timer) \
	X(irradiate_timer) X(stasis_timer) X(plague_timer) X(storm_timer) X(irradiated_by) X(irradiate_player) \
	X(parasites) X(cycle_counter) X(blind) X(maelstrom_timer) X(unknown_0x125) X(acid_spores) X(acid_time0) \
	X(acid_time1) X(acid_time2) X(acid_time3) X(acid_time4) X(acid_time5) X(acid_time6) X(acid_time7) \
	X(acid_time8) X(attack_sequence) X(unknown_0x132) X(ai) X(air_strength) X(ground_strength) X(bound_left) \
	X(bound_right) X(bound_top) X(bound_bottom) X(repulse_flags) X(repulse_angle) X(repulse_x) X(repulse_y) \
	X(sprite) X(queue_head) X(queue_tail)

#define KP_UNIT_FIELD(name) int64_t name[KP_UNITS + 1];
typedef struct {
	KP_UNIT_FIELDS(KP_UNIT_FIELD)
} KpUnits;

/* The unit lists of game.lists, in the state model's order. */
enum { KP_LIST_ACTIVE, KP_LIST_PENDING, KP_LIST_FREE, KP_LIST_HIDDEN, KP_LIST_SPECIAL, KP_LIST_COUNT };

typedef struct KpGame KpGame;

/* Calls the Lua game method `method` with argc integer arguments; fills res[0..nres-1]
 * (KP_NIL for nil, 0/1 for booleans). Failures go through kp_images_fail. */
typedef void (*KpLuaCall)(KpGame *g, const char *method, int argc, const int64_t *args, int nres, int64_t *res);
/* The DAT value table.column[index]; KP_NIL when the column has no such index (as nil in Lua,
 * unequal to every number); fails when the column is absent. */
typedef int64_t (*KpDatRead)(KpGame *g, const char *table, const char *column, int64_t index);
/* A value of the Lua game's state not ported yet: g[k1][i1][k2][i2], each step skipped when
 * its key is NULL or KP_NIL; KP_NIL for nil, 0/1 for booleans. */
typedef int64_t (*KpLuaGet)(KpGame *g, const char *k1, int64_t i1, const char *k2, int64_t i2);
/* A text line for a player (the game's on_text callback). */
typedef void (*KpText)(KpGame *g, int64_t player, const char *text);

/* Scalar fields of the Lua game (a Lua number or boolean, or nil before it is set). */
typedef enum {
	KP_UNITS_ALLOCATED, KP_ERROR_CODE, KP_CLOAKED_HEAD, KP_TILE_COUNTDOWN, KP_UPDATE_TILES, KP_MOVE_REVEAL,
	KP_ORDER_SPREAD, KP_SECONDARY_SPREAD, KP_INTERCEPTOR_ALERTED, KP_HIT_SLOT, KP_FRAME, KP_TURN, KP_ELAPSED,
	KP_HAS_TRIGGERS, KP_COMMAND_PLAYER, KP_SECOND_ORDER, KP_ORDER_MARKER, KP_BULLETS_ACTIVE, KP_POWER_DIRTY,
	KP_PLACEMENT_ACTIVE, KP_CHEATS, KP_REQUIREMENT_ERROR, KP_RESOURCE_AREAS, KP_PATH_FREE_HEAD, KP_PATH_SUBJECT,
	KP_PATH_ALL_UNITS,
	KP_SCALAR_COUNT
} KpScalar;

/* The scalar's Lua name and whether Lua sees it as a boolean. */
typedef struct {
	const char *name;
	int boolean;
} KpScalarInfo;
extern const KpScalarInfo KP_SCALARS[KP_SCALAR_COUNT];

#define KP_ORDER_NODES 2000
#define KP_SLOTS 12
#define KP_SELECTION_PLAYERS 8
#define KP_GROUPS 18
#define KP_RECENT 8
#define KP_GUARDS 1000
#define KP_LIST_MAX 4096 /* results of a Lua call returning a list */

/* Requirement domains (game/units/requirements.lua M.OFFSETS). */
enum { KP_REQ_ORDERS, KP_REQ_RESEARCH, KP_REQ_TECH_USE, KP_REQ_UPGRADES, KP_REQ_UNITS, KP_REQ_DOMAINS };

/* game/units/orders.lua: nodes 0..1999, null link -1. */
typedef struct {
	int64_t prev[KP_ORDER_NODES], next[KP_ORDER_NODES], order[KP_ORDER_NODES], type[KP_ORDER_NODES];
	int64_t x[KP_ORDER_NODES], y[KP_ORDER_NODES], unit[KP_ORDER_NODES];
	KpList free;
	int64_t allocated;
} KpOrders;

/* game/ai/step.lua */
typedef struct {
	int64_t terrain_changed, period, cycle, rounds, region_player, region_cursor, spending_player, town_player, scripts;
} KpAi;

/* game/ai/guard.lua: records 0..999, null link -1. */
typedef struct {
	int64_t next[KP_GUARDS], prev[KP_GUARDS], kind[KP_GUARDS], unit[KP_GUARDS], type[KP_GUARDS];
	int64_t x1[KP_GUARDS], y1[KP_GUARDS], x2[KP_GUARDS], y2[KP_GUARDS], flag9[KP_GUARDS];
	int64_t heads[8];
	int64_t free;
} KpGuards;

/* game/triggers/clock.lua */
typedef struct {
	int64_t second, pass, result, leaderboard, game_timer, countdowns, leaderboard_kind;
} KpTriggerClock;

/* A command of the turn. */
typedef struct {
	int64_t player;
	const uint8_t *bytes;
	size_t size;
} KpCommand;

/* The formation record of a group move (game/units/formation.lua). */
typedef struct {
	int64_t x, y, goal_x, goal_y, ground_x, ground_y, dx, dy, area, radius, flags;
} KpFormation;

/* game/units/spatial.lua: two arrays of unit bounds sorted by coordinate, entries 0..count-1. */
#define KP_SPATIAL_CAPACITY (0x6a40 / 8)
typedef struct {
	int64_t x_unit[KP_SPATIAL_CAPACITY], x_at[KP_SPATIAL_CAPACITY], y_unit[KP_SPATIAL_CAPACITY], y_at[KP_SPATIAL_CAPACITY];
	int64_t count, max_w, max_h;
	int64_t mark[KP_UNITS + 1], seen[KP_UNITS + 1], stamp; /* query state */
} KpSpatial;

/* game/units/flingy_move.lua: the prepared step (the step globals 0x543280..). */
typedef struct {
	int64_t next_x, next_y, old_x, old_y, next_halt_x, next_halt_y, step, walk_start, walk_stop, flags_before, flags_after;
} KpMove;

/* The mission's terrain contours (game/units/placement.lua M.contours): per side (top, right, bottom, left) the
 * records 0..count-1. */
typedef struct {
	int32_t count;
	int64_t *axis, *start, *stop, *dir, *info; /* owned by the game */
} KpContourSide;

/* Calls the Lua game method `method`, which returns a list; fills out[0..max-1] and returns
 * the list's length. */
typedef int (*KpLuaList)(KpGame *g, const char *method, int argc, const int64_t *args, int64_t *out, int max);

struct KpGame {
	KpImages *w;
	KpRngGame *rng;
	KpRngCrt *crt; /* g.crt (creep variants); NULL when the game has none */
	KpLuaCall lua;
	KpLuaList list;
	KpDatRead dat;
	KpLuaGet get;
	KpText text;
	void *ctx;

	/* Settings fixed at the game's creation (game/simulation/game.lua M.new). */
	int64_t map_w, map_h, map_w_px, map_h_px, local_player, local_vision, expansion;

	int64_t scalar[KP_SCALAR_COUNT];
	uint8_t scalar_present[KP_SCALAR_COUNT];

	/* game/units/unit.lua */
	KpUnits u;
	KpList lists[KP_LIST_COUNT];
	int64_t player_units[KP_PLAYERS];

	/* game/units/counts.lua: per type and player (index type * 12 + player), per player */
	int64_t count_all[KP_UNIT_TYPES * KP_PLAYERS], count_completed[KP_UNIT_TYPES * KP_PLAYERS];
	int64_t count_kills[KP_UNIT_TYPES * KP_PLAYERS], count_deaths[KP_UNIT_TYPES * KP_PLAYERS];
	int64_t pstat[KP_PSTAT_ARRAYS][KP_PLAYERS];

	int64_t strength[2][KP_UNIT_TYPES]; /* game/units/strength.lua: air, ground */
	KpOrders orders;

	/* game/units/selection.lua */
	int64_t local_selection[KP_SLOTS];
	int64_t selections[KP_SELECTION_PLAYERS][KP_SLOTS];
	int64_t groups[KP_SELECTION_PLAYERS][KP_GROUPS][KP_SLOTS];
	int64_t recent_stamps[KP_SELECTION_PLAYERS][KP_RECENT];
	int64_t circle_colors[KP_PLAYERS];

	/* game/units/requirements.lua: the lazy offset arrays of each domain */
	int64_t req_offsets[KP_REQ_DOMAINS][KP_UNIT_TYPES];
	uint8_t req_offsets_ready[KP_REQ_DOMAINS];

	KpAi ai;
	KpSpatial spatial;
	KpMove mv;
	KpMissionRegions regions; /* the mission's RGNS (game.regions) */
	KpContourSide contours[4];
	KpPathing pathing;
	KpCollision collision;
	KpTerrain terrain;
	KpGuards guards;
	KpTriggerClock trigger_clock;
	int64_t results[8];
};

KpGame *kp_game_new(KpImages *w, KpRngGame *rng);
void kp_game_free(KpGame *g);
/* The mission's RGNS (copied; fields as kp_mission_load parses them) and the contour records of side d (allocated
 * for count records, filled by the caller). -1 when out of memory. */
int kp_game_set_regions(KpGame *g, const uint8_t *data, size_t size, const KpMissionRegions *fields);
int kp_game_set_contour_side(KpGame *g, int d, int32_t count);

#if defined(__GNUC__) || defined(__clang__)
__attribute__((noreturn, format(printf, 2, 3)))
#endif
void kp_game_fail(KpGame *g, const char *fmt, ...);

/* Calls of Lua methods with up to four arguments; the first result (KP_NIL for none). */
int64_t kp_lua(KpGame *g, const char *method, int argc, int64_t a, int64_t b, int64_t c, int64_t d);
#define KP_LUA0(g, m) kp_lua(g, m, 0, 0, 0, 0, 0)
#define KP_LUA1(g, m, a) kp_lua(g, m, 1, a, 0, 0, 0)
#define KP_LUA2(g, m, a, b) kp_lua(g, m, 2, a, b, 0, 0)
#define KP_LUA3(g, m, a, b, c) kp_lua(g, m, 3, a, b, c, 0)
#define KP_LUA4(g, m, a, b, c, d) kp_lua(g, m, 4, a, b, c, d)

/* DAT reads: KP_DAT(g, units, hit_points, t). */
#define KP_DAT(g, table, column, i) ((g)->dat((g), #table, #column, (int64_t)(i)))

/* Unit record checks (a field read that would be nil in Lua fails). */
int64_t kp_game_unit(KpGame *g, int64_t n);

/* The images world's unit and flingy contexts, saved by kp_ctx_unit (which sets the unit) and
 * restored by kp_ctx_restore. */
typedef struct {
	int32_t unit, flingy;
	int has_unit, has_flingy;
} KpCtx;
KpCtx kp_ctx_unit(KpGame *g, int64_t n);
void kp_ctx_restore(KpGame *g, KpCtx c);

/* game/units/unit.lua */
int kp_rally_producer(int64_t t);
int32_t kp_unit_sprite(KpGame *g, int64_t n);
void kp_units_init(KpGame *g);
int64_t *kp_union_word(KpGame *g, int word);
void kp_set_union_unit(KpGame *g, int64_t n, int word, int64_t target);
int kp_unit_is_dead(KpGame *g, int64_t n);
int64_t kp_unit_from_ref(KpGame *g, int64_t ref);
int kp_unit_fits_map(KpGame *g, int64_t t, int64_t x, int64_t y);
int64_t kp_max_energy(KpGame *g, int64_t n);
void kp_set_button_set(KpGame *g, int64_t n, int64_t set);
int64_t kp_unit_damage_overlay_slots(KpGame *g, int64_t n);
int kp_thingy_init(KpGame *g, int64_t n, int64_t flingy, int64_t x, int64_t y, int64_t owner, int64_t heading);
void kp_step_unit_sprite(KpGame *g, int64_t n);
int kp_unit_initialize_type(KpGame *g, int64_t n, int64_t t, int64_t x, int64_t y, int64_t owner);
int kp_unit_initialize(KpGame *g, int64_t n, int64_t t, int64_t x, int64_t y, int64_t owner);
int64_t kp_unit_allocate(KpGame *g, int64_t t, int64_t x, int64_t y, int64_t owner);
int64_t kp_unit_create(KpGame *g, int64_t t, int64_t x, int64_t y, int64_t owner);
void kp_replace_unit_images(KpGame *g, int64_t n, int construction);
void kp_sprite_replace_main(KpGame *g, int32_t s, int64_t id, int64_t heading);
void kp_rebuild_status_overlays(KpGame *g, int64_t n);
void kp_unit_activate_spatial(KpGame *g, int64_t n);
void kp_add_building_occupancy(KpGame *g, int64_t n);
void kp_remove_building_occupancy(KpGame *g, int64_t n);
void kp_unit_deactivate_spatial(KpGame *g, int64_t n);
void kp_reset_movement(KpGame *g, int64_t n);
void kp_reset_to_idle_animation(KpGame *g, int64_t n);
void kp_unit_show(KpGame *g, int64_t n);
void kp_unit_prefinish(KpGame *g, int64_t n);
void kp_clamp_to_map(KpGame *g, int64_t t, int64_t *x, int64_t *y);
void kp_unit_relocate(KpGame *g, int64_t n, int64_t x, int64_t y);
int kp_set_move_target_point(KpGame *g, int64_t n, int64_t x, int64_t y);
int kp_set_move_target_unit(KpGame *g, int64_t n, int64_t target);
int kp_unit_place(KpGame *g, int64_t n);
void kp_unit_finish_completion(KpGame *g, int64_t n);
int kp_unit_complete(KpGame *g, int64_t n);
void kp_unit_set_cloak_state(KpGame *g, int64_t n, int64_t state);
void kp_set_unit_hp(KpGame *g, int64_t n, int64_t hp);
void kp_update_damage_overlays(KpGame *g, int64_t n);
void kp_damage_overlay_add(KpGame *g, int64_t n);
void kp_damage_overlay_remove(KpGame *g, int64_t n);

/* A preplaced UNIT record (game/data/unit_records.lua). */
typedef struct {
	int64_t valid_states, valid_properties, hp_percent, shield_percent, energy_percent, resources, hangar, states;
} KpUnitRecord;

/* game/units/map_units.lua */
int64_t kp_create_map_unit(KpGame *g, int64_t t, int64_t x, int64_t y, int64_t owner);
void kp_apply_unit_record(KpGame *g, int64_t n, const KpUnitRecord *r);
int64_t kp_hangar_capacity(KpGame *g, int64_t n);
void kp_create_hangar_units(KpGame *g, int64_t n, int64_t count);
void kp_hangar_add(KpGame *g, int64_t n, int64_t c);
int64_t kp_cloak_tech(KpGame *g, int64_t n);
void kp_cloak_list_add(KpGame *g, int64_t n);
int kp_cloak_list_remove(KpGame *g, int64_t n);
void kp_cloak_hidden(KpGame *g, int64_t n);
void kp_set_burrowed(KpGame *g, int64_t n);
int64_t kp_unit_race(KpGame *g, int64_t n);
void kp_attach_addon(KpGame *g, int64_t building, int64_t addon);
void kp_lift_off_initial(KpGame *g, int64_t n);
void kp_update_mineral_animation(KpGame *g, int64_t n);
void kp_unit_disable_doodad(KpGame *g, int64_t n);

/* Scalars: the value, KP_NIL when the Lua game would hold nil. */
int64_t kp_scalar(const KpGame *g, KpScalar s);
void kp_scalar_set(KpGame *g, KpScalar s, int64_t v);
void kp_scalar_clear(KpGame *g, KpScalar s);

/* The flingy direction tables (kp_directions.c). */
extern const int32_t KP_DIR_X[256];
extern const int32_t KP_DIR_Y[256];
extern const int64_t KP_SLOPES[64];

/* game/simulation/game.lua */
int64_t kp_upgrade_level(KpGame *g, int64_t player, int64_t id);
int kp_is_morphing_zerg_building(KpGame *g, int64_t n);
void kp_apply_creation_upgrades(KpGame *g, int64_t n);
/* The units in a rectangle, in the spatial index's order (game/vision/detection.lua). */
int kp_units_in_rect(KpGame *g, int64_t left, int64_t top, int64_t right, int64_t bottom, int64_t *out, int max);

/* game/units/orders.lua */
void kp_orders_init(KpGame *g);
void kp_order_queue(KpGame *g, int64_t n, int64_t order, int64_t x, int64_t y, int64_t target, int64_t fow);
void kp_order_append(KpGame *g, int64_t n, int64_t order, int64_t x, int64_t y, int64_t target, int64_t fow,
	int clear_others);
void kp_order_insert_after(KpGame *g, int64_t n, int64_t order, int64_t x, int64_t y, int64_t target, int64_t fow,
	int64_t anchor);
void kp_order_insert_before(KpGame *g, int64_t n, int64_t order, int64_t x, int64_t y, int64_t target, int64_t fow,
	int64_t anchor);
/* Order node i, checked against the pool. */
int64_t kp_order_node(KpGame *g, int64_t i);
void kp_order_remove(KpGame *g, int64_t n, int64_t node);
void kp_order_clear_queue(KpGame *g, int64_t n);
void kp_set_order(KpGame *g, int64_t n, int64_t order);
void kp_set_order_at(KpGame *g, int64_t n, int64_t order, int64_t x, int64_t y);
void kp_set_order_fow(KpGame *g, int64_t n, int64_t order, int64_t fow, int64_t x, int64_t y);
void kp_set_order_target(KpGame *g, int64_t n, int64_t order, int64_t target);
void kp_set_secondary_order(KpGame *g, int64_t n, int64_t order);
void kp_order_activate(KpGame *g, int64_t n);
void kp_order_done(KpGame *g, int64_t n);
void kp_unit_kill(KpGame *g, int64_t n);
void kp_issue_order(KpGame *g, int64_t n, int64_t order);
void kp_issue_order_at(KpGame *g, int64_t n, int64_t order, int64_t x, int64_t y);
void kp_issue_order_fow(KpGame *g, int64_t n, int64_t order, int64_t fow, int64_t x, int64_t y);
void kp_issue_order_target(KpGame *g, int64_t n, int64_t order, int64_t target);
void kp_issue_order_packed(KpGame *g, int64_t n, int64_t order, int64_t x, int64_t y, int64_t target);

/* game/units/timers.lua */
int64_t kp_energy_cap(KpGame *g, int64_t n);
void kp_update_unit_energy(KpGame *g, int64_t n);
void kp_update_acid_spores(KpGame *g, int64_t n);
void kp_update_spell_timers(KpGame *g, int64_t n);
void kp_update_unit_timers(KpGame *g, int64_t n);

/* game/units/order_dispatch.lua */
int kp_request_move_to_point(KpGame *g, int64_t n, int64_t x, int64_t y);
int kp_request_move_to_unit(KpGame *g, int64_t n, int64_t target);
void kp_set_cloak_bits(KpGame *g, int64_t n, int64_t v);
int kp_order_has_handler(int64_t order);
int kp_order_handler_run(KpGame *g, int64_t order, int64_t n);
void kp_dispatch_main_order(KpGame *g, int64_t n);
void kp_dispatch_hidden_order(KpGame *g, int64_t n);
void kp_dispatch_secondary_order(KpGame *g, int64_t n);
void kp_secondary_cloak_order(KpGame *g, int64_t n, int64_t order);

/* game/units/targets.lua */
int kp_is_enemy(KpGame *g, int64_t n, int64_t target);
int kp_is_undetected_by(KpGame *g, int64_t n, int64_t viewer);
int64_t kp_ground_weapon(KpGame *g, int64_t n);
int64_t kp_ground_weapon_with_subunit(KpGame *g, int64_t n);
int64_t kp_air_weapon_with_subunit(KpGame *g, int64_t n);
int64_t kp_seek_range(KpGame *g, int64_t n);
int kp_can_attack(KpGame *g, int64_t n, int64_t target, int check_detection);
int64_t kp_acquire_target(KpGame *g, int64_t n);
int64_t kp_acquire_random_target(KpGame *g, int64_t n);

/* game/units/requirements.lua */
int kp_tech_available(KpGame *g, int64_t player, int64_t tech);
int kp_tech_researched(KpGame *g, int64_t player, int64_t tech);
int64_t kp_right_click_action(KpGame *g, int64_t n);
int kp_unit_can_move(KpGame *g, int64_t n);
int kp_unit_can_attack(KpGame *g, int64_t n);
int64_t kp_evaluate_requirements(KpGame *g, int64_t context, int64_t player, int64_t n, int domain, int64_t at);
int64_t kp_required_parent_type(KpGame *g, int64_t t);
int kp_unit_disabled(KpGame *g, int64_t n);
int64_t kp_can_issue_order(KpGame *g, int64_t order, int64_t player, int64_t n);
int64_t kp_can_use_tech(KpGame *g, int64_t tech, int64_t player, int64_t n);

/* game/units/owner.lua */
void kp_remove_unit_ai(KpGame *g, int64_t n, int64_t killed);
void kp_assign_unit_ai(KpGame *g, int64_t n);
void kp_change_owner(KpGame *g, int64_t n, int64_t owner, int score);

/* game/units/remove.lua */
void kp_clear_references(KpGame *g, int64_t n, int full);
void kp_remove_from_selections(KpGame *g, int64_t n);
void kp_remove_from_local_selection(KpGame *g, int64_t n);
void kp_unit_hide(KpGame *g, int64_t n);
void kp_remove_body(KpGame *g, int64_t n);
void kp_unit_destroy(KpGame *g, int64_t n);
void kp_update_pending_units(KpGame *g);

/* game/units/selection.lua */
void kp_selection_init(KpGame *g);
int64_t kp_unit_ref(KpGame *g, int64_t n);
void kp_add_selection_circle(KpGame *g, int64_t n, int64_t slot);
void kp_flash_selection(KpGame *g, int64_t n);
void kp_remove_selection_circle(KpGame *g, int64_t n);
void kp_select_locally(KpGame *g, const int64_t *units, int count);
void kp_move_placement_cursor(KpGame *g, int64_t x, int64_t y);
void kp_mark_target(KpGame *g, int64_t x, int64_t y, int64_t unit);
int kp_multi_selectable(KpGame *g, int64_t n);
void kp_command_select(KpGame *g, int64_t p, const int64_t *refs, int count);
int kp_selected_units(KpGame *g, int64_t p, int64_t *out);

/* game/units/formation.lua */
void kp_group_formation(KpGame *g, int64_t x, int64_t y, int adjust, KpFormation *f);
void kp_formation_point(KpGame *g, int64_t n, const KpFormation *f, int64_t *x, int64_t *y);

/* game/simulation/commands.lua */
void kp_execute_command(KpGame *g, int64_t player, const uint8_t *bytes, size_t size);
void kp_run_turn(KpGame *g, const KpCommand *commands, int count);

/* game/simulation/step.lua */
void kp_step_init(KpGame *g);
void kp_set_context(KpGame *g, int64_t n);
void kp_update_units(KpGame *g);
void kp_step_objects(KpGame *g);
void kp_prime(KpGame *g);
void kp_step_frame(KpGame *g);
void kp_run_frame(KpGame *g, const KpCommand *commands, int count);

/* game/triggers/clock.lua */
void kp_trigger_clock_init(KpGame *g, int has_triggers);
void kp_trigger_step(KpGame *g);

/* game/ai/step.lua, game/ai/guard.lua */
void kp_ai_init(KpGame *g);
void kp_ai_step(KpGame *g);
void kp_guard_init(KpGame *g);
void kp_guard_attach(KpGame *g, int64_t n);

/* game/units/spatial.lua */
void kp_spatial_clear(KpGame *g);
void kp_spatial_insert(KpGame *g, int64_t n);
void kp_spatial_remove(KpGame *g, int64_t n);
void kp_spatial_move(KpGame *g, int64_t n, int64_t dx, int64_t dy);
int kp_spatial_query(KpGame *g, int64_t left, int64_t top, int64_t right, int64_t bottom, int64_t *out, int max);
int kp_spatial_overlap(KpGame *g, int64_t a, int64_t b);
void kp_unit_rect(KpGame *g, int64_t n, int64_t *left, int64_t *top, int64_t *right, int64_t *bottom);
void kp_update_overlap_flags(KpGame *g, int64_t n);

/* game/vision/detection.lua (the spatial queries; the rest is N.5) */
int kp_unit_box_within(KpGame *g, int64_t n, int64_t range, int64_t target);

/* game/units/flingy.lua */
int64_t kp_speed_modified(KpGame *g, int64_t n, int64_t v);
int64_t kp_top_speed_modified(KpGame *g, int64_t n, int64_t v);
int64_t kp_acceleration_modified(KpGame *g, int64_t n);
int64_t kp_turn_radius_modified(KpGame *g, int64_t n);
void kp_iscript_walk_speed(KpGame *g, int64_t n);
void kp_recalculate_speed(KpGame *g, int64_t n);
void kp_set_speed_upgrade(KpGame *g, int64_t n);
int kp_flingy_init(KpGame *g, int64_t n, int64_t flingy, int64_t x, int64_t y, int64_t owner, int64_t heading);
void kp_flingy_set_heading(KpGame *g, int64_t n, int64_t heading);
void kp_flingy_set_position(KpGame *g, int64_t n, int64_t x, int64_t y);
void kp_flingy_set_exact_position(KpGame *g, int64_t n, int64_t hx, int64_t hy);
void kp_flingy_set_target_waypoint(KpGame *g, int64_t n, int64_t x, int64_t y);
void kp_flingy_set_move_target(KpGame *g, int64_t n, int64_t x, int64_t y);
void kp_flingy_set_move_target_unit(KpGame *g, int64_t n, int64_t target);
void kp_flingy_to_idle(KpGame *g, int64_t n);
void kp_flingy_set_speed(KpGame *g, int64_t n, int64_t v);
void kp_flingy_turn(KpGame *g, int64_t n, int64_t steps);
void kp_flingy_set_spawn_frame(KpGame *g, int64_t n, int64_t d);
void kp_nobrk_end(KpGame *g, int64_t n);

/* game/units/flingy_move.lua */
int64_t kp_approx_distance(int64_t dx, int64_t dy);
void kp_move_context_init(KpGame *g);
void kp_flingy_set_current_speed(KpGame *g, int64_t n, int64_t v);
int64_t kp_flingy_halt_distance(KpGame *g, int64_t n);
int64_t kp_direction_to(KpGame *g, int64_t x1, int64_t y1, int64_t x2, int64_t y2);
void kp_flingy_turn_velocity(KpGame *g, int64_t n);
void kp_flingy_update_desired(KpGame *g, int64_t n);
void kp_flingy_progress_speed(KpGame *g, int64_t n);
void kp_flingy_update_walking(KpGame *g, int64_t n);
void kp_prepare_flingy_movement(KpGame *g, int64_t n);
void kp_prepare_movement_in_direction(KpGame *g, int64_t n, int64_t dir);
void kp_flingy_turn_heading(KpGame *g, int64_t n);
void kp_flingy_commit_blocked(KpGame *g, int64_t n);
void kp_forecast_position(KpGame *g, int64_t n, int64_t steps, int64_t *x, int64_t *y);
void kp_flingy_recompute_velocity(KpGame *g, int64_t n);
void kp_flingy_commit_turn(KpGame *g, int64_t n);
void kp_prepare_movement_toward(KpGame *g, int64_t n, int64_t dir, int64_t speed);
int kp_commit_unit_movement(KpGame *g, int64_t n);

/* game/units/movement.lua */
int64_t kp_move_arrival(KpGame *g, int64_t n);
int64_t kp_try_move_to_legal(KpGame *g, int64_t n);
void kp_stop_at_target(KpGame *g, int64_t n);
int kp_update_movement_state(KpGame *g, int64_t n, int repath);
void kp_execute_movement(KpGame *g, int64_t n);
void kp_stop_flingy(KpGame *g, int64_t n);
void kp_stop_movement(KpGame *g, int64_t n);
void kp_prepare_movement(KpGame *g, int64_t n);

/* game/units/strength.lua */
void kp_strength_init(KpGame *g);
int64_t kp_unit_type_strength(KpGame *g, int64_t n, int kind);
int64_t kp_current_life(KpGame *g, int64_t n);
int64_t kp_max_hit_points(KpGame *g, int64_t n);
int64_t kp_max_life(KpGame *g, int64_t n);
int64_t kp_unit_current_strength(KpGame *g, int64_t n, int kind);
void kp_refresh_unit_strength(KpGame *g, int64_t n);

/* game/units/counts.lua */
void kp_counts_init(KpGame *g);
void kp_account_live(KpGame *g, int64_t n, int64_t delta);
void kp_account_completed(KpGame *g, int64_t n, int64_t delta, int score);

#ifdef __cplusplus
}
#endif

#endif
