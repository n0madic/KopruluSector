/*
 * What kp_present's readers of containers share (kp_present_internal.h): an error's text, the bytes a container's
 * sections end at. Their own file: kp_hd.c's need of them links nothing else of the library into kp_hd_phone, whose
 * SHA-256 stamps the phones' transcoding caches (tools/product.sh).
 */
#include "kp_present_internal.h"

#include <stdio.h>

int kp_present_fail(KpError *err, const char *name, const char *msg)
{
	snprintf(err->msg, sizeof err->msg, "%s: %s", name, msg);
	return -1;
}

size_t kp_present_head_size(const uint8_t *first, size_t n, size_t file_size, const char *const *names, int count)
{
	KpKpf c;
	KpError e;
	size_t end = 0;
	int k;
	if (kp_kpf_parse_prefix(&c, first, n, file_size, "", &e) != 0) return 0;
	for (k = 0; k < count; k++) {
		const KpKpfSection *s = kp_kpf_find(&c, names[k]);
		if (!s) {
			end = 0;
			break;
		}
		if ((size_t)s->offset + s->size > end) end = (size_t)s->offset + s->size;
	}
	kp_kpf_free(&c);
	return end;
}
