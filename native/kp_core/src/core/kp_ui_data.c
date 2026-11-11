/* The interface's data (kp_ui_data.h), built from the original files. */
#include "kp_ui_data.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kp_dat.h"
#include "kp_grp.h"
#include "kp_kpf.h"
#include "kp_text.h"
#include "kp_wav.h"

#define SOUND_PRESENT 0x01
#define SOUND_PARSED 0x02
#define PORTRAIT_VARIANTS 10 /* the variant digits probed per stem (0x4a6b40, 0x4a6c50) */
#define NONE16 0xffffu
#define THEME_FONT_ROWS KP_UI_THEME_FONT_ROWS

static int fail(KpError *err, const char *what)
{
	snprintf(err->msg, sizeof err->msg, "ui data: %s", what);
	return -1;
}

/* A malloc'd copy of a NUL-terminated text, or NULL. */
static char *copy_text(const char *s)
{
	size_t n = strlen(s) + 1;
	char *c = (char *)malloc(n);
	if (c) memcpy(c, s, n);
	return c;
}

static void *own(KpUiData *d, void *p)
{
	if (!p) return NULL;
	if (d->owned_count == d->owned_cap) {
		int cap = d->owned_cap ? 2 * d->owned_cap : 64;
		void **n = (void **)realloc(d->owned, (size_t)cap * sizeof *n);
		if (!n) {
			free(p);
			return NULL;
		}
		d->owned = n;
		d->owned_cap = cap;
	}
	d->owned[d->owned_count++] = p;
	return p;
}

void kp_ui_data_free(KpUiData *d)
{
	int k;
	for (k = 0; k < d->owned_count; k++) free(d->owned[k]);
	free(d->owned);
	free(d->grps.files);
	free(d->pcx.files);
	free(d->glue.files);
	free(d->ghosts.files);
	free(d->vector_fonts.files);
	free(d->portraits.files);
	memset(d, 0, sizeof *d);
}

int kp_ui_files_section(const uint8_t *data, size_t size, KpUiFiles *out)
{
	size_t pos = 2;
	int i, n;
	out->files = NULL;
	out->count = 0;
	if (data == NULL || size < 2) return -1;
	n = (int)kp_le_u16(data);
	if (!(out->files = (KpUiFile *)calloc((size_t)(n > 0 ? n : 1), sizeof(KpUiFile)))) return -1;
	for (i = 0; i < n; i++) {
		size_t len, bytes;
		if (pos + 1 > size) return -1;
		len = data[pos];
		if (pos + 1 + len + 4 > size) return -1;
		bytes = kp_le_u32(data + pos + 1 + len);
		if ((uint64_t)pos + 5 + len + bytes > size) return -1; /* 64 bits: bytes is the file's u32 */
		out->files[i].path = (const char *)data + pos + 1;
		out->files[i].path_len = len;
		out->files[i].data = data + pos + 5 + len;
		out->files[i].size = bytes;
		pos += 5 + len + bytes;
	}
	out->count = n;
	return 0;
}

const uint8_t *kp_ui_data_table(const KpUiData *d, const char *fourcc, uint32_t *size)
{
	const KpDefsTable *t;
	*size = 0;
	if (!(t = kp_defs_table(d->profile, fourcc))) return NULL;
	*size = t->size;
	return t->data;
}

int kp_ui_music_track(const uint8_t *musi, size_t size, int k, char *path, size_t path_size, int *kind)
{
	size_t at = 1, len;
	int t;
	if (size < 1 || k < 0 || k >= musi[0]) return -1;
	for (t = 0;; t++) {
		if (at + 1 > size || at + 4 + musi[at] > size) return -1;
		len = musi[at];
		if (t == k) break;
		at += 4 + len;
	}
	if (len >= path_size) return -1;
	memcpy(path, musi + at + 1, len);
	path[len] = 0;
	*kind = musi[at + 1 + len];
	return 0;
}

/* --- built from the original files --- */

static const char *const TBL_FILES[KP_UI_TBLS] = { "rez\\stat_txt.tbl", "rez\\network.tbl", "rez\\gluAll.tbl",
	"rez\\gluHist.tbl", "rez\\tips.tbl", "rez\\help_txt.tbl" };
static const char *const SOUND_PACKS[3] = { "sound\\terran\\", "sound\\protoss\\", "sound\\zerg\\" }; /* PACK 2..4; else 1 */

typedef struct {
	const uint8_t *p;
	size_t n;
} Name;

/* A compiled names table (tools/defs_c.py names_table: u16 count; per name u8 length, its bytes): the count, or -1. */
static int names_count(const KpDefsTable *t)
{
	size_t at = 2;
	int n, k;
	if (!t || t->size < 2) return -1;
	n = (int)kp_le_u16(t->data);
	for (k = 0; k < n; k++) {
		if (at + 1 > t->size || at + 1 + t->data[at] > t->size) return -1;
		at += 1 + (size_t)t->data[at];
	}
	return n;
}

