/* The queries of the trigger engine (plan phase 5.2; atlas P40; docs/research/native-trigger-counters.md,
 * native-trigger-unit-counts.md): the player selectors, the scalar counters (resources, scores, deaths, kills) with
 * their setters, the unit-count cache of Command and Bring, and the locations (the elevation filter, the first
 * trigger unit at a location, its center). Values are the original's u32s: sums wrap, subtractions stop at 0. */
#include "kp_game_internal.h"

#include <string.h>

/* the alliance bytes the group selectors test (0x534914) */
#define ENEMY 0
#define ALLY 1
#define ALLIED_VICTORY 2
/* the unit groups of a condition or action */
#define ANY_UNIT 229
#define MEN 230
#define BUILDINGS 231
#define FACTORIES 232
#define GROUP_MEN 0x08
#define GROUP_BUILDING 0x10
#define GROUP_FACTORY 0x20
/* the per-player statistics by native address (0x5290c4 + 0x30 * k) */
#define PSTAT(addr) (((addr) - 0x5290c4) / 0x30)
#define MEN_LIVE PSTAT(0x5290c4)
#define MEN_COMPLETED PSTAT(0x5290f4)
#define MEN_LOST PSTAT(0x529154)
#define MEN_KILLED PSTAT(0x529184)
#define UNIT_SCORE PSTAT(0x5291b4)
#define KILL_SCORE PSTAT(0x5291e4)
#define BUILDINGS_LIVE PSTAT(0x529214)
#define BUILDINGS_COMPLETED PSTAT(0x529244)
#define BUILDINGS_LOST PSTAT(0x5292a4)
#define BUILDINGS_RAZED PSTAT(0x5292d4)
#define BUILDING_SCORE PSTAT(0x529304)
#define RAZING_SCORE PSTAT(0x529334)
#define FACTORIES_LIVE PSTAT(0x529364)
#define FACTORIES_COMPLETED PSTAT(0x529394)
#define FACTORIES_LOST PSTAT(0x5293c4)
#define FACTORIES_RAZED PSTAT(0x5293f4)
#define CUSTOM_SCORE PSTAT(0x5295d4)
/* the unit-count cache keys (0x6ca6d0) */
#define COUNTS_INVALID 0
#define COUNTS_GLOBAL 1
#define COUNTS_LOCATION 2
/* units */
#define STATUS_COMPLETED 0x1
#define STATUS_AIR 0x4
#define STATUS_HALLUCINATION 0x40000000
#define DAT_WORKER 0x8
#define DAT_SUBUNIT 0x10
#define FLAG_UNIT 215
#define NUCLEAR_SILO 108
#define NUCLEAR_MISSILE 14
#define ZERG_EGG 36
#define ZERG_COCOON 59
#define LURKER_EGG 97
/* the start of a least ranking's search (u32) */
#define LEAST_START 0xffffffffLL

/* --- the player selectors --- */

/* A group selector (Foes, Allies, Neutral Players, All Players, Forces 1-4, the unused 22-25 and Non-Allied Victory
 * Players), and whether it takes slot p now: the original's aggregators (0x4b9660 and the others) test each eligible
 * player (0x4b3160) as they reach it, and All Players adds slots 8-11. */
int kp_trigger_group(int64_t selector)
{
	int64_t s = kp_u32v(selector);
	return s > KP_SEL_CURRENT && s <= KP_SEL_NON_ALLIED_VICTORY;
}

int kp_trigger_group_takes(KpGame *g, int64_t selector, int64_t p)
{
	int64_t s = kp_u32v(selector), current = g->trig.player;
	if (p >= 8) return s == KP_SEL_ALL;
	if (!kp_trigger_player_active(g, p)) return 0;
	switch (s) {
	case KP_SEL_FOES: return p != current && kp_alliance(g, current, p) == ENEMY;
	case KP_SEL_ALLIES: return p != current && kp_alliance(g, current, p) == ALLIED_VICTORY;
	case KP_SEL_NEUTRAL: return p != current && kp_alliance(g, current, p) == ALLY;
	case KP_SEL_ALL: return 1;
	case KP_SEL_NON_ALLIED_VICTORY:
		return p != current
			&& !(kp_alliance(g, current, p) == ALLIED_VICTORY && kp_alliance(g, p, current) == ALLIED_VICTORY);
	default: return s >= KP_SEL_FORCE_FIRST && s <= KP_SEL_FORCE_LAST && g->players.force[p] == s - KP_SEL_ALL;
	}
}

