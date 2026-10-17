/* The DAT requirement evaluator (kp_game.h). */
#include "kp_game_internal.h"

#include <string.h>

#define END 0xffff
#define OR 0xff01
#define TECHS 44
#define ORDERS 0xbd
#define CHEAT_TECH 0x1000
#define ERR_NONE 0
#define ERR_OWNER 1
#define ERR_UNAVAILABLE 2
#define ERR_INCOMPLETE 0x14
#define ERR_DISABLED 10
#define ERR_RANGE_TECH 0x11
#define ERR_HALLUCINATION 22
#define ERR_NO_RECIPE 23
#define ERR_UNREQUIRED 7
#define ERR_MISSING 8
#define ERR_STATE 5
#define ERR_GROUND_ONLY 6
#define ERR_PROPERTY 19
#define ERR_PRODUCER 25
#define ERR_EXPANSION 26
#define ERR_DISALLOWED 21
#define BURROWING 11
#define LURKER 0x67
#define NO_WEAPON 0x82
#define CARRIER 0x48
#define GANTRITHOR 0x52
#define REAVER 0x53
#define WARBRINGER 0x51
#define UPGRADE_IDS 61
#define NO_TYPE 228
#define LAST_UNIT_TYPE 105 /* a queue word above it is not a unit (CanTrainUnit's full-queue test) */
#define ERR_RANGE_UNIT 16
#define ERR_QUEUE 3
#define ERR_ADDON 4
#define ERR_TECH 9
#define CHEAT_ADDON 0x2000
#define INTERCEPTOR 73
#define SCARAB 85
#define SCV 7
#define ORDER_CONSTRUCTING 0x21

static const char *const DOMAINS[KP_REQ_DOMAINS] = { "orders", "research", "tech_use", "upgrades", "units" };
static const char *const TABLES[KP_REQ_DOMAINS] = { "orders", "techdata", "techdata", "upgrades", "units" };
static const char *const COLUMNS[KP_REQ_DOMAINS] = { "requirements", "research_requirements", "use_requirements",
	"requirements", "requirements" };
static const int64_t IDS[KP_REQ_DOMAINS] = { 189, 44, 44, 61, 228 };

static int64_t word(KpGame *g, int domain, int64_t k) { return kp_dat_requirement(g, DOMAINS[domain], k); }

/* The lazy offset initializers (0x407d50, 0x407f20, 0x4080d0, 0x408280). */
static int64_t *requirement_offsets(KpGame *g, int domain)
{
	int64_t *offsets = g->req_offsets[domain], id;
	if (g->req_offsets_ready[domain]) return offsets;
	for (id = 0; id < IDS[domain]; id++) offsets[id] = kp_dat_read_named(g, TABLES[domain], COLUMNS[domain], id);
	g->req_offsets_ready[domain] = 1;
	return offsets;
}

static int64_t *ensure_offsets(KpGame *g, int domain, int64_t id)
{
	int64_t *offsets = requirement_offsets(g, domain), k = 1, i;
	if (id < 0 || id >= IDS[domain]) kp_game_fail(g, "requirements: id %lld of %s", (long long)id, DOMAINS[domain]);
	if (offsets[id] != END) return offsets;
	while (word(g, domain, k) != END) {
		int64_t record = word(g, domain, k);
		if ((domain != KP_REQ_UPGRADES || record < UPGRADE_IDS) && record >= 0 && record < IDS[domain]) offsets[record] = k + 1;
		k++;
		while (word(g, domain, k) != END) k++;
		k++;
	}
	for (i = 0; i < IDS[domain]; i++) {
		if (offsets[i] == END) offsets[i] = 0;
	}
	return offsets;
}

static int64_t fail(KpGame *g, int64_t code, int64_t result)
{
	kp_scalar_set(g, KP_REQUIREMENT_ERROR, code);
	return result;
}

int kp_tech_available(KpGame *g, int64_t player, int64_t tech)
{
	if (player < 0 || player >= KP_PLAYERS) kp_game_fail(g, "requirements: no player %lld", (long long)player);
	return tech >= 0 && tech < KP_TECH_IDS && g->players.tech_available[player][tech] != 0;
}

int kp_tech_researched(KpGame *g, int64_t player, int64_t tech)
{
	if (player < 0 || player >= KP_PLAYERS) kp_game_fail(g, "requirements: no player %lld", (long long)player);
	return tech >= 0 && tech < KP_TECH_IDS && g->players.tech_researched[player][tech] != 0;
}