static Name name_at(const KpDefsTable *t, int index)
{
	Name r;
	size_t at = 2;
	int k;
	for (k = 0; k < index; k++) at += 1 + (size_t)t->data[at];
	r.n = t->data[at];
	r.p = t->data + at + 1;
	return r;
}

static const KpDefsTable *names_table(const KpFiles *f, const char *fourcc, int *count, KpError *err)
{
	const KpDefsTable *t = kp_defs_table(f->profile, fourcc);
	if ((*count = names_count(t)) < 0) {
		snprintf(err->msg, sizeof err->msg, "ui data: no names table %s compiled for profile \"%s\"", fourcc, f->profile->id);
		return NULL;
	}
	return t;
}

static uint8_t latin1_lower(uint8_t c)
{
	if (c >= 'A' && c <= 'Z') return (uint8_t)(c + 32);
	if (c >= 0xc0 && c <= 0xde && c != 0xd7) return (uint8_t)(c + 32);
	return c;
}

static int same_path(const char *a, size_t an, const uint8_t *b, size_t bn)
{
	size_t k;
	if (an != bn) return 0;
	for (k = 0; k < an; k++)
		if (latin1_lower((uint8_t)a[k]) != latin1_lower(b[k])) return 0;
	return 1;
}

/* A path list being gathered (the glue's files: case-insensitive, the first spelling kept). */
typedef struct {
	char **paths;
	size_t *lens;
	int count, cap;
} Paths;

static int paths_add(Paths *l, const uint8_t *p, size_t n, int dedupe)
{
	int k;
	char *copy;
	if (dedupe)
		for (k = 0; k < l->count; k++)
			if (same_path(l->paths[k], l->lens[k], p, n)) return 0;
	if (l->count == l->cap) {
		int cap = l->cap ? 2 * l->cap : 64;
		char **np = (char **)realloc(l->paths, (size_t)cap * sizeof *np);
		size_t *nl;
		if (!np) return -1;
		l->paths = np;
		if (!(nl = (size_t *)realloc(l->lens, (size_t)cap * sizeof *nl))) return -1;
		l->lens = nl;
		l->cap = cap;
	}
	if (!(copy = (char *)malloc(n + 1))) return -1;
	memcpy(copy, p, n);
	copy[n] = 0;
	l->paths[l->count] = copy;
	l->lens[l->count++] = n;
	return 0;
}

static int paths_add_dedupe(void *ctx, const uint8_t *p, size_t n) { return paths_add((Paths *)ctx, p, n, 1); }

static int paths_addz(Paths *l, const char *s, int dedupe) { return paths_add(l, (const uint8_t *)s, strlen(s), dedupe); }

static void paths_free(Paths *l)
{
	int k;
	for (k = 0; k < l->count; k++) free(l->paths[k]);
	free(l->paths);
	free(l->lens);
	memset(l, 0, sizeof *l);
}

/* The files of a path list read into a KpUiFiles (the paths and the bytes owned by d). */
static int read_list(KpUiData *d, const KpFiles *f, Paths *l, KpUiFiles *out, KpError *err)
{
	int k;
	if (!(out->files = (KpUiFile *)calloc((size_t)(l->count > 0 ? l->count : 1), sizeof *out->files))) return fail(err, "out of memory");
	for (k = 0; k < l->count; k++) {
		size_t size;
		uint8_t *b = kp_files_need(f, l->paths[k], &size, err);
		if (!b || !own(d, b)) return b ? fail(err, "out of memory") : -1;
		out->files[k].path = l->paths[k];
		out->files[k].path_len = l->lens[k];
		out->files[k].data = b;
		out->files[k].size = size;
		if (!own(d, l->paths[k])) return fail(err, "out of memory");
		l->paths[k] = NULL; /* now d's */
		out->count = k + 1;
	}
	return 0;
}

/* A file the data needs, its bytes owned by d. */
static uint8_t *need(KpUiData *d, const KpFiles *f, const char *path, size_t *size, KpError *err)
{
	uint8_t *b = kp_files_need(f, path, size, err);
	if (b && !own(d, b)) {
		fail(err, "out of memory");
		return NULL;
	}
	return b;
}

/* The element of a DAT column in file order (converter dat.values()[k]). */
static uint32_t col_at(const KpDatColumn *c, uint32_t k) { return kp_dat_value(c, (uint32_t)c->first_id * c->per_id + k); }

/* A TBL's entry (1-based) up to its first NUL (tbl.decode_tbl), or NULL when the table or the id is malformed. */
static const uint8_t *tbl_entry(const uint8_t *tbl, size_t size, uint32_t id, size_t *n)
{
	size_t off, end;
	if (size < 2 || id == 0 || id > kp_le_u16(tbl) || 2 + 2 * (size_t)kp_le_u16(tbl) > size) return NULL;
	off = kp_le_u16(tbl + 2 * id);
	if (off > size) off = size;
	for (end = off; end < size && tbl[end]; end++) {
	}
	*n = end - off;
	return tbl + off;
}

