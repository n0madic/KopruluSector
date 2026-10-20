/* The start of a native game (stage N.6): the UMS override effects (kp_rules.c; game/data/rules.lua) and the
 * game start (kp_start.c; game/data/unit_records.lua, game/data/terrain.lua). */
#ifndef KP_START_H
#define KP_START_H

#include <stdint.h>

#include "kp_aiscript.h"
#include "kp_dat.h"
#include "kp_mission.h"
#include "kp_grpmeta.h"
#include "kp_iscript.h"
#include "kp_rng.h"

/* The game speed (the speed byte 0x6d3130, an index of the frame lengths 0x503360) a game starts with unless its
 * caller sets one: the original's with no saved setting (Fast), as the emulator runs it. */
#define KP_DEFAULT_SPEED 4

#ifdef __cplusplus
extern "C" {
#endif

#define KP_RULE_UNITS 228
#define KP_RULE_WEAPONS 130
#define KP_RULE_PLAYERS 12
#define KP_RULE_UNIT_FIELDS 7 /* health, shields, armor, build_time, mineral_cost, gas_cost, name */
#define KP_RULE_UPGRADES 61
#define KP_RULE_TECHS 44
#define KP_RULE_UPGRADE_FIELDS 6
#define KP_RULE_TECH_FIELDS 4

/* rules.lua M.new: every value KP_RULE_UNSET until a section sets it (nil in Lua). */
#define KP_RULE_UNSET INT64_MIN

/* What a new game takes from the game before it in the same process (plan phase 8, docs/phase8.md): the globals the
 * original's per-game initialization leaves as they were (InitTriggerState 0x4be240 resets neither the pending
 * results 0x6ca758, the trigger pass's player 0x6ca754, the result dialog's delay 0x6ca74c, the objectives 0x5349a4
 * nor the leaderboard label 0x5349f4, nor anything the unit-count cache of the trigger queries 0x6ca6d0..
 * holds until its next refresh; InitAiRegions 0x4b0640 leaves the sources of the AI's reach cache 0x6b68e0). A first
 * game takes zeros (the original's fresh data section). */
#define KP_CARRY_PLAYERS 8
#define KP_CARRY_COUNTS (228 * 12)
#define KP_CARRY_PSTATS 28
typedef struct {
	int64_t pending[KP_CARRY_PLAYERS], objectives[KP_CARRY_PLAYERS];
	int64_t player, result_delay, lb_label;
	int64_t count_key, count_location;
	int64_t count_all[KP_CARRY_COUNTS], count_completed[KP_CARRY_COUNTS];
	int64_t count_pstat[KP_CARRY_PSTATS][12];
	int64_t ai_reach_source[KP_CARRY_PLAYERS]; /* 0x6b68e0, which InitAiRegions 0x4b0640 leaves */
} KpCarry;
typedef struct {
	int64_t units[KP_RULE_UNIT_FIELDS][KP_RULE_UNITS];
	int64_t weapon_damage[KP_RULE_WEAPONS], weapon_bonus[KP_RULE_WEAPONS];
	int64_t upgrades[KP_RULE_UPGRADE_FIELDS][KP_RULE_UPGRADES];
	int64_t techs[KP_RULE_TECH_FIELDS][KP_RULE_TECHS];
	int64_t availability[KP_RULE_PLAYERS][KP_RULE_UNITS];
	int64_t upgrade_levels[2][KP_RULE_PLAYERS][KP_RULE_UPGRADES]; /* [0] max, [1] start */
	int64_t tech_states[2][KP_RULE_PLAYERS][KP_RULE_TECHS];       /* [0] available, [1] researched */
} KpRuleEffects;

/* rules.lua M.effects: all override sections of the mission in order over dat's units table (subunit1,
 * ground_weapon, air_weapon). -1 with the Lua error text in err. */
int kp_rules_effects(KpRuleEffects *e, const KpMission *m, const KpDat *dat, int expansion, KpError *err);

/* The inputs of a game start. The data must outlive the game; dat is the game's own copy (the UMS overrides
 * and the acquisition ranges change it), rng and crt are seeded with the game's initial seed. */
typedef struct {
	KpDat *dat;
	const KpGrpMeta *grp;
	const KpIScript *script;
	const KpAiBins *ai_bins; /* the AI script binaries the AIScript VM runs */
	const KpMission *mission;
	const uint8_t *tileset; /* tilesets/<name>.kpd (kind TSET) */
	size_t tileset_size;
	KpRngGame *rng;
	KpRngCrt *crt;
	int expansion;
	int no_units; /* tests: the mission without its preplaced units (UNIT section) */
	int no_events; /* a game without a presentation: its event ring is off (kp_game.h KpEvents) */
	int trace_triggers; /* the oracle's runs: the ring also records the trigger and AI traces (KP_EVENT_TRIGGER, KP_EVENT_AI) */
	int64_t speed;         /* the game speed byte 0x6d3130 (KP_DEFAULT_SPEED from kp_game_start_input) */
	int64_t mission_index; /* the campaign row 0x526524 (mapdata.dat; 0 a custom map) */
	int64_t cheats;        /* the cheat word 0x5432fc of the game before (a new game keeps KP_CHEATS_CARRIED) */
	const KpCarry *carry;  /* the game before's carried globals (kp_carry_take); NULL: the process's first game */
	int64_t turn;          /* the turn counter 0x672658 before RunGame resets it (kp_start_turns): what the menus
	                        * left, 0 in the process's first game */
} KpStartInput;

struct KpGame;
/* The start of a game: the game at its first object step, owning its images world (kp_game_free releases it), or
 * NULL with the error in err. */
struct KpGame *kp_start(const KpStartInput *in, KpError *err);
/* The globals a game leaves for the next one (KpCarry), at its end. */
void kp_carry_take(const struct KpGame *g, KpCarry *c);

#ifdef __cplusplus
}
#endif

#endif
