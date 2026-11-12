/*
 * The dialogs read from the original data (ADR 0018, stage D3): each dialog BIN's records in the native traversal order
 * of the loader's fixup 0x4c33e0 (scdata/formats/dialog.py parse) as the runtime's widget and SMK records
 * (KP_UI_WIDGET_SIZE, KP_UI_SMK_SIZE: what ui.kpd's DLGS holds), their texts in UTF-8 in one pool (POOL), in the
 * converter's order (kpd.dialogs_section): a record's text, then its SMK nodes' file names.
 */
#include "kp_ui_data.h"

#include "kp_text.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BIN_RECORD 86 /* PyMS's DialogBIN layout <L6H4LH5L4HLL4HLL */
#define BIN_NODE 30   /* <LH3LHHLL */
#define TYPE_CONTAINER 0
#define TYPE_IMAGE 5
#define TYPE_BUTTON 14
#define FLAG_HOTKEY 0x200u
#define FLAG_SHARED_BITMAP 0x20u
#define NONE16 0xffffu
#define NONE32 0xffffffffu
#define RECORDS_MAX 1024 /* a BIN's records (the original's largest has 90) */
#define NODES_MAX 256    /* a button's SMK nodes (the original's have at most a few): a longer chain loops */

typedef struct {
	uint8_t *p;
	size_t n, cap;
} Grow;

static int put(Grow *g, const void *data, size_t n)
{
	if (g->n + n > g->cap) {
		size_t cap = g->cap ? g->cap : 4096;
		uint8_t *p;
		while (cap < g->n + n) cap *= 2;
		if ((p = (uint8_t *)realloc(g->p, cap)) == NULL) return -1;
		g->p = p;
		g->cap = cap;
	}
	if (n) memcpy(g->p + g->n, data, n);
	g->n += n;
	return 0;
}

static int put16(Grow *g, uint32_t v)
{
	uint8_t b[2];
	b[0] = (uint8_t)v;
	b[1] = (uint8_t)(v >> 8);
	return put(g, b, 2);
}

static int put32(Grow *g, uint32_t v)
{
	uint8_t b[4];
	int k;
	for (k = 0; k < 4; k++) b[k] = (uint8_t)(v >> (8 * k));
	return put(g, b, 4);
}

/* A text into the pool (kpd.text_utf8: UTF-8 kept, anything else read as windows-1252) and its NUL: its offset and
 * length. */
static int pool_text(Grow *pool, const uint8_t *raw, size_t n, uint32_t *off, uint32_t *len)
{
	size_t size;
	uint8_t *utf8 = kp_text_utf8_of(raw, n, &size);
	int rc;
	if (!utf8) return -1;
	*off = (uint32_t)pool->n;
	*len = (uint32_t)size;
	rc = put(pool, utf8, size + 1);
	free(utf8);
	return rc;
}

static int cstring(const uint8_t *bin, size_t size, uint32_t at, size_t *n)
{
	const uint8_t *end;
	if (at >= size || (end = (const uint8_t *)memchr(bin + at, 0, size - at)) == NULL) return -1;
	*n = (size_t)(end - (bin + at));
	return 0;
}

/* One record's text: its own, a label in its place, or none. */
typedef struct {
	const uint8_t *data;
	size_t size;
	int present;
	int lowered; /* the hotkey's first letter, lowercased (0x4d1375) */
} Text;

static int emit_text(Grow *pool, const Text *t, uint32_t *off, uint32_t *len)
{
	uint8_t *copy;
	int rc;
	if (!t->present) {
		*off = NONE32;
		*len = 0;
		return 0;
	}
	if (!t->lowered) return pool_text(pool, t->data, t->size, off, len);
	if ((copy = (uint8_t *)malloc(t->size)) == NULL) return -1;
	memcpy(copy, t->data, t->size);
	copy[0] = (uint8_t)(copy[0] + 32);
	rc = pool_text(pool, copy, t->size, off, len);
	free(copy);
	return rc;
}

/* The records of a BIN in the native traversal order (dialog.py _order: a container's children first, then the next
 * record): visit(ctx, record offset, parent index or -1, index) for each; 0, or -1 with the reason in err. */
