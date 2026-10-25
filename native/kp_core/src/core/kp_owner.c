/* Ownership transfer with its AI hooks (kp_game.h). */
#include "kp_game_internal.h"

#define SPRITE_SELECTED 0x08
#define STATUS_COMPLETED 0x1
#define STATUS_GROUNDED_BUILDING 0x2
#define DAT_WORKER 0x08
#define DAT_SUBUNIT 0x10


/* IsPlayerEnemyOfUnit 0x429110. */
int kp_player_enemy_of(KpGame *g, int64_t p, int64_t n)
{
	int64_t owner = g->u.owner[n];
	if (owner == KP_PLAYER_NEUTRAL) owner = g->w->spr.owner[kp_unit_sprite(g, n)];
	return kp_alliance(g, p, owner) == 0;
}

/* RemoveUnitAi 0x48dbc0. */
void kp_remove_unit_ai(KpGame *g, int64_t n, int64_t killed)
{
	KpUnits *u = &g->u;
	kp_ai_remove_from_regions(g, n, killed);
	kp_ai_remove_from_town(g, n); /* RemoveUnitFromTown 0x486af0 */
	kp_ai_detach_guard(g, n);
	kp_ai_release_medic(g, n); /* 0x483220 */
	u->ai[n] = 0;
}

/* AssignCreatedUnitAi 0x4b1fe0: a computer's new unit of a town type joins its first town, another becomes a guard. */
void kp_assign_unit_ai(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t p = u->owner[n];
	if (p >= 8) return;
	kp_ai_register_medic(g, n); /* RegisterFreeMedic 0x482520 */
	if (kp_band(KP_DAT(g, units, ai_internal, u->type[n]), KP_AIINT_TOWN) != 0) {
		kp_ai_assign_to_first_town(g, n); /* AssignNewUnitToActiveTown 0x484f40 */
		return;
	}
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
	g->interface_refresh++; /* RefreshStatusUi 0x4a7ed0, for the interface */
	if (kp_unit_race(g, n) == KP_RACE_PROTOSS && kp_band(u->status[n], STATUS_GROUNDED_BUILDING) != 0) {
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
		if (item != 0 && u->type[item] == KP_TYPE_FLAG && (owner == u->owner[item] || owner >= 8)) {
			kp_drop_powerup(g, n);
		}
	}
}

