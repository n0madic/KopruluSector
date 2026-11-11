/* kp_scr (native/kp_app/kp_scr.h, ADR 0018 stage D6): StarCraft: Remastered's own formats brought back to 1.17's files,
 * on the synthetic inputs `python3 -m converter.kpf.fixtures` writes with converter/casc/rebuild.py's results beside
 * them (converter.kpf.fixtures.scr_fixture, tests/fixtures/generated/kpf/scr/): each function gives rebuild.py's bytes.
 * Over a real install `kp_convert digests` checks every rebuild kp_res makes against `converter scr-digests`. */
#include "kp_test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kp_scr.h"
#include "../../native/kp_core/src/core/kp_kpf.h"

#define DIR "tests/fixtures/generated/kpf/scr/"

static uint8_t *fixture(KpT *T, const char *name, size_t *size)
{
	char path[256];
	snprintf(path, sizeof path, DIR "%s", name);
	return kp_t_read(T, path, size);
}

static int same(const uint8_t *a, size_t an, const uint8_t *b, size_t bn)
{
	return an == bn && (an == 0 || memcmp(a, b, an) == 0);
}

static void image_free(void *p) { kp_scr_image_free((KpScrImage *)p); }
static void map_free(void *p) { kp_index_map_free((KpIndexMap *)p); }

/* A fixture's DDS decoded, released at the end. */
static KpScrImage *decoded(KpT *T, const char *name)
{
	char file[64], why[KP_SCR_ERR] = "";
	size_t size;
	const uint8_t *data;
	KpScrImage *im = (KpScrImage *)calloc(1, sizeof *im);
	kp_t_ok(T, im != NULL, "out of memory");
	kp_t_defer(T, free, im);
	snprintf(file, sizeof file, "%s.dds", name);
	data = fixture(T, file, &size);
	kp_t_eq(T, kp_scr_dds(data, size, im, why, sizeof why), 0, "%s decodes: %s", file, why);
	kp_t_defer(T, image_free, im);
	return im;
}

/* The palette's index rule, released at the end. */
static KpIndexMap *palette_map(KpT *T)
{
	size_t size;
	const uint8_t *palette = fixture(T, "palette.pal", &size);
	KpIndexMap *map = (KpIndexMap *)calloc(1, sizeof *map);
	kp_t_ok(T, map != NULL && size == 768, "palette.pal");
	kp_t_defer(T, free, map);
	kp_t_eq(T, kp_index_map_init(map, palette), 0, "the index map");
	kp_t_defer(T, map_free, map);
	return map;
}

/* An uncompressed DDS with alpha, DXT1 in both colour modes, DXT5 in both alpha modes decode to dds.decode's RGBA and
 * take rebuild.indices' indices; a DXT body cut short is refused. */
static void dds_decode_and_index(KpT *T)
{
	static const char *const NAMES[] = { "rgba", "dxt1", "dxt5" };
	KpIndexMap *map = palette_map(T);
	size_t k, size, want_size;
	for (k = 0; k < sizeof NAMES / sizeof NAMES[0]; k++) {
		char file[64];
		KpScrImage *im = decoded(T, NAMES[k]);
		uint8_t *want, *got;
		snprintf(file, sizeof file, "%s.rgba", NAMES[k]);
		want = fixture(T, file, &want_size);
		kp_t_ok(T, same(im->rgba, (size_t)im->width * im->height * 4, want, want_size), "%s's RGBA", NAMES[k]);
		snprintf(file, sizeof file, "%s.indices", NAMES[k]);
		want = fixture(T, file, &want_size);
		got = (uint8_t *)malloc((size_t)im->width * im->height);
		kp_t_ok(T, got != NULL, "out of memory");
		kp_t_defer(T, free, got);
		kp_scr_indices(im, map, got);
		kp_t_ok(T, same(got, (size_t)im->width * im->height, want, want_size), "%s's indices", NAMES[k]);
	}
	{
		KpScrImage im;
		char why[KP_SCR_ERR] = "";
		const uint8_t *data = fixture(T, "dxt5.dds", &size);
		kp_t_eq(T, kp_scr_dds(data, size - 1, &im, why, sizeof why), -1, "a DXT5 body cut short is refused");
		kp_t_ok(T, why[0] != 0, "with a reason");
	}
}

