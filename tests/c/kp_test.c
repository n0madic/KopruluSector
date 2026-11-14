/* kp_test: the C suites' runner (kp_test.h). */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L /* lstat */
#endif
#include "kp_test.h"

#include "kp_sha256.h"
#include "kp_webm.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <direct.h>
#include <io.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

static const KpSuite *const SUITES[] = {
	&kp_suite_rng, &kp_suite_initial_state, &kp_suite_scenarios, &kp_suite_damage_table, &kp_suite_save,
	&kp_suite_references, &kp_suite_orders_native, &kp_suite_requirements, &kp_suite_aiscript, &kp_suite_kp_core,
	&kp_suite_kp_ui, &kp_suite_present, &kp_suite_session, &kp_suite_hud, &kp_suite_data, &kp_suite_ui_data, &kp_suite_images,
	&kp_suite_fog, &kp_suite_campaign, &kp_suite_touch, &kp_suite_player_reach, &kp_suite_gfx,
	&kp_suite_flow, &kp_suite_locale, &kp_suite_speech, &kp_suite_script, &kp_suite_res, &kp_suite_formats,
	&kp_suite_original, &kp_suite_chk, &kp_suite_cine, &kp_suite_scr, &kp_suite_movies, &kp_suite_encoders,
	&kp_suite_convert, &kp_suite_hd, &kp_suite_rep,
};
#define SUITE_COUNT ((int)(sizeof SUITES / sizeof SUITES[0]))
#define DEFAULT_LOGS "build/c-tests"
#define MAX_JOBS 4096
/* A parallel run's jobs: each started with this flag, which has it print its time on ELAPSED_LINE; the times kept in
 * TIMES_FILE beside the logs, by which the next run starts the longest jobs first. */
#define TIMED_FLAG "--timed"
#define ELAPSED_LINE "kp_test: elapsed "
#define TIMES_FILE "times"

/* The runtime of the run: the compiler, wasm and the sanitizers. */
static void backend(char *out, size_t size)
{
#if defined(__EMSCRIPTEN__)
	snprintf(out, size, "C (wasm, clang %d.%d)", __clang_major__, __clang_minor__);
#elif defined(__clang__)
	snprintf(out, size, "C (clang %d.%d%s)", __clang_major__, __clang_minor__,
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
		", ASan, UBSan"
#else
		""
#endif
#else
		""
#endif
	);
#elif defined(__GNUC__)
	snprintf(out, size, "C (gcc %d.%d)", __GNUC__, __GNUC_MINOR__);
#else
	snprintf(out, size, "C");
#endif
}

/* --- the context --- */

int kp_t_mine(const KpT *T, int k) { return (k - 1) % (T->shards > 0 ? T->shards : 1) == (T->shard > 0 ? T->shard : 1) - 1; }

/* Whether item k is in the sample of a sampled run (kp_t_mine_sampled). */
static int sampled(const KpT *T, int k)
{
	uint32_t hash = 2166136261u ^ T->seed; /* FNV-1a */
	const char *c;
	if (T->sample <= 1) return 1;
	for (c = T->test; *c; c++) hash = (hash ^ (uint8_t)*c) * 16777619u;
	hash = (hash ^ (uint32_t)k) * 16777619u;
	/* MurmurHash3's finalizer: FNV carries only upwards, so its low bits follow k's in a loop of some dozens of items,
	 * which then gather in a few shards (and the processes that hold them set the test's time), and a seed's high bits
	 * never reach them */
	hash ^= hash >> 16;
	hash *= 0x85ebca6bu;
	hash ^= hash >> 13;
	hash *= 0xc2b2ae35u;
	hash ^= hash >> 16;
	return hash % (uint32_t)T->sample == 0;
}

int kp_t_mine_sampled(const KpT *T, int k) { return kp_t_mine(T, k) && sampled(T, k); }

typedef struct {
	double weight;
	int item;
} Weighted;

static int heaviest_first(const void *a, const void *b)
{
	const Weighted *x = (const Weighted *)a, *y = (const Weighted *)b;
	if (x->weight != y->weight) return x->weight > y->weight ? -1 : 1;
	return x->item - y->item;
}

int kp_t_mine_weighted(const KpT *T, int count, const double *weights, unsigned char *mine)
{
	int shards = T->shards > 0 ? T->shards : 1, shard = (T->shard > 0 ? T->shard : 1) - 1, k, n = 0;
	Weighted *order = (Weighted *)malloc((count > 0 ? (size_t)count : 1) * sizeof *order);
	double *load = (double *)calloc((size_t)shards, sizeof *load);
	if (!order || !load) {
		free(order);
		free(load);
		return -1;
	}
	for (k = 0; k < count; k++) {
		mine[k] = 0;
		if (!sampled(T, k + 1)) continue;
		order[n].weight = weights[k];
		order[n++].item = k;
	}
	qsort(order, (size_t)n, sizeof *order, heaviest_first);
	for (k = 0; k < n; k++) {
		int s, least = 0;
		for (s = 1; s < shards; s++) {
			if (load[s] < load[least]) least = s;
		}
		load[least] += order[k].weight;
		if (least == shard) mine[order[k].item] = 1;
	}
	free(order);
	free(load);
	return 0;
}

