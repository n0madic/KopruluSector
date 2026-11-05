/* The console's minimap (plan phase 9B; minimap.cpp; atlas P24 0x46c920..0x46eba0): the terrain's colours by the
 * map's size (LayoutMinimap 0x46d850), the fog (kp_fog_minimap), the units' boxes by player and the fog ghosts
 * (DrawMinimapUnits 0x46d510), the pings (blink.grp, DrawMinimapPings 0x46d3f0), the view's rectangle
 * (DrawMinimapImage 0x46e990) and the timers 1 (1000 ms, the redraw), 2 (200 ms, the view), 3 (200 ms, the blink)
 * and 6 (100 ms, the pings). The minimap reads the game and never writes it; its buttons are phase 9C's. */
#include "kp_ui.h"

#include <stdlib.h>
#include <string.h>

#include "kp_fog_view.h"
#include "kp_game.h"
#include "kp_ids.h"
#include "kp_images.h"

#define C_IMAGE 1
#define IMAGE_SIDE 128
#define CONSOLE_Y 315                 /* the dialog's y origin of the clip rectangle 0x508bb0 */
#define TIMER_REDRAW 1
#define TIMER_VIEW 2
#define TIMER_BLINK 3
#define TIMER_PINGS 6
#define REDRAW_PERIOD 1000
#define VIEW_PERIOD 200
#define BLINK_PERIOD 200
#define PINGS_PERIOD 100
#define PING_LAST_FRAME 19
#define PING_HALF 0x40                /* a ping's offset: half the blink.grp frame centred on it (0x46e8f0) */
#define FLAG_BUILDING 0x1
#define FLAG_SUBUNIT 0x10
#define STATUS_CLOAK 0x300
#define TYPE_DARK_SWARM 202
#define TYPE_FIRST_DOOR 203
#define TYPE_LAST_TRAP 213
#define TYPE_DISRUPTION_WEB 105
#define UI_WHITE 0                    /* 0x6d2368 */
#define UI_FRAME_LIGHT 2              /* 0x6d236a */
#define UI_FRAME_DARK 5               /* 0x6d236d */
#define UI_BLACK 15                   /* 0x6d2377 */
#define BUTTON_TERRAIN 2
#define KEY_TAB 9
#define COLOUR_MODES 3
#define S_SHOW_TERRAIN 0x32a              /* "Show Terrain in Minimap (Tab)" */
#define S_HIDE_TERRAIN 0x32b
#define UI_ALLY 17                    /* 0x6d2379 */
#define UI_LOCAL 18                   /* 0x6d237a */
#define UI_ENEMY 20                   /* 0x6d237c */
#define UI_RESOURCE 25                /* 0x6d2381 */
#define MEGATILE_ID 0x7fff
#define MMNI_RECORD 4
#define DARK_ROWS 32

/* The ghosts' resource sprites (DrawMinimapUnits 0x46d510). */
static int resource_sprite(int32_t id) { return id == 275 || id == 279 || id == 280 || id == 281; }

static KpUiMinimap *mm(KpUi *ui) { return &ui->minimap; }
/* The view in tiles: the original's 20 x 13 (DrawMinimapViewRect, MinimapMoveCamera 0x46e020) for its 640 x 400. */
static uint32_t view_tiles_w(const KpUi *ui) { return (uint32_t)ui->view_w / 32; }
static uint32_t view_tiles_h(const KpUi *ui) { return ((uint32_t)ui->view_h + 31) / 32; }

static uint8_t colour(const KpUi *ui, int k) { return ui->ui_colours[k]; }

/* --- the terrain (BuildMinimapTerrain1x 0x46c920, Half 0x46c990, 2x 0x46ca20) --- */

static uint8_t cell(const KpUi *ui, int64_t megatile, int k)
{
	size_t at = (size_t)(megatile & MEGATILE_ID) * MMNI_RECORD + (size_t)k;
	return at < ui->minimap.mmni_size ? ui->minimap.palette_map[ui->minimap.mmni[at]] : 0;
}

static void build_terrain(KpUi *ui)
{
	KpGame *g = ui->game;
	KpUiMinimap *m = mm(ui);
	int64_t mw = g->map_w, side = g->map_w > g->map_h ? g->map_w : g->map_h, x, y;
	if (m->terrain == NULL || m->mmni == NULL) return;
	for (y = 0; y < m->h; y++) {
		for (x = 0; x < m->w; x++) {
			uint8_t c;
			if (side == 64) c = cell(ui, g->terrain.megatiles[(y / 2) * mw + x / 2], (int)((y & 1) * 2 + (x & 1)));
			else if (side >= 192) c = cell(ui, g->terrain.megatiles[(2 * y) * mw + 2 * x], 0);
			else c = cell(ui, g->terrain.megatiles[y * mw + x], 0);
			m->terrain[y * m->w + x] = c;
		}
	}
}

