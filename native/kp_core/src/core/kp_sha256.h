/* SHA-256 (FIPS 180-4): the digests of the input manifest (defs/scr/inputs-<profile>.json) that the original data's
 * files are checked against when the game reads them (ADR 0018). */
#ifndef KP_SHA256_H
#define KP_SHA256_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KP_SHA256_SIZE 32

typedef struct {
	uint32_t h[8];
	uint64_t bytes;      /* the message's length so far */
	uint8_t block[64];
	size_t fill;         /* bytes of block in use */
} KpSha256;

void kp_sha256_init(KpSha256 *s);
void kp_sha256_update(KpSha256 *s, const void *data, size_t size);
void kp_sha256_final(KpSha256 *s, uint8_t digest[KP_SHA256_SIZE]);
/* The digest of data[0..size-1] at once. */
void kp_sha256(const void *data, size_t size, uint8_t digest[KP_SHA256_SIZE]);
/* The digest as 64 lower-case hexadecimal digits and a NUL. */
void kp_sha256_hex(const uint8_t digest[KP_SHA256_SIZE], char hex[2 * KP_SHA256_SIZE + 1]);
/* kp_sha256_hex's reverse for any n bytes: out[0..n-1] from 2n hexadecimal digits of either case (more may follow).
 * 0, or -1 when a digit is not one. kp_hex_decode(hex, d, KP_SHA256_SIZE) reads a manifest's or a listing's digest. */
int kp_hex_decode(const char *hex, uint8_t *out, size_t n);

#ifdef __cplusplus
}
#endif

#endif
