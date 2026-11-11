/* The data loaders of the native core over the converter's fixtures (plan phase S, stage S7): the container reader
 * (kp_kpf.h), dat.kpd (kp_dat.h), the mission container (kp_mission.h), grpmeta.kpd (kp_grpmeta.h), iscript.kpd and
 * aiscript.kpd (kp_iscript.h, kp_aiscript.h), the UMS override effects (kp_rules.c), the tileset container and the
 * terrain import (kp_present's kp_tileset_load, kp_terrain_import) and palettes.kpd (kp_palettes_load). The fixtures
 * and their expectations are written by `python3 -m converter.kpf.fixtures` (tests/fixtures/generated/kpf, expect.json:
 * the Python twins' values); the tests are skipped without them. The mission strings are read through GetMapString
 * (kp_map_string); the text engine (kp_ui_text.h) interprets their control codes. The ImportMapTiles flags of the
 * game's own import (kp_start.c) are the original's in test_initial_state's "tiles" category; here they are checked
 * from the tileset loader's CV5 and megatile flags. */
#include "kp_test.h"

#include <string.h>

#include "kp_defs.h"
#include "kp_present.h"
#include "../../native/kp_core/src/core/kp_aiscript.h"
#include "../../native/kp_core/src/core/kp_start.h"

#define DIR "tests/fixtures/generated/kpf/"
#define EXPECT DIR "expect.json"

static const char *const FIXTURES[] = { EXPECT, NULL };

/* Whether the fixtures exist (the generator's output; a SKIP line without them). */
static int ready(KpT *T, const char *label) { return kp_t_ready(T, label, FIXTURES); }

static const KpJson *expect(KpT *T) { return kp_t_golden(T, EXPECT); }

/* An expectation's member, failing when absent. */
static const KpJson *member(KpT *T, const KpJson *e, const char *key)
{
	const KpJson *v = kp_json_get(e, key);
	kp_t_ok(T, v != NULL, "expect.json has no %s", key);
	return v;
}

static int64_t at_int(const KpJson *array, int k) { return kp_json_int(kp_json_at(array, k), KP_NIL); }

/* A fixture's bytes. */
static uint8_t *fixture(KpT *T, const char *name, size_t *size)
{
	char path[256];
	snprintf(path, sizeof path, DIR "%s", name);
	return kp_t_read(T, path, size);
}

/* A copy of the bytes released at the end (patched variants). */
static uint8_t *copy_of(KpT *T, const uint8_t *data, size_t size)
{
	uint8_t *c = (uint8_t *)malloc(size ? size : 1);
	kp_t_ok(T, c != NULL, "out of memory");
	kp_t_defer(T, free, c);
	memcpy(c, data, size);
	return c;
}

/* T.raises(fn, substring): the call failed with a message containing `want`. */
static void raised(KpT *T, int r, const KpError *err, const char *want, const char *what)
{
	kp_t_ok(T, r != 0, "%s: no error", what);
	kp_t_ok(T, strstr(err->msg, want) != NULL, "%s: \"%s\" lacks \"%s\"", what, err->msg, want);
}

static void free_kpf(void *p)
{
	kp_kpf_free((KpKpf *)p);
	free(p);
}

/* A parsed container released at the end. */
static KpKpf *parse(KpT *T, const uint8_t *data, size_t size, const char *name, int verify)
{
	KpKpf *c = (KpKpf *)calloc(1, sizeof *c);
	KpError err;
	kp_t_ok(T, c != NULL, "out of memory");
	if (kp_kpf_parse(c, data, size, name, verify, &err) != 0) {
		free(c);
		kp_t_fail(T, "%s", err.msg);
	}
	kp_t_defer(T, free_kpf, c);
	return c;
}

static const KpKpfSection *section(KpT *T, const KpKpf *c, const char *fourcc)
{
	const KpKpfSection *s = kp_kpf_find(c, fourcc);
	kp_t_ok(T, s != NULL, "no section %s", fourcc);
	return s;
}

/* --- the container --- */

static void kpf_header(KpT *T)
{
	const KpJson *e, *hash;
	size_t size;
	uint8_t *data;
	KpKpf *c;
	if (!ready(T, "test_data")) return;
	e = expect(T);
	data = fixture(T, "basic.kpd", &size);
	kp_t_eq(T, (int64_t)size, kp_json_int(member(T, e, "basic_size"), -1), "the file's size");
	c = parse(T, data, size, "basic.kpd", 1);
	kp_t_eq_str(T, c->kind, "TEST", "kind");
	kp_t_eq(T, c->schema_version, 3, "schema version");
	kp_t_eq(T, c->converter_version, 7, "converter version");
	kp_t_eq_str(T, c->profile_id, "bw-1.17.0.1", "profile");
	hash = member(T, e, "source_hash");
	kp_t_ok(T, hash->count == 32 && memcmp(c->source_hash, hash->s, 32) == 0, "the source hash");
	kp_t_eq(T, c->section_count, 5, "sections");
	kp_t_eq_str(T, c->sections[0].fourcc, "INTS", "the first section");
	kp_t_eq_str(T, c->sections[4].fourcc, "WALK", "the last section");
}

static void kpf_section_payloads(KpT *T)
{
	const KpJson *ints;
	size_t size;
	uint8_t *data;
	KpKpf *c;
	const KpKpfSection *s;
	int k;
	if (!ready(T, "test_data")) return;
	ints = member(T, expect(T), "ints");
	data = fixture(T, "basic.kpd", &size);
	c = parse(T, data, size, "basic.kpd", 0);
	s = section(T, c, "INTS");
	kp_t_eq(T, s->size, 4 * kp_json_len(ints), "INTS size");
	for (k = 0; k < kp_json_len(ints); k++) kp_t_eq(T, kp_le_u32(s->data + 4 * k), at_int(ints, k), "INTS[%d]", k + 1);
	s = section(T, c, "BYTE");
	kp_t_eq(T, s->size, 256, "BYTE size");
	for (k = 0; k < 256; k++) kp_t_eq(T, s->data[k], k, "BYTE[%d]", k);
	kp_t_eq(T, s->schema_version, 2, "BYTE schema version");
	s = section(T, c, "ODD ");
	kp_t_ok(T, s->size == 3 && memcmp(s->data, "abc", 3) == 0, "ODD holds abc");
	kp_t_eq(T, section(T, c, "EMPT")->size, 0, "EMPT is empty");
	kp_t_ok(T, kp_kpf_find(c, "WALK") != NULL && kp_kpf_find(c, "NONE") == NULL, "has WALK, not NONE");
}

static void kpf_derived_metadata(KpT *T)
{
	size_t size;
	uint8_t *data;
	const KpKpfSection *w;
	if (!ready(T, "test_data")) return;
	data = fixture(T, "basic.kpd", &size);
	w = section(T, parse(T, data, size, "basic.kpd", 0), "WALK");
	kp_t_eq(T, w->class_, KP_KPF_DERIVED, "class");
	kp_t_eq_str(T, w->algorithm_id, "WLK1", "algorithm");
	kp_t_eq(T, w->algorithm_version, 5, "algorithm version");
	kp_t_eq(T, w->flags, 0x8001, "flags");
	kp_t_eq(T, (int64_t)sizeof w->inputs_hash, 16, "inputs hash");
}

/* One Container:check expectation: refused with a message containing `want` (NULL: accepted). */
static void check_one(KpT *T, const KpKpf *c, const KpKpfExpect *x, const char *want, const char *what)
{
	KpError err;
	int r = kp_kpf_check(c, x, &err);
	if (!want) kp_t_ok(T, r == 0, "%s: %s", what, err.msg);
	else raised(T, r, &err, want, what);
}

