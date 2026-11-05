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

/* The GL flavour of the context: GLES 3.0 (WebGL2 on the web), desktop GL core with the GLSL version (one of
 * KP_GFX_GLSL_CORE). */
typedef struct {
	int gles;
	int glsl_version;
} KpGfxApi;

/* The desktop GL core contexts the host asks for, as their GLSL versions (the GL version times 100): 4.1 (macOS, and
 * where a driver has no 3.3 core) and 3.3; test_gfx compiles the shaders for each. */
#define KP_GFX_GLSL_41 410
#define KP_GFX_GLSL_33 330
#define KP_GFX_GLSL_CORE { KP_GFX_GLSL_41, KP_GFX_GLSL_33 }

KpGfx *kp_gfx_new(const KpGfxApi *api, char *err, size_t err_size);
void kp_gfx_free(KpGfx *g);
/* Every resource dropped (the host uploads them again): the context was lost and made again, or the art changes (the
 * objects the current context still has are deleted). A reset that fails leaves no backend: every call after it draws
 * and uploads nothing, and the reads fail. */
int kp_gfx_reset(KpGfx *g, char *err, size_t err_size);
/* Whether the current context no longer has kp_gfx's objects (a phone's context lost in the background). */
int kp_gfx_lost(const KpGfx *g);
/* The textures that could not be made since the last call (out of memory, a size past the context's limits), the
 * last one described in msg: each draws as a zero texel (its pass shows nothing) until an upload makes it. */
int kp_gfx_take_failures(KpGfx *g, char *msg, size_t size);

/* --- resources (each upload replaces the previous one) --- */

/* The lookup rows of the mission's tileset (the remap and the destination tables, the trans50 and brightness rows). */
void kp_gfx_lookups(KpGfx *g, const KpLookups *l);
/* The palette the palette pass maps through (768 bytes). */
void kp_gfx_palette(KpGfx *g, const uint8_t *rgb);
/* The gamma option (60..140, kp_palette_set_brightness's) on the HD interface's art, which is not in the palette. */
void kp_gfx_gamma(KpGfx *g, int32_t brightness);
/* The palette last given (256 RGBA entries; the checks'). */
const uint8_t *kp_gfx_palette_rgba(const KpGfx *g);
/* The atlas pages of every loaded pack (8-bit indices, w x h each, rows top first), in the frame table's page order. */
void kp_gfx_pages(KpGfx *g, int count, int32_t w, int32_t h, const uint8_t *const *pages);
void kp_gfx_frame_table(KpGfx *g, const KpFrameTable *t);
/* The image data: all of it (a new world, a restored context), or the runs of its texel rows (a tick's changes). */
void kp_gfx_image_data(KpGfx *g, const KpImageData *d);
void kp_gfx_image_rows(KpGfx *g, const KpImageData *d, const KpRowRange *ranges, int count);
/* The health bars: all of them (a new world, a restored context), or the slots redrawn (dirty_lo..dirty_hi-1). */
void kp_gfx_bars(KpGfx *g, const KpBars *b);
void kp_gfx_bar_slots(KpGfx *g, const KpBars *b);
/* Whether the world images are drawn (the pages, frame table and image data are up). */
void kp_gfx_world(KpGfx *g, int on);
/* The terrain: the megatile sheet (8-bit, columns megatiles per row; creep's edge frames in its cells from edge_base on)
 * and the map's cells (w x h, two u16 each: the megatile word and 1 + the creep edge frame drawn over it, 0 none:
 * kp_terrain_edge). NULL cells: no terrain; NULL sheet: the cells alone. */
void kp_gfx_terrain(KpGfx *g, const uint8_t *sheet, int32_t sheet_w, int32_t sheet_h, int32_t columns, int32_t edge_base,
	const uint16_t *cells, int32_t map_w, int32_t map_h);
/* The fog's level map (its changed rectangle) and the tileset's dark rows (uploaded when another table is given; a
 * table's contents must not change); f NULL or off: no fog. */
void kp_gfx_fog(KpGfx *g, KpFogLayer *f, const uint8_t *dark);
/* The interface's canvas (its changed rectangle, the blend row); NULL: none. */
void kp_gfx_canvas(KpGfx *g, const KpCanvas *c);
/* The vector text's atlas (when it changed or was lost) and the frame's quads (kp_vtext_quads); NULL: none drawn. */
void kp_gfx_text(KpGfx *g, KpVText *v);
/* The cursor's frame (when it changed) at the logical point (x, y) of its top-left; NULL: none. */
void kp_gfx_cursor(KpGfx *g, KpCursor *c, int32_t x, int32_t y);