/* A .dds.grp's entries are its images in order with their sizes; one cut short is refused. */
static void dds_container_entries(KpT *T)
{
	size_t size, a_size, b_size;
	const uint8_t *data = fixture(T, "images.dds.grp", &size), *a = fixture(T, "rgba.dds", &a_size);
	const uint8_t *b = fixture(T, "dxt1.dds", &b_size);
	KpScrDdsEntry *e = NULL;
	uint32_t count = 0;
	kp_t_eq(T, kp_scr_dds_container(data, size, &e, &count), 0, "the container reads");
	kp_t_defer(T, free, e);
	kp_t_eq(T, count, 2, "its images");
	kp_t_ok(T, e[0].width == 3 && e[0].height == 2 && same(e[0].dds, e[0].size, a, a_size), "the first image");
	kp_t_ok(T, e[1].width == 8 && e[1].height == 4 && same(e[1].dds, e[1].size, b, b_size), "the second image");
	kp_t_eq(T, kp_scr_dds_container(data, size - 1, &e, &count), -1, "a container cut short is refused");
}

/* rebuild.fit's three modes: a block-compressed image's padding cut, an image of a whole multiple sampled, a shorter
 * image kept as the bottom rows; one that fits none is refused. */
static void images_fit(KpT *T)
{
	static const struct {
		const char *name, *source;
		uint32_t width, height;
	} CASES[] = { { "crop", "dxt1", 6, 3 }, { "sample", "dxt1", 4, 2 }, { "bottom", "rgba", 3, 5 } };
	size_t k, size;
	for (k = 0; k < sizeof CASES / sizeof CASES[0]; k++) {
		char file[64];
		KpScrImage *im = decoded(T, CASES[k].source);
		uint32_t above = 99;
		const uint8_t *want;
		snprintf(file, sizeof file, "fit-%s", CASES[k].name);
		want = fixture(T, file, &size);
		kp_t_ok(T, kp_scr_fits(im->width, im->height, CASES[k].width, CASES[k].height), "%s fits", CASES[k].name);
		kp_t_eq(T, kp_scr_fit(im, CASES[k].width, CASES[k].height, &above), 0, "%s", CASES[k].name);
		kp_t_eq(T, above, kp_le_u32(want), "%s: the rows above", CASES[k].name);
		kp_t_ok(T, im->width == CASES[k].width && same(im->rgba, (size_t)im->width * im->height * 4, want + 4, size - 4),
			"%s: the pixels", CASES[k].name);
	}
	{
		KpScrImage *im = decoded(T, "rgba");
		uint32_t above;
		kp_t_ok(T, !kp_scr_fits(3, 2, 4, 2) && !kp_scr_fits(8, 4, 3, 2), "3 x 2 fits no 4 x 2, 8 x 4 no 3 x 2");
		kp_t_eq(T, kp_scr_fit(im, 4, 2, &above), -1, "nor is it brought to it");
	}
}

/* rebuild.pcx_file's bytes (the RLE, the header's bytes from header[6], the palette after its marker), and the palette
 * read back. */
static void pcx_written(KpT *T)
{
	static const uint8_t HEADER[6] = { 1, 2, 3, 4, 5, 6 };
	size_t size, pixels_size, palette_size, want_size;
	const uint8_t *pixels = fixture(T, "pcx.pixels", &pixels_size), *palette = fixture(T, "palette.pal", &palette_size);
	const uint8_t *want = fixture(T, "pcx.pcx", &want_size);
	uint8_t *pcx, back[768];
	kp_t_eq(T, pixels_size, 15, "pcx.pixels");
	pcx = kp_scr_pcx(5, 3, pixels, palette, HEADER, &size);
	kp_t_ok(T, pcx != NULL, "out of memory");
	kp_t_defer(T, free, pcx);
	kp_t_ok(T, same(pcx, size, want, want_size), "the PCX (%u bytes, rebuild.py's %u)", (unsigned)size,
		(unsigned)want_size);
	kp_t_eq(T, kp_scr_pcx_palette(pcx, size, back), 0, "its palette");
	kp_t_ok(T, memcmp(back, palette, 768) == 0, "the palette read back");
	kp_t_eq(T, kp_scr_pcx_palette(pcx, 700, back), -1, "no palette in 700 bytes");
}