static void kpf_check_expectations(KpT *T)
{
	size_t size;
	uint8_t *data, zeros[32];
	KpKpf *c;
	KpKpfExpect x;
	KpKpfSectionExpect s[2];
	KpError err;
	if (!ready(T, "test_data")) return;
	data = fixture(T, "basic.kpd", &size);
	c = parse(T, data, size, "basic.kpd", 0);
	memset(&x, 0, sizeof x);
	memset(s, 0, sizeof s);
	x.kind = "TEST";
	x.schema_version = 3;
	x.profile_id = "bw-1.17.0.1";
	s[0].fourcc = "WALK";
	s[0].algorithm_id = "WLK1";
	s[0].algorithm_version = 5;
	s[1].fourcc = "BYTE";
	s[1].schema_version = 2;
	x.sections = s;
	x.section_count = 2;
	check_one(T, c, &x, NULL, "the matching expectations");
	memset(&x, 0, sizeof x);
	x.schema_version = 4;
	check_one(T, c, &x, "regenerate", "another schema version");
	memset(&x, 0, sizeof x);
	x.profile_id = "bw-1.16.1";
	check_one(T, c, &x, "profile", "another profile");
	memset(&x, 0, sizeof x);
	memset(zeros, 0, sizeof zeros);
	x.source_hash = zeros;
	check_one(T, c, &x, "different data set", "another source hash");
	memset(&x, 0, sizeof x);
	s[0].algorithm_version = 6;
	x.sections = s;
	x.section_count = 1;
	check_one(T, c, &x, "WLK1 v5", "another algorithm version");
	memset(s, 0, sizeof s);
	s[0].fourcc = "INTS";
	s[0].algorithm_id = "X";
	s[0].algorithm_version = 1;
	check_one(T, c, &x, "not derived", "a canonical section expected derived");
	raised(T, kp_kpf_require(c, "NONE", &err) == NULL ? -1 : 0, &err, "missing section NONE", "a missing section");
}

static void expect_parse_error(KpT *T, const uint8_t *data, size_t size, int verify, const char *want)
{
	KpKpf c;
	KpError err;
	int r = kp_kpf_parse(&c, data, size, "x", verify, &err);
	if (r == 0) kp_kpf_free(&c);
	raised(T, r, &err, want, want);
}

static void kpf_rejects_bad_files(KpT *T)
{
	size_t size, pos;
	uint8_t *data, *bad;
	const KpKpfSection *w;
	KpKpf c;
	KpError err;
	if (!ready(T, "test_data")) return;
	data = fixture(T, "basic.kpd", &size);
	expect_parse_error(T, (const uint8_t *)"short", 5, 0, "shorter");
	bad = copy_of(T, data, size);
	memcpy(bad, "XXXX", 4);
	expect_parse_error(T, bad, size, 0, "bad magic");
	expect_parse_error(T, data, size - 4, 0, "truncated");
	bad = copy_of(T, data, size);
	bad[8] = 2;
	expect_parse_error(T, bad, size, 0, "version 2");
	/* the first section's offset misaligned, then past the file: the message names the section */
	bad = copy_of(T, data, size);
	pos = kp_le_u32(bad + 104) + 8;
	bad[pos] = (uint8_t)(bad[pos] + 1);
	expect_parse_error(T, bad, size, 0, "section INTS out of bounds or misaligned");
	bad = copy_of(T, data, size);
	bad[pos + 3] = 0x7f;
	expect_parse_error(T, bad, size, 0, "section INTS out of bounds or misaligned");
	/* the last payload byte of WALK flipped: only detected with the checksums verified */
	w = section(T, parse(T, data, size, "x", 0), "WALK");
	pos = w->offset + w->size - 1;
	bad = copy_of(T, data, size);
	bad[pos] = (uint8_t)(bad[pos] + 1);
	kp_t_ok(T, kp_kpf_parse(&c, bad, size, "x", 0, &err) == 0, "unverified: %s", err.msg);
	kp_kpf_free(&c);
	expect_parse_error(T, bad, size, 1, "checksum");
}

static void kpf_large_file(KpT *T)
{
	const KpJson *e, *tail;
	size_t size;
	uint8_t *data;
	const KpKpfSection *blob;
	if (!ready(T, "test_data")) return;
	e = expect(T);
	data = fixture(T, "large.kpd", &size);
	kp_t_eq(T, (int64_t)size, kp_json_int(member(T, e, "large_size"), -1), "the file's size");
	blob = section(T, parse(T, data, size, "large.kpd", 0), "BLOB");
	kp_t_eq(T, blob->size, kp_json_int(member(T, e, "large_payload_size"), -1), "BLOB size");
	tail = member(T, e, "large_tail");
	kp_t_ok(T, tail->count == 16 && blob->size >= 16 && memcmp(blob->data + blob->size - 16, tail->s, 16) == 0,
		"the payload's last 16 bytes");
	kp_t_eq(T, blob->crc32, kp_json_int(member(T, e, "large_crc32"), -1), "BLOB crc32");
	kp_t_eq(T, kp_crc32(blob->data, blob->size), blob->crc32, "the payload's CRC");
}

static void kpf_crc32_known(KpT *T)
{
	kp_t_eq(T, kp_crc32((const uint8_t *)"", 0), 0, NULL);
	kp_t_eq(T, kp_crc32((const uint8_t *)"123456789", 9), 0xCBF43926u, NULL);
	kp_t_eq(T, kp_crc32((const uint8_t *)"xx123456789yy" + 2, 9), 0xCBF43926u, "a byte range");
	kp_t_eq(T, kp_crc32_update(kp_crc32((const uint8_t *)"1234", 4), (const uint8_t *)"56789", 5), 0xCBF43926u,
		"continued");
}

/* --- dat.kpd --- */

static void free_dat(void *p)
{
	kp_dat_free((KpDat *)p);
	free(p);
}

static KpDat *load_dat(KpT *T, const uint8_t *data, size_t size, const KpKpfExpect *x)
{
	KpDat *d = (KpDat *)calloc(1, sizeof *d);
	KpError err;
	kp_t_ok(T, d != NULL, "out of memory");
	if (kp_dat_load(d, data, size, "dat.kpd", x, &err) != 0) {
		free(d);
		kp_t_fail(T, "%s", err.msg);
	}
	kp_t_defer(T, free_dat, d);
	return d;
}

static const KpDatColumn *dat_column(KpT *T, const KpDat *d, const char *table, const char *column)
{
	const KpDatColumn *c = kp_dat_column(d, table, column);
	kp_t_ok(T, c != NULL, "no column %s.%s", table, column);
	return c;
}

static int64_t records_of(const KpDat *d, const char *table)
{
	int k;
	for (k = 0; k < d->table_count; k++) {
		if (strcmp(d->tables[k].name, table) == 0) return d->tables[k].records;
	}
	return -1;
}

static void dat_columns_match_python(KpT *T)
{
	const KpJson *e, *probes, *hash;
	KpKpfExpect x;
	size_t size;
	uint8_t *data;
	KpDat *d;
	int k;
	if (!ready(T, "test_data")) return;
	e = expect(T);
	hash = member(T, e, "source_hash");
	memset(&x, 0, sizeof x);
	x.source_hash = (const uint8_t *)hash->s;
	data = fixture(T, "dat.kpd", &size);
	d = load_dat(T, data, size, &x);
	probes = member(T, e, "dat_probes");
	kp_t_ok(T, kp_json_len(probes) > 0, "probes");
	for (k = 0; k < kp_json_len(probes); k++) {
		const KpJson *p = kp_json_at(probes, k);
		const char *table = kp_json_str(kp_json_at(p, 0), ""), *name = kp_json_str(kp_json_at(p, 1), "");
		const KpDatColumn *c = dat_column(T, d, table, name);
		int64_t id = at_int(p, 2), element = at_int(p, 3);
		uint32_t i = (uint32_t)(id * c->per_id + element);
		kp_t_ok(T, kp_dat_has(c, i), "%s.%s[%lld] present", table, name, (long long)id);
		kp_t_eq(T, kp_dat_value(c, i), at_int(p, 4), "%s.%s[%lld]", table, name, (long long)id);
	}
}

