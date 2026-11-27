/* The check of a loaded state (kp_load_check, kp_load): the record numbers, links and indices that the step uses
 * unchecked must lie in their arrays, and the lists it walks to their end must end. A save's hash table detects damage,
 * it does not authenticate (kp_save_seal): a save of someone's making may hold any value, and a link out of its array
 * would make the step read or write past it. The game writes only values in these domains, so a save of a game never
 * fails the check; a list is walked from its head, as the step walks it (a stale link past a list's end is the
 * original's, and stays unchecked).
 *
 * The union words (+0xc0..+0xd8) are checked where the step writes through them (a Nuclear Silo's missile, a research's
 * tech and an upgrade's id, a resource's area); their reads stay unchecked.
 * TODO: the union words the step reads unchecked (a hangar's lists, a worker's harvest target and powerup, an add-on, a
 * Nydus exit, a Pylon's field sprite, a Ghost's nuke dot, a resource's waiting list), the town requests' unit ids and
 * the bullets' sprite positions (the terrain's tiles): a value out of range there is read past its array. */
#include "kp_game_internal.h"

#include <stddef.h>
#include <stdio.h>

typedef struct {
	char *why;
	size_t size;
	int failed;
} Check;

static int refuse(Check *c, const char *what, int64_t at, int64_t v)
{
	if (!c->failed) snprintf(c->why, c->size, "save: %s %lld holds %lld", what, (long long)at, (long long)v);
	c->failed = 1;
	return 0;
}

/* v in [lo, hi] (what at its index at) */
static int in(Check *c, const char *what, int64_t at, int64_t v, int64_t lo, int64_t hi)
{
	return (v >= lo && v <= hi) || refuse(c, what, at, v);
}

/* a[from..to] each in [lo, hi] */
static void each(Check *c, const char *what, const int64_t *a, int64_t from, int64_t to, int64_t lo, int64_t hi)
{
	int64_t k;
	for (k = from; k <= to && !c->failed; k++) in(c, what, k, a[k], lo, hi);
}

static void each32(Check *c, const char *what, const int32_t *a, int64_t from, int64_t to, int64_t lo, int64_t hi)
{
	int64_t k;
	for (k = from; k <= to && !c->failed; k++) in(c, what, k, a[k], lo, hi);
}

/* each over a narrowed array (kp_game.h) of its element size, signed or not: EACH(c, what, a, sign, ...) */
static void eachn(Check *c, const char *what, const void *a, size_t elem, int sign, int64_t from, int64_t to, int64_t lo,
	int64_t hi)
{
	int64_t k;
	for (k = from; k <= to && !c->failed; k++) in(c, what, k, kp_num_get(a, elem, sign, k), lo, hi);
}
#define EACH(c, what, a, sign, from, to, lo, hi) eachn(c, what, a, KP_ELEM(a), sign, from, to, lo, hi)

/* The list from head along next to none, its records in [lo, hi]: it must end within its records' count. */
static void walk(Check *c, const char *what, int64_t head, const int64_t *next, int64_t none, int64_t lo, int64_t hi)
{
	int64_t n = head, steps = 0;
	while (n != none && !c->failed) {
		if (!in(c, what, steps, n, lo, hi)) return;
		if (++steps > hi - lo + 1) {
			refuse(c, what, steps, n); /* a cycle */
			return;
		}
		n = next[n];
	}
}

static void walk32(Check *c, const char *what, int64_t head, const int32_t *next, int64_t none, int64_t lo, int64_t hi)
{
	int64_t n = head, steps = 0;
	while (n != none && !c->failed) {
		if (!in(c, what, steps, n, lo, hi)) return;
		if (++steps > hi - lo + 1) {
			refuse(c, what, steps, n);
			return;
		}
		n = next[n];
	}
}