/* --- the HD world (ADR 0013): StarCraft: Remastered's art drawn instead of the index world when it is up and on --- */

/* The slots of the HD pages (kp_hd_cache, stage R4a): count layers of page_size squared, the diffuse and team colour
 * blocks of a pack format (KP_HD_FORMAT_*: kp_hd_layer_codec; blocks set, and the context must take them: kp_gfx_hd_takes),
 * else RGBA8 and one byte; the first lit of them (blocks only, else none) with the lit
 * pages' lighting layers too (KP_HD_LIGHT_*: their sprites drawn lit); their contents are lost. -1 when the context
 * cannot make them (out of the GPU's memory): no slots. */
int kp_gfx_hd_slots(KpGfx *g, int32_t count, int32_t lit, int32_t page_size, int format, int blocks);
/* Whether the context takes the HD pages of a pack format as blocks (else decoded: RGBA8 diffuse, the mask's red). */
int kp_gfx_hd_takes(KpGfx *g, int format);
/* One page into a slot: its blocks, or its decoded texels (as the slots were made), and in a lit slot (one of the
 * first lit) its lighting layers' blocks (KP_HD_LIGHT_*; only a lit slot reads them, and draws its page lit). */
void kp_gfx_hd_page(KpGfx *g, int32_t slot, const uint8_t *diffuse, const uint8_t *team,
	uint8_t *const light[KP_HD_LIGHTS]);
/* Each HD page's slot, -1 when not resident (its images' SD frames are drawn). */
void kp_gfx_hd_page_slots(KpGfx *g, const int32_t *slot_of, int32_t pages);
/* The HD frame table (kp_hd_frame_table_build) and the art's scale (HD pixels a logical pixel). */
void kp_gfx_hd_frame_table(KpGfx *g, const KpFrameTable *t, int scale);
/* The team colour of each remap row (a player's minimap colour, minimap: tminimap.pcx's index per colour; without one its
 * brightest tunit colour; the hallucination row marked) and the solid colour of each selection row (its circles'
 * colour). */
void kp_gfx_hd_colors(KpGfx *g, const KpLookups *l, const uint8_t *rgb, const uint8_t *minimap, size_t minimap_size);
/* The atlas of the map's HD megatiles and their slots (kp_hd_terrain_build), with their water masks and the tileset's
 * creep edges; NULL: none. */
void kp_gfx_hd_terrain(KpGfx *g, const KpHdTerrain *a);
/* The atlas built again (another megatile shown): its tiles, water masks and slots; the tileset's edges stay. */
void kp_gfx_hd_terrain_atlas(KpGfx *g, const KpHdTerrain *a);
/* The HD terrain's cells' slots again (kp_hd_terrain_relink). */
void kp_gfx_hd_terrain_slots(KpGfx *g, const KpHdTerrain *a);
/* The water's normal maps (stage R4c); NULL: none (the water stays still). */
void kp_gfx_hd_water(KpGfx *g, const KpHdWater *w);
/* The palette the HD art was drawn for (the tileset's, before the brightness option) and the entries its tone skips
 * (the colour cycles, entry 0): the HD world follows the uploaded palette's fades and brightness (kp_palette_tone). */
void kp_gfx_hd_tone_base(KpGfx *g, const uint8_t rgb[768], const uint8_t skip[256]);
#define KP_HD_WATER_PERIOD 3600.0 /* the water's clock wraps (the shader's float precision) */

/* --- the HD interface (ADR 0013 R4d): StarCraft: Remastered's art for the canvas's items (kp_app_hd_ui.c) --- */

