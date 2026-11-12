/* The product's helpers of the C suites (kp_test_app.h). */
#include "kp_test_app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kp_data.h"
#include "kp_hd_ui_scr.h"
#include "kp_kpf.h" /* kp_le_put32 */

/* kp_res_close as a deferred cleanup (a call through another function type is undefined) */
static void res_close(void *p) { kp_res_close((KpRes *)p); }

KpRes *kp_t_install(KpT *T, const char *env)
{
	const char *dir = getenv(env), *dirs[1];
	KpResConfig c;
	char err[1024];
	KpRes *r;
	if (!dir || !dir[0]) kp_t_skip(T, "no %s", env);
	memset(&c, 0, sizeof c);
	dirs[0] = dir;
	r = kp_res_open(dirs, 1, &c, err, sizeof err);
	kp_t_ok(T, r != NULL, "%s: %s", dir, err);
	kp_t_defer(T, res_close, r);
	return r;
}

const uint8_t *kp_t_console_art(KpT *T, size_t *size)
{
	/* kept for the process's later tests (the build reads the install's DDS consoles and SD PCXs) */
	static uint8_t *art;
	static size_t art_size;
	static char why[sizeof ((KpError *)0)->msg];
	if (!art && !why[0]) {
		KpRes *r = kp_t_install(T, "KP_TEST_SCR");
		KpData d;
		KpError e;
		if (kp_data_build(&d, r, NULL, 0, NULL, 0, &e) != 0) kp_t_fail(T, "the install's data: %s", e.msg);
		art = kp_hd_ui_scr_console_art(r, &d.files, &art_size, &e);
		kp_data_free(&d);
		if (!art) snprintf(why, sizeof why, "%s", e.msg[0] ? e.msg : "the install holds no HD2 consoles");
	}
	if (!art) kp_t_skip(T, "no consoles' art: %s", why);
	if (size) *size = art_size;
	return art;
}

void kp_t_bits(KpTBits *o, uint32_t v, int n)
{
	int i;
	for (i = 0; i < n; i++, o->at++) {
		if (v >> i & 1u) o->bytes[o->at >> 3] |= (uint8_t)(1u << (o->at & 7));
	}
}

size_t kp_t_bits_bytes(const KpTBits *o) { return (o->at + 7) / 8; }

void kp_t_bits_leaf(KpTBits *o, uint8_t v)
{
	kp_t_bits(o, 0, 1);
	kp_t_bits(o, v, 8);
}

void kp_t_bits_pair(KpTBits *o, uint8_t zero, uint8_t one)
{
	kp_t_bits(o, 1, 1);
	kp_t_bits_leaf(o, zero);
	kp_t_bits_leaf(o, one);
}

#define SMK_HEADER 104       /* the SMK2 header's bytes before the frames' sizes */
#define SMK_RATE (-6666)     /* the cinematics': 66.66 ms a frame */
#define SMK_FILL_2048 (3 | 63 << 2) /* a TYPE code: a solid fill of 2048 blocks (the most a run takes) */

uint8_t *kp_t_smk(KpT *T, uint32_t width, uint32_t height, uint32_t frames, uint8_t fill, const uint32_t tracks[2],
	KpTSmkChunks chunks, void *ctx, size_t *size)
{
	size_t cap = SMK_HEADER + 5 * (size_t)frames + sizeof(KpTBits) + (size_t)frames * (KP_T_SMK_CHUNKS + 8), at, tree_bytes;
	uint8_t *m = (uint8_t *)calloc(1, cap);
	KpTBits trees;
	uint32_t f;
	if (!m) kp_t_fail(T, "out of memory");
	kp_t_defer(T, free, m);
	kp_t_ok(T, width % 4 == 0 && height % 4 == 0 && (width / 4) * (height / 4) <= 2048, "%ux%u: whole blocks, one run",
		width, height);
	memset(&trees, 0, sizeof trees);
	kp_t_bits(&trees, 0, 1); /* MMAP, MCLR, FULL absent */
	kp_t_bits(&trees, 0, 1);
	kp_t_bits(&trees, 0, 1);
	kp_t_bits(&trees, 1, 1); /* TYPE: */
	kp_t_bits(&trees, 1, 1); kp_t_bits_leaf(&trees, SMK_FILL_2048 & 0xff); kp_t_bits(&trees, 0, 1); /* low: the fill */
	kp_t_bits(&trees, 1, 1); kp_t_bits_leaf(&trees, fill); kp_t_bits(&trees, 0, 1);                 /* high: its colour */
	kp_t_bits(&trees, 0x1111, 16);
	kp_t_bits(&trees, 0x2222, 16);
	kp_t_bits(&trees, 0x3333, 16);
	kp_t_bits(&trees, 0, 1); /* the big tree: one leaf */
	kp_t_bits(&trees, 0, 1);
	tree_bytes = kp_t_bits_bytes(&trees);
	memcpy(m, "SMK2", 4);
	kp_le_put32(m + 4, width);
	kp_le_put32(m + 8, height);
	kp_le_put32(m + 12, frames);
	kp_le_put32(m + 16, (uint32_t)SMK_RATE);
	kp_le_put32(m + 52, (uint32_t)tree_bytes);
	kp_le_put32(m + 68, 64);
	kp_le_put32(m + 72, tracks ? tracks[0] : 0);
	kp_le_put32(m + 76, tracks ? tracks[1] : 0);
	at = SMK_HEADER + 4 * (size_t)frames + frames + tree_bytes;
	memcpy(m + SMK_HEADER + 5 * (size_t)frames, trees.bytes, tree_bytes);
	for (f = 0; f < frames; f++) {
		size_t start = at;
		uint8_t type = 0;
		if (chunks) {
			size_t n = chunks(ctx, (int)f, &type, m + at);
			kp_t_ok(T, n <= KP_T_SMK_CHUNKS, "frame %u's chunks: %lu bytes", f, (unsigned long)n);
			at += n;
		}
		m[SMK_HEADER + 4 * (size_t)frames + f] = type;
		at += 4; /* the video: no bits needed */
		while ((at - start) % 4) at++;
		kp_le_put32(m + SMK_HEADER + 4 * (size_t)f, (uint32_t)(at - start));
	}
	*size = at;
	return m;
}
