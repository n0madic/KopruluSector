#include "kp_repack.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kp_cine.h"
#include "kp_data.h"
#include "kp_fs.h"
#include "kp_grpmeta.h"
#include "kp_hd_scr.h"
#include "kp_hd_transcode.h"
#include "kp_hd_ui_pack.h"
#include "kp_hd_ui_scr.h"
#include "kp_json.h"
#include "kp_locale.h"
#include "kp_movie.h"
#include "kp_mpq.h"
#include "kp_ogg.h"
#include "kp_sha256.h"
#include "kp_wav.h"
#include "kp_work.h"

#define SINGLE "koprulu.mpq"
#define CORE "koprulu-core.mpq"
#define MUSIC "koprulu-music.mpq"
#define CINEMATICS "koprulu-cinematics" /* + ".mpq" the SD set's group, + "-hd2.mpq" the HD2 set's: read by ranges */
#define HD_GROUP "koprulu-hd"            /* + the rest of an HD set's name + ".mpq" (KP_HD_REPACK_GROUP): read by ranges */
#define HD_NAMES "koprulu/"              /* KP_HD_REPACK_PREFIX's start, normalized */
#define HD_HEADS "koprulu/heads/"        /* KP_HD_REPACK_HEAD's: the core's */
#define UI_NAMES "koprulu/ui/"           /* KP_HD_UI_SCR_REPACK's start, normalized: a variant's ui group's */
#define UI_GROUP "koprulu-ui-"           /* KP_HD_UI_SCR_GROUP's start: + the variant + ".mpq" */
#define LOCALE_GROUP "koprulu-locale-" /* + the locale in lower case + ".mpq" */
#define LOCALES "locales/"
#define WINDOW 64 /* the files read, then encoded, at a time (the music's WAVs are 17 MB each) */
#define SPARE_FILES 8 /* an archive's room past its files: the listing, (listfile) */
#define PATH 1024
#define NOTE 400

/* A file to read: its name as the archive and the listing spell it, its normalized name, the normalized name it is
 * stored under (the key's, but a 1.17 movie's: its WebM's), its 1.17 movie (-1 none), whether it is Remastered's own (a
 * language's speech, the movies, the base's subtitles: kp_res_raw_read). */
typedef struct {
	char name[KP_REPACK_NAME], key[KP_REPACK_NAME], stored[KP_REPACK_NAME];
	int movie, raw;
} Read;

/* A file of the window: as read, then as stored. */
typedef struct {
	const Read *read;
	uint8_t *data;
	size_t size;
	KpResStatus status;
	uint8_t *ogg; /* the Ogg Vorbis that replaces it, NULL: stored as it is */
	size_t ogg_size;
	uint8_t wav_sha[KP_SHA256_SIZE];
	size_t header_size; /* the WAV's header, data[0, header_size) */
	int encoded;        /* the Ogg or the WebM was encoded now (not the cache's) */
	size_t speech_from; /* a language's line or voice re-encoded: the size it had (0: as it is) */
	size_t movie_from;  /* a 1.17 movie transcoded: its SMK's size */
	int failed;         /* a 1.17 movie that does not transcode: the repack stops (note: why) */
	char note[NOTE];    /* why a WAV stays a WAV, logged in order */
} Job;

typedef struct {
	char file[64], prefix[KP_REPACK_NAME];
	KpMpq *mpq;
	KpBuf lines; /* its listing's file lines */
	int planned, files;
	int ranged;  /* read by ranges (the cinematics'): its files stored as they are, each placed in the core's listing */
	uint64_t bytes;
} Group;

typedef struct {
	const KpRepackConfig *c;
	Job *jobs;
} Encoding;

static void normalize(const char *name, char *out, size_t size)
{
	size_t i;
	for (i = 0; name[i] && i + 1 < size; i++) out[i] = name[i] == '\\' ? '/' : (char)tolower((unsigned char)name[i]);
	out[i] = 0;
}

static void mpq_name(const char *key, char *out, size_t size)
{
	size_t i;
	for (i = 0; key[i] && i + 1 < size; i++) out[i] = key[i] == '/' ? '\\' : key[i];
	out[i] = 0;
}

static int ends_with(const char *s, const char *suffix)
{
	size_t n = strlen(s), k = strlen(suffix);
	return n >= k && strcmp(s + n - k, suffix) == 0;
}

#define MOVIES_SD "sd/smk/"   /* KP_CINE_WEBM_FOLDER's, normalized */
#define MOVIES_HD2 "hd2/smk/"

/* Whether a locale's n letters are the base's (KP_LOCALE_BASE, any case) */
static int base_locale(const char *locale, size_t n)
{
	size_t i;
	if (n != strlen(KP_LOCALE_BASE)) return 0;
	for (i = 0; i < n; i++)
		if (tolower((unsigned char)locale[i]) != tolower((unsigned char)KP_LOCALE_BASE[i])) return 0;
	return 1;
}

/* Whether a group's files are read by ranges: the cinematics', the HD sets', the HD interface's variants' and the
 * HD portraits' */
static int ranged_group(const char *file)
{
	return strncmp(file, CINEMATICS, sizeof CINEMATICS - 1) == 0 || strncmp(file, HD_GROUP, sizeof HD_GROUP - 1) == 0
		|| strncmp(file, UI_GROUP, sizeof UI_GROUP - 1) == 0 || strcmp(file, KP_HD_UI_SCR_PORTRAITS) == 0;
}

/* Whether a normalized name is the HD interface's (stage C6): a variant's ui.kpd, a portrait's WebM (HD2's or HD's). */
static int ui_name(const char *key)
{
	static const char HD2_PORTRAIT[] = "hd2/portrait/", PORTRAIT[] = "portrait/";
	return strncmp(key, UI_NAMES, sizeof UI_NAMES - 1) == 0
		|| ((strncmp(key, HD2_PORTRAIT, sizeof HD2_PORTRAIT - 1) == 0 || strncmp(key, PORTRAIT, sizeof PORTRAIT - 1) == 0)
			&& ends_with(key, ".webm"));
}

