/*
 * A map's class (the map list entry's +0x260, LoadMapEntryInfo 0x45d870): OpenMapArchiveWithCondition 0x469560's.
 * ScanMapCertificates 0x4c1350 with ProbeConditionFile 0x469650 finds the first condition file ("certificate") the
 * map's archive holds, by kind from 6 down; VerifyMapSignature 0x46b0d0 keeps its kind only when the map file's strong
 * signature is valid (VerifyNgisSignature 0x49d460: an "NGIS" tag and 256 bytes after the archive, RSA-2048 with
 * e = 65537 over SHA-1 of the file before the tag and its upper-cased name). Only the Ladder, Tournament and Server
 * templates read the class (ValidateGameTemplate 0x45b700), and Battle.net's Create Game's map icons (0x44d190).
 *
 * The public key is Blizzard's: the 1.17 executable's 256 bytes at KP_MAP_KEY_VA, read from the player's install at
 * run time when a template first asks a map's class (kp_data_map_key), never committed. Without it (a repack or Remastered alone: the web, the phones; TODO in
 * kp_data.c) every map's class is 0, an ordinary map's.
 */
#ifndef KP_MAP_CLASS_H
#define KP_MAP_CLASS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KP_MAP_KEY_SIZE 256         /* the modulus's bytes, the least significant first (SBigFromBinary) */
#define KP_MAP_KEY_EXPONENT 65537u  /* 0x4eed90 */
#define KP_MAP_KEY_VA 0x4eec90u     /* the modulus in the 1.17 executable (its profile's SHA-256) */
#define KP_SHA1_SIZE 20

struct KpMpq;

/* SHA-1 (FIPS 180-1), the digest's bytes big-endian as the engine's 0x4beaa0 writes them */
void kp_sha1(const void *data, size_t size, const void *more, size_t more_size, uint8_t digest[KP_SHA1_SIZE]);

/* The modulus at KP_MAP_KEY_VA of a PE executable's bytes into key: 1, or 0 when no section holds it (any bytes: a
 * field that points past the file or wraps refuses it). */
int kp_map_key_from_executable(const uint8_t *exe, size_t size, uint8_t key[KP_MAP_KEY_SIZE]);

/* VerifyNgisSignature 0x49d460: whether data (a map file's bytes) ends in a strong signature valid under key for the
 * file's name: its path's last component (UTF-8) in the ANSI form the original hashes, windows-1252 when it has one,
 * upper-cased by SStrUpper's ASCII. */
int kp_map_signature_valid(const char *name, const uint8_t *data, size_t size, const uint8_t key[KP_MAP_KEY_SIZE]);

/* ScanMapCertificates 0x4c1350 with ProbeConditionFile 0x469650: the kind of the first certificate the archive holds,
 * 0 none (kind 5 also needs League_<n>.cnd, n the server templates' sub-byte 0x66fd3c: 0 offline). */
uint32_t kp_map_certificate(struct KpMpq *mpq);

/* OpenMapArchiveWithCondition 0x469560's class of the map file at path: its certificate's kind when its signature is
 * valid under key, else 0 (key NULL: 0; a file that does not open as an archive: 0). */
uint32_t kp_map_class(const char *path, const uint8_t *key);

#ifdef __cplusplus
}
#endif

#endif
