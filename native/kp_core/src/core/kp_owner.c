/* Ownership transfer with its AI hooks (kp_game.h). */
#include "kp_game_internal.h"

#define CONTROLLER_COMPUTER 1
#define NEUTRAL 11
#define SPRITE_SELECTED 0x08
#define STATUS_COMPLETED 0x1
#define STATUS_GROUNDED_BUILDING 0x2
#define DAT_WORKER 0x08
#define DAT_SUBUNIT 0x10
#define AI_TOWN 0x2
#define MEDIC 0x22
#define FLAG 0xd7
#define RACE_PROTOSS 2


/* IsPlayerEnemyOfUnit 0x429110. */
int kp_player_enemy_of(KpGame *g, int64_t p, int64_t n)
{
	int64_t owner = g->u.owner[n];
	if (owner == NEUTRAL) owner = g->w->spr.owner[kp_unit_sprite(g, n)];
	return kp_alliance(g, p, owner) == 0;
}

/* RemoveUnitAi 0x48dbc0. */
void kp_remove_unit_ai(KpGame *g, int64_t n, int64_t killed)
{
	KpUnits *u = &g->u;
	kp_ai_remove_from_regions(g, n, killed);
	/* RemoveUnitFromTown 0x486af0 (with ClearResourceReferences 0x486000): no player has towns before the AI scripts
	 * (plan phase 7) make them, so only a unit's town record could matter. */
	if (u->ai[n] != 0 && kp_ai_unit_record_type(g, n) != 1) kp_game_fail(g, "ai: removing a unit from its town (0x486af0)");
	kp_ai_detach_guard(g, n);
	if (u->type[n] == MEDIC && kp_controller(g, u->owner[n]) == CONTROLLER_COMPUTER) {
		kp_game_fail(g, "ai: the free Medic of a computer player (0x483220)");
	}
	u->ai[n] = 0;
}

/* AssignCreatedUnitAi 0x4b1fe0. */
void kp_assign_unit_ai(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t p = u->owner[n];
	if (p >= 8) return;
	if (u->type[n] == MEDIC && kp_controller(g, p) == CONTROLLER_COMPUTER) {
		kp_game_fail(g, "ai: the free Medic of a computer player (0x482520)");
	}
	if (kp_band(KP_DAT(g, units, ai_internal, u->type[n]), AI_TOWN) != 0) return;
	kp_guard_attach(g, n);
}

/* ClearNonEnemyTargets 0x41dd40. */
static void clear_non_enemy_targets(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t target = u->order_unit[n];
	int k;
	if (target != 0 && KP_DAT(g, orders, use_weapon_targeting, u->order[n]) != 0 && !kp_is_enemy(g, n, target)) {
		u->order_unit[n] = 0;
	}
	for (k = 0; k < 2; k++) {
		int64_t m = g->lists[k == 0 ? KP_LIST_ACTIVE : KP_LIST_HIDDEN].head;
		while (m != 0) {
			int64_t x = m, sub = u->subunit[m];
			if (sub != 0 && kp_band(KP_DAT(g, units, special_ability_flags, u->type[sub]), DAT_SUBUNIT) != 0) x = sub;
			if (u->order_unit[x] == n && KP_DAT(g, orders, use_weapon_targeting, u->order[x]) != 0 && !kp_is_enemy(g, x, n)) {
				u->order_unit[x] = 0;
			}
			m = u->next[m];
		}
	}
}

void kp_change_owner(KpGame *g, int64_t n, int64_t owner, int score)
{
	KpUnits *u = &g->u;
	KpImages *w = g->w;
	int morphing, completed, selected;
	int32_t s;
	int64_t slot = 0, prev, next, head, sub;
	if (u->owner[n] == owner) return;
	kp_remove_unit_ai(g, n, 0);
	morphing = kp_is_morphing_zerg_building(g, n);
	if (morphing) u->status[n] = kp_bor(u->status[n], STATUS_COMPLETED);
	kp_account_live(g, n, -1);
	completed = kp_band(u->status[n], STATUS_COMPLETED) != 0;
	if (completed) kp_account_completed(g, n, -1, 0);
	s = kp_unit_sprite(g, n);
	selected = kp_band(w->spr.flags[s], SPRITE_SELECTED) != 0;
	if (selected) {
		slot = w->spr.selection[s];
		kp_remove_selection_circle(g, n);
	}
	prev = u->player_prev[n];
	next = u->player_next[n];
	if (prev != 0) u->player_next[prev] = next;
	if (next != 0) u->player_prev[next] = prev;
	if (g->player_units[u->owner[n]] == n) g->player_units[u->owner[n]] = next;
	u->player_prev[n] = u->player_next[n] = 0;
	u->owner[n] = owner;
	if (selected) kp_add_selection_circle(g, n, slot);
	if (owner < 0 || owner >= KP_PLAYERS) kp_game_fail(g, "owner: player %lld", (long long)owner);
	head = g->player_units[owner];
	u->player_prev[n] = 0;
	u->player_next[n] = head;
	if (head != 0) u->player_prev[head] = n;
	g->player_units[owner] = n;
	kp_account_live(g, n, 1);
	if (completed) kp_account_completed(g, n, 1, score);
	if (morphing) u->status[n] = kp_band(u->status[n], kp_bnot(STATUS_COMPLETED));
	clear_non_enemy_targets(g, n);
	if (kp_unit_race(g, n) == RACE_PROTOSS && kp_band(u->status[n], STATUS_GROUNDED_BUILDING) != 0) {
		kp_scalar_set(g, KP_POWER_DIRTY, 1);
	}
	kp_assign_unit_ai(g, n);
	sub = u->subunit[n];
	if (sub != 0 && kp_band(KP_DAT(g, units, special_ability_flags, u->type[sub]), DAT_SUBUNIT) != 0) {
		kp_change_owner(g, sub, owner, score);
	}
	kp_apply_creation_upgrades(g, n);
	if (kp_band(KP_DAT(g, units, special_ability_flags, u->type[n]), DAT_WORKER) != 0) {
		int64_t item = u->union_0xc0[n];
		if (item != 0 && u->type[item] == FLAG && (owner == u->owner[item] || owner >= 8)) {
			kp_game_fail(g, "owner: a worker drops the flag it carries (0x4272c0)");
		}
	}
}

