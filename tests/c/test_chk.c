/* A mission read from its scenario (ADR 0018, stage D4: kp_chk, kp_derive, kp_mission_from_files). The converter's
 * fixture scenario (kp_t_fixture_files: campaign\fixture\staredit\scenario.chk) read through the profile's loader
 * tables gives the fixture's mission (its fields: test_data); variants of it are refused as scdata/tests/test_chk.py
 * and the trigger validation refuse them; every campaign mission of the installs of KP_TEST_ORIGINAL (1.17) and
 * KP_TEST_SCR (StarCraft: Remastered) is built from its files, and the two installs' missions are equal but their
 * speech's lengths and rates (Remastered's own recordings). */
#include "kp_test_app.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <sys/stat.h>

#include "kp_data.h"
#include "kp_files.h"
#include "kp_sha256.h"
#include "../../native/kp_core/src/core/kp_campaign.h"
#include "../../native/kp_core/src/core/kp_chk.h"
#include "../../native/kp_core/src/core/kp_derive.h"
#include "../../native/kp_core/src/core/kp_mission.h"
#include "../../native/kp_core/src/core/kp_terrain.h"

#define FIXTURE_CHK KP_T_FIXTURE_FILES "campaign/fixture/staredit/scenario.chk"
#define FIXTURE_WAV_DATA 4410

static const char *const FIXTURES[] = { FIXTURE_CHK, NULL };
#define CHUNK_HEADER 8
#define TRIGGER_ACTIONS 320
#define ACTION_SIZE 32
#define CAMPAIGN_MISSIONS 64

static void release(void *p) { free(p); }

static void *owned(KpT *T, size_t n)
{
	void *p = calloc(1, n ? n : 1);
	kp_t_ok(T, p != NULL, "out of memory");
	kp_t_defer(T, release, p);
	return p;
}

static int bytes_eq(const uint8_t *a, size_t an, const uint8_t *b, size_t bn)
{
	return an == bn && (an == 0 || memcmp(a, b, an) == 0);
}

static void le32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
	p[2] = (uint8_t)(v >> 16);
	p[3] = (uint8_t)(v >> 24);
}

/* --- the fixture's files with a scenario of the test's --- */

typedef struct {
	KpFiles files;
	const uint8_t *chk;
	size_t chk_size;
} Source;

static int same_name(const char *a, const char *b)
{
	for (; *a && *b; a++, b++) {
		int x = *a == '/' ? '\\' : (unsigned char)*a, y = *b == '/' ? '\\' : (unsigned char)*b;
		if (x >= 'A' && x <= 'Z') x += 'a' - 'A';
		if (y >= 'A' && y <= 'Z') y += 'a' - 'A';
		if (x != y) return 0;
	}
	return *a == *b;
}

static uint8_t *source_read(void *ctx, const char *name, size_t *size, char *err, size_t err_size)
{
	const Source *s = (const Source *)ctx;
	uint8_t *b;
	if (!same_name(name, KP_T_FIXTURE_MISSION "\\staredit\\scenario.chk")) {
		return s->files.read(s->files.ctx, name, size, err, err_size);
	}
	err[0] = '\0';
	if (!(b = (uint8_t *)malloc(s->chk_size + 1))) return NULL;
	memcpy(b, s->chk, s->chk_size);
	b[s->chk_size] = 0;
	*size = s->chk_size;
	return b;
}

/* The fixture's files (kp_t_fixture_files) with chk as the fixture scenario's. */
static KpFiles fixture_files(KpT *T, Source *s, const uint8_t *chk, size_t chk_size)
{
	KpFiles f;
	s->files = kp_t_fixture_files(T);
	s->chk = chk;
	s->chk_size = chk_size;
	f = s->files;
	f.read = source_read;
	f.ctx = s;
	return f;
}

/* --- two missions compared --- */

static void mission_free(void *p) { kp_mission_free((KpMission *)p); }

#define SAME(field) kp_t_ok(T, bytes_eq(a->field.data, a->field.size, b->field.data, b->field.size), "%s: " #field, name)

/* Two installs' missions: every part, and of the WAVs their strings and indices (not the lengths and rates of the
 * speech's recordings) */
static void mission_eq(KpT *T, const KpMission *a, const KpMission *b, const char *name)
{
	int k;
	kp_t_ok(T, a->chk_version == b->chk_version && a->era == b->era && a->width == b->width && a->height == b->height
			&& a->tileset == b->tileset, "%s: the header", name);
	kp_t_ok(T, memcmp(a->owners, b->owners, 12) == 0 && memcmp(a->races, b->races, 12) == 0
			&& memcmp(a->force_player, b->force_player, 8) == 0 && memcmp(a->force_flags, b->force_flags, 4) == 0
			&& memcmp(a->force_name, b->force_name, sizeof a->force_name) == 0, "%s: the players and forces", name);
	kp_t_ok(T, a->name_string == b->name_string && a->description_string == b->description_string
			&& a->has_colors == b->has_colors && memcmp(a->colors, b->colors, 8) == 0, "%s: the name and colours", name);
	SAME(tiles);
	SAME(mask);
	SAME(sprites);
	SAME(units);
	SAME(locations);
	SAME(strings);
	SAME(triggers);
	SAME(briefing);
	SAME(properties);
	SAME(walk);
	SAME(regions.data);
	SAME(contours.data);
	kp_t_eq(T, b->rule_count, a->rule_count, "%s: the rules", name);
	for (k = 0; k < a->rule_count && k < b->rule_count; k++)
		kp_t_ok(T, strcmp(a->rules[k].tag, b->rules[k].tag) == 0
				&& bytes_eq(a->rules[k].data.data, a->rules[k].data.size, b->rules[k].data.data, b->rules[k].data.size),
			"%s: rule %d", name, k);
	kp_t_eq(T, b->sound_count, a->sound_count, "%s: the sounds", name);
	for (k = 0; k < a->sound_count && k < b->sound_count; k++) {
		const KpMissionSound *x = &a->sounds[k], *y = &b->sounds[k];
		kp_t_eq(T, y->string_id, x->string_id, "%s: sound %d's string", name, k);
		kp_t_eq(T, y->index, x->index, "%s: sound %d's index (string %u)", name, k, (unsigned)x->string_id);
	}
	kp_t_eq(T, b->pack_count, a->pack_count, "%s: the packs", name);
	for (k = 0; k < a->pack_count && k < b->pack_count; k++) SAME(packs[k]);
}

#undef SAME

/* --- the fixture --- */

static void fixture_mission_from_its_chk(KpT *T)
{
	KpFiles f;
	KpMission *b = (KpMission *)owned(T, sizeof *b);
	KpError err;
	if (!kp_t_ready(T, "test_chk", FIXTURES)) return;
	f = kp_t_fixture_files(T);
	kp_t_ok(T, kp_mission_from_files(b, &f, KP_T_FIXTURE_MISSION, &err) == 0, "%s", err.msg);
	kp_t_defer(T, mission_free, b);
	kp_t_eq(T, b->sound_count, 2, "the WAV strings");
	kp_t_eq(T, b->sounds[0].index, 0, "a.wav found");
	kp_t_eq(T, b->sounds[0].length, FIXTURE_WAV_DATA, "its data");
	kp_t_eq(T, b->sounds[1].index, KP_MISSION_NO_SOUND, "b.wav missing");
}

/* The fixture's scenario with chunk `tag`'s payload replaced (payload NULL: the chunk left out), or with a chunk added
 * at the end (append); the buffer has room for one more chunk header. */
/* base's chunks with each chunk tag replaced by payload (n bytes), or dropped for payload NULL, owned by the test. */
static uint8_t *replaced(KpT *T, const uint8_t *base, size_t base_size, const char *tag, const void *payload, size_t n,
	size_t *size)
{
	size_t at = 0, out = 0;
	uint8_t *v = (uint8_t *)owned(T, base_size + CHUNK_HEADER + n);
	while (at + CHUNK_HEADER <= base_size) {
		size_t len = (size_t)base[at + 4] | (size_t)base[at + 5] << 8 | (size_t)base[at + 6] << 16
			| (size_t)base[at + 7] << 24;
		if (memcmp(base + at, tag, 4) != 0) {
			memcpy(v + out, base + at, CHUNK_HEADER + len);
			out += CHUNK_HEADER + len;
		} else if (payload) {
			memcpy(v + out, tag, 4);
			le32(v + out + 4, (uint32_t)n);
			memcpy(v + out + CHUNK_HEADER, payload, n);
			out += CHUNK_HEADER + n;
		}
		at += CHUNK_HEADER + len;
	}
	*size = out;
	return v;
}

