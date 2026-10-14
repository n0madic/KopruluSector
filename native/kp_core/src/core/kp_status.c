/* Status effects of spells (kp_game.h), ported from the atlas (P08, P13): the status overlays of a unit, Ensnare,
 * Parasite and Spawn Broodlings. */
#include "kp_game_internal.h"

#define DAT_BUILDING 0x1
#define DAT_MEDIUM_OVERLAY 0x2000000
#define DAT_LARGE_OVERLAY 0x4000000
#define STATUS_BURROWED 0x10
#define STATUS_NO_COLLIDE 0x200000
#define STATUS_INVINCIBLE 0x4000000
#define STATUS_HALLUCINATION 0x40000000
#define BROODLING 40
#define BROODLING_LIFE 1800
#define HALLUCINATION_LIFE 1350
#define BROOD_ATTEMPTS 2
#define BROOD_SEARCH 32
#define ENSNARE_TIME 75
#define ENSNARE_RADIUS 64
#define ENSNARE_SPRITE 364
#define ENSNARE_ELEVATION 19
#define ENSNARE_OVERLAY 384
#define PARASITE_SOUND 921

/* GetOverlaySize 0x428d90: 0 small, 1 medium, 2 large. */
int64_t kp_overlay_size(KpGame *g, int64_t n)
{
	int64_t f = KP_DAT(g, units, special_ability_flags, g->u.type[n]);
	if (kp_band(f, DAT_MEDIUM_OVERLAY) != 0) return 1;
	if (kp_band(f, DAT_LARGE_OVERLAY) != 0) return 2;
	return 0;
}

/* The sprite a unit's status overlays go on: its subunit's or its parent's when linked (+0x70). */
static int32_t overlay_sprite(KpGame *g, int64_t n)
{
	int64_t linked = g->u.subunit[n];
	return kp_unit_sprite(g, linked != 0 ? linked : n);
}

/* AddOverlayToUnitLinkedSprite 0x422ae0 (with 0x415260: before the image head). */
void kp_add_status_overlay(KpGame *g, int64_t n, int64_t id)
{
	kp_sprite_image_on_top(g->w, overlay_sprite(g, n), (int32_t)id, 0, 0, 0);
}

static int32_t image_in_range(KpImages *w, int32_t s, int64_t low, int64_t high)
{
	int32_t i;
	for (i = w->spr.images[s].head; i != KP_NONE; i = w->img.next[i]) {
		if (w->img.id[i] >= low && w->img.id[i] <= high) return i;
	}
	return KP_NONE;
}

/* RemoveImageRangeFromUnit 0x422e70: the first image of the range on the unit's sprite, else on its linked unit's. */
void kp_remove_status_overlay(KpGame *g, int64_t n, int64_t low, int64_t high)
{
	KpImages *w = g->w;
	int32_t i = image_in_range(w, kp_unit_sprite(g, n), low, high);
	if (i == KP_NONE && g->u.subunit[n] != 0) i = image_in_range(w, kp_unit_sprite(g, g->u.subunit[n]), low, high);
	if (i != KP_NONE) kp_image_release(w, i);
}

/* GetTimedLifeDuration 0x434a90. */
int64_t kp_timed_life(KpGame *g, int64_t n)
{
	if (kp_band(g->u.status[n], STATUS_HALLUCINATION) != 0) return HALLUCINATION_LIFE;
	return g->u.type[n] == BROODLING ? BROODLING_LIFE : 0;
}

/* ApplyEnsnareToUnit 0x43e0e0. */
void kp_apply_ensnare(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	if (u->ensnare_timer[n] == 0 && kp_band(u->status[n], STATUS_BURROWED) == 0) {
		kp_add_status_overlay(g, n, ENSNARE_OVERLAY + kp_overlay_size(g, n));
	}
	u->ensnare_timer[n] = ENSNARE_TIME;
	kp_recalculate_speed(g, n);
}

/* EndEnsnare 0x43dee0. */
void kp_end_ensnare(KpGame *g, int64_t n)
{
	kp_remove_status_overlay(g, n, ENSNARE_OVERLAY, ENSNARE_OVERLAY + 2);
	g->u.ensnare_timer[n] = 0;
	kp_recalculate_speed(g, n);
}

/* ApplyEnsnareArea 0x43dc90: the cloud and every unit around (x, y) but the source, buildings, invincible and
 * burrowed units (EnsnareAreaCallback 0x43df10). */
