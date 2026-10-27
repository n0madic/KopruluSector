/*
 * The narrow backend under kp_gfx's passes (ADR 0012 decision 3): named textures, render targets and programs, their
 * uploads, draws of a vertex count with the textures and constants bound, target copies and readback. The passes
 * (kp_gfx_passes.c) see nothing else; kp_gfx_gl.c implements it over GLES 3.0 / WebGL2 / desktop GL core.
 */
#ifndef KP_GFX_INTERNAL_H
#define KP_GFX_INTERNAL_H

#include "kp_gfx.h"

/* The textures. */
enum {
	KP_TEX_REMAP, KP_TEX_TABLES, KP_TEX_PAGES, KP_TEX_FRAMES, KP_TEX_IMAGE_DATA, KP_TEX_BARS, KP_TEX_SHEET, KP_TEX_MAP,
	KP_TEX_FOG_LEVELS, KP_TEX_FOG_DARK, KP_TEX_CANVAS, KP_TEX_BLEND, KP_TEX_CURSOR, KP_TEX_PALETTE, KP_TEX_TEXT_ATLAS,
	KP_TEX_TEXT_QUADS, KP_TEX_HD_DIFFUSE, KP_TEX_HD_TEAM, KP_TEX_HD_FRAMES, KP_TEX_HD_SHEET, KP_TEX_HD_MAP,
	KP_TEX_HD_COLORS, KP_TEX_HD_SLOTS, KP_TEX_HD_WATER, KP_TEX_HD_NORMAL_A, KP_TEX_HD_NORMAL_B, KP_TEX_HD_TONE,
	KP_TEX_UI_IDS, KP_TEX_UI_ITEMS, KP_TEX_UI_ATLAS, KP_TEXTURES
};
/* Their formats: one byte (an index), two bytes (index and opacity), four floats, one unsigned 16-bit integer, four
 * bytes (a colour), the HD art's BC1 and BC3 blocks (S3TC) and its EAC R11, ETC2 RGB8 and RGBA8 ones (kp_gb_takes_blocks). */
enum {
	KP_FMT_R8, KP_FMT_RG8, KP_FMT_RGBA32F, KP_FMT_R16UI, KP_FMT_RGBA8, KP_FMT_BC1, KP_FMT_BC3, KP_FMT_EAC_R11,
	KP_FMT_ETC2_RGB8, KP_FMT_ETC2_RGBA8, KP_FORMATS
};
/* The render targets: the index target (R8, the palette index, with a 24-bit depth buffer: the images' ranks), its
 * copy, the RGB target of the sharp upscale (linear); the HD world (RGBA8, linear, with depth: the ranks; the logical
 * screen times the HD art's scale, kp_gb_hd_target); KP_TARGET_WINDOW the window's framebuffer. */
enum { KP_TARGET_INDEX, KP_TARGET_DEST, KP_TARGET_RGB, KP_TARGET_HD, KP_TARGETS, KP_TARGET_WINDOW = -1 };
/* The programs. */
enum {
	KP_PROG_TERRAIN, KP_PROG_IMAGES, KP_PROG_DEST, KP_PROG_DISTORT, KP_PROG_FOG, KP_PROG_UI,
	KP_PROG_PALETTE, KP_PROG_SHARP, KP_PROG_TEXT, KP_PROG_HD_TERRAIN, KP_PROG_HD_IMAGES, KP_PROG_HD_FOG,
	KP_PROG_HD_COMPOSITE, KP_PROGRAMS
};
/* The constants (vec4 each). */
enum {
	KP_U_TERRAIN, KP_U_TERRAIN_SIZE, KP_U_VIEW, KP_U_INTERP, KP_U_WINDOW, KP_U_SLOT_BASE, KP_U_LOOKUPS, KP_U_FOG,
	KP_U_FOG_ORIGIN, KP_U_CURSOR, KP_U_BLEND, KP_U_SOURCE_SIZE, KP_U_HD, KP_U_WATER, KP_U_HD_CURSOR_RECT,
	KP_U_HD_CURSOR_SRC, KP_U_HD_CURSOR_LAYER, KP_U_ART, KP_UNIFORMS
};
/* The samplers' names and the textures they read (a target's colour: KP_SAMPLE_TARGET + target). */
enum {
	KP_S_TERRAIN_MAP, KP_S_TERRAIN_SHEET, KP_S_IMAGE_DATA, KP_S_FRAME_TABLE, KP_S_ATLAS, KP_S_REMAP, KP_S_BARS, KP_S_DEST,
	KP_S_TABLES, KP_S_FOG_LEVELS, KP_S_FOG_DARK, KP_S_UI_CANVAS, KP_S_UI_BLEND, KP_S_UI_CURSOR, KP_S_INDEX_TARGET,
	KP_S_RGB_TARGET, KP_S_PALETTE, KP_S_TEXT_ATLAS, KP_S_TEXT_QUADS, KP_S_HD_DIFFUSE, KP_S_HD_TEAM, KP_S_HD_FRAMES,
	KP_S_HD_SHEET, KP_S_HD_MAP, KP_S_HD_COLORS, KP_S_HD_TARGET, KP_S_HD_SLOTS, KP_S_HD_WATER, KP_S_HD_NORMAL_A,
	KP_S_HD_NORMAL_B, KP_S_HD_TONE, KP_S_UI_IDS, KP_S_UI_ITEMS, KP_S_UI_ATLAS, KP_SAMPLERS
};
#define KP_SAMPLE_TARGET 1000
/* The vector text's quads per row of their texture (four texels each). */
#define KP_TEXT_QUADS_PER_ROW 256

