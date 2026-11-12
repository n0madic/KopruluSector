#include "kp_scenario.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kp_kpf.h"

#define CHUNK_HEADER 8
#define PLAYER_SLOTS 12
#define UNIT_RECORD 36    /* serial, x, y, type, relation, valid states, valid properties, owner, hp, shields, energy,
			   * resources, hangar, states, unused, the related unit's serial */
#define THG2_RECORD 10    /* type, x, y, owner, unused, flags */
#define THG2_SPRITE 0x1000
#define LOCATION_RECORD 20 /* MRGN: left, top, right, bottom, name string, elevation flags */
#define TRIGGER_RECORD 2400
#define CONDITIONS 16
#define CONDITION_SIZE 20
#define ACTIONS 64
#define ACTIONS_OFFSET 320
#define ACTION_SIZE 32
#define EXECUTION_FLAGS 2368
#define OWNERS 2372
#define OWNER_COUNT 27
#define RELATION_NYDUS 0x200
#define RELATION_ADDON 0x400
#define NYDUS_CANAL 134
#define RESOURCES_VALID 0x10
#define HANGAR_VALID 0x20
#define STR_LIMIT 0x10000 /* STR's offsets are 16-bit */
#define TEXTS_MAX 256

/* The OWNR controllers a spec's "players" names. */
static const struct {
	const char *name;
	uint8_t controller;
} CONTROLLERS[] = { { "inactive", 0 }, { "rescuable", 3 }, { "computer", 5 }, { "human", 6 }, { "neutral", 7 } };
/* The CHK unit states a spec unit may start in ("state": [...]): valid bits and values alike. */
static const struct {
	const char *name;
	uint16_t bit;
} UNIT_STATES[] = { { "cloaked", 0x1 }, { "burrowed", 0x2 } };
/* Optional percentages of a spec unit and their CHK valid-property bits. */
static const struct {
	const char *name;
	uint16_t bit;
} UNIT_PERCENTS[] = { { "hp", 0x2 }, { "shields", 0x4 }, { "energy", 0x8 } };
/* The player tech sections "researched" marks: PTEC's 24 techs, PTEx's 44. */
static const struct {
	const char *tag;
	int techs;
} TECH_SECTIONS[] = { { "PTEC", 24 }, { "PTEx", 44 } };

/* The TRIG condition and action ids by name (tables 0x50e058 and 0x50df68, atlas P40; BWAPI's names in snake case;
 * scdata/analyze/triggers.py CONDITION_IDS, ACTION_IDS). */
static const char *const CONDITION_NAMES[] = {
	"none", "countdown_timer", "command", "bring", "accumulate", "kill", "command_the_most", "commands_the_most_at",
	"most_kills", "highest_score", "most_resources", "switch", "elapsed_time", "mission_briefing", "opponents",
	"deaths", "command_the_least", "command_the_least_at", "least_kills", "lowest_score", "least_resources", "score",
	"always", "never" };
static const char *const ACTION_NAMES[] = {
	"none", "victory", "defeat", "preserve_trigger", "wait", "pause_game", "unpause_game", "transmission", "play_wav",
	"display_text_message", "center_view", "create_unit_with_properties", "set_mission_objectives", "set_switch",
	"set_countdown_timer", "run_ai_script", "run_ai_script_at_location", "leaderboard_control",
	"leaderboard_control_at_location", "leaderboard_resources", "leaderboard_kills", "leaderboard_points",
	"kill_unit", "kill_unit_at_location", "remove_unit", "remove_unit_at_location", "set_resources", "set_score",
	"minimap_ping", "talking_portrait", "mute_unit_speech", "unmute_unit_speech", "leaderboard_computer_players",
	"leaderboard_goal_control", "leaderboard_goal_control_at_location", "leaderboard_goal_resources",
	"leaderboard_goal_kills", "leaderboard_goal_points", "move_location", "move_unit", "leaderboard_greed",
	"set_next_scenario", "set_doodad_state", "set_invincibility", "create_unit", "set_deaths", "order", "comment",
	"give_units_to_player", "modify_unit_hit_points", "modify_unit_energy", "modify_unit_shield_points",
	"modify_unit_resource_amount", "modify_unit_hangar_count", "pause_timer", "unpause_timer", "draw",
	"set_alliance_status", "disable_debug_mode", "enable_debug_mode" };

