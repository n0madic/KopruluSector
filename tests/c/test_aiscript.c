/* The AIScript VM against the original's code (plan phase 7A): the interpreter ExecuteAiScript 0x493a60, the scheduler
 * RunAiScripts 0x4938e0, the pool (CreateAiScript 0x494d90, FreeAiScript 0x493240), the directories' lookup
 * FindAiScriptOffset 0x494d20 and the helpers AddAttackUnits 0x4839f0, the defense lists (0x482550..0x482e40) and
 * FindScriptSpellcaster 0x4932a0 (docs/research/native-ai-opcodes.md).
 *
 * tests/golden/oracle/aiscript_cases.json holds the 89 isolated original-code probes of
 * tools/research/inspect_ai_opcodes_117.py, exported by tools/oracle/export_aiscript_cases.py. Each case is rebuilt on a
 * native game started from a scenario map: the probes' synthetic binaries go through the binaries' override
 * (kp_aiscript_program), their zeroed script records are pool slots the case creates, and the helpers the probes
 * called on their own run through the opcodes that call them on the probe's player. No case is skipped. */
#include "kp_test.h"

#define CASES "tests/golden/oracle/aiscript_cases.json"
#define MAP "scenarios/idle_marines"
#define SEED 946684819
#define PLAYERS 12
#define ROSTER 64        /* KP_AI_ROSTER */
#define DEFENSE 20       /* KP_AI_DEFENSE */
#define TANK 5
#define TANK_SIEGED 30
#define WRAITH 8
#define OP_ATTACK_ADD 0x0c
#define OP_DEFENSEBUILD_GG 0x13
#define SHOWN 20

static const char *const FILES[] = { KP_T_DATA, MAP, NULL };

/* A game of the probes' map (events on, no campaign row). */
static KpOracleGame *new_game(KpT *T) { return kp_t_probe_game(T, MAP, SEED, 0, 0); }

/* The bytes of a JSON list. */
static size_t bytes_of(const KpJson *list, uint8_t *out, size_t max)
{
	int k;
	for (k = 0; k < kp_json_len(list) && (size_t)k < max; k++) out[k] = (uint8_t)kp_json_int(kp_json_at(list, k), 0);
	return (size_t)k;
}

static void program(KpT *T, KpGame *g, const uint8_t *ai, size_t ai_size, const uint8_t *bw, size_t bw_size)
{
	kp_t_ok(T, kp_aiscript_program(g, ai, ai_size, bw, bw_size) == 0, "out of memory");
}

/* A game running the probe's binaries with a zeroed script of player p at PC 0 of binary bin (the probes' record). */
static KpGame *scripted(KpT *T, const uint8_t *code, size_t size, const uint8_t *other, size_t other_size, int64_t p,
	int bin, int64_t *s)
{
	KpGame *g = new_game(T)->g;
	program(T, g, code, size, other, other_size);
	*s = kp_aiscript_create(g, 0, 0, NULL, bin);
	g->scripts.player[*s] = p;
	return g;
}

typedef struct {
	const KpJson *c;
	KpJsonDoc *doc;
	KpJson *got; /* what the case ended with, compared with its expected */
	char diff[256];
} Case;

static KpJson *ints(KpJsonDoc *doc, const int64_t *v, int n)
{
	KpJson *a = kp_json_new(doc, KP_JSON_ARRAY);
	int k;
	for (k = 0; k < n; k++) kp_json_push_int(doc, a, v[k]);
	return a;
}

static void attack_roster(KpT *T, Case *x)
{
	const KpJson *c = x->c;
	int64_t unit = kp_json_field_int(c, "unit", 0), player = kp_json_field_int(c, "player", 0), s, p;
	uint8_t code[7];
	KpGame *g;
	int k;
	code[0] = OP_ATTACK_ADD;
	code[1] = (uint8_t)kp_json_field_int(c, "count", 0);
	code[2] = (uint8_t)(unit % 256);
	code[3] = (uint8_t)(unit / 256 % 256);
	code[4] = 0x02;
	code[5] = 0x07;
	code[6] = 0x00;
	g = scripted(T, code, 7, NULL, 0, player, 0, &s);
	p = player < 8 ? player : 0; /* a rejected player: the lists stay as they were */
	for (k = 0; k < kp_json_field_int(c, "occupied", 0); k++) g->pai.roster[p][k] = 7;
	kp_aiscript_execute(g, s);
	x->got = ints(x->doc, g->pai.roster[p], ROSTER);
}

static void defense_roster(KpT *T, Case *x)
{
	const KpJson *c = x->c;
	int64_t unit = kp_json_field_int(c, "unit", 0), list = kp_json_field_int(c, "list", 0), s;
	uint8_t code[7];
	KpGame *g;
	int k;
	code[0] = (uint8_t)(OP_DEFENSEBUILD_GG + list);
	code[1] = (uint8_t)kp_json_field_int(c, "count", 0);
	code[2] = (uint8_t)(unit % 256);
	code[3] = (uint8_t)(unit / 256 % 256);
	code[4] = 0x02;
	code[5] = 0x07;
	code[6] = 0x00;
	g = scripted(T, code, 7, NULL, 0, 0, 0, &s);
	for (k = 0; k < kp_json_field_int(c, "occupied", 0); k++) g->pai.defense[0][list][k] = 7;
	kp_aiscript_execute(g, s);
	x->got = ints(x->doc, g->pai.defense[0][list], DEFENSE);
}

static void training_opcode(KpT *T, Case *x)
{
	const KpJson *c = x->c;
	uint8_t code[256];
	int64_t s, got[3];
	KpGame *g = scripted(T, code, bytes_of(kp_json_get(c, "code"), code, sizeof code), NULL, 0, 0, 0, &s);
	g->count_all[TANK * PLAYERS] = kp_json_field_int(c, "all5", 0);
	g->count_all[TANK_SIEGED * PLAYERS] = kp_json_field_int(c, "all30", 0);
	g->count_completed[TANK * PLAYERS] = kp_json_field_int(c, "complete5", 0);
	g->count_completed[TANK_SIEGED * PLAYERS] = kp_json_field_int(c, "complete30", 0);
	kp_aiscript_execute(g, s);
	got[0] = g->scripts.pc[s];
	got[1] = g->scripts.wait[s];
	got[2] = g->pai.wanted_unit[0];
	x->got = ints(x->doc, got, 3);
}

static void seed_opcode(KpT *T, Case *x)
{
	const KpJson *c = x->c;
	uint8_t code[256];
	int64_t s, got[3];
	KpGame *g = scripted(T, code, bytes_of(kp_json_get(c, "code"), code, sizeof code), NULL, 0, 0, 0, &s);
	g->rng->state = (uint32_t)kp_json_field_int(c, "state", 0);
	kp_aiscript_execute(g, s);
	got[0] = g->rng->initial_seed;
	got[1] = g->rng->state;
	got[2] = g->scripts.pc[s];
	x->got = ints(x->doc, got, 3);
}

