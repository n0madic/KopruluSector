/* Group movement of the right-click command (kp_game.h, kp_pathing.h; game/units/formation.lua, the
 * specification): group_formation, formation_point and the destination adjustments. */
#include "kp_pathing_internal.h"

#include <math.h>

#define GROUND 1
#define OFFSET_X 1
#define OFFSET_Y 2
#define CONVERGE 4
#define GROUND_GROUP 8
#define AREA_SCALE 2.0
#define CONVERGE_MARGIN 0x20
#define GROUND_SPREAD 0xc0
#define AIR_SPREAD 0x100
#define MAX_STEPS 0x10

static int64_t min64(int64_t a, int64_t b) { return a < b ? a : b; }
static int64_t max64(int64_t a, int64_t b) { return a > b ? a : b; }

void kp_group_formation(KpGame *g, int64_t x, int64_t y, int adjust, KpFormation *f)
{
	KpUnits *u = &g->u;
	int64_t units[KP_SLOTS], sum_x = 0, sum_y = 0, min_x = 0x7fffffff, min_y = 0x7fffffff, max_x = 0, max_y = 0, cx, cy;
	int ground = 0, count = 0, total, k, spread;
	f->x = f->goal_x = f->ground_x = x;
	f->y = f->goal_y = f->ground_y = y;
	f->dx = f->dy = f->area = f->radius = f->flags = 0;
	total = kp_selected_units(g, kp_scalar(g, KP_COMMAND_PLAYER), units);
	for (k = 0; k < total; k++) {
		int64_t n = units[k], s = u->sprite[n];
		if (s != -1) {
			int64_t sx, sy, t, h, w;
			if (kp_band(u->pathing_flags[n], GROUND) != 0) ground = 1;
			count++;
			sx = kp_s16v(g->w->spr.x[kp_check_sprite(g->w, (int32_t)s)]);
			sy = kp_s16v(g->w->spr.y[s]);
			sum_x += sx;
			sum_y += sy;
			min_x = min64(min_x, sx);
			max_x = max64(max_x, sx);
			min_y = min64(min_y, sy);
			max_y = max64(max_y, sy);
			t = u->type[n];
			h = kp_s16v(KP_DAT(g, units, unit_extents, t * 4 + 1) + 1 + KP_DAT(g, units, unit_extents, t * 4 + 3));
			w = kp_s16v(KP_DAT(g, units, unit_extents, t * 4 + 2) + 1 + KP_DAT(g, units, unit_extents, t * 4));
			f->area = kp_s32v(f->area + h * w);
		}
	}
	if (count == 0) return;
	f->radius = kp_mod((int64_t)floor(sqrt((double)f->area * AREA_SCALE)), 65536);
	/* The centroid uses unsigned division of the coordinate sums. */
	cx = kp_s16v(kp_udiv64(g, sum_x, count));
	cy = kp_s16v(kp_udiv64(g, sum_y, count));
	if (ground && adjust) {
		if (count == 1) {
			kp_adjust_move_destination(g, units[0], f->ground_x, f->ground_y, &f->ground_x, &f->ground_y);
		} else {
			kp_adjust_group_destination(g, f->ground_x, f->ground_y, f->radius, f->radius, cx, cy, &f->ground_x,
				&f->ground_y);
		}
		f->ground_x = max64(0, min64(f->ground_x, g->map_w_px - 1)); /* ClampPointToMapPixels 0x49c210 */
		f->ground_y = max64(0, min64(f->ground_y, g->map_h_px - 1));
	}
	if (count < 2) return;
	if (x <= max_x && min_x <= x && y <= max_y && min_y <= y) {
		f->flags = CONVERGE;
		f->dx = f->dy = 0;
		return;
	}
	spread = AIR_SPREAD;
	if (ground) {
		f->flags = kp_bor(f->flags, GROUND_GROUP);
		spread = GROUND_SPREAD;
	}
	if (max_x - min_x <= spread && max_y - min_y <= spread) {
		f->dx = kp_s16v(f->goal_x - cx);
		f->dy = kp_s16v(f->goal_y - cy);
		f->flags = kp_bor(f->flags, OFFSET_X + OFFSET_Y);
	}
}

/* The ground neighbours of region r contain `other`, or it is r itself (0x49c270). */
static int same_or_neighbor(KpGame *g, int64_t r, int64_t other)
{
	int64_t neighbors[256];
	int count, k;
	if (r == other) return 1;
	count = kp_region_ground_neighbors(g, r, neighbors, 256);
	for (k = 0; k < count; k++) {
		if (neighbors[k] == other) return 1;
	}
	return 0;
}