static void dat_layout(KpT *T)
{
	size_t size;
	uint8_t *data;
	KpDat *d;
	const KpDatColumn *c;
	if (!ready(T, "test_data")) return;
	data = fixture(T, "dat.kpd", &size);
	d = load_dat(T, data, size, NULL);
	kp_t_eq(T, records_of(d, "units"), 228, "units records");
	kp_t_eq(T, dat_column(T, d, "units", "unit_extents")->per_id, 4, "unit_extents per id");
	c = dat_column(T, d, "units", "infestation");
	kp_t_eq(T, c->first_id, 106, "infestation's first id");
	kp_t_ok(T, !kp_dat_has(c, 105) && kp_dat_has(c, 106), "infestation from unit 106");
	kp_t_eq(T, records_of(d, "weapons"), 130, "weapons records");
	kp_t_eq(T, records_of(d, "images"), 999, "images records");
}

static void dat_rejects_other_kinds(KpT *T)
{
	size_t size;
	uint8_t *data;
	KpDat d;
	KpError err;
	int r;
	if (!ready(T, "test_data")) return;
	data = fixture(T, "mission.kpm", &size);
	r = kp_dat_load(&d, data, size, "mission.kpm", NULL, &err);
	if (r == 0) kp_dat_free(&d);
	raised(T, r, &err, "file kind", "a mission as dat.kpd");
}

/* --- the mission container --- */

static void free_mission(void *p)
{
	kp_mission_free((KpMission *)p);
	free(p);
}

static KpMission *load_mission(KpT *T)
{
	const KpJson *hash = member(T, expect(T), "source_hash");
	KpMission *m = (KpMission *)calloc(1, sizeof *m);
	KpKpfExpect x;
	KpError err;
	size_t size;
	uint8_t *data = fixture(T, "mission.kpm", &size);
	kp_t_ok(T, m != NULL, "out of memory");
	memset(&x, 0, sizeof x);
	x.profile_id = "bw-1.17.0.1";
	x.source_hash = (const uint8_t *)hash->s;
	if (kp_mission_load(m, data, size, "mission.kpm", &x, &err) != 0) {
		free(m);
		kp_t_fail(T, "%s", err.msg);
	}
	kp_t_defer(T, free_mission, m);
	return m;
}

static void mission_meta(KpT *T)
{
	KpMission *m;
	int i;
	if (!ready(T, "test_data")) return;
	m = load_mission(T);
	kp_t_eq(T, m->chk_version, 205, "CHK version");
	kp_t_eq(T, m->era, 13, "era");
	kp_t_eq(T, m->tileset, 5, "tileset");
	kp_t_eq(T, m->width, 8, "width");
	kp_t_eq(T, m->height, 4, "height");
	for (i = 0; i < 12; i++) {
		kp_t_eq(T, m->owners[i], i, "owner %d", i + 1);
		kp_t_eq(T, m->races[i], 20 + i, "race %d", i + 1);
	}
	for (i = 0; i < 8; i++) kp_t_eq(T, m->force_player[i], i + 1, "force of player %d", i + 1);
	kp_t_eq(T, m->force_name[0], 9 + 10 * 256, "force 1's name");
	kp_t_eq(T, m->force_flags[3], 20, "force 4's flags");
	kp_t_eq(T, m->name_string, 1, "name string");
	kp_t_eq(T, m->description_string, 4, "description string");
	kp_t_ok(T, m->has_colors, "COLR");
	kp_t_eq(T, m->colors[0], 7, "colour 1");
	kp_t_eq(T, m->colors[7], 0, "colour 8");
}

static void mission_raw_sections(KpT *T)
{
	KpMission *m;
	if (!ready(T, "test_data")) return;
	m = load_mission(T);
	kp_t_eq(T, m->tiles.size, 64, "MTXM size");
	kp_t_eq(T, kp_le_u16(m->tiles.data), 1000, "tile 0");
	kp_t_eq(T, kp_le_u16(m->tiles.data + 60), 1030, "tile 30");
	kp_t_eq(T, kp_le_u16(m->tiles.data + 62), 1000 + 0x8000, "tile 31");
	kp_t_eq(T, m->mask.data[31], 31, "mask byte 31");
	kp_t_eq(T, m->locations.size, 255 * 20, "MRGN size");
	kp_t_eq(T, m->properties.size, 1280, "UPRP size");
	kp_t_eq(T, m->triggers.size, 2400, "TRIG size");
	kp_t_eq(T, m->briefing.size, 2400, "MBRF size");
	kp_t_eq(T, m->units.size, 0, "UNIT size");
	kp_t_eq(T, m->sprites.size, 0, "THG2 size");
}

static void mission_rules_in_dispatch_order(KpT *T)
{
	KpMission *m;
	char tags[256];
	size_t at = 0;
	int k;
	if (!ready(T, "test_data")) return;
	m = load_mission(T);
	tags[0] = '\0';
	for (k = 0; k < m->rule_count; k++) {
		at += (size_t)snprintf(tags + at, sizeof tags - at, "%s%s:%u", k ? " " : "", m->rules[k].tag,
			(unsigned)m->rules[k].data.size);
		if (at >= sizeof tags) break;
	}
	kp_t_eq_str(T, tags, "UNIx:4168 UPGx:794 TECx:396 PUNI:5700 PUPx:2318 PTEx:1672", "the override sections");
}

static const KpMissionSound *sound_of(const KpMission *m, uint16_t string_id)
{
	int k;
	for (k = 0; k < m->sound_count; k++) {
		if (m->sounds[k].string_id == string_id) return &m->sounds[k];
	}
	return NULL;
}

static void mission_sounds(KpT *T)
{
	KpMission *m;
	const KpMissionSound *s;
	if (!ready(T, "test_data")) return;
	m = load_mission(T);
	s = sound_of(m, 2);
	kp_t_ok(T, s != NULL, "string 2's sound");
	kp_t_eq(T, s->index, 0, "string 2's file");
	kp_t_eq(T, s->length, 4410, "string 2's length");
	kp_t_eq(T, s->avg_bytes_per_sec, 44100, "string 2's rate");
	s = sound_of(m, 3);
	kp_t_ok(T, s != NULL, "string 3's sound");
	kp_t_eq(T, s->index, KP_MISSION_NO_SOUND, "string 3's file is missing");
	kp_t_eq(T, s->length, 0, "string 3's length");
	kp_t_ok(T, sound_of(m, 1) == NULL, "string 1 names no sound");
}

/* The mission strings through GetMapString (kp_map_string reads only the game's copy of STR). */
static void mission_map_strings(KpT *T)
{
	KpMission *m;
	const KpJson *want;
	KpGame *g;
	int k, n;
	if (!ready(T, "test_data")) return;
	m = load_mission(T);
	want = member(T, expect(T), "mission_strings");
	g = (KpGame *)calloc(1, sizeof *g);
	kp_t_ok(T, g != NULL, "out of memory");
	kp_t_defer(T, free, g);
	g->trig.strings = (uint8_t *)m->strings.data;
	g->trig.strings_size = m->strings.size;
	n = kp_json_len(want);
	kp_t_eq(T, kp_le_u16(m->strings.data), n, "the string count");
	for (k = 0; k < n; k++) {
		const KpJson *s = kp_json_at(want, k);
		const char *got = kp_map_string(g, k + 1);
		kp_t_ok(T, got != NULL && strlen(got) == (size_t)s->count && memcmp(got, s->s, (size_t)s->count) == 0,
			"string %d", k + 1);
	}
	kp_t_ok(T, kp_map_string(g, 0) == NULL, "string 0 is none");
	kp_t_eq_str(T, kp_map_string(g, n + 1), "", "past the count");
	kp_t_eq_str(T, kp_map_string(g, 65536 + 1), "name", "the id's low WORD");
}

