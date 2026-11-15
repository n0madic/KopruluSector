/* The fuzz targets' seeds (phase T, stage T2, ADR 0022), run from the source tree's root:
 *
 *   kp_fuzz_seeds --synthetic tests/fuzz/seeds   the made-up ones, committed (no Blizzard bytes)
 *   kp_fuzz_seeds build/fuzz/seeds               and, with the data, the ones made from it (never committed)
 *
 * Each target's under DIR/<target>/ (kp_fuzz_write_seeds). */
#include <stdio.h>
#include <string.h>

#include "kp_fuzz.h"

int main(int argc, char **argv)
{
	char why[512];
	int with_data = 1, count;
	const char *dir;
	if (argc == 3 && strcmp(argv[1], "--synthetic") == 0) with_data = 0;
	if (argc != 2 + !with_data) {
		fprintf(stderr, "usage: kp_fuzz_seeds [--synthetic] DIR\n");
		return 2;
	}
	dir = argv[argc - 1];
	count = kp_fuzz_write_seeds(dir, with_data, why, sizeof why);
	kp_fuzz_free();
	if (count < 0) {
		fprintf(stderr, "kp_fuzz_seeds: %s\n", why);
		return 1;
	}
	printf("kp_fuzz_seeds: %d seeds into %s\n", count, dir);
	return 0;
}
