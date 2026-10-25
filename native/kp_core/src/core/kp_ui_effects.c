/* The interface's blends (effects.cpp, atlas P44 0x4ca110..0x4ca9b0): the three blend tables of a palette
 * (SetEffectPalette 0x4ca660 with the nearest-colour search 0x4c9c70; the glue's descriptor is 0, so slot s has
 * weight s + 1), the opacity levels (InitEffects 0x4ca420: level =
 * 7 * percent / 100) and the blits of BlitWithEffect 0x4ca110 (0 copy, 1 colour 0 transparent, 3 opacity), and the
 * colour row of DrawTextWithOpacity 0x4ca290. The SCode loops of 0x4ca420: below level 3 D = T[S + 256 D] (0x4f1264),
 * from level 3 D = T[D + 256 S] (0x4f12ec); a table's row and column 0 are the identity, so colour 0 keeps D. */
#include "kp_ui.h"

#include <stdlib.h>
#include <string.h>

#define GRID 18             /* the buckets: 16 per channel and a border on each side */
#define NEIGHBOURS 26
#define NEAR_ENOUGH 0x400u
#define GAME_PERCENT 60     /* StartDialogPaletteEffect 0x47bca0's opacity */

/* 0x50ef98: the neighbour buckets tried in order */
static const int8_t NEAR[NEIGHBOURS][3] = {
	{ -1, 0, 0 }, { 0, -1, 0 }, { 0, 0, -1 }, { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 }, { -1, 1, 0 }, { -1, 0, 1 },
	{ 1, -1, 0 }, { 0, -1, 1 }, { 1, 0, -1 }, { 0, 1, -1 }, { -1, -1, 0 }, { -1, 0, -1 }, { 0, -1, -1 }, { 1, 1, 0 },
	{ 1, 0, 1 }, { 0, 1, 1 }, { -1, -1, 1 }, { -1, 1, -1 }, { 1, -1, -1 }, { 1, 1, -1 }, { 1, -1, 1 }, { -1, 1, 1 },
	{ -1, -1, -1 }, { 1, 1, 1 },
};

int kp_ui_effect_level(uint32_t percent) { return (int)(7u * percent / 100u); }

static uint32_t distance(const uint8_t *pal, int k, const int t[3])
{
	int32_t dr = (int32_t)pal[4 * k] - t[0], dg = (int32_t)pal[4 * k + 1] - t[1], db = (int32_t)pal[4 * k + 2] - t[2];
	return (uint32_t)(dr * dr + dg * dg + db * db);
}

/* The table of weight w (BuildBlendTable 0x4c9c70): T[r * 256 + c] = the palette entry nearest
 * (w P[c] + (8 - w) P[r]) / 8; an empty bucket starts from r at the distance 0xffffffff. */
