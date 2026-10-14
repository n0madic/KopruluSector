/*
 * The game state of the simulation beyond the image world (ADR 0008): units, orders, pathing, vision, terrain, the
 * AI and trigger clocks, ported line by line from the original's functions, which the comments name.
 *
 * Every game field is an int64_t (the wraps of the original's fixed-width arithmetic are explicit), so the dump
 * prints the lines of tools/native_state.py. Unit records are 1..KP_UNITS (0 = none).
 *
 * The game reads its own DAT (kp_game_use_dat) and reports to presentation through its event ring. Failures go
 * through the image world's handler stack (kp_images_fail on KpGame.w).
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
#include "kp_dat.h"
#include "kp_dat_columns.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KP_UNITS 1700
#define KP_BULLETS 100
/* The flingy records: units 1..KP_UNITS, then bullet b's flingy part at KP_UNITS + b (kp_bullets.c). */
#define KP_FLINGIES (KP_UNITS + KP_BULLETS)
#define KP_PLAYERS 12
#define KP_UNIT_TYPES 228
#define KP_PSTAT_ARRAYS 28
#define KP_WEAPON_IDS 130
#define KP_NIL INT64_MIN /* no value: an unset scalar, a missing DAT value, no result */

/* The unit record fields (kp_units.c); sprite, queue_head and queue_tail start at -1. */
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

/* Every field spans the flingy records: a bullet's flingy part keeps its CFlingy fields at the unit offsets, its owner
 * (+0x4c), state (+0x4d, `order`), signal, order point and target there too. */
#define KP_UNIT_FIELD(name) int64_t name[KP_FLINGIES + 1];
typedef struct {
	KP_UNIT_FIELDS(KP_UNIT_FIELD)
} KpUnits;

/* The bullet records 1..KP_BULLETS (InitBullets 0x40a0e0): the fields of CBullet beyond its flingy part, the active
 * and free lists and the allocated count. */
#define KP_BULLET_FIELDS(X) \
	X(prev) X(next) X(unknown_0x52) X(weapon) X(time) X(hit_flags) X(bounces) X(source) X(previous_target) X(ring) \
	X(unknown_0x6d) X(unknown_0x6e)
#define KP_BULLET_FIELD(name) int64_t name[KP_BULLETS + 1];
typedef struct {
	KP_BULLET_FIELDS(KP_BULLET_FIELD)
	KpList active, free;
	int64_t allocated;
	int64_t last_spawner; /* 0x540760: the unit of the previous shot (the alternating launch spin) */
	int64_t spin_sign;    /* 0x540764: the side of that shot's spin */
} KpBullets;

/* The unit lists of game.lists, in the state model's order. */
enum { KP_LIST_ACTIVE, KP_LIST_PENDING, KP_LIST_FREE, KP_LIST_HIDDEN, KP_LIST_SPECIAL, KP_LIST_COUNT };

typedef struct KpGame KpGame;


/* Scalar fields of the game (a number or boolean, KP_NIL before it is set). */
typedef enum {
	KP_UNITS_ALLOCATED, KP_ERROR_CODE, KP_CLOAKED_HEAD, KP_TILE_COUNTDOWN, KP_UPDATE_TILES, KP_MOVE_REVEAL,
	KP_ORDER_SPREAD, KP_SECONDARY_SPREAD, KP_INTERCEPTOR_ALERTED, KP_HIT_SLOT, KP_FRAME, KP_TURN, KP_ELAPSED,
	KP_HAS_TRIGGERS, KP_COMMAND_PLAYER, KP_SECOND_ORDER, KP_ORDER_MARKER, KP_POWER_DIRTY,
	KP_PLACEMENT_ACTIVE, KP_CHEATS, KP_REQUIREMENT_ERROR, KP_RESOURCE_AREAS, KP_PATH_FREE_HEAD, KP_PATH_SUBJECT,
	KP_PATH_ALL_UNITS,
	KP_SCALAR_COUNT
} KpScalar;

/* The scalar's name and whether it is a boolean (as the Lua binding serves it). */
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
#define KP_LIST_MAX 4096 /* the most results of a call returning a list */

/* Requirement domains (kp_requirements.c). */
enum { KP_REQ_ORDERS, KP_REQ_RESEARCH, KP_REQ_TECH_USE, KP_REQ_UPGRADES, KP_REQ_UNITS, KP_REQ_DOMAINS };

/* The order node pool (kp_orders.c): nodes 0..1999, null link -1. */
typedef struct {
	int64_t prev[KP_ORDER_NODES], next[KP_ORDER_NODES], order[KP_ORDER_NODES], type[KP_ORDER_NODES];
	int64_t x[KP_ORDER_NODES], y[KP_ORDER_NODES], unit[KP_ORDER_NODES];
	KpList free;
	int64_t allocated;
} KpOrders;

/* The computer players' regional AI (kp_ai.c; docs/research/atlas/p37.md, p38.md): an AI region record per player
 * (0..7) and map region (InitAiRegions 0x4b0640 makes 2500; FinishAiMapSetup 0x48dd80 keeps the map's count). Unit
 * fields hold unit records (0 none). */
#define KP_AI_PLAYERS 8
#define KP_AI_REGIONS 2500
#define KP_AI_REGION_FIELDS(X) \
	X(dest) X(state) X(priority) X(retry) X(flags) X(wait) X(ground_count) X(need_ground) X(need_air) X(local_ground) \
	X(local_air) X(all_ground) X(all_air) X(enemy_air) X(enemy_ground) X(target_air) X(target_ground) X(slowest) \
	X(detector) X(military)
#define KP_AI_REGION_FIELD(name) int64_t name[KP_AI_PLAYERS][KP_AI_REGIONS];
typedef struct {
	KP_AI_REGION_FIELDS(KP_AI_REGION_FIELD)
} KpAiRegions;

