#include "kp_sha256.h"

#include <string.h>

static const uint32_t K[64] = {
	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
	0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
	0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
	0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
	0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
	0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
	0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
	0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

static uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

static void compress(uint32_t h[8], const uint8_t *p)
{
	uint32_t w[64], a, b, c, d, e, f, g, k;
	int i;
	for (i = 0; i < 16; i++)
		w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
	for (i = 16; i < 64; i++) {
		uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
		uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
		w[i] = w[i - 16] + s0 + w[i - 7] + s1;
	}
	a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], k = h[7];
	for (i = 0; i < 64; i++) {
		uint32_t t1 = k + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
		uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
		k = g, g = f, f = e, e = d + t1, d = c, c = b, b = a, a = t1 + t2;
	}
	h[0] += a, h[1] += b, h[2] += c, h[3] += d, h[4] += e, h[5] += f, h[6] += g, h[7] += k;
}

void kp_sha256_init(KpSha256 *s)
{
	static const uint32_t H0[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
		0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
	memcpy(s->h, H0, sizeof H0);
	s->bytes = 0;
	s->fill = 0;
}

void kp_sha256_update(KpSha256 *s, const void *data, size_t size)
{
	const uint8_t *p = (const uint8_t *)data;
	s->bytes += size;
	if (s->fill) {
		size_t n = 64 - s->fill < size ? 64 - s->fill : size;
		memcpy(s->block + s->fill, p, n);
		s->fill += n, p += n, size -= n;
		if (s->fill < 64) return;
		compress(s->h, s->block);
		s->fill = 0;
	}
	for (; size >= 64; p += 64, size -= 64) compress(s->h, p);
	memcpy(s->block, p, size);
	s->fill = size;
}

void kp_sha256_final(KpSha256 *s, uint8_t digest[KP_SHA256_SIZE])
{
	uint64_t bits = s->bytes * 8;
	int i;
	s->block[s->fill++] = 0x80;
	if (s->fill > 56) {
		memset(s->block + s->fill, 0, 64 - s->fill);
		compress(s->h, s->block);
		s->fill = 0;
	}
	memset(s->block + s->fill, 0, 56 - s->fill);
	for (i = 0; i < 8; i++) s->block[56 + i] = (uint8_t)(bits >> (56 - 8 * i));
	compress(s->h, s->block);
	for (i = 0; i < 32; i++) digest[i] = (uint8_t)(s->h[i / 4] >> (24 - 8 * (i % 4)));
}

void kp_sha256(const void *data, size_t size, uint8_t digest[KP_SHA256_SIZE])
{
	KpSha256 s;
	kp_sha256_init(&s);
	kp_sha256_update(&s, data, size);
	kp_sha256_final(&s, digest);
}

void kp_sha256_hex(const uint8_t digest[KP_SHA256_SIZE], char hex[2 * KP_SHA256_SIZE + 1])
{
	static const char DIGITS[] = "0123456789abcdef";
	int i;
	for (i = 0; i < KP_SHA256_SIZE; i++) {
		hex[2 * i] = DIGITS[digest[i] >> 4];
		hex[2 * i + 1] = DIGITS[digest[i] & 15];
	}
	hex[2 * KP_SHA256_SIZE] = 0;
}

static int hex_digit(char c)
{
	return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

int kp_hex_decode(const char *hex, uint8_t *out, size_t n)
{
	size_t i;
	for (i = 0; i < n; i++) {
		int hi = hex_digit(hex[2 * i]), lo = hi < 0 ? -1 : hex_digit(hex[2 * i + 1]);
		if (lo < 0) return -1;
		out[i] = (uint8_t)(hi << 4 | lo);
	}
	return 0;
}
