/* kp_res's rebuilds from StarCraft: Remastered's own formats (kp_res_scr.h). */
#include "kp_res_scr.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kp_index_map.h"
#include "kp_kpf.h"
#include "kp_scr.h"
#include "kp_sha256.h"
#include "kp_webm.h"
#include "zlib.h"

#define NONE16 0xffffu
#define MAKING_MAX 8   /* the files being rebuilt, one inside another (a donor's rebuild inside its user's) */
#define NAME_MAX_LEN 512
#define KPW_HEADER (24 + 768)
#define KPW_RING 1u
#define DIALOG_THEMES_MAX 32

/* The original GRPs Remastered dropped, rebuilt from another GRP drawn with another palette (source.py REMAPPED): the
 * glue's arrow cursors are the game's in each glue palette. */
#define REMAPPED_BASE "arrow.grp"
#define REMAPPED_PREFIX "glue/"
#define REMAPPED_GRP "cursor\\arrow.grp"
#define REMAPPED_WPE "tileset\\badlands.wpe"
#define RC3_SUFFIX "/rc3.xml"

typedef struct {
	const char *s;
	size_t n;
} Str;

typedef struct {
	Str name;
	uint32_t w, h, group;
	const uint8_t *header, *pixels_sha, *rgb_sha;
} PcxEntry;

typedef struct {
	Str name;
	uint32_t group, frames, box_w, box_h;
	const uint8_t *records; /* frames x (u8 x, y, w, h, u32 offset) */
} GrpEntry;

typedef struct {
	Str name;
	uint32_t frames, ring, group;
	int32_t rate;
} SmkEntry;

typedef struct {
	Str name, xml, base;
	uint32_t ids, nul_entries;
	const uint8_t *ids_at, *nuls_at;
} TblEntry;

typedef struct {
	Str name;
	uint32_t raw_size, packed_size, texts;
	const uint8_t *packed, *texts_at;
} BinEntry;

typedef struct {
	uint32_t donors;
	const uint8_t *at; /* the donors' names */
} Group;

/* A file rebuilt before (source.py _rebuilt): its status and the palette it was drawn in; failed: none at the top */
typedef struct {
	char *key;
	int failed;
	KpResStatus status;
	char source[KP_RES_SOURCE_NAME];
	uint8_t palette[768];
	int has_palette;
} Made;

/* A palette found for a group's files in a folder (source.py _palettes) */
typedef struct {
	uint32_t group;
	char *folder;
	uint8_t palette[768];
} Found;

typedef struct {
	char *path;
	KpScrStrings strings;
} Xml;

struct KpResScr {
	Group *groups;
	PcxEntry *pcx;
	GrpEntry *grp;
	SmkEntry *smk;
	TblEntry *tbl;
	BinEntry *bin;
	uint32_t group_count, pcx_count, grp_count, smk_count, tbl_count, bin_count;
	char making[MAKING_MAX][NAME_MAX_LEN];
	int making_count;
	Made *made;
	size_t made_count, made_cap;
	Found *found;
	size_t found_count, found_cap;
	Xml *xml;
	size_t xml_count, xml_cap;
	char themes[DIALOG_THEMES_MAX][NAME_MAX_LEN]; /* the glue themes' Dlg.dds.grp, sorted (source.py _dialog_frames) */
	int theme_count;
};

/* --- the tables --- */

typedef struct {
	const uint8_t *p, *end;
	int bad;
} Cursor;

static const uint8_t *take(Cursor *c, size_t n)
{
	const uint8_t *at = c->p;
	if (c->bad || (size_t)(c->end - c->p) < n) {
		c->bad = 1;
		return NULL;
	}
	c->p += n;
	return at;
}

