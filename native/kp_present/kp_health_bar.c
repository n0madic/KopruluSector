/*
 * Health, shield, energy and timed-life bars (draw method 11, DrawHealthBar 0x464d20, docs/research/
 * native-health-bars.md): the original's segment arithmetic and line drawing, and the texture the world's bar images
 * read (every drawn bar gets a slot of KP_BAR_ROWS rows; the texel holds the palette index and whether the original
 * writes it there).
 *
 * The bar is the frame of a health-bar image: `width` columns (sprites.dat health bar), 5 hit point rows (7 with
 * shields, the shields over the top 4), and the energy or timer bar 6 (8) rows below. Colours index thpbar.pcx
 * (0x66cb5c, 19 entries). The original draws a part only when the visible crop holds all its rows.
 */
#include "kp_present.h"

#include <string.h>

#include "kp_game.h"

#define SHIELD_ROWS 4
#define ENERGY_OFFSET 6
#define SHIELD_ENERGY_OFFSET 8
#define SEGMENT 3
#define MIN_FILL 3
#define SEPARATOR 0x12
#define FULL 100
#define FIXED 256
#define SPELLCASTER 0x200000
#define HALLUCINATION 0x40000000
#define BROODLING 40

typedef struct {
	int32_t threshold;
	uint8_t rows[7];
} Colors;

/* The first entry whose threshold the percentage reaches applies (0x5085d0, 0x5085f4, 0x5085e4). */
static const Colors HP_COLORS[3] = { { 66, { 0x12, 0x00, 0x01, 0x02, 0x12 } }, { 33, { 0x12, 0x03, 0x04, 0x05, 0x12 } },
	{ 0, { 0x12, 0x06, 0x07, 0x08, 0x12 } } };
static const Colors SHIELD_HP_COLORS[3] = { { 66, { 0x12, 0x00, 0x00, 0x01, 0x01, 0x02, 0x12 } },
	{ 33, { 0x12, 0x03, 0x03, 0x04, 0x04, 0x05, 0x12 } }, { 0, { 0x12, 0x06, 0x06, 0x07, 0x07, 0x08, 0x12 } } };
static const Colors ENERGY_COLORS[1] = { { 0, { 0x12, 0x0c, 0x0d, 0x0e, 0x12 } } };
static const uint8_t EMPTY[5] = { 0x12, 0x0f, 0x10, 0x11, 0x12 };                          /* 0x5085ec */
static const uint8_t SHIELD_HP_EMPTY[7] = { 0x12, 0x0f, 0x0f, 0x10, 0x10, 0x11, 0x12 };    /* 0x50860c */
static const uint8_t SHIELD_FILL[4] = { 0x12, 0x0a, 0x0b, 0x12 };                          /* 0x508614 */
static const uint8_t SHIELD_EMPTY[4] = { 0x12, 0x10, 0x11, 0x12 };                         /* 0x508618 */

/* The truncating division of the original (int.idiv). */
static int32_t idiv(int64_t a, int64_t b) { return (int32_t)(a / b); }

static int32_t mod(int32_t a, int32_t b)
{
	int32_t m = a % b;
	return m < 0 ? m + b : m;
}

void kp_bar_fill(int32_t max, int32_t cur, int32_t span, int32_t *pct, int32_t *filled)
{
	int32_t p = max == 0 ? FULL : idiv((int64_t)cur * 100, max), n = idiv((int64_t)p * span, 100), rem;
	if (n < MIN_FILL) n = MIN_FILL;
	rem = mod(n, SEGMENT);
	if (rem == 1) n--;
	else if (rem == 2) n++;
	*pct = p;
	*filled = n;
}

static const uint8_t *colors(const Colors *entries, int count, int32_t pct)
{
	int k;
	for (k = 0; k < count; k++) {
		if (pct >= entries[k].threshold) return entries[k].rows;
	}
	return entries[count - 1].rows;
}

static uint8_t entry(const KpBarCanvas *c, uint8_t k) { return c->thpbar[k]; }

