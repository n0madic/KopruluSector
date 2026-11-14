/*
 * The HD interface from a StarCraft: Remastered install (ADR 0019, stage C6): the art of the interface's SD files (the
 * dialogs' and the glue's GRPs and PCXs, the consoles) laid out frame for frame by the placement table
 * (defs/scr/hd-ui-<profile>.json, compiled as SHUI: kp_convert hd-ui-table's), for the runtime when it draws an HD
 * variant and for kp_convert repack, which writes it as ui.kpd (HDUI) into the variant's ui group; and the consoles'
 * pieces at one texel per logical pixel (CONW, console.kpd), which every kind of graphics composes the console from.
 *
 * A frame lies where the table says when it moved, else at its usual place (centred on its SD frame; a wireframe on
 * the GRP's canvas); a PCX's art is taken when the table names the PCX (the SD picture drawn again); the centre piece
 * of a console where the table puts it. An HD file whose content key (CASC's MD5 of its bytes) is not the table's is
 * left in SD: the table was made from other art (Remastered updated: kp_convert hd-ui-table makes it again).
 */
#ifndef KP_HD_UI_SCR_H
#define KP_HD_UI_SCR_H

#include <stddef.h>
#include <stdint.h>

#include "kp_files.h"
#include "kp_present.h"
#include "kp_res.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The placement table's compiled section (tools/defs_c.py SHUI) */
#define KP_HD_UI_SCR_TABLE "SHUI"
/* A console's centre piece is looked for this far (logical pixels) around its group's first column (kp_convert) */
#define KP_HD_UI_SCR_SEARCH 12
/* The repack's names (kp_convert repack): a variant's ui.kpd in its group (KP_HD_UI_SCR_GROUP, read by ranges: a
 * bundle carries the variants it draws), its head (its bytes up to FRMS's end, which the runtime opens it from) in the
 * core; the HD portraits' clips under their own names in a group of theirs (read by ranges); the consoles' art in the
 * core */
#define KP_HD_UI_SCR_GROUP "koprulu-ui-%s.mpq" /* the variant */
#define KP_HD_UI_SCR_PORTRAITS "koprulu-portraits.mpq"
#define KP_HD_UI_SCR_REPACK "koprulu\\ui\\%s.kpd"
#define KP_HD_UI_SCR_REPACK_HEAD "koprulu\\heads\\ui\\%s.kpd"
#define KP_HD_UI_SCR_REPACK_CONSOLE "koprulu\\console.kpd"

/* Where a frame's image is: its DDS in an HD file (the frame record's offset and size), or a console wing's tile cut
 * from the piece frame's image (side 1 the left piece's, 2 the right's). */
typedef struct {
	int32_t file;  /* KpHdUiScr.files */
	int side;      /* 0 the DDS's image */
	int32_t piece; /* a tile's piece frame */
	int race;      /* a console's (by CONW's order: zerg, terran, protoss), -1 none */
} KpHdUiScrSource;

#define KP_HD_UI_SCR_NAME 96
typedef struct {
	int variant, scale;
	KpHdUi index;                 /* over aset and frms: ui.kpd's records, offset and size those of a frame's DDS in
	                               * its HD file */
	uint8_t *head;                /* HEAD, ASET, FRMS (kp_hd_ui_scr_container writes them as they are) */
	size_t aset_at, frms_at;
	KpHdUiScrSource *sources;     /* per frame */
	char (*files)[KP_HD_UI_SCR_NAME]; /* the HD files' CASC names */
	int32_t file_count;
	KpResPlace install;           /* the install's place, a file's name set to read it (kp_res_reader_read) */
	int32_t skipped;              /* HD files left in SD: their content key not the table's */
	char first_skipped[KP_HD_UI_SCR_NAME];
} KpHdUiScr;

/* Whether the install holds a variant's HD interface (KP_HD_SCR_HD2, KP_HD_SCR_HD: its command icons). */
int kp_hd_ui_scr_held(KpRes *r, int variant);

/* What the HD interface makes of an SD path of kp_ui_data_art_paths (hd_ui.py generate's rules): none; a console
 * (*race); a replay's console (game\nconsole.pcx: Remastered's one picture, its file in file); a GRP's art, a PCX's (taken when it is the SD picture drawn again), a race's progress bar's (Remastered's
 * console picture: taken as it is), its HD file's CASC name in file (the variant's prefix, lowercase); a strip kept in
 * SD whatever Remastered holds (hd_ui.py SD_ONLY). */
