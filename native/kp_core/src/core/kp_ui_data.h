/*
 * The interface's data (ADR 0018, stages D3 and D8d): what kp_ui reads, built from the original files
 * (kp_ui_data_from_files: the converter's rules in C, scdata/kpf/kpd.py build_ui, build_fonts, build_strings,
 * build_sounds, build_portraits). Each part is the runtime's own layout: the dialogs' widget and SMK records (DLGS) and
 * their UTF-8 texts (POOL), the files by path, the colour rows, the TBLs in UTF-8, the sound records, the portrait rows;
 * the section names below are those of the containers the converter writes the languages' and the speech's overlays in
 * (kp_ui.h kp_ui_set_locale). The executable's tables (BTNS, STLY, MUSI, VOLT, ACCL, ESTB) are compiled in
 * (kp_defs_table).
 */
#ifndef KP_UI_DATA_H
#define KP_UI_DATA_H

#include "kp_dat.h"
#include "kp_files.h"
#include "kp_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KP_UI_FONT_COLOUR_ROWS 192 /* game\tFontGam.pcx: 24 rows of 8 (fonts.kpd TFGM) */
#define KP_UI_THEME_FONT_ROWS 48   /* a theme's tFont.pcx prefix (ui.kpd TFNT) */
#define KP_UI_TITLE_FONT_ROWS 40   /* glue\title\tFont.pcx: 5 rows of 8 (fonts.kpd TFNT) */

typedef struct {
	const uint8_t *data;
	size_t size;
} KpUiBytes;

/* A list of files by path (KpUiFile: its path, its bytes). */
typedef struct {
	KpUiFile *files;
	int count;
} KpUiFiles;

typedef struct KpUiData {
	const KpDefsProfile *profile; /* the data's: its compiled tables (kp_ui_data_table) */
	KpUiBytes dlgs, pool;        /* ui.kpd DLGS, POOL */
	KpUiFiles grps, pcx, glue, ghosts, vector_fonts; /* DGRP, PCXS, GFIL, GHST, VFNT */
	KpUiBytes theme_fonts;       /* TFNT: KP_UI_THEME_FONT_ROWS bytes per theme */
	KpUiBytes theme_effects;     /* TEFF: u16 size, then each theme's tEffect.pcx */
	KpUiBytes fonts[KP_UI_FNT_FONTS]; /* fonts.kpd F8..F16X: the decoded FNT and one NUL */
	KpUiBytes font_rows;         /* TFGM */
	/* fonts.kpd TFNT: the title screen's colour rows, which InitTitleScreen 0x4b5450 loads with title.pcx (GFIL) for
	 * its loading text (InitTitleLoadingText 0x4b5300, colour table 2; kp_ui_title.c). */
	KpUiBytes title_font_rows;
	KpUiBytes tbl[KP_UI_TBLS];   /* strings.kpd: STAT, NETW, GLUA, GHST, TIPS, HELP in UTF-8 */
	KpUiBytes sounds;            /* sounds.kpd SFX: u16 count, KP_UI_SOUND_RECORD bytes a sound */
	KpUiBytes sound_packs;       /* PACK: u16 count, a byte a sound (its pack + 1, 0 none) */
	KpUiBytes music_lengths;     /* MUSL */
	KpUiBytes portrait_rows;     /* portraits.kpd ROWS */
	KpUiFiles portraits;         /* FILE: the SMK files */
	/* each sfxdata.dat sound's file ("sound\" + its sfxdata.tbl entry; NULL none), which the app plays (ADR 0018,
	 * stage D5) */
	const char **sound_paths;
	uint32_t sound_path_count;
	/* what the parts point into */
	void **owned;
	int owned_count, owned_cap;
} KpUiData;

/* --- the dialogs from their BINs (kp_ui_dialogs.c; scdata/kpf/kpd.py dialogs_section) --- */

/* A text given to a record of a dialog's own that has none (kpd.koprulu_labels: the main menu's version label). */
typedef struct {
	int record;
	const char *text; /* UTF-8 */
} KpUiDialogLabel;

/* A dialog: its BIN, the records appended to it (a BIN whose root container is left out: NULL for none), its labels. */
typedef struct {
	const char *name;
	const uint8_t *bin;
	size_t bin_size;
	const uint8_t *extend;
	size_t extend_size;
	const KpUiDialogLabel *labels;
	int label_count;
} KpUiDialogSource;

/* DLGS (u16 count; per dialog u8 name length, name, u16 widgets, u16 SMK nodes, the KP_UI_WIDGET_SIZE records, the
 * 28-byte SMK node records: kpd.UI_SMK) and POOL (the texts in UTF-8, each with a NUL), malloc'd. */
typedef struct {
	uint8_t *dlgs, *pool;
	size_t dlgs_size, pool_size;
} KpUiDialogs;

