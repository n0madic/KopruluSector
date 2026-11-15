/* The fuzzing's kept inputs (phase T, stage T2; ADR 0022): every seed of tests/fuzz/seeds/<target>/ and every input a
 * fuzzer found of tests/fuzz/regress/<target>/ (and koprulu-private/fuzz/regress/<target>/, inputs with the install's
 * bytes, when present) run through the targets' entries (kp_fuzz.c) in every build: on the host, under ASan and UBSan
 * and in wasm. libFuzzer's targets (the KP_FUZZ build, tools/fuzz.sh) look for new ones. */
#include "kp_test_app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kp_fuzz.h"
#include "kp_session.h"

static const char *const KEPT[] = { "tests/fuzz/seeds", "tests/fuzz/regress", "koprulu-private/fuzz/regress" };

static void free_fuzz(void *p)
{
	(void)p;
	kp_fuzz_free();
}

/* Every input of dir through target: none fails a property (a crash or a sanitizer's report ends the run). Their
 * count. */
static int run_dir(KpT *T, int target, const char *dir)
{
	char why[KP_ERROR_SIZE];
	char **names;
	int k, count;
	names = kp_list_files(dir, "", &count);
	for (k = 0; k < count; k++) {
		char path[KP_T_PATH];
		size_t size;
		uint8_t *bytes;
		snprintf(path, sizeof path, "%s/%s", dir, names[k]);
		if (!(bytes = kp_read_file(path, &size))) {
			kp_list_files_free(names, count);
			kp_t_fail(T, "%s: not read", path);
		}
		if (kp_fuzz_run(target, bytes, size, why, sizeof why) != 0) {
			free(bytes);
			kp_list_files_free(names, count);
			kp_t_fail(T, "%s: %s", path, why);
		}
		free(bytes);
	}
	kp_list_files_free(names, count);
	return count;
}

static void init_target(KpT *T, int target)
{
	char why[KP_ERROR_SIZE];
	kp_t_defer(T, free_fuzz, NULL);
	if (kp_fuzz_init(target, why, sizeof why) != 0) kp_t_fail(T, "%s: %s", kp_fuzz_name(target), why);
}

/* Every kept input of target. */
static void kept_inputs(KpT *T, int target)
{
	int d, files = 0;
	if (kp_fuzz_needs_data(target)) kp_t_data(T); /* skips without it */
	init_target(T, target);
	for (d = 0; d < (int)(sizeof KEPT / sizeof KEPT[0]); d++) {
		char dir[KP_T_PATH];
		snprintf(dir, sizeof dir, "%s/%s", KEPT[d], kp_fuzz_name(target));
		files += run_dir(T, target, dir);
	}
	kp_t_log(T, "%s: %d inputs, %ld games refused, %ld saves loaded", kp_fuzz_name(target), files,
		kp_fuzz_stats()->refused, kp_fuzz_stats()->loaded);
	/* the committed seeds (kp_fuzz_seeds --synthetic tests/fuzz/seeds); a save is a game's, none made up: the save
	 * target's seeds are data_seeds_run's */
	if (target != KP_FUZZ_SAVE) kp_t_ok(T, files > 0, "%s: some kept inputs", kp_fuzz_name(target));
}

static void kept_rep_inputs(KpT *T) { kept_inputs(T, KP_FUZZ_REP); }
static void kept_chk_inputs(KpT *T) { kept_inputs(T, KP_FUZZ_CHK); }
static void kept_save_inputs(KpT *T) { kept_inputs(T, KP_FUZZ_SAVE); }
static void kept_command_inputs(KpT *T) { kept_inputs(T, KP_FUZZ_COMMANDS); }

/* The seeds made from the data (kp_fuzz_write_seeds, as tools/fuzz.sh makes build/fuzz/seeds) through their
 * targets: none fails, every save the game made loads (both forms), and each target has the seeds it should. */