/* The probe's dispatch log ends at the PC of the last opcode run: here the wait the shared return lands on. */
static void cross_bin_shared_return(KpT *T, Case *x)
{
	const KpJson *c = x->c;
	uint8_t code[256], other[256];
	size_t n = bytes_of(kp_json_get(c, "code"), code, sizeof code), m = bytes_of(kp_json_get(c, "other"), other, sizeof other);
	int64_t a, b, got[5];
	KpGame *g = scripted(T, code, n, other, m, 0, 0, &a);
	b = kp_aiscript_create(g, 0, 0, NULL, 1);
	kp_aiscript_execute(g, a);
	got[0] = g->scripts.ret;
	kp_aiscript_execute(g, b);
	got[1] = g->scripts.ret;
	kp_aiscript_execute(g, a);
	got[2] = g->scripts.ret;
	got[3] = g->scripts.pc[a];
	got[4] = g->scripts.pc[a] - 3;
	x->got = ints(x->doc, got, 5);
}

/* The first two units of the active list get the probe's owners; the others become the script's player's. */
static void enemy_resource_first_owner(KpT *T, Case *x)
{
	const KpJson *c = x->c, *owners = kp_json_get(c, "owners"), *minerals = kp_json_get(c, "minerals");
	const KpJson *gas = kp_json_get(c, "gas");
	uint8_t code[256];
	int64_t s, n, got[2];
	int k = 0, i;
	KpGame *g = scripted(T, code, bytes_of(kp_json_get(c, "code"), code, sizeof code), NULL, 0, 0, 0, &s);
	for (n = g->lists[KP_LIST_ACTIVE].head; n != 0; n = g->u.next[n]) {
		g->u.owner[n] = kp_json_int(kp_json_at(owners, k), 0);
		k++;
	}
	g->players.alliance[0][0] = 1; /* the owners are the script's player's enemies */
	g->players.alliance[0][1] = 0;
	g->players.alliance[0][2] = 0;
	for (i = 0; i < kp_json_len(minerals); i++) g->players.minerals[strtol(minerals->keys[i], NULL, 10)] = kp_json_int(minerals->items[i], 0);
	for (i = 0; i < kp_json_len(gas); i++) g->players.gas[strtol(gas->keys[i], NULL, 10)] = kp_json_int(gas->items[i], 0);
	kp_aiscript_execute(g, s);
	got[0] = g->scripts.pc[s];
	got[1] = g->scripts.wait[s];
	x->got = ints(x->doc, got, 2);
}

/* Player 0's unit list holds the given units only, in order, each a Corsair (or the given type) with the given energy
 * and order; picked gets their records. */
static int casters(KpGame *g, const KpJson *units, int64_t *picked)
{
	int count = 0, k;
	int64_t n = g->lists[KP_LIST_ACTIVE].head;
	while (n != 0 && count < kp_json_len(units)) {
		picked[count++] = n;
		n = g->u.next[n];
	}
	g->player_units[0] = count ? picked[0] : 0;
	for (k = 0; k < count; k++) {
		const KpJson *u = kp_json_at(units, k);
		int64_t m = picked[k];
		g->u.player_next[m] = k + 1 < count ? picked[k + 1] : 0;
		g->u.type[m] = kp_json_field_int(u, "type", 60);
		g->u.energy[m] = kp_json_field_int(u, "energy", 0);
		g->u.order[m] = kp_json_field_int(u, "order", 0);
	}
	return count;
}

static void spell_unit_eligibility(KpT *T, Case *x)
{
	const KpJson *c = x->c;
	KpGame *g = new_game(T)->g;
	KpJsonDoc *doc = x->doc;
	KpJson *units = kp_json_new(doc, KP_JSON_ARRAY), *u = kp_json_new(doc, KP_JSON_OBJECT);
	int64_t picked[8], got;
	int expected = kp_json_truthy(kp_json_get(c, "expected"));
	kp_json_set_int(doc, u, "type", kp_json_field_int(c, "type", 60));
	kp_json_set_int(doc, u, "energy", kp_json_field_int(c, "energy", 0));
	kp_json_set_int(doc, u, "order", kp_json_field_int(c, "order", 0));
	kp_json_push(doc, units, u);
	casters(g, units, picked);
	got = kp_ai_script_caster(g, 0, 60, kp_json_field_int(c, "minimum", 0));
	if ((got == picked[0]) != expected || (got != 0 && got != picked[0])) {
		snprintf(x->diff, sizeof x->diff, "got unit %lld, original %s", (long long)got, expected ? "true" : "false");
	}
}

static void spell_unit_first_eligible(KpT *T, Case *x)
{
	const KpJson *c = x->c;
	KpGame *g = new_game(T)->g;
	int64_t picked[16], got, first = kp_json_field_int(c, "first", 0);
	int count = casters(g, kp_json_get(c, "units"), picked);
	got = kp_ai_script_caster(g, 0, 60, kp_json_field_int(c, "minimum", 0));
	if (first >= count || got != picked[first]) {
		snprintf(x->diff, sizeof x->diff, "got unit %lld, original the record %lld", (long long)got, (long long)first + 1);
	}
}

/* The probe's directories: each binary's table at offset 64 holds one record, the zeroed bytes after it end it. */
static void bin_id_resolution(KpT *T, Case *x)
{
	const KpJson *c = x->c;
	KpGame *g = new_game(T)->g;
	uint8_t ai[64 + 16 + 16], bw[64 + 8 + 8];
	const char *requested = kp_json_field_str(c, "requested", "");
	int64_t got[2];
	int bin = -1;
	memset(ai, 0, sizeof ai);
	memset(bw, 0, sizeof bw);
	kp_le_put32(ai, 64);
	memcpy(ai + 64, kp_json_field_str(c, "ai_id", "????"), 4);
	kp_le_put32(ai + 68, (uint32_t)kp_json_field_int(c, "ai_offset", 0));
	kp_le_put32(ai + 72, 7);
	kp_le_put32(ai + 76, 0xffffffffu);
	kp_le_put32(bw, 64);
	memcpy(bw + 64, kp_json_field_str(c, "bw_id", "????"), 4);
	kp_le_put32(bw + 68, (uint32_t)kp_json_field_int(c, "bw_offset", 0));
	program(T, g, ai, sizeof ai, bw, sizeof bw);
	kp_t_ok(T, strlen(requested) == 4, "a script id of %d characters", (int)strlen(requested));
	got[0] = kp_ai_bins_find(g->ai_bins, kp_le_u32((const uint8_t *)requested), &bin);
	got[1] = bin;
	x->got = ints(x->doc, got, 2);
}

