/* The flingy part of a unit (kp_game.h). */
#include "kp_game_internal.h"

#define MOVE_FLINGY 0
#define MOVE_PARTIAL 1
#define MOVE_ISCRIPT 2
#define STATUS_SPEED_UPGRADE 0x10000000
#define TOP_SPEED_BOOSTED_MAX 0x6ab
#define TOP_SPEED_BOOSTED_MIN 0x355
#define WALK_SAMPLES 32
#define ANIM_WALKING 11

static int fast_flyer(int64_t t) { return t == 0x46 || t == 0x50 || t == 0x58; }

/* The net speed modifier count: stim, speed upgrade, ensnare. */
static int modifier(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int k = u->stim_timer[n] != 0 ? 1 : 0;
	if (kp_band(u->status[n], STATUS_SPEED_UPGRADE) != 0) k++;
	if (u->ensnare_timer[n] != 0) k--;
	return k;
}

int64_t kp_speed_modified(KpGame *g, int64_t n, int64_t v)
{
	int k = modifier(g, n);
	if (k > 0) return kp_mod(v * 2, 4294967296LL);
	if (k < 0) return v - kp_shrv(v, 2);
	return v;
}

int64_t kp_top_speed_modified(KpGame *g, int64_t n, int64_t v)
{
	int k = modifier(g, n);
	if (k < 1) {
		if (k < 0) return kp_shrv(v, 1);
		return v;
	}
	if (fast_flyer(g->u.type[n])) return TOP_SPEED_BOOSTED_MAX;
	v = kp_mod(v + kp_shrv(v, 1), 4294967296LL);
	if (v < TOP_SPEED_BOOSTED_MIN) return TOP_SPEED_BOOSTED_MIN;
	return v;
}

static int64_t flingy_of(KpGame *g, int64_t n) { return KP_DAT(g, units, graphics, g->u.type[n]); }

int64_t kp_acceleration_modified(KpGame *g, int64_t n)
{
	int64_t v = KP_DAT(g, flingy, acceleration, flingy_of(g, n));
	int k = modifier(g, n);
	if (k > 0) return kp_mod(v * 2, 65536);
	if (k < 0) v = v - kp_shrv(v, 2);
	return v;
}

int64_t kp_turn_radius_modified(KpGame *g, int64_t n)
{
	int64_t v = KP_DAT(g, flingy, turn_radius, flingy_of(g, n));
	int k = modifier(g, n);
	if (k > 0) return kp_mod(v * 2, 256);
	if (k < 0) v = v - kp_shrv(v, 2);
	return v;
}

void kp_iscript_walk_speed(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	KpImages *w = g->w;
	int32_t main, header;
	int64_t entry, sum = 0;
	KpScriptState state;
	int k;
	if (u->movement_type[n] != MOVE_ISCRIPT) return;
	main = kp_check_image(w, w->spr.main[kp_unit_sprite(g, n)]);
	header = w->img.header[main];
	if ((int64_t)kp_code_u16(w, (int64_t)header + 4) < ANIM_WALKING) return;
	entry = kp_code_u16(w, (int64_t)header + 8 + 2 * ANIM_WALKING);
	if (entry == 0) return;
	state.header = header;
	state.pc = (int32_t)entry;
	state.ret = 0;
	state.wait = 0;
	for (k = 1; k <= WALK_SAMPLES; k++) {
		KpCtx saved = kp_ctx_unit(g, n);
		int64_t speed = 0;
		if (!kp_images_run_iscript(w, main, &state, 1, 1, &speed)) speed = 0;
		kp_ctx_restore(g, saved);
		sum += kp_speed_modified(g, n, speed);
	}
	u->top_speed[n] = kp_idiv64(g, kp_s32v(sum), WALK_SAMPLES);
}

void kp_recalculate_speed(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t control = KP_DAT(g, flingy, movement_control, flingy_of(g, n));
	if (control != MOVE_FLINGY && control != MOVE_PARTIAL) {
		kp_iscript_walk_speed(g, n);
		return;
	}
	u->top_speed[n] = kp_top_speed_modified(g, n, KP_DAT(g, flingy, speed, flingy_of(g, n)));
	u->acceleration[n] = kp_acceleration_modified(g, n);
	u->turn_speed[n] = kp_turn_radius_modified(g, n);
}

void kp_set_speed_upgrade(KpGame *g, int64_t n)
{
	g->u.status[n] = kp_bor(g->u.status[n], STATUS_SPEED_UPGRADE);
	kp_recalculate_speed(g, n);
}

