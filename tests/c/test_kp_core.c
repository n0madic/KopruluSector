/* The native core (ADR 0008) on its own: the x86 integer helpers (kp_int.h), the container reader and the loaders
 * over every converted data file and their errors, the event ring of a native game, the unit sounds, a C failure under
 * a handler, the placement ghosts and the mission loader's bounds. */
#include "kp_test.h"

#include "../../native/kp_core/src/core/kp_int.h"
#include "../../native/kp_core/src/core/kp_kpf.h"
#include "../../native/kp_core/src/core/kp_terrain.h"

#define DATA KP_ORACLE_DATA "dat.kpd"
#define BASIC "tests/fixtures/generated/kpf/basic.kpd"

/* A case's value as an unsigned 32-bit register (mod 2^32). */
static uint32_t u(int64_t v) { return (uint32_t)(v & 0xffffffffLL); }

/* --- int (kp_int.h) --- */

static void int_wrap(KpT *T)
{
	kp_t_eq(T, u(256) & 0xffu, 0, "u8(256)");
	kp_t_eq(T, u(-1) & 0xffu, 255, "u8(-1)");
	kp_t_eq(T, u(65536 + 5) & 0xffffu, 5, "u16");
	kp_t_eq(T, u(-1) & 0xffffu, 65535, "u16(-1)");
	kp_t_eq(T, u(-1), 4294967295LL, "u32(-1)");
	kp_t_eq(T, u(4294967296LL), 0, "u32(2^32)");
	kp_t_eq(T, kp_s8(u(128)), -128, "s8(128)");
	kp_t_eq(T, kp_s8(u(255)), -1, "s8(255)");
	kp_t_eq(T, kp_s8(u(127)), 127, "s8(127)");
	kp_t_eq(T, kp_s16(u(32768)), -32768, "s16");
	kp_t_eq(T, kp_s16(u(65535)), -1, "s16(65535)");
	kp_t_eq(T, kp_s32(u(2147483648LL)), -2147483648LL, "s32(2^31)");
	kp_t_eq(T, kp_s32(u(4294967295LL)), -1, "s32(2^32 - 1)");
	kp_t_eq(T, kp_s32(u(-1)), -1, "s32(-1)");
	kp_t_eq(T, (uint32_t)(u(4294967295LL) + u(1)), 0, "add");
	kp_t_eq(T, (uint32_t)(u(0) - u(1)), 4294967295LL, "sub");
	kp_t_eq(T, (uint32_t)(0u - u(1)), 4294967295LL, "neg");
}

static void int_mul_boundaries(KpT *T)
{
	kp_t_eq(T, kp_mul(0xFFFFFFFFu, 0xFFFFFFFFu), 1, NULL);
	kp_t_eq(T, kp_mulhi(0xFFFFFFFFu, 0xFFFFFFFFu), 0xFFFFFFFEu, NULL);
	kp_t_eq(T, (int64_t)kp_mul(0x015a4e35u, 0xFFFFFFFFu) + 1, 4272271820LL, NULL); /* docs/research/native-prng.md */
	kp_t_eq(T, kp_mul(0x80000000u, 2), 0, NULL);
	kp_t_eq(T, kp_mulhi(0x80000000u, 2), 1, NULL);
	kp_t_eq(T, kp_mul(u(-1), 3), 4294967293LL, NULL); /* signed inputs give the same low bits */
	kp_t_eq(T, kp_mul(65536, 65536), 0, NULL);
	kp_t_eq(T, kp_mulhi(65536, 65536), 1, NULL);
	kp_t_eq(T, kp_mul(0x12345678u, 0x9ABCDEF0u), 0x242D2080, NULL);
	kp_t_eq(T, kp_mulhi(0x12345678u, 0x9ABCDEF0u), 0x0B00EA4E, NULL);
}

static void int_mul_matches_uint64(KpT *T)
{
	uint64_t x = 1;
	int i;
	for (i = 1; i <= 5000; i++) {
		uint64_t y, p;
		x = (x * 1103515245u + 12345u + (uint64_t)i) % 4294967296ULL;
		y = (x * 2654435761ULL) % 4294967296ULL;
		p = x * y;
		kp_t_eq(T, kp_mul((uint32_t)x, (uint32_t)y), (int64_t)(p % 4294967296ULL), "mul");
		kp_t_eq(T, kp_mulhi((uint32_t)x, (uint32_t)y), (int64_t)(p / 4294967296ULL), "mulhi");
	}
}

