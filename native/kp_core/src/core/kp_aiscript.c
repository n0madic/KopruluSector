/* The AIScript VM (kp_game.h KpScripts), ported from the atlas (P32, P28; docs/research/native-ai-opcodes.md,
 * native-ai.md): the binaries' loader and directories (kp_aiscript.h, LoadAiScriptBins 0x493800, FindAiScriptOffset
 * 0x494d20), the pool (InitAiScriptPool 0x493330, CreateAiScript 0x494d90, FreeAiScript 0x493240), the launch gate
 * LaunchAiScript 0x493970, the scheduler RunAiScripts 0x4938e0 and the interpreter ExecuteAiScript 0x493a60 with the
 * PlayerAi setters of its opcodes.
 *
 * An opcode's operands are read as the interpreter reads them (0x4932f0 a byte, 0x493310 a little-endian word) from
 * the script's binary; a handler continues with the next opcode unless it waits, retries or stops. A retry puts the
 * PC back at the opcode and waits 30 scheduler visits. The opcodes only the melee scripts use fail with
 * their address (outside V1). */
#include "kp_game_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NONE (-1)
#define KILLABLE 0x2
#define KILLED 0x4
#define RICH_EXPANSION_MINERALS 5000 /* a non-campaign player this rich does not expand */
#define RICH_EXPANSION_GAS 3000
#define AREA_SHIFT 3 /* the flags' bits from 3: the resource area of start_town and start_areatown */
#define RETRY_WAIT 30 /* the scheduler visits before an opcode that waits for its condition runs again */
#define LAST_OPCODE 0x70
#define ALLIED 1
#define ALLIED_VICTORY 2
#define TECH_RECALL 21
#define TECH_DISRUPTION_WEB 25
#define RESOURCE_MIN 500
#define RESOURCE_GIFT 2000
#define DEFENSE_COUNT 10   /* the most units a defense opcode adds */
#define DEFENSE_LISTS 4    /* the build lists gg, ag, ga, aa; the use lists follow them */
#define DIRECTORY_RECORD 8 /* the id and offset DWORDs a directory record starts with */
#define FOURCC(a, b, c, d) ((uint32_t)(a) | (uint32_t)(b) << 8 | (uint32_t)(c) << 16 | (uint32_t)(d) << 24)
#define RESCUE_SCRIPT FOURCC('R', 's', 'c', 'u')

/* PlayerAi +0x218 */
#define QUICK_ATTACK_GROUPING 5 /* quick_attack's grouping ends after this many seconds */
#define AI_STATE_START 1 /* 0x67f7ac, which InitPlayerAiData 0x4830d0 sets to 1 */
#define CREEP_ON 4 /* creep's operand that sets KP_AIF_CREEP (0x4828d0) */
#define PLAYER_NEED_PRIORITY 80
/* the priority of a region an attack group leaves (a non-campaign AI's kept region's) */
#define RESET_PRIORITY 10
#define RESET_PRIORITY_KEPT 40
/* AI region states */
#define NO_TECH 44

enum {
	OP_GOTO = 0x00, OP_NOTOWNS_JUMP = 0x01, OP_WAIT = 0x02, OP_START_TOWN = 0x03, OP_START_AREATOWN = 0x04,
	OP_EXPAND = 0x05, OP_BUILD = 0x06, OP_UPGRADE = 0x07, OP_TECH = 0x08, OP_WAIT_BUILD = 0x09,
	OP_WAIT_BUILDSTART = 0x0a, OP_ATTACK_CLEAR = 0x0b, OP_ATTACK_ADD = 0x0c, OP_ATTACK_PREPARE = 0x0d,
	OP_ATTACK_DO = 0x0e, OP_WAIT_SECURE = 0x0f, OP_CAPT_EXPAND = 0x10, OP_BUILD_BUNKERS = 0x11,
	OP_WAIT_BUNKERS = 0x12, OP_DEFENSEBUILD_GG = 0x13, OP_DEFENSEBUILD_AA = 0x16, OP_DEFENSEUSE_GG = 0x17,
	OP_DEFENSEUSE_AA = 0x1a, OP_DEFENSECLEAR_GG = 0x1b, OP_DEFENSECLEAR_AA = 0x1e, OP_SEND_SUICIDE = 0x1f,
	OP_PLAYER_ENEMY = 0x20, OP_PLAYER_ALLY = 0x21, OP_DEFAULT_MIN = 0x22, OP_DEFAULTBUILD_OFF = 0x23, OP_STOP = 0x24,
	OP_SWITCH_RESCUE = 0x25, OP_MOVE_DT = 0x26, OP_DEBUG = 0x27, OP_FATAL_ERROR = 0x28, OP_ENTER_BUNKER = 0x29,
	OP_VALUE_AREA = 0x2a, OP_TRANSPORTS_OFF = 0x2b, OP_CHECK_TRANSPORTS = 0x2c, OP_NUKE_RATE = 0x2d,
	OP_MAX_FORCE = 0x2e, OP_CLEAR_COMBATDATA = 0x2f, OP_RANDOM_JUMP = 0x30, OP_TIME_JUMP = 0x31,
	OP_FARMS_NOTIMING = 0x32, OP_FARMS_TIMING = 0x33, OP_BUILD_TURRETS = 0x34, OP_WAIT_TURRETS = 0x35,
	OP_DEFAULT_BUILD = 0x36, OP_HARASS_FACTOR = 0x37, OP_START_CAMPAIGN = 0x38, OP_RACE_JUMP = 0x39,
	OP_REGION_SIZE = 0x3a, OP_GET_OLDPEONS = 0x3b, OP_GROUNDMAP_JUMP = 0x3c, OP_PLACE_GUARD = 0x3d,
	OP_WAIT_FORCE = 0x3e, OP_GUARD_RESOURCES = 0x3f, OP_CALL = 0x40, OP_RETURN = 0x41, OP_EVAL_HARASS = 0x42,
	OP_CREEP = 0x43, OP_PANIC = 0x44, OP_PLAYER_NEED = 0x45, OP_DO_MORPH = 0x46, OP_WAIT_UPGRADES = 0x47,
	OP_MULTIRUN = 0x48, OP_RUSH = 0x49, OP_SCOUT_WITH = 0x4a, OP_DEFINE_MAX = 0x4b, OP_TRAIN = 0x4c,
	OP_TARGET_EXPANSION = 0x4d, OP_WAIT_TRAIN = 0x4e, OP_SET_ATTACKS = 0x4f, OP_SET_GENCMD = 0x50,
	OP_MAKE_PATROL = 0x51, OP_GIVE_MONEY = 0x52, OP_PREP_DOWN = 0x53, OP_RESOURCES_JUMP = 0x54,
	OP_ENTER_TRANSPORT = 0x55, OP_EXIT_TRANSPORT = 0x56, OP_SHAREDVISION_ON = 0x57, OP_SHAREDVISION_OFF = 0x58,
	OP_NUKE_LOCATION = 0x59, OP_HARASS_LOCATION = 0x5a, OP_IMPLODE = 0x5b, OP_GUARD_ALL = 0x5c,
	OP_ENEMYOWNS_JUMP = 0x5d, OP_ENEMYRESOURCES_JUMP = 0x5e, OP_IF_DIF = 0x5f, OP_EASY_ATTACK = 0x60,
	OP_KILL_THREAD = 0x61, OP_KILLABLE = 0x62, OP_WAIT_FINISHATTACK = 0x63, OP_QUICK_ATTACK = 0x64,
	OP_JUNKYARD_DOG = 0x65, OP_FAKE_NUKE = 0x66, OP_DISRUPTION_WEB = 0x67, OP_RECALL_LOCATION = 0x68,
	OP_SET_RANDOMSEED = 0x69, OP_IF_OWNED = 0x6a, OP_CREATE_NUKE = 0x6b, OP_CREATE_UNIT = 0x6c, OP_NUKE_POS = 0x6d,
	OP_HELP_IFTROUBLE = 0x6e, OP_ALLIES_WATCH = 0x6f, OP_TRY_TOWNPOINT = 0x70
};

