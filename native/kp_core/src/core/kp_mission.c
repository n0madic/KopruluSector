/* The mission from its scenario (kp_mission.h). */
#include "kp_mission.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kp_chk.h"
#include "kp_derive.h"
#include "kp_ids.h"

const char *const KP_TILESET_NAMES[KP_TILESETS] = { "badlands", "platform", "install", "ashworld", "jungle", "desert", "ice",
	"twilight" };

/* The parts parsed into a KpMission besides the raw CHK payloads and WALK, in this order. */
enum { PART_META, PART_RULE, PART_WAVS, PART_RGNS, PART_CONT, PART_PACK, PART_COUNT };

static int fail(KpError *err, const char *name, const char *msg)
{
	snprintf(err->msg, sizeof err->msg, "mission: %s: %s", name && name[0] ? name : "<data>", msg);
	return -1;
}

static int parse_meta(KpMission *m, KpBytes meta)
{
	const uint8_t *p = meta.data;
	int i;
	if (meta.size < 65) return -1;
	m->chk_version = kp_le_u16(p);
	m->era = kp_le_u16(p + 2);
	m->width = kp_le_u16(p + 4);
	m->height = kp_le_u16(p + 6);
	memcpy(m->owners, p + 8, 12);
	memcpy(m->races, p + 20, 12);
	memcpy(m->force_player, p + 32, 8);
	for (i = 0; i < 4; i++) m->force_name[i] = (uint16_t)kp_le_u16(p + 40 + 2 * i);
	memcpy(m->force_flags, p + 48, 4);
	m->name_string = (uint16_t)kp_le_u16(p + 52);
	m->description_string = (uint16_t)kp_le_u16(p + 54);
	m->has_colors = (p[64] & KP_MISSION_META_COLR) != 0;
	if (m->has_colors) memcpy(m->colors, p + 56, 8);
	m->tileset = m->era % 8; /* the ERA handler 0x468b00 masks with 7 (expansion on) */
	return 0;
}

static int parse_rules(KpMission *m, KpBytes r)
{
	uint32_t pos = 0;
	int n = 0;
	while (pos < r.size) {
		uint32_t len;
		if (r.size - pos < 8) return -1;
		len = kp_le_u32(r.data + pos + 4);
		if (len > r.size - pos - 8) return -1;
		pos += 8 + len;
		n++;
	}
	m->rules = (KpMissionRule *)calloc(n ? n : 1, sizeof *m->rules);
	if (!m->rules) return -1;
	for (pos = 0; m->rule_count < n; m->rule_count++) {
		KpMissionRule *rule = &m->rules[m->rule_count];
		memcpy(rule->tag, r.data + pos, 4);
		rule->tag[4] = '\0';
		rule->data.size = kp_le_u32(r.data + pos + 4);
		rule->data.data = r.data + pos + 8;
		pos += 8 + rule->data.size;
	}
	return 0;
}

static int parse_sounds(KpMission *m, KpBytes w)
{
	uint32_t i, n;
	if (w.size < 2) return -1;
	n = kp_le_u16(w.data);
	if ((uint64_t)2 + (uint64_t)12 * n > w.size) return -1;
	m->sounds = (KpMissionSound *)calloc(n ? n : 1, sizeof *m->sounds);
	if (!m->sounds) return -1;
	for (i = 0; i < n; i++) {
		const uint8_t *a = w.data + 2 + 12 * i;
		KpMissionSound *s = &m->sounds[i];
		s->string_id = (uint16_t)kp_le_u16(a);
		s->index = (uint16_t)kp_le_u16(a + 2);
		s->length = kp_le_u32(a + 4);
		s->avg_bytes_per_sec = kp_le_u32(a + 8);
	}
	m->sound_count = (int)n;
	return 0;
}

static int parse_regions(KpMissionRegions *r, KpBytes s)
{
	if (s.size < 12) return -1;
	r->data = s;
	r->width = kp_le_u16(s.data);
	r->height = kp_le_u16(s.data + 2);
	r->count = kp_le_u16(s.data + 4);
	r->split_count = kp_le_u16(s.data + 6);
	r->pool_words = kp_le_u32(s.data + 8);
	{
		/* in 64 bits: the counts come from the file */
		uint64_t pool_at = 12 + (uint64_t)r->count * KP_REGION_SIZE;
		uint64_t raster_at = pool_at + 2 * (uint64_t)r->pool_words;
		uint64_t splits_at = raster_at + 2 * (uint64_t)r->width * r->height;
		if (splits_at + (uint64_t)r->split_count * KP_SPLIT_SIZE != s.size) return -1;
		r->regions_at = 12;
		r->pool_at = (uint32_t)pool_at;
		r->raster_at = (uint32_t)raster_at;
		r->splits_at = (uint32_t)splits_at;
	}
	return 0;
}

static int parse_contours(KpMissionContours *c, KpBytes s)
{
	uint32_t pos = 16;
	int d;
	if (s.size < 16) return -1;
	c->data = s;
	for (d = 0; d < 4; d++) {
		c->counts[d] = kp_le_u16(s.data + 2 * d);
		c->capacities[d] = kp_le_u16(s.data + 8 + 2 * d);
		c->at[d] = pos;
		pos += c->counts[d] * KP_CONTOUR_SIZE; /* at most 4 * 65535 * 8: no overflow */
	}
	return pos == s.size ? 0 : -1;
}

static int parse_packs(KpMission *m, KpBytes s)
{
	uint32_t pos = 1, i, n;
	if (s.size < 1) return -1;
	n = s.data[0];
	if (n > sizeof m->packs / sizeof m->packs[0]) return -1;
	for (i = 0; i < n; i++) {
		if (pos >= s.size || s.data[pos] > s.size - pos - 1) return -1;
		m->packs[i].size = s.data[pos];
		m->packs[i].data = s.data + pos + 1;
		pos += 1 + s.data[pos];
	}
	m->pack_count = (int)n;
	return 0;
}

