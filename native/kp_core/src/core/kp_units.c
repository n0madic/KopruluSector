/* Unit pool, lists, creation and completion (kp_game.h). */
#include "kp_game_internal.h"

#include <string.h>

#define NO_UNIT 228
#define ST_COMPLETED 0x1
#define ST_GROUNDED_BUILDING 0x2
#define ST_AIR 0x4
#define ST_CLOAKED 0x300
#define ST_NOT_AIR 0x100000
#define ST_BEACON 0x200000
#define ST_INVINCIBLE 0x4000000
#define ST_INTERCEPTED 0x80000
#define RECENT_ORDER_TIME 0xf
#define ORDER_AI_TRANSPORT 0x60
#define DAT_BUILDING 0x1
#define DAT_SUBUNIT 0x10
#define DAT_HERO 0x40
#define DAT_DOUBLE_SUPPLY 0x400
#define DAT_POWERUP 0x800
#define DAT_PERMANENT_CLOAK 0x400000
#define DAT_INVINCIBLE 0x20000000
#define ORDER_NOTHING 23
#define ORDER_RESCUE 161
#define ORDER_NEUTRAL 162
#define ORDER_THINGY_DEFAULT 0xbc
#define BUTTONS_RALLY 0xe8
#define BUTTONS_DEFAULT 0xe7
#define SPIDER_MINES 3
#define UNKNOWN_0XC8_BUILDING 0x3d2c
#define ENERGY_HERO 64000
#define ENERGY_NORMAL 0xc800
#define ENERGY_FULL 0x6400
#define VISIBILITY_ALL 0xffffffffLL
#define ERROR_UNIT_CAP 0x3d
#define ERROR_NO_UNIT 0x3e
#define RANDOM_UNIT 14
#define RANDOM_WIREFRAME 15
#define RANDOM_HEADING 36
#define RANDOM_DIRECTION 32
#define RANDOM_FLAME 8
#define CONTROLLER_COMPUTER 1
#define CONTROLLER_RESCUE 3
#define CONTROLLER_NEUTRAL 7
#define SOUND_CLOAK 0x111
#define SMALL_FLAMES 450
#define LARGE_FLAMES 472
#define FLAME_SLOTS 22

static int is_beacon(int64_t t) { return (t >= 0xc2 && t <= 199) || t == 0xcc; }
static int revealer(int64_t t) { return t == 33 || t == 101; }
static int hangar_child(int64_t t) { return t == 73 || t == 85; }
static int trap(int64_t t) { return t == 203 || (t >= 209 && t <= 213); }

int kp_rally_producer(int64_t t)
{
	return t == 0x6a || t == 0x6f || t == 0x71 || t == 0x72 || t == 0x82 || t == 0x83 || t == 0x84 || t == 0x85
		|| t == 0x9a || t == 0xa0 || t == 0xa7 || t == 0x9b;
}

static int64_t flags(KpGame *g, int64_t t) { return KP_DAT(g, units, special_ability_flags, t); }

/* The sprite record of unit n, checked (a missing sprite fails). */
int32_t kp_unit_sprite(KpGame *g, int64_t n)
{
	return kp_check_sprite(g->w, (int32_t)g->u.sprite[kp_game_flingy(g, n)]);
}

/* The images world's unit context (0x543300), saved and set; KpCtx restores it. */
KpCtx kp_ctx_unit(KpGame *g, int64_t n)
{
	KpCtx c;
	c.unit = g->w->unit;
	c.has_unit = g->w->has_unit;
	c.flingy = g->w->flingy;
	c.has_flingy = g->w->has_flingy;
	g->w->unit = (int32_t)n;
	g->w->has_unit = 1;
	return c;
}

void kp_ctx_restore(KpGame *g, KpCtx c)
{
	g->w->unit = c.unit;
	g->w->has_unit = c.has_unit;
	g->w->flingy = c.flingy;
	g->w->has_flingy = c.has_flingy;
}

void kp_units_init(KpGame *g)
{
	int64_t n;
	int k;
	memset(&g->u, 0, sizeof g->u);
	for (n = 1; n <= KP_UNITS; n++) {
		g->u.sprite[n] = KP_NONE;
		g->u.queue_head[n] = KP_NONE;
		g->u.queue_tail[n] = KP_NONE;
	}
	for (k = 0; k < KP_LIST_COUNT; k++) g->lists[k].head = g->lists[k].tail = 0;
	for (k = 0; k < KP_PLAYERS; k++) g->player_units[k] = 0;
	kp_scalar_set(g, KP_UNITS_ALLOCATED, 0);
	for (n = 1; n <= KP_UNITS; n++) kp_list64_insert_after_head(g->u.prev, g->u.next, &g->lists[KP_LIST_FREE], n, 0);
	kp_spatial_clear(g);
	kp_scalar_set(g, KP_ERROR_CODE, 0);
}

/* The union words; bit k of union_units flags word k as a unit reference. */
int64_t *kp_union_word(KpGame *g, int word)
{
	switch (word) {
	case 0: return g->u.union_0xc0;
	case 1: return g->u.union_0xc4;
	case 2: return g->u.union_0xc8;
	case 3: return g->u.union_0xcc;
	case 4: return g->u.union_0xd0;
	case 5: return g->u.union_0xd4;
	case 6: return g->u.union_0xd8;
	}
	kp_game_fail(g, "unit: union word %d", word);
}

