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
	"slot_base", "lookups", "fog", "fog_origin", "cursor", "blend", "source_size", "hd", "water", "hd_cursor_rect",
	"hd_cursor_src", "hd_cursor_layer", "art", "ui_gamma", "video_yuv", "video_gb", "video_chroma", "hd_atlas",
	"light", "video_caption_rect" };
static const char *const SAMPLER_NAMES[KP_SAMPLERS] = { "terrain_map", "terrain_sheet", "image_data", "frame_table",
	"atlas", "remap_table", "bars", "dest", "tables", "fog_levels", "fog_dark", "ui_canvas", "ui_blend", "ui_cursor",
	"index_target", "rgb_target", "palette", "text_atlas", "text_quads", "hd_diffuse", "hd_team", "hd_frames",
	"hd_sheet", "hd_map", "hd_colors", "hd_target", "hd_slots", "hd_water", "hd_normal_a", "hd_normal_b", "hd_tone",
	"ui_ids", "ui_below", "ui_items", "ui_atlas", "hd_edges", "video_y", "video_cb", "video_cr", "hd_light", "hd_occlusion",
	"hd_glow", "video_caption", "glow_fit" };

/* The errors a drain reads at most: a driver with no current context (or a lost one) may never answer GL_NO_ERROR. */
#define KP_GL_ERRORS_MAX 32

typedef struct {
	GLuint id;
	GLenum target;
	int format;
	int32_t w, h, layers;
} Texture;

typedef struct {
	GLuint fbo, color, depth;
} Target;

/* The textures a sampler reads where its own is not made: one zero texel of each sampler type the shaders declare
 * (WebGL checks every sampler of a draw against its texture's type). */
enum { DUMMY_2D, DUMMY_ARRAY, DUMMY_UINT, DUMMIES };

struct KpGfxBackend {
	KpGfxApi api;
	GLuint vao, window_fbo, window_rb;
	GLuint programs[KP_PROGRAMS];
	GLint uniforms[KP_PROGRAMS][KP_UNIFORMS];
	GLint units[KP_PROGRAMS][KP_SAMPLERS]; /* a program's samplers' texture units, -1 the samplers it lacks */
	uint8_t dummy_of[KP_PROGRAMS][KP_SAMPLERS]; /* and the dummy of their type (DUMMY_*) */
	GLuint dummies[DUMMIES];
	Texture textures[KP_TEXTURES];
	Target targets[KP_TARGETS];
	int32_t target_w, target_h;
	int32_t hd_w, hd_h; /* the HD target's size (0: none) */
	int32_t hd_failed_w, hd_failed_h; /* the size the HD target could not be made at (not tried again) */
	int takes[KP_FORMATS]; /* a block format's textures: 0 untried, 1 yes, -1 no */
	GLuint current; /* the program in use */
	/* the textures that could not be made since kp_gb_take_failures, the last described; a texture failing again at
	 * the size it failed at last is not counted again (a caller trying each frame) */
	int32_t failures;
	char failure[160];
	int failed_tex;
	int32_t failed_w, failed_h, failed_layers;
};

/* The errors raised before a call are read, so that the call's own are the next. */
static void clear_errors(void)
{
	int k;
	for (k = 0; k < KP_GL_ERRORS_MAX && glGetError() != GL_NO_ERROR; k++) {}
}

/* The block formats' rows, in KP_HD_CODEC_* order, name only their GL format: their blocks are kp_present's. */
static const struct {
	GLint internal;
	GLenum format, type;
	int bytes; /* a texel's */
} FORMATS[KP_FORMATS] = { { GL_R8, GL_RED, GL_UNSIGNED_BYTE, 1 }, { GL_RG8, GL_RG, GL_UNSIGNED_BYTE, 2 },
	{ GL_RGBA32F, GL_RGBA, GL_FLOAT, 16 }, { GL_R16UI, GL_RED_INTEGER, GL_UNSIGNED_SHORT, 2 },
	{ GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, 4 }, { GL_RG16UI, GL_RG_INTEGER, GL_UNSIGNED_SHORT, 4 },
	{ GL_COMPRESSED_RGBA_S3TC_DXT1_EXT, 0, 0, 0 }, { GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, 0, 0, 0 },
	{ GL_COMPRESSED_R11_EAC, 0, 0, 0 }, { GL_COMPRESSED_RG11_EAC, 0, 0, 0 }, { GL_COMPRESSED_RGB8_ETC2, 0, 0, 0 },
	{ GL_COMPRESSED_RGBA8_ETC2_EAC, 0, 0, 0 }, { GL_COMPRESSED_RGBA_ASTC_4x4_KHR, 0, 0, 0 },
	{ GL_COMPRESSED_RGBA_ASTC_6x6_KHR, 0, 0, 0 }, { GL_COMPRESSED_RGBA_ASTC_8x8_KHR, 0, 0, 0 } };

