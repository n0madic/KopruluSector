/* Constants and helpers shared by the kp_images sources (not part of the library's API). */
#ifndef KP_IMAGES_INTERNAL_H
#define KP_IMAGES_INTERNAL_H

#include "kp_images.h"
#include "kp_int.h"

#ifdef __cplusplus
extern "C" {
#endif

/* game/images/image.lua */
#define KP_METHOD_CLOAK 2
#define KP_METHOD_DECLOAK 4
#define KP_METHOD_DECLOAK_BLEND 7
#define KP_METHOD_REMAP 9
#define KP_METHOD_SHADOW 10
#define KP_METHOD_HEALTH 11
#define KP_METHOD_SELECTION 13
#define KP_METHOD_PLAYER_COLOR 14
#define KP_METHOD_BOX 15
#define KP_METHOD_HALLUCINATION 16
#define KP_METHOD_WARP_FIRE 17
#define KP_WARP_FIRE_PARAM 0x230u
#define KP_CLOAK_START 0x300u
#define KP_DECLOAK_START 0x308u
#define KP_LO_DAMAGE 1
#define KP_LO_SPECIAL 2
#define KP_DAMAGE_SLOTS 22
#define KP_LO_UNUSED 127
#define KP_ANIM_INIT 0
#define KP_ANIM_DEATH 1
#define KP_ANIM_GND_ATTK_INIT 2
#define KP_ANIM_AIR_ATTK_INIT 3
#define KP_ANIM_GND_ATTK_RPT 5
#define KP_ANIM_AIR_ATTK_RPT 6
#define KP_ANIM_WALKING 11
#define KP_ANIM_IS_WORKING 19
#define KP_STATUS_AIR 0x04
#define KP_STATUS_CLOAKED 0x300
#define KP_STATUS_HALLUCINATION 0x40000000

/* `flags` with `bit` set when on is nonzero, else cleared. */
static inline int32_t kp_flag(int32_t flags, int32_t bit, uint32_t on) { return on ? flags | bit : flags & ~bit; }

/* Record indices read from fields that may hold KP_NONE; out of range fails. */
int32_t kp_check_image(KpImages *w, int32_t i);
int32_t kp_check_sprite(KpImages *w, int32_t s);

/* The dat value of `c` at flat index i; a missing column or index fails. */
uint32_t kp_images_dat(KpImages *w, const KpDatColumn *c, int32_t i);

/* iscript.bin reads; outside the code fails. */
static inline uint32_t kp_code_u8(KpImages *w, int64_t at)
{
	if (at < 0 || at >= (int64_t)w->script->code_size) kp_images_fail(w, "iscript: read at 0x%llx beyond the code", (long long)at);
	return w->script->code[at];
}

static inline uint32_t kp_code_u16(KpImages *w, int64_t at)
{
	return kp_code_u8(w, at) | kp_code_u8(w, at + 1) << 8;
}

/* PlayImageAnimation's entry point of animation `anim` from image i's header. */
int32_t kp_images_animation_pc(KpImages *w, int32_t i, int32_t anim);

#ifdef __cplusplus
}
#endif

#endif
