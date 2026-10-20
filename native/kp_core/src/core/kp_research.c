/* Research and upgrades (kp_game.h), ported from the atlas (P02, P12, P15, P25, P34, P35;
 * docs/research/native-research-upgrade.md, native-dat-requirements.md): the price checks CheckTechCost 0x408670 and
 * CheckUpgradeCost 0x408710, the starts 0x43af40 and 0x43afb0, the in-progress bits (0x4a0560, 0x4a0590, 0x4a05c0,
 * 0x4a05f0, 0x4a0500, 0x4a0530), the state writers SetTechResearched 0x49a300 and SetUpgradeLevel 0x49a370 (the
 * expansion's ids only in an expansion game), the orders ResearchTech 75 (0x43ac40) and Upgrade 76 (0x43ace0), the
 * full and three-quarter refunds (0x43ab80, 0x43abe0, 0x43adf0, 0x43ae60) and the commands Research 0x30 (0x472dc0),
 * Cancel Research 0x31 (0x4735e0), Upgrade 0x32 (0x473a60) and Cancel Upgrade 0x33 (0x473630). The gates
 * CanResearchTech 0x407dc0 and CanUpgrade 0x408140 are kp_requirements.c's.
 *
 * The producer's union: +0xc6 the time left (WORD), +0xc8 the technology being researched (44 for none), +0xc9 the
 * upgrade (61 for none), +0xcd the level it reaches. */
#include "kp_game_internal.h"

#define FIRST_EXPANSION_TECH 24
#define FIRST_EXPANSION_UPGRADE 46
#define DAT_BUILDING 0x1
#define STATUS_COMPLETED 0x1
#define STATUS_GROUNDED_BUILDING 0x2
#define SOUND_RESEARCH_DONE 0x7b /* + race (0x446b70) */
#define SOUND_UPGRADE_DONE 0x83  /* + race (0x447060) */
#define U_TIME 0xc6
#define U_TECH 0xc8
#define U_UPGRADE 0xc9
#define U_NEXT_LEVEL 0xcd

static void check_player(KpGame *g, int64_t p)
{
	if (p < 0 || p >= KP_PLAYERS) kp_game_fail(g, "research: player %lld", (long long)p);
}

static void play(KpGame *g, int64_t n, int anim) { kp_sprite_play(g->w, kp_unit_sprite(g, n), anim, 1); }

/* The in-progress byte of a technology (0x536510: 6 bytes a player) or an upgrade (0x5366c0: 8 bytes). */
static int64_t *progress_byte(KpGame *g, int64_t p, int64_t id, int upgrade)
{
	check_player(g, p);
	if (id < 0 || kp_shrv(id, 3) >= (upgrade ? KP_UPGRADE_PROGRESS_BYTES : KP_TECH_PROGRESS_BYTES)) {
		kp_game_fail(g, "research: id %lld", (long long)id);
	}
	return upgrade ? &g->players.upgrade_progress[p][kp_shrv(id, 3)] : &g->players.research_progress[p][kp_shrv(id, 3)];
}

/* IsResearchInProgress 0x4a0560 and IsUpgradeInProgress 0x4a0590 (normalized: every caller tests for nonzero). */
int kp_in_progress(KpGame *g, int64_t p, int64_t id, int upgrade)
{
	return kp_band(*progress_byte(g, p, id, upgrade), kp_shlv(1, kp_band(id, 7))) != 0;
}

/* 0x4a05c0/0x4a05f0 set, 0x4a0500/0x4a0530 clear. */
static void set_progress(KpGame *g, int64_t p, int64_t id, int upgrade, int on)
{
	int64_t *b = progress_byte(g, p, id, upgrade), bit = kp_shlv(1, kp_band(id, 7));
	*b = on ? kp_bor(*b, bit) : kp_band(*b, kp_bnot(bit));
}

