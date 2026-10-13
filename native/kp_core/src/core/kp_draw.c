/* The collection of the native draw list (kp_images.h; presentation/draw_list.lua M.build, steps 2 and 3): the
 * sprites the local player sees, from the first drawn row down, in the order of the native depth key
 * (GetSpriteDepthKey 0x4160b0, ascending, as DrawSortedSprites 0x415b30 draws them; the key ORs the sprite index
 * into its low bits), and each sprite's images from the tail to the head (DrawSprite 0x415a80) that can be drawn:
 * a draw method with a material, not hidden, a virtual header or a GRP frame that exists. Presentation turns the
 * records into materials, ranks and composition windows. */
#include "kp_images_internal.h"

#include <stdlib.h>

static int by_key(const void *a, const void *b)
{
	uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
	return x < y ? -1 : x > y;
}

int32_t kp_draw_collect(KpImages *w, int32_t first_row, uint32_t methods, int32_t *out, int32_t max)
{
	uint64_t keys[KP_SPRITES];
	int32_t n = 0, at = 0, r, k;
	if (first_row < 0) first_row = 0;
	for (r = first_row; r < w->map_h && r < KP_ROWS; r++) {
		int32_t s = w->rows[r].head;
		while (s != KP_NONE) {
			s = kp_check_sprite(w, s);
			if ((w->spr.visibility[s] & w->local_vision) != 0) {
				if (n >= KP_SPRITES) kp_images_fail(w, "draw: more sprites in the rows than records");
				keys[n++] = (uint64_t)kp_sprite_depth_key(w, s) << 12 | (uint64_t)s;
			}
			s = w->spr.next[s];
		}
	}
	qsort(keys, (size_t)n, sizeof keys[0], by_key);
	for (k = 0; k < n; k++) {
		int32_t s = (int32_t)(keys[k] & 0xfff), main = w->spr.main[s], i, header = at, count = 0;
		if (at + KP_DRAW_SPRITE > max) return -1;
		out[at++] = s;
		out[at++] = w->spr.owner[s];
		out[at++] = main == KP_NONE ? -1 : w->img.method[kp_check_image(w, main)];
		out[at++] = 0; /* image count */
		for (i = w->spr.images[s].tail; i != KP_NONE; i = w->img.prev[i]) {
			int32_t method, g, frame, fw, fh, virtual_;
			i = kp_check_image(w, i);
			method = w->img.method[i];
			g = w->img.grp[i];
			frame = w->img.frame[i];
			virtual_ = g == KP_GRP_VIRTUAL && w->has_vgrp[i];
			if (method < 0 || method > 31 || (methods & (1u << method)) == 0 || (w->img.flags[i] & KP_F_HIDDEN) != 0) continue;
			if (virtual_) {
				fw = w->vgrp_fw[i];
				fh = w->vgrp_fh[i];
			} else {
				int x, y, gw, gh;
				if (g <= 0) continue;
				if ((uint32_t)g > w->grp->grp_count) kp_images_fail(w, "draw: image %d has no GRP %d", (int)i, (int)g);
				if (frame < 0 || (uint32_t)frame >= w->grp->grps[g].frames) continue;
				kp_grpmeta_frame(w->grp, (uint32_t)g, (uint32_t)frame, &x, &y, &gw, &gh);
				fw = gw;
				fh = gh;
			}
			if (at + KP_DRAW_IMAGE > max) return -1;
			out[at++] = i;
			out[at++] = method;
			out[at++] = w->img.param[i];
			out[at++] = w->img.flags[i];
			out[at++] = g;
			out[at++] = frame;
			out[at++] = w->img.x[i];
			out[at++] = w->img.y[i];
			out[at++] = w->img.id[i];
			out[at++] = fw;
			out[at++] = fh;
			out[at++] = virtual_;
			count++;
		}
		out[header + 3] = count;
	}
	return at;
}