/* The fog over the terrain (BuildMinimapFogTables 0x46e840 and the composer), or index 0 with the terrain hidden. */
static void compose(KpUi *ui)
{
	KpGame *g = ui->game;
	KpUiMinimap *m = mm(ui);
	KpFogVision v;
	KpUiSurface image;
	int64_t w, h;
	int32_t all[4];
	if (m->image == NULL) return;
	/* the image is drawn again whole: the pings' images there before have no pixel left (ADR 0013 R4d) */
	image.w = m->w;
	image.h = m->h;
	image.pixels = m->image;
	all[0] = all[1] = 0;
	all[2] = m->w;
	all[3] = m->h;
	kp_ui_image_erase(ui, &image, all, NULL, 0, 0);
	if (m->terrain_hidden) {
		memset(m->image, 0, (size_t)m->w * (size_t)m->h);
		return;
	}
	memset(&v, 0, sizeof v);
	v.tiles = g->terrain.tiles;
	v.map_w = g->map_w;
	v.map_h = g->map_h;
	v.not_visible = (int64_t)1 << g->local_player;
	v.not_explored = v.not_visible << 8;
	if (m->dark != NULL) (void)kp_fog_minimap(&v, m->terrain, m->dark, colour(ui, UI_BLACK), m->image, &w, &h);
}

/* --- the units (DrawMinimapUnitBox 0x46cb80, DrawMinimapUnits 0x46d510 and its player walks) --- */

static void unit_box(KpUi *ui, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint8_t c, int building)
{
	KpUiMinimap *m = mm(ui);
	uint32_t hw = (w & 0xff) >> 1, hh = (h & 0xff) >> 1, shift = m->shift, div = m->divisor;
	int16_t left, top, bw, bh;
	int32_t iw, ih;
	KpUiSurface target, *saved = ui->text.target;
	if (x < hw) x = hw;
	if (y < hh) y = hh;
	left = (int16_t)(((x - hw) << shift) / div >> 5);
	top = (int16_t)(((y - hh) << shift) / div >> 5);
	iw = (int32_t)((w & 0xff) << shift) / (int32_t)div;
	ih = (int32_t)((h & 0xff) << shift) / (int32_t)div;
	bw = (int16_t)(iw / 32);
	bh = (int16_t)(ih / 32);
	if (bw < 2 || (!building && bw > 2)) bw = 2;
	else if (building && bw > 4) bw = 4;
	if (bh < 2 || (!building && bh > 2)) bh = 2;
	else if (building && bh > 4) bh = 4;
	if (left >= 0 && m->w <= left + bw - 1) bw = (int16_t)(m->w - left);
	if (top >= 0 && m->h <= top + bh - 1) bh = (int16_t)(m->h - top);
	target.w = m->w;
	target.h = m->h;
	target.pixels = m->image;
	ui->text.target = &target;
	kp_ui_fill_rect(ui, left, top, bw, bh, c);
	ui->text.target = saved;
}

static int skipped(KpGame *g, int64_t t)
{
	return (KP_DAT(g, units, special_ability_flags, t) & FLAG_SUBUNIT) || (t >= TYPE_FIRST_DOOR && t <= TYPE_LAST_TRAP)
		|| t == TYPE_DARK_SWARM || t == TYPE_DISRUPTION_WEB;
}

/* The low byte of a type's placement WORD (0x522728 width, 0x52272a height). */
static uint32_t placement(KpGame *g, int64_t t, int k)
{
	return (uint32_t)KP_DAT(g, units, staredit_placement_size, t * 2 + k) & 0xff;
}

static uint32_t sight_mask(KpGame *g, int64_t n)
{
	int32_t s = kp_unit_sprite(g, n);
	uint32_t mask = (uint32_t)g->w->spr.visibility[s] & 0xff;
	if (g->u.status[n] & STATUS_CLOAK) mask &= (uint32_t)g->u.visibility[n];
	return mask;
}

static void box_of(KpUi *ui, int64_t n, uint8_t c)
{
	KpGame *g = ui->game;
	int32_t s = kp_unit_sprite(g, n);
	int64_t t = g->u.type[n];
	unit_box(ui, (uint32_t)(uint16_t)g->w->spr.x[s], (uint32_t)(uint16_t)g->w->spr.y[s], placement(g, t, 0),
		placement(g, t, 1), c, (KP_DAT(g, units, special_ability_flags, t) & FLAG_BUILDING) != 0);
}

static uint8_t player_colour(KpUi *ui, int64_t p)
{
	KpGame *g = ui->game;
	if (mm(ui)->colour_mode == 0) return mm(ui)->player_colours[p & 0xf];
	return kp_alliance(g, g->local_player, p) ? colour(ui, UI_ALLY) : colour(ui, UI_ENEMY);
}

/* DrawNeutralMinimapUnits 0x46e5b0 (resources == 1) and DrawOtherMinimapUnits 0x46e700. */
static void other_units(KpUi *ui, int64_t p, int resources)
{
	KpGame *g = ui->game;
	uint8_t c = player_colour(ui, p);
	int64_t n;
	for (n = g->player_units[p]; n != 0; n = g->u.player_next[n]) {
		int64_t t = g->u.type[n];
		if (skipped(g, t)) continue;
		if (!(sight_mask(g, n) & (1u << g->local_player))) continue; /* TODO: a replay's vision (0x67cc84) */
		box_of(ui, n, resources && (kp_mineral_field(t) || t == KP_TYPE_GEYSER) ? colour(ui, UI_RESOURCE) : c);
	}
}

/* DrawLocalMinimapUnits 0x46e490. */
static void local_units(KpUi *ui)
{
	KpGame *g = ui->game;
	KpUiMinimap *m = mm(ui);
	int64_t n;
	m->blinked = 0;
	for (n = g->player_units[g->local_player]; n != 0; n = g->u.player_next[n]) {
		int64_t t = g->u.type[n];
		uint8_t c = colour(ui, UI_LOCAL);
		if (skipped(g, t)) continue;
		if (!(sight_mask(g, n) & (1u << g->local_player))) continue;
		/* the blink counter +0x8a (under attack 20, a talk with a ping 8) counts the draws down: the interface writes
		 * the unit's byte as the original does, and nothing of the simulation reads it (ADR 0010) */
		if ((g->u.last_event_timer[n] & 0xff) != 0) {
			g->u.last_event_timer[n] = (g->u.last_event_timer[n] & 0xff) - 1;
			m->blinked = 1;
			if (m->blink_phase) c = (uint8_t)g->u.last_event_color[n];
		}
		box_of(ui, n, c);
	}
}