/* The technology recipes of the unit types (0x4f0940): a type, then the techs it uses, ending at 44. */
static const int16_t TECH_RECIPES[][6] = {
	{ KP_TYPE_MARINE, KP_TECH_STIM_PACKS, KP_TECH_NONE },
	{ KP_TYPE_FIREBAT, KP_TECH_STIM_PACKS, KP_TECH_NONE },
	{ KP_TYPE_SCIENCE_VESSEL, KP_TECH_DEFENSIVE_MATRIX, KP_TECH_IRRADIATE, KP_TECH_EMP_SHOCKWAVE, KP_TECH_NONE },
	{ KP_TYPE_GHOST, KP_TECH_LOCKDOWN, KP_TECH_PERSONNEL_CLOAKING, KP_TECH_NONE },
	{ KP_TYPE_VULTURE, KP_TECH_SPIDER_MINES, KP_TECH_NONE },
	{ KP_TYPE_SIEGE_TANK, KP_TECH_TANK_SIEGE_MODE, KP_TECH_NONE },
	{ KP_TYPE_SIEGE_TANK_SIEGED, KP_TECH_TANK_SIEGE_MODE, KP_TECH_NONE },
	{ KP_TYPE_WRAITH, KP_TECH_CLOAKING_FIELD, KP_TECH_NONE },
	{ KP_TYPE_BATTLECRUISER, KP_TECH_YAMATO_GUN, KP_TECH_NONE },
	{ KP_TYPE_QUEEN, KP_TECH_INFESTATION, KP_TECH_PARASITE, KP_TECH_SPAWN_BROODLINGS, KP_TECH_ENSNARE, KP_TECH_NONE },
	{ KP_TYPE_ZERGLING, KP_TECH_BURROWING, KP_TECH_NONE },
	{ KP_TYPE_HYDRALISK, KP_TECH_BURROWING, KP_TECH_NONE },
	{ KP_TYPE_DRONE, KP_TECH_BURROWING, KP_TECH_NONE },
	{ KP_TYPE_INFESTED_TERRAN, KP_TECH_BURROWING, KP_TECH_NONE },
	{ KP_TYPE_DEFILER, KP_TECH_BURROWING, KP_TECH_DARK_SWARM, KP_TECH_PLAGUE, KP_TECH_CONSUME, KP_TECH_NONE },
	{ KP_TYPE_HIGH_TEMPLAR, KP_TECH_ARCHON_WARP, KP_TECH_PSIONIC_STORM, KP_TECH_HALLUCINATION, KP_TECH_NONE },
	{ KP_TYPE_ARBITER, KP_TECH_RECALL, KP_TECH_STASIS_FIELD, KP_TECH_NONE },
	{ KP_TYPE_MEDIC, KP_TECH_HEALING, KP_TECH_RESTORATION, KP_TECH_OPTICAL_FLARE, KP_TECH_NONE },
	{ KP_TYPE_CORSAIR, KP_TECH_DISRUPTION_WEB, KP_TECH_NONE },
	{ KP_TYPE_DARK_TEMPLAR, KP_TECH_DARK_ARCHON_MELD, KP_TECH_NONE },
	{ KP_TYPE_DARK_ARCHON, KP_TECH_MIND_CONTROL, KP_TECH_FEEDBACK, KP_TECH_MAELSTROM, KP_TECH_NONE },
	{ KP_TYPE_LURKER, KP_TECH_LURKER_ASPECT, KP_TECH_NONE },
	{ KP_TYPE_HYDRALISK, KP_TECH_LURKER_ASPECT, KP_TECH_NONE },
};

/* The upgrade recipes of the unit types (0x4f0a58): a type, then the upgrades it uses, ending at 61. */
static const int16_t UPGRADE_RECIPES[][5] = {
	{ KP_TYPE_MARINE, KP_UPG_U_238_SHELLS, KP_UPG_NONE },
	{ KP_TYPE_VULTURE, KP_UPG_ION_THRUSTERS, KP_UPG_NONE },
	{ KP_TYPE_SCIENCE_VESSEL, KP_UPG_TITAN_REACTOR, KP_UPG_NONE },
	{ KP_TYPE_GHOST, KP_UPG_OCULAR_IMPLANTS, KP_UPG_MOEBIUS_REACTOR, KP_UPG_NONE },
	{ KP_TYPE_WRAITH, KP_UPG_APOLLO_REACTOR, KP_UPG_NONE },
	{ KP_TYPE_BATTLECRUISER, KP_UPG_COLOSSUS_REACTOR, KP_UPG_NONE },
	{ KP_TYPE_OVERLORD, KP_UPG_VENTRAL_SACS, KP_UPG_ANTENNAE, KP_UPG_PNEUMATIZED_CARAPACE, KP_UPG_NONE },
	{ KP_TYPE_ZERGLING, KP_UPG_METABOLIC_BOOST, KP_UPG_ADRENAL_GLANDS, KP_UPG_NONE },
	{ KP_TYPE_HYDRALISK, KP_UPG_MUSCULAR_AUGMENTS, KP_UPG_GROOVED_SPINES, KP_UPG_NONE },
	{ KP_TYPE_QUEEN, KP_UPG_GAMETE_MEIOSIS, KP_UPG_NONE },
	{ KP_TYPE_DEFILER, KP_UPG_METASYNAPTIC_NODE, KP_UPG_NONE },
	{ KP_TYPE_DRAGOON, KP_UPG_SINGULARITY_CHARGE, KP_UPG_NONE },
	{ KP_TYPE_ZEALOT, KP_UPG_LEG_ENHANCEMENTS, KP_UPG_NONE },
	{ KP_TYPE_REAVER, KP_UPG_SCARAB_DAMAGE, KP_UPG_REAVER_CAPACITY, KP_UPG_NONE },
	{ KP_TYPE_SHUTTLE, KP_UPG_GRAVITIC_DRIVE, KP_UPG_NONE },
	{ KP_TYPE_OBSERVER, KP_UPG_SENSOR_ARRAY, KP_UPG_GRAVITIC_BOOSTERS, KP_UPG_NONE },
	{ KP_TYPE_HIGH_TEMPLAR, KP_UPG_KHAYDARIN_AMULET, KP_UPG_NONE },
	{ KP_TYPE_SCOUT, KP_UPG_APIAL_SENSORS, KP_UPG_GRAVITIC_THRUSTERS, KP_UPG_NONE },
	{ KP_TYPE_CARRIER, KP_UPG_CARRIER_CAPACITY, KP_UPG_NONE },
	{ KP_TYPE_ARBITER, KP_UPG_KHAYDARIN_CORE, KP_UPG_NONE },
	{ KP_TYPE_GOLIATH, KP_UPG_CHARON_BOOSTERS, KP_UPG_NONE },
	{ KP_TYPE_ULTRALISK, KP_UPG_ANABOLIC_SYNTHESIS, KP_UPG_CHITINOUS_PLATING, KP_UPG_NONE },
	{ KP_TYPE_MEDIC, KP_UPG_CADUCEUS_REACTOR, KP_UPG_NONE },
	{ KP_TYPE_DARK_ARCHON, KP_UPG_ARGUS_TALISMAN, KP_UPG_NONE },
	{ KP_TYPE_CORSAIR, KP_UPG_ARGUS_JEWEL, KP_UPG_NONE },
};

