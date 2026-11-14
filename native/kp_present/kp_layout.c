/*
 * The interface's layout engine and its layouts (ADR 0011, plan §2.1): the logical screen of a window and where each
 * panel goes.
 *
 * The logical screen is the index target the world and the panels are drawn into: at least 640 x 480 logical pixels
 * (the original's screen), so the console always fits; one logical pixel is `scale` window pixels. "integer" keeps a
 * whole scale (nearest), "sharp" a fractional one with the sharp-bilinear upscale, "auto" the first from a 2x scale
 * up, except on a fixed 640 x 480 screen (the glue's), which it fits whole with the second: a whole scale left a
 * margin up to a third of the window on every side (3x on a 2184 x 1968 phone, where 3.41x fits). Positions are logical pixels from the top-left; panels keep their own (dialog) coordinates inside them.
 *
 * The console is composed from StarCraft: Remastered's pieces (kp_canvas.c): three groups cut at columns no panel crosses,
 * the minimap, the status panel, and the portrait with the menu button and the command card. The layouts place each group
 * by the screen's width alone. "classic" keeps the original's arrangement centred. "wide" opens from the original's 640
 * columns: first the pieces' ornaments come in from both edges, then the minimap and the card stay at the edges and the
 * status panel goes to the middle between them, over the wings; "edges" is the same without the ornaments, the minimap
 * and the card in the corners (a phone's thumbs). "narrow" keeps the status panel left and the card right along the bottom
 * with the minimap's group above them; "narrow original" is the same in the original's art alone, the card beside the
 * status panel for want of a wing between them. Panels outside the console are pinned to a corner or centred; the modeless
 * dialogs keep their place on the original's screen centred.
 */
#include "kp_present.h"

#include <math.h>
#include <string.h>

#define CONSOLE_W 640
#define CONSOLE_H 480
#define ORIGINAL_TOP 290   /* the art's top row over the minimap group: the console's highest on the original's */
#define PIECES_TOP 283     /* ... of the pieces (the Zerg's left one) */
#define BOTTOM_ROW_TOP 352 /* the centre piece's top row: a narrow screen's bottom row */
#define ORIGINAL_BOTTOM_ROW_TOP 311 /* ... in the original's art: its top row over the status and right groups */
#define WINGS_TOP 350      /* a row over the wings' and the centre piece's art, whichever lies under the text box */
#define ORNAMENT_LEFT 106  /* the columns of the left and right pieces' ornaments */
#define ORNAMENT_RIGHT 107
#define NARROW_STATUS_DX (-KP_UI_CONSOLE_CUT_1) /* the status panel at the screen's left */
enum { GROUP_LEFT, GROUP_STATUS, GROUP_RIGHT };

static const int32_t GROUP_COLUMNS[KP_LAYOUT_GROUPS][2] = { { 0, KP_UI_CONSOLE_CUT_1 },
	{ KP_UI_CONSOLE_CUT_1, KP_UI_CONSOLE_CUT_2 }, { KP_UI_CONSOLE_CUT_2, CONSOLE_W } };

static const char *const NAMES[KP_LAYOUTS] = { "classic", "wide", "narrow", "edges", "narrow original" };

enum { ANCHOR_GROUP, ANCHOR_TOP_LEFT, ANCHOR_TOP_RIGHT, ANCHOR_BOTTOM_CENTRE };

typedef struct {
	const char *bin;
	int anchor, group;
	int32_t dy;
	int lift;
} Panel;