/* The dialogs (kpd.dialogs_section over defs/ui's BINs, then ours: KBIN, with KEXT's records and KLAB's texts). */
static int dialogs(KpUiData *d, const KpFiles *f, KpError *err)
{
	const KpDefsTable *bins, *kbin = kp_defs_table(f->profile, "KBIN"), *kext = kp_defs_table(f->profile, "KEXT"),
		*klab = kp_defs_table(f->profile, "KLAB");
	KpUiDialogSource *src = NULL;
	KpUiDialogLabel *labels = NULL;
	KpUiFiles ours = { NULL, 0 }, ext = { NULL, 0 };
	KpUiDialogs out;
	int n, k, j, count, label_count = 0, rc = -1;
	char why[KP_ERROR_SIZE];
	if (!(bins = names_table(f, "UBIN", &n, err))) return -1;
	if (!kbin || !kext || !klab || klab->size < 2) return fail(err, "no compiled KBIN, KEXT or KLAB");
	if (kp_ui_files_section(kbin->data, kbin->size, &ours) != 0 || kp_ui_files_section(kext->data, kext->size, &ext) != 0) {
		goto bad;
	}
	count = n + ours.count;
	src = (KpUiDialogSource *)calloc((size_t)count, sizeof *src);
	labels = (KpUiDialogLabel *)calloc((size_t)kp_le_u16(klab->data) + 1, sizeof *labels);
	if (!src || !labels) {
		fail(err, "out of memory");
		goto done;
	}
	for (k = 0; k < n; k++) {
		Name nm = name_at(bins, k);
		char *path = (char *)own(d, malloc(nm.n + 1));
		size_t size;
		if (!path) {
			fail(err, "out of memory");
			goto done;
		}
		memcpy(path, nm.p, nm.n);
		path[nm.n] = 0;
		src[k].name = path;
		if (!(src[k].bin = need(d, f, path, &size, err))) goto done;
		src[k].bin_size = size;
	}
	for (k = 0; k < ours.count; k++) {
		char *name = (char *)own(d, malloc(ours.files[k].path_len + 1));
		if (!name) {
			fail(err, "out of memory");
			goto done;
		}
		memcpy(name, ours.files[k].path, ours.files[k].path_len);
		name[ours.files[k].path_len] = 0;
		src[n + k].name = name;
		src[n + k].bin = ours.files[k].data;
		src[n + k].bin_size = ours.files[k].size;
	}
	for (j = 0; j < ext.count; j++) { /* the records appended to an original's */
		for (k = 0; k < n && !same_path(src[k].name, strlen(src[k].name), (const uint8_t *)ext.files[j].path, ext.files[j].path_len); k++) {
		}
		if (k == n) goto bad;
		src[k].extend = ext.files[j].data;
		src[k].extend_size = ext.files[j].size;
	}
	{ /* KLAB: u16 count; per text a label (u8 length, the dialog), u16 record, u16 length, the UTF-8 text */
		size_t at = 2;
		int total = (int)kp_le_u16(klab->data);
		for (j = 0; j < total; j++) {
			size_t len, tlen;
			char *text;
			if (at + 1 > klab->size || at + 1 + klab->data[at] + 4 > klab->size) goto bad;
			len = klab->data[at];
			tlen = kp_le_u16(klab->data + at + 3 + len);
			if (at + 5 + len + tlen > klab->size) goto bad;
			for (k = 0; k < n && !same_path(src[k].name, strlen(src[k].name), klab->data + at + 1, len); k++) {
			}
			if (k == n || !(text = (char *)own(d, malloc(tlen + 1)))) goto bad;
			memcpy(text, klab->data + at + 5 + len, tlen);
			text[tlen] = 0;
			labels[label_count].record = (int)kp_le_u16(klab->data + at + 1 + len);
			labels[label_count].text = text;
			if (!src[k].labels) src[k].labels = &labels[label_count];
			else if (src[k].labels + src[k].label_count != &labels[label_count]) goto bad; /* one dialog's labels together */
			src[k].label_count++;
			label_count++;
			at += 5 + len + tlen;
		}
	}
	if (kp_ui_dialogs_build(src, count, &out, why, sizeof why) != 0) {
		snprintf(err->msg, sizeof err->msg, "ui data: %s", why);
		goto done;
	}
	if (!own(d, out.dlgs) || !own(d, out.pool)) {
		fail(err, "out of memory");
		goto done;
	}
	d->dlgs.data = out.dlgs;
	d->dlgs.size = out.dlgs_size;
	d->pool.data = out.pool;
	d->pool.size = out.pool_size;
	rc = 0;
	goto done;
bad:
	fail(err, "the compiled dialogs (KBIN, KEXT, KLAB) are malformed");
done:
	free(src);
	free(labels); /* the dialogs' records and texts are copies */
	free(ours.files);
	free(ext.files);
	return rc;
}

