/* The original's bitmap-font text engine (font.cpp, vidblit.cpp; atlas P44 0x4cd100, P45 0x4cd670..0x4ce3e0,
 * 0x4cfdf0; docs/research/native-text.md), drawing into kp_ui's 8-bit surfaces.
 *
 * One state block, as the original's globals 0x6d3008..0x6d3114: the selected font, the colour rows, the layout
 * flags, the clip rectangle, the pen and the extents the last draw produced. Text is a NUL-terminated byte string;
 * functions return byte offsets into it where the original returns pointers. The Korean double-byte paths
 * (GetUserDefaultLangID 0x412) are not modelled: the profile is the English 1.17.0.1. */
#ifndef KP_UI_TEXT_H
#define KP_UI_TEXT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KP_UI_TEXT_ROWS 24  /* colour rows 0x6d3050..0x6d3108, 8 palette indices each */
#define KP_UI_GREY_ROW 3    /* 0x6d3068: locks colour changes while drawing */

/* Layout flags 0x6d300e. */
#define KP_TEXT_CENTRE 0x02
#define KP_TEXT_RIGHT 0x04
#define KP_TEXT_CENTRE_IF_ONE 0x08
#define KP_TEXT_NO_VALIGN 0x10
#define KP_TEXT_VCENTRE 0x20

/* An 8-bit surface: width is the stride (the original's bitmap {s16 w, s16 h, u8 *pixels}). */
typedef struct {
	int32_t w, h;
	uint8_t *pixels;
} KpUiSurface;

#define KP_UI_GLYPH_NONE UINT32_MAX /* no glyph (a chain's end) */

/* A glyph the text engine plotted while its glyph log is on (the vector text of large scales, ADR 0011, 2026-10-26):
 * where it went, what it is and the pixels it wrote with what lay under them, so the canvas can put the vector font's
 * glyph in its place while the surface keeps the original's pixels. A blit carries the glyphs of its source into its
 * target (kp_ui_glyph_log_blit); a record then may hold a part of its glyph's pixels, clipped to where they lie. */
typedef struct {
	uint32_t surface;        /* its surface's entry in the log's surfaces */
	uint32_t next;           /* the next glyph of its surface, in the order they were recorded */
	int32_t pen_x, pen_y;    /* the pen: the glyph cell's top-left in the surface */
	const uint8_t *font;
	int32_t code;            /* its code point (ADR 0016) */
	uint8_t advance;
	uint8_t box[4];          /* the glyph's bitmap as its FNT header: width, height, x and y from the pen */
	uint8_t fill, shadow;    /* the colour row's entries 1 (the body) and 7 (the drop shadow) */
	uint8_t has_shadow;      /* some pixel of mask 7 */
	uint8_t wrapped;         /* a pixel outside the glyph's box (a pen past a row's end): it stays a bitmap */
	uint8_t dead;            /* a later glyph or blit wrote over its pixels */
	uint8_t part;            /* a part of the glyph: the vector glyph is clipped to `clip` */
	int32_t clip[4];         /* a part's rectangle in the surface (x0, y0, x1, y1 exclusive) */
	int32_t end;             /* one past its last pixel's offset: the surface holds at least as many */
	uint32_t first, count;   /* its pixels in the log's arrays */
	uint32_t seen;           /* the last compose that found it live */
} KpUiGlyph;

/* A surface glyphs were recorded on: its pixels and stride, and the chain of its glyphs. */
typedef struct {
	const uint8_t *pixels;
	int32_t stride;
	uint32_t head, tail;     /* its first and last glyph (KP_UI_GLYPH_NONE: none) */
} KpUiGlyphSurface;

typedef struct KpUiGlyphLog {
	KpUiGlyph *glyphs;
	size_t count, capacity;
	KpUiGlyphSurface *surfaces;
	size_t surface_count, surface_capacity;
	int32_t *offsets;        /* each pixel's linear offset in its surface */
	uint8_t *values, *under; /* the index plotted and the one it replaced */
	size_t pixel_count, pixel_capacity;
	uint8_t *before;         /* a blit's target pixels before it (kp_ui_glyph_log_blit_begin) */
	size_t before_size;
	uint32_t serial;         /* the composes so far */
	int failed;              /* out of memory: the log stopped recording until the next prune */
} KpUiGlyphLog;

/* A blit's pixel rule: the index it writes for source index src over target index dst. `keyed`: a source index 0
 * leaves the target's pixel (the keyed and the single-colour blits). */
typedef struct {
	uint8_t (*write)(const void *ctx, uint8_t src, uint8_t dst);
	const void *ctx;
	int keyed;
} KpUiGlyphRule;