void kp_ensnare_area(KpGame *g, int64_t x, int64_t y, int64_t source)
{
	KpUnits *u = &g->u;
	KpImages *w = g->w;
	int32_t t = kp_lone_create(w, ENSNARE_SPRITE, (int32_t)x, (int32_t)y, 0);
	int64_t *found;
	int count, k;
	if (t != KP_NONE) {
		w->spr.elevation[w->thg.sprite[t]] = ENSNARE_ELEVATION;
		kp_thingy_visibility(w, t);
	}
	/* FindUnitInRadiusBox 0x442b20 */
	found = kp_query_rect(g, kp_s16v(x - ENSNARE_RADIUS), kp_s16v(y - ENSNARE_RADIUS), kp_s16v(x + ENSNARE_RADIUS),
		kp_s16v(y + ENSNARE_RADIUS), &count);
	for (k = 0; k < count; k++) {
		int64_t c = found[k];
		if (c == source || kp_band(KP_DAT(g, units, special_ability_flags, u->type[c]), DAT_BUILDING) != 0
			|| kp_band(u->status[c], STATUS_INVINCIBLE | STATUS_BURROWED) != 0) {
			continue;
		}
		kp_apply_ensnare(g, c);
	}
	kp_query_release(g, found);
}

/* The Parasite effect of a bullet (0x4090e0 type 6): player `owner` sees through target. */
void kp_apply_parasite(KpGame *g, int64_t target, int64_t owner, int64_t source)
{
	kp_play_unit_sound(g, PARASITE_SOUND, target);
	g->u.parasites[target] = kp_bor(g->u.parasites[target], (int64_t)1 << (owner & 7));
	if (source != 0) kp_ai_react_to_attack(g, target, source, 0, 0);
}

/* SpawnBroodlings 0x43e120: the target dies and two Broodlings of the Queen's owner take its place. */
void kp_spawn_broodlings(KpGame *g, int64_t queen, int64_t target)
{
	KpUnits *u = &g->u;
	int64_t owner = u->owner[queen];
	if (kp_band(u->status[target], STATUS_HALLUCINATION) == 0) {
		int32_t s = kp_unit_sprite(g, target);
		int64_t x = kp_s16v(g->w->spr.x[s]) - 2, y = kp_s16v(g->w->spr.y[s]) - 2;
		int k;
		for (k = 0; k < BROOD_ATTEMPTS; k++, x += 4, y += 4) {
			int64_t px = x, py = y, b, life;
			if (!kp_unit_type_fits(g, BROODLING, x, y)) {
				int64_t box[4];
				box[0] = kp_s16v(x - BROOD_SEARCH);
				box[1] = kp_s16v(y - BROOD_SEARCH);
				box[2] = kp_s16v(x + BROOD_SEARCH);
				box[3] = kp_s16v(y + BROOD_SEARCH);
				if (!kp_find_unit_placement(g, queen, x, y, box, 0, 0, &px, &py)) continue;
				x = kp_s16v(px);
				y = kp_s16v(py);
			}
			px = x;
			py = y;
			kp_clamp_to_map(g, BROODLING, &px, &py);
			b = kp_unit_create(g, BROODLING, px, py, owner);
			if (b == 0) continue; /* ReportUnitCreationError 0x425020: the interface's */
			kp_unit_complete_in_place(g, b);
			life = kp_timed_life(g, b);
			if (u->remove_timer[b] == 0 || kp_mod(life, 65536) < u->remove_timer[b]) u->remove_timer[b] = kp_mod(life, 65536);
			if (u->ai[queen] != 0) kp_game_fail(g, "status: a computer's Broodlings join its Queen (0x48d320)");
		}
	}
	kp_record_death(g, target, owner);
	kp_unit_kill(g, target);
	kp_increment_kills(g, queen);
}

#define DAT_ORGANIC 0x10000
#define NUCLEAR_MISSILE 14
#define STATUS_DISABLED 0x400
#define STATUS_IN_TRANSPORT 0x40
#define LOCKDOWN_OVERLAY 361
#define STASIS_OVERLAY_ID 365
#define IRRADIATE_OVERLAY 380
#define PLAGUE_OVERLAY 388
#define MAELSTROM_OVERLAY 982
#define RESTORATION_OVERLAY 967
#define FLARE_OVERLAY 976
#define ACID_OVERLAY_FIRST 986
#define ACID_OVERLAY_LAST 997
#define PLAGUE_TIME 75
#define PLAGUE_TICKS 76
#define PLAGUE_RADIUS 64
#define IRRADIATE 34 /* the weapon */
#define PLAGUE_WEAPON 60
#define EMP_WEAPON 33
#define IRRADIATE_RADIUS 160
#define IRRADIATE_RANGE 32
#define NO_PLAYER 8
#define FLARE_SOUND 1019
#define CONSUME_ENERGY 12800
#define LARVA 35
#define EGG 36
#define LURKER_EGG 97
#define ANIM_DEATH 1
#define NO_BUTTONS 228
#define DISPATCH_THROTTLE 15

