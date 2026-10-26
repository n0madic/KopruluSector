/* The games' fields by name for the C suites (kp_test.h). */
#include "kp_test.h"

#include <string.h>

typedef struct {
	const char *name;
	size_t offset;
} UnitField;

#define UNIT_FIELD(name) { #name, offsetof(KpUnits, name) },
static const UnitField UNIT_FIELDS[] = { KP_UNIT_FIELDS(UNIT_FIELD) };
#undef UNIT_FIELD

int64_t *kp_t_unit_field(KpT *T, KpGame *g, const char *field)
{
	size_t k;
	for (k = 0; k < sizeof UNIT_FIELDS / sizeof UNIT_FIELDS[0]; k++) {
		if (strcmp(UNIT_FIELDS[k].name, field) == 0) return (int64_t *)((char *)&g->u + UNIT_FIELDS[k].offset);
	}
	kp_t_fail(T, "no unit field %s", field);
}

static const KpDatColumn *column(KpT *T, KpOracleGame *og, const char *table, const char *name)
{
	const KpDatColumn *c = kp_dat_column(&og->data.dat, table, name);
	if (!c) kp_t_fail(T, "no DAT column %s.%s", table, name);
	return c;
}

int64_t kp_t_dat(KpT *T, KpOracleGame *og, const char *table, const char *name, int64_t i)
{
	const KpDatColumn *c = column(T, og, table, name);
	if (i < 0 || i > 0xffffffffLL || !kp_dat_has(c, (uint32_t)i)) return KP_NIL;
	return kp_dat_value(c, (uint32_t)i);
}

void kp_t_dat_set(KpT *T, KpOracleGame *og, const char *table, const char *name, int64_t i, int64_t v)
{
	const KpDatColumn *c = column(T, og, table, name);
	int64_t limit = c->size >= 4 ? 0xffffffffLL : (1LL << (8 * c->size)) - 1;
	if (v < 0 || v > limit) kp_t_fail(T, "game: DAT column %s takes integers 0..%lld, not %lld", c->name, (long long)limit, (long long)v);
	if (i < 0 || i > 0xffffffffLL || !kp_dat_set(&og->data.dat, c, (uint32_t)i, (uint32_t)v)) {
		kp_t_fail(T, "game: DAT column %s has no entry %lld", c->name, (long long)i);
	}
}

KpScalar kp_t_scalar(KpT *T, const char *name)
{
	int k;
	for (k = 0; k < KP_SCALAR_COUNT; k++) {
		if (strcmp(KP_SCALARS[k].name, name) == 0) return (KpScalar)k;
	}
	kp_t_fail(T, "no scalar %s", name);
}

int kp_t_list_units(KpGame *g, int list, int64_t *out, int max)
{
	int count = 0;
	int64_t n = g->lists[list].head;
	while (n != 0 && count < max) {
		out[count++] = n;
		n = g->u.next[n];
	}
	return count;
}
