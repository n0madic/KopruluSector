/* The command card's button conditions (plan phase 9C; statcmd.cpp, atlas P35 0x49f410..0x4a0490, the replay's
 * 0x480ea0..0x480f90): whether a button record of the card shows, hidden (0), disabled (< 0, the requirement
 * evaluators leave their error in KP_REQUIREMENT_ERROR 0x6be654) or enabled (> 0). Every condition is the original's
 * function at its address: CL the record's condition value, EDX the player, the stack argument the status unit. Most
 * read the console's selection snapshot (0x6ab4c8, all twelve slots in order whatever the count 0x6ab4c0); the
 * requirement checks are kp_requirements.c's (CanIssueOrder 0x407bb0, CanResearchTech 0x407dc0, CanUseTech 0x407f90,
 * CanUpgrade 0x408140, CanTrainUnit 0x4b32a0). The conditions read the game and never write it, but for the error. */
#include "kp_ui.h"

#include <stddef.h>

#include "kp_game.h"
#include "kp_ids.h"

/* units.dat special ability flags, statuses, the carried resource (+0xe0) */
#define FLAG_BURROWABLE 0x100000
#define STATUS_COMPLETED 0x1
#define STATUS_GROUNDED_BUILDING 0x2
#define STATUS_BURROWED 0x10
#define STATUS_CLOAKED 0x100
#define STATUS_FREE_CLOAK 0x800    /* cloaked by an Arbiter (RequiresDetection without the energy) */
#define CARRIED_RESOURCE 0x3
/* GetUnitRightClickAction 0x422d90's classes: none, and the burrowed Lurker's */
#define ACTION_NONE 0
#define ACTION_BURROWED_LURKER 3

/* the build menus' types kp_ids.h does not name */
#define TYPE_ACADEMY 112
#define TYPE_ENGINEERING_BAY 122
#define TYPE_ARMORY 123
#define TYPE_HYDRALISK_DEN 135
#define TYPE_DEFILER_MOUND 136
#define TYPE_QUEENS_NEST 138
#define TYPE_EVOLUTION_CHAMBER 139
#define TYPE_ULTRALISK_CAVERN 140
#define TYPE_SPAWNING_POOL 142
#define TYPE_OBSERVATORY 159
#define TYPE_CITADEL_OF_ADUN 163
#define TYPE_CYBERNETICS_CORE 164
#define TYPE_TEMPLAR_ARCHIVES 165
#define TYPE_FORGE 166
#define TYPE_FLEET_BEACON 169
#define TYPE_ARBITER_TRIBUNAL 170
#define TYPE_ROBOTICS_SUPPORT_BAY 171