/* The players of a selector, in the order the aggregators visit them; a direct selector names one slot. Returns the
 * count. */
int kp_trigger_players(KpGame *g, int64_t selector, int64_t *out)
{
	int64_t s = kp_u32v(selector), p;
	int count = 0;
	if (s == KP_SEL_CURRENT) {
		out[0] = g->trig.player;
		return 1;
	}
	if (!kp_trigger_group(s)) {
		out[0] = s;
		return 1;
	}
	for (p = 0; p < KP_PLAYERS; p++) {
		if (kp_trigger_group_takes(g, s, p)) out[count++] = p;
	}
	return count;
}

/* The owners (slots 0-11) a selector matches, a bit each (SelectorMatchesUnitOwner 0x4ba260 for every owner): an
 * action's callbacks test each unit's owner against the mask made once (kp_trigger_owned_by), as the alliances,
 * forces and controllers the selector reads do not change while they run. */
int64_t kp_trigger_owners(KpGame *g, int64_t selector)
{
	int64_t players[KP_PLAYERS], mask = 0;
	int count = kp_trigger_players(g, selector, players), k;
	for (k = 0; k < count; k++) {
		if (players[k] >= 0 && players[k] < KP_PLAYERS) mask |= (int64_t)1 << players[k];
	}
	return mask;
}

/* A direct slot a getter reads: the original reads past its arrays for a slot of 12 or more (no campaign does). */
static int64_t read_slot(KpGame *g, int64_t p)
{
	if (p < 0 || p >= KP_PLAYERS) kp_game_fail(g, "triggers: a counter of player slot %lld", (long long)p);
	return p;
}

/* --- the scalar counters --- */

/* GetResources 0x4ba410 of one slot */
static int64_t resources(KpGame *g, int64_t p, int64_t type)
{
	KpPlayers *pl = &g->players;
	p = read_slot(g, p);
	switch (type) {
	case KP_RES_MINERALS: return kp_u32v(pl->minerals[p]);
	case KP_RES_GAS: return kp_u32v(pl->gas[p]);
	case KP_RES_BOTH: return kp_u32v(pl->minerals[p] + pl->gas[p]);
	default: return 0;
	}
}

/* GetScore 0x4ba510 of one slot */
static int64_t score(KpGame *g, int64_t p, int64_t type)
{
	int64_t (*s)[KP_PLAYERS] = g->pstat;
	p = read_slot(g, p);
	switch (type) {
	case KP_SCORE_TOTAL:
		return kp_u32v(s[UNIT_SCORE][p] + s[BUILDING_SCORE][p] + s[KILL_SCORE][p] + s[RAZING_SCORE][p] + s[CUSTOM_SCORE][p]);
	case KP_SCORE_UNITS: return kp_u32v(s[UNIT_SCORE][p]);
	case KP_SCORE_BUILDINGS: return kp_u32v(s[BUILDING_SCORE][p]);
	case KP_SCORE_UNITS_BUILDINGS: return kp_u32v(s[UNIT_SCORE][p] + s[BUILDING_SCORE][p]);
	case KP_SCORE_KILLS: return kp_u32v(s[KILL_SCORE][p]);
	case KP_SCORE_RAZINGS: return kp_u32v(s[RAZING_SCORE][p]);
	case KP_SCORE_KILLS_RAZINGS: return kp_u32v(s[KILL_SCORE][p] + s[RAZING_SCORE][p]);
	case KP_SCORE_CUSTOM: return kp_u32v(s[CUSTOM_SCORE][p]);
	default: return 0;
	}
}

/* GetDeaths 0x4b99e0 and GetKills 0x4b9d30 of one slot: a unit type's counter, or the Men, Buildings and Factories
 * aggregates (Any Units = Men + Buildings). */