/* The probe created its parent on a fresh pool and ran the scheduler twice. */
static void multirun_scheduler(KpT *T, Case *x)
{
	const KpJson *c = x->c;
	KpGame *g = new_game(T)->g;
	KpScripts *sc = &g->scripts;
	KpJsonDoc *doc = x->doc;
	uint8_t code[256];
	int64_t parent, child, first[3];
	program(T, g, code, bytes_of(kp_json_get(c, "code"), code, sizeof code), (const uint8_t *)"", 0);
	kp_ai_init(g);
	parent = kp_aiscript_create(g, 0, 0, NULL, 0);
	kp_aiscript_run(g);
	child = sc->head;
	first[0] = sc->pc[child];
	first[1] = sc->wait[child];
	first[2] = sc->next[child];
	kp_aiscript_run(g);
	x->got = kp_json_new(doc, KP_JSON_ARRAY);
	kp_json_push_int(doc, x->got, first[0]);
	kp_json_push_int(doc, x->got, first[1]);
	if (first[2] == parent) kp_json_push(doc, x->got, kp_json_new_string(doc, "parent", 6));
	else kp_json_push_int(doc, x->got, first[2]);
	kp_json_push_int(doc, x->got, sc->pc[child]);
	kp_json_push_int(doc, x->got, sc->wait[child]);
	if (sc->head == child) kp_json_push(doc, x->got, kp_json_new_string(doc, "child", 5));
	else kp_json_push_int(doc, x->got, sc->head);
}

static void flagged_slot_reuse(KpT *T, Case *x)
{
	KpGame *g = new_game(T)->g;
	KpJsonDoc *doc = x->doc;
	int64_t first, reused, flags, pc;
	kp_ai_init(g);
	first = kp_aiscript_create(g, 0, 0, NULL, 0);
	g->scripts.flags[first] = 6;
	kp_aiscript_free(g, first);
	reused = kp_aiscript_create(g, 1, 10, NULL, 1);
	flags = g->scripts.flags[reused];
	pc = g->scripts.pc[reused];
	kp_aiscript_run(g);
	/* the killed script is freed without running: its PC stays where the creation put it */
	x->got = kp_json_new(doc, KP_JSON_ARRAY);
	kp_json_push(doc, x->got, kp_json_new_bool(doc, first == reused));
	kp_json_push_int(doc, x->got, flags);
	kp_json_push_int(doc, x->got, g->scripts.head);
	kp_json_push_int(doc, x->got, g->scripts.pc[reused] == pc ? 0 : 1);
}

/* The probe's list: the script (kill_thread, then a long wait), a killable victim of another player waiting 2, and an
 * unmarked one waiting long. */
static void delayed_cross_player_kill(KpT *T, Case *x)
{
	const KpJson *c = x->c;
	KpGame *g = new_game(T)->g;
	KpScripts *sc = &g->scripts;
	KpJsonDoc *doc = x->doc;
	uint8_t code[256];
	int64_t unmarked, victim, script;
	int k;
	program(T, g, code, bytes_of(kp_json_get(c, "code"), code, sizeof code), (const uint8_t *)"", 0);
	kp_ai_init(g);
	unmarked = kp_aiscript_create(g, 0, 0, NULL, 0);
	victim = kp_aiscript_create(g, kp_json_field_int(c, "victim_player", 0), 0, NULL, 0);
	script = kp_aiscript_create(g, 0, 0, NULL, 0);
	sc->flags[victim] = kp_json_field_int(c, "victim_flags", 0);
	sc->wait[victim] = kp_json_field_int(c, "victim_wait", 0);
	kp_aiscript_execute(g, script);
	x->got = kp_json_new(doc, KP_JSON_ARRAY);
	kp_json_push_int(doc, x->got, sc->flags[victim]);
	sc->wait[unmarked] = kp_json_field_int(c, "unmarked_wait", 0);
	for (k = 0; k < 3; k++) {
		kp_aiscript_run(g);
		kp_json_push(doc, x->got, kp_json_new_bool(doc, sc->next[script] == victim));
	}
}

static const struct {
	const char *kind;
	void (*fn)(KpT *T, Case *x);
} RUN[] = {
	{ "attack_roster", attack_roster }, { "defense_roster", defense_roster }, { "training_opcode", training_opcode },
	{ "seed_opcode", seed_opcode }, { "cross_bin_shared_return", cross_bin_shared_return },
	{ "enemy_resource_first_owner", enemy_resource_first_owner }, { "spell_unit_eligibility", spell_unit_eligibility },
	{ "spell_unit_first_eligible", spell_unit_first_eligible }, { "bin_id_resolution", bin_id_resolution },
	{ "multirun_scheduler", multirun_scheduler }, { "flagged_slot_reuse", flagged_slot_reuse },
	{ "delayed_cross_player_kill", delayed_cross_player_kill },
};

static void run_case(KpT *T, void *ud)
{
	Case *x = (Case *)ud;
	const char *kind = kp_json_field_str(x->c, "kind", "");
	size_t k;
	for (k = 0; k < sizeof RUN / sizeof RUN[0]; k++) {
		if (strcmp(RUN[k].kind, kind) == 0) {
			RUN[k].fn(T, x);
			if (x->got && !kp_json_equal(x->got, kp_json_get(x->c, "expected"))) {
				KpBuf a, b;
				kp_buf_init(&a);
				kp_buf_init(&b);
				kp_json_write(&a, x->got);
				kp_json_write(&b, kp_json_get(x->c, "expected"));
				snprintf(x->diff, sizeof x->diff, "got %s, original %s", kp_buf_text(&a), kp_buf_text(&b));
				kp_buf_free(&a);
				kp_buf_free(&b);
			}
			return;
		}
	}
	kp_t_fail(T, "%s", kind);
}

static void vm_equals_the_original(KpT *T)
{
	const KpJson *golden, *cases;
	KpBuf *bad;
	int k, failures = 0;
	if (!kp_t_ready(T, "test_aiscript", FILES)) return;
	golden = kp_t_golden(T, CASES);
	cases = kp_json_get(golden, "cases");
	bad = kp_t_buf(T);
	for (k = 0; k < kp_json_len(cases); k++) {
		KpJsonDoc doc;
		Case x;
		char failure[512];
		int mark = kp_t_mark(T);
		kp_json_doc_init(&doc);
		memset(&x, 0, sizeof x);
		x.c = kp_json_at(cases, k);
		x.doc = &doc;
		if (kp_t_pcall(T, run_case, &x, failure, sizeof failure) != 0) snprintf(x.diff, sizeof x.diff, "%s", failure);
		kp_t_release(T, mark);
		kp_json_doc_free(&doc);
		if (x.diff[0] && failures++ < SHOWN) kp_buf_printf(bad, "\n%d %s: %s", k + 1, kp_json_field_str(x.c, "kind", ""), x.diff);
	}
	kp_t_ok(T, failures == 0, "%d of %d cases differ:%s", failures, kp_json_len(cases), kp_buf_text(bad));
}