typedef struct {
	int sampler; /* KP_S_* */
	int texture; /* KP_TEX_*, or KP_SAMPLE_TARGET + KP_TARGET_* */
} KpGfxBinding;

/* Depth state of a draw (the test passes a greater rank). */
enum { KP_DEPTH_OFF, KP_DEPTH_GREATER };

typedef struct KpGfxBackend KpGfxBackend;

KpGfxBackend *kp_gb_new(const KpGfxApi *api, char *err, size_t err_size);
/* Frees the backend; lost: its context is gone, and its objects with it. */
void kp_gb_free(KpGfxBackend *b, int lost);
/* (Re)makes the texture (w x h x layers) with no contents; layers 0 a 2D texture. */
int kp_gb_texture(KpGfxBackend *b, int tex, int format, int32_t w, int32_t h, int32_t layers);
int kp_gb_has_texture(const KpGfxBackend *b, int tex, int32_t *w, int32_t *h);
/* Uploads a rectangle (rows of stride bytes; stride 0: tight) into a texture's layer. */
void kp_gb_upload(KpGfxBackend *b, int tex, int32_t layer, int32_t x, int32_t y, int32_t w, int32_t h, const void *data,
	size_t stride);
/* Sizes the targets (the index target, its copy and the RGB target are the logical screen's size). */
int kp_gb_targets(KpGfxBackend *b, int32_t w, int32_t h);
/* Binds a target with its viewport. */
void kp_gb_bind(KpGfxBackend *b, int target, int32_t vx, int32_t vy, int32_t vw, int32_t vh);
void kp_gb_clear(KpGfxBackend *b, float r, float gg, float bb, float a, int depth);
/* The constants of the next draws of a program. */
void kp_gb_uniform(KpGfxBackend *b, int prog, int u, float x, float y, float z, float w);
/* Draws vertices of a program with the bindings and the depth state (write: the depth mask). */
void kp_gb_draw(KpGfxBackend *b, int prog, int32_t vertices, const KpGfxBinding *bind, int count, int depth, int write);
/* Blends the next draws' colour over the target by their alpha (on), or writes it (off, the default). */
void kp_gb_blend(KpGfxBackend *b, int on);
/* Blends the next draws' premultiplied colour over the target (the HD world: a colour with alpha 0 adds). */
void kp_gb_blend_premultiplied(KpGfxBackend *b);
/* Whether the context takes a block format's textures (KP_FMT_BC1 ... KP_FMT_ETC2_RGBA8); tried once each on a small
 * texture array. */
int kp_gb_takes_blocks(KpGfxBackend *b, int format);
/* Uploads one layer of blocks (w x h pixels, size bytes) into a texture of a block format. */
void kp_gb_upload_blocks(KpGfxBackend *b, int tex, int32_t layer, int32_t w, int32_t h, const void *data, size_t size);
/* Sizes the HD target (w x h, RGBA8 with depth); its colour is sampled linearly. */
int kp_gb_hd_target(KpGfxBackend *b, int32_t w, int32_t h);
/* Copies a target's colour into another of the same size. */
void kp_gb_copy(KpGfxBackend *b, int from, int to);
/* Reads a target's colour (RGBA8, rows bottom-up). */
int kp_gb_read(KpGfxBackend *b, int target, int32_t x, int32_t y, int32_t w, int32_t h, uint8_t *out);
/* Whether the backend's objects still exist in the current context (0: the context lost them). */
int kp_gb_alive(const KpGfxBackend *b);

/* The shader sources (kp_gfx_shaders.c). */
extern const char KP_GFX_FULLSCREEN_VS[], KP_GFX_TERRAIN_FS[], KP_GFX_WORLD_VS[], KP_GFX_WORLD_FS[], KP_GFX_DEST_FS[],
	KP_GFX_DISTORT_FS[], KP_GFX_FOG_FS[], KP_GFX_UI_FS[], KP_GFX_PALETTE_FS[], KP_GFX_SHARP_FS[], KP_GFX_TEXT_VS[],
	KP_GFX_TEXT_FS[], KP_GFX_HD_TERRAIN_FS[], KP_GFX_HD_WORLD_VS[], KP_GFX_HD_WORLD_FS[], KP_GFX_HD_FOG_FS[],
	KP_GFX_HD_COMPOSITE_FS[];

#endif
