/*
 * kp_gfx_internal.h's operations dispatched to the backend KpGfxApi.backend names (kp_gfx_backend.h): every backend's
 * struct starts with its table.
 */
#include "kp_gfx_backend.h"

#include <stdio.h>

static const KpGfxBackendOps *ops(const KpGfxBackend *b) { return *(const KpGfxBackendOps *const *)(const void *)b; }

KpGfxBackend *kp_gb_new(const KpGfxApi *api, char *err, size_t err_size)
{
	if (api->backend == KP_GFX_BACKEND_GPU) {
#ifdef KP_GFX_GPU
		return KP_GFX_GPU_OPS.create(api, err, err_size);
#else
		snprintf(err, err_size, "gfx: this build has no SDL_GPU backend");
		return NULL;
#endif
	}
	return KP_GFX_GL_OPS.create(api, err, err_size);
}

void kp_gb_free(KpGfxBackend *b, int lost)
{
	if (b) ops(b)->free(b, lost);
}

KpGfxBackend *kp_gb_reset(KpGfxBackend *b, char *err, size_t err_size) { return ops(b)->reset(b, err, err_size); }

int kp_gb_texture(KpGfxBackend *b, int tex, int format, int32_t w, int32_t h, int32_t layers)
{
	return ops(b)->texture(b, tex, format, w, h, layers);
}

void kp_gb_drop(KpGfxBackend *b, int tex) { ops(b)->drop(b, tex); }

int kp_gb_take_failures(KpGfxBackend *b, char *msg, size_t size) { return ops(b)->take_failures(b, msg, size); }

int kp_gb_has_texture(const KpGfxBackend *b, int tex, int32_t *w, int32_t *h)
{
	return ops(b)->has_texture(b, tex, w, h);
}

void kp_gb_upload(KpGfxBackend *b, int tex, int32_t layer, int32_t x, int32_t y, int32_t w, int32_t h, const void *data,
	size_t stride)
{
	ops(b)->upload(b, tex, layer, x, y, w, h, data, stride);
}

int kp_gb_targets(KpGfxBackend *b, int32_t w, int32_t h) { return ops(b)->targets(b, w, h); }

void kp_gb_bind(KpGfxBackend *b, int target, int32_t vx, int32_t vy, int32_t vw, int32_t vh)
{
	ops(b)->bind(b, target, vx, vy, vw, vh);
}

void kp_gb_clear(KpGfxBackend *b, float r, float gg, float bb, float a, int depth)
{
	ops(b)->clear(b, r, gg, bb, a, depth);
}

void kp_gb_uniform(KpGfxBackend *b, int prog, int u, float x, float y, float z, float w)
{
	ops(b)->uniform(b, prog, u, x, y, z, w);
}

void kp_gb_draw(KpGfxBackend *b, int prog, int32_t vertices, const KpGfxBinding *bind, int count, int depth, int write)
{
	ops(b)->draw(b, prog, vertices, bind, count, depth, write);
}

void kp_gb_blend(KpGfxBackend *b, int on) { ops(b)->blend(b, on); }

void kp_gb_blend_premultiplied(KpGfxBackend *b) { ops(b)->blend_premultiplied(b); }

int kp_gb_takes_blocks(KpGfxBackend *b, int format) { return ops(b)->takes_blocks(b, format); }

void kp_gb_upload_blocks(KpGfxBackend *b, int tex, int32_t layer, int32_t w, int32_t h, const void *data, size_t size)
{
	ops(b)->upload_blocks(b, tex, layer, w, h, data, size);
}

int kp_gb_hd_target(KpGfxBackend *b, int32_t w, int32_t h) { return ops(b)->hd_target(b, w, h); }

void kp_gb_copy(KpGfxBackend *b, int from, int to) { ops(b)->copy(b, from, to); }

int kp_gb_read(KpGfxBackend *b, int target, int32_t x, int32_t y, int32_t w, int32_t h, uint8_t *out)
{
	return ops(b)->read(b, target, x, y, w, h, out);
}

int kp_gb_alive(const KpGfxBackend *b) { return ops(b)->alive(b); }

int kp_gb_present(KpGfxBackend *b, int wait) { return ops(b)->present(b, wait); }

int kp_gb_vsync(KpGfxBackend *b, int on) { return ops(b)->vsync(b, on); }

void kp_gb_finish(KpGfxBackend *b) { ops(b)->finish(b); }

const char *kp_gb_version(const KpGfxBackend *b) { return ops(b)->version(b); }