/* The PlayerAi records (0x67d068, 0x4e8 bytes each; docs/research/atlas/p28.md): the fields the game uses so far. */
#define KP_PLAYER_AI_FIELDS(X) \
	X(heap_count) X(placement_throttle) X(nuke_rate) X(expansion_attacks) X(last_nuke) X(flags) X(panic) X(max_force) \
	X(attack_target) X(wanted_unit) X(default_min) X(spend_counter) X(timed_request) X(grouping_time) X(countdown0) \
	X(countdown1) X(countdown2) X(ai_state)
#define KP_PLAYER_AI_FIELD(name) int64_t name[KP_AI_PLAYERS];
typedef struct {
	KP_PLAYER_AI_FIELDS(KP_PLAYER_AI_FIELD)
} KpPlayerAi;

/* Protoss psi power (kp_power.c): the mask of the Psi field (0x665b50, half extents 0x665b58/0x665b5c in pixels) and
 * the previous links of the provider list (+0xf8; the next links are rally_unit, +0xfc). */
#define KP_POWER_MASK 1024
typedef struct {
	int64_t half_w, half_h;
	uint8_t mask[KP_POWER_MASK];
	int64_t prev[KP_UNITS + 1];
} KpPower;

/* The AI step (kp_step.c) */
typedef struct {
	int64_t terrain_changed, period, cycle, rounds, region_player, region_cursor, spending_player, town_player, scripts;
	/* the reactive flag a spell selector leaves for the next candidate tests (0x665d30) */
	int64_t spell_reactive;
} KpAi;

/* The AI guard records (kp_step.c): records 0..999, null link -1. */
typedef struct {
	int64_t next[KP_GUARDS], prev[KP_GUARDS], kind[KP_GUARDS], unit[KP_GUARDS], type[KP_GUARDS];
	int64_t x1[KP_GUARDS], y1[KP_GUARDS], x2[KP_GUARDS], y2[KP_GUARDS], deaths[KP_GUARDS];
	int64_t heads[8];
	int64_t free;
} KpGuards;

/* The trigger clocks (kp_step.c) */
typedef struct {
	int64_t second, pass, result, leaderboard, game_timer, countdowns, leaderboard_kind;
} KpTriggerClock;

/* A command of the turn. */
typedef struct {
	int64_t player;
	const uint8_t *bytes;
	size_t size;
} KpCommand;
/* The most commands a caller hands to one turn (the Lua binding's run_turn/run_frame, the replay tools). */
#define KP_TURN_COMMANDS 1024

/* Simulation -> presentation/audio/UI events (plan D15): a ring of records (kind, frame, sequence number, three
 * numeric arguments, a text slot), drained by the presentation (kp_event_take; the mission manager,
 * game/simulation/manager.lua). A full ring, or more texts waiting than their slots, fails: an event the consumer
 * never sees would silently change what the player hears or sees. A game without a presentation (the oracle runs,
 * the replay tools) starts with the ring off (KpStartInput.no_events): its events are not recorded at all. */
#define KP_EVENTS (4 * 5220)
#define KP_EVENT_TEXTS 64
#define KP_EVENT_TEXT_SIZE 256 /* the original formats a message into a 256-byte buffer (0x4cb860 for 0x46ac90) */
enum {
	KP_EVENT_SOUND = 1, /* a = sfx id, b = x, c = y (map pixels; PlayImageSound 0x40f5a0) */
	KP_EVENT_ERROR = 2, /* a = player, b = string id (the local player's error notifications, 0x46ae50) */
	KP_EVENT_TEXT = 3,  /* a = player; the text */
	KP_EVENT_SOUND_SCREEN = 4 /* a = sfx id; a sound without a position, at full volume (0x446940 without a unit) */
};
typedef struct {
	int64_t first, count, sequence, text_next, texts_waiting;
	int off; /* the game records no events */
	int64_t kind[KP_EVENTS], frame[KP_EVENTS], seq[KP_EVENTS], a[KP_EVENTS], b[KP_EVENTS], c[KP_EVENTS];
	int32_t text[KP_EVENTS]; /* slot in texts, -1 for none */
	char texts[KP_EVENT_TEXTS][KP_EVENT_TEXT_SIZE];
} KpEvents;

/* The players (kp_game.c, kp_start.c): the runtime controller, race and force of each slot, the alliance matrix,
 * shared vision, upgrade levels and tech states (0 when unset). */
#define KP_UPGRADE_IDS 64
#define KP_TECH_IDS 64
typedef struct {
	int64_t controller[KP_PLAYERS], race[KP_PLAYERS], force[KP_PLAYERS];
	int64_t alliance[KP_PLAYERS][KP_PLAYERS];
	int64_t shared_vision[KP_PLAYERS];
	int64_t upgrades[KP_PLAYERS][KP_UPGRADE_IDS];
	int64_t tech_available[KP_PLAYERS][KP_TECH_IDS], tech_researched[KP_PLAYERS][KP_TECH_IDS];
} KpPlayers;

/* The formation record of a group move (kp_formation.c). */
typedef struct {
	int64_t x, y, goal_x, goal_y, ground_x, ground_y, dx, dy, area, radius, flags;
} KpFormation;

/* The unit spatial index (kp_spatial.c): two arrays of unit bounds sorted by coordinate, entries 0..count-1. */
#define KP_SPATIAL_CAPACITY (0x6a40 / 8)
#define KP_QUERY_ARENA (8 * KP_UNITS)
typedef struct {
	int64_t x_unit[KP_SPATIAL_CAPACITY], x_at[KP_SPATIAL_CAPACITY], y_unit[KP_SPATIAL_CAPACITY], y_at[KP_SPATIAL_CAPACITY];
	int64_t count, max_w, max_h;
	int64_t mark[KP_UNITS + 1], seen[KP_UNITS + 1], stamp; /* query state */
	int64_t frames[KP_QUERY_ARENA], frame_top; /* the stacked results of nested queries (kp_query_rect) */
} KpSpatial;

