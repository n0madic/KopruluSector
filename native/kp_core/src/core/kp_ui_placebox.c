/* The building placement's preview (placebox.cpp, atlas P27 0x47cd80..0x47db80): the preview bitmaps of the placed
 * type (k 1) and of an add-on's parent (k 0), each with the type's ghost (frame 0 of its GRP in its owner's colours),
 * whose footprint cells the cursor's moves stipple green or red by the placement validator's codes, and screen layers
 * 3 + k that show them over the game view (kp_ui_canvas_game_view). */
#include "kp_ui.h"
#include "kp_game.h"
#include "kp_ids.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CELL 32
#define CELLS_W 6               /* the cells' rows of 0x675188 + 48k */
#define UNDRAWN 0x0c            /* a code no cell has: every cell is stippled at the next draw */
#define COLOUR_VALID 19         /* 0x6d237b */
#define COLOUR_INVALID 20       /* 0x6d237c */
#define OWNER_NONE 0xff

static int32_t place_w(KpGame *g, uint16_t t) { return (int16_t)KP_DAT(g, units, staredit_placement_size, t * 2); }
static int32_t place_h(KpGame *g, uint16_t t) { return (int16_t)KP_DAT(g, units, staredit_placement_size, t * 2 + 1); }
static int32_t round_up(int32_t v) { return (v + CELL - 1) / CELL * CELL; }

/* The type's ghost: frame 0 of the GRP of its flingy's sprite's image (0x40f640), from ui.kpd GHST. */
static const KpUiFile *ghost_grp(KpUi *ui, uint16_t type)
{
	KpGame *g = ui->game;
	char path[24];
	int64_t image = KP_DAT(g, sprites, image, KP_DAT(g, flingy, sprite, KP_DAT(g, units, graphics, type)));
	snprintf(path, sizeof path, "ghost\\%d", (int)image);
	return kp_ui_file(ui->ghosts, ui->ghost_count, path);
}

static void free_bitmap(KpUiPlacement *pl, int k)
{
	free(pl->bitmap[k].pixels);
	memset(&pl->bitmap[k], 0, sizeof pl->bitmap[k]);
	pl->shown[k] = 0;
}

void kp_ui_placement_free(KpUi *ui)
{
	free_bitmap(&ui->placement, 0);
	free_bitmap(&ui->placement, 1);
	ui->placement.anchored = 0;
}

/* DrawPlacementGhostImage 0x47cd80: frame 0 at its own offsets in the colours of the status unit's sprite's player
 * (a landing, or order 36 with type 0) or of its owner. */
static void draw_ghost(KpUi *ui, KpUiSurface *target, uint16_t type)
{
	KpGame *g = ui->game;
	const KpUiFile *f = ghost_grp(ui, type);
	int64_t status = ui->hud.status_unit, owner;
	uint8_t order = (uint8_t)kp_scalar(g, KP_PLACEMENT_ORDER), remap[256];
	int32_t w, h, crop[4];
	KpUiSurface *saved;
	if (f == NULL || f->size < 14 || status == 0 || kp_ui_grp_frame_size(f->data, f->size, 0, &w, &h) != 0) return;
	if (order == KP_ORD_BUILDING_LAND || (order == KP_ORD_PLACE_ADDON && type == 0)) {
		owner = g->w->spr.owner[kp_unit_sprite(g, status)];
	} else {
		owner = g->u.owner[status];
	}
	memcpy(remap, ui->remap, sizeof remap);
	memcpy(ui->remap + 8, ui->player_colours[owner % 12], 8); /* SetDrawColor 0x41d740 */
	saved = ui->text.target;
	ui->text.target = target;
	crop[0] = 0;
	crop[1] = 0;
	crop[2] = w;
	crop[3] = h;
	kp_ui_draw_grp_rle(ui, f->data[6], f->data[7], f->data, f->size, 0, crop);
	ui->text.target = saved;
	memcpy(ui->remap, remap, sizeof remap);
}

/* PreparePlacementBitmap 0x47cec0: bitmap k of the type, as wide and high as its ghost or its footprint, whichever
 * is larger, each rounded up to whole cells, cleared, with the ghost; every cell stippled at the next draw. */
