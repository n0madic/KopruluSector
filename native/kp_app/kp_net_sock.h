/*
 * The provider's sockets over the system's UDP (phase N, stage N2; kp_net.h): POSIX and Winsock, none on the web (no
 * network play there: kp_netplay.h). Socket A takes the game list (port 6111) and socket B Storm's packets (port 6112,
 * the one above A's), both with broadcasts allowed, as standard.snp binds them (atlas U01).
 *
 * Addresses. On a LAN a datagram's source is what the system reports, and a broadcast goes to 255.255.255.255:6111.
 * With a map (the network oracle's loopback bridge, tools/research/sc_emulator --bridge BASE, and Koprulu's own games
 * on one machine) the virtual hosts 10.0.0.1 to 10.0.0.n are the real ports 127.0.0.1:BASE+2k (their 6111) and
 * BASE+2k+1 (their 6112): a send to a virtual address goes to its real port, a broadcast to every host's 6111 port, and
 * what arrives from a mapped port comes in as its virtual address. The sockets are bound to 127.0.0.1 then, else to
 * every interface. A program the map does not hold (Koprulu against the bridge) binds two adjacent ports of its own:
 * the bridge names it 10.0.1.n, and to itself it is 127.0.0.2 (its broadcasts come back to it, as on Windows).
 *
 * Nothing here runs a thread but a name's lookup (kp_net_lookup_*): kp_net_sock_wait reads what arrived, the caller
 * feeds the provider and pumps SNet.
 */
#ifndef KP_NET_SOCK_H
#define KP_NET_SOCK_H

#include <stddef.h>
#include <stdint.h>

#include "kp_net.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KP_NET_MAP_IP 0x0a000001u      /* 10.0.0.1, the map's first host */
#define KP_NET_LOOPBACK_IP 0x7f000001u /* 127.0.0.1, whose ports a map's hosts are */

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
/* The calling thread asleep for ms milliseconds (Storm's waits once the sockets failed). */
void kp_net_sock_sleep(uint32_t ms);
/* Datagrams the system refused to send (an unreachable host, a full buffer). */
long kp_net_sock_send_errors(const KpNetSock *s);
/* A host's address as the player gives it (kp_net_host_parse) looked up as its socket A's provider address, off the
 * caller's thread: a dotted IPv4 address, or a text that is no address, is answered at once; a name goes to the
 * system's resolver (IPv4 only: Storm's addresses are sockaddr_in) on a thread of its own, which may take as long as
 * the resolver does. The web has no resolver: a name has no address there. */
typedef struct KpNetLookup KpNetLookup;
/* The lookup of text started; NULL only when out of memory. */
KpNetLookup *kp_net_lookup_start(const char *text);
/* Never waits: 1 the address into *out, 0 none (the reason in `error`), -1 still resolving. */
int kp_net_lookup_poll(KpNetLookup *l, KpNetAddr *out, char *error, size_t size);
/* Frees l (NULL nothing); one still resolving is given up, its thread frees it once the resolver returns. */
void kp_net_lookup_free(KpNetLookup *l);

#ifdef __cplusplus
}
#endif

#endif
