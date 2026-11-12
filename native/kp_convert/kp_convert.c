/*
 * kp_convert: the original data checked, repacked and converted, in C on every desktop (ADR 0019). Run from the
 * repository's root or anywhere: the profile and the input manifest are compiled in (kp_defs.h, tools/defs_c.py).
 *
 *   verify DIR... [--res-log PATH]
 *       reads every file of the input manifest through kp_res from the folders given (a repack, a 1.17 install, a
 *       StarCraft: Remastered install; kp_res_open's rules), reports each kind's statuses and time (a read includes
 *       kp_res's check: a file the simulation reads hashed, the rest by its size) and checks every exact or derived
 *       file to the byte against the manifest; a 1.17 install's StarCraft.exe by its SHA-256. The exit status is 1 when
 *       a file was refused, when an exact or derived file's bytes are not the manifest's, when a file of the core could
 *       not be read or a 1.17 install's core archives miss it (the game cannot start without it), or when the
 *       executable is not the profile's. A mission's sound or a track the install's archives hold but cannot give
 *       whole (a damaged copy: a disc copied with a bad read, issue #2) is reported: the game goes without it, as it
 *       does without one the CDs' archives miss. --res-log: the files read, their status and source.
 *   digests DIR --out PATH
 *       each manifest file's normalized name, status and the SHA-256 of the bytes read (empty when none), a tab
 *       between, in the manifest's order: the twin of `python3 -m converter scr-digests` (tools/ci.sh compares them
 *       over a StarCraft: Remastered install)
 *   extract DIR NAME OUT
 *       a file as kp_res reads it (an MPQ path, any case, '\' or '/'), or a StarCraft: Remastered install's own file
 *       by its CASC name ("locales/ruRU/Assets/rez/stat_txt.xml"), written to OUT
 *   icon DIR [--out generated]
 *       the game's icon for the product's bundles (kp_icon.h), from a 1.17 install's StarCraft.exe (the profile's, by
 *       its SHA-256): <out>/icon/ with icon-<size>.png, icon.ico, icon.icns and the ios/, web/ and android/res/ files
 *   repack DIR [--out DIR] [--single] [--wav] [--res-log FILE] [--cache DIR] [--threads N] [--locales LIST]
 *       the koprulu*.mpq repack of an install (kp_repack.h): its groups into generated/repack, or with --single one
 *       koprulu.mpq (the cinematics' and the languages' groups beside it) into generated/repack-single; a WAV as Ogg
 *       Vorbis when that is smaller (--wav keeps the WAVs; each encoded once into --cache,
 *       generated/cache/repack-vorbis); --res-log: a run's kp_res log, the names it read unlisted go in too;
 *       --locales: the languages that go in (their texts and speech), each its koprulu-locale-<locale>.mpq
 *       ("ruRU,deDE"; "none"; by default every language of the profile but the base's the install holds)
 *   locale-check DIR [--locale L|all] [--out generated]
 *       each language (all: every one of the profile's but the base's the install holds) built from the source's
 *       files as the game builds it (kp_locale.h): its texts, every campaign mission's strings and the glyphs, a
 *       report each in <out>/reports/locale-<locale>.json (the tables', dialogs', scripts' and missions' counts, the
 *       subtitled movies, the glyphs, the rows an exception lets differ, the texts mended, the problems: a text the
 *       game refuses and shows the base's of, a character no glyph draws). The exit status is 1 on any problem.
 *   scenarios DIR [--specs tests/scenarios] [--out generated]
 *       the test scenarios (kp_scenario.h): <out>/scenarios/<name>.chk of each spec and their list
 *       <out>/scenarios/index.txt, a line each: name, base mission, tileset, width, height, resource packs ("a,b,c",
 *       "-" none; the profile's compiled SPAK, which python3 -m converter scr-manifest records)
 *   terrain DIR [--out generated]
 *       the terrain checks' CRCs: <out>/viewer/<tileset>.mcrc, the CRC-32 of each megatile's 32 x 32 indices
 *       (koprulu --autotest compares the drawn terrain with them)
 *
 * DIR is the folder --data names: an install, or for verify a repack too.
 */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kp_campaign.h"
#include "kp_chk.h"
#include "kp_data.h"
#include "kp_fs.h"
#include "kp_icon.h"
#include "kp_json.h"
#include "kp_kpf.h"
#include "kp_locale.h"
#include "kp_mission.h"
#include "kp_ogg.h"
#include "kp_present.h"
#include "kp_repack.h"
#include "kp_res.h"
#include "kp_scenario.h"
#include "kp_sha256.h"
#include "kp_work.h"

#define DIRS_MAX 8
#define MESSAGE 1400
#define PATH 1024
#define SHOWN_LINES 20 /* kp_res's log lines shown */
#define SHOWN_FAILURES 5
#define SHOWN_MISSING 10
#define LOCALES_MAX 16
#define LOCALE_ID 16

/* The kinds of file the measurements are kept for: the game reads the core at its start and at a mission's, the
 * mission's speech with the mission, the music as it plays. */
enum { KIND_CORE, KIND_MISSION_WAV, KIND_MUSIC, KIND_COUNT };
static const char *const KIND_NAMES[KIND_COUNT] = { "core", "mission speech", "music" };

typedef struct {
	int files, status[KP_RES_STATUS_COUNT];
	double bytes, seconds;
} Tally;

static int kind_of(const char *name)
{
	size_t n = strlen(name);
	if (strncmp(name, "music", 5) == 0) return KIND_MUSIC;
	if (strncmp(name, "campaign", 8) == 0 && n > 4 && strcmp(name + n - 4, ".wav") == 0) return KIND_MISSION_WAV;
	return KIND_CORE;
}

static void log_line(void *ctx, const char *line)
{
	int *shown = (int *)ctx;
	if ((*shown)++ < SHOWN_LINES) fprintf(stderr, "%s\n", line);
}

static void warn_line(void *ctx, const char *line)
{
	(void)ctx;
	fprintf(stderr, "%s\n", line);
}