/* The launch gate (LaunchAiScript 0x493970): the binaries' own directories resolve the scripts. */
static void launch_gate(KpT *T)
{
	KpGame *g;
	int bin = -1;
	uint32_t off;
	if (!kp_t_ready(T, "test_aiscript", FILES)) return;
	g = new_game(T)->g;
	off = kp_ai_bins_find(g->ai_bins, kp_le_u32((const uint8_t *)"Ter3"), &bin);
	kp_t_ok(T, off > 0 && bin == 0, "Ter3 is in aiscript.bin");
	off = kp_ai_bins_find(g->ai_bins, kp_le_u32((const uint8_t *)"PB1A"), &bin);
	kp_t_ok(T, off > 0 && bin == 1, "PB1A is in bwscript.bin");
	kp_t_eq(T, kp_ai_bins_find(g->ai_bins, kp_le_u32((const uint8_t *)"Nope"), &bin), 0, "an unknown id");
}

/* Binaries too small for a directory record hold no script: the lookup reads none of their bytes (a directory offset
 * of 0xffffffff in them would point far outside). */
static void small_binaries_hold_no_scripts(KpT *T)
{
	uint8_t ff[8];
	KpGame *g;
	int bin, size;
	if (!kp_t_ready(T, "test_aiscript", FILES)) return;
	g = new_game(T)->g;
	memset(ff, 0xff, sizeof ff);
	for (size = 0; size <= 7; size++) {
		program(T, g, ff, (size_t)size, ff, (size_t)size);
		kp_t_eq(T, kp_ai_bins_find(g->ai_bins, kp_le_u32((const uint8_t *)"Ter3"), &bin), 0, "%d bytes", size);
	}
	program(T, g, NULL, 0, NULL, 0);
	kp_t_ok(T, kp_ai_bins_find(g->ai_bins, kp_le_u32((const uint8_t *)"Ter3"), &bin) > 0, "the game's binaries again");
}

/* A lost roster entry (MarkRosterUnitLost 0x4839a0) in PrepareAttack 0x4af300: the flags of an entry one past the last
 * type are the next column's first byte, the Marine's target acquisition range (0x5219f0); it stopped the game before. */
static void lost_roster_entry_reads_the_next_column(KpT *T)
{
	KpOracleGame *og;
	KpGame *g;
	int64_t t;
	if (!kp_t_ready(T, "test_aiscript", FILES)) return;
	og = new_game(T);
	g = og->g;
	for (t = 0; t < KP_UNIT_TYPES; t++) {
		if (kp_ai_roster_flags(g, t + 1) != (kp_t_dat(T, og, "units", "special_ability_flags", t) & 0xff)) break;
	}
	kp_t_eq(T, t, KP_UNIT_TYPES, "a type's entry reads the low byte of its special ability flags");
	kp_t_eq(T, kp_ai_roster_flags(g, KP_AI_ROSTER_LOST), kp_t_dat(T, og, "units", "target_acquisition_range", 0),
		"a lost entry reads the Marine's target acquisition range");
	g->pai.roster[1][0] = WRAITH + 1;
	g->pai.roster[1][1] = KP_AI_ROSTER_LOST;
	kp_ai_prepare_attack(g, 1, 1000, 1000, 1, 0);
	kp_t_ok(T, g->w->error[0] == '\0', "the attack is prepared with a lost entry: %s", g->w->error);
}

/* Every type the stock scripts' defense opcodes name (docs/research/evidence/ai-defense-strength.json, made by
 * tools/research/inspect_ai_defense_strength.py) has strength in the direction its list is credited by
 * (FulfilStrengthRecipe 0x4ac3c0: ag and aa by air, gg and ga by ground), so a nonempty build list always gains credit
 * and the regional production's loop (0x4af930, kp_ai_military.c recipe_stalled) never stalls on them. */
static void stock_defense_types_have_strength(KpT *T)
{
	static const int64_t AIR[] = { 0, 1, 3, 8, 12, 38, 43, 47, 58, 60, 62, 66, 68, 70, 71, 72 };
	static const int64_t GROUND[] = { 0, 1, 2, 3, 5, 8, 12, 32, 37, 38, 39, 43, 44, 50, 61, 65, 66, 68, 70, 71, 72, 83,
		103 };
	KpGame *g;
	size_t k;
	if (!kp_t_ready(T, "test_aiscript", FILES)) return;
	g = new_game(T)->g;
	for (k = 0; k < sizeof AIR / sizeof *AIR; k++) {
		kp_t_ok(T, g->strength[0][AIR[k]] > 0, "type %lld has air strength", (long long)AIR[k]);
	}
	for (k = 0; k < sizeof GROUND / sizeof *GROUND; k++) {
		kp_t_ok(T, g->strength[1][GROUND[k]] > 0, "type %lld has ground strength", (long long)GROUND[k]);
	}
	kp_t_eq(T, g->strength[1][0], 100, "the weakest gg entry, the Marine");
	kp_t_eq(T, g->strength[0][47], 70, "the weakest ag and aa entry, the Scourge");
	kp_t_eq(T, g->strength[1][WRAITH], 130, "the weakest ga entry, the Wraith");
	kp_t_eq(T, g->strength[0][32], 0, "a gg entry without air strength, the Firebat: the direction matters");
}

/* One script run once under kp_t_pcall: a failure lands in the harness, whose setjmp is out of the caller's frame (a
 * static helper with its own setjmp is inlined by GCC, and -Wclobbered flags the caller's locals). */
typedef struct {
	KpGame *g;
	int64_t s;
} Once;

static void execute_once(KpT *T, void *ud)
{
	const Once *o = (const Once *)ud;
	(void)T;
	kp_aiscript_execute(o->g, o->s);
}

/* The AIOP entry of opcode `op` (scdata/analyze/ai.py section): its length, the opcode byte and its operands' widths,
 * and whether one operand is an address; -1 when the table does not hold it. */
#define CHECK_FILL 0x24      /* stop: every operand byte, so a word operand reads 0x2424 */
#define CHECK_TARGET 0x2424  /* where a jump of the one-opcode script lands */
static int aiop_entry(const KpDefsTable *t, int op, int *address)
{
	uint32_t at = 1, k, i;
	*address = 0;
	if (!t || t->size < 1) return -1;
	for (k = 0; k < t->data[0]; k++) {
		uint32_t n;
		if (at + 2 > t->size) return -1;
		n = t->data[at + 1];
		if (at + 2 + n > t->size) return -1;
		if ((int)k == op) {
			int length = 1;
			for (i = 0; i < n; i++) {
				length += t->data[at + 2 + i] & 0x7f;
				if (t->data[at + 2 + i] & 0x80) *address = 1;
			}
			return length;
		}
		at += 2 + n;
	}
	return -1;
}

/* The PC of the second instruction script s traced (KP_EVENT_AI: pc << 8 | op), -1 when it ran one only. */
static int64_t second_pc(KpGame *g, int64_t s)
{
	KpEvent e;
	int64_t pc = -1;
	int seen = 0;
	while (kp_event_take(g, &e)) {
		if (e.kind != KP_EVENT_AI || e.b >> 1 != s) continue;
		if (++seen == 2) pc = e.c >> 8;
	}
	return pc;
}