/* A field of a condition (20 bytes) or an action (32 bytes): its name, offset and width. */
typedef struct {
	const char *name;
	int offset, width;
} Field;

static const Field CONDITION_FIELDS[] = { { "location", 0, 4 }, { "player", 4, 4 }, { "number", 8, 4 }, { "unit", 12, 2 },
	{ "comparison", 14, 1 }, { "type", 15, 1 }, { "resource", 16, 1 }, { "flags", 17, 1 } };
static const Field ACTION_FIELDS[] = { { "location", 0, 4 }, { "string", 4, 4 }, { "wav", 8, 4 }, { "time", 12, 4 },
	{ "player", 16, 4 }, { "number", 20, 4 }, { "unit", 24, 2 }, { "type", 26, 1 }, { "modifier", 27, 1 }, { "flags", 28, 1 } };

typedef struct {
	char *err;
	size_t err_size;
	int failed;
	const char *texts[TEXTS_MAX]; /* the trigger texts appended to STR */
	int text_count, string_count;
} Spec;

#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 2, 3)))
#endif
static int fail(Spec *s, const char *format, ...)
{
	va_list ap;
	if (!s->failed) {
		va_start(ap, format);
		vsnprintf(s->err, s->err_size, format, ap);
		va_end(ap);
	}
	s->failed = 1;
	return -1;
}

static void put(uint8_t *at, int width, int64_t v)
{
	if (width == 1) at[0] = (uint8_t)v;
	else if (width == 2) kp_le_put16(at, (uint32_t)v);
	else kp_le_put32(at, (uint32_t)v);
}

static int is_number(const KpJson *v) { return v && v->type == KP_JSON_NUMBER && v->integral; }

/* A required integer field. */
static int64_t need(Spec *s, const KpJson *object, const char *key, const char *what, long k)
{
	const KpJson *v = kp_json_get(object, key);
	if (!is_number(v)) fail(s, "%s %ld: no integer \"%s\"", what, k, key);
	return is_number(v) ? v->i : 0;
}

static int lookup(const char *const *names, int count, const char *name)
{
	int k;
	for (k = 0; k < count; k++)
		if (strcmp(names[k], name) == 0) return k;
	return -1;
}

/* A condition's or an action's type: its id, or its name in the table. */
static int64_t type_of(Spec *s, const KpJson *v, const char *const *names, int count, const char *what, long trigger)
{
	int k;
	if (is_number(v)) return v->i;
	if (!v || v->type != KP_JSON_STRING || (k = lookup(names, count, v->s)) < 0)
		return fail(s, "trigger %ld: unknown %s type \"%s\"", trigger, what, v && v->type == KP_JSON_STRING ? v->s : "?"), 0;
	return k;
}

/* A string field given as text: the text appended to STR, its 1-based id; a number as it is. */
static int64_t string_of(Spec *s, const KpJson *v)
{
	if (!v) return 0;
	if (is_number(v)) return v->i;
	if (v->type != KP_JSON_STRING || s->text_count == TEXTS_MAX) return fail(s, "a trigger's string field (a number, or text)"), 0;
	s->texts[s->text_count++] = v->s;
	return s->string_count + s->text_count;
}

static const Field *field(const Field *fields, int count, const char *name)
{
	int k;
	for (k = 0; k < count; k++)
		if (strcmp(fields[k].name, name) == 0) return &fields[k];
	return NULL;
}