const unsigned char *kp_t_mine_goldens(KpT *T, const KpJson *index, const char *path)
{
	int count = kp_json_len(index), k, rc;
	double *weights = (double *)malloc((count > 0 ? (size_t)count : 1) * sizeof *weights);
	unsigned char *mine = (unsigned char *)malloc(count > 0 ? (size_t)count : 1);
	if (!weights || !mine) {
		free(weights);
		free(mine);
		kp_t_fail(T, "out of memory");
	}
	kp_t_defer(T, free, mine);
	for (k = 0; k < count; k++) {
		char name[512];
		const KpJson *steps;
		int mark = kp_t_mark(T), i;
		snprintf(name, sizeof name, path, kp_json_str(kp_json_at(index, k), ""));
		weights[k] = 0;
		if (!sampled(T, k + 1)) continue; /* out of the sample: no shard deals it, its weight is never read */
		if (kp_t_has(T, name) && (steps = kp_json_get(kp_t_golden(T, name), "steps")) != NULL) {
			for (i = 0; i < kp_json_len(steps); i++) {
				double step = strtod(steps->keys[i], NULL);
				if (step > weights[k]) weights[k] = step;
			}
		}
		kp_t_release(T, mark); /* a missing golden weighs nothing: the test reports it */
	}
	rc = kp_t_mine_weighted(T, count, weights, mine);
	free(weights);
	if (rc != 0) kp_t_fail(T, "out of memory");
	return mine;
}

void kp_t_fail(KpT *T, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(T->message, sizeof T->message, fmt, ap);
	va_end(ap);
	if (!T->message[0]) snprintf(T->message, sizeof T->message, "assertion failed");
	longjmp(T->catch_.buf, 1);
}

void kp_t_skip(KpT *T, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(T->message, sizeof T->message, fmt, ap);
	va_end(ap);
	T->skipped = 1;
	longjmp(T->catch_.buf, 1);
}

/* A golden file that a public checkout lacks (KP_TEST_PUBLIC, set by tools/ci.sh without tests/golden/): the test is
 * skipped; in any other run a missing file is a failure. */
static void skip_missing_golden(KpT *T, const char *path)
{
	const char *pub = getenv("KP_TEST_PUBLIC");
	if (pub && pub[0] && strncmp(path, "tests/golden/", 13) == 0 && !kp_t_has(T, path)) {
		kp_t_skip(T, "no %s (not published)", path);
	}
}

static void vfail(KpT *T, const char *prefix, const char *fmt, va_list ap, const char *suffix)
{
	char msg[KP_T_MESSAGE / 2];
	msg[0] = '\0';
	if (fmt) vsnprintf(msg, sizeof msg, fmt, ap);
	if (prefix) snprintf(T->message, sizeof T->message, "%s%s%s", msg, msg[0] ? ": " : "", suffix);
	else snprintf(T->message, sizeof T->message, "%s", msg[0] ? msg : "assertion failed");
	longjmp(T->catch_.buf, 1);
}

void kp_t_ok(KpT *T, int cond, const char *fmt, ...)
{
	va_list ap;
	if (cond) return;
	va_start(ap, fmt);
	vfail(T, NULL, fmt, ap, NULL);
}

void kp_t_eq(KpT *T, int64_t actual, int64_t expected, const char *fmt, ...)
{
	va_list ap;
	char what[96];
	if (actual == expected) return;
	snprintf(what, sizeof what, "expected %lld, got %lld", (long long)expected, (long long)actual);
	va_start(ap, fmt);
	vfail(T, "", fmt, ap, what);
}

void kp_t_eq_str(KpT *T, const char *actual, const char *expected, const char *fmt, ...)
{
	va_list ap;
	char *what;
	size_t n;
	if (strcmp(actual, expected) == 0) return;
	n = strlen(actual) + strlen(expected) + 32;
	what = (char *)malloc(n);
	if (what) {
		snprintf(what, n, "expected \"%s\", got \"%s\"", expected, actual);
		kp_t_defer(T, free, what);
	}
	va_start(ap, fmt);
	vfail(T, "", fmt, ap, what ? what : "strings differ");
}

void kp_t_log(KpT *T, const char *fmt, ...)
{
	va_list ap;
	(void)T;
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	putchar('\n');
	fflush(stdout);
}

void kp_t_defer(KpT *T, void (*fn)(void *), void *p)
{
	if (T->cleanup_count >= KP_T_CLEANUPS) {
		fn(p);
		kp_t_fail(T, "kp_test: more than %d resources in one test", KP_T_CLEANUPS);
	}
	T->cleanups[T->cleanup_count].fn = fn;
	T->cleanups[T->cleanup_count].p = p;
	T->cleanup_count++;
}

uint8_t *kp_t_read(KpT *T, const char *path, size_t *size)
{
	size_t n;
	uint8_t *data;
	skip_missing_golden(T, path);
	data = kp_read_file(path, size ? size : &n);
	if (!data) kp_t_fail(T, "cannot read %s", path);
	kp_t_defer(T, free, data);
	return data;
}

int kp_t_has(const KpT *T, const char *path)
{
	KpError err;
	const KpData *d;
	const KpDataMission *m;
	char chk[256];
	FILE *f;
	int key = strncmp(path, "missions/", 9) == 0 || strncmp(path, "scenarios/", 10) == 0;
	(void)T;
	if (strcmp(path, KP_T_DATA) == 0 || key) { /* the original data, a mission of its catalog */
		if (!(d = kp_oracle_data(".", &err))) return 0;
		if (!key) return 1;
		if (!(m = kp_data_find(d, path))) return 0;
		if (m->catalog) return 1;
		snprintf(chk, sizeof chk, KP_DATA_SCENARIOS "%s.chk", path + 10);
		path = chk;
	}
	if (!(f = fopen(path, "rb"))) return 0;
	fclose(f);
	return 1;
}

static void free_golden(void *p)
{
	kp_golden_free((KpGolden *)p);
	free(p);
}

