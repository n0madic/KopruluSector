/*
 * kp_bench ROOT REPLAY [RUNS]: the simulation step time of a native game (plan §10: a step within 4 ms on the target
 * device, none above 8 ms) over a scenario's replay (kp_oracle export_replay): the wall time of each object step
 * (CLOCK_MONOTONIC), the mean, median, 99th percentile and maximum over RUNS runs, and the slowest steps.
 * kp_bench --glyphs DATA LOCALE [RUNS]: the language's glyphs as each start rasterizes them (kp_locale_glyphs over the
 * language's text, ADR 0019, stage C3) from DATA (an install, or a folder of koprulu*.mpq with the language's group):
 * the minimum, median and maximum over RUNS runs, and the text's build before them.
 * kp_bench --dump ROOT GOLDEN [EVERY]: the state dumps of a golden run (tests/golden/oracle) as the C suites take
 * them: every EVERY steps (default 16) the category CRCs of the text dump (kp_state_crcs) and the state hash
 * (kp_dump_hash), each timed (CLOCK_MONOTONIC); the steps' own time, and the last dump's bytes by category.
 * Built for the phone with the Android NDK (tools/native/android_bench.sh) and for the desktop by CMakeLists.txt.
 */
#define _POSIX_C_SOURCE 199309L
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "kp_data.h"
#include "kp_locale.h"
#include "kp_oracle.h"
#include "kp_res.h"
#include "kp_runner.h"
#include "../src/core/kp_kpf.h"

#define SLOWEST 8
#define GLYPH_RUNS_MAX 1000
#define DUMP_EVERY 16      /* test_session's CHECK_EVERY_LONG */
#define DUMP_CATEGORIES 96 /* KP_ORACLE_CATEGORIES */
#define DUMP_SHOWN 12      /* the categories of the most bytes printed */

static double now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

static int by_value(const void *a, const void *b)
{
	double x = *(const double *)a, y = *(const double *)b;
	return x < y ? -1 : x > y;
}

/* kp_bench --glyphs DATA LOCALE [RUNS] (argv past "--glyphs") */
static int glyphs(int argc, char **argv)
{
	const char *dirs[1];
	char why[600];
	int runs = argc > 2 ? atoi(argv[2]) : 1, k, rc = 2, codes = 0;
	double times[GLYPH_RUNS_MAX], t0, text_ms;
	KpResConfig config;
	KpRes *res;
	KpData d;
	KpLocale l;
	KpError err;
	if (argc < 2 || runs < 1 || runs > GLYPH_RUNS_MAX) {
		fprintf(stderr, "usage: kp_bench --glyphs DATA LOCALE [RUNS] (RUNS 1 to %d)\n", GLYPH_RUNS_MAX);
		return 2;
	}
	dirs[0] = argv[0];
	memset(&config, 0, sizeof config);
	if (!(res = kp_res_open(dirs, 1, &config, why, sizeof why))) {
		fprintf(stderr, "kp_bench: %s\n", why);
		return 2;
	}
	if (kp_data_build(&d, res, NULL, 0, NULL, 0, &err) != 0) {
		fprintf(stderr, "kp_bench: %s\n", err.msg);
		kp_res_close(res);
		return 2;
	}
	t0 = now_ms();
	if (kp_locale_build(&l, &d.ui, &d.raw, argv[1], &err) != 0) {
		fprintf(stderr, "kp_bench: %s\n", err.msg);
		goto done;
	}
	text_ms = now_ms() - t0;
	for (k = 0; k < runs; k++) {
		KpLocaleGlyphs g;
		KpLocaleFound found;
		int built;
		memset(&found, 0, sizeof found);
		t0 = now_ms();
		built = kp_locale_glyphs(&g, &d.ui, &d.raw, &l, &found, &err);
		times[k] = now_ms() - t0;
		kp_locale_found_free(&found);
		if (built <= 0) {
			fprintf(stderr, "kp_bench: %s: %s\n", argv[1], built < 0 ? err.msg : "the data holds no TrueType fonts");
			kp_locale_free(&l);
			goto done;
		}
		codes = g.code_count;
		kp_locale_glyphs_free(&g);
	}
	kp_locale_free(&l);
	qsort(times, (size_t)runs, sizeof *times, by_value);
	printf("kp_bench glyphs %s: %d runs, %d characters; min %.1f ms, median %.1f ms, max %.1f ms (the text before them "
		"in %.1f ms)\n", argv[1], runs, codes, times[0], times[runs / 2], times[runs - 1], text_ms);
	rc = 0;
done:
	kp_data_free(&d);
	kp_res_close(res);
	return rc;
}