static int64_t unit_counter(KpGame *g, int64_t p, int64_t unit, int kills)
{
	int64_t (*s)[KP_PLAYERS] = g->pstat;
	int men = kills ? MEN_KILLED : MEN_LOST, buildings = kills ? BUILDINGS_RAZED : BUILDINGS_LOST;
	int factories = kills ? FACTORIES_RAZED : FACTORIES_LOST;
	p = read_slot(g, p);
	switch (unit) {
	case ANY_UNIT: return kp_u32v(s[men][p] + s[buildings][p]);
	case MEN: return kp_u32v(s[men][p]);
	case BUILDINGS: return kp_u32v(s[buildings][p]);
	case FACTORIES: return kp_u32v(s[factories][p]);
	default:
		if (unit < 0 || unit >= KP_UNIT_TYPES) kp_game_fail(g, "triggers: the counter of unit type %lld", (long long)unit);
		return kp_u32v((kills ? g->count_kills : g->count_deaths)[unit * KP_PLAYERS + p]);
	}
}

/* The wrapping sum of a counter over a selector's players. */
static int64_t counter_sum(KpGame *g, int64_t selector, int64_t type, int64_t (*get)(KpGame *, int64_t, int64_t))
{
	int64_t players[KP_PLAYERS], sum = 0;
	int count = kp_trigger_players(g, selector, players), k;
	for (k = 0; k < count; k++) sum = kp_u32v(sum + get(g, players[k], type));
	return sum;
}

static int64_t deaths(KpGame *g, int64_t p, int64_t unit) { return unit_counter(g, p, unit, 0); }
static int64_t kills(KpGame *g, int64_t p, int64_t unit) { return unit_counter(g, p, unit, 1); }

int64_t kp_trigger_resources(KpGame *g, int64_t selector, int64_t type) { return counter_sum(g, selector, type, resources); }
int64_t kp_trigger_score(KpGame *g, int64_t selector, int64_t type) { return counter_sum(g, selector, type, score); }
int64_t kp_trigger_deaths(KpGame *g, int64_t selector, int64_t unit) { return counter_sum(g, selector, unit, deaths); }
int64_t kp_trigger_kills(KpGame *g, int64_t selector, int64_t unit) { return counter_sum(g, selector, unit, kills); }

/* A modifier applied to a u32: set, add (wraps), subtract (stops at 0). */
static void modify(int64_t *v, int64_t modifier, int64_t amount)
{
	int64_t old = kp_u32v(*v);
	amount = kp_u32v(amount);
	if (modifier == KP_MOD_SET_TO) *v = amount;
	else if (modifier == KP_MOD_ADD) *v = kp_u32v(old + amount);
	else if (modifier == KP_MOD_SUBTRACT) *v = old > amount ? old - amount : 0;
}

/* Set Resources 0x4b82d0, Add 0x4b6be0, Subtract 0x4b8810: set and add change the current and the cumulative
 * amounts, subtract the current one; slots of 8 or more are left alone. */
void kp_trigger_set_resources(KpGame *g, int64_t selector, int64_t type, int64_t modifier, int64_t amount)
{
	KpPlayers *pl = &g->players;
	int64_t players[KP_PLAYERS];
	int count = kp_trigger_players(g, selector, players), k;
	if (modifier != KP_MOD_SET_TO && modifier != KP_MOD_ADD && modifier != KP_MOD_SUBTRACT) return;
	for (k = 0; k < count; k++) {
		int64_t p = players[k];
		if (p < 0 || p >= 8) continue;
		if (type == KP_RES_MINERALS || type == KP_RES_BOTH) {
			modify(&pl->minerals[p], modifier, amount);
			if (modifier != KP_MOD_SUBTRACT) modify(&pl->cumulative_minerals[p], modifier, amount);
		}
		if (type == KP_RES_GAS || type == KP_RES_BOTH) {
			modify(&pl->gas[p], modifier, amount);
			if (modifier != KP_MOD_SUBTRACT) modify(&pl->cumulative_gas[p], modifier, amount);
		}
	}
}

/* Set Score 0x4b8440, Add 0x4b6cf0, Subtract 0x4b8950: a pair gets (amount + 1) >> 1 on each component, the total
 * (amount + 3) >> 2 on each of units, buildings, kills and razings (custom stays). */