/* The dialogs' records in the native traversal order of the loader's fixup 0x4c33e0 (dialog.py parse): 0, or -1 with
 * the dialog's name and the reason in err. */
int kp_ui_dialogs_build(const KpUiDialogSource *src, int count, KpUiDialogs *out, char *err, size_t err_size);
void kp_ui_dialogs_free(KpUiDialogs *d);
/* The files a BIN names (kpd.glue_file_paths): each image's (type 5) text, its hotkey's letter lowercased, and each
 * button's (type 14) SMK node names, in traversal order, through add (0, or -1 to stop): 0, or -1 with the reason. */
int kp_ui_bin_files(const uint8_t *bin, size_t size, int (*add)(void *ctx, const uint8_t *path, size_t n), void *ctx,
	char *err, size_t err_size);

/* The interface's art files the HD interface replaces (converter hd_ui.py sd_paths; ADR 0019, stage C6): each theme's
 * Dlg.grp, the dialogs' GRPs, the interface's GRPs and PCXs, then GFIL's files without the credits' (kpd.glue_file_paths
 * without has), the GRPs and PCXs among them, the first spelling of a path; through add (0, or -1 to stop with err
 * set). 0, or -1 with err. */
int kp_ui_data_art_paths(const KpFiles *f, int (*add)(void *ctx, const char *path, size_t n), void *ctx, KpError *err);
/* The parts built from the original files and the data set's DAT (the ghosts', sounds' and portraits' columns); the
 * vector fonts are the app's (NULL, 0: none; their bytes must outlive d). */
int kp_ui_data_from_files(KpUiData *d, const KpFiles *files, const KpDat *dat, const KpUiFile *vector_fonts,
	int vector_font_count, KpError *err);
void kp_ui_data_free(KpUiData *d);
/* The music's lengths (MUSL) read again from files (the old bytes stay d's until kp_ui_data_free): for tracks whose
 * files came after d was built (the web's music group, fetched after the start; ADR 0018). 0, or -1 with err. */
int kp_ui_data_music_lengths(KpUiData *d, const KpFiles *files, KpError *err);
/* A language's file for an original sound's path ("sound\\terran\\...wav", a scenario's "campaign\\...wav"; ADR 0019,
 * stage C2): Remastered's of the same name under the language's assets, "locales/<locale>/Assets/", its ".wav" (any
 * case) replaced by suffix, '/' apart. 0, or -1 when out is too small. */
int kp_ui_data_speech_path(const char *locale, const char *path, const char *suffix, char *out, size_t size);
/* A language's voices' records over d's SFX (the interface's talking durations): a malloc'd copy of d->sounds
 * (*size bytes, its u16 count first) in which each sound whose language file raw holds (kp_ui_data_speech_path with
 * ".wav", Remastered's names: kp_res_raw_files) is recorded from that file's header, *voices of them. A file that
 * cannot be read keeps the base's record, the first such named in err (err->msg "" none). NULL out of memory. */
uint8_t *kp_ui_data_speech_sounds(const KpUiData *d, const KpFiles *raw, const char *locale, size_t *size, int *voices,
	KpError *err);
/* A table of the executable's the interface reads (STLY, ESTB, VOLT, MUSI, ACCL, BTNS): the profile's compiled table;
 * NULL without it. */
const uint8_t *kp_ui_data_table(const KpUiData *d, const char *fourcc, uint32_t *size);
/* A files section (u16 count; per file u8 path length, the path, u32 size, the bytes) as a list pointing into data
 * (out->files malloc'd, also on failure): 0, or -1 when a record runs past the section. */
int kp_ui_files_section(const uint8_t *data, size_t size, KpUiFiles *out);
/* Track k of a MUSI table (u8 count; per track u8 length, the path, kind, fade-in, next): its path (NUL-terminated;
 * "" none) and kind; 0, or -1 past the tracks or for a malformed table or a path longer than path_size. */
int kp_ui_music_track(const uint8_t *musi, size_t size, int k, char *path, size_t path_size, int *kind);

/* The interface over its data (borrowed: d and the bytes it points into must outlive the interface): the dialogs, the
 * files, the fonts, the executable's tables, and the strings, sound records and portraits d has. NULL with the reason
 * in err. */
KpUi *kp_ui_new_data(const KpUiData *d, char *err, size_t err_size);
/* The parts of the sounds and the portraits (SFX with its u16 count, MUSL; ROWS with its u16 count, FILE's list, which
 * is copied): 0, or -1 with ui->error. */
int kp_ui_set_sound_records(KpUi *ui, const uint8_t *sfx, size_t size, const uint8_t *musl, size_t musl_size);
int kp_ui_set_portrait_parts(KpUi *ui, const uint8_t *rows, size_t rows_size, const KpUiFile *files, int count);

#ifdef __cplusplus
}
#endif

#endif
