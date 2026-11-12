/* kp_convert manifest (kp_manifest.h). */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "kp_manifest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kp_data.h"
#include "kp_files.h"
#include "kp_grp.h"
#include "kp_json.h"
#include "kp_kpf.h"
#include "kp_present.h"
#include "kp_sha256.h"
#include "kp_smk.h"

#define HEX (2 * KP_SHA256_SIZE + 1)
#define MESSAGE 512
#define PCX_PALETTE_MARKER 0x0c /* the byte before a PCX's trailing palette */
#define PALETTE 768
#define SMK_HEADER 104 /* the fixed header: signature, size, frames, rate, flags, the sounds' sizes and rates, trees */

enum { KIND_OTHER, KIND_PCX, KIND_GRP, KIND_SMK };
enum { STATE_OPENED, STATE_READ, STATE_DAMAGED };

typedef struct {
	char *name;  /* as the game first named it, '\\' apart */
	char *key;   /* that in lower case */
	int state, kind;
	uint32_t size;
	char sha[HEX];
	char palette[HEX];  /* a PCX's (its colours' SHA-256), a GRP's or a movie's by its screen ("" none) */
	uint8_t *colours;   /* a PCX's palette, a movie's when it has one */
	/* a PCX */
	char pixels[HEX], rgb[HEX];
	int32_t w, h;
	uint8_t header[6];
	/* a GRP of plain rows (frames != NULL; pixels its rows' SHA-256) */
	uint32_t box_w, box_h, frame_count;
	uint32_t *frames; /* x, y, w, h, offset each */
	/* a movie */
	int32_t smk[5];
	uint8_t used[256];  /* the indices it draws (with colours: one palette) */
	/* a damaged copy: the committed manifest's row */
	char *kept;
} Entry;

struct KpManifest {
	Entry *entries;
	int count, cap;
	int *slots; /* the entries by key: index + 1, 0 empty */
	size_t slot_cap;
	char **absent;
	int absent_count, absent_cap;
	char err[MESSAGE]; /* the first description that failed */
	void (*log)(void *ctx, const char *line);
	void *log_ctx;
};

/* --- names --- */

static char *spelled(const char *name, int lower)
{
	size_t n = strlen(name), k;
	char *s = (char *)malloc(n + 1);
	if (!s) return NULL;
	for (k = 0; k <= n; k++) {
		char c = name[k] == '/' ? '\\' : name[k];
		s[k] = lower && c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c;
	}
	return s;
}

static uint32_t hash_key(const char *s)
{
	uint32_t h = 2166136261u;
	for (; *s; s++) h = (h ^ (uint8_t)*s) * 16777619u;
	return h;
}

static int grow_slots(KpManifest *m)
{
	size_t cap = m->slot_cap ? 2 * m->slot_cap : 1024, k;
	int *slots = (int *)calloc(cap, sizeof *slots), i;
	if (!slots) return -1;
	for (i = 0; i < m->count; i++) {
		for (k = hash_key(m->entries[i].key) & (cap - 1); slots[k]; k = (k + 1) & (cap - 1)) {}
		slots[k] = i + 1;
	}
	free(m->slots);
	m->slots = slots;
	m->slot_cap = cap;
	return 0;
}

/* The entry of a name, added (opened) when new; NULL out of memory. */
static Entry *entry_of(KpManifest *m, const char *name)
{
	char *key = spelled(name, 1);
	size_t k;
	Entry *e;
	if (!key) return NULL;
	if (2 * (size_t)(m->count + 1) > m->slot_cap && grow_slots(m) != 0) {
		free(key);
		return NULL;
	}
	for (k = hash_key(key) & (m->slot_cap - 1); m->slots[k]; k = (k + 1) & (m->slot_cap - 1)) {
		if (strcmp(m->entries[m->slots[k] - 1].key, key) == 0) {
			free(key);
			return &m->entries[m->slots[k] - 1];
		}
	}
	if (m->count == m->cap) {
		int cap = m->cap ? 2 * m->cap : 1024;
		Entry *g = (Entry *)realloc(m->entries, (size_t)cap * sizeof *g);
		if (!g) {
			free(key);
			return NULL;
		}
		m->entries = g;
		m->cap = cap;
	}
	e = &m->entries[m->count];
	memset(e, 0, sizeof *e);
	e->key = key;
	if (!(e->name = spelled(name, 0))) {
		free(key);
		return NULL;
	}
	m->slots[k] = ++m->count;
	return e;
}

