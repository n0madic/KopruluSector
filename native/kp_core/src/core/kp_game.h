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

#include <stdint.h>

#include "kp_images.h"
#include "kp_rng.h"

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
/* The DAT value table.column[index]; fails when absent. */
typedef int64_t (*KpDatRead)(KpGame *g, const char *table, const char *column, int64_t index);
/* A value of the Lua game's state not ported yet: g[k1][i1][k2][i2], each step skipped when
 * its key is NULL or KP_NIL; KP_NIL for nil, 0/1 for booleans. */
typedef int64_t (*KpLuaGet)(KpGame *g, const char *k1, int64_t i1, const char *k2, int64_t i2);
/* A text line for a player (the game's on_text callback). */
typedef void (*KpText)(KpGame *g, int64_t player, const char *text);

struct KpGame {
	KpImages *w;
	KpRngGame *rng;
	KpLuaCall lua;
	KpDatRead dat;
	KpLuaGet get;
	KpText text;
	void *ctx;

	/* Settings fixed at the game's creation (game/simulation/game.lua M.new). */
	int64_t map_w, map_h, map_w_px, map_h_px, local_player, local_vision, expansion;

	/* game/units/unit.lua */
	KpUnits u;
	KpList lists[KP_LIST_COUNT];
	int64_t player_units[KP_PLAYERS];
	int64_t units_allocated, error_code;
	int64_t cloaked_head; /* game/units/map_units.lua cloak list */
	int64_t strength[2][KP_UNIT_TYPES]; /* game/units/strength.lua: air, ground */

	/* game/units/counts.lua: per type and player (index type * 12 + player), per player */
	int64_t count_all[KP_UNIT_TYPES * KP_PLAYERS], count_completed[KP_UNIT_TYPES * KP_PLAYERS];
	int64_t count_kills[KP_UNIT_TYPES * KP_PLAYERS], count_deaths[KP_UNIT_TYPES * KP_PLAYERS];
	int64_t pstat[KP_PSTAT_ARRAYS][KP_PLAYERS];
};

KpGame *kp_game_new(KpImages *w, KpRngGame *rng);
void kp_game_free(KpGame *g);

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