/* The technology recipes of the unit types (0x4f0940): a type, then the techs it uses, ending at 44. */
static const int16_t TECH_RECIPES[][6] = {
	{ 0, 0, 44 }, { 32, 0, 44 }, { 9, 6, 7, 2, 44 }, { 1, 1, 10, 44 }, { 2, 3, 44 }, { 5, 5, 44 }, { 30, 5, 44 },
	{ 8, 9, 44 }, { 12, 8, 44 }, { 45, 12, 18, 13, 17, 44 }, { 37, 11, 44 }, { 38, 11, 44 }, { 41, 11, 44 },
	{ 50, 11, 44 }, { 46, 11, 14, 15, 16, 44 }, { 67, 23, 19, 20, 44 }, { 71, 21, 22, 44 }, { 34, 34, 24, 30, 44 },
	{ 60, 25, 44 }, { 61, 28, 44 }, { 63, 27, 29, 31, 44 }, { 103, 32, 44 }, { 38, 32, 44 },
};

/* The upgrade recipes of the unit types (0x4f0a58): a type, then the upgrades it uses, ending at 61. */
static const int16_t UPGRADE_RECIPES[][5] = {
	{ 0, 16, 61 }, { 2, 17, 61 }, { 9, 19, 61 }, { 1, 20, 21, 61 }, { 8, 22, 61 }, { 12, 23, 61 },
	{ 42, 24, 25, 26, 61 }, { 37, 27, 28, 61 }, { 38, 29, 30, 61 }, { 45, 31, 61 }, { 46, 32, 61 }, { 66, 33, 61 },
	{ 65, 34, 61 }, { 83, 35, 36, 61 }, { 69, 37, 61 }, { 84, 38, 39, 61 }, { 67, 40, 61 }, { 70, 41, 42, 61 },
	{ 72, 43, 61 }, { 71, 44, 61 }, { 3, 54, 61 }, { 39, 53, 52, 61 }, { 34, 51, 61 }, { 63, 49, 61 }, { 60, 47, 61 },
};

#define NO_TECH 44
#define NO_UPGRADE 61
#define RECIPE_TECHS 5
#define RECIPE_UPGRADES 4
#define HANGAR_WARBRINGER 81
#define HANGAR_GANTRITHOR 82
#define HANGAR_CARRIER 72
#define HANGAR_REAVER 83
#define INTERCEPTOR 73
#define SCARAB 85
#define NUCLEAR_MISSILE 14
#define DAT_SINGLE_ENTITY 0x800
#define STATUS_LOADED 0x40
#define CONTROLLER_RESCUE 3
#define CONTROLLER_NEUTRAL 7
#define ORDER_RESCUE_PASSIVE 161
#define ORDER_NEUTRAL 162
#define STATUS_ADRENAL 0x20000000

/* ApplyUpgradeToExistingUnits 0x43b0b0: the speed (or the Adrenal Glands cooldown) of upgrade on owner's units. */
void kp_apply_upgrade_to_units(KpGame *g, int64_t owner, int64_t upgrade)
{
	KpUnits *u = &g->u;
	int64_t a = -1, b = -1, m;
	int speed = 1, adrenal = 0;
	switch (upgrade) {
	case 17: a = 2; b = 19; break;
	case 26: a = 42; break;
	case 27: a = 37; break;
	case 28: a = 37; speed = 0; adrenal = 1; break;
	case 29: a = 38; break;
	case 34: a = 65; break;
	case 37: a = 69; break;
	case 39: a = 84; break;
	case 42: a = 70; break;
	case 53: a = 39; b = 48; break;
	default: return;
	}
	for (m = g->player_units[owner]; m != 0; m = u->player_next[m]) {
		if (u->type[m] != a && u->type[m] != b) continue;
		if (adrenal) u->status[m] = kp_bor(u->status[m], STATUS_ADRENAL);
		if (speed) kp_set_speed_upgrade(g, m);
	}
}