/* Every ordinal to 0x70 has the interpreter's case and AIOP is its operands. A script of one opcode (every other byte
 * a stop, so its operands read 0x24 and its jumps and return land on a stop) never stops the game for a missing case;
 * and when it goes on to a second instruction, that one is past the operands AIOP gives it (the interpreter read as
 * many bytes as kp_aiscript_check skips), or a jump's target when AIOP has an address operand. */
static void every_opcode_reads_its_aiop_operands(KpT *T)
{
	static uint8_t code[0x2500];
	const KpDefsTable *aiop = kp_defs_table(kp_defs_profile("bw-1.17.0.1"), "AIOP");
	int op, mismatches = 0;
	if (!kp_t_ready(T, "test_aiscript", FILES)) return;
	kp_t_ok(T, aiop != NULL, "the profile's AIOP");
	for (op = 0; op <= 0x72; op++) {
		KpOracleGame *og = new_game(T);
		KpGame *g = og->g;
		int64_t s, next;
		char want[32];
		int stopped, length, address;
		char error[KP_ERROR_SIZE];
		KpEvent e;
		Once once;
		memset(code, CHECK_FILL, sizeof code);
		code[0] = (uint8_t)op;
		program(T, g, code, sizeof code, code, sizeof code);
		s = kp_aiscript_create(g, 0, 0, NULL, 0);
		g->scripts.ret = 1; /* return lands on a stop too */
		while (kp_event_take(g, &e)) {
		}
		g->events.trace = 1;
		once.g = g;
		once.s = s;
		kp_t_pcall(T, execute_once, &once, error, sizeof error);
		snprintf(want, sizeof want, "ai: opcode 0x%02x without", (unsigned)op);
		stopped = strncmp(error, want, strlen(want)) == 0;
		if (stopped) {
			kp_t_ok(T, 0, "opcode 0x%02x: %s", (unsigned)op, error);
			mismatches++;
		}
		next = second_pc(g, s);
		length = aiop_entry(aiop, op, &address);
		if (op <= 0x70 && next >= 0 && next != length && !(address && next == CHECK_TARGET)) {
			kp_t_ok(T, 0, "opcode 0x%02x: the interpreter goes on at %lld, AIOP's length is %d%s", (unsigned)op,
				(long long)next, length, address ? " (or its target)" : "");
			mismatches++;
		}
		kp_t_game_free(T, og);
		if (mismatches > 4) break;
	}
}

/* Binaries of one script each: aiscript.bin's directory (16-byte records) at 64 names 'Tst1'..'Tst5', bwscript.bin's
 * (8-byte records) at 32 names 'Bpan'. */
#define CHECK_AI_SIZE 176
#define CHECK_BW_SIZE 48
static void check_bins(KpT *T, KpAiBins *b, uint8_t *ai, uint8_t *bw)
{
	static const struct {
		const char *id;
		uint32_t offset;
	} AI[] = { { "Tst1", 4 }, { "Tst2", 16 }, { "Tst3", 24 }, { "Tst4", 30 }, { "Tst5", 40 } };
	const KpDefsTable *t = kp_defs_table(kp_defs_profile("bw-1.17.0.1"), "AIOP");
	size_t k;
	memset(ai, 0, CHECK_AI_SIZE);
	memset(bw, 0, CHECK_BW_SIZE);
	kp_le_put32(ai, 64);
	for (k = 0; k < sizeof AI / sizeof AI[0]; k++) {
		memcpy(ai + 64 + 16 * k, AI[k].id, 4);
		kp_le_put32(ai + 64 + 16 * k + 4, AI[k].offset);
	}
	/* Tst1: goto 10; at 7 fatal_error (never reached); at 10 stop */
	ai[4] = 0x00, kp_le_put16(ai + 5, 10), ai[7] = 0x28, ai[10] = 0x24;
	/* Tst2: random_jump 128 to 7 (fatal_error), then stop */
	ai[16] = 0x30, ai[17] = 128, kp_le_put16(ai + 18, 7), ai[20] = 0x24;
	/* Tst3: goto 0x7fff, outside */
	ai[24] = 0x00, kp_le_put16(ai + 25, 0x7fff);
	/* Tst4: call 36, then an ordinal past the table (freed as stop); at 36 return */
	ai[30] = 0x40, kp_le_put16(ai + 31, 36), ai[33] = 0x71, ai[36] = 0x41;
	/* Tst5: wait_secure (0x0f), a melee opcode, then fatal_error */
	ai[40] = 0x0f, ai[41] = 0x28;
	/* Bpan: panic 7 (the panic block runs from aiscript.bin, where 7 is fatal_error), then stop */
	kp_le_put32(bw, 32);
	memcpy(bw + 32, "Bpan", 4);
	kp_le_put32(bw + 36, 4);
	bw[4] = 0x44, kp_le_put16(bw + 5, 7), bw[7] = 0x24;
	memset(b, 0, sizeof *b);
	b->bin[0] = ai;
	b->bin_size[0] = CHECK_AI_SIZE;
	b->bin[1] = bw;
	b->bin_size[1] = CHECK_BW_SIZE;
	kp_t_ok(T, t != NULL, "the profile's AIOP");
	b->operands = t ? t->data : NULL;
	b->operands_size = t ? t->size : 0;
}

static uint32_t script_id(const char *id) { return kp_le_u32((const uint8_t *)id); }

