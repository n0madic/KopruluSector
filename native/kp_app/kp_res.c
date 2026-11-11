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

#define KP_RES_PATH 1024
#define KP_RES_NAME 512
#define KP_RES_ARCHIVES 8 /* an install's archives (the profile's) */
#define KP_RES_REPACK_MAX 32 /* a repack's archives: the core or single file and its groups */
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
	uint32_t size;    /* the repack table's: the listed size and digest, the archive */
	uint8_t sha[KP_SHA256_SIZE];
	int archive;
	int status;
} Entry;

typedef struct {
	Entry *slots;
	size_t count, cap;
} Map;

typedef struct {
	KpMpq *mpq;
	char path[KP_RES_PATH];
} Archive;

struct KpRes {
	const KpDefsProfile *profile;
	void (*log)(void *ctx, const char *line);
	void *log_ctx;
	Archive repack[KP_RES_REPACK_MAX];
	int repack_count;
	Map listed;  /* the repacks' files */
	KpResSourceKind install;
	char install_dir[KP_RES_PATH];
	Archive archives[KP_RES_ARCHIVES];
	const char *archive_names[KP_RES_ARCHIVES];
	int archive_count;
	KpCasc *casc;
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
		"missing", "absent", "unreadable", "repacked", "exact", "derived", "differs", "unlisted", "refused", "error",
		"equivalent", "colour-equal", "approximation",
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

static int hex_digest(const char *hex, uint8_t out[KP_SHA256_SIZE])
{
	int i;
	for (i = 0; i < KP_SHA256_SIZE; i++) {
		unsigned v;
		if (!isxdigit((unsigned char)hex[2 * i]) || !isxdigit((unsigned char)hex[2 * i + 1])
			|| sscanf(hex + 2 * i, "%2x", &v) != 1)
			return -1;
		out[i] = (uint8_t)v;
	}
	return 0;
}

/* A repack's listing: "koprulu repack 1", "profile ID", "group FILE" lines (the core's: its groups), then
 * "SHA256 SIZE NAME" a file. 0, or -1 with the reason (another profile, a broken listing). */
static int read_listing(KpRes *r, int archive, const char *dir, char *err, size_t err_size)
{
	size_t size = 0;
	char why[KP_RES_MESSAGE];
	char *text = (char *)kp_mpq_read(r->repack[archive].mpq, KP_RES_LISTING, &size, why, sizeof why);
	char *line, *next;
	int lines = 0, profile_ok = 0;
	if (!text) {
		if (err_size) snprintf(err, err_size, "%s: no %s%s%s", r->repack[archive].path, KP_RES_LISTING, why[0] ? ": " : "", why);
		return -1;
	}
	for (line = text; line && *line; line = next) {
		char name[KP_RES_NAME], key[KP_RES_NAME], hex[2 * KP_SHA256_SIZE + 1];
		uint8_t sha[KP_SHA256_SIZE];
		unsigned long n;
		next = strchr(line, '\n');
		if (next) *next++ = 0;
		if (lines++ == 0) {
			if (strcmp(line, KP_RES_LISTING_MAGIC) != 0) break;
			continue;
		}
		if (strncmp(line, "profile ", 8) == 0) {
			profile_ok = strcmp(line + 8, r->profile->id) == 0;
			if (!profile_ok) {
				if (err_size) snprintf(err, err_size, "%s: a repack of profile %s, not %s", r->repack[archive].path, line + 8, r->profile->id);
				free(text);
				return -1;
			}
		} else if (strncmp(line, "group ", 6) == 0 && dir) {
			char path[KP_RES_PATH], why2[KP_RES_MESSAGE];
			/* a group not there yet is added when it is (kp_res_add_repack: the web fetches it before a mission) */
			if (find_file(dir, line + 6, path, sizeof path) && kp_res_add_repack(r, path, why2, sizeof why2) != 0)
				say(r, "res: %s (its files fall back to the install)", why2, NULL);
		} else if (sscanf(line, "%64s %lu %511[^\n]", hex, &n, name) == 3 && hex_digest(hex, sha) == 0) {
			Entry *e;
			normalize(name, key, sizeof key);
			if (!(e = map_put(&r->listed, key))) break;
			memcpy(e->sha, sha, sizeof sha);
			e->size = (uint32_t)n;
			e->archive = archive;
		}
	}
	free(text);
	if (lines == 0 || !profile_ok) {
		if (err_size) snprintf(err, err_size, "%s: its %s is not a repack's listing of profile %s", r->repack[archive].path,
			KP_RES_LISTING, r->profile->id);
		return -1;
	}
	return 0;
}

int kp_res_add_repack(KpRes *r, const char *path, char *err, size_t err_size)
{
	int k = r->repack_count, i;
	char dir[KP_RES_PATH];
	const char *slash;
	for (i = 0; i < r->repack_count; i++)
		if (strcmp(r->repack[i].path, path) == 0) return 0;
	if (k >= KP_RES_REPACK_MAX) {
		if (err_size) snprintf(err, err_size, "%s: more than %d repack archives", path, KP_RES_REPACK_MAX);
		return -1;
	}
	if (!(r->repack[k].mpq = kp_mpq_open(path, 0, err, err_size))) return -1;
	snprintf(r->repack[k].path, sizeof r->repack[k].path, "%s", path);
	r->repack_count++;
	slash = strrchr(path, '/');
	snprintf(dir, sizeof dir, "%.*s", slash ? (int)(slash - path) : 1, slash ? path : ".");
	if (read_listing(r, k, dir, err, err_size) != 0) {
		kp_mpq_close(r->repack[k].mpq);
		r->repack[k].mpq = NULL;
		r->repack_count--;
		return -1;
	}
	say(r, "res: repack %s%s", path, NULL);
	return 0;
}

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
		if (kind == KP_RES_SOURCE_CASC && !(r->scr = kp_res_scr_new(r->profile)))
			say(r, "res: profile %s has no rebuilds of Remastered's own formats: its PCX, GRP, TBL, BIN and SMK files only it holds are missing", r->profile->id, NULL);
		say(r, "res: %s %s", kp_res_source_name(kind), dir);
	}
	if (r->install == KP_RES_SOURCE_NONE && r->repack_count == 0) {
		set_err(err, err_size, count ? "no usable data in the folders given" : "no data folder given");
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
	map_free(&r->seen);
	free(r);
}

