/* The flingy step of a moving unit (kp_game.h; game/units/flingy_move.lua, the specification). */
#include "kp_game_internal.h"

#define FLAG_TURNING 0x01
#define FLAG_MOVING 0x02
#define FLAG_WALK_IDLE 0x08
#define FLAG_PATHING 0x10
#define FLAG_WAYPOINT_SLOW 0x20
#define FLAG_NEAR_GOAL 0x40
#define CONTROL_ISCRIPT 2
#define SLOW_SPEED 0xc0
#define TURN_SLOWDOWN 0x20
#define ANIM_WALKING 0x0b
#define ANIM_WALKING_TO_IDLE 0x0c
#define GROWING_TURN_FIRST 0x8d
#define GROWING_TURN_LAST 0xab
#define GROWING_TURN2_FIRST 0xc9
#define GROWING_TURN2_COUNT 6

/* ApproxDistance 0x405cf0 (game/vision/detection.lua M.approx_distance). */
int64_t kp_approx_distance(int64_t dx, int64_t dy)
{
	int64_t big, small;
	dx = dx < 0 ? -dx : dx;
	dy = dy < 0 ? -dy : dy;
	big = dx;
	small = dy;
	if (big < small) {
		big = dy;
		small = dx;
	}
	if (kp_sarv(big, 2) < small) {
		big = big - kp_sarv(big, 4) + kp_sarv(small * 3, 3) - kp_sarv(big, 6) + kp_sarv(small * 3, 8);
	}
	return big;
}

void kp_move_context_init(KpGame *g)
{
	KpMove *mv = &g->mv;
	mv->next_x = mv->next_y = mv->old_x = mv->old_y = mv->next_halt_x = mv->next_halt_y = mv->step = 0;
	mv->walk_start = mv->walk_stop = mv->flags_before = mv->flags_after = 0;
}

static void velocity(KpGame *g, int64_t n, int64_t v)
{
	KpUnits *u = &g->u;
	int64_t h = u->heading2[n];
	u->speed_x[n] = kp_sarv(kp_mulv(KP_DIR_X[kp_mod(h, 256)], v), 8);
	u->speed_y[n] = kp_sarv(kp_mulv(KP_DIR_Y[kp_mod(h, 256)], v), 8);
}

void kp_flingy_set_current_speed(KpGame *g, int64_t n, int64_t v)
{
	KpUnits *u = &g->u;
	if (u->speed1[n] == v) return;
	u->speed1[n] = v;
	velocity(g, n, v);
}

static void stop_speed(KpUnits *u, int64_t n) { u->speed1[n] = u->speed_x[n] = u->speed_y[n] = 0; }

/* AccelerateSpeed 0x40bd10. */
static void accelerate(KpGame *g, int64_t n, int64_t delta)
{
	KpUnits *u = &g->u;
	int64_t v = kp_s32v(u->speed1[n] + delta), top = u->top_speed[n];
	if (v > top) {
		if (u->speed1[n] != top) {
			u->speed1[n] = top;
			velocity(g, n, top);
		}
	} else if (v < 0) {
		if (u->speed1[n] != 0) stop_speed(u, n);
	} else if (u->speed1[n] != v) {
		u->speed1[n] = v;
		velocity(g, n, v);
	}
}

int64_t kp_flingy_halt_distance(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t v = u->speed2[n], t;
	if (v == 0 || u->movement_type[n] != 0) return 0;
	t = u->flingy[n];
	if (v == KP_DAT(g, flingy, speed, t) && u->acceleration[n] == KP_DAT(g, flingy, acceleration, t)) {
		return KP_DAT(g, flingy, halt_distance, t);
	}
	return kp_idiv64(g, kp_s32v(kp_mulv(v, v)), u->acceleration[n] * 2);
}

/* SlopeToDirectionOffset 0x40b940. */
static int64_t slope_offset(int64_t slope)
{
	int negative = slope < 0;
	int64_t lo = 0, hi = 0x40, mid = 0x20;
	if (negative) slope = -slope;
	do {
		if (kp_mod(slope, 4294967296LL) > KP_SLOPES[mid]) lo = mid + 1;
		else hi = mid;
		mid = kp_shrv(lo + hi, 1);
	} while (hi != lo);
	return negative ? -mid : mid;
}