/* credits_paths' tags: "</BACKGROUND x>" and "</FONTCOLOR x>", the keyword in any case: each x in order. */
static int credit_tags(Paths *l, const uint8_t *t, size_t n)
{
	static const char *const TAGS[2] = { "BACKGROUND", "FONTCOLOR" };
	size_t at = 0;
	while (at + 2 <= n) {
		int k, matched = 0;
		if (t[at] != '<' || t[at + 1] != '/') {
			at++;
			continue;
		}
		for (k = 0; k < 2 && !matched; k++) {
			size_t m = strlen(TAGS[k]), j, end;
			if (at + 2 + m + 1 > n) continue;
			for (j = 0; j < m; j++) {
				uint8_t c = t[at + 2 + j];
				if (c >= 'a' && c <= 'z') c = (uint8_t)(c - 32);
				if (c != (uint8_t)TAGS[k][j]) break;
			}
			if (j < m || t[at + 2 + m] != ' ') continue;
			for (end = at + 3 + m; end < n && t[end] != '>'; end++) {
			}
			if (end == n) continue;
			if (paths_add(l, t + at + 3 + m, end - (at + 3 + m), 1) != 0) return -1;
			at = end + 1;
			matched = 1;
		}
		if (!matched) at++;
	}
	return 0;
}

/* GFIL (kpd.glue_file_paths): each theme's Backgnd.pcx, the themes' popup backgrounds, the dialogs' images and SMK
 * nodes in order, the glue's files, the credits' scripts and what their tags name; the first spelling of a path. */
static int glue_paths(KpUiData *d, const KpFiles *f, Paths *l, KpError *err)
{
	const KpDefsTable *themes, *theme_files, *bins, *glue, *credits, *estb = kp_defs_table(f->profile, "ESTB");
	int nt, ntf, nb, ng, nc, k, j;
	char path[300];
	if (!(themes = names_table(f, "UTHM", &nt, err)) || !(theme_files = names_table(f, "UTHF", &ntf, err))
		|| !(bins = names_table(f, "UBIN", &nb, err)) || !(glue = names_table(f, "UGLF", &ng, err))
		|| !(credits = names_table(f, "UCRD", &nc, err)))
		return -1;
	if (!estb || estb->size < 1) return fail(err, "no compiled ESTB");
	for (k = 0; k < nt; k++) {
		Name t = name_at(themes, k);
		snprintf(path, sizeof path, "glue\\%.*s\\Backgnd.pcx", (int)t.n, (const char *)t.p);
		if (paths_addz(l, path, 1) != 0) goto oom;
	}
	for (j = 0; j < ntf; j++) {
		Name n = name_at(theme_files, j);
		for (k = 0; k < nt; k++) {
			Name t = name_at(themes, k);
			snprintf(path, sizeof path, "glue\\%.*s\\%.*s", (int)t.n, (const char *)t.p, (int)n.n, (const char *)n.p);
			if (paths_addz(l, path, 1) != 0) goto oom;
		}
	}
	for (k = 0; k < nb; k++) {
		Name b = name_at(bins, k);
		size_t size;
		uint8_t *bin;
		snprintf(path, sizeof path, "%.*s", (int)b.n, (const char *)b.p);
		char why[KP_ERROR_SIZE];
		if (!(bin = kp_files_need(f, path, &size, err))) return -1;
		j = kp_ui_bin_files(bin, size, paths_add_dedupe, l, why, sizeof why);
		free(bin);
		if (j != 0) {
			snprintf(err->msg, sizeof err->msg, "ui data: %s: %s", path, why);
			return -1;
		}
	}
	for (k = 0; k < ng; k++) {
		Name n = name_at(glue, k);
		if (paths_add(l, n.p, n.n, 1) != 0) goto oom;
	}
	{ /* credits_paths: the establishing and finale texts (ESTB's names), the credits, the epilogs */
		size_t at = 1;
		int count = estb->data[0];
		char why[KP_ERROR_SIZE];
		for (k = 0; k < count + nc; k++) {
			Name n;
			uint8_t *text;
			size_t size;
			if (k < count) {
				if (at + 1 > estb->size || at + 1 + estb->data[at] + 2 > estb->size) return fail(err, "ESTB is malformed");
				n.p = estb->data + at + 1;
				n.n = estb->data[at];
				at += 1 + n.n + 2;
			} else {
				n = name_at(credits, k - count);
			}
			snprintf(path, sizeof path, "rez\\%.*s.txt", (int)n.n, (const char *)n.p);
			why[0] = 0;
			if (!(text = f->read(f->ctx, path, &size, why, sizeof why))) {
				if (!why[0]) continue; /* a script the data lacks */
				snprintf(err->msg, sizeof err->msg, "%s: %s", path, why);
				return -1;
			}
			if (paths_addz(l, path, 1) != 0) {
				free(text);
				goto oom;
			}
			j = credit_tags(l, text, size);
			free(text);
			if (j != 0) goto oom;
		}
	}
	(void)d;
	return 0;
oom:
	return fail(err, "out of memory");
}