/* The parsed parts of a mission built from its CHK (kp_mission_from_chk). */
static int assemble(KpMission *m, const KpBytes part[], const char *name, KpError *err)
{
	if (parse_meta(m, part[PART_META]) != 0) return fail(err, name, "META section is malformed");
	if (parse_rules(m, part[PART_RULE]) != 0) return fail(err, name, "RULE section is malformed");
	if (parse_sounds(m, part[PART_WAVS]) != 0) return fail(err, name, "WAVS section is malformed");
	if (parse_regions(&m->regions, part[PART_RGNS]) != 0) return fail(err, name, "RGNS size mismatch");
	if (parse_contours(&m->contours, part[PART_CONT]) != 0) return fail(err, name, "CONT size mismatch");
	if (parse_packs(m, part[PART_PACK]) != 0) return fail(err, name, "PACK section is malformed");
	if (m->tiles.size != (uint64_t)2 * m->width * m->height) {
		char why[96];
		snprintf(why, sizeof why, "MTXM size does not match %ux%u", (unsigned)m->width, (unsigned)m->height);
		return fail(err, name, why);
	}
	return 0;
}

/* --- the mission from its scenario (ADR 0018, stage D4; scdata/missions.py, kpf/kpm.py build) --- */

#define META_SIZE 68 /* kpm.META <4H12s12s20s2H8sB3x */
#define META_FORC 32
#define META_FORC_NAMES (META_FORC + 8) /* FORC: 8 players' forces, then 4 names */
#define META_SPRP 52
#define TRIGGER_SIZE 2400
#define CONDITION_COUNT 16
#define CONDITION_SIZE 20
#define CONDITION_TYPE 0x0f
#define CONDITION_FLAGS 0x11
#define ACTION_COUNT 64
#define ACTIONS_OFFSET 320
#define ACTION_SIZE 32
#define ACTION_LOCATION 0x00
#define ACTION_STRING 0x04
#define ACTION_WAV 0x08
#define ACTION_NUMBER 0x14
#define ACTION_TYPE 0x1a
#define ACTION_FLAGS 0x1c
#define ENTRY_IGNORED 0x02
/* the condition and action tables of EvaluateTriggerConditions 0x4bded0 and the runner 0x4bdf10 (atlas P40) */
#define CONDITION_TYPES 24
#define ACTION_TYPES 60
#define ACTION_MOVE_UNIT 39
#define ACTION_ORDER 46
#define ACTION_SET_NEXT_SCENARIO 41
#define LOCATIONS 255
#define WAVS_ENTRY 12
#define STRING_IDS 65536
/* the fields and ids the checks of ADR 0023 read (kp_game_internal.h's KP_COND_*, KP_ACT_*, KP_SEL_*) */
#define CONDITION_LOCATION 0x00
#define CONDITION_PLAYER 0x04
#define CONDITION_UNIT 0x0c
#define ACTION_PLAYER 0x10
#define ACTION_UNIT 0x18
#define PLAYER_SLOTS 12
#define SELECTOR_CURRENT 13
#define SELECTOR_LAST_GROUP 26
#define UNIT_TYPES 228
#define UNIT_ANY 229        /* Any Unit; Men, Buildings and Factories follow */
#define UNIT_FACTORIES 232
#define UNIT_RECORD 36
#define UNIT_RECORD_TYPE 8
#define UNIT_RECORD_OWNER 16
enum {
	COND_COMMAND = 2, COND_BRING = 3, COND_ACCUMULATE = 4, COND_KILL = 5, COND_COMMAND_THE_MOST = 6,
	COND_COMMANDS_THE_MOST_AT = 7, COND_MOST_KILLS = 8, COND_DEATHS = 15, COND_COMMAND_THE_LEAST = 16,
	COND_COMMAND_THE_LEAST_AT = 17, COND_LEAST_KILLS = 18, COND_SCORE = 21
};
enum {
	ACT_CREATE_UNIT_WITH_PROPERTIES = 11, ACT_RUN_AI_SCRIPT = 15, ACT_RUN_AI_SCRIPT_AT = 16, ACT_LB_CONTROL = 17,
	ACT_LB_CONTROL_AT = 18, ACT_LB_KILLS = 20, ACT_KILL_UNIT = 22, ACT_REMOVE_UNIT = 24, ACT_LB_GOAL_CONTROL = 33,
	ACT_LB_GOAL_CONTROL_AT = 34, ACT_LB_GOAL_KILLS = 36, ACT_CREATE_UNIT = 44, ACT_SET_DEATHS = 45,
	ACT_SET_ALLIANCE = 57
};

/* The end of a trigger's actions that run: the first type 0 not ignored (the runner 0x4bdf10). */
static uint32_t actions_end(const uint8_t *base)
{
	uint32_t j;
	for (j = 0; j < ACTION_COUNT; j++) {
		const uint8_t *a = base + ACTIONS_OFFSET + j * ACTION_SIZE;
		if (a[ACTION_TYPE] == 0 && !(a[ACTION_FLAGS] & ENTRY_IGNORED)) return j;
	}
	return ACTION_COUNT;
}

/* Whether an action before actions_end runs: neither ignored nor of type 0. */
static int action_runs(const uint8_t *a) { return a[ACTION_TYPE] != 0 && !(a[ACTION_FLAGS] & ENTRY_IGNORED); }

/* A selector that names one slot directly (kp_trigger_players): neither the current player nor a group. */
static int direct_slot(uint32_t selector) { return selector < SELECTOR_CURRENT || selector > SELECTOR_LAST_GROUP; }

/* A unit type the counters' getters take (kp_trigger_queries.c unit_counter, kp_trigger_cached_count): a type, Any
 * Unit, Men, Buildings or Factories. */
static int counter_unit(uint32_t unit) { return unit < UNIT_TYPES || (unit >= UNIT_ANY && unit <= UNIT_FACTORIES); }

/* A condition that stops the game when evaluated (ADR 0023): a counter of a direct slot past the players (read_slot,
 * kp_trigger_queries.c) or of a unit type past the table (unit_counter, kp_trigger_cached_count). NULL when none. */