/* Rows of a fill bar (0x4649a0 with 5 rows, 0x464ab0 with 7): the filled columns from the frame's left, the rest
 * empty, then the separators every SEGMENT frame columns. x, y: the visible crop's top-left on the canvas. */
static void bar_rows(const KpBarCanvas *c, int32_t x, int32_t y, const int32_t crop[4], int32_t filled,
	const uint8_t *row_colors, const uint8_t *empty, int32_t rows)
{
	int32_t f, e, r, sx, rem;
	if (crop[3] != rows) return;
	f = filled - crop[0];
	if (f > crop[2]) f = crop[2];
	else if (f < 0) f = 0;
	e = crop[2] - f;
	for (r = 0; r < rows; r++) {
		if (f > 0) c->hline(c->ud, x, y + r, f, entry(c, row_colors[r]));
		if (e > 0) c->hline(c->ud, x + f, y + r, e, entry(c, empty[r]));
	}
	sx = x;
	rem = mod(crop[0], SEGMENT);
	if (crop[0] != 0 && rem != 0) sx = x - rem + SEGMENT;
	while (sx < x + crop[2]) {
		c->vline(c->ud, sx, y, rows, entry(c, SEPARATOR));
		sx += SEGMENT;
	}
}

/* 0x464bc0: the shield rows over the top of the hit points; with the frame's left column visible the left end slants
 * (rows start 0, 2, 3, 5 columns in) and the slant pixels take the separator colour. */
static void shield_rows(const KpBarCanvas *c, int32_t x, int32_t y, const int32_t crop[4], int32_t filled)
{
	int32_t f, e, fx, ex, r, sx, rem, h;
	uint8_t sep = entry(c, SEPARATOR);
	if (crop[3] != KP_BAR_SHIELD_HP_ROWS) return;
	f = filled - crop[0];
	if (f > crop[2]) f = crop[2];
	else if (f < 0) f = 0;
	e = crop[2] - f;
	fx = x;
	ex = x + f;
	for (r = 0; r < SHIELD_ROWS; r++) {
		if (f > 0) c->hline(c->ud, fx, y + r, f, entry(c, SHIELD_FILL[r]));
		if (e > 0) c->hline(c->ud, ex, y + r, e, entry(c, SHIELD_EMPTY[r]));
		if (crop[0] == 0) {
			int32_t d = r != 1 ? 2 : 1;
			if (f != 0) {
				f -= d;
				fx += d;
			} else if (e != 0) {
				e -= d;
				ex += d;
			}
		}
	}
	sx = x;
	rem = mod(crop[0], SEGMENT);
	if (crop[0] != 0 && rem != 0) sx = x - rem + SEGMENT;
	sx += SEGMENT;
	h = crop[3] < SHIELD_ROWS ? crop[3] : SHIELD_ROWS;
	while (sx < x + crop[2]) {
		c->vline(c->ud, sx, y, h, sep);
		sx += SEGMENT;
	}
	if (crop[0] == 0) {
		int32_t px = x;
		for (r = 0; r < SHIELD_ROWS; r++) {
			c->hline(c->ud, px, y + r, 2, sep);
			px += r != 1 ? 2 : 1;
		}
	}
}