/* A TBL's strings (rebuild.tbl_spans) and the TBL written again (tbl_encode); offsets out of order are refused. */
static void tbl_spans_and_encode(KpT *T)
{
	size_t size, out_size;
	uint8_t *data = fixture(T, "strings.tbl", &size), *out;
	KpScrSpan *spans = NULL;
	uint32_t count = 0;
	kp_t_eq(T, kp_scr_tbl_spans(data, size, &spans, &count), 0, "the TBL's strings");
	kp_t_defer(T, free, spans);
	kp_t_eq(T, count, 4, "four strings");
	kp_t_ok(T, same(spans[2].data, spans[2].size, (const uint8_t *)"third\0part", 10) && spans[1].size == 0,
		"a NUL inside a string kept, an empty one");
	out = kp_scr_tbl(spans, count, &out_size);
	kp_t_ok(T, out != NULL, "out of memory");
	kp_t_defer(T, free, out);
	kp_t_ok(T, same(out, out_size, data, size), "written again as it was");
	kp_le_put16(data + 2, kp_le_u16(data + 4) + 1); /* the first string's offset past the second's */
	kp_t_eq(T, kp_scr_tbl_spans(data, size, &spans, &count), -1, "offsets out of order are refused");
}

static void strings_free(void *p) { kp_scr_strings_free((KpScrStrings *)p); }

/* Remastered's string XML (rebuild.xml_strings: the entities undone, UTF-8 to windows-1252), plain and with RC3.xml's
 * escapes (unescape_rc3); the value of an id the last of equal ids. */
static void xml_strings(KpT *T)
{
	static const char *const NAMES[] = { "strings", "rc3" };
	int k;
	for (k = 0; k < 2; k++) {
		char file[64], why[KP_SCR_ERR] = "";
		size_t size, want_size, at = 0;
		const uint8_t *xml, *want;
		KpScrStrings *s = (KpScrStrings *)calloc(1, sizeof *s);
		const KpScrString *last;
		uint32_t n = 0;
		kp_t_ok(T, s != NULL, "out of memory");
		kp_t_defer(T, free, s);
		snprintf(file, sizeof file, "%s.xml", NAMES[k]);
		xml = fixture(T, file, &size);
		snprintf(file, sizeof file, "%s.expect", NAMES[k]);
		want = fixture(T, file, &want_size);
		kp_t_eq(T, kp_scr_xml_strings(xml, size, k, s, why, sizeof why), 0, "%s: %s", NAMES[k], why);
		kp_t_defer(T, strings_free, s);
		while (at < want_size) {
			const char *id = (const char *)want + at;
			size_t id_len = strlen(id), value_size;
			at += id_len + 1;
			value_size = kp_le_u32(want + at);
			at += 4;
			kp_t_ok(T, n < s->count && strcmp(s->items[n].id, id) == 0
				&& same(s->items[n].value, s->items[n].size, want + at, value_size), "%s: string %u (%s)", NAMES[k],
				(unsigned)n, id);
			at += value_size;
			n++;
		}
		kp_t_eq(T, s->count, n, "%s: the strings", NAMES[k]);
		last = kp_scr_string(s, "a-1", 3);
		kp_t_ok(T, last && same(last->value, last->size, (const uint8_t *)"the last of equal ids", 21),
			"%s: the last of equal ids", NAMES[k]);
		kp_t_ok(T, kp_scr_string(s, "zz", 2) == NULL, "%s: no such id", NAMES[k]);
	}
}

/* rebuild.dialog_rebuild: a text of its length in its place, a longer one appended with its pointers moved, a hotkey's
 * first letter upper case. */
