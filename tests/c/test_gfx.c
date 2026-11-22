/*
 * kp_gfx without a GL context (plan phase S, ADR 0012): its programs' shaders compiled and linked by glslangValidator
 * as GLES 3.0 / WebGL2 and desktop GL core take them, and its passes (kp_gfx_passes.c) over a backend that records
 * the draws (kp_gfx_internal.h; kp_gfx_gl.c is the product's): the depth state each pass draws with, the HD
 * sprites' lighting layers and light.
 */
#define _POSIX_C_SOURCE 200809L /* popen */
#include "kp_test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kp_gfx_gpu_glsl.h"
#include "kp_gfx_internal.h"
#include "kp_sha256.h"

/* --- the recording backend --- */

#define DRAWS_MAX 256

typedef struct {
	int prog, depth, write;
	float slot_base; /* KP_U_SLOT_BASE's x when it was drawn: the family's first slot */
	int below;       /* KP_S_UI_BELOW bound to KP_TEX_UI_BELOW */
	int lit;         /* KP_S_HD_LIGHT, KP_S_HD_OCCLUSION and KP_S_HD_GLOW bound to their textures */
	int fit;         /* KP_S_GLOW_FIT bound to KP_TEX_GLOW_FIT */
} Draw;

struct KpGfxBackend {
	Draw draws[DRAWS_MAX];
	int count;
	float slot_base[KP_PROGRAMS];
	int made[KP_TEXTURES];
	int32_t w[KP_TEXTURES], h[KP_TEXTURES];
	int format[KP_TEXTURES], layers[KP_TEXTURES];
	uint32_t block_layers[KP_TEXTURES]; /* the layers (below 32) blocks went into */
	size_t last_blocks[KP_TEXTURES]; /* the last upload's bytes */
	int blocks;                         /* kp_gb_takes_blocks: every block format ... */
	int no_astc;                        /* ... but ASTC's */
	float light[4];                     /* KP_PROG_HD_IMAGES's KP_U_LIGHT */
	/* the SDL_GPU backend's pipelines (KP_GFX_GPU_PIPELINES): the target bound, the blend, the rows the draws took,
	 * the draws without one and the last of them */
	int bound, blend;
	uint32_t piped;
	int unpiped, unpiped_prog, unpiped_target, unpiped_blend, unpiped_depth;
};

static KpGfxBackend *recorder; /* the last one made: the test's (one gfx at a time) */

KpGfxBackend *kp_gb_new(const KpGfxApi *api, char *err, size_t err_size)
{
	(void)api;
	if (!(recorder = (KpGfxBackend *)calloc(1, sizeof *recorder))) snprintf(err, err_size, "out of memory");
	return recorder;
}

void kp_gb_free(KpGfxBackend *b, int lost)
{
	(void)lost;
	if (b == recorder) recorder = NULL;
	free(b);
}

int kp_gb_texture(KpGfxBackend *b, int tex, int format, int32_t w, int32_t h, int32_t layers)
{
	b->format[tex] = format;
	b->layers[tex] = layers;
	b->block_layers[tex] = 0;
	b->made[tex] = 1;
	b->w[tex] = w;
	b->h[tex] = h;
	return 0;
}

void kp_gb_drop(KpGfxBackend *b, int tex) { b->made[tex] = 0; }

int kp_gb_take_failures(KpGfxBackend *b, char *msg, size_t size)
{
	(void)b;
	if (size) msg[0] = 0;
	return 0;
}

int kp_gb_has_texture(const KpGfxBackend *b, int tex, int32_t *w, int32_t *h)
{
	if (w) *w = b->w[tex];
	if (h) *h = b->h[tex];
	return b->made[tex];
}

void kp_gb_upload(KpGfxBackend *b, int tex, int32_t layer, int32_t x, int32_t y, int32_t w, int32_t h, const void *data,
	size_t stride)
{
	(void)b, (void)tex, (void)layer, (void)x, (void)y, (void)w, (void)h, (void)data, (void)stride;
}

int kp_gb_targets(KpGfxBackend *b, int32_t w, int32_t h)
{
	(void)b, (void)w, (void)h;
	return 0;
}

void kp_gb_bind(KpGfxBackend *b, int target, int32_t vx, int32_t vy, int32_t vw, int32_t vh)
{
	(void)vx, (void)vy, (void)vw, (void)vh;
	b->bound = target;
}

void kp_gb_clear(KpGfxBackend *b, float r, float gg, float bb, float a, int depth)
{
	(void)b, (void)r, (void)gg, (void)bb, (void)a, (void)depth;
}

void kp_gb_uniform(KpGfxBackend *b, int prog, int u, float x, float y, float z, float w)
{
	if (u == KP_U_SLOT_BASE) b->slot_base[prog] = x;
	if (u == KP_U_LIGHT && prog == KP_PROG_HD_IMAGES) {
		b->light[0] = x;
		b->light[1] = y;
		b->light[2] = z;
		b->light[3] = w;
	}
}

void kp_gb_draw(KpGfxBackend *b, int prog, int32_t vertices, const KpGfxBinding *bind, int count, int depth, int write)
{
	Draw *d;
	int k, p = kp_gfx_gpu_pipeline(prog, kp_gfx_gpu_target_kind(b->bound), b->blend, kp_gfx_gpu_depth(depth, write));
	(void)vertices;
	if (p >= 0) {
		b->piped |= 1u << p;
	} else {
		b->unpiped++;
		b->unpiped_prog = prog;
		b->unpiped_target = b->bound;
		b->unpiped_blend = b->blend;
		b->unpiped_depth = kp_gfx_gpu_depth(depth, write);
	}
	if (b->count == DRAWS_MAX) return;
	d = &b->draws[b->count++];
	d->prog = prog;
	d->depth = depth;
	d->write = write;
	d->slot_base = b->slot_base[prog];
	d->below = 0;
	d->lit = 0;
	d->fit = 0;
	for (k = 0; k < count; k++) {
		d->fit |= bind[k].sampler == KP_S_GLOW_FIT && bind[k].texture == KP_TEX_GLOW_FIT;
		d->below |= bind[k].sampler == KP_S_UI_BELOW && bind[k].texture == KP_TEX_UI_BELOW;
		d->lit += (bind[k].sampler == KP_S_HD_LIGHT && bind[k].texture == KP_TEX_HD_LIGHT)
			+ (bind[k].sampler == KP_S_HD_OCCLUSION && bind[k].texture == KP_TEX_HD_OCCLUSION)
			+ (bind[k].sampler == KP_S_HD_GLOW && bind[k].texture == KP_TEX_HD_GLOW);
	}
	d->lit = d->lit == 3;
}

