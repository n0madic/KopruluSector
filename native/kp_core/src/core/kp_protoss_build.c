/* The Protoss construction (kp_game.h), ported from the atlas (P04, P10; docs/research/native-protoss-construction.md,
 * native-assimilator.md): a Probe's PlaceProtossBuilding 31 (0x42ee00) and CreateProtossBuilding 32 (0x42f230), which
 * place a warp anchor that builds itself (an Assimilator over its Geyser, 0x41b3f0), the building's IncompleteWarping
 * 48 (0x42f020) with the warp texture (StartWarpTextureImage 0x411840) and a unit's WarpIn 174 (0x42f1b0). */
#include "kp_game_internal.h"

#define NO_TYPE 228
#define ASSIMILATOR 157
#define DAT_PERMANENT_CLOAK 0x400000
#define STATUS_DISABLED 0x400
#define STATUS_IGNORE_TILE_COLLISION 0x800000
#define FLINGY_MOVING 0x2
#define SIGNAL_WARP 0x1
#define ORDER_CREATE_PROTOSS_BUILDING_STATE 2
#define ORDER_PLACE_PROTOSS_BUILDING 31
#define ORDER_INCOMPLETE_WARPING 48
#define ORDER_WAIT_FOR_GAS 82
#define ANIM_WALKING_TO_IDLE 12
#define ANIM_SPECIAL1 13
#define ANIM_WARP_IN 21
#define ANIM_DISABLE 24
#define IMAGE_WARP_ANCHOR 211
#define IMAGE_ASSIMILATOR_WARP 344
#define SOUND_WARP_PLACED 0x210
#define SOUND_WARP_STARTED 0x211
#define SOUND_CLOAK 0x111
#define PLACE_RANGE 70
#define ASSIMILATOR_RANGE 88


/* The sprite's ordinary main image again (sprites.dat's), facing heading (ReplaceSpriteMainImage 0x415590). */
static void restore_main_image(KpGame *g, int64_t n, int64_t heading)
{
	int32_t s = kp_unit_sprite(g, n);
	kp_sprite_replace_main(g, s, KP_DAT(g, sprites, image, g->w->spr.id[s]), heading);
}

/* Within the reach of the site: 88 for an Assimilator, else 70 (0x417aa0). */
static int near_site(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	return kp_within_distance(g, n, kp_queued_type(g, n) == ASSIMILATOR ? ASSIMILATOR_RANGE : PLACE_RANGE, kp_s16v(u->order_x[n]),
		kp_s16v(u->order_y[n]));
}

/* The warp anchor of the queued building at the site, paid for (0x41bcb0); its Probe goes on with its queue. */
static void place(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t t, b;
	int32_t s;
	if (!kp_pay_for_building(g, n)) {
		kp_order_done(g, n);
		return;
	}
	t = kp_queued_type(g, n);
	b = t == ASSIMILATOR ? kp_convert_geyser(g, n, ASSIMILATOR)
		: kp_unit_create(g, t, kp_s16v(u->order_x[n]), kp_s16v(u->order_y[n]), u->owner[n]);
	*kp_queue_head(g, n) = NO_TYPE;
	if (b == 0) {
		kp_report_creation_error(g, u->owner[n]); /* 0x425020 */
		kp_order_done(g, n);
		return;
	}
	s = kp_unit_sprite(g, b);
	kp_sprite_replace_main(g, s, IMAGE_WARP_ANCHOR, 0);
	if (t == ASSIMILATOR) kp_sprite_image_below_main(g->w, s, IMAGE_ASSIMILATOR_WARP, 0, 0, 0);
	u->order_unit[n] = b;
	kp_builder_town(g, n); /* 0x486200 */
	kp_issue_order(g, b, ORDER_INCOMPLETE_WARPING);
	u->order_state[n] = 3;
	kp_play_unit_sound(g, SOUND_WARP_PLACED, n);
}

/* Order_PlaceProtossBuilding 0x42ee00 (order 31): the Probe walks within reach of the site, stops, places the
 * building, and then gathers from its new Assimilator when nothing else is queued. */
