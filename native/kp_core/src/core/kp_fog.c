/* Tile flags and line of sight of the C game (kp_terrain.h; los.cpp, mask.cpp).
 *
 * g->terrain.tiles[ty * map_w + tx] is the tile flag DWORD of 0x666454: byte 0 = players that do not see the
 * tile, byte 1 = players that have not explored it, high bits from the terrain. */
#include "kp_game_internal.h"

#define TWO32 4294967296LL
#define INNER 3
#define CARGO_VISION 0x08
#define NUKE 14
#define AIR 0x4
#define ALL_PLAYERS 0xff
#define VISION_CHEATS 0x4001
#define SPRITE_SELECTED 0x08
#define SPRITE_DASHED 0x06
#define DOODAD_FIRST 0xcb
#define DOODAD_LAST 0xd5
#define OVERLORD_TRANSPORT 0x18
#define VISION_RECORDS_MAX KP_SIGHT_RECORDS

static const int64_t HEIGHT_REQUIRED[3] = { 0x07000000, 0x05000000, 0x01000000 }; /* by source height */
static const int64_t DIRECTIONS[4][2] = { { 1, 0 }, { 0, 1 }, { -1, 0 }, { 0, -1 } }; /* 0x4ee644/0x4ee648 */

static int64_t *tile_ref(KpGame *g, int64_t i)
{
	if (i < 0 || i >= KP_TILES) kp_game_fail(g, "fog: tile %lld is nil", (long long)i);
	return &g->terrain.tiles[i];
}

/* GenerateVisionGrid 0x466990: the cells of a w x h grid inside the sight shape (the inner rectangle and the
 * integer circle of radius w / 2). Fills marks[0..w*h-1]; returns their count. */
static int64_t vision_grid(KpGame *g, int64_t w, int64_t h, int64_t iw, int64_t ih, uint8_t *marks)
{
	int64_t k, pos, r, y0, left, right, top, bottom, err, threshold, step, step2, i, limit, count = 0, y, x;
	for (k = 0; k < w * h; k++) marks[k] = 0;
	pos = kp_idiv64(g, (h - ih) * w, 2) + kp_idiv64(g, w - iw, 2);
	for (y = 1; y <= ih; y++) {
		for (x = 1; x <= iw; x++) {
			marks[pos] = 1;
			pos = pos + 1;
		}
		pos = pos + (w - iw);
	}
	r = kp_idiv64(g, w, 2);
	y0 = kp_idiv64(g, h, 2) - r;
	left = 0;
	right = r * 2;
	top = r;
	bottom = r;
	err = r * r;
	threshold = (r - 1) * r; /* local_20, local_24 (unsigned) */
	step = 0;
	step2 = r * 2; /* local_1c, local_2c */
	i = 0;
	limit = r; /* local_28, local_18 */
#define MARK(cell) \
	do { \
		int64_t c_ = (cell); \
		if (c_ < 0 || c_ >= w * h) kp_game_fail(g, "fog: vision grid cell %lld", (long long)c_); \
		marks[c_] = 1; \
	} while (0)
	do {
		if (left <= right) {
			for (x = left; x <= right; x++) MARK((y0 + top) * w + x);
			for (x = left; x <= right; x++) MARK((y0 + bottom) * w + x);
		}
		if (top <= bottom) {
			for (y = top; y <= bottom; y++) MARK((y0 + left) * w + y);
			for (y = top; y <= bottom; y++) MARK((y0 + right) * w + y);
		}
		step = step + 2;
		bottom = bottom + 1;
		err = kp_mod(err + 1 - step, TWO32);
		top = top - 1;
		i = i + 1;
		if (err <= threshold) {
			limit = limit - 1;
			step2 = step2 - 2;
			right = right - 1;
			left = left + 1;
			threshold = kp_mod(threshold - step2, TWO32);
		}
	} while (!(i > limit));
#undef MARK
	for (k = 0; k < w * h; k++) {
		if (marks[k]) count++;
	}
	return count;
}

