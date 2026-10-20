/* Units created from the map (kp_game.h). */
#include "kp_game_internal.h"

#define EXPANSION_ONLY 0x200
#define AVAILABLE 0x2 /* the StarEdit availability bit a created type needs (0x524708) */
#define DAT_BUILDING 0x1
#define DAT_ADDON 0x2
#define GEYSER_AMOUNT 5000
#define MINERAL_AMOUNT 1500
#define STATUS_COMPLETED 0x1
#define STATUS_GROUNDED_BUILDING 0x2
#define CREEP_FLAG 0x20000
#define RESOURCE_CONTAINER 0x2000
#define VALID_HP 0x20000
#define VALID_SHIELDS 0x40000
#define VALID_ENERGY 0x80000
#define VALID_RESOURCES 0x100000
#define VALID_HANGAR 0x200000
#define STATE_CLOAKED 1
#define STATE_BURROWED 2
#define STATE_LIFTED 4
#define STATE_HALLUCINATED 8
#define STATE_INVINCIBLE 0x10
#define STATUS_HALLUCINATED 0x40000000
#define STATUS_CLOAKED 0x100
#define SHADOW_METHOD 10
#define LIFTED_SHADOW_Y 0x2a

/* A Refinery, an Extractor or an Assimilator (IsCompletedGasBuilding 0x424300 without its completion test). */
int kp_gas_building(int64_t t) { return t == KP_TYPE_REFINERY || t == KP_TYPE_EXTRACTOR || t == KP_TYPE_ASSIMILATOR; }
/* IsMineralField 0x424380. */
int kp_mineral_field(int64_t t) { return t >= KP_TYPE_FIRST_MINERAL && t <= KP_TYPE_LAST_MINERAL; }
/* IsResourceType 0x4243e0 / IsResourceTypeId 0x424420: a mineral field or a geyser. */
int kp_resource_type(int64_t t) { return kp_mineral_field(t) || t == KP_TYPE_GEYSER; }

static int creep_building(int64_t t)
{
	return t == KP_TYPE_LAIR || t == KP_TYPE_HIVE || t == KP_TYPE_SPORE_COLONY || t == KP_TYPE_SUNKEN_COLONY;
}
static int creep_morph(int64_t t) { return t == KP_TYPE_HATCHERY || t == KP_TYPE_CREEP_COLONY; }
static int doodad_trap(int64_t t)
{
	return t == KP_TYPE_FLOOR_MISSILE_TRAP || (t >= KP_TYPE_FLOOR_GUN_TRAP && t <= KP_TYPE_LAST_TRAP);
}
static int doodad_low(int64_t t)
{
	return t == KP_TYPE_FLOOR_MISSILE_TRAP || (t >= KP_TYPE_UPPER_LEVEL_DOOR && t <= KP_TYPE_FLOOR_GUN_TRAP);
}

/* GetHangarChildType 0x41cc20: Interceptor for a Carrier or Gantrithor, Scarab for a Reaver or Warbringer, else 0. */
int64_t kp_hangar_child_type(int64_t t)
{
	if (t == KP_TYPE_CARRIER || t == KP_TYPE_GANTRITHOR) return KP_TYPE_INTERCEPTOR;
	if (t == KP_TYPE_REAVER || t == KP_TYPE_WARBRINGER) return KP_TYPE_SCARAB;
	return 0;
}

int64_t kp_create_map_unit(KpGame *g, int64_t t, int64_t x, int64_t y, int64_t owner)
{
	int64_t n;
	if (kp_band(KP_DAT(g, units, staredit_availability_flags, t), EXPANSION_ONLY) != 0 && g->expansion == 0) return 0;
	n = kp_unit_create(g, t, x, y, owner);
	if (n == 0) {
		kp_report_error(g, owner, kp_scalar(g, KP_ERROR_CODE));
		return 0;
	}
	if (creep_building(t) || creep_morph(t) || kp_band(KP_DAT(g, units, special_ability_flags, t), CREEP_FLAG) != 0) {
		int32_t s = kp_unit_sprite(g, n);
		kp_creep_place_initial(g, t, kp_s16v(g->w->spr.x[s]), kp_s16v(g->w->spr.y[s]));
	}
	if (!kp_unit_complete(g, n)) {
		/* The original shows a 7-second text line for the local player (0x46b070) and kills
		 * the unit (0x425700). */
		kp_game_text(g, owner, "placement failed");
		kp_kill_flagged(g, n);
		return 0;
	}
	kp_guard_attach(g, n);
	return n;
}

