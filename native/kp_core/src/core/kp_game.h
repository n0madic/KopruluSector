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

#include "kp_aiscript.h"
#include "kp_images.h"
#include "kp_mission.h"
#include "kp_rng.h"
#include "kp_pathing.h"
#include "kp_start.h"
#include "kp_collision.h"
#include "kp_terrain.h"
#include "kp_dat.h"
#include "kp_dat_columns.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KP_UNITS 1700
#define KP_BULLETS 100
#define KP_HIT_SLOTS 32
#define KP_HIT_PAIRS 16
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
	/* The hit history of the Lurker's line (0x540768): KP_HIT_SLOTS slots of KP_HIT_PAIRS source and target pairs,
	 * the current slot in the scalar KP_HIT_SLOT (0x541768) and its used pairs (0x54176c). */
	int64_t hit_source[KP_HIT_SLOTS * KP_HIT_PAIRS], hit_target[KP_HIT_SLOTS * KP_HIT_PAIRS];
	int64_t hit_used;
} KpBullets;

/* The unit lists of game.lists, in the state model's order. */
enum { KP_LIST_ACTIVE, KP_LIST_PENDING, KP_LIST_FREE, KP_LIST_HIDDEN, KP_LIST_SPECIAL, KP_LIST_COUNT };

typedef struct KpGame KpGame;


/* Scalar fields of the game (a number or boolean, KP_NIL before it is set). */
typedef enum {
	KP_UNITS_ALLOCATED, KP_ERROR_CODE, KP_CLOAKED_HEAD, KP_TILE_COUNTDOWN, KP_UPDATE_TILES, KP_MOVE_REVEAL,
	KP_ORDER_SPREAD, KP_SECONDARY_SPREAD, KP_INTERCEPTOR_ALERTED, KP_HIT_SLOT, KP_FRAME, KP_TURN, KP_ELAPSED,
	KP_COMMAND_PLAYER, KP_SECOND_ORDER, KP_ORDER_MARKER, KP_POWER_DIRTY,
	KP_PLACEMENT_ACTIVE, KP_CHEATS, KP_REQUIREMENT_ERROR, KP_PATH_FREE_HEAD, KP_PATH_SUBJECT,
	KP_PATH_ALL_UNITS, KP_SCREEN_X, KP_SCREEN_Y, KP_SCREEN_W, KP_SCREEN_H, KP_PAUSED, KP_SPEED, KP_MISSION_INDEX,
	KP_PLACEMENT_ORDER, KP_PLACEMENT_TYPE,
	KP_SCALAR_COUNT
} KpScalar;

/* The scalar's name and whether it is a boolean (the C suites read the fields by name: kp_test_game.c). */
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
/* The longest program of the evaluator's program override (kp_requirement_program). */
#define KP_REQ_PROGRAM_WORDS 32

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
#define KP_AI_HEAP 63      /* the spending heap +0x18: 63 entries {priority, kind, WORD id, DWORD context} */
/* A spending request's kind (the heap entry's byte +1; kp_ai_spending.c): a unit, a guard's unit, a town's building,
 * worker, upgrade, tech or add-on, a support unit (an Observer, PushKind8UnitRequest 0x482930). */
enum {
	KP_AI_KIND_NONE = 0, KP_AI_KIND_UNIT = 1, KP_AI_KIND_GUARD = 2, KP_AI_KIND_BUILDING = 3, KP_AI_KIND_WORKER = 4,
	KP_AI_KIND_UPGRADE = 5, KP_AI_KIND_TECH = 6, KP_AI_KIND_ADDON = 7, KP_AI_KIND_MILITARY = 8
};
#define KP_AI_ROSTER 64    /* the attack roster +0x230: 63 WORDs (unit + 1) and the terminator WORD 63 */
#define KP_AI_ROSTER_UNITS 63 /* the roster's usable WORDs */
#define KP_AI_ROSTER_LOST 0xe5 /* a lost unit's entry (MarkRosterUnitLost 0x4839a0): past every type + 1 */
#define KP_AI_DEFENSE 20   /* a defense list: 20 WORDs (unit + 1) */
#define KP_AI_DEFENSE_LISTS 8 /* build gg, ag, ga, aa (+0x2b0, +0x2d8, +0x300, +0x328), use gg, ag, ga, aa (+0x350..) */
#define KP_PLAYER_AI_FIELDS(X) \
	X(heap_count) X(placement_throttle) X(nuke_rate) X(expansion_attacks) X(last_nuke) X(flags) X(panic) X(max_force) \
	X(attack_target) X(wanted_unit) X(default_min) X(spend_counter) X(timed_request) X(grouping_time) X(countdown0) \
	X(countdown1) X(countdown2) X(ai_state)
#define KP_PLAYER_AI_FIELD(name) int64_t name[KP_AI_PLAYERS];
typedef struct {
	KP_PLAYER_AI_FIELDS(KP_PLAYER_AI_FIELD)
	/* the available budget +0xc/+0x10 (AddAiBudget 0x482f00; the dump's category "economy") */
	int64_t minerals[KP_AI_PLAYERS], gas[KP_AI_PLAYERS];
	/* the dump's category "ai_lists": the demand +0/+4/+8 and the available supply +0x14, the spending heap, the
	 * attack roster, the defense lists, the unit build limits +0x3f0 (define_max: 0 none) and the 16-byte area
	 * +0x4d8 (set_gencmd, make_patrol's point) */
	int64_t demand_minerals[KP_AI_PLAYERS], demand_gas[KP_AI_PLAYERS], demand_supply[KP_AI_PLAYERS];
	int64_t supply[KP_AI_PLAYERS];
	int64_t heap_priority[KP_AI_PLAYERS][KP_AI_HEAP], heap_kind[KP_AI_PLAYERS][KP_AI_HEAP];
	int64_t heap_id[KP_AI_PLAYERS][KP_AI_HEAP], heap_context[KP_AI_PLAYERS][KP_AI_HEAP];
	int64_t roster[KP_AI_PLAYERS][KP_AI_ROSTER];
	int64_t defense[KP_AI_PLAYERS][KP_AI_DEFENSE_LISTS][KP_AI_DEFENSE];
	int64_t limits[KP_AI_PLAYERS][KP_UNIT_TYPES];
	int64_t area[KP_AI_PLAYERS][4];
	/* the player's first Medic +0x4d4 (RegisterFreeMedic 0x482520; ai_lists "medic", 0 none) */
	int64_t medic[KP_AI_PLAYERS];
} KpPlayerAi;

/* Protoss psi power (kp_power.c): the mask of the Psi field (0x665b50, half extents 0x665b58/0x665b5c in pixels) and
 * the previous links of the provider list (+0xf8; the next links are rally_unit, +0xfc). */
#define KP_POWER_MASK 1024
typedef struct {
	int64_t half_w, half_h;
	uint8_t mask[KP_POWER_MASK];
	int64_t prev[KP_UNITS + 1];
	int64_t fields_shown; /* the local player sees the Psi fields (0x665b64) */
} KpPower;

/* The AI step (kp_step.c) */
#define KP_AI_ANCHORS 24
typedef struct {
	int64_t terrain_changed, period, cycle, rounds, region_player, region_cursor, spending_player, town_player;
	/* the reactive flag a spell selector leaves for the next candidate tests (0x665d30) */
	int64_t spell_reactive;
	/* the town migration's lock 0x69b558 (MigrateTownIfEmpty 0x485840; set only within it, never saved) */
	int64_t migrating;
	/* the builder of the AI's placement search 0x6a2e80 (kp_ai_placement.c; set only within it, never saved) */
	int64_t placement_builder;
	/* the defense anchors 0x6a2e84 (count) and 0x6a5d70 (region ids) of ProduceDefenseAnchors 0x496e10 */
	int64_t anchor_count, anchors[KP_AI_ANCHORS];
} KpAi;

/* The AI guard records (kp_step.c): records 0..999, null link -1; time is the elapsed time of the last replacement
 * request (+0x1c, the dump's category "guard_time"). */
typedef struct {
	int64_t next[KP_GUARDS], prev[KP_GUARDS], kind[KP_GUARDS], unit[KP_GUARDS], type[KP_GUARDS];
	int64_t x1[KP_GUARDS], y1[KP_GUARDS], x2[KP_GUARDS], y2[KP_GUARDS], deaths[KP_GUARDS], time[KP_GUARDS];
	int64_t heads[8];
	int64_t free;
} KpGuards;

/* A unit's AI record (u.ai, +0x134): 0 for none, else the record's type (its byte +8) << 16 | its index in that
 * type's pool + 1: a guard (kp_step.c KpGuards), a town's worker or building (KpTowns), a military record. */
enum { KP_AI_GUARD = 1, KP_AI_WORKER = 2, KP_AI_BUILDING = 3, KP_AI_MILITARY = 4 };
#define KP_AI_REF(kind, k) ((int64_t)(kind) << 16 | ((int64_t)(k) + 1))
static inline int64_t kp_ai_ref_kind(int64_t ref) { return ref >> 16; }
static inline int64_t kp_ai_ref_index(int64_t ref) { return (ref & 0xffff) - 1; }

/* An AI context (a spending request's DWORD +4, a building's training context): 0 for none, else its kind << 16 |
 * its index + 1: an AI region record of the request's player (its region), a guard record, a town. The kinds are
 * the training tags' (0x484010). */
enum { KP_AI_CONTEXT_REGION = 1, KP_AI_CONTEXT_GUARD = 2, KP_AI_CONTEXT_TOWN = 3 };
#define KP_AI_CONTEXT(kind, k) ((int64_t)(kind) << 16 | ((int64_t)(k) + 1))
/* A training tag (a building record's bytes +9..+0xd) or a spending request's kind: a context's kind, or the tag of
 * an Observer request's unit or of a Silo's missile. */
enum { KP_AI_TAG_OBSERVER = 5, KP_AI_TAG_NUKE = 8 };

/* The computer's towns (kp_ai_town.c; atlas P29, docs/research/native-towns.md, native-workers.md): the Town pool
 * 0x690160 (100 records of 0x1cc bytes, shared by the players), the WorkerAI pool 0x67f7b0 (1000 of 24 bytes) and the
 * BuildingAI pool 0x685578 (1000 of 44 bytes), each a doubly linked free list (InitTownAiPools 0x486a20) whose records
 * keep their payload when freed, null link -1. A town's list links +0/+4 within its player's towns (heads 0x69b518
 * + 8p + 4), its worker and building list heads +0xc/+0x14, its owner +0x18, initialized +0x19, worker count +0x1a
 * (u8) and limit +0x1b, resource area +0x1c and gas refresh +0x1d, under-attack countdown +0x1e, centre +0x20/+0x22
 * (s16), main building +0x24, repair SCV +0x28, mineral field +0x2c and gas references +0x30..+0x38 (units), and its
 * 100 persistent requests +0x3c (u32 each: byte count << 3 | kind << 1 | special, byte priority, WORD id). A worker
 * record: type 2 +8, assignment +9 (1 minerals, 2..4 a gas reference), reassign count +0xa, wait +0xb, update time
 * +0xc, unit +0x10, town +0x14. A building record: type 3 +8, five training tags +9..+0xd (0 none, 1 an AI region
 * record, 2 a guard, 3 a town) with their contexts +0x18..+0x28 (that record's index), unit +0x10, town +0x14. */
#define KP_TOWNS 100
#define KP_TOWN_REQUESTS 100
#define KP_TOWN_GAS 3
#define KP_WORKER_AIS 1000
#define KP_BUILDING_AIS 1000
#define KP_TRAINING_TAGS 5
/* a town's links (its lists) and the rest of its record */
#define KP_TOWN_LINK_FIELDS(X) X(next) X(prev) X(workers) X(buildings)
#define KP_TOWN_DATA_FIELDS(X) \
	X(owner) X(initialized) X(worker_count) X(worker_limit) X(area) X(gas_refresh) X(under_attack) X(x) X(y) X(main) \
	X(repair) X(mineral)
#define KP_TOWN_FIELDS(X) KP_TOWN_LINK_FIELDS(X) KP_TOWN_DATA_FIELDS(X)
#define KP_WORKER_AI_FIELDS(X) X(next) X(prev) X(type) X(assignment) X(reassign) X(wait) X(time) X(unit) X(town)
#define KP_BUILDING_AI_FIELDS(X) X(next) X(prev) X(type) X(unit) X(town)
#define KP_TOWN_FIELD(name) int64_t name[KP_TOWNS];
#define KP_WORKER_AI_FIELD(name) int64_t name[KP_WORKER_AIS];
#define KP_BUILDING_AI_FIELD(name) int64_t name[KP_BUILDING_AIS];
typedef struct {
	KP_TOWN_FIELDS(KP_TOWN_FIELD)
	int64_t gas[KP_TOWNS][KP_TOWN_GAS];
	int64_t requests[KP_TOWNS][KP_TOWN_REQUESTS];
	int64_t heads[KP_AI_PLAYERS], free;
} KpTowns;
typedef struct {
	KP_WORKER_AI_FIELDS(KP_WORKER_AI_FIELD)
	int64_t free;
} KpWorkerAis;
typedef struct {
	KP_BUILDING_AI_FIELDS(KP_BUILDING_AI_FIELD)
	int64_t tags[KP_BUILDING_AIS][KP_TRAINING_TAGS], contexts[KP_BUILDING_AIS][KP_TRAINING_TAGS];
	int64_t free;
} KpBuildingAis;

