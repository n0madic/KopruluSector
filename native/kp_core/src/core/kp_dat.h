/*
 * dat.kpd (docs/formats/kpd.md): the DAT columns of StarCraft 1.17. The loader copies the
 * file; columns point into the copy.
 *
 * kp_dat_value(column, i) is the raw unsigned value of flat index i (id * per_id + k); a
 * column covering an id subrange (units.infestation from 106) has no values outside it.
 */
#ifndef KP_DAT_H
#define KP_DAT_H

#include <stddef.h>
#include <stdint.h>

#include "kp_kpf.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KP_DAT_KIND "DAT "
#define KP_DAT_SCHEMA 1

typedef struct {
	const char *table; /* units, weapons, ... (kp_dat_table_name) */
	char name[64];
	uint8_t size, per_id;
	uint16_t count, first_id;
	const uint8_t *data;
} KpDatColumn;

typedef struct {
	const char *name; /* orders, research, tech_use, upgrades, units */
	const uint8_t *words;
	uint32_t count;
} KpDatRequirements;

typedef struct {
	const char *name;
	uint16_t records;
} KpDatTable;

#define KP_DAT_MAX_TABLES 16
#define KP_DAT_REQUIREMENTS 5

typedef struct {
	uint8_t *blob; /* owned copy of the file */
	size_t size;
	int table_count;
	KpDatTable tables[KP_DAT_MAX_TABLES];
	int column_count;
	KpDatColumn *columns; /* owned */
	KpDatRequirements requirements[KP_DAT_REQUIREMENTS]; /* words NULL when absent */
} KpDat;

/* expect may be NULL; it adds profile and source-hash checks. Returns -1 with err set. */
int kp_dat_load(KpDat *dat, const uint8_t *data, size_t size, const char *name, const KpKpfExpect *expect, KpError *err);
void kp_dat_free(KpDat *dat);

const KpDatColumn *kp_dat_column(const KpDat *dat, const char *table, const char *column);

/* A DAT built column by column instead of loaded (the C suites build one, tests/c/test_data.c):
 * `bytes` of zeroed value storage and room for column_count descriptors, which the caller
 * fills in order with each column's data inside blob. kp_dat_table_name gives the static
 * name of a table ("units", ...) for KpDatColumn.table, or NULL for an unknown one. */
int kp_dat_alloc(KpDat *dat, size_t bytes, int column_count, KpError *err);
const char *kp_dat_table_name(const char *name);

/* Stores `value` (truncated to the element size) at flat index i of a column of the loader's
 * copy; returns 0 when the column has no such index. Test harnesses use it to patch a table,
 * as the original-code probes patch the loaded DAT in memory. */
int kp_dat_set(KpDat *dat, const KpDatColumn *c, uint32_t i, uint32_t value);

static inline int kp_dat_has(const KpDatColumn *c, uint32_t i)
{
	uint32_t base = (uint32_t)c->first_id * c->per_id;
	return i >= base && i - base < c->count;
}

static inline uint32_t kp_dat_value(const KpDatColumn *c, uint32_t i)
{
	const uint8_t *p;
	if (!kp_dat_has(c, i)) return 0;
	p = c->data + (size_t)(i - (uint32_t)c->first_id * c->per_id) * c->size;
	if (c->size == 1) return kp_le_u8(p);
	if (c->size == 2) return kp_le_u16(p);
	return kp_le_u32(p);
}

#ifdef __cplusplus
}
#endif

#endif