static void draw_units(KpUi *ui)
{
	KpGame *g = ui->game;
	KpImages *w = g->w;
	int32_t t;
	int64_t p;
	for (p = 11; p >= 0; p--) {
		if (p >= 8) other_units(ui, p, 1);
		else if (p != g->local_player) other_units(ui, p, 0);
	}
	local_units(ui);
	for (t = w->active_fog.head; t >= 0 && t < KP_FOG; t = w->fog.next[t]) { /* the fog ghosts */
		int32_t s = w->fog.sprite[t], kind = w->fog.kind[t];
		uint8_t c;
		if ((uint16_t)(kind - TYPE_FIRST_DOOR) <= TYPE_LAST_TRAP - TYPE_FIRST_DOOR) continue;
		if (resource_sprite(w->spr.id[s])) c = colour(ui, UI_RESOURCE);
		else c = player_colour(ui, w->spr.owner[s]);
		unit_box(ui, (uint32_t)(uint16_t)w->spr.x[s], (uint32_t)(uint16_t)w->spr.y[s], placement(g, kind, 0),
			placement(g, kind, 1), c, 1);
		if (t == w->active_fog.tail) break;
	}
}

/* DrawMinimapPings 0x46d3f0. */
static void draw_pings(KpUi *ui)
{
	KpUiMinimap *m = mm(ui);
	KpUiSurface target, *saved = ui->text.target;
	int k, i;
	const KpUiFile *blink = kp_ui_file(ui->grps, ui->grp_count, "game\\blink.grp");
	if (blink == NULL) return;
	target.w = m->w;
	target.h = m->h;
	target.pixels = m->image;
	ui->text.target = &target;
	for (k = 0; k < KP_UI_PINGS; k++) {
		const KpUiPing *p = &m->pings[k];
		int32_t fw, fh, crop[4], x, y, frame = p->frame; /* the blink.grp frame (0x463bd0) */
		const uint8_t *rec;
		if (!p->active) continue;
		memcpy(ui->remap, m->blink_rows + 8 * (p->colour % 19), 8);
		if (kp_ui_grp_frame_size(blink->data, blink->size, frame, &fw, &fh) == 0) {
			if ((uint32_t)(frame & 0xffff) >= ((uint32_t)blink->data[0] | (uint32_t)(blink->data[1] & 0x7f) << 8)) frame = 0;
			rec = blink->data + 6 + 8 * frame;
			crop[0] = 0;
			crop[1] = 0;
			crop[2] = fw;
			crop[3] = fh;
			x = rec[0] + p->x;
			y = rec[1] + p->y;
			if (x < 0) {
				crop[0] -= x;
				crop[2] += x;
				x = 0;
			}
			if (crop[2] - m->w + x > 0) crop[2] -= crop[2] - m->w + x;
			if (y < 0) {
				crop[1] -= y;
				crop[3] += y;
				y = 0;
			}
			if (crop[3] - m->h + y > 0) crop[3] -= crop[3] - m->h + y;
			kp_ui_draw_grp_rle(ui, x, y, blink->data, blink->size, frame, crop);
		}
		for (i = 0; i < 8; i++) ui->remap[i] = (uint8_t)i;
	}
	ui->text.target = saved;
}

/* The HD interface's images (ADR 0013 R4d) lose a line drawn on t: w x h pixels from (x, y). */
static void erase_line(KpUi *ui, const KpUiSurface *t, int32_t x, int32_t y, int32_t w, int32_t h)
{
	int32_t rect[4];
	rect[0] = x;
	rect[1] = y;
	rect[2] = x + w;
	rect[3] = y + h;
	kp_ui_image_erase(ui, t, rect, NULL, 0, 0);
}

/* RedrawMinimap 0x46e0e0 (the terrain rebuilt first when asked: RebuildAndRedrawMinimap 0x46e140). */
static void redraw(KpUi *ui, int rebuild)
{
	if (rebuild) build_terrain(ui);
	compose(ui);
	draw_units(ui);
	draw_pings(ui);
	kp_ui_mark_dirty(ui, mm(ui)->control);
}

/* The screen's top-left tile (WORDs 0x5264b0, 0x5264b2), which the game keeps from the screen's draws. */
static uint16_t screen_tile(const KpGame *g, KpScalar s)
{
	int64_t v = kp_scalar(g, s);
	return v == KP_NIL ? 0 : (uint16_t)v;
}

/* --- the timers --- */

/* The game's terrain changes since the last look set the rebuild flag 0x671fa2. */
static void note_terrain(KpUi *ui)
{
	KpUiMinimap *m = mm(ui);
	if (ui->game->minimap_terrain != m->terrain_serial) {
		m->terrain_serial = ui->game->minimap_terrain;
		m->rebuild = 1;
	}
}

static void timer_redraw(KpUi *ui, int32_t owner)
{
	(void)owner;
	note_terrain(ui);
	redraw(ui, mm(ui)->rebuild); /* 0x46e8d0 */
	mm(ui)->rebuild = 0;
}

