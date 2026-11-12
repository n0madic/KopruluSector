/* The original data read at run time (kp_res.h, ADR 0018). */
#include "kp_res.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kp_casc.h"
#include "kp_mpq.h"
#include "kp_res_scr.h"
#include "kp_sha256.h"

#define KP_RES_NAME 512
#define KP_RES_ARCHIVES 8 /* an install's archives (the profile's) */
#define KP_RES_REPACK_MAX 128 /* a repack's archives: the core or single file and its groups */
#define KP_RES_GROUPS 128     /* the groups the core's listing names */
#define KP_RES_GROUP_FILE 128
#define KP_RES_MESSAGE 1400

/* Where StarCraft: Remastered keeps the profile's files: its SD copies first, since the copies at the root are its own
 * where both exist; the enUS locale last (converter/casc/source.py PREFIXES). */
static const char *const CASC_PREFIXES[] = { "SD/", "", "locales/enUS/Assets/SD/", "locales/enUS/Assets/" };
#define CASC_PREFIX_COUNT ((int)(sizeof CASC_PREFIXES / sizeof CASC_PREFIXES[0]))

/* Original files Remastered keeps under another name (converter/casc/source.py MOVED) */
static const struct {
	const char *name, *casc;
} CASC_MOVED[] = {
	{ "dlgs/protoss.grp", "game/consoles/protoss/dlg.grp" },
	{ "dlgs/terran.grp", "game/consoles/terran/dlg.grp" },
	{ "dlgs/zerg.grp", "game/consoles/zerg/dlg.grp" },
	{ "files/font/font8.fnt", "SD/font/font8.fnt" },
	{ "files/font/font10.fnt", "SD/font/font10.fnt" },
	{ "files/font/font14.fnt", "SD/font/font14.fnt" },
	{ "files/font/font16.fnt", "SD/font/font16.fnt" },
	{ "files/font/font16x.fnt", "SD/font/font16x.fnt" },
	{ "glue/palnl/popopup.pcx", "glue/Battlenet/ToonRegistry/popopup.pcx" },
	{ "unit/cmdbtns/cmdicons.grp", "unit/cmdicons/cmdicons.grp" },
	{ "unit/cmdbtns/pcmdbtns.grp", "game/consoles/protoss/cmdbtns.grp" },
	{ "unit/cmdbtns/tcmdbtns.grp", "game/consoles/terran/cmdbtns.grp" },
	{ "unit/cmdbtns/zcmdbtns.grp", "game/consoles/zerg/cmdbtns.grp" },
	{ "unit/cmdbtns/ticon.pcx", "game/ticon.pcx" },
};
#define CASC_MOVED_COUNT ((int)(sizeof CASC_MOVED / sizeof CASC_MOVED[0]))

/* Tileset files Remastered grows by appending records: the original is the prefix of the manifest's size; CV5 groups
 * empty in 1.17 that Remastered fills, [first, end) (converter/casc/source.py GROWN, CV5_FILLED). */
static const char *const CASC_GROWN[] = { ".cv5", ".vf4", ".vr4" };
#define CV5_GROUP 52
static const struct {
	const char *tileset;
	int first, end;
} CV5_FILLED[] = { { "desert", 770, 1024 }, { "platform", 933, 1024 }, { "twilight", 797, 1024 } };
#define VX4EX ".vx4ex"

typedef struct {
	char *name;       /* normalized; NULL: an empty slot */
	char *source;     /* the seen table's: where it was read from */
	uint32_t size;    /* the repack table's: the listed size, the archive */
	int archive;
	int status;       /* the seen table's: what the read found; the repack table's: the listed status */
	uint8_t *head;    /* the repack table's Ogg of a WAV: the WAV's header and size */
	uint32_t head_size, wav_size;
	uint8_t sha256[KP_SHA256_SIZE]; /* the repack table's: the stored bytes' listed SHA-256 */
	int ranged;       /* the raw table's: placed by the core's listing in a group read by ranges (archive -1 until it opens) */
	int group;        /* that group (KpRes.groups) */
	uint64_t offset;  /* and the bytes' offset in its file */
} Entry;

typedef struct {
	Entry *slots;
	size_t count, cap;
} Map;

typedef struct {
	KpMpq *mpq;
	char path[KP_RES_PATH];
} Archive;

typedef struct {
	KpMpq *mpq;
	char path[KP_RES_PATH];       /* what kp_mpq_open opens: a file's path, or a slice's name (KpResConfig.repack_file) */
	char name[KP_RES_PATH];       /* in messages: the path, or a slice's file name */
	char file[KP_RES_GROUP_FILE]; /* the file's name, which the core's listing names its groups by and the reads' sources */
} Repack;

struct KpRes {
	const KpDefsProfile *profile;
	void (*log)(void *ctx, const char *line);
	void *log_ctx;
	int (*repack_file)(void *ctx, const char *file, char *out, size_t size);
	void *repack_ctx;
	Repack repack[KP_RES_REPACK_MAX];
	int repack_count;
	char repack_dir[KP_RES_PATH]; /* the core's folder ("" when it is not a folder's) */
	struct {
		char file[KP_RES_GROUP_FILE], prefix[KP_RES_NAME];
		int open;
	} groups[KP_RES_GROUPS]; /* the core's listing's */
	int group_count;
	Map listed;  /* the repacks' files */
	Map raw;     /* the repacks' Remastered files by their CASC names (listed "remastered": kp_res_raw_read) */
	KpResSourceKind install;
	char install_dir[KP_RES_PATH];
	Archive archives[KP_RES_ARCHIVES];
	const char *archive_names[KP_RES_ARCHIVES];
	int archive_count;
	KpCasc *casc;
	char text_locale[8]; /* the CASC install's (kp_res_text_locale) */
	KpResScr *scr; /* the rebuilds from Remastered's own formats (NULL: the profile has no tables for them) */
	Map seen;    /* the names read: their status and source */
};

struct KpResStream {
	KpMpq *mpq;
	KpMpqFile *mpq_file;
	KpCasc *casc;
	KpCascFile *casc_file;
	int borrowed; /* the archive's handles are the KpRes's (kp_res_head on the calling thread), not closed with it */
};

/* --- small helpers --- */

static void say(const KpRes *r, const char *fmt, const char *a, const char *b)
{
	char line[KP_RES_MESSAGE];
	if (!r->log) return;
	snprintf(line, sizeof line, fmt, a, b ? b : "");
	r->log(r->log_ctx, line);
}

static void set_err(char *err, size_t err_size, const char *text)
{
	if (err_size) snprintf(err, err_size, "%s", text);
}

/* name in lower case with '/' for '\\' (converter/casc/storage.py normalize) */
static void normalize(const char *name, char *out, size_t size)
{
	size_t i;
	for (i = 0; name[i] && i + 1 < size; i++) out[i] = name[i] == '\\' ? '/' : (char)tolower((unsigned char)name[i]);
	out[i] = 0;
}

/* a normalized name as an MPQ path ('\\' separators; StormLib's hash ignores case) */
static void mpq_name(const char *key, char *out, size_t size)
{
	size_t i;
	for (i = 0; key[i] && i + 1 < size; i++) out[i] = key[i] == '/' ? '\\' : key[i];
	out[i] = 0;
}

static int ends_with(const char *s, const char *suffix)
{
	size_t n = strlen(s), m = strlen(suffix), i;
	if (m > n) return 0;
	for (i = 0; i < m; i++)
		if (tolower((unsigned char)s[n - m + i]) != tolower((unsigned char)suffix[i])) return 0;
	return 1;
}

static char *dup_string(const char *s)
{
	size_t n = strlen(s) + 1;
	char *d = (char *)malloc(n);
	if (d) memcpy(d, s, n);
	return d;
}

static int file_exists(const char *path)
{
	FILE *f = fopen(path, "rb");
	if (!f) return 0;
	fclose(f);
	return 1;
}

/* dir/name as given, else in lower or upper case (an install copied to a case-sensitive file system): 1 and its path */
static int find_file(const char *dir, const char *name, char *out, size_t size)
{
	char alt[KP_RES_NAME];
	size_t i;
	int pass;
	for (pass = 0; pass < 3; pass++) {
		for (i = 0; name[i] && i + 1 < sizeof alt; i++)
			alt[i] = pass == 0 ? name[i] : pass == 1 ? (char)tolower((unsigned char)name[i]) : (char)toupper((unsigned char)name[i]);
		alt[i] = 0;
		snprintf(out, size, "%s/%s", dir, alt);
		if (file_exists(out)) return 1;
	}
	return 0;
}

