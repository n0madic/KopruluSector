/*
 * kp_gfx's backend over GLES 3.0 / WebGL2 / desktop GL core (kp_gfx_internal.h). The world images need no vertex
 * buffer: a draw of six vertices per slot reads gl_VertexID; an empty vertex array is bound for the core profiles.
 */
#include "kp_gfx_internal.h"
#include "kp_gl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const UNIFORM_NAMES[KP_UNIFORMS] = { "terrain", "terrain_size", "view", "interp", "window",
	"slot_base", "lookups", "fog", "fog_origin", "cursor", "blend", "source_size", "hd" };
static const char *const SAMPLER_NAMES[KP_SAMPLERS] = { "terrain_map", "terrain_sheet", "image_data", "frame_table",
	"atlas", "remap_table", "bars", "dest", "tables", "fog_levels", "fog_dark", "ui_canvas", "ui_blend", "ui_cursor",
	"index_target", "rgb_target", "palette", "text_atlas", "text_quads", "hd_diffuse", "hd_team", "hd_frames",
	"hd_sheet", "hd_map", "hd_colors", "hd_target", "hd_slots" };

typedef struct {
	GLuint id;
	GLenum target;
	int format;
	int32_t w, h, layers;
} Texture;

typedef struct {
	GLuint fbo, color, depth;
} Target;

struct KpGfxBackend {
	KpGfxApi api;
	GLuint vao, window_fbo, window_rb;
	GLuint programs[KP_PROGRAMS];
	GLint uniforms[KP_PROGRAMS][KP_UNIFORMS];
	Texture textures[KP_TEXTURES];
	Target targets[KP_TARGETS];
	int32_t target_w, target_h;
	int32_t hd_w, hd_h; /* the HD target's size (0: none) */
	int compressed;     /* S3TC: 0 untried, 1 yes, -1 no */
	GLuint current; /* the program in use */
};

static const struct {
	GLint internal;
	GLenum format, type;
	int bytes;
} FORMATS[KP_FORMATS] = { { GL_R8, GL_RED, GL_UNSIGNED_BYTE, 1 }, { GL_RG8, GL_RG, GL_UNSIGNED_BYTE, 2 },
	{ GL_RGBA32F, GL_RGBA, GL_FLOAT, 16 }, { GL_R16UI, GL_RED_INTEGER, GL_UNSIGNED_SHORT, 2 },
	{ GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, 4 },
	/* the block formats: bytes per 4 x 4 block */
	{ GL_COMPRESSED_RGBA_S3TC_DXT1_EXT, 0, 0, 8 }, { GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, 0, 0, 16 } };

static int is_blocks(int format) { return format == KP_FMT_BC1 || format == KP_FMT_BC3; }

static size_t blocks_size(int format, int32_t w, int32_t h)
{
	return (size_t)((w + 3) / 4) * (size_t)((h + 3) / 4) * (size_t)FORMATS[format].bytes;
}

static GLuint compile(const KpGfxApi *api, GLenum type, const char *body, char *err, size_t err_size)
{
	static const char ES[] = "#version 300 es\nprecision highp float;\nprecision highp int;\n"
		"precision highp sampler2D;\nprecision highp sampler2DArray;\nprecision highp usampler2D;\n";
	char header[64];
	const char *src[2];
	GLuint s = glCreateShader(type);
	GLint ok = 0;
	if (api->gles) {
		src[0] = ES;
	} else {
		snprintf(header, sizeof header, "#version %d core\n", api->glsl_version);
		src[0] = header;
	}
	src[1] = body;
	glShaderSource(s, 2, src, NULL);
	glCompileShader(s);
	glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		char log[1024];
		GLsizei n = 0;
		glGetShaderInfoLog(s, (GLsizei)sizeof log, &n, log);
		snprintf(err, err_size, "gfx: a %s shader does not compile: %.*s", type == GL_VERTEX_SHADER ? "vertex" : "fragment",
			(int)n, log);
		glDeleteShader(s);
		return 0;
	}
	return s;
}