static KpRes *open_sources(const char *const *dirs, int count, int *shown)
{
	KpResConfig config;
	char err[MESSAGE];
	KpRes *res;
	memset(&config, 0, sizeof config);
	config.log = log_line;
	config.log_ctx = shown;
	if (!(res = kp_res_open(dirs, count, &config, err, sizeof err))) fprintf(stderr, "kp_convert: %s\n", err);
	return res;
}

/* An install's executable, checked against the profile's size and SHA-256: malloc'd bytes, or NULL with the reason. */
static uint8_t *read_executable(const char *dir, const KpDefsProfile *p, size_t *size, char *err, size_t err_size)
{
	char path[PATH], hex[2 * KP_SHA256_SIZE + 1];
	uint8_t digest[KP_SHA256_SIZE];
	uint8_t *exe;
	snprintf(path, sizeof path, "%s/%s", dir, p->executable);
	if (!(exe = kp_fs_read(path, size, err, err_size))) {
		if (!err[0]) snprintf(err, err_size, "%s has no %s", dir, p->executable);
		return NULL;
	}
	kp_sha256(exe, *size, digest);
	kp_sha256_hex(digest, hex);
	if (*size != p->executable_size || strcmp(hex, p->executable_sha256) != 0) {
		snprintf(err, err_size, "%s is not the profile's (%lu bytes, SHA-256 %s)", p->executable, (unsigned long)*size, hex);
		free(exe);
		return NULL;
	}
	return exe;
}

/* An install's executable checked (verify): 0, or -1 (reported). */
static int check_executable(const char *dir, const KpDefsProfile *p)
{
	char err[MESSAGE];
	size_t size = 0;
	uint8_t *exe = read_executable(dir, p, &size, err, sizeof err);
	if (!exe) printf("%s\n", err);
	free(exe);
	return exe ? 0 : -1;
}

/* verify and digests: every manifest file read through kp_res and checked. */
static int read_all(const char *const *dirs, int dir_count, const char *res_log, const char *digests_path)
{
	const KpDefsProfile *p;
	FILE *digests = NULL;
	Tally tally[KIND_COUNT];
	char err[MESSAGE];
	int i, k, failed = 0, shown = 0;
	double t0 = kp_work_seconds();
	KpRes *res = open_sources(dirs, dir_count, &shown);
	if (!res) return 2;
	if (digests_path && !(digests = fopen(digests_path, "w"))) {
		fprintf(stderr, "kp_convert: cannot write %s\n", digests_path);
		kp_res_close(res);
		return 2;
	}
	p = kp_res_profile(res);
	printf("%s%s: opened in %.3f s\n", kp_res_source_name(kp_res_install(res)), kp_res_repacks(res) ? " with a repack" : "",
		kp_work_seconds() - t0);
	if (kp_res_install(res) == KP_RES_SOURCE_MPQ && !digests) {
		for (i = 0; i < dir_count; i++)
			if (kp_res_detect(dirs[i], p) == KP_RES_SOURCE_MPQ) break;
		if (i < dir_count && check_executable(dirs[i], p) != 0) failed++;
	}
	memset(tally, 0, sizeof tally);
	for (i = 0; i < p->file_count; i++) {
		const char *name = p->files[i].name;
		Tally *t = &tally[kind_of(name)];
		KpResStatus st;
		size_t size = 0;
		uint8_t *b;
		double a = kp_work_seconds();
		b = kp_res_read(res, name, &size, &st, err, sizeof err);
		t->seconds += kp_work_seconds() - a;
		t->files++;
		t->status[st]++;
		t->bytes += (double)size;
		if (b && (digests || st == KP_RES_EXACT || st == KP_RES_DERIVED)) {
			uint8_t digest[KP_SHA256_SIZE], want[KP_SHA256_SIZE];
			kp_sha256(b, size, digest);
			if ((st == KP_RES_EXACT || st == KP_RES_DERIVED)
				&& (size != p->files[i].size || kp_hex_decode(p->files[i].sha256, want, KP_SHA256_SIZE) != 0
					|| memcmp(digest, want, sizeof want) != 0)) {
				if (failed++ < SHOWN_FAILURES) printf("%s is %s but not the manifest's bytes\n", name, kp_res_status_name(st));
			}
			if (digests) {
				char hex[2 * KP_SHA256_SIZE + 1];
				kp_sha256_hex(digest, hex);
				fprintf(digests, "%s\t%s\t%s\n", name, kp_res_status_name(st), hex);
			}
		} else if (digests) {
			fprintf(digests, "%s\t%s\t\n", name, kp_res_status_name(st));
		}
		if (st == KP_RES_REFUSED || (st == KP_RES_ERROR && kind_of(name) == KIND_CORE)) {
			if (failed++ < SHOWN_FAILURES) printf("%s\n", err);
		} else if (st == KP_RES_ERROR) {
			printf("damaged copy, the game goes without it: %s\n", err);
		} else if (st == KP_RES_MISSING && kp_res_install(res) == KP_RES_SOURCE_MPQ && kind_of(name) == KIND_CORE) {
			if (failed++ < SHOWN_FAILURES) printf("missing %s\n", name);
		}
		free(b);
	}
	for (i = 0; i < KIND_COUNT; i++) {
		printf("  %-15s %5d files, %7.1f MB in %.3f s:", KIND_NAMES[i], tally[i].files, tally[i].bytes / 1048576.0, tally[i].seconds);
		for (k = 0; k < KP_RES_STATUS_COUNT; k++)
			if (tally[i].status[k]) printf(" %d %s", tally[i].status[k], kp_res_status_name((KpResStatus)k));
		printf("\n");
	}
	if (digests && fclose(digests) != 0) {
		fprintf(stderr, "kp_convert: cannot write %s\n", digests_path);
		failed = 1;
	}
	if (res_log && kp_res_write_log(res, res_log, err, sizeof err) != 0) {
		fprintf(stderr, "kp_convert: %s\n", err);
		failed = 1;
	}
	kp_res_close(res);
	return failed ? 1 : 0;
}

