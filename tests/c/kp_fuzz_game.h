/* The network game's fuzz target game (kp_fuzz.h; phase N after stage N6), kp_fuzz.c's entries call it. */
#ifndef KP_FUZZ_GAME_H
#define KP_FUZZ_GAME_H

#include <stddef.h>
#include <stdint.h>

#include "kp_data.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The seat's game started once a process over the data (its first turn run, then its snapshot): 0, or -1 with the
 * reason in why. */
int kp_fuzz_game_init(const KpData *data, char *why, size_t why_size);
/* One input from the snapshot: 0, or -1 with the reason in why when a property failed. */
int kp_fuzz_game_run(const uint8_t *d, size_t size, char *why, size_t why_size);
void kp_fuzz_game_free(void);

/* What the last input did (test_fuzz checks that the seeds reach their paths): the frames the game ran, the turns and
 * messages the seat sent, the messages it took, the players it dropped, whether a fatal report latched. */
typedef struct {
	int frames, turns_sent, messages_sent, messages_taken, drops, fatal;
} KpFuzzGameLast;
const KpFuzzGameLast *kp_fuzz_game_last(void);

/* The synthetic seeds (no data's bytes): fn(ud, name, bytes) for each; their count. */
typedef void (*KpFuzzGameSeed)(void *ud, const char *name, const uint8_t *b, size_t n);
int kp_fuzz_game_seeds(KpFuzzGameSeed fn, void *ud);

#ifdef __cplusplus
}
#endif

#endif
