/* The trigger actions on units (plan phase 5.3; atlas P40; docs/research/native-trigger-unit-actions.md,
 * native-trigger-ownership.md, native-trigger-move.md): Create Unit (with Properties), Kill and Remove Unit (at a
 * location), Order, Move Unit, Give Units to Player, Set Invincibility, Modify Unit Hit Points, Energy and Shields,
 * Set Doodad State, Set Alliance Status and Run AI Script. */
#include "kp_game_internal.h"

#include <string.h>

#define NEUTRAL_PLAYER 11
#define DAT_WORKER 0x8
#define DAT_SUBUNIT 0x10
#define DAT_POWERUP 0x800
/* the quantity of a Kill, Remove or Move without one (0x4b6870, 0x4b5840) */
#define ALL_UNITS 999999
#define WHOLE_PLAYER 255 /* the location of Kill Unit and Remove Unit (0x4b6840) */
#define ALL_MODIFIED 65535 /* the quantity of Give and the Modify actions without one (0x4b5a50) */
/* the alliance values */
#define ALLY 1
#define ALLIED_VICTORY 2
/* units */
#define DAT_BUILDING 0x1
#define DAT_RESOURCE_CONTAINER 0x2000
#define DAT_SPELLCASTER 0x200000
#define DAT_INVINCIBLE 0x20000000
#define STATUS_COMPLETED 0x1
#define STATUS_GROUNDED_BUILDING 0x2
#define STATUS_BURROWED 0x10
#define STATUS_DISABLED 0x400
#define STATUS_CAN_MOVE 0x20000
#define STATUS_INVINCIBLE 0x4000000
#define STATUS_HALLUCINATION 0x40000000
#define CONTROLLER_RESCUABLE 3
#define CONTROLLER_UNUSED 4
#define NUCLEAR_MISSILE 14
#define BROODLING 40
#define INTERCEPTOR 73
#define SCARAB 85
#define DARK_SWARM 202
#define TRAP_FIRST 203 /* traps and doors (IsTrapOrDoorType 0x425ca0) */
#define TRAP_LAST 213
/* orders */
#define ORDER_MOVE 6
#define ORDER_ATTACK_MOVE 14
#define ORDER_NOTHING 23
#define ORDER_BUILDING_LAND 0x47
#define ORDER_BUILDING_LIFTOFF 0x48
#define ORDER_PATROL 152
#define ORDER_ENABLE_DOODAD 172
#define ORDER_DISABLE_DOODAD 173
#define NO_TYPE 228
/* the auxiliary value 0x673934 Create Unit sets while it creates (0x4781f0) and restores */
#define CREATE_AUXILIARY 123456789u

static uint32_t u32_at(const uint8_t *p) { return kp_le_u32(p); }


/* ApplyUnitRecordProperties 0x46a420 with UPRP record k (1-64). */
static void apply_properties(KpGame *g, int64_t n, int64_t k)
{
	const uint8_t *p = g->trig.properties + (size_t)(k - 1) * KP_UNIT_PROPERTY_SIZE;
	KpUnitRecord r;
	r.valid_states = kp_le_u16(p);
	r.valid_properties = kp_le_u16(p + 2);
	r.hp_percent = p[5];
	r.shield_percent = p[6];
	r.energy_percent = p[7];
	r.resources = kp_le_u32(p + 8);
	r.hangar = kp_le_u16(p + 12);
	r.states = kp_le_u16(p + 14);
	kp_apply_unit_record(g, n, &r);
}

/* TrigActCreateUnit 0x4b64d0 (actions 11 and 44): `quantity` times (0 is one) CreateTriggerUnitForSelector 0x4b7020
 * for each selected player below 8 at the location's center, with UPRP record `property` (1-64) when made. */
