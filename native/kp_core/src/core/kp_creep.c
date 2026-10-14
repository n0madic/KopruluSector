/* The creep engine (kp_terrain.h): creep.cpp, atlas P42 "Creep
 * engine", P39 receding creep and callbacks; docs/research/native-trigger-creep.md. It rewrites MTXM tile words
 * (creep is CV5 group 1: `(word & 0x7ff0) == 0x10`), keeps the replaced word per tile (backup) and a border byte
 * per non-creep tile, and reports each change to the game's callbacks: the display megatile, the tile flags
 * (0x40000000 creep shown, walkability) and the receding-creep buckets. */
#include "kp_game_internal.h"

#define CREEP_GROUP 0x10
#define SHOWN 0x40000000
#define RECEDING 0x10000000
#define OCCUPIED 0x8000000
#define NO_CREEP 0x20000000
#define UNBUILDABLE 0x800000
#define MODE_ADD 0
#define MODE_REMOVE 1
#define MODE_BORDER 2
#define WALK_BITS 0x450000
#define WALKABLE 0x10000
#define UNWALKABLE 0x40000
#define CV5_RECORD 52
#define RANDOM_SPREAD 26
#define ELLIPSE 0x9c4000
#define RECEDE_NODES KP_RECEDE_NODES
#define FREE_BUCKET KP_RECEDE_BUCKETS
#define RECEDE_RETRY_PERIOD 3
#define RECEDE_RANDOM 27
#define EXTRACTOR 149
#define DAT_BUILDING 0x1
#define GROUP_ZERG 0x1
#define STATUS_GROUNDED_BUILDING 0x2
#define STATUS_COMPLETED 0x1
#define SUSTAIN_LEFT 624
#define SUSTAIN_RIGHT 656
#define SUSTAIN_TOP 384
#define SUSTAIN_BOTTOM 416
#define RECEDE_BUCKETS KP_RECEDE_BUCKETS
#define TWO32 4294967296LL

/* 0x50decc */
static const int64_t RECEDE_DELAY[RECEDE_BUCKETS] = { 1, 3, 5, 6, 7, 8, 9, 9, 9 };
/* Neighbour table 0x4f0fd8 (the tile itself last); M.NEIGHBOURS[k] is NEIGHBOURS[k - 1]. */
static const int64_t NEIGHBOURS[9][2] = { { 1, 1 }, { 0, 1 }, { -1, 1 }, { 1, 0 }, { -1, 0 }, { 1, -1 }, { 0, -1 },
	{ -1, -1 }, { 0, 0 } };
/* The neighbours of UpdateNeighborRecedingBuckets 0x4b4230. */
static const int64_t RECEDE_NEIGHBOURS[8][2] = { { -1, -1 }, { 0, -1 }, { 1, -1 }, { -1, 0 }, { 1, 0 }, { -1, 1 },
	{ 0, 1 }, { 1, 1 } };

static void remove_creep(KpGame *g, int64_t x, int64_t y);
static int64_t creep_neighbours(KpGame *g, int64_t x, int64_t y);

/* M.LARGE: Lair, Hive, Spore and Sunken Colony. */
static int is_large(int64_t t) { return t == 132 || t == 133 || t == 144 || t == 146; }
/* M.LARGE_COMPLETED: Hatchery, Creep Colony. */
static int is_large_completed(int64_t t) { return t == 131 || t == 143; }

/* BuildCreepShapeTable 0x4bff60 and BuildCreepShapeRanks 0x4c0010 (M.SHAPE, M.RANK), built on first use. */
static int64_t SHAPE[512], RANK[128];
static int shapes_built;

static int has_bit(int64_t u, int k) { return kp_band(u, (int64_t)1 << k) != 0; }

