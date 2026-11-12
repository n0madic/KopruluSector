/* The lookups of the generated defs tables (kp_defs.h, tools/defs_c.py). */
#include "kp_defs.h"

#include <string.h>

const KpDefsInput *kp_defs_input(const KpDefsProfile *p, const char *normalized)
{
	int lo = 0, hi = p->file_count;
	while (lo < hi) {
		int mid = lo + (hi - lo) / 2, c = strcmp(p->files[mid].name, normalized);
		if (c == 0) return &p->files[mid];
		if (c < 0) lo = mid + 1;
		else hi = mid;
	}
	return NULL;
}

const KpDefsProfile *kp_defs_profile(const char *id)
{
	static const KpDefsProfile *const PROFILES[] = KP_DEFS_PROFILES;
	size_t k;
	for (k = 0; id && k < sizeof PROFILES / sizeof PROFILES[0]; k++)
		if (strcmp(PROFILES[k]->id, id) == 0) return PROFILES[k];
	return NULL;
}

const KpDefsTable *kp_defs_table(const KpDefsProfile *p, const char *fourcc)
{
	int lo = 0, hi = p->table_count;
	while (lo < hi) {
		int mid = lo + (hi - lo) / 2, c = strcmp(p->tables[mid].fourcc, fourcc);
		if (c == 0) return &p->tables[mid];
		if (c < 0) lo = mid + 1;
		else hi = mid;
	}
	return NULL;
}

int kp_defs_listed(const char *const *names, int count, const char *normalized)
{
	int lo = 0, hi = count;
	while (lo < hi) {
		int mid = lo + (hi - lo) / 2, c = strcmp(names[mid], normalized);
		if (c == 0) return 1;
		if (c < 0) lo = mid + 1;
		else hi = mid;
	}
	return 0;
}

int kp_defs_names_count(const KpDefsTable *t)
{
	size_t at = 2;
	int n, k;
	if (!t || t->size < 2) return -1;
	n = (int)(t->data[0] | t->data[1] << 8);
	for (k = 0; k < n; k++) {
		if (at + 1 > t->size || at + 1 + t->data[at] > t->size) return -1;
		at += 1 + (size_t)t->data[at];
	}
	return n;
}

const uint8_t *kp_defs_name_at(const KpDefsTable *t, int index, size_t *size)
{
	size_t at = 2;
	int k;
	for (k = 0; k < index; k++) at += 1 + (size_t)t->data[at];
	*size = t->data[at];
	return t->data + at + 1;
}

int kp_defs_names_has(const KpDefsProfile *p, const char *fourcc, const char *name)
{
	const KpDefsTable *t = kp_defs_table(p, fourcc);
	size_t want = strlen(name), size;
	int n = kp_defs_names_count(t), k;
	for (k = 0; k < n; k++) {
		const uint8_t *at = kp_defs_name_at(t, k, &size);
		if (size == want && memcmp(at, name, size) == 0) return 1;
	}
	return 0;
}