static int64_t dat_flags(KpGame *g, int64_t n) { return KP_DAT(g, units, special_ability_flags, g->u.type[n]); }

static int disabled_by_status(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	return kp_band(u->status[n], STATUS_DISABLED) != 0 || u->lockdown_timer[n] != 0 || u->stasis_timer[n] != 0
		|| u->maelstrom_timer[n] != 0;
}

/* 0x435a40: a Ghost lets go of its Nuclear Missile. */
void kp_ghost_release_nuke(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t t = u->type[n], m;
	if (t != 1 && t != 16 && t != 100 && t != 99 && t != 104) return;
	m = u->connected[n];
	if (m != 0 && u->type[m] == NUCLEAR_MISSILE) u->connected[n] = u->connected[m] = 0;
}

/* DisableUnit 0x434c60: the common part of Lockdown, Stasis and Maelstrom. */
void kp_disable_unit(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t sub = u->subunit[n], t = u->type[n], order;
	if (sub != 0) u->status[sub] = kp_bor(u->status[sub], STATUS_DISABLED);
	kp_stop_movement(g, n);
	kp_ghost_release_nuke(g, n);
	if (t == 0x48 || t == 0x52) kp_game_fail(g, "status: a disabled Carrier's Interceptors (0x41cfb0)");
	order = u->order[n];
	if (order == 34 || order == 70) u->order_state[n] = 0;
	if (KP_DAT(g, orders, disabled_maintain_unit_target, order) == 0) u->order_unit[n] = 0;
	kp_reset_to_idle_animation(g, n);
	if ((g->w->spr.flags[kp_unit_sprite(g, n)] & 0x08) != 0) {
		int k = 0;
		while (k < KP_SLOTS && g->local_selection[k] != 0) k++;
		if (k > 1) kp_game_fail(g, "status: a disabled unit leaves a group selection (0x4a7b80)");
	}
	kp_set_button_set(g, n, NO_BUTTONS);
}

/* EndStatusOverlay 0x4343b0: the overlay of the range dies away; a parent no longer disabled frees its subunit. */
static void end_status_overlay(KpGame *g, int64_t n, int64_t low, int64_t high)
{
	KpUnits *u = &g->u;
	KpImages *w = g->w;
	int64_t sub = u->subunit[n];
	int32_t i = image_in_range(w, kp_unit_sprite(g, n), low, high);
	if (sub != 0) {
		if (i == KP_NONE) i = image_in_range(w, kp_unit_sprite(g, sub), low, high);
		if (!disabled_by_status(g, n)) {
			u->status[sub] = kp_band(u->status[sub], kp_bnot(STATUS_DISABLED));
			kp_issue_order(g, sub, KP_DAT(g, units, return_to_idle, u->type[sub]));
		}
	}
	if (i != KP_NONE) kp_image_play(w, i, ANIM_DEATH);
	if (kp_band(dat_flags(g, n), 0x8) != 0) kp_game_fail(g, "status: a worker resumes harvesting (0x4244c0)");
	u->queue_timer[n] = DISPATCH_THROTTLE;
}

/* ApplyLockdown 0x4389c0. */
void kp_apply_lockdown(KpGame *g, int64_t n, int64_t duration)
{
	KpUnits *u = &g->u;
	if (u->lockdown_timer[n] == 0) kp_add_status_overlay(g, n, LOCKDOWN_OVERLAY + kp_overlay_size(g, n));
	if (u->lockdown_timer[n] < kp_band(duration, 0xff)) u->lockdown_timer[n] = kp_band(duration, 0xff);
	kp_disable_unit(g, n);
}

/* EndLockdown 0x4386e0, EndMaelstrom 0x438730. */
static void end_disable(KpGame *g, int64_t n, int64_t low)
{
	if (!disabled_by_status(g, n)) kp_set_button_set(g, n, g->u.type[n]);
	end_status_overlay(g, n, low, low + 2);
}

void kp_end_lockdown(KpGame *g, int64_t n)
{
	g->u.lockdown_timer[n] = 0;
	end_disable(g, n, LOCKDOWN_OVERLAY);
}

void kp_end_maelstrom(KpGame *g, int64_t n)
{
	g->u.maelstrom_timer[n] = 0;
	end_disable(g, n, MAELSTROM_OVERLAY);
}