static int bin_walk(const uint8_t *bin, size_t size, int (*visit)(void *ctx, uint32_t at, int parent, int index),
	void *ctx, char *err, size_t err_size)
{
	uint32_t seen[RECORDS_MAX], at = 0;
	int count = 0, parent = -1, k;
	for (;;) {
		if (count == RECORDS_MAX || (uint64_t)at + BIN_RECORD > size) {
			snprintf(err, err_size, "record at 0x%x runs past the end", (unsigned)at);
			return -1;
		}
		for (k = 0; k < count; k++) {
			if (seen[k] == at) {
				snprintf(err, err_size, "record at 0x%x repeats", (unsigned)at);
				return -1;
			}
		}
		seen[count] = at;
		if (visit(ctx, at, parent, count) != 0) return -1;
		count++;
		if (kp_le_u32(bin + at + 34) == TYPE_CONTAINER) {
			parent = count - 1;
			at = kp_le_u32(bin + at + 66);
		} else {
			at = kp_le_u32(bin + at);
		}
		if (!at) return 0;
	}
}

/* A record's text (dialog.py parse): its own, the hotkey's first letter lowercased (0x4d1375); -1 when unterminated. */
static int record_text(const uint8_t *bin, size_t size, const uint8_t *r, Text *text, char *err, size_t err_size)
{
	memset(text, 0, sizeof *text);
	if (!kp_le_u32(r + 20)) return 0;
	if (cstring(bin, size, kp_le_u32(r + 20), &text->size) != 0) {
		snprintf(err, err_size, "text at 0x%x is not NUL-terminated", (unsigned)kp_le_u32(r + 20));
		return -1;
	}
	text->data = bin + kp_le_u32(r + 20);
	text->present = 1;
	text->lowered = (kp_le_u32(r + 24) & FLAG_HOTKEY) && text->size && text->data[0] >= 'A' && text->data[0] <= 'Z';
	return 0;
}

/* An SMK node's file name (none: empty); -1 when the node runs past the end or its name is unterminated. */
static int node_name(const uint8_t *bin, size_t size, uint32_t node, Text *name, char *err, size_t err_size)
{
	memset(name, 0, sizeof *name);
	name->present = 1;
	if ((uint64_t)node + BIN_NODE > size) {
		snprintf(err, err_size, "SMK node at 0x%x runs past the end", (unsigned)node);
		return -1;
	}
	if (!kp_le_u32(bin + node + 10)) return 0;
	if (cstring(bin, size, kp_le_u32(bin + node + 10), &name->size) != 0) {
		snprintf(err, err_size, "SMK name at 0x%x is not NUL-terminated", (unsigned)kp_le_u32(bin + node + 10));
		return -1;
	}
	name->data = bin + kp_le_u32(bin + node + 10);
	return 0;
}

/* One BIN's records onto a dialog's (records before `skip` left out, parents re-based by it). */
typedef struct {
	Grow *records, *smks, *pool;
	int *widget_count, *smk_count;
	const uint8_t *bin;
	size_t size;
	int skip;
	const KpUiDialogLabel *labels;
	int label_count;
	char *err;
	size_t err_size;
} Records;

