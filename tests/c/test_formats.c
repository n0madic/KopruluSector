/* The readers of the original formats the data is built from (ADR 0018, stage D3) over synthetic files: a GRP's frame
 * as a one-frame GRP (kp_grp.h), a WAV's header from its first bytes (kp_wav.h), a TBL's text to UTF-8 (kp_text.h), the
 * files a dialog BIN names (kp_ui_bin_files), the megatile flags of a VF4 (kp_terrain_megatile_flags). */
#include "kp_test.h"

#include <stdlib.h>
#include <string.h>

#include "kp_grp.h"
#include "kp_text.h"
#include "kp_ui_data.h"
#include "kp_wav.h"
#include "../../native/kp_core/src/core/kp_terrain.h"

static void le16(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
}

static void le32(uint8_t *p, uint32_t v)
{
	le16(p, v);
	le16(p + 2, v >> 16);
}

/* Two frames of 4x2 on a 16x8 canvas; frame 1's rows share one packet run (row 1 points at row 0's bytes). */
static void grp_single_frame_copies_the_rows(KpT *T)
{
	uint8_t grp[64], *one;
	size_t size = 0;
	memset(grp, 0, sizeof grp);
	le16(grp, 2);
	le16(grp + 2, 16);
	le16(grp + 4, 8);
	/* frame 0 at 22: rows at +4 and +7 */
	grp[6] = 1, grp[7] = 2, grp[8] = 4, grp[9] = 2, le32(grp + 10, 22);
	/* frame 1 at 0x8000002e (the high bit masked): both rows at +4 */
	grp[14] = 3, grp[15] = 4, grp[16] = 4, grp[17] = 2, le32(grp + 18, 0x80000000u | 46);
	le16(grp + 22, 4), le16(grp + 24, 7);
	grp[26] = 0x84;                          /* row 0: skip 4 */
	grp[27] = 0, grp[28] = 0;                /* padding */
	grp[29] = 0x42, grp[30] = 9, grp[31] = 0x82; /* row 1: 2 of index 9, skip 2 */
	le16(grp + 46, 4), le16(grp + 48, 4);
	grp[50] = 2, grp[51] = 5, grp[52] = 6, grp[53] = 0x82; /* 2 literal, skip 2 */
	one = kp_grp_single_frame(grp, 54, 1, &size);
	kp_t_ok(T, one != NULL, "frame 1");
	kp_t_defer(T, free, one);
	kp_t_eq(T, (int64_t)size, 14 + 4 + 2 * 4, "the size");
	{
		static const uint8_t want[] = { 1, 0, 16, 0, 8, 0, 3, 4, 4, 2, 14, 0, 0, 0, 4, 0, 8, 0, 2, 5, 6, 0x82, 2, 5, 6,
			0x82 };
		kp_t_ok(T, memcmp(one, want, sizeof want) == 0, "the bytes");
	}
	kp_t_ok(T, kp_grp_single_frame(grp, 54, 2, &size) == NULL, "a frame past the count");
	kp_t_ok(T, kp_grp_single_frame(grp, 52, 1, &size) == NULL, "a row past the end");
}

/* RIFF WAVE with a LIST chunk between fmt and data, no even-size padding. */
static size_t wav(uint8_t *w, uint32_t data_size)
{
	memcpy(w, "RIFF\0\0\0\0WAVEfmt ", 16);
	le32(w + 16, 16);
	le16(w + 20, 1), le16(w + 22, 2), le32(w + 24, 22050), le32(w + 28, 88200), le16(w + 32, 4), le16(w + 34, 16);
	memcpy(w + 36, "LIST", 4), le32(w + 40, 3), memcpy(w + 44, "abc", 3);
	memcpy(w + 47, "data", 4), le32(w + 51, data_size);
	memset(w + 55, 0x11, 8);
	return 63;
}

static void wav_header_reads_from_the_first_bytes(KpT *T)
{
	uint8_t w[64];
	KpWavInfo info;
	size_t n = wav(w, 8);
	kp_t_eq(T, kp_wav_info(w, n, n, &info), 0, "the whole file");
	kp_t_ok(T, info.format_tag == 1 && info.channels == 2 && info.sample_rate == 22050 && info.avg_bytes_per_sec == 88200
		&& info.block_align == 4 && info.bits_per_sample == 16 && info.data_offset == 55 && info.data_length == 8,
		"the fields");
	kp_t_eq(T, kp_wav_info(w, 55, 1000000, &info), 0, "the header alone, a long file");
	kp_t_eq(T, kp_wav_info(w, 40, n, &info), -2, "the data chunk past the bytes read");
	kp_t_eq(T, kp_wav_info(w, n, 54, &info), -1, "a data chunk past the file");
	w[0] = 'X';
	kp_t_eq(T, kp_wav_info(w, n, n, &info), -1, "not RIFF");
}