/* ApplyIrradiate 0x438960. */
void kp_apply_irradiate(KpGame *g, int64_t n, int64_t source, int64_t player)
{
	KpUnits *u = &g->u;
	if (u->irradiate_timer[n] == 0 && kp_band(u->status[n], STATUS_BURROWED) == 0) {
		kp_add_status_overlay(g, n, IRRADIATE_OVERLAY + kp_overlay_size(g, n));
	}
	u->irradiate_timer[n] = KP_DAT(g, weapons, weapon_cooldown, IRRADIATE);
	u->irradiated_by[n] = source;
	u->irradiate_player[n] = kp_band(player, 0xff);
}

/* The Irradiate damage callback 0x438650. */
static void irradiate_hit(KpGame *g, int64_t c, int64_t source)
{
	KpUnits *u = &g->u;
	int64_t t = u->type[c], f = dat_flags(g, c), damage;
	if (kp_band(KP_DAT(g, units, special_ability_flags, t), DAT_ORGANIC) == 0 || kp_band(f, DAT_BUILDING) != 0) return;
	if (t == LARVA || t == EGG || t == LURKER_EGG) return;
	if (kp_band(u->status[c], STATUS_BURROWED) != 0 && c != source) return;
	if (kp_band(u->status[source], STATUS_IN_TRANSPORT) == 0 && !kp_unit_box_within(g, source, IRRADIATE_RANGE, c)) return;
	damage = kp_s32v(kp_shlv(KP_DAT(g, weapons, damage_amount, IRRADIATE), 8)) / kp_s32v(KP_DAT(g, weapons, weapon_cooldown, IRRADIATE));
	kp_weapon_damage(g, c, damage, IRRADIATE, 1, 0, u->irradiated_by[source], u->irradiate_player[source]);
}

/* IrradiateTick 0x4385d0. */
static void irradiate_tick(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int32_t s;
	int64_t *found, x, y;
	int count, k;
	if (kp_band(u->status[n], STATUS_BURROWED) != 0) {
		irradiate_hit(g, n, n);
		return;
	}
	if (kp_band(u->status[n], STATUS_IN_TRANSPORT) != 0) {
		kp_game_fail(g, "status: Irradiate in a transport (0x437da0)");
	}
	s = kp_unit_sprite(g, n);
	x = kp_s16v(g->w->spr.x[s]);
	y = kp_s16v(g->w->spr.y[s]);
	found = kp_query_rect(g, kp_s16v(x - IRRADIATE_RADIUS), kp_s16v(y - IRRADIATE_RADIUS), kp_s16v(x + IRRADIATE_RADIUS),
		kp_s16v(y + IRRADIATE_RADIUS), &count);
	for (k = 0; k < count; k++) irradiate_hit(g, found[k], n);
	kp_query_release(g, found);
}

/* EndIrradiate 0x438620. */
static void end_irradiate(KpGame *g, int64_t n)
{
	kp_remove_status_overlay(g, n, IRRADIATE_OVERLAY, IRRADIATE_OVERLAY + 2);
	g->u.irradiate_timer[n] = 0;
	g->u.irradiated_by[n] = 0;
	g->u.irradiate_player[n] = NO_PLAYER;
}

void kp_update_irradiate(KpGame *g, int64_t n)
{
	g->u.irradiate_timer[n]--;
	irradiate_tick(g, n);
	if (g->u.irradiate_timer[n] == 0) end_irradiate(g, n);
}

/* ApplyPlagueToUnit 0x43e0a0. */
static void apply_plague(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	if (u->plague_timer[n] == 0 && kp_band(u->status[n], STATUS_BURROWED) == 0) {
		kp_add_status_overlay(g, n, PLAGUE_OVERLAY + kp_overlay_size(g, n));
	}
	u->plague_timer[n] = PLAGUE_TIME;
}

/* ApplyPlagueArea 0x43db50 with PlagueAreaCallback 0x43de10. */
void kp_plague_area(KpGame *g, int64_t x, int64_t y, int64_t source)
{
	KpUnits *u = &g->u;
	int64_t *found;
	int count, k;
	found = kp_query_rect(g, kp_s16v(x - PLAGUE_RADIUS), kp_s16v(y - PLAGUE_RADIUS), kp_s16v(x + PLAGUE_RADIUS),
		kp_s16v(y + PLAGUE_RADIUS), &count);
	for (k = 0; k < count; k++) {
		int64_t c = found[k];
		if (c == source || kp_band(u->status[c], STATUS_INVINCIBLE | STATUS_BURROWED) != 0) continue;
		apply_plague(g, c);
		if (source != 0) kp_on_unit_damaged(g, c, source, 1);
	}
	kp_query_release(g, found);
}