/* kp_aiscript_check over synthetic binaries: what the closure reaches is checked, what it does not is left. */
static void scripts_are_checked_before_a_game(KpT *T)
{
	static const struct {
		const char *id;
		const char *want; /* NULL: passes */
	} CHECKS[] = {
		{ "Tst1", NULL },
		{ "Tst2", "AI script 'Tst2' reaches fatal_error at aiscript.bin+0x7" },
		{ "Tst3", "AI script 'Tst3' reads PC 0x7fff outside aiscript.bin" },
		{ "Tst4", NULL },
		{ "Tst5", "AI script 'Tst5' reaches fatal_error at aiscript.bin+0x29" },
		{ "Bpan", "AI script 'Bpan' reaches fatal_error at aiscript.bin+0x7" },
		{ "None", NULL }, /* no binary has it: the launch does nothing */
	};
	uint8_t ai[CHECK_AI_SIZE], bw[CHECK_BW_SIZE], trig[2400];
	KpAiBins b;
	KpMission m;
	KpError err;
	char why[KP_ERROR_SIZE];
	size_t k;
	check_bins(T, &b, ai, bw);
	for (k = 0; k < sizeof CHECKS / sizeof CHECKS[0]; k++) {
		int rc = kp_aiscript_check(&b, script_id(CHECKS[k].id), why, sizeof why);
		if (CHECKS[k].want) kp_t_ok(T, rc == -1 && strstr(why, CHECKS[k].want) != NULL, "%s: %s", CHECKS[k].id, why);
		else kp_t_eq(T, rc, 0, "%s: %s", CHECKS[k].id, why);
	}
	/* a truncated operand at the end of the binary */
	ai[CHECK_AI_SIZE - 2] = 0x00, ai[CHECK_AI_SIZE - 1] = 0x01;
	kp_le_put32(ai + 64 + 4, CHECK_AI_SIZE - 2);
	kp_t_ok(T, kp_aiscript_check(&b, script_id("Tst1"), why, sizeof why) == -1 && strstr(why, "outside aiscript.bin"),
		"a truncated goto: %s", why);
	b.operands_size--;
	kp_t_ok(T, kp_aiscript_check(&b, script_id("Tst4"), why, sizeof why) == -1 && strstr(why, "(AIOP) is damaged"),
		"a damaged table: %s", why);
	/* the mission's Run AI Script actions that run (action 0 of trigger 0) */
	check_bins(T, &b, ai, bw);
	memset(&m, 0, sizeof m);
	memset(trig, 0, sizeof trig);
	m.triggers.data = trig;
	m.triggers.size = sizeof trig;
	trig[320 + 0x1a] = 15; /* Run AI Script */
	kp_le_put32(trig + 320 + 0x14, script_id("Tst2"));
	kp_t_ok(T, kp_mission_check_ai(&m, &b, &err) == -1 && strstr(err.msg, "TRIG: trigger 0 action 0: AI script 'Tst2'"),
		"Run AI Script: %s", err.msg);
	trig[320 + 0x1a] = 16; /* Run AI Script At */
	kp_t_eq(T, kp_mission_check_ai(&m, &b, &err), -1, "Run AI Script At");
	trig[320 + 0x1c] = 0x02; /* ignored */
	kp_t_eq(T, kp_mission_check_ai(&m, &b, &err), 0, "an ignored action: %s", err.msg);
	trig[320 + 0x1c] = 0;
	memcpy(trig + 320 + 2 * 32, trig + 320, 32); /* after the first action of type 0: never run */
	memset(trig + 320, 0, 32);
	kp_t_eq(T, kp_mission_check_ai(&m, &b, &err), 0, "an action after the end: %s", err.msg);
}

#define OP_DEBUG 0x27
#define OP_FATAL 0x28
#define OP_WAIT_SECURE 0x0f
#define OP_BUILD_TURRETS 0x34
#define OP_WAIT_TURRETS 0x35
#define OP_IMPLODE 0x5b
#define GUARDED 0x01
#define WANTS_TURRET 0x10
#define RETRY_WAIT 30
#define BUDGET_REFRESHED 0x1 /* KP_AIF_BUDGET_REFRESHED */
#define STATE_IDLE 0
#define STATE_WATCH 4
#define STATE_BASE 5

/* One opcode run from PC 0 of aiscript.bin for player 0 (every other byte a stop); the error it stopped with. */
static KpGame *run_opcode(KpT *T, uint8_t *code, size_t size, int64_t *s, char *error, size_t error_size)
{
	KpGame *g = scripted(T, code, size, code, size, 0, 0, s);
	Once once;
	KpEvent e;
	while (kp_event_take(g, &e)) {
	}
	once.g = g;
	once.s = *s;
	kp_t_pcall(T, execute_once, &once, error, error_size);
	return g;
}

/* The melee scripts' opcodes as ExecuteAiScript 0x493a60 runs them: debug shows its text from aiscript.bin as the
 * player's chat and jumps; fatal_error is the original's fatal error; wait_secure retries until the budget flag;
 * build_turrets flags the guarded regions without a Missile Turret (six at most) and wait_turrets retries while one is
 * flagged; implode turns a region with at most one low neighbour to state 4 at priority 40. */
static void melee_opcodes_follow_the_interpreter(KpT *T)
{
	static uint8_t code[64];
	char error[KP_ERROR_SIZE];
	KpGame *g;
	KpEvent e;
	int64_t s, r, flagged = 0, count;
	int chat = 0;
	if (!kp_t_ready(T, "test_aiscript", FILES)) return;
	memset(code, CHECK_FILL, sizeof code);
	code[0] = OP_DEBUG, code[1] = 40, code[2] = 0, memcpy(code + 3, "hello", 6);
	g = run_opcode(T, code, sizeof code, &s, error, sizeof error);
	while (kp_event_take(g, &e)) {
		if (e.kind != KP_EVENT_CHAT) continue;
		chat++;
		kp_t_ok(T, e.a == 0 && e.b == 0 && e.text && strcmp(e.text, "hello") == 0, "debug's chat: %lld %lld %s",
			(long long)e.a, (long long)e.b, e.text ? e.text : "(none)");
	}
	kp_t_eq(T, chat, 1, "one chat line");
	kp_t_eq(T, error[0], 0, "debug runs on: %s", error);
	memset(code, CHECK_FILL, sizeof code);
	code[0] = OP_FATAL;
	run_opcode(T, code, sizeof code, &s, error, sizeof error);
	kp_t_ok(T, strstr(error, "Illegal AI script executed.") != NULL, "fatal_error: %s", error);
	code[0] = OP_WAIT_SECURE;
	g = run_opcode(T, code, sizeof code, &s, error, sizeof error);
	kp_t_ok(T, g->scripts.pc[s] == 0 && g->scripts.wait[s] == RETRY_WAIT, "wait_secure retries: PC %lld, wait %lld",
		(long long)g->scripts.pc[s], (long long)g->scripts.wait[s]);
	g->pai.flags[0] |= BUDGET_REFRESHED;
	g->scripts.wait[s] = 0;
	kp_aiscript_execute(g, s);
	kp_t_ok(T, g->scripts.pc[s] != 0, "wait_secure goes on once the budget is refreshed");
	/* build_turrets over every guarded region, then wait_turrets until none is flagged */
	code[0] = OP_BUILD_TURRETS, code[1] = OP_WAIT_TURRETS;
	g = scripted(T, code, sizeof code, code, sizeof code, 0, 0, &s);
	count = (int64_t)g->regions.count;
	for (r = 1; r < count; r++) g->air.flags[0][r] = GUARDED;
	kp_aiscript_execute(g, s);
	for (r = 1; r < count; r++) flagged += (g->air.flags[0][r] & WANTS_TURRET) != 0;
	kp_t_eq(T, flagged, count > 6 ? 6 : count - 1, "build_turrets flags six regions");
	kp_t_ok(T, g->scripts.pc[s] == 1 && g->scripts.wait[s] == RETRY_WAIT, "wait_turrets retries: PC %lld",
		(long long)g->scripts.pc[s]);
	for (r = 1; r < count; r++) g->air.flags[0][r] &= ~(int64_t)WANTS_TURRET;
	g->scripts.wait[s] = 0;
	kp_aiscript_execute(g, s);
	kp_t_ok(T, g->scripts.pc[s] != 1, "wait_turrets goes on with no region flagged");
	/* implode: every region of state 0 whose neighbours are all above state 3 but one */
	code[0] = OP_IMPLODE;
	g = scripted(T, code, sizeof code, code, sizeof code, 0, 0, &s);
	count = (int64_t)g->regions.count;
	for (r = 1; r < count; r++) g->air.state[0][r] = STATE_BASE;
	g->air.state[0][1] = STATE_IDLE;
	kp_aiscript_execute(g, s);
	kp_t_ok(T, count < 2 || (g->air.state[0][1] == STATE_WATCH && g->air.priority[0][1] == 40),
		"implode: region 1 in state %lld at priority %lld", (long long)g->air.state[0][1], (long long)g->air.priority[0][1]);
}