void kp_trigger_set_score(KpGame *g, int64_t selector, int64_t type, int64_t modifier, int64_t amount)
{
	int64_t players[KP_PLAYERS];
	int count = kp_trigger_players(g, selector, players), k;
	if (modifier != KP_MOD_SET_TO && modifier != KP_MOD_ADD && modifier != KP_MOD_SUBTRACT) return;
	for (k = 0; k < count; k++) {
		int64_t p = players[k], (*s)[KP_PLAYERS] = g->pstat, part;
		if (p < 0 || p >= 8) continue;
		switch (type) {
		case KP_SCORE_TOTAL:
			part = kp_shrv(kp_u32v(amount + 3), 2);
			modify(&s[UNIT_SCORE][p], modifier, part);
			modify(&s[BUILDING_SCORE][p], modifier, part);
			modify(&s[KILL_SCORE][p], modifier, part);
			modify(&s[RAZING_SCORE][p], modifier, part);
			break;
		case KP_SCORE_UNITS: modify(&s[UNIT_SCORE][p], modifier, amount); break;
		case KP_SCORE_BUILDINGS: modify(&s[BUILDING_SCORE][p], modifier, amount); break;
		case KP_SCORE_UNITS_BUILDINGS:
			part = kp_shrv(kp_u32v(amount + 1), 1);
			modify(&s[UNIT_SCORE][p], modifier, part);
			modify(&s[BUILDING_SCORE][p], modifier, part);
			break;
		case KP_SCORE_KILLS: modify(&s[KILL_SCORE][p], modifier, amount); break;
		case KP_SCORE_RAZINGS: modify(&s[RAZING_SCORE][p], modifier, amount); break;
		case KP_SCORE_KILLS_RAZINGS:
			part = kp_shrv(kp_u32v(amount + 1), 1);
			modify(&s[KILL_SCORE][p], modifier, part);
			modify(&s[RAZING_SCORE][p], modifier, part);
			break;
		case KP_SCORE_CUSTOM: modify(&s[CUSTOM_SCORE][p], modifier, amount); break;
		default: break;
		}
	}
}

/* Set Deaths 0x4b7e80, Add 0x4b6a90, Subtract 0x4b8640: a unit type's death counter or one aggregate (Any Units and
 * type 228 do nothing). */
void kp_trigger_set_deaths(KpGame *g, int64_t selector, int64_t unit, int64_t modifier, int64_t amount)
{
	int64_t players[KP_PLAYERS];
	int count = kp_trigger_players(g, selector, players), k;
	if (modifier != KP_MOD_SET_TO && modifier != KP_MOD_ADD && modifier != KP_MOD_SUBTRACT) return;
	for (k = 0; k < count; k++) {
		int64_t p = players[k];
		if (p < 0 || p >= 8) continue;
		if (unit == MEN) modify(&g->pstat[MEN_LOST][p], modifier, amount);
		else if (unit == BUILDINGS) modify(&g->pstat[BUILDINGS_LOST][p], modifier, amount);
		else if (unit == FACTORIES) modify(&g->pstat[FACTORIES_LOST][p], modifier, amount);
		else if (unit >= 0 && unit < KP_UNIT_TYPES) modify(&g->count_deaths[unit * KP_PLAYERS + p], modifier, amount);
		else if (unit != KP_UNIT_TYPES && unit != ANY_UNIT) {
			kp_game_fail(g, "triggers: Set Deaths of unit type %lld", (long long)unit);
		}
	}
}

/* --- the locations --- */

/* The location record of a 0-based index. */
static const int64_t *location(KpGame *g, int64_t index)
{
	if (index < 0 || index >= KP_LOCATIONS) kp_game_fail(g, "triggers: location %lld", (long long)index + 1);
	return g->trig.locations[index];
}

/* A location's center: (left + right) / 2 and (top + bottom) / 2 in signed 32-bit arithmetic. */
void kp_location_center(KpGame *g, int64_t index, int64_t *x, int64_t *y)
{
	const int64_t *l = location(g, index);
	*x = kp_s32v(kp_s32v(l[KP_LOC_LEFT] + l[KP_LOC_RIGHT]) / 2);
	*y = kp_s32v(kp_s32v(l[KP_LOC_TOP] + l[KP_LOC_BOTTOM]) / 2);
}

/* The units overlapping a location's rectangle (its corners narrowed to WORDs; 0x443110 over 0x4224c0) and the
 * location's elevation flags. */