#define BUILD_INFO_MAX (1u << 20)
#define BUILD_INFO_COLUMNS 64

/* Field k of a '|' separated line [p, end): its start, *field_end its end; NULL past the last field. */
static const char *bar_field(const char *p, const char *end, int k, const char **field_end)
{
	for (; k > 0; k--) {
		while (p < end && *p != '|') p++;
		if (p == end) return NULL;
		p++;
	}
	*field_end = p;
	while (*field_end < end && **field_end != '|') (*field_end)++;
	return p;
}

static int span_is(const char *p, const char *end, const char *word)
{
	size_t n = strlen(word);
	return (size_t)(end - p) == n && memcmp(p, word, n) == 0;
}

static int is_locale(const char *w, const char *end) /* converter/emit/cinematics.py LOCALE: ^[a-z]{2}[A-Z]{2}$ */
{
	return end - w == 4 && islower((unsigned char)w[0]) && islower((unsigned char)w[1]) && isupper((unsigned char)w[2])
		&& isupper((unsigned char)w[3]);
}

void kp_res_build_info_text_locale(const char *text, char *out, size_t size)
{
	const char *p, *line_end, *header = NULL, *header_end = NULL, *row = NULL, *row_end = NULL, *f, *f_end, *g;
	int active = -1, tags = -1, c;
	if (size) out[0] = 0;
	if (size < 5) return;
	for (p = text; *p; p = line_end) {
		line_end = p + strcspn(p, "\r\n");
		if (line_end == p) {
			line_end++;
			continue;
		}
		if (!header) {
			header = p;
			header_end = line_end;
			for (c = 0; c < BUILD_INFO_COLUMNS && (f = bar_field(header, header_end, c, &f_end)) != NULL; c++) {
				const char *name_end = (const char *)memchr(f, '!', (size_t)(f_end - f));
				if (!name_end) name_end = f_end;
				if (span_is(f, name_end, "Active")) active = c;
				if (span_is(f, name_end, "Tags")) tags = c;
			}
			continue;
		}
		if (!row) {
			row = p;
			row_end = line_end;
		}
		if (active >= 0 && (f = bar_field(p, line_end, active, &f_end)) != NULL && span_is(f, f_end, "1")) {
			row = p;
			row_end = line_end;
			break;
		}
	}
	if (!row || tags < 0 || (f = bar_field(row, row_end, tags, &f_end)) == NULL) return;
	for (g = f; g < f_end;) {
		const char *g_end = (const char *)memchr(g, ':', (size_t)(f_end - g)), *w, *locale = NULL;
		int text_tag = 0;
		if (!g_end) g_end = f_end;
		for (w = g; w < g_end;) {
			const char *w_end = w;
			while (w_end < g_end && !isspace((unsigned char)*w_end)) w_end++;
			if (span_is(w, w_end, "text?")) text_tag = 1;
			if (!locale && is_locale(w, w_end)) locale = w;
			for (w = w_end; w < g_end && isspace((unsigned char)*w); w++) {}
		}
		if (text_tag && locale) {
			memcpy(out, locale, 4);
			out[4] = 0;
			return;
		}
		g = g_end + (g_end < f_end);
	}
}

/* The install's own (kp_res_build_info_text_locale over its .build.info), "" without one. */
static void build_info_text_locale(const char *dir, char *out, size_t size)
{
	char path[KP_RES_PATH];
	char *text = NULL;
	size_t n = 0;
	FILE *file;
	if (size) out[0] = 0;
	if (!find_file(dir, ".build.info", path, sizeof path) || !(file = fopen(path, "rb"))) return;
	if ((text = (char *)malloc(BUILD_INFO_MAX + 1)) != NULL) n = fread(text, 1, BUILD_INFO_MAX, file);
	fclose(file);
	if (!text) return;
	text[n] = 0;
	kp_res_build_info_text_locale(text, out, size);
	free(text);
}

/* --- the name maps (open addressing, FNV-1a) --- */

static uint32_t hash_name(const char *s)
{
	uint32_t h = 2166136261u;
	while (*s) h = (h ^ (uint8_t)*s++) * 16777619u;
	return h;
}

static Entry *map_find(const Map *m, const char *key)
{
	size_t at;
	if (!m->cap) return NULL;
	for (at = hash_name(key) & (m->cap - 1); m->slots[at].name; at = (at + 1) & (m->cap - 1))
		if (strcmp(m->slots[at].name, key) == 0) return &m->slots[at];
	return NULL;
}

/* the entry of key, added (its name copied) when new; NULL when out of memory */
static Entry *map_put(Map *m, const char *key)
{
	Entry *e = map_find(m, key);
	size_t at;
	if (e) return e;
	if ((m->count + 1) * 2 > m->cap) {
		Map grown;
		size_t k;
		grown.cap = m->cap ? m->cap * 2 : 256;
		grown.count = m->count;
		if (!(grown.slots = (Entry *)calloc(grown.cap, sizeof(Entry)))) return NULL;
		for (k = 0; k < m->cap; k++) {
			if (!m->slots[k].name) continue;
			for (at = hash_name(m->slots[k].name) & (grown.cap - 1); grown.slots[at].name; at = (at + 1) & (grown.cap - 1)) {}
			grown.slots[at] = m->slots[k];
		}
		free(m->slots);
		*m = grown;
	}
	for (at = hash_name(key) & (m->cap - 1); m->slots[at].name; at = (at + 1) & (m->cap - 1)) {}
	if (!(m->slots[at].name = dup_string(key))) return NULL;
	m->count++;
	return &m->slots[at];
}

static void map_free(Map *m)
{
	size_t k;
	for (k = 0; k < m->cap; k++) {
		free(m->slots[k].name);
		free(m->slots[k].source);
		free(m->slots[k].head);
	}
	free(m->slots);
	memset(m, 0, sizeof *m);
}

static void seen(KpRes *r, const char *key, KpResStatus status, const char *source)
{
	Entry *e = map_put(&r->seen, key);
	if (!e) return;
	e->status = (int)status;
	if (!e->source || strcmp(e->source, source) != 0) {
		free(e->source);
		e->source = dup_string(source);
	}
}

/* --- names --- */

const char *kp_res_source_name(KpResSourceKind kind)
{
	switch (kind) {
	case KP_RES_SOURCE_REPACK: return "repack";
	case KP_RES_SOURCE_MPQ: return "1.17 install";
	case KP_RES_SOURCE_CASC: return "StarCraft: Remastered install";
	case KP_RES_SOURCE_NONE: break;
	}
	return "none";
}

const char *kp_res_status_name(KpResStatus status)
{
	static const char *const NAMES[KP_RES_STATUS_COUNT] = {
		"missing", "absent", "repacked", "exact", "derived", "differs", "unlisted", "refused", "error",
		"equivalent", "colour-equal", "approximation", "remastered", "converted",
	};
	return (unsigned)status < KP_RES_STATUS_COUNT ? NAMES[status] : "?";
}

/* --- detection and opening --- */

KpResSourceKind kp_res_detect(const char *dir, const KpDefsProfile *profile)
{
	char path[KP_RES_PATH];
	if (!profile) profile = &kp_defs_bw_1_17_0_1;
	if (find_file(dir, "koprulu.mpq", path, sizeof path) || find_file(dir, "koprulu-core.mpq", path, sizeof path))
		return KP_RES_SOURCE_REPACK;
	if (find_file(dir, ".build.info", path, sizeof path)) return KP_RES_SOURCE_CASC;
	if (find_file(dir, profile->executable, path, sizeof path)
		|| (profile->archive_count > 0 && find_file(dir, profile->archives[0].name, path, sizeof path)))
		return KP_RES_SOURCE_MPQ;
	return KP_RES_SOURCE_NONE;
}

