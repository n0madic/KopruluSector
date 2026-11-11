/* The interface over its data's parts (kp_ui_data.h; plan phase S, stage S7) from the converter's fixtures (`python3 -m
 * converter.kpf.fixtures`; skipped without them): an interface over the fixture's ui.kpd and fonts.kpd sections
 * (kp_t_fixture_ui), its fonts through the text engine, a dialog loaded from its BIN, the themes and files kp_ui reads,
 * the sounds' durations over sounds.kpd's SFX and MUSL (kp_ui_set_sound_records) and the portraits' movies over
 * portraits.kpd's ROWS (kp_ui_set_portrait_parts). The executable's tables are the profile's compiled ones (the
 * interfaces over the original data read them: test_hud, test_kp_ui).
 *
 * What the containers hold and no C code reads is not checked here: fonts.kpd's TFNT (the glue themes' tFont banks
 * are ui.kpd's TFNT), ui.kpd's FLUF, CURS and RECT, sounds.kpd's PACK, and portraits.kpd's SMKS and PALS (kp_ui decodes
 * the movies of FILE itself, kp_smk.h); their contents are the converter's, checked by converter/tests
 * (test_ui_tables.py, test_sounds.py, test_portraits.py). */
#include "kp_test_ui.h"

#include <stdlib.h>
#include <string.h>

#include "../../native/kp_core/src/core/kp_smk.h"
#include "../../native/kp_core/src/core/kp_ui.h"
#include "../../native/kp_core/src/core/kp_ui_text.h"
#include "kp_webm.h"

#define DIR KP_T_FIXTURES

static const char *const FIXTURES[] = { DIR "ui.kpd", DIR "fonts.kpd", DIR "sounds.kpd", DIR "portraits.kpd", NULL };

static int ready(KpT *T) { return kp_t_ready(T, "test_ui_data", FIXTURES); }


/* --- fonts.kpd (one tiny font in every slot, codes 32..34, glyph 34 3 x 2) --- */

static void fonts_metrics(KpT *T)
{
	KpUi *ui;
	KpUiText t;
	const uint8_t *g;
	int k;
	if (!ready(T)) return;
	ui = kp_t_fixture_ui(T);
	for (k = 0; k < 5; k++) kp_t_ok(T, ui->font_files[k] != NULL && ui->font_file_sizes[k] > 0, "font %d", k);
	kp_ui_text_init(&t);
	kp_t_eq(T, kp_ui_text_select_font(&t, ui->font_files[4], ui->font_file_sizes[4]), 0, "font16x is an FNT image");
	kp_t_eq(T, t.font[4], 32, "the first code");
	kp_t_eq(T, t.font[5], 34, "the last code");
	kp_t_eq(T, t.max_width, 3, "the maximum width");
	kp_t_eq(T, t.max_height, 2, "the maximum height");
	kp_t_eq(T, kp_ui_glyph(&t, 32), 0, "space has no glyph");
	kp_t_eq(T, kp_ui_char_width(&t, 32), 1, "space advances half the maximum width");
	kp_t_ok(T, kp_ui_glyph(&t, 34) > 0 && (size_t)kp_ui_glyph(&t, 34) + 4 <= t.font_size, "glyph 34");
	g = t.font + kp_ui_glyph(&t, 34);
	kp_t_ok(T, g[0] == 3 && g[1] == 2 && g[2] == 1 && g[3] == 0, "glyph 34: %d x %d at (%d, %d)", g[0], g[1], g[2], g[3]);
	kp_t_eq(T, kp_ui_glyph(&t, 35), -1, "35 is outside the font");
	kp_t_eq(T, kp_ui_glyph(&t, 31), -1, "31 is outside the font");
	kp_t_eq(T, (int64_t)ui->game_rows_size, 192, "tFontGam.pcx's rows");
}

/* --- ui.kpd (the synthetic dialog of converter/tests/test_ui.py) --- */