static void mission_derived_sections(KpT *T)
{
	KpMission *m;
	const KpJson *d, *neighbors, *counts, *caps, *first;
	const KpMissionRegions *r;
	const uint8_t *s, *rec, *c;
	uint32_t last, count, offset;
	int k;
	if (!ready(T, "test_data")) return;
	m = load_mission(T);
	d = member(T, expect(T), "mission_derived");
	kp_t_eq(T, m->walk.size, kp_json_field_int(d, "walk_size", -1), "WALK size");
	r = &m->regions;
	kp_t_eq(T, r->count, kp_json_field_int(d, "regions", -1), "regions");
	kp_t_eq(T, r->split_count, kp_json_field_int(d, "splits", -1), "splits");
	s = r->data.data;
	last = r->count - 1;
	rec = s + r->regions_at + last * KP_REGION_SIZE;
	kp_t_eq(T, kp_le_u16(rec), kp_json_field_int(d, "last_class", -1), "the last region's class");
	kp_t_eq(T, kp_le_u16(rec + 2), kp_json_field_int(d, "last_group", -1), "its group");
	kp_t_eq(T, kp_le_u16(rec + 4), kp_json_field_int(d, "last_tiles", -1), "its tiles");
	neighbors = kp_json_get(d, "last_neighbors");
	count = rec[7];
	offset = kp_le_u32(rec + 12);
	kp_t_eq(T, count, kp_json_len(neighbors), "its neighbour count");
	for (k = 0; k < (int)count; k++) {
		kp_t_eq(T, kp_le_u16(s + r->pool_at + 2 * (offset + (uint32_t)k)), at_int(neighbors, k), "neighbour %d", k + 1);
	}
	kp_t_eq(T, (int32_t)kp_le_u32(rec + 16), kp_json_field_int(d, "last_center_x", -1), "its center x");
	kp_t_eq(T, kp_le_u16(s + r->raster_at + 2 * (2 * r->width + 3)), kp_json_field_int(d, "cell_3_2", -1), "cell (3, 2)");
	counts = kp_json_get(d, "contour_counts");
	caps = kp_json_get(d, "capacities");
	for (k = 0; k < 4; k++) {
		kp_t_eq(T, m->contours.counts[k], at_int(counts, k), "contour count %d", k + 1);
		kp_t_eq(T, m->contours.capacities[k], at_int(caps, k), "capacity %d", k + 1);
	}
	first = kp_json_get(d, "first_contour");
	c = m->contours.data.data + m->contours.at[0];
	kp_t_eq(T, (int16_t)kp_le_u16(c), at_int(first, 0), "the first contour's axis");
	kp_t_eq(T, (int16_t)kp_le_u16(c + 2), at_int(first, 1), "its start");
	kp_t_eq(T, (int16_t)kp_le_u16(c + 4), at_int(first, 2), "its stop");
	kp_t_eq(T, c[6], at_int(first, 3), "its direction");
	kp_t_eq(T, c[7], at_int(first, 4), "its info");
}

static void mission_packs(KpT *T)
{
	KpMission *m;
	if (!ready(T, "test_data")) return;
	m = load_mission(T);
	kp_t_eq(T, m->pack_count, 2, "packs");
	kp_t_ok(T, m->packs[0].size == 7 && memcmp(m->packs[0].data, "effects", 7) == 0, "the first pack is effects");
	kp_t_ok(T, m->packs[1].size == 6 && memcmp(m->packs[1].data, "terran", 6) == 0, "the second pack is terran");
}

static void mission_rejects_other_kinds(KpT *T)
{
	KpMission m;
	KpKpfExpect x;
	KpError err;
	size_t size;
	uint8_t *data, other[32];
	int r;
	if (!ready(T, "test_data")) return;
	data = fixture(T, "basic.kpd", &size);
	r = kp_mission_load(&m, data, size, "basic.kpd", NULL, &err);
	if (r == 0) kp_mission_free(&m);
	raised(T, r, &err, "file kind", "basic.kpd as a mission");
	data = fixture(T, "mission.kpm", &size);
	memset(other, 'x', sizeof other);
	memset(&x, 0, sizeof x);
	x.source_hash = other;
	r = kp_mission_load(&m, data, size, "mission.kpm", &x, &err);
	if (r == 0) kp_mission_free(&m);
	raised(T, r, &err, "different data set", "another data set");
}

/* --- grpmeta.kpd (the hits are the Python twin of 0x40fd50 on the raw GRP bytes) --- */

static void free_grp(void *p)
{
	kp_grpmeta_free((KpGrpMeta *)p);
	free(p);
}

static KpGrpMeta *load_grp(KpT *T)
{
	KpGrpMeta *g = (KpGrpMeta *)calloc(1, sizeof *g);
	KpError err;
	size_t size;
	uint8_t *data = fixture(T, "grpmeta.kpd", &size);
	kp_t_ok(T, g != NULL, "out of memory");
	if (kp_grpmeta_load(g, data, size, "grpmeta.kpd", NULL, &err) != 0) {
		free(g);
		kp_t_fail(T, "%s", err.msg);
	}
	kp_t_defer(T, free_grp, g);
	return g;
}

static int label_is(const KpLabel *l, const char *s) { return l->size == strlen(s) && memcmp(l->text, s, l->size) == 0; }

static void grpmeta_entries_and_atlases(KpT *T)
{
	KpGrpMeta *g;
	const KpGrpEntry *t;
	const KpAtlas *a;
	if (!ready(T, "test_data")) return;
	g = load_grp(T);
	kp_t_eq(T, g->tbl_count, 5, "images.tbl entries");
	kp_t_eq(T, kp_grpmeta_grp_of_tbl(g, 1), 1, "tbl 1");
	kp_t_eq(T, kp_grpmeta_grp_of_tbl(g, 4), 1, "tbl 4: the same file, another case");
	kp_t_eq(T, kp_grpmeta_grp_of_tbl(g, 3), 2, "tbl 3");
	kp_t_eq(T, kp_grpmeta_grp_of_tbl(g, 2), 0, "tbl 2 is no GRP");
	kp_t_eq(T, kp_grpmeta_grp_of_tbl(g, 5), 0, "tbl 5 is no GRP");
	kp_t_eq(T, kp_grpmeta_lo_of_tbl(g, 2), 1, "tbl 2's LO");
	kp_t_eq(T, g->grp_count, 2, "GRPs");
	t = &g->grps[1];
	kp_t_ok(T, label_is(&t->path, "unit\\terran\\fix.grp"), "the first GRP's path");
	kp_t_eq(T, t->frames, 3, "frames");
	kp_t_eq(T, t->width, 40, "width");
	kp_t_eq(T, t->height, 30, "height");
	kp_t_ok(T, label_is(&t->animation, "terran_fix"), "the animation");
	kp_t_eq(T, t->first_frame, 0, "the first frame");
	kp_t_eq(T, g->grps[2].first_frame, 3, "the second GRP's first frame");
	kp_t_ok(T, t->atlas >= 1 && t->atlas <= g->atlas_count, "the first GRP's atlas");
	a = &g->atlases[t->atlas];
	kp_t_ok(T, label_is(&a->name, "terran"), "its atlas is terran");
	kp_t_eq(T, a->mask, 0, "terran's mask");
	a = &g->atlases[g->grps[2].atlas];
	kp_t_ok(T, label_is(&a->name, "effects"), "the second GRP's atlas is effects");
	kp_t_ok(T, label_is(&a->pack, "effects"), "in the effects pack");
	kp_t_eq(T, a->mask, 1, "the effects fixture draws an opaque index 0");
}