/* The scripts any player may start (0x508f70): the others need a computer, Rscu also a neutral player. */
static const uint32_t ANY_PLAYER[] = {
	FOURCC('E', 'n', 'B', 'k'), FOURCC('E', 'n', 'T', 'r'), FOURCC('E', 'x', 'T', 'r'), FOURCC('D', 'W', 'H', 'e'),
	FOURCC('P', 'B', '3', 'F'),
	FOURCC('+', 'V', 'i', '0'), FOURCC('+', 'V', 'i', '1'), FOURCC('+', 'V', 'i', '2'), FOURCC('+', 'V', 'i', '3'),
	FOURCC('+', 'V', 'i', '4'), FOURCC('+', 'V', 'i', '5'), FOURCC('+', 'V', 'i', '6'), FOURCC('+', 'V', 'i', '7'),
	FOURCC('-', 'V', 'i', '0'), FOURCC('-', 'V', 'i', '1'), FOURCC('-', 'V', 'i', '2'), FOURCC('-', 'V', 'i', '3'),
	FOURCC('-', 'V', 'i', '4'), FOURCC('-', 'V', 'i', '5'), FOURCC('-', 'V', 'i', '6'), FOURCC('-', 'V', 'i', '7')
};

/* ---- The binaries ---- */

static int fail(KpError *err, const char *msg)
{
	snprintf(err->msg, sizeof err->msg, "aiscript: %s", msg);
	return -1;
}

void kp_ai_bins_free(KpAiBins *b)
{
	free(b->blob);
	memset(b, 0, sizeof *b);
}

int kp_ai_bins_load(KpAiBins *b, const uint8_t *data, size_t size, const char *name, KpError *err)
{
	static const char *const SECTIONS[KP_AI_BINS] = { "AISC", "BWSC" };
	KpKpf c;
	KpKpfExpect want;
	int k;
	memset(b, 0, sizeof *b);
	if (!data) return fail(err, "no aiscript.kpd");
	b->blob = (uint8_t *)malloc(size ? size : 1);
	if (!b->blob) return fail(err, "out of memory");
	memcpy(b->blob, data, size);
	b->size = size;
	if (kp_kpf_parse(&c, b->blob, size, name, 0, err) != 0) {
		kp_ai_bins_free(b);
		return -1;
	}
	memset(&want, 0, sizeof want);
	want.kind = KP_AISCRIPT_KIND;
	want.schema_version = KP_AISCRIPT_SCHEMA;
	if (kp_kpf_check(&c, &want, err) != 0) goto failed;
	for (k = 0; k < KP_AI_BINS; k++) {
		const KpKpfSection *s = kp_kpf_require(&c, SECTIONS[k], err);
		if (!s) goto failed;
		b->bin[k] = s->data;
		b->bin_size[k] = s->size;
	}
	kp_kpf_free(&c);
	return 0;
failed:
	kp_kpf_free(&c);
	kp_ai_bins_free(b);
	return -1;
}

/* A directory's offset of id (its records of `stride` bytes {id, offset, ...} from the DWORD at offset 0, up to id 0):
 * the first matching record's, 0 when none matches. */
static uint32_t directory_find(const uint8_t *bin, uint32_t size, uint32_t id, uint32_t stride)
{
	uint32_t at;
	if (size < DIRECTORY_RECORD) return 0; /* no room for a record {id, offset} */
	for (at = kp_le_u32(bin); at <= size - DIRECTORY_RECORD; at += stride) {
		uint32_t rid = kp_le_u32(bin + at);
		if (rid == 0) return 0;
		if (rid == id) return kp_le_u32(bin + at + 4);
		if (size - DIRECTORY_RECORD - at < stride) break;
	}
	return 0;
}

uint32_t kp_ai_bins_find(const KpAiBins *b, uint32_t id, int *bin)
{
	uint32_t off = directory_find(b->bin[0], b->bin_size[0], id, 16);
	*bin = 0;
	if (off != 0) return off;
	off = directory_find(b->bin[1], b->bin_size[1], id, 8);
	if (off != 0) *bin = 1;
	return off;
}

int kp_aiscript_program(KpGame *g, const uint8_t *ai, size_t ai_size, const uint8_t *bw, size_t bw_size)
{
	KpAiBins *b = &g->ai_program;
	kp_ai_bins_free(b);
	g->ai_bins = g->ai_game_bins;
	if (!ai) return 0;
	b->size = ai_size + bw_size;
	b->blob = (uint8_t *)malloc(b->size ? b->size : 1);
	if (!b->blob) return -1;
	if (ai_size) memcpy(b->blob, ai, ai_size);
	if (bw_size) memcpy(b->blob + ai_size, bw, bw_size);
	b->bin[0] = b->blob;
	b->bin_size[0] = (uint32_t)ai_size;
	b->bin[1] = b->blob + ai_size;
	b->bin_size[1] = (uint32_t)bw_size;
	g->ai_bins = b;
	return 0;
}

/* ---- The pool ---- */

/* InitAiScriptPool 0x493330 (LoadAiScriptBins 0x493800): the free list 0, 1, ..., 99 and an empty active list. */
void kp_aiscript_init(KpGame *g)
{
	KpScripts *sc = &g->scripts;
	int64_t k;
	memset(sc, 0, sizeof *sc);
	for (k = 0; k < KP_AI_SCRIPTS; k++) {
		sc->next[k] = k + 1 < KP_AI_SCRIPTS ? k + 1 : NONE;
		sc->prev[k] = k > 0 ? k - 1 : NONE;
		sc->town[k] = NONE;
	}
	sc->head = NONE;
	sc->free = 0;
	sc->ret = 0;
}

/* FreeAiScript 0x493240 (and the scheduler's inline copy): s leaves the active list for the head of the free list,
 * its payload kept. */
void kp_aiscript_free(KpGame *g, int64_t s)
{
	KpScripts *sc = &g->scripts;
	kp_pool_release(sc->next, sc->prev, &sc->head, &sc->free, s);
}

/* CreateAiScript 0x494d90: the first free slot at the head of the active list, at pc of binary bin, with the
 * rectangle {left, top, right, bottom} (its centre truncated toward zero) or none; NONE when the pool is empty. The
 * flags keep their old bits 1 and 2 (flags &= 7). */