void kp_trigger_create(KpGame *g, const uint8_t *a)
{
	int64_t players[KP_PLAYERS], location, type = kp_le_u16(a + KP_ACTION_UNIT), property = u32_at(a + KP_ACTION_NUMBER), q, x, y;
	uint32_t saved;
	int count, k;
	if (a[KP_ACTION_LOCATION] == 0) return;
	location = a[KP_ACTION_LOCATION] - 1;
	saved = g->rng->initial_seed;
	g->rng->initial_seed = CREATE_AUXILIARY;
	for (q = a[KP_ACTION_MODIFIER] != 0 ? a[KP_ACTION_MODIFIER] : 1; q > 0; q--) {
		count = kp_trigger_players(g, u32_at(a + KP_ACTION_PLAYER), players);
		for (k = 0; k < count; k++) {
			int64_t p = players[k], n;
			if (p < 0 || p >= 8) continue;
			if (kp_resource_type(type)) p = NEUTRAL_PLAYER;
			kp_location_center(g, location, &x, &y);
			n = kp_create_unit_checked(g, type, x, y, p);
			if (n != 0 && property >= 1 && property <= KP_UNIT_PROPERTIES) apply_properties(g, n, property);
		}
	}
	g->rng->initial_seed = saved;
}

/* IsUnitInactive 0x41fe40: no sprite or the Die order. */
static int inactive(KpGame *g, int64_t n) { return g->u.sprite[n] == KP_NONE || g->u.order[n] == 0; }

/* The Kill and Remove predicates 0x4b8c40 (Any Unit), 0x4b8e10 (Men), 0x4b8c60 (Buildings), 0x4b8c90 (Factories)
 * and 0x4b8e40 (one type, its low WORD): the unit's owner and type. */
static int kill_matches(KpGame *g, int64_t player, int64_t type, int64_t n)
{
	return g->u.owner[n] == player && kp_trigger_type_matches(g, g->u.type[n], kp_band(type, 0xffff));
}

/* The kill or removal of one unit (0x4b73a0, 0x4b7d00); a transport removed with KillUnitFlagged 0x425700. */
static void kill_one(KpGame *g, int64_t n, int remove, int transport_flagged)
{
	KpUnits *u = &g->u;
	if (kp_band(KP_DAT(g, units, special_ability_flags, u->type[n]), DAT_POWERUP) != 0 && u->union_0xd4[n] != 0) {
		/* TODO: a carried powerup dies with its worker's cargo (DestroyWorkerCargo 0x4271d0) */
		kp_game_fail(g, "triggers: killing a carried powerup (0x4271d0)");
	}
	if (remove && transport_flagged) {
		kp_kill_flagged(g, n);
		return;
	}
	if (remove) kp_unit_hide(g, n); /* HideUnit 0x422ba0 */
	kp_unit_kill(g, n);
}

/* KillPlayerUnits 0x4b73a0: the player's units (its list, the next unit read before the kill) up to the limit. */
static void kill_player_units(KpGame *g, int64_t p, int64_t type, int64_t limit, int remove)
{
	int64_t n, next;
	if (p < 0 || p >= KP_PLAYERS) kp_game_fail(g, "triggers: killing the units of player slot %lld", (long long)p);
	for (n = g->player_units[p]; n != 0; n = next) {
		next = g->u.player_next[n];
		if (kp_band(KP_DAT(g, units, special_ability_flags, g->u.type[n]), DAT_SUBUNIT) != 0 || inactive(g, n)) continue;
		if (!kill_matches(g, p, type, n)) continue;
		kill_one(g, n, remove, 0);
		if (--limit == 0) return;
	}
}

/* The state KillUnitsAtLocation 0x4b7c70 keeps in globals: the remove mode 0x6c4c00, the limit 0x6c4c04 and the flag
 * 0x6c4c08 of a recursive child, which each child's visit sets and clears again (so a visit nested in a child's ends
 * it early, as in the original). */
typedef struct {
	int64_t player, type, flags, limit;
	int remove, in_child;
} KillAt;

/* KillAtLocationCallback 0x4b7d00: a transport's cargo and a worker's powerup first, then the unit; 1 when the limit is
 * spent. A child's kill does not count while the child flag is set. */
