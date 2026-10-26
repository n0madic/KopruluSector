/*
 * kp_test: the C suites' runner (plan phase S, stage S1). A suite is a table of tests (KpSuite); a test gets its
 * context T and fails through the KP_T_* checks, a failed check ending the test at once, as does a failure of a game
 * the test started (kp_t_game: the game's failure handler is the test's). Whatever a test registers (kp_t_read,
 * kp_t_golden, kp_t_game, kp_t_defer) is released when it ends, passed or failed.
 *
 *   kp_test                         every suite
 *   kp_test SUITE[:TEST[@I/N]]      one suite, one test, or shard I of N of a test
 *   kp_test -j N                    the suites in N parallel processes, the sharded tests split into their shards
 *   kp_test --list                  the jobs of a parallel run, one per line
 * Run from the repository root (the generated data and the goldens are read from there). A parallel run starts each
 * job with KP_TEST_EXEC (default: this program; "node build/cmake-wasm/kp_test.js" runs the jobs under node) and
 * writes their logs to KP_TEST_LOGS (default build/c-tests).
 */
#ifndef KP_TEST_H
#define KP_TEST_H

#include <setjmp.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kp_oracle.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct KpT KpT;
typedef void (*KpTestFn)(KpT *T);

typedef struct {
	const char *name;
	KpTestFn fn;
	int shards; /* the shards of a parallel run (the test's long loops use kp_t_mine); 0 or 1: one job */
} KpTest;

typedef struct {
	const char *name;
	const KpTest *tests;
	int count;
} KpSuite;

#define KP_T_CLEANUPS 256
#define KP_T_MESSAGE 8192

struct KpT {
	const char *suite, *test;
	int shard, shards;
	KpTry catch_; /* where a failed check or a failed game ends the test */
	char message[KP_T_MESSAGE];
	struct {
		void (*fn)(void *);
		void *p;
	} cleanups[KP_T_CLEANUPS];
	int cleanup_count;
	KpGame *guarded[KP_T_CLEANUPS]; /* the games whose failure handler is the test's (their error is its message) */
	int guarded_count;
};

/* Whether item k (1-based) of a sharded test's loop is this run's: shard `shard` of `shards` takes every shards-th
 * item (all of them unsharded). */
int kp_t_mine(const KpT *T, int k);

#if defined(__GNUC__) || defined(__clang__)
#define KP_T_NORETURN __attribute__((noreturn))
#define KP_T_PRINTF(a, b) __attribute__((format(printf, a, b)))
#else
#define KP_T_NORETURN
#define KP_T_PRINTF(a, b)
#endif

/* Ends the test as failed with the message. */
KP_T_NORETURN KP_T_PRINTF(2, 3) void kp_t_fail(KpT *T, const char *fmt, ...);
/* T.ok, T.eq: the check fails with "msg: expected E, got A". */
KP_T_PRINTF(3, 4) void kp_t_ok(KpT *T, int cond, const char *fmt, ...);
KP_T_PRINTF(4, 5) void kp_t_eq(KpT *T, int64_t actual, int64_t expected, const char *fmt, ...);
KP_T_PRINTF(4, 5) void kp_t_eq_str(KpT *T, const char *actual, const char *expected, const char *fmt, ...);
/* env.log: a line of the test's output (SKIP lines). */
KP_T_PRINTF(2, 3) void kp_t_log(KpT *T, const char *fmt, ...);

/* Registers fn(p) to run when the test ends. */
void kp_t_defer(KpT *T, void (*fn)(void *), void *p);
/* A file's bytes (relative to the repository root; NUL terminated past size), released at the end; fails without it. */
uint8_t *kp_t_read(KpT *T, const char *path, size_t *size);
/* Whether a file is readable (optional inputs such as generated/ data). */
int kp_t_has(const KpT *T, const char *path);
/* A golden's JSON (kp_golden_load), released at the end; fails without it. */
const KpJson *kp_t_golden(KpT *T, const char *path);
/* A native game of the mission at path (relative to the root), released at the end; fails when it does not start.
 * Its failure handler is the test's, so a C failure fails the test (KpTry locals inside the test catch theirs). */
