/* Per-step unit timers (kp_game.h). */
#include "kp_game_internal.h"

#define SPRITE_HIDDEN 0x20
#define DAT_REGENERATES 0x80
#define DAT_SPELLCASTER 0x200000
#define DAT_FLYING_BUILDING 0x20
#define STATUS_COMPLETED 0x1
#define STATUS_GROUNDED_BUILDING 0x2
#define STATUS_CLOAKED 0x300
#define STATUS_FREE_CLOAK 0x800
#define STATUS_HALLUCINATION 0x40000000
#define SHIELD_REGEN 7
#define ENERGY_REGEN 8
#define SPELL_PERIOD 8
#define BURN_DAMAGE 0x14
#define REGENERATION 4
#define SUMMON_ENERGY 0x3200

static void decrement(int64_t *t, int64_t n)
{
	if (t[n] != 0) t[n] = t[n] - 1;
}

int64_t kp_energy_cap(KpGame *g, int64_t n)
{
	/* GetEnergyCap 0x434a50: a Dark Archon still in its summon (IsArchonSummonState0 0x42ffd0) holds 50 */
	if (g->u.type[n] == KP_TYPE_DARK_ARCHON && g->u.order[n] == KP_ORD_ARCHON_SUMMON && g->u.order_state[n] == 0) return SUMMON_ENERGY;
	return kp_max_energy(g, n);
}

/* GetCloakEnergyUpkeep 0x4348c0: the energy a cloaked caster pays per energy tick. */
static int64_t cloak_upkeep(int64_t t)
{
	if (kp_ghost_type(t) || t == 51) return 10;
	return t == 8 || t == 21 ? 13 : 0;
}

/* UpdateUnitEnergy 0x438e20. */
void kp_update_unit_energy(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t status = u->status[n], energy, cap;
	if (kp_band(KP_DAT(g, units, special_ability_flags, u->type[n]), DAT_SPELLCASTER) == 0
		|| kp_band(status, STATUS_HALLUCINATION) != 0 || kp_band(status, STATUS_COMPLETED) == 0) {
		return;
	}
	if (kp_band(status, STATUS_CLOAKED) != 0 && kp_band(status, STATUS_FREE_CLOAK) == 0) {
		int64_t upkeep = cloak_upkeep(u->type[n]);
		if (kp_needs_energy(g, n, upkeep)) {
			/* out of energy: the cloak ends */
			if (u->secondary_order[n] == KP_ORD_CLOAK) kp_set_secondary_order(g, n, KP_ORD_NOTHING);
			return;
		}
		/* the selected sprite's health bar redraw (0x416bb0) is the presentation's */
		kp_spend_energy(g, n, upkeep);
		return;
	}
	energy = u->energy[n];
	cap = kp_energy_cap(g, n);
	if (energy == cap) return;
	energy += ENERGY_REGEN;
	if (energy > cap) energy = cap;
	u->energy[n] = energy;
}

void kp_update_spell_timers(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	if (u->stasis_timer[n] != 0) {
		u->stasis_timer[n]--;
		if (u->stasis_timer[n] == 0) kp_end_stasis(g, n);
	}
	if (u->stim_timer[n] != 0) {
		u->stim_timer[n]--;
		if (u->stim_timer[n] == 0) kp_recalculate_speed(g, n); /* RecalculateUnitSpeed 0x43aea0 */
	}
	if (u->ensnare_timer[n] != 0) {
		u->ensnare_timer[n]--;
		if (u->ensnare_timer[n] == 0) kp_end_ensnare(g, n);
	}
	if (u->matrix_timer[n] != 0) {
		u->matrix_timer[n]--;
		if (u->matrix_timer[n] == 0) kp_reduce_matrix(g, n, u->matrix_damage[n]);
	}
	if (u->irradiate_timer[n] != 0) kp_update_irradiate(g, n);
	if (u->lockdown_timer[n] != 0) {
		u->lockdown_timer[n]--;
		if (u->lockdown_timer[n] == 0) kp_end_lockdown(g, n);
	}
	if (u->maelstrom_timer[n] != 0) {
		u->maelstrom_timer[n]--;
		if (u->maelstrom_timer[n] == 0) kp_end_maelstrom(g, n);
	}
	if (u->plague_timer[n] != 0) kp_update_plague(g, n);
	decrement(u->storm_timer, n);
	kp_update_acid_spores(g, n);
}

void kp_update_unit_timers(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t t = u->type[n], hp;
	int32_t s;
	decrement(u->order_timer, n);
	decrement(u->ground_cooldown, n);
	decrement(u->air_cooldown, n);
	decrement(u->spell_cooldown, n);
	s = (int32_t)u->sprite[n];
	if (KP_DAT(g, units, shield_enabled, t) != 0) {
		int64_t max = KP_DAT(g, units, shield_amount, t) * 256;
		if (u->shields[n] != max) {
			int64_t v = u->shields[n] + SHIELD_REGEN;
			if (v > max) v = max;
			u->shields[n] = v;
		}
	}
	if ((t == KP_TYPE_ZERGLING || t == KP_TYPE_DEVOURING_ONE) && u->ground_cooldown[n] == 0) u->queue_timer[n] = 0;
	u->being_healed[n] = 0;
	if (kp_band(u->status[n], STATUS_COMPLETED) != 0 || kp_band(g->w->spr.flags[kp_check_sprite(g->w, s)], SPRITE_HIDDEN) == 0) {
		u->cycle_counter[n] = kp_mod(u->cycle_counter[n] + 1, 256);
		if (u->cycle_counter[n] >= SPELL_PERIOD) {
			u->cycle_counter[n] = 0;
			kp_update_spell_timers(g, n);
		}
	}
	if (kp_band(u->status[n], STATUS_COMPLETED) == 0) return;
	hp = u->hp[n];
	if (kp_band(KP_DAT(g, units, special_ability_flags, t), DAT_REGENERATES) != 0 && hp > 0
		&& hp != KP_DAT(g, units, hit_points, t)) {
		kp_set_unit_hp(g, n, hp + REGENERATION); /* AddUnitHp 0x41b900 */
	}
	kp_update_unit_energy(g, n);
	decrement(u->recent_order_timer, n);
	if (u->remove_timer[n] != 0) {
		u->remove_timer[n]--;
		if (u->remove_timer[n] == 0) {
			kp_unit_kill(g, n);
			return;
		}
	}
	if (kp_unit_race(g, n) == KP_RACE_TERRAN
		&& (kp_band(u->status[n], STATUS_GROUNDED_BUILDING) != 0
			|| kp_band(KP_DAT(g, units, special_ability_flags, t), DAT_FLYING_BUILDING) != 0)) {
		int64_t max = kp_sarv(KP_DAT(g, units, hit_points, t), 8);
		if (max == 0) {
			max = kp_sarv(hp + 0xff, 8);
			if (max == 0) max = 1;
		}
		/* IsHpAtMostOneThird 0x438dd0 */
		if (kp_idiv64(g, kp_sarv(hp + 0xff, 8) * 100, max) < 0x22) {
			kp_damage_hit_points(g, n, BURN_DAMAGE, 0, u->last_attacker[n], 1);
		}
	}
}
