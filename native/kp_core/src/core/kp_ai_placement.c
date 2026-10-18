/* The AI's building placement (atlas P32, P33; docs/research/native-ai-placement.md): a 64 x 64 grid of candidate
 * tiles around a seed, filtered by the pipeline 0x495bb0, and the search 0x494fc0 that picks the candidate nearest an
 * anchor by route distance. A grid cell (row, column) is the tile (seed tile x - 32 + column, seed tile y - 32 + row);
 * the filters that measure from the seed take the cell's point at the seed's offset within its tile. */
#include "kp_game_internal.h"
#include "kp_pathing.h"
#include "kp_pathing_internal.h"
#include "kp_terrain.h"

#include <setjmp.h>
#include <string.h>

#define GRID 64
#define HALF 32
#define DAT_BUILDING 0x1
#define DAT_ADDON 0x2
#define DAT_RESOURCE_DEPOT 0x1000
#define DAT_RESOURCE_CONTAINER 0x2000
#define STATUS_COMPLETED 0x1
#define STATUS_GROUNDED 0x2
#define STATUS_DISABLED 0x400
#define TILE_WALKABLE 0x10000
#define NEUTRAL 11
#define FAR 99999999
#define NO_ANCHOR 2500
#define TURRET_SIZE 40 /* a Missile Turret's search window */
#define DEFAULT_SIZE 3
#define DEFENSE_LEVELS 4
/* the unit types the filters name */

typedef uint8_t Grid[GRID][GRID];

static int64_t flags_of(KpGame *g, int64_t t) { return KP_DAT(g, units, special_ability_flags, t); }
static int64_t place_w(KpGame *g, int64_t t) { return kp_s16v(KP_DAT(g, units, staredit_placement_size, t * 2)); }
static int64_t place_h(KpGame *g, int64_t t) { return kp_s16v(KP_DAT(g, units, staredit_placement_size, t * 2 + 1)); }
static int64_t div32(int64_t v) { return v / 32; } /* signed, toward zero */
static int64_t sprite_x(KpGame *g, int64_t n) { return kp_s16v(g->w->spr.x[kp_unit_sprite(g, n)]); }
static int64_t sprite_y(KpGame *g, int64_t n) { return kp_s16v(g->w->spr.y[kp_unit_sprite(g, n)]); }

/* 0x496210: no candidate is left. */
static int empty(Grid grid)
{
	int r, c;
	for (r = 0; r < GRID; r++) {
		for (c = 0; c < GRID; c++) {
			if (grid[r][c] != 0) return 0;
		}
	}
	return 1;
}

/* A filter that would leave no candidate takes its entry's grid back. */
static void restore_if_empty(Grid grid, Grid entry)
{
	if (empty(grid)) memcpy(grid, entry, sizeof(Grid));
}

/* 0x4960b0: the cells where the requested type would overlap an object of objw x objh tiles at tile (ox, oy), grown
 * by margin, are no candidates. */
static void mask_rect(KpGame *g, Grid grid, int64_t objw, int64_t objh, int64_t ox, int64_t oy, int64_t req, int64_t sx,
	int64_t sy, int64_t margin)
{
	int64_t rw = div32(place_w(g, req)), rh = div32(place_h(g, req));
	int64_t left = ox - kp_shrv(objw, 1) - rw - margin + 1, width = rw + 2 * margin + objw - 1;
	int64_t height = 2 * margin - 1 + rh + objh, row = oy - div32(sy) - kp_shrv(objh, 1) - rh - margin + 1 + HALF;
	int64_t k, j;
	for (k = 0; k < kp_u32v(height); k++, row++) {
		int64_t col = left - div32(sx) + HALF;
		for (j = 0; j < kp_u32v(width); j++, col++) {
			if (kp_u32v(col) < GRID && kp_u32v(row) < GRID) grid[row][col] = 0;
		}
	}
}

/* 0x4961a0: mask_rect around unit n. */
static void mask_unit(KpGame *g, Grid grid, int64_t n, int64_t req, int64_t sx, int64_t sy, int64_t margin)
{
	int64_t t = g->u.type[n];
	mask_rect(g, grid, div32(place_w(g, t)), div32(place_h(g, t)), div32(sprite_x(g, n)), div32(sprite_y(g, n)), req, sx,
		sy, margin);
}

/* 0x495d40: the window of size cells around the centre where the type may stand (a Command Center, Factory, Starport
 * or Science Facility also with room for its add-on, tested as a Missile Turret). */