static void units(Check *c, KpGame *g)
{
	static const struct {
		const char *name;
		size_t offset;
	} REFS[] = {
		{ "unit prev", offsetof(KpUnits, prev) }, { "unit next", offsetof(KpUnits, next) },
		{ "unit player_prev", offsetof(KpUnits, player_prev) }, { "unit player_next", offsetof(KpUnits, player_next) },
		{ "unit subunit", offsetof(KpUnits, subunit) }, { "unit move_unit", offsetof(KpUnits, move_unit) },
		{ "unit order_unit", offsetof(KpUnits, order_unit) }, { "unit auto_target", offsetof(KpUnits, auto_target) },
		{ "unit connected", offsetof(KpUnits, connected) }, { "unit build_unit", offsetof(KpUnits, build_unit) },
		{ "unit irradiated_by", offsetof(KpUnits, irradiated_by) }, { "unit rally_unit", offsetof(KpUnits, rally_unit) },
		{ "unit burrow_prev", offsetof(KpUnits, burrow_prev) }, { "unit burrow_next", offsetof(KpUnits, burrow_next) },
	};
	const KpUnits *u = &g->u;
	size_t k;
	int64_t n;
	int l;
	for (k = 0; k < sizeof REFS / sizeof REFS[0]; k++)
		each(c, REFS[k].name, (const int64_t *)((const char *)u + REFS[k].offset), 1, KP_FLINGIES, 0, KP_UNITS);
	each(c, "unit owner", u->owner, 1, KP_FLINGIES, 0, KP_PLAYERS - 1);
	each(c, "unit sprite", u->sprite, 1, KP_FLINGIES, KP_NONE, KP_SPRITES - 1);
	each(c, "unit queue_head", u->queue_head, 1, KP_FLINGIES, KP_NONE, KP_ORDER_NODES - 1);
	each(c, "unit queue_tail", u->queue_tail, 1, KP_FLINGIES, KP_NONE, KP_ORDER_NODES - 1);
	each(c, "unit last_attacker", u->last_attacker, 1, KP_FLINGIES, 0, 0xff);
	each(c, "unit irradiate_player", u->irradiate_player, 1, KP_FLINGIES, 0, 0xff);
	each(c, "unit heading2", u->heading2, 1, KP_FLINGIES, 0, 0xff);
	each(c, "unit queue_slot", u->queue_slot, 1, KP_FLINGIES, 0, 4);
	each(c, "unit type", u->type, 1, KP_UNITS, 0, KP_UNIT_TYPES - 1);
	EACH(c, "unit bound_left", u->bound_left, 1, 1, KP_UNITS, KP_NONE, KP_SPATIAL_CAPACITY - 1);
	EACH(c, "unit bound_right", u->bound_right, 1, 1, KP_UNITS, KP_NONE, KP_SPATIAL_CAPACITY - 1);
	EACH(c, "unit bound_top", u->bound_top, 1, 1, KP_UNITS, KP_NONE, KP_SPATIAL_CAPACITY - 1);
	EACH(c, "unit bound_bottom", u->bound_bottom, 1, 1, KP_UNITS, KP_NONE, KP_SPATIAL_CAPACITY - 1);
	for (n = 1; n <= KP_FLINGIES && !c->failed; n++) {
		int64_t ai = u->ai[n], kind = kp_ai_ref_kind(ai), k2 = kp_ai_ref_index(ai);
		if (ai != 0 && !(kind >= KP_AI_GUARD && kind <= KP_AI_MILITARY && k2 >= 0 && k2 < KP_GUARDS && ai == KP_AI_REF(kind, k2)))
			refuse(c, "unit ai", n, ai);
	}
	/* the union words the step writes through */
	for (n = 1; n <= KP_UNITS && !c->failed; n++) {
		int64_t t = u->type[n];
		if (t == KP_TYPE_NUCLEAR_SILO) in(c, "a Nuclear Silo's missile", n, u->union_0xd0[n], 0, KP_UNITS);
		if (u->order[n] == KP_ORD_RESEARCH) {
			int64_t tech = kp_union_get(g, n, 0xc8, 1);
			in(c, "a research's tech", n, tech, 0, KP_TECH_PROGRESS_BYTES * 8 - 1);
		}
		if (u->order[n] == KP_ORD_UPGRADE) in(c, "an upgrade's id", n, kp_union_get(g, n, 0xc9, 1), 0, KP_UPGRADE_IDS - 1);
		if (kp_band(KP_DAT(g, units, special_ability_flags, t), 0x2000) != 0) /* a resource container */
			in(c, "a resource's area", n, kp_ai_resource_area(g, n), 0, KP_AREAS - 1);
	}
	for (l = 0; l < KP_LIST_COUNT && !c->failed; l++) {
		in(c, "unit list tail", l, g->lists[l].tail, 0, KP_UNITS);
		walk(c, "unit list", g->lists[l].head, u->next, 0, 1, KP_UNITS);
	}
	for (l = 0; l < KP_PLAYERS && !c->failed; l++) walk(c, "player unit list", g->player_units[l], u->player_next, 0, 1, KP_UNITS);
	if (g->scalar_present[KP_CLOAKED_HEAD]) walk(c, "cloaked list", g->scalar[KP_CLOAKED_HEAD], u->burrow_next, 0, 1, KP_UNITS);
	walk(c, "power list", g->power_head, u->rally_unit, 0, 1, KP_UNITS);
	each(c, "power prev", g->power.prev, 1, KP_UNITS, 0, KP_UNITS);
	each(c, "local selection", g->local_selection, 0, KP_SLOTS - 1, 0, KP_UNITS);
	each(c, "selection", &g->selections[0][0], 0, KP_SELECTION_PLAYERS * KP_SLOTS - 1, 0, KP_UNITS);
}