static void int_division_truncates(KpT *T)
{
	kp_t_eq(T, kp_idiv(7, 2), 3, NULL);
	kp_t_eq(T, kp_irem(7, 2), 1, NULL);
	kp_t_eq(T, kp_idiv(-7, 2), -3, NULL);
	kp_t_eq(T, kp_irem(-7, 2), -1, NULL);
	kp_t_eq(T, kp_idiv(7, -2), -3, NULL);
	kp_t_eq(T, kp_irem(7, -2), 1, NULL);
	kp_t_eq(T, kp_idiv(-7, -2), 3, NULL);
	kp_t_eq(T, kp_irem(-7, -2), -1, NULL);
	kp_t_eq(T, kp_idiv(2147483647, 1), 2147483647, NULL);
	kp_t_eq(T, kp_idiv(INT32_MIN, 2), -1073741824, NULL);
	kp_t_eq(T, kp_idiv(2147483646, 2147483647), 0, NULL);
	kp_t_eq(T, kp_idiv(-2147483647, 2147483646), -1, NULL);
	kp_t_ok(T, kp_idiv_faults(1, 0), "division by zero");
	kp_t_ok(T, kp_idiv_faults(INT32_MIN, -1), "overflow");
	kp_t_eq(T, 4294967295u / 2u, 2147483647, NULL);
	kp_t_eq(T, 4294967295u % 10u, 5, NULL);
	kp_t_eq(T, 4294967295u / 4294967294u, 1, NULL);
}

static void int_shifts(KpT *T)
{
	kp_t_eq(T, kp_shl(1, 31), 2147483648LL, NULL);
	kp_t_eq(T, kp_shl(3, 31), 2147483648LL, NULL);
	kp_t_eq(T, kp_shl(0xFFFFFFFFu, 4), 0xFFFFFFF0u, NULL);
	kp_t_eq(T, kp_shl(5, 32), 5, "count mod 32, as on x86");
	kp_t_eq(T, kp_shr(0x80000000u, 31), 1, NULL);
	kp_t_eq(T, kp_shr(0xFFFFFFFFu, 16), 0xFFFF, NULL);
	kp_t_eq(T, kp_shr(u(-1), 28), 15, NULL);
	kp_t_eq(T, kp_sar(kp_s32(0x80000000u), 4), -134217728, NULL);
	kp_t_eq(T, kp_sar(-1, 31), -1, NULL);
	kp_t_eq(T, kp_sar(-7, 1), -4, "floor, unlike idiv");
	kp_t_eq(T, kp_sar(7, 1), 3, NULL);
}

static void int_bitwise(KpT *T)
{
	kp_t_eq(T, 0xFFFFFFFFu & 0x80000001u, 0x80000001u, NULL);
	kp_t_eq(T, 0x80000000u | 1u, 0x80000001u, NULL);
	kp_t_eq(T, 0xFFFFFFFFu ^ 0x0F0F0F0Fu, 0xF0F0F0F0u, NULL);
	kp_t_eq(T, (uint32_t)~0u, 0xFFFFFFFFu, NULL);
	kp_t_eq(T, (uint32_t)~0xFFFFFFFFu, 0, NULL);
}

/* Fixed-point positions are 1/256 px (plan D13); conversions use x86 shifts. */
static void int_fixed_point_8(KpT *T)
{
	kp_t_eq(T, kp_sar(kp_s32(u(-3 * 256)), 8), -3, NULL);
	kp_t_eq(T, kp_sar(kp_s32(u(-1)), 8), -1, "-1/256 px rounds toward -inf");
	kp_t_eq(T, kp_idiv(-1, 256), 0, "but C division rounds toward zero");
	kp_t_eq(T, kp_sar(kp_s32(u(-257)), 8), -2, NULL);
	kp_t_eq(T, kp_idiv(-257, 256), -1, NULL);
}

/* --- containers and loaders --- */

/* The data files of the oracle suites: dat.kpd and the missions of the initial-state and scenario goldens. */
static int data_files(KpT *T, char paths[][512], int max)
{
	static const char *const DIRS[2] = { "initial_state", "scenarios" };
	int count = 0, d, k, i;
	snprintf(paths[count++], 512, "%s", DATA);
	for (d = 0; d < 2; d++) {
		char path[512];
		const KpJson *index;
		snprintf(path, sizeof path, "tests/golden/oracle/%s/index.json", DIRS[d]);
		index = kp_t_golden(T, path);
		for (k = 0; k < kp_json_len(index) && count < max; k++) {
			const char *m;
			char file[512];
			snprintf(path, sizeof path, "tests/golden/oracle/%s/%s.json", DIRS[d], kp_json_str(kp_json_at(index, k), ""));
			m = kp_json_field_str(kp_t_golden(T, path), "mission", "");
			snprintf(file, sizeof file, strncmp(m, "scenarios/", 10) == 0 ? "generated/%s.kpm" : "generated/missions/%s.kpm", m);
			for (i = 0; i < count && strcmp(paths[i], file) != 0; i++) {
			}
			if (i == count) snprintf(paths[count++], 512, "%s", file);
		}
	}
	return count;
}