static const char *condition_stops(const uint8_t *c, char *why, size_t why_size)
{
	uint32_t kind = c[CONDITION_TYPE], player = kp_le_u32(c + CONDITION_PLAYER), unit = kp_le_u16(c + CONDITION_UNIT);
	int located = c[CONDITION_LOCATION] != 0; /* Bring and the rankings At count nothing without a location */
	int slot = kind == COND_COMMAND || (kind == COND_BRING && located) || kind == COND_ACCUMULATE || kind == COND_KILL
		|| kind == COND_DEATHS || kind == COND_SCORE;
	int counted = kind == COND_COMMAND || (kind == COND_BRING && located) || kind == COND_KILL || kind == COND_DEATHS
		|| kind == COND_COMMAND_THE_MOST || kind == COND_COMMAND_THE_LEAST || kind == COND_MOST_KILLS
		|| kind == COND_LEAST_KILLS || ((kind == COND_COMMANDS_THE_MOST_AT || kind == COND_COMMAND_THE_LEAST_AT) && located);
	if (slot && direct_slot(player) && player >= PLAYER_SLOTS) {
		snprintf(why, why_size, "a counter of player slot %u", (unsigned)player);
		return why;
	}
	if (counted && !counter_unit(unit)) {
		snprintf(why, why_size, "a counter of unit type %u", (unsigned)unit);
		return why;
	}
	return NULL;
}

/* An action that stops the game when it runs (ADR 0023): Kill and Remove Unit of a direct slot past the players
 * (kp_trigger_units.c kill_player_units), Set Alliance with slot 12 (set_alliance), Set Deaths of a unit type past the
 * aggregates for a slot below 8 (kp_trigger_set_deaths), a leaderboard of units or kills of a type past the table
 * (lb_get). NULL when none. */
static const char *action_stops(const uint8_t *a, char *why, size_t why_size)
{
	uint32_t kind = a[ACTION_TYPE], player = kp_le_u32(a + ACTION_PLAYER), unit = kp_le_u16(a + ACTION_UNIT);
	int located = a[ACTION_LOCATION] != 0;
	if ((kind == ACT_KILL_UNIT || kind == ACT_REMOVE_UNIT) && direct_slot(player) && player >= PLAYER_SLOTS) {
		snprintf(why, why_size, "%s of player slot %u", kind == ACT_KILL_UNIT ? "Kill Unit" : "Remove Unit",
			(unsigned)player);
		return why;
	}
	if (kind == ACT_SET_ALLIANCE && player == PLAYER_SLOTS) {
		snprintf(why, why_size, "Set Alliance with player slot %u", (unsigned)player);
		return why;
	}
	if (kind == ACT_SET_DEATHS && unit > UNIT_FACTORIES && !(direct_slot(player) && player >= 8)) {
		snprintf(why, why_size, "Set Deaths of unit type %u", (unsigned)unit);
		return why;
	}
	if ((kind == ACT_LB_CONTROL || kind == ACT_LB_KILLS || kind == ACT_LB_GOAL_CONTROL || kind == ACT_LB_GOAL_KILLS
			|| ((kind == ACT_LB_CONTROL_AT || kind == ACT_LB_GOAL_CONTROL_AT) && located))
		&& !counter_unit(unit)) {
		snprintf(why, why_size, "a leaderboard of unit type %u", (unsigned)unit);
		return why;
	}
	return NULL;
}

/* scdata/analyze/triggers.py validate: what the original's tables would index past (a reachable condition above type
 * 23, an action above 59, an Order past the 255 locations, a Move Unit to location 256); and what the runtime stops
 * the game at (condition_stops, action_stops). Entries the runners never reach are left. */
static int validate_triggers(const uint8_t *trig, uint32_t size, char *err, size_t err_size)
{
	uint32_t t, j;
	char why[KP_ERROR_SIZE];
	if (size % TRIGGER_SIZE) {
		snprintf(err, err_size, "TRIG: trigger payload of %u bytes is not a multiple of %d", (unsigned)size, TRIGGER_SIZE);
		return -1;
	}
	for (t = 0; t < size / TRIGGER_SIZE; t++) {
		const uint8_t *base = trig + (size_t)t * TRIGGER_SIZE;
		uint32_t end = actions_end(base);
		for (j = 0; j < CONDITION_COUNT; j++) { /* the evaluated ones: ignored skipped, the first type 0 ends them */
			const uint8_t *c = base + j * CONDITION_SIZE;
			if (c[CONDITION_FLAGS] & ENTRY_IGNORED) continue;
			if (c[CONDITION_TYPE] == 0) break;
			if (c[CONDITION_TYPE] >= CONDITION_TYPES) {
				snprintf(err, err_size, "TRIG: trigger %u condition %u: type %u (the table has %d)", (unsigned)t,
					(unsigned)j, (unsigned)c[CONDITION_TYPE], CONDITION_TYPES);
				return -1;
			}
			if (condition_stops(c, why, sizeof why)) {
				snprintf(err, err_size, "TRIG: trigger %u condition %u: %s", (unsigned)t, (unsigned)j, why);
				return -1;
			}
		}
		for (j = 0; j < end; j++) {
			const uint8_t *a = base + ACTIONS_OFFSET + j * ACTION_SIZE;
			uint32_t kind = a[ACTION_TYPE], number = kp_le_u32(a + ACTION_NUMBER);
			if (!action_runs(a)) continue;
			if (kind >= ACTION_TYPES) {
				snprintf(err, err_size, "TRIG: trigger %u action %u: type %u (the table has %d)", (unsigned)t, (unsigned)j,
					(unsigned)kind, ACTION_TYPES);
				return -1;
			}
			if (kind == ACTION_ORDER && number > LOCATIONS) {
				snprintf(err, err_size, "TRIG: trigger %u action %u: Order to location %u", (unsigned)t, (unsigned)j,
					(unsigned)number);
				return -1;
			}
			if (kind == ACTION_MOVE_UNIT && kp_le_u32(a + ACTION_LOCATION) && number && !(number & 0xff)) {
				snprintf(err, err_size, "TRIG: trigger %u action %u: Move Unit to location 256 (number %u)", (unsigned)t,
					(unsigned)j, (unsigned)number);
				return -1;
			}
			if (action_stops(a, why, sizeof why)) {
				snprintf(err, err_size, "TRIG: trigger %u action %u: %s", (unsigned)t, (unsigned)j, why);
				return -1;
			}
		}
	}
	return 0;
}