static int record_visit(void *ctx, uint32_t at, int parent, int index)
{
	Records *c = (Records *)ctx;
	const uint8_t *r = c->bin + at;
	uint32_t kind = kp_le_u32(r + 34), flags = kp_le_u32(r + 24), bitmap = kp_le_u32(r + 16), x1 = kp_le_u16(r + 4), y1 = kp_le_u16(r + 6), shared,
		delay, t_off, t_len, first = NONE16, nodes = 0, parent_out, ex[4];
	Text text;
	int k;
	if (record_text(c->bin, c->size, r, &text, c->err, c->err_size) != 0) return -1;
	for (k = 0; k < c->label_count; k++) {
		if (c->labels[k].record != index) continue;
		if (text.present || index == 0) {
			snprintf(c->err, c->err_size, "record %d has a text of its own", index);
			return -1;
		}
		text.data = (const uint8_t *)c->labels[k].text;
		text.size = strlen(c->labels[k].text);
		text.present = 1;
	}
	for (k = 0; k < 4; k++) ex[k] = kp_le_u16(r + 70 + 2 * k);
	shared = NONE32;
	if (bitmap & 0x80000000u) {
		if (bitmap == NONE32) {
			snprintf(c->err, c->err_size, "record at 0x%x: shared-bitmap reference -1", (unsigned)at);
			return -1;
		}
		shared = bitmap;
		flags |= FLAG_SHARED_BITMAP;
	}
	if (kind > 0xffff) {
		snprintf(c->err, c->err_size, "record at 0x%x: type %u", (unsigned)at, (unsigned)kind);
		return -1;
	}
	if (index < c->skip) return 0;
	parent_out = parent < 0 ? NONE16 : (uint32_t)parent; /* as kpd.koprulu_extensions keeps them: the root's children 0 */
	if (emit_text(c->pool, &text, &t_off, &t_len) != 0) goto oom;
	if (kind == TYPE_BUTTON) {
		uint32_t node = kp_le_u32(r + 66);
		while (node) {
			const uint8_t *n8 = c->bin + node;
			Text name;
			uint32_t f_off, f_len;
			if (nodes == NODES_MAX) {
				snprintf(c->err, c->err_size, "record at 0x%x: more than %d SMK nodes (a loop)", (unsigned)at, NODES_MAX);
				return -1;
			}
			if (node_name(c->bin, c->size, node, &name, c->err, c->err_size) != 0) return -1;
			if (first == NONE16) first = (uint32_t)*c->smk_count;
			if (emit_text(c->pool, &name, &f_off, &f_len) != 0 || put16(c->smks, kp_le_u16(n8 + 4)) != 0
				|| put32(c->smks, f_off) != 0 || put16(c->smks, f_len) != 0 || put16(c->smks, kp_le_u16(n8 + 18)) != 0
				|| put16(c->smks, kp_le_u16(n8 + 20)) != 0 || put32(c->smks, kp_le_u32(n8 + 6)) != 0
				|| put32(c->smks, kp_le_u32(n8 + 14)) != 0 || put32(c->smks, kp_le_u32(n8 + 22)) != 0
				|| put32(c->smks, kp_le_u32(n8 + 26)) != 0)
				goto oom;
			(*c->smk_count)++;
			nodes++;
			node = kp_le_u32(n8);
		}
		ex[0] = (ex[0] + x1) & 0xffff;
		ex[1] = (ex[1] + y1) & 0xffff;
		ex[2] = (ex[2] + x1) & 0xffff;
		ex[3] = (ex[3] + y1) & 0xffff;
	}
	delay = kp_le_u32(r + 28);
	if (put16(c->records, parent_out) != 0 || put16(c->records, kind) != 0 || put16(c->records, kp_le_u16(r + 32)) != 0
		|| put32(c->records, flags) != 0 || put(c->records, r + 4, 12) != 0 || put32(c->records, t_off) != 0
		|| put16(c->records, t_len) != 0 || put16(c->records, first) != 0 || put16(c->records, nodes) != 0
		|| put(c->records, r + 54, 8) != 0 || put16(c->records, ex[0]) != 0 || put16(c->records, ex[1]) != 0
		|| put16(c->records, ex[2]) != 0 || put16(c->records, ex[3]) != 0 || put32(c->records, shared) != 0
		|| put16(c->records, delay & 0xffff) != 0 || put16(c->records, delay >> 16) != 0
		|| put32(c->records, kp_le_u32(r + 38)) != 0)
		goto oom;
	(*c->widget_count)++;
	return 0;
oom:
	snprintf(c->err, c->err_size, "out of memory");
	return -1;
}

static int dialog_records(Grow *records, Grow *smks, Grow *pool, int *widget_count, int *smk_count, const uint8_t *bin,
	size_t size, int skip, const KpUiDialogLabel *labels, int label_count, char *err, size_t err_size)
{
	Records c;
	c.records = records;
	c.smks = smks;
	c.pool = pool;
	c.widget_count = widget_count;
	c.smk_count = smk_count;
	c.bin = bin;
	c.size = size;
	c.skip = skip;
	c.labels = labels;
	c.label_count = label_count;
	c.err = err;
	c.err_size = err_size;
	return bin_walk(bin, size, record_visit, &c, err, err_size);
}

/* The files a BIN names (kpd.glue_file_paths): type-5 images' texts and buttons' SMK names, in traversal order. */
typedef struct {
	const uint8_t *bin;
	size_t size;
	int (*add)(void *ctx, const uint8_t *path, size_t n);
	void *ctx;
	char *err;
	size_t err_size;
} BinFiles;

static int add_text(BinFiles *c, const Text *t)
{
	uint8_t path[256];
	if (t->size >= sizeof path) {
		snprintf(c->err, c->err_size, "a file name of %u bytes", (unsigned)t->size);
		return -1;
	}
	if (t->size) memcpy(path, t->data, t->size);
	if (t->lowered) path[0] = (uint8_t)(path[0] + 32);
	if (c->add(c->ctx, path, t->size) != 0) {
		snprintf(c->err, c->err_size, "out of memory");
		return -1;
	}
	return 0;
}