/* A unit's center (x, y) of type t kept inside the map by its extents, in 16-bit arithmetic (0x4261f0, 0x4b7810). */
void kp_clamp_center(KpGame *g, int64_t t, int64_t *x, int64_t *y)
{
	int64_t ext[4], d;
	int k;
	for (k = 0; k < 4; k++) ext[k] = KP_DAT(g, units, unit_extents, t * 4 + k);
	d = kp_s16v(*x - ext[0]);
	if (d < 0) *x -= d;
	else if (kp_s16v(*x + ext[2]) >= g->map_w_px) *x += g->map_w_px - kp_s16v(*x + ext[2]) - 1;
	d = kp_s16v(*y - ext[1]);
	if (d < 0) *y -= d;
	else if (kp_s16v(*y + ext[3]) >= g->map_h_px) *y += g->map_h_px - kp_s16v(*y + ext[3]) - 1;
}

/* The creep under a unit made or moved by a trigger (0x4261f0, 0x4b7810): a creep provider stamps its own
 * (StampBuildingCreep 0x43c220), a type that needs creep gets its starting creep (PlaceInitialCreep 0x4c0aa0), and the
 * creep under it stops receding (0x4b4dd0); 1 for a provider. */
int kp_creep_under(KpGame *g, int64_t n)
{
	int64_t t = g->u.type[n];
	int32_t s = kp_unit_sprite(g, n);
	int64_t x = kp_s16v(g->w->spr.x[s]), y = kp_s16v(g->w->spr.y[s]);
	if (kp_creep_provider_state(t, 1)) kp_creep_stamp_building(g, t, x, y);
	else if (kp_band(KP_DAT(g, units, special_ability_flags, t), CREEP_FLAG) != 0) kp_creep_place_initial(g, t, x, y);
	else return 0;
	kp_creep_restore_provider(g, n);
	return kp_creep_provider_state(t, 1);
}

/* AttachNearbyAddon 0x437180 for a grounded Terran building: a neutral add-on next to it joins it. */
void kp_attach_nearby_addon(KpGame *g, int64_t n)
{
	int64_t a;
	if (kp_band(g->u.status[n], STATUS_GROUNDED_BUILDING) == 0 || kp_unit_race(g, n) != KP_RACE_TERRAN) return;
	a = kp_find_attachable_addon(g, n);
	if (a != 0) kp_attach_addon(g, n, a);
}

/* CreateUnitWithPlacementCheck 0x4261f0 (trigger and AI creation): a unit of an available type made at (x, y) kept
 * inside the map, placed and completed; 0 when it cannot be made or placed (a placed unit that does not fit dies). */