void kp_gb_blend(KpGfxBackend *b, int on) { b->blend = on ? KP_GPU_BLEND_ALPHA : KP_GPU_BLEND_OFF; }

void kp_gb_blend_premultiplied(KpGfxBackend *b) { b->blend = KP_GPU_BLEND_PREMULTIPLIED; }

int kp_gb_takes_blocks(KpGfxBackend *b, int format)
{
	return b->blocks && !(b->no_astc && format >= KP_FMT_ASTC_4X4 && format <= KP_FMT_ASTC_8X8);
}

void kp_gb_upload_blocks(KpGfxBackend *b, int tex, int32_t layer, int32_t w, int32_t h, const void *data, size_t size)
{
	(void)w, (void)h, (void)data;
	b->last_blocks[tex] = size;
	if (layer >= 0 && layer < 32) b->block_layers[tex] |= 1u << layer;
}

int kp_gb_hd_target(KpGfxBackend *b, int32_t w, int32_t h)
{
	(void)b, (void)w, (void)h;
	return 0;
}

void kp_gb_copy(KpGfxBackend *b, int from, int to) { (void)b, (void)from, (void)to; }

int kp_gb_read(KpGfxBackend *b, int target, int32_t x, int32_t y, int32_t w, int32_t h, uint8_t *out)
{
	(void)b, (void)target, (void)x, (void)y, (void)w, (void)h, (void)out;
	return -1;
}

int kp_gb_alive(const KpGfxBackend *b)
{
	(void)b;
	return 1;
}

KpGfxBackend *kp_gb_reset(KpGfxBackend *b, char *err, size_t err_size)
{
	kp_gb_free(b, 0);
	return kp_gb_new(NULL, err, err_size);
}

int kp_gb_present(KpGfxBackend *b, int wait)
{
	(void)b, (void)wait;
	return 1;
}

int kp_gb_vsync(KpGfxBackend *b, int on)
{
	(void)b, (void)on;
	return 0;
}

void kp_gb_finish(KpGfxBackend *b) { (void)b; }

const char *kp_gb_version(const KpGfxBackend *b)
{
	(void)b;
	return "recorder";
}

/* --- the shaders --- */

#define GLSLANG "glslangValidator"
#define GLSLANG_REQUIRED "KP_TEST_GLSLANG" /* set (tools/ci.sh): no glslangValidator fails the test, not skips it */
/* The desktop GL versions the host asks for (KpGfxApi.glsl_version). */
static const int CORE_VERSIONS[] = KP_GFX_GLSL_CORE;

/* Writes header and body into path (0, else -1). */
static int write_shader(const char *path, const char *header, const char *body)
{
	FILE *f = fopen(path, "w");
	if (!f) return -1;
	fputs(header, f);
	fputs(body, f);
	return fclose(f) == 0 ? 0 : -1;
}

/* The validator's verdict on a program's two stages linked (0 linked; its output in log). */
static int link_program(KpT *T, const char *dir, const KpGfxProgramSource *p, const char *header, char *log,
	size_t size)
{
	char vs[512], fs[512], command[1400];
	FILE *out;
	size_t n;
	snprintf(vs, sizeof vs, "%s/kp_gfx_%d.vert", dir, p->prog);
	snprintf(fs, sizeof fs, "%s/kp_gfx_%d.frag", dir, p->prog);
	kp_t_ok(T, write_shader(vs, header, p->vs) == 0 && write_shader(fs, header, p->fs) == 0, "%s: not written", dir);
	snprintf(command, sizeof command, GLSLANG " -l '%s' '%s' 2>&1", vs, fs);
	if (!(out = popen(command, "r"))) return -1;
	n = fread(log, 1, size - 1, out);
	log[n] = 0;
	remove(vs);
	remove(fs);
	return pclose(out);
}

/* The validator can run here (1), else why not, skipped (0: test's SKIP line) or with GLSLANG_REQUIRED set failed
 * (-1). */
static int glslang_runs(KpT *T, const char *test)
{
	const char *why = NULL;
	FILE *probe = popen("command -v " GLSLANG " 2>/dev/null", "r");
	if (probe == NULL) {
		why = "no popen (the wasm suites under node)";
	} else {
		int c = fgetc(probe);
		pclose(probe);
		if (c == EOF) why = "no " GLSLANG " (glslang) on PATH";
	}
	if (why == NULL) return 1;
	if (getenv(GLSLANG_REQUIRED) != NULL) {
		kp_t_ok(T, 0, "%s with " GLSLANG_REQUIRED " set: the shaders not compiled", why);
		return -1;
	}
	kp_t_log(T, "SKIP test_gfx.%s: %s", test, why);
	return 0;
}

/* Every program's vertex and fragment shader compile and link after the version and precision lines kp_gfx_gl.c puts
 * before them: GLES 3.0's (WebGL2, the phones) and desktop GL core's. A shader that does not compile stops the app at
 * its start (kp_gb_new); this is the only check before a context runs them. The files go in a directory of this run's
 * own (kp_test processes run beside each other). */
static void shaders_compile_for_every_context(KpT *T)
{
	const char *tmp = getenv("TMPDIR"), *dir;
	char log[4096], header[64];
	int k, v;
	if (glslang_runs(T, "shaders_compile_for_every_context") <= 0) return;
	dir = kp_t_temp_dir(T, tmp && tmp[0] ? tmp : "/tmp");
	for (k = 0; k < KP_PROGRAMS; k++) {
		const KpGfxProgramSource *p = &KP_GFX_PROGRAMS[k];
		kp_t_eq(T, p->prog, k, "the table in KP_PROG_* order");
		kp_t_eq(T, link_program(T, dir, p, KP_GFX_ES_HEADER, log, sizeof log), 0, "program %d, GLES 3.0:\n%s", k, log);
		for (v = 0; v < (int)(sizeof CORE_VERSIONS / sizeof CORE_VERSIONS[0]); v++) {
			snprintf(header, sizeof header, "#version %d core\n", CORE_VERSIONS[v]);
			kp_t_eq(T, link_program(T, dir, p, header, log, sizeof log), 0, "program %d, GL %d core:\n%s", k,
				CORE_VERSIONS[v], log);
		}
	}
}