static int files_visit(void *ctx, uint32_t at, int parent, int index)
{
	BinFiles *c = (BinFiles *)ctx;
	const uint8_t *r = c->bin + at;
	uint32_t kind = kp_le_u32(r + 34), node;
	int count;
	Text text;
	(void)parent;
	(void)index;
	if (record_text(c->bin, c->size, r, &text, c->err, c->err_size) != 0) return -1;
	if (kind == TYPE_IMAGE && text.size && add_text(c, &text) != 0) return -1;
	if (kind != TYPE_BUTTON) return 0;
	for (node = kp_le_u32(r + 66), count = 0; node; node = kp_le_u32(c->bin + node), count++) {
		if (count == NODES_MAX) {
			snprintf(c->err, c->err_size, "record at 0x%x: more than %d SMK nodes (a loop)", (unsigned)at, NODES_MAX);
			return -1;
		}
		if (node_name(c->bin, c->size, node, &text, c->err, c->err_size) != 0 || add_text(c, &text) != 0) return -1;
	}
	return 0;
}

int kp_ui_bin_files(const uint8_t *bin, size_t size, int (*add)(void *ctx, const uint8_t *path, size_t n), void *ctx,
	char *err, size_t err_size)
{
	BinFiles c;
	c.bin = bin;
	c.size = size;
	c.add = add;
	c.ctx = ctx;
	c.err = err;
	c.err_size = err_size;
	return bin_walk(bin, size, files_visit, &c, err, err_size);
}

int kp_ui_dialogs_build(const KpUiDialogSource *src, int count, KpUiDialogs *out, char *err, size_t err_size)
{
	Grow dlgs, pool;
	int i;
	memset(out, 0, sizeof *out);
	memset(&dlgs, 0, sizeof dlgs);
	memset(&pool, 0, sizeof pool);
	if (put16(&dlgs, (uint32_t)count) != 0) goto oom;
	for (i = 0; i < count; i++) {
		const KpUiDialogSource *s = &src[i];
		Grow records, smks;
		int widgets = 0, smk_count = 0, k, rc;
		size_t name_len = strlen(s->name);
		char why[160];
		uint8_t len8 = (uint8_t)name_len;
		memset(&records, 0, sizeof records);
		memset(&smks, 0, sizeof smks);
		why[0] = 0;
		rc = dialog_records(&records, &smks, &pool, &widgets, &smk_count, s->bin, s->bin_size, 0, s->labels, s->label_count,
			why, sizeof why);
		for (k = 0; rc == 0 && k < s->label_count; k++) {
			if (s->labels[k].record <= 0 || s->labels[k].record >= widgets) {
				snprintf(why, sizeof why, "no record %d without a text", s->labels[k].record);
				rc = -1;
			}
		}
		if (rc == 0 && s->extend != NULL)
			rc = dialog_records(&records, &smks, &pool, &widgets, &smk_count, s->extend, s->extend_size, 1, NULL, 0, why,
				sizeof why);
		if (rc == 0 && (name_len > 255 || widgets > 0xffff || smk_count > 0xffff)) {
			snprintf(why, sizeof why, "too many records");
			rc = -1;
		}
		if (rc == 0 && (put(&dlgs, &len8, 1) != 0 || put(&dlgs, s->name, name_len) != 0 || put16(&dlgs, (uint32_t)widgets) != 0
				|| put16(&dlgs, (uint32_t)smk_count) != 0 || put(&dlgs, records.p, records.n) != 0
				|| put(&dlgs, smks.p, smks.n) != 0)) {
			snprintf(why, sizeof why, "out of memory");
			rc = -1;
		}
		free(records.p);
		free(smks.p);
		if (rc != 0) {
			snprintf(err, err_size, "%s: %s", s->name, why);
			free(dlgs.p);
			free(pool.p);
			return -1;
		}
	}
	out->dlgs = dlgs.p;
	out->dlgs_size = dlgs.n;
	out->pool = pool.p;
	out->pool_size = pool.n;
	return 0;
oom:
	snprintf(err, err_size, "out of memory");
	free(dlgs.p);
	free(pool.p);
	return -1;
}

void kp_ui_dialogs_free(KpUiDialogs *d)
{
	free(d->dlgs);
	free(d->pool);
	memset(d, 0, sizeof *d);
}
