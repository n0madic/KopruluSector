/* The interface's readers of ui.kpd, fonts.kpd, sounds.kpd and portraits.kpd (kp_ui.h; plan phase S, stage S7) over the
 * converter's fixtures (`python3 -m converter.kpf.fixtures`; skipped without them): an interface over the fixture's
 * ui.kpd and fonts.kpd, its fonts through the text engine, a dialog loaded from its BIN, the themes, files and the
 * executable's tables kp_ui reads, the sounds' durations and the portraits' movies.
 *
 * What the containers hold and no C code reads is not checked here: fonts.kpd's TFNT (the glue themes' tFont banks
 * are ui.kpd's TFNT), ui.kpd's FLUF, CURS and RECT and the music tracks' file names (kp_ui keeps the original's own
 * rectangles, cursors and track paths), sounds.kpd's PACK, and portraits.kpd's SMKS and PALS (kp_ui decodes the movies
 * of FILE itself, kp_smk.h); their contents are the converter's, checked by converter/tests (test_ui_tables.py,
 * test_sounds.py, test_portraits.py). */
#include "kp_test.h"

#include <stdlib.h>
#include <string.h>

#include "../../native/kp_core/src/core/kp_smk.h"
#include "../../native/kp_core/src/core/kp_ui.h"
#include "../../native/kp_core/src/core/kp_ui_text.h"

#define DIR "tests/fixtures/generated/kpf/"

static const char *const FIXTURES[] = { DIR "ui.kpd", DIR "fonts.kpd", DIR "sounds.kpd", DIR "portraits.kpd", NULL };

static int ready(KpT *T) { return kp_t_ready(T, "test_ui_data", FIXTURES); }

static void free_ui(void *p) { kp_ui_free((KpUi *)p); }

/* An interface over the fixture's ui.kpd and fonts.kpd (the bytes live until the test ends). */
static KpUi *fixture_ui(KpT *T)
{
	size_t ui_size, fonts_size;
	uint8_t *ui_kpd = kp_t_read(T, DIR "ui.kpd", &ui_size), *fonts = kp_t_read(T, DIR "fonts.kpd", &fonts_size);
	char err[256];
	KpUi *ui = kp_ui_new(ui_kpd, ui_size, fonts, fonts_size, err, sizeof err);
	kp_t_ok(T, ui != NULL, "%s", err);
	kp_t_defer(T, free_ui, ui);
	return ui;
}

/* --- fonts.kpd (one tiny font in every slot, codes 32..34, glyph 34 3 x 2) --- */

static void fonts_metrics(KpT *T)
{
	KpUi *ui;
	KpUiText t;
	const uint8_t *g;
	int k;
	if (!ready(T)) return;
	ui = fixture_ui(T);
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
	ui = fixture_ui(T);
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
	ui = fixture_ui(T);
	kp_t_ok(T, ui->theme_fonts != NULL, "the themes' colour banks (12 x 48)");
	kp_t_eq(T, ui->theme_fonts[47], 47, "the first bank's last byte");
	kp_t_eq(T, ui->theme_effect_size, 2, "tEffect.pcx's size");
	f = kp_ui_file(ui->grps, ui->grp_count, "a.grp");
	kp_t_ok(T, f != NULL, "the dialog GRP a.grp");
	kp_t_eq(T, (int64_t)f->size, 14, "its size");
	f = kp_ui_glue_file(ui, "GLUE\\X.PCX");
	kp_t_ok(T, f != NULL && f->size == 3 && memcmp(f->data, "abc", 3) == 0, "the glue file glue\\x.pcx");
}

/* The executable's tables kp_ui takes from ui.kpd: the button sets (BTNS), the status layouts (STLY), the music
 * tracks (MUSI), the volume attenuation (VOLT) and the accelerators (ACCL). */