/* --- the SDL_GPU backend's shaders (ADR 0028) --- */

#define GPU_TEXT_BYTES (64 * 1024)

/* A stage's Vulkan GLSL (kp_gfx_gpu_glsl) into text: its length. */
static int gpu_stage(KpT *T, int prog, int fragment, char *text, KpGfxGpuStage *stage)
{
	const KpGfxProgramSource *p = &KP_GFX_PROGRAMS[prog];
	char err[256] = "";
	int len = kp_gfx_gpu_glsl(fragment ? p->fs : p->vs, p->vs, !fragment, text, GPU_TEXT_BYTES, stage, err, sizeof err);
	kp_t_ok(T, len > 0, "program %d's %s stage: %s", prog, fragment ? "fragment" : "vertex", err);
	return len;
}

/* The transform's layout: the constants in one block at 16 times their KP_U_* number, the samplers bound in the
 * stage's set in their order, an unsigned integer one a texture without a sampler after them, the varyings at the
 * vertex outputs' locations, gl_VertexIndex, and the vertex stage's main ending on the negated y. What it cannot read
 * (an early return in a vertex stage's main, an unknown constant, an input no vertex output feeds) is refused. */
static void gpu_glsl_lays_out_the_declarations(KpT *T)
{
	static const char VS[] = "uniform highp sampler2D image_data;\n"
		"uniform vec4 view; // xy\n"
		"out highp vec2 var_a;\n"
		"flat out vec4 var_b;\n"
		"void main()\n"
		"{\n"
		"    int i = gl_VertexID % 6;\n"
		"    var_a = vec2(0.0);\n"
		"    var_b = vec4(float(i));\n"
		"    gl_Position = vec4(view.xy, 0.0, 1.0);\n"
		"}\n";
	static const char FS[] = "uniform highp usampler2D terrain_map;\n"
		"uniform highp sampler2DArray atlas;\n"
		"flat in vec4 var_b;\n"
		"in highp vec2 var_a;\n"
		"uniform highp vec4 hd;\n"
		"out vec4 out_fragColor;\n"
		"void main() { out_fragColor = var_b + vec4(texelFetch(terrain_map, ivec2(var_a), 0)); }\n";
	static char text[GPU_TEXT_BYTES];
	char err[256], want[128];
	KpGfxGpuStage st;
	kp_t_ok(T, kp_gfx_gpu_glsl(VS, VS, 1, text, sizeof text, &st, err, sizeof err) > 0, "the vertex stage: %s", err);
	snprintf(want, sizeof want, "layout(offset = %d) vec4 view;", 16 * KP_U_VIEW);
	kp_t_ok(T, strstr(text, "layout(std140, set = 1, binding = 0) uniform kp_vertex_constants") && strstr(text, want),
		"the vertex constants' block:\n%s", text);
	kp_t_ok(T, strstr(text, "layout(set = 0, binding = 0) uniform highp sampler2D image_data;") != NULL, "a vertex sampler");
	kp_t_ok(T, strstr(text, "layout(location = 0) out highp vec2 var_a;") != NULL
		&& strstr(text, "layout(location = 1) flat out vec4 var_b;") != NULL, "the outputs in order");
	kp_t_ok(T, strstr(text, "gl_VertexIndex % 6") != NULL && !strstr(text, "gl_VertexID"), "gl_VertexIndex");
	kp_t_ok(T, strstr(text, "1.0);\n    gl_Position.xy = gl_Position.xy * kp_viewport.xy + kp_viewport.zw * gl_Position.w;\n"
		"    gl_Position.y = -gl_Position.y;\n}\n") != NULL, "the viewport's mapping and the negated y last:\n%s", text);
	snprintf(want, sizeof want, "layout(offset = %d) vec4 kp_viewport;", 16 * KP_UNIFORMS);
	kp_t_ok(T, KP_GPU_VIEWPORT_OFFSET == 16 * KP_UNIFORMS && strstr(text, want) != NULL, "the mapping after the constants");
	kp_t_ok(T, st.samplers == 1 && st.sampler[0] == KP_S_IMAGE_DATA && st.kind[0] == KP_GPU_KIND_2D && st.storage == 0
		&& st.uniforms == 1, "the vertex stage's reflection");
	kp_t_ok(T, kp_gfx_gpu_glsl(FS, VS, 0, text, sizeof text, &st, err, sizeof err) > 0, "the fragment stage: %s", err);
	snprintf(want, sizeof want, "layout(offset = %d) vec4 hd;", 16 * KP_U_HD);
	kp_t_ok(T, strstr(text, "layout(std140, set = 3, binding = 0) uniform kp_fragment_constants") && strstr(text, want),
		"the fragment constants' block:\n%s", text);
	kp_t_ok(T, strstr(text, "#extension GL_EXT_samplerless_texture_functions : require") != NULL
		&& strstr(text, "layout(set = 2, binding = 1) uniform utexture2D terrain_map;") != NULL
		&& strstr(text, "layout(set = 2, binding = 0) uniform highp sampler2DArray atlas;") != NULL,
		"the integer texture after the sampler:\n%s", text);
	kp_t_ok(T, strstr(text, "layout(location = 1) flat in vec4 var_b;") != NULL
		&& strstr(text, "layout(location = 0) in highp vec2 var_a;") != NULL
		&& strstr(text, "layout(location = 0) out vec4 out_fragColor;") != NULL, "the inputs at the outputs' locations");
	kp_t_ok(T, st.samplers == 1 && st.sampler[0] == KP_S_ATLAS && st.kind[0] == KP_GPU_KIND_ARRAY && st.storage == 1
		&& st.storage_of[0] == KP_S_TERRAIN_MAP && st.uniforms == 1, "the fragment stage's reflection");
	kp_t_eq(T, kp_gfx_gpu_glsl("void main()\n{\n    if (true) return;\n}\n", VS, 1, text, sizeof text, &st, err,
		sizeof err), -1, "an early return refused");
	kp_t_eq(T, kp_gfx_gpu_glsl("void main() {\n    gl_Position = vec4(0.0);\n}\n", VS, 1, text, sizeof text, &st, err,
		sizeof err), -1, "a vertex main not on a line of its own refused (no mapping, no negated y)");
	kp_t_eq(T, kp_gfx_gpu_glsl("uniform vec4 nothing;\n", VS, 0, text, sizeof text, &st, err, sizeof err), -1,
		"an unknown constant refused");
	kp_t_eq(T, kp_gfx_gpu_glsl("in vec2 var_c;\n", VS, 0, text, sizeof text, &st, err, sizeof err), -1,
		"an input without its output refused");
	kp_t_eq(T, kp_gfx_gpu_glsl(VS, VS, 1, text, 64, &st, err, sizeof err), -1, "a text past its buffer refused");
}