int64_t kp_direction_to(KpGame *g, int64_t x1, int64_t y1, int64_t x2, int64_t y2)
{
	int64_t dx = x2 - x1, dy = y2 - y1, a;
	if (dx == 0) return dy <= 0 ? 0 : 0x80;
	a = slope_offset(kp_idiv64(g, kp_s32v(kp_shlv(dy, 8)), dx));
	if (dx >= 0) return kp_mod(a + 0x40, 256);
	a += 0xc0;
	if (a == 0x100) a = 0;
	return kp_mod(a, 256);
}

/* TurnVelocityDirection 0x40d450. */
void kp_flingy_turn_velocity(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t want = u->velocity_direction2[n], cur = u->velocity_direction[n], h;
	if (want == cur) {
		h = cur;
	} else {
		int64_t step = u->turn_speed[n], diff;
		if (u->movement_type[n] != CONTROL_ISCRIPT) step = kp_shrv(step, 1);
		diff = kp_s8((uint32_t)(want - cur));
		if (diff >= step) diff = step;
		else if (diff <= -step) diff = -step;
		h = kp_mod(cur + diff, 256);
	}
	if (u->heading2[n] != h) {
		u->heading2[n] = h;
		velocity(g, n, u->speed1[n]);
	}
}

/* UpdateDesiredDirection 0x40cf50. */
void kp_flingy_update_desired(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t px = u->x[n], py = u->y[n];
	if (u->waypoint_x[n] == px && u->waypoint_y[n] == py) {
		if (u->target_waypoint_x[n] == px && u->target_waypoint_y[n] == py) u->velocity_direction2[n] = u->heading[n];
		else u->velocity_direction2[n] = kp_direction_to(g, px, py, u->target_waypoint_x[n], u->target_waypoint_y[n]);
	} else {
		u->velocity_direction2[n] = kp_direction_to(g, px, py, u->waypoint_x[n], u->waypoint_y[n]);
	}
	kp_flingy_turn_velocity(g, n);
}

static int64_t heading_gap(int64_t a, int64_t b)
{
	int64_t d = kp_band(a - b, 0xff);
	return d > 0x80 ? 0x100 - d : d;
}

/* ProgressSpeed 0x40d260. */
void kp_flingy_progress_speed(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t mt = u->movement_type[n], dist, want, f;
	int at_move = u->move_x[n] == u->x[n] && u->move_y[n] == u->y[n];
	if (mt == 0) {
		if (at_move && u->speed2[n] < SLOW_SPEED && kp_band(u->movement_flags[n], 0x30) == 0) {
			u->movement_flags[n] = kp_band(u->movement_flags[n], 0xfb);
			kp_flingy_set_current_speed(g, n, 0);
			return;
		}
	} else if (mt == 1 || mt == 2) {
		if (at_move) {
			u->movement_flags[n] = kp_band(u->movement_flags[n], 0xfb);
			if (u->speed1[n] != 0) stop_speed(u, n);
			return;
		}
		if (mt == 2) {
			f = u->movement_flags[n];
			if (kp_band(f, FLAG_TURNING) != 0 && heading_gap(u->heading[n], u->velocity_direction2[n]) >= 0x20) {
				if (kp_band(f, FLAG_MOVING) == 0) return;
				u->movement_flags[n] = kp_band(f, 0xf9);
				kp_flingy_set_current_speed(g, n, 0);
				return;
			}
			kp_flingy_set_current_speed(g, n, u->speed2[n]);
			return;
		}
	}
	if (u->speed1[n] != u->speed2[n]) {
		u->speed1[n] = u->speed2[n];
		velocity(g, n, u->speed2[n]);
	}
	dist = kp_approx_distance(u->x[n] - u->waypoint_x[n], u->y[n] - u->waypoint_y[n]);
	want = u->velocity_direction2[n];
	if (u->heading2[n] != want && kp_mod(dist, 4294967296LL) < TURN_SLOWDOWN) {
		int64_t turn = u->turn_speed[n], gap = heading_gap(want, u->heading2[n]), need;
		need = kp_idiv64(g, turn - 1 + gap * 2, turn);
		need = kp_shrv(kp_mod(kp_mulv(need, u->speed2[n]), 4294967296LL), 8);
		need = kp_shrv(need * 3, 1);
		if (need > kp_mod(dist, 4294967296LL)) {
			accelerate(g, n, -u->acceleration[n]);
			return;
		}
	}
	f = u->movement_flags[n];
	if (mt == 0 && (kp_band(f, FLAG_PATHING) == 0 || kp_band(f, FLAG_NEAR_GOAL) != 0)
		&& (kp_band(f, FLAG_WAYPOINT_SLOW) == 0 || u->waypoint_x[n] != u->move_x[n] || u->waypoint_y[n] != u->move_y[n])) {
		int64_t h = kp_flingy_halt_distance(g, n);
		int64_t d = kp_approx_distance(u->halt_x[n] - kp_shlv(u->waypoint_x[n], 8), u->halt_y[n] - kp_shlv(u->waypoint_y[n], 8));
		if (kp_mod(h, 4294967296LL) >= kp_mod(d, 4294967296LL)) {
			accelerate(g, n, -u->acceleration[n]);
			return;
		}
	}
	accelerate(g, n, u->acceleration[n]);
}

