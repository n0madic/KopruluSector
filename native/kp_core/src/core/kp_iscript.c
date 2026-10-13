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

int kp_iscript_load(KpIScript *s, const uint8_t *data, size_t size, const char *name, const KpKpfExpect *expect,
	KpError *err)
{
	KpKpf c;
	KpKpfExpect want;
	const KpKpfSection *h, *sl, *code;
	uint32_t count, i, n;
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
		|| !(sl = kp_kpf_require(&c, "SLOT", err)) || !(code = kp_kpf_require(&c, "CODE", err)))
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
	for (i = 0; i < 256; i++) s->slots[i] = -1;
	if (sl->size < 1 || sl->size < 1 + 2 * (uint32_t)sl->data[0]) {
		fail(err, "SLOT section is malformed");
		goto failed;
	}
	n = sl->data[0];
	for (i = 0; i < n; i++) s->slots[sl->data[1 + 2 * i]] = sl->data[2 + 2 * i];
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
