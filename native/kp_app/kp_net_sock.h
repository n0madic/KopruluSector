/*
 * The provider's sockets over the system's UDP (phase N, stage N2; kp_net.h): POSIX and Winsock, none on the web (the
 * relay of stage N5 carries the web's datagrams). Socket A takes the game list (port 6111) and socket B Storm's packets
 * (port 6112, the one above A's), both with broadcasts allowed, as standard.snp binds them (atlas U01).
 *
 * Addresses. On a LAN a datagram's source is what the system reports, and a broadcast goes to 255.255.255.255:6111.
 * With a map (the network oracle's loopback bridge, tools/research/sc_emulator --bridge BASE, and Koprulu's own games
 * on one machine) the virtual hosts 10.0.0.1 to 10.0.0.n are the real ports 127.0.0.1:BASE+2k (their 6111) and
 * BASE+2k+1 (their 6112): a send to a virtual address goes to its real port, a broadcast to every host's 6111 port, and
 * what arrives from a mapped port comes in as its virtual address. The sockets are bound to 127.0.0.1 then, else to
 * every interface. A program the map does not hold (Koprulu against the bridge) binds two adjacent ports of its own:
 * the bridge names it 10.0.1.n, and to itself it is 127.0.0.2 (its broadcasts come back to it, as on Windows).
 *
 * Nothing here runs a thread: kp_net_sock_wait reads what arrived, the caller feeds the provider and pumps SNet.
 */
#ifndef KP_NET_SOCK_H
#define KP_NET_SOCK_H

#include <stddef.h>
#include <stdint.h>

#include "kp_net.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KP_NET_MAP_IP 0x0a000001u /* 10.0.0.1, the map's first host */

typedef struct {
	uint16_t port;     /* socket A's real port (0: 6111, or the map's port of map_self); B binds the next one */
	uint16_t map_base; /* 0: no map */
	int map_hosts;     /* the map's hosts: 10.0.0.1 to 10.0.0.map_hosts */
	int map_self;      /* this program's host in the map, -1 for none */
} KpNetSockConfig;

typedef struct KpNetSock KpNetSock;

/* What arrived on one of the sockets (KP_NET_SOCKET_*). */
typedef void (*KpNetSockDeliver)(void *ud, int socket, const KpNetAddr *from, const uint8_t *data, size_t size);

/* Binds both sockets; NULL with the reason in `error` (no sockets on the web, a port in use). */
KpNetSock *kp_net_sock_open(const KpNetSockConfig *c, char *error, size_t size);
void kp_net_sock_close(KpNetSock *s);
/* The provider's I/O over the sockets; its clock is kp_net_sock_now. */
KpNetIo kp_net_sock_io(KpNetSock *s);
/* Waits at most `ms` for a datagram, then hands every waiting one to fn; how many, -1 on a socket error. */
int kp_net_sock_wait(KpNetSock *s, uint32_t ms, KpNetSockDeliver fn, void *ud);
/* The real port socket A or B is bound to. */
uint16_t kp_net_sock_port(const KpNetSock *s, int socket);
/* A monotonic millisecond clock that wraps as GetTickCount does. */
uint32_t kp_net_sock_now(void);
/* Datagrams the system refused to send (an unreachable host, a full buffer). */
long kp_net_sock_send_errors(const KpNetSock *s);

#ifdef __cplusplus
}
#endif

#endif
