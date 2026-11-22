/*
 * GLSL ES 3.00 to Vulkan GLSL 450 in SDL_GPU's layout (kp_gfx_gpu_glsl.h). The sources' declarations are regular (one a
 * line, from the line's start): `uniform [precision] vec4 NAME;` becomes a member of the stage's uniform block at
 * 16 * its KP_U_* number; a sampler a binding in the stage's set in the order declared, an unsigned integer one a
 * texture without a sampler (SDL_GPU refuses integer formats in samplers) bound after them; the varyings take the
 * locations of the vertex stage's outputs in its order; gl_VertexID is gl_VertexIndex; and the vertex stage's main ends
 * by mapping gl_Position from GL's viewport into the one the backend sets (kp_viewport, after the constants in the
 * block: GL's viewport may lie past its target, SDL_GPU's may not) and by negating gl_Position.y, so that a target's
 * memory rows are where GL has them (row 0 at NDC y -1: SDL_GPU puts NDC y +1 at the viewport's first row on Metal and
 * on Vulkan alike).
 */
#include "kp_gfx_gpu_glsl.h"
#include "kp_gfx_internal.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define LINE_BYTES 512
#define NAME_BYTES 64
#define VARYINGS_MAX 16

typedef struct {
	char *out;
	size_t size, len;
	int over;
} Text;

static void put(Text *t, const char *s, size_t n)
{
	if (t->over || t->len + n + 1 > t->size) {
		t->over = 1;
		return;
	}
	memcpy(t->out + t->len, s, n);
	t->len += n;
	t->out[t->len] = 0;
}

static void put_str(Text *t, const char *s) { put(t, s, strlen(s)); }

static void put_fmt(Text *t, const char *fmt, ...)
{
	char buf[LINE_BYTES];
	va_list ap;
	int n;
	va_start(ap, fmt);
	n = vsnprintf(buf, sizeof buf, fmt, ap);
	va_end(ap);
	if (n < 0 || (size_t)n >= sizeof buf) {
		t->over = 1;
		return;
	}
	put(t, buf, (size_t)n);
}

static void fail(char *err, size_t size, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(err, size, fmt, ap);
	va_end(ap);
}

/* s past the word w and one space, or NULL when s does not start so. */
static const char *word(const char *s, const char *w)
{
	size_t n = strlen(w);
	return strncmp(s, w, n) == 0 && s[n] == ' ' ? s + n + 1 : NULL;
}

static int ident_char(char c) { return c == '_' || isalnum((unsigned char)c); }

/* The identifier at s into name; s past it. */
static const char *ident(const char *s, char *name)
{
	size_t n = 0;
	while (ident_char(*s) && n + 1 < NAME_BYTES) name[n++] = *s++;
	name[n] = 0;
	return s;
}

static const char *precision(const char *s)
{
	const char *r;
	if ((r = word(s, "highp")) || (r = word(s, "mediump")) || (r = word(s, "lowp"))) return r;
	return s;
}

/* A declaration `TYPE NAME;` at s (after its qualifiers): 0, else -1. */
static int type_and_name(const char *s, char *type, char *name)
{
	s = ident(precision(s), type);
	if (!type[0] || *s != ' ') return -1;
	while (*s == ' ') s++;
	s = ident(s, name);
	while (*s == ' ') s++;
	return name[0] && *s == ';' ? 0 : -1;
}

/* A uniform's declaration: 1 with its type and name, 0 not one, -1 malformed. */
static int uniform_line(const char *line, char *type, char *name)
{
	const char *rest = word(line, "uniform");
	if (!rest) return 0;
	return type_and_name(rest, type, name) == 0 ? 1 : -1;
}

/* A varying's declaration ([flat] in/out ...): 'i' or 'o' with its name, 0 not one, -1 malformed. */
static int varying_line(const char *line, char *name)
{
	char type[NAME_BYTES];
	const char *rest = line, *r;
	int dir;
	if ((r = word(rest, "flat"))) rest = r;
	if ((r = word(rest, "out"))) dir = 'o';
	else if ((r = word(rest, "in"))) dir = 'i';
	else return 0;
	return type_and_name(r, type, name) == 0 ? dir : -1;
}

static int find_name(const char *const *names, int count, const char *name)
{
	int k;
	for (k = 0; k < count; k++) {
		if (strcmp(names[k], name) == 0) return k;
	}
	return -1;
}