int64_t kp_aiscript_create(KpGame *g, int64_t player, int64_t pc, const int64_t *rect, int bin)
{
	KpScripts *sc = &g->scripts;
	int64_t s = kp_pool_take(sc->next, sc->prev, &sc->free, &sc->head);
	if (s == NONE) return NONE;
	sc->pc[s] = pc;
	sc->wait[s] = 0;
	sc->flags[s] = kp_band(kp_bor(kp_band(sc->flags[s], kp_bnot(KP_AI_SCRIPT_BW)), bin ? KP_AI_SCRIPT_BW : 0), 7);
	sc->player[s] = player;
	if (rect) {
		sc->left[s] = kp_s32v(rect[0]);
		sc->top[s] = kp_s32v(rect[1]);
		sc->right[s] = kp_s32v(rect[2]);
		sc->bottom[s] = kp_s32v(rect[3]);
		sc->x[s] = kp_s32v(sc->left[s] + kp_s32v(sc->right[s] - sc->left[s]) / 2);
		sc->y[s] = kp_s32v(sc->top[s] + kp_s32v(sc->bottom[s] - sc->top[s]) / 2);
	} else {
		sc->left[s] = sc->top[s] = sc->right[s] = sc->bottom[s] = 0;
		sc->x[s] = sc->y[s] = 0;
	}
	sc->town[s] = NONE;
	return s;
}

/* SpawnAreaTownScript 0x494e80: a script of player p at pc in binary bw starts at resource area a (its flags carry
 * the area); the area's region is marked for defense, and a non-campaign player defends the route cuts too. */
void kp_aiscript_spawn_area_town(KpGame *g, int64_t p, int bw, int64_t area, int64_t pc)
{
	int64_t x = kp_s16v(g->areas.x[area]), y = kp_s16v(g->areas.y[area]); /* 0x4970c0, 0x4970d0 */
	int64_t rect[4] = { x - 1, y - 1, x + 1, y + 1 };
	int64_t s = kp_aiscript_create(g, p, pc, rect, bw);
	if (s == NONE) return;
	g->scripts.flags[s] = kp_bor(kp_shlv(area, AREA_SHIFT), kp_band(g->scripts.flags[s], 7));
	kp_ai_mark_defense_region(g, p, kp_region_at(g, x, y), 0); /* 0x4b0030 */
	if (!kp_ai_campaign(g, p)) {
		/* TODO: a non-campaign player's route cuts 0x4b0be0 (outside the campaigns) */
		kp_game_fail(g, "ai: a non-campaign expansion's route cuts (0x4b0be0)");
	}
}

/* HelpAlliesInTrouble 0x4acca0: every mutual ally's idle record of a region p's AI uses becomes a defended region
 * (state 4, priority 40; the state reset of SetRegionState 0x4ab900 written inline). */
static void help_allies(KpGame *g, int64_t p)
{
	int64_t q, r, count = g->regions.count;
	for (q = 0; q < KP_AI_PLAYERS; q++) {
		if (g->players.alliance[p][q] == 0 || g->players.alliance[q][p] == 0 || count <= 1) continue;
		for (r = 1; r < count; r++) {
			if (g->air.state[p][r] == KP_RSTATE_IDLE || g->air.state[q][r] != KP_RSTATE_IDLE) continue;
			kp_ai_set_region_state(g, q, r, KP_RSTATE_WATCH);
		}
	}
}

/* CountPlayerTowns 0x4872d0. */
static int64_t player_towns(KpGame *g, int64_t p)
{
	int64_t k, count = 0;
	for (k = g->towns.heads[p]; k != NONE; k = g->towns.next[k]) count++;
	return count;
}

void kp_aiscript_kill_town(KpGame *g, int64_t k)
{
	KpScripts *sc = &g->scripts;
	int64_t s = sc->head;
	while (s != NONE) {
		int64_t next = sc->next[s];
		if (sc->town[s] == k) kp_aiscript_free(g, s);
		if (sc->head == NONE) break;
		s = next;
	}
}

/* LaunchAiScript 0x493970: script id for player p with the rectangle or none, when a script any player may start or
 * p's controller is a computer (a neutral player's for Rscu). */
void kp_aiscript_launch(KpGame *g, uint32_t id, int64_t p, const int64_t *rect)
{
	size_t k;
	int allowed = 0, bin;
	uint32_t pc;
	for (k = 0; k < sizeof ANY_PLAYER / sizeof ANY_PLAYER[0]; k++) {
		if (ANY_PLAYER[k] == id) allowed = 1;
	}
	if (!allowed) {
		int64_t c = kp_controller(g, p);
		allowed = c == KP_CTRL_COMPUTER || (c == KP_CTRL_NEUTRAL && id == RESCUE_SCRIPT);
	}
	if (!allowed) return;
	pc = kp_ai_bins_find(g->ai_bins, id, &bin);
	if (pc != 0) kp_aiscript_create(g, p, pc, rect, bin);
}

/* ---- The interpreter ---- */

static uint8_t read_byte(KpGame *g, int64_t s)
{
	KpScripts *sc = &g->scripts;
	int bin = (int)kp_band(sc->flags[s], KP_AI_SCRIPT_BW);
	int64_t pc = sc->pc[s];
	if (pc < 0 || pc >= (int64_t)g->ai_bins->bin_size[bin]) {
		kp_game_fail(g, "ai: script %lld reads PC %lld outside its binary", (long long)s, (long long)pc);
	}
	sc->pc[s] = pc + 1;
	return g->ai_bins->bin[bin][pc];
}

static int64_t read_word(KpGame *g, int64_t s)
{
	int64_t lo = read_byte(g, s);
	return lo | (int64_t)read_byte(g, s) << 8;
}

/* The retry of the opcode at pc: the PC back at it, the script waiting RETRY_WAIT visits. */
static void retry(KpGame *g, int64_t s, int64_t pc)
{
	g->scripts.pc[s] = pc;
	g->scripts.wait[s] = RETRY_WAIT;
}

static void rect_of(const KpScripts *sc, int64_t s, int64_t *rect)
{
	rect[0] = sc->left[s];
	rect[1] = sc->top[s];
	rect[2] = sc->right[s];
	rect[3] = sc->bottom[s];
}

static int64_t player_ai(KpGame *g, int64_t p)
{
	if (kp_u32v(p) >= KP_AI_PLAYERS) kp_game_fail(g, "ai: the PlayerAi record of player %lld", (long long)p);
	return p;
}

static void set_flag(KpGame *g, int64_t p, int64_t bit, int on)
{
	int64_t *f = &g->pai.flags[player_ai(g, p)];
	*f = on ? kp_bor(*f, bit) : kp_band(*f, kp_bnot(bit));
}

/* GetUnitCountAll 0x4143c0 / GetUnitCountCompleted 0x4143e0. */
static int64_t unit_count(KpGame *g, int64_t t, int64_t p, int completed)
{
	if (t < 0 || t >= KP_UNIT_TYPES || p < 0 || p >= KP_PLAYERS) {
		kp_game_fail(g, "ai: the count of type %lld of player %lld", (long long)t, (long long)p);
	}
	return (completed ? g->count_completed : g->count_all)[t * KP_PLAYERS + p];
}