static int cmd_verify(int argc, char **argv)
{
	const char *dirs[DIRS_MAX], *res_log = NULL;
	int count = 0, i;
	for (i = 0; i < argc; i++) {
		if (strcmp(argv[i], "--res-log") == 0 && i + 1 < argc) res_log = argv[++i];
		else if (argv[i][0] == '-' || count == DIRS_MAX) return -1;
		else dirs[count++] = argv[i];
	}
	return count ? read_all(dirs, count, res_log, NULL) : -1;
}

static int cmd_digests(int argc, char **argv)
{
	const char *dir = NULL, *out = NULL;
	int i;
	for (i = 0; i < argc; i++) {
		if (strcmp(argv[i], "--out") == 0 && i + 1 < argc) out = argv[++i];
		else if (argv[i][0] == '-' || dir) return -1;
		else dir = argv[i];
	}
	return dir && out ? read_all(&dir, 1, NULL, out) : -1;
}

static int cmd_extract(int argc, char **argv)
{
	char err[MESSAGE];
	KpResStatus st;
	size_t size = 0;
	uint8_t *b;
	int shown = 0, rc;
	KpRes *res;
	if (argc != 3) return -1;
	if (!(res = open_sources((const char *const *)argv, 1, &shown))) return 2;
	b = kp_res_read(res, argv[1], &size, &st, err, sizeof err);
	if (!b && !err[0]) { /* a Remastered file by its CASC name: the install's, or a repack's language group's */
		b = kp_res_raw_read(res, argv[1], &size, err, sizeof err);
		st = KP_RES_REMASTERED;
	}
	if (!b) {
		fprintf(stderr, "kp_convert: %s\n", err[0] ? err : "no such file");
		kp_res_close(res);
		return 1;
	}
	rc = kp_fs_write(argv[2], b, size, err, sizeof err);
	if (rc == 0) printf("%s: %lu bytes, %s, from %s\n", argv[1], (unsigned long)size, kp_res_status_name(st),
		kp_res_source_name(kp_res_install(res)));
	else fprintf(stderr, "kp_convert: %s\n", err);
	free(b);
	kp_res_close(res);
	return rc == 0 ? 0 : 1;
}

/* The names a run's kp_res log ("name<TAB>status<TAB>source" a line) read unlisted, kept in text. */
static char **unlisted_names(const char *path, char **text, int *count)
{
	char err[MESSAGE], **names = NULL, *line;
	size_t size = 0;
	int cap = 0;
	*count = 0;
	if (!(*text = (char *)kp_fs_read(path, &size, err, sizeof err))) {
		fprintf(stderr, "kp_convert: %s\n", err[0] ? err : "no such log");
		return NULL;
	}
	for (line = *text; line && *line;) {
		char *end = strchr(line, '\n'), *tab = strchr(line, '\t');
		if (end) *end = 0;
		if (tab && strncmp(tab + 1, "unlisted\t", 9) == 0) {
			*tab = 0;
			if (*count == cap) {
				char **grown = (char **)realloc(names, (size_t)(cap = cap ? 2 * cap : 64) * sizeof *grown);
				if (!grown) break;
				names = grown;
			}
			names[(*count)++] = line;
		}
		line = end ? end + 1 : NULL;
	}
	if (!names) names = (char **)calloc(1, sizeof *names);
	return names;
}

static int cmp_files(const void *a, const void *b)
{
	return strcmp(((const char *)a), ((const char *)b));
}

static int cmd_icon(int argc, char **argv)
{
	const char *dir = NULL, *out = "generated";
	KpIconImage images[KP_ICON_IMAGES];
	char err[MESSAGE], path[PATH];
	int i, count, files = 0, written;
	size_t size = 0;
	uint8_t *exe;
	for (i = 0; i < argc; i++) {
		if (strcmp(argv[i], "--out") == 0 && i + 1 < argc) out = argv[++i];
		else if (argv[i][0] == '-' || dir) return -1;
		else dir = argv[i];
	}
	if (!dir) return -1;
	if (!(exe = read_executable(dir, &kp_defs_bw_1_17_0_1, &size, err, sizeof err))) {
		fprintf(stderr, "kp_convert: %s\n", err);
		return 1;
	}
	count = kp_icon_read(exe, size, KP_ICON_GROUP, images, KP_ICON_IMAGES, err, sizeof err);
	free(exe);
	if (count < 0) {
		fprintf(stderr, "kp_convert: %s: %s\n", kp_defs_bw_1_17_0_1.executable, err);
		return 1;
	}
	snprintf(path, sizeof path, "%s/icon", out);
	written = kp_icon_write_set(images, count, path, &files, err, sizeof err);
	if (written < 0) fprintf(stderr, "kp_convert: %s\n", err);
	else {
		printf("icon: %d files (%d written) in %s from the executable's", files, written, path);
		for (i = 0; i < count; i++) printf("%s%d", i ? "/" : " ", images[i].size);
		printf(" pixel images\n");
	}
	for (i = 0; i < count; i++) free(images[i].rgba);
	return written < 0;
}

/* The languages whose speech a repack takes (stage C2): --locales' list (comma apart; "none" no language), else every
 * language of the profile's LANG but the first (the base's) whose speech the install holds (KP_DATA_SPEECH_MARKER).
 * Their count, or -1 with a line. */