/* EndPlague 0x43ddf0. */
static void end_plague(KpGame *g, int64_t n)
{
	kp_remove_status_overlay(g, n, PLAGUE_OVERLAY, PLAGUE_OVERLAY + 2);
	g->u.plague_timer[n] = 0;
}

/* PlagueDamageTick 0x43ddb0 and the expiry. */
void kp_update_plague(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	u->plague_timer[n]--;
	if (kp_band(u->status[n], STATUS_INVINCIBLE) == 0) {
		int64_t damage = kp_s32v(kp_shlv(KP_DAT(g, weapons, damage_amount, PLAGUE_WEAPON), 8)) / PLAGUE_TICKS;
		if (kp_s32v(u->hp[n]) > damage) kp_damage_hit_points(g, n, damage, 0, -1, 1);
	}
	if (u->plague_timer[n] == 0) end_plague(g, n);
}

/* EmpShockwaveImpact 0x433fc0. */
void kp_emp_shockwave(KpGame *g, int64_t x, int64_t y, int64_t source)
{
	KpUnits *u = &g->u;
	int64_t r = KP_DAT(g, weapons, inner_splash_range, EMP_WEAPON), l, t, rr, b, *found, sub = source != 0 ? u->subunit[source] : 0;
	int count, k;
	l = kp_s16v(x - r);
	t = kp_s16v(y - r);
	rr = kp_s16v(x + r);
	b = kp_s16v(y + r);
	if (l < 0) l = 0;
	else if (rr >= kp_mod(g->map_w_px, 65536)) rr = kp_mod(g->map_w_px, 65536) - 1;
	if (t < 0) t = 0;
	else if (b >= kp_mod(g->map_h_px, 65536)) b = kp_mod(g->map_h_px, 65536) - 1;
	found = kp_query_rect(g, l, t, rr, b, &count);
	for (k = 0; k < count; k++) {
		int64_t c = found[k];
		if (c == source || (sub != 0 && c == sub)) continue;
		if (kp_band(u->status[c], STATUS_HALLUCINATION) != 0) {
			kp_unit_kill(g, c);
		} else if (u->stasis_timer[c] == 0) {
			u->energy[c] = 0;
			u->shields[c] = 0;
		}
	}
	kp_query_release(g, found);
}

/* ApplyConsume 0x43db70. */
void kp_apply_consume(KpGame *g, int64_t defiler, int64_t target)
{
	KpUnits *u = &g->u;
	int32_t s = kp_unit_sprite(g, target);
	int64_t e;
	if (!kp_validate_spell_target(g, defiler, 16, target, NO_BUTTONS, kp_s16v(g->w->spr.x[s]), kp_s16v(g->w->spr.y[s]), NULL)) {
		return;
	}
	kp_record_death(g, target, u->owner[defiler]);
	kp_unit_kill(g, target);
	if (kp_band(u->status[target], STATUS_HALLUCINATION) != 0) return;
	e = u->energy[defiler] + CONSUME_ENERGY;
	if (e > kp_max_energy(g, defiler)) e = kp_max_energy(g, defiler);
	u->energy[defiler] = kp_mod(e, 65536);
}

/* ApplyOpticalFlare 0x438a20. */
void kp_apply_optical_flare(KpGame *g, int64_t n, int64_t player)
{
	KpUnits *u = &g->u;
	if (kp_band(u->status[n], STATUS_HALLUCINATION) != 0) {
		kp_unit_kill(g, n);
		return;
	}
	if (player < 8) u->blind[n] = kp_bor(u->blind[n], (int64_t)1 << player);
	kp_play_unit_sound(g, FLARE_SOUND, n);
	kp_add_status_overlay(g, n, FLARE_OVERLAY + kp_overlay_size(g, n));
	kp_scalar_set(g, KP_TILE_COUNTDOWN, 1); /* 0x6663e4: the periodic visibility pass runs on the next step */
}

static void clear_acid_spores(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	if (u->acid_spores[n] == 0) return;
	u->acid_spores[n] = 0;
	u->acid_time0[n] = u->acid_time1[n] = u->acid_time2[n] = u->acid_time3[n] = u->acid_time4[n] = 0;
	u->acid_time5[n] = u->acid_time6[n] = u->acid_time7[n] = u->acid_time8[n] = 0;
	kp_remove_status_overlay(g, n, ACID_OVERLAY_FIRST, ACID_OVERLAY_LAST);
}

