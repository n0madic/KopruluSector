/* The bitmap-font text engine (kp_ui_text.h; atlas P44 0x4cd100, P45 0x4cd670..0x4ce3e0, 0x4cfdf0). */
#include "kp_ui_text.h"

#include <stdint.h>
#include <string.h>

#define FNT_HEADER 8
#define SPACE 0x20
#define TAB 0x09
#define LF 0x0a
#define FF 0x0c
#define CR 0x0d
#define RIGHT_REST 0x12
#define CENTRE_REST 0x13
#define SUB 0x1a
#define HIGHLIGHT_PREFIX 0x04
#define DEFAULT_GAP 3
#define DEFAULT_FLAGS 0x11
#define DEFAULT_SPACING 1
#define CLIP_MAX 0x7fff
#define EXTENT_MARGIN 2
#define MEASURE_FLAGS 0x68 /* CENTRE_IF_ONE | VCENTRE | 0x40 */

static uint32_t le32(const uint8_t *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* The row of each colour control code (SelectTextColor's jump table 0x4ce368), -1 for none. */
static int colour_row(int code)
{
	if (code >= 2 && code <= 7) return code - 2;
	if (code == 8) return 8;
	if (code >= 14 && code <= 17) return code - 5;
	if (code >= 21 && code <= 25) return code - 8;
	if (code >= 27 && code <= 30) return code - 9;
	if (code == 31) return 23;
	return -1;
}

/* The control codes DrawTextSpan treats as colour changes (index table 0x4cd9d0). */
static int is_colour_code(int code)
{
	return (code >= 1 && code <= 8) || (code >= 14 && code <= 17) || (code >= 21 && code <= 25)
		|| (code >= 27 && code <= 31);
}

/* x86 idiv's remainder: the dividend's sign. */
static int32_t idiv_rem(int32_t a, int32_t b)
{
	return a - (a / b) * b;
}

static int32_t next_tab(const KpUiText *t, int32_t x)
{
	int32_t step = t->tab;
	if (step == 0) return x; /* the original divides by zero; no stock font has a zero width */
	return x + step - idiv_rem(x - t->clip_left, step);
}

void kp_ui_text_init(KpUiText *t)
{
	memset(t, 0, sizeof *t);
	t->wrap = 1;
}

void kp_ui_text_set_rows(KpUiText *t, const uint8_t *bank, size_t n)
{
	memset(t->rows, 0, sizeof t->rows);
	if (n > sizeof t->rows) n = sizeof t->rows;
	memcpy(t->rows, bank, n);
}

int kp_ui_text_select_font(KpUiText *t, const uint8_t *font, size_t size)
{
	if (font == NULL) return 0;
	if (size < FNT_HEADER || memcmp(font, "FONT", 4) != 0 || font[5] < font[4]
		|| size < FNT_HEADER + 4 * (size_t)(font[5] - font[4] + 1)) {
		return -1;
	}
	t->font = font;
	t->font_size = size;
	t->row = t->previous_row = 0;
	t->max_width = font[6];
	t->max_height = font[7];
	t->tab = (uint8_t)(2 * font[6]);
	t->gap = DEFAULT_GAP;
	t->flags = DEFAULT_FLAGS;
	t->spacing = DEFAULT_SPACING;
	t->clip_left = t->clip_top = 0;
	t->clip_right = t->clip_bottom = CLIP_MAX;
	return 0;
}

int64_t kp_ui_glyph(const KpUiText *t, int code)
{
	if (t->font == NULL || code < t->font[4] || code > t->font[5]) return -1;
	return (int64_t)le32(t->font + FNT_HEADER + 4 * (code - t->font[4]));
}

/* The glyph's metrics, or NULL for a byte with no glyph (outside the font, offset 0, or past the image). */
static const uint8_t *glyph_at(const KpUiText *t, int code)
{
	int64_t off = kp_ui_glyph(t, code);
	if (off <= 0 || (size_t)off + 4 > t->font_size) return NULL;
	return t->font + off;
}

static int32_t glyph_advance(const KpUiText *t, const uint8_t *g)
{
	return g[2] + g[0] + t->spacing;
}

int32_t kp_ui_char_width(const KpUiText *t, int code)
{
	const uint8_t *g;
	if (code == SPACE) return t->max_width / 2;
	if (code == TAB) return t->tab;
	if (code >= 1 && code <= 31 && code != 11) return 0;
	g = glyph_at(t, code);
	return g ? glyph_advance(t, g) : 0;
}

void kp_ui_select_color(KpUiText *t, int code)
{
	int row;
	if (code == 1) {
		t->row = t->previous_row;
		return;
	}
	t->previous_row = t->row;
	row = colour_row(code);
	if (row >= 0) t->row = row;
}

size_t kp_ui_find_line_break(KpUiText *t, const uint8_t *text, size_t at)
{
	size_t p = at;
	int32_t x = t->x, right = t->clip_right;
	if (!t->wrap) {
		for (; text[p] != 0; p++) {
			int c = text[p];
			if (c == SPACE) x += t->max_width / 2;
			else if (c == TAB) x = next_tab(t, x);
			else if (c == LF || c == FF) {
				t->x = x;
				return p + 1;
			} else if (c == SUB || c == RIGHT_REST || c == CENTRE_REST) continue;
			else if (c == t->highlight && p != at && text[p - 1] == HIGHLIGHT_PREFIX) continue;
			else {
				x += kp_ui_char_width(t, c);
				if (x > right) break;
			}
			t->x = x;
		}
		t->x = x;
		return p;
	}
	while (text[p] != 0) {
		int c = text[p];
		if (c >= 1 && c <= 31 && is_colour_code(c)) { p++; continue; } /* byte map 0x4cd2ec: skipped */
		if (c == SPACE) { x += t->max_width / 2; t->x = x; p++; continue; }
		if (c == TAB) { x = next_tab(t, x); t->x = x; p++; continue; }
		if (c == LF || c == FF) return p + 1;
		if (c == SUB || c == CR || c == RIGHT_REST || c == CENTRE_REST) { p++; continue; }
		/* a word (0x4cd265): it fits whole or the line breaks before it */
		if (x > right) return p;
		{
			int32_t start_x = x;
			size_t start = p;
			while (text[p] != 0 && text[p] != SPACE && text[p] != TAB && text[p] != LF && text[p] != FF
				&& text[p] != SUB && text[p] != RIGHT_REST && text[p] != CENTRE_REST) {
				x += kp_ui_char_width(t, text[p]);
				t->x = x;
				p++;
			}
			if (x > right) {
				t->x = start_x;
				return start;
			}
		}
	}
	return p;
}

static size_t plot_linear(const KpUiText *t, KpUiSurface *s, int64_t base, const uint8_t *packets, size_t avail,
	int w, int h);

/* Plots glyph g with its pen at the linear offset d: the original moves a pointer, so a pen past a row's end lands
 * on the next row. */
static void plot(const KpUiText *t, int64_t d, const uint8_t *g)
{
	KpUiSurface *s = t->target;
	if (s == NULL) return;
	(void)plot_linear(t, s, d + (int64_t)g[3] * s->w + g[2], g + 4, t->font_size - (size_t)(g - t->font) - 4, g[0],
		g[1]);
}

size_t kp_ui_text_span(KpUiText *t, const uint8_t *text, size_t at, size_t end)
{
	int64_t stride = t->target ? t->target->w : 0;
	int64_t d = stride * t->y + t->x, saved_d = 0;
	int32_t saved_x = 0;
	int have_saved = 0;
	size_t p = at;
	for (; p < end; p++) {
		int c = text[p];
		const uint8_t *g;
		int32_t advance, bottom;
		if (c == 0 || c == FF) break;
		if (is_colour_code(c)) {
			if (!t->measuring && t->row != KP_UI_GREY_ROW) kp_ui_select_color(t, c);
			continue;
		}
		if (c == LF || c == CR || c == SUB) continue;
		if (c == TAB) {
			int32_t nx = next_tab(t, t->x);
			d += nx - t->x;
			t->x = nx;
			continue;
		}
		if (c == SPACE) {
			t->x += t->max_width / 2;
			d += t->max_width / 2;
			continue;
		}
		if (c == RIGHT_REST || c == CENTRE_REST) {
			int32_t xe;
			(void)kp_ui_find_line_break(t, text, p);
			xe = t->x;
			d += c == RIGHT_REST ? t->clip_right - xe : (t->clip_right - xe) >> 1;
			continue;
		}
		g = glyph_at(t, c);
		if (g == NULL) break;
		if (c == t->highlight && p != at && text[p - 1] == HIGHLIGHT_PREFIX) {
			saved_d = d;
			saved_x = t->x;
			have_saved = 1;
			advance = 0;
		} else {
			advance = glyph_advance(t, g);
			if (!t->measuring) plot(t, d, g);
		}
		bottom = t->y + g[3] + g[1] - 1;
		if (bottom >= t->ext_bottom) t->ext_bottom = (int16_t)bottom;
		t->x += advance;
		d += advance;
	}
	if (have_saved && !t->measuring) {
		const uint8_t *g = glyph_at(t, t->highlight);
		t->x = saved_x;
		if (g) plot(t, saved_d, g);
	}
	return p;
}

size_t kp_ui_layout_lines(KpUiText *t, const uint8_t *text, size_t at, int32_t *count)
{
	size_t p = at;
	t->ext_left = CLIP_MAX;
	t->ext_right = 0;
	t->ext_top = t->ext_bottom = (int16_t)t->y;
	*count = 1;
	if (text[p] != 0) {
		for (;;) {
			int32_t start = t->x, w;
			size_t e = kp_ui_find_line_break(t, text, p), stop;
			w = t->x - start;
			if (t->flags & KP_TEXT_CENTRE) {
				t->x = start + (t->clip_right - start) / 2 - w / 2;
				if (t->x + w >= t->ext_right) t->ext_right = (int16_t)(t->x + w);
			} else if (t->flags & KP_TEXT_RIGHT) {
				t->ext_right = t->clip_right;
				t->x = t->clip_left + t->clip_right - w - start;
			} else {
				int32_t line_end = t->x;
				t->x = start;
				if (line_end >= t->ext_right) t->ext_right = (int16_t)line_end;
			}
			if (t->x <= t->ext_left) t->ext_left = (int16_t)t->x;
			stop = kp_ui_text_span(t, text, p, e);
			t->x = t->clip_left;
			t->y += t->gap + t->max_height;
			t->ext_bottom = (int16_t)(t->ext_bottom + t->gap - 1);
			(*count)++;
			p = stop;
			if (text[p] == FF) return p + 1;
			if (!(t->y + t->max_height <= t->clip_bottom && text[p] != 0)) break;
		}
	}
	t->ext_bottom = (int16_t)(t->ext_bottom + 1 - t->gap);
	t->y -= t->gap;
	(*count)--;
	return p;
}

void kp_ui_align_vertically(const KpUiText *t, int32_t *y)
{
	int32_t remaining = t->clip_bottom - *y, used = t->y - *y;
	if (t->flags & KP_TEXT_VCENTRE) {
		remaining /= 2;
		used /= 2;
	}
	*y = *y - used + remaining;
}

size_t kp_ui_draw_text(KpUiText *t, int32_t x, int32_t y, const uint8_t *text)
{
	size_t p = 0;
	int32_t count = 0;
	uint8_t flags = t->flags;
	int16_t l, tp, r, b;
	if (t->font == NULL) return SIZE_MAX; /* no font selected (the original always has one): nothing drawn */
	if (t->clip_left < 0) t->clip_left = 0;
	if (t->clip_top < 0) t->clip_top = 0;
	if (t->target != NULL) {
		if (t->clip_right >= t->target->w) t->clip_right = (int16_t)(t->target->w - 1);
		if (t->clip_bottom >= t->target->h) t->clip_bottom = (int16_t)(t->target->h - 1);
	}
	t->ext_left = t->ext_right = (int16_t)x;
	t->ext_top = t->ext_bottom = (int16_t)y;
	if (x >= t->clip_left && y >= t->clip_top && x <= t->clip_right && y + t->max_height <= t->clip_bottom) {
		t->x = x;
		t->y = y;
		if (t->flags & MEASURE_FLAGS) {
			t->measuring = 1;
			(void)kp_ui_layout_lines(t, text, 0, &count);
			t->measuring = 0;
			if (!(t->flags & KP_TEXT_NO_VALIGN)) kp_ui_align_vertically(t, &y);
		}
		if (t->flags & KP_TEXT_CENTRE_IF_ONE) t->flags ^= count > 1 ? 0x09 : 0x0a;
		t->x = x;
		t->y = y;
		p = kp_ui_layout_lines(t, text, 0, &count);
		t->flags = flags;
	}
	l = (int16_t)(t->ext_left - EXTENT_MARGIN);
	tp = (int16_t)(t->ext_top - EXTENT_MARGIN);
	r = (int16_t)(t->ext_right + EXTENT_MARGIN);
	b = (int16_t)(t->ext_bottom + EXTENT_MARGIN);
	t->ext_left = l > t->clip_left ? l : t->clip_left;
	t->ext_top = tp > t->clip_top ? tp : t->clip_top;
	t->ext_right = r < t->clip_right ? r : t->clip_right;
	t->ext_bottom = b < t->clip_bottom ? b : t->clip_bottom;
	return text[p] == 0 ? SIZE_MAX : p;
}

/* The advance of a byte in the measuring helpers (0x4ce4f8 / 0x4cde64): tab is the tab width. */
static int32_t helper_width(const KpUiText *t, int c)
{
	return kp_ui_char_width(t, c);
}

int32_t kp_ui_text_width(const KpUiText *t, const uint8_t *text)
{
	int32_t best = 0, line = 0;
	size_t p;
	for (p = 0; text[p] != 0; p++) {
		if (text[p] == LF) {
			if (line > best) best = line;
			line = 0;
			continue;
		}
		line += helper_width(t, text[p]);
	}
	return line > best ? line : best;
}

size_t kp_ui_text_fit_length(const KpUiText *t, const uint8_t *text, int32_t max_width)
{
	int32_t w = 0;
	size_t p;
	for (p = 0; text[p] != 0; p++) {
		if (text[p] == LF) {
			w = 0;
			continue;
		}
		w += helper_width(t, text[p]);
		if (w > max_width) break;
	}
	return p;
}

static size_t plot_linear(const KpUiText *t, KpUiSurface *s, int64_t base, const uint8_t *packets, size_t avail,
	int w, int h)
{
	int64_t pos = 0, total = (int64_t)w * h, limit = (int64_t)s->w * s->h;
	size_t at = 0;
	const uint8_t *row = t->rows[t->row];
	if (w <= 0) return 0;
	while (pos < total && at < avail) {
		int value = packets[at++], skip = value >> 3, mask = value & 7;
		pos += skip;
		if (pos >= total) break;
		if (skip == 0 || mask != 0) {
			int64_t o = base + (pos / w) * s->w + pos % w;
			if (o >= 0 && o < limit) s->pixels[o] = row[mask];
			pos++;
		}
	}
	return at;
}

size_t kp_ui_plot_glyph(const KpUiText *t, KpUiSurface *s, int32_t x, int32_t y, const uint8_t *packets,
	size_t avail, int w, int h)
{
	return plot_linear(t, s, (int64_t)y * s->w + x, packets, avail, w, h);
}