static int program(KpGfxBackend *b, int prog, const char *vs, const char *fs, char *err, size_t err_size)
{
	GLuint v = compile(&b->api, GL_VERTEX_SHADER, vs, err, err_size), f, p;
	GLint ok = 0;
	int k;
	if (!v) return -1;
	f = compile(&b->api, GL_FRAGMENT_SHADER, fs, err, err_size);
	if (!f) {
		glDeleteShader(v);
		return -1;
	}
	p = glCreateProgram();
	glAttachShader(p, v);
	glAttachShader(p, f);
	glLinkProgram(p);
	glDeleteShader(v);
	glDeleteShader(f);
	glGetProgramiv(p, GL_LINK_STATUS, &ok);
	if (!ok) {
		char log[1024];
		GLsizei n = 0;
		glGetProgramInfoLog(p, (GLsizei)sizeof log, &n, log);
		snprintf(err, err_size, "gfx: program %d does not link: %.*s", prog, (int)n, log);
		glDeleteProgram(p);
		return -1;
	}
	b->programs[prog] = p;
	for (k = 0; k < KP_UNIFORMS; k++) b->uniforms[prog][k] = glGetUniformLocation(p, UNIFORM_NAMES[k]);
	/* each sampler reads the texture unit of its own number (kp_gb_draw binds it there) */
	glUseProgram(p);
	b->current = p;
	for (k = 0; k < KP_SAMPLERS; k++) {
		GLint at = glGetUniformLocation(p, SAMPLER_NAMES[k]);
		if (at >= 0) glUniform1i(at, k);
	}
	return 0;
}

KpGfxBackend *kp_gb_new(const KpGfxApi *api, char *err, size_t err_size)
{
	static const struct {
		int prog;
		const char *vs, *fs;
	} PROGRAMS[KP_PROGRAMS] = {
		{ KP_PROG_TERRAIN, KP_GFX_FULLSCREEN_VS, KP_GFX_TERRAIN_FS },
		{ KP_PROG_IMAGES, KP_GFX_WORLD_VS, KP_GFX_WORLD_FS },
		{ KP_PROG_DEST, KP_GFX_WORLD_VS, KP_GFX_DEST_FS },
		{ KP_PROG_DISTORT, KP_GFX_WORLD_VS, KP_GFX_DISTORT_FS },
		{ KP_PROG_FOG, KP_GFX_FULLSCREEN_VS, KP_GFX_FOG_FS },
		{ KP_PROG_UI, KP_GFX_FULLSCREEN_VS, KP_GFX_UI_FS },
		{ KP_PROG_PALETTE, KP_GFX_FULLSCREEN_VS, KP_GFX_PALETTE_FS },
		{ KP_PROG_SHARP, KP_GFX_FULLSCREEN_VS, KP_GFX_SHARP_FS },
		{ KP_PROG_TEXT, KP_GFX_TEXT_VS, KP_GFX_TEXT_FS },
		{ KP_PROG_HD_TERRAIN, KP_GFX_FULLSCREEN_VS, KP_GFX_HD_TERRAIN_FS },
		{ KP_PROG_HD_IMAGES, KP_GFX_HD_WORLD_VS, KP_GFX_HD_WORLD_FS },
		{ KP_PROG_HD_FOG, KP_GFX_FULLSCREEN_VS, KP_GFX_HD_FOG_FS },
		{ KP_PROG_HD_COMPOSITE, KP_GFX_FULLSCREEN_VS, KP_GFX_HD_COMPOSITE_FS },
	};
	KpGfxBackend *b;
	const char *missing = kp_gl_load();
	GLint fbo = 0;
	int k;
	if (missing) {
		snprintf(err, err_size, "gfx: no GL function %s", missing);
		return NULL;
	}
	b = (KpGfxBackend *)calloc(1, sizeof *b);
	if (!b) {
		snprintf(err, err_size, "gfx: out of memory");
		return NULL;
	}
	b->api = *api;
	/* the window's framebuffer and renderbuffer: 0, or the ones the platform made (UIKit's, whose swap presents the
	 * renderbuffer bound) */
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);
	b->window_fbo = (GLuint)fbo;
	glGetIntegerv(GL_RENDERBUFFER_BINDING, &fbo);
	b->window_rb = (GLuint)fbo;
	glGenVertexArrays(1, &b->vao);
	glBindVertexArray(b->vao);
	for (k = 0; k < KP_PROGRAMS; k++) {
		if (program(b, PROGRAMS[k].prog, PROGRAMS[k].vs, PROGRAMS[k].fs, err, err_size) != 0) {
			kp_gb_free(b, 0);
			return NULL;
		}
	}
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glDisable(GL_BLEND);
	glDisable(GL_CULL_FACE);
	glDisable(GL_DITHER);
	glDepthFunc(GL_GREATER);
	return b;
}

