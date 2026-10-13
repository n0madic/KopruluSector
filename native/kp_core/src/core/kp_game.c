/* The game (kp_game.h): creation, failures, the Lua calls of the hybrid. */
#include "kp_game_internal.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

KpGame *kp_game_new(KpImages *w, KpRngGame *rng)
{
	KpGame *g = (KpGame *)calloc(1, sizeof *g);
	if (!g) return NULL;
	g->w = w;
	g->rng = rng;
	return g;
}

static void free_regions(KpGame *g)
{
	int d;
	free((void *)g->regions.data.data);
	memset(&g->regions, 0, sizeof g->regions);
	for (d = 0; d < 4; d++) {
		free(g->contours[d].axis);
		memset(&g->contours[d], 0, sizeof g->contours[d]);
	}
}

void kp_game_free(KpGame *g)
{
	if (!g) return;
	kp_pathing_free(g);
	kp_terrain_free(g);
	free_regions(g);
	free(g);
}

int kp_game_set_regions(KpGame *g, const uint8_t *data, size_t size, const KpMissionRegions *fields)
{
	uint8_t *copy = (uint8_t *)malloc(size ? size : 1);
	if (!copy) return -1;
	memcpy(copy, data, size);
	free((void *)g->regions.data.data);
	g->regions = *fields;
	g->regions.data.data = copy;
	g->regions.data.size = (uint32_t)size;
	return 0;
}

int kp_game_set_contour_side(KpGame *g, int d, int32_t count)
{
	KpContourSide *c = &g->contours[d];
	int64_t *b = (int64_t *)calloc((size_t)(count > 0 ? count : 1) * 5, sizeof *b);
	if (!b) return -1;
	free(c->axis);
	c->count = count;
	c->axis = b;
	c->start = b + count;
	c->stop = b + count * 2;
	c->dir = b + count * 3;
	c->info = b + count * 4;
	return 0;
}

void kp_game_fail(KpGame *g, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(g->w->error, sizeof g->w->error, fmt, ap);
	va_end(ap);
	kp_images_rethrow(g->w);
}

int64_t kp_lua(KpGame *g, const char *method, int argc, int64_t a, int64_t b, int64_t c, int64_t d)
{
	int64_t args[4], res[1];
	args[0] = a;
	args[1] = b;
	args[2] = c;
	args[3] = d;
	res[0] = KP_NIL;
	g->lua(g, method, argc, args, 1, res);
	return res[0];
}

int64_t kp_game_unit(KpGame *g, int64_t n)
{
	if (n < 1 || n > KP_UNITS) kp_game_fail(g, "unit: record %lld out of range", (long long)n);
	return n;
}

int64_t kp_idiv64(KpGame *g, int64_t a, int64_t b)
{
	if (b == 0) kp_game_fail(g, "integer division by zero");
	if (a == -2147483648LL && b == -1) kp_game_fail(g, "integer overflow in division");
	return a / b;
}

int64_t kp_udiv64(KpGame *g, int64_t a, int64_t b)
{
	if (b == 0 || (uint32_t)b == 0) kp_game_fail(g, "integer division by zero");
	return (int64_t)((uint32_t)a / (uint32_t)b);
}

int64_t kp_urem64(KpGame *g, int64_t a, int64_t b)
{
	if (b == 0 || (uint32_t)b == 0) kp_game_fail(g, "integer division by zero");
	return (int64_t)((uint32_t)a % (uint32_t)b);
}

void kp_list64_unlink(int64_t *prev, int64_t *next, KpList *list, int64_t node, int64_t none)
{
	if (list->head == node) list->head = (int32_t)next[node];
	if (list->tail == node) list->tail = (int32_t)prev[node];
	if (prev[node] != none) next[prev[node]] = next[node];
	if (next[node] != none) prev[next[node]] = prev[node];
	next[node] = none;
	prev[node] = none;
}

void kp_list64_insert_after(int64_t *prev, int64_t *next, KpList *list, int64_t existing, int64_t node, int64_t none)
{
	if (list->tail == existing) list->tail = (int32_t)node;
	prev[node] = existing;
	next[node] = next[existing];
	if (next[existing] != none) prev[next[existing]] = node;
	next[existing] = node;
}

void kp_list64_insert_after_head(int64_t *prev, int64_t *next, KpList *list, int64_t node, int64_t none)
{
	if (list->head == none) {
		list->head = (int32_t)node;
		list->tail = (int32_t)node;
		return;
	}
	kp_list64_insert_after(prev, next, list, list->head, node, none);
}