static void ui_dialog(KpT *T)
{
	KpUi *ui;
	const KpUiControl *root, *button, *html;
	const KpUiBin *bin;
	const uint8_t *smk;
	int32_t d;
	if (!ready(T)) return;
	ui = kp_t_fixture_ui(T);
	kp_t_eq(T, ui->bin_count, 1, "BINs");
	bin = &ui->bins[0];
	kp_t_ok(T, bin->name_len == 9 && memcmp(bin->name, "rez\\x.bin", 9) == 0, "the BIN's name");
	kp_t_eq(T, bin->widget_count, 3, "widgets");
	d = kp_ui_load_dialog(ui, "rez\\x.bin");
	kp_t_ok(T, d >= 0, "the dialog loads: %s", ui->error);
	root = &ui->controls[d];
	kp_t_eq(T, root->type, 0, "the root is a dialog");
	kp_t_eq(T, root->parent, -1, "the root has no parent");
	kp_t_ok(T, root->child >= 0, "the root has children");
	button = &ui->controls[root->child];
	kp_t_eq(T, button->parent, d, "the button's parent");
	kp_t_eq(T, button->id, 7, "the button's id");
	kp_t_ok(T, button->text != NULL, "the button's text");
	kp_t_eq_str(T, (const char *)button->text, "quit", "the button's text");
	kp_t_eq(T, button->flags & 0xffff, 0x200, "the button's flags");
	kp_t_ok(T, button->next >= 0, "a second child");
	html = &ui->controls[button->next];
	kp_t_eq(T, html->type, 14, "an HTML control");
	kp_t_eq(T, html->left, 100, "its left");
	kp_t_eq(T, html->extra[0], 101, "its first extra word");
	kp_t_eq(T, html->extra[3], 54, "its last extra word");
	kp_t_eq(T, kp_le_u16(bin->widgets + 2 * KP_UI_WIDGET_SIZE + 28), 0, "its first SMK node");
	kp_t_eq(T, bin->smk_count, 1, "SMK nodes");
	smk = bin->smks;
	kp_t_ok(T, kp_le_u32(smk + 2) < ui->pool_size, "the node's file name");
	kp_t_eq_str(T, (const char *)ui->pool + kp_le_u32(smk + 2), "port.smk", "the node's file name");
	kp_t_eq(T, (int16_t)kp_le_u16(smk + 10), 6, "the node's y offset");
}

static void ui_themes_and_grps(KpT *T)
{
	KpUi *ui;
	const KpUiFile *f;
	if (!ready(T)) return;
	ui = kp_t_fixture_ui(T);
	kp_t_ok(T, ui->theme_fonts != NULL, "the themes' colour banks (12 x 48)");
	kp_t_eq(T, ui->theme_fonts[47], 47, "the first bank's last byte");
	kp_t_eq(T, ui->theme_effect_size, 2, "tEffect.pcx's size");
	f = kp_ui_file(ui->grps, ui->grp_count, "a.grp");
	kp_t_ok(T, f != NULL, "the dialog GRP a.grp");
	kp_t_eq(T, (int64_t)f->size, 14, "its size");
	f = kp_ui_glue_file(ui, "GLUE\\X.PCX");
	kp_t_ok(T, f != NULL && f->size == 3 && memcmp(f->data, "abc", 3) == 0, "the glue file glue\\x.pcx");
}

/* --- sounds.kpd --- */