/* The military records (kp_ai_military.c; atlas P37, P38): the pool 0x6acc50 (1000 records of 20 bytes, shared by the
 * players, InitMilitaryPool 0x4a9a90), each linked in its AI region record's list (KpAiRegions.military: the first
 * record + 1, 0 none): links +0/+4 (null -1), type 4 +8, the region of its last HarassMove +0xa, unit +0xc and region
 * record +0x10 (its player and region). */
#define KP_MILITARY_AIS 1000
#define KP_MILITARY_AI_FIELDS(X) X(next) X(prev) X(type) X(harass) X(unit) X(player) X(region)
#define KP_MILITARY_AI_FIELD(name) int64_t name[KP_MILITARY_AIS];
typedef struct {
	KP_MILITARY_AI_FIELDS(KP_MILITARY_AI_FIELD)
	int64_t free;
} KpMilitaryAis;

/* The AI's resource areas (kp_ai_areas.c; atlas P32, P33, docs/research/native-resource-generation.md,
 * native-resource-areas.md): the table 0x6a2e88 of 250 records of 48 bytes (record 0 unused): x/y +0/+2 (s16), the
 * mineral field and geyser counts +4/+5, start marker +6, flags +7 (1 occupied, 2 reserved), the remaining minerals
 * +8 and gas +0xc (u32); the used count 0x6a5d68 and the refresh countdown 0x6a5d6c. A resource's area is its byte
 * +0xd8 (the low byte of union_0xd8). */
#define KP_AREAS 250
#define KP_AREA_FIELDS(X) X(x) X(y) X(minerals) X(gas) X(start) X(flags) X(mineral_total) X(gas_total)
#define KP_AREA_FIELD(name) int64_t name[KP_AREAS];
typedef struct {
	KP_AREA_FIELDS(KP_AREA_FIELD)
	int64_t used, countdown;
} KpAreas;

/* The scratch of one AI search, per game (never saved): the area generation's resource classes and cluster seeds
 * (kp_ai_areas.c) and a computer Medic's heal candidates (kp_medic.c). */
typedef struct {
	int64_t box[4], weight, unit;
} KpAreaSeed;
typedef struct {
	uint8_t area_class[KP_UNITS];
	KpAreaSeed area_seeds[KP_UNITS];
	int64_t heal_candidates[KP_UNITS];
} KpAiScratch;

/* The AIScript VM (kp_aiscript.c; atlas P32, docs/research/native-ai-opcodes.md): the scripts of the pool 0x6a1a18
 * (InitAiScriptPool 0x493330), slots 0..99 with null link -1, the head of the active list 0x6a2e70 and of the free list
 * (pool +0x1450), and the return PC 0x6a2e7c that the call and return of every script share. A script's fields: its
 * PC +8 in its binary, its wait +0xc (u32), its player +0x10, its rectangle +0x14..+0x20 (signed left, top, right,
 * bottom), its centre +0x24/+0x28, its town +0x2c (-1 none) and its flags +0x30 (KP_AI_SCRIPT_BW the binary
 * bwscript.bin, 0x2 killable, 0x4 killed; start_town and start_areatown read the bits from 3). */
#define KP_AI_SCRIPTS 100
#define KP_AI_SCRIPT_BW 0x1
#define KP_AI_SCRIPT_FIELDS(X) \
	X(next) X(prev) X(pc) X(wait) X(player) X(left) X(top) X(right) X(bottom) X(x) X(y) X(town) X(flags)
#define KP_AI_SCRIPT_FIELD(name) int64_t name[KP_AI_SCRIPTS];
typedef struct {
	KP_AI_SCRIPT_FIELDS(KP_AI_SCRIPT_FIELD)
	int64_t head, free, ret;
} KpScripts;

/* The trigger engine (kp_triggers.c; atlas P40, P41, docs/research/native-triggers.md). */
#define KP_TRIGGER_RECORD 2400
#define KP_TRIGGER_PLAYERS 8
#define KP_LOCATIONS 255
#define KP_SWITCH_WORDS 8
#define KP_NEXT_SCENARIO 32
#define KP_UNIT_PROPERTIES 64
#define KP_UNIT_PROPERTY_SIZE 20
/* A location (MRGN 0x534f40): left, top, right, bottom (signed), name string, elevation flags. */
enum { KP_LOC_LEFT, KP_LOC_TOP, KP_LOC_RIGHT, KP_LOC_BOTTOM, KP_LOC_NAME, KP_LOC_ELEVATION, KP_LOC_FIELDS };
typedef struct {
	/* the dividers of the clocks: seconds 0x6ca744, trigger passes 0x6ca748, the result dialog 0x6ca74c, the
	 * leaderboard 0x6ca750 */
	int64_t second, pass, result, leaderboard;
	/* The copies of the player lists (0x50e0d8): each copy's 2400-byte record as the original's node keeps it (the
	 * raw TRIG record, its execution flags and continuation byte changing in place) and the next copy of its
	 * player's list (-1 ends); allocated at the start (kp_triggers_init), the game owns them. */
	uint8_t *records;
	int64_t *next;
	int64_t *place; /* each copy's place in its player's list (the trigger trace's trigger number) */
	int64_t count;
	int64_t head[KP_TRIGGER_PLAYERS];
	int64_t player, current;   /* 0x6ca754, the copy of 0x6ca790 (-1 none) */
	int64_t paused;            /* 0x6ca740: a trigger paused the game */
	int64_t result_dialog;     /* 0x6ca73c: the victory or defeat dialog is open */
	int64_t wait_active[KP_TRIGGER_PLAYERS], wait_remaining[KP_TRIGGER_PLAYERS]; /* 0x6ca760, 0x6ca768 */
	int64_t pending[KP_TRIGGER_PLAYERS];      /* 0x6ca758 */
	int64_t participants[KP_TRIGGER_PLAYERS]; /* 0x6ca788 */
	int64_t objectives[KP_TRIGGER_PLAYERS];   /* 0x5349a4: the mission objectives string of each player */
	int64_t lead_objectives;                  /* what the briefing's first tick sets (kp_briefing_lead), 0 none */
	int64_t switches[KP_SWITCH_WORDS];        /* 0x534f20 */
	int64_t countdown, countdown_paused;      /* 0x5349d4, 0x53632c */
	/* the countdown's text the presentation shows (UpdateCountdownText 0x4bde30 and HideCountdownText 0x4a79a0, both run
	 * by the second's pass): the seconds it formats, -1 hidden; not saved, a load sets it from the loaded countdown */
	int64_t countdown_text;
	int64_t quiet;                            /* 0x667280: unit speech muted (EnterQuietMode 0x446930) */
	int64_t locations[KP_LOCATIONS][KP_LOC_FIELDS];
	uint8_t next_scenario[KP_NEXT_SCENARIO];  /* 0x526526 */
	/* the leaderboard: include computers 0x5349e8, action type 0x5349ec, location 0x5349ed, type 0x5349ee, goal
	 * 0x5349f0, label 0x5349f4, the entries 0x6ca6d8 (player or -1, value) */
	int64_t lb_computers, lb_action, lb_location, lb_type, lb_goal, lb_label;
	int64_t lb_entries[KP_TRIGGER_PLAYERS][2];
	/* The unit-count cache of Command and Bring (kp_trigger_queries.c): its key 0x6ca6d0 (0 invalid, 1 global, 2 a
	 * location) and location 0x6ca6d1, the all-unit and completed-unit counters (0x6c7c10, 0x6c5150; index
	 * type * 12 + player) and the per-player statistics (0x6c4c10, the arrays of pstat). */
	int64_t count_key, count_location;
	int64_t count_all[KP_UNIT_TYPES * KP_PLAYERS], count_completed[KP_UNIT_TYPES * KP_PLAYERS];
	int64_t count_pstat[KP_PSTAT_ARRAYS][KP_PLAYERS];
	/* the mission's UPRP (0x6c4700): the unit properties of Create Unit with Properties, 64 records of 20 bytes */
	uint8_t properties[KP_UNIT_PROPERTIES * KP_UNIT_PROPERTY_SIZE];
	/* the mission's STR, copied: the texts of the actions */
	uint8_t *strings;
	size_t strings_size;
	/* the local player's briefing list 0x5082f8 (ExpandBriefingOwners 0x4607e0): MBRF copies of KP_TRIGGER_RECORD
	 * bytes, which the ready room's VM runs (kp_ui_briefing.c) */
	uint8_t *briefing;
	int64_t briefing_count;
} KpTriggers;

/* A command of the turn. */
typedef struct {
	int64_t player;
	const uint8_t *bytes;
	size_t size;
} KpCommand;
/* The most commands a caller hands to one turn (the oracle's runs, the replay tools). */
#define KP_TURN_COMMANDS 1024

/* Simulation -> presentation/audio/UI events (plan D15): a ring of records (kind, frame, sequence number, three numeric
 * arguments, a text slot), drained by the presentation (kp_event_take; the product's session, kp_session.c). A full
 * ring, or more texts waiting than their slots, fails: an event the consumer never sees would silently change what the
 * player hears or sees. A game without a presentation (the oracle runs, the replay tools) starts with the ring off
 * (KpStartInput.no_events): its events are not recorded at all. */
