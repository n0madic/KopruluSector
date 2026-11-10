/*
 * kp_data_probe: reads the files of the input manifest straight from an install (ADR 0018, stage D0) and checks each
 * against the manifest's size and SHA-256, timing the reads and the digests per kind of file.
 *
 *   kp_data_probe mpq DIR    a 1.17 install: the profile's archives (defs/profile/<profile>.json) in its order, the
 *                            first copy of each name (converter/mpq/profile.py Resolver); every listed file must be
 *                            equal, and the exit status is 1 otherwise
 *   kp_data_probe casc DIR   a StarCraft: Remastered install (.build.info): each name under the converter's prefixes
 *                            (converter/casc/source.py PREFIXES); counts the copies equal to the manifest's (the rest
 *                            are moved, rebuilt or Remastered's own, which stage D1 and D6 handle)
 *   --manifest PATH          default defs/scr/inputs-bw-1.17.0.1.json
 *   --profile PATH           default defs/profile/bw-1.17.0.1.json
 *
 * Run from the repository root.
 */
#define _POSIX_C_SOURCE 200112L
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "kp_casc.h"
#include "kp_host.h"
#include "kp_json.h"
#include "kp_sha256.h"

/* last: on Windows it includes windows.h, whose macros (near, far) would rename the project headers' fields */
#include "StormLib.h"

#define ARCHIVES_MAX 8
#define NAME_MAX_BYTES 512

/* The kinds of file the measurements are kept for: the game reads the core at its start and at a mission's, the
 * mission's speech with the mission, the music as it plays. */
enum { KIND_CORE, KIND_MISSION_WAV, KIND_MUSIC, KIND_COUNT };
static const char *const KIND_NAMES[KIND_COUNT] = { "core", "mission speech", "music" };

typedef struct {
	int files, equal, differ, missing;
	double bytes, read_s, hash_s;
} Tally;

static double now(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
}

static int starts_with(const char *s, const char *prefix) { return strncmp(s, prefix, strlen(prefix)) == 0; }

static int kind_of(const char *lower)
{
	size_t n = strlen(lower);
	if (starts_with(lower, "music")) return KIND_MUSIC;
	if (starts_with(lower, "campaign") && n > 4 && strcmp(lower + n - 4, ".wav") == 0) return KIND_MISSION_WAV;
	return KIND_CORE;
}

/* name in lower case with '/' for '\\' (converter/casc/storage.py normalize) */
static void normalize(const char *name, char *out, size_t size)
{
	size_t i;
	for (i = 0; name[i] && i + 1 < size; i++) out[i] = name[i] == '\\' ? '/' : (char)tolower((unsigned char)name[i]);
	out[i] = 0;
}

static const KpJson *load_json(KpJsonDoc *doc, const char *path)
{
	size_t size = 0;
	char err[256];
	uint8_t *text = kp_read_file(path, &size);
	const KpJson *v;
	if (!text) {
		fprintf(stderr, "kp_data_probe: cannot read %s\n", path);
		exit(2);
	}
	v = kp_json_parse(doc, (const char *)text, size, err, sizeof err);
	free(text);
	if (!v) {
		fprintf(stderr, "kp_data_probe: %s: %s\n", path, err);
		exit(2);
	}
	return v;
}

static uint8_t *mpq_read(HANDLE *archives, int count, const char *name, size_t *size)
{
	int i;
	for (i = 0; i < count; i++) {
		HANDLE f = NULL;
		DWORD high = 0, n, got = 0;
		uint8_t *b;
		if (!SFileOpenFileEx(archives[i], name, 0, &f)) continue;
		n = SFileGetFileSize(f, &high);
		b = (uint8_t *)malloc((size_t)n + 1);
		if (!b || !SFileReadFile(f, b, n, &got, NULL) || got != n) {
			free(b);
			SFileCloseFile(f);
			return NULL;
		}
		SFileCloseFile(f);
		*size = n;
		return b;
	}
	return NULL;
}

static const char *const PREFIXES[] = { "sd/", "", "locales/enus/assets/sd/", "locales/enus/assets/" };
#define PREFIX_COUNT ((int)(sizeof PREFIXES / sizeof PREFIXES[0]))

