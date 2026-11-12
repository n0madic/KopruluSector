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
#include "kp_mission.h"
#include "kp_present.h"
#include "kp_res.h"
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

#ifdef __cplusplus
}
#endif

#endif