static int repack_locales(KpRes *res, const char *spec, char ids[LOCALES_MAX][LOCALE_ID], const char **out)
{
	const KpDefsTable *lang = kp_defs_table(kp_res_profile(res), "LANG");
	int n = 0, k;
	if (spec) {
		const char *at = spec;
		if (strcmp(spec, "none") == 0) return 0;
		while (*at) {
			size_t len = strcspn(at, ",");
			if (len == 0 || len >= LOCALE_ID || n == LOCALES_MAX) {
				fprintf(stderr, "kp_convert: --locales %s: a list of locales (\"ruRU,deDE\") or none\n", spec);
				return -1;
			}
			snprintf(ids[n], LOCALE_ID, "%.*s", (int)len, at);
			out[n] = ids[n];
			n++;
			at += len + (at[len] == ',');
		}
		return n;
	}
	if (lang && lang->size >= 1) {
		size_t at = 1;
		for (k = 0; k < lang->data[0] && at < lang->size; k++) {
			size_t id_n = lang->data[at], name_n;
			char marker[128];
			if (at + 1 + id_n >= lang->size) break;
			name_n = lang->data[at + 1 + id_n];
			if (k > 0 && id_n < LOCALE_ID && n < LOCALES_MAX) {
				snprintf(ids[n], LOCALE_ID, "%.*s", (int)id_n, (const char *)lang->data + at + 1);
				snprintf(marker, sizeof marker, KP_DATA_SPEECH_MARKER, ids[n]);
				if (!kp_res_raw_held(res, marker)) snprintf(marker, sizeof marker, KP_LOCALE_PROBE, ids[n]);
				if (kp_res_raw_held(res, marker)) {
					out[n] = ids[n];
					n++;
				}
			}
			at += 2 + id_n + name_n;
		}
	}
	return n;
}

static int cmd_repack(int argc, char **argv)
{
	KpRepackConfig c;
	KpRepackReport *rep;
	const char *dir = NULL, *res_log = NULL, *locales_spec = NULL, *locales[LOCALES_MAX];
	char err[MESSAGE], **extra = NULL, *log_text = NULL, groups[KP_REPACK_GROUPS][64], ids[LOCALES_MAX][LOCALE_ID];
	int i, shown = 0, rc, files = 0, statuses_shown = 0;
	KpRes *res;
	memset(&c, 0, sizeof c);
	c.cache = "generated/cache/repack-vorbis";
	c.log = warn_line;
	for (i = 0; i < argc; i++) {
		if (strcmp(argv[i], "--out") == 0 && i + 1 < argc) c.out = argv[++i];
		else if (strcmp(argv[i], "--single") == 0) c.single = 1;
		else if (strcmp(argv[i], "--wav") == 0) c.wav = 1;
		else if (strcmp(argv[i], "--res-log") == 0 && i + 1 < argc) res_log = argv[++i];
		else if (strcmp(argv[i], "--cache") == 0 && i + 1 < argc) c.cache = argv[++i];
		else if (strcmp(argv[i], "--threads") == 0 && i + 1 < argc) c.threads = atoi(argv[++i]);
		else if (strcmp(argv[i], "--locales") == 0 && i + 1 < argc) locales_spec = argv[++i];
		else if (argv[i][0] == '-' || dir) return -1;
		else dir = argv[i];
	}
	if (!dir) return -1;
	if (!c.out) c.out = c.single ? "generated/repack-single" : "generated/repack";
	if (res_log && !(extra = unlisted_names(res_log, &log_text, &c.extra_count))) return 2;
	c.extra = (const char *const *)extra;
	if (!(res = open_sources(&dir, 1, &shown))) {
		free(extra);
		free(log_text);
		return 2;
	}
	if ((c.locale_count = repack_locales(res, locales_spec, ids, locales)) < 0 || !(rep = (KpRepackReport *)malloc(sizeof *rep))) {
		if (c.locale_count >= 0) fprintf(stderr, "kp_convert: out of memory\n");
		free(extra);
		free(log_text);
		kp_res_close(res);
		return 2;
	}
	c.locales = locales;
	rc = kp_repack_write(res, &c, rep, err, sizeof err);
	for (i = 0; i < rep->missing && i < SHOWN_MISSING && i < KP_REPACK_NAMES; i++)
		fprintf(stderr, "not in the source, left out: %s\n", rep->missing_names[i]);
	if (rep->missing > SHOWN_MISSING) fprintf(stderr, "... %d files left out in all\n", rep->missing);
	for (i = 0; i < rep->damaged && i < KP_REPACK_NAMES; i++)
		fprintf(stderr, "damaged in the source (its copy cannot be read whole), left out: %s\n", rep->damaged_names[i]);
	if (rep->damaged > KP_REPACK_NAMES) fprintf(stderr, "... %d damaged in all\n", rep->damaged);
	if (rc != 0) {
		fprintf(stderr, "kp_convert: %s\n", err);
	} else {
		for (i = 0; i < rep->group_count; i++) snprintf(groups[i], sizeof groups[i], "%s", rep->groups[i].file);
		qsort(groups, (size_t)rep->group_count, sizeof groups[0], cmp_files);
		for (i = 0; i < rep->group_count; i++) {
			int k;
			for (k = 0; strcmp(rep->groups[k].file, groups[i]) != 0; k++) {}
			printf("%s: %d files, %.1f MB\n", groups[i], rep->groups[k].files, (double)rep->groups[k].bytes / 1e6);
			files += rep->groups[k].files;
		}
		printf("%s: %d files (", c.out, files);
		for (i = 0; i < KP_RES_STATUS_COUNT; i++)
			if (rep->statuses[i]) printf("%s%d %s", statuses_shown++ ? ", " : "", rep->statuses[i], kp_res_status_name((KpResStatus)i));
		printf("), %d WAVs as Ogg Vorbis (%.1f MB for %.1f MB), %d encoded\n", rep->ogg, (double)rep->ogg_bytes / 1e6,
			(double)rep->wav_bytes / 1e6, rep->encoded);
		if (c.locale_count > 0) {
			printf("speech of");
			for (i = 0; i < c.locale_count; i++) printf(" %s", locales[i]);
			printf(": %d lines and voices as Ogg Vorbis at %d Hz (%.1f MB for %.1f MB), %d names the install lacks or keeps "
				"empty (the base's)\n", rep->speech, KP_OGG_SPEECH_RATE, (double)rep->speech_bytes / 1e6,
				(double)rep->speech_from / 1e6, rep->speech_absent);
		}
	}
	free(rep);
	free(extra);
	free(log_text);
	kp_res_close(res);
	return rc == 0 ? 0 : 1;
}