void kp_set_union_unit(KpGame *g, int64_t n, int word, int64_t target)
{
	int64_t bit = (int64_t)1 << word;
	kp_union_word(g, word)[n] = target;
	if (target != 0) g->u.union_units[n] = kp_bor(g->u.union_units[n], bit);
	else g->u.union_units[n] = kp_band(g->u.union_units[n], kp_bnot(bit));
}

/* IsUnitDead. */
int kp_unit_is_dead(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	kp_game_unit(g, n);
	return u->sprite[n] == -1 || (u->order[n] == 0 && u->order_state[n] == 1);
}

int64_t kp_unit_from_ref(KpGame *g, int64_t ref)
{
	int64_t n;
	if (ref == 0) return 0;
	n = kp_mod(ref, 0x800);
	/* Records above KP_UNITS do not exist: not dead, and no uniqueness to match. */
	if (n == 0 || n > KP_UNITS || kp_unit_is_dead(g, n) || g->u.uniqueness[n] != kp_shrv(ref, 11)) return 0;
	return n;
}

int kp_unit_fits_map(KpGame *g, int64_t t, int64_t x, int64_t y)
{
	return x - kp_s16v(KP_DAT(g, units, unit_extents, t * 4)) >= 0
		&& kp_s16v(KP_DAT(g, units, unit_extents, t * 4 + 2)) + x < g->map_w_px
		&& y - kp_s16v(KP_DAT(g, units, unit_extents, t * 4 + 1)) >= 0
		&& kp_s16v(KP_DAT(g, units, unit_extents, t * 4 + 3)) + y < g->map_h_px;
}

/* The upgrade that raises the energy cap of unit type t, -1 for none. */
static int64_t energy_upgrade(int64_t t)
{
	switch (t) {
	case 1: return 0x15;
	case 8: return 0x16;
	case 9: return 0x13;
	case 12: return 0x17;
	case 34: return 0x33;
	case 45: return 0x1f;
	case 46: return 0x20;
	case 60: return 0x2f;
	case 63: return 0x31;
	case 67: return 0x28;
	case 71: return 0x2c;
	}
	return -1;
}

int64_t kp_max_energy(KpGame *g, int64_t n)
{
	int64_t t = g->u.type[n], upgrade;
	if (kp_band(flags(g, t), DAT_HERO) != 0) return ENERGY_HERO;
	upgrade = energy_upgrade(t);
	if (upgrade >= 0 && kp_upgrade_level(g, g->u.owner[n], upgrade) != 0) return ENERGY_HERO;
	return ENERGY_NORMAL;
}

void kp_set_button_set(KpGame *g, int64_t n, int64_t set)
{
	KpUnits *u = &g->u;
	if ((kp_band(u->status[n], 0x400) == 0 && u->lockdown_timer[n] == 0 && u->stasis_timer[n] == 0
			&& u->maelstrom_timer[n] == 0)
		|| kp_band(flags(g, u->type[n]), DAT_BUILDING) != 0 || set == NO_UNIT) {
		u->button_set[n] = set;
	}
}

int64_t kp_unit_damage_overlay_slots(KpGame *g, int64_t n)
{
	KpImages *w = g->w;
	int32_t s = kp_unit_sprite(g, n);
	return kp_images_damage_overlay_slots(w, w->img.id[kp_check_image(w, w->spr.main[s])]);
}

/* InitBuildHpGain 0x427d90 and InitBuildShieldGain 0x427de0. */
static void init_build_hp_gain(KpGame *g, int64_t n)
{
	int64_t t = g->u.type[n], time = KP_DAT(g, units, build_time, t), hp = KP_DAT(g, units, hit_points, t), gain = 0;
	if (time != 0) gain = kp_mod(kp_udiv64(g, (time - 1) + (hp - kp_udiv64(g, hp, 10)), time), 65536);
	g->u.hp_gain[n] = gain != 0 ? gain : 1;
}

static void init_build_shield_gain(KpGame *g, int64_t n)
{
	int64_t t = g->u.type[n], start, time, gain = 0;
	if (KP_DAT(g, units, shield_enabled, t) == 0 || kp_band(g->u.status[n], ST_GROUNDED_BUILDING) == 0) return;
	start = kp_udiv64(g, KP_DAT(g, units, shield_amount, t) * 256, 10);
	g->u.shields[n] = start;
	time = KP_DAT(g, units, build_time, t);
	if (time != 0) gain = kp_mod(kp_udiv64(g, KP_DAT(g, units, shield_amount, t) * 256 - start, time), 65536);
	g->u.shield_gain[n] = gain != 0 ? gain : 1;
}

int kp_thingy_init(KpGame *g, int64_t n, int64_t flingy, int64_t x, int64_t y, int64_t owner, int64_t heading)
{
	KpUnits *u = &g->u;
	if (!kp_flingy_init(g, n, flingy, x, y, owner, heading)) return 0;
	u->owner[n] = owner;
	u->order_type[n] = NO_UNIT;
	u->order_x[n] = u->order_y[n] = 0;
	u->order[n] = ORDER_THINGY_DEFAULT;
	u->order_state[n] = u->order_signal[n] = 0;
	u->order_timer[n] = u->ground_cooldown[n] = u->air_cooldown[n] = u->spell_cooldown[n] = 0;
	u->order_unit[n] = 0;
	return 1;
}

void kp_step_unit_sprite(KpGame *g, int64_t n)
{
	int64_t s = g->u.sprite[n];
	if (s == KP_NONE) return;
	kp_sprite_update(g->w, kp_check_sprite(g->w, (int32_t)s));
	if (g->w->spr.images[s].head == KP_NONE) g->u.sprite[n] = KP_NONE;
}

