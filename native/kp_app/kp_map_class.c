/*
 * A map's class: its certificate and its strong signature (kp_map_class.h; atlas P23, P34, P42).
 */
#include "kp_map_class.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kp_host.h"
#include "kp_mpq.h"

/* --- SHA-1 --- */

typedef struct {
	uint32_t h[5];
	uint8_t block[64];
	size_t used;
	uint64_t bytes;
} Sha1;

static uint32_t rol(uint32_t x, int n) { return x << n | x >> (32 - n); }

static void sha1_block(Sha1 *s, const uint8_t *p)
{
	uint32_t w[80], a = s->h[0], b = s->h[1], c = s->h[2], d = s->h[3], e = s->h[4], f, k, t;
	int i;
	for (i = 0; i < 16; i++) {
		w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
	}
	for (i = 16; i < 80; i++) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
	for (i = 0; i < 80; i++) {
		if (i < 20) f = (b & c) | (~b & d), k = 0x5a827999u;
		else if (i < 40) f = b ^ c ^ d, k = 0x6ed9eba1u;
		else if (i < 60) f = (b & c) | (b & d) | (c & d), k = 0x8f1bbcdcu;
		else f = b ^ c ^ d, k = 0xca62c1d6u;
		t = rol(a, 5) + f + e + k + w[i];
		e = d;
		d = c;
		c = rol(b, 30);
		b = a;
		a = t;
	}
	s->h[0] += a;
	s->h[1] += b;
	s->h[2] += c;
	s->h[3] += d;
	s->h[4] += e;
}

static void sha1_update(Sha1 *s, const uint8_t *p, size_t n)
{
	s->bytes += n;
	while (n > 0) {
		size_t take = 64 - s->used < n ? 64 - s->used : n;
		memcpy(s->block + s->used, p, take);
		s->used += take;
		p += take;
		n -= take;
		if (s->used == 64) {
			sha1_block(s, s->block);
			s->used = 0;
		}
	}
}

void kp_sha1(const void *data, size_t size, const void *more, size_t more_size, uint8_t digest[KP_SHA1_SIZE])
{
	static const uint32_t H[5] = { 0x67452301u, 0xefcdab89u, 0x98badcfeu, 0x10325476u, 0xc3d2e1f0u };
	Sha1 s;
	uint8_t pad[72] = { 0x80 };
	uint64_t bits;
	size_t n;
	int i;
	memset(&s, 0, sizeof s);
	memcpy(s.h, H, sizeof H);
	sha1_update(&s, (const uint8_t *)data, size);
	if (more_size) sha1_update(&s, (const uint8_t *)more, more_size);
	bits = s.bytes * 8;
	n = s.used < 56 ? 56 - s.used : 120 - s.used;
	for (i = 0; i < 8; i++) pad[n + i] = (uint8_t)(bits >> (56 - 8 * i));
	sha1_update(&s, pad, n + 8);
	for (i = 0; i < 20; i++) digest[i] = (uint8_t)(s.h[i / 4] >> (24 - 8 * (i % 4)));
}

/* --- RSA-2048: buffer^65537 mod N (RsaApplyInPlace 0x49d3e0's SBigPowMod), by Montgomery's multiplication --- */

#define LIMBS (KP_MAP_KEY_SIZE / 4)

typedef struct {
	uint32_t v[LIMBS];
} Big;

static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

static void big_from_bytes(Big *x, const uint8_t *b)
{
	int k;
	for (k = 0; k < LIMBS; k++) x->v[k] = le32(b + 4 * k);
}

static void big_to_bytes(const Big *x, uint8_t *b)
{
	int k;
	for (k = 0; k < 4 * LIMBS; k++) b[k] = (uint8_t)(x->v[k / 4] >> (8 * (k % 4)));
}

static int big_at_least(const Big *a, const Big *b)
{
	int k;
	for (k = LIMBS - 1; k >= 0; k--) {
		if (a->v[k] != b->v[k]) return a->v[k] > b->v[k];
	}
	return 1;
}

static void big_subtract(Big *a, const Big *b)
{
	uint64_t borrow = 0;
	int k;
	for (k = 0; k < LIMBS; k++) {
		uint64_t d = (uint64_t)a->v[k] - b->v[k] - borrow;
		a->v[k] = (uint32_t)d;
		borrow = d >> 63;
	}
}