/* A PACK payload (u8 count, then u8 length and name a pack) as the index's list "a,b,c" ("-" none). */
static void packs_list(const uint8_t *payload, size_t size, char *out, size_t out_size)
{
	size_t at = 1, o = 0;
	int k, count = size ? payload[0] : 0;
	for (k = 0; k < count && at < size && o + 1 < out_size; k++) {
		size_t n = payload[at++];
		if (at + n > size) break;
		o += (size_t)snprintf(out + o, out_size - o, "%s%.*s", k ? "," : "", (int)n, (const char *)payload + at);
		at += n;
	}
	if (o == 0) snprintf(out, out_size, "-");
}

/* The catalog mission's directory whose short name ("terran/terran01": the directories after the first, in lower case)
 * is base, or NULL. */
static const char *base_directory(const KpCampaign *c, const char *base)
{
	int k;
	for (k = 1; k < c->directory_count; k++) {
		const char *d = c->directories[k], *rest = d ? strchr(d, '\\') : NULL;
		size_t i;
		if (!rest) continue;
		for (i = 0, rest++; rest[i] && base[i]; i++) {
			int a = rest[i] == '\\' ? '/' : (rest[i] >= 'A' && rest[i] <= 'Z' ? rest[i] + 'a' - 'A' : rest[i]);
			if (a != base[i]) break;
		}
		if (!rest[i] && !base[i]) return d;
	}
	return NULL;
}

/* A spec's scenario written (counted in *written when its bytes changed), its index line added: 0, or -1 (reported). */
static int scenario(KpRes *res, const KpCampaign *campaign, const char *path, const char *stem, const char *out, KpBuf *index,
	int *written)
{
	const KpDefsProfile *p = kp_res_profile(res);
	char err[MESSAGE], chk_name[PATH], target[PATH], packs[256];
	const char *name, *base, *directory;
	uint8_t *text = NULL, *base_chk = NULL, *chk = NULL;
	const uint8_t *payload;
	size_t size = 0, base_size = 0, chk_size = 0, payload_size = 0;
	const KpChkSection *era, *dim;
	KpResStatus st;
	KpJsonDoc doc;
	KpJson *spec;
	KpChk read;
	int rc = -1, changed;
	kp_json_doc_init(&doc);
	if (!(text = kp_fs_read(path, &size, err, sizeof err))) {
		fprintf(stderr, "kp_convert: %s\n", err[0] ? err : path);
		goto done;
	}
	if (!(spec = kp_json_parse(&doc, (const char *)text, size, err, sizeof err))) {
		fprintf(stderr, "kp_convert: %s: %s\n", path, err);
		goto done;
	}
	name = kp_json_field_str(spec, "name", stem);
	base = kp_json_field_str(spec, "base", "");
	if (!(directory = base_directory(campaign, base))) {
		fprintf(stderr, "kp_convert: %s: no catalog mission \"%s\"\n", path, base);
		goto done;
	}
	snprintf(chk_name, sizeof chk_name, "%s\\staredit\\scenario.chk", directory);
	if (!(base_chk = kp_res_read(res, chk_name, &base_size, &st, err, sizeof err))) {
		fprintf(stderr, "kp_convert: %s: %s\n", chk_name, err[0] ? err : "not in the install");
		goto done;
	}
	if (!(chk = kp_scenario_chk(base_chk, base_size, spec, &chk_size, err, sizeof err))) {
		fprintf(stderr, "kp_convert: %s: %s\n", path, err);
		goto done;
	}
	if (kp_chk_read(&read, chk, chk_size, p, 1, err, sizeof err) != 0 || !(era = kp_chk_section(&read, "ERA "))
		|| !(dim = kp_chk_section(&read, "DIM ")) || era->size < 2 || dim->size < 4) {
		fprintf(stderr, "kp_convert: %s: the scenario does not load: %s\n", name, err);
		goto done;
	}
	if (!(payload = kp_mission_packs_entry(p, "SPAK", name, &payload_size))) {
		fprintf(stderr, "kp_convert: %s: the profile has no resource packs for it (SPAK; python3 -m converter scr-manifest "
			"records them)\n", name);
		goto done;
	}
	packs_list(payload, payload_size, packs, sizeof packs);
	snprintf(target, sizeof target, "%s/scenarios/%s.chk", out, name);
	if ((changed = kp_fs_write_if_changed(target, chk, chk_size, err, sizeof err)) < 0) {
		fprintf(stderr, "kp_convert: %s\n", err);
		goto done;
	}
	if (changed) printf("wrote %s\n", target);
	*written += changed;
	kp_buf_printf(index, "%s %s %u %u %u %s\n", name, base, kp_le_u16(era->data) & 7u, kp_le_u16(dim->data),
		kp_le_u16(dim->data + 2), packs);
	rc = 0;
done:
	free(text);
	free(base_chk);
	free(chk);
	kp_json_doc_free(&doc);
	return rc;
}

/* A JSON string: UTF-8 as it is, the quote, the backslash and the control bytes escaped. */
static void json_string(KpBuf *b, const char *s)
{
	kp_buf_printf(b, "\"");
	for (; *s; s++) {
		if (*s == '"' || *s == '\\') kp_buf_printf(b, "\\%c", *s);
		else if ((unsigned char)*s < 0x20) kp_buf_printf(b, "\\u%04x", (unsigned)(unsigned char)*s);
		else kp_buf_printf(b, "%c", *s);
	}
	kp_buf_printf(b, "\"");
}

static void json_lines(KpBuf *b, const char *key, char *const *lines, int count, int last)
{
	int k;
	kp_buf_printf(b, " \"%s\": [", key);
	for (k = 0; k < count; k++) {
		kp_buf_printf(b, "%s\n  ", k ? "," : "");
		json_string(b, lines[k]);
	}
	kp_buf_printf(b, "%s]%s\n", count ? "\n " : "", last ? "" : ",");
}