/* The committed kp_gfx_gpu_shaders.c is the transform's of the current sources: each stage's reflection and the
 * SHA-256 of its text (the SPIR-V and the MSL come from tools that need not be here; kp_gfx_gpu_gen --check compares
 * them where they are). A source changed without the file regenerated fails here. */
static void gpu_shaders_are_current(KpT *T)
{
	static char text[GPU_TEXT_BYTES];
	char hex[2 * KP_SHA256_SIZE + 1];
	uint8_t digest[KP_SHA256_SIZE];
	int k, f;
	for (k = 0; k < KP_PROGRAMS; k++) {
		for (f = 0; f < 2; f++) {
			int n = f ? KP_GFX_GPU_PROGRAMS[k].fs : KP_GFX_GPU_PROGRAMS[k].vs, len;
			const KpGfxGpuShader *sh;
			KpGfxGpuStage st;
			kp_t_ok(T, n >= 0 && n < KP_GFX_GPU_SHADER_COUNT, "program %d's stage %d", k, n);
			sh = &KP_GFX_GPU_SHADERS[n];
			len = gpu_stage(T, k, f, text, &st);
			kp_sha256(text, (size_t)len, digest);
			kp_sha256_hex(digest, hex);
			kp_t_ok(T, strcmp(hex, sh->sha256) == 0 && memcmp(&st, &sh->stage, sizeof st) == 0,
				"program %d's %s stage changed: regenerate native/kp_gfx/kp_gfx_gpu_shaders.c (build/cmake/kp_gfx_gpu_gen"
				" native/kp_gfx/kp_gfx_gpu_shaders.c)", k, f ? "fragment" : "vertex");
			kp_t_ok(T, sh->spirv_size >= 20 && sh->spirv_size % 4 == 0 && sh->spirv[0] == 0x07230203u, "stage %d's SPIR-V",
				n);
			kp_t_ok(T, sh->msl_size == strlen(sh->msl) && strstr(sh->msl, f ? "fragment main0_out main0(" :
				"vertex main0_out main0(") != NULL, "stage %d's MSL entry point main0", n);
		}
	}
}

/* Every program's stages as the transform writes them compile as Vulkan GLSL (glslangValidator, as the generator
 * compiles them; skipped without it unless KP_TEST_GLSLANG is set). */
static void gpu_shaders_compile_for_vulkan(KpT *T)
{
	static char text[GPU_TEXT_BYTES];
	const char *tmp = getenv("TMPDIR"), *dir;
	char path[512], command[1400], log[4096];
	KpGfxGpuStage st;
	int k, f;
	if (glslang_runs(T, "gpu_shaders_compile_for_vulkan") <= 0) return;
	dir = kp_t_temp_dir(T, tmp && tmp[0] ? tmp : "/tmp");
	for (k = 0; k < KP_PROGRAMS; k++) {
		for (f = 0; f < 2; f++) { /* each stage on its own, as the generator compiles them: SPIR-V has no link */
			FILE *out;
			size_t n;
			gpu_stage(T, k, f, text, &st);
			snprintf(path, sizeof path, "%s/kp_gpu_%d.%s", dir, k, f ? "frag" : "vert");
			kp_t_ok(T, write_shader(path, "", text) == 0, "%s: not written", path);
			snprintf(command, sizeof command, GLSLANG " --target-env vulkan1.0 -o '%s.spv' '%s' 2>&1", path, path);
			out = popen(command, "r");
			kp_t_ok(T, out != NULL, "no popen");
			n = fread(log, 1, sizeof log - 1, out);
			log[n] = 0;
			kp_t_eq(T, pclose(out), 0, "program %d's %s stage for Vulkan:\n%s", k, f ? "fragment" : "vertex", log);
		}
	}
}

/* --- the passes' depth state --- */

static void gfx_free(void *p) { kp_gfx_free((KpGfx *)p); }

static void lookups_free(void *p)
{
	free(((KpLookups *)p)->tables);
	free(((KpLookups *)p)->glow);
}

/* The draws of prog in the frame recorded: their count; each one's depth test and depth mask must be depth and the
 * family's write (by its first slot: KP_FAMILY_BASE). */
static int check_family_draws(KpT *T, const KpGfxBackend *b, int prog, const int write[KP_FAMILIES], const char *what)
{
	int k, f, n = 0;
	for (k = 0; k < b->count; k++) {
		const Draw *d = &b->draws[k];
		if (d->prog != prog) continue;
		for (f = 0; f < KP_FAMILIES && (float)KP_FAMILY_BASE[f] != d->slot_base; f++) {}
		kp_t_ok(T, f < KP_FAMILIES, "%s: program %d's draw %d has no family's slots", what, prog, k);
		kp_t_eq(T, d->depth, KP_DEPTH_GREATER, "%s: program %d's draw %d tests the ranks", what, prog, k);
		kp_t_eq(T, d->write, write[f], "%s: program %d, family %d's depth mask", what, prog, f);
		n++;
	}
	return n;
}

/* The world's depth: the index world's overwriting and distortion images leave their ranks (the original's distortion
 * is opaque), its destination layers map what lies under them and leave none; in the RGBA world (the HD art's, or the
 * original's through it at a scale, stage R5) only the overwriting family writes: the destination and distortion
 * families blend over every image under them (a cloak at a quarter's alpha), and a rank they left would cut the lower
 * of two overlapping cloaked units out under the higher one's texels. Every full-screen pass draws without depth. */
