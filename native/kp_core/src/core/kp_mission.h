/*
 * A mission built from its scenario (ADR 0018, stage D4): kp_mission_from_files reads the CHK, derives the terrain
 * data and lays them out in one blob as the parts the game reads (META, RULE, WAVS, WALK, RGNS, CONT, PACK and the raw
 * CHK payloads, docs/formats/kpm.md); the parts point into the blob. KP_MISSION_KIND is a language's overlay's kind
 * (kp_host_game_text, kp_host_game_speech).
 */
#ifndef KP_MISSION_H
#define KP_MISSION_H

#include <stddef.h>
#include <stdint.h>

#include "kp_files.h"
#include "kp_kpf.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KP_MISSION_KIND "MISN"
#define KP_MISSION_SCHEMA 1
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
 * its WAV strings name files in (NULL: a test scenario, converter/scenarios.py, with no WAVs), packs PACK's payload
 * (kp_mission_packs_payload). Returns -1 with err set. */
int kp_mission_from_chk(KpMission *m, const KpFiles *f, uint8_t *chk, size_t chk_size, const char *name,
	const char *directory, const uint8_t *packs, size_t pack_size, KpError *err);
/* A PACK payload of the profile's compiled table fourcc by its key, matched in lower case with '\\' for '/' (MPAK: each
 * campaign mission's packs by its directory; SPAK: each test scenario's by its name; recorded from the reference install
 * by converter/missions.py write_packs): its size in *size, or NULL when the table has no such key. */
const uint8_t *kp_mission_packs_entry(const KpDefsProfile *p, const char *fourcc, const char *key, size_t *size);
/* PACK's payload (kpm.packs_payload: u8 count, then u8 length + name a pack, sorted) of a list "a,b,c" ("": none) into
 * out: its size, or -1 for an empty or too long name, too many packs or too small an out. */
int kp_mission_packs_payload(const char *list, uint8_t *out, size_t cap);
/* The file a trigger's WAV string names (the PlayWav and transmission actions, the briefing's): <directory>\<the
 * mission's string string_id> (ADR 0018, stage D5: the app reads it as it reads the other original files). 1 with the
 * path in out, 0 for no string or an empty one, -1 for a malformed STR or a path longer than size. */
int kp_mission_sound_path(const KpMission *m, const char *directory, uint32_t string_id, char *out, size_t size);
void kp_mission_free(KpMission *m);

#ifdef __cplusplus
}
#endif

#endif