void kp_ui_placement_prepare(KpUi *ui, int k, uint16_t type)
{
	KpUiPlacement *pl = &ui->placement;
	const KpUiFile *f = ghost_grp(ui, type);
	int32_t gw = 0, gh = 0, w, h;
	if (f != NULL && f->size >= 6) {
		gw = f->data[2] | f->data[3] << 8;
		gh = f->data[4] | f->data[5] << 8;
	}
	w = round_up(gw > place_w(ui->game, type) ? gw : place_w(ui->game, type));
	h = round_up(gh > place_h(ui->game, type) ? gh : place_h(ui->game, type));
	free_bitmap(pl, k);
	pl->type[k] = type;
	if (w > 0 && h > 0 && (pl->bitmap[k].pixels = (uint8_t *)calloc((size_t)w * (size_t)h, 1)) != NULL) {
		pl->bitmap[k].w = w;
		pl->bitmap[k].h = h;
		draw_ghost(ui, &pl->bitmap[k], type);
	}
	memset(pl->drawn[k], UNDRAWN, sizeof pl->drawn[k]);
	pl->shown[k] = pl->bitmap[k].pixels != NULL;
	pl->positioned = 0;
}

/* CheckPlacementAt 0x47db80 at tile (tx, ty): the validator for the placed type (entry 1) and an add-on's parent at its
 * place for the add-on (entry 0); returns the first error, 0 fits. */
static int64_t check_placement(KpUi *ui, int64_t tx, int64_t ty)
{
	KpGame *g = ui->game;
	KpUiPlacement *pl = &ui->placement;
	uint16_t type = (uint16_t)kp_scalar(g, KP_PLACEMENT_TYPE), parent = ui->input.placement_parent;
	int64_t status = ui->hud.status_unit, owner = status ? g->u.owner[status] : g->local_player, e, e2 = 0;
	e = kp_validate_placement_cells(g, status, owner, tx, ty, type, 1, 0, 0, pl->cells[1]);
	if (parent != 0) { /* add-on mode: the parent at its place for the add-on */
		e2 = kp_validate_placement_cells(g, status, owner, tx - (int16_t)KP_DAT(g, units, addon_position, type * 2) / 32,
			ty - (int16_t)KP_DAT(g, units, addon_position, type * 2 + 1) / 32, parent, 1, 0, 0, pl->cells[0]);
	}
	return e != 0 ? e : e2;
}

/* DrawPlacementGrid 0x47d920: layer 3 + k centred on the footprint at the snapped screen point (x, y); each footprint
 * cell whose code changed is stippled (StipplePlacementCell 0x405c12: the cell's pixels whose row and column add up to
 * an even number) green when it fits, red when it does not. */
static void draw_grid(KpUi *ui, int k, int32_t x, int32_t y)
{
	KpUiPlacement *pl = &ui->placement;
	KpUiSurface *b = &pl->bitmap[k];
	int32_t w = place_w(ui->game, pl->type[k]) / CELL, h = place_h(ui->game, pl->type[k]) / CELL, ox, oy, r, c, i, j;
	if (b->pixels == NULL) return;
	ox = (b->w - CELL * w) / 2;
	oy = (b->h - CELL * h) / 2;
	pl->x[k] = x - ox;
	pl->y[k] = y - oy;
	pl->shown[k] = 1;
	for (r = 0; r < h && r * CELLS_W < KP_PLACEMENT_CELLS; r++) {
		for (c = 0; c < w && c < CELLS_W; c++) {
			uint8_t v = pl->cells[k][r * CELLS_W + c], colour;
			if (v == pl->drawn[k][r * CELLS_W + c]) continue;
			pl->drawn[k][r * CELLS_W + c] = v;
			colour = ui->ui_colours[v ? COLOUR_INVALID : COLOUR_VALID];
			for (j = 0; j < CELL; j++) {
				for (i = (j & 1); i < CELL; i += 2) {
					int32_t px = ox + CELL * c + i, py = oy + CELL * r + j;
					if (px < b->w && py < b->h) b->pixels[(size_t)py * (size_t)b->w + (size_t)px] = colour;
				}
			}
		}
	}
}

int kp_ui_placement_tile(const KpUi *ui, int32_t *x, int32_t *y, int64_t *tx, int64_t *ty)
{
	const KpUiInput *p = &ui->input;
	if ((uint16_t)*x >= ui->view_w || (uint16_t)*y >= ui->view_h) return 0;
	*tx = (*x >> 5) + (p->screen_x >> 5);
	*ty = (*y >> 5) + (p->screen_y >> 5);
	*x = (int32_t)*tx * CELL - p->screen_x;
	*y = (int32_t)*ty * CELL - p->screen_y;
	if (*x < 0) { /* the view's partial first column and row snap to the next tile */
		*x += CELL;
		++*tx;
	}
	if (*y < 0) {
		*y += CELL;
		++*ty;
	}
	return 1;
}