static void base_grid(KpGame *g, Grid grid, int64_t id, int64_t p, int64_t sx, int64_t sy, int64_t size)
{
	uint32_t start = (uint32_t)(HALF - kp_shrv(size, 1)), end = start + (uint32_t)size;
	int addon = id == KP_TYPE_COMMAND_CENTER || id == KP_TYPE_FACTORY || id == KP_TYPE_STARPORT || id == KP_TYPE_SCIENCE_FACILITY;
	int64_t r, c;
	for (r = 0; r < GRID && (uint32_t)r < end; r++) {
		int64_t ty = div32(sy) - HALF + r;
		if ((uint32_t)r < start) continue;
		for (c = 0; c < GRID && (uint32_t)c < end; c++) {
			int64_t tx = div32(sx) - HALF + c;
			if ((uint32_t)c < start) continue;
			if (addon && kp_validate_placement(g, 0, p, tx + 4, ty + 1, KP_TYPE_MISSILE_TURRET, 0, 1, 0) != 0) continue;
			if (kp_validate_placement(g, 0, p, tx, ty, id, 0, 1, 0) == 0) grid[r][c] = 1;
		}
	}
}

/* 0x497630: candidates of another ground group than the seed's are none. */
static void filter_group(KpGame *g, Grid grid, int64_t sx, int64_t sy)
{
	int64_t group = kp_region_group_of(g, kp_region_at(g, sx, sy)), r, c;
	for (r = 0; r < GRID; r++) {
		for (c = 0; c < GRID; c++) {
			if (grid[r][c] == 0) continue;
			if (kp_region_group_of(g, kp_region_at(g, sx - 1024 + 32 * c, sy - 1024 + 32 * r)) != group) grid[r][c] = 0;
		}
	}
}

/* 0x4ae700: no neighbour of region r is abandoned, threatened or excluded for player p. */
static int quiet_neighbors(KpGame *g, int64_t p, int64_t r)
{
	const KpAiRegions *a = &g->air;
	int64_t at, n = kp_region_all_neighbors(g, r, &at), k;
	for (k = 0; k < n; k++) {
		int64_t q = g->pathing.rg.neighbors[at + k];
		if (a->state[p][q] == KP_RSTATE_ABANDONED || a->target_air[p][q] != 0 || a->target_ground[p][q] != 0
			|| kp_band(a->flags[p][q], KP_RFLAG_UNDER_ATTACK) != 0) {
			return 0;
		}
	}
	return 1;
}

/* 0x497910: a candidate next to a threatened region is none (not for the main buildings). */
static void filter_threat(KpGame *g, Grid grid, int64_t id, int64_t p, int64_t sx, int64_t sy)
{
	int64_t r, c;
	if (id == KP_TYPE_COMMAND_CENTER || id == KP_TYPE_HATCHERY || id == KP_TYPE_NEXUS) return;
	for (r = 0; r < GRID; r++) {
		for (c = 0; c < GRID; c++) {
			if (grid[r][c] == 0) continue;
			if (!quiet_neighbors(g, p, kp_band(kp_region_at(g, sx - 1024 + 32 * c, sy - 1024 + 32 * r), 0xffff))) grid[r][c] = 0;
		}
	}
}

/* 0x4976d0: a candidate farther than limit route tiles from the seed is none. */
static void filter_radius(KpGame *g, Grid grid, int64_t limit, int64_t sx, int64_t sy)
{
	int64_t r, c;
	for (r = 0; r < GRID; r++) {
		for (c = 0; c < GRID; c++) {
			if (grid[r][c] == 0) continue;
			if (kp_u32v(kp_ai_route_distance(g, sx, sy, sx - 1024 + 32 * c, sy - 1024 + 32 * r)) > kp_u32v(limit)) grid[r][c] = 0;
		}
	}
}

/* 0x498430: rather candidates at the seed's height. */
static void filter_height(KpGame *g, Grid grid, int64_t sx, int64_t sy)
{
	Grid entry;
	int64_t h = kp_terrain_height(g, sx, sy), r, c;
	memcpy(entry, grid, sizeof entry);
	for (r = 0; r < GRID; r++) {
		for (c = 0; c < GRID; c++) {
			if (grid[r][c] != 0 && kp_terrain_height(g, sx - 1024 + 32 * c, sy - 1024 + 32 * r) != h) grid[r][c] = 0;
		}
	}
	restore_if_empty(grid, entry);
}

/* 0x498820: rather candidates away from the tiles that are not walkable (TileHasFlags 0x413270). */
static void filter_tiles(KpGame *g, Grid grid, int64_t id, int64_t sx, int64_t sy)
{
	Grid entry;
	int64_t r, c;
	memcpy(entry, grid, sizeof entry);
	for (r = 0; r < GRID; r++) {
		int64_t ty = div32(sy) - HALF + r;
		if (ty >= g->map_h) break;
		if (ty < 0) continue;
		for (c = 0; c < GRID; c++) {
			int64_t tx = div32(sx) - HALF + c;
			if (tx >= g->map_w) break;
			if (tx < 0) continue;
			if (kp_band(g->terrain.tiles[ty * g->map_w + tx], TILE_WALKABLE) == 0) mask_rect(g, grid, 1, 1, tx, ty, id, sx, sy, 1);
		}
	}
	restore_if_empty(grid, entry);
}

/* 0x498230: rather candidates away from player p's resource depots and resources and from the neutral resources (the
 * neutral ones not for a Bunker); none for an add-on. */