static void timer_view(KpUi *ui, int32_t owner)
{
	/* MinimapViewTimer 0x46d360 */
	KpUiMinimap *m = mm(ui);
	uint16_t x = screen_tile(ui->game, KP_SCREEN_X), y = screen_tile(ui->game, KP_SCREEN_Y);
	(void)owner;
	if (x != m->view_x || y != m->view_y) {
		m->view_x = x;
		m->view_y = y;
		kp_ui_mark_dirty(ui, m->control);
	}
}

static int any_ping(const KpUiMinimap *m)
{
	int k;
	for (k = 0; k < KP_UI_PINGS; k++) {
		if (m->pings[k].active) return 1;
	}
	return 0;
}

static void timer_blink(KpUi *ui, int32_t owner)
{
	/* 0x46eba0 */
	KpUiMinimap *m = mm(ui);
	(void)owner;
	m->blink_phase ^= 1;
	if (!m->blinked || any_ping(m)) return;
	note_terrain(ui);
	if (m->rebuild) build_terrain(ui);
	compose(ui);
	draw_units(ui);
	kp_ui_mark_dirty(ui, m->control);
	m->rebuild = 0;
}

static void timer_pings(KpUi *ui, int32_t owner)
{
	/* MinimapPingTimer 0x46ea90 */
	KpUiMinimap *m = mm(ui);
	int k, any = 0;
	(void)owner;
	for (k = 0; k < KP_UI_PINGS; k++) {
		if (!m->pings[k].active) continue;
		if (m->pings[k].frame == PING_LAST_FRAME) m->pings[k].active = 0;
		else m->pings[k].frame++;
		any = 1;
	}
	if (!any) return;
	note_terrain(ui);
	if (m->rebuild) build_terrain(ui);
	compose(ui);
	draw_units(ui);
	draw_pings(ui);
	kp_ui_mark_dirty(ui, m->control);
	m->rebuild = 0;
}

/* InitMinimap 0x46e260's per-game state: the view's tile, the terrain's serial, no pings. */
static void init_state(KpUi *ui)
{
	KpUiMinimap *m = mm(ui);
	int k;
	m->view_x = screen_tile(ui->game, KP_SCREEN_X);
	m->view_y = screen_tile(ui->game, KP_SCREEN_Y);
	m->terrain_serial = ui->game->minimap_terrain;
	m->rebuild = 0;
	for (k = 0; k < KP_UI_PINGS; k++) m->pings[k].active = 0;
}

void kp_ui_minimap_reset(KpUi *ui)
{
	KpUiMinimap *m = mm(ui);
	if (ui->game == NULL || m->image == NULL || m->control < 0) return;
	init_state(ui);
	redraw(ui, 1); /* MinimapImageInit 0x46d7e0's drawing, the terrain built */
}

void kp_ui_minimap_ping(KpUi *ui, uint32_t x, uint32_t y, uint8_t colour_row)
{
	/* 0x46e8f0 */
	KpUiMinimap *m = mm(ui);
	int k;
	for (k = 0; k < KP_UI_PINGS && m->pings[k].active; k++) {}
	if (k == KP_UI_PINGS) return;
	m->pings[k].active = 1;
	m->pings[k].x = (int32_t)((((x >> 5) << m->shift) / m->divisor) - PING_HALF);
	m->pings[k].y = (int32_t)((((y >> 5) << m->shift) / m->divisor) - PING_HALF);
	m->pings[k].colour = colour_row;
	m->pings[k].frame = 0;
	m->pings[k].map_x = (int32_t)x;
	m->pings[k].map_y = (int32_t)y;
}

/* MinimapPointToMap 0x46cfd0's first step: a screen point as the image's (not clamped to it). */
static void image_point(const KpUi *ui, int32_t sx, int32_t sy, int32_t *px, int32_t *py)
{
	const KpUiControl *c = &ui->controls[ui->minimap.control];
	*px = sx - c->left;
	*py = sy - CONSOLE_Y - c->top;
}

int kp_ui_minimap_ping_near(const KpUi *ui, int32_t x, int32_t y, int32_t radius, int32_t *map_x, int32_t *map_y)
{
	const KpUiMinimap *m = &ui->minimap;
	int64_t best = (int64_t)radius * radius + 1;
	int32_t px, py;
	int k, found = 0;
	if (m->control < 0 || m->image == NULL) return 0;
	image_point(ui, x, y, &px, &py);
	for (k = 0; k < KP_UI_PINGS; k++) {
		const KpUiPing *p = &m->pings[k];
		int64_t dx, dy;
		if (!p->active) continue;
		dx = px - (p->x + PING_HALF); /* the blink's frame centred on the ping */
		dy = py - (p->y + PING_HALF);
		if (dx * dx + dy * dy < best) {
			best = dx * dx + dy * dy;
			*map_x = p->map_x;
			*map_y = p->map_y;
			found = 1;
		}
	}
	return found;
}

/* --- the dialog --- */