/* The preplaced units refused (ADR 0023): a start location of a slot past the players, which the start stops at
 * (kp_start.c load_units). */
static int validate_units(const uint8_t *units, uint32_t size, char *err, size_t err_size)
{
	uint32_t i;
	for (i = 0; i + UNIT_RECORD <= size; i += UNIT_RECORD) {
		uint32_t type = kp_le_u16(units + i + UNIT_RECORD_TYPE), owner = units[i + UNIT_RECORD_OWNER];
		if (type == KP_TYPE_START_LOCATION && owner >= PLAYER_SLOTS) {
			snprintf(err, err_size, "UNIT: unit %u: a start location of player %u", (unsigned)(i / UNIT_RECORD),
				(unsigned)owner);
			return -1;
		}
	}
	return 0;
}

int kp_mission_check_ai(const KpMission *m, const KpAiBins *b, KpError *err)
{
	uint32_t checked[ACTION_COUNT], count = 0, t, j, k;
	char why[KP_ERROR_SIZE];
	for (t = 0; t < m->triggers.size / TRIGGER_SIZE; t++) {
		const uint8_t *base = m->triggers.data + (size_t)t * TRIGGER_SIZE;
		uint32_t end = actions_end(base);
		for (j = 0; j < end; j++) {
			const uint8_t *a = base + ACTIONS_OFFSET + j * ACTION_SIZE;
			uint32_t id = kp_le_u32(a + ACTION_NUMBER);
			if (!action_runs(a) || (a[ACTION_TYPE] != ACT_RUN_AI_SCRIPT && a[ACTION_TYPE] != ACT_RUN_AI_SCRIPT_AT)) continue;
			for (k = 0; k < count && checked[k] != id; k++) {
			}
			if (k < count) continue;
			if (count < ACTION_COUNT) checked[count++] = id;
			if (kp_aiscript_check(b, id, why, sizeof why) != 0) {
				snprintf(err->msg, sizeof err->msg, "TRIG: trigger %u action %u: %s", (unsigned)t, (unsigned)j, why);
				return -1;
			}
		}
	}
	return 0;
}

/* kpm.wav_string_ids: the WAV strings of TRIG's Transmission (7) and Play WAV (8) and MBRF's Play WAV (2) and
 * Transmission (8) actions, disabled ones too; distinct and sorted into ids, their count */
static int wav_string_ids(const KpChkSection *trig, const KpChkSection *mbrf, uint8_t *seen, uint16_t *ids)
{
	const KpChkSection *s[2];
	int k, n = 0;
	uint32_t i, a;
	s[0] = trig;
	s[1] = mbrf;
	memset(seen, 0, STRING_IDS);
	for (k = 0; k < 2; k++)
		for (i = 0; i + TRIGGER_SIZE <= s[k]->size; i += TRIGGER_SIZE)
			for (a = 0; a < ACTION_COUNT; a++) {
				const uint8_t *act = s[k]->data + i + ACTIONS_OFFSET + a * ACTION_SIZE;
				uint32_t kind = act[ACTION_TYPE], wav = kp_le_u32(act + ACTION_WAV) & 0xffff;
				if ((k == 0 ? kind == 7 || kind == 8 : kind == 2 || kind == 8) && wav) seen[wav] = 1;
			}
	for (i = 1; i < STRING_IDS; i++)
		if (seen[i]) ids[n++] = (uint16_t)i;
	return n;
}

/* GetMapString 0x445670 (kp_map_string, kpm.map_string) over STR's bytes: 0 for none or the empty string (index 0, past
 * the count, or past STR: the original reads its heap there), else 1 with the string's bytes up to its terminator or
 * STR's end (ADR 0024) */
static int map_string_in(const uint8_t *strs, size_t strs_size, uint32_t index, const uint8_t **text, size_t *n)
{
	uint32_t offset;
	const uint8_t *end;
	*text = NULL;
	*n = 0;
	index &= 0xffff;
	if (index == 0 || strs_size < 2 || index - 1 >= kp_le_u16(strs) || 2 * (size_t)index + 2 > strs_size) return 0;
	offset = kp_le_u16(strs + 2 * index);
	if (offset >= strs_size) return 0;
	*text = strs + offset;
	end = (const uint8_t *)memchr(*text, 0, strs_size - offset);
	*n = end ? (size_t)(end - *text) : strs_size - offset;
	return 1;
}

static int map_string(const KpChkSection *strs, uint32_t index, const uint8_t **text, size_t *n)
{
	return map_string_in(strs->data, strs->size, index, text, n);
}

/* The path a WAV action builds from its string (the WAVS builder's and the players'): <directory>\<string>, or the
 * string alone for the directory "" (a custom map's). 1 with the path in out, 0 for no string or an empty one, -1 for a
 * path too long. */
static int wav_path(const uint8_t *strs, size_t strs_size, const char *directory, uint32_t string_id, char *out,
	size_t size)
{
	const uint8_t *text;
	size_t len;
	int s = map_string_in(strs, strs_size, string_id, &text, &len);
	if (s <= 0 || len == 0) return s;
	if (strlen(directory) + 1 + len >= size) return -1;
	if (directory[0])
		snprintf(out, size, "%s\\%.*s", directory, (int)len, (const char *)text);
	else /* a custom map's: the file in its MPQ */
		snprintf(out, size, "%.*s", (int)len, (const char *)text);
	return 1;
}

int kp_mission_sound_path(const KpMission *m, const char *directory, uint32_t string_id, char *out, size_t size)
{
	return wav_path(m->strings.data, m->strings.size, directory, string_id, out, size);
}

/* WAVS (kpm.mission_sounds, build): u16 count; per WAV string its id, its index: its position among the WAV strings
 * (KP_MISSION_NO_SOUND: its file missing or refused by the original's parser; kp_files_wav reads a file's header
 * alone, so a damaged copy whose header reads is found), the data chunk's length and nAvgBytesPerSec. malloc'd, NULL
 * with the reason. */
