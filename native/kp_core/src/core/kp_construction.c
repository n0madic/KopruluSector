/* Building placement and Terran construction (kp_game.h), ported from the atlas (P04, P06, P25, P27;
 * docs/research/native-placement.md, native-workers.md, native-construction.md).
 *
 * The placement validator ValidateBuildingPlacement 0x412f10 with its grids: the ordinary tiles 0x412870 (with a
 * resource depot's spacing 0x4180a0, 0x413370), creep 0x412ac0, a Nydus exit's 0x412720, the gas lookup 0x413610
 * (0x4134f0, 0x413580, 0x418000, 0x4132b0 over GetUnitsContainingPoint 0x4221f0), power 0x4130f0 and the occupancy
 * 0x412360 (0x4122d0, 0x413390). The cells of a footprint (six by eight at most) go to the caller's grid as the
 * original's 0x675188 + entry * 48; each grid returns the greatest code it wrote. Codes: 0 fits, 1 an unseen unit, 2 a
 * resource too near a depot, 3 off the map, 4 occupied, 5 no creep, 6 unpowered, 7 unreachable, 8 no geyser, 9 seen
 * but not now (a Nydus exit), 10 unexplored.
 *
 * The Terran construction: the command Build 0x0c (ExecBuild 0x472d30 with its gate 0x47da50, ExecPlaceBuilding
 * 0x47d730, IssuePlacementOrder 0x47cc80), the SCV's orders PlaceBuilding 30 (0x41a580, the payment 0x41bcb0) and
 * ConstructingBuilding 33 (0x41a870) and the building's BuildTerran 44 (0x41a770). */
#include "kp_game_internal.h"

#include <string.h>

#define NO_TYPE 228
#define GRID_W 6
#define GRID_H 8
#define DAT_BUILDING 0x1
#define DAT_ADDON 0x2
#define DAT_RESOURCE_DEPOT 0x1000
#define DAT_RESOURCE 0x2000
#define DAT_REQUIRES_CREEP 0x20000
#define DAT_REQUIRES_PSI 0x80000
#define STATUS_COMPLETED 0x1
#define STATUS_GROUNDED_BUILDING 0x2
#define STATUS_GROUNDED_OR_AIR 0x6
#define STATUS_CLOAKED 0x300
#define STATUS_CAN_MOVE 0x20000
#define STATUS_COLLIDES 0x100000
#define STATUS_IGNORE_TILE_COLLISION 0x800000
#define SPRITE_HIDDEN 0x20
#define TILE_OCCUPIED 0x08000000
#define TILE_CREEP 0x00400000
#define TILE_CREEP_SEEN 0x40000000
#define TILE_BLOCKED 0x20800000 /* unbuildable and its doodad-blocked companion */
#define TILE_HIGH 0x80000000LL
#define CONTROLLER_HUMAN 2
#define NYDUS_CANAL 134
#define NEUTRAL_PLAYER 11
#define GEYSER 188
#define GEYSER_SPRITE 275
#define BUTTONS_GAS_CONSTRUCTION 231
#define IMAGE_GAS_CONSTRUCTION 345
#define SCV 7
#define REFINERY 110
#define EXTRACTOR 149
#define ASSIMILATOR 157
#define BEACON_FIRST 194 /* the beacons may stand on high tiles */
#define BEACON_LAST 199
#define IGNORED_A 105
#define IGNORED_B 202
#define DEPOT_MARGIN 3
#define ERR_OFF_MAP 3
#define ERR_OCCUPIED 4
#define ERR_UNSEEN 1
#define ERR_DEPOT 2
#define ERR_NO_CREEP 5
#define ERR_UNPOWERED 6
#define ERR_UNREACHABLE 7
#define ERR_NO_GEYSER 8
#define ERR_SEEN_BEFORE 9
#define ERR_UNEXPLORED 10
#define ORDER_CONSTRUCTING 33
#define ORDER_INCOMPLETE_BUILDING 44
#define ORDER_HARVEST1 79
#define ORDER_RESET_COLLISION 150
#define ORDER_NOTHING 23
#define ORDER_BUILDING_LAND 71
#define ORDER_BUILD_NYDUS_EXIT 46
#define ORDER_CTF_COP 155 /* the Capture the Flag beacon (0x47da50: until game time 600) */
#define ORDER_DRONE_BUILD 26
#define ORDER_DRONE_LAND 70
#define DRONE 41
#define STATUS_ORDER_LOCKS 0x3000
#define NEAR_SITE 128
#define WELD_NEAR 20
#define WELD_RANDOM 9   /* the RNG source of a welding spot */
#define WELD_TIME_RANDOM 10
#define WELD_TIME_MASK 0x3f
#define WELD_TIME_BASE 30
#define ANIM_GND_ATTK_TO_IDLE 8
#define ANIM_WALKING_TO_IDLE 12
#define ANIM_SPECIAL1 13
#define ANIM_SPECIAL2 14
#define ANIM_ALMOST_BUILT 15
#define STRING_CANNOT_BUILD 0x358
#define SOUND_WORKER_ERROR 0xa2 /* + race */
#define SOUND_ZERG_DONE 135
#define SOUND_SCV_DONE 136
#define SOUND_TERRAN_DONE 126

typedef uint8_t Cells[GRID_H][GRID_W];