static void build_table(const uint8_t *pal, int w, const uint8_t *excluded, uint8_t *table)
{
	int16_t head[GRID * GRID * GRID], next[256];
	int i, r, c;
	for (i = 0; i < GRID * GRID * GRID; i++) head[i] = -1;
	for (i = 0; i < 256; i++) { /* prepended: a bucket lists its entries from the highest index down */
		int b;
		next[i] = -1;
		if (excluded != NULL && excluded[i]) continue;
		if (i > 0 && memcmp(pal + 4 * i, pal + 4 * (i - 1), 3) == 0) continue;
		b = ((pal[4 * i] >> 4) + 1) * GRID * GRID + ((pal[4 * i + 1] >> 4) + 1) * GRID + (pal[4 * i + 2] >> 4) + 1;
		next[i] = head[b];
		head[b] = (int16_t)i;
	}
	for (r = 1; r < 256; r++) {
		for (c = 1; c < 256; c++) {
			int t[3], b[3], k, best = r, n;
			uint32_t best_d = 0xffffffffu;
			for (k = 0; k < 3; k++) {
				t[k] = (w * pal[4 * c + k] + (8 - w) * pal[4 * r + k]) >> 3;
				b[k] = (t[k] >> 4) + 1;
			}
			k = head[b[0] * GRID * GRID + b[1] * GRID + b[2]];
			if (k >= 0 && next[k] < 0) {
				best = k;
			} else if (k >= 0) {
				for (; k >= 0; k = next[k]) {
					uint32_t d = distance(pal, k, t);
					if (d < best_d) {
						best_d = d;
						best = k;
					}
				}
			} else {
				for (n = 0; n < NEIGHBOURS && best_d > NEAR_ENOUGH; n++) {
					int x = b[0] + NEAR[n][0], y = b[1] + NEAR[n][1], z = b[2] + NEAR[n][2];
					if (x < 0 || y < 0 || z < 0 || x >= GRID || y >= GRID || z >= GRID) continue;
					for (k = head[x * GRID * GRID + y * GRID + z]; k >= 0; k = next[k]) {
						uint32_t d = distance(pal, k, t);
						if (d < best_d) {
							best_d = d;
							best = k;
						}
					}
				}
			}
			table[r * 256 + c] = (uint8_t)best;
		}
		table[r * 257] = (uint8_t)r;
	}
	for (i = 0; i < 256; i++) table[i] = table[i * 256] = (uint8_t)i;
}

/* The slot of a level 1..6 (0x4ca420: level <= 3 ? level - 1 : 6 - level). */
static int slot_of(int level) { return level <= 3 ? level - 1 : 6 - level; }

static int build_slot(KpUiEffects *e, int level, const uint8_t *excluded)
{
	int s = slot_of(level);
	if (e->tables[s] == NULL) e->tables[s] = (uint8_t *)malloc(256 * 256);
	if (e->tables[s] == NULL) return -1;
	build_table(e->palette, level, excluded, e->tables[s]);
	e->slot_built[s] = 1;
	return 0;
}

int kp_ui_effects_palette(KpUi *ui, const uint8_t *palette)
{
	/* SetEffectPalette 0x4ca660 without a descriptor (the glue's): the three slots of levels 1..3, no exclusions */
	KpUiEffects *e = &ui->effects;
	int level;
	if (e->built && memcmp(e->palette, palette, sizeof e->palette) == 0) return 0;
	memcpy(e->palette, palette, sizeof e->palette);
	for (level = 1; level <= KP_UI_EFFECT_SLOTS; level++) {
		if (build_slot(e, level, NULL) != 0) return -1;
	}
	e->built = 1;
	return 0;
}

int kp_ui_effects_descriptor(KpUi *ui, const uint8_t *palette, const uint8_t *excluded, const uint8_t *levels,
	int level_count)
{
	/* InitEffects 0x4ca420 (the cached palette cleared), SetEffectPalette 0x4ca660 with the descriptor: a slot is built
	 * once, by the first of its levels; levels 0 and 7 build none */
	KpUiEffects *e = &ui->effects;
	int k;
	memcpy(e->palette, palette, sizeof e->palette);
	memset(e->slot_built, 0, sizeof e->slot_built);
	for (k = 0; k < level_count; k++) {
		if (levels[k] == 0 || levels[k] >= 7 || e->slot_built[slot_of(levels[k])]) continue;
		if (build_slot(e, levels[k], excluded) != 0) return -1;
	}
	e->built = 1;
	return 0;
}

int kp_ui_game_effects(KpUi *ui, const uint8_t *rgb, const uint8_t *cycled)
{
	uint8_t palette[1024], level = (uint8_t)kp_ui_effect_level(GAME_PERCENT);
	int k;
	for (k = 0; k < 256; k++) {
		palette[4 * k] = rgb[3 * k];
		palette[4 * k + 1] = rgb[3 * k + 1];
		palette[4 * k + 2] = rgb[3 * k + 2];
		palette[4 * k + 3] = 0;
	}
	if (kp_ui_effects_descriptor(ui, palette, cycled, &level, 1) != 0) return -1;
	ui->effects.image_percent = ui->effects.combo_percent = ui->effects.dialog_percent = GAME_PERCENT;
	ui->effects.level[0] = level; /* 0x4ca7e0 of each of the three into set A's level */
	return 0;
}

