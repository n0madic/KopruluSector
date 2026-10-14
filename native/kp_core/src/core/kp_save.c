/* Saves of a game at a step boundary, version 1 (kp_game.h; plan D17, ADR 0008 "What changes in formats"): an
 * explicit little-endian encoding of every mutable field of the game and of its images world, in sections, with
 * an FNV-1a fingerprint. The static data that the start of a mission rebuilds (DAT, regions, contours, tileset,
 * vision templates, GRP metadata, IScript, the hooks) and the derived state (the remembered-terrain set, the
 * event ring, scratch lists) stay out; a load restores the fields into a game of the same mission and data, fresh
 * from its start or at any later step, after checking the whole save, so a refused save leaves the game as it was.
 * One walk over the state serves both directions, so the two orders cannot drift apart. */
#include "kp_game_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAGIC "KPSV"
#define FNV_OFFSET 0xcbf29ce484222325ULL
#define FNV_PRIME 0x100000001b3ULL

typedef struct {
	int reading;
	int dry; /* reading: check the save without writing into the game */
	uint8_t *buf; /* writing: growable; reading: the save */
	size_t size, cap, at;
	int failed; /* writing: out of memory; reading: past the end or a wrong section */
	char error[KP_ERROR_SIZE];
} Codec;

static void put_bytes(Codec *c, const void *p, size_t n)
{
	if (c->failed) return;
	if (c->size + n > c->cap) {
		size_t cap = c->cap ? c->cap : 65536;
		uint8_t *b;
		while (c->size + n > cap) cap *= 2;
		b = (uint8_t *)realloc(c->buf, cap);
		if (!b) {
			c->failed = 1;
			snprintf(c->error, sizeof c->error, "save: out of memory");
			return;
		}
		c->buf = b;
		c->cap = cap;
	}
	memcpy(c->buf + c->size, p, n);
	c->size += n;
}

static const uint8_t *take(Codec *c, size_t n)
{
	const uint8_t *p;
	if (c->failed) return NULL;
	if (c->at + n > c->size) {
		c->failed = 1;
		snprintf(c->error, sizeof c->error, "save: truncated");
		return NULL;
	}
	p = c->buf + c->at;
	c->at += n;
	return p;
}

static void u64(Codec *c, uint64_t *v)
{
	uint8_t b[8];
	int k;
	if (c->reading) {
		const uint8_t *p = take(c, 8);
		uint64_t x = 0;
		if (!p) return;
		for (k = 7; k >= 0; k--) x = x << 8 | p[k];
		if (!c->dry) *v = x;
	} else {
		for (k = 0; k < 8; k++) b[k] = (uint8_t)(*v >> (8 * k));
		put_bytes(c, b, 8);
	}
}