static int kill_at(KpGame *g, int64_t n, KillAt *k, int check)
{
	KpUnits *u = &g->u;
	int transport;
	if (check && kp_location_excludes(g, n, k->flags)) return 0;
	transport = kp_is_transport(g, n);
	if (transport) {
		int64_t cargo[KP_CARGO_SLOTS];
		int count = kp_cargo_units(g, n, cargo), j;
		for (j = 0; j < count; j++) {
			k->in_child = 1;
			kill_at(g, cargo[j], k, 0);
			k->in_child = 0;
		}
	}
	if (kp_band(KP_DAT(g, units, special_ability_flags, u->type[n]), DAT_WORKER) != 0 && u->union_0xc0[n] != 0) {
		k->in_child = 1;
		kill_at(g, kp_game_unit(g, u->union_0xc0[n]), k, 0);
		k->in_child = 0;
	}
	if (inactive(g, n) || !kill_matches(g, k->player, k->type, n)) return 0;
	kill_one(g, n, k->remove, transport);
	if (!k->in_child) k->limit--;
	return k->limit == 0;
}

/* KillUnitsAtLocation 0x4b7c70 over the location's units (0x443110). */
static void kill_location_units(KpGame *g, int64_t p, int64_t type, int64_t index, int64_t limit, int remove)
{
	int64_t units[KP_UNITS], flags;
	int count = kp_location_units(g, index, units, &flags), j;
	KillAt k;
	k.player = p;
	k.type = type;
	k.flags = flags;
	k.limit = limit;
	k.remove = remove;
	k.in_child = 0;
	for (j = 0; j < count; j++) {
		if (kill_at(g, units[j], &k, 1)) return;
	}
}

/* KillUnitsForSelector 0x4b8cc0 over the selector's players (the limited aggregators 0x4b96c0 and others): the limit
 * applies to each player. */
static void kill_units(KpGame *g, int64_t selector, int64_t type, int64_t location, int64_t limit, int remove)
{
	int64_t players[KP_PLAYERS];
	int count = kp_trigger_players(g, selector, players), k;
	for (k = 0; k < count; k++) {
		if (location >= WHOLE_PLAYER) kill_player_units(g, players[k], type, limit, remove);
		else kill_location_units(g, players[k], type, location, limit, remove);
	}
}

/* TrigActKillUnit 0x4b6840 and TrigActRemoveUnit 0x4b68c0: every unit of the type of the selected players. */
void kp_trigger_kill(KpGame *g, const uint8_t *a, int remove)
{
	kill_units(g, u32_at(a + KP_ACTION_PLAYER), kp_le_u16(a + KP_ACTION_UNIT), WHOLE_PLAYER, ALL_UNITS, remove);
	kp_trigger_invalidate_counts(g);
}

/* TrigActKillUnitAtLocation 0x4b6870 and TrigActRemoveUnitAtLocation 0x4b68f0: up to the quantity (0 is all) of
 * each selected player at the location. */
void kp_trigger_kill_at(KpGame *g, const uint8_t *a, int remove)
{
	if (a[KP_ACTION_LOCATION] == 0) return;
	kill_units(g, u32_at(a + KP_ACTION_PLAYER), kp_le_u16(a + KP_ACTION_UNIT), a[KP_ACTION_LOCATION] - 1,
		a[KP_ACTION_MODIFIER] != 0 ? a[KP_ACTION_MODIFIER] : ALL_UNITS, remove);
	kp_trigger_invalidate_counts(g);
}

/* The center of a destination location given by an action's full number (1-255; 0x534f2c + 20 * number). */
static void destination(KpGame *g, uint32_t number, int64_t *x, int64_t *y)
{
	if (number < 1 || number > KP_LOCATIONS) kp_game_fail(g, "triggers: destination location %u", (unsigned)number);
	kp_location_center(g, number - 1, x, y);
}

static int building_lifting(KpGame *g, int64_t n)
{
	int64_t o = g->u.order[n];
	return kp_band(KP_DAT(g, units, special_ability_flags, g->u.type[n]), DAT_BUILDING) != 0
		&& (o == ORDER_BUILDING_LAND || o == ORDER_BUILDING_LIFTOFF);
}