void kp_ui_effect_row(const KpUi *ui, int set, uint8_t out[256])
{
	int d;
	for (d = 0; d < 256; d++) out[d] = kp_ui_single_colour(ui, set, ui->effects.colour[set & 1], (uint8_t)d);
}

void kp_ui_effects_free(KpUi *ui)
{
	int s;
	for (s = 0; s < KP_UI_EFFECT_SLOTS; s++) {
		free(ui->effects.tables[s]);
		ui->effects.tables[s] = NULL;
	}
	ui->effects.built = 0;
	memset(ui->effects.slot_built, 0, sizeof ui->effects.slot_built);
}

static const uint8_t *slot(const KpUi *ui, int level) { return ui->effects.tables[slot_of(level)]; }

/* A level 1..6 whose table exists (a game's descriptor builds one slot only). */
static int usable(const KpUi *ui, int level) { return ui->effects.built && slot(ui, level) != NULL; }

uint8_t kp_ui_blend(const KpUi *ui, int level, uint8_t src, uint8_t dst)
{
	if (level <= 0) return dst;
	if (level >= 7 || !usable(ui, level)) return src ? src : dst;
	return level < 3 ? slot(ui, level)[src + 256 * dst] : slot(ui, level)[dst + 256 * src];
}

uint8_t kp_ui_single_colour(const KpUi *ui, int set, uint8_t src, uint8_t dst)
{
	int level = ui->effects.level[set & 1];
	if (src == 0) return dst;
	if (src != ui->effects.colour[set & 1] || level >= 7) return src;
	if (level <= 0) return 0;
	if (!usable(ui, level)) return src;
	/* row c of 0x6cfbcc, read as T[D + 256 S] by the loop 0x4f12ec */
	return level <= 2 ? slot(ui, level)[dst * 256 + src] : slot(ui, level)[src * 256 + dst];
}

void kp_ui_blit_effect(const KpUi *ui, uint8_t *dst, int32_t dst_pitch, const uint8_t *src, int32_t src_pitch,
	int32_t w, int32_t h, int mode, uint32_t percent)
{
	int32_t x, y;
	int level = kp_ui_effect_level(percent);
	for (y = 0; y < h; y++) {
		uint8_t *d = dst + (size_t)y * (size_t)dst_pitch;
		const uint8_t *s = src + (size_t)y * (size_t)src_pitch;
		switch (mode) {
		case KP_UI_BLIT_COPY: memcpy(d, s, (size_t)w); break;
		case KP_UI_BLIT_KEYED: /* BlitKeyed 0x4ca950 */
			for (x = 0; x < w; x++) {
				if (s[x]) d[x] = s[x];
			}
			break;
		case KP_UI_BLIT_OPACITY: /* BlitOpacity 0x4ca8a0 */
			if (level <= 0) return;
			for (x = 0; x < w; x++) d[x] = kp_ui_blend(ui, level, s[x], d[x]);
			break;
		default: return;
		}
	}
}

void kp_ui_text_opacity_row(const KpUi *ui, uint8_t out[8], const uint8_t row[8], const uint8_t base[8],
	uint32_t percent)
{
	/* DrawTextWithOpacity 0x4ca290: a = the text's row, b = row 2 */
	int level = kp_ui_effect_level(percent), k;
	for (k = 0; k < 8; k++) {
		uint8_t a = row[k], b = base[k];
		if (level <= 0) out[k] = a;
		else if (level >= 7 || !usable(ui, level)) out[k] = b;
		else if (level <= 2) out[k] = slot(ui, level)[a * 256 + b];
		else out[k] = slot(ui, level)[b * 256 + a];
	}
}
