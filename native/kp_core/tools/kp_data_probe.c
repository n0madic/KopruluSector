/*
 * kp_data_probe: reads every file of the input manifest through kp_res (ADR 0018) from the data folders given (a
 * repack, a 1.17 install, a StarCraft: Remastered install; kp_res_open's rules) and reports each kind's statuses and
 * time (a read includes its check against the manifest).
 *
 *   kp_data_probe DIR...          the exit status is 1 when a file was refused or could not be read, or when a 1.17
 *                                 install's core archives miss a file of the manifest
 *   --res-log PATH                the list of the files read, their status and source (kp_res_write_log)
 *   --digests PATH                each manifest file's normalized name, status and the SHA-256 of the bytes read (empty
 *                                 when none), a tab between, in the manifest's order: the twin of `python3 -m converter
 *                                 scr-digests` (tools/ci.sh compares them over a StarCraft: Remastered install)
 *
 * Run from anywhere: the profile and the manifest are compiled in (kp_defs.h, tools/defs_c.py).
 */
#define _POSIX_C_SOURCE 200112L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "kp_res.h"
#include "kp_sha256.h"

#define DIRS_MAX 8

/* The kinds of file the measurements are kept for: the game reads the core at its start and at a mission's, the
 * mission's speech with the mission, the music as it plays. */
enum { KIND_CORE, KIND_MISSION_WAV, KIND_MUSIC, KIND_COUNT };
static const char *const KIND_NAMES[KIND_COUNT] = { "core", "mission speech", "music" };

typedef struct {
	int files, status[KP_RES_STATUS_COUNT];
	double bytes, seconds;
} Tally;

static double now(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
}

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
	if ((*shown)++ < 20) fprintf(stderr, "%s\n", line);
}

int main(int argc, char **argv)
{
	const char *dirs[DIRS_MAX], *res_log = NULL, *digests_path = NULL;
	FILE *digests = NULL;
	int dir_count = 0, i, k, failed = 0, shown = 0;
	const KpDefsProfile *p = &kp_defs_bw_1_17_0_1;
	KpResConfig config;
	KpRes *res;
	Tally tally[KIND_COUNT];
	char err[1400];
	double t0;
	for (i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--res-log") == 0 && i + 1 < argc) res_log = argv[++i];
		else if (strcmp(argv[i], "--digests") == 0 && i + 1 < argc) digests_path = argv[++i];
		else if (dir_count < DIRS_MAX) dirs[dir_count++] = argv[i];
	}
	if (!dir_count) {
		fprintf(stderr, "usage: kp_data_probe DIR... [--res-log PATH] [--digests PATH]\n");
		return 2;
	}
	if (digests_path && !(digests = fopen(digests_path, "w"))) {
		fprintf(stderr, "kp_data_probe: cannot write %s\n", digests_path);
		return 2;
	}
	memset(&config, 0, sizeof config);
	config.log = log_line;
	config.log_ctx = &shown;
	t0 = now();
	if (!(res = kp_res_open(dirs, dir_count, &config, err, sizeof err))) {
		fprintf(stderr, "kp_data_probe: %s\n", err);
		if (digests) fclose(digests);
		return 2;
	}
	printf("%s%s: opened in %.3f s\n", kp_res_source_name(kp_res_install(res)),
		kp_res_repacks(res) ? " with a repack" : "", now() - t0);
	memset(tally, 0, sizeof tally);
	for (i = 0; i < p->file_count; i++) {
		const char *name = p->files[i].name;
		Tally *t = &tally[kind_of(name)];
		KpResStatus st;
		size_t size = 0;
		uint8_t *b;
		double a = now();
		b = kp_res_read(res, name, &size, &st, err, sizeof err);
		t->seconds += now() - a;
		t->files++;
		t->status[st]++;
		t->bytes += (double)size;
		if (digests) {
			uint8_t digest[KP_SHA256_SIZE];
			char hex[2 * KP_SHA256_SIZE + 1] = "";
			if (b) {
				kp_sha256(b, size, digest);
				kp_sha256_hex(digest, hex);
			}
			fprintf(digests, "%s\t%s\t%s\n", name, kp_res_status_name(st), hex);
		}
		if (st == KP_RES_REFUSED || st == KP_RES_ERROR) {
			if (failed++ < 5) printf("%s\n", err);
		} else if (st == KP_RES_MISSING && kp_res_install(res) == KP_RES_SOURCE_MPQ && kind_of(name) == KIND_CORE) {
			if (failed++ < 5) printf("missing %s\n", name);
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
		fprintf(stderr, "kp_data_probe: cannot write %s\n", digests_path);
		failed = 1;
	}
	if (res_log && kp_res_write_log(res, res_log, err, sizeof err) != 0) {
		fprintf(stderr, "kp_data_probe: %s\n", err);
		failed = 1;
	}
	kp_res_close(res);
	return failed ? 1 : 0;
}