/* The next line of *p into line (no newline): 1, 0 at the end, -1 too long. */
static int next_line(const char **p, char *line)
{
	const char *s = *p, *e;
	size_t n;
	if (!*s) return 0;
	e = strchr(s, '\n');
	n = e ? (size_t)(e - s) : strlen(s);
	if (n + 1 > LINE_BYTES) return -1;
	memcpy(line, s, n);
	line[n] = 0;
	*p = e ? e + 1 : s + n;
	return 1;
}

/* The vertex stage's outputs in order. */
static int vs_outputs(const char *vs, char names[VARYINGS_MAX][NAME_BYTES], char *err, size_t err_size)
{
	char line[LINE_BYTES], name[NAME_BYTES];
	int n = 0, r;
	while ((r = next_line(&vs, line)) > 0) {
		int dir = varying_line(line, name);
		if (dir == -1) {
			fail(err, err_size, "a varying not read: %s", line);
			return -1;
		}
		if (dir != 'o') continue;
		if (n == VARYINGS_MAX) {
			fail(err, err_size, "more than %d varyings", VARYINGS_MAX);
			return -1;
		}
		snprintf(names[n++], NAME_BYTES, "%s", name);
	}
	if (r < 0) {
		fail(err, err_size, "a line past %d bytes", LINE_BYTES);
		return -1;
	}
	return n;
}

/* line with every gl_VertexID token as gl_VertexIndex. */
static void vertex_index(Text *t, const char *line)
{
	static const char ID[] = "gl_VertexID";
	const char *s = line, *at;
	while ((at = strstr(s, ID)) != NULL) {
		size_t n = sizeof ID - 1;
		put(t, s, (size_t)(at - s));
		if ((at > line && ident_char(at[-1])) || ident_char(at[n])) put(t, at, n);
		else put_str(t, "gl_VertexIndex");
		s = at + n;
	}
	put_str(t, s);
}

static int has_token(const char *line, const char *token)
{
	size_t n = strlen(token);
	const char *s = line, *at;
	while ((at = strstr(s, token)) != NULL) {
		if (!(at > line && ident_char(at[-1])) && !ident_char(at[n])) return 1;
		s = at + n;
	}
	return 0;
}

