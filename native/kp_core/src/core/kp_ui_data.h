/*
 * The interface's data (ADR 0018, stage D3): what kp_ui reads, from the converter's containers (ui.kpd, fonts.kpd,
 * strings.kpd, sounds.kpd, portraits.kpd: kp_ui_data_from_kpf) or built from the original files
 * (kp_ui_data_from_files: the converter's rules in C, converter/kpf/kpd.py build_ui, build_fonts, build_strings,
 * build_sounds, build_portraits). Each part is the runtime's own layout: the dialogs' widget and SMK records (DLGS) and
 * their UTF-8 texts (POOL), the files by path, the colour rows, the TBLs in UTF-8, the sound records, the portrait rows.
 * The executable's tables (BTNS, STLY, MUSI, VOLT, ACCL, ESTB) are compiled in (kp_kpf_table).
 */
#ifndef KP_UI_DATA_H
#define KP_UI_DATA_H

#include "kp_dat.h"
#include "kp_files.h"
#include "kp_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The containers' kinds and schemas (converter/kpf/kpd.py): the base's, which a language's overlay must have too. */
#define KP_UI_DATA_UI_KIND "UI  "
#define KP_UI_DATA_UI_SCHEMA 4
#define KP_UI_DATA_FONTS_KIND "FONT"
#define KP_UI_DATA_FONTS_SCHEMA 1
#define KP_UI_DATA_STRINGS_KIND "STRS"
#define KP_UI_DATA_STRINGS_SCHEMA 1
#define KP_UI_DATA_SOUNDS_KIND "SNDS"
#define KP_UI_DATA_SOUNDS_SCHEMA 2
#define KP_UI_DATA_PORTRAITS_KIND "PORT"
#define KP_UI_DATA_PORTRAITS_SCHEMA 2

#define KP_UI_FONT_COLOUR_ROWS 192 /* game\tFontGam.pcx: 24 rows of 8 (fonts.kpd TFGM) */
#define KP_UI_THEME_FONT_ROWS 48   /* a theme's tFont.pcx prefix (ui.kpd TFNT) */

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
	KpUiBytes tbl[KP_UI_TBLS];   /* strings.kpd: STAT, NETW, GLUA, GHST, TIPS, HELP in UTF-8 */
	KpUiBytes sounds;            /* sounds.kpd SFX: u16 count, KP_UI_SOUND_RECORD bytes a sound */
	KpUiBytes sound_packs;       /* PACK: u16 count, a byte a sound (its pack + 1, 0 none) */
	KpUiBytes music_lengths;     /* MUSL */
	KpUiBytes portrait_rows;     /* portraits.kpd ROWS */
	KpUiFiles portraits;         /* FILE: the SMK files */
	/* kp_ui_data_from_files: each sfxdata.dat sound's file ("sound\" + its sfxdata.tbl entry; NULL none), which the
	 * app plays (ADR 0018, stage D5); NULL from the containers (their packs hold the sounds) */
	const char **sound_paths;
	uint32_t sound_path_count;
	/* what the parts point into */
	KpKpf kpf[5];
	int kpf_count;
	void **owned;
	int owned_count, owned_cap;
} KpUiData;

/* --- the dialogs from their BINs (kp_ui_dialogs.c; converter/kpf/kpd.py dialogs_section) --- */

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

/* The parts from the containers' bytes (ui, fonts, strings, sounds, portraits; they must outlive d; the last three may be
 * NULL, their parts then left out), each of its kind and schema: 0, or -1 with the reason. */
int kp_ui_data_from_kpf(KpUiData *d, const uint8_t *const kpd[5], const size_t sizes[5], KpError *err);
/* The parts built from the original files and the data set's DAT (the ghosts', sounds' and portraits' columns); the
 * vector fonts are the app's (NULL, 0: none; their bytes must outlive d). */
int kp_ui_data_from_files(KpUiData *d, const KpFiles *files, const KpDat *dat, const KpUiFile *vector_fonts,
	int vector_font_count, KpError *err);
void kp_ui_data_free(KpUiData *d);
/* A table of the executable's the interface reads (STLY, ESTB, VOLT, MUSI, ACCL, BTNS): ui.kpd's section of that name,
 * else the profile's compiled table (kp_kpf_table); NULL without either. */
const uint8_t *kp_ui_data_table(const KpUiData *d, const char *fourcc, uint32_t *size);
/* A files section (u16 count; per file u8 path length, the path, u32 size, the bytes) as a list pointing into data
 * (out->files malloc'd, also on failure): 0, or -1 when a record runs past the section. */
int kp_ui_files_section(const uint8_t *data, size_t size, KpUiFiles *out);
/* Track k of a MUSI table (u8 count; per track u8 length, the path, kind, fade-in, next): its path (NUL-terminated;
 * "" none) and kind; 0, or -1 past the tracks or for a malformed table or a path longer than path_size. */
int kp_ui_music_track(const uint8_t *musi, size_t size, int k, char *path, size_t path_size, int *kind);

/* The interface over its data (borrowed: d and the bytes it points into must outlive the interface): the dialogs, the
 * files, the fonts, the executable's tables, and the strings, sound records and portraits d has (kp_ui_set_strings,
 * kp_ui_set_sounds and kp_ui_set_portraits give them from the containers). NULL with the reason in err. */
KpUi *kp_ui_new_data(const KpUiData *d, char *err, size_t err_size);
/* The parts of the sounds and the portraits as kp_ui_set_sounds and kp_ui_set_portraits read them from their containers
 * (SFX with its u16 count, MUSL; ROWS with its u16 count, FILE's list, which is copied): 0, or -1 with ui->error. */
int kp_ui_set_sound_records(KpUi *ui, const uint8_t *sfx, size_t size, const uint8_t *musl, size_t musl_size);
int kp_ui_set_portrait_parts(KpUi *ui, const uint8_t *rows, size_t rows_size, const KpUiFile *files, int count);

#ifdef __cplusplus
}
#endif

#endif