/* CountUnitsMergedTanks 0x4939e0: both modes of the Siege Tank count as one type. */
static int64_t merged_count(KpGame *g, int64_t t, int64_t p, int completed)
{
	int64_t n = unit_count(g, t, p, completed);
	if (t == KP_TYPE_SIEGE_TANK_SIEGED) n = kp_u32v(n + unit_count(g, KP_TYPE_SIEGE_TANK, p, completed));
	else if (t == KP_TYPE_SIEGE_TANK) n = kp_u32v(n + unit_count(g, KP_TYPE_SIEGE_TANK_SIEGED, p, completed));
	return n;
}

/* A list of WORDs (unit + 1) of capacity cap: count more units after its first zero (none when it is full). The
 * attack roster (AddAttackUnits 0x4839f0) and the defense lists (0x482550 and the others) share the form; the scan
 * past a full list reads the WORD after it, which changes nothing. */
static void append_units(int64_t *list, int64_t cap, int64_t count, int64_t unit)
{
	int64_t k = 0;
	while (list[k] != 0) {
		if (k >= cap - 1) return;
		k++;
	}
	while (count-- > 0 && k < cap) list[k++] = unit + 1;
}

static void attack_add(KpGame *g, int64_t p, int64_t count, int64_t unit)
{
	if (kp_u32v(p) >= KP_AI_PLAYERS || kp_u32v(count) >= KP_AI_ROSTER_UNITS || kp_u32v(unit) >= KP_UNIT_TYPES) return;
	append_units(g->pai.roster[p], KP_AI_ROSTER_UNITS, count, unit);
}

/* The defense list of an opcode (0x482670, 0x482610, 0x4825b0, 0x482550 build; 0x482e40, 0x482de0, 0x482d80,
 * 0x482d20 use): count <= 10 more units. */
static void defense_add(KpGame *g, int64_t p, int list, int64_t count, int64_t unit)
{
	if (count > DEFENSE_COUNT || kp_u32v(unit) >= KP_UNIT_TYPES) return;
	append_units(g->pai.defense[player_ai(g, p)][list], KP_AI_DEFENSE, count, unit);
}

/* ResetAttackGroupRegions 0x4ad230: the player's regions of an attack group (state 8) back to state 0 at priority
 * 10 (40 for a non-campaign AI's region with flag 0x40), their flag 0x20 cleared. */
static void reset_attack_regions(KpGame *g, int64_t p)
{
	KpAiRegions *a = &g->air;
	int64_t r;
	for (r = 1; r < (int64_t)g->regions.count; r++) {
		if (a->state[p][r] != KP_RSTATE_GATHER) continue;
		a->flags[p][r] = kp_band(a->flags[p][r], kp_bnot(KP_RFLAG_UNDER_ATTACK));
		a->state[p][r] = 0;
		a->priority[p][r] = RESET_PRIORITY;
		if (kp_band(a->flags[p][r], KP_RFLAG_NO_DECAY) != 0 && !kp_ai_campaign(g, p)) a->priority[p][r] = RESET_PRIORITY_KEPT;
	}
}

/* ClearAttackRoster 0x483770. */
void kp_ai_attack_clear(KpGame *g, int64_t p, int timestamp)
{
	KpPlayerAi *pai = &g->pai;
	player_ai(g, p);
	memset(pai->roster[p], 0, sizeof pai->roster[p]);
	pai->attack_target[p] = 0;
	if (timestamp) pai->grouping_time[p] = 0;
	reset_attack_regions(g, p);
}

/* SetAllianceInRect 0x48de00: player p's alliance toward the owner of every unit in the rectangle (its low WORDs). */
static void alliance_in_rect(KpGame *g, int64_t p, const int64_t *rect, int64_t value)
{
	int count, k;
	int64_t *found = kp_query_rect(g, kp_s16v(rect[0]), kp_s16v(rect[1]), kp_s16v(rect[2]), kp_s16v(rect[3]), &count);
	for (k = 0; k < count; k++) {
		int64_t q = g->u.owner[found[k]];
		if (q != p) g->players.alliance[p][q] = value;
	}
	kp_query_release(g, found);
}

/* MakePlayerRescuable 0x48dc50: the player becomes rescuable, allied both ways with every player, and its live
 * units leave the AI (RemoveUnitAi 0x48dbc0's steps) for RescuePassive. */
static void switch_rescue(KpGame *g, int64_t p)
{
	KpUnits *u = &g->u;
	int64_t q, n;
	g->players.controller[p] = KP_CTRL_RESCUE;
	for (q = 0; q < KP_PLAYERS; q++) g->players.alliance[p][q] = g->players.alliance[q][p] = ALLIED;
	for (n = g->player_units[p]; n != 0; n = u->player_next[n]) {
		if (kp_unit_inactive(g, n)) continue;
		kp_remove_unit_ai(g, n, 0);
		kp_issue_order(g, n, KP_ORD_RESCUE_PASSIVE);
		u->ai[n] = 0;
	}
}

/* ClearCombatDataInArea 0x4ad2a0: the player's regions whose centre is in the rectangle (inclusive) go to state 0. */
static void clear_combat_data(KpGame *g, int64_t p, const int64_t *rect)
{
	int64_t r;
	for (r = 1; r < (int64_t)g->regions.count; r++) {
		int64_t x, y;
		kp_ai_region_center(g, r, &x, &y);
		if (rect[0] <= x && x <= rect[2] && rect[1] <= y && y <= rect[3]) kp_ai_set_region_state(g, p, r, KP_RSTATE_IDLE);
	}
}

/* ValueArea 0x4ad9e0: the player's region at (x, y) below state 5 goes to state 5 and needs at least 200 of each. */
static void value_area(KpGame *g, int64_t p, int64_t x, int64_t y)
{
	KpAiRegions *a = &g->air;
	int64_t r = kp_region_at(g, x, y);
	if (a->state[p][r] >= KP_RSTATE_BASE) return;
	kp_ai_set_region_state(g, p, r, KP_RSTATE_BASE);
	if (a->need_ground[p][r] < 200) a->need_ground[p][r] = 200;
	if (a->need_air[p][r] < 200) a->need_air[p][r] = 200;
}

/* The enemyowns_jump and enemyresources_jump scans: the first unit of the active list hostile to p (of type t when
 * t >= 0), 0 when none. */
static int64_t first_enemy(KpGame *g, int64_t p, int64_t t)
{
	int64_t n;
	for (n = g->lists[KP_LIST_ACTIVE].head; n != 0; n = g->u.next[n]) {
		if ((t < 0 || g->u.type[n] == t) && kp_player_enemy_of(g, p, n)) return n;
	}
	return 0;
}

/* FindScriptSpellcaster 0x4932a0: the first of player p's units of type t with at least `minimum` whole energy that
 * is idle under the AI (Guard, ComputerAI or AIGuard), or 0. */