static uint8_t *variant(KpT *T, const char *tag, const void *payload, size_t n, int append, size_t *size)
{
	size_t base_size;
	const uint8_t *base = kp_t_read(T, FIXTURE_CHK, &base_size);
	uint8_t *v;
	if (!append) return replaced(T, base, base_size, tag, payload, n, size);
	v = (uint8_t *)owned(T, base_size + CHUNK_HEADER + n);
	memcpy(v, base, base_size);
	/* payload NULL: the header alone, its payload past the end */
	memcpy(v + base_size, tag, 4);
	le32(v + base_size + 4, (uint32_t)n);
	if (payload && n) memcpy(v + base_size + CHUNK_HEADER, payload, n);
	*size = base_size + CHUNK_HEADER + (payload ? n : 0);
	return v;
}

static void refused(KpT *T, const uint8_t *chk, size_t size, int expansion, const char *want)
{
	KpChk c;
	char err[KP_ERROR_SIZE];
	kp_t_eq(T, kp_chk_read(&c, chk, size, kp_t_fixture_profile(T), expansion, err, sizeof err), -1, "refused (%s)", want);
	kp_t_ok(T, strstr(err, want) != NULL, "the reason: %s (expected %s)", err, want);
}

/* scdata/tests/test_chk.py over the version 205 fixture */
static void chk_variants_are_refused(KpT *T)
{
	static const struct {
		const char *tag;
		size_t size;
		const char *want;
	} RULES[] = {
		{ "DIM ", 3, "'DIM ': 3 bytes, the native loader requires exactly 4" },
		{ "FORC", 21, "'FORC': 21 bytes, the native loader accepts at most 20" },
		{ "TRIG", 2401, "'TRIG': 2401 bytes is not a multiple of 2400" },
		{ "UNIT", 35, "'UNIT': 35 bytes is not a multiple of 36" },
		{ "MTXM", 0x20001, "at most 131072" },
	};
	static const uint8_t VER57[2] = { 57, 0 }, DIM[4] = { 128, 0, 128, 0 };
	uint8_t *zeros = (uint8_t *)owned(T, 0x20001), *v;
	size_t size, k, chk_size;
	const uint8_t *chk;
	KpChk c;
	char err[KP_ERROR_SIZE];
	if (!kp_t_ready(T, "test_chk", FIXTURES)) return;
	chk = kp_t_read(T, FIXTURE_CHK, &chk_size);
	kp_t_eq(T, kp_chk_read(&c, chk, chk_size, kp_t_fixture_profile(T), 1, err, sizeof err), 0, "the fixture: %s", err);
	kp_t_eq(T, c.version, 205, "its version");
	kp_t_ok(T, kp_chk_section(&c, "COLR") && kp_chk_section(&c, "COLR")->size == 8, "COLR read");
	for (k = 0; k < sizeof RULES / sizeof RULES[0]; k++) {
		v = variant(T, RULES[k].tag, zeros, RULES[k].size, 0, &size);
		refused(T, v, size, 1, RULES[k].want);
	}
	v = variant(T, "VCOD", "other", 5, 0, &size);
	refused(T, v, size, 1, "'VCOD' differs from the executable's VCOD resource");
	v = variant(T, "UPRP", NULL, 0, 0, &size);
	refused(T, v, size, 1, "missing required section 'UPRP'");
	v = variant(T, "ABCD", "", 0, 1, &size);
	refused(T, v, size, 1, "'ABCD' is neither read by the version 205 loader nor allowlisted");
	v = variant(T, "DIM ", DIM, sizeof DIM, 1, &size);
	refused(T, v, size, 1, "duplicate section 'DIM '");
	v = variant(T, "VER ", VER57, sizeof VER57, 0, &size);
	refused(T, v, size, 1, "CHK version 57 has no native loader descriptor");
	refused(T, chk, chk_size, 0, "CHK version 205 requires Brood War");
	/* an ignored tag whose payload runs past the end is enumerated, not read; a read one is refused */
	v = variant(T, "ISOM", NULL, 100, 1, &size);
	kp_t_eq(T, kp_chk_read(&c, v, size, kp_t_fixture_profile(T), 1, err, sizeof err), 0, "a truncated ISOM: %s", err);
	v = variant(T, "TRIG", NULL, 0, 0, &size);
	memcpy(v + size, "TRIG\x60\x09\0\0", CHUNK_HEADER);
	refused(T, v, size + CHUNK_HEADER, 1, "'TRIG' at");
	memcpy(v + size, "ISOM\xf8\xff\xff\xff", CHUNK_HEADER);
	refused(T, v, size + CHUNK_HEADER, 1, "negative size -8");
}

/* A custom map's loads (scdata/tests/test_chk.py's game loads over the version 205 fixture): an unread chunk is
 * skipped, STR keeps the last chunk, MTXM and MASK lay theirs over each other, an appending section's repeat is
 * refused; a melee load reads no triggers. */
static void chk_game_loads(KpT *T)
{
	static const uint8_t STR[2] = { 0, 0 }, TILES[2] = { 1, 2 }, FOG[1] = { 7 };
	const KpDefsProfile *profile = kp_t_fixture_profile(T);
	const KpChkSection *s, *base_tiles;
	uint8_t *v, *unit = (uint8_t *)owned(T, 36);
	size_t size, chk_size;
	const uint8_t *chk;
	KpChk c, base;
	char err[KP_ERROR_SIZE];
	if (!kp_t_ready(T, "test_chk", FIXTURES)) return;
	chk = kp_t_read(T, FIXTURE_CHK, &chk_size);
	kp_t_eq(T, kp_chk_read(&base, chk, chk_size, profile, 1, err, sizeof err), 0, "the fixture: %s", err);
	base_tiles = kp_chk_section(&base, "MTXM");

	v = variant(T, "ABCD", "", 0, 1, &size);
	kp_t_eq(T, kp_chk_load(&c, v, size, profile, 1, KP_CHK_UMS, err, sizeof err), 0, "an unread tag: %s", err);
	kp_chk_free(&c);
	v = variant(T, "STR ", STR, sizeof STR, 1, &size);
	kp_t_eq(T, kp_chk_load(&c, v, size, profile, 1, KP_CHK_UMS, err, sizeof err), 0, "a second STR: %s", err);
	s = kp_chk_section(&c, "STR ");
	kp_t_ok(T, s && bytes_eq(s->data, s->size, STR, sizeof STR) && !s->owned, "the last STR read");
	kp_chk_free(&c);
	v = variant(T, "MTXM", TILES, sizeof TILES, 1, &size);
	kp_t_eq(T, kp_chk_load(&c, v, size, profile, 1, KP_CHK_UMS, err, sizeof err), 0, "a second MTXM: %s", err);
	s = kp_chk_section(&c, "MTXM");
	kp_t_ok(T, s && base_tiles && s->owned && s->size == base_tiles->size && memcmp(s->data, TILES, 2) == 0
			&& memcmp(s->data + 2, base_tiles->data + 2, s->size - 2) == 0,
		"the second MTXM over the first");
	kp_chk_free(&c);
	v = variant(T, "MASK", FOG, sizeof FOG, 1, &size);
	kp_t_eq(T, kp_chk_load(&c, v, size, profile, 1, KP_CHK_UMS, err, sizeof err), 0, "a second MASK: %s", err);
	s = kp_chk_section(&c, "MASK");
	kp_t_ok(T, s && s->owned && s->data[0] == 7, "the second MASK over the first");
	kp_chk_free(&c);
	v = variant(T, "MTXM", NULL, 100, 1, &size);
	kp_t_eq(T, kp_chk_load(&c, v, size, profile, 1, KP_CHK_UMS, err, sizeof err), -1, "a truncated second MTXM");
	kp_t_ok(T, strstr(err, "'MTXM' at") != NULL, "the reason: %s", err);
	v = variant(T, "UNIT", unit, 36, 1, &size);
	kp_t_eq(T, kp_chk_load(&c, v, size, profile, 1, KP_CHK_UMS, err, sizeof err), -1, "a second UNIT");
	kp_t_ok(T, strstr(err, "duplicate section 'UNIT'") != NULL, "the reason: %s", err);

	kp_t_eq(T, kp_chk_load(&c, chk, chk_size, profile, 1, KP_CHK_MELEE, err, sizeof err), 0, "a melee load: %s", err);
	kp_t_ok(T, !kp_chk_section(&c, "TRIG") && !kp_chk_section(&c, "MASK") && kp_chk_section(&c, "COLR"),
		"the melee group's sections");
	kp_chk_free(&c);
	v = variant(T, "UNIT", NULL, 0, 0, &size);
	kp_t_eq(T, kp_chk_load(&c, v, size, profile, 1, KP_CHK_MELEE, err, sizeof err), -1, "a melee map without UNIT");
	kp_t_ok(T, strstr(err, "missing required section 'UNIT'") != NULL, "the reason: %s", err);
}