/* The bytes and lines of a dump by category (kp_dump's lines). */
typedef struct {
	int count;
	char names[DUMP_CATEGORIES][32];
	size_t bytes[DUMP_CATEGORIES], lines[DUMP_CATEGORIES];
} DumpSizes;

static void dump_size_line(void *ud, const char *line, size_t length)
{
	DumpSizes *s = (DumpSizes *)ud;
	size_t c = 0;
	int k;
	while (c < length && line[c] != ' ') c++;
	for (k = 0; k < s->count; k++) {
		if (strlen(s->names[k]) == c && memcmp(s->names[k], line, c) == 0) break;
	}
	if (k == s->count) {
		if (s->count >= DUMP_CATEGORIES || c >= sizeof s->names[0]) return;
		memcpy(s->names[s->count], line, c);
		s->names[s->count++][c] = '\0';
	}
	s->bytes[k] += length + 1;
	s->lines[k]++;
}

/* kp_bench --dump ROOT GOLDEN [EVERY] (argv past "--dump") */
static int dumps(int argc, char **argv)
{
	KpGolden golden;
	KpGoldenRun gr;
	KpError err;
	KpTry t;
	DumpSizes sizes;
	uint64_t h;
	/* set after the setjmp below, or read after it (GCC's -Wclobbered) */
	volatile int64_t step = 0, count = 0;
	volatile double step_ms = 0, crcs_ms = 0, hash_ms = 0;
	volatile uint32_t mix = 0, text = 0; /* the hashes and the text dumps' CRCs over the run (both drawn and not) */
	int every = argc > 2 ? atoi(argv[2]) : DUMP_EVERY, k, j;
	size_t total = 0;
	if (argc < 2 || every < 1) {
		fprintf(stderr, "usage: kp_bench --dump ROOT GOLDEN [EVERY]\n");
		return 2;
	}
	if (kp_golden_load(&golden, argv[1], &err) != 0 || kp_golden_run_start(&gr, argv[0], golden.root, NULL, 0, &err) != 0) {
		fprintf(stderr, "kp_bench: %s\n", err.msg);
		return 2;
	}
	kp_images_begin(gr.game.g->w, &t);
	if (setjmp(t.buf)) {
		kp_images_end(gr.game.g->w, &t);
		fprintf(stderr, "kp_bench: step %lld: %s\n", (long long)step, gr.game.g->w->error);
		return 1;
	}
	for (step = 1; step <= gr.replay.last; step++) {
		double t0 = now_ms();
		KpStateCrcs c;
		kp_runner_to(&gr.run, step);
		step_ms += now_ms() - t0;
		if (step % every != 0) continue;
		t0 = now_ms();
		if (kp_state_crcs(gr.game.g, step >= KP_ORACLE_DRAWN_STEP, &c) != 0) {
			fprintf(stderr, "kp_bench: step %lld: the dump failed\n", (long long)step);
			return 1;
		}
		crcs_ms += now_ms() - t0;
		text = kp_crc32_update(text, (const uint8_t *)c.crcs, (size_t)c.count * sizeof c.crcs[0]);
		for (k = 0; k < c.count; k++) text = kp_crc32_update(text, (const uint8_t *)c.names[k], strlen(c.names[k]));
		t0 = now_ms();
		if (kp_dump_hash(gr.game.g, step >= KP_ORACLE_DRAWN_STEP, &h) != 0) {
			fprintf(stderr, "kp_bench: step %lld: the hash failed\n", (long long)step);
			return 1;
		}
		hash_ms += now_ms() - t0;
		mix = kp_crc32_update(mix, (const uint8_t *)&h, sizeof h);
		if (count++ % 4 == 0) { /* the dump without the drawn fields too */
			if (kp_state_crcs(gr.game.g, 0, &c) != 0 || kp_dump_hash(gr.game.g, 0, &h) != 0) return 1;
			text = kp_crc32_update(text, (const uint8_t *)c.crcs, (size_t)c.count * sizeof c.crcs[0]);
			mix = kp_crc32_update(mix, (const uint8_t *)&h, sizeof h);
		}
	}
	kp_images_end(gr.game.g->w, &t);
	memset(&sizes, 0, sizeof sizes);
	kp_dump(gr.game.g, 1, dump_size_line, &sizes);
	for (k = 0; k < sizes.count; k++) total += sizes.bytes[k];
	printf("kp_bench dump %s: %lld steps in %.0f ms; %lld dumps every %d steps: text CRCs %.3f ms each, hash %.3f ms "
		"each; the last dump %zu bytes; the run's text CRCs %08x, hashes %08x\n", gr.replay.mission,
		(long long)gr.replay.last, step_ms, (long long)count, every, count ? crcs_ms / (double)count : 0,
		count ? hash_ms / (double)count : 0, total, (unsigned)text, (unsigned)mix);
	printf("kp_bench dump bytes by category:");
	for (k = 0; k < DUMP_SHOWN && k < sizes.count; k++) {
		int best = -1;
		for (j = 0; j < sizes.count; j++) {
			if (sizes.lines[j] && (best < 0 || sizes.bytes[j] > sizes.bytes[best])) best = j;
		}
		if (best < 0) break;
		printf(" %s %zu/%zu", sizes.names[best], sizes.bytes[best], sizes.lines[best]);
		sizes.lines[best] = 0;
	}
	printf("\n");
	kp_golden_run_free(&gr);
	kp_golden_free(&golden);
	return 0;
}