/* ApplyRestoration 0x4383e0. */
void kp_apply_restoration(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t size = kp_overlay_size(g, n);
	if (kp_band(u->status[n], STATUS_HALLUCINATION) != 0) {
		kp_unit_kill(g, n);
		return;
	}
	kp_add_status_overlay(g, n, RESTORATION_OVERLAY + size);
	u->parasites[n] = 0;
	u->blind[n] = 0;
	if (u->ensnare_timer[n] != 0) {
		u->ensnare_timer[n] = 0;
		kp_end_ensnare(g, n);
	}
	if (u->plague_timer[n] != 0) {
		u->plague_timer[n] = 0;
		end_plague(g, n);
	}
	if (u->irradiate_timer[n] != 0) {
		u->irradiate_timer[n] = 0;
		kp_remove_status_overlay(g, n, IRRADIATE_OVERLAY, IRRADIATE_OVERLAY + 2);
		u->irradiated_by[n] = 0;
		u->irradiate_player[n] = NO_PLAYER;
	}
	if (u->lockdown_timer[n] != 0) kp_end_lockdown(g, n);
	if (u->maelstrom_timer[n] != 0) kp_end_maelstrom(g, n);
	clear_acid_spores(g, n);
}

#define MATRIX_OVERLAY 371
#define MATRIX_BACK 374
#define MATRIX_HIT 377
#define MATRIX_TIME 168
#define MATRIX_HP 0xfa00
#define DEFENSIVE_MATRIX 6
#define NO_TYPE 228

/* ApplyDefensiveMatrix 0x438900. */
void kp_apply_matrix(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	if (u->matrix_timer[n] == 0 && kp_band(u->status[n], STATUS_BURROWED) == 0) {
		int64_t size = kp_overlay_size(g, n);
		kp_add_status_overlay(g, n, MATRIX_OVERLAY + size);
		kp_sprite_image_below_main(g->w, kp_unit_sprite(g, n), (int32_t)(MATRIX_BACK + size), 0, 0, 0);
	}
	u->matrix_timer[n] = MATRIX_TIME;
	u->matrix_damage[n] = MATRIX_HP;
}

/* ReduceDefensiveMatrix 0x4384c0: the matrix absorbs amount; it flashes while it lasts. */
void kp_reduce_matrix(KpGame *g, int64_t n, int64_t amount)
{
	KpUnits *u = &g->u;
	if (kp_s32v(amount) >= kp_mod(u->matrix_damage[n], 65536)) {
		int32_t i;
		u->matrix_damage[n] = 0;
		u->matrix_timer[n] = 0;
		kp_remove_status_overlay(g, n, MATRIX_OVERLAY, MATRIX_OVERLAY + 2);
		i = image_in_range(g->w, kp_unit_sprite(g, n), MATRIX_BACK, MATRIX_BACK + 2);
		if (i != KP_NONE) kp_image_release(g->w, i);
	} else {
		u->matrix_damage[n] = kp_mod(u->matrix_damage[n] - amount, 65536);
	}
	if (u->matrix_timer[n] != 0 && kp_band(u->status[n], STATUS_BURROWED) == 0) {
		kp_add_status_overlay(g, n, MATRIX_HIT + kp_overlay_size(g, n));
	}
}

