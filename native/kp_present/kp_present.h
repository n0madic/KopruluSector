/*
 * kp_present: the presentation of the game and its interface in plain C (plan phase S, stage S2; ADR 0012). It
 * turns the native core's state (kp_core: the images world, the fog view, the interface's canvas) into CPU buffers
 * and a frame plan that a drawing backend (native/kp_gfx) uploads and draws, and it reads the data the drawing
 * needs (the palettes, the tilesets, the resource packs' frame tables). No SDL, no GL: the C suites test it headless.
 *
 * Its parts: the draw list and its composition plan (kp_draw_list.c, ADR 0006), the lookup rows (kp_lookups.c), the
 * palette with its cycles, gamma and fades (kp_palette.c), the health bars (kp_health_bar.c), the world images' data
 * (kp_world_images.c), the packs and the frame table (kp_packs.c), the fog's level map (kp_fog_layer.c), the layouts
 * (kp_layout.c), the canvas and the cursor (kp_canvas.c), the terrain's tile map (kp_tileset.c); the HD art (ADR 0013):
 * the HD containers (kp_hd.c), the HD frame table and terrain atlas (kp_hd_render.c), the HD pages' residency
 * (kp_hd_cache.c), the HD interface and portraits (kp_hd_ui.c), the BCn and ETC2 blocks (kp_bcn.c, kp_etc.c); and the
 * vector text (kp_vtext.c). The helpers its files share are in kp_present_internal.h (kp_present_common.c).
 *
 * Coordinates: map pixels have row 0 at the top; the world's y grows upward, world y = KP_WORLD_TOP - map y (the
 * terrain template is 256 x 256 tiles of 32 pixels), and the index target's row 0 is the view's bottom row.
 */
#ifndef KP_PRESENT_H
#define KP_PRESENT_H

#include <stddef.h>
#include <stdint.h>

#include "kp_files.h"
#include "kp_kpf.h"
#include "kp_images.h"
#include "kp_mission.h"
#include "kp_fog_view.h"
#include "kp_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KP_WORLD_TEMPLATE 256
#define KP_WORLD_TILE 32
#define KP_WORLD_TOP (KP_WORLD_TEMPLATE * KP_WORLD_TILE)

/* A map rectangle (left, top inclusive; right, bottom exclusive). */
typedef struct {
	int32_t left, top, right, bottom;
} KpRect;

/* a / b rounded toward negative infinity (b != 0). */
static inline int32_t kp_floor_div(int32_t a, int32_t b)
{
	int32_t q = a / b;
	if ((a % b != 0) && ((a < 0) != (b < 0))) q--;
	return q;
}

/* ------------------------------------------------------------------------------------------------------------- */
/* The draw list (kp_draw_list.c, ADR 0006)                                                                        */
/* ------------------------------------------------------------------------------------------------------------- */

/* Material families: overwriting images write their pixel; destination images map the pixel below through a table;
 * distortion images read displaced pixels of the target. */
enum { KP_FAMILY_OVERWRITE, KP_FAMILY_DEST, KP_FAMILY_DISTORT, KP_FAMILIES };
/* Kinds inside a family (the shader's branch, packed into the image's frame code). */
enum { KP_KIND_REMAP = 0, KP_KIND_WARP = 1, KP_KIND_OUTLINE = 2, KP_KIND_BAR = 3 };             /* overwrite */
enum { KP_KIND_TABLE = 0, KP_KIND_CLOAK_BLEND = 1, KP_KIND_TRANS50 = 2 };                       /* dest */
enum { KP_KIND_BRIGHT = 0, KP_KIND_RAW = 1, KP_KIND_CLOAK = 2 };                                /* distort */
#define KP_DRAW_KINDS 4
#define KP_DRAW_TRANS50 5          /* remap table of the cloak blends 5..7 (images.dat remapping 5) */
#define KP_DRAW_PARAM_SPAN 256     /* threshold row code = owner row * PARAM_SPAN + parameter byte */
#define KP_DRAW_COLOR_SPAN 65536   /* cloak blend row code += trans50 colour row * COLOR_SPAN */
#define KP_DRAW_ROWS_ABOVE 4       /* sprites more than this many rows above the view's top row are not drawn */
#define KP_DRAW_REMAP 9
#define KP_DRAW_PLAYER_COLOUR 14   /* an image of an explicit player colour (wrapper 0x40e950, 0x40e990) */
#define KP_DRAW_LAYER_CELL 64      /* world pixels per cell of the destination overlap grid */
#define KP_DRAW_LAYER_MARGIN 128   /* destination images get layers only this near the view */
/* The depth range of the ranks: clip z = (rank + 1) / RANK_RANGE. Every image's rank fits (checked in
 * kp_draw_list.c). */
#define KP_RANK_RANGE 8192

/* The family and kind of a draw method (installed draw pointers 0x5030d4); 0 for a method without a material. */
int kp_draw_method(int32_t method, int *family, int *kind);
/* The draw methods with a material, as the bit mask of kp_draw_collect. */
uint32_t kp_draw_method_mask(void);

/* A composition window: the copy of the target made at `rank` (the distortion image that opened it), the last
 * distortion reading that copy, and the destination layers of the window ranked before it. */
typedef struct {
	int32_t rank, last, layers;
} KpWindow;

/* A drawn image of the list: left/top/w/h in map pixels; x256/y256 and px256/py256 (the same image on the previous
 * tick) the frame centre in world coordinates, 1/256 pixels; frame the global frame (the frame table's); window the
 * composition window (a distortion image's: the copy it reads, 0 for one out of view before any copy, which draws
 * in no window); trans50 the owner whose colours the trans50 rows 8..15
 * hold, -1 for unchanged rows; wraps how many images of an explicit player colour (method 14) the sprite drew before
 * this one: each restores the sprite's colour through the setter again (kp_lookups_owner_wrapped). */
typedef struct {
	int32_t key, family, kind, method, owner, param, trans50, wraps;
	int32_t left, top, w, h;
	int32_t x256, y256, px256, py256;
	int32_t frame, grp, grp_frame, flip, virtual_;
	int32_t rank, layer, window;
} KpDrawItem;

/* The global frame of frame `frame` of GRP entry grp (KP_GRP_VIRTUAL: the empty frame of a virtual image), or -1
 * when its atlas is not loaded. */
typedef int32_t (*KpFrameOf)(void *ud, int32_t grp, int32_t frame);
typedef void (*KpDrawEmit)(void *ud, const KpDrawItem *d);

typedef struct KpDrawList KpDrawList;

KpDrawList *kp_draw_list_new(void);
void kp_draw_list_free(KpDrawList *l);
/* Forgets the motion of the ticks built so far (a loaded game): the next build draws every image at its position. */
void kp_draw_list_restart(KpDrawList *l);
/* Builds the tick's list of world w: emit(ud, d) per drawn image in rank order. view (NULL:
 * none) is the target the copies are made of; replan rebuilds the same tick for another view. Returns the images
 * drawn (the highest rank), *layers the destination layers of the last window; -1 out of memory. */
int32_t kp_draw_list_build(KpDrawList *l, KpImages *w, KpFrameOf frame_of, KpDrawEmit emit, void *ud,
	const KpRect *view, int replan, int32_t *layers);
/* The plan of the last build: windows[0..count-1]. */
const KpWindow *kp_draw_list_windows(const KpDrawList *l, int32_t *count);
/* Counters of the last build: distortion images, and those whose reads meet the view widened by the margin. */
void kp_draw_list_counts(const KpDrawList *l, int32_t *distortions, int32_t *near);

/* ------------------------------------------------------------------------------------------------------------- */
/* Palettes and lookup rows (kp_lookups.c)                                                                         */
/* ------------------------------------------------------------------------------------------------------------- */

#define KP_REMAP_TABLES 7 /* ofire, gfire, bfire, bexpl, trans50, red, green (0x508828 records 1..7) */
#define KP_DARK_ROWS 32
#define KP_DARK_SIZE (KP_DARK_ROWS * 256)

typedef struct {
	uint16_t rows;
	const uint8_t *data; /* rows * 256 bytes */
} KpRemapTable;

typedef struct {
	KpRemapTable remap[KP_REMAP_TABLES];
	const uint8_t *dark;  /* 32 rows of 256 */
	const uint8_t *shift; /* 256 */
} KpTilesetPalettes;

/* Per tileset its remap tables, dark.pcx rows and shift row; the game tables. */
typedef struct {
	KpTilesetPalettes tilesets[8];
	const uint8_t *tunit, *tminimap, *tselect, *thpbar;
	size_t tunit_size, tminimap_size, tselect_size, thpbar_size;
	uint8_t *owned; /* what the tables point into */
} KpPalettes;

/* The tables from the original files (ADR 0018, stage D3; converter/kpf/kpd.py build_palettes): each tileset's
 * remap PCXs (256 wide: a zero row, then theirs), dark.pcx's and shift.pcx's first bytes, the game's colour tables. */
int kp_palettes_from_files(KpPalettes *p, const KpFiles *files, KpError *err);
void kp_palettes_free(KpPalettes *p);

