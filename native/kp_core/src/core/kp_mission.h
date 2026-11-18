/*
 * A mission built from its scenario (ADR 0018, stage D4): kp_mission_from_files reads the CHK, derives the terrain
 * data and lays them out in one blob as the parts the game reads (META, RULE, WAVS, WALK, RGNS, CONT, PACK and the raw
 * CHK payloads, docs/formats/kpm.md); the parts point into the blob.
 */
#ifndef KP_MISSION_H
#define KP_MISSION_H

#include <stddef.h>
#include <stdint.h>

#include "kp_aiscript.h"
#include "kp_chk.h"
#include "kp_files.h"
#include "kp_kpf.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KP_MISSION_NO_SOUND 0xffffu
#define KP_MISSION_META_COLR 0x01u
#define KP_REGION_SIZE 32
#define KP_SPLIT_SIZE 6
#define KP_CONTOUR_SIZE 8

typedef struct {
	const uint8_t *data;
	uint32_t size;
} KpBytes;

/* An override section of RULE, in native dispatch order. */
typedef struct {
	char tag[5];
	KpBytes data;
} KpMissionRule;

typedef struct {
	/* index: the WAV string's position among the mission's (string-id order), whether or not the others' files are
	 * there, so a language's speech keyed by it never shifts (ADR 0016); KP_MISSION_NO_SOUND: this file is missing */
	uint16_t string_id, index;
	uint32_t length, avg_bytes_per_sec;
} KpMissionSound;

/* RGNS: header, region records, neighbour pool, W x H raster, split records. */
typedef struct {
	KpBytes data;
	uint32_t width, height, count, split_count, pool_words;
	uint32_t regions_at, pool_at, raster_at, splits_at;
} KpMissionRegions;

/* CONT: per direction (top, right, bottom, left) the record count, the native capacity and
 * the offset of its 8-byte records. */
typedef struct {
	KpBytes data;
	uint32_t counts[4], capacities[4], at[4];
} KpMissionContours;

/* The tilesets by a mission's tileset index (its era & 7). */
#define KP_TILESETS 8
extern const char *const KP_TILESET_NAMES[KP_TILESETS];

#define KP_MISSION_PACKS 32

typedef struct {
	uint8_t *blob; /* owned: the scenario and the parts built from it */
	size_t size;
	size_t chk_size; /* the scenario's bytes at the blob's start, as read (a replay's map) */
	KpChkLoad load; /* how the scenario was read: KP_CHK_CAMPAIGN, or a custom map's game (a save keeps its MAPC) */
	uint32_t chk_version, era, width, height, tileset;
	uint8_t owners[12], races[12];
	uint8_t force_player[8], force_flags[4];
	uint16_t force_name[4];
	uint16_t name_string, description_string;
	int has_colors;
	uint8_t colors[8];
	/* raw CHK payloads */
	KpBytes tiles, mask, sprites, units, locations, strings, triggers, briefing, properties;
	int rule_count;
	KpMissionRule *rules; /* owned */
	int sound_count;
	KpMissionSound *sounds; /* owned, file order */
	KpBytes walk;           /* (height * 4) rows of (width * 4) mini-cells */
	KpMissionRegions regions;
	KpMissionContours contours;
	int pack_count;
	KpBytes packs[KP_MISSION_PACKS]; /* names of the resource packs (ADR 0004), sorted */
} KpMission;

/* The mission of a campaign directory ("campaign\\terran\\terran01") from its files: <directory>\staredit\scenario.chk
 * read by the profile's loader tables (kp_chk), its triggers validated, META, the override sections (RULE), the WAV
 * strings' files (WAVS), the terrain-derived WALK, RGNS and CONT from the tileset of ERA & 7 (kp_derive), the packs of
 * the profile's MPAK. The blob holds the scenario and those parts. Returns -1 with err set. */
int kp_mission_from_files(KpMission *m, const KpFiles *f, const char *directory, KpError *err);
/* The same from a scenario's bytes (malloc'd, taken, also on failure) under name (its errors'): directory the folder
 * its WAV strings name files in (NULL: a test scenario, scdata/scenarios.py, with no WAVs), packs PACK's payload
 * (kp_mission_packs_payload). Returns -1 with err set. */
int kp_mission_from_chk(KpMission *m, const KpFiles *f, uint8_t *chk, size_t chk_size, const char *name,
	const char *directory, const uint8_t *packs, size_t pack_size, KpError *err);
/* A custom map's mission (ADR 0024): its scenario (taken as by kp_mission_from_chk) read as the original reads it for a
 * game of load, KP_CHK_MELEE (the melee group: no triggers, briefing, settings or locations, the fog mask all 0xff) or
 * KP_CHK_UMS (use map settings); its WAV strings name files of f by themselves (the map's MPQ first, kp_app), its
 * strings checked as the game reads them (a melee map's: SPRP's and FORC's). Returns -1 with err set. */