static void world_passes_keep_their_depth_state(KpT *T)
{
	static const int INDEX_WRITES[KP_FAMILIES] = { 1, 0, 1 }, HD_WRITES[KP_FAMILIES] = { 1, 0, 0 };
	static KpPlan plan;
	static KpLookups lookups;
	static uint8_t table[256];
	KpGfxApi api = { 1, 0, KP_GFX_BACKEND_GL, NULL, NULL };
	KpGfxFrame f;
	KpGfx *g;
	char err[256] = "";
	int k;
	g = kp_gfx_new(&api, err, sizeof err);
	kp_t_ok(T, g != NULL && recorder != NULL, "no gfx: %s", err);
	kp_t_defer(T, gfx_free, g);
	kp_t_eq(T, KP_FAMILY_OVERWRITE == 0 && KP_FAMILY_DEST == 1 && KP_FAMILY_DISTORT == 2, 1, "the families' order");
	memset(&lookups, 0, sizeof lookups);
	lookups.remap_rows = 1;
	lookups.table_rows = 1;
	lookups.tables = (uint8_t *)malloc(sizeof table);
	kp_t_ok(T, lookups.tables != NULL, "out of memory");
	kp_t_defer(T, lookups_free, &lookups);
	kp_gfx_lookups(g, &lookups); /* the destination layers drawn */
	recorder->made[KP_TEX_PAGES] = recorder->made[KP_TEX_FRAMES] = recorder->made[KP_TEX_IMAGE_DATA] = 1;
	kp_gfx_world(g, 1);
	memset(&plan, 0, sizeof plan);
	plan.slots[KP_FAMILY_OVERWRITE] = 3;
	plan.slots[KP_FAMILY_DEST] = 2;
	plan.slots[KP_FAMILY_DISTORT] = 1;
	plan.count = 1; /* one distortion image's window, a destination layer in it and after it */
	plan.windows[0].rank = plan.windows[0].last = 10;
	plan.windows[0].layers = 1;
	plan.layers = 1;
	memset(&f, 0, sizeof f);
	f.win_w = f.full_w = 640;
	f.win_h = f.full_h = 480;
	f.mode = KP_SCALE_INTEGER;
	f.scale = 1;
	f.world_w = 640;
	f.world_h = 480;
	f.plan = &plan;
	kp_gfx_frame(g, &f);
	kp_t_eq(T, check_family_draws(T, recorder, KP_PROG_IMAGES, INDEX_WRITES, "index world"), 2, "two overwriting windows");
	kp_t_eq(T, check_family_draws(T, recorder, KP_PROG_DEST, INDEX_WRITES, "index world"), 2, "a layer in each");
	kp_t_eq(T, check_family_draws(T, recorder, KP_PROG_DISTORT, INDEX_WRITES, "index world"), 1, "the distortion");
	for (k = 0; k < recorder->count; k++) {
		const Draw *d = &recorder->draws[k];
		if (d->prog == KP_PROG_IMAGES || d->prog == KP_PROG_DEST || d->prog == KP_PROG_DISTORT) continue;
		kp_t_ok(T, d->depth == KP_DEPTH_OFF && d->write == 0, "index world: full-screen program %d with depth", d->prog);
	}
	recorder->count = 0;
	kp_gfx_hd(g, 1);
	kp_gfx_effects(g, 2, 1, 0); /* the original's art through the RGBA world */
	kp_t_ok(T, kp_gfx_hd_ready(g), "the RGBA world ready");
	kp_gfx_frame(g, &f);
	kp_t_eq(T, check_family_draws(T, recorder, KP_PROG_HD_IMAGES, HD_WRITES, "RGBA world"), 3, "each family once");
	for (k = 0; k < recorder->count; k++) {
		const Draw *d = &recorder->draws[k];
		if (d->prog == KP_PROG_HD_IMAGES) continue;
		kp_t_ok(T, d->prog != KP_PROG_IMAGES && d->prog != KP_PROG_DEST && d->prog != KP_PROG_DISTORT,
			"RGBA world: the index world drawn too");
		kp_t_ok(T, d->depth == KP_DEPTH_OFF && d->write == 0, "RGBA world: full-screen program %d with depth", d->prog);
	}
}

/* The original's art through the RGBA world cannot look a blend bank's destination up (an index): its glows and flames
 * read the fitted blends of the table rows (kp_lookups_build's glow, one texel a row, 256 to a texture row), bound to
 * every draw of the world's images; a lookups without them (no fit made) draws no blend from the texture. */
static void rgba_world_reads_the_fitted_blends(KpT *T)
{
	static KpPlan plan;
	static KpLookups lookups;
	KpGfxApi api = { 1, 0, KP_GFX_BACKEND_GL, NULL, NULL };
	KpGfxFrame f;
	KpGfx *g;
	char err[256] = "";
	int k, bound = 0, fit_rows = (KP_LOOKUP_TABLE_ROWS + 255) / 256;
	g = kp_gfx_new(&api, err, sizeof err);
	kp_t_ok(T, g != NULL && recorder != NULL, "no gfx: %s", err);
	kp_t_defer(T, gfx_free, g);
	memset(&lookups, 0, sizeof lookups);
	lookups.remap_rows = 1;
	lookups.table_rows = KP_LOOKUP_TABLE_ROWS;
	lookups.tables = (uint8_t *)calloc((size_t)KP_LOOKUP_TABLE_ROWS, 256);
	lookups.glow = (uint8_t *)calloc((size_t)KP_LOOKUP_TABLE_ROWS, 4);
	kp_t_ok(T, lookups.tables != NULL && lookups.glow != NULL, "out of memory");
	kp_t_defer(T, lookups_free, &lookups);
	kp_gfx_lookups(g, &lookups);
	kp_t_ok(T, recorder->made[KP_TEX_GLOW_FIT], "the fitted blends' texture made");
	kp_t_eq(T, recorder->w[KP_TEX_GLOW_FIT], 256, "256 rows' texels to a texture row");
	kp_t_eq(T, recorder->h[KP_TEX_GLOW_FIT], fit_rows, "every table row has its texel");
	kp_t_eq(T, recorder->format[KP_TEX_GLOW_FIT], KP_FMT_RGBA8, "a premultiplied colour and its coverage");
	recorder->made[KP_TEX_PAGES] = recorder->made[KP_TEX_FRAMES] = recorder->made[KP_TEX_IMAGE_DATA] = 1;
	kp_gfx_world(g, 1);
	memset(&plan, 0, sizeof plan);
	plan.slots[KP_FAMILY_OVERWRITE] = 3;
	plan.slots[KP_FAMILY_DEST] = 2;
	plan.slots[KP_FAMILY_DISTORT] = 1;
	plan.count = 1;
	plan.windows[0].rank = plan.windows[0].last = 10;
	plan.windows[0].layers = 1;
	plan.layers = 1;
	memset(&f, 0, sizeof f);
	f.win_w = f.full_w = 640;
	f.win_h = f.full_h = 480;
	f.mode = KP_SCALE_INTEGER;
	f.scale = 1;
	f.world_w = 640;
	f.world_h = 480;
	f.plan = &plan;
	kp_gfx_hd(g, 1);
	kp_gfx_effects(g, 2, 1, 0); /* the original's art through the RGBA world */
	kp_gfx_frame(g, &f);
	for (k = 0; k < recorder->count; k++) {
		if (recorder->draws[k].prog != KP_PROG_HD_IMAGES) continue;
		kp_t_ok(T, recorder->draws[k].fit, "the RGBA world's draw %d reads the fitted blends", k);
		bound++;
	}
	kp_t_eq(T, bound, 3, "each family once");
}

