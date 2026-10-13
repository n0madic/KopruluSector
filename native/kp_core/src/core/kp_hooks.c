/* The hooks of the images world into a native game (kp_game.h; game/units/hooks.lua image_hooks, the
 * specification): the world calls them with the unit first. */
#include "kp_game_internal.h"

static int64_t unit_arg(KpGame *g, const int64_t *args, int argc)
{
	if (argc < 1) kp_game_fail(g, "hooks: no unit");
	return kp_game_unit(g, args[0]);
}

static int64_t arg(KpGame *g, const int64_t *args, int argc, int k)
{
	if (argc <= k) kp_game_fail(g, "hooks: argument %d missing", k + 1);
	return args[k];
}

int kp_game_hook(KpImages *w, void *ctx, KpHook hook, int op, const int64_t *args, int argc, int64_t *out)
{
	KpGame *g = (KpGame *)ctx;
	KpUnits *u = &g->u;
	int64_t n;
	switch (hook) {
	case KP_HOOK_UNIT_STATUS:
		out[0] = u->status[unit_arg(g, args, argc)];
		return 1;
	case KP_HOOK_UNIT_OWNER:
		out[0] = u->owner[unit_arg(g, args, argc)];
		return 1;
	case KP_HOOK_UNIT_TYPE:
		out[0] = u->type[unit_arg(g, args, argc)];
		return 1;
	case KP_HOOK_UNIT_SPRITE:
		out[0] = u->sprite[unit_arg(g, args, argc)];
		return 1;
	case KP_HOOK_UNIT_HEADING:
		out[0] = u->heading[unit_arg(g, args, argc)];
		return 1;
	case KP_HOOK_UNIT_TARGET:
		out[0] = u->order_unit[unit_arg(g, args, argc)];
		return out[0] != 0;
	case KP_HOOK_SET_CLOAK_STATE:
		kp_unit_set_cloak_state(g, unit_arg(g, args, argc), arg(g, args, argc, 1));
		return 1;
	case KP_HOOK_MOVE_SPEED:
		out[0] = kp_top_speed_modified(g, unit_arg(g, args, argc), arg(g, args, argc, 1));
		return 1;
	case KP_HOOK_SET_MOVE_SPEED:
		kp_flingy_set_speed(g, unit_arg(g, args, argc), arg(g, args, argc, 1));
		return 1;
	case KP_HOOK_TURN_CCW:
		kp_flingy_turn(g, unit_arg(g, args, argc), -arg(g, args, argc, 1));
		return 1;
	case KP_HOOK_TURN_CW:
		kp_flingy_turn(g, unit_arg(g, args, argc), arg(g, args, argc, 1));
		return 1;
	case KP_HOOK_SET_SPAWN_FRAME:
		kp_flingy_set_spawn_frame(g, unit_arg(g, args, argc), arg(g, args, argc, 1));
		return 1;
	case KP_HOOK_SET_FLINGY_DIRECTION:
		kp_flingy_set_heading(g, unit_arg(g, args, argc), arg(g, args, argc, 1));
		return 1;
	case KP_HOOK_SET_TOP_SPEED:
		n = unit_arg(g, args, argc);
		u->top_speed[n] = arg(g, args, argc, 1);
		return 1;
	case KP_HOOK_ORDER_SIGNAL:
		n = unit_arg(g, args, argc);
		u->order_signal[n] = kp_mod(kp_band(kp_bor(u->order_signal[n], arg(g, args, argc, 1)), kp_bnot(arg(g, args, argc, 2))), 256);
		return 1;
	case KP_HOOK_CLEAR_REPEAT_ATTACK:
		n = unit_arg(g, args, argc);
		u->movement_flags[n] = kp_band(u->movement_flags[n], 0xf7);
		return 1;
	case KP_HOOK_NOBRK_START: {
		int32_t s;
		n = unit_arg(g, args, argc);
		u->status[n] = kp_bor(u->status[n], 0x2000);
		s = kp_unit_sprite(g, n);
		w->spr.flags[s] = (int32_t)kp_bor(w->spr.flags[s], 0x80);
		return 1;
	}
	case KP_HOOK_NOBRK_END:
		kp_nobrk_end(g, unit_arg(g, args, argc));
		return 1;
	case KP_HOOK_IGNORE_REST_IDLE:
		kp_reset_to_idle_animation(g, unit_arg(g, args, argc));
		return 1;
	case KP_HOOK_HIDE_ORDER_MARKER: /* 0x417ef0 */
		kp_scalar_set(g, KP_ORDER_MARKER, 0);
		return 1;
	case KP_HOOK_UNIT_FITS:
		out[0] = kp_unit_type_fits(g, arg(g, args, argc, 0), arg(g, args, argc, 1), arg(g, args, argc, 2));
		return 1;
	case KP_HOOK_SELECTION_UNIT: {
		/* The unit of a selected sprite's health bar: its local selection slot (0x40f6e0). */
		int64_t s = arg(g, args, argc, 0), slot;
		if (s < 0 || s >= KP_SPRITES) kp_game_fail(g, "hooks: sprite %lld", (long long)s);
		slot = w->spr.selection[s];
		out[0] = slot >= 0 && slot < KP_SLOTS ? g->local_selection[slot] : KP_NIL;
		if (out[0] == KP_NIL) kp_game_fail(g, "images: hook selection_unit returned a nil, not an integer");
		return 1;
	}
	case KP_HOOK_SOUND:
		kp_event(g, KP_EVENT_SOUND, arg(g, args, argc, 0), arg(g, args, argc, 1), arg(g, args, argc, 2), NULL);
		return 1;
	case KP_HOOK_PLAY_PROBE:
		return 0;
	default:
		break;
	}
	if (op >= 0) kp_game_fail(g, "iscript: opcode 0x%02x needs hook %s", op, kp_hook_name(hook));
	kp_game_fail(g, "images: needs hook %s", kp_hook_name(hook));
}