/* A line (malloc'd, taken) at the end of a list of a build's findings; 0, or -1 out of memory (the line freed). */
static int push_line(char ***lines, int *count, int *cap, char *line)
{
	if (*count == *cap) {
		int grown_cap = *cap ? 2 * *cap : 64;
		char **grown = (char **)realloc(*lines, (size_t)grown_cap * sizeof *grown);
		if (!grown) {
			free(line);
			return -1;
		}
		*lines = grown;
		*cap = grown_cap;
	}
	(*lines)[(*count)++] = line;
	return 0;
}

static void move_lines(char ***to, int *count, int *cap, char **from, int *from_count)
{
	int k;
	for (k = 0; k < *from_count; k++) {
		push_line(to, count, cap, from[k]);
		from[k] = NULL;
	}
	*from_count = 0;
}

/* The problems and the texts mended of `from` moved to the end of `to`'s (the report lists them together). */
static void take_findings(KpLocaleFound *to, KpLocaleFound *from)
{
	move_lines(&to->problems, &to->problem_count, &to->problem_cap, from->problems, &from->problem_count);
	move_lines(&to->mended, &to->mended_count, &to->mended_cap, from->mended, &from->mended_count);
}

static int cmp_names(const void *a, const void *b) { return strcmp(*(const char *const *)a, *(const char *const *)b); }

/* A language's report (the Python converter's form until stage C3): its problems' count, -1 when it was not built. */
static int locale_report(const KpData *d, const char *locale, const char *out)
{
	static const char *const TABLES[KP_UI_TBLS] = { "STAT", "NETW", "GLUA", "GHST", "TIPS", "HELP" };
	static const char *const FONTS[KP_UI_FNT_FONTS] = { "U8", "U10", "U14", "U16", "U16X" };
	static const char MISSIONS[] = "missions/";
	KpLocale l;
	KpLocaleGlyphs g;
	KpLocaleFound missions_found, glyphs_found;
	KpError e;
	KpBuf b;
	char path[PATH], err[MESSAGE], (*names)[PATH];
	int k, first = 1, problems, glyphs;
	if (kp_locale_build(&l, &d->ui, &d->raw, locale, &e) != 0) {
		fprintf(stderr, "kp_convert: %s\n", e.msg);
		return -1;
	}
	memset(&missions_found, 0, sizeof missions_found);
	memset(&glyphs_found, 0, sizeof glyphs_found);
	/* the glyphs of the language's texts, as the game builds them before any mission */
	if ((glyphs = kp_locale_glyphs(&g, &d->ui, &d->raw, &l, &glyphs_found, &e)) < 0) {
		fprintf(stderr, "kp_convert: %s %s\n", locale, e.msg);
		kp_locale_free(&l);
		return -1;
	}
	kp_buf_init(&b);
	kp_buf_printf(&b, "{\n \"locale\": \"%s\",\n \"tables\": {", locale);
	for (k = 0; k < KP_UI_TBLS; k++) {
		const KpLocaleTableReport *t = &l.tables[k];
		kp_buf_printf(&b, "%s\n  \"%s\": {\"rows\": %d, \"translated\": %d, \"shorter\": %d, \"bytes\": %llu}", k ? "," : "",
			TABLES[k], t->rows, t->translated, t->shorter, (unsigned long long)t->bytes);
	}
	kp_buf_printf(&b, "\n },\n \"dialogs\": {\"translated\": %d, \"kept\": %d, \"layout\": %d, \"bytes\": %llu},\n",
		l.dialogs_translated, l.dialogs_kept, l.dialogs_laid_out, (unsigned long long)l.pool_size);
	kp_buf_printf(&b, " \"scripts\": {\"translated\": [");
	if (l.script_count > 0 && (names = (char (*)[PATH])malloc((size_t)l.script_count * sizeof *names)) != NULL) {
		const char **order = (const char **)malloc((size_t)l.script_count * sizeof *order);
		for (k = 0; k < l.script_count; k++) snprintf(names[k], PATH, "%.*s", (int)l.scripts[k].path_len, l.scripts[k].path);
		for (k = 0; order && k < l.script_count; k++) order[k] = names[k];
		if (order) qsort(order, (size_t)l.script_count, sizeof *order, cmp_names);
		for (k = 0; k < l.script_count; k++) {
			kp_buf_printf(&b, "%s", k ? ", " : "");
			json_string(&b, order ? order[k] : names[k]);
		}
		free(order);
		free(names);
	}
	kp_buf_printf(&b, "], \"base\": %d},\n \"missions\": {", l.scripts_left);
	for (k = 0; k < d->mission_count; k++) {
		const KpDataMission *entry = &d->missions[k];
		const char *name = entry->key + sizeof MISSIONS - 1;
		KpMission m;
		uint8_t *strs = NULL;
		size_t size = 0;
		int counts[3] = { 0, 0, 0 }, rc;
		if (!entry->catalog || !entry->directory[0] || strncmp(entry->key, MISSIONS, sizeof MISSIONS - 1) != 0) continue;
		kp_buf_printf(&b, "%s\n  ", first ? "" : ",");
		first = 0;
		json_string(&b, name);
		if (kp_data_mission(d, entry, NULL, 0, &m, &e) != 0) {
			kp_buf_printf(&b, ": {}");
			fprintf(stderr, "kp_convert: %s: %s\n", entry->key, e.msg);
			continue;
		}
		rc = kp_locale_mission(&d->raw, locale, name, &m, &missions_found, &strs, &size, counts, &e);
		kp_mission_free(&m);
		free(strs);
		if (rc < 0) { /* the map is damaged, or its strings do not fit the base's: the mission's own text */
			size_t n = (size_t)snprintf(err, sizeof err, "%s: %s", name, e.msg) + 1;
			char *line = (char *)malloc(n < sizeof err ? n : sizeof err);
			if (line) {
				memcpy(line, err, n < sizeof err ? n : sizeof err);
				line[(n < sizeof err ? n : sizeof err) - 1] = 0;
				push_line(&missions_found.problems, &missions_found.problem_count, &missions_found.problem_cap, line);
			}
			kp_buf_printf(&b, ": {}");
			continue;
		}
		if (rc == 0) kp_buf_printf(&b, ": {\"translated\": 0}");
		else kp_buf_printf(&b, ": {\"translated\": %d, \"kept\": %d, \"looked_up\": %d}", counts[0], counts[1], counts[2]);
	}
	kp_buf_printf(&b, "\n },\n \"subtitles\": %d,\n \"glyphs\": ", l.subtitles);
	if (glyphs > 0) {
		kp_locale_glyphs_cover(&g, &d->ui, &missions_found, &glyphs_found); /* the missions' characters too */
		kp_buf_printf(&b, "{\"codes\": %d, \"dropped\": [", g.code_count);
		for (k = 0; k < g.dropped_count; k++) kp_buf_printf(&b, "%s%d", k ? ", " : "", (int)g.dropped[k]);
		kp_buf_printf(&b, "], \"bytes\": {");
		for (k = 0; k < KP_UI_FNT_FONTS; k++)
			kp_buf_printf(&b, "%s\"%s\": %llu", k ? ", " : "", FONTS[k], (unsigned long long)g.supplement_sizes[k]);
		kp_buf_printf(&b, "}},\n");
	} else {
		kp_buf_printf(&b, "null,\n"); /* no TrueType fonts in the source: the FNT fonts' own glyphs */
	}
	take_findings(&l.found, &missions_found);
	take_findings(&l.found, &glyphs_found);
	json_lines(&b, "exceptions", l.found.exceptions, l.found.exception_count, 0);
	json_lines(&b, "mended", l.found.mended, l.found.mended_count, 0);
	json_lines(&b, "problems", l.found.problems, l.found.problem_count, 1);
	kp_buf_printf(&b, "}\n");
	problems = l.found.problem_count;
	snprintf(path, sizeof path, "%s/reports/locale-%s.json", out, locale);
	if (kp_fs_write_if_changed(path, b.data ? b.data : "", b.size, err, sizeof err) < 0) {
		fprintf(stderr, "kp_convert: %s\n", err);
		problems = -1;
	} else {
		printf("%s: %d tables, %d dialog texts, %d scripts, %d movies' subtitles, %d glyphs per font, %d problems (%s)\n",
			locale, KP_UI_TBLS, l.dialogs_translated, l.script_count, l.subtitles, glyphs > 0 ? g.code_count : 0, problems, path);
		for (k = 0; k < problems && k < SHOWN_FAILURES; k++) fprintf(stderr, "%s: %s\n", locale, l.found.problems[k]);
	}
	kp_buf_free(&b);
	if (glyphs > 0) kp_locale_glyphs_free(&g);
	kp_locale_found_free(&missions_found);
	kp_locale_found_free(&glyphs_found);
	kp_locale_free(&l);
	return problems;
}

