/* A mission read from its scenario (ADR 0018, stage D4: kp_chk, kp_derive, kp_mission_from_files). The converter's
 * fixture scenario (mission.chk, the CHK of mission.kpm) read through the profile's loader tables gives the fixture's
 * mission; variants of it are refused as converter/tests/test_chk.py and the trigger validation refuse them; every
 * campaign mission of the installs of KP_TEST_ORIGINAL (1.17) and KP_TEST_SCR (StarCraft: Remastered) equals the
 * converted .kpm, skipped without them or the converted missions; every test scenario built from its CHK and its index
 * line's packs (stage D8c, kp_data_mission) equals its .kpm. */
#include "kp_test_app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kp_files.h"
#include "kp_sha256.h"
#include "../../native/kp_core/src/core/kp_campaign.h"
#include "../../native/kp_core/src/core/kp_chk.h"
#include "../../native/kp_core/src/core/kp_mission.h"

#define DIR "tests/fixtures/generated/kpf/"
#define MISSIONS "generated/missions/"
#define FIXTURE_DIRECTORY "campaign\\fixture" /* kpf/fixtures.py mission_chk */
#define FIXTURE_TILESET "tileset\\desert"     /* its ERA 13: 13 & 7 */
#define FIXTURE_WAV "campaign\\fixture\\a.wav"
#define FIXTURE_WAV_DATA 4410
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

/* --- the fixture's profile and files --- */

typedef struct {
	KpDefsTable tables[2]; /* sorted: CHKL, MPAK */
	KpDefsProfile profile;
} FixtureProfile;

/* The compiled loader tables with the VCOD digest of the fixtures' b"vcod", and the fixture mission's packs (MPAK: one
 * mission, kpf/fixtures.py's "effects" and "terran"). */
static const KpDefsProfile *fixture_profile(KpT *T)
{
	static const uint8_t MPAK[] = { 1, 0, 16, 'c', 'a', 'm', 'p', 'a', 'i', 'g', 'n', '\\', 'f', 'i', 'x', 't', 'u', 'r',
		'e', 2, 7, 'e', 'f', 'f', 'e', 'c', 't', 's', 6, 't', 'e', 'r', 'r', 'a', 'n' };
	const KpDefsTable *real = kp_defs_table(kp_defs_profile("bw-1.17.0.1"), "CHKL");
	FixtureProfile *fp = (FixtureProfile *)owned(T, sizeof *fp);
	uint8_t *chkl;
	kp_t_ok(T, real != NULL && real->size > KP_SHA256_SIZE, "the profile's compiled CHKL");
	chkl = (uint8_t *)owned(T, real->size);
	memcpy(chkl, real->data, real->size);
	kp_sha256("vcod", 4, chkl + real->size - KP_SHA256_SIZE);
	memcpy(fp->tables[0].fourcc, "CHKL", 5);
	fp->tables[0].data = chkl;
	fp->tables[0].size = real->size;
	memcpy(fp->tables[1].fourcc, "MPAK", 5);
	fp->tables[1].data = MPAK;
	fp->tables[1].size = (uint32_t)sizeof MPAK;
	fp->profile.id = "fixture";
	fp->profile.tables = fp->tables;
	fp->profile.table_count = 2;
	return &fp->profile;
}