#define MAX_FILES 512

/* Every data file's container parses with its checksums verified, its sections inside the file. */
static void containers_verify(KpT *T)
{
	char (*paths)[512];
	int count, k;
	if (!kp_t_has(T, DATA)) return;
	paths = (char (*)[512])malloc(MAX_FILES * 512);
	kp_t_ok(T, paths != NULL, "out of memory");
	kp_t_defer(T, free, paths);
	count = data_files(T, paths, MAX_FILES);
	for (k = 0; k < count; k++) {
		size_t size;
		uint8_t *data;
		KpKpf c;
		KpError err;
		int s, mark = kp_t_mark(T);
		if (!kp_t_mine(T, k + 1)) continue;
		data = kp_t_read(T, paths[k], &size);
		kp_t_ok(T, kp_kpf_parse(&c, data, size, paths[k], 1, &err) == 0, "%s", err.msg);
		for (s = 0; s < c.section_count; s++) {
			const KpKpfSection *sec = &c.sections[s];
			kp_t_ok(T, (uint64_t)sec->offset + sec->size <= size, "%s section %s inside the file", paths[k], sec->fourcc);
		}
		kp_t_ok(T, c.section_count > 0, "%s has sections", paths[k]);
		kp_kpf_free(&c);
		kp_t_release(T, mark);
	}
	kp_t_ok(T, count > 64, "data files: %d", count);
}

static void expect_refused(KpT *T, const uint8_t *data, size_t size, int verify, const char *message)
{
	KpKpf c;
	KpError err;
	int r = kp_kpf_parse(&c, data, size, "x", verify, &err);
	if (r == 0) kp_kpf_free(&c);
	kp_t_ok(T, r != 0, "refused: %s", message);
	kp_t_eq_str(T, err.msg, message, "the message");
}

static void container_errors(KpT *T)
{
	size_t size;
	uint8_t *data, *copy;
	KpKpf c;
	KpError err;
	const KpKpfSection *walk;
	size_t pos;
	if (!kp_t_has(T, BASIC)) return;
	data = kp_t_read(T, BASIC, &size);
	copy = (uint8_t *)malloc(size);
	kp_t_ok(T, copy != NULL, "out of memory");
	kp_t_defer(T, free, copy);
	expect_refused(T, (const uint8_t *)"short", 5, 0, "kpf: x: file shorter than the container header");
	memcpy(copy, data, size);
	memcpy(copy, "XXXX", 4);
	expect_refused(T, copy, size, 0, "kpf: x: not a Koprulu container (bad magic)");
	expect_refused(T, data, size - 4, 0, "kpf: x: file is 712 bytes, header says 716 (truncated or corrupt)");
	memcpy(copy, data, size);
	copy[8] = 2;
	expect_refused(T, copy, size, 0,
		"kpf: x: container version 2 is not supported by this build (expects 1); regenerate the data with the converter");
	kp_t_ok(T, kp_kpf_parse(&c, data, size, "x", 0, &err) == 0, "%s", err.msg);
	walk = kp_kpf_find(&c, "WALK");
	kp_t_ok(T, walk != NULL, "the WALK section");
	pos = (size_t)(walk->data - data) + walk->size - 1;
	kp_kpf_free(&c);
	memcpy(copy, data, size);
	copy[pos] = (uint8_t)(copy[pos] + 1);
	kp_t_ok(T, kp_kpf_parse(&c, copy, size, "x", 0, &err) == 0, "unverified: %s", err.msg);
	kp_kpf_free(&c);
	expect_refused(T, copy, size, 1, "kpf: x: section WALK checksum mismatch (corrupt file)");
}