/* DrawMinimapImage 0x46e990. */
static void draw_image(KpUi *ui, KpUiControl *c, int32_t x, int32_t y, const int16_t clip[4])
{
	KpUiMinimap *m = mm(ui);
	KpUiSurface *t = ui->text.target;
	int16_t r[4];
	int32_t w, h, x0, y0, i, bottom;
	(void)x;
	(void)y;
	(void)clip;
	r[0] = 0;
	r[1] = 0;
	r[2] = (int16_t)(m->w - 1);
	r[3] = (int16_t)(m->h - 1);
	kp_ui_blit_opaque(ui, c->left, c->top, m->image, m->w, m->h, r);
	w = (int32_t)(uint16_t)((uint32_t)(view_tiles_w(ui) << m->shift) / m->divisor);
	h = (int32_t)(uint16_t)((uint32_t)(view_tiles_h(ui) << m->shift) / m->divisor);
	x0 = c->left + (int16_t)((int32_t)((uint32_t)m->view_x << m->shift) / (int32_t)m->divisor);
	y0 = c->top + (int16_t)((int32_t)((uint32_t)m->view_y << m->shift) / (int32_t)m->divisor);
	bottom = c->top + 1 + (c->bottom - c->top);
	if (bottom < y0 + h) h = bottom - y0;
	if (t == NULL) return;
#define PUT(px, py) do { if ((px) >= 0 && (px) < t->w && (py) >= 0 && (py) < t->h) t->pixels[(int64_t)(py) * t->w + (px)] = colour(ui, UI_WHITE); } while (0)
	for (i = 0; i < w; i++) PUT(x0 + i, y0);
	for (i = 0; i < h; i++) PUT(x0, y0 + i);
	for (i = 0; i < h; i++) PUT(x0 + w - 1, y0 + i);
	for (i = 0; i < w; i++) PUT(x0 + i, y0 + h - 1);
#undef PUT
	erase_line(ui, t, x0, y0, w, 1);
	erase_line(ui, t, x0, y0, 1, h);
	erase_line(ui, t, x0 + w - 1, y0, 1, h);
	erase_line(ui, t, x0, y0 + h - 1, w, 1);
}

/* MinimapImageInit 0x46d7e0. */
static void init_image(KpUi *ui, KpUiControl *c)
{
	c->draw = draw_image;
	build_terrain(ui);
	compose(ui);
	draw_units(ui);
	kp_ui_mark_dirty(ui, (int32_t)(c - ui->controls));
}

#define CMD_MINIMAP_PING 0x58

/* MinimapPointToMap 0x46cfd0: a screen point on the image as a map pixel. */
static void point_to_map(KpUi *ui, int32_t sx, int32_t sy, int32_t *x, int32_t *y)
{
	KpUiMinimap *m = mm(ui);
	int32_t scale = (int32_t)((32u * m->divisor) >> m->shift), ix, iy;
	int16_t px, py;
	image_point(ui, sx, sy, &ix, &iy);
	px = (int16_t)ix;
	py = (int16_t)iy;
	if (px < 0) px = 0;
	else if (px >= m->w) px = (int16_t)(m->w - 1);
	if (py < 0) py = 0;
	else if (py >= m->h) py = (int16_t)(m->h - 1);
	*x = (uint16_t)(px * scale);
	*y = (uint16_t)(py * scale);
}

/* MinimapMoveCamera 0x46e020. */
static void move_camera(KpUi *ui)
{
	KpUiMinimap *m = mm(ui);
	int32_t x, y;
	if (ui->input.scroll.pan) { /* the camera pan holds the view: the captures go (the cursor's clip is the host's) */
		kp_ui_release_refs(ui, m->control);
		return;
	}
	point_to_map(ui, (int16_t)(ui->mouse_x - (int32_t)((view_tiles_w(ui) << m->shift) / (2 * m->divisor))),
		(int16_t)(ui->mouse_y - (int32_t)((view_tiles_h(ui) << m->shift) / (2 * m->divisor))), &x, &y);
	if ((uint16_t)x >> 5 != (uint16_t)(ui->input.screen_x >> 5) || (uint16_t)y >> 5 != (uint16_t)(ui->input.screen_y >> 5)) {
		kp_ui_set_screen_position(ui, ((uint16_t)x >> 5) * 32, ((uint16_t)y >> 5) * 32);
	}
	kp_ui_mark_dirty(ui, m->control);
}

/* MinimapImageProc 0x46d6a0: a paused game's presses do nothing (IsGamePaused 0x47cb60); the cursor's clips to the
 * minimap (0x4ce850) are the host's, which owns the system cursor. */
static int image_interact(KpUi *ui, KpUiControl *c, KpUiEvent *e)
{
	int32_t self = (int32_t)(c - ui->controls), x, y;
	switch (e->type) {
	case KP_UI_MOUSE_MOVE:
		move_camera(ui);
		return 1;
	case KP_UI_LEFT_DOWN: case KP_UI_LEFT_DOUBLE:
		if (kp_scalar(ui->game, KP_PAUSED) != 0) return 1;
		point_to_map(ui, e->x, e->y, &x, &y);
		if (ui->input.targeting) {
			kp_ui_minimap_target(ui, x, y);
		} else if (ui->input.keys[2]) { /* MinimapLeftPress 0x46e420: Alt pings */
			uint8_t cmd[5];
			cmd[0] = CMD_MINIMAP_PING;
			cmd[1] = (uint8_t)x;
			cmd[2] = (uint8_t)(x >> 8);
			cmd[3] = (uint8_t)y;
			cmd[4] = (uint8_t)(y >> 8);
			kp_ui_queue_command(ui, cmd, sizeof cmd);
		} else {
			kp_ui_set_capture(ui, self, KP_UI_LEFT_UP);
			kp_ui_set_capture(ui, self, KP_UI_MOUSE_MOVE);
			move_camera(ui);
		}
		return 1;
	case KP_UI_LEFT_UP: case KP_UI_RIGHT_UP:
		kp_ui_release_refs(ui, self);
		return 1;
	case KP_UI_RIGHT_DOWN: case KP_UI_RIGHT_DOUBLE:
		if (kp_scalar(ui->game, KP_PAUSED) != 0) return 1;
		if (kp_scalar(ui->game, KP_PLACEMENT_ACTIVE) != 1 && !ui->input.targeting) {
			point_to_map(ui, e->x, e->y, &x, &y);
			kp_ui_minimap_right_click(ui, x, y);
		} else {
			kp_ui_set_capture(ui, self, KP_UI_RIGHT_UP);
			kp_ui_set_capture(ui, self, KP_UI_MOUSE_MOVE);
			move_camera(ui);
		}
		return 1;
	case KP_UI_USER:
		if (e->code == KP_UI_CREATE) init_image(ui, c);
		else if (e->code == KP_UI_HIT_TEST) return 1;
		else if (e->code == KP_UI_DESTROY || e->code == KP_UI_FOCUS_LOST) kp_ui_release_refs(ui, self);
		return kp_ui_type_interact(c->type)(ui, c, e);
	default:
		return kp_ui_type_interact(c->type)(ui, c, e);
	}
}