/* The fixture's scenario v (size bytes) loaded as its mission: 0, else -1 with the reason in why. */
static int load_variant(KpT *T, const uint8_t *v, size_t size, char *why, size_t why_size)
{
	Source s;
	KpFiles f;
	KpMission *m = (KpMission *)owned(T, sizeof *m);
	KpError err;
	int rc;
	f = fixture_files(T, &s, v, size);
	rc = kp_mission_from_files(m, &f, KP_T_FIXTURE_MISSION, &err);
	if (rc == 0) kp_mission_free(m);
	snprintf(why, why_size, "%s", err.msg);
	return rc;
}

/* A patch of the fixture's TRIG: `width` bytes (1, 2 or 4) little-endian at `at`. */
typedef struct {
	size_t at;
	uint32_t value;
	int width;
} Patch;

/* The fixture's triggers with the patches applied in order: 0 when the mission loads, else -1 with the reason in
 * why. */
static int with_patches(KpT *T, const Patch *patch, size_t count, char *why, size_t why_size)
{
	size_t chk_size, size, k;
	const uint8_t *chk = kp_t_read(T, FIXTURE_CHK, &chk_size);
	uint8_t *v = variant(T, "TRIG", NULL, 0, 0, &size), *trig = NULL;
	const uint8_t *p = chk;
	int i;
	for (k = 0; k + CHUNK_HEADER <= chk_size;) { /* the fixture's TRIG, last */
		size_t len = (size_t)p[k + 4] | (size_t)p[k + 5] << 8 | (size_t)p[k + 6] << 16 | (size_t)p[k + 7] << 24;
		if (memcmp(p + k, "TRIG", 4) == 0) {
			memcpy(v + size, p + k, CHUNK_HEADER + len);
			trig = v + size + CHUNK_HEADER;
			size += CHUNK_HEADER + len;
		}
		k += CHUNK_HEADER + len;
	}
	kp_t_ok(T, trig != NULL, "the fixture's TRIG");
	for (k = 0; k < count; k++)
		for (i = 0; i < patch[k].width; i++) trig[patch[k].at + i] = (uint8_t)(patch[k].value >> (8 * i));
	return load_variant(T, v, size, why, why_size);
}

/* The fixture's first trigger with byte `at` set to `value` (and the u32 at `at32` to `v32` when at32 >= 0). */
static int with_trigger(KpT *T, size_t at, uint8_t value, long at32, uint32_t v32, char *why, size_t why_size)
{
	Patch p[2];
	p[0].at = at;
	p[0].value = value;
	p[0].width = 1;
	p[1].at = at32 >= 0 ? (size_t)at32 : 0;
	p[1].value = v32;
	p[1].width = 4;
	return with_patches(T, p, at32 >= 0 ? 2 : 1, why, why_size);
}

/* scdata/analyze/triggers.py validate: the reachable conditions and actions past the tables are refused; ignored ones
 * are left, as are Opponents and the actions 52 and 53 (phase M's stage M2) */
static void trigger_validation(KpT *T)
{
	enum { COND_TYPE = 0x0f, COND_FIELDS = 0x0e, ACT = TRIGGER_ACTIONS, ACT_NUMBER = 0x14, ACT_TYPE = 0x1a };
	char why[KP_ERROR_SIZE];
	if (!kp_t_ready(T, "test_chk", FIXTURES)) return;
	kp_t_eq(T, with_trigger(T, COND_TYPE, 24, -1, 0, why, sizeof why), -1, "condition 24");
	kp_t_ok(T, strstr(why, "trigger 0 condition 0: type 24 (the table has 24)") != NULL, "%s", why);
	kp_t_eq(T, with_trigger(T, COND_TYPE, 14, -1, 0, why, sizeof why), 0, "opponents: %s", why);
	kp_t_eq(T, with_trigger(T, ACT + ACT_TYPE, 60, -1, 0, why, sizeof why), -1, "action 60");
	kp_t_ok(T, strstr(why, "trigger 0 action 0: type 60 (the table has 60)") != NULL, "%s", why);
	kp_t_eq(T, with_trigger(T, ACT + ACT_TYPE, 52, -1, 0, why, sizeof why), 0, "action 52: %s", why);
	kp_t_eq(T, with_trigger(T, ACT + ACT_TYPE, 53, -1, 0, why, sizeof why), 0, "action 53: %s", why);
	kp_t_eq(T, with_trigger(T, ACT + ACT_TYPE, 46, ACT + ACT_NUMBER, 256, why, sizeof why), -1, "Order to 256");
	kp_t_ok(T, strstr(why, "action 0: Order to location 256") != NULL, "%s", why);
	kp_t_eq(T, with_trigger(T, ACT + ACT_TYPE, 46, ACT + ACT_NUMBER, 255, why, sizeof why), 0, "Order to 255: %s", why);
	kp_t_eq(T, with_trigger(T, ACT + ACT_TYPE, 39, ACT + ACT_NUMBER, 0x100, why, sizeof why), 0,
		"Move Unit without a location: %s", why);
	/* type 24 with the flags' bit 0x02 (comparison, type, resource, flags from +0x0e): never evaluated */
	kp_t_eq(T, with_trigger(T, COND_TYPE, 24, COND_FIELDS, 0x02001800u, why, sizeof why), 0, "an ignored condition: %s",
		why);
	/* type 60 with the flags' bit 0x02 (type, modifier, flags from +0x1a): never run */
	kp_t_eq(T, with_trigger(T, ACT + ACT_TYPE, 60, ACT + ACT_TYPE, 0x0002003cu, why, sizeof why), 0,
		"an ignored action: %s", why);
}

/* The runtime's stops a scenario decides (ADR 0023) are refused when its mission loads: the counters of a direct
 * slot past the players or of a unit type past the table, Kill and Remove Unit of such a slot, Set Alliance with slot
 * 12, Set Deaths past the aggregates, a leaderboard's unit type, a map string without its terminator, a start location
 * of slot 12; the same records the runtime takes are left, Flags among them (Capture the Flag's, phase M's stage M2). */
