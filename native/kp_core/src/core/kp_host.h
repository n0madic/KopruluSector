/*
 * A host's native game (plan phase S, stage S2): a mission's game with the data it starts from, its generators, the
 * files it keeps and the data identity of its saves (the CRC-32s of the files). The product's session (native/kp_app)
 * and the oracle's tools (kp_runner.h KpOracleGame) start their games here; the host reads the files (from a directory,
 * an Android asset, the web's preloaded data) and hands them over.
 */
#ifndef KP_HOST_H
#define KP_HOST_H

#include "kp_files.h"
#include "kp_game_data.h"
#include "kp_game.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The options of a game start. */
typedef struct {
	int events;            /* the event ring on (a presentation's or a drained oracle run's) */
	int trace;             /* the ring also records the trigger and AI traces (KP_EVENT_TRIGGER, KP_EVENT_AI) */
	int no_units;          /* the map without its preplaced units */
	int64_t mission_index; /* the campaign row 0x526524 (0: a custom map) */
	int64_t cheats;        /* the cheat word of the game before */
	int64_t turn;          /* the turn counter the menus left */
	const KpCarry *carry;  /* what the game before left (kp_carry_take), or NULL */
	int64_t speed;         /* the game speed byte 0x6d3130; KP_NIL: KP_DEFAULT_SPEED */
} KpHostOpts;

void kp_host_opts(KpHostOpts *o);

/* The file at path (malloc'd, NUL terminated past its size), or NULL. */
uint8_t *kp_read_file(const char *path, size_t *size);

/* The data a process's games share (grpmeta, the iscript, the AI scripts) with their CRC-32s for the saves'
 * identity. It must outlive every game started over it. */
typedef struct {
	KpGameShared data;
	uint32_t grp_crc, script_crc, ai_crc;
} KpHostShared;

/* Built from the original files (ADR 0018, stage D5: kp_grpmeta_from_files over the data set's DAT,
 * kp_iscript_from_files, kp_ai_bins_from_files), the CRC-32s of the structures' blobs; -1 with the error, sh released. */
int kp_host_shared_from_files(KpHostShared *sh, const KpFiles *files, const KpDat *dat, KpError *err);
void kp_host_shared_free(KpHostShared *sh);

typedef struct {
	KpGameData data;
	uint8_t *text; /* a language's overlay of the mission's strings (owned; kp_host_game_text) */
	uint8_t *speech; /* the mission's TRIG and MBRF with a language's TIME (owned; kp_host_game_speech) */
	KpRngGame rng;
	KpRngCrt crt;
	uint8_t *cv5, *vf4; /* its tileset's original CV5 and VF4 (owned; kp_host_game_tileset_files) */
	size_t cv5_size, vf4_size;
	const KpHostShared *shared;
	uint32_t dat_crc; /* the DAT's blob it was built into (the UMS overrides change the data's copy) */
	char name[KP_SAVE_NAME];
	char identity[64]; /* the data identity of save and load: the CRC-32s of the game's files */
	KpGame *g;
} KpHostGame;

/* Loads the game's data over the shared data under the mission's name (its saves carry it; NULL: "mission"): a copy of
 * the data set's DAT and the mission built from the original files (kp_mission_from_files, kp_mission_from_chk), moved
 * into hg (*mission left empty, also on failure). -1 with the error, hg released. */
int kp_host_game_load_from(KpHostGame *hg, const KpHostShared *sh, const KpDat *dat, KpMission *mission,
	const char *name, KpError *err);
/* The loaded mission's tileset as its original files, tileset\<name>.cv5 and .vf4, which kp_host_game_start needs: 0,
 * or -1 with the error (hg kept). */
int kp_host_game_tileset_files(KpHostGame *hg, const KpFiles *files, KpError *err);
/* A language's text over a loaded game, before its start: the MISN overlay's STRS (ADR 0016), taken and freed with the
 * game. -1 with the error (the mission's own text stays) when it is corrupt, of another kind or schema than a mission
 * or has other strings than the mission. */
int kp_host_game_text(KpHostGame *hg, uint8_t *overlay, size_t size, KpError *err);
/* A language's speech over a loaded game, before its start (ADR 0019, stage C2): the TIME of its map's triggers and
 * briefing (a transmission's and a wait's milliseconds), the language's scenario KP_HOST_SPEECH_MAP read through raw
 * (Remastered's names: kp_res_raw_files) and merged into a copy of the mission's records, taken and freed with the
 * game. 1 merged, 0 when raw has no such map (the mission's own TIME), -1 with the error when it is damaged or refused
 * or its TRIG and MBRF are not of the mission's sizes (the mission's own triggers stay). Records that differ in more
 * than TIME keep the mission's and are counted: the profile's SPEX lists the missions where those are Remastered's own
 * fixes. */
#define KP_HOST_SPEECH_MAP "locales/%s/Assets/campaign/%s/staredit/scenario.chk" /* the locale, "terran/terran01" */
typedef struct {
	int times;     /* the actions whose TIME the language's changed */
	int differ;    /* the records that differ from the mission's in more than TIME */
	int excepted;  /* the profile's SPEX lists the mission for the locale */
	char first[48]; /* the first such record, "TRIG record 12" */
} KpHostSpeech;
int kp_host_game_speech(KpHostGame *hg, const KpFiles *raw, const char *locale, const char *mission, KpHostSpeech *out,
	KpError *err);
/* Starts the loaded game over the CV5 and VF4 of kp_host_game_tileset_files at the seed and expansion; -1 with the
 * error, hg released. */
int kp_host_game_start(KpHostGame *hg, uint32_t seed, int expansion, const KpHostOpts *opts, KpError *err);
void kp_host_game_free(KpHostGame *hg);

#ifdef __cplusplus
}
#endif

#endif