/* SetSpriteOwnerWithSubunits 0x426e80: the sprites of n and its indexed subunits. */
void kp_set_sprite_owner(KpGame *g, int64_t n, int64_t owner)
{
	KpUnits *u = &g->u;
	int64_t m = n;
	while (m != 0) {
		g->w->spr.owner[kp_unit_sprite(g, m)] = (int32_t)owner;
		m = u->subunit[m];
		if (m != 0 && kp_band(KP_DAT(g, units, special_ability_flags, u->type[m]), DAT_SUBUNIT) == 0) break;
	}
}

/* GiveUnitToPlayer 0x428df0 for a unit TriggerGiveUnit 0x4b81a0 accepts. */
void kp_give_unit(KpGame *g, int64_t n, int64_t owner)
{
	KpUnits *u = &g->u;
	int64_t t = u->type[n], old = u->owner[n], ctl;
	size_t r;
	int k;
	/* ForEachUnitTechRecipe 0x4bbcb0 with CopyTechToRecipientCallback 0x429490, from the last row */
	for (r = sizeof TECH_RECIPES / sizeof TECH_RECIPES[0]; r-- > 0;) {
		if (TECH_RECIPES[r][0] != t) continue;
		for (k = 1; k <= RECIPE_TECHS && TECH_RECIPES[r][k] != NO_TECH; k++) {
			int64_t id = TECH_RECIPES[r][k];
			if (kp_tech_researched(g, old, id) && !kp_tech_researched(g, owner, id)) g->players.tech_researched[owner][id] = 1;
		}
	}
	/* ForEachUnitUpgradeRecipe 0x4bbd40 with CopyUpgradeToRecipientCallback 0x4294d0 */
	for (r = sizeof UPGRADE_RECIPES / sizeof UPGRADE_RECIPES[0]; r-- > 0;) {
		if (UPGRADE_RECIPES[r][0] != t) continue;
		for (k = 1; k <= RECIPE_UPGRADES && UPGRADE_RECIPES[r][k] != NO_UPGRADE; k++) {
			int64_t id = UPGRADE_RECIPES[r][k], level = kp_upgrade_level(g, old, id);
			if (level > kp_upgrade_level(g, owner, id)) g->players.upgrades[owner][id] = level;
		}
	}
	kp_remove_from_selections(g, n);
	kp_remove_from_local_selection(g, n);
	if (kp_band(u->status[n], STATUS_GROUNDED_BUILDING) != 0) {
		kp_game_fail(g, "owner: giving a building (0x41b5c0, 0x43ab80, 0x43abe0)");
	}
	if (t == HANGAR_CARRIER || t == HANGAR_WARBRINGER || t == HANGAR_GANTRITHOR || t == HANGAR_REAVER) {
		kp_game_fail(g, "owner: giving a hangar unit (0x41b5c0, 0x41ca90)");
	}
	if (kp_has_cargo(g, n)) kp_game_fail(g, "owner: giving a loaded transport (0x437da0)");
	kp_change_owner(g, n, owner, 1);
	if (owner < 8) kp_set_sprite_owner(g, n, owner);
	/* ForEachUnitUpgradeRecipe with ApplyUpgradeEffectCallback 0x429500 */
	for (r = sizeof UPGRADE_RECIPES / sizeof UPGRADE_RECIPES[0]; r-- > 0;) {
		if (UPGRADE_RECIPES[r][0] != t) continue;
		for (k = 1; k <= RECIPE_UPGRADES && UPGRADE_RECIPES[r][k] != NO_UPGRADE; k++) {
			if (kp_upgrade_level(g, owner, UPGRADE_RECIPES[r][k]) != 0) kp_apply_upgrade_to_units(g, owner, UPGRADE_RECIPES[r][k]);
		}
	}
	if (kp_band(u->status[n], STATUS_COMPLETED) == 0
		|| kp_band(KP_DAT(g, units, special_ability_flags, t), DAT_SINGLE_ENTITY) != 0
		|| t == INTERCEPTOR || t == SCARAB || t == NUCLEAR_MISSILE || kp_band(u->status[n], STATUS_LOADED) != 0) {
		return;
	}
	ctl = kp_controller(g, owner);
	if (ctl == CONTROLLER_COMPUTER) kp_issue_order(g, n, KP_DAT(g, units, comp_ai_idle, t));
	else if (ctl == CONTROLLER_RESCUE) kp_issue_order(g, n, ORDER_RESCUE_PASSIVE);
	else if (ctl == CONTROLLER_NEUTRAL) kp_issue_order(g, n, ORDER_NEUTRAL);
	else kp_issue_order(g, n, KP_DAT(g, units, human_ai_idle, t));
}