static uint8_t *mission_wavs(const KpFiles *f, const char *directory, const KpChkSection *strs,
	const KpChkSection *trig, const KpChkSection *mbrf, size_t *size, KpError *err)
{
	uint8_t *seen = (uint8_t *)malloc(STRING_IDS), *out = NULL;
	uint16_t *ids = (uint16_t *)malloc(STRING_IDS * sizeof *ids);
	int n, k;
	if (!seen || !ids) goto oom;
	n = wav_string_ids(trig, mbrf, seen, ids);
	*size = 2 + (size_t)n * WAVS_ENTRY;
	if (!(out = (uint8_t *)calloc(1, *size))) goto oom;
	kp_le_put16(out, (uint32_t)n);
	for (k = 0; k < n; k++) {
		uint8_t *e = out + 2 + (size_t)k * WAVS_ENTRY;
		const uint8_t *text;
		size_t len;
		char path[KP_ERROR_SIZE];
		KpWavInfo w;
		int found = 0;
		if (map_string(strs, ids[k], &text, &len) > 0 && len > 0) { /* the path the WAV actions build */
			if (wav_path(strs->data, strs->size, directory, ids[k], path, sizeof path) < 0) {
				snprintf(err->msg, sizeof err->msg, "mission: %s: WAV string %u is too long", directory, (unsigned)ids[k]);
				goto failed;
			}
			if ((found = kp_files_wav(f, path, &w, err)) == -2) goto failed;
		}
		kp_le_put16(e, ids[k]);
		kp_le_put16(e + 2, found == 1 ? (uint32_t)k : KP_MISSION_NO_SOUND);
		kp_le_put32(e + 4, found == 1 ? w.data_length : 0);
		kp_le_put32(e + 8, found == 1 ? w.avg_bytes_per_sec : 0);
	}
	free(seen);
	free(ids);
	return out;
oom:
	snprintf(err->msg, sizeof err->msg, "mission: %s: out of memory", directory);
failed:
	free(seen);
	free(ids);
	free(out);
	return NULL;
}

const uint8_t *kp_mission_packs_entry(const KpDefsProfile *p, const char *fourcc, const char *directory, size_t *size)
{
	const KpDefsTable *t = p ? kp_defs_table(p, fourcc) : NULL;
	size_t pos = 2, dn = strlen(directory);
	uint32_t count, k;
	if (!t || t->size < 2) return NULL;
	count = kp_le_u16(t->data);
	for (k = 0; k < count; k++) {
		size_t len, at, end, i, packs;
		int same;
		if (pos >= t->size || pos + 1 + (len = t->data[pos]) + 1 > t->size) return NULL;
		at = pos + 1 + len;
		for (same = len == dn, i = 0; i < len && same; i++) {
			int c = (unsigned char)directory[i];
			if (c == '/') c = '\\';
			if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
			same = c == t->data[pos + 1 + i];
		}
		for (packs = t->data[at], end = at + 1, i = 0; i < packs && end < t->size; i++) end += 1 + (size_t)t->data[end];
		if (i < packs || end > t->size) return NULL;
		if (same) {
			*size = end - at;
			return t->data + at;
		}
		pos = end;
	}
	return NULL;
}

static const char *const RULE_TAGS[] = { "UNIS", "UNIx", "UPGS", "UPGx", "TECS", "TECx", "UPGR", "PUPx", "PTEC", "PTEx",
	"PUNI" }; /* rules.LAYOUT: the override sections */

static int rule_tag(const char *tag)
{
	size_t k;
	for (k = 0; k < sizeof RULE_TAGS / sizeof RULE_TAGS[0]; k++)
		if (memcmp(RULE_TAGS[k], tag, 4) == 0) return 1;
	return 0;
}

/* n bytes of a section into the META field at p, cut or zero-filled (struct.pack's fixed strings) */
static void meta_put(uint8_t *p, const KpChkSection *s, size_t n)
{
	memcpy(p, s->data, s->size < n ? s->size : n);
}