static void dialog_rebuilt(KpT *T)
{
	size_t size, texts_size, want_size, out_size, at = 2;
	const uint8_t *skeleton = fixture(T, "dialog.skeleton", &size), *in = fixture(T, "dialog.texts", &texts_size);
	const uint8_t *want = fixture(T, "dialog.bin", &want_size);
	KpScrDialogText texts[8];
	uint32_t count = kp_le_u16(in), k;
	uint8_t *out;
	kp_t_ok(T, count > 0 && count <= 8, "dialog.texts");
	for (k = 0; k < count; k++) {
		KpScrDialogText *t = &texts[k];
		t->offset = kp_le_u32(in + at);
		t->length = kp_le_u32(in + at + 4);
		t->hotkey = in[at + 8];
		t->pointer_count = in[at + 9];
		t->pointers = in + at + 10;
		at += 10 + 4 * (size_t)t->pointer_count;
		t->text_size = kp_le_u16(in + at);
		t->text = in + at + 2;
		at += 2 + t->text_size;
	}
	kp_t_eq(T, at, texts_size, "dialog.texts read whole");
	out = kp_scr_dialog(skeleton, size, texts, count, &out_size);
	kp_t_ok(T, out != NULL, "the dialog rebuilt");
	kp_t_defer(T, free, out);
	kp_t_ok(T, same(out, out_size, want, want_size), "rebuild.py's BIN (%u bytes, %u)", (unsigned)out_size,
		(unsigned)want_size);
	for (k = 0; k < count; k++) /* a text written in its place, put outside the skeleton */
		if (texts[k].text_size == texts[k].length) texts[k].offset = (uint32_t)size;
	kp_t_ok(T, kp_scr_dialog(skeleton, size, texts, count, &out_size) == NULL, "a text outside the skeleton is refused");
}

/* rebuild.uncompressed_grp: the header, the records, each frame's pixels at its offset, frames sharing rows keeping the
 * first's. */
static void plain_grp(KpT *T)
{
	size_t size, want_size, at = 6;
	const uint8_t *in = fixture(T, "plain.in", &size), *want = fixture(T, "plain.grp", &want_size);
	KpScrGrpRecord records[4];
	uint8_t *frames[4], *out;
	uint32_t count = kp_le_u16(in + 4), k;
	kp_t_ok(T, count <= 4, "plain.in");
	for (k = 0; k < count; k++) {
		KpScrGrpRecord *r = &records[k];
		r->x = in[at];
		r->y = in[at + 1];
		r->w = in[at + 2];
		r->h = in[at + 3];
		r->offset = kp_le_u32(in + at + 4);
		frames[k] = (uint8_t *)in + at + 8;
		at += 8 + (size_t)r->w * r->h;
	}
	out = kp_scr_plain_grp(kp_le_u16(in), kp_le_u16(in + 2), records, count, frames, kp_le_u32(in + at));
	kp_t_ok(T, out != NULL, "the GRP written");
	kp_t_defer(T, free, out);
	kp_t_ok(T, same(out, kp_le_u32(in + at), want, want_size), "rebuild.py's GRP");
	kp_t_ok(T, kp_scr_plain_grp(6, 4, records, count, frames, 60) == NULL, "a frame past the size is refused");
}

/* rebuild.remap_grp: a row-compressed GRP's opaque pixels take their WPE colours' indices in another palette (one it
 * lacks the nearest), re-encoded as grp.encode does. */
static void remap_grp(KpT *T)
{
	size_t size, wpe_size, want_size, out_size;
	const uint8_t *grp = fixture(T, "remap.grp", &size), *wpe = fixture(T, "remap.wpe", &wpe_size);
	const uint8_t *want = fixture(T, "remap.out", &want_size);
	KpIndexMap *map = palette_map(T);
	uint8_t *out = kp_scr_remap_grp(grp, size, wpe, wpe_size, map, &out_size);
	kp_t_ok(T, out != NULL, "the GRP remapped");
	kp_t_defer(T, free, out);
	kp_t_ok(T, same(out, out_size, want, want_size), "rebuild.py's GRP (%u bytes, %u)", (unsigned)out_size,
		(unsigned)want_size);
	kp_t_ok(T, kp_scr_remap_grp(grp, 5, wpe, wpe_size, map, &out_size) == NULL, "a GRP cut short is refused");
}

static const KpTest TESTS[] = {
	{ "dds_decode_and_index", dds_decode_and_index, 0 },
	{ "dds_container_entries", dds_container_entries, 0 },
	{ "images_fit", images_fit, 0 },
	{ "pcx_written", pcx_written, 0 },
	{ "tbl_spans_and_encode", tbl_spans_and_encode, 0 },
	{ "xml_strings", xml_strings, 0 },
	{ "dialog_rebuilt", dialog_rebuilt, 0 },
	{ "plain_grp", plain_grp, 0 },
	{ "remap_grp", remap_grp, 0 },
};
KP_SUITE(kp_suite_scr, "test_scr", TESTS);
