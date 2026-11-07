/* The console's status panel (plan phase 9B; statdata.cpp, statwire.cpp; atlas P35 0x4a1b60..0x4a2570, P36
 * 0x4a25b0..0x4a5910, P37 0x4a7f10..0x4a82e0): the status unit's name, hit points, energy, status line, kills, the
 * weapon, armor, shield and ammo slots, the training queue, research and upgrade progress, the supply lines, the
 * resource amount, the cargo, the twelve-unit multi-selection and the wireframes. The layout of a unit type comes from
 * the executable's table 0x50ba50 (ui.kpd STLY), dispatched here by the procedures' addresses. The panel reads the game
 * and never writes it; the commands of its buttons (cancel, unload, select) are phase 9C's. */
#include "kp_ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kp_game.h"
#include "kp_ids.h"

/* units.dat special ability flags, statuses, types, orders */
#define FLAG_BUILDING 0x1
#define FLAG_HERO 0x40
#define FLAG_RESOURCE_CONTAINER 0x2000
#define FLAG_SPELLCASTER 0x200000
#define FLAG_NO_RANK 0x800
#define STATUS_COMPLETED 0x1
#define STATUS_GROUNDED_BUILDING 0x2
#define STATUS_BURROWED 0x10
#define STATUS_DISABLED 0x400
#define STATUS_INVINCIBLE 0x04000000
#define STATUS_HALLUCINATION 0x40000000
#define TYPE_NONE 228
#define WEAPON_NONE 130
#define TECH_NONE 44
#define UPGRADE_NONE 61
#define ARMOR_UPGRADE_NONE 60
#define TRAINABLE_LAST 105        /* a queue head up to 105 is a unit in training */
#define ORDER_COMPLETING_ARCHON 0x6a
#define UPG_PLASMA_SHIELDS 15
#define TECH_SPIDER_MINES 3
#define SHIELD_ICON 310
#define MAX_SHOWN 9999
#define TOOLTIP_TEXT 256
#define TECH_TOOLTIP_TEXT 520
#define TYPE_EGG 36
#define TYPE_COCOON 59
#define TYPE_LURKER_EGG 97
#define WEAPON_HALO_ROCKETS 103
#define WEAPON_PULSE_CANNON 79   /* the Interceptor's */
#define CMD_CANCEL_TRAIN 0x20
#define CMD_CANCEL_RESEARCH 0x31
#define CMD_CANCEL_UPGRADE 0x33
#define CMD_CANCEL_ADDON 0x34
#define CMD_CANCEL_CONSTRUCTION 0x18
#define CMD_UNLOAD 0x29

/* control indexes of statdata.bin */
enum {
	C_WIREFRAME = 1, C_QUEUE = 2, C_PROGRESS_7 = 7, C_FRAME_11 = 8, C_SLOT = 9, C_PROGRESS_13 = 13, C_PROGRESS_14 = 14,
	C_RESEARCH = 15, C_PROGRESS_16 = 16, C_ADDON = 17, C_CARGO_FIRST = 18, C_CARGO_LAST = 32, C_BOX_FIRST = 33,
	C_BOX_LAST = 44, C_NAME = -5, C_HP = -7, C_ENERGY = -8, C_SUPPLY_1 = -10, C_SUPPLY_2 = -11, C_SUPPLY_3 = -12,
	C_SUPPLY_4 = -13, C_QUEUE_LABEL = -15, C_STATUS = -20, C_KILLS = -21, C_BUILD_LABEL = -30, C_RESEARCH_LABEL = -35,
	C_MORPH_LABEL = -40
};

/* the layout modes 0x6aab31 */
enum {
	M_NAME, M_MULTI, M_GENERIC, M_STATS, M_PLAIN, M_QUEUE, M_BUILDING, M_RESEARCH, M_UPGRADE, M_ADDON, M_MORPH,
	M_CARGO, M_SUPPLY, M_SILO, M_POWERUP, M_RESOURCE
};

/* slot record kinds */
enum { K_WEAPON, K_ARMOR, K_SHIELDS, K_COUNT, K_TECH, K_UPGRADE, K_EMPTY };

/* stat_txt strings */
enum {
	S_KILLS = 0x2fc, S_UPGRADING = 0x2fd, S_RESEARCHING = 0x300, S_MORPHING = 0x303, S_BUILDING = 0x306,
	S_DISABLED = 0x33c, S_HALLUCINATION = 0x340, S_ACID_SPORES = 0x514, S_BLIND = 0x513, S_PARASITE = 0x33b,
	S_DETECTOR = 0x33f, S_RANK = 0x516, S_HERO_A = 0x23c, S_HERO_B = 0x23b, S_UNDER_CONSTRUCTION = 0x31f,
	S_ADDING_ON = 0x313, S_SUMMONING = 0x316, S_MINERALS = 0x31c, S_GAS = 0x31d, S_DEPLETED = 0x31e,
	S_SUPPLY_USED = 0x335, S_SUPPLY_PROVIDED = 0x32f, S_SUPPLY_TOTAL = 0x332, S_SUPPLY_MAX = 0x338,
	S_DAMAGE = 0x309, S_ARMOR = 0x30a, S_SHIELDS = 0x30b, S_PER_ROCKET = 0x515, S_INTERCEPTORS = 0x317,
	S_SCARABS = 0x318, S_NUKES = 0x319, S_SPIDER_MINES = 0x31a, S_QUEUED = 0x322, S_CANCEL_UPGRADE = 0x325,
	S_CANCEL_RESEARCH = 0x326, S_UNLOAD = 0x328, S_SELECT = 0x329
};

/* the wireframe colour tables 0x50d7dc, 0x50d804 (hit points, rows of 4), 0x50d82c (shields, rows of 2), 0x50d83c
 * (outline, rows of 4) */
static const uint8_t HP_COLOURS_A[10][4] = { { 10, 10, 10, 10 }, { 10, 10, 10, 0 }, { 10, 10, 0, 0 }, { 10, 10, 0, 1 },
	{ 10, 10, 1, 1 }, { 10, 0, 1, 1 }, { 10, 1, 1, 1 }, { 0, 1, 1, 1 }, { 0, 1, 1, 1 }, { 1, 1, 1, 1 } };
static const uint8_t HP_COLOURS_B[10][4] = { { 10, 10, 10, 10 }, { 10, 10, 10, 0 }, { 10, 10, 0, 0 }, { 10, 0, 0, 0 },
	{ 10, 0, 0, 1 }, { 0, 0, 0, 1 }, { 0, 0, 1, 1 }, { 0, 1, 1, 1 }, { 0, 1, 1, 1 }, { 1, 1, 1, 1 } };
static const uint8_t SHIELD_COLOURS[8][2] = { { 2, 2 }, { 9, 2 }, { 8, 9 }, { 7, 8 }, { 6, 7 }, { 5, 6 }, { 4, 5 },
	{ 3, 4 } };
static const uint8_t OUTLINE_COLOURS[6][4] = { { 14, 6, 7, 8 }, { 19, 11, 13, 6 }, { 19, 11, 10, 16 },
	{ 1, 18, 20, 16 }, { 1, 18, 20, 12 }, { 0, 1, 18, 10 } };
#define REMAP_HP 0xd0
#define REMAP_SHIELDS 0xc0
#define REMAP_OUTLINE 0xd8

const uint8_t KP_UI_WIRE_INDEXES[KP_UI_WIRE_COLOURS] = { REMAP_HP, REMAP_HP + 1, REMAP_HP + 2, REMAP_HP + 3,
	REMAP_SHIELDS, REMAP_SHIELDS + 1, REMAP_OUTLINE, REMAP_OUTLINE + 1, REMAP_OUTLINE + 2, REMAP_OUTLINE + 3 };
#define TWIRE_MARK 2

static const char RACE_LETTERS[3] = { 'z', 't', 'p' };

static KpGame *game(KpUi *ui) { return ui->game; }

static int64_t status_unit(KpUi *ui) { return ui->hud.status_unit; }

/* Owned by the local player (the replay's branch is outside V1). */
static int owned(KpUi *ui, int64_t n) { return game(ui)->u.owner[n] == game(ui)->local_player; }

static int64_t dat_type(int64_t t) { return t < 0 || t >= KP_UNIT_TYPES ? 0 : t; }

static int64_t special(KpGame *g, int64_t t) { return KP_DAT(g, units, special_ability_flags, dat_type(t)); }

static int hallucination(KpGame *g, int64_t n) { return (g->u.status[n] & STATUS_HALLUCINATION) != 0; }

/* IsUnitDisabled 0x407b50. */
static int disabled(KpGame *g, int64_t n)
{
	const KpUnits *u = &g->u;
	return (u->status[n] & STATUS_DISABLED) != 0 || u->lockdown_timer[n] != 0 || u->stasis_timer[n] != 0
		|| u->maelstrom_timer[n] != 0;
}

/* GetBuildQueueEntry 0x41b300: queue[(slot + i) % 5]. */
static int64_t queue_at(KpGame *g, int64_t n, int i)
{
	const KpUnits *u = &g->u;
	switch ((u->queue_slot[n] + i) % 5) {
	case 0: return u->queue0[n];
	case 1: return u->queue1[n];
	case 2: return u->queue2[n];
	case 3: return u->queue3[n];
	default: return u->queue4[n];
	}
}

/* GetDisplayedUnitType 0x428b20. */
static int64_t displayed_type(KpGame *g, int64_t n)
{
	return kp_is_morphing_zerg_building(g, n) ? queue_at(g, n, 0) : g->u.type[n];
}

/* GetUnitRace 0x428d60: of the unit's own type, by its group flags. */
static int race_of(KpGame *g, int64_t n)
{
	int64_t f = KP_DAT(g, units, staredit_group_flags, g->u.type[n]);
	if (f & 1) return 0;
	if (f & 4) return 2;
	return (f & 2) ? 1 : 4;
}

static const uint8_t *stat_txt(KpUi *ui, uint32_t id) { return kp_ui_tbl(ui, KP_UI_STAT_TXT, id); }

const uint8_t *kp_ui_unit_type_name(KpUi *ui, int64_t t)
{
	KpGame *g = game(ui);
	static const uint8_t EMPTY[1] = { 0 };
	int64_t map;
	if (t < 0 || t > TYPE_NONE - 1) return EMPTY;
	map = KP_DAT(g, units, unit_map_string, t);
	if (map != 0) {
		const char *s = kp_map_string(g, map);
		return s ? (const uint8_t *)s : EMPTY;
	}
	return stat_txt(ui, (uint32_t)(t + 1));
}

/* GetUnitName 0x428d10. */
static const uint8_t *unit_name(KpUi *ui, int64_t n) { return kp_ui_unit_type_name(ui, displayed_type(game(ui), n)); }

/* GetBuildProgressPercent 0x41b2b0. */
static int32_t build_percent(KpGame *g, int64_t n)
{
	int64_t t = g->u.type[n], total;
	if (t == KP_TYPE_EGG || t == KP_TYPE_COCOON || t == KP_TYPE_LURKER_EGG || kp_is_morphing_zerg_building(g, n)) {
		t = queue_at(g, n, 0);
	}
	total = (uint16_t)KP_DAT(g, units, build_time, dat_type(t));
	if (total == 0) return 0;
	return (int32_t)(((total - (uint16_t)g->u.build_time[n]) * 100) / total);
}

/* GetCurrentWorkTarget 0x41b330. */
static int64_t work_target(KpGame *g, int64_t n)
{
	const KpUnits *u = &g->u;
	int64_t o = u->order[n];
	if (o == 0x1e || o == 0x22 || o == 0x21 || kp_building_unit(g, n)) return u->order_unit[n];
	if (u->secondary_order[n] == 0x26 || u->secondary_order[n] == 0x3f || kp_constructing_addon(g, n)) {
		return u->build_unit[n];
	}
	return 0;
}

