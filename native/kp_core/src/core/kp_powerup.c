/* Powerups a worker carries (kp_game.h), ported from the atlas (P07, P09, P11; docs/research/native-powerup-lifecycle.md):
 * the orders PowerupIdle 97 (0x427290) and PowerupReturn 17 (0x427210), the picker search FindWorkerToPickUpPowerup
 * 0x431c20 with its callback 0x431980, the pickup WorkerPickUpPowerup 0x4270f0, the drop WorkerDropPowerup 0x4272c0
 * and DestroyWorkerCargo 0x4271d0.
 *
 * A worker's +0xc0 is the powerup it carries and its resource_type (+0xe0) gains 0x4; the powerup's +0xd0/+0xd2 is its
 * home (the place it was created at) and +0xd4 its carrier. */
#include "kp_game_internal.h"

#define DAT_WORKER 0x8
#define STATUS_COMPLETED 0x1
#define STATUS_BURROWED 0x10
#define STATUS_HALLUCINATION 0x40000000
#define CARRY_RESOURCE 0x3
#define CARRY_POWERUP 0x4
#define SPRITE_HIDDEN 0x20
#define TILE_ANY 0xffffffffLL
#define TILE_OCCUPIED 0x08000000
#define URAJ_IMAGE 958
#define KHALIS_IMAGE 959
#define POWERUP_IMAGE_BASE 177 /* the carried image of the other powerups: their type + 177 */
#define PLAYER_COLOR_METHOD 14
#define PICK_UP_RADIUS 24
#define RETURN_TIME 337
#define W_CARRIED 0     /* the worker's union word +0xc0 */
#define W_CARRIER 5     /* the powerup's union word +0xd4 */
#define U_HOME 0xd0

static int64_t sprite_x(KpGame *g, int64_t n) { return kp_s16v(g->w->spr.x[kp_unit_sprite(g, n)]); }
static int64_t sprite_y(KpGame *g, int64_t n) { return kp_s16v(g->w->spr.y[kp_unit_sprite(g, n)]); }

/* PowerupPickerCallback 0x431980: a completed worker, neither burrowed nor a hallucination, with no powerup, that may
 * take powerup p (its own player's Flag aside). */
static int picker(KpGame *g, int64_t c, int64_t p)
{
	KpUnits *u = &g->u;
	return kp_band(KP_DAT(g, units, special_ability_flags, u->type[c]), DAT_WORKER) != 0
		&& kp_band(u->status[c], STATUS_COMPLETED) != 0 && kp_band(u->status[c], STATUS_BURROWED | STATUS_HALLUCINATION) == 0
		&& u->union_0xc0[c] == 0 && !(u->type[p] == KP_TYPE_FLAG && u->owner[c] == u->owner[p]);
}

/* FindWorkerToPickUpPowerup 0x431c20: the first picker within 24 pixels in the spatial order (0x442b20). */
static int64_t find_picker(KpGame *g, int64_t p)
{
	int64_t x = sprite_x(g, p), y = sprite_y(g, p), *found, c = 0;
	int count, k;
	found = kp_query_rect(g, kp_s16v(x - PICK_UP_RADIUS), kp_s16v(y - PICK_UP_RADIUS), kp_s16v(x + PICK_UP_RADIUS),
		kp_s16v(y + PICK_UP_RADIUS), &count);
	for (k = 0; k < count && c == 0; k++) {
		if (picker(g, found[k], p)) c = found[k];
	}
	kp_query_release(g, found);
	return c;
}

/* WorkerPickUpPowerup 0x4270f0: the worker drops its minerals or gas and carries powerup p. */
static void pick_up(KpGame *g, int64_t w, int64_t p)
{
	KpUnits *u = &g->u;
	KpImages *im = g->w;
	int64_t t = u->type[p], image;
	int32_t i;
	if (u->resource_type[w] != 0) {
		if (u->union_0xc0[w] != 0) return;
		kp_remove_cargo_images(g, w);
		u->resource_type[w] = 0;
	}
	/* TODO: ShowFlagTakenMessage 0x421760 when Capture the Flag maps come into scope (outside the V1 profile) */
	if (t == KP_TYPE_FLAG) kp_game_fail(g, "powerup: a Flag is taken (0x421760)");
	image = t == KP_TYPE_URAJ_CRYSTAL ? URAJ_IMAGE : t == KP_TYPE_KHALIS_CRYSTAL ? KHALIS_IMAGE : t + POWERUP_IMAGE_BASE;
	kp_unit_hide(g, p); /* 0x422ba0 */
	kp_issue_order(g, p, KP_ORD_NOTHING);
	kp_set_union_unit(g, p, W_CARRIER, w);
	i = kp_sprite_image_above_main(im, kp_unit_sprite(g, w), (int32_t)image, 0, 0, 0);
	if (i != KP_NONE) {
		if (im->img.method[i] == PLAYER_COLOR_METHOD) im->img.param[i] = (int32_t)kp_band(u->owner[p], 0xff);
		kp_image_set_follow(im, i, 1);
	}
	u->resource_type[w] = kp_bor(u->resource_type[w], CARRY_POWERUP);
	kp_set_union_unit(g, w, W_CARRIED, p);
}