/* dat.kpd, grpmeta.kpd and iscript.kpd load; another kind of file is refused with the kind's message. */
static void loaders_refuse_other_kinds(KpT *T)
{
	size_t size, basic_size;
	uint8_t *data, *basic;
	KpError err;
	KpDat dat;
	KpGrpMeta grp;
	KpIScript script;
	if (!kp_t_has(T, DATA) || !kp_t_has(T, BASIC)) return;
	basic = kp_t_read(T, BASIC, &basic_size);
	data = kp_t_read(T, DATA, &size);
	kp_t_ok(T, kp_dat_load(&dat, data, size, DATA, NULL, &err) == 0, "%s", err.msg);
	kp_dat_free(&dat);
	kp_t_ok(T, kp_dat_load(&dat, basic, basic_size, "basic", NULL, &err) != 0, "a DAT of another kind");
	kp_t_eq_str(T, err.msg, "kpf: basic: file kind \"TEST\", expected \"DAT \"", NULL);
	data = kp_t_read(T, KP_ORACLE_DATA "grpmeta.kpd", &size);
	kp_t_ok(T, kp_grpmeta_load(&grp, data, size, "grpmeta", NULL, &err) == 0, "%s", err.msg);
	kp_grpmeta_free(&grp);
	kp_t_ok(T, kp_grpmeta_load(&grp, basic, basic_size, "basic", NULL, &err) != 0, "a grpmeta of another kind");
	kp_t_eq_str(T, err.msg, "kpf: basic: file kind \"TEST\", expected \"GRPM\"", NULL);
	data = kp_t_read(T, KP_ORACLE_DATA "iscript.kpd", &size);
	kp_t_ok(T, kp_iscript_load(&script, data, size, "iscript", NULL, &err) == 0, "%s", err.msg);
	kp_iscript_free(&script);
	kp_t_ok(T, kp_iscript_load(&script, basic, basic_size, "basic", NULL, &err) != 0, "an iscript of another kind");
	kp_t_eq_str(T, err.msg, "kpf: basic: file kind \"TEST\", expected \"ISCR\"", NULL);
}

/* Every mission of the oracle suites loads. */
static void missions_load(KpT *T)
{
	char (*paths)[512];
	int count, k, n = 0;
	if (!kp_t_has(T, DATA)) return;
	paths = (char (*)[512])malloc(MAX_FILES * 512);
	kp_t_ok(T, paths != NULL, "out of memory");
	kp_t_defer(T, free, paths);
	count = data_files(T, paths, MAX_FILES);
	for (k = 0; k < count; k++) {
		size_t size, len = strlen(paths[k]);
		uint8_t *data;
		KpMission m;
		KpError err;
		int mark = kp_t_mark(T);
		if (len < 4 || strcmp(paths[k] + len - 4, ".kpm") != 0) continue;
		data = kp_t_read(T, paths[k], &size);
		kp_t_ok(T, kp_mission_load(&m, data, size, paths[k], NULL, &err) == 0, "%s", err.msg);
		kp_mission_free(&m);
		kp_t_release(T, mark);
		n++;
	}
	kp_t_ok(T, n >= 64, "missions checked: %d", n);
}

/* Lengths and counts read from a file never wrap the bounds checks (32-bit sums in C). */
static void mission_rejects_wrapping_lengths(KpT *T)
{
	static const uint8_t WRAP[4] = { 0xf8, 0xff, 0xff, 0xff }, WIDE[4] = { 0xff, 0xff, 0xff, 0xff };
	const char *path = "generated/scenarios/marine_walk.kpm";
	size_t size, rule_at, meta_at;
	uint8_t *data, *copy;
	KpKpf c;
	KpMission m;
	KpError err;
	const KpKpfSection *rule, *meta;
	if (!kp_t_has(T, path)) return;
	data = kp_t_read(T, path, &size);
	kp_t_ok(T, kp_kpf_parse(&c, data, size, path, 0, &err) == 0, "%s", err.msg);
	rule = kp_kpf_find(&c, "RULE");
	meta = kp_kpf_find(&c, "META");
	kp_t_ok(T, rule && meta, "RULE and META sections");
	kp_t_ok(T, rule->size >= 8, "RULE holds an override");
	rule_at = (size_t)(rule->data - data) + 4; /* the first override's length */
	meta_at = (size_t)(meta->data - data) + 4;
	kp_kpf_free(&c);
	copy = (uint8_t *)malloc(size);
	kp_t_ok(T, copy != NULL, "out of memory");
	kp_t_defer(T, free, copy);
	memcpy(copy, data, size);
	memcpy(copy + rule_at, WRAP, 4);
	kp_t_ok(T, kp_mission_load(&m, copy, size, path, NULL, &err) != 0, "a wrapping override length is refused");
	kp_t_ok(T, strstr(err.msg, "RULE section is malformed") != NULL, "the message: %s", err.msg);
	memcpy(copy, data, size);
	memcpy(copy + meta_at, WIDE, 4);
	kp_t_ok(T, kp_mission_load(&m, copy, size, path, NULL, &err) != 0, "a wrapping map size is refused");
	kp_t_ok(T, strstr(err.msg, "MTXM size does not match 65535x65535") != NULL, "the message: %s", err.msg);
}