static void sounds_records_and_durations(KpT *T)
{
	KpUi *ui;
	size_t sfx_size, musl_size;
	const uint8_t *sfx, *musl;
	if (!ready(T)) return;
	ui = kp_t_fixture_ui(T);
	sfx = kp_t_fixture_section(T, "sounds.kpd", "SFX ", &sfx_size);
	musl = kp_t_fixture_section(T, "sounds.kpd", "MUSL", &musl_size);
	kp_t_eq(T, kp_ui_set_sound_records(ui, sfx, sfx_size, musl, musl_size), 0, "%s", ui->error);
	kp_t_eq(T, ui->sound_count, 4, "sounds");
	kp_t_eq(T, kp_le_u16(ui->sounds) & 3, 0, "sound 0 has no file");
	kp_t_eq(T, kp_le_u16(ui->sounds + 22 * 3) & 3, 1, "sound 3's file is present and not parsed");
	kp_t_eq(T, kp_ui_sound_duration(ui, 0), 0, "no file, no duration");
	kp_t_eq(T, kp_ui_sound_duration(ui, 3), 0, "a file the native parser rejects has none");
	kp_t_eq(T, kp_le_u32(ui->sounds + 22 + 6), 22050, "sound 1's sample rate");
	kp_t_eq(T, kp_ui_sound_duration(ui, 1), 1000, "44,100 bytes at 44,100 per second");
	/* 5,000,000 * 1000 wraps u32 before the division, as the native imul/div */
	kp_t_eq(T, kp_ui_sound_duration(ui, 2), (int64_t)(((5000000ull * 1000ull) % 4294967296ull) / 44100ull), "the wrap");
	kp_t_eq(T, kp_ui_sound_duration(ui, 4), 0, "past the table");
	kp_t_eq(T, ui->music.length_ms[0], 10000, "MUSL: 441,000 bytes at 44,100 per second");
}

/* --- portraits.kpd (row 0's idle variants 0 and 2, its talking variant 1) --- */

/* The portraits' parts from the fixture's portraits.kpd: its ROWS, and a FILE list (u16 count; per file u8 path length,
 * the path, u32 size, the bytes) of one byte per SMKS path (the fixture holds no movies). rows_cut and file_cut drop
 * that many bytes from the end of ROWS and of FILE: 0, or -1 when the parts are refused (FILE by kp_ui_files_section,
 * ROWS by kp_ui_set_portrait_parts). The interface borrows FILE's bytes: they live until the test ends. */
static int set_portraits(KpT *T, KpUi *ui, size_t rows_cut, size_t file_cut)
{
	enum { FILE_ROOM = 512 };
	size_t rows_size, smks_size, at = 2, pos = 2;
	const uint8_t *rows = kp_t_fixture_section(T, "portraits.kpd", "ROWS", &rows_size),
		*smks = kp_t_fixture_section(T, "portraits.kpd", "SMKS", &smks_size);
	uint8_t *file = (uint8_t *)malloc(FILE_ROOM);
	KpUiFiles files;
	int k, n, rc;
	if (!file) kp_t_fail(T, "out of memory");
	kp_t_defer(T, free, file);
	kp_t_ok(T, smks_size >= 2, "portraits.kpd's SMKS");
	n = (int)kp_le_u16(smks);
	memcpy(file, smks, 2);
	for (k = 0; k < n; k++) {
		size_t len = smks[pos];
		uint32_t frames;
		if (pos + 1 + len + 13 > smks_size || at + 6 + len > FILE_ROOM) break;
		memcpy(file + at, smks + pos, 1 + len);
		at += 1 + len;
		file[at] = 1; /* u32 size 1, then the byte */
		file[at + 1] = file[at + 2] = file[at + 3] = 0;
		file[at + 4] = (uint8_t)('a' + k);
		at += 5;
		frames = kp_le_u16(smks + pos + 1 + len + 4);
		pos += 1 + len + 13 + 2 * (size_t)frames;
	}
	kp_t_eq(T, k, n, "portraits.kpd's SMKS is well formed");
	if (kp_ui_files_section(file, at - file_cut, &files) != 0) {
		free(files.files);
		return -1;
	}
	rc = kp_ui_set_portrait_parts(ui, rows, rows_size - rows_cut, files.files, files.count);
	free(files.files);
	return rc;
}

