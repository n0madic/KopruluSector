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

/* A glyph the text engine plotted while its glyph log is on (the vector text of large scales, ADR 0011, 2026-10-26):
 * where it went, what it is and the pixels it wrote with what lay under them, so the canvas can put the vector font's
 * glyph in its place while the surface keeps the original's pixels. */
typedef struct {
	const uint8_t *pixels;   /* the surface's pixels and stride it was plotted into */
	int32_t stride;
	int32_t pen_x, pen_y;    /* the pen: the glyph cell's top-left in the surface */
	const uint8_t *font;
	uint8_t code, advance;
	uint8_t box[4];          /* the glyph's bitmap as its FNT header: width, height, x and y from the pen */
	uint8_t fill, shadow;    /* the colour row's entries 1 (the body) and 7 (the drop shadow) */
	uint8_t has_shadow;      /* some pixel of mask 7 */
	uint8_t wrapped;         /* a pixel outside the glyph's box (a pen past a row's end): it stays a bitmap */
	uint8_t dead;            /* a later glyph wrote over its pixels */
	uint32_t first, count;   /* its pixels in the log's arrays */
	uint32_t seen;           /* the last compose that found it live */
} KpUiGlyph;

typedef struct KpUiGlyphLog {
	KpUiGlyph *glyphs;
	size_t count, capacity;
	int32_t *offsets;        /* each pixel's linear offset in its surface */
	uint8_t *values, *under; /* the index plotted and the one it replaced */
	size_t pixel_count, pixel_capacity;
	uint32_t serial;         /* the composes so far */
	int failed;              /* out of memory: the log stopped recording */
} KpUiGlyphLog;

void kp_ui_glyph_log_free(KpUiGlyphLog *log);
/* A glyph is live while every pixel it wrote still holds its value. */
int kp_ui_glyph_live(const KpUiGlyphLog *log, const KpUiGlyph *g);
/* Drops the dead glyphs and those no compose has found live for `age` composes; compacts the arrays. */
void kp_ui_glyph_log_prune(KpUiGlyphLog *log, uint32_t age);
/* The cap height of an FNT font: its 'H' glyph's top row and height from the pen (0, 0 without one). */
void kp_ui_font_cap(const uint8_t *font, size_t size, int *top, int *height);

typedef struct {
	const uint8_t *font;     /* 0x6d3020: an FNT image (offsets relative to it); NULL none */
	size_t font_size;
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
} KpUiText;

/* The state at load: no font, row 0, the wrap mode on (0x50f160 = 1), the highlight glyph 0. */
void kp_ui_text_init(KpUiText *t);
/* The colour rows from a bank of n bytes (tFontGam.pcx's 192, a glue theme's tFont.pcx prefix); the rest is 0. */
void kp_ui_text_set_rows(KpUiText *t, const uint8_t *bank, size_t n);
/* SelectFont 0x4ce160: font, row = previous = 0, maxima, tab 2 x max width, gap 3, flags 0x11, spacing 1, clip
 * {0, 0, 0x7fff, 0x7fff}. NULL keeps everything. Returns -1 for a font that is not an FNT image. */
int kp_ui_text_select_font(KpUiText *t, const uint8_t *font, size_t size);
/* GetGlyph 0x4cde90: the glyph's offset in the font (the font base, 0, for space), or -1 outside the font. */
int64_t kp_ui_glyph(const KpUiText *t, int code);
/* GetCharWidth 0x4cdc30. */
int32_t kp_ui_char_width(const KpUiText *t, int code);
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