int kp_flingy_init(KpGame *g, int64_t n, int64_t flingy, int64_t x, int64_t y, int64_t owner, int64_t heading)
{
	KpUnits *u = &g->u;
	int32_t s;
	u->flingy[n] = flingy;
	u->movement_flags[n] = 0;
	u->speed2[n] = 0;
	u->top_speed[n] = KP_DAT(g, flingy, speed, flingy);
	u->acceleration[n] = KP_DAT(g, flingy, acceleration, flingy);
	u->turn_speed[n] = KP_DAT(g, flingy, turn_radius, flingy);
	u->movement_type[n] = KP_DAT(g, flingy, movement_control, flingy);
	x = kp_s16v(x);
	y = kp_s16v(y);
	u->x[n] = x;
	u->y[n] = y;
	u->halt_x[n] = x * 256;
	u->halt_y[n] = y * 256;
	if (x != u->move_x[n] || y != u->move_y[n]) {
		u->move_x[n] = x;
		u->move_y[n] = y;
		u->move_unit[n] = 0;
		u->movement_flags[n] = 1;
		u->waypoint_x[n] = x;
		u->waypoint_y[n] = y;
	}
	if (x != u->target_waypoint_x[n] || y != u->target_waypoint_y[n]) {
		u->target_waypoint_x[n] = x;
		u->target_waypoint_y[n] = y;
	}
	u->heading[n] = u->velocity_direction[n] = heading;
	u->hp[n] = 1; /* the thingy's "+8 = 1" of 0x4183b0 lands in the unit's hit points */
	s = kp_sprite_create(g->w, (int32_t)KP_DAT(g, flingy, sprite, flingy), (int32_t)kp_mod(x, 65536),
		(int32_t)kp_mod(y, 65536), (int32_t)owner);
	u->sprite[n] = s;
	if (s == KP_NONE) return 0;
	kp_sprite_set_facing(g->w, s, (int32_t)u->heading[n]);
	return 1;
}

static void velocity(KpGame *g, int64_t n, int64_t h)
{
	KpUnits *u = &g->u;
	u->speed_x[n] = kp_sarv(kp_mulv(KP_DIR_X[h], u->speed1[n]), 8);
	u->speed_y[n] = kp_sarv(kp_mulv(KP_DIR_Y[h], u->speed1[n]), 8);
}

static void sync_target_waypoint(KpGame *g, int64_t n, int32_t s)
{
	KpUnits *u = &g->u;
	int64_t sx = kp_s16v(g->w->spr.x[s]), sy = kp_s16v(g->w->spr.y[s]);
	if (sx != u->target_waypoint_x[n] || sy != u->target_waypoint_y[n]) {
		u->target_waypoint_x[n] = sx;
		u->target_waypoint_y[n] = sy;
	}
}

void kp_flingy_set_heading(KpGame *g, int64_t n, int64_t heading)
{
	KpUnits *u = &g->u;
	int32_t s;
	heading = kp_mod(heading, 256);
	u->velocity_direction[n] = u->heading[n] = u->heading2[n] = heading;
	velocity(g, n, heading);
	s = kp_unit_sprite(g, n);
	sync_target_waypoint(g, n, s);
	kp_sprite_set_facing(g->w, s, (int32_t)heading);
}

void kp_flingy_set_position(KpGame *g, int64_t n, int64_t x, int64_t y)
{
	KpUnits *u = &g->u;
	x = kp_s16v(x);
	y = kp_s16v(y);
	u->halt_x[n] = x * 256;
	u->x[n] = x;
	u->y[n] = y;
	u->halt_y[n] = y * 256;
	kp_sprite_move(g->w, kp_unit_sprite(g, n), (int32_t)kp_mod(x, 65536), (int32_t)kp_mod(y, 65536));
}

/* ClampFlingyToMap 0x40bae0. */
void kp_flingy_clamp_to_map(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t w = kp_mod(g->map_w_px, 65536), h = kp_mod(g->map_h_px, 65536);
	if (u->x[n] < 0) kp_flingy_set_position(g, n, 0, u->y[n]);
	else if (u->x[n] >= w) kp_flingy_set_position(g, n, w - 1, u->y[n]);
	if (u->y[n] < 0) kp_flingy_set_position(g, n, u->x[n], 0);
	else if (u->y[n] >= h) kp_flingy_set_position(g, n, u->x[n], h - 1);
}

void kp_flingy_set_exact_position(KpGame *g, int64_t n, int64_t hx, int64_t hy)
{
	KpUnits *u = &g->u;
	int64_t x = kp_s16v(kp_sarv(hx, 8)), y = kp_s16v(kp_sarv(hy, 8));
	u->x[n] = x;
	u->y[n] = y;
	u->halt_x[n] = hx;
	u->halt_y[n] = hy;
	kp_sprite_move(g->w, kp_unit_sprite(g, n), (int32_t)kp_mod(x, 65536), (int32_t)kp_mod(y, 65536));
}

void kp_flingy_set_target_waypoint(KpGame *g, int64_t n, int64_t x, int64_t y)
{
	KpUnits *u = &g->u;
	if (x != u->target_waypoint_x[n] || y != u->target_waypoint_y[n]) {
		u->target_waypoint_x[n] = kp_s16v(x);
		u->target_waypoint_y[n] = kp_s16v(y);
	}
}

