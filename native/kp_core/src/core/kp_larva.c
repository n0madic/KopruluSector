/* Larvae and the Zerg unit morphs (kp_game.h), ported from the atlas (P12, P13; docs/research/native-zerg-construction.md):
 * the unit morph order 42 (OrderZergUnitMorph 0x43bac0: a Larva, Hydralisk or Mutalisk becomes an Egg, Lurker Egg or
 * Cocoon), the birth order 41 (OrderZergBirth 0x43bd50) with the twin of a two-unit egg (0x43b5e0, CopyStatusEffects
 * 0x434880), the Larva order 77
 * (OrderLarva 0x43cd90 with ChkLarvaStray 0x43c3b0 and GetLarvaReturnPoint 0x43d800), a Hatchery's, Lair's or
 * Hive's secondary order SpawningLarva 78 (0x43d0d0) with the count of its Larvae (0x43d910), the spawn point
 * (FindLarvaSpawnPoint 0x43d360, IsLarvaSpawnPointValid 0x43c490) and a new Larva's side (InitLarvaSide 0x43d990).
 *
 * The union of a Hatchery: +0xca the Larva timer, +0xd0/+0xd2/+0xd4/+0xd6 the left, top, right and bottom side
 * weights of its workers' traffic (RecordDepotApproachSide 0x43da30). */
#include "kp_game_internal.h"

#define LARVA 35
#define EGG 36
#define ZERGLING 37
#define HYDRALISK 38
#define DRONE 41
#define OVERLORD 42
#define MUTALISK 43
#define SCOURGE 47
#define COCOON 59
#define LURKER_EGG 97
#define NO_TYPE 228
#define STATUS_COMPLETED 0x1
#define STATUS_AIR 0x4
#define DAT_TWO_IN_EGG 0x400
#define SIGNAL_BIRTH 0x4
#define ANIM_SPECIAL1 13
#define ORDER_ZERG_BIRTH 41
#define CONTROLLER_COMPUTER 1 /* the runtime player type */
#define SPRITE_SELECTED 0x8
#define LO_DUST 3 /* the LO kind of the landing-dust points */
#define ZERGLING_FACING 18 /* the twins' facing, in 8-step directions */
#define SCOURGE_FACING 2
#define HOVER_FLAGS 0xc1 /* units.dat's movement flags of a hovering type */
#define HOVER_DROP 7
#define AIR_DROP 42
#define NEUTRAL_PLAYER 11
#define LARVA_LIMIT 3
#define LARVA_RADIUS 256
#define LARVA_PERIOD 37
#define LARVA_RETRY 3
#define LARVA_NEAR 10
#define LARVA_STEP 10
#define LARVA_RANDOM 20 /* the RNG source of a Larva's step */
#define SPRITE_HIDDEN 0x20
#define TILE_CREEP 0x400000
#define TILE_CREEP_OR_WALKABLE 0x410000
#define TILE_OCCUPIED 0x08000000
#define SIDE_GAP 10   /* a spawn point's distance from the side line */
#define APPROACH 22   /* the probe of the point's approach */
#define NO_WEIGHT 101 /* above any weight a side may win with */
#define U_TIMER 0xca
#define W_LEFT 0xd0
#define W_TOP 0xd2
#define W_RIGHT 0xd4
#define W_BOTTOM 0xd6

static int64_t sprite_x(KpGame *g, int64_t n) { return kp_s16v(g->w->spr.x[kp_unit_sprite(g, n)]); }
static int64_t sprite_y(KpGame *g, int64_t n) { return kp_s16v(g->w->spr.y[kp_unit_sprite(g, n)]); }
static int64_t weight(KpGame *g, int64_t n, int offset) { return kp_s16v(kp_union_get(g, n, offset, 2)); }

/* The Larvae of parent p (0x43d910 over FindUnitInRadiusBox 0x442b20), counted up to 3. */
static int larvae(KpGame *g, int64_t p)
{
	KpUnits *u = &g->u;
	int64_t x = sprite_x(g, p), y = sprite_y(g, p), *found;
	int count, k, n = 0;
	found = kp_query_rect(g, kp_s16v(x - LARVA_RADIUS), kp_s16v(y - LARVA_RADIUS), kp_s16v(x + LARVA_RADIUS),
		kp_s16v(y + LARVA_RADIUS), &count);
	for (k = 0; k < count && n < LARVA_LIMIT; k++) {
		int64_t c = found[k];
		if (u->type[c] == LARVA && kp_band(g->w->spr.flags[kp_unit_sprite(g, c)], SPRITE_HIDDEN) == 0
			&& u->connected[c] == p) {
			n++;
		}
	}
	kp_query_release(g, found);
	return n;
}