int64_t kp_right_click_action(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t t = u->type[n], action;
	if (t == LURKER && kp_band(u->status[n], 0x10) != 0) return 3;
	action = KP_DAT(g, units, right_click_action, t);
	if (action == 0 && kp_band(u->status[n], 2) != 0 && kp_rally_producer(t)) action = 2;
	return action;
}

int kp_unit_can_move(KpGame *g, int64_t n)
{
	int64_t action;
	if (kp_band(g->u.status[n], 2) != 0) return 0;
	action = kp_right_click_action(g, n);
	return action != 0 && action != 3;
}

static int weaponless(KpGame *g, int64_t n)
{
	int64_t t = g->u.type[n];
	return (t == LURKER && kp_band(g->u.status[n], 0x10) == 0) || KP_DAT(g, units, ground_weapon, t) == NO_WEAPON;
}

int kp_unit_can_attack(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t t = u->type[n], hangar, sub;
	if (!weaponless(g, n) || KP_DAT(g, units, air_weapon, t) != NO_WEAPON) return 1;
	hangar = u->union_0xc8[n];
	if ((t == CARRIER || t == GANTRITHOR) && kp_mod(kp_band(hangar, 0xff) + kp_band(kp_shrv(hangar, 8), 0xff), 256) != 0) return 1;
	if ((t == REAVER || t == WARBRINGER) && kp_band(hangar, 0xff) != 0) return 1;
	sub = u->subunit[n];
	if (sub == 0) return 0;
	if (weaponless(g, sub)) return KP_DAT(g, units, air_weapon, u->type[sub]) != NO_WEAPON;
	return 1;
}

static int64_t count_at(KpGame *g, int64_t *table, int64_t i)
{
	if (i < 0 || i >= KP_UNIT_TYPES * KP_PLAYERS) kp_game_fail(g, "requirements: count %lld", (long long)i);
	return table[i];
}

static int64_t property_bit(int64_t w)
{
	switch (w) {
	case 0xff16: return 0x8;
	case 0xff17: return 0x20;
	case 0xff19: return 0x800;
	}
	return 0x10; /* 0xff1a */
}