/* A scenario's mission by a load's groups: kp_mission_from_chk (the campaign's), kp_mission_from_map (a game's). */
static int build(KpMission *m, const KpFiles *f, uint8_t *chk_bytes, size_t chk_size, const char *name,
	const char *directory, const uint8_t *packs, size_t pack_size, KpChkLoad load, KpError *err)
{
	/* the sections the mission reads; a campaign load requires them all, a melee one reads no MASK, MRGN, TRIG, MBRF
	 * or UPRP (they stay empty: the fog mask all 0xff, as without MASK) */
	static const char *const NEEDED[] = { "VER ", "DIM ", "ERA ", "OWNR", "SIDE", "FORC", "SPRP", "MTXM", "MASK", "THG2",
		"UNIT", "MRGN", "STR ", "TRIG", "MBRF", "UPRP" };
	enum { VER, DIM, ERA, OWNR, SIDE, FORC, SPRP, MTXM, MASK, THG2, UNIT, MRGN, STR, TRIG, MBRF, UPRP, NEEDED_COUNT };
	static const uint32_t MELEE_UNREAD = 1u << MASK | 1u << MRGN | 1u << TRIG | 1u << MBRF | 1u << UPRP;
	static const uint8_t NONE[1] = { 0 };
	char path[KP_ERROR_SIZE], why[KP_ERROR_SIZE];
	KpChk chk;
	const KpChkSection *s[NEEDED_COUNT], *colr;
	const uint8_t *src[NEEDED_COUNT]; /* each section's bytes: into chk_bytes, else copied after the parts */
	uint32_t len[NEEDED_COUNT];
	KpDerived derived;
	KpBytes part[PART_COUNT];
	uint8_t meta[META_SIZE], *rules = NULL, *wavs = NULL, *cv5 = NULL, *vf4 = NULL, *mask = NULL, *blob;
	size_t rules_size = 0, wavs_size = 0, cv5_size, vf4_size, total, at, copied = 0, offsets[NEEDED_COUNT];
	int inside[NEEDED_COUNT];
	uint32_t w, h;
	int i, k, rc = -1, game = load != KP_CHK_CAMPAIGN;
	memset(m, 0, sizeof *m);
	memset(&chk, 0, sizeof chk);
	memset(&derived, 0, sizeof derived);
	if (!chk_bytes) return fail(err, name, "out of memory");
	if (kp_chk_load(&chk, chk_bytes, chk_size, f->profile, 1, load, why, sizeof why) != 0) {
		fail(err, name, why);
		goto done;
	}
	for (i = 0; i < NEEDED_COUNT; i++) {
		s[i] = kp_chk_section(&chk, NEEDED[i]);
		src[i] = s[i] ? s[i]->data : NONE;
		len[i] = s[i] ? s[i]->size : 0;
		if (!s[i] && !(load == KP_CHK_MELEE && (MELEE_UNREAD >> i & 1u))) {
			snprintf(why, sizeof why, "the scenario has no '%s' section", NEEDED[i]);
			fail(err, name, why);
			goto done;
		}
	}
	colr = kp_chk_section(&chk, "COLR");
	w = kp_le_u16(s[DIM]->data);
	h = kp_le_u16(s[DIM]->data + 2);
	if (!game && (s[MTXM]->size != (uint64_t)2 * w * h || s[MASK]->size != (uint64_t)w * h)) {
		/* the native loader accepts other lengths; no campaign map has one */
		snprintf(why, sizeof why, "MTXM/MASK sizes %u/%u do not match %ux%u", (unsigned)s[MTXM]->size,
			(unsigned)s[MASK]->size, (unsigned)w, (unsigned)h);
		fail(err, name, why);
		goto done;
	}
	if (game) {
		/* LoadTileMapSection 0x469170 copies over the tile buffer an earlier map filled: a map whose chunks leave
		 * part of it unwritten would show that map's tiles; LoadFogMaskSection 0x469260 writes past the mask of the
		 * map's size */
		if (w == 0 || h == 0 || s[MTXM]->size < (uint64_t)2 * w * h) {
			snprintf(why, sizeof why, "MTXM: %u bytes leave tiles of the %ux%u map unwritten", (unsigned)s[MTXM]->size,
				(unsigned)w, (unsigned)h);
			fail(err, name, why);
			goto done;
		}
		len[MTXM] = 2 * w * h;
		if (s[MASK] && s[MASK]->size > (uint64_t)w * h) {
			snprintf(why, sizeof why, "MASK: %u bytes, past the %ux%u map", (unsigned)s[MASK]->size, (unsigned)w,
				(unsigned)h);
			fail(err, name, why);
			goto done;
		}
		if (!s[MASK] || s[MASK]->size < (uint64_t)w * h) { /* the rest 0xff, as the mask stays without MASK */
			if (!(mask = (uint8_t *)malloc((size_t)w * h))) goto oom;
			memset(mask, 0xff, (size_t)w * h);
			if (s[MASK]) memcpy(mask, s[MASK]->data, s[MASK]->size);
			src[MASK] = mask;
			len[MASK] = w * h;
		}
	}
	if ((s[TRIG] && validate_triggers(s[TRIG]->data, s[TRIG]->size, why, sizeof why) != 0)
		|| validate_units(s[UNIT]->data, s[UNIT]->size, why, sizeof why) != 0) {
		fail(err, name, why);
		goto done;
	}
	/* META */
	memset(meta, 0, sizeof meta);
	meta_put(meta, s[VER], 2);
	meta_put(meta + 2, s[ERA], 2);
	meta_put(meta + 4, s[DIM], 4);
	meta_put(meta + 8, s[OWNR], 12);
	meta_put(meta + 20, s[SIDE], 12);
	meta_put(meta + META_FORC, s[FORC], 20); /* the native FORC zero-fills the tail */
	meta_put(meta + META_SPRP, s[SPRP], 4);
	if (colr) {
		meta_put(meta + 56, colr, 8);
		meta[64] = KP_MISSION_META_COLR;
	}
	/* RULE: the override sections in dispatch order, {fourcc, u32 size, payload} (rules.encode_rule_section) */
	for (k = 0; k < chk.count; k++)
		if (rule_tag(chk.sections[k].tag)) rules_size += 8 + chk.sections[k].size;
	if (!(rules = (uint8_t *)malloc(rules_size ? rules_size : 1))) goto oom;
	for (k = 0, at = 0; k < chk.count; k++)
		if (rule_tag(chk.sections[k].tag)) {
			memcpy(rules + at, chk.sections[k].tag, 4);
			kp_le_put32(rules + at + 4, chk.sections[k].size);
			memcpy(rules + at + 8, chk.sections[k].data, chk.sections[k].size);
			at += 8 + chk.sections[k].size;
		}
	if (!directory || !s[TRIG]) { /* a test scenario (scdata/scenarios.py builds it with none), or a melee map: no WAVs */
		if (!(wavs = (uint8_t *)calloc(1, wavs_size = 2))) goto oom;
	} else if (!(wavs = mission_wavs(f, directory, s[STR], s[TRIG], s[MBRF], &wavs_size, err))) {
		goto done;
	}
	{ /* WALK, RGNS and CONT from the tileset of ERA & 7 (kpm.add_terrain_derived) */
		const char *tileset = KP_TILESET_NAMES[kp_le_u16(s[ERA]->data) & 7];
		snprintf(path, sizeof path, "tileset\\%s.cv5", tileset);
		if (!(cv5 = kp_files_need(f, path, &cv5_size, err))) goto done;
		snprintf(path, sizeof path, "tileset\\%s.vf4", tileset);
		if (!(vf4 = kp_files_need(f, path, &vf4_size, err))) goto done;
		if (kp_derive_terrain(w, h, src[MTXM], cv5, cv5_size, vf4, vf4_size, &derived, why, sizeof why) != 0) {
			fail(err, name, why);
			goto done;
		}
	}
	for (i = 0; i < NEEDED_COUNT; i++) {
		inside[i] = src[i] >= chk_bytes && src[i] < chk_bytes + chk_size;
		offsets[i] = inside[i] ? (size_t)(src[i] - chk_bytes) : 0;
		if (!inside[i]) copied += len[i];
	}
	/* one blob: the scenario (the raw payloads point into it), the parts built here, then the payloads the load laid
	 * over each other or made */
	total = chk_size + META_SIZE + rules_size + wavs_size + derived.walk_size + derived.regions_size
		+ derived.contours_size + pack_size + copied;
	if (!(blob = (uint8_t *)realloc(chk_bytes, total ? total : 1))) goto oom;
	chk_bytes = NULL;
	m->blob = blob;
	m->size = total;
	m->chk_size = chk_size;
	m->load = load;
	at = chk_size;
#define APPEND(out, bytes, n) \
	do { \
		if (n) memcpy(blob + at, (bytes), (n)); \
		(out).data = blob + at; \
		(out).size = (uint32_t)(n); \
		at += (n); \
	} while (0)
	APPEND(part[PART_META], meta, META_SIZE);
	APPEND(part[PART_RULE], rules, rules_size);
	APPEND(part[PART_WAVS], wavs, wavs_size);
	APPEND(m->walk, derived.walk, derived.walk_size);
	APPEND(part[PART_RGNS], derived.regions, derived.regions_size);
	APPEND(part[PART_CONT], derived.contours, derived.contours_size);
	APPEND(part[PART_PACK], packs, pack_size);
	{
		KpBytes raw[NEEDED_COUNT];
		for (i = 0; i < NEEDED_COUNT; i++) {
			if (inside[i]) {
				raw[i].data = blob + offsets[i];
				raw[i].size = len[i];
			} else {
				APPEND(raw[i], src[i], len[i]);
			}
		}
		m->tiles = raw[MTXM], m->mask = raw[MASK], m->sprites = raw[THG2], m->units = raw[UNIT];
		m->locations = raw[MRGN], m->strings = raw[STR], m->triggers = raw[TRIG], m->briefing = raw[MBRF];
		m->properties = raw[UPRP];
	}
#undef APPEND
	rc = assemble(m, part, directory && directory[0] ? directory : name, err);
	goto done;
oom:
	fail(err, name, "out of memory");
done:
	if (rc != 0) kp_mission_free(m);
	kp_chk_free(&chk);
	free(chk_bytes);
	free(mask);
	free(rules);
	free(wavs);
	free(cv5);
	free(vf4);
	kp_derived_free(&derived);
	return rc;
}