#define KP_LOOKUP_PLAYER_ROWS 12
#define KP_LOOKUP_PLAYER_FIRST 8
#define KP_LOOKUP_PLAYER_COLORS 8
#define KP_LOOKUP_SELECTION_FIRST 1
#define KP_LOOKUP_SELECTION_COLORS 8
#define KP_LOOKUP_SELECTION_CIRCLE 6 /* the index the selection circles' frames draw most (thingy\\o*.grp, od*.grp) */
#define KP_LOOKUP_BANK_ROWS 256
#define KP_LOOKUP_SHADOW_DARK_ROW 18
#define KP_LOOKUP_MAX_REMAP 64
#define KP_LOOKUP_TABLE_ROWS (KP_REMAP_TABLES * KP_LOOKUP_BANK_ROWS + 1)

/* The lookup rows of one tileset: the remap texture (256 x remap_rows, row k = what each index maps to), the
 * destination table texture (256 x table_rows) and the row numbers the images' draw methods use. */
typedef struct {
	uint8_t remap[KP_LOOKUP_MAX_REMAP * 256];
	int32_t remap_rows;
	uint8_t *tables; /* KP_LOOKUP_TABLE_ROWS * 256, owned */
	/* Per table row the blend over a destination colour that fits it best: RGBA, the premultiplied colour then the
	 * coverage (out = colour + (1 - coverage) * destination), for the original's art drawn in RGBA (the destination
	 * there is no index to look up); KP_LOOKUP_TABLE_ROWS * 4, owned */
	uint8_t *glow;
	int32_t table_rows;
	int32_t shift_row, selection_base, selections, bright, shadow;
	int32_t bank_base[KP_REMAP_TABLES];
	int32_t trans50; /* the row code of the cloak blends (remap(TRANS50)) */
	int32_t outline_color;
	/* The player row each player number draws with, plus 1 (SetDrawColor 0x41d740); 0: the player's own, in every colour
	 * mode but the minimap's mode 2, where the viewer's, the allies' and the others' rows are the interface's (the
	 * kp_ui_team_rows) */
	uint8_t team_row[KP_LOOKUP_PLAYER_ROWS];
} KpLookups;

/* Builds the rows of tileset `tileset` over its palette rgb (768 bytes); -1 with err (a tunit colour inside the
 * player rows, a blend bank taller than a bank, out of memory). */
int kp_lookups_build(KpLookups *l, const KpPalettes *p, int tileset, const uint8_t *rgb, int32_t outline_color,
	KpError *err);
void kp_lookups_free(KpLookups *l);
/* The rows of the methods. */
int32_t kp_lookups_owner(const KpLookups *l, int32_t owner);
/* The colour row of a sprite's owner after `wraps` restores of an explicit-colour wrapper (0x40e950, 0x40e990): the
 * wrapper restores the saved colour, already mapped, through the setter, which maps it again in the colour mode 2 (as
 * the player it names). The same as kp_lookups_owner when no mode maps. */
int32_t kp_lookups_owner_wrapped(const KpLookups *l, int32_t owner, int32_t wraps);
/* Sets the player each number draws as (rows[KP_LOOKUP_PLAYER_ROWS] below KP_LOOKUP_PLAYER_ROWS; NULL: each its own).
 * Returns whether a row changed (the draw list's items must be built again). */
int kp_lookups_set_player_rows(KpLookups *l, const uint8_t *rows);
int32_t kp_lookups_remap_code(const KpLookups *l, int32_t index);
/* The remap/table row of an image by its draw method, parameter, sprite owner and trans50 colour owner (-1: the
 * rows unchanged); warp_frame is the warp texture's global frame for method 12. Returns -1 for
 * a selection row outside tselect. */
int32_t kp_draw_row_of(const KpLookups *l, int32_t method, int32_t param, int32_t owner, int32_t trans50,
	int32_t warp_frame);
/* The same for an image that follows `wraps` explicit-colour images of its sprite. */
int32_t kp_draw_row_wrapped(const KpLookups *l, int32_t method, int32_t param, int32_t owner, int32_t wraps,
	int32_t trans50, int32_t warp_frame);

/* ------------------------------------------------------------------------------------------------------------- */
/* The screen's palette (kp_palette.c)                                                                             */
/* ------------------------------------------------------------------------------------------------------------- */

#define KP_PALETTE_RECORDS 8
#define KP_PALETTE_CYCLE_PERIOD_MS 15.625
#define KP_PALETTE_BRIGHTNESS_MIN KP_UI_GAMMA_MIN /* the gamma option's range (kp_ui.h) */
#define KP_PALETTE_BRIGHTNESS_MAX KP_UI_GAMMA_MAX
#define KP_PALETTE_BRIGHTNESS_DEFAULT KP_UI_GAMMA_DEFAULT
#define KP_PALETTE_FADE_MAX_STEPS 10
#define KP_PALETTE_FADE_WAIT_MS 10
#define KP_PALETTE_MAX_CYCLES_PER_ADVANCE 2

typedef struct {
	int active;
	int32_t speed, counter, low, high;
} KpCycleRecord;

/* A tileset's colour cycles (the profile's CYC<n>, kp_defs.h): one speed, ranges low..high. */
typedef struct {
	int32_t speed, count;
	uint8_t low[KP_PALETTE_RECORDS], high[KP_PALETTE_RECORDS];
} KpCycles;

typedef struct {
	uint8_t r[256], g[256], b[256];
	int32_t brightness;
	uint32_t version;
	double clock, cycle_acc;
	KpCycleRecord records[KP_PALETTE_RECORDS];
	int fading;
	uint8_t fade_to[768];
	double fade_at;
	uint8_t rgb[768]; /* kp_palette_rgb's cache */
	uint32_t rgb_version;
	uint8_t gamma[256]; /* the gamma table of gamma_brightness (-1: none yet) */
	int32_t gamma_brightness;
} KpPalette;

void kp_palette_init(KpPalette *p, const uint8_t rgb[768], const KpCycles *cycles);
/* 0x4ccd30: the cycle driver once; returns whether a range rotated. */
int kp_palette_cycle(KpPalette *p);
void kp_palette_set_brightness(KpPalette *p, int32_t v);
/* 0x4cf760: the gamma table of a brightness. */
void kp_palette_gamma_table(int32_t brightness, uint8_t out[256]);
/* Its exponent: (200 - brightness) / 100 (1 at the default 100). */
double kp_palette_gamma_exponent(int32_t brightness);
/* 0x4ccf40: the grey of a colour. */
int32_t kp_palette_gray(int32_t r, int32_t g, int32_t b);
void kp_palette_current(const KpPalette *p, uint8_t out[768]);
/* The palette the device gets (gamma unless brightness is 100), cached per version. */
const uint8_t *kp_palette_rgb(KpPalette *p);
void kp_palette_fade_to(KpPalette *p, const uint8_t target[768], int32_t steps);
void kp_palette_grayscale(KpPalette *p);
/* Advances the palette clock by ms of real time; returns whether the palette changed. */
int kp_palette_advance(KpPalette *p, double ms, int paused);
/* The palette's tone for art drawn in its own colours (the HD world, stage R4c): out[4 v + c] is what a colour level
 * v of channel c of the base palette has become in the current one (fades, the brightness option), from the entries
 * not skipped (skip[i] set: the colour cycles, entry 0) by level and between them, past them in proportion to the
 * nearest; out[3] 255 when the current palette is grey where the base is not (kp_palette_grayscale). */
void kp_palette_tone(const uint8_t base[768], const uint8_t current[768], const uint8_t skip[256], uint8_t out[1024]);

/* ------------------------------------------------------------------------------------------------------------- */
/* Health bars (kp_health_bar.c)                                                                                   */
/* ------------------------------------------------------------------------------------------------------------- */

#define KP_BAR_WIDTH 256
#define KP_BAR_ROWS 16
#define KP_BAR_SLOTS 12
#define KP_BAR_HP_ROWS 5
#define KP_BAR_SHIELD_HP_ROWS 7
#define KP_BAR_TIMER_HALLUCINATION 1350
#define KP_BAR_TIMER_BROODLING 1800

/* The values of a bar (0x464d20): width columns, shields, hit points and shields (whole), the energy or timer bar
 * (has_energy). */
typedef struct {
	int32_t width;
	int shields;
	int32_t hp, hp_max, sh, sh_max;
	int has_energy;
	int32_t energy, energy_max;
} KpBar;

/* A canvas the bar draws on: hline/vline of n pixels in a palette index. */
typedef struct {
	void (*hline)(void *ud, int32_t x, int32_t y, int32_t n, uint8_t color);
	void (*vline)(void *ud, int32_t x, int32_t y, int32_t n, uint8_t color);
	void *ud;
	const uint8_t *thpbar; /* the 19 thpbar.pcx entries */
} KpBarCanvas;

/* 0x4648a0 / 0x464920: percentage and filled columns. */
void kp_bar_fill(int32_t max, int32_t cur, int32_t span, int32_t *pct, int32_t *filled);
/* 0x464d20: draws bar at (x, y) with the visible crop (left, top, w, h) of the frame. */
void kp_bar_draw(const KpBarCanvas *c, int32_t x, int32_t y, const int32_t crop[4], const KpBar *bar);
/* A unit's values as 0x464d20 reads them (8.8 fixed point hit points, shields and energy) and its type's: the hit
 * point maximum (8.8), shields, shield amount, special ability flags, the sprite's health bar width, the energy
 * maximum (8.8; upgrades decide it, 0x434940). */
