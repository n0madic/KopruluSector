/* The lobby's fuzz targets lobby and transfer (kp_fuzz.h; phase N, stage N3), kp_fuzz.c's entries call them. */
#ifndef KP_FUZZ_LOBBY_H
#define KP_FUZZ_LOBBY_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One input of the target: 0, or -1 with the reason in why when a property failed. */
int kp_fuzz_lobby_run(const uint8_t *d, size_t size, char *why, size_t why_size);
int kp_fuzz_transfer_run(const uint8_t *d, size_t size, char *why, size_t why_size);

/* The seeds, recorded from Koprulu's own lobbies and transfers in the process: fn(ud, lobby, name, bytes) for each
 * (lobby 0: the transfer target's), each replayed first, which must do what its session did. Their count, -1 with the
 * reason when one could not be made. */
typedef void (*KpFuzzLobbySeed)(void *ud, int lobby, const char *name, const uint8_t *b, size_t n);
int kp_fuzz_lobby_seeds(KpFuzzLobbySeed fn, void *ud, char *why, size_t why_size);

#ifdef __cplusplus
}
#endif

#endif
