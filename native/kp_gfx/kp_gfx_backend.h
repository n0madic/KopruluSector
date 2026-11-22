/*
 * The product's backends behind kp_gfx_internal.h's operations (ADR 0028): kp_gfx_backend.c dispatches each kp_gb_*
 * call through the table at the start of the backend's struct, the GL backend's (kp_gfx_gl.c) or SDL_GPU's
 * (kp_gfx_gpu.c). The C suites link a backend of their own instead and see none of this.
 */
#ifndef KP_GFX_BACKEND_H
#define KP_GFX_BACKEND_H

#include "kp_gfx_internal.h"

typedef struct KpGfxBackendOps {
	KpGfxBackend *(*create)(const KpGfxApi *api, char *err, size_t err_size);
	void (*free)(KpGfxBackend *b, int lost);
	KpGfxBackend *(*reset)(KpGfxBackend *b, char *err, size_t err_size);
	int (*texture)(KpGfxBackend *b, int tex, int format, int32_t w, int32_t h, int32_t layers);
	void (*drop)(KpGfxBackend *b, int tex);
	int (*take_failures)(KpGfxBackend *b, char *msg, size_t size);
	int (*has_texture)(const KpGfxBackend *b, int tex, int32_t *w, int32_t *h);
	void (*upload)(KpGfxBackend *b, int tex, int32_t layer, int32_t x, int32_t y, int32_t w, int32_t h,
		const void *data, size_t stride);
	int (*targets)(KpGfxBackend *b, int32_t w, int32_t h);
	void (*bind)(KpGfxBackend *b, int target, int32_t vx, int32_t vy, int32_t vw, int32_t vh);
	void (*clear)(KpGfxBackend *b, float r, float gg, float bb, float a, int depth);
	void (*uniform)(KpGfxBackend *b, int prog, int u, float x, float y, float z, float w);
	void (*draw)(KpGfxBackend *b, int prog, int32_t vertices, const KpGfxBinding *bind, int count, int depth,
		int write);
	void (*blend)(KpGfxBackend *b, int on);
	void (*blend_premultiplied)(KpGfxBackend *b);
	int (*takes_blocks)(KpGfxBackend *b, int format);
	void (*upload_blocks)(KpGfxBackend *b, int tex, int32_t layer, int32_t w, int32_t h, const void *data,
		size_t size);
	int (*hd_target)(KpGfxBackend *b, int32_t w, int32_t h);
	void (*copy)(KpGfxBackend *b, int from, int to);
	int (*read)(KpGfxBackend *b, int target, int32_t x, int32_t y, int32_t w, int32_t h, uint8_t *out);
	int (*alive)(const KpGfxBackend *b);
	int (*present)(KpGfxBackend *b, int wait);
	int (*vsync)(KpGfxBackend *b, int on);
	void (*finish)(KpGfxBackend *b);
	const char *(*version)(const KpGfxBackend *b);
} KpGfxBackendOps;

extern const KpGfxBackendOps KP_GFX_GL_OPS;
#ifdef KP_GFX_GPU
extern const KpGfxBackendOps KP_GFX_GPU_OPS;
#endif

#endif
