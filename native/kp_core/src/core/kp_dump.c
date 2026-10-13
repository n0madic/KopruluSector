/* The state lines of a game (kp_game.h) in the format of tools/native_state.py: one line per record,
 * `<category> <key> <field>=<value> ...` with the fields in sorted order, so the original's dumps and Koprulu's
 * state compare line by line and hash the same way. Allocates (the dump is not part of a step). */
#include "kp_game_internal.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RESOURCE_CONTAINER 0x2000
#define MAX_FIELDS 256

typedef struct {
	size_t name, value; /* offsets into the arena */
} Field;

typedef struct {
	KpDumpLine emit;
	void *ud;
	int drawn; /* leave out the fields the original's drawing refreshes (image x, y) */
	char *arena;
	size_t used, cap;
	Field fields[MAX_FIELDS];
	int count;
	char category[32], key[64];
	int failed;
} Dump;

static const char *sort_arena; /* the arena of the fields being sorted (dumps run on one thread) */

static int by_name(const void *a, const void *b)
{
	return strcmp(sort_arena + ((const Field *)a)->name, sort_arena + ((const Field *)b)->name);
}

/* Appends `size` bytes to the arena; returns the offset or (size_t)-1. */
static size_t put(Dump *d, const char *s, size_t size)
{
	size_t at = d->used;
	if (d->failed) return (size_t)-1;
	if (d->used + size + 1 > d->cap) {
		size_t cap = d->cap ? d->cap : 4096;
		char *p;
		while (d->used + size + 1 > cap) cap *= 2;
		p = (char *)realloc(d->arena, cap);
		if (!p) {
			d->failed = 1;
			return (size_t)-1;
		}
		d->arena = p;
		d->cap = cap;
	}
	memcpy(d->arena + d->used, s, size);
	d->used += size;
	d->arena[d->used] = '\0';
	return at;
}

static void begin(Dump *d, const char *category, const char *fmt, ...)
{
	va_list ap;
	snprintf(d->category, sizeof d->category, "%s", category);
	va_start(ap, fmt);
	vsnprintf(d->key, sizeof d->key, fmt, ap);
	va_end(ap);
	d->used = 0;
	d->count = 0;
}

/* A field whose value is the string s (size bytes). */
static void field_str(Dump *d, const char *name, const char *s, size_t size)
{
	Field *f;
	if (d->failed) return;
	if (d->count >= MAX_FIELDS) {
		d->failed = 1;
		return;
	}
	if (d->drawn && strcmp(d->category, "image") == 0 && (strcmp(name, "x") == 0 || strcmp(name, "y") == 0)) return;
	f = &d->fields[d->count++];
	f->name = put(d, name, strlen(name) + 1);
	f->value = put(d, s, size);
	put(d, "", 1);
}

/* A number field in decimal; KP_NIL leaves the field out. */
static void field_int(Dump *d, const char *name, int64_t v)
{
	char s[32];
	if (v == KP_NIL) return;
	field_str(d, name, s, (size_t)snprintf(s, sizeof s, "%lld", (long long)v));
}

static void field_fmt(Dump *d, const char *name, const char *fmt, ...)
{
	char s[64];
	int n;
	va_list ap;
	va_start(ap, fmt);
	n = vsnprintf(s, sizeof s, fmt, ap);
	va_end(ap);
	field_str(d, name, s, (size_t)n);
}

static void end(Dump *d)
{
	char *line, *p;
	size_t size;
	int k;
	if (d->failed) return;
	sort_arena = d->arena;
	qsort(d->fields, (size_t)d->count, sizeof d->fields[0], by_name);
	size = strlen(d->category) + 1 + strlen(d->key);
	for (k = 0; k < d->count; k++) size += 2 + strlen(d->arena + d->fields[k].name) + strlen(d->arena + d->fields[k].value);
	line = (char *)malloc(size + 1);
	if (!line) {
		d->failed = 1;
		return;
	}
	p = line + snprintf(line, size + 1, "%s %s", d->category, d->key);
	for (k = 0; k < d->count; k++) {
		p += snprintf(p, size + 1 - (size_t)(p - line), " %s=%s", d->arena + d->fields[k].name, d->arena + d->fields[k].value);
	}
	d->emit(d->ud, line, (size_t)(p - line));
	free(line);
}