static const char *extension(const char *key)
{
	const char *dot = strrchr(key, '.');
	return dot && !strchr(dot, '\\') ? dot + 1 : "";
}

/* --- what is kept of a file (scdata/casc/rebuild.py describe) --- */

static void hex_of(const uint8_t digest[KP_SHA256_SIZE], char hex[HEX])
{
	kp_sha256_hex(digest, hex);
}

/* The SHA-256 of u16 w, u16 h and a plane (rebuild.py plane_hash). */
static void plane_hash(int32_t w, int32_t h, const uint8_t *plane, size_t size, char hex[HEX])
{
	KpSha256 s;
	uint8_t head[4], digest[KP_SHA256_SIZE];
	kp_le_put16(head, (uint16_t)w);
	kp_le_put16(head + 2, (uint16_t)h);
	kp_sha256_init(&s);
	kp_sha256_update(&s, head, sizeof head);
	kp_sha256_update(&s, plane, size);
	kp_sha256_final(&s, digest);
	hex_of(digest, hex);
}

static int describe_pcx(Entry *e, const uint8_t *data, size_t size, char *err, size_t err_size)
{
	int32_t w, h;
	uint8_t *px = kp_files_pcx_pixels(data, size, &w, &h), *rgb = NULL, digest[KP_SHA256_SIZE];
	size_t k, n;
	if (!px || size < PALETTE + 1 || data[size - PALETTE - 1] != PCX_PALETTE_MARKER || w > 0xffff || h > 0xffff) {
		snprintf(err, err_size, "%s: not an 8-bit PCX with its palette", e->name);
		free(px);
		return -1;
	}
	n = (size_t)w * (size_t)h;
	if (!(rgb = (uint8_t *)malloc(3 * n + 1)) || !(e->colours = (uint8_t *)malloc(PALETTE))) {
		snprintf(err, err_size, "out of memory");
		free(px);
		free(rgb);
		return -1;
	}
	memcpy(e->colours, data + size - PALETTE, PALETTE);
	for (k = 0; k < n; k++) memcpy(rgb + 3 * k, e->colours + 3 * (size_t)px[k], 3);
	plane_hash(w, h, px, n, e->pixels);
	plane_hash(w, h, rgb, 3 * n, e->rgb);
	kp_sha256(e->colours, PALETTE, digest);
	hex_of(digest, e->palette);
	e->w = w;
	e->h = h;
	memcpy(e->header, data + 12, 4);
	memcpy(e->header + 4, data + 68, 2);
	free(px);
	free(rgb);
	e->kind = KIND_PCX;
	return 0;
}

/* A GRP's frame table when its frames are plain rows (rebuild.py uncompressed_grp_layout): frames that fit the file,
 * not all of which decode as rows of packets (kp_grp_decode is grp.py decode). */
static int describe_grp(Entry *e, const uint8_t *data, size_t size, char *err, size_t err_size)
{
	uint32_t count, w, h, k;
	uint8_t digest[KP_SHA256_SIZE];
	int packets = 1;
	e->kind = KIND_GRP;
	if (kp_grp_header(data, size, &count, &w, &h) != 0) {
		snprintf(err, err_size, "%s: not a GRP (its frame table runs past its end)", e->name);
		return -1;
	}
	if (count == 0) return 0;
	for (k = 0; k < count; k++) {
		const uint8_t *r = data + KP_GRP_HEADER + (size_t)KP_GRP_RECORD * k;
		if ((uint64_t)kp_le_u32(r + 4) + (uint64_t)r[2] * r[3] > size) return 0;
	}
	for (k = 0; k < count && packets; k++) packets = kp_grp_decode(data, size, k, NULL, NULL, NULL) == 0;
	if (packets) return 0;
	if (!(e->frames = (uint32_t *)malloc(5 * sizeof *e->frames * count))) {
		snprintf(err, err_size, "out of memory");
		return -1;
	}
	for (k = 0; k < count; k++) {
		const uint8_t *r = data + KP_GRP_HEADER + (size_t)KP_GRP_RECORD * k;
		e->frames[5 * k] = r[0];
		e->frames[5 * k + 1] = r[1];
		e->frames[5 * k + 2] = r[2];
		e->frames[5 * k + 3] = r[3];
		e->frames[5 * k + 4] = kp_le_u32(r + 4);
	}
	e->frame_count = count;
	e->box_w = w;
	e->box_h = h;
	k = KP_GRP_HEADER + KP_GRP_RECORD * count;
	kp_sha256(data + k, size - k, digest);
	hex_of(digest, e->pixels);
	return 0;
}