/* GenerateVisionRecords 0x466bd0: records in rings around the centre (directions right, down, left, up), each
 * with its tile offset for `stride` and one or two earlier records on the way back to the centre. */
static void vision_records(KpGame *g, KpVisionTemplate *t, int64_t w, int64_t h, const uint8_t *marks, int64_t total,
	int64_t stride)
{
	int64_t cell[VISION_RECORDS_MAX]; /* grid cell -> 1-based record index (0 = none) */
	int64_t count = 1, side = 2, x = -1, y = -1, lens[5], records = 1, k;
	for (k = 0; k < w * h; k++) cell[k] = 0;
#define AT(ax, ay) ((kp_idiv64(g, h, 2) + (ay)) * w + kp_idiv64(g, w, 2) + (ax))
#define CELL(c) ((c) >= 0 && (c) < w * h ? cell[c] : 0)
	t->off[0] = t->dx[0] = t->dy[0] = t->dep1[0] = t->dep2[0] = 0;
	cell[AT(0, 0)] = 1;
	lens[0] = h;
	lens[1] = w;
	lens[2] = h;
	lens[3] = w;
	lens[4] = h;
	while (count < total) {
		for (k = 0; k <= 3; k++) {
			int64_t dx = DIRECTIONS[k][0], dy = DIRECTIONS[k][1], n;
			int skip_first = 0;
			if (side <= lens[k]) {
				y = y + dy;
				n = lens[k + 1] - 1;
				if (n > side) n = side;
				skip_first = 1;
			} else {
				x = x + dx * side;
				y = y + dy * side;
				n = 1;
			}
			do {
				if (skip_first) {
					skip_first = 0;
				} else {
					int64_t c = AT(x, y);
					if (c >= 0 && c < w * h && marks[c]) {
						int64_t at = records;
						if (records >= VISION_RECORDS_MAX) kp_game_fail(g, "fog: too many vision records");
						t->dx[at] = x;
						t->dy[at] = y;
						t->off[at] = y * stride + x;
						t->dep1[at] = t->dep2[at] = 0;
						if (x == 0 && y == 0) kp_game_fail(g, "fog: a vision record without dependencies");
						{
							int64_t ax = x == 0 ? 0 : (x > 0 ? x - 1 : x + 1);
							int64_t ay = y == 0 ? 0 : (y > 0 ? y - 1 : y + 1);
							int64_t mx = ax < 0 ? -ax : ax, my = ay < 0 ? -ay : ay;
							if (mx == my || (x == 0 && dx != 0) || (y == 0 && dy != 0)) {
								t->dep1[at] = CELL(AT(ax, ay));
							} else {
								int64_t first = CELL(AT(ax, ay));
								if (my < mx) ay = ay + (y < 0 ? -1 : 1);
								else ax = ax + (x < 0 ? -1 : 1);
								t->dep1[at] = first;
								t->dep2[at] = CELL(AT(ax, ay));
							}
						}
						records++;
						cell[c] = records;
						count = count + 1;
					}
					y = y + dy;
					n = n - 1;
				}
				x = x + dx;
			} while (n != 0);
			x = x - dx;
			y = y - dy;
		}
		if (side < w - 1) x = x - 1;
		if (side < h - 1) y = y - 1;
		side = side + 2;
	}
#undef CELL
#undef AT
}

/* Templates for a map stride (0x466e40 over the descriptors 0x508a50: w = h = 3 + 2 sight). */
static void templates(KpGame *g, int64_t stride)
{
	int64_t sight;
	uint8_t marks[VISION_RECORDS_MAX];
	for (sight = 0; sight < KP_SIGHTS; sight++) {
		KpVisionTemplate *t = &g->terrain.templates[sight];
		int64_t w = 3 + 2 * sight, total, k;
		total = vision_grid(g, w, w, INNER, INNER, marks);
		vision_records(g, t, w, w, marks, total, stride);
		t->width = t->height = w;
		t->inner = INNER * INNER;
		t->outer = total - t->inner;
		for (k = 0; k < total; k++) t->snap[k] = 0xff;
	}
	g->terrain.has_templates = 1;
}