static void filter_resources(KpGame *g, Grid grid, int64_t id, int64_t p, int64_t sx, int64_t sy)
{
	KpUnits *u = &g->u;
	Grid entry;
	int64_t n;
	if (kp_band(flags_of(g, id), DAT_ADDON) != 0) return;
	memcpy(entry, grid, sizeof entry);
	for (n = g->player_units[p]; n != 0; n = u->player_next[n]) {
		int64_t t = u->type[n];
		int depot;
		if (kp_unit_inactive(g, n)) continue;
		depot = (kp_band(u->status[n], STATUS_COMPLETED) != 0 || t == KP_TYPE_HIVE || t == KP_TYPE_LAIR
			|| (t == KP_TYPE_HATCHERY && kp_is_morphing_zerg_building(g, n))) /* 0x43c040 */
			&& kp_band(u->status[n], STATUS_GROUNDED) != 0 && kp_band(flags_of(g, t), DAT_RESOURCE_DEPOT) != 0;
		if (depot || kp_band(flags_of(g, t), DAT_RESOURCE_CONTAINER) != 0) {
			mask_rect(g, grid, div32(place_w(g, t)), div32(place_h(g, t)), div32(sprite_x(g, n)), div32(sprite_y(g, n)), id,
				sx, sy, 2);
		}
	}
	if (id != KP_TYPE_BUNKER) {
		for (n = g->player_units[NEUTRAL]; n != 0; n = u->player_next[n]) {
			int64_t t = u->type[n];
			if (kp_unit_inactive(g, n) || kp_band(flags_of(g, t), DAT_RESOURCE_CONTAINER) == 0) continue;
			mask_rect(g, grid, div32(place_w(g, t)), div32(place_h(g, t)), div32(sprite_x(g, n)), div32(sprite_y(g, n)), id,
				sx, sy, 2);
		}
	}
	restore_if_empty(grid, entry);
}

/* 0x497d90: rather keep the two columns right of player p's Command Centers, Factories, Starports and Science
 * Facilities free (their add-ons), as high as the requested type and one more row. */
static void filter_addons(KpGame *g, Grid grid, int64_t id, int64_t p, int64_t sx, int64_t sy)
{
	KpUnits *u = &g->u;
	Grid entry;
	int64_t n, height = div32(place_h(g, id)) + 1;
	memcpy(entry, grid, sizeof entry);
	for (n = g->player_units[p]; n != 0; n = u->player_next[n]) {
		int64_t t = u->type[n], col, k;
		if (kp_unit_inactive(g, n)) continue;
		if (t != KP_TYPE_COMMAND_CENTER && t != KP_TYPE_FACTORY && t != KP_TYPE_STARPORT && t != KP_TYPE_SCIENCE_FACILITY) continue;
		for (col = div32(sprite_x(g, n)) - div32(sx) + HALF + 2; col <= div32(sprite_x(g, n)) - div32(sx) + HALF + 3; col++) {
			int64_t row = div32(sprite_y(g, n)) - div32(sy) + HALF + 1;
			if (kp_u32v(col) >= GRID) continue;
			for (k = 0; k < height; k++, row--) {
				if (kp_u32v(row) < GRID) grid[row][col] = 0;
			}
		}
	}
	restore_if_empty(grid, entry);
}

/* 0x497f00: rather candidates spaced from player p's Barracks, Bunkers and Creep Colonies (margin 1) and its
 * Factories, Missile Turrets, Photon Cannons, Robotics Facilities and Gateways (margin 2); these types and Barracks also need all
 * eight neighbours of a candidate candidates. */
static void filter_spacing(KpGame *g, Grid grid, int64_t id, int64_t p, int64_t sx, int64_t sy)
{
	KpUnits *u = &g->u;
	Grid entry;
	int64_t n, r, c;
	memcpy(entry, grid, sizeof entry);
	for (n = g->player_units[p]; n != 0; n = u->player_next[n]) {
		int64_t t = u->type[n];
		if (kp_unit_inactive(g, n)) continue;
		switch (t) {
		case KP_TYPE_BARRACKS: case KP_TYPE_BUNKER: case KP_TYPE_CREEP_COLONY:
			mask_unit(g, grid, n, id, sx, sy, 1);
			break;
		case KP_TYPE_FACTORY: case KP_TYPE_MISSILE_TURRET: case KP_TYPE_ROBOTICS_FACILITY: case KP_TYPE_GATEWAY: case KP_TYPE_PHOTON_CANNON:
			mask_rect(g, grid, div32(place_w(g, t)), div32(place_h(g, t)), div32(sprite_x(g, n)), div32(sprite_y(g, n)), id,
				sx, sy, 2);
			break;
		}
	}
	switch (id) {
	case KP_TYPE_BARRACKS: case KP_TYPE_FACTORY: case KP_TYPE_MISSILE_TURRET: case KP_TYPE_BUNKER: case KP_TYPE_ROBOTICS_FACILITY:
	case KP_TYPE_GATEWAY: case KP_TYPE_PHOTON_CANNON:
		for (r = 0; r < GRID; r++) {
			for (c = 0; c < GRID; c++) {
				int dr, dc, keep = 1;
				if (grid[r][c] == 0) continue;
				for (dr = -1; dr <= 1 && keep; dr++) {
					for (dc = -1; dc <= 1 && keep; dc++) {
						if (dr == 0 && dc == 0) continue;
						keep = kp_u32v(r + dr) < GRID && kp_u32v(c + dc) < GRID && entry[r + dr][c + dc] != 0;
					}
				}
				if (!keep) grid[r][c] = 0;
			}
		}
		break;
	}
	restore_if_empty(grid, entry);
}

