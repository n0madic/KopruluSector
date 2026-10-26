/* IScript VM: RunIScript 0x410480 (kp_images.h). */
#include "kp_images_internal.h"

#define RNG_WAITRAND 3
#define RNG_PLAYSNDRAND 4
#define RNG_PLAYSNDBTWN 5
#define RNG_TURNRAND 6
#define RNG_RANDCONDJMP 7
#define IGNORE_REST_WAIT 10
#define GOLIATH_SPRITE 505
#define GAS_OVERLAY_FULL 430
#define GAS_OVERLAY_EMPTY 435
#define SPELL_WEAPON_LIMIT 130
#define WEAPON_GROUND 1
#define WEAPON_AIR 2

/* Floored modulo for a positive m. */
static int64_t lmod(int64_t a, int64_t m)
{
	int64_t r = a % m;
	return r < 0 ? r + m : r;
}

static uint32_t rng_raw(KpImages *w, uint8_t source)
{
	if (!w->rng) kp_images_fail(w, "iscript: the world has no game generator");
	return kp_rng_game_raw(w->rng, source);
}

static void context_error(KpImages *w, int op, const char *what)
{
	kp_images_fail(w, "iscript: opcode 0x%02x needs a %s context", op, what);
}

static int64_t unit_of(KpImages *w, int op)
{
	if (!w->has_unit) context_error(w, op, "unit");
	return w->unit;
}

static int64_t hook1(KpImages *w, KpHook h, int op, int64_t a) { return kp_images_hook(w, h, op, &a, 1); }

static int64_t hook2(KpImages *w, KpHook h, int op, int64_t a, int64_t b)
{
	int64_t args[2];
	args[0] = a;
	args[1] = b;
	return kp_images_hook(w, h, op, args, 2);
}

static int64_t hook3(KpImages *w, KpHook h, int op, int64_t a, int64_t b, int64_t c)
{
	int64_t args[3];
	args[0] = a;
	args[1] = b;
	args[2] = c;
	return kp_images_hook(w, h, op, args, 3);
}

/* Whether unit u has a target (the unit_target hook returns one). */
static int has_target(KpImages *w, int op, int64_t u)
{
	int64_t t;
	return kp_images_hook_opt(w, KP_HOOK_UNIT_TARGET, op, &u, 1, &t);
}

void kp_images_flingy_signal(KpImages *w, int32_t set_mask, int32_t clear_mask, int op)
{
	if (!w->has_flingy) context_error(w, op < 0 ? 0 : op, "flingy");
	hook3(w, KP_HOOK_ORDER_SIGNAL, op, w->flingy, set_mask, clear_mask);
}

void kp_images_unit_set_cloak_state(KpImages *w, int32_t n)
{
	int64_t u = unit_of(w, 0);
	hook2(w, KP_HOOK_SET_CLOAK_STATE, -1, u, n);
}

void kp_image_sound(KpImages *w, int32_t i, int32_t id)
{
	int32_t s;
	int64_t args[3];
	if (!w->has_sound) return;
	s = kp_check_sprite(w, w->img.sprite[i]);
	args[0] = id & 0xffff;
	args[1] = w->spr.x[s];
	args[2] = w->spr.y[s];
	w->hook(w, w->ctx, KP_HOOK_SOUND, -1, args, 3, NULL);
}

/* Stores the state at a return point (wait < 0: the wait is kept). */
static void store(KpImages *w, int32_t i, KpScriptState *state, int32_t pc, int32_t wait)
{
	if (state) {
		state->pc = pc;
		if (wait >= 0) state->wait = wait;
	} else {
		w->img.pc[i] = pc;
		if (wait >= 0) w->img.wait[i] = wait;
	}
}

static void follow_main(KpImages *w, int32_t i, int32_t main)
{
	KpImageRecords *img = &w->img;
	img->dir[i] = img->dir[main];
	img->flags[i] = kp_flag(img->flags[i], KP_F_MIRROR, (uint32_t)(img->flags[main] & KP_F_MIRROR));
	kp_image_set_method(w, i, img->method[i]);
	kp_image_sync_frame(w, i);
}

