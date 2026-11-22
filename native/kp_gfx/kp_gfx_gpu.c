/*
 * kp_gfx's backend over SDL_GPU (ADR 0028; kp_gfx_internal.h, behind kp_gfx_backend.c): Metal on macOS, Vulkan on
 * Windows and Linux. The application owns the device and the window's claim; the backend makes its shaders (the
 * generated kp_gfx_gpu_shaders.c), every pipeline of the passes (KP_GFX_GPU_PIPELINES) and its textures.
 *
 * A target's memory is laid out as GL's (the vertex stages negate gl_Position.y): row 0 at the bottom, read back
 * bottom-up. The window is an offscreen RGBA8 texture, blitted flipped to the swapchain by kp_gb_present.
 *
 * Everything runs on the main thread into one command buffer a frame, acquired at its first use and submitted by the
 * present (or earlier: a read, a full staging buffer). A bound target's render pass opens at its first draw, loading
 * what it held or clearing it (a clear waits for a draw: one no draw took up clears in a pass of its own when the
 * target changes, before a copy, a read and a present). The uploads go through one staging buffer of a fixed size,
 * in stripes of rows past it.
 */
#include "kp_gfx_backend.h"
#include "kp_gfx_gpu_glsl.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void gpu_free(KpGfxBackend *b, int lost);
static void gpu_drop(KpGfxBackend *b, int tex);

/* SDL_GPU's limits the backend checks itself (SDL checks them in its debug mode only): a texture's side and layers. */
#define KP_GPU_SIDE_MAX 16384
#define KP_GPU_LAYERS_MAX 2048
/* The uploads' staging buffer; an upload's offset in it is a multiple of KP_GPU_STAGING_ALIGN (four floats, a block). */
#define KP_GPU_STAGING_BYTES (16u << 20)
#define KP_GPU_STAGING_ALIGN 16u
/* The generated stages and the pipelines at most. */
#define KP_GPU_STAGES_MAX 32
#define KP_GPU_PIPELINES_MAX 32
/* No target bound. */
#define NO_TARGET (-2)
/* The vertex constants: the program's, then the viewport's mapping (kp_gfx_gpu_glsl.h). */
typedef char kp_gpu_viewport_offset_check[KP_GPU_VIEWPORT_OFFSET == 16 * KP_UNIFORMS ? 1 : -1];

/* The formats' SDL_GPU formats, in KP_FMT_* order (the blocks' in KP_HD_CODEC_* order): ETC2 and EAC have none, and
 * the passes decode such pages; bytes: a texel's (0 the blocks). */
static const struct {
	SDL_GPUTextureFormat format;
	int bytes;
} FORMATS[KP_FORMATS] = { { SDL_GPU_TEXTUREFORMAT_R8_UNORM, 1 }, { SDL_GPU_TEXTUREFORMAT_R8G8_UNORM, 2 },
	{ SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT, 16 }, { SDL_GPU_TEXTUREFORMAT_R16_UINT, 2 },
	{ SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, 4 }, { SDL_GPU_TEXTUREFORMAT_R16G16_UINT, 4 },
	{ SDL_GPU_TEXTUREFORMAT_BC1_RGBA_UNORM, 0 }, { SDL_GPU_TEXTUREFORMAT_BC3_RGBA_UNORM, 0 },
	{ SDL_GPU_TEXTUREFORMAT_INVALID, 0 }, { SDL_GPU_TEXTUREFORMAT_INVALID, 0 }, { SDL_GPU_TEXTUREFORMAT_INVALID, 0 },
	{ SDL_GPU_TEXTUREFORMAT_INVALID, 0 }, { SDL_GPU_TEXTUREFORMAT_ASTC_4x4_UNORM, 0 },
	{ SDL_GPU_TEXTUREFORMAT_ASTC_6x6_UNORM, 0 }, { SDL_GPU_TEXTUREFORMAT_ASTC_8x8_UNORM, 0 } };

/* The samplers: GL's filters and wraps (kp_gfx_gl.c's parameters()). */
enum { SAMPLER_NEAREST, SAMPLER_LINEAR, SAMPLER_LINEAR_REPEAT, SAMPLERS };

typedef struct {
	SDL_GPUTexture *tex;
	int format, kind, sampler; /* KP_FMT_*, KP_GPU_KIND_*, SAMPLER_* */
	int32_t w, h, layers;
} Texture;

typedef struct {
	SDL_GPUTexture *color, *depth;
	int32_t w, h;
} Target;

struct KpGfxBackend {
	const KpGfxBackendOps *ops; /* first: kp_gfx_backend.c's dispatch */
	KpGfxApi api;
	SDL_GPUDevice *device;
	SDL_Window *window;
	SDL_GPUTextureFormat depth_format;
	SDL_GPUShader *shaders[KP_GPU_STAGES_MAX];
	SDL_GPUGraphicsPipeline *pipelines[KP_GPU_PIPELINES_MAX];
	SDL_GPUSampler *samplers[SAMPLERS];
	SDL_GPUTexture *dummies[KP_GPU_KINDS]; /* one zero texel of each kind a stage reads */
	SDL_GPUTransferBuffer *staging;
	Texture textures[KP_TEXTURES];
	Target targets[KP_TARGETS];
	Target screen;          /* the window's offscreen texture */
	int screen_sized;       /* its size checked against the window's since the last present */
	int32_t hd_failed_w, hd_failed_h; /* the size the HD target could not be made at (not tried again) */
	int takes[KP_FORMATS];  /* a block format's textures: 0 untried, 1 yes, -1 no */
	float u[KP_PROGRAMS][KP_UNIFORMS][4]; /* each program's constants, as its stages' blocks lay them out */
	int blend;              /* KP_GPU_BLEND_* */
	/* the frame's command buffer, the pass open in it (a render or a copy pass, not both) */
	SDL_GPUCommandBuffer *cmd;
	SDL_GPURenderPass *pass;
	SDL_GPUCopyPass *copy;
	int pass_target, pass_pipeline; /* the open render pass's target and pipeline (-1 none yet) */
	int bound;                      /* the target bound (KP_TARGET_*, NO_TARGET) */
	/* its viewport: GL's within the target (none: empty), and the vertex stages' mapping of GL's into it (the vertex
	 * constants after the program's, KP_GPU_VIEWPORT_OFFSET) */
	SDL_GPUViewport viewport;
	int viewport_empty;
	float vertex_u[KP_UNIFORMS + 1][4];
	struct {
		int on, target, depth;
		SDL_FColor color;
	} clear;                        /* the bound target's clear no draw took up yet */
	uint32_t staged;                /* the staging buffer's bytes the command buffer uploads from */
	SDL_GPUFence *frame_fence;      /* the last frame submitted without a swapchain (a hidden window's) */
	int unpiped;                    /* a draw had no pipeline (logged once) */
	/* the textures that could not be made since gpu_take_failures, the last described (kp_gfx_gl.c's accounting) */
	int32_t failures;
	char failure[160];
	int failed_tex;
	int32_t failed_w, failed_h, failed_layers;
	char version[200];
};