/* MinimapButtonProc 0x46d0e0 with MinimapButtonInit 0x46d130 (a campaign: only the terrain button). */
/* ShowMinimapButtonTooltip 0x46e1e0 (with ShowTerrainButtonTooltip 0x46e880's terrain text): at the button's right
 * top. TODO: the multiplayer buttons' texts (messaging 0x32d, diplomacy 0x32c, a replay's 0x604) */
static void button_tooltip(KpUi *ui, const KpUiControl *c)
{
	const KpUiControl *d = &ui->controls[c->parent];
	if (c->id != BUTTON_TERRAIN) {
		kp_ui_tooltip_hide(ui);
		return;
	}
	kp_ui_tooltip_text(ui, d->left + c->right, d->top + c->top,
		kp_ui_tbl(ui, KP_UI_STAT_TXT, mm(ui)->terrain_hidden ? S_SHOW_TERRAIN : S_HIDE_TERRAIN));
}

static int button_interact(KpUi *ui, KpUiControl *c, KpUiEvent *e)
{
	int32_t self = (int32_t)(c - ui->controls);
	if (e->type == KP_UI_MOUSE_MOVE) { /* MinimapButtonHover 0x46d300 */
		KpUiMinimap *m = mm(ui);
		if (kp_scalar(ui->game, KP_PAUSED) != 0 || kp_ui_hit_test(ui, c->parent, e) != self) {
			m->hovered = -1;
			kp_ui_tooltip_hide(ui);
		} else if (m->hovered != self) {
			m->hovered = self;
			button_tooltip(ui, c);
		}
	} else if (e->type == KP_UI_USER && e->code == KP_UI_CLOSE) { /* MinimapButtonActivate 0x46d060 */
		if (c->id == BUTTON_TERRAIN) {
			mm(ui)->terrain_hidden ^= 1;
			mm(ui)->hovered = -1;
			redraw(ui, 0);
			button_tooltip(ui, c);
		} else {
			kp_ui_tooltip_hide(ui); /* TODO: the message and alliance dialogs (0x441be0, 0x441b70) */
		}
		return 1;
	}
	if (e->type == KP_UI_USER && e->code == KP_UI_CREATE) {
		static const uint16_t FRAMES[3] = { 0x0003, 0x0053, 0x0000 }; /* SetControlUserWord's table 0x508ba0[0, 1, 6] */
		if (c->id == 2) c->wuser = FRAMES[0];
		else if (c->id == 4) c->wuser = FRAMES[1];
		/* TODO: multiplayer and replays (messaging, diplomacy, view players) */
		kp_ui_set_visible(ui, self, c->id == 2);
	}
	return kp_ui_type_interact(c->type)(ui, c, e);
}

/* DrawMinimapFrame 0x46dcf0: the dialog's default draw, then the edges around a smaller image. */
static void draw_frame(KpUi *ui, KpUiControl *d, int32_t x, int32_t y, const int16_t clip[4])
{
	KpUiMinimap *m = mm(ui);
	KpUiDraw stock = kp_ui_type_draw(d->type);
	KpUiSurface *t = ui->text.target;
	const KpUiControl *c = &ui->controls[m->control];
	int32_t i;
	if (stock) stock(ui, d, x, y, clip);
	if (!(c->flags & KP_UI_VISIBLE) || t == NULL) return;
#define PUT(px, py, k) do { if ((px) >= 0 && (px) < t->w && (py) >= 0 && (py) < t->h) t->pixels[(int64_t)(py) * t->w + (px)] = colour(ui, k); } while (0)
	if (m->w >= IMAGE_SIDE && m->h >= IMAGE_SIDE) return;
	if (m->w >= IMAGE_SIDE) {
		for (i = 0; i < m->w; i++) PUT(c->left + i, c->top - 1, UI_FRAME_LIGHT);
		for (i = 0; i < m->w; i++) PUT(c->left + i, c->bottom + 1, UI_FRAME_DARK);
		erase_line(ui, t, c->left, c->top - 1, m->w, 1);
		erase_line(ui, t, c->left, c->bottom + 1, m->w, 1);
	} else if (m->h >= IMAGE_SIDE) {
		for (i = 0; i < m->h; i++) PUT(c->right + 1, c->top + i, UI_FRAME_LIGHT);
		for (i = 0; i < m->h; i++) PUT(c->left - 1, c->top + i, UI_FRAME_DARK);
		erase_line(ui, t, c->right + 1, c->top, 1, m->h);
		erase_line(ui, t, c->left - 1, c->top, 1, m->h);
	} else {
		for (i = 0; i < m->h + 2; i++) PUT(c->right + 1, c->top - 1 + i, UI_FRAME_LIGHT);
		for (i = 0; i < m->w + 2; i++) PUT(c->left - 1 + i, c->top - 1, UI_FRAME_LIGHT);
		for (i = 0; i < m->h + 2; i++) PUT(c->left - 1, c->top - 1 + i, UI_FRAME_DARK);
		for (i = 0; i < m->w + 2; i++) PUT(c->left - 1 + i, c->bottom + 1, UI_FRAME_DARK);
		erase_line(ui, t, c->right + 1, c->top - 1, 1, m->h + 2);
		erase_line(ui, t, c->left - 1, c->top - 1, m->w + 2, 1);
		erase_line(ui, t, c->left - 1, c->top - 1, 1, m->h + 2);
		erase_line(ui, t, c->left - 1, c->bottom + 1, m->w + 2, 1);
	}
#undef PUT
}

