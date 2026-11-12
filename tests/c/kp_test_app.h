/* The helpers of the C suites that run the product's own code (native/kp_app: test_session.c, test_flow.c). */
#ifndef KP_TEST_APP_H
#define KP_TEST_APP_H

#include "kp_host.h"
#include "kp_res.h"
#include "kp_test.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The install the environment variable env names (KP_TEST_ORIGINAL: 1.17, KP_TEST_SCR: StarCraft: Remastered) opened
 * through kp_res, closed when the test ends; skips the test when env is not set, fails when the install does not
 * open. */
KpRes *kp_t_install(KpT *T, const char *env);

/* --- a Smacker movie written for a test (test_movies.c, test_convert.c) --- */

/* Smacker's bits, the least significant first */
typedef struct {
	uint8_t bytes[4096];
	size_t at; /* bits */
} KpTBits;

void kp_t_bits(KpTBits *o, uint32_t v, int n);
size_t kp_t_bits_bytes(const KpTBits *o);
/* A leaf of an 8-bit tree, and a node of two leaves */
void kp_t_bits_leaf(KpTBits *o, uint8_t v);
void kp_t_bits_pair(KpTBits *o, uint8_t zero, uint8_t one);

#define KP_T_SMK_CHUNKS 256 /* the most bytes a frame's chunks before its video take */

/* Frame f's chunks before its video (a palette record, its sound tracks' chunks) into out: their size, the frame's type
 * byte (bit 0 a palette record, bit 1 + k track k's chunk) into *type (0 when none is written). */
typedef size_t (*KpTSmkChunks)(void *ctx, int f, uint8_t *type, uint8_t *out);

/* An SMK2 of width x height (multiples of 4, at most 2048 blocks of 4 x 4), frames frames at the cinematics' rate
 * (-6666: 50000/3333 a second), every pixel the index fill (the TYPE tree one leaf: fill blocks of that colour), track
 * k's header flags and rate tracks[k] (0: none), each frame's chunks before its video from chunks (NULL: none): its
 * bytes, freed when the test ends, and *size. */
uint8_t *kp_t_smk(KpT *T, uint32_t width, uint32_t height, uint32_t frames, uint8_t fill, const uint32_t tracks[2],
	KpTSmkChunks chunks, void *ctx, size_t *size);

#ifdef __cplusplus
}
#endif

#endif
