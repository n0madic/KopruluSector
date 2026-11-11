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

typedef struct {
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

/* The parts from the containers' bytes (ui, fonts, strings, sounds, portraits; they must outlive d): 0, or -1 with the
 * reason. */
int kp_ui_data_from_kpf(KpUiData *d, const uint8_t *const kpd[5], const size_t sizes[5], KpError *err);
/* The parts built from the original files and the data set's DAT (the ghosts', sounds' and portraits' columns); the
 * vector fonts are the app's (NULL, 0: none; their bytes must outlive d). */
int kp_ui_data_from_files(KpUiData *d, const KpFiles *files, const KpDat *dat, const KpUiFile *vector_fonts,
	int vector_font_count, KpError *err);
void kp_ui_data_free(KpUiData *d);

#ifdef __cplusplus
}
#endif

#endif
