/*
 * Loads every Koprulu data file given on the command line with the kp_core loaders and walks
 * what they return, for the sanitizer builds (tools/native/build.sh SANITIZE=1): dat.kpd,
 * grpmeta.kpd and iscript.kpd through their loaders, .kpm through kp_mission_load, everything
 * else through kp_kpf_parse with the checksums verified. Prints one line per file; exits 1 at the first failure.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/core/kp_dat.h"
#include "../src/core/kp_grpmeta.h"
#include "../src/core/kp_iscript.h"
#include "../src/core/kp_kpf.h"
#include "../src/core/kp_mission.h"

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

static int ends_with(const char *s, const char *tail)
{
	size_t a = strlen(s), b = strlen(tail);
	return a >= b && strcmp(s + a - b, tail) == 0;
}

int main(int argc, char **argv)
{
	int i;
	for (i = 1; i < argc; i++) {
		size_t size = 0;
		uint8_t *data = read_file(argv[i], &size);
		KpError err;
		uint64_t sum = 0;
		if (!data || size == 0) {
			fprintf(stderr, "kp_load_check: cannot read %s\n", argv[i]);
			return 1;
		}
		if (ends_with(argv[i], "/dat.kpd")) {
			KpDat dat;
			int k;
			if (kp_dat_load(&dat, data, size, argv[i], NULL, &err) != 0) goto failed;
			for (k = 0; k < dat.column_count; k++) {
				const KpDatColumn *c = &dat.columns[k];
				uint32_t base = (uint32_t)c->first_id * c->per_id, j;
				for (j = 0; j < c->count; j++) sum += kp_dat_value(c, base + j);
			}
			printf("%s: %d columns, sum %llu\n", argv[i], dat.column_count, (unsigned long long)sum);
			kp_dat_free(&dat);
		} else if (ends_with(argv[i], "/grpmeta.kpd")) {
			KpGrpMeta g;
			uint32_t e, f;
			int32_t p;
			if (kp_grpmeta_load(&g, data, size, argv[i], NULL, &err) != 0) goto failed;
			for (e = 1; e <= g.grp_count; e++) {
				for (f = 0; f < g.grps[e].frames; f++) {
					int x, y, w, h;
					kp_grpmeta_frame(&g, e, f, &x, &y, &w, &h);
					sum += (uint64_t)(x + y + w + h);
				}
			}
			for (e = 1; e <= g.lo_count; e++) {
				for (f = 0; f < g.los[e].frames; f++) {
					for (p = 0; (uint32_t)p < g.los[e].points; p++) {
						int x, y;
						if (kp_grpmeta_lo_point(&g.los[e], f, p, &x, &y)) sum += (uint64_t)(x + y + 256);
					}
				}
			}
			printf("%s: %u GRPs, %u LOs, sum %llu\n", argv[i], (unsigned)g.grp_count, (unsigned)g.lo_count,
				(unsigned long long)sum);
			kp_grpmeta_free(&g);
		} else if (ends_with(argv[i], "/iscript.kpd")) {
			KpIScript s;
			uint32_t j;
			if (kp_iscript_load(&s, data, size, argv[i], NULL, &err) != 0) goto failed;
			for (j = 0; j < s.id_count; j++) sum += (uint64_t)(s.headers[j] + 1);
			printf("%s: %u ids, %u code bytes, sum %llu\n", argv[i], (unsigned)s.id_count, (unsigned)s.code_size,
				(unsigned long long)sum);
			kp_iscript_free(&s);
		} else if (ends_with(argv[i], ".kpm")) {
			KpMission m;
			uint32_t j;
			if (kp_mission_load(&m, data, size, argv[i], NULL, &err) != 0) goto failed;
			for (j = 0; j < m.tiles.size; j++) sum += m.tiles.data[j];
			for (j = 0; j < m.walk.size; j++) sum += m.walk.data[j];
			printf("%s: %ux%u, %d rules, %d sounds, %u regions, sum %llu\n", argv[i], (unsigned)m.width,
				(unsigned)m.height, m.rule_count, m.sound_count, (unsigned)m.regions.count, (unsigned long long)sum);
			kp_mission_free(&m);
		} else {
			KpKpf c;
			if (kp_kpf_parse(&c, data, size, argv[i], 1, &err) != 0) goto failed;
			printf("%s: %d sections\n", argv[i], c.section_count);
			kp_kpf_free(&c);
		}
		free(data);
		continue;
	failed:
		fprintf(stderr, "kp_load_check: %s\n", err.msg);
		free(data);
		return 1;
	}
	return 0;
}