/* UpdateWalkingFlags 0x40d110. */
void kp_flingy_update_walking(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	KpMove *mv = &g->mv;
	int64_t f = u->movement_flags[n];
	int moving = kp_band(f, FLAG_MOVING) != 0, at_move = u->move_x[n] == u->x[n] && u->move_y[n] == u->y[n];
	mv->walk_start = mv->walk_stop = 0;
	if (moving && at_move) {
		f = kp_band(f, 0xfd);
		u->movement_flags[n] = f;
		if (kp_band(f, FLAG_WALK_IDLE) == 0) mv->walk_stop = 1;
		return;
	}
	if (moving || at_move) return;
	if (u->movement_type[n] != CONTROL_ISCRIPT || kp_band(f, FLAG_WALK_IDLE) == 0) u->movement_flags[n] = kp_bor(f, FLAG_MOVING);
	mv->walk_start = kp_band(u->movement_flags[n], FLAG_WALK_IDLE) == 0 ? 1 : 0;
}

/* PrepareNextPosition 0x40d020. */
static void prepare_next(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	KpMove *mv = &g->mv;
	int64_t d;
	mv->step = u->speed1[n];
	if (kp_band(u->movement_flags[n], FLAG_MOVING) == 0 || u->speed1[n] == 0) {
		mv->next_x = u->x[n];
		mv->next_y = u->y[n];
		mv->next_halt_x = u->halt_x[n];
		mv->next_halt_y = u->halt_y[n];
		return;
	}
	d = kp_approx_distance(u->halt_x[n] - kp_shlv(u->waypoint_x[n], 8), u->halt_y[n] - kp_shlv(u->waypoint_y[n], 8));
	if (u->speed1[n] < d) {
		mv->next_halt_x = kp_s32v(u->speed_x[n] + u->halt_x[n]);
		mv->next_halt_y = kp_s32v(u->speed_y[n] + u->halt_y[n]);
		mv->next_x = kp_s16v(kp_sarv(mv->next_halt_x, 8));
		mv->next_y = kp_s16v(kp_sarv(mv->next_halt_y, 8));
	} else {
		mv->next_x = u->waypoint_x[n];
		mv->next_y = u->waypoint_y[n];
		mv->next_halt_x = kp_shlv(mv->next_x, 8);
		mv->next_halt_y = kp_shlv(mv->next_y, 8);
		mv->step = d;
	}
	if (u->movement_type[n] == CONTROL_ISCRIPT && u->speed1[n] != 0) stop_speed(u, n);
}

static void prepare_tail(KpGame *g, int64_t n)
{
	kp_flingy_update_walking(g, n);
	prepare_next(g, n);
	g->mv.flags_after = g->u.movement_flags[n];
	g->u.movement_flags[n] = g->mv.flags_before;
}

void kp_prepare_flingy_movement(KpGame *g, int64_t n)
{
	g->mv.flags_before = g->u.movement_flags[n];
	kp_flingy_update_desired(g, n);
	kp_flingy_progress_speed(g, n);
	prepare_tail(g, n);
}

void kp_prepare_movement_in_direction(KpGame *g, int64_t n, int64_t dir)
{
	KpUnits *u = &g->u;
	g->mv.flags_before = u->movement_flags[n];
	if (u->heading2[n] != dir) {
		u->heading2[n] = dir;
		velocity(g, n, u->speed1[n]);
	}
	kp_flingy_progress_speed(g, n);
	prepare_tail(g, n);
}