/* IsLarvaSpawnPointValid 0x43c490: (x, y) on creep and inside the map for a Larva, its approach (x + dx, y + dy) inside
 * it too (16-bit sums) on creep or walkable ground that no building occupies. */
static int valid_point(KpGame *g, int64_t x, int64_t y, int64_t dx, int64_t dy)
{
	int64_t cx = kp_s16v(x), cy = kp_s16v(y), ax, ay;
	kp_clamp_to_map(g, LARVA, &cx, &cy);
	if (cx != x || cy != y || !kp_tile_has_flags(g, x, y, TILE_CREEP, 0)) return 0;
	ax = kp_s16v(x + dx);
	ay = kp_s16v(y + dy);
	kp_clamp_to_map(g, LARVA, &ax, &ay);
	if (ax != x + dx || ay != y + dy) return 0;
	return kp_tile_has_flags(g, ax, ay, TILE_CREEP_OR_WALKABLE, TILE_OCCUPIED);
}

/* The first valid of the points (x0, y0) and (x1, y1) with the approach (dx, dy). */
static int valid_pair(KpGame *g, int64_t x0, int64_t y0, int64_t x1, int64_t y1, int64_t dx, int64_t dy, int64_t *ox,
	int64_t *oy)
{
	if (valid_point(g, x0, y0, dx, dy)) {
		*ox = x0;
		*oy = y0;
		return 1;
	}
	if (valid_point(g, x1, y1, dx, dy)) {
		*ox = x1;
		*oy = y1;
		return 1;
	}
	return 0;
}

/* FindLarvaSpawnPoint 0x43d360: the midpoint of the least used side first (bottom, left, right, top; a side's weight
 * must be below the best so far, the top wins at once), then the points half a side away from the midpoints (the
 * bottom without the weight test). */
static int spawn_point(KpGame *g, int64_t p, int64_t *ox, int64_t *oy)
{
	struct Side {
		int weight;
		int64_t dx, dy;
	};
	static const struct Side SIDES[4] = { { W_BOTTOM, 0, APPROACH }, { W_LEFT, -APPROACH, 0 }, { W_RIGHT, APPROACH, 0 },
		{ W_TOP, 0, -APPROACH } };
	int64_t px = sprite_x(g, p), py = sprite_y(g, p), l, t, r, b, hw, hh, best = NO_WEIGHT, x, y;
	int found = 0, k, pass;
	kp_type_box(g, g->u.type[p], px, py, &l, &t, &r, &b);
	hw = kp_s16v(r - l + 1) / 2;
	hh = kp_s16v(b - t + 1) / 2;
	for (pass = 0; pass < 2; pass++) {
		for (k = 0; k < 4; k++) {
			const struct Side *s = &SIDES[k];
			int64_t w = weight(g, p, s->weight), x0, y0, x1, y1;
			int ok;
			if ((pass == 0 || k != 0) && w >= best) continue;
			x0 = x1 = s->dx < 0 ? l - SIDE_GAP : s->dx > 0 ? r + SIDE_GAP : px;
			y0 = y1 = s->dy < 0 ? t - SIDE_GAP : s->dy > 0 ? b + SIDE_GAP : py;
			if (pass == 0) {
				ok = valid_point(g, x0, y0, s->dx, s->dy);
				x = x0;
				y = y0;
			} else {
				if (s->dx == 0) {
					x0 = px - hw;
					x1 = px + hw;
				} else {
					y0 = py - hh;
					y1 = py + hh;
				}
				ok = valid_pair(g, x0, y0, x1, y1, s->dx, s->dy, &x, &y);
			}
			if (!ok) continue;
			*ox = x;
			*oy = y;
			if (s->weight == W_TOP) return 1;
			best = w;
			found = 1;
		}
		if (found) return 1;
	}
	return 0;
}

/* InitLarvaSide 0x43d990: the side of its parent a new Larva starts on (order state): 0 left, 1 top, 2 right, 3 bottom. */
static void init_side(KpGame *g, int64_t n, int64_t p)
{
	int64_t l, t, r, b, x = sprite_x(g, n), y = sprite_y(g, n);
	kp_type_box(g, g->u.type[p], sprite_x(g, p), sprite_y(g, p), &l, &t, &r, &b);
	if (x < l) g->u.order_state[n] = 0;
	else if (y < t) g->u.order_state[n] = 1;
	else g->u.order_state[n] = x <= r ? 3 : 2;
}