/* 0x41fbe0: the ground weapon, the subunit's when the type has none; none for an unburrowed Lurker. */
static int64_t ground_weapon(KpGame *g, int64_t n)
{
	const KpUnits *u = &g->u;
	int64_t w;
	if (u->type[n] == KP_TYPE_LURKER && !(u->status[n] & STATUS_BURROWED)) return WEAPON_NONE;
	w = KP_DAT(g, units, ground_weapon, u->type[n]);
	if (w == WEAPON_NONE) return u->subunit[n] ? KP_DAT(g, units, ground_weapon, u->type[u->subunit[n]]) : WEAPON_NONE;
	return w;
}

/* 0x41fb40. */
static int64_t air_weapon(KpGame *g, int64_t n)
{
	const KpUnits *u = &g->u;
	int64_t w = KP_DAT(g, units, air_weapon, u->type[n]);
	if (w == WEAPON_NONE) return u->subunit[n] ? KP_DAT(g, units, air_weapon, u->type[u->subunit[n]]) : WEAPON_NONE;
	return w;
}

/* 0x41fc20: the type's own ground weapon (none for an unburrowed Lurker). */
static int64_t own_ground_weapon(KpGame *g, int64_t n)
{
	const KpUnits *u = &g->u;
	if (u->type[n] == KP_TYPE_LURKER && !(u->status[n] & STATUS_BURROWED)) return WEAPON_NONE;
	return KP_DAT(g, units, ground_weapon, u->type[n]);
}

static int32_t hp_whole(KpGame *g, int64_t n) { return (int32_t)(g->u.hp[n] + 255) >> 8; }

/* GetMaxHitPoints 0x409c90. */
static int32_t max_hp(KpGame *g, int64_t n)
{
	int32_t m = (int32_t)KP_DAT(g, units, hit_points, g->u.type[n]) >> 8, c;
	if (m != 0) return m;
	c = hp_whole(g, n);
	return c ? c : 1;
}

static uint32_t hp_percent(KpGame *g, int64_t n) { return (uint32_t)((hp_whole(g, n) * 100) / max_hp(g, n)); }

/* --- the panel's controls --- */

static int32_t ctl(KpUi *ui, int16_t id) { return kp_ui_find_control(ui, ui->status.panel, id); }

static KpUiControl *at(KpUi *ui, int32_t k) { return &ui->controls[k]; }

static void show(KpUi *ui, int32_t k)
{
	if (k >= 0) kp_ui_show_control(ui, k);
}

static void hide(KpUi *ui, int32_t k)
{
	if (k >= 0) kp_ui_hide_control(ui, k);
}

static void dirty(KpUi *ui, int32_t k)
{
	if (k >= 0) kp_ui_mark_dirty(ui, k);
}

static void enable(KpUi *ui, int32_t k, int on)
{
	if (k < 0) return;
	if (on) at(ui, k)->flags &= ~KP_UI_DISABLED;
	else at(ui, k)->flags |= KP_UI_DISABLED;
	dirty(ui, k); /* EnableControl 0x4c2910, DisableControl 0x4c2280 */
}

/* SetStatusLineFromArg 0x4a4740 and its inlined copies: the text, shown, dirtied. */
static void set_text(KpUi *ui, int16_t id, const void *text)
{
	int32_t k = ctl(ui, id);
	if (k < 0) return;
	at(ui, k)->text = (const uint8_t *)text;
	show(ui, k);
	dirty(ui, k);
}

/* ShowStatusControlValue 0x4a1ab0: wUser = value, shown, dirtied. */
static void show_value(KpUi *ui, int16_t id, uint16_t value)
{
	int32_t k = ctl(ui, id);
	if (k < 0) return;
	at(ui, k)->wuser = value;
	show(ui, k);
	dirty(ui, k);
}

static void set_record(KpUi *ui, int32_t k, const KpUiFile *grp, uint16_t frame, uint16_t kind, uint16_t id)
{
	KpUiSlot *r = &at(ui, k)->slot;
	r->grp = grp ? grp->data : NULL;
	r->grp_size = grp ? grp->size : 0;
	r->frame = frame;
	r->kind = kind;
	r->id = id;
}

/* HideControlList 0x4a1ed0 on the panel. */
static void hide_all(KpUi *ui)
{
	int32_t k;
	for (k = at(ui, ui->status.panel)->child; k >= 0; k = at(ui, k)->next) hide(ui, k);
}

/* HideStatusControlsAfterMinus7 0x4a1ef0. */
static void hide_after_hp(KpUi *ui)
{
	int32_t k = ctl(ui, C_HP);
	if (k < 0) return;
	for (k = at(ui, k)->next; k >= 0; k = at(ui, k)->next) hide(ui, k);
}

/* The show runs 0x4a2450..0x4a2540: from start, each control in list order through the one with index end. */
static void show_run(KpUi *ui, int16_t start, int16_t end)
{
	int32_t k;
	for (k = ctl(ui, start); k >= 0; k = at(ui, k)->next) {
		show(ui, k);
		if (at(ui, k)->id == end) break;
	}
}

/* Mode changes: when the mode differs, hide after -7 (or all), show a run, then store the mode. */
static void enter_mode(KpUi *ui, uint8_t mode, int16_t start, int16_t end, int all)
{
	if (ui->status.mode == mode) return;
	if (all) hide_all(ui);
	else hide_after_hp(ui);
	if (start != 0) show_run(ui, start, end);
	ui->status.mode = mode;
}

static void cache_values(KpUi *ui)
{
	KpGame *g = game(ui);
	int64_t n = status_unit(ui);
	ui->status.hp[0] = (int32_t)g->u.hp[n];
	ui->status.energy = (uint16_t)((g->u.energy[n] >> 8) & 0xff);
	ui->status.shields = (int32_t)g->u.shields[n] >> 8;
}

/* CacheSecondaryValue 0x4a5910. */
static void cache_secondary(KpUi *ui)
{
	KpGame *g = game(ui);
	int64_t n = status_unit(ui), t = g->u.type[n];
	if (t == KP_TYPE_CARRIER || t == KP_TYPE_GANTRITHOR || t == KP_TYPE_REAVER || t == KP_TYPE_WARBRINGER) {
		uint8_t count = (uint8_t)kp_union_get(g, n, 0xc8, 1);
		if (t == KP_TYPE_CARRIER || t == KP_TYPE_GANTRITHOR) count = (uint8_t)(count + kp_union_get(g, n, 0xc9, 1));
		ui->status.secondary = (ui->status.secondary & 0xffffff00u) | count;
		if (!hallucination(g, n)) return;
	}
	if (!hallucination(g, n) && t != KP_TYPE_BROODLING) {
		ui->status.secondary = 0;
		return;
	}
	ui->status.secondary = (ui->status.secondary & 0xffffu) | (uint32_t)((uint16_t)g->u.remove_timer[n] / 10) << 16;
}

/* StatusNeedsProgressRefresh 0x4a5320. */
static int needs_progress(KpUi *ui)
{
	KpGame *g = game(ui);
	int64_t n = status_unit(ui), s = g->u.status[n];
	if (!owned(ui, n)) return 0;
	if (kp_is_transport(g, n)) return !(s & STATUS_COMPLETED);
	if (!(s & STATUS_COMPLETED)) return 1;
	if (s & STATUS_GROUNDED_BUILDING) {
		return kp_unit_producing(g, n) || kp_union_get(g, n, 0xc8, 1) != TECH_NONE
			|| kp_union_get(g, n, 0xc9, 1) != UPGRADE_NONE;
	}
	return kp_unit_producing(g, n) != 0;
}

/* --- the slots 9..12 and the queue (0x4a3230..0x4a50a0) --- */

static int32_t slot_control(KpUi *ui, int slot) { return ctl(ui, (int16_t)(C_SLOT + slot)); }

static void fill_slot(KpUi *ui, int slot, unsigned value, uint16_t frame, uint16_t kind, uint16_t id)
{
	int32_t k = slot_control(ui, slot);
	snprintf(ui->status.slot_text[slot], sizeof ui->status.slot_text[slot], "%d ", (int)value);
	if (k < 0) return;
	show(ui, k);
	enable(ui, k, 0);
	at(ui, k)->wuser = 12;
	set_record(ui, k, ui->status.cmdicons, frame, kind, id);
	at(ui, k)->text = (const uint8_t *)ui->status.slot_text[slot];
	dirty(ui, k);
}

/* SetArmorSlot 0x4a3230. */
static void armor_slot(KpUi *ui, int slot)
{
	KpGame *g = game(ui);
	int64_t n = status_unit(ui), up = KP_DAT(g, units, armor_upgrade, displayed_type(g, n));
	uint8_t b = (uint8_t)kp_armor_bonus(g, n);
	fill_slot(ui, slot, b, (uint16_t)KP_DAT(g, upgrades, icon, up), K_ARMOR, (uint16_t)up);
	ui->status.upgrades = (uint16_t)(ui->status.upgrades + b);
}

/* SetShieldSlot 0x4a4540. */
static void shield_slot(KpUi *ui, int slot)
{
	KpGame *g = game(ui);
	uint8_t b = (uint8_t)kp_upgrade_level(g, g->u.owner[status_unit(ui)], UPG_PLASMA_SHIELDS);
	fill_slot(ui, slot, b, SHIELD_ICON, K_SHIELDS, UPG_PLASMA_SHIELDS);
	ui->status.upgrades = (uint16_t)(ui->status.upgrades + b);
}

/* SetWeaponSlot 0x4a50a0. */
static void weapon_slot(KpUi *ui, int slot, int64_t w)
{
	KpGame *g = game(ui);
	uint8_t b = (uint8_t)kp_upgrade_level(g, g->u.owner[status_unit(ui)], KP_DAT(g, weapons, damage_upgrade, w));
	fill_slot(ui, slot, b, (uint16_t)KP_DAT(g, weapons, icon, w), K_WEAPON, (uint16_t)(w & 0xff));
	ui->status.upgrades = (uint16_t)(ui->status.upgrades + b);
}

/* SetHangarCountSlot 0x4a3af0. */
static void hangar_slot(KpUi *ui, int slot)
{
	KpGame *g = game(ui);
	int64_t n = status_unit(ui), t = g->u.type[n];
	uint8_t count = 0;
	uint16_t child = (uint16_t)kp_hangar_child_type(t);
	if (t == KP_TYPE_REAVER || t == KP_TYPE_WARBRINGER) count = (uint8_t)kp_union_get(g, n, 0xc8, 1);
	else if (t == KP_TYPE_CARRIER || t == KP_TYPE_GANTRITHOR) {
		count = (uint8_t)(kp_union_get(g, n, 0xc9, 1) + kp_union_get(g, n, 0xc8, 1));
	}
	fill_slot(ui, slot, count, child, K_COUNT, child);
}

/* SetSpiderMineSlot 0x4a3c90. */
static int mine_slot(KpUi *ui, int slot)
{
	KpGame *g = game(ui);
	int64_t n = status_unit(ui);
	if (kp_can_use_tech(g, TECH_SPIDER_MINES, g->u.owner[n], n) != 1) return 0;
	fill_slot(ui, slot, (uint8_t)kp_union_get(g, n, 0xc0, 1), 13, K_COUNT, 13);
	return 1;
}

/* SetNukeSlot 0x4a3f40. */
static void nuke_slot(KpUi *ui, int slot)
{
	KpGame *g = game(ui);
	fill_slot(ui, slot, kp_union_get(g, status_unit(ui), 0xd0, 4) != 0, 14, K_COUNT, 14);
}