int kp_unit_initialize_type(KpGame *g, int64_t n, int64_t t, int64_t x, int64_t y, int64_t owner)
{
	KpUnits *u = &g->u;
	KpCtx saved = kp_ctx_unit(g, n);
	int64_t f, status, build;
	if (!kp_thingy_init(g, n, KP_DAT(g, units, graphics, t), x, y, owner, 0)) {
		kp_ctx_restore(g, saved);
		return 0;
	}
	u->union_0xc0[n] = u->union_0xc4[n] = u->union_0xc8[n] = u->union_0xcc[n] = 0;
	u->union_units[n] = kp_band(u->union_units[n], kp_bnot(0xf));
	u->resource_type[n] = u->secondary_timer[n] = 0;
	u->type[n] = t;
	kp_step_unit_sprite(g, n);
	kp_ctx_restore(g, saved);
	u->last_attacker[n] = 8;
	u->shields[n] = KP_DAT(g, units, shield_amount, t) * 256;
	if (t == 0xac) u->energy[n] = ENERGY_FULL;
	else u->energy[n] = kp_mod(kp_idiv64(g, kp_max_energy(g, n), 4), 65536);
	u->ai_action[n] = 0;
	g->w->spr.elevation[kp_unit_sprite(g, n)] = (int32_t)KP_DAT(g, units, elevation_level, t);
	f = flags(g, t);
	status = u->status[n];
	status = kp_band(status, kp_bnot(2)) + kp_band(f * 2, 2);
	status = kp_band(status, kp_bnot(4)) + kp_band(f, 4);
	status = kp_band(status, kp_bnot(0x10000)) + kp_band(kp_shrv(f, 12), 0x10000);
	status = kp_band(status, kp_bnot(0x20000)) + kp_band(kp_shrv(f, 10), 0x20000);
	status = kp_band(status, kp_bnot(ST_NOT_AIR)) + (kp_band(status, ST_AIR) == 0 ? ST_NOT_AIR : 0);
	u->status[n] = status;
	u->pathing_flags[n] = kp_band(u->pathing_flags[n], 0xfe) + (KP_DAT(g, units, elevation_level, t) < 12 ? 1 : 0);
	if (is_beacon(t)) u->status[n] = kp_bor(u->status[n], ST_BEACON);
	if (kp_band(f, DAT_BUILDING) != 0) u->union_0xc8[n] = kp_band(u->union_0xc8[n], 0xffff0000) + UNKNOWN_0XC8_BUILDING;
	u->path[n] = u->movement_state[n] = u->recent_order_timer[n] = 0;
	if (kp_band(f, DAT_INVINCIBLE) != 0) u->status[n] = kp_bor(u->status[n], ST_INVINCIBLE);
	else u->status[n] = kp_band(u->status[n], kp_bnot(ST_INVINCIBLE));
	u->overlay_state[n] = kp_unit_damage_overlay_slots(g, n);
	build = KP_DAT(g, units, build_time, t);
	u->build_time[n] = build != 0 ? build : 1;
	init_build_hp_gain(g, n);
	init_build_shield_gain(g, n);
	kp_apply_creation_upgrades(g, n);
	kp_recalculate_speed(g, n);
	return 1;
}