/* RebuildStatusOverlays 0x434d10: the overlays of the active effects again (after a type or burrow change). */
void kp_rebuild_status_overlays(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t saved, size = kp_overlay_size(g, n);
	if (u->matrix_timer[n] != 0) {
		/* RefreshDefensiveMatrix 0x438780 */
		int32_t s = kp_unit_sprite(g, n);
		if (!kp_validate_spell_target(g, n, DEFENSIVE_MATRIX, n, NO_TYPE, kp_s16v(g->w->spr.x[s]), kp_s16v(g->w->spr.y[s]),
				NULL)) {
			u->matrix_timer[n] = 0;
			u->matrix_damage[n] = 0;
		} else {
			int64_t timer = u->matrix_timer[n], hp = u->matrix_damage[n];
			u->matrix_timer[n] = 0;
			u->matrix_damage[n] = 0;
			kp_apply_matrix(g, n);
			u->matrix_timer[n] = timer;
			u->matrix_damage[n] = hp;
		}
	}
	if (u->lockdown_timer[n] != 0) {
		/* RefreshLockdown 0x438860 */
		saved = u->lockdown_timer[n];
		u->lockdown_timer[n] = 0;
		kp_add_status_overlay(g, n, LOCKDOWN_OVERLAY + size);
		u->lockdown_timer[n] = saved;
		kp_disable_unit(g, n);
	}
	if (u->maelstrom_timer[n] != 0) {
		/* RefreshMaelstrom 0x4388b0 */
		saved = u->maelstrom_timer[n];
		u->maelstrom_timer[n] = 0;
		kp_add_status_overlay(g, n, MAELSTROM_OVERLAY + size);
		u->maelstrom_timer[n] = saved;
		kp_disable_unit(g, n);
	}
	if (u->irradiate_timer[n] != 0) {
		/* RefreshIrradiate 0x438800 */
		if (!kp_weapon_can_target(g, n, IRRADIATE, n)) {
			u->irradiate_timer[n] = 0;
			u->irradiated_by[n] = 0;
		} else {
			saved = u->irradiate_timer[n];
			u->irradiate_timer[n] = 0;
			kp_apply_irradiate(g, n, u->irradiated_by[n], u->irradiate_player[n]);
			u->irradiate_timer[n] = saved;
		}
	}
	if (u->ensnare_timer[n] != 0) {
		/* ReapplyEnsnareOverlay 0x43e050 */
		saved = u->ensnare_timer[n];
		u->ensnare_timer[n] = 0;
		kp_apply_ensnare(g, n);
		u->ensnare_timer[n] = saved;
	}
	if (u->stasis_timer[n] != 0) {
		/* ReapplyStasisOverlay 0x431030 */
		saved = u->stasis_timer[n];
		u->stasis_timer[n] = 0;
		kp_add_status_overlay(g, n, STASIS_OVERLAY_ID + size);
		u->status[n] = kp_bor(u->status[n], STATUS_INVINCIBLE);
		u->stasis_timer[n] = saved;
		kp_disable_unit(g, n);
	}
	if (u->plague_timer[n] != 0) {
		/* ReapplyPlagueOverlay 0x43dfc0 */
		saved = u->plague_timer[n];
		u->plague_timer[n] = 0;
		if (kp_band(u->status[n], STATUS_BURROWED) == 0) kp_add_status_overlay(g, n, PLAGUE_OVERLAY + size);
		u->plague_timer[n] = saved;
	}
	if (u->acid_spores[n] != 0) kp_game_fail(g, "status: the acid spore overlay again (0x43e010)");
}

#define DARK_SWARM 202
#define SWARM_LIFE 900
#define SWARM_ELEVATION 11
#define NEUTRAL 11

/* CreateDarkSwarm 0x43dc10. */
void kp_create_dark_swarm(KpGame *g, int64_t x, int64_t y, int64_t owner)
{
	KpUnits *u = &g->u;
	int64_t n;
	(void)owner; /* ReportUnitCreationError 0x425020 of a failure is the interface's */
	kp_clamp_to_map(g, DARK_SWARM, &x, &y);
	n = kp_unit_create(g, DARK_SWARM, x, y, NEUTRAL);
	if (n == 0) return;
	u->status[n] = kp_bor(u->status[n], STATUS_NO_COLLIDE);
	g->w->spr.elevation[kp_unit_sprite(g, n)] = SWARM_ELEVATION;
	kp_unit_complete_in_place(g, n);
	if (u->remove_timer[n] == 0 || u->remove_timer[n] > SWARM_LIFE) u->remove_timer[n] = SWARM_LIFE;
}

#define STASIS_OVERLAY 365
#define STASIS_TIME 131
#define MAELSTROM_TIME 22
#define STASIS_SPRITE 361
#define MAELSTROM_SPRITE 514
#define FIELD_ELEVATION 17
#define FIELD_RADIUS 48
#define MAELSTROM_SOUND 1064
#define DISRUPTION_WEB 105
#define WEB_LIFE 360
#define DAT_INVINCIBLE_TYPE 0x20000000

/* A field's lone sprite at (x, y) (0x430820, 0x430880). */
static void field_sprite(KpGame *g, int64_t id, int64_t x, int64_t y)
{
	KpImages *w = g->w;
	int32_t t = kp_lone_create(w, (int32_t)id, (int32_t)x, (int32_t)y, 0);
	if (t == KP_NONE) return;
	w->spr.elevation[w->thg.sprite[t]] = FIELD_ELEVATION;
	kp_thingy_visibility(w, t);
}