int kp_mission_from_chk(KpMission *m, const KpFiles *f, uint8_t *chk_bytes, size_t chk_size, const char *name,
	const char *directory, const uint8_t *packs, size_t pack_size, KpError *err)
{
	return build(m, f, chk_bytes, chk_size, name, directory, packs, pack_size, KP_CHK_CAMPAIGN, err);
}

int kp_mission_from_map(KpMission *m, const KpFiles *f, uint8_t *chk_bytes, size_t chk_size, const char *name,
	KpChkLoad load, const uint8_t *packs, size_t pack_size, KpError *err)
{
	if (load == KP_CHK_CAMPAIGN || load == KP_CHK_HEADER) {
		free(chk_bytes);
		memset(m, 0, sizeof *m);
		return fail(err, name, "a custom map is loaded as a melee or use map settings game");
	}
	return build(m, f, chk_bytes, chk_size, name, "", packs, pack_size, load, err);
}

/* A map string's bytes into out (at most size - 1 of them, NUL-terminated; "" for none): 1, or 0 for a null string */
static int map_string_copy(const KpChkSection *strs, uint32_t index, char *out, size_t size)
{
	const uint8_t *text;
	size_t n;
	out[0] = 0;
	if (strs == NULL || !map_string(strs, index, &text, &n)) return 0;
	if (n >= size) n = size - 1;
	memcpy(out, text, n);
	out[n] = 0;
	return 1;
}

int kp_map_header(KpMapHeader *h, const uint8_t *chk, size_t size, const KpDefsProfile *profile, int expansion,
	char *err, size_t err_size)
{
	const KpChkSection *dim, *era, *ownr, *side, *sprp, *strs, *forc;
	uint8_t force_chunk[20] = {0};
	KpChk c;
	int p;
	memset(h, 0, sizeof *h);
	h->first_human = -1;
	if (kp_chk_load(&c, chk, size, profile, expansion, KP_CHK_HEADER, err, err_size) != 0) {
		h->version = c.version;
		return -1;
	}
	h->version = c.version;
	dim = kp_chk_section(&c, "DIM ");
	era = kp_chk_section(&c, "ERA ");
	ownr = kp_chk_section(&c, "OWNR");
	side = kp_chk_section(&c, "SIDE");
	sprp = kp_chk_section(&c, "SPRP");
	strs = kp_chk_section(&c, "STR ");
	forc = kp_chk_section(&c, "FORC");
	if (dim == NULL || dim->size < 4 || era == NULL || ownr == NULL || ownr->size < KP_MAP_HEADER_SLOTS || side == NULL
		|| side->size < KP_MAP_HEADER_SLOTS) {
		snprintf(err, err_size, "the map's header has no DIM, ERA, OWNR or SIDE");
		kp_chk_free(&c);
		return -1;
	}
	h->width = (uint16_t)kp_le_u16(dim->data);
	h->height = (uint16_t)kp_le_u16(dim->data + 2);
	h->tileset = (uint16_t)(kp_le_u16(era->data) & 7);
	for (p = 0; p < KP_MAP_HEADER_SLOTS; p++) {
		uint8_t type = ownr->data[p];
		h->owners[p] = type;
		h->races[p] = side->data[p];
		h->user_select[p] = side->data[p] == KP_MAP_RACE_USER_SELECT; /* 0x478a20 from the header's step 3 */
		/* 0x478e70: the slots of a player (rescuable, either, open, neutral); 0x478ea0: the computers; 0x479550: the
		 * open slots of the player table the header filled (0x479bf0 keeps the types) */
		if (type >= 3 && type <= 7) h->players++;
		if (type == 1 || type == 5) h->computers++;
		if (type == 6) {
			h->humans++;
			if (h->first_human < 0) h->first_human = (int8_t)p;
		}
	}
	if (sprp != NULL && sprp->size >= 4) { /* the info's WORDs +0 and +2: the scenario's name and description */
		map_string_copy(strs, kp_le_u16(sprp->data), h->title, sizeof h->title);
		map_string_copy(strs, kp_le_u16(sprp->data + 2), h->description, sizeof h->description);
	}
	/* FORC: 8 slot forces, 4 WORD name string ids, 4 flags */
	if (forc != NULL) memcpy(force_chunk, forc->data, forc->size < sizeof force_chunk ? forc->size : sizeof force_chunk);
	memcpy(h->forces, force_chunk, KP_MAP_HEADER_SLOTS);
	memcpy(h->force_flags, force_chunk + 16, KP_MAP_HEADER_FORCES);
	for (p = 0; p < KP_MAP_HEADER_FORCES; p++)
		h->force_named[p] = (uint8_t)map_string_copy(strs, kp_le_u16(force_chunk + 8 + 2 * p), h->force_names[p],
			sizeof h->force_names[p]);
	kp_chk_free(&c);
	return 0;
}