int kp_unit_initialize(KpGame *g, int64_t n, int64_t t, int64_t x, int64_t y, int64_t owner)
{
	KpUnits *u = &g->u;
	KpImages *w = g->w;
	int64_t head;
	int32_t s;
	u->union_0xd0[n] = u->union_0xd4[n] = u->union_0xd8[n] = 0;
	u->rally_x[n] = u->rally_y[n] = u->rally_unit[n] = 0;
	u->burrow_prev[n] = u->burrow_next[n] = u->auto_target[n] = u->connected[n] = 0;
	u->queue_count[n] = u->queue_timer[n] = u->unknown_0x86[n] = u->attack_notify_timer[n] = 0;
	u->previous_type[n] = u->last_event_timer[n] = u->last_event_color[n] = u->unknown_0x8c[n] = 0;
	u->rank[n] = u->kills[n] = u->remove_timer[n] = u->matrix_damage[n] = u->matrix_timer[n] = 0;
	u->stim_timer[n] = u->ensnare_timer[n] = u->lockdown_timer[n] = u->irradiate_timer[n] = 0;
	u->stasis_timer[n] = u->plague_timer[n] = u->storm_timer[n] = u->irradiated_by[n] = 0;
	u->irradiate_player[n] = u->parasites[n] = u->cycle_counter[n] = u->blind[n] = u->maelstrom_timer[n] = 0;
	u->unknown_0x125[n] = u->acid_spores[n] = 0;
	u->acid_time0[n] = u->acid_time1[n] = u->acid_time2[n] = u->acid_time3[n] = u->acid_time4[n] = 0;
	u->acid_time5[n] = u->acid_time6[n] = u->acid_time7[n] = u->acid_time8[n] = 0;
	u->status[n] = u->user_action[n] = u->pathing_flags[n] = u->ai[n] = 0;
	u->queue_head[n] = u->queue_tail[n] = KP_NONE;
	u->union_units[n] = kp_band(u->union_units[n], 0xf);
	u->previous_hp[n] = 1;
	if (!kp_unit_initialize_type(g, n, t, x, y, owner)) return 0;
	u->queue0[n] = u->queue1[n] = u->queue2[n] = u->queue3[n] = u->queue4[n] = NO_UNIT;
	u->queue_slot[n] = 0;
	u->uniqueness[n] = kp_mod(u->uniqueness[n] + 1, 32);
	kp_set_button_set(g, n, kp_rally_producer(t) ? BUTTONS_RALLY : BUTTONS_DEFAULT);
	u->wireframe[n] = kp_rng_game_raw(g->rng, RANDOM_WIREFRAME) % 256;
	if (kp_band(flags(g, t), DAT_SUBUNIT) != 0) u->hp[n] = 1;
	else u->hp[n] = kp_udiv64(g, KP_DAT(g, units, hit_points, t), 10);
	if (kp_band(u->status[n], ST_GROUNDED_BUILDING) == 0) kp_set_order(g, n, ORDER_NOTHING);
	else kp_set_order(g, n, KP_DAT(g, units, human_ai_idle, t));
	kp_set_secondary_order(g, n, ORDER_NOTHING);
	u->bound_left[n] = u->bound_right[n] = u->bound_top[n] = u->bound_bottom[n] = -1;
	u->player_prev[n] = 0;
	if (owner < 0 || owner >= KP_PLAYERS) kp_game_fail(g, "unit: owner %lld", (long long)owner);
	head = g->player_units[owner];
	u->player_next[n] = head;
	if (head != 0) u->player_prev[head] = n;
	g->player_units[owner] = n;
	kp_account_live(g, n, 1);
	s = kp_unit_sprite(g, n);
	if (kp_band(u->status[n], ST_GROUNDED_BUILDING) == 0) {
		if (t == 2 || t == 0x13) u->union_0xc0[n] = kp_band(u->union_0xc0[n], 0xffffff00) + SPIDER_MINES;
		w->spr.flags[s] = (int32_t)kp_bor(w->spr.flags[s], KP_SF_HIDDEN);
		kp_sprite_set_visibility(w, s, 0);
	} else {
		kp_unit_activate_spatial(g, n);
		kp_replace_unit_images(g, n, 1);
		kp_sprite_set_visibility(w, s, 0);
		if (kp_scalar(g, KP_PLACEMENT_ACTIVE) == 1) kp_game_fail(g, "unit: building creation during placement (0x47d530)");
	}
	u->visibility[n] = VISIBILITY_ALL;
	if (kp_band(flags(g, t), DAT_SUBUNIT) != 0) {
		w->spr.flags[s] = (int32_t)kp_bor(w->spr.flags[s], KP_SF_DEPTH);
		return 1;
	}
	if (kp_band(w->spr.flags[s], KP_SF_HIDDEN) == 0) kp_unit_update_vision(g, n);
	return 1;
}

int64_t kp_unit_allocate(KpGame *g, int64_t t, int64_t x, int64_t y, int64_t owner)
{
	int64_t n;
	if (kp_scalar(g, KP_UNITS_ALLOCATED) == KP_UNITS) {
		kp_scalar_set(g, KP_ERROR_CODE, ERROR_UNIT_CAP);
		return 0;
	}
	if (!kp_unit_fits_map(g, t, x, y)) {
		kp_scalar_set(g, KP_ERROR_CODE, 0);
		return 0;
	}
	n = g->lists[KP_LIST_FREE].head;
	if (n != 0 && kp_unit_initialize(g, n, t, x, y, owner)) {
		kp_scalar_set(g, KP_UNITS_ALLOCATED, kp_scalar(g, KP_UNITS_ALLOCATED) + 1);
		kp_list64_unlink(g->u.prev, g->u.next, &g->lists[KP_LIST_FREE], n, 0);
		return n;
	}
	kp_scalar_set(g, KP_ERROR_CODE, ERROR_NO_UNIT);
	return 0;
}

int64_t kp_unit_create(KpGame *g, int64_t t, int64_t x, int64_t y, int64_t owner)
{
	KpUnits *u = &g->u;
	KpImages *w = g->w;
	int64_t n, sub, child;
	int32_t ox, oy;
	kp_rng_game_raw(g->rng, RANDOM_UNIT);
	n = kp_unit_allocate(g, t, x, y, owner);
	if (n == 0) return 0;
	kp_list64_insert_after_head(u->prev, u->next,
		&g->lists[kp_band(u->status[n], ST_GROUNDED_BUILDING) != 0 ? KP_LIST_ACTIVE : KP_LIST_HIDDEN], n, 0);
	sub = t <= 0x69 ? KP_DAT(g, units, subunit1, t) : NO_UNIT;
	if (sub == NO_UNIT) {
		u->subunit[n] = 0;
		return n;
	}
	child = kp_unit_allocate(g, sub, x, y, owner);
	if (child == 0) kp_game_fail(g, "unit: subunit allocation failure (0x425cd0) for type %lld", (long long)t);
	u->subunit[n] = child;
	u->subunit[child] = n;
	kp_images_lo_offset(w, kp_check_image(w, w->spr.main[kp_unit_sprite(g, n)]), 2, 0, &ox, &oy);
	kp_image_set_offset(w, kp_check_image(w, w->spr.main[kp_unit_sprite(g, child)]), ox & 0xff, oy & 0xff);
	return n;
}