/* The canvas pass uploads the HD interface's planes at the canvas's size: the items and what lay under their SD
 * pixels (KpUiCanvas.below), which the HD composite reads. */
static void hd_composite_reads_what_lay_under(KpT *T)
{
	enum { W = 8, H = 4 };
	static KpCanvas c;
	static uint8_t pixels[W * H * 2];
	static uint16_t ids[W * H], below[W * H];
	KpGfxApi api = { 1, 0, KP_GFX_BACKEND_GL, NULL, NULL };
	KpGfxFrame f;
	KpGfx *g;
	char err[256] = "";
	int k, composites = 0;
	g = kp_gfx_new(&api, err, sizeof err);
	kp_t_ok(T, g != NULL && recorder != NULL, "no gfx: %s", err);
	kp_t_defer(T, gfx_free, g);
	memset(&c, 0, sizeof c);
	c.w = c.canvas.w = W;
	c.h = c.canvas.h = H;
	c.canvas.pixels = pixels;
	c.canvas.ids = ids;
	c.canvas.below = below;
	c.dirty[2] = W;
	c.dirty[3] = H;
	kp_gfx_canvas(g, &c);
	kp_t_ok(T, recorder->made[KP_TEX_UI_IDS] && recorder->made[KP_TEX_UI_BELOW], "both planes made");
	kp_t_ok(T, recorder->w[KP_TEX_UI_BELOW] == W && recorder->h[KP_TEX_UI_BELOW] == H, "at the canvas's size: %d x %d",
		(int)recorder->w[KP_TEX_UI_BELOW], (int)recorder->h[KP_TEX_UI_BELOW]);
	kp_gfx_hd(g, 1);
	kp_gfx_effects(g, 2, 1, 0);
	memset(&f, 0, sizeof f);
	f.win_w = f.full_w = 640;
	f.win_h = f.full_h = 480;
	f.mode = KP_SCALE_INTEGER;
	f.scale = 1;
	f.world_w = 640;
	f.world_h = 480;
	kp_gfx_frame(g, &f);
	for (k = 0; k < recorder->count; k++) {
		const Draw *d = &recorder->draws[k];
		if (d->prog != KP_PROG_HD_COMPOSITE) continue;
		composites++;
		kp_t_ok(T, d->below, "the composite's draw %d reads the plane", k);
	}
	kp_t_ok(T, composites > 0, "the HD composite drawn");
}

/* The lit slots (blocks only) get the lighting layers (the normal map, the occlusion, and two a slot of the specular and
 * emissive colours), filled only for a page in a lit slot; the HD images' draws bind them, with
 * the half vector of StarCraft: Remastered's light and its eye over the view and the lit slots' count. */
