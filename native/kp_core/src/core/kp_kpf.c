/* Koprulu container reader (kp_kpf.h). */
#include "kp_kpf.h"
#include "kp_crc32_table.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ARMv8's CRC32 instructions compute this CRC-32 (IEEE, reflected 0xedb88320) where the compiler targets them (Apple
 * silicon; -march=armv8-a+crc elsewhere): a little-endian load of 8 bytes is the 8 bytes in order. */
#if defined(__ARM_FEATURE_CRC32) && defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#include <arm_acle.h>
#define KP_CRC32_ARM 1
#endif

#define REGENERATE "regenerate the data with the converter"

uint32_t kp_crc32(const uint8_t *data, size_t size) { return kp_crc32_update(0, data, size); }

/* CRC-32 (IEEE) by the CPU's instructions (KP_CRC32_ARM), else by slicing-by-8 over the constant tables of
 * kp_crc32_table.h; the same values either way. */
uint32_t kp_crc32_update(uint32_t crc, const uint8_t *data, size_t size)
{
#ifdef KP_CRC32_ARM
	uint32_t c = crc ^ 0xffffffffu;
	for (; size >= 8; data += 8, size -= 8) {
		uint64_t v;
		memcpy(&v, data, 8);
		c = __crc32d(c, v);
	}
	while (size-- > 0) c = __crc32b(c, *data++);
	return c ^ 0xffffffffu;
#else
	const uint32_t(*t)[256] = KP_CRC32_TABLE;
	uint32_t c = crc ^ 0xffffffffu;
	while (size >= 8) {
		uint32_t lo = c ^ ((uint32_t)data[0] | (uint32_t)data[1] << 8 | (uint32_t)data[2] << 16 | (uint32_t)data[3] << 24);
		c = t[7][lo & 0xffu] ^ t[6][(lo >> 8) & 0xffu] ^ t[5][(lo >> 16) & 0xffu] ^ t[4][lo >> 24] ^ t[3][data[4]]
			^ t[2][data[5]] ^ t[1][data[6]] ^ t[0][data[7]];
		data += 8;
		size -= 8;
	}
	while (size-- > 0) c = t[0][(c ^ *data++) & 0xffu] ^ (c >> 8);
	return c ^ 0xffffffffu;
#endif
}

static int fail(KpError *err, const char *name, const char *fmt, ...)
{
	char text[KP_ERROR_SIZE];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(text, sizeof text, fmt, ap);
	va_end(ap);
	snprintf(err->msg, sizeof err->msg, "kpf: %s: %s", name && name[0] ? name : "<data>", text);
	return -1;
}

static void copy_tag(char *to, const uint8_t *from)
{
	memcpy(to, from, 4);
	to[4] = '\0';
}

