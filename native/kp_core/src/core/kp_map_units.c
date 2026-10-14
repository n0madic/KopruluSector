/* Units created from the map (kp_game.h). */
#include "kp_game_internal.h"

#define EXPANSION_ONLY 0x200
#define CREEP_FLAG 0x20000
#define RESOURCE_CONTAINER 0x2000
#define MINERAL_FIRST 0xb0
#define MINERAL_LAST 0xb2
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
#define TECH_BURROWING 11
#define ANIM_BURROW 14
#define ORDER_BURROWED 117
#define ANIM_ADDON_ATTACHED 0x11
#define NO_TECH 0x2c
#define ORDER_LIFTOFF 0x48
#define ANIM_LIFTOFF 13
#define SHADOW_METHOD 10
#define LIFTED_SHADOW_Y 0x2a

static int creep_building(int64_t t) { return t == 0x84 || t == 0x85 || t == 0x90 || t == 0x92; }
static int creep_morph(int64_t t) { return t == 0x83 || t == 0x8f; }
static int doodad_trap(int64_t t) { return t == 0xcb || (t >= 0xd1 && t <= 0xd5); }
static int doodad_low(int64_t t) { return t == 0xcb || (t >= 0xcd && t <= 0xd1); }

/* The hangar child type of a carrier or reaver, or -1. */
static int64_t hangar_child(int64_t t)
{
	if (t == 0x48 || t == 0x52) return 0x49;
	if (t == 0x53 || t == 0x51) return 0x55;
	return -1;
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
		g->u.user_action[n] = kp_bor(g->u.user_action[n], 4);
		kp_unit_kill(g, n);
		return 0;
	}
	kp_guard_attach(g, n);
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
		if (t >= MINERAL_FIRST && t <= MINERAL_LAST) kp_update_mineral_animation(g, n);
	}
	if (kp_band(valid, VALID_HANGAR) != 0) kp_create_hangar_units(g, n, kp_mod(r->hangar, 256));
	if (kp_band(valid, STATE_CLOAKED) != 0 && kp_band(r->states, STATE_CLOAKED) != 0) {
		if (kp_can_use_tech(g, kp_cloak_tech(g, n), u->owner[n], n) == 1) kp_cloak_hidden(g, n);
	}
	if (kp_band(valid, STATE_BURROWED) != 0 && kp_band(r->states, STATE_BURROWED) != 0
		&& kp_can_use_tech(g, TECH_BURROWING, u->owner[n], n) == 1) {
		kp_sprite_play(g->w, kp_unit_sprite(g, n), ANIM_BURROW, 1);
		kp_set_burrowed(g, n);
		kp_cloak_hidden(g, n);
		u->status[n] = kp_bor(u->status[n], 0x200000);
		kp_issue_order(g, n, ORDER_BURROWED);
	}
	if (kp_band(valid, STATE_LIFTED) != 0 && kp_band(r->states, STATE_LIFTED) != 0) kp_lift_off_initial(g, n);
	if (kp_band(valid, STATE_HALLUCINATED) != 0 && kp_band(r->states, STATE_HALLUCINATED) != 0) {
		kp_game_fail(g, "map units: initial hallucinated state (0x430a00)");
	}
	if (kp_band(valid, STATE_INVINCIBLE) != 0 && kp_band(r->states, STATE_INVINCIBLE) != 0) {
		u->status[n] = kp_bor(u->status[n], 0x4000000);
	}
	if (t == 0x28) kp_game_fail(g, "map units: Broodling timer (0x434a90)");
}

int64_t kp_hangar_capacity(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t child = hangar_child(u->type[n]);
	/* normal, upgraded, hero capacity and the capacity upgrade of each child */
	int64_t normal = child == 0x49 ? 4 : 5, upgraded = child == 0x49 ? 8 : 10, upgrade = child == 0x49 ? 0x2b : 0x24;
	if (child < 0) return 0;
	if (kp_band(KP_DAT(g, units, special_ability_flags, u->type[n]), 0x40) != 0) return upgraded;
	return kp_upgrade_level(g, u->owner[n], upgrade) != 0 ? upgraded : normal;
}

void kp_create_hangar_units(KpGame *g, int64_t n, int64_t count)
{
	KpUnits *u = &g->u;
	int64_t child_type = hangar_child(u->type[n]), capacity, x, y, k;
	int32_t s;
	if (child_type < 0) return;
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

int64_t kp_cloak_tech(KpGame *g, int64_t n)
{
	switch (g->u.type[n]) {
	case 1: case 0x10: case 100: case 99: case 0x68: case 0x33: return 10;
	case 8: case 0x15: return 9;
	}
	return NO_TECH;
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

void kp_cloak_hidden(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	if (kp_band(u->status[n], 0x100) != 0) return;
	kp_cloak_list_add(g, n);
	kp_sprite_set_cloak_mode(g->w, kp_unit_sprite(g, n), 3, 0, 0);
	u->status[n] = kp_bor(u->status[n], 0x300);
	kp_set_secondary_order(g, n, 0x6d);
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

void kp_attach_addon(KpGame *g, int64_t building, int64_t addon)
{
	KpUnits *u = &g->u;
	KpImages *w = g->w;
	int64_t m;
	KpCtx saved;
	kp_set_union_unit(g, building, 0, addon);
	if (u->owner[addon] != u->owner[building]) kp_game_fail(g, "map units: addon of another owner (0x426ed0)");
	m = addon;
	do {
		w->spr.owner[kp_unit_sprite(g, m)] = (int32_t)u->owner[building];
		m = u->subunit[m];
	} while (!(m == 0 || kp_band(KP_DAT(g, units, special_ability_flags, u->type[m]), 0x10) == 0));
	saved = kp_ctx_unit(g, addon);
	kp_sprite_play(w, kp_unit_sprite(g, addon), ANIM_ADDON_ATTACHED, 1);
	kp_ctx_restore(g, saved);
	if (u->type[building] == 0x6c && u->union_0xd0[building] != 0) {
		kp_game_fail(g, "map units: Nuclear Silo missile owner (0x426ed0)");
	}
}

void kp_lift_off_initial(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	KpImages *w = g->w;
	int32_t s, tail;
	int64_t status;
	if (kp_can_issue_order(g, ORDER_LIFTOFF, u->owner[n], n) != 1) return;
	s = kp_unit_sprite(g, n);
	tail = kp_check_image(w, w->spr.images[s].tail);
	if (w->img.method[tail] == SHADOW_METHOD) w->img.yoff[tail] = LIFTED_SHADOW_Y;
	kp_sprite_play(w, s, ANIM_LIFTOFF, 1);
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