/* A movie's header and, decoded whole (every frame, a ring movie's extra one too), whether one palette draws it and
 * the indices it draws. */
static int describe_smk(Entry *e, const uint8_t *data, size_t size, char *err, size_t err_size)
{
	KpSmk s;
	uint32_t f, k;
	size_t n;
	int one = 1;
	e->kind = KIND_SMK;
	if (size < SMK_HEADER || (memcmp(data, "SMK2", 4) != 0 && memcmp(data, "SMK4", 4) != 0) || kp_smk_open(&s, data, size) != 0) {
		snprintf(err, err_size, "%s: not a Smacker movie", e->name);
		return -1;
	}
	if (!(e->colours = (uint8_t *)malloc(PALETTE))) {
		kp_smk_close(&s);
		snprintf(err, err_size, "out of memory");
		return -1;
	}
	n = (size_t)s.width * s.height;
	for (f = 0; f < s.frames + s.ring; f++) {
		if (kp_smk_decode(&s) != 0) {
			kp_smk_close(&s);
			snprintf(err, err_size, "%s: frame %u does not decode", e->name, (unsigned)f);
			return -1;
		}
		if (f == 0) memcpy(e->colours, s.palette, PALETTE);
		else if (memcmp(e->colours, s.palette, PALETTE) != 0) one = 0;
		for (k = 0; k < n; k++) e->used[s.pixels[k]] = 1;
	}
	e->smk[0] = (int32_t)s.width;
	e->smk[1] = (int32_t)s.height;
	e->smk[2] = (int32_t)s.frames;
	e->smk[3] = (int32_t)s.ring;
	e->smk[4] = (int32_t)kp_le_u32(data + 16);
	kp_smk_close(&s);
	if (!one) {
		free(e->colours);
		e->colours = NULL;
	}
	return 0;
}

static void describe(KpManifest *m, Entry *e, const uint8_t *data, size_t size)
{
	const char *ext = extension(e->key);
	uint8_t digest[KP_SHA256_SIZE];
	char why[MESSAGE];
	int rc = 0;
	kp_sha256(data, size, digest);
	hex_of(digest, e->sha);
	e->size = (uint32_t)size;
	e->state = STATE_READ;
	why[0] = 0;
	if (strcmp(ext, "pcx") == 0) rc = describe_pcx(e, data, size, why, sizeof why);
	else if (strcmp(ext, "grp") == 0) rc = describe_grp(e, data, size, why, sizeof why);
	else if (strcmp(ext, "smk") == 0) rc = describe_smk(e, data, size, why, sizeof why);
	if (rc != 0 && !m->err[0]) snprintf(m->err, sizeof m->err, "%s", why);
}

/* --- recording --- */

KpManifest *kp_manifest_new(void (*log)(void *ctx, const char *line), void *log_ctx)
{
	KpManifest *m = (KpManifest *)calloc(1, sizeof(KpManifest));
	if (m) {
		m->log = log;
		m->log_ctx = log_ctx;
	}
	return m;
}

void kp_manifest_free(KpManifest *m)
{
	int k;
	if (!m) return;
	for (k = 0; k < m->count; k++) {
		Entry *e = &m->entries[k];
		free(e->name);
		free(e->key);
		free(e->colours);
		free(e->frames);
		free(e->kept);
	}
	for (k = 0; k < m->absent_count; k++) free(m->absent[k]);
	free(m->entries);
	free(m->slots);
	free(m->absent);
	free(m);
}