static void conditions(Spec *s, uint8_t *record, const KpJson *list, long t)
{
	int j, m;
	for (j = 0; j < kp_json_len(list); j++) {
		const KpJson *c = kp_json_at(list, j);
		uint8_t *at = record + j * CONDITION_SIZE;
		for (m = 0; m < kp_json_len(c); m++) {
			const Field *f = field(CONDITION_FIELDS, (int)(sizeof CONDITION_FIELDS / sizeof CONDITION_FIELDS[0]), c->keys[m]);
			int64_t v;
			if (!f) {
				fail(s, "trigger %ld condition %d: unknown field \"%s\"", t, j, c->keys[m]);
				return;
			}
			v = strcmp(f->name, "type") == 0
				? type_of(s, c->items[m], CONDITION_NAMES, (int)(sizeof CONDITION_NAMES / sizeof CONDITION_NAMES[0]), "condition", t)
				: is_number(c->items[m]) ? c->items[m]->i : (fail(s, "trigger %ld condition %d: \"%s\" is no integer", t, j, f->name), 0);
			put(at + f->offset, f->width, v);
		}
	}
}

static void actions(Spec *s, uint8_t *record, const KpJson *list, long t)
{
	int j, m;
	for (j = 0; j < kp_json_len(list); j++) {
		const KpJson *a = kp_json_at(list, j);
		const KpJson *script = kp_json_get(a, "script");
		uint8_t *at = record + ACTIONS_OFFSET + j * ACTION_SIZE;
		for (m = 0; m < kp_json_len(a); m++) {
			const Field *f;
			int64_t v;
			if (strcmp(a->keys[m], "script") == 0) continue;
			if (!(f = field(ACTION_FIELDS, (int)(sizeof ACTION_FIELDS / sizeof ACTION_FIELDS[0]), a->keys[m]))) {
				fail(s, "trigger %ld action %d: unknown field \"%s\"", t, j, a->keys[m]);
				return;
			}
			if (strcmp(f->name, "string") == 0 || strcmp(f->name, "wav") == 0) continue; /* below, in that order */
			if (strcmp(f->name, "type") == 0)
				v = type_of(s, a->items[m], ACTION_NAMES, (int)(sizeof ACTION_NAMES / sizeof ACTION_NAMES[0]), "action", t);
			else
				v = is_number(a->items[m]) ? a->items[m]->i : (fail(s, "trigger %ld action %d: \"%s\" is no integer", t, j, f->name), 0);
			put(at + f->offset, f->width, v);
		}
		/* a text's id is the next string's: the string's text before the WAV's, as the converter numbered them */
		put(at + 4, 4, string_of(s, kp_json_get(a, "string")));
		put(at + 8, 4, string_of(s, kp_json_get(a, "wav")));
		if (script) { /* Run AI Script's four-byte id, as a campaign TRIG holds it in the number field */
			if (kp_json_get(a, "number") || script->type != KP_JSON_STRING || script->count != 4) {
				fail(s, "trigger %ld action %d: script (four characters, no number)", t, j);
				return;
			}
			memcpy(at + 20, script->s, 4);
		}
	}
}

/* The spec's TRIG records (their texts kept in s for STR). */
static uint8_t *triggers(Spec *s, const KpJson *spec, size_t *size)
{
	const KpJson *list = kp_json_get(spec, "triggers");
	int n = kp_json_len(list), k, o;
	uint8_t *out = (uint8_t *)calloc((size_t)n + 1, TRIGGER_RECORD);
	*size = (size_t)n * TRIGGER_RECORD;
	if (!out) return fail(s, "out of memory"), (uint8_t *)NULL;
	for (k = 0; k < n && !s->failed; k++) {
		const KpJson *t = kp_json_at(list, k), *owners = kp_json_get(t, "owners");
		uint8_t *record = out + (size_t)k * TRIGGER_RECORD;
		if (kp_json_len(kp_json_get(t, "conditions")) > CONDITIONS || kp_json_len(kp_json_get(t, "actions")) > ACTIONS) {
			fail(s, "trigger %d: more than %d conditions or %d actions", k, CONDITIONS, ACTIONS);
			break;
		}
		conditions(s, record, kp_json_get(t, "conditions"), k);
		actions(s, record, kp_json_get(t, "actions"), k);
		kp_le_put32(record + EXECUTION_FLAGS, (uint32_t)kp_json_field_int(t, "flags", 0));
		if (!owners || owners->type != KP_JSON_ARRAY) fail(s, "trigger %d: no owners", k);
		for (o = 0; o < kp_json_len(owners); o++) {
			int64_t owner = kp_json_int(kp_json_at(owners, o), -1);
			if (owner < 0 || owner >= OWNER_COUNT) {
				fail(s, "trigger %d: owner %ld", k, (long)owner);
				break;
			}
			record[OWNERS + owner] = 1;
		}
	}
	if (s->failed) {
		free(out);
		return NULL;
	}
	return out;
}