int main(int argc, char **argv)
{
	const char *manifest_path = "defs/scr/inputs-bw-1.17.0.1.json", *profile_path = "defs/profile/bw-1.17.0.1.json";
	const char *source, *root;
	KpJsonDoc doc;
	const KpJson *manifest, *files;
	HANDLE archives[ARCHIVES_MAX];
	int archive_count = 0, casc = 0, i, failed = 0;
	KpCasc *storage = NULL;
	Tally tally[KIND_COUNT];
	double t0, open_s;
	if (argc < 3) {
		fprintf(stderr, "usage: kp_data_probe mpq|casc DIR [--manifest PATH] [--profile PATH]\n");
		return 2;
	}
	source = argv[1], root = argv[2];
	for (i = 3; i + 1 < argc; i += 2) {
		if (strcmp(argv[i], "--manifest") == 0) manifest_path = argv[i + 1];
		else if (strcmp(argv[i], "--profile") == 0) profile_path = argv[i + 1];
	}
	casc = strcmp(source, "casc") == 0;
	if (!casc && strcmp(source, "mpq") != 0) {
		fprintf(stderr, "kp_data_probe: the source is mpq or casc, not %s\n", source);
		return 2;
	}
	kp_json_doc_init(&doc);
	manifest = load_json(&doc, manifest_path);
	files = kp_json_get(manifest, "files");
	t0 = now();
	if (casc) {
		char err[512];
		storage = kp_casc_open(root, err, sizeof err);
		if (!storage) {
			fprintf(stderr, "kp_data_probe: %s\n", err);
			return 2;
		}
	} else {
		const KpJson *list = kp_json_get(load_json(&doc, profile_path), "archives");
		for (i = 0; i < kp_json_len(list) && archive_count < ARCHIVES_MAX; i++) {
			char path[1024];
			snprintf(path, sizeof path, "%s/%s", root, kp_json_field_str(kp_json_at(list, i), "name", ""));
			if (!SFileOpenArchive(path, 0, STREAM_FLAG_READ_ONLY, &archives[archive_count])) {
				fprintf(stderr, "kp_data_probe: cannot open %s (StormLib error %u)\n", path,
					(unsigned)SErrGetLastError());
				return 2;
			}
			archive_count++;
		}
	}
	open_s = now() - t0;
	memset(tally, 0, sizeof tally);
	for (i = 0; i < kp_json_len(files); i++) {
		const KpJson *e = kp_json_at(files, i);
		const char *name = kp_json_str(kp_json_at(e, 0), "");
		int64_t want_size = kp_json_int(kp_json_at(e, 1), -1);
		const char *want_hex = kp_json_str(kp_json_at(e, 2), "");
		char lower[NAME_MAX_BYTES], hex[2 * KP_SHA256_SIZE + 1];
		uint8_t digest[KP_SHA256_SIZE], *b = NULL;
		size_t size = 0;
		double a;
		Tally *t;
		int p;
		normalize(name, lower, sizeof lower);
		t = &tally[kind_of(lower)];
		t->files++;
		a = now();
		if (casc) {
			/* the first copy under the prefixes with the manifest's digest: the converter's "exact" */
			for (p = 0; p < PREFIX_COUNT; p++) {
				char full[NAME_MAX_BYTES + 32], err[512];
				snprintf(full, sizeof full, "%s%s", PREFIXES[p], lower);
				b = kp_casc_read(storage, full, &size, err, sizeof err);
				if (!b) continue;
				t->read_s += now() - a;
				a = now();
				kp_sha256(b, size, digest);
				t->hash_s += now() - a;
				t->bytes += (double)size;
				kp_sha256_hex(digest, hex);
				if ((int64_t)size == want_size && strcmp(hex, want_hex) == 0) break;
				free(b);
				b = NULL;
				a = now();
			}
			if (b) t->equal++;
			else t->differ++;
			free(b);
			continue;
		}
		b = mpq_read(archives, archive_count, name, &size);
		t->read_s += now() - a;
		if (!b) {
			if (t->missing++ < 5) printf("missing %s\n", name);
			failed = 1;
			continue;
		}
		a = now();
		kp_sha256(b, size, digest);
		t->hash_s += now() - a;
		t->bytes += (double)size;
		kp_sha256_hex(digest, hex);
		if ((int64_t)size == want_size && strcmp(hex, want_hex) == 0) {
			t->equal++;
		} else {
			if (t->differ++ < 5) printf("differs %s\n", name);
			failed = 1;
		}
		free(b);
	}
	printf("%s %s: opened in %.3f s\n", source, root, open_s);
	for (i = 0; i < KIND_COUNT; i++)
		printf("  %-15s %5d files: %5d equal, %5d not, %3d missing; %7.1f MB read in %.3f s, SHA-256 in %.3f s\n",
			KIND_NAMES[i], tally[i].files, tally[i].equal, tally[i].differ, tally[i].missing, tally[i].bytes / 1048576.0,
			tally[i].read_s, tally[i].hash_s);
	for (i = 0; i < archive_count; i++) SFileCloseArchive(archives[i]);
	kp_casc_close(storage);
	kp_json_doc_free(&doc);
	return casc ? 0 : failed;
}