void kp_manifest_record(void *ctx, const char *name, KpResRecord what, const uint8_t *data, size_t size)
{
	KpManifest *m = (KpManifest *)ctx;
	Entry *e;
	if (what == KP_RES_RECORD_MISSING) {
		if (m->absent_count == m->absent_cap) {
			int cap = m->absent_cap ? 2 * m->absent_cap : 256;
			char **g = (char **)realloc(m->absent, (size_t)cap * sizeof *g);
			if (!g) goto oom;
			m->absent = g;
			m->absent_cap = cap;
		}
		if (!(m->absent[m->absent_count] = spelled(name, 0))) goto oom;
		m->absent_count++;
		return;
	}
	if (!(e = entry_of(m, name))) goto oom;
	if (e->state == STATE_READ) return; /* read whole once: described */
	if (what == KP_RES_RECORD_READ) describe(m, e, data, size);
	else if (what == KP_RES_RECORD_DAMAGED) e->state = STATE_DAMAGED;
	return;
oom:
	if (!m->err[0]) snprintf(m->err, sizeof m->err, "out of memory");
}

int kp_manifest_read_opened(KpManifest *m, KpRes *res, char *err, size_t err_size)
{
	int k;
	for (k = 0; k < m->count; k++) {
		char why[MESSAGE];
		size_t size = 0;
		KpResStatus st;
		if (m->entries[k].state != STATE_OPENED) continue;
		free(kp_res_read(res, m->entries[k].name, &size, &st, why, sizeof why)); /* recorded: read or damaged */
		if (m->entries[k].state == STATE_OPENED) {
			snprintf(err, err_size, "%s: opened, then not read whole (%s)", m->entries[k].name, why[0] ? why : kp_res_status_name(st));
			return -1;
		}
	}
	return 0;
}

/* --- the text (scdata/casc/source.py write_manifest's, as json.dumps writes) --- */

static void json_string(KpBuf *b, const char *s)
{
	kp_buf_char(b, '"');
	for (; *s; s++) {
		uint8_t c = (uint8_t)*s;
		if (c == '"' || c == '\\') kp_buf_printf(b, "\\%c", c);
		else if (c == '\n') kp_buf_str(b, "\\n");
		else if (c == '\r') kp_buf_str(b, "\\r");
		else if (c == '\t') kp_buf_str(b, "\\t");
		else if (c == '\b') kp_buf_str(b, "\\b");
		else if (c == '\f') kp_buf_str(b, "\\f");
		else if (c < 0x20 || c >= 0x80) kp_buf_printf(b, "\\u%04x", (unsigned)c); /* ensure_ascii, a name's Latin-1 */
		else kp_buf_char(b, (char)c);
	}
	kp_buf_char(b, '"');
}

static int by_key(const void *a, const void *b)
{
	return strcmp((*(const Entry *const *)a)->key, (*(const Entry *const *)b)->key);
}

static int by_text(const void *a, const void *b)
{
	return strcmp(*(const char *const *)a, *(const char *const *)b);
}

/* The folder of a key (up to its last '\\'), its length. */
static size_t folder_of(const char *key)
{
	const char *slash = strrchr(key, '\\');
	return slash ? (size_t)(slash - key) : strlen(key);
}

static int ends_with(const char *s, const char *suffix)
{
	size_t n = strlen(s), k = strlen(suffix);
	return n >= k && strcmp(s + n - k, suffix) == 0;
}

/* A damaged copy's row in the committed manifest: its line (without its indent and comma), and a PCX's palette. */
static int keep_row(Entry *e, const char *committed, size_t committed_size, char *err, size_t err_size)
{
	const char *p = committed, *end = committed + committed_size;
	while (p && p < end) {
		const char *line_end = (const char *)memchr(p, '\n', (size_t)(end - p)), *q = p;
		size_t n;
		if (!line_end) line_end = end;
		while (q < line_end && *q == ' ') q++;
		n = (size_t)(line_end - q);
		if (n && q[n - 1] == ',') n--;
		if (n > 2 && q[0] == '[' && q[1] == '"') {
			KpJsonDoc doc;
			const KpJson *row, *extras;
			char why[MESSAGE];
			char *key;
			kp_json_doc_init(&doc);
			if ((row = kp_json_parse(&doc, q, n, why, sizeof why)) != NULL && kp_json_len(row) >= 3
				&& (key = spelled(kp_json_str(kp_json_at(row, 0), ""), 1)) != NULL) {
				int same = strcmp(key, e->key) == 0;
				free(key);
				if (same) {
					if (!(e->kept = (char *)malloc(n + 1))) {
						kp_json_doc_free(&doc);
						snprintf(err, err_size, "out of memory");
						return -1;
					}
					memcpy(e->kept, q, n);
					e->kept[n] = 0;
					e->kind = ends_with(e->key, ".pcx") ? KIND_PCX : KIND_OTHER;
					if (e->kind == KIND_PCX && (extras = kp_json_at(row, 3)) != NULL)
						snprintf(e->palette, sizeof e->palette, "%s", kp_json_field_str(extras, "palette", ""));
					kp_json_doc_free(&doc);
					return 0;
				}
			}
			kp_json_doc_free(&doc);
		}
		p = line_end + 1;
	}
	snprintf(err, err_size, "%s: the reference install's copy cannot be read whole and the manifest has no row of it "
		"(take its size and SHA-256 from a sound copy)", e->name);
	return -1;
}