static void build_shapes(void)
{
	int64_t u, v, counter = 0;
	uint8_t present[128];
	for (u = 0; u <= 511; u++) {
		int64_t b;
		if (u >= 256) {
			b = 0x7f;
		} else {
			b = 0;
			if (has_bit(u, 4)) b = kp_bor(b, 9);
			if (has_bit(u, 3)) b = kp_bor(b, 0x24);
			if (has_bit(u, 6)) b = kp_bor(b, 2);
			if (has_bit(u, 1)) b = kp_bor(b, 0x10);
			if (has_bit(u, 6) && has_bit(u, 7)) b = kp_bor(b, 1);
			if (has_bit(u, 5) && has_bit(u, 6)) b = kp_bor(b, 4);
			if (has_bit(u, 1) && has_bit(u, 2)) b = kp_bor(b, 8);
			if (has_bit(u, 0) && has_bit(u, 1)) b = kp_bor(b, 0x20);
			if (kp_band(b, 0x21) == 0x21 || kp_band(b, 0xc) == 0xc) b = kp_bor(b, 0x40);
		}
		SHAPE[u] = b;
	}
	for (v = 0; v < 128; v++) present[v] = 0;
	for (u = 0; u <= 511; u++) present[SHAPE[u]] = 1;
	for (v = 0; v <= 127; v++) {
		RANK[v] = 0;
		if (present[v]) {
			RANK[v] = kp_mod(counter, 256);
			counter = counter + 1;
		}
	}
	shapes_built = 1;
}

/* The index x + map_w * y of a tile array; an index outside the arrays fails. */
static int64_t tile_index(KpGame *g, int64_t x, int64_t y)
{
	int64_t i = x + g->map_w * y;
	if (i < 0 || i >= KP_TILES) kp_game_fail(g, "creep: tile %lld (%lld, %lld) is nil", (long long)i, (long long)x, (long long)y);
	return i;
}

static int is_creep_word(int64_t word) { return kp_band(word, 0x7ff0) == CREEP_GROUP; }

static int in_map(KpGame *g, int64_t x, int64_t y) { return x >= 0 && x < g->map_w && y >= 0 && y < g->map_h; }

static int64_t *bucket_count(KpTerrain *t, int64_t b) { return b == FREE_BUCKET ? &t->recede_free : &t->recede_buckets[b]; }

static void recede_push(KpTerrain *t, int64_t node, int64_t b)
{
	int64_t head = t->recede_head[b];
	t->recede_prev[node] = 0;
	t->recede_next[node] = head;
	if (head != 0) t->recede_prev[head] = node;
	t->recede_head[b] = node;
	t->recede_bucket[node] = b;
	*bucket_count(t, b) += 1;
}

static void recede_unlink(KpTerrain *t, int64_t node)
{
	int64_t b = t->recede_bucket[node], prev = t->recede_prev[node], next = t->recede_next[node];
	if (prev != 0) t->recede_next[prev] = next;
	else t->recede_head[b] = next;
	if (next != 0) t->recede_prev[next] = prev;
	t->recede_next[node] = t->recede_prev[node] = 0;
	*bucket_count(t, b) -= 1;
}

void kp_creep_init(KpGame *g)
{
	/* InitCreepEngine 0x4c0690 (the zeroed backup and border maps; their entries outside the map stay 0). */
	KpTerrain *t = &g->terrain;
	int64_t i, b;
	for (i = 0; i < KP_TILES; i++) t->creep_backup[i] = t->creep_border[i] = 0;
	/* InitRecedingCreepPool 0x4b44a0: every node, in ascending order, at the front of the free list; empty buckets,
	 * both timers 0. */
	for (b = 0; b < RECEDE_BUCKETS; b++) t->recede_buckets[b] = 0;
	for (b = 0; b <= FREE_BUCKET; b++) t->recede_head[b] = 0;
	for (i = 0; i < KP_TILES; i++) t->recede_node[i] = 0;
	t->recede_free = 0;
	for (i = 1; i <= RECEDE_NODES; i++) recede_push(t, i, FREE_BUCKET);
	t->recede_timer = 0;
	t->recede_retry = 0;
}

/* RecedeCreepTick 0x4b4010: one receding tile goes per period, the period shrinking with the number of receding
 * tiles (0x50decc by free nodes / 128). */