#define OP_BUILD_BUNKERS 0x11
#define OP_WAIT_BUNKERS 0x12
#define OP_HARASS_FACTOR 0x37
#define OP_REGION_SIZE 0x3a
#define OP_EVAL_HARASS 0x42
#define OP_WAIT_UPGRADES 0x47
#define WANTS_BUNKER 0x02
#define REQUESTS_FIRST 0x400 /* KP_AIF_REQUESTS_FIRST */
#define STATE_GATHER 8
#define REGION_SIZE_UNIT 32
#define FAR_JUMP 50 /* a jump target past the opcode under test */
#define JUMPED (FAR_JUMP + 1) /* the script's PC once it jumped: past the stop there */
#define TOWN_AT 3
#define MARINE 0
#define SCV 7
#define COMMAND_CENTER 106
#define BUNKER 125

/* The tiles of the regions connected to r over ground neighbours, each counted once. */
static int64_t connected_tiles(KpGame *g, int64_t r0)
{
	const KpRegionGraph *rg = &g->pathing.rg;
	static uint8_t seen[KP_AI_REGIONS];
	static int64_t queue[KP_AI_REGIONS];
	int64_t head = 0, tail = 0, sum = 0;
	memset(seen, 0, sizeof seen);
	queue[tail++] = r0;
	seen[r0] = 1;
	while (head < tail) {
		int64_t r = queue[head++], j;
		sum += kp_region_tiles(g, r);
		for (j = 0; j < rg->ground[r]; j++) {
			int64_t nb = rg->neighbors[rg->nb_at[r] + j];
			if (!seen[nb]) seen[nb] = 1, queue[tail++] = nb;
		}
	}
	return sum;
}

/* The first unit of player p (0 when none). */
static int64_t first_unit_of(KpGame *g, int64_t p)
{
	return g->player_units[p];
}

/* The opcodes no stock script reaches, beyond those above: wait_upgrades waits for the requests-first flag; build_bunkers
 * flags the guarded regions with an own building or worker and no own Bunker (three at most) and wait_bunkers retries
 * while one is flagged; region_size jumps when the walk from the town's region sums fewer tiles than the limit, a
 * region counted each time it is reached (MeasureTownRegionSize 0x4877f0); harass_factor repeats the roster by the
 * strength of the enemy nearest the target region (0x483830); eval_harass jumps when the attack cannot be prepared
 * or the roster is weaker than the enemies near the grouping region (0x4af5b0). */
