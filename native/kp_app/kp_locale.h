/*
 * A language's texts (ADR 0016; ADR 0019, stage C3), built from its own files where --data's source holds them (an
 * install's locales/<locale>/Assets/, a repack's koprulu-locale-<locale>.mpq: Remastered's names, kp_res_raw_files) over
 * the interface's base (KpUiData), by the rules the Python converter's emit/locale.py had until this replaced it:
 * - the six tables (KpUiData.tbl) in UTF-8 with u32 offsets, each row the language's XML's (stat_txt's
 *   by position over Remastered's own TBL's rows, the others by the manifest's ids: the profile's STBL);
 * - the dialogs' texts (DLGS, POOL): each original dialog rebuilt from the profile's SBIN with the language's strings,
 *   its records the base's but for their texts and the rectangles defs/l10n/locales.json's `layout` gives; our own
 *   dialogs' and records' texts from defs/l10n/koprulu-<locale>.json;
 * - the scripts of the credits and the epilogues (rez\*.txt);
 * - the movies' subtitles checked (the player reads each movie's .srt itself: KP_LOCALE_SUBTITLES);
 * - a campaign mission's strings (kp_locale_mission);
 * - the glyphs of the FNT fonts' supplements and the vector fonts (kp_locale_glyphs).
 * locales.json's rules are compiled in (tools/defs_c.py: LROW, LEXC, LTPL, LKEP, LTXT, LLAY, LSTR, LOWN). A text that
 * fails a check (UTF-8, the base's printf conversions and NULs, no control byte the base does not use, no Remastered
 * substitution, a button's form, an ASCII hotkey) keeps the base's and is named in the problems; a part that does not
 * fit the base at all (another row count, another dialog) stays the base's whole. kp_convert locale-check lists them.
 */
#ifndef KP_LOCALE_H
#define KP_LOCALE_H

#include <stddef.h>
#include <stdint.h>

#include "kp_data.h"
#include "kp_files.h"
#include "kp_mission.h"
#include "kp_ui_data.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KP_LOCALE_BASE "enUS"                             /* the language of the base's text */
#define KP_LOCALE_ID 8                                    /* "ruRU" and its NUL */
#define KP_LOCALE_PROBE "locales/%s/Assets/rez/RC3.xml"   /* a language's strings are there when this is */
#define KP_LOCALE_SUBTITLES "locales/%s/Assets/Smk/%s.srt" /* a movie's, its name in lower case */

/* What a build found: the problems (each a malloc'd line), the code points the language's text uses (each with where
 * it was first seen), the rows that differ from the base's in their conversions by an exception, the texts mended. */
typedef struct {
	char **problems;
	int problem_count, problem_cap;
	int32_t *codes;
	char **code_where;
	int code_count, code_cap;
	uint8_t *seen; /* a bit per code point noted */
	char **exceptions;
	int exception_count, exception_cap;
	char **mended;
	int mended_count, mended_cap;
} KpLocaleFound;

typedef struct {
	int rows, translated, shorter;
	size_t bytes;
} KpLocaleTableReport;

typedef struct {
	char locale[KP_LOCALE_ID];
	/* the six tables (KpUiData.tbl's order) with u32 offsets, malloc'd; NULL: the base's */
	uint8_t *tbl[KP_UI_TBLS];
	size_t tbl_size[KP_UI_TBLS];
	KpLocaleTableReport tables[KP_UI_TBLS];
	uint8_t *dlgs, *pool; /* NULL: the base's */
	size_t dlgs_size, pool_size;
	int dialogs_translated, dialogs_kept, dialogs_laid_out;
	KpUiFile *scripts;    /* the language's rez\*.txt, by the base's paths, each malloc'd */
	int script_count;
	int scripts_left;     /* the base's scripts the language has none of */
	int subtitles;        /* the movies whose subtitles read */
	KpLocaleFound found;
} KpLocale;

/* Whether raw holds the language's strings (KP_LOCALE_PROBE). */
int kp_locale_held(const KpFiles *raw, const char *locale);
/* The language over the base d (its profile's rules): 0, the parts that do not fit kept the base's and named in
 * l->found; -1 with err only out of memory or for a locale that is not a language of the profile's other than the base
 * (l emptied). */
