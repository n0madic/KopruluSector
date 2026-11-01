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
 * The console is the original's 640 x 480 console art with its dialogs, cut into three groups at columns no panel
 * crosses, as StarCraft: Remastered cuts its HD console: the minimap, the status panel, and the portrait with the menu
 * button and the command card. A layout places each group: "centre" keeps the original's arrangement centred, "middle"
 * centres the group alone, "left" and "right" pin the group to that edge, "follow" keeps it beside the group before it,
 * "above" pins it left on top of the other groups' art (the narrow screens' second row). Between groups a layout moves
 * apart, a filler stretches one column of the console art over the gap: a column of the group on the gap's left, opaque
 * from no higher than that group's art. Panels outside the console are pinned to a corner or centred; the modeless
 * dialogs keep their place on the original's screen centred.
 */
#include "kp_present.h"

#include <math.h>
#include <string.h>

#define CONSOLE_W 640
#define CONSOLE_H 480
#define BOTTOM_ROW_TOP 311 /* the art's top row over the status and right groups: a narrow screen's bottom row */
#define ORIGINAL_TOP 290   /* the art's top row over the minimap group: the console's highest on the original's */
enum { PLACE_CENTRE, PLACE_MIDDLE, PLACE_LEFT, PLACE_RIGHT, PLACE_FOLLOW, PLACE_ABOVE };
enum { GROUP_LEFT, GROUP_STATUS, GROUP_RIGHT };

static const struct {
	int32_t x0, x1, top; /* the art's top row over the group (the highest of the three races' consoles) */
	int32_t filler;      /* the column a filler on the group's right stretches */
} GROUPS[KP_LAYOUT_GROUPS] = { { 0, KP_UI_CONSOLE_CUT_1, ORIGINAL_TOP, 70 },
	{ KP_UI_CONSOLE_CUT_1, KP_UI_CONSOLE_CUT_2, 311, 300 }, { KP_UI_CONSOLE_CUT_2, 640, 312, -1 } };

static const int PLACES[KP_LAYOUTS][KP_LAYOUT_GROUPS] = {
	{ PLACE_CENTRE, PLACE_CENTRE, PLACE_CENTRE }, /* classic */
	{ PLACE_LEFT, PLACE_MIDDLE, PLACE_RIGHT },    /* wide */
	{ PLACE_ABOVE, PLACE_LEFT, PLACE_FOLLOW },    /* narrow: the status panel, portrait and card along the bottom,
	                                                 the minimap over */
};

static const char *const NAMES[KP_LAYOUTS] = { "classic", "wide", "narrow" };

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
	"rez\\wmission.bin", "rez\\lmission.bin" };

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

int kp_layout_choose(int32_t w, int32_t h)
{
	(void)h;
	if (w < KP_LAYOUT_MIN_W) return KP_LAYOUT_NARROW;
	if (w >= 800) return KP_LAYOUT_WIDE;
	return KP_LAYOUT_CLASSIC;
}

const char *kp_layout_name(int layout) { return layout >= 0 && layout < KP_LAYOUTS ? NAMES[layout] : "?"; }

/* The x offset of a console group on a logical screen w wide (previous_dx: the group before it). A group in the
 * middle stays between the group before it and the rest of the console pinned right: on a screen too narrow to centre
 * it, it keeps to the original's arrangement. */
static int32_t group_dx(int g, int place, int32_t w, int32_t previous_dx)
{
	if (place == PLACE_LEFT || place == PLACE_ABOVE) return -GROUPS[g].x0;
	if (place == PLACE_RIGHT) return w - GROUPS[g].x1;
	if (place == PLACE_MIDDLE) {
		int32_t dx = kp_floor_div(w - (GROUPS[g].x1 - GROUPS[g].x0), 2) - GROUPS[g].x0;
		int32_t lo = g > 0 ? previous_dx + GROUPS[g - 1].x1 - GROUPS[g].x0 : -GROUPS[g].x0, hi = w - CONSOLE_W;
		if (dx > hi) dx = hi;
		return dx < lo ? lo : dx;
	}
	if (place == PLACE_FOLLOW) return previous_dx;
	return kp_floor_div(w - CONSOLE_W, 2);
}

void kp_layout_arrange(KpArrangement *a, int32_t w, int32_t h, int layout)
{
	const KpLayoutGroup *previous = NULL;
	int previous_g = -1;
	int g;
	memset(a, 0, sizeof *a);
	if (layout < 0 || layout >= KP_LAYOUTS) layout = kp_layout_choose(w, h);
	a->layout = layout;
	a->w = w;
	a->h = h;
	a->dy = h - CONSOLE_H;
	a->top = a->dy;
	for (g = 0; g < KP_LAYOUT_GROUPS; g++) {
		int place = PLACES[layout][g];
		KpLayoutGroup *p = &a->groups[g];
		int32_t art_top;
		p->x = group_dx(g, place, w, g > 0 ? a->groups[g - 1].x : 0);
		/* a group placed above rises onto the other groups' art top */
		p->y = place == PLACE_ABOVE ? a->dy - (CONSOLE_H - BOTTOM_ROW_TOP) : a->dy;
		p->x0 = GROUPS[g].x0;
		p->x1 = GROUPS[g].x1;
		p->left = p->x + p->x0;
		p->right = p->x + p->x1;
		p->above = place == PLACE_ABOVE;
		art_top = p->y + GROUPS[g].top;
		if (g == 0 || art_top < a->top) a->top = art_top;
		if (place != PLACE_ABOVE && previous && !previous->above && p->left > previous->right
			&& a->filler_count < KP_LAYOUT_FILLERS) {
			KpLayoutFiller *f = &a->fillers[a->filler_count++];
			f->x = previous->right;
			f->y = a->dy;
			f->w = p->left - previous->right;
			f->h = CONSOLE_H;
			f->column = GROUPS[previous_g].filler;
			f->top = a->dy + GROUPS[previous_g].top;
		}
		if (place != PLACE_ABOVE) {
			previous = p;
			previous_g = g;
		}
	}
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
	if (layout == KP_LAYOUT_NARROW) {
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
		 * the console art, so over a filler (whose column is opaque from its group's art top) it rises above it */
		int32_t px = kp_floor_div(a->w - w, 2) + rect[0] - kp_floor_div(KP_LAYOUT_MIN_W - w, 2);
		int32_t py = rect[1] + a->dy, h = rect[3] - rect[1] + 1;
		int k;
		if (a->w < KP_LAYOUT_MIN_W) px = kp_floor_div(a->w - w, 2);
		if (py + h > a->top && a->top < a->dy + GROUPS[GROUP_LEFT].top) py = a->top - h;
		for (k = 0; k < a->filler_count; k++) {
			const KpLayoutFiller *f = &a->fillers[k];
			if (px < f->x + f->w && px + w > f->x && py + h > f->top) py = f->top - h;
		}
		*x = px;
		*y = py;
		return 1;
	}
	}
}