static void movie_is(KpT *T, KpUi *ui, uint32_t row, int talking, int variant, const char *path)
{
	const KpUiFile *f = kp_ui_portrait_movie(ui, row, talking, variant);
	if (!path) {
		kp_t_ok(T, f == NULL, "row %u %s variant %d has no movie", row, talking ? "talking" : "idle", variant);
		return;
	}
	kp_t_ok(T, f != NULL && f->path_len == strlen(path) && memcmp(f->path, path, f->path_len) == 0,
		"row %u %s variant %d is %s", row, talking ? "talking" : "idle", variant, path);
}

static void portraits_rows_and_variants(KpT *T)
{
	KpUi *ui;
	char name[64];
	if (!ready(T)) return;
	ui = kp_t_fixture_ui(T);
	kp_t_eq(T, set_portraits(T, ui, 0, 0), 0, "%s", ui->error);
	kp_t_eq(T, ui->portrait_row_count, 2, "portrait rows");
	kp_t_eq(T, ui->portrait_file_count, 3, "movies");
	kp_t_eq(T, kp_le_u16(ui->portrait_rows), 0, "row 0's idle movies start at the first file");
	kp_t_eq(T, kp_le_u16(ui->portrait_rows + 2), 5, "row 0's idle variants 0 and 2");
	movie_is(T, ui, 0, 0, 0, "portrait\\a0.smk");
	movie_is(T, ui, 0, 0, 1, NULL);
	movie_is(T, ui, 0, 0, 2, "portrait\\a2.smk");
	movie_is(T, ui, 0, 1, 1, "portrait\\t1.smk");
	movie_is(T, ui, 1, 0, 0, NULL);
	movie_is(T, ui, 2, 0, 0, NULL);
	kp_t_ok(T, kp_ui_portrait_movie(ui, 0, 1, 1)->size == 1 && kp_ui_portrait_movie(ui, 0, 1, 1)->data[0] == 'c',
		"the talking movie's bytes");
	kp_t_eq(T, kp_ui_portrait_movie_name(ui, 0, 1, 1, name, sizeof name), 0, "the talking movie's name");
	kp_t_eq_str(T, name, "portrait\\t1.smk", "the talking movie's name");
	kp_t_eq(T, kp_ui_portrait_movie_name(ui, 1, 0, 0, name, sizeof name), -1, "row 1 has no idle movie");
}

/* A ROWS or FILE section shorter than its counts say is rejected without a read past it (the last record's data, then
 * its size, then its path cut; a row cut off), and a rejected ROWS leaves no movies. */
static void portraits_reject_truncated(KpT *T)
{
	KpUi *ui;
	size_t cut;
	if (!ready(T)) return;
	ui = kp_t_fixture_ui(T);
	kp_t_eq(T, set_portraits(T, ui, 0, 0), 0, "the whole parts load: %s", ui->error); /* then rejections */
	for (cut = 1; cut <= 20; cut += cut < 8 ? 1 : 6) {
		kp_t_eq(T, set_portraits(T, ui, 0, cut), -1, "FILE cut by %u bytes is rejected", (unsigned)cut);
	}
	kp_t_eq(T, set_portraits(T, ui, 1, 0), -1, "ROWS cut by a byte is rejected");
	kp_t_eq(T, ui->portrait_file_count, 0, "no movies after a rejected ROWS");
	kp_t_eq(T, ui->portrait_row_count, 0, "no rows after a rejected ROWS");
	kp_t_ok(T, kp_ui_portrait_movie(ui, 0, 0, 0) == NULL, "no movie after a rejected ROWS");
	kp_t_eq(T, set_portraits(T, ui, 0, 0), 0, "the whole parts load again: %s", ui->error);
}

/* A KPW1 fixture (kp_smk.h, converter/formats/kpw.py) plays as an SMK ring movie (frames 0, 1, 2, the ring frame, then 1
 * again; a rewind back to 0), each stored frame the fixture's indices: `frames` frames and the ring frame. */
