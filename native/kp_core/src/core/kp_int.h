/*
 * Exact x86 integer arithmetic of StarCraft 1.17 in portable C. Values are fixed-width:
 * uint32_t for registers and unsigned fields, int32_t/int16_t/int8_t for signed views. Nothing
 * here relies on signed overflow, on implementation-defined narrowing or on the sign behaviour
 * of >> (plan D7, ADR 0008).
 */
#ifndef KP_INT_H
#define KP_INT_H

#include <stdint.h>

#define KP_S32_MIN (-2147483647 - 1)

/* Signed views of the low bits. */
static inline int32_t kp_s8(uint32_t x) { x &= 0xffu; return x >= 0x80u ? (int32_t)x - 0x100 : (int32_t)x; }
static inline int32_t kp_s16(uint32_t x) { x &= 0xffffu; return x >= 0x8000u ? (int32_t)x - 0x10000 : (int32_t)x; }
static inline int32_t kp_s32(uint32_t x)
{
	return x >= 0x80000000u ? (int32_t)(x - 0x80000000u) + KP_S32_MIN : (int32_t)x;
}

/* The two's complement bits of a signed value. */
static inline uint32_t kp_u32(int32_t x) { return x < 0 ? (uint32_t)(x - KP_S32_MIN) + 0x80000000u : (uint32_t)x; }

/* Low and unsigned high 32 bits of a product (x86 imul/mul). */
static inline uint32_t kp_mul(uint32_t a, uint32_t b) { return (uint32_t)((uint64_t)a * b); }
static inline uint32_t kp_mulhi(uint32_t a, uint32_t b) { return (uint32_t)(((uint64_t)a * b) >> 32); }

/* Shifts take the count modulo 32, as x86 does for 32-bit operands. */
static inline uint32_t kp_shl(uint32_t x, uint32_t n) { return x << (n & 31u); }
static inline uint32_t kp_shr(uint32_t x, uint32_t n) { return x >> (n & 31u); }

/* Arithmetic right shift: rounds toward negative infinity. */
static inline int32_t kp_sar(int32_t x, uint32_t n)
{
	n &= 31u;
	if (x >= 0) return (int32_t)((uint32_t)x >> n);
	return -1 - (int32_t)((uint32_t)(-1 - x) >> n);
}

/*
 * C/x86 signed division: the quotient truncates toward zero, the remainder takes the sign of
 * the dividend. The caller guarantees b != 0 and not (a == INT32_MIN and b == -1), which
 * fault on x86; kp_idiv_faults tells.
 */
static inline int kp_idiv_faults(int32_t a, int32_t b) { return b == 0 || (a == KP_S32_MIN && b == -1); }
static inline int32_t kp_idiv(int32_t a, int32_t b) { return a / b; }
static inline int32_t kp_irem(int32_t a, int32_t b) { return a % b; }

#endif
