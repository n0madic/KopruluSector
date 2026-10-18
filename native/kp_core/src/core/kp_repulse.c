/* The air repulsion of flying units (kp_collision.h): flyers
 * count themselves in a grid of 48-pixel cells (AddAirDensity 0x481760, RemoveAirDensity 0x481830); a flyer
 * in a cell with other flyers drifts away along its repulse angle (ApplyAirRepulsion 0x481880,
 * UpdateRepulseAngle 0x4814e0) and settles where it drifted (UpdateAirRepulsion 0x4815d0). The grid is
 * g->collision.density (0x67cf60, 0xab cells per row). */
#include "kp_game_internal.h"

#include <string.h>

#define CELL 48
#define RANDOM_DENSITY 37 /* RandomizeRepulse source of 0x481760 */
#define CAN_MOVE 0x20000
#define STATUS_LOCKS 0x3000
#define DAT_BUILDING 1
#define EDGE 0x20   /* the margin inside the map edges that turns the repulse angle back */
#define SETTLE 0x18 /* the distance below which the move target follows the drift */
#define TIMER 7     /* the low bits of +0x14c: steps until the repulse angle is recomputed */

/* The drift bias of each repulse profile (the high nibble of +0x14c), 0x508e68. */
static const int64_t BIAS[16][2] = {
	{ -5, -5 }, { -5, 5 }, { 5, -5 }, { 5, 5 }, { 5, -5 }, { 5, 5 }, { -5, -5 }, { -5, 5 },
	{ -5, 5 }, { 5, -5 }, { -5, -5 }, { 5, 5 }, { -5, 5 }, { 5, -5 }, { 5, 5 }, { -5, -5 },
};

/* The repulse angle jitter 0x67cf68 (InitRepulseAngleJitter 0x4816f0): the offset from the target direction
 * for each angle difference k (0..255), 0x20..0x3f on one side and 0xc0..0xdf on the other. */
static int64_t jitter(int64_t k) { return k < 0x80 ? kp_mod(k, 0x20) + 0x20 : kp_mod(k, 0x20) + 0xc0; }

/* Signed division by two rounding towards zero (cdq; sub; sar). */
static int64_t half(KpGame *g, int64_t v) { return kp_idiv64(g, v, 2); }

/* The drift d moves away from zero by the bias b while it is small and towards zero once it passes 4b. */
static int64_t biased(int64_t d, int64_t b)
{
	if (d > b * 4) return d - b;
	if (d > 0) return d + b;
	if (d < -b * 4) return d + b;
	if (d < 0) return d - b;
	return d;
}

void kp_collision_init(KpGame *g)
{
	memset(g->collision.density, 0, sizeof g->collision.density);
}

/* Flyers that take part in the repulsion: movable non-building units except Interceptors. */
static int repelled(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t t;
	if (kp_band(u->status[kp_game_unit(g, n)], CAN_MOVE) == 0) return 0;
	t = u->type[n];
	return kp_band(KP_DAT(g, units, special_ability_flags, t), DAT_BUILDING) == 0 && t != KP_TYPE_INTERCEPTOR;
}

/* The density cell of unit n (the grid holds every cell repulse_x/y 0..255 give; another fails). */
static int64_t *cell(KpGame *g, int64_t n)
{
	int64_t c = g->u.repulse_x[n] + g->u.repulse_y[n] * KP_DENSITY_COLUMNS;
	if (c < 0 || c >= KP_DENSITY_CELLS) kp_game_fail(g, "repulse: density cell %lld out of range", (long long)c);
	return &g->collision.density[c];
}

/* AddAirDensity 0x481760: a flyer without a repulse profile draws one. */
void kp_air_density_add(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int32_t s;
	int64_t *v;
	if (!repelled(g, n)) return;
	if (kp_band(u->repulse_flags[n], 0xf0) == 0) {
		int64_t r = (int64_t)kp_rng_game_raw(g->rng, RANDOM_DENSITY), high;
		u->repulse_angle[n] = kp_mod(r, 256);
		high = kp_band(kp_shrv(r, 8), 0xf0);
		if (high == 0) high = 0xf0;
		u->repulse_flags[n] = kp_bor(u->repulse_flags[n], high);
	}
	u->repulse_flags[n] = kp_band(u->repulse_flags[n], 0xf8);
	s = kp_unit_sprite(g, n);
	u->repulse_x[n] = kp_mod(kp_idiv64(g, kp_s16v(g->w->spr.x[s]), CELL), 256);
	u->repulse_y[n] = kp_mod(kp_idiv64(g, kp_s16v(g->w->spr.y[s]), CELL), 256);
	v = cell(g, n);
	if (*v != 0xff) *v = *v + 1;
}

/* RemoveAirDensity 0x481830. */
void kp_air_density_remove(KpGame *g, int64_t n)
{
	int64_t *v;
	if (!repelled(g, n)) return;
	v = cell(g, n);
	if (*v != 0) *v = *v - 1;
}

/* UpdateRepulseAngle 0x4814e0: the angle turns to the jittered direction of the move target and away from
 * the map edges; near an edge the profile is cleared. */