/* InitFogOfWar 0x46c1e0 (tile part): every tile unseen and unexplored. */
void kp_fog_init(KpGame *g)
{
	int64_t i;
	for (i = 0; i < KP_TILES; i++) g->terrain.tiles[i] = kp_bor(g->terrain.tiles[i], 0xffff);
	templates(g, g->map_w);
}

/* SetTileFlagsRect 0x412c40 / ClearTileFlagsRect 0x4134a0. */
void kp_tiles_or(KpGame *g, int64_t tx, int64_t ty, int64_t w, int64_t h, int64_t mask)
{
	int64_t x, y;
	for (y = ty; y <= ty + h - 1; y++) {
		for (x = tx; x <= tx + w - 1; x++) {
			int64_t *f = tile_ref(g, y * g->map_w + x);
			*f = kp_bor(*f, mask);
		}
	}
}

void kp_tiles_clear(KpGame *g, int64_t tx, int64_t ty, int64_t w, int64_t h, int64_t mask)
{
	int64_t x, y, keep = kp_bnot(mask);
	for (y = ty; y <= ty + h - 1; y++) {
		for (x = tx; x <= tx + w - 1; x++) {
			int64_t *f = tile_ref(g, y * g->map_w + x);
			*f = kp_band(*f, keep);
		}
	}
}

/* GetTerrainHeightAt 0x445690: 2 high, 1 mid, 0 low from the mini-tile flags. */
int64_t kp_terrain_height(KpGame *g, int64_t x, int64_t y)
{
	int64_t tx = kp_sarv(x, 5), ty = kp_sarv(y, 5), i = g->map_w * ty + tx;
	int64_t mega = kp_megatile_at(g, i);
	int64_t flags = kp_minitile_flags(g, mega, kp_band(kp_sarv(x, 3), 3), kp_band(kp_sarv(y, 3), 3));
	if (kp_band(flags, 4) != 0) return 2;
	return kp_band(kp_shrv(flags, 1), 1);
}

static void clear_tile(KpGame *g, int64_t i, int64_t bits)
{
	int64_t *f = tile_ref(g, i);
	*f = *f - kp_band(*f, bits);
}

/* RevealSightArea 0x46bbb0 with the four callbacks 0x46c620 (ground), 0x46c570 (air), 0x46c6a0 (ground,
 * clipped) and 0x46c5b0 (air, clipped). A clipped reveal skips the records outside the map (unsigned
 * compares). */
