/* The command card's button actions (plan phase 9C; atlas P27 0x47c1b0..0x47ca50, the senders of P25 0x473cc0..0x474560,
 * the targeting mode of P39 0x4b2840..0x4b2900): each button's action as the original's table names it, the commands
 * it queues, the targeting it begins and the building placement it starts. */
#include "kp_ui.h"

#include <string.h>

#include "kp_game.h"
#include "kp_ids.h"

#define SET_TARGETING 229      /* the command card's Cancel set while targeting */
#define SET_PLACEMENT 230
#define MINIMAP_TARGETING 2    /* 0x667288: the minimap's cursor while targeting */
#define STATUS_GROUNDED_BUILDING 0x02
#define DAT_ADDON 0x02
#define PROMPT_TARGET 0x3f     /* network.tbl: the targeting's prompt */
#define PROMPT_PLACE 0x40      /* the placement's */
#define LARVA_REACH 128        /* BtnSelectLarva 0x47c670: the status unit's sprite +- 128 */
#define SLOTS 12
#define STIM_HP (10 << 8)      /* 0x4383d0: more than 10 hit points */
#define STRING_STIM 0x36a
#define STRING_ENERGY 0x360    /* + race: "Not enough energy." */
#define SOUND_ENERGY 0x9c      /* + race */
#define STRING_SELECT_TARGET 0x3f /* network.tbl */
#define STRING_SELECT_PLACE 0x40

/* Orders (orders.dat ids, atlas P27). */
enum {
	O_ATTACK1 = 8, O_ATTACK2 = 9, O_GUARD = 2, O_ATTACK_MOVE = 14, O_TOWER_GUARD = 18, O_TOWER_ATTACK = 19,
	O_DRONE_BUILD = 25, O_INFEST = 27, O_MOVE_INFEST = 28, O_PLACE_BUILDING = 30, O_PLACE_PROTOSS = 31,
	O_REPAIR = 34, O_MOVE_REPAIR = 35, O_PLACE_ADDON = 36, O_RALLY_UNIT = 39, O_RALLY_TILE = 40,
	O_BUILD_NYDUS = 46, O_FOLLOW = 49, O_REAVER_CARRIER_MOVE = 51, O_CARRIER_ATTACK = 53, O_CARRIER_MOVE_ATTACK = 54,
	O_REAVER_ATTACK = 59, O_REAVER_MOVE_ATTACK = 60, O_RECHARGE = 67, O_LAND = 71, O_HARVEST1 = 79, O_HARVEST2 = 80,
	O_PICKUP_TRANSPORT = 94, O_PICKUP_BUNKER = 95, O_MOVE = 6, O_NOTHING = 23, O_MOVE_UNLOAD = 112,
	O_YAMATO = 113, O_MOVE_YAMATO = 114, O_NUKE_PAINT = 126, O_PATROL = 152, O_CTF_COP2 = 155, O_HEAL_MOVE = 177
};

static KpGame *game(KpUi *ui) { return ui->game; }

static void queue(KpUi *ui, const uint8_t *bytes, size_t n) { kp_ui_queue_command(ui, bytes, n); }

static void send1(KpUi *ui, uint8_t id)
{
	queue(ui, &id, 1);
}

static void send2(KpUi *ui, uint8_t id, uint8_t v)
{
	uint8_t b[2];
	b[0] = id;
	b[1] = v;
	queue(ui, b, 2);
}

static void send3(KpUi *ui, uint8_t id, uint16_t v)
{
	uint8_t b[3];
	b[0] = id;
	b[1] = (uint8_t)v;
	b[2] = (uint8_t)(v >> 8);
	queue(ui, b, 3);
}

/* --- targeting and placement --- */