/* ApplyStasis 0x431090. */
static void apply_stasis(KpGame *g, int64_t n, int64_t duration)
{
	KpUnits *u = &g->u;
	if (u->stasis_timer[n] == 0) {
		kp_add_status_overlay(g, n, STASIS_OVERLAY + kp_overlay_size(g, n));
		u->status[n] = kp_bor(u->status[n], STATUS_INVINCIBLE);
	}
	if (u->stasis_timer[n] < kp_band(duration, 0xff)) u->stasis_timer[n] = kp_band(duration, 0xff);
	kp_disable_unit(g, n);
}

/* StasisFieldImpact 0x430880 with its callback 0x430920. */
void kp_stasis_field(KpGame *g, int64_t x, int64_t y, int64_t source)
{
	KpUnits *u = &g->u;
	int64_t *found;
	int count, k;
	field_sprite(g, STASIS_SPRITE, x, y);
	found = kp_query_rect(g, kp_s16v(x - FIELD_RADIUS), kp_s16v(y - FIELD_RADIUS), kp_s16v(x + FIELD_RADIUS),
		kp_s16v(y + FIELD_RADIUS), &count);
	for (k = 0; k < count; k++) {
		int64_t c = found[k];
		if (c == source || kp_band(u->status[c], STATUS_INVINCIBLE | STATUS_BURROWED) != 0
			|| kp_band(dat_flags(g, c), DAT_BUILDING) != 0) {
			continue;
		}
		apply_stasis(g, c, STASIS_TIME);
	}
	kp_query_release(g, found);
}

/* EndStasis 0x4310f0. */
void kp_end_stasis(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	u->stasis_timer[n] = 0;
	if (!disabled_by_status(g, n)) kp_set_button_set(g, n, u->type[n]);
	if (kp_band(dat_flags(g, n), DAT_INVINCIBLE_TYPE) == 0) u->status[n] = kp_band(u->status[n], kp_bnot(STATUS_INVINCIBLE));
	end_status_overlay(g, n, STASIS_OVERLAY, STASIS_OVERLAY + 2);
}

/* ApplyMaelstrom 0x438a90. */
static void apply_maelstrom(KpGame *g, int64_t n, int64_t duration)
{
	KpUnits *u = &g->u;
	if (u->maelstrom_timer[n] == 0) kp_add_status_overlay(g, n, MAELSTROM_OVERLAY + kp_overlay_size(g, n));
	if (u->maelstrom_timer[n] < kp_band(duration, 0xff)) u->maelstrom_timer[n] = kp_band(duration, 0xff);
	kp_disable_unit(g, n);
}

/* MaelstromImpact 0x430820 with its callback 0x4308d0. */
void kp_maelstrom(KpGame *g, int64_t x, int64_t y, int64_t source)
{
	KpUnits *u = &g->u;
	int64_t *found;
	int count, k;
	field_sprite(g, MAELSTROM_SPRITE, x, y);
	kp_play_unit_sound(g, MAELSTROM_SOUND, source);
	found = kp_query_rect(g, kp_s16v(x - FIELD_RADIUS), kp_s16v(y - FIELD_RADIUS), kp_s16v(x + FIELD_RADIUS),
		kp_s16v(y + FIELD_RADIUS), &count);
	for (k = 0; k < count; k++) {
		int64_t c = found[k];
		if (kp_band(u->status[c], STATUS_HALLUCINATION) != 0) {
			kp_unit_kill(g, c);
			continue;
		}
		if (c == source || kp_band(u->status[c], STATUS_INVINCIBLE | STATUS_BURROWED) != 0
			|| kp_band(dat_flags(g, c), DAT_BUILDING) != 0) {
			continue;
		}
		if (kp_band(dat_flags(g, c), DAT_ORGANIC) != 0) apply_maelstrom(g, c, MAELSTROM_TIME);
	}
	kp_query_release(g, found);
}

/* CreateDisruptionWeb 0x4307a0. */
void kp_create_disruption_web(KpGame *g, int64_t x, int64_t y, int64_t owner)
{
	KpUnits *u = &g->u;
	int64_t n;
	(void)owner; /* ReportUnitCreationError 0x425020 of a failure is the interface's */
	kp_clamp_to_map(g, DISRUPTION_WEB, &x, &y);
	n = kp_unit_create(g, DISRUPTION_WEB, x, y, NEUTRAL);
	if (n == 0) return;
	u->status[n] = kp_bor(u->status[n], STATUS_NO_COLLIDE);
	g->w->spr.elevation[kp_unit_sprite(g, n)] = SWARM_ELEVATION;
	kp_unit_complete_in_place(g, n);
	if (u->remove_timer[n] == 0 || u->remove_timer[n] > WEB_LIFE) u->remove_timer[n] = WEB_LIFE;
}