static void grpmeta_frames(KpT *T)
{
	KpGrpMeta *g;
	int x, y, w, h;
	if (!ready(T, "test_data")) return;
	g = load_grp(T);
	kp_grpmeta_frame(g, 1, 0, &x, &y, &w, &h);
	kp_t_ok(T, x == 3 && y == 4 && w == 23 && h == 9, "frame 0: %d %d %d %d", x, y, w, h);
	kp_grpmeta_frame(g, 1, 1, &x, &y, &w, &h);
	kp_t_ok(T, w == 200 && h == 4, "frame 1: %d x %d", w, h);
	kp_grpmeta_frame(g, 1, 2, &x, &y, &w, &h);
	kp_t_ok(T, w == 0 && h == 0, "frame 2 is empty");
	kp_grpmeta_frame(g, 2, 0, &x, &y, &w, &h);
	kp_t_ok(T, x == 1 && w == 4, "the second GRP's frame 0: x %d w %d", x, w);
}

static void grpmeta_pixel_opaque_matches_python_twin(KpT *T)
{
	KpGrpMeta *g;
	const KpJson *hits;
	int n = 0;
	uint32_t entry, frame;
	if (!ready(T, "test_data")) return;
	g = load_grp(T);
	hits = member(T, expect(T), "grpmeta_hits");
	for (entry = 1; entry <= g->grp_count; entry++) {
		for (frame = 0; frame < g->grps[entry].frames; frame++) {
			const KpJson *want = kp_json_at(hits, n++);
			int x, y, w, h, i = 0;
			kp_t_ok(T, want != NULL, "a hit string for entry %u frame %u", entry, frame);
			kp_grpmeta_frame(g, entry, frame, &x, &y, &w, &h);
			for (y = -1; y <= h; y++) {
				for (x = -1; x <= w; x++) {
					int got = kp_grpmeta_pixel_opaque(g, entry, frame, x, y);
					kp_t_ok(T, i < want->count && got == want->s[i] - '0', "entry %u frame %u pixel (%d, %d): got %d", entry,
						frame, x, y, got);
					i++;
				}
			}
			kp_t_eq(T, i, want->count, "entry %u frame %u: the pixels", entry, frame);
		}
	}
	kp_t_eq(T, n, kp_json_len(hits), "the frames");
}

static void grpmeta_run_boundaries(KpT *T)
{
	KpGrpMeta *g;
	if (!ready(T, "test_data")) return;
	g = load_grp(T);
	/* frame 1 row 0: a leading empty transparent packet, literal 2, repeat 3, transparent 133 */
	kp_t_eq(T, kp_grpmeta_pixel_opaque(g, 1, 1, 0, 0), 0, "x 0 takes the first (empty, transparent) packet");
	kp_t_eq(T, kp_grpmeta_pixel_opaque(g, 1, 1, 5, 0), 1, "boundary 5 belongs to the preceding opaque run");
	kp_t_eq(T, kp_grpmeta_pixel_opaque(g, 1, 1, 6, 0), 0, NULL);
	kp_t_eq(T, kp_grpmeta_pixel_opaque(g, 1, 1, 138, 0), 0, "the end of the transparent 5 + 127 + 1");
	kp_t_eq(T, kp_grpmeta_pixel_opaque(g, 1, 1, 139, 0), 1, NULL);
	kp_t_eq(T, kp_grpmeta_pixel_opaque(g, 1, 1, 200, 0), 0, "outside the frame");
}

static void grpmeta_lo_points(KpT *T)
{
	KpGrpMeta *g;
	const KpLoEntry *lo;
	int x, y;
	if (!ready(T, "test_data")) return;
	g = load_grp(T);
	kp_t_ok(T, g->lo_count >= 1, "an LO entry");
	lo = &g->los[1];
	kp_t_ok(T, label_is(&lo->path, "unit\\terran\\fix.lol"), "the LO's path");
	kp_t_eq(T, lo->frames, 2, "LO frames");
	kp_t_eq(T, lo->points, 2, "LO points");
	kp_t_ok(T, kp_grpmeta_lo_point(lo, 0, 0, &x, &y) && x == -3 && y == 4, "frame 0 point 0: %d, %d", x, y);
	kp_t_ok(T, kp_grpmeta_lo_point(lo, 0, 1, &x, &y) && x == 127 && y == -128, "frame 0 point 1: %d, %d", x, y);
	kp_t_ok(T, kp_grpmeta_lo_point(lo, 1, 0, &x, &y) && x == 0 && y == -1, "frame 1 point 0: %d, %d", x, y);
}

/* images.dat's grp_file and LO columns name images.tbl entries (0: none). */
static void grpmeta_image_lookup(KpT *T)
{
	KpGrpMeta *g;
	if (!ready(T, "test_data")) return;
	g = load_grp(T);
	kp_t_eq(T, kp_grpmeta_grp_of_tbl(g, 4), 1, "an image of tbl 4");
	kp_t_eq(T, kp_grpmeta_grp_of_tbl(g, 0), 0, "an image without a GRP");
	kp_t_eq(T, kp_grpmeta_lo_of_tbl(g, 2), 1, "an overlay of tbl 2");
	kp_t_eq(T, kp_grpmeta_lo_of_tbl(g, 0), 0, "no overlay");
	kp_t_eq(T, kp_grpmeta_grp_of_tbl(g, 6), 0, "an index past the table");
}

/* --- iscript.kpd and aiscript.kpd --- */

static void free_script(void *p)
{
	kp_iscript_free((KpIScript *)p);
	free(p);
}

static void scripts_iscript_headers(KpT *T)
{
	KpIScript *s;
	KpError err;
	size_t size;
	uint8_t *data;
	int32_t h;
	if (!ready(T, "test_data")) return;
	data = fixture(T, "iscript.kpd", &size);
	s = (KpIScript *)calloc(1, sizeof *s);
	kp_t_ok(T, s != NULL, "out of memory");
	if (kp_iscript_load(s, data, size, "iscript.kpd", NULL, &err) != 0) {
		free(s);
		kp_t_fail(T, "%s", err.msg);
	}
	kp_t_defer(T, free_script, s);
	kp_t_eq(T, kp_iscript_header(s, 5), 4, "id 5: the first match wins");
	h = kp_iscript_header(s, 3);
	kp_t_eq(T, h, 16, "id 3");
	kp_t_ok(T, h >= 0 && (uint32_t)h + 4 <= s->code_size && memcmp(s->code + h, "SCPE", 4) == 0, "header 3 is SCPE");
	kp_t_eq(T, s->slots[0], 2, "type 0's slots");
	kp_t_eq(T, s->slots[12], 14, "type 12's slots");
	kp_t_eq(T, s->slots[1], -1, "type 1 has none");
}

static void scripts_ai_resolution(KpT *T)
{
	static const struct {
		const char *id;
		uint32_t offset;
		int bin;
	} CASES[] = {
		{ "Ter5", 16, 0 }, { "Redi", 24, 1 }, { "Both", 20, 0 }, { "BwOn", 28, 1 }, { "Zero", 0, -1 }, { "None", 0, -1 },
	};
	KpAiBins b;
	KpError err;
	size_t size;
	uint8_t *data;
	int k;
	if (!ready(T, "test_data")) return;
	data = fixture(T, "aiscript.kpd", &size);
	kp_t_ok(T, kp_ai_bins_load(&b, data, size, "aiscript.kpd", &err) == 0, "%s", err.msg);
	for (k = 0; k < (int)(sizeof CASES / sizeof CASES[0]); k++) {
		int bin = -1;
		uint32_t off = kp_ai_bins_find(&b, kp_le_u32((const uint8_t *)CASES[k].id), &bin);
		if (off != CASES[k].offset || (off != 0 && bin != CASES[k].bin)) {
			kp_ai_bins_free(&b);
			kp_t_fail(T, "%s: offset %u bin %d, expected %u bin %d", CASES[k].id, off, bin, CASES[k].offset, CASES[k].bin);
		}
	}
	kp_ai_bins_free(&b);
}