/* The panels: a console panel's group, the others' anchors. */
static const Panel PANELS[] = {
	{ "rez\\minimap.bin", ANCHOR_GROUP, GROUP_LEFT, 0, 0 },
	{ "rez\\statdata.bin", ANCHOR_GROUP, GROUP_STATUS, 0, 0 },
	{ "rez\\statport.bin", ANCHOR_GROUP, GROUP_RIGHT, 0, 0 },
	{ "rez\\stat_f10.bin", ANCHOR_GROUP, GROUP_RIGHT, 0, 0 },
	{ "rez\\statbtnt.bin", ANCHOR_GROUP, GROUP_RIGHT, 0, 0 },
	{ "rez\\statbtnz.bin", ANCHOR_GROUP, GROUP_RIGHT, 0, 0 },
	{ "rez\\statbtnp.bin", ANCHOR_GROUP, GROUP_RIGHT, 0, 0 },
	{ "rez\\statbtnn.bin", ANCHOR_GROUP, GROUP_RIGHT, 0, 0 }, /* a replay's (InitCommandCard 0x4a0f20) */
	{ "rez\\statres.bin", ANCHOR_TOP_RIGHT, 0, 0, 0 },
	{ "rez\\statlb.bin", ANCHOR_TOP_LEFT, 0, 0, 0 },
	{ "rez\\ttextbox.bin", ANCHOR_BOTTOM_CENTRE, 0, 0, 0 },
	{ "rez\\ztextbox.bin", ANCHOR_BOTTOM_CENTRE, 0, 0, 0 },
	{ "rez\\ptextbox.bin", ANCHOR_BOTTOM_CENTRE, 0, 0, 0 },
	/* the text lines' layer, the whole original screen; its rows over the console art no higher than the original's */
	{ "lines", ANCHOR_TOP_LEFT, 0, 0, 1 },
};

/* The narrow layout's own anchors: the leaderboard under the resources. */
static const Panel NARROW_PANELS[] = { { "rez\\statlb.bin", ANCHOR_TOP_LEFT, 0, 20, 0 } };

/* The modeless dialogs over the game: the original's 640 x 480 screen centred on the logical screen. */
static const char *const CENTRED[] = { "rez\\gamemenu.bin", "rez\\abrtmenu.bin", "rez\\objctdlg.bin",
	"rez\\options.bin", "rez\\helpmenu.bin", "rez\\help.bin", "rez\\quit.bin", "rez\\quit2mnu.bin",
	"rez\\quitrepl.bin", "rez\\restart.bin", "rez\\savegame.bin", "rez\\loadgame.bin", "rez\\snd_dlg.bin",
	"rez\\spd_dlg.bin", "koprulu\\video.bin", "rez\\ok.bin", "rez\\okcancel.bin", "rez\\wait.bin", "rez\\tips_dlg.bin",
	"rez\\wmission.bin", "rez\\lmission.bin", "rez\\allyfltr.bin" };

/* A fixed screen fills the whole drawable: fitted to the safe area, the 640 x 480 lost the gesture zones' height (the
 * Fold7's cover screen in landscape: 931 of 1080 pixels) and left black bands. Its edges reach the zones, where the
 * system takes swipes only (a tap goes through; Android's tappable insets are the visible bars'), and on a screen wider
 * or taller than 4:3 a cutout falls into the margins. */
void kp_layout_picture(int fixed, int32_t full_w, int32_t full_h, const int32_t safe[4], int32_t picture[4])
{
	if (fixed || safe[2] <= 0 || safe[3] <= 0) {
		picture[0] = picture[1] = 0;
		picture[2] = full_w;
		picture[3] = full_h;
		return;
	}
	memcpy(picture, safe, 4 * sizeof *picture);
}

void kp_layout_minimum(int32_t win_w, int32_t win_h, int fixed, int32_t *min_w, int32_t *min_h)
{
	*min_w = !fixed && win_h > win_w ? KP_LAYOUT_NARROW_MIN_W : KP_LAYOUT_MIN_W;
	*min_h = KP_LAYOUT_MIN_H;
}