void kp_repack_group(const char *key, char *out, size_t size, char *prefix, size_t prefix_size)
{
	static const char CAMPAIGN[] = "campaign/";
	const char *at;
	if (strncmp(key, CAMPAIGN, sizeof CAMPAIGN - 1) == 0 && (at = strstr(key, "/staredit/")) != NULL
		&& at > key + sizeof CAMPAIGN - 1) {
		size_t n = (size_t)(at - key) + 1, i, o;
		snprintf(prefix, prefix_size, "%.*s", (int)n, key);
		o = (size_t)snprintf(out, size, "koprulu");
		/* "koprulu-" and the directories between "campaign/" and "/staredit/", '-' apart */
		for (i = sizeof CAMPAIGN - 2; i + 1 < n && o + 1 < size; i++) out[o++] = key[i] == '/' ? '-' : key[i];
		snprintf(out + o, size > o ? size - o : 0, ".mpq");
		return;
	}
	if (strncmp(key, LOCALES, sizeof LOCALES - 1) == 0 && (at = strchr(key + sizeof LOCALES - 1, '/')) != NULL
		&& !base_locale(key + sizeof LOCALES - 1, (size_t)(at - key) - (sizeof LOCALES - 1))) { /* the base's: the core's */
		size_t n = (size_t)(at - key) + 1;
		snprintf(prefix, prefix_size, "%.*s", (int)n, key);
		snprintf(out, size, LOCALE_GROUP "%.*s.mpq", (int)(n - sizeof LOCALES), key + sizeof LOCALES - 1);
		return;
	}
	if (ui_name(key)) { /* read by ranges by their names alone: no prefix */
		if (strncmp(key, UI_NAMES, sizeof UI_NAMES - 1) == 0) { /* "koprulu/ui/<variant>.kpd" */
			const char *v = key + sizeof UI_NAMES - 1, *dot = strchr(v, '.');
			char variant[32];
			snprintf(variant, sizeof variant, "%.*s", (int)(dot ? (size_t)(dot - v) : strlen(v)), v);
			snprintf(out, size, KP_HD_UI_SCR_GROUP, variant);
		} else
			snprintf(out, size, KP_HD_UI_SCR_PORTRAITS);
		prefix[0] = 0;
		return;
	}
	if (strncmp(key, HD_NAMES, sizeof HD_NAMES - 1) == 0 && strncmp(key, HD_HEADS, sizeof HD_HEADS - 1) != 0
		&& (at = strchr(key + sizeof HD_NAMES - 1, '/')) != NULL) { /* an HD set's: "koprulu/<set>/" */
		size_t n = (size_t)(at - key) + 1;
		char set[64];
		snprintf(prefix, prefix_size, "%.*s", (int)n, key);
		snprintf(set, sizeof set, "%.*s", (int)(n - sizeof HD_NAMES), key + sizeof HD_NAMES - 1);
		snprintf(out, size, KP_HD_REPACK_GROUP, set);
		return;
	}
	if (strncmp(key, "music/", 6) == 0) {
		snprintf(out, size, MUSIC);
		snprintf(prefix, prefix_size, "music/");
	} else if (strncmp(key, MOVIES_SD, sizeof MOVIES_SD - 1) == 0) {
		snprintf(out, size, CINEMATICS ".mpq");
		snprintf(prefix, prefix_size, MOVIES_SD);
	} else if (strncmp(key, MOVIES_HD2, sizeof MOVIES_HD2 - 1) == 0) {
		snprintf(out, size, CINEMATICS "-hd2.mpq");
		snprintf(prefix, prefix_size, MOVIES_HD2);
	} else {
		snprintf(out, size, CORE);
		snprintf(prefix, prefix_size, "%s", "");
	}
}

/* The file's group in the repack: its own, or with single the root's (the cinematics' and the languages' kept apart). */
static Group *group_of(Group *groups, int *count, const char *key, int single, char *err, size_t err_size)
{
	char file[64], prefix[KP_REPACK_NAME];
	int k;
	kp_repack_group(key, file, sizeof file, prefix, sizeof prefix);
	if (single && !ranged_group(file) && strncmp(file, LOCALE_GROUP, sizeof LOCALE_GROUP - 1) != 0) {
		snprintf(file, sizeof file, SINGLE);
		prefix[0] = 0;
	}
	for (k = 0; k < *count; k++)
		if (strcmp(groups[k].file, file) == 0) return &groups[k];
	if (*count == KP_REPACK_GROUPS) {
		snprintf(err, err_size, "more than %d groups", KP_REPACK_GROUPS);
		return NULL;
	}
	memset(&groups[*count], 0, sizeof groups[0]);
	snprintf(groups[*count].file, sizeof groups[0].file, "%s", file);
	snprintf(groups[*count].prefix, sizeof groups[0].prefix, "%s", prefix);
	groups[*count].ranged = ranged_group(file);
	return &groups[(*count)++];
}

static int add_read(Read **reads, int *count, int *cap, const char *name, int movie, int raw)
{
	Read *r;
	if (*count == *cap) {
		int grown_cap = *cap ? 2 * *cap : 4096;
		Read *grown = (Read *)realloc(*reads, (size_t)grown_cap * sizeof *grown);
		if (!grown) return -1;
		*reads = grown;
		*cap = grown_cap;
	}
	r = &(*reads)[(*count)++];
	normalize(name, r->key, sizeof r->key);
	mpq_name(r->key, r->name, sizeof r->name);
	snprintf(r->stored, sizeof r->stored, "%s", r->key);
	r->movie = movie;
	r->raw = raw;
	return 0;
}

static int listed(const Read *reads, int count, const char *key)
{
	int k;
	for (k = 0; k < count; k++)
		if (strcmp(reads[k].key, key) == 0) return 1;
	return 0;
}

/* A Remastered name of a language's speech, its folders with '/', once (a voice's file several sounds share). */
static int add_raw(Read **reads, int *count, int *cap, int from, const char *name)
{
	char key[KP_REPACK_NAME];
	normalize(name, key, sizeof key);
	if (listed(*reads + from, *count - from, key)) return 0;
	return add_read(reads, count, cap, key, -1, 1);
}

/* add_raw's list for kp_locale_names */
typedef struct {
	Read **reads;
	int *count, *cap, from;
} RawList;

static int add_listed(void *ctx, const char *name)
{
	RawList *l = (RawList *)ctx;
	return add_raw(l->reads, l->count, l->cap, l->from, name);
}

/* A language's speech (stage C2): each campaign mission's map and lines, then the voices of sfxdata's sounds, as the
 * runtime reads them (kp_host_game_speech, kp_app_audio.c's speech_file). */
