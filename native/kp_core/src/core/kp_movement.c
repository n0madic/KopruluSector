/* Unit movement states (kp_game.h). */
#include "kp_game_internal.h"

#define INIT 0
#define NOBRK 1
#define DEAD 2
#define TURRET 3
#define TURN_HIDDEN 4
#define TURN 5
#define HIDDEN 6
#define FLY 7
#define AT_REST 9
#define DORMANT 10
#define CHECK_ILLEGAL 12
#define REPLACED_ON_LEGAL 13
#define ROUTE 14
#define FAILED_WAIT 16
#define FAILED_WAIT_STEPS 0x14
#define ARRIVED 11
#define FAILED 15
#define START_MOVE 17
#define RETRY 18
#define BLOCKED_REPATH 19
#define REPATH_STALE 21
#define REPATH 22
#define REPATH_FAILED 23
#define FOLLOW 25
#define SCOUT 26
#define REFOLLOW 27
#define BLOCKED_BY_UNIT 28
#define BLOCKED_BY_TERRAIN 34
#define TERRAIN_SLIDE 35
#define FACE_TARGET 20
#define REPATH_MOVERS 24
#define WAIT_FREE 29
#define SLIDE_START 30
#define SLIDE_PREP 31
#define SLIDE 32
#define FORCE_FREE 33
#define STATUS_INTERCEPT 0x80000
#define ANIM_WALKING_TO_IDLE 0x0c
#define SCOUT_DELAY 0x1e
#define SCOUT_RADIUS 0x20
#define SCOUT_STEPS 0x1e
#define PATH_RANDOM 0x33
#define PATH_RETRY_MAX 2
#define USER_RIGHT_CLICKED 2
#define STATUS_NOBRK 0x2000
#define STATUS_IN_BUILDING 0x20
#define STATUS_BURROWED 0x10
#define STATUS_CAN_MOVE 0x20000
#define STATUS_CAN_TURN 0x10000
#define STATUS_COLLIDING 0x40000
#define STATUS_NOT_AIR 0x100000
#define STATUS_IGNORED 0x200000
#define STATUS_LOCKS 0x3000
#define SPRITE_HIDDEN 0x20
#define DAT_SUBUNIT 0x10
#define STATUS_COMPLETED 0x1
#define STATUS_TURRET_WALKING 0x1000000
#define STATUS_TURRET_ALIGNED 0x2000000
#define ANIM_WALKING 0x0b
#define GOLIATH_ARC 0x20
#define TURN_SETTLED 10
/* path record offsets (the KP_PATH_* of kp_pathing.h) */
#define P_FRAME 0x0c
#define P_BLOCKER 0x10
#define P_SPEED 0x14
#define P_FLAGS 0x18
#define P_RETRY 0x19
#define P_SLIDE 0x1a

static void sprite_pos(KpGame *g, int64_t n, int64_t *x, int64_t *y)
{
	int32_t s = kp_unit_sprite(g, n);
	*x = kp_s16v(g->w->spr.x[s]);
	*y = kp_s16v(g->w->spr.y[s]);
}

static int at_move_target(KpGame *g, int64_t n)
{
	int64_t x, y;
	sprite_pos(g, n, &x, &y);
	return g->u.move_x[n] == x && g->u.move_y[n] == y;
}

int64_t kp_move_arrival(KpGame *g, int64_t n)
{
	if (!at_move_target(g, n)) return 0;
	return kp_band(g->u.status[n], STATUS_INTERCEPT) != 0 ? 2 : 1;
}

int64_t kp_try_move_to_legal(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t x, y;
	if (kp_band(u->status[n], STATUS_NOT_AIR) == 0) return 0;
	sprite_pos(g, n, &x, &y);
	if (kp_position_blocked(g, n, x, y) == 0) return 0;
	if (kp_band(u->status[n], STATUS_LOCKS) == 0 && kp_band(u->movement_flags[n], 8) == 0) {
		int64_t px = x, py = y;
		kp_find_legal_position(g, n, &x, &y);
		kp_clamp_to_map(g, u->type[n], &x, &y);
		if (x != px || y != py) {
			kp_path_free(g, n);
			if (!at_move_target(g, n) && kp_band(u->movement_flags[n], 4) == 0) kp_set_path_waypoint(g, n, x, y);
			else kp_set_move_target_point(g, n, x, y);
			kp_path_set_direct(g, n, x, y, x, y);
			u->contour_left[n] = u->contour_top[n] = u->contour_right[n] = u->contour_bottom[n] = 0;
			return 1;
		}
	}
	u->status[n] = kp_bor(u->status[n], STATUS_COLLIDING);
	return 0;
}

void kp_stop_at_target(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t x, y;
	if (kp_band(u->pathing_flags[n], 2) != 0) u->pathing_flags[n] = kp_bor(u->pathing_flags[n], 4); /* 0x42ad10 */
	kp_stop_flingy(g, n);
	x = u->move_x[n];
	y = u->move_y[n];
	kp_clamp_to_map(g, u->type[n], &x, &y);
	if (x != u->move_x[n] || y != u->move_y[n]) {
		u->move_x[n] = u->waypoint_x[n] = x;
		u->move_y[n] = u->waypoint_y[n] = y;
	}
	u->recent_order_timer[n] = 15;
}

/* HaltAtTarget 0x42c7a0 (SetUnitImmovable). */
void kp_halt_at_target(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t x, y;
	kp_flingy_set_speed(g, n, 0);
	kp_stop_at_target(g, n);
	sprite_pos(g, n, &x, &y);
	kp_set_move_target_point(g, n, x, y);
	u->status[n] = kp_bor(u->status[n], STATUS_INTERCEPT);
	u->user_action[n] = kp_band(u->user_action[n], 0xfd);
}