/* LayoutMinimap 0x46d850. */
static void layout(KpUi *ui)
{
	KpGame *g = ui->game;
	KpUiMinimap *m = mm(ui);
	KpUiControl *c = &ui->controls[m->control];
	int64_t side = g->map_w > g->map_h ? g->map_w : g->map_h;
	int16_t d;
	switch (side) {
	case 64:
		m->w = (int32_t)(2 * g->map_w);
		m->h = (int32_t)(2 * g->map_h);
		m->divisor = 1;
		m->shift = 1;
		break;
	case 96: case 128:
		m->w = (int32_t)g->map_w;
		m->h = (int32_t)g->map_h;
		m->divisor = 1;
		m->shift = 0;
		break;
	case 192: case 256:
		m->w = (int32_t)(g->map_w / 2);
		m->h = (int32_t)(g->map_h / 2);
		m->divisor = 2;
		m->shift = 0;
		break;
	default: /* the previous values: 128 x 128 after a teardown */
		break;
	}
	d = (int16_t)((IMAGE_SIDE - m->w) / 2);
	c->left = (int16_t)(c->left + d);
	c->right = (int16_t)(c->right - d);
	d = (int16_t)((IMAGE_SIDE - m->h) / 2);
	c->top = (int16_t)(c->top + d);
	c->bottom = (int16_t)(c->bottom - d);
	free(m->terrain);
	free(m->image);
	m->terrain = (uint8_t *)calloc((size_t)m->w * (size_t)m->h, 1);
	m->image = (uint8_t *)calloc((size_t)m->w * (size_t)m->h, 1);
	m->clip[0] = c->left;
	m->clip[1] = (int16_t)(c->top + CONSOLE_Y);
	m->clip[2] = (int16_t)(m->w - 1 + c->left);
	m->clip[3] = (int16_t)(m->h + CONSOLE_Y - 1 + c->top);
}

/* MinimapDialogInit 0x46db30. */
static void init_dialog(KpUi *ui, int32_t dialog)
{
	static const KpUiInteract CHILDREN[4] = { image_interact, button_interact, button_interact, button_interact };
	KpUiMinimap *m = mm(ui);
	ui->controls[dialog].flags |= 0x20000000u;
	ui->controls[dialog].draw = draw_frame;
	kp_ui_bind_interacts(ui, dialog, CHILDREN, 4);
	kp_ui_capture_console(ui, dialog);
	m->control = kp_ui_find_control(ui, dialog, C_IMAGE);
	if (m->control < 0) return;
	layout(ui);
	(void)kp_ui_set_timer(&ui->timers, dialog, TIMER_REDRAW, REDRAW_PERIOD, kp_ui_callback(ui, timer_redraw), ui->now);
	(void)kp_ui_set_timer(&ui->timers, dialog, TIMER_VIEW, VIEW_PERIOD, kp_ui_callback(ui, timer_view), ui->now);
	(void)kp_ui_set_timer(&ui->timers, dialog, TIMER_BLINK, BLINK_PERIOD, kp_ui_callback(ui, timer_blink), ui->now);
	(void)kp_ui_set_timer(&ui->timers, dialog, TIMER_PINGS, PINGS_PERIOD, kp_ui_callback(ui, timer_pings), ui->now);
	kp_ui_show_control(ui, m->control);
	kp_ui_mark_dirty(ui, m->control);
}

