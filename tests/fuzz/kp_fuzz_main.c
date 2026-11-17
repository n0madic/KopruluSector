/* libFuzzer's driver of one target (KP_FUZZ_TARGET, the CMake option KP_FUZZ: fuzz_rep, fuzz_chk, fuzz_save,
 * fuzz_commands; phase T, stage T2, ADR 0022). The entries are tests/c/kp_fuzz.c's, the ones test_fuzz runs the kept
 * inputs through. Run from the source tree's root (the data's targets read kp_oracle_data(".")); tools/fuzz.sh runs
 * them all. A property the entry checks that fails aborts, a finding as a crash is. */
#include <stdio.h>
#include <stdlib.h>

#include "kp_fuzz.h"

#ifndef KP_FUZZ_TARGET
#error "KP_FUZZ_TARGET names the target (KP_FUZZ_REP, KP_FUZZ_CHK, KP_FUZZ_SAVE, KP_FUZZ_COMMANDS, KP_FUZZ_TURNS, KP_FUZZ_UDP or KP_FUZZ_SNET)"
#endif

#define WHY_SIZE 512

int LLVMFuzzerInitialize(int *argc, char ***argv);
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

/* The run's counts at the process's end: the inputs, the games the game's own checks stopped, the saves loaded. */
static void report(void)
{
	const KpFuzzStats *st = kp_fuzz_stats();
	int r;
	for (r = 0; r < st->reasons; r++) fprintf(stderr, "  refused %ld: %s\n", st->reason_count[r], st->reason[r]);
	if (st->reason_others) fprintf(stderr, "  refused %ld: other reasons\n", st->reason_others);
	fprintf(stderr, "fuzz_%s: %ld inputs, %ld games refused, %ld saves loaded\n", kp_fuzz_name(KP_FUZZ_TARGET),
		st->inputs, st->refused, st->loaded);
	kp_fuzz_free();
}

int LLVMFuzzerInitialize(int *argc, char ***argv)
{
	char why[WHY_SIZE];
	(void)argc;
	(void)argv;
	if (kp_fuzz_init(KP_FUZZ_TARGET, why, sizeof why) != 0) {
		fprintf(stderr, "fuzz_%s: %s\n", kp_fuzz_name(KP_FUZZ_TARGET), why);
		exit(1);
	}
	atexit(report);
	return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
	char why[WHY_SIZE];
	if (kp_fuzz_run(KP_FUZZ_TARGET, data, size, why, sizeof why) != 0) {
		fprintf(stderr, "fuzz_%s: %s\n", kp_fuzz_name(KP_FUZZ_TARGET), why);
		abort();
	}
	return 0;
}