static void spawn_turned(KpImages *w, int32_t i, int32_t id, int32_t dx, int32_t dy, int32_t elevation)
{
	int32_t t = kp_image_spawn_sprite(w, i, id, dx, dy, elevation);
	if (t != KP_NONE) {
		int32_t d = w->img.dir[i];
		if (w->img.flags[i] & KP_F_MIRROR) d = 32 - d;
		kp_sprite_set_direction(w, w->thg.sprite[t], d);
	}
}

static int cloaked_skip(KpImages *w, int32_t sprite_id)
{
	if (!w->has_unit) return 0;
	if (!(hook1(w, KP_HOOK_UNIT_STATUS, 0x13, w->unit) & KP_STATUS_CLOAKED)) return 0;
	return kp_images_dat(w, w->col.draw_if_cloaked, (int32_t)kp_images_dat(w, w->col.sprite_image, sprite_id)) == 0;
}

static int32_t play_sound_list(KpImages *w, int32_t i, int32_t at, int noop)
{
	int32_t n = (int32_t)kp_code_u8(w, at);
	if (!noop) {
		uint32_t r;
		if (n == 0) kp_images_fail(w, "iscript: playsndrand with no sounds");
		r = rng_raw(w, RNG_PLAYSNDRAND) % (uint32_t)n;
		kp_image_sound(w, i, (int32_t)kp_code_u16(w, at + 1 + 2 * (int64_t)r));
	}
	return at + 1 + 2 * n;
}

static int32_t sprite_of(KpImages *w, int32_t i) { return kp_check_sprite(w, w->img.sprite[i]); }

#define U8(k) ((int32_t)kp_code_u8(w, (int64_t)at + (k)))
#define U16(k) ((int32_t)kp_code_u16(w, (int64_t)at + (k)))
#define S8(k) kp_s8(kp_code_u8(w, (int64_t)at + (k)))