int kp_mission_from_files(KpMission *m, const KpFiles *f, const char *directory, KpError *err)
{
	char path[KP_ERROR_SIZE];
	const uint8_t *packs;
	uint8_t *chk;
	size_t chk_size, pack_size = 0;
	memset(m, 0, sizeof *m);
	if (!(packs = kp_mission_packs_entry(f->profile, "MPAK", directory, &pack_size)))
		return fail(err, directory, "the profile has no resource packs for this mission (MPAK)");
	snprintf(path, sizeof path, "%s\\staredit\\scenario.chk", directory);
	if (!(chk = kp_files_need(f, path, &chk_size, err))) return -1;
	return kp_mission_from_chk(m, f, chk, chk_size, directory, directory, packs, pack_size, err);
}

int kp_mission_packs_payload(const char *list, uint8_t *out, size_t cap)
{
	const char *names[KP_MISSION_PACKS];
	size_t lens[KP_MISSION_PACKS], at = 1;
	int count = 0, k, j;
	while (*list) {
		size_t n = strcspn(list, ",");
		if (n == 0 || n > 255 || count == KP_MISSION_PACKS) return -1;
		names[count] = list;
		lens[count++] = n;
		list += n;
		if (*list == ',') list++;
	}
	for (k = 1; k < count; k++) /* sorted by bytes (kpm.packs_payload) */
		for (j = k; j > 0; j--) {
			size_t a = lens[j - 1], b = lens[j];
			int c = memcmp(names[j - 1], names[j], a < b ? a : b);
			const char *tn;
			size_t tl;
			if (c < 0 || (c == 0 && a <= b)) break;
			tn = names[j], names[j] = names[j - 1], names[j - 1] = tn;
			tl = lens[j], lens[j] = lens[j - 1], lens[j - 1] = tl;
		}
	if (cap < 1) return -1;
	out[0] = (uint8_t)count;
	for (k = 0; k < count; k++) {
		if (at + 1 + lens[k] > cap) return -1;
		out[at] = (uint8_t)lens[k];
		memcpy(out + at + 1, names[k], lens[k]);
		at += 1 + lens[k];
	}
	return (int)at;
}

int kp_mission_string_looked_up(const KpMission *m, uint32_t string_id)
{
	size_t r;
	int k, a;
	for (k = 0; k < m->sound_count; k++)
		if (m->sounds[k].string_id == string_id) return 1;
	for (r = 0; r + TRIGGER_SIZE <= m->triggers.size; r += TRIGGER_SIZE)
		for (a = 0; a < ACTION_COUNT; a++) {
			const uint8_t *act = m->triggers.data + r + ACTIONS_OFFSET + (size_t)a * ACTION_SIZE;
			if (act[ACTION_TYPE] == ACTION_SET_NEXT_SCENARIO && (kp_le_u32(act + ACTION_STRING) & 0xffff) == string_id) return 1;
		}
	return 0;
}

/* A part of `from`'s blob moved to the same place in `to`. */
static void rebase(KpBytes *b, const uint8_t *from, uint8_t *to)
{
	if (b->data) b->data = to + (b->data - from);
}

int kp_mission_copy(KpMission *out, const KpMission *m)
{
	const uint8_t *from = m->blob;
	uint8_t *to;
	int k;
	*out = *m;
	out->blob = (uint8_t *)malloc(m->size ? m->size : 1);
	out->rules = (KpMissionRule *)malloc((m->rule_count ? (size_t)m->rule_count : 1) * sizeof *m->rules);
	out->sounds = (KpMissionSound *)malloc((m->sound_count ? (size_t)m->sound_count : 1) * sizeof *m->sounds);
	if (!out->blob || !out->rules || !out->sounds) {
		kp_mission_free(out);
		return -1;
	}
	to = out->blob;
	if (m->size) memcpy(to, from, m->size);
	if (m->rule_count) memcpy(out->rules, m->rules, (size_t)m->rule_count * sizeof *m->rules);
	if (m->sound_count) memcpy(out->sounds, m->sounds, (size_t)m->sound_count * sizeof *m->sounds);
	rebase(&out->tiles, from, to);
	rebase(&out->mask, from, to);
	rebase(&out->sprites, from, to);
	rebase(&out->units, from, to);
	rebase(&out->locations, from, to);
	rebase(&out->strings, from, to);
	rebase(&out->triggers, from, to);
	rebase(&out->briefing, from, to);
	rebase(&out->properties, from, to);
	rebase(&out->walk, from, to);
	rebase(&out->regions.data, from, to);
	rebase(&out->contours.data, from, to);
	for (k = 0; k < out->rule_count; k++) rebase(&out->rules[k].data, from, to);
	for (k = 0; k < out->pack_count; k++) rebase(&out->packs[k], from, to);
	return 0;
}

void kp_mission_free(KpMission *m)
{
	free(m->rules);
	free(m->sounds);
	free(m->blob);
	memset(m, 0, sizeof *m);
}