/* A path list read whole into a KpUiFiles (no dedupe). */
static int read_names(KpUiData *d, const KpFiles *f, Paths *l, KpUiFiles *out, KpError *err)
{
	int rc = read_list(d, f, l, out, err);
	paths_free(l);
	return rc;
}

/* DGRP: each theme's Dlg.grp, each theme's arrow.grp, the dialogs' GRPs, the glue's; PCXS: the glue's PCXs. */
static int file_lists(KpUiData *d, const KpFiles *f, KpError *err)
{
	const KpDefsTable *themes, *dgr, *grp, *pcx;
	int nt, nd, ng, np, k;
	Paths l;
	char path[300];
	memset(&l, 0, sizeof l);
	if (!(themes = names_table(f, "UTHM", &nt, err)) || !(dgr = names_table(f, "UDGR", &nd, err))
		|| !(grp = names_table(f, "UGRP", &ng, err)) || !(pcx = names_table(f, "UPCX", &np, err)))
		return -1;
	for (k = 0; k < 2 * nt; k++) {
		Name t = name_at(themes, k % nt);
		snprintf(path, sizeof path, "glue\\%.*s\\%s", (int)t.n, (const char *)t.p, k < nt ? "Dlg.grp" : "arrow.grp");
		if (paths_addz(&l, path, 0) != 0) goto oom;
	}
	for (k = 0; k < nd + ng; k++) {
		Name n = k < nd ? name_at(dgr, k) : name_at(grp, k - nd);
		if (paths_add(&l, n.p, n.n, 0) != 0) goto oom;
	}
	if (read_names(d, f, &l, &d->grps, err) != 0) return -1;
	for (k = 0; k < np; k++) {
		Name n = name_at(pcx, k);
		if (paths_add(&l, n.p, n.n, 0) != 0) goto oom;
	}
	if (read_names(d, f, &l, &d->pcx, err) != 0) return -1;
	if (glue_paths(d, f, &l, err) != 0) {
		paths_free(&l);
		return -1;
	}
	if (read_names(d, f, &l, &d->glue, err) != 0) return -1;
	{ /* TFNT: each theme's tFont.pcx prefix; TEFF: u16 size, each theme's tEffect.pcx */
		uint8_t *tfnt = (uint8_t *)own(d, malloc((size_t)nt * THEME_FONT_ROWS + 1)), *teff = NULL;
		size_t effect = 0;
		if (!tfnt) goto oom;
		for (k = 0; k < nt; k++) {
			Name t = name_at(themes, k);
			uint8_t *px;
			size_t n;
			int32_t w, h;
			snprintf(path, sizeof path, "glue\\%.*s\\tFont.pcx", (int)t.n, (const char *)t.p);
			if (kp_files_pcx_prefix(f, path, tfnt + (size_t)k * THEME_FONT_ROWS, THEME_FONT_ROWS, err) != 0) return -1;
			snprintf(path, sizeof path, "glue\\%.*s\\tEffect.pcx", (int)t.n, (const char *)t.p);
			if (!(px = kp_files_pcx(f, path, &w, &h, err))) return -1;
			n = (size_t)w * (size_t)h;
			if (k == 0) {
				effect = n;
				if (n > 0xffff || !(teff = (uint8_t *)own(d, malloc(2 + (size_t)nt * n)))) {
					free(px);
					return fail(err, n > 0xffff ? "tEffect.pcx is too large" : "out of memory");
				}
				teff[0] = (uint8_t)n;
				teff[1] = (uint8_t)(n >> 8);
			} else if (n != effect) {
				free(px);
				return fail(err, "tEffect.pcx sizes differ between themes");
			}
			memcpy(teff + 2 + (size_t)k * n, px, n);
			free(px);
		}
		d->theme_fonts.data = tfnt;
		d->theme_fonts.size = (size_t)nt * THEME_FONT_ROWS;
		d->theme_effects.data = teff;
		d->theme_effects.size = teff ? 2 + (size_t)nt * effect : 0;
	}
	return 0;
oom:
	paths_free(&l);
	return fail(err, "out of memory");
}

/* GHST (kpd.ghosts_section): frame 0 of the GRP of every building type's flingy sprite's image, one-frame GRPs named
 * "ghost\<images.dat id>", by image id. */