void kp_bar_draw(const KpBarCanvas *c, int32_t x, int32_t y, const int32_t crop[4], const KpBar *bar)
{
	int32_t local[4], offset, pct, filled;
	local[0] = crop[0];
	local[1] = crop[1];
	local[2] = crop[2];
	local[3] = crop[3];
	if (!bar->shields) {
		local[3] = crop[3] < KP_BAR_HP_ROWS ? crop[3] : KP_BAR_HP_ROWS;
		kp_bar_fill(bar->hp_max, bar->hp, bar->width - 1, &pct, &filled);
		bar_rows(c, x, y, local, filled, colors(HP_COLORS, 3, pct), EMPTY, KP_BAR_HP_ROWS);
		offset = ENERGY_OFFSET;
	} else {
		int32_t spct, shield;
		local[3] = crop[3] < KP_BAR_SHIELD_HP_ROWS ? crop[3] : KP_BAR_SHIELD_HP_ROWS;
		kp_bar_fill(bar->hp_max, bar->hp, bar->width - 1, &pct, &filled);
		kp_bar_fill(bar->sh_max, bar->sh, bar->width, &spct, &shield);
		bar_rows(c, x, y, local, filled, colors(SHIELD_HP_COLORS, 3, pct), SHIELD_HP_EMPTY, KP_BAR_SHIELD_HP_ROWS);
		shield_rows(c, x, y, local, shield);
		offset = SHIELD_ENERGY_OFFSET;
	}
	if (bar->has_energy) {
		local[1] = crop[1] + offset;
		local[3] = crop[3] - offset;
		if (local[3] > 0) {
			kp_bar_fill(bar->energy_max, bar->energy, bar->width, &pct, &filled);
			bar_rows(c, x, y + offset, local, filled, colors(ENERGY_COLORS, 1, pct), EMPTY, KP_BAR_HP_ROWS);
		}
	}
}

static int64_t floor_fixed(int64_t v)
{
	int64_t q = v / FIXED;
	if (v % FIXED != 0 && v < 0) q--;
	return q;
}

void kp_bar_from_unit(const KpBarUnit *u, int64_t local_player, KpBar *bar)
{
	int64_t hp_max = floor_fixed(u->hit_points), hp = floor_fixed(u->hp + 0xff);
	int hallucination = (u->status & HALLUCINATION) != 0;
	int energy = (u->abilities & SPELLCASTER) != 0 && !hallucination;
	int timer = !energy && (hallucination || u->type == BROODLING);
	memset(bar, 0, sizeof *bar);
	if (hp_max == 0) hp_max = hp != 0 ? hp : 1;
	bar->width = (int32_t)u->width;
	bar->shields = u->shield_enabled != 0;
	bar->hp = (int32_t)hp;
	bar->hp_max = (int32_t)hp_max;
	bar->sh = (int32_t)floor_fixed(u->shields);
	bar->sh_max = (int32_t)u->shield_amount;
	if ((energy || timer) && u->owner == local_player) {
		bar->has_energy = 1;
		if (energy) {
			bar->energy = (int32_t)floor_fixed(u->energy);
			bar->energy_max = (int32_t)floor_fixed(u->max_energy);
		} else {
			bar->energy = (int32_t)u->timer;
			bar->energy_max = hallucination ? KP_BAR_TIMER_HALLUCINATION : KP_BAR_TIMER_BROODLING;
		}
	}
}

void kp_bar_of_unit(KpGame *g, int64_t n, KpBar *bar)
{
	KpImages *w = g->w;
	KpBarUnit u;
	int64_t t = g->u.type[n], s = g->u.sprite[n];
	u.type = t;
	u.hp = g->u.hp[n];
	u.shields = g->u.shields[n];
	u.energy = g->u.energy[n];
	u.timer = g->u.remove_timer[n];
	u.status = g->u.status[n];
	u.owner = g->u.owner[n];
	u.hit_points = KP_DAT(g, units, hit_points, t);
	u.shield_enabled = KP_DAT(g, units, shield_enabled, t);
	u.shield_amount = KP_DAT(g, units, shield_amount, t);
	u.abilities = KP_DAT(g, units, special_ability_flags, t);
	u.width = w->col.health_bar ? kp_dat_value(w->col.health_bar, (uint32_t)w->spr.id[s]) : 0;
	u.max_energy = kp_max_energy(g, n);
	kp_bar_from_unit(&u, g->local_player, bar);
}

/* --- the bar texture --- */

void kp_bars_init(KpBars *b, const uint8_t *thpbar, size_t size)
{
	int k;
	memset(b, 0, sizeof *b);
	for (k = 0; k < KP_IMAGES; k++) b->slot_of[k] = -1;
	for (k = 0; k < KP_BAR_SLOTS; k++) b->key_of[k] = -1;
	memcpy(b->thpbar, thpbar, size < sizeof b->thpbar ? size : sizeof b->thpbar);
}