void kp_replace_unit_images(KpGame *g, int64_t n, int construction)
{
	KpUnits *u = &g->u;
	KpImages *w = g->w;
	int32_t s = kp_unit_sprite(g, n);
	int64_t id;
	KpCtx saved;
	if (kp_band(u->status[n], ST_CLOAKED) != 0) {
		kp_game_fail(g, "unit: replacing the images of a cloaked unit (0x416030/0x415e50)");
	}
	id = construction ? KP_DAT(g, units, construction_animation, u->type[n]) : 0;
	if (id == 0) id = KP_DAT(g, sprites, image, w->spr.id[s]);
	saved = kp_ctx_unit(g, n);
	kp_sprite_replace_main(g, s, id, u->heading[n]);
	kp_ctx_restore(g, saved);
	kp_rebuild_status_overlays(g, n);
	u->overlay_state[n] = kp_unit_damage_overlay_slots(g, n);
}

void kp_sprite_replace_main(KpGame *g, int32_t s, int64_t id, int64_t heading)
{
	KpImages *w = g->w;
	int32_t i, main;
	if (kp_band(w->spr.flags[s], 0x0f) != 0) {
		kp_game_fail(g, "unit: replacing the main image of a selected sprite (0x416d50/0x416cd0)");
	}
	i = w->spr.images[s].head;
	while (i != KP_NONE) {
		int32_t next = w->img.next[i];
		kp_image_release(w, i);
		i = next;
	}
	main = kp_image_alloc(w);
	if (main != KP_NONE) {
		KpList *list = &w->spr.images[s];
		if (list->head == KP_NONE) {
			w->spr.main[s] = main;
			kp_list_insert_after_head(w->img.prev, w->img.next, list, main);
		} else {
			kp_list_insert_before(w->img.prev, w->img.next, list, list->head, main);
		}
		kp_image_init_record(w, main, (int32_t)id, s, 0, 0);
		kp_image_init_drawing(w, main);
		kp_image_set_direction(w, main, 0);
	}
	i = w->spr.images[s].head;
	while (i != KP_NONE) {
		kp_image_direction_from_angle(w, i, (int32_t)heading);
		i = w->img.next[i];
	}
}

void kp_rebuild_status_overlays(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	if (u->matrix_timer[n] != 0) kp_game_fail(g, "unit: status overlays (0x434d10, matrix_timer)");
	if (u->lockdown_timer[n] != 0) kp_game_fail(g, "unit: status overlays (0x434d10, lockdown_timer)");
	if (u->maelstrom_timer[n] != 0) kp_game_fail(g, "unit: status overlays (0x434d10, maelstrom_timer)");
	if (u->irradiate_timer[n] != 0) kp_game_fail(g, "unit: status overlays (0x434d10, irradiate_timer)");
	if (u->ensnare_timer[n] != 0) kp_game_fail(g, "unit: status overlays (0x434d10, ensnare_timer)");
	if (u->stasis_timer[n] != 0) kp_game_fail(g, "unit: status overlays (0x434d10, stasis_timer)");
	if (u->plague_timer[n] != 0) kp_game_fail(g, "unit: status overlays (0x434d10, plague_timer)");
	if (u->acid_spores[n] != 0) kp_game_fail(g, "unit: status overlays (0x434d10, acid_spores)");
}

void kp_unit_activate_spatial(KpGame *g, int64_t n)
{
	kp_spatial_insert(g, n);
	if (kp_band(g->u.status[n], ST_GROUNDED_BUILDING) != 0) kp_add_building_occupancy(g, n);
	kp_update_overlap_flags(g, n);
	if (kp_band(g->u.status[n], ST_AIR) != 0) kp_air_density_add(g, n);
}

/* The placement box's tile rectangle of unit n (0x419fd0, 0x41a190). */
static void occupancy_rect(KpGame *g, int64_t n, int64_t *tx, int64_t *ty, int64_t *tw, int64_t *th)
{
	int64_t t = g->u.type[n];
	int32_t s = kp_unit_sprite(g, n);
	int64_t w = kp_s16v(KP_DAT(g, units, staredit_placement_size, t * 2));
	int64_t h = kp_s16v(KP_DAT(g, units, staredit_placement_size, t * 2 + 1));
	int64_t x = kp_s16v(g->w->spr.x[s]), y = kp_s16v(g->w->spr.y[s]);
	*tx = kp_sarv(x - kp_idiv64(g, w, 2), 5);
	*ty = kp_sarv(y - kp_idiv64(g, h, 2), 5);
	*tw = kp_idiv64(g, w, 32);
	*th = kp_idiv64(g, h, 32);
}

void kp_add_building_occupancy(KpGame *g, int64_t n)
{
	int64_t tx, ty, tw, th;
	occupancy_rect(g, n, &tx, &ty, &tw, &th);
	kp_tiles_or(g, tx, ty, tw, th, 0x08000000);
}

void kp_remove_building_occupancy(KpGame *g, int64_t n)
{
	int64_t tx, ty, tw, th;
	occupancy_rect(g, n, &tx, &ty, &tw, &th);
	kp_tiles_clear(g, tx, ty, tw, th, 0x08000000);
}

void kp_unit_deactivate_spatial(KpGame *g, int64_t n)
{
	kp_spatial_remove(g, n);
	if (kp_band(g->u.status[n], ST_GROUNDED_BUILDING) != 0) kp_remove_building_occupancy(g, n);
	if (kp_band(g->u.status[n], ST_AIR) != 0) kp_air_density_remove(g, n);
}

void kp_reset_movement(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	if (u->path[n] != 0) kp_path_free(g, n);
	u->movement_state[n] = 0;
	u->pathing_flags[n] = kp_band(u->pathing_flags[n], 0xfe) + (g->w->spr.elevation[kp_unit_sprite(g, n)] < 12 ? 1 : 0);
}