static void bullets_and_orders(Check *c, KpGame *g)
{
	const KpBullets *b = &g->bullets;
	const KpOrders *o = &g->orders;
	int64_t n;
	each(c, "bullet prev", b->prev, 1, KP_BULLETS, 0, KP_BULLETS);
	each(c, "bullet next", b->next, 1, KP_BULLETS, 0, KP_BULLETS);
	each(c, "bullet ring", b->ring, 1, KP_BULLETS, 0, 13);
	each(c, "bullet source", b->source, 1, KP_BULLETS, 0, KP_UNITS);
	in(c, "bullet hit_used", 0, b->hit_used, 0, KP_HIT_PAIRS);
	if (g->scalar_present[KP_HIT_SLOT]) in(c, "the hit slot", 0, g->scalar[KP_HIT_SLOT], 0, KP_HIT_SLOTS - 1);
	walk(c, "bullet list", b->active.head, b->next, 0, 1, KP_BULLETS);
	walk(c, "bullet free list", b->free.head, b->next, 0, 1, KP_BULLETS);
	each(c, "order prev", o->prev, 0, KP_ORDER_NODES - 1, KP_NONE, KP_ORDER_NODES - 1);
	each(c, "order next", o->next, 0, KP_ORDER_NODES - 1, KP_NONE, KP_ORDER_NODES - 1);
	each(c, "order unit", o->unit, 0, KP_ORDER_NODES - 1, 0, KP_UNITS);
	walk(c, "order free list", o->free.head, o->next, KP_NONE, 0, KP_ORDER_NODES - 1);
	for (n = 1; n <= KP_FLINGIES && !c->failed; n++) walk(c, "order queue", g->u.queue_head[n], o->next, KP_NONE, 0, KP_ORDER_NODES - 1);
}

/* A pool of records 0..count-1 with null link -1: its links, its free list and its heads. */
static void pool(Check *c, const char *what, const int64_t *next, const int64_t *prev, int64_t count, int64_t free_head,
	const int64_t *heads, int nheads)
{
	int k;
	each(c, what, next, 0, count - 1, KP_NONE, count - 1);
	each(c, what, prev, 0, count - 1, KP_NONE, count - 1);
	walk(c, what, free_head, next, KP_NONE, 0, count - 1);
	for (k = 0; k < nheads && !c->failed; k++) walk(c, what, heads[k], next, KP_NONE, 0, count - 1);
}