/* MoveState_Init 0x42bcc0. */
static int state_init(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	KpImages *w = g->w;
	int32_t s = kp_unit_sprite(g, n);
	int subunit;
	int64_t status;
	u->contour_left[n] = u->contour_top[n] = u->contour_right[n] = u->contour_bottom[n] = 0;
	u->pathing_flags[n] = kp_bor(kp_band(u->pathing_flags[n], 0xfc), w->spr.elevation[s] < 12 ? 1 : 0);
	subunit = kp_band(KP_DAT(g, units, special_ability_flags, u->type[n]), DAT_SUBUNIT) != 0;
	if (!subunit && kp_band(u->status[n], STATUS_NOBRK) != 0) {
		u->movement_state[n] = NOBRK;
		return 1;
	}
	if (kp_unit_is_dead(g, n)) {
		u->movement_state[n] = DEAD;
		return 1;
	}
	status = u->status[n];
	if (kp_band(status, STATUS_IN_BUILDING) != 0) {
		u->movement_state[n] = TURN_HIDDEN;
		return 1;
	}
	if (kp_band(w->spr.flags[s], SPRITE_HIDDEN) != 0) {
		/* A hidden unit that still moves, or is away from its move target, stops there. */
		if (kp_band(u->movement_flags[n], 2) != 0 || kp_move_arrival(g, n) == 0) {
			kp_halt_at_target(g, n);
			kp_prepare_flingy_movement(g, n);
			kp_commit_unit_movement(g, n);
		}
		u->movement_state[n] = HIDDEN;
		return 1;
	}
	if (kp_band(status, STATUS_BURROWED) == 0) {
		if (kp_band(status, STATUS_CAN_MOVE) != 0) {
			u->movement_state[n] = kp_band(u->pathing_flags[n], 1) * 2 + FLY;
			return 1;
		}
		if (kp_band(status, STATUS_CAN_TURN) != 0) {
			u->movement_state[n] = subunit ? TURRET : TURN;
			return 1;
		}
		if (kp_band(u->pathing_flags[n], 1) != 0) {
			/* A ground unit that cannot move settles unless it is still away from its move target. */
			u->movement_state[n] = (kp_band(u->movement_flags[n], 2) == 0 && at_move_target(g, n)) ? DEAD : ROUTE;
			return 1;
		}
	}
	u->movement_state[n] = DEAD;
	return 1;
}

/* CommitGroundMovementOrStop 0x42abb0. */
static int commit_or_stop(KpGame *g, int64_t n)
{
	int64_t x, y;
	if (kp_ground_unit_collision(g, n) == 0 && !kp_terrain_collision(g, n)) return kp_commit_unit_movement(g, n);
	kp_flingy_commit_blocked(g, n);
	kp_flingy_set_speed(g, n, 0);
	sprite_pos(g, n, &x, &y);
	kp_flingy_set_move_target(g, n, x, y);
	kp_stop_at_target(g, n);
	return 0;
}

/* MoveState_Route 0x42be10 (14). */
static int state_route(KpGame *g, int64_t n)
{
	if (at_move_target(g, n) && kp_band(g->u.movement_flags[n], 2) == 0) {
		g->u.movement_state[n] = DEAD;
		return 1;
	}
	kp_prepare_flingy_movement(g, n);
	commit_or_stop(g, n);
	return 0;
}

static void count_down_interval(KpUnits *u, int64_t n)
{
	int64_t c = u->collision_interval[n];
	if (c > 2) u->collision_interval[n] = 2;
	else if (c != 0) u->collision_interval[n] = c - 1;
}

/* MoveState_AtRest 0x42b030. */
static int state_at_rest(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int at_target = at_move_target(g, n), settled = 1, step;
	int64_t flags, status;
	if (at_target) u->collision_interval[n] = 0;
	else count_down_interval(u, n);
	flags = u->movement_flags[n];
	step = kp_band(flags, 4) != 0;
	if (!step && at_target) {
		if (kp_band(flags, 2) != 0) {
			step = 1;
		} else if (u->target_waypoint_x[n] != u->x[n] || u->target_waypoint_y[n] != u->y[n]) {
			int64_t x, y, d;
			sprite_pos(g, n, &x, &y);
			d = kp_direction_to(g, x, y, u->target_waypoint_x[n], u->target_waypoint_y[n]);
			step = d != u->heading[n] || d != u->velocity_direction[n];
		}
	}
	if (step) {
		settled = 0;
		kp_prepare_flingy_movement(g, n);
		commit_or_stop(g, n);
	}
	status = u->status[n];
	if (kp_band(status, STATUS_COLLIDING) != 0 && kp_band(status, STATUS_NOT_AIR) != 0) {
		u->movement_state[n] = CHECK_ILLEGAL;
		return 0;
	}
	if (!at_move_target(g, n) && kp_band(u->movement_flags[n], 4) == 0) {
		u->movement_state[n] = START_MOVE;
		return 1;
	}
	if (settled) {
		int64_t x, y;
		sprite_pos(g, n, &x, &y);
		kp_flingy_set_speed(g, n, 0);
		kp_flingy_set_target_waypoint(g, n, x, y);
		u->movement_state[n] = DORMANT;
	}
	return 0;
}

/* MoveState_CheckIllegal 0x42b1b0. */
static int state_check_illegal(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	u->status[n] = kp_band(u->status[n], kp_bnot(STATUS_COLLIDING));
	if (kp_try_move_to_legal(g, n) != 0) {
		u->pathing_flags[n] = kp_bor(u->pathing_flags[n], 2);
		u->movement_state[n] = REPLACED_ON_LEGAL;
		return 0;
	}
	u->pathing_flags[n] = kp_band(u->pathing_flags[n], 0xf9);
	if (!at_move_target(g, n) && kp_band(u->movement_flags[n], 4) == 0) {
		u->movement_state[n] = REPATH;
		return 1;
	}
	u->movement_state[n] = AT_REST;
	return 1;
}

/* MoveState_Dormant 0x42b240. */
static int state_dormant(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t status = u->status[n];
	if ((kp_band(status, STATUS_COLLIDING) == 0 || kp_band(status, STATUS_NOT_AIR) == 0) && at_move_target(g, n)
		&& u->target_waypoint_x[n] == u->x[n] && u->target_waypoint_y[n] == u->y[n]) {
		return 0;
	}
	u->movement_state[n] = AT_REST;
	return 1;
}

int kp_update_movement_state(KpGame *g, int64_t n, int repath)
{
	KpUnits *u = &g->u;
	int64_t status, k, state, cur;
	if (at_move_target(g, n) || kp_band(u->movement_flags[n], 4) != 0) {
		u->movement_state[n] = ARRIVED;
		return 1;
	}
	status = u->status[n];
	if (kp_band(status, STATUS_COLLIDING) != 0 && kp_band(status, STATUS_NOT_AIR) != 0) {
		kp_path_free(g, n);
		u->movement_state[n] = CHECK_ILLEGAL;
		return 1;
	}
	k = u->path[n];
	if (repath && k == 0) state = REPATH;
	else if (k != 0 && kp_band(kp_path_u8(g, k, P_FLAGS), 1) != 0) state = REPATH_STALE;
	else return 0;
	cur = u->movement_state[n];
	if (cur >= 0x1c && cur <= 0x23) state = BLOCKED_REPATH;
	u->movement_state[n] = state;
	return 1;
}