static int is_blocks(int format) { return format >= KP_FMT_BLOCKS && format < KP_FORMATS; }

static int is_uint(int format) { return format == KP_FMT_R16UI || format == KP_FMT_RG16UI; }

static void block_shape(int format, int *side, int *bytes) { kp_hd_codec_block(format - KP_FMT_BLOCKS, side, bytes); }

static size_t blocks_size(int format, int32_t w, int32_t h)
{
	int side, bytes;
	block_shape(format, &side, &bytes);
	return (size_t)((w + side - 1) / side) * (size_t)((h + side - 1) / side) * (size_t)bytes;
}

/* --- the command buffer and its passes --- */

static SDL_GPUCommandBuffer *command(KpGfxBackend *b)
{
	if (!b->cmd) b->cmd = SDL_AcquireGPUCommandBuffer(b->device);
	return b->cmd;
}

static void end_render_pass(KpGfxBackend *b)
{
	if (!b->pass) return;
	SDL_EndGPURenderPass(b->pass);
	b->pass = NULL;
}

static void end_passes(KpGfxBackend *b)
{
	end_render_pass(b);
	if (b->copy) {
		SDL_EndGPUCopyPass(b->copy);
		b->copy = NULL;
	}
}

static SDL_GPUCopyPass *copy_pass(KpGfxBackend *b)
{
	end_render_pass(b);
	if (!b->copy && command(b)) b->copy = SDL_BeginGPUCopyPass(b->cmd);
	return b->copy;
}

/* Submits the command buffer (none: nothing); the staging buffer is the next one's from its start. */
static void submit(KpGfxBackend *b)
{
	end_passes(b);
	if (b->cmd) SDL_SubmitGPUCommandBuffer(b->cmd);
	b->cmd = NULL;
	b->staged = 0;
}

/* Submits the command buffer with a fence (NULL: nothing submitted, or no fence). */
static SDL_GPUFence *submit_fenced(KpGfxBackend *b)
{
	SDL_GPUFence *f = NULL;
	end_passes(b);
	if (b->cmd) f = SDL_SubmitGPUCommandBufferAndAcquireFence(b->cmd);
	b->cmd = NULL;
	b->staged = 0;
	return f;
}

static void wait_fence(KpGfxBackend *b, SDL_GPUFence **f)
{
	if (!*f) return;
	SDL_WaitForGPUFences(b->device, true, f, 1);
	SDL_ReleaseGPUFence(b->device, *f);
	*f = NULL;
}

/* Submits the command buffer and waits until the GPU has done it. */
static void submit_and_wait(KpGfxBackend *b)
{
	SDL_GPUFence *f = submit_fenced(b);
	wait_fence(b, &f);
}

static const Target *target_of(const KpGfxBackend *b, int target)
{
	return target == KP_TARGET_WINDOW ? &b->screen : &b->targets[target];
}

/* Opens the render pass of the bound target (draw: with its viewport); 0, else -1 (no target, not made). */
static int begin_pass(KpGfxBackend *b, int draw)
{
	SDL_GPUColorTargetInfo color;
	SDL_GPUDepthStencilTargetInfo depth;
	const Target *t;
	int clear;
	if (b->pass) return 0;
	if (b->bound == NO_TARGET) return -1;
	t = target_of(b, b->bound);
	if (!t->color || !command(b)) return -1;
	if (b->copy) {
		SDL_EndGPUCopyPass(b->copy);
		b->copy = NULL;
	}
	clear = b->clear.on && b->clear.target == b->bound;
	memset(&color, 0, sizeof color);
	color.texture = t->color;
	color.clear_color = b->clear.color;
	color.load_op = clear ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
	color.store_op = SDL_GPU_STOREOP_STORE;
	memset(&depth, 0, sizeof depth);
	depth.texture = t->depth;
	depth.clear_depth = 0.0f; /* the ranks pass GREATER */
	depth.load_op = clear && b->clear.depth ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
	depth.store_op = SDL_GPU_STOREOP_STORE;
	depth.stencil_load_op = SDL_GPU_LOADOP_DONT_CARE;
	depth.stencil_store_op = SDL_GPU_STOREOP_DONT_CARE;
	b->pass = SDL_BeginGPURenderPass(b->cmd, &color, 1, t->depth ? &depth : NULL);
	if (!b->pass) return -1;
	if (clear) b->clear.on = 0;
	b->pass_target = b->bound;
	b->pass_pipeline = -1;
	if (draw) SDL_SetGPUViewport(b->pass, &b->viewport);
	return 0;
}

/* The clear no draw took up, in a pass of its own. */
static void flush_clear(KpGfxBackend *b)
{
	int bound = b->bound;
	if (!b->clear.on) return;
	end_render_pass(b);
	b->bound = b->clear.target;
	if (begin_pass(b, 0) == 0) end_render_pass(b);
	b->clear.on = 0;
	b->bound = bound;
}

/* A target about to be released: its pass ended, its clear and its binding dropped. */
static void forget_target(KpGfxBackend *b, int target)
{
	if (b->pass && b->pass_target == target) end_render_pass(b);
	if (b->clear.on && b->clear.target == target) b->clear.on = 0;
	if (b->bound == target) b->bound = NO_TARGET;
}

/* --- textures, targets and the uploads --- */

static SDL_GPUTexture *make_texture(KpGfxBackend *b, SDL_GPUTextureType type, SDL_GPUTextureFormat format,
	SDL_GPUTextureUsageFlags usage, int32_t w, int32_t h, int32_t layers)
{
	SDL_GPUTextureCreateInfo info;
	if (w < 1 || h < 1 || layers < 1 || w > KP_GPU_SIDE_MAX || h > KP_GPU_SIDE_MAX || layers > KP_GPU_LAYERS_MAX
		|| format == SDL_GPU_TEXTUREFORMAT_INVALID)
		return NULL;
	memset(&info, 0, sizeof info);
	info.type = type;
	info.format = format;
	info.usage = usage;
	info.width = (Uint32)w;
	info.height = (Uint32)h;
	info.layer_count_or_depth = (Uint32)layers;
	info.num_levels = 1;
	info.sample_count = SDL_GPU_SAMPLECOUNT_1;
	return SDL_CreateGPUTexture(b->device, &info);
}

static void release_texture(KpGfxBackend *b, SDL_GPUTexture **t)
{
	if (*t) SDL_ReleaseGPUTexture(b->device, *t);
	*t = NULL;
}

static void release_target(KpGfxBackend *b, Target *t)
{
	release_texture(b, &t->color);
	release_texture(b, &t->depth);
	t->w = t->h = 0;
}