static void hd_lit_slots_light_the_sprites(KpT *T)
{
	static uint8_t diffuse[64 * 64], team[64 * 32], layer[64 * 64];
	uint8_t *const light[KP_HD_LIGHTS] = { layer, layer, layer, layer };
	static const float HALF[3] = { 0.45169f, 0.14614f, 0.88012f }; /* the view 640 x 480 */
	static KpPlan plan;
	KpGfxApi api = { 1, 0, KP_GFX_BACKEND_GL, NULL, NULL };
	KpGfxFrame f;
	KpGfx *g;
	char err[256] = "";
	int k, lit = 0;
	g = kp_gfx_new(&api, err, sizeof err);
	kp_t_ok(T, g != NULL && recorder != NULL, "no gfx: %s", err);
	kp_t_defer(T, gfx_free, g);
	recorder->blocks = 1;
	kp_t_eq(T, kp_gfx_hd_slots(g, 4, 2, 64, KP_HD_FORMAT_BC, 1), 0, "made");
	kp_t_ok(T, recorder->made[KP_TEX_HD_LIGHT] && recorder->format[KP_TEX_HD_LIGHT] == KP_FMT_BC3
		&& recorder->layers[KP_TEX_HD_LIGHT] == 2, "the normal maps: a BC3 layer a lit slot");
	kp_t_ok(T, recorder->made[KP_TEX_HD_OCCLUSION] && recorder->format[KP_TEX_HD_OCCLUSION] == KP_FMT_BC3
		&& recorder->layers[KP_TEX_HD_OCCLUSION] == 2, "the occlusion: a BC3 layer a lit slot");
	kp_t_ok(T, recorder->made[KP_TEX_HD_GLOW] && recorder->format[KP_TEX_HD_GLOW] == KP_FMT_BC1
		&& recorder->layers[KP_TEX_HD_GLOW] == 4, "the specular and emissive colours: two BC1 layers a lit slot");
	kp_gfx_hd_page(g, 1, diffuse, team, light);
	kp_t_ok(T, recorder->block_layers[KP_TEX_HD_LIGHT] == 0x2 && recorder->block_layers[KP_TEX_HD_OCCLUSION] == 0x2
		&& recorder->block_layers[KP_TEX_HD_GLOW] == 0xc, "lit slot 1's layers: 1, 1, and 2 and 3");
	kp_gfx_hd_page(g, 3, diffuse, team, light);
	kp_t_eq(T, recorder->block_layers[KP_TEX_HD_LIGHT], 0x2, "an unlit slot's page brings none");
	kp_t_eq(T, (int)recorder->block_layers[KP_TEX_HD_DIFFUSE], 0xa, "both pages' diffuse blocks");
	kp_gfx_hd(g, 1);
	kp_gfx_effects(g, 2, 1, 0); /* the RGBA world drawn */
	memset(&f, 0, sizeof f);
	f.win_w = f.full_w = 640;
	f.win_h = f.full_h = 480;
	f.mode = KP_SCALE_INTEGER;
	f.scale = 1;
	f.world_w = 640;
	f.world_h = 480;
	memset(&plan, 0, sizeof plan);
	plan.slots[KP_FAMILY_OVERWRITE] = 1; /* one sprite */
	f.plan = &plan;
	recorder->made[KP_TEX_PAGES] = recorder->made[KP_TEX_FRAMES] = recorder->made[KP_TEX_IMAGE_DATA] = 1;
	kp_gfx_world(g, 1);
	kp_gfx_frame(g, &f);
	for (k = 0; k < recorder->count; k++) {
		if (recorder->draws[k].prog != KP_PROG_HD_IMAGES) continue;
		kp_t_ok(T, recorder->draws[k].lit, "HD images' draw %d binds the lighting layers", k);
		lit++;
	}
	kp_t_ok(T, lit > 0, "the HD images drawn");
	for (k = 0; k < 3; k++) {
		kp_t_ok(T, recorder->light[k] > HALF[k] - 1e-4f && recorder->light[k] < HALF[k] + 1e-4f,
			"the half vector's %d: %f", k, (double)recorder->light[k]);
	}
	kp_t_eq(T, (int)recorder->light[3], 2, "two lit slots");
	kp_t_eq(T, kp_gfx_hd_slots(g, 4, 2, 64, KP_HD_FORMAT_ETC2, 1), 0, "ETC2 slots made");
	kp_t_ok(T, recorder->made[KP_TEX_HD_LIGHT] && recorder->format[KP_TEX_HD_LIGHT] == KP_FMT_EAC_RG11
		&& recorder->layers[KP_TEX_HD_LIGHT] == 2, "ETC2's normal maps: an RG11 layer a lit slot");
	kp_t_ok(T, recorder->made[KP_TEX_HD_OCCLUSION] && recorder->format[KP_TEX_HD_OCCLUSION] == KP_FMT_EAC_R11
		&& recorder->layers[KP_TEX_HD_OCCLUSION] == 2, "ETC2's occlusion: an R11 layer a lit slot");
	kp_t_ok(T, recorder->made[KP_TEX_HD_GLOW] && recorder->format[KP_TEX_HD_GLOW] == KP_FMT_ETC2_RGB8
		&& recorder->layers[KP_TEX_HD_GLOW] == 4, "ETC2's specular and emissive colours: two RGB8 layers a lit slot");
	kp_t_ok(T, recorder->format[KP_TEX_HD_DIFFUSE] == KP_FMT_ETC2_RGBA8 && recorder->format[KP_TEX_HD_TEAM] == KP_FMT_EAC_R11,
		"ETC2's diffuse and team blocks");
	kp_gfx_hd_page(g, 0, diffuse, team, light);
	kp_t_ok(T, recorder->block_layers[KP_TEX_HD_LIGHT] == 0x1 && recorder->block_layers[KP_TEX_HD_OCCLUSION] == 0x1
		&& recorder->block_layers[KP_TEX_HD_GLOW] == 0x3, "lit slot 0's layers: 0, 0, and 0 and 1");
	kp_t_eq(T, kp_gfx_hd_slots(g, 4, 2, 64, KP_HD_FORMAT_ASTC, 1), 0, "ASTC slots made");
	kp_t_ok(T, recorder->format[KP_TEX_HD_DIFFUSE] == KP_FMT_ASTC_4X4 && recorder->format[KP_TEX_HD_TEAM] == KP_FMT_ASTC_8X8,
		"ASTC's diffuse and team blocks");
	kp_t_ok(T, recorder->format[KP_TEX_HD_LIGHT] == KP_FMT_EAC_RG11 && recorder->format[KP_TEX_HD_OCCLUSION] == KP_FMT_EAC_R11
		&& recorder->format[KP_TEX_HD_GLOW] == KP_FMT_ASTC_6X6,
		"ASTC's pages keep their normal maps and occlusion in EAC, their colours in ASTC");
	recorder->last_blocks[KP_TEX_HD_DIFFUSE] = recorder->last_blocks[KP_TEX_HD_TEAM] = 0;
	kp_gfx_hd_page(g, 0, diffuse, team, light);
	kp_t_eq(T, (int)recorder->last_blocks[KP_TEX_HD_DIFFUSE], 16 * 16 * 16, "a 64-pixel page's 4 x 4 blocks");
	kp_t_eq(T, (int)recorder->last_blocks[KP_TEX_HD_TEAM], 8 * 8 * 16, "its 8 x 8 blocks");
	recorder->no_astc = 1;
	kp_t_eq(T, kp_gfx_hd_takes(g, KP_HD_FORMAT_ASTC), 0, "a context without ASTC");
	kp_t_eq(T, kp_gfx_hd_takes(g, KP_HD_FORMAT_ETC2), 1, "takes ETC2 all the same");
	kp_t_eq(T, kp_gfx_hd_takes(g, KP_HD_FORMAT_BC), 1, "and S3TC");
	kp_t_eq(T, kp_gfx_hd_takes(g, -1), 0, "no format");
	kp_t_eq(T, kp_gfx_hd_takes(g, KP_HD_FORMATS), 0, "a format past the known ones");
	recorder->no_astc = 0;
	recorder->blocks = 0;
	kp_t_eq(T, kp_gfx_hd_takes(g, KP_HD_FORMAT_ETC2), 0, "a context taking no blocks");
	kp_t_eq(T, kp_gfx_hd_slots(g, 4, 2, 64, KP_HD_FORMAT_BC, 1), 0, "decoded slots made");
	kp_t_ok(T, !recorder->made[KP_TEX_HD_LIGHT] && !recorder->made[KP_TEX_HD_OCCLUSION] && !recorder->made[KP_TEX_HD_GLOW],
		"decoded pages without lighting");
	recorder->count = 0;
	kp_gfx_frame(g, &f);
	kp_t_eq(T, (int)recorder->light[3], 0, "no lit slots: the sprites unlit");
}

/* Every draw of the passes has its SDL_GPU pipeline (ADR 0028: all are made when the backend starts, from
 * KP_GFX_GPU_PIPELINES), and every row of the table is drawn with: the index world with its fog, canvas, cursor and
 * vector text at a whole and at a sharp scale, the RGBA world with its fog under the HD interface, the vector text and
 * the cursor's HD frame, a movie. */