static void ui_tables(KpT *T)
{
	KpUi *ui;
	const uint8_t *set, *b;
	const KpUiAccelTable *game;
	if (!ready(T)) return;
	ui = fixture_ui(T);
	kp_t_eq(T, ui->button_set_count, 2, "button sets");
	set = ui->button_sets + ui->button_set_at[0];
	kp_t_eq(T, kp_le_u16(set), 0xffff, "set 0 links no set");
	set = ui->button_sets + ui->button_set_at[1];
	kp_t_eq(T, kp_le_u16(set), 0, "set 1 links set 0");
	kp_t_eq(T, kp_le_u16(set + 2), 2, "set 1's buttons");
	b = set + 4;
	kp_t_eq(T, kp_le_u16(b), 7, "the position");
	kp_t_eq(T, kp_le_u32(b + 4), 0x49f850, "the condition");
	kp_t_eq(T, kp_le_u32(b + 8), 0x47c420, "the action");
	kp_t_eq(T, kp_le_u16(b + 12), 10, "the condition's value");
	kp_t_eq(T, kp_le_u16(b + 18), 355, "the disabled string");
	kp_t_eq(T, ui->status_layout_count, 1, "status layouts");
	kp_t_eq(T, kp_le_u32(ui->status_layouts + 4), 0x4a4e40, "the layout's draw procedure");
	kp_t_eq(T, ui->music.track_count, 2, "music tracks");
	kp_t_eq(T, ui->music.kind[1], 2, "track 1's kind");
	kp_t_eq(T, ui->music.next[1], 2, "track 1's next");
	kp_t_ok(T, ui->attenuation != NULL && ui->attenuation_count == 3, "the attenuation table");
	kp_t_eq(T, (int32_t)kp_le_u32(ui->attenuation), -4000, "its first entry");
	game = &ui->keys.tables[KP_UI_ACCEL_GAME];
	kp_t_eq(T, game->count, 3, "the game's accelerators: Local.dll's 101 then the executable's 113");
	kp_t_eq(T, game->cmd[0], 40056, "Local.dll's entry");
	kp_t_eq(T, game->fvirt[2], 19, "the executable's second entry's flags");
	kp_t_eq(T, game->key[2], 49, "its key");
	kp_t_eq(T, game->cmd[2], 39921, "its command");
}

/* --- sounds.kpd --- */