typedef struct {
	int64_t type, hp, shields, energy, timer, status, owner;
	int64_t hit_points, shield_enabled, shield_amount, abilities, width, max_energy;
} KpBarUnit;

/* The bar of a unit for the local player. */
void kp_bar_from_unit(const KpBarUnit *u, int64_t local_player, KpBar *out);
/* The bar of game unit n. */
struct KpGame;
void kp_bar_of_unit(struct KpGame *g, int64_t n, KpBar *out);

/* The health bar texture: 256 x (16 * 12) texels, 2 bytes each (index, drawn 255 / 0). */
typedef struct {
	uint8_t pixels[KP_BAR_WIDTH * KP_BAR_ROWS * KP_BAR_SLOTS * 2];
	int32_t slot_of[KP_IMAGES]; /* image key -> slot, -1 */
	int32_t key_of[KP_BAR_SLOTS];
	uint32_t seen[KP_BAR_SLOTS], tick;
	int32_t free[KP_BAR_SLOTS], free_count, next_slot;
	KpBar drawn[KP_BAR_SLOTS];
	int32_t drawn_w[KP_BAR_SLOTS], drawn_h[KP_BAR_SLOTS];
	int has_drawn[KP_BAR_SLOTS];
	uint8_t thpbar[19];
	int32_t dirty_lo, dirty_hi; /* the slots redrawn since the last upload, lo..hi-1 (lo == hi: none) */
} KpBars;

void kp_bars_init(KpBars *b, const uint8_t *thpbar, size_t size);
void kp_bars_begin(KpBars *b);
/* The slot of image key showing bar in a w x h frame (drawn when it changed); -1 when every slot is taken. */
int32_t kp_bars_put(KpBars *b, int32_t key, const KpBar *bar, int32_t w, int32_t h);
void kp_bars_finish(KpBars *b);

/* ------------------------------------------------------------------------------------------------------------- */
/* Resource packs (docs/formats/packs.md) and the frame table                                                    */
/* ------------------------------------------------------------------------------------------------------------- */

#define KP_PACK_PAGE_SIZE 2048

typedef struct {
	char name[64];
	uint32_t first, count;
} KpPackAtlas;

typedef struct {
	uint16_t page, x, y, w, h;
} KpPackFrame;

typedef struct {
	uint16_t id;
	const uint8_t *ogg;
	uint32_t size;
} KpPackSound;

/* A pack (kp_pack_build: its pages apart) or a sounds container (kp_mission_sounds_load: points into the caller's
 * bytes); owned arrays. */
typedef struct {
	int atlas_count;
	KpPackAtlas *atlases;
	uint32_t frame_count;
	const uint8_t *frames; /* FRMS records, 10 bytes each */
	int page_count;
	uint16_t page_w, page_h;
	int sound_count;
	KpPackSound *sounds;
	uint8_t *owned; /* kp_pack_build: the FRMS records */
} KpPack;

void kp_pack_free(KpPack *p);
void kp_pack_frame(const KpPack *p, uint32_t i, KpPackFrame *out);
/* A sound of a pack or of a mission's sounds (kind MSND): the OGG of id, or NULL. */
const KpPackSound *kp_pack_sound(const KpPack *p, uint32_t id);
/* A mission's sounds container (kind MSND: the SNDS section only). */
int kp_mission_sounds_load(KpPack *p, const uint8_t *data, size_t size, const char *name, KpError *err);

/* A rectangle's place on the pages (kp_pages_pack). */
typedef struct {
	uint16_t page, x, y;
} KpPagePlace;

/* The skyline packer of converter/emit/pages.py (ADR 0018, stage D5): n rectangles (sizes[2 k] wide, sizes[2 k + 1]
 * high) placed in order of height, then width (both descending), then k, each on the first page of page_size squared
 * where it fits at the skyline's lowest top edge (ties: the lowest x), a new page when none has room; out[k] its place
 * (a zero-area rectangle: page 0 at 0, 0), *page_count the pages. -1 with the error when one is larger than a page. */
int kp_pages_pack(const uint16_t *sizes, uint32_t n, int32_t page_size, KpPagePlace *out, int *page_count, KpError *err);
/* Pack `name` built from the original GRPs as the converter's container holds it (converter/kpf/resources.py
 * build_pack, without its sounds): grpmeta's atlases of the pack in order, each GRP entry's frames read through files
 * and decoded, equal frames sharing one rectangle, FRMS in p (p->owned), the pages' indices in *pages (page_count
 * buffers of KP_PACK_PAGE_SIZE squared, malloc'd with their array: the caller frees them). -1 with the error (a GRP
 * missing or malformed, one drawing an opaque index 0, which the pages cannot tell from transparency). */
int kp_pack_build(KpPack *p, uint8_t ***pages, const KpGrpMeta *g, const KpFiles *files, const char *name, KpError *err);

/* The frames of every loaded atlas: global frame g of GRP entry e frame f is
 * atlas_base[e's atlas] + afrm[e] + f; texels[2 g] = (x, y, w, h), texels[2 g + 1] = (layer, 0, 0, 0) where layer
 * is the frame's page among all loaded pages; the frame `empty` past the last has zero size. */
#define KP_FRAME_TABLE_WIDTH 1024
typedef struct {
	float *texels;  /* RGBA32F, KP_FRAME_TABLE_WIDTH x height */
	int32_t height;
	int32_t empty;
	int32_t *atlas_base; /* [1..atlas_count] of grpmeta: the first global frame, -1 not loaded */
	int32_t *atlas_in;   /* [2 a], [2 a + 1]: a loaded atlas's pack and its index there (kp_pack_atlas_find) */
	int32_t atlas_count;
	const uint32_t *afrm; /* grpmeta AFRM: [GRP entry - 1] */
	uint32_t grp_count;
	int32_t page_count; /* the pages of the loaded packs, in pack order (the texture array's layers) */
	int pack_count;     /* the packs it was built from (atlas_in's packs lie below it) */
} KpFrameTable;

/* Per GRP entry its first frame within its atlas (the frames of the atlas's earlier entries, in GRPS order), as
 * as converter/kpf/kpd.py atlas_frame_offsets computes it; malloc'd, -1 out of memory. */
int kp_grpmeta_afrm(const KpGrpMeta *g, uint32_t **out, KpError *err);
/* Grpmeta's atlas a (from 1) among the loaded packs (packs[k] the pack of name pack_names[k]): 1 with its pack k and
 * the pack's atlas j, 0 when no loaded pack has it. */
int kp_pack_atlas_find(const KpGrpMeta *g, uint32_t a, const KpPack *const *packs, const char *const *pack_names,
	int pack_count, int *k, int *j);
/* The table over the loaded packs (packs[k] the pack of name pack_names[k]; grpmeta's atlases name their pack). */
int kp_frame_table_build(KpFrameTable *t, const KpGrpMeta *g, const uint32_t *afrm, const KpPack *const *packs,
	const char *const *pack_names, int pack_count, KpError *err);
void kp_frame_table_free(KpFrameTable *t);
int32_t kp_frame_table_frame(const KpFrameTable *t, const KpGrpMeta *g, int32_t grp, int32_t frame);

/* ------------------------------------------------------------------------------------------------------------- */
/* StarCraft: Remastered's HD art (ADR 0013, kp_hd.c, kp_hd_render.c; converter/emit/hd.py)                       */
/* ------------------------------------------------------------------------------------------------------------- */

#define KP_HD_PACK_KIND "HDPK"
#define KP_HD_TILESET_KIND "HDTS"
#define KP_HD_SCHEMA 2      /* the tilesets', the water's, ui.kpd's container and portraits.kpd's */
#define KP_HD_PACK_SCHEMA 3 /* the packs': their lit pages' lighting layers (converter/emit/hd.py PACK_SCHEMA) */
#define KP_HD_PACK_HEAD_BYTES 12 /* u8 scale, u16 page size, u16 pages, u32 frames, u8 format, u16 lit pages */
/* An HD terrain atlas layer's largest side and the most layers: GLES 3.0's least GL_MAX_TEXTURE_SIZE and
 * GL_MAX_ARRAY_TEXTURE_LAYERS */
#define KP_HD_TERRAIN_SIDE 2048
#define KP_HD_TERRAIN_LAYERS 256
#define KP_HD_PACK_FIRST_READ 1024 /* the bytes of an HD pack read first: its header and section table */
#define KP_HD_SLOT_TABLE_WIDTH 256 /* pages a row of kp_gfx's table of the pages' slots */
/* An HD pack's page format: the blocks of each of its layers (kp_hd_layer_codec). Computers and the web take BC; phones
 * ETC2 and EAC or ASTC, which tools/product.sh transcodes them. */
enum { KP_HD_FORMAT_BC, KP_HD_FORMAT_ETC2, KP_HD_FORMAT_ASTC, KP_HD_FORMATS };
/* The byte of the format in an HD pack's HEAD. */
#define KP_HD_PACK_HEAD_FORMAT 9
/* A lit page's lighting layers, StarCraft: Remastered's as its anims hold them (sections NRML, AODP, SPEC, EMIS): the
 * normal map (x in red, y in alpha) and the ambient occlusion (green; the depth in alpha) in BC3 blocks, the specular
 * and the emissive colours in BC1 ones. */