static int speech_names(const KpData *d, const char *locale, Read **reads, int *count, int *cap, char *err, size_t err_size)
{
	static const char MISSIONS[] = "missions/";
	char path[KP_REPACK_NAME];
	int from = *count, k, n;
	uint32_t s;
	for (k = 0; k < d->mission_count; k++) {
		const KpDataMission *entry = &d->missions[k];
		KpMission m;
		KpError e;
		if (!entry->catalog || !entry->directory[0] || strncmp(entry->key, MISSIONS, sizeof MISSIONS - 1) != 0) continue;
		snprintf(path, sizeof path, KP_HOST_SPEECH_MAP, locale, entry->key + sizeof MISSIONS - 1);
		if (add_raw(reads, count, cap, from, path) != 0) goto memory;
		if (kp_data_mission(d, entry, NULL, 0, &m, &e) != 0) {
			snprintf(err, err_size, "%s: %s", entry->key, e.msg);
			return -1;
		}
		for (n = 0; n < m.sound_count; n++) {
			char wav[KP_REPACK_NAME];
			if (kp_mission_sound_path(&m, entry->directory, m.sounds[n].string_id, wav, sizeof wav) != 1
				|| kp_ui_data_speech_path(locale, wav, ".ogg", path, sizeof path) != 0)
				continue;
			if (add_raw(reads, count, cap, from, path) != 0) {
				kp_mission_free(&m);
				goto memory;
			}
		}
		kp_mission_free(&m);
	}
	for (s = 0; s < d->ui.sound_path_count; s++) {
		if (!d->ui.sound_paths[s] || kp_ui_data_speech_path(locale, d->ui.sound_paths[s], ".wav", path, sizeof path) != 0) continue;
		if (add_raw(reads, count, cap, from, path) != 0) goto memory;
	}
	return 0;
memory:
	snprintf(err, err_size, "out of memory");
	return -1;
}

/* A Remastered install's movies (stage C4): each of the catalog's in the SD and the HD2 set as they are, and the
 * base's subtitles the install holds (downloaded: an other text locale's install names them, CascLib's offline). */
static int movie_names(KpRes *res, const KpCineCatalog *catalog, Read **reads, int *count, int *cap)
{
	int k, set;
	for (k = 0; k < catalog->count; k++) {
		char name[KP_REPACK_NAME], lower[KP_CINE_NAME];
		normalize(catalog->movies[k].name, lower, sizeof lower);
		for (set = 0; set < 2; set++) {
			snprintf(name, sizeof name, KP_CINE_WEBM_FORMAT, KP_CINE_WEBM_FOLDER[set], lower);
			if (add_read(reads, count, cap, name, -1, 1) != 0) return -1;
		}
		snprintf(name, sizeof name, KP_LOCALE_SUBTITLES, KP_LOCALE_BASE, lower);
		if (kp_res_raw_held(res, name) && add_read(reads, count, cap, name, -1, 1) != 0) return -1;
	}
	return 0;
}

/* The HD portraits (stage C6): each SD portrait movie's clip as the runtime finds it (kp_hd_ui_scr_clip_name: HD2's,
 * else HD's), under its own name. */
static int portrait_names(KpRes *res, const KpData *d, Read **reads, int *count, int *cap)
{
	int k, v;
	for (k = 0; k < d->ui.portraits.count; k++) {
		const KpUiFile *f = &d->ui.portraits.files[k];
		char name[KP_HD_UI_SCR_NAME];
		for (v = 0; v < KP_HD_SCR_VARIANTS; v++) {
			if (kp_hd_ui_scr_clip_name(f->path, f->path_len, v, name, sizeof name) != 0 || !kp_res_raw_held(res, name)) continue;
			if (add_read(reads, count, cap, name, -1, 1) != 0) return -1;
			break;
		}
	}
	return 0;
}

/* The files to read, in order: the manifest's, the movies' (a 1.17 install's), the extra names the rest leave out; a
 * Remastered install's movies (movie_names), with an HD set the HD portraits (portrait_names), the languages' speech
 * and texts (kp_locale_names: the files the runtime builds a language from, Remastered's TrueType fonts with them,
 * which the base's text takes too). */
static Read *plan(KpRes *res, const KpRepackConfig *c, const KpCineCatalog *catalog, const KpCineCatalog *movies,
	int *count, char *err, size_t err_size)
{
	const KpDefsProfile *p = kp_res_profile(res);
	Read *reads = NULL;
	int cap = 0, k, a, manifest;
	*count = 0;
	for (k = 0; k < p->file_count; k++)
		if (add_read(&reads, count, &cap, p->files[k].name, -1, 0) != 0) goto memory;
	for (k = 0; movies && k < movies->count; k++) { /* a 1.17 install's, transcoded: stored as Remastered's SD set */
		char name[KP_REPACK_NAME], lower[KP_CINE_NAME];
		snprintf(name, sizeof name, "smk\\%s.smk", movies->movies[k].name);
		if (add_read(&reads, count, &cap, name, k, 0) != 0) goto memory;
		normalize(movies->movies[k].name, lower, sizeof lower);
		snprintf(name, sizeof name, KP_CINE_WEBM_FORMAT, KP_CINE_WEBM_FOLDER[0], lower);
		normalize(name, reads[*count - 1].stored, sizeof reads[*count - 1].stored);
	}
	manifest = *count;
	for (k = 0; k < c->extra_count; k++) {
		char key[KP_REPACK_NAME];
		normalize(c->extra[k], key, sizeof key);
		for (a = 0; a < p->absent_count; a++)
			if (strcmp(p->absent[a], key) == 0) break;
		if (a < p->absent_count || listed(reads, manifest, key) || listed(reads + manifest, *count - manifest, key)) continue;
		if (add_read(&reads, count, &cap, c->extra[k], -1, 0) != 0) goto memory;
	}
	if (kp_res_install(res) == KP_RES_SOURCE_CASC) {
		KpData d;
		KpError e;
		RawList fonts = { &reads, count, &cap, *count };
		int failed;
		if (kp_data_build(&d, res, NULL, 0, NULL, 0, &e) != 0) {
			snprintf(err, err_size, "the languages: %s", e.msg);
			free(reads);
			return NULL;
		}
		failed = kp_locale_names(&d, KP_LOCALE_BASE, add_listed, &fonts, &e) != 0;
		if (failed) snprintf(err, err_size, "the TrueType fonts: %s", e.msg);
		if (!failed && (movie_names(res, catalog, &reads, count, &cap) != 0
				|| (c->hd_set_count > 0 && portrait_names(res, &d, &reads, count, &cap) != 0))) {
			snprintf(err, err_size, "out of memory");
			failed = 1;
		}
		for (k = 0; k < c->locale_count && !failed; k++) {
			RawList text = { &reads, count, &cap, *count }; /* the language's own: its speech's names too */
			failed = speech_names(&d, c->locales[k], &reads, count, &cap, err, err_size) != 0;
			if (!failed && kp_locale_names(&d, c->locales[k], add_listed, &text, &e) != 0) {
				snprintf(err, err_size, "%s", e.msg);
				failed = 1;
			}
		}
		kp_data_free(&d);
		if (failed) {
			free(reads);
			return NULL;
		}
	}
	return reads;
memory:
	free(reads);
	snprintf(err, err_size, "out of memory");
	return NULL;
}

/* A language's line or voice (a raw job's .ogg or .wav) as Ogg Vorbis mono at 22,050 Hz from the cache, or encoded
 * and kept there (<source SHA-256>-q3-s<serial>-speech.ogg), in its place when smaller: a voice's as a WAV's Ogg (its
 * header listed), a line's as its bytes. */