/* A render target of w x h in format, with depth or not; sampled too (the window's blitted). */
static int make_target(KpGfxBackend *b, Target *t, int32_t w, int32_t h, SDL_GPUTextureFormat format, int depth)
{
	t->color = make_texture(b, SDL_GPU_TEXTURETYPE_2D, format,
		SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER, w, h, 1);
	if (depth) {
		t->depth = make_texture(b, SDL_GPU_TEXTURETYPE_2D, b->depth_format, SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET,
			w, h, 1);
	}
	if (!t->color || (depth && !t->depth)) {
		release_target(b, t);
		return -1;
	}
	t->w = w;
	t->h = h;
	return 0;
}

/* Room for n bytes in the staging buffer, mapped (its offset in *offset), or NULL. A full buffer is submitted and
 * waited for (GL's uploads are synchronous too); the first bytes of a command buffer cycle the buffer, which an
 * earlier frame may still read. */
static uint8_t *stage(KpGfxBackend *b, uint32_t n, uint32_t *offset)
{
	uint32_t at = (b->staged + KP_GPU_STAGING_ALIGN - 1) & ~(KP_GPU_STAGING_ALIGN - 1);
	uint8_t *map;
	if (n == 0 || n > KP_GPU_STAGING_BYTES) return NULL;
	if (at + n > KP_GPU_STAGING_BYTES || at < b->staged) {
		submit_and_wait(b);
		at = 0;
	}
	map = (uint8_t *)SDL_MapGPUTransferBuffer(b->device, b->staging, b->staged == 0);
	if (!map) return NULL;
	b->staged = at + n;
	*offset = at;
	return map + at;
}

/* Uploads rows into a texture's layer from (x, y): units rows of row_bytes each (stride bytes apart in src), a row
 * covering side pixel rows (1, or a block's side), h pixel rows in all, in stripes the staging buffer holds. */
static void upload_rows(KpGfxBackend *b, SDL_GPUTexture *tex, uint32_t layer, uint32_t x, uint32_t y, uint32_t w,
	uint32_t h, const uint8_t *src, size_t row_bytes, size_t stride, uint32_t units, uint32_t side)
{
	uint32_t per = row_bytes ? (uint32_t)(KP_GPU_STAGING_BYTES / row_bytes) : 0, done = 0;
	if (per == 0 || !tex) return;
	while (done < units) {
		uint32_t n = units - done < per ? units - done : per, offset = 0, k;
		uint8_t *dst = stage(b, (uint32_t)(n * row_bytes), &offset);
		SDL_GPUTextureTransferInfo from;
		SDL_GPUTextureRegion to;
		SDL_GPUCopyPass *copy;
		if (!dst) return;
		for (k = 0; k < n; k++) memcpy(dst + (size_t)k * row_bytes, src + (size_t)(done + k) * stride, row_bytes);
		SDL_UnmapGPUTransferBuffer(b->device, b->staging);
		if (!(copy = copy_pass(b))) return;
		memset(&from, 0, sizeof from);
		from.transfer_buffer = b->staging;
		from.offset = offset; /* the rows tight: pixels_per_row and rows_per_layer 0 */
		memset(&to, 0, sizeof to);
		to.texture = tex;
		to.layer = layer;
		to.x = x;
		to.y = y + done * side;
		to.w = w;
		to.h = (done + n) * side < h ? n * side : h - done * side;
		to.d = 1;
		SDL_UploadToGPUTexture(copy, &from, &to, false); /* the other texels and layers kept */
		done += n;
	}
}

static int gpu_texture(KpGfxBackend *b, int tex, int format, int32_t w, int32_t h, int32_t layers)
{
	Texture *t = &b->textures[tex];
	int array = layers > 0 || is_blocks(format), uint = is_uint(format);
	if (t->tex && t->format == format && t->w == w && t->h == h && t->layers == layers) return 0;
	gpu_drop(b, tex);
	/* the unsigned integer textures cannot be sampled in SDL_GPU: their stages read them as storage textures */
	t->tex = make_texture(b, array ? SDL_GPU_TEXTURETYPE_2D_ARRAY : SDL_GPU_TEXTURETYPE_2D, FORMATS[format].format,
		uint ? SDL_GPU_TEXTUREUSAGE_GRAPHICS_STORAGE_READ : SDL_GPU_TEXTUREUSAGE_SAMPLER, w, h, layers > 0 ? layers : 1);
	if (!t->tex) { /* out of memory, a size past the limits, a format SDL_GPU lacks */
		memset(t, 0, sizeof *t);
		if (b->failed_tex != tex || b->failed_w != w || b->failed_h != h || b->failed_layers != layers) {
			b->failures++;
			snprintf(b->failure, sizeof b->failure, "gfx: texture %d (%d x %d x %d, format %d) cannot be made", tex,
				(int)w, (int)h, (int)layers, format);
			b->failed_tex = tex;
			b->failed_w = w;
			b->failed_h = h;
			b->failed_layers = layers;
		}
		return -1;
	}
	t->format = format;
	t->w = w;
	t->h = h;
	t->layers = layers;
	t->kind = uint ? KP_GPU_KIND_UINT : array ? KP_GPU_KIND_ARRAY : KP_GPU_KIND_2D;
	/* the HD art is sampled between its texels (it is drawn at the window's scale, not its own); the water's normal
	 * maps repeat across the map */
	if (tex == KP_TEX_HD_NORMAL_A || tex == KP_TEX_HD_NORMAL_B) {
		t->sampler = SAMPLER_LINEAR_REPEAT;
	} else if (is_blocks(format) || tex == KP_TEX_HD_DIFFUSE || tex == KP_TEX_HD_TEAM || tex == KP_TEX_UI_ATLAS
		|| tex == KP_TEX_VIDEO_Y || tex == KP_TEX_VIDEO_CB || tex == KP_TEX_VIDEO_CR || tex == KP_TEX_VIDEO_CAPTION) {
		t->sampler = SAMPLER_LINEAR;
	} else {
		t->sampler = SAMPLER_NEAREST;
	}
	if (b->failed_tex == tex) b->failed_tex = -1; /* made again: its next failure is reported */
	return 0;
}

static int gpu_take_failures(KpGfxBackend *b, char *msg, size_t size)
{
	int32_t n = b->failures;
	if (n > 0 && size > 0) snprintf(msg, size, "%s", b->failure);
	b->failures = 0;
	return (int)n;
}

static void gpu_drop(KpGfxBackend *b, int tex)
{
	Texture *t = &b->textures[tex];
	release_texture(b, &t->tex); /* SDL keeps it until the command buffers using it are done */
	memset(t, 0, sizeof *t);
}