KpOracleGame *kp_t_game(KpT *T, const char *path, uint32_t seed, int expansion, const KpOracleOpts *opts);
/* A game released now (before the test ends). */
void kp_t_game_free(KpT *T, KpOracleGame *og);
/* A probe's game of map (events on, no campaign row, the mission named "mission"), as kp_t_game. */
KpOracleGame *kp_t_probe_game(KpT *T, const char *map, uint32_t seed, int expansion, int no_units);
/* Golden g's run (kp_golden_run_start; trace: the traces collected), released at the end, its game's failure handler
 * the test's; fails when it does not start. */
KpGoldenRun *kp_t_golden_run(KpT *T, const KpJson *g, const KpOracleOpts *opts, int trace);
/* A malloc'd golden run released (kp_golden_run_free and its memory). */
void kp_t_golden_run_release(KpGoldenRun *gr);
/* An empty replay, released at the end. */
KpReplay *kp_t_replay(KpT *T);
/* Whether the files exist; logs "SKIP label: no <file>" for the first missing one. files ends with
 * NULL. */
int kp_t_ready(KpT *T, const char *label, const char *const *files);
/* The failure handler of a game the test starts itself (kp_t_game does it). */
void kp_t_guard(KpT *T, KpGame *g);
/* A string of the test's arena (released at the end). */
char *kp_t_strdup(KpT *T, const char *s);
/* A protected call: runs fn(T, ud); 0 when it returns, -1 when a check or a game of the test failed inside it (its
 * message in msg), the test going on. What fn registered stays registered. */
int kp_t_pcall(KpT *T, void (*fn)(KpT *T, void *ud), void *ud, char *msg, size_t size);
/* A buffer released at the end. */
KpBuf *kp_t_buf(KpT *T);
/* The resources registered so far, and the release of those registered after a mark (a loop's games and goldens). */
int kp_t_mark(const KpT *T);
void kp_t_release(KpT *T, int mark);

/* --- the games' fields by name (kp_test_game.c: the unit arrays, the game's DAT, the scalar fields) --- */

/* The unit array g->u.<field> (index 1..KP_UNITS), failing for an unknown field. */
int64_t *kp_t_unit_field(KpT *T, KpGame *g, const char *field);
/* A value of the game's own DAT (table's column at flat index i), failing for an unknown column; KP_NIL
 * where the column has no value. */
int64_t kp_t_dat(KpT *T, KpOracleGame *og, const char *table, const char *column, int64_t i);
/* Writes it (the probes patch the loaded DAT), failing outside the column or its element's range. */
void kp_t_dat_set(KpT *T, KpOracleGame *og, const char *table, const char *column, int64_t i, int64_t v);
/* A scalar field of the game by name (KP_SCALARS), failing for an unknown name. */
KpScalar kp_t_scalar(KpT *T, const char *name);
/* The units of a list (g.lists.<name>), in order, into out (at most max); returns the count. */
int kp_t_list_units(KpGame *g, int list, int64_t *out, int max);

/* --- the suites (tests/c/test_*.c) --- */
extern const KpSuite kp_suite_rng, kp_suite_initial_state, kp_suite_scenarios, kp_suite_save, kp_suite_references,
	kp_suite_orders_native, kp_suite_requirements, kp_suite_aiscript, kp_suite_damage_table, kp_suite_kp_core,
	kp_suite_kp_ui, kp_suite_present, kp_suite_session, kp_suite_hud, kp_suite_data, kp_suite_ui_data, kp_suite_images,
	kp_suite_fog, kp_suite_campaign;

#define KP_SUITE(var, label, tests) const KpSuite var = { label, tests, (int)(sizeof tests / sizeof tests[0]) }

#ifdef __cplusplus
}
#endif

#endif