void kp_ui_glyph_log_free(KpUiGlyphLog *log);
/* The surface entry of pixels with that stride, NULL when no glyph was recorded on it. */
const KpUiGlyphSurface *kp_ui_glyph_surface(const KpUiGlyphLog *log, const uint8_t *pixels, int32_t stride);
/* A glyph is live while every pixel it wrote still holds its value in s, its surface as it is now (a surface too small
 * for its pixels, another one at a freed surface's address, holds none of them). */
int kp_ui_glyph_live(const KpUiGlyphLog *log, const KpUiGlyph *g, const KpUiSurface *s);
/* Drops the dead glyphs and those no compose has found live for `age` composes, compacts the arrays and lets a log
 * that ran out of memory record again. */
void kp_ui_glyph_log_prune(KpUiGlyphLog *log, uint32_t age);
/* Before a blit of src into dst's rect (x0, y0, x1, y1 exclusive, inside dst): whether it concerns the log's glyphs
 * (then kp_ui_glyph_log_blit follows it); dst's pixels there are kept when src has glyphs. */
int kp_ui_glyph_log_blit_begin(KpUiGlyphLog *log, const KpUiSurface *src, const KpUiSurface *dst, const int32_t rect[4]);
/* After that blit, of src's pixels from (sx, sy) on by a rule: dst's glyphs the blit wrote over lose those pixels,
 * those it blended over (a source pixel the rule did not copy) keep them as a part in the blended colours; src's live
 * glyphs come along as dst's (parts when the blit takes some of their pixels), what lay under them blitted alike. */
void kp_ui_glyph_log_blit(KpUiGlyphLog *log, const KpUiSurface *src, const KpUiSurface *dst, int32_t sx, int32_t sy,
	const int32_t rect[4], const KpUiGlyphRule *rule);
/* The cap height of an FNT font: the first row and the rows of its 'H' glyph's body (mask 1, without the bevels and
 * the drop shadow) from the pen (0, 0 without one). */
void kp_ui_font_cap(const uint8_t *font, size_t size, int *top, int *height);

/* Not the original's: told of the pixels a glyph plotted on s (their linear offsets), a batch at a time (the HD
 * interface's image log, ADR 0013 R4d: a glyph's pixels show the glyph, not the art under it). */
typedef void (*KpUiTextPlotted)(void *ctx, const KpUiSurface *s, const int32_t *offsets, size_t count);

#define KP_UI_FNT_FONTS 5  /* the FNT font files: font8, font10, font14, font16, font16x */
/* An FNT image: "FONT", the first and last code, the widest and tallest glyph, u32 offsets; a glyph: width, height, x,
 * y, packets. A glyph supplement: u32 count, entries of u32 code point and u32 offset, glyphs (below). */
#define KP_FNT_HEADER 8
#define KP_FNT_MAX_HEIGHT 7 /* the tallest glyph's byte: the cell a line of text takes */
#define KP_FNT_GLYPH_HEAD 4
#define KP_FNT_SUPPLEMENT_HEAD 4
#define KP_FNT_SUPPLEMENT_ENTRY 8
#define KP_UI_TEXT_MISSING 16 /* the code points with no glyph kept for one log line each */

/* A font's glyph supplement (ADR 0016; kp_fnt_supplement, built by kp_locale.c): u32 count, count pairs of u32 code point
 * (ascending) and u32 offset from its start, the glyphs in FNT's form (width, height, x, y, packets). */
typedef struct {
	const uint8_t *font; /* the FNT image it goes with */
	const uint8_t *data;
	size_t size;
} KpUiSupplement;

typedef struct {
	const uint8_t *font;     /* 0x6d3020: an FNT image (offsets relative to it); NULL none */
	size_t font_size;
	const KpUiSupplement *supplement; /* not the original's: the font's, NULL none */
	KpUiSupplement supplements[KP_UI_FNT_FONTS];
	int32_t missing[KP_UI_TEXT_MISSING]; /* code points drawn with no glyph, each once */
	int missing_count, missing_told;
	uint8_t rows[KP_UI_TEXT_ROWS + 1][8]; /* 0x6d3050..; the last is DrawTextWithOpacity's 0x6d06c8 */
	int row, previous_row;   /* 0x6d3010, 0x6d3110 as row indexes */
	int measuring;           /* 0x6d300d */
	int wrap;                /* 0x50f160 */
	uint8_t flags;           /* 0x6d300e */
	uint8_t tab;             /* 0x6d300f */
	uint8_t gap, spacing;    /* 0x6d3034, 0x6d3035 */
	uint8_t max_width, max_height; /* 0x6d3036, 0x6d3037 */
	int16_t clip_left, clip_top, clip_right, clip_bottom;   /* 0x6d3038.. inclusive */
	int16_t ext_left, ext_top, ext_right, ext_bottom;       /* 0x6d3040.. */
	int32_t x, y;            /* 0x6d3048, 0x6d304c */
	uint8_t highlight;       /* 0x6d3114 */
	KpUiSurface *target;     /* 0x6d0c40 */
	KpUiGlyphLog *log;       /* not the original's: the plotted glyphs recorded (NULL: off) */
	KpUiTextPlotted plotted; /* not the original's: told of the plotted pixels (NULL: none) */
	void *plotted_ctx;
} KpUiText;