void kp_list64_insert_before(int64_t *prev, int64_t *next, KpList *list, int64_t existing, int64_t node, int64_t none)
{
	if (list->head == existing) list->head = (int32_t)node;
	prev[node] = prev[existing];
	next[node] = existing;
	if (prev[existing] != none) next[prev[existing]] = node;
	prev[existing] = node;
}

const KpScalarInfo KP_SCALARS[KP_SCALAR_COUNT] = {
	{ "units_allocated", 0 }, { "error_code", 0 }, { "cloaked_head", 0 }, { "tile_countdown", 0 },
	{ "update_tiles", 1 }, { "move_reveal", 1 }, { "order_spread", 0 }, { "secondary_spread", 0 },
	{ "interceptor_alerted", 1 }, { "hit_slot", 0 }, { "frame", 0 }, { "turn", 0 }, { "elapsed", 0 },
	{ "has_triggers", 1 }, { "command_player", 0 }, { "second_order", 0 }, { "order_marker", 1 },
	{ "bullets_active", 1 }, { "power_dirty", 1 }, { "placement_active", 1 }, { "cheats", 0 },
	{ "requirement_error", 0 }, { "resource_areas", 1 }, { "path_free_head", 0 }, { "path_subject", 0 },
	{ "path_all_units", 0 },
};

int64_t kp_scalar(const KpGame *g, KpScalar s) { return g->scalar_present[s] ? g->scalar[s] : KP_NIL; }

void kp_scalar_set(KpGame *g, KpScalar s, int64_t v)
{
	g->scalar[s] = v;
	g->scalar_present[s] = 1;
}

void kp_scalar_clear(KpGame *g, KpScalar s)
{
	g->scalar[s] = 0;
	g->scalar_present[s] = 0;
}

/* GetUpgradeLevel 0x49a470 (upgrades[player][id] or 0). */
int64_t kp_upgrade_level(KpGame *g, int64_t player, int64_t id)
{
	int64_t v = KP_GET(g, "upgrades", player, NULL, id);
	return v == KP_NIL ? 0 : v;
}

/* IsMorphingIntoAdvancedZergBuilding 0x43c040. */
int kp_is_morphing_zerg_building(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t q;
	if (kp_band(u->status[n], 1) != 0) return 0;
	switch (u->queue_slot[n]) {
	case 0: q = u->queue0[n]; break;
	case 1: q = u->queue1[n]; break;
	case 2: q = u->queue2[n]; break;
	case 3: q = u->queue3[n]; break;
	case 4: q = u->queue4[n]; break;
	default: kp_game_fail(g, "game: queue slot %lld", (long long)u->queue_slot[n]);
	}
	return q == 0x85 || q == 0x84 || q == 0x89 || q == 0x90 || q == 0x92;
}

/* ApplyCreationSpeedUpgrades 0x43a8e0. */
static int64_t speed_upgrade(int64_t t)
{
	switch (t) {
	case 2: case 19: return 0x11;
	case 38: return 0x1d;
	case 39: return 0x35;
	case 42: return 0x1a;
	case 65: return 0x22;
	case 69: return 0x25;
	case 70: return 0x2a;
	case 84: return 0x27;
	}
	return -1;
}

static int always_fast(int64_t t) { return t == 53 || t == 57 || t == 77 || t == 80 || t == 88 || t == 103; }

void kp_apply_creation_upgrades(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t t = u->type[n], p = u->owner[n], up = speed_upgrade(t);
	if (!(up >= 0 || always_fast(t) || t == 37 || t == 54 || t == 48)) return;
	if (up >= 0) {
		if (kp_upgrade_level(g, p, up) != 0) kp_set_speed_upgrade(g, n);
	} else if (t == 37) { /* Zergling: Metabolic Boost, then Adrenal Glands */
		if (kp_upgrade_level(g, p, 0x1b) != 0) kp_set_speed_upgrade(g, n);
		if (kp_upgrade_level(g, p, 0x1c) != 0) u->status[n] = kp_bor(u->status[n], 0x20000000);
	} else if (t == 54) { /* Devouring One */
		kp_set_speed_upgrade(g, n);
		u->status[n] = kp_bor(u->status[n], 0x20000000);
	} else if (t == 48) {
		if (g->expansion != 0) kp_set_speed_upgrade(g, n);
	} else if (always_fast(t)) {
		kp_set_speed_upgrade(g, n);
	}
	kp_recalculate_speed(g, n);
}