/* A growable string for list values (a,b,c). */
typedef struct {
	char *s;
	size_t len, cap;
	int failed;
} Text;

static void text_add(Text *t, const char *fmt, ...)
{
	char buf[64];
	int n;
	va_list ap;
	va_start(ap, fmt);
	n = vsnprintf(buf, sizeof buf, fmt, ap);
	va_end(ap);
	if (t->failed) return;
	if (t->len + (size_t)n + 1 > t->cap) {
		size_t cap = t->cap ? t->cap * 2 : 256;
		char *p;
		while (t->len + (size_t)n + 1 > cap) cap *= 2;
		p = (char *)realloc(t->s, cap);
		if (!p) {
			t->failed = 1;
			return;
		}
		t->s = p;
		t->cap = cap;
	}
	memcpy(t->s + t->len, buf, (size_t)n + 1);
	t->len += (size_t)n;
}

static void text_item(Text *t, const char *fmt, int64_t v)
{
	if (t->len > 0) text_add(t, ",");
	text_add(t, fmt, (long long)v);
}

static void field_text(Dump *d, const char *name, Text *t)
{
	if (t->failed) d->failed = 1;
	field_str(d, name, t->s ? t->s : "", t->len);
	free(t->s);
	memset(t, 0, sizeof *t);
}

static Text text_new(void)
{
	Text t;
	memset(&t, 0, sizeof t);
	return t;
}

/* The chain of records from head through next[] up to none. */
#define CHAIN(t, head, next, none, limit) \
	do { \
		int64_t c_ = (head); \
		int guard_ = 0; \
		while (c_ != (none) && c_ >= 0 && c_ < (limit) && guard_++ <= (limit)) { \
			text_item(&(t), "%lld", c_); \
			c_ = (next)[c_]; \
		} \
	} while (0)