static void data_seeds_run(KpT *T)
{
	/* rep: + recorded; chk: + 7 maps; save: a file and a native save a point; commands: + a player's units and an
	 * order past orders.dat a map */
	static const int AT_LEAST[KP_FUZZ_TARGETS] = { 2, 8, 2 * KP_FUZZ_SAVE_SEED_POINTS, 9 };
	const char *root;
	char why[KP_ERROR_SIZE];
	int target, written;
	kp_t_data(T);
	root = kp_t_temp_dir(T, NULL);
	init_target(T, KP_FUZZ_SAVE);
	if ((written = kp_fuzz_write_seeds(root, 1, why, sizeof why)) < 0) kp_t_fail(T, "seeds: %s", why);
	for (target = 0; target < KP_FUZZ_TARGETS; target++) {
		char dir[KP_T_PATH];
		long loaded = kp_fuzz_stats()->loaded;
		int count;
		if (kp_fuzz_init(target, why, sizeof why) != 0) kp_t_fail(T, "%s: %s", kp_fuzz_name(target), why);
		snprintf(dir, sizeof dir, "%s/%s", root, kp_fuzz_name(target));
		count = run_dir(T, target, dir);
		kp_t_log(T, "%s: %d seeds", kp_fuzz_name(target), count);
		kp_t_ok(T, count >= AT_LEAST[target], "%s: %d seeds, %d at least", kp_fuzz_name(target), count, AT_LEAST[target]);
		if (target == KP_FUZZ_SAVE) {
			kp_t_ok(T, kp_fuzz_stats()->loaded - loaded == count, "every save loads: %ld of %d",
				kp_fuzz_stats()->loaded - loaded, count);
		}
		if (target == KP_FUZZ_COMMANDS) { /* the orders past orders.dat, refused, and the games back at their start */
			const KpFuzzStats *st = kp_fuzz_stats();
			int r;
			for (r = 0; r < st->reasons; r++) kp_t_log(T, "refused %ld: %s", st->reason_count[r], st->reason[r]);
			kp_t_ok(T, st->refused == 3 && st->reasons == 1 && strstr(st->reason[0], "of tech_use") != NULL,
				"the three orders past orders.dat refused, nothing else: %ld", st->refused);
		}
	}
	kp_t_log(T, "%d seeds", written);
}

/* kp_save_seal makes a save's hash table right for its bytes: a save sealed again is the same bytes; with its table
 * cut off or zeroed, kp_load refuses it and takes it sealed. */
static void seal_makes_the_hash_table(KpT *T)
{
	KpOracleGame og;
	KpOracleOpts o;
	KpError err;
	uint8_t *save, *sealed, *cut;
	size_t size, sealed_size, table;
	int k;
	kp_t_data(T);
	kp_oracle_opts(&o);
	if (kp_oracle_game_start(&og, ".", "scenarios/marine_group", 7, 1, &o, &err) != 0) kp_t_fail(T, "%s", err.msg);
	kp_t_guard(T, og.g);
	for (k = 0; k < 10; k++) kp_run_frame(og.g, NULL, 0);
	if (kp_save(og.g, og.name, og.identity, &save, &size, &err) != 0) {
		kp_oracle_game_free(&og);
		kp_t_fail(T, "%s", err.msg);
	}
	kp_t_ok(T, kp_save_seal(og.g, save, size, &sealed, &sealed_size) == 0, "sealed");
	kp_t_ok(T, sealed_size == size && memcmp(sealed, save, size) == 0, "a save sealed again is the same bytes");
	free(sealed);
	/* the table at the end: u32 its count n, n * (tag, hash), the total */
	for (table = 0, k = 1; k <= KP_SAVE_SECTIONS_MAX && !table; k++) {
		size_t t = 4 + (size_t)k * 12 + 8;
		if (t < size && kp_le_u32(save + size - t) == (uint32_t)k) table = t;
	}
	kp_t_ok(T, table > 0, "the hash table found");
	cut = (uint8_t *)malloc(size);
	kp_t_ok(T, cut != NULL, "out of memory");
	memcpy(cut, save, size);
	memset(cut + size - table + 4, 0, table - 4);
	kp_t_ok(T, kp_load(og.g, cut, size, og.name, og.identity, &err) != 0, "a zeroed table refused");
	kp_t_ok(T, kp_save_seal(og.g, cut, size - table, &sealed, &sealed_size) == 0, "the table cut off, sealed");
	kp_t_ok(T, sealed_size == size && memcmp(sealed, save, size) == 0, "the table back");
	kp_t_ok(T, kp_load(og.g, sealed, sealed_size, og.name, og.identity, &err) == 0, "loads: %s", err.msg);
	free(sealed);
	free(cut);
	free(save);
	kp_oracle_game_free(&og);
}

static const KpTest TESTS[] = {
	{ "kept_rep_inputs", kept_rep_inputs, 0 },
	{ "kept_chk_inputs", kept_chk_inputs, 0 },
	{ "kept_save_inputs", kept_save_inputs, 0 },
	{ "kept_command_inputs", kept_command_inputs, 0 },
	{ "data_seeds_run", data_seeds_run, 0 },
	{ "seal_makes_the_hash_table", seal_makes_the_hash_table, 0 },
};

KP_SUITE(kp_suite_fuzz, "test_fuzz", TESTS);