static int64_t place_w(KpGame *g, int64_t t) { return kp_s16v(KP_DAT(g, units, staredit_placement_size, t * 2)); }
static int64_t place_h(KpGame *g, int64_t t) { return kp_s16v(KP_DAT(g, units, staredit_placement_size, t * 2 + 1)); }
static int64_t div32(int64_t v) { return v / 32; }  /* signed, toward zero */
static int64_t dat_flags(KpGame *g, int64_t t) { return KP_DAT(g, units, special_ability_flags, t); }
static int64_t tile(KpGame *g, int64_t tx, int64_t ty) { return g->terrain.tiles[ty * g->map_w + tx]; }
static int human(KpGame *g, int64_t p) { return kp_controller(g, p) == CONTROLLER_HUMAN; }
static int64_t sprite_x(KpGame *g, int64_t n) { return kp_s16v(g->w->spr.x[kp_unit_sprite(g, n)]); }
static int64_t sprite_y(KpGame *g, int64_t n) { return kp_s16v(g->w->spr.y[kp_unit_sprite(g, n)]); }
static int64_t ext(KpGame *g, int64_t t, int k) { return kp_s16v(KP_DAT(g, units, unit_extents, t * 4 + k)); }

/* The fog ghost of a resource near depot tile (x, y), within `margin` tiles of its box (0x4180a0): the ghosts of the
 * geyser sprites take a Geyser's placement size, those of the mineral fields their field's. */
static int fog_resource_near(KpGame *g, int64_t x, int64_t y, int64_t margin)
{
	KpImages *w = g->w;
	int32_t f;
	for (f = w->active_fog.head; f != KP_NONE; f = w->fog.next[f]) {
		int32_t s = kp_check_sprite(w, w->fog.sprite[f]);
		int64_t id = w->spr.id[s], t, pw, ph, left, top;
		if (id == 178 || id == 204 || id == 263 || id == GEYSER_SPRITE) t = GEYSER;
		else if (id >= 279 && id <= 281) t = id - 103;
		else continue;
		pw = place_w(g, t);
		ph = place_h(g, t);
		left = div32(kp_s16v(w->spr.x[s]) - pw / 2);
		top = div32(kp_s16v(w->spr.y[s]) - ph / 2);
		if (kp_u32v(x - left + margin) < kp_u32v(div32(pw) + margin * 2)
			&& kp_u32v(y - top + margin) < kp_u32v(div32(ph) + margin * 2)) {
			return 1;
		}
	}
	return 0;
}

/* A resource container among the units of the border query around depot tile (x, y) (0x442b60 with 0x413370). */
static int live_resource_near(KpGame *g, int64_t x, int64_t y)
{
	int64_t *found;
	int count, k, hit = 0;
	found = kp_query_rect(g, kp_s16v((x - DEPOT_MARGIN) * 32), kp_s16v((y - DEPOT_MARGIN) * 32),
		kp_s16v((x + DEPOT_MARGIN + 1) * 32), kp_s16v((y + DEPOT_MARGIN + 1) * 32), &count);
	for (k = 0; k < count && !hit; k++) hit = kp_band(dat_flags(g, g->u.type[found[k]]), DAT_RESOURCE) != 0;
	kp_query_release(g, found);
	return hit;
}

/* The ordinary tile grid 0x412870. */
static int64_t ordinary_grid(KpGame *g, int64_t t, int64_t tx, int64_t ty, int64_t p, int64_t w, int64_t h, Cells cells,
	int vision)
{
	int64_t high = human(g, p) ? kp_shlv(1, p + 8) : 0, low = human(g, p) ? kp_shlv(1, p) : 0, mask = TILE_BLOCKED;
	int64_t best = 0, r, c;
	if (kp_band(KP_DAT(g, units, staredit_group_flags, t), 1) == 0) mask = kp_bor(mask, vision ? TILE_CREEP_SEEN : TILE_CREEP);
	for (r = 0; r < h; r++) {
		for (c = 0; c < w; c++) {
			int64_t f = tile(g, tx + c, ty + r), code;
			if (kp_band(f, high) != 0) {
				code = ERR_UNEXPLORED;
			} else if ((!vision && kp_band(f, low) != 0) || kp_band(f, mask) == 0
				|| (t >= BEACON_FIRST && t <= BEACON_LAST && kp_band(f, TILE_HIGH) != 0)) {
				code = 0;
				if (kp_band(dat_flags(g, t), DAT_RESOURCE_DEPOT) != 0
					&& ((vision && fog_resource_near(g, tx + c, ty + r, DEPOT_MARGIN)) || live_resource_near(g, tx + c, ty + r))) {
					code = ERR_DEPOT;
				}
			} else {
				code = ERR_OCCUPIED;
			}
			cells[r][c] = (uint8_t)code;
			if (code > best) best = code;
		}
	}
	return best;
}

/* The creep grid 0x412ac0: an unexplored cell is 10, then a cell without creep (unless the player sees it) 5, the
 * return the greatest code met (a cell can hold 5 after its 10). */
static int64_t creep_grid(KpGame *g, int64_t tx, int64_t ty, int64_t p, int64_t w, int64_t h, Cells cells, int vision,
	int ignore)
{
	int64_t high = human(g, p) ? kp_shlv(1, p + 8) : 0, low = human(g, p) ? kp_shlv(1, p) : 0;
	int64_t need = vision ? TILE_CREEP_SEEN : TILE_CREEP, best = 0, r, c;
	for (r = 0; r < h; r++) {
		for (c = 0; c < w; c++) {
			int64_t f = tile(g, tx + c, ty + r), code = 0;
			if (kp_band(f, high) != 0) {
				code = ERR_UNEXPLORED;
				best = ERR_UNEXPLORED;
			}
			if ((ignore || vision || kp_band(f, low) == 0) && kp_band(f, need) == 0) {
				code = ERR_NO_CREEP;
				if (best < ERR_NO_CREEP) best = ERR_NO_CREEP;
			}
			cells[r][c] = (uint8_t)code;
		}
	}
	return best;
}