/* The state at load: no font, row 0, the wrap mode on (0x50f160 = 1), the highlight glyph 0. */
void kp_ui_text_init(KpUiText *t);
/* The colour rows from a bank of n bytes (tFontGam.pcx's 192, a glue theme's tFont.pcx prefix); the rest is 0. */
void kp_ui_text_set_rows(KpUiText *t, const uint8_t *bank, size_t n);
/* SelectFont 0x4ce160: font, row = previous = 0, maxima, tab 2 x max width, gap 3, flags 0x11, spacing 1, clip
 * {0, 0, 0x7fff, 0x7fff}. NULL keeps everything. Returns -1 for a font that is not an FNT image. */
int kp_ui_text_select_font(KpUiText *t, const uint8_t *font, size_t size);
/* The glyph supplement of the FNT image font, the bytes outliving t: 0, or -1 when they are malformed (none taken). */
int kp_ui_text_add_supplement(KpUiText *t, const uint8_t *font, const uint8_t *data, size_t size);
/* A supplement's code points: their count (-1: malformed), code k through kp_ui_supplement_code. */
int32_t kp_ui_supplement_count(const uint8_t *data, size_t size);
int32_t kp_ui_supplement_code(const uint8_t *data, int32_t k);
/* The next code point drawn with no glyph that was not told yet, 0 when none: a log line for each (ADR 0016). */
int32_t kp_ui_text_take_missing(KpUiText *t);
/* GetGlyph 0x4cde90: the glyph's offset in the font (the font base, 0, for space), or -1 outside the font. */
int64_t kp_ui_glyph(const KpUiText *t, int code);
/* GetCharWidth 0x4cdc30. */
int32_t kp_ui_char_width(const KpUiText *t, int code);
/* The selected font has a glyph for the code point (0x4cde90's range check). */
int kp_ui_has_glyph(const KpUiText *t, int32_t cp);
/* SelectTextColor 0x4ce250. */
void kp_ui_select_color(KpUiText *t, int code);
/* FindLineBreak 0x4cd100 from text + at: the break offset; updates x. */
size_t kp_ui_find_line_break(KpUiText *t, const uint8_t *text, size_t at);
/* DrawTextSpan 0x4cd670 over text[at, end): plots unless measuring; returns where it stopped. */
size_t kp_ui_text_span(KpUiText *t, const uint8_t *text, size_t at, size_t end);
/* LayoutTextLines 0x4cd9f0 from text + at: the stop offset and the line count. */
size_t kp_ui_layout_lines(KpUiText *t, const uint8_t *text, size_t at, int32_t *count);
/* AlignTextVertically 0x4cdbf0. */
void kp_ui_align_vertically(const KpUiText *t, int32_t *y);
/* DrawText 0x4cdf20 at (x, y) into the target: returns the unconsumed offset, or SIZE_MAX when the text was consumed
 * up to its NUL (the original's 0); the extents get their 2-pixel margin. */
size_t kp_ui_draw_text(KpUiText *t, int32_t x, int32_t y, const uint8_t *text);
/* GetTextWidth 0x4ce3e0: the widest LF-separated line. */
int32_t kp_ui_text_width(const KpUiText *t, const uint8_t *text);
/* GetTextFitLength 0x4cdd30: the bytes that fit max_width. */
size_t kp_ui_text_fit_length(const KpUiText *t, const uint8_t *text, int32_t max_width);
/* PlotGlyphPackets 0x4cfdf0: a w x h glyph's packets at (x, y) of the surface with the current colour row; returns
 * the packet bytes consumed. Pixels outside the surface are not written (the original does not clip; its callers
 * keep glyphs inside). */
size_t kp_ui_plot_glyph(const KpUiText *t, KpUiSurface *s, int32_t x, int32_t y, const uint8_t *packets,
	size_t avail, int w, int h);

#ifdef __cplusplus
}
#endif

#endif
