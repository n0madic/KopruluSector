/*
 * The SDL_GPU backend's shaders from the one source (ADR 0028): each GLSL ES 3.00 stage of kp_gfx_shaders.c rewritten
 * as Vulkan GLSL 450 in SDL_GPU's resource layout, line by line and deterministically, and its reflection (the
 * bindings the backend makes for a draw). The generator (tools/kp_gfx_gpu_gen.c) compiles the text to SPIR-V and MSL;
 * test_gfx checks the committed output against this transform.
 */
#ifndef KP_GFX_GPU_GLSL_H
#define KP_GFX_GPU_GLSL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* SDL_GPU's limits per stage: samplers, storage textures. */
#define KP_GPU_STAGE_SAMPLERS 16
#define KP_GPU_STAGE_STORAGE 8

/* A sampler's texture kind: a 2D texture, a 2D array; the unsigned integer textures are storage textures. */
enum { KP_GPU_KIND_2D, KP_GPU_KIND_ARRAY, KP_GPU_KIND_UINT, KP_GPU_KINDS };

/* A vertex stage's mapping of GL's viewport into the backend's (xy scale, zw offset of the NDC position: 1 and 0 when
 * they are one), after the constants in its block: KP_GPU_VIEWPORT_OFFSET bytes in, 16 * KP_UNIFORMS. */
#define KP_GPU_VIEWPORT_OFFSET 400

/* A stage's reflection: its samplers in binding order (set 0 in a vertex stage, 2 in a fragment stage), then its
 * unsigned integer textures (storage textures, bound after the samplers in the same set), and whether it reads the
 * constants (one uniform buffer: every KP_U_* at 16 * its number; every vertex stage's, its viewport's mapping). */
typedef struct {
	uint8_t samplers, storage, uniforms;
	uint8_t sampler[KP_GPU_STAGE_SAMPLERS]; /* per binding its KP_S_* */
	uint8_t kind[KP_GPU_STAGE_SAMPLERS];    /* and its KP_GPU_KIND_* */
	uint8_t storage_of[KP_GPU_STAGE_STORAGE]; /* per storage texture its KP_S_* */
} KpGfxGpuStage;

/* A stage compiled (kp_gfx_gpu_shaders.c, the generator's): its SPIR-V (entry point main), its MSL (main0), its
 * reflection and the SHA-256 of the transform's text it came from (64 hex digits); its metallib (main0), built offline
 * by Xcode's Metal toolchain from the MSL whose SHA-256 it records, or NULL where the generator had no toolchain and no
 * metallib of that MSL to keep (Metal compiles the MSL when the backend starts). */
typedef struct {
	const uint32_t *spirv;
	size_t spirv_size; /* bytes */
	const char *msl;
	size_t msl_size;
	KpGfxGpuStage stage;
	const char *sha256;
	const uint8_t *metallib;
	size_t metallib_size;
	const char *metallib_msl_sha256;
} KpGfxGpuShader;

/* Per KP_PROG_* its stages' numbers in KP_GFX_GPU_SHADERS. */
typedef struct {
	int vs, fs;
} KpGfxGpuProgram;

extern const KpGfxGpuShader KP_GFX_GPU_SHADERS[];
extern const int KP_GFX_GPU_SHADER_COUNT;
extern const char KP_GFX_GPU_METAL[]; /* the metal compiler's version the metallibs come from ("" none) */
extern const KpGfxGpuProgram KP_GFX_GPU_PROGRAMS[]; /* KP_PROGRAMS */

/* The kinds of target a pipeline draws into: the index target (R8 with depth), the HD target (RGBA8 with depth), an
 * RGBA8 target without depth (the RGB target, the window). */
enum { KP_GPU_TARGET_INDEX, KP_GPU_TARGET_HD, KP_GPU_TARGET_RGBA, KP_GPU_TARGETS };
/* The blends: off, by the colour's alpha (kp_gb_blend), premultiplied (kp_gb_blend_premultiplied). */
enum { KP_GPU_BLEND_OFF, KP_GPU_BLEND_ALPHA, KP_GPU_BLEND_PREMULTIPLIED };
/* The depth states: no test, the test (KP_DEPTH_GREATER), the test writing the depth (kp_gb_draw's write). */
enum { KP_GPU_DEPTH_OFF, KP_GPU_DEPTH_TEST, KP_GPU_DEPTH_WRITE };

/* A pipeline: a KP_PROG_* drawn into a kind of target with a blend and a depth state. */
typedef struct {
	uint8_t prog, target, blend, depth;
} KpGfxGpuPipeline;

/* The pipelines the passes draw with (kp_gfx_passes.c's kp_gb_draw calls), all made when the backend starts; test_gfx
 * checks that every draw of the passes has its row. */
extern const KpGfxGpuPipeline KP_GFX_GPU_PIPELINES[];
extern const int KP_GFX_GPU_PIPELINE_COUNT;

/* A KP_TARGET_*'s kind (KP_GPU_TARGET_*). */
int kp_gfx_gpu_target_kind(int target);
/* kp_gb_draw's depth and write as a KP_GPU_DEPTH_*. */
int kp_gfx_gpu_depth(int depth, int write);
/* The row of KP_GFX_GPU_PIPELINES of a draw, or -1 when the table has none. */
int kp_gfx_gpu_pipeline(int prog, int target_kind, int blend, int depth);

/* Writes the Vulkan GLSL 450 of a stage: vertex 1, else a fragment stage whose inputs take the locations of vs's
 * outputs. The length of the text (NUL-terminated in out), or -1 with the reason in err. */
int kp_gfx_gpu_glsl(const char *body, const char *vs, int vertex, char *out, size_t size, KpGfxGpuStage *stage,
	char *err, size_t err_size);

#ifdef __cplusplus
}
#endif

#endif