/* The defense patterns 0x509120, 0x509248, 0x5092f8, 0x50934c of DefenseMaskFilter 0x498600: 'x' masks a cell. */
static const char *const DEFENSE_PATTERNS[DEFENSE_LEVELS] = {
	"......xxxxx......" "....xxxxxxxxx...." "...xxxxxxxxxxx..." "..xxxxxxxxxxxxx.." ".xxxxxxxxxxxxxxx."
	".xxxxxxxxxxxxxxx." "xxxxxxxxxxxxxxxxx" "xxxxxxxxxxxxxxxxx" "xxxxxxxxxxxxxxxxx" "xxxxxxxxxxxxxxxxx"
	"xxxxxxxxxxxxxxxxx" ".xxxxxxxxxxxxxxx." ".xxxxxxxxxxxxxxx." "..xxxxxxxxxxxxx.." "...xxxxxxxxxxx..."
	"....xxxxxxxxx...." "......xxxxx......",
	"....xxxxx...." "...xxxxxxx..." "..xxxxxxxxx.." ".xxxxxxxxxxx." "xxxxxxxxxxxxx" "xxxxxxxxxxxxx"
	"xxxxxxxxxxxxx" "xxxxxxxxxxxxx" "xxxxxxxxxxxxx" ".xxxxxxxxxxx." "..xxxxxxxxx.." "...xxxxxxx..." "....xxxxx....",
	"...xxx..." ".xxxxxxx." ".xxxxxxx." "xxxxxxxxx" "xxxxxxxxx" "xxxxxxxxx" ".xxxxxxx." ".xxxxxxx." "...xxx...",
	".xxx." "xxxxx" "xxxxx" "xxxxx" ".xxx.",
};
static const int64_t DEFENSE_SIDES[DEFENSE_LEVELS] = { 17, 13, 9, 5 };

/* DefenseMaskFilter 0x498600: rather candidates outside the pattern of the level around player p's defenses of the
 * requested type and its colonies; when none is left, the next smaller pattern. */
static void filter_defense(KpGame *g, Grid grid, int64_t p, int64_t sx, int64_t sy, int64_t id, int level)
{
	KpUnits *u = &g->u;
	Grid entry;
	int64_t n;
	memcpy(entry, grid, sizeof entry);
	for (n = g->player_units[p]; n != 0; n = u->player_next[n]) {
		int64_t t = u->type[n], side, oy, ox, k, j;
		const char *pattern;
		if (kp_unit_inactive(g, n)) continue;
		if (t == KP_TYPE_MISSILE_TURRET || t == KP_TYPE_BUNKER || t == KP_TYPE_PHOTON_CANNON) {
			if (t != id) continue;
		} else if (t != KP_TYPE_CREEP_COLONY && t != KP_TYPE_SPORE_COLONY && t != KP_TYPE_SUNKEN_COLONY) {
			continue;
		}
		if (level >= DEFENSE_LEVELS) return;
		side = DEFENSE_SIDES[level];
		pattern = DEFENSE_PATTERNS[level];
		oy = div32(sprite_y(g, n) - sy) - side / 2 + (HALF - 1);
		ox = div32(sprite_x(g, n) - sx) - side / 2 + (HALF - 1);
		for (k = 0; k < side; k++) {
			int64_t row = oy + k;
			if (row >= GRID) break;
			if (row < 0) continue;
			for (j = 0; j < side; j++) {
				int64_t col = ox + j;
				if (kp_u32v(col) < GRID && pattern[k * side + j] == 'x') grid[row][col] = 0;
			}
		}
	}
	if (!empty(grid)) return;
	memcpy(grid, entry, sizeof(Grid));
	if (level < DEFENSE_LEVELS - 1) filter_defense(g, grid, p, sx, sy, id, level + 1);
}

/* 0x498520: rather a Pylon's candidates outside player p's power (IsPointPowered 0x430000 as a Photon Cannon). */
static void filter_pylons(KpGame *g, Grid grid, int64_t p, int64_t sx, int64_t sy)
{
	Grid entry;
	int64_t r, c;
	memcpy(entry, grid, sizeof entry);
	for (r = 0; r < GRID; r++) {
		for (c = 0; c < GRID; c++) {
			if (grid[r][c] != 0 && kp_point_powered(g, KP_TYPE_PHOTON_CANNON, sx - 1024 + 32 * c, sy - 1024 + 32 * r, p)) grid[r][c] = 0;
		}
	}
	restore_if_empty(grid, entry);
}