/* The status a listing names a file by (the source's), or -1. */
static int listed_status(const char *word)
{
	static const KpResStatus LISTED[] = { KP_RES_EXACT, KP_RES_DERIVED, KP_RES_DIFFERS, KP_RES_UNLISTED,
		KP_RES_EQUIVALENT, KP_RES_COLOUR_EQUAL, KP_RES_APPROXIMATION, KP_RES_REMASTERED, KP_RES_CONVERTED };
	size_t k;
	for (k = 0; k < sizeof LISTED / sizeof LISTED[0]; k++)
		if (strcmp(word, kp_res_status_name(LISTED[k])) == 0) return (int)LISTED[k];
	return -1;
}

/* A group the core's listing names, "FILE PREFIX" (a group line's) or "FILE" (an in line's), kept (its prefix
 * normalized); the index, or -1. */
static int add_group(KpRes *r, const char *spec)
{
	char file[KP_RES_GROUP_FILE], prefix[KP_RES_NAME];
	int k;
	prefix[0] = 0;
	if (sscanf(spec, "%127s %511s", file, prefix) < 1) return -1;
	for (k = 0; k < r->group_count; k++)
		if (strcmp(r->groups[k].file, file) == 0) {
			if (!r->groups[k].prefix[0]) normalize(prefix, r->groups[k].prefix, sizeof r->groups[k].prefix);
			return k;
		}
	if (r->group_count >= KP_RES_GROUPS) return -1;
	snprintf(r->groups[k].file, sizeof r->groups[k].file, "%s", file);
	normalize(prefix, r->groups[k].prefix, sizeof r->groups[k].prefix);
	r->groups[k].open = 0;
	r->group_count++;
	return k;
}

/* A listing's file line into the repack table: "SHA256 SIZE STATUS NAME", or "SHA256 SIZE STATUS ogg WAV-SHA256
 * WAV-SIZE WAV-HEADER NAME" (the header in hexadecimal). An exact or derived file is the manifest's: its listed digest
 * (an Ogg's, its WAV's) is checked against the manifest's here, its size against the listing's when read. A
 * "remastered" file is Remastered's own by its CASC name, into the raw table (no manifest lists it). 0, or -1 for a
 * line that is not a file's or does not hold. */
static int listed_file(KpRes *r, int archive, const char *line)
{
	char hex[2 * KP_SHA256_SIZE + 1], word[32], key[KP_RES_NAME];
	uint8_t sha[KP_SHA256_SIZE], wav_sha[KP_SHA256_SIZE];
	unsigned long n, wav_n = 0;
	uint8_t *head = NULL;
	size_t head_size = 0;
	const char *p, *name;
	const KpDefsInput *in;
	Entry *e;
	int used = 0, status, ogg = 0;
	if (sscanf(line, "%64s %lu %31s %n", hex, &n, word, &used) != 3 || used == 0 || kp_hex_decode(hex, sha, KP_SHA256_SIZE) != 0
		|| (status = listed_status(word)) < 0 || n > UINT32_MAX)
		return -1;
	p = line + used;
	if (strncmp(p, "ogg ", 4) == 0) {
		const char *h, *h_end;
		used = 0;
		if (sscanf(p + 4, "%64s %lu %n", hex, &wav_n, &used) != 2 || used == 0 || kp_hex_decode(hex, wav_sha, KP_SHA256_SIZE) != 0
			|| wav_n > UINT32_MAX)
			return -1;
		h = p + 4 + used;
		if (!(h_end = strchr(h, ' ')) || (h_end - h) % 2 != 0 || h_end == h) return -1;
		head_size = (size_t)(h_end - h) / 2;
		if (!(head = (uint8_t *)malloc(head_size))) return -1;
		if (kp_hex_decode(h, head, head_size) != 0) {
			free(head);
			return -1;
		}
		ogg = 1;
		p = h_end + 1;
	}
	name = p;
	if (!*name) {
		free(head);
		return -1;
	}
	normalize(name, key, sizeof key);
	if ((status == KP_RES_EXACT || status == KP_RES_DERIVED) && (in = kp_defs_input(r->profile, key)) != NULL) {
		uint8_t want[KP_SHA256_SIZE];
		if (kp_hex_decode(in->sha256, want, KP_SHA256_SIZE) != 0 || memcmp(want, ogg ? wav_sha : sha, KP_SHA256_SIZE) != 0
			|| in->size != (ogg ? wav_n : n)) {
			say(r, "res: %s's listing names %s as the 1.17 file, which it is not: the install's is read",
				r->repack[archive].name, name);
			free(head);
			return 0;
		}
	}
	if (!(e = map_put(status == KP_RES_REMASTERED || status == KP_RES_CONVERTED ? &r->raw : &r->listed, key))) {
		free(head);
		return -1;
	}
	e->size = (uint32_t)n;
	e->archive = archive;
	e->status = status;
	free(e->head);
	e->head = head;
	e->head_size = (uint32_t)head_size;
	e->wav_size = (uint32_t)wav_n;
	memcpy(e->sha256, sha, KP_SHA256_SIZE);
	return 0;
}

/* A line of the core's listing that places a file of a group read by ranges (ADR 0019, stage C4), "in FILE OFFSET
 * SHA256 SIZE STATUS NAME" (a raw status: remastered, converted), into the raw table with its group and offset; a file
 * the table does not hold yet is in no open archive (-1) until its group opens (listed_file). 0, or -1 for a line that
 * does not hold. */
static int ranged_file(KpRes *r, const char *line)
{
	char file[KP_RES_GROUP_FILE], hex[2 * KP_SHA256_SIZE + 1], word[32], key[KP_RES_NAME];
	unsigned long long offset;
	unsigned long n;
	uint8_t sha[KP_SHA256_SIZE];
	int used = 0, status, g;
	Entry *e;
	if (sscanf(line, "%127s %llu %64s %lu %31s %n", file, &offset, hex, &n, word, &used) != 5 || used == 0 || !line[used]
		|| kp_hex_decode(hex, sha, KP_SHA256_SIZE) != 0 || n > UINT32_MAX
		|| ((status = listed_status(word)) != KP_RES_REMASTERED && status != KP_RES_CONVERTED) || (g = add_group(r, file)) < 0)
		return -1;
	normalize(line + used, key, sizeof key);
	if (!(e = map_find(&r->raw, key))) {
		if (!(e = map_put(&r->raw, key))) return -1;
		e->archive = -1;
		e->size = (uint32_t)n;
		e->status = status;
		memcpy(e->sha256, sha, KP_SHA256_SIZE);
	}
	e->ranged = 1;
	e->group = g;
	e->offset = (uint64_t)offset;
	return 0;
}

/* Whether a listing is a repack's of the profile: its first line the magic, its profile line the profile's. Checked
 * before any of its lines is applied, so a listing refused leaves nothing in the tables. 0, or -1 with the reason. */
static int listing_check(const KpRes *r, int archive, const char *text, char *err, size_t err_size)
{
	size_t magic = strlen(KP_RES_LISTING_MAGIC), id = strlen(r->profile->id);
	const char *line, *end;
	if (strncmp(text, KP_RES_LISTING_MAGIC, magic) == 0 && (text[magic] == '\n' || text[magic] == 0))
		for (line = text; line; line = (end = strchr(line, '\n')) ? end + 1 : NULL) {
			if (strncmp(line, "profile ", 8) != 0) continue;
			if (strncmp(line + 8, r->profile->id, id) == 0 && (line[8 + id] == '\n' || line[8 + id] == 0)) return 0;
			end = strchr(line, '\n');
			if (err_size) snprintf(err, err_size, "%s: a repack of profile %.*s, not %s", r->repack[archive].name,
				(int)(end ? (size_t)(end - line) - 8 : strlen(line + 8)), line + 8, r->profile->id);
			return -1;
		}
	if (err_size) snprintf(err, err_size, "%s: its %s is not a repack's listing of profile %s", r->repack[archive].name,
		KP_RES_LISTING, r->profile->id);
	return -1;
}

static int add_repack(KpRes *r, const char *path, const char *file, const char *dir, char *err, size_t err_size);

/* A repack's file beside the core: in dir, or (dir NULL) the one KpResConfig.repack_file gives. 1 and what kp_mpq_open
 * opens in out, 0 when there is none, -1 with the reason in out. */
static int find_repack(const KpRes *r, const char *dir, const char *file, char *out, size_t size)
{
	if (dir) return find_file(dir, file, out, size);
	return r->repack_file ? r->repack_file(r->repack_ctx, file, out, size) : 0;
}