/* The condition addresses. */
enum {
	REPLAY_SLOWER = 0x480ea0, REPLAY_FASTER = 0x480f30, REPLAY_PAUSE = 0x480f90,
	ALWAYS = 0x49f410, CAN_TRAIN_UNIT = 0x49f420, CAN_TRAIN_HANGAR_UNIT = 0x49f460, ANY_MOVABLE = 0x49f4c0,
	ANY_COMMANDABLE = 0x49f500, ANY_CAN_ATTACK = 0x49f530, ANY_ACTIVE_ARMED = 0x49f570, QUEUE_HEAD_IS_UNIT = 0x49f5d0,
	CONSTRUCTING_ADDON = 0x49f5f0, IS_HATCHERY_TIER = 0x49f600, CAN_SET_RALLY = 0x49f640,
	HATCHERY_CAN_SET_RALLY = 0x49f650, INCOMPLETE = 0x49f690, IS_UPGRADING = 0x49f6b0, IS_RESEARCHING = 0x49f6d0,
	ANY_NOT_CARRYING = 0x49f6f0, ANY_CARRYING = 0x49f720, ANY_TRANSPORT_WITH_SPACE = 0x49f750,
	ANY_HAS_CARGO = 0x49f790, CAN_RESEARCH = 0x49f7c0, CAN_USE_TECH = 0x49f7d0, ANY_CAN_USE_TECH = 0x49f7e0,
	CAN_UPGRADE = 0x49f840, CLOAK_VISIBLE = 0x49f850, DECLOAK_VISIBLE = 0x49f8c0, ANY_CAN_CLOAK = 0x49f930,
	ALL_CAN_DECLOAK = 0x49f9a0, SIEGE_VISIBLE = 0x49fa10, UNSIEGE_VISIBLE = 0x49fa60, NONE_SIEGED = 0x49fab0,
	NUKE_READY = 0x49fae0, NOT_DISABLED = 0x49fb00, IS_LIFTED = 0x49fb40, CAN_LAND = 0x49fb60,
	CAN_LIFT_OFF = 0x49fb90, BURROW_VISIBLE = 0x49fbf0, UNBURROW_VISIBLE = 0x49fc70, CAN_MORPH_LURKER = 0x49fce0,
	NONE_BURROWED = 0x49fcf0, NONE_BURROWED_CAN_ATTACK = 0x49fd20, DRONE_CAN_GATHER = 0x49fd70,
	DRONE_CAN_RETURN = 0x49fdc0, SAME_BURROW_STATE = 0x49fe10, NO_NYDUS_EXIT = 0x49fe60,
	DRONE_BASIC_BUILD_MENU = 0x49fe80, DRONE_ADVANCED_BUILD_MENU = 0x49ff20, CARRIER_HAS_INTERCEPTORS = 0x49ffb0,
	REAVER_HAS_SCARABS = 0x49fff0, TECH_DISABLED_PLACEHOLDER = 0x4a0030, ARCHON_WARP = 0x4a0050,
	DARK_ARCHON_MELD = 0x4a0090, PROBE_BASIC_BUILD_MENU = 0x4a00d0, PROBE_ADVANCED_BUILD_MENU = 0x4a0180,
	ALL_CONSTRUCTING = 0x4a0230, ANY_NOT_CONSTRUCTING = 0x4a0260, SCV_CAN_GATHER = 0x4a0290,
	SCV_CAN_RETURN = 0x4a02f0, SCV_BASIC_BUILD_MENU = 0x4a0350, SCV_ADVANCED_BUILD_MENU = 0x4a0410,
	SINGLE_CAN_TRAIN = 0x4a0490
};

/* The build menus' types in the order the conditions test them. */
static const int64_t DRONE_BASIC[] = { KP_TYPE_HATCHERY, KP_TYPE_CREEP_COLONY, KP_TYPE_EXTRACTOR, TYPE_SPAWNING_POOL,
	TYPE_EVOLUTION_CHAMBER, TYPE_HYDRALISK_DEN };
static const int64_t DRONE_ADVANCED[] = { KP_TYPE_NYDUS_CANAL, KP_TYPE_SPIRE, TYPE_QUEENS_NEST, TYPE_ULTRALISK_CAVERN,
	TYPE_DEFILER_MOUND };
static const int64_t PROBE_BASIC[] = { KP_TYPE_NEXUS, KP_TYPE_PYLON, KP_TYPE_ASSIMILATOR, KP_TYPE_GATEWAY, TYPE_FORGE,
	KP_TYPE_PHOTON_CANNON, TYPE_CYBERNETICS_CORE, KP_TYPE_SHIELD_BATTERY };
static const int64_t PROBE_ADVANCED[] = { KP_TYPE_ROBOTICS_FACILITY, KP_TYPE_STARGATE, TYPE_CITADEL_OF_ADUN,
	TYPE_ROBOTICS_SUPPORT_BAY, TYPE_FLEET_BEACON, TYPE_TEMPLAR_ARCHIVES, TYPE_OBSERVATORY, TYPE_ARBITER_TRIBUNAL };
static const int64_t SCV_BASIC[] = { KP_TYPE_COMMAND_CENTER, KP_TYPE_SUPPLY_DEPOT, KP_TYPE_REFINERY, KP_TYPE_BARRACKS,
	TYPE_ENGINEERING_BAY, KP_TYPE_MISSILE_TURRET, TYPE_ACADEMY, KP_TYPE_BUNKER };
static const int64_t SCV_ADVANCED[] = { KP_TYPE_FACTORY, KP_TYPE_STARPORT, KP_TYPE_SCIENCE_FACILITY, TYPE_ARMORY };
#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

typedef int (*UnitTest)(KpGame *g, int64_t n);