static void free_targets(KpGfxBackend *b)
{
	int k;
	for (k = 0; k < KP_TARGETS; k++) {
		Target *t = &b->targets[k];
		if (t->fbo) glDeleteFramebuffers(1, &t->fbo);
		if (t->color) glDeleteTextures(1, &t->color);
		if (t->depth) glDeleteRenderbuffers(1, &t->depth);
		memset(t, 0, sizeof *t);
	}
	b->target_w = b->target_h = 0;
	b->hd_w = b->hd_h = 0;
}

void kp_gb_free(KpGfxBackend *b, int lost)
{
	int k;
	if (!b) return;
	if (lost) { /* the objects went with the context */
		free(b);
		return;
	}
	free_targets(b);
	for (k = 0; k < KP_TEXTURES; k++) {
		if (b->textures[k].id) glDeleteTextures(1, &b->textures[k].id);
	}
	for (k = 0; k < KP_PROGRAMS; k++) {
		if (b->programs[k]) glDeleteProgram(b->programs[k]);
	}
	if (b->vao) glDeleteVertexArrays(1, &b->vao);
	free(b);
}

static void parameters(GLenum target, GLint filter)
{
	glTexParameteri(target, GL_TEXTURE_MIN_FILTER, filter);
	glTexParameteri(target, GL_TEXTURE_MAG_FILTER, filter);
	glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

int kp_gb_texture(KpGfxBackend *b, int tex, int format, int32_t w, int32_t h, int32_t layers)
{
	Texture *t = &b->textures[tex];
	if (t->id && t->format == format && t->w == w && t->h == h && t->layers == layers) return 0;
	if (t->id) glDeleteTextures(1, &t->id);
	memset(t, 0, sizeof *t);
	glGenTextures(1, &t->id);
	t->format = format;
	t->w = w;
	t->h = h;
	t->layers = layers;
	t->target = layers > 0 || is_blocks(format) ? GL_TEXTURE_2D_ARRAY : GL_TEXTURE_2D;
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(t->target, t->id);
	/* the HD art is sampled between its texels (it is drawn at the window's scale, not its own) */
	parameters(t->target, is_blocks(format) || tex == KP_TEX_HD_DIFFUSE || tex == KP_TEX_HD_TEAM ? GL_LINEAR : GL_NEAREST);
	if (is_blocks(format)) { /* texture arrays only; the contents follow by layer (kp_gb_upload_blocks) */
		size_t size = blocks_size(format, w, h) * (size_t)(layers > 0 ? layers : 1);
		glCompressedTexImage3D(GL_TEXTURE_2D_ARRAY, 0, (GLenum)FORMATS[format].internal, w, h, layers > 0 ? layers : 1, 0,
			(GLsizei)size, NULL);
	} else if (layers > 0) {
		glTexImage3D(t->target, 0, FORMATS[format].internal, w, h, layers, 0, FORMATS[format].format,
			FORMATS[format].type, NULL);
	} else {
		glTexImage2D(t->target, 0, FORMATS[format].internal, w, h, 0, FORMATS[format].format, FORMATS[format].type, NULL);
	}
	return 0;
}

int kp_gb_has_texture(const KpGfxBackend *b, int tex, int32_t *w, int32_t *h)
{
	const Texture *t = &b->textures[tex];
	if (w) *w = t->w;
	if (h) *h = t->h;
	return t->id != 0;
}

void kp_gb_upload(KpGfxBackend *b, int tex, int32_t layer, int32_t x, int32_t y, int32_t w, int32_t h, const void *data,
	size_t stride)
{
	Texture *t = &b->textures[tex];
	int bytes = FORMATS[t->format].bytes;
	if (!t->id || w <= 0 || h <= 0) return;
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(t->target, t->id);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, stride ? (GLint)(stride / (size_t)bytes) : 0);
	if (t->layers > 0) {
		glTexSubImage3D(t->target, 0, x, y, layer, w, h, 1, FORMATS[t->format].format, FORMATS[t->format].type, data);
	} else {
		glTexSubImage2D(t->target, 0, x, y, w, h, FORMATS[t->format].format, FORMATS[t->format].type, data);
	}
	glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
}