static void update_angle(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int32_t s = kp_unit_sprite(g, n);
	int64_t sx = kp_s16v(g->w->spr.x[s]), sy = kp_s16v(g->w->spr.y[s]);
	int64_t a = u->repulse_angle[n], t;
	int near_x = -1, near_y = -1; /* -1 for none */
	if (sx != u->move_x[n] || sy != u->move_y[n]) {
		int64_t d = kp_direction_to(g, sx, sy, u->move_x[n], u->move_y[n]);
		a = kp_mod(jitter(kp_mod(a - d, 256)) + d, 256);
	}
	t = u->type[n];
	if (sx < kp_s16v(KP_DAT(g, units, unit_extents, t * 4)) + EDGE) {
		near_x = kp_s8((uint32_t)a) < 0;
	} else if (sx > g->map_w_px - kp_s16v(KP_DAT(g, units, unit_extents, t * 4 + 2)) - EDGE - 1) {
		near_x = kp_s8((uint32_t)a) >= 0;
	}
	if (near_x != -1) {
		if (near_x) a = kp_mod(-a, 256);
		u->repulse_flags[n] = kp_band(u->repulse_flags[n], 0x0f);
	}
	if (sy < kp_s16v(KP_DAT(g, units, unit_extents, t * 4 + 1)) + EDGE) {
		near_y = kp_s8((uint32_t)(a + 0x40)) >= 0;
	} else if (sy > g->map_h_px - kp_s16v(KP_DAT(g, units, unit_extents, t * 4 + 3)) - EDGE - 1) {
		near_y = kp_s8((uint32_t)(a + 0x40)) < 0;
	}
	if (near_y != -1) {
		a = kp_mod(a + 0x40, 256);
		if (near_y) a = kp_mod(-a, 256);
		a = kp_mod(a - 0x40, 256);
		u->repulse_flags[n] = kp_band(u->repulse_flags[n], 0x0f);
	}
	u->repulse_angle[n] = a;
}

/* ApplyAirRepulsion 0x481880: a flyer sharing its cell drifts the prepared step along its repulse angle
 * (further in a crowded cell once it reached its move target); true when it drifted. */
int kp_air_repulsion(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	KpMove *mv = &g->mv;
	int64_t density, f, a, dx, dy, bias;
	int32_t s;
	if (!repelled(g, n) || kp_band(u->status[n], STATUS_LOCKS) != 0) return 0;
	density = *cell(g, n);
	if (density <= 1) {
		u->repulse_flags[n] = kp_band(u->repulse_flags[n], 0xf8);
		return 0;
	}
	f = u->repulse_flags[n];
	if (kp_band(f, TIMER) != 0) {
		u->repulse_flags[n] = f - 1;
	} else {
		u->repulse_flags[n] = kp_bor(f, TIMER);
		update_angle(g, n);
	}
	a = u->repulse_angle[n];
	if (a < 0 || a > 255) kp_game_fail(g, "repulse: direction %lld is nil", (long long)a);
	dx = half(g, KP_DIR_X[a]);
	dy = half(g, KP_DIR_Y[a]);
	s = kp_unit_sprite(g, n);
	if (u->move_x[n] == kp_s16v(g->w->spr.x[s]) && u->move_y[n] == kp_s16v(g->w->spr.y[s])) { /* 0x41a250 */
		int64_t k = kp_mod(kp_shrv(density, 2) + 1, 256);
		dx = half(g, k * dx);
		dy = half(g, k * dy);
	}
	bias = kp_shrv(u->repulse_flags[n], 4);
	if (bias > 15) kp_game_fail(g, "repulse: bias %lld is nil", (long long)bias);
	dx = biased(dx, BIAS[bias][0]);
	dy = biased(dy, BIAS[bias][1]);
	mv->next_halt_x = kp_s32v(mv->next_halt_x + dx);
	mv->next_halt_y = kp_s32v(mv->next_halt_y + dy);
	mv->next_x = kp_s16v(kp_sarv(mv->next_halt_x, 8));
	mv->next_y = kp_s16v(kp_sarv(mv->next_halt_y, 8));
	return 1;
}

static int64_t abs64(int64_t v) { return v < 0 ? -v : v; }

/* UpdateAirRepulsion 0x4815d0: after the step the flyer moves to its new cell; one that drifted close to its
 * move target takes the drifted position as the target. */
void kp_air_repulsion_settle(KpGame *g, int64_t n, int drifted)
{
	KpUnits *u = &g->u;
	KpMove *mv = &g->mv;
	int32_t s;
	int64_t sx, sy;
	if (!repelled(g, n)) return;
	s = kp_unit_sprite(g, n);
	sx = kp_s16v(g->w->spr.x[s]);
	sy = kp_s16v(g->w->spr.y[s]);
	if (u->repulse_x[n] != kp_idiv64(g, sx, CELL) || u->repulse_y[n] != kp_idiv64(g, sy, CELL)) {
		kp_air_density_remove(g, n);
		kp_air_density_add(g, n);
	}
	if (!drifted) return;
	if (abs64(u->move_x[n] - mv->next_x) < SETTLE && abs64(u->move_y[n] - mv->next_y) < SETTLE) {
		u->move_x[n] = u->waypoint_x[n] = mv->next_x;
		u->move_y[n] = u->waypoint_y[n] = mv->next_y;
	}
}