void kp_creep_recede_tick(KpGame *g)
{
	KpTerrain *t = &g->terrain;
	int64_t b, k;
	if (t->recede_timer != 0) {
		t->recede_timer = t->recede_timer - 1;
		return;
	}
	k = kp_shrv(t->recede_free, 7);
	/* a delay index past the table fails */
	if (k < 0 || k >= RECEDE_BUCKETS) kp_game_fail(g, "creep: receding delay %lld is nil", (long long)k);
	t->recede_timer = RECEDE_DELAY[k];
	/* RecedeOneCreepTile 0x4b4a40: a random tile of the lowest non-empty bucket (the fewest creep neighbours) loses its
	 * creep and its node goes back to the free list. */
	for (b = 0; b < RECEDE_BUCKETS; b++) {
		int64_t node, x, y, i;
		if (t->recede_buckets[b] == 0) continue;
		k = kp_rng_game_range(g->rng, RECEDE_RANDOM, 0, (uint32_t)(t->recede_buckets[b] - 1));
		for (node = t->recede_head[b]; k > 0; k--) node = t->recede_next[node];
		x = t->recede_x[node];
		y = t->recede_y[node];
		i = tile_index(g, x, y);
		recede_unlink(t, node);
		t->recede_node[i] = 0;
		t->tiles[i] = kp_band(t->tiles[i], kp_bnot(RECEDING));
		remove_creep(g, x, y);
		recede_push(t, node, FREE_BUCKET);
		return;
	}
}

/* OnCreepTileChanged 0x4b4040 (callback 0). */
static void tile_changed(KpGame *g, int64_t x, int64_t y, int64_t mode, int64_t word)
{
	KpTerrain *t = &g->terrain;
	int64_t i = tile_index(g, x, y), f, group, at, cv5_flags, mega, terrain;
	if (mode == MODE_BORDER) {
		t->megatiles[i] = kp_bor(t->megatiles[i], 0x8000);
		return;
	}
	f = t->tiles[i];
	if (kp_band(f, g->local_vision) == 0) {
		t->megatiles[i] = kp_cv5_megatile(g, word);
		if (mode == MODE_ADD) f = kp_bor(f, SHOWN);
		else f = kp_band(f, kp_bnot(SHOWN));
	}
	group = kp_band(kp_shrv(word, 4), 0x7ff);
	at = group * CV5_RECORD + 2;
	if (t->cv5 == NULL || at < 0 || (size_t)at + 1 >= t->cv5_size)
		kp_game_fail(g, "creep: CV5 flags of group %lld are nil", (long long)group);
	cv5_flags = (int64_t)t->cv5[at] + (int64_t)t->cv5[at + 1] * 256;
	mega = kp_band(kp_cv5_megatile(g, word), 0x7fff);
	if (t->mflags == NULL || mega >= t->mflags_count) kp_game_fail(g, "creep: mflags[%lld] is nil", (long long)mega);
	terrain = kp_bor(kp_shlv(kp_band(cv5_flags, kp_bnot(5)), 16), t->mflags[mega]);
	f = kp_bor(kp_band(f, kp_bnot(WALK_BITS)), kp_band(terrain, WALK_BITS));
	if (kp_band(f, WALKABLE) != 0) f = kp_band(f, kp_bnot(UNWALKABLE));
	if (kp_band(f, WALK_BITS) == 0) f = kp_bor(f, UNWALKABLE);
	t->tiles[i] = f;
	kp_receding_neighbours(g, x, y);
}

/* UpdateNeighborRecedingBuckets 0x4b4230: a receding neighbour moves to the bucket of its new creep neighbour count. */
void kp_receding_neighbours(KpGame *g, int64_t x, int64_t y)
{
	int k;
	for (k = 0; k < 8; k++) {
		int64_t nx = x + RECEDE_NEIGHBOURS[k][0], ny = y + RECEDE_NEIGHBOURS[k][1];
		if (nx >= 0 && nx < g->map_w && ny >= 0 && ny < g->map_h) {
			int64_t j = tile_index(g, nx, ny);
			int64_t node = g->terrain.recede_node[j], count;
			if (!is_creep_word(g->terrain.raw_tiles[j]) || kp_band(g->terrain.tiles[j], RECEDING) == 0) continue;
			count = creep_neighbours(g, nx, ny);
			if (count != g->terrain.recede_bucket[node]) {
				recede_unlink(&g->terrain, node);
				recede_push(&g->terrain, node, count);
			}
		}
	}
}