/* Each GRP of a folder of PCX files takes the palette most of them have (source.py palette_groups); sorted holds the
 * entries by key. */
static void grp_palettes(Entry **sorted, int count)
{
	int k, i, j;
	for (k = 0; k < count; k++) {
		Entry *g = sorted[k];
		size_t folder = folder_of(g->key);
		const char *best = NULL;
		int best_count = 0;
		if (!ends_with(g->key, ".grp") || g->kept) continue;
		for (i = 0; i < count; i++) {
			const Entry *p = sorted[i];
			int n = 0;
			if (p->kind != KIND_PCX || !p->palette[0] || folder_of(p->key) != folder || strncmp(p->key, g->key, folder) != 0)
				continue;
			for (j = 0; j < count; j++) {
				const Entry *q = sorted[j];
				if (q->kind == KIND_PCX && folder_of(q->key) == folder && strncmp(q->key, g->key, folder) == 0
					&& strcmp(q->palette, p->palette) == 0)
					n++;
			}
			if (n > best_count || (n == best_count && strcmp(p->palette, best) < 0)) {
				best = p->palette;
				best_count = n;
			}
		}
		if (best) snprintf(g->palette, sizeof g->palette, "%s", best);
	}
}

/* A movie of one palette is drawn with the PCX palette nearest it at the indices it draws, within the tolerance
 * (rebuild.py screen_palette: the first by SHA-256 of the nearest). */
static void screen_palettes(Entry **sorted, int count)
{
	const Entry **pcx;
	int k, i, n = 0;
	if (!(pcx = (const Entry **)malloc((size_t)(count ? count : 1) * sizeof *pcx))) return;
	for (k = 0; k < count; k++)
		if (sorted[k]->kind == KIND_PCX && sorted[k]->colours) pcx[n++] = sorted[k];
	for (k = 0; k < count; k++) {
		Entry *e = sorted[k];
		int best = KP_MANIFEST_SCREEN_PALETTE_TOLERANCE + 1;
		const char *at = NULL;
		if (e->kind != KIND_SMK || !e->colours) continue;
		for (i = 0; i < n; i++) {
			int diff = 0, v, c;
			for (v = 0; v < 256; v++) {
				if (!e->used[v]) continue;
				for (c = 0; c < 3; c++) {
					int d = abs((int)e->colours[3 * v + c] - (int)pcx[i]->colours[3 * v + c]);
					if (d > diff) diff = d;
				}
			}
			if (diff < best || (diff == best && at && strcmp(pcx[i]->palette, at) < 0)) {
				best = diff;
				at = pcx[i]->palette;
			}
		}
		if (at) snprintf(e->palette, sizeof e->palette, "%s", at);
	}
	free(pcx);
}