int64_t kp_create_unit_checked(KpGame *g, int64_t t, int64_t x, int64_t y, int64_t owner)
{
	KpUnits *u = &g->u;
	int64_t n;
	if (t < 0 || t >= KP_UNIT_TYPES || kp_band(KP_DAT(g, units, staredit_availability_flags, t), AVAILABLE) == 0) return 0;
	kp_clamp_center(g, t, &x, &y);
	if (kp_band(KP_DAT(g, units, special_ability_flags, t), DAT_BUILDING) != 0) {
		int64_t tx, ty;
		kp_snap_building(g, t, &x, &y, &tx, &ty, 1);
		if (kp_validate_grounded(g, owner, tx, ty, t) != 0) { /* 0x412e20 */
			kp_game_text(g, owner, "placement failed"); /* string 0x12 with the name and position, 7 s (0x46b070) */
			return 0;
		}
	}
	n = kp_unit_create(g, t, x, y, owner);
	if (n == 0) {
		kp_report_error(g, owner, kp_scalar(g, KP_ERROR_CODE));
		return 0;
	}
	kp_creep_under(g, n); /* before the placement, as the original's: a unit that does not fit leaves its creep */
	kp_unit_prefinish(g, n);
	if (!kp_unit_place(g, n)) {
		kp_game_text(g, owner, "placement failed"); /* string 0x12 with the name and position, 7 s (0x46b070) */
		kp_unit_kill(g, n);
		return 0;
	}
	kp_unit_finish_completion(g, n);
	if (kp_creep_provider_state(t, 1)) kp_creep_restore_provider(g, n); /* 0x4b4dd0 */
	if (kp_band(KP_DAT(g, units, special_ability_flags, u->type[n]), DAT_ADDON) != 0) kp_change_owner(g, n, KP_PLAYER_NEUTRAL, 0); /* 0x426ed0 */
	kp_attach_nearby_addon(g, n);
	kp_assign_unit_ai(g, n);
	kp_scalar_set(g, KP_POWER_DIRTY, 1); /* MarkPowerDirty 0x42ffc0 */
	if (t == KP_TYPE_GEYSER || (kp_band(u->status[n], STATUS_COMPLETED) != 0 && kp_gas_building(t))) { /* 0x424300 */
		u->union_0xd0[n] = kp_bor(kp_band(u->union_0xd0[n], kp_bnot(0xffff)), GEYSER_AMOUNT);
	} else if (kp_mineral_field(t)) {
		u->union_0xd0[n] = kp_bor(kp_band(u->union_0xd0[n], kp_bnot(0xffff)), MINERAL_AMOUNT);
		kp_update_mineral_animation(g, n);
	}
	return n;
}

void kp_apply_unit_record(KpGame *g, int64_t n, const KpUnitRecord *r)
{
	KpUnits *u = &g->u;
	int64_t t = u->type[n], valid = r->valid_states + r->valid_properties * 65536;
	int64_t max_shields, shields, max_energy;
	if (kp_band(valid, VALID_HP) != 0) {
		int64_t hp = kp_udiv64(g, kp_mulv(r->hp_percent, KP_DAT(g, units, hit_points, t)), 100);
		kp_set_unit_hp(g, n, hp != 0 ? hp : 1);
	}
	max_shields = KP_DAT(g, units, shield_amount, t);
	shields = kp_udiv64(g, r->shield_percent * max_shields, 100);
	if (kp_band(valid, VALID_SHIELDS) != 0) {
		if (max_shields < shields) shields = max_shields;
		u->shields[n] = shields * 256;
	}
	max_energy = kp_sarv(kp_max_energy(g, n), 8);
	if (kp_band(valid, VALID_ENERGY) != 0) {
		int64_t e = kp_mod(kp_udiv64(g, max_energy * r->energy_percent, 100), 256);
		if (max_energy < e) e = max_energy;
		u->energy[n] = kp_mod(e * 256, 65536);
	}
	if (kp_band(valid, VALID_RESOURCES) != 0 && kp_band(KP_DAT(g, units, special_ability_flags, t), RESOURCE_CONTAINER) != 0) {
		u->union_0xd0[n] = kp_band(u->union_0xd0[n], 0xffff0000) + kp_mod(r->resources, 65536);
		if (kp_mineral_field(t)) kp_update_mineral_animation(g, n);
	}
	if (kp_band(valid, VALID_HANGAR) != 0) kp_create_hangar_units(g, n, kp_mod(r->hangar, 256));
	if (kp_band(valid, STATE_CLOAKED) != 0 && kp_band(r->states, STATE_CLOAKED) != 0) {
		if (kp_can_use_tech(g, kp_cloak_tech(g, n), u->owner[n], n) == 1) kp_cloak_hidden(g, n);
	}
	if (kp_band(valid, STATE_BURROWED) != 0 && kp_band(r->states, STATE_BURROWED) != 0
		&& kp_can_use_tech(g, KP_TECH_BURROWING, u->owner[n], n) == 1) {
		kp_sprite_play(g->w, kp_unit_sprite(g, n), KP_ANIM_SPECIAL2, 1);
		kp_set_burrowed(g, n);
		kp_cloak_hidden(g, n);
		u->status[n] = kp_bor(u->status[n], 0x200000);
		kp_issue_order(g, n, KP_ORD_BURROWED);
	}
	if (kp_band(valid, STATE_LIFTED) != 0 && kp_band(r->states, STATE_LIFTED) != 0) kp_lift_off_initial(g, n);
	if (kp_band(valid, STATE_HALLUCINATED) != 0 && kp_band(r->states, STATE_HALLUCINATED) != 0) {
		kp_convert_to_hallucination(g, n);
	}
	if (kp_band(valid, STATE_INVINCIBLE) != 0 && kp_band(r->states, STATE_INVINCIBLE) != 0) {
		u->status[n] = kp_bor(u->status[n], 0x4000000);
	}
	if (t == KP_TYPE_BROODLING) kp_game_fail(g, "map units: Broodling timer (0x434a90)");
}