void kp_order_place_protoss_building(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t b;
	if (u->ai[n] != 0) u->status[n] = kp_bor(u->status[n], STATUS_IGNORE_TILE_COLLISION);
	switch (u->order_state[n]) {
	case 0:
		if (kp_request_move_to_point(g, n, kp_s16v(u->order_x[n]), kp_s16v(u->order_y[n]))) u->order_state[n] = 1;
		return;
	case 1:
		if (near_site(g, n)) {
			kp_stop_and_face(g, n); /* 0x43a4e0 */
			u->order_state[n] = 2;
			return;
		}
		break;
	case 2:
		if (kp_band(u->movement_flags[n], FLINGY_MOVING) != 0) return;
		if (near_site(g, n)) {
			place(g, n);
			return;
		}
		break;
	case 3:
		b = u->order_unit[n];
		if (b != 0 && u->type[b] == ASSIMILATOR && u->queue_head[n] == KP_NONE) {
			kp_issue_order_target(g, n, ORDER_WAIT_FOR_GAS, b);
		} else {
			kp_order_done(g, n);
		}
		kp_sprite_play(g->w, kp_unit_sprite(g, n), ANIM_WALKING_TO_IDLE, 1);
		return;
	default:
		return;
	}
	/* out of reach: at its move target the Probe walks to the site again */
	if (kp_move_arrival(g, n) != 0) kp_request_move_to_point(g, n, kp_s16v(u->order_x[n]), kp_s16v(u->order_y[n]));
}

/* Order_CreateProtossBuilding 0x42f230 (order 32): PlaceProtossBuilding in its placing state. */
void kp_order_create_protoss_building(KpGame *g, int64_t n)
{
	g->u.order[n] = ORDER_PLACE_PROTOSS_BUILDING;
	g->u.order_state[n] = ORDER_CREATE_PROTOSS_BUILDING_STATE;
	kp_order_place_protoss_building(g, n);
}

/* The warp signal of the building's script, taken. */
static int take_signal(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	if (kp_band(u->order_signal[n], SIGNAL_WARP) == 0) return 0;
	u->order_signal[n] = kp_band(u->order_signal[n], kp_bnot(SIGNAL_WARP));
	return 1;
}

/* Order_IncompleteWarping 0x42f020 (order 48): the anchor waits out the build time, then on three signals of its
 * script turns into the building under the warp texture, warps in and completes; it gains its hit points and shields
 * every step until then (and once more as it completes). */
void kp_order_incomplete_warping(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t gain, max;
	int32_t s = kp_unit_sprite(g, n);
	switch (u->order_state[n]) {
	case 0:
		if (kp_band(u->build_time[n], 0xffff) == 0) {
			kp_sprite_play(g->w, s, ANIM_SPECIAL1, 1);
			kp_play_unit_sound(g, SOUND_WARP_STARTED, n);
			u->order_state[n] = 1;
		}
		break;
	case 1:
		if (!take_signal(g, n)) return;
		restore_main_image(g, n, 0);
		kp_image_start_warp_texture(g->w, kp_check_image(g->w, g->w->spr.main[kp_unit_sprite(g, n)]));
		u->order_state[n] = 2;
		return;
	case 2:
		if (!take_signal(g, n)) return;
		restore_main_image(g, n, 0);
		kp_sprite_play(g->w, kp_unit_sprite(g, n), ANIM_WARP_IN, 1);
		u->order_state[n] = 3;
		return;
	case 3:
		if (!take_signal(g, n)) return;
		kp_unit_complete_in_place(g, n); /* 0x424da0 */
		kp_update_overlap_flags(g, n);   /* 0x42e600 */
		if (kp_band(u->status[n], STATUS_DISABLED) != 0) kp_sprite_play(g->w, kp_unit_sprite(g, n), ANIM_DISABLE, 1);
		break;
	}
	kp_tick_build_timer(g, n); /* 0x41a540 */
	kp_set_unit_hp(g, n, u->hp[n] + kp_build_hp_gain(g, n)); /* AddUnitHp 0x41b900 */
	gain = kp_build_shield_gain(g, n);
	max = kp_band(KP_DAT(g, units, shield_amount, u->type[n]), 0xffff) * 256;
	u->shields[n] = kp_s32v(gain + u->shields[n]) <= kp_s32v(max) ? kp_u32v(gain + u->shields[n]) : max;
}

/* Order_WarpIn 0x42f1b0 (order 174): on its script's signals the unit takes its ordinary image back and warps in,
 * then cloaks when its type always is (0x41d200) and goes on. */
void kp_order_warp_in(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	if (u->order_state[n] == 0) {
		if (!take_signal(g, n)) return;
		restore_main_image(g, n, u->heading[n]);
		kp_sprite_play(g->w, kp_unit_sprite(g, n), ANIM_WARP_IN, 1);
		u->order_state[n] = 1;
	} else if (u->order_state[n] == 1 && take_signal(g, n)) {
		if (kp_band(KP_DAT(g, units, special_ability_flags, u->type[n]), DAT_PERMANENT_CLOAK) != 0) {
			kp_begin_cloak(g, n, SOUND_CLOAK);
		}
		kp_order_unlock_done(g, n);
	}
}