const KpJson *kp_t_golden(KpT *T, const char *path)
{
	KpGolden *g;
	KpError err;
	skip_missing_golden(T, path); /* before the allocation: the skip ends the test without freeing it */
	g = (KpGolden *)malloc(sizeof *g);
	if (!g) kp_t_fail(T, "out of memory");
	if (kp_golden_load(g, path, &err) != 0) {
		free(g);
		kp_t_fail(T, "%s", err.msg);
	}
	kp_t_defer(T, free_golden, g);
	return g->root;
}

static void free_game(void *p)
{
	KpOracleGame *og = (KpOracleGame *)p;
	if (og->g) kp_oracle_game_free(og);
	free(og);
}

static void unguard(KpT *T, KpGame *g)
{
	int k;
	for (k = 0; k < T->guarded_count; k++) {
		if (T->guarded[k] == g) T->guarded[k] = NULL;
	}
}

typedef struct {
	KpT *T;
	KpGame *g;
} Guard;

static void end_guard(void *p)
{
	Guard *guard = (Guard *)p;
	unguard(guard->T, guard->g);
	free(guard);
}

void kp_t_guard(KpT *T, KpGame *g)
{
	Guard *guard;
	int k;
	g->w->try_ = &T->catch_;
	g->w->error[0] = '\0';
	for (k = 0; k < T->guarded_count; k++) {
		if (T->guarded[k] == g) return;
	}
	for (k = 0; k < T->guarded_count && T->guarded[k]; k++) {
	}
	if (k == T->guarded_count) {
		if (T->guarded_count >= KP_T_CLEANUPS) kp_t_fail(T, "kp_test: more than %d games in one test", KP_T_CLEANUPS);
		T->guarded_count++;
	}
	T->guarded[k] = g;
	guard = (Guard *)malloc(sizeof *guard);
	if (!guard) kp_t_fail(T, "out of memory");
	guard->T = T;
	guard->g = g;
	kp_t_defer(T, end_guard, guard); /* before the game's release (the cleanups run in reverse) */
}

const KpData *kp_t_data(KpT *T)
{
	KpError err;
	const KpData *d = kp_oracle_data(".", &err);
	if (!d) kp_t_skip(T, "%s", err.msg);
	return d;
}

const KpData *kp_t_data_117(KpT *T)
{
	const KpData *d = kp_t_data(T);
	KpResSourceKind origin = kp_oracle_data_origin(".");
	char dir[600];
	if (origin != KP_RES_SOURCE_MPQ) {
		kp_data_dir(".", dir, sizeof dir);
		kp_t_fail(T, "the original data (%s) are %s files, and the original's recordings of its interface were made "
			"over 1.17's: write the repack from the 1.17 install (make repack SC=DIR)", dir,
			origin == KP_RES_SOURCE_CASC ? "StarCraft: Remastered's" : "an unknown install's");
	}
	return d;
}

KpOracleGame *kp_t_game(KpT *T, const char *key, uint32_t seed, int expansion, const KpOracleOpts *opts)
{
	KpOracleGame *og;
	KpError err;
	kp_t_data(T); /* skips without it */
	if (!(og = (KpOracleGame *)calloc(1, sizeof *og))) kp_t_fail(T, "out of memory");
	if (kp_oracle_game_start(og, ".", key, seed, expansion, opts, &err) != 0) {
		free(og);
		kp_t_fail(T, "%s", err.msg);
	}
	kp_t_defer(T, free_game, og);
	kp_t_guard(T, og->g);
	return og;
}

void kp_t_game_free(KpT *T, KpOracleGame *og)
{
	if (!og->g) return;
	unguard(T, og->g);
	kp_oracle_game_free(og);
}

KpOracleGame *kp_t_probe_game(KpT *T, const char *map, uint32_t seed, int expansion, int no_units)
{
	KpOracleOpts o;
	kp_oracle_opts(&o);
	o.host.events = 1;
	o.host.mission_index = 0;
	o.host.no_units = no_units;
	o.name = "mission";
	return kp_t_game(T, map, seed, expansion, &o);
}

void kp_t_golden_run_release(KpGoldenRun *gr)
{
	kp_golden_run_free(gr);
	free(gr);
}

static void free_golden_run(void *p) { kp_t_golden_run_release((KpGoldenRun *)p); }

KpGoldenRun *kp_t_golden_run(KpT *T, const KpJson *g, const KpOracleOpts *opts, int trace)
{
	KpGoldenRun *gr;
	KpError err;
	kp_t_data(T); /* skips without it */
	if (!(gr = (KpGoldenRun *)malloc(sizeof *gr))) kp_t_fail(T, "out of memory");
	if (kp_golden_run_start(gr, ".", g, opts, trace, &err) != 0) {
		free(gr);
		kp_t_fail(T, "%s", err.msg);
	}
	kp_t_defer(T, free_golden_run, gr);
	kp_t_guard(T, gr->game.g);
	return gr;
}

static void free_replay(void *p)
{
	kp_replay_free((KpReplay *)p);
	free(p);
}

KpReplay *kp_t_replay(KpT *T)
{
	KpReplay *r = (KpReplay *)malloc(sizeof *r);
	if (!r) kp_t_fail(T, "out of memory");
	kp_replay_init(r);
	kp_t_defer(T, free_replay, r);
	return r;
}

/* The first of files that is not there (kp_t_has), NULL when all are */
static const char *first_missing(const KpT *T, const char *const *files)
{
	int k;
	for (k = 0; files && files[k]; k++) {
		if (!kp_t_has(T, files[k])) return files[k];
	}
	return NULL;
}

int kp_t_ready(KpT *T, const char *label, const char *const *files)
{
	const char *missing = first_missing(T, files);
	if (missing) kp_t_log(T, "SKIP %s: no %s", label, missing);
	return missing == NULL;
}

