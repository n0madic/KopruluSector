/*
 * kp_gfx: the index-space drawing of the game (plan phase S, stage S4; ADR 0012): the terrain and the world images into
 * an index target composed by depth rank in windows and destination layers (ADR 0006), the fog of war, the interface's
 * canvas and cursor, then the palette pass and the scaling to the window. Two layers: the passes (kp_gfx_passes.c) over
 * a narrow backend (kp_gfx_internal.h: textures, targets, programs, draws, copies, readback), today GLES 3.0 / WebGL2 /
 * desktop GL (kp_gfx_gl.c); a native SDL_GPU backend can come under the same passes (stage S9).
 *
 * The data comes from kp_present (CPU buffers); kp_gfx keeps no game state, only the textures made from them, so a
 * lost context is redrawn after the host uploads everything again (kp_gfx_reset).
 */
#ifndef KP_GFX_H
#define KP_GFX_H

#include "kp_present.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct KpGfx KpGfx;

/* The GL flavour of the context: GLES 3.0 (WebGL2 on the web), desktop GL core with the GLSL version (410 or 330). */
typedef struct {
	int gles;
	int glsl_version;
} KpGfxApi;

KpGfx *kp_gfx_new(const KpGfxApi *api, char *err, size_t err_size);
void kp_gfx_free(KpGfx *g);
/* The context was lost and made again: every resource is dropped (the host uploads them again). A reset that fails
 * leaves no backend: every call after it draws and uploads nothing, and the reads fail. */
int kp_gfx_reset(KpGfx *g, char *err, size_t err_size);
/* Whether the current context no longer has kp_gfx's objects (a phone's context lost in the background). */
int kp_gfx_lost(const KpGfx *g);

/* --- resources (each upload replaces the previous one) --- */

/* The lookup rows of the mission's tileset (the remap and the destination tables, the trans50 and brightness rows). */
void kp_gfx_lookups(KpGfx *g, const KpLookups *l);
/* The palette the palette pass maps through (768 bytes). */
void kp_gfx_palette(KpGfx *g, const uint8_t *rgb);
/* The palette last given (256 RGBA entries; the checks'). */
const uint8_t *kp_gfx_palette_rgba(const KpGfx *g);
/* The atlas pages of every loaded pack (8-bit indices, w x h each, rows top first), in the frame table's page order. */
void kp_gfx_pages(KpGfx *g, int count, int32_t w, int32_t h, const uint8_t *const *pages);
void kp_gfx_frame_table(KpGfx *g, const KpFrameTable *t);
/* The image data: all of it (a new world, a restored context), or its texel rows row..row+rows-1 (a tick's changes). */
void kp_gfx_image_data(KpGfx *g, const KpImageData *d);
void kp_gfx_image_rows(KpGfx *g, const KpImageData *d, int32_t row, int32_t rows);
/* The health bars: all of them (a new world, a restored context), or the slots redrawn (dirty_lo..dirty_hi-1). */
void kp_gfx_bars(KpGfx *g, const KpBars *b);
void kp_gfx_bar_slots(KpGfx *g, const KpBars *b);
/* Whether the world images are drawn (the pages, frame table and image data are up). */
void kp_gfx_world(KpGfx *g, int on);
/* The terrain: the megatile sheet (8-bit, columns megatiles per row) and the map's megatile ids (w x h). NULL ids: no
 * terrain. */
void kp_gfx_terrain(KpGfx *g, const uint8_t *sheet, int32_t sheet_w, int32_t sheet_h, int32_t columns,
	const uint16_t *ids, int32_t map_w, int32_t map_h);
/* The fog's level map (its changed rectangle) and the tileset's dark rows (uploaded when another table is given; a
 * table's contents must not change); f NULL or off: no fog. */
void kp_gfx_fog(KpGfx *g, KpFogLayer *f, const uint8_t *dark);
/* The interface's canvas (its changed rectangle, the blend row); NULL: none. */
void kp_gfx_canvas(KpGfx *g, const KpCanvas *c);
/* The vector text's atlas (when it changed or was lost) and the frame's quads (kp_vtext_quads); NULL: none drawn. */
void kp_gfx_text(KpGfx *g, KpVText *v);
/* The cursor's frame (when it changed) at the logical point (x, y) of its top-left; NULL: none. */
void kp_gfx_cursor(KpGfx *g, KpCursor *c, int32_t x, int32_t y);

/* --- the frame --- */

typedef struct {
	int32_t win_w, win_h;     /* the window's drawable size in pixels */
	int mode;                 /* KP_SCALE_INTEGER or KP_SCALE_SHARP (resolved) */
	double scale;
	int32_t world_w, world_h; /* the logical screen */
	int64_t cam_x, cam_y;     /* the view's top-left map pixel */
	double interp;            /* the world images' interpolation between the last two ticks */
	const KpPlan *plan;       /* the uploaded image data's composition plan */
	int world_only;           /* no canvas (the index captures) */
} KpGfxFrame;

/* Draws the frame into the window's framebuffer. */
void kp_gfx_frame(KpGfx *g, const KpGfxFrame *f);
/* The index target's indices x..x+w-1, y..y+h-1 (GL rows, bottom-up) of the last frame into out (w * h); -1 none. */
int kp_gfx_read_index(KpGfx *g, int32_t x, int32_t y, int32_t w, int32_t h, uint8_t *out);
/* The window's pixels (RGBA, rows bottom-up) of the last frame; -1 none. */
int kp_gfx_read_window(KpGfx *g, int32_t w, int32_t h, uint8_t *out);
/* The context's GL version string. */
const char *kp_gfx_version(void);

#ifdef __cplusplus
}
#endif

#endif