static void runtime_stops_are_refused(KpT *T)
{
	enum {
		C_LOCATION = 0x00, C_PLAYER = 0x04, C_UNIT = 0x0c, C_TYPE = 0x0f, A = TRIGGER_ACTIONS, A_LOCATION = A,
		A_PLAYER = A + 0x10, A_UNIT = A + 0x18, A_TYPE = A + 0x1a, A3 = A + 3 * 32
	};
	static const struct {
		Patch patch[3];
		const char *want; /* NULL: loads */
	} CASES[] = {
		{ { { C_TYPE, 15, 1 }, { C_PLAYER, 12, 4 } }, "trigger 0 condition 0: a counter of player slot 12" },
		{ { { C_TYPE, 4, 1 }, { C_PLAYER, 27, 4 } }, "condition 0: a counter of player slot 27" },
		{ { { C_TYPE, 21, 1 }, { C_PLAYER, 13, 4 } }, NULL }, /* the current player */
		{ { { C_TYPE, 2, 1 }, { C_PLAYER, 17, 4 } }, NULL },  /* a group */
		{ { { C_TYPE, 3, 1 }, { C_PLAYER, 12, 4 } }, NULL },  /* Bring without a location counts nothing */
		{ { { C_TYPE, 3, 1 }, { C_PLAYER, 12, 4 }, { C_LOCATION, 1, 1 } }, "a counter of player slot 12" },
		{ { { C_TYPE, 15, 1 }, { C_UNIT, 233, 2 } }, "condition 0: a counter of unit type 233" },
		{ { { C_TYPE, 6, 1 }, { C_UNIT, 228, 2 } }, "a counter of unit type 228" },
		{ { { C_TYPE, 15, 1 }, { C_UNIT, 229, 2 } }, NULL }, /* Any Unit */
		{ { { C_TYPE, 7, 1 }, { C_UNIT, 228, 2 } }, NULL },  /* Commands the Most At without a location */
		{ { { A_TYPE, 22, 1 }, { A_PLAYER, 12, 4 } }, "trigger 0 action 0: Kill Unit of player slot 12" },
		{ { { A_TYPE, 24, 1 }, { A_PLAYER, 30, 4 } }, "action 0: Remove Unit of player slot 30" },
		{ { { A_TYPE, 22, 1 }, { A_PLAYER, 13, 4 } }, NULL },
		{ { { A_TYPE, 23, 1 }, { A_PLAYER, 12, 4 } }, NULL }, /* Kill Unit At takes any slot */
		{ { { A_TYPE, 57, 1 }, { A_PLAYER, 12, 4 } }, "action 0: Set Alliance with player slot 12" },
		{ { { A_TYPE, 57, 1 }, { A_PLAYER, 27, 4 } }, NULL }, /* a selector from 27 sets nothing */
		{ { { A_TYPE, 45, 1 }, { A_UNIT, 233, 2 } }, "action 0: Set Deaths of unit type 233" },
		{ { { A_TYPE, 45, 1 }, { A_UNIT, 233, 2 }, { A_PLAYER, 8, 4 } }, NULL }, /* slots from 8 are left alone */
		{ { { A_TYPE, 45, 1 }, { A_UNIT, 228, 2 } }, NULL },
		{ { { A_TYPE, 20, 1 }, { A_UNIT, 228, 2 } }, "action 0: a leaderboard of unit type 228" },
		{ { { A_TYPE, 34, 1 }, { A_UNIT, 228, 2 }, { A_LOCATION, 1, 1 } }, "a leaderboard of unit type 228" },
		{ { { A_TYPE, 18, 1 }, { A_UNIT, 228, 2 } }, NULL }, /* Control At without a location changes nothing */
		{ { { A_TYPE, 44, 1 }, { A_UNIT, 215, 2 }, { A_LOCATION, 1, 1 } }, NULL }, /* a Flag */
		{ { { A_TYPE, 11, 1 }, { A_UNIT, 215, 2 } }, NULL }, /* no location: nothing is made */
		{ { { A3 + 0x1a, 22, 1 }, { A3 + 0x10, 12, 4 } }, NULL }, /* after the first action of type 0: never run */
		{ { { A_TYPE, 22, 1 }, { A_PLAYER, 12, 4 }, { A + 0x1c, 2, 1 } }, NULL }, /* ignored */
	};
	static const uint8_t UNITS[3][2] = { { 214, 12 }, { 214, 11 }, { 215, 0 } }; /* type, owner */
	uint8_t record[3][36];
	char why[KP_ERROR_SIZE];
	const uint8_t *chk;
	uint8_t *strs, *v;
	size_t k, n, chk_size, size, strs_size = 0;
	if (!kp_t_ready(T, "test_chk", FIXTURES)) return;
	for (k = 0; k < sizeof CASES / sizeof CASES[0]; k++) {
		for (n = 0; n < 3 && CASES[k].patch[n].width; n++) {
		}
		if (CASES[k].want) {
			kp_t_eq(T, with_patches(T, CASES[k].patch, n, why, sizeof why), -1, "case %u refused", (unsigned)k);
			kp_t_ok(T, strstr(why, CASES[k].want) != NULL, "case %u: %s", (unsigned)k, why);
		} else {
			kp_t_eq(T, with_patches(T, CASES[k].patch, n, why, sizeof why), 0, "case %u: %s", (unsigned)k, why);
		}
	}
	memset(record, 0, sizeof record);
	for (k = 0; k < 3; k++) record[k][8] = UNITS[k][0], record[k][16] = UNITS[k][1];
	v = variant(T, "UNIT", record[0], sizeof record[0], 0, &size);
	kp_t_eq(T, load_variant(T, v, size, why, sizeof why), -1, "a start location of slot 12");
	kp_t_ok(T, strstr(why, "UNIT: unit 0: a start location of player 12") != NULL, "%s", why);
	v = variant(T, "UNIT", record[1], sizeof record[1], 0, &size);
	kp_t_eq(T, load_variant(T, v, size, why, sizeof why), 0, "a start location of slot 11: %s", why);
	v = variant(T, "UNIT", record[2], sizeof record[2], 0, &size);
	kp_t_eq(T, load_variant(T, v, size, why, sizeof why), 0, "a Flag: %s", why);
	/* the fixture's STR (4 strings, its last byte a terminator) with string 4 past the end, then at the last byte, then
	 * without the last terminator: each loads, as the original's (a string past STR reads empty, one STR does not end
	 * ends at STR's end: ADR 0024) */
	chk = kp_t_read(T, FIXTURE_CHK, &chk_size);
	for (k = 0; k + CHUNK_HEADER <= chk_size; k += CHUNK_HEADER + n) {
		n = (size_t)chk[k + 4] | (size_t)chk[k + 5] << 8 | (size_t)chk[k + 6] << 16 | (size_t)chk[k + 7] << 24;
		if (memcmp(chk + k, "STR ", 4) == 0) strs_size = n, strs = (uint8_t *)owned(T, n), memcpy(strs, chk + k + 8, n);
	}
	kp_t_ok(T, strs_size >= 10 && strs[strs_size - 1] == 0, "the fixture's STR");
	if (strs_size < 10) return;
	strs[8] = (uint8_t)(strs_size + 7), strs[9] = 0;
	v = variant(T, "STR ", strs, strs_size, 0, &size);
	kp_t_eq(T, load_variant(T, v, size, why, sizeof why), 0, "string 4 past STR: %s", why);
	strs[8] = (uint8_t)(strs_size - 1);
	v = variant(T, "STR ", strs, strs_size, 0, &size);
	kp_t_eq(T, load_variant(T, v, size, why, sizeof why), 0, "string 4 empty at the last byte: %s", why);
	strs[strs_size - 1] = 'x';
	v = variant(T, "STR ", strs, strs_size, 0, &size);
	kp_t_eq(T, load_variant(T, v, size, why, sizeof why), 0, "STR without its last terminator: %s", why);
}

/* --- the installs --- */

static void campaign_free(void *p) { kp_campaign_free((KpCampaign *)p); }

/* An install's files and its campaign (released when the test ends). */
static const KpCampaign *install_campaign(KpT *T, const char *env, KpFiles *f)
{
	KpCampaign *c = (KpCampaign *)owned(T, sizeof *c);
	KpError err;
	kp_res_files(kp_t_install(T, env), f);
	kp_t_ok(T, kp_campaign_from_files(c, f, &err) == 0, "%s", err.msg);
	kp_t_defer(T, campaign_free, c);
	return c;
}

/* Mission k of an install's campaign, released when the test ends or at kp_t_release. */
static KpMission *campaign_mission(KpT *T, const KpFiles *f, const KpCampaign *c, int k)
{
	KpMission *m = (KpMission *)owned(T, sizeof *m);
	KpError err;
	kp_t_ok(T, kp_mission_from_files(m, f, c->directories[k], &err) == 0, "%s: %s", c->directories[k], err.msg);
	kp_t_defer(T, mission_free, m);
	return m;
}

static void ai_bins_free(void *p) { kp_ai_bins_free((KpAiBins *)p); }

/* Every mission of the campaign loads (the checks of kp_mission_from_chk) and its AI scripts pass kp_mission_check_ai
 * against the install's binaries. */