/* The lines of DirectionalFilter 0x498930 (0x509040: start column, start row, column step, row step). */
static const int DIRECTION_LINES[][4] = {
	{ 32, 0, 0, 1 }, { 0, 32, 1, 0 }, { 31, 0, 0, 1 }, { 0, 31, 1, 0 }, { 33, 0, 0, 1 }, { 0, 33, 1, 0 }, { 30, 0, 0, 1 },
	{ 29, 0, 0, 1 }, { 0, 30, 1, 0 }, { 28, 0, 0, 1 }, { 0, 29, 1, 0 }, { 27, 0, 0, 1 }, { 0, 28, 1, 0 },
};

/* DirectionalFilter 0x498930: the lines through the seed are cleared one after another; when one empties the grid,
 * the grid from before the filter comes back. */
static void filter_direction(Grid grid)
{
	Grid entry;
	size_t i;
	int k;
	memcpy(entry, grid, sizeof entry);
	for (i = 0; i < sizeof DIRECTION_LINES / sizeof DIRECTION_LINES[0]; i++) {
		int c = DIRECTION_LINES[i][0], r = DIRECTION_LINES[i][1];
		for (k = 0; k < GRID; k++, c += DIRECTION_LINES[i][2], r += DIRECTION_LINES[i][3]) grid[r][c] = 0;
		if (empty(grid)) {
			memcpy(grid, entry, sizeof(Grid));
			return;
		}
	}
}

/* ChooseDefenseAnchor 0x495480: of the defense anchors, the one farthest (by its nearest) from player p's Bunkers,
 * Creep Colonies and Sunken Colonies; 0 without anchors. */
static int defense_anchor(KpGame *g, int64_t p, int64_t *region)
{
	KpAi *ai = &g->ai;
	int64_t nearest[KP_AI_ANCHORS], cx[KP_AI_ANCHORS], cy[KP_AI_ANCHORS], n, k, best;
	if (ai->anchor_count == 0) return 0;
	if (ai->anchor_count == 1) {
		*region = ai->anchors[0];
		return 1;
	}
	for (k = 0; k < ai->anchor_count; k++) {
		nearest[k] = FAR;
		kp_region_center_point(g, ai->anchors[k], &cx[k], &cy[k]); /* GetRegionCenterPoint 0x481a60 */
	}
	for (n = g->player_units[p]; n != 0; n = g->u.player_next[n]) {
		int64_t t = g->u.type[n];
		if (kp_unit_inactive(g, n) || (t != KP_TYPE_BUNKER && t != KP_TYPE_CREEP_COLONY && t != KP_TYPE_SUNKEN_COLONY)) continue;
		for (k = 0; k < ai->anchor_count; k++) {
			int64_t d = kp_u32v(kp_ai_route_distance(g, sprite_x(g, n), sprite_y(g, n), cx[k], cy[k]));
			if (d < nearest[k]) nearest[k] = d;
		}
	}
	for (k = 1, best = 0; k < ai->anchor_count; k++) {
		if (nearest[k] > nearest[best]) best = k;
	}
	*region = ai->anchors[best];
	return 1;
}

/* The Pylon search's callback 0x433f20: a disabled unit of player p. */
static int disabled_own(KpGame *g, int64_t c, int64_t p)
{
	return kp_band(g->u.status[c], STATUS_DISABLED) != 0 && g->u.owner[c] == p;
}

/* 0x495bb0: the candidates of type id for player p around (sx, sy) within size. */
static void pipeline(KpGame *g, Grid grid, int64_t id, int64_t p, int64_t sx, int64_t sy, int64_t size)
{
	int base = id == KP_TYPE_COMMAND_CENTER || id == KP_TYPE_HATCHERY || id == KP_TYPE_NEXUS;
	int64_t anchor;
	memset(grid, 0, sizeof(Grid));
	base_grid(g, grid, id, p, sx, sy, size);
	filter_group(g, grid, sx, sy);
	if (id != KP_TYPE_PYLON) filter_threat(g, grid, id, p, sx, sy);
	filter_radius(g, grid, size, sx, sy);
	if (empty(grid)) return;
	filter_height(g, grid, sx, sy);
	filter_tiles(g, grid, id, sx, sy);
	if (!base) filter_resources(g, grid, id, p, sx, sy);
	filter_addons(g, grid, id, p, sx, sy);
	switch (id) {
	case KP_TYPE_PYLON:
		filter_pylons(g, grid, p, sx, sy);
		return;
	case KP_TYPE_CREEP_COLONY:
		if (kp_band(g->pai.flags[p], KP_AIF_CREEP) == 0 && defense_anchor(g, p, &anchor)) return;
		filter_defense(g, grid, p, sx, sy, id, 0);
		return;
	case KP_TYPE_BUNKER:
		if (defense_anchor(g, p, &anchor)) return;
		filter_defense(g, grid, p, sx, sy, id, 0);
		return;
	case KP_TYPE_MISSILE_TURRET: case KP_TYPE_PHOTON_CANNON:
		filter_defense(g, grid, p, sx, sy, id, 0);
		return;
	default:
		if (!base) filter_spacing(g, grid, id, p, sx, sy);
		return;
	}
}