/* MoveState_StartPath 0x42c400. */
static int state_start_path(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t action;
	if (!kp_advance_path_waypoint(g, n, u->move_x[n], u->move_y[n])) {
		u->movement_state[n] = FAILED;
		return 0;
	}
	action = u->user_action[n];
	if (kp_band(action, USER_RIGHT_CLICKED) != 0) {
		u->user_action[n] = kp_band(action, kp_bnot(USER_RIGHT_CLICKED));
		if (u->movement_type[n] == 2) {
			kp_path_set_retry(g, n, kp_rng_game_range(g->rng, PATH_RANDOM, 0, PATH_RETRY_MAX));
			u->movement_state[n] = RETRY;
			return 1;
		}
	}
	u->movement_state[n] = FOLLOW;
	return 1;
}

static int state_retry(KpGame *g, int64_t n)
{
	int64_t k, t;
	if (kp_update_movement_state(g, n, 1)) return 1;
	k = g->u.path[n];
	t = kp_path_u8(g, k, P_RETRY);
	if (t != 0) {
		kp_path_w8(g, k, P_RETRY, t - 1);
		return 0;
	}
	g->u.movement_state[n] = FOLLOW;
	return 1;
}

static int state_repath_stale(KpGame *g, int64_t n)
{
	int64_t k = g->u.path[n];
	kp_path_w8(g, k, P_FLAGS, kp_band(kp_path_u8(g, k, P_FLAGS), 0xfe));
	if (kp_update_movement_state(g, n, 1)) return 1;
	kp_path_free(g, n);
	g->u.movement_state[n] = REPATH_FAILED;
	return 1;
}

static int state_repath(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	u->movement_state[n] = kp_advance_path_waypoint(g, n, u->move_x[n], u->move_y[n]) ? FOLLOW : FAILED;
	return 0;
}

/* MoveState_AtMoveTarget 0x42afe0. */
static int state_arrived(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	kp_path_free(g, n);
	if (u->waypoint_x[n] != u->move_x[n] || u->waypoint_y[n] != u->move_y[n]) {
		u->waypoint_x[n] = u->move_x[n];
		u->waypoint_y[n] = u->move_y[n];
	}
	if (kp_band(u->status[n], STATUS_NOT_AIR) != 0 && kp_band(u->movement_flags[n], 4) == 0) u->movement_state[n] = CHECK_ILLEGAL;
	else u->movement_state[n] = AT_REST;
	return 1;
}

/* MoveState_FollowPath 0x42b8d0. */
static int state_follow_path(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	KpMove *mv = &g->mv;
	int64_t speed, blocker, x, y;
	int terrain;
	if (kp_update_movement_state(g, n, 1)) return 1;
	if (!kp_advance_path_waypoint(g, n, u->move_x[n], u->move_y[n])) {
		u->movement_state[n] = REPATH;
		return 1;
	}
	speed = u->speed2[n];
	kp_prepare_flingy_movement(g, n);
	if (u->movement_type[n] == 0) speed = u->speed1[n];
	blocker = kp_ground_unit_collision(g, n);
	terrain = kp_terrain_collision(g, n);
	if (blocker != 0) {
		int through = 0;
		if (!terrain && (u->speed_x[n] >= 0x20 || u->speed_y[n] >= 0x20) && mv->step == u->speed1[n]) {
			int64_t hx = mv->next_halt_x, hy = mv->next_halt_y, nx = mv->next_x, ny = mv->next_y, div;
			for (div = 2; div <= 4; div += 2) {
				mv->next_halt_x = kp_idiv64(g, u->speed_x[n], div) + u->halt_x[n];
				mv->next_halt_y = kp_idiv64(g, u->speed_y[n], div) + u->halt_y[n];
				mv->next_x = kp_s16v(kp_sarv(mv->next_halt_x, 8));
				mv->next_y = kp_s16v(kp_sarv(mv->next_halt_y, 8));
				if (kp_ground_unit_collision(g, n) == 0) {
					through = 1;
					break;
				}
			}
			if (!through) {
				mv->next_halt_x = hx;
				mv->next_halt_y = hy;
				mv->next_x = nx;
				mv->next_y = ny;
			}
		}
		if (!through) {
			int64_t k = u->path[n];
			kp_path_w32(g, k, P_BLOCKER, kp_unit_ref(g, blocker));
			kp_path_w32(g, k, P_SPEED, speed);
			u->movement_state[n] = BLOCKED_BY_UNIT;
			return 1;
		}
	} else if (terrain) {
		kp_path_w32(g, u->path[n], P_SPEED, speed);
		u->movement_state[n] = BLOCKED_BY_TERRAIN;
		return 1;
	}
	sprite_pos(g, n, &x, &y);
	if (mv->next_x != x || mv->next_y != y) count_down_interval(u, n);
	kp_commit_unit_movement(g, n);
	if (at_move_target(g, n)) {
		u->movement_state[n] = ARRIVED;
		return 0;
	}
	if (kp_path_poll_retry(g, n)) {
		kp_path_set_retry(g, n, SCOUT_DELAY);
		u->movement_state[n] = SCOUT;
	}
	return 0;
}

/* MoveState_ScoutPath 0x42c030. */
static int state_scout_path(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t near[KP_UNITS], meets = 0;
	int count, k;
	if (kp_update_movement_state(g, n, 1)) return 1;
	count = kp_moving_neighbors(g, n, SCOUT_RADIUS, SCOUT_RADIUS, near, KP_UNITS);
	if (count != 0) {
		int64_t mx, my, t = u->type[n];
		kp_forecast_position(g, n, SCOUT_STEPS, &mx, &my);
		{
			int64_t px[KP_UNITS], py[KP_UNITS];
			for (k = 0; k < count; k++) kp_forecast_position(g, near[k], SCOUT_STEPS, &px[k], &py[k]);
			for (k = 0; k < count; k++) {
				int64_t ct = u->type[near[k]];
#define EXT(tt, i) KP_DAT(g, units, unit_extents, (tt) * 4 + (i))
				int64_t top = kp_s16v(py[k] - EXT(ct, 1) + (-1 - EXT(t, 3)));
				int64_t bottom = kp_s16v(EXT(ct, 3) + py[k] + EXT(t, 1) + 1);
				int64_t right = kp_s16v(EXT(ct, 2) + px[k] + EXT(t, 0) + 1);
				int64_t left = kp_s16v(px[k] - EXT(ct, 0) + (-1 - EXT(t, 2)));
#undef EXT
				if (mx >= left && mx <= right && my >= top && my <= bottom) meets++;
			}
		}
	}
	u->movement_state[n] = meets > 0 ? REFOLLOW : FOLLOW;
	return 1;
}

