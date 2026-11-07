/* The bitmap-font text engine (kp_ui_text.h; atlas P44 0x4cd100, P45 0x4cd670..0x4ce3e0, 0x4cfdf0). */
#include "kp_ui_text.h"
#include "kp_kpf.h"
#include "kp_text.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

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
	if (size < KP_FNT_HEADER || memcmp(font, "FONT", 4) != 0 || font[5] < font[4]
		|| size < KP_FNT_HEADER + 4 * (size_t)(font[5] - font[4] + 1)) {
		return -1;
	}
	t->font = font;
	t->font_size = size;
	t->supplement = NULL;
	{
		int k;
		for (k = 0; k < KP_UI_FNT_FONTS; k++) {
			if (t->supplements[k].font == font) t->supplement = &t->supplements[k];
		}
	}
	t->row = t->previous_row = 0;
	t->max_width = font[6];
	t->max_height = font[KP_FNT_MAX_HEIGHT];
	t->tab = (uint8_t)(2 * font[6]);
	t->gap = DEFAULT_GAP;
	t->flags = DEFAULT_FLAGS;
	t->spacing = DEFAULT_SPACING;
	t->clip_left = t->clip_top = 0;
	t->clip_right = t->clip_bottom = CLIP_MAX;
	return 0;
}

int32_t kp_ui_supplement_count(const uint8_t *data, size_t size)
{
	uint32_t count, k, previous = 0;
	if (data == NULL || size < KP_FNT_SUPPLEMENT_HEAD) return -1;
	count = kp_le_u32(data);
	if (count > (size - KP_FNT_SUPPLEMENT_HEAD) / KP_FNT_SUPPLEMENT_ENTRY) return -1;
	for (k = 0; k < count; k++) {
		const uint8_t *e = data + KP_FNT_SUPPLEMENT_HEAD + (size_t)k * KP_FNT_SUPPLEMENT_ENTRY;
		uint32_t cp = kp_le_u32(e), at = kp_le_u32(e + 4);
		if ((k > 0 && cp <= previous) || at > size || size - at < KP_FNT_GLYPH_HEAD) return -1;
		previous = cp;
	}
	return (int32_t)count;
}

int32_t kp_ui_supplement_code(const uint8_t *data, int32_t k)
{
	return (int32_t)kp_le_u32(data + KP_FNT_SUPPLEMENT_HEAD + (size_t)k * KP_FNT_SUPPLEMENT_ENTRY);
}

int kp_ui_text_add_supplement(KpUiText *t, const uint8_t *font, const uint8_t *data, size_t size)
{
	int k;
	if (kp_ui_supplement_count(data, size) < 0) return -1;
	for (k = 0; k < KP_UI_FNT_FONTS; k++) {
		KpUiSupplement *s = &t->supplements[k];
		if (s->font == NULL || s->font == font) {
			s->font = font;
			s->data = data;
			s->size = size;
			if (t->font == font) t->supplement = s;
			return 0;
		}
	}
	return -1;
}

int32_t kp_ui_text_take_missing(KpUiText *t)
{
	return t->missing_told < t->missing_count ? t->missing[t->missing_told++] : 0;
}

static void note_missing(KpUiText *t, int32_t cp)
{
	int k;
	for (k = 0; k < t->missing_count; k++) {
		if (t->missing[k] == cp) return;
	}
	if (t->missing_count < KP_UI_TEXT_MISSING) t->missing[t->missing_count++] = cp;
}