/* ApplyCreepTileChange 0x4c0ca0 with the three mutators (0x4c0c40 add, 0x4c0bc0 remove, 0x4c0d90 border). */
void kp_creep_apply(KpGame *g, int64_t x, int64_t y, int64_t mode, int64_t arg)
{
	KpTerrain *t = &g->terrain;
	int64_t i = tile_index(g, x, y);
	if (mode == MODE_ADD) {
		int64_t variant;
		if (t->creep_backup[i] == 0) t->creep_backup[i] = t->raw_tiles[i];
		if (g->crt == NULL) kp_game_fail(g, "creep: the game has no crt generator");
		if ((int64_t)kp_rng_crt_rand(g->crt) % 100 < 4) {
			variant = (int64_t)kp_rng_crt_rand(g->crt) % 7 + 6;
		} else {
			variant = (int64_t)kp_rng_crt_rand(g->crt) % 6;
		}
		t->raw_tiles[i] = CREEP_GROUP + variant;
	} else if (mode == MODE_REMOVE) {
		if (t->creep_backup[i] == 0) return;
		t->raw_tiles[i] = t->creep_backup[i];
		t->creep_backup[i] = 0;
	} else {
		if (!shapes_built) build_shapes();
		t->creep_border[i] = RANK[SHAPE[kp_band(arg, 0xff)]];
	}
	if (t->has_terrain_changed) t->terrain_changed[i] = 1;
	tile_changed(g, x, y, mode, t->raw_tiles[i]);
}

/* TileShowsCreep 0x4b45f0 (callback 1) and IsTileHiddenFromViewer 0x4b4560 (callback 2). */
static int shows_creep(KpGame *g, int64_t x, int64_t y)
{
	return kp_band(g->terrain.tiles[tile_index(g, x, y)], SHOWN) != 0;
}

static int hidden_from_viewer(KpGame *g, int64_t x, int64_t y)
{
	return kp_band(g->terrain.tiles[tile_index(g, x, y)], g->local_vision) != 0;
}

/* ForEachCreepNeighbor 0x4c03f0 with RefreshCreepBorderAt 0x4c0470 (and the neighbour mask of 0x4c0e00):
 * non-creep tiles around (x, y) get their border shape. */
void kp_creep_refresh_borders(KpGame *g, int64_t x, int64_t y)
{
	int d, k;
	for (d = 0; d < 9; d++) {
		int64_t nx = x + NEIGHBOURS[d][0], ny = y + NEIGHBOURS[d][1];
		if (in_map(g, nx, ny) && !shows_creep(g, nx, ny)) {
			int64_t mask = 0;
			for (k = 1; k <= 9; k++) {
				int64_t mx = nx + NEIGHBOURS[k - 1][0], my = ny + NEIGHBOURS[k - 1][1];
				if (k <= 8 && in_map(g, mx, my) && shows_creep(g, mx, my)) mask = kp_bor(mask, (int64_t)1 << (k - 1));
			}
			kp_creep_apply(g, nx, ny, MODE_BORDER, mask);
		}
	}
}

/* RemoveCreepAt 0x4c0c00: the remove mutator and, for seen tiles, the borders around it. */
static void remove_creep(KpGame *g, int64_t x, int64_t y)
{
	kp_creep_apply(g, x, y, MODE_REMOVE, 0);
	if (!hidden_from_viewer(g, x, y)) kp_creep_refresh_borders(g, x, y);
}

/* AddCreepAt: the add mutator and, for seen tiles, the borders around it. */
static void add_creep(KpGame *g, int64_t x, int64_t y)
{
	kp_creep_apply(g, x, y, MODE_ADD, 0);
	if (!hidden_from_viewer(g, x, y)) kp_creep_refresh_borders(g, x, y);
}

/* TileCannotHaveCreep 0x4b45b0. */
int kp_tile_cannot_have_creep(KpGame *g, int64_t x, int64_t y)
{
	int64_t f = g->terrain.tiles[tile_index(g, x, y)];
	if (kp_band(f, kp_bor(NO_CREEP, UNBUILDABLE)) != 0) return 1;
	if (kp_mod(y + 1, 65536) == kp_mod(g->map_h, 65536)) return 0;
	return kp_band(g->terrain.tiles[tile_index(g, x, y + 1)], UNBUILDABLE) != 0;
}

