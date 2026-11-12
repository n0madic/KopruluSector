/*
 * kp_bench ROOT REPLAY [RUNS]: the simulation step time of a native game (plan §10: a step within 4 ms on the target
 * device, none above 8 ms) over a scenario's replay (kp_oracle export_replay): the wall time of each object step
 * (CLOCK_MONOTONIC), the mean, median, 99th percentile and maximum over RUNS runs, and the slowest steps.
 * kp_bench --glyphs DATA LOCALE [RUNS]: the language's glyphs as each start rasterizes them (kp_locale_glyphs over the
 * language's text, ADR 0019, stage C3) from DATA (an install, or a folder of koprulu*.mpq with the language's group):
 * the minimum, median and maximum over RUNS runs, and the text's build before them.
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
#include "kp_res.h"
#include "kp_runner.h"

#define SLOWEST 8
#define GLYPH_RUNS_MAX 1000

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
	if (argc < 3 || runs < 1) {
		fprintf(stderr, "usage: kp_bench ROOT REPLAY [RUNS] | kp_bench --glyphs DATA LOCALE [RUNS]\n");
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