void kp_layout_screen(int32_t win_w, int32_t win_h, int mode, int fixed, int32_t min_w, int32_t min_h, int *mode_out,
	double *scale, int32_t *w, int32_t *h)
{
	double fit;
	if (win_w < 1) win_w = 1;
	if (win_h < 1) win_h = 1;
	if (min_w <= 0) min_w = KP_LAYOUT_MIN_W;
	if (min_h <= 0) min_h = KP_LAYOUT_MIN_H;
	fit = (double)win_w / min_w < (double)win_h / min_h ? (double)win_w / min_w : (double)win_h / min_h;
	if (mode == KP_SCALE_AUTO) mode = fit >= 2 && !fixed ? KP_SCALE_INTEGER : KP_SCALE_SHARP;
	if (mode == KP_SCALE_INTEGER && fit < 1) mode = KP_SCALE_SHARP; /* a window smaller than the screen: no whole scale */
	*mode_out = mode;
	if (mode == KP_SCALE_INTEGER) {
		int32_t s = (int32_t)floor(fit);
		*scale = s;
		*w = (win_w + s - 1) / s;
		*h = (win_h + s - 1) / s;
		return;
	}
	if ((double)win_w / win_h >= (double)min_w / min_h) {
		*scale = (double)win_h / min_h;
		*w = (int32_t)ceil((double)min_h * win_w / win_h);
		*h = min_h;
		return;
	}
	*scale = (double)win_w / min_w;
	*w = min_w;
	*h = (int32_t)ceil((double)min_w * win_h / win_w);
}

int kp_layout_choose(int32_t w, int pieces, int edges)
{
	if (w < KP_LAYOUT_MIN_W) return pieces ? KP_LAYOUT_NARROW : KP_LAYOUT_NARROW_ORIGINAL;
	if (!pieces) return KP_LAYOUT_CLASSIC;
	return edges ? KP_LAYOUT_EDGES : KP_LAYOUT_WIDE;
}

const char *kp_layout_name(int layout) { return layout >= 0 && layout < KP_LAYOUTS ? NAMES[layout] : "?"; }

static int32_t clamp32(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }

static int is_narrow(int layout) { return layout == KP_LAYOUT_NARROW || layout == KP_LAYOUT_NARROW_ORIGINAL; }

/* The groups' x offsets from the original's arrangement on a screen w wide. */
static void group_offsets(int layout, int32_t w, int32_t dx[KP_LAYOUT_GROUPS])
{
	int32_t spare = w - CONSOLE_W, left, right, status;
	if (layout == KP_LAYOUT_CLASSIC) {
		dx[GROUP_LEFT] = dx[GROUP_STATUS] = dx[GROUP_RIGHT] = kp_floor_div(spare, 2);
		return;
	}
	if (is_narrow(layout)) {
		dx[GROUP_LEFT] = 0;
		dx[GROUP_STATUS] = NARROW_STATUS_DX;
		dx[GROUP_RIGHT] = layout == KP_LAYOUT_NARROW && spare > NARROW_STATUS_DX ? spare : NARROW_STATUS_DX;
		return;
	}
	if (layout == KP_LAYOUT_EDGES || spare <= 0) {
		left = 0;
		right = spare;
	} else {
		left = clamp32(kp_floor_div(spare, 2), 0, ORNAMENT_LEFT);
		right = spare - clamp32(spare - left, 0, ORNAMENT_RIGHT);
	}
	/* the status panel in the screen's middle, as far as the blocks beside it let it */
	status = kp_floor_div(w - (KP_UI_CONSOLE_CUT_2 - KP_UI_CONSOLE_CUT_1), 2) - KP_UI_CONSOLE_CUT_1;
	dx[GROUP_LEFT] = left;
	dx[GROUP_STATUS] = clamp32(status, left, right);
	dx[GROUP_RIGHT] = right;
}

void kp_layout_arrange(KpArrangement *a, int32_t w, int32_t h, int layout)
{
	int32_t dx[KP_LAYOUT_GROUPS];
	int g;
	memset(a, 0, sizeof *a);
	if (layout < 0 || layout >= KP_LAYOUTS) layout = kp_layout_choose(w, 1, 0);
	a->layout = layout;
	a->w = w;
	a->h = h;
	a->dy = h - CONSOLE_H;
	group_offsets(layout, w, dx);
	for (g = 0; g < KP_LAYOUT_GROUPS; g++) {
		KpLayoutGroup *p = &a->groups[g];
		p->x = dx[g];
		p->above = is_narrow(layout) && g == GROUP_LEFT;
		/* a group placed above rises onto the other groups' art top */
		p->y = p->above ? a->dy - (CONSOLE_H - (layout == KP_LAYOUT_NARROW ? BOTTOM_ROW_TOP : ORIGINAL_BOTTOM_ROW_TOP)) : a->dy;
		p->x0 = GROUP_COLUMNS[g][0];
		p->x1 = GROUP_COLUMNS[g][1];
		p->left = p->x + p->x0;
		p->right = p->x + p->x1;
	}
	/* the pieces' higher rows count only over a minimap's group raised alone: elsewhere nothing lifts on a screen of the
	 * original's height, whose panels and lines keep the original's rows */
	a->top = a->groups[GROUP_LEFT].y + (layout == KP_LAYOUT_NARROW ? PIECES_TOP : ORIGINAL_TOP);
}