static void missions_built(KpT *T, const char *env)
{
	KpFiles f;
	const KpCampaign *c = install_campaign(T, env, &f);
	KpAiBins *bins = (KpAiBins *)owned(T, sizeof *bins);
	KpError err;
	int k, built = 0;
	kp_t_ok(T, kp_ai_bins_from_files(bins, &f, &err) == 0, "%s", err.msg);
	kp_t_defer(T, ai_bins_free, bins);
	for (k = 1; k < c->directory_count && c->directories[k][0]; k++) { /* kp_data_catalog's rows */
		int mark = kp_t_mark(T);
		KpMission *m = campaign_mission(T, &f, c, k);
		kp_t_ok(T, kp_mission_check_ai(m, bins, &err) == 0, "%s: %s", c->directories[k], err.msg);
		kp_t_release(T, mark);
		built++;
	}
	kp_t_eq(T, built, CAMPAIGN_MISSIONS, "the campaign's missions built");
}

static void original_missions(KpT *T) { missions_built(T, "KP_TEST_ORIGINAL"); }

static void remastered_missions(KpT *T) { missions_built(T, "KP_TEST_SCR"); }

/* 1.17's missions equal Remastered's but the speech's WAV records' lengths and rates (Remastered's own recordings): the
 * same files are found at the same indices, which a language's speech is keyed by (kp_app_world.c). */
static void installs_missions_equal(KpT *T)
{
	KpFiles fa, fb;
	const KpCampaign *a = install_campaign(T, "KP_TEST_ORIGINAL", &fa), *b = install_campaign(T, "KP_TEST_SCR", &fb);
	int k, compared = 0;
	kp_t_eq(T, b->directory_count, a->directory_count, "the campaign's directories");
	for (k = 0; k < a->directory_count; k++) {
		int mark = kp_t_mark(T);
		kp_t_eq_str(T, b->directories[k], a->directories[k], "mission %d's directory", k);
		if (k == 0 || !a->directories[k][0]) continue; /* index 0 is no mission */
		mission_eq(T, campaign_mission(T, &fa, a, k), campaign_mission(T, &fb, b, k), a->directories[k]);
		kp_t_release(T, mark);
		compared++;
	}
	kp_t_eq(T, compared, CAMPAIGN_MISSIONS, "the campaign's missions compared");
}

/* --- custom maps (ADR 0024) --- */

typedef struct {
	KpT *T;
	const KpFiles *files;
	int maps, melee, ums;
} MapsWalk;

static void map_loads(MapsWalk *w, const char *path)
{
	static const KpChkLoad LOADS[] = { KP_CHK_MELEE, KP_CHK_UMS };
	int k;
	w->maps++;
	for (k = 0; k < 2; k++) {
		KpDataMap map;
		KpMission m;
		KpError err;
		if (kp_data_map_open(&map, w->files, path, &err) != 0) {
			kp_t_ok(w->T, 0, "%s", err.msg);
			return;
		}
		if (kp_data_map_mission(&map, LOADS[k], &m, &err) == 0) {
			if (k == 0) w->melee++; else w->ums++;
			kp_mission_free(&m);
		} else {
			kp_t_ok(w->T, 0, "%s %s: %s", k == 0 ? "melee" : "ums", path, err.msg);
		}
		kp_data_map_close(&map);
	}
}

static void maps_walk(MapsWalk *w, const char *dir)
{
	DIR *d = opendir(dir);
	struct dirent *e;
	char path[1024];
	kp_t_ok(w->T, d != NULL, "%s", dir);
	if (!d) return;
	while ((e = readdir(d)) != NULL) {
		size_t n = strlen(e->d_name);
		struct stat st;
		if (e->d_name[0] == '.') continue;
		snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
		if (stat(path, &st) != 0) continue;
		if (S_ISDIR(st.st_mode)) maps_walk(w, path);
		else if (n > 4 && (strcasecmp(e->d_name + n - 4, ".scm") == 0 || strcasecmp(e->d_name + n - 4, ".scx") == 0))
			map_loads(w, path);
	}
	closedir(d);
}

/* Every map of the 1.17 install's maps folder loads as a melee game's and as a use map settings game's (Capture the
 * Flag's Flags and the Opponents condition since phase M's stage M2). */
static void install_maps_load(KpT *T)
{
	const char *root = getenv("KP_TEST_ORIGINAL");
	char dir[1024];
	KpFiles f;
	MapsWalk w;
	memset(&w, 0, sizeof w);
	if (!root || !root[0]) kp_t_skip(T, "no KP_TEST_ORIGINAL");
	kp_res_files(kp_t_install(T, "KP_TEST_ORIGINAL"), &f);
	w.T = T;
	w.files = &f;
	snprintf(dir, sizeof dir, "%s/maps", root);
	maps_walk(&w, dir);
	kp_t_ok(T, w.maps > 0, "the install's maps");
	kp_t_eq(T, w.melee, w.maps, "every map as a melee game's");
	kp_t_eq(T, w.ums, w.maps, "every map as a use map settings game's");
	kp_t_log(T, "%d maps: %d load as melee games', %d as use map settings games'", w.maps, w.melee, w.ums);
}

/* A custom map's mission from the fixture's scenario: a use map settings load's parts are the campaign's; a melee load
 * reads no triggers, locations or briefing and its fog mask is all 0xff; MTXM's chunks are laid over each other, and
 * a map whose tiles they leave unwritten is refused. */
static void map_missions(KpT *T)
{
	static const uint8_t TILES[2] = { 1, 2 }, NO_PACKS[1] = { 0 };
	Source src;
	KpFiles f;
	KpMission *a = (KpMission *)owned(T, sizeof *a), *b = (KpMission *)owned(T, sizeof *b);
	KpError err;
	size_t chk_size, size, k;
	const uint8_t *chk;
	uint8_t *copy, *v;
	if (!kp_t_ready(T, "test_chk", FIXTURES)) return;
	chk = kp_t_read(T, FIXTURE_CHK, &chk_size);
	f = fixture_files(T, &src, chk, chk_size);
	kp_t_ok(T, kp_mission_from_files(a, &f, KP_T_FIXTURE_MISSION, &err) == 0, "the campaign's: %s", err.msg);
	kp_t_defer(T, mission_free, a);
	copy = (uint8_t *)malloc(chk_size);
	if (copy) memcpy(copy, chk, chk_size);
	kp_t_ok(T, kp_mission_from_map(b, &f, copy, chk_size, "fixture", KP_CHK_UMS, NO_PACKS, 1, &err) == 0, "ums: %s", err.msg);
	kp_t_defer(T, mission_free, b);
	{
		const KpBytes *pa[] = { &a->tiles, &a->mask, &a->sprites, &a->units, &a->locations, &a->strings, &a->triggers,
			&a->briefing, &a->properties, &a->walk },
			*pb[] = { &b->tiles, &b->mask, &b->sprites, &b->units, &b->locations, &b->strings, &b->triggers,
				&b->briefing, &b->properties, &b->walk };
		for (k = 0; k < sizeof pa / sizeof pa[0]; k++)
			kp_t_ok(T, bytes_eq(pa[k]->data, pa[k]->size, pb[k]->data, pb[k]->size), "use map settings: part %u",
				(unsigned)k);
	}
	kp_mission_free(b);
	copy = (uint8_t *)malloc(chk_size);
	if (copy) memcpy(copy, chk, chk_size);
	kp_t_ok(T, kp_mission_from_map(b, &f, copy, chk_size, "fixture", KP_CHK_MELEE, NO_PACKS, 1, &err) == 0, "melee: %s",
		err.msg);
	kp_t_ok(T, bytes_eq(a->tiles.data, a->tiles.size, b->tiles.data, b->tiles.size), "melee: the tiles");
	kp_t_eq(T, b->triggers.size, 0, "melee: no triggers");
	kp_t_eq(T, b->locations.size, 0, "melee: no locations");
	kp_t_eq(T, b->briefing.size, 0, "melee: no briefing");
	kp_t_eq(T, b->mask.size, (uint32_t)b->width * b->height, "melee: the mask's size");
	for (k = 0; k < b->mask.size && b->mask.data[k] == 0xff; k++) {
	}
	kp_t_eq(T, k, b->mask.size, "melee: the mask all 0xff");
	kp_mission_free(b);
	v = variant(T, "MTXM", TILES, sizeof TILES, 1, &size);
	copy = (uint8_t *)malloc(size);
	if (copy) memcpy(copy, v, size);
	kp_t_ok(T, kp_mission_from_map(b, &f, copy, size, "fixture", KP_CHK_UMS, NO_PACKS, 1, &err) == 0, "a second MTXM: %s",
		err.msg);
	kp_t_ok(T, b->tiles.size == a->tiles.size && memcmp(b->tiles.data, TILES, 2) == 0
			&& memcmp(b->tiles.data + 2, a->tiles.data + 2, a->tiles.size - 2) == 0,
		"the second MTXM over the first");
	kp_mission_free(b);
	v = variant(T, "MTXM", TILES, sizeof TILES, 0, &size);
	copy = (uint8_t *)malloc(size);
	if (copy) memcpy(copy, v, size);
	kp_t_eq(T, kp_mission_from_map(b, &f, copy, size, "fixture", KP_CHK_UMS, NO_PACKS, 1, &err), -1, "a short MTXM");
	kp_t_ok(T, strstr(err.msg, "MTXM: 2 bytes leave tiles") != NULL, "the reason: %s", err.msg);
	copy = (uint8_t *)malloc(chk_size);
	if (copy) memcpy(copy, chk, chk_size);
	kp_t_eq(T, kp_mission_from_map(b, &f, copy, chk_size, "fixture", KP_CHK_HEADER, NO_PACKS, 1, &err), -1,
		"the header's load is no game's");
}