/* SetTechResearched 0x49a300 and SetUpgradeLevel 0x49a370: the expansion's ids only in an expansion game. */
static void set_researched(KpGame *g, int64_t p, int64_t tech, int64_t v)
{
	check_player(g, p);
	if (tech < FIRST_EXPANSION_TECH || g->expansion != 0) g->players.tech_researched[p][tech] = kp_band(v, 0xff);
}

void kp_set_upgrade_level(KpGame *g, int64_t p, int64_t id, int64_t v)
{
	check_player(g, p);
	if (id < FIRST_EXPANSION_UPGRADE || g->expansion != 0) g->players.upgrades[p][id] = kp_band(v, 0xff);
}

/* The price of an upgrade's next level (0x43b2a0 minerals, 0x43b270 gas): base + level * factor, a WORD. */
static int64_t upgrade_minerals(KpGame *g, int64_t p, int64_t id)
{
	return kp_band(kp_band(kp_upgrade_level(g, p, id), 0xff) * KP_DAT(g, upgrades, mineral_cost_factor, id)
		+ KP_DAT(g, upgrades, mineral_cost_base, id), 0xffff);
}

static int64_t upgrade_gas(KpGame *g, int64_t p, int64_t id)
{
	return kp_band(kp_band(kp_upgrade_level(g, p, id), 0xff) * KP_DAT(g, upgrades, vespene_cost_factor, id)
		+ KP_DAT(g, upgrades, vespene_cost_base, id), 0xffff);
}

static int64_t tech_minerals(KpGame *g, int64_t tech) { return kp_band(KP_DAT(g, techdata, mineral_cost, tech), 0xffff); }
static int64_t tech_gas(KpGame *g, int64_t tech) { return kp_band(KP_DAT(g, techdata, vespene_cost, tech), 0xffff); }

/* CheckTechCost 0x408670 and CheckUpgradeCost 0x408710: the price cached with no supply, then the stock. */
static int check_cost(KpGame *g, int64_t p, int64_t minerals, int64_t gas)
{
	check_player(g, p);
	g->cost_minerals[p] = minerals;
	g->cost_gas[p] = gas;
	g->cost_supply[p] = 0;
	return kp_check_stock(g, p, 1);
}

/* GetTechCost 0x408790 and GetUpgradeCost 0x408820: the prices (WORDs). */
void kp_tech_price(KpGame *g, int64_t tech, int64_t *minerals, int64_t *gas)
{
	*minerals = tech_minerals(g, tech);
	*gas = tech_gas(g, tech);
}

void kp_upgrade_price(KpGame *g, int64_t p, int64_t id, int64_t *minerals, int64_t *gas)
{
	*minerals = upgrade_minerals(g, p, id);
	*gas = upgrade_gas(g, p, id);
}

int kp_check_tech_cost(KpGame *g, int64_t p, int64_t tech)
{
	return check_cost(g, p, tech_minerals(g, tech), tech_gas(g, tech));
}

int kp_check_upgrade_cost(KpGame *g, int64_t p, int64_t id)
{
	return check_cost(g, p, upgrade_minerals(g, p, id), upgrade_gas(g, p, id));
}

/* StartResearch 0x43af40: the producer pays and starts tech (its legality is the caller's). */
int kp_start_research(KpGame *g, int64_t n, int64_t tech)
{
	int64_t p = g->u.owner[n];
	tech = kp_band(tech, 0xff);
	if (tech >= KP_TECH_NONE || !check_cost(g, p, tech_minerals(g, tech), tech_gas(g, tech))) return 0;
	kp_union_set(g, n, U_TECH, 1, tech);
	kp_union_set(g, n, U_TIME, 2, KP_DAT(g, techdata, research_time, tech));
	kp_deduct_cost(g, p);
	set_progress(g, p, tech, 0, 1);
	play(g, n, KP_ANIM_IS_WORKING);
	return 1;
}