static void kpw_plays(KpT *T, const char *name, uint32_t width, uint32_t height, uint32_t frames)
{
	char path[128], indices_path[160], err[256] = "";
	size_t size, indices_size;
	KpWebm *webm;
	const uint8_t *data, *indices;
	uint32_t f, area = width * height;
	KpSmk s;
	snprintf(path, sizeof path, DIR "%s", name);
	snprintf(indices_path, sizeof indices_path, "%s.indices", path);
	data = kp_t_read(T, path, &size);
	indices = kp_t_read(T, indices_path, &indices_size);
	kp_t_eq(T, (int64_t)indices_size, (int64_t)area * (frames + 1), "%s: the fixture's frames and the ring frame", name);
	webm = kp_webm_open(data + 24 + 768, size - 24 - 768, err, sizeof err);
	kp_t_ok(T, webm != NULL, "%s: its WebM opens: %s", name, err);
	kp_t_eq(T, kp_webm_width(webm) * 1000 + kp_webm_height(webm), width * 1000 + height, "%s: the WebM's size", name);
	kp_webm_close(webm);
	kp_t_eq(T, kp_smk_open(&s, data, size), 0, "%s opens", name);
	kp_t_eq(T, s.width * 1000 + s.height, width * 1000 + height, "%s: its size", name);
	kp_t_eq(T, s.frames, frames, "%s: its frames", name);
	kp_t_eq(T, kp_smk_frame_time(&s), 10000, "%s: 100 ms a frame", name);
	for (f = 0; f <= frames; f++) {
		kp_t_eq(T, kp_smk_decode(&s), 0, "%s: frame %u", name, (unsigned)f);
		kp_t_ok(T, memcmp(s.pixels, indices + (size_t)f * area, area) == 0, "%s: frame %u's indices", name, (unsigned)f);
	}
	kp_t_eq(T, s.frame, 1, "%s: after the ring frame, frame 1", name);
	kp_t_eq(T, kp_smk_decode(&s), 0, "%s: frame 1 again", name);
	kp_t_ok(T, memcmp(s.pixels, indices + area, area) == 0, "%s: frame 1's indices again", name);
	kp_smk_rewind(&s);
	kp_t_eq(T, kp_smk_decode(&s), 0, "%s: frame 0 after the rewind", name);
	kp_t_ok(T, memcmp(s.pixels, indices, area) == 0, "%s: frame 0's indices after the rewind", name);
	kp_smk_close(&s);
}

/* The lossless 4:4:4 sRGB movie (as Remastered's SD portraits): three WebM frames, the ring frame past them its last;
 * the VP8 movie with alpha (as the glue's overlays): white, red with its left half transparent, the green ring frame,
 * each colour its palette index. A header of another size than the WebM's, and a WebM cut short, are refused. */
static void kpw_movie_plays_as_an_smk(KpT *T)
{
	size_t size;
	uint8_t *data;
	KpSmk s;
	if (!kp_t_has(T, DIR "movie.kpw"))
		kp_t_skip(T, "no " DIR "movie.kpw (python3 -m converter.kpf.fixtures with FFmpeg's libvpx)");
	kpw_plays(T, "movie.kpw", 8, 4, 3);
	kpw_plays(T, "movie-alpha.kpw", 16, 16, 2);
	data = kp_t_read(T, DIR "movie.kpw", &size);
	kp_t_eq(T, kp_smk_open(&s, data, 24 + 768 + 16), -1, "a WebM cut short is refused");
	kp_t_eq(T, kp_smk_open(&s, data, 24 + 767), -1, "a palette cut short is refused");
	data[4] = 9; /* width 9, the WebM's 8 */
	kp_t_eq(T, kp_smk_open(&s, data, size), -1, "a header of another width than the WebM's is refused");
}

/* An SMK keeps one block: the four header trees' entries and the frame's pixels (a portrait opens one at every change).
 * A 4 x 4 movie of one frame whose four trees are absent opens with every code 0; a header tree declaring more than the
 * decoder holds is refused. */