/* --- games --- */

static const KpJson *scenario(KpT *T, const char *name)
{
	char path[512];
	snprintf(path, sizeof path, "tests/golden/oracle/scenarios/%s.json", name);
	return kp_t_golden(T, path);
}

static void free_run(void *p)
{
	kp_golden_run_free((KpGoldenRun *)p);
	free(p);
}

static KpGoldenRun *golden_run(KpT *T, const KpJson *g, const KpOracleOpts *opts)
{
	KpGoldenRun *gr = (KpGoldenRun *)malloc(sizeof *gr);
	KpError err;
	if (!gr) kp_t_fail(T, "out of memory");
	if (kp_golden_run_start(gr, ".", g, opts, 0, &err) != 0) {
		free(gr);
		kp_t_fail(T, "%s", err.msg);
	}
	kp_t_defer(T, free_run, gr);
	kp_t_guard(T, gr->game.g);
	return gr;
}

typedef struct {
	KpT *T;
	int64_t n[32], last;
} Kinds;

static void count_event(void *ud, const KpEvent *e)
{
	Kinds *k = (Kinds *)ud;
	if (e->kind >= 0 && e->kind < 32) k->n[e->kind]++;
	kp_t_ok(k->T, e->seq > k->last, "sequence numbers grow");
	k->last = e->seq;
	if (e->kind == KP_EVENT_UNIT_UNPLACED) kp_t_ok(k->T, e->a >= 0 && e->a < 228 && e->b > 0 && e->c > 0, "a unit type and its place");
}

static void kinds(KpT *T, const char *name, int events, Kinds *k)
{
	KpOracleOpts o;
	KpGoldenRun *gr;
	KpEvent e;
	int left = 0;
	kp_oracle_opts(&o);
	o.events = events;
	memset(k, 0, sizeof *k);
	k->T = T;
	gr = golden_run(T, scenario(T, name), &o);
	gr->on_event = count_event;
	gr->ud = k;
	kp_runner_to(&gr->run, 150);
	while (kp_event_take(gr->game.g, &e)) left++;
	kp_t_eq(T, left, 0, "the runner drains every step");
}

/* The event ring of a native game: the sounds of the steps, the local player's error notifications and the units that
 * could not be placed, each drained once in emission order; a game without a presentation records none. */
static void native_events(KpT *T)
{
	Kinds k;
	int64_t all = 0;
	int i;
	if (!kp_t_has(T, DATA)) return;
	kinds(T, "rescue_fog", 1, &k);
	kp_t_ok(T, k.n[KP_EVENT_SOUND] + k.n[KP_EVENT_SOUND_SCREEN] + k.n[KP_EVENT_SOUND_UNIT] > 0, "sounds");
	kinds(T, "placement_fail", 1, &k);
	kp_t_ok(T, k.n[KP_EVENT_ERROR] > 0 && k.n[KP_EVENT_UNIT_UNPLACED] > 0, "errors %lld, units not placed %lld",
		(long long)k.n[KP_EVENT_ERROR], (long long)k.n[KP_EVENT_UNIT_UNPLACED]);
	kinds(T, "placement_fail", 0, &k);
	for (i = 0; i < 32; i++) all += k.n[i];
	kp_t_eq(T, all, 0, "the ring off");
}

/* A sound event's a as its fields. */
static void sound_args(int64_t a, int64_t *out)
{
	int64_t low = a & 0xffffffffLL, high = (int64_t)((uint64_t)a >> 32);
	out[0] = low % 0x10000;
	out[1] = low / 0x10000 % 0x100;
	out[2] = low / 0x1000000;
	out[3] = high % 4;
	out[4] = high / 4 % 2;
	out[5] = high / 0x100 % 0x100;
}

static void check_args(KpT *T, int64_t a, int64_t sfx, int64_t volume, int64_t enable, int64_t talk, int64_t camera,
	int64_t colour, const char *what)
{
	int64_t f[6];
	sound_args(a, f);
	kp_t_ok(T, f[0] == sfx && f[1] == volume && f[2] == enable && f[3] == talk && f[4] == camera && f[5] == colour,
		"%s: %lld,%lld,%lld,%lld,%lld,%lld", what, (long long)f[0], (long long)f[1], (long long)f[2], (long long)f[3],
		(long long)f[4], (long long)f[5]);
}

/* A packed sprite position as signed x, y. */
static void position(int64_t c, int64_t *x, int64_t *y)
{
	*x = c % 0x10000;
	*y = c / 0x10000 % 0x10000;
	if (*x >= 0x8000) *x -= 0x10000;
	if (*y >= 0x8000) *y -= 0x10000;
}