void kp_formation_point(KpGame *g, int64_t n, const KpFormation *f, int64_t *rx, int64_t *ry)
{
	KpUnits *u = &g->u;
	int32_t s = kp_unit_sprite(g, n);
	int64_t sx = kp_s16v(g->w->spr.x[s]), sy = kp_s16v(g->w->spr.y[s]), gx = f->goal_x, gy = f->goal_y;
	int64_t ox = f->x, oy = f->y, t, goal_region, d, step, k;
	int ground = kp_band(u->pathing_flags[n], GROUND) != 0;
	if (ground) {
		gx = f->ground_x;
		gy = f->ground_y;
	}
	if (kp_band(f->flags, CONVERGE) != 0) {
		ox = sx < gx ? max64(gx - CONVERGE_MARGIN, sx) : min64(gx + CONVERGE_MARGIN, sx);
		oy = sy < gy ? max64(gy - CONVERGE_MARGIN, sy) : min64(gy + CONVERGE_MARGIN, sy);
	} else {
		if (kp_band(f->flags, OFFSET_X) != 0) ox = kp_s16v(sx + f->dx);
		if (kp_band(f->flags, OFFSET_Y) != 0) oy = kp_s16v(sy + f->dy);
	}
	t = u->type[n];
	kp_clamp_to_map(g, t, &ox, &oy);
	if (!ground) {
		*rx = ox;
		*ry = oy;
		return;
	}
	goal_region = kp_region_at(g, gx, gy);
	if (kp_unit_type_fits(g, t, ox, oy) && same_or_neighbor(g, goal_region, kp_region_at(g, ox, oy))) {
		*rx = ox;
		*ry = oy;
		return;
	}
	d = kp_approx_distance(ox - gx, oy - gy);
	if (d == 0) d = 1;
	step = kp_idiv64(g, d, 4);
	if (step < 2) step = d;
	if (step > MAX_STEPS) step = MAX_STEPS;
	for (k = step; k <= d; k += step) {
		int64_t px = kp_s16v(kp_idiv64(g, (gx - ox) * k, d) + ox);
		int64_t py = kp_s16v(kp_idiv64(g, (gy - oy) * k, d) + oy);
		if (kp_unit_type_fits(g, t, px, py) && same_or_neighbor(g, goal_region, kp_region_at(g, px, py))) {
			*rx = px;
			*ry = py;
			return;
		}
	}
	*rx = gx;
	*ry = gy;
}

/* AdjustDestinationToReachableFit 0x42c9a0. box: {left, up, right, down}. */
void kp_adjust_destination_fit(KpGame *g, int64_t x, int64_t y, const int64_t *box, int64_t ox, int64_t oy, int64_t *rx,
	int64_t *ry)
{
	KpRoute c;
	int walkable = kp_point_walkable(g, x, y), has_region = 0;
	int64_t region = 0;
	kp_set_box_extents(g->pathing.extent_ctx, box[0], box[1], box[2], box[3]); /* SetBoxExtents 0x489270 */
	if (walkable) {
		region = kp_tile_region_first(g, x, y);
		has_region = 1;
		if (kp_region_group_of(g, kp_tile_region_first(g, ox, oy)) != kp_region_group_of(g, region)) has_region = 0;
	}
	if (!has_region) region = kp_reachable_substitute_region(g, ox, oy, x, y);
	kp_route_new(&c, 0, 0, 0, 0, 0);
	c.goal_x = x;
	c.goal_y = y;
	kp_snap_goal_into_region(g, &c, region);
	c.ext_left = kp_s16v(-box[0]);
	c.ext_up = kp_s16v(-box[1]);
	c.ext_right = kp_s16v(box[2]);
	c.ext_down = kp_s16v(box[3]);
	kp_footprint_near(g, &c, c.goal_x, c.goal_y, rx, ry);
}

/* AdjustUnitMoveDestination 0x42c8c0. */
void kp_adjust_move_destination(KpGame *g, int64_t n, int64_t x, int64_t y, int64_t *rx, int64_t *ry)
{
	int64_t t = g->u.type[kp_game_unit(g, n)], box[4], sx, sy;
	if (kp_unit_type_fits(g, t, x, y)) {
		*rx = x;
		*ry = y;
		return;
	}
	box[0] = kp_s16v(KP_DAT(g, units, unit_extents, t * 4));
	box[1] = kp_s16v(KP_DAT(g, units, unit_extents, t * 4 + 1));
	box[2] = kp_s16v(KP_DAT(g, units, unit_extents, t * 4 + 2));
	box[3] = kp_s16v(KP_DAT(g, units, unit_extents, t * 4 + 3));
	kp_sprite_pos(g, n, &sx, &sy);
	kp_adjust_destination_fit(g, x, y, box, sx, sy, rx, ry);
}

/* AdjustGroupMoveDestination 0x42c930. */
void kp_adjust_group_destination(KpGame *g, int64_t x, int64_t y, int64_t w, int64_t h, int64_t ox, int64_t oy,
	int64_t *rx, int64_t *ry)
{
	int64_t hw, hh, box[4];
	if (kp_box_fits(g, x, y, w, h)) {
		*rx = x;
		*ry = y;
		return;
	}
	hw = kp_idiv64(g, w, 2);
	hh = kp_idiv64(g, h, 2);
	box[0] = hw;
	box[1] = hh;
	box[2] = w - hw;
	box[3] = h - hh;
	kp_adjust_destination_fit(g, x, y, box, ox, oy, rx, ry);
}
