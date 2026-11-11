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

static const KpTest TESTS[] = {
	{ "vm_equals_the_original", vm_equals_the_original, 0 },
	{ "launch_gate", launch_gate, 0 },
	{ "small_binaries_hold_no_scripts", small_binaries_hold_no_scripts, 0 },
	{ "lost_roster_entry_reads_the_next_column", lost_roster_entry_reads_the_next_column, 0 },
	{ "stock_defense_types_have_strength", stock_defense_types_have_strength, 0 },
};
KP_SUITE(kp_suite_aiscript, "test_aiscript", TESTS);