int kp_ui_placement_at(const KpUi *ui, int32_t x, int32_t y)
{
	const KpUiPlacement *pl = &ui->placement;
	KpGame *g = ui->game;
	int32_t left, top;
	if (g == NULL || kp_scalar(g, KP_PLACEMENT_ACTIVE) != 1 || !pl->shown[1]) return 0;
	left = (int32_t)pl->tile_x * CELL - ui->input.screen_x;
	top = (int32_t)pl->tile_y * CELL - ui->input.screen_y;
	return x >= left && y >= top && x < left + round_up(place_w(g, pl->type[1]))
		&& y < top + round_up(place_h(g, pl->type[1]));
}

void kp_ui_placement_anchor(KpUi *ui, int64_t parent)
{
	KpUiPlacement *pl = &ui->placement;
	KpGame *g = ui->game;
	uint16_t type = (uint16_t)kp_scalar(g, KP_PLACEMENT_TYPE), parent_type = (uint16_t)g->u.type[parent];
	int32_t sprite = kp_unit_sprite(g, parent), x, y;
	int64_t tx, ty;
	pl->anchored = 0;
	/* the add-on's top-left tile: the parent's top-left (its sprite's centre less half its footprint) and the offset the
	 * add-on takes from it */
	tx = (g->w->spr.x[sprite] - place_w(g, parent_type) / 2 + (int16_t)KP_DAT(g, units, addon_position, type * 2)) / CELL;
	ty = (g->w->spr.y[sprite] - place_h(g, parent_type) / 2 + (int16_t)KP_DAT(g, units, addon_position, type * 2 + 1)) / CELL;
	/* the middle of the tile's first cell: ScreenToPlacementTile 0x47d490 reads it as the tile */
	x = (int32_t)(tx - (ui->input.screen_x >> 5)) * CELL + CELL / 2;
	y = (int32_t)(ty - (ui->input.screen_y >> 5)) * CELL + CELL / 2;
	if ((uint16_t)x >= ui->view_w || (uint16_t)y >= ui->view_h) return;
	ui->mouse_x = (int16_t)x;
	ui->mouse_y = (int16_t)y;
	kp_ui_set_over_console(ui, 0); /* the pointer was on the button that began the placement: its answer is stale */
	pl->anchored = 1;
	pl->anchor_x = x;
	pl->anchor_y = y;
}

int kp_ui_placement_take_anchor(KpUi *ui, int32_t *x, int32_t *y)
{
	KpUiPlacement *pl = &ui->placement;
	if (!pl->anchored) return 0;
	pl->anchored = 0;
	*x = pl->anchor_x;
	*y = pl->anchor_y;
	return 1;
}

void kp_ui_placement_update(KpUi *ui)
{
	KpUiPlacement *pl = &ui->placement;
	KpUiInput *p = &ui->input;
	KpGame *g = ui->game;
	int32_t x = ui->mouse_x, y = ui->mouse_y;
	int64_t tx, ty;
	if (g == NULL || kp_scalar(g, KP_PLACEMENT_ACTIVE) != 1) return;
	/* a point on a panel is a console point, not the view's (the layout's view is larger than the original's, so a
	 * panel's point in its dialog's coordinates would fall inside it): the preview stays where the pointer left it
	 * (phase 10: a touch on the panel's cancel button must not carry it along) */
	if (pl->positioned && kp_ui_over_console(ui, x, y)) return;
	if (!kp_ui_placement_tile(ui, &x, &y, &tx, &ty)) { /* the layers hide, every cell drawn again later */
		int k;
		for (k = 0; k < 2; k++) {
			memset(pl->drawn[k], UNDRAWN, sizeof pl->drawn[k]);
			pl->shown[k] = 0;
		}
		return;
	}
	pl->positioned = 1;
	pl->tile_x = tx;
	pl->tile_y = ty;
	pl->error = check_placement(ui, tx, ty);
	draw_grid(ui, 1, x, y);
	if (p->placement_parent != 0) {
		uint16_t type = (uint16_t)kp_scalar(g, KP_PLACEMENT_TYPE);
		draw_grid(ui, 0, x - (int16_t)KP_DAT(g, units, addon_position, type * 2),
			y - (int16_t)KP_DAT(g, units, addon_position, type * 2 + 1));
	}
}