static void scripts_kinds(KpT *T)
{
	KpIScript s;
	KpAiBins b;
	KpError err;
	size_t size;
	uint8_t *data;
	int r;
	if (!ready(T, "test_data")) return;
	data = fixture(T, "aiscript.kpd", &size);
	r = kp_iscript_load(&s, data, size, "aiscript.kpd", NULL, &err);
	if (r == 0) kp_iscript_free(&s);
	raised(T, r, &err, "file kind", "aiscript.kpd as iscript.kpd");
	data = fixture(T, "iscript.kpd", &size);
	r = kp_ai_bins_load(&b, data, size, "iscript.kpd", &err);
	if (r == 0) kp_ai_bins_free(&b);
	raised(T, r, &err, "file kind", "iscript.kpd as aiscript.kpd");
}

/* --- the override effects (the Python twin converter/derive/rules.py) --- */

#define CHECKSUM 1000003

/* The fixture's checksum of a table of effects: sum((key * 31 + value) % 1000003) % 1000003 over the set values, a
 * tuple key (k0, k1, k2) hashed as sum((k + 1) * 997^position) (a single key as itself). */
static int64_t sum1(const int64_t *v, int n)
{
	int64_t sum = 0;
	int k;
	for (k = 0; k < n; k++) {
		if (v[k] != KP_RULE_UNSET) sum += ((int64_t)k * 31 + v[k]) % CHECKSUM;
	}
	return sum % CHECKSUM;
}

static int64_t sum2(const int64_t *v, int rows, int cols)
{
	int64_t sum = 0;
	int a, b;
	for (a = 0; a < rows; a++) {
		for (b = 0; b < cols; b++) {
			int64_t x = v[a * cols + b], h = (a + 1) + (int64_t)(b + 1) * 997;
			if (x != KP_RULE_UNSET) sum += (h * 31 + x) % CHECKSUM;
		}
	}
	return sum % CHECKSUM;
}

static int64_t sum3(const int64_t *v, int n0, int n1, int n2)
{
	int64_t sum = 0;
	int a, b, c;
	for (a = 0; a < n0; a++) {
		for (b = 0; b < n1; b++) {
			for (c = 0; c < n2; c++) {
				int64_t x = v[(a * n1 + b) * n2 + c], h = (a + 1) + (int64_t)(b + 1) * 997 + (int64_t)(c + 1) * 997 * 997;
				if (x != KP_RULE_UNSET) sum += (h * 31 + x) % CHECKSUM;
			}
		}
	}
	return sum % CHECKSUM;
}

/* The DAT columns the effects read: units.subunit1, ground_weapon and air_weapon from the fixture's associations. */
static KpDat *association_dat(KpT *T, const KpJson *assoc)
{
	static const char *const COLUMNS[3] = { "subunit1", "ground_weapon", "air_weapon" };
	static const uint8_t SIZES[3] = { 2, 1, 1 };
	KpDat *d = (KpDat *)calloc(1, sizeof *d);
	KpError err;
	size_t at = 0;
	int k, u, n = kp_json_len(assoc);
	kp_t_ok(T, d != NULL, "out of memory");
	if (kp_dat_alloc(d, (size_t)n * 4, 3, &err) != 0) {
		free(d);
		kp_t_fail(T, "%s", err.msg);
	}
	kp_t_defer(T, free_dat, d);
	for (k = 0; k < 3; k++) {
		KpDatColumn *c = &d->columns[d->column_count++];
		c->table = kp_dat_table_name("units");
		snprintf(c->name, sizeof c->name, "%s", COLUMNS[k]);
		c->size = SIZES[k];
		c->per_id = 1;
		c->count = (uint16_t)n;
		c->first_id = 0;
		c->data = d->blob + at;
		at += (size_t)c->size * (size_t)n;
		for (u = 0; u < n; u++) kp_dat_set(d, c, (uint32_t)u, (uint32_t)at_int(kp_json_at(assoc, u), k));
	}
	return d;
}

static void rules_matches_python(KpT *T)
{
	static const char *const UNIT_FIELDS[KP_RULE_UNIT_FIELDS] = { "health", "shields", "armor", "build_time",
		"mineral_cost", "gas_cost", "name" };
	static const char *const UPGRADE_FIELDS[KP_RULE_UPGRADE_FIELDS] = { "mineral_base", "mineral_factor", "gas_base",
		"gas_factor", "time_base", "time_factor" };
	static const char *const TECH_FIELDS[KP_RULE_TECH_FIELDS] = { "mineral_cost", "gas_cost", "research_time", "energy" };
	const KpJson *e, *want;
	KpMission m;
	KpMissionRule rules[16];
	KpRuleEffects *eff;
	KpDat *d;
	KpError err;
	size_t size, pos = 0;
	uint8_t *raw;
	char key[64];
	int k;
	if (!ready(T, "test_data")) return;
	e = expect(T);
	d = association_dat(T, member(T, e, "rule_associations"));
	raw = fixture(T, "rules.bin", &size);
	memset(&m, 0, sizeof m);
	/* the sections as mission.kpm's RULE holds them: tag, u32 size, payload */
	while (pos + 8 <= size) {
		uint32_t n = kp_le_u32(raw + pos + 4);
		kp_t_ok(T, m.rule_count < 16 && pos + 8 + n <= size, "rules.bin's sections");
		memcpy(rules[m.rule_count].tag, raw + pos, 4);
		rules[m.rule_count].tag[4] = '\0';
		rules[m.rule_count].data.data = raw + pos + 8;
		rules[m.rule_count].data.size = n;
		m.rule_count++;
		pos += 8 + n;
	}
	m.rules = rules;
	eff = (KpRuleEffects *)malloc(sizeof *eff);
	kp_t_ok(T, eff != NULL, "out of memory");
	kp_t_defer(T, free, eff);
	kp_t_ok(T, kp_rules_effects(eff, &m, d, 0, &err) == 0, "%s", err.msg);
	want = member(T, e, "rule_sums");
	for (k = 0; k < KP_RULE_UNIT_FIELDS; k++) {
		snprintf(key, sizeof key, "unit_%s", UNIT_FIELDS[k]);
		kp_t_eq(T, sum1(eff->units[k], KP_RULE_UNITS), kp_json_field_int(want, key, -1), "%s", key);
	}
	for (k = 0; k < KP_RULE_UPGRADE_FIELDS; k++) {
		snprintf(key, sizeof key, "upgrade_%s", UPGRADE_FIELDS[k]);
		kp_t_eq(T, sum1(eff->upgrades[k], KP_RULE_UPGRADES), kp_json_field_int(want, key, -1), "%s", key);
	}
	for (k = 0; k < KP_RULE_TECH_FIELDS; k++) {
		snprintf(key, sizeof key, "tech_%s", TECH_FIELDS[k]);
		kp_t_eq(T, sum1(eff->techs[k], KP_RULE_TECHS), kp_json_field_int(want, key, -1), "%s", key);
	}
	kp_t_eq(T, sum1(eff->weapon_damage, KP_RULE_WEAPONS), kp_json_field_int(want, "weapon_damage", -1), "damage");
	kp_t_eq(T, sum1(eff->weapon_bonus, KP_RULE_WEAPONS), kp_json_field_int(want, "weapon_bonus", -1), "bonus");
	kp_t_eq(T, sum2(&eff->availability[0][0], KP_RULE_PLAYERS, KP_RULE_UNITS), kp_json_field_int(want, "availability", -1),
		"availability");
	kp_t_eq(T, sum3(&eff->upgrade_levels[0][0][0], 2, KP_RULE_PLAYERS, KP_RULE_UPGRADES),
		kp_json_field_int(want, "upgrade_levels", -1), "upgrade levels");
	kp_t_eq(T, sum3(&eff->tech_states[0][0][0], 2, KP_RULE_PLAYERS, KP_RULE_TECHS),
		kp_json_field_int(want, "tech_states", -1), "tech states");
}

