/* The walk over a game's state (kp_game.h; plan D17, ADR 0008 "What changes in formats", ADR 0021): every mutable
 * field of the game and of its images world, in sections, visited by one walk that serves every use, so they cannot
 * drift apart:
 * - saves at a step boundary, version 2: an explicit little-endian encoding of the fields with a table of the
 *   sections' hashes; a load restores them into a game of the same mission and data, fresh from its start or at any
 *   later step, after checking the whole save, so a refused save leaves the game as it was;
 * - the state hash (kp_state_hash): the same bytes' hash per section, without encoding them;
 * - the in-process snapshot (kp_snapshot, kp_restore): the arrays as they are, into the caller's buffer;
 * - the coverage (kp_state_ranges): the fields the walk visits and the ones it leaves out on purpose (the exclusion
 *   table below), which tests/c/test_save.c checks against the whole of KpGame, KpImages and KpRngGame.
 * The static data that the start of a mission rebuilds (DAT, regions, contours, tileset, vision templates, GRP
 * metadata, IScript, the hooks) and the derived state (the remembered-terrain set, the event ring, scratch lists) stay
 * out; restored() derives what a restored game needs of the latter. */
#include "kp_game_internal.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAGIC "KPSV"
#define SECTIONS_MAX 64
/* The hash: 64-bit words through a multiply-xorshift, a section's seeded with its tag and closed with its length
 * (murmur3's finaliser); "kphash" and the hash's version as the seed. */
#define HASH_SEED (0x6b70686173680000ULL | (uint64_t)KP_HASH_VERSION)
#define HASH_K 0x9e3779b97f4a7c15ULL
/* The snapshot's header: magic "KPSN", sizeof KpGame, sizeof KpImages, the region graph's, the AI's regions and the
 * trigger copies' counts, whether the game has the CRT generator, the map's width and height (u32 each), then the
 * whole size (u64). */
#define SNAP_MAGIC 0x4e53504bu
#define SNAP_FIELDS 9
#define SNAP_HEADER (SNAP_FIELDS * 4 + 8)

enum { MODE_BYTES, MODE_HASH, MODE_RAW, MODE_COVER };
/* The primitives' kinds (the coverage's alignment, the schema's fingerprint). */
enum { KIND_U8 = 1, KIND_U32, KIND_U64, KIND_I32, KIND_I64, KIND_INT, KIND_LIST };

typedef struct {
	const char *tag;
	int kind;
	const void *at;
	size_t count, elem;
} Call;

typedef struct {
	int mode;
	int reading;
	int dry; /* reading: check the save without writing into the game */
	uint8_t *buf; /* BYTES writing: growable; RAW writing: the caller's (NULL: only counted); reading: the save */
	size_t size, cap, at;
	int failed; /* writing: out of memory or past the caller's buffer; reading: past the end or a wrong section */
	char error[KP_ERROR_SIZE];
	/* BYTES and HASH: the sections walked; BYTES also where each one's bytes start (after its tag) */
	int sections;
	uint32_t section_tag[SECTIONS_MAX];
	size_t section_start[SECTIONS_MAX];
	/* HASH: the open section's state, partial word (fill bytes) and length; the sections' fold */
	uint64_t h, word, total;
	unsigned fill;
	size_t length;
	const char *tag; /* HASH and COVER: the open section's tag */
	KpSectionHash each;
	void *ud;
	/* COVER: the calls */
	Call *calls;
	size_t ncalls, ncap;
} Codec;

static void fail(Codec *c, const char *msg)
{
	if (c->failed) return;
	c->failed = 1;
	snprintf(c->error, sizeof c->error, "%s", msg);
}

/* --- the hash --- */

static uint64_t mix(uint64_t h, uint64_t w)
{
	h = (h ^ w) * HASH_K;
	return h ^ (h >> 32);
}

static uint64_t finish(uint64_t h, uint64_t n)
{
	h = mix(h, n);
	h ^= h >> 33;
	h *= 0xff51afd7ed558ccdULL;
	h ^= h >> 33;
	h *= 0xc4ceb9fe1a85ec53ULL;
	return h ^ (h >> 33);
}

static uint32_t tag_word(const char *tag)
{
	const uint8_t *t = (const uint8_t *)tag;
	return (uint32_t)t[0] | (uint32_t)t[1] << 8 | (uint32_t)t[2] << 16 | (uint32_t)t[3] << 24;
}

static uint64_t le64(const uint8_t *p)
{
	uint64_t x = 0;
	int k;
	for (k = 7; k >= 0; k--) x = x << 8 | p[k];
	return x;
}

/* A section's hash from its bytes in a save (kp_save's table, kp_load's check): the definition kp_state_hash
 * computes without the bytes. */
static uint64_t hash_bytes(uint32_t tag, const uint8_t *p, size_t n)
{
	uint64_t h = mix(HASH_SEED, tag), w = 0;
	size_t k, j;
	for (k = 0; k + 8 <= n; k += 8) h = mix(h, le64(p + k));
	if (k < n) {
		for (j = 0; k + j < n; j++) w |= (uint64_t)p[k + j] << (8 * j);
		h = mix(h, w);
	}
	return finish(h, (uint64_t)n);
}