/* The Nydus exit grid 0x412720. */
static int64_t nydus_grid(KpGame *g, int64_t tx, int64_t ty, int64_t p, int64_t w, int64_t h, Cells cells, int vision)
{
	int64_t high = human(g, p) ? kp_shlv(1, p + 8) : 0, low = human(g, p) ? kp_shlv(1, p) : 0;
	int64_t need = vision ? TILE_CREEP_SEEN : TILE_CREEP, best = 0, r, c;
	for (r = 0; r < h; r++) {
		for (c = 0; c < w; c++) {
			int64_t f = tile(g, tx + c, ty + r), code;
			if (kp_band(f, high) != 0) code = ERR_UNEXPLORED;
			else if (vision && kp_band(f, low) != 0) code = ERR_SEEN_BEFORE;
			else code = kp_band(f, need) != 0 ? 0 : ERR_NO_CREEP;
			cells[r][c] = (uint8_t)code;
			if (code > best) best = code;
		}
	}
	return best;
}

/* GetUnitsContainingPoint 0x4221f0 then the first Geyser whose sprite lies in the point's tile (0x4132b0); 0 for
 * none. */
int64_t kp_geyser_at(KpGame *g, int64_t x, int64_t y)
{
	KpSpatial *sp = &g->spatial;
	KpUnits *u = &g->u;
	int64_t *found, hit = 0;
	int count, k;
	found = kp_query_rect(g, kp_s16v(x), kp_s16v(y), kp_s16v(x + sp->max_w), kp_s16v(y + sp->max_h), &count);
	for (k = 0; k < count && hit == 0; k++) {
		int64_t c = found[k];
		if (sp->x_at[u->bound_left[c]] > x || x >= sp->x_at[u->bound_right[c]] || sp->y_at[u->bound_top[c]] > y
			|| y >= sp->y_at[u->bound_bottom[c]]) {
			continue;
		}
		if (u->type[c] == GEYSER && div32(sprite_x(g, c)) == div32(kp_s16v(x)) && div32(sprite_y(g, c)) == div32(kp_s16v(y))) {
			hit = c;
		}
	}
	kp_query_release(g, found);
	return hit;
}

/* ConvertGeyserToBuilding 0x41b3f0: the worker's Geyser (its target, or the one at its order point) becomes its
 * owner's unfinished gas building of type t with a tenth of its hit points; 0 without a Geyser. The placement
 * preview's ghost images it takes off the Geyser (0x47cd70, 0x437050) are the interface's. */
int64_t kp_convert_geyser(KpGame *g, int64_t n, int64_t t)
{
	KpUnits *u = &g->u;
	int64_t geyser = u->order_unit[n];
	if (geyser == 0 || u->type[geyser] != GEYSER) geyser = kp_geyser_at(g, kp_s16v(u->order_x[n]), kp_s16v(u->order_y[n]));
	if (geyser == 0) return 0;
	kp_account_completed(g, geyser, -1, 1); /* 0x414580 */
	u->status[geyser] = kp_band(u->status[geyser], kp_bnot(STATUS_COMPLETED));
	kp_change_owner(g, geyser, u->owner[n], 0); /* 0x426ed0 */
	kp_set_sprite_owner(g, geyser, u->owner[n]); /* 0x426e80 */
	kp_change_unit_type(g, geyser, t);
	kp_set_button_set(g, geyser, BUTTONS_GAS_CONSTRUCTION);
	kp_sprite_image_below_main(g->w, kp_unit_sprite(g, geyser), IMAGE_GAS_CONSTRUCTION, 0, 0, 0);
	u->hp[geyser] = kp_udiv64(g, KP_DAT(g, units, hit_points, t), 10);
	return geyser;
}

/* The first fog ghost with the Geyser's sprite whose box (a Geyser's) holds the point, and whether its center is
 * the point (0x418000). */
static int fog_geyser_at(KpGame *g, int64_t x, int64_t y)
{
	KpImages *w = g->w;
	int64_t pw = place_w(g, GEYSER), ph = place_h(g, GEYSER);
	int32_t f;
	for (f = w->active_fog.head; f != KP_NONE; f = w->fog.next[f]) {
		int32_t s = kp_check_sprite(w, w->fog.sprite[f]);
		int64_t sx = kp_s16v(w->spr.x[s]), sy = kp_s16v(w->spr.y[s]);
		if (w->spr.id[s] != GEYSER_SPRITE || kp_fog_ignored_type(w->fog.kind[f])) continue;
		if (kp_u32v(pw / 2 + sx - x) < kp_u32v(pw) && kp_u32v(ph / 2 + sy - y) < kp_u32v(ph)) return sx == x && sy == y;
	}
	return 0;
}

/* The gas building's lookup 0x413610, over a Geyser's footprint at tile (tx, ty). */
static int64_t gas_lookup(KpGame *g, int64_t tx, int64_t ty, int64_t p, int vision)
{
	int64_t pw = place_w(g, GEYSER), ph = place_h(g, GEYSER), w = div32(pw), h = div32(ph), cx = pw / 2 + tx * 32;
	int64_t cy = ph / 2 + ty * 32, r, c;
	if (human(g, p)) {
		int all = 1;
		for (r = h - 1; r >= 0; r--) {
			for (c = w - 1; c >= 0; c--) {
				if (kp_band(tile(g, tx + c, ty + r), kp_shlv(1, p + 8)) != 0) return ERR_UNEXPLORED; /* 0x4134f0 */
			}
		}
		for (r = h - 1; r >= 0 && all; r--) {
			for (c = w - 1; c >= 0 && all; c--) all = kp_band(tile(g, tx + c, ty + r), kp_shlv(1, p)) != 0; /* 0x413580 */
		}
		if (all) {
			if (!vision) return 0;
			return fog_geyser_at(g, cx, cy) ? 0 : ERR_NO_GEYSER;
		}
	}
	return kp_geyser_at(g, cx, cy) ? 0 : ERR_NO_GEYSER;
}