static void bump_interval(KpUnits *u, int64_t n)
{
	if (u->collision_interval[n] != 0xff) u->collision_interval[n] = u->collision_interval[n] + 1;
}

/* StopWalkingAnimation 0x42c810. */
static void stop_walking(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	if (kp_band(u->movement_flags[n], 2) != 0 && u->collision_interval[n] > 2 && kp_band(u->status[n], STATUS_LOCKS) == 0) {
		u->movement_flags[n] = kp_band(u->movement_flags[n], 0xfd);
		kp_sprite_play(g->w, kp_unit_sprite(g, n), ANIM_WALKING_TO_IDLE, 1);
	}
}

static int faces_waypoint(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t x, y;
	sprite_pos(g, n, &x, &y);
	return u->velocity_direction[n] == kp_direction_to(g, x, y, u->waypoint_x[n], u->waypoint_y[n]);
}

/* MoveState_FixCollision 0x42b3b0 (28). */
static int state_fix_collision(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t k, c, t, cx, cy, action = 0, mu;
	int32_t cs;
	if (kp_update_movement_state(g, n, 1)) return 1;
	bump_interval(u, n);
	k = u->path[n];
	c = kp_unit_from_ref(g, kp_path_u32(g, k, P_BLOCKER));
	if (c == 0 || kp_band(g->w->spr.flags[kp_unit_sprite(g, c)], 0x20) != 0 || kp_band(u->pathing_flags[c], 1) == 0
		|| kp_band(u->status[c], STATUS_IGNORED) != 0 || c == n || !kp_units_collide(g, c, n)) {
		u->movement_state[n] = FOLLOW;
		return 0;
	}
	t = u->type[n];
	cs = kp_unit_sprite(g, c);
	cx = kp_s16v(g->w->spr.x[cs]);
	cy = kp_s16v(g->w->spr.y[cs]);
	mu = u->move_unit[n];
	if (mu != 0 && kp_unit_box_within(g, n, 0, mu)) {
		action = 1; /* the target unit is reached */
	} else if (kp_footprint_touches(g, c, cx, cy, t, u->move_x[n], u->move_y[n])) {
		if (u->collision_interval[c] < 0x1e && !kp_footprint_touches(g, c, u->move_x[c], u->move_y[c], t, u->move_x[n], u->move_y[n])) {
			action = kp_blocker_heading_away(g, n, c) ? 4 : 6;
		} else {
			action = (u->move_unit[n] != 0 ? 1 : 0) + 2;
		}
	} else if (kp_footprint_touches(g, c, cx, cy, t, u->waypoint_x[n], u->waypoint_y[n])) {
		if (u->collision_interval[c] < 0x1e
			&& !kp_footprint_touches(g, c, u->move_x[c], u->move_y[c], t, u->waypoint_x[n], u->waypoint_y[n])) {
			action = kp_blocker_heading_away(g, n, c) ? 4 : 6;
		} else {
			action = 3;
		}
	}
	if (action == 0 && kp_band(u->movement_flags[c], 2) != 0) {
		int64_t side = kp_cardinal_side(g, n, c), v = u->velocity_direction[c];
		if ((side == 0 && (v <= 0x40 || v >= 0xc0)) || (side == 1 && v <= 0x80) || (side == 2 && v >= 0x40 && v <= 0xc0)
			|| (side == 3 && (v == 0 || v >= 0x80))) {
			action = 6;
		}
	}
	if (action == 0) {
		int64_t cstate = u->collision_interval[c] > 2 ? WAIT_FREE : u->movement_state[c];
		if (!at_move_target(g, c) && kp_band(u->movement_flags[c], 2) != 0 && cstate != WAIT_FREE) {
			action = (cstate == FOLLOW || cstate == SCOUT) ? 6 : 4;
		} else {
			action = kp_slide_direction(g, n, c) >= 0 ? 7 : 5;
		}
	}
	if (kp_mod(kp_scalar(g, KP_FRAME) - kp_path_u32(g, k, P_FRAME), 4294967296LL) < 7 && action >= 3 && action <= 5) {
		stop_walking(g, n);
		return 0;
	}
	if (action == 1 || action == 2) {
		kp_halt_at_target(g, n);
		if (action == 1) u->status[n] = kp_band(u->status[n], kp_bnot(STATUS_INTERCEPT));
		u->movement_state[n] = ARRIVED;
	} else if (action == 3) {
		u->movement_state[n] = REPATH_FAILED;
	} else if (action == 4) {
		u->movement_state[n] = REPATH_MOVERS;
	} else if (action == 5) {
		u->movement_state[n] = BLOCKED_REPATH;
		kp_flingy_set_speed(g, n, 0);
	} else if (action == 6) {
		u->movement_state[n] = WAIT_FREE;
	} else if (action == 7) {
		kp_path_w32(g, k, P_BLOCKER, kp_unit_ref(g, c));
		kp_path_w8(g, k, P_SLIDE, kp_slide_direction(g, n, c));
		u->movement_state[n] = SLIDE_PREP;
	}
	return 0;
}

/* StepAlongLegalPath 0x42d830: 1 when the move to the legal point is over or blocked. */
static int step_to_legal(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	KpMove *mv = &g->mv;
	int64_t t = u->type[n];
	if (!kp_path_keeps_waypoint(g, n)) return 1;
	kp_prepare_flingy_movement(g, n);
	if (mv->next_x - kp_s16v(KP_DAT(g, units, unit_extents, t * 4)) >= 0 && mv->next_y - kp_s16v(KP_DAT(g, units, unit_extents, t * 4 + 1)) >= 0
		&& kp_s16v(KP_DAT(g, units, unit_extents, t * 4 + 2)) + mv->next_x < kp_mod(g->map_w_px, 65536)
		&& kp_s16v(KP_DAT(g, units, unit_extents, t * 4 + 3)) + mv->next_y < kp_mod(g->map_h_px, 65536)) {
		kp_commit_unit_movement(g, n);
		return 0;
	}
	if (!faces_waypoint(g, n)) {
		kp_prepare_flingy_movement(g, n);
		kp_flingy_commit_turn(g, n);
		return 0;
	}
	return 1;
}

