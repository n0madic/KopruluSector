/*
 * The interface's layout engine and its layouts (ADR 0011, plan §2.1): the logical screen of a window and where each
 * panel goes.
 *
 * The logical screen is the index target the world and the panels are drawn into: at least 640 x 480 logical pixels
 * (the original's screen), so the console always fits; one logical pixel is `scale` window pixels. "integer" keeps a
 * whole scale (nearest), "sharp" a fractional one with the sharp-bilinear upscale, "auto" the first from a 2x scale
 * up. Positions are logical pixels from the top-left; panels keep their own (dialog) coordinates inside them.
 *
 * The console is the original's 640 x 480 console art with its dialogs, cut into groups at columns no panel crosses.
 * A layout places each group: "centre" keeps the original's arrangement centred, "left" and "right" pin the group to
 * that edge, "above" pins it left on top of the other groups' art (the narrow screens' second row). Between groups a
 * layout moves apart, a filler stretches one column of the console art over the gap. Panels outside the console are
 * pinned to a corner or centred; the modeless dialogs keep their place on the original's screen centred.
 */
#include "kp_present.h"

#include <math.h>
#include <string.h>

#define CONSOLE_W 640
#define CONSOLE_H 480
#define FILLER_COLUMN 70
enum { PLACE_CENTRE, PLACE_LEFT, PLACE_RIGHT, PLACE_ABOVE };
enum { GROUP_LEFT, GROUP_RIGHT };

static const struct {
	int32_t x0, x1, top; /* the art's top row over the group (the highest of the three races' consoles) */
} GROUPS[KP_LAYOUT_GROUPS] = { { 0, 138, 290 }, { 138, 640, 311 } };

static const int PLACES[KP_LAYOUTS][KP_LAYOUT_GROUPS] = {
	{ PLACE_CENTRE, PLACE_CENTRE }, /* classic */
	{ PLACE_LEFT, PLACE_RIGHT },    /* wide */
	{ PLACE_ABOVE, PLACE_LEFT },    /* narrow: the status panel, portrait and card along the bottom, the minimap over */
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
	{ "rez\\statdata.bin", ANCHOR_GROUP, GROUP_RIGHT, 0, 0 },
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
	"rez\\spd_dlg.bin", "rez\\video.bin", "rez\\ok.bin", "rez\\okcancel.bin", "rez\\wait.bin", "rez\\tips_dlg.bin",
	"rez\\wmission.bin", "rez\\lmission.bin" };

void kp_layout_minimum(int32_t win_w, int32_t win_h, int fixed, int32_t *min_w, int32_t *min_h)
{
	*min_w = !fixed && win_h > win_w ? KP_LAYOUT_NARROW_MIN_W : KP_LAYOUT_MIN_W;
	*min_h = KP_LAYOUT_MIN_H;
}

void kp_layout_screen(int32_t win_w, int32_t win_h, int mode, int32_t min_w, int32_t min_h, int *mode_out,
	double *scale, int32_t *w, int32_t *h)
{
	double fit;
	if (win_w < 1) win_w = 1;
	if (win_h < 1) win_h = 1;
	if (min_w <= 0) min_w = KP_LAYOUT_MIN_W;
	if (min_h <= 0) min_h = KP_LAYOUT_MIN_H;
	fit = (double)win_w / min_w < (double)win_h / min_h ? (double)win_w / min_w : (double)win_h / min_h;
	if (mode == KP_SCALE_AUTO) mode = fit >= 2 ? KP_SCALE_INTEGER : KP_SCALE_SHARP;
	*mode_out = mode;
	if (mode == KP_SCALE_INTEGER) {
		int32_t s = (int32_t)floor(fit);
		if (s < 1) s = 1;
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

/* The x offset of a console group on a logical screen w wide. */
static int32_t group_dx(int g, int place, int32_t w)
{
	if (place == PLACE_LEFT || place == PLACE_ABOVE) return -GROUPS[g].x0;
	if (place == PLACE_RIGHT) return w - GROUPS[g].x1;
	return kp_floor_div(w - CONSOLE_W, 2);
}

void kp_layout_arrange(KpArrangement *a, int32_t w, int32_t h, int layout)
{
	const KpLayoutGroup *previous = NULL;
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
		p->x = group_dx(g, place, w);
		/* a group placed above rises onto the other groups' art top */
		p->y = place == PLACE_ABOVE ? a->dy - (CONSOLE_H - GROUPS[GROUP_RIGHT].top) : a->dy;
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
			f->column = FILLER_COLUMN;
		}
		if (place != PLACE_ABOVE) previous = p;
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
			if (strcmp(NARROW_PANELS[k].bin, bin) == 0) return &NARROW_PANELS[k];
		}
	}
	for (k = 0; k < sizeof PANELS / sizeof PANELS[0]; k++) {
		if (strcmp(PANELS[k].bin, bin) == 0) return &PANELS[k];
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
		if (p->lift) {
			KpArrangement original;
			int32_t lift;
			kp_layout_arrange(&original, KP_LAYOUT_MIN_W, KP_LAYOUT_MIN_H, KP_LAYOUT_CLASSIC);
			lift = a->top - original.top;
			if (lift < 0) *y += lift;
		}
		return 1;
	case ANCHOR_TOP_RIGHT:
		*x = a->w - (KP_LAYOUT_MIN_W - rect[0]);
		*y = rect[1] + p->dy;
		return 1;
	default: {
		/* over the console's top as the original's, or above a second row of the console; a backdrop dialog lies under
		 * the console art, so over a filler (whose column is opaque from the left group's art top) it rises above it */
		int32_t px = kp_floor_div(a->w - w, 2) + rect[0] - kp_floor_div(KP_LAYOUT_MIN_W - w, 2);
		int32_t py = rect[1] + a->dy, h = rect[3] - rect[1] + 1, filler_top = a->dy + GROUPS[GROUP_LEFT].top;
		int k;
		if (a->w < KP_LAYOUT_MIN_W) px = kp_floor_div(a->w - w, 2);
		if (py + h > a->top && a->top < filler_top) py = a->top - h;
		for (k = 0; k < a->filler_count; k++) {
			const KpLayoutFiller *f = &a->fillers[k];
			if (px < f->x + f->w && px + w > f->x && py + h > filler_top) py = filler_top - h;
		}
		*x = px;
		*y = py;
		return 1;
	}
	}
}
