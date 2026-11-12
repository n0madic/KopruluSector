/* The native core (ADR 0008) on its own: the x86 integer helpers (kp_int.h), the container reader's errors, every
 * oracle mission built from the original data, the event ring of a native game, the unit sounds, a C failure under a
 * handler, the placement ghosts and the UTF-8 decoder (kp_utf8.h). */
#include "kp_test.h"

#include "../../native/kp_core/src/core/kp_int.h"
#include "../../native/kp_core/src/core/kp_kpf.h"
#include "../../native/kp_core/src/core/kp_terrain.h"
#include "../../native/kp_core/src/core/kp_utf8.h"

#define DATA KP_T_DATA
#define BASIC "tests/fixtures/kpf/basic.kpd"

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

/* The missions of the initial-state and scenario goldens, by key ("missions/<campaign>/<name>", "scenarios/<name>"). */
static int golden_missions(KpT *T, char keys[][512], int max)
{
	static const char *const DIRS[2] = { "initial_state", "scenarios" };
	int count = 0, d, k, i;
	for (d = 0; d < 2; d++) {
		char path[512];
		const KpJson *index;
		snprintf(path, sizeof path, "tests/golden/oracle/%s/index.json", DIRS[d]);
		index = kp_t_golden(T, path);
		for (k = 0; k < kp_json_len(index) && count < max; k++) {
			const char *m;
			char key[512];
			snprintf(path, sizeof path, "tests/golden/oracle/%s/%s.json", DIRS[d], kp_json_str(kp_json_at(index, k), ""));
			m = kp_json_field_str(kp_t_golden(T, path), "mission", "");
			snprintf(key, sizeof key, strncmp(m, "scenarios/", 10) == 0 ? "%s" : "missions/%s", m);
			for (i = 0; i < count && strcmp(keys[i], key) != 0; i++) {
			}
			if (i == count) snprintf(keys[count++], 512, "%s", key);
		}
	}
	return count;
}

#define MAX_FILES 512

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

/* Every mission of the oracle suites builds from the original data. */
static void missions_load(KpT *T)
{
	char (*keys)[512];
	int count, k;
	if (!kp_t_has(T, DATA)) return;
	keys = (char (*)[512])malloc(MAX_FILES * 512);
	kp_t_ok(T, keys != NULL, "out of memory");
	kp_t_defer(T, free, keys);
	count = golden_missions(T, keys, MAX_FILES);
	for (k = 0; k < count; k++) {
		KpMission m;
		KpError err;
		kp_t_ok(T, kp_oracle_mission(".", keys[k], &m, &err) != NULL, "%s", err.msg);
		kp_mission_free(&m);
	}
	kp_t_ok(T, count >= 64, "missions checked: %d", count);
}

/* --- games --- */