void kp_reset_to_idle_animation(KpGame *g, int64_t n)
{
	KpCtx saved;
	int32_t s;
	g->u.status[n] = kp_band(g->u.status[n], kp_bnot(0x2000));
	s = kp_unit_sprite(g, n);
	g->w->spr.flags[s] &= 0x7f;
	saved = kp_ctx_unit(g, n);
	g->w->flingy = (int32_t)n;
	g->w->has_flingy = 1;
	kp_flingy_to_idle(g, n);
	kp_ctx_restore(g, saved);
}

void kp_unit_show(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	KpImages *w = g->w;
	int32_t s = kp_unit_sprite(g, n);
	int64_t sub;
	int sub_indexed;
	if (kp_band(w->spr.flags[s], KP_SF_HIDDEN) == 0) return;
	w->spr.flags[s] = (int32_t)kp_band(w->spr.flags[s], kp_bnot(KP_SF_HIDDEN));
	sub = u->subunit[n];
	sub_indexed = sub != 0 && kp_band(flags(g, u->type[sub]), DAT_SUBUNIT) != 0;
	if (sub_indexed) {
		int32_t ss = kp_unit_sprite(g, sub);
		w->spr.flags[ss] = (int32_t)kp_band(w->spr.flags[ss], kp_bnot(KP_SF_HIDDEN));
	}
	kp_unit_update_vision(g, n);
	kp_unit_update_sprite_visibility(g, n);
	kp_unit_activate_spatial(g, n);
	kp_reset_movement(g, n);
	if (sub_indexed) kp_reset_movement(g, sub);
	kp_list64_unlink(u->prev, u->next, &g->lists[KP_LIST_HIDDEN], n, 0);
	kp_list64_insert_after_head(u->prev, u->next, &g->lists[KP_LIST_ACTIVE], n, 0);
}

void kp_unit_prefinish(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t t = u->type[n];
	if (u->build_time[n] != 0) {
		u->hp[n] = KP_DAT(g, units, hit_points, t);
		u->shields[n] = KP_DAT(g, units, shield_amount, t) * 256;
		u->build_time[n] = 0;
	}
	kp_set_button_set(g, n, t);
	if (kp_band(u->status[n], ST_GROUNDED_BUILDING) == 0) {
		if (kp_band(u->status[n], 0x10000) != 0) {
			int64_t dir = KP_DAT(g, units, unit_direction, t);
			if (dir == RANDOM_DIRECTION) dir = kp_rng_game_raw(g->rng, RANDOM_HEADING) % 32;
			kp_flingy_set_heading(g, n, dir * 8);
		}
		if (t > 0xca && t < 0xd6) kp_unit_show(g, n);
	} else {
		u->parasites[n] = u->blind[n] = 0;
		kp_replace_unit_images(g, n, 0);
		kp_sprite_play(g->w, kp_unit_sprite(g, n), 0x10, 1);
	}
}

void kp_clamp_to_map(KpGame *g, int64_t t, int64_t *x, int64_t *y)
{
	int64_t left = kp_s16v(KP_DAT(g, units, unit_extents, t * 4));
	int64_t up = kp_s16v(KP_DAT(g, units, unit_extents, t * 4 + 1));
	int64_t right = kp_s16v(KP_DAT(g, units, unit_extents, t * 4 + 2));
	int64_t down = kp_s16v(KP_DAT(g, units, unit_extents, t * 4 + 3));
	if (*x < left) *x = left;
	else if (g->map_w_px - right <= *x) *x = kp_s16v(g->map_w_px - right - 1);
	if (*y < up) *y = up;
	else if (g->map_h_px - down - 32 <= *y) *y = kp_s16v(g->map_h_px - down - 33);
}

void kp_unit_relocate(KpGame *g, int64_t n, int64_t x, int64_t y)
{
	KpUnits *u = &g->u;
	KpImages *w = g->w;
	int32_t s;
	int64_t ox, oy;
	kp_clamp_to_map(g, u->type[n], &x, &y);
	s = kp_unit_sprite(g, n);
	ox = kp_s16v(w->spr.x[s]);
	oy = kp_s16v(w->spr.y[s]);
	kp_flingy_set_position(g, n, x, y);
	if (!(u->sprite[n] == KP_NONE || u->order[n] == 0)) {
		kp_set_move_target_point(g, n, x, y);
		kp_flingy_set_target_waypoint(g, n, x, y);
	}
	if (kp_band(flags(g, u->type[n]), DAT_SUBUNIT) == 0) {
		kp_spatial_move(g, n, x - ox, y - oy);
		if (kp_band(w->spr.flags[s], KP_SF_HIDDEN) == 0) kp_update_overlap_flags(g, n);
	}
}

static void move_target_set(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t head;
	u->status[n] = kp_band(u->status[n], kp_bnot(ST_INTERCEPTED));
	u->recent_order_timer[n] = RECENT_ORDER_TIME;
	head = u->queue_head[n];
	if (head != KP_NONE && KP_DAT(g, orders, waypoints_slowdown, g->orders.order[head]) == 0) {
		u->movement_flags[n] = kp_bor(u->movement_flags[n], 0x20);
	} else {
		u->movement_flags[n] = kp_band(u->movement_flags[n], 0xdf);
	}
}