/* StartUpgrade 0x43afb0: the time is base + level * factor (WORD), the level reached the current one + 1 (a byte). */
int kp_start_upgrade(KpGame *g, int64_t n, int64_t id)
{
	int64_t p = g->u.owner[n], level;
	id = kp_band(id, 0xff);
	if (id >= KP_UPG_NONE || !check_cost(g, p, upgrade_minerals(g, p, id), upgrade_gas(g, p, id))) return 0;
	kp_union_set(g, n, U_UPGRADE, 1, id);
	level = kp_band(kp_upgrade_level(g, p, id), 0xff);
	kp_union_set(g, n, U_TIME, 2,
		level * KP_DAT(g, upgrades, research_time_factor, id) + KP_DAT(g, upgrades, research_time_base, id));
	kp_union_set(g, n, U_NEXT_LEVEL, 1, level + 1);
	kp_deduct_cost(g, p);
	set_progress(g, p, id, 1, 1);
	play(g, n, KP_ANIM_IS_WORKING);
	return 1;
}

/* The advisor's voice of a finished research (0x446b70) or upgrade (0x447060) for the local owner; the talking
 * portrait it requests is the presentation's. */
static void done_voice(KpGame *g, int64_t n, int64_t base)
{
	if (g->u.owner[n] != g->local_player || g->w->replay) return;
	kp_play_unit_sound(g, base + kp_unit_race(g, n), n);
}

/* The end of both orders: ComputerAI with an AI record, else the next order. */
static void end_order(KpGame *g, int64_t n)
{
	if (g->u.ai[n] != 0) kp_issue_order(g, n, KP_ORD_COMPUTER_AI);
	else kp_order_done(g, n);
}

/* OrderResearchTech 0x43ac40 (order 75): the time runs down (a WORD), the research ends a step after it reaches 0. */
void kp_order_research(KpGame *g, int64_t n)
{
	int64_t p = g->u.owner[n], tech = kp_union_get(g, n, U_TECH, 1), time;
	if (tech != KP_TECH_NONE) {
		time = kp_union_get(g, n, U_TIME, 2);
		kp_union_set(g, n, U_TIME, 2, time - 1);
		if (time != 0 && !kp_tech_researched(g, p, tech) && kp_band(kp_cheats(g), KP_CHEAT_OPERATION_CWAL) == 0) return;
		done_voice(g, n, SOUND_RESEARCH_DONE);
		set_researched(g, p, tech, 1);
	}
	play(g, n, KP_ANIM_WORKING_TO_IDLE);
	set_progress(g, p, tech, 0, 0);
	kp_union_set(g, n, U_TECH, 1, KP_TECH_NONE);
	end_order(g, n);
}

/* OrderUpgrade 0x43ace0 (order 76): only a grounded building's upgrade runs. */
void kp_order_upgrade(KpGame *g, int64_t n)
{
	int64_t p = g->u.owner[n], id = kp_union_get(g, n, U_UPGRADE, 1), time, next, level;
	if (kp_band(g->u.status[n], STATUS_GROUNDED_BUILDING) != 0 && id != KP_UPG_NONE) {
		time = kp_union_get(g, n, U_TIME, 2);
		next = kp_union_get(g, n, U_NEXT_LEVEL, 1);
		kp_union_set(g, n, U_TIME, 2, time - 1);
		if (time != 0 && kp_band(kp_upgrade_level(g, p, id), 0xff) < next && kp_band(kp_cheats(g), KP_CHEAT_OPERATION_CWAL) == 0) return;
		done_voice(g, n, SOUND_UPGRADE_DONE);
		level = kp_band(kp_upgrade_level(g, p, id), 0xff);
		if (level < next && level < kp_band(g->players.upgrade_max[p][id], 0xff)) {
			kp_set_upgrade_level(g, p, id, next);
			kp_apply_upgrade_to_units(g, p, id); /* 0x43b0b0 */
		}
	}
	play(g, n, KP_ANIM_WORKING_TO_IDLE);
	set_progress(g, p, id, 1, 0);
	kp_union_set(g, n, U_UPGRADE, 1, KP_UPG_NONE);
	kp_union_set(g, n, U_NEXT_LEVEL, 1, 0);
	end_order(g, n);
}