enum { KP_HD_LIGHT_NORMAL, KP_HD_LIGHT_AO, KP_HD_LIGHT_SPECULAR, KP_HD_LIGHT_EMISSIVE, KP_HD_LIGHTS };
/* A page's layers in a pack, the lighting's from KP_HD_LAYER_LIGHT on (in KP_HD_LIGHT_* order). */
enum { KP_HD_LAYER_DIFFUSE, KP_HD_LAYER_TEAM, KP_HD_LAYER_LIGHT, KP_HD_LAYERS = KP_HD_LAYER_LIGHT + KP_HD_LIGHTS };
/* The blocks a page's layer can be in (kp_gfx's block formats are these, in this order). In the phones' formats the
 * normal map holds x in red and y in green (kp_gfx swizzles green into alpha, where the shader reads y), the occlusion
 * its red (swizzled into green), the depth is left out. */
enum {
	KP_HD_CODEC_BC1, KP_HD_CODEC_BC3, KP_HD_CODEC_EAC_R11, KP_HD_CODEC_EAC_RG11, KP_HD_CODEC_ETC2_RGB8,
	KP_HD_CODEC_ETC2_RGBA8, KP_HD_CODEC_ASTC_4X4, KP_HD_CODEC_ASTC_6X6, KP_HD_CODEC_ASTC_8X8, KP_HD_CODECS
};
/* The blocks of a layer in a pack format: the one table of them, all the rest is reckoned from it. */
int kp_hd_layer_codec(int format, int layer);
/* A codec's square block: its side in texels and its bytes. */
void kp_hd_codec_block(int codec, int *side, int *bytes);
/* The bytes of one page's layer in a pack format. */
size_t kp_hd_layer_bytes(int format, int layer, int32_t page_size);
/* Whether the product can decode a pack format's pages for a context that does not take their blocks (ASTC: no codec). */
int kp_hd_format_decodable(int format);
/* Whether pages of a format (-1 and others no format) can be drawn: decoded, or as their blocks unless decoding is asked
 * for (decode) and when the context takes them (takes). */
int kp_hd_format_usable(int format, int decode, int takes);

/* An HD pack, opened from its first bytes (its frames point into them); its pages stay in the file and are read one
 * at a time (kp_hd_cache): its frames are its SD pack's, in order. Its first lit_pages pages hold the images
 * StarCraft: Remastered lights, and only them, with their lighting layers. */
typedef struct {
	int scale;           /* HD pixels per logical pixel: 2 (HD2) or 4 (HD) */
	int32_t page_size;   /* pages of page_size squared */
	int32_t page_count;
	int32_t lit_pages;
	uint32_t frame_count;
	int format;             /* the pages' blocks: KP_HD_FORMAT_* */
	const uint8_t *frames;  /* 14 bytes each: u16 page, x, y, w, h, i16 dx, dy (half HD pixels, y down) */
	uint32_t diffuse_offset; /* in the file: the pages' diffuse blocks, page after page */
	uint32_t team_offset;    /* in the file: their team colour masks' blocks */
	uint32_t light_offset[KP_HD_LIGHTS]; /* in the file: the lit pages' lighting layers, page after page */
} KpHdPack;

/* The bytes of an HD pack to read before kp_hd_pack_open (its header, section table and frames), from the first
 * KP_HD_PACK_FIRST_READ (or fewer when the file is shorter); 0 when they are not an HD pack's. */
size_t kp_hd_pack_head_size(const uint8_t *first, size_t n, size_t file_size);
int kp_hd_pack_open(KpHdPack *p, const uint8_t *head, size_t n, size_t file_size, const char *name, KpError *err);
/* The page format of a pack whose first bytes these are (of its file_size bytes, as kp_hd_pack_head_size), -1 when they
 * are none of a pack's or name no format. */
int kp_hd_pack_format(const uint8_t *first, size_t n, size_t file_size);
/* A lit page's lighting layer (KP_HD_LIGHT_*) of size squared, BC blocks, written in its ETC2 format into out
 * (kp_hd_layer_bytes of KP_HD_FORMAT_ETC2); -1 out of memory. */
int kp_hd_light_to_etc2(int light, const uint8_t *bc, int32_t size, uint8_t *out);
/* The file offsets of a page's diffuse and team blocks, and with light of a lit page's lighting layers (0 for a page
 * not lit). */
void kp_hd_page_offsets(const KpHdPack *p, int32_t page, uint32_t *diffuse, uint32_t *team,
	uint32_t light[KP_HD_LIGHTS]);
/* The HD frame table over the SD one (built from the same packs; hd_packs[k] the HD pack of packs[k], or NULL): global
 * frame g's texels[2 g] = (x, y, w, h) in its HD page, texels[2 g + 1] = (page among the HD packs' pages, dx, dy,
 * scale); a frame without HD art has zero size. */
int kp_hd_frame_table_build(KpFrameTable *hd, const KpFrameTable *sd, const KpGrpMeta *g, const KpPack *const *packs,
	const KpHdPack *const *hd_packs, const char *const *pack_names, int pack_count, KpError *err);

/* The residency of the HD pages (stage R4a, kp_hd_cache.c): a fixed number of slots (the layers of the HD textures)
 * hold the pages last wanted; a page is wanted while an image drawn shows a frame of it (kp_hd_cache_want_images, once
 * a tick), loaded by the caller in the order taken, placed in a free slot or in the least recently wanted page's, never
 * in one wanted this tick. A page not resident draws its images' SD frames. With lit slots (kp_hd_cache_lit) a lit
 * page takes one of the first lit_slots (the slots with lighting layers), any other page one of the rest. */
enum { KP_HD_ABSENT, KP_HD_WANTED, KP_HD_LOADING, KP_HD_RESIDENT, KP_HD_FAILED };
#define KP_HD_RETRY 60 /* ticks a page refused a slot waits (more pages wanted than slots: no load every frame) */
typedef struct {
	int32_t pages, slots;
	int32_t *slot_of;  /* [page] its slot, -1 */
	int32_t *page_of;  /* [slot] its page, -1 */
	uint32_t *used;    /* [page] the clock when last wanted */
	uint32_t *retry;   /* [page] the clock from which a page refused a slot may be wanted again */
	uint8_t *state;    /* [page] KP_HD_* */
	uint8_t *lit;      /* [page] 1: a lit page (kp_hd_cache_lit) */
	int32_t lit_slots; /* the slots of the lit pages, from slot 0; 0: one kind of slot */
	int32_t *queue;    /* the wanted pages not yet taken, in order */
	int32_t queue_head, queue_count;
	uint32_t clock;
	int dirty;         /* slot_of changed since kp_hd_cache_take_dirty */
} KpHdCache;

int kp_hd_cache_init(KpHdCache *c, int32_t pages, int32_t slots);
void kp_hd_cache_free(KpHdCache *c);
/* The lit pages (lit[page] nonzero) and their slots, the first lit_slots (0 < lit_slots <= slots), before any page is
 * placed; -1 when lit_slots is out of range. */
int kp_hd_cache_lit(KpHdCache *c, const uint8_t *lit, int32_t lit_slots);
/* The slots a budget (bytes) affords pages of which lit are lit, a slot costing page_bytes (a lit one lit_bytes: its
 * lighting layers too), at most max (and max / 2 lit ones: kp_gfx keeps their layers two a slot): a slot each when
 * they fit, else the lit and the other pages' slots in proportion to their counts, at least one each. Returns the
 * slots (at least one); *lit_slots the lit ones among them. */
int32_t kp_hd_slots_split(size_t budget, size_t page_bytes, size_t lit_bytes, int32_t pages, int32_t lit, int32_t max,
	int32_t *lit_slots);
/* Every page absent (the slots' contents are gone: a new context). */
void kp_hd_cache_reset(KpHdCache *c);
/* A new tick: the pages wanted from here on are this tick's. */
void kp_hd_cache_tick(KpHdCache *c);
void kp_hd_cache_want(KpHdCache *c, int32_t page);
/* The next page to load (now KP_HD_LOADING), 0 when none; a page no longer wanted this tick goes back to absent. */
int kp_hd_cache_take(KpHdCache *c, int32_t *page);
/* The slot for a loaded page (resident from now on; the page it held, if any, in *evicted, else -1), or -1 when every
 * slot holds a page wanted this tick (the page goes back to absent and is not loaded again for KP_HD_RETRY ticks) or
 * when the page is not loading (not taken, or reset since: its state stays). */
int32_t kp_hd_cache_place(KpHdCache *c, int32_t page, int32_t *evicted);
/* A page that could not be loaded: drawn in SD from now on. */
void kp_hd_cache_failed(KpHdCache *c, int32_t page);
/* Whether slot_of changed since the last call. */
int kp_hd_cache_take_dirty(KpHdCache *c);
/* Pages loading or wanted (not yet resident, failed or dropped). */
int32_t kp_hd_cache_pending(const KpHdCache *c);

/* An HD tileset: one tile of size squared per 1.17 megatile. */
typedef struct {
	int32_t count, size;
	int format; /* KP_HD_FORMAT_BC: BC1 tiles; KP_HD_FORMAT_ETC2: ETC2 RGB8 (the same size) */
	const uint8_t *tiles;
	/* its water (stage R4c; NULL: none): per megatile 0 dry, else 1 + its mask; the masks, size squared bytes each */
	const uint8_t *water_map;
	const uint8_t *water_masks;
	int32_t water_mask_count;
	/* creep's edges (EDGE; NULL: none): size squared RGBA8 texels each */
	const uint8_t *edges;
	int32_t edge_count;
} KpHdTileset;