/* MoveState_MoveToLegal 0x42be70 (13). */
static int state_move_to_legal(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t k = u->path[n], x, y;
	if (k == 0) {
		u->movement_state[n] = CHECK_ILLEGAL;
		return 1;
	}
	if (kp_band(u->pathing_flags[n], 4) != 0) {
		u->status[n] = kp_bor(u->status[n], STATUS_COLLIDING);
		u->pathing_flags[n] = kp_band(u->pathing_flags[n], 0xfb);
		kp_path_free(g, n);
		u->movement_state[n] = AT_REST;
		return 1;
	}
	kp_path_point(g, k, KP_PATH_NEXT, &x, &y);
	if (kp_band(kp_path_u8(g, k, P_FLAGS), 1) != 0 || x != u->waypoint_x[n] || y != u->waypoint_y[n]) {
		u->waypoint_x[n] = x;
		u->waypoint_y[n] = y;
	}
	if (!step_to_legal(g, n)) return 0;
	if (u->path[n] != 0) kp_path_free(g, n);
	kp_update_overlap_flags(g, n);
	u->status[n] = kp_bor(u->status[n], STATUS_COLLIDING);
	u->movement_state[n] = CHECK_ILLEGAL;
	return 0;
}

/* MoveState_WaitFree 0x42c670 (29). */
static int state_wait_free(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	bump_interval(u, n);
	if (kp_update_movement_state(g, n, 1)) return 1;
	kp_flingy_set_speed(g, n, kp_path_u32(g, u->path[n], P_SPEED));
	kp_prepare_flingy_movement(g, n);
	if (kp_ground_unit_collision_any(g, n) == 0) {
		if (kp_terrain_collision(g, n)) {
			u->movement_state[n] = REPATH_FAILED;
			return 0;
		}
		kp_commit_unit_movement(g, n);
		u->movement_state[n] = FOLLOW;
		return 0;
	}
	stop_walking(g, n);
	kp_flingy_set_speed(g, n, 0);
	if (!faces_waypoint(g, n)) kp_flingy_commit_turn(g, n);
	if (u->collision_interval[n] >= 0x19) {
		if (kp_rng_game_raw(g->rng, PATH_RANDOM) < 0x3fff) {
			u->movement_state[n] = FORCE_FREE;
			return 0;
		}
		kp_flingy_set_speed(g, n, 0);
		u->movement_state[n] = REPATH_MOVERS;
	}
	return 0;
}

/* MoveState_FaceTarget 0x42b2b0 (20). */
static int state_face_target(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	if (kp_update_movement_state(g, n, 1)) return 1;
	if (at_move_target(g, n) || kp_band(u->movement_flags[n], 4) != 0) {
		u->movement_state[n] = ARRIVED;
		return 1;
	}
	if (!faces_waypoint(g, n)) {
		kp_prepare_flingy_movement(g, n);
		kp_flingy_commit_turn(g, n);
		return 0;
	}
	if (kp_band(kp_path_u8(g, u->path[n], P_FLAGS), 1) != 0) u->movement_state[n] = REPATH_STALE;
	else u->movement_state[n] = FOLLOW;
	return 1;
}

/* MoveState_RepathMovers 0x42bf60 (24). */
static int state_repath_movers(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t mx, my, mu;
	int ok;
	kp_path_free(g, n);
	mx = u->move_x[n];
	my = u->move_y[n];
	mu = u->move_unit[n];
	kp_scalar_set(g, KP_PATH_ALL_UNITS, 1);
	ok = kp_advance_path_waypoint(g, n, mx, my);
	kp_scalar_set(g, KP_PATH_ALL_UNITS, 0);
	if (ok && kp_band(u->status[n], STATUS_INTERCEPT) == 0) {
		u->movement_state[n] = FACE_TARGET;
		return 1;
	}
	u->move_x[n] = mx;
	u->move_y[n] = my;
	u->move_unit[n] = mu;
	u->movement_state[n] = BLOCKED_REPATH;
	return 0;
}

static void drop_blocked_path(KpGame *g, int64_t n)
{
	int64_t k = g->u.path[n];
	if (k != 0) {
		kp_scalar_set(g, KP_PATH_SUBJECT, kp_unit_from_ref(g, kp_path_u32(g, k, P_BLOCKER)));
		kp_path_free(g, n);
	}
}

/* MoveState_RepathBlocked 0x42c570 (19). */
static int state_repath_blocked(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	drop_blocked_path(g, n);
	if (!at_move_target(g, n) && kp_band(u->movement_flags[n], 4) == 0) {
		int ok = kp_advance_path_waypoint(g, n, u->move_x[n], u->move_y[n]);
		kp_scalar_set(g, KP_PATH_SUBJECT, 0);
		if (!ok) {
			u->movement_state[n] = FAILED;
			return 1;
		}
		u->movement_state[n] = FACE_TARGET;
		return 0;
	}
	u->movement_state[n] = ARRIVED;
	return 1;
}

/* MoveState_RepathAround 0x42bf10 (23). */
static int state_repath_around(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int ok;
	drop_blocked_path(g, n);
	ok = kp_advance_path_waypoint(g, n, u->move_x[n], u->move_y[n]);
	kp_scalar_set(g, KP_PATH_SUBJECT, 0);
	u->movement_state[n] = ok ? FOLLOW : FAILED;
	return 1;
}

/* MoveState_GetFree 0x42bb80 (30), unreachable in the original. */
static int state_slide_check(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t c;
	bump_interval(u, n);
	if (kp_update_movement_state(g, n, 1)) return 1;
	kp_flingy_set_speed(g, n, kp_path_u32(g, u->path[n], P_SPEED));
	kp_prepare_flingy_movement(g, n);
	c = kp_ground_unit_collision_any(g, n);
	if (c == 0) {
		if (kp_terrain_collision(g, n)) {
			u->movement_state[n] = REPATH_FAILED;
			return 0;
		}
		kp_commit_unit_movement(g, n);
		u->movement_state[n] = FOLLOW;
		return 0;
	}
	stop_walking(g, n);
	if (!faces_waypoint(g, n)) {
		kp_flingy_commit_turn(g, n);
		return 0;
	}
	kp_path_free(g, n);
	if (kp_slide_direction(g, n, c) < 0) {
		u->movement_state[n] = REPATH_MOVERS;
		return 0;
	}
	/* The original writes the blocker and the slide direction through the path pointer that
	 * 0x48e880 has just cleared (0x42bc9e), a null write. No FixCollision action selects state
	 * 30, so the state is unreachable. */
	kp_game_fail(g, "movement: GetFree writes through a null path (0x42bc9e; state 30 is unreachable)");
}

