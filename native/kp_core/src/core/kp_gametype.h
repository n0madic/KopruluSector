/*
 * The game types' start and victory (phase M stage M2, ADR 0024): what StartGame 0x443f40 does with the game's setup
 * (KpGameSetup, kp_start.h) beyond a campaign mission's. The template's predicates (gametype.cpp, atlas P15), the
 * slots' steps (net_record.cpp, P27: the random races, the slot types, the random starts, the colours), the starting
 * resources, units and alliances, and the victory trigger files with Capture the Flag's beacons.
 */
#ifndef KP_GAMETYPE_H
#define KP_GAMETYPE_H

#include <stdint.h>

#include "kp_game.h"
#include "kp_start.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KP_GAME_TYPE_TOP_VS_BOTTOM 0x0f
#define KP_VICTORY_MELEE 1
#define KP_VICTORY_GREED 3
#define KP_VICTORY_CTF 4
#define KP_VICTORY_SUDDEN_DEATH 5
#define KP_VICTORY_SLAUGHTER 6
#define KP_VICTORY_MELEE_TRIGGERS 7 /* loads the melee triggers too (IsVictory7 0x447d80) */
#define KP_RACE_RANDOM 6
#define KP_RACE_USER_SELECT 5
#define KP_RACE_NONE 7 /* the single-player custom races' empty entry (0x526547) */
#define KP_CUSTOM_RACES 8

/* The template's predicates over a setup. */
int kp_setup_ums(const KpGameSetup *s);                /* IsUseMapSettings 0x447db0 */
int kp_setup_team_game(const KpGameSetup *s);          /* IsTeamGame 0x4b2d90: the multiplayer byte and teams */
int kp_setup_top_vs_bottom(const KpGameSetup *s);      /* IsTopVsBottom 0x447e70 */
int kp_setup_alliances_allowed(const KpGameSetup *s);  /* AreAlliancesAllowed 0x4474d0 */
int kp_setup_ctf(const KpGameSetup *s);                /* IsCaptureTheFlag 0x447cb0: the victory byte 4 */
int kp_setup_sudden_death(const KpGameSetup *s);       /* IsSuddenDeath 0x447e60: the victory byte 5 */
int kp_setup_tournament(const KpGameSetup *s);         /* IsTournamentMode 0x447d00 */
int kp_setup_cheats(const KpGameSetup *s);             /* the template's cheats byte 0x67294e at 1 (ExecCheat 0x4b1c20) */
/* GetTeamSize 0x4b2b10: a team game's players per team (teams 2: 4, 3: 3, 4: 2), -1 for another count (the original's
 * error 0x5d "Illegal number of teams", network.tbl 0x49). */
int kp_setup_team_size(const KpGameSetup *s);
/* GetTeamCount 0x4b2bc0: Top vs Bottom's 2, else the template's teams. */
int kp_setup_team_count(const KpGameSetup *s);
/* The game player a record's commands play as (StartGame's storm_to_player 0x672878 by SetupTeamGameSlots 0x4b2db0):
 * in a team game a human's force's leader, else the record itself. */
int64_t kp_setup_record_player(const KpGameSetup *s, int64_t record);
/* SetupVictoryConditions 0x448530's first step: the restriction 1 or 3 outside Use Map Settings starts the computers'
 * melee AI scripts (StartCustomLevelAIScripts 0x4486a0); otherwise every player's AI is campaign AI (0x482ee0). */
int kp_setup_melee_ai(const KpGameSetup *s);

/* ApplyCampaignSlotRaces 0x443b90 over a lobby's records, the single-player custom races (the flag 0x5264c3 and the
 * races 0x526546: the local slot's, then up to eight computers', KP_RACE_NONE for none): outside Use Map Settings every
 * lobby computer slot opens, the local slot takes its race, and each race other than KP_RACE_NONE turns the last open
 * slot into a computer of that race. The lobby (the Play Custom screen) runs it before the start, as StartGame's step
 * 3 does. */
void kp_setup_custom_races(KpGameSetup *s, int64_t local, int64_t local_race, const int64_t races[KP_CUSTOM_RACES]);