/* BuildStatSlots 0x4a4770. */
static void build_slots(KpUi *ui)
{
	KpGame *g = game(ui);
	int64_t n = status_unit(ui), t = g->u.type[n];
	uint16_t s = 0;
	int32_t k;
	if (!hallucination(g, n) || !owned(ui, n)) {
		int64_t armor = KP_DAT(g, units, armor_upgrade, displayed_type(g, n)), gw, aw;
		if (armor != ARMOR_UPGRADE_NONE) armor_slot(ui, 0);
		s = armor != ARMOR_UPGRADE_NONE;
		if (KP_DAT(g, units, shield_enabled, t)) shield_slot(ui, s++);
		gw = ground_weapon(g, n);
		if (gw != WEAPON_NONE) weapon_slot(ui, s++, gw);
		aw = air_weapon(g, n);
		if (aw != WEAPON_NONE && aw != gw) weapon_slot(ui, s++, aw);
		if (t == KP_TYPE_CARRIER || t == KP_TYPE_WARBRINGER || t == KP_TYPE_GANTRITHOR || t == KP_TYPE_REAVER) {
			hangar_slot(ui, s++);
		}
		if ((t == KP_TYPE_VULTURE || t == KP_TYPE_JIM_RAYNOR_VULTURE) && mine_slot(ui, s)) s++;
		if (displayed_type(g, n) == KP_TYPE_NUCLEAR_SILO) nuke_slot(ui, s++);
	}
	for (k = slot_control(ui, s); k >= 0 && s < 4; s++, k = at(ui, k)->next) hide(ui, k);
}

/* --- the text lines --- */

/* SetEnergyOrTimerText 0x4a3d50. */
static void energy_text(KpUi *ui)
{
	KpGame *g = game(ui);
	int64_t n = status_unit(ui), t = g->u.type[n];
	int32_t k = ctl(ui, C_ENERGY);
	const void *text = NULL;
	if ((special(g, t) & FLAG_SPELLCASTER) && !hallucination(g, n) && owned(ui, n)) {
		snprintf(ui->status.energy_text, sizeof ui->status.energy_text, "\x04%d/%d", (int)((g->u.energy[n] >> 8) & 0xff),
			(int)(kp_max_energy(g, n) >> 8));
		text = ui->status.energy_text;
	} else if ((hallucination(g, n) || t == KP_TYPE_BROODLING) && owned(ui, n)) {
		snprintf(ui->status.energy_text, sizeof ui->status.energy_text, "\x04%d/%d",
			(int)((uint16_t)g->u.remove_timer[n] / 10), (int)((uint16_t)kp_timed_life(g, n) / 10));
		text = ui->status.energy_text;
	}
	if (k < 0) return;
	at(ui, k)->text = (const uint8_t *)text;
	show(ui, k);
	dirty(ui, k);
}

/* SetHitPointText 0x4a3990. */
static void hp_text(KpUi *ui)
{
	KpGame *g = game(ui);
	int64_t n = status_unit(ui), t = g->u.type[n];
	int32_t m = (int32_t)KP_DAT(g, units, hit_points, t) >> 8, k = ctl(ui, C_HP);
	int whole_zero = m == 0 && hp_whole(g, n) == 0;
	const void *text = NULL;
	if ((m == 0 ? (whole_zero || hp_whole(g, n) <= MAX_SHOWN) : m <= MAX_SHOWN)
		&& (uint16_t)KP_DAT(g, units, shield_amount, t) <= MAX_SHOWN && !(g->u.status[n] & STATUS_INVINCIBLE)) {
		uint32_t pct = hp_percent(g, n);
		int colour = pct < 34 ? 6 : pct - 34 < 33 ? 3 : 7;
		if (!KP_DAT(g, units, shield_enabled, t)) {
			snprintf(ui->status.hp_text, sizeof ui->status.hp_text, "%c%d/%d", colour, (int)hp_whole(g, n),
				(int)max_hp(g, n));
		} else {
			snprintf(ui->status.hp_text, sizeof ui->status.hp_text, "%d/%d %c%d/%d", (int)((int32_t)g->u.shields[n] >> 8),
				(int)(uint16_t)KP_DAT(g, units, shield_amount, displayed_type(g, n)), colour, (int)hp_whole(g, n),
				(int)max_hp(g, n));
		}
		text = ui->status.hp_text;
	}
	if (k >= 0) {
		at(ui, k)->text = (const uint8_t *)text;
		show(ui, k);
		dirty(ui, k);
	}
	energy_text(ui);
}

/* 0x431160: the rank of a Terran unit, two heroes' labels, else nothing. */
static const uint8_t *rank_text(KpUi *ui, int64_t n)
{
	KpGame *g = game(ui);
	int64_t t = g->u.type[n], f = special(g, t);
	if ((f & FLAG_NO_RANK) || t == 0xf || t == 0xd) return NULL;
	if (t == KP_TYPE_GANTRITHOR) return KP_DAT(g, units, unit_map_string, t) == 0 ? stat_txt(ui, S_HERO_A) : NULL;
	if ((t == 0xc || t == 0x1d || t == 0x1c) && (f & FLAG_HERO) && KP_DAT(g, units, unit_map_string, t) != 0) {
		return stat_txt(ui, S_HERO_B);
	}
	if (race_of(g, n) != 1) return NULL;
	return stat_txt(ui, (uint16_t)(S_RANK + (uint8_t)(KP_DAT(g, units, sublabel, t) + g->u.rank[n])));
}

/* SetStatusLineText 0x4a40a0. */
static void status_line(KpUi *ui)
{
	KpGame *g = game(ui);
	int64_t n = status_unit(ui), s = g->u.status[n], t = g->u.type[n];
	const KpUnits *u = &g->u;
	if (hallucination(g, n) && owned(ui, n)) {
		set_text(ui, C_STATUS, stat_txt(ui, S_HALLUCINATION));
		return;
	}
	if ((special(g, t) & FLAG_BUILDING) && disabled(g, n) && (s & STATUS_COMPLETED) && !kp_unit_producing(g, n)
		&& kp_union_get(g, n, 0xc8, 1) == TECH_NONE && kp_union_get(g, n, 0xc9, 1) == UPGRADE_NONE) {
		set_text(ui, C_STATUS, stat_txt(ui, (uint32_t)(S_DISABLED + race_of(g, n))));
		return;
	}
	if (u->acid_spores[n] != 0 && (s & STATUS_COMPLETED)) {
		snprintf(ui->status.lines[0], sizeof ui->status.lines[0], "%s (%d)", (const char *)stat_txt(ui, S_ACID_SPORES),
			(int)(int8_t)u->acid_spores[n]);
		set_text(ui, C_STATUS, ui->status.lines[0]);
		return;
	}
	if (u->blind[n] != 0 && (s & STATUS_COMPLETED)) {
		set_text(ui, C_STATUS, stat_txt(ui, S_BLIND));
		return;
	}
	if (u->parasites[n] != 0 && (s & STATUS_COMPLETED)) {
		set_text(ui, C_STATUS, stat_txt(ui, S_PARASITE));
		return;
	}
	if (kp_is_detector(g, n)) {
		set_text(ui, C_STATUS, stat_txt(ui, S_DETECTOR));
		return;
	}
	set_text(ui, C_STATUS, (special(g, t) & FLAG_BUILDING) ? NULL : rank_text(ui, n));
}

/* SetKillCountText 0x4a3bd0. */
static void kills_text(KpUi *ui)
{
	KpGame *g = game(ui);
	int64_t n = status_unit(ui), t = g->u.type[n];
	if (t != KP_TYPE_SCOURGE && t != KP_TYPE_INFESTED_TERRAN && (!hallucination(g, n) || !owned(ui, n))
		&& (kp_unit_has_weapon(g, n) || (uint8_t)g->u.kills[n] != 0)) {
		snprintf(ui->status.lines[2], sizeof ui->status.lines[2], "%s %d", (const char *)stat_txt(ui, S_KILLS),
			(int)(uint8_t)g->u.kills[n]);
		set_text(ui, C_KILLS, ui->status.lines[2]);
		return;
	}
	hide(ui, ctl(ui, C_KILLS));
}

static void name_text(KpUi *ui) { set_text(ui, C_NAME, unit_name(ui, status_unit(ui))); }

/* --- the layouts --- */

static void draw_building(KpUi *ui);

/* DrawGenericUnit 0x4a4e40, mode 2. */
static void draw_generic(KpUi *ui)
{
	cache_values(ui);
	cache_secondary(ui);
	enter_mode(ui, M_GENERIC, C_STATUS, 12, 0);
	ui->status.upgrades = 1;
	build_slots(ui);
	status_line(ui);
	kills_text(ui);
	hp_text(ui);
	name_text(ui);
}

/* DrawNameOnly 0x4a3f00, mode 0. */
static void draw_name_only(KpUi *ui)
{
	ui->status.mode = M_NAME;
	hide_all(ui);
	name_text(ui);
}

/* DrawConstructionProgress 0x4a4dc0, mode 6. */
static void draw_construction(KpUi *ui)
{
	KpGame *g = game(ui);
	int64_t n = status_unit(ui);
	show_run(ui, C_BUILD_LABEL, C_PROGRESS_13);
	if (ui->status.mode != M_BUILDING) {
		hide_after_hp(ui);
		ui->status.mode = M_BUILDING;
	}
	show_value(ui, C_PROGRESS_13, (uint16_t)build_percent(g, n));
	set_text(ui, C_BUILD_LABEL, stat_txt(ui, (uint32_t)(S_UNDER_CONSTRUCTION + race_of(g, n))));
}

/* DrawTrainingQueue 0x4a48e0, mode 5. */
static void draw_queue(KpUi *ui)
{
	KpGame *g = game(ui);
	int64_t n = status_unit(ui), target, label;
	int32_t k = ctl(ui, C_QUEUE);
	int i;
	if (!owned(ui, n)) return;
	enter_mode(ui, M_QUEUE, C_QUEUE_LABEL, C_FRAME_11, 0);
	target = work_target(g, n);
	show_value(ui, C_PROGRESS_7, (uint16_t)(target ? build_percent(g, target) : 0));
	for (i = 0; k >= 0 && i < 5; i++, k = at(ui, k)->next) {
		int64_t t = (int16_t)queue_at(g, n, i);
		snprintf(ui->status.slot_text[i], sizeof ui->status.slot_text[i], i == 0 ? "%d " : "\x03%d\x01 ", i + 1);
		at(ui, k)->wuser = (uint16_t)(i == 0 || i > 3 ? 2 : 4);
		if (t == TYPE_NONE) {
			set_record(ui, k, ui->status.cmdbtns, (uint16_t)(i + 6), K_EMPTY, at(ui, k)->slot.id);
			enable(ui, k, 0);
			at(ui, k)->text = NULL;
		} else {
			set_record(ui, k, ui->status.cmdicons, (uint16_t)t, K_COUNT, (uint16_t)t);
			enable(ui, k, 1);
			at(ui, k)->text = (const uint8_t *)ui->status.slot_text[i];
		}
		dirty(ui, k);
	}
	if ((g->u.status[n] & STATUS_GROUNDED_BUILDING) && disabled(g, n)) label = S_DISABLED;
	else if (displayed_type(g, n) == KP_TYPE_GATEWAY || displayed_type(g, n) == KP_TYPE_STARGATE) label = S_MORPHING;
	else label = S_BUILDING;
	set_text(ui, C_QUEUE_LABEL, stat_txt(ui, (uint32_t)(label + race_of(g, n))));
}

static void progress_icon(KpUi *ui, int16_t id, uint16_t frame, uint16_t kind, uint16_t value)
{
	int32_t k = ctl(ui, id);
	if (k < 0) return;
	at(ui, k)->wuser = 2;
	set_record(ui, k, ui->status.cmdicons, frame, kind, value);
}