static uint8_t *units(Spec *s, const KpJson *spec, size_t *size)
{
	const KpJson *list = kp_json_get(spec, "units");
	int n = kp_json_len(list), k, i;
	int *related = (int *)malloc(((size_t)n + 1) * sizeof *related);
	uint8_t *out = (uint8_t *)calloc((size_t)n + 1, UNIT_RECORD);
	*size = (size_t)n * UNIT_RECORD;
	if (!out || !related) {
		free(out);
		free(related);
		return fail(s, "out of memory"), (uint8_t *)NULL;
	}
	for (k = 0; k < n; k++) related[k] = -1;
	for (k = 0; k < n && !s->failed; k++) { /* "related": k links a unit and unit k both ways */
		const KpJson *r = kp_json_get(kp_json_at(list, k), "related");
		int other;
		if (!r) continue;
		other = (int)kp_json_int(r, -1);
		if (other < 0 || other >= n || other == k) fail(s, "spec unit %d: related unit %d is not another spec unit", k, other);
		else if (related[k] >= 0 && related[k] != other) fail(s, "spec unit %d: related to both %d and %d", k, related[k], other);
		else if (related[other] >= 0 && related[other] != k) fail(s, "spec unit %d: related to both %d and %d", other, related[other], k);
		else related[k] = other, related[other] = k;
	}
	for (k = 0; k < n && !s->failed; k++) {
		const KpJson *u = kp_json_at(list, k), *states = kp_json_get(u, "state");
		uint8_t *r = out + (size_t)k * UNIT_RECORD;
		uint16_t state = 0, valid = 0, relation = 0;
		int64_t type = need(s, u, "type", "spec unit", k);
		for (i = 0; i < kp_json_len(states); i++) {
			const char *name = kp_json_str(kp_json_at(states, i), "");
			size_t b;
			for (b = 0; b < sizeof UNIT_STATES / sizeof UNIT_STATES[0] && strcmp(UNIT_STATES[b].name, name) != 0; b++) {}
			if (b == sizeof UNIT_STATES / sizeof UNIT_STATES[0]) {
				fail(s, "spec unit %d: unknown state \"%s\"", k, name);
				break;
			}
			state = (uint16_t)(state | UNIT_STATES[b].bit);
		}
		for (i = 0; i < (int)(sizeof UNIT_PERCENTS / sizeof UNIT_PERCENTS[0]); i++)
			if (kp_json_get(u, UNIT_PERCENTS[i].name)) valid = (uint16_t)(valid | UNIT_PERCENTS[i].bit);
		if (kp_json_get(u, "resources")) valid |= RESOURCES_VALID;
		if (kp_json_get(u, "hangar")) valid |= HANGAR_VALID;
		if (related[k] >= 0) relation = type == NYDUS_CANAL ? RELATION_NYDUS : RELATION_ADDON;
		kp_le_put32(r, (uint32_t)(k + 1));
		kp_le_put16(r + 4, (uint32_t)need(s, u, "x", "spec unit", k));
		kp_le_put16(r + 6, (uint32_t)need(s, u, "y", "spec unit", k));
		kp_le_put16(r + 8, (uint32_t)type);
		kp_le_put16(r + 10, relation);
		kp_le_put16(r + 12, state);
		kp_le_put16(r + 14, valid);
		r[16] = (uint8_t)need(s, u, "owner", "spec unit", k);
		r[17] = (uint8_t)kp_json_field_int(u, "hp", 100);
		r[18] = (uint8_t)kp_json_field_int(u, "shields", 100);
		r[19] = (uint8_t)kp_json_field_int(u, "energy", 100);
		kp_le_put32(r + 20, (uint32_t)kp_json_field_int(u, "resources", 0));
		kp_le_put16(r + 24, (uint32_t)kp_json_field_int(u, "hangar", 0));
		kp_le_put16(r + 26, state);
		kp_le_put32(r + 28, 0);
		kp_le_put32(r + 32, related[k] >= 0 ? (uint32_t)(related[k] + 1) : 0);
	}
	free(related);
	if (s->failed) {
		free(out);
		return NULL;
	}
	return out;
}