int kp_mission_from_map(KpMission *m, const KpFiles *f, uint8_t *chk, size_t chk_size, const char *name,
	KpChkLoad load, const uint8_t *packs, size_t pack_size, KpError *err);
/* A map's header as LoadMapHeader 0x46b5c0 reads it for the map lists (LoadMapEntryInfo 0x45d870, ADR 0024): the
 * header group of its scenario (KP_CHK_HEADER), its slots 0..7 and their counts, its name and description (SPRP's
 * strings in the map's encoding, cut to the list's buffers). */
#define KP_MAP_HEADER_SLOTS 8
#define KP_MAP_HEADER_TITLE 32       /* the entry's title +0x49 */
#define KP_MAP_HEADER_DESCRIPTION 256 /* its description +0x69 */
#define KP_MAP_RACE_USER_SELECT 5
#define KP_MAP_HEADER_FORCES 4
#define KP_MAP_FORCE_NAME 30          /* LoadForceChunk 0x443320's SStrCopy bound */
typedef struct {
	uint16_t version;             /* VER, also when the read failed (0: none): a later one is the expansion's (> 59) */
	uint16_t width, height, tileset; /* DIM, ERA & 7 (0x5264b4, 0x5264b6, 0x5264bc) */
	uint8_t owners[KP_MAP_HEADER_SLOTS], races[KP_MAP_HEADER_SLOTS]; /* OWNR, SIDE */
	uint8_t user_select[KP_MAP_HEADER_SLOTS]; /* 0x673f08: SIDE's User Select slots */
	uint8_t players, computers, humans; /* 0x478e70 (types 3..7), 0x478ea0 (1 and 5), 0x479550 (6) */
	int8_t first_human;           /* the first open slot (type 6), -1 none */
	char title[KP_MAP_HEADER_TITLE], description[KP_MAP_HEADER_DESCRIPTION];
	/* FORC (LoadForceChunk 0x443320; a short chunk zero-filled): each slot's force as the chunk holds it (0-based), the
	 * 4 force flags, and the force names of the map's strings (force_named[f] 0: the string is null, the lobby shows
	 * network.tbl's "Force n" instead) */
	uint8_t forces[KP_MAP_HEADER_SLOTS], force_flags[KP_MAP_HEADER_FORCES], force_named[KP_MAP_HEADER_FORCES];
	char force_names[KP_MAP_HEADER_FORCES][KP_MAP_FORCE_NAME];
} KpMapHeader;
/* 0, or -1 with err (h->version still the VER read). */
int kp_map_header(KpMapHeader *h, const uint8_t *chk, size_t size, const KpDefsProfile *profile, int expansion,
	char *err, size_t err_size);
/* A PACK payload of the profile's compiled table fourcc by its key, matched in lower case with '\\' for '/' (MPAK: each
 * campaign mission's packs by its directory; SPAK: each test scenario's by its name; recorded from the reference install
 * by scdata/missions.py write_packs): its size in *size, or NULL when the table has no such key. */
const uint8_t *kp_mission_packs_entry(const KpDefsProfile *p, const char *fourcc, const char *key, size_t *size);
/* PACK's payload (kpm.packs_payload: u8 count, then u8 length + name a pack, sorted) of a list "a,b,c" ("": none) into
 * out: its size, or -1 for an empty or too long name, too many packs or too small an out. */
int kp_mission_packs_payload(const char *list, uint8_t *out, size_t cap);
/* The file a trigger's WAV string names (the PlayWav and transmission actions, the briefing's): <directory>\<the
 * mission's string string_id> (ADR 0018, stage D5: the app reads it as it reads the other original files). 1 with the
 * path in out, 0 for no string or an empty one, -1 for a malformed STR or a path longer than size. */
int kp_mission_sound_path(const KpMission *m, const char *directory, uint32_t string_id, char *out, size_t size);
/* Whether the game looks a STR row up by its text (the language's kept rows, kp_locale_mission), so a language keeps the base's: a sound's
 * (m's WAV strings) or Set Next Scenario's map name (TRIG's action 41). */
int kp_mission_string_looked_up(const KpMission *m, uint32_t string_id);
/* The mission's AI scripts checked against the binaries before a game (ADR 0023): every script a Run AI Script (At)
 * action that runs names, by kp_aiscript_check. Returns -1 with err set ("TRIG: trigger T action A: ..."). */
int kp_mission_check_ai(const KpMission *m, const KpAiBins *b, KpError *err);

/* A copy of m that owns its own blob, rules and sounds (its parts point into the copy's blob): 0, or -1 out of memory
 * with out zeroed. */
int kp_mission_copy(KpMission *out, const KpMission *m);
void kp_mission_free(KpMission *m);

#ifdef __cplusplus
}
#endif

#endif