/* DrawResearchProgress 0x4a4270 (mode 7) and DrawUpgradeProgress 0x4a4f30 (mode 8). */
static void draw_research(KpUi *ui, int upgrade)
{
	KpGame *g = game(ui);
	int64_t n = status_unit(ui), id, label;
	uint32_t total, left = (uint32_t)kp_union_get(g, n, 0xc6, 2);
	int32_t k;
	if (!owned(ui, n)) return;
	enter_mode(ui, (uint8_t)(upgrade ? M_UPGRADE : M_RESEARCH), C_RESEARCH_LABEL, C_RESEARCH, 0);
	if (upgrade) {
		id = kp_union_get(g, n, 0xc9, 1);
		total = (uint16_t)((uint32_t)(uint8_t)kp_upgrade_level(g, g->u.owner[n], id)
			* (uint32_t)KP_DAT(g, upgrades, research_time_factor, id) + (uint32_t)KP_DAT(g, upgrades, research_time_base, id));
	} else {
		id = kp_union_get(g, n, 0xc8, 1);
		total = (uint16_t)KP_DAT(g, techdata, research_time, id);
	}
	show_value(ui, C_PROGRESS_14, (uint16_t)(total ? (total - left) * 100 / total : 0));
	k = ctl(ui, C_RESEARCH);
	progress_icon(ui, C_RESEARCH,
		(uint16_t)(upgrade ? KP_DAT(g, upgrades, icon, id) : KP_DAT(g, techdata, icon, id)),
		(uint16_t)(upgrade ? K_UPGRADE : K_TECH), (uint16_t)id);
	enable(ui, k, 1);
	label = disabled(g, n) ? S_DISABLED : upgrade ? S_UPGRADING : S_RESEARCHING;
	set_text(ui, C_RESEARCH_LABEL, stat_txt(ui, (uint32_t)(label + race_of(g, n))));
}

/* ShowAddonProgress 0x4a3070, mode 9. */
static void draw_addon(KpUi *ui)
{
	KpGame *g = game(ui);
	int64_t n = status_unit(ui), target, t;
	int32_t k;
	if (!owned(ui, n)) return;
	enter_mode(ui, M_ADDON, C_MORPH_LABEL, C_ADDON, 0);
	target = work_target(g, n);
	show_value(ui, C_PROGRESS_16, (uint16_t)build_percent(g, target));
	t = displayed_type(g, target);
	k = ctl(ui, C_ADDON);
	progress_icon(ui, C_ADDON, (uint16_t)t, K_COUNT, (uint16_t)t);
	dirty(ui, k);
	set_text(ui, C_MORPH_LABEL, stat_txt(ui, (uint32_t)(S_ADDING_ON + race_of(g, target))));
}

/* DrawSupplyProvider 0x4a35f0, mode 12. */
static void draw_supply(KpUi *ui)
{
	KpGame *g = game(ui);
	int64_t n = status_unit(ui), p = g->u.owner[n], t;
	int r = race_of(g, n);
	int32_t used, total;
	if (!owned(ui, n)) {
		draw_generic(ui);
		return;
	}
	total = (int32_t)kp_supply_provided_capped(g, r, p);
	used = (int32_t)kp_supply_used(g, r, p);
	ui->status.secondary = (uint32_t)(uint16_t)total | (uint32_t)(uint16_t)used << 16;
	enter_mode(ui, M_SUPPLY, C_SUPPLY_1, C_SUPPLY_4, 0);
	snprintf(ui->status.lines[1], sizeof ui->status.lines[1], "%s %d", (const char *)stat_txt(ui, (uint32_t)(S_SUPPLY_USED + r)), (used + 1) / 2);
	snprintf(ui->status.lines[2], sizeof ui->status.lines[2], "%s %d", (const char *)stat_txt(ui, (uint32_t)(S_SUPPLY_PROVIDED + r)),
		((int)(uint8_t)KP_DAT(g, units, supply_provided, displayed_type(g, n)) + 1) / 2);
	snprintf(ui->status.lines[3], sizeof ui->status.lines[3], "%s %d", (const char *)stat_txt(ui, (uint32_t)(S_SUPPLY_TOTAL + r)), (total + 1) / 2);
	snprintf(ui->status.lines[4], sizeof ui->status.lines[4], "%s %d", (const char *)stat_txt(ui, (uint32_t)(S_SUPPLY_MAX + r)),
		(int)(((uint32_t)kp_supply_max(g, displayed_type(g, n), p) + 1) >> 1));
	set_text(ui, C_SUPPLY_1, ui->status.lines[1]);
	set_text(ui, C_SUPPLY_2, ui->status.lines[2]);
	set_text(ui, C_SUPPLY_3, ui->status.lines[3]);
	set_text(ui, C_SUPPLY_4, ui->status.lines[4]);
	t = g->u.type[n];
	if (KP_DAT(g, units, shield_enabled, t)) shield_slot(ui, 0);
	else if (t == KP_TYPE_OVERLORD || t == KP_TYPE_YGGDRASILL) armor_slot(ui, 0);
	else hide(ui, slot_control(ui, 0));
}

/* DrawResourceAmount 0x4a43d0, mode 15. */
static void draw_resource(KpUi *ui)
{
	KpGame *g = game(ui);
	int64_t n = status_unit(ui), amount = (uint16_t)kp_union_get(g, n, 0xd0, 2);
	ui->status.secondary = (ui->status.secondary & 0xffff0000u) | (uint32_t)amount;
	enter_mode(ui, M_RESOURCE, C_SUPPLY_1, C_SUPPLY_4, 0);
	set_text(ui, C_SUPPLY_1, NULL);
	set_text(ui, C_SUPPLY_3, NULL);
	set_text(ui, C_SUPPLY_4, NULL);
	if (!kp_mineral_field(g->u.type[n]) && amount == 0) {
		set_text(ui, C_SUPPLY_2, stat_txt(ui, S_DEPLETED));
	} else {
		snprintf(ui->status.lines[1], sizeof ui->status.lines[1], "%s %d",
			(const char *)stat_txt(ui, kp_mineral_field(g->u.type[n]) ? S_MINERALS : S_GAS), (int)(int16_t)amount);
		set_text(ui, C_SUPPLY_2, ui->status.lines[1]);
	}
	if (KP_DAT(g, units, shield_enabled, g->u.type[n])) shield_slot(ui, 0);
	else hide(ui, slot_control(ui, 0));
}

/* DrawNuclearSiloStatus 0x4a45e0. */
static void draw_silo(KpUi *ui)
{
	KpGame *g = game(ui);
	int64_t n = status_unit(ui);
	if ((uint16_t)queue_at(g, n, 0) <= TRAINABLE_LAST) {
		int32_t k = ctl(ui, C_QUEUE);
		int64_t target = work_target(g, n);
		uint16_t t = target ? (uint16_t)g->u.type[target] : 0;
		if (ui->status.mode != M_SILO) {
			hide_after_hp(ui);
			ui->status.mode = M_SILO;
		}
		show_value(ui, C_PROGRESS_7, (uint16_t)(target ? build_percent(g, target) : 0));
		snprintf(ui->status.slot_text[0], sizeof ui->status.slot_text[0], "%d ", 1);
		if (k >= 0) {
			at(ui, k)->wuser = 2;
			set_record(ui, k, ui->status.cmdicons, t, K_COUNT, t);
			enable(ui, k, 1);
			at(ui, k)->text = (const uint8_t *)ui->status.slot_text[0];
			show(ui, k);
			dirty(ui, k);
		}
		set_text(ui, C_QUEUE_LABEL, stat_txt(ui, (uint32_t)(S_MORPHING + race_of(g, n))));
		return;
	}
	enter_mode(ui, M_STATS, C_STATUS, 12, 0);
	build_slots(ui);
	hide(ui, ctl(ui, C_STATUS));
	hide(ui, ctl(ui, C_KILLS));
}

/* DrawBuildingStatus 0x4a3310. */
static void draw_building(KpUi *ui)
{
	KpGame *g = game(ui);
	int64_t n = status_unit(ui), t = g->u.type[n];
	cache_values(ui);
	ui->status.progress = (uint8_t)needs_progress(ui);
	cache_secondary(ui);
	if (kp_mineral_field(t) || t == KP_TYPE_GEYSER) {
		draw_resource(ui);
	} else if (!owned(ui, n)) {
		enter_mode(ui, M_PLAIN, 0, 0, 0);
	} else if (!(g->u.status[n] & STATUS_COMPLETED)) {
		draw_construction(ui);
	} else if (t == KP_TYPE_NUCLEAR_SILO) {
		draw_silo(ui);
	} else if (kp_is_transport(g, n)) {
		enter_mode(ui, M_PLAIN, 0, 0, 0);
	} else if ((uint16_t)queue_at(g, n, 0) <= TRAINABLE_LAST) {
		draw_queue(ui);
	} else if (kp_constructing_addon(g, n) || kp_building_unit(g, n)) {
		draw_addon(ui);
	} else if (kp_union_get(g, n, 0xc8, 1) != TECH_NONE) {
		draw_research(ui, 0);
	} else if (kp_union_get(g, n, 0xc9, 1) != UPGRADE_NONE) {
		draw_research(ui, 1);
	} else if (KP_DAT(g, units, supply_provided, t) != 0) {
		draw_supply(ui);
	} else if (kp_gas_building(t) && (g->u.status[n] & STATUS_COMPLETED)) {
		draw_resource(ui);
	} else if (KP_DAT(g, units, air_weapon, displayed_type(g, n)) == WEAPON_NONE && own_ground_weapon(g, n) == WEAPON_NONE
		&& !KP_DAT(g, units, shield_enabled, t)) {
		enter_mode(ui, M_PLAIN, 0, 0, 0);
	} else {
		enter_mode(ui, M_STATS, C_STATUS, 12, 0);
		build_slots(ui);
		hide(ui, ctl(ui, C_KILLS));
	}
	hp_text(ui);
	status_line(ui);
	name_text(ui);
}

/* DrawHangarTrainer 0x4a3540. */
static void draw_hangar(KpUi *ui)
{
	KpGame *g = game(ui);
	int64_t n = status_unit(ui);
	if (owned(ui, n)) {
		ui->status.progress = (uint8_t)needs_progress(ui);
		cache_values(ui);
		cache_secondary(ui);
		if ((uint16_t)queue_at(g, n, 0) <= TRAINABLE_LAST) {
			draw_queue(ui);
			hp_text(ui);
			name_text(ui);
			return;
		}
	}
	draw_generic(ui);
}

/* DrawEggMorph 0x4a3840, mode 10. */
static void draw_egg(KpUi *ui)
{
	KpGame *g = game(ui);
	int64_t n = status_unit(ui), head;
	int32_t k;
	if (!owned(ui, n) || hallucination(g, n)) {
		draw_generic(ui);
		return;
	}
	enter_mode(ui, M_MORPH, C_MORPH_LABEL, C_ADDON, 0);
	show_value(ui, C_PROGRESS_16, (uint16_t)build_percent(g, n));
	head = (uint16_t)queue_at(g, n, 0);
	k = ctl(ui, C_ADDON);
	progress_icon(ui, C_ADDON, (uint16_t)head, K_COUNT, (uint16_t)head);
	dirty(ui, k);
	set_text(ui, C_MORPH_LABEL, stat_txt(ui, (uint32_t)(S_MORPHING + race_of(g, n))));
	hp_text(ui);
	name_text(ui);
}

/* DrawArchonSummon 0x4a3160, mode 6. */
static void draw_archon(KpUi *ui)
{
	KpGame *g = game(ui);
	int64_t n = status_unit(ui);
	if (!owned(ui, n) || g->u.order[n] != ORDER_COMPLETING_ARCHON) {
		draw_generic(ui);
		return;
	}
	show_run(ui, C_BUILD_LABEL, C_PROGRESS_13);
	if (ui->status.mode != M_BUILDING) {
		hide_after_hp(ui);
		ui->status.mode = M_BUILDING;
	}
	show_value(ui, C_PROGRESS_13, (uint16_t)build_percent(g, n));
	set_text(ui, C_BUILD_LABEL, stat_txt(ui, S_SUMMONING));
	hp_text(ui);
	name_text(ui);
}