/* The prepared step of a moving unit (kp_flingy_move.c; the step globals 0x543280..). */
typedef struct {
	int64_t next_x, next_y, old_x, old_y, next_halt_x, next_halt_y, step, walk_start, walk_stop, flags_before, flags_after;
} KpMove;

/* The mission's terrain contours (loaded by kp_start.c): per side (top, right, bottom, left) the
 * records 0..count-1. */
typedef struct {
	int32_t count;
	int64_t *axis, *start, *stop, *dir, *info; /* owned by the game */
} KpContourSide;


struct KpGame {
	KpImages *w;
	KpRngGame *rng;
	KpImages *own_world; /* the world kp_game_free releases (a native game's) */
	KpRngCrt *crt; /* g.crt (creep variants); NULL when the game has none */
	/* The game's own DAT (kp_game_use_dat): KP_DAT reads its columns. */
	const KpDat *cdat;
	const KpDatColumn *col[KP_COL_COUNT];
	void *ctx; /* the binding's object of the game */
	/* The hooks the C game leaves to its Lua binding (KP_HOOK_PLAY_PROBE of the tests), or NULL. */
	KpHookCall lua_hook;
	void *lua_hook_ctx;

	/* Settings fixed at the game's start (kp_start.c). */
	int64_t map_w, map_h, map_w_px, map_h_px, local_player, local_vision, expansion;

	int64_t scalar[KP_SCALAR_COUNT];
	uint8_t scalar_present[KP_SCALAR_COUNT];

	/* The units (kp_units.c) */
	KpUnits u;
	KpList lists[KP_LIST_COUNT];
	int64_t player_units[KP_PLAYERS];
	KpBullets bullets;

	/* The unit counters (kp_counts.c): per type and player (index type * 12 + player), per player */
	int64_t count_all[KP_UNIT_TYPES * KP_PLAYERS], count_completed[KP_UNIT_TYPES * KP_PLAYERS];
	int64_t count_kills[KP_UNIT_TYPES * KP_PLAYERS], count_deaths[KP_UNIT_TYPES * KP_PLAYERS];
	int64_t pstat[KP_PSTAT_ARRAYS][KP_PLAYERS];

	int64_t strength[2][KP_UNIT_TYPES]; /* kp_strength.c: air, ground */
	KpOrders orders;

	/* The selections (kp_selection.c) */
	int64_t local_selection[KP_SLOTS];
	int64_t selections[KP_SELECTION_PLAYERS][KP_SLOTS];
	int64_t groups[KP_SELECTION_PLAYERS][KP_GROUPS][KP_SLOTS];
	int64_t recent_stamps[KP_SELECTION_PLAYERS][KP_RECENT];
	int64_t circle_colors[KP_PLAYERS];

	/* The requirements (kp_requirements.c): the lazy offset arrays of each domain */
	int64_t req_offsets[KP_REQ_DOMAINS][KP_UNIT_TYPES];
	uint8_t req_offsets_ready[KP_REQ_DOMAINS];

