/* Helpers shared by the kp_game sources (not part of the library's API). */
#ifndef KP_GAME_INTERNAL_H
#define KP_GAME_INTERNAL_H

#include "kp_game.h"
#include "kp_images_internal.h"
#include "kp_int.h"

#ifdef __cplusplus
extern "C" {
#endif

/* int.lua over Lua numbers held as int64_t: the 32-bit results of band/bor/... are unsigned. */
static inline int64_t kp_band(int64_t a, int64_t b) { return (int64_t)((uint32_t)a & (uint32_t)b); }
static inline int64_t kp_bor(int64_t a, int64_t b) { return (int64_t)((uint32_t)a | (uint32_t)b); }
static inline int64_t kp_bnot(int64_t a) { return (int64_t)(uint32_t)~(uint32_t)a; }
static inline int64_t kp_bxor(int64_t a, int64_t b) { return (int64_t)((uint32_t)a ^ (uint32_t)b); }
static inline int64_t kp_s32v(int64_t a) { return kp_s32((uint32_t)a); }
static inline int64_t kp_s16v(int64_t a) { return kp_s16((uint32_t)a); }
static inline int64_t kp_shrv(int64_t a, int64_t n) { return (int64_t)kp_shr((uint32_t)a, (uint32_t)n); }
static inline int64_t kp_shlv(int64_t a, int64_t n) { return (int64_t)kp_shl((uint32_t)a, (uint32_t)n); }
/* int.idiv (truncating division of the numbers as they are), int.udiv and int.urem (of their
 * low 32 bits); a zero divisor and INT_MIN / -1 fail, as in int.lua. */
int64_t kp_idiv64(KpGame *g, int64_t a, int64_t b);
int64_t kp_udiv64(KpGame *g, int64_t a, int64_t b);
int64_t kp_urem64(KpGame *g, int64_t a, int64_t b);
static inline int64_t kp_sarv(int64_t a, int64_t n) { return kp_sar(kp_s32((uint32_t)a), (uint32_t)n); }
static inline int64_t kp_mulv(int64_t a, int64_t b) { return (int64_t)kp_mul((uint32_t)a, (uint32_t)b); }
static inline int64_t kp_addv(int64_t a, int64_t b) { return (int64_t)(uint32_t)((uint32_t)a + (uint32_t)b); }
static inline int64_t kp_u32v(int64_t a) { return (int64_t)(uint32_t)a; }
/* Lua's floored % for a positive m. */
static inline int64_t kp_mod(int64_t a, int64_t m)
{
	int64_t r = a % m;
	return r < 0 ? r + m : r;
}

/* The native list helpers over int64_t links whose null link is `none` (0 for units). */
void kp_list64_unlink(int64_t *prev, int64_t *next, KpList *list, int64_t node, int64_t none);
void kp_list64_insert_after(int64_t *prev, int64_t *next, KpList *list, int64_t existing, int64_t node, int64_t none);
void kp_list64_insert_after_head(int64_t *prev, int64_t *next, KpList *list, int64_t node, int64_t none);
void kp_list64_insert_before(int64_t *prev, int64_t *next, KpList *list, int64_t existing, int64_t node, int64_t none);

#ifdef __cplusplus
}
#endif

#endif