/* A map's header for the lists (LoadMapHeader 0x46b5c0 from LoadMapEntryInfo 0x45d870): the header group alone (a map
 * without the game's sections reads), its size, tileset (ERA's low 3 bits), owners and races (User Select marked), the
 * counts (players: owner types 3..7, 0x478e70; computers: types 1 and 5, 0x478ea0; humans: type 6, 0x479550) and the
 * first human slot, SPRP's title and description by their strings (none: ""), FORC's slot forces, flags and force
 * names (a null string marked unnamed; a short chunk zero-filled, LoadForceChunk 0x443320); a version past the
 * build's or a scenario without OWNR is refused with the version still read. */
#define HEADER_SLOTS 12
static void map_header_reads_the_lists_fields(KpT *T)
{
	/* slots 0..7: inactive, computer, rescuable, human, neutral, human, computer, unused; 8..11 inactive */
	static const uint8_t OWNR[HEADER_SLOTS] = { 0, 1, 3, 6, 7, 6, 5, 4, 0, 0, 0, 0 };
	static const uint8_t SIDE[HEADER_SLOTS] = { 0, 1, 2, 5, 7, 5, 2, 1, 0, 0, 0, 0 };
	static const uint8_t DIM[4] = { 96, 0, 64, 0 }, ERA[2] = { 0x0d, 0 }, SPRP[4] = { 1, 0, 2, 0 },
		NO_TITLE[4] = { 0, 0, 2, 0 };
	/* STR: u16 count 2, two offsets, then the strings */
	static const uint8_t STRS[] = { 2, 0, 6, 0, 18, 0, 'F', 'i', 'x', 't', 'u', 'r', 'e', ' ', 'M', 'a', 'p', 0,
		'I', 't', 's', ' ', 't', 'e', 'x', 't', 0 };
	/* FORC: the slots' forces, the force names' strings (2, null, 1, null), the force flags */
	static const uint8_t FORC[20] = { 0, 1, 1, 2, 3, 0, 1, 2, 2, 0, 0, 0, 1, 0, 0, 0, 1, 3, 0, 0x0f };
	static const uint8_t SHORT_FORC[10] = { 3, 2, 1, 0, 3, 2, 1, 0, 1, 0 };
	static const uint8_t FORCE_FLAGS[KP_MAP_HEADER_FORCES] = { 1, 3, 0, 0x0f };
	static const char *const FORCE_NAMES[KP_MAP_HEADER_FORCES] = { "Its text", "", "Fixture Map", "" };
	static const struct { const char *tag; const uint8_t *data; size_t n; } PARTS[] = {
		{ "OWNR", OWNR, sizeof OWNR }, { "SIDE", SIDE, sizeof SIDE }, { "DIM ", DIM, sizeof DIM },
		{ "ERA ", ERA, sizeof ERA }, { "SPRP", SPRP, sizeof SPRP }, { "STR ", STRS, sizeof STRS },
		{ "FORC", FORC, sizeof FORC }, { "UNIT", NULL, 0 } }; /* no game section: the header still reads */
	const KpDefsProfile *profile = kp_t_fixture_profile(T);
	KpMapHeader h;
	char err[KP_ERROR_SIZE];
	const uint8_t *v;
	size_t size, k;
	int p;
	if (!kp_t_ready(T, "test_chk", FIXTURES)) return;
	v = kp_t_read(T, FIXTURE_CHK, &size);
	for (k = 0; k < sizeof PARTS / sizeof PARTS[0]; k++) v = replaced(T, v, size, PARTS[k].tag, PARTS[k].data, PARTS[k].n, &size);
	kp_t_eq(T, kp_map_header(&h, v, size, profile, 1, err, sizeof err), 0, "the header: %s", err);
	kp_t_eq(T, h.version, 205, "its version");
	kp_t_ok(T, h.width == 96 && h.height == 64, "its size %ux%u", h.width, h.height);
	kp_t_eq(T, h.tileset, 5, "ERA's low 3 bits");
	for (p = 0; p < KP_MAP_HEADER_SLOTS; p++) {
		kp_t_ok(T, h.owners[p] == OWNR[p] && h.races[p] == SIDE[p], "slot %d's owner and race", p);
		kp_t_eq(T, h.user_select[p], SIDE[p] == KP_MAP_RACE_USER_SELECT, "slot %d's User Select", p);
	}
	kp_t_eq(T, h.players, 6, "the slots of owner types 3..7");
	kp_t_eq(T, h.computers, 2, "the computers: types 1 and 5");
	kp_t_eq(T, h.humans, 2, "the humans: type 6");
	kp_t_eq(T, h.first_human, 3, "the first human slot");
	kp_t_eq_str(T, h.title, "Fixture Map", "SPRP's title");
	kp_t_eq_str(T, h.description, "Its text", "SPRP's description");
	for (p = 0; p < KP_MAP_HEADER_SLOTS; p++) kp_t_eq(T, h.forces[p], FORC[p], "slot %d's force", p);
	for (p = 0; p < KP_MAP_HEADER_FORCES; p++) {
		kp_t_eq(T, h.force_flags[p], FORCE_FLAGS[p], "force %d's flags", p + 1);
		kp_t_eq(T, h.force_named[p], FORCE_NAMES[p][0] != 0, "force %d named", p + 1);
		kp_t_eq_str(T, h.force_names[p], FORCE_NAMES[p], "force %d's name", p + 1);
	}
	v = replaced(T, v, size, "FORC", SHORT_FORC, sizeof SHORT_FORC, &size);
	kp_t_eq(T, kp_map_header(&h, v, size, profile, 1, err, sizeof err), 0, "a short FORC: %s", err);
	for (p = 0; p < KP_MAP_HEADER_SLOTS; p++) kp_t_eq(T, h.forces[p], SHORT_FORC[p], "a short FORC: slot %d's force", p);
	for (p = 0; p < KP_MAP_HEADER_FORCES; p++) {
		kp_t_eq(T, h.force_flags[p], 0, "a short FORC: force %d's flags zero-filled", p + 1);
		kp_t_eq(T, h.force_named[p], p == 0, "a short FORC: force %d named", p + 1);
	}
	kp_t_eq_str(T, h.force_names[0], "Fixture Map", "a short FORC: force 1's name");
	v = replaced(T, v, size, "SPRP", NO_TITLE, sizeof NO_TITLE, &size);
	kp_t_eq(T, kp_map_header(&h, v, size, profile, 1, err, sizeof err), 0, "no title: %s", err);
	kp_t_eq_str(T, h.title, "", "no title: the lists take the file's name");
	kp_t_eq(T, kp_map_header(&h, v, size, profile, 0, err, sizeof err), -1, "without Brood War");
	kp_t_eq(T, h.version, 205, "the version still read (the lists' gluAll 60)");
	v = replaced(T, v, size, "OWNR", NULL, 0, &size);
	kp_t_eq(T, kp_map_header(&h, v, size, profile, 1, err, sizeof err), -1, "no OWNR");
	kp_t_eq(T, h.first_human, -1, "no human slot read");
}

/* --- the terrain's derived parts against the original's (docs/research/evidence) --- */