static void row(KpBuf *b, const Entry *e)
{
	uint32_t k;
	if (e->kept) {
		kp_buf_str(b, e->kept);
		return;
	}
	kp_buf_char(b, '[');
	json_string(b, e->name);
	kp_buf_printf(b, ", %u, \"%s\"", (unsigned)e->size, e->sha);
	if (e->kind == KIND_PCX) {
		kp_buf_printf(b, ", {\"pixels\": \"%s\", \"rgb\": \"%s\", \"palette\": \"%s\", \"size\": [%d, %d], \"header\": \"",
			e->pixels, e->rgb, e->palette, (int)e->w, (int)e->h);
		for (k = 0; k < 6; k++) kp_buf_printf(b, "%02x", e->header[k]);
		kp_buf_str(b, "\"}");
	} else if (e->kind == KIND_GRP && (e->frames || e->palette[0])) {
		kp_buf_str(b, ", {");
		if (e->frames) {
			kp_buf_printf(b, "\"box\": [%u, %u], \"frames\": [", (unsigned)e->box_w, (unsigned)e->box_h);
			for (k = 0; k < e->frame_count; k++) {
				const uint32_t *f = e->frames + 5 * k;
				kp_buf_printf(b, "%s[%u, %u, %u, %u, %u]", k ? ", " : "", (unsigned)f[0], (unsigned)f[1], (unsigned)f[2],
					(unsigned)f[3], (unsigned)f[4]);
			}
			kp_buf_printf(b, "], \"pixels\": \"%s\"%s", e->pixels, e->palette[0] ? ", " : "");
		}
		if (e->palette[0]) kp_buf_printf(b, "\"palette\": \"%s\"", e->palette);
		kp_buf_char(b, '}');
	} else if (e->kind == KIND_SMK) {
		kp_buf_printf(b, ", {\"smk\": [%d, %d, %d, %d, %d]", (int)e->smk[0], (int)e->smk[1], (int)e->smk[2], (int)e->smk[3],
			(int)e->smk[4]);
		if (e->palette[0]) kp_buf_printf(b, ", \"palette\": \"%s\"", e->palette);
		kp_buf_char(b, '}');
	}
	kp_buf_char(b, ']');
}

typedef struct {
	const char *palette;
	int order; /* the file's place by key */
	const char *name;
} Member;

static int by_palette(const void *a, const void *b)
{
	const Member *x = (const Member *)a, *y = (const Member *)b;
	int c = strcmp(x->palette, y->palette);
	return c ? c : x->order - y->order;
}

char *kp_manifest_text(KpManifest *m, const char *profile_id, const char *committed, size_t committed_size,
	KpManifestCounts *counts, char *err, size_t err_size)
{
	Entry **sorted = NULL;
	char **absent = NULL;
	Member *members = NULL;
	KpBuf b;
	int k, n = 0, member_count = 0, groups = 0, absent_count = 0;
	char *text = NULL;
	memset(counts, 0, sizeof *counts);
	kp_buf_init(&b);
	if (m->err[0]) {
		snprintf(err, err_size, "%s", m->err);
		return NULL;
	}
	if (!(sorted = (Entry **)malloc((size_t)(m->count ? m->count : 1) * sizeof *sorted))
		|| !(absent = (char **)malloc((size_t)(m->absent_count ? m->absent_count : 1) * sizeof *absent))
		|| !(members = (Member *)malloc((size_t)(m->count ? m->count : 1) * sizeof *members))) {
		snprintf(err, err_size, "out of memory");
		goto done;
	}
	for (k = 0; k < m->count; k++) {
		Entry *e = &m->entries[k];
		if (e->state == STATE_OPENED) {
			snprintf(err, err_size, "%s: opened, never read whole", e->name);
			goto done;
		}
		if (e->state == STATE_DAMAGED) {
			if (m->log) {
				char line[MESSAGE];
				snprintf(line, sizeof line, "%s: the reference install's copy cannot be read whole: the manifest's row is kept", e->name);
				m->log(m->log_ctx, line);
			}
			if (keep_row(e, committed ? committed : "", committed ? committed_size : 0, err, err_size) != 0) goto done;
			counts->damaged++;
		}
		sorted[n++] = e;
	}
	qsort(sorted, (size_t)n, sizeof *sorted, by_key);
	grp_palettes(sorted, n);
	screen_palettes(sorted, n);
	kp_buf_printf(&b, "{\n \"comment\": \"The files profile %s's game reads from the 1.17 install: name, size, SHA-256 "
		"(hashes only). kp_res checks a StarCraft: Remastered install against them (tools/defs_c.py compiles them); "
		"written by kp_convert manifest DIR.\",\n \"profile\": ", profile_id);
	json_string(&b, profile_id);
	kp_buf_str(&b, ",\n \"files\": [\n  ");
	for (k = 0; k < n; k++) {
		if (k) kp_buf_str(&b, ",\n  ");
		row(&b, sorted[k]);
		if (sorted[k]->kind == KIND_PCX && sorted[k]->palette[0]) {
			members[member_count].palette = sorted[k]->palette;
			members[member_count].order = k;
			members[member_count++].name = sorted[k]->name;
		}
	}
	/* the names looked for and not found, but for a file read (by key), once each, by key */
	for (k = 0; k < m->absent_count; k++) {
		char *key = spelled(m->absent[k], 1);
		size_t s;
		int found = 0;
		if (!key) {
			snprintf(err, err_size, "out of memory");
			goto done;
		}
		for (s = hash_key(key) & (m->slot_cap - 1); m->slot_cap && m->slots[s]; s = (s + 1) & (m->slot_cap - 1))
			if (strcmp(m->entries[m->slots[s] - 1].key, key) == 0) found = 1;
		if (found) free(key);
		else absent[absent_count++] = key;
	}
	qsort(absent, (size_t)absent_count, sizeof *absent, by_text);
	kp_buf_str(&b, "\n ],\n \"absent\": [\n  ");
	for (k = 0; k < absent_count; k++) {
		int i;
		if (k && strcmp(absent[k], absent[k - 1]) == 0) continue;
		for (i = 0; i < m->absent_count; i++) { /* the first spelling */
			char *key = spelled(m->absent[i], 1);
			int same = key && strcmp(key, absent[k]) == 0;
			free(key);
			if (same) break;
		}
		if (counts->absent) kp_buf_str(&b, ",\n  ");
		json_string(&b, m->absent[i]);
		counts->absent++;
	}
	kp_buf_str(&b, "\n ],\n \"palettes\": {\n  ");
	qsort(members, (size_t)member_count, sizeof *members, by_palette);
	for (k = 0; k < member_count; k++) {
		if (k && strcmp(members[k].palette, members[k - 1].palette) == 0) {
			kp_buf_str(&b, ", ");
		} else {
			if (k) kp_buf_str(&b, "],\n  ");
			kp_buf_printf(&b, "\"%s\": [", members[k].palette);
			groups++;
		}
		json_string(&b, members[k].name);
	}
	kp_buf_str(&b, member_count ? "]\n }\n}\n" : "\n }\n}\n");
	counts->files = n;
	counts->palettes = groups;
	text = b.data;
	b.data = NULL;
done:
	for (k = 0; k < absent_count; k++) free(absent[k]);
	free(absent);
	free(sorted);
	free(members);
	kp_buf_free(&b);
	return text;
}