/* A repack's listing: "koprulu repack 4", "profile ID", "source WHAT", "group FILE PREFIX" lines (the core's: its
 * groups, those beside it, in dir or KpResConfig.repack_file's when dir is NULL, added), "in ..." lines (the core's:
 * the files of the groups read by ranges, ranged_file), then a file a line (listed_file). 0, or -1 with the reason (another profile, a broken listing), its lines then unapplied. */
static int read_listing(KpRes *r, int archive, const char *dir, char *err, size_t err_size)
{
	size_t size = 0;
	char why[KP_RES_MESSAGE];
	char *text = (char *)kp_mpq_read(r->repack[archive].mpq, KP_RES_LISTING, &size, why, sizeof why);
	char *line, *next;
	int bad = 0;
	if (!text) {
		if (err_size) snprintf(err, err_size, "%s: no %s%s%s", r->repack[archive].name, KP_RES_LISTING, why[0] ? ": " : "", why);
		return -1;
	}
	if (listing_check(r, archive, text, err, err_size) != 0) {
		free(text);
		return -1;
	}
	for (line = strchr(text, '\n'); line && *++line; line = next) { /* past the magic */
		next = strchr(line, '\n');
		if (next) *next = 0;
		if (!*line || strncmp(line, "profile ", 8) == 0 || strncmp(line, "source ", 7) == 0) {
			/* the profile's checked */
		} else if (strncmp(line, "group ", 6) == 0) {
			char path[KP_RES_PATH], why2[KP_RES_MESSAGE];
			int g = add_group(r, line + 6), found = g >= 0 ? find_repack(r, dir, r->groups[g].file, path, sizeof path) : 0;
			/* a group not there yet is added when it is (kp_res_add_repack: the web fetches it before a mission) */
			if (found < 0) say(r, "res: %s (its files fall back to the install)", path, NULL);
			else if (found && add_repack(r, path, r->groups[g].file, dir, why2, sizeof why2) != 0)
				say(r, "res: %s (its files fall back to the install)", why2, NULL);
		} else if (strncmp(line, "in ", 3) == 0 ? ranged_file(r, line + 3) != 0 : listed_file(r, archive, line) != 0) {
			bad++;
		}
	}
	free(text);
	if (bad) {
		char count[32];
		snprintf(count, sizeof count, "%d", bad);
		say(r, "res: %s: %s lines of its listing are not a file's (ignored)", r->repack[archive].name, count);
	}
	return 0;
}

/* A repack's archive, file at path (dir its folder), or (dir NULL) KpResConfig.repack_file's: 0, or -1 with the
 * reason. */
static int add_repack(KpRes *r, const char *path, const char *file, const char *dir, char *err, size_t err_size)
{
	int k = r->repack_count, i;
	Repack *rp = &r->repack[k];
	for (i = 0; i < r->repack_count; i++)
		if (strcmp(r->repack[i].path, path) == 0) return 0;
	if (k >= KP_RES_REPACK_MAX) {
		if (err_size) snprintf(err, err_size, "%s: more than %d repack archives", dir ? path : file, KP_RES_REPACK_MAX);
		return -1;
	}
	if (!(rp->mpq = kp_mpq_open(path, 0, err, err_size))) return -1;
	snprintf(rp->path, sizeof rp->path, "%s", path);
	snprintf(rp->name, sizeof rp->name, "%s", dir ? path : file);
	snprintf(rp->file, sizeof rp->file, "%s", file);
	r->repack_count++;
	if (read_listing(r, k, dir, err, err_size) != 0) {
		kp_mpq_close(rp->mpq);
		rp->mpq = NULL;
		r->repack_count--;
		return -1;
	}
	for (i = 0; i < r->group_count; i++)
		if (strcmp(r->groups[i].file, file) == 0) r->groups[i].open = 1;
	say(r, dir ? "res: repack %s" : "res: repack %s (%s)", rp->name, dir ? NULL : path);
	return 0;
}

int kp_res_add_repack(KpRes *r, const char *path, char *err, size_t err_size)
{
	char dir[KP_RES_PATH];
	const char *slash = strrchr(path, '/');
	snprintf(dir, sizeof dir, "%.*s", slash ? (int)(slash - path) : 1, slash ? path : ".");
	return add_repack(r, path, slash ? slash + 1 : path, dir, err, err_size);
}

int kp_res_group_wanted(const KpRes *r, const char *name, char *out, size_t size)
{
	char key[KP_RES_NAME];
	int k;
	normalize(name, key, sizeof key);
	for (k = 0; k < r->group_count; k++) {
		size_t n = strlen(r->groups[k].prefix);
		if (r->groups[k].open || n == 0 || strncmp(key, r->groups[k].prefix, n) != 0) continue;
		snprintf(out, size, "%s", r->groups[k].file);
		return 1;
	}
	return 0;
}

const char *kp_res_repack_dir(const KpRes *r) { return r->repack_dir; }

static int open_mpq_install(KpRes *r, const char *dir, char *err, size_t err_size)
{
	int k;
	for (k = 0; k < r->profile->archive_count && r->archive_count < KP_RES_ARCHIVES; k++) {
		const KpDefsArchive *a = &r->profile->archives[k];
		Archive *slot = &r->archives[r->archive_count];
		if (!find_file(dir, a->name, slot->path, sizeof slot->path)) {
			/* the CDs' archives are optional: no music, movies or speech of theirs */
			if (strcmp(a->role, "core") == 0) {
				if (err_size) snprintf(err, err_size, "%s: no %s (profile %s)", dir, a->name, r->profile->id);
				return -1;
			}
			say(r, "res: %s has no %s: its files are missing", dir, a->name);
			continue;
		}
		if (!(slot->mpq = kp_mpq_open(slot->path, 0, err, err_size))) return -1;
		r->archive_names[r->archive_count++] = a->name;
	}
	return 0;
}

KpRes *kp_res_open(const char *const *dirs, int count, const KpResConfig *config, char *err, size_t err_size)
{
	KpRes *r = (KpRes *)calloc(1, sizeof *r);
	int k;
	if (!r) {
		set_err(err, err_size, "out of memory");
		return NULL;
	}
	r->profile = config && config->profile ? config->profile : &kp_defs_bw_1_17_0_1;
	r->log = config ? config->log : NULL;
	r->log_ctx = config ? config->log_ctx : NULL;
	r->repack_file = config ? config->repack_file : NULL;
	r->repack_ctx = config ? config->repack_ctx : NULL;
	for (k = 0; k < count; k++) {
		const char *dir = dirs[k];
		KpResSourceKind kind = kp_res_detect(dir, r->profile);
		char path[KP_RES_PATH], why[KP_RES_MESSAGE];
		if (kind == KP_RES_SOURCE_NONE) {
			if (err_size) snprintf(err, err_size, "%s holds no data (a repack koprulu.mpq, a 1.17 install with %s, or StarCraft: Remastered's .build.info)", dir, r->profile->executable);
			kp_res_close(r);
			return NULL;
		}
		if (kind == KP_RES_SOURCE_REPACK) {
			if (!find_file(dir, "koprulu.mpq", path, sizeof path)) find_file(dir, "koprulu-core.mpq", path, sizeof path);
			if (kp_res_add_repack(r, path, why, sizeof why) != 0) say(r, "res: %s (ignored: the install is read)", why, NULL);
			else if (!r->repack_dir[0]) snprintf(r->repack_dir, sizeof r->repack_dir, "%s", dir);
			continue;
		}
		if (r->install != KP_RES_SOURCE_NONE) {
			say(r, "res: %s ignored: %s is the install", dir, r->install_dir);
			continue;
		}
		r->install = kind;
		snprintf(r->install_dir, sizeof r->install_dir, "%s", dir);
		if (kind == KP_RES_SOURCE_CASC ? !(r->casc = kp_casc_open(dir, err, err_size)) : open_mpq_install(r, dir, err, err_size) != 0) {
			kp_res_close(r);
			return NULL;
		}
		if (kind == KP_RES_SOURCE_CASC) build_info_text_locale(dir, r->text_locale, sizeof r->text_locale);
		if (kind == KP_RES_SOURCE_CASC && !(r->scr = kp_res_scr_new(r->profile)))
			say(r, "res: profile %s has no rebuilds of Remastered's own formats: its PCX, GRP, TBL, BIN and SMK files only it holds are missing", r->profile->id, NULL);
		say(r, "res: %s %s", kp_res_source_name(kind), dir);
	}
	if (r->repack_file) {
		static const char *const CORES[] = { "koprulu.mpq", "koprulu-core.mpq" };
		char path[KP_RES_PATH], why[KP_RES_MESSAGE];
		const char *ignored = r->install != KP_RES_SOURCE_NONE ? " (ignored: the install is read)" : " (ignored)";
		int found = 0;
		for (k = 0; k < (int)(sizeof CORES / sizeof CORES[0]) && found == 0; k++)
			if ((found = r->repack_file(r->repack_ctx, CORES[k], path, sizeof path)) < 0)
				say(r, "res: %s%s", path, ignored);
			else if (found && add_repack(r, path, CORES[k], NULL, why, sizeof why) != 0)
				say(r, "res: %s%s", why, ignored);
	}
	if (r->install == KP_RES_SOURCE_NONE && r->repack_count == 0) {
		set_err(err, err_size, count || r->repack_file ? "no usable data in the folders given or the program's repack"
			: "no data folder given");
		kp_res_close(r);
		return NULL;
	}
	return r;
}