void kp_reveal_sight(KpGame *g, int64_t x, int64_t y, int air, int64_t mask, int64_t sight)
{
	KpVisionTemplate *t;
	int64_t bits, height, tx, ty, hw, hh, w = g->map_w, h = g->map_h, base, k;
	int clipped;
	/* An empty mask clears nothing (the dependency tests decide only which tiles). */
	if (mask >= 0x100 || mask == 0) return;
	bits = mask * 257; /* the mask's seen and explored bits */
	height = air ? 3 : kp_terrain_height(g, x, y);
	tx = kp_sarv(x, 5);
	ty = kp_sarv(y, 5);
	if (!g->terrain.has_templates || sight < 0 || sight >= KP_SIGHTS) {
		kp_game_fail(g, "fog: no vision template %lld", (long long)sight);
	}
	t = &g->terrain.templates[sight];
	hw = kp_idiv64(g, t->width, 2);
	hh = kp_idiv64(g, t->height, 2);
	clipped = tx - hw < 0 || w <= tx + hw || ty - hh < 0 || h <= ty + hh;
	base = w * ty + tx;
#define INSIDE(k) (!clipped || (kp_mod(t->dx[k] + tx, TWO32) < w && kp_mod(t->dy[k] + ty, TWO32) < h))
	if (air) {
		for (k = 0; k < t->outer; k++) {
			if (INSIDE(k)) clear_tile(g, base + t->off[k], bits);
		}
		return;
	}
	{
		/* snap holds the value each record's tile is left with (its own bits cleared). */
		int64_t required = kp_bor(mask, HEIGHT_REQUIRED[height]), *snap = t->snap;
		for (k = 0; k < t->inner; k++) {
			int64_t v = 0xff;
			if (INSIDE(k)) {
				int64_t f = *tile_ref(g, base + t->off[k]);
				v = f - kp_band(f, bits);
				clear_tile(g, base + t->off[k], bits);
			}
			snap[k] = v;
		}
		/* An outer record is seen when one of its dependencies (the second checked first) left none of the
		 * required bits. */
		for (k = t->inner; k < t->inner + t->outer; k++) {
			int64_t v = 0xff;
			if (INSIDE(k)) {
				int64_t d1 = t->dep1[k], d2 = t->dep2[k];
				if (d1 == 0) kp_game_fail(g, "fog: vision record %lld without a dependency", (long long)k + 1);
				if ((d2 != 0 && kp_band(snap[d2 - 1], required) == 0) || kp_band(snap[d1 - 1], required) == 0) {
					int64_t f = *tile_ref(g, base + t->off[k]);
					v = f - kp_band(f, bits);
					clear_tile(g, base + t->off[k], bits);
				}
			}
			snap[k] = v;
		}
	}
#undef INSIDE
}

#define CREEP_GROUP 0x10
#define CREEP_SHOWN 0x40000000

/* The remembered terrain of tile i the viewer sees again: its megatile, the creep shown and the creep borders around
 * it (0x4c0ba0); 1 when it changed. */
static int refresh_tile(KpGame *g, int64_t i)
{
	KpTerrain *t = &g->terrain;
	int64_t m;
	if (kp_band(t->tiles[i], g->local_vision) != 0) return 0;
	m = kp_cv5_megatile(g, t->raw_tiles[i]);
	if (kp_band(t->megatiles[i], 0x7fff) == m) return 0;
	t->megatiles[i] = m;
	if (kp_band(t->raw_tiles[i], 0x7ff0) == CREEP_GROUP) t->tiles[i] = kp_bor(t->tiles[i], CREEP_SHOWN);
	else t->tiles[i] = kp_band(t->tiles[i], kp_bnot(CREEP_SHOWN));
	kp_creep_refresh_borders(g, kp_mod(i, g->map_w), i / g->map_w);
	return 1;
}

static int64_t screen(KpGame *g, KpScalar s)
{
	int64_t v = kp_scalar(g, s);
	return v == KP_NIL ? 0 : v;
}

/* RefreshRememberedTerrainOffscreen 0x40af30 (every 100 steps): the tiles the viewer sees outside the screen's tiles
 * show what they have become (only creep makes the remembered and the current megatiles differ). */
void kp_refresh_remembered_terrain(KpGame *g)
{
	KpTerrain *t = &g->terrain;
	int64_t i, count = g->map_w * g->map_h, sx = screen(g, KP_SCREEN_X), sy = screen(g, KP_SCREEN_Y);
	int64_t sw = kp_scalar(g, KP_SCREEN_W) == KP_NIL ? KP_SCREEN_TILES_W : kp_scalar(g, KP_SCREEN_W);
	int64_t sh = kp_scalar(g, KP_SCREEN_H) == KP_NIL ? KP_SCREEN_TILES_H : kp_scalar(g, KP_SCREEN_H);
	if (count > KP_TILES) kp_game_fail(g, "fog: a map of %lld tiles", (long long)count);
	if (!t->has_terrain_changed) {
		for (i = 0; i < count; i++) t->terrain_changed[i] = kp_band(t->megatiles[i], 0x7fff) != kp_cv5_megatile(g, t->raw_tiles[i]);
		t->has_terrain_changed = 1;
	}
	for (i = 0; i < count; i++) {
		int64_t x = kp_mod(i, g->map_w), y = i / g->map_w;
		if (!t->terrain_changed[i]) continue;
		if (kp_u32v(x - sx) < sw && kp_u32v(y - sy) < sh) continue;
		refresh_tile(g, i);
	}
}