/* ChkLarvaStray 0x43c3b0: (x, y) (unsigned) is still on the Larva's side of its parent (the side lines are WORDs). */
static int on_side(KpGame *g, int64_t n, int64_t x, int64_t y)
{
	int64_t p = g->u.connected[n], l, t, r, b;
	if (p == 0) return 1;
	kp_type_box(g, g->u.type[p], sprite_x(g, p), sprite_y(g, p), &l, &t, &r, &b);
	x = kp_u32v(x);
	y = kp_u32v(y);
	switch (g->u.order_state[n]) {
	case 0: return !(kp_mod(l, 65536) < x);
	case 1: return !(kp_mod(t, 65536) < y);
	case 2: return !(x < kp_mod(r, 65536));
	case 3: return !(y < kp_mod(b, 65536));
	}
	kp_game_fail(g, "larva: ChkLarvaStray: 1 (side %lld)", (long long)g->u.order_state[n]);
	return 0;
}

/* GetLarvaReturnPoint 0x43d800: just outside the parent on the Larva's side; (x, y) unchanged without a parent. */
static void return_point(KpGame *g, int64_t n, int64_t *x, int64_t *y)
{
	int64_t p = g->u.connected[n], px, py, l, t, r, b;
	if (p == 0) return;
	px = sprite_x(g, p);
	py = sprite_y(g, p);
	kp_type_box(g, g->u.type[p], px, py, &l, &t, &r, &b);
	switch (g->u.order_state[n]) {
	case 0: *x = l - SIDE_GAP; *y = py; return;
	case 1: *x = px; *y = t - SIDE_GAP; return;
	case 2: *x = r + SIDE_GAP; *y = py; return;
	case 3: *x = px; *y = b + SIDE_GAP; return;
	}
	kp_game_fail(g, "larva: ChkLarvaStray: 2 (side %lld)", (long long)g->u.order_state[n]);
}

/* OrderLarva 0x43cd90 (order 77): the Larva keeps near its parent and wanders on creep in steps of 10 pixels on its
 * side; off creep it dies. */
void kp_order_larva(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t p = u->connected[n], m = u->move_unit[n], x = sprite_x(g, n), y = sprite_y(g, n), r, nx, ny;
	int arrived, astray;
	if (p != 0 && !kp_unit_box_within(g, n, LARVA_NEAR, p)) kp_request_move_to_unit(g, n, p);
	arrived = u->move_x[n] == x && u->move_y[n] == y;
	astray = m != 0 && (u->move_x[n] != sprite_x(g, m) || u->move_y[n] != sprite_y(g, m))
		&& !kp_tile_has_flags(g, u->move_x[n], u->move_y[n], TILE_CREEP, 0);
	if (!arrived && !astray) return;
	if ((p == 0 || m != p) && !kp_tile_has_flags(g, x, y, TILE_CREEP, 0)) {
		kp_unit_kill(g, n);
		return;
	}
	r = kp_rng_game_raw(g->rng, LARVA_RANDOM);
	nx = x + (kp_band(r, 8) != 0 ? LARVA_STEP : -LARVA_STEP);
	ny = y + (kp_band(r, 0x80) != 0 ? LARVA_STEP : -LARVA_STEP);
	if (kp_mod(nx, 65536) >= kp_mod(g->map_w_px, 65536) || kp_mod(ny, 65536) >= kp_mod(g->map_h_px, 65536)
		|| !kp_tile_has_flags(g, nx, ny, TILE_CREEP, 0)) {
		return;
	}
	if (!on_side(g, n, nx, ny)) {
		if (on_side(g, n, x, y)) return;
		return_point(g, n, &nx, &ny);
	}
	kp_request_move_to_point(g, n, nx, ny);
}

/* SecondarySpawningLarva 0x43d0d0 (secondary order 78). */
void kp_secondary_spawning_larva(KpGame *g, int64_t p)
{
	KpUnits *u = &g->u;
	int64_t timer, x, y, n;
	if (u->queue_timer[p] != 0 || u->owner[p] == NEUTRAL_PLAYER || larvae(g, p) >= LARVA_LIMIT) return;
	timer = kp_union_get(g, p, U_TIMER, 1);
	if (timer != 0) {
		kp_union_set(g, p, U_TIMER, 1, timer - 1);
		return;
	}
	kp_union_set(g, p, U_TIMER, 1, LARVA_PERIOD);
	if (!spawn_point(g, p, &x, &y)) {
		kp_union_set(g, p, U_TIMER, 1, LARVA_RETRY);
		return;
	}
	n = kp_unit_create(g, LARVA, x, y, u->owner[p]);
	if (n == 0) return;
	kp_unit_complete_in_place(g, n);
	u->connected[n] = p;
	init_side(g, n, p);
	/* TODO: AddBuildingToBuilderTown 0x486200 for a computer's Hatchery once towns exist (the AIScript VM of plan
	 * phase 7) */
	if (u->ai[p] != 0) kp_game_fail(g, "larva: a computer's new Larva joins its town (0x486200)");
}