static void more_melee_opcodes_follow_the_interpreter(KpT *T)
{
	static uint8_t code[64];
	char error[KP_ERROR_SIZE];
	KpGame *g;
	int64_t s, r, count, n, flagged, x, y, region, total, limit, sum, len, enemy;
	if (!kp_t_ready(T, "test_aiscript", FILES)) return;
	memset(code, CHECK_FILL, sizeof code);
	code[0] = OP_WAIT_UPGRADES;
	g = run_opcode(T, code, sizeof code, &s, error, sizeof error);
	kp_t_ok(T, g->scripts.pc[s] == 0 && g->scripts.wait[s] == RETRY_WAIT, "wait_upgrades retries: PC %lld",
		(long long)g->scripts.pc[s]);
	g->pai.flags[0] |= REQUESTS_FIRST;
	g->scripts.wait[s] = 0;
	kp_aiscript_execute(g, s);
	kp_t_ok(T, g->scripts.pc[s] != 0, "wait_upgrades goes on once the requests come first");
	/* build_bunkers: only the guarded region with player 0's SCV is flagged */
	code[0] = OP_BUILD_BUNKERS, code[1] = OP_WAIT_BUNKERS;
	g = scripted(T, code, sizeof code, code, sizeof code, 0, 0, &s);
	count = (int64_t)g->regions.count;
	n = first_unit_of(g, 0);
	kp_t_ok(T, n != 0, "player 0 has a unit");
	if (n == 0) return;
	x = (int16_t)g->w->spr.x[kp_unit_sprite(g, n)];
	y = (int16_t)g->w->spr.y[kp_unit_sprite(g, n)];
	(void)kp_unit_complete(g, kp_unit_create(g, SCV, x, y, 0));
	region = (kp_region_at(g, x, y) & 0xffff);
	for (r = 1; r < count; r++) g->air.flags[0][r] = GUARDED;
	kp_aiscript_execute(g, s);
	flagged = 0;
	for (r = 1; r < count; r++) flagged += (g->air.flags[0][r] & WANTS_BUNKER) != 0;
	kp_t_ok(T, flagged >= 1 && flagged <= 3 && (g->air.flags[0][region] & WANTS_BUNKER) != 0,
		"build_bunkers flags the SCV's region %lld (%lld flagged)", (long long)region, (long long)flagged);
	kp_t_ok(T, g->scripts.pc[s] == 1 && g->scripts.wait[s] == RETRY_WAIT, "wait_bunkers retries: PC %lld",
		(long long)g->scripts.pc[s]);
	for (r = 1; r < count; r++) g->air.flags[0][r] &= ~(int64_t)WANTS_BUNKER;
	(void)kp_unit_complete(g, kp_unit_create(g, BUNKER, x, y, 0));
	g->scripts.wait[s] = 0;
	g->scripts.pc[s] = 0;
	kp_aiscript_execute(g, s);
	kp_t_ok(T, (g->air.flags[0][region] & WANTS_BUNKER) == 0, "a region with an own Bunker is not flagged");
	/* region_size from a town at player 0's unit */
	code[0] = OP_REGION_SIZE, code[1] = 1, code[2] = FAR_JUMP, code[3] = 0;
	g = scripted(T, code, sizeof code, code, sizeof code, 0, 0, &s);
	n = first_unit_of(g, 0);
	x = (int16_t)g->w->spr.x[kp_unit_sprite(g, n)];
	y = (int16_t)g->w->spr.y[kp_unit_sprite(g, n)];
	region = (kp_region_at(g, x, y) & 0xffff);
	g->scripts.town[s] = kp_ai_start_town(g, 0, x, y, 0);
	kp_aiscript_execute(g, s);
	kp_t_ok(T, kp_region_tiles(g, region) < REGION_SIZE_UNIT || g->scripts.pc[s] != JUMPED,
		"region_size of 32 tiles: no jump from a region of %lld tiles", (long long)kp_region_tiles(g, region));
	/* a limit just past the connected regions' tiles counted once: the walk counts them again and reaches it */
	total = connected_tiles(g, region);
	limit = total / REGION_SIZE_UNIT + 1;
	kp_t_ok(T, limit <= 255 && g->pathing.rg.ground[region] > 0, "the town's region has neighbours and the map's tiles fit a byte's limit (%lld)",
		(long long)total);
	if (limit <= 255 && g->pathing.rg.ground[region] > 0) {
		code[1] = (uint8_t)limit;
		program(T, g, code, sizeof code, code, sizeof code); /* the game keeps its own copy */
		g->scripts.pc[s] = 0;
		kp_aiscript_execute(g, s);
		kp_t_ok(T, g->scripts.pc[s] != JUMPED, "region_size counts a region each time: no jump at %lld tiles (%lld once)",
			(long long)(limit * REGION_SIZE_UNIT), (long long)total);
	}
	/* harass_factor: the roster of two Marines, the enemy (player 1, a Command Center at the target region) */
	memset(code, CHECK_FILL, sizeof code);
	code[0] = OP_HARASS_FACTOR, code[1] = 1, code[2] = 0;
	g = scripted(T, code, sizeof code, code, sizeof code, 0, 0, &s);
	n = first_unit_of(g, 0);
	x = (int16_t)g->w->spr.x[kp_unit_sprite(g, n)];
	y = (int16_t)g->w->spr.y[kp_unit_sprite(g, n)];
	region = (kp_region_at(g, x, y) & 0xffff);
	kp_ai_region_center(g, region, &x, &y);
	enemy = kp_unit_create(g, COMMAND_CENTER, x, y, 1);
	(void)kp_unit_complete(g, enemy);
	kp_t_ok(T, enemy != 0 && (g->u.status[enemy] & 0x2) != 0, "a grounded enemy building");
	sum = 0;
	for (n = g->player_units[1]; n != 0; n = g->u.player_next[n]) {
		int64_t a = g->u.air_strength[n], gr = g->u.ground_strength[n];
		if (!kp_unit_inactive(g, n)) sum += a > gr ? a : gr;
	}
	if (sum == 0) {
		g->u.ground_strength[enemy] = 10;
		sum = 10;
	}
	g->pai.attack_target[0] = region;
	g->pai.roster[0][0] = MARINE + 1, g->pai.roster[0][1] = MARINE + 1;
	kp_aiscript_execute(g, s); /* divisor 1: three times the roster */
	for (len = 0; len < ROSTER && g->pai.roster[0][len] != 0; len++) {}
	kp_t_eq(T, len, sum >= 4 ? 6 : sum >= 3 ? 4 : 2, "harass_factor of divisor 1 (strength %lld)", (long long)sum);
	g->pai.roster[0][2] = 0;
	code[1] = (uint8_t)(sum % 256), code[2] = (uint8_t)(sum / 256 % 256); /* divisor = strength: once */
	program(T, g, code, sizeof code, code, sizeof code);
	g->scripts.pc[s] = 0;
	kp_aiscript_execute(g, s);
	for (len = 0; len < ROSTER && g->pai.roster[0][len] != 0; len++) {}
	kp_t_eq(T, len, 2, "harass_factor of the strength's divisor leaves the roster");
	code[1] = 0, code[2] = 0;
	program(T, g, code, sizeof code, code, sizeof code);
	g->scripts.pc[s] = 0;
	{
		Once once;
		once.g = g;
		once.s = s;
		kp_t_pcall(T, execute_once, &once, error, sizeof error);
	}
	kp_t_ok(T, strstr(error, "divides by 0") != NULL, "harass_factor of divisor 0: %s", error);
	/* eval_harass: no attack while countdown 2 runs (jump), and a Marine against no enemy near the grouping region */
	memset(code, CHECK_FILL, sizeof code);
	code[0] = OP_EVAL_HARASS, code[1] = FAR_JUMP, code[2] = 0;
	g = scripted(T, code, sizeof code, code, sizeof code, 0, 0, &s);
	g->pai.countdown2[0] = 1;
	kp_aiscript_execute(g, s);
	kp_t_eq(T, g->scripts.pc[s], JUMPED, "eval_harass jumps while the attack waits");
	g->pai.countdown2[0] = 0;
	n = first_unit_of(g, 0);
	region = kp_region_at(g, (int16_t)g->w->spr.x[kp_unit_sprite(g, n)], (int16_t)g->w->spr.y[kp_unit_sprite(g, n)]) & 0xffff;
	g->pai.attack_target[0] = TOWN_AT + 1;
	g->air.state[0][TOWN_AT] = STATE_GATHER;
	g->air.dest[0][TOWN_AT] = region;
	g->pai.roster[0][0] = MARINE + 1;
	kp_ai_region_center(g, region, &x, &y);
	sum = 0;
	for (n = g->lists[KP_LIST_ACTIVE].head; n != 0; n = g->u.next[n]) sum += kp_player_enemy_of(g, 0, n);
	g->scripts.pc[s] = 0;
	kp_aiscript_execute(g, s);
	kp_t_ok(T, sum != 0 || g->scripts.pc[s] != JUMPED, "eval_harass goes on with no enemy (PC %lld)",
		(long long)g->scripts.pc[s]);
	/* an enemy army at the grouping region outweighs the Marine */
	for (r = 0; r < 12; r++) {
		n = kp_unit_create(g, MARINE, x + (r % 4) * 16, y + (r / 4) * 16, 1);
		(void)kp_unit_complete(g, n);
		g->u.air_strength[n] = g->strength[0][MARINE]; /* the strengths a step gives them */
		g->u.ground_strength[n] = g->strength[1][MARINE];
	}
	g->scripts.pc[s] = 0;
	kp_aiscript_execute(g, s);
	kp_t_eq(T, g->scripts.pc[s], JUMPED, "eval_harass jumps against a stronger enemy");
}

static const KpTest TESTS[] = {
	{ "vm_equals_the_original", vm_equals_the_original, 0 },
	{ "launch_gate", launch_gate, 0 },
	{ "small_binaries_hold_no_scripts", small_binaries_hold_no_scripts, 0 },
	{ "lost_roster_entry_reads_the_next_column", lost_roster_entry_reads_the_next_column, 0 },
	{ "stock_defense_types_have_strength", stock_defense_types_have_strength, 0 },
	{ "every_opcode_reads_its_aiop_operands", every_opcode_reads_its_aiop_operands, 0 },
	{ "scripts_are_checked_before_a_game", scripts_are_checked_before_a_game, 0 },
	{ "melee_opcodes_follow_the_interpreter", melee_opcodes_follow_the_interpreter, 0 },
	{ "more_melee_opcodes_follow_the_interpreter", more_melee_opcodes_follow_the_interpreter, 0 },
};
KP_SUITE(kp_suite_aiscript, "test_aiscript", TESTS);