static const KpJson *scenario(KpT *T, const char *name)
{
	char path[512];
	snprintf(path, sizeof path, "tests/golden/oracle/scenarios/%s.json", name);
	return kp_t_golden(T, path);
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
	o.host.events = events;
	memset(k, 0, sizeof *k);
	k->T = T;
	gr = kp_t_golden_run(T, scenario(T, name), &o, 0);
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
	o.host.events = 1;
	gr = kp_t_golden_run(T, scenario(T, "marine_group"), &o, 0);
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
	gr = kp_t_golden_run(T, scenario(T, "marine_group"), NULL, 0);
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
/* The map's creep tiles and the receding ones (kp_creep_tiles, the play checks' count): creep_recede's colony and pool
 * have creep and none of it recedes at the start; a creep tile marked receding counts as one. */
static void creep_tiles_count_the_map(KpT *T)
{
	const char *path = "scenarios/creep_recede";
	KpGame *g;
	int64_t i, n, receding, marked = -1;
	if (!kp_t_has(T, DATA) || !kp_t_has(T, path)) return;
	g = kp_t_game(T, path, 1, 0, NULL)->g;
	n = kp_creep_tiles(g, &receding);
	kp_t_ok(T, n > 0 && receding == 0, "%lld creep tiles, %lld receding at the start", (long long)n, (long long)receding);
	for (i = 0; i < g->map_w * g->map_h && marked < 0; i++) {
		if ((g->terrain.raw_tiles[i] & 0x7ff0) == 0x10) marked = i; /* CV5 group 1 */
	}
	g->terrain.tiles[marked] |= 0x10000000; /* the creep engine's receding flag */
	kp_t_eq(T, kp_creep_tiles(g, &receding), n, "the same creep tiles");
	kp_t_eq(T, receding, 1, "one receding");
}

static void placement_ghosts(KpT *T)
{
	const char *terran02 = "missions/terran/terran02", *terran09 = "missions/terran/terran09";
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

/* The UTF-8 decoder (kp_utf8.h): each form's bounds, the forms table 3-7 refuses, and Unicode's own example of the
 * maximal subparts (3.9, table 3-8): a malformed sequence is one error up to the byte that cannot continue it. */
static void utf8_decodes_strictly(KpT *T)
{
	static const struct {
		const char *in;
		int32_t out[12];
		int count;
	} CASES[] = {
		{ "a\x7f", { 0x61, 0x7f }, 2 },
		{ "\xc2\x80\xdf\xbf", { 0x80, 0x7ff }, 2 },
		{ "\xe0\xa0\x80\xef\xbf\xbf\xe2\x82\xac", { 0x800, 0xffff, 0x20ac }, 3 },
		{ "\xf0\x90\x80\x80\xf4\x8f\xbf\xbf", { 0x10000, 0x10ffff }, 2 },
		{ "\xc0\xaf\xc1\xbf", { -1, -1, -1, -1 }, 4 },          /* overlong two-byte leads */
		{ "\xe0\x9f\xbf", { -1, -1, -1 }, 3 },                  /* overlong three-byte */
		{ "\xed\xa0\x80\xed\x9f\xbf", { -1, -1, -1, 0xd7ff }, 4 }, /* a surrogate, then the last before them */
		{ "\xf0\x8f\xbf\xbf", { -1, -1, -1, -1 }, 4 },          /* overlong four-byte */
		{ "\xf4\x90\x80\x80\xf5\x80", { -1, -1, -1, -1, -1, -1 }, 6 }, /* past U+10FFFF */
		{ "\xe2\x82", { -1 }, 1 },                               /* cut short by the end */
		{ "\xc3" "c\xe2\x82" "d\x80" "e", { -1, 'c', -1, 'd', -1, 'e' }, 6 },
		{ "a\xf1\x80\x80\xe1\x80\xc2" "b\x80" "c\x80\xbf" "d", { 'a', -1, -1, -1, 'b', -1, 'c', -1, -1, 'd' }, 10 },
	};
	int c, k;
	for (c = 0; c < (int)(sizeof CASES / sizeof CASES[0]); c++) {
		const uint8_t *s = (const uint8_t *)CASES[c].in;
		size_t n = strlen(CASES[c].in), at = 0;
		for (k = 0; at < n; k++) {
			int32_t cp = kp_utf8_next(s, n, &at);
			kp_t_ok(T, k < CASES[c].count, "case %d: more characters than %d", c, CASES[c].count);
			kp_t_eq(T, cp, CASES[c].out[k], "case %d, character %d", c, k);
		}
		kp_t_eq(T, k, CASES[c].count, "case %d's characters", c);
		kp_t_eq(T, at, n, "case %d ends at its end", c);
	}
	/* the length bounds it: a sequence cut short by n is one error though the bytes past n would complete it */
	{
		static const uint8_t EURO[] = { 0xe2, 0x82, 0xac };
		size_t at = 0;
		kp_t_eq(T, kp_utf8_next(EURO, 2, &at), KP_UTF8_BAD, "cut short at 2 of 3 bytes");
		kp_t_eq(T, at, 2, "nothing past n taken");
		at = 0;
		kp_t_eq(T, kp_utf8_next(EURO, 3, &at), 0x20ac, "the whole of it");
		kp_t_eq(T, at, 3, NULL);
	}
}

/* The runner's sample of a long loop: the same items whatever the shards, about one in S, others under another seed. */
static void runner_samples_long_loops(KpT *T)
{
	enum { ITEMS = 2000, SHARDS = 8, PLAYS = 71 };
	static const unsigned SEEDS[2] = { 1, 123456789 };
	static const unsigned SPREAD_SEEDS[] = { 1, 123456789, 0xabcdef0, 0x5555555, 0x1234567, 0x7654321 };
	static char taken[2][ITEMS + 1];
	KpT *t = (KpT *)calloc(1, sizeof *t);
	int seed, k, shard, count = 0, differ = 0;
	kp_t_ok(T, t != NULL, "out of memory");
	kp_t_defer(T, free, t);
	t->test = "a_long_test";
	for (k = 1; k <= ITEMS; k++) kp_t_ok(T, kp_t_mine_sampled(t, k), "not sampled: item %d runs", k);
	t->sample = 1;
	for (k = 1; k <= ITEMS; k++) kp_t_ok(T, kp_t_mine_sampled(t, k), "a sample of 1: item %d runs", k);
	t->sample = 4;
	for (seed = 0; seed < 2; seed++) {
		t->seed = SEEDS[seed];
		t->shards = t->shard = 0;
		for (k = 1; k <= ITEMS; k++) taken[seed][k] = (char)kp_t_mine_sampled(t, k);
		t->shards = SHARDS;
		for (k = 1; k <= ITEMS; k++) {
			int runs = 0;
			for (shard = 1; shard <= SHARDS; shard++) {
				t->shard = shard;
				runs += kp_t_mine_sampled(t, k);
			}
			kp_t_eq(T, runs, taken[seed][k], "item %d runs once if it is taken, whatever the shards", k);
		}
		count = 0;
		for (k = 1; k <= ITEMS; k++) count += taken[seed][k];
		kp_t_ok(T, count > ITEMS / 5 && count < ITEMS * 3 / 10, "about one in 4: %d of %d", count, ITEMS);
	}
	for (k = 1; k <= ITEMS; k++) differ += taken[0][k] != taken[1][k];
	kp_t_ok(T, differ > ITEMS / 5, "another seed takes other items: %d differ", differ);
	t->seed = SEEDS[0];
	t->test = "another_long_test";
	t->shards = t->shard = 0;
	differ = 0;
	for (k = 1; k <= ITEMS; k++) differ += kp_t_mine_sampled(t, k) != taken[0][k];
	kp_t_ok(T, differ > ITEMS / 5, "another test takes other items: %d differ", differ);
	/* a seed's every bit counts: it comes from a hash, and only the high ones may differ */
	t->test = "a_long_test";
	t->seed = SEEDS[0] | (1u << 27);
	differ = 0;
	for (k = 1; k <= ITEMS; k++) differ += kp_t_mine_sampled(t, k) != taken[0][k];
	kp_t_ok(T, differ > ITEMS / 5, "a seed's high bit takes other items: %d differ", differ);
	/* a loop as short as the plays': its items spread over the shards, whose processes run at the same time */
	t->test = "campaign_plays_equal_the_original";
	for (seed = 0; seed < (int)(sizeof SPREAD_SEEDS / sizeof SPREAD_SEEDS[0]); seed++) {
		int used = 0;
		t->seed = SPREAD_SEEDS[seed];
		for (shard = 1; shard <= SHARDS; shard++) {
			int items = 0;
			t->shards = SHARDS;
			t->shard = shard;
			for (k = 1; k <= PLAYS; k++) items += kp_t_mine_sampled(t, k);
			used += items > 0;
		}
		kp_t_ok(T, used >= SHARDS - 2, "seed %u: %d of %d shards hold its items", SPREAD_SEEDS[seed], used, SHARDS);
	}
}

static const KpTest TESTS[] = {
	{ "runner_samples_long_loops", runner_samples_long_loops, 0 },
	{ "utf8_decodes_strictly", utf8_decodes_strictly, 0 },
	{ "int_wrap", int_wrap, 0 },
	{ "int_mul_boundaries", int_mul_boundaries, 0 },
	{ "int_mul_matches_uint64", int_mul_matches_uint64, 0 },
	{ "int_division_truncates", int_division_truncates, 0 },
	{ "int_shifts", int_shifts, 0 },
	{ "int_bitwise", int_bitwise, 0 },
	{ "int_fixed_point_8", int_fixed_point_8, 0 },
	{ "container_errors", container_errors, 0 },
	{ "missions_load", missions_load, 0 },
	{ "native_events", native_events, 0 },
	{ "unit_sounds", unit_sounds, 0 },
	{ "core_failures_leave_the_game_usable", core_failures_leave_the_game_usable, 0 },
	{ "creep_tiles_count_the_map", creep_tiles_count_the_map, 0 },
	{ "placement_ghosts", placement_ghosts, 0 },
};
KP_SUITE(kp_suite_kp_core, "test_kp_core", TESTS);