void kp_t_need(KpT *T, const char *const *files)
{
	const char *missing = first_missing(T, files);
	if (missing) kp_t_skip(T, "no %s", missing);
}

char *kp_t_strdup(KpT *T, const char *s)
{
	size_t n = strlen(s) + 1;
	char *copy = (char *)malloc(n);
	if (!copy) kp_t_fail(T, "out of memory");
	memcpy(copy, s, n);
	kp_t_defer(T, free, copy);
	return copy;
}

int kp_t_mkdir(const char *path)
{
#ifdef _WIN32
	return _mkdir(path);
#else
	return mkdir(path, 0700);
#endif
}

/* A folder and everything in it removed (what cannot be is left). */
static void remove_tree(const char *path)
{
	char p[KP_T_PATH];
#ifdef _WIN32
	struct _finddata_t found;
	intptr_t h;
	snprintf(p, sizeof p, "%s/*", path);
	if ((h = _findfirst(p, &found)) != -1) {
		do {
			if (strcmp(found.name, ".") == 0 || strcmp(found.name, "..") == 0) continue;
			snprintf(p, sizeof p, "%s/%s", path, found.name);
			if (found.attrib & _A_SUBDIR) remove_tree(p);
			else remove(p);
		} while (_findnext(h, &found) == 0);
		_findclose(h);
	}
	_rmdir(path);
#else
	DIR *d = opendir(path);
	struct dirent *e;
	struct stat st;
	if (d) {
		while ((e = readdir(d)) != NULL) {
			if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) continue;
			snprintf(p, sizeof p, "%s/%s", path, e->d_name);
			if (lstat(p, &st) == 0 && S_ISDIR(st.st_mode)) remove_tree(p);
			else unlink(p);
		}
		closedir(d);
	}
	rmdir(path); /* not remove(): musl's (wasm) takes a folder for rmdir only when unlink says EISDIR, macOS says EPERM */
#endif
}

static void temp_dir_release(void *p) { remove_tree((const char *)p); }

const char *kp_t_temp_dir(KpT *T, const char *parent)
{
	static unsigned serial; /* a process's folders apart; another process's apart by mkdir failing on its name */
	unsigned seed = (unsigned)time(NULL) * 2654435761u ^ (unsigned)(uintptr_t)&serial;
	char *dir = (char *)malloc(KP_T_PATH);
	int tries;
	if (!dir) kp_t_fail(T, "out of memory");
	kp_t_defer(T, free, dir); /* the cleanups run last first: the folder is removed before its name is freed */
	for (tries = 0; tries < 100; tries++) {
		snprintf(dir, KP_T_PATH, "%s/%s_%08x", parent ? parent : "build", T->test, seed + ++serial * 0x9e3779b9u);
		if (kp_t_mkdir(dir) == 0) {
			kp_t_defer(T, temp_dir_release, dir);
			return dir;
		}
		if (errno != EEXIST) break;
	}
	kp_t_fail(T, "a directory %s: %s", dir, strerror(errno));
}

/* The failure message of a game whose C code failed (its world's error). */
static const char *game_failure(const KpT *T)
{
	int k;
	for (k = 0; k < T->guarded_count; k++) {
		if (T->guarded[k] && T->guarded[k]->w->error[0]) return T->guarded[k]->w->error;
	}
	return "a game failed";
}

static void free_buf(void *p)
{
	kp_buf_free((KpBuf *)p);
	free(p);
}

KpBuf *kp_t_buf(KpT *T)
{
	KpBuf *b = (KpBuf *)malloc(sizeof *b);
	if (!b) kp_t_fail(T, "out of memory");
	kp_buf_init(b);
	kp_buf_text(b);
	kp_t_defer(T, free_buf, b);
	return b;
}

int kp_t_pcall(KpT *T, void (*fn)(KpT *T, void *ud), void *ud, char *msg, size_t size)
{
	KpTry outer = T->catch_; /* the games' handlers point at T->catch_: a failure inside lands here */
	volatile int failed = 0;
	int k;
	if (setjmp(T->catch_.buf)) {
		failed = 1;
	} else {
		fn(T, ud);
	}
	T->catch_ = outer;
	if (failed) {
		snprintf(msg, size, "%s", T->message[0] ? T->message : game_failure(T));
		T->message[0] = '\0';
		for (k = 0; k < T->guarded_count; k++) {
			if (T->guarded[k]) T->guarded[k]->w->error[0] = '\0';
		}
		return -1;
	}
	if (size) msg[0] = '\0';
	return 0;
}

int kp_t_mark(const KpT *T) { return T->cleanup_count; }

void kp_t_release(KpT *T, int mark)
{
	while (T->cleanup_count > mark) {
		T->cleanup_count--;
		T->cleanups[T->cleanup_count].fn(T->cleanups[T->cleanup_count].p);
	}
}

/* Runs one test; 0 passed, 1 failed (the FAIL line printed). */
static int run_test(const KpSuite *s, const KpTest *t, int shard, int shards, int sample, uint32_t seed)
{
	KpT *T = (KpT *)calloc(1, sizeof *T);
	volatile int failed = 0;
	if (!T) {
		printf("FAIL %s.%s: out of memory\n", s->name, t->name);
		return 1;
	}
	T->suite = s->name;
	T->test = t->name;
	T->shard = shard;
	T->shards = shards;
	T->sample = sample;
	T->seed = seed;
	if (setjmp(T->catch_.buf)) {
		failed = 1;
	} else {
		t->fn(T);
	}
	if (failed && T->skipped) {
		printf("SKIP %s.%s: %s\n", s->name, t->name, T->message);
		fflush(stdout);
		failed = 0;
	} else if (failed) {
		printf("FAIL %s.%s: %s\n", s->name, t->name, T->message[0] ? T->message : game_failure(T));
		fflush(stdout);
	}
	kp_t_release(T, 0);
	free(T);
	return failed;
}