static void add_stock(KpGame *g, int64_t p, int64_t minerals, int64_t gas, int full)
{
	check_player(g, p);
	g->players.minerals[p] = kp_u32v(g->players.minerals[p] + (full ? minerals : minerals * 3 / 4));
	g->players.gas[p] = kp_u32v(g->players.gas[p] + (full ? gas : gas * 3 / 4));
}

/* The research's price back, all with the idle animation (RefundResearch 0x43ab80) or three quarters without
 * (0x43adf0). */
void kp_refund_research(KpGame *g, int64_t n, int full)
{
	int64_t tech = kp_union_get(g, n, U_TECH, 1), p = g->u.owner[n];
	if (tech == KP_TECH_NONE) return;
	add_stock(g, p, tech_minerals(g, tech), tech_gas(g, tech), full);
	set_progress(g, p, tech, 0, 0);
	kp_union_set(g, n, U_TECH, 1, KP_TECH_NONE);
	kp_union_set(g, n, U_TIME, 2, 0);
	if (full) play(g, n, KP_ANIM_WORKING_TO_IDLE);
}

/* The upgrade's price at the current level back: all for a building type, with the idle animation (RefundUpgrade
 * 0x43abe0), or three quarters for any type, without (0x43ae60). */
void kp_refund_upgrade(KpGame *g, int64_t n, int full)
{
	int64_t id = kp_union_get(g, n, U_UPGRADE, 1), p = g->u.owner[n];
	if (full && kp_band(KP_DAT(g, units, special_ability_flags, g->u.type[n]), DAT_BUILDING) == 0) return;
	if (id == KP_UPG_NONE) return;
	add_stock(g, p, upgrade_minerals(g, p, id), upgrade_gas(g, p, id), full);
	set_progress(g, p, id, 1, 0);
	kp_union_set(g, n, U_UPGRADE, 1, KP_UPG_NONE);
	kp_union_set(g, n, U_TIME, 2, 0);
	kp_union_set(g, n, U_NEXT_LEVEL, 1, 0);
	if (full) play(g, n, KP_ANIM_WORKING_TO_IDLE);
}

/* ExecResearch 0x472dc0 (command 0x30) and ExecUpgrade 0x473a60 (command 0x32): the only selected unit starts what
 * its gate allows (a refusal's message is the interface's). */
void kp_command_research(KpGame *g, int64_t player, int64_t n, int64_t id, int upgrade)
{
	if (n == 0) return;
	if (upgrade) {
		if (kp_can_upgrade(g, id, player, n) == 1 && kp_start_upgrade(g, n, id)) kp_issue_order(g, n, KP_ORD_UPGRADE);
	} else if (kp_can_research(g, id, player, n) == 1 && kp_start_research(g, n, id)) {
		kp_issue_order(g, n, KP_ORD_RESEARCH);
	}
}

/* ExecCancelResearch 0x4735e0 (command 0x31: a completed building of the player's) and ExecCancelUpgrade 0x473630
 * (command 0x33: any completed unit of the player's; the refund itself wants a building type). */
void kp_command_cancel_research(KpGame *g, int64_t player, int64_t n, int upgrade)
{
	KpUnits *u = &g->u;
	if (n == 0 || kp_band(u->status[n], STATUS_COMPLETED) == 0 || u->owner[n] != player) return;
	if (upgrade) {
		kp_refund_upgrade(g, n, 1);
	} else if (kp_band(KP_DAT(g, units, special_ability_flags, u->type[n]), DAT_BUILDING) != 0) {
		kp_refund_research(g, n, 1);
	}
}