/* OrderUnitCallback 0x4b75d0 */
static void order_one(KpGame *g, int64_t n, int64_t owners, int64_t type, int64_t flags, int64_t x, int64_t y,
	int64_t kind)
{
	KpUnits *u = &g->u;
	int64_t t = u->type[n], order, c;
	if (kp_location_excludes(g, n, flags) || !kp_trigger_owned_by(owners, u->owner[n]) || building_lifting(g, n)) return;
	if (kp_band(u->status[n], STATUS_CAN_MOVE) == 0 || t == NUCLEAR_MISSILE || t == INTERCEPTOR || t == SCARAB) return;
	c = kp_controller(g, u->owner[n]); /* IsRescuableOwner 0x4292d0 */
	if (c == CONTROLLER_RESCUABLE || c == CONTROLLER_UNUSED || !kp_trigger_type_matches(g, t, type)) return;
	switch (kind) {
	case 0: order = ORDER_MOVE; break;
	case 1: order = ORDER_PATROL; break;
	case 2: order = ORDER_ATTACK_MOVE; break;
	default: order = ORDER_NOTHING; break;
	}
	if (kp_band(u->status[n], STATUS_BURROWED) != 0) {
		kp_request_unburrow(g, n);
		kp_order_append(g, n, order, x, y, 0, NO_TYPE, 0); /* QueueOrderAtPoint 0x429ee0 */
	} else {
		if (kp_can_issue_order(g, order, u->owner[n], n) != 1) return;
		kp_issue_order_at(g, n, order, x, y);
	}
	if (u->ai[n] != 0 && (order == ORDER_MOVE || order == ORDER_ATTACK_MOVE)) kp_ai_set_guard_home(g, n, x, y);
}

/* TrigActOrder 0x4b65a0: the units at the source location take Move, Patrol, Attack Move or Nothing to the
 * destination's center. */
void kp_trigger_order(KpGame *g, const uint8_t *a)
{
	int64_t units[KP_UNITS], flags, x, y, owners;
	uint32_t dest = u32_at(a + KP_ACTION_NUMBER);
	int count, k;
	if (a[KP_ACTION_LOCATION] == 0 || dest == 0) return;
	destination(g, dest, &x, &y);
	owners = kp_trigger_owners(g, u32_at(a + KP_ACTION_PLAYER));
	count = kp_location_units(g, a[KP_ACTION_LOCATION] - 1, units, &flags);
	for (k = 0; k < count; k++) {
		order_one(g, units[k], owners, kp_le_u16(a + KP_ACTION_UNIT), flags, kp_s16v(x), kp_s16v(y), a[KP_ACTION_MODIFIER]);
	}
}

/* MoveOrdinaryUnit 0x4b7a80: the unit leaves, goes to the nearest free place around the point (FindUnitPlacement
 * 0x47dc30) or back where it was, and a completed one ends its order. */
static void move_ordinary(KpGame *g, int64_t n, int64_t x, int64_t y)
{
	int32_t s = kp_unit_sprite(g, n);
	int64_t ox = kp_s16v(g->w->spr.x[s]), oy = kp_s16v(g->w->spr.y[s]), px, py;
	kp_unit_detach(g, n, 0);
	kp_unit_relocate(g, n, x, y);
	if (!kp_find_unit_placement(g, n, x, y, NULL, 0, 0, &px, &py)) {
		kp_unit_relocate(g, n, ox, oy);
		kp_unit_reattach(g, n);
		return;
	}
	kp_unit_relocate(g, n, px, py);
	kp_unit_reattach(g, n);
	if (kp_band(g->u.status[n], STATUS_COMPLETED) != 0) kp_order_done(g, n);
}

/* MoveUnitCallback 0x4b7b20: 1 when the quantity is spent. */
static int move_one(KpGame *g, int64_t n, int64_t owners, int64_t type, int64_t flags, int64_t x, int64_t y,
	int64_t *quantity)
{
	KpUnits *u = &g->u;
	int64_t t = u->type[n];
	if (kp_location_excludes(g, n, flags) || inactive(g, n) || !kp_trigger_owned_by(owners, u->owner[n])) return 0;
	if (building_lifting(g, n) || t == NUCLEAR_MISSILE || t == DARK_SWARM || kp_resource_type(t)
		|| kp_band(KP_DAT(g, units, special_ability_flags, t), DAT_RESOURCE_CONTAINER) != 0
		|| (t >= TRAP_FIRST && t <= TRAP_LAST) || !kp_trigger_type_matches(g, t, type)) {
		return 0;
	}
	if (kp_band(u->status[n], STATUS_GROUNDED_BUILDING) != 0) {
		/* TODO: a grounded building's move (MoveGroundedBuilding 0x4b7810 with 0x412e20, plan phase 6) */
		kp_game_fail(g, "triggers: moving a grounded building (0x4b7810)");
	}
	move_ordinary(g, n, x, y);
	return --*quantity == 0;
}

