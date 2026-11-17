/* What the network's layers share (kp_net.h; phase N, stage N2). */
#include "kp_net.h"

#include <stdio.h>
#include <string.h>

KpNetAddr kp_net_addr(uint32_t ip, uint16_t port)
{
	KpNetAddr a;
	memset(&a, 0, sizeof a);
	a.b[0] = 2; /* AF_INET */
	a.b[2] = (uint8_t)(port >> 8);
	a.b[3] = (uint8_t)port;
	a.b[4] = (uint8_t)(ip >> 24);
	a.b[5] = (uint8_t)(ip >> 16);
	a.b[6] = (uint8_t)(ip >> 8);
	a.b[7] = (uint8_t)ip;
	return a;
}

uint32_t kp_net_addr_ip(const KpNetAddr *a)
{
	return (uint32_t)a->b[4] << 24 | (uint32_t)a->b[5] << 16 | (uint32_t)a->b[6] << 8 | a->b[7];
}

uint16_t kp_net_addr_port(const KpNetAddr *a) { return (uint16_t)(a->b[2] << 8 | a->b[3]); }

int kp_net_addr_eq(const KpNetAddr *a, const KpNetAddr *b) { return memcmp(a->b, b->b, KP_NET_ADDR) == 0; }

const char *kp_net_addr_text(const KpNetAddr *a, char *out, size_t size)
{
	snprintf(out, size, "%u.%u.%u.%u:%u", a->b[4], a->b[5], a->b[6], a->b[7], kp_net_addr_port(a));
	return out;
}

uint16_t kp_net_sum(const uint8_t *p, size_t n)
{
	uint32_t a = 0, b = 0;
	while (n) {
		a += p[--n];
		if (a > 254) a -= 255;
		b += a;
	}
	/* (u8)(b + b / 255) is b mod 255 */
	return (uint16_t)((a & 0xff) << 8 | (uint8_t)(b + b / 255));
}

void kp_net_checksum(uint8_t *packet, size_t size)
{
	uint32_t s, lo, hi, c1, c0;
	if (size < 2) return;
	s = kp_net_sum(packet + 2, size - 2);
	lo = s & 0xff;
	hi = s >> 8;
	c1 = 0xff - (lo + hi) % 255;
	c0 = 0xff - (c1 + hi) % 255;
	packet[0] = (uint8_t)c0;
	packet[1] = (uint8_t)c1;
}