static void encode_speech(const KpRepackConfig *c, Job *j)
{
	KpWavInfo w;
	char hex[2 * KP_SHA256_SIZE + 1], path[PATH], why[NOTE];
	uint32_t serial = kp_ogg_serial(j->read->key);
	size_t size = 0;
	uint8_t *ogg = NULL;
	int wav = ends_with(j->read->key, ".wav");
	if (!wav && !ends_with(j->read->key, ".ogg")) return;
	if (wav && (kp_wav_info(j->data, j->size, j->size, &w) != 0 || w.data_offset > KP_REPACK_WAV_HEAD)) return;
	kp_sha256(j->data, j->size, j->wav_sha);
	if (c->cache) {
		kp_sha256_hex(j->wav_sha, hex);
		snprintf(path, sizeof path, "%s/%s-q%d-s%u-speech.ogg", c->cache, hex, (int)(KP_OGG_QUALITY * 10.0f + 0.5f), serial);
		ogg = kp_fs_read(path, &size, why, sizeof why);
	}
	if (!ogg) {
		if (!(ogg = kp_ogg_speech(j->data, j->size, KP_OGG_QUALITY, serial, &size, why, sizeof why))) {
			snprintf(j->note, sizeof j->note, "%s: kept as it is (%s)", j->read->name, why);
			return;
		}
		j->encoded = 1;
		if (c->cache && kp_fs_write(path, ogg, size, why, sizeof why) != 0)
			snprintf(j->note, sizeof j->note, "%s: the Ogg Vorbis is not kept: %s", j->read->name, why);
	}
	if (size >= j->size) {
		free(ogg);
		return;
	}
	j->speech_from = j->size;
	if (wav) {
		j->ogg = ogg;
		j->ogg_size = size;
		j->header_size = w.data_offset;
	} else {
		free(j->data);
		j->data = ogg;
		j->size = size;
	}
}

/* A 1.17 movie as WebM (kp_movie_smk_webm) from the cache, or transcoded and kept there (<SMK SHA-256>-<settings>.webm),
 * in its place; one that does not transcode fails the repack. */
static void encode_movie(const KpRepackConfig *c, Job *j)
{
	char hex[2 * KP_SHA256_SIZE + 1], path[PATH], why[NOTE], clip[KP_REPACK_NAME];
	uint8_t sha[KP_SHA256_SIZE], *webm = NULL;
	size_t size = 0, dot;
	KpMovieInfo info;
	kp_sha256(j->data, j->size, sha);
	if (c->cache) {
		kp_sha256_hex(sha, hex);
		snprintf(path, sizeof path, "%s/%s-%s.webm", c->cache, hex, KP_MOVIE_SETTINGS);
		webm = kp_fs_read(path, &size, why, sizeof why);
	}
	if (!webm) {
		snprintf(clip, sizeof clip, "%s", j->read->name); /* "smk\\blizzard.smk" as the clip's "smk\\blizzard" */
		if ((dot = strlen(clip)) > 4 && strcmp(clip + dot - 4, ".smk") == 0) clip[dot - 4] = 0;
		if (!(webm = kp_movie_smk_webm(j->data, j->size, clip, &size, &info, why, sizeof why))) {
			snprintf(j->note, sizeof j->note, "%s", why);
			j->failed = 1;
			return;
		}
		j->encoded = 1;
		if (c->cache && kp_fs_write(path, webm, size, why, sizeof why) != 0)
			snprintf(j->note, sizeof j->note, "%s: the WebM is not kept: %s", j->read->name, why);
	}
	j->movie_from = j->size;
	free(j->data);
	j->data = webm;
	j->size = size;
	j->status = KP_RES_CONVERTED;
}

/* The Ogg Vorbis of a WAV from the cache, or encoded and kept there; a language's speech's (encode_speech), a 1.17
 * movie's WebM (encode_movie). */
static void encode(void *ctx, int k)
{
	Encoding *e = (Encoding *)ctx;
	Job *j = &e->jobs[k];
	KpWavInfo w;
	char hex[2 * KP_SHA256_SIZE + 1], path[PATH], why[NOTE];
	uint32_t serial;
	size_t size = 0;
	uint8_t *ogg = NULL;
	if (j->data && j->read->movie >= 0) { /* a WebM with --wav too: its name is the WebM's */
		encode_movie(e->c, j);
		return;
	}
	if (e->c->wav) return; /* the sounds as they are */
	if (j->data && j->read->raw) {
		encode_speech(e->c, j);
		return;
	}
	if (!j->data || !ends_with(j->read->key, ".wav")) return;
	if (kp_wav_info(j->data, j->size, j->size, &w) != 0 || w.data_offset > KP_REPACK_WAV_HEAD) return;
	kp_sha256(j->data, j->size, j->wav_sha);
	serial = kp_ogg_serial(j->read->key);
	if (e->c->cache) {
		kp_sha256_hex(j->wav_sha, hex);
		snprintf(path, sizeof path, "%s/%s-q%d-s%u.ogg", e->c->cache, hex, (int)(KP_OGG_QUALITY * 10.0f + 0.5f), serial);
		ogg = kp_fs_read(path, &size, why, sizeof why);
	}
	if (!ogg) {
		if (!(ogg = kp_ogg_from_wav(j->data, j->size, KP_OGG_QUALITY, serial, &size, why, sizeof why))) {
			snprintf(j->note, sizeof j->note, "%s: kept as a WAV (%s)", j->read->name, why);
			return;
		}
		j->encoded = 1;
		if (e->c->cache && kp_fs_write(path, ogg, size, why, sizeof why) != 0)
			snprintf(j->note, sizeof j->note, "%s: the Ogg Vorbis is not kept: %s", j->read->name, why);
	}
	if (size >= j->size) { /* not smaller: the WAV as it is */
		free(ogg);
		return;
	}
	j->ogg = ogg;
	j->ogg_size = size;
	j->header_size = w.data_offset;
}

static void report_name(char names[][KP_REPACK_NAME], int *count, const char *name)
{
	if (*count < KP_REPACK_NAMES) snprintf(names[*count], KP_REPACK_NAME, "%s", name);
	(*count)++;
}

static void say(const KpRepackConfig *c, const char *line)
{
	if (c->log && line[0]) c->log(c->log_ctx, line);
}

/* A file's bytes into its group's archive under its MPQ name (a group read by ranges: stored as they are, placed in the
 * core's listing, ranges) and its listing line begun ("<sha256> <size> <status>"). 0, or -1. */