/* TrigActMoveUnit 0x4b5840 */
void kp_trigger_move_units(KpGame *g, const uint8_t *a)
{
	int64_t units[KP_UNITS], flags, x, y, owners, quantity = a[KP_ACTION_MODIFIER] != 0 ? a[KP_ACTION_MODIFIER] : ALL_UNITS;
	int count, k;
	if (a[KP_ACTION_LOCATION] == 0 || u32_at(a + KP_ACTION_NUMBER) == 0) return;
	/* the destination is the number's low byte less one (the converter rejects a low byte of 0: location 256) */
	kp_location_center(g, kp_band(a[KP_ACTION_NUMBER] - 1, 0xff), &x, &y);
	owners = kp_trigger_owners(g, u32_at(a + KP_ACTION_PLAYER));
	count = kp_location_units(g, a[KP_ACTION_LOCATION] - 1, units, &flags);
	for (k = 0; k < count; k++) {
		if (move_one(g, units[k], owners, kp_le_u16(a + KP_ACTION_UNIT), flags, kp_s16v(x), kp_s16v(y), &quantity)) break;
	}
	kp_trigger_invalidate_counts(g);
}

/* ModifyUnitsCallback 0x4b7750 over a location's units: each unit of the selector's owners and type gets fn(value)
 * until the quantity (0 is 65535) is spent; a unit counts when fn accepts it. */
static void modify_units(KpGame *g, const uint8_t *a, int (*fn)(KpGame *, int64_t, int64_t), int64_t value)
{
	int64_t units[KP_UNITS], flags, owners, quantity = a[KP_ACTION_MODIFIER] != 0 ? a[KP_ACTION_MODIFIER] : ALL_MODIFIED;
	int count, k;
	if (a[KP_ACTION_LOCATION] == 0) return;
	owners = kp_trigger_owners(g, u32_at(a + KP_ACTION_PLAYER));
	count = kp_location_units(g, a[KP_ACTION_LOCATION] - 1, units, &flags);
	for (k = 0; k < count && quantity > 0; k++) {
		int64_t n = units[k];
		if (kp_location_excludes(g, n, flags) || !kp_trigger_owned_by(owners, g->u.owner[n])
			|| !kp_trigger_type_matches(g, g->u.type[n], kp_le_u16(a + KP_ACTION_UNIT))) {
			continue;
		}
		if (fn(g, n, value)) quantity--;
	}
}

/* TrigActGiveUnits 0x4b5a50 */
void kp_trigger_give(KpGame *g, const uint8_t *a) { modify_units(g, a, kp_give_unit_checked, u32_at(a + KP_ACTION_NUMBER)); }

/* ModifyHitPointsCallback 0x4b8110: max(1, max hit points (fixed) * percent / 100) in u32 arithmetic; a
 * hallucination's or a Broodling's timed life does not grow past its duration (0x434a90). */
static int modify_hp(KpGame *g, int64_t n, int64_t percent)
{
	KpUnits *u = &g->u;
	int64_t hp = kp_u32v(kp_u32v(KP_DAT(g, units, hit_points, u->type[n]) * percent) / 100);
	kp_set_unit_hp(g, n, hp != 0 ? hp : 1);
	if (kp_band(u->status[n], STATUS_HALLUCINATION) != 0 || u->type[n] == BROODLING) {
		int64_t life = kp_band(kp_timed_life(g, n), 0xffff), now = kp_band(u->remove_timer[n], 0xffff);
		if (now == 0 || life < now) u->remove_timer[n] = life;
	}
	return 1;
}

/* ModifyEnergyCallback 0x4b7fd0: a spellcaster that is no hallucination gets the low byte of max * percent / 100,
 * at most its maximum. */
