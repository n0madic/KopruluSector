/* The DAT files read directly (kp_dat.h). */
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

/* A table's bytes by its fourcc (the container's section, an original file read): NULL with err set. */
typedef const uint8_t *(*TableBytes)(void *ctx, const char *fourcc, uint32_t *size, KpError *err);

/* The tables and columns COLS describes over the tables' bytes: 0, or -1 with err set (dat's arrays left to the
 * caller's kp_dat_free). */
static int columns(KpDat *dat, const uint8_t *cols, uint32_t cols_size, TableBytes table, void *ctx, KpError *err)
{
	uint32_t tables, t, pos, total = 0;
	/* first pass: count the columns */
	if (cols_size < 2) return fail(err, "COLS section is malformed");
	tables = kp_le_u16(cols);
	pos = 2;
	for (t = 0; t < tables; t++) {
		uint32_t ncols, j;
		if (cols_size - pos < 8) return fail(err, "COLS section is malformed");
		ncols = kp_le_u16(cols + pos + 6);
		pos += 8;
		for (j = 0; j < ncols; j++) {
			if (cols_size - pos < 11 || cols[pos + 10] > cols_size - pos - 11) return fail(err, "COLS section is malformed");
			pos += 11 + cols[pos + 10];
		}
		total += ncols;
	}
	if (tables > KP_DAT_MAX_TABLES) return fail(err, "COLS section is malformed");
	dat->columns = (KpDatColumn *)calloc(total ? total : 1, sizeof *dat->columns);
	if (!dat->columns) return fail(err, "out of memory");
	pos = 2;
	for (t = 0; t < tables; t++) {
		const uint8_t *e = cols + pos, *bytes;
		const char *tname = table_name(e);
		uint32_t ncols = kp_le_u16(e + 6), j, size;
		char fourcc[5];
		memcpy(fourcc, e, 4);
		fourcc[4] = '\0';
		if (!tname) {
			snprintf(err->msg, sizeof err->msg, "dat: unknown table %s", fourcc);
			return -1;
		}
		if (!(bytes = table(ctx, fourcc, &size, err))) return -1;
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
			if (col->size != 1 && col->size != 2 && col->size != 4) return fail(err, "COLS section is malformed");
			if ((uint64_t)offset + (uint64_t)col->size * col->count > size) {
				snprintf(err->msg, sizeof err->msg, "dat: column %s out of bounds", col->name);
				return -1;
			}
			col->data = bytes + offset;
			dat->column_count++;
			pos += 11 + d[10];
		}
		dat->table_count = (int)t + 1;
	}
	return 0;
}

/* The DAT files read for columns(): each table's arr\<name>.dat in a buffer of its own until kp_dat_from_files joins
 * them in one blob. */
typedef struct {
	const KpFiles *files;
	int count;
	uint8_t *bytes[KP_DAT_MAX_TABLES];
	size_t size[KP_DAT_MAX_TABLES];
} DatFiles;

static const uint8_t *file_table(void *ctx, const char *fourcc, uint32_t *size, KpError *err)
{
	DatFiles *d = (DatFiles *)ctx;
	char path[64];
	if (d->count >= KP_DAT_MAX_TABLES) {
		fail(err, "too many tables");
		return NULL;
	}
	snprintf(path, sizeof path, "arr\\%s.dat", table_name((const uint8_t *)fourcc));
	if (!(d->bytes[d->count] = kp_files_need(d->files, path, &d->size[d->count], err))) return NULL;
	*size = (uint32_t)d->size[d->count];
	return d->bytes[d->count++];
}

/* Moves the pointers of dat's columns and requirements inside [from, from + size) to the same offsets of to. */
static void rebase(KpDat *dat, const uint8_t *from, size_t size, const uint8_t *to)
{
	int k;
	for (k = 0; k < dat->column_count; k++) {
		const uint8_t *p = dat->columns[k].data;
		if (p >= from && p < from + size) dat->columns[k].data = to + (p - from);
	}
	for (k = 0; k < KP_DAT_REQUIREMENTS; k++) {
		const uint8_t *p = dat->requirements[k].words;
		if (p && p >= from && p < from + size) dat->requirements[k].words = to + (p - from);
	}
}

int kp_dat_from_files(KpDat *dat, const KpFiles *f, KpError *err)
{
	const KpDefsTable *cols = kp_defs_table(f->profile, "COLS");
	DatFiles d;
	size_t total = 0, at = 0;
	int k, rc = -1;
	memset(dat, 0, sizeof *dat);
	memset(&d, 0, sizeof d);
	d.files = f;
	if (!cols) return fail(err, "no compiled COLS table");
	if (columns(dat, cols->data, cols->size, file_table, &d, err) != 0) goto done;
	for (k = 0; k < d.count; k++) total += d.size[k];
	if (!(dat->blob = (uint8_t *)malloc(total ? total : 1))) {
		fail(err, "out of memory");
		goto done;
	}
	dat->size = total;
	for (k = 0; k < d.count; k++) { /* one blob, as a container's copy is (kp_dat_set writes into it) */
		memcpy(dat->blob + at, d.bytes[k], d.size[k]);
		rebase(dat, d.bytes[k], d.size[k], dat->blob + at);
		at += d.size[k];
	}
	for (k = 0; k < KP_DAT_REQUIREMENTS; k++) {
		const KpDefsTable *t = kp_defs_table(f->profile, REQUIREMENT_NAMES[k].fourcc);
		dat->requirements[k].name = REQUIREMENT_NAMES[k].name;
		if (t) {
			dat->requirements[k].words = t->data;
			dat->requirements[k].count = t->size / 2;
		}
	}
	rc = 0;
done:
	for (k = 0; k < d.count; k++) free(d.bytes[k]);
	if (rc != 0) kp_dat_free(dat);
	return rc;
}

int kp_dat_copy(KpDat *out, const KpDat *in, KpError *err)
{
	*out = *in;
	out->blob = (uint8_t *)malloc(in->size ? in->size : 1);
	out->columns = (KpDatColumn *)malloc((size_t)(in->column_count ? in->column_count : 1) * sizeof *out->columns);
	if (!out->blob || !out->columns) {
		kp_dat_free(out);
		return fail(err, "out of memory");
	}
	memcpy(out->blob, in->blob, in->size);
	memcpy(out->columns, in->columns, (size_t)in->column_count * sizeof *out->columns);
	rebase(out, in->blob, in->size, out->blob);
	return 0;
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