int kp_locale_build(KpLocale *l, const KpUiData *d, const KpFiles *raw, const char *locale, KpError *err);
void kp_locale_free(KpLocale *l);

/* A campaign mission's strings in the language (`mission` "terran/terran01", its map KP_HOST_SPEECH_MAP read through
 * raw): the base's STR (m's) with the language's text in each row but those looked up by their text (the sounds'
 * paths, Set Next Scenario's map name) and locales.json's `strings`; a row that fails a check keeps the base's (named
 * in found). 1 and *strs (malloc'd, *size bytes), 0 when raw has no such map, -1 with err when it is damaged or its
 * strings do not fit the base's (another count, past STR's 16-bit offsets). counts: translated, kept, looked up (may
 * be NULL). */
int kp_locale_mission(const KpFiles *raw, const char *locale, const char *mission, const KpMission *m, KpLocaleFound *found,
	uint8_t **strs, size_t *size, int counts[3], KpError *err);

/* Remastered's TrueType pair the glyphs are made from (raw's names): the regular font, the wide font16x's. */
extern const char *const KP_LOCALE_FONTS[KP_UI_VECTOR_FONTS];

/* The glyphs for the FNT fonts and the vector fonts: Remastered's TrueType pair (KP_LOCALE_FONTS) read through raw,
 * each FNT font's supplement (kp_fnt_supplement) of the code points of the languages' code pages, of the languages'
 * names and of what l's text uses (NULL: none), but the ones with a byte slot of their own (an untrue slot of
 * windows-1252's 0x80..0xa0 aside) and those either font lacks. */
typedef struct {
	uint8_t *supplements[KP_UI_FNT_FONTS];
	size_t supplement_sizes[KP_UI_FNT_FONTS];
	uint8_t *ttf[KP_UI_VECTOR_FONTS];
	size_t ttf_size[KP_UI_VECTOR_FONTS];
	KpUiFile vector_fonts[KP_UI_VECTOR_FONTS]; /* VFNT's names, over ttf */
	int code_count;
	int32_t *dropped; /* the code points either TrueType font lacks */
	int dropped_count;
} KpLocaleGlyphs;
/* 1 built, 0 when raw holds no TrueType pair (no glyphs: the base's text alone), -1 with err (out of memory, a font
 * that does not rasterize). A code point l uses that neither a slot nor the set holds is a problem in found (may be
 * NULL), as is a slot of an FNT font with no glyph for a character l uses. */
int kp_locale_glyphs(KpLocaleGlyphs *g, const KpUiData *d, const KpFiles *raw, const KpLocale *l, KpLocaleFound *found,
	KpError *err);
void kp_locale_glyphs_free(KpLocaleGlyphs *g);
/* Each code point `used` notes (its texts' place for each) that is drawn by neither an FNT slot of its own nor g's
 * supplements is a problem in found, as is one whose slot has no glyph in an FNT font: kp_locale_glyphs' check of
 * l's text, which kp_convert locale-check makes of the missions' strings too. */
void kp_locale_glyphs_cover(const KpLocaleGlyphs *g, const KpUiData *d, const KpLocaleFound *used, KpLocaleFound *found);

/* The files of d's raw the language is built from, as kp_locale_held, kp_locale_build, kp_locale_mission (each campaign
 * mission of d's catalog) and kp_locale_glyphs read them, by raw's names: add(ctx, name) for each one raw holds (a
 * name may come more than once; add returns 0, or -1 to stop). The TrueType fonts are the base's text's too. 0, or -1
 * with err (add's stop: "out of memory"). */
typedef int (*KpLocaleAdd)(void *ctx, const char *name);
int kp_locale_names(const KpData *d, const char *locale, KpLocaleAdd add, void *ctx, KpError *err);

/* The language's texts and glyphs as the interface takes them (kp_ui_set_locale; either may be NULL); speech as
 * KpUiLocale's. */
void kp_locale_to_ui(const KpLocale *l, const KpLocaleGlyphs *g, KpUiLocale *out);

void kp_locale_found_free(KpLocaleFound *f);

#ifdef __cplusplus
}
#endif

#endif