/* The tile of the add-on beside its parent at the seed (centre) for a builder of type parent. */
static void addon_tile(KpGame *g, int64_t parent, int64_t id, int64_t sx, int64_t sy, int64_t *tx, int64_t *ty)
{
	*tx = kp_s16v(div32(sx) - div32(place_w(g, parent) / 2) + div32(kp_s16v(KP_DAT(g, units, addon_position, id * 2))));
	*ty = kp_s16v(div32(sy) + div32(-(place_h(g, parent) / 2)) + div32(kp_s16v(KP_DAT(g, units, addon_position, id * 2 + 1))));
}

/* FindBuildingPlacement 0x494fc0: an add-on beside its builder when it fits there; else the candidate of the pipeline
 * nearest by route distance to the anchor (a defense anchor's centre, else the seed), within size before beyond it;
 * a failed defense anchor is followed by a pass from the seed. (*ox, *oy) is the chosen tile's corner plus half the
 * placement box; 0 when none. */
int kp_ai_find_placement(KpGame *g, int64_t builder, int64_t id, int64_t sx, int64_t sy, int64_t size, int64_t *ox, int64_t *oy)
{
	Grid grid;
	int64_t p = g->u.owner[builder], anchor = NO_ANCHOR, ax, ay, pass, t = id;
	int directional = 1;
	sx = kp_s16v(sx);
	sy = kp_s16v(sy);
	if (kp_band(flags_of(g, id), DAT_ADDON) != 0) {
		int64_t tx, ty;
		addon_tile(g, g->u.type[builder], id, sx, sy, &tx, &ty);
		if (kp_validate_placement(g, builder, p, tx, ty, id, 0, 1, 0) == 0) {
			int64_t px = kp_s16v(tx - div32(kp_s16v(KP_DAT(g, units, addon_position, id * 2))));
			int64_t py = kp_s16v(ty - div32(kp_s16v(KP_DAT(g, units, addon_position, id * 2 + 1))));
			*ox = kp_s16v(px * 32 + place_w(g, g->u.type[builder]) / 2);
			*oy = kp_s16v(py * 32 + place_h(g, g->u.type[builder]) / 2);
			return 1;
		}
	}
	switch (id) {
	case KP_TYPE_COMMAND_CENTER: case KP_TYPE_HATCHERY: case KP_TYPE_NEXUS: directional = 0; break;
	case KP_TYPE_COMSAT: case KP_TYPE_NUCLEAR_SILO: t = KP_TYPE_COMMAND_CENTER; break;
	case KP_TYPE_CONTROL_TOWER: t = KP_TYPE_STARPORT; break;
	case KP_TYPE_COVERT_OPS: case KP_TYPE_PHYSICS_LAB: t = KP_TYPE_SCIENCE_FACILITY; break;
	case KP_TYPE_MACHINE_SHOP: t = KP_TYPE_FACTORY; break;
	case KP_TYPE_BUNKER:
		if (defense_anchor(g, p, &anchor)) directional = 0;
		break;
	case KP_TYPE_CREEP_COLONY:
		if (kp_band(g->pai.flags[p], KP_AIF_CREEP) == 0 && defense_anchor(g, p, &anchor)) directional = 0;
		break;
	case KP_TYPE_PYLON: { /* the search starts at the player's nearest disabled unit (0x433220; the result 0x6a5da0 is never
	               * read) */
		int64_t c = kp_nearest_unit_point(g, sx, sy, g->map_rect, disabled_own, p);
		if (c != 0) {
			sx = sprite_x(g, c);
			sy = sprite_y(g, c);
			directional = 0;
		}
		break;
	}
	}
	g->ai.placement_builder = builder;
	pipeline(g, grid, t, p, sx, sy, size);
	g->ai.placement_builder = 0;
	if (directional) filter_direction(grid);
	if (anchor != NO_ANCHOR) {
		kp_region_center_point(g, anchor, &ax, &ay);
		ax = div32(ax);
		ay = div32(ay);
	} else {
		ax = div32(sx);
		ay = div32(sy);
	}
	for (pass = 0; pass < 2; pass++) {
		int64_t best_in = FAR, best_out = FAR, in_x = 0, in_y = 0, out_x = 0, out_y = 0, r, c;
		for (r = 0; r < GRID; r++) {
			for (c = 0; c < GRID; c++) {
				int64_t tx = div32(sx) - HALF + c, ty = div32(sy) - HALF + r, d;
				if (grid[r][c] == 0) continue;
				d = kp_u32v(kp_ai_route_distance(g, kp_s16v(ax) * 32, kp_s16v(ay) * 32, tx * 32, ty * 32));
				if (d >= best_in) continue;
				if (d <= kp_u32v(size)) {
					best_in = d;
					in_x = tx;
					in_y = ty;
				} else if (d < best_out) {
					best_out = d;
					out_x = tx;
					out_y = ty;
				}
			}
		}
		if (best_in != FAR || best_out != FAR) {
			int64_t cx = best_in != FAR ? in_x : out_x, cy = best_in != FAR ? in_y : out_y;
			*ox = kp_s16v(kp_s16v(cx) * 32 + place_w(g, t) / 2);
			*oy = kp_s16v(kp_s16v(cy) * 32 + place_h(g, t) / 2);
			return 1;
		}
		if (anchor == NO_ANCHOR) return 0;
		ax = div32(sx);
		ay = div32(sy);
	}
	return 0;
}