/* RefreshRememberedTerrainOnscreen 0x40b220 with RefreshRememberedTerrainRow 0x40b310: each draw of the game screen,
 * whose top-left tile is (x0, y0), refreshes the remembered terrain of its w x h tiles (the original's 21x14). The
 * presentation (or the replay of the original's draws) calls it; the off-screen refresh leaves these tiles out. */
void kp_refresh_terrain_onscreen(KpGame *g, int64_t x0, int64_t y0, int64_t w, int64_t h)
{
	int64_t row, k, n = g->map_w - x0;
	kp_scalar_set(g, KP_SCREEN_X, x0);
	kp_scalar_set(g, KP_SCREEN_Y, y0);
	kp_scalar_set(g, KP_SCREEN_W, w);
	kp_scalar_set(g, KP_SCREEN_H, h);
	if (n > w) n = w;
	for (row = 0; row < h; row++) {
		int64_t y = y0 + row;
		if (y >= g->map_h) continue;
		for (k = 0; k < n; k++) refresh_tile(g, y * g->map_w + x0 + k);
	}
}

/* ResetTileVisibility 0x46c180: every tile unseen again before the units' sight is revealed anew, unless a
 * vision cheat is on. */
void kp_reset_tile_visibility(KpGame *g)
{
	int64_t i, count = g->map_w * g->map_h;
	if (kp_band(kp_scalar(g, KP_CHEATS), VISION_CHEATS) != 0) return;
	for (i = 0; i < count; i++) {
		int64_t *f = tile_ref(g, i);
		*f = kp_bor(*f, 0xff);
	}
}

/* Game.SIGHT_UPGRADES: the upgrade that gives a unit type sight 11. */
static int64_t sight_upgrade(int64_t t)
{
	switch (t) {
	case 1: return 0x14;
	case 42: return 0x19;
	case 70: return 0x29;
	case 84: return 0x26;
	}
	return -1;
}

/* GetUnitSightRange 0x423150 (the sight index of the vision templates). */
int64_t kp_unit_sight(KpGame *g, int64_t n, int ignore_blind)
{
	KpUnits *u = &g->u;
	int64_t upgrade;
	if (kp_band(u->status[n], 2) != 0 && kp_band(u->status[n], 1) == 0 && !kp_is_morphing_zerg_building(g, n)) return 4;
	if (!ignore_blind && u->blind[n] != 0) return 2;
	upgrade = sight_upgrade(u->type[n]);
	if (upgrade >= 0 && kp_upgrade_level(g, u->owner[n], upgrade) != 0) return 11;
	return KP_DAT(g, units, sight_range, u->type[n]);
}

static int64_t loaded(KpUnits *u, int k, int64_t n)
{
	switch (k) {
	case 0: return u->loaded0[n];
	case 1: return u->loaded1[n];
	case 2: return u->loaded2[n];
	case 3: return u->loaded3[n];
	case 4: return u->loaded4[n];
	case 5: return u->loaded5[n];
	case 6: return u->loaded6[n];
	}
	return u->loaded7[n];
}