int64_t kp_ai_script_caster(KpGame *g, int64_t p, int64_t t, int64_t minimum)
{
	const KpUnits *u = &g->u;
	int64_t n;
	for (n = g->player_units[p]; n != 0; n = u->player_next[n]) {
		int64_t o = u->order[n];
		if (u->type[n] == t && kp_band(kp_shrv(u->energy[n], 8), 0xff) >= minimum
			&& (o == KP_ORD_COMPUTER_AI || o == KP_ORD_AI_GUARD || o == KP_ORD_GUARD)) {
			return n;
		}
	}
	return 0;
}

/* OwnBunkerCallback 0x432c30: a Bunker of unit `arg`'s owner. */
static int own_bunker(KpGame *g, int64_t c, int64_t arg)
{
	return g->u.type[c] == KP_TYPE_BUNKER && g->u.owner[c] == g->u.owner[arg];
}

/* EnterNearestBunkers 0x4af490: each of player p's units in the rectangle (its low WORDs) enters the nearest Bunker of
 * its owner (FindNearestOwnBunker 0x432f10, map-wide) when it fits there now. */
static void enter_bunkers(KpGame *g, int64_t p, const int64_t *rect)
{
	int count, k;
	int64_t *found = kp_query_rect(g, kp_s16v(rect[0]), kp_s16v(rect[1]), kp_s16v(rect[2]), kp_s16v(rect[3]), &count);
	for (k = 0; k < count; k++) {
		int64_t n = found[k], b;
		if (g->u.owner[n] != p || unit_count(g, KP_TYPE_BUNKER, p, 1) == 0) continue;
		b = kp_nearest_unit_map(g, n, own_bunker, n);
		if (b != 0 && kp_can_load(g, n, b)) kp_issue_order_target(g, n, KP_ORD_ENTER_TRANSPORT, b);
	}
	kp_query_release(g, found);
}

/* disruption_web and recall_location: the first ready caster casts at the script's centre (its rectangle's). */
static void cast_at_location(KpGame *g, int64_t s, int64_t t, int64_t tech, int64_t order)
{
	const KpScripts *sc = &g->scripts;
	int64_t n = kp_ai_script_caster(g, sc->player[s], t, KP_DAT(g, techdata, energy_required, tech));
	if (n != 0) kp_issue_order_at(g, n, order, sc->x[s], sc->y[s]);
}

static int resources_cover(KpGame *g, int64_t p, int64_t minerals, int64_t gas)
{
	return kp_s32v(g->players.minerals[p]) >= minerals && kp_s32v(g->players.gas[p]) >= gas;
}

/* GiveAiMoney 0x482ff0. */
static void give_money(KpGame *g, int64_t p)
{
	if (kp_s32v(g->players.minerals[p]) < RESOURCE_MIN) g->players.minerals[p] = kp_u32v(g->players.minerals[p] + RESOURCE_GIFT);
	if (kp_s32v(g->players.gas[p]) < RESOURCE_MIN) g->players.gas[p] = kp_u32v(g->players.gas[p] + RESOURCE_GIFT);
}

static void trace(KpGame *g, int64_t s, int64_t pc, int64_t op)
{
	const KpScripts *sc = &g->scripts;
	if (!g->events.trace) return;
	kp_event(g, KP_EVENT_AI, sc->player[s], s << 1 | kp_band(sc->flags[s], KP_AI_SCRIPT_BW), pc << 8 | op, NULL);
}

/* MarkAiScriptStarted 0x482490 and RefreshComputerRegions 0x494f80: a town started, the player's AI is running and a
 * computer's regions all take a step at once. */
static void town_started(KpGame *g, int64_t p)
{
	int64_t r;
	set_flag(g, p, KP_AIF_SCRIPT_STARTED, 1);
	g->pai.ai_state[p] = AI_STATE_START;
	if (kp_controller(g, p) != KP_CTRL_COMPUTER) return;
	for (r = 0; r < (int64_t)g->regions.count; r++) kp_ai_step_region(g, p, r);
}

/* The script's town for an opcode that needs one; the original would use a null town. */
static int64_t script_town(KpGame *g, int64_t s, int64_t op)
{
	if (g->scripts.town[s] == NONE) kp_game_fail(g, "ai: opcode 0x%02llx of a script without a town", (long long)op);
	return g->scripts.town[s];
}

/* An opcode only the melee scripts use (StartCustomLevelAIScripts 0x4486a0's), whose helper is not ported.
 * TODO: the melee AI's opcodes (outside V1). */
static void unported(KpGame *g, int64_t op, unsigned helper, const char *what)
{
	kp_game_fail(g, "ai: opcode 0x%02llx, %s (0x%x)", (long long)op, what, helper);
}