/* staredit_placement_size[k] as int.s16 takes it (a missing value fails). */
static int64_t placement_size(KpGame *g, int64_t t, int k)
{
	int64_t v = KP_DAT(g, units, staredit_placement_size, t * 2 + k);
	if (v == KP_NIL) kp_game_fail(g, "creep: staredit_placement_size[%lld] is nil", (long long)(t * 2 + k));
	return kp_s16v(v);
}

/* GetCreepAreaBounds 0x4c04f0: tile bounds and whether the area is large. */
int kp_creep_bounds(KpGame *g, int64_t t, int64_t cx, int64_t cy, int completed, int64_t *left, int64_t *top,
	int64_t *right, int64_t *bottom)
{
	int large = is_large(t) || (is_large_completed(t) && completed);
	int64_t l, tp, r, b;
	if (large) {
		l = kp_idiv64(g, cx - 320, 32);
		r = kp_idiv64(g, cx + 320, 32);
		tp = kp_idiv64(g, cy - 200, 32);
		b = kp_idiv64(g, cy + 200, 32);
	} else {
		int64_t w = kp_idiv64(g, placement_size(g, t, 0), 32), h = kp_idiv64(g, placement_size(g, t, 1), 32);
		l = kp_s16v(kp_idiv64(g, cx, 32)) - kp_idiv64(g, w, 2);
		r = l + w - 1;
		tp = kp_s16v(kp_idiv64(g, cy, 32)) - kp_idiv64(g, h, 2);
		b = tp + h - 1;
	}
	if (l < 0) l = 0;
	else if (r >= g->map_w) r = g->map_w - 1;
	if (tp < 0) tp = 0;
	else if (b >= g->map_h) b = g->map_h - 1;
	*left = l;
	*top = tp;
	*right = r;
	*bottom = b;
	return large;
}

static int outside_ellipse(int64_t dx, int64_t dy)
{
	return kp_s32v(kp_mulv(kp_mulv(dx, dx), 100) + kp_mulv(kp_mulv(dy, dy), 256)) > ELLIPSE;
}

/* CountCreepNeighbors 0x4c0060. */
static int64_t creep_neighbours(KpGame *g, int64_t x, int64_t y)
{
	int64_t n = 0;
	int k;
	for (k = 0; k < 8; k++) {
		int64_t nx = kp_mod(x + NEIGHBOURS[k][0], TWO32), ny = kp_mod(y + NEIGHBOURS[k][1], TWO32);
		if (nx < g->map_w && ny < g->map_h && is_creep_word(g->terrain.raw_tiles[tile_index(g, nx, ny)])) n = n + 1;
	}
	return n;
}

/* The spread candidate test of SpreadCreepOnce for tile (x, y): its bucket 1..8, or 0 when it is no candidate
 * (*occupied set when an occupied tile was seen). Reads only. */
static int64_t spread_bucket(KpGame *g, int64_t x, int64_t y, int large, int64_t cx, int64_t cy, int *occupied)
{
	int64_t i = tile_index(g, x, y);
	if (!is_creep_word(g->terrain.raw_tiles[i]) && !kp_tile_cannot_have_creep(g, x, y)) {
		if (kp_band(g->terrain.tiles[i], OCCUPIED) != 0) {
			*occupied = 1;
		} else {
			int64_t n = creep_neighbours(g, x, y);
			if (n >= 1 && n <= 8 && !(large && outside_ellipse(32 * x - cx + 16, 32 * y - cy + 16))) return n;
		}
	}
	return 0;
}

/* SpreadCreepOnce 0x4c0e60: 1 when a tile was added, 2 when only occupied candidates were seen, else 0.
 * The candidates fall into eight buckets; instead of collecting them in lists, this scans the area twice (the scan only
 * reads, so the second pass sees the same candidates in the same order): first counting the buckets, then
 * finding the drawn candidate of the highest non-empty bucket. */
