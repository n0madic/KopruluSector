/*
 * A glyph supplement for the converter's `locale` (ADR 0016; kp_fnt_supplement):
 *
 *     kp_fnt_make FONT.fnt FONT.ttf CODES.txt OUT
 *
 * CODES.txt holds the code points in hexadecimal, ascending, one a line. OUT receives the supplement's bytes; each code
 * point the TrueType font lacks is printed as "missing XXXX", and the converter decides what that means.
 */
#include "kp_host.h"
#include "kp_present.h"

#include <stdio.h>
#include <stdlib.h>

#define ERROR_SIZE 256

int main(int argc, char **argv)
{
	uint8_t *font, *ttf, *text, *out = NULL;
	size_t font_size, ttf_size, text_size, out_size = 0, at = 0;
	int32_t *codes, *missing, count = 0, missing_count = 0, k;
	char err[ERROR_SIZE];
	FILE *f;
	if (argc != 5) {
		fprintf(stderr, "usage: kp_fnt_make FONT.fnt FONT.ttf CODES.txt OUT\n");
		return 2;
	}
	font = kp_read_file(argv[1], &font_size);
	ttf = kp_read_file(argv[2], &ttf_size);
	text = kp_read_file(argv[3], &text_size);
	if (font == NULL || ttf == NULL || text == NULL) {
		fprintf(stderr, "kp_fnt_make: cannot read an input\n");
		return 1;
	}
	codes = (int32_t *)malloc((text_size + 1) * sizeof *codes);
	missing = (int32_t *)malloc((text_size + 1) * sizeof *missing);
	if (codes == NULL || missing == NULL) {
		fprintf(stderr, "kp_fnt_make: out of memory\n");
		return 1;
	}
	while (at < text_size) {
		char *end;
		long value = strtol((const char *)text + at, &end, 16);
		if (end == (const char *)text + at) break;
		codes[count++] = (int32_t)value;
		at = (size_t)(end - (const char *)text);
	}
	if (kp_fnt_supplement(ttf, ttf_size, font, font_size, codes, count, &out, &out_size, missing, &missing_count, err,
		sizeof err) != 0) {
		fprintf(stderr, "kp_fnt_make: %s\n", err);
		return 1;
	}
	for (k = 0; k < missing_count; k++) printf("missing %04X\n", (unsigned)missing[k]);
	if (!(f = fopen(argv[4], "wb")) || fwrite(out, 1, out_size, f) != out_size || fclose(f) != 0) {
		fprintf(stderr, "kp_fnt_make: cannot write %s\n", argv[4]);
		return 1;
	}
	free(out);
	free(codes);
	free(missing);
	free(font);
	free(ttf);
	free(text);
	return 0;
}