/* The events of the game since the last call: the count, the SOUND_UNIT ones into units. */
static int drain(KpGame *g, KpEvent *all, int *count, KpEvent *units, int max)
{
	KpEvent e;
	int n = 0;
	*count = 0;
	while (kp_event_take(g, &e)) {
		if (*count < max) all[(*count)++] = e;
		if (e.kind == KP_EVENT_SOUND_UNIT && n < max) units[n++] = e;
	}
	return n;
}

/* Unit sounds (PlaySoundAtUnit 0x446940, here through BeginCloak 0x41d200): heard by the local player at the unit's
 * sprite when the unit is theirs or its sprite is seen (in a replay: seen by the world's vision), with the call's
 * arguments and the talking request after it (KP_EVENT_SOUND_UNIT); sound 0 plays nothing; without a unit the sound
 * has no position (KP_EVENT_SOUND_SCREEN); a talking request that does not wait for the sound keeps its event, with
 * nothing to play. NotifyUnitAttacked 0x446f00 leaves the interface the unit and its position. */
static void unit_sounds(KpT *T)
{
	KpOracleOpts o;
	KpGoldenRun *gr;
	KpGame *g;
	KpImages *w;
	KpReplay empty;
	KpRunner run;
	KpEvent all[64], e[64];
	int count, k_sounds;
	int64_t n, m, k, s, x, y, owner;
	if (!kp_t_has(T, DATA)) return;
	kp_oracle_opts(&o);
	o.events = 1;
	gr = golden_run(T, scenario(T, "marine_group"), &o);
	g = gr->game.g;
	w = g->w;
	kp_replay_init(&empty);
	kp_runner_init(&run, g, &empty);
	kp_runner_to(&run, 3);
	drain(g, all, &count, e, 64);
	n = g->lists[KP_LIST_ACTIVE].head;
	s = g->u.sprite[n];
	kp_begin_cloak(g, n, 0x111);
	k_sounds = drain(g, all, &count, e, 64);
	kp_t_eq(T, k_sounds, 1, "the cloak sound");
	kp_t_eq(T, e[0].a, 0x111, "enable 1, minVolume 0, no talking request");
	kp_t_eq(T, e[0].b, n, NULL);
	position(e[0].c, &x, &y);
	kp_t_eq(T, x, w->spr.x[s], NULL);
	kp_t_eq(T, y, w->spr.y[s], NULL);
	/* a voice: enable bit 0 of the options word, minVolume 99, a talking request when it played (camera 1, 0x11) */
	kp_unit_sound(g, 0x7e, n, 1, 99, 2, 1, 0x11);
	drain(g, all, &count, e, 64);
	kp_t_eq(T, e[0].a, 0x7e + 99 * 0x10000LL + 0x1000000LL + 2 * (1LL << 32) + (1LL << 34) + 0x11 * (1LL << 40), NULL);
	check_args(T, e[0].a, 0x7e, 99, 1, 2, 1, 0x11, "the voice's fields");
	m = g->u.next[n];
	kp_begin_cloak(g, m, 0);
	kp_t_eq(T, drain(g, all, &count, e, 64), 0, "sound 0");
	k = g->u.next[m];
	owner = g->u.owner[k];
	g->u.owner[k] = (owner + 1) % 8;
	w->spr.visibility[g->u.sprite[k]] = 0;
	kp_begin_cloak(g, k, 0x111);
	kp_t_eq(T, drain(g, all, &count, e, 64), 0, "another player's unseen unit");
	kp_unit_sound(g, 0x111, k, 0, 0, 2, 1, 0x11);
	kp_t_eq(T, drain(g, all, &count, e, 64), 0, "unseen, with a talking request when it played");
	kp_unit_sound(g, 0x111, k, 0, 99, 1, 1, 0x11);
	kp_t_eq(T, drain(g, all, &count, e, 64), 1, "unseen, with a talking request regardless");
	check_args(T, e[0].a, 0x111, 99, 0xff, 1, 1, 0x11, "nothing to play");
	kp_unit_sound(g, 0, n, 1, 99, 1, 1, 0x11);
	kp_t_eq(T, drain(g, all, &count, e, 64), 1, "sound 0 with a talking request regardless");
	check_args(T, e[0].a, 0, 99, 0xff, 1, 1, 0x11, "sound 0's fields");
	kp_unit_sound(g, 0x20, 0, 0, 0, 0, 0, 0);
	drain(g, all, &count, e, 64);
	kp_t_eq(T, count, 1, "a sound without a unit");
	kp_t_eq(T, all[0].kind, KP_EVENT_SOUND_SCREEN, NULL);
	kp_t_eq(T, all[0].a, 0x20, NULL);
	kp_t_eq(T, all[0].b, 0, NULL);
	/* the local player's unit under attack (the voice and its gates are the interface's); another player's: nothing */
	g->u.owner[k] = g->local_player;
	kp_notify_attacked(g, k);
	drain(g, all, &count, e, 64);
	kp_t_eq(T, count, 1, "the attacked unit");
	kp_t_eq(T, all[0].kind, KP_EVENT_UNIT_ATTACKED, NULL);
	kp_t_eq(T, all[0].a, k, NULL);
	kp_t_eq(T, all[0].b, (g->u.status[k] & 2) != 0 ? 1 : 0, NULL);
	position(all[0].c, &x, &y);
	kp_t_eq(T, x, w->spr.x[g->u.sprite[k]], NULL);
	kp_t_eq(T, y, w->spr.y[g->u.sprite[k]], NULL);
	g->u.owner[k] = (g->local_player + 1) % 8;
	kp_notify_attacked(g, k);
	drain(g, all, &count, e, 64);
	kp_t_eq(T, count, 0, "another player's unit");
	g->u.owner[k] = owner;
	/* In a replay the world's vision decides, even for the local player's unit. */
	w->replay = 1;
	w->local_vision = 0;
	kp_unit_sound(g, 0x111, n, 0, 0, 0, 0, 0);
	kp_t_eq(T, drain(g, all, &count, e, 64), 0, "a replay's vision that does not see the unit");
	w->local_vision = 0xff;
	kp_unit_sound(g, 0x111, n, 0, 0, 0, 0, 0);
	kp_t_eq(T, drain(g, all, &count, e, 64), 1, "a replay's vision that sees it");
	/* The local player is the game's and its world's, with the vision derived from it; a replay's world keeps the
	 * replay's vision. */
	kp_set_local_player(g, 2);
	kp_t_eq(T, w->local_vision, 0xff, "the replay's vision stays");
	w->replay = 0;
	kp_set_local_player(g, 3);
	kp_t_eq(T, g->local_player, 3, NULL);
	kp_t_eq(T, w->local_player, 3, "the world's local player follows");
	kp_t_eq(T, g->local_vision, 8, "the game's vision follows");
	kp_t_eq(T, w->local_vision, 8, "the world's vision follows");
}

