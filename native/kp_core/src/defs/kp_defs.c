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