static uint32_t u8(Cursor *c)
{
	const uint8_t *p = take(c, 1);
	return p ? p[0] : 0;
}
static uint32_t u16(Cursor *c)
{
	const uint8_t *p = take(c, 2);
	return p ? (uint32_t)p[0] | (uint32_t)p[1] << 8 : 0;
}
static uint32_t u32(Cursor *c)
{
	const uint8_t *p = take(c, 4);
	return p ? (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24 : 0;
}
static Str str8(Cursor *c)
{
	Str s;
	s.n = u8(c);
	s.s = (const char *)take(c, s.n);
	if (!s.s) s.n = 0;
	return s;
}

static Cursor table(const KpDefsProfile *p, const char *fourcc)
{
	const KpDefsTable *t = kp_defs_table(p, fourcc);
	Cursor c;
	c.p = t ? t->data : NULL;
	c.end = t ? t->data + t->size : NULL;
	c.bad = !t;
	return c;
}

static int str_is(Str s, const char *key) { return strlen(key) == s.n && memcmp(s.s, key, s.n) == 0; }

static void str_copy(Str s, char *out, size_t size)
{
	size_t n = s.n < size - 1 ? s.n : size - 1;
	memcpy(out, s.s, n);
	out[n] = 0;
}

#define ALLOC_ENTRIES(field, type, count) ((s->field = (type *)calloc((count) ? (count) : 1, sizeof(type))) != NULL)

static int parse(KpResScr *s, const KpDefsProfile *p)
{
	Cursor c;
	uint32_t k, j;
	c = table(p, "SPAL");
	s->group_count = u16(&c);
	if (!ALLOC_ENTRIES(groups, Group, s->group_count)) return -1;
	for (k = 0; k < s->group_count && !c.bad; k++) {
		s->groups[k].donors = u16(&c);
		s->groups[k].at = c.p;
		for (j = 0; j < s->groups[k].donors; j++) str8(&c);
	}
	if (c.bad) return -1;
	c = table(p, "SPCX");
	s->pcx_count = u16(&c);
	if (!ALLOC_ENTRIES(pcx, PcxEntry, s->pcx_count)) return -1;
	for (k = 0; k < s->pcx_count && !c.bad; k++) {
		PcxEntry *e = &s->pcx[k];
		e->name = str8(&c);
		e->w = u16(&c);
		e->h = u16(&c);
		e->header = take(&c, 6);
		e->group = u16(&c);
		e->pixels_sha = take(&c, KP_SHA256_SIZE);
		e->rgb_sha = take(&c, KP_SHA256_SIZE);
	}
	if (c.bad) return -1;
	c = table(p, "SGRP");
	s->grp_count = u16(&c);
	if (!ALLOC_ENTRIES(grp, GrpEntry, s->grp_count)) return -1;
	for (k = 0; k < s->grp_count && !c.bad; k++) {
		GrpEntry *e = &s->grp[k];
		e->name = str8(&c);
		e->group = u16(&c);
		e->frames = u16(&c);
		if (e->frames) {
			e->box_w = u16(&c);
			e->box_h = u16(&c);
			e->records = take(&c, (size_t)8 * e->frames);
		}
	}
	if (c.bad) return -1;
	c = table(p, "SSMK");
	s->smk_count = u16(&c);
	if (!ALLOC_ENTRIES(smk, SmkEntry, s->smk_count)) return -1;
	for (k = 0; k < s->smk_count && !c.bad; k++) {
		SmkEntry *e = &s->smk[k];
		e->name = str8(&c);
		e->frames = u16(&c);
		e->ring = u8(&c);
		e->rate = (int32_t)u32(&c);
		e->group = u16(&c);
	}
	if (c.bad) return -1;
	c = table(p, "STBL");
	s->tbl_count = u16(&c);
	if (!ALLOC_ENTRIES(tbl, TblEntry, s->tbl_count)) return -1;
	for (k = 0; k < s->tbl_count && !c.bad; k++) {
		TblEntry *e = &s->tbl[k];
		e->name = str8(&c);
		e->xml = str8(&c);
		e->base = str8(&c);
		e->ids = u16(&c);
		e->ids_at = c.p;
		for (j = 0; j < e->ids && !c.bad; j++) {
			uint32_t n = u8(&c);
			if (n != 0xff) take(&c, n);
		}
		e->nul_entries = u16(&c);
		e->nuls_at = c.p;
		for (j = 0; j < e->nul_entries && !c.bad; j++) {
			u16(&c);
			take(&c, 2 * (size_t)u16(&c));
		}
	}
	if (c.bad) return -1;
	c = table(p, "SBIN");
	s->bin_count = u16(&c);
	if (!ALLOC_ENTRIES(bin, BinEntry, s->bin_count)) return -1;
	for (k = 0; k < s->bin_count && !c.bad; k++) {
		BinEntry *e = &s->bin[k];
		e->name = str8(&c);
		e->raw_size = u32(&c);
		e->packed_size = u32(&c);
		e->packed = take(&c, e->packed_size);
		e->texts = u16(&c);
		e->texts_at = c.p;
		for (j = 0; j < e->texts && !c.bad; j++) {
			u32(&c);
			u16(&c);
			u8(&c);
			take(&c, 4 * (size_t)u8(&c));
			take(&c, u16(&c));
		}
	}
	return c.bad ? -1 : 0;
}

static int cmp_names(const void *a, const void *b) { return strcmp((const char *)a, (const char *)b); }

KpResScr *kp_res_scr_new(const KpDefsProfile *profile)
{
	KpResScr *s;
	uint32_t k;
	if (!kp_defs_table(profile, "SPAL") || !(s = (KpResScr *)calloc(1, sizeof *s))) return NULL;
	if (parse(s, profile) != 0) {
		kp_res_scr_free(s);
		return NULL;
	}
	/* the glue themes' dialog frames: Remastered's SD/glue/<theme>/dlg.dds.grp of each glue\Pal<theme>\Dlg.grp */
	for (k = 0; k < s->grp_count && s->theme_count < DIALOG_THEMES_MAX; k++) {
		const GrpEntry *e = &s->grp[k];
		char name[NAME_MAX_LEN], *theme;
		size_t n;
		str_copy(e->name, name, sizeof name);
		if (strncmp(name, "glue/pal", 8) != 0 || !(theme = strchr(name + 5, '/'))) continue;
		if (strcmp(theme, "/dlg.grp") != 0) continue;
		for (n = 8; name + n < theme && name[n] >= 'a' && name[n] <= 'z'; n++) {
		}
		if (name + n != theme || n == 8) continue;
		*theme = 0;
		snprintf(s->themes[s->theme_count++], NAME_MAX_LEN, "SD/%s/dlg.dds.grp", name);
	}
	qsort(s->themes, (size_t)s->theme_count, NAME_MAX_LEN, cmp_names);
	return s;
}

void kp_res_scr_free(KpResScr *s)
{
	size_t k;
	if (!s) return;
	free(s->groups);
	free(s->pcx);
	free(s->grp);
	free(s->smk);
	free(s->tbl);
	free(s->bin);
	for (k = 0; k < s->made_count; k++) free(s->made[k].key);
	free(s->made);
	for (k = 0; k < s->found_count; k++) free(s->found[k].folder);
	free(s->found);
	for (k = 0; k < s->xml_count; k++) {
		free(s->xml[k].path);
		kp_scr_strings_free(&s->xml[k].strings);
	}
	free(s->xml);
	free(s);
}

#define FIND_ENTRY(type, list, count) \
	static const type *find_##list(const KpResScr *s, const char *key) \
	{ \
		uint32_t k; \
		for (k = 0; k < s->count; k++) \
			if (str_is(s->list[k].name, key)) return &s->list[k]; \
		return NULL; \
	}
FIND_ENTRY(PcxEntry, pcx, pcx_count)
FIND_ENTRY(GrpEntry, grp, grp_count)
FIND_ENTRY(SmkEntry, smk, smk_count)
FIND_ENTRY(TblEntry, tbl, tbl_count)
FIND_ENTRY(BinEntry, bin, bin_count)

static const char *extension(const char *key)
{
	const char *dot = strrchr(key, '.');
	return dot && !strchr(dot, '/') ? dot + 1 : "";
}

/* rebuild.remastered_xml: RC3.xml's strings carry escapes of their own */
static int is_rc3(const char *path, size_t len)
{
	size_t n = strlen(RC3_SUFFIX), i;
	if (len < n) return 0;
	for (i = 0; i < n; i++) {
		char c = path[len - n + i];
		if ((c >= 'A' && c <= 'Z' ? (char)(c + 32) : c) != RC3_SUFFIX[i]) return 0;
	}
	return 1;
}

static int is_remapped(const char *key)
{
	const char *base = strrchr(key, '/');
	return strncmp(key, REMAPPED_PREFIX, strlen(REMAPPED_PREFIX)) == 0 && base && strcmp(base + 1, REMAPPED_BASE) == 0;
}

/* --- the state: what is being made, what was made, the palettes found --- */

static int making(const KpResScr *s, const char *key)
{
	int k;
	for (k = 0; k < s->making_count; k++)
		if (strcmp(s->making[k], key) == 0) return 1;
	return 0;
}

static Made *made_of(KpResScr *s, const char *key)
{
	size_t k;
	for (k = 0; k < s->made_count; k++)
		if (strcmp(s->made[k].key, key) == 0) return &s->made[k];
	return NULL;
}

static Made *made_add(KpResScr *s, const char *key)
{
	Made *m;
	if (s->made_count == s->made_cap) {
		size_t cap = s->made_cap ? 2 * s->made_cap : 64;
		Made *g = (Made *)realloc(s->made, cap * sizeof *g);
		if (!g) return NULL;
		s->made = g;
		s->made_cap = cap;
	}
	m = &s->made[s->made_count];
	memset(m, 0, sizeof *m);
	if (!(m->key = (char *)malloc(strlen(key) + 1))) return NULL;
	strcpy(m->key, key);
	s->made_count++;
	return m;
}

static void folder_of(const char *key, char *out, size_t size)
{
	const char *slash = strrchr(key, '/');
	size_t n = slash ? (size_t)(slash - key) : strlen(key);
	if (n >= size) n = size - 1;
	memcpy(out, key, n);
	out[n] = 0;
}

static int same_folder(Str name, const char *folder)
{
	size_t n = strlen(folder);
	return name.n > n && memcmp(name.s, folder, n) == 0 && name.s[n] == '/' && !memchr(name.s + n + 1, '/', name.n - n - 1);
}

static int palette_of_pcx(const uint8_t *data, size_t size, uint8_t palette[768])
{
	return kp_scr_pcx_palette(data, size, palette);
}

/* A donor's status and palette read through kp_res (a rebuild inside this one when it has none): 0, -1. */
static int read_donor(KpRes *r, KpResScr *s, const char *donor, KpResStatus *status, uint8_t palette[768])
{
	char err[8];
	size_t size = 0;
	uint8_t *data;
	Made *m = made_of(s, donor);
	if (m && !m->failed && m->has_palette) { /* rebuilt before: its status and palette (source.py _rebuilt) */
		*status = m->status;
		memcpy(palette, m->palette, 768);
		return 0;
	}
	if (!(data = kp_res_read(r, donor, &size, status, err, sizeof err))) return -1;
	if (palette_of_pcx(data, size, palette) != 0) {
		free(data);
		return -1;
	}
	free(data);
	return 0;
}

/* source.py _find_palette: the palette of the first donor Remastered keeps exactly, else of one it rebuilds with the
 * original's palette (one level: a donor's donors must be exact), else of a PCX beside the file it keeps or rebuilds.
 * 0, -1 when none. */
static int find_palette(KpRes *r, KpResScr *s, const char *key, uint32_t group, uint8_t palette[768])
{
	char folder[NAME_MAX_LEN], donor[NAME_MAX_LEN];
	const Group *g = group < s->group_count ? &s->groups[group] : NULL;
	int pass;
	uint32_t k;
	folder_of(key, folder, sizeof folder);
	/* the donors in the converter's order: those of the same folder first */
	for (pass = 0; pass < 2 && g; pass++) {
		Cursor c;
		c.p = g->at;
		c.end = g->at + 0x7fffffff;
		c.bad = 0;
		for (k = 0; k < g->donors; k++) {
			Str d = str8(&c);
			size_t size = 0;
			uint8_t *data;
			if (same_folder(d, folder) != !pass) continue;
			str_copy(d, donor, sizeof donor);
			if (strcmp(donor, key) == 0) continue;
			if ((data = kp_res_casc_exact(r, donor, &size)) != NULL) {
				int rc = palette_of_pcx(data, size, palette);
				free(data);
				return rc;
			}
		}
	}
	if (s->making_count > 1) return -1;
	for (pass = 0; pass < 2 && g; pass++) {
		Cursor c;
		c.p = g->at;
		c.end = g->at + 0x7fffffff;
		c.bad = 0;
		for (k = 0; k < g->donors; k++) {
			Str d = str8(&c);
			KpResStatus st;
			if (same_folder(d, folder) != !pass) continue;
			str_copy(d, donor, sizeof donor);
			if (strcmp(donor, key) == 0 || making(s, donor)) continue;
			if (read_donor(r, s, donor, &st, palette) == 0 && (st == KP_RES_DERIVED || st == KP_RES_EQUIVALENT)) return 0;
		}
	}
	/* no PCX shares its palette: a neighbour's (pixels of colours it lacks take the nearest) */
	for (k = 0; k < s->pcx_count && s->making_count == 1; k++) {
		KpResStatus st;
		if (!same_folder(s->pcx[k].name, folder)) continue;
		str_copy(s->pcx[k].name, donor, sizeof donor);
		if (making(s, donor)) continue;
		if (read_donor(r, s, donor, &st, palette) == 0
			&& (st == KP_RES_EXACT || st == KP_RES_DERIVED || st == KP_RES_EQUIVALENT))
			return 0;
	}
	return -1;
}

/* source.py _palette: find_palette's, kept per group and folder */
static int palette_for(KpRes *r, KpResScr *s, const char *key, uint32_t group, uint8_t palette[768])
{
	char folder[NAME_MAX_LEN];
	size_t k;
	Found *f;
	folder_of(key, folder, sizeof folder);
	for (k = 0; k < s->found_count; k++) {
		if (s->found[k].group == group && strcmp(s->found[k].folder, folder) == 0) {
			memcpy(palette, s->found[k].palette, 768);
			return 0;
		}
	}
	if (find_palette(r, s, key, group, palette) != 0) return -1;
	if (s->found_count == s->found_cap) {
		size_t cap = s->found_cap ? 2 * s->found_cap : 32;
		Found *g = (Found *)realloc(s->found, cap * sizeof *g);
		if (!g) return 0;
		s->found = g;
		s->found_cap = cap;
	}
	f = &s->found[s->found_count];
	if (!(f->folder = (char *)malloc(strlen(folder) + 1))) return 0;
	strcpy(f->folder, folder);
	f->group = group;
	memcpy(f->palette, palette, 768);
	s->found_count++;
	return 0;
}

/* --- reading Remastered's files --- */

/* source.py _first: the first of the names the storage reads */
static uint8_t *first_of(KpCasc *casc, const char *const *names, int count, size_t *size, char *source)
{
	int k;
	for (k = 0; k < count; k++) {
		char why[256];
		uint8_t *b = kp_casc_read(casc, names[k], size, why, sizeof why);
		if (b) {
			snprintf(source, KP_RES_SOURCE_NAME, "CASC %s", names[k]);
			return b;
		}
	}
	return NULL;
}

/* "SD/" + stem + suffix, then stem + suffix */
static uint8_t *sd_or_root(KpCasc *casc, const char *key, const char *suffix, size_t *size, char *source)
{
	char a[NAME_MAX_LEN], b[NAME_MAX_LEN];
	const char *names[2];
	const char *dot = strrchr(key, '.');
	int stem = dot ? (int)(dot - key) : (int)strlen(key);
	snprintf(a, sizeof a, "SD/%.*s%s", stem, key, suffix);
	snprintf(b, sizeof b, "%.*s%s", stem, key, suffix);
	names[0] = a;
	names[1] = b;
	return first_of(casc, names, 2, size, source);
}

static int digest_is(const uint8_t *data, size_t size, const uint8_t *want)
{
	uint8_t d[KP_SHA256_SIZE];
	kp_sha256(data, size, d);
	return memcmp(d, want, KP_SHA256_SIZE) == 0;
}

static int hex_digest(const char *hex, uint8_t out[KP_SHA256_SIZE])
{
	int k;
	for (k = 0; k < KP_SHA256_SIZE; k++) {
		unsigned v;
		if (sscanf(hex + 2 * k, "%2x", &v) != 1) return -1;
		out[k] = (uint8_t)v;
	}
	return 0;
}

/* source.py _judged */
static KpResStatus judged(const KpDefsInput *in, const uint8_t *data, size_t size)
{
	uint8_t want[KP_SHA256_SIZE];
	return hex_digest(in->sha256, want) == 0 && size == in->size && digest_is(data, size, want) ? KP_RES_DERIVED
		: KP_RES_APPROXIMATION;
}

/* rebuild.plane_hash: SHA-256 of u16 width, u16 height and the plane */
static void plane_hash(uint32_t w, uint32_t h, const uint8_t *plane, size_t size, uint8_t out[KP_SHA256_SIZE])
{
	KpSha256 c;
	uint8_t head[4];
	head[0] = (uint8_t)w;
	head[1] = (uint8_t)(w >> 8);
	head[2] = (uint8_t)h;
	head[3] = (uint8_t)(h >> 8);
	kp_sha256_init(&c);
	kp_sha256_update(&c, head, 4);
	kp_sha256_update(&c, plane, size);
	kp_sha256_final(&c, out);
}

/* --- the rebuilds --- */

static uint8_t *remake_pcx(KpRes *r, KpResScr *s, KpCasc *casc, const char *key, const PcxEntry *e,
	const KpDefsInput *in, size_t *size, KpResStatus *status, char *source, uint8_t palette[768])
{
	size_t dds_size = 0, n = (size_t)e->w * e->h;
	uint8_t *dds = sd_or_root(casc, key, ".dds", &dds_size, source), *pixels = NULL, *out = NULL;
	uint8_t digest[KP_SHA256_SIZE];
	KpScrImage im;
	KpIndexMap map;
	uint32_t above;
	char why[KP_SCR_ERR];
	int no_palette = palette_for(r, s, key, e->group, palette) != 0; /* looked up either way, as source.py does */
	if (!dds || no_palette) {
		free(dds);
		return NULL;
	}
	if (kp_scr_dds(dds, dds_size, &im, why, sizeof why) != 0 || kp_scr_fit(&im, e->w, e->h, &above) != 0
		|| !(pixels = (uint8_t *)calloc(n ? n : 1, 1)) || kp_index_map_init(&map, palette) != 0) {
		kp_scr_image_free(&im);
		free(dds);
		free(pixels);
		return NULL;
	}
	free(dds);
	kp_scr_indices(&im, &map, pixels + (size_t)e->w * above);
	kp_index_map_free(&map);
	kp_scr_image_free(&im);
	out = kp_scr_pcx(e->w, e->h, pixels, palette, e->header, size);
	if (out) {
		*status = judged(in, out, *size);
		if (*status != KP_RES_DERIVED) {
			uint8_t *rgb;
			plane_hash(e->w, e->h, pixels, n, digest);
			if (memcmp(digest, e->pixels_sha, KP_SHA256_SIZE) == 0) {
				*status = KP_RES_EQUIVALENT;
			} else if ((rgb = (uint8_t *)malloc(3 * n + 1)) != NULL) {
				size_t i;
				for (i = 0; i < n; i++) memcpy(rgb + 3 * i, palette + 3 * pixels[i], 3);
				plane_hash(e->w, e->h, rgb, 3 * n, digest);
				if (memcmp(digest, e->rgb_sha, KP_SHA256_SIZE) == 0) *status = KP_RES_COLOUR_EQUAL;
				free(rgb);
			}
		}
	}
	free(pixels);
	return out;
}

/* A dialog GRP of plain rows from Remastered's DXT frames of it: its own theme's, else another theme's (the same art
 * in other colours) */
static uint8_t *remake_plain_grp(KpCasc *casc, KpResScr *s, const char *key, const GrpEntry *e, const KpDefsInput *in,
	const uint8_t palette[768], size_t *size, KpResStatus *status, char *source)
{
	char own[2][NAME_MAX_LEN];
	const char *dot = strrchr(key, '.');
	int stem = dot ? (int)(dot - key) : (int)strlen(key), k, count;
	uint8_t *data = NULL, *out = NULL, **frames = NULL;
	KpScrDdsEntry *images = NULL;
	KpScrGrpRecord *records = NULL;
	uint32_t n = 0, f;
	snprintf(own[0], NAME_MAX_LEN, "SD/%.*s.dds.grp", stem, key);
	snprintf(own[1], NAME_MAX_LEN, "%.*s.dds.grp", stem, key);
	if (!(records = (KpScrGrpRecord *)calloc(e->frames, sizeof *records))) return NULL;
	for (f = 0; f < e->frames; f++) {
		const uint8_t *p = e->records + 8 * (size_t)f;
		records[f].x = p[0];
		records[f].y = p[1];
		records[f].w = p[2];
		records[f].h = p[3];
		records[f].offset = (uint32_t)p[4] | (uint32_t)p[5] << 8 | (uint32_t)p[6] << 16 | (uint32_t)p[7] << 24;
	}
	count = 2 + s->theme_count;
	for (k = 0; k < count && !images; k++) {
		const char *name = k < 2 ? own[k] : s->themes[k - 2];
		size_t got = 0;
		int fits = 1;
		data = first_of(casc, &name, 1, &got, source);
		if (data && kp_scr_dds_container(data, got, &images, &n) == 0 && n >= e->frames) {
			for (f = 0; f < e->frames && fits; f++)
				fits = (images[f].width == records[f].w && images[f].height == records[f].h)
					|| kp_scr_fits(images[f].width, images[f].height, records[f].w, records[f].h);
			if (fits) break;
		}
		free(images);
		images = NULL;
		free(data);
		data = NULL;
	}
	if (!images || !(frames = (uint8_t **)calloc(e->frames, sizeof *frames))) goto done;
	for (f = 0; f < e->frames; f++) {
		KpScrImage im;
		KpIndexMap map;
		uint32_t above;
		char why[KP_SCR_ERR];
		size_t w = records[f].w, h = records[f].h;
		if (!(frames[f] = (uint8_t *)calloc(w * h > 0 ? w * h : 1, 1))) goto done;
		if (kp_scr_dds(images[f].dds, images[f].size, &im, why, sizeof why) != 0) goto done;
		if (kp_scr_fit(&im, (uint32_t)w, (uint32_t)h, &above) != 0 || kp_index_map_init(&map, palette) != 0) {
			kp_scr_image_free(&im);
			goto done;
		}
		kp_scr_indices(&im, &map, frames[f] + w * above);
		kp_index_map_free(&map);
		kp_scr_image_free(&im);
	}
	if ((out = kp_scr_plain_grp(e->box_w, e->box_h, records, e->frames, frames, in->size)) != NULL) {
		*size = in->size;
		*status = judged(in, out, *size);
	}
done:
	if (frames)
		for (f = 0; f < e->frames; f++) free(frames[f]);
	free(frames);
	free(images);
	free(data);
	free(records);
	return out;
}

static uint8_t *remake_grp(KpRes *r, KpResScr *s, KpCasc *casc, const char *key, const KpDefsInput *in, size_t *size,
	KpResStatus *status, char *source, uint8_t palette[768])
{
	const GrpEntry *e = find_grp(s, key);
	char err[8];
	size_t grp_size = 0, wpe_size = 0;
	uint8_t *grp, *wpe, *out;
	KpResStatus st;
	KpIndexMap map;
	if (palette_for(r, s, key, e ? e->group : NONE16, palette) != 0) return NULL; /* first, as source.py does */
	if (e && e->frames) return remake_plain_grp(casc, s, key, e, in, palette, size, status, source);
	if (!is_remapped(key)) return NULL;
	if (!(grp = kp_res_read(r, REMAPPED_GRP, &grp_size, &st, err, sizeof err))) return NULL;
	if (!(wpe = kp_res_read(r, REMAPPED_WPE, &wpe_size, &st, err, sizeof err)) || kp_index_map_init(&map, palette) != 0) {
		free(grp);
		free(wpe);
		return NULL;
	}
	out = kp_scr_remap_grp(grp, grp_size, wpe, wpe_size, &map, size);
	kp_index_map_free(&map);
	free(grp);
	free(wpe);
	if (out) {
		*status = judged(in, out, *size);
		snprintf(source, KP_RES_SOURCE_NAME, "%s", REMAPPED_GRP);
	}
	return out;
}

/* A Remastered XML's strings, read once (source.py _xml and remastered_xml) */
static const KpScrStrings *xml_of(KpResScr *s, KpCasc *casc, const char *path, size_t len)
{
	size_t k, size = 0;
	char name[NAME_MAX_LEN], why[KP_SCR_ERR];
	uint8_t *raw;
	Xml *x;
	for (k = 0; k < s->xml_count; k++)
		if (strlen(s->xml[k].path) == len && memcmp(s->xml[k].path, path, len) == 0) return &s->xml[k].strings;
	if (len >= sizeof name) return NULL;
	memcpy(name, path, len);
	name[len] = 0;
	if (s->xml_count == s->xml_cap) {
		size_t cap = s->xml_cap ? 2 * s->xml_cap : 8;
		Xml *g = (Xml *)realloc(s->xml, cap * sizeof *g);
		if (!g) return NULL;
		s->xml = g;
		s->xml_cap = cap;
	}
	x = &s->xml[s->xml_count];
	memset(x, 0, sizeof *x);
	if (!(x->path = (char *)malloc(len + 1))) return NULL;
	memcpy(x->path, name, len + 1);
	raw = kp_casc_read(casc, name, &size, why, sizeof why); /* none: no strings (every text of it empty) */
	if (raw) {
		if (kp_scr_xml_strings(raw, size, is_rc3(name, len), &x->strings, why, sizeof why) != 0)
			memset(&x->strings, 0, sizeof x->strings);
		free(raw);
	}
	s->xml_count++;
	return &x->strings;
}

static uint8_t *remake_tbl(KpCasc *casc, const TblEntry *e, const KpDefsInput *in, size_t *size,
	KpResStatus *status, char *source)
{
	char xml_name[NAME_MAX_LEN], base_name[NAME_MAX_LEN], why[KP_SCR_ERR];
	size_t xml_size = 0, base_size = 0;
	uint8_t *xml, *base = NULL, *out = NULL, **owned = NULL;
	KpScrSpan *base_spans = NULL, *spans = NULL;
	uint32_t base_count = 0, k, j;
	KpScrStrings strings;
	Cursor ids, nuls;
	str_copy(e->xml, xml_name, sizeof xml_name);
	if (!(xml = kp_casc_read(casc, xml_name, &xml_size, why, sizeof why))) return NULL;
	if (e->base.n) {
		str_copy(e->base, base_name, sizeof base_name);
		if ((base = kp_casc_read(casc, base_name, &base_size, why, sizeof why)) != NULL
			&& kp_scr_tbl_spans(base, base_size, &base_spans, &base_count) != 0) {
			free(base);
			free(xml);
			return NULL;
		}
	}
	if (kp_scr_xml_strings(xml, xml_size, is_rc3(xml_name, strlen(xml_name)), &strings, why, sizeof why) != 0) goto done;
	if (!(spans = (KpScrSpan *)calloc(e->ids ? e->ids : 1, sizeof *spans))) goto strings_done;
	ids.p = e->ids_at;
	ids.end = e->ids_at + 0x7fffffff;
	ids.bad = 0;
	for (k = 0; k < e->ids; k++) {
		uint32_t n = u8(&ids);
		const char *id = n == 0xff ? NULL : (const char *)take(&ids, n);
		if (base && k < base_count) {
			spans[k] = base_spans[k];
		} else if (id && n) {
			const KpScrString *v = kp_scr_string(&strings, id, n);
			spans[k].data = v ? v->value : NULL;
			spans[k].size = v ? v->size : 0;
		}
	}
	/* the NULs inside a string that the XML writes as 0x07 (rebuild.span_from_xml), in copies of the values */
	if (!(owned = (uint8_t **)calloc(e->ids ? e->ids : 1, sizeof *owned))) goto spans_done;
	nuls.p = e->nuls_at;
	nuls.end = e->nuls_at + 0x7fffffff;
	nuls.bad = 0;
	for (k = 0; k < e->nul_entries; k++) {
		uint32_t at = u16(&nuls), count = u16(&nuls);
		const uint8_t *positions = take(&nuls, 2 * (size_t)count);
		if (at >= e->ids || (base && at < base_count) || !spans[at].size || owned[at]) continue;
		if (!(owned[at] = (uint8_t *)malloc(spans[at].size))) goto spans_done;
		memcpy(owned[at], spans[at].data, spans[at].size);
		for (j = 0; j < count; j++) {
			uint32_t i = (uint32_t)positions[2 * j] | (uint32_t)positions[2 * j + 1] << 8;
			if (i < spans[at].size && owned[at][i] == KP_SCR_TBL_NUL_IN_XML) owned[at][i] = 0;
		}
		spans[at].data = owned[at];
	}
	out = kp_scr_tbl(spans, e->ids, size);
	if (out) {
		*status = judged(in, out, *size);
		snprintf(source, KP_RES_SOURCE_NAME, "CASC %s", xml_name);
	}
spans_done:
	if (owned)
		for (k = 0; k < e->ids; k++) free(owned[k]);
	free(owned);
	free(spans);
strings_done:
	kp_scr_strings_free(&strings);
done:
	free(base_spans);
	free(base);
	free(xml);
	return out;
}

/* A dialog BIN from its skeleton and its texts (rebuild.dialog_from_table): a name as the table keeps it, a Remastered
 * string by its source "id:<xml>#<id>" (empty when it has none) */
static uint8_t *remake_bin(KpResScr *s, KpCasc *casc, const BinEntry *e, const KpDefsInput *in, size_t *size,
	KpResStatus *status, char *source)
{
	uLongf raw_size = e->raw_size;
	uint8_t *skeleton = (uint8_t *)malloc(e->raw_size ? e->raw_size : 1), *out = NULL;
	KpScrDialogText *texts = (KpScrDialogText *)calloc(e->texts ? e->texts : 1, sizeof *texts);
	Cursor c;
	uint32_t k;
	if (!skeleton || !texts || uncompress(skeleton, &raw_size, e->packed, e->packed_size) != Z_OK || raw_size != e->raw_size)
		goto done;
	c.p = e->texts_at;
	c.end = e->texts_at + 0x7fffffff;
	c.bad = 0;
	for (k = 0; k < e->texts; k++) {
		KpScrDialogText *t = &texts[k];
		uint32_t n;
		const char *src;
		t->offset = u32(&c);
		t->length = u16(&c);
		t->hotkey = (int)u8(&c);
		t->pointer_count = u8(&c);
		t->pointers = take(&c, 4 * (size_t)t->pointer_count);
		n = u16(&c);
		src = (const char *)take(&c, n);
		if (n >= 5 && memcmp(src, "name:", 5) == 0) {
			t->text = (const uint8_t *)src + 5;
			t->text_size = n - 5;
		} else if (n >= 3 && memcmp(src, "id:", 3) == 0) {
			const char *hash = (const char *)memchr(src + 3, '#', n - 3);
			const KpScrStrings *strings = hash ? xml_of(s, casc, src + 3, (size_t)(hash - src - 3)) : NULL;
			const KpScrString *v = strings ? kp_scr_string(strings, hash + 1, (size_t)(src + n - hash - 1)) : NULL;
			if (v) {
				t->text = v->value;
				t->text_size = v->size;
			}
		}
	}
	if (!c.bad && (out = kp_scr_dialog(skeleton, e->raw_size, texts, e->texts, size)) != NULL) {
		*status = judged(in, out, *size);
		snprintf(source, KP_RES_SOURCE_NAME, "defs/scr texts");
	}
done:
	free(skeleton);
	free(texts);
	return out;
}

/* A movie only Remastered's WebM has, as the KPW1 that stands in for its SMK (kp_smk.h): the SMK's size, frame count,
 * rate and ring flag, the palette of its screen (its indices are that palette's), then the WebM */
static uint8_t *remake_smk(KpRes *r, KpResScr *s, KpCasc *casc, const char *key, const SmkEntry *e, size_t *size,
	KpResStatus *status, char *source, uint8_t palette[768])
{
	size_t webm_size = 0;
	uint8_t *webm = sd_or_root(casc, key, ".webm", &webm_size, source), *out;
	uint32_t w, h;
	if (!webm) return NULL;
	/* TODO: a movie with a palette of its own (none in profile bw-1.17.0.1: every SMK's palette is a screen's) would
	 * need its colours, which the converter took with FFmpeg's palettegen */
	/* the size the WebM's, which may differ from the SMK's (converter/formats/kpw.py) */
	if (e->group == NONE16 || palette_for(r, s, key, e->group, palette) != 0
		|| kp_webm_size(webm, webm_size, &w, &h) != 0 || !(out = (uint8_t *)malloc(KPW_HEADER + webm_size + 1))) {
		free(webm);
		return NULL;
	}
	memcpy(out, KP_RES_KPW_MAGIC, 4);
	kp_le_put32(out + 4, w);
	kp_le_put32(out + 8, h);
	kp_le_put32(out + 12, e->frames);
	kp_le_put32(out + 16, (uint32_t)e->rate);
	kp_le_put32(out + 20, e->ring ? KPW_RING : 0);
	memcpy(out + 24, palette, 768);
	memcpy(out + KPW_HEADER, webm, webm_size);
	free(webm);
	*size = KPW_HEADER + webm_size;
	*status = KP_RES_APPROXIMATION;
	return out;
}

uint8_t *kp_res_scr_remake(KpRes *r, KpResScr *s, KpCasc *casc, const char *key, const KpDefsInput *in, size_t *size,
	KpResStatus *status, char *source)
{
	const char *ext = extension(key);
	uint8_t palette[768], *out = NULL;
	int has_palette = 0;
	Made *m = made_of(s, key);
	const void *e;
	if ((m && m->failed) || making(s, key) || s->making_count >= MAKING_MAX) return NULL;
	snprintf(s->making[s->making_count++], NAME_MAX_LEN, "%s", key);
	if (strcmp(ext, "pcx") == 0 && (e = find_pcx(s, key)) != NULL) {
		out = remake_pcx(r, s, casc, key, (const PcxEntry *)e, in, size, status, source, palette);
		has_palette = 1;
	} else if (strcmp(ext, "grp") == 0) {
		out = remake_grp(r, s, casc, key, in, size, status, source, palette);
		has_palette = 1;
	} else if (strcmp(ext, "tbl") == 0 && (e = find_tbl(s, key)) != NULL) {
		out = remake_tbl(casc, (const TblEntry *)e, in, size, status, source);
	} else if (strcmp(ext, "bin") == 0 && (e = find_bin(s, key)) != NULL) {
		out = remake_bin(s, casc, (const BinEntry *)e, in, size, status, source);
	} else if (strcmp(ext, "smk") == 0 && (e = find_smk(s, key)) != NULL) {
		out = remake_smk(r, s, casc, key, (const SmkEntry *)e, size, status, source, palette);
		has_palette = 1;
	}
	s->making_count--;
	/* kept: a success, and a failure at the top (a rebuild inside another may fail only for want of its donors) */
	if (!m && (out || s->making_count == 0) && (m = made_add(s, key)) != NULL) {
		m->failed = !out;
		if (out) {
			m->status = *status;
			snprintf(m->source, sizeof m->source, "%s", source);
			m->has_palette = has_palette;
			if (has_palette) memcpy(m->palette, palette, 768);
		}
	}
	return out;
}