/* TurnTowardDesiredHeading 0x40d1a0. */
void kp_flingy_turn_heading(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t f = u->movement_flags[n], h, want, diff, turn, fl;
	if (kp_band(f, FLAG_MOVING) != 0 && kp_band(f, FLAG_TURNING) == 0) return;
	h = u->heading[n];
	want = u->velocity_direction2[n];
	diff = kp_s8((uint32_t)(want - h));
	turn = u->turn_speed[n];
	if (diff > kp_s8((uint32_t)turn)) u->heading[n] = kp_mod(turn + h, 256);
	else if (diff < -kp_s8((uint32_t)turn)) u->heading[n] = kp_mod(h - turn, 256);
	else u->heading[n] = kp_mod(h + diff, 256);
	fl = u->flingy[n];
	if ((fl >= GROWING_TURN_FIRST && fl <= GROWING_TURN_LAST) || kp_mod(fl - GROWING_TURN2_FIRST, 65536) < GROWING_TURN2_COUNT) {
		u->turn_speed[n] = kp_mod(turn + 1, 256);
	}
	if (u->velocity_direction[n] == want && u->heading[n] == want) u->movement_flags[n] = kp_band(f, 0xfe);
}

static void walking_animation(KpGame *g, int32_t s)
{
	if (g->mv.walk_stop != 0) kp_sprite_play(g->w, s, ANIM_WALKING_TO_IDLE, 0);
	else if (g->mv.walk_start != 0) kp_sprite_play(g->w, s, ANIM_WALKING, 1);
}

void kp_flingy_commit_blocked(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int32_t s;
	u->velocity_direction[n] = u->heading2[n];
	u->movement_flags[n] = g->mv.flags_after;
	u->speed2[n] = 0;
	kp_flingy_turn_heading(g, n);
	s = kp_unit_sprite(g, n);
	kp_sprite_set_facing(g->w, s, (int32_t)u->heading[n]);
	walking_animation(g, s);
}

/* CommitFlingyMovement 0x40bdb0: true when the position changed. */
static int commit_flingy(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	KpMove *mv = &g->mv;
	int moved = !(u->x[n] == mv->next_x && u->y[n] == mv->next_y);
	int32_t s;
	mv->old_x = u->x[n];
	mv->old_y = u->y[n];
	u->velocity_direction[n] = u->heading2[n];
	u->movement_flags[n] = mv->flags_after;
	u->speed2[n] = u->speed1[n];
	u->x[n] = mv->next_x;
	u->y[n] = mv->next_y;
	u->halt_x[n] = mv->next_halt_x;
	u->halt_y[n] = mv->next_halt_y;
	s = kp_unit_sprite(g, n);
	kp_sprite_move(g->w, s, (int32_t)u->x[n], (int32_t)u->y[n]);
	kp_flingy_turn_heading(g, n);
	kp_sprite_set_facing(g->w, s, (int32_t)u->heading[n]);
	walking_animation(g, s);
	return moved;
}

static int64_t turn_step(int64_t cur, int64_t want, int64_t turn)
{
	int64_t diff = kp_s8((uint32_t)(want - cur));
	if (diff >= turn) return turn;
	if (diff <= -turn) return -turn;
	return diff;
}