static void unit_of_zero(KpT *T, void *ud)
{
	(void)T;
	kp_unit_is_dead((KpGame *)ud, kp_game_unit((KpGame *)ud, 0));
}

/* A C failure under the test's handler reaches the caller as an error and leaves the game usable. */
static void core_failures_leave_the_game_usable(KpT *T)
{
	KpGoldenRun *gr;
	char msg[256];
	KpReplay empty;
	KpRunner run;
	if (!kp_t_has(T, DATA)) return;
	gr = golden_run(T, scenario(T, "marine_group"), NULL);
	kp_replay_init(&empty);
	kp_runner_init(&run, gr->game.g, &empty);
	kp_runner_to(&run, 3);
	kp_t_ok(T, kp_t_pcall(T, unit_of_zero, gr->game.g, msg, sizeof msg) != 0 && strstr(msg, "out of range"),
		"a C failure: %s", msg);
	kp_runner_to(&run, 40);
	kp_t_ok(T, kp_scalar(gr->game.g, KP_FRAME) > 0, "the game runs on");
}

#define SCV 7
#define REFINERY 110
#define COMMAND_CENTER 106
#define GEYSER 188
#define SILO 108
#define PLACE 30
#define GHOST 581
#define BOX 583

static KpGame *placement_game(KpT *T, const char *path)
{
	KpGame *g = kp_t_game(T, path, 1, 0, NULL)->g;
	int64_t n, scv = 0;
	for (n = 1; n <= KP_UNITS; n++) {
		if (g->u.type[n] == SCV && g->u.owner[n] == g->local_player) {
			scv = n;
			break;
		}
	}
	kp_select_locally(g, &scv, 1);
	return g;
}

/* The ghost and footprint images of unit n's sprite: "<id> <x>,<y>" joined by ";". */
static void previews(KpGame *g, int64_t n, char *out, size_t size)
{
	KpImages *w = g->w;
	int32_t i = w->spr.images[g->u.sprite[n]].head;
	size_t at = 0;
	out[0] = '\0';
	while (i != -1) {
		if (w->img.id[i] == GHOST || w->img.id[i] == BOX) {
			at += (size_t)snprintf(out + at, at < size ? size - at : 0, "%s%d %d,%d", at ? ";" : "", (int)w->img.id[i],
				(int)w->img.xoff[i], (int)w->img.yoff[i]);
		}
		i = w->img.next[i];
	}
}