/* THG2 with only its sprites, then the spec's doodad units. */
static uint8_t *doodads(Spec *s, const KpJson *spec, const uint8_t *thg2, size_t thg2_size, size_t *size)
{
	const KpJson *list = kp_json_get(spec, "doodad_units");
	int n = kp_json_len(list), k;
	size_t at, o = 0;
	uint8_t *out = (uint8_t *)malloc(thg2_size + (size_t)n * THG2_RECORD + 1);
	if (!out) return fail(s, "out of memory"), (uint8_t *)NULL;
	for (at = 0; at + THG2_RECORD <= thg2_size; at += THG2_RECORD)
		if (kp_le_u16(thg2 + at + 8) & THG2_SPRITE) {
			memcpy(out + o, thg2 + at, THG2_RECORD);
			o += THG2_RECORD;
		}
	for (k = 0; k < n; k++) {
		const KpJson *d = kp_json_at(list, k);
		int64_t flags = need(s, d, "flags", "spec doodad unit", k);
		if (flags & THG2_SPRITE) {
			fail(s, "spec doodad unit %d: flags %#lx make it a sprite", k, (unsigned long)flags);
			break;
		}
		kp_le_put16(out + o, (uint32_t)need(s, d, "type", "spec doodad unit", k));
		kp_le_put16(out + o + 2, (uint32_t)need(s, d, "x", "spec doodad unit", k));
		kp_le_put16(out + o + 4, (uint32_t)need(s, d, "y", "spec doodad unit", k));
		out[o + 6] = (uint8_t)need(s, d, "owner", "spec doodad unit", k);
		out[o + 7] = 0;
		kp_le_put16(out + o + 8, (uint32_t)flags);
		o += THG2_RECORD;
	}
	if (s->failed) {
		free(out);
		return NULL;
	}
	*size = o;
	return out;
}

/* STR with the trigger texts appended (ids count + 1, ...): u16 count, u16 offsets, the NUL-terminated strings. */
static uint8_t *strings(Spec *s, const uint8_t *str, size_t str_size, size_t *size)
{
	int count = str_size >= 2 ? (int)kp_le_u16(str) : 0, k;
	size_t shift = 2 * (size_t)s->text_count, data_at = 2 + 2 * (size_t)count, at, o;
	uint8_t *out;
	if (data_at > str_size) return fail(s, "STR is cut short"), (uint8_t *)NULL;
	at = 2 + 2 * ((size_t)count + (size_t)s->text_count) + (str_size - data_at);
	for (k = 0; k < s->text_count; k++) at += strlen(s->texts[k]) + 1;
	if (at > STR_LIMIT) return fail(s, "STR: the appended trigger texts pass the 16-bit offsets"), (uint8_t *)NULL;
	if (!(out = (uint8_t *)malloc(at + 1))) return fail(s, "out of memory"), (uint8_t *)NULL;
	*size = at;
	kp_le_put16(out, (uint32_t)(count + s->text_count));
	for (k = 0; k < count; k++) kp_le_put16(out + 2 + 2 * k, (uint32_t)(kp_le_u16(str + 2 + 2 * k) + shift));
	at = 2 + 2 * ((size_t)count + (size_t)s->text_count) + (str_size - data_at);
	for (k = 0; k < s->text_count; k++) {
		kp_le_put16(out + 2 + 2 * ((size_t)count + (size_t)k), (uint32_t)at);
		at += strlen(s->texts[k]) + 1;
	}
	o = 2 + 2 * ((size_t)count + (size_t)s->text_count);
	memcpy(out + o, str + data_at, str_size - data_at);
	o += str_size - data_at;
	for (k = 0; k < s->text_count; k++) {
		size_t n = strlen(s->texts[k]) + 1;
		memcpy(out + o, s->texts[k], n);
		o += n;
	}
	return out;
}