	KpAi ai;
	KpAiRegions air;
	KpPlayerAi pai;
	KpPower power;
	int64_t power_head; /* 0x665b54 */
	/* The military reachability cache (0x6b1aa0, 0x6b68c0, 0x6b68e0): per player, a mark per region and the elapsed
	 * second and source region it was built at. */
	uint8_t ai_reach[KP_AI_PLAYERS][KP_AI_REGIONS];
	int64_t ai_reach_time[KP_AI_PLAYERS], ai_reach_source[KP_AI_PLAYERS];
	KpSpatial spatial;
	KpMove mv;
	KpMissionRegions regions; /* the mission's RGNS (game.regions) */
	KpContourSide contours[4];
	KpPathing pathing;
	KpCollision collision;
	KpTerrain terrain;
	KpPlayers players;
	KpEvents events;
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


/* DAT reads: KP_DAT(g, units, hit_points, t). */
#define KP_DAT(g, table, column, i) kp_dat_read((g), KP_COL_##table##_##column, (int64_t)(i))
/* DAT value i of a column of the game's own DAT; KP_NIL where the column has none (unequal to every
 * DAT value). */
int64_t kp_dat_read(KpGame *g, int col, int64_t i);
/* Writes value i of a column of the game's own DAT (a value the original changes while it runs). */
void kp_dat_write(KpGame *g, int col, int64_t i, int64_t v);
/* The same for a column named at run time (looked up on every call). */
int64_t kp_dat_read_named(KpGame *g, const char *table, const char *column, int64_t i);
/* Word k of a requirement domain (orders, research, tech_use, upgrades, units), KP_NIL past the end. */
int64_t kp_dat_requirement(KpGame *g, const char *domain, int64_t k);
/* Makes dat the game's own DAT (it must outlive the game); -1 with the missing column named in err. */
int kp_game_use_dat(KpGame *g, const KpDat *dat, KpError *err);

/* Unit record checks (a record outside 1..KP_UNITS fails). */
int64_t kp_game_unit(KpGame *g, int64_t n);
/* A flingy record (a unit's or a bullet's; a record outside 1..KP_FLINGIES fails). */
int64_t kp_game_flingy(KpGame *g, int64_t n);

/* The images world's unit and flingy contexts, saved by kp_ctx_unit (which sets the unit) and
 * restored by kp_ctx_restore. */
typedef struct {
	int32_t unit, flingy;
	int has_unit, has_flingy;
} KpCtx;
KpCtx kp_ctx_unit(KpGame *g, int64_t n);
void kp_ctx_restore(KpGame *g, KpCtx c);

/* kp_units.c */
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
void kp_unit_complete_in_place(KpGame *g, int64_t n);
void kp_unit_set_cloak_state(KpGame *g, int64_t n, int64_t state);
void kp_set_unit_hp(KpGame *g, int64_t n, int64_t hp);
void kp_update_damage_overlays(KpGame *g, int64_t n);
void kp_damage_overlay_add(KpGame *g, int64_t n);
void kp_damage_overlay_remove(KpGame *g, int64_t n);

/* A preplaced UNIT record (game/data/unit_records.lua). */
typedef struct {
	int64_t valid_states, valid_properties, hp_percent, shield_percent, energy_percent, resources, hangar, states;
} KpUnitRecord;

/* kp_map_units.c */
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
/* Burrow (kp_burrow.c): the orders Burrowing 116 and Unburrowing 118, RequestUnburrow 0x43c560. */
void kp_order_burrowing(KpGame *g, int64_t n);
void kp_order_unburrowing(KpGame *g, int64_t n);
void kp_request_unburrow(KpGame *g, int64_t n);
void kp_clear_burrowed(KpGame *g, int64_t n);
/* FindLegalMovePosition 0x42ce60 (kp_collision.c). */
void kp_find_legal_position(KpGame *g, int64_t n, int64_t *x, int64_t *y);
/* Spider Mines (kp_mines.c): PlaceMine 132, the mine's order 20, MoveToInterceptTarget 0x40c3f0 (kp_pursue). */
void kp_order_place_mine(KpGame *g, int64_t n);
void kp_order_spider_mine(KpGame *g, int64_t n);
void kp_pursue(KpGame *g, int64_t n, int64_t target);
/* ChangeUnitType 0x424ad0 (kp_morph.c): unit n becomes type t in place, with its subunit. */
void kp_change_unit_type(KpGame *g, int64_t n, int64_t t);
/* Siege Mode (kp_siege.c): the orders 98 and 99; the order a siege command gives unit n, 0 for none. */
void kp_order_sieging(KpGame *g, int64_t n);
void kp_order_unsieging(KpGame *g, int64_t n);
int64_t kp_siege_command_order(KpGame *g, int64_t n, int siege);
void kp_halt_at_target(KpGame *g, int64_t n);
int kp_facing_target_waypoint(KpGame *g, int64_t n);
int64_t kp_unit_race(KpGame *g, int64_t n);
void kp_attach_addon(KpGame *g, int64_t building, int64_t addon);
void kp_lift_off_initial(KpGame *g, int64_t n);
void kp_update_mineral_animation(KpGame *g, int64_t n);
void kp_unit_disable_doodad(KpGame *g, int64_t n);

/* Scalars: the value, KP_NIL when unset. */
int64_t kp_scalar(const KpGame *g, KpScalar s);
void kp_scalar_set(KpGame *g, KpScalar s, int64_t v);
void kp_scalar_clear(KpGame *g, KpScalar s);

/* The flingy direction tables (kp_directions.c). */
extern const int32_t KP_DIR_X[256];
extern const int32_t KP_DIR_Y[256];
extern const int64_t KP_SLOPES[64];

/* kp_game.c */
int64_t kp_upgrade_level(KpGame *g, int64_t player, int64_t id);
int kp_is_morphing_zerg_building(KpGame *g, int64_t n);
void kp_apply_creation_upgrades(KpGame *g, int64_t n);
/* The units in a rectangle, in the spatial index's order (kp_spatial.c). */
int kp_units_in_rect(KpGame *g, int64_t left, int64_t top, int64_t right, int64_t bottom, int64_t *out, int max);

/* kp_orders.c */
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
/* Appends an event (text may be NULL). */
void kp_event(KpGame *g, int64_t kind, int64_t a, int64_t b, int64_t c, const char *text);
/* Takes the oldest event into *out; 0 when the ring is empty. out->text (NULL for none) stays valid until the next
 * kp_event. */
typedef struct {
	int64_t kind, frame, seq, a, b, c;
	const char *text;
} KpEvent;
int kp_event_take(KpGame *g, KpEvent *out);
/* The local player's error notification (0x46ae50) and a text line for a player, as events. */
void kp_report_error(KpGame *g, int64_t player, int64_t id);
void kp_game_text(KpGame *g, int64_t player, const char *text);
/* PlaySoundAtUnit 0x446940: sound sfx at unit n as a sound event, when the local player hears it; unit 0 plays it
 * without a position. */
void kp_play_unit_sound(KpGame *g, int64_t sfx, int64_t n);
void kp_play_sound_at(KpGame *g, int64_t sfx, int64_t x, int64_t y);
/* Makes player p (0..11) the local player of the game and of its images world, with the vision derived from it as
 * the start derives it (a replay's world keeps the replay's vision); fails for another p. */
void kp_set_local_player(KpGame *g, int64_t p);
/* The state lines of tools/native_state.py (kp_dump.c): emit(ud, line, length) per line; drawn leaves
 * out the fields the original's drawing refreshes. -1 when out of memory. */
typedef void (*KpDumpLine)(void *ud, const char *line, size_t length);
int kp_dump(KpGame *g, int drawn, KpDumpLine emit, void *ud);
/* The CRC-32 of each category's dump lines joined by newlines (tools/state_check.py golden_lines): each(ud, category,
 * crc) in dump order. -1 when out of memory or when a category's lines are not in one run. */
typedef void (*KpCategoryCrc)(void *ud, const char *category, uint32_t crc);
int kp_dump_crcs(KpGame *g, int drawn, KpCategoryCrc each, void *ud);
/* Saves (kp_save.c, version 1): the game's mutable state with the mission's name and the data's identity, both
 * checked by kp_load, which restores into a game of the same mission and data (fresh or at any later step) and
 * leaves the game unchanged when it refuses the save. *out is malloc'd. */
#define KP_SAVE_VERSION 1
#define KP_SAVE_NAME 256
int kp_save(KpGame *g, const char *mission, const char *data, uint8_t **out, size_t *size, KpError *err);
int kp_load(KpGame *g, const uint8_t *bytes, size_t size, const char *mission, const char *data, KpError *err);
/* The hooks of the images world into a native game (kp_hooks.c), a KpHookCall with the game as context. */
int kp_game_hook(KpImages *w, void *ctx, KpHook hook, int op, const int64_t *args, int argc, int64_t *out);
/* The players' state (kp_game.c): a player outside 0..11 fails. */
int64_t kp_controller(KpGame *g, int64_t p);
int64_t kp_alliance(KpGame *g, int64_t a, int64_t b);
int64_t kp_shared_vision(KpGame *g, int64_t p);
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

/* kp_timers.c */
int64_t kp_energy_cap(KpGame *g, int64_t n);
void kp_update_unit_energy(KpGame *g, int64_t n);
void kp_update_acid_spores(KpGame *g, int64_t n);
void kp_update_spell_timers(KpGame *g, int64_t n);
void kp_update_unit_timers(KpGame *g, int64_t n);

/* kp_order_dispatch.c */
int kp_request_move_to_point(KpGame *g, int64_t n, int64_t x, int64_t y);
int kp_request_move_to_unit(KpGame *g, int64_t n, int64_t target);
int kp_move_toward_target(KpGame *g, int64_t n, int64_t target);
void kp_set_cloak_bits(KpGame *g, int64_t n, int64_t v);
int kp_order_has_handler(int64_t order);
int kp_order_handler_run(KpGame *g, int64_t order, int64_t n);
void kp_dispatch_main_order(KpGame *g, int64_t n);
void kp_dispatch_hidden_order(KpGame *g, int64_t n);
void kp_dispatch_secondary_order(KpGame *g, int64_t n);
void kp_secondary_cloak_order(KpGame *g, int64_t n, int64_t order);

/* kp_targets.c */
int kp_is_enemy(KpGame *g, int64_t n, int64_t target);
int kp_is_undetected_by(KpGame *g, int64_t n, int64_t viewer);
int64_t kp_ground_weapon(KpGame *g, int64_t n);
int64_t kp_ground_weapon_with_subunit(KpGame *g, int64_t n);
int64_t kp_air_weapon_with_subunit(KpGame *g, int64_t n);
int64_t kp_seek_range(KpGame *g, int64_t n);
int kp_can_attack(KpGame *g, int64_t n, int64_t target, int check_detection);
int64_t kp_acquire_target(KpGame *g, int64_t n);
int64_t kp_acquire_random_target(KpGame *g, int64_t n);

/* kp_requirements.c */
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

/* kp_owner.c */
void kp_remove_unit_ai(KpGame *g, int64_t n, int64_t killed);
void kp_assign_unit_ai(KpGame *g, int64_t n);
int kp_player_enemy_of(KpGame *g, int64_t p, int64_t n);
void kp_change_owner(KpGame *g, int64_t n, int64_t owner, int score);
void kp_give_unit(KpGame *g, int64_t n, int64_t owner);
void kp_apply_upgrade_to_units(KpGame *g, int64_t owner, int64_t upgrade);

/* kp_remove.c */
void kp_clear_references(KpGame *g, int64_t n, int full);
void kp_remove_from_selections(KpGame *g, int64_t n);
void kp_remove_from_local_selection(KpGame *g, int64_t n);
void kp_unit_hide(KpGame *g, int64_t n);
void kp_remove_body(KpGame *g, int64_t n);
void kp_unit_destroy(KpGame *g, int64_t n);
void kp_update_pending_units(KpGame *g);

/* kp_selection.c */
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

/* kp_formation.c */
void kp_group_formation(KpGame *g, int64_t x, int64_t y, int adjust, KpFormation *f);
void kp_formation_point(KpGame *g, int64_t n, const KpFormation *f, int64_t *x, int64_t *y);

/* kp_commands.c */
void kp_execute_command(KpGame *g, int64_t player, const uint8_t *bytes, size_t size);
void kp_run_turn(KpGame *g, const KpCommand *commands, int count);

/* kp_step.c */
void kp_step_init(KpGame *g);
void kp_set_context(KpGame *g, int64_t n);
void kp_update_units(KpGame *g);
void kp_step_objects(KpGame *g);
void kp_prime(KpGame *g);
void kp_step_frame(KpGame *g);
void kp_run_frame(KpGame *g, const KpCommand *commands, int count);

/* kp_step.c: the trigger clocks */
void kp_trigger_clock_init(KpGame *g, int has_triggers);
void kp_trigger_step(KpGame *g);

/* kp_step.c: the AI step and the guard records */
void kp_ai_init(KpGame *g);
void kp_ai_step(KpGame *g);
void kp_guard_init(KpGame *g);
void kp_guard_attach(KpGame *g, int64_t n);

/* kp_spatial.c */
void kp_spatial_clear(KpGame *g);
void kp_spatial_insert(KpGame *g, int64_t n);
void kp_spatial_remove(KpGame *g, int64_t n);
void kp_spatial_move(KpGame *g, int64_t n, int64_t dx, int64_t dy);
int kp_spatial_query(KpGame *g, int64_t left, int64_t top, int64_t right, int64_t bottom, int64_t *out, int max);
/* QueryUnitsInRect 0x422330 into the query frames (0x5d88f8): the units of the rectangle, stacked so that queries
 * nested inside the walk of an earlier one keep its results; kp_query_release (0x4222b0) drops the frame. */
int64_t *kp_query_rect(KpGame *g, int64_t left, int64_t top, int64_t right, int64_t bottom, int *count);
/* kp_query_overlap: the units of QueryUnitsOverlappingRect 0x4224c0 (kp_spatial_query) in a frame as kp_query_rect. */
int64_t *kp_query_overlap(KpGame *g, int64_t left, int64_t top, int64_t right, int64_t bottom, int *count);
void kp_query_release(KpGame *g, int64_t *frame);
/* The nearest unit to unit n (never n) that accept(g, candidate, arg) takes, within rect (left, top, right, bottom,
 * signed 16-bit) or within radius of n's sprite; 0 for none (0x442c20, 0x442bc0). */
typedef int (*KpUnitFilter)(KpGame *g, int64_t c, int64_t arg);
int64_t kp_nearest_unit_from(KpGame *g, int64_t n, const int64_t rect[4], KpUnitFilter accept, int64_t arg);
int64_t kp_nearest_unit_around(KpGame *g, int64_t n, int64_t radius, KpUnitFilter accept, int64_t arg);
int kp_spatial_overlap(KpGame *g, int64_t a, int64_t b);
void kp_unit_rect(KpGame *g, int64_t n, int64_t *left, int64_t *top, int64_t *right, int64_t *bottom);
void kp_update_overlap_flags(KpGame *g, int64_t n);

/* kp_spatial.c: the rectangle queries of the detection code (the rest is in kp_terrain.h) */
int kp_unit_box_within(KpGame *g, int64_t n, int64_t range, int64_t target);
/* GetUnitDistanceToUnit 0x422850 and GetUnitDistanceToPoint 0x422760 (a subunit measures from its parent). */
int64_t kp_unit_distance(KpGame *g, int64_t n, int64_t target);
int64_t kp_unit_distance_to_point(KpGame *g, int64_t n, int64_t x, int64_t y);

/* kp_flingy.c */
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
void kp_flingy_clamp_to_map(KpGame *g, int64_t n);
void kp_flingy_set_target_waypoint(KpGame *g, int64_t n, int64_t x, int64_t y);
void kp_flingy_set_move_target(KpGame *g, int64_t n, int64_t x, int64_t y);
void kp_flingy_set_move_target_unit(KpGame *g, int64_t n, int64_t target);
void kp_flingy_to_idle(KpGame *g, int64_t n);
void kp_flingy_set_speed(KpGame *g, int64_t n, int64_t v);
void kp_flingy_turn(KpGame *g, int64_t n, int64_t steps);
void kp_flingy_set_spawn_frame(KpGame *g, int64_t n, int64_t d);
void kp_nobrk_end(KpGame *g, int64_t n);

/* kp_flingy_move.c */
int64_t kp_approx_distance(int64_t dx, int64_t dy);
int64_t kp_heading_difference(int64_t a, int64_t b);
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
/* CommitFlingyMovement 0x40bdb0: true when the position changed. */
int kp_commit_flingy(KpGame *g, int64_t n);

/* kp_movement.c */
int64_t kp_move_arrival(KpGame *g, int64_t n);
int64_t kp_try_move_to_legal(KpGame *g, int64_t n);
void kp_stop_at_target(KpGame *g, int64_t n);
int kp_update_movement_state(KpGame *g, int64_t n, int repath);
void kp_execute_movement(KpGame *g, int64_t n);
void kp_stop_flingy(KpGame *g, int64_t n);
void kp_stop_movement(KpGame *g, int64_t n);
void kp_stop_and_face(KpGame *g, int64_t n);
void kp_prepare_movement(KpGame *g, int64_t n);

/* kp_strength.c */
void kp_strength_init(KpGame *g);
int64_t kp_unit_type_strength(KpGame *g, int64_t n, int kind);
int64_t kp_current_life(KpGame *g, int64_t n);
int64_t kp_max_hit_points(KpGame *g, int64_t n);
int64_t kp_max_life(KpGame *g, int64_t n);
int64_t kp_unit_current_strength(KpGame *g, int64_t n, int kind);
void kp_refresh_unit_strength(KpGame *g, int64_t n);

/* kp_combat.c */
int64_t *kp_cargo_slots(KpGame *g, int k);
int64_t kp_cargo_unit(KpGame *g, int64_t n, int k);
int kp_has_cargo(KpGame *g, int64_t n);
int64_t kp_first_cargo(KpGame *g, int64_t n);
/* kp_transport.c */
int kp_can_load(KpGame *g, int64_t p, int64_t t);
void kp_detach_from_transport(KpGame *g, int64_t p, int death);
int kp_unload_unit(KpGame *g, int64_t p);
void kp_cargo_at_removal(KpGame *g, int64_t n);
void kp_order_enter_transport(KpGame *g, int64_t n);
void kp_order_pickup(KpGame *g, int64_t n);
void kp_order_pickup_idle(KpGame *g, int64_t n);
void kp_order_move_unload(KpGame *g, int64_t n);
void kp_order_unload(KpGame *g, int64_t n);
void kp_bunker_muzzle_flash(KpGame *g, int64_t n);
/* kp_power.c */
void kp_power_init(KpGame *g);
int kp_point_powered(KpGame *g, int64_t type, int64_t x, int64_t y, int64_t player);
void kp_order_init_psi_provider(KpGame *g, int64_t n);
void kp_power_remove_provider(KpGame *g, int64_t n);
void kp_update_power(KpGame *g);
/* kp_shields.c */
int kp_can_recharge_from(KpGame *g, int64_t n, int64_t b);
void kp_order_recharge_shields(KpGame *g, int64_t n);
void kp_order_battery_recharge(KpGame *g, int64_t b);
void kp_order_battery_glow(KpGame *g, int64_t b);
void kp_order_recharge_done(KpGame *g, int64_t n);
/* kp_archon.c */
void kp_command_merge(KpGame *g, int64_t player, int dark);
void kp_order_archon_merge(KpGame *g, int64_t n);
void kp_order_completing_summon(KpGame *g, int64_t n);
/* kp_carrier.c */
void kp_hangar_unlink(KpGame *g, int64_t c);
void kp_order_carrier(KpGame *g, int64_t n);
void kp_order_carrier_attack(KpGame *g, int64_t n);
void kp_order_carrier_stop(KpGame *g, int64_t n);
void kp_order_carrier_ignore(KpGame *g, int64_t n);
void kp_order_interceptor_attack(KpGame *g, int64_t n);
void kp_order_interceptor_return(KpGame *g, int64_t n);
void kp_order_train_fighter(KpGame *g, int64_t n);
int64_t kp_miss_threshold(KpGame *g, int64_t n, int64_t target);
int kp_player_finished(KpGame *g, int64_t p);
void kp_record_death(KpGame *g, int64_t n, int64_t player);
void kp_increment_kills(KpGame *g, int64_t n);
void kp_notify_attacked(KpGame *g, int64_t n);
void kp_damage_hit_points(KpGame *g, int64_t n, int64_t damage, int64_t attacker, int64_t player, int notify);
void kp_on_unit_damaged(KpGame *g, int64_t n, int64_t attacker, int notify);
void kp_weapon_damage(KpGame *g, int64_t n, int64_t damage, int64_t weapon, int64_t divisor, int64_t direction,
	int64_t attacker, int64_t player);
void kp_bullet_damage(KpGame *g, int64_t n, int64_t b, int64_t divisor);
void kp_melee_damage(KpGame *g, int64_t n);

/* kp_attack.c */
int64_t kp_weapon_max_range(KpGame *g, int64_t n, int64_t weapon);
int kp_can_see_target(KpGame *g, int64_t n, int64_t target);
int kp_target_in_range(KpGame *g, int64_t n, int64_t target);
int64_t kp_weapon_cooldown(KpGame *g, int64_t n, int64_t weapon);
int kp_start_attack_animation(KpGame *g, int64_t n, int64_t anim);
int kp_unit_has_weapon(KpGame *g, int64_t n);
int64_t kp_target_priority(KpGame *g, int64_t n, int64_t target);
int kp_unit_unreachable(KpGame *g, int64_t n, int64_t target);
int kp_reject_chase(KpGame *g, int64_t n, int64_t target);
int kp_target_out_of_range(KpGame *g, int64_t n, int64_t target);
void kp_order_attack_unit(KpGame *g, int64_t n);
void kp_order_stay_in_range(KpGame *g, int64_t n);
void kp_order_turret_attack(KpGame *g, int64_t n);
int kp_attack_in_place(KpGame *g, int64_t n);
void kp_set_target_switch(KpGame *g, int64_t n);
void kp_update_attack_target(KpGame *g, int64_t n);
int kp_try_attack_auto_target(KpGame *g, int64_t n);
void kp_apply_stim(KpGame *g, int64_t n);
int kp_within_range_moving(KpGame *g, int64_t n, int64_t range, int64_t target);
int kp_weapon_ready(KpGame *g, int64_t n, int64_t weapon);
void kp_spawn_weapon_bullet(KpGame *g, int64_t n, int64_t weapon);
/* Spells (kp_spells.c): ValidateSpellTarget 0x434130, SpellTargetInvalid 0x434e70, FireWeaponFromUnit 0x41f4a0,
 * OrderCastSpell 0x434460, IsInfestableCommandCenter 0x41fed0. */
int kp_validate_spell_target(KpGame *g, int64_t caster, int64_t tech, int64_t target, int64_t type, int64_t x, int64_t y,
	int64_t *error);
int kp_spell_target_invalid(KpGame *g, int64_t n);
void kp_fire_weapon(KpGame *g, int64_t n, int64_t weapon);
void kp_order_cast_spell(KpGame *g, int64_t n);
void kp_order_defensive_matrix(KpGame *g, int64_t n);
void kp_order_feedback(KpGame *g, int64_t n);
void kp_order_mind_control(KpGame *g, int64_t n);
void kp_order_hallucination(KpGame *g, int64_t n);
void kp_order_recall(KpGame *g, int64_t n);
/* kp_hallucination.c */
int64_t kp_create_hallucination(KpGame *g, int64_t target, int64_t owner);
int kp_place_hallucination(KpGame *g, int64_t h);
void kp_order_hallucination_end(KpGame *g, int64_t n);
void kp_hallucination_death(KpGame *g, int64_t n);
int kp_infestable(KpGame *g, int64_t n);
/* Status effects (kp_status.c). */
int64_t kp_overlay_size(KpGame *g, int64_t n);
void kp_add_status_overlay(KpGame *g, int64_t n, int64_t id);
void kp_remove_status_overlay(KpGame *g, int64_t n, int64_t low, int64_t high);
int64_t kp_timed_life(KpGame *g, int64_t n);
void kp_apply_ensnare(KpGame *g, int64_t n);
void kp_end_ensnare(KpGame *g, int64_t n);
void kp_ensnare_area(KpGame *g, int64_t x, int64_t y, int64_t source);
void kp_apply_parasite(KpGame *g, int64_t target, int64_t owner, int64_t source);
void kp_spawn_broodlings(KpGame *g, int64_t queen, int64_t target);
void kp_ghost_release_nuke(KpGame *g, int64_t n);
void kp_disable_unit(KpGame *g, int64_t n);
void kp_apply_lockdown(KpGame *g, int64_t n, int64_t duration);
void kp_end_lockdown(KpGame *g, int64_t n);
void kp_end_maelstrom(KpGame *g, int64_t n);
void kp_apply_irradiate(KpGame *g, int64_t n, int64_t source, int64_t player);
void kp_update_irradiate(KpGame *g, int64_t n);
void kp_plague_area(KpGame *g, int64_t x, int64_t y, int64_t source);
void kp_update_plague(KpGame *g, int64_t n);
void kp_emp_shockwave(KpGame *g, int64_t x, int64_t y, int64_t source);
void kp_apply_consume(KpGame *g, int64_t defiler, int64_t target);
void kp_apply_optical_flare(KpGame *g, int64_t n, int64_t player);
void kp_apply_restoration(KpGame *g, int64_t n);
void kp_create_dark_swarm(KpGame *g, int64_t x, int64_t y, int64_t owner);
void kp_apply_matrix(KpGame *g, int64_t n);
void kp_reduce_matrix(KpGame *g, int64_t n, int64_t amount);
void kp_stasis_field(KpGame *g, int64_t x, int64_t y, int64_t source);
void kp_end_stasis(KpGame *g, int64_t n);
void kp_maelstrom(KpGame *g, int64_t x, int64_t y, int64_t source);
void kp_create_disruption_web(KpGame *g, int64_t x, int64_t y, int64_t owner);
int kp_move_to_point_throttled(KpGame *g, int64_t n, int64_t x, int64_t y);
int kp_ai_guard_home(KpGame *g, int64_t n, int64_t *x, int64_t *y);
void kp_order_computer_return(KpGame *g, int64_t n);
void kp_ai_attack_helpless(KpGame *g, int64_t enemy, int64_t radius, int second_pass);
int kp_ai_can_reach_point(KpGame *g, int64_t n, int64_t x, int64_t y);
void kp_ai_set_guard_home(KpGame *g, int64_t n, int64_t x, int64_t y);
void kp_flee_position(KpGame *g, int64_t attacker, int64_t n, int64_t *x, int64_t *y);
/* The Medic (kp_medic.c): FindHealTarget 0x431ce0 and the orders 175-179. */
int64_t kp_find_heal_target(KpGame *g, int64_t n);
void kp_order_medic_idle(KpGame *g, int64_t n);
void kp_order_medic_heal(KpGame *g, int64_t n);
void kp_order_heal_move(KpGame *g, int64_t n);
void kp_order_medic_hold(KpGame *g, int64_t n);
void kp_order_medic_heal_to_idle(KpGame *g, int64_t n);
void kp_impact_point(KpGame *g, int64_t n, int64_t *x, int64_t *y);
/* ClipSegmentToRect 0x48c6d0 (Cohen-Sutherland): 1 when the segment a-b meets rect {left, top, right, bottom}, clipped. */
int kp_clip_segment(KpGame *g, int64_t *a, int64_t *b, const int64_t *r);
int kp_within_distance(KpGame *g, int64_t n, int64_t dist, int64_t x, int64_t y);
void kp_issue_attack(KpGame *g, int64_t n, int64_t target, int ready, int ai_path);

/* kp_bullets.c */
int64_t kp_bullet_flingy(KpGame *g, int64_t b);
void kp_bullets_init(KpGame *g);
void kp_clear_last_bullet_spawner(KpGame *g);
void kp_bullets_step(KpGame *g);
void kp_clear_bullet_references(KpGame *g, int64_t n);
void kp_fire_weapon_by_selector(KpGame *g, int64_t n, int64_t selector);
void kp_bullet_hit(KpGame *g, int64_t b);
int64_t kp_current_bullet(KpGame *g);
int64_t kp_cover_miss_chance(KpGame *g, int64_t target);
int kp_bullet_goliath_trail(KpGame *g, int64_t b);
int kp_weapon_can_target(KpGame *g, int64_t n, int64_t weapon, int64_t target);

/* kp_ai.c */
void kp_ai_regions_init(KpGame *g, int campaign);
int64_t kp_unit_region(KpGame *g, int64_t n);
void kp_ai_step_region(KpGame *g, int64_t p, int64_t r);
void kp_ai_rebuild_enemy_strength(KpGame *g);
int kp_ai_campaign(KpGame *g, int64_t p);
int kp_ai_guard_return_home(KpGame *g, int64_t n, int check_only);
int kp_ai_update_attack_target(KpGame *g, int64_t n, int issue, int allow_critters, int strict);
int kp_ai_reaction_spell(KpGame *g, int64_t n, int reactive);
void kp_order_ai_guard(KpGame *g, int64_t n);
void kp_order_computer_ai(KpGame *g, int64_t n);
void kp_ai_consider_stim(KpGame *g, int64_t n);
int kp_ai_transport_for_move(KpGame *g, int64_t n, int64_t x, int64_t y);
void kp_ai_set_region_state(KpGame *g, int64_t p, int64_t r, int64_t state);
void kp_ai_react_to_attack(KpGame *g, int64_t n, int64_t a, int direct, int propagated);
int kp_ai_spell_cast_order(int64_t order);
int64_t kp_ai_unit_record_type(KpGame *g, int64_t n);
void kp_ai_remove_from_regions(KpGame *g, int64_t n, int64_t killed);
void kp_ai_detach_guard(KpGame *g, int64_t n);

/* kp_counts.c */
void kp_counts_init(KpGame *g);
void kp_account_live(KpGame *g, int64_t n, int64_t delta);
void kp_account_completed(KpGame *g, int64_t n, int64_t delta, int score);

#ifdef __cplusplus
}
#endif

#endif