/* The low `bytes` (1, 4 or 8) bytes of v as the save writes them, into the open section's words. */
static void hash_put(Codec *c, uint64_t v, unsigned bytes)
{
	c->length += bytes;
	c->word |= v << (8 * c->fill);
	if (c->fill + bytes < 8) {
		c->fill += bytes;
		return;
	}
	c->h = mix(c->h, c->word);
	c->fill = c->fill + bytes - 8;
	c->word = c->fill ? v >> (8 * (bytes - c->fill)) : 0;
}

static void hash_run(Codec *c, const uint8_t *p, size_t n)
{
	size_t k = 0;
	while (k < n && c->fill != 0) hash_put(c, p[k++], 1);
	for (; k + 8 <= n; k += 8) {
		c->h = mix(c->h, le64(p + k));
		c->length += 8;
	}
	while (k < n) hash_put(c, p[k++], 1);
}

static void hash_close(Codec *c)
{
	uint64_t h;
	if (!c->tag) return;
	if (c->fill) c->h = mix(c->h, c->word);
	h = finish(c->h, (uint64_t)c->length);
	c->total = mix(c->total, h);
	c->sections++;
	if (c->each) c->each(c->ud, c->tag, h, c->length);
	c->tag = NULL;
}

static void hash_open(Codec *c, const char *tag)
{
	hash_close(c);
	c->tag = tag;
	c->h = mix(HASH_SEED, tag_word(tag));
	c->word = 0;
	c->fill = 0;
	c->length = 0;
}

/* --- the other modes' leaves --- */

static void cover(Codec *c, int kind, const void *at, size_t count, size_t elem)
{
	Call *k;
	if (c->failed) return;
	if (c->ncalls == c->ncap) {
		size_t cap = c->ncap ? c->ncap * 2 : 1024;
		Call *calls = (Call *)realloc(c->calls, cap * sizeof *calls);
		if (!calls) {
			fail(c, "state: out of memory");
			return;
		}
		c->calls = calls;
		c->ncap = cap;
	}
	k = &c->calls[c->ncalls++];
	k->tag = c->tag;
	k->kind = kind;
	k->at = at;
	k->count = count;
	k->elem = elem;
}