int64_t kp_evaluate_requirements(KpGame *g, int64_t context, int64_t player, int64_t n, int domain, int64_t at)
{
	KpUnits *u = &g->u;
	int64_t t = u->type[n], flags = KP_DAT(g, units, special_ability_flags, t);
	int any_missing = 1, mismatch = 0, hallucination_ok = 0;
	int64_t k = at, w = word(g, domain, k);
	for (;;) {
		int nothing = 1;
		int64_t count = 0;
		if (w == END) {
			if (kp_band(u->status[n], 0x40000000) != 0 && !hallucination_ok) return fail(g, ERR_HALLUCINATION, 0);
			return 1;
		}
		for (;;) {
			if (w == 0xff02) {
				int64_t want;
				k++;
				want = word(g, domain, k);
				if (count_at(g, g->count_completed, want * 12 + player) != 0) nothing = 0;
				if (want == t) {
					count++;
					mismatch = 0;
					any_missing = 0;
				} else if (any_missing) {
					mismatch = 1;
				}
			} else if (w == 0xff03) {
				int64_t i;
				k++;
				i = word(g, domain, k) * 12 + player;
				count += count_at(g, g->count_completed, i) + count_at(g, g->count_all, i);
			} else if (w == 0xff05) {
				if (kp_band(u->status[n], 2) == 0) return fail(g, ERR_UNREQUIRED, 0);
				count++;
			} else if (w == 0xff06) {
				if (kp_band(u->status[n], 2) != 0) return fail(g, ERR_GROUND_ONLY, 0);
				count++;
			} else if (w == 0xff09) {
				if (kp_mod(u->union_0xc8[n], 256) != 44) return fail(g, ERR_STATE, 0);
				count++;
			} else if (w == 0xff0a) {
				if (kp_mod(kp_shrv(u->union_0xc8[n], 8), 256) != 61) return fail(g, ERR_STATE, 0);
				count++;
			} else if (w == 0xff0d) {
				if (u->union_0xd0[n] != 0) return fail(g, ERR_MISSING, 0);
				count++;
			} else if (w == 0xff0f) {
				if (context == BURROWING && t == LURKER) count++;
				else if (kp_tech_researched(g, player, context) || kp_band(kp_scalar(g, KP_CHEATS), CHEAT_TECH) != 0) count++;
			} else if (w == 0xff11) {
				if (kp_band(u->status[n], 0x10) != 0 && u->ai[n] == 0) return fail(g, ERR_STATE, 0);
				count++;
			} else if (w == 0xff12) {
				if (kp_band(u->status[n], 2) != 0) return fail(g, ERR_PROPERTY, 0);
				count++;
			} else if (w == 0xff13) {
				if (kp_band(u->status[n], 2) == 0) return fail(g, ERR_PROPERTY, 0);
				count++;
			} else if (w == 0xff16 || w == 0xff17 || w == 0xff19 || w == 0xff1a) {
				if (kp_band(flags, property_bit(w)) == 0) return fail(g, ERR_PROPERTY, 0);
				count++;
			} else if (w == 0xff18) {
				if (kp_is_transport(g, n)) count++;
			} else if (w == 0xff1b) {
				if (kp_mod(u->union_0xc0[n], 256) == 0) return fail(g, ERR_MISSING, 0);
				count++;
			} else if (w == 0xff1c) {
				if (kp_band(flags, 0x40) != 0) count++;
			} else if (w == 0xff1e) {
				hallucination_ok = 1;
				count++;
			} else if (w == 0xff1f) {
				int64_t level = kp_upgrade_level(g, player, context);
				int64_t marker = level == 1 ? 0xff20 : (level == 2 ? 0xff21 : 0xff1f);
				if (word(g, domain, k) != marker) {
					do k++; while (word(g, domain, k) != marker);
				}
				count++;
			} else if (w == 0xff22) {
				return fail(g, ERR_DISALLOWED, -1);
			} else if (w == 0xff23) {
				return fail(g, ERR_NO_RECIPE, 0);
			} else if (w == 0xff24) {
				if (g->expansion == 0) return fail(g, ERR_EXPANSION, 0);
				count++;
			} else if (w == 0xff26) {
				if (kp_band(u->status[n], 0x10) == 0) return fail(g, ERR_STATE, 0);
				count++;
			} else if (w == 0xff1d) {
				if (kp_right_click_action(g, n) == 0) return fail(g, ERR_MISSING, 0);
				count++;
			} else if (w == 0xff14) {
				if (!kp_unit_can_move(g, n)) return fail(g, ERR_PROPERTY, 0);
				count++;
			} else if (w == 0xff15) {
				if (!kp_unit_can_attack(g, n)) return fail(g, ERR_PROPERTY, 0);
				count++;
			} else if (w == 0xff07 || w == 0xff08) {
				kp_game_fail(g, "requirements: token 0x%llx (0x407570)", (long long)w);
			} else {
				count += count_at(g, g->count_completed, w * 12 + player);
			}
			if (word(g, domain, k + 1) != OR) break;
			k += 2;
			w = word(g, domain, k);
		}
		if (mismatch) return fail(g, ERR_PRODUCER, 0);
		if (count == 0 && nothing) return fail(g, ERR_MISSING, -1);
		k++;
		w = word(g, domain, k);
	}
}

int64_t kp_required_parent_type(KpGame *g, int64_t t)
{
	int64_t *offsets = ensure_offsets(g, KP_REQ_UNITS, t), k = offsets[t];
	if (k != 0 && word(g, KP_REQ_UNITS, k) == 0xff02) return word(g, KP_REQ_UNITS, k + 1);
	return 228;
}

int kp_unit_disabled(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	return kp_band(u->status[n], 0x400) != 0 || u->lockdown_timer[n] != 0 || u->stasis_timer[n] != 0
		|| u->maelstrom_timer[n] != 0;
}

static int ghost(int64_t t) { return t == 1 || t == 0x10 || t == 100 || t == 99 || t == 0x68; }

int64_t kp_can_issue_order(KpGame *g, int64_t order, int64_t player, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t *offsets, t, current;
	kp_scalar_set(g, KP_REQUIREMENT_ERROR, ERR_NONE);
	if (order >= ORDERS) return fail(g, 0xf, 0);
	offsets = ensure_offsets(g, KP_REQ_ORDERS, order);
	if (player != u->owner[n]) return fail(g, ERR_OWNER, 0);
	if (order != 0x27 && order != 0x28 && kp_band(u->status[n], 1) == 0) return fail(g, ERR_INCOMPLETE, 0);
	if (kp_unit_disabled(g, n)) return fail(g, ERR_DISABLED, 0);
	t = u->type[n];
	current = u->order[n];
	if (kp_band(u->status[n], 0x10) != 0 && t != LURKER && order != 0x76) return fail(g, 0xb, 0);
	if ((t == 7 && current == 0x21 && order != 1) || (ghost(t) && current == 0x81)
		|| ((t == 0x44 || t == 0x3f) && current == 0x6a)) {
		return fail(g, ERR_MISSING, 0);
	}
	if (offsets[order] == 0) return fail(g, ERR_NO_RECIPE, 0);
	return kp_evaluate_requirements(g, order, player, n, KP_REQ_ORDERS, offsets[order]);
}