/* The lobby's records a single-player custom game leaves for StartGame (Play Custom's Ok, CreateGameFromEntry 0x45bfc0;
 * the next scenario's CreateGameFromMapPath 0x45c510): the map's slots as LoadMapHeader 0x46b5c0 reads them (the OWNR
 * types and SIDE races of slots 0..7, a User Select race as Random with its slot flagged, the FORC force plus one of a
 * slot in the game, slots 8..11 empty; every Storm ID -1), the local human in the first open slot (PlaceLocalHumanSlot
 * 0x4bb920 by FindFreeJoinSlot 0x4794e0: type 2, the player's name, the Storm ID 0 CreateLocalGame 0x475220 gives),
 * the template's record (ValidateGameTemplate 0x45b700) and the races of the screen (ApplyCustomGameRaces 0x455020):
 * a melee template's local race and opponents' races through kp_setup_custom_races as StartGame's step 3 applies them,
 * another template's local race (the shown race list's) on the slot itself, the random races then resolved at once from
 * the game RNG's state before the game's seed (SetLocalRaceAndResolve 0x479c90: rng_state, KpCarry's, 0 for the
 * process's first game). local_race KP_RACE_NONE keeps the map's; races NULL is none. 0, or -1 when the map has no
 * open slot (the original's 0x80000007). */
int kp_setup_lobby(KpGameSetup *s, const KpMission *m, const uint8_t template_record[KP_TEMPLATE_SIZE], const char *name,
	int64_t local_race, const int64_t races[KP_CUSTOM_RACES], uint32_t rng_state);

/* StartGame's step 6 for a lobby's setup (g->setup, started 0) over the mission's map: the colours 0..7
 * (LoadPlayerColorTables 0x41d570), the random races (ResolveRandomRaces 0x478ee0), the slot types, starts and colours
 * (SetupGameSlots 0x479340). The game RNG draws them; the caller seeds it again after (step 7). g->setup is then the
 * started game's (started 1). */
void kp_gametype_slots(KpGame *g, const KpMission *m, KpRngGame *rng);
/* Whether a setup's colours are the rows 0..7 the original's are (LoadPlayerColorTables 0x41d570's, permuted by
 * ShufflePlayerColors 0x41d460): a replay's header with others is no game the original started. */
int kp_setup_colours_valid(const KpGameSetup *s);
/* The slot of the human with this Storm ID among the started records (RebuildStormSlotMaps 0x4793b0), or -1. */
int64_t kp_setup_storm_slot(const KpGameSetup *s, int64_t storm);

/* ApplyStartingResources 0x448420 (ResetPerGameState 0x444230). */
void kp_gametype_resources(KpGame *g);
/* The melee UNIT filter 0x469600 and the Use Map Settings user-select gate (LoadUnitSection 0x468f30's steps 3 and
 * 4): 1 when the record of type t for owner is left out. */
int kp_gametype_unit_excluded(KpGame *g, int64_t t, int64_t owner);
/* CreateStartingUnitsAndView 0x469e90 outside a save's load: the start units of each slot (CreateMeleeStartUnits
 * 0x47e100), a team game's of each team's leader (CreateTeamStartingUnits 0x47eb00). */
void kp_gametype_start_units(KpGame *g);
/* InitUnitCountsAndSupplyMax 0x414400's supply caps by race (Zerg, Terran, Protoss) and player: 400 outside a team
 * game; in one a team leader's max(400, 400 times its block's lobby races of the race, CountTeamRace 0x4b2c00), every
 * other player's 0. */
#define KP_SUPPLY_MAX 400
void kp_gametype_supply_max(KpGame *g, int64_t max[3][KP_PLAYERS]);
/* SetupVictoryConditions 0x448530's victory files: the template's .trg in place of the trigger lists (and Capture the
 * Flag's beacons first), after the melee AI's start (its scripts checked first: kp_aiscript_check). 0, or -1 with err
 * as the original's error 0x5d (an unknown Greed or Slaughter value, a missing file) or a melee script that would stop
 * the game. */
int kp_gametype_victory(KpGame *g, const struct KpVictoryFiles *v, KpError *err);

#ifdef __cplusplus
}
#endif

#endif