static int ghosts(KpUiData *d, const KpFiles *f, const KpDat *dat, KpError *err)
{
	const KpDatColumn *flags = kp_dat_column(dat, "units", "special_ability_flags"),
		*graphics = kp_dat_column(dat, "units", "graphics"), *sprite = kp_dat_column(dat, "flingy", "sprite"),
		*image = kp_dat_column(dat, "sprites", "image"), *grp_file = kp_dat_column(dat, "images", "grp_file");
	uint8_t *used = NULL, *tbl = NULL;
	size_t tbl_size;
	uint32_t t, i, n = 0;
	int rc = -1;
	if (!flags || !graphics || !sprite || !image || !grp_file) return fail(err, "the DAT lacks a column the ghosts read");
	if (!(used = (uint8_t *)calloc((size_t)grp_file->count + 1, 1))) return fail(err, "out of memory");
	for (t = 0; t < flags->count; t++) {
		if (!(col_at(flags, t) & 1)) continue;
		i = col_at(image, col_at(sprite, col_at(graphics, t)));
		if (i >= grp_file->count) {
			fail(err, "a building's image is outside images.dat");
			goto done;
		}
		if (!used[i]) n++;
		used[i] = 1;
	}
	if (!(tbl = kp_files_need(f, "arr\\images.tbl", &tbl_size, err))) goto done;
	if (!(d->ghosts.files = (KpUiFile *)calloc((size_t)n + 1, sizeof *d->ghosts.files))) {
		fail(err, "out of memory");
		goto done;
	}
	for (i = 0; i < grp_file->count; i++) {
		char path[300], *label;
		const uint8_t *name;
		size_t len, size, one_size;
		uint8_t *grp, *one;
		if (!used[i]) continue;
		if (!(name = tbl_entry(tbl, tbl_size, col_at(grp_file, i), &len)) || len > 250) {
			fail(err, "a building's GRP is outside images.tbl");
			goto done;
		}
		snprintf(path, sizeof path, "unit\\%.*s", (int)len, (const char *)name);
		if (!(grp = kp_files_need(f, path, &size, err))) goto done;
		one = kp_grp_single_frame(grp, size, 0, &one_size);
		free(grp);
		if (!one || !own(d, one)) {
			snprintf(err->msg, sizeof err->msg, "ui data: %s has no frame 0 the game reads", path);
			goto done;
		}
		if (!(label = (char *)own(d, malloc(32)))) {
			fail(err, "out of memory");
			goto done;
		}
		snprintf(label, 32, "ghost\\%u", (unsigned)i);
		d->ghosts.files[d->ghosts.count].path = label;
		d->ghosts.files[d->ghosts.count].path_len = strlen(label);
		d->ghosts.files[d->ghosts.count].data = one;
		d->ghosts.files[d->ghosts.count].size = one_size;
		d->ghosts.count++;
	}
	rc = 0;
done:
	free(used);
	free(tbl);
	return rc;
}

/* fonts.kpd: each font's plaintext and one NUL (FNTP's paths, F8..F16X), TFGM game\tFontGam.pcx's rows. */
static int fonts(KpUiData *d, const KpFiles *f, KpError *err)
{
	const KpDefsTable *t;
	uint8_t *rows;
	int n, k;
	if (!(t = names_table(f, "FNTP", &n, err))) return -1;
	if (n != KP_UI_FNT_FONTS) return fail(err, "FNTP names a count of fonts the interface does not read");
	for (k = 0; k < n; k++) {
		Name p = name_at(t, k);
		char path[300];
		size_t size;
		uint8_t *b;
		snprintf(path, sizeof path, "%.*s", (int)p.n, (const char *)p.p);
		if (!(b = need(d, f, path, &size, err))) return -1;
		d->fonts[k].data = b; /* kp_files' copies end in a NUL past size: the decoded font's */
		d->fonts[k].size = size + 1;
	}
	if (!(rows = (uint8_t *)own(d, malloc(KP_UI_FONT_COLOUR_ROWS)))) return fail(err, "out of memory");
	if (kp_files_pcx_prefix(f, "game\\tFontGam.pcx", rows, KP_UI_FONT_COLOUR_ROWS, err) != 0) return -1;
	d->font_rows.data = rows;
	d->font_rows.size = KP_UI_FONT_COLOUR_ROWS;
	return 0;
}

/* strings.kpd: the TBLs with their text in UTF-8 (kpd.table_utf8). */
static int strings(KpUiData *d, const KpFiles *f, KpError *err)
{
	int k;
	for (k = 0; k < KP_UI_TBLS; k++) {
		size_t size, n;
		uint8_t *b = kp_files_need(f, TBL_FILES[k], &size, err), *u;
		if (!b) return -1;
		u = size >= 2 && 2 + 2 * (size_t)kp_le_u16(b) <= size ? kp_text_table_utf8(b, size, &n) : NULL;
		free(b);
		if (!u || !own(d, u)) {
			snprintf(err->msg, sizeof err->msg, "ui data: %s: its offset table is out of bounds or its text outgrows it",
				TBL_FILES[k]);
			return -1;
		}
		d->tbl[k].data = u;
		d->tbl[k].size = n;
	}
	return 0;
}