int kp_hd_tileset_load(KpHdTileset *t, const uint8_t *data, size_t size, const char *name, KpError *err);

/* The megatiles a map uses in an atlas of the tileset's blocks, a 2D array whose layers are no larger than
 * KP_HD_TERRAIN_SIDE on a side (columns slots a row, rows slot rows a layer; slot s at column s % columns, row
 * s / columns % rows of layer s / (columns * rows)), and each cell's slot. */
typedef struct {
	uint8_t *blocks; /* layers of w x h pixels of BC1 (or ETC2 RGB8) blocks; NULL: the slots and water masks alone */
	size_t size;
	int format;      /* the tileset's */
	int32_t w, h, columns, rows, layers, tile;
	uint16_t *slots; /* map_w x map_h */
	int32_t map_w, map_h;
	uint8_t *water;  /* layers of w x h: each slot's water mask (255 water), or NULL when the map has none */
	int32_t *slot_of; /* per megatile of the tileset its slot, -1 none */
	int32_t megatiles;
	const uint8_t *edges; /* the tileset's creep edges (tile squared RGBA8 texels each), NULL: none */
	int32_t edge_count;
} KpHdTerrain;

/* The water's normal maps (water.kpd, HDWA): two animated sets of frames (the large waves, the detail), BC1 blocks or
 * ETC2 RGB8 ones (format) of size squared each, pointing into the caller's bytes. */
#define KP_HD_WATER_KIND "HDWA"
typedef struct {
	int format;
	int32_t frames[2], size[2];
	const uint8_t *blocks[2];
} KpHdWater;

int kp_hd_water_load(KpHdWater *w, const uint8_t *data, size_t size, const char *name, KpError *err);

/* The atlas of the megatiles ids show; a tileset whose tiles are NULL gives the slots and water masks alone (blocks
 * NULL: the original's art drawn with the water masks, stage R5). */
int kp_hd_terrain_build(KpHdTerrain *a, const KpHdTileset *t, const uint16_t *ids, int32_t map_w, int32_t map_h,
	KpError *err);
/* The cells' slots again for the megatiles the map shows now (creep, the remembered terrain): 0, or 1 when one of them
 * has no slot (the caller builds the atlas again). */
int kp_hd_terrain_relink(KpHdTerrain *a, const uint16_t *ids);
void kp_hd_terrain_free(KpHdTerrain *a);

/* The interface's HD art (ui.kpd, HDUI; stage R4d, kp_hd_ui.c; converter/emit/hd_ui.py): per SD interface file (a GRP
 * or a PCX, by its path) its frames' HD images, read from the file one at a time. */
#define KP_HD_UI_KIND "HDUI"
#define KP_HD_UI_SCHEMA 3 /* every frame's x, y: where it lies (converter/emit/hd_ui.py UI_SCHEMA) */
#define KP_HD_UI_PATH 64
#define KP_HD_UI_CONSOLE 1 /* an asset's flag: a console's three pieces */
#define KP_HD_UI_WIRE_LAYERS 8 /* a wireframe's HD frame: its parts' layers side by side, square */
enum { KP_HD_UI_BC1, KP_HD_UI_BC3, KP_HD_UI_PNG };
typedef struct {
	uint32_t offset, size;  /* in the file */
	int32_t w, h;           /* HD pixels */
	int format;             /* KP_HD_UI_* */
	int32_t x, y;           /* the image's top-left in HD pixels from the SD frame's (a GRP frame's, a PCX's) */
} KpHdUiFrame;

typedef struct {
	int scale;
	int32_t asset_count, frame_count;
	const uint8_t *assets;  /* ASET: 72 bytes each (into the caller's head bytes) */
	const uint8_t *frames;  /* FRMS: 18 bytes each */
	uint32_t images;        /* IMGS's offset in the file */
} KpHdUi;

int kp_hd_ui_open(KpHdUi *u, const uint8_t *head, size_t n, size_t file_size, const char *name, KpError *err);
/* The bytes of ui.kpd to read before kp_hd_ui_open, from its first ones; 0 when they are not its. */
size_t kp_hd_ui_head_size(const uint8_t *first, size_t n, size_t file_size);
/* The asset of an SD path (len bytes; case and the separator ignored), its first frame, frame count and flags; -1
 * none. */
int32_t kp_hd_ui_asset(const KpHdUi *u, const char *path, size_t len, int32_t *first, int32_t *count, int *flags);
void kp_hd_ui_frame(const KpHdUi *u, int32_t frame, KpHdUiFrame *out);
/* Whether a frame is a wireframe's: its parts' KP_HD_UI_WIRE_LAYERS square layers side by side. */
int kp_hd_ui_frame_wire(const KpHdUiFrame *f);
/* The rectangle (x, y, w, h on the logical screen) of a frame's HD image for the SD frame at (x, y) it replaces: at the
 * frame's offset from it (a wireframe: one of its square layers). */
void kp_hd_ui_frame_place(int scale, const KpHdUiFrame *f, int32_t x, int32_t y, float rect[4]);

/* An item's HD image as the HD composite samples it (kp_gfx's item()): its rectangle on the logical screen, its
 * texels in the frame (x0, y0, x1, y1) and the frame's RGBA8 texels (w x h; NULL: not read, taken as opaque); a tiled
 * item's SD frame's size and its first tile's top-left (tile_w 0: none); a wireframe's (wire) part layers side by side,
 * each src's width. */
typedef struct {
	float rect[4], src[4];
	const uint8_t *rgba;
	int32_t w, h;
	float tile_w, tile_h, tile_x, tile_y;
	int wire;
} KpHdUiSample;
/* Its alpha at the logical point (px, py), sampled as the composite does (linear, a wireframe's parts over each
 * other); -1 where the image does not reach. */
float kp_hd_ui_alpha(const KpHdUiSample *s, float px, float py);

/* What became of an item's HD image in the last draw: drawn; no HD art for its file or frame in ui.kpd; waiting for
 * its frame or a place in the atlas (SD meanwhile); its frame unreadable. */
enum { KP_HD_UI_ITEM_DRAWN, KP_HD_UI_ITEM_NO_ART, KP_HD_UI_ITEM_WAITING, KP_HD_UI_ITEM_FAILED };
/* The share of an SD pixel the composite shows through the HD art over it from which the development checks count the
 * pixel as SD (kp_hd_ui_count_coverage). */
#define KP_HD_UI_SEE_THROUGH 0.5f
/* A canvas item as the composite drew it: its state (KP_HD_UI_ITEM_*), whether it replaces its texels (a console
 * piece: its own silhouette over the world) and its HD image. */
typedef struct {
	int state;
	int replaces;
	KpHdUiSample image;
} KpHdUiCovered;
/* The HD interface's coverage of a canvas composed (the development checks): its drawn pixels by what the composite
 * shows there; every pixel but `hd` and `no_art` is SD where HD art exists. */
typedef struct {
	int64_t hd;             /* an item's HD image (a console piece's: its own silhouette over the world) */
	int64_t no_art;         /* an item whose file or frame has no HD art (SD by design); its paths in no_art_paths */
	int64_t waiting, failed; /* an item still waiting for its HD image, or whose frame cannot be read */
	int64_t outside;        /* an item's pixel its HD image does not reach (no console piece): the SD pixel */
	int64_t translucent;    /* an item's pixel whose HD art, with the item's under it, lets KP_HD_UI_SEE_THROUGH of the
	                         * SD pixel show (no console piece) where the composite has its own SD pixel under the art,
	                         * not what lay under it (KpUiCanvas.below) */
	int64_t lost;           /* a logged image's pixel left without an item (kp_ui_image_owners: the items full) */
	int64_t unlogged;       /* a surface's pixel an image wrote and a draw outside the image log changed (hd_audit) */
	char no_art_paths[512];
	char outside_paths[512]; /* the items with pixels outside */
	char translucent_paths[512];
	char unlogged_paths[512]; /* the images drawn over outside the log */
} KpHdUiCoverage;
/* The coverage of canvas v whose items 1..count (items[0] unused) the composite drew as `items` describes. */
void kp_hd_ui_count_coverage(const KpUiCanvas *v, const KpHdUiCovered *items, int32_t count, KpHdUiCoverage *out);

/* The HD portraits (portraits.kpd, HDPV; stage R4e, kp_hd_ui.c; converter/emit/hd_video.py): per SD portrait movie
 * (by its path) its HD clip, a WebM (kp_webm.h), read from the file when the movie is first shown. */
#define KP_HD_VIDEO_KIND "HDPV"
#define KP_HD_VIDEO_MAX 256 /* a clip's largest width and height */
typedef struct {
	int32_t clip_count;
	const uint8_t *clips;   /* CLIP: 72 bytes each (into the caller's head bytes) */
	uint32_t data;          /* DATA's offset in the file */
} KpHdVideo;

int kp_hd_video_open(KpHdVideo *v, const uint8_t *head, size_t n, size_t file_size, const char *name, KpError *err);
/* The bytes of portraits.kpd to read before kp_hd_video_open, from its first ones; 0 when they are not its. */
size_t kp_hd_video_head_size(const uint8_t *first, size_t n, size_t file_size);
/* The clip of an SD movie's path (case and the separator ignored): its offset in the file and size; -1 none. */
int32_t kp_hd_video_clip(const KpHdVideo *v, const char *path, size_t len, uint32_t *offset, uint32_t *size);