int64_t kp_creep_spread_once(KpGame *g, int64_t t, int64_t cx, int64_t cy)
{
	int64_t left, top, right, bottom, x, y, b, r, pick, seen;
	int64_t count[9];
	int occupied = 0, large = kp_creep_bounds(g, t, cx, cy, 1, &left, &top, &right, &bottom);
	for (b = 0; b <= 8; b++) count[b] = 0;
	for (y = top; y <= bottom; y++) {
		for (x = left; x <= right; x++) count[spread_bucket(g, x, y, large, cx, cy, &occupied)]++;
	}
	for (b = 8; b >= 1; b--) {
		if (count[b] > 0) {
			r = (int64_t)kp_rng_game_raw(g->rng, RANDOM_SPREAD);
			pick = kp_urem64(g, kp_shrv(r, 4), count[b]);
			seen = 0;
			for (y = top; y <= bottom; y++) {
				for (x = left; x <= right; x++) {
					int dummy = 0;
					if (spread_bucket(g, x, y, large, cx, cy, &dummy) == b) {
						if (seen == pick) {
							add_creep(g, x, y);
							return 1;
						}
						seen++;
					}
				}
			}
			kp_game_fail(g, "creep: spread candidate %lld of bucket %lld is nil", (long long)pick, (long long)b);
		}
	}
	return occupied ? 2 : 0;
}

/* PlaceInitialCreep 0x4c0aa0. */
void kp_creep_place_initial(KpGame *g, int64_t t, int64_t cx, int64_t cy)
{
	int64_t w = kp_idiv64(g, placement_size(g, t, 0), 32), h = kp_idiv64(g, placement_size(g, t, 1), 32);
	int64_t left = kp_idiv64(g, cx, 32) - kp_idiv64(g, w, 2);
	int64_t top = kp_idiv64(g, cy, 32) - kp_idiv64(g, h, 2);
	int64_t x1 = kp_mod(left + w, TWO32), y1 = kp_mod(top + h, TWO32);
	int64_t y = kp_mod(top, TWO32);
	while (y < y1) {
		int64_t x = kp_mod(left, TWO32);
		while (x < x1) {
			if (!kp_tile_cannot_have_creep(g, x, y)) add_creep(g, x, y);
			x = x + 1;
		}
		y = y + 1;
	}
	while (kp_creep_spread_once(g, t, cx, cy) == 1) {
	}
}

/* AddRecedingCreepTile 0x4b3f30: the tile gets a node from the free list in the bucket of its creep neighbour count;
 * 0 when the pool is empty. */
static int add_receding(KpGame *g, int64_t x, int64_t y)
{
	KpTerrain *t = &g->terrain;
	int64_t node = t->recede_head[FREE_BUCKET], i = tile_index(g, x, y);
	if (t->recede_free == 0) return 0;
	recede_unlink(t, node);
	t->recede_x[node] = kp_band(x, 0xff);
	t->recede_y[node] = kp_band(y, 0xff);
	recede_push(t, node, creep_neighbours(g, x, y));
	t->recede_node[i] = node;
	t->tiles[i] = kp_bor(t->tiles[i], RECEDING);
	return 1;
}

/* CancelRecedingTile 0x4b4930: the tile keeps its creep; 0 when it was not receding. */
int kp_creep_cancel_receding(KpGame *g, int64_t x, int64_t y)
{
	KpTerrain *t = &g->terrain;
	int64_t i = tile_index(g, x, y), node = t->recede_node[i];
	if (node == 0) return 0;
	recede_unlink(t, node);
	t->recede_node[i] = 0;
	recede_push(t, node, FREE_BUCKET);
	t->tiles[i] = kp_band(t->tiles[i], kp_bnot(RECEDING));
	return 1;
}

/* 0x4c0830 for grounded unit c: a Zerg building whose creep area (always within its ellipse) holds tile (x, y). */
static int keeps_creep(KpGame *g, int64_t c, int64_t x, int64_t y)
{
	KpUnits *u = &g->u;
	int64_t type = u->type[c], left, top, right, bottom, sx, sy;
	int32_t s;
	if (kp_band(KP_DAT(g, units, special_ability_flags, type), DAT_BUILDING) == 0) return 0;
	if (kp_band(KP_DAT(g, units, staredit_group_flags, type), GROUP_ZERG) == 0) return 0;
	s = kp_unit_sprite(g, c);
	sx = kp_s16v(g->w->spr.x[s]);
	sy = kp_s16v(g->w->spr.y[s]);
	kp_creep_bounds(g, type, sx, sy, kp_band(u->status[c], STATUS_COMPLETED) != 0, &left, &top, &right, &bottom);
	if (x < left || x > right || y < top || y > bottom) return 0;
	return !outside_ellipse(32 * x - sx + 16, 32 * y - sy + 16);
}