int main(int argc, char **argv)
{
	KpReplay r;
	KpError err;
	uint8_t *text;
	size_t size;
	int run_k, k, units = 0;
	/* set after the setjmp below, or read after it (GCC's -Wclobbered) */
	volatile int count = 0, runs = argc > 3 ? atoi(argv[3]) : 1;
	double *times, *sorted;
	volatile double sum = 0;
	int64_t *at;
	if (argc > 1 && strcmp(argv[1], "--glyphs") == 0) return glyphs(argc - 2, argv + 2);
	if (argc > 1 && strcmp(argv[1], "--dump") == 0) return dumps(argc - 2, argv + 2);
	if (argc < 3 || runs < 1) {
		fprintf(stderr, "usage: kp_bench ROOT REPLAY [RUNS] | kp_bench --glyphs DATA LOCALE [RUNS] | kp_bench --dump ROOT "
			"GOLDEN [EVERY]\n");
		return 2;
	}
	text = kp_read_file(argv[2], &size);
	if (!text || kp_replay_parse(&r, (const char *)text, size, &err) != 0) {
		fprintf(stderr, "kp_bench: %s\n", text ? err.msg : "cannot read the replay");
		return 2;
	}
	free(text);
	times = (double *)malloc(sizeof *times * (size_t)(r.last + 1) * (size_t)runs);
	sorted = (double *)malloc(sizeof *sorted * (size_t)(r.last + 1) * (size_t)runs);
	at = (int64_t *)malloc(sizeof *at * (size_t)(r.last + 1) * (size_t)runs);
	if (!times || !sorted || !at) return 2;
	for (run_k = 0; run_k < runs; run_k++) {
		KpReplayGame rg;
		KpRunner run;
		KpTry t;
		int64_t step, n;
		if (kp_replay_game_start(&rg, argv[1], &r, &err) != 0) {
			fprintf(stderr, "kp_bench: %s\n", err.msg);
			return 2;
		}
		kp_runner_init(&run, rg.g, &r);
		kp_images_begin(rg.g->w, &t);
		if (setjmp(t.buf)) {
			kp_images_end(rg.g->w, &t);
			fprintf(stderr, "kp_bench: step %lld: %s\n", (long long)run.step, rg.g->w->error);
			return 1;
		}
		for (step = 2; step <= r.last; step++) {
			double t0 = now_ms();
			kp_runner_to(&run, step);
			times[count] = now_ms() - t0;
			at[count++] = step;
		}
		kp_images_end(rg.g->w, &t);
		units = 0;
		for (n = rg.g->lists[KP_LIST_ACTIVE].head; n != 0; n = rg.g->u.next[n]) units++;
		kp_replay_game_free(&rg);
	}
	if (count == 0) {
		fprintf(stderr, "kp_bench: %s has no steps to time (last %lld)\n", argv[2], (long long)r.last);
		return 2;
	}
	for (k = 0; k < count; k++) {
		sorted[k] = times[k];
		sum += times[k];
	}
	qsort(sorted, (size_t)count, sizeof *sorted, by_value);
	printf("kp_bench %s: %d runs, %d steps, %d active units; mean %.3f ms, median %.3f ms, p99 %.3f ms, max %.3f ms\n",
		r.mission, runs, count, units, sum / count, sorted[count / 2], sorted[(int)((double)count * 0.99)], sorted[count - 1]);
	printf("kp_bench slowest steps (step: ms):");
	for (k = 0; k < SLOWEST && k < count; k++) {
		int j, best = 0;
		for (j = 1; j < count; j++) {
			if (times[j] > times[best]) best = j;
		}
		printf(" %lld: %.3f", (long long)at[best], times[best]);
		times[best] = -1;
	}
	printf("\n");
	free(times);
	free(sorted);
	free(at);
	kp_replay_free(&r);
	return 0;
}