static int64_t status(KpGame *g, int64_t n) { return g->u.status[n]; }
static int burrowed(KpGame *g, int64_t n) { return (status(g, n) & STATUS_BURROWED) != 0; }
static int not_constructing(KpGame *g, int64_t n) { return (g->u.order[n] & 0xff) != KP_ORD_CONSTRUCTING; }
static int carrying(KpGame *g, int64_t n) { return (g->u.resource_type[n] & CARRIED_RESOURCE) != 0; }
static int not_carrying(KpGame *g, int64_t n) { return !carrying(g, n); }
static int researching(KpGame *g, int64_t n) { return (g->u.union_0xc8[n] & 0xff) != KP_TECH_NONE; } /* +0xc8 */
static int upgrading(KpGame *g, int64_t n) { return ((g->u.union_0xc8[n] >> 8) & 0xff) != KP_UPG_NONE; } /* +0xc9 */

static int hatchery_tier(KpGame *g, int64_t n)
{
	int64_t t = g->u.type[n];
	return t == KP_TYPE_HATCHERY || t == KP_TYPE_LAIR || t == KP_TYPE_HIVE;
}

static int sieged(KpGame *g, int64_t n)
{
	int64_t t = g->u.type[n];
	return t == KP_TYPE_SIEGE_TANK_SIEGED || t == KP_TYPE_EDMUND_DUKE_SIEGED;
}

static int burrowable(KpGame *g, int64_t n)
{
	return (KP_DAT(g, units, special_ability_flags, g->u.type[n]) & FLAG_BURROWABLE) != 0;
}

/* IsCloakingForFree 0x4a04e0, inlined by the cloak conditions. */
static int cloaking_for_free(KpGame *g, int64_t n)
{
	return (status(g, n) & STATUS_FREE_CLOAK) != 0 && !burrowed(g, n);
}

static int cloaked(KpGame *g, int64_t n) { return (status(g, n) & STATUS_CLOAKED) != 0; }

static int movable(KpGame *g, int64_t n)
{
	int64_t action = kp_right_click_action(g, n);
	return action != ACTION_NONE && action != ACTION_BURROWED_LURKER;
}

static int commandable(KpGame *g, int64_t n) { return kp_right_click_action(g, n) != ACTION_NONE; }
static int can_attack(KpGame *g, int64_t n) { return commandable(g, n) && kp_unit_has_weapon(g, n); }
static int active_armed(KpGame *g, int64_t n) { return !kp_unit_disabled(g, n) && kp_unit_has_weapon(g, n); }

/* TransportHasFreeSpace 0x4382f0: its passengers' space below the space it provides. */
static int free_space(KpGame *g, int64_t n)
{
	return kp_cargo_space(g, n) < KP_DAT(g, units, space_provided, g->u.type[n]);
}

/* Carrier with interceptors (+0xc8 + +0xc9 as a byte), Reaver with scarabs (+0xc8). */
static int armed_carrier(KpGame *g, int64_t n)
{
	int64_t t = g->u.type[n], h = g->u.union_0xc8[n];
	return (t == KP_TYPE_CARRIER || t == KP_TYPE_GANTRITHOR) && ((h + (h >> 8)) & 0xff) != 0;
}

static int armed_reaver(KpGame *g, int64_t n)
{
	int64_t t = g->u.type[n];
	return (t == KP_TYPE_REAVER || t == KP_TYPE_WARBRINGER) && (g->u.union_0xc8[n] & 0xff) != 0;
}

/* Whether a unit of the selection (all twelve slots) passes the test. */
static int any_selected(KpUi *ui, UnitTest test)
{
	int k;
	for (k = 0; k < KP_UI_SLOTS; k++) {
		int64_t n = ui->hud.selection[k];
		if (n != 0 && test(ui->game, n)) return 1;
	}
	return 0;
}

/* The build menus: some type of the list CanTrainUnit 0x4b32a0 does not refuse outright (1 or -1). */
static int any_trainable(KpGame *g, const int64_t *types, int count, int64_t player, int64_t n)
{
	int k;
	for (k = 0; k < count; k++) {
		if (kp_can_train_unit(g, types[k], player, n) != 0) return 1;
	}
	return 0;
}