/* ForecastPositions 0x42e6c0 (one point). */
void kp_forecast_position(KpGame *g, int64_t n, int64_t steps, int64_t *rx, int64_t *ry)
{
	KpUnits *u = &g->u;
	KpImages *w = g->w;
	int32_t s = kp_unit_sprite(g, n), main;
	int64_t x = kp_s16v(w->spr.x[s]), y = kp_s16v(w->spr.y[s]);
	int64_t want = kp_direction_to(g, x, y, u->waypoint_x[n], u->waypoint_y[n]), cur = u->velocity_direction[n];
	int64_t turn = kp_turn_radius_modified(g, n), speed = u->speed2[n], hx = u->halt_x[n], hy = u->halt_y[n];
	int64_t accel = kp_acceleration_modified(g, n), vx = 0, vy = 0, k;
	int recompute = 1;
	KpScriptState st;
	if (u->movement_type[n] != CONTROL_ISCRIPT) {
		/* 0x42e890: a flingy mover turns by half its turn radius per step while it moves,
		 * accelerating to its top speed. */
		int64_t top = u->top_speed[n], half = kp_idiv64(g, turn, 2);
		for (k = 1; k <= steps; k++) {
			if (want != cur) {
				cur = kp_mod(cur + turn_step(cur, want, half), 256);
				recompute = 1;
			}
			if (speed < top) {
				speed += accel;
				if (speed > top) speed = top;
				recompute = 1;
			}
			if (recompute) {
				recompute = 0;
				vx = kp_sarv(kp_mulv(KP_DIR_X[cur], speed), 8);
				vy = kp_sarv(kp_mulv(KP_DIR_Y[cur], speed), 8);
			}
			hx = kp_s32v(hx + vx);
			hy = kp_s32v(hy + vy);
		}
		*rx = kp_s16v(kp_sarv(hx, 8));
		*ry = kp_s16v(kp_sarv(hy, 8));
		return;
	}
	main = kp_check_image(w, w->spr.main[s]);
	st.header = w->img.header[main];
	st.pc = w->img.pc[main];
	st.ret = w->img.ret[main];
	st.wait = w->img.wait[main];
#define RUN(var) \
	do { \
		KpCtx saved_ = kp_ctx_unit(g, n); \
		int64_t v_ = 0; \
		int has_ = kp_images_run_iscript(w, main, &st, 1, 1, &v_); \
		kp_ctx_restore(g, saved_); \
		var = has_ ? v_ : KP_NIL; \
	} while (0)
	if (w->img.anim[main] != ANIM_WALKING) {
		int64_t v;
		/* SetForecastAnimation 0x42e9b0: the Walking entry of the script, when it has one. */
		if (ANIM_WALKING <= (int64_t)kp_code_u16(w, (int64_t)st.header + 4)) {
			int64_t entry = kp_code_u16(w, (int64_t)st.header + 8 + 2 * ANIM_WALKING);
			st.pc = (int32_t)entry;
			if (entry != 0) st.wait = st.ret = 0;
		}
		RUN(v);
		if (v != KP_NIL) speed = v;
	}
	for (k = 1; k <= steps; k++) {
		int move = 1;
		int64_t v;
		if (want != cur) {
			cur = kp_mod(cur + turn_step(cur, want, turn), 256);
			recompute = 1;
			move = cur == want;
		}
		if (move && speed != 0) {
			if (recompute) {
				recompute = 0;
				vx = kp_sarv(kp_mulv(KP_DIR_X[cur], speed), 8);
				vy = kp_sarv(kp_mulv(KP_DIR_Y[cur], speed), 8);
			}
			hx = kp_s32v(hx + vx);
			hy = kp_s32v(hy + vy);
		}
		RUN(v);
		speed = v == KP_NIL ? 0 : v;
	}
#undef RUN
	*rx = kp_s16v(kp_sarv(hx, 8));
	*ry = kp_s16v(kp_sarv(hy, 8));
}

void kp_flingy_recompute_velocity(KpGame *g, int64_t n) { velocity(g, n, g->u.speed1[n]); }

void kp_flingy_commit_turn(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	u->velocity_direction[n] = u->heading2[n];
	kp_flingy_turn_heading(g, n);
	kp_sprite_set_facing(g->w, kp_unit_sprite(g, n), (int32_t)u->heading[n]);
}

void kp_prepare_movement_toward(KpGame *g, int64_t n, int64_t dir, int64_t speed)
{
	KpUnits *u = &g->u;
	g->mv.flags_before = u->movement_flags[n];
	if (u->heading2[n] != dir) {
		u->heading2[n] = dir;
		velocity(g, n, u->speed1[n]);
	}
	u->speed2[n] = speed;
	if (u->speed1[n] != speed) {
		u->speed1[n] = speed;
		velocity(g, n, speed);
	}
	prepare_tail(g, n);
}

int kp_commit_unit_movement(KpGame *g, int64_t n)
{
	KpMove *mv = &g->mv;
	if (!commit_flingy(g, n)) return 0;
	kp_spatial_move(g, n, mv->next_x - mv->old_x, mv->next_y - mv->old_y);
	if (kp_idiv64(g, mv->old_x, 32) != kp_idiv64(g, mv->next_x, 32) || kp_idiv64(g, mv->old_y, 32) != kp_idiv64(g, mv->next_y, 32)) {
		kp_scalar_set(g, KP_MOVE_REVEAL, 1);
	}
	return 1;
}
