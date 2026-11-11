/* Mission container loader and the mission from its scenario (kp_mission.h). */
#include "kp_mission.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kp_chk.h"
#include "kp_derive.h"

/* Derived sections and the producer versions this build reads, and WAVS v2. */
const char *const KP_TILESET_NAMES[KP_TILESETS] = { "badlands", "platform", "install", "ashworld", "jungle", "desert", "ice",
	"twilight" };

static const KpKpfSectionExpect SECTIONS[] = {
	{ "WAVS", 2, NULL, 0 },
	{ "WALK", 0, "WLK1", 1 },
	{ "RGNS", 0, "RGN1", 2 },
	{ "CONT", 0, "CNT1", 1 },
	{ "PACK", 0, "PAK1", 1 },
};

/* The parts parsed into a KpMission besides the raw CHK payloads and WALK, in this order. */
enum { PART_META, PART_RULE, PART_WAVS, PART_RGNS, PART_CONT, PART_PACK, PART_COUNT };

static int fail(KpError *err, const char *name, const char *msg)
{
	snprintf(err->msg, sizeof err->msg, "mission: %s: %s", name && name[0] ? name : "<data>", msg);
	return -1;
}

static int section(const KpKpf *c, const char *fourcc, KpBytes *out, KpError *err)
{
	const KpKpfSection *s = kp_kpf_require(c, fourcc, err);
	if (!s) return -1;
	out->data = s->data;
	out->size = s->size;
	return 0;
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

/* The parsed parts of a mission, from its container's sections or built from its CHK (kp_mission_from_files). */
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

int kp_mission_load(KpMission *m, const uint8_t *data, size_t size, const char *name, const KpKpfExpect *expect,
	KpError *err)
{
	uint8_t *copy = (uint8_t *)malloc(size ? size : 1);
	if (!copy) {
		memset(m, 0, sizeof *m);
		return fail(err, name, "out of memory");
	}
	memcpy(copy, data, size);
	return kp_mission_take(m, copy, size, name, expect, err);
}

int kp_mission_take(KpMission *m, uint8_t *data, size_t size, const char *name, const KpKpfExpect *expect,
	KpError *err)
{
	static const char *const PARTS[PART_COUNT] = { "META", "RULE", "WAVS", "RGNS", "CONT", "PACK" };
	KpKpf c;
	KpKpfExpect want;
	KpBytes part[PART_COUNT];
	int k;
	memset(m, 0, sizeof *m);
	if (!data) return fail(err, name, "no file");
	m->blob = data;
	m->size = size;
	if (kp_kpf_parse(&c, m->blob, size, name, 0, err) != 0) {
		kp_mission_free(m);
		return -1;
	}
	memset(&want, 0, sizeof want);
	if (expect) want = *expect;
	want.kind = KP_MISSION_KIND;
	want.schema_version = KP_MISSION_SCHEMA;
	want.sections = SECTIONS;
	want.section_count = (int)(sizeof SECTIONS / sizeof SECTIONS[0]);
	if (kp_kpf_check(&c, &want, err) != 0) goto failed;
	for (k = 0; k < PART_COUNT; k++)
		if (section(&c, PARTS[k], &part[k], err)) goto failed;
	if (section(&c, "MTXM", &m->tiles, err) || section(&c, "MASK", &m->mask, err)
		|| section(&c, "SPRT", &m->sprites, err) || section(&c, "UNIT", &m->units, err)
		|| section(&c, "LOCS", &m->locations, err) || section(&c, "STRS", &m->strings, err)
		|| section(&c, "TRIG", &m->triggers, err) || section(&c, "MBRF", &m->briefing, err)
		|| section(&c, "UPRP", &m->properties, err) || section(&c, "WALK", &m->walk, err)) {
		goto failed;
	}
	if (assemble(m, part, name, err) != 0) goto failed;
	kp_kpf_free(&c);
	return 0;
failed:
	kp_kpf_free(&c);
	kp_mission_free(m);
	return -1;
}

/* --- the mission from its scenario (ADR 0018, stage D4; converter/missions.py, kpf/kpm.py build) --- */

#define META_SIZE 68 /* kpm.META <4H12s12s20s2H8sB3x */
#define TRIGGER_SIZE 2400
#define CONDITION_COUNT 16
#define CONDITION_SIZE 20
#define CONDITION_TYPE 0x0f
#define CONDITION_FLAGS 0x11
#define ACTION_COUNT 64
#define ACTIONS_OFFSET 320
#define ACTION_SIZE 32
#define ACTION_LOCATION 0x00
#define ACTION_WAV 0x08
#define ACTION_NUMBER 0x14
#define ACTION_TYPE 0x1a
#define ACTION_FLAGS 0x1c
#define ENTRY_IGNORED 0x02
/* the condition and action tables of EvaluateTriggerConditions 0x4bded0 and the runner 0x4bdf10 (atlas P40) */
#define CONDITION_TYPES 24
#define ACTION_TYPES 60
#define CONDITION_OPPONENTS 14
#define ACTION_MOVE_UNIT 39
#define ACTION_ORDER 46
#define ACTION_MODIFY_RESOURCE_AMOUNT 52
#define ACTION_MODIFY_HANGAR_COUNT 53
#define LOCATIONS 255
#define WAVS_ENTRY 12
#define STRING_IDS 65536

/* converter/analyze/triggers.py validate: what the original's tables would index past (a reachable condition above type
 * 23, an action above 59, an Order past the 255 locations, a Move Unit to location 256) and what the V1 profile has no
 * handler for (Opponents; Modify Unit Resource Amount and Hangar Count). Entries the runners never reach are left. */
static int validate_triggers(const uint8_t *trig, uint32_t size, char *err, size_t err_size)
{
	uint32_t t, j;
	if (size % TRIGGER_SIZE) {
		snprintf(err, err_size, "TRIG: trigger payload of %u bytes is not a multiple of %d", (unsigned)size, TRIGGER_SIZE);
		return -1;
	}
	for (t = 0; t < size / TRIGGER_SIZE; t++) {
		const uint8_t *base = trig + (size_t)t * TRIGGER_SIZE;
		uint32_t end = ACTION_COUNT;
		for (j = 0; j < CONDITION_COUNT; j++) { /* the evaluated ones: ignored skipped, the first type 0 ends them */
			const uint8_t *c = base + j * CONDITION_SIZE;
			if (c[CONDITION_FLAGS] & ENTRY_IGNORED) continue;
			if (c[CONDITION_TYPE] == 0) break;
			if (c[CONDITION_TYPE] >= CONDITION_TYPES) {
				snprintf(err, err_size, "TRIG: trigger %u condition %u: type %u (the table has %d)", (unsigned)t,
					(unsigned)j, (unsigned)c[CONDITION_TYPE], CONDITION_TYPES);
				return -1;
			}
			if (c[CONDITION_TYPE] == CONDITION_OPPONENTS) {
				snprintf(err, err_size, "TRIG: trigger %u condition %u: opponents is outside the V1 profile", (unsigned)t,
					(unsigned)j);
				return -1;
			}
		}
		for (j = 0; j < ACTION_COUNT && end == ACTION_COUNT; j++) { /* the first type 0 not ignored ends them */
			const uint8_t *a = base + ACTIONS_OFFSET + j * ACTION_SIZE;
			if (a[ACTION_TYPE] == 0 && !(a[ACTION_FLAGS] & ENTRY_IGNORED)) end = j;
		}
		for (j = 0; j < end; j++) {
			const uint8_t *a = base + ACTIONS_OFFSET + j * ACTION_SIZE;
			uint32_t kind = a[ACTION_TYPE], number = kp_le_u32(a + ACTION_NUMBER);
			if (kind == 0 || (a[ACTION_FLAGS] & ENTRY_IGNORED)) continue;
			if (kind >= ACTION_TYPES) {
				snprintf(err, err_size, "TRIG: trigger %u action %u: type %u (the table has %d)", (unsigned)t, (unsigned)j,
					(unsigned)kind, ACTION_TYPES);
				return -1;
			}
			if (kind == ACTION_MODIFY_RESOURCE_AMOUNT || kind == ACTION_MODIFY_HANGAR_COUNT) {
				/* TODO: when custom and UMS maps come into scope, implement them in kp_triggers.c (converter's
				 * triggers.UNSUPPORTED_ACTIONS goes with them) */
				snprintf(err, err_size, "TRIG: trigger %u action %u: %s is outside the V1 profile", (unsigned)t,
					(unsigned)j,
					kind == ACTION_MODIFY_RESOURCE_AMOUNT ? "modify_unit_resource_amount" : "modify_unit_hangar_count");
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

/* GetMapString 0x445670 (kpm.map_string) over STR's bytes: the string's bytes and 1, 0 for none (index 0, or past the
 * count: empty), -1 when it is not terminated inside STR */
static int map_string_in(const uint8_t *strs, size_t strs_size, uint32_t index, const uint8_t **text, size_t *n)
{
	uint32_t offset;
	const uint8_t *end;
	*text = NULL;
	*n = 0;
	index &= 0xffff;
	if (index == 0) return 0;
	if (strs_size < 2) return -1;
	if (index - 1 >= kp_le_u16(strs)) return 0;
	if (2 * (size_t)index + 2 > strs_size) return -1;
	offset = kp_le_u16(strs + 2 * index);
	if (offset >= strs_size || !(end = (const uint8_t *)memchr(strs + offset, 0, strs_size - offset))) return -1;
	*text = strs + offset;
	*n = (size_t)(end - *text);
	return 1;
}

static int map_string(const KpChkSection *strs, uint32_t index, const uint8_t **text, size_t *n)
{
	return map_string_in(strs->data, strs->size, index, text, n);
}

/* The path a WAV action builds from its string (the WAVS builder's and the players'): <directory>\<string>. 1 with
 * the path in out, 0 for no string or an empty one, -1 for a string not terminated inside STR or a path too long. */
static int wav_path(const uint8_t *strs, size_t strs_size, const char *directory, uint32_t string_id, char *out,
	size_t size)
{
	const uint8_t *text;
	size_t len;
	int s = map_string_in(strs, strs_size, string_id, &text, &len);
	if (s <= 0 || len == 0) return s;
	if (strlen(directory) + 1 + len >= size) return -1;
	snprintf(out, size, "%s\\%.*s", directory, (int)len, (const char *)text);
	return 1;
}

int kp_mission_sound_path(const KpMission *m, const char *directory, uint32_t string_id, char *out, size_t size)
{
	return wav_path(m->strings.data, m->strings.size, directory, string_id, out, size);
}

/* WAVS (kpm.mission_sounds, build): u16 count; per WAV string its id, its index among the files found
 * (KP_MISSION_NO_SOUND: missing, unreadable or refused by the original's parser), the data chunk's length and
 * nAvgBytesPerSec. malloc'd, NULL with the reason. */
static uint8_t *mission_wavs(const KpFiles *f, const char *directory, const KpChkSection *strs,
	const KpChkSection *trig, const KpChkSection *mbrf, size_t *size, KpError *err)
{
	uint8_t *seen = (uint8_t *)malloc(STRING_IDS), *out = NULL;
	uint16_t *ids = (uint16_t *)malloc(STRING_IDS * sizeof *ids);
	int n, k, index = 0;
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
		int found = 0, s = map_string(strs, ids[k], &text, &len);
		if (s < 0) {
			snprintf(err->msg, sizeof err->msg, "mission: %s: map string %u is not terminated inside STR", directory,
				(unsigned)ids[k]);
			goto failed;
		}
		if (s > 0 && len > 0) { /* the path the WAV actions build */
			if (wav_path(strs->data, strs->size, directory, ids[k], path, sizeof path) < 0) {
				snprintf(err->msg, sizeof err->msg, "mission: %s: WAV string %u is too long", directory, (unsigned)ids[k]);
				goto failed;
			}
			if ((found = kp_files_wav(f, path, &w, err)) == -2) goto failed;
		}
		kp_le_put16(e, ids[k]);
		kp_le_put16(e + 2, found == 1 ? (uint32_t)index : KP_MISSION_NO_SOUND);
		kp_le_put32(e + 4, found == 1 ? w.data_length : 0);
		kp_le_put32(e + 8, found == 1 ? w.avg_bytes_per_sec : 0);
		index += found == 1;
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

/* PACK: the mission's resource packs, the profile's compiled MPAK (each campaign mission's packs by its lowercased
 * directory, recorded from the reference install: converter/missions.py write_packs); NULL when it has none */
static const uint8_t *mission_packs(const KpDefsProfile *p, const char *directory, size_t *size)
{
	const KpDefsTable *t = p ? kp_defs_table(p, "MPAK") : NULL;
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

int kp_mission_from_files(KpMission *m, const KpFiles *f, const char *directory, KpError *err)
{
	/* the sections the mission reads; the loader's tables require them all */
	static const char *const NEEDED[] = { "VER ", "DIM ", "ERA ", "OWNR", "SIDE", "FORC", "SPRP", "MTXM", "MASK", "THG2",
		"UNIT", "MRGN", "STR ", "TRIG", "MBRF", "UPRP" };
	enum { VER, DIM, ERA, OWNR, SIDE, FORC, SPRP, MTXM, MASK, THG2, UNIT, MRGN, STR, TRIG, MBRF, UPRP, NEEDED_COUNT };
	char path[KP_ERROR_SIZE], why[KP_ERROR_SIZE];
	KpChk chk;
	const KpChkSection *s[NEEDED_COUNT], *colr;
	KpDerived derived;
	KpBytes part[PART_COUNT];
	uint8_t meta[META_SIZE], *chk_bytes = NULL, *rules = NULL, *wavs = NULL, *cv5 = NULL, *vf4 = NULL, *blob;
	const uint8_t *packs;
	size_t chk_size, rules_size = 0, wavs_size = 0, cv5_size, vf4_size, pack_size = 0, total, at, offsets[NEEDED_COUNT];
	uint32_t w, h;
	int i, k, rc = -1;
	memset(m, 0, sizeof *m);
	memset(&derived, 0, sizeof derived);
	snprintf(path, sizeof path, "%s\\staredit\\scenario.chk", directory);
	if (!(chk_bytes = kp_files_need(f, path, &chk_size, err))) return -1;
	if (kp_chk_read(&chk, chk_bytes, chk_size, f->profile, 1, why, sizeof why) != 0) {
		fail(err, directory, why);
		goto done;
	}
	for (i = 0; i < NEEDED_COUNT; i++) {
		if (!(s[i] = kp_chk_section(&chk, NEEDED[i]))) {
			snprintf(why, sizeof why, "the scenario has no '%s' section", NEEDED[i]);
			fail(err, directory, why);
			goto done;
		}
		offsets[i] = (size_t)(s[i]->data - chk_bytes);
	}
	colr = kp_chk_section(&chk, "COLR");
	w = kp_le_u16(s[DIM]->data);
	h = kp_le_u16(s[DIM]->data + 2);
	if (s[MTXM]->size != (uint64_t)2 * w * h || s[MASK]->size != (uint64_t)w * h) {
		/* the native loader accepts other lengths; no campaign map has one */
		snprintf(why, sizeof why, "MTXM/MASK sizes %u/%u do not match %ux%u", (unsigned)s[MTXM]->size,
			(unsigned)s[MASK]->size, (unsigned)w, (unsigned)h);
		fail(err, directory, why);
		goto done;
	}
	if (validate_triggers(s[TRIG]->data, s[TRIG]->size, why, sizeof why) != 0) {
		fail(err, directory, why);
		goto done;
	}
	/* META */
	memset(meta, 0, sizeof meta);
	meta_put(meta, s[VER], 2);
	meta_put(meta + 2, s[ERA], 2);
	meta_put(meta + 4, s[DIM], 4);
	meta_put(meta + 8, s[OWNR], 12);
	meta_put(meta + 20, s[SIDE], 12);
	meta_put(meta + 32, s[FORC], 20); /* the native FORC zero-fills the tail */
	meta_put(meta + 52, s[SPRP], 4);
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
	if (!(wavs = mission_wavs(f, directory, s[STR], s[TRIG], s[MBRF], &wavs_size, err))) goto done;
	{ /* WALK, RGNS and CONT from the tileset of ERA & 7 (kpm.add_terrain_derived) */
		const char *tileset = KP_TILESET_NAMES[kp_le_u16(s[ERA]->data) & 7];
		snprintf(path, sizeof path, "tileset\\%s.cv5", tileset);
		if (!(cv5 = kp_files_need(f, path, &cv5_size, err))) goto done;
		snprintf(path, sizeof path, "tileset\\%s.vf4", tileset);
		if (!(vf4 = kp_files_need(f, path, &vf4_size, err))) goto done;
		if (kp_derive_terrain(w, h, s[MTXM]->data, cv5, cv5_size, vf4, vf4_size, &derived, why, sizeof why) != 0) {
			fail(err, directory, why);
			goto done;
		}
	}
	if (!(packs = mission_packs(f->profile, directory, &pack_size))) {
		fail(err, directory, "the profile has no resource packs for this mission (MPAK)");
		goto done;
	}
	/* one blob: the scenario (the raw payloads point into it), then the parts built here */
	total = chk_size + META_SIZE + rules_size + wavs_size + derived.walk_size + derived.regions_size
		+ derived.contours_size + pack_size;
	if (!(blob = (uint8_t *)realloc(chk_bytes, total))) goto oom;
	chk_bytes = NULL;
	m->blob = blob;
	m->size = total;
	at = chk_size;
#define APPEND(out, bytes, n) \
	do { \
		memcpy(blob + at, (bytes), (n)); \
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
#undef APPEND
#define RAW(field, index) (m->field.data = blob + offsets[index], m->field.size = s[index]->size)
	RAW(tiles, MTXM), RAW(mask, MASK), RAW(sprites, THG2), RAW(units, UNIT), RAW(locations, MRGN);
	RAW(strings, STR), RAW(triggers, TRIG), RAW(briefing, MBRF), RAW(properties, UPRP);
#undef RAW
	rc = assemble(m, part, directory, err);
	goto done;
oom:
	fail(err, directory, "out of memory");
done:
	if (rc != 0) kp_mission_free(m);
	free(chk_bytes);
	free(rules);
	free(wavs);
	free(cv5);
	free(vf4);
	kp_derived_free(&derived);
	return rc;
}

void kp_mission_free(KpMission *m)
{
	free(m->rules);
	free(m->sounds);
	free(m->blob);
	memset(m, 0, sizeof *m);
}