static void sounds_records_and_durations(KpT *T)
{
	KpUi *ui;
	size_t size;
	uint8_t *data;
	if (!ready(T)) return;
	ui = fixture_ui(T);
	data = kp_t_read(T, DIR "sounds.kpd", &size);
	kp_t_eq(T, kp_ui_set_sounds(ui, data, size), 0, "%s", ui->error);
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

/* The fixture's portraits.kpd with its movies' files: ROWS as written, FILE with one byte per SMKS path (the
 * fixture holds no movies). */
static void portraits_with_files(KpT *T, uint8_t **out, size_t *out_size)
{
	size_t size, at, pos = 2;
	uint8_t *data = kp_t_read(T, DIR "portraits.kpd", &size), file[512];
	KpKpf c;
	KpError err;
	const KpKpfSection *rows, *smks;
	KpKpfOut sections[2];
	int k, n;
	kp_t_ok(T, kp_kpf_parse(&c, data, size, "portraits.kpd", 1, &err) == 0, "%s", err.msg);
	rows = kp_kpf_find(&c, "ROWS");
	smks = kp_kpf_find(&c, "SMKS");
	if (!rows || !smks || smks->size < 2) {
		kp_kpf_free(&c);
		kp_t_fail(T, "portraits.kpd lacks ROWS or SMKS");
	}
	n = (int)kp_le_u16(smks->data);
	memcpy(file, smks->data, 2);
	at = 2;
	for (k = 0; k < n; k++) {
		size_t len = smks->data[pos];
		uint32_t frames;
		if (pos + 1 + len + 13 > smks->size || at + 6 + len > sizeof file) break;
		memcpy(file + at, smks->data + pos, 1 + len);
		at += 1 + len;
		file[at] = 1; /* u32 size 1, then the byte */
		file[at + 1] = file[at + 2] = file[at + 3] = 0;
		file[at + 4] = (uint8_t)('a' + k);
		at += 5;
		frames = kp_le_u16(smks->data + pos + 1 + len + 4);
		pos += 1 + len + 13 + 2 * (size_t)frames;
	}
	if (k != n) {
		kp_kpf_free(&c);
		kp_t_fail(T, "portraits.kpd's SMKS is malformed");
	}
	memset(sections, 0, sizeof sections);
	sections[0].fourcc = "ROWS";
	sections[0].payload = rows->data;
	sections[0].size = rows->size;
	sections[1].fourcc = "FILE";
	sections[1].payload = file;
	sections[1].size = at;
	k = kp_kpf_build(c.kind, c.schema_version, c.converter_version, c.profile_id, c.source_hash, sections, 2, out, out_size);
	kp_kpf_free(&c);
	kp_t_eq(T, k, 0, "the container builds");
	kp_t_defer(T, free, *out);
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
	uint8_t *data;
	size_t size;
	char name[64];
	if (!ready(T)) return;
	ui = fixture_ui(T);
	portraits_with_files(T, &data, &size);
	kp_t_eq(T, kp_ui_set_portraits(ui, data, size), 0, "%s", ui->error);
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

/* KPV1 (kp_smk.h, converter/formats/kpv.py): a 2 x 2 ring movie of two frames and the extra one plays as an SMK ring
 * movie plays (frames 0, 1, the extra one, then 1 again); a frame record past the file's end and a run past a frame's
 * pixels are rejected. */
static void kpv_movie_plays_as_an_smk(KpT *T)
{
	static const uint8_t header[24] = { 'K', 'P', 'V', '1', 2, 0, 0, 0, 2, 0, 0, 0, 2, 0, 0, 0, 100, 0, 0, 0, 1, 0, 0, 0 };
	static const uint8_t body[24] = {
		5, 0, 0, 0, 3, 1, 2, 3, 4, /* frame 0: four literal indices */
		2, 0, 0, 0, 129, 9,        /* frame 1: index 9 four times */
		5, 0, 0, 0, 3, 5, 6, 7, 8, /* the ring frame */
	};
	uint8_t file[24 + 768 + sizeof body];
	KpSmk s;
	memset(file, 0, sizeof file);
	memcpy(file, header, sizeof header);
	memcpy(file + 24 + 768, body, sizeof body);
	kp_t_eq(T, kp_smk_open(&s, file, sizeof file - 1), -1, "a frame record past the file's end is rejected");
	kp_t_eq(T, kp_smk_open(&s, file, sizeof file), 0, "a KPV1 ring movie opens");
	kp_t_eq(T, s.width * 10 + s.height, 22, "its size");
	kp_t_eq(T, kp_smk_frame_time(&s), 10000, "100 ms a frame");
	kp_t_eq(T, kp_smk_decode(&s), 0, "frame 0");
	kp_t_ok(T, memcmp(s.pixels, "\1\2\3\4", 4) == 0, "frame 0's literal run");
	kp_t_eq(T, kp_smk_decode(&s), 0, "frame 1");
	kp_t_ok(T, memcmp(s.pixels, "\11\11\11\11", 4) == 0, "frame 1's repeated index");
	kp_t_eq(T, kp_smk_decode(&s), 0, "the ring frame");
	kp_t_ok(T, memcmp(s.pixels, "\5\6\7\10", 4) == 0, "the ring frame's pixels");
	kp_t_eq(T, s.frame, 1, "after the ring frame, frame 1");
	kp_t_eq(T, kp_smk_decode(&s), 0, "frame 1 again");
	kp_t_ok(T, memcmp(s.pixels, "\11\11\11\11", 4) == 0, "frame 1's pixels again");
	file[24 + 768 + 9 + 4] = 130; /* frame 1: five repeats for four pixels */
	kp_smk_rewind(&s);
	kp_t_eq(T, kp_smk_decode(&s), 0, "frame 0 after the rewind");
	kp_t_eq(T, kp_smk_decode(&s), -1, "a run past the frame is rejected");
	kp_smk_close(&s);
	if (kp_t_has(T, DIR "movie.kpv")) { /* the same movie as the converter writes it */
		size_t size;
		const uint8_t *data = kp_t_read(T, DIR "movie.kpv", &size);
		file[24 + 768 + 9 + 4] = 129;
		kp_t_eq(T, size, sizeof file, "the converter's KPV1 has the layout above");
		kp_t_ok(T, size == sizeof file && memcmp(data, file, 24) == 0 && memcmp(data + 24 + 768, body, sizeof body) == 0,
			"the converter's KPV1 header and frames");
	}
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
	{ "kpv_movie_plays_as_an_smk", kpv_movie_plays_as_an_smk, 0 },
	{ "video_target_is_the_movie_size", video_target_is_the_movie_size, 0 },
	{ "smk_header_opens_in_one_block", smk_header_opens_in_one_block, 0 },
	{ "table_strings_are_no_formats", table_strings_are_no_formats, 0 },
	{ "fonts_metrics", fonts_metrics, 0 },
	{ "ui_dialog", ui_dialog, 0 },
	{ "ui_themes_and_grps", ui_themes_and_grps, 0 },
	{ "ui_tables", ui_tables, 0 },
	{ "sounds_records_and_durations", sounds_records_and_durations, 0 },
	{ "portraits_rows_and_variants", portraits_rows_and_variants, 0 },
};
KP_SUITE(kp_suite_ui_data, "test_ui_data", TESTS);