void kp_ui_begin_targeting(KpUi *ui, uint8_t unit_order, uint8_t ground_order, uint8_t fog_order)
{
	KpUiInput *p = &ui->input;
	p->target_orders[0] = unit_order;
	p->target_orders[1] = ground_order;
	p->target_orders[2] = fog_order;
	p->targeting = 1;
	kp_ui_set_textbox_prompt(ui, kp_ui_tbl(ui, KP_UI_NETWORK, PROMPT_TARGET)); /* 0x4b28b0 */
	kp_ui_install_game_input(ui);
	p->minimap_cursor = MINIMAP_TARGETING;
	kp_ui_card_override(ui, SET_TARGETING);
}

static void end_targeting(KpUi *ui)
{
	KpUiInput *p = &ui->input;
	p->targeting = 0;
	kp_ui_set_textbox_prompt(ui, NULL); /* 0x4b2850, 0x4b2900 */
	kp_ui_install_game_input(ui);
	p->minimap_cursor = 0;
	kp_ui_card_override(ui, KP_UI_SET_NONE);
}

uint8_t kp_ui_finish_targeting(KpUi *ui, int64_t unit, uint16_t fog_type)
{
	end_targeting(ui);
	if (unit != 0) return ui->input.target_orders[0];
	return fog_type == KP_TYPE_NONE ? ui->input.target_orders[1] : ui->input.target_orders[2];
}

void kp_ui_cancel_targeting(KpUi *ui) { end_targeting(ui); }

void kp_ui_start_placement(KpUi *ui, uint8_t order, uint16_t type)
{
	KpGame *g = game(ui);
	int64_t parent = ui->hud.status_unit;
	kp_ui_card_override(ui, SET_PLACEMENT);
	kp_placement_start(g, order, type); /* with the world's ghosts of a gas building or a Terran type (0x47d190, 0x47d080) */
	ui->placement.error = 0;
	kp_ui_placement_prepare(ui, 1, type); /* StartPlacement 0x47d3a0: the placed type's preview (0x47cec0(1, type)) */
	/* an add-on's placement checks and shows its parent's place too (0x47cec0(0, parent type)) */
	ui->input.placement_parent = (uint16_t)(parent != 0
		&& (KP_DAT(g, units, special_ability_flags, type) & DAT_ADDON) != 0 ? g->u.type[parent] : 0);
	if (ui->input.placement_parent != 0) {
		kp_ui_placement_prepare(ui, 0, ui->input.placement_parent);
		kp_ui_placement_anchor(ui, parent); /* the preview beside the parent, not at the cursor (a touch has none) */
	}
	kp_ui_set_textbox_prompt(ui, kp_ui_tbl(ui, KP_UI_NETWORK, PROMPT_PLACE)); /* 0x47d3a0 */
	kp_ui_install_game_input(ui);
	kp_ui_placement_update(ui);
	ui->input.minimap_cursor = 0;
}

/* --- the checks --- */

static int64_t status_owner(KpUi *ui) { return game(ui)->u.owner[ui->hud.status_unit]; }

/* CheckUnitCost 0x4086b0 for the status unit's owner, with the supply and the feedback. */
static int affordable(KpUi *ui, uint16_t type) { return kp_check_unit_cost(game(ui), status_owner(ui), type, 1, 1); }

/* The energy error of AnySelectedCanCloak 0x47c200 and AnySelectedHasTechEnergy 0x47c290. */
static int energy_for(KpUi *ui, int tech_of_unit, uint8_t tech)
{
	KpGame *g = game(ui);
	int k;
	for (k = 0; k < SLOTS; k++) {
		int64_t n = ui->hud.selection[k], t;
		if (n == 0) continue;
		t = tech_of_unit ? kp_cloak_tech(g, n) : tech;
		if (!kp_needs_energy(g, n, KP_DAT(g, techdata, energy_required, t) << 8)) return 1;
	}
	{
		int64_t s = ui->hud.status_unit, race = kp_unit_race(g, s);
		kp_ui_error_sound(ui, g->u.owner[s], (uint32_t)(SOUND_ENERGY + race), (uint32_t)(STRING_ENERGY + race)); /* 0x4461f0 */
	}
	return 0;
}

