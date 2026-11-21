/*
 * The in-process LAN (phase N, stage N2): a deterministic network of hosts for the tests and the fuzz targets, the
 * transport of providers that run in one process (kp_net.h). Each host has an IPv4 address and the provider's two
 * sockets (ports 6111 and 6112); a datagram goes to the host with its address (or, broadcast, to every host) and to the
 * socket of its port, after a delay drawn from the seed, or is lost. As on Windows, a host's own broadcasts and the
 * datagrams to its own address come back to it, at once and never lost (the network oracle's switch,
 * tools/research/sc_emulator/lan.py).
 *
 * Time is the LAN's millisecond clock, which only kp_net_lan_run moves: the providers' `now`. Nothing is delivered
 * inside a send, so a provider that answers what it is given never runs inside itself.
 */
#ifndef KP_NET_LAN_H
#define KP_NET_LAN_H

#include <stddef.h>
#include <stdint.h>

#include "kp_net.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KP_NET_LAN_HOSTS 16
#define KP_NET_LAN_IDLE 0xffffffffu /* kp_net_lan_next: nothing in flight */

/* What a host is given: a datagram that arrived on one of its sockets from `from`. */
typedef void (*KpNetLanDeliver)(void *ud, int socket, const KpNetAddr *from, const uint8_t *data, size_t size);

typedef struct KpNetLan KpNetLan;

/* A LAN whose clock starts at `start` ms (GetTickCount is far from 0 on a running machine), draws from `seed`. NULL
 * out of memory. */
KpNetLan *kp_net_lan_new(uint64_t seed, uint32_t start);
void kp_net_lan_free(KpNetLan *lan);
/* Each copy to another host is lost with probability loss/1000, else arrives delay + [0, jitter] ms after its send
 * (defaults: 1, 1, 0). */
void kp_net_lan_link(KpNetLan *lan, uint32_t delay, uint32_t jitter, uint32_t loss);
/* A host at `ip` (host order); its index, -1 when the LAN is full. */
int kp_net_lan_add(KpNetLan *lan, uint32_t ip, KpNetLanDeliver deliver, void *ud);
/* While cut, nothing reaches the host or leaves it (its cable pulled). */
void kp_net_lan_cut(KpNetLan *lan, int host, int cut);
/* Off (0): a broadcast reaches its sender alone, as on the internet or a network that drops broadcasts (default on). */
void kp_net_lan_broadcasts(KpNetLan *lan, int on);
/* The I/O of a provider at the host: its sends enter the LAN, its clock is the LAN's. */
KpNetIo kp_net_lan_io(KpNetLan *lan, int host);
uint32_t kp_net_lan_now(const KpNetLan *lan);
/* The arrival time of the earliest datagram in flight, KP_NET_LAN_IDLE for none. */
uint32_t kp_net_lan_next(const KpNetLan *lan);
/* Delivers the datagrams due at or before `to` in the order of their arrival (their send order on a tie), the clock
 * at each arrival, then sets the clock to `to` (never back). What the deliveries send joins the run when it is due
 * by then. Returns the datagrams delivered. */
int kp_net_lan_run(KpNetLan *lan, uint32_t to);

/* Called for every datagram a host sends, before it is routed: the time, the sending host and socket, the
 * destination as given, the bytes. */
typedef void (*KpNetLanTap)(void *ud, uint32_t at, int host, int socket, const KpNetAddr *to, const uint8_t *data,
	size_t size);
void kp_net_lan_tap(KpNetLan *lan, KpNetLanTap tap, void *ud);

/* Counters: sent (copies routed), delivered, lost, cut, unreachable (no host at the address). */
typedef struct {
	long sent, delivered, lost, cut, unreachable;
} KpNetLanStats;
KpNetLanStats kp_net_lan_stats(const KpNetLan *lan);

#ifdef __cplusplus
}
#endif

#endif