/* The interface's HD atlas (kp_hd_ui.c): layers of size squared texels where the frames shown are placed along
 * shelves as they are first needed; full, it is emptied (kp_hd_ui_atlas_reset) and the frames placed again. */
#define KP_HD_UI_ATLAS_LAYERS 8
typedef struct {
	int32_t layer, x, y, w, h; /* w 0: not placed */
	uint8_t tint[3];           /* [0]: the luminance of the image's light texels (a tinted frame's colour is over it) */
} KpHdUiPlace;

typedef struct {
	int32_t size, layers;
	int32_t shelf_x[KP_HD_UI_ATLAS_LAYERS], shelf_y[KP_HD_UI_ATLAS_LAYERS], shelf_h[KP_HD_UI_ATLAS_LAYERS];
	KpHdUiPlace *places;       /* per HDUI frame */
	int32_t frame_count;
	uint32_t generation;       /* counts the resets: the texels of an earlier one are gone */
} KpHdUiAtlas;

int kp_hd_ui_atlas_init(KpHdUiAtlas *a, int32_t size, int32_t layers, int32_t frame_count);
void kp_hd_ui_atlas_free(KpHdUiAtlas *a);
void kp_hd_ui_atlas_reset(KpHdUiAtlas *a);
/* The place of a frame, placed before or now (w x h texels, *fresh set: its texels to upload); NULL when it does not
 * fit (the caller resets the atlas and tries again on the next frame). */
KpHdUiPlace *kp_hd_ui_atlas_place(KpHdUiAtlas *a, int32_t frame, int32_t w, int32_t h, int *fresh);

/* BC1 (bc3 0) or BC3 blocks of a w x h image decoded to RGBA8 (kp_bcn.c). */
void kp_bcn_decode(const uint8_t *blocks, int32_t w, int32_t h, int bc3, uint8_t *rgba);
/* ETC2 RGBA8 (EAC alpha; 16 bytes a block), ETC2 RGB8 and EAC R11 (8 bytes) blocks of a w x h image (w, h multiples
 * of 4; the phones' HD pages and tiles, stage R4f; kp_etc.c): encoded from RGBA8 or one byte a texel, and decoded (the
 * colour modes the encoder writes and ETC2's planar mode; T and H decode as black). */
void kp_etc2_encode(const uint8_t *rgba, int32_t w, int32_t h, uint8_t *blocks);
void kp_etc2_rgb_encode(const uint8_t *rgba, int32_t w, int32_t h, uint8_t *blocks);
void kp_etc2_rgb_decode(const uint8_t *blocks, int32_t w, int32_t h, uint8_t *rgba);
void kp_eac_r11_encode(const uint8_t *r, int32_t w, int32_t h, uint8_t *blocks);
/* EAC RG11 (16 bytes a block: R11's red, then green): the phones' normal maps and occlusion. */
void kp_eac_rg11_encode(const uint8_t *r, const uint8_t *g, int32_t w, int32_t h, uint8_t *blocks);
void kp_eac_rg11_decode(const uint8_t *blocks, int32_t w, int32_t h, uint8_t *r, uint8_t *g);
void kp_etc2_decode(const uint8_t *blocks, int32_t w, int32_t h, uint8_t *rgba);
void kp_eac_r11_decode(const uint8_t *blocks, int32_t w, int32_t h, uint8_t *r);
/* An HD page's blocks of `format` (KP_HD_FORMAT_*) decoded: RGBA8 diffuse, the team mask's red (one byte a texel);
 * -1 out of memory, or ASTC's blocks (the GPU's alone: the product has no ASTC codec). */
int kp_hd_page_decode(int format, const uint8_t *diffuse, const uint8_t *team, int32_t size, uint8_t *rgba,
	uint8_t *mask);

/* ------------------------------------------------------------------------------------------------------------- */
/* The world images: their data texture and the presentation of a world (kp_world_images.c)                      */
/* ------------------------------------------------------------------------------------------------------------- */

#define KP_IMAGE_DATA_WIDTH 256
#define KP_TEXELS_PER_SLOT 2
#define KP_FAMILY_CAPACITY_OVERWRITE KP_IMAGES
#define KP_FAMILY_CAPACITY_DEST 3000
#define KP_FAMILY_CAPACITY_DISTORT 1000
#define KP_IMAGE_SLOTS (KP_FAMILY_CAPACITY_OVERWRITE + KP_FAMILY_CAPACITY_DEST + KP_FAMILY_CAPACITY_DISTORT)
/* Each family's first global slot and its slot count. */
extern const int32_t KP_FAMILY_BASE[KP_FAMILIES], KP_FAMILY_CAPACITY[KP_FAMILIES];
#define KP_VIRTUAL_SIZE 256 /* a virtual frame's size travels as width * 256 + height */

/* A tick's composition plan of the uploaded image data. */
#define KP_PLAN_WINDOWS KP_FAMILY_CAPACITY_DISTORT /* at most one copy per distortion image */
typedef struct {
	KpWindow windows[KP_PLAN_WINDOWS];
	int32_t count, layers;
	int32_t slots[KP_FAMILIES]; /* each family's slots in use (0..its highest ever taken): the slots a pass draws */
} KpPlan;

/* The image data texture of every family (RGBA32F 256 x height):
 *   data[2 slot]     = (x, y, x - previous x, y - previous y) in 1/256 world pixels
 *   data[2 slot + 1] = ((frame * KINDS + kind) * 2 + hflip, rank, row, layer)
 * then, from texel KP_IMAGE_ORDER_TEXEL, four values a texel, each family's slots in rank order: value
 * KP_FAMILY_BASE[f] + n is the local slot family f draws n-th (its images first, lowest rank first, then its free
 * slots). The HD world draws its images in that order: they blend, so a translucent texel drawn before an image it
 * lies over would leave its rank in the depth and cut that image out. The SD passes draw in slot order.
 * Families own fixed slot ranges (overwrite, dest, distort); an image keeps its slot while it stays in its family.
 * Values change only when the image does; `writes` counts the changes of the last tick. */
#define KP_IMAGE_ORDER_TEXEL (KP_IMAGE_SLOTS * KP_TEXELS_PER_SLOT)
#define KP_IMAGE_DATA_RUNS (KP_FAMILIES + 1) /* the families' rows and the order's */
typedef struct {
	float *data;
	int32_t height;
	int32_t *slot_of[KP_FAMILIES]; /* image key -> local slot, -1 */
	int32_t *free[KP_FAMILIES], free_count[KP_FAMILIES], next_slot[KP_FAMILIES];
	uint32_t *seen, tick; /* per global slot */
	int32_t *key_of;      /* per global slot its image key, -1 when free */
	int32_t *rank_first, *rank_next; /* the order's scratch: per rank its first global slot, per slot the next */
	int32_t empty;
	int32_t writes;
	KpPlan plan; /* the plan of the data last written */
	int pending; /* written since the last upload */
	/* per family and the order, the texel rows changed since the last upload: lo..hi-1 (none when equal) */
	int32_t dirty_lo[KP_IMAGE_DATA_RUNS], dirty_hi[KP_IMAGE_DATA_RUNS];
} KpImageData;

/* A run of texel rows of the image data: row .. row + rows - 1. */
typedef struct {
	int32_t row, rows;
} KpRowRange;

int kp_image_data_init(KpImageData *d, int32_t empty, KpError *err);
void kp_image_data_free(KpImageData *d);
/* A tick's writes: begin, one put per image (0 when its family's slots are full), finish with the tick's plan
 * (the slots not written are freed). */
void kp_image_data_begin(KpImageData *d);
int kp_image_data_put(KpImageData *d, const KpDrawItem *it, int32_t row, int32_t layer);
void kp_image_data_finish(KpImageData *d, const KpWindow *windows, int32_t count, int32_t layers);
/* Ends a tick that failed before its plan: the slots written are uploaded and drawn with the plan before (the slots not
 * written are kept until a tick finishes). */
void kp_image_data_abort(KpImageData *d);
/* Per frame: whether the data is to be uploaded now (it then counts as uploaded), and the texel rows that changed as
 * *count ascending runs in ranges (a run per family that wrote and one for the order, the runs that touch joined;
 * *count 0: none). The upload goes on the write's own frame (the 1:1 port's rule of the frame after, spike 15, went
 * with stage S8). */
int kp_image_data_take(KpImageData *d, KpRowRange ranges[KP_IMAGE_DATA_RUNS], int *count);
/* Wants the HD pages of the frames the image data shows (hd: the HD frame table; kp_hd_cache.c). */
void kp_hd_cache_want_images(KpHdCache *c, const KpImageData *d, const KpFrameTable *hd);

/* A drawn image as the composition checks log it (kp_app_dev.c check_compose). */
typedef struct {
	int32_t key, left, top, w, h, grp, frame, global, flip, method, rank, row, layer, window, owner, param, virtual_;
} KpDrawn;