/* MRGN with the spec's records ("n": {"rect": [left, top, right, bottom], "name": id, "elevation": flags}, n 1-based). */
static uint8_t *locations(Spec *s, const KpJson *list, const uint8_t *mrgn, size_t mrgn_size)
{
	uint8_t *out = (uint8_t *)malloc(mrgn_size + 1);
	int k, i;
	if (!out) return fail(s, "out of memory"), (uint8_t *)NULL;
	memcpy(out, mrgn, mrgn_size);
	for (k = 0; k < kp_json_len(list); k++) {
		const KpJson *loc = list->items[k], *rect = kp_json_get(loc, "rect");
		long n = atol(list->keys[k]);
		uint8_t *r;
		if (n < 1 || (size_t)n > mrgn_size / LOCATION_RECORD || kp_json_len(rect) != 4) {
			fail(s, "location %s: MRGN holds %lu records, or no rect of four", list->keys[k], (unsigned long)(mrgn_size / LOCATION_RECORD));
			free(out);
			return NULL;
		}
		r = out + (size_t)(n - 1) * LOCATION_RECORD;
		for (i = 0; i < 4; i++) kp_le_put32(r + 4 * i, (uint32_t)kp_json_int(kp_json_at(rect, i), 0));
		kp_le_put16(r + 16, (uint32_t)kp_json_field_int(loc, "name", 0));
		kp_le_put16(r + 18, (uint32_t)kp_json_field_int(loc, "elevation", 0));
	}
	return out;
}

/* PTEC or PTEx with "researched"'s techs available and researched for their slots: available[12][n], researched[12][n],
 * default available[n], default researched[n], use default[12][n]. */
static uint8_t *researched(Spec *s, const KpJson *list, const uint8_t *payload, size_t size, int techs)
{
	uint8_t *out = (uint8_t *)malloc(size + 1);
	int k, i;
	if (!out) return fail(s, "out of memory"), (uint8_t *)NULL;
	memcpy(out, payload, size);
	for (k = 0; k < kp_json_len(list); k++) {
		long p = atol(list->keys[k]);
		const KpJson *ids = list->items[k];
		if (p < 0 || p >= PLAYER_SLOTS) {
			fail(s, "researched: player slot %s", list->keys[k]);
			free(out);
			return NULL;
		}
		for (i = 0; i < kp_json_len(ids); i++) {
			int64_t t = kp_json_int(kp_json_at(ids, i), techs);
			size_t a = (size_t)(p * techs + t), b = (size_t)(PLAYER_SLOTS * techs) + a,
				c = (size_t)(2 * PLAYER_SLOTS * techs + 2 * techs) + a;
			if (t < 0 || t >= techs) continue;
			if (c >= size) {
				fail(s, "researched: the tech section is cut short");
				free(out);
				return NULL;
			}
			out[a] = 1;
			out[b] = 1;
			out[c] = 0;
		}
	}
	return out;
}

static int controller(Spec *s, const char *kind)
{
	size_t k;
	for (k = 0; k < sizeof CONTROLLERS / sizeof CONTROLLERS[0]; k++)
		if (strcmp(CONTROLLERS[k].name, kind) == 0) return CONTROLLERS[k].controller;
	return fail(s, "players: unknown controller \"%s\"", kind);
}