int kp_set_move_target_point(KpGame *g, int64_t n, int64_t x, int64_t y)
{
	KpUnits *u = &g->u;
	if (u->move_x[n] == x && u->move_y[n] == y) return 1;
	if (u->ai[n] != 0 && kp_ai_transport_for_move(g, n, x, y)) return 0;
	if (u->path[n] != 0) kp_path_mark_stale(g, n);
	kp_clamp_to_map(g, u->type[n], &x, &y);
	kp_flingy_set_move_target(g, n, x, y);
	move_target_set(g, n);
	return 1;
}

static int on_contact_boundary(KpGame *g, int64_t n, int64_t target, int64_t x, int64_t y)
{
	KpUnits *u = &g->u;
	int32_t s = kp_unit_sprite(g, target);
	int64_t tx = g->w->spr.x[s], ty = g->w->spr.y[s];
	int64_t t = u->type[target] * 4, m = u->type[n] * 4;
#define EXT(i) KP_DAT(g, units, unit_extents, i)
	int64_t left = kp_s16v(tx - EXT(t) - 1 - EXT(m + 2));
	int64_t right = kp_s16v(tx + EXT(t + 2) + EXT(m) + 1 + EXT(m + 1) + 1);
	int64_t top = kp_s16v(ty - EXT(t + 1) - 1 - EXT(m + 3));
	int64_t bottom = kp_s16v(ty + EXT(t + 3));
#undef EXT
	x = kp_s16v(x);
	y = kp_s16v(y);
	if (x == left || x == right) return y >= top && y <= bottom;
	if (y == top || y == bottom) return x >= left && x <= right;
	return 0;
}

int kp_set_move_target_unit(KpGame *g, int64_t n, int64_t target)
{
	KpUnits *u = &g->u;
	int64_t cur = u->move_unit[n];
	if (target == cur) {
		int32_t s = kp_unit_sprite(g, cur);
		if ((u->move_x[n] == kp_s16v(g->w->spr.x[s]) && u->move_y[n] == kp_s16v(g->w->spr.y[s]))
			|| on_contact_boundary(g, n, cur, u->move_x[n], u->move_y[n])) {
			u->status[n] = kp_band(u->status[n], kp_bnot(ST_INTERCEPTED));
			return 1;
		}
	}
	if (u->ai[n] != 0 && u->order[n] != ORDER_AI_TRANSPORT) {
		int32_t ts = kp_unit_sprite(g, target);
		if (kp_ai_transport_for_move(g, n, kp_s16v(g->w->spr.x[ts]), kp_s16v(g->w->spr.y[ts]))) return 0;
	}
	if (u->path[n] != 0) kp_path_mark_stale(g, n);
	kp_flingy_set_move_target_unit(g, n, target);
	move_target_set(g, n);
	return 1;
}

int kp_unit_place(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	KpImages *w = g->w;
	int32_t s = kp_unit_sprite(g, n);
	int64_t x, y, sub;
	if (kp_band(w->spr.flags[s], KP_SF_HIDDEN) == 0) return 1;
	if (!kp_find_unit_placement(g, n, kp_s16v(w->spr.x[s]), kp_s16v(w->spr.y[s]), NULL, 0, 0, &x, &y)) {
		if (kp_scalar(g, KP_ERROR_CODE) != 0) {
			kp_report_error(g, u->owner[n], kp_scalar(g, KP_ERROR_CODE));
			kp_scalar_set(g, KP_ERROR_CODE, 0);
		}
		return 0;
	}
	kp_unit_relocate(g, n, x, y);
	sub = u->subunit[n];
	if (sub != 0 && kp_band(flags(g, u->type[n]), DAT_SUBUNIT) == 0) {
		kp_account_completed(g, sub, 1, 0);
		u->status[sub] = kp_bor(u->status[sub], ST_COMPLETED);
		u->hp[sub] = KP_DAT(g, units, hit_points, u->type[sub]);
		kp_unit_relocate(g, sub, x, y);
	}
	return 1;
}

void kp_unit_finish_completion(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t t = u->type[n], controller, order;
	if (kp_band(flags(g, t), DAT_DOUBLE_SUPPLY) != 0) {
		kp_account_live(g, n, -1);
		u->status[n] = kp_bor(u->status[n], ST_COMPLETED);
		kp_account_live(g, n, 1);
	} else {
		u->status[n] = kp_bor(u->status[n], ST_COMPLETED);
	}
	kp_account_completed(g, n, 1, 1);
	if (kp_band(flags(g, t), DAT_PERMANENT_CLOAK) != 0) kp_begin_cloak(g, n, SOUND_CLOAK);
	if (revealer(t)) {
		kp_list64_unlink(u->prev, u->next, &g->lists[KP_LIST_HIDDEN], n, 0);
		kp_list64_insert_after_head(u->prev, u->next, &g->lists[KP_LIST_SPECIAL], n, 0);
		kp_unit_update_vision(g, n);
	} else if (kp_band(g->w->spr.flags[kp_unit_sprite(g, n)], KP_SF_HIDDEN) != 0 && !hangar_child(t)) {
		kp_unit_show(g, n);
	}
	if (trap(t)) {
		kp_unit_set_cloak_state(g, n, 3);
		u->visibility[n] = 0x80000000LL;
		u->secondary_timer[n] = 0;
	}
	controller = kp_controller(g, u->owner[n]);
	if (controller == CONTROLLER_RESCUE) order = ORDER_RESCUE;
	else if (controller == CONTROLLER_NEUTRAL) order = ORDER_NEUTRAL;
	else if (controller == CONTROLLER_COMPUTER) order = KP_DAT(g, units, comp_ai_idle, t);
	else order = KP_DAT(g, units, human_ai_idle, t);
	kp_issue_order(g, n, order);
	if (kp_band(flags(g, t), DAT_POWERUP) != 0) {
		int32_t s = kp_unit_sprite(g, n);
		u->union_0xd0[n] = kp_mod(g->w->spr.x[s], 65536) + kp_mod(g->w->spr.y[s], 65536) * 65536;
		kp_issue_order(g, n, KP_DAT(g, units, human_ai_idle, t));
	}
	kp_refresh_unit_strength(g, n);
}

