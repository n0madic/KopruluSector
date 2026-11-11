/* A mission read from its scenario (ADR 0018, stage D4: kp_chk, kp_derive, kp_mission_from_files). The converter's
 * fixture scenario (kp_t_fixture_files: campaign\fixture\staredit\scenario.chk) read through the profile's loader
 * tables gives the fixture's mission (its fields: test_data); variants of it are refused as converter/tests/test_chk.py
 * and the trigger validation refuse them; every campaign mission of the installs of KP_TEST_ORIGINAL (1.17) and
 * KP_TEST_SCR (StarCraft: Remastered) is built from its files, and the two installs' missions are equal but their
 * speech's lengths and rates (Remastered's own recordings). */
#include "kp_test_app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kp_files.h"
#include "kp_sha256.h"
#include "../../native/kp_core/src/core/kp_campaign.h"
#include "../../native/kp_core/src/core/kp_chk.h"
#include "../../native/kp_core/src/core/kp_mission.h"

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
static uint8_t *variant(KpT *T, const char *tag, const void *payload, size_t n, int append, size_t *size)
{
	size_t base_size, at = 0, out = 0;
	const uint8_t *base = kp_t_read(T, FIXTURE_CHK, &base_size);
	uint8_t *v = (uint8_t *)owned(T, base_size + CHUNK_HEADER + n);
	while (at + CHUNK_HEADER <= base_size) {
		size_t len = (size_t)base[at + 4] | (size_t)base[at + 5] << 8 | (size_t)base[at + 6] << 16
			| (size_t)base[at + 7] << 24;
		if (!append && memcmp(base + at, tag, 4) == 0) {
			if (payload) {
				memcpy(v + out, tag, 4);
				le32(v + out + 4, (uint32_t)n);
				memcpy(v + out + CHUNK_HEADER, payload, n);
				out += CHUNK_HEADER + n;
			}
		} else {
			memcpy(v + out, base + at, CHUNK_HEADER + len);
			out += CHUNK_HEADER + len;
		}
		at += CHUNK_HEADER + len;
	}
	if (append) { /* payload NULL: the header alone, its payload past the end */
		memcpy(v + out, tag, 4);
		le32(v + out + 4, (uint32_t)n);
		if (payload && n) memcpy(v + out + CHUNK_HEADER, payload, n);
		out += CHUNK_HEADER + (payload ? n : 0);
	}
	*size = out;
	return v;
}

static void refused(KpT *T, const uint8_t *chk, size_t size, int expansion, const char *want)
{
	KpChk c;
	char err[KP_ERROR_SIZE];
	kp_t_eq(T, kp_chk_read(&c, chk, size, kp_t_fixture_profile(T), expansion, err, sizeof err), -1, "refused (%s)", want);
	kp_t_ok(T, strstr(err, want) != NULL, "the reason: %s (expected %s)", err, want);
}

/* converter/tests/test_chk.py over the version 205 fixture */
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

/* The fixture's first trigger with byte `at` set to `value` (and the u32 at `at32` to `v32` when at32 >= 0): 0 when
 * the mission loads, else -1 with the reason in why. */
static int with_trigger(KpT *T, size_t at, uint8_t value, long at32, uint32_t v32, char *why, size_t why_size)
{
	size_t chk_size, size, k;
	const uint8_t *chk = kp_t_read(T, FIXTURE_CHK, &chk_size);
	uint8_t *v = variant(T, "TRIG", NULL, 0, 0, &size), *trig = NULL;
	const uint8_t *p = chk;
	Source s;
	KpFiles f;
	KpMission *m = (KpMission *)owned(T, sizeof *m);
	KpError err;
	int rc;
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
	trig[at] = value;
	if (at32 >= 0) le32(trig + at32, v32);
	f = fixture_files(T, &s, v, size);
	rc = kp_mission_from_files(m, &f, KP_T_FIXTURE_MISSION, &err);
	if (rc == 0) kp_mission_free(m);
	snprintf(why, why_size, "%s", err.msg);
	return rc;
}

/* converter/analyze/triggers.py validate: the reachable conditions and actions past the tables or outside the V1
 * profile are refused; ignored ones are left */
static void trigger_validation(KpT *T)
{
	enum { COND_TYPE = 0x0f, COND_FIELDS = 0x0e, ACT = TRIGGER_ACTIONS, ACT_NUMBER = 0x14, ACT_TYPE = 0x1a };
	char why[KP_ERROR_SIZE];
	if (!kp_t_ready(T, "test_chk", FIXTURES)) return;
	kp_t_eq(T, with_trigger(T, COND_TYPE, 24, -1, 0, why, sizeof why), -1, "condition 24");
	kp_t_ok(T, strstr(why, "trigger 0 condition 0: type 24 (the table has 24)") != NULL, "%s", why);
	kp_t_eq(T, with_trigger(T, COND_TYPE, 14, -1, 0, why, sizeof why), -1, "opponents");
	kp_t_ok(T, strstr(why, "condition 0: opponents is outside the V1 profile") != NULL, "%s", why);
	kp_t_eq(T, with_trigger(T, ACT + ACT_TYPE, 60, -1, 0, why, sizeof why), -1, "action 60");
	kp_t_ok(T, strstr(why, "trigger 0 action 0: type 60 (the table has 60)") != NULL, "%s", why);
	kp_t_eq(T, with_trigger(T, ACT + ACT_TYPE, 52, -1, 0, why, sizeof why), -1, "action 52");
	kp_t_ok(T, strstr(why, "action 0: modify_unit_resource_amount is outside the V1 profile") != NULL, "%s", why);
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

static void missions_built(KpT *T, const char *env)
{
	KpFiles f;
	const KpCampaign *c = install_campaign(T, env, &f);
	int k, built = 0;
	for (k = 1; k < c->directory_count && c->directories[k][0]; k++) { /* kp_data_catalog's rows */
		int mark = kp_t_mark(T);
		campaign_mission(T, &f, c, k);
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

static const KpTest TESTS[] = {
	{ "the_fixture_mission_is_built_from_its_chk", fixture_mission_from_its_chk, 0 },
	{ "chk_variants_are_refused_as_the_native_loader_does", chk_variants_are_refused, 0 },
	{ "unreachable_or_unsupported_triggers_are_validated", trigger_validation, 0 },
	{ "the_1_17_missions_are_built_from_its_files", original_missions, 0 },
	{ "the_remastered_missions_are_built_from_its_files", remastered_missions, 0 },
	{ "the_1_17_and_remastered_missions_are_equal", installs_missions_equal, 0 },
};
KP_SUITE(kp_suite_chk, "test_chk", TESTS);