static void put_bytes(Codec *c, const void *p, size_t n)
{
	if (c->failed) return;
	if (c->size + n > c->cap) {
		size_t cap = c->cap ? c->cap : 65536;
		uint8_t *b;
		while (c->size + n > cap) cap *= 2;
		b = (uint8_t *)realloc(c->buf, cap);
		if (!b) {
			fail(c, "save: out of memory");
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
	if (n > c->size - c->at) { /* not at + n: a save's length can wrap a 32-bit size_t (wasm) */
		fail(c, "save: truncated");
		return NULL;
	}
	p = c->buf + c->at;
	c->at += n;
	return p;
}

/* RAW: n bytes of the state as they are, into the caller's buffer (or counted) or back. */
static void raw(Codec *c, void *v, size_t n)
{
	if (c->failed) return;
	if (c->reading) {
		const uint8_t *p = take(c, n);
		if (p) memcpy(v, p, n);
	} else if (c->buf && c->size + n > c->cap) {
		fail(c, "snapshot: the buffer is too small");
	} else {
		if (c->buf) memcpy(c->buf + c->size, v, n);
		c->size += n;
	}
}

/* --- the primitives --- */

static void u64(Codec *c, uint64_t *v)
{
	uint8_t b[8];
	int k;
	switch (c->mode) {
	case MODE_COVER: cover(c, KIND_U64, v, 1, 8); return;
	case MODE_HASH: hash_put(c, *v, 8); return;
	case MODE_RAW: raw(c, v, 8); return;
	default: break;
	}
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
	switch (c->mode) {
	case MODE_COVER: cover(c, KIND_U32, v, 1, 4); return;
	case MODE_HASH: hash_put(c, *v, 4); return;
	case MODE_RAW: raw(c, v, 4); return;
	default: break;
	}
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
	switch (c->mode) {
	case MODE_COVER: cover(c, KIND_I64, v, n, 8); return;
	case MODE_HASH:
		for (k = 0; k < n; k++) hash_put(c, (uint64_t)v[k], 8);
		return;
	case MODE_RAW: raw(c, v, n * sizeof *v); return;
	default: break;
	}
	for (k = 0; k < n; k++) {
		uint64_t x = (uint64_t)v[k];
		u64(c, &x);
		v[k] = (int64_t)x;
	}
}

static void i32s(Codec *c, int32_t *v, size_t n)
{
	size_t k;
	switch (c->mode) {
	case MODE_COVER: cover(c, KIND_I32, v, n, 4); return;
	case MODE_HASH:
		for (k = 0; k < n; k++) hash_put(c, (uint32_t)v[k], 4);
		return;
	case MODE_RAW: raw(c, v, n * sizeof *v); return;
	default: break;
	}
	for (k = 0; k < n; k++) {
		uint32_t x = (uint32_t)v[k];
		u32(c, &x);
		v[k] = (int32_t)x;
	}
}

static void u32s(Codec *c, uint32_t *v, size_t n)
{
	size_t k;
	switch (c->mode) {
	case MODE_COVER: cover(c, KIND_U32, v, n, 4); return;
	case MODE_RAW: raw(c, v, n * sizeof *v); return;
	default: break;
	}
	for (k = 0; k < n; k++) u32(c, &v[k]);
}

static void u8s(Codec *c, uint8_t *v, size_t n)
{
	switch (c->mode) {
	case MODE_COVER: cover(c, KIND_U8, v, n, 1); return;
	case MODE_HASH: hash_run(c, v, n); return;
	case MODE_RAW: raw(c, v, n); return;
	default: break;
	}
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
	switch (c->mode) {
	case MODE_COVER: cover(c, KIND_INT, v, n, sizeof *v); return;
	case MODE_HASH:
		for (k = 0; k < n; k++) hash_put(c, (uint32_t)(int32_t)v[k], 4);
		return;
	case MODE_RAW: raw(c, v, n * sizeof *v); return;
	default: break;
	}
	for (k = 0; k < n; k++) {
		int32_t x = (int32_t)v[k];
		i32s(c, &x, 1);
		v[k] = x;
	}
}

static void lists(Codec *c, KpList *l, size_t n)
{
	size_t k;
	switch (c->mode) {
	case MODE_COVER: cover(c, KIND_LIST, l, n, sizeof *l); return;
	case MODE_HASH:
		for (k = 0; k < n; k++) {
			hash_put(c, (uint32_t)l[k].head, 4);
			hash_put(c, (uint32_t)l[k].tail, 4);
		}
		return;
	case MODE_RAW: raw(c, l, n * sizeof *l); return;
	default: break;
	}
	for (k = 0; k < n; k++) {
		i32s(c, &l[k].head, 1);
		i32s(c, &l[k].tail, 1);
	}
}

/* A name of the save's header (BYTES only). */
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

/* A section tag: written, or checked when reading; the hash's and the coverage's domain. */
static void section(Codec *c, const char *tag)
{
	switch (c->mode) {
	case MODE_COVER: c->tag = tag; return;
	case MODE_HASH: hash_open(c, tag); return;
	default: break;
	}
	if (c->reading) {
		const uint8_t *p = take(c, 4);
		if (p && memcmp(p, tag, 4) != 0) {
			c->failed = 1;
			snprintf(c->error, sizeof c->error, "save: section %s expected", tag);
		}
	} else if (c->mode == MODE_RAW) {
		raw(c, (void *)tag, 4);
	} else {
		put_bytes(c, tag, 4);
	}
	if (c->mode == MODE_BYTES && !c->failed) {
		if (c->sections == SECTIONS_MAX) {
			fail(c, "save: too many sections");
			return;
		}
		c->section_tag[c->sections] = tag_word(tag);
		c->section_start[c->sections++] = c->reading ? c->at : c->size;
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
/* a multi-dimensional int64_t array, all its elements */
#define I64N(field) i64s(c, (int64_t *)(field), sizeof(field) / sizeof(int64_t))

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
	I64S(g->bullets.hit_source);
	I64S(g->bullets.hit_target);
	I64(g->bullets.hit_used);
	section(c, "LDAT");
	{
		/* The DAT values the game writes while it runs: the forward offsets of attkshiftproj (0x411023). */
		int64_t offsets[KP_WEAPON_IDS];
		memset(offsets, 0, sizeof offsets); /* the coverage's walk reads no DAT (the schema's game has none) */
		if (c->mode != MODE_COVER) {
			for (k = 0; k < KP_WEAPON_IDS; k++) offsets[k] = kp_dat_read(g, KP_COL_weapons_forward_offset, (int64_t)k);
		}
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
	I64(g->ai.spell_reactive);
	I64(g->power_head);
	I64S(g->power.prev);
	I64(g->power.fields_shown);
	section(c, "AIRG");
	{
		/* The AI region records within the map's region count (at most KP_AI_REGIONS, kp_ai_regions_init); the
		 * coverage counts the whole rows (past the map's regions they are no state). */
		size_t count = c->mode == MODE_COVER ? (size_t)KP_AI_REGIONS : (size_t)g->regions.count;
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
	I64S(g->pai.minerals);
	I64S(g->cost_minerals);
	I64S(g->cost_gas);
	I64S(g->cost_supply);
	I64S(g->pai.gas);
	I64S(g->map_rect);
	section(c, "AILS");
	I64S(g->pai.demand_minerals);
	I64S(g->pai.demand_gas);
	I64S(g->pai.demand_supply);
	I64S(g->pai.supply);
	I64N(g->pai.heap_priority);
	I64N(g->pai.heap_kind);
	I64N(g->pai.heap_id);
	I64N(g->pai.heap_context);
	I64N(g->pai.roster);
	I64N(g->pai.defense);
	I64N(g->pai.limits);
	I64N(g->pai.area);
	I64S(g->pai.medic);
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
	I64S(t->recede_next);
	I64S(t->recede_prev);
	I64S(t->recede_x);
	I64S(t->recede_y);
	I64S(t->recede_bucket);
	I64S(t->recede_head);
	I64S(t->recede_node);
	section(c, "PLAY");
	I64S(pl->controller);
	I64S(pl->race);
	I64S(pl->force);
	for (k = 0; k < KP_PLAYERS; k++) {
		I64S(pl->alliance[k]);
		I64S(pl->upgrades[k]);
		I64S(pl->upgrade_max[k]);
		I64S(pl->unit_available[k]);
		I64S(pl->tech_available[k]);
		I64S(pl->tech_researched[k]);
		I64S(pl->research_progress[k]);
		I64S(pl->upgrade_progress[k]);
	}
	I64S(pl->shared_vision);
	I64S(pl->minerals);
	I64S(pl->gas);
	I64S(pl->cumulative_minerals);
	I64S(pl->cumulative_gas);
	I64S(pl->start_x);
	I64S(pl->start_y);
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
	I64S(g->guards.time);
	I64S(g->guards.heads);
	I64(g->guards.free);
	section(c, "SCRP");
#define AI_SCRIPT(name) I64S(g->scripts.name);
	KP_AI_SCRIPT_FIELDS(AI_SCRIPT)
#undef AI_SCRIPT
	I64(g->scripts.head);
	I64(g->scripts.free);
	I64(g->scripts.ret);
	section(c, "TOWN");
#define TOWN(name) I64S(g->towns.name);
	KP_TOWN_FIELDS(TOWN)
#undef TOWN
	I64N(g->towns.gas);
	I64N(g->towns.requests);
	I64S(g->towns.heads);
	I64(g->towns.free);
	section(c, "WKAI");
#define WORKER_AI(name) I64S(g->workers.name);
	KP_WORKER_AI_FIELDS(WORKER_AI)
#undef WORKER_AI
	I64(g->workers.free);
	section(c, "BDAI");
#define BUILDING_AI(name) I64S(g->buildings.name);
	KP_BUILDING_AI_FIELDS(BUILDING_AI)
#undef BUILDING_AI
	I64N(g->buildings.tags);
	I64N(g->buildings.contexts);
	I64(g->buildings.free);
	section(c, "MLAI");
#define MILITARY_AI(name) I64S(g->military.name);
	KP_MILITARY_AI_FIELDS(MILITARY_AI)
#undef MILITARY_AI
	I64(g->military.free);
	section(c, "AREA");
#define AREA(name) I64S(g->areas.name);
	KP_AREA_FIELDS(AREA)
#undef AREA
	I64(g->areas.used);
	I64(g->areas.countdown);
	I64(g->ai.anchor_count);
	I64S(g->ai.anchors);
	section(c, "TRIG");
	{
		/* The copies' records with their lists (the start of the same mission made as many). */
		KpTriggers *tr = &g->trig;
		uint32_t count = (uint32_t)tr->count;
		checked_u32(c, &count);
		if (c->reading && !c->failed && count != (uint32_t)tr->count) {
			c->failed = 1;
			snprintf(c->error, sizeof c->error, "save: %u trigger copies, the mission has %lld", count,
				(long long)tr->count);
			return;
		}
		u8s(c, tr->records, (size_t)tr->count * KP_TRIGGER_RECORD);
		i64s(c, tr->next, (size_t)tr->count);
		I64S(tr->head);
		I64(tr->second);
		I64(tr->pass);
		I64(tr->result);
		I64(tr->leaderboard);
		I64(tr->player);
		I64(tr->current);
		I64(tr->paused);
		I64(tr->result_dialog);
		I64S(tr->wait_active);
		I64S(tr->wait_remaining);
		I64S(tr->pending);
		I64S(tr->participants);
		I64S(tr->objectives);
		I64S(tr->switches);
		I64(tr->countdown);
		I64(tr->countdown_paused);
		I64(tr->quiet);
		for (k = 0; k < KP_LOCATIONS; k++) I64S(tr->locations[k]);
		u8s(c, tr->next_scenario, sizeof tr->next_scenario);
		I64(tr->lb_computers);
		I64(tr->lb_action);
		I64(tr->lb_location);
		I64(tr->lb_type);
		I64(tr->lb_goal);
		I64(tr->lb_label);
		for (k = 0; k < KP_TRIGGER_PLAYERS; k++) I64S(tr->lb_entries[k]);
		I64(tr->count_key);
		I64(tr->count_location);
		I64S(tr->count_all);
		I64S(tr->count_completed);
		for (k = 0; k < KP_PSTAT_ARRAYS; k++) I64S(tr->count_pstat[k]);
	}
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

/* What a restored game derives (kp_load, kp_restore): the remembered-terrain set is rebuilt at its next refresh; the
 * game's pending events are dropped; the countdown's text follows the countdown; the interface takes the game anew. */
static void restored(KpGame *g)
{
	g->terrain.has_terrain_changed = 0;
	g->events.first = g->events.count = g->events.texts_waiting = 0;
	/* the countdown's text (not saved) as the restored countdown shows it, not the game's before */
	g->trig.countdown_text = g->trig.countdown != 0 ? g->trig.countdown : -1;
	/* for the interface: another selection, another console and terrain (kp_ui_hud_frame) */
	g->local_selection_serial++;
	g->interface_refresh++;
	g->minimap_terrain++;
	g->load_serial++;
}

/* --- saves --- */

static void header(Codec *c, uint32_t *version, char *mission, char *data)
{
	if (c->reading) {
		const uint8_t *p = take(c, 4);
		if (p && memcmp(p, MAGIC, 4) != 0) fail(c, "save: not a save");
	} else {
		put_bytes(c, MAGIC, 4);
	}
	u32(c, version);
	str(c, mission, KP_SAVE_NAME);
	str(c, data, KP_SAVE_NAME);
}

/* Section k's bytes (after its tag, up to the next tag or, for the last, to end) hashed. */
static uint64_t section_hash(const Codec *c, const uint8_t *bytes, int k, size_t end)
{
	size_t start = c->section_start[k];
	size_t stop = k + 1 < c->sections ? c->section_start[k + 1] - 4 : end;
	return hash_bytes(c->section_tag[k], bytes + start, stop - start);
}

/* The hash table after the sections: u32 their count, per section its tag and u64 hash, then u64 the total (the
 * sections' hashes folded as kp_state_hash folds them). */
static void put_table(Codec *c)
{
	size_t end = c->size;
	uint32_t n = (uint32_t)c->sections;
	uint64_t total = HASH_SEED;
	int k;
	if (c->failed) return;
	u32(c, &n);
	for (k = 0; k < c->sections && !c->failed; k++) {
		uint32_t tag = c->section_tag[k];
		uint64_t h = section_hash(c, c->buf, k, end);
		total = mix(total, h);
		u32(c, &tag);
		u64(c, &h);
	}
	total = finish(total, n);
	u64(c, &total);
}

static int check_table(Codec *c, const uint8_t *bytes)
{
	size_t end = c->at;
	uint32_t n = 0;
	uint64_t want = 0, total = HASH_SEED;
	int k;
	checked_u32(c, &n);
	if (!c->failed && n != (uint32_t)c->sections) {
		c->failed = 1;
		snprintf(c->error, sizeof c->error, "save: a hash table of %u sections, the save has %d", n, c->sections);
	}
	for (k = 0; k < c->sections && !c->failed; k++) {
		uint32_t tag = 0;
		uint64_t h;
		checked_u32(c, &tag);
		checked_u64(c, &want);
		if (c->failed) break;
		h = section_hash(c, bytes, k, end);
		if (tag != c->section_tag[k] || h != want) {
			uint32_t t = c->section_tag[k];
			c->failed = 1;
			snprintf(c->error, sizeof c->error, "save: section %c%c%c%c damaged", (char)(t & 0xff), (char)(t >> 8 & 0xff),
				(char)(t >> 16 & 0xff), (char)(t >> 24));
			break;
		}
		total = mix(total, h);
	}
	checked_u64(c, &want);
	if (!c->failed && finish(total, n) != want) fail(c, "save: damaged (hash table)");
	return c->failed ? -1 : 0;
}

int kp_save(KpGame *g, const char *mission, const char *data, uint8_t **out, size_t *size, KpError *err)
{
	Codec c;
	char m[KP_SAVE_NAME], d[KP_SAVE_NAME];
	uint32_t version = KP_SAVE_VERSION;
	memset(&c, 0, sizeof c);
	if (strlen(mission) >= sizeof m || strlen(data) >= sizeof d) {
		snprintf(err->msg, sizeof err->msg, "save: a mission or data name of %d bytes or more", KP_SAVE_NAME);
		return -1;
	}
	snprintf(m, sizeof m, "%s", mission);
	snprintf(d, sizeof d, "%s", data);
	header(&c, &version, m, d);
	walk(&c, g);
	put_table(&c);
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
	size_t start;
	memset(&c, 0, sizeof c);
	c.reading = 1;
	c.buf = (uint8_t *)bytes;
	c.size = size;
	header(&c, &version, m, d);
	if (!c.failed && version != KP_SAVE_VERSION) {
		snprintf(err->msg, sizeof err->msg, "save: version %u, this build reads %d", version, KP_SAVE_VERSION);
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
	/* A dry walk checks every section, the region count, the generators, the length and the hashes of the sections'
	 * bytes first, so a refused save leaves the game as it was; the second walk writes. */
	start = c.at;
	c.dry = 1;
	if (!c.failed) walk(&c, g);
	if (!c.failed) (void)check_table(&c, bytes);
	if (!c.failed && c.at != c.size) {
		c.failed = 1;
		snprintf(c.error, sizeof c.error, "save: %u bytes past the state", (unsigned)(c.size - c.at));
	}
	if (!c.failed) {
		c.dry = 0;
		c.at = start;
		c.sections = 0;
		walk(&c, g);
	}
	if (c.failed) {
		snprintf(err->msg, sizeof err->msg, "%s", c.error);
		return -1;
	}
	restored(g);
	return 0;
}

/* --- the state hash and the schema --- */

int kp_state_hash(KpGame *g, KpSectionHash each, void *ud, uint64_t *total)
{
	Codec c;
	memset(&c, 0, sizeof c);
	c.mode = MODE_HASH;
	c.each = each;
	c.ud = ud;
	c.total = HASH_SEED;
	walk(&c, g);
	hash_close(&c);
	if (total) *total = finish(c.total, (uint64_t)c.sections);
	return c.failed ? -1 : 0;
}

uint64_t kp_save_schema(void)
{
	/* The walk over a zeroed game: the map's counts are zero there (the AI regions' rows are KP_AI_REGIONS in the
	 * coverage's walk), so only the code decides the calls. */
	KpGame *g = (KpGame *)calloc(1, sizeof *g);
	KpImages *w = (KpImages *)calloc(1, sizeof *w);
	KpRngGame *rng = (KpRngGame *)calloc(1, sizeof *rng);
	uint64_t h = 0;
	if (g && w && rng) {
		Codec c;
		size_t k;
		g->w = w;
		g->rng = rng;
		memset(&c, 0, sizeof c);
		c.mode = MODE_COVER;
		walk(&c, g);
		h = HASH_SEED;
		for (k = 0; k < c.ncalls; k++) {
			h = mix(h, tag_word(c.calls[k].tag));
			h = mix(h, (uint64_t)c.calls[k].kind);
			h = mix(h, (uint64_t)c.calls[k].count);
		}
		h = c.failed ? 0 : finish(h, (uint64_t)c.ncalls);
		free(c.calls);
	}
	free(g);
	free(w);
	free(rng);
	return h;
}

/* --- snapshots --- */

static void snap_fields(KpGame *g, uint32_t f[SNAP_FIELDS])
{
	f[0] = SNAP_MAGIC;
	f[1] = (uint32_t)sizeof(KpGame);
	f[2] = (uint32_t)sizeof(KpImages);
	f[3] = (uint32_t)g->pathing.rg.count;
	f[4] = (uint32_t)g->regions.count;
	f[5] = (uint32_t)g->trig.count;
	f[6] = g->crt != NULL;
	f[7] = (uint32_t)g->map_w;
	f[8] = (uint32_t)g->map_h;
}

size_t kp_snapshot_size(KpGame *g)
{
	Codec c;
	memset(&c, 0, sizeof c);
	c.mode = MODE_RAW;
	walk(&c, g);
	return SNAP_HEADER + c.size;
}

int kp_snapshot(KpGame *g, uint8_t *buf, size_t size, KpError *err)
{
	Codec c;
	uint32_t f[SNAP_FIELDS];
	uint64_t total;
	int k;
	if (size < SNAP_HEADER) {
		snprintf(err->msg, sizeof err->msg, "snapshot: a buffer of %lu bytes", (unsigned long)size);
		return -1;
	}
	memset(&c, 0, sizeof c);
	c.mode = MODE_RAW;
	c.buf = buf + SNAP_HEADER;
	c.cap = size - SNAP_HEADER;
	walk(&c, g);
	if (c.failed) {
		snprintf(err->msg, sizeof err->msg, "%s", c.error);
		return -1;
	}
	snap_fields(g, f);
	for (k = 0; k < SNAP_FIELDS; k++) memcpy(buf + 4 * k, &f[k], 4);
	total = (uint64_t)(SNAP_HEADER + c.size);
	memcpy(buf + 4 * SNAP_FIELDS, &total, 8);
	return 0;
}

int kp_restore(KpGame *g, const uint8_t *buf, size_t size, KpError *err)
{
	static const char *const NAMES[SNAP_FIELDS] = { "magic", "game size", "world size", "region graph count",
		"AI region count", "trigger copies", "CRT generator", "map width", "map height" };
	uint32_t want[SNAP_FIELDS], have;
	uint64_t total;
	size_t expected;
	Codec c;
	int k;
	if (size < SNAP_HEADER) {
		snprintf(err->msg, sizeof err->msg, "snapshot: truncated");
		return -1;
	}
	/* the whole header before the first change: after it the walk reads what kp_snapshot wrote of this game */
	snap_fields(g, want);
	for (k = 0; k < SNAP_FIELDS; k++) {
		memcpy(&have, buf + 4 * k, 4);
		if (have != want[k]) {
			snprintf(err->msg, sizeof err->msg, "snapshot: %s %u, the game's %u", NAMES[k], have, want[k]);
			return -1;
		}
	}
	memcpy(&total, buf + 4 * SNAP_FIELDS, 8);
	expected = kp_snapshot_size(g);
	if (total > size || total != (uint64_t)expected) {
		snprintf(err->msg, sizeof err->msg, "snapshot: %llu bytes in a buffer of %lu, the game's %lu", (unsigned long long)total,
			(unsigned long)size, (unsigned long)expected);
		return -1;
	}
	memset(&c, 0, sizeof c);
	c.mode = MODE_RAW;
	c.reading = 1;
	c.buf = (uint8_t *)buf + SNAP_HEADER;
	c.size = (size_t)total - SNAP_HEADER;
	walk(&c, g);
	if (c.failed) { /* a section tag of a damaged buffer: the game is then partly restored */
		snprintf(err->msg, sizeof err->msg, "%s", c.error);
		return -1;
	}
	restored(g);
	return 0;
}

/* --- the coverage --- */

typedef struct {
	KpStateRange *r;
	size_t n, cap;
	int failed;
} Ranges;

static int base_of(KpGame *g, const void *at, size_t bytes, size_t *offset)
{
	uintptr_t a = (uintptr_t)at, b[KP_STATE_BASES];
	size_t size[KP_STATE_BASES];
	int k;
	b[KP_STATE_GAME] = (uintptr_t)g;
	size[KP_STATE_GAME] = sizeof *g;
	b[KP_STATE_WORLD] = (uintptr_t)g->w;
	size[KP_STATE_WORLD] = sizeof *g->w;
	b[KP_STATE_RNG] = (uintptr_t)g->rng;
	size[KP_STATE_RNG] = sizeof *g->rng;
	for (k = 0; k < KP_STATE_BASES; k++) {
		if (a >= b[k] && a - b[k] + bytes <= size[k]) {
			*offset = (size_t)(a - b[k]);
			return k;
		}
	}
	return -1;
}

static void add_range(Ranges *rs, KpGame *g, int klass, const void *at, size_t bytes, size_t align, const char *name)
{
	size_t offset;
	int base;
	KpStateRange *r;
	if (rs->failed || bytes == 0 || (base = base_of(g, at, bytes, &offset)) < 0) return; /* heap and locals: not here */
	if (rs->n == rs->cap) {
		size_t cap = rs->cap ? rs->cap * 2 : 1024;
		KpStateRange *more = (KpStateRange *)realloc(rs->r, cap * sizeof *more);
		if (!more) {
			rs->failed = 1;
			return;
		}
		rs->r = more;
		rs->cap = cap;
	}
	r = &rs->r[rs->n++];
	r->base = base;
	r->klass = klass;
	r->offset = offset;
	r->bytes = bytes;
	r->align = align;
	r->name = name;
}

/* The largest power of two up to 8 dividing n: an element's alignment from its size. */
static size_t align_of_size(size_t n)
{
	size_t a = 1;
	while (a < 8 && n % (a * 2) == 0) a *= 2;
	return a;
}

static size_t kind_align(int kind)
{
	switch (kind) {
	case KIND_U8: return 1;
	case KIND_U64:
	case KIND_I64: return 8;
	default: return 4;
	}
}

#define EXCLUDE(field, klass) add_range(rs, g, KP_STATE_##klass, &(field), sizeof(field), align_of_size(sizeof(field)), #field)
#define EXCLUDE_ARRAY(field, klass) \
	add_range(rs, g, KP_STATE_##klass, (field), sizeof(field), align_of_size(sizeof((field)[0])), #field)
/* The padding between the end of a member and the next one, measured on this platform (0 bytes, so no range, where
 * there is none): for a next member whose struct is aligned past its first walked field, so the coverage cannot tell
 * the padding by that field's alignment. */
#define EXCLUDE_PADDING(end, next) \
	add_range(rs, g, KP_STATE_PADDING, (end), (size_t)((const char *)&(next) - (const char *)(end)), 1, "padding before " #next)

/* The fields the walk leaves out, each with its class:
 * - DERIVED: rebuilt from the saved state or from the mission (restored() and the start), or scratch of one step;
 * - STATIC: the mission's and the data's, which the start of the same mission builds the same;
 * - VIEW: the interface's and the presentation's, no state of the simulation;
 * - HOST: the host's objects and settings, and the tests' overrides;
 * - PADDING: a platform's padding the coverage cannot tell by the next field's alignment. */
static void exclusions(Ranges *rs, KpGame *g)
{
	KpImages *w = g->w;
	/* KpGame: the host's objects, the game's DAT and hooks */
	EXCLUDE(g->w, HOST);
	EXCLUDE(g->rng, HOST);
	EXCLUDE(g->own_world, HOST);
	EXCLUDE(g->crt, HOST); /* its holdrand is saved (RNGS) */
	EXCLUDE(g->cdat, STATIC);
	EXCLUDE_ARRAY(g->col, STATIC);
	EXCLUDE(g->ctx, HOST);
	EXCLUDE(g->probe_hook, HOST);
	EXCLUDE(g->probe_hook_ctx, HOST);
	/* the settings fixed at the start; who watches is the host's */
	EXCLUDE(g->map_w, STATIC);
	EXCLUDE(g->map_h, STATIC);
	EXCLUDE(g->map_w_px, STATIC);
	EXCLUDE(g->map_h_px, STATIC);
	EXCLUDE(g->local_player, HOST);
	EXCLUDE(g->local_vision, HOST);
	EXCLUDE(g->expansion, STATIC);
	/* a replay's playback (a replay has no saves) */
	EXCLUDE(g->replay_multiplier, HOST);
	EXCLUDE(g->replay_paused, HOST);
	EXCLUDE(g->replay_frames, HOST);
	EXCLUDE(g->replay_header_speed, HOST);
	/* the interface's refreshes (restored() raises them) */
	EXCLUDE(g->local_selection_serial, VIEW);
	EXCLUDE(g->interface_refresh, VIEW);
	EXCLUDE(g->minimap_terrain, VIEW);
	EXCLUDE(g->load_serial, VIEW);
	/* the tests' overrides */
	EXCLUDE_ARRAY(g->req_program, HOST);
	EXCLUDE(g->req_program_words, HOST);
	EXCLUDE(g->req_program_set, HOST);
	/* locks set only within one call */
	EXCLUDE(g->ai.migrating, DERIVED);
	EXCLUDE(g->ai.placement_builder, DERIVED);
	/* the Psi field's mask, built at the start from its graphic */
	EXCLUDE(g->power.half_w, STATIC);
	EXCLUDE(g->power.half_h, STATIC);
	EXCLUDE_ARRAY(g->power.mask, STATIC);
	/* the nested queries' result stack, empty between steps */
	EXCLUDE_ARRAY(g->spatial.frames, DERIVED);
	EXCLUDE(g->spatial.frame_top, DERIVED);
	/* the mission's regions and contours, the region graph (its run-time slots are saved, PATH) */
	EXCLUDE(g->regions, STATIC);
	EXCLUDE_ARRAY(g->contours, STATIC);
	/* KpPathing is aligned to 8 but walked from its bytes: where pointers are 4 bytes (wasm) the contours end 4 bytes
	 * before it */
	EXCLUDE_PADDING(g->contours + ARRAY_N(g->contours), g->pathing);
	EXCLUDE(g->pathing.rg, STATIC);
	/* the collision's scratch */
	EXCLUDE_ARRAY(g->collision.swept, DERIVED);
	EXCLUDE_ARRAY(g->collision.adjacent, DERIVED);
	EXCLUDE_ARRAY(g->collision.query, DERIVED);
	/* the remembered-terrain set (rebuilt at its next refresh), the tileset and the vision templates */
	EXCLUDE(g->terrain.has_terrain_changed, DERIVED);
	EXCLUDE_ARRAY(g->terrain.terrain_changed, DERIVED);
	EXCLUDE(g->terrain.cv5, STATIC);
	EXCLUDE(g->terrain.vf4, STATIC);
	EXCLUDE(g->terrain.cv5_size, STATIC);
	EXCLUDE(g->terrain.vf4_size, STATIC);
	EXCLUDE(g->terrain.mflags, STATIC);
	EXCLUDE(g->terrain.mflags_count, STATIC);
	EXCLUDE(g->terrain.has_templates, STATIC);
	EXCLUDE_ARRAY(g->terrain.templates, STATIC);
	EXCLUDE_ARRAY(g->players.force_name, STATIC);
	/* the game's notifications to the host (a restore drops the waiting ones) */
	EXCLUDE(g->events, VIEW);
	/* the AI scripts' binaries, the tests' synthetic ones, the AI's scratch */
	EXCLUDE(g->ai_bins, STATIC);
	EXCLUDE(g->ai_game_bins, STATIC);
	EXCLUDE(g->ai_program, HOST);
	EXCLUDE(g->ai_scratch, DERIVED);
	/* the trigger copies the start makes (their records and links are saved, TRIG), the briefing's lead, the mission's
	 * texts; the countdown's text follows the countdown (restored()) */
	EXCLUDE(g->trig.records, STATIC);
	EXCLUDE(g->trig.next, STATIC);
	EXCLUDE(g->trig.place, STATIC);
	EXCLUDE(g->trig.count, STATIC);
	EXCLUDE(g->trig.lead_objectives, STATIC);
	EXCLUDE(g->trig.countdown_text, DERIVED);
	EXCLUDE_ARRAY(g->trig.properties, STATIC);
	EXCLUDE(g->trig.strings, STATIC);
	EXCLUDE(g->trig.strings_size, STATIC);
	EXCLUDE(g->trig.briefing, STATIC);
	EXCLUDE(g->trig.briefing_count, STATIC);
	/* KpImages: the data, the hooks, the settings fixed at the start and the host's */
	EXCLUDE(w->dat, STATIC);
	EXCLUDE(w->grp, STATIC);
	EXCLUDE(w->script, STATIC);
	EXCLUDE(w->rng, HOST);
	EXCLUDE(w->col, STATIC);
	EXCLUDE(w->hook, HOST);
	EXCLUDE(w->tile_flags, HOST);
	EXCLUDE(w->tile_ctx, HOST);
	EXCLUDE(w->ctx, HOST);
	EXCLUDE(w->map_w, STATIC);
	EXCLUDE(w->map_h, STATIC);
	EXCLUDE(w->map_w_px, STATIC);
	EXCLUDE(w->map_h_px, STATIC);
	EXCLUDE(w->tileset, STATIC);
	EXCLUDE(w->expansion, STATIC);
	EXCLUDE(w->local_player, HOST);
	EXCLUDE(w->replay, HOST);
	EXCLUDE(w->replay_reveal, HOST);
	EXCLUDE(w->local_vision, HOST);
	EXCLUDE(w->step_limit, HOST);
	EXCLUDE(w->has_sound, HOST);
	EXCLUDE(w->has_play_probe, HOST);
	/* the draw list's sort keys, the overlay slots' cache, the failure handler */
	EXCLUDE_ARRAY(w->draw_keys, DERIVED);
	EXCLUDE_ARRAY(w->overlay_slots, DERIVED);
	EXCLUDE(w->try_, HOST);
	EXCLUDE_ARRAY(w->error, HOST);
}

int kp_state_ranges(KpGame *g, KpStateRange **out, size_t *count)
{
	Codec c;
	Ranges rs;
	size_t k;
	*out = NULL;
	*count = 0;
	memset(&c, 0, sizeof c);
	c.mode = MODE_COVER;
	walk(&c, g);
	memset(&rs, 0, sizeof rs);
	for (k = 0; k < c.ncalls; k++) {
		const Call *a = &c.calls[k];
		add_range(&rs, g, KP_STATE_SAVED, a->at, a->count * a->elem, kind_align(a->kind), a->tag);
	}
	free(c.calls);
	exclusions(&rs, g);
	if (c.failed || rs.failed) {
		free(rs.r);
		return -1;
	}
	*out = rs.r;
	*count = rs.n;
	return 0;
}