#define RECIPE_TECHS 5
#define RECIPE_UPGRADES 4
#define DAT_SINGLE_ENTITY 0x800
#define STATUS_LOADED 0x40
#define STATUS_ADRENAL 0x20000000

/* ApplyUpgradeToExistingUnits 0x43b0b0: the speed (or the Adrenal Glands cooldown) of upgrade on owner's units. */
void kp_apply_upgrade_to_units(KpGame *g, int64_t owner, int64_t upgrade)
{
	KpUnits *u = &g->u;
	int64_t a = -1, b = -1, m;
	int speed = 1, adrenal = 0;
	switch (upgrade) {
	case KP_UPG_ION_THRUSTERS: a = KP_TYPE_VULTURE; b = KP_TYPE_JIM_RAYNOR_VULTURE; break;
	case KP_UPG_PNEUMATIZED_CARAPACE: a = KP_TYPE_OVERLORD; break;
	case KP_UPG_METABOLIC_BOOST: a = KP_TYPE_ZERGLING; break;
	case KP_UPG_ADRENAL_GLANDS: a = KP_TYPE_ZERGLING; speed = 0; adrenal = 1; break;
	case KP_UPG_MUSCULAR_AUGMENTS: a = KP_TYPE_HYDRALISK; break;
	case KP_UPG_LEG_ENHANCEMENTS: a = KP_TYPE_ZEALOT; break;
	case KP_UPG_GRAVITIC_DRIVE: a = KP_TYPE_SHUTTLE; break;
	case KP_UPG_GRAVITIC_BOOSTERS: a = KP_TYPE_OBSERVER; break;
	case KP_UPG_GRAVITIC_THRUSTERS: a = KP_TYPE_SCOUT; break;
	case KP_UPG_ANABOLIC_SYNTHESIS: a = KP_TYPE_ULTRALISK; b = KP_TYPE_TORRASQUE; break;
	default: return;
	}
	for (m = g->player_units[owner]; m != 0; m = u->player_next[m]) {
		if (u->type[m] != a && u->type[m] != b) continue;
		if (adrenal) u->status[m] = kp_bor(u->status[m], STATUS_ADRENAL);
		if (speed) kp_set_speed_upgrade(g, m);
	}
}

#define STATUS_HALLUCINATED 0x40000000
#define DAT_ADDON 0x2