int64_t kp_ui_glyph(const KpUiText *t, int code)
{
	if (t->font == NULL || code < t->font[4] || code > t->font[5]) return -1;
	return (int64_t)kp_le_u32(t->font + KP_FNT_HEADER + 4 * (code - t->font[4]));
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

/* The glyph of a code point in the font's supplement, or NULL. */
static const uint8_t *supplement_glyph(const KpUiText *t, int32_t cp)
{
	const KpUiSupplement *s = t->supplement;
	uint32_t lo = 0, hi;
	if (s == NULL) return NULL;
	hi = kp_le_u32(s->data);
	while (lo < hi) {
		uint32_t mid = lo + (hi - lo) / 2;
		const uint8_t *e = s->data + KP_FNT_SUPPLEMENT_HEAD + (size_t)mid * KP_FNT_SUPPLEMENT_ENTRY;
		int32_t code = (int32_t)kp_le_u32(e);
		if (code == cp) return s->data + kp_le_u32(e + 4);
		if (code < cp) lo = mid + 1;
		else hi = mid;
	}
	return NULL;
}

/* The glyph of a code point: its FNT font's slot, else the supplement's (ADR 0016). */
static const uint8_t *glyph_of(const KpUiText *t, int32_t cp)
{
	int slot = kp_text_byte(cp);
	return slot >= 0 ? glyph_at(t, slot) : supplement_glyph(t, cp);
}

int kp_ui_has_glyph(const KpUiText *t, int32_t cp)
{
	int slot = kp_text_byte(cp);
	if (t->font == NULL) return 0;
	return slot >= 0 ? slot >= t->font[4] && slot <= t->font[5] : supplement_glyph(t, cp) != NULL;
}

/* The bytes a glyph's packets may take: to the end of the image it lies in. */
static size_t glyph_room(const KpUiText *t, const uint8_t *g)
{
	const KpUiSupplement *s = t->supplement;
	uintptr_t at = (uintptr_t)g;
	if (s != NULL && at >= (uintptr_t)s->data && at < (uintptr_t)s->data + s->size) {
		return s->size - (size_t)(at - (uintptr_t)s->data) - KP_FNT_GLYPH_HEAD;
	}
	return t->font_size - (size_t)(at - (uintptr_t)t->font) - KP_FNT_GLYPH_HEAD;
}

/* The advance of the character at text[*p] and *p past it. */
static int32_t char_step(const KpUiText *t, const uint8_t *text, size_t *p)
{
	const uint8_t *g;
	int c = text[*p];
	if (c < KP_TEXT_ASCII_END) {
		(*p)++;
		return kp_ui_char_width(t, c);
	}
	g = glyph_of(t, kp_text_next(text, KP_TEXT_NUL_TERMINATED, p));
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

/* TODO: lines of CJK text break without spaces (ADR 0016, out of scope for now). */
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
				size_t q = p;
				x += char_step(t, text, &q);
				if (x > right) break;
				p = q - 1; /* the loop's step */
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
				x += char_step(t, text, &p);
				t->x = x;
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
	int w, int h, KpUiGlyphLog *log);

/* --- the glyph log (not the original's: the vector text of large scales, ADR 0011) --- */

#define GLYPH_LOG_FIRST 256
#define GLYPH_SURFACES_FIRST 16
#define AT_SIDE 32             /* the glyph box log_glyph indexes directly; a larger glyph is wrapped (stays a bitmap) */
#define CLIP_OPEN 0x3fffffff   /* a side of a whole glyph's clip */

void kp_ui_glyph_log_free(KpUiGlyphLog *log)
{
	free(log->glyphs);
	free(log->surfaces);
	free(log->offsets);
	free(log->values);
	free(log->under);
	free(log->before);
	memset(log, 0, sizeof *log);
}

/* Room for one more glyph of up to `pixels` pixels; -1 (and the log stops) out of memory. */
static int log_reserve(KpUiGlyphLog *log, size_t pixels)
{
	if (log->count == log->capacity) {
		size_t n = log->capacity ? 2 * log->capacity : GLYPH_LOG_FIRST;
		KpUiGlyph *g = (KpUiGlyph *)realloc(log->glyphs, n * sizeof *g);
		if (g == NULL) return log->failed = 1, -1;
		log->glyphs = g;
		log->capacity = n;
	}
	if (log->pixel_count + pixels > log->pixel_capacity) {
		size_t n = log->pixel_capacity ? 2 * log->pixel_capacity : 64 * GLYPH_LOG_FIRST;
		int32_t *o;
		uint8_t *v, *u;
		while (n < log->pixel_count + pixels) n *= 2;
		o = (int32_t *)realloc(log->offsets, n * sizeof *o);
		if (o != NULL) log->offsets = o;
		v = (uint8_t *)realloc(log->values, n);
		if (v != NULL) log->values = v;
		u = (uint8_t *)realloc(log->under, n);
		if (u != NULL) log->under = u;
		if (o == NULL || v == NULL || u == NULL) return log->failed = 1, -1;
		log->pixel_capacity = n;
	}
	return 0;
}

static uint32_t find_surface(const KpUiGlyphLog *log, const uint8_t *pixels, int32_t stride)
{
	size_t k;
	for (k = 0; k < log->surface_count; k++) {
		if (log->surfaces[k].pixels == pixels && log->surfaces[k].stride == stride) return (uint32_t)k;
	}
	return KP_UI_GLYPH_NONE;
}

const KpUiGlyphSurface *kp_ui_glyph_surface(const KpUiGlyphLog *log, const uint8_t *pixels, int32_t stride)
{
	uint32_t k = find_surface(log, pixels, stride);
	return k == KP_UI_GLYPH_NONE ? NULL : &log->surfaces[k];
}

/* A surface's entry, made when it has none: KP_UI_GLYPH_NONE (and the log stops) out of memory. */
static uint32_t surface_of(KpUiGlyphLog *log, const uint8_t *pixels, int32_t stride)
{
	uint32_t k = find_surface(log, pixels, stride);
	KpUiGlyphSurface *sf;
	if (k != KP_UI_GLYPH_NONE) return k;
	if (log->surface_count == log->surface_capacity) {
		size_t n = log->surface_capacity ? 2 * log->surface_capacity : GLYPH_SURFACES_FIRST;
		KpUiGlyphSurface *grown = (KpUiGlyphSurface *)realloc(log->surfaces, n * sizeof *grown);
		if (grown == NULL) return log->failed = 1, KP_UI_GLYPH_NONE;
		log->surfaces = grown;
		log->surface_capacity = n;
	}
	sf = &log->surfaces[log->surface_count];
	sf->pixels = pixels;
	sf->stride = stride;
	sf->head = sf->tail = KP_UI_GLYPH_NONE;
	return (uint32_t)log->surface_count++;
}

/* Glyph k joins the end of its surface's chain. */
static void link_glyph(KpUiGlyphLog *log, uint32_t k)
{
	KpUiGlyphSurface *sf = &log->surfaces[log->glyphs[k].surface];
	log->glyphs[k].next = KP_UI_GLYPH_NONE;
	if (sf->tail == KP_UI_GLYPH_NONE) sf->head = k;
	else log->glyphs[sf->tail].next = k;
	sf->tail = k;
}

static void open_clip(int32_t clip[4])
{
	clip[0] = clip[1] = -CLIP_OPEN;
	clip[2] = clip[3] = CLIP_OPEN;
}

static void clip_to(int32_t clip[4], const int32_t r[4])
{
	if (r[0] > clip[0]) clip[0] = r[0];
	if (r[1] > clip[1]) clip[1] = r[1];
	if (r[2] < clip[2]) clip[2] = r[2];
	if (r[3] < clip[3]) clip[3] = r[3];
}

int kp_ui_glyph_live(const KpUiGlyphLog *log, const KpUiGlyph *g, const KpUiSurface *s)
{
	const KpUiGlyphSurface *sf = &log->surfaces[g->surface];
	uint32_t k;
	if (g->dead || s->pixels != sf->pixels || s->w != sf->stride || s->h <= 0
		|| (int64_t)g->end > (int64_t)s->w * (int64_t)s->h) {
		return 0;
	}
	for (k = g->first; k < g->first + g->count; k++) {
		if (s->pixels[log->offsets[k]] != log->values[k]) return 0;
	}
	return 1;
}

void kp_ui_glyph_log_prune(KpUiGlyphLog *log, uint32_t age)
{
	size_t k, kept = 0, pixels = 0, s, surfaces = 0;
	for (k = 0; k < log->count; k++) {
		KpUiGlyph g = log->glyphs[k];
		if (g.dead || log->serial - g.seen > age) continue;
		memmove(log->offsets + pixels, log->offsets + g.first, g.count * sizeof *log->offsets);
		memmove(log->values + pixels, log->values + g.first, g.count);
		memmove(log->under + pixels, log->under + g.first, g.count);
		g.first = (uint32_t)pixels;
		pixels += g.count;
		log->glyphs[kept++] = g;
	}
	log->count = kept;
	log->pixel_count = pixels;
	/* the surfaces that keep glyphs, renumbered in their order (the new number in tail), and their chains again */
	for (s = 0; s < log->surface_count; s++) log->surfaces[s].tail = KP_UI_GLYPH_NONE;
	for (k = 0; k < kept; k++) log->surfaces[log->glyphs[k].surface].tail = 0;
	for (s = 0; s < log->surface_count; s++) {
		if (log->surfaces[s].tail != KP_UI_GLYPH_NONE) log->surfaces[s].tail = (uint32_t)surfaces++;
	}
	for (k = 0; k < kept; k++) log->glyphs[k].surface = log->surfaces[log->glyphs[k].surface].tail;
	for (s = 0; s < log->surface_count; s++) {
		if (log->surfaces[s].tail != KP_UI_GLYPH_NONE) log->surfaces[log->surfaces[s].tail] = log->surfaces[s];
	}
	log->surface_count = surfaces;
	for (s = 0; s < surfaces; s++) log->surfaces[s].head = log->surfaces[s].tail = KP_UI_GLYPH_NONE;
	for (k = 0; k < kept; k++) link_glyph(log, (uint32_t)k);
	log->failed = 0;
}

/* The glyph just plotted at pen offset d (its pixels from `first` on) on the log's surface `surface`: kept with its
 * colours, and the earlier glyphs of the surface it wrote over are dead. */
static void log_glyph(const KpUiText *t, const KpUiSurface *s, int64_t d, const uint8_t *g, int32_t code, size_t first,
	uint32_t surface)
{
	KpUiGlyphLog *log = t->log;
	uint32_t index = (uint32_t)log->count, m;
	KpUiGlyph *n = &log->glyphs[index];
	const uint8_t *row = t->rows[t->row];
	int32_t at[AT_SIDE * AT_SIDE]; /* the new glyph's pixel at each place of its box, -1 none */
	int32_t bx, by, bw = g[0], bh = g[1];
	int direct = bw <= AT_SIDE && bh <= AT_SIDE;
	size_t k;
	memset(n, 0, sizeof *n);
	n->surface = surface;
	n->pen_y = (int32_t)(d >= 0 ? d / s->w : -((-d + s->w - 1) / s->w));
	n->pen_x = (int32_t)(d - (int64_t)n->pen_y * s->w);
	n->font = t->font;
	n->code = code;
	n->advance = (uint8_t)glyph_advance(t, g);
	memcpy(n->box, g, 4);
	n->fill = row[1];
	n->shadow = row[7];
	open_clip(n->clip);
	n->first = (uint32_t)first;
	n->count = (uint32_t)(log->pixel_count - first);
	n->seen = log->serial;
	bx = n->pen_x + g[2];
	by = n->pen_y + g[3];
	n->wrapped = bx < 0 || bx + bw > s->w || !direct;
	for (k = 0; k < AT_SIDE * AT_SIDE; k++) at[k] = -1;
	for (k = first; k < log->pixel_count; k++) {
		int32_t o = log->offsets[k], x = o % s->w - bx, y = o / s->w - by;
		if (o + 1 > n->end) n->end = o + 1;
		if (log->values[k] == row[7] && row[7] != row[1]) n->has_shadow = 1;
		if (x < 0 || y < 0 || x >= bw || y >= bh) n->wrapped = 1;
		else if (direct) at[y * AT_SIDE + x] = (int32_t)k;
	}
	/* the earlier glyphs of this surface under it are dead; where it wrote over one's intact pixel (text drawn again
	 * over itself without its background), what lay under that one lies under it */
	for (m = log->surfaces[surface].head; m != KP_UI_GLYPH_NONE; m = log->glyphs[m].next) {
		KpUiGlyph *o = &log->glyphs[m];
		int32_t ox = o->pen_x + o->box[2], oy = o->pen_y + o->box[3];
		uint8_t was_dead = o->dead;
		uint32_t j;
		if (was_dead || ox >= bx + bw || bx >= ox + o->box[0] || oy >= by + bh || by >= oy + o->box[1]) continue;
		for (j = o->first; j < o->first + o->count; j++) {
			int32_t x = log->offsets[j] % s->w - bx, y = log->offsets[j] / s->w - by, hit = -1;
			if (n->wrapped) {
				for (k = first; k < log->pixel_count; k++) {
					if (log->offsets[k] == log->offsets[j]) hit = (int32_t)k;
				}
			} else if (x >= 0 && y >= 0 && x < bw && y < bh) {
				hit = at[y * AT_SIDE + x];
			}
			if (hit < 0) continue;
			if (log->under[hit] == log->values[j]) log->under[hit] = log->under[j];
			o->dead = 1;
		}
	}
	log->count++;
	link_glyph(log, index);
}

/* Plots glyph g (of code point `code`) with its pen at the linear offset d: the original moves a pointer, so a pen past
 * a row's end lands on the next row. */
static void plot(const KpUiText *t, int64_t d, const uint8_t *g, int32_t code)
{
	KpUiSurface *s = t->target;
	KpUiGlyphLog *log = t->log;
	uint32_t surface = KP_UI_GLYPH_NONE;
	size_t first;
	if (s == NULL) return;
	if (log == NULL || log->failed || s->w <= 0 || log_reserve(log, (size_t)g[0] * g[1]) != 0
		|| (surface = surface_of(log, s->pixels, s->w)) == KP_UI_GLYPH_NONE) {
		(void)plot_linear(t, s, d + (int64_t)g[3] * s->w + g[2], g + 4, glyph_room(t, g),
			g[0], g[1], NULL);
		return;
	}
	first = log->pixel_count;
	(void)plot_linear(t, s, d + (int64_t)g[3] * s->w + g[2], g + 4, glyph_room(t, g), g[0],
		g[1], log);
	log_glyph(t, s, d, g, code, first, surface);
}

int kp_ui_glyph_log_blit_begin(KpUiGlyphLog *log, const KpUiSurface *src, const KpUiSurface *dst, const int32_t rect[4])
{
	int32_t w = rect[2] - rect[0], h = rect[3] - rect[1], y;
	size_t need = (size_t)(w > 0 ? w : 0) * (size_t)(h > 0 ? h : 0);
	int from = find_surface(log, src->pixels, src->w) != KP_UI_GLYPH_NONE;
	if (log->failed || need == 0 || (!from && find_surface(log, dst->pixels, dst->w) == KP_UI_GLYPH_NONE)) return 0;
	if (!from) return 1;
	if (need > log->before_size) {
		uint8_t *grown = (uint8_t *)realloc(log->before, need);
		if (grown == NULL) return log->failed = 1, 0;
		log->before = grown;
		log->before_size = need;
	}
	for (y = 0; y < h; y++) {
		memcpy(log->before + (size_t)y * (size_t)w, dst->pixels + (size_t)(rect[1] + y) * (size_t)dst->w + (size_t)rect[0],
			(size_t)w);
	}
	return 1;
}

/* Target glyph k under a blit whose source place plus (dx, dy) is its target place: the pixels the blit wrote over
 * leave it, those it blended over (the rule did not copy the source pixel) go to a part of their own in the blended
 * colours. */
static void blit_over(KpUiGlyphLog *log, const KpUiSurface *src, const KpUiSurface *dst, uint32_t k, int32_t dx,
	int32_t dy, const int32_t rect[4], const KpUiGlyphRule *rule)
{
	KpUiGlyph *g = &log->glyphs[k];
	int32_t ext[4] = { CLIP_OPEN, CLIP_OPEN, -CLIP_OPEN, -CLIP_OPEN }, box[4] = { CLIP_OPEN, CLIP_OPEN, -CLIP_OPEN,
		-CLIP_OPEN }, clip[4];
	int32_t gx = g->pen_x + g->box[2], gy = g->pen_y + g->box[3];
	uint32_t j, kept = 0, kept_inside = 0, blended = 0;
	int tint = -1;
	if (g->dead || gx >= rect[2] || gy >= rect[3] || gx + g->box[0] <= rect[0] || gy + g->box[1] <= rect[1]) {
		if (!g->wrapped || g->dead) return; /* a wrapped glyph's pixels may lie outside its box */
	}
	/* which of its pixels the blit left, wrote over or blended over */
	for (j = g->first; j < g->first + g->count; j++) {
		int32_t x = log->offsets[j] % dst->w, y = log->offsets[j] / dst->w;
		uint8_t v;
		if (x >= ext[2]) ext[2] = x + 1;
		if (x < ext[0]) ext[0] = x;
		if (y >= ext[3]) ext[3] = y + 1;
		if (y < ext[1]) ext[1] = y;
		if (x < rect[0] || y < rect[1] || x >= rect[2] || y >= rect[3]) {
			kept++;
			continue;
		}
		v = src->pixels[(size_t)(y - dy) * (size_t)src->w + (size_t)(x - dx)];
		if (rule->keyed && v == 0) kept++, kept_inside++;
		else if (rule->write(rule->ctx, v, log->values[j]) != v) blended++, tint = v;
	}
	if (kept == g->count) return;
	if (blended > 0 && log_reserve(log, blended) == 0) {
		uint32_t index = (uint32_t)log->count;
		KpUiGlyph *t = &log->glyphs[index];
		g = &log->glyphs[k];
		*t = *g;
		t->part = 1;
		if (!g->part) open_clip(t->clip);
		clip_to(t->clip, rect);
		t->fill = rule->write(rule->ctx, (uint8_t)tint, g->fill);
		t->shadow = rule->write(rule->ctx, (uint8_t)tint, g->shadow);
		t->first = (uint32_t)log->pixel_count;
		t->end = 0;
		for (j = g->first; j < g->first + g->count; j++) {
			int32_t o = log->offsets[j], x = o % dst->w, y = o / dst->w;
			uint8_t v;
			if (x < rect[0] || y < rect[1] || x >= rect[2] || y >= rect[3]) continue;
			v = src->pixels[(size_t)(y - dy) * (size_t)src->w + (size_t)(x - dx)];
			if ((rule->keyed && v == 0) || rule->write(rule->ctx, v, log->values[j]) == v) continue;
			log->offsets[log->pixel_count] = o;
			log->values[log->pixel_count] = dst->pixels[o];
			log->under[log->pixel_count++] = rule->write(rule->ctx, v, log->under[j]);
			if (o + 1 > t->end) t->end = o + 1;
		}
		t->count = (uint32_t)(log->pixel_count - t->first);
		log->count++;
		link_glyph(log, index);
	}
	/* it keeps the pixels the blit left */
	g = &log->glyphs[k];
	if (kept == 0) {
		g->dead = 1;
		return;
	}
	for (j = g->first; j < g->first + g->count; j++) {
		int32_t o = log->offsets[j], x = o % dst->w, y = o / dst->w;
		uint8_t v;
		if (x >= rect[0] && y >= rect[1] && x < rect[2] && y < rect[3]) {
			v = src->pixels[(size_t)(y - dy) * (size_t)src->w + (size_t)(x - dx)];
			if (!rule->keyed || v != 0) continue;
		}
		if (x < box[0]) box[0] = x;
		if (y < box[1]) box[1] = y;
		if (x + 1 > box[2]) box[2] = x + 1;
		if (y + 1 > box[3]) box[3] = y + 1;
	}
	{
		uint32_t to = g->first;
		for (j = g->first; j < g->first + g->count; j++) {
			int32_t o = log->offsets[j], x = o % dst->w, y = o / dst->w;
			if (x >= rect[0] && y >= rect[1] && x < rect[2] && y < rect[3]
				&& (!rule->keyed || src->pixels[(size_t)(y - dy) * (size_t)src->w + (size_t)(x - dx)] != 0)) {
				continue;
			}
			log->offsets[to] = o;
			log->values[to] = log->values[j];
			log->under[to++] = log->under[j];
		}
		g->count = kept;
	}
	/* its clip: cut at the blit's rectangle where that spans the glyph and took all its pixels inside, else its kept
	 * pixels' box */
	if (g->part) memcpy(clip, g->clip, sizeof clip);
	else open_clip(clip);
	if (kept_inside > 0) {
		clip_to(clip, box);
	} else if (rect[1] <= ext[1] && rect[3] >= ext[3] && (rect[0] <= ext[0] || rect[2] >= ext[2])) {
		if (rect[0] <= ext[0]) clip[0] = clip[0] > rect[2] ? clip[0] : rect[2];
		else clip[2] = clip[2] < rect[0] ? clip[2] : rect[0];
	} else if (rect[0] <= ext[0] && rect[2] >= ext[2] && (rect[1] <= ext[1] || rect[3] >= ext[3])) {
		if (rect[1] <= ext[1]) clip[1] = clip[1] > rect[3] ? clip[1] : rect[3];
		else clip[3] = clip[3] < rect[1] ? clip[3] : rect[1];
	} else {
		clip_to(clip, box);
	}
	memcpy(g->clip, clip, sizeof clip);
	g->part = 1;
}

/* Source glyph k comes along to the blit's target (its source place plus (dx, dy)) with the pixels the blit took. */
static void blit_from(KpUiGlyphLog *log, const KpUiSurface *src, const KpUiSurface *dst, uint32_t k, int32_t dx,
	int32_t dy, const int32_t rect[4], const KpUiGlyphRule *rule, uint32_t *target)
{
	KpUiGlyph *g = &log->glyphs[k], *n;
	int32_t w = rect[2] - rect[0];
	uint32_t j, inside = 0, index;
	if (g->dead || g->wrapped || !kp_ui_glyph_live(log, g, src)) return;
	for (j = g->first; j < g->first + g->count; j++) {
		int32_t x = log->offsets[j] % src->w + dx, y = log->offsets[j] / src->w + dy;
		inside += x >= rect[0] && y >= rect[1] && x < rect[2] && y < rect[3];
	}
	if (inside == 0) return;
	if (*target == KP_UI_GLYPH_NONE) *target = surface_of(log, dst->pixels, dst->w);
	if (*target == KP_UI_GLYPH_NONE || log_reserve(log, inside) != 0) return;
	index = (uint32_t)log->count;
	g = &log->glyphs[k];
	n = &log->glyphs[index];
	*n = *g;
	n->surface = *target;
	n->pen_x += dx;
	n->pen_y += dy;
	if (n->part) {
		n->clip[0] += dx;
		n->clip[1] += dy;
		n->clip[2] += dx;
		n->clip[3] += dy;
	}
	if (inside < g->count) {
		if (!n->part) open_clip(n->clip);
		n->part = 1;
		clip_to(n->clip, rect);
	}
	n->first = (uint32_t)log->pixel_count;
	n->end = 0;
	n->seen = log->serial;
	for (j = g->first; j < g->first + g->count; j++) {
		int32_t x = log->offsets[j] % src->w + dx, y = log->offsets[j] / src->w + dy, o;
		if (x < rect[0] || y < rect[1] || x >= rect[2] || y >= rect[3]) continue;
		o = y * dst->w + x;
		log->offsets[log->pixel_count] = o;
		log->values[log->pixel_count] = dst->pixels[o];
		log->under[log->pixel_count++] = rule->write(rule->ctx, log->under[j],
			log->before[(size_t)(y - rect[1]) * (size_t)w + (size_t)(x - rect[0])]);
		if (o + 1 > n->end) n->end = o + 1;
	}
	n->count = inside;
	log->count++;
	link_glyph(log, index);
}

void kp_ui_glyph_log_blit(KpUiGlyphLog *log, const KpUiSurface *src, const KpUiSurface *dst, int32_t sx, int32_t sy,
	const int32_t rect[4], const KpUiGlyphRule *rule)
{
	uint32_t target = find_surface(log, dst->pixels, dst->w), source = find_surface(log, src->pixels, src->w), k;
	uint32_t old = (uint32_t)log->count; /* the glyphs before the blit; the parts it makes join the chains' ends */
	int32_t dx = rect[0] - sx, dy = rect[1] - sy;
	if (log->failed) return;
	if (target != KP_UI_GLYPH_NONE) {
		for (k = log->surfaces[target].head; k != KP_UI_GLYPH_NONE && k < old; k = log->glyphs[k].next) {
			blit_over(log, src, dst, k, dx, dy, rect, rule);
		}
	}
	if (source == KP_UI_GLYPH_NONE || src->pixels == dst->pixels) return;
	for (k = log->surfaces[source].head; k != KP_UI_GLYPH_NONE; k = log->glyphs[k].next) {
		blit_from(log, src, dst, k, dx, dy, rect, rule, &target);
	}
}

size_t kp_ui_text_span(KpUiText *t, const uint8_t *text, size_t at, size_t end)
{
	int64_t stride = t->target ? t->target->w : 0;
	int64_t d = stride * t->y + t->x, saved_d = 0;
	int32_t saved_x = 0;
	int have_saved = 0;
	size_t p = at, next;
	for (; p < end; p = next) {
		int c = text[p];
		int32_t code = c;
		const uint8_t *g;
		int32_t advance, bottom;
		next = p + 1;
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
		if (c >= KP_TEXT_ASCII_END) {
			next = p;
			code = kp_text_next(text, end, &next);
			g = glyph_of(t, code);
			if (g == NULL && next - p > 1) { /* a character with no glyph: no width, a stray byte stops (ADR 0016) */
				note_missing(t, code);
				continue;
			}
		} else {
			g = glyph_at(t, c);
		}
		if (g == NULL) break;
		if (c == t->highlight && p != at && text[p - 1] == HIGHLIGHT_PREFIX) {
			saved_d = d;
			saved_x = t->x;
			have_saved = 1;
			advance = 0;
		} else {
			advance = glyph_advance(t, g);
			if (!t->measuring) plot(t, d, g, code);
		}
		bottom = t->y + g[3] + g[1] - 1;
		if (bottom >= t->ext_bottom) t->ext_bottom = (int16_t)bottom;
		t->x += advance;
		d += advance;
	}
	if (have_saved && !t->measuring) {
		const uint8_t *g = glyph_at(t, t->highlight);
		t->x = saved_x;
		if (g) plot(t, saved_d, g, kp_text_from_byte(t->highlight));
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

int32_t kp_ui_text_width(const KpUiText *t, const uint8_t *text)
{
	/* the measuring helpers 0x4ce4f8 / 0x4cde64: tab is the tab width */
	int32_t best = 0, line = 0;
	size_t p = 0;
	while (text[p] != 0) {
		if (text[p] == LF) {
			if (line > best) best = line;
			line = 0;
			p++;
			continue;
		}
		line += char_step(t, text, &p);
	}
	return line > best ? line : best;
}

size_t kp_ui_text_fit_length(const KpUiText *t, const uint8_t *text, int32_t max_width)
{
	int32_t w = 0;
	size_t p = 0;
	while (text[p] != 0) {
		size_t q = p;
		if (text[p] == LF) {
			w = 0;
			p++;
			continue;
		}
		w += char_step(t, text, &q);
		if (w > max_width) break;
		p = q;
	}
	return p;
}

#define PLOTTED_BATCH 1024 /* the pixels told to t->plotted at a time: a glyph's (each call reads the image log) */

static size_t plot_linear(const KpUiText *t, KpUiSurface *s, int64_t base, const uint8_t *packets, size_t avail,
	int w, int h, KpUiGlyphLog *log)
{
	int64_t pos = 0, total = (int64_t)w * h, limit = (int64_t)s->w * s->h;
	size_t at = 0, batched = 0;
	int32_t batch[PLOTTED_BATCH];
	const uint8_t *row = t->rows[t->row];
	if (w <= 0) return 0;
	while (pos < total && at < avail) {
		int value = packets[at++], skip = value >> 3, mask = value & 7;
		pos += skip;
		if (pos >= total) break;
		if (skip == 0 || mask != 0) {
			int64_t o = base + (pos / w) * s->w + pos % w;
			if (o >= 0 && o < limit) {
				if (log != NULL) { /* reserved for the glyph's w x h pixels */
					log->offsets[log->pixel_count] = (int32_t)o;
					log->under[log->pixel_count] = s->pixels[o];
					log->values[log->pixel_count++] = row[mask];
				}
				s->pixels[o] = row[mask];
				if (t->plotted != NULL) {
					batch[batched++] = (int32_t)o;
					if (batched == PLOTTED_BATCH) {
						t->plotted(t->plotted_ctx, s, batch, batched);
						batched = 0;
					}
				}
			}
			pos++;
		}
	}
	if (batched > 0) t->plotted(t->plotted_ctx, s, batch, batched);
	return at;
}

size_t kp_ui_plot_glyph(const KpUiText *t, KpUiSurface *s, int32_t x, int32_t y, const uint8_t *packets,
	size_t avail, int w, int h)
{
	return plot_linear(t, s, (int64_t)y * s->w + x, packets, avail, w, h, NULL);
}

void kp_ui_font_cap(const uint8_t *font, size_t size, int *top, int *height)
{
	KpUiText t;
	KpUiSurface s;
	uint8_t mask[32 * 32];
	int64_t off;
	int w, h, y, x, first = -1, last = -1;
	kp_ui_text_init(&t);
	*top = *height = 0;
	if (kp_ui_text_select_font(&t, font, size) != 0 || t.font == NULL) return;
	off = kp_ui_glyph(&t, 'H');
	if (off <= 0 || (size_t)off + 4 > size || font[off] > 32 || font[off + 1] > 32) return;
	w = font[off];
	h = font[off + 1];
	/* the glyph's masks (row k's entry is k): its body is the rows of mask 1, without the bevels and the shadow */
	for (y = 0; y < KP_UI_TEXT_ROWS && y < 8; y++) {
		for (x = 0; x < 8; x++) t.rows[y][x] = (uint8_t)x;
	}
	memset(mask, 0, sizeof mask);
	s.w = w;
	s.h = h;
	s.pixels = mask;
	(void)plot_linear(&t, &s, 0, font + off + 4, size - (size_t)off - 4, w, h, NULL);
	for (y = 0; y < h; y++) {
		for (x = 0; x < w; x++) {
			if (mask[y * w + x] != 1) continue;
			if (first < 0) first = y;
			last = y;
		}
	}
	if (first < 0) return;
	*top = font[off + 3] + first;
	*height = last - first + 1;
}