static int is_blocks(int format) { return format >= KP_FMT_BLOCKS && format < KP_FORMATS; }

static void parameters(GLenum target, GLint filter, GLint wrap)
{
	glTexParameteri(target, GL_TEXTURE_MIN_FILTER, filter);
	glTexParameteri(target, GL_TEXTURE_MAG_FILTER, filter);
	glTexParameteri(target, GL_TEXTURE_WRAP_S, wrap);
	glTexParameteri(target, GL_TEXTURE_WRAP_T, wrap);
}

static size_t blocks_size(int format, int32_t w, int32_t h)
{
	int side, bytes;
	kp_hd_codec_block(format - KP_FMT_BLOCKS, &side, &bytes);
	return (size_t)((w + side - 1) / side) * (size_t)((h + side - 1) / side) * (size_t)bytes;
}

static GLuint compile(const KpGfxApi *api, GLenum type, const char *body, char *err, size_t err_size)
{
	char header[64];
	const char *src[2];
	GLuint s = glCreateShader(type);
	GLint ok = 0;
	if (api->gles) {
		src[0] = KP_GFX_ES_HEADER;
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

/* The dummy of each sampler of a program by its type (a 2D array, an unsigned integer 2D texture, else a 2D one). */
static void sampler_types(KpGfxBackend *b, int prog)
{
	GLint count = 0, i;
	int k;
	glGetProgramiv(b->programs[prog], GL_ACTIVE_UNIFORMS, &count);
	for (i = 0; i < count; i++) {
		char name[64];
		GLsizei n = 0;
		GLint size = 0;
		GLenum type = 0;
		glGetActiveUniform(b->programs[prog], (GLuint)i, (GLsizei)sizeof name, &n, &size, &type, name);
		for (k = 0; k < KP_SAMPLERS; k++) {
			if (strcmp(name, SAMPLER_NAMES[k]) != 0) continue;
			b->dummy_of[prog][k] = type == GL_SAMPLER_2D_ARRAY ? DUMMY_ARRAY
				: type == GL_UNSIGNED_INT_SAMPLER_2D ? DUMMY_UINT : DUMMY_2D;
		}
	}
}

/* The dummies: one zero texel each. */
static void make_dummies(KpGfxBackend *b)
{
	static const uint8_t zero[8] = { 0 };
	glGenTextures(DUMMIES, b->dummies);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, b->dummies[DUMMY_2D]);
	parameters(GL_TEXTURE_2D, GL_NEAREST, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, zero);
	glBindTexture(GL_TEXTURE_2D_ARRAY, b->dummies[DUMMY_ARRAY]);
	parameters(GL_TEXTURE_2D_ARRAY, GL_NEAREST, GL_CLAMP_TO_EDGE);
	glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, 1, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, zero);
	glBindTexture(GL_TEXTURE_2D, b->dummies[DUMMY_UINT]);
	parameters(GL_TEXTURE_2D, GL_NEAREST, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16UI, 1, 1, 0, GL_RGBA_INTEGER, GL_UNSIGNED_SHORT, zero);
}

static int program(KpGfxBackend *b, int prog, const char *vs, const char *fs, char *err, size_t err_size)
{
	GLuint v = compile(&b->api, GL_VERTEX_SHADER, vs, err, err_size), f, p;
	GLint ok = 0;
	int k, unit;
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
	sampler_types(b, prog);
	/* the program's samplers read units 0, 1, ... in the order of their numbers (kp_gb_draw binds them there; WebGL 2
	 * has 32 units in all, fewer than the samplers, and 16 a fragment shader) */
	glUseProgram(p);
	b->current = p;
	for (k = 0, unit = 0; k < KP_SAMPLERS; k++) {
		GLint at = glGetUniformLocation(p, SAMPLER_NAMES[k]);
		b->units[prog][k] = at >= 0 ? unit : -1;
		if (at >= 0) glUniform1i(at, unit++);
	}
	return 0;
}