static void smk_header_opens_in_one_block(KpT *T)
{
	uint8_t file[104 + 4 + 1 + 1];
	KpSmk s;
	int i;
	memset(file, 0, sizeof file);
	memcpy(file, "SMK2", 4);
	file[4] = file[8] = 4;  /* width, height */
	file[12] = 1;           /* one frame */
	file[16] = 40;          /* 40 ms */
	file[52] = 1;           /* the trees' bytes: the four absent flags */
	for (i = 0; i < 4; i++) file[56 + 4 * i] = 16; /* declared sizes */
	file[104] = 1;          /* the frame's size */
	kp_t_eq(T, kp_smk_open(&s, file, sizeof file), 0, "an SMK without trees opens");
	kp_t_eq(T, s.width * 10 + s.height, 44, "its size");
	kp_t_ok(T, s.mem != NULL && s.pixels == s.mem + 4 * (((16 + 3) >> 2) + 4) * sizeof(int32_t),
		"the pixels follow the trees in one block");
	for (i = 0; i < 4; i++) kp_t_ok(T, s.trees[i].count == 1 && s.trees[i].values[0] == 0, "tree %d: every code is 0", i);
	kp_t_ok(T, s.pixels[0] == 0 && s.pixels[15] == 0, "the pixels are clear");
	kp_smk_close(&s);
	kp_t_ok(T, s.mem == NULL && s.pixels == NULL, "closed");
	file[56 + 8] = 1;       /* the FULL tree declares 16 MB + 1 */
	file[56 + 11] = 1;
	kp_t_eq(T, kp_smk_open(&s, file, sizeof file), -1, "a header tree of 16 MB and more is refused");
	file[56 + 11] = 0;
	file[56 + 8] = 16;
	kp_t_eq(T, kp_smk_open(&s, file, sizeof file), 0, "and the same file with its size back opens");
	kp_smk_close(&s);
	file[52] = 0;           /* the trees' bytes past the file: no sum that wraps where size_t is 32-bit lets it in */
	file[53] = 0xf0;
	file[54] = file[55] = 0xff;
	kp_t_eq(T, kp_smk_open(&s, file, sizeof file), -1, "the trees' bytes past the file are refused");
	file[52] = 1;
	file[53] = file[54] = file[55] = 0;
	file[104] = 0xfc;       /* a frame of 4 GB - 4 */
	file[105] = file[106] = file[107] = 0xff;
	kp_t_eq(T, kp_smk_open(&s, file, sizeof file), 0, NULL);
	kp_t_eq(T, kp_smk_decode(&s), -1, "a frame past the file is not decoded");
	kp_smk_close(&s);
	{
		uint8_t pal[104 + 4 + 1 + 1 + 8];
		memcpy(pal, file, 104);
		memset(pal + 104, 0, sizeof pal - 104);
		pal[52] = 1;
		pal[104] = 8;          /* a frame of 8 bytes */
		pal[108] = 0x03;       /* a palette and audio track 1 */
		pal[109] = 0;          /* the four absent trees' flags */
		pal[110] = 0xff;       /* the palette's 1020 bytes past the frame's 8 */
		kp_t_eq(T, kp_smk_open(&s, pal, sizeof pal), 0, NULL);
		kp_t_eq(T, kp_smk_decode(&s), -1, "a palette past its frame's end is refused before the audio track's size");
		kp_smk_close(&s);
	}
}

/* A table string goes through the original's SStrVPrintf rules, never as a printf format: %s the name, %d the numbers,
 * %% one %, anything else (a %n, a third %d's x) copied or formatted safely, within the buffer. */
