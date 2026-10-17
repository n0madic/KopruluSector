/* The start of a native game (stage N.6): the UMS override effects (kp_rules.c; game/data/rules.lua) and the
 * game start (kp_start.c; game/data/unit_records.lua, game/data/terrain.lua). */
#ifndef KP_START_H
#define KP_START_H

#include <stdint.h>

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
	const KpMission *mission;
	const uint8_t *tileset; /* tilesets/<name>.kpd (kind TSET) */
	size_t tileset_size;
	KpRngGame *rng;
	KpRngCrt *crt;
	int expansion;
	int no_units; /* tests: the mission without its preplaced units (UNIT section) */
	int no_events; /* a game without a presentation: its event ring is off (kp_game.h KpEvents) */
	int trace_triggers; /* the oracle's runs: the ring also records the trigger trace (KP_EVENT_TRIGGER) */
	int64_t speed;         /* the game speed byte 0x6d3130 (KP_DEFAULT_SPEED from kp_game_start_input) */
	int64_t mission_index; /* the campaign row 0x526524 (mapdata.dat; 0 a custom map) */
} KpStartInput;

struct KpGame;
/* The start of a game: the game at its first object step, owning its images world (kp_game_free releases it), or
 * NULL with the error in err. */
struct KpGame *kp_start(const KpStartInput *in, KpError *err);

#ifdef __cplusplus
}
#endif

#endif
