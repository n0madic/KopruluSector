/*
 * Mission container (.kpm, kind MISN; docs/formats/kpm.md). The loader copies the file; the raw
 * CHK payloads, the derived terrain data and the override sections stay packed and point into
 * the copy.
 */
#ifndef KP_MISSION_H
#define KP_MISSION_H

#include <stddef.h>
#include <stdint.h>

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
	uint16_t string_id, index; /* index KP_MISSION_NO_SOUND: the file is missing */
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

/* The tilesets by a mission's tileset index (its era & 7) and their converted files' path under the data root. */
extern const char *const KP_TILESET_NAMES[8];
#define KP_TILESET_PATH "generated/data/tilesets/%s.kpd"

typedef struct {
	uint8_t *blob; /* owned copy of the file */
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
	KpBytes packs[32]; /* names of the resource packs (ADR 0004), sorted */
} KpMission;

/* expect may be NULL; it adds profile and source-hash checks. Returns -1 with err set. */
int kp_mission_load(KpMission *m, const uint8_t *data, size_t size, const char *name, const KpKpfExpect *expect,
	KpError *err);
/* kp_mission_load over the file's malloc'd bytes, taken (also on failure) instead of copied. */
int kp_mission_take(KpMission *m, uint8_t *data, size_t size, const char *name, const KpKpfExpect *expect,
	KpError *err);
void kp_mission_free(KpMission *m);

#ifdef __cplusplus
}
#endif

#endif
