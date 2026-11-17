/*
 * What the network's layers share (phase N, stage N2; ADR 0026, docs/formats/storm-udp.md): a provider address,
 * Storm's packet checksum, and the I/O a provider's state machine is given.
 *
 * Layers (kp_snet.h, kp_snp_udp.h): Storm's SNet (kp_snet) over standard.snp's UDP provider (kp_snp_udp), both pure
 * state machines. The provider hands its datagrams to KpNetIo.send and is fed what arrives (kp_snp_udp_input); a
 * transport carries them: real sockets (kp_net_sock.h), the in-process LAN of the tests (kp_net_lan.h), later the relay.
 */
#ifndef KP_NET_H
#define KP_NET_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KP_NET_ADDR 16
#define KP_NET_LIST_PORT 6111 /* the provider's socket A: the game list */
#define KP_NET_GAME_PORT 6112 /* socket B: Storm's packets */
#define KP_NET_BROADCAST 0xffffffffu

/* The provider's sockets (atlas U01): A on port 6111, B on port 6112. */
enum { KP_NET_SOCKET_LIST, KP_NET_SOCKET_GAME, KP_NET_SOCKETS };

/* A provider address as Storm holds it (player record +0x108, command 6): Winsock's sockaddr_in, family 2
 * little-endian, the port and the IPv4 address in network order, eight zero bytes. */
typedef struct {
	uint8_t b[KP_NET_ADDR];
} KpNetAddr;

/* The address of IPv4 `ip` (host order, 10.0.0.1 = 0x0a000001) and `port`. */
KpNetAddr kp_net_addr(uint32_t ip, uint16_t port);
uint32_t kp_net_addr_ip(const KpNetAddr *a);
uint16_t kp_net_addr_port(const KpNetAddr *a);
int kp_net_addr_eq(const KpNetAddr *a, const KpNetAddr *b);
/* "a.b.c.d:port" into out (at least 22 bytes). */
const char *kp_net_addr_text(const KpNetAddr *a, char *out, size_t size);

/* Storm's FletcherSum 0x15021c50 of n bytes, walked from the last to the first: 0 over a whole valid packet. */
uint16_t kp_net_sum(const uint8_t *p, size_t n);
/* ComputePacketChecksum 0x15021ca0: bytes 0 and 1 of a packet (or a game-list datagram) of `size` bytes from the sum
 * over bytes 2 to the end; nothing below 2 bytes. */
void kp_net_checksum(uint8_t *packet, size_t size);

/* A provider's I/O: `send` transmits a datagram from one of its sockets (KP_NET_SOCKET_*), `now` is the millisecond
 * clock (GetTickCount: wraps, compared by differences). */
typedef struct {
	void (*send)(void *ud, int socket, const KpNetAddr *to, const uint8_t *data, size_t size);
	uint32_t (*now)(void *ud);
	void *ud;
} KpNetIo;

static inline uint32_t kp_net_get16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static inline uint32_t kp_net_get32(const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static inline void kp_net_put16(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
}
static inline void kp_net_put32(uint8_t *p, uint32_t v)
{
	kp_net_put16(p, v);
	kp_net_put16(p + 2, v >> 16);
}

#ifdef __cplusplus
}
#endif

#endif