/* Parses the container whose first `size` of file_size bytes are in `data`; a section past them has no data. */
static int parse(KpKpf *c, const uint8_t *data, size_t available, size_t size, const char *name, int verify_crc,
	KpError *err)
{
	uint32_t version, header_size, entry_size, count, table_offset, file_size;
	int i;
	memset(c, 0, sizeof *c);
	snprintf(c->name, sizeof c->name, "%s", name ? name : "");
	if (available < KP_KPF_HEADER_SIZE) return fail(err, name, "file shorter than the container header");
	if (memcmp(data, KP_KPF_MAGIC, 4) != 0) return fail(err, name, "not a Koprulu container (bad magic)");
	version = kp_le_u16(data + 8);
	header_size = kp_le_u16(data + 10);
	entry_size = kp_le_u16(data + 12);
	if (version != KP_KPF_VERSION || header_size != KP_KPF_HEADER_SIZE || entry_size != KP_KPF_ENTRY_SIZE) {
		return fail(err, name, "container version %u is not supported by this build (expects %d); %s",
			(unsigned)version, KP_KPF_VERSION, REGENERATE);
	}
	count = kp_le_u16(data + 14);
	table_offset = kp_le_u32(data + 104);
	file_size = kp_le_u32(data + 108);
	if (file_size != size) {
		return fail(err, name, "file is %lu bytes, header says %u (truncated or corrupt)", (unsigned long)size,
			(unsigned)file_size);
	}
	if ((uint64_t)table_offset + (uint64_t)count * KP_KPF_ENTRY_SIZE > available) {
		return fail(err, name, available < size ? "section table past the bytes read" : "section table out of bounds");
	}
	c->data = data;
	c->size = size;
	copy_tag(c->kind, data + 4);
	c->schema_version = kp_le_u32(data + 16);
	c->converter_version = kp_le_u32(data + 20);
	memcpy(c->profile_id, data + 24, 16);
	c->profile_id[16] = '\0';
	memcpy(c->source_hash, data + 40, 32);
	memcpy(c->provenance_hash, data + 72, 32);
	c->sections = (KpKpfSection *)calloc(count ? count : 1, sizeof *c->sections);
	if (!c->sections) return fail(err, name, "out of memory");
	for (i = 0; i < (int)count; i++) {
		const uint8_t *e = data + table_offset + (size_t)i * KP_KPF_ENTRY_SIZE;
		KpKpfSection *s = &c->sections[i];
		copy_tag(s->fourcc, e);
		s->class_ = (uint8_t)kp_le_u8(e + 4);
		s->flags = (uint16_t)kp_le_u16(e + 6);
		s->offset = kp_le_u32(e + 8);
		s->size = kp_le_u32(e + 12);
		s->schema_version = kp_le_u32(e + 16);
		s->crc32 = kp_le_u32(e + 20);
		copy_tag(s->algorithm_id, e + 24);
		s->algorithm_version = kp_le_u32(e + 28);
		memcpy(s->inputs_hash, e + 32, 16);
		if (s->offset % 4 != 0 || (uint64_t)s->offset + s->size > size) {
			char fourcc[5];
			memcpy(fourcc, s->fourcc, 5);
			kp_kpf_free(c);
			return fail(err, name, "section %s out of bounds or misaligned", fourcc);
		}
		if (kp_kpf_find(c, s->fourcc)) { /* the entries before this one */
			char fourcc[5];
			memcpy(fourcc, s->fourcc, 5);
			kp_kpf_free(c);
			return fail(err, name, "duplicate section %s", fourcc);
		}
		s->data = (uint64_t)s->offset + s->size <= available ? data + s->offset : NULL;
		if (verify_crc && s->data && kp_crc32(s->data, s->size) != s->crc32) {
			char fourcc[5];
			memcpy(fourcc, s->fourcc, 5);
			kp_kpf_free(c);
			return fail(err, name, "section %s checksum mismatch (corrupt file)", fourcc);
		}
		c->section_count = i + 1;
	}
	return 0;
}

int kp_kpf_parse(KpKpf *c, const uint8_t *data, size_t size, const char *name, int verify_crc, KpError *err)
{
	return parse(c, data, size, size, name, verify_crc, err);
}

int kp_kpf_parse_prefix(KpKpf *c, const uint8_t *data, size_t available, size_t file_size, const char *name,
	KpError *err)
{
	if (available > file_size) return fail(err, name, "more bytes read than the file has");
	return parse(c, data, available, file_size, name, 0, err);
}

void kp_kpf_free(KpKpf *c)
{
	free(c->sections);
	c->sections = NULL;
	c->section_count = 0;
}

const KpKpfSection *kp_kpf_find(const KpKpf *c, const char *fourcc)
{
	int i;
	for (i = 0; i < c->section_count; i++) {
		if (memcmp(c->sections[i].fourcc, fourcc, 4) == 0) return &c->sections[i];
	}
	return NULL;
}

const KpKpfSection *kp_kpf_require(const KpKpf *c, const char *fourcc, KpError *err)
{
	const KpKpfSection *s = kp_kpf_find(c, fourcc);
	if (!s) fail(err, c->name, "missing section %s; %s", fourcc, REGENERATE);
	return s;
}

const uint8_t *kp_kpf_section(const uint8_t *data, size_t size, const char *fourcc, size_t *n)
{
	KpKpf c;
	KpError err;
	const KpKpfSection *s;
	const uint8_t *p = NULL;
	*n = 0;
	if (!data || kp_kpf_parse(&c, data, size, "data", 0, &err) != 0) return NULL;
	if ((s = kp_kpf_find(&c, fourcc))) {
		p = s->data;
		*n = s->size;
	}
	kp_kpf_free(&c);
	return p;
}

