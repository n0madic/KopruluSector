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
#define RECEDE_NODES 1024
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

void kp_creep_init(KpGame *g)
{
	/* InitCreepEngine 0x4c0690 (the zeroed backup and border maps; their entries outside the map stay 0). */
	KpTerrain *t = &g->terrain;
	int64_t i, b;
	for (i = 0; i < KP_TILES; i++) t->creep_backup[i] = t->creep_border[i] = 0;
	/* InitRecedingCreepPool 0x4b44a0: 1024 free nodes, empty buckets, both timers 0. */
	for (b = 0; b < RECEDE_BUCKETS; b++) t->recede_buckets[b] = 0;
	t->recede_timer = 0;
	t->recede_free = RECEDE_NODES;
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
	/* RecedeOneCreepTile 0x4b4a40 picks a tile of the lowest non-empty bucket. */
	for (b = 0; b < RECEDE_BUCKETS; b++) {
		if (t->recede_buckets[b] != 0) kp_game_fail(g, "creep: receding creep tile (0x4b4a40)");
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

/* UpdateNeighborRecedingBuckets 0x4b4230: only receding tiles (none before the pool is used) have bucket nodes. */
void kp_receding_neighbours(KpGame *g, int64_t x, int64_t y)
{
	int k;
	for (k = 0; k < 8; k++) {
		int64_t nx = x + RECEDE_NEIGHBOURS[k][0], ny = y + RECEDE_NEIGHBOURS[k][1];
		if (nx >= 0 && nx < g->map_w && ny >= 0 && ny < g->map_h) {
			int64_t j = tile_index(g, nx, ny);
			if (is_creep_word(g->terrain.raw_tiles[j]) && kp_band(g->terrain.tiles[j], RECEDING) != 0)
				kp_game_fail(g, "creep: receding creep buckets (0x4b4230)");
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