/* The builder's or a building's border overlaps tile (tx, ty) strictly (the builder exception of 0x4122d0). */
static int border_overlaps(KpGame *g, int64_t n, int64_t tx, int64_t ty)
{
	KpSpatial *sp = &g->spatial;
	KpUnits *u = &g->u;
	return sp->x_at[u->bound_left[n]] < tx * 32 + 32 && tx * 32 < sp->x_at[u->bound_right[n]]
		&& sp->y_at[u->bound_top[n]] < ty * 32 + 32 && ty * 32 < sp->y_at[u->bound_bottom[n]];
}

/* 0x4122d0: 0 for a visible grounded builder (not a Nydus Canal) on the tile, else 1. */
static int not_builder_tile(KpGame *g, int64_t b, int64_t tx, int64_t ty)
{
	KpUnits *u = &g->u;
	return !(b != 0 && kp_band(u->status[b], STATUS_GROUNDED_BUILDING) != 0 && u->type[b] != NYDUS_CANAL
		&& kp_band(g->w->spr.flags[kp_unit_sprite(g, b)], SPRITE_HIDDEN) == 0 && border_overlaps(g, b, tx, ty));
}

/* 0x413390: the first unit of the list in the tile's way, 1 when the builder's owner cannot see it (cloaked), else
 * 4; 0 for none. */
static int64_t unit_in_tile(KpGame *g, int64_t b, const int64_t *list, int count, int64_t tx, int64_t ty, int mobile,
	int unseen)
{
	KpUnits *u = &g->u;
	int k;
	for (k = 0; k < count; k++) {
		int64_t c = list[k];
		int hidden = b != 0 && kp_band(u->status[c], STATUS_CLOAKED) != 0
			&& kp_band(u->visibility[c], kp_shlv(1, u->owner[b])) == 0;
		if (!mobile && kp_band(u->status[c], STATUS_CAN_MOVE) != 0) continue;
		if (c == b || kp_band(u->status[c], STATUS_GROUNDED_OR_AIR) != 0 || u->type[c] == IGNORED_B || u->type[c] == IGNORED_A) {
			continue;
		}
		if ((hidden && !unseen) || kp_band(g->w->spr.flags[kp_unit_sprite(g, c)], SPRITE_HIDDEN) != 0) continue;
		if (!border_overlaps(g, c, tx, ty)) continue;
		return hidden ? ERR_UNSEEN : ERR_OCCUPIED;
	}
	return 0;
}

/* The occupancy grid 0x412360 over the cells the earlier grid left at 0. */
static int64_t occupancy_grid(KpGame *g, int64_t b, int64_t tx, int64_t ty, int64_t p, int64_t t, int64_t w, int64_t h,
	Cells cells, int mobile, int unseen, int ignore)
{
	int64_t low = human(g, p) ? kp_shlv(1, p) : 0, cx = tx * 32 + place_w(g, t) / 2, cy = ty * 32 + place_h(g, t) / 2;
	int64_t best = 0, *found, r, c;
	int count;
	found = kp_query_rect(g, kp_s16v(cx - ext(g, t, 0)), kp_s16v(cy - ext(g, t, 1)), kp_s16v(cx + ext(g, t, 2)),
		kp_s16v(cy + ext(g, t, 3)), &count);
	for (r = 0; r < h; r++) {
		for (c = 0; c < w; c++) {
			int64_t f = tile(g, tx + c, ty + r), code;
			if (cells[r][c] != 0 || (!ignore && kp_band(f, low) != 0)) continue;
			if (kp_band(f, TILE_OCCUPIED) != 0 && not_builder_tile(g, b, tx + c, ty + r)) code = ERR_OCCUPIED;
			else code = unit_in_tile(g, b, found, count, tx + c, ty + r, mobile, unseen);
			cells[r][c] = (uint8_t)code;
			if (code > best) best = code;
		}
	}
	kp_query_release(g, found);
	return best;
}

static int64_t fill(Cells cells, int64_t code)
{
	memset(cells, (int)code, sizeof(Cells));
	return code;
}

/* ValidateBuildingPlacement 0x412f10: 0 when type t may stand at tile (tx, ty) for builder b of player p. */
int64_t kp_validate_placement(KpGame *g, int64_t b, int64_t p, int64_t tx, int64_t ty, int64_t t, int vision, int unseen,
	int ignore)
{
	Cells cells;
	int64_t w = div32(place_w(g, t)), h = div32(place_h(g, t)), flags = dat_flags(g, t), grid, occupancy;
	tx = kp_s16v(tx);
	ty = kp_s16v(ty);
	if (tx < 0 || ty < 0 || w + tx > g->map_w || h + ty > g->map_h) return fill(cells, ERR_OFF_MAP);
	if (b != 0 && kp_band(g->u.status[b], STATUS_GROUNDED_OR_AIR) == 0 && !kp_placement_reachable(g, b, tx * 32, ty * 32)) {
		return fill(cells, ERR_UNREACHABLE);
	}
	if (t == REFINERY || t == EXTRACTOR || t == ASSIMILATOR) return fill(cells, gas_lookup(g, tx, ty, p, vision));
	if (kp_band(flags, DAT_REQUIRES_PSI) != 0
		&& !kp_point_powered(g, t, tx * 32 + place_w(g, t) / 2, ty * 32 + place_h(g, t) / 2, p)) { /* 0x4130f0 */
		return fill(cells, ERR_UNPOWERED);
	}
	if (b != 0 && g->u.type[b] == NYDUS_CANAL) grid = nydus_grid(g, tx, ty, p, w, h, cells, vision);
	else if (kp_band(flags, DAT_REQUIRES_CREEP) != 0) grid = creep_grid(g, tx, ty, p, w, h, cells, vision, ignore);
	else grid = ordinary_grid(g, t, tx, ty, p, w, h, cells, vision);
	/* TODO: the builder override 0x6a2e80 of the AI's placement search (plan phase 7) */
	occupancy = occupancy_grid(g, b, tx, ty, p, t, w, h, cells, kp_band(flags, DAT_ADDON) == 0, unseen, ignore);
	return occupancy > grid ? occupancy : grid;
}

