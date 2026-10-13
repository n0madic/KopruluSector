/* Group movement of the right-click command (kp_game.h; game/units/formation.lua, the
 * specification): group_formation and formation_point. The destination adjustments belong to
 * the pathing of N.4 and run in Lua until then. */
#include "kp_game_internal.h"

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

/* A Lua call returning two numbers (a point). */
static void lua_point(KpGame *g, const char *method, int argc, const int64_t *args, int64_t *x, int64_t *y)
{
	int64_t res[2];
	g->lua(g, method, argc, args, 2, res);
	*x = res[0];
	*y = res[1];
}

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
		int64_t args[6];
		if (count == 1) {
			args[0] = units[0];
			args[1] = f->ground_x;
			args[2] = f->ground_y;
			lua_point(g, "adjust_move_destination", 3, args, &f->ground_x, &f->ground_y);
		} else {
			args[0] = f->ground_x;
			args[1] = f->ground_y;
			args[2] = f->radius;
			args[3] = f->radius;
			args[4] = cx;
			args[5] = cy;
			lua_point(g, "adjust_group_destination", 6, args, &f->ground_x, &f->ground_y);
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

static int64_t region_at(KpGame *g, int64_t x, int64_t y) { return KP_LUA2(g, "region_at", x, y); }

/* The ground neighbours of region r contain `other`, or it is r itself (0x49c270). */
static int same_or_neighbor(KpGame *g, int64_t r, int64_t other)
{
	int64_t neighbors[KP_LIST_MAX], args[1];
	int count, k;
	if (r == other) return 1;
	args[0] = r;
	count = g->list(g, "region_ground_neighbors", 1, args, neighbors, KP_LIST_MAX);
	for (k = 0; k < count; k++) {
		if (neighbors[k] == other) return 1;
	}
	return 0;
}

static int type_fits(KpGame *g, int64_t t, int64_t x, int64_t y) { return KP_LUA3(g, "unit_type_fits", t, x, y) == 1; }

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
	goal_region = region_at(g, gx, gy);
	if (type_fits(g, t, ox, oy) && same_or_neighbor(g, goal_region, region_at(g, ox, oy))) {
		*rx = ox;
		*ry = oy;
		return;
	}
	d = KP_LUA4(g, "approx_distance", ox, oy, gx, gy);
	if (d == 0) d = 1;
	step = kp_idiv64(g, d, 4);
	if (step < 2) step = d;
	if (step > MAX_STEPS) step = MAX_STEPS;
	for (k = step; k <= d; k += step) {
		int64_t px = kp_s16v(kp_idiv64(g, (gx - ox) * k, d) + ox);
		int64_t py = kp_s16v(kp_idiv64(g, (gy - oy) * k, d) + oy);
		if (type_fits(g, t, px, py) && same_or_neighbor(g, goal_region, region_at(g, px, py))) {
			*rx = px;
			*ry = py;
			return;
		}
	}
	*rx = gx;
	*ry = gy;
}