void kp_res_close(KpRes *r)
{
	int k;
	if (!r) return;
	for (k = 0; k < r->repack_count; k++) kp_mpq_close(r->repack[k].mpq);
	for (k = 0; k < r->archive_count; k++) kp_mpq_close(r->archives[k].mpq);
	kp_casc_close(r->casc);
	kp_res_scr_free(r->scr);
	map_free(&r->listed);
	map_free(&r->raw);
	map_free(&r->seen);
	free(r);
}

KpResSourceKind kp_res_install(const KpRes *r) { return r->install; }
int kp_res_repacks(const KpRes *r) { return r->repack_count; }
const KpDefsProfile *kp_res_profile(const KpRes *r) { return r->profile; }

/* --- reading --- */

/* Whether data's SHA-256 is want */
static int digest_is(const uint8_t *data, size_t size, const uint8_t want[KP_SHA256_SIZE])
{
	uint8_t d[KP_SHA256_SIZE];
	kp_sha256(data, size, d);
	return memcmp(d, want, KP_SHA256_SIZE) == 0;
}

/* Whether data is the manifest entry's file: its size and SHA-256 */
static int matches(const uint8_t *data, size_t size, const KpDefsInput *in)
{
	uint8_t want[KP_SHA256_SIZE];
	return size == in->size && kp_hex_decode(in->sha256, want, KP_SHA256_SIZE) == 0 && digest_is(data, size, want);
}

/* Whether the simulation is built from the file (a normalized name), so an install's copy must be the manifest's to
 * the byte, a repack's its listing's: the DAT tables, the scripts and dialog BINs, the missions' CHK, the tilesets' CV5
 * and VF4 (the walk raster), the GRPs and LO files grpmeta reads (the saves' identity has its CRC), the string tables
 * (images.tbl names the GRPs). The rest only shows or plays (sounds, movies, pictures, the tilesets' VX4, VR4 and WPE,
 * fonts, texts): its size is checked. */
static int simulation_input(const char *key)
{
	static const char *const KINDS[] = { ".dat", ".bin", ".chk", ".cv5", ".vf4", ".grp", ".tbl" };
	const char *dot = strrchr(key, '.');
	size_t k;
	if (!dot || strchr(dot, '/')) return 0;
	if (strlen(dot) == 4 && strncmp(dot, ".lo", 3) == 0) return 1; /* .loa, .lob, ...: the overlays' offsets */
	for (k = 0; k < sizeof KINDS / sizeof KINDS[0]; k++)
		if (strcmp(dot, KINDS[k]) == 0) return 1;
	return 0;
}

uint8_t *kp_res_casc_rebuild(const char *key, const char *casc_name, const uint8_t *data, size_t size, uint32_t want)
{
	size_t k;
	uint8_t *out;
	if (ends_with(casc_name, VX4EX)) {
		size_t count = want / 2;
		if (want % 2 || size < count * 4) return NULL;
		for (k = 0; k < count; k++)
			if (data[4 * k + 2] || data[4 * k + 3]) return NULL; /* not a 16-bit reference */
		if (!(out = (uint8_t *)malloc((size_t)want + 1))) return NULL;
		for (k = 0; k < count; k++) {
			out[2 * k] = data[4 * k];
			out[2 * k + 1] = data[4 * k + 1];
		}
		out[want] = 0;
		return out;
	}
	for (k = 0; k < sizeof CASC_GROWN / sizeof CASC_GROWN[0]; k++)
		if (ends_with(key, CASC_GROWN[k])) break;
	if (k == sizeof CASC_GROWN / sizeof CASC_GROWN[0] || size < want || !(out = (uint8_t *)malloc((size_t)want + 1))) return NULL;
	memcpy(out, data, want);
	out[want] = 0;
	if (ends_with(key, ".cv5")) {
		const char *stem = strrchr(key, '/');
		stem = stem ? stem + 1 : key;
		for (k = 0; k < sizeof CV5_FILLED / sizeof CV5_FILLED[0]; k++) {
			size_t n = strlen(CV5_FILLED[k].tileset), first = (size_t)CV5_FILLED[k].first * CV5_GROUP;
			size_t end = (size_t)CV5_FILLED[k].end * CV5_GROUP;
			if (strncmp(stem, CV5_FILLED[k].tileset, n) == 0 && strcmp(stem + n, ".cv5") == 0 && first < want)
				memset(out + first, 0, (end < want ? end : want) - first);
		}
	}
	return out;
}

/* The CASC names a 1.17 name may have, in the order they are tried */
static int casc_candidates(const char *key, char names[][KP_RES_NAME], int cap)
{
	int n = 0, k;
	for (k = 0; k < CASC_MOVED_COUNT && n < cap; k++)
		if (strcmp(CASC_MOVED[k].name, key) == 0) snprintf(names[n++], KP_RES_NAME, "%s", CASC_MOVED[k].casc);
	for (k = 0; k < CASC_PREFIX_COUNT && n < cap; k++) snprintf(names[n++], KP_RES_NAME, "%s%s", CASC_PREFIXES[k], key);
	if (ends_with(key, ".vx4"))
		for (k = 0; k < CASC_PREFIX_COUNT && n < cap; k++) snprintf(names[n++], KP_RES_NAME, "%s%sex", CASC_PREFIXES[k], key);
	return n;
}

/* A file from Remastered: an exact copy, else one rebuilt to the 1.17 file, else (unless exact) one remade from
 * Remastered's own formats, else a copy as it is (KP_RES_DIFFERS). */
static uint8_t *read_casc(KpRes *r, const char *key, int exact, size_t *size, KpResStatus *status, char *source)
{
	char names[CASC_MOVED_COUNT + 2 * CASC_PREFIX_COUNT][KP_RES_NAME];
	int n = casc_candidates(key, names, (int)(sizeof names / sizeof names[0])), k, fallback = -1;
	const KpDefsInput *in = kp_defs_input(r->profile, key);
	uint8_t *kept = NULL;
	size_t kept_size = 0;
	for (k = 0; k < n; k++) {
		char why[KP_RES_MESSAGE];
		size_t got = 0;
		uint8_t *b = kp_casc_read(r->casc, names[k], &got, why, sizeof why), *rebuilt;
		if (!b) {
			/* named but not installed locally (kp_res_casc_exact's donors are probed quietly) */
			if (why[0] && !exact) say(r, "res: %s%s", why, NULL);
			continue;
		}
		if (!in || matches(b, got, in)) {
			*status = in ? KP_RES_EXACT : KP_RES_UNLISTED;
			snprintf(source, KP_RES_NAME, "CASC %s", names[k]);
			*size = got;
			free(kept);
			return b;
		}
		if ((rebuilt = kp_res_casc_rebuild(key, names[k], b, got, in->size)) != NULL && matches(rebuilt, in->size, in)) {
			*status = KP_RES_DERIVED;
			snprintf(source, KP_RES_NAME, "CASC %s", names[k]);
			*size = in->size;
			free(b);
			free(kept);
			return rebuilt;
		}
		free(rebuilt);
		if (!exact && fallback < 0 && !ends_with(names[k], VX4EX)) {
			fallback = k;
			kept = b;
			kept_size = got;
		} else {
			free(b);
		}
	}
	/* a listed file no copy reaches: rebuilt from Remastered's own formats, before its copy is taken as it is */
	if (in && r->scr && !exact) {
		uint8_t *made = kp_res_scr_remake(r, r->scr, r->casc, key, in, size, status, source);
		if (made) {
			free(kept);
			return made;
		}
	}
	if (kept) {
		snprintf(source, KP_RES_NAME, "CASC %s", names[fallback]);
		*status = KP_RES_DIFFERS;
		*size = kept_size;
		return kept;
	}
	*status = KP_RES_MISSING;
	return NULL;
}

