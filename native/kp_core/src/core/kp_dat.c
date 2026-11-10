/* dat.kpd loader (kp_dat.h). */
#include "kp_dat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const struct {
	const char *fourcc, *name;
} TABLE_NAMES[] = {
	{ "FLNG", "flingy" }, { "IMAG", "images" }, { "MAPD", "mapdata" }, { "ORDR", "orders" },
	{ "PORT", "portdata" }, { "SFXD", "sfxdata" }, { "SPRT", "sprites" }, { "TECH", "techdata" },
	{ "UNIT", "units" }, { "UPGR", "upgrades" }, { "WEAP", "weapons" },
};

/* Built-in requirement tables of StarCraft.exe (converter/formats/requirements.py). */
static const struct {
	const char *fourcc, *name;
} REQUIREMENT_NAMES[KP_DAT_REQUIREMENTS] = {
	{ "RQOR", "orders" }, { "RQRS", "research" }, { "RQTU", "tech_use" }, { "RQUP", "upgrades" }, { "RQUN", "units" },
};

static int fail(KpError *err, const char *msg)
{
	snprintf(err->msg, sizeof err->msg, "dat: %s", msg);
	return -1;
}

static const char *table_name(const uint8_t *fourcc)
{
	size_t i;
	for (i = 0; i < sizeof TABLE_NAMES / sizeof TABLE_NAMES[0]; i++) {
		if (memcmp(TABLE_NAMES[i].fourcc, fourcc, 4) == 0) return TABLE_NAMES[i].name;
	}
	return NULL;
}

int kp_dat_load(KpDat *dat, const uint8_t *data, size_t size, const char *name, const KpKpfExpect *expect, KpError *err)
{
	KpKpf c;
	KpKpfExpect want;
	const uint8_t *cols;
	uint32_t cols_size;
	uint32_t tables, t, pos, total = 0;
	int k;
	memset(dat, 0, sizeof *dat);
	dat->blob = (uint8_t *)malloc(size ? size : 1);
	if (!dat->blob) return fail(err, "out of memory");
	memcpy(dat->blob, data, size);
	dat->size = size;
	if (kp_kpf_parse(&c, dat->blob, size, name, 0, err) != 0) {
		kp_dat_free(dat);
		return -1;
	}
	memset(&want, 0, sizeof want);
	if (expect) want = *expect;
	want.kind = KP_DAT_KIND;
	want.schema_version = KP_DAT_SCHEMA;
	if (kp_kpf_check(&c, &want, err) != 0 || !(cols = kp_kpf_table(&c, "COLS", &cols_size, err))) goto failed;
	/* first pass: count the columns */
	if (cols_size < 2) goto bad;
	tables = kp_le_u16(cols);
	pos = 2;
	for (t = 0; t < tables; t++) {
		uint32_t ncols, j;
		if (cols_size - pos < 8) goto bad;
		ncols = kp_le_u16(cols + pos + 6);
		pos += 8;
		for (j = 0; j < ncols; j++) {
			if (cols_size - pos < 11 || cols[pos + 10] > cols_size - pos - 11) goto bad;
			pos += 11 + cols[pos + 10];
		}
		total += ncols;
	}
	if (tables > KP_DAT_MAX_TABLES) goto bad;
	dat->columns = (KpDatColumn *)calloc(total ? total : 1, sizeof *dat->columns);
	if (!dat->columns) {
		fail(err, "out of memory");
		goto failed;
	}
	pos = 2;
	for (t = 0; t < tables; t++) {
		const uint8_t *e = cols + pos;
		const char *tname = table_name(e);
		const KpKpfSection *section;
		uint32_t ncols = kp_le_u16(e + 6), j;
		char fourcc[5];
		memcpy(fourcc, e, 4);
		fourcc[4] = '\0';
		if (!tname) {
			snprintf(err->msg, sizeof err->msg, "dat: unknown table %s", fourcc);
			goto failed;
		}
		if (!(section = kp_kpf_require(&c, fourcc, err))) goto failed;
		dat->tables[t].name = tname;
		dat->tables[t].records = (uint16_t)kp_le_u16(e + 4);
		pos += 8;
		for (j = 0; j < ncols; j++) {
			const uint8_t *d = cols + pos;
			KpDatColumn *col = &dat->columns[dat->column_count];
			uint32_t offset = kp_le_u32(d + 6), len = d[10];
			col->table = tname;
			col->size = d[0];
			col->per_id = d[1];
			col->count = (uint16_t)kp_le_u16(d + 2);
			col->first_id = (uint16_t)kp_le_u16(d + 4);
			if (len >= sizeof col->name) len = sizeof col->name - 1;
			memcpy(col->name, d + 11, len);
			col->name[len] = '\0';
			if (col->size != 1 && col->size != 2 && col->size != 4) goto bad;
			if ((uint64_t)offset + (uint64_t)col->size * col->count > section->size) {
				snprintf(err->msg, sizeof err->msg, "dat: column %s out of bounds", col->name);
				goto failed;
			}
			col->data = section->data + offset;
			dat->column_count++;
			pos += 11 + d[10];
		}
		dat->table_count = (int)t + 1;
	}
	for (k = 0; k < KP_DAT_REQUIREMENTS; k++) {
		uint32_t n = 0;
		const uint8_t *words = kp_kpf_table(&c, REQUIREMENT_NAMES[k].fourcc, &n, NULL);
		dat->requirements[k].name = REQUIREMENT_NAMES[k].name;
		if (words) {
			dat->requirements[k].words = words;
			dat->requirements[k].count = n / 2;
		}
	}
	kp_kpf_free(&c);
	return 0;
bad:
	fail(err, "COLS section is malformed");
failed:
	kp_kpf_free(&c);
	kp_dat_free(dat);
	return -1;
}