static void computers(Check *c, KpGame *g)
{
	const KpAiRegions *air = &g->air;
	const KpPlayerAi *pai = &g->pai;
	int64_t p, k, regions = KP_AI_PLAYERS * KP_AI_REGIONS - 1;
	in(c, "the AI's region player", 0, g->ai.region_player, 0, KP_AI_PLAYERS - 1);
	in(c, "the AI's region cursor", 0, g->ai.region_cursor, 0, KP_AI_REGIONS);
	in(c, "the AI's town player", 0, g->ai.town_player, 0, KP_AI_PLAYERS - 1);
	in(c, "the AI's spending player", 0, g->ai.spending_player, 0, KP_AI_PLAYERS - 1);
	in(c, "the AI's anchor count", 0, g->ai.anchor_count, 0, KP_AI_ANCHORS);
	in(c, "the AI's used areas", 0, g->areas.used, 0, KP_AREAS - 1);
	EACH(c, "AI region dest", air->dest[0], 0, 0, regions, 0, KP_AI_REGIONS - 1);
	EACH(c, "AI region military", air->military[0], 1, 0, regions, 0, KP_MILITARY_AIS);
	EACH(c, "AI region slowest", air->slowest[0], 1, 0, regions, 0, KP_UNITS);
	EACH(c, "AI region detector", air->detector[0], 1, 0, regions, 0, KP_UNITS);
	EACH(c, "AI region target_air", air->target_air[0], 1, 0, regions, 0, KP_UNITS);
	EACH(c, "AI region target_ground", air->target_ground[0], 1, 0, regions, 0, KP_UNITS);
	each(c, "AI attack target", pai->attack_target, 0, KP_AI_PLAYERS - 1, 0, KP_AI_REGIONS);
	each(c, "AI heap count", pai->heap_count, 0, KP_AI_PLAYERS - 1, 0, KP_AI_HEAP);
	for (p = 0; p < KP_AI_PLAYERS && !c->failed; p++) {
		for (k = 0; k < pai->heap_count[p] && !c->failed; k++) {
			int64_t ctx = pai->heap_context[p][k], kind = ctx >> 16, idx = (ctx & 0xffff) - 1;
			if (kind == KP_AI_CONTEXT_REGION) in(c, "AI heap region", k, idx, 0, KP_AI_REGIONS - 1);
			if (kind == KP_AI_CONTEXT_GUARD) in(c, "AI heap guard", k, idx, 0, KP_GUARDS - 1);
		}
		for (k = 0; k < g->regions.count && k < KP_AI_REGIONS && !c->failed; k++)
			walk(c, "AI region military list", air->military[p][k] - 1, g->military.next, KP_NONE, 0, KP_MILITARY_AIS - 1);
	}
	pool(c, "guard link", g->guards.next, g->guards.prev, KP_GUARDS, g->guards.free, g->guards.heads, 8);
	each(c, "guard unit", g->guards.unit, 0, KP_GUARDS - 1, 0, KP_UNITS);
	pool(c, "script link", g->scripts.next, g->scripts.prev, KP_AI_SCRIPTS, g->scripts.free, &g->scripts.head, 1);
	each(c, "script player", g->scripts.player, 0, KP_AI_SCRIPTS - 1, 0, KP_AI_PLAYERS - 1);
	each(c, "script town", g->scripts.town, 0, KP_AI_SCRIPTS - 1, KP_NONE, KP_TOWNS - 1);
	pool(c, "town link", g->towns.next, g->towns.prev, KP_TOWNS, g->towns.free, g->towns.heads, KP_AI_PLAYERS);
	each(c, "town workers", g->towns.workers, 0, KP_TOWNS - 1, KP_NONE, KP_WORKER_AIS - 1);
	each(c, "town buildings", g->towns.buildings, 0, KP_TOWNS - 1, KP_NONE, KP_BUILDING_AIS - 1);
	each(c, "town owner", g->towns.owner, 0, KP_TOWNS - 1, 0, KP_AI_PLAYERS - 1);
	each(c, "town main", g->towns.main, 0, KP_TOWNS - 1, 0, KP_UNITS);
	each(c, "town repair", g->towns.repair, 0, KP_TOWNS - 1, 0, KP_UNITS);
	each(c, "town mineral", g->towns.mineral, 0, KP_TOWNS - 1, 0, KP_UNITS);
	each(c, "town gas", &g->towns.gas[0][0], 0, KP_TOWNS * KP_TOWN_GAS - 1, 0, KP_UNITS);
	for (k = 0; k < KP_TOWNS && !c->failed; k++) {
		walk(c, "town worker list", g->towns.workers[k], g->workers.next, KP_NONE, 0, KP_WORKER_AIS - 1);
		walk(c, "town building list", g->towns.buildings[k], g->buildings.next, KP_NONE, 0, KP_BUILDING_AIS - 1);
	}
	pool(c, "worker link", g->workers.next, g->workers.prev, KP_WORKER_AIS, g->workers.free, NULL, 0);
	each(c, "worker unit", g->workers.unit, 0, KP_WORKER_AIS - 1, 0, KP_UNITS);
	each(c, "worker town", g->workers.town, 0, KP_WORKER_AIS - 1, KP_NONE, KP_TOWNS - 1);
	pool(c, "building link", g->buildings.next, g->buildings.prev, KP_BUILDING_AIS, g->buildings.free, NULL, 0);
	each(c, "building unit", g->buildings.unit, 0, KP_BUILDING_AIS - 1, 0, KP_UNITS);
	each(c, "building town", g->buildings.town, 0, KP_BUILDING_AIS - 1, KP_NONE, KP_TOWNS - 1);
	for (k = 0; k < KP_BUILDING_AIS * KP_TRAINING_TAGS && !c->failed; k++) {
		int64_t tag = (&g->buildings.tags[0][0])[k], idx = kp_ai_ref_index((&g->buildings.contexts[0][0])[k]);
		if (tag == KP_AI_CONTEXT_REGION) in(c, "building training region", k, idx, 0, KP_AI_REGIONS - 1);
		if (tag == KP_AI_CONTEXT_GUARD) in(c, "building training guard", k, idx, 0, KP_GUARDS - 1);
	}
	pool(c, "military link", g->military.next, g->military.prev, KP_MILITARY_AIS, g->military.free, NULL, 0);
	each(c, "military unit", g->military.unit, 0, KP_MILITARY_AIS - 1, 0, KP_UNITS);
	each(c, "military player", g->military.player, 0, KP_MILITARY_AIS - 1, KP_NONE, KP_AI_PLAYERS - 1);
	each(c, "military region", g->military.region, 0, KP_MILITARY_AIS - 1, KP_NONE, KP_AI_REGIONS - 1);
}