#define SOUND_RECORD 22 /* kpd.SOUND_RECORD <HHHIIHHI */

static int sound_pack(const char *path)
{
	int k;
	for (k = 0; k < 3; k++) {
		size_t n = strlen(SOUND_PACKS[k]), i;
		for (i = 0; i < n && path[i] && latin1_lower((uint8_t)path[i]) == (uint8_t)SOUND_PACKS[k][i]; i++) {
		}
		if (i == n) return k + 2;
	}
	return 1;
}

/* MUSL: per MUSI track (u8 count; u8 length, name, kind, fade, next) its data length and nAvgBytesPerSec, zeros for a
 * track whose file is not there or is one the parser rejects. */
int kp_ui_data_music_lengths(KpUiData *d, const KpFiles *f, KpError *err)
{
	const KpDefsTable *musi = kp_defs_table(f->profile, "MUSI");
	uint8_t *musl;
	int tracks, t;
	if (!musi || musi->size < 1) return fail(err, "no MUSI table compiled for the profile");
	tracks = musi->data[0];
	if (!(musl = (uint8_t *)own(d, calloc(1 + 8 * (size_t)tracks, 1)))) return fail(err, "out of memory");
	musl[0] = (uint8_t)tracks;
	for (t = 0; t < tracks; t++) {
		char path[300];
		KpWavInfo w;
		int got, kind;
		if (kp_ui_music_track(musi->data, musi->size, t, path, sizeof path, &kind) != 0)
			return fail(err, "the compiled MUSI table is malformed");
		if (!path[0]) continue;
		if ((got = kp_files_wav(f, path, &w, err)) == -2) return -1;
		if (got <= 0) continue;
		kp_le_put32(musl + 1 + 8 * (size_t)t, w.data_length);
		kp_le_put32(musl + 5 + 8 * (size_t)t, w.avg_bytes_per_sec);
	}
	d->music_lengths.data = musl;
	d->music_lengths.size = 1 + 8 * (size_t)tracks;
	return 0;
}

/* sounds.kpd: SFX (a record per sfxdata.dat sound: "sound\" + its sfxdata.tbl entry), PACK, MUSL (MUSI's tracks). */
static int sounds(KpUiData *d, const KpFiles *f, const KpDat *dat, KpError *err)
{
	const KpDatColumn *file = kp_dat_column(dat, "sfxdata", "sound_file");
	uint8_t *tbl, *sfx, *pack;
	const char **paths;
	size_t tbl_size;
	uint32_t n, k;
	int rc = -1;
	if (!file) return fail(err, "the DAT lacks sfxdata.dat's sound_file");
	n = file->count;
	if (n > 0xffff) return fail(err, "sfxdata.dat has more sounds than a u16 counts");
	if (!(tbl = kp_files_need(f, "arr\\sfxdata.tbl", &tbl_size, err))) return -1;
	sfx = (uint8_t *)own(d, calloc(2 + (size_t)n * SOUND_RECORD, 1));
	pack = (uint8_t *)own(d, calloc(2 + (size_t)n, 1));
	paths = (const char **)own(d, calloc(n ? n : 1, sizeof *paths));
	if (!sfx || !pack || !paths) {
		fail(err, "out of memory");
		goto done;
	}
	kp_le_put16(sfx, n);
	kp_le_put16(pack, n);
	for (k = 0; k < n; k++) {
		uint32_t ref = col_at(file, k);
		uint8_t *r = sfx + 2 + (size_t)k * SOUND_RECORD;
		const uint8_t *name;
		size_t len;
		char path[300];
		KpWavInfo w;
		int got;
		if (!ref) continue;
		if (!(name = tbl_entry(tbl, tbl_size, ref, &len)) || len > 250) {
			fail(err, "a sound is outside sfxdata.tbl");
			goto done;
		}
		snprintf(path, sizeof path, "sound\\%.*s", (int)len, (const char *)name);
		if ((got = kp_files_wav(f, path, &w, err)) == -2) goto done;
		if (got < 0) continue;
		pack[2 + k] = (uint8_t)sound_pack(path);
		kp_le_put16(r, got ? SOUND_PRESENT | SOUND_PARSED : SOUND_PRESENT);
		if (!got) continue; /* the original keeps the sound and never plays it */
		if (!(paths[k] = (const char *)own(d, copy_text(path)))) {
			fail(err, "out of memory");
			goto done;
		}
		kp_le_put16(r + 2, w.format_tag);
		kp_le_put16(r + 4, w.channels);
		kp_le_put32(r + 6, w.sample_rate);
		kp_le_put32(r + 10, w.avg_bytes_per_sec);
		kp_le_put16(r + 14, w.block_align);
		kp_le_put16(r + 16, w.bits_per_sample);
		kp_le_put32(r + 18, w.data_length);
	}
	d->sounds.data = sfx;
	d->sounds.size = 2 + (size_t)n * SOUND_RECORD;
	d->sound_packs.data = pack;
	d->sound_packs.size = 2 + (size_t)n;
	d->sound_paths = paths;
	d->sound_path_count = n;
	if (kp_ui_data_music_lengths(d, f, err) != 0) goto done;
	rc = 0;
done:
	free(tbl);
	return rc;
}