void kp_dat_free(KpDat *dat)
{
	free(dat->columns);
	free(dat->blob);
	memset(dat, 0, sizeof *dat);
}

const KpDatColumn *kp_dat_column(const KpDat *dat, const char *table, const char *column)
{
	int i;
	for (i = 0; i < dat->column_count; i++) {
		const KpDatColumn *c = &dat->columns[i];
		if (strcmp(c->table, table) == 0 && strcmp(c->name, column) == 0) return c;
	}
	return NULL;
}

int kp_dat_set(KpDat *dat, const KpDatColumn *c, uint32_t i, uint32_t value)
{
	size_t at;
	uint8_t *p;
	if (!kp_dat_has(c, i)) return 0;
	at = (size_t)(c->data - dat->blob) + (size_t)(i - (uint32_t)c->first_id * c->per_id) * c->size;
	p = dat->blob + at;
	p[0] = (uint8_t)value;
	if (c->size >= 2) p[1] = (uint8_t)(value >> 8);
	if (c->size == 4) {
		p[2] = (uint8_t)(value >> 16);
		p[3] = (uint8_t)(value >> 24);
	}
	return 1;
}

int kp_dat_alloc(KpDat *dat, size_t bytes, int column_count, KpError *err)
{
	memset(dat, 0, sizeof *dat);
	dat->blob = (uint8_t *)calloc(bytes ? bytes : 1, 1);
	dat->columns = (KpDatColumn *)calloc(column_count > 0 ? (size_t)column_count : 1, sizeof *dat->columns);
	if (!dat->blob || !dat->columns) {
		kp_dat_free(dat);
		return fail(err, "out of memory");
	}
	dat->size = bytes;
	return 0;
}

const char *kp_dat_table_name(const char *name)
{
	size_t i;
	for (i = 0; i < sizeof TABLE_NAMES / sizeof TABLE_NAMES[0]; i++) {
		if (strcmp(TABLE_NAMES[i].name, name) == 0) return TABLE_NAMES[i].name;
	}
	return NULL;
}