/* The atlas of the interface's HD images: layers of size squared RGBA8 texels (made again: their contents are lost). */
void kp_gfx_ui_atlas(KpGfx *g, int32_t size, int32_t layers);
void kp_gfx_ui_atlas_put(KpGfx *g, int32_t layer, int32_t x, int32_t y, int32_t w, int32_t h, const uint8_t *rgba);
/* The canvas's items 1..count, KP_GFX_UI_ITEM_FLOATS each from item 1 (item 0 unused): their HD image's rectangle on
 * the logical screen (x, y, w, h), its texels in the atlas (u0, v0, u1, v1), its layer (negative, -1 - layer: the image
 * replaces its texels, the world where it is transparent or does not reach; else it lies over the SD texels), tint
 * (r, g, b; r < 0 none; r -2: a wireframe, its square layers side by side from the texels, each in a part's palette
 * colour, three indices to a float in y, z and w of the fourth texel), the item drawn under it first (0 none) and, for a
 * tiled item that is no wireframe, the SD frame's w and h it repeats by (y and z of the fourth texel; 0 none) and the
 * first tile's SD frame's top-left (x and y of the fifth); count 0: none (the canvas is drawn in SD). */
#define KP_GFX_UI_ITEM_FLOATS 20
void kp_gfx_ui_items(KpGfx *g, const float *items, int32_t count);
/* The cursor's HD frame: its rectangle on the logical screen, its texels in the atlas (u0, v0, u1, v1) and layer;
 * NULL: the SD cursor. */
void kp_gfx_ui_cursor(KpGfx *g, const float rect[4], const float src[4], float layer);
/* Draws the HD world (when its art is up) instead of the index world; the index captures (world_only) keep the index
 * world. */
void kp_gfx_hd(KpGfx *g, int on);
/* The world's modern effects (ADR 0013, stage R5), set every frame: sd_scale > 0 draws the original's art through the
 * RGBA path of the HD world at that many target pixels a logical pixel (with kp_gfx_hd on and no HD art up); smooth:
 * the fog of war as a smooth darkening (else, over the original's art in RGBA, the original's dark rows per fragment;
 * the HD art's fog is always smooth); water: the water's surface where the tileset's water masks are up. A new gfx
 * draws the HD art's effects (smooth, water). */
void kp_gfx_effects(KpGfx *g, int32_t sd_scale, int smooth, int water);
int kp_gfx_hd_ready(const KpGfx *g);

/* --- the frame --- */

typedef struct {
	int32_t win_w, win_h;     /* the picture's size in the window's drawable, in pixels */
	int32_t win_x, win_y;     /* its offset from the drawable's left and bottom (the rest black) */
	int32_t full_w, full_h;   /* the whole drawable (0: the picture's size) */
	int mode;                 /* KP_SCALE_INTEGER or KP_SCALE_SHARP (resolved) */
	double scale;
	int32_t world_w, world_h; /* the logical screen */
	int64_t cam_x, cam_y;     /* the view's top-left map pixel */
	double interp;            /* the world images' interpolation between the last two ticks */
	const KpPlan *plan;       /* the uploaded image data's composition plan and slots in use (NULL: no images) */
	int world_only;           /* no canvas (the index captures) */
	double time;              /* seconds: the HD water's clock */
} KpGfxFrame;

/* Draws the frame into the window's framebuffer. */
void kp_gfx_frame(KpGfx *g, const KpGfxFrame *f);

/* A cinematic's picture instead of a frame, converted to RGB by the GPU: v (its size w x h; NULL: the last one again,
 * the drawable black when it was of another size or w or h is 0) over the window's rectangle f gives (win_x, win_y,
 * win_w, win_h, full_w, full_h; the rest of the drawable black), its planes sampled between their texels. */
void kp_gfx_video(KpGfx *g, const KpVideoPicture *v, int32_t w, int32_t h, const KpGfxFrame *f);
/* The subtitle kp_gfx_video draws over the picture from now on: rgba (premultiplied, w x h, rows top first; NULL: none)
 * in rect (left, top, right, bottom of the picture, 0..1 from its top-left), sampled between its texels. */
void kp_gfx_video_caption(KpGfx *g, const uint8_t *rgba, int32_t w, int32_t h, const float rect[4]);
/* The index target's indices x..x+w-1, y..y+h-1 (GL rows, bottom-up) of the last frame into out (w * h); -1 none. */
int kp_gfx_read_index(KpGfx *g, int32_t x, int32_t y, int32_t w, int32_t h, uint8_t *out);
/* The window's pixels (RGBA, rows bottom-up) of the last frame; -1 none. */
int kp_gfx_read_window(KpGfx *g, int32_t x, int32_t y, int32_t w, int32_t h, uint8_t *out);
/* The context's GL version string. */
const char *kp_gfx_version(void);

#ifdef __cplusplus
}
#endif

#endif