static void table_strings_are_no_formats(KpT *T)
{
	char out[32];
	kp_ui_format_name_xy(out, sizeof out, (const uint8_t *)"Replace \"%s\"? %n%x 100%%", (const uint8_t *)"save", 0, 0);
	kp_t_ok(T, strcmp(out, "Replace \"save\"? %n%x 100%") == 0, "only %%s and %%%% replaced: %s", out);
	kp_ui_format_name_xy(out, sizeof out, (const uint8_t *)"%s %s %d %d %d", (const uint8_t *)"a", 3, 4);
	kp_t_ok(T, strcmp(out, "a a 3 4 4") == 0, "a second %%s, a third %%d: %s", out);
	kp_ui_format_name_xy(out, 6, (const uint8_t *)"%s!", (const uint8_t *)"a long name", 0, 0);
	kp_t_ok(T, strcmp(out, "a lon") == 0, "cut to the buffer: %s", out);
	kp_ui_format_name_xy(out, sizeof out, NULL, (const uint8_t *)"a", 0, 0);
	kp_t_ok(T, out[0] == 0, "no table string: empty");
}

/* A movie decodes into a target only of its own size (a BIN's or a control's size that differs would overflow it). */
static void video_target_is_the_movie_size(KpT *T)
{
	uint8_t file[104 + 4 + 1 + 1], target[8 * 8];
	KpUiFile f;
	KpUiVideo v;
	KpUi *ui = (KpUi *)calloc(1, sizeof *ui);
	int i;
	kp_t_ok(T, ui != NULL, "an interface");
	if (ui == NULL) return;
	memset(file, 0, sizeof file);
	memcpy(file, "SMK2", 4);
	file[4] = file[8] = 4; /* 4 x 4 */
	file[12] = 1;
	file[52] = 1;
	for (i = 0; i < 4; i++) file[56 + 4 * i] = 16;
	file[104] = 1;
	f.path = "test.smk";
	f.path_len = 8;
	f.data = file;
	f.size = sizeof file;
	memset(&v, 0, sizeof v);
	kp_t_eq(T, kp_ui_video_begin(ui, &v, &f, target, 4, 4), 0, "a target of the movie's size");
	kp_t_ok(T, v.open, NULL);
	kp_t_eq(T, kp_ui_video_begin(ui, &v, &f, target, 4, 0), -1, "a target of another size is refused");
	kp_t_ok(T, !v.open, "and nothing is open");
	kp_t_eq(T, kp_ui_video_begin(ui, &v, &f, target, 2, 8), -1, "the same bytes in other rows too");
	kp_t_eq(T, kp_ui_video_begin(ui, &v, &f, NULL, 0, 0), 0, "no target: the decoder's own pixels");
	kp_t_eq(T, kp_ui_video_begin_zoomed(ui, &v, &f, target, 8, 8), 0, "a zoomed target of 2w x 2h");
	kp_t_eq(T, kp_ui_video_begin_zoomed(ui, &v, &f, target, 4, 4), -1, "a zoomed target of w x h is refused");
	kp_t_ok(T, !v.open && v.frame == NULL, "and released");
	kp_ui_video_end(&v);
	free(ui);
}

static const KpTest TESTS[] = {
	{ "kpw_movie_plays_as_an_smk", kpw_movie_plays_as_an_smk, 0 },
	{ "video_target_is_the_movie_size", video_target_is_the_movie_size, 0 },
	{ "smk_header_opens_in_one_block", smk_header_opens_in_one_block, 0 },
	{ "table_strings_are_no_formats", table_strings_are_no_formats, 0 },
	{ "fonts_metrics", fonts_metrics, 0 },
	{ "ui_dialog", ui_dialog, 0 },
	{ "ui_themes_and_grps", ui_themes_and_grps, 0 },
	{ "sounds_records_and_durations", sounds_records_and_durations, 0 },
	{ "portraits_rows_and_variants", portraits_rows_and_variants, 0 },
	{ "portraits_reject_truncated", portraits_reject_truncated, 0 },
};
KP_SUITE(kp_suite_ui_data, "test_ui_data", TESTS);