int kp_unit_complete(KpGame *g, int64_t n)
{
	kp_unit_prefinish(g, n);
	if (!kp_unit_place(g, n)) return 0;
	kp_unit_finish_completion(g, n);
	return 1;
}

void kp_unit_set_cloak_state(KpGame *g, int64_t n, int64_t state)
{
	g->u.status[n] = kp_band(g->u.status[n], kp_bnot(ST_CLOAKED)) + kp_band(state * 256, ST_CLOAKED);
}

void kp_set_unit_hp(KpGame *g, int64_t n, int64_t hp)
{
	KpUnits *u = &g->u;
	KpImages *w = g->w;
	int64_t max = KP_DAT(g, units, hit_points, u->type[n]);
	int32_t s;
	if (hp > max) hp = max;
	u->hp[n] = hp;
	s = kp_unit_sprite(g, n);
	/* A selected sprite's health bars are redrawn (0x416bb0): presentation only. */
	if (kp_band(u->status[n], ST_COMPLETED) != 0) {
		if (KP_DAT(g, images, damage_overlay, w->img.id[kp_check_image(w, w->spr.main[s])]) != 0) {
			kp_update_damage_overlays(g, n);
		}
		if (kp_band(u->status[n], ST_COMPLETED) != 0) kp_refresh_unit_strength(g, n);
	}
}

void kp_update_damage_overlays(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t max = KP_DAT(g, units, hit_points, u->type[n]);
	int64_t third = kp_idiv64(g, max, 3);
	int64_t slots = kp_mod(kp_unit_damage_overlay_slots(g, n), 256);
	int64_t step = kp_udiv64(g, max - third, slots + 1);
	int64_t target = kp_idiv64(g, u->hp[n] - kp_urem64(g, max - third, step) - third - 1, step);
	if (target < 0) target = 0;
	target = kp_mod(target, 256);
	while (u->overlay_state[n] < target) {
		kp_damage_overlay_remove(g, n);
		u->overlay_state[n] = u->overlay_state[n] + 1;
	}
	while (u->overlay_state[n] > target) {
		kp_damage_overlay_add(g, n);
		u->overlay_state[n] = u->overlay_state[n] - 1;
	}
}

void kp_damage_overlay_add(KpGame *g, int64_t n)
{
	KpImages *w = g->w;
	int32_t s = kp_unit_sprite(g, n), main = kp_check_image(w, w->spr.main[s]), i, x, y;
	int64_t count, k, seen = 0, slot = 0;
	if (KP_DAT(g, images, damage_overlay, w->img.id[main]) == 0) return;
	i = w->spr.images[s].head;
	while (i != KP_NONE) {
		int32_t id = w->img.id[i];
		if (id >= SMALL_FLAMES && id < LARGE_FLAMES) {
			kp_images_lo_offset(w, main, 1, id - SMALL_FLAMES, &x, &y);
			kp_image_release(w, i);
			kp_sprite_image_on_top(w, s, id + FLAME_SLOTS, x, y, 0);
			return;
		}
		i = w->img.next[i];
	}
	count = kp_shrv(kp_images_damage_overlay_slots(w, w->img.id[main]), 1);
	k = kp_urem64(g, kp_rng_game_raw(g->rng, RANDOM_FLAME), count) + 1;
	for (;;) {
		if (slot > FLAME_SLOTS - 1) slot = 0;
		kp_images_lo_offset(w, main, 1, (int32_t)slot, &x, &y);
		if (x != 127 || y != 127) {
			int taken = 0;
			int32_t j = w->spr.images[s].head;
			seen++;
			while (j != KP_NONE) {
				if (w->img.id[j] == slot + LARGE_FLAMES) {
					taken = 1;
					break;
				}
				j = w->img.next[j];
			}
			if (!taken && seen >= k) {
				kp_sprite_image_on_top(w, s, (int32_t)slot + SMALL_FLAMES, x, y, 0);
				return;
			}
		}
		slot++;
	}
}

void kp_damage_overlay_remove(KpGame *g, int64_t n)
{
	KpImages *w = g->w;
	int32_t s = kp_unit_sprite(g, n), main = kp_check_image(w, w->spr.main[s]), i, x, y;
	if (KP_DAT(g, images, damage_overlay, w->img.id[main]) == 0) return;
	i = w->spr.images[s].head;
	while (i != KP_NONE) {
		int32_t id = w->img.id[i];
		if (id >= LARGE_FLAMES && id < LARGE_FLAMES + FLAME_SLOTS) {
			kp_images_lo_offset(w, main, 1, id - LARGE_FLAMES, &x, &y);
			kp_image_release(w, i);
			kp_sprite_image_on_top(w, s, id - FLAME_SLOTS, x, y, 0);
			return;
		}
		i = w->img.next[i];
	}
	i = w->spr.images[s].head;
	while (i != KP_NONE) {
		int32_t id = w->img.id[i];
		if (id >= SMALL_FLAMES && id < LARGE_FLAMES) {
			kp_image_release(w, i);
			return;
		}
		i = w->img.next[i];
	}
}