/* windows-1252 text grows; offsets before, inside and past the text move as kpd.table_utf8 moves them. */
static void tbl_text_moves_to_utf8(KpT *T)
{
	static const uint8_t tbl[] = { 3, 0, 8, 0, 11, 0, 40, 0, 'a', 0xe9, 0, 'b', 0x80, 0 };
	static const uint8_t want[] = { 3, 0, 8, 0, 12, 0, 43, 0, 'a', 0xc3, 0xa9, 0, 'b', 0xe2, 0x82, 0xac, 0 };
	size_t n = 0;
	uint8_t *u = kp_text_table_utf8(tbl, sizeof tbl, &n);
	kp_t_ok(T, u != NULL, "converted");
	kp_t_defer(T, free, u);
	kp_t_eq(T, (int64_t)n, (int64_t)sizeof want, "the size");
	kp_t_ok(T, memcmp(u, want, sizeof want) == 0, "the bytes");
}

static int collect(void *ctx, const uint8_t *path, size_t n)
{
	char *out = (char *)ctx;
	strncat(out, (const char *)path, n);
	strcat(out, "|");
	return 0;
}

/* A container with an image (hotkey: its first letter lowercased) and a button with two SMK nodes. */
static void bin_names_its_images_and_smks(KpT *T)
{
	uint8_t bin[512];
	char got[256] = "";
	char err[128];
	memset(bin, 0, sizeof bin);
	le32(bin + 34, 0), le32(bin + 66, 86);                  /* the container: child at 86 */
	le32(bin + 86, 172), le32(bin + 86 + 34, 5);            /* an image, next 172 */
	le32(bin + 86 + 20, 400), le32(bin + 86 + 24, 0x200);
	le32(bin + 172 + 34, 14), le32(bin + 172 + 66, 300);    /* a button, its nodes at 300 and 340 */
	le32(bin + 300, 340), le32(bin + 300 + 10, 430);
	le32(bin + 340 + 10, 450);
	memcpy(bin + 400, "Glue\\x.pcx", 11);
	memcpy(bin + 430, "portrait\\a.smk", 15);
	memcpy(bin + 450, "portrait\\b.smk", 15);
	kp_t_eq(T, kp_ui_bin_files(bin, sizeof bin, collect, got, err, sizeof err), 0, "%s", err);
	kp_t_eq_str(T, got, "glue\\x.pcx|portrait\\a.smk|portrait\\b.smk|", "the files");
	le32(bin + 172, 86);
	kp_t_ok(T, kp_ui_bin_files(bin, sizeof bin, collect, got, err, sizeof err) != 0 && strstr(err, "repeats") != NULL,
		"a record that repeats");
}

static void vf4_counts_make_the_flags(KpT *T)
{
	uint8_t vf4[3 * 32];
	uint32_t flags[3];
	int k;
	memset(vf4, 0, sizeof vf4);
	for (k = 0; k < 16; k++) le16(vf4 + 2 * k, 0x0001 | (k < 12 ? 0x0004 : 0));            /* walkable, high */
	for (k = 0; k < 13; k++) le16(vf4 + 32 + 2 * k, 0x0001 | (k < 6 ? 0x0002 : 0x0004)); /* 13 walk, middle+high 13 */
	le16(vf4 + 64, 0x0008);                                                             /* none walkable, very high */
	kp_terrain_megatile_flags(vf4, sizeof vf4, flags);
	kp_t_eq(T, flags[0], 0x00010000 | 0x04000000, "walkable and high");
	kp_t_eq(T, flags[1], 0x00010000 | 0x20000000 | 0x02000000, "partly walkable, middle");
	kp_t_eq(T, flags[2], 0x00040000 | 0x01000000, "unwalkable, very high");
}

static const KpTest TESTS[] = {
	{ "grp_single_frame_copies_the_rows", grp_single_frame_copies_the_rows, 0 },
	{ "wav_header_reads_from_the_first_bytes", wav_header_reads_from_the_first_bytes, 0 },
	{ "tbl_text_moves_to_utf8", tbl_text_moves_to_utf8, 0 },
	{ "bin_names_its_images_and_smks", bin_names_its_images_and_smks, 0 },
	{ "vf4_counts_make_the_flags", vf4_counts_make_the_flags, 0 },
};
KP_SUITE(kp_suite_formats, "test_formats", TESTS);