static void gpu_pipelines_cover_the_passes(KpT *T)
{
	enum { W = 8, H = 4, LEVELS = 4 };
	static KpPlan plan;
	static KpLookups lookups;
	static KpCanvas c;
	static KpCursor cursor;
	static KpVText text;
	static KpFogLayer fog;
	static uint8_t pixels[W * H * 2], atlas[16], dark[256 * KP_DARK_ROWS], plane[16 * 16];
	static uint16_t ids[W * H], below[W * H], cells[8];
	static float quads[KP_VTEXT_QUAD_FLOATS], items[2 * KP_GFX_UI_ITEM_FLOATS];
	static const float RECT[4] = { 10, 10, 32, 32 }, SRC[4] = { 0, 0, 1, 1 };
	KpGfxApi api = { 1, 0, KP_GFX_BACKEND_GL, NULL, NULL };
	KpVideoPicture v;
	KpGfxFrame f;
	KpGfx *g;
	char err[256] = "";
	int k;
	g = kp_gfx_new(&api, err, sizeof err);
	kp_t_ok(T, g != NULL && recorder != NULL, "no gfx: %s", err);
	kp_t_defer(T, gfx_free, g);
	kp_t_ok(T, KP_GFX_GPU_PIPELINE_COUNT <= 32, "the rows fit the recorder's mask");
	memset(&lookups, 0, sizeof lookups);
	lookups.remap_rows = 1;
	lookups.table_rows = 1;
	lookups.tables = (uint8_t *)calloc(1, 256);
	kp_t_ok(T, lookups.tables != NULL, "out of memory");
	kp_t_defer(T, lookups_free, &lookups);
	kp_gfx_lookups(g, &lookups);
	kp_gfx_terrain(g, atlas, 4, 4, 1, 0, cells, 2, 2);
	recorder->made[KP_TEX_PAGES] = recorder->made[KP_TEX_FRAMES] = recorder->made[KP_TEX_IMAGE_DATA] = 1;
	kp_gfx_world(g, 1);
	memset(&fog, 0, sizeof fog);
	fog.on = 1;
	fog.width = fog.height = LEVELS;
	kp_gfx_fog(g, &fog, dark);
	memset(&c, 0, sizeof c);
	c.w = c.canvas.w = W;
	c.h = c.canvas.h = H;
	c.canvas.pixels = pixels;
	c.canvas.ids = ids;
	c.canvas.below = below;
	c.dirty[2] = W;
	c.dirty[3] = H;
	kp_gfx_canvas(g, &c);
	cursor.shown = cursor.changed = 1;
	cursor.w = cursor.h = 1;
	kp_gfx_cursor(g, &cursor, 4, 4);
	memset(&text, 0, sizeof text);
	text.atlas = atlas;
	text.atlas_w = text.atlas_h = 4;
	text.atlas_changed = 1;
	text.quads = quads;
	text.quad_count = 1;
	text.quads_changed = 1;
	kp_gfx_text(g, &text);
	memset(&plan, 0, sizeof plan);
	plan.slots[KP_FAMILY_OVERWRITE] = 3;
	plan.slots[KP_FAMILY_DEST] = 2;
	plan.slots[KP_FAMILY_DISTORT] = 1;
	plan.count = 1;
	plan.windows[0].rank = plan.windows[0].last = 10;
	plan.windows[0].layers = 1;
	plan.layers = 1;
	memset(&f, 0, sizeof f);
	f.win_w = f.full_w = 1280;
	f.win_h = f.full_h = 960;
	f.mode = KP_SCALE_INTEGER;
	f.scale = 2;
	f.world_w = 640;
	f.world_h = 480;
	f.plan = &plan;
	kp_gfx_frame(g, &f);
	f.mode = KP_SCALE_SHARP;
	f.scale = 1.5;
	kp_gfx_frame(g, &f);
	kp_gfx_ui_atlas(g, 16, 1);
	kp_gfx_ui_items(g, items, 1);
	kp_gfx_ui_cursor(g, RECT, SRC, 0);
	kp_gfx_hd(g, 1);
	kp_gfx_effects(g, 2, 1, 0); /* the original's art through the RGBA world, its fog smooth (the HD fog pass) */
	kp_t_ok(T, kp_gfx_hd_ready(g), "the RGBA world ready");
	kp_gfx_frame(g, &f);
	memset(&v, 0, sizeof v);
	for (k = 0; k < 3; k++) {
		v.planes[k] = plane;
		v.strides[k] = 16;
	}
	v.width = v.height = v.chroma_w = v.chroma_h = 16;
	kp_gfx_video(g, &v, 16, 16, &f);
	kp_t_eq(T, recorder->unpiped, 0, "draws without a pipeline: the last, program %d into target %d, blend %d, depth %d",
		recorder->unpiped_prog, recorder->unpiped_target, recorder->unpiped_blend, recorder->unpiped_depth);
	for (k = 0; k < KP_GFX_GPU_PIPELINE_COUNT; k++) {
		const KpGfxGpuPipeline *p = &KP_GFX_GPU_PIPELINES[k];
		kp_t_ok(T, (recorder->piped >> k) & 1, "pipeline %d (program %d, target %d, blend %d, depth %d) not drawn with", k,
			p->prog, p->target, p->blend, p->depth);
	}
}

static const KpTest TESTS[] = {
	{ "shaders_compile_for_every_context", shaders_compile_for_every_context, 0 },
	{ "gpu_glsl_lays_out_the_declarations", gpu_glsl_lays_out_the_declarations, 0 },
	{ "gpu_shaders_are_current", gpu_shaders_are_current, 0 },
	{ "gpu_shaders_compile_for_vulkan", gpu_shaders_compile_for_vulkan, 0 },
	{ "world_passes_keep_their_depth_state", world_passes_keep_their_depth_state, 0 },
	{ "rgba_world_reads_the_fitted_blends", rgba_world_reads_the_fitted_blends, 0 },
	{ "hd_composite_reads_what_lay_under", hd_composite_reads_what_lay_under, 0 },
	{ "hd_lit_slots_light_the_sprites", hd_lit_slots_light_the_sprites, 0 },
	{ "gpu_pipelines_cover_the_passes", gpu_pipelines_cover_the_passes, 0 },
};

KP_SUITE(kp_suite_gfx, "test_gfx", TESTS);