static int any_can_move(KpUi *ui)
{
	int k;
	for (k = 0; k < SLOTS; k++) {
		if (ui->hud.selection[k] != 0 && kp_unit_can_move(game(ui), ui->hud.selection[k])) return 1; /* 0x427e60 */
	}
	return 0;
}

static int grounded_status_building(KpUi *ui)
{
	return (game(ui)->u.status[ui->hud.status_unit] & STATUS_GROUNDED_BUILDING) != 0;
}

/* BtnUseTech 0x47c7b0: the targeting of a targeted spell (techs 5, 9, 10, 11, 23, 26 and 28 do nothing). */
static void use_tech(KpUi *ui, uint16_t tech)
{
	static const uint8_t ORDERS[][4] = { /* tech, orders on a unit, on the ground, on a fogged unit */
		{ 1, 115, 115, 115 }, { 2, 122, 122, 122 }, { 3, 132, 132, 132 }, { 4, 139, 139, 139 }, { 6, 141, 141, 141 },
		{ 7, 143, 143, 143 }, { 8, O_YAMATO, O_YAMATO, O_MOVE_YAMATO }, { 12, O_INFEST, O_MOVE, O_MOVE_INFEST },
		{ 13, 121, 121, 121 }, { 14, 119, 119, 119 }, { 15, 144, 144, 144 }, { 16, 145, 145, 145 },
		{ 17, 146, 146, 146 }, { 18, 120, 120, 120 }, { 19, 142, 142, 142 }, { 20, 148, 148, 148 },
		{ 21, 137, 137, 137 }, { 22, 147, 147, 147 }, { 24, 180, 180, 180 }, { 25, 181, 181, 181 },
		{ 27, 182, 182, 182 }, { 29, 184, 184, 184 }, { 30, 185, 185, 185 }, { 31, 186, 186, 186 }
	};
	size_t k;
	if (!energy_for(ui, 0, (uint8_t)tech)) return;
	for (k = 0; k < sizeof ORDERS / sizeof ORDERS[0]; k++) {
		if (ORDERS[k][0] == tech) {
			kp_ui_begin_targeting(ui, ORDERS[k][1], ORDERS[k][2], ORDERS[k][3]);
			return;
		}
	}
}

/* BtnSelectLarva 0x47c670 with CollectOwnLarvae 0x47c1b0. */
static void select_larva(KpUi *ui)
{
	KpGame *g = game(ui);
	int64_t s = ui->hud.status_unit, *found, list[SLOTS];
	int32_t sp = kp_unit_sprite(g, s);
	int16_t x = (int16_t)g->w->spr.x[sp], y = (int16_t)g->w->spr.y[sp];
	int count, k, n = 0;
	found = kp_query_rect(g, (int16_t)(x - LARVA_REACH), (int16_t)(y - LARVA_REACH), (int16_t)(x + LARVA_REACH),
		(int16_t)(y + LARVA_REACH), &count);
	for (k = 0; k < count && n < SLOTS; k++) {
		int64_t u = found[k];
		if (g->u.type[u] == KP_TYPE_LARVA && g->u.connected[u] == s
			&& !(g->w->spr.flags[kp_unit_sprite(g, u)] & KP_SF_HIDDEN)) {
			list[n++] = u;
		}
	}
	kp_query_release(g, found);
	if (n) kp_ui_select_units(ui, list, n, 1);
}

/* --- the actions --- */

