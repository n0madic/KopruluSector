/* The network fuzz targets udp and snet (kp_fuzz.h; phase N, stage N2), kp_fuzz.c's entries call them. */
#ifndef KP_FUZZ_NET_H
#define KP_FUZZ_NET_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One input of the target: 0, or -1 with the reason in why when a property failed. */
int kp_fuzz_net_udp(const uint8_t *d, size_t size, char *why, size_t why_size);
int kp_fuzz_net_snet(const uint8_t *d, size_t size, char *why, size_t why_size);

/* The seeds, made from Koprulu's own traffic on the in-process LAN: fn(ud, snet, name, bytes) for each (snet 0: the
 * udp target's). Their count, -1 with the reason when one could not be made. */
typedef void (*KpFuzzNetSeed)(void *ud, int snet, const char *name, const uint8_t *b, size_t n);
int kp_fuzz_net_seeds(KpFuzzNetSeed fn, void *ud, char *why, size_t why_size);

#ifdef __cplusplus
}
#endif

#endif
