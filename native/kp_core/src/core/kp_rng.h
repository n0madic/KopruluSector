/*
 * The three independent random generators of StarCraft 1.17.0.1 (plan D14). None consumes
 * another's state.
 *   game     : simulation LCG, 0x478200 Rand15 / 0x478270 RandRange (docs/research/native-prng.md)
 *   crt      : MSVC CRT rand/srand, 0x4d14d4 / 0x4d14f5 (docs/research/atlas/c01.md)
 *   portrait : console portrait RNG 0x4a6e00 (docs/research/native-audiovisual.md)
 */
#ifndef KP_RNG_H
#define KP_RNG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KP_RNG_SOURCES 256 /* dword counters at 0x673940 */

typedef struct {
	uint32_t initial_seed; /* 0x673934 */
	uint32_t state;        /* 0x67393c */
	uint32_t enabled;      /* 0x673930; nonzero enables */
	uint32_t total;        /* 0x673938 */
	uint32_t counters[KP_RNG_SOURCES];
} KpRngGame;

typedef struct {
	uint32_t holdrand;
} KpRngCrt;

typedef struct {
	uint32_t x, y, z; /* 0x50d7cc, 0x50d7d0, 0x50d7d4 */
} KpRngPortrait;

void kp_rng_game_init(KpRngGame *r, uint32_t initial_seed);
uint32_t kp_rng_game_set_enabled(KpRngGame *r, uint32_t flag); /* 0x4781c0: returns the old flag */
void kp_rng_game_seed(KpRngGame *r, uint32_t seed);              /* 0x478240 */
uint32_t kp_rng_game_raw(KpRngGame *r, uint8_t source);          /* 0x478200 */
uint32_t kp_rng_game_range(KpRngGame *r, uint8_t source, uint32_t from, uint32_t to); /* 0x478270 */

void kp_rng_crt_init(KpRngCrt *r, uint32_t seed);
uint32_t kp_rng_crt_rand(KpRngCrt *r);

/* The words stored in the mapped PE: file state, not a post-startup snapshot. */
void kp_rng_portrait_init(KpRngPortrait *r);
/* 0x4a6e00: a value in [low, high]; mask_size is a power of two above high - low.
 * Rejected candidates still advance the state. */
int64_t kp_rng_portrait_draw(KpRngPortrait *r, int64_t low, int64_t high, uint32_t mask_size);

#ifdef __cplusplus
}
#endif

#endif