/* FindBuildingPlacementFrom 0x4962c0: the search from (sx, sy) (a Missile Turret's within 40); a throttled player
 * (placement_throttle +0x211) finds none, and a failed throttled search throttles it. */
int kp_ai_place_from(KpGame *g, int64_t builder, int64_t id, int64_t sx, int64_t sy, int throttle, int64_t size,
	int64_t *ox, int64_t *oy)
{
	int64_t p = g->u.owner[builder];
	int found;
	if (throttle && g->pai.placement_throttle[p] != 0) return 0; /* 0x482ec0 */
	found = kp_ai_find_placement(g, builder, id, sx, sy, kp_band(id, 0xffff) == KP_TYPE_MISSILE_TURRET ? TURRET_SIZE : size, ox, oy);
	if (!found && throttle) g->pai.placement_throttle[p] = 1; /* 0x482ea0 */
	return found;
}

/* FindBuildingPlacement wrapper 0x496240: the search from the builder's sprite with the ordinary window. */
int kp_ai_place(KpGame *g, int64_t builder, int64_t id, int throttle, int64_t *ox, int64_t *oy)
{
	return kp_ai_place_from(g, builder, id, sprite_x(g, builder), sprite_y(g, builder), throttle, DEFAULT_SIZE, ox, oy);
}

/* ---- the defense anchors (ProduceDefenseAnchors 0x496e10; docs/research/native-region-paths.md#route-cuts) ---- */

#define CUT_MIN_INDEX 2
#define CUT_MAX_INDEX 6
#define CUTS_KEPT 3
#define CUT_DETOUR 10 /* a fourth route longer than the first by more than this keeps the three cuts */
#define CUT_RANK_OFFSET 9 /* the RGNS record's cut rank (native +0x20) */
#define DAT_SUBUNIT 0x10
enum { CUT_FOUND, CUT_DETOURED, CUT_NO_ROUTE, CUT_NO_RANK, CUT_TOO_SHORT };

/* 0x48dff0: player q's unit nearest (x, y) that player p is hostile to, outside unwalkable regions; 0 for none. */
static int64_t nearest_hostile(KpGame *g, int64_t p, int64_t q, int64_t x, int64_t y)
{
	int64_t n, best = 0, best_d = FAR;
	for (n = g->player_units[q]; n != 0; n = g->u.player_next[n]) {
		int64_t d;
		if (kp_unit_inactive(g, n) || kp_band(flags_of(g, g->u.type[n]), DAT_SUBUNIT) != 0) continue;
		if (!kp_player_enemy_of(g, p, n)) continue; /* 0x429110 */
		if (g->pathing.rg.klass[kp_unit_region(g, n)] == KP_REGION_UNWALKABLE) continue; /* 0x481bb0 */
		d = kp_u32v(kp_approx_distance(sprite_x(g, n) - x, sprite_y(g, n) - y));
		if (d < best_d) {
			best_d = d;
			best = n;
		}
	}
	return best;
}

/* SingleRouteCut 0x40e4e0: on the route of c, the region of the highest cut rank (the last of equal ones) at indexes
 * min..max (and before the route's last two); its status. */
static int single_cut(KpGame *g, KpRoute *c, int64_t max_index, int64_t min_index, int64_t *cut)
{
	int64_t length, hi, i, best = 0;
	if (!kp_search_region_path(g, c) || c->partial != 0) return CUT_NO_ROUTE; /* SearchRegionPath 0x491300 */
	length = c->region_count;
	if (kp_u32v(length) < kp_u32v(min_index)) return CUT_TOO_SHORT;
	hi = kp_u32v(length - 2) < kp_u32v(max_index) ? length - 2 : max_index;
	if (kp_u32v(hi) < kp_u32v(min_index)) return CUT_TOO_SHORT;
	for (i = min_index; i <= hi; i++) {
		int64_t r = c->regions[i];
		int64_t rank = kp_rgns_read(g, g->regions.regions_at + r * KP_REGION_SIZE + CUT_RANK_OFFSET, 1); /* 0x481a90 */
		if (rank != 0 && rank >= best) {
			best = rank;
			*cut = r;
		}
	}
	return best != 0 ? CUT_FOUND : CUT_NO_RANK;
}