int kp_gfx_gpu_glsl(const char *body, const char *vs, int vertex, char *out, size_t size, KpGfxGpuStage *stage,
	char *err, size_t err_size)
{
	char line[LINE_BYTES], type[NAME_BYTES], name[NAME_BYTES], outputs[VARYINGS_MAX][NAME_BYTES];
	int used[KP_UNIFORMS] = { 0 };
	int set = vertex ? 0 : 2, outs, k, r, samplers = 0, storage = 0, any = 0, in_main = 0, depth = 0, ended = 0;
	const char *p;
	Text t = { out, size, 0, 0 };
	memset(stage, 0, sizeof *stage);
	if (size) out[0] = 0;
	outs = vs_outputs(vertex ? body : vs, outputs, err, err_size);
	if (outs < 0) return -1;
	/* the declarations first: the samplers' count places the storage textures, the constants make the block */
	for (p = body; (r = next_line(&p, line)) > 0;) {
		int u = uniform_line(line, type, name);
		if (u == 0) continue;
		if (u < 0) {
			fail(err, err_size, "a uniform not read: %s", line);
			return -1;
		}
		if (strcmp(type, "vec4") == 0) {
			k = find_name(KP_GFX_UNIFORM_NAMES, KP_UNIFORMS, name);
			if (k < 0) {
				fail(err, err_size, "constant %s not in KP_GFX_UNIFORM_NAMES", name);
				return -1;
			}
			used[k] = any = 1;
		} else if (strcmp(type, "sampler2D") == 0 || strcmp(type, "sampler2DArray") == 0) {
			samplers++;
		} else if (strcmp(type, "usampler2D") == 0) {
			storage++;
		} else {
			fail(err, err_size, "uniform %s of type %s", name, type);
			return -1;
		}
	}
	if (r < 0) {
		fail(err, err_size, "a line past %d bytes", LINE_BYTES);
		return -1;
	}
	if (samplers > KP_GPU_STAGE_SAMPLERS || storage > KP_GPU_STAGE_STORAGE) {
		fail(err, err_size, "%d samplers and %d integer textures in a stage", samplers, storage);
		return -1;
	}
	put_str(&t, "#version 450\n");
	if (storage) put_str(&t, "#extension GL_EXT_samplerless_texture_functions : require\n");
	if (any || vertex) { /* a vertex stage's block holds its viewport's mapping at least */
		/* named by the stage: a link (glslang's, GL's rules) wants one name's blocks alike in both stages */
		put_fmt(&t, "layout(std140, set = %d, binding = 0) uniform kp_%s_constants\n{\n", set + 1,
			vertex ? "vertex" : "fragment");
		for (k = 0; k < KP_UNIFORMS; k++) {
			if (used[k]) put_fmt(&t, "    layout(offset = %d) vec4 %s;\n", 16 * k, KP_GFX_UNIFORM_NAMES[k]);
		}
		if (vertex) put_fmt(&t, "    layout(offset = %d) vec4 kp_viewport;\n", KP_GPU_VIEWPORT_OFFSET);
		put_str(&t, "};\n");
	}
	stage->uniforms = (uint8_t)(any || vertex);
	for (p = body; next_line(&p, line) > 0;) {
		int u = uniform_line(line, type, name), dir;
		if (u > 0) {
			if (strcmp(type, "vec4") == 0) continue; /* in the block */
			k = find_name(KP_GFX_SAMPLER_NAMES, KP_SAMPLERS, name);
			if (k < 0) {
				fail(err, err_size, "sampler %s not in KP_GFX_SAMPLER_NAMES", name);
				return -1;
			}
			if (strcmp(type, "usampler2D") == 0) {
				put_fmt(&t, "layout(set = %d, binding = %d) uniform utexture2D %s;\n", set, samplers + stage->storage,
					name);
				stage->storage_of[stage->storage++] = (uint8_t)k;
			} else {
				put_fmt(&t, "layout(set = %d, binding = %d) uniform highp %s %s;\n", set, stage->samplers, type, name);
				stage->kind[stage->samplers] = (uint8_t)(strcmp(type, "sampler2DArray") == 0 ? KP_GPU_KIND_ARRAY
					: KP_GPU_KIND_2D);
				stage->sampler[stage->samplers++] = (uint8_t)k;
			}
			continue;
		}
		dir = varying_line(line, name);
		if (dir < 0) {
			fail(err, err_size, "a varying not read: %s", line);
			return -1;
		}
		if (dir == 'o' && !vertex) {
			if (strcmp(name, "out_fragColor") != 0) {
				fail(err, err_size, "fragment output %s", name);
				return -1;
			}
			put_fmt(&t, "layout(location = 0) %s\n", line);
			continue;
		}
		if (dir) {
			for (k = 0; k < outs && strcmp(outputs[k], name) != 0; k++) {}
			if (k == outs || (dir == 'i' && vertex)) {
				fail(err, err_size, "varying %s has no vertex output", name);
				return -1;
			}
			put_fmt(&t, "layout(location = %d) %s\n", k, line);
			continue;
		}
		if (!vertex) {
			put_str(&t, line);
			put_str(&t, "\n");
			continue;
		}
		if (strcmp(line, "void main()") == 0) {
			in_main = 1;
			depth = 0;
		} else if (in_main) {
			const char *c;
			if (has_token(line, "return")) {
				fail(err, err_size, "a vertex stage's main returns early: %s", line);
				return -1;
			}
			for (c = line; *c; c++) depth += *c == '{' ? 1 : *c == '}' ? -1 : 0;
			if (depth == 0) { /* main's closing brace */
				if (strcmp(line, "}") != 0) {
					fail(err, err_size, "a vertex stage's main does not end on a line of its own");
					return -1;
				}
				put_str(&t, "    gl_Position.xy = gl_Position.xy * kp_viewport.xy + kp_viewport.zw * gl_Position.w;\n");
				put_str(&t, "    gl_Position.y = -gl_Position.y;\n");
				in_main = 0;
				ended = 1;
			}
		}
		vertex_index(&t, line);
		put_str(&t, "\n");
	}
	if (t.over) {
		fail(err, err_size, "the text past %lu bytes", (unsigned long)size);
		return -1;
	}
	if (vertex && !ended) { /* else its frames would come out unmapped and upside down */
		fail(err, err_size, "a vertex stage without `void main()` on a line of its own and its `}` on another");
		return -1;
	}
	return (int)t.len;
}