int kp_images_run_iscript(KpImages *w, int32_t i, KpScriptState *state, int noop, int out, int64_t *result)
{
	KpImageRecords *img = &w->img;
	int32_t wait = state ? state->wait : img->wait[i];
	int32_t pc, budget = w->step_limit;
	int has_result = 0;
	if (wait != 0) {
		if (state) state->wait = wait - 1;
		else img->wait[i] = wait - 1;
		return 0;
	}
	pc = state ? state->pc : img->pc[i];
	for (;;) {
		int32_t op, at;
		if (w->step_limit >= 0) {
			budget--;
			if (budget < 0) {
				kp_images_fail(w, "iscript: no wait after %d opcodes (pc 0x%x)", (int)w->step_limit, (unsigned)pc);
			}
		}
		op = (int32_t)kp_code_u8(w, pc);
		at = pc + 1;
		switch (op) {
		case 0x00: /* playfram */
			if (!noop) kp_image_set_frameset(w, i, U16(0));
			pc = at + 2;
			break;
		case 0x01: /* playframtile */
			if (!noop) {
				int32_t f = (U16(0) + w->tileset) & 0xffff;
				if (f < (kp_images_grp_frames(w, i) & 0x7fff)) kp_image_set_frameset(w, i, f);
			}
			pc = at + 2;
			break;
		case 0x02: /* sethorpos */
			if (!noop) {
				int32_t x = S8(0);
				if (img->xoff[i] != x) img->xoff[i] = x;
			}
			pc = at + 1;
			break;
		case 0x03: /* setvertpos */
			if (!noop) {
				if (!w->has_unit || lmod(hook1(w, KP_HOOK_UNIT_STATUS, op, w->unit), 1024) < 256) {
					int32_t y = S8(0);
					if (img->yoff[i] != y) img->yoff[i] = y;
				}
			}
			pc = at + 1;
			break;
		case 0x04: /* setpos */
			if (!noop) kp_image_set_offset(w, i, U8(0), U8(1));
			pc = at + 2;
			break;
		case 0x05: /* wait (also in no-op mode) */
			store(w, i, state, at + 1, (U8(0) - 1) & 0xff);
			goto done;
		case 0x06: /* waitrand */
			if (!noop) {
				int32_t a = U8(0), b = U8(1), d = b - a + 1;
				int32_t r;
				if (d == 0) kp_images_fail(w, "iscript: waitrand with an empty range");
				r = (int32_t)(rng_raw(w, RNG_WAITRAND) & 0xffu);
				store(w, i, state, at + 2, (int32_t)lmod(r % d - 1 + a, 256));
				goto done;
			}
			pc = at + 2;
			break;
		case 0x07: /* goto */
			pc = U16(0);
			break;
		case 0x08: /* imgol */
		case 0x09: /* imgul */
			if (!noop) kp_image_create_relative(w, i, U16(0), img->xoff[i] + U8(2), U8(3) + img->yoff[i], op == 0x08);
			pc = at + 4;
			break;
		case 0x0a: /* imgolorig */
		case 0x0b: /* switchul */
			if (!noop) {
				int32_t n = kp_image_create_relative(w, i, U16(0), 0, 0, op == 0x0a);
				if (n != KP_NONE) kp_image_set_follow(w, n, 1);
			}
			pc = at + 2;
			break;
		case 0x0d: /* imgoluselo */
		case 0x0e: /* imgululelo */
			if (!noop) {
				int32_t x, y;
				kp_images_lo_offset(w, i, U8(2), S8(3), &x, &y);
				kp_image_create_relative(w, i, U16(0), img->xoff[i] + x, img->yoff[i] + y, op == 0x0d);
			}
			pc = at + 4;
			break;
		case 0x0f: /* sprol */
			if (!noop) {
				int32_t id = U16(0);
				if (w->has_bullet && hook1(w, KP_HOOK_SPROL_GOLIATH, op, w->bullet)) id = GOLIATH_SPRITE;
				kp_image_spawn_sprite(w, i, id, S8(2), S8(3), w->spr.elevation[sprite_of(w, i)] + 1);
			}
			pc = at + 4;
			break;
		case 0x10: /* highsprol */
		case 0x11: /* lowsprul */
			if (!noop) {
				int32_t elevation = op == 0x10 ? w->spr.elevation[sprite_of(w, i)] - 1 : 1;
				kp_image_spawn_sprite(w, i, U16(0), S8(2), S8(3), elevation);
			}
			pc = at + 4;
			break;
		case 0x12: /* uflunstable */
			if (!noop) {
				int32_t s = sprite_of(w, i);
				hook3(w, KP_HOOK_CREATE_UNSTABLE_FLINGY, op, U16(0), kp_s16((uint32_t)w->spr.x[s]) + img->xoff[i],
					kp_s16((uint32_t)w->spr.y[s]) + img->yoff[i]);
			}
			pc = at + 2;
			break;
		case 0x13: /* spruluselo */
		case 0x14: /* sprul */
		{
			int32_t id = U16(0);
			if (!noop && !cloaked_skip(w, id)) {
				int32_t e = w->spr.elevation[sprite_of(w, i)];
				if (op == 0x14) e = (e - 1) & 0xff;
				spawn_turned(w, i, id, S8(2), S8(3), e);
			}
			pc = at + 4;
			break;
		}
		case 0x15: /* sproluselo */
			if (!noop) {
				int32_t x, y;
				kp_images_lo_offset(w, i, U8(2), 0, &x, &y);
				spawn_turned(w, i, U16(0), x, y, w->spr.elevation[sprite_of(w, i)] + 1);
			}
			pc = at + 3;
			break;
		case 0x16: /* end */
			if (!noop) {
				kp_image_release(w, i);
				store(w, i, state, at, -1);
				goto done;
			}
			pc = at;
			break;
		case 0x17: /* setflipstate */
			if (!noop) kp_image_set_mirror(w, i, U8(0));
			pc = at + 1;
			break;
		case 0x18: /* playsnd */
			if (!noop) kp_image_sound(w, i, U16(0));
			pc = at + 2;
			break;
		case 0x19: /* playsndrand */
			pc = play_sound_list(w, i, at, noop);
			break;
		case 0x1a: /* playsndbtwn */
			if (!noop) {
				int32_t a = U16(0), b = U16(2);
				uint32_t span = (uint32_t)(b - a + 1);
				if (span == 0) kp_images_fail(w, "iscript: playsndbtwn with an empty range");
				kp_image_sound(w, i, (int32_t)(rng_raw(w, RNG_PLAYSNDBTWN) % span + (uint32_t)a));
			}
			pc = at + 4;
			break;
		case 0x1b: /* domissiledmg */
		case 0x44: /* dogrddamage */
			if (!noop) {
				if (!w->has_bullet) context_error(w, op, "bullet");
				hook1(w, KP_HOOK_MISSILE_DAMAGE, op, w->bullet);
			}
			pc = at;
			break;
		case 0x1c: /* attackmelee */
			if (!noop) hook1(w, KP_HOOK_ATTACK_MELEE, op, unit_of(w, op));
			pc = play_sound_list(w, i, at, noop);
			break;
		case 0x1d: /* followmaingraphic */
			if (!noop) {
				int32_t main = w->spr.main[sprite_of(w, i)];
				if (main != KP_NONE
					&& (img->frame[i] != img->frame[main]
						|| ((img->flags[i] & KP_F_MIRROR) != 0) != ((img->flags[main] & KP_F_MIRROR) != 0))) {
					img->frameset[i] = img->frameset[main];
					follow_main(w, i, main);
				}
			}
			pc = at;
			break;
		case 0x1e: /* randcondjmp (consumes the generator in no-op mode too) */
		{
			uint32_t r = rng_raw(w, RNG_RANDCONDJMP) & 0xffu;
			pc = r <= (uint32_t)U8(0) ? U16(1) : at + 3;
			break;
		}
		case 0x1f: /* turnccwise */
		case 0x20: /* turncwise */
			if (!noop) hook2(w, op == 0x1f ? KP_HOOK_TURN_CCW : KP_HOOK_TURN_CW, op, unit_of(w, op), U8(0));
			pc = at + 1;
			break;
		case 0x21: /* turn1cwise */
			if (!noop) {
				int64_t u = unit_of(w, op);
				if (!has_target(w, op, u)) hook2(w, KP_HOOK_TURN_CW, op, u, 1);
			}
			pc = at;
			break;
		case 0x22: /* turnrand */
			if (!noop) {
				uint32_t r = rng_raw(w, RNG_TURNRAND);
				int64_t u = unit_of(w, op);
				hook2(w, r % 4 == 1 ? KP_HOOK_TURN_CCW : KP_HOOK_TURN_CW, op, u, U8(0));
			}
			pc = at + 1;
			break;
		case 0x23: /* setspawnframe */
			if (!noop) hook2(w, KP_HOOK_SET_SPAWN_FRAME, op, unit_of(w, op), U8(0));
			pc = at + 1;
			break;
		case 0x24: /* sigorder */
			if (!noop) kp_images_flingy_signal(w, U8(0), 0, op);
			pc = at + 1;
			break;
		case 0x25: /* attackwith */
			if (!noop) hook2(w, KP_HOOK_ATTACK_WITH, op, unit_of(w, op), U8(0));
			pc = at + 1;
			break;
		case 0x26: /* attack */
			if (!noop) {
				int64_t u = unit_of(w, op), target;
				int air = 0;
				if (kp_images_hook_opt(w, KP_HOOK_UNIT_TARGET, op, &u, 1, &target)) {
					air = ((uint64_t)hook1(w, KP_HOOK_UNIT_STATUS, op, target) / KP_STATUS_AIR & 1) != 0;
				}
				hook2(w, KP_HOOK_ATTACK_WITH, op, u, air ? WEAPON_AIR : WEAPON_GROUND);
			}
			pc = at;
			break;
		case 0x27: /* castspell */
			if (!noop) {
				int64_t u = unit_of(w, op);
				int64_t weapon = hook1(w, KP_HOOK_ORDER_WEAPON, op, u);
				if (weapon < SPELL_WEAPON_LIMIT && !hook1(w, KP_HOOK_SPELL_TARGET_INVALID, op, u)) {
					hook2(w, KP_HOOK_FIRE_WEAPON, op, u, weapon);
				}
			}
			pc = at;
			break;
		case 0x28: /* useweapon */
			if (!noop) hook2(w, KP_HOOK_USE_WEAPON, op, unit_of(w, op), U8(0));
			pc = at + 1;
			break;
		case 0x29: /* move (the output part runs in no-op mode too) */
		{
			int32_t v = U8(0) * 256;
			if (out) {
				int64_t speed = hook2(w, KP_HOOK_MOVE_SPEED, op, unit_of(w, op), v);
				if (result) *result = speed;
				has_result = 1;
			}
			if (!noop) {
				int64_t u = unit_of(w, op);
				hook2(w, KP_HOOK_SET_MOVE_SPEED, op, u, hook2(w, KP_HOOK_MOVE_SPEED, op, u, v));
			}
			pc = at + 1;
			break;
		}
		case 0x2a: /* gotorepeatattk */
			if (!noop && w->has_unit) hook1(w, KP_HOOK_CLEAR_REPEAT_ATTACK, op, w->unit);
			pc = at;
			break;
		case 0x2b: /* engframe */
			if (!noop) {
				int32_t main;
				img->frameset[i] = U8(0);
				main = w->spr.main[sprite_of(w, i)];
				if (main == KP_NONE) kp_images_fail(w, "iscript: engframe without a main image");
				follow_main(w, i, main);
			}
			pc = at + 1;
			break;
		case 0x2c: /* engset */
			if (!noop) {
				int32_t main = w->spr.main[sprite_of(w, i)];
				if (main == KP_NONE) kp_images_fail(w, "iscript: engset without a main image");
				img->frameset[i] = ((kp_images_grp_frames(w, main) & 0x7fff) * U8(0) + img->frameset[main]) & 0xffff;
				follow_main(w, i, main);
			}
			pc = at + 1;
			break;
		case 0x2d: /* hide the right-click order marker (UI byte 0x5c9988) */
			if (!noop) kp_images_hook(w, KP_HOOK_HIDE_ORDER_MARKER, op, NULL, 0);
			pc = at;
			break;
		case 0x2e: /* nobrkcodestart */
			if (!noop) {
				int64_t u = unit_of(w, op);
				int32_t s;
				hook1(w, KP_HOOK_NOBRK_START, op, u);
				s = kp_check_sprite(w, (int32_t)hook1(w, KP_HOOK_UNIT_SPRITE, op, u));
				w->spr.flags[s] |= KP_SF_NOBRK;
			}
			pc = at;
			break;
		case 0x2f: /* nobrkcodeend */
			if (!noop && w->has_unit) hook1(w, KP_HOOK_NOBRK_END, op, w->unit);
			pc = at;
			break;
		case 0x30: /* ignorerest */
			if (!noop) {
				int64_t u = unit_of(w, op);
				if (has_target(w, op, u)) {
					store(w, i, state, pc, IGNORE_REST_WAIT);
					goto done;
				}
				hook1(w, KP_HOOK_IGNORE_REST_IDLE, op, u);
			}
			pc = at;
			break;
		case 0x31: /* attkshiftproj */
			if (!noop) {
				int64_t u = unit_of(w, op);
				hook2(w, KP_HOOK_SET_FORWARD_OFFSET, op, u, U8(0));
				hook2(w, KP_HOOK_ATTACK_WITH, op, u, WEAPON_GROUND);
			}
			pc = at + 1;
			break;
		case 0x32: /* tmprmgraphicstart */
			if (!noop) kp_image_hide(w, i);
			pc = at;
			break;
		case 0x33: /* tmprmgraphicend */
			if (!noop) kp_image_show(w, i);
			pc = at;
			break;
		case 0x34: /* setfldirect */
			if (!noop) hook2(w, KP_HOOK_SET_FLINGY_DIRECTION, op, unit_of(w, op), U8(0) * 8);
			pc = at + 1;
			break;
		case 0x35: /* call (one level, also in no-op mode) */
			if (state) state->ret = at + 2;
			else img->ret[i] = at + 2;
			pc = U16(0);
			break;
		case 0x36: /* return */
			pc = state ? state->ret : img->ret[i];
			break;
		case 0x37: /* setflspeed */
			if (!noop) hook2(w, KP_HOOK_SET_TOP_SPEED, op, unit_of(w, op), U16(0));
			pc = at + 2;
			break;
		case 0x38: /* creategasoverlays */
			if (!noop) {
				int32_t n = S8(0), x, y, base, created;
				int64_t u;
				kp_images_lo_offset(w, i, KP_LO_SPECIAL, n, &x, &y);
				u = unit_of(w, op);
				base = hook1(w, KP_HOOK_UNIT_RESOURCES, op, u) != 0 ? GAS_OVERLAY_FULL : GAS_OVERLAY_EMPTY;
				created = kp_image_alloc(w);
				if (created != KP_NONE) {
					int32_t s = sprite_of(w, i);
					kp_list_insert_before(img->prev, img->next, &w->spr.images[s], i, created);
					kp_image_init_record(w, created, (base + n) & 0xffff, s, img->xoff[i] + x, img->yoff[i] + y);
					kp_image_init_drawing(w, created);
				}
			}
			pc = at + 1;
			break;
		case 0x39: /* pwrupcondjmp (also in no-op mode) */
		{
			int32_t s = img->sprite[i];
			pc = s != KP_NONE && w->spr.main[kp_check_sprite(w, s)] != i ? U16(0) : at + 2;
			break;
		}
		case 0x3a: /* trgtrangecondjmp */
			pc = at + 4;
			if (!noop) {
				int64_t u = unit_of(w, op);
				if (has_target(w, op, u)) {
					int64_t point[2], args[4];
					kp_images_hook_opt(w, KP_HOOK_TARGET_POINT, op, &u, 1, point);
					args[0] = u;
					args[1] = U16(0);
					args[2] = point[0];
					args[3] = point[1];
					if (kp_images_hook(w, KP_HOOK_WITHIN_DISTANCE, op, args, 4)) pc = U16(2);
				}
			}
			break;
		case 0x3b: /* trgtarccondjmp */
			pc = at + 6;
			if (!noop) {
				int64_t u = unit_of(w, op);
				if (has_target(w, op, u)) {
					int64_t h = hook1(w, KP_HOOK_TARGET_HEADING, op, u);
					if (hook2(w, KP_HOOK_ANGLE_DIFF, op, h, U16(0)) < U16(2)) pc = U16(4);
				}
			}
			break;
		case 0x3c: /* curdirectcondjmp */
			pc = at + 6;
			if (!noop) {
				int64_t u = unit_of(w, op);
				int64_t h = hook1(w, KP_HOOK_UNIT_HEADING, op, u);
				if (hook2(w, KP_HOOK_ANGLE_DIFF, op, h, U16(0)) < U16(2)) pc = U16(4);
			}
			break;
		case 0x3d: /* imgulnextid */
			if (!noop) kp_image_create_relative(w, i, img->id[i] + 1, img->xoff[i] + U8(0), U8(1) + img->yoff[i], 0);
			pc = at + 2;
			break;
		case 0x3f: /* liftoffcondjmp */
			pc = at + 2;
			if (!noop) {
				int64_t u = unit_of(w, op);
				if ((uint64_t)hook1(w, KP_HOOK_UNIT_STATUS, op, u) / KP_STATUS_AIR & 1) pc = U16(0);
			}
			break;
		case 0x40: /* warpoverlay */
			if (!noop) img->param[i] = U16(0);
			pc = at + 2;
			break;
		case 0x41: /* orderdone */
			if (!noop) kp_images_flingy_signal(w, 0, U8(0), op);
			pc = at + 1;
			break;
		case 0x42: /* grdsprol */
			if (!noop) {
				int32_t s = sprite_of(w, i), dx = S8(2), dy = S8(3);
				int32_t px = kp_s16((uint32_t)(kp_s16((uint32_t)w->spr.x[s]) + img->xoff[i] + dx));
				int32_t py = kp_s16((uint32_t)(img->yoff[i] + dy + kp_s16((uint32_t)w->spr.y[s])));
				if (hook3(w, KP_HOOK_UNIT_FITS, op, 0, px, py)) {
					kp_image_spawn_sprite(w, i, U16(0), dx, dy, w->spr.elevation[s] + 1);
				}
			}
			pc = at + 4;
			break;
		default: /* 0x0c, 0x3e, 0x43 and above 0x44: one-byte no-ops */
			pc = at;
			break;
		}
	}
done:
	return has_result;
}