/* --- the pass --- */

char *kp_manifest_build(const char *dir, const char *committed, size_t committed_size, KpManifestCounts *counts,
	void (*log)(void *ctx, const char *line), void *log_ctx, char *err, size_t err_size)
{
	KpManifest *m = kp_manifest_new(log, log_ctx);
	KpResConfig config;
	KpRes *res = NULL;
	KpData d;
	KpError e;
	char *text = NULL;
	int k, built = 0;
	memset(&config, 0, sizeof config);
	memset(&e, 0, sizeof e);
	config.log = log;
	config.log_ctx = log_ctx;
	config.record = kp_manifest_record;
	config.record_ctx = m;
	if (!m) {
		snprintf(err, err_size, "out of memory");
		return NULL;
	}
	if (!(res = kp_res_open(&dir, 1, &config, err, err_size))) goto done;
	if (kp_res_install(res) != KP_RES_SOURCE_MPQ || kp_res_repacks(res)) {
		snprintf(err, err_size, "%s: the manifest is of a 1.17 install's files, not a %s's", dir,
			kp_res_source_name(kp_res_repacks(res) ? KP_RES_SOURCE_REPACK : kp_res_install(res)));
		goto done;
	}
	if (kp_data_build(&d, res, NULL, 0, NULL, 0, &e) != 0) goto failed;
	built = 1;
	for (k = 0; k < d.mission_count; k++) {
		KpMission mission;
		if (!d.missions[k].directory[0]) continue; /* a test scenario: none without its index */
		if (kp_data_mission(&d, &d.missions[k], NULL, 0, &mission, &e) != 0) goto failed;
		kp_mission_free(&mission);
	}
	for (k = 0; k < KP_TILESETS; k++) {
		KpTileset t;
		if (kp_tileset_from_files(&t, &d.files, k, &e) != 0) goto failed;
		kp_tileset_free(&t);
	}
	if (kp_manifest_read_opened(m, res, err, err_size) != 0) goto done;
	text = kp_manifest_text(m, kp_res_profile(res)->id, committed, committed_size, counts, err, err_size);
	goto done;
failed:
	snprintf(err, err_size, "%s", e.msg);
done:
	if (built) kp_data_free(&d);
	kp_res_close(res);
	kp_manifest_free(m);
	return text;
}