static uint8_t *read_mpq(KpRes *r, const char *key, size_t *size, KpResStatus *status, char *source, char *err, size_t err_size)
{
	char name[KP_RES_NAME];
	const KpDefsInput *in = kp_defs_input(r->profile, key);
	int k;
	mpq_name(key, name, sizeof name);
	for (k = 0; k < r->archive_count; k++) {
		char why[KP_RES_MESSAGE];
		size_t got = 0;
		uint8_t *b = kp_mpq_read(r->archives[k].mpq, name, &got, why, sizeof why);
		if (!b) {
			if (why[0]) {
				set_err(err, err_size, why);
				*status = KP_RES_ERROR;
				return NULL;
			}
			continue;
		}
		snprintf(source, KP_RES_NAME, "%s", r->archive_names[k]);
		if (!in) {
			*status = KP_RES_UNLISTED;
		} else if (simulation_input(key) ? !matches(b, got, in) : got != in->size) {
			uint8_t d[KP_SHA256_SIZE];
			char hex[2 * KP_SHA256_SIZE + 1];
			kp_sha256(b, got, d);
			kp_sha256_hex(d, hex);
			if (err_size) snprintf(err, err_size, "%s: %s has size %zu, sha256 %s; profile %s expects size %u, sha256 %s",
				r->archives[k].path, name, got, hex, r->profile->id, (unsigned)in->size, in->sha256);
			free(b);
			*status = KP_RES_REFUSED;
			return NULL;
		} else {
			*status = KP_RES_EXACT;
		}
		*size = got;
		return b;
	}
	*status = KP_RES_MISSING;
	return NULL;
}

/* A repack's copy as listed: its size, and its SHA-256 when the simulation is built from it (simulation_input: a
 * damaged copy would play another game, silently; one that only shows or plays is taken by its size). NULL when it
 * has none or its copy differs (logged: the install's is read). */
static uint8_t *read_repack(KpRes *r, const char *key, size_t *size, char *source)
{
	Entry *e = map_find(&r->listed, key);
	char name[KP_RES_NAME], why[KP_RES_MESSAGE];
	size_t got = 0;
	uint8_t *b;
	if (!e) return NULL;
	mpq_name(key, name, sizeof name);
	b = kp_mpq_read(r->repack[e->archive].mpq, name, &got, why, sizeof why);
	if (b && got == e->size && (!simulation_input(key) || digest_is(b, got, e->sha256))) {
		snprintf(source, KP_RES_NAME, "%s", r->repack[e->archive].file);
		*size = got;
		return b;
	}
	free(b);
	say(r, "res: %s in %s differs from its listing or cannot be read: the install's is read", name, r->repack[e->archive].name);
	return NULL;
}

uint8_t *kp_res_read(KpRes *r, const char *name, size_t *size, KpResStatus *status, char *err, size_t err_size)
{
	char key[KP_RES_NAME], source[KP_RES_NAME];
	KpResStatus st = KP_RES_MISSING;
	uint8_t *b = NULL;
	*size = 0;
	if (err_size) err[0] = 0;
	source[0] = 0;
	normalize(name, key, sizeof key);
	if (kp_defs_listed(r->profile->absent, r->profile->absent_count, key)) {
		st = KP_RES_ABSENT;
	} else if ((b = read_repack(r, key, size, source)) != NULL) {
		st = KP_RES_REPACKED;
	} else if (r->install == KP_RES_SOURCE_MPQ) {
		b = read_mpq(r, key, size, &st, source, err, err_size);
	} else if (r->install == KP_RES_SOURCE_CASC) {
		b = read_casc(r, key, 0, size, &st, source);
	}
	if (st == KP_RES_UNLISTED && !map_find(&r->seen, key)) say(r, "res: %s is not in the input manifest: read unchecked from %s", name, source);
	if (st == KP_RES_DIFFERS && !map_find(&r->seen, key)) say(r, "res: %s differs from the 1.17 file: Remastered's %s is read", name, source);
	seen(r, key, st, source);
	if (status) *status = st;
	return b;
}

int kp_res_exists(KpRes *r, const char *name)
{
	char key[KP_RES_NAME], mname[KP_RES_NAME], names[CASC_MOVED_COUNT + 2 * CASC_PREFIX_COUNT][KP_RES_NAME];
	int k, n;
	normalize(name, key, sizeof key);
	if (kp_defs_listed(r->profile->absent, r->profile->absent_count, key))
		return 0;
	if (map_find(&r->listed, key) || (r->casc && kp_defs_input(r->profile, key))) return 1;
	mpq_name(key, mname, sizeof mname);
	for (k = 0; k < r->archive_count; k++)
		if (kp_mpq_has(r->archives[k].mpq, mname)) return 1;
	if (r->casc) {
		n = casc_candidates(key, names, (int)(sizeof names / sizeof names[0]));
		for (k = 0; k < n; k++)
			if (kp_casc_has(r->casc, names[k])) return 1;
	}
	return 0;
}

const char *kp_res_text_locale(const KpRes *r) { return r->text_locale; }

uint8_t *kp_res_casc_exact(KpRes *r, const char *key, size_t *size)
{
	char source[KP_RES_NAME];
	KpResStatus st;
	*size = 0;
	if (!r->casc || !kp_defs_input(r->profile, key)) return NULL;
	return read_casc(r, key, 1, size, &st, source); /* exact: an exact or derived copy only */
}

/* --- streams --- */

/* A file opened to read in pieces: on archive handles of its own (own: another thread's stream) or the KpRes's. */
static KpResStream *open_file(KpRes *r, const char *name, int own, uint64_t *size, char *err, size_t err_size)
{
	char key[KP_RES_NAME], mname[KP_RES_NAME];
	KpResStream *s = (KpResStream *)calloc(1, sizeof *s);
	const KpDefsInput *in;
	Entry *e;
	int k;
	if (err_size) err[0] = 0;
	*size = 0;
	if (!s) {
		set_err(err, err_size, "out of memory");
		return NULL;
	}
	s->borrowed = !own;
	normalize(name, key, sizeof key);
	mpq_name(key, mname, sizeof mname);
	in = kp_defs_input(r->profile, key);
	if (kp_defs_listed(r->profile->absent, r->profile->absent_count, key))
		goto none;
	if ((e = map_find(&r->listed, key)) != NULL
		&& (s->mpq = own ? kp_mpq_open(r->repack[e->archive].path, 1, err, err_size) : r->repack[e->archive].mpq) != NULL) {
		if ((s->mpq_file = kp_mpq_file_open(s->mpq, mname, size, err, err_size)) != NULL && *size == e->size) {
			seen(r, key, KP_RES_REPACKED, r->repack[e->archive].file);
			return s;
		}
		say(r, "res: %s in %s differs from its listing or cannot be read: the install's is read", mname, r->repack[e->archive].name);
		kp_mpq_file_close(s->mpq_file);
		if (own) kp_mpq_close(s->mpq);
		s->mpq_file = NULL;
		s->mpq = NULL;
	}
	for (k = 0; k < r->archive_count; k++) {
		if (!kp_mpq_has(r->archives[k].mpq, mname)) continue;
		if (!(s->mpq = own ? kp_mpq_open(r->archives[k].path, 1, err, err_size) : r->archives[k].mpq)
			|| !(s->mpq_file = kp_mpq_file_open(s->mpq, mname, size, err, err_size)))
			goto fail;
		if (in && *size != in->size) {
			if (err_size) snprintf(err, err_size, "%s: %s has size %llu; profile %s expects size %u", r->archives[k].path,
				mname, (unsigned long long)*size, r->profile->id, (unsigned)in->size);
			seen(r, key, KP_RES_REFUSED, r->archive_names[k]);
			goto fail;
		}
		seen(r, key, in ? KP_RES_EXACT : KP_RES_UNLISTED, r->archive_names[k]);
		return s;
	}
	if (r->casc) {
		char names[CASC_MOVED_COUNT + 2 * CASC_PREFIX_COUNT][KP_RES_NAME], why[KP_RES_MESSAGE], source[KP_RES_NAME];
		int n = casc_candidates(key, names, (int)(sizeof names / sizeof names[0]));
		for (k = 0; k < n; k++) {
			if (ends_with(names[k], VX4EX) || !kp_casc_has(r->casc, names[k])) continue;
			if (!s->casc && !(s->casc = own ? kp_casc_open(r->install_dir, err, err_size) : r->casc)) goto fail;
			if (!(s->casc_file = kp_casc_file_open(s->casc, names[k], size, why, sizeof why))) {
				if (why[0]) say(r, "res: %s%s", why, NULL); /* named but not installed locally: the next copy */
				continue;
			}
			snprintf(source, sizeof source, "CASC %s", names[k]);
			seen(r, key, !in ? KP_RES_UNLISTED : *size == in->size ? KP_RES_EXACT : KP_RES_DIFFERS, source);
			return s;
		}
	}
none:
	seen(r, key, KP_RES_MISSING, "");
	kp_res_stream_close(s);
	return NULL;
fail:
	kp_res_stream_close(s);
	return NULL;
}