int64_t kp_can_use_tech(KpGame *g, int64_t tech, int64_t player, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t *offsets;
	kp_scalar_set(g, KP_REQUIREMENT_ERROR, ERR_NONE);
	if (tech >= TECHS) return fail(g, ERR_RANGE_TECH, 0);
	offsets = ensure_offsets(g, KP_REQ_TECH_USE, tech);
	if (player != u->owner[n]) return fail(g, ERR_OWNER, 0);
	if (kp_band(u->status[n], 1) == 0) return fail(g, ERR_INCOMPLETE, 0);
	if (kp_unit_disabled(g, n)) return fail(g, ERR_DISABLED, 0);
	if (kp_band(u->status[n], 0x40000000) != 0) return fail(g, ERR_HALLUCINATION, 0);
	if (!kp_tech_available(g, player, tech) && kp_band(kp_scalar(g, KP_CHEATS), CHEAT_TECH) == 0) return fail(g, ERR_UNAVAILABLE, 0);
	if (offsets[tech] == 0) return fail(g, ERR_NO_RECIPE, 0);
	return kp_evaluate_requirements(g, tech, player, n, KP_REQ_TECH_USE, offsets[tech]);
}

/* The fighters a Carrier or Reaver has and has queued (byte arithmetic, 255 for another type; 0x41b380, 0x41b3d0,
 * QueuedFighters 0x41cc50 counting both fighter types in all five slots). */
static int64_t fighters(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t t = u->type[n], have, queued = 0, k, q;
	if (t == CARRIER || t == GANTRITHOR) have = kp_band(u->union_0xc8[n], 0xff) + kp_band(kp_shrv(u->union_0xc8[n], 8), 0xff);
	else if (t == REAVER || t == WARBRINGER) have = kp_band(u->union_0xc8[n], 0xff);
	else return 0xff;
	for (k = 0; k < 5; k++) {
		q = kp_band(*kp_queue_entry(g, n, k), 0xffff);
		if (q == INTERCEPTOR || q == SCARAB) queued++;
	}
	return kp_band(have + queued, 0xff);
}

/* IsUnitBusy 0x407b10: something queued, an add-on or a Protoss building under construction. */
static int busy(KpGame *g, int64_t n)
{
	if (kp_band(*kp_queue_entry(g, n, kp_mod(g->u.queue_slot[n], 5)), 0xffff) != NO_TYPE) return 1;
	return kp_constructing_addon(g, n) || kp_building_unit(g, n);
}

/* CanTrainUnit 0x4b32a0, the unit-production gate: 1, or 0 / -1 with the error (0x6be654). Its recipe interpreter is
 * not the general one (0x407570): the tokens it knows and their failures are its own, an unknown token counts as a
 * unit type. */