/* PlacementPhysicalGrid 0x412560: the terrain mask (unbuildable, doodad-blocked, occupied, creep for a non-Zerg type; a
 * beacon may stand on a high tile) and a resource depot's spacing from live resources. */
static int64_t physical_grid(KpGame *g, int64_t t, int64_t tx, int64_t ty, int64_t w, int64_t h)
{
	int64_t mask = kp_bor(TILE_BLOCKED, TILE_OCCUPIED), best = 0, r, c;
	if (kp_band(KP_DAT(g, units, staredit_group_flags, t), 1) == 0) mask = kp_bor(mask, TILE_CREEP);
	for (r = 0; r < h; r++) {
		for (c = 0; c < w; c++) {
			int64_t f = tile(g, tx + c, ty + r), code = 0;
			if (kp_band(f, mask) != 0 && !(t >= BEACON_FIRST && t <= BEACON_LAST && kp_band(f, TILE_HIGH) != 0)) {
				code = ERR_OCCUPIED;
			} else if (kp_band(dat_flags(g, t), DAT_RESOURCE_DEPOT) != 0 && live_resource_near(g, tx + c, ty + r)) {
				code = ERR_DEPOT;
			}
			if (code > best) best = code;
		}
	}
	return best;
}

/* ValidateGroundedRelocation 0x412e20: a building of the owner's of type t put (by a trigger) with its footprint at
 * tile (tx, ty): inside the map, the terrain, then the units over it (mobile ones only for a type that is not an
 * add-on). */
int64_t kp_validate_grounded(KpGame *g, int64_t owner, int64_t tx, int64_t ty, int64_t t)
{
	Cells cells;
	int64_t w = div32(place_w(g, t)), h = div32(place_h(g, t)), grid, occupancy;
	tx = kp_s16v(tx);
	ty = kp_s16v(ty);
	if (tx < 0 || ty < 0 || w + tx > g->map_w || h + ty > g->map_h) return ERR_OFF_MAP;
	grid = physical_grid(g, t, tx, ty, w, h);
	occupancy = occupancy_grid(g, 0, tx, ty, owner, t, w, h, cells, kp_band(dat_flags(g, t), DAT_ADDON) == 0, 1, 0);
	return occupancy > grid ? occupancy : grid;
}

/* A building's center (x, y) aligned to the tile grid by its placement box (0x4261f0, 0x4b7810): its left (top) edge,
 * kept within the map, rounded down to a tile, plus half the box again; `signed_edge` keeps a negative edge at 0
 * (creation), else it wraps as unsigned to the map's last pixel (a move). The tile of its footprint comes back too. */
void kp_snap_building(KpGame *g, int64_t t, int64_t *x, int64_t *y, int64_t *tx, int64_t *ty, int signed_edge)
{
	int64_t hw = kp_idiv64(g, place_w(g, t), 2), hh = kp_idiv64(g, place_h(g, t), 2), ex = kp_s32v(*x - hw);
	int64_t ey = kp_s32v(*y - hh);
	if (signed_edge && ex < 0) ex = 0;
	else if (kp_u32v(ex) >= g->map_w_px) ex = g->map_w_px - 1;
	if (signed_edge && ey < 0) ey = 0;
	else if (kp_u32v(ey) >= g->map_h_px) ey = g->map_h_px - 1;
	*x = kp_band(ex, ~(int64_t)31) + hw;
	*y = kp_band(ey, ~(int64_t)31) + hh;
	*tx = div32(*x) - div32(hw);
	*ty = div32(*y) - div32(hh);
}

/* ShowWorkerBuildError 0x4464e0: the race's error sound at the worker and the message, for its local owner. */
void kp_worker_error(KpGame *g, int64_t n, int64_t string)
{
	int64_t p = g->u.owner[n];
	if (p != g->local_player || g->w->replay) return;
	kp_play_unit_sound(g, SOUND_WORKER_ERROR + kp_unit_race(g, n), n);
	kp_report_error(g, p, string); /* the talking portrait is the presentation's */
}

/* PlayConstructionCompleteVoice 0x446280. */
void kp_construction_voice(KpGame *g, int64_t n)
{
	int64_t race;
	if (g->u.owner[n] != g->local_player || g->w->replay) return;
	race = kp_unit_race(g, n);
	if (race == 0) kp_play_unit_sound(g, SOUND_ZERG_DONE, n);
	else if (race == 1) kp_play_unit_sound(g, g->u.type[n] == SCV ? SOUND_SCV_DONE : SOUND_TERRAN_DONE, n);
}

/* The tile of the placement box whose center the order point is (signed, toward zero). */
void kp_site_tile(KpGame *g, int64_t t, int64_t x, int64_t y, int64_t *tx, int64_t *ty)
{
	*tx = div32(x - place_w(g, t) / 2);
	*ty = div32(y - place_h(g, t) / 2);
}