KpResSourceKind kp_res_install(const KpRes *r) { return r->install; }
int kp_res_repacks(const KpRes *r) { return r->repack_count; }

/* --- reading --- */

static int digest_equal(const uint8_t *data, size_t size, uint32_t want_size, const uint8_t want[KP_SHA256_SIZE])
{
	uint8_t d[KP_SHA256_SIZE];
	if (size != want_size) return 0;
	kp_sha256(data, size, d);
	return memcmp(d, want, KP_SHA256_SIZE) == 0;
}

static int matches(const uint8_t *data, size_t size, const KpDefsInput *in)
{
	uint8_t want[KP_SHA256_SIZE];
	return hex_digest(in->sha256, want) == 0 && digest_equal(data, size, in->size, want);
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

static uint8_t *read_casc(KpRes *r, const char *key, size_t *size, KpResStatus *status, char *source)
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
			if (why[0]) say(r, "res: %s%s", why, NULL); /* named but not installed locally */
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
		if (fallback < 0 && !ends_with(names[k], VX4EX)) {
			fallback = k;
			kept = b;
			kept_size = got;
		} else {
			free(b);
		}
	}
	/* a listed file no copy reaches: rebuilt from Remastered's own formats, before its copy is taken as it is */
	if (in && r->scr) {
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
		} else if (!matches(b, got, in)) {
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

/* A repack's copy, equal to its listing; NULL when it has none or its copy differs (logged: the install's is read). */
static uint8_t *read_repack(KpRes *r, const char *key, size_t *size, char *source)
{
	Entry *e = map_find(&r->listed, key);
	char name[KP_RES_NAME], why[KP_RES_MESSAGE];
	size_t got = 0;
	uint8_t *b;
	if (!e) return NULL;
	mpq_name(key, name, sizeof name);
	b = kp_mpq_read(r->repack[e->archive].mpq, name, &got, why, sizeof why);
	if (b && digest_equal(b, got, e->size, e->sha)) {
		const char *base = strrchr(r->repack[e->archive].path, '/');
		snprintf(source, KP_RES_NAME, "%s", base ? base + 1 : r->repack[e->archive].path);
		*size = got;
		return b;
	}
	free(b);
	say(r, "res: %s in %s differs from its listing or cannot be read: the install's is read", name, r->repack[e->archive].path);
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
	if (kp_defs_listed(r->profile->unreadable, r->profile->unreadable_count, key)) {
		st = KP_RES_UNREADABLE;
	} else if (kp_defs_listed(r->profile->absent, r->profile->absent_count, key)) {
		st = KP_RES_ABSENT;
	} else if ((b = read_repack(r, key, size, source)) != NULL) {
		/* TODO: (stage D8, the repack's writer) the listing names each file's status (exact, derived, differs, a
		 * sound as Ogg Vorbis) and an exact or derived copy is checked against the manifest's digest too: the listing
		 * alone vouches only for the repack's own bytes, so a repack rewritten with its listing replaces any file. */
		st = KP_RES_REPACKED;
	} else if (r->install == KP_RES_SOURCE_MPQ) {
		b = read_mpq(r, key, size, &st, source, err, err_size);
	} else if (r->install == KP_RES_SOURCE_CASC) {
		b = read_casc(r, key, size, &st, source);
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
	if (kp_defs_listed(r->profile->unreadable, r->profile->unreadable_count, key)
		|| kp_defs_listed(r->profile->absent, r->profile->absent_count, key))
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

uint8_t *kp_res_casc_file(KpRes *r, const char *casc_name, size_t *size, char *err, size_t err_size)
{
	*size = 0;
	if (!r->casc) {
		if (err_size) err[0] = 0;
		return NULL;
	}
	return kp_casc_read(r->casc, casc_name, size, err, err_size);
}

uint8_t *kp_res_casc_exact(KpRes *r, const char *key, size_t *size)
{
	char names[CASC_MOVED_COUNT + 2 * CASC_PREFIX_COUNT][KP_RES_NAME];
	int n = casc_candidates(key, names, (int)(sizeof names / sizeof names[0])), k;
	const KpDefsInput *in = kp_defs_input(r->profile, key);
	if (!in || !r->casc) return NULL;
	for (k = 0; k < n; k++) {
		char why[KP_RES_MESSAGE];
		size_t got = 0;
		uint8_t *b = kp_casc_read(r->casc, names[k], &got, why, sizeof why), *rebuilt;
		if (!b) continue;
		if (matches(b, got, in)) {
			*size = got;
			return b;
		}
		rebuilt = kp_res_casc_rebuild(key, names[k], b, got, in->size);
		free(b);
		if (rebuilt && matches(rebuilt, in->size, in)) {
			*size = in->size;
			return rebuilt;
		}
		free(rebuilt);
	}
	return NULL;
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
	if (kp_defs_listed(r->profile->unreadable, r->profile->unreadable_count, key)
		|| kp_defs_listed(r->profile->absent, r->profile->absent_count, key))
		goto none;
	if ((e = map_find(&r->listed, key)) != NULL
		&& (s->mpq = own ? kp_mpq_open(r->repack[e->archive].path, 1, err, err_size) : r->repack[e->archive].mpq) != NULL) {
		if ((s->mpq_file = kp_mpq_file_open(s->mpq, mname, size, err, err_size)) != NULL && *size == e->size) {
			seen(r, key, KP_RES_REPACKED, r->repack[e->archive].path);
			return s;
		}
		say(r, "res: %s in %s differs from its listing or cannot be read: the install's is read", mname, r->repack[e->archive].path);
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
		char names[CASC_MOVED_COUNT + 2 * CASC_PREFIX_COUNT][KP_RES_NAME];
		int n = casc_candidates(key, names, (int)(sizeof names / sizeof names[0]));
		for (k = 0; k < n; k++) {
			if (ends_with(names[k], VX4EX) || !kp_casc_has(r->casc, names[k])) continue;
			if (!s->casc && !(s->casc = own ? kp_casc_open(r->install_dir, err, err_size) : r->casc)) goto fail;
			if (!(s->casc_file = kp_casc_file_open(s->casc, names[k], size, err, err_size))) {
				if (err[0]) goto fail;
				continue;
			}
			seen(r, key, !in ? KP_RES_UNLISTED : *size == in->size ? KP_RES_EXACT : KP_RES_DIFFERS, names[k]);
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

uint8_t *kp_res_head(KpRes *r, const char *name, size_t want, size_t *got, uint64_t *total, char *err, size_t err_size)
{
	KpResStream *s = open_file(r, name, 0, total, err, err_size);
	uint8_t *b;
	size_t n;
	*got = 0;
	if (!s) return NULL;
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