static void unit_line(Dump *d, KpGame *g, int64_t n, const char *list)
{
	static const char *const UNION_WORDS[7] = { "union_0xc0", "union_0xc4", "union_0xc8", "union_0xcc", "union_0xd0",
		"union_0xd4", "union_0xd8" };
	KpUnits *u = &g->u;
	int64_t unions = u->union_units[n];
	int k;
	begin(d, "unit", "%lld", (long long)n);
#define UNIT_FIELD(name) \
	if (strcmp(#name, "prev") != 0 && strcmp(#name, "next") != 0 && strcmp(#name, "union_units") != 0 \
		&& strncmp(#name, "union_0x", 8) != 0 && strcmp(#name, "ai") != 0) { \
		field_int(d, #name, u->name[n]); \
	}
	KP_UNIT_FIELDS(UNIT_FIELD)
#undef UNIT_FIELD
	{
		int64_t words[7];
		words[0] = u->union_0xc0[n];
		words[1] = u->union_0xc4[n];
		words[2] = u->union_0xc8[n];
		words[3] = u->union_0xcc[n];
		words[4] = u->union_0xd0[n];
		words[5] = u->union_0xd4[n];
		words[6] = u->union_0xd8[n];
		for (k = 0; k < 7; k++) {
			if (k == 6 && (KP_DAT(g, units, special_ability_flags, u->type[n]) & RESOURCE_CONTAINER) != 0) {
				/* TODO: resource area tags of GenerateResourceAreas 0x497a20 (byte +0xd8 of resources), the AI's
				 * resource areas (plan phase 7B). */
				continue;
			}
			if ((unions & ((int64_t)1 << k)) != 0) field_fmt(d, UNION_WORDS[k], "u%lld", (long long)words[k]);
			else field_int(d, UNION_WORDS[k], words[k]);
		}
	}
	if (u->ai[n] != 0) field_fmt(d, "ai", "g%lld", (long long)(u->ai[n] - 1));
	else field_int(d, "ai", 0);
	field_str(d, "list", list, strlen(list));
	end(d);
}

/* The header, points and region ids of path record k (tools/native_state.py PATH). */
static void path_line(Dump *d, KpGame *g, int64_t n, int64_t k)
{
	Text pts = text_new(), regions = text_new();
	int64_t points = kp_path_s8(g, k, KP_PATH_POINTS), count = kp_path_s8(g, k, KP_PATH_REGIONS), i, x, y, at;
	begin(d, "path", "%lld", (long long)n);
	field_int(d, "record", k);
	field_int(d, "frame", kp_path_u32(g, k, KP_PATH_FRAME));
	field_int(d, "blocker", kp_path_u32(g, k, KP_PATH_BLOCKER));
	field_int(d, "speed", kp_path_u32(g, k, KP_PATH_SPEED));
	field_int(d, "flags", kp_path_u8(g, k, KP_PATH_FLAGS));
	field_int(d, "retry", kp_path_u8(g, k, KP_PATH_RETRY));
	field_int(d, "slide", kp_path_u8(g, k, KP_PATH_SLIDE));
	field_int(d, "full_regions", kp_path_u8(g, k, KP_PATH_FULL_REGIONS));
	field_int(d, "regions", count);
	field_int(d, "region_index", kp_path_s8(g, k, KP_PATH_REGION_INDEX));
	field_int(d, "points", points);
	field_int(d, "point_index", kp_path_s8(g, k, KP_PATH_POINT_INDEX));
	kp_path_point(g, k, KP_PATH_SRC, &x, &y);
	field_int(d, "src_x", x);
	field_int(d, "src_y", y);
	kp_path_point(g, k, KP_PATH_NEXT, &x, &y);
	field_int(d, "next_x", x);
	field_int(d, "next_y", y);
	kp_path_point(g, k, KP_PATH_DEST, &x, &y);
	field_int(d, "dest_x", x);
	field_int(d, "dest_y", y);
	for (i = 0; i <= points - 1; i++) {
		kp_path_point(g, k, KP_PATH_PAYLOAD + 4 * i, &x, &y);
		if (pts.len > 0) text_add(&pts, ",");
		text_add(&pts, "%lld:%lld", (long long)x, (long long)y);
	}
	at = KP_PATH_PAYLOAD + 4 * (points > 0 ? points : 0);
	for (i = 0; i <= count - 1; i++) text_item(&regions, "%lld", kp_path_u16(g, k, at + 2 * i));
	field_text(d, "point_list", &pts);
	field_text(d, "region_list", &regions);
	end(d);
}

static void list12(Text *t, const int64_t *v)
{
	int k;
	for (k = 0; k < 12; k++) text_item(t, "%lld", v[k]);
}

static void dump_lines(Dump *d, KpGame *g)
{
	static const char *const LIST_NAMES[KP_LIST_COUNT] = { "active", "pending", "free", "hidden", "special" };
	static const int UNIT_LISTS[4] = { KP_LIST_ACTIVE, KP_LIST_HIDDEN, KP_LIST_SPECIAL, KP_LIST_PENDING };
	KpUnits *u = &g->u;
	KpImages *w = g->w;
	KpOrders *o = &g->orders;
	KpTerrain *tr = &g->terrain;
	int k, p, r, l;
	int64_t n, i;
	begin(d, "global", "0");
	field_int(d, "rng", g->rng->state);
	field_int(d, "units_allocated", kp_scalar(g, KP_UNITS_ALLOCATED));
	field_int(d, "orders_allocated", o->allocated);
	field_int(d, "spatial_count", g->spatial.count);
	field_int(d, "active_tail", g->lists[KP_LIST_ACTIVE].tail);
	field_int(d, "pending_tail", g->lists[KP_LIST_PENDING].tail);
	field_int(d, "free_tail", g->lists[KP_LIST_FREE].tail);
	field_int(d, "hidden_tail", g->lists[KP_LIST_HIDDEN].tail);
	field_int(d, "special_tail", g->lists[KP_LIST_SPECIAL].tail);
	field_int(d, "cloaked_head", kp_scalar(g, KP_CLOAKED_HEAD));
	end(d);
	for (k = 0; k < KP_LIST_COUNT; k++) {
		Text t = text_new();
		CHAIN(t, g->lists[k].head, u->next, 0, KP_UNITS + 1);
		begin(d, "list", "%s", LIST_NAMES[k]);
		field_text(d, "units", &t);
		end(d);
	}
	for (p = 0; p < KP_PLAYERS; p++) {
		Text t = text_new();
		CHAIN(t, g->player_units[p], u->player_next, 0, KP_UNITS + 1);
		begin(d, "list", "player%d", p);
		field_text(d, "units", &t);
		end(d);
	}
	/* units, then their order queues */
	for (l = 0; l < 4; l++) {
		for (n = g->lists[UNIT_LISTS[l]].head; n > 0 && n <= KP_UNITS; n = u->next[n]) unit_line(d, g, n, LIST_NAMES[UNIT_LISTS[l]]);
	}
	for (l = 0; l < 4; l++) {
		for (n = g->lists[UNIT_LISTS[l]].head; n > 0 && n <= KP_UNITS; n = u->next[n]) {
			int64_t node = u->queue_head[n], q = 0;
			while (node != KP_NONE && node >= 0 && node < KP_ORDER_NODES) {
				begin(d, "order", "%lld.%lld", (long long)n, (long long)q++);
				field_int(d, "node", node);
				field_int(d, "order", o->order[node]);
				field_int(d, "type", o->type[node]);
				field_int(d, "x", o->x[node]);
				field_int(d, "y", o->y[node]);
				field_int(d, "unit", o->unit[node]);
				end(d);
				node = o->next[node];
			}
		}
	}
	{
		Text t = text_new();
		CHAIN(t, o->free.head, o->next, KP_NONE, KP_ORDER_NODES);
		begin(d, "order_free", "0");
		field_text(d, "nodes", &t);
		end(d);
	}
	for (l = 0; l < 4; l++) {
		for (n = g->lists[UNIT_LISTS[l]].head; n > 0 && n <= KP_UNITS; n = u->next[n]) {
			if (u->path[n] != 0) path_line(d, g, n, u->path[n]);
		}
	}
	{
		Text t = text_new();
		int64_t pk = kp_scalar(g, KP_PATH_FREE_HEAD);
		int guard = 0;
		while (pk != 0 && pk != KP_NIL && guard++ <= KP_PATH_RECORDS) {
			text_item(&t, "%lld", pk);
			pk = kp_path_u32(g, pk, 0);
		}
		begin(d, "path_free", "0");
		field_text(d, "records", &t);
		end(d);
	}
	{
		Text t = text_new();
		for (i = 0; i < KP_DENSITY_CELLS; i++) {
			if (g->collision.density[i] == 0) continue;
			if (t.len > 0) text_add(&t, ",");
			text_add(&t, "%lld:%lld", (long long)i, (long long)g->collision.density[i]);
		}
		begin(d, "density", "0");
		field_text(d, "cells", &t);
		end(d);
	}
	{
		/* sprites by row, then the images of all of them */
		int64_t *images = (int64_t *)malloc(sizeof *images * KP_IMAGES);
		int64_t count = 0;
		if (!images) {
			d->failed = 1;
			return;
		}
		for (r = 0; r < KP_ROWS; r++) {
			int32_t s = w->rows[r].head;
			int guard = 0;
			while (s != KP_NONE && s >= 0 && s < KP_SPRITES && guard++ <= KP_SPRITES) {
				Text ims = text_new();
				int32_t im = w->spr.images[s].head;
				int iguard = 0;
				while (im != KP_NONE && im >= 0 && im < KP_IMAGES && iguard++ <= KP_IMAGES) {
					text_item(&ims, "%lld", im);
					if (count < KP_IMAGES) images[count++] = im;
					im = w->img.next[im];
				}
				begin(d, "sprite", "%d", (int)s);
				field_int(d, "id", w->spr.id[s]);
				field_int(d, "owner", w->spr.owner[s]);
				field_int(d, "selection", w->spr.selection[s]);
				field_int(d, "visibility", w->spr.visibility[s]);
				field_int(d, "elevation", w->spr.elevation[s]);
				field_int(d, "flags", w->spr.flags[s]);
				field_int(d, "sel_timer", w->spr.sel_timer[s]);
				field_int(d, "index", w->spr.index[s]);
				field_int(d, "width", w->spr.width[s]);
				field_int(d, "height", w->spr.height[s]);
				field_int(d, "x", kp_s16v(w->spr.x[s]));
				field_int(d, "y", kp_s16v(w->spr.y[s]));
				field_int(d, "row", r);
				field_int(d, "main", w->spr.main[s]);
				field_text(d, "images", &ims);
				end(d);
				s = w->spr.next[s];
			}
		}
		for (i = 0; i < count; i++) {
			int32_t im = (int32_t)images[i];
			begin(d, "image", "%d", (int)im);
			field_int(d, "id", w->img.id[im]);
			field_int(d, "method", w->img.method[im]);
			field_int(d, "dir", w->img.dir[im]);
			field_int(d, "flags", w->img.flags[im]);
			field_int(d, "xoff", kp_s8((uint32_t)w->img.xoff[im]));
			field_int(d, "yoff", kp_s8((uint32_t)w->img.yoff[im]));
			field_int(d, "header", w->img.header[im]);
			field_int(d, "pc", w->img.pc[im]);
			field_int(d, "ret", w->img.ret[im]);
			field_int(d, "anim", w->img.anim[im]);
			field_int(d, "wait", w->img.wait[im]);
			field_int(d, "frameset", w->img.frameset[im]);
			field_int(d, "frame", w->img.frame[im]);
			field_int(d, "x", kp_s16v(w->img.x[im]));
			field_int(d, "y", kp_s16v(w->img.y[im]));
			field_int(d, "sprite", w->img.sprite[im]);
			end(d);
		}
		free(images);
	}
	{
		static const char *const NAMES[4] = { "all", "completed", "kills", "deaths" };
		const int64_t *counts[4];
		counts[0] = g->count_all;
		counts[1] = g->count_completed;
		counts[2] = g->count_kills;
		counts[3] = g->count_deaths;
		for (k = 0; k < 4; k++) {
			int t;
			for (t = 0; t < KP_UNIT_TYPES; t++) {
				int any = 0;
				for (p = 0; p < KP_PLAYERS; p++) {
					if (counts[k][t * 12 + p] != 0) any = 1;
				}
				if (any) {
					Text row = text_new();
					for (p = 0; p < KP_PLAYERS; p++) text_item(&row, "%lld", counts[k][t * 12 + p]);
					begin(d, "counter", "%s.%d", NAMES[k], t);
					field_text(d, "players", &row);
					end(d);
				}
			}
		}
	}
	for (k = 0; k < KP_PSTAT_ARRAYS; k++) {
		Text row = text_new();
		for (p = 0; p < KP_PLAYERS; p++) text_item(&row, "%lld", kp_mod(g->pstat[k][p], 4294967296LL));
		begin(d, "pstat", "0x%x", 0x5290c4 + 0x30 * k);
		field_text(d, "players", &row);
		end(d);
	}
	for (k = 0; k < 2; k++) {
		const int64_t *unit = k == 0 ? g->spatial.x_unit : g->spatial.y_unit, *at = k == 0 ? g->spatial.x_at : g->spatial.y_at;
		Text e = text_new();
		for (i = 0; i < g->spatial.count && i < KP_SPATIAL_CAPACITY; i++) {
			if (e.len > 0) text_add(&e, ",");
			text_add(&e, "%lld:%lld", (long long)unit[i], (long long)at[i]);
		}
		begin(d, "spatial", "%s", k == 0 ? "x" : "y");
		field_text(d, "entries", &e);
		end(d);
	}
	for (p = 0; p < KP_PLAYERS; p++) {
		Text a = text_new();
		int q;
		for (q = 0; q < KP_PLAYERS; q++) text_add(&a, "%02llx", (long long)g->players.alliance[p][q]);
		begin(d, "player", "%d", p);
		field_int(d, "controller", g->players.controller[p]);
		field_int(d, "force", g->players.force[p]);
		field_text(d, "alliance", &a);
		field_int(d, "shared_vision", g->players.shared_vision[p]);
		end(d);
	}
	{
		Text buckets = text_new();
		int64_t v;
		for (k = 0; k < KP_RECEDE_BUCKETS; k++) text_add(&buckets, "%02llx", (long long)tr->recede_buckets[k]);
		begin(d, "step", "0");
		v = kp_scalar(g, KP_TILE_COUNTDOWN);
		field_int(d, "tile_countdown", v == KP_NIL ? KP_NIL : kp_mod(v, 4294967296LL));
		field_int(d, "order_spread", kp_scalar(g, KP_ORDER_SPREAD));
		field_int(d, "secondary_spread", kp_scalar(g, KP_SECONDARY_SPREAD));
		field_int(d, "recede_timer", tr->recede_timer);
		field_int(d, "recede_free", tr->recede_free);
		field_int(d, "recede_retry", tr->recede_retry);
		field_text(d, "recede_buckets", &buckets);
		field_int(d, "ai_terrain_changed", g->ai.terrain_changed);
		field_int(d, "ai_period", kp_mod(g->ai.period, 4294967296LL));
		field_int(d, "ai_cycle", g->ai.cycle);
		field_int(d, "ai_rounds", g->ai.rounds);
		field_int(d, "ai_region_player", g->ai.region_player);
		field_int(d, "ai_region_cursor", g->ai.region_cursor);
		field_int(d, "ai_spending_player", g->ai.spending_player);
		field_int(d, "ai_town_player", g->ai.town_player);
		field_int(d, "hit_slot", kp_scalar(g, KP_HIT_SLOT));
		v = kp_scalar(g, KP_POWER_DIRTY);
		field_int(d, "power_dirty", v != KP_NIL && v != 0 ? 1 : 0);
		field_int(d, "elapsed", kp_scalar(g, KP_ELAPSED));
		field_int(d, "clock_second", g->trigger_clock.second);
		field_int(d, "clock_pass", g->trigger_clock.pass);
		field_int(d, "clock_result", g->trigger_clock.result);
		field_int(d, "clock_leaderboard", g->trigger_clock.leaderboard);
		field_int(d, "turn", kp_scalar(g, KP_TURN));
		end(d);
	}
	{
		Text t = text_new();
		list12(&t, g->local_selection);
		begin(d, "selection", "local");
		field_text(d, "units", &t);
		end(d);
	}
	for (p = 0; p < KP_SELECTION_PLAYERS; p++) {
		Text t = text_new(), stamps = text_new();
		begin(d, "selection", "%d", p);
		list12(&t, g->selections[p]);
		field_text(d, "units", &t);
		for (k = 0; k < KP_RECENT; k++) text_item(&stamps, "%lld", g->recent_stamps[p][k]);
		field_text(d, "recent", &stamps);
		for (k = 0; k < KP_GROUPS; k++) {
			int any = 0, j;
			for (j = 0; j < 12; j++) {
				if (g->groups[p][k][j] != 0) any = 1;
			}
			if (any) {
				char name[16];
				Text group = text_new();
				list12(&group, g->groups[p][k]);
				snprintf(name, sizeof name, "group%d", k);
				field_text(d, name, &group);
			}
		}
		end(d);
	}
	for (p = 0; p < 8; p++) {
		KpGuards *gd = &g->guards;
		Text recs = text_new();
		int64_t c = gd->heads[p];
		int guard = 0;
		while (c != -1 && c >= 0 && c < KP_GUARDS && guard++ <= KP_GUARDS) {
			if (recs.len > 0) text_add(&recs, ",");
			text_add(&recs, "%lld:", (long long)c);
			text_add(&recs, "%lld:", (long long)gd->kind[c]);
			text_add(&recs, "%lld:", (long long)gd->unit[c]);
			text_add(&recs, "%lld:", (long long)gd->type[c]);
			text_add(&recs, "%lld:", (long long)gd->x1[c]);
			text_add(&recs, "%lld:", (long long)gd->y1[c]);
			text_add(&recs, "%lld:", (long long)gd->x2[c]);
			text_add(&recs, "%lld", (long long)gd->y2[c]);
			c = gd->next[c];
		}
		begin(d, "guard", "%d", p);
		field_text(d, "records", &recs);
		end(d);
	}
	begin(d, "guard", "free");
	field_int(d, "head", g->guards.free);
	end(d);
	for (i = 0; i < g->map_h; i++) {
		Text row = text_new();
		int64_t x;
		for (x = 0; x < g->map_w; x++) text_add(&row, "%08llx", (long long)kp_mod(tr->tiles[i * g->map_w + x], 4294967296LL));
		begin(d, "tiles", "%lld", (long long)i);
		field_text(d, "flags", &row);
		end(d);
	}
	for (i = 0; i < g->map_h; i++) {
		Text mtxm = text_new(), backup = text_new(), border = text_new(), display = text_new();
		int64_t x;
		for (x = 0; x < g->map_w; x++) {
			int64_t at = i * g->map_w + x;
			text_add(&mtxm, "%04llx", (long long)tr->raw_tiles[at]);
			text_add(&backup, "%04llx", (long long)tr->creep_backup[at]);
			text_add(&border, "%02llx", (long long)tr->creep_border[at]);
			text_add(&display, "%04llx", (long long)tr->megatiles[at]);
		}
		begin(d, "terrain", "%lld", (long long)i);
		field_text(d, "mtxm", &mtxm);
		field_text(d, "backup", &backup);
		field_text(d, "border", &border);
		field_text(d, "display", &display);
		end(d);
	}
}

int kp_dump(KpGame *g, int drawn, KpDumpLine emit, void *ud)
{
	Dump *d = (Dump *)calloc(1, sizeof *d);
	int failed;
	if (!d) return -1;
	d->emit = emit;
	d->ud = ud;
	d->drawn = drawn;
	dump_lines(d, g);
	failed = d->failed;
	free(d->arena);
	free(d);
	return failed ? -1 : 0;
}

typedef struct {
	KpCategoryCrc each;
	void *ud;
	char category[32];
	char done[64][32]; /* categories already reported */
	int done_count;
	char *text;
	size_t size, cap;
	int failed;
} Crcs;

static void crcs_flush(Crcs *o)
{
	if (o->category[0] == '\0') return;
	o->each(o->ud, o->category, kp_crc32((const uint8_t *)o->text, o->size));
	if (o->done_count < 64) snprintf(o->done[o->done_count++], sizeof o->done[0], "%s", o->category);
	o->category[0] = '\0';
	o->size = 0;
}

static void crcs_line(void *ud, const char *line, size_t length)
{
	Crcs *o = (Crcs *)ud;
	size_t c = 0, need;
	int k;
	if (o->failed) return;
	while (c < length && line[c] != ' ') c++;
	if (c >= sizeof o->category) {
		o->failed = 1;
		return;
	}
	if (strncmp(o->category, line, c) != 0 || o->category[c] != '\0') {
		crcs_flush(o);
		memcpy(o->category, line, c);
		o->category[c] = '\0';
		for (k = 0; k < o->done_count; k++) {
			if (strcmp(o->done[k], o->category) == 0) o->failed = 1;
		}
	}
	need = o->size + (o->size ? 1 : 0) + length;
	if (need > o->cap) {
		size_t cap = o->cap ? o->cap : 65536;
		char *t;
		while (cap < need) cap *= 2;
		t = (char *)realloc(o->text, cap);
		if (!t) {
			o->failed = 1;
			return;
		}
		o->text = t;
		o->cap = cap;
	}
	if (o->size) o->text[o->size++] = '\n';
	memcpy(o->text + o->size, line, length);
	o->size += length;
}

int kp_dump_crcs(KpGame *g, int drawn, KpCategoryCrc each, void *ud)
{
	Crcs *o = (Crcs *)calloc(1, sizeof *o);
	int r;
	if (!o) return -1;
	o->each = each;
	o->ud = ud;
	r = kp_dump(g, drawn, crcs_line, o);
	if (r == 0 && !o->failed) crcs_flush(o);
	r = r != 0 || o->failed ? -1 : 0;
	free(o->text);
	free(o);
	return r;
}