/* The powerup shows and a worker next to it picks it up (the start of 0x427210 and 0x427290). */
static void offer(KpGame *g, int64_t p)
{
	int64_t w;
	if (kp_band(g->w->spr.flags[kp_unit_sprite(g, p)], SPRITE_HIDDEN) != 0) kp_unit_show(g, p);
	w = find_picker(g, p);
	if (w != 0) pick_up(g, w, p);
}

/* OrderPowerupIdle 0x427290 (order 97). */
void kp_order_powerup_idle(KpGame *g, int64_t p) { offer(g, p); }

/* OrderPowerupReturn 0x427210 (order 17): a Flag dropped in the field goes home after 337 frames. */
void kp_order_powerup_return(KpGame *g, int64_t p)
{
	KpUnits *u = &g->u;
	int64_t home;
	offer(g, p);
	if (u->order_state[p] == 0) {
		u->order_state[p] = 1;
		u->build_time[p] = RETURN_TIME;
	} else if (u->order_state[p] == 1) {
		if (u->build_time[p] != 0) {
			u->build_time[p]--;
			return;
		}
		/* TODO: ShowFlagReturnedMessage 0x4216e0 when Capture the Flag maps come into scope (outside the V1 profile) */
		kp_game_fail(g, "powerup: a Flag returns home (0x4216e0)");
		home = kp_union_get(g, p, U_HOME, 4);
		kp_unit_relocate(g, p, kp_s16v(home), kp_s16v(kp_shrv(home, 16)));
		kp_issue_order(g, p, KP_ORD_POWERUP_IDLE);
	}
}

/* WorkerDropPowerup 0x4272c0: the powerup goes down where the worker stands (its home when the tile is occupied or the
 * powerup does not fit there), at the nearest place it fits within the whole-map rectangle, which this clamps in
 * place. */
void kp_drop_powerup(KpGame *g, int64_t w)
{
	KpUnits *u = &g->u;
	int64_t p, x, y, px, py, home;
	int field = 1;
	if (u->resource_type[w] == 0 || kp_band(u->resource_type[w], CARRY_RESOURCE) != 0) return;
	p = u->union_0xc0[w];
	kp_remove_cargo_images(g, w);
	x = sprite_x(g, w);
	y = sprite_y(g, w);
	if (!kp_tile_has_flags(g, x, y, TILE_ANY, TILE_OCCUPIED) || !kp_unit_type_fits(g, u->type[p], x, y)) {
		home = kp_union_get(g, p, U_HOME, 4);
		x = kp_s16v(home);
		y = kp_s16v(kp_shrv(home, 16));
		field = 0;
	}
	px = x;
	py = y;
	kp_find_unit_placement(g, p, x, y, g->map_rect, 0, &px, &py);
	kp_unit_relocate(g, p, px, py);
	kp_issue_order(g, p, u->type[p] == KP_TYPE_FLAG && field ? KP_ORD_UNUSED_POWERUP : KP_ORD_POWERUP_IDLE);
	kp_set_union_unit(g, p, W_CARRIER, 0);
	u->resource_type[w] = 0;
	kp_set_union_unit(g, w, W_CARRIED, 0);
}

/* DestroyWorkerCargo 0x4271d0: what the worker carries goes, a powerup dying with it (KillUnitFlagged 0x425700). */
void kp_destroy_worker_cargo(KpGame *g, int64_t w)
{
	KpUnits *u = &g->u;
	int64_t p = u->union_0xc0[w];
	if (u->resource_type[w] != 0) kp_remove_cargo_images(g, w);
	if (p != 0) kp_kill_flagged(g, p);
	u->resource_type[w] = 0;
	kp_set_union_unit(g, w, W_CARRIED, 0);
}