#define EVIDENCE "docs/research/evidence/"
#define GRAPHS EVIDENCE "native-campaign-regions-probes.json"
#define CONTOURS EVIDENCE "native-contours-probes.json"
#define INGAME EVIDENCE "native-ingame-regions.json"
#define GRAPH_CASE "campaign_graph_"
#define CONTOUR_CASE "authentic_contours_"
#define CAMPAIGN_MAPS 68 /* the catalog's 64 and four VER=57 maps no row names */
#define RGNS_HEADER 12
#define RGNS_REGION 32
#define CONT_HEADER 16
#define CONT_CAPACITY 5000
#define CONT_CAPACITY_STEP 2500
#define NAME_SIZE 256

static const char *const EVIDENCE_FILES[] = { GRAPHS, CONTOURS, INGAME, NULL };

static uint32_t u16_at(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t u32_at(const uint8_t *p) { return u16_at(p) | u16_at(p + 2) << 16; }

static void sha_text(KpSha256 *s, const char *fmt, ...)
{
	char b[160];
	va_list ap;
	int n;
	va_start(ap, fmt);
	n = vsnprintf(b, sizeof b, fmt, ap);
	va_end(ap);
	kp_sha256_update(s, b, (size_t)n);
}

/* The SHA-256 of an RGNS's graph in the probes' snapshot (their JSON, compact separators: the research probes and
 * tools/research/ingame_regions.py hashed the original's graphs so), or 0 when the section is cut short. */
static int graph_snapshot(const uint8_t *r, size_t size, char hex[2 * KP_SHA256_SIZE + 1])
{
	uint32_t w, h, n, splits, pool, i, j;
	const uint8_t *pool_at, *raster, *split_at;
	KpSha256 s;
	uint8_t d[KP_SHA256_SIZE];
	if (size < RGNS_HEADER) return 0;
	w = u16_at(r), h = u16_at(r + 2), n = u16_at(r + 4), splits = u16_at(r + 6), pool = u32_at(r + 8);
	if (size < RGNS_HEADER + (size_t)RGNS_REGION * n + 2 * (size_t)pool + 2 * (size_t)w * h + 6 * (size_t)splits)
		return 0;
	pool_at = r + RGNS_HEADER + (size_t)RGNS_REGION * n;
	raster = pool_at + 2 * (size_t)pool;
	split_at = raster + 2 * (size_t)w * h;
	kp_sha256_init(&s);
	sha_text(&s, "{\"width\":%u,\"height\":%u,\"regions\":[", w, h);
	for (i = 0; i < n; i++) {
		const uint8_t *q = r + RGNS_HEADER + (size_t)RGNS_REGION * i; /* class, group, tiles, ground, neighbours,
		                                                                * zero, cut rank, pad, pool at, centre, bounds */
		uint32_t at = u32_at(q + 12);
		if (at + q[7] > pool) return 0;
		sha_text(&s, "%s{\"id\":%u,\"flags\":%u,\"group\":%u,\"tiles\":%u,\"ground_neighbors\":%u,\"neighbors\":[",
			i ? "," : "", i, u16_at(q), u16_at(q + 2), u16_at(q + 4), q[6]);
		for (j = 0; j < q[7]; j++)
			sha_text(&s, "%s%u", j ? "," : "", u16_at(pool_at + 2 * (size_t)(at + j)));
		sha_text(&s, "],\"center_fixed8\":[%d,%d],\"bounds_pixels\":[%d,%d,%d,%d],\"cut_rank\":%u}",
			(int)(int32_t)u32_at(q + 16), (int)(int32_t)u32_at(q + 20), (int)(int16_t)u16_at(q + 24),
			(int)(int16_t)u16_at(q + 26), (int)(int16_t)u16_at(q + 28), (int)(int16_t)u16_at(q + 30), q[9]);
	}
	sha_text(&s, "],\"raster\":[");
	for (i = 0; i < h; i++) {
		sha_text(&s, "%s[", i ? "," : "");
		for (j = 0; j < w; j++)
			sha_text(&s, "%s%u", j ? "," : "", u16_at(raster + 2 * ((size_t)i * w + j)));
		sha_text(&s, "]");
	}
	sha_text(&s, "],\"splits\":[");
	for (i = 0; i < splits; i++) {
		const uint8_t *q = split_at + 6 * (size_t)i;
		sha_text(&s, "%s[%u,%u,%u]", i ? "," : "", u16_at(q), u16_at(q + 2), u16_at(q + 4));
	}
	sha_text(&s, "]}");
	kp_sha256_final(&s, d);
	kp_sha256_hex(d, hex);
	return 1;
}

static void bytes_sha(const uint8_t *data, size_t size, char hex[2 * KP_SHA256_SIZE + 1])
{
	uint8_t d[KP_SHA256_SIZE];
	kp_sha256(data, size, d);
	kp_sha256_hex(d, hex);
}

/* A case of an evidence file's "cases" by its name. */
static const KpJson *evidence_case(const KpJson *doc, const char *name)
{
	const KpJson *cases = kp_json_get(doc, "cases");
	int k;
	for (k = 0; k < kp_json_len(cases); k++)
		if (strcmp(kp_json_field_str(kp_json_at(cases, k), "name", ""), name) == 0) return kp_json_at(cases, k);
	return NULL;
}

/* A scenario's first MTXM chunk (the four VER=57 maps, which no loader descriptor reads: as the probes took it). */
static const uint8_t *first_mtxm(const uint8_t *chk, size_t size, size_t *n)
{
	size_t at = 0;
	while (at + CHUNK_HEADER <= size) {
		uint32_t len = u32_at(chk + at + 4);
		if (memcmp(chk + at, "MTXM", 4) == 0) {
			*n = len < size - at - CHUNK_HEADER ? len : size - at - CHUNK_HEADER;
			return chk + at + CHUNK_HEADER;
		}
		if ((int32_t)len < 0 || len > size - at - CHUNK_HEADER) break;
		at += CHUNK_HEADER + len;
	}
	return NULL;
}

/* Every campaign map's terrain derived as the game derives it, against the original's, which the research probes
 * hashed (the Python twin's check before ADR 0019's stage C7): the walk raster; the region graph built from the
 * imported flags alone (the probes ran the builder on them); the mission's RGNS (the strips behind the console) against
 * the graph the full game holds (native-ingame-regions.json, the catalog's 64); the contours sorted as the original
 * sorts them, and the capacities their arrays grew to. A catalog map is the mission kp_mission_from_files builds. */
static void derived_terrain_is_the_originals(KpT *T)
{
	KpFiles f;
	const KpCampaign *c;
	const KpJson *graphs, *contour_doc, *ingame, *cases;
	uint8_t *cv5[KP_TILESETS] = { 0 }, *vf4[KP_TILESETS] = { 0 };
	uint32_t *mflags[KP_TILESETS] = { 0 };
	size_t cv5_size[KP_TILESETS] = { 0 }, vf4_size[KP_TILESETS] = { 0 };
	int k, walks = 0, graphs_ = 0, contours = 0, catalog = 0, games = 0;
	if (!kp_t_ready(T, "derived_terrain_is_the_originals", EVIDENCE_FILES)) return;
	c = install_campaign(T, "KP_TEST_ORIGINAL", &f);
	graphs = kp_t_golden(T, GRAPHS);
	contour_doc = kp_t_golden(T, CONTOURS);
	ingame = kp_json_get(kp_t_golden(T, INGAME), "maps");
	cases = kp_json_get(graphs, "cases");
	for (k = 0; k < kp_json_len(cases); k++) {
		const KpJson *g = kp_json_at(cases, k), *in = kp_json_get(g, "inputs"), *seen = kp_json_get(g, "observed");
		const char *name = kp_json_field_str(g, "name", ""), *path = name + strlen(GRAPH_CASE);
		const KpJson *native, *counts;
		char want[NAME_SIZE], hex[2 * KP_SHA256_SIZE + 1], cname[NAME_SIZE], err[KP_ERROR_SIZE];
		uint32_t w = (uint32_t)kp_json_field_int(in, "width", 0), h = (uint32_t)kp_json_field_int(in, "height", 0);
		int t, d, mark, row = 0, grown = 0;
		const uint8_t *mtxm, *cont, *rgns;
		size_t chk_size, mtxm_size = 0, cont_size, rgns_size, cells = (size_t)w * h;
		uint8_t *chk, *walk, *graph;
		uint16_t *ids;
		uint32_t *flags;
		int64_t bad;
		KpError e;
		const KpMission *m = NULL;
		if (strncmp(name, GRAPH_CASE, strlen(GRAPH_CASE)) != 0) continue;
		for (t = 0; t < KP_TILESETS && strcmp(KP_TILESET_NAMES[t], kp_json_field_str(in, "tileset", "")) != 0; t++) {}
		kp_t_ok(T, t < KP_TILESETS, "%s: tileset %s", path, kp_json_field_str(in, "tileset", ""));
		if (!cv5[t]) {
			snprintf(cname, sizeof cname, "tileset\\%s.cv5", KP_TILESET_NAMES[t]);
			cv5[t] = kp_files_need(&f, cname, &cv5_size[t], &e);
			kp_t_ok(T, cv5[t] != NULL, "%s", e.msg);
			kp_t_defer(T, release, cv5[t]);
			snprintf(cname, sizeof cname, "tileset\\%s.vf4", KP_TILESET_NAMES[t]);
			vf4[t] = kp_files_need(&f, cname, &vf4_size[t], &e);
			kp_t_ok(T, vf4[t] != NULL, "%s", e.msg);
			kp_t_defer(T, release, vf4[t]);
			mflags[t] = (uint32_t *)owned(T, vf4_size[t] / KP_DERIVE_VF4_RECORD * sizeof **mflags);
			kp_terrain_megatile_flags(vf4[t], vf4_size[t], mflags[t]);
		}
		mark = kp_t_mark(T); /* the map's own parts, released after it (the tilesets' stay) */
		for (row = 1; row < c->directory_count; row++) {
			snprintf(want, sizeof want, "%s\\staredit\\scenario.chk", c->directories[row]);
			if (c->directories[row][0] && same_name(want, path)) break;
		}
		chk = kp_files_need(&f, path, &chk_size, &e);
		kp_t_ok(T, chk != NULL, "%s", e.msg);
		kp_t_defer(T, release, chk);
		if (row < c->directory_count) { /* the MTXM the native loader selects, as the mission has it */
			m = campaign_mission(T, &f, c, row);
			kp_t_ok(T, m->width == w && m->height == h && m->tileset == (uint32_t)t, "%s: its size and tileset", path);
			mtxm = m->tiles.data, mtxm_size = m->tiles.size;
			catalog++;
		} else {
			mtxm = first_mtxm(chk, chk_size, &mtxm_size);
		}
		kp_t_ok(T, mtxm != NULL, "%s: an MTXM", path);
		ids = (uint16_t *)owned(T, cells * sizeof *ids);
		flags = (uint32_t *)owned(T, cells * sizeof *flags);
		walk = (uint8_t *)owned(T, cells * 16);
		kp_t_eq(T, kp_derive_import(mtxm, w, h, cv5[t], cv5_size[t], mflags[t],
			(uint32_t)(vf4_size[t] / KP_DERIVE_VF4_RECORD), ids, flags, &bad), 0, "%s: the tiles imported", path);
		kp_t_eq(T, kp_derive_walk(w, h, ids, vf4[t], vf4_size[t], walk), 0, "%s: the walk raster", path);
		bytes_sha(walk, cells * 16, hex);
		kp_t_eq_str(T, hex, kp_json_field_str(seen, "walk_sha256", ""), "%s: the walk raster", path);
		if (m) kp_t_ok(T, bytes_eq(m->walk.data, m->walk.size, walk, cells * 16), "%s: the mission's walk raster", path);
		walks++;
		graph = kp_derive_regions(w, h, flags, ids, vf4[t], vf4_size[t], walk, &rgns_size, err, sizeof err);
		kp_t_ok(T, graph != NULL, "%s: %s", path, err);
		kp_t_defer(T, release, graph);
		kp_t_ok(T, graph_snapshot(graph, rgns_size, hex), "%s: the graph", path);
		kp_t_eq_str(T, hex, kp_json_field_str(seen, "snapshot_sha256", ""), "%s: the graph", path);
		kp_t_eq(T, u16_at(graph + 4), kp_json_field_int(seen, "region_count", -1), "%s: the regions", path);
		kp_t_eq(T, u16_at(graph + 6), kp_json_field_int(seen, "split_count", -1), "%s: the splits", path);
		graphs_++;
		if (m) { /* the game's graph: the mission's RGNS, built from the flags with the console's strips */
			const KpJson *game;
			snprintf(cname, sizeof cname, "%s", c->directories[row] + strlen("campaign\\"));
			for (d = 0; cname[d]; d++) cname[d] = cname[d] == '\\' ? '/' : (char)tolower((unsigned char)cname[d]);
			game = kp_json_get(ingame, cname);
			kp_t_ok(T, game != NULL, "%s: no in-game graph %s", path, cname);
			rgns = m->regions.data.data, rgns_size = m->regions.data.size;
			kp_t_ok(T, graph_snapshot(rgns, rgns_size, hex), "%s: the mission's RGNS", path);
			kp_t_eq_str(T, hex, kp_json_field_str(game, "snapshot_sha256", ""), "%s: the game's graph", cname);
			games++;
		}
		snprintf(want, sizeof want, CONTOUR_CASE "%s", path);
		native = kp_json_get(evidence_case(contour_doc, want), "observed");
		kp_t_ok(T, native != NULL, "%s: no contour case", path);
		if (m) {
			cont = m->contours.data.data, cont_size = m->contours.data.size;
		} else {
			uint8_t *own = kp_derive_contours(w, h, walk, &cont_size, err, sizeof err);
			kp_t_ok(T, own != NULL, "%s: %s", path, err);
			kp_t_defer(T, release, own);
			cont = own;
		}
		kp_t_ok(T, cont_size >= CONT_HEADER, "%s: CONT", path);
		counts = kp_json_get(native, "counts");
		for (d = 0; d < 4; d++) {
			kp_t_eq(T, u16_at(cont + 2 * d), kp_json_int(kp_json_at(counts, d), -1), "%s: direction %d's contours", path,
				d);
			grown |= u16_at(cont + 2 * d) > CONT_CAPACITY;
		}
		for (d = 0; d < 4; d++)
			kp_t_eq(T, u16_at(cont + 8 + 2 * d), CONT_CAPACITY + grown * CONT_CAPACITY_STEP, "%s: direction %d's capacity",
				path, d);
		bytes_sha(cont + CONT_HEADER, cont_size - CONT_HEADER, hex);
		kp_t_eq_str(T, hex, kp_json_field_str(native, "native_sorted_contours_sha256", ""), "%s: the sorted contours",
			path);
		contours++;
		kp_t_release(T, mark);
	}
	kp_t_eq(T, walks, CAMPAIGN_MAPS, "the walk rasters");
	kp_t_eq(T, graphs_, CAMPAIGN_MAPS, "the graphs");
	kp_t_eq(T, contours, CAMPAIGN_MAPS, "the contours");
	kp_t_eq(T, catalog, CAMPAIGN_MISSIONS, "the catalog's maps");
	kp_t_eq(T, games, CAMPAIGN_MISSIONS, "the in-game graphs");
}

static const KpTest TESTS[] = {
	{ "the_fixture_mission_is_built_from_its_chk", fixture_mission_from_its_chk, 0 },
	{ "chk_variants_are_refused_as_the_native_loader_does", chk_variants_are_refused, 0 },
	{ "chk_game_loads_are_the_originals", chk_game_loads, 0 },
	{ "a_custom_maps_mission", map_missions, 0 },
	{ "a_maps_header_has_the_lists_fields", map_header_reads_the_lists_fields, 0 },
	{ "unreachable_or_unsupported_triggers_are_validated", trigger_validation, 0 },
	{ "runtime_stops_are_refused", runtime_stops_are_refused, 0 },
	{ "the_1_17_missions_are_built_from_its_files", original_missions, 0 },
	{ "the_remastered_missions_are_built_from_its_files", remastered_missions, 0 },
	{ "the_1_17_and_remastered_missions_are_equal", installs_missions_equal, 0 },
	{ "the_campaign_maps_derived_terrain_is_the_originals", derived_terrain_is_the_originals, 0 },
	{ "the_install_maps_load", install_maps_load, 0 },
};
KP_SUITE(kp_suite_chk, "test_chk", TESTS);