/* MoveState_SlidePrep 0x42c390 (31). */
static int state_slide_prep(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t k, v;
	if (kp_update_movement_state(g, n, 1)) return 1;
	k = u->path[n];
	if (u->movement_type[n] == 0) {
		v = kp_s32v(kp_path_u32(g, k, P_SPEED));
		if (v >= 0x200) kp_path_w32(g, k, P_SPEED, kp_idiv64(g, v, 2));
		else if (v > 0x100) kp_path_w32(g, k, P_SPEED, 0x100);
	}
	v = kp_s32v(kp_path_u32(g, k, P_SPEED));
	u->speed1[n] = u->speed2[n] = v;
	u->heading2[n] = u->velocity_direction[n];
	kp_flingy_recompute_velocity(g, n);
	u->movement_state[n] = SLIDE;
	return 1;
}

/* MoveState_SlideFree 0x42c1e0 (32). */
static int state_slide_free(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t next_state = SLIDE, after = 0, k, c, dir, speed, left;
	bump_interval(u, n);
	if (kp_update_movement_state(g, n, 1)) return 1;
	k = u->path[n];
	c = kp_unit_from_ref(g, kp_path_u32(g, k, P_BLOCKER));
	u->movement_flags[n] = kp_bor(u->movement_flags[n], 1);
	u->velocity_direction2[n] = kp_path_u8(g, k, P_SLIDE);
	kp_flingy_turn_velocity(g, n);
	u->velocity_direction[n] = u->heading2[n];
	kp_flingy_turn_heading(g, n);
	kp_sprite_set_facing(g->w, kp_unit_sprite(g, n), (int32_t)u->heading[n]);
	if (u->movement_type[n] == 2) u->speed1[n] = u->top_speed[n];
	else kp_flingy_progress_speed(g, n);
	dir = u->velocity_direction[n];
	speed = u->speed1[n];
	u->speed2[n] = speed;
	left = speed;
	while (left > 0) {
		int64_t step = left > 0x100 ? 0x100 : left;
		left -= 0x100;
		u->movement_flags[n] = kp_band(u->movement_flags[n], 0xfe);
		kp_prepare_movement_toward(g, n, kp_path_u8(g, k, P_SLIDE), step);
		if (kp_ground_unit_collision_any(g, n) != 0 || kp_terrain_collision(g, n)) {
			next_state = FORCE_FREE;
			break;
		}
		kp_commit_unit_movement(g, n);
		u->velocity_direction[n] = dir;
		kp_flingy_update_desired(g, n);
		kp_prepare_movement_toward(g, n, u->heading2[n], speed);
		after = kp_ground_unit_collision_any(g, n);
		if (after != c) break;
	}
	u->speed1[n] = u->speed2[n] = speed;
	u->heading2[n] = u->velocity_direction[n] = dir;
	kp_flingy_recompute_velocity(g, n);
	if (next_state != SLIDE) {
		u->movement_state[n] = next_state;
		return 0;
	}
	if (after == 0) {
		u->movement_flags[n] = kp_bor(u->movement_flags[n], 1);
		k = u->path[n];
		if (k != 0 && kp_mod(kp_scalar(g, KP_FRAME) - kp_path_u32(g, k, P_FRAME), 4294967296LL) < 0x96) {
			u->movement_state[n] = FOLLOW;
			return 0;
		}
		u->movement_state[n] = BLOCKED_REPATH;
	}
	return 0;
}

/* MoveState_ForceMoveFree 0x42bb00 (33). */
static int state_force_free(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	bump_interval(u, n);
	if (at_move_target(g, n) || kp_band(u->movement_flags[n], 4) != 0) {
		u->movement_state[n] = ARRIVED;
		return 0;
	}
	kp_flingy_set_speed(g, n, 0);
	kp_path_free(g, n);
	u->movement_state[n] = BLOCKED_REPATH;
	return 1;
}

/* MoveState_TerrainBlocked 0x42b700 (34). */
static int state_terrain_blocked(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	bump_interval(u, n);
	if (kp_update_movement_state(g, n, 1)) return 1;
	kp_flingy_set_speed(g, n, kp_path_u32(g, u->path[n], P_SPEED));
	kp_prepare_flingy_movement(g, n);
	if (!kp_terrain_collision(g, n) && kp_ground_unit_collision_any(g, n) == 0) {
		kp_commit_unit_movement(g, n);
		u->movement_flags[n] = kp_bor(u->movement_flags[n], 1);
		u->movement_state[n] = FOLLOW;
		return 0;
	}
	stop_walking(g, n);
	if (!faces_waypoint(g, n)) {
		kp_flingy_commit_turn(g, n);
		return 0;
	}
	kp_path_w8(g, u->path[n], P_SLIDE, kp_mod(kp_terrain_slide_direction(g, n), 256));
	u->movement_state[n] = TERRAIN_SLIDE;
	return 0;
}

/* MoveState_TerrainSlide 0x42c470 (35). */
static int state_terrain_slide(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t k, facing, slide, speed, x, y;
	bump_interval(u, n);
	if (kp_update_movement_state(g, n, 1)) return 1;
	k = u->path[n];
	u->movement_flags[n] = kp_band(u->movement_flags[n], 0xfe);
	facing = u->velocity_direction[n];
	slide = kp_path_u8(g, k, P_SLIDE);
	kp_prepare_movement_in_direction(g, n, slide);
	u->movement_flags[n] = kp_bor(u->movement_flags[n], 1);
	u->velocity_direction[n] = facing;
	u->velocity_direction2[n] = slide;
	kp_flingy_turn_velocity(g, n);
	if (kp_ground_unit_collision_any(g, n) != 0 || kp_terrain_collision(g, n)) {
		u->movement_state[n] = FORCE_FREE;
		return 0;
	}
	kp_commit_unit_movement(g, n);
	speed = u->speed2[n];
	kp_flingy_set_speed(g, n, kp_path_u32(g, k, P_SPEED));
	sprite_pos(g, n, &x, &y);
	kp_prepare_movement_in_direction(g, n, kp_direction_to(g, x, y, u->waypoint_x[n], u->waypoint_y[n]));
	kp_flingy_set_speed(g, n, speed);
	if (!kp_terrain_collision(g, n)) {
		u->movement_flags[n] = kp_bor(u->movement_flags[n], 1);
		u->movement_state[n] = FOLLOW;
	}
	return 0;
}

