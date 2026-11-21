/* The start of a native game (stage N.6): the UMS override effects (kp_rules.c) and the game start (kp_start.c). */
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

/* Every value is KP_RULE_UNSET until a section sets it. */
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
	/* the music's rotation 0x503354 plus one (0: -1, the process's games before the first custom game's draw), which
	 * LoadConsoleSkinAndPickMusic 0x443680 advances in each custom game (KpGame.music_rotation) */
	int64_t music_rotation;
	/* the game RNG's state 0x67393c as the game left it (a first game's 0): a Use Map Settings lobby whose local slot is
	 * User Select resolves its random races from it before the next game's seed (SetLocalRaceAndResolve 0x479c90) */
	int64_t rng_state;
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

/* All override sections of the mission in order over dat's units table (subunit1, ground_weapon, air_weapon). -1
 * with the error text in err. */
int kp_rules_effects(KpRuleEffects *e, const KpMission *m, const KpDat *dat, int expansion, KpError *err);

/* A game's setup (ADR 0024): what the original's game creation leaves for StartGame 0x443f40. The template record
 * 0x67293d (the game header 0x6728d0 + 0x6d; a .got file's bytes 65..96, a replay header's +0x81), the multiplayer
 * byte 0x672664, and per slot the player record 0x6726c8's Storm ID, type (KP_CTRL_*: a lobby's 5, 6 and 8 too), race,
 * force and name.
 *
 * `started` says whose records they are. 0: the lobby's, as StartGame's step 6 finds them (after the single-player
 * custom races 0x443b90, kp_setup_custom_races): the start resolves the random races, the slot types, the random
 * starts and the colours (kp_gametype.c) and keeps the started game's records in the game's copy (g->setup, started 1).
 * 1: a started game's (a replay's header, a save's MAPC, a campaign's from its map): the start leaves them. */
#define KP_TEMPLATE_SIZE 32
#define KP_TEMPLATE_TYPE 0x00          /* the game type (WORD) */
#define KP_TEMPLATE_SUBTYPE 0x02       /* WORD */
#define KP_TEMPLATE_VICTORY 0x08       /* 0x672945 */
#define KP_TEMPLATE_RESOURCES 0x09     /* 0x672946 */
#define KP_TEMPLATE_STARTING_UNITS 0x0c /* 0x672949 */
#define KP_TEMPLATE_FIXED_POSITIONS 0x0d /* 0x67294a */
#define KP_TEMPLATE_RESTRICTION 0x0e   /* 0x67294b */
#define KP_TEMPLATE_ALLIES 0x0f        /* 0x67294c */
#define KP_TEMPLATE_TEAMS 0x10         /* 0x67294d */
#define KP_TEMPLATE_CHEATS 0x11        /* 0x67294e */
#define KP_TEMPLATE_TOURNAMENT 0x12    /* 0x67294f */
#define KP_TEMPLATE_VALUE 0x04         /* 0x672941: the subtype's display value (Greed's resources, Slaughter's minutes) */
#define KP_TEMPLATE_RESOURCE_VALUE 0x17 /* 0x672954: the starting resources (DWORD) */
#define KP_SETUP_SLOTS 12
#define KP_PLAYER_NAME 25 /* a player record's name field */
#define KP_SETUP_NAME 32  /* a slot's name kept: the field's bytes, NUL padded */
typedef struct {
	uint8_t template_record[KP_TEMPLATE_SIZE];
	int64_t multiplayer;
	int64_t slots; /* the map's player slots the lobby offered (game header +0x25): a campaign's 1 */
	int64_t started;
	int64_t storm[KP_SETUP_SLOTS], type[KP_SETUP_SLOTS], race[KP_SETUP_SLOTS], force[KP_SETUP_SLOTS];
	char name[KP_SETUP_SLOTS][KP_SETUP_NAME];
	int64_t user_select[8]; /* 0x673f08: the slots whose map race was User Select (LoadMapHeader 0x46b5c0; a replay
	                         * header's +0x271) */
	int64_t color[8];       /* 0x5264fc: the colour row each player draws with; the start's (0x41d570, shuffled by
	                         * 0x41d460), a started setup's as it is */
	int64_t race_summary[8]; /* 0x5264a0: the races of slots 0..7 in the lobby's order after the random races
	                          * (RecordSlotSummary 0x47e290), before a team game moves its records: its starting units,
	                          * supply maxima and the seat's race read them */
	int64_t leader[5];       /* 0x5264ab: a team game's leader of force 1..4, the slot the team plays as
	                          * (SetupTeamGameSlots 0x4b2db0; KP_NO_LEADER none, [0] unused) */
} KpGameSetup;
#define KP_NO_LEADER 0xff
/* The Use Map Settings template a single-player campaign game runs under (CreateSinglePlayerGame 0x443970; every 1.17
 * campaign replay's header holds it): type 0x0a, subtype 1, fog 2, fixed positions 1, restrictions 3, allies 1,
 * cheats 1. */
extern const uint8_t KP_TEMPLATE_UMS[KP_TEMPLATE_SIZE];
/* IsUseMapSettings 0x447db0 of a template record: victory, starting units and tournament all 0. */
int kp_template_ums(const uint8_t *template_record);
/* The melee UNIT filter 0x469600 of a template record whose starting units byte is set: 1 when the record of type t
 * for owner is left out (the neutral player's resources and critters stay); 0 for every record otherwise. */
int kp_template_unit_excluded(const uint8_t *template_record, int64_t t, int64_t owner);

/* The inputs of a game start. The data must outlive the game; dat is the game's own copy (the UMS overrides
 * and the acquisition ranges change it), rng and crt are seeded with the game's initial seed. */
typedef struct {
	KpDat *dat;
	const KpGrpMeta *grp;
	const KpIScript *script;
	const KpAiBins *ai_bins; /* the AI script binaries the AIScript VM runs */
	const KpMission *mission;
	const uint8_t *cv5, *vf4; /* the tileset's original CV5 and VF4 (ADR 0018, stage D5), its megatile flags counted */
	size_t cv5_size, vf4_size;
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
	int replay;            /* a replay's playback: the replay flag 0x67cc90 and full vision (StartGame 0x443f40 step
	                        * 13: 0x67cc84 = 0x672668 = 0xff, 0x67cc88 = 0) */
	const KpGameSetup *setup; /* a custom map's game (ADR 0024); NULL: a campaign mission's (KP_TEMPLATE_UMS, the
	                           * slots of its CHK's owners) */
	const struct KpVictoryFiles *victory; /* the victory trigger files (kp_template.h) a game type other than Use Map
	                                       * Settings loads (SetupVictoryConditions 0x448530); NULL: none in the data */
	int64_t local_storm; /* the Storm ID of the local human (0x508d58) among a lobby setup's records: a single-player
	                      * game's 0 */
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