static int put(Group *g, const char *name, const uint8_t *data, size_t size, int compress, KpResStatus status,
	KpBuf *ranges, char *err, size_t err_size)
{
	uint8_t sha[KP_SHA256_SIZE];
	char hex[2 * KP_SHA256_SIZE + 1];
	if (kp_mpq_add(g->mpq, name, data, size, compress && !g->ranged ? KP_MPQ_ZLIB : KP_MPQ_STORE, err, err_size) != 0) return -1;
	kp_sha256(data, size, sha);
	kp_sha256_hex(sha, hex);
	if (g->ranged) {
		uint64_t offset;
		if (kp_mpq_stored_at(g->mpq, name, &offset) != 0) {
			snprintf(err, err_size, "%s: its place in %s does not read", name, g->file);
			return -1;
		}
		kp_buf_printf(ranges, "in %s %llu %s %lu %s %s\n", g->file, (unsigned long long)offset, hex, (unsigned long)size,
			kp_res_status_name(status), name);
	}
	kp_buf_printf(&g->lines, "%s %lu %s", hex, (unsigned long)size, kp_res_status_name(status));
	g->files++;
	g->bytes += size;
	return 0;
}

/* The job's file into its group's archive and listing (put). */
static int store(Group *g, Job *j, KpBuf *ranges, KpRepackReport *rep, char *err, size_t err_size)
{
	const uint8_t *data = j->ogg ? j->ogg : j->data;
	size_t size = j->ogg ? j->ogg_size : j->size;
	char hex[2 * KP_SHA256_SIZE + 1], name[KP_REPACK_NAME];
	int compress = !j->ogg && !ends_with(j->read->stored, ".ogg") && !ends_with(j->read->stored, ".webm");
	mpq_name(j->read->stored, name, sizeof name);
	if (put(g, name, data, size, compress, j->status, ranges, err, err_size) != 0) return -1;
	if (j->ogg) {
		size_t i;
		kp_sha256_hex(j->wav_sha, hex);
		kp_buf_printf(&g->lines, " ogg %s %lu ", hex, (unsigned long)j->size);
		for (i = 0; i < j->header_size; i++) kp_buf_printf(&g->lines, "%02x", j->data[i]);
		if (!j->read->raw) {
			rep->ogg++;
			rep->wav_bytes += j->size;
			rep->ogg_bytes += size;
		}
	}
	if (j->speech_from) {
		rep->speech++;
		rep->speech_from += j->speech_from;
		rep->speech_bytes += size;
	}
	if (j->movie_from) {
		rep->movies++;
		rep->movie_from += j->movie_from;
		rep->movie_bytes += size;
	}
	if (j->read->raw && ui_name(j->read->stored)) rep->portraits++;
	kp_buf_printf(&g->lines, " %s\n", name);
	rep->statuses[j->status]++;
	if (j->encoded) rep->encoded++;
	return 0;
}

/* --- the HD sets (ADR 0019, stage C5) --- */

#define HD_PACKS 16

/* What the HD sets are built from: the install's data, its grpmeta, the SD packs' names (its atlases', in order). */
typedef struct {
	KpData d;
	KpGrpMeta grp;
	char packs[HD_PACKS][32];
	int pack_count, built;
} Hd;

static void hd_free(Hd *h)
{
	if (!h->built) return;
	kp_grpmeta_free(&h->grp);
	kp_data_free(&h->d);
	h->built = 0;
}

/* The install's data, grpmeta and packs (a Remastered install holding each set's anims): 0, or -1 with err. */
static int hd_init(Hd *h, KpRes *res, const KpRepackConfig *c, char *err, size_t err_size)
{
	KpError e;
	uint32_t a;
	int k, variant, format;
	memset(h, 0, sizeof *h);
	for (k = 0; k < c->hd_set_count; k++) {
		if (kp_hd_scr_set(c->hd_sets[k], &variant, &format) != 0) {
			snprintf(err, err_size, "%s: not an HD set (hd2, hd, hd2-astc, hd2-etc2)", c->hd_sets[k]);
			return -1;
		}
		if (!kp_hd_scr_held(res, variant)) {
			snprintf(err, err_size, "the HD set %s: the install holds no %s anims (StarCraft: Remastered's)", c->hd_sets[k],
				kp_hd_scr_variant_name(variant));
			return -1;
		}
	}
	if (kp_data_build(&h->d, res, NULL, 0, NULL, 0, &e) != 0) {
		snprintf(err, err_size, "the HD sets: %s", e.msg);
		return -1;
	}
	if (kp_grpmeta_from_files(&h->grp, &h->d.files, &h->d.dat, &e) != 0) {
		kp_data_free(&h->d);
		snprintf(err, err_size, "the HD sets: %s", e.msg);
		return -1;
	}
	h->built = 1;
	for (a = 1; a <= h->grp.atlas_count; a++) {
		const KpLabel *pack = &h->grp.atlases[a].pack;
		for (k = 0; k < h->pack_count; k++) {
			if (pack->size == strlen(h->packs[k]) && memcmp(pack->text, h->packs[k], pack->size) == 0) break;
		}
		if (k < h->pack_count) continue;
		if (h->pack_count == HD_PACKS || pack->size >= sizeof h->packs[0]) {
			snprintf(err, err_size, "the HD sets: more than %d packs, or a long pack name", HD_PACKS);
			return -1;
		}
		snprintf(h->packs[h->pack_count++], sizeof h->packs[0], "%.*s", (int)pack->size, pack->text);
	}
	return 0;
}

/* An HD set's names, normalized, as kp_repack_group groups them: k < pack_count its packs, then the tilesets', the
 * water's; with head its packs' heads (k < pack_count). 0, or -1 past the last. */
static int hd_name(const Hd *h, const char *set, int k, int head, char *out, size_t size)
{
	char name[KP_REPACK_NAME];
	if (k < h->pack_count) snprintf(name, sizeof name, head ? KP_HD_REPACK_HEAD : KP_HD_REPACK_PACK, set, h->packs[k]);
	else if (head) return -1;
	else if (k < h->pack_count + KP_TILESETS) snprintf(name, sizeof name, KP_HD_REPACK_TILESET, set, KP_TILESET_NAMES[k - h->pack_count]);
	else if (k == h->pack_count + KP_TILESETS) snprintf(name, sizeof name, KP_HD_REPACK_WATER, set);
	else return -1;
	normalize(name, out, size);
	return 0;
}

/* A BC container in a phone's format (kp_hd_transcode) from the cache, or encoded and kept there
 * (<BC SHA-256>-<etc2|astc>-KP_HD_TRANSCODE_SETTINGS.kpd). Malloc'd, or NULL with err. */