#define KP_EVENTS (4 * 5220)
#define KP_EVENT_TEXTS 64
#define KP_EVENT_TEXT_SIZE 256 /* the original formats a message into a 256-byte buffer (0x4cb860 for 0x46ac90) */
enum {
	/* The sounds carry their call's arguments in a (the KP_SOUND_* fields below): the interface plays them as the
	 * original does (the sound switch 0x6d3140, the enable bit of the options word 0x6d3148, the volume from the
	 * distance to the screen 0x4463e0 and minVolume against sounds.dat's 0x51e630, quiet mode's /5, the pan) and makes
	 * the talking request RequestUnitTalking 0x4a7200 that follows; the game's own gates (the local owner, the
	 * sprite's or the tile's visibility, a replay's vision) passed already.
	 * PlaySoundAtPosition 0x446a20 (PlayImageSound 0x40f5a0, OrderCastRecall 0x42fc40): a = sfx | minVolume << 16 |
	 * enable << 24, b = x, c = y (map pixels) */
	KP_EVENT_SOUND = 1,
	KP_EVENT_ERROR = 2, /* a = player, b = string id, c = 1 a rez\network.tbl string (the unit pool's, 0x425020), else
	                     * a stat_txt one (the local player's error notifications, 0x46ae50) */
	KP_EVENT_UNIT_UNPLACED = 3, /* a = unit type, b = x, c = y: a unit made that could not be placed (0x469460, 0x4261f0;
	                             * DisplaySystemMessage 0x46b070 of network.tbl 0x12 with its name and position) */
	/* a sound without a position (PlaySoundAtUnit 0x446940 without a unit; ShowErrorWithSound 0x4461f0 and the other
	 * advisor sounds, enable 1 and minVolume 99): a = the sound's arguments with the talking request, b = the talking
	 * request's unit or 0, c = 0 */
	KP_EVENT_SOUND_SCREEN = 4,
	/* the trigger trace (plan phase 5 item 8): an action that ran, a = player, b = the trigger's place in the
	 * player's list, c = action index << 16 | action type << 8 | the handler's result */
	KP_EVENT_TRIGGER = 5,
	/* the trigger actions' presentation (atlas P40; the interface answers a portrait's or a pan's end with
	 * kp_trigger_release_wait): a text, a = string id, b = milliseconds, c = shown even without subtitles (action flag
	 * 0x04; DisplayTriggerText 0x46aec0) */
	KP_EVENT_TRIGGER_TEXT = 6,
	KP_EVENT_TRIGGER_WAV = 7,      /* a = the WAV's string id (PlayStreamedSoundFile 0x49e810) */
	KP_EVENT_PORTRAIT = 8,         /* a = unit type, b = milliseconds, c = the unit or 0 (ShowTriggerPortrait 0x4a7320) */
	KP_EVENT_MINIMAP_PING = 9,     /* a = x, b = y, c = color (AddMinimapPing 0x46e8f0) */
	KP_EVENT_CAMERA = 10,          /* a = x, b = y of the screen's top-left (StartCameraPan 0x49be10); c = 1: the pan
	                                * ends (EndCameraPan 0x49be50) */
	KP_EVENT_TRIGGER_PAUSE = 11,   /* a = 1 a trigger paused the game, 0 it resumed it (0x4b6020, 0x4b6080) */
	KP_EVENT_QUIET = 12,           /* a = 1 unit speech muted, 0 unmuted (0x446930, 0x446fd0) */
	KP_EVENT_TRIGGER_SKIP = 13,    /* Escape skipped the transmissions: the streamed sounds, the portrait and the
	                                * trigger texts stop (0x49db50, 0x4a73e0, 0x46aef0) */
	KP_EVENT_GAME_RESULT = 14,     /* a = the local player's result (2 defeat, 3 victory, 5 draw): the result dialog
	                                * opens over the paused game (0x4bcf80, 0x4bcff0) */
	/* the AI trace (plan phase 7, a game that traces): an AIScript opcode the interpreter 0x493a60 dispatches, a =
	 * the script's player, b = its slot << 1 | its binary (KP_AI_SCRIPT_BW), c = its PC << 8 | the opcode */
	KP_EVENT_AI = 15,
	KP_EVENT_NETWORK_TEXT = 16,    /* a = a rez\network.tbl string id shown as a game message (0x46a860): the cheat
	                                * messages 0x41 "Cheat Enabled" and 0x42 "Cheat Disabled" (0x40d9d0) */
	KP_EVENT_SELECTION_SENT = 17,  /* the game took a unit out of the local selection where the original also tells the
	                                * network (RemoveUnitFromSelection 0x4a7b80: SendSelectionCommands 0x474130 is the
	                                * interface's): the selection left, 11 bits a unit (kp_selection_sent_unit), units
	                                * 0..3 in a, 4..7 in b, 8..11 in c */
	/* PlaySoundAtUnit 0x446940 with a unit the local player hears (its own, or its sprite seen; else, with a talking
	 * request regardless, the enable KP_SOUND_OFF): a = the sound's arguments with the talking request, b = the unit
	 * (the talking request's too), c = its sprite's x & 0xffff | (y & 0xffff) << 16 (signed 16-bit each) | its type
	 * << 32, at the call (a cancelled Zerg building plays its sound before it is a Drone again) */
	KP_EVENT_SOUND_UNIT = 18,
	/* NotifyUnitAttacked 0x446f00 past the game's gates (the local player's unit outside a replay; its +0x8a/+0x8b
	 * are written): a = the unit, b = 1 when its status has 0x2 (the voice 0x75 + the local race, else 0x78 + it),
	 * c = its sprite's x & 0xffff | (y & 0xffff) << 16 (signed 16-bit each). The rest is the interface's: the
	 * cooldown 0x66727c, the off-screen test 0x422c20 and +0x87, SuppressNearbyAttackNotices 0x4460d0, then the
	 * voice without a position (enable bit 0 of 0x6d3148, minVolume 0) and the talking request for the unit
	 * (camera 1, colour 0x10) whether it played or not */
	KP_EVENT_UNIT_ATTACKED = 19,
	/* RemoveFromSelections 0x49c8b0 took unit a out of every selection: the interface's network mirror too
	 * (0x472100) */
	KP_EVENT_SELECTION_GONE = 20,
	/* RequestGameExit 0x444e00 from a command (ExecRestartGame 0x472e20: a = 5, a single player's restart) */
	KP_EVENT_GAME_EXIT = 21,
	/* ExecSaveGame 0x472f50 / ExecLoadGame 0x4729e0 (a single player's): a = the time stamp / the save's ID, the text
	 * the save's name; the files are the host's */
	KP_EVENT_SAVE_GAME = 22,
	KP_EVENT_LOAD_GAME = 23,
	/* The game ended the local cursor modes: a = 1 CancelTargetingUi 0x423120 (the selection emptied: the placement and
	 * the targeting), 0 the placement's close by EndPlacement 0x47d300 (its builder can no longer place); kp_core closed
	 * the placement, its cursor, handlers and command card are the interface's (ClosePlacement 0x47d580) */
	KP_EVENT_CURSOR_MODES = 24
};
/* A sound event's a (KP_EVENT_SOUND, KP_EVENT_SOUND_SCREEN, KP_EVENT_SOUND_UNIT): the sound in bits 0..15, the call's
 * minVolume in 16..23, its enable in 24..31 (KP_SOUND_ON, KP_SOUND_OPTION + k, KP_SOUND_OFF), then the talking
 * request that follows: when in 32..33 (KP_TALK_*), its camera flag in 34 and its colour byte in 40..47 (the duration
 * is the sound's, 0x49db30 and 0x51dd40). */
enum {
	KP_SOUND_ON = 0,       /* enable: the constant 1 */
	KP_SOUND_OPTION = 1,   /* enable: KP_SOUND_OPTION + k is bit k (0..7) of the options word 0x6d3148 */
	KP_SOUND_OFF = 0xff,   /* enable: the constant 0, nothing plays (a talking request alone) */
	KP_TALK_NONE = 0,      /* no talking request */
	KP_TALK_ALWAYS = 1,    /* a talking request whether the sound played or not */
	KP_TALK_PLAYED = 2,    /* a talking request when the sound played (0x446940 returned nonzero) */
	KP_VOICE_VOLUME = 99,  /* the minVolume of the voices and of the advisor's sounds */
	KP_VOICE_CAMERA = 1,   /* the voices' talking request: its camera flag (the last event's place 0x527014, the
	                        * unit's minimap blink +0x8a/+0x8b and the minimap ping 0x46e8f0) */
	KP_VOICE_COLOUR = 0x11, /* and its colour (0x10: the under-attack voice's, NotifyUnitAttacked 0x446f00) */
	KP_SOUND_MIN_VOLUME_SHIFT = 16, KP_SOUND_ENABLE_SHIFT = 24, KP_SOUND_TALK_SHIFT = 32, KP_SOUND_CAMERA_SHIFT = 34,
	KP_SOUND_COLOUR_SHIFT = 40
};
typedef struct {
	int64_t first, count, sequence, text_next, texts_waiting;
	int off; /* the game records no events */
	int trace; /* the game records the trigger and AI traces (KP_EVENT_TRIGGER, KP_EVENT_AI; KpStartInput.trace_triggers) */
	int64_t kind[KP_EVENTS], frame[KP_EVENTS], seq[KP_EVENTS], a[KP_EVENTS], b[KP_EVENTS], c[KP_EVENTS];
	int32_t text[KP_EVENTS]; /* slot in texts, -1 for none */
	char texts[KP_EVENT_TEXTS][KP_EVENT_TEXT_SIZE];
} KpEvents;

/* The players (kp_game.c, kp_start.c): the runtime controller, race and force of each slot, the alliance matrix,
 * shared vision, upgrade levels and their maxima and tech states (0 when unset), and the resources: current minerals
 * 0x5263d0 and gas 0x526400, and the cumulative minerals 0x526460 and gas 0x526430 (u32 values). */