void kp_ui_button_action(KpUi *ui, uint32_t address, uint16_t value, uint8_t shift)
{
	KpGame *g = game(ui);
	int k;
	switch (address) {
	case 0x47c310: kp_ui_begin_targeting(ui, O_ATTACK1, any_can_move(ui) ? O_ATTACK_MOVE : O_GUARD, O_ATTACK2); break;
	case 0x47c360: kp_ui_begin_targeting(ui, O_ATTACK1, O_ATTACK1, O_ATTACK2); break;
	case 0x47c370: { /* BtnLiftOff: SendLiftOffCommand 0x473ef0 */
		int32_t s = kp_unit_sprite(g, ui->hud.status_unit);
		uint8_t b[5];
		b[0] = 0x2f;
		b[1] = (uint8_t)g->w->spr.x[s];
		b[2] = (uint8_t)(g->w->spr.x[s] >> 8);
		b[3] = (uint8_t)g->w->spr.y[s];
		b[4] = (uint8_t)(g->w->spr.y[s] >> 8);
		queue(ui, b, 5);
		break;
	}
	case 0x47c380: send3(ui, 0x1f, value); break;       /* BtnTrain */
	case 0x47c390: send1(ui, 0x27); break;              /* BtnTrainFighter */
	case 0x47c3a0: send2(ui, 0x2c, shift); break;       /* BtnBurrow */
	case 0x47c3b0: send1(ui, 0x34); break;              /* BtnCancelAddon */
	case 0x47c3c0: send1(ui, 0x31); break;              /* BtnCancelResearch */
	case 0x47c3d0: send1(ui, 0x33); break;              /* BtnCancelUpgrade */
	case 0x47c3e0: send1(ui, 0x2e); break;              /* BtnCancelNuke */
	case 0x47c3f0: kp_ui_begin_targeting(ui, O_CARRIER_ATTACK, O_ATTACK_MOVE, O_CARRIER_MOVE_ATTACK); break;
	case 0x47c400: kp_ui_begin_targeting(ui, O_FOLLOW, O_REAVER_CARRIER_MOVE, O_REAVER_CARRIER_MOVE); break;
	case 0x47c410: send1(ui, 0x1b); break;              /* BtnCarrierStop */
	case 0x47c420: if (energy_for(ui, 1, 0)) send2(ui, 0x21, shift); break; /* BtnCloak */
	case 0x47c440: send2(ui, 0x2d, shift); break;       /* BtnUnburrow */
	case 0x47c450: send2(ui, 0x22, shift); break;       /* BtnDecloak */
	case 0x47c460: /* BtnLoad */
		if (grounded_status_building(ui)) kp_ui_begin_targeting(ui, O_PICKUP_BUNKER, O_NOTHING, O_NOTHING);
		else kp_ui_begin_targeting(ui, O_PICKUP_TRANSPORT, O_MOVE, O_MOVE);
		break;
	case 0x47c490: kp_ui_begin_targeting(ui, O_HARVEST1, O_MOVE, O_HARVEST2); break;
	case 0x47c4a0: kp_ui_begin_targeting(ui, O_HEAL_MOVE, O_HEAL_MOVE, O_HEAL_MOVE); break;
	case 0x47c4b0: send2(ui, 0x2b, shift); break;       /* BtnHoldPosition */
	case 0x47c4c0: send1(ui, 0x18); break;              /* BtnCancelConstruction */
	case 0x47c4d0: send1(ui, 0x19); break;              /* BtnCancelMorph */
	case 0x47c4e0: kp_ui_start_placement(ui, O_LAND, value); break;
	case 0x47c4f0: if (affordable(ui, value)) send3(ui, 0x23, value); break; /* BtnUnitMorph */
	case 0x47c520: kp_ui_begin_targeting(ui, O_FOLLOW, O_MOVE, O_MOVE); break;
	case 0x47c530: kp_ui_begin_targeting(ui, O_NUKE_PAINT, O_NUKE_PAINT, O_NUKE_PAINT); break;
	case 0x47c540: kp_ui_start_placement(ui, O_BUILD_NYDUS, value); break;
	case 0x47c550: kp_ui_begin_targeting(ui, O_PATROL, O_PATROL, O_PATROL); break;
	case 0x47c560: if (affordable(ui, value)) kp_ui_start_placement(ui, O_PLACE_BUILDING, value); break;
	case 0x47c590: if (affordable(ui, value)) kp_ui_start_placement(ui, O_PLACE_ADDON, value); break;
	case 0x47c5c0: kp_ui_start_placement(ui, O_CTF_COP2, (uint16_t)g->u.type[ui->hud.status_unit]); break;
	case 0x47c5d0: if (affordable(ui, value)) kp_ui_start_placement(ui, O_PLACE_PROTOSS, value); break;
	case 0x47c600: kp_ui_begin_targeting(ui, O_RALLY_UNIT, O_RALLY_TILE, O_RALLY_TILE); break;
	case 0x47c610: kp_ui_begin_targeting(ui, O_REAVER_ATTACK, O_ATTACK_MOVE, O_REAVER_MOVE_ATTACK); break;
	case 0x47c620: kp_ui_begin_targeting(ui, O_RECHARGE, O_RECHARGE, O_RECHARGE); break;
	case 0x47c630: kp_ui_begin_targeting(ui, O_REPAIR, O_MOVE, O_MOVE_REPAIR); break;
	case 0x47c640: /* BtnReturnCargo */
		kp_ui_yes_sound(ui, ui->hud.status_unit);
		send2(ui, 0x1e, shift);
		break;
	case 0x47c660: send2(ui, 0x30, (uint8_t)value); break; /* BtnResearch */
	case 0x47c670: select_larva(ui); break;
	case 0x47c700: send2(ui, 0x26, shift); break;       /* BtnSiege */
	case 0x47c710: /* BtnStimPack */
		for (k = 0; k < SLOTS; k++) {
			int64_t n = ui->hud.selection[k];
			if (n != 0 && g->u.hp[n] > STIM_HP) {
				send1(ui, 0x36);
				return;
			}
		}
		kp_ui_error_buzz(ui, ui->hud.status_unit, STRING_STIM, g->local_player); /* ShowErrorWithRaceBuzz 0x446760 */
		break;
	case 0x47c760: send2(ui, 0x1a, shift); break;       /* BtnStop */
	case 0x47c770: send3(ui, 0x20, value); break;       /* BtnCancelTrain */
	case 0x47c780: send1(ui, 0x2a); break;              /* BtnArchonWarp */
	case 0x47c790: send1(ui, 0x5a); break;              /* BtnDarkArchonMeld */
	case 0x47c7a0: send2(ui, 0x25, shift); break;       /* BtnUnsiege */
	case 0x47c7b0: use_tech(ui, value); break;
	case 0x47c9c0: kp_ui_begin_targeting(ui, O_TOWER_ATTACK, O_TOWER_GUARD, O_TOWER_GUARD); break;
	case 0x47c9d0: send1(ui, 0x1c); break;              /* BtnReaverStop */
	case 0x47c9e0: /* BtnUnload */
		if (grounded_status_building(ui)) send2(ui, 0x28, shift);
		else kp_ui_begin_targeting(ui, O_MOVE_UNLOAD, O_MOVE_UNLOAD, O_MOVE_UNLOAD);
		break;
	case 0x47ca10: send2(ui, 0x32, (uint8_t)value); break; /* BtnUpgrade */
	case 0x47ca20: if (affordable(ui, value)) send3(ui, 0x35, value); break; /* BtnBuildingMorph */
	case 0x47ca50: if (affordable(ui, value)) kp_ui_start_placement(ui, O_DRONE_BUILD, value); break;
	case 0x4a04b0: kp_ui_cancel_targeting(ui); break;   /* BtnActCancelTargeting */
	case 0x4a04c0: kp_ui_end_placement(ui); break;      /* BtnActCancelPlacement: EndPlacement 0x47d300(0, 0, 1) */
	case 0x4a11e0: kp_ui_card_override(ui, value); break; /* the build menus and their return */
	case 0x4be950: break;                               /* NoOp */
	default: break; /* TODO: a replay's buttons (0x480e50, 0x480ee0, 0x480f70) */
	}
}