/* out = a b R^-1 mod n, R = 2^2048, for a and b below n (n odd, n0 = -n^-1 mod 2^32) */
static void mont_mul(Big *out, const Big *a, const Big *b, const Big *n, uint32_t n0)
{
	uint32_t t[LIMBS + 2];
	int i, j;
	memset(t, 0, sizeof t);
	for (i = 0; i < LIMBS; i++) {
		uint64_t c = 0;
		uint32_t m;
		for (j = 0; j < LIMBS; j++) {
			c += (uint64_t)a->v[j] * b->v[i] + t[j];
			t[j] = (uint32_t)c;
			c >>= 32;
		}
		c += t[LIMBS];
		t[LIMBS] = (uint32_t)c;
		t[LIMBS + 1] = (uint32_t)(c >> 32);
		m = t[0] * n0;
		c = ((uint64_t)m * n->v[0] + t[0]) >> 32;
		for (j = 1; j < LIMBS; j++) {
			c += (uint64_t)m * n->v[j] + t[j];
			t[j - 1] = (uint32_t)c;
			c >>= 32;
		}
		c += t[LIMBS];
		t[LIMBS - 1] = (uint32_t)c;
		t[LIMBS] = t[LIMBS + 1] + (uint32_t)(c >> 32);
	}
	memcpy(out->v, t, sizeof out->v);
	if (t[LIMBS] || big_at_least(out, n)) big_subtract(out, n);
}

/* x^65537 mod n into x; 0 when n is no 2048-bit odd modulus */
static int pow_mod(Big *x, const Big *n)
{
	Big r2, xm, acc, one;
	uint32_t inv, n0;
	int k;
	if (!(n->v[0] & 1) || !(n->v[LIMBS - 1] >> 31)) return 0;
	inv = n->v[0]; /* n^-1 mod 2^32 by Newton's iteration: each step doubles the correct bits */
	for (k = 0; k < 5; k++) inv *= 2 - n->v[0] * inv;
	n0 = 0u - inv;
	memset(&r2, 0, sizeof r2); /* R^2 mod n: 1 doubled 4096 times */
	r2.v[0] = 1;
	for (k = 0; k < 2 * 32 * LIMBS; k++) {
		uint32_t carry = r2.v[LIMBS - 1] >> 31;
		int j;
		for (j = LIMBS - 1; j > 0; j--) r2.v[j] = r2.v[j] << 1 | r2.v[j - 1] >> 31;
		r2.v[0] <<= 1;
		if (carry || big_at_least(&r2, n)) big_subtract(&r2, n);
	}
	while (big_at_least(x, n)) big_subtract(x, n); /* n > 2^2047: once at most */
	mont_mul(&xm, x, &r2, n, n0);
	acc = xm;
	for (k = 0; k < 16; k++) mont_mul(&acc, &acc, &acc, n, n0);
	mont_mul(&acc, &acc, &xm, n, n0);
	memset(&one, 0, sizeof one);
	one.v[0] = 1;
	mont_mul(x, &acc, &one, n, n0);
	return 1;
}

/* --- the signature --- */

#define NGIS 0x5349474eu       /* the strong signature's tag */
#define NAME_MAX_CHARS 1023    /* HashDataAndName 0x49d370: the name's copy */
#define PAD_BYTE 0xbb
#define PAD_LAST 0x0b

int kp_map_signature_valid(const char *name, const uint8_t *data, size_t size, const uint8_t key[KP_MAP_KEY_SIZE])
{
	uint8_t expected[KP_MAP_KEY_SIZE], got[KP_MAP_KEY_SIZE];
	char upper[NAME_MAX_CHARS + 1];
	const char *base = name;
	const uint8_t *tag;
	size_t at, n;
	Big sig, mod;
	for (at = 0; name[at]; at++) {
		if (name[at] == '\\' || name[at] == '/') base = name + at + 1;
	}
	if (size < KP_MAP_KEY_SIZE + 4) return 0;
	at = size - KP_MAP_KEY_SIZE - 4;
	tag = data + at;
	if (le32(tag) != NGIS) return 0;
	for (n = 0; base[n] && n < NAME_MAX_CHARS; n++) {
		upper[n] = base[n] >= 'a' && base[n] <= 'z' ? (char)(base[n] - ('a' - 'A')) : base[n];
	}
	upper[n] = 0;
	memset(expected, PAD_BYTE, sizeof expected);
	expected[KP_MAP_KEY_SIZE - 1] = PAD_LAST;
	kp_sha1(data, at, upper, n, expected);
	big_from_bytes(&sig, tag + 4);
	big_from_bytes(&mod, key);
	if (!pow_mod(&sig, &mod)) return 0;
	big_to_bytes(&sig, got);
	return memcmp(got, expected, sizeof got) == 0;
}