/* IsConstructingAddon 0x435990: a grounded building whose add-on (+0xec) is not complete yet. */
int kp_constructing_addon(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	return u->secondary_order[n] == KP_ORD_BUILD_ADDON && kp_band(u->status[n], STATUS_GROUNDED_BUILDING) != 0
		&& u->build_unit[n] != 0 && kp_band(u->status[u->build_unit[n]], STATUS_COMPLETED) == 0;
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
		for (k = 1; k <= RECIPE_TECHS && TECH_RECIPES[r][k] != KP_TECH_NONE; k++) {
			int64_t id = TECH_RECIPES[r][k];
			if (kp_tech_researched(g, old, id) && !kp_tech_researched(g, owner, id)) g->players.tech_researched[owner][id] = 1;
		}
	}
	/* ForEachUnitUpgradeRecipe 0x4bbd40 with CopyUpgradeToRecipientCallback 0x4294d0 */
	for (r = sizeof UPGRADE_RECIPES / sizeof UPGRADE_RECIPES[0]; r-- > 0;) {
		if (UPGRADE_RECIPES[r][0] != t) continue;
		for (k = 1; k <= RECIPE_UPGRADES && UPGRADE_RECIPES[r][k] != KP_UPG_NONE; k++) {
			int64_t id = UPGRADE_RECIPES[r][k], level = kp_upgrade_level(g, old, id);
			if (level > kp_upgrade_level(g, owner, id)) g->players.upgrades[owner][id] = level;
		}
	}
	kp_remove_from_selections(g, n);
	kp_remove_from_local_selection(g, n);
	if (kp_band(u->status[n], STATUS_GROUNDED_BUILDING) != 0) {
		if (kp_queued_type(g, n) <= KP_TYPE_LAST_UNIT) {
			kp_cancel_all_queued(g, n); /* 0x41b5c0 */
			kp_sprite_play(g->w, kp_unit_sprite(g, n), KP_ANIM_WORKING_TO_IDLE, 1);
		}
		kp_refund_research(g, n, 1); /* 0x43ab80 */
		kp_refund_upgrade(g, n, 1);  /* 0x43abe0 */
	}
	if (kp_hangar_child_type(t) != 0) kp_cancel_all_queued(g, n);
	kp_change_owner(g, n, owner, 1);
	if (owner < 8) kp_set_sprite_owner(g, n, owner);
	/* ForEachUnitUpgradeRecipe with ApplyUpgradeEffectCallback 0x429500 */
	for (r = sizeof UPGRADE_RECIPES / sizeof UPGRADE_RECIPES[0]; r-- > 0;) {
		if (UPGRADE_RECIPES[r][0] != t) continue;
		for (k = 1; k <= RECIPE_UPGRADES && UPGRADE_RECIPES[r][k] != KP_UPG_NONE; k++) {
			if (kp_upgrade_level(g, owner, UPGRADE_RECIPES[r][k]) != 0) kp_apply_upgrade_to_units(g, owner, UPGRADE_RECIPES[r][k]);
		}
	}
	if (kp_constructing_addon(g, n) || kp_band(u->status[n], STATUS_COMPLETED) == 0
		|| kp_band(KP_DAT(g, units, special_ability_flags, t), DAT_SINGLE_ENTITY) != 0
		|| t == KP_TYPE_INTERCEPTOR || t == KP_TYPE_SCARAB || t == KP_TYPE_NUCLEAR_MISSILE || kp_band(u->status[n], STATUS_LOADED) != 0) {
		return;
	}
	ctl = kp_controller(g, owner);
	if (ctl == KP_CTRL_COMPUTER) kp_issue_order(g, n, KP_DAT(g, units, comp_ai_idle, t));
	else if (ctl == KP_CTRL_RESCUE) kp_issue_order(g, n, KP_ORD_RESCUE_PASSIVE);
	else if (ctl == KP_CTRL_NEUTRAL) kp_issue_order(g, n, KP_ORD_NEUTRAL);
	else kp_issue_order(g, n, KP_DAT(g, units, human_ai_idle, t));
}

/* GetUnitUnderConstruction 0x41b330 of a grounded building: the unit it trains (secondary orders 38 and 63) or the
 * add-on it builds. */
