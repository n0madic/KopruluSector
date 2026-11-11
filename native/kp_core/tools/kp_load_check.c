/*
 * Parses every Koprulu container given on the command line (the converter's HD files, console.kpd, the cinematics'
 * packs) with kp_kpf_parse, the checksums verified, for the sanitizer builds (CMakeLists.txt with KP_SANITIZE=ON,
 * tools/ci.sh). Prints one line per file; exits 1 at the first failure. The SD data has no containers (ADR 0018): its
 * readers run over the original files in the C suites.
 */
#include <stdio.h>
#include <stdlib.h>

#include "../src/core/kp_kpf.h"

static uint8_t *read_file(const char *path, size_t *size)
{
	FILE *f = fopen(path, "rb");
	uint8_t *data;
	long n;
	if (!f) return NULL;
	fseek(f, 0, SEEK_END);
	n = ftell(f);
	fseek(f, 0, SEEK_SET);
	data = (uint8_t *)malloc(n > 0 ? (size_t)n : 1);
	*size = data && fread(data, 1, (size_t)n, f) == (size_t)n ? (size_t)n : 0;
	fclose(f);
	return data;
}

int main(int argc, char **argv)
{
	int i;
	for (i = 1; i < argc; i++) {
		size_t size = 0;
		uint8_t *data = read_file(argv[i], &size);
		KpError err;
		KpKpf c;
		if (!data || size == 0) {
			fprintf(stderr, "kp_load_check: cannot read %s\n", argv[i]);
			free(data);
			return 1;
		}
		if (kp_kpf_parse(&c, data, size, argv[i], 1, &err) != 0) {
			fprintf(stderr, "kp_load_check: %s\n", err.msg);
			free(data);
			return 1;
		}
		printf("%s: %d sections\n", argv[i], c.section_count);
		kp_kpf_free(&c);
		free(data);
	}
	return 0;
}