/* BtnCondCanTrainHangarUnit 0x49f460: every selected unit the status unit's type; the first non-zero result. */
static int64_t can_train_hangar_unit(KpUi *ui, int64_t t, int64_t player, int64_t lead)
{
	KpGame *g = ui->game;
	int64_t result = 0;
	int k;
	for (k = 0; k < KP_UI_SLOTS; k++) {
		int64_t n = ui->hud.selection[k];
		if (n == 0) continue;
		if (g->u.type[n] != g->u.type[lead]) return 0;
		if (result == 0) result = kp_can_train_unit(g, t, player, n);
	}
	return result;
}

/* BtnCondAnyTransportWithSpace 0x49f750: a unit that is no transport (IsTransport 0x4381f0) ends the scan. */
static int any_transport_with_space(KpUi *ui)
{
	int k;
	for (k = 0; k < KP_UI_SLOTS; k++) {
		int64_t n = ui->hud.selection[k];
		if (n == 0) continue;
		if (!kp_is_transport(ui->game, n)) return 0;
		if (free_space(ui->game, n)) return 1;
	}
	return 0;
}

/* BtnCondAnyCanUseTech 0x49f7e0: 1 at a unit that may use it, else the least of 0 and the results. */
static int64_t any_can_use_tech(KpUi *ui, int64_t tech, int64_t player)
{
	int64_t least = 0, r;
	int k;
	for (k = 0; k < KP_UI_SLOTS; k++) {
		int64_t n = ui->hud.selection[k];
		if (n == 0) continue;
		r = kp_can_use_tech(ui->game, tech, player, n);
		if (r == 1) return 1;
		if (r < least) least = r;
	}
	return least;
}

/* BtnCondCloakVisible 0x49f850 (tech_of_unit 0: the record's tech, any refusal returned) and BtnCondAnyCanCloak
 * 0x49f930 (each unit's GetCloakTech 0x434900, a refusal skips the unit): a unit not cloaked that does not cloak for
 * free. */
static int64_t cloak_visible(KpUi *ui, int64_t tech, int64_t player, int tech_of_unit)
{
	KpGame *g = ui->game;
	int64_t r;
	int k;
	for (k = 0; k < KP_UI_SLOTS; k++) {
		int64_t n = ui->hud.selection[k];
		if (n == 0) continue;
		r = kp_can_use_tech(g, tech_of_unit ? kp_cloak_tech(g, n) : tech, player, n);
		if (r != 1) {
			if (tech_of_unit) continue;
			return r;
		}
		if (cloaking_for_free(g, n)) continue;
		if (!cloaked(g, n)) return 1;
	}
	return 0;
}

/* BtnCondDecloakVisible 0x49f8c0 and BtnCondAllCanDecloak 0x49f9a0 (each unit's cloak tech): every selected unit may
 * use it, does not cloak for free and is cloaked. */
static int all_can_decloak(KpUi *ui, int64_t tech, int64_t player, int tech_of_unit)
{
	KpGame *g = ui->game;
	int k;
	for (k = 0; k < KP_UI_SLOTS; k++) {
		int64_t n = ui->hud.selection[k];
		if (n == 0) continue;
		if (kp_can_use_tech(g, tech_of_unit ? kp_cloak_tech(g, n) : tech, player, n) != 1 || cloaking_for_free(g, n)
			|| !cloaked(g, n)) return 0;
	}
	return 1;
}

/* BtnCondBurrowVisible 0x49fbf0: over the burrowable units, a refusal is the result; one not burrowed nor burrowing
 * or unburrowing (+0x4d 116, 118) shows it. */
static int64_t burrow_visible(KpUi *ui, int64_t tech, int64_t player)
{
	KpGame *g = ui->game;
	int64_t r, order;
	int k;
	for (k = 0; k < KP_UI_SLOTS; k++) {
		int64_t n = ui->hud.selection[k];
		if (n == 0 || !burrowable(g, n)) continue;
		r = kp_can_use_tech(g, tech, player, n);
		if (r != 1) return r;
		order = g->u.order[n] & 0xff;
		if (!burrowed(g, n) && order != KP_ORD_BURROWING && order != KP_ORD_UNBURROWING) return 1;
	}
	return 0;
}