static const KpSuite *find_suite(const char *name, size_t n)
{
	int k;
	for (k = 0; k < SUITE_COUNT; k++) {
		if (strlen(SUITES[k]->name) == n && strncmp(SUITES[k]->name, name, n) == 0) return SUITES[k];
	}
	return NULL;
}

/* A suite's tests sorted by name. */
static int by_test_name(const void *a, const void *b)
{
	return strcmp((*(const KpTest *const *)a)->name, (*(const KpTest *const *)b)->name);
}

static int sorted_tests(const KpSuite *s, const KpTest **out)
{
	int k;
	for (k = 0; k < s->count; k++) out[k] = &s->tests[k];
	qsort(out, (size_t)s->count, sizeof *out, by_test_name);
	return s->count;
}

/* An entry: SUITE, SUITE:TEST, SUITE:TEST@I/N or SUITE:TEST@I/N/SAMPLE/SEED; counts into *passed and *failed. -1 for an unknown entry. */
static int run_entry(const char *entry, int *passed, int *failed)
{
	const char *colon = strchr(entry, ':');
	const KpSuite *s = find_suite(entry, colon ? (size_t)(colon - entry) : strlen(entry));
	const KpTest *tests[512];
	int k, count, shard = 0, shards = 0, sample = 0;
	unsigned seed = 0;
	char only[256] = "";
	if (!s) {
		printf("FAIL %s: no such suite\n", entry);
		return -1;
	}
	if (colon) {
		const char *at = strchr(colon + 1, '@');
		size_t n = at ? (size_t)(at - colon - 1) : strlen(colon + 1);
		if (n >= sizeof only) n = sizeof only - 1;
		memcpy(only, colon + 1, n);
		only[n] = '\0';
		if (at) {
			int fields = sscanf(at + 1, "%d/%d/%d/%u", &shard, &shards, &sample, &seed);
			if (fields != 2 && (fields != 4 || sample < 1)) {
				printf("FAIL %s: bad shard\n", entry);
				return -1;
			}
		}
	}
	count = sorted_tests(s, tests);
	for (k = 0; k < count; k++) {
		if (only[0] && strcmp(tests[k]->name, only) != 0) continue;
		if (run_test(s, tests[k], shard, shards, sample, seed)) (*failed)++;
		else (*passed)++;
		if (only[0]) return 0;
	}
	if (only[0]) {
		printf("FAIL %s: no test %s.%s\n", entry, s->name, only);
		return -1;
	}
	return 0;
}

/* --- the parallel run --- */

typedef struct {
	char entry[300];
	char test[300]; /* "" for a whole suite's job, else SUITE.TEST of a sharded test */
	double seconds; /* its time in an earlier run, or < 0 */
} Job;

static int by_shard_first(const void *a, const void *b)
{
	const Job *x = (const Job *)a, *y = (const Job *)b;
	int sx = strchr(x->entry, '@') != NULL, sy = strchr(y->entry, '@') != NULL;
	if (sx != sy) return sx ? -1 : 1;
	return strcmp(x->entry, y->entry);
}

/* The order a parallel run starts its jobs in (xargs takes them in turn as processes end): the jobs no earlier run
 * timed first, the shards before the whole suites, then the others the longest first, so that the run ends with short
 * jobs instead of one long job alone. */
static int by_time(const void *a, const void *b)
{
	const Job *x = (const Job *)a, *y = (const Job *)b;
	if ((x->seconds < 0) != (y->seconds < 0)) return x->seconds < 0 ? -1 : 1;
	if (x->seconds < 0 || x->seconds == y->seconds) return by_shard_first(a, b);
	return x->seconds > y->seconds ? -1 : 1;
}

/* A job's key in TIMES_FILE: its entry without the sample's seed (a sampled shard takes about as long whatever the
 * seed; its sample stays, a sampled shard being shorter than the whole one). */
static void time_key(const char *entry, char *out, size_t size)
{
	const char *at = strchr(entry, '@'), *c;
	size_t n = strlen(entry);
	int slashes = 0;
	if (at) {
		for (c = at; *c; c++) slashes += *c == '/';
		if (slashes == 3) n = (size_t)(strrchr(entry, '/') - entry);
	}
	if (n >= size) n = size - 1;
	memcpy(out, entry, n);
	out[n] = '\0';
}

typedef struct {
	char key[300];
	double seconds;
	int current; /* a job of this run's */
} JobTime;

typedef struct {
	JobTime *list;
	int count, cap;
} JobTimes;

static JobTime *job_time(JobTimes *t, const char *key, int add)
{
	int k;
	for (k = 0; k < t->count; k++) {
		if (strcmp(t->list[k].key, key) == 0) return &t->list[k];
	}
	if (!add) return NULL;
	if (t->count == t->cap) {
		int cap = t->cap ? 2 * t->cap : 256;
		JobTime *list = (JobTime *)realloc(t->list, (size_t)cap * sizeof *list);
		if (!list) return NULL;
		t->list = list;
		t->cap = cap;
	}
	snprintf(t->list[t->count].key, sizeof t->list[0].key, "%s", key);
	t->list[t->count].seconds = -1;
	t->list[t->count].current = 0;
	return &t->list[t->count++];
}