int64_t kp_hangar_capacity(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t child = kp_hangar_child_type(u->type[n]);
	/* normal, upgraded, hero capacity and the capacity upgrade of each child */
	int64_t normal = child == KP_TYPE_INTERCEPTOR ? 4 : 5, upgraded = child == KP_TYPE_INTERCEPTOR ? 8 : 10,
		upgrade = child == KP_TYPE_INTERCEPTOR ? KP_UPG_CARRIER_CAPACITY : KP_UPG_REAVER_CAPACITY;
	if (child == 0) return 0;
	if (kp_band(KP_DAT(g, units, special_ability_flags, u->type[n]), 0x40) != 0) return upgraded;
	return kp_upgrade_level(g, u->owner[n], upgrade) != 0 ? upgraded : normal;
}

void kp_create_hangar_units(KpGame *g, int64_t n, int64_t count)
{
	KpUnits *u = &g->u;
	int64_t child_type = kp_hangar_child_type(u->type[n]), capacity, x, y, k;
	int32_t s;
	if (child_type == 0) return;
	capacity = kp_hangar_capacity(g, n);
	if (count > capacity) count = capacity;
	s = kp_unit_sprite(g, n);
	x = kp_s16v(g->w->spr.x[s]);
	y = kp_s16v(g->w->spr.y[s]);
	for (k = 1; k <= count; k++) {
		int64_t c = kp_unit_create(g, child_type, x, y, u->owner[n]);
		if (c != 0) {
			kp_unit_prefinish(g, c);
			kp_unit_finish_completion(g, c);
			kp_set_union_unit(g, c, 0, n);
			kp_hangar_add(g, n, c);
		}
	}
}

void kp_hangar_add(KpGame *g, int64_t n, int64_t c)
{
	KpUnits *u = &g->u;
	int64_t head;
	kp_unit_hide(g, c);
	head = kp_band(u->union_units[n], 1) != 0 ? u->union_0xc0[n] : 0;
	kp_set_union_unit(g, c, 1, head);
	kp_set_union_unit(g, c, 2, 0);
	if (head != 0) kp_set_union_unit(g, head, 2, c);
	u->union_0xc8[n] = kp_band(u->union_0xc8[n], 0xffffff00) + kp_mod(u->union_0xc8[n] + 1, 256);
	kp_set_union_unit(g, n, 0, c);
	u->union_0xcc[c] = kp_band(u->union_0xcc[c], 0xffffff00);
}

/* The cloaking tech of n's type (GetCloakTech 0x434900): Personnel Cloaking, Cloaking Field, else none (44). */
int64_t kp_cloak_tech(KpGame *g, int64_t n)
{
	switch (g->u.type[n]) {
	case KP_TYPE_GHOST: case KP_TYPE_SARAH_KERRIGAN: case KP_TYPE_ALEXEI_STUKOV: case KP_TYPE_SAMIR_DURAN:
	case KP_TYPE_INFESTED_DURAN: case KP_TYPE_INFESTED_KERRIGAN: return KP_TECH_PERSONNEL_CLOAKING;
	case KP_TYPE_WRAITH: case KP_TYPE_TOM_KAZANSKY: return KP_TECH_CLOAKING_FIELD;
	}
	return KP_TECH_NONE;
}