void kp_flingy_set_move_target(KpGame *g, int64_t n, int64_t x, int64_t y)
{
	KpUnits *u = &g->u;
	if (x != u->move_x[n] || y != u->move_y[n]) {
		u->move_x[n] = kp_s16v(x);
		u->move_y[n] = kp_s16v(y);
		u->movement_flags[n] = kp_bor(kp_band(u->movement_flags[n], 0xfb), 1);
		u->move_unit[n] = 0;
		u->waypoint_x[n] = u->move_x[n];
		u->waypoint_y[n] = u->move_y[n];
	}
}

void kp_flingy_set_move_target_unit(KpGame *g, int64_t n, int64_t target)
{
	KpUnits *u = &g->u;
	int32_t s = kp_unit_sprite(g, target);
	int64_t x = kp_s16v(g->w->spr.x[s]), y = kp_s16v(g->w->spr.y[s]);
	if (x == u->move_x[n] && y == u->move_y[n] && target == u->move_unit[n]) return;
	u->move_x[n] = x;
	u->move_y[n] = y;
	u->movement_flags[n] = kp_bor(kp_band(u->movement_flags[n], 0xfb), 1);
	u->move_unit[n] = target;
	u->waypoint_x[n] = x;
	u->waypoint_y[n] = y;
}

void kp_flingy_to_idle(KpGame *g, int64_t n)
{
	KpImages *w = g->w;
	int32_t s = kp_unit_sprite(g, n), main = kp_check_image(w, w->spr.main[s]), anim = w->img.anim[main], target = -1;
	if (anim == 2 || anim == 5) {
		target = 8;
	} else if (anim == 3 || anim == 6) {
		target = 9;
	} else if (anim == 7) {
		target = 12;
	} else if (anim == 13) {
		if (w->spr.id[s] == 0x1e9) target = 12;
	} else if (anim == 15) {
		int32_t id = w->spr.id[s];
		if (id == 0xef || id == 0x8c || id == 0xc2) target = 8;
	}
	if (target >= 0) {
		int32_t i = w->spr.images[s].head;
		while (i != KP_NONE) {
			kp_image_play(w, i, target);
			i = w->img.next[i];
		}
	}
	g->u.movement_flags[n] = kp_band(g->u.movement_flags[n], 0xf7);
}

/* --- the unit hooks of the IScript VM (dispatched by kp_hooks.c) --- */

void kp_flingy_set_speed(KpGame *g, int64_t n, int64_t v)
{
	KpUnits *u = &g->u;
	u->speed2[n] = v;
	if (u->speed1[n] != v) {
		u->speed1[n] = v;
		velocity(g, n, u->heading2[n]);
	}
}

void kp_flingy_turn(KpGame *g, int64_t n, int64_t steps)
{
	KpUnits *u = &g->u;
	int64_t h = kp_mod(u->heading[n] + steps * 8, 256);
	int32_t s;
	u->velocity_direction[n] = u->heading[n] = u->heading2[n] = h;
	velocity(g, n, h);
	s = kp_unit_sprite(g, n);
	sync_target_waypoint(g, n, s);
	kp_sprite_set_facing(g->w, s, (int32_t)h);
}

void kp_flingy_set_spawn_frame(KpGame *g, int64_t n, int64_t d)
{
	KpUnits *u = &g->u;
	int64_t k = kp_mod(d * 8, 256);
	int64_t x = kp_sarv(kp_shlv(KP_DIR_X[k], 12), 8) + u->halt_x[n];
	int64_t y = kp_sarv(kp_shlv(KP_DIR_Y[k], 12), 8) + u->halt_y[n];
	x = kp_sarv(x, 8);
	y = kp_sarv(y, 8);
	if (x != u->target_waypoint_x[n] || y != u->target_waypoint_y[n]) {
		u->target_waypoint_x[n] = kp_s16v(x);
		u->target_waypoint_y[n] = kp_s16v(y);
	}
}

void kp_nobrk_end(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int32_t s;
	u->status[n] = kp_band(u->status[n], kp_bnot(0x2000));
	s = kp_unit_sprite(g, n);
	g->w->spr.flags[s] &= 0x7f;
	if (u->queue_head[n] != -1 && kp_band(u->user_action[n], 1) != 0) {
		kp_reset_to_idle_animation(g, n);
		kp_order_activate(g, n);
	}
}

/* IsFacingTargetWaypoint 0x424810: at the waypoint already, or heading toward it. */
int kp_facing_target_waypoint(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int32_t s;
	if (u->target_waypoint_x[n] == u->x[n] && u->target_waypoint_y[n] == u->y[n]) return 1;
	s = kp_unit_sprite(g, n);
	return u->heading[n] == kp_direction_to(g, kp_s16v(g->w->spr.x[s]), kp_s16v(g->w->spr.y[s]), u->target_waypoint_x[n],
		u->target_waypoint_y[n]);
}
