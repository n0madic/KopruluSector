/* The games' fields by name for the C suites (kp_test.h). */
#include "kp_test.h"

#include <string.h>

typedef struct {
	const char *name;
	size_t offset, elem;
	int sign;
} UnitField;

#define UNIT_FIELD(name, type) { #name, offsetof(KpUnits, name), sizeof(type), (type)-1 < 0 },
static const UnitField UNIT_FIELDS[] = { KP_UNIT_FIELDS(UNIT_FIELD) };
#undef UNIT_FIELD

static const UnitField *unit_field(KpT *T, const char *field)
{
	size_t k;
	for (k = 0; k < sizeof UNIT_FIELDS / sizeof UNIT_FIELDS[0]; k++) {
		if (strcmp(UNIT_FIELDS[k].name, field) == 0) return &UNIT_FIELDS[k];
	}
	kp_t_fail(T, "no unit field %s", field);
}

int64_t kp_t_unit_get(KpT *T, KpGame *g, const char *field, int64_t n)
{
	const UnitField *f = unit_field(T, field);
	if (n < 0 || n > KP_FLINGIES) kp_t_fail(T, "unit field %s: record %lld", field, (long long)n);
	return kp_num_get((const char *)&g->u + f->offset, f->elem, f->sign, n);
}

void kp_t_unit_set(KpT *T, KpGame *g, const char *field, int64_t n, int64_t v)
{
	const UnitField *f = unit_field(T, field);
	char *at = (char *)&g->u + f->offset + (size_t)n * f->elem;
	int8_t i8 = (int8_t)v;
	int16_t i16 = (int16_t)v;
	int32_t i32 = (int32_t)v;
	uint8_t u8 = (uint8_t)v;
	uint16_t u16 = (uint16_t)v;
	uint32_t u32 = (uint32_t)v;
	if (n < 0 || n > KP_FLINGIES) kp_t_fail(T, "unit field %s: record %lld", field, (long long)n);
	switch (f->elem * 2 + (size_t)f->sign) {
	case 3: memcpy(at, &i8, 1); break;
	case 2: memcpy(at, &u8, 1); break;
	case 5: memcpy(at, &i16, 2); break;
	case 4: memcpy(at, &u16, 2); break;
	case 9: memcpy(at, &i32, 4); break;
	case 8: memcpy(at, &u32, 4); break;
	default: memcpy(at, &v, 8); return;
	}
	if (kp_t_unit_get(T, g, field, n) != v) kp_t_fail(T, "unit field %s holds no %lld", field, (long long)v);
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