KpGfxBackend *kp_gb_new(const KpGfxApi *api, char *err, size_t err_size)
{
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
	b->failed_tex = -1;
	/* the window's framebuffer and renderbuffer: 0, or the ones the platform made (UIKit's, whose swap presents the
	 * renderbuffer bound) */
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);
	b->window_fbo = (GLuint)fbo;
	glGetIntegerv(GL_RENDERBUFFER_BINDING, &fbo);
	b->window_rb = (GLuint)fbo;
	glGenVertexArrays(1, &b->vao);
	glBindVertexArray(b->vao);
	for (k = 0; k < KP_PROGRAMS; k++) {
		if (program(b, KP_GFX_PROGRAMS[k].prog, KP_GFX_PROGRAMS[k].vs, KP_GFX_PROGRAMS[k].fs, err, err_size) != 0) {
			kp_gb_free(b, 0);
			return NULL;
		}
	}
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	make_dummies(b);
	glDisable(GL_BLEND);
	glDisable(GL_CULL_FACE);
	glDisable(GL_DITHER);
	glDepthFunc(GL_GREATER);
	return b;
}

static void free_target(Target *t)
{
	if (t->fbo) glDeleteFramebuffers(1, &t->fbo);
	if (t->color) glDeleteTextures(1, &t->color);
	if (t->depth) glDeleteRenderbuffers(1, &t->depth);
	memset(t, 0, sizeof *t);
}

static void free_targets(KpGfxBackend *b)
{
	int k;
	for (k = 0; k < KP_TARGETS; k++) free_target(&b->targets[k]);
	b->target_w = b->target_h = 0;
	b->hd_w = b->hd_h = 0;
	b->hd_failed_w = b->hd_failed_h = 0; /* the memory the targets held is back: one more try */
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
	if (b->dummies[0]) glDeleteTextures(DUMMIES, b->dummies);
	if (b->vao) glDeleteVertexArrays(1, &b->vao);
	free(b);
}