static int modify_energy(KpGame *g, int64_t n, int64_t percent)
{
	KpUnits *u = &g->u;
	int64_t max, e;
	if (kp_band(KP_DAT(g, units, special_ability_flags, u->type[n]), DAT_SPELLCASTER) == 0
		|| kp_band(u->status[n], STATUS_HALLUCINATION) != 0) {
		return 0;
	}
	max = kp_sarv(kp_max_energy(g, n), 8);
	e = kp_band(kp_u32v(max * percent) / 100, 0xff);
	if (max < e) e = max;
	u->energy[n] = kp_band(e * 256, 0xffff);
	return 1;
}

/* ModifyShieldsCallback 0x4b85e0: a unit with shields gets min(max, max * percent / 100). */
static int modify_shields(KpGame *g, int64_t n, int64_t percent)
{
	KpUnits *u = &g->u;
	int64_t max, v;
	if (KP_DAT(g, units, shield_enabled, u->type[n]) == 0) return 0;
	max = KP_DAT(g, units, shield_amount, u->type[n]);
	v = kp_u32v(max * percent) / 100;
	if (max < v) v = max;
	u->shields[n] = v * 256;
	return 1;
}

/* TrigActModifyHitPoints 0x4b5b00, TrigActModifyEnergy 0x4b5bb0, TrigActModifyShields 0x4b5c60 */
void kp_trigger_modify(KpGame *g, const uint8_t *a)
{
	switch (a[KP_ACTION_TYPE]) {
	case KP_ACT_MODIFY_HP: modify_units(g, a, modify_hp, u32_at(a + KP_ACTION_NUMBER)); break;
	case KP_ACT_MODIFY_ENERGY: modify_units(g, a, modify_energy, u32_at(a + KP_ACTION_NUMBER)); break;
	default: modify_units(g, a, modify_shields, u32_at(a + KP_ACTION_NUMBER)); break;
	}
}

/* SetInvincibilityCallback 0x4b7440: set, clear or toggle (an invincible type stays so); a transport's cargo too. */
static void invincibility_one(KpGame *g, int64_t n, int64_t owners, int64_t type, int64_t flags, int check,
	int64_t modifier)
{
	KpUnits *u = &g->u;
	int dat_invincible;
	if (check && kp_location_excludes(g, n, flags)) return;
	if (!kp_trigger_owned_by(owners, u->owner[n])) return;
	if (kp_is_transport(g, n)) {
		int64_t cargo[KP_CARGO_SLOTS];
		int count = kp_cargo_units(g, n, cargo), k;
		for (k = 0; k < count; k++) invincibility_one(g, cargo[k], owners, type, flags, 0, modifier);
	}
	if (!kp_trigger_type_matches(g, u->type[n], type)) return;
	dat_invincible = kp_band(KP_DAT(g, units, special_ability_flags, u->type[n]), DAT_INVINCIBLE) != 0;
	switch (modifier) {
	case KP_MOD_SET: u->status[n] = kp_bor(u->status[n], STATUS_INVINCIBLE); break;
	case KP_MOD_CLEAR:
		if (!dat_invincible) u->status[n] = kp_band(u->status[n], kp_bnot(STATUS_INVINCIBLE));
		break;
	case KP_MOD_TOGGLE:
		if (kp_band(u->status[n], STATUS_INVINCIBLE) == 0) u->status[n] = kp_bor(u->status[n], STATUS_INVINCIBLE);
		else if (!dat_invincible) u->status[n] = kp_band(u->status[n], kp_bnot(STATUS_INVINCIBLE));
		break;
	default: break;
	}
}

/* TrigActSetInvincibility 0x4b59c0 */
void kp_trigger_invincibility(KpGame *g, const uint8_t *a)
{
	int64_t units[KP_UNITS], flags, owners;
	int count, k;
	if (a[KP_ACTION_LOCATION] == 0) return;
	owners = kp_trigger_owners(g, u32_at(a + KP_ACTION_PLAYER));
	count = kp_location_units(g, a[KP_ACTION_LOCATION] - 1, units, &flags);
	for (k = 0; k < count; k++) {
		invincibility_one(g, units[k], owners, kp_le_u16(a + KP_ACTION_UNIT), flags, 1, a[KP_ACTION_MODIFIER]);
	}
}