/* ExecuteAiScript 0x493a60 */
void kp_aiscript_execute(KpGame *g, int64_t s)
{
	KpScripts *sc = &g->scripts;
	KpPlayerAi *pai = &g->pai;
	for (;;) {
		int64_t p = sc->player[s], pc = sc->pc[s], op = read_byte(g, s), a, b, c, rect[4];
		trace(g, s, pc, op);
		if (op > LAST_OPCODE) { /* an ordinal past the table: freed as stop does */
			kp_aiscript_free(g, s);
			return;
		}
		switch (op) {
		case OP_GOTO:
			sc->pc[s] = read_word(g, s);
			break;
		case OP_WAIT:
			sc->wait[s] = read_word(g, s);
			return;
		case OP_STOP:
			kp_aiscript_free(g, s);
			return;
		case OP_NOTOWNS_JUMP: case OP_IF_OWNED:
			a = read_word(g, s);
			b = read_word(g, s);
			if ((unit_count(g, a, p, 0) == 0) == (op == OP_NOTOWNS_JUMP)) sc->pc[s] = b;
			break;
		case OP_RANDOM_JUMP:
			a = read_byte(g, s);
			b = read_word(g, s);
			if (kp_band(kp_rng_game_raw(g->rng, 0x34), 0xff) <= a) sc->pc[s] = b;
			break;
		case OP_TIME_JUMP:
			a = read_byte(g, s);
			b = read_word(g, s);
			if (kp_u32v(kp_scalar(g, KP_ELAPSED)) >= a * 60) sc->pc[s] = b;
			break;
		case OP_RESOURCES_JUMP:
			a = read_word(g, s);
			b = read_word(g, s);
			c = read_word(g, s);
			if (resources_cover(g, p, a, b)) sc->pc[s] = c;
			break;
		case OP_ENEMYOWNS_JUMP:
			a = read_word(g, s);
			b = read_word(g, s);
			if (first_enemy(g, p, a) != 0) sc->pc[s] = b;
			break;
		case OP_ENEMYRESOURCES_JUMP: {
			int64_t n;
			a = read_word(g, s);
			b = read_word(g, s);
			c = read_word(g, s);
			n = first_enemy(g, p, -1);
			if (n != 0 && resources_cover(g, g->u.owner[n], a, b)) sc->pc[s] = c;
			break;
		}
		case OP_IF_DIF:
			a = read_byte(g, s);
			b = read_byte(g, s);
			c = read_word(g, s);
			if (a == 0 ? pai->ai_state[player_ai(g, p)] < b : pai->ai_state[player_ai(g, p)] > b) sc->pc[s] = c;
			break;
		case OP_CALL:
			a = read_word(g, s);
			sc->ret = sc->pc[s];
			sc->pc[s] = a;
			break;
		case OP_RETURN:
			sc->pc[s] = sc->ret;
			sc->ret = 0;
			break;
		case OP_MULTIRUN: {
			int64_t child;
			a = read_word(g, s);
			rect_of(sc, s, rect);
			child = kp_aiscript_create(g, p, a, rect, (int)kp_band(sc->flags[s], KP_AI_SCRIPT_BW));
			if (child != NONE) sc->town[child] = sc->town[s];
			break;
		}
		case OP_KILLABLE:
			sc->flags[s] = kp_bor(sc->flags[s], KILLABLE);
			break;
		case OP_KILL_THREAD: {
			int64_t k;
			for (k = sc->head; k != NONE; k = sc->next[k]) {
				if (kp_band(sc->flags[k], KILLABLE) != 0) sc->flags[k] = kp_bor(sc->flags[k], KILLED);
			}
			break;
		}
		case OP_SET_RANDOMSEED: /* SetInitialSeed 0x4781f0: the saved seed, not the generator */
			a = read_word(g, s);
			g->rng->initial_seed = (uint32_t)(a | read_word(g, s) << 16);
			break;
		case OP_SCOUT_WITH:
			read_word(g, s);
			break;
		case OP_HARASS_LOCATION:
			break;
		case OP_START_CAMPAIGN: /* SetCampaignAi 0x482ee0 */
			set_flag(g, p, KP_AIF_CAMPAIGN, 1);
			break;
		case OP_DEFAULT_MIN: /* SetDefaultMin 0x4837e0 */
			pai->default_min[player_ai(g, p)] = read_byte(g, s);
			break;
		case OP_DEFAULTBUILD_OFF: /* SetDefaultBuildOff 0x483990 */
			set_flag(g, p, KP_AIF_DEFAULT_BUILD_OFF, 1);
			break;
		case OP_DEFAULT_BUILD: /* SetDefaultBuildOn 0x4837b0 */
			set_flag(g, p, KP_AIF_DEFAULT_BUILD_OFF, 0);
			break;
		case OP_TRANSPORTS_OFF: case OP_CHECK_TRANSPORTS: /* SetTransports 0x4835c0 */
			set_flag(g, p, KP_AIF_TRANSPORTS_OFF, op == OP_TRANSPORTS_OFF);
			break;
		case OP_FARMS_NOTIMING: case OP_FARMS_TIMING: /* SetFarmsTiming 0x482fa0 */
			set_flag(g, p, KP_AIF_FARMS_NO_TIMING, op == OP_FARMS_NOTIMING);
			break;
		case OP_CREEP: /* SetAiFlag80 0x4828d0 */
			set_flag(g, p, KP_AIF_CREEP, read_byte(g, s) == CREEP_ON);
			break;
		case OP_TARGET_EXPANSION: /* SetTargetExpansion 0x483510 */
			set_flag(g, p, KP_AIF_TARGET_EXPANSION, 1);
			break;
		case OP_NUKE_RATE: /* SetNukeRate 0x483370 */
			pai->nuke_rate[player_ai(g, p)] = read_byte(g, s);
			break;
		case OP_MAX_FORCE: /* SetMaxForce 0x483290 */
			pai->max_force[player_ai(g, p)] = read_word(g, s);
			break;
		case OP_SET_ATTACKS: /* SetAttackCount 0x4834c0 */
			pai->expansion_attacks[player_ai(g, p)] = read_byte(g, s);
			break;
		case OP_PANIC: /* SetPanicBlock 0x482c60 */
			pai->panic[player_ai(g, p)] = read_word(g, s);
			break;
		case OP_FAKE_NUKE: /* SetFakeNukeTime 0x482f80 */
			pai->last_nuke[player_ai(g, p)] = kp_u32v(kp_scalar(g, KP_ELAPSED) + 1);
			break;
		case OP_DEFINE_MAX: /* SetUnitBuildLimit 0x482c40 */
			a = read_byte(g, s);
			b = read_word(g, s);
			if (b >= KP_UNIT_TYPES) kp_game_fail(g, "ai: define_max of type %lld (0x482c40)", (long long)b);
			pai->limits[player_ai(g, p)][b] = a;
			break;
		case OP_SET_GENCMD: { /* SetAiRecord4d8 0x483ab0 */
			int k;
			rect_of(sc, s, rect);
			for (k = 0; k < 4; k++) pai->area[player_ai(g, p)][k] = rect[k];
			break;
		}
		case OP_GIVE_MONEY:
			give_money(g, p);
			break;
		case OP_ATTACK_ADD:
			a = read_byte(g, s);
			attack_add(g, p, a, read_word(g, s));
			break;
		case OP_EASY_ATTACK:
			a = read_byte(g, s);
			b = read_word(g, s);
			if (pai->ai_state[player_ai(g, p)] == 0) attack_add(g, p, a, b);
			break;
		case OP_PREP_DOWN: {
			int64_t n;
			a = read_byte(g, s);
			b = read_byte(g, s);
			c = read_word(g, s);
			n = kp_s32v(merged_count(g, c, p, 0) - a);
			attack_add(g, p, n > b ? n : b, c);
			break;
		}
		case OP_ATTACK_CLEAR:
			kp_ai_attack_clear(g, p, 1);
			break;
		case OP_DEFENSEBUILD_GG: case OP_DEFENSEBUILD_GG + 1: case OP_DEFENSEBUILD_GG + 2: case OP_DEFENSEBUILD_AA:
		case OP_DEFENSEUSE_GG: case OP_DEFENSEUSE_GG + 1: case OP_DEFENSEUSE_GG + 2: case OP_DEFENSEUSE_AA:
			a = read_byte(g, s);
			defense_add(g, p, (int)(op - OP_DEFENSEBUILD_GG), a, read_word(g, s));
			break;
		case OP_DEFENSECLEAR_GG: case OP_DEFENSECLEAR_GG + 1: case OP_DEFENSECLEAR_GG + 2: case OP_DEFENSECLEAR_AA: {
			int list = (int)(op - OP_DEFENSECLEAR_GG); /* 0x482890, 0x482850, 0x482810, 0x4827d0: build and use */
			memset(pai->defense[player_ai(g, p)][list], 0, sizeof pai->defense[p][list]);
			memset(pai->defense[p][list + DEFENSE_LISTS], 0, sizeof pai->defense[p][list + DEFENSE_LISTS]);
			break;
		}
		case OP_WAIT_FORCE: case OP_DO_MORPH: case OP_TRAIN:
			a = read_byte(g, s);
			b = read_word(g, s);
			if (merged_count(g, b, p, op != OP_TRAIN) < a) {
				pai->wanted_unit[player_ai(g, p)] = kp_band(b + 1, 0xffff); /* SetWantedUnit 0x482eb0 */
				if (op != OP_DO_MORPH) {
					retry(g, s, pc);
					return;
				}
			}
			break;
		case OP_WAIT_TRAIN:
			a = read_byte(g, s);
			b = read_word(g, s);
			if (merged_count(g, b, p, 1) < a) {
				retry(g, s, pc);
				return;
			}
			break;
		case OP_PLAYER_ENEMY: case OP_PLAYER_ALLY: /* ScriptPlayerEnemy 0x48e170, ScriptPlayerAlly 0x48e160 */
			rect_of(sc, s, rect);
			alliance_in_rect(g, p, rect, op == OP_PLAYER_ALLY ? ALLIED_VICTORY : 0);
			break;
		case OP_SWITCH_RESCUE:
			switch_rescue(g, p);
			break;
		case OP_SHAREDVISION_ON: case OP_SHAREDVISION_OFF: /* SetSharedVision 0x4b0fe0 */
			a = read_byte(g, s);
			if (a >= KP_PLAYERS) kp_game_fail(g, "ai: shared vision with player %lld (0x4b0fe0)", (long long)a);
			g->players.shared_vision[a] = op == OP_SHAREDVISION_ON ? kp_bor(g->players.shared_vision[a], (int64_t)1 << p)
				: kp_band(g->players.shared_vision[a], kp_bnot((int64_t)1 << p));
			break;
		case OP_CLEAR_COMBATDATA:
			rect_of(sc, s, rect);
			clear_combat_data(g, p, rect);
			break;
		case OP_VALUE_AREA:
			value_area(g, p, sc->x[s], sc->y[s]);
			break;
		case OP_ENTER_BUNKER:
			rect_of(sc, s, rect);
			enter_bunkers(g, p, rect);
			break;
		case OP_DISRUPTION_WEB:
			cast_at_location(g, s, KP_TYPE_CORSAIR, TECH_DISRUPTION_WEB, KP_ORD_CAST_DISRUPTION_WEB);
			break;
		case OP_RECALL_LOCATION:
			cast_at_location(g, s, KP_TYPE_ARBITER, TECH_RECALL, KP_ORD_CAST_RECALL);
			break;
		case OP_START_TOWN: case OP_START_AREATOWN: { /* StartTown 0x487030, StartAreaTown 0x486e80 */
			int64_t area = kp_shrv(sc->flags[s], AREA_SHIFT);
			if (op == OP_START_TOWN) {
				sc->town[s] = kp_ai_start_town(g, p, sc->x[s], sc->y[s], area);
			} else {
				rect_of(sc, s, rect);
				sc->town[s] = kp_ai_start_area_town(g, p, rect, area);
			}
			if (sc->town[s] == NONE) {
				kp_aiscript_free(g, s);
				return;
			}
			town_started(g, p);
			break;
		}
		case OP_WAIT_BUILD: case OP_WAIT_BUILDSTART: { /* 0x493abe: the town's count of the type (completed for
			                                                * wait_build), workers up to the area's limit */
			int64_t k;
			a = read_byte(g, s);
			b = read_word(g, s);
			k = script_town(g, s, op);
			if (b == kp_ai_race_worker(g, p) && g->towns.area[k] != 0) {
				int64_t limit = kp_band(kp_ai_area_worker_limit(g, g->towns.area[k], p, 1), 0xff); /* 0x496690 */
				if (a > limit) a = limit;
			}
			if (kp_ai_count_town_units(g, k, b, op == OP_WAIT_BUILD) >= a) break; /* CountTownUnits 0x4872f0 */
			retry(g, s, pc);
			return;
		}
		case OP_RACE_JUMP: unported(g, op, 0x486c50, "race_jump"); break;
		case OP_REGION_SIZE: unported(g, op, 0x4877f0, "region_size"); break;
		case OP_GET_OLDPEONS: unported(g, op, 0x486480, "get_oldpeons"); break;
		case OP_GROUNDMAP_JUMP: unported(g, op, 0x486410, "groundmap_jump"); break;
		case OP_ALLIES_WATCH: /* a free area is reserved for a script there */
			a = read_byte(g, s);
			b = read_word(g, s);
			if (kp_ai_area_reserve(g, a)) kp_aiscript_spawn_area_town(g, p, (int)kp_band(sc->flags[s], KP_AI_SCRIPT_BW), a, b);
			break;
		case OP_TRY_TOWNPOINT:
			a = read_byte(g, s);
			b = read_word(g, s);
			if (player_towns(g, p) == a) sc->pc[s] = b;
			break;
		case OP_EXPAND: /* ScriptExpand 0x494f20 while the player has at most `a` towns (a is also the mode) */
			a = read_byte(g, s);
			b = read_word(g, s);
			if (player_towns(g, p) > a) break;
			if (kp_s32v(g->players.minerals[p]) >= RICH_EXPANSION_MINERALS && kp_s32v(g->players.gas[p]) >= RICH_EXPANSION_GAS
				&& !kp_ai_campaign(g, p)) {
				break;
			}
			{
				int64_t home = g->towns.area[script_town(g, s, op)], area; /* GetTownArea 0x487b70 */
				if (home == 0) break;
				area = kp_ai_choose_expansion(g, p, home, a); /* 0x4970e0 */
				if (area != 0) kp_aiscript_spawn_area_town(g, p, (int)kp_band(sc->flags[s], KP_AI_SCRIPT_BW), area, b);
			}
			break;
		case OP_GUARD_RESOURCES: unported(g, op, 0x493790, "guard_resources"); break;
		case OP_PLACE_GUARD: { /* PlaceTownGuard 0x4b27f0: a guard of the type waits at the selected place */
			int64_t x, y;
			b = read_word(g, s);
			a = read_byte(g, s);
			if (kp_ai_town_guard_position(g, script_town(g, s, op), a, &x, &y)) kp_guard_precreate(g, p, b, x, y);
			break;
		}
		case OP_BUILD:
			a = read_byte(g, s);
			b = read_word(g, s);
			c = read_byte(g, s);
			kp_ai_town_build_request(g, script_town(g, s, op), a, b, c, 0);
			break;
		case OP_PLAYER_NEED:
			a = read_byte(g, s);
			b = read_word(g, s);
			kp_ai_town_build_request(g, script_town(g, s, op), a, b, PLAYER_NEED_PRIORITY, 1);
			break;
		case OP_UPGRADE:
			a = read_byte(g, s);
			b = read_word(g, s);
			c = read_byte(g, s);
			kp_ai_town_upgrade_request(g, script_town(g, s, op), a, b, c);
			break;
		case OP_TECH:
			a = read_word(g, s);
			kp_ai_town_tech_request(g, script_town(g, s, op), a, read_byte(g, s));
			break;
		case OP_WAIT_UPGRADES: unported(g, op, 0x483200, "wait_upgrades"); break;
		case OP_ATTACK_PREPARE:
			kp_ai_prepare_attack(g, p, sc->x[s], sc->y[s], 1, 0); /* PrepareAttack 0x4af300 */
			break;
		case OP_ATTACK_DO:
			if (kp_ai_prepare_attack(g, p, sc->x[s], sc->y[s], 1, 0)) break;
			retry(g, s, pc);
			return;
		case OP_QUICK_ATTACK:
			kp_ai_prepare_attack(g, p, sc->x[s], sc->y[s], 1, 0);
			kp_ai_start_grouping(g, p, QUICK_ATTACK_GROUPING); /* StartAttackGrouping 0x483ad0 */
			break;
		case OP_WAIT_FINISHATTACK:
			if (!kp_ai_attack_active(g, p)) break; /* HasActiveAttackRegion 0x4ab5d0 */
			retry(g, s, pc);
			return;
		case OP_WAIT_SECURE: unported(g, op, 0x482470, "wait_secure"); break;
		case OP_CAPT_EXPAND: kp_ai_captain_expand(g, p); break; /* CaptainExpand 0x4ad310 */
		case OP_BUILD_BUNKERS: unported(g, op, 0x4ade10, "build_bunkers"); break;
		case OP_WAIT_BUNKERS: unported(g, op, 0x4b05c0, "wait_bunkers"); break;
		case OP_BUILD_TURRETS: unported(g, op, 0x4aded0, "build_turrets"); break;
		case OP_WAIT_TURRETS: unported(g, op, 0x4b0600, "wait_turrets"); break;
		case OP_SEND_SUICIDE: /* the attack roster cleared, then SendSuicide 0x4b11b0 and a grouping of 5 */
			a = read_byte(g, s);
			kp_ai_attack_clear(g, p, 1); /* ClearAttackRoster 0x483770 */
			kp_ai_send_suicide(g, p, (int)a);
			kp_ai_start_grouping(g, p, QUICK_ATTACK_GROUPING); /* StartAttackGrouping 0x483ad0 */
			break;
		case OP_MOVE_DT: /* MoveDarkTemplars 0x48dcf0: every Dark Templar joins the military at the script's centre */
			for (a = g->player_units[p]; a != 0; a = g->u.player_next[a]) {
				if (kp_unit_inactive(g, a) || (g->u.type[a] != KP_TYPE_DARK_TEMPLAR && g->u.type[a] != KP_TYPE_HERO_DARK_TEMPLAR)) continue;
				if (g->u.ai[a] != 0) kp_remove_unit_ai(g, a, 0); /* 0x4b13d0: RemoveUnitAi 0x48dbc0 */
				kp_ai_military_add(g, g->u.owner[a], kp_band(kp_region_at(g, sc->x[s], sc->y[s]), 0xffff), a, 1); /* 0x4ae760 */
			}
			break;
		case OP_GUARD_ALL: /* every unit of the player claims its region (0x4b00a0) */
			for (a = g->player_units[p]; a != 0; a = g->u.player_next[a]) kp_ai_building_region_state(g, p, a);
			break;
		case OP_HELP_IFTROUBLE: help_allies(g, p); break;
		case OP_HARASS_FACTOR: unported(g, op, 0x483830, "harass_factor"); break;
		case OP_EVAL_HARASS: unported(g, op, 0x4af5b0, "eval_harass"); break;
		case OP_RUSH: unported(g, op, 0x4ad550, "rush"); break;
		case OP_IMPLODE: unported(g, op, 0x4ad430, "implode"); break;
		case OP_ENTER_TRANSPORT: unported(g, op, 0x4af520, "enter_transport"); break;
		case OP_EXIT_TRANSPORT: unported(g, op, 0x4af6d0, "exit_transport"); break;
		case OP_JUNKYARD_DOG: { /* IssueOrderToPlayerUnitsInRect 0x4b0ec0: the player's units in the rectangle */
			int count, k;
			int64_t *found;
			rect_of(sc, s, rect);
			found = kp_query_rect(g, kp_s16v(rect[0]), kp_s16v(rect[1]), kp_s16v(rect[2]), kp_s16v(rect[3]), &count);
			for (k = 0; k < count; k++) {
				if (g->u.owner[found[k]] == p) kp_issue_order(g, found[k], KP_ORD_JUNKYARD_DOG);
			}
			kp_query_release(g, found);
			break;
		}
		case OP_MAKE_PATROL: unported(g, op, 0x4b0f10, "make_patrol"); break;
		case OP_NUKE_LOCATION: case OP_NUKE_POS: { /* a ready Ghost paints a nuke with a ready Silo's missile: at the
			                                            * location's best enemy, else its centre, or at the point */
			int64_t ghost, silo, target = 0, x, y;
			if (op == OP_NUKE_POS) {
				a = read_word(g, s);
				b = read_word(g, s);
			}
			ghost = kp_ai_script_caster(g, p, KP_TYPE_GHOST, 0); /* 0x4932a0 */
			if (ghost == 0) break;
			silo = kp_find_ready_silo(g, ghost, 1, ghost); /* 0x436f60 */
			if (silo == 0) break;
			if (op == OP_NUKE_POS) {
				x = a;
				y = b;
			} else {
				rect_of(sc, s, rect);
				target = kp_ai_nuke_target_in_rect(g, ghost, rect); /* 0x432f60 */
				x = kp_s32v(sc->left[s] + sc->right[s]) / 2;
				y = kp_s32v(sc->top[s] + sc->bottom[s]) / 2;
			}
			if (target != 0) kp_issue_order_target(g, ghost, KP_ORD_NUKE_PAINT, target);
			else kp_issue_order_at(g, ghost, KP_ORD_NUKE_PAINT, x, y);
			g->u.connected[g->u.union_0xd0[silo]] = ghost;
			break;
		}
		case OP_CREATE_UNIT: { /* the unit is made at the point and cloaks when its tech allows */
			int64_t n, tech;
			a = read_word(g, s);
			b = read_word(g, s);
			c = read_word(g, s);
			n = kp_create_unit_checked(g, a, b, c, p); /* 0x4261f0 */
			if (n == 0) break;
			tech = kp_cloak_tech(g, n); /* 0x434900 */
			if (tech != NO_TECH && kp_can_use_tech(g, tech, g->u.owner[n], n) == 1) kp_cloak_hidden(g, n); /* 0x41d410 */
			break;
		}
		case OP_CREATE_NUKE: /* the player's first Silo without a missile gets one */
			for (a = g->player_units[p]; a != 0; a = g->u.player_next[a]) {
				if (g->u.type[a] == KP_TYPE_NUCLEAR_SILO && g->u.union_0xd4[a] == 0) {
					kp_silo_create_nuke(g, a);
					break;
				}
			}
			break;
		/* outside the campaigns' scripts */
		case OP_DEBUG: unported(g, op, 0x46a860, "debug"); break;
		case OP_FATAL_ERROR: unported(g, op, 0x4cc190, "fatal_error"); break;
		default: unported(g, op, 0x494a98, "unknown"); break;
		}
	}
}

/* RunAiScripts 0x4938e0: every active script's wait counts down; one whose wait was 0 is freed when it was killed,
 * else it runs. The next script is taken before one runs, so a script that one starts runs from the next pass. */
void kp_aiscript_run(KpGame *g)
{
	KpScripts *sc = &g->scripts;
	int64_t s = sc->head;
	while (s != NONE) {
		int64_t next = sc->next[s], old = sc->wait[s];
		sc->wait[s] = kp_u32v(old - 1);
		if (old == 0) {
			if (kp_band(sc->flags[s], KILLED) != 0) kp_aiscript_free(g, s);
			else kp_aiscript_execute(g, s);
		}
		if (sc->head == NONE) break;
		s = next;
	}
}