/* --- the pipelines --- */

const KpGfxGpuPipeline KP_GFX_GPU_PIPELINES[] = {
	/* the index world into the index target */
	{ KP_PROG_TERRAIN, KP_GPU_TARGET_INDEX, KP_GPU_BLEND_OFF, KP_GPU_DEPTH_OFF },
	{ KP_PROG_IMAGES, KP_GPU_TARGET_INDEX, KP_GPU_BLEND_OFF, KP_GPU_DEPTH_WRITE },
	{ KP_PROG_DEST, KP_GPU_TARGET_INDEX, KP_GPU_BLEND_OFF, KP_GPU_DEPTH_TEST },
	{ KP_PROG_DISTORT, KP_GPU_TARGET_INDEX, KP_GPU_BLEND_OFF, KP_GPU_DEPTH_WRITE },
	{ KP_PROG_FOG, KP_GPU_TARGET_INDEX, KP_GPU_BLEND_OFF, KP_GPU_DEPTH_OFF },
	{ KP_PROG_UI, KP_GPU_TARGET_INDEX, KP_GPU_BLEND_OFF, KP_GPU_DEPTH_OFF },
	/* the palette pass into the window or the RGB target, the sharp upscale, the vector text over them */
	{ KP_PROG_PALETTE, KP_GPU_TARGET_RGBA, KP_GPU_BLEND_OFF, KP_GPU_DEPTH_OFF },
	{ KP_PROG_SHARP, KP_GPU_TARGET_RGBA, KP_GPU_BLEND_OFF, KP_GPU_DEPTH_OFF },
	{ KP_PROG_TEXT, KP_GPU_TARGET_RGBA, KP_GPU_BLEND_ALPHA, KP_GPU_DEPTH_OFF },
	/* the HD world into the HD target: only the overwriting family writes its rank */
	{ KP_PROG_HD_TERRAIN, KP_GPU_TARGET_HD, KP_GPU_BLEND_OFF, KP_GPU_DEPTH_OFF },
	{ KP_PROG_HD_IMAGES, KP_GPU_TARGET_HD, KP_GPU_BLEND_PREMULTIPLIED, KP_GPU_DEPTH_WRITE },
	{ KP_PROG_HD_IMAGES, KP_GPU_TARGET_HD, KP_GPU_BLEND_PREMULTIPLIED, KP_GPU_DEPTH_TEST },
	{ KP_PROG_HD_FOG, KP_GPU_TARGET_HD, KP_GPU_BLEND_PREMULTIPLIED, KP_GPU_DEPTH_OFF },
	/* the HD composite into the window, the cursor's HD frame blended over it; the movies */
	{ KP_PROG_HD_COMPOSITE, KP_GPU_TARGET_RGBA, KP_GPU_BLEND_OFF, KP_GPU_DEPTH_OFF },
	{ KP_PROG_HD_COMPOSITE, KP_GPU_TARGET_RGBA, KP_GPU_BLEND_ALPHA, KP_GPU_DEPTH_OFF },
	{ KP_PROG_VIDEO, KP_GPU_TARGET_RGBA, KP_GPU_BLEND_OFF, KP_GPU_DEPTH_OFF },
};
const int KP_GFX_GPU_PIPELINE_COUNT = (int)(sizeof KP_GFX_GPU_PIPELINES / sizeof KP_GFX_GPU_PIPELINES[0]);

int kp_gfx_gpu_target_kind(int target)
{
	return target == KP_TARGET_INDEX || target == KP_TARGET_DEST ? KP_GPU_TARGET_INDEX
		: target == KP_TARGET_HD ? KP_GPU_TARGET_HD : KP_GPU_TARGET_RGBA;
}

int kp_gfx_gpu_depth(int depth, int write)
{
	return depth == KP_DEPTH_OFF ? KP_GPU_DEPTH_OFF : write ? KP_GPU_DEPTH_WRITE : KP_GPU_DEPTH_TEST;
}

int kp_gfx_gpu_pipeline(int prog, int target_kind, int blend, int depth)
{
	int k;
	for (k = 0; k < KP_GFX_GPU_PIPELINE_COUNT; k++) {
		const KpGfxGpuPipeline *p = &KP_GFX_GPU_PIPELINES[k];
		if (p->prog == prog && p->target == target_kind && p->blend == blend && p->depth == depth) return k;
	}
	return -1;
}