static int same_lower(const char *a, const char *b)
{
	for (; *a && *b; a++, b++) {
		char x = *a >= 'A' && *a <= 'Z' ? (char)(*a + 32) : *a, y = *b >= 'A' && *b <= 'Z' ? (char)(*b + 32) : *b;
		if (x != y) return 0;
	}
	return *a == *b;
}

static const Panel *find_panel(int layout, const char *bin)
{
	size_t k;
	if (is_narrow(layout)) {
		for (k = 0; k < sizeof NARROW_PANELS / sizeof NARROW_PANELS[0]; k++) {
			if (same_lower(NARROW_PANELS[k].bin, bin)) return &NARROW_PANELS[k];
		}
	}
	for (k = 0; k < sizeof PANELS / sizeof PANELS[0]; k++) {
		if (same_lower(PANELS[k].bin, bin)) return &PANELS[k];
	}
	return NULL;
}

static int centred(const char *bin)
{
	size_t k;
	for (k = 0; k < sizeof CENTRED / sizeof CENTRED[0]; k++) {
		if (same_lower(CENTRED[k], bin)) return 1;
	}
	return 0;
}

int kp_layout_place(const KpArrangement *a, const char *bin, const int32_t rect[4], int32_t *x, int32_t *y)
{
	const Panel *p = find_panel(a->layout, bin);
	int32_t w = rect[2] - rect[0] + 1;
	if (!p && centred(bin)) {
		if (a->w < KP_LAYOUT_MIN_W) { /* narrower than the original's screen: the dialog itself centred */
			*x = kp_floor_div(a->w - w, 2);
			*y = kp_floor_div(a->h - KP_LAYOUT_MIN_H, 2) + rect[1];
			return 1;
		}
		*x = kp_floor_div(a->w - KP_LAYOUT_MIN_W, 2) + rect[0];
		*y = kp_floor_div(a->h - KP_LAYOUT_MIN_H, 2) + rect[1];
		return 1;
	}
	if (!p) return 0;
	switch (p->anchor) {
	case ANCHOR_GROUP:
		*x = rect[0] + a->groups[p->group].x;
		*y = rect[1] + a->groups[p->group].y;
		return 1;
	case ANCHOR_TOP_LEFT:
		*x = rect[0];
		*y = rect[1] + p->dy;
		/* a console whose art rises higher than the original's over the panel (a short narrow screen) lifts it */
		if (p->lift && a->top < ORIGINAL_TOP) *y += a->top - ORIGINAL_TOP;
		return 1;
	case ANCHOR_TOP_RIGHT:
		*x = a->w - (KP_LAYOUT_MIN_W - rect[0]);
		*y = rect[1] + p->dy;
		return 1;
	default: {
		/* over the console's top as the original's, or above a second row of the console; a backdrop dialog lies under
		 * the console art, so where the panel's holes do not lie under it (any screen but the original's) it rises above
		 * the wings, or the second row */
		int32_t px = kp_floor_div(a->w - w, 2) + rect[0] - kp_floor_div(KP_LAYOUT_MIN_W - w, 2);
		int32_t py = rect[1] + a->dy, h = rect[3] - rect[1] + 1;
		if (a->w < KP_LAYOUT_MIN_W) px = kp_floor_div(a->w - w, 2);
		if (is_narrow(a->layout)) {
			if (py + h > a->top) py = a->top - h;
		} else if (a->w != KP_LAYOUT_MIN_W && a->layout != KP_LAYOUT_CLASSIC && py + h > a->dy + WINGS_TOP) {
			py = a->dy + WINGS_TOP - h;
		}
		*x = px;
		*y = py;
		return 1;
	}
	}
}