/* MoveState_PathFailed 0x42b370 (15). */
static int state_path_failed(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	u->collision_interval[n] = 10;
	if (kp_band(u->movement_flags[n], 2) != 0 && kp_band(u->status[n], STATUS_LOCKS) == 0) {
		u->movement_flags[n] = kp_band(u->movement_flags[n], 0xfd);
		kp_sprite_play(g->w, kp_unit_sprite(g, n), ANIM_WALKING_TO_IDLE, 1);
	}
	u->movement_state[n] = FAILED_WAIT;
	return 0;
}

/* MoveState_PathFailedWait 0x42bfd0 (16). */
static int state_path_failed_wait(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t c;
	if (kp_update_movement_state(g, n, 0)) return 1;
	c = u->collision_interval[n];
	if (c < FAILED_WAIT_STEPS) {
		u->collision_interval[n] = c + 1;
		return 0;
	}
	if (kp_advance_path_waypoint(g, n, u->move_x[n], u->move_y[n])) {
		u->movement_state[n] = FOLLOW;
		return 1;
	}
	kp_halt_at_target(g, n);
	u->movement_state[n] = ARRIVED;
	return 0;
}

static int64_t min64(int64_t a, int64_t b) { return a < b ? a : b; }
static int64_t max64(int64_t a, int64_t b) { return a > b ? a : b; }

/* ClampMoveTargetToMap 0x42aa60. */
static void clamp_move_target(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t mx = u->move_x[n], my = u->move_y[n], x, y, t;
	sprite_pos(g, n, &x, &y);
	if (mx == x && my == y) return;
	t = u->type[n];
	x = min64(max64(mx, kp_s16v(KP_DAT(g, units, unit_extents, t * 4))), g->map_w_px - kp_s16v(KP_DAT(g, units, unit_extents, t * 4 + 2)) - 1);
	y = min64(max64(my, kp_s16v(KP_DAT(g, units, unit_extents, t * 4 + 1))), g->map_h_px - kp_s16v(KP_DAT(g, units, unit_extents, t * 4 + 3)) - 1);
	if (x == mx && y == my) return;
	kp_flingy_set_move_target(g, n, x, y);
	u->status[n] = kp_bor(u->status[n], STATUS_INTERCEPT);
}

/* MoveState_Fly 0x42b7f0 (7). */
static int state_fly(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	KpMove *mv = &g->mv;
	int64_t t, left, up, right, down;
	int drifted;
	clamp_move_target(g, n);
	kp_prepare_flingy_movement(g, n);
	drifted = kp_air_repulsion(g, n);
	t = u->type[n];
	left = kp_s16v(KP_DAT(g, units, unit_extents, t * 4));
	up = kp_s16v(KP_DAT(g, units, unit_extents, t * 4 + 1));
	right = kp_s16v(KP_DAT(g, units, unit_extents, t * 4 + 2));
	down = kp_s16v(KP_DAT(g, units, unit_extents, t * 4 + 3));
	if (mv->next_x - left < 0) mv->next_x = left;
	if (right + mv->next_x >= g->map_w_px) mv->next_x = kp_s16v(g->map_w_px - right - 1);
	if (mv->next_y - up < 0) mv->next_y = up;
	if (down + mv->next_y >= g->map_h_px) mv->next_y = kp_s16v(g->map_h_px - down - 1);
	kp_commit_unit_movement(g, n);
	kp_air_repulsion_settle(g, n, drifted);
	return 0;
}

static void turn_in_place(KpGame *g, int64_t n)
{
	kp_flingy_update_desired(g, n);
	kp_flingy_update_walking(g, n);
	kp_flingy_commit_turn(g, n);
}

/* ClearTurningNearDesired 0x40c6f0. */
static void settle_turning(KpUnits *u, int64_t n)
{
	int64_t d = kp_mod(u->velocity_direction2[n] - u->heading[n], 256);
	if (kp_mod(d + TURN_SETTLED, 256) <= 2 * TURN_SETTLED) u->movement_flags[n] = kp_band(u->movement_flags[n], 0xfe);
}

/* MoveState_Turret 0x42c610 (3). */
static int state_turret(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t x, y;
	kp_scalar_set(g, KP_MOVE_REVEAL, 0);
	sprite_pos(g, n, &x, &y);
	kp_set_move_target_point(g, n, x, y);
	settle_turning(u, n);
	if (kp_band(u->status[n], STATUS_TURRET_ALIGNED) != 0) {
		kp_flingy_update_walking(g, n);
		return 0;
	}
	turn_in_place(g, n);
	return 0;
}

typedef int (*State)(KpGame *g, int64_t n);

static State state_of(int64_t state)
{
	switch (state) {
	case INIT: return state_init;
	case AT_REST: return state_at_rest;
	case DORMANT: return state_dormant;
	case CHECK_ILLEGAL: return state_check_illegal;
	case START_MOVE: return state_start_path;
	case RETRY: return state_retry;
	case REPATH_STALE: return state_repath_stale;
	case REPATH: return state_repath;
	case ARRIVED: return state_arrived;
	case FOLLOW: return state_follow_path;
	case SCOUT: return state_scout_path;
	case BLOCKED_BY_UNIT: return state_fix_collision;
	case WAIT_FREE: return state_wait_free;
	case FACE_TARGET: return state_face_target;
	case REPATH_MOVERS: return state_repath_movers;
	case BLOCKED_REPATH: return state_repath_blocked;
	case REPATH_FAILED: return state_repath_around;
	case SLIDE_START: return state_slide_check;
	case SLIDE_PREP: return state_slide_prep;
	case SLIDE: return state_slide_free;
	case FORCE_FREE: return state_force_free;
	case BLOCKED_BY_TERRAIN: return state_terrain_blocked;
	case TERRAIN_SLIDE: return state_terrain_slide;
	case FLY: return state_fly;
	case ROUTE: return state_route;
	case TURRET: return state_turret;
	case FAILED: return state_path_failed;
	case FAILED_WAIT: return state_path_failed_wait;
	}
	return NULL;
}