void kp_bars_begin(KpBars *b) { b->tick++; }

typedef struct {
	KpBars *b;
	int32_t base;
} BarTexture;

static void put(BarTexture *t, int32_t x, int32_t y, uint8_t color)
{
	size_t o;
	if (x < 0 || x >= KP_BAR_WIDTH || y < 0 || y >= KP_BAR_ROWS) return;
	o = ((size_t)(t->base + y) * KP_BAR_WIDTH + (size_t)x) * 2;
	t->b->pixels[o] = color;
	t->b->pixels[o + 1] = 255;
}

static void tex_hline(void *ud, int32_t x, int32_t y, int32_t n, uint8_t color)
{
	int32_t k;
	for (k = 0; k < n; k++) put((BarTexture *)ud, x + k, y, color);
}

static void tex_vline(void *ud, int32_t x, int32_t y, int32_t n, uint8_t color)
{
	int32_t k;
	for (k = 0; k < n; k++) put((BarTexture *)ud, x, y + k, color);
}

static int same_bar(const KpBar *a, const KpBar *b)
{
	return a->width == b->width && a->shields == b->shields && a->hp == b->hp && a->hp_max == b->hp_max
		&& a->sh == b->sh && a->sh_max == b->sh_max && a->has_energy == b->has_energy
		&& (!a->has_energy || (a->energy == b->energy && a->energy_max == b->energy_max));
}

int32_t kp_bars_put(KpBars *b, int32_t key, const KpBar *bar, int32_t w, int32_t h)
{
	int32_t slot;
	if (key < 0 || key >= KP_IMAGES) return -1;
	slot = b->slot_of[key];
	if (slot < 0) {
		if (b->free_count > 0) slot = b->free[--b->free_count];
		else if (b->next_slot < KP_BAR_SLOTS) slot = b->next_slot++;
		else return -1;
		b->slot_of[key] = slot;
		b->key_of[slot] = key;
	}
	b->seen[slot] = b->tick;
	if (!b->has_drawn[slot] || !same_bar(&b->drawn[slot], bar) || b->drawn_w[slot] != w || b->drawn_h[slot] != h) {
		BarTexture t;
		KpBarCanvas c;
		int32_t crop[4];
		b->has_drawn[slot] = 1;
		b->drawn[slot] = *bar;
		b->drawn_w[slot] = w;
		b->drawn_h[slot] = h;
		memset(&b->pixels[(size_t)slot * KP_BAR_ROWS * KP_BAR_WIDTH * 2], 0, (size_t)KP_BAR_ROWS * KP_BAR_WIDTH * 2);
		t.b = b;
		t.base = slot * KP_BAR_ROWS;
		c.hline = tex_hline;
		c.vline = tex_vline;
		c.ud = &t;
		c.thpbar = b->thpbar;
		crop[0] = 0;
		crop[1] = 0;
		crop[2] = w;
		crop[3] = h;
		kp_bar_draw(&c, 0, 0, crop, bar);
		if (b->dirty_lo == b->dirty_hi) {
			b->dirty_lo = slot;
			b->dirty_hi = slot + 1;
		} else {
			if (slot < b->dirty_lo) b->dirty_lo = slot;
			if (slot >= b->dirty_hi) b->dirty_hi = slot + 1;
		}
	}
	return slot;
}

/* Frees the slots not used this tick (in slot order). */
void kp_bars_finish(KpBars *b)
{
	int32_t slot;
	for (slot = 0; slot < KP_BAR_SLOTS; slot++) {
		int32_t key = b->key_of[slot];
		if (key < 0 || b->seen[slot] == b->tick) continue;
		b->slot_of[key] = -1;
		b->key_of[slot] = -1;
		b->has_drawn[slot] = 0;
		b->free[b->free_count++] = slot;
	}
}