enum { KP_HD_UI_SCR_NONE, KP_HD_UI_SCR_CONSOLE, KP_HD_UI_SCR_GRP, KP_HD_UI_SCR_PCX, KP_HD_UI_SCR_PICTURE,
	KP_HD_UI_SCR_KEPT_SD, KP_HD_UI_SCR_NEUTRAL };
int kp_hd_ui_scr_classify(const char *path, size_t len, int variant, char *file, size_t size, int *race);
/* A console's file in the install: its SD art (piece -1), its left, centre or right piece (0, 1, 2) of the variant. */
void kp_hd_ui_scr_console_file(int variant, int race, int piece, char *out, size_t size);
/* An SD portrait movie's HD clip in the install (HD2's, else HD's: try the variants in order), the variant's WebM by
 * its CASC name ("HD2/portrait/tmarine/tmafid00.webm"): 0, or -1 when it does not fit or the movie is not a portrait
 * (the glue's movies stay SD, though Remastered holds larger ones). */
int kp_hd_ui_scr_clip_name(const char *path, size_t len, int variant, char *out, size_t size);
/* A GRP frame's places for its HD image (w x h; a wireframe's layers side by side), from the SD frame's top-left in
 * HD pixels (hd_ui.py place_frame): centred on the SD frame (sx, sy, sw x sh), at the canvas's origin, centred on the
 * canvas (cw x ch); the usual one's index (a wireframe's on the canvas). */
enum { KP_HD_UI_SCR_ON_FRAME, KP_HD_UI_SCR_AT_ORIGIN, KP_HD_UI_SCR_ON_CANVAS, KP_HD_UI_SCR_PLACES };
int kp_hd_ui_scr_places(int32_t scale, int32_t sx, int32_t sy, int32_t sw, int32_t sh, int32_t cw, int32_t ch, int32_t w,
	int32_t h, int32_t places[KP_HD_UI_SCR_PLACES][2]);
/* The top-left in HD pixels, from an SD rectangle's (sw x sh), of an HD image (w x h) centred on it (rounded up and
 * left). */
void kp_hd_ui_scr_centred(int32_t scale, int32_t sw, int32_t sh, int32_t w, int32_t h, int32_t *x, int32_t *y);
/* A DDS file's image (BC1, BC3 or 32-bit RGBA) decoded to RGBA8: malloc'd *w x *h, or NULL. */
uint8_t *kp_hd_ui_scr_dds_rgba(const uint8_t *dds, size_t size, int32_t *w, int32_t *h);

/* A variant's layout from the install (hd_ui.py generate by the table): each SD file of the interface
 * (kp_ui_data_art_paths, through sd) with HD art the table takes, its frames' sizes and formats from their DDS headers
 * (read through h), their places. 0, or -1 with err (no table, an HD file unreadable). */
int kp_hd_ui_scr_build(KpRes *r, KpResReader *h, const KpFiles *sd, int variant, KpHdUiScr *out, KpError *err);
void kp_hd_ui_scr_free(KpHdUiScr *s);
/* A frame's image as ui.kpd keeps it before its PNG: its BC blocks (format KP_HD_UI_BC1, KP_HD_UI_BC3) or RGBA8 rows
 * (KP_HD_UI_PNG). Malloc'd (*size bytes), or NULL with err. */
uint8_t *kp_hd_ui_scr_image(const KpHdUiScr *s, KpResReader *h, int32_t frame, size_t *size, KpError *err);
/* A frame's RGBA8 texels (its image decoded): malloc'd w x h, or NULL with err. */
uint8_t *kp_hd_ui_scr_rgba(const KpHdUiScr *s, KpResReader *h, int32_t frame, KpError *err);
/* console.kpd (CONW) from the install's HD2 consoles and the SD consoles' PCXs (through sd): malloc'd, or NULL with
 * err. */
uint8_t *kp_hd_ui_scr_console(KpRes *r, KpResReader *h, const KpFiles *sd, size_t *size, KpError *err);
/* The consoles' art of the source: a repack's console.kpd (KP_HD_UI_SCR_REPACK_CONSOLE), else built from a Remastered
 * install's (kp_hd_ui_scr_console). Malloc'd, or NULL: err empty when the source has none (a 1.17 install, a repack
 * of one), else why it cannot be read or built. */
uint8_t *kp_hd_ui_scr_console_art(KpRes *r, const KpFiles *sd, size_t *size, KpError *err);

#ifdef __cplusplus
}
#endif

#endif