/* 0x41bcb0: the queued building is paid for when it still fits where the worker stands. */
int kp_pay_for_building(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t t = kp_queued_type(g, n), tx, ty;
	if (!kp_check_unit_cost(g, u->owner[n], t, 1, 1)) return 0;
	kp_site_tile(g, t, kp_s16v(u->order_x[n]), kp_s16v(u->order_y[n]), &tx, &ty);
	if (kp_validate_placement(g, n, u->owner[n], tx, ty, t, 0, 1, 0) != 0) {
		kp_worker_error(g, n, STRING_CANNOT_BUILD);
		return 0;
	}
	kp_deduct_cost(g, u->owner[n]);
	return 1;
}

/* AddBuildingToBuilderTown 0x486200: a computer's builder hands its building to its town. */
void kp_builder_town(KpGame *g, int64_t n)
{
	/* TODO: the town of a computer's builder (plan phase 7, the AIScript VM) */
	if (g->u.ai[n] != 0 && kp_controller(g, g->u.owner[n]) == 1) kp_game_fail(g, "construction: the builder's town (0x486200)");
}

/* OrderPlaceBuilding 0x41a580 (order 30): the SCV walks to the site, pays and starts the building it queued. */
void kp_order_place_building(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t t, b;
	if (u->ai[n] != 0) u->status[n] = kp_bor(u->status[n], STATUS_IGNORE_TILE_COLLISION);
	if (u->order_state[n] == 0) {
		if (u->order_unit[n] != 0) {
			u->order_x[n] = sprite_x(g, u->order_unit[n]);
			u->order_y[n] = sprite_y(g, u->order_unit[n]);
		}
		if (kp_request_move_to_point(g, n, kp_s16v(u->order_x[n]), kp_s16v(u->order_y[n]))) u->order_state[n] = 1;
		return;
	}
	if (u->order_state[n] != 1 || u->move_x[n] != sprite_x(g, n) || u->move_y[n] != sprite_y(g, n)) return;
	if (kp_move_arrival(g, n) == 2 || !kp_within_distance(g, n, NEAR_SITE, kp_s16v(u->order_x[n]), kp_s16v(u->order_y[n]))
		|| !kp_pay_for_building(g, n)) {
		kp_order_done(g, n);
		return;
	}
	u->status[n] = kp_band(u->status[n], kp_bnot(STATUS_COLLIDES));
	g->w->spr.elevation[kp_unit_sprite(g, n)] = (int32_t)KP_DAT(g, units, elevation_level, u->type[n]) + 1;
	if (u->queue_head[n] == KP_NONE) {
		kp_order_insert_after(g, n, ORDER_RESET_COLLISION, 0, 0, 0, NO_TYPE, KP_NONE);
		kp_order_insert_after(g, n, KP_DAT(g, units, return_to_idle, u->type[n]), 0, 0, 0, NO_TYPE, KP_NONE);
	} else {
		kp_order_insert_before(g, n, ORDER_RESET_COLLISION, 0, 0, 0, NO_TYPE, u->queue_head[n]);
	}
	t = kp_queued_type(g, n);
	b = t == REFINERY ? kp_convert_geyser(g, n, REFINERY)
		: kp_unit_create(g, t, kp_s16v(u->order_x[n]), kp_s16v(u->order_y[n]), u->owner[n]);
	*kp_queue_head(g, n) = NO_TYPE;
	if (b == 0) {
		kp_report_creation_error(g, u->owner[n]); /* 0x425020 */
		kp_issue_order(g, n, KP_DAT(g, units, return_to_idle, u->type[n]));
		return;
	}
	u->connected[b] = n;
	u->order[n] = ORDER_CONSTRUCTING;
	u->order_state[n] = 3;
	u->order_unit[n] = b;
	kp_placement_recheck(g); /* 0x47d070, 0x47db60, 0x47d300 (the interface refresh is the presentation's) */
	kp_issue_order(g, b, ORDER_INCOMPLETE_BUILDING);
	kp_builder_town(g, n);
}

/* A welding spot: x in [x0, x1], then y in [y0, y1] (RNG source 9), clamped into the map for the SCV. */
static void weld_spot(KpGame *g, int64_t n, int64_t x0, int64_t x1, int64_t y0, int64_t y1)
{
	KpUnits *u = &g->u;
	int64_t x, y;
	x = kp_s16v(kp_rng_game_range(g->rng, WELD_RANDOM, (uint32_t)x0, (uint32_t)x1));
	y = kp_s16v(kp_rng_game_range(g->rng, WELD_RANDOM, (uint32_t)y0, (uint32_t)y1));
	kp_clamp_to_map(g, u->type[n], &x, &y);
	u->order_x[n] = x;
	u->order_y[n] = y;
}

/* A worker turned to its facing waypoint within its ground weapon's attack angle, or standing on the waypoint
 * (0x40ba70; the welding and repair orders). */
int kp_facing_waypoint(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t d;
	if (u->target_waypoint_x[n] == u->x[n] && u->target_waypoint_y[n] == u->y[n]) return 1;
	d = kp_mod(u->heading[n] - kp_direction_to(g, sprite_x(g, n), sprite_y(g, n), u->target_waypoint_x[n],
		u->target_waypoint_y[n]), 256);
	if (d > 128) d = 256 - d;
	return KP_DAT(g, weapons, attack_angle, kp_ground_weapon(g, n)) >= d;
}

/* OrderConstructingBuilding 0x41a870 (order 33): the SCV takes the building over, walks to it and welds at random
 * spots of its sprite while the building grows; then it harvests a finished gas building or finishes. */