int kp_location_units(KpGame *g, int64_t index, int64_t *units, int64_t *flags)
{
	const int64_t *l = location(g, index);
	*flags = l[KP_LOC_ELEVATION];
	return kp_spatial_query(g, kp_s16v(l[KP_LOC_LEFT]), kp_s16v(l[KP_LOC_TOP]), kp_s16v(l[KP_LOC_RIGHT]),
		kp_s16v(l[KP_LOC_BOTTOM]), units, KP_UNITS);
}

/* IsExcludedByLocationElevation 0x4b95f0: the location's flag for the unit's terrain height, bits 1/2/4 on the
 * ground and 8/0x10/0x20 in the air. */
int kp_location_excludes(KpGame *g, int64_t n, int64_t flags)
{
	int32_t s = kp_unit_sprite(g, n);
	int64_t h = kp_terrain_height(g, kp_s16v(g->w->spr.x[s]), kp_s16v(g->w->spr.y[s]));
	int air = kp_band(g->u.status[n], STATUS_AIR) != 0;
	if (h < 0 || h > 2) return 0;
	return kp_band(flags, ((int64_t)1 << h) << (air ? 3 : 0)) != 0;
}

/* A type of a condition or an action: Any Unit, Men, Buildings, Factories (units.dat group bits) or exactly one. */
int kp_trigger_type_matches(KpGame *g, int64_t t, int64_t want)
{
	int64_t group = KP_DAT(g, units, staredit_group_flags, t);
	switch (want) {
	case ANY_UNIT: return 1;
	case MEN: return kp_band(group, GROUP_MEN) != 0;
	case BUILDINGS: return kp_band(group, GROUP_BUILDING) != 0;
	case FACTORIES: return kp_band(group, GROUP_FACTORY) != 0;
	default: return t == want;
	}
}

/* TriggerUnitMatchCallback 0x4b71f0: a unit of the owners (kp_trigger_owners) and type, or one that carries one (a worker's powerup, a
 * transport's cargo, tested without the elevation check). */
int kp_trigger_unit_matches(KpGame *g, int64_t n, int64_t owners, int64_t type, int64_t flags, int check)
{
	KpUnits *u = &g->u;
	if (check && kp_location_excludes(g, n, flags)) return 0;
	if (kp_band(KP_DAT(g, units, special_ability_flags, u->type[n]), DAT_WORKER) != 0 && u->union_0xc0[n] != 0
		&& kp_trigger_unit_matches(g, kp_game_unit(g, u->union_0xc0[n]), owners, type, flags, 0)) {
		return 1;
	}
	if (!kp_trigger_owned_by(owners, u->owner[n])) return 0;
	if (kp_is_transport(g, n)) {
		int64_t cargo[KP_CARGO_SLOTS];
		int count = kp_cargo_units(g, n, cargo), k;
		for (k = 0; k < count; k++) {
			if (kp_trigger_unit_matches(g, cargo[k], owners, type, flags, 0)) return 1;
		}
	}
	return kp_trigger_type_matches(g, u->type[n], type);
}

/* FindTriggerUnitAtLocation 0x4b7170: the first unit at the location (0-based) the callback accepts, or 0. */
int64_t kp_trigger_unit_at(KpGame *g, int64_t index, int64_t selector, int64_t type)
{
	int64_t units[KP_UNITS], flags, owners = kp_trigger_owners(g, selector);
	int count = kp_location_units(g, index, units, &flags), k;
	for (k = 0; k < count; k++) {
		if (kp_trigger_unit_matches(g, units[k], owners, type, flags, 1)) return units[k];
	}
	return 0;
}

/* --- the unit-count cache of Command and Bring --- */

/* InvalidateUnitCountCache 0x4b9820 */
void kp_trigger_invalidate_counts(KpGame *g) { g->trig.count_key = COUNTS_INVALID; }

/* PrepareGlobalUnitCounts 0x4b9c00: a copy of the global counters. */
static void prepare_global(KpGame *g)
{
	KpTriggers *t = &g->trig;
	if (t->count_key == COUNTS_GLOBAL) return;
	t->count_key = COUNTS_GLOBAL;
	memcpy(t->count_pstat, g->pstat, sizeof t->count_pstat);
	memcpy(t->count_completed, g->count_completed, sizeof t->count_completed);
	memcpy(t->count_all, g->count_all, sizeof t->count_all);
}

