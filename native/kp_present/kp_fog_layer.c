/*
 * The fog of war over the game view (plan phase 8 item 5, atlas P23/P24): kp_core's fog view builds the original's
 * level map from the local player's tile visibility around the camera; this layer keeps it as a texture's contents (one
 * texel per 8 x 8-pixel block, level in R) with the rectangle that changed since the backend last took it. A moved
 * camera tile or a resized view rebuilds the whole map. The dark rows of the tileset (256 x 32) are the other texture
 * of the fog pass.
 */
#include "kp_present.h"

#include <string.h>

#include "kp_game.h"

void kp_fog_layer_init(KpFogLayer *f)
{
	memset(f, 0, sizeof *f);
	kp_fog_view_init(&f->view);
}

static void grow_dirty(KpFogLayer *f, int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
	if (f->dirty[2] == 0) {
		f->dirty[0] = x0;
		f->dirty[1] = y0;
		f->dirty[2] = x1;
		f->dirty[3] = y1;
		return;
	}
	if (x0 < f->dirty[0]) f->dirty[0] = x0;
	if (y0 < f->dirty[1]) f->dirty[1] = y0;
	if (x1 > f->dirty[2]) f->dirty[2] = x1;
	if (y1 > f->dirty[3]) f->dirty[3] = y1;
}

void kp_fog_layer_update(KpFogLayer *f, KpGame *g, int64_t cam_x, int64_t cam_y, int64_t w, int64_t h)
{
	KpFogVision vision;
	int64_t rect[4];
	int changed;
	vision.tiles = g->terrain.tiles;
	vision.map_w = g->map_w;
	vision.map_h = g->map_h;
	vision.not_visible = g->local_vision;
	vision.not_explored = g->local_vision << 8;
	vision.replay = g->w->replay != 0;
	vision.replay_vision = g->w->local_vision;
	vision.replay_reveal = g->w->replay_reveal;
	changed = kp_fog_view_update(&f->view, &vision, cam_x, cam_y, w, h, rect);
	if (changed > 0) {
		int32_t lw = (int32_t)f->view.level_w, lh = (int32_t)f->view.level_h;
		if (lw != f->width || lh != f->height) {
			f->width = lw;
			f->height = lh;
			grow_dirty(f, 0, 0, lw, lh);
		} else {
			grow_dirty(f, (int32_t)rect[0], (int32_t)rect[1], (int32_t)rect[2], (int32_t)rect[3]);
		}
		f->block_x = (int32_t)(f->view.tile_x * 4);
		f->block_y = (int32_t)(f->view.tile_y * 4);
	}
	f->on = 1;
	f->map_x = (int32_t)cam_x;
	f->map_y = (int32_t)cam_y;
}

void kp_fog_layer_taken(KpFogLayer *f)
{
	memset(f->dirty, 0, sizeof f->dirty);
}
