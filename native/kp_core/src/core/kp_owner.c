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