/* --- the tileset and the terrain import (the Python twin converter/derive/terrain.py) --- */

#define CV5_RECORD 52
#define CV5_FLAG_MASK 0xd8fau

static void load_tileset(KpT *T, KpTileset *ts)
{
	KpError err;
	size_t size;
	uint8_t *data = fixture(T, "tileset.kpd", &size);
	kp_t_ok(T, kp_tileset_load(ts, data, size, "tileset.kpd", &err) == 0, "%s", err.msg);
}

static void terrain_tileset(KpT *T)
{
	KpTileset ts;
	if (!ready(T, "test_data")) return;
	load_tileset(T, &ts);
	kp_t_eq(T, ts.groups, 70, "CV5 groups");
	kp_t_eq(T, ts.rgb[3], 4 * 3 % 256, "entry 1's red is WPE byte 4");
	kp_t_eq(T, ts.cycles.speed, 8, "the cycles' speed");
	kp_t_eq(T, ts.cycles.count, 3, "cycles");
	kp_t_eq(T, ts.cycles.high[2], 254, "the third cycle's high index");
	kp_t_eq(T, ts.megatiles, 40, "megatile flags 0..39");
	kp_t_ok(T, ts.edge_count == 2 && ts.edge_png && ts.edge_size > 8 && memcmp(ts.edge_png + 1, "PNG", 3) == 0,
		"creep's two edge frames, a PNG");
}

static void terrain_import_matches_python(KpT *T)
{
	KpTileset ts;
	KpMission *m;
	const KpJson *e, *ids, *flags;
	uint16_t got[32];
	int64_t bad = -1;
	int i;
	if (!ready(T, "test_data")) return;
	e = expect(T);
	load_tileset(T, &ts);
	m = load_mission(T);
	kp_t_eq(T, (int64_t)m->width * m->height, 32, "the mission's cells");
	kp_t_eq(T, kp_terrain_import(m, &ts, got, &bad), 0, "the import (bad cell %lld)", (long long)bad);
	ids = member(T, e, "terrain_ids");
	flags = member(T, e, "terrain_flags");
	for (i = 0; i < 32; i++) {
		uint32_t raw = kp_le_u16(m->tiles.data + 2 * i), group = (raw >> 4) & 0x7ff, mega = got[i] & 0x7fffu, f;
		kp_t_eq(T, got[i], at_int(ids, i), "megatile %d", i);
		/* ImportMapTiles' flags from the loader's CV5 and megatile flags (the game's own import, kp_start.c, is the
		 * original's in test_initial_state's "tiles") */
		kp_t_ok(T, mega < ts.megatiles, "megatile %u has flags", mega);
		f = 0xffffu | (kp_le_u16(ts.cv5 + group * CV5_RECORD + 2) & CV5_FLAG_MASK) << 16 | kp_le_u32(ts.mflags + 4 * mega);
		if (raw >= 0x8000) f |= 0x00400000u;
		kp_t_eq(T, f, at_int(flags, i), "flags %d", i);
	}
}

static void terrain_missing_group_stops(KpT *T)
{
	KpTileset ts;
	KpMission *m;
	uint16_t got[32];
	int64_t bad = -1;
	if (!ready(T, "test_data")) return;
	load_tileset(T, &ts);
	m = load_mission(T);
	ts.groups = 63; /* tile 1008 (cell 8) selects group 63, now missing */
	kp_t_eq(T, kp_terrain_import(m, &ts, got, &bad), -1, "the import stops");
	kp_t_eq(T, bad, 8, "at cell 8");
}

/* --- palettes.kpd (pixel (row, column) of each synthetic PCX is
 * (row * 7 + column + salt) % 256, salt = len(tileset) + len(name)) --- */

#define OFIRE 0
#define TRANS50 4

static void load_palettes(KpT *T, KpPalettes *p)
{
	KpError err;
	size_t size;
	uint8_t *data = fixture(T, "palettes.kpd", &size);
	kp_t_ok(T, kp_palettes_load(p, data, size, &err) == 0, "%s", err.msg);
}

static void palettes_tables(KpT *T)
{
	KpPalettes p;
	const KpTilesetPalettes *badlands;
	const KpRemapTable *ofire;
	int salt;
	if (!ready(T, "test_data")) return;
	load_palettes(T, &p);
	badlands = &p.tilesets[0];
	ofire = &badlands->remap[OFIRE];
	salt = (int)(strlen("badlands") + strlen("ofire"));
	kp_t_eq(T, ofire->rows, 64, "ofire's rows");
	kp_t_eq(T, ofire->data[0], 0, "row 0 is never initialized natively; stored as zero");
	kp_t_eq(T, ofire->data[256], salt % 256, "PCX row 0 column 0 is table row 1");
	kp_t_eq(T, ofire->data[2 * 256 + 3], (7 + 3 + salt) % 256, "PCX row 1 column 3");
	kp_t_eq(T, badlands->remap[TRANS50].rows, 256, "trans50's rows");
	kp_t_eq(T, badlands->dark[256], (7 + 8 + 4) % 256, "dark row 1");
	kp_t_eq(T, p.tilesets[7].shift[0], (8 + 5) % 256, "twilight's shift row");
}

static void palettes_game_tables(KpT *T)
{
	KpPalettes p;
	int salt = (int)strlen("game\\tunit.pcx");
	if (!ready(T, "test_data")) return;
	load_palettes(T, &p);
	kp_t_eq(T, (int64_t)p.tunit_size, 0x60, "tunit");
	kp_t_eq(T, (int64_t)p.tminimap_size, 12, "tminimap");
	kp_t_eq(T, (int64_t)p.tselect_size, 24, "tselect");
	kp_t_eq(T, (int64_t)p.thpbar_size, 19, "thpbar");
	kp_t_eq(T, p.tunit[2 * 8 + 3], (16 + 3 + salt) % 256, "player 2's colour 3");
}

/* The executable's tables compiled in (kp_defs.h, ADR 0018 stage D2), each found by its fourcc: the containers' sections
 * of the same name, and the lists the runtime builds its structures from the original files with (stage D3: the
 * tilesets' cycles, the fonts' paths, the interface's dialogs and files), which no container carries. */
static const char *const DEFS_TABLES[] = { "ACCL", "BTNS", "CHEA", "COLS", "ESTB", "MUSI", "RQOR", "RQRS", "RQTU",
	"RQUN", "RQUP", "SLOT", "STLY", "TABL", "VOLT" };
static const char *const BUILT_TABLES[] = { "CYC0", "CYC1", "CYC2", "CYC3", "CYC4", "CYC5", "CYC6", "CYC7", "FNTP",
	"KBIN", "KEXT", "KLAB", "UBIN", "UCRD", "UDGR", "UGLF", "UGRP", "UPCX", "UTHF", "UTHM" };
#define DEFS_COUNT (sizeof DEFS_TABLES / sizeof DEFS_TABLES[0])
#define BUILT_COUNT (sizeof BUILT_TABLES / sizeof BUILT_TABLES[0])