uint8_t *kp_scenario_chk(const uint8_t *base, size_t base_size, const KpJson *spec, size_t *size, char *err, size_t err_size)
{
	const KpJson *players = kp_json_get(spec, "players"), *locs = kp_json_get(spec, "locations"),
		*research = kp_json_get(spec, "researched");
	uint8_t owners[PLAYER_SLOTS], *trig = NULL, *out = NULL;
	size_t trig_size = 0, at, o = 0;
	Spec s;
	int k, pass;
	memset(&s, 0, sizeof s);
	s.err = err;
	s.err_size = err_size;
	memset(owners, 0, sizeof owners);
	for (k = 0; k < kp_json_len(players); k++) {
		long slot = atol(players->keys[k]);
		int c = controller(&s, kp_json_str(players->items[k], ""));
		if (slot < 0 || slot >= PLAYER_SLOTS) fail(&s, "players: slot %s", players->keys[k]);
		else if (c >= 0) owners[slot] = (uint8_t)c;
	}
	for (at = 0; at + CHUNK_HEADER <= base_size; at += CHUNK_HEADER + (size_t)kp_le_u32(base + at + 4)) /* the first STR */
		if (memcmp(base + at, "STR ", 4) == 0) {
			s.string_count = (int)kp_le_u16(base + at + CHUNK_HEADER);
			break;
		}
	if (!s.failed) trig = triggers(&s, spec, &trig_size);
	/* two passes over the chunks (0x4c1170's enumeration: a header where 8 bytes remain, the next past its payload):
	 * the size, then the bytes */
	for (pass = 0; pass < 2 && !s.failed; pass++) {
		o = 0;
		for (at = 0; at + CHUNK_HEADER <= base_size && !s.failed;) {
			const uint8_t *tag = base + at, *payload = base + at + CHUNK_HEADER;
			uint32_t declared = kp_le_u32(base + at + 4);
			size_t n = declared <= base_size - at - CHUNK_HEADER ? declared : base_size - at - CHUNK_HEADER, size_out = n;
			uint8_t *made = NULL;
			const uint8_t *data = payload;
			size_t i;
			if (declared & 0x80000000u) {
				fail(&s, "chunk %.4s has a negative size (unsupported)", (const char *)tag);
				break;
			}
			if (memcmp(tag, "OWNR", 4) == 0) {
				data = owners;
				size_out = sizeof owners;
			} else if (memcmp(tag, "UNIT", 4) == 0) {
				data = made = units(&s, spec, &size_out);
			} else if (memcmp(tag, "THG2", 4) == 0) {
				data = made = doodads(&s, spec, payload, n, &size_out);
			} else if (memcmp(tag, "TRIG", 4) == 0) {
				data = trig;
				size_out = trig_size;
			} else if (memcmp(tag, "STR ", 4) == 0 && s.text_count) {
				data = made = strings(&s, payload, n, &size_out);
			} else if (memcmp(tag, "MRGN", 4) == 0 && locs) {
				data = made = locations(&s, locs, payload, n);
			} else if (memcmp(tag, "MBRF", 4) == 0) {
				size_out = 0;
			} else if (research) {
				for (i = 0; i < sizeof TECH_SECTIONS / sizeof TECH_SECTIONS[0]; i++)
					if (memcmp(tag, TECH_SECTIONS[i].tag, 4) == 0) data = made = researched(&s, research, payload, n, TECH_SECTIONS[i].techs);
			}
			if (!s.failed && pass == 1) {
				memcpy(out + o, tag, 4);
				kp_le_put32(out + o + 4, (uint32_t)size_out);
				if (size_out) memcpy(out + o + CHUNK_HEADER, data, size_out);
			}
			o += CHUNK_HEADER + size_out;
			free(made);
			at += CHUNK_HEADER + (size_t)declared;
			if (at < CHUNK_HEADER) break; /* past the address space */
		}
		if (pass == 0 && !s.failed && !(out = (uint8_t *)malloc(o + 1))) fail(&s, "out of memory");
	}
	free(trig);
	if (s.failed) {
		free(out);
		return NULL;
	}
	*size = o;
	return out;
}