KpResStream *kp_res_stream_open(KpRes *r, const char *name, uint64_t *size, char *err, size_t err_size)
{
	return open_file(r, name, 1, size, err, err_size);
}

/* A repack's Ogg of a WAV, the WAV's header and size as listed: its first want bytes (NUL-terminated past them). */
static uint8_t *ogg_head(const Entry *e, size_t want, size_t *got, uint64_t *total, char *err, size_t err_size)
{
	size_t n = e->head_size < want ? e->head_size : want;
	uint8_t *b = (uint8_t *)malloc(n + 1);
	if (!b) {
		set_err(err, err_size, "out of memory");
		return NULL;
	}
	memcpy(b, e->head, n);
	b[n] = 0;
	*got = n;
	*total = e->wav_size;
	if (err_size) err[0] = 0;
	return b;
}

uint8_t *kp_res_head(KpRes *r, const char *name, size_t want, size_t *got, uint64_t *total, char *err, size_t err_size)
{
	KpResStream *s;
	uint8_t *b;
	size_t n;
	char key[KP_RES_NAME];
	Entry *e;
	*got = 0;
	normalize(name, key, sizeof key);
	if ((e = map_find(&r->listed, key)) != NULL && e->head) {
		if ((b = ogg_head(e, want, got, total, err, err_size)) != NULL) seen(r, key, KP_RES_REPACKED, r->repack[e->archive].file);
		return b;
	}
	if (!(s = open_file(r, name, 0, total, err, err_size))) return NULL;
	n = *total < want ? (size_t)*total : want;
	if (!(b = (uint8_t *)malloc(n + 1)) || kp_res_stream_read(s, 0, b, n) != 0) {
		if (err_size) snprintf(err, err_size, "%s: %s", name, b ? "cannot be read" : "out of memory");
		free(b);
		kp_res_stream_close(s);
		return NULL;
	}
	b[n] = 0;
	*got = n;
	kp_res_stream_close(s);
	return b;
}

/* --- Remastered's own files by their CASC names (ADR 0019, stage C2) --- */

/* A repack's raw entry as listed: its size (a CHK's SHA-256 too: a language's map times its game), NULL when it has
 * none, its group is not open (a group read by ranges) or its copy differs (logged: the install's is read). */
static uint8_t *read_raw_repack(KpRes *r, const char *key, const Entry *e, size_t *size)
{
	char name[KP_RES_NAME], why[KP_RES_MESSAGE];
	size_t got = 0;
	uint8_t *b;
	if (e->archive < 0) return NULL;
	mpq_name(key, name, sizeof name);
	b = kp_mpq_read(r->repack[e->archive].mpq, name, &got, why, sizeof why);
	if (b && got == e->size && (!simulation_input(key) || digest_is(b, got, e->sha256))) {
		*size = got;
		return b;
	}
	free(b);
	say(r, "res: %s in %s differs from its listing or cannot be read: the install's is read", name, r->repack[e->archive].name);
	return NULL;
}

uint8_t *kp_res_raw_read(KpRes *r, const char *raw, size_t *size, char *err, size_t err_size)
{
	char key[KP_RES_NAME];
	const Entry *e;
	uint8_t *b;
	*size = 0;
	if (err_size) err[0] = 0;
	normalize(raw, key, sizeof key);
	if ((e = map_find(&r->raw, key)) != NULL && (b = read_raw_repack(r, key, e, size)) != NULL) {
		seen(r, key, (KpResStatus)e->status, r->repack[e->archive].file);
		return b;
	}
	if (!r->casc || !(b = kp_casc_read(r->casc, raw, size, err, err_size))) return NULL;
	seen(r, key, KP_RES_REMASTERED, "CASC");
	return b;
}

/* The first want bytes of an open file of size total (NUL-terminated past them), or NULL with the reason. */
static uint8_t *head_of(int (*read)(void *file, uint64_t offset, void *out, size_t size), void *file, const char *raw,
	size_t want, uint64_t total, size_t *got, char *err, size_t err_size)
{
	size_t n = total < want ? (size_t)total : want;
	uint8_t *b = (uint8_t *)malloc(n + 1);
	if (!b || read(file, 0, b, n) != 0) {
		if (err_size) snprintf(err, err_size, "%s: %s", raw, b ? "cannot be read" : "out of memory");
		free(b);
		return NULL;
	}
	b[n] = 0;
	*got = n;
	return b;
}

static int mpq_file_read(void *file, uint64_t offset, void *out, size_t size)
{
	return kp_mpq_file_read((KpMpqFile *)file, offset, out, size);
}

static int casc_file_read(void *file, uint64_t offset, void *out, size_t size)
{
	return kp_casc_file_read((KpCascFile *)file, offset, out, size);
}

uint8_t *kp_res_raw_head(KpRes *r, const char *raw, size_t want, size_t *got, uint64_t *total, char *err, size_t err_size)
{
	char key[KP_RES_NAME], name[KP_RES_NAME], why[KP_RES_MESSAGE];
	const Entry *e;
	uint8_t *b = NULL;
	*got = 0;
	*total = 0;
	if (err_size) err[0] = 0;
	normalize(raw, key, sizeof key);
	if ((e = map_find(&r->raw, key)) != NULL && e->archive >= 0) {
		KpMpqFile *f;
		if (e->head) {
			if ((b = ogg_head(e, want, got, total, err, err_size)) != NULL) seen(r, key, (KpResStatus)e->status, r->repack[e->archive].file);
			return b;
		}
		mpq_name(key, name, sizeof name);
		if ((f = kp_mpq_file_open(r->repack[e->archive].mpq, name, total, why, sizeof why)) != NULL && *total == e->size)
			b = head_of(mpq_file_read, f, raw, want, *total, got, err, err_size);
		kp_mpq_file_close(f);
		if (b) {
			seen(r, key, (KpResStatus)e->status, r->repack[e->archive].file);
			return b;
		}
		say(r, "res: %s in %s differs from its listing or cannot be read: the install's is read", name, r->repack[e->archive].name);
		*total = 0;
	}
	if (r->casc) {
		KpCascFile *f = kp_casc_file_open(r->casc, raw, total, err, err_size);
		if (f && (b = head_of(casc_file_read, f, raw, want, *total, got, err, err_size)) != NULL) seen(r, key, KP_RES_REMASTERED, "CASC");
		kp_casc_file_close(f);
	}
	return b;
}

int kp_res_raw_key(KpRes *r, const char *raw, uint8_t key[16])
{
	return r->casc ? kp_casc_key(r->casc, raw, key) : -1;
}

int kp_res_raw_held(KpRes *r, const char *raw)
{
	char key[KP_RES_NAME];
	normalize(raw, key, sizeof key);
	return map_find(&r->raw, key) != NULL || (r->casc && kp_casc_held(r->casc, raw));
}