/* UnitHasVisibleCargo 0x4273c0. */
int kp_unit_has_visible_cargo(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t parent;
	if (kp_band(KP_DAT(g, units, special_ability_flags, u->type[n]), CARGO_VISION) == 0) {
		int k;
		if (!kp_is_transport(g, n)) return 0;
		/* 0x438290: a loaded unit seeing through its transport. */
		for (k = 0; k <= 7; k++) {
			int64_t c = kp_unit_from_ref(g, loaded(u, k, n));
			if (c != 0 && kp_band(KP_DAT(g, units, special_ability_flags, u->type[c]), CARGO_VISION) != 0
				&& kp_band(u->union_units[c], 1) != 0 && u->union_0xc0[c] != 0
				&& u->type[kp_game_unit(g, u->union_0xc0[c])] == 0xd7) {
				return 1;
			}
		}
		return 0;
	}
	parent = u->union_0xc0[n];
	return parent != 0 && kp_band(u->union_units[n], 1) != 0 && u->type[kp_game_unit(g, parent)] == 0xd7;
}

/* IsTransport 0x4381f0. */
int kp_is_transport(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	if (kp_band(u->status[n], 0x40000000) != 0) return 0;
	if (u->type[n] == 0x2a && kp_upgrade_level(g, u->owner[n], OVERLORD_TRANSPORT) == 0) return 0;
	return KP_DAT(g, units, space_provided, u->type[n]) != 0;
}

/* UpdateUnitVision 0x423260. */
void kp_unit_update_vision(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t owner = u->owner[n], mask;
	int32_t s;
	if (!(owner < 8 || u->parasites[n] != 0) || u->type[n] == NUKE) return;
	if (!kp_unit_has_visible_cargo(g, n) && !(u->type[n] == 0xd7 && u->order[n] == 0x11)) {
		int64_t parasites = u->parasites[n], p;
		mask = parasites;
		if (owner < 8) mask = kp_bor(mask, kp_shared_vision(g, owner));
		for (p = 0; p <= 11; p++) {
			if (kp_band(parasites, (int64_t)1 << p) != 0) mask = kp_bor(mask, kp_shared_vision(g, p));
		}
	} else {
		mask = ALL_PLAYERS;
	}
	s = kp_unit_sprite(g, n);
	kp_reveal_sight(g, kp_s16v(g->w->spr.x[s]), kp_s16v(g->w->spr.y[s]), kp_band(u->status[n], AIR) != 0, mask,
		kp_unit_sight(g, n, 0));
}

/* UpdateUnitSpriteVisibility 0x438c20. */
void kp_unit_update_sprite_visibility(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	KpImages *w = g->w;
	int64_t t = u->type[n], sub = u->subunit[n];
	int32_t s = kp_unit_sprite(g, n);
	int changed = kp_sprite_visibility_rect(w, s, (int32_t)kp_s16v(KP_DAT(g, units, staredit_placement_size, t * 2)),
		(int32_t)kp_s16v(KP_DAT(g, units, staredit_placement_size, t * 2 + 1)));
	if (sub != 0 && kp_band(w->spr.flags[kp_unit_sprite(g, sub)], 0x20) == 0) {
		kp_sprite_set_visibility(w, kp_unit_sprite(g, sub), w->spr.visibility[s]);
	}
	if (!changed) return;
	/* The local player lost sight of the unit: it leaves the local selection (DeselectUnitLocally 0x4a7b80),
	 * loses its circles, and a building or a map doodad (203..213) leaves a fog-of-war ghost. */
	if (kp_band(w->spr.flags[s], SPRITE_SELECTED) != 0) {
		int64_t rest[KP_SLOTS];
		int k, count = 0;
		for (k = 0; k < KP_SLOTS; k++) {
			int64_t m = g->local_selection[k];
			if (m == 0) break;
			if (m != n) rest[count++] = m;
		}
		kp_select_locally(g, rest, count);
	}
	kp_sprite_remove_circle(w, s);
	if (kp_band(w->spr.flags[s], SPRITE_DASHED) != 0) {
		kp_game_fail(g, "vision: shared dashed selection circles of a team game (0x416cd0)");
	}
	if ((kp_band(u->status[n], 2) != 0 || (t >= DOODAD_FIRST && t <= DOODAD_LAST)) && !kp_unit_is_dead(g, n)) {
		kp_fog_create(w, (int32_t)t, s);
	}
}