void kp_gb_upload_blocks(KpGfxBackend *b, int tex, int32_t layer, int32_t w, int32_t h, const void *data, size_t size)
{
	Texture *t = &b->textures[tex];
	if (!t->id || !is_blocks(t->format) || t->layers <= 0 || size != blocks_size(t->format, w, h)) return;
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(t->target, t->id);
	glCompressedTexSubImage3D(t->target, 0, 0, 0, layer, w, h, 1, (GLenum)FORMATS[t->format].internal, (GLsizei)size,
		data);
}

int kp_gb_compressed(KpGfxBackend *b)
{
	static const uint8_t block[8] = { 0 };
	GLuint id = 0;
	if (b->compressed) return b->compressed > 0;
	while (glGetError() != GL_NO_ERROR) {}
	glGenTextures(1, &id);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D_ARRAY, id);
	glCompressedTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_COMPRESSED_RGBA_S3TC_DXT1_EXT, 4, 4, 1, 0, sizeof block, block);
	b->compressed = glGetError() == GL_NO_ERROR ? 1 : -1;
	glDeleteTextures(1, &id);
	return b->compressed > 0;
}

static int make_target(Target *t, int32_t w, int32_t h, GLint filter, int depth, int index)
{
	glGenTextures(1, &t->color);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, t->color);
	parameters(GL_TEXTURE_2D, filter);
	if (index) glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, w, h, 0, GL_RED, GL_UNSIGNED_BYTE, NULL);
	else glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glGenFramebuffers(1, &t->fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, t->fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t->color, 0);
	if (depth) {
		glGenRenderbuffers(1, &t->depth);
		glBindRenderbuffer(GL_RENDERBUFFER, t->depth);
		glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, t->depth);
	}
	return glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE ? 0 : -1;
}

int kp_gb_targets(KpGfxBackend *b, int32_t w, int32_t h)
{
	int rc = 0;
	if (w < 1) w = 1;
	if (h < 1) h = 1;
	if (w == b->target_w && h == b->target_h) return 0;
	free_targets(b);
	rc |= make_target(&b->targets[KP_TARGET_INDEX], w, h, GL_NEAREST, 1, 1);
	rc |= make_target(&b->targets[KP_TARGET_DEST], w, h, GL_NEAREST, 0, 1);
	rc |= make_target(&b->targets[KP_TARGET_RGB], w, h, GL_LINEAR, 0, 0);
	glBindFramebuffer(GL_FRAMEBUFFER, b->window_fbo);
	glBindRenderbuffer(GL_RENDERBUFFER, b->window_rb); /* the depth buffers' binding would be presented on iOS */
	if (rc != 0) {
		free_targets(b); /* not kept: the next call tries again */
		return rc;
	}
	b->target_w = w;
	b->target_h = h;
	return 0;
}

int kp_gb_hd_target(KpGfxBackend *b, int32_t w, int32_t h)
{
	Target *t = &b->targets[KP_TARGET_HD];
	int rc;
	if (w < 1) w = 1;
	if (h < 1) h = 1;
	if (t->fbo && w == b->hd_w && h == b->hd_h) return 0;
	if (t->fbo) glDeleteFramebuffers(1, &t->fbo);
	if (t->color) glDeleteTextures(1, &t->color);
	if (t->depth) glDeleteRenderbuffers(1, &t->depth);
	memset(t, 0, sizeof *t);
	rc = make_target(t, w, h, GL_LINEAR, 1, 0);
	glBindFramebuffer(GL_FRAMEBUFFER, b->window_fbo);
	glBindRenderbuffer(GL_RENDERBUFFER, b->window_rb);
	if (rc != 0) {
		if (t->fbo) glDeleteFramebuffers(1, &t->fbo);
		if (t->color) glDeleteTextures(1, &t->color);
		if (t->depth) glDeleteRenderbuffers(1, &t->depth);
		memset(t, 0, sizeof *t);
		b->hd_w = b->hd_h = 0;
		return rc;
	}
	b->hd_w = w;
	b->hd_h = h;
	return 0;
}