int kp_res_raw_range(const KpRes *r, const char *raw, char *file, size_t file_size, uint64_t *offset, uint32_t *size)
{
	char key[KP_RES_NAME];
	const Entry *e;
	normalize(raw, key, sizeof key);
	if (!(e = map_find(&r->raw, key)) || !e->ranged || e->archive >= 0) return 0; /* none, or its group is open */
	snprintf(file, file_size, "%s", r->groups[e->group].file);
	*offset = e->offset;
	*size = e->size;
	return 1;
}

/* --- Remastered's own files read on another thread (ADR 0019, stage C5: the HD pages) --- */

int kp_res_raw_place(KpRes *r, const char *raw, KpResPlace *p)
{
	char key[KP_RES_NAME];
	const Entry *e;
	memset(p, 0, sizeof *p);
	normalize(raw, key, sizeof key);
	if ((e = map_find(&r->raw, key)) != NULL) {
		if (e->archive < 0 || e->head) return 0; /* its group not open (the web's: kp_res_raw_range), an Ogg of a WAV */
		p->kind = KP_RES_SOURCE_REPACK;
		snprintf(p->path, sizeof p->path, "%s", r->repack[e->archive].path);
		mpq_name(key, p->name, sizeof p->name);
		p->size = e->size;
		seen(r, key, (KpResStatus)e->status, r->repack[e->archive].file);
		return 1;
	}
	if (!r->casc || !kp_casc_held(r->casc, raw)) return 0;
	p->kind = KP_RES_SOURCE_CASC;
	snprintf(p->path, sizeof p->path, "%s", r->install_dir);
	snprintf(p->name, sizeof p->name, "%s", raw);
	seen(r, key, KP_RES_REMASTERED, "CASC");
	return 1;
}

struct KpResReader {
	KpResSourceKind kind; /* the open file's place: KP_RES_SOURCE_REPACK or _CASC, NONE: none */
	char path[KP_RES_PATH], name[KP_RES_NAME];
	KpMpq *mpq;           /* the archive at path (a repack's) */
	KpCasc *casc;         /* the install at path */
	KpMpqFile *mpq_file;  /* the file named, kept open for the next read */
	KpCascFile *casc_file;
	uint64_t size;
};

KpResReader *kp_res_reader_new(void) { return (KpResReader *)calloc(1, sizeof(KpResReader)); }

/* The place's file open on the reader's own handles: 0, or -1. */
static int reader_open(KpResReader *h, const KpResPlace *p)
{
	char why[KP_RES_MESSAGE];
	if (h->kind == p->kind && strcmp(h->path, p->path) == 0 && strcmp(h->name, p->name) == 0) return 0;
	kp_mpq_file_close(h->mpq_file);
	kp_casc_file_close(h->casc_file);
	h->mpq_file = NULL;
	h->casc_file = NULL;
	h->kind = KP_RES_SOURCE_NONE;
	if (p->kind == KP_RES_SOURCE_REPACK) {
		if (!h->mpq || strcmp(h->path, p->path) != 0) {
			kp_mpq_close(h->mpq);
			kp_casc_close(h->casc);
			h->casc = NULL;
			if (!(h->mpq = kp_mpq_open(p->path, 0, why, sizeof why))) return -1;
		}
		if (!(h->mpq_file = kp_mpq_file_open(h->mpq, p->name, &h->size, why, sizeof why))) return -1;
	} else if (p->kind == KP_RES_SOURCE_CASC) {
		if (!h->casc || strcmp(h->path, p->path) != 0) {
			kp_casc_close(h->casc);
			kp_mpq_close(h->mpq);
			h->mpq = NULL;
			if (!(h->casc = kp_casc_open(p->path, why, sizeof why))) return -1;
		}
		if (!(h->casc_file = kp_casc_file_open(h->casc, p->name, &h->size, why, sizeof why))) return -1;
	} else {
		return -1;
	}
	h->kind = p->kind;
	snprintf(h->path, sizeof h->path, "%s", p->path);
	snprintf(h->name, sizeof h->name, "%s", p->name);
	return 0;
}

int kp_res_reader_size(KpResReader *h, const KpResPlace *p, uint64_t *size)
{
	if (reader_open(h, p) != 0) return -1;
	*size = h->size;
	return 0;
}

int kp_res_reader_read(KpResReader *h, const KpResPlace *p, uint64_t offset, void *out, size_t size)
{
	if (reader_open(h, p) != 0 || offset > h->size || size > h->size - offset) return -1;
	return h->mpq_file ? kp_mpq_file_read(h->mpq_file, offset, out, size) : kp_casc_file_read(h->casc_file, offset, out, size);
}

void kp_res_reader_free(KpResReader *h)
{
	if (!h) return;
	kp_mpq_file_close(h->mpq_file);
	kp_casc_file_close(h->casc_file);
	kp_mpq_close(h->mpq);
	kp_casc_close(h->casc);
	free(h);
}

int kp_res_stream_read(KpResStream *s, uint64_t offset, void *out, size_t size)
{
	if (s->mpq_file) return kp_mpq_file_read(s->mpq_file, offset, out, size);
	if (s->casc_file) return kp_casc_file_read(s->casc_file, offset, out, size);
	return -1;
}

void kp_res_stream_close(KpResStream *s)
{
	if (!s) return;
	kp_mpq_file_close(s->mpq_file);
	kp_casc_file_close(s->casc_file);
	if (!s->borrowed) {
		kp_mpq_close(s->mpq);
		kp_casc_close(s->casc);
	}
	free(s);
}

/* --- what was read --- */

void kp_res_counts(const KpRes *r, int counts[KP_RES_STATUS_COUNT])
{
	size_t k;
	memset(counts, 0, sizeof(int) * KP_RES_STATUS_COUNT);
	for (k = 0; k < r->seen.cap; k++)
		if (r->seen.slots[k].name) counts[r->seen.slots[k].status]++;
}

static int by_name(const void *a, const void *b)
{
	return strcmp((*(const Entry *const *)a)->name, (*(const Entry *const *)b)->name);
}

int kp_res_write_log(const KpRes *r, const char *path, char *err, size_t err_size)
{
	const Entry **list = (const Entry **)malloc((r->seen.count ? r->seen.count : 1) * sizeof *list);
	size_t k, n = 0;
	FILE *f;
	if (!list) {
		set_err(err, err_size, "out of memory");
		return -1;
	}
	for (k = 0; k < r->seen.cap; k++)
		if (r->seen.slots[k].name) list[n++] = &r->seen.slots[k];
	qsort(list, n, sizeof *list, by_name);
	if (!(f = fopen(path, "wb"))) {
		if (err_size) snprintf(err, err_size, "cannot write %s", path);
		free(list);
		return -1;
	}
	for (k = 0; k < n; k++)
		fprintf(f, "%s\t%s\t%s\n", list[k]->name, kp_res_status_name((KpResStatus)list[k]->status), list[k]->source ? list[k]->source : "");
	free(list);
	if (fclose(f) != 0) {
		if (err_size) snprintf(err, err_size, "cannot write %s", path);
		return -1;
	}
	return 0;
}

/* --- the core's reader of the original files (kp_files.h) --- */

static uint8_t *files_read(void *ctx, const char *name, size_t *size, char *err, size_t err_size)
{
	return kp_res_read((KpRes *)ctx, name, size, NULL, err, err_size);
}

static uint8_t *files_head(void *ctx, const char *name, size_t want, size_t *got, uint64_t *total, char *err,
	size_t err_size)
{
	return kp_res_head((KpRes *)ctx, name, want, got, total, err, err_size);
}

void kp_res_files(KpRes *r, KpFiles *f)
{
	f->read = files_read;
	f->head = files_head;
	f->ctx = r;
	f->profile = r->profile;
}

static uint8_t *raw_files_read(void *ctx, const char *name, size_t *size, char *err, size_t err_size)
{
	return kp_res_raw_read((KpRes *)ctx, name, size, err, err_size);
}

static uint8_t *raw_files_head(void *ctx, const char *name, size_t want, size_t *got, uint64_t *total, char *err,
	size_t err_size)
{
	return kp_res_raw_head((KpRes *)ctx, name, want, got, total, err, err_size);
}

void kp_res_raw_files(KpRes *r, KpFiles *f)
{
	f->read = raw_files_read;
	f->head = raw_files_head;
	f->ctx = r;
	f->profile = r->profile;
}