static void defs_tables_are_compiled(KpT *T)
{
	const KpDefsProfile *p = kp_defs_profile("bw-1.17.0.1");
	size_t k;
	kp_t_ok(T, p == &kp_defs_bw_1_17_0_1, "the profile by its id");
	kp_t_ok(T, kp_defs_profile("bw-0") == NULL && kp_defs_profile(NULL) == NULL, "no other profile");
	kp_t_eq(T, p->table_count, (int64_t)(DEFS_COUNT + BUILT_COUNT), "the tables");
	for (k = 0; k < DEFS_COUNT + BUILT_COUNT; k++) {
		const char *fourcc = k < DEFS_COUNT ? DEFS_TABLES[k] : BUILT_TABLES[k - DEFS_COUNT];
		const KpDefsTable *t = kp_defs_table(p, fourcc);
		kp_t_ok(T, t != NULL && t->size > 0 && strcmp(t->fourcc, fourcc) == 0, "%s", fourcc);
	}
	kp_t_ok(T, kp_defs_table(p, "CODE") == NULL && kp_defs_table(p, "AAAA") == NULL && kp_defs_table(p, "ZZZZ") == NULL,
		"no other table");
}

/* kp_kpf_table: a container's own section first (the fixtures' tables), else the compiled table of its profile, else
 * none with the error. */
static void kpf_table_falls_back_to_the_compiled(KpT *T)
{
	static const uint8_t own[] = { 1, 7, 9 };
	const KpDefsTable *slot = kp_defs_table(&kp_defs_bw_1_17_0_1, "SLOT");
	KpKpfOut out;
	uint8_t *with = NULL, *without = NULL, *other = NULL;
	size_t with_size = 0, without_size = 0, other_size = 0, n;
	const uint8_t *p;
	uint32_t size = 1;
	KpKpf c;
	KpError err;
	memset(&out, 0, sizeof out);
	out.fourcc = "SLOT";
	out.payload = own;
	out.size = sizeof own;
	kp_t_ok(T, kp_kpf_build("ISCR", 2, 1, "bw-1.17.0.1", NULL, &out, 1, &with, &with_size) == 0
		&& kp_kpf_build("ISCR", 2, 1, "bw-1.17.0.1", NULL, NULL, 0, &without, &without_size) == 0
		&& kp_kpf_build("ISCR", 2, 1, "bw-0", NULL, NULL, 0, &other, &other_size) == 0, "the containers built");
	kp_t_defer(T, free, with);
	kp_t_defer(T, free, without);
	kp_t_defer(T, free, other);
	p = kp_kpf_table_in(with, with_size, "SLOT", &n);
	kp_t_ok(T, p != NULL && n == sizeof own && memcmp(p, own, n) == 0, "the container's own SLOT");
	p = kp_kpf_table_in(without, without_size, "SLOT", &n);
	kp_t_ok(T, slot != NULL && p == slot->data && n == slot->size, "the compiled SLOT");
	p = kp_kpf_table_in(other, other_size, "SLOT", &n);
	kp_t_ok(T, p == NULL && n == 0, "none for another profile");
	kp_t_ok(T, kp_kpf_parse(&c, other, other_size, "other", 0, &err) == 0, "%s", err.msg);
	p = kp_kpf_table(&c, "SLOT", &size, &err);
	kp_kpf_free(&c);
	kp_t_ok(T, p == NULL && size == 0 && strstr(err.msg, "no section SLOT") != NULL, "the error: %s", err.msg);
}

/* The compiled tables equal the converted data's sections of the same name (the converter writes both with the same
 * functions, tools/defs_c.py). */
static void defs_tables_equal_the_converted_sections(KpT *T)
{
	static const struct {
		const char *file, *fourccs[7];
	} FILES[] = {
		{ "generated/data/dat.kpd", { "COLS", "RQOR", "RQRS", "RQTU", "RQUN", "RQUP", NULL } },
		{ "generated/data/iscript.kpd", { "SLOT", NULL } },
		{ "generated/data/campaign.kpd", { "TABL", "CHEA", NULL } },
		{ "generated/data/ui.kpd", { "ACCL", "BTNS", "ESTB", "MUSI", "STLY", "VOLT" } },
	};
	static const char *const NEEDED[] = { "generated/data/dat.kpd", "generated/data/iscript.kpd",
		"generated/data/campaign.kpd", "generated/data/ui.kpd", NULL };
	size_t f;
	int k, compared = 0;
	if (!kp_t_ready(T, "the converted data", NEEDED)) return;
	for (f = 0; f < sizeof FILES / sizeof FILES[0]; f++) {
		size_t size, n;
		uint8_t *data = kp_t_read(T, FILES[f].file, &size);
		for (k = 0; k < 7 && FILES[f].fourccs[k]; k++) {
			const char *fourcc = FILES[f].fourccs[k];
			const uint8_t *s = kp_kpf_section(data, size, fourcc, &n);
			const KpDefsTable *t = kp_defs_table(&kp_defs_bw_1_17_0_1, fourcc);
			kp_t_ok(T, s != NULL && t != NULL && n == t->size && memcmp(s, t->data, n) == 0, "%s's %s", FILES[f].file,
				fourcc);
			compared++;
		}
	}
	kp_t_eq(T, compared, (int64_t)DEFS_COUNT, "every section's table compared");
}

static const KpTest TESTS[] = {
	{ "kpf_header", kpf_header, 0 },
	{ "kpf_section_payloads", kpf_section_payloads, 0 },
	{ "kpf_derived_metadata", kpf_derived_metadata, 0 },
	{ "kpf_check_expectations", kpf_check_expectations, 0 },
	{ "kpf_rejects_bad_files", kpf_rejects_bad_files, 0 },
	{ "kpf_large_file", kpf_large_file, 0 },
	{ "kpf_crc32_known", kpf_crc32_known, 0 },
	{ "dat_columns_match_python", dat_columns_match_python, 0 },
	{ "dat_layout", dat_layout, 0 },
	{ "dat_rejects_other_kinds", dat_rejects_other_kinds, 0 },
	{ "mission_meta", mission_meta, 0 },
	{ "mission_raw_sections", mission_raw_sections, 0 },
	{ "mission_rules_in_dispatch_order", mission_rules_in_dispatch_order, 0 },
	{ "mission_sounds", mission_sounds, 0 },
	{ "mission_map_strings", mission_map_strings, 0 },
	{ "mission_derived_sections", mission_derived_sections, 0 },
	{ "mission_packs", mission_packs, 0 },
	{ "mission_rejects_other_kinds", mission_rejects_other_kinds, 0 },
	{ "grpmeta_entries_and_atlases", grpmeta_entries_and_atlases, 0 },
	{ "grpmeta_frames", grpmeta_frames, 0 },
	{ "grpmeta_pixel_opaque_matches_python_twin", grpmeta_pixel_opaque_matches_python_twin, 0 },
	{ "grpmeta_run_boundaries", grpmeta_run_boundaries, 0 },
	{ "grpmeta_lo_points", grpmeta_lo_points, 0 },
	{ "grpmeta_image_lookup", grpmeta_image_lookup, 0 },
	{ "scripts_iscript_headers", scripts_iscript_headers, 0 },
	{ "scripts_ai_resolution", scripts_ai_resolution, 0 },
	{ "scripts_kinds", scripts_kinds, 0 },
	{ "rules_matches_python", rules_matches_python, 0 },
	{ "terrain_tileset", terrain_tileset, 0 },
	{ "terrain_import_matches_python", terrain_import_matches_python, 0 },
	{ "terrain_missing_group_stops", terrain_missing_group_stops, 0 },
	{ "palettes_tables", palettes_tables, 0 },
	{ "palettes_game_tables", palettes_game_tables, 0 },
	{ "defs_tables_are_compiled", defs_tables_are_compiled, 0 },
	{ "kpf_table_falls_back_to_the_compiled", kpf_table_falls_back_to_the_compiled, 0 },
	{ "defs_tables_equal_the_converted_sections", defs_tables_equal_the_converted_sections, 0 },
};
KP_SUITE(kp_suite_data, "test_data", TESTS);
