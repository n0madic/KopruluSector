/*
 * Reader of the Koprulu container (.kpd/.kpm; docs/formats/container.md), the C counterpart
 * of game/data/kpf.lua. A parsed container points into the caller's bytes, which must outlive
 * it. Errors are reported as "kpf: NAME: ..." messages in a KpError, worded as the Lua reader's.
 */
#ifndef KP_KPF_H
#define KP_KPF_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KP_KPF_MAGIC "KPRF"
#define KP_KPF_VERSION 1
#define KP_KPF_HEADER_SIZE 128
#define KP_KPF_ENTRY_SIZE 48
#define KP_KPF_CANONICAL 0
#define KP_KPF_DERIVED 1
#define KP_ERROR_SIZE 256

typedef struct {
	char msg[KP_ERROR_SIZE];
} KpError;

typedef struct {
	char fourcc[5];
	uint8_t class_;
	uint16_t flags;
	uint32_t offset, size, schema_version, crc32;
	char algorithm_id[5];
	uint32_t algorithm_version;
	uint8_t inputs_hash[16];
	const uint8_t *data; /* the payload, size bytes */
} KpKpfSection;

typedef struct {
	const uint8_t *data;
	size_t size;
	char name[64];
	char kind[5];
	uint32_t schema_version, converter_version;
	char profile_id[17];
	uint8_t source_hash[32], provenance_hash[32];
	int section_count;
	KpKpfSection *sections; /* owned */
} KpKpf;

/* Expectations of the running code (Container:check). Zero or NULL fields are not checked. */
typedef struct {
	const char *fourcc;
	uint32_t schema_version;
	const char *algorithm_id; /* derived sections */
	uint32_t algorithm_version;
} KpKpfSectionExpect;

typedef struct {
	const char *kind;
	uint32_t schema_version;
	const char *profile_id;
	const uint8_t *source_hash; /* 32 bytes */
	const KpKpfSectionExpect *sections;
	int section_count;
} KpKpfExpect;

uint32_t kp_crc32(const uint8_t *data, size_t size);
/* zlib's crc32(crc, data, size): continues the CRC-32 of earlier bytes. */
uint32_t kp_crc32_update(uint32_t crc, const uint8_t *data, size_t size);

/* Parses `data`; on failure returns -1 with the message in err. verify_crc checks every
 * section checksum. kp_kpf_free releases the section table. */
int kp_kpf_parse(KpKpf *c, const uint8_t *data, size_t size, const char *name, int verify_crc, KpError *err);
void kp_kpf_free(KpKpf *c);

int kp_kpf_check(const KpKpf *c, const KpKpfExpect *expect, KpError *err);

/* The section `fourcc`, or NULL (kp_kpf_require sets the "missing section" error). */
const KpKpfSection *kp_kpf_find(const KpKpf *c, const char *fourcc);
const KpKpfSection *kp_kpf_require(const KpKpf *c, const char *fourcc, KpError *err);

/* Little-endian reads; the caller has checked the bounds. */
static inline uint32_t kp_le_u8(const uint8_t *p) { return p[0]; }
static inline uint32_t kp_le_u16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static inline uint32_t kp_le_u32(const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

#ifdef __cplusplus
}
#endif

#endif