/* LayoutCargoBoxes 0x4a4b90. */
static void layout_cargo(KpUi *ui)
{
	static const uint16_t FRAMES[3][4] = { /* size 1, 2, 4: hallucination, hero, parasited, normal */
		{ 31, 19, 25, 13 }, { 33, 21, 27, 15 }, { 35, 23, 29, 17 } };
	KpGame *g = game(ui);
	int64_t n = status_unit(ui);
	int16_t big, medium, small;
	int32_t k;
	int size, i;
	for (k = ctl(ui, C_CARGO_FIRST); k >= 0; k = at(ui, k)->next) {
		at(ui, k)->user = 0;
		at(ui, k)->slot.unit = 0;
		if (at(ui, k)->id == C_CARGO_LAST) break;
	}
	if (KP_DAT(g, units, space_provided, g->u.type[n]) >= 7) {
		big = 18;
		medium = 21;
		small = 25;
	} else {
		big = 20;
		medium = 22;
		small = 27;
	}
	for (size = 4; size != 0; size >>= 1) {
		int row = size == 4 ? 2 : size == 2 ? 1 : 0;
		for (i = 0; i < 8; i++) {
			int64_t c = kp_cargo_unit(g, n, i);
			int16_t id;
			uint16_t frame;
			if (c == 0 || KP_DAT(g, units, space_required, g->u.type[c]) != size) continue;
			id = size == 4 ? big : size == 2 ? medium : small;
			if (hallucination(g, c)) frame = FRAMES[row][0];
			else if (special(g, g->u.type[c]) & FLAG_HERO) frame = FRAMES[row][1];
			else frame = g->u.parasites[c] ? FRAMES[row][2] : FRAMES[row][3];
			k = ctl(ui, id);
			if (k >= 0) {
				at(ui, k)->wuser = frame;
				at(ui, k)->user = (uint32_t)c;
				at(ui, k)->slot.unit = c;
				show(ui, k);
				dirty(ui, k);
			}
			big = (int16_t)(big + (size >> 2));
			medium = (int16_t)(medium + (size >> 1));
			small = (int16_t)(small + size);
		}
	}
	for (k = ctl(ui, C_CARGO_FIRST); k >= 0; k = at(ui, k)->next) {
		if (at(ui, k)->user == 0) hide(ui, k);
		if (at(ui, k)->id == C_CARGO_LAST) break;
	}
}

/* CacheCargo 0x4a5880 (0x4a5320 and the value caches, then CacheSecondaryValue's tail). */
static void cache_cargo(KpUi *ui)
{
	KpGame *g = game(ui);
	int64_t n = status_unit(ui);
	const int64_t *loaded[8] = { g->u.loaded0, g->u.loaded1, g->u.loaded2, g->u.loaded3, g->u.loaded4, g->u.loaded5,
		g->u.loaded6, g->u.loaded7 };
	int i;
	for (i = 0; i < 8; i++) {
		int64_t c = kp_cargo_unit(g, n, i);
		ui->status.cargo[i] = (uint16_t)loaded[i][n];
		ui->status.hp[1 + i] = c ? (int32_t)g->u.hp[c] : 0;
	}
	ui->status.progress = (uint8_t)needs_progress(ui);
	cache_values(ui);
	cache_secondary(ui);
}

/* DrawTransportStatus 0x4a4af0, mode 11. */
static void draw_transport(KpUi *ui)
{
	KpGame *g = game(ui);
	int64_t n = status_unit(ui);
	if (owned(ui, n) && !hallucination(g, n)) {
		cache_cargo(ui);
		if (kp_has_cargo(g, n) && owned(ui, n)) {
			if (ui->status.mode != M_CARGO) {
				hide_after_hp(ui);
				ui->status.mode = M_CARGO;
			}
			layout_cargo(ui);
			hp_text(ui);
			name_text(ui);
			return;
		}
	}
	if (g->u.status[n] & STATUS_GROUNDED_BUILDING) draw_building(ui);
	else draw_generic(ui);
}

/* DrawOverlordStatus 0x4a3fd0. */
static void draw_overlord(KpUi *ui)
{
	KpGame *g = game(ui);
	int64_t n = status_unit(ui);
	if (!owned(ui, n) || hallucination(g, n)) {
		draw_generic(ui);
		return;
	}
	if (kp_has_cargo(g, n)) {
		draw_transport(ui);
		return;
	}
	draw_supply(ui);
	hp_text(ui);
	status_line(ui);
	name_text(ui);
}

/* DrawPowerupOrTrap 0x4a4030, mode 14. */
static void draw_powerup(KpUi *ui)
{
	if (ui->status.mode != M_POWERUP) {
		hide_all(ui);
		ui->status.mode = M_POWERUP;
	}
	ui->status.hp[0] = (int32_t)game(ui)->u.hp[status_unit(ui)];
	build_slots(ui);
	hp_text(ui);
	status_line(ui);
	name_text(ui);
}

/* CacheMultiSelection 0x4a5830. */
static void cache_multi(KpUi *ui)
{
	KpGame *g = game(ui);
	int i;
	for (i = 0; i < KP_UI_SLOTS; i++) {
		int64_t n = ui->hud.selection[i];
		ui->status.hp[i] = n ? (int32_t)g->u.hp[n] : 0;
		ui->status.types[i] = n ? (uint16_t)g->u.type[n] : TYPE_NONE;
	}
}

/* DrawMultiSelection 0x4a3e70, mode 1. */
static void draw_multi(KpUi *ui)
{
	int32_t k;
	int i;
	if (ui->status.mode != M_MULTI) {
		hide_all(ui);
		ui->status.mode = M_MULTI;
	}
	cache_multi(ui);
	k = ctl(ui, C_BOX_FIRST);
	if (k < 0) return;
	for (i = 0; i < KP_UI_SLOTS; i++) {
		int64_t n = ui->hud.selection[i];
		if (n == 0) continue;
		at(ui, k)->slot.unit = n;
		at(ui, k)->slot.frame = (uint16_t)game(ui)->u.type[n];
		at(ui, k)->user = (uint32_t)n;
		show(ui, k);
		dirty(ui, k);
		k = at(ui, k)->next;
		if (k < 0) return;
	}
	for (; k >= 0; k = at(ui, k)->next) {
		hide(ui, k);
		if (at(ui, k)->id == C_BOX_LAST) break;
	}
}

/* --- needsRefresh (0x4a5160..0x4a55e0) --- */

/* SecondaryValueChanged 0x4a54e0. */
static int secondary_changed(KpUi *ui)
{
	KpGame *g = game(ui);
	int64_t n = status_unit(ui), t = g->u.type[n];
	uint32_t v = ui->status.secondary;
	int h = 0;
	if (t == KP_TYPE_CARRIER || t == KP_TYPE_GANTRITHOR) {
		h = (uint8_t)v != (uint8_t)(kp_union_get(g, n, 0xc9, 1) + kp_union_get(g, n, 0xc8, 1));
	} else if (t == KP_TYPE_WARBRINGER || t == KP_TYPE_REAVER) {
		h = (uint8_t)v != (uint8_t)kp_union_get(g, n, 0xc8, 1);
	}
	if (hallucination(g, n) || t == KP_TYPE_BROODLING) {
		if (h) return 1;
		return (v >> 16) != (uint32_t)((uint16_t)g->u.remove_timer[n] / 10);
	}
	if (KP_DAT(g, units, supply_provided, t) != 0) {
		int r = race_of(g, n);
		if ((v & 0xffff) != (uint32_t)kp_supply_provided_capped(g, r, g->u.owner[n])) return 1;
		return (v >> 16) != (uint32_t)kp_supply_used(g, r, g->u.owner[n]);
	}
	if (special(g, t) & FLAG_RESOURCE_CONTAINER) return (uint16_t)v != (uint16_t)kp_union_get(g, n, 0xd0, 2);
	return v != 0;
}

static int hp_or_shields_changed(KpUi *ui)
{
	KpGame *g = game(ui);
	int64_t n = status_unit(ui);
	return ui->status.hp[0] != (int32_t)g->u.hp[n] || ui->status.shields != ((int32_t)g->u.shields[n] >> 8);
}

static int energy_changed(KpUi *ui)
{
	return ui->status.energy != (uint16_t)((game(ui)->u.energy[status_unit(ui)] >> 8) & 0xff);
}

/* NeedsRefreshDefault 0x4a55e0. */
static int refresh_default(KpUi *ui)
{
	if (hp_or_shields_changed(ui)) return 1;
	if (!owned(ui, status_unit(ui))) return 0;
	if (energy_changed(ui)) return 1;
	return secondary_changed(ui);
}

/* NeedsRefreshBuilding 0x4a5180. */
static int refresh_building(KpUi *ui)
{
	if (hp_or_shields_changed(ui)) return 1;
	if (!owned(ui, status_unit(ui))) return 0;
	if (energy_changed(ui) || needs_progress(ui) || secondary_changed(ui)) return 1;
	return ui->status.progress;
}

/* NeedsRefreshTransport 0x4a5410. */
static int refresh_transport(KpUi *ui)
{
	KpGame *g = game(ui);
	int64_t n = status_unit(ui);
	const int64_t *loaded[8] = { g->u.loaded0, g->u.loaded1, g->u.loaded2, g->u.loaded3, g->u.loaded4, g->u.loaded5,
		g->u.loaded6, g->u.loaded7 };
	int i;
	if (!owned(ui, n)) return refresh_default(ui);
	for (i = 0; i < 8; i++) {
		int64_t c;
		if (ui->status.cargo[i] != (uint16_t)loaded[i][n]) return 1;
		c = kp_cargo_unit(g, n, i);
		if (c != 0 && ui->status.hp[1 + i] != (int32_t)g->u.hp[c]) return 1;
	}
	if (ui->status.progress || hp_or_shields_changed(ui)) return 1;
	if (!owned(ui, n)) return 0;
	if (energy_changed(ui)) return 1;
	return secondary_changed(ui);
}

/* 0x4a5230 (supply). */
static int refresh_supply(KpUi *ui)
{
	KpGame *g = game(ui);
	int64_t n = status_unit(ui);
	int r;
	if (!owned(ui, n)) return refresh_default(ui);
	r = race_of(g, n);
	return (ui->status.secondary & 0xffff) != (uint32_t)kp_supply_provided_capped(g, r, g->u.owner[n])
		|| (ui->status.secondary >> 16) != (uint32_t)kp_supply_used(g, r, g->u.owner[n]);
}

/* 0x4a53a0 (Overlords). */
static int refresh_overlord(KpUi *ui)
{
	KpGame *g = game(ui);
	int64_t n = status_unit(ui);
	if (hp_or_shields_changed(ui)) return 1;
	if (kp_has_cargo(g, n)) return refresh_transport(ui);
	return KP_DAT(g, units, supply_provided, g->u.type[n]) != 0 && refresh_supply(ui);
}

static int needs_refresh(KpUi *ui, uint32_t address)
{
	KpGame *g = game(ui);
	int64_t n = status_unit(ui);
	switch (address) {
	case 0x4a5160: return g->u.order[n] == ORDER_COMPLETING_ARCHON || refresh_default(ui);
	case 0x4a5180: return refresh_building(ui);
	case 0x4a51f0: return (owned(ui, n) && (uint16_t)queue_at(g, n, 0) <= TRAINABLE_LAST) || refresh_default(ui);
	case 0x4a52a0: return owned(ui, n) || refresh_default(ui);
	case 0x4a53a0: return refresh_overlord(ui);
	case 0x4a53f0: return ui->status.hp[0] != (int32_t)g->u.hp[n];
	case 0x4a5410: return refresh_transport(ui);
	case 0x4a55e0: return refresh_default(ui);
	default: return 0; /* 0x4b8e80 ReturnFalse */
	}
}

static void draw_layout(KpUi *ui, uint32_t address)
{
	switch (address) {
	case 0x4a3160: draw_archon(ui); break;
	case 0x4a3310: draw_building(ui); break;
	case 0x4a3540: draw_hangar(ui); break;
	case 0x4a3840: draw_egg(ui); break;
	case 0x4a3f00: draw_name_only(ui); break;
	case 0x4a3fd0: draw_overlord(ui); break;
	case 0x4a4030: draw_powerup(ui); break;
	case 0x4a4af0: draw_transport(ui); break;
	default: draw_generic(ui); break; /* 0x4a4e40 */
	}
}

