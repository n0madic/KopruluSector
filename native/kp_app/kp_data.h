/*
 * The original data a process plays from (ADR 0018, stage D8c): what the games and the interface share, built once
 * from the files of a source kp_res opened (a repack or an install): the data set's DAT, the shared game data
 * (grpmeta, iscript, the AI scripts), the palettes, the campaign, the interface's data, and the missions' catalog (the
 * campaign's mission directories by mapdata index, then the test scenarios of generated/scenarios/index.txt). The app
 * (sdl/kp_app_files.c), the oracle's tools (native/kp_core/tools/kp_runner.c) and the C suites (tests/c) share it.
 *
 * A mission is built from its files: a campaign mission from its scenario (kp_mission_from_files), a test scenario
 * from its CHK (generated/scenarios/<name>.chk, scdata/scenarios.py) with the packs its index line names.
 */
#ifndef KP_DATA_H
#define KP_DATA_H

#include <stddef.h>
#include <stdint.h>

#include "kp_campaign.h"
#include "kp_dat.h"
#include "kp_host.h"
#include "kp_map_class.h"
#include "kp_mission.h"
#include "kp_present.h"
#include "kp_res.h"
#include "kp_template.h"
#include "kp_ui_data.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KP_DATA_MISSIONS 256 /* the catalog's missions and the test scenarios */
#define KP_DATA_PACKS 160    /* a scenario's packs as its index line names them ("a,b,c") */
#define KP_DATA_SCENARIOS "generated/scenarios/" /* the test scenarios under the data's root: index.txt, <name>.chk */

/* The interface's vector fonts: their path in the interface's files and their file under fonts/ (a bundle's) or
 * third_party/fonts/ (the source tree's). */
#define KP_DATA_VECTOR_FONTS 2
extern const char *const KP_DATA_VECTOR_FONT_FILES[KP_DATA_VECTOR_FONTS][2];

/* A mission of the catalog or a test scenario on a catalog map. */
typedef struct {
	char name[64];      /* "terran/terran01", "scenarios/marine_group" */
	char key[96];       /* "missions/terran/terran01", "scenarios/marine_group": the goldens' and the saves' name */
	char directory[64]; /* "campaign\terran\terran01": its scenario's folder; "": a test scenario's CHK */
	char packs[KP_DATA_PACKS]; /* a test scenario's packs ("a,b,c"; "" none) */
	int64_t index;      /* the campaign row of the map (0x526524) */
	int catalog;
} KpDataMission;

/* A language's speech in the data (ADR 0019, stage C2): Remastered's advisor's first line, the locale's */
#define KP_DATA_SPEECH_MARKER "locales/%s/Assets/sound/Terran/advisor/TAdUpd00.wav"

typedef struct {
	KpFiles files; /* over the caller's KpRes */
	KpFiles raw;   /* Remastered's own files by their CASC names over it (kp_res_raw_files: a language's speech) */
	KpDat dat;     /* the data set's DAT; a game takes a copy of it */
	KpHostShared shared;
	KpPalettes palettes;
	KpCampaign campaign;
	KpUiData ui;
	KpTemplates templates;   /* the game templates (templates.lst and its .got files; ADR 0024) */
	uint8_t map_key[KP_MAP_KEY_SIZE]; /* the maps' signature key, from a 1.17 install's executable (kp_map_class.h) */
	int has_map_key;                  /* 0 without one (a repack, Remastered): every map's class 0 */
	KpDataMission missions[KP_DATA_MISSIONS];
	int mission_count;
} KpData;

/* The folder of original data under root: root itself when it is a source (kp_res_detect), else its
 * generated/repack-single (a computer's single file), else its generated/repack (a phone's and the web's groups); 1
 * with it in out, 0 for none. */
int kp_data_dir(const char *root, char *out, size_t size);

/* Builds d over res (borrowed: it must outlive d) with the interface's vector fonts (NULL, 0: none; their bytes must
 * outlive d) and the test scenarios' index (NULL: none); 0, or -1 with the error (d released). */
int kp_data_build(KpData *d, KpRes *res, const KpUiFile *vector_fonts, int vector_font_count, const char *scenarios,
	size_t scenarios_size, KpError *err);
void kp_data_free(KpData *d);

/* The catalog of d: the campaign's missions by mapdata index, then the scenarios of the index text (lines "name base
 * tileset width height packs", packs "a,b,c" or "-"), a scenario's index its base map's. */
void kp_data_catalog(KpData *d, const char *scenarios, size_t scenarios_size);
/* The mission of a key ("missions/<campaign>/<name>", "scenarios/<name>"), or NULL. */
const KpDataMission *kp_data_find(const KpData *d, const char *key);
/* The mission of an entry: a campaign mission's from its scenario, a test scenario's from its CHK (chk: its bytes,
 * malloc'd, taken also on failure; NULL for a campaign mission). 0, or -1 with the error (m released). */
int kp_data_mission(const KpData *d, const KpDataMission *entry, uint8_t *chk, size_t chk_size, KpMission *m,
	KpError *err);

/* A custom map (ADR 0024): an .scm/.scx file, an MPQ whose staredit\scenario.chk is the scenario and whose other files
 * are the sounds its strings name, or a scenario alone (a replay's, a save's MAPC). files reads the map's own files
 * first, then the data's. */
#define KP_DATA_MAP_SCENARIO "staredit\\scenario.chk"
#define KP_DATA_MAP_PACKS "doodads,effects,neutral,protoss,terran,zerg" /* a custom map may place any race's units */
typedef struct {
	struct KpMpq *mpq; /* NULL: a scenario alone */
	const KpFiles *base;
	KpFiles files;
	uint8_t *chk; /* the scenario alone's, until the mission takes it */
	size_t chk_size;
	char name[64]; /* the file's name without its folder, or the caller's */
} KpDataMap;

/* Opens the map at path over base (a KpData's files; borrowed): 0, or -1 with the error (nothing to close). */
int kp_data_map_open(KpDataMap *map, const KpFiles *base, const char *path, KpError *err);
/* A scenario alone (malloc'd, taken also on failure) under name, over base: 0, or -1 with the error. */
int kp_data_map_from_chk(KpDataMap *map, const KpFiles *base, uint8_t *chk, size_t chk_size, const char *name,
	KpError *err);
/* The map's scenario, malloc'd (a copy of a scenario alone's), or NULL with the error. */
uint8_t *kp_data_map_chk(KpDataMap *map, size_t *size, KpError *err);
/* The map's mission for a game of load (KP_CHK_MELEE, KP_CHK_UMS; kp_mission_from_map with KP_DATA_MAP_PACKS): 0, or -1
 * with the error (m released). The map stays open while a game plays it: its sounds are read through files. */
int kp_data_map_mission(KpDataMap *map, KpChkLoad load, KpMission *m, KpError *err);
void kp_data_map_close(KpDataMap *map);

#ifdef __cplusplus
}
#endif

#endif