/* The presentation of an images world: its draw list into the image data, with the lookups and health bars. */
typedef struct {
	KpImages *w;
	const KpGrpMeta *grp;
	const KpFrameTable *frames;
	const KpLookups *lookups;
	KpBars *bars;          /* NULL: no health bars */
	struct KpGame *game;   /* the bars' units (NULL: a world without a game) */
	KpDrawList *list;
	int32_t warp_texture;  /* the GRP entry of the warp texture (image 210) */
	int32_t *first_frame;  /* [grp entry] cached frame_of, -2 unknown */
	int32_t drawn, overflow; /* overflow: images left out for a full family (a safeguard measure) */
	/* images left out for want of their row or warp frame (the data's fault, not the tick's), the count of the sync
	 * before (the host logs a change) and the first one's reason */
	int32_t skipped, skipped_before;
	KpError skip;
	int planned;
	KpRect planned_view;
	/* the drawn images of the last sync, when collecting (composition checks) */
	KpDrawn *collected;
	int32_t collected_count, collected_cap, collect;
	KpImageData *data;
	KpError error;
	int failed;
} KpImagesPresenter;

int kp_presenter_init(KpImagesPresenter *p, KpImages *w, const KpGrpMeta *grp, const KpFrameTable *frames,
	const KpLookups *lookups, KpBars *bars, struct KpGame *game, KpError *err);
void kp_presenter_free(KpImagesPresenter *p);
/* Writes the tick's draw list into the image data; collect keeps the drawn images. Returns the
 * images drawn, -1 with p->error. */
int32_t kp_presenter_sync(KpImagesPresenter *p, KpImageData *d, const KpRect *view, int replan, int collect);
void kp_presenter_restart(KpImagesPresenter *p);
/* Whether the plan was made for another view than `view`. */
int kp_presenter_needs_replan(const KpImagesPresenter *p, const KpRect *view);

/* ------------------------------------------------------------------------------------------------------------- */
/* Tilesets and the terrain (kp_tileset.c)                                                                         */
/* ------------------------------------------------------------------------------------------------------------- */

/* A tileset's palette, cycles, CV5 groups and megatile flags, minimap colours, megatile sheet and creep's edges. */
typedef struct {
	uint8_t rgb[768];
	uint8_t cycled[256]; /* 1 for an index inside a cycle range */
	KpCycles cycles;
	const uint8_t *cv5;
	uint32_t groups;
	const uint8_t *mflags; /* u32 per megatile */
	uint32_t megatiles;
	const uint8_t *mmni;
	size_t mmni_size;
	uint16_t sheet_count, sheet_columns;
	uint16_t edge_count;      /* 0: none */
	/* the sheet's and the edges' indices, and what the tileset owns */
	const uint8_t *sheet;     /* megatile t at ((t % columns) * 32, (t / columns) * 32): sheet_columns * 32 wide */
	int32_t sheet_w, sheet_h;
	const uint8_t *edges;     /* creep's edge frame k in the 32x32 cell at (k * 32, 0), 0 transparent: 32 high */
	uint8_t *owned_cv5, *owned_flags, *owned_mmni, *owned_sheet, *owned_edges;
} KpTileset;

/* Tileset `index` (KP_TILESET_NAMES) from tileset\<name>.cv5, .vx4, .vr4, .vf4, .wpe and .grp (ADR 0018, stage D3) by
 * the converter's rules (converter/kpf/kpd.py build_tileset): the palette, cycles (the profile's compiled CYCn), CV5,
 * megatile flags and MMNI; the sheet and the edges as indices. */
int kp_tileset_from_files(KpTileset *t, const KpFiles *files, int index, KpError *err);
void kp_tileset_free(KpTileset *t);
/* ImportMapTiles 0x445720 over the mission's MTXM: the megatile id of each cell (bit 15 the tile's high bit), w * h;
 * -1 with *bad_cell when a tile selects a missing CV5 group. */
int kp_terrain_import(const KpMission *m, const KpTileset *t, uint16_t *ids, int64_t *bad_cell);
/* The creep edge the game draws over a tile of megatile word `word` and border byte `border` (kp_creep.c): 1 + the frame
 * of the tileset's edges (border - 1, 0x4c03b0) where the word has bit 15 (0x40ad1b) and the frame is one of `edges`,
 * else 0. */
uint16_t kp_terrain_edge(uint16_t word, int64_t border, int32_t edges);

/* ------------------------------------------------------------------------------------------------------------- */
/* The fog's level map (kp_fog_layer.c)                                                                            */
/* ------------------------------------------------------------------------------------------------------------- */

/* The fog view's levels as a texture (one texel per 8 x 8 block, the level in R), with the rectangle that changed;
 * the tileset's dark rows (256 x 32) are the other texture. */
typedef struct {
	KpFogView view;
	int32_t width, height, block_x, block_y; /* the level map's size and its first cell's map block */
	int32_t dirty[4]; /* x0, y0, x1, y1 exclusive; x1 == 0: none */
	int on;
	int32_t map_x, map_y; /* the view's top-left map pixel of the last update */
} KpFogLayer;

void kp_fog_layer_init(KpFogLayer *f);
/* The levels for a w x h view whose top-left is map pixel (cam_x, cam_y) after the game's steps or a camera move. */
void kp_fog_layer_update(KpFogLayer *f, struct KpGame *g, int64_t cam_x, int64_t cam_y, int64_t w, int64_t h);
void kp_fog_layer_taken(KpFogLayer *f);

/* ------------------------------------------------------------------------------------------------------------- */
/* The layouts (kp_layout.c)                                                                                       */
/* ------------------------------------------------------------------------------------------------------------- */

#define KP_LAYOUT_MIN_W 640
#define KP_LAYOUT_MIN_H 480
#define KP_LAYOUT_NARROW_MIN_W 502
#define KP_LAYOUT_CONSOLE_ROWS 80
enum { KP_SCALE_AUTO, KP_SCALE_INTEGER, KP_SCALE_SHARP };
enum { KP_LAYOUT_CLASSIC, KP_LAYOUT_WIDE, KP_LAYOUT_NARROW, KP_LAYOUT_EDGES, KP_LAYOUT_NARROW_ORIGINAL, KP_LAYOUTS };
#define KP_LAYOUT_GROUPS 3

/* The logical screen of a window: the mode, the scale (window pixels per logical pixel) and its size, at least
 * min_w x min_h. fixed: a fixed 640 x 480 screen (the glue's), which auto fits with the sharp scale. */
void kp_layout_screen(int32_t win_w, int32_t win_h, int mode, int fixed, int32_t min_w, int32_t min_h, int *mode_out,
	double *scale, int32_t *w, int32_t *h);
/* The window's rectangle a screen is drawn into, in pixels from the drawable's left and bottom (x, y, w, h): a game's
 * the safe area (safe: its panels and buttons off the system bars, the cutouts and the gesture zones), a fixed screen's
 * (the glue's 640 x 480) the whole drawable, full_w x full_h. */
void kp_layout_picture(int fixed, int32_t full_w, int32_t full_h, const int32_t safe[4], int32_t picture[4]);
/* The smallest logical screen of a window (a fixed screen: always 640 x 480). */
void kp_layout_minimum(int32_t win_w, int32_t win_h, int fixed, int32_t *min_w, int32_t *min_h);
/* The layout of a screen w wide: by its width with the console's pieces (edges: without their ornaments), without them
 * the arrangements of the original's art, which has nothing to close a gap with. */
int kp_layout_choose(int32_t w, int pieces, int edges);
const char *kp_layout_name(int layout);

typedef struct {
	int32_t x, y, x0, x1, left, right;
	int above;
} KpLayoutGroup;

typedef struct {
	int layout;
	KpLayoutGroup groups[KP_LAYOUT_GROUPS]; /* the minimap, the status panel, the portrait and the card */
	int32_t dy, w, h, top;
} KpArrangement;

/* The arrangement of a w x h logical screen; layout -1: chosen by its width, with the pieces and their ornaments. */
void kp_layout_arrange(KpArrangement *a, int32_t w, int32_t h, int layout);
/* Where a dialog of `bin` with the original rectangle rect (left, top, right, bottom) goes: 1 and its top-left, or 0
 * for a dialog the layouts do not place. */
int kp_layout_place(const KpArrangement *a, const char *bin, const int32_t rect[4], int32_t *x, int32_t *y);

/* ------------------------------------------------------------------------------------------------------------- */
/* The canvas and the cursor (kp_canvas.c)                                                                         */
/* ------------------------------------------------------------------------------------------------------------- */

#define KP_CANVAS_PANELS 64

/* A panel of a frame: the text lines' layer (lines), or a dialog with its BIN and original rectangle (backdrop: a
 * dialog composited onto the backdrop, under the console). */
typedef struct {
	int lines, backdrop;
	int32_t dialog;
	char bin[64];
	int32_t rect[4];
} KpPanel;

typedef struct {
	int32_t dialog, x, y;
} KpPlace;

typedef struct {
	KpUiCanvas canvas;
	int32_t w, h;
	int resized;
	KpArrangement arrangement;
	KpPlace places[KP_CANVAS_PANELS];
	int place_count;
	int32_t dirty[4]; /* the last compose's changed rectangle, x1 == 0 none */
	uint8_t blend_row[256];
	int blend_valid;
	const KpHdUi *hd_ui; /* the HD interface's art (the console's pieces), or NULL */
	/* a layer of our own over the panels (the touch pad, ADR 0014; index 0 transparent, the blend colour translucent:
	 * kp_ui_canvas_translucent) at (overlay_x, overlay_y), or NULL */
	const KpUiSurface *overlay;
	int32_t overlay_x, overlay_y;
} KpCanvas;