/* MinimapDialogProc 0x46de70. */
static int dialog_interact(KpUi *ui, KpUiControl *d, KpUiEvent *e)
{
	int32_t self = (int32_t)(d - ui->controls), hit;
	KpUiInteract stock = kp_ui_type_interact(0);
	switch (e->type) {
	case KP_UI_KEY_DOWN: case KP_UI_KEY_REPEAT:
		return 0;
	case KP_UI_MOUSE_MOVE: /* MinimapDialogHover 0x46dfd0 */
		if (kp_scalar(ui->game, KP_PAUSED) != 0) return 0;
		hit = kp_ui_hit_test(ui, self, e);
		if (hit != mm(ui)->hovered) {
			mm(ui)->hovered = hit;
			if (hit >= 0 && hit != self) button_tooltip(ui, &ui->controls[hit]);
			else kp_ui_tooltip_hide(ui);
		}
		return stock(ui, d, e);
	case KP_UI_LEFT_DOWN: case KP_UI_LEFT_DOUBLE: case KP_UI_RIGHT_DOWN: case KP_UI_RIGHT_DOUBLE:
		if (kp_scalar(ui->game, KP_PAUSED) != 0) return 0; /* IsSinglePlayerPaused 0x47cb70: the clicks pass on */
		hit = kp_ui_hit_test(ui, self, e);
		if (hit < 0) return 0;
		if (hit != self && ui->controls[hit].interact) (void)ui->controls[hit].interact(ui, &ui->controls[hit], e);
		return 1;
	case KP_UI_USER:
		if (e->code == KP_UI_CREATE) init_dialog(ui, self);
		else if (e->code == KP_UI_HIT_TEST) return 1;
		return stock(ui, d, e);
	case KP_UI_CHAR: { /* Tab: the terrain; Shift+Tab: the minimap's colours */
		int32_t b = kp_ui_find_control(ui, self, BUTTON_TERRAIN);
		if ((e->key & 0xff) != KEY_TAB || kp_scalar(ui->game, KP_PAUSED) != 0) return 0;
		if (ui->input.keys[0]) {
			KpUiMinimap *m = mm(ui);
			if (++m->colour_mode > COLOUR_MODES - 1) m->colour_mode = 0;
			/* Modes 0 and 2 also redraw the whole screen (0x4cb6c0). TODO: mode 2 recolours the game view's sprites
			 * (SetDrawColor 0x41d740, 0x41d4f0: own 2, allied 7, else 0, found in the colour slots 0x5264fc) */
		} else {
			mm(ui)->terrain_hidden ^= 1;
		}
		redraw(ui, 0);
		if (b >= 0 && (ui->controls[b].flags & KP_UI_HOVER)) button_tooltip(ui, &ui->controls[b]);
		return 1;
	}
	default:
		return stock(ui, d, e);
	}
}

/* The first `size` pixels of a PCX of ui.kpd (SBmpLoadImage into a buffer of that size, 0x4444e0). */
static int pcx_head(KpUi *ui, const char *path, uint8_t *out, size_t size)
{
	const KpUiFile *f = kp_ui_file(ui->pcx, ui->pcx_count, path);
	int32_t w, h;
	uint8_t *pixels;
	if (f == NULL || kp_ui_pcx(f->data, f->size, &w, &h, NULL, 0) != 0) return -1;
	pixels = (uint8_t *)malloc((size_t)w * (size_t)h);
	if (pixels == NULL || kp_ui_pcx(f->data, f->size, &w, &h, pixels, (size_t)w * (size_t)h) != 0) {
		free(pixels);
		return -1;
	}
	memcpy(out, pixels, (size_t)w * (size_t)h < size ? (size_t)w * (size_t)h : size);
	free(pixels);
	return 0;
}

int kp_ui_set_minimap(KpUi *ui, const uint8_t *mmni, size_t mmni_size, const uint8_t *rgb, const uint8_t *cycled,
	const uint8_t *dark, const uint8_t *colour_slots)
{
	KpUiMinimap *m = mm(ui);
	uint8_t loaded[12];
	int i;
	m->mmni = mmni;
	m->mmni_size = mmni_size;
	m->dark = dark;
	/* LoadPlayerColorTables 0x41d570 and InitMinimap 0x46e260's game\tblink.pcx */
	if (pcx_head(ui, "game\\tminimap.pcx", loaded, sizeof loaded) != 0
		|| pcx_head(ui, "game\\tblink.pcx", m->blink_rows, sizeof m->blink_rows) != 0) {
		return -1;
	}
	memcpy(m->player_colours, loaded, sizeof loaded);
	if (colour_slots != NULL) { /* ApplyColorSlotPermutation 0x41d5d0 */
		for (i = 7; i >= 0; i--) {
			if (colour_slots[i] != i && colour_slots[i] < 12) {
				m->player_colours[i] = loaded[colour_slots[i]];
				m->colour_table[i] = colour_slots[i]; /* 0x461b70 */
			}
		}
	}
	/* InitMinimap 0x46e260's palette map: a colour-cycled index becomes the nearest static colour (0x4666c0) */
	for (i = 0; i < 256; i++) {
		int j;
		uint32_t best = 0xffffffffu;
		m->palette_map[i] = (uint8_t)i;
		if (cycled == NULL || !cycled[i]) continue;
		m->palette_map[i] = 0;
		for (j = 1; j < 256; j++) {
			int32_t dr = rgb[3 * j] - rgb[3 * i], dg = rgb[3 * j + 1] - rgb[3 * i + 1], db = rgb[3 * j + 2] - rgb[3 * i + 2];
			uint32_t d = (uint32_t)(dr * dr + dg * dg + db * db);
			if (cycled[j]) continue;
			if (d < best) {
				best = d;
				m->palette_map[i] = (uint8_t)j;
			}
		}
	}
	return 0;
}

int kp_ui_minimap_create(KpUi *ui)
{
	/* InitMinimap 0x46e260 */
	KpUiMinimap *m = mm(ui);
	int32_t d;
	init_state(ui);
	if (m->w == 0) {
		m->w = m->h = IMAGE_SIDE;
		m->divisor = 1;
	}
	d = kp_ui_load_dialog(ui, "rez\\minimap.bin");
	if (d < 0) return -1;
	m->dialog = d;
	m->hovered = -1;
	kp_ui_init_dialog(ui, d, dialog_interact);
	return m->control >= 0 ? 0 : -1;
}