/* BtnCondUnburrowVisible 0x49fc70: every burrowable unit burrowed, not in a burrow order, and may use the tech. */
static int unburrow_visible(KpUi *ui, int64_t tech, int64_t player)
{
	KpGame *g = ui->game;
	int64_t order;
	int k;
	for (k = 0; k < KP_UI_SLOTS; k++) {
		int64_t n = ui->hud.selection[k];
		if (n == 0 || !burrowable(g, n)) continue;
		order = g->u.order[n] & 0xff;
		if (!burrowed(g, n) || order == KP_ORD_BURROWING || order == KP_ORD_UNBURROWING) return 0;
		if (kp_can_use_tech(g, tech, player, n) != 1) return 0;
	}
	return 1;
}

/* BtnCondSameBurrowState 0x49fe10: against the console's status unit (0x6ab4c4), not the argument. */
static int same_burrow_state(KpUi *ui)
{
	KpGame *g = ui->game;
	int lead = burrowed(g, ui->hud.status_unit), k;
	for (k = 0; k < KP_UI_SLOTS; k++) {
		int64_t n = ui->hud.selection[k];
		if (n != 0 && burrowed(g, n) != lead) return 0;
	}
	return 1;
}

/* BtnCondArchonWarp 0x4a0050, BtnCondDarkArchonMeld 0x4a0090: a single unit cannot merge, shown disabled. */
static int64_t merge(KpUi *ui, int64_t tech, int64_t player, int64_t n)
{
	if (kp_can_use_tech(ui->game, tech, player, n) != 1) return 0;
	return (uint8_t)ui->hud.selection_count == 1 ? -1 : 1;
}