static void u32(Codec *c, uint32_t *v)
{
	uint8_t b[4];
	int k;
	if (c->reading) {
		const uint8_t *p = take(c, 4);
		if (!p) return;
		if (!c->dry) *v = (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
	} else {
		for (k = 0; k < 4; k++) b[k] = (uint8_t)(*v >> (8 * k));
		put_bytes(c, b, 4);
	}
}

static void i64s(Codec *c, int64_t *v, size_t n)
{
	size_t k;
	for (k = 0; k < n; k++) {
		uint64_t x = (uint64_t)v[k];
		u64(c, &x);
		v[k] = (int64_t)x;
	}
}

static void i32s(Codec *c, int32_t *v, size_t n)
{
	size_t k;
	for (k = 0; k < n; k++) {
		uint32_t x = (uint32_t)v[k];
		u32(c, &x);
		v[k] = (int32_t)x;
	}
}

static void u32s(Codec *c, uint32_t *v, size_t n)
{
	size_t k;
	for (k = 0; k < n; k++) u32(c, &v[k]);
}

static void u8s(Codec *c, uint8_t *v, size_t n)
{
	if (c->reading) {
		const uint8_t *p = take(c, n);
		if (p && !c->dry) memcpy(v, p, n);
	} else {
		put_bytes(c, v, n);
	}
}

static void ints(Codec *c, int *v, size_t n)
{
	size_t k;
	for (k = 0; k < n; k++) {
		int32_t x = (int32_t)v[k];
		i32s(c, &x, 1);
		v[k] = x;
	}
}

static void lists(Codec *c, KpList *l, size_t n)
{
	size_t k;
	for (k = 0; k < n; k++) {
		i32s(c, &l[k].head, 1);
		i32s(c, &l[k].tail, 1);
	}
}

static void str(Codec *c, char *s, size_t cap)
{
	uint32_t n = (uint32_t)strlen(s);
	u32(c, &n);
	if (c->reading) {
		const uint8_t *p = take(c, n);
		if (!p) return;
		if (n >= cap) {
			c->failed = 1;
			snprintf(c->error, sizeof c->error, "save: a name of %u bytes", n);
			return;
		}
		memcpy(s, p, n);
		s[n] = '\0';
	} else {
		put_bytes(c, s, n);
	}
}

/* A section tag: written, or checked when reading. */
static void section(Codec *c, const char *tag)
{
	if (c->reading) {
		const uint8_t *p = take(c, 4);
		if (p && memcmp(p, tag, 4) != 0) {
			c->failed = 1;
			snprintf(c->error, sizeof c->error, "save: section %s expected", tag);
		}
	} else {
		put_bytes(c, tag, 4);
	}
}

/* A value the walk checks: read even in a dry run (v is the caller's local). */
static void checked_u64(Codec *c, uint64_t *v)
{
	int dry = c->dry;
	c->dry = 0;
	u64(c, v);
	c->dry = dry;
}

static void checked_u32(Codec *c, uint32_t *v)
{
	int dry = c->dry;
	c->dry = 0;
	u32(c, v);
	c->dry = dry;
}

#define ARRAY_N(a) (sizeof(a) / sizeof((a)[0]))
#define I64S(field) i64s(c, (field), ARRAY_N(field))
#define I64(field) i64s(c, &(field), 1)

static void walk_pathing(Codec *c, KpGame *g)
{
	KpPathing *p = &g->pathing;
	KpLocalGeometry *lg = &p->lg;
	KpVisited *v = &p->visited;
	int64_t count;
	size_t k;
	u8s(c, p->path_bytes, sizeof p->path_bytes);
	I64S(p->extent_ctx);
	for (k = 0; k < ARRAY_N(p->rnodes); k++) {
		KpRegionNode *r = &p->rnodes[k];
		I64(r->region);
		I64(r->parent);
		I64(r->heur);
		I64(r->cost);
		I64(r->prio);
		I64(r->closed);
	}
	I64S(p->rheap);
	I64(p->rheap_count);
	ints(c, lg->active_local, ARRAY_N(lg->active_local));
	for (k = 0; k < 4; k++) {
		KpEdgeArray *e = &lg->loc[k];
		I64(e->count);
		I64S(e->axis);
		I64S(e->start);
		I64S(e->stop);
		I64S(e->dir);
		I64S(e->info);
	}
	I64S(lg->ext);
	I64S(lg->edge);
	for (k = 0; k < ARRAY_N(lg->nb); k++) {
		I64(lg->nb[k].x);
		I64(lg->nb[k].y);
		I64(lg->nb[k].flags);
		I64(lg->nb[k].goal);
	}
	I64(lg->nb_count);
	for (k = 0; k < ARRAY_N(lg->rects); k++) I64S(lg->rects[k]);
	I64(lg->rect_count);
	I64(lg->x);
	I64(lg->y);
	I64(lg->top);
	I64(lg->right);
	I64(lg->bottom);
	I64(lg->left);
	I64(lg->unit);
	I64(lg->goal_x);
	I64(lg->goal_y);
	I64(lg->target);
	I64S(lg->box);
	I64(lg->goal_found);
	I64(lg->mask);
	I64(v->count);
	I64S(v->xs);
	I64S(v->colmap);
	for (k = 0; k < ARRAY_N(v->lists); k++) {
		I64(v->lists[k].count);
		I64S(v->lists[k].y0);
		I64S(v->lists[k].y1);
	}
	I64(v->xs_hw);
	I64(v->colmap_hw);
	I64(v->lists_hw);
	for (k = 0; k < ARRAY_N(p->lnodes); k++) {
		KpLocalNode *n = &p->lnodes[k];
		I64(n->x);
		I64(n->y);
		I64(n->parent);
		I64(n->region);
		I64(n->flags);
		I64(n->depth);
		I64(n->in_next);
		I64(n->goal);
		I64(n->near);
		I64(n->closed);
		I64(n->heur);
		I64(n->cost);
		I64(n->prio);
	}
	I64(p->lnode_count);
	I64S(p->lheap);
	I64(p->lheap_count);
	I64S(p->hash_x);
	I64S(p->hash_y);
	I64S(p->hash_node);
	/* The run-time slots of the region graph (the graph itself is static). */
	{
		uint64_t n = (uint64_t)p->rg.count;
		checked_u64(c, &n);
		count = (int64_t)n;
	}
	if (c->reading && !c->failed && count != p->rg.count) {
		c->failed = 1;
		snprintf(c->error, sizeof c->error, "save: %lld regions, the mission has %lld", (long long)count, (long long)p->rg.count);
		return;
	}
	if (count > 0 && p->rg.node) {
		i64s(c, p->rg.node, (size_t)count);
		i64s(c, p->rg.mark, (size_t)count);
	}
}

static void walk_world(Codec *c, KpImages *w)
{
	i32s(c, &w->unit, 1);
	i32s(c, &w->flingy, 1);
	i32s(c, &w->bullet, 1);
	ints(c, &w->has_unit, 1);
	ints(c, &w->has_flingy, 1);
	ints(c, &w->has_bullet, 1);
#define IMG(name) i32s(c, w->img.name, KP_IMAGES);
	KP_IMAGE_FIELDS(IMG)
#undef IMG
#define SPR(name) i32s(c, w->spr.name, KP_SPRITES);
	KP_SPRITE_FIELDS(SPR)
#undef SPR
	lists(c, w->spr.images, KP_SPRITES);
#define THG(name) i32s(c, w->thg.name, KP_LONE);
	KP_THINGY_FIELDS(THG)
#undef THG
#define FOG(name) i32s(c, w->fog.name, KP_FOG);
	KP_THINGY_FIELDS(FOG)
#undef FOG
	i32s(c, w->vgrp_width, KP_IMAGES);
	i32s(c, w->vgrp_height, KP_IMAGES);
	i32s(c, w->vgrp_fw, KP_IMAGES);
	i32s(c, w->vgrp_fh, KP_IMAGES);
	u8s(c, w->has_vgrp, KP_IMAGES);
	lists(c, &w->free_images, 1);
	lists(c, &w->free_health, 1);
	lists(c, &w->free_circles, 1);
	lists(c, &w->free_ghosts, 1);
	lists(c, &w->free_boxes, 1);
	lists(c, &w->free_sprites, 1);
	lists(c, w->rows, KP_ROWS);
	lists(c, &w->free_lone, 1);
	lists(c, &w->active_lone, 1);
	lists(c, &w->free_fog, 1);
	lists(c, &w->active_fog, 1);
	i32s(c, &w->cursor, 1);
	i32s(c, w->circle_colors, 256);
	ints(c, &w->has_circle_colors, 1);
}

static void walk(Codec *c, KpGame *g)
{
	KpTerrain *t = &g->terrain;
	KpPlayers *pl = &g->players;
	size_t k;
	section(c, "SCAL");
	I64S(g->scalar);
	u8s(c, g->scalar_present, sizeof g->scalar_present);
	section(c, "UNIT");
#define UNIT(name) I64S(g->u.name);
	KP_UNIT_FIELDS(UNIT)
#undef UNIT
	lists(c, g->lists, KP_LIST_COUNT);
	I64S(g->player_units);
	section(c, "BULL");
#define BULLET(name) I64S(g->bullets.name);
	KP_BULLET_FIELDS(BULLET)
#undef BULLET
	lists(c, &g->bullets.active, 1);
	lists(c, &g->bullets.free, 1);
	I64(g->bullets.allocated);
	I64(g->bullets.last_spawner);
	I64(g->bullets.spin_sign);
	section(c, "LDAT");
	{
		/* The DAT values the game writes while it runs: the forward offsets of attkshiftproj (0x411023). */
		int64_t offsets[KP_WEAPON_IDS];
		for (k = 0; k < KP_WEAPON_IDS; k++) offsets[k] = kp_dat_read(g, KP_COL_weapons_forward_offset, (int64_t)k);
		I64S(offsets);
		if (c->reading && !c->dry) {
			for (k = 0; k < KP_WEAPON_IDS; k++) kp_dat_write(g, KP_COL_weapons_forward_offset, (int64_t)k, offsets[k]);
		}
	}
	section(c, "CNTS");
	I64S(g->count_all);
	I64S(g->count_completed);
	I64S(g->count_kills);
	I64S(g->count_deaths);
	for (k = 0; k < KP_PSTAT_ARRAYS; k++) I64S(g->pstat[k]);
	for (k = 0; k < 2; k++) I64S(g->strength[k]);
	section(c, "ORDR");
	I64S(g->orders.prev);
	I64S(g->orders.next);
	I64S(g->orders.order);
	I64S(g->orders.type);
	I64S(g->orders.x);
	I64S(g->orders.y);
	I64S(g->orders.unit);
	lists(c, &g->orders.free, 1);
	I64(g->orders.allocated);
	section(c, "SELN");
	I64S(g->local_selection);
	for (k = 0; k < KP_SELECTION_PLAYERS; k++) {
		size_t j;
		I64S(g->selections[k]);
		for (j = 0; j < KP_GROUPS; j++) I64S(g->groups[k][j]);
		I64S(g->recent_stamps[k]);
	}
	I64S(g->circle_colors);
	section(c, "REQS");
	for (k = 0; k < KP_REQ_DOMAINS; k++) I64S(g->req_offsets[k]);
	u8s(c, g->req_offsets_ready, sizeof g->req_offsets_ready);
	section(c, "AI  ");
	I64(g->ai.terrain_changed);
	I64(g->ai.period);
	I64(g->ai.cycle);
	I64(g->ai.rounds);
	I64(g->ai.region_player);
	I64(g->ai.region_cursor);
	I64(g->ai.spending_player);
	I64(g->ai.town_player);
	I64(g->ai.scripts);
	section(c, "AIRG");
	{
		/* The AI region records within the map's region count. */
		size_t count = (size_t)(g->regions.count < KP_AI_REGIONS ? g->regions.count : KP_AI_REGIONS);
		for (k = 0; k < KP_AI_PLAYERS; k++) {
#define AI_REGION(name) i64s(c, g->air.name[k], count);
			KP_AI_REGION_FIELDS(AI_REGION)
#undef AI_REGION
		}
	}
	for (k = 0; k < KP_AI_PLAYERS; k++) u8s(c, g->ai_reach[k], KP_AI_REGIONS);
	I64S(g->ai_reach_time);
	I64S(g->ai_reach_source);
#define PLAYER_AI(name) I64S(g->pai.name);
	KP_PLAYER_AI_FIELDS(PLAYER_AI)
#undef PLAYER_AI
	section(c, "SPAT");
	I64S(g->spatial.x_unit);
	I64S(g->spatial.x_at);
	I64S(g->spatial.y_unit);
	I64S(g->spatial.y_at);
	I64(g->spatial.count);
	I64(g->spatial.max_w);
	I64(g->spatial.max_h);
	I64S(g->spatial.mark);
	I64S(g->spatial.seen);
	I64(g->spatial.stamp);
	section(c, "MOVE");
	I64(g->mv.next_x);
	I64(g->mv.next_y);
	I64(g->mv.old_x);
	I64(g->mv.old_y);
	I64(g->mv.next_halt_x);
	I64(g->mv.next_halt_y);
	I64(g->mv.step);
	I64(g->mv.walk_start);
	I64(g->mv.walk_stop);
	I64(g->mv.flags_before);
	I64(g->mv.flags_after);
	section(c, "PATH");
	walk_pathing(c, g);
	section(c, "COLL");
	I64S(g->collision.density);
	section(c, "TERR");
	I64S(t->tiles);
	I64S(t->megatiles);
	I64S(t->raw_tiles);
	I64S(t->creep_backup);
	I64S(t->creep_border);
	I64(t->recede_timer);
	I64(t->recede_free);
	I64(t->recede_retry);
	I64S(t->recede_buckets);
	section(c, "PLAY");
	I64S(pl->controller);
	I64S(pl->race);
	I64S(pl->force);
	for (k = 0; k < KP_PLAYERS; k++) {
		I64S(pl->alliance[k]);
		I64S(pl->upgrades[k]);
		I64S(pl->tech_available[k]);
		I64S(pl->tech_researched[k]);
	}
	I64S(pl->shared_vision);
	section(c, "GUAR");
	I64S(g->guards.next);
	I64S(g->guards.prev);
	I64S(g->guards.kind);
	I64S(g->guards.unit);
	I64S(g->guards.type);
	I64S(g->guards.x1);
	I64S(g->guards.y1);
	I64S(g->guards.x2);
	I64S(g->guards.y2);
	I64S(g->guards.deaths);
	I64S(g->guards.heads);
	I64(g->guards.free);
	section(c, "TRIG");
	I64(g->trigger_clock.second);
	I64(g->trigger_clock.pass);
	I64(g->trigger_clock.result);
	I64(g->trigger_clock.leaderboard);
	I64(g->trigger_clock.game_timer);
	I64(g->trigger_clock.countdowns);
	I64(g->trigger_clock.leaderboard_kind);
	I64S(g->results);
	section(c, "RNGS");
	u32(c, &g->rng->initial_seed);
	u32(c, &g->rng->state);
	u32(c, &g->rng->enabled);
	u32(c, &g->rng->total);
	u32s(c, g->rng->counters, KP_RNG_SOURCES);
	{
		uint32_t has_crt = g->crt != NULL, holdrand = g->crt ? g->crt->holdrand : 0;
		checked_u32(c, &has_crt);
		checked_u32(c, &holdrand);
		if (c->reading && !c->failed) {
			if ((g->crt != NULL) != (has_crt != 0)) {
				c->failed = 1;
				snprintf(c->error, sizeof c->error, "save: the CRT generator differs");
				return;
			}
			if (g->crt && !c->dry) g->crt->holdrand = holdrand;
		}
	}
	section(c, "WRLD");
	walk_world(c, g->w);
	section(c, "END ");
}

static uint64_t fnv(const uint8_t *p, size_t n)
{
	uint64_t h = FNV_OFFSET;
	size_t k;
	for (k = 0; k < n; k++) {
		h ^= p[k];
		h *= FNV_PRIME;
	}
	return h;
}

static void header(Codec *c, uint32_t *version, char *mission, char *data)
{
	if (c->reading) {
		const uint8_t *p = take(c, 4);
		if (p && memcmp(p, MAGIC, 4) != 0) {
			c->failed = 1;
			snprintf(c->error, sizeof c->error, "save: not a save");
		}
	} else {
		put_bytes(c, MAGIC, 4);
	}
	u32(c, version);
	str(c, mission, KP_SAVE_NAME);
	str(c, data, KP_SAVE_NAME);
}

int kp_save(KpGame *g, const char *mission, const char *data, uint8_t **out, size_t *size, KpError *err)
{
	Codec c;
	char m[KP_SAVE_NAME], d[KP_SAVE_NAME];
	uint32_t version = KP_SAVE_VERSION;
	uint64_t h;
	memset(&c, 0, sizeof c);
	if (strlen(mission) >= sizeof m || strlen(data) >= sizeof d) {
		snprintf(err->msg, sizeof err->msg, "save: a mission or data name of %d bytes or more", KP_SAVE_NAME);
		return -1;
	}
	snprintf(m, sizeof m, "%s", mission);
	snprintf(d, sizeof d, "%s", data);
	header(&c, &version, m, d);
	walk(&c, g);
	h = fnv(c.buf, c.size);
	u64(&c, &h);
	if (c.failed) {
		free(c.buf);
		snprintf(err->msg, sizeof err->msg, "%s", c.error);
		return -1;
	}
	*out = c.buf;
	*size = c.size;
	return 0;
}

int kp_load(KpGame *g, const uint8_t *bytes, size_t size, const char *mission, const char *data, KpError *err)
{
	Codec c;
	char m[KP_SAVE_NAME] = "", d[KP_SAVE_NAME] = "";
	uint32_t version = 0;
	uint64_t want, h;
	memset(&c, 0, sizeof c);
	c.reading = 1;
	c.buf = (uint8_t *)bytes;
	c.size = size;
	header(&c, &version, m, d);
	if (!c.failed && version != KP_SAVE_VERSION) {
		snprintf(err->msg, sizeof err->msg, "save: version %u, expected %d", version, KP_SAVE_VERSION);
		return -1;
	}
	if (!c.failed && strcmp(m, mission) != 0) {
		snprintf(err->msg, sizeof err->msg, "save: mission %s, expected %s", m, mission);
		return -1;
	}
	if (!c.failed && strcmp(d, data) != 0) {
		snprintf(err->msg, sizeof err->msg, "save: made with other game data");
		return -1;
	}
	if (c.failed || size < 8) {
		snprintf(err->msg, sizeof err->msg, "%s", c.failed ? c.error : "save: truncated");
		return -1;
	}
	{
		Codec f;
		memset(&f, 0, sizeof f);
		f.reading = 1;
		f.buf = (uint8_t *)bytes;
		f.size = size;
		f.at = size - 8;
		u64(&f, &want);
		h = fnv(bytes, size - 8);
		if (h != want) {
			snprintf(err->msg, sizeof err->msg, "save: damaged (fingerprint)");
			return -1;
		}
	}
	/* A dry walk checks every section, the region count, the generators and the length first, so a refused save
	 * leaves the game as it was; the second walk writes. */
	c.size = size - 8;
	{
		size_t start = c.at;
		int pass;
		for (pass = 0; pass < 2; pass++) {
			c.dry = pass == 0;
			c.at = start;
			walk(&c, g);
			if (!c.failed && c.at != c.size) {
				c.failed = 1;
				snprintf(c.error, sizeof c.error, "save: %u bytes past the state", (unsigned)(c.size - c.at));
			}
			if (c.failed) {
				snprintf(err->msg, sizeof err->msg, "%s", c.error);
				return -1;
			}
		}
	}
	/* Derived state: the remembered-terrain set is rebuilt at its next refresh; the game's pending events are
	 * dropped. */
	g->terrain.has_terrain_changed = 0;
	g->events.first = g->events.count = g->events.texts_waiting = 0;
	return 0;
}