/* TryStartCloak 0x434270: a unit not cloaked yet pays the energy of its cloaking tech and starts the Cloak secondary
 * order. */
void kp_try_start_cloak(KpGame *g, int64_t n)
{
	int64_t cost;
	if (kp_band(g->u.status[n], STATUS_CLOAKED) != 0) return;
	cost = kp_tech_energy(g, kp_cloak_tech(g, n));
	if (kp_needs_energy(g, n, cost)) return;
	kp_spend_energy(g, n, cost);
	kp_set_secondary_order(g, n, KP_ORD_CLOAK);
}

void kp_cloak_list_add(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t count = u->is_cloaked[n];
	u->is_cloaked[n] = kp_mod(count + 1, 256);
	if (count == 0 && kp_band(u->status[n], 0x100) == 0 && u->burrow_next[n] == 0) {
		u->burrow_next[n] = g->scalar[KP_CLOAKED_HEAD];
		u->burrow_prev[n] = 0;
		if (g->scalar[KP_CLOAKED_HEAD] != 0) u->burrow_prev[kp_game_unit(g, g->scalar[KP_CLOAKED_HEAD])] = n;
		g->scalar[KP_CLOAKED_HEAD] = n;
	}
}

int kp_cloak_list_remove(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t prev, next;
	if (u->is_cloaked[n] != 0) return 0;
	if (g->scalar[KP_CLOAKED_HEAD] == n) g->scalar[KP_CLOAKED_HEAD] = u->burrow_next[n];
	prev = u->burrow_prev[n];
	next = u->burrow_next[n];
	if (prev != 0) u->burrow_next[kp_game_unit(g, prev)] = next;
	if (next != 0) u->burrow_prev[kp_game_unit(g, next)] = prev;
	u->status[n] = kp_band(u->status[n], 0xfffff7ff);
	u->burrow_prev[n] = u->burrow_next[n] = 0;
	return 1;
}

/* StartCloaking 0x41d410: as 0x41d300 without the interface refresh and the +0x91 reset. */
void kp_cloak_hidden(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	if (kp_band(u->status[n], 0x100) != 0) return;
	kp_cloak_list_add(g, n);
	kp_sprite_set_cloak_mode(g->w, kp_unit_sprite(g, n), 3, 0, 0);
	u->status[n] = kp_bor(u->status[n], 0x300);
	kp_set_secondary_order(g, n, KP_ORD_CLOAK);
	u->visibility[n] = 0x80000000LL;
}

int64_t kp_unit_race(KpGame *g, int64_t n)
{
	int64_t group = KP_DAT(g, units, staredit_group_flags, g->u.type[n]);
	if (kp_band(group, 1) != 0) return 0;
	if (kp_band(group, 4) != 0) return 2;
	if (kp_band(group, 2) != 0) return 1;
	return 4;
}

/* MakeAddonNeutral 0x428db0: unit a goes to player 11 and, unless a hallucination, to secondary order Nothing. */
void kp_make_neutral(KpGame *g, int64_t a)
{
	KpUnits *u = &g->u;
	if (kp_constructing_addon(g, a)) kp_cancel_incomplete_build_unit(g, a); /* 0x4355c0 */
	kp_give_unit(g, a, KP_PLAYER_NEUTRAL);
	if (kp_band(u->status[a], STATUS_HALLUCINATED) == 0) kp_set_secondary_order(g, a, KP_ORD_NOTHING);
}

/* DetachAddon 0x435ba0: the add-on of building n goes to player 11 (MakeAddonNeutral 0x428db0) and lifts its
 * antenna; the interface refresh and the local message (0x4466d0) are the presentation's. */