/* CopyStatusEffects 0x434880: the timed statuses (+0x110..+0x12f) of src, their overlays and the speed they give. */
static void copy_status(KpGame *g, int64_t dst, int64_t src)
{
	KpUnits *u = &g->u;
#define COPY(f) u->f[dst] = u->f[src];
	COPY(remove_timer) COPY(matrix_damage) COPY(matrix_timer) COPY(stim_timer) COPY(ensnare_timer) COPY(lockdown_timer)
	COPY(irradiate_timer) COPY(stasis_timer) COPY(plague_timer) COPY(storm_timer) COPY(irradiated_by)
	COPY(irradiate_player) COPY(parasites) COPY(cycle_counter) COPY(blind) COPY(maelstrom_timer) COPY(unknown_0x125)
	COPY(acid_spores) COPY(acid_time0) COPY(acid_time1) COPY(acid_time2) COPY(acid_time3) COPY(acid_time4)
	COPY(acid_time5) COPY(acid_time6) COPY(acid_time7) COPY(acid_time8)
#undef COPY
	kp_rebuild_status_overlays(g, dst);
	kp_recalculate_speed(g, dst);
}

/* AddUnitToLocalSelection 0x43c090: the unit takes the first free slot of the local selection. */
static void add_to_local_selection(KpGame *g, int64_t n)
{
	int64_t units[KP_SLOTS];
	int k;
	for (k = 0; k < KP_SLOTS; k++) {
		units[k] = g->local_selection[k];
		if (units[k] == 0) break;
	}
	if (k == KP_SLOTS) return;
	units[k] = n;
	kp_select_locally(g, units, k + 1);
}

/* 0x43b5e0: the second unit of a two-unit egg, at the second landing-dust point of the egg's main image (the first
 * one takes the first), facing as its type does, with the first's statuses; it joins a local selection that holds the
 * first. */
static int64_t hatch_twin(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	KpImages *w = g->w;
	int32_t s = kp_unit_sprite(g, n), main = kp_check_image(w, w->spr.main[s]), dx0, dy0, dx1, dy1;
	int64_t x = sprite_x(g, n), y = sprite_y(g, n), x0, y0, x1, y1, twin;
	kp_images_lo_offset(w, main, LO_DUST, 0, &dx0, &dy0);
	kp_images_lo_offset(w, main, LO_DUST, 1, &dx1, &dy1);
	x0 = kp_s16v(x + dx0);
	y0 = kp_s16v(y + dy0);
	x1 = kp_s16v(x + dx1);
	y1 = kp_s16v(y + dy1);
	kp_clamp_to_map(g, u->type[n], &x0, &y0);
	kp_clamp_to_map(g, u->type[n], &x1, &y1);
	kp_unit_relocate(g, n, x0, y0);
	twin = kp_unit_create(g, u->type[n], x1, y1, u->owner[n]);
	if (twin == 0) {
		kp_report_creation_error(g, u->owner[n]); /* 0x425020 */
		return 0;
	}
	kp_flingy_set_heading(g, twin, (u->type[n] == ZERGLING ? ZERGLING_FACING : u->type[n] == SCOURGE ? SCOURGE_FACING : 0) * 8);
	kp_unit_complete_in_place(g, twin);
	copy_status(g, twin, n);
	if (!w->replay && kp_band(w->spr.flags[s], SPRITE_SELECTED) != 0) add_to_local_selection(g, twin);
	return twin;
}

/* The AI's new unit (0x48d320): a computer's only. */
static void assign_ai(KpGame *g, int64_t n)
{
	/* TODO: AssignNewUnitAi 0x48d320 once the AIScript VM of plan phase 7 gives the computer its towns */
	if (kp_controller(g, g->u.owner[n]) == CONTROLLER_COMPUTER) kp_game_fail(g, "larva: a computer's new unit joins its AI (0x48d320)");
}