typedef struct {
	const uint8_t *chk, *cv5, *vf4;
	size_t chk_size, cv5_size, vf4_size;
	uint8_t wav[44 + FIXTURE_WAV_DATA];
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

static uint8_t *copy_bytes(const uint8_t *d, size_t n, size_t *size)
{
	uint8_t *b = (uint8_t *)malloc(n + 1);
	if (!b) return NULL;
	memcpy(b, d, n);
	b[n] = 0;
	*size = n;
	return b;
}

static uint8_t *source_read(void *ctx, const char *name, size_t *size, char *err, size_t err_size)
{
	const Source *s = (const Source *)ctx;
	(void)err_size;
	err[0] = '\0';
	if (same_name(name, FIXTURE_DIRECTORY "\\staredit\\scenario.chk")) return copy_bytes(s->chk, s->chk_size, size);
	if (same_name(name, FIXTURE_TILESET ".cv5")) return copy_bytes(s->cv5, s->cv5_size, size);
	if (same_name(name, FIXTURE_TILESET ".vf4")) return copy_bytes(s->vf4, s->vf4_size, size);
	if (same_name(name, FIXTURE_WAV)) return copy_bytes(s->wav, sizeof s->wav, size);
	return NULL;
}

static void kpf_free(void *p) { kp_kpf_free((KpKpf *)p); }

/* kpf/fixtures.py fixture_wav: mono 22,050 Hz PCM16 with 4,410 data bytes */
static void fixture_wav(uint8_t *w)
{
	static const uint8_t FMT[16] = { 1, 0, 1, 0, 0x22, 0x56, 0, 0, 0x44, 0xac, 0, 0, 2, 0, 16, 0 };
	memset(w, 0, 44 + FIXTURE_WAV_DATA);
	memcpy(w, "RIFF", 4);
	le32(w + 4, 36 + FIXTURE_WAV_DATA);
	memcpy(w + 8, "WAVEfmt ", 8);
	le32(w + 16, 16);
	memcpy(w + 20, FMT, 16);
	memcpy(w + 36, "data", 4);
	le32(w + 40, FIXTURE_WAV_DATA);
}

/* The fixture's files with the scenario chk: the CV5 and VF4 of tileset.kpd, a.wav. */
static KpFiles fixture_files(KpT *T, Source *s, const uint8_t *chk, size_t chk_size)
{
	KpKpf *c = (KpKpf *)owned(T, sizeof *c);
	KpError err;
	KpFiles f;
	const KpKpfSection *cv5, *vf4;
	size_t size;
	const uint8_t *kpd = kp_t_read(T, DIR "tileset.kpd", &size);
	kp_t_ok(T, kp_kpf_parse(c, kpd, size, "tileset.kpd", 1, &err) == 0, "%s", err.msg);
	kp_t_defer(T, kpf_free, c);
	cv5 = kp_kpf_require(c, "CV5 ", &err);
	kp_t_ok(T, cv5 != NULL, "%s", err.msg);
	vf4 = kp_kpf_require(c, "VF4 ", &err);
	kp_t_ok(T, vf4 != NULL, "%s", err.msg);
	s->chk = chk;
	s->chk_size = chk_size;
	s->cv5 = cv5->data;
	s->cv5_size = cv5->size;
	s->vf4 = vf4->data;
	s->vf4_size = vf4->size;
	fixture_wav(s->wav);
	memset(&f, 0, sizeof f);
	f.read = source_read;
	f.ctx = s;
	f.profile = fixture_profile(T);
	return f;
}

/* --- two missions compared --- */

static void mission_free(void *p) { kp_mission_free((KpMission *)p); }

#define SAME(field) kp_t_ok(T, bytes_eq(a->field.data, a->field.size, b->field.data, b->field.size), "%s: " #field, name)

/* sounds: the WAVs' records compared too, not their string ids alone */
static void mission_eq(KpT *T, const KpMission *a, const KpMission *b, const char *name, int sounds)
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
		if (sounds)
			kp_t_ok(T, x->index == y->index && x->length == y->length && x->avg_bytes_per_sec == y->avg_bytes_per_sec,
				"%s: sound %d", name, k);
	}
	kp_t_eq(T, b->pack_count, a->pack_count, "%s: the packs", name);
	for (k = 0; k < a->pack_count && k < b->pack_count; k++) SAME(packs[k]);
}

#undef SAME

/* --- the fixture --- */