/* The disabled test of the doodad helpers (0x428a80): disabled, locked down, in stasis or in a maelstrom. */
static int doodad_disabled(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	return kp_band(u->status[n], STATUS_DISABLED) != 0 || u->lockdown_timer[n] != 0 || u->stasis_timer[n] != 0
		|| u->maelstrom_timer[n] != 0;
}

/* TrigActSetDoodadState 0x4b5930 on the first matching unit at the location: RequestEnableDoodad 0x428a80,
 * RequestDisableDoodad 0x427e90 or ToggleDoodadState 0x429420. */
void kp_trigger_doodad(KpGame *g, const uint8_t *a)
{
	KpUnits *u = &g->u;
	int64_t n;
	if (a[KP_ACTION_LOCATION] == 0) return;
	n = kp_trigger_unit_at(g, a[KP_ACTION_LOCATION] - 1, u32_at(a + KP_ACTION_PLAYER), kp_le_u16(a + KP_ACTION_UNIT));
	if (n == 0) return;
	switch (a[KP_ACTION_MODIFIER]) {
	case KP_MOD_SET:
		if (!doodad_disabled(g, n)) return;
		u->status[n] = kp_band(u->status[n], kp_bnot(STATUS_DISABLED));
		kp_issue_order(g, n, ORDER_ENABLE_DOODAD);
		break;
	case KP_MOD_CLEAR:
		if (!doodad_disabled(g, n)) kp_issue_order(g, n, ORDER_DISABLE_DOODAD);
		break;
	case 0: case KP_MOD_TOGGLE:
		if (!doodad_disabled(g, n)) {
			kp_issue_order(g, n, ORDER_DISABLE_DOODAD);
		} else {
			u->status[n] = kp_band(u->status[n], kp_bnot(STATUS_DISABLED));
			kp_issue_order(g, n, ORDER_ENABLE_DOODAD);
		}
		break;
	default: break;
	}
}

/* SetAllianceForSelector 0x4b6e90 for one slot: the current player's stance towards p; a new nonzero stance unlike
 * its own towards itself replaces every nonzero stance of its row. */
static void set_alliance(KpGame *g, int64_t p, int64_t status)
{
	int64_t current = g->trig.player, v = status == 1 ? ALLY : status == 2 ? ALLIED_VICTORY : 0, q;
	if (p == current) return;
	if (p < 0 || p >= KP_PLAYERS) kp_game_fail(g, "triggers: the alliance with player slot %lld", (long long)p);
	kp_set_alliance(g, current, p, v);
	if (v == 0 || v == kp_alliance(g, current, current)) return;
	for (q = 0; q < KP_PLAYERS; q++) {
		if (kp_alliance(g, current, q) != 0) kp_set_alliance(g, current, q, v);
	}
}

/* TrigActSetAllianceStatus 0x4b5e90: the selected players one by one against the changing matrix, then
 * ClearAlliedOrderTargets 0x4721c0 over the current player's units (the colors 0x41d7b0 are the interface's). */
void kp_trigger_alliance(KpGame *g, const uint8_t *a)
{
	KpUnits *u = &g->u;
	int64_t selector = u32_at(a + KP_ACTION_PLAYER), status = kp_le_u16(a + KP_ACTION_UNIT), current = g->trig.player, p, n;
	if (selector < KP_SEL_UNKNOWN) {
		if (!kp_trigger_group(selector)) {
			if (selector != KP_SEL_CURRENT) set_alliance(g, selector, status);
		} else {
			for (p = 0; p < KP_PLAYERS; p++) {
				if (kp_trigger_group_takes(g, selector, p)) set_alliance(g, p, status);
			}
		}
	}
	for (n = g->player_units[current]; n != 0; n = u->player_next[n]) {
		int64_t target = u->order_unit[n];
		if (target == 0 || KP_DAT(g, orders, use_weapon_targeting, u->order[n]) == 0) continue;
		if (kp_alliance(g, current, u->owner[target]) == 0) continue;
		if (!kp_is_enemy(g, n, target)) u->order_unit[n] = 0; /* 0x420950 */
	}
}