int64_t kp_can_train_unit(KpGame *g, int64_t t, int64_t player, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t *offsets, k, w, count, c = kp_scalar(g, KP_CHEATS);
	int nothing, mismatch = 0, first = 1;
	if (c == KP_NIL) c = 0;
	kp_scalar_set(g, KP_REQUIREMENT_ERROR, ERR_NONE);
	if (kp_band(t, 0xffff) >= NO_TYPE) return fail(g, ERR_RANGE_UNIT, 0);
	if (player != u->owner[n]) return fail(g, ERR_OWNER, 0);
	if (kp_band(u->status[n], 1) == 0) return fail(g, ERR_INCOMPLETE, 0);
	if (kp_unit_disabled(g, n)) return fail(g, ERR_DISABLED, 0);
	if (g->players.unit_available[player][t] == 0) return fail(g, ERR_UNAVAILABLE, 0);
	if (kp_band(*kp_queue_entry(g, n, kp_mod(u->queue_slot[n] + 4, 5)), 0xffff) <= LAST_UNIT_TYPE) {
		return fail(g, ERR_QUEUE, 0);
	}
	if (kp_band(u->status[n], 0x40000000) != 0) return fail(g, ERR_HALLUCINATION, 0);
	offsets = ensure_offsets(g, KP_REQ_UNITS, t);
	k = offsets[t];
	if (k == 0) return fail(g, ERR_NO_RECIPE, 0);
	w = word(g, KP_REQ_UNITS, k);
	while (w != END) {
		nothing = 1;
		count = 0;
		for (;;) {
			int64_t id;
			switch (w) {
			case 0xff02:
				id = word(g, KP_REQ_UNITS, ++k);
				if (count_at(g, g->count_completed, id * 12 + player) != 0) nothing = 0;
				if (id == u->type[n]) {
					mismatch = 0;
					count++;
					first = 0;
				} else if (first) {
					mismatch = 1;
				}
				break;
			case 0xff03:
				id = word(g, KP_REQ_UNITS, ++k) * 12 + player;
				count = kp_u32v(count + count_at(g, g->count_completed, id) + count_at(g, g->count_all, id));
				break;
			case 0xff04:
				if (kp_band(c, CHEAT_ADDON) != 0) {
					k++;
				} else if (u->union_0xc0[n] == 0 || u->type[u->union_0xc0[n]] != word(g, KP_REQ_UNITS, ++k)) {
					if (!mismatch) kp_scalar_set(g, KP_REQUIREMENT_ERROR, ERR_ADDON);
					return -1;
				}
				count++;
				break;
			case 0xff05:
				if (kp_band(u->status[n], 2) == 0) return fail(g, ERR_UNREQUIRED, 0);
				count++;
				break;
			case 0xff07:
				if (busy(g, n) || kp_mod(u->union_0xc8[n], 256) != 44 || kp_mod(kp_shrv(u->union_0xc8[n], 8), 256) != 61) {
					return fail(g, ERR_STATE, 0);
				}
				count++;
				break;
			case 0xff08:
				if (kp_constructing_addon(g, n) || kp_building_unit(g, n)) return fail(g, ERR_STATE, 0);
				count++;
				break;
			case 0xff09:
				if (kp_mod(u->union_0xc8[n], 256) != 44) return fail(g, ERR_STATE, 0);
				count++;
				break;
			case 0xff0a:
				if (kp_mod(kp_shrv(u->union_0xc8[n], 8), 256) != 61) return fail(g, ERR_STATE, 0);
				count++;
				break;
			case 0xff0b:
				if (u->type[n] == SCV && u->order[n] == ORDER_CONSTRUCTING) return fail(g, ERR_STATE, 0);
				count++;
				break;
			case 0xff0c:
				if (u->union_0xc0[n] != 0) {
					if (!mismatch) kp_scalar_set(g, KP_REQUIREMENT_ERROR, ERR_ADDON);
					return 0;
				}
				count++;
				break;
			case 0xff0e:
				if (kp_hangar_capacity(g, n) <= fighters(g, n)) return fail(g, ERR_QUEUE, 0);
				count++;
				break;
			case 0xff10:
				if (u->union_0xd0[n] != 0) return fail(g, ERR_QUEUE, 0);
				count++;
				break;
			case 0xff11:
				if (kp_band(u->status[n], 0x10) != 0) return fail(g, ERR_STATE, 0);
				count++;
				break;
			case 0xff22:
				return fail(g, ERR_DISALLOWED, -1);
			case 0xff23:
				return fail(g, ERR_NO_RECIPE, 0);
			case 0xff24:
				if (g->expansion == 0) return fail(g, ERR_EXPANSION, 0);
				count++;
				break;
			case 0xff25:
				id = word(g, KP_REQ_UNITS, ++k);
				if (!kp_tech_researched(g, player, id) && kp_band(c, CHEAT_TECH) == 0) return fail(g, ERR_TECH, -1);
				count++;
				break;
			case 0xff26:
				if (kp_band(u->status[n], 0x10) == 0) return fail(g, ERR_STATE, -1);
				count++;
				break;
			default:
				if (kp_band(c, CHEAT_ADDON) != 0) count++;
				count = kp_u32v(count + count_at(g, g->count_completed, w * 12 + player));
				break;
			}
			if (word(g, KP_REQ_UNITS, k + 1) != OR) break;
			k += 2;
			w = word(g, KP_REQ_UNITS, k);
		}
		if (count == 0 && nothing) return fail(g, ERR_MISSING, -1);
		k++;
		w = word(g, KP_REQ_UNITS, k);
	}
	if (mismatch) return fail(g, ERR_PRODUCER, -1);
	return 1;
}