/* 0x4c0250 with IsTileSustainedByUnits 0x4b4170: a grounded Zerg building of any owner near tile (x, y) keeps it. */
static int sustained(KpGame *g, int64_t x, int64_t y)
{
	int64_t left = 32 * x - SUSTAIN_LEFT, right = 32 * x + SUSTAIN_RIGHT, top = 32 * y - SUSTAIN_TOP, bottom = 32 * y + SUSTAIN_BOTTOM, *found;
	int count, k, any = 0;
	if (left < 0) left = 0;
	else if (right >= g->map_w * 32) right = g->map_w * 32 - 1;
	if (top < 0) top = 0;
	else if (bottom >= g->map_h * 32) bottom = g->map_h * 32 - 1;
	found = kp_query_rect(g, kp_s16v(left), kp_s16v(top), kp_s16v(right), kp_s16v(bottom), &count);
	for (k = 0; k < count && !any; k++) {
		any = kp_band(g->u.status[found[k]], STATUS_GROUNDED_BUILDING) != 0 && keeps_creep(g, found[k], x, y);
	}
	kp_query_release(g, found);
	return any;
}

/* RemoveCreepProviderAt 0x4b46e0: the creep of a provider of type t at (cx, cy) that nothing else keeps starts to
 * recede; 0 when the pool runs out. */
int kp_creep_remove_provider(KpGame *g, int64_t t, int64_t cx, int64_t cy, int completed)
{
	KpTerrain *tr = &g->terrain;
	int64_t left, top, right, bottom, x, y, skip = t == EXTRACTOR ? RECEDING : RECEDING | OCCUPIED;
	int large = kp_creep_bounds(g, t, cx, cy, completed, &left, &top, &right, &bottom);
	for (y = top; y <= bottom; y++) {
		for (x = left; x <= right; x++) {
			int64_t i = tile_index(g, x, y);
			if (!is_creep_word(tr->raw_tiles[i])) continue;
			if (large && outside_ellipse(32 * x - cx + 16, 32 * y - cy + 16)) continue;
			if (kp_band(tr->tiles[i], skip) != 0 || sustained(g, x, y)) continue;
			if (!add_receding(g, x, y)) return 0;
		}
	}
	tr->recede_retry = 0;
	return 1;
}


/* RetryDeadProviderCreep 0x4b4a10: every fourth call after a failure, dead provider n tries to give up its creep again;
 * 1 when it did. */
int kp_creep_retry_provider(KpGame *g, int64_t n)
{
	KpTerrain *t = &g->terrain;
	KpUnits *u = &g->u;
	if (t->recede_retry != 0) {
		t->recede_retry = t->recede_retry - 1;
		return 0;
	}
	t->recede_retry = RECEDE_RETRY_PERIOD;
	return kp_creep_remove_provider(g, u->type[n], u->x[n], u->y[n], kp_band(u->status[n], STATUS_COMPLETED) != 0);
}

/* RemoveUnsustainedCreep 0x4c0960 with the callback 0x4265d0, which keeps nothing: the creep of type t's area at
 * (cx, cy) goes at once. */
void kp_creep_clear_area(KpGame *g, int64_t t, int64_t cx, int64_t cy)
{
	int64_t left, top, right, bottom, x, y;
	int large = kp_creep_bounds(g, t, cx, cy, 1, &left, &top, &right, &bottom);
	for (y = top; y <= bottom; y++) {
		for (x = left; x <= right; x++) {
			if (!is_creep_word(g->terrain.raw_tiles[tile_index(g, x, y)])) continue;
			if (large && outside_ellipse(32 * x - cx + 16, 32 * y - cy + 16)) continue;
			remove_creep(g, x, y);
		}
	}
}