static int cmd_locale_check(int argc, char **argv)
{
	const char *dir = NULL, *only = "all", *out = "generated", *locales[LOCALES_MAX];
	char ids[LOCALES_MAX][LOCALE_ID], path[PATH], err[MESSAGE];
	int i, count, shown = 0, failed = 0, checked = 0;
	KpData d;
	KpError e;
	KpRes *res;
	for (i = 0; i < argc; i++) {
		if (strcmp(argv[i], "--locale") == 0 && i + 1 < argc) only = argv[++i];
		else if (strcmp(argv[i], "--out") == 0 && i + 1 < argc) out = argv[++i];
		else if (argv[i][0] == '-' || dir) return -1;
		else dir = argv[i];
	}
	if (!dir) return -1;
	snprintf(path, sizeof path, "%s/reports", out);
	if (kp_fs_mkdirs(path, err, sizeof err) != 0) {
		fprintf(stderr, "kp_convert: %s\n", err);
		return 1;
	}
	if (!(res = open_sources(&dir, 1, &shown))) return 2;
	if ((count = repack_locales(res, strcmp(only, "all") == 0 ? NULL : only, ids, locales)) < 0) {
		kp_res_close(res);
		return 2;
	}
	if (kp_data_build(&d, res, NULL, 0, NULL, 0, &e) != 0) {
		fprintf(stderr, "kp_convert: %s\n", e.msg);
		kp_res_close(res);
		return 1;
	}
	for (i = 0; i < count; i++) {
		int problems;
		if (!kp_locale_held(&d.raw, locales[i])) {
			fprintf(stderr, "kp_convert: %s: the source holds none of its strings\n", locales[i]);
			failed = 1;
			continue;
		}
		problems = locale_report(&d, locales[i], out);
		failed |= problems != 0;
		checked++;
	}
	if (checked == 0 && !failed) printf("locale-check: the source holds no language but the base\n");
	kp_data_free(&d);
	kp_res_close(res);
	return failed;
}