/* CountUnitAtLocation 0x4b9970: the all-unit counters (a Zerg egg counts as one of the men). */
static void count_all_at(KpGame *g, int64_t n)
{
	KpTriggers *t = &g->trig;
	int64_t ty = g->u.type[n], p = g->u.owner[n], group = KP_DAT(g, units, staredit_group_flags, ty);
	t->count_all[ty * KP_PLAYERS + p]++;
	if (kp_band(group, GROUP_FACTORY) != 0) t->count_pstat[FACTORIES_LIVE][p]++;
	if (kp_band(group, GROUP_MEN) != 0 || (kp_band(group, GROUP_BUILDING) == 0
		&& (ty == ZERG_EGG || ty == ZERG_COCOON || ty == LURKER_EGG))) {
		t->count_pstat[MEN_LIVE][p]++;
	} else if (kp_band(group, GROUP_BUILDING) != 0) {
		t->count_pstat[BUILDINGS_LIVE][p]++;
	}
}

/* CountCompletedUnitAtLocation 0x4b9830: the completed counters with what the unit holds (its cargo, the hangar
 * children it keeps inside, a worker's powerup, a Silo's missile). */
static void count_completed_at(KpGame *g, int64_t n)
{
	KpTriggers *t = &g->trig;
	KpUnits *u = &g->u;
	int64_t ty = u->type[n], p = u->owner[n], group = KP_DAT(g, units, staredit_group_flags, ty), child;
	t->count_completed[ty * KP_PLAYERS + p]++;
	if (kp_is_transport(g, n)) {
		int64_t cargo[KP_CARGO_SLOTS];
		int count = kp_cargo_units(g, n, cargo), k;
		for (k = 0; k < count; k++) count_completed_at(g, cargo[k]);
	}
	child = kp_hangar_child_type(ty);
	if (child != 0) t->count_completed[child * KP_PLAYERS + p] += kp_band(u->union_0xc8[n], 0xff);
	if (kp_band(KP_DAT(g, units, special_ability_flags, ty), DAT_WORKER) != 0 && u->union_0xc0[n] != 0) {
		int64_t carried = kp_game_unit(g, u->union_0xc0[n]);
		if (u->type[carried] == FLAG_UNIT) count_completed_at(g, carried);
		else t->count_completed[u->type[carried] * KP_PLAYERS + p]++;
	}
	if (ty == NUCLEAR_SILO && u->union_0xd4[n] != 0) t->count_completed[NUCLEAR_MISSILE * KP_PLAYERS + p]++;
	if (kp_band(group, GROUP_FACTORY) != 0) t->count_pstat[FACTORIES_COMPLETED][p]++;
	if (kp_band(group, GROUP_MEN) != 0) t->count_pstat[MEN_COMPLETED][p]++;
	else if (kp_band(group, GROUP_BUILDING) != 0) t->count_pstat[BUILDINGS_COMPLETED][p]++;
}

/* PrepareLocationUnitCounts 0x4ba350: the counts of the units at a location (0-based), kept until invalidated or
 * another location's are made. */
static void prepare_location(KpGame *g, int64_t index)
{
	KpTriggers *t = &g->trig;
	int64_t units[KP_UNITS], flags;
	int count, k;
	if (t->count_key == COUNTS_LOCATION && t->count_location == index) return;
	t->count_key = COUNTS_LOCATION;
	t->count_location = index;
	memset(t->count_pstat, 0, sizeof t->count_pstat);
	memset(t->count_completed, 0, sizeof t->count_completed);
	memset(t->count_all, 0, sizeof t->count_all);
	count = kp_location_units(g, index, units, &flags);
	/* the callback 0x4ba830 */
	for (k = 0; k < count; k++) {
		int64_t n = units[k];
		if (kp_location_excludes(g, n, flags) || kp_band(g->u.status[n], STATUS_HALLUCINATION) != 0
			|| kp_band(KP_DAT(g, units, special_ability_flags, g->u.type[n]), DAT_SUBUNIT) != 0) {
			continue;
		}
		count_all_at(g, n);
		if (kp_band(g->u.status[n], STATUS_COMPLETED) != 0) count_completed_at(g, n);
	}
}