void kp_execute_movement(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	if (kp_scalar(g, KP_UPDATE_TILES) == KP_NIL) kp_scalar_clear(g, KP_MOVE_REVEAL);
	else kp_scalar_set(g, KP_MOVE_REVEAL, kp_scalar(g, KP_UPDATE_TILES));
	for (;;) {
		int64_t state = u->movement_state[n];
		State f = state_of(state);
		int r;
		if (f) {
			r = f(g, n);
		} else if (state == REFOLLOW) {
			u->movement_state[n] = FOLLOW;
			r = 1;
		} else if (state == NOBRK) {
			if (kp_band(u->status[n], STATUS_NOBRK) != 0) return;
			u->movement_state[n] = INIT;
			r = 1;
		} else if (state == HIDDEN) {
			kp_scalar_set(g, KP_MOVE_REVEAL, 0);
			return;
		} else if (state == TURN_HIDDEN || state == TURN) {
			if (state == TURN_HIDDEN) kp_scalar_set(g, KP_MOVE_REVEAL, 0);
			turn_in_place(g, n);
			return;
		} else if (state == REPLACED_ON_LEGAL) {
			r = state_move_to_legal(g, n);
		} else {
			return; /* 2 (dead), 8 and the unused states */
		}
		if (r == 0) return;
	}
}

void kp_stop_flingy(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t flags = u->movement_flags[n], h, dir, x, y, sx, sy;
	if (kp_band(flags, 4) != 0) return;
	flags = kp_band(flags, 0x8f);
	u->move_x[n] = u->x[n];
	u->move_y[n] = u->y[n];
	u->movement_flags[n] = flags;
	u->move_unit[n] = 0;
	u->waypoint_x[n] = u->move_x[n];
	u->waypoint_y[n] = u->move_y[n];
	if (kp_band(flags, 2) == 0 || u->movement_type[n] != 0) return;
	h = kp_flingy_halt_distance(g, n);
	dir = kp_mod(u->velocity_direction[n], 256);
	x = kp_s16v(kp_sarv(kp_sarv(kp_mulv(KP_DIR_X[dir], h), 8) + u->halt_x[n], 8));
	y = kp_s16v(kp_sarv(kp_sarv(kp_mulv(KP_DIR_Y[dir], h), 8) + u->halt_y[n], 8));
	u->move_x[n] = u->waypoint_x[n] = x;
	u->move_y[n] = u->waypoint_y[n] = y;
	sprite_pos(g, n, &sx, &sy);
	if (x != sx || y != sy) u->movement_flags[n] = kp_bor(flags, 4);
}

/* StopMovement 0x43a490. */
void kp_stop_movement(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t x, y;
	if (kp_band(u->pathing_flags[n], 2) != 0) u->pathing_flags[n] = kp_bor(u->pathing_flags[n], 4);
	kp_stop_flingy(g, n);
	x = u->move_x[n];
	y = u->move_y[n];
	kp_clamp_to_map(g, u->type[n], &x, &y);
	if (x != u->move_x[n] || y != u->move_y[n]) {
		u->move_x[n] = u->waypoint_x[n] = x;
		u->move_y[n] = u->waypoint_y[n] = y;
	}
	u->recent_order_timer[n] = 15;
}

/* StopMovementAndFace 0x43a4e0. */
void kp_stop_and_face(KpGame *g, int64_t n)
{
	kp_stop_movement(g, n);
	kp_flingy_set_target_waypoint(g, n, g->u.move_x[n], g->u.move_y[n]);
}

/* FollowBaseHeading 0x43a800. */
static void follow_base(KpGame *g, int64_t sub, int64_t base, int64_t turn)
{
	KpUnits *u = &g->u;
	if (u->order_unit[sub] != 0) u->status[sub] = kp_band(u->status[sub], kp_bnot(STATUS_TURRET_ALIGNED));
	else if (u->heading[sub] == u->heading[base]) u->status[sub] = kp_bor(u->status[sub], STATUS_TURRET_ALIGNED);
	if (kp_band(u->status[sub], STATUS_TURRET_ALIGNED) != 0) {
		kp_flingy_set_heading(g, sub, u->heading[base]);
	} else {
		int64_t v = kp_mod(u->velocity_direction[sub] + turn, 256);
		u->velocity_direction[sub] = u->heading[sub] = v;
	}
	if (u->type[sub] == 4 || u->type[sub] == 0x12) {
		int64_t h = u->heading[sub], d = kp_s8((uint32_t)(u->heading[base] - h));
		if (d < -GOLIATH_ARC) u->heading[sub] = kp_mod(u->heading[base] + GOLIATH_ARC, 256);
		else if (d > GOLIATH_ARC) u->heading[sub] = kp_mod(u->heading[base] - GOLIATH_ARC, 256);
	}
}

void kp_prepare_movement(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	KpImages *w = g->w;
	int64_t before = u->velocity_direction[n], sub, status;
	int32_t ox, oy;
	kp_execute_movement(g, n);
	if (kp_scalar(g, KP_MOVE_REVEAL) == 1) kp_unit_update_vision(g, n);
	sub = u->subunit[n];
	if (kp_band(u->status[n], STATUS_COMPLETED) == 0 || sub == 0) return;
	if (kp_band(KP_DAT(g, units, special_ability_flags, u->type[n]), DAT_SUBUNIT) != 0) return;
	follow_base(g, sub, n, kp_mod(u->velocity_direction[n] - before, 256));
	kp_images_lo_offset(w, kp_check_image(w, w->spr.main[kp_unit_sprite(g, n)]), 2, 0, &ox, &oy);
	kp_flingy_set_exact_position(g, sub, u->halt_x[n], u->halt_y[n]);
	kp_image_set_offset(w, kp_check_image(w, w->spr.main[kp_unit_sprite(g, sub)]), ox & 0xff, oy & 0xff);
	w->unit = (int32_t)sub;
	w->has_unit = 1;
	status = u->status[sub];
	if (kp_band(u->movement_flags[n], 2) == 0) {
		if (kp_band(status, STATUS_TURRET_WALKING) != 0) {
			u->status[sub] = kp_band(status, kp_bnot(STATUS_TURRET_WALKING));
			if (kp_band(u->status[n], STATUS_CAN_MOVE) != 0 && kp_band(u->movement_flags[sub], 8) == 0) {
				kp_sprite_play(w, kp_unit_sprite(g, sub), ANIM_WALKING_TO_IDLE, 1);
			}
		}
	} else if (kp_band(status, STATUS_TURRET_WALKING) == 0) {
		u->status[sub] = kp_bor(status, STATUS_TURRET_WALKING);
		if (kp_band(u->movement_flags[sub], 8) == 0) kp_sprite_play(w, kp_unit_sprite(g, sub), ANIM_WALKING, 1);
	}
	kp_prepare_movement(g, sub);
	w->unit = (int32_t)n;
}