static uint8_t *hd_phone(const KpRepackConfig *c, const uint8_t *bc, size_t bc_size, int format, size_t *size,
	int *encoded, char *err, size_t err_size)
{
	char hex[2 * KP_SHA256_SIZE + 1], path[PATH], why[NOTE];
	uint8_t sha[KP_SHA256_SIZE], *out = NULL;
	KpError e;
	if (c->cache) {
		kp_sha256(bc, bc_size, sha);
		kp_sha256_hex(sha, hex);
		snprintf(path, sizeof path, "%s/%s-%s-%s.kpd", c->cache, hex, format == KP_HD_FORMAT_ASTC ? "astc" : "etc2",
			KP_HD_TRANSCODE_SETTINGS);
		if ((out = kp_fs_read(path, size, why, sizeof why)) != NULL) return out;
	}
	if (!(out = kp_hd_transcode(bc, bc_size, format, c->threads > 0 ? c->threads : kp_work_processors(), size, &e))) {
		snprintf(err, err_size, "%s", e.msg);
		return NULL;
	}
	(*encoded)++;
	if (c->cache && kp_fs_write(path, out, *size, why, sizeof why) != 0) {
		snprintf(why + strlen(why), sizeof why - strlen(why), " (the transcoding is not kept)");
		say(c, why);
	}
	return out;
}

/* A file into its group under its name, listed (compress: in a group not read by ranges): 0, or -1 with err. */
static int put_listed(Group *groups, int *group_count, const char *key, const uint8_t *data, size_t size, int compress,
	int single, KpBuf *ranges, char *err, size_t err_size)
{
	char name[KP_REPACK_NAME];
	Group *g;
	if (!(g = group_of(groups, group_count, key, single, err, err_size))) return -1;
	mpq_name(key, name, sizeof name);
	if (put(g, name, data, size, compress, KP_RES_CONVERTED, ranges, err, err_size) != 0) return -1;
	kp_buf_printf(&g->lines, " %s\n", name);
	return 0;
}

/* A container into its group (an HD set's, a variant's ui group: stored, read by ranges), and its head into the root: its first
 * head_size bytes (a pack's up to its frames' end, kp_hd_pack_head_size; ui.kpd's, kp_hd_ui_head_size), which the
 * runtime opens it from. 0, or -1 with err. */
static int hd_put(Group *groups, int *group_count, const char *key, const char *head_key, size_t head_size,
	const uint8_t *data, size_t size, int single, KpBuf *ranges, char *err, size_t err_size)
{
	if (put_listed(groups, group_count, key, data, size, 0, single, ranges, err, err_size) != 0) return -1;
	if (!head_key) return 0;
	if (head_size == 0 || head_size > size) {
		snprintf(err, err_size, "%s: no container's head", key);
		return -1;
	}
	return put_listed(groups, group_count, head_key, data, head_size, 1, single, ranges, err, err_size);
}

/* An HD set built from the install's anims, tilesets and water (kp_hd_scr.h; a phone's transcoded, hd_phone) into its
 * group and its packs' heads into the root: 0, or -1 with err. */
static int hd_write(KpRes *res, const KpRepackConfig *c, Hd *h, const char *set, Group *groups, int *group_count,
	KpBuf *ranges, KpRepackReport *rep, char *err, size_t err_size)
{
	const KpDatColumn *grp_file = kp_dat_column(&h->d.dat, "images", "grp_file");
	int32_t *owner_of = NULL, images = grp_file ? (int32_t)grp_file->count : 0, i;
	KpAnim *anims = NULL;
	KpHdScrSheets sheets;
	KpError e;
	int variant, format, k, rc = -1;
	char key[KP_REPACK_NAME], head[KP_REPACK_NAME];
	memset(&sheets, 0, sizeof sheets);
	kp_hd_scr_set(set, &variant, &format);
	sheets.variant = variant;
	if (!kp_res_raw_place(res, KP_HD_SCR_RELATIONS, &sheets.install) || sheets.install.kind != KP_RES_SOURCE_CASC) {
		snprintf(err, err_size, "the HD set %s: no install to read the anims from", set);
		return -1;
	}
	if (kp_hd_scr_owners(res, &h->grp, &h->d.dat, variant, &owner_of, &e) != 0) {
		snprintf(err, err_size, "the HD set %s: %s", set, e.msg);
		return -1;
	}
	if (!(anims = (KpAnim *)calloc((size_t)(images ? images : 1), sizeof *anims)) || !(sheets.reader = kp_res_reader_new())) {
		snprintf(err, err_size, "out of memory");
		goto done;
	}
	for (k = 0; hd_name(h, set, k, 0, key, sizeof key) == 0; k++) {
		uint8_t *data = NULL, *phone;
		size_t size = 0;
		int is_pack = k < h->pack_count;
		if (is_pack) {
			KpHdLayout l;
			if (kp_hd_scr_layout(res, sheets.reader, &h->grp, owner_of, variant, h->packs[k], anims, images, &l, &e) != 0
				|| kp_hd_pack_build(&l, kp_hd_scr_sheets_read, &sheets, kp_res_profile(res)->id, &data, &size, &e) != 0) {
				kp_hd_layout_free(&l);
				snprintf(err, err_size, "the HD set %s: %s", set, e.msg);
				goto done;
			}
			rep->hd_pages += l.page_count;
			kp_hd_layout_free(&l);
			hd_name(h, set, k, 1, head, sizeof head);
		} else if (k < h->pack_count + KP_TILESETS) {
			data = kp_hd_scr_tileset(res, variant, k - h->pack_count, &size, &e);
		} else {
			data = kp_hd_scr_water(res, variant, &size, &e);
		}
		if (!data) {
			snprintf(err, err_size, "the HD set %s: %s", set, e.msg);
			goto done;
		}
		if (format != KP_HD_FORMAT_BC) { /* a phone's: the tiles and normal maps in ETC2 for either */
			phone = hd_phone(c, data, size, is_pack ? format : KP_HD_FORMAT_ETC2, &size, &rep->hd_encoded, err, err_size);
			free(data);
			if (!(data = phone)) goto done;
		}
		if (hd_put(groups, group_count, key, is_pack ? head : NULL,
				is_pack ? kp_hd_pack_head_size(data, size < KP_HD_PACK_FIRST_READ ? size : KP_HD_PACK_FIRST_READ, size) : 0,
				data, size, c->single, ranges, err, err_size) != 0) {
			free(data);
			goto done;
		}
		free(data);
		rep->hd_files++;
	}
	rc = 0;
done:
	for (i = 0; anims && i < images; i++) kp_anim_free(&anims[i]);
	free(anims);
	kp_res_reader_free(sheets.reader);
	free(owner_of);
	return rc;
}

/* --- the HD interface (ADR 0019, stage C6) --- */

/* The variants of the HD sets asked, once each, whose interface the install holds. */
static int ui_variants(KpRes *res, const KpRepackConfig *c, int variants[KP_HD_SCR_VARIANTS])
{
	int k, n = 0, v, format, i;
	for (k = 0; k < c->hd_set_count; k++) {
		if (kp_hd_scr_set(c->hd_sets[k], &v, &format) != 0 || !kp_hd_ui_scr_held(res, v)) continue;
		for (i = 0; i < n && variants[i] != v; i++) {}
		if (i == n) variants[n++] = v;
	}
	return n;
}