void kp_order_constructing_building(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	KpImages *w = g->w;
	int64_t b = u->order_unit[n];
	if (b == 0 || kp_band(u->status[b], STATUS_COMPLETED) != 0) goto finish;
	switch (u->order_state[n]) {
	case 0:
		if (!kp_request_move_to_unit(g, n, b)) return;
		u->order_state[n] = 2;
		/* fall through */
	case 2: {
		int64_t arrival = kp_move_arrival(g, n), other;
		if (arrival == 0) return;
		other = u->connected[b];
		if (arrival == 2 || (other != 0 && other != n && u->order_unit[other] == b)) {
			kp_order_done(g, n);
			return;
		}
		u->connected[b] = n;
		u->status[n] = kp_band(u->status[n], kp_bnot(STATUS_COLLIDES));
		w->spr.elevation[kp_unit_sprite(g, n)] = (int32_t)KP_DAT(g, units, elevation_level, u->type[n]) + 1;
		kp_order_append(g, n, ORDER_RESET_COLLISION, 0, 0, 0, NO_TYPE, 0);
		kp_order_append(g, n, KP_DAT(g, units, return_to_idle, u->type[n]), 0, 0, 0, NO_TYPE, 0);
		if (kp_request_move_to_point(g, n, sprite_x(g, b), sprite_y(g, b))) u->order_state[n] = 3;
		return;
	}
	case 3:
		if (kp_move_arrival(g, n) != 0) u->order_state[n] = 4;
		return;
	case 4: {
		int32_t s = kp_unit_sprite(g, b);
		int64_t sw = w->spr.width[s], sh = w->spr.height[s], bx = sprite_x(g, b), by = sprite_y(g, b), t = u->type[n];
		int64_t left = kp_s16v(bx - kp_shrv(sw, 1)), right = kp_s16v(left + sw - 1), top = kp_s16v(by - kp_shrv(sh, 1));
		int64_t bottom = kp_s16v(top + sh - 1), px = kp_s16v(bx - place_w(g, u->type[b]) / 2);
		int64_t py = kp_s16v(by - place_h(g, u->type[b]) / 2), x0, x1, y0, y1;
		weld_spot(g, n, left, right, top, bottom);
		if (kp_terrain_sweep_collision(g, n, u->order_x[n] - sprite_x(g, n), u->order_y[n] - sprite_y(g, n))) {
			/* the footprint inset by the SCV's own extents, within the sprite */
			x0 = kp_s16v(px + ext(g, t, 0) + 1);
			x1 = kp_s16v(px + place_w(g, u->type[b]) - 2 - ext(g, t, 2));
			y0 = kp_s16v(py + ext(g, t, 1) + 1);
			y1 = kp_s16v(py + place_h(g, u->type[b]) - 2 - ext(g, t, 3));
			weld_spot(g, n, x0 < left ? left : x0, x1 > right ? right : x1, y0 < top ? top : y0, y1 > bottom ? bottom : y1);
		}
		if (!kp_request_move_to_point(g, n, u->order_x[n], u->order_y[n])) return;
		u->order_state[n] = 5;
		break;
	}
	case 5:
		u->order_state[n] = 6;
		break;
	case 6:
		if (kp_within_distance(g, n, WELD_NEAR, kp_s16v(u->order_x[n]), kp_s16v(u->order_y[n]))) {
			int64_t dx, dy;
			kp_stop_movement(g, n);
			kp_flingy_set_target_waypoint(g, n, sprite_x(g, b), sprite_y(g, b));
			kp_direction_vector(kp_direction_to(g, sprite_x(g, n), sprite_y(g, n), u->target_waypoint_x[n],
				u->target_waypoint_y[n]), WELD_NEAR, &dx, &dy);
			u->order_x[n] = kp_s16v(sprite_x(g, n) + dx);
			u->order_y[n] = kp_s16v(sprite_y(g, n) + dy);
			u->order_state[n] = 7;
		}
		break;
	case 7:
		if (!kp_facing_waypoint(g, n)) break;
		u->order_timer[n] = kp_band(kp_rng_game_raw(g->rng, WELD_TIME_RANDOM), WELD_TIME_MASK) + WELD_TIME_BASE;
		kp_sprite_play(w, kp_unit_sprite(g, n), ANIM_ALMOST_BUILT, 1);
		u->order_state[n] = 8;
		break;
	case 8:
		if (u->order_timer[n] == 0) {
			kp_sprite_play(w, kp_unit_sprite(g, n), ANIM_GND_ATTK_TO_IDLE, 1);
			u->order_state[n] = 4;
		}
		break;
	}
	kp_progress_construction(g, b, kp_build_hp_gain(g, b), 0);
	if (kp_band(u->status[b], STATUS_COMPLETED) == 0) return;
finish:
	kp_sprite_play(w, kp_unit_sprite(g, n), ANIM_WALKING_TO_IDLE, 1);
	if (b != 0) {
		u->connected[b] = 0;
		kp_construction_voice(g, n);
		if ((kp_band(u->status[b], STATUS_COMPLETED) != 0 && kp_gas_building(u->type[b]) && u->owner[b] == u->owner[n])
			|| kp_mineral_field(u->type[b])) {
			kp_issue_order_target(g, n, ORDER_HARVEST1, b);
			return;
		}
	}
	kp_order_done(g, n);
}

/* BuildTerran 0x41a770 (order 44): the building's construction images as its hit points pass 1/5, 2/5, 3/5 and
 * 4/5 of the maximum (unsigned arithmetic). */
