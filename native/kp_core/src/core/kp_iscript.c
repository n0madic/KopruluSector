/* iscript.kpd loader (kp_iscript.h). */
#include "kp_iscript.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail(KpError *err, const char *msg)
{
	snprintf(err->msg, sizeof err->msg, "iscript: %s", msg);
	return -1;
}

/* The header types' animation slots (SLOT: u8 count; per type u8 type, u8 slots). */
static int set_slots(KpIScript *s, const uint8_t *sl, uint32_t sl_size, KpError *err)
{
	uint32_t i, n;
	for (i = 0; i < 256; i++) s->slots[i] = -1;
	if (sl_size < 1 || sl_size < 1 + 2 * (uint32_t)sl[0]) return fail(err, "SLOT section is malformed");
	n = sl[0];
	for (i = 0; i < n; i++) s->slots[sl[1 + 2 * i]] = sl[2 + 2 * i];
	return 0;
}

int kp_iscript_load(KpIScript *s, const uint8_t *data, size_t size, const char *name, const KpKpfExpect *expect,
	KpError *err)
{
	KpKpf c;
	KpKpfExpect want;
	const KpKpfSection *h, *code;
	const uint8_t *sl;
	uint32_t count, i, sl_size;
	memset(s, 0, sizeof *s);
	s->blob = (uint8_t *)malloc(size ? size : 1);
	if (!s->blob) return fail(err, "out of memory");
	memcpy(s->blob, data, size);
	s->size = size;
	if (kp_kpf_parse(&c, s->blob, size, name, 0, err) != 0) {
		kp_iscript_free(s);
		return -1;
	}
	memset(&want, 0, sizeof want);
	if (expect) want = *expect;
	want.kind = KP_ISCRIPT_KIND;
	want.schema_version = KP_ISCRIPT_SCHEMA;
	if (kp_kpf_check(&c, &want, err) != 0 || !(h = kp_kpf_require(&c, "HDRS", err))
		|| !(sl = kp_kpf_table(&c, "SLOT", &sl_size, err)) || !(code = kp_kpf_require(&c, "CODE", err)))
		goto failed;
	s->code = code->data;
	s->code_size = code->size;
	if (h->size < 2 || (uint64_t)h->size < 2 + 4 * (uint64_t)kp_le_u16(h->data)) {
		fail(err, "HDRS section is malformed");
		goto failed;
	}
	count = kp_le_u16(h->data);
	for (i = 0; i < count; i++) {
		uint32_t id = kp_le_u16(h->data + 2 + 4 * i);
		if (id + 1 > s->id_count) s->id_count = id + 1;
	}
	s->headers = (int32_t *)malloc((s->id_count ? s->id_count : 1) * sizeof *s->headers);
	if (!s->headers) {
		fail(err, "out of memory");
		goto failed;
	}
	for (i = 0; i < s->id_count; i++) s->headers[i] = KP_ISCRIPT_NO_HEADER;
	for (i = 0; i < count; i++) {
		s->headers[kp_le_u16(h->data + 2 + 4 * i)] = (int32_t)kp_le_u16(h->data + 4 + 4 * i);
	}
	if (set_slots(s, sl, sl_size, err) != 0) goto failed;
	kp_kpf_free(&c);
	return 0;
failed:
	kp_kpf_free(&c);
	kp_iscript_free(s);
	return -1;
}

void kp_iscript_free(KpIScript *s)
{
	free(s->headers);
	free(s->blob);
	memset(s, 0, sizeof *s);
}

int kp_iscript_from_code(KpIScript *s, const uint8_t *code, size_t size, uint32_t id_count, KpError *err)
{
	uint32_t i;
	memset(s, 0, sizeof *s);
	s->blob = (uint8_t *)malloc(size ? size : 1);
	s->headers = (int32_t *)malloc((id_count ? id_count : 1) * sizeof *s->headers);
	if (!s->blob || !s->headers) {
		kp_iscript_free(s);
		return fail(err, "out of memory");
	}
	memcpy(s->blob, code, size);
	s->size = size;
	s->code = s->blob;
	s->code_size = (uint32_t)size;
	s->id_count = id_count;
	for (i = 0; i < id_count; i++) s->headers[i] = KP_ISCRIPT_NO_HEADER;
	for (i = 0; i < 256; i++) s->slots[i] = -1;
	return 0;
}

#define ISCRIPT_FILE "scripts\\iscript.bin"
#define ISCRIPT_END 0xffffu
#define ISCRIPT_HEADER_TAG "SCPE"

int kp_iscript_from_files(KpIScript *s, const KpFiles *f, KpError *err)
{
	const KpDefsTable *slot = kp_defs_table(f->profile, "SLOT");
	size_t size;
	uint8_t *bin;
	uint32_t pos, id_count = 0, id;
	memset(s, 0, sizeof *s);
	if (!slot) return fail(err, "no SLOT table compiled for the profile");
	if (!(bin = kp_files_need(f, ISCRIPT_FILE, &size, err))) return -1;
	/* scripts.iscript_headers: the id index from the DWORD at 0, {u16 id, u16 header offset} up to the id 0xffff, each
	 * header starting with SCPE; an id's first header (the native search stops at the first match) */
	for (pos = size >= 4 ? kp_le_u32(bin) : 0;; pos += 4) {
		uint32_t offset;
		if (size < 4 || (uint64_t)pos + 4 > size) {
			free(bin);
			return fail(err, "iscript.bin's id index runs past the end without the 0xffff terminator");
		}
		id = kp_le_u16(bin + pos);
		offset = kp_le_u16(bin + pos + 2);
		if (id == ISCRIPT_END) break;
		if ((uint64_t)offset + 4 > size || memcmp(bin + offset, ISCRIPT_HEADER_TAG, 4) != 0) {
			free(bin);
			return fail(err, "an iscript.bin header does not start with SCPE");
		}
		if (id + 1 > id_count) id_count = id + 1;
	}
	if (kp_iscript_from_code(s, bin, size, id_count, err) != 0) {
		free(bin);
		return -1;
	}
	for (pos = kp_le_u32(bin);; pos += 4) {
		if ((id = kp_le_u16(bin + pos)) == ISCRIPT_END) break;
		if (s->headers[id] == KP_ISCRIPT_NO_HEADER) s->headers[id] = (int32_t)kp_le_u16(bin + pos + 2);
	}
	free(bin);
	if (set_slots(s, slot->data, (uint32_t)slot->size, err) != 0) {
		kp_iscript_free(s);
		return -1;
	}
	return 0;
}