/* The HD interface's names, normalized: k -1 the consoles' art, then each variant's ui.kpd (k even) and its head. */
static void ui_key(int k, const int *variants, char *out, size_t size)
{
	char name[KP_REPACK_NAME];
	if (k < 0) snprintf(name, sizeof name, KP_HD_UI_SCR_REPACK_CONSOLE);
	else snprintf(name, sizeof name, k % 2 ? KP_HD_UI_SCR_REPACK_HEAD : KP_HD_UI_SCR_REPACK, kp_hd_scr_variant_name(variants[k / 2]));
	normalize(name, out, size);
}

/* A Remastered install's HD interface: the consoles' art (kp_hd_ui_scr_console) into the root, and each variant's
 * ui.kpd (kp_hd_ui_pack) into its ui group with its head into the root. 0, or -1 with err. */
static int ui_write(KpRes *res, const KpRepackConfig *c, Hd *h, Group *groups, int *group_count, KpBuf *ranges,
	KpRepackReport *rep, char *err, size_t err_size)
{
	int variants[KP_HD_SCR_VARIANTS], n = ui_variants(res, c, variants), k, rc = -1;
	KpResReader *reader = kp_res_reader_new();
	char key[KP_REPACK_NAME], head[KP_REPACK_NAME];
	KpError e;
	uint8_t *data = NULL;
	size_t size;
	if (!reader) {
		snprintf(err, err_size, "out of memory");
		return -1;
	}
	if (kp_hd_ui_scr_held(res, KP_HD_SCR_HD2)) {
		ui_key(-1, variants, key, sizeof key);
		if (!(data = kp_hd_ui_scr_console(res, reader, &h->d.files, &size, &e))) {
			snprintf(err, err_size, "the consoles' art: %s", e.msg);
			goto done;
		}
		if (put_listed(groups, group_count, key, data, size, 1, c->single, ranges, err, err_size) != 0) goto done;
		free(data);
		data = NULL;
		rep->ui_console = 1;
	}
	for (k = 0; k < n; k++) {
		KpHdUiScr ui;
		if (kp_hd_ui_scr_build(res, reader, &h->d.files, variants[k], &ui, &e) != 0) {
			snprintf(err, err_size, "the HD interface: %s", e.msg);
			goto done;
		}
		if (ui.skipped) {
			char why[NOTE];
			snprintf(why, sizeof why, "the HD interface %s: %d files not the placement table's (the first: %s) left in SD; "
				"regenerate the table with kp_convert hd-ui-table", kp_hd_scr_variant_name(variants[k]), (int)ui.skipped,
				ui.first_skipped);
			say(c, why);
		}
		data = kp_hd_ui_pack(&ui, reader, kp_res_profile(res)->id, &size, &e);
		kp_hd_ui_scr_free(&ui);
		if (!data) {
			snprintf(err, err_size, "the HD interface: %s", e.msg);
			goto done;
		}
		ui_key(2 * k, variants, key, sizeof key);
		ui_key(2 * k + 1, variants, head, sizeof head);
		if (hd_put(groups, group_count, key, head, kp_hd_ui_head_size(data, size, size), data, size, c->single, ranges, err,
				err_size) != 0) {
			goto done;
		}
		free(data);
		data = NULL;
		rep->ui_files++;
	}
	rc = 0;
done:
	free(data);
	kp_res_reader_free(reader);
	return rc;
}

static int cmp_groups(const void *a, const void *b)
{
	return strcmp((*(const Group *const *)a)->file, (*(const Group *const *)b)->file);
}

/* Each archive's listing written and the archive closed; the root's names the groups that hold files, then places the
 * files of those read by ranges (ranges). */
static int finish(Group *groups, int count, const char *root, const char *head, const KpBuf *ranges, const char *out,
	KpRepackReport *rep, char *err, size_t err_size)
{
	Group *sorted[KP_REPACK_GROUPS];
	int k, failed = 0;
	for (k = 0; k < count; k++) sorted[k] = &groups[k];
	qsort(sorted, (size_t)count, sizeof sorted[0], cmp_groups);
	for (k = 0; k < count; k++) {
		Group *g = &groups[k];
		KpBuf listing;
		int i;
		kp_buf_init(&listing);
		kp_buf_str(&listing, head);
		if (strcmp(g->file, root) == 0) {
			for (i = 0; i < count; i++)
				if (sorted[i] != g && sorted[i]->files) kp_buf_printf(&listing, "group %s %s\n", sorted[i]->file, sorted[i]->prefix);
			kp_buf_add(&listing, ranges->data, ranges->size);
		}
		kp_buf_add(&listing, g->lines.data, g->lines.size);
		if (!failed && kp_mpq_add(g->mpq, KP_RES_LISTING, listing.data, listing.size, KP_MPQ_ZLIB, err, err_size) != 0)
			failed = 1;
		kp_buf_free(&listing);
	}
	for (k = 0; k < count; k++) {
		Group *g = &groups[k];
		char path[PATH];
		if (kp_mpq_close(g->mpq) != 0 && !failed) {
			snprintf(err, err_size, "cannot write %s/%s", out, g->file);
			failed = 1;
		}
		g->mpq = NULL;
		snprintf(path, sizeof path, "%s/%s", out, g->file);
		if (!g->files && strcmp(g->file, root) != 0) {
			remove(path);
			continue;
		}
		if (rep->group_count < KP_REPACK_GROUPS) {
			snprintf(rep->groups[rep->group_count].file, sizeof rep->groups[0].file, "%s", g->file);
			rep->groups[rep->group_count].files = g->files;
			rep->groups[rep->group_count].bytes = g->bytes;
			if (kp_fs_size(path, &rep->groups[rep->group_count].archive) != 0) rep->groups[rep->group_count].archive = 0;
			rep->group_count++;
		}
	}
	return failed ? -1 : 0;
}