static void places(Check *c, KpGame *g)
{
	const KpSpatial *sp = &g->spatial;
	const KpTerrain *t = &g->terrain;
	const KpTriggers *tr = &g->trig;
	const KpVisited *v = &g->pathing.visited;
	int64_t k;
	in(c, "the spatial index's count", 0, sp->count, 0, KP_SPATIAL_CAPACITY);
	if (!c->failed) {
		EACH(c, "spatial x unit", sp->x_unit, 1, 0, sp->count - 1, 0, KP_UNITS);
		EACH(c, "spatial y unit", sp->y_unit, 1, 0, sp->count - 1, 0, KP_UNITS);
	}
	for (k = 0; k <= KP_RECEDE_BUCKETS && !c->failed; k++)
		walk(c, "receding creep list", t->recede_head[k], t->recede_next, 0, 1, KP_RECEDE_NODES);
	each(c, "receding creep next", t->recede_next, 0, KP_RECEDE_NODES, 0, KP_RECEDE_NODES);
	each(c, "receding creep prev", t->recede_prev, 0, KP_RECEDE_NODES, 0, KP_RECEDE_NODES);
	each(c, "receding creep bucket", t->recede_bucket, 0, KP_RECEDE_NODES, 0, KP_RECEDE_BUCKETS);
	EACH(c, "receding creep tile", t->recede_node, 1, 0, KP_TILES - 1, 0, KP_RECEDE_NODES);
	if (tr->count > 0) {
		each(c, "trigger next", tr->next, 0, tr->count - 1, KP_NONE, tr->count - 1);
		for (k = 0; k < KP_TRIGGER_PLAYERS && !c->failed; k++) walk(c, "trigger list", tr->head[k], tr->next, KP_NONE, 0, tr->count - 1);
	}
	in(c, "the current trigger", 0, tr->current, KP_NONE, tr->count - 1);
	for (k = 0; k < g->pathing.rg.count && !c->failed; k++) in(c, "region search node", k, g->pathing.rg.node[k], KP_NONE, KP_REGION_ARENA);
	in(c, "the swept columns' mark", 0, v->xs_hw, KP_NONE, KP_LOCAL_MAX_COLUMNS + 1);
	in(c, "the swept column map's mark", 0, v->colmap_hw, KP_NONE, KP_LOCAL_MAX_COLUMNS + 1);
	in(c, "the swept lists' mark", 0, v->lists_hw, KP_NONE, KP_LOCAL_MAX_COLUMNS);
	for (k = 1; k < 5 && !c->failed; k++)
		if (g->setup.leader[k] != KP_NO_LEADER) in(c, "a team's leader", k, g->setup.leader[k], 0, KP_AI_PLAYERS - 1);
}