static void fixture_mission_from_its_chk(KpT *T)
{
	Source s;
	KpFiles f;
	KpMission *a = (KpMission *)owned(T, sizeof *a), *b = (KpMission *)owned(T, sizeof *b);
	KpError err;
	size_t chk_size, size;
	const uint8_t *chk = kp_t_read(T, DIR "mission.chk", &chk_size), *kpm = kp_t_read(T, DIR "mission.kpm", &size);
	f = fixture_files(T, &s, chk, chk_size);
	kp_t_ok(T, kp_mission_load(a, kpm, size, "mission.kpm", NULL, &err) == 0, "%s", err.msg);
	kp_t_defer(T, mission_free, a);
	kp_t_ok(T, kp_mission_from_files(b, &f, FIXTURE_DIRECTORY, &err) == 0, "%s", err.msg);
	kp_t_defer(T, mission_free, b);
	mission_eq(T, a, b, "the fixture", 1);
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
	const uint8_t *base = kp_t_read(T, DIR "mission.chk", &base_size);
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
	kp_t_eq(T, kp_chk_read(&c, chk, size, fixture_profile(T), expansion, err, sizeof err), -1, "refused (%s)", want);
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
	const uint8_t *chk = kp_t_read(T, DIR "mission.chk", &chk_size);
	KpChk c;
	char err[KP_ERROR_SIZE];
	kp_t_eq(T, kp_chk_read(&c, chk, chk_size, fixture_profile(T), 1, err, sizeof err), 0, "the fixture: %s", err);
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
	kp_t_eq(T, kp_chk_read(&c, v, size, fixture_profile(T), 1, err, sizeof err), 0, "a truncated ISOM: %s", err);
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
	const uint8_t *chk = kp_t_read(T, DIR "mission.chk", &chk_size);
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
	rc = kp_mission_from_files(m, &f, FIXTURE_DIRECTORY, &err);
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

static void missions_equal(KpT *T, const char *env, int sounds)
{
	KpFiles f;
	KpCampaign *c = (KpCampaign *)owned(T, sizeof *c);
	KpError err;
	int k, compared = 0;
	if (!kp_t_has(T, MISSIONS "index.txt")) kp_t_skip(T, "no " MISSIONS "index.txt");
	kp_res_files(kp_t_install(T, env), &f);
	kp_t_ok(T, kp_campaign_from_files(c, &f, &err) == 0, "%s", err.msg);
	kp_t_defer(T, campaign_free, c);
	for (k = 0; k < c->directory_count; k++) {
		char name[96], path[160];
		KpMission *a, *b;
		size_t size;
		const uint8_t *kpm;
		int mark = kp_t_mark(T);
		if (!kp_campaign_mission_name(c, k, name, sizeof name)) continue;
		snprintf(path, sizeof path, MISSIONS "%s.kpm", name);
		if (!kp_t_has(T, path)) continue;
		a = (KpMission *)owned(T, sizeof *a);
		b = (KpMission *)owned(T, sizeof *b);
		kpm = kp_t_read(T, path, &size);
		kp_t_ok(T, kp_mission_load(a, kpm, size, name, NULL, &err) == 0, "%s", err.msg);
		kp_t_defer(T, mission_free, a);
		kp_t_ok(T, kp_mission_from_files(b, &f, c->directories[k], &err) == 0, "%s", err.msg);
		kp_t_defer(T, mission_free, b);
		mission_eq(T, a, b, name, sounds);
		kp_t_release(T, mark);
		compared++;
	}
	kp_t_eq(T, compared, CAMPAIGN_MISSIONS, "the campaign's missions compared");
}

/* Every test scenario of the data's catalog from its CHK (converter/scenarios.py writes both) equals its .kpm. */
static void scenarios_equal_the_converted(KpT *T)
{
	const KpData *d = kp_t_data(T);
	KpError err;
	int k, compared = 0;
	for (k = 0; k < d->mission_count; k++) {
		const KpDataMission *e = &d->missions[k];
		char path[160];
		KpMission *a, *b;
		size_t size;
		const uint8_t *kpm;
		int mark = kp_t_mark(T);
		if (e->catalog) continue;
		snprintf(path, sizeof path, "generated/%s.kpm", e->key);
		if (!kp_t_has(T, path)) continue;
		a = (KpMission *)owned(T, sizeof *a);
		b = (KpMission *)owned(T, sizeof *b);
		kpm = kp_t_read(T, path, &size);
		kp_t_ok(T, kp_mission_load(a, kpm, size, e->name, NULL, &err) == 0, "%s", err.msg);
		kp_t_defer(T, mission_free, a);
		kp_t_ok(T, kp_oracle_mission(".", e->key, b, &err) != NULL, "%s", err.msg);
		kp_t_defer(T, mission_free, b);
		mission_eq(T, a, b, e->name, 1);
		kp_t_release(T, mark);
		compared++;
	}
	kp_t_ok(T, compared > 100, "the test scenarios compared: %d", compared);
}

static void original_missions(KpT *T) { missions_equal(T, "KP_TEST_ORIGINAL", 1); }
/* Remastered's speech differs from 1.17's (its own recordings): the WAV records are its own */
static void remastered_missions(KpT *T) { missions_equal(T, "KP_TEST_SCR", 0); }

static const KpTest TESTS[] = {
	{ "the_fixture_mission_is_built_from_its_chk", fixture_mission_from_its_chk, 0 },
	{ "chk_variants_are_refused_as_the_native_loader_does", chk_variants_are_refused, 0 },
	{ "unreachable_or_unsupported_triggers_are_validated", trigger_validation, 0 },
	{ "the_1_17_missions_equal_the_converted", original_missions, 0 },
	{ "the_remastered_missions_equal_the_converted", remastered_missions, 0 },
	{ "the_scenarios_from_their_chk_equal_the_converted", scenarios_equal_the_converted, 0 },
};
KP_SUITE(kp_suite_chk, "test_chk", TESTS);