static int gpu_has_texture(const KpGfxBackend *b, int tex, int32_t *w, int32_t *h)
{
	const Texture *t = &b->textures[tex];
	if (w) *w = t->w;
	if (h) *h = t->h;
	return t->tex != NULL;
}

static void gpu_upload(KpGfxBackend *b, int tex, int32_t layer, int32_t x, int32_t y, int32_t w, int32_t h,
	const void *data, size_t stride)
{
	const Texture *t = &b->textures[tex];
	size_t row;
	if (!t->tex || is_blocks(t->format) || w <= 0 || h <= 0 || x < 0 || y < 0 || x + w > t->w || y + h > t->h
		|| layer < 0 || layer >= (t->layers > 0 ? t->layers : 1))
		return;
	row = (size_t)w * (size_t)FORMATS[t->format].bytes;
	upload_rows(b, t->tex, (uint32_t)layer, (uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h, (const uint8_t *)data,
		row, stride ? stride : row, (uint32_t)h, 1);
}

static void gpu_upload_blocks(KpGfxBackend *b, int tex, int32_t layer, int32_t w, int32_t h, const void *data,
	size_t size)
{
	const Texture *t = &b->textures[tex];
	int side, bytes;
	size_t row;
	if (!t->tex || !is_blocks(t->format) || t->layers <= 0 || layer < 0 || layer >= t->layers || w <= 0 || h <= 0
		|| w > t->w || h > t->h || size != blocks_size(t->format, w, h))
		return;
	block_shape(t->format, &side, &bytes);
	row = (size_t)((w + side - 1) / side) * (size_t)bytes;
	upload_rows(b, t->tex, (uint32_t)layer, 0, 0, (uint32_t)w, (uint32_t)h, (const uint8_t *)data, row, row,
		(uint32_t)((h + side - 1) / side), (uint32_t)side);
}

static int gpu_takes_blocks(KpGfxBackend *b, int format)
{
	if (!is_blocks(format)) return 0;
	if (!b->takes[format]) {
		b->takes[format] = FORMATS[format].format != SDL_GPU_TEXTUREFORMAT_INVALID
			&& SDL_GPUTextureSupportsFormat(b->device, FORMATS[format].format, SDL_GPU_TEXTURETYPE_2D_ARRAY,
				SDL_GPU_TEXTUREUSAGE_SAMPLER) ? 1 : -1;
	}
	return b->takes[format] > 0;
}

static void free_targets(KpGfxBackend *b)
{
	int k;
	for (k = 0; k < KP_TARGETS; k++) {
		forget_target(b, k);
		release_target(b, &b->targets[k]);
	}
	b->hd_failed_w = b->hd_failed_h = 0; /* the memory the targets held is back: one more try */
}

static int gpu_targets(KpGfxBackend *b, int32_t w, int32_t h)
{
	const Target *index = &b->targets[KP_TARGET_INDEX];
	int rc = 0;
	if (w < 1) w = 1;
	if (h < 1) h = 1;
	if (index->color && index->w == w && index->h == h) return 0;
	free_targets(b);
	rc |= make_target(b, &b->targets[KP_TARGET_INDEX], w, h, SDL_GPU_TEXTUREFORMAT_R8_UNORM, 1);
	rc |= make_target(b, &b->targets[KP_TARGET_DEST], w, h, SDL_GPU_TEXTUREFORMAT_R8_UNORM, 0);
	rc |= make_target(b, &b->targets[KP_TARGET_RGB], w, h, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, 0);
	if (rc != 0) free_targets(b); /* not kept: the next call tries again */
	return rc;
}

static int gpu_hd_target(KpGfxBackend *b, int32_t w, int32_t h)
{
	Target *t = &b->targets[KP_TARGET_HD];
	if (w < 1) w = 1;
	if (h < 1) h = 1;
	if (t->color && w == t->w && h == t->h) return 0;
	if (w == b->hd_failed_w && h == b->hd_failed_h) return -1; /* not made again every frame */
	forget_target(b, KP_TARGET_HD);
	release_target(b, t);
	if (make_target(b, t, w, h, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, 1) != 0) {
		b->hd_failed_w = w;
		b->hd_failed_h = h;
		return -1;
	}
	b->hd_failed_w = b->hd_failed_h = 0;
	return 0;
}

/* The window's offscreen texture at the window's size in pixels, checked at its first binding after a present. */
static void size_window(KpGfxBackend *b)
{
	int w = 0, h = 0;
	if (b->screen_sized) return;
	b->screen_sized = 1;
	if (!SDL_GetWindowSizeInPixels(b->window, &w, &h) || w < 1 || h < 1) return; /* minimized: kept */
	if (b->screen.color && b->screen.w == w && b->screen.h == h) return;
	forget_target(b, KP_TARGET_WINDOW);
	release_target(b, &b->screen);
	if (make_target(b, &b->screen, w, h, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, 0) != 0) {
		SDL_LogError(SDL_LOG_CATEGORY_RENDER, "gfx: the window's %d x %d texture cannot be made: %s", w, h,
			SDL_GetError());
	}
}

/* --- drawing --- */

/* GL's viewport (its numbers: the memory rows are GL's) cut to the target, as GL's clipping cuts it, and the vertex
 * stages' mapping into the cut one (xy scale, zw offset: exactly 1 and 0 when nothing is cut). */
static void set_viewport(KpGfxBackend *b, const Target *t, int32_t vx, int32_t vy, int32_t vw, int32_t vh)
{
	int32_t x0 = vx > 0 ? vx : 0, y0 = vy > 0 ? vy : 0;
	int32_t x1 = vx + vw < t->w ? vx + vw : t->w, y1 = vy + vh < t->h ? vy + vh : t->h;
	float *m = b->vertex_u[KP_UNIFORMS];
	b->viewport_empty = vw <= 0 || vh <= 0 || x1 <= x0 || y1 <= y0;
	if (b->viewport_empty) return;
	b->viewport.x = (float)x0;
	b->viewport.y = (float)y0;
	b->viewport.w = (float)(x1 - x0);
	b->viewport.h = (float)(y1 - y0);
	b->viewport.min_depth = 0.0f;
	b->viewport.max_depth = 1.0f;
	m[0] = (float)((double)vw / (double)(x1 - x0));
	m[1] = (float)((double)vh / (double)(y1 - y0));
	m[2] = (float)((2.0 * (double)(vx - x0) + (double)vw) / (double)(x1 - x0) - 1.0);
	m[3] = (float)((2.0 * (double)(vy - y0) + (double)vh) / (double)(y1 - y0) - 1.0);
}

static void gpu_bind(KpGfxBackend *b, int target, int32_t vx, int32_t vy, int32_t vw, int32_t vh)
{
	if (target == KP_TARGET_WINDOW) size_window(b);
	if (b->pass && b->pass_target != target) end_render_pass(b);
	if (b->clear.on && b->clear.target != target) flush_clear(b);
	b->bound = target;
	set_viewport(b, target_of(b, target), vx, vy, vw, vh);
	if (b->pass && !b->viewport_empty) SDL_SetGPUViewport(b->pass, &b->viewport);
}

static void gpu_clear(KpGfxBackend *b, float r, float gg, float bb, float a, int depth)
{
	if (b->bound == NO_TARGET) return;
	end_render_pass(b); /* GL's clear is the whole target's, the draws before it overwritten */
	if (b->clear.on && b->clear.target == b->bound) depth |= b->clear.depth;
	b->clear.on = 1;
	b->clear.target = b->bound;
	b->clear.depth = depth;
	b->clear.color.r = r;
	b->clear.color.g = gg;
	b->clear.color.b = bb;
	b->clear.color.a = a;
}

static void gpu_uniform(KpGfxBackend *b, int prog, int u, float x, float y, float z, float w)
{
	b->u[prog][u][0] = x;
	b->u[prog][u][1] = y;
	b->u[prog][u][2] = z;
	b->u[prog][u][3] = w;
}

/* The texture a draw binds to a sampler (KP_TEX_*, KP_SAMPLE_TARGET + KP_TARGET_*), -1 none. */
static int bound_texture(const KpGfxBinding *bind, int count, int sampler)
{
	int k;
	for (k = 0; k < count; k++) {
		if (bind[k].sampler == sampler) return bind[k].texture;
	}
	return -1;
}

/* What a slot of a kind reads for a binding: the texture, a target's colour, else the dummy of its kind (a texture
 * not made, not bound, or of another kind: GL's zero texel). */
static void slot_texture(const KpGfxBackend *b, int tex, int kind, SDL_GPUTexture **texture, SDL_GPUSampler **sampler)
{
	*texture = b->dummies[kind];
	*sampler = b->samplers[SAMPLER_NEAREST];
	if (tex >= KP_SAMPLE_TARGET) {
		int target = tex - KP_SAMPLE_TARGET;
		if (kind == KP_GPU_KIND_2D && b->targets[target].color) {
			*texture = b->targets[target].color;
			*sampler = b->samplers[target == KP_TARGET_RGB || target == KP_TARGET_HD ? SAMPLER_LINEAR : SAMPLER_NEAREST];
		}
	} else if (tex >= 0 && b->textures[tex].tex && b->textures[tex].kind == kind) {
		*texture = b->textures[tex].tex;
		*sampler = b->samplers[b->textures[tex].sampler];
	}
}

/* Binds every slot a stage declares (SDL_GPU checks each), and pushes its constants. */
static void bind_stage(KpGfxBackend *b, int prog, const KpGfxGpuStage *st, int vertex, const KpGfxBinding *bind,
	int count)
{
	SDL_GPUTextureSamplerBinding s[KP_GPU_STAGE_SAMPLERS];
	SDL_GPUTexture *storage[KP_GPU_STAGE_STORAGE];
	SDL_GPUSampler *unused;
	int k;
	for (k = 0; k < st->samplers; k++) {
		slot_texture(b, bound_texture(bind, count, st->sampler[k]), st->kind[k], &s[k].texture, &s[k].sampler);
	}
	for (k = 0; k < st->storage; k++) {
		slot_texture(b, bound_texture(bind, count, st->storage_of[k]), KP_GPU_KIND_UINT, &storage[k], &unused);
	}
	if (vertex) {
		if (st->samplers) SDL_BindGPUVertexSamplers(b->pass, 0, s, st->samplers);
		if (st->storage) SDL_BindGPUVertexStorageTextures(b->pass, 0, storage, st->storage);
		if (st->uniforms) { /* the program's constants, then the viewport's mapping */
			memcpy(b->vertex_u, b->u[prog], sizeof b->u[prog]);
			SDL_PushGPUVertexUniformData(b->cmd, 0, b->vertex_u, (Uint32)sizeof b->vertex_u);
		}
	} else {
		if (st->samplers) SDL_BindGPUFragmentSamplers(b->pass, 0, s, st->samplers);
		if (st->storage) SDL_BindGPUFragmentStorageTextures(b->pass, 0, storage, st->storage);
		if (st->uniforms) SDL_PushGPUFragmentUniformData(b->cmd, 0, b->u[prog], (Uint32)sizeof b->u[prog]);
	}
}

static void gpu_draw(KpGfxBackend *b, int prog, int32_t vertices, const KpGfxBinding *bind, int count, int depth,
	int write)
{
	int p;
	if (vertices <= 0 || b->bound == NO_TARGET || b->viewport_empty) return;
	p = kp_gfx_gpu_pipeline(prog, kp_gfx_gpu_target_kind(b->bound), b->blend, kp_gfx_gpu_depth(depth, write));
	if (p < 0) { /* test_gfx keeps the table whole: a draw state the passes gained */
		if (!b->unpiped) {
			SDL_LogError(SDL_LOG_CATEGORY_RENDER, "gfx: no SDL_GPU pipeline for program %d (target %d, blend %d, depth %d,"
				" write %d)", prog, b->bound, b->blend, depth, write);
		}
		b->unpiped = 1;
		return;
	}
	if (begin_pass(b, 1) != 0) return;
	if (b->pass_pipeline != p) {
		SDL_BindGPUGraphicsPipeline(b->pass, b->pipelines[p]);
		b->pass_pipeline = p;
	}
	bind_stage(b, prog, &KP_GFX_GPU_SHADERS[KP_GFX_GPU_PROGRAMS[prog].vs].stage, 1, bind, count);
	bind_stage(b, prog, &KP_GFX_GPU_SHADERS[KP_GFX_GPU_PROGRAMS[prog].fs].stage, 0, bind, count);
	SDL_DrawGPUPrimitives(b->pass, (Uint32)vertices, 1, 0, 0); /* no vertex buffer: the stages read gl_VertexIndex */
}

static void gpu_blend(KpGfxBackend *b, int on) { b->blend = on ? KP_GPU_BLEND_ALPHA : KP_GPU_BLEND_OFF; }

static void gpu_blend_premultiplied(KpGfxBackend *b) { b->blend = KP_GPU_BLEND_PREMULTIPLIED; }

static void gpu_copy(KpGfxBackend *b, int from, int to)
{
	const Target *s = &b->targets[from], *d = &b->targets[to];
	SDL_GPUTextureLocation src, dst;
	SDL_GPUCopyPass *copy;
	if (!s->color || !d->color || s->w != d->w || s->h != d->h) return;
	flush_clear(b);
	if (!(copy = copy_pass(b))) return;
	memset(&src, 0, sizeof src);
	src.texture = s->color;
	memset(&dst, 0, sizeof dst);
	dst.texture = d->color;
	SDL_CopyGPUTextureToTexture(copy, &src, &dst, (Uint32)s->w, (Uint32)s->h, 1, false);
}

static int gpu_read(KpGfxBackend *b, int target, int32_t x, int32_t y, int32_t w, int32_t h, uint8_t *out)
{
	const Target *t = target_of(b, target);
	int bytes = target == KP_TARGET_INDEX || target == KP_TARGET_DEST ? 1 : 4;
	SDL_GPUTransferBufferCreateInfo info;
	SDL_GPUTransferBuffer *tb;
	SDL_GPUTextureRegion from;
	SDL_GPUTextureTransferInfo to;
	SDL_GPUCopyPass *copy;
	const uint8_t *map;
	size_t k, n = (size_t)w * (size_t)h;
	if (!t->color || w <= 0 || h <= 0 || x < 0 || y < 0 || x + w > t->w || y + h > t->h) return -1;
	flush_clear(b);
	memset(&info, 0, sizeof info);
	info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
	info.size = (Uint32)(n * (size_t)bytes);
	if (!(tb = SDL_CreateGPUTransferBuffer(b->device, &info))) return -1;
	if (!(copy = copy_pass(b))) {
		SDL_ReleaseGPUTransferBuffer(b->device, tb);
		return -1;
	}
	memset(&from, 0, sizeof from);
	from.texture = t->color;
	from.x = (Uint32)x;
	from.y = (Uint32)y; /* the memory rows are GL's: bottom-up */
	from.w = (Uint32)w;
	from.h = (Uint32)h;
	from.d = 1;
	memset(&to, 0, sizeof to);
	to.transfer_buffer = tb;
	SDL_DownloadFromGPUTexture(copy, &from, &to);
	submit_and_wait(b);
	map = (const uint8_t *)SDL_MapGPUTransferBuffer(b->device, tb, false);
	if (!map) {
		SDL_ReleaseGPUTransferBuffer(b->device, tb);
		return -1;
	}
	if (bytes == 4) {
		memcpy(out, map, n * 4);
	} else { /* an index target as glReadPixels gives it: (index, 0, 0, 255) */
		for (k = 0; k < n; k++) {
			out[4 * k] = map[k];
			out[4 * k + 1] = out[4 * k + 2] = 0;
			out[4 * k + 3] = 255;
		}
	}
	SDL_UnmapGPUTransferBuffer(b->device, tb);
	SDL_ReleaseGPUTransferBuffer(b->device, tb);
	return 0;
}

static int gpu_alive(const KpGfxBackend *b)
{
	(void)b;
	return 1; /* SDL_GPU reports no lost device on the desktops */
}

/* --- presenting --- */

static int gpu_present(KpGfxBackend *b, int wait)
{
	SDL_GPUTexture *swap = NULL;
	Uint32 sw = 0, sh = 0;
	int shown = 0, tried = 0;
	flush_clear(b);
	end_passes(b);
	b->screen_sized = 0;
	if (!command(b)) return 0;
	if (b->screen.color && !(SDL_GetWindowFlags(b->window) & (SDL_WINDOW_HIDDEN | SDL_WINDOW_MINIMIZED))) {
		/* not waiting: a frame not ready is not shown (macOS's vsync with the display asleep would block) */
		tried = 1;
		bool ok = wait ? SDL_WaitAndAcquireGPUSwapchainTexture(b->cmd, b->window, &swap, &sw, &sh)
			: SDL_AcquireGPUSwapchainTexture(b->cmd, b->window, &swap, &sw, &sh);
		if (ok && swap) {
			SDL_GPUBlitInfo blit;
			memset(&blit, 0, sizeof blit);
			blit.source.texture = b->screen.color;
			blit.source.w = (Uint32)b->screen.w;
			blit.source.h = (Uint32)b->screen.h;
			blit.destination.texture = swap;
			blit.destination.w = sw;
			blit.destination.h = sh;
			blit.load_op = SDL_GPU_LOADOP_DONT_CARE;
			blit.flip_mode = SDL_FLIP_VERTICAL; /* GL's rows bottom-up, the swapchain's top-down */
			blit.filter = SDL_GPU_FILTER_NEAREST;
			SDL_BlitGPUTexture(b->cmd, &blit);
			shown = 1;
		}
	}
	if (shown) {
		submit(b);
	} else { /* no swapchain paces a hidden window's frames: one in flight at most */
		SDL_GPUFence *f = submit_fenced(b);
		wait_fence(b, &b->frame_fence);
		b->frame_fence = f;
	}
	return shown || !tried; /* a hidden window's frame has nothing to be shown in, as GL's swap of it */
}

static int gpu_vsync(KpGfxBackend *b, int on)
{
	SDL_GPUPresentMode mode = SDL_GPU_PRESENTMODE_VSYNC;
	if (!on) { /* tearing, else the newest frame at the refresh, else the refresh's */
		if (SDL_WindowSupportsGPUPresentMode(b->device, b->window, SDL_GPU_PRESENTMODE_IMMEDIATE)) {
			mode = SDL_GPU_PRESENTMODE_IMMEDIATE;
		} else if (SDL_WindowSupportsGPUPresentMode(b->device, b->window, SDL_GPU_PRESENTMODE_MAILBOX)) {
			mode = SDL_GPU_PRESENTMODE_MAILBOX;
		}
	}
	if (!SDL_SetGPUSwapchainParameters(b->device, b->window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, mode)) return -1;
	return on || mode != SDL_GPU_PRESENTMODE_VSYNC ? 0 : -1;
}

static void gpu_finish(KpGfxBackend *b)
{
	flush_clear(b);
	submit(b);
	SDL_WaitForGPUIdle(b->device);
}

static const char *gpu_version(const KpGfxBackend *b) { return b->version; }

/* --- making and freeing --- */

static int fail(char *err, size_t err_size, const char *what)
{
	snprintf(err, err_size, "gfx: %s: %s", what, SDL_GetError());
	return -1;
}

/* The formats the backend needs beyond the sampled ones every device takes: the unsigned integer textures read as
 * storage textures, a depth format. */
static int check_formats(KpGfxBackend *b, char *err, size_t err_size)
{
	static const SDL_GPUTextureFormat UINTS[3] = { SDL_GPU_TEXTUREFORMAT_R16_UINT, SDL_GPU_TEXTUREFORMAT_R16G16_UINT,
		SDL_GPU_TEXTUREFORMAT_R16G16B16A16_UINT };
	int k;
	for (k = 0; k < 3; k++) {
		if (!SDL_GPUTextureSupportsFormat(b->device, UINTS[k], SDL_GPU_TEXTURETYPE_2D,
				SDL_GPU_TEXTUREUSAGE_GRAPHICS_STORAGE_READ)) {
			snprintf(err, err_size, "gfx: the SDL_GPU device reads no unsigned integer format %d as a storage texture",
				(int)UINTS[k]);
			return -1;
		}
	}
	/* the ranks (rank + 1) / 8192 are exact in both (D24 is not on Apple's GPUs) */
	if (SDL_GPUTextureSupportsFormat(b->device, SDL_GPU_TEXTUREFORMAT_D32_FLOAT, SDL_GPU_TEXTURETYPE_2D,
			SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET)) {
		b->depth_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
	} else if (SDL_GPUTextureSupportsFormat(b->device, SDL_GPU_TEXTUREFORMAT_D24_UNORM, SDL_GPU_TEXTURETYPE_2D,
			SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET)) {
		b->depth_format = SDL_GPU_TEXTUREFORMAT_D24_UNORM;
	} else {
		snprintf(err, err_size, "gfx: the SDL_GPU device has no depth format");
		return -1;
	}
	return 0;
}

static int make_shaders(KpGfxBackend *b, char *err, size_t err_size)
{
	SDL_GPUShaderFormat formats = SDL_GetGPUShaderFormats(b->device);
	int msl = (formats & SDL_GPU_SHADERFORMAT_MSL) != 0, vertex[KP_GPU_STAGES_MAX] = { 0 }, k;
	if (!msl && !(formats & SDL_GPU_SHADERFORMAT_SPIRV)) {
		snprintf(err, err_size, "gfx: the SDL_GPU device takes neither MSL nor SPIR-V");
		return -1;
	}
	for (k = 0; k < KP_PROGRAMS; k++) vertex[KP_GFX_GPU_PROGRAMS[k].vs] = 1;
	for (k = 0; k < KP_GFX_GPU_SHADER_COUNT; k++) {
		const KpGfxGpuShader *s = &KP_GFX_GPU_SHADERS[k];
		SDL_GPUShaderCreateInfo info;
		memset(&info, 0, sizeof info);
		/* TODO: a metallib built offline (Xcode's Metal toolchain) if the first start's compile, about 5 s on a
		 * machine, or the index world's exactness under Metal's fast math needs it (ADR 0028) */
		if (msl) { /* compiled here: Metal's defaults (ADR 0028) */
			info.code = (const Uint8 *)s->msl;
			info.code_size = s->msl_size;
			info.entrypoint = "main0";
			info.format = SDL_GPU_SHADERFORMAT_MSL;
		} else {
			info.code = (const Uint8 *)s->spirv;
			info.code_size = s->spirv_size;
			info.entrypoint = "main";
			info.format = SDL_GPU_SHADERFORMAT_SPIRV;
		}
		info.stage = vertex[k] ? SDL_GPU_SHADERSTAGE_VERTEX : SDL_GPU_SHADERSTAGE_FRAGMENT;
		info.num_samplers = s->stage.samplers;
		info.num_storage_textures = s->stage.storage;
		info.num_uniform_buffers = s->stage.uniforms;
		if (!(b->shaders[k] = SDL_CreateGPUShader(b->device, &info))) {
			char what[64];
			snprintf(what, sizeof what, "SDL_GPU stage %d does not compile", k);
			return fail(err, err_size, what);
		}
	}
	return 0;
}

static int make_pipelines(KpGfxBackend *b, char *err, size_t err_size)
{
	int k;
	for (k = 0; k < KP_GFX_GPU_PIPELINE_COUNT; k++) {
		const KpGfxGpuPipeline *p = &KP_GFX_GPU_PIPELINES[k];
		SDL_GPUGraphicsPipelineCreateInfo info;
		SDL_GPUColorTargetDescription color;
		memset(&color, 0, sizeof color);
		color.format = p->target == KP_GPU_TARGET_INDEX ? SDL_GPU_TEXTUREFORMAT_R8_UNORM
			: SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
		if (p->blend != KP_GPU_BLEND_OFF) { /* GL's: the colour by coverage, the target's alpha kept */
			color.blend_state.enable_blend = true;
			color.blend_state.src_color_blendfactor = p->blend == KP_GPU_BLEND_ALPHA ? SDL_GPU_BLENDFACTOR_SRC_ALPHA
				: SDL_GPU_BLENDFACTOR_ONE;
			color.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
			color.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
			color.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
			color.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
			color.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
		}
		memset(&info, 0, sizeof info);
		info.vertex_shader = b->shaders[KP_GFX_GPU_PROGRAMS[p->prog].vs];
		info.fragment_shader = b->shaders[KP_GFX_GPU_PROGRAMS[p->prog].fs];
		info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
		info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
		info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
		info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
		info.rasterizer_state.enable_depth_clip = true;
		info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
		info.depth_stencil_state.enable_depth_test = p->depth != KP_GPU_DEPTH_OFF;
		info.depth_stencil_state.enable_depth_write = p->depth == KP_GPU_DEPTH_WRITE;
		info.depth_stencil_state.compare_op = p->depth != KP_GPU_DEPTH_OFF ? SDL_GPU_COMPAREOP_GREATER
			: SDL_GPU_COMPAREOP_ALWAYS;
		info.target_info.color_target_descriptions = &color;
		info.target_info.num_color_targets = 1;
		info.target_info.has_depth_stencil_target = p->target != KP_GPU_TARGET_RGBA;
		info.target_info.depth_stencil_format = b->depth_format;
		if (!(b->pipelines[k] = SDL_CreateGPUGraphicsPipeline(b->device, &info))) {
			char what[64];
			snprintf(what, sizeof what, "SDL_GPU pipeline %d (program %d) cannot be made", k, p->prog);
			return fail(err, err_size, what);
		}
	}
	return 0;
}

static int make_samplers(KpGfxBackend *b, char *err, size_t err_size)
{
	static const SDL_GPUFilter FILTER[SAMPLERS] = { SDL_GPU_FILTER_NEAREST, SDL_GPU_FILTER_LINEAR,
		SDL_GPU_FILTER_LINEAR };
	static const SDL_GPUSamplerAddressMode WRAP[SAMPLERS] = { SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
		SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE, SDL_GPU_SAMPLERADDRESSMODE_REPEAT };
	int k;
	for (k = 0; k < SAMPLERS; k++) {
		SDL_GPUSamplerCreateInfo info;
		memset(&info, 0, sizeof info);
		info.min_filter = info.mag_filter = FILTER[k];
		info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST; /* one level, as GL's textures */
		info.address_mode_u = info.address_mode_v = info.address_mode_w = WRAP[k];
		if (!(b->samplers[k] = SDL_CreateGPUSampler(b->device, &info))) return fail(err, err_size, "a sampler");
	}
	return 0;
}

/* The staging buffer and the dummies, their zero texels up. */
static int make_dummies(KpGfxBackend *b, char *err, size_t err_size)
{
	static const uint8_t zero[8] = { 0 };
	SDL_GPUTransferBufferCreateInfo info;
	memset(&info, 0, sizeof info);
	info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
	info.size = KP_GPU_STAGING_BYTES;
	if (!(b->staging = SDL_CreateGPUTransferBuffer(b->device, &info))) return fail(err, err_size, "the staging buffer");
	b->dummies[KP_GPU_KIND_2D] = make_texture(b, SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
		SDL_GPU_TEXTUREUSAGE_SAMPLER, 1, 1, 1);
	b->dummies[KP_GPU_KIND_ARRAY] = make_texture(b, SDL_GPU_TEXTURETYPE_2D_ARRAY, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
		SDL_GPU_TEXTUREUSAGE_SAMPLER, 1, 1, 1);
	b->dummies[KP_GPU_KIND_UINT] = make_texture(b, SDL_GPU_TEXTURETYPE_2D, SDL_GPU_TEXTUREFORMAT_R16G16B16A16_UINT,
		SDL_GPU_TEXTUREUSAGE_GRAPHICS_STORAGE_READ, 1, 1, 1);
	if (!b->dummies[KP_GPU_KIND_2D] || !b->dummies[KP_GPU_KIND_ARRAY] || !b->dummies[KP_GPU_KIND_UINT])
		return fail(err, err_size, "the dummy textures");
	upload_rows(b, b->dummies[KP_GPU_KIND_2D], 0, 0, 0, 1, 1, zero, 4, 4, 1, 1);
	upload_rows(b, b->dummies[KP_GPU_KIND_ARRAY], 0, 0, 0, 1, 1, zero, 4, 4, 1, 1);
	upload_rows(b, b->dummies[KP_GPU_KIND_UINT], 0, 0, 0, 1, 1, zero, 8, 8, 1, 1);
	submit(b);
	return 0;
}

static void describe(KpGfxBackend *b)
{
	SDL_PropertiesID props = SDL_GetGPUDeviceProperties(b->device);
	const char *name = props ? SDL_GetStringProperty(props, SDL_PROP_GPU_DEVICE_NAME_STRING, NULL) : NULL;
	const char *driver = props ? SDL_GetStringProperty(props, SDL_PROP_GPU_DEVICE_DRIVER_INFO_STRING, NULL) : NULL;
	if (!driver && props) driver = SDL_GetStringProperty(props, SDL_PROP_GPU_DEVICE_DRIVER_VERSION_STRING, NULL);
	snprintf(b->version, sizeof b->version, "SDL_GPU %s, %s%s%s", SDL_GetGPUDeviceDriver(b->device),
		name ? name : "?", driver ? ", " : "", driver ? driver : "");
}

static KpGfxBackend *gpu_new(const KpGfxApi *api, char *err, size_t err_size)
{
	KpGfxBackend *b;
	if (!api->gpu_device || !api->window) {
		snprintf(err, err_size, "gfx: SDL_GPU needs a device and the window it claimed");
		return NULL;
	}
	if (KP_GFX_GPU_SHADER_COUNT > KP_GPU_STAGES_MAX || KP_GFX_GPU_PIPELINE_COUNT > KP_GPU_PIPELINES_MAX) {
		snprintf(err, err_size, "gfx: %d stages and %d pipelines past the backend's tables", KP_GFX_GPU_SHADER_COUNT,
			KP_GFX_GPU_PIPELINE_COUNT);
		return NULL;
	}
	b = (KpGfxBackend *)calloc(1, sizeof *b);
	if (!b) {
		snprintf(err, err_size, "gfx: out of memory");
		return NULL;
	}
	b->ops = &KP_GFX_GPU_OPS;
	b->api = *api;
	b->device = (SDL_GPUDevice *)api->gpu_device;
	b->window = (SDL_Window *)api->window;
	b->failed_tex = -1;
	b->bound = NO_TARGET;
	b->pass_pipeline = -1;
	/* every failure before the first frame: the application falls back to GL */
	if (check_formats(b, err, err_size) != 0 || make_shaders(b, err, err_size) != 0
		|| make_pipelines(b, err, err_size) != 0 || make_samplers(b, err, err_size) != 0
		|| make_dummies(b, err, err_size) != 0) {
		gpu_free(b, 0);
		return NULL;
	}
	describe(b);
	return b;
}

/* Every texture and target released (the dummies kept). */
static void release_resources(KpGfxBackend *b)
{
	int k;
	end_passes(b);
	b->clear.on = 0;
	b->bound = NO_TARGET;
	for (k = 0; k < KP_TEXTURES; k++) gpu_drop(b, k);
	free_targets(b);
	release_target(b, &b->screen);
	b->screen_sized = 0;
}

static void gpu_free(KpGfxBackend *b, int lost)
{
	int k;
	(void)lost; /* SDL_GPU loses no device on the desktops */
	if (!b) return;
	flush_clear(b);
	submit(b); /* SDL releases the objects once the command buffers using them are done */
	wait_fence(b, &b->frame_fence);
	release_resources(b);
	for (k = 0; k < KP_GPU_KINDS; k++) release_texture(b, &b->dummies[k]);
	if (b->staging) SDL_ReleaseGPUTransferBuffer(b->device, b->staging);
	for (k = 0; k < SAMPLERS; k++) {
		if (b->samplers[k]) SDL_ReleaseGPUSampler(b->device, b->samplers[k]);
	}
	for (k = 0; k < KP_GPU_PIPELINES_MAX; k++) {
		if (b->pipelines[k]) SDL_ReleaseGPUGraphicsPipeline(b->device, b->pipelines[k]);
	}
	for (k = 0; k < KP_GPU_STAGES_MAX; k++) {
		if (b->shaders[k]) SDL_ReleaseGPUShader(b->device, b->shaders[k]);
	}
	free(b);
}

/* The textures and targets released; the shaders, pipelines, samplers and dummies kept (no MSL compiled again when
 * the art is reloaded). */
static KpGfxBackend *gpu_reset(KpGfxBackend *b, char *err, size_t err_size)
{
	(void)err, (void)err_size;
	flush_clear(b);
	submit(b);
	release_resources(b);
	memset(b->u, 0, sizeof b->u);
	b->blend = KP_GPU_BLEND_OFF;
	b->hd_failed_w = b->hd_failed_h = 0;
	b->failed_tex = -1;
	b->failures = 0;
	return b;
}

const KpGfxBackendOps KP_GFX_GPU_OPS = { gpu_new, gpu_free, gpu_reset, gpu_texture, gpu_drop, gpu_take_failures,
	gpu_has_texture, gpu_upload, gpu_targets, gpu_bind, gpu_clear, gpu_uniform, gpu_draw, gpu_blend,
	gpu_blend_premultiplied, gpu_takes_blocks, gpu_upload_blocks, gpu_hd_target, gpu_copy, gpu_read, gpu_alive,
	gpu_present, gpu_vsync, gpu_finish, gpu_version };