void kp_gb_bind(KpGfxBackend *b, int target, int32_t vx, int32_t vy, int32_t vw, int32_t vh)
{
	glBindFramebuffer(GL_FRAMEBUFFER, target == KP_TARGET_WINDOW ? b->window_fbo : b->targets[target].fbo);
	glViewport(vx, vy, vw, vh);
}

void kp_gb_clear(KpGfxBackend *b, float r, float gg, float bb, float a, int depth)
{
	(void)b;
	glClearColor(r, gg, bb, a);
	glDepthMask(GL_TRUE);
#ifdef KP_GLES
	glClearDepthf(0.0f);
#else
	glClearDepth(0.0);
#endif
	glClear(GL_COLOR_BUFFER_BIT | (depth ? GL_DEPTH_BUFFER_BIT : 0));
}

static void use(KpGfxBackend *b, int prog)
{
	if (b->current == b->programs[prog]) return;
	glUseProgram(b->programs[prog]);
	b->current = b->programs[prog];
}

void kp_gb_uniform(KpGfxBackend *b, int prog, int u, float x, float y, float z, float w)
{
	GLint at = b->uniforms[prog][u];
	if (at < 0) return;
	use(b, prog);
	glUniform4f(at, x, y, z, w);
}

void kp_gb_draw(KpGfxBackend *b, int prog, int32_t vertices, const KpGfxBinding *bind, int count, int depth, int write)
{
	int k;
	use(b, prog);
	for (k = 0; k < count; k++) {
		glActiveTexture((GLenum)(GL_TEXTURE0 + bind[k].sampler));
		if (bind[k].texture >= KP_SAMPLE_TARGET) {
			glBindTexture(GL_TEXTURE_2D, b->targets[bind[k].texture - KP_SAMPLE_TARGET].color);
		} else {
			const Texture *t = &b->textures[bind[k].texture];
			glBindTexture(t->target, t->id);
		}
	}
	if (depth == KP_DEPTH_OFF) glDisable(GL_DEPTH_TEST);
	else glEnable(GL_DEPTH_TEST);
	glDepthMask(write ? GL_TRUE : GL_FALSE);
	glDrawArrays(GL_TRIANGLES, 0, vertices); /* the vertex array kp_gb_new bound */
	/* a target's colour left bound counts as a feedback loop on WebGL when that target is drawn next */
	for (k = 0; k < count; k++) {
		if (bind[k].texture < KP_SAMPLE_TARGET) continue;
		glActiveTexture((GLenum)(GL_TEXTURE0 + bind[k].sampler));
		glBindTexture(GL_TEXTURE_2D, 0);
	}
}

void kp_gb_blend(KpGfxBackend *b, int on)
{
	(void)b;
	if (on) {
		glEnable(GL_BLEND);
		/* the colour by coverage; the target's alpha stays (a window with alpha would show through the edges) */
		glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
	} else {
		glDisable(GL_BLEND);
	}
}

void kp_gb_blend_premultiplied(KpGfxBackend *b)
{
	(void)b;
	glEnable(GL_BLEND);
	glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
}

void kp_gb_copy(KpGfxBackend *b, int from, int to)
{
	glBindFramebuffer(GL_READ_FRAMEBUFFER, b->targets[from].fbo);
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, b->targets[to].fbo);
	glBlitFramebuffer(0, 0, b->target_w, b->target_h, 0, 0, b->target_w, b->target_h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
	glBindFramebuffer(GL_FRAMEBUFFER, b->window_fbo);
}

int kp_gb_read(KpGfxBackend *b, int target, int32_t x, int32_t y, int32_t w, int32_t h, uint8_t *out)
{
	while (glGetError() != GL_NO_ERROR) {} /* only the read's own error counts */
	glBindFramebuffer(GL_FRAMEBUFFER, target == KP_TARGET_WINDOW ? b->window_fbo : b->targets[target].fbo);
	glReadPixels(x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, out);
	glBindFramebuffer(GL_FRAMEBUFFER, b->window_fbo);
	return glGetError() == GL_NO_ERROR ? 0 : -1;
}

int kp_gb_alive(const KpGfxBackend *b) { return glIsProgram(b->programs[0]) == GL_TRUE; }

const char *kp_gfx_version(void)
{
	const GLubyte *v = glGetString(GL_VERSION);
	return v ? (const char *)v : "?";
}
