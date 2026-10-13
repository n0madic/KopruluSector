/* The three random generators (kp_rng.h). */
#include "kp_rng.h"

#include "kp_int.h"

#define GAME_MULTIPLIER 0x015a4e35u
#define CRT_MULTIPLIER 214013u
#define CRT_INCREMENT 2531011u
#define PORTRAIT_STEP 0x71823402u

void kp_rng_game_init(KpRngGame *r, uint32_t initial_seed)
{
	int i;
	r->initial_seed = initial_seed;
	r->state = 0;
	r->enabled = 0;
	r->total = 0;
	for (i = 0; i < KP_RNG_SOURCES; i++) r->counters[i] = 0;
}

uint32_t kp_rng_game_set_enabled(KpRngGame *r, uint32_t flag)
{
	uint32_t old = r->enabled;
	r->enabled = flag;
	return old;
}

void kp_rng_game_seed(KpRngGame *r, uint32_t seed)
{
	int i;
	r->state = seed;
	r->total = 0;
	for (i = 0; i < KP_RNG_SOURCES; i++) r->counters[i] = 0;
}

uint32_t kp_rng_game_raw(KpRngGame *r, uint8_t source)
{
	if (r->enabled == 0) return 0;
	r->state = kp_mul(r->state, GAME_MULTIPLIER) + 1u;
	r->counters[source]++;
	r->total++;
	return (r->state >> 16) & 0x7fffu;
}

uint32_t kp_rng_game_range(KpRngGame *r, uint8_t source, uint32_t from, uint32_t to)
{
	uint32_t raw, span;
	if (r->enabled == 0) return from;
	raw = kp_rng_game_raw(r, source);
	span = to - from + 1u;
	return from + kp_shr(kp_mul(raw, span), 15);
}

void kp_rng_crt_init(KpRngCrt *r, uint32_t seed)
{
	r->holdrand = seed;
}

uint32_t kp_rng_crt_rand(KpRngCrt *r)
{
	r->holdrand = kp_mul(r->holdrand, CRT_MULTIPLIER) + CRT_INCREMENT;
	return (r->holdrand >> 16) & 0x7fffu;
}

void kp_rng_portrait_init(KpRngPortrait *r)
{
	r->x = 0x12345678u;
	r->y = 0xe9e9e9e9u;
	r->z = 0x10934134u;
}

int64_t kp_rng_portrait_draw(KpRngPortrait *r, int64_t low, int64_t high, uint32_t mask_size)
{
	uint32_t x = r->x, y = r->y, z = r->z, candidate;
	int64_t limit = high - low + 1;
	do {
		x += y;
		y += PORTRAIT_STEP;
		z += x;
		x >>= 1;
		y ^= z;
		candidate = (y ^ x) % mask_size;
	} while ((int64_t)candidate >= limit);
	r->x = x;
	r->y = y;
	r->z = z;
	return low + (int64_t)candidate;
}
