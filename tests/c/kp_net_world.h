/* kp_net_world: the network tests' world (phase N): seats that are whole SNet stacks (kp_snet over the UDP provider)
 * on the in-process LAN (kp_net_lan.h, hosts 10.0.0.1 and up, on the LAN's clock) or on the system's sockets
 * (kp_net_sock.h, on the system's clock). The world pumps each seat as Storm's pump thread does (when a datagram
 * arrived or a send signalled it, and when its timer ran out) and runs the test's game frame of every seat each
 * KP_NET_WORLD_STEP ms, as StarCraft's loop does in the lobby and in the game.
 *
 * A test or a fuzzer's seed embeds a KpNetSeat first in its seat and a KpNetWorld first in its world, registers its
 * seats with kp_net_world_add, and sets the frame and event callbacks. The world's own failures (out of memory, a
 * socket's error) go to its fail callback, or stay in `failed` and `why` without one. */
#ifndef KP_NET_WORLD_H
#define KP_NET_WORLD_H

#include <stddef.h>
#include <stdint.h>

#include "kp_net.h"
#include "kp_net_lan.h"
#include "kp_net_sock.h"
#include "kp_snet.h"
#include "kp_snp_udp.h"

#define KP_NET_WORLD_SEATS 8
#define KP_NET_WORLD_PROGRAM 0x53544152u /* 'STAR' */
#define KP_NET_WORLD_VERSION 0xd5u
#define KP_NET_WORLD_START 1000000u /* the LAN's clock at the start: GetTickCount of a running machine */
#define KP_NET_WORLD_STEP 10        /* ms between two game frames */
#define KP_NET_WORLD_IP(k) (0x0a000001u + (uint32_t)(k))

typedef struct KpNetWorld KpNetWorld;

typedef struct {
	KpNetWorld *w;
	int index, host; /* the seat's index, its LAN host */
	KpSnpUdp snp;
	KpSnet *net;
	KpNetSock *sock; /* a world of sockets: the seat's (the test opens and closes it) */
	uint32_t wake;   /* the pump's next pass, KP_SNET_INFINITE none */
	int signalled;
} KpNetSeat;

struct KpNetWorld {
	struct KpT *T;  /* the test's (kp_test.h), NULL a fuzzer's */
	void (*fail)(void *ud, const char *why); /* the world's own failures, NULL none (failed and why kept) */
	void *fail_ud;
	int failed;
	char why[128];
	KpNetLan *lan; /* NULL: a world of sockets */
	KpNetSeat *seat[KP_NET_WORLD_SEATS];
	int count;
	int in_join;    /* the seat inside kp_snet_join_game (it pumps itself, plays no frame), -1 */
	uint64_t trace; /* FNV-1a of every datagram the LAN sent: time, host, socket, destination, bytes */
	long datagrams;
	void (*frame)(KpNetSeat *s);                         /* a seat's game frame, NULL none */
	void (*event)(KpNetSeat *s, const KpSnetEvent *e);   /* SNet's events, NULL none */
};

uint64_t kp_net_world_fnv(uint64_t h, const void *p, size_t n);
#define KP_NET_WORLD_FNV_BASIS 0xcbf29ce484222325ull

void kp_net_world_init(KpNetWorld *w, struct KpT *T, void (*fail)(void *ud, const char *why), void *fail_ud);
/* The seats' SNet stacks and the LAN freed (not the sockets). */
void kp_net_world_release(KpNetWorld *w);
/* The LAN of a seed, its datagrams traced: 1, or 0 failed. */
int kp_net_world_lan(KpNetWorld *w, uint64_t seed);
/* The next seat over io (s zeroed by the caller's allocation), its SNet stack created: 1, or 0 failed. */
int kp_net_world_add(KpNetWorld *w, KpNetSeat *s, const KpNetIo *io);
/* The next seat as LAN host 10.0.0.(count + 1): 1, or 0 failed. */
int kp_net_world_add_lan(KpNetWorld *w, KpNetSeat *s);

/* The LAN's tap that traces (ud: the world). */
void kp_net_world_tap(void *ud, uint32_t at, int host, int socket, const KpNetAddr *to, const uint8_t *data,
	size_t size);
/* What arrived at a seat (ud: the seat). */
void kp_net_world_deliver(void *ud, int socket, const KpNetAddr *from, const uint8_t *data, size_t size);

uint32_t kp_net_world_now(const KpNetWorld *w);
/* The pump thread's passes of the seats that are due. */
void kp_net_world_pump(KpNetWorld *w);
/* The network until `to`: deliveries and timers in time order, no game frames. */
void kp_net_world_network(KpNetWorld *w, uint32_t to);
/* Time until `to`: every step each seat's frame, then the network. */
void kp_net_world_advance(KpNetWorld *w, uint32_t to);
/* The sockets of a seat outside the map of the network oracle's loopback bridge (the live checks; kp_net_sock.h): spec
 * "BASE[:HOSTS]" is the bridge's first port and its emulated hosts (default 1); the seat binds two adjacent ports of
 * its own above the bridge's. With the bridge's map its emulated hosts are 10.0.0.1.. and its broadcasts reach them;
 * `alone`, the seat is the one host of a map of its own: its broadcasts reach only itself, and an emulated host is
 * the real address 127.0.0.1:BASE+2k it must be given (kp_snp_udp_direct), as a host over the internet is. NULL with
 * the reason in error. */
KpNetSock *kp_net_world_bridge_open(const char *spec, int alone, char *error, size_t size);

#endif