/* TIMES_FILE's lines "KEY SECONDS" (none: an empty table). */
static void read_times(const char *dir, JobTimes *t)
{
	char path[1024], key[300], *text, *line;
	size_t size;
	double seconds;
	JobTime *slot;
	snprintf(path, sizeof path, "%s/" TIMES_FILE, dir);
	if (!(text = (char *)kp_read_file(path, &size))) return;
	for (line = strtok(text, "\n"); line; line = strtok(NULL, "\n")) {
		if (sscanf(line, "%299s %lf", key, &seconds) == 2 && seconds >= 0 && (slot = job_time(t, key, 1))) slot->seconds = seconds;
	}
	free(text);
}

/* The times of this run's jobs (a parallel run has every job, so another key is a job no more: a test's shards before
 * their count changed), written whole (a stopped run leaves the earlier file). */
static void write_times(const char *dir, const JobTimes *t)
{
	char path[1024], tmp[1100];
	FILE *f;
	int k;
	snprintf(path, sizeof path, "%s/" TIMES_FILE, dir);
	snprintf(tmp, sizeof tmp, "%s.tmp", path);
	if (!(f = fopen(tmp, "w"))) return;
	for (k = 0; k < t->count; k++) {
		if (t->list[k].current && t->list[k].seconds >= 0) fprintf(f, "%s %.3f\n", t->list[k].key, t->list[k].seconds);
	}
	if (fclose(f) != 0 || rename(tmp, path) != 0) remove(tmp);
}

/* The jobs of a parallel run: each suite in a process of its own; a suite with sharded tests or split (KP_SUITE_SPLIT)
 * one job a test, each sharded test one job per shard. The shards go first: they are the long ones. */
static int list_jobs(Job *jobs, int max, int sample, unsigned seed)
{
	int k, count = 0;
	for (k = 0; k < SUITE_COUNT; k++) {
		const KpSuite *s = SUITES[k];
		const KpTest *tests[512];
		int n = sorted_tests(s, tests), i, sharded = 0;
		for (i = 0; i < n; i++) sharded |= tests[i]->shards > 1;
		if (!sharded && !s->split) {
			if (count < max) {
				snprintf(jobs[count].entry, sizeof jobs[count].entry, "%s", s->name);
				jobs[count].seconds = -1;
				jobs[count++].test[0] = '\0';
			}
			continue;
		}
		for (i = 0; i < n; i++) {
			int shards = tests[i]->shards > 1 ? tests[i]->shards : 1, j;
			for (j = 1; j <= shards && count < max; j++) {
				Job *job = &jobs[count++];
				job->seconds = -1;
				if (shards > 1) {
					if (sample > 1) snprintf(job->entry, sizeof job->entry, "%s:%s@%d/%d/%d/%u", s->name, tests[i]->name, j, shards, sample, seed);
					else snprintf(job->entry, sizeof job->entry, "%s:%s@%d/%d", s->name, tests[i]->name, j, shards);
					snprintf(job->test, sizeof job->test, "%s.%s", s->name, tests[i]->name);
				} else {
					snprintf(job->entry, sizeof job->entry, "%s:%s", s->name, tests[i]->name);
					job->test[0] = '\0';
				}
			}
		}
	}
	qsort(jobs, (size_t)count, sizeof *jobs, by_shard_first);
	return count;
}

/* The log of a job: its entry with ":@/" as "_". */
static void log_of(const char *dir, const char *entry, char *out, size_t size)
{
	size_t k, n = (size_t)snprintf(out, size, "%s/", dir);
	for (k = 0; entry[k] && n + 5 < size; k++) {
		char c = entry[k];
		out[n++] = c == ':' || c == '@' || c == '/' ? '_' : c;
	}
	snprintf(out + n, size - n, ".log");
}

/* A word quoted for sh. */
static void quote(const char *word, char *out, size_t size)
{
	size_t n = 0, k;
	if (size < 3) return;
	out[n++] = '\'';
	for (k = 0; word[k] && n + 6 < size; k++) {
		if (word[k] == '\'') {
			memcpy(out + n, "'\\''", 4);
			n += 4;
		} else {
			out[n++] = word[k];
		}
	}
	out[n++] = '\'';
	out[n] = '\0';
}

/* The line a job's shell appends to its log: the exit status of the program, which run_parallel reads back. */
#define EXIT_STATUS_LINE "kp_test: exit status "

/* Whether every shard of a sharded test passed. */
typedef struct {
	char test[300];
	int ok;
} ShardOk;

