/*
 * The campaign progression of the original (uiSingle.cpp, atlas P41 0x4bb5b0..0x4bbc20; plan phase S): campaign.kpd's
 * six campaign tables and mission directories, the next mission after a game, the SetNextScenario resolver and the
 * profile's progress. Pure state functions over a run and a profile the caller keeps (the oracle's campaign chains now;
 * the product's session from stage S2).
 */
#ifndef KP_CAMPAIGN_H
#define KP_CAMPAIGN_H

#include <stddef.h>
#include <stdint.h>

#include "kp_kpf.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KP_CAMPAIGN_KIND "CAMP"
#define KP_CAMPAIGN_SCHEMA 1
#define KP_CAMPAIGN_TABLES 6         /* Zerg, Terran, Protoss of 0x50e0b8 (group 0) and 0x50e0c4 (group 1) */
#define KP_CAMPAIGN_TABLES_PER_GROUP 3
#define KP_CAMPAIGN_MISSIONS 65      /* the directory strings 0x51abb8 */
#define KP_CAMPAIGN_RACES 256
/* The glue states and the game mode the next step leads to (0x66a274, 0x6662b8). */
#define KP_CAMPAIGN_GLUE_MENU 6
#define KP_CAMPAIGN_GLUE_EXPANSION_MENU 22
#define KP_CAMPAIGN_RESULT_VICTORY 3

typedef struct {
	uint16_t label, mission, cinematic;
	uint8_t race, hidden;
} KpCampaignRow;

typedef struct {
	int expansion, race, count;
	const KpCampaignRow *rows; /* with the table's terminator row (mission 0) */
} KpCampaignTable;

/* campaign.kpd (converter/campaign.py; docs/formats/kpd.md): the tables in the executable's pointer order, the
 * mission directories by mapdata index ("campaign\terran\terran01", "" none) and the cheat bits a new game keeps. */
typedef struct {
	KpCampaignTable tables[KP_CAMPAIGN_TABLES];
	int table_count;
	const char *directories[KP_CAMPAIGN_MISSIONS];
	int directory_count;
	uint32_t carried;
	void *owned; /* the rows and the directories' bytes */
} KpCampaign;

int kp_campaign_load(KpCampaign *c, const uint8_t *data, size_t size, const char *name, KpError *err);
void kp_campaign_free(KpCampaign *c);

/* A campaign row: its table (0..5; -1 none) and index in it. */
typedef struct {
	int table, index;
} KpCampaignRef;

/* A run: the expansion byte 0x536720, the cached row 0x6ca718, the game's mission index 0x526524 and the next
 * scenario's name ("" none, 0x526526). */
typedef struct {
	int expansion;
	KpCampaignRef row;
	int64_t mission;
	char pending[64];
} KpCampaignRun;

/* The profile: the highest mission index reached per race (character record +0x28 original, +0x34 expansion). */
typedef struct {
	int64_t original[KP_CAMPAIGN_RACES], expansion[KP_CAMPAIGN_RACES];
} KpCampaignProfile;

/* What follows a game (AdvanceCampaignAfterGame 0x4bb790). */
enum {
	KP_CAMPAIGN_STEP_MENU,      /* the main menu (glue state 0) */
	KP_CAMPAIGN_STEP_RESTART,   /* the lost mission again (mode 5); the pending name stays */
	KP_CAMPAIGN_STEP_MISSION,   /* the next game (CreateCampaignMissionGame 0x4bb6c0): mission, race, glue */
	KP_CAMPAIGN_STEP_CINEMATIC, /* a cinematic (mode 10): cinematic, mission */
	KP_CAMPAIGN_STEP_GLUE,      /* the campaign's end in a menu: glue 6 or 22 */
	KP_CAMPAIGN_STEP_EPILOG     /* the campaign's end in the epilog */
};

typedef struct {
	int kind;
	int64_t mission, race, glue, cinematic;
} KpCampaignStep;

void kp_campaign_new_run(KpCampaignRun *run, int expansion, int64_t mission);
void kp_campaign_new_profile(KpCampaignProfile *p);
/* The row of a ref, or NULL. */
const KpCampaignRow *kp_campaign_row(const KpCampaign *c, KpCampaignRef ref);
/* ResolveNextScenarioRow 0x4bba20: a SetNextScenario file name to a row; without a dot, or no match, the run's row. */
KpCampaignRef kp_campaign_resolve(const KpCampaign *c, const KpCampaignRun *run, const char *name);
/* RecordNextMissionReached 0x4bb970 (the score screen, any result). */
void kp_campaign_record_next_reached(const KpCampaign *c, const KpCampaignRun *run, KpCampaignProfile *p);
/* AdvanceCampaignAfterGame 0x4bb790 after a game (won: the local player's victory). */
void kp_campaign_advance(const KpCampaign *c, KpCampaignRun *run, KpCampaignProfile *p, int won, KpCampaignStep *out);
/* What follows a game that ended with `result` (2 defeat, 3 victory, 5 draw) and left the next scenario's name: the
 * score screen's RecordNextMissionReached, then AdvanceCampaignAfterGame through its cinematics (their ids in
 * cinematics[0..*count-1], at most max). */
void kp_campaign_after_game(const KpCampaign *c, KpCampaignRun *run, KpCampaignProfile *p, int64_t result,
	const char *next_scenario, KpCampaignStep *out, int64_t *cinematics, int *count, int max);
/* What follows a mission history's cinematic row (StartCampaignEpisode 0x4bb5b0: the main loop plays the cinematic,
 * then AdvanceCampaignAfterGame goes on from its row): the row of `cinematic` and `mission` in the expansion's or the
 * original's tables, advanced past it and the cinematic rows after it (their ids in cinematics[0..*count-1], at most
 * max) to a mission, or to the campaign's end for a table's last row; 0 (a STEP_MENU) when no table has the row. */
int kp_campaign_after_cinematic(const KpCampaign *c, int expansion, int64_t mission, int64_t cinematic,
	KpCampaignRun *run, KpCampaignProfile *p, KpCampaignStep *out, int64_t *cinematics, int *count, int max);
/* A run from a mission played directly (the Ophelia warp, a tool's choice): its row and expansion byte from the tables
 * that list it (the original's first); 0 for a mission of no campaign. */
int kp_campaign_run_of_mission(const KpCampaign *c, int64_t mission, KpCampaignRun *run);
/* The generated mission name of a mission index ("terran/terran01"), or 0. */
int kp_campaign_mission_name(const KpCampaign *c, int64_t index, char *out, size_t size);

#ifdef __cplusplus
}
#endif

#endif