void kp_detach_addon(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t a = u->union_0xc0[n];
	KpCtx saved;
	kp_make_neutral(g, a);
	kp_set_union_unit(g, n, 0, 0);
	saved = kp_ctx_unit(g, a);
	kp_sprite_play(g->w, kp_unit_sprite(g, a), KP_ANIM_LIFT_OFF, 1);
	kp_ctx_restore(g, saved);
}

void kp_attach_addon(KpGame *g, int64_t building, int64_t addon)
{
	KpUnits *u = &g->u;
	KpImages *w = g->w;
	int64_t m;
	KpCtx saved;
	kp_set_union_unit(g, building, 0, addon);
	kp_change_owner(g, addon, u->owner[building], 0); /* ChangeUnitOwner 0x426ed0: nothing for the same owner */
	m = addon;
	do {
		w->spr.owner[kp_unit_sprite(g, m)] = (int32_t)u->owner[building];
		m = u->subunit[m];
	} while (!(m == 0 || kp_band(KP_DAT(g, units, special_ability_flags, u->type[m]), 0x10) == 0));
	saved = kp_ctx_unit(g, addon);
	kp_sprite_play(w, kp_unit_sprite(g, addon), KP_ANIM_LANDING, 1);
	kp_ctx_restore(g, saved);
	if (u->type[building] == KP_TYPE_NUCLEAR_SILO && u->union_0xd0[building] != 0) {
		kp_game_fail(g, "map units: Nuclear Silo missile owner (0x426ed0)");
	}
}

void kp_lift_off_initial(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	KpImages *w = g->w;
	int32_t s, tail;
	int64_t status;
	if (kp_can_issue_order(g, KP_ORD_BUILDING_LIFTOFF, u->owner[n], n) != 1) return;
	s = kp_unit_sprite(g, n);
	tail = kp_check_image(w, w->spr.images[s].tail);
	if (w->img.method[tail] == SHADOW_METHOD) w->img.yoff[tail] = LIFTED_SHADOW_Y;
	kp_sprite_play(w, s, KP_ANIM_SPECIAL1, 1);
	kp_unit_deactivate_spatial(g, n);
	/* 0x437010: no longer a grounded building, now able to turn; 0x20000 unless a subunit. */
	status = kp_bor(kp_band(u->status[n], 0xfffffffd), 0x10000);
	if (kp_band(KP_DAT(g, units, special_ability_flags, u->type[n]), 0x10) != 0) status = kp_band(status, kp_bnot(0x20000));
	else status = kp_bor(status, 0x20000);
	u->status[n] = status;
	kp_reset_movement(g, n);
	w->spr.elevation[s] = 12;
	u->status[n] = kp_bor(u->status[n], 4);
	kp_reset_movement(g, n);
	kp_unit_activate_spatial(g, n);
}

void kp_update_mineral_animation(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t amount = kp_mod(u->union_0xd0[n], 65536), anim, word;
	if (amount >= 0x2ee) anim = 0x14;
	else if (amount >= 500) anim = 0xf;
	else anim = amount < 0xfa ? 0xd : 0xe;
	word = u->union_0xd0[n];
	if (anim != kp_mod(kp_shrv(word, 16), 256)) {
		u->union_0xd0[n] = kp_band(word, 0xff00ffff) + anim * 65536;
		kp_sprite_play(g->w, kp_unit_sprite(g, n), (int32_t)anim, 1);
	}
}

void kp_unit_disable_doodad(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t t;
	if (kp_band(u->status[n], 0x400) != 0 || u->lockdown_timer[n] != 0 || u->stasis_timer[n] != 0
		|| u->maelstrom_timer[n] != 0) {
		return;
	}
	u->status[n] = kp_bor(u->status[n], 0x400);
	kp_sprite_play(g->w, kp_unit_sprite(g, n), 0xf, 1);
	t = u->type[n];
	u->status[n] = kp_bor(u->status[n], 0x200000);
	if (doodad_low(t)) g->w->spr.elevation[kp_unit_sprite(g, n)] = 1;
	if (doodad_trap(t)) {
		kp_unit_set_cloak_state(g, n, 3);
		u->visibility[n] = 0x80000000LL;
		u->secondary_timer[n] = 0;
	}
}
