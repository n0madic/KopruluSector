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

static int host_char(int c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_';
}

int kp_net_host_parse(const char *text, char *name, size_t size, uint16_t *port)
{
	const char *s = text, *end, *colon = NULL;
	size_t n;
	uint32_t value = KP_NET_LIST_PORT;
	if (!text || !name || size < KP_NET_HOST || !port) return 0;
	while (*s == ' ') s++;
	end = s + strlen(s);
	while (end > s && end[-1] == ' ') end--;
	for (n = 0; s + n < end; n++) {
		if (s[n] == ':') {
			colon = s + n;
			break;
		}
		if (!host_char((unsigned char)s[n])) return 0;
	}
	if (n == 0 || n >= KP_NET_HOST) return 0;
	memcpy(name, s, n);
	name[n] = 0;
	/* digits and dots alone are an IPv4 address, whole (a resolver reads "1.2.3" as 1.2.0.3) */
	if (name[strspn(name, "0123456789.")] == 0 && !kp_net_ipv4_parse(name, &value)) return 0;
	value = KP_NET_LIST_PORT;
	if (colon) {
		const char *d = colon + 1;
		if (d == end) return 0;
		for (value = 0; d < end; d++) {
			if (*d < '0' || *d > '9') return 0;
			value = value * 10u + (uint32_t)(*d - '0');
			if (value > 0xfffeu) return 0;
		}
		if (value == 0) return 0;
	}
	*port = (uint16_t)value;
	return 1;
}

int kp_net_ipv4_parse(const char *text, uint32_t *ip)
{
	uint32_t v = 0;
	int part;
	const char *s = text;
	if (!text || !ip) return 0;
	for (part = 0; part < 4; part++) {
		uint32_t b = 0;
		int digits = 0;
		while (*s >= '0' && *s <= '9') {
			b = b * 10u + (uint32_t)(*s++ - '0');
			if (++digits > 3 || b > 255) return 0;
		}
		if (!digits) return 0;
		v = v << 8 | b;
		if (part < 3 && *s++ != '.') return 0;
	}
	if (*s) return 0;
	*ip = v;
	return 1;
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