void kp_order_build_terran(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t max = kp_u32v(KP_DAT(g, units, hit_points, u->type[n])), hp = kp_s32v(u->hp[n]);
	int32_t s = kp_unit_sprite(g, n);
	switch (u->order_state[n]) {
	case 0: if (kp_s32v(max / 5) < hp) u->order_state[n] = 1; return;
	case 1: kp_sprite_play(g->w, s, ANIM_SPECIAL1, 1); u->order_state[n] = 2; return;
	case 2: if (kp_s32v(kp_u32v(max * 2) / 5) < hp) u->order_state[n] = 3; return;
	case 3: kp_sprite_play(g->w, s, ANIM_SPECIAL2, 1); u->order_state[n] = 4; return;
	case 4: if (kp_s32v(kp_u32v(max * 3) / 5) < hp) u->order_state[n] = 5; return;
	case 5:
		kp_replace_unit_images(g, n, 0);
		kp_sprite_play(g->w, s, ANIM_ALMOST_BUILT, 1);
		u->order_state[n] = 6;
		return;
	case 6: if (kp_s32v(kp_u32v(max * 4) / 5) < hp) kp_issue_order(g, n, ORDER_NOTHING); return;
	}
}

/* IssuePlacementOrder 0x47cc80: the builder goes to the placed point; a paid-for building type is queued (a building
 * builder keeps the type in +0xc4). */
void kp_issue_placement(KpGame *g, int64_t order, int64_t n, int64_t x, int64_t y, int64_t t)
{
	KpUnits *u = &g->u;
	if (order == ORDER_BUILDING_LAND) {
		kp_issue_order_at(g, n, order, x, y);
		return;
	}
	if (u->type[n] == DRONE && ((u->order[n] == ORDER_DRONE_LAND && kp_band(u->status[n], STATUS_ORDER_LOCKS) != 0)
		|| u->order[n] == ORDER_DRONE_BUILD)) {
		return;
	}
	if ((u->type[n] == NYDUS_CANAL && order == ORDER_BUILD_NYDUS_EXIT) || order == ORDER_CTF_COP) {
		kp_issue_order_at(g, n, order, x, y);
		return;
	}
	if (!kp_check_unit_cost(g, u->owner[n], t, 1, 1)) return;
	kp_issue_order_at(g, n, order, x, y);
	kp_cancel_all_queued(g, n);
	if (kp_band(dat_flags(g, u->type[n]), DAT_BUILDING) != 0 && kp_band(dat_flags(g, t), DAT_BUILDING) != 0) {
		kp_union_set(g, n, 0xc4, 2, t);
	} else {
		kp_enqueue_unit(g, n, t);
	}
}

/* The placement command's gate 0x47da50: the builder may make type t with the order. */
int kp_may_place(KpGame *g, int64_t order, int64_t t, int64_t player, int64_t n)
{
	switch (order) {
	case 0x19: case 0x1e: case 0x1f: case 0x20: case 0x24:
		if (kp_can_train_unit(g, t, player, n) != 1) return 0;
		break;
	case ORDER_CTF_COP:
		if (kp_scalar(g, KP_ELAPSED) > 600) return 0;
		/* fall through */
	case ORDER_BUILD_NYDUS_EXIT: case ORDER_BUILDING_LAND:
		if (g->u.type[n] != t) return 0;
		break;
	default:
		return 0;
	}
	return kp_can_issue_order(g, order, player, n) == 1;
}

/* ExecBuild 0x472d30 (command 0x0c): the only selected unit builds type t at tile (tx, ty) (ExecPlacementCommand
 * 0x47d5f0, ExecPlaceBuilding 0x47d730); a refusal's message is the interface's. */
void kp_command_build(KpGame *g, int64_t player, int64_t order, int64_t tx, int64_t ty, int64_t t, int64_t n)
{
	if (n == 0 || kp_u32v(tx) >= g->map_w || kp_u32v(ty) >= g->map_h || !kp_may_place(g, order, t, player, n)) return;
	if (kp_band(dat_flags(g, t), DAT_ADDON) != 0) {
		kp_command_place_addon(g, player, order, tx, ty, t, n); /* 0x47d5f0 */
		return;
	}
	if (kp_validate_placement(g, n, player, tx, ty, t, 0, 0, 0) != 0) return;
	kp_issue_placement(g, order, n, kp_s16v(place_w(g, t) / 2 + tx * 32), kp_s16v(place_h(g, t) / 2 + ty * 32), t);
}

/* AddonCandidateCallback 0x4317d0: a neutral add-on of building b's type whose parent tile is b's. */
static int attachable(KpGame *g, int64_t a, int64_t b)
{
	KpUnits *u = &g->u;
	int64_t at = u->type[a], bt = u->type[b], ax, ay;
	if (kp_band(dat_flags(g, at), DAT_ADDON) == 0 || u->owner[a] != NEUTRAL_PLAYER || kp_required_parent_type(g, at) != bt) {
		return 0;
	}
	ax = kp_s16v(sprite_x(g, a) - place_w(g, at) / 2) - kp_s16v(KP_DAT(g, units, addon_position, at * 2));
	ay = kp_s16v(sprite_y(g, a) - place_h(g, at) / 2) - kp_s16v(KP_DAT(g, units, addon_position, at * 2 + 1));
	return div32(kp_s16v(sprite_x(g, b) - place_w(g, bt) / 2)) == div32(ax)
		&& div32(kp_s16v(sprite_y(g, b) - place_h(g, bt) / 2)) == div32(ay);
}

/* FindAttachableAddon 0x431ae0: the first such add-on in the border query a tile around building b's box. */
int64_t kp_find_attachable_addon(KpGame *g, int64_t b)
{
	int64_t t = g->u.type[b], left = kp_s16v(sprite_x(g, b) - place_w(g, t) / 2 - 32), top = kp_s16v(sprite_y(g, b) - place_h(g, t) / 2 - 32);
	int64_t *found, a = 0;
	int count, k;
	found = kp_query_rect(g, left, top, kp_s16v(left + 64 + place_w(g, t)), kp_s16v(top + 64 + place_h(g, t)), &count);
	for (k = 0; k < count && a == 0; k++) {
		if (attachable(g, found[k], b)) a = found[k];
	}
	kp_query_release(g, found);
	return a;
}