/* portraits.kpd ROWS and FILE (kpd.portrait_set): per portdata.dat row its idle and talking SMKs, "portrait\" + the
 * portdata.tbl stem + a variant digit + ".smk" for each digit whose file exists (0x4a6b40, 0x4a6c50). */
static int portraits(KpUiData *d, const KpFiles *f, const KpDat *dat, KpError *err)
{
	static const char *const KINDS[2] = { "idle_portrait_file", "talking_portrait_file" };
	const KpDatColumn *col[2];
	uint8_t *tbl, *rows;
	size_t tbl_size;
	uint32_t n, row;
	int cap = 0, rc = -1, kind, v;
	col[0] = kp_dat_column(dat, "portdata", KINDS[0]);
	col[1] = kp_dat_column(dat, "portdata", KINDS[1]);
	if (!col[0] || !col[1] || col[0]->count != col[1]->count) return fail(err, "the DAT lacks portdata.dat's portraits");
	n = col[0]->count;
	if (!(tbl = kp_files_need(f, "arr\\portdata.tbl", &tbl_size, err))) return -1;
	if (!(rows = (uint8_t *)own(d, malloc(2 + 8 * (size_t)n)))) {
		fail(err, "out of memory");
		goto done;
	}
	kp_le_put16(rows, n);
	for (row = 0; row < n; row++) {
		for (kind = 0; kind < 2; kind++) {
			uint32_t ref = col_at(col[kind], row), first = NONE16, mask = 0;
			const uint8_t *stem;
			size_t len;
			if (ref) {
				if (!(stem = tbl_entry(tbl, tbl_size, ref, &len)) || len > 250) {
					fail(err, "a portrait is outside portdata.tbl");
					goto done;
				}
				for (v = 0; v < PORTRAIT_VARIANTS; v++) {
					char path[300], *label;
					char why[KP_ERROR_SIZE];
					size_t size;
					uint8_t *b;
					KpUiFile *file;
					snprintf(path, sizeof path, "portrait\\%.*s%d.smk", (int)len, (const char *)stem, v);
					why[0] = 0;
					if (!(b = f->read(f->ctx, path, &size, why, sizeof why))) {
						if (!why[0]) continue; /* no such variant */
						snprintf(err->msg, sizeof err->msg, "%s: %s", path, why);
						goto done;
					}
					if (!own(d, b) || !(label = (char *)own(d, malloc(strlen(path) + 1)))) {
						fail(err, "out of memory");
						goto done;
					}
					if (d->portraits.count == cap) {
						int c = cap ? 2 * cap : 256;
						KpUiFile *g = (KpUiFile *)realloc(d->portraits.files, (size_t)c * sizeof *g);
						if (!g) {
							fail(err, "out of memory");
							goto done;
						}
						d->portraits.files = g;
						cap = c;
					}
					if (first == NONE16) first = (uint32_t)d->portraits.count;
					strcpy(label, path);
					file = &d->portraits.files[d->portraits.count++];
					file->path = label;
					file->path_len = strlen(label);
					file->data = b;
					file->size = size;
					mask |= 1u << v;
				}
			}
			kp_le_put16(rows + 2 + 8 * (size_t)row + 4 * (size_t)kind, first);
			kp_le_put16(rows + 4 + 8 * (size_t)row + 4 * (size_t)kind, mask);
		}
	}
	d->portrait_rows.data = rows;
	d->portrait_rows.size = 2 + 8 * (size_t)n;
	rc = 0;
done:
	free(tbl);
	return rc;
}

int kp_ui_data_from_files(KpUiData *d, const KpFiles *files, const KpDat *dat, const KpUiFile *vector_fonts,
	int vector_font_count, KpError *err)
{
	memset(d, 0, sizeof *d);
	d->profile = files->profile;
	if (vector_font_count > 0) {
		if (!(d->vector_fonts.files = (KpUiFile *)malloc((size_t)vector_font_count * sizeof(KpUiFile)))) {
			return fail(err, "out of memory");
		}
		memcpy(d->vector_fonts.files, vector_fonts, (size_t)vector_font_count * sizeof(KpUiFile));
		d->vector_fonts.count = vector_font_count;
	}
	if (dialogs(d, files, err) != 0 || file_lists(d, files, err) != 0 || ghosts(d, files, dat, err) != 0
		|| fonts(d, files, err) != 0 || strings(d, files, err) != 0 || sounds(d, files, dat, err) != 0
		|| portraits(d, files, dat, err) != 0) {
		kp_ui_data_free(d);
		return -1;
	}
	return 0;
}