int kp_ui_button_condition(KpUi *ui, uint32_t address, uint8_t value, int64_t player, int64_t status_unit)
{
	KpGame *g = ui->game;
	int64_t n = status_unit, t;
	int single = (uint8_t)ui->hud.selection_count == 1; /* the conditions compare the count's byte */
	if (address == ALWAYS) return 1; /* BtnCondAlways 0x49f410 */
	if (address == REPLAY_SLOWER || address == REPLAY_FASTER || address == REPLAY_PAUSE) {
		/* the replay's buttons, with or without a status unit: 0 outside a replay (0x67cc90); a single player is the
		 * lowest active one (IsLowestActivePlayer 0x477800) */
		int64_t speed, m;
		if (g == NULL || !g->w->replay) return 0;
		if (address == REPLAY_PAUSE) return 1; /* ReplayButtonPauseState 0x480f90 */
		speed = kp_scalar(g, KP_SPEED);
		m = g->replay_multiplier;
		if (g->replay_paused) return -1;
		if (address == REPLAY_SLOWER) return speed == 0 && m >= 1 ? -1 : 1; /* ReplayButtonSlowerState 0x480ea0 */
		return speed == 6 && m >= 16 ? -1 : 1;                             /* ReplayButtonFasterState 0x480f30 */
	}
	/* the original builds no card without a game and a status unit */
	if (g == NULL || n == 0) return 0;
	t = g->u.type[n];
	switch (address) {
	case CAN_TRAIN_UNIT: /* BtnCondCanTrainUnit 0x49f420 */
		if ((uint8_t)ui->hud.selection_count > 1 && t != KP_TYPE_LARVA && t != KP_TYPE_MUTALISK && t != KP_TYPE_HYDRALISK) {
			return 0;
		}
		return (int)kp_can_train_unit(g, value, player, n);
	case CAN_TRAIN_HANGAR_UNIT: return (int)can_train_hangar_unit(ui, value, player, n);
	case ANY_MOVABLE: return any_selected(ui, movable);              /* BtnCondAnyMovable 0x49f4c0 */
	case ANY_COMMANDABLE: return any_selected(ui, commandable);      /* BtnCondAnyCommandable 0x49f500 */
	case ANY_CAN_ATTACK: return any_selected(ui, can_attack);        /* BtnCondAnyCanAttack 0x49f530 */
	case ANY_ACTIVE_ARMED: return any_selected(ui, active_armed);    /* BtnCondAnyActiveArmed 0x49f570 */
	case QUEUE_HEAD_IS_UNIT: return kp_queued_type(g, n) <= KP_TYPE_LAST_UNIT; /* BtnCondQueueHeadIsUnit 0x49f5d0 */
	case CONSTRUCTING_ADDON: return kp_constructing_addon(g, n);     /* BtnCondConstructingAddon 0x49f5f0 */
	case IS_HATCHERY_TIER: return hatchery_tier(g, n);               /* BtnCondIsHatcheryTier 0x49f600 */
	case CAN_SET_RALLY: /* BtnCondCanSetRally 0x49f640 */
		return (int)kp_can_issue_order(g, KP_ORD_RALLY_POINT_TILE, player, n);
	case HATCHERY_CAN_SET_RALLY: /* BtnCondHatcheryCanSetRally 0x49f650 */
		return hatchery_tier(g, n) ? (int)kp_can_issue_order(g, KP_ORD_RALLY_POINT_TILE, player, n) : 0;
	case INCOMPLETE: return (status(g, n) & STATUS_COMPLETED) == 0; /* BtnCondIncomplete 0x49f690 */
	case IS_UPGRADING: return upgrading(g, n);                      /* BtnCondIsUpgrading 0x49f6b0 */
	case IS_RESEARCHING: return researching(g, n);                  /* BtnCondIsResearching 0x49f6d0 */
	case ANY_NOT_CARRYING: return any_selected(ui, not_carrying);    /* BtnCondAnyNotCarrying 0x49f6f0 */
	case ANY_CARRYING: return any_selected(ui, carrying);            /* BtnCondAnyCarrying 0x49f720 */
	case ANY_TRANSPORT_WITH_SPACE: return any_transport_with_space(ui);
	case ANY_HAS_CARGO: return any_selected(ui, kp_has_cargo);       /* BtnCondAnyHasCargo 0x49f790 */
	case CAN_RESEARCH: return (int)kp_can_research(g, value, player, n);  /* BtnCondCanResearch 0x49f7c0 */
	case CAN_USE_TECH: return (int)kp_can_use_tech(g, value, player, n);  /* BtnCondCanUseTech 0x49f7d0 */
	case ANY_CAN_USE_TECH: return (int)any_can_use_tech(ui, value, player);
	case CAN_UPGRADE: return (int)kp_can_upgrade(g, value, player, n);    /* BtnCondCanUpgrade 0x49f840 */
	case CLOAK_VISIBLE: return (int)cloak_visible(ui, value, player, 0);
	case DECLOAK_VISIBLE: return all_can_decloak(ui, value, player, 0);
	case ANY_CAN_CLOAK: return (int)cloak_visible(ui, value, player, 1);
	case ALL_CAN_DECLOAK: return all_can_decloak(ui, value, player, 1);
	case SIEGE_VISIBLE: { /* BtnCondSiegeVisible 0x49fa10 */
		int64_t r = kp_can_use_tech(g, value, player, n);
		if (r != 1) return (int)r;
		return !any_selected(ui, sieged);
	}
	case UNSIEGE_VISIBLE: /* BtnCondUnsiegeVisible 0x49fa60 */
		return kp_can_use_tech(g, value, player, n) == 1 && any_selected(ui, sieged);
	case NONE_SIEGED: return !any_selected(ui, sieged);              /* BtnCondNoneSieged 0x49fab0 */
	case NUKE_READY: return kp_find_ready_silo(g, n, 0, 0) != 0 ? 1 : -1; /* BtnCondNukeReady 0x49fae0 */
	case NOT_DISABLED: return !kp_unit_disabled(g, n);               /* BtnCondNotDisabled 0x49fb00 */
	case IS_LIFTED: return (status(g, n) & STATUS_GROUNDED_BUILDING) == 0; /* BtnCondIsLifted 0x49fb40 */
	case CAN_LAND: return single && (status(g, n) & STATUS_GROUNDED_BUILDING) == 0; /* BtnCondCanLand 0x49fb60 */
	case CAN_LIFT_OFF: /* BtnCondCanLiftOff 0x49fb90: the queue head, 0x435990, 0x42f240, +0xc8 and +0xc9 */
		return (status(g, n) & STATUS_GROUNDED_BUILDING) != 0 && !kp_unit_occupied(g, n);
	case BURROW_VISIBLE: return (int)burrow_visible(ui, value, player);
	case UNBURROW_VISIBLE: return unburrow_visible(ui, value, player);
	case CAN_MORPH_LURKER: /* BtnCondCanMorphLurker 0x49fce0: the record's value is not read */
		return (int)kp_can_use_tech(g, KP_TECH_LURKER_ASPECT, player, n);
	case NONE_BURROWED: return !any_selected(ui, burrowed);          /* BtnCondNoneBurrowed 0x49fcf0 */
	case NONE_BURROWED_CAN_ATTACK: /* BtnCondNoneBurrowedCanAttack 0x49fd20 */
		return !any_selected(ui, burrowed) && any_selected(ui, can_attack);
	case DRONE_CAN_GATHER: /* BtnCondDroneCanGather 0x49fd70 */
		return !any_selected(ui, burrowed) && any_selected(ui, not_carrying);
	case DRONE_CAN_RETURN: /* BtnCondDroneCanReturn 0x49fdc0 */
		return !any_selected(ui, burrowed) && any_selected(ui, carrying);
	case SAME_BURROW_STATE: return same_burrow_state(ui);
	case NO_NYDUS_EXIT: return g->u.union_0xd0[n] == 0;             /* BtnCondNoNydusExit 0x49fe60: +0xd0 */
	case DRONE_BASIC_BUILD_MENU: /* BtnCondDroneBasicBuildMenu 0x49fe80 */
		return single && !burrowed(g, n) && any_trainable(g, DRONE_BASIC, COUNT(DRONE_BASIC), player, n);
	case DRONE_ADVANCED_BUILD_MENU: /* BtnCondDroneAdvancedBuildMenu 0x49ff20 */
		return single && !burrowed(g, n) && any_trainable(g, DRONE_ADVANCED, COUNT(DRONE_ADVANCED), player, n);
	case CARRIER_HAS_INTERCEPTORS: return any_selected(ui, armed_carrier) ? 1 : -1; /* 0x49ffb0 */
	case REAVER_HAS_SCARABS: return any_selected(ui, armed_reaver) ? 1 : -1;        /* 0x49fff0 */
	case TECH_DISABLED_PLACEHOLDER: /* BtnCondTechDisabledPlaceholder 0x4a0030 */
		return kp_can_use_tech(g, value, player, n) == -1 ? -1 : 0;
	case ARCHON_WARP: return (int)merge(ui, KP_TECH_ARCHON_WARP, player, n);
	case DARK_ARCHON_MELD: return (int)merge(ui, KP_TECH_DARK_ARCHON_MELD, player, n);
	case PROBE_BASIC_BUILD_MENU: /* BtnCondProbeBasicBuildMenu 0x4a00d0 */
		return single && any_trainable(g, PROBE_BASIC, COUNT(PROBE_BASIC), player, n);
	case PROBE_ADVANCED_BUILD_MENU: /* BtnCondProbeAdvancedBuildMenu 0x4a0180 */
		return single && any_trainable(g, PROBE_ADVANCED, COUNT(PROBE_ADVANCED), player, n);
	case ALL_CONSTRUCTING: return !any_selected(ui, not_constructing);  /* BtnCondAllConstructing 0x4a0230 */
	case ANY_NOT_CONSTRUCTING: return any_selected(ui, not_constructing); /* BtnCondAnyNotConstructing 0x4a0260 */
	case SCV_CAN_GATHER: /* BtnCondScvCanGather 0x4a0290 */
		return any_selected(ui, not_carrying) && any_selected(ui, not_constructing);
	case SCV_CAN_RETURN: /* BtnCondScvCanReturn 0x4a02f0 */
		return any_selected(ui, carrying) && any_selected(ui, not_constructing);
	case SCV_BASIC_BUILD_MENU: /* BtnCondScvBasicBuildMenu 0x4a0350 */
		return single && any_selected(ui, not_constructing)
			&& any_trainable(g, SCV_BASIC, COUNT(SCV_BASIC), player, n);
	case SCV_ADVANCED_BUILD_MENU: /* BtnCondScvAdvancedBuildMenu 0x4a0410 */
		return single && any_selected(ui, not_constructing)
			&& any_trainable(g, SCV_ADVANCED, COUNT(SCV_ADVANCED), player, n);
	case SINGLE_CAN_TRAIN: return single ? (int)kp_can_train_unit(g, value, player, n) : 0; /* 0x4a0490 */
	}
	return 0; /* not a condition of the button table */
}