int kp_kpf_check(const KpKpf *c, const KpKpfExpect *expect, KpError *err)
{
	int i;
	if (expect->kind && memcmp(c->kind, expect->kind, 4) != 0) {
		return fail(err, c->name, "file kind \"%s\", expected \"%s\"", c->kind, expect->kind);
	}
	if (expect->schema_version && c->schema_version != expect->schema_version) {
		return fail(err, c->name, "schema version %u, this build expects %u; %s", (unsigned)c->schema_version,
			(unsigned)expect->schema_version, REGENERATE);
	}
	if (expect->profile_id && strcmp(c->profile_id, expect->profile_id) != 0) {
		return fail(err, c->name, "data profile \"%s\", this build expects \"%s\"", c->profile_id, expect->profile_id);
	}
	if (expect->source_hash && memcmp(c->source_hash, expect->source_hash, 32) != 0) {
		return fail(err, c->name, "built from a different data set than the other loaded files; %s", REGENERATE);
	}
	for (i = 0; i < expect->section_count; i++) {
		const KpKpfSectionExpect *want = &expect->sections[i];
		const KpKpfSection *s = kp_kpf_require(c, want->fourcc, err);
		if (!s) return -1;
		if (want->schema_version && s->schema_version != want->schema_version) {
			return fail(err, c->name, "section %s schema version %u, expected %u; %s", want->fourcc,
				(unsigned)s->schema_version, (unsigned)want->schema_version, REGENERATE);
		}
		if (want->algorithm_id) {
			if (s->class_ != KP_KPF_DERIVED) return fail(err, c->name, "section %s is not derived", want->fourcc);
			if (memcmp(s->algorithm_id, want->algorithm_id, 4) != 0 || s->algorithm_version != want->algorithm_version) {
				return fail(err, c->name, "derived section %s was built by %s v%u, this build expects %s v%u; %s",
					want->fourcc, s->algorithm_id, (unsigned)s->algorithm_version, want->algorithm_id,
					(unsigned)want->algorithm_version, REGENERATE);
			}
		}
	}
	return 0;
}

int kp_kpf_layout(const char *kind, uint32_t schema_version, uint32_t converter_version, const char *profile_id,
	const uint8_t *source_hash, const KpKpfOut *sections, int count, uint8_t **out, size_t *size, size_t *offsets)
{
	size_t cursor = KP_KPF_HEADER_SIZE + (size_t)KP_KPF_ENTRY_SIZE * (size_t)count, total;
	uint8_t *b;
	int k;
	*out = NULL;
	*size = 0;
	if (strlen(kind) != 4 || strlen(profile_id) > 16 || count < 0 || count > 0xffff) return -1;
	for (k = 0; k < count; k++) {
		cursor += (4 - cursor % 4) % 4;
		offsets[k] = cursor;
		cursor += sections[k].size;
	}
	total = cursor + (4 - cursor % 4) % 4;
	if (total > UINT32_MAX || !(b = (uint8_t *)calloc(total, 1))) return -1;
	memcpy(b, KP_KPF_MAGIC, 4);
	memcpy(b + 4, kind, 4);
	kp_le_put16(b + 8, KP_KPF_VERSION);
	kp_le_put16(b + 10, KP_KPF_HEADER_SIZE);
	kp_le_put16(b + 12, KP_KPF_ENTRY_SIZE);
	kp_le_put16(b + 14, (uint32_t)count);
	kp_le_put32(b + 16, schema_version);
	kp_le_put32(b + 20, converter_version);
	memcpy(b + 24, profile_id, strlen(profile_id));
	if (source_hash) memcpy(b + 40, source_hash, 32);
	kp_le_put32(b + 104, KP_KPF_HEADER_SIZE);
	kp_le_put32(b + 108, (uint32_t)total);
	for (k = 0; k < count; k++) {
		uint8_t *e = b + KP_KPF_HEADER_SIZE + (size_t)KP_KPF_ENTRY_SIZE * (size_t)k;
		memcpy(e, sections[k].fourcc, 4);
		e[4] = KP_KPF_CANONICAL;
		kp_le_put32(e + 8, (uint32_t)offsets[k]);
		kp_le_put32(e + 12, (uint32_t)sections[k].size);
		kp_le_put32(e + 16, sections[k].schema_version ? sections[k].schema_version : 1);
	}
	*out = b;
	*size = total;
	return 0;
}

void kp_kpf_seal(uint8_t *data)
{
	uint32_t k, count = kp_le_u16(data + 14);
	for (k = 0; k < count; k++) {
		uint8_t *e = data + KP_KPF_HEADER_SIZE + (size_t)KP_KPF_ENTRY_SIZE * k;
		kp_le_put32(e + 20, kp_crc32(data + kp_le_u32(e + 8), kp_le_u32(e + 12)));
	}
}

int kp_kpf_build(const char *kind, uint32_t schema_version, uint32_t converter_version, const char *profile_id,
	const uint8_t *source_hash, const KpKpfOut *sections, int count, uint8_t **out, size_t *size)
{
	size_t *offsets = (size_t *)malloc(sizeof *offsets * (size_t)(count > 0 ? count : 1));
	int k, rc;
	*out = NULL;
	*size = 0;
	if (!offsets) return -1;
	rc = kp_kpf_layout(kind, schema_version, converter_version, profile_id, source_hash, sections, count, out, size, offsets);
	for (k = 0; rc == 0 && k < count; k++) {
		if (sections[k].size) memcpy(*out + offsets[k], sections[k].payload, sections[k].size);
	}
	if (rc == 0) kp_kpf_seal(*out);
	free(offsets);
	return rc;
}