static void world(Check *c, const KpImages *w)
{
	const KpList *const IMAGE_LISTS[] = { &w->free_images, &w->free_health, &w->free_circles, &w->free_ghosts, &w->free_boxes };
	size_t k;
	int64_t s;
	each32(c, "image prev", w->img.prev, 0, KP_IMAGES - 1, KP_NONE, KP_IMAGES - 1);
	each32(c, "image next", w->img.next, 0, KP_IMAGES - 1, KP_NONE, KP_IMAGES - 1);
	each32(c, "sprite prev", w->spr.prev, 0, KP_SPRITES - 1, KP_NONE, KP_SPRITES - 1);
	each32(c, "sprite next", w->spr.next, 0, KP_SPRITES - 1, KP_NONE, KP_SPRITES - 1);
	each32(c, "lone prev", w->thg.prev, 0, KP_LONE - 1, KP_NONE, KP_LONE - 1);
	each32(c, "lone next", w->thg.next, 0, KP_LONE - 1, KP_NONE, KP_LONE - 1);
	each32(c, "lone sprite", w->thg.sprite, 0, KP_LONE - 1, KP_NONE, KP_SPRITES - 1);
	each32(c, "fog prev", w->fog.prev, 0, KP_FOG - 1, KP_NONE, KP_FOG - 1);
	each32(c, "fog next", w->fog.next, 0, KP_FOG - 1, KP_NONE, KP_FOG - 1);
	each32(c, "fog sprite", w->fog.sprite, 0, KP_FOG - 1, KP_NONE, KP_SPRITES - 1);
	for (k = 0; k < sizeof IMAGE_LISTS / sizeof IMAGE_LISTS[0] && !c->failed; k++) {
		in(c, "image list tail", (int64_t)k, IMAGE_LISTS[k]->tail, KP_NONE, KP_IMAGES - 1);
		walk32(c, "image list", IMAGE_LISTS[k]->head, w->img.next, KP_NONE, 0, KP_IMAGES - 1);
	}
	for (s = 0; s < KP_SPRITES && !c->failed; s++) {
		in(c, "sprite image tail", s, w->spr.images[s].tail, KP_NONE, KP_IMAGES - 1);
		walk32(c, "sprite image list", w->spr.images[s].head, w->img.next, KP_NONE, 0, KP_IMAGES - 1);
	}
	walk32(c, "sprite free list", w->free_sprites.head, w->spr.next, KP_NONE, 0, KP_SPRITES - 1);
	in(c, "sprite free list tail", 0, w->free_sprites.tail, KP_NONE, KP_SPRITES - 1);
	for (s = 0; s < KP_ROWS && !c->failed; s++) {
		in(c, "sprite row tail", s, w->rows[s].tail, KP_NONE, KP_SPRITES - 1);
		walk32(c, "sprite row", w->rows[s].head, w->spr.next, KP_NONE, 0, KP_SPRITES - 1);
	}
	walk32(c, "lone free list", w->free_lone.head, w->thg.next, KP_NONE, 0, KP_LONE - 1);
	walk32(c, "lone list", w->active_lone.head, w->thg.next, KP_NONE, 0, KP_LONE - 1);
	walk32(c, "fog free list", w->free_fog.head, w->fog.next, KP_NONE, 0, KP_FOG - 1);
	walk32(c, "fog list", w->active_fog.head, w->fog.next, KP_NONE, 0, KP_FOG - 1);
	in(c, "lone free list tail", 0, w->free_lone.tail, KP_NONE, KP_LONE - 1);
	in(c, "lone list tail", 0, w->active_lone.tail, KP_NONE, KP_LONE - 1);
	in(c, "fog free list tail", 0, w->free_fog.tail, KP_NONE, KP_FOG - 1);
	in(c, "fog list tail", 0, w->active_fog.tail, KP_NONE, KP_FOG - 1);
}

int kp_load_check(KpGame *g, char *why, size_t size)
{
	Check c;
	c.why = why;
	c.size = size;
	c.failed = 0;
	units(&c, g);
	bullets_and_orders(&c, g);
	computers(&c, g);
	places(&c, g);
	world(&c, g->w);
	return c.failed ? -1 : 0;
}