static uint32_t layout_word(const KpUi *ui, int64_t t, int k)
{
	const uint8_t *p;
	if (ui->status_layouts == NULL || t < 0 || t >= ui->status_layout_count) return 0;
	p = ui->status_layouts + 8 * t + 4 * k;
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* --- the controls' drawing --- */

static int grp_size_of(const KpUiFile *f, int frame, int32_t *w, int32_t *h)
{
	return f == NULL ? -1 : kp_ui_grp_frame_size(f->data, f->size, frame, w, h);
}

static void draw_rle(KpUi *ui, int32_t x, int32_t y, const uint8_t *grp, size_t size, int frame, int32_t w, int32_t h)
{
	int32_t crop[4];
	crop[0] = 0;
	crop[1] = 0;
	crop[2] = w;
	crop[3] = h;
	kp_ui_draw_grp_rle(ui, x, y, grp, size, frame, crop);
}

/* DrawStatusButtonBackground 0x4a26c0: the race's cmdbtns.grp frame wUser (+1 pressed), centred. */
static void button_background(KpUi *ui, KpUiControl *c)
{
	const KpUiFile *f = ui->status.cmdbtns;
	int frame = c->wuser + ((c->flags & KP_UI_PRESSED) ? 1 : 0);
	int32_t fw, fh, cw = c->right - c->left + 1, ch = c->bottom - c->top + 1, w, h;
	if (grp_size_of(f, frame, &fw, &fh) != 0) return;
	w = fw < cw ? fw : cw;
	h = fh < ch ? fh : ch;
	draw_rle(ui, c->left + (cw - w) / 2, c->top + (ch - h) / 2, f->data, f->size, frame, w, h);
}

/* DrawStatusButtonIcon 0x4a2770: the record's frame through ticon's slice 3 (an enabled button) or 4. */
static void button_icon(KpUi *ui, KpUiControl *c)
{
	int k = (!(c->flags & KP_UI_DISABLED) && c->type == 2) ? 3 : 4, i;
	int32_t w, h;
	memcpy(ui->remap, ui->status.ticon + 16 * k, 16);
	if (c->slot.grp != NULL && kp_ui_grp_frame_size(c->slot.grp, c->slot.grp_size, c->slot.frame, &w, &h) == 0) {
		draw_rle(ui, c->left + (c->right - w - c->left + 1) / 2, c->top + (c->bottom - h - c->top + 1) / 2, c->slot.grp,
			c->slot.grp_size, c->slot.frame, w, h);
	}
	for (i = 0; i < 16; i++) ui->remap[i] = (uint8_t)i;
}

static void button_text(KpUi *ui, KpUiControl *c)
{
	if (c->text == NULL) return;
	(void)kp_ui_text_select_font(&ui->text, ui->fonts[0], ui->font_sizes[0]);
	ui->text.flags = 0x44;
	ui->text.clip_left = c->left;
	ui->text.clip_top = c->top;
	ui->text.clip_right = c->right;
	ui->text.clip_bottom = c->bottom;
	(void)kp_ui_draw_text(&ui->text, c->left, c->top, c->text);
	ui->text.font = NULL;
}

/* DrawStatusButtonBackThenIcon 0x4a2660. */
static void draw_back_then_icon(KpUi *ui, KpUiControl *c, int32_t x, int32_t y, const int16_t clip[4])
{
	(void)x;
	(void)y;
	(void)clip;
	button_background(ui, c);
	button_icon(ui, c);
	button_text(ui, c);
}

/* DrawStatusButtonIconThenBack 0x4a28e0 (slots 9..12). */
static void draw_icon_then_back(KpUi *ui, KpUiControl *c, int32_t x, int32_t y, const int16_t clip[4])
{
	(void)x;
	(void)y;
	(void)clip;
	button_icon(ui, c);
	button_background(ui, c);
	button_text(ui, c);
}

/* ResetWireframeRemap 0x4a7f10. */
static void reset_wire_remap(KpUi *ui)
{
	int i;
	for (i = 0; i < 4; i++) ui->remap[REMAP_HP + i] = (uint8_t)(REMAP_HP + i);
	for (i = 0; i < 2; i++) ui->remap[REMAP_SHIELDS + i] = (uint8_t)(REMAP_SHIELDS + i);
	for (i = 0; i < 4; i++) ui->remap[REMAP_OUTLINE + i] = (uint8_t)(REMAP_OUTLINE + i);
}

/* SetWireframeHitPointColors 0x4a7f50, SetWireframeShieldColors 0x4a80c0, SetWireframeOutlineColors 0x4a8030. */
static void wire_colours(KpUi *ui, int64_t n)
{
	KpGame *g = game(ui);
	uint8_t mark = ui->status.twire[TWIRE_MARK], random = (uint8_t)g->u.wireframe[n];
	int32_t m = max_hp(g, n), row, k;
	const uint8_t (*table)[4] = (random & 1) ? HP_COLOURS_A : HP_COLOURS_B;
	int64_t dt = displayed_type(g, n), max_shields;
	for (k = 0; k < 4; k++) ui->remap[REMAP_HP + k] = mark;
	row = hp_whole(g, n) * 9 / m;
	if (row < 0) row = 0;
	/* Above the maximum the original reads past its table (into the next tables, 0x50d804...); no run of the oracle's
	 * corpus (scenarios, console runs and the 69 plays) has a unit with 1.11 times its maximum */
	if (row > 9) row = 9;
	for (k = 0; k < 4; k++) {
		int s = (int)(((random >> k) & 7) * 3) / 7;
		while (ui->remap[REMAP_HP + s] != mark) s = s + 1 == 4 ? 0 : s + 1;
		ui->remap[REMAP_HP + s] = ui->status.twire[table[row][k] % 24];
	}
	max_shields = (uint16_t)KP_DAT(g, units, shield_amount, dt);
	if (KP_DAT(g, units, shield_enabled, g->u.type[n]) && max_shields != 0) {
		int32_t r = (((int32_t)g->u.shields[n] >> 8) * 7) / (int32_t)max_shields;
		if (r < 0) r = 0;
		if (r > 7) r = 7;
		ui->remap[REMAP_SHIELDS] = ui->status.twire[SHIELD_COLOURS[r][0]];
		ui->remap[REMAP_SHIELDS + 1] = ui->status.twire[SHIELD_COLOURS[r][1]];
	} else {
		ui->remap[REMAP_SHIELDS] = mark;
		ui->remap[REMAP_SHIELDS + 1] = mark;
	}
	row = hp_whole(g, n) * 5 / m;
	if (row < 0) row = 0;
	if (row > 5) row = 5;
	for (k = 0; k < 4; k++) ui->remap[REMAP_OUTLINE + k] = ui->status.twire[OUTLINE_COLOURS[row][k]];
}

static void draw_wire_centred(KpUi *ui, KpUiControl *c, const KpUiFile *f, int frame)
{
	int32_t w, h;
	if (grp_size_of(f, frame, &w, &h) != 0) return;
	draw_rle(ui, c->left + (c->right - w - c->left + 1) / 2, c->top + (c->bottom - h - c->top + 1) / 2, f->data, f->size,
		frame, w, h);
}

/* DrawSelectedWireframe 0x4a8150. */
static void draw_big_wireframe(KpUi *ui, KpUiControl *c, int32_t x, int32_t y, const int16_t clip[4])
{
	int64_t n = c->slot.unit;
	(void)x;
	(void)y;
	(void)clip;
	if (n == 0) return;
	wire_colours(ui, n);
	draw_wire_centred(ui, c, ui->status.wirefram, (int)displayed_type(game(ui), n));
	reset_wire_remap(ui);
}

/* DrawSelectionBox 0x4a20e0. */
static void draw_selection_box(KpUi *ui, KpUiControl *c, int32_t x, int32_t y, const int16_t clip[4])
{
	KpGame *g = game(ui);
	int64_t n = c->slot.unit;
	(void)x;
	(void)y;
	(void)clip;
	if (n == 0) return;
	if (hallucination(g, n)) c->wuser = 31;
	else if (special(g, g->u.type[n]) & FLAG_HERO) c->wuser = 19;
	else c->wuser = g->u.parasites[n] ? 25 : 13;
	button_background(ui, c);
	wire_colours(ui, n);
	draw_wire_centred(ui, c, ui->status.grpwire, c->slot.frame);
	reset_wire_remap(ui);
}

/* DrawCargoWireframe 0x4a29e0. */
static void draw_cargo_box(KpUi *ui, KpUiControl *c, int32_t x, int32_t y, const int16_t clip[4])
{
	int64_t n = c->slot.unit;
	(void)x;
	(void)y;
	(void)clip;
	button_background(ui, c);
	if (n == 0) return;
	wire_colours(ui, n);
	draw_wire_centred(ui, c, ui->status.tranwire, (int)game(ui)->u.type[n]);
	reset_wire_remap(ui);
}

/* DrawStatusFrame11 0x4a22b0. */
static void draw_frame_11(KpUi *ui, KpUiControl *c, int32_t x, int32_t y, const int16_t clip[4])
{
	const KpUiFile *f = ui->status.cmdbtns;
	int32_t fw, fh, cw = c->right - c->left + 1, ch = c->bottom - c->top + 1;
	(void)x;
	(void)y;
	(void)clip;
	if (grp_size_of(f, 11, &fw, &fh) != 0) return;
	draw_rle(ui, c->left + (c->right - fw - c->left) / 2, c->top, f->data, f->size, 11, fw < cw ? fw : cw,
		fh < ch ? fh : ch);
}

/* DrawStatusProgressBar 0x4a1c70 (and the command card's DrawCommandProgressBar 0x4a0920, over the same files): the
 * empty bar, then wUser percent of the full one. */
void kp_ui_draw_progress(KpUi *ui, KpUiControl *c, int32_t x, int32_t y, const int16_t clip[4])
{
	int16_t r[4];
	int32_t w = c->right - c->left + 1;
	(void)x;
	(void)y;
	(void)clip;
	r[0] = 0;
	r[1] = 0;
	r[2] = (int16_t)((uint16_t)(c->right - c->left + 1) - 1);
	r[3] = (int16_t)((uint16_t)(c->bottom - c->top + 1) - 1);
	if (ui->status.bar_empty.pixels) {
		kp_ui_blit_opaque(ui, (uint16_t)c->left, (uint16_t)c->top, ui->status.bar_empty.pixels, ui->status.bar_empty.w,
			ui->status.bar_empty.h, r);
	}
	r[2] = (int16_t)((int16_t)(w * c->wuser / 100) - 1);
	if (ui->status.bar_full.pixels) {
		kp_ui_blit_opaque(ui, (uint16_t)c->left, (uint16_t)c->top, ui->status.bar_full.pixels, ui->status.bar_full.w,
			ui->status.bar_full.h, r);
	}
}

/* --- the tooltips and the clicks (statdata.cpp; atlas P35, P36) --- */

/* A copy of fmt with its %s replaced by arg (the SStrPrintf of a stat_txt format with one string). */
static void format_s(char *out, size_t size, const uint8_t *fmt, const uint8_t *arg)
{
	size_t at = 0;
	if (size == 0) return;
	while (fmt && *fmt && at + 1 < size) {
		if (fmt[0] == '%' && fmt[1] == 's') {
			const uint8_t *a = arg;
			while (a && *a && at + 1 < size) out[at++] = (char)*a++;
			fmt += 2;
		} else {
			out[at++] = (char)*fmt++;
		}
	}
	out[at] = 0;
}

static const char *txt(KpUi *ui, uint32_t id)
{
	const uint8_t *s = stat_txt(ui, id);
	return s ? (const char *)s : "";
}

/* The tooltip at the control's right top on the screen (ShowControlTooltipText 0x4a1d80's place). */
static void tooltip_at(KpUi *ui, const KpUiControl *c, const uint8_t *text)
{
	int32_t x = c->right, y = c->top;
	if (c->type != 0 && c->parent >= 0) {
		x += ui->controls[c->parent].left;
		y += ui->controls[c->parent].top;
	}
	kp_ui_tooltip_text(ui, x, y, text);
}

/* The weapon's damage factor (0x420e40, 0x420eb0): 2 for the Firebats and Zealots, 1 for the Valkyrie. */
static uint32_t damage_factor(KpGame *g, int64_t n, int64_t w)
{
	int64_t t = g->u.type[n];
	uint32_t f = (uint8_t)KP_DAT(g, weapons, damage_factor, w);
	if (t == KP_TYPE_FIREBAT || t == KP_TYPE_GUI_MONTAG || t == KP_TYPE_ZEALOT || t == KP_TYPE_FENIX_ZEALOT) f = 2;
	if (t == KP_TYPE_VALKYRIE) f = 1;
	return f;
}

/* GetWeaponDamageBonus 0x420e40 and GetWeaponBaseDamage 0x420eb0 (WORDs as the tooltip takes them). */
static uint16_t weapon_bonus(KpGame *g, int64_t n, int64_t w)
{
	return (uint16_t)((uint32_t)(uint16_t)KP_DAT(g, weapons, damage_bonus, w)
		* (uint8_t)kp_upgrade_level(g, g->u.owner[n], KP_DAT(g, weapons, damage_upgrade, w)) * damage_factor(g, n, w));
}

static uint16_t weapon_damage(KpGame *g, int64_t n, int64_t w)
{
	return (uint16_t)((uint32_t)(uint16_t)KP_DAT(g, weapons, damage_amount, w) * damage_factor(g, n, w));
}

/* "%s\n%s %d+%d" or, without a bonus, "%s\n%s %d" (0x4eff3c, 0x4efe70). */
static void value_text(char *out, const char *label, const char *what, unsigned value, unsigned bonus)
{
	if (bonus != 0) snprintf(out, TOOLTIP_TEXT, "%s\n%s %u+%u", label, what, value, bonus);
	else snprintf(out, TOOLTIP_TEXT, "%s\n%s %u", label, what, value);
}

/* ShowWeaponArmorSlotTooltip 0x4a2bf0 (slots 9..12). */
static void weapon_armor_tooltip(KpUi *ui, const KpUiControl *c)
{
	KpGame *g = game(ui);
	int64_t n = status_unit(ui), w;
	char buf[TOOLTIP_TEXT];
	unsigned b;
	switch (c->slot.kind) {
	case K_WEAPON:
		w = (uint8_t)c->slot.id;
		b = weapon_bonus(g, n, w);
		if (c->slot.id == WEAPON_HALO_ROCKETS) {
			const char *label = txt(ui, (uint32_t)KP_DAT(g, weapons, label, w)), *per = txt(ui, S_PER_ROCKET);
			if (b != 0) snprintf(buf, sizeof buf, "%s\n%s %u+%u %s", label, txt(ui, S_DAMAGE), weapon_damage(g, n, w), b, per);
			else snprintf(buf, sizeof buf, "%s\n%s %u %s", label, txt(ui, S_DAMAGE), weapon_damage(g, n, w), per);
		} else {
			value_text(buf, txt(ui, (uint32_t)KP_DAT(g, weapons, label, w)), txt(ui, S_DAMAGE), weapon_damage(g, n, w), b);
		}
		break;
	case K_ARMOR:
		b = (uint8_t)kp_armor_bonus(g, n);
		value_text(buf, txt(ui, (uint32_t)KP_DAT(g, upgrades, label, c->slot.id)), txt(ui, S_ARMOR),
			(uint8_t)KP_DAT(g, units, armor, g->u.type[n]), b);
		break;
	case K_SHIELDS:
		b = (uint8_t)kp_upgrade_level(g, g->u.owner[n], UPG_PLASMA_SHIELDS);
		value_text(buf, txt(ui, (uint32_t)KP_DAT(g, upgrades, label, c->slot.id)), txt(ui, S_SHIELDS), 0, b);
		break;
	case K_COUNT: {
		static const struct { uint16_t id; uint8_t weapon; uint16_t label; } COUNTS[3] = {
			{ KP_TYPE_SPIDER_MINE, KP_WEAPON_SPIDER_MINES, S_SPIDER_MINES },
			{ KP_TYPE_INTERCEPTOR, WEAPON_PULSE_CANNON, S_INTERCEPTORS }, { KP_TYPE_SCARAB, KP_WEAPON_SCARAB, S_SCARABS } };
		int i;
		if (c->slot.id == KP_TYPE_NUCLEAR_MISSILE) {
			tooltip_at(ui, c, stat_txt(ui, S_NUKES));
			return;
		}
		for (i = 0; i < 3 && COUNTS[i].id != c->slot.id; i++) {}
		if (i == 3) return;
		value_text(buf, txt(ui, COUNTS[i].label), txt(ui, S_DAMAGE), weapon_damage(g, n, COUNTS[i].weapon),
			weapon_bonus(g, n, COUNTS[i].weapon));
		tooltip_at(ui, c, (const uint8_t *)buf);
		return;
	}
	default:
		return;
	}
	tooltip_at(ui, c, (const uint8_t *)buf);
}

/* ShowStatusTooltip 0x4a2380 by the control's index. */
static void status_tooltip(KpUi *ui, int32_t k)
{
	KpGame *g = game(ui);
	const KpUiControl *c;
	int64_t n = status_unit(ui);
	char buf[TECH_TOOLTIP_TEXT];
	if (n == 0 || k < 0 || k == ui->status.panel) {
		kp_ui_tooltip_hide(ui);
		return;
	}
	c = &ui->controls[k];
	if (g->w->replay && (c->id < C_SLOT || c->id > C_SLOT + 3)) {
		kp_ui_tooltip_hide(ui);
		return;
	}
	if ((c->id >= C_QUEUE && c->id <= C_QUEUE + 4) || c->id == C_ADDON) {
		/* ShowQueueSlotTooltip 0x4a19e0 */
		int64_t t = g->u.type[n];
		if (c->slot.kind == K_COUNT
			&& (!(t == TYPE_EGG || t == TYPE_COCOON || t == TYPE_LURKER_EGG) || !disabled(g, n))) {
			format_s(buf, TOOLTIP_TEXT, stat_txt(ui, S_QUEUED + (uint32_t)race_of(g, n)),
				kp_ui_unit_type_name(ui, c->slot.id));
			tooltip_at(ui, c, (const uint8_t *)buf);
		} else {
			kp_ui_tooltip_hide(ui);
		}
	} else if (c->id >= C_SLOT && c->id <= C_SLOT + 3) {
		weapon_armor_tooltip(ui, c);
	} else if (c->id == C_RESEARCH) {
		/* ShowTechUpgradeCancelTooltip 0x4a2f50 */
		if (c->slot.kind == K_TECH) {
			snprintf(buf, sizeof buf, "%s\n(%s)", txt(ui, S_CANCEL_RESEARCH),
				txt(ui, (uint32_t)KP_DAT(g, techdata, label, c->slot.id)));
			tooltip_at(ui, c, (const uint8_t *)buf);
		} else if (c->slot.kind == K_UPGRADE) {
			snprintf(buf, sizeof buf, "%s\n(%s)", txt(ui, S_CANCEL_UPGRADE),
				txt(ui, (uint32_t)KP_DAT(g, upgrades, label, c->slot.id)));
			tooltip_at(ui, c, (const uint8_t *)buf);
		}
	} else if (c->id >= C_CARGO_FIRST && c->id <= C_CARGO_LAST) {
		/* ShowUnloadUnitTooltip 0x4a3000 */
		format_s(buf, TOOLTIP_TEXT, stat_txt(ui, S_UNLOAD), c->slot.unit ? unit_name(ui, c->slot.unit) : NULL);
		tooltip_at(ui, c, (const uint8_t *)buf);
	} else if (c->id >= C_BOX_FIRST && c->id <= C_BOX_LAST) {
		tooltip_at(ui, c, stat_txt(ui, S_SELECT));
	} else {
		kp_ui_tooltip_hide(ui);
	}
}

/* The value the hover caches (the control's lUser: a cargo box's passenger, else the control's own record). */
static uint32_t hover_user(const KpUi *ui, int32_t k)
{
	const KpUiControl *c = &ui->controls[k];
	return (c->id >= C_CARGO_FIRST && c->id <= C_CARGO_LAST) ? (uint32_t)c->slot.unit : (uint32_t)k + 1;
}

/* StatusControlMouseMove 0x4a1ae0. */
static void control_mouse_move(KpUi *ui, KpUiControl *c, KpUiEvent *e)
{
	KpUiStatus *s = &ui->status;
	int32_t self = (int32_t)(c - ui->controls);
	if (kp_scalar(game(ui), KP_PAUSED) != 0 || kp_ui_hit_test(ui, c->parent, e) != self) {
		s->hovered = -1;
		s->hovered_user = 0xffffffffu;
		kp_ui_tooltip_hide(ui);
	} else if (s->hovered != self) {
		s->hovered = self;
		s->hovered_user = hover_user(ui, self);
		status_tooltip(ui, self);
	}
}

/* UpdateStatusPanelHover 0x4a1fa0. */
static void panel_hover(KpUi *ui, KpUiEvent *e)
{
	KpUiStatus *s = &ui->status;
	int32_t h = kp_scalar(game(ui), KP_PAUSED) != 0 ? -1 : kp_ui_hit_test(ui, s->panel, e);
	if (h == s->hovered && (h < 0 || hover_user(ui, h) == s->hovered_user)) return;
	s->hovered = h;
	if (h >= 0) s->hovered_user = hover_user(ui, h);
	status_tooltip(ui, h);
}

/* RefreshStatusHoverTooltip 0x4a2b70: the panel's hover again at the mouse (0x6d3154) after the panel's new layout;
 * a point the layout put on none of its panels hovers nothing. */
static void refresh_hover(KpUi *ui)
{
	KpUiEvent e;
	memset(&e, 0, sizeof e);
	e.x = (int16_t)ui->mouse_x;
	e.y = (int16_t)ui->mouse_y;
	if (ui->input.over_console >= 0) e.x = e.y = -1;
	panel_hover(ui, &e);
}

/* User code 6 of the slots, the cargo and the selection boxes: the hovered control's tooltip goes. */
static void hover_lost(KpUi *ui, KpUiControl *c)
{
	if (ui->status.hovered == (int32_t)(c - ui->controls)) {
		kp_ui_tooltip_hide(ui);
		ui->status.hovered = -1;
	}
}

/* StatDataSendCancelCommand 0x4a25b0. */
static void send_cancel(KpUi *ui, KpUiControl *c)
{
	KpGame *g = game(ui);
	int64_t n = status_unit(ui);
	uint8_t cmd[3];
	if (g->w->replay) return;
	if (c->id >= C_QUEUE && c->id <= C_QUEUE + 4) {
		cmd[0] = CMD_CANCEL_TRAIN;
		cmd[1] = (uint8_t)(c->id - C_QUEUE);
		cmd[2] = 0;
		kp_ui_queue_command(ui, cmd, 3);
	} else if (c->id == C_RESEARCH) {
		if (ui->status.mode == M_RESEARCH) cmd[0] = CMD_CANCEL_RESEARCH;
		else if (ui->status.mode == M_UPGRADE) cmd[0] = CMD_CANCEL_UPGRADE;
		else return;
		kp_ui_queue_command(ui, cmd, 1);
	} else if (c->id == C_ADDON && n != 0) {
		int64_t t = g->u.type[n];
		if (!(t == TYPE_EGG || t == TYPE_COCOON || t == TYPE_LURKER_EGG)) cmd[0] = CMD_CANCEL_ADDON;
		else if (!disabled(g, n)) cmd[0] = CMD_CANCEL_CONSTRUCTION;
		else return;
		kp_ui_queue_command(ui, cmd, 1);
	}
}

/* SelectionBoxClick 0x4a2000: Shift drops the box's unit, Ctrl keeps the boxes of its type, else the unit alone. */
static void box_click(KpUi *ui, KpUiControl *c)
{
	KpGame *g = game(ui);
	int64_t list[KP_UI_SLOTS], u = c->slot.unit;
	int n = 0;
	int32_t k;
	if (ui->input.keys[0] || ui->input.keys[1]) {
		for (k = kp_ui_find_control(ui, ui->status.panel, C_BOX_FIRST); k >= 0 && n < KP_UI_SLOTS;
			k = ui->controls[k].next) {
			const KpUiControl *b = &ui->controls[k];
			if (b->id < C_BOX_FIRST || b->id > C_BOX_LAST) break;
			if (!(b->flags & KP_UI_VISIBLE) || b->slot.unit == 0) continue;
			if (ui->input.keys[0] ? b != c : g->u.type[b->slot.unit] == g->u.type[u]) list[n++] = b->slot.unit;
		}
	} else if (u != 0) {
		list[n++] = u;
	}
	kp_ui_select_units(ui, list, n, 0);
}

/* --- the controls' procedures --- */

static int stock(KpUi *ui, KpUiControl *c, KpUiEvent *e) { return kp_ui_type_interact(c->type)(ui, c, e); }

static int user(const KpUiEvent *e, uint32_t code) { return e->type == KP_UI_USER && e->code == code; }

/* WireframeControlInteract 0x4a82e0. */
static int wireframe_interact(KpUi *ui, KpUiControl *c, KpUiEvent *e)
{
	if (user(e, KP_UI_CREATE)) {
		c->user = 0;
		c->slot.unit = 0;
		c->draw = draw_big_wireframe;
		ui->status.wireframe = (int32_t)(c - ui->controls);
		return 1;
	}
	return stock(ui, c, e);
}

/* StatSlotInteract 0x4a2830 (slots 2..6, 9..12, 15, 17). */
static int slot_interact(KpUi *ui, KpUiControl *c, KpUiEvent *e)
{
	if (user(e, KP_UI_CREATE)) { /* InitStatusSlotRecord 0x4a2570 */
		c->draw = (c->id >= C_SLOT && c->id < C_SLOT + 4) ? draw_icon_then_back : draw_back_then_icon;
		c->wuser = 0;
		memset(&c->slot, 0, sizeof c->slot);
	} else if (user(e, 4)) {
		if (c->flags & KP_UI_VISIBLE) return 1;
	} else if (user(e, KP_UI_CLOSE)) {
		send_cancel(ui, c);
		return 1;
	} else if (user(e, KP_UI_FOCUS_LOST)) {
		hover_lost(ui, c);
	} else if (e->type == KP_UI_MOUSE_MOVE) {
		control_mouse_move(ui, c, e);
	}
	return stock(ui, c, e);
}

/* StatusProgressBarInteract 0x4a1d40 (7, 13, 14, 16). */
static int progress_interact(KpUi *ui, KpUiControl *c, KpUiEvent *e)
{
	if (user(e, KP_UI_CREATE)) { /* InitStatusProgressBar 0x4a1b60 */
		c->draw = kp_ui_draw_progress;
		c->wuser = 0;
	}
	return stock(ui, c, e);
}

/* StatusFrameControlInteract 0x4a2350 (8). */
static int frame_interact(KpUi *ui, KpUiControl *c, KpUiEvent *e)
{
	if (user(e, KP_UI_CREATE)) {
		c->draw = draw_frame_11;
		c->interact = kp_ui_type_interact(c->type);
	}
	return stock(ui, c, e);
}

/* CargoSlotInteract 0x4a2940 (18..32). */
static int cargo_interact(KpUi *ui, KpUiControl *c, KpUiEvent *e)
{
	if (user(e, KP_UI_CREATE)) {
		c->draw = draw_cargo_box;
		c->wuser = 0;
		c->user = 0;
		c->slot.unit = 0;
	} else if (user(e, 4)) {
		if (c->flags & KP_UI_VISIBLE) return 1;
	} else if (user(e, KP_UI_CLOSE)) { /* SendUnloadUnitCommand 0x473dc0 */
		uint8_t cmd[3];
		uint16_t ref = c->slot.unit ? kp_ui_unit_ref(game(ui), c->slot.unit) : 0;
		cmd[0] = CMD_UNLOAD;
		cmd[1] = (uint8_t)ref;
		cmd[2] = (uint8_t)(ref >> 8);
		kp_ui_queue_command(ui, cmd, sizeof cmd);
		return 1;
	} else if (user(e, KP_UI_FOCUS_LOST)) {
		hover_lost(ui, c);
	} else if (e->type == KP_UI_MOUSE_MOVE) {
		control_mouse_move(ui, c, e);
	}
	return stock(ui, c, e);
}

/* SelectionBoxInteract 0x4a21d0 (33..44). */
static int box_interact(KpUi *ui, KpUiControl *c, KpUiEvent *e)
{
	if (user(e, KP_UI_CREATE)) {
		c->draw = draw_selection_box;
		c->wuser = 0;
		memset(&c->slot, 0, sizeof c->slot);
	} else if (user(e, 4)) {
		if (c->flags & KP_UI_VISIBLE) return 1;
	} else if (user(e, KP_UI_CLOSE)) {
		box_click(ui, c);
		return 1;
	} else if (user(e, KP_UI_FOCUS_LOST)) {
		hover_lost(ui, c);
	} else if (e->type == KP_UI_MOUSE_MOVE) {
		control_mouse_move(ui, c, e);
	}
	return stock(ui, c, e);
}

/* StatusDataDialogProc 0x4a1db0. */
static int panel_interact(KpUi *ui, KpUiControl *d, KpUiEvent *e)
{
	static const KpUiInteract TABLE[44] = { wireframe_interact, slot_interact, slot_interact, slot_interact,
		slot_interact, slot_interact, progress_interact, frame_interact, slot_interact, slot_interact, slot_interact,
		slot_interact, progress_interact, progress_interact, slot_interact, progress_interact, slot_interact,
		cargo_interact, cargo_interact, cargo_interact, cargo_interact, cargo_interact, cargo_interact, cargo_interact,
		cargo_interact, cargo_interact, cargo_interact, cargo_interact, cargo_interact, cargo_interact, cargo_interact,
		cargo_interact, box_interact, box_interact, box_interact, box_interact, box_interact, box_interact, box_interact,
		box_interact, box_interact, box_interact, box_interact, box_interact };
	int32_t self = (int32_t)(d - ui->controls);
	switch (e->type) {
	case KP_UI_KEY_DOWN: case KP_UI_KEY_REPEAT: case KP_UI_CHAR:
		return 0;
	case KP_UI_USER:
		if (e->code == KP_UI_CREATE) {
			kp_ui_bind_interacts(ui, self, TABLE, 44);
			kp_ui_capture_console(ui, self);
			ui->hud.status_refresh = 1;
		}
		break;
	case KP_UI_MOUSE_MOVE:
		panel_hover(ui, e);
		break;
	default:
		break;
	}
	return kp_ui_type_interact(0)(ui, d, e);
}

static int load_pcx(KpUi *ui, const char *path, KpUiSurface *s, uint8_t *head, size_t head_size)
{
	const KpUiFile *f = kp_ui_file(ui->pcx, ui->pcx_count, path);
	int32_t w, h;
	uint8_t *pixels;
	if (f == NULL || kp_ui_pcx(f->data, f->size, &w, &h, NULL, 0) != 0) return -1;
	pixels = (uint8_t *)malloc((size_t)w * (size_t)h);
	if (pixels == NULL || kp_ui_pcx(f->data, f->size, &w, &h, pixels, (size_t)w * (size_t)h) != 0) {
		free(pixels);
		return -1;
	}
	if (head != NULL) {
		size_t n = (size_t)w * (size_t)h < head_size ? (size_t)w * (size_t)h : head_size;
		memcpy(head, pixels, n);
		free(pixels);
		return 0;
	}
	kp_ui_surface_free(ui, s->pixels);
	s->pixels = pixels;
	s->w = w;
	s->h = h;
	kp_ui_picture(ui, pixels, w, h, f); /* the HD interface's (ADR 0013 R4d): the progress bars */
	return 0;
}

int kp_ui_status_create(KpUi *ui)
{
	KpGame *g = game(ui);
	int race = (int)g->players.race[g->local_player];
	char path[64];
	int32_t d;
	KpUiStatus *s = &ui->status;
	if (race < 0 || race > 2) race = 1;
	/* InitCommandCard 0x4a0f20's art, which the status panel shares */
	snprintf(path, sizeof path, "unit\\cmdbtns\\%ccmdbtns.grp", RACE_LETTERS[race]);
	s->cmdbtns = kp_ui_file(ui->grps, ui->grp_count, path);
	s->cmdicons = kp_ui_file(ui->grps, ui->grp_count, "unit\\cmdbtns\\cmdicons.grp");
	if (load_pcx(ui, "unit\\cmdbtns\\ticon.pcx", NULL, s->ticon, sizeof s->ticon) != 0) return -1;
	/* InitStatusPanel 0x4a1f10 */
	s->tranwire = kp_ui_file(ui->grps, ui->grp_count, "unit\\wirefram\\tranwire.grp");
	s->grpwire = kp_ui_file(ui->grps, ui->grp_count, "unit\\wirefram\\grpwire.grp");
	/* InitStatusProgressBar 0x4a1b60 loads these on its first bar */
	snprintf(path, sizeof path, "game\\%cpbrempt.pcx", RACE_LETTERS[race]);
	if (load_pcx(ui, path, &s->bar_empty, NULL, 0) != 0) return -1;
	snprintf(path, sizeof path, "game\\%cpbrfull.pcx", RACE_LETTERS[race]);
	if (load_pcx(ui, path, &s->bar_full, NULL, 0) != 0) return -1;
	ui->hud.status_refresh = 1;
	s->mode = M_NAME;
	s->hovered = -1;
	s->hovered_user = 0xffffffffu;
	s->wireframe = -1;
	d = kp_ui_load_dialog(ui, "rez\\statdata.bin");
	if (d < 0) return -1;
	s->panel = d;
	kp_ui_init_dialog(ui, d, panel_interact);
	/* LoadWireframeData 0x4a8260 */
	s->wirefram = kp_ui_file(ui->grps, ui->grp_count, "unit\\wirefram\\wirefram.grp");
	if (load_pcx(ui, "game\\twire.pcx", NULL, s->twire, sizeof s->twire) != 0) return -1;
	return s->cmdbtns && s->cmdicons && s->tranwire && s->grpwire && s->wirefram ? 0 : -1;
}

/* UpdateSelectedWireframeControl 0x4a82a0. */
static void update_wireframe(KpUi *ui)
{
	int32_t k = ui->status.wireframe;
	if (k < 0) return;
	at(ui, k)->user = (uint32_t)status_unit(ui);
	at(ui, k)->slot.unit = status_unit(ui);
	if (status_unit(ui) != 0) show(ui, k);
	else hide(ui, k);
	dirty(ui, k);
}

void kp_ui_status_frame(KpUi *ui)
{
	/* UpdateStatusDataPanel 0x4a2aa0 */
	int64_t n = status_unit(ui);
	if (ui->game == NULL || ui->status.panel <= 0) return;
	if (n == 0) {
		if (ui->hud.status_refresh) {
			hide_all(ui);
			ui->status.mode = M_NAME;
		}
	} else if (ui->hud.selection_count == 1) {
		int64_t t = game(ui)->u.type[n];
		if (ui->hud.status_refresh || needs_refresh(ui, layout_word(ui, t, 0))) {
			draw_layout(ui, layout_word(ui, t, 1));
			update_wireframe(ui);
			refresh_hover(ui);
		}
	} else {
		int i, changed = 0;
		for (i = 0; i < KP_UI_SLOTS && !changed; i++) { /* MultiSelectionChanged 0x4a52d0 */
			int64_t m = ui->hud.selection[i];
			if (m != 0 && (ui->status.hp[i] != (int32_t)game(ui)->u.hp[m] || ui->status.types[i] != (uint16_t)game(ui)->u.type[m])) {
				changed = 1;
			}
		}
		if (ui->hud.status_refresh || changed) draw_multi(ui);
	}
	ui->hud.status_refresh = 0;
}