/* --- the certificate --- */

#define LEAGUE 0 /* 0x66fd3c: a Battle.net server template's sub-byte (0x447cc0), none offline */

uint32_t kp_map_certificate(KpMpq *mpq)
{
	/* ScanMapCertificates 0x4c1350's table 0x4f1020, scanned by kind from 6 down, each kind's rows from the last */
	static const struct {
		const char *name;
		uint32_t kind;
	} ROWS[] = {
		{ "Blizzard_2.cnd", 1 }, { "Ladder_2.cnd", 1 }, { "PGL_2.cnd", 3 }, { "Ladder_3.cnd", 1 }, { "Ladder_4.cnd", 2 },
		{ "KBK_2.cnd", 4 }, { "CompUSA_1.cnd", 3 }, { "WGT_1.cnd", 6 }, { "Tourn_1.cnd", 5 },
	};
	enum { KIND_LEAGUE = 5, KIND_TOP = 6 };
	char league[32];
	uint32_t kind;
	int k;
	snprintf(league, sizeof league, "League_%d.cnd", LEAGUE);
	for (kind = KIND_TOP; kind > 0; kind--) {
		for (k = (int)(sizeof ROWS / sizeof ROWS[0]) - 1; k >= 0; k--) {
			if (ROWS[k].kind != kind || !kp_mpq_has(mpq, ROWS[k].name)) continue;
			if (kind == KIND_LEAGUE && !kp_mpq_has(mpq, league)) continue;
			return kind;
		}
	}
	return 0;
}

uint32_t kp_map_class(const char *path, const uint8_t *key)
{
	char why[256];
	KpMpq *mpq;
	uint32_t kind;
	uint8_t *data;
	size_t size = 0;
	if (!key || !(mpq = kp_mpq_open(path, 0, why, sizeof why))) return 0;
	kind = kp_map_certificate(mpq);
	kp_mpq_close(mpq);
	if (kind == 0 || !(data = kp_read_file(path, &size))) return 0;
	if (!kp_map_signature_valid(path, data, size, key)) kind = 0;
	free(data);
	return kind;
}

/* --- the key --- */

int kp_map_key_from_executable(const uint8_t *exe, size_t size, uint8_t key[KP_MAP_KEY_SIZE])
{
	enum { PE_OFFSET = 0x3c, COFF = 4, SECTIONS = 2, OPTIONAL_SIZE = 16, OPTIONAL = 20, IMAGE_BASE = 28, SECTION = 40 };
	uint32_t pe, base, rva, count, k;
	size_t table;
	if (size < PE_OFFSET + 4 || exe[0] != 'M' || exe[1] != 'Z') return 0;
	pe = le32(exe + PE_OFFSET);
	if ((size_t)pe + COFF + OPTIONAL + IMAGE_BASE + 4 > size || memcmp(exe + pe, "PE\0\0", 4) != 0) return 0;
	count = le32(exe + pe + COFF + SECTIONS) & 0xffffu;
	table = (size_t)pe + COFF + OPTIONAL + (le32(exe + pe + COFF + OPTIONAL_SIZE) & 0xffffu);
	base = le32(exe + pe + COFF + OPTIONAL + IMAGE_BASE);
	rva = KP_MAP_KEY_VA - base;
	for (k = 0; k < count && table + (k + 1) * SECTION <= size; k++) {
		const uint8_t *s = exe + table + k * SECTION;
		uint32_t va = le32(s + 12), raw_size = le32(s + 16), raw = le32(s + 20);
		if (rva >= va && rva + KP_MAP_KEY_SIZE <= va + raw_size && (size_t)raw + (rva - va) + KP_MAP_KEY_SIZE <= size) {
			memcpy(key, exe + raw + (rva - va), KP_MAP_KEY_SIZE);
			return 1;
		}
	}
	return 0;
}