/* OrderZergUnitMorph 0x43bac0 (order 42): a Larva, Hydralisk or Mutalisk with the supply becomes its egg for the
 * queued type's build time, then hatches it (with its construction image and a birth, or at once). */
void kp_order_zerg_unit_morph(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t t = u->type[n], target = kp_band(*kp_queue_entry(g, n, kp_mod(u->queue_slot[n], 5)), 0xffff), egg;
	switch (u->order_state[n]) {
	case 0:
		if (t != LARVA && t != HYDRALISK && t != MUTALISK) return;
		if (!kp_check_supply(g, u->owner[n], target, 1)) {
			kp_cancel_all_queued(g, n);
			kp_issue_order(g, n, KP_DAT(g, units, return_to_idle, t));
			return;
		}
		kp_account_completed(g, n, -1, 0); /* 0x414580 */
		u->status[n] = kp_band(u->status[n], kp_bnot(STATUS_COMPLETED));
		egg = t == LARVA ? EGG : t == HYDRALISK ? LURKER_EGG : COCOON;
		kp_change_unit_type(g, n, egg);
		kp_set_button_set(g, n, u->type[n]);
		u->build_time[n] = KP_DAT(g, units, build_time, target);
		u->order_state[n] = 1;
		return;
	case 1:
		if (kp_tick_build_timer(g, n)) {
			kp_sprite_play(g->w, kp_unit_sprite(g, n), ANIM_SPECIAL1, 1);
			u->order_state[n] = 2;
		}
		return;
	case 2:
		if (kp_band(u->order_signal[n], SIGNAL_BIRTH) == 0) return;
		u->order_signal[n] = kp_band(u->order_signal[n], kp_bnot(SIGNAL_BIRTH));
		kp_change_unit_type(g, n, target);
		if (u->type[n] == DRONE || u->type[n] == OVERLORD) assign_ai(g, n);
		kp_ready_voice(g, n);
		*kp_queue_entry(g, n, kp_mod(u->queue_slot[n], 5)) = NO_TYPE;
		if (KP_DAT(g, units, construction_animation, u->type[n]) == 0) {
			kp_unit_complete_in_place(g, n);
			if (t == EGG) kp_send_to_rally(g, n, u->connected[n]);
		} else {
			kp_replace_unit_images(g, n, 1);
			kp_sprite_play(g->w, kp_unit_sprite(g, n), ANIM_SPECIAL1, 1);
			kp_issue_order(g, n, ORDER_ZERG_BIRTH);
		}
		return;
	}
}

/* OrderZergBirth 0x43bd50 (order 41): on the birth signal the unit (and the twin of a two-unit egg) completes, drops
 * from its egg (up 7 pixels for a hovering type, 42 for a flyer, unless twins or out of a Cocoon) and goes to the
 * parent's rally point (not out of a Cocoon or Lurker Egg). */
void kp_order_zerg_birth(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t t, twin = 0, x, y, nx, ny;
	int drop, pair;
	if (kp_band(u->order_signal[n], SIGNAL_BIRTH) == 0) return;
	u->order_signal[n] = kp_band(u->order_signal[n], kp_bnot(SIGNAL_BIRTH));
	t = u->type[n];
	pair = kp_band(KP_DAT(g, units, special_ability_flags, t), DAT_TWO_IN_EGG) != 0;
	drop = !pair && u->previous_type[n] != COCOON;
	if (pair) {
		twin = hatch_twin(g, n);
		if (twin != 0) assign_ai(g, twin);
	}
	if (t != DRONE && t != OVERLORD) assign_ai(g, n);
	kp_replace_unit_images(g, n, 0);
	kp_unit_complete_in_place(g, n);
	x = sprite_x(g, n);
	y = sprite_y(g, n);
	nx = x;
	ny = y;
	if (drop) {
		if (KP_DAT(g, units, unknown_flags, t) == HOVER_FLAGS) ny = y - HOVER_DROP;
		else if (kp_band(u->status[n], STATUS_AIR) != 0) ny = y - AIR_DROP;
	}
	ny = kp_s16v(ny);
	kp_clamp_to_map(g, t, &nx, &ny);
	if (nx != x || ny != y) kp_unit_relocate(g, n, nx, ny);
	if (u->previous_type[n] != COCOON && u->previous_type[n] != LURKER_EGG) {
		kp_send_to_rally(g, n, u->connected[n]);
		if (twin != 0) kp_send_to_rally(g, twin, u->connected[n]);
	}
	kp_order_unlock_done(g, n);
}