/* 0x40e2f0: region r leaves its neighbours' lists (their counts one less), for the next search. */
static void remove_region(KpGame *g, int64_t r)
{
	KpRegionGraph *rg = &g->pathing.rg;
	int64_t k;
	for (k = 0; k < rg->nb_count[r]; k++) {
		int64_t q = rg->neighbors[rg->nb_at[r] + k], j, at = rg->nb_at[q];
		for (j = 0; j < rg->nb_count[q] && rg->neighbors[at + j] != r; j++) {}
		if (j == rg->nb_count[q]) kp_game_fail(g, "ai: region %lld is not its neighbour's (0x40e2f0)", (long long)r);
		for (; j + 1 < rg->nb_count[q]; j++) rg->neighbors[at + j] = rg->neighbors[at + j + 1];
		rg->nb_count[q]--;
		rg->ground[q]--;
	}
}

/* The graph that the cuts' searches change, kept in KpRegionGraph.saved_* and restored (0x40e480). */
static void save_graph(KpRegionGraph *rg)
{
	memcpy(rg->saved_nb_count, rg->nb_count, sizeof(int64_t) * (size_t)rg->count);
	memcpy(rg->saved_ground, rg->ground, sizeof(int64_t) * (size_t)rg->count);
	memcpy(rg->saved_neighbors, rg->neighbors, sizeof(int64_t) * (size_t)rg->neighbor_count);
}

static void restore_graph(KpRegionGraph *rg)
{
	memcpy(rg->nb_count, rg->saved_nb_count, sizeof(int64_t) * (size_t)rg->count);
	memcpy(rg->ground, rg->saved_ground, sizeof(int64_t) * (size_t)rg->count);
	memcpy(rg->neighbors, rg->saved_neighbors, sizeof(int64_t) * (size_t)rg->neighbor_count);
}

/* FindRouteCuts 0x40e190: up to three regions that cut the route from src to dst, each search after the earlier cuts
 * left the graph (restored afterwards, also when a search fails); a fourth route found keeps them only when it is a
 * detour. */
static int64_t route_cuts(KpGame *g, int64_t src, int64_t dst, int64_t max_index, int64_t min_index, int64_t *cuts)
{
	KpRegionGraph *rg = &g->pathing.rg;
	KpRoute c;
	KpTry t;
	int64_t sx, sy, dx, dy, length, kept = 0, extra;
	int status;
	if (min_index < CUT_MIN_INDEX) min_index = CUT_MIN_INDEX;
	if (kp_band(src, 0xffff) == kp_band(dst, 0xffff) || kp_u32v(max_index) < kp_u32v(min_index)) return 0;
	if (kp_region_group_of(g, src) != kp_region_group_of(g, dst)) return 0;
	kp_region_center_point(g, src, &sx, &sy);
	kp_region_center_point(g, dst, &dx, &dy);
	kp_route_new(&c, 0, sx, sy, dx, dy);
	c.src_region = src;
	c.dst_region = dst;
	if (single_cut(g, &c, max_index, min_index, &cuts[0]) != CUT_FOUND) return 0;
	length = c.region_count;
	save_graph(rg);
	kp_images_begin(g->w, &t);
	if (setjmp(t.buf)) { /* a failure in the searches leaves the graph as it was */
		kp_images_end(g->w, &t);
		restore_graph(rg);
		kp_images_rethrow(g->w);
	}
	for (kept = 1; kept <= CUTS_KEPT; kept++) {
		remove_region(g, cuts[kept - 1]);
		status = single_cut(g, &c, max_index, min_index, kept < CUTS_KEPT ? &cuts[kept] : &extra);
		if (status != CUT_FOUND) break;
	}
	kp_images_end(g->w, &t);
	if (kept > CUTS_KEPT) kept = c.region_count > length + CUT_DETOUR ? CUTS_KEPT : 0;
	else if (status == CUT_NO_RANK) kept = 0;
	restore_graph(rg);
	return kept;
}

void kp_ai_defense_anchors(KpGame *g, int64_t p, int64_t src, int64_t limit)
{
	KpAi *ai = &g->ai;
	int64_t sx, sy, q;
	kp_region_center_point(g, src, &sx, &sy);
	ai->anchor_count = 0;
	for (q = 0; q < KP_AI_PLAYERS; q++) {
		int64_t t, dst, max, count = 0, cuts[CUTS_KEPT], k;
		if (kp_alliance(g, p, q) != 0) continue;
		t = nearest_hostile(g, p, q, sx, sy);
		if (t == 0) continue;
		dst = kp_band(kp_unit_region(g, t), 0xffff);
		for (max = CUT_MIN_INDEX; max <= CUT_MAX_INDEX; max++) {
			count = route_cuts(g, src, dst, max, CUT_MIN_INDEX, cuts);
			if (count != 0 && kp_u32v(count) <= kp_u32v(limit)) break;
		}
		if (max > CUT_MAX_INDEX) continue;
		for (k = 0; k < count; k++) {
			int64_t j;
			for (j = 0; j < ai->anchor_count && ai->anchors[j] != cuts[k]; j++) {}
			if (j == ai->anchor_count) ai->anchors[ai->anchor_count++] = cuts[k];
		}
	}
}