static int units_of(KpGame *g, int64_t t, int64_t *out, int max)
{
	int64_t n;
	int count = 0;
	for (n = 1; n <= KP_UNITS && count < max; n++) {
		if (g->u.type[n] == t && g->u.sprite[n] != -1) out[count++] = n;
	}
	return count;
}

/* The placement mode's ghost images (StartPlacement 0x47d3a0, ClosePlacement 0x47d580): a gas building's on every
 * Geyser, a Terran type's by the neutral add-ons it could take where the builder has explored (0x413150); they go with
 * the mode. The state dump leaves them out, as the original's (tools/native_state.py). */
static void placement_ghosts(KpT *T)
{
	const char *terran02 = "generated/missions/terran/terran02.kpm", *terran09 = "generated/missions/terran/terran09.kpm";
	int64_t found[8];
	char got[256];
	KpBuf *lines;
	const char *p;
	KpGame *g;
	int count, k;
	if (!kp_t_has(T, DATA) || !kp_t_has(T, terran02) || !kp_t_has(T, terran09)) return;
	g = placement_game(T, terran02);
	count = units_of(g, GEYSER, found, 8);
	kp_t_eq(T, count, 2, "terran02's Geysers");
	kp_placement_start(g, PLACE, REFINERY);
	for (k = 0; k < count; k++) {
		previews(g, found[k], got, sizeof got);
		kp_t_eq_str(T, got, "583 0,0;581 0,0", "a Refinery's ghost and its footprint on Geyser %lld", (long long)found[k]);
	}
	lines = kp_t_buf(T);
	kp_t_ok(T, kp_state_lines(g, 0, lines) == 0, "out of memory");
	for (p = kp_buf_text(lines); *p; p = strchr(p, '\n') ? strchr(p, '\n') + 1 : p + strlen(p)) {
		kp_t_ok(T, strncmp(p, "image 5092", 10) != 0 && strncmp(p, "image 5156", 10) != 0, "the dump leaves the ghosts out");
	}
	kp_placement_cancel(g);
	for (k = 0; k < count; k++) {
		previews(g, found[k], got, sizeof got);
		kp_t_eq_str(T, got, "", "the ghosts go with the mode");
	}
	g = placement_game(T, terran09);
	count = units_of(g, SILO, found, 8);
	kp_t_eq(T, count, 2, "terran09's neutral Nuclear Silos");
	kp_placement_start(g, PLACE, COMMAND_CENTER);
	for (k = 0; k < count; k++) {
		previews(g, found[k], got, sizeof got);
		kp_t_eq_str(T, got, "", "an unexplored place shows no ghost");
	}
	kp_placement_cancel(g);
	kp_tiles_clear(g, 0, 0, 256, 256, 0xff00); /* every tile explored */
	kp_placement_start(g, PLACE, COMMAND_CENTER);
	for (k = 0; k < count; k++) {
		previews(g, found[k], got, sizeof got);
		kp_t_eq_str(T, got, "583 -96,-16;581 -96,-16", "a Command Center's ghost by Silo %lld", (long long)found[k]);
	}
	kp_placement_cancel(g);
}

static const KpTest TESTS[] = {
	{ "int_wrap", int_wrap, 0 },
	{ "int_mul_boundaries", int_mul_boundaries, 0 },
	{ "int_mul_matches_uint64", int_mul_matches_uint64, 0 },
	{ "int_division_truncates", int_division_truncates, 0 },
	{ "int_shifts", int_shifts, 0 },
	{ "int_bitwise", int_bitwise, 0 },
	{ "int_fixed_point_8", int_fixed_point_8, 0 },
	{ "containers_verify", containers_verify, 6 },
	{ "container_errors", container_errors, 0 },
	{ "loaders_refuse_other_kinds", loaders_refuse_other_kinds, 0 },
	{ "missions_load", missions_load, 0 },
	{ "mission_rejects_wrapping_lengths", mission_rejects_wrapping_lengths, 0 },
	{ "native_events", native_events, 0 },
	{ "unit_sounds", unit_sounds, 0 },
	{ "core_failures_leave_the_game_usable", core_failures_leave_the_game_usable, 0 },
	{ "placement_ghosts", placement_ghosts, 0 },
};
KP_SUITE(kp_suite_kp_core, "test_kp_core", TESTS);