#define KP_UPGRADE_IDS 64
#define KP_TECH_IDS 64
/* the ids of the original's arrays: techs 0..43, upgrades 0..60 (the expansion's from 24 and 46) */
#define KP_TECHS 44
#define KP_UPGRADES 61
#define KP_TECH_PROGRESS_BYTES 6
#define KP_UPGRADE_PROGRESS_BYTES 8
typedef struct {
	int64_t controller[KP_PLAYERS], race[KP_PLAYERS], force[KP_PLAYERS];
	int64_t alliance[KP_PLAYERS][KP_PLAYERS];
	int64_t shared_vision[KP_PLAYERS];
	int64_t upgrades[KP_PLAYERS][KP_UPGRADE_IDS], upgrade_max[KP_PLAYERS][KP_UPGRADE_IDS];
	/* the unit availability 0x52655c (the map's PUNI over 1) */
	int64_t unit_available[KP_PLAYERS][KP_UNIT_TYPES];
	int64_t tech_available[KP_PLAYERS][KP_TECH_IDS], tech_researched[KP_PLAYERS][KP_TECH_IDS];
	/* the in-progress bits of research 0x536510 (6 bytes a player) and upgrades 0x5366c0 (8 bytes) */
	int64_t research_progress[KP_PLAYERS][KP_TECH_PROGRESS_BYTES], upgrade_progress[KP_PLAYERS][KP_UPGRADE_PROGRESS_BYTES];
	int64_t minerals[KP_PLAYERS], gas[KP_PLAYERS], cumulative_minerals[KP_PLAYERS], cumulative_gas[KP_PLAYERS];
	/* the start locations 0x534a00 (WORD pairs; 0, 0 for none), the map's last of each player (0x46a280) */
	int64_t start_x[KP_PLAYERS], start_y[KP_PLAYERS];
	/* the map's force names (FORC: string ids, 0x53487e's source); the mission's start sets them (not saved) */
	int64_t force_name[4];
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
	/* The CRT rand of the creep variants; NULL when the game has none. The interface's unit voices (kp_ui_voice.c,
	 * 0x447130, 0x447190) draw from it too, as an oracle run's "crt" events do. */
	KpRngCrt *crt;
	/* The game's own DAT (kp_game_use_dat): KP_DAT reads its columns. */
	const KpDat *cdat;
	const KpDatColumn *col[KP_COL_COUNT];
	void *ctx; /* the host's object of the game (kp_host.c's KpHostGame) */
	/* The hooks the C game leaves to its caller (KP_HOOK_PLAY_PROBE of tests/c/test_orders_native.c), or NULL. */
	KpHookCall probe_hook;
	void *probe_hook_ctx;

	/* Settings fixed at the game's start (kp_start.c). */
	int64_t map_w, map_h, map_w_px, map_h_px, local_player, local_vision, expansion;
	/* the whole-map rectangle 0x543228 {left, top, right, bottom} of the nearest searches, which a dropped powerup's
	 * placement clamps in place (0x4272c0 through 0x47dc30) */
	int64_t map_rect[4];

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
	/* For the interface (kp_ui; neither state nor saved): the local selection's sets (SetSelection 0x49cf80 ends in
	 * OnSelectionChanged 0x4a7b60) and the refreshes the game asks of the console (RefreshStatusUi 0x4a7ed0). */
	int64_t local_selection_serial, interface_refresh;
	int64_t minimap_terrain; /* the terrain changes the minimap must show (MarkMinimapTerrainDirty 0x46e8c0) */
	int64_t load_serial;     /* the saves loaded into the game (kp_load): the interface takes the game anew */

	/* The requirements (kp_requirements.c): the lazy offset arrays of each domain */
	int64_t req_offsets[KP_REQ_DOMAINS][KP_UNIT_TYPES];
	uint8_t req_offsets_ready[KP_REQ_DOMAINS];
	/* The evaluator's program override of the tests (kp_requirement_program): while req_program_set, word k of every
	 * domain is req_program[k], the synthetic tables of the original-code probes. Not game state (never saved). */
	int64_t req_program[KP_REQ_PROGRAM_WORDS];
	int64_t req_program_words;
	uint8_t req_program_set;

	KpAi ai;
	KpAiRegions air;
	KpPlayerAi pai;
	/* the price cache of the cost checks (0x53dad0 minerals, 0x53db00 gas, 0x53db30 supply), read by the debit 0x408650 */
	int64_t cost_minerals[KP_PLAYERS], cost_gas[KP_PLAYERS], cost_supply[KP_PLAYERS];
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
	/* The AIScript VM (kp_aiscript.c) over the game's AI script binaries (KpStartInput.ai_bins), or while
	 * ai_program is set over the tests' synthetic binaries (kp_aiscript_program; not game state, never saved) */
	const KpAiBins *ai_bins, *ai_game_bins;
	KpAiBins ai_program;
	KpScripts scripts;
	/* The towns and their workers and buildings (kp_ai_town.c) */
	KpTowns towns;
	KpWorkerAis workers;
	KpBuildingAis buildings;
	KpMilitaryAis military;
	KpAreas areas;
	KpAiScratch ai_scratch;
	KpTriggers trig;
	int64_t results[8]; /* the committed results 0x5349e0 (0 none, 1 dropped, 2 defeated, 3 victory, 4 ignore defeat, 5 draw) */
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
int32_t kp_flingy_sprite(KpGame *g, int64_t n);
void kp_units_init(KpGame *g);
int64_t *kp_queue_entry(KpGame *g, int64_t n, int64_t slot);
int64_t *kp_queue_head(KpGame *g, int64_t n);
int64_t kp_queued_type(KpGame *g, int64_t n);
int64_t *kp_union_word(KpGame *g, int word);
void kp_set_union_unit(KpGame *g, int64_t n, int word, int64_t target);
/* A field of `size` bytes (1, 2 or 4) of the union at CUnit byte `offset` (0xc0..0xdb), unsigned; the setter keeps
 * the rest of the word. */
int64_t kp_union_get(KpGame *g, int64_t n, int offset, int size);
void kp_union_set(KpGame *g, int64_t n, int offset, int size, int64_t v);
#define KP_UNION_PEER 4 /* the union word +0xd0: a Nydus Canal's exit, an add-on's building */
int kp_unit_is_dead(KpGame *g, int64_t n);
int kp_unit_inactive(KpGame *g, int64_t n);
/* The Ghost family: Ghost 1, Sarah Kerrigan 16, Alexei Stukov 99, Samir Duran 100, Infested Duran 104. */
int kp_ghost_type(int64_t t);
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
void kp_report_creation_error(KpGame *g, int64_t player);
void kp_replace_unit_images(KpGame *g, int64_t n, int construction);
void kp_sprite_replace_main(KpGame *g, int32_t s, int64_t id, int64_t heading);
void kp_rebuild_status_overlays(KpGame *g, int64_t n);
void kp_unit_activate_spatial(KpGame *g, int64_t n);
void kp_add_building_occupancy(KpGame *g, int64_t n);
void kp_building_occupancy_at(KpGame *g, int64_t t, int64_t x, int64_t y, int set);
void kp_remove_building_occupancy(KpGame *g, int64_t n);
void kp_unit_deactivate_spatial(KpGame *g, int64_t n);
void kp_reset_movement(KpGame *g, int64_t n);
void kp_reset_to_idle_animation(KpGame *g, int64_t n);
void kp_unit_show(KpGame *g, int64_t n);
void kp_unit_detach(KpGame *g, int64_t n, int hide);
/* Nydus Canals (kp_transport.c): the entry test 0x43c360 and EnterNydusCanal 47 (0x43ccc0). */
int kp_can_enter_nydus(KpGame *g, int64_t n, int64_t canal);
void kp_order_enter_nydus(KpGame *g, int64_t n);
/* OrderTransportRequest 0x437a00 (order 96; kp_transport.c). */
void kp_order_transport_request(KpGame *g, int64_t n);
void kp_unit_reattach(KpGame *g, int64_t n);
void kp_unit_prefinish(KpGame *g, int64_t n);
void kp_clamp_to_map(KpGame *g, int64_t t, int64_t *x, int64_t *y);
int kp_tile_has_flags(KpGame *g, int64_t x, int64_t y, int64_t required, int64_t forbidden);
/* The point's tile lacks the player's bit in the tile flags 0x666454: the player sees it (kp_fog.c). */
int kp_point_visible_to(KpGame *g, int64_t x, int64_t y, int64_t player);
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

/* A preplaced UNIT record (the mission's UNIT section; kp_start.c). */
typedef struct {
	int64_t valid_states, valid_properties, hp_percent, shield_percent, energy_percent, resources, hangar, states;
} KpUnitRecord;

/* kp_map_units.c */
int64_t kp_create_map_unit(KpGame *g, int64_t t, int64_t x, int64_t y, int64_t owner);
void kp_apply_unit_record(KpGame *g, int64_t n, const KpUnitRecord *r);
int64_t kp_create_unit_checked(KpGame *g, int64_t t, int64_t x, int64_t y, int64_t owner);
int64_t kp_hangar_child_type(int64_t t);
int kp_gas_building(int64_t t);
int kp_mineral_field(int64_t t);
int kp_resource_type(int64_t t);
int64_t kp_hangar_capacity(KpGame *g, int64_t n);
void kp_create_hangar_units(KpGame *g, int64_t n, int64_t count);
void kp_hangar_add(KpGame *g, int64_t n, int64_t c);
void kp_queue_fighter(KpGame *g, int64_t n);
int64_t kp_cloak_tech(KpGame *g, int64_t n);
void kp_cloak_list_add(KpGame *g, int64_t n);
void kp_try_start_cloak(KpGame *g, int64_t n);
void kp_make_neutral(KpGame *g, int64_t a);
void kp_detach_addon(KpGame *g, int64_t n);
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
/* The Infested Terran's SapUnit 0x86 (0x41ef70) and SapLocation 0x87 (0x41eeb0). */
void kp_order_sap_unit(KpGame *g, int64_t n);
void kp_order_sap_location(KpGame *g, int64_t n);
void kp_pursue(KpGame *g, int64_t n, int64_t target);
/* ChangeUnitType 0x424ad0 (kp_morph.c): unit n becomes type t in place, with its subunit. */
void kp_change_unit_type(KpGame *g, int64_t n, int64_t t);
/* A unit's cloak drawing across new images (kp_morph.c): its mode (status bits 0x100, 0x200) and the main image's two
 * coloring bytes (GetMainImageColorBytes 0x416030), then drawn again (0x415e50), detected by the local player
 * through `detector`'s detection (0x417400). */
typedef struct {
	int32_t mode, a, b;
} KpCloak;
KpCloak kp_unit_save_cloak(KpGame *g, int64_t n);
void kp_unit_restore_cloak(KpGame *g, int64_t n, KpCloak c, int64_t detector);
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
/* The cheat flags (0 before they are set). */
int64_t kp_cheats(KpGame *g);
/* A CRT rand draw of the interface (a unit voice's choice), which the creep tile art shares; a failure without the
 * game's generator. */
void kp_interface_crt_draw(KpGame *g);
/* The interface's srand 0x4d14f5 of the CRT generator: a typed line's ProcessCheatText 0x40dee0 ends with
 * srand(time(NULL)). */
void kp_interface_crt_seed(KpGame *g, int64_t seed);

/* The flingy direction tables (kp_directions.c). */
extern const int32_t KP_DIR_X[256];
/* GetDirectionVector 0x40c270: (KP_DIR_X[dir] * len) >> 8 and the same for y (low 32-bit product, arithmetic shift). */
void kp_direction_vector(int64_t dir, int64_t len, int64_t *dx, int64_t *dy);
extern const int32_t KP_DIR_Y[256];
extern const int64_t KP_SLOPES[64];

/* kp_game.c */
int64_t kp_upgrade_level(KpGame *g, int64_t player, int64_t id);
/* GetArmorUpgradeBonus 0x43ab10 (kp_combat.c). */
int64_t kp_armor_bonus(KpGame *g, int64_t n);
/* IsDetector 0x431a90 (kp_detection.c): a completed detector that is neither disabled nor blind. */
int kp_is_detector(KpGame *g, int64_t n);
/* SetUpgradeLevel 0x49a370 (the expansion's ids only in an expansion game). */
void kp_set_upgrade_level(KpGame *g, int64_t p, int64_t id, int64_t v);
/* ExecCheat 0x472530 (command 0x12) with the new cheat word (kp_cheats.c). */
void kp_exec_cheat(KpGame *g, int64_t word);
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
/* PrependOrNewOrder 0x429b20: order before the queue's head, or alone in an empty queue (not activated). */
void kp_order_prepend(KpGame *g, int64_t n, int64_t order);
/* PrependOrNewOrderAt 0x429c40 and PrependOrNewOrderOnUnit 0x429bc0 (at the target's position); not activated. */
void kp_order_prepend_at(KpGame *g, int64_t n, int64_t order, int64_t x, int64_t y, int64_t target);
void kp_order_prepend_target(KpGame *g, int64_t n, int64_t order, int64_t target);
/* Order node i, checked against the pool. */
int64_t kp_order_node(KpGame *g, int64_t i);
/* Appends an event (text may be NULL). */
void kp_event(KpGame *g, int64_t kind, int64_t a, int64_t b, int64_t c, const char *text);
/* KP_EVENT_SELECTION_SENT with the local selection, and its unit k (0..11; 0 = none) back from the event's fields. */
void kp_event_selection_sent(KpGame *g);
int64_t kp_selection_sent_unit(int64_t a, int64_t b, int64_t c, int k);
/* Takes the oldest event into *out; 0 when the ring is empty. out->text (NULL for none) stays valid until the next
 * kp_event. */
typedef struct {
	int64_t kind, frame, seq, a, b, c;
	const char *text;
} KpEvent;
int kp_event_take(KpGame *g, KpEvent *out);
/* The index-th waiting event (0 the oldest) without taking it; 0 past the last. */
int kp_event_peek(KpGame *g, int64_t index, KpEvent *out);
/* The sequence number of the last event appended. */
int64_t kp_event_sequence(KpGame *g);
/* The local player's error notification (0x46ae50) and a unit that could not be placed (KP_EVENT_UNIT_UNPLACED), as
 * events. */
void kp_report_error(KpGame *g, int64_t player, int64_t id);
void kp_report_error_sound(KpGame *g, int64_t player, int64_t sound, int64_t id);
void kp_report_unplaced(KpGame *g, int64_t t, int64_t x, int64_t y);
/* PlaySoundAtUnit 0x446940 (ECX = sfx, EDX = unit n or 0, stack [enable, minVolume]) as a sound event, with the
 * talking request RequestUnitTalking 0x4a7200 its caller makes for n (talk KP_TALK_*, its camera flag and colour). */
void kp_unit_sound(KpGame *g, int64_t sfx, int64_t n, int64_t enable, int64_t min_volume, int64_t talk, int64_t camera,
	int64_t colour);
/* PlaySoundAtUnit 0x446940 with the stack [1, 0] most of its callers pass, without a talking request. */
void kp_play_unit_sound(KpGame *g, int64_t sfx, int64_t n);
/* The advisor's sound without a position (enable 1, minVolume 99; ShowErrorWithSound 0x4461f0,
 * PlayResearchCompleteVoice 0x446b70), with a talking request for unit n (talk KP_TALK_*). */
void kp_advisor_sound(KpGame *g, int64_t sfx, int64_t n, int64_t talk, int64_t camera, int64_t colour);
/* PlaySoundAtPosition 0x446a20 with EDX = 1 and minVolume 0, as both its callers pass them. */
void kp_play_sound_at(KpGame *g, int64_t sfx, int64_t x, int64_t y);
/* Unit n's sprite position as the events carry it: x & 0xffff | (y & 0xffff) << 16 (signed 16-bit each). */
int64_t kp_unit_sprite_packed(KpGame *g, int64_t n);
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
void kp_set_alliance(KpGame *g, int64_t a, int64_t b, int64_t v);
int64_t kp_shared_vision(KpGame *g, int64_t p);
void kp_order_remove(KpGame *g, int64_t n, int64_t node);
void kp_order_clear_queue(KpGame *g, int64_t n);
void kp_set_order(KpGame *g, int64_t n, int64_t order);
void kp_set_order_at(KpGame *g, int64_t n, int64_t order, int64_t x, int64_t y);
void kp_set_order_fow(KpGame *g, int64_t n, int64_t order, int64_t fow, int64_t x, int64_t y);
void kp_set_order_target(KpGame *g, int64_t n, int64_t order, int64_t target);
void kp_set_order_unit(KpGame *g, int64_t n, int64_t t);
void kp_set_secondary_order(KpGame *g, int64_t n, int64_t order);
void kp_order_activate(KpGame *g, int64_t n);
void kp_order_done(KpGame *g, int64_t n);
void kp_set_secondary_order_force(KpGame *g, int64_t n, int64_t order);
void kp_order_unlock_done(KpGame *g, int64_t n);
void kp_clear_order_target(KpGame *g, int64_t n);
int kp_is_subunit(KpGame *g, int64_t n);
void kp_set_order_no_target(KpGame *g, int64_t n, int64_t order);
/* Scanner Sweep (kp_spells.c): orders 139 (0x436990) and 140 (0x436a60). */
void kp_order_scanner_sweep(KpGame *g, int64_t n);
void kp_order_scanner(KpGame *g, int64_t n);
/* Infestation (kp_spells.c): orders 27 (0x43cf10), 29 (0x43cfe0) and 15 (0x43c5a0). */
void kp_order_cast_infestation(KpGame *g, int64_t n);
void kp_order_infesting(KpGame *g, int64_t n);
void kp_order_infested_command_center(KpGame *g, int64_t n);
/* NeedsMoreEnergy 0x434b50 and SpendEnergy 0x435300 (both skipped under the energy cheat). */
int kp_needs_energy(KpGame *g, int64_t n, int64_t cost);
int64_t kp_tech_energy(KpGame *g, int64_t tech);
int64_t kp_order_energy(KpGame *g, int64_t order);
void kp_spend_energy(KpGame *g, int64_t n, int64_t cost);
void kp_unit_kill(KpGame *g, int64_t n);
void kp_critter_annoyed(KpGame *g, int64_t n);
void kp_kill_flagged(KpGame *g, int64_t n);
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
void kp_ai_patrol_point(KpGame *g, int64_t n, int64_t *x, int64_t *y);
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
int64_t kp_can_train_unit(KpGame *g, int64_t t, int64_t player, int64_t n);
/* kp_research.c: GetTechCost 0x408790, GetUpgradeCost 0x408820 (the next level's), the AI's price checks CheckTechCost
 * 0x408670 and CheckUpgradeCost 0x408710 (with feedback), and the starts StartResearch 0x43af40 and StartUpgrade
 * 0x43afb0 (0 when the stock does not cover the price). */
void kp_tech_price(KpGame *g, int64_t tech, int64_t *minerals, int64_t *gas);
void kp_upgrade_price(KpGame *g, int64_t p, int64_t id, int64_t *minerals, int64_t *gas);
int kp_check_tech_cost(KpGame *g, int64_t p, int64_t tech);
int kp_check_upgrade_cost(KpGame *g, int64_t p, int64_t id);
int kp_start_research(KpGame *g, int64_t n, int64_t tech);
int kp_start_upgrade(KpGame *g, int64_t n, int64_t id);
int64_t kp_can_research(KpGame *g, int64_t tech, int64_t player, int64_t n);
int64_t kp_can_upgrade(KpGame *g, int64_t id, int64_t player, int64_t n);
/* Something queued, an add-on or a Protoss building under construction (IsUnitBusyProducing 0x407b10); occupied adds
 * a research or an upgrade (+0xc8, +0xc9). */
int kp_unit_producing(KpGame *g, int64_t n);
int kp_unit_occupied(KpGame *g, int64_t n);
/* The requirement offset of id in a domain (the lazy offset initializers 0x407d50, 0x407f20, 0x4080d0, 0x408280,
 * 0x4b3230). */
int64_t kp_requirement_offset(KpGame *g, int domain, int64_t id);
/* The evaluator's program override, a test hook: words[0..count) replace the word stream of every requirement domain
 * (word k reads words[k], a word from count on fails) until a call with count < 0 clears it; every call drops the
 * offsets resolved so far, which are read again from the DAT's offset columns. */
void kp_requirement_program(KpGame *g, const int64_t *words, int count);

/* kp_harvest.c: the worker orders 79, 81-90 and 151, the resources' service lists, cargo and the searches for depots
 * and mineral fields. */
void kp_order_move_to_gas(KpGame *g, int64_t n);
void kp_order_wait_for_gas(KpGame *g, int64_t n);
void kp_order_harvest_gas(KpGame *g, int64_t n);
void kp_order_return_cargo(KpGame *g, int64_t n);
void kp_order_move_to_minerals(KpGame *g, int64_t n);
void kp_order_wait_for_minerals(KpGame *g, int64_t n);
void kp_order_mining_minerals(KpGame *g, int64_t n);
void kp_order_harvest3(KpGame *g, int64_t n);
void kp_order_harvest4(KpGame *g, int64_t n);
void kp_order_reset_harvest_collision(KpGame *g, int64_t n);
int64_t kp_harvest_right_click_order(KpGame *g, int64_t w, int64_t target);
void kp_harvest_cancel(KpGame *g, int64_t r, int64_t w);
void kp_harvest_detach_all(KpGame *g, int64_t r);
void kp_harvest_resume(KpGame *g, int64_t w);
int32_t kp_carried_image(KpImages *w, int32_t s);
int64_t kp_remove_cargo_images(KpGame *g, int64_t n);
int64_t kp_nearest_depot(KpGame *g, int64_t w);

/* kp_larva.c */
void kp_secondary_spawning_larva(KpGame *g, int64_t p);
void kp_order_larva(KpGame *g, int64_t n);
void kp_order_zerg_unit_morph(KpGame *g, int64_t n);
void kp_order_zerg_birth(KpGame *g, int64_t n);

/* kp_powerup.c */
void kp_order_powerup_idle(KpGame *g, int64_t p);
void kp_order_powerup_return(KpGame *g, int64_t p);
void kp_drop_powerup(KpGame *g, int64_t w);
void kp_destroy_worker_cargo(KpGame *g, int64_t w);

/* kp_production.c: prices, supply, the unit queue, Train and the build progress */
int kp_check_supply(KpGame *g, int64_t p, int64_t t, int feedback);
int64_t kp_free_supply(KpGame *g, int64_t t, int64_t p);
int kp_check_stock_of(KpGame *g, int64_t p, int64_t minerals, int64_t gas, int feedback);
int kp_check_stock(KpGame *g, int64_t p, int feedback);
int kp_check_unit_cost(KpGame *g, int64_t p, int64_t t, int supply, int feedback);
void kp_deduct_cost(KpGame *g, int64_t p);
void kp_refund_unit(KpGame *g, int64_t p, int64_t t);
void kp_refund_unit_partial(KpGame *g, int64_t p, int64_t t);
int kp_enqueue_unit(KpGame *g, int64_t n, int64_t t);
int64_t kp_count_queued_type(KpGame *g, int64_t n, int64_t t);
int64_t kp_type_race_index(KpGame *g, int64_t t);
void kp_cancel_construction(KpGame *g, int64_t n);
void kp_cancel_queued(KpGame *g, int64_t n, int64_t r);
void kp_cancel_last_queued(KpGame *g, int64_t n);
void kp_cancel_all_queued(KpGame *g, int64_t n);
void kp_set_rally_point(KpGame *g, int64_t n, int64_t x, int64_t y);
void kp_set_rally_unit(KpGame *g, int64_t n, int64_t target);
void kp_send_to_rally(KpGame *g, int64_t n, int64_t producer);
void kp_ready_voice(KpGame *g, int64_t n);
int kp_tick_build_timer(KpGame *g, int64_t n);
int kp_progress_construction(KpGame *g, int64_t n, int64_t gain, int place);
int64_t kp_create_queued(KpGame *g, int64_t n, int64_t t, int feedback);
int64_t kp_build_hp_gain(KpGame *g, int64_t n);
int64_t kp_build_shield_gain(KpGame *g, int64_t n);
void kp_secondary_train(KpGame *g, int64_t n);

/* kp_research.c: research and upgrades */
int kp_in_progress(KpGame *g, int64_t p, int64_t id, int upgrade);
void kp_order_research(KpGame *g, int64_t n);
void kp_order_upgrade(KpGame *g, int64_t n);
void kp_refund_research(KpGame *g, int64_t n, int full);
void kp_refund_upgrade(KpGame *g, int64_t n, int full);
void kp_command_research(KpGame *g, int64_t player, int64_t n, int64_t id, int upgrade);
void kp_command_cancel_research(KpGame *g, int64_t player, int64_t n, int upgrade);

/* kp_construction.c: the building placement validator and the Terran construction */
int64_t kp_validate_placement(KpGame *g, int64_t b, int64_t p, int64_t tx, int64_t ty, int64_t t, int vision, int unseen,
	int ignore);
/* The same with the codes of the footprint's cells (0x675188 + entry * 48: rows of six, at most eight rows; a rejection
 * of the whole footprint fills all 48 with its code). */
#define KP_PLACEMENT_CELLS 48
int64_t kp_validate_placement_cells(KpGame *g, int64_t b, int64_t p, int64_t tx, int64_t ty, int64_t t, int vision,
	int unseen, int ignore, uint8_t cells[KP_PLACEMENT_CELLS]);
void kp_order_place_building(KpGame *g, int64_t n);
void kp_order_constructing_building(KpGame *g, int64_t n);
void kp_order_build_terran(KpGame *g, int64_t n);
void kp_command_build(KpGame *g, int64_t player, int64_t order, int64_t tx, int64_t ty, int64_t t, int64_t n);
int64_t kp_find_attachable_addon(KpGame *g, int64_t b);
int kp_facing_waypoint(KpGame *g, int64_t n);
void kp_worker_error(KpGame *g, int64_t n, int64_t string);
void kp_cannot_land_voice(KpGame *g, int64_t n);
void kp_construction_voice(KpGame *g, int64_t n);
void kp_site_tile(KpGame *g, int64_t t, int64_t x, int64_t y, int64_t *tx, int64_t *ty);
int kp_pay_for_building(KpGame *g, int64_t n);
int64_t kp_convert_geyser(KpGame *g, int64_t n, int64_t t);
int kp_may_place(KpGame *g, int64_t order, int64_t t, int64_t player, int64_t n);
void kp_issue_placement(KpGame *g, int64_t order, int64_t n, int64_t x, int64_t y, int64_t t);
int64_t kp_geyser_at(KpGame *g, int64_t x, int64_t y);
int64_t kp_validate_grounded(KpGame *g, int64_t owner, int64_t tx, int64_t ty, int64_t t);
int64_t kp_validate_addon_placement(KpGame *g, int64_t b, int64_t p, int64_t tx, int64_t ty, int64_t t);
void kp_clamp_center(KpGame *g, int64_t t, int64_t *x, int64_t *y);
int kp_creep_under(KpGame *g, int64_t n);
void kp_attach_nearby_addon(KpGame *g, int64_t n);
void kp_snap_building(KpGame *g, int64_t t, int64_t *x, int64_t *y, int64_t *tx, int64_t *ty, int signed_edge);

/* kp_build_mode.c: the local player's building placement mode, the interface's state the game consults */
void kp_placement_start(KpGame *g, int64_t order, int64_t t);
void kp_placement_close(KpGame *g);
void kp_placement_cancel(KpGame *g);
void kp_placement_recheck(KpGame *g);
/* The mode's ghost images: while it is on (IsPlacementActive 0x47d070) RefreshPlacementGhosts 0x47d530 makes them again
 * and AddGhostToNewGeyser 0x47d8e0 puts one on a Geyser that came back; those of a unit go (0x47cd70,
 * RemovePlacementPreview 0x437050: a Geyser that becomes a gas building). */
void kp_placement_refresh_ghosts(KpGame *g);
void kp_placement_geyser_ghost(KpGame *g, int64_t geyser);
void kp_placement_remove_ghosts(KpGame *g, int64_t n);
/* CancelTargetingUi 0x423120 (the selection emptied) and CancelCursorModes 0x448c20: the placement closes, and the
 * interface hears it (KP_EVENT_CURSOR_MODES 1: the targeting is its own). */
void kp_cancel_cursor_modes(KpGame *g);

/* kp_lift.c: Terran add-ons and flying buildings */
void kp_cancel_incomplete_build_unit(KpGame *g, int64_t n);
void kp_command_place_addon(KpGame *g, int64_t player, int64_t order, int64_t tx, int64_t ty, int64_t t, int64_t n);
void kp_order_place_addon(KpGame *g, int64_t n);
void kp_secondary_build_addon(KpGame *g, int64_t n);
void kp_command_cancel_addon(KpGame *g, int64_t player, int64_t n);
void kp_command_lift_off(KpGame *g, int64_t player, int64_t n, int64_t x, int64_t y);
void kp_order_building_liftoff(KpGame *g, int64_t n);
void kp_order_lifting_off(KpGame *g, int64_t n);
void kp_order_building_land(KpGame *g, int64_t n);

/* kp_nuke.c: nuclear strikes */
void kp_silo_create_nuke(KpGame *g, int64_t silo);
/* SelectNukeTargetInRect 0x432f60 (kp_ai_spells.c). */
int64_t kp_ai_nuke_target_in_rect(KpGame *g, int64_t ghost, const int64_t rect[4]);
int64_t kp_find_ready_silo(KpGame *g, int64_t n, int check_link, int64_t requester);
void kp_order_nuke_train(KpGame *g, int64_t n);
void kp_order_nuke_paint(KpGame *g, int64_t n);
void kp_order_nuke_ground(KpGame *g, int64_t n);
void kp_order_nuke_unit(KpGame *g, int64_t n);
void kp_order_nuke_track(KpGame *g, int64_t n);
void kp_ghost_drop_dot(KpGame *g, int64_t n);
void kp_order_nuke_launch(KpGame *g, int64_t n);
void kp_command_cancel_nuke(KpGame *g, int64_t player);
/* A Nuclear Missile or Silo, whose links TeardownUnitTypeLinks 0x4253a0 undoes (kp_nuke_teardown). */
int kp_nuke_linked_type(int64_t t);
void kp_nuke_teardown(KpGame *g, int64_t n);

/* kp_repair.c */
void kp_order_repair(KpGame *g, int64_t n);

/* kp_protoss_build.c */
void kp_order_place_protoss_building(KpGame *g, int64_t n);
void kp_order_create_protoss_building(KpGame *g, int64_t n);
void kp_order_incomplete_warping(KpGame *g, int64_t n);
void kp_order_warp_in(KpGame *g, int64_t n);

/* kp_zerg_build.c */
void kp_order_drone_start_build(KpGame *g, int64_t n);
void kp_order_drone_land(KpGame *g, int64_t n);
void kp_order_drone_build(KpGame *g, int64_t n);
void kp_order_zerg_build_self(KpGame *g, int64_t n);
void kp_command_building_morph(KpGame *g, int64_t player, int64_t n, int64_t t);
void kp_order_zerg_building_morph(KpGame *g, int64_t n);
void kp_order_build_nydus_exit(KpGame *g, int64_t n);
void kp_cancel_zerg_building(KpGame *g, int64_t n);

/* kp_owner.c */
void kp_remove_unit_ai(KpGame *g, int64_t n, int64_t killed);
void kp_assign_unit_ai(KpGame *g, int64_t n);
int kp_player_enemy_of(KpGame *g, int64_t p, int64_t n);
void kp_set_sprite_owner(KpGame *g, int64_t n, int64_t owner);
void kp_change_owner(KpGame *g, int64_t n, int64_t owner, int score);
void kp_give_unit(KpGame *g, int64_t n, int64_t owner);
int kp_give_unit_checked(KpGame *g, int64_t n, int64_t to);
int kp_constructing_addon(KpGame *g, int64_t n);
int kp_building_unit(KpGame *g, int64_t n);
void kp_apply_upgrade_to_units(KpGame *g, int64_t owner, int64_t upgrade);

/* kp_remove.c */
void kp_clear_references(KpGame *g, int64_t n, int full);
void kp_remove_from_selections(KpGame *g, int64_t n);
void kp_remove_from_local_selection(KpGame *g, int64_t n);
void kp_leave_local_selection(KpGame *g, int64_t n);
int kp_local_selection_count(KpGame *g);
void kp_unit_hide(KpGame *g, int64_t n);
void kp_remove_body(KpGame *g, int64_t n);
void kp_unit_destroy(KpGame *g, int64_t n);
void kp_update_pending_units(KpGame *g);

/* kp_selection.c */
void kp_selection_init(KpGame *g);
void kp_command_control_group(KpGame *g, int64_t p, int64_t action, int64_t group);
void kp_command_select_remove(KpGame *g, int64_t p, const int64_t *refs, int count);
void kp_command_select_add(KpGame *g, int64_t p, const int64_t *refs, int count);
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
/* FindUnitOfTypeAtPoint 0x418210: the unit of a remembered type that stands at a point (0 for none). */
int64_t kp_fog_unit(KpGame *g, int64_t x, int64_t y, int64_t type);
/* OrderRightClickAction 0x431200 (order 133). */
void kp_order_right_click_action(KpGame *g, int64_t n);
void kp_run_turn(KpGame *g, const KpCommand *commands, int count);
/* RunGame 0x445310 before its first turn: the turn counter 0x672658 starts again at 0 (atlas P15). */
void kp_start_turns(KpGame *g);

/* kp_step.c */
void kp_step_init(KpGame *g);
void kp_set_context(KpGame *g, int64_t n);
void kp_update_units(KpGame *g);
void kp_step_objects(KpGame *g);
void kp_prime(KpGame *g);
void kp_step_frame(KpGame *g);
void kp_run_frame(KpGame *g, const KpCommand *commands, int count);

/* kp_triggers.c: the trigger engine. kp_triggers_init resets its state (InitTriggerState 0x4be240) and expands the
 * mission's TRIG records into the player lists (0x4bdb00); kp_trigger_step is the scheduler of a frame (0x4be500)
 * and kp_paused_tick a frame of a paused game (RunPendingFrames 0x444e30: the flashes, then the scheduler). */
void kp_triggers_init(KpGame *g, const KpMission *m, const KpCarry *carry);
void kp_triggers_free(KpGame *g);
void kp_trigger_step(KpGame *g);
void kp_paused_tick(KpGame *g, const KpCommand *commands, int count);
/* The Transmission's end (the portrait timer 0x4a7300 -> 0x4be3a0) and the Escape skip (0x4bda80) of the local
 * player, which the interface sends; kp_trigger_skippable tells whether a skip would end a trigger that still runs
 * (the interface's Escape does something else without one). */
void kp_trigger_release_wait(KpGame *g);
int kp_trigger_skippable(KpGame *g);
void kp_trigger_skip(KpGame *g);
/* The briefing's Set Mission Objectives (MBRF action 4, 0x44fdc0) before the game: the local player's objectives,
 * which carry into the mission. kp_briefing_lead is the string the briefing's first tick sets (Start pressed at once,
 * as in the emulator's runs), 0 for none. */
void kp_briefing_objectives(KpGame *g, int64_t string);
/* The local player's briefing list (0x5082f8): its count and copy k, which the VM's flags +0x940 and index +0x95f
 * change. */
int64_t kp_briefing_count(KpGame *g);
uint8_t *kp_briefing_record(KpGame *g, int64_t k);
int64_t kp_briefing_lead(KpGame *g);
/* kp_trigger_queries.c: the player selectors, the counters, the unit counts and the locations of the triggers. */
int kp_trigger_player_active(KpGame *g, int64_t p);
int kp_trigger_group(int64_t selector);
int kp_trigger_group_takes(KpGame *g, int64_t selector, int64_t p);
int kp_trigger_players(KpGame *g, int64_t selector, int64_t *out);
int64_t kp_trigger_owners(KpGame *g, int64_t selector);
int64_t kp_trigger_resources(KpGame *g, int64_t selector, int64_t type);
int64_t kp_trigger_score(KpGame *g, int64_t selector, int64_t type);
int64_t kp_trigger_deaths(KpGame *g, int64_t selector, int64_t unit);
int64_t kp_trigger_kills(KpGame *g, int64_t selector, int64_t unit);
void kp_trigger_set_resources(KpGame *g, int64_t selector, int64_t type, int64_t modifier, int64_t amount);
void kp_trigger_set_score(KpGame *g, int64_t selector, int64_t type, int64_t modifier, int64_t amount);
void kp_trigger_set_deaths(KpGame *g, int64_t selector, int64_t unit, int64_t modifier, int64_t amount);
void kp_location_center(KpGame *g, int64_t index, int64_t *x, int64_t *y);
int kp_location_excludes(KpGame *g, int64_t n, int64_t flags);
int kp_location_units(KpGame *g, int64_t index, int64_t *units, int64_t *flags);
int kp_trigger_type_matches(KpGame *g, int64_t t, int64_t want);
int kp_trigger_unit_matches(KpGame *g, int64_t n, int64_t owners, int64_t type, int64_t flags, int check);
int64_t kp_trigger_unit_at(KpGame *g, int64_t index, int64_t selector, int64_t type);
void kp_trigger_invalidate_counts(KpGame *g);
void kp_trigger_prepare_counts(KpGame *g, int64_t index);
int64_t kp_trigger_cached_count(KpGame *g, int64_t p, int64_t type, int completed);
int64_t kp_trigger_unit_count(KpGame *g, int64_t index, int64_t selector, int64_t type, int completed);
int kp_trigger_unit_ranking(KpGame *g, int64_t index, int64_t type, int most);
int kp_trigger_kills_ranking(KpGame *g, int64_t unit, int most);
int kp_trigger_score_ranking(KpGame *g, int64_t type, int most);
int kp_trigger_resources_ranking(KpGame *g, int64_t type, int most);
/* kp_trigger_units.c: the actions on units (the raw 32-byte action). */
void kp_trigger_create(KpGame *g, const uint8_t *a);
void kp_trigger_kill(KpGame *g, const uint8_t *a, int remove);
void kp_trigger_kill_at(KpGame *g, const uint8_t *a, int remove);
void kp_trigger_order(KpGame *g, const uint8_t *a);
void kp_trigger_move_units(KpGame *g, const uint8_t *a);
void kp_trigger_give(KpGame *g, const uint8_t *a);
void kp_trigger_invincibility(KpGame *g, const uint8_t *a);
void kp_trigger_modify(KpGame *g, const uint8_t *a);
void kp_trigger_doodad(KpGame *g, const uint8_t *a);
void kp_trigger_alliance(KpGame *g, const uint8_t *a);
/* GetMapString 0x445670: the mission string of an id (low 16 bits, 1-based), NULL for 0, "" past the count. */
const char *kp_map_string(KpGame *g, int64_t id);

/* kp_step.c: the AI step and the guard records */
void kp_ai_init(KpGame *g);
void kp_ai_step(KpGame *g);
void kp_guard_init(KpGame *g);
void kp_guard_attach(KpGame *g, int64_t n);
/* PrecreateGuard 0x4b22a0: a guard record of type t at (x, y) without its unit yet; AttachGuardToUnit 0x4b25b0: guard
 * k takes unit n at its original placement (kp_step.c). */
void kp_guard_precreate(KpGame *g, int64_t p, int64_t t, int64_t x, int64_t y);
void kp_guard_attach_unit(KpGame *g, int64_t k, int64_t n);

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
/* Keeps the first `kept` units of the topmost frame (a caller that filtered it in place). */
void kp_query_keep(KpGame *g, int64_t *frame, int kept);
/* The nearest unit to unit n (never n) that accept(g, candidate, arg) takes, within rect (left, top, right, bottom,
 * signed 16-bit) or within radius of n's sprite; 0 for none (0x442c20, 0x442bc0). */
typedef int (*KpUnitFilter)(KpGame *g, int64_t c, int64_t arg);
int64_t kp_nearest_unit_from(KpGame *g, int64_t n, const int64_t rect[4], KpUnitFilter accept, int64_t arg);
int64_t kp_nearest_unit_around(KpGame *g, int64_t n, int64_t radius, KpUnitFilter accept, int64_t arg);
/* The nearest to the point (x, y) within radius (0x442cc0); no unit is excluded. */
int64_t kp_nearest_unit_at(KpGame *g, int64_t x, int64_t y, int64_t radius, KpUnitFilter accept, int64_t arg);
/* The nearest to the point (x, y) within rect (0x442d40). */
int64_t kp_nearest_unit_point(KpGame *g, int64_t x, int64_t y, const int64_t rect[4], KpUnitFilter accept, int64_t arg);
/* The nearest over the whole map (the rect 0x543228). */
int64_t kp_nearest_unit_map(KpGame *g, int64_t n, KpUnitFilter accept, int64_t arg);
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
/* ForEachCargoUnit 0x437da0: the units of transport n's cargo slots below its space, in slot order; returns their
 * count (at most KP_CARGO_SLOTS). */
#define KP_CARGO_SLOTS 8
int kp_cargo_units(KpGame *g, int64_t n, int64_t *out);
int kp_has_cargo(KpGame *g, int64_t n);
/* GetUsedCargoSpace 0x438350 (kp_transport.c). */
int64_t kp_cargo_space(KpGame *g, int64_t n);
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
/* What SaveGameFile 0x498d20 changes in the game it saves (kp_save_effects.c): run by the save command (0x06). */
void kp_save_game_effects(KpGame *g);
void kp_power_show_fields(KpGame *g);
void kp_power_hide_fields(KpGame *g);
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
int64_t kp_route_distance(KpGame *g, int64_t n, int64_t x, int64_t y);
void kp_hangar_unlink(KpGame *g, int64_t c);
void kp_hangar_release(KpGame *g, int64_t n);
void kp_hangar_recall_outside(KpGame *g, int64_t n);
void kp_hangar_hallucinate(KpGame *g, int64_t h, int64_t original);
void kp_hangar_hallucinate_inside(KpGame *g, int64_t n);
void kp_order_carrier(KpGame *g, int64_t n);
void kp_order_carrier_attack(KpGame *g, int64_t n);
void kp_order_carrier_stop(KpGame *g, int64_t n);
void kp_order_reaver_stop(KpGame *g, int64_t n);
void kp_order_carrier_ignore(KpGame *g, int64_t n);
void kp_order_reaver(KpGame *g, int64_t n);
void kp_order_scarab_attack(KpGame *g, int64_t n);
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
int kp_point_unreachable(KpGame *g, int64_t n, int64_t x, int64_t y);
/* GetUnitsInRegion 0x4222d0 (kp_ai.c): the units standing in region r, a query frame as kp_query_rect's. */
int64_t *kp_region_units(KpGame *g, int64_t r, int *count);
int kp_reject_chase(KpGame *g, int64_t n, int64_t target);
int kp_target_out_of_range(KpGame *g, int64_t n, int64_t target);
int kp_target_inside_min_range(KpGame *g, int64_t n, int64_t target);
void kp_order_attack_unit(KpGame *g, int64_t n);
void kp_order_stay_in_range(KpGame *g, int64_t n);
void kp_order_attack_fixed_range(KpGame *g, int64_t n);
void kp_order_turret_attack(KpGame *g, int64_t n);
int kp_attack_in_place(KpGame *g, int64_t n);
void kp_set_target_switch(KpGame *g, int64_t n);
void kp_update_attack_target(KpGame *g, int64_t n);
int kp_try_attack_auto_target(KpGame *g, int64_t n);
void kp_apply_stim(KpGame *g, int64_t n);
int kp_within_range_moving(KpGame *g, int64_t n, int64_t range, int64_t target);
int kp_weapon_ready(KpGame *g, int64_t n, int64_t weapon);
void kp_spawn_weapon_bullet(KpGame *g, int64_t n, int64_t weapon);
void kp_use_weapon(KpGame *g, int64_t n, int64_t weapon);
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
void kp_convert_to_hallucination(KpGame *g, int64_t n);
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
/* The timed life (0x434a90) as unit n's remove timer, unless the timer already runs out sooner (0: none). */
void kp_cap_timed_life(KpGame *g, int64_t n);
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
void kp_create_dark_swarm(KpGame *g, int64_t x, int64_t y);
void kp_apply_matrix(KpGame *g, int64_t n);
void kp_acid_spore_area(KpGame *g, int64_t x, int64_t y, int64_t player);
void kp_reduce_matrix(KpGame *g, int64_t n, int64_t amount);
void kp_stasis_field(KpGame *g, int64_t x, int64_t y, int64_t source);
void kp_end_stasis(KpGame *g, int64_t n);
void kp_maelstrom(KpGame *g, int64_t x, int64_t y, int64_t source);
void kp_create_disruption_web(KpGame *g, int64_t x, int64_t y);
int kp_move_to_point_throttled(KpGame *g, int64_t n, int64_t x, int64_t y);
int kp_ai_guard_home(KpGame *g, int64_t n, int64_t *x, int64_t *y);
void kp_order_computer_return(KpGame *g, int64_t n);
void kp_ai_attack_helpless(KpGame *g, int64_t enemy, int64_t radius, int second_pass);
void kp_ai_try_cloak(KpGame *g, int64_t n);
int kp_ai_return_to_base(KpGame *g, int64_t n);
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
void kp_advance_hit_history(KpGame *g);
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
int64_t kp_region_all_neighbors(KpGame *g, int64_t r, int64_t *at);
void kp_ai_step_region(KpGame *g, int64_t p, int64_t r);
void kp_ai_rebuild_enemy_strength(KpGame *g);
int kp_ai_campaign(KpGame *g, int64_t p);
int kp_ai_guard_return_home(KpGame *g, int64_t n, int check_only);
int kp_ai_update_attack_target(KpGame *g, int64_t n, int issue, int allow_critters, int strict);
int kp_is_critter(KpGame *g, int64_t n);
int kp_ai_reaction_spell(KpGame *g, int64_t n, int reactive);
void kp_order_ai_guard(KpGame *g, int64_t n);
void kp_order_ai_patrol(KpGame *g, int64_t n);
void kp_ai_request_detection(KpGame *g, int64_t n, int64_t e);
void kp_order_computer_ai(KpGame *g, int64_t n);
void kp_ai_consider_stim(KpGame *g, int64_t n);
int kp_ai_transport_for_move(KpGame *g, int64_t n, int64_t x, int64_t y);
/* FindTransportForUnit 0x437f80 (kp_ai.c): *out the transport, 1 when one is fit to fly. */
int kp_ai_find_transport(KpGame *g, int64_t n, int64_t *out);
void kp_ai_set_region_state(KpGame *g, int64_t p, int64_t r, int64_t state);
/* AssignBuildingRegionState 0x4b00a0 (kp_ai.c) for player p's town unit n. */
void kp_ai_building_region_state(KpGame *g, int64_t p, int64_t n);
/* The towns (kp_ai_town.c; kp_game.h KpTowns): the pools' start (InitTownAiPools 0x486a20), a unit's assignment to
 * town k (AssignUnitToTown 0x484cd0), StartTown 0x487030 at (x, y) and StartAreaTown 0x486e80 over a rectangle (the
 * town, or -1), the race's main and gas building types (0x485ef0, 0x486100) and IsProducerBusy 0x4b14d0. */
void kp_towns_init(KpGame *g);
void kp_ai_town_assign(KpGame *g, int64_t k, int64_t n);
/* AssignNewUnitToActiveTown 0x484f40 (kp_ai_town.c): a computer's new unit of a town type joins its first town. */
void kp_ai_assign_to_first_town(KpGame *g, int64_t n);
int64_t kp_ai_start_town(KpGame *g, int64_t p, int64_t x, int64_t y, int64_t area);
int64_t kp_ai_start_area_town(KpGame *g, int64_t p, const int64_t *rect, int64_t area);
int64_t kp_ai_race_main_building(KpGame *g, int64_t p);
int64_t kp_ai_race_gas_building(KpGame *g, int64_t p);
int64_t kp_ai_race_worker(KpGame *g, int64_t p);
int kp_ai_producer_busy(KpGame *g, int64_t n);
/* The workers (kp_ai_town.c): RunWorkerAi 0x487580, ServiceTownConstruction 0x485400, GetUnitTownRepairScv
 * 0x487530. */
void kp_ai_run_worker(KpGame *g, int64_t w);
int kp_ai_town_construction(KpGame *g, int64_t k, int64_t w);
int64_t kp_ai_town_repair_scv(KpGame *g, int64_t n);
/* The towns' persistent requests (kp_ai_town.c): AddTownBuildRequest 0x487550, AddTownUpgradeRequest 0x4878b0,
 * AddTownTechRequest 0x487890. */
void kp_ai_town_build_request(KpGame *g, int64_t k, int64_t count, int64_t id, int64_t priority, int64_t special);
void kp_ai_town_upgrade_request(KpGame *g, int64_t k, int64_t level, int64_t id, int64_t priority);
void kp_ai_town_tech_request(KpGame *g, int64_t k, int64_t id, int64_t priority);
/* CountTownUnits 0x4872f0, TransferWorkersToTown 0x486480, RemoveUnitFromTown 0x486af0 (1 when a record was
 * released), ClearResourceReferences 0x486000 and ProcessTownRequests 0x4865b0 (kp_ai_town.c). */
int64_t kp_ai_count_town_units(KpGame *g, int64_t k, int64_t t, int completed);
void kp_ai_transfer_workers(KpGame *g, int64_t k, int64_t count);
int kp_ai_remove_from_town(KpGame *g, int64_t n);
void kp_ai_clear_resource_refs(KpGame *g, int64_t n);
void kp_ai_process_town_requests(KpGame *g, int64_t p);
/* The AI of a computer's new unit (kp_ai_town.c): AssignCompletedUnitAi 0x48d320 (from its producer, or itself),
 * AddBuildingToBuilderTown 0x486200 (builder's building) and RegisterFreeMedic 0x482520. */
void kp_ai_assign_completed(KpGame *g, int64_t producer, int64_t n);
void kp_ai_add_building(KpGame *g, int64_t builder, int64_t n);
void kp_ai_register_medic(KpGame *g, int64_t n);
/* GetTownGuardPosition 0x487ac0 (kp_ai_town.c): a guard's place at town k by the selector. */
int kp_ai_town_guard_position(KpGame *g, int64_t k, int64_t selector, int64_t *x, int64_t *y);
/* A computer Drone's town record as it becomes a building (0x486ba0) or one again (0x487900; kp_ai_town.c). */
void kp_ai_drone_town_change(KpGame *g, int64_t n, int to_building);
/* ClearTownRepairScv 0x484f10 (kp_ai_town.c). */
void kp_ai_clear_repair_scv(KpGame *g, int64_t n);
/* MarkUnitTownAttacked 0x487b80 (kp_ai_town.c). */
void kp_ai_town_attacked(KpGame *g, int64_t n);
/* The player's Medic goes (0x483220; kp_ai_town.c). */
void kp_ai_release_medic(KpGame *g, int64_t n);
/* MaintainNextPlayerTowns 0x486c80 for player p (kp_ai_town.c). */
void kp_ai_maintain_towns(KpGame *g, int64_t p);
/* The resource areas (kp_ai_areas.c; kp_game.h KpAreas): GenerateResourceAreas 0x497a20 (FinishAiMapSetup 0x48dd80
 * after the map's units), RefreshResourceAreas 0x496c80 (StepAi), a resource's area (its byte +0xd8) and the route
 * distance 0x496930 in tiles from (sx, sy) to (dx, dy) (9999 unreachable). */
void kp_ai_areas_generate(KpGame *g);
void kp_ai_areas_refresh(KpGame *g);
int64_t kp_ai_resource_area(KpGame *g, int64_t n);
int64_t kp_ai_route_distance(KpGame *g, int64_t sx, int64_t sy, int64_t dx, int64_t dy);
/* The AI's building placement (kp_ai_placement.c): FindBuildingPlacement 0x494fc0 for builder with the window size
 * around (sx, sy), FindBuildingPlacementFrom 0x4962c0 (throttled) and its wrapper 0x496240 from the builder; the
 * position (*ox, *oy) found, 0 for none. */
int kp_ai_find_placement(KpGame *g, int64_t builder, int64_t id, int64_t sx, int64_t sy, int64_t size, int64_t *ox, int64_t *oy);
int kp_ai_place_from(KpGame *g, int64_t builder, int64_t id, int64_t sx, int64_t sy, int throttle, int64_t size,
	int64_t *ox, int64_t *oy);
int kp_ai_place(KpGame *g, int64_t builder, int64_t id, int throttle, int64_t *ox, int64_t *oy);
/* ProduceDefenseAnchors 0x496e10 (kp_ai_placement.c): the regions cutting the routes from region src to player p's
 * nearest enemies (at most limit cuts each). */
void kp_ai_defense_anchors(KpGame *g, int64_t p, int64_t src, int64_t limit);
/* The areas' lookups (kp_ai_areas.c): the mineral field count 0x496f20 and geyser count 0x496f40, the reserved flag's
 * release 0x497380, FindNearestArea 0x496bf0 (0 none), FindFirstMineralInArea 0x4964b0, FindAreaGasObjects 0x496510
 * and the worker limit 0x496690 (with_gas) / 0x4966f0 of player p. */
int64_t kp_ai_area_minerals(KpGame *g, int64_t area);
int64_t kp_ai_area_gas(KpGame *g, int64_t area);
void kp_ai_area_release(KpGame *g, int64_t area);
int64_t kp_ai_nearest_area(KpGame *g, int64_t p, int64_t x, int64_t y);
/* TryReserveArea 0x497360 and ChooseExpansionArea 0x4970e0 (kp_ai_areas.c; 0 none). */
int kp_ai_area_reserve(KpGame *g, int64_t area);
/* IsRouteInteriorSafe 0x496ac0 (kp_ai_areas.c). */
int kp_ai_route_interior_safe(KpGame *g, int64_t p, int64_t src, int64_t dst);
int64_t kp_ai_choose_expansion(KpGame *g, int64_t p, int64_t home, int64_t mode);
/* SpawnAreaTownScript 0x494e80 (kp_aiscript.c). */
void kp_aiscript_spawn_area_town(KpGame *g, int64_t p, int bw, int64_t area, int64_t pc);
int64_t kp_ai_area_mineral(KpGame *g, int64_t area);
void kp_ai_area_gas_objects(KpGame *g, int64_t area, int64_t out[3]);
int64_t kp_ai_area_worker_limit(KpGame *g, int64_t area, int64_t p, int with_gas);
/* GetHatcherySeedForArea 0x496f50 (kp_ai_areas.c). */
void kp_ai_hatchery_seed(KpGame *g, int64_t area, int64_t *x, int64_t *y);
/* KillTownScripts 0x493860 (kp_aiscript.c): the scripts of town k stop. */
void kp_aiscript_kill_town(KpGame *g, int64_t k);
/* IsUnitRegionUnthreatened 0x4aeaa0 (kp_ai.c). */
int kp_ai_unit_region_unthreatened(KpGame *g, int64_t n);
/* FindNearestFreeMineralField 0x431fe0 (kp_harvest.c). */
int64_t kp_nearest_free_mineral(KpGame *g, int64_t w, int64_t x, int64_t y, int64_t flag);
/* The spending (kp_ai_spending.c): IsUnitLimitReached 0x4835e0, the top request's kind 0x483550, RunBuildingAi
 * 0x487110, RefreshAiBudget 0x4823d0, ConvertTownRequest 0x482b60 and the training metadata of a producer's queue
 * (0x484f70 clear, 0x484f90 copy). */
int kp_ai_unit_limit_reached(KpGame *g, int64_t p, int64_t t);
int64_t kp_ai_top_kind(KpGame *g, int64_t p);
int64_t kp_ai_top_id(KpGame *g, int64_t p);
int64_t kp_ai_top_context(KpGame *g, int64_t p);
void kp_ai_complete_top(KpGame *g, int64_t p);
void kp_ai_drop_top(KpGame *g, int64_t p);
/* CanAiAffordUnit 0x4826d0 and IsProducerRaceCompatible 0x4860a0 (error 25). */
int kp_ai_afford_unit(KpGame *g, int64_t p, int64_t t);
int kp_ai_race_compatible(KpGame *g, int64_t n, int64_t t);
int kp_ai_run_building(KpGame *g, int64_t n);
void kp_ai_refresh_budget(KpGame *g, int64_t p);
void kp_ai_convert_town_request(KpGame *g, int64_t p, int64_t town, int64_t kind, int64_t id, int64_t priority);
/* RequestTownUnit 0x482ae0 and GetGatherSplitPercent 0x483030. */
void kp_ai_request_town_unit(KpGame *g, int64_t p, int64_t town, int64_t t, int64_t priority);
/* RequestUnitProduction 0x482990 (context an AI context) and PushKind8UnitRequest 0x482930. */
void kp_ai_request_unit(KpGame *g, int64_t p, int64_t context, int64_t t, int64_t priority, int keep, int bypass);
void kp_ai_request_military_unit(KpGame *g, int64_t p, int64_t t, int64_t priority);
/* RequestGuardReplacement 0x482a40 for guard k, RunAiSpendingPass 0x483400's pass for player p. */
void kp_ai_request_guard(KpGame *g, int64_t p, int64_t k);
void kp_ai_spending_pass(KpGame *g, int64_t p);
/* RebuildGuards 0x4b2320 (kp_step.c). */
void kp_guard_rebuild(KpGame *g, int64_t p);
/* The military records (kp_ai_military.c): the pool's start (InitMilitaryPool 0x4a9a90), AddMilitaryAi 0x4ae760 (force
 * keeps an abandoned region), the release 0x4a9a30 of record k, MilitaryArrive 0x4a9b20, RequestComputerOrder
 * 0x4ada90, IsMobileMilitaryAiUnit 0x4b1a30, IsMilitaryCasterType 0x4abc90, the first record of a region's list (-1
 * none; next through KpMilitaryAis.next), a unit's record (-1 none, with its player and region),
 * RunRegionalMilitaryProduction 0x4af930 and FindSpareMilitaryUnit 0x4af750 (0 none). */
void kp_ai_military_init(KpGame *g);
void kp_ai_military_add(KpGame *g, int64_t p, int64_t r, int64_t n, int force);
void kp_ai_military_release(KpGame *g, int64_t k);
void kp_ai_military_arrive(KpGame *g, int64_t n, int64_t order, int64_t x, int64_t y);
void kp_ai_request_order(KpGame *g, int64_t n, int64_t order);
int kp_ai_mobile_military(KpGame *g, int64_t n);
int kp_ai_military_caster(int64_t t);
int64_t kp_ai_military_first(KpGame *g, int64_t p, int64_t r);
int64_t kp_ai_military_record(KpGame *g, int64_t n, int64_t *p, int64_t *r);
/* The attacks (kp_ai_military.c): PrepareAttack 0x4af300 (0 while a grouping waits), StartAttackGrouping 0x483ad0
 * and HasActiveAttackRegion 0x4ab5d0. */
int kp_ai_prepare_attack(KpGame *g, int64_t p, int64_t x, int64_t y, int force, int retry);
int64_t kp_ai_roster_flags(KpGame *g, int64_t entry); /* the flags 0x4af300 reads for a roster entry, a lost one's too */
void kp_ai_start_grouping(KpGame *g, int64_t p, int64_t left);
/* SendSuicide 0x4b11b0 (kp_ai_military.c) and ClearAttackRoster 0x483770 (kp_aiscript.c). */
void kp_ai_send_suicide(KpGame *g, int64_t p, int mode);
/* EvaluateGlobalAttackOpportunity 0x4acdf0 (kp_ai_military.c). */
void kp_ai_evaluate_attacks(KpGame *g);
void kp_ai_attack_clear(KpGame *g, int64_t p, int timestamp);
int kp_ai_attack_active(KpGame *g, int64_t p);
/* A group's moves (kp_ai_military.c): GetGroupSlowestUnit 0x4afaf0, FollowerIsReady 0x4b09c0, LeaderShouldWait
 * 0x4aff20, GroupHasPendingPickup 0x4afec0 and RejoinAssignedRegion 0x4b0e10. */
int64_t kp_ai_group_slowest(KpGame *g, int64_t n);
/* UpdateSlowestMember 0x4ab870 (kp_ai_military.c). */
void kp_ai_update_slowest(KpGame *g, int64_t p, int64_t r);
int kp_ai_follower_ready(KpGame *g, int64_t n);
int kp_ai_leader_should_wait(KpGame *g, int64_t n);
int kp_ai_group_pending_pickup(KpGame *g, int64_t n);
void kp_ai_rejoin_region(KpGame *g, int64_t n);
/* The gathering of an attack (kp_ai_military.c): transports 0x4aa8a0, GetGroupWeaponSummary 0x4aaef0,
 * ShouldRetargetRegion 0x4ab490, ChooseGlobalAttackTarget 0x4aba70, TransferRegionMilitary 0x4b15d0,
 * DisperseRegionMilitary 0x4b1500, CheckAttackGroupingTimeout 0x483940, HasStragglingAttacker 0x4ab150 and a missing
 * roster unit (0x4aa500). */
int kp_ai_has_transports(KpGame *g, int64_t p);
/* RecruitForRegion 0x4ae960 (0 without a donor) and IsInGroundDefenseUseList 0x4824b0 (kp_ai_military.c); a
 * recruitment at KP_AI_MANNED_CREW_PRIORITY (the crew of a campaign AI's Bunker that already holds someone) takes no
 * donor from a building. */
#define KP_AI_MANNED_CREW_PRIORITY 20
int kp_ai_recruit(KpGame *g, int64_t p, int64_t r, int64_t t, int64_t threshold);
int kp_ai_defense_use_has(KpGame *g, int64_t p, int64_t t);
void kp_ai_weapon_summary(KpGame *g, int64_t p, int64_t r, int *has_ground, int *no_ground, int *no_air);
int kp_ai_should_retarget(KpGame *g, int64_t p, int64_t src, int64_t dst);
int64_t kp_ai_global_target(KpGame *g, int64_t p, int64_t r);
void kp_ai_transfer_military(KpGame *g, int64_t p, int64_t src, int64_t dst, int recruit_guards, int casters,
	int restrict_loaded);
void kp_ai_disperse_military(KpGame *g, int64_t p, int64_t r);
int kp_ai_grouping_timeout(KpGame *g, int64_t p);
int kp_ai_straggling_attacker(KpGame *g, int64_t p);
int kp_ai_roster_missing(KpGame *g, int64_t p, int64_t r);
/* MilitaryAiStep 0x4b1010 and IsInActiveAttackRegion 0x4b0580 (kp_ai_military.c). */
void kp_ai_military_step(KpGame *g, int64_t n);
int kp_ai_in_attack_region(KpGame *g, int64_t n, int exclude_staging);
void kp_ai_military_production(KpGame *g, int64_t p);
int64_t kp_ai_spare_military(KpGame *g, int64_t p, int64_t t, int64_t x, int64_t y);
/* kp_ai.c: UpdateRegionStrength 0x4adbd0, IsRegionReachableForMilitary 0x4ab330, a region's tile count. */
void kp_ai_update_region_strength(KpGame *g, int64_t p, int64_t r);
int kp_ai_military_reachable(KpGame *g, int64_t p, int64_t source, int64_t target);
int64_t kp_region_tiles(KpGame *g, int64_t r);
/* ChooseUnloadDestination 0x4af810 (kp_ai.c). */
void kp_ai_choose_unload(KpGame *g, int64_t n);
/* MarkDefenseRegion 0x4b0030 (kp_ai.c). */
void kp_ai_mark_defense_region(KpGame *g, int64_t p, int64_t r, int strong);
/* CaptainExpand 0x4ad310 (capt_expand; kp_ai.c). */
void kp_ai_captain_expand(KpGame *g, int64_t p);
void kp_ai_gather_split(KpGame *g, int64_t p, int64_t *mineral_percent, int64_t *gas_percent);
void kp_ai_training_clear(KpGame *g, int64_t n, int64_t slot);
void kp_ai_training_copy(KpGame *g, int64_t n, int64_t to, int64_t from);
/* FreeParentlessGuards 0x4b21f0 (kp_step.c). */
void kp_guard_free_parentless(KpGame *g, int64_t p);
/* HasAdmissibleGuardVacancy 0x4b25f0 (a parentless guard of type t whose placement region is unthreatened) and
 * ResetGuardRetryForType 0x4b2750 (kp_step.c). */
int kp_guard_vacancy(KpGame *g, int64_t p, int64_t t);
void kp_guard_reset_retry(KpGame *g, int64_t p, int64_t t);
/* IsRegionUnthreatened 0x4aea70 (kp_ai.c): not abandoned (state 3), without targets or flag 0x20. */
int kp_ai_region_unthreatened(KpGame *g, int64_t p, int64_t r);
/* IsThreatenedBy 0x41fd90 (kp_ai.c): other, or n's target when 0, can attack n now. */
int kp_ai_threatened_by(KpGame *g, int64_t n, int64_t o);
/* HasAttackableEnemyNearby 0x4317a0 (kp_attack.c). */
int kp_enemy_in_reach(KpGame *g, int64_t n);
/* IsValidResourceDepot 0x424460 (kp_harvest.c). */
int kp_valid_depot(KpGame *g, int64_t n);
/* ValidateOrderTarget 0x429870 for Repair 34 (kp_commands.c): a completed mechanical Terran unit below its full hit
 * points. */
int kp_repairable(KpGame *g, int64_t target);
/* GetRegionCenter 0x481a30: region r's centre in pixels. */
void kp_ai_region_center(KpGame *g, int64_t r, int64_t *x, int64_t *y);

/* The AIScript VM (kp_aiscript.c; kp_game.h KpScripts): the pool's start (InitAiScriptPool 0x493330), the scheduler's
 * pass of a step (RunAiScripts 0x4938e0) and the launch of script id (its four bytes little-endian) for player p
 * with the rectangle {left, top, right, bottom} or none (LaunchAiScript 0x493970; nothing when the gate refuses it,
 * the binaries lack it or the pool is full). */
void kp_aiscript_init(KpGame *g);
void kp_aiscript_run(KpGame *g);
void kp_aiscript_launch(KpGame *g, uint32_t id, int64_t p, const int64_t *rect);
/* The pieces the original-code probes call on their own (tests/c/test_aiscript.c): CreateAiScript 0x494d90 (the
 * slot or -1), FreeAiScript 0x493240 and ExecuteAiScript 0x493a60 of slot s, and the binaries' override of the tests:
 * aiscript.bin's and bwscript.bin's bytes (copied) for the game's, NULL restoring them; -1 when out of memory. */
int64_t kp_aiscript_create(KpGame *g, int64_t player, int64_t pc, const int64_t *rect, int bin);
void kp_aiscript_free(KpGame *g, int64_t s);
void kp_aiscript_execute(KpGame *g, int64_t s);
int kp_aiscript_program(KpGame *g, const uint8_t *ai, size_t ai_size, const uint8_t *bw, size_t bw_size);
/* FindScriptSpellcaster 0x4932a0: the first of player p's units of type t with at least `minimum` whole energy idle
 * under the AI (Guard, ComputerAI, AIGuard), or 0. */
int64_t kp_ai_script_caster(KpGame *g, int64_t p, int64_t t, int64_t minimum);
void kp_ai_react_to_attack(KpGame *g, int64_t n, int64_t a, int direct, int propagated);
int kp_ai_spell_cast_order(int64_t order);
int64_t kp_ai_unit_record_type(KpGame *g, int64_t n);
void kp_ai_remove_from_regions(KpGame *g, int64_t n, int64_t killed);
void kp_ai_detach_guard(KpGame *g, int64_t n);

/* kp_counts.c */
void kp_counts_init(KpGame *g);
void kp_account_live(KpGame *g, int64_t n, int64_t delta);
int64_t kp_supply_max(KpGame *g, int64_t t, int64_t p);
int64_t kp_supply_used(KpGame *g, int64_t race, int64_t p);
int64_t kp_supply_provided_capped(KpGame *g, int64_t race, int64_t p);
int64_t kp_supply_free(KpGame *g, int64_t t, int64_t p, int cheat);
void kp_account_completed(KpGame *g, int64_t n, int64_t delta, int score);
void kp_unscore_produced(KpGame *g, int64_t p, int64_t t);

#ifdef __cplusplus
}
#endif

#endif