static int run_parallel(const char *self, int jobs_wanted)
{
	const char *dir = getenv("KP_TEST_LOGS") ? getenv("KP_TEST_LOGS") : DEFAULT_LOGS;
	const char *exec_env = getenv("KP_TEST_EXEC");
	Job *jobs = (Job *)calloc(MAX_JOBS, sizeof *jobs);
	const char *sample_env = getenv("KP_TEST_SAMPLE"), *seed_env = getenv("KP_TEST_SEED");
	char path[1024], key[300], command[4096], exec[1200], job[2048], quoted[2400], label[160], sampled[64] = "";
	int count, k, passed = 0, failed = 0, sample = sample_env ? atoi(sample_env) : 0;
	unsigned seed = seed_env ? (unsigned)strtoul(seed_env, NULL, 10) : 0;
	FILE *list;
	ShardOk *shard_ok;
	JobTimes times = { NULL, 0, 0 };
	int shard_count = 0;
	if (!jobs) return 1;
	count = list_jobs(jobs, MAX_JOBS, sample, seed);
	read_times(dir, &times);
	for (k = 0; k < count; k++) {
		JobTime *known;
		time_key(jobs[k].entry, key, sizeof key);
		if ((known = job_time(&times, key, 1)) != NULL) {
			jobs[k].seconds = known->seconds; /* -1 when no earlier run timed it */
			known->current = 1;
		}
	}
	qsort(jobs, (size_t)count, sizeof *jobs, by_time);
	shard_ok = (ShardOk *)calloc((size_t)count + 1, sizeof *shard_ok);
	if (!shard_ok) return 1;
	/* a job that never runs must not leave an earlier run's log behind: it would be read as this run's; and a sampled
	 * run's logs carry the seed in their names, so they would pile up */
	snprintf(command, sizeof command, "mkdir -p '%s' && rm -f '%s'/*.log", dir, dir);
	if (system(command) != 0) {
		fprintf(stderr, "kp_test: cannot create %s\n", dir);
		return 1;
	}
	snprintf(path, sizeof path, "%s/jobs", dir);
	if (!(list = fopen(path, "w"))) {
		fprintf(stderr, "kp_test: cannot write %s\n", path);
		return 1;
	}
	for (k = 0; k < count; k++) fprintf(list, "%s\n", jobs[k].entry);
	fclose(list);
	if (exec_env && exec_env[0]) {
		snprintf(exec, sizeof exec, "%s", exec_env);
	} else {
		quote(self, exec, sizeof exec);
	}
	/* each job: the program with the job's entry ($0), its output in its log, then its exit status: a sanitizer reports
	 * at exit, after the summary line (LeakSanitizer), and ends the process with a status of its own */
	snprintf(job, sizeof job,
		"L=\"%s/$(printf %%s \"$0\" | tr \":@/\" ___).log\"; %s " TIMED_FLAG " \"$0\" > \"$L\" 2>&1; s=$?; echo >> \"$L\"; echo \"%s$s\" >> \"$L\"",
		dir, exec, EXIT_STATUS_LINE);
	quote(job, quoted, sizeof quoted);
	snprintf(command, sizeof command, "xargs -P %d -n 1 sh -c %s < '%s/jobs'", jobs_wanted, quoted, dir);
	if (system(command) == -1) {
		fprintf(stderr, "kp_test: cannot run the jobs\n");
		return 1;
	}
	for (k = 0; k < count; k++) {
		size_t size;
		char *text, *line, summary[200] = "";
		int p = -1, q = -1, status = 0;
		double elapsed = -1;
		log_of(dir, jobs[k].entry, path, sizeof path);
		text = (char *)kp_read_file(path, &size);
		for (line = text; line && *line;) {
			char *end = strchr(line, '\n');
			if (end) *end = '\0';
			if (strncmp(line, "FAIL ", 5) == 0) {
				printf("%s%s%s%s\n", line, strchr(jobs[k].entry, '@') ? " [" : "", strchr(jobs[k].entry, '@') ? jobs[k].entry : "",
					strchr(jobs[k].entry, '@') ? "]" : "");
			}
			if (strncmp(line, EXIT_STATUS_LINE, sizeof EXIT_STATUS_LINE - 1) == 0) status = atoi(line + sizeof EXIT_STATUS_LINE - 1);
			if (strncmp(line, ELAPSED_LINE, sizeof ELAPSED_LINE - 1) == 0) elapsed = atof(line + sizeof ELAPSED_LINE - 1);
			if (!summary[0] && strncmp(line, "SUMMARY: ", 9) == 0) snprintf(summary, sizeof summary, "%s", line); /* a sanitizer's */
			{
				const char *at = strstr(line, ": ");
				int a, b;
				while (at) { /* the summary: the last ": N passed, M failed" of the log */
					if (sscanf(at, ": %d passed, %d failed", &a, &b) == 2) {
						p = a;
						q = b;
					}
					at = strstr(at + 2, ": ");
				}
			}
			if (!end) break;
			line = end + 1;
		}
		free(text);
		if (elapsed >= 0) {
			JobTime *slot;
			time_key(jobs[k].entry, key, sizeof key);
			if ((slot = job_time(&times, key, 0)) != NULL) slot->seconds = elapsed;
		}
		if (p < 0) {
			printf("FAIL %s: no summary (see %s)\n", jobs[k].entry, path);
			p = 0;
			q = 1;
		}
		if (status != 0 && q == 0) { /* every test passed and the process still failed: a sanitizer's report at exit */
			printf("FAIL %s: exit status %d after every test passed%s%s (see %s)\n", jobs[k].entry, status, summary[0] ? ": " : "",
				summary, path);
			q = 1;
		}
		if (jobs[k].test[0]) {
			int i;
			for (i = 0; i < shard_count && strcmp(shard_ok[i].test, jobs[k].test) != 0; i++) {
			}
			if (i == shard_count) {
				snprintf(shard_ok[shard_count].test, sizeof shard_ok[0].test, "%s", jobs[k].test);
				shard_ok[shard_count++].ok = 1;
			}
			if (q != 0) shard_ok[i].ok = 0;
		} else {
			passed += p;
			failed += q;
		}
	}
	for (k = 0; k < shard_count; k++) {
		if (shard_ok[k].ok) passed++;
		else failed++;
	}
	write_times(dir, &times);
	free(times.list);
	backend(label, sizeof label);
	if (sample > 1) snprintf(sampled, sizeof sampled, ", the long loops' 1 in %d, seed %u", sample, seed);
	printf("%s%s%s: %d passed, %d failed (%d jobs in parallel%s)\n", label, exec_env && exec_env[0] ? " via " : "",
		exec_env && exec_env[0] ? exec_env : "", passed, failed, jobs_wanted, sampled);
	free(shard_ok);
	free(jobs);
	return failed == 0 ? 0 : 1;
}

/* The time since an arbitrary start, in seconds. */
static double seconds_now(void)
{
#ifdef _WIN32
	return (double)clock() / CLOCKS_PER_SEC; /* the CRT's: wall time since the process started */
#else
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
#endif
}