static int cmd_scenarios(int argc, char **argv)
{
	const char *dir = NULL, *out = "generated", *specs = "tests/scenarios";
	char path[PATH], err[MESSAGE], **names;
	int i, count = 0, shown = 0, failed = 0, written = 0, changed;
	KpCampaign campaign;
	KpFiles files;
	KpError e;
	KpBuf index;
	KpRes *res;
	for (i = 0; i < argc; i++) {
		if (strcmp(argv[i], "--out") == 0 && i + 1 < argc) out = argv[++i];
		else if (strcmp(argv[i], "--specs") == 0 && i + 1 < argc) specs = argv[++i];
		else if (argv[i][0] == '-' || dir) return -1;
		else dir = argv[i];
	}
	if (!dir) return -1;
	if (!(names = kp_fs_list(specs, ".json", &count, err, sizeof err))) {
		fprintf(stderr, "kp_convert: %s\n", err);
		return 1;
	}
	snprintf(path, sizeof path, "%s/scenarios", out);
	if (kp_fs_mkdirs(path, err, sizeof err) != 0) {
		fprintf(stderr, "kp_convert: %s\n", err);
		failed = 1;
	}
	if (!failed && !(res = open_sources(&dir, 1, &shown))) failed = 2;
	if (!failed) {
		memset(&e, 0, sizeof e);
		kp_res_files(res, &files);
		if (kp_campaign_from_files(&campaign, &files, &e) != 0) {
			fprintf(stderr, "kp_convert: %s\n", e.msg);
			failed = 1;
		} else {
			kp_buf_init(&index);
			for (i = 0; i < count && !failed; i++) {
				char stem[256];
				snprintf(stem, sizeof stem, "%.*s", (int)(strlen(names[i]) - 5), names[i]);
				snprintf(path, sizeof path, "%s/%s", specs, names[i]);
				if (scenario(res, &campaign, path, stem, out, &index, &written) != 0) failed = 1;
			}
			snprintf(path, sizeof path, "%s/scenarios/index.txt", out);
			if (!failed && (changed = kp_fs_write_if_changed(path, index.data ? index.data : "", index.size, err, sizeof err)) < 0) {
				fprintf(stderr, "kp_convert: %s\n", err);
				failed = 1;
			} else if (!failed) {
				printf("scenarios: %d in %s/scenarios (%d written), index.txt %s\n", count, out, written,
					changed ? "written" : "the same");
			}
			kp_buf_free(&index);
			kp_campaign_free(&campaign);
		}
		kp_res_close(res);
	}
	for (i = 0; i < count; i++) free(names[i]);
	free(names);
	return failed;
}

static int cmd_terrain(int argc, char **argv)
{
	const char *dir = NULL, *out = "generated";
	char path[PATH], err[MESSAGE];
	int i, t, shown = 0, failed = 0, written = 0;
	KpFiles files;
	KpRes *res;
	for (i = 0; i < argc; i++) {
		if (strcmp(argv[i], "--out") == 0 && i + 1 < argc) out = argv[++i];
		else if (argv[i][0] == '-' || dir) return -1;
		else dir = argv[i];
	}
	if (!dir || !(res = open_sources(&dir, 1, &shown))) return dir ? 2 : -1;
	kp_res_files(res, &files);
	snprintf(path, sizeof path, "%s/viewer", out);
	if (kp_fs_mkdirs(path, err, sizeof err) != 0) {
		fprintf(stderr, "kp_convert: %s\n", err);
		kp_res_close(res);
		return 1;
	}
	for (t = 0; t < KP_TILESETS && !failed; t++) {
		KpTileset ts;
		KpError e;
		uint8_t *crcs, block[32 * 32];
		int m, y, changed;
		memset(&e, 0, sizeof e);
		if (kp_tileset_from_files(&ts, &files, t, &e) != 0) {
			fprintf(stderr, "kp_convert: %s\n", e.msg);
			failed = 1;
			break;
		}
		if (!(crcs = (uint8_t *)malloc(4 * (size_t)ts.sheet_count + 1))) {
			kp_tileset_free(&ts);
			failed = 1;
			break;
		}
		for (m = 0; m < ts.sheet_count; m++) {
			const uint8_t *at = ts.sheet + (size_t)(m / ts.sheet_columns) * 32 * (size_t)ts.sheet_w + (size_t)(m % ts.sheet_columns) * 32;
			for (y = 0; y < 32; y++) memcpy(block + 32 * y, at + (size_t)y * (size_t)ts.sheet_w, 32);
			kp_le_put32(crcs + 4 * m, kp_crc32(block, sizeof block));
		}
		snprintf(path, sizeof path, "%s/viewer/%s.mcrc", out, KP_TILESET_NAMES[t]);
		changed = kp_fs_write_if_changed(path, crcs, 4 * (size_t)ts.sheet_count, err, sizeof err);
		if (changed < 0) {
			fprintf(stderr, "kp_convert: %s\n", err);
			failed = 1;
		} else if (changed) {
			printf("wrote %s: %d megatiles\n", KP_TILESET_NAMES[t], ts.sheet_count);
			written++;
		}
		free(crcs);
		kp_tileset_free(&ts);
	}
	if (!failed) printf("terrain: %d tilesets in %s/viewer (%d written)\n", KP_TILESETS, out, written);
	kp_res_close(res);
	return failed ? 1 : 0;
}

static const struct {
	const char *name, *usage;
	int (*run)(int argc, char **argv);
} COMMANDS[] = {
	{ "verify", "verify DIR... [--res-log PATH]", cmd_verify },
	{ "digests", "digests DIR --out PATH", cmd_digests },
	{ "extract", "extract DIR NAME OUT", cmd_extract },
	{ "icon", "icon DIR [--out DIR]", cmd_icon },
	{ "repack", "repack DIR [--out DIR] [--single] [--wav] [--res-log FILE] [--cache DIR] [--threads N] [--locales LIST]",
		cmd_repack },
	{ "locale-check", "locale-check DIR [--locale L|all] [--out DIR]", cmd_locale_check },
	{ "scenarios", "scenarios DIR [--specs DIR] [--out DIR]", cmd_scenarios },
	{ "terrain", "terrain DIR [--out DIR]", cmd_terrain },
};

static int usage(void)
{
	size_t k;
	fprintf(stderr, "usage: kp_convert COMMAND ...\n");
	for (k = 0; k < sizeof COMMANDS / sizeof COMMANDS[0]; k++) fprintf(stderr, "  kp_convert %s\n", COMMANDS[k].usage);
	return 2;
}

int main(int argc, char **argv)
{
	size_t k;
	if (argc < 2) return usage();
	for (k = 0; k < sizeof COMMANDS / sizeof COMMANDS[0]; k++) {
		if (strcmp(argv[1], COMMANDS[k].name) == 0) {
			int rc = COMMANDS[k].run(argc - 2, argv + 2);
			if (rc < 0) {
				fprintf(stderr, "usage: kp_convert %s\n", COMMANDS[k].usage);
				return 2;
			}
			return rc;
		}
	}
	return usage();
}