static int64_t building_child(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t o = u->order[n], target = u->order_unit[n];
	if (o == KP_ORD_PLACE_BUILDING || o == KP_ORD_CONSTRUCTING || o == KP_ORD_REPAIR) return target;
	if (o == KP_ORD_PLACE_PROTOSS_BUILDING && kp_band(u->status[n], STATUS_GROUNDED_BUILDING) != 0 && target != 0
		&& kp_band(u->status[target], STATUS_COMPLETED) == 0) {
		return target;
	}
	if (u->secondary_order[n] == KP_ORD_TRAIN || u->secondary_order[n] == KP_ORD_TRAIN_FIGHTER
		|| kp_constructing_addon(g, n)) {
		return u->build_unit[n];
	}
	return 0;
}

/* FindWorkerInsideGasBuilding 0x423f90: the owner's first hidden worker harvesting inside gas building n, or 0. */
static int64_t worker_inside(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t m;
	for (m = g->player_units[u->owner[n]]; m != 0; m = u->player_next[m]) {
		/* a worker inside the gas building */
		if (u->order[m] == KP_ORD_HARVEST_GAS && kp_band(KP_DAT(g, units, special_ability_flags, u->type[m]), DAT_WORKER) != 0
			&& kp_band(g->w->spr.flags[kp_unit_sprite(g, m)], KP_SF_HIDDEN) != 0 && u->order_unit[m] == n) {
			return m;
		}
	}
	return 0;
}

/* TriggerGiveUnit 0x4b81a0 (Give Units to Player, Mind Control): unit n and what belongs to it go to player `to`
 * (Current Player resolved); 1 when given. */
int kp_give_unit_checked(KpGame *g, int64_t n, int64_t to)
{
	KpUnits *u = &g->u;
	int64_t t = u->type[n], c;
	if (to == KP_SEL_CURRENT) to = g->trig.player;
	if (to < 0 || to >= KP_PLAYERS || kp_band(u->status[n], STATUS_HALLUCINATED) != 0 || kp_resource_type(t)
		|| t == KP_TYPE_INTERCEPTOR || t == KP_TYPE_SCARAB || kp_band(KP_DAT(g, units, special_ability_flags, t), DAT_ADDON) != 0
		|| u->owner[n] == to) {
		return 0;
	}
	if (kp_band(u->status[n], STATUS_COMPLETED) != 0 && kp_gas_building(t)) { /* IsCompletedGasBuilding 0x424300 */
		c = worker_inside(g, n);
		if (c != 0) kp_give_unit(g, c, to);
	}
	kp_give_unit(g, n, to);
	if (kp_is_transport(g, n)) { /* ForEachCargoUnit 0x437da0 with GiveChildCallback 0x4b82c0 */
		int64_t cargo[KP_CARGO_SLOTS];
		int count = kp_cargo_units(g, n, cargo), k;
		for (k = 0; k < count; k++) kp_give_unit(g, cargo[k], to);
	}
	if (kp_hangar_child_type(t) != 0) { /* ForEachHangarChild 0x41ca90: the counts bound the walks */
		int64_t k, count = kp_band(u->union_0xc8[n], 0xff);
		for (c = u->union_0xc0[n], k = 0; k < count && c != 0; k++, c = u->union_0xc4[c]) kp_give_unit(g, c, to);
		count = kp_band(kp_shrv(u->union_0xc8[n], 8), 0xff);
		for (c = u->union_0xc4[n], k = 0; k < count && c != 0; k++, c = u->union_0xc4[c]) kp_give_unit(g, c, to);
	}
	if (kp_band(u->status[n], STATUS_GROUNDED_BUILDING) != 0) {
		if (u->union_0xc0[n] != 0) kp_give_unit(g, u->union_0xc0[n], to);
		if (kp_constructing_addon(g, n)) {
			c = building_child(g, n);
			if (c != 0) kp_give_unit(g, c, to);
		}
		if (t == KP_TYPE_NYDUS_CANAL && u->union_0xd0[n] != 0) kp_give_unit(g, u->union_0xd0[n], to);
	}
	return 1;
}