void kp_canvas_init(KpCanvas *c);
void kp_canvas_free(KpCanvas *c);
/* The panels of a game's interface: the text lines, then the open dialogs bottom first. */
int kp_canvas_hud_panels(KpUi *ui, KpPanel *out, int max);
/* Composes a frame for a w x h logical screen; returns 1 when something changed (c->dirty),
 * -1 out of memory. */
int kp_canvas_compose(KpCanvas *c, KpUi *ui, int32_t w, int32_t h, const KpPanel *panels, int count, int layout);
/* A fixed screen's frame: its dialog at (x, y) with the margins, or index 0 without a dialog (dialog -1). */
int kp_canvas_compose_screen(KpCanvas *c, KpUi *ui, int32_t w, int32_t h, int32_t dialog, int32_t x, int32_t y);
/* The place of a dialog in the last arrangement, or 0. */
int kp_canvas_place(const KpCanvas *c, int32_t dialog, int32_t *x, int32_t *y);
/* The canvas's index and opacity at a logical point (0, 0 outside). */
void kp_canvas_pixel(const KpCanvas *c, int32_t x, int32_t y, uint8_t *index, uint8_t *alpha);

/* A cinematic's decoded picture (ADR 0015) as its three 8-bit planes, for a drawing that converts it itself (the GPU,
 * kp_gfx_video): rows of strides bytes, row 0 on top; Y, Cb and Cr, or G, B and R for an sRGB picture (the SD movies'
 * 4:4:4). YCbCr in 0..1 (a sample / 255) turns into RGB by l = (y - y_low) * y_scale, then r = l + cr_r * cr,
 * g = l - cb_g * cb - cr_g * cr, b = l + cb_b * cb, cb and cr less 128/255. */
typedef struct {
	const uint8_t *planes[3];
	int32_t strides[3];
	int32_t width, height;      /* the luma plane's: the clip's size */
	int32_t chroma_w, chroma_h; /* the other two planes' */
	int srgb;                   /* the planes are G, B, R */
	float y_low, y_scale, cr_r, cb_g, cr_g, cb_b;
} KpVideoPicture;

#define KP_CURSOR_SIZE 128
typedef struct {
	int64_t key;   /* the frame's key, -1 none */
	int shown;
	int32_t w, h, dx, dy;
	uint8_t pixels[KP_CURSOR_SIZE * KP_CURSOR_SIZE]; /* w x h, index 0 transparent */
	int changed;   /* a new frame to upload */
} KpCursor;

void kp_cursor_init(KpCursor *c);
/* Each frame: the cursor of interface ui (NULL: none shows) at the logical point (has_point: the mouse reached the
 * window). Returns whether a cursor is shown. */
int kp_cursor_frame(KpCursor *c, KpUi *ui, int has_point);

/* ------------------------------------------------------------------------------------------------------------- */
/* The vector text (kp_vtext.c; ADR 0011, 2026-10-26)                                                             */
/* ------------------------------------------------------------------------------------------------------------- */

/* Not the original's: at large scales the canvas's glyphs (KpUiCanvasGlyph) are drawn with a TrueType font in the
 * window's resolution. The original's layout stays by words: the glyphs of a word (cells that follow each other) keep
 * the TrueType advances and kerning, centred in the word's FNT cells and narrowed when wider than them, on the FNT
 * font's baseline at its cap height. */
#define KP_VTEXT_AUTO_SCALE 2.0 /* auto (KP_VIDEO_AUTO; the modes KP_VTEXT_* in kp_ui.h): the vector text from here up */
#define KP_VTEXT_FONTS KP_UI_FNT_FONTS /* font8, font10, font14, font16, font16x */
#define KP_VTEXT_TTFS 2         /* the TrueType fonts: the text's, and font16x's wide one */
#define KP_VTEXT_QUAD_FLOATS 16 /* a quad: its rectangle and atlas texels, its clip, its colour (logical screen) */

typedef struct {
	int16_t x, y, w, h;       /* its atlas rectangle (w 0: no bitmap) */
	float left, top;          /* the bitmap's offset from the pen and the baseline, in window pixels */
	float advance;            /* the TrueType advance in window pixels */
	int ok;
} KpVTextGlyph;

typedef struct {
	void *info[KP_VTEXT_TTFS]; /* stbtt_fontinfo of each TrueType font */
	int ttf_count;
	int32_t cap_units[KP_VTEXT_TTFS]; /* each one's 'H' height in font units */
	uint8_t codes[256];       /* the byte slots (their code points in windows-1252) every font has a glyph for */
	double size;              /* the cap height over the FNT font's (1: equal; a development switch) */
	double scale, built_size; /* the scale and size the atlas was made for (scale 0: none) */
	uint32_t atlas_serial;    /* the atlases made so far */
	float px[KP_VTEXT_FONTS]; /* the TrueType scale of each font (window pixels per font unit) */
	const uint8_t *fonts[KP_VTEXT_FONTS];
	int cap_top[KP_VTEXT_FONTS], cap_height[KP_VTEXT_FONTS];
	KpVTextGlyph glyphs[KP_VTEXT_FONTS][256];
	int32_t *extra_codes;     /* the glyph supplement's code points every font has, ascending (kp_vtext_add_codes) */
	int32_t extra_count;
	KpVTextGlyph *extra[KP_VTEXT_FONTS]; /* their glyphs */
	uint8_t *atlas;           /* one byte of coverage per texel, rows top first */
	int32_t atlas_w, atlas_h;
	int atlas_changed;        /* a new atlas to upload */
	float *quads;             /* the frame's quads, KP_VTEXT_QUAD_FLOATS each */
	int32_t quad_count, quad_capacity;
	int quads_changed;        /* quads laid out again since kp_gfx_text uploaded them */
	KpUiCanvasGlyph *laid;    /* the canvas glyphs the quads were laid out for (-1 count: none) */
	int32_t laid_count, laid_capacity;
	uint32_t laid_serial;     /* the atlas they were laid out on */
} KpVText;

/* The TrueType fonts (kept, not copied): the text's, then font16x's wide one (count 1: one for all): 0, or -1 when
 * one is no TrueType font. */
int kp_vtext_init(KpVText *v, const uint8_t *const ttf[], const size_t sizes[], int count);
void kp_vtext_free(KpVText *v);
/* The code points past the byte slots to draw too, ascending (the glyph supplement's; ADR 0016): those every font has
 * join the atlas, the others stay the supplement's bitmaps. Before the first atlas: 0, or -1 out of memory (none). */
int kp_vtext_add_codes(KpVText *v, const int32_t *codes, int32_t count);
/* A glyph supplement (kp_ui_text.h's form; ADR 0016) of an FNT font from a TrueType font: the code points (ascending)
 * the TrueType font has, rasterized to the FNT font's cap height, fitted to its Latin letters' widths, kept in its cell
 * and in its glyphs' masks, into *out (malloc'd); the ones it lacks in missing (room for count). 0, or -1 with err. */
int kp_fnt_supplement(const uint8_t *ttf, size_t ttf_size, const uint8_t *font, size_t font_size, const int32_t *codes,
	int32_t count, uint8_t **out, size_t *out_size, int32_t *missing, int32_t *missing_count, char *err, size_t err_size);
/* The mode's verdict for a scale: whether the vector text draws. */
int kp_vtext_active(int mode, double scale);
/* The atlas of the FNT fonts (font_files[5]: font8, font10, font14, font16, font16x) at a scale, made again when the
 * scale or the fonts change: 0, or -1 (too large or out of memory: the text stays bitmaps). */
int kp_vtext_atlas(KpVText *v, const uint8_t *const fonts[KP_VTEXT_FONTS], const size_t sizes[KP_VTEXT_FONTS],
	double scale);
/* The quads of the canvas's vector glyphs at the atlas's scale, laid out by words (a shadow quad under each glyph with
 * a drop shadow) when the glyphs or the atlas changed (quads_changed); returns their count. */
int32_t kp_vtext_quads(KpVText *v, const KpUiCanvas *c);

/* ------------------------------------------------------------------------------------------------------------- */
/* A cinematic's subtitle (kp_caption.c; not the original's: Remastered's .srt, 2026-11-04)                        */
/* ------------------------------------------------------------------------------------------------------------- */

#define KP_CAPTION_LINES 8 /* the lines a subtitle is laid out in at most (its own and the wrapped ones) */

typedef struct {
	uint8_t *rgba; /* premultiplied RGBA, rows top first: white text in a dark outline over nothing */
	int32_t w, h;
	int lines;
} KpCaption;

/* The subtitle text[0..len) (UTF-8, its lines apart by '\n', each wrapped at spaces to max_w pixels) laid out
 * centred in the TrueType font at a line height of px pixels into out (freed and made again); 0, or -1 (no font, no
 * glyph, out of memory) with out empty. Characters the font lacks and bytes that are not UTF-8 are left out. */
int kp_caption_render(KpCaption *out, const uint8_t *ttf, size_t ttf_size, const char *text, size_t len, float px,
	int32_t max_w);
void kp_caption_free(KpCaption *c);

#ifdef __cplusplus
}
#endif

#endif
