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