int kp_gb_texture(KpGfxBackend *b, int tex, int format, int32_t w, int32_t h, int32_t layers)
{
	Texture *t = &b->textures[tex];
	if (t->id && t->format == format && t->w == w && t->h == h && t->layers == layers) return 0;
	kp_gb_drop(b, tex);
	clear_errors(); /* only the texture's own error counts */
	glGenTextures(1, &t->id);
	t->format = format;
	t->w = w;
	t->h = h;
	t->layers = layers;
	t->target = layers > 0 || is_blocks(format) ? GL_TEXTURE_2D_ARRAY : GL_TEXTURE_2D;
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(t->target, t->id);
	/* the HD art is sampled between its texels (it is drawn at the window's scale, not its own); the water's normal
	 * maps repeat across the map */
	parameters(t->target,
		is_blocks(format) || tex == KP_TEX_HD_DIFFUSE || tex == KP_TEX_HD_TEAM || tex == KP_TEX_HD_NORMAL_A || tex == KP_TEX_UI_ATLAS
				|| tex == KP_TEX_VIDEO_Y || tex == KP_TEX_VIDEO_CB || tex == KP_TEX_VIDEO_CR || tex == KP_TEX_HD_NORMAL_B
				|| tex == KP_TEX_VIDEO_CAPTION ? GL_LINEAR : GL_NEAREST,
		tex == KP_TEX_HD_NORMAL_A || tex == KP_TEX_HD_NORMAL_B ? GL_REPEAT : GL_CLAMP_TO_EDGE);
#ifndef __EMSCRIPTEN__ /* WebGL has no swizzle: kp_gb_takes_blocks refuses RG11, the phones' pages draw decoded there */
	if (format == KP_FMT_EAC_RG11) glTexParameteri(t->target, GL_TEXTURE_SWIZZLE_A, GL_GREEN); /* y of the normal map */
	if (tex == KP_TEX_HD_OCCLUSION && format == KP_FMT_EAC_R11) glTexParameteri(t->target, GL_TEXTURE_SWIZZLE_G, GL_RED);
#endif
	if (is_blocks(format)) { /* texture arrays only; the contents follow by layer (kp_gb_upload_blocks) */
#ifdef KP_GLES
		/* WebGL's compressedTexImage3D takes no empty contents (Emscripten's would read size bytes of the heap from 0) */
		glTexStorage3D(GL_TEXTURE_2D_ARRAY, 1, (GLenum)FORMATS[format].internal, w, h, layers > 0 ? layers : 1);
#else /* GL 4.1 (macOS) has no texture storage */
		size_t size = blocks_size(format, w, h) * (size_t)(layers > 0 ? layers : 1);
		glCompressedTexImage3D(GL_TEXTURE_2D_ARRAY, 0, (GLenum)FORMATS[format].internal, w, h, layers > 0 ? layers : 1, 0,
			(GLsizei)size, NULL);
#endif
	} else if (layers > 0) {
		glTexImage3D(t->target, 0, FORMATS[format].internal, w, h, layers, 0, FORMATS[format].format,
			FORMATS[format].type, NULL);
	} else {
		glTexImage2D(t->target, 0, FORMATS[format].internal, w, h, 0, FORMATS[format].format, FORMATS[format].type, NULL);
	}
	if (glGetError() != GL_NO_ERROR) { /* out of memory, or a size past the context's */
		kp_gb_drop(b, tex);
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
	if (b->failed_tex == tex) b->failed_tex = -1; /* made again: its next failure is reported */
	return 0;
}

int kp_gb_take_failures(KpGfxBackend *b, char *msg, size_t size)
{
	int32_t n = b->failures;
	if (n > 0 && size > 0) snprintf(msg, size, "%s", b->failure);
	b->failures = 0;
	return (int)n;
}

void kp_gb_drop(KpGfxBackend *b, int tex)
{
	Texture *t = &b->textures[tex];
	if (t->id) glDeleteTextures(1, &t->id);
	memset(t, 0, sizeof *t);
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

int kp_gb_takes_blocks(KpGfxBackend *b, int format)
{
	static const uint8_t block[16] = { 0 };
	GLuint id = 0;
	int side, bytes;
	if (!is_blocks(format)) return 0;
#ifdef __EMSCRIPTEN__
	if (format == KP_FMT_EAC_RG11) return 0; /* the shader's normal y needs its swizzle (kp_gb_texture) */
#endif
	if (b->takes[format]) return b->takes[format] > 0;
	kp_hd_codec_block(format - KP_FMT_BLOCKS, &side, &bytes);
	clear_errors();
	glGenTextures(1, &id);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D_ARRAY, id);
	glCompressedTexImage3D(GL_TEXTURE_2D_ARRAY, 0, (GLenum)FORMATS[format].internal, 4, 4, 1, 0, bytes, block);
	b->takes[format] = glGetError() == GL_NO_ERROR ? 1 : -1;
	glDeleteTextures(1, &id);
	return b->takes[format] > 0;
}

static int make_target(Target *t, int32_t w, int32_t h, GLint filter, int depth, int index)
{
	glGenTextures(1, &t->color);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, t->color);
	parameters(GL_TEXTURE_2D, filter, GL_CLAMP_TO_EDGE);
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
	if (w == b->hd_failed_w && h == b->hd_failed_h) return -1; /* not made again every frame */
	free_target(t);
	rc = make_target(t, w, h, GL_LINEAR, 1, 0);
	glBindFramebuffer(GL_FRAMEBUFFER, b->window_fbo);
	glBindRenderbuffer(GL_RENDERBUFFER, b->window_rb);
	if (rc != 0) {
		free_target(t);
		b->hd_w = b->hd_h = 0;
		b->hd_failed_w = w;
		b->hd_failed_h = h;
		return rc;
	}
	b->hd_w = w;
	b->hd_h = h;
	b->hd_failed_w = b->hd_failed_h = 0;
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
		int s = bind[k].sampler;
		GLuint dummy = b->dummies[b->dummy_of[prog][s]];
		if (b->units[prog][s] < 0) continue; /* a sampler the program does not read */
		glActiveTexture((GLenum)(GL_TEXTURE0 + b->units[prog][s]));
		if (bind[k].texture >= KP_SAMPLE_TARGET) {
			GLuint color = b->targets[bind[k].texture - KP_SAMPLE_TARGET].color;
			glBindTexture(GL_TEXTURE_2D, color ? color : dummy);
		} else if (b->textures[bind[k].texture].id) {
			const Texture *t = &b->textures[bind[k].texture];
			glBindTexture(t->target, t->id);
		} else { /* not made: the dummy of the sampler's type */
			glBindTexture(b->dummy_of[prog][s] == DUMMY_ARRAY ? GL_TEXTURE_2D_ARRAY : GL_TEXTURE_2D, dummy);
		}
	}
	if (depth == KP_DEPTH_OFF) glDisable(GL_DEPTH_TEST);
	else glEnable(GL_DEPTH_TEST);
	glDepthMask(write ? GL_TRUE : GL_FALSE);
	glDrawArrays(GL_TRIANGLES, 0, vertices); /* the vertex array kp_gb_new bound */
	/* a target's colour left bound counts as a feedback loop on WebGL when that target is drawn next */
	for (k = 0; k < count; k++) {
		if (bind[k].texture < KP_SAMPLE_TARGET || b->units[prog][bind[k].sampler] < 0) continue;
		glActiveTexture((GLenum)(GL_TEXTURE0 + b->units[prog][bind[k].sampler]));
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
	if (target != KP_TARGET_WINDOW && !b->targets[target].fbo) return -1; /* not made: the window is no stand-in */
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