/* GetUnitCount 0x4ba680 of one slot from the cache: completed or all units of a type or group. */
int64_t kp_trigger_cached_count(KpGame *g, int64_t p, int64_t type, int completed)
{
	KpTriggers *t = &g->trig;
	int men = completed ? MEN_COMPLETED : MEN_LIVE, buildings = completed ? BUILDINGS_COMPLETED : BUILDINGS_LIVE;
	int factories = completed ? FACTORIES_COMPLETED : FACTORIES_LIVE;
	p = read_slot(g, p);
	switch (type) {
	case ANY_UNIT: return kp_u32v(t->count_pstat[men][p] + t->count_pstat[buildings][p]);
	case MEN: return kp_u32v(t->count_pstat[men][p]);
	case BUILDINGS: return kp_u32v(t->count_pstat[buildings][p]);
	case FACTORIES: return kp_u32v(t->count_pstat[factories][p]);
	default:
		if (type < 0 || type >= KP_UNIT_TYPES) kp_game_fail(g, "triggers: the count of unit type %lld", (long long)type);
		return kp_u32v((completed ? t->count_completed : t->count_all)[type * KP_PLAYERS + p]);
	}
}

static int64_t count_of(KpGame *g, int64_t selector, int64_t type, int completed)
{
	int64_t players[KP_PLAYERS], sum = 0;
	int count = kp_trigger_players(g, selector, players), k;
	for (k = 0; k < count; k++) sum = kp_u32v(sum + kp_trigger_cached_count(g, players[k], type, completed));
	return sum;
}

/* The cache of the global counters (location < 0) or of a 0-based location. */
void kp_trigger_prepare_counts(KpGame *g, int64_t index)
{
	if (index < 0) prepare_global(g);
	else prepare_location(g, index);
}

/* The unit count of Command (location < 0, the global counters) or Bring (a 0-based location). */
int64_t kp_trigger_unit_count(KpGame *g, int64_t index, int64_t selector, int64_t type, int completed)
{
	kp_trigger_prepare_counts(g, index);
	return count_of(g, selector, type, completed);
}

/* Commands the Most/Least (At) 0x4b9070, 0x4b90e0, 0x4b93b0, 0x4b9420: the current player's completed count
 * against the extremum over the eligible players (unsigned, ties accepted; the initial values 0 and 0xffffffff). */
int kp_trigger_unit_ranking(KpGame *g, int64_t index, int64_t type, int most)
{
	int64_t players[KP_PLAYERS], best = most ? 0 : LEAST_START, mine = best;
	int count, k;
	kp_trigger_prepare_counts(g, index);
	count = kp_trigger_players(g, KP_SEL_ALL, players);
	for (k = 0; k < count && players[k] < 8; k++) {
		int64_t v = kp_trigger_cached_count(g, players[k], type, 1);
		if (players[k] == g->trig.player) mine = v;
		if (most ? v > best : v < best) best = v;
	}
	return most ? mine >= best : mine <= best;
}

/* The scalar rankings 0x4b9160 (kills), 0x4b91c0 (score), 0x4b9220 (resources) and their least forms. */
static int scalar_ranking(KpGame *g, int64_t type, int64_t (*get)(KpGame *, int64_t, int64_t), int most)
{
	int64_t players[KP_PLAYERS], best = most ? 0 : LEAST_START, mine = best;
	int count = kp_trigger_players(g, KP_SEL_ALL, players), k;
	for (k = 0; k < count && players[k] < 8; k++) {
		int64_t v = get(g, players[k], type);
		if (players[k] == g->trig.player) mine = v;
		if (most ? v > best : v < best) best = v;
	}
	return most ? mine >= best : mine <= best;
}

int kp_trigger_kills_ranking(KpGame *g, int64_t unit, int most) { return scalar_ranking(g, unit, kills, most); }
int kp_trigger_score_ranking(KpGame *g, int64_t type, int most) { return scalar_ranking(g, type, score, most); }
int kp_trigger_resources_ranking(KpGame *g, int64_t type, int most)
{
	return scalar_ranking(g, type, resources, most);
}