int main(int argc, char **argv)
{
	int passed = 0, failed = 0, k, timed = 0;
	double start = seconds_now();
	char label[160];
	setvbuf(stdout, NULL, _IOLBF, 0);
	kp_smk_set_video(&kp_webm_smk_video); /* the KPW1 movies, as the app has them */
	if (argc > 1 && strcmp(argv[1], "--list") == 0) {
		Job *jobs = (Job *)calloc(MAX_JOBS, sizeof *jobs);
		int count = jobs ? list_jobs(jobs, MAX_JOBS, 0, 0) : 0;
		for (k = 0; k < count; k++) printf("%s\n", jobs[k].entry);
		free(jobs);
		return 0;
	}
	if (argc > 1 && strcmp(argv[1], "-j") == 0) {
		int n = argc > 2 ? atoi(argv[2]) : 0;
		if (n < 1 || argc > 3) { /* no suites here: every one runs */
			fprintf(stderr, "usage: kp_test -j N\n");
			return 2;
		}
		return run_parallel(argv[0], n);
	}
	if (argc > 1 && strcmp(argv[1], TIMED_FLAG) == 0) { /* a parallel run's job */
		timed = 1;
		argv++;
		argc--;
	}
	if (argc > 1) {
		for (k = 1; k < argc; k++) {
			if (run_entry(argv[k], &passed, &failed) != 0) failed++;
		}
	} else {
		for (k = 0; k < SUITE_COUNT; k++) run_entry(SUITES[k]->name, &passed, &failed);
	}
	if (timed) printf(ELAPSED_LINE "%.3f\n", seconds_now() - start);
	backend(label, sizeof label);
	printf("%s: %d passed, %d failed\n", label, passed, failed);
	return failed == 0 ? 0 : 1;
}

/* --- the converter's fixtures --- */

#define FIXTURE_PATH 512

/* the fixtures' MPAK: one mission, "campaign\fixture", with the packs "effects" and "terran" */
static const uint8_t FIXTURE_MPAK[] = { 1, 0, 16, 'c', 'a', 'm', 'p', 'a', 'i', 'g', 'n', '\\', 'f', 'i', 'x', 't', 'u', 'r',
	'e', 2, 7, 'e', 'f', 'f', 'e', 'c', 't', 's', 6, 't', 'e', 'r', 'r', 'a', 'n' };
static const uint8_t FIXTURE_SLOT[] = { 2, 0, 2, 12, 14 }; /* kpd.slots_section({0: 2, 12: 14}) */

typedef struct {
	KpDefsProfile profile;
	KpDefsTable *tables;
	uint8_t *chkl;
} FixtureProfile;

static void free_fixture_profile(void *p)
{
	FixtureProfile *fp = (FixtureProfile *)p;
	free(fp->tables);
	free(fp->chkl);
	free(fp);
}

const KpDefsProfile *kp_t_fixture_profile(KpT *T)
{
	const KpDefsProfile *real = kp_defs_profile("bw-1.17.0.1");
	FixtureProfile *fp = (FixtureProfile *)calloc(1, sizeof *fp);
	int k;
	kp_t_ok(T, real != NULL && fp != NULL, "the compiled profile");
	kp_t_defer(T, free_fixture_profile, fp);
	fp->tables = (KpDefsTable *)malloc((size_t)real->table_count * sizeof *fp->tables);
	kp_t_ok(T, fp->tables != NULL, "out of memory");
	memcpy(fp->tables, real->tables, (size_t)real->table_count * sizeof *fp->tables);
	for (k = 0; k < real->table_count; k++) {
		KpDefsTable *t = &fp->tables[k];
		if (strcmp(t->fourcc, "CHKL") == 0) {
			kp_t_ok(T, t->size > KP_SHA256_SIZE && (fp->chkl = (uint8_t *)malloc(t->size)) != NULL, "the CHKL table");
			memcpy(fp->chkl, t->data, t->size);
			kp_sha256("vcod", 4, fp->chkl + t->size - KP_SHA256_SIZE);
			t->data = fp->chkl;
		} else if (strcmp(t->fourcc, "MPAK") == 0) {
			t->data = FIXTURE_MPAK;
			t->size = (uint32_t)sizeof FIXTURE_MPAK;
		} else if (strcmp(t->fourcc, "SLOT") == 0) {
			t->data = FIXTURE_SLOT;
			t->size = (uint32_t)sizeof FIXTURE_SLOT;
		}
	}
	kp_t_ok(T, fp->chkl != NULL, "the profile's CHKL");
	fp->profile = *real;
	fp->profile.id = "fixture";
	fp->profile.tables = fp->tables;
	return &fp->profile;
}

static uint8_t *fixture_read(void *ctx, const char *name, size_t *size, char *err, size_t err_size)
{
	char path[FIXTURE_PATH];
	size_t n = strlen(KP_T_FIXTURE_FILES), i;
	(void)ctx;
	(void)err_size;
	err[0] = '\0';
	*size = 0;
	if (n + strlen(name) >= sizeof path) return NULL;
	memcpy(path, KP_T_FIXTURE_FILES, n);
	for (i = 0; name[i]; i++) {
		char c = name[i] == '\\' ? '/' : name[i];
		path[n + i] = c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c;
	}
	path[n + i] = '\0';
	return kp_read_file(path, size);
}

KpFiles kp_t_fixture_files(KpT *T)
{
	KpFiles f;
	memset(&f, 0, sizeof f);
	f.read = fixture_read;
	f.profile = kp_t_fixture_profile(T);
	return f;
}