int kp_repack_write(KpRes *res, const KpRepackConfig *c, KpRepackReport *rep, char *err, size_t err_size)
{
	const KpDefsProfile *p = kp_res_profile(res);
	KpResSourceKind kind = kp_res_install(res);
	const char *root = c->single ? SINGLE : CORE;
	const KpDefsTable *cinm = kp_defs_table(p, "CINM");
	KpCineCatalog catalog;
	Group *groups = NULL;
	Read *reads = NULL;
	Job *jobs = NULL;
	KpBuf head, ranges;
	Hd hd;
	int count = 0, group_count = 0, k, at, rc = -1, threads = c->threads > 0 ? c->threads : kp_work_processors();
	memset(rep, 0, sizeof *rep);
	memset(&hd, 0, sizeof hd);
	kp_buf_init(&head);
	kp_buf_init(&ranges);
	if (kp_res_repacks(res) || (kind != KP_RES_SOURCE_MPQ && kind != KP_RES_SOURCE_CASC)) {
		snprintf(err, err_size, "the repack is made from an install (1.17's or Remastered's), not from %s",
			kp_res_repacks(res) ? "a repack" : "nothing");
		return -1;
	}
	if (!cinm || kp_cine_catalog(&catalog, cinm->data, cinm->size) != 0) {
		snprintf(err, err_size, "the profile's movies (CINM) do not read");
		return -1;
	}
	if (!(reads = plan(res, c, &catalog, kind == KP_RES_SOURCE_MPQ ? &catalog : NULL, &count, err, err_size))) return -1;
	if (!(groups = (Group *)calloc(KP_REPACK_GROUPS, sizeof *groups)) || !(jobs = (Job *)calloc(WINDOW, sizeof *jobs))) {
		snprintf(err, err_size, "out of memory");
		goto done;
	}
	for (k = 0; k < count; k++) {
		Group *g = group_of(groups, &group_count, reads[k].stored, c->single, err, err_size);
		if (!g) goto done;
		g->planned++;
	}
	if ((c->hd_set_count > 0 || kind == KP_RES_SOURCE_CASC) && hd_init(&hd, res, c, err, err_size) != 0) goto done;
	if (kind == KP_RES_SOURCE_CASC) { /* the HD interface's files (ui_write) */
		int variants[KP_HD_SCR_VARIANTS], n = ui_variants(res, c, variants);
		char key[KP_REPACK_NAME];
		for (k = kp_hd_ui_scr_held(res, KP_HD_SCR_HD2) ? -1 : 0; k < 2 * n; k++) {
			Group *g;
			ui_key(k, variants, key, sizeof key);
			if (!(g = group_of(groups, &group_count, key, c->single, err, err_size))) goto done;
			g->planned++;
		}
	}
	for (k = 0; k < c->hd_set_count; k++) { /* the HD sets' containers and their packs' heads (hd_write) */
		char key[KP_REPACK_NAME];
		int n, heads;
		for (heads = 0; heads < 2; heads++) {
			for (n = 0; hd_name(&hd, c->hd_sets[k], n, heads, key, sizeof key) == 0; n++) {
				Group *g = group_of(groups, &group_count, key, c->single, err, err_size);
				if (!g) goto done;
				g->planned++;
			}
		}
	}
	if (!group_of(groups, &group_count, "", c->single, err, err_size)) goto done; /* the root, even with no file */
	if (kp_fs_mkdirs(c->out, err, err_size) != 0 || kp_fs_remove_matching(c->out, "koprulu", ".mpq", err, err_size) < 0)
		goto done;
	if (c->cache && kp_fs_mkdirs(c->cache, err, err_size) != 0) goto done;
	for (k = 0; k < group_count; k++) {
		char path[PATH];
		snprintf(path, sizeof path, "%s/%s", c->out, groups[k].file);
		if (!(groups[k].mpq = kp_mpq_create(path, (uint32_t)(groups[k].planned + SPARE_FILES), err, err_size))) goto done;
	}
	kp_buf_printf(&head, "%s\nprofile %s\nsource %s\n", KP_RES_LISTING_MAGIC, p->id, kind == KP_RES_SOURCE_MPQ ? "1.17" : "remastered");
	for (at = 0; at < count; at += WINDOW) {
		int n = count - at < WINDOW ? count - at : WINDOW;
		Encoding e;
		for (k = 0; k < n; k++) { /* read in order on this thread: a KpRes is one thread's */
			Job *j = &jobs[k];
			char why[1400];
			memset(j, 0, sizeof *j);
			j->read = &reads[at + k];
			if (j->read->raw) { /* a language's file: none, or an empty one (a silent stand-in), is the base's */
				j->status = KP_RES_REMASTERED;
				if (!(j->data = kp_res_raw_read(res, j->read->key, &j->size, why, sizeof why)) && !why[0]) {
					rep->raw_absent++;
					continue;
				}
				if (j->data && j->size == 0) {
					free(j->data);
					j->data = NULL;
					rep->raw_absent++;
					continue;
				}
			} else {
				j->data = kp_res_read(res, j->read->name, &j->size, &j->status, why, sizeof why);
			}
			if (!j->data) {
				if (j->status == KP_RES_REFUSED) {
					snprintf(err, err_size, "%s", why);
					goto done;
				}
				if (why[0]) report_name(rep->damaged_names, &rep->damaged, j->read->name);
				else report_name(rep->missing_names, &rep->missing, j->read->name);
				continue;
			}
			if (j->read->movie >= 0) {
				uint8_t sha[KP_SHA256_SIZE];
				kp_sha256(j->data, j->size, sha);
				if (j->size != catalog.movies[j->read->movie].size || memcmp(sha, catalog.movies[j->read->movie].sha256, sizeof sha) != 0) {
					snprintf(why, sizeof why, "%s: not the reference copy's bytes, left out", j->read->name);
					say(c, why);
					free(j->data);
					j->data = NULL;
					report_name(rep->missing_names, &rep->missing, j->read->name);
				}
			}
		}
		e.c = c;
		e.jobs = jobs;
		kp_work_run(encode, &e, n, threads);
		for (k = 0; k < n; k++) {
			Job *j = &jobs[k];
			Group *g;
			if (j->failed) {
				snprintf(err, err_size, "%s", j->note);
				goto done;
			}
			say(c, j->note);
			if (!j->data) continue;
			g = group_of(groups, &group_count, j->read->stored, c->single, err, err_size);
			if (!g || store(g, j, &ranges, rep, err, err_size) != 0) goto done;
			free(j->data);
			free(j->ogg);
			j->data = j->ogg = NULL;
		}
	}
	for (k = 0; k < c->hd_set_count; k++) {
		if (hd_write(res, c, &hd, c->hd_sets[k], groups, &group_count, &ranges, rep, err, err_size) != 0) goto done;
	}
	if (kind == KP_RES_SOURCE_CASC && ui_write(res, c, &hd, groups, &group_count, &ranges, rep, err, err_size) != 0) goto done;
	rc = finish(groups, group_count, root, kp_buf_text(&head), &ranges, c->out, rep, err, err_size);
done:
	hd_free(&hd);
	for (k = 0; jobs && k < WINDOW; k++) {
		free(jobs[k].data);
		free(jobs[k].ogg);
	}
	for (k = 0; groups && k < group_count; k++) {
		if (groups[k].mpq) kp_mpq_close(groups[k].mpq);
		kp_buf_free(&groups[k].lines);
	}
	kp_buf_free(&head);
	kp_buf_free(&ranges);
	free(jobs);
	free(groups);
	free(reads);
	return rc;
}
