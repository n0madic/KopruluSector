// Spike 16: what the SDL3 runtime (phase S, ADR 0012) can rely on, on every platform.
//
// Each check prints "SPIKE <name> PASS|FAIL <details>"; the run ends with
// "SPIKE DONE pass=<n> fail=<m>". The GL checks render into off-screen targets and read
// them back, so they compare exact palette indices as the oracles do (docs/spikes/16-sdl3.md).
#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3_mixer/SDL_mixer.h>

#if defined(SDL_PLATFORM_IOS)
#define SPIKE_GLES 1
#define GLES_SILENCE_DEPRECATION 1
#include <OpenGLES/ES3/gl.h>
#elif defined(__EMSCRIPTEN__) || defined(SDL_PLATFORM_ANDROID)
#define SPIKE_GLES 1
#include <GLES3/gl3.h>
#elif defined(SDL_PLATFORM_MACOS)
#define GL_SILENCE_DEPRECATION 1
#include <OpenGL/gl3.h>
#else
// Linux's GL links its entry points; the spike was never built for Windows, whose loader over
// SDL_GL_GetProcAddress is the product's (native/kp_gfx/kp_gl.c, stage S4).
#define GL_GLEXT_PROTOTYPES 1
#include <GL/glcorearb.h>
#endif

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include "assets.h"

enum {
	TARGET_W = 640,
	TARGET_H = 480,
	QUADS = 64,
	BENCH_WARMUP = 30,
	BENCH_FRAMES = 300,
	BENCH_DRAWS = 4000,
	IMAGE_DATA_W = 256,
	IMAGE_DATA_H = 73, // the product's image_data texture: 9220 slots, 2 texels each
	AUDIO_TIMEOUT_MS = 8000,
};

typedef struct Stats {
	double samples[BENCH_FRAMES];
	int count;
} Stats;

typedef struct Spike {
	SDL_Window *window;
	SDL_GLContext gl;
	int pass, fail;
	// GL objects
	GLuint vao, prog_fetch, prog_quads, prog_palette, prog_bench;
	GLuint tex_r8, tex_data, tex_canvas, tex_palette, tex_target, rb_depth, fbo;
	float *image_data;
	unsigned char *canvas;
	// benchmark
	int frame;
	Stats upload, frame_time, draws;
	Uint64 last;
	// audio
	MIX_Mixer *mixer;
	MIX_Track *track;
	Uint64 audio_start;
	SDL_AtomicInt audio_stopped;
	double audio_expected_ms;
	bool audio_done;
	bool idbfs_done;
	bool keep;
} Spike;

static void report(Spike *s, const char *name, bool ok, const char *fmt, ...)
{
	char detail[512];
	va_list ap;
	va_start(ap, fmt);
	SDL_vsnprintf(detail, sizeof detail, fmt, ap);
	va_end(ap);
	SDL_Log("SPIKE %s %s %s", name, ok ? "PASS" : "FAIL", detail);
	if (ok) s->pass++; else s->fail++;
}

// ---- shaders ------------------------------------------------------------------------------

static const char *glsl_header(void)
{
#ifdef SPIKE_GLES
	return "#version 300 es\nprecision highp float;\nprecision highp int;\nprecision highp sampler2D;\n";
#elif defined(SDL_PLATFORM_MACOS)
	return "#version 410 core\n";
#else
	return "#version 330 core\n";
#endif
}

static GLuint compile(GLenum kind, const char *body)
{
	const char *src[2] = { glsl_header(), body };
	GLuint sh = glCreateShader(kind);
	glShaderSource(sh, 2, src, NULL);
	glCompileShader(sh);
	GLint ok = 0;
	glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		char log[1024];
		glGetShaderInfoLog(sh, sizeof log, NULL, log);
		SDL_Log("shader error: %s", log);
	}
	return sh;
}

static GLuint program(const char *vs, const char *fs)
{
	GLuint p = glCreateProgram();
	glAttachShader(p, compile(GL_VERTEX_SHADER, vs));
	glAttachShader(p, compile(GL_FRAGMENT_SHADER, fs));
	glLinkProgram(p);
	GLint ok = 0;
	glGetProgramiv(p, GL_LINK_STATUS, &ok);
	if (!ok) {
		char log[1024];
		glGetProgramInfoLog(p, sizeof log, NULL, log);
		SDL_Log("link error: %s", log);
	}
	return p;
}

// A full-target triangle from gl_VertexID; no vertex buffers anywhere in the spike.
static const char *VS_FULL =
	"void main() {\n"
	"  vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));\n"
	"  gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
	"}\n";

// Index texels copied 1:1 (texelFetch of an R8 texture into an R8 target).
static const char *FS_FETCH =
	"uniform sampler2D src;\n"
	"out vec4 frag;\n"
	"void main() {\n"
	"  ivec2 size = textureSize(src, 0);\n"
	"  ivec2 p = ivec2(gl_FragCoord.xy) % size;\n"
	"  frag = vec4(texelFetch(src, p, 0).r, 0.0, 0.0, 1.0);\n"
	"}\n";

// Quads placed by an RGBA32F data texture read in the vertex shader (the mesh_texture
// scheme of ADR 0002): texel 2q = (x, y, w, h), texel 2q+1 = (index, rank, 0, 0).
static const char *VS_QUADS =
	"uniform sampler2D data;\n"
	"uniform vec2 target;\n"
	"uniform float depth_scale;\n"
	"flat out float index;\n"
	"void main() {\n"
	"  int q = gl_VertexID / 6;\n"
	"  int c = gl_VertexID % 6;\n"
	"  int corner = c < 3 ? c : c - 2;\n"
	"  vec4 r = texelFetch(data, ivec2((2 * q) % 256, (2 * q) / 256), 0);\n"
	"  vec4 m = texelFetch(data, ivec2((2 * q + 1) % 256, (2 * q + 1) / 256), 0);\n"
	"  vec2 k = vec2(float(corner & 1), float(corner >> 1));\n"
	"  vec2 p = (r.xy + k * r.zw) / target * 2.0 - 1.0;\n"
	"  index = m.x;\n"
	"  gl_Position = vec4(p, m.y * depth_scale * 2.0 - 1.0, 1.0);\n"
	"}\n";

static const char *FS_QUADS =
	"flat in float index;\n"
	"out vec4 frag;\n"
	"void main() { frag = vec4(index / 255.0, 0.0, 0.0, 1.0); }\n";

// The palette pass: index target → RGB through a 256-entry uniform array.
static const char *FS_PALETTE =
	"uniform sampler2D src;\n"
	"uniform vec4 palette[256];\n"
	"out vec4 frag;\n"
	"void main() {\n"
	"  int i = int(texelFetch(src, ivec2(gl_FragCoord.xy), 0).r * 255.0 + 0.5);\n"
	"  frag = palette[i];\n"
	"}\n";

// One tiny quad per draw call, positioned by a uniform.
static const char *VS_BENCH =
	"uniform vec4 rect;\n"
	"void main() {\n"
	"  int c = gl_VertexID % 6;\n"
	"  int corner = c < 3 ? c : c - 2;\n"
	"  vec2 k = vec2(float(corner & 1), float(corner >> 1));\n"
	"  gl_Position = vec4(rect.xy + k * rect.zw, 0.0, 1.0);\n"
	"}\n";

static const char *FS_BENCH =
	"uniform float index;\n"
	"out vec4 frag;\n"
	"void main() { frag = vec4(index / 255.0, 0.0, 0.0, 1.0); }\n";

// ---- GL helpers ---------------------------------------------------------------------------

static GLuint texture(GLenum internal, int w, int h, GLenum format, GLenum type, const void *data)
{
	GLuint t;
	glGenTextures(1, &t);
	glBindTexture(GL_TEXTURE_2D, t);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexImage2D(GL_TEXTURE_2D, 0, (GLint)internal, w, h, 0, format, type, data);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	return t;
}

static void bind_sampler(GLuint prog, const char *name, int unit, GLuint tex)
{
	glActiveTexture(GL_TEXTURE0 + (GLenum)unit);
	glBindTexture(GL_TEXTURE_2D, tex);
	glUniform1i(glGetUniformLocation(prog, name), unit);
}

// Reads the R channel of the bound framebuffer (RGBA/UNSIGNED_BYTE is the read format
// every GLES3 implementation must take for a normalized colour buffer).
static void read_r8(unsigned char *out, int w, int h)
{
	unsigned char *rgba = SDL_malloc((size_t)w * (size_t)h * 4);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
	for (int i = 0; i < w * h; i++) out[i] = rgba[i * 4];
	SDL_free(rgba);
}

static double ms_since(Uint64 start)
{
	return (double)(SDL_GetPerformanceCounter() - start) * 1000.0 / (double)SDL_GetPerformanceFrequency();
}

// ---- the GL checks ------------------------------------------------------------------------

static void check_context(Spike *s)
{
	int depth_bits = 0;
	glBindFramebuffer(GL_FRAMEBUFFER, s->fbo);
	glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
		GL_FRAMEBUFFER_ATTACHMENT_DEPTH_SIZE, &depth_bits);
	GLint max_tex = 0, max_vtex = 0;
	glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_tex);
	glGetIntegerv(GL_MAX_VERTEX_TEXTURE_IMAGE_UNITS, &max_vtex);
	GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
	report(s, "context", status == GL_FRAMEBUFFER_COMPLETE && depth_bits >= 24 && max_vtex >= 4,
		"gl=\"%s\" renderer=\"%s\" glsl=\"%s\" r8_target=%s depth_bits=%d max_texture=%d vertex_units=%d",
		glGetString(GL_VERSION), glGetString(GL_RENDERER), glGetString(GL_SHADING_LANGUAGE_VERSION),
		status == GL_FRAMEBUFFER_COMPLETE ? "complete" : "incomplete", depth_bits, max_tex, max_vtex);
}

static void check_fetch(Spike *s)
{
	static unsigned char src[256 * 256];
	for (int y = 0; y < 256; y++)
		for (int x = 0; x < 256; x++) src[y * 256 + x] = (unsigned char)((x * 7 + y * 13) & 255);
	glBindTexture(GL_TEXTURE_2D, s->tex_r8);
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 256, GL_RED, GL_UNSIGNED_BYTE, src);

	glBindFramebuffer(GL_FRAMEBUFFER, s->fbo);
	glViewport(0, 0, TARGET_W, TARGET_H);
	glDisable(GL_DEPTH_TEST);
	glUseProgram(s->prog_fetch);
	bind_sampler(s->prog_fetch, "src", 0, s->tex_r8);
	glDrawArrays(GL_TRIANGLES, 0, 3);

	static unsigned char got[TARGET_W * TARGET_H];
	read_r8(got, TARGET_W, TARGET_H);
	int bad = 0;
	for (int y = 0; y < TARGET_H; y++)
		for (int x = 0; x < TARGET_W; x++)
			if (got[y * TARGET_W + x] != src[(y % 256) * 256 + x % 256]) bad++;
	report(s, "r8_texel_fetch", bad == 0, "mismatched=%d of %d", bad, TARGET_W * TARGET_H);
}

// Quads drawn in a shuffled order with depth GREATER: the highest rank must win at every
// pixel, with ranks 2^-20 apart (more than 16-bit depth can hold).
static void check_quads(Spike *s)
{
	float *data = SDL_calloc(256 * 2 * 4, sizeof(float));
	float rect[QUADS][4];
	int rank[QUADS];
	for (int q = 0; q < QUADS; q++) {
		rect[q][0] = (float)((q * 37) % 400);
		rect[q][1] = (float)((q * 53) % 300);
		rect[q][2] = (float)(60 + (q * 11) % 120);
		rect[q][3] = (float)(50 + (q * 17) % 110);
		rank[q] = (q * 29) % QUADS + 1; // distinct, shuffled against the draw order
		float *t = data + q * 8;
		t[0] = rect[q][0]; t[1] = rect[q][1]; t[2] = rect[q][2]; t[3] = rect[q][3];
		t[4] = (float)(q + 1); t[5] = (float)rank[q];
	}
	glBindTexture(GL_TEXTURE_2D, s->tex_data);
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 2, GL_RGBA, GL_FLOAT, data);
	SDL_free(data);

	glBindFramebuffer(GL_FRAMEBUFFER, s->fbo);
	glViewport(0, 0, TARGET_W, TARGET_H);
	glClearColor(0, 0, 0, 0);
	glClearDepthf(0.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_GREATER);
	glUseProgram(s->prog_quads);
	bind_sampler(s->prog_quads, "data", 0, s->tex_data);
	glUniform2f(glGetUniformLocation(s->prog_quads, "target"), TARGET_W, TARGET_H);
	// Ranks 1..64 at 2^-20 apart, offset to the middle of the range.
	glUniform1f(glGetUniformLocation(s->prog_quads, "depth_scale"), 1.0f / 1048576.0f);
	glDrawArrays(GL_TRIANGLES, 0, QUADS * 6);
	glDisable(GL_DEPTH_TEST);

	// Switch to another target and back: GL keeps the first target's contents.
	GLuint other = texture(GL_R8, 16, 16, GL_RED, GL_UNSIGNED_BYTE, NULL), fbo2;
	glGenFramebuffers(1, &fbo2);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo2);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, other, 0);
	glClear(GL_COLOR_BUFFER_BIT);
	glBindFramebuffer(GL_FRAMEBUFFER, s->fbo);

	static unsigned char got[TARGET_W * TARGET_H];
	read_r8(got, TARGET_W, TARGET_H);
	int bad = 0;
	for (int y = 0; y < TARGET_H; y++)
		for (int x = 0; x < TARGET_W; x++) {
			int best = 0, best_rank = 0;
			for (int q = 0; q < QUADS; q++)
				if (x >= rect[q][0] && x < rect[q][0] + rect[q][2] && y >= rect[q][1] &&
					y < rect[q][1] + rect[q][3] && rank[q] > best_rank) {
					best = q + 1;
					best_rank = rank[q];
				}
			if (got[y * TARGET_W + x] != best) bad++;
		}
	glDeleteFramebuffers(1, &fbo2);
	glDeleteTextures(1, &other);
	report(s, "vertex_fetch_depth24_keep", bad == 0, "quads=%d rank_step=2^-20 mismatched=%d", QUADS, bad);
}

static void check_palette(Spike *s)
{
	float palette[256 * 4];
	for (int i = 0; i < 256; i++) {
		palette[i * 4 + 0] = (float)((i * 3) & 255) / 255.0f;
		palette[i * 4 + 1] = (float)((i * 5 + 17) & 255) / 255.0f;
		palette[i * 4 + 2] = (float)(255 - i) / 255.0f;
		palette[i * 4 + 3] = 1.0f;
	}
	static unsigned char index[TARGET_W * TARGET_H];
	glBindFramebuffer(GL_FRAMEBUFFER, s->fbo);
	read_r8(index, TARGET_W, TARGET_H);

	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glViewport(0, 0, TARGET_W, TARGET_H);
	glUseProgram(s->prog_palette);
	bind_sampler(s->prog_palette, "src", 0, s->tex_target);
	glUniform4fv(glGetUniformLocation(s->prog_palette, "palette"), 256, palette);
	glDrawArrays(GL_TRIANGLES, 0, 3);

	unsigned char *rgba = SDL_malloc(TARGET_W * TARGET_H * 4);
	glReadPixels(0, 0, TARGET_W, TARGET_H, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
	int bad = 0;
	for (int i = 0; i < TARGET_W * TARGET_H; i++) {
		int k = index[i];
		if (rgba[i * 4] != ((k * 3) & 255) || rgba[i * 4 + 1] != ((k * 5 + 17) & 255) ||
			rgba[i * 4 + 2] != 255 - k)
			bad++;
	}
	SDL_free(rgba);
	report(s, "palette_uniform", bad == 0, "mismatched=%d", bad);
}

// ---- PNG ----------------------------------------------------------------------------------

static void check_png(Spike *s, const char *name, const unsigned char *png, size_t len)
{
	SDL_Surface *surf = SDL_LoadPNG_IO(SDL_IOFromConstMem(png, len), true);
	if (!surf) {
		report(s, name, false, "load failed: %s", SDL_GetError());
		return;
	}
	int bad = 0;
	bool indexed = surf->format == SDL_PIXELFORMAT_INDEX8;
	for (int y = 0; y < ASSET_PNG_H && indexed; y++)
		for (int x = 0; x < ASSET_PNG_W; x++)
			if (((const unsigned char *)surf->pixels)[y * surf->pitch + x] != asset_png_index(x, y)) bad++;
	report(s, name, indexed && bad == 0, "format=%s %dx%d mismatched=%d",
		SDL_GetPixelFormatName(surf->format), surf->w, surf->h, indexed ? bad : -1);
	SDL_DestroySurface(surf);
}

// ---- audio --------------------------------------------------------------------------------

static void SDLCALL audio_stopped(void *userdata, MIX_Track *track)
{
	(void)track;
	Spike *s = userdata;
	SDL_SetAtomicInt(&s->audio_stopped, 1);
}

static void start_audio(Spike *s)
{
	if (!MIX_Init()) {
		report(s, "audio", false, "MIX_Init: %s", SDL_GetError());
		s->audio_done = true;
		return;
	}
	s->mixer = MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, NULL);
	if (!s->mixer) {
		report(s, "audio", false, "no device: %s", SDL_GetError());
		s->audio_done = true;
		return;
	}
	MIX_Audio *audio = MIX_LoadAudio_IO(s->mixer, SDL_IOFromConstMem(asset_ogg, sizeof asset_ogg), true, true);
	if (!audio) {
		report(s, "audio", false, "ogg load: %s", SDL_GetError());
		s->audio_done = true;
		return;
	}
	s->audio_expected_ms = (double)MIX_AudioFramesToMS(audio, MIX_GetAudioDuration(audio));
	s->track = MIX_CreateTrack(s->mixer);
	MIX_SetTrackAudio(s->track, audio);
	// DirectSound's pan +2000 attenuates the left side by 20 dB (kp_ui_sound.c): an exact
	// per-channel gain, which Defold's linear pan could not give.
	MIX_StereoGains gains = { SDL_powf(10.0f, -2000.0f / 2000.0f), 1.0f };
	MIX_SetTrackStereo(s->track, &gains);
	MIX_SetTrackGain(s->track, SDL_powf(10.0f, -600.0f / 2000.0f));
	MIX_SetTrackStoppedCallback(s->track, audio_stopped, s);
	s->audio_start = SDL_GetPerformanceCounter();
	MIX_PlayTrack(s->track, 0);
}

static void poll_audio(Spike *s)
{
	if (s->audio_done) return;
	double elapsed = ms_since(s->audio_start);
	if (SDL_GetAtomicInt(&s->audio_stopped)) {
		s->audio_done = true;
		report(s, "audio_ogg_stereo", SDL_fabs(elapsed - s->audio_expected_ms) < 250.0,
			"expected_ms=%.0f stopped_after_ms=%.0f", s->audio_expected_ms, elapsed);
	} else if (elapsed > AUDIO_TIMEOUT_MS) {
		s->audio_done = true;
		report(s, "audio_ogg_stereo", false, "no stop callback after %.0f ms (autoplay blocked?)", elapsed);
	}
}

// ---- IDBFS (web) --------------------------------------------------------------------------

#ifdef __EMSCRIPTEN__
static Spike *idbfs_spike;

EMSCRIPTEN_KEEPALIVE void spike_idbfs_result(int ok, int previous)
{
	report(idbfs_spike, "idbfs", ok != 0, "synced=%d value_from_previous_load=%d", ok, previous);
	idbfs_spike->idbfs_done = true;
}

static void start_idbfs(Spike *s)
{
	idbfs_spike = s;
	EM_ASM({
		FS.mkdir('/save');
		FS.mount(IDBFS, {}, '/save');
		FS.syncfs(true, function(err) {
			var previous = -1;
			try { previous = parseInt(FS.readFile('/save/count.txt', { encoding: 'utf8' })); } catch (e) {}
			FS.writeFile('/save/count.txt', String(previous + 1));
			FS.syncfs(false, function(err2) { _spike_idbfs_result(err || err2 ? 0 : 1, previous); });
		});
	});
}
#endif

// ---- benchmark ----------------------------------------------------------------------------

static int cmp_double(const void *a, const void *b)
{
	double x = *(const double *)a, y = *(const double *)b;
	return x < y ? -1 : x > y;
}

static void summary(Spike *s, const char *name, Stats *st, double budget_ms)
{
	double sum = 0;
	for (int i = 0; i < st->count; i++) sum += st->samples[i];
	SDL_qsort(st->samples, (size_t)st->count, sizeof(double), cmp_double);
	double mean = sum / st->count, p95 = st->samples[st->count * 95 / 100], max = st->samples[st->count - 1];
	report(s, name, p95 <= budget_ms, "frames=%d mean=%.3f p95=%.3f max=%.3f ms (budget p95 %.1f)",
		st->count, mean, p95, max, budget_ms);
}

// One frame of the product's worst case: the image data (RGBA32F 256×73) and a canvas
// rectangle uploaded on the same frame, then 4000 draw calls (Defold allowed 1024 render
// commands per frame).
static void bench_frame(Spike *s)
{
	for (int i = 0; i < IMAGE_DATA_W * IMAGE_DATA_H * 4; i++) s->image_data[i] = (float)(s->frame + i);
	for (int i = 0; i < TARGET_W * 160; i++) s->canvas[i] = (unsigned char)(s->frame + i);

	Uint64 t0 = SDL_GetPerformanceCounter();
	glBindTexture(GL_TEXTURE_2D, s->tex_data);
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, IMAGE_DATA_W, IMAGE_DATA_H, GL_RGBA, GL_FLOAT, s->image_data);
	glBindTexture(GL_TEXTURE_2D, s->tex_canvas);
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, TARGET_W, 160, GL_RED, GL_UNSIGNED_BYTE, s->canvas);
	double upload = ms_since(t0);

	Uint64 t1 = SDL_GetPerformanceCounter();
	glBindFramebuffer(GL_FRAMEBUFFER, s->fbo);
	glViewport(0, 0, TARGET_W, TARGET_H);
	glUseProgram(s->prog_bench);
	GLint rect = glGetUniformLocation(s->prog_bench, "rect");
	GLint index = glGetUniformLocation(s->prog_bench, "index");
	for (int i = 0; i < BENCH_DRAWS; i++) {
		glUniform4f(rect, -1.0f + (float)(i % 80) * 0.025f, -1.0f + (float)(i / 80) * 0.04f, 0.02f, 0.03f);
		glUniform1f(index, (float)(i & 255));
		glDrawArrays(GL_TRIANGLES, 0, 6);
	}
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	double draws = ms_since(t1);

	if (s->frame >= BENCH_WARMUP && s->upload.count < BENCH_FRAMES) {
		s->upload.samples[s->upload.count++] = upload;
		s->draws.samples[s->draws.count++] = draws;
	}
}

// ---- app callbacks ------------------------------------------------------------------------

SDL_AppResult SDL_AppInit(void **appstate, int argc, char **argv)
{
	Spike *s = SDL_calloc(1, sizeof *s);
	*appstate = s;
	for (int i = 1; i < argc; i++)
		if (SDL_strcmp(argv[i], "--keep") == 0) s->keep = true;

	if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
		SDL_Log("SDL_Init: %s", SDL_GetError());
		return SDL_APP_FAILURE;
	}
#ifdef SPIKE_GLES
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#else
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
#endif
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
	s->window = SDL_CreateWindow("Koprulu SDL3 spike", 1280, 720,
		SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
	if (!s->window) {
		SDL_Log("window: %s", SDL_GetError());
		return SDL_APP_FAILURE;
	}
	s->gl = SDL_GL_CreateContext(s->window);
	if (!s->gl) {
		SDL_Log("context: %s", SDL_GetError());
		return SDL_APP_FAILURE;
	}
	SDL_GL_SetSwapInterval(0);
	SDL_Log("SPIKE platform %s sdl=%d.%d.%d mixer=%d", SDL_GetPlatform(), SDL_VERSIONNUM_MAJOR(SDL_GetVersion()),
		SDL_VERSIONNUM_MINOR(SDL_GetVersion()), SDL_VERSIONNUM_MICRO(SDL_GetVersion()), MIX_Version());

	glGenVertexArrays(1, &s->vao);
	glBindVertexArray(s->vao);
	s->prog_fetch = program(VS_FULL, FS_FETCH);
	s->prog_quads = program(VS_QUADS, FS_QUADS);
	s->prog_palette = program(VS_FULL, FS_PALETTE);
	s->prog_bench = program(VS_BENCH, FS_BENCH);
	s->tex_r8 = texture(GL_R8, 256, 256, GL_RED, GL_UNSIGNED_BYTE, NULL);
	s->tex_data = texture(GL_RGBA32F, IMAGE_DATA_W, IMAGE_DATA_H, GL_RGBA, GL_FLOAT, NULL);
	s->tex_canvas = texture(GL_R8, TARGET_W, TARGET_H, GL_RED, GL_UNSIGNED_BYTE, NULL);
	s->tex_target = texture(GL_R8, TARGET_W, TARGET_H, GL_RED, GL_UNSIGNED_BYTE, NULL);
	glGenRenderbuffers(1, &s->rb_depth);
	glBindRenderbuffer(GL_RENDERBUFFER, s->rb_depth);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, TARGET_W, TARGET_H);
	glGenFramebuffers(1, &s->fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, s->fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s->tex_target, 0);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, s->rb_depth);
	s->image_data = SDL_malloc(IMAGE_DATA_W * IMAGE_DATA_H * 4 * sizeof(float));
	s->canvas = SDL_malloc(TARGET_W * TARGET_H);

	check_context(s);
	check_fetch(s);
	check_quads(s);
	check_palette(s);
	report(s, "gl_errors", glGetError() == GL_NO_ERROR, "after the checks");
	check_png(s, "png_gray8", asset_png_gray, sizeof asset_png_gray);
	check_png(s, "png_palette8", asset_png_palette, sizeof asset_png_palette);
	start_audio(s);
#ifdef __EMSCRIPTEN__
	start_idbfs(s);
#else
	s->idbfs_done = true;
#endif
	s->last = SDL_GetPerformanceCounter();
	return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event)
{
	(void)appstate;
	if (event->type == SDL_EVENT_QUIT) return SDL_APP_SUCCESS;
	return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate)
{
	Spike *s = appstate;
	bool benching = s->upload.count < BENCH_FRAMES;
	if (benching) bench_frame(s);
	int w, h;
	SDL_GetWindowSizeInPixels(s->window, &w, &h);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);
	glViewport(0, 0, w, h);
	glClearColor(0.1f, 0.1f, 0.12f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	SDL_GL_SwapWindow(s->window);
	double frame = ms_since(s->last);
	s->last = SDL_GetPerformanceCounter();
	if (benching && s->frame >= BENCH_WARMUP && s->frame_time.count < BENCH_FRAMES)
		s->frame_time.samples[s->frame_time.count++] = frame;
	s->frame++;
	poll_audio(s);

	if (!benching && s->upload.count == BENCH_FRAMES && s->frame_time.count > 0) {
		summary(s, "bench_upload_tick_frame", &s->upload, 1.0);
		summary(s, "bench_4000_draws", &s->draws, 8.0);
		summary(s, "bench_frame_vsync_off", &s->frame_time, 16.6);
		s->frame_time.count = 0;
		s->upload.count = BENCH_FRAMES + 1;
	}
	if (s->upload.count > BENCH_FRAMES && s->audio_done && s->idbfs_done) {
		SDL_Log("SPIKE DONE pass=%d fail=%d", s->pass, s->fail);
		s->upload.count = BENCH_FRAMES + 2;
		if (!s->keep) return s->fail ? SDL_APP_FAILURE : SDL_APP_SUCCESS;
	}
	return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result)
{
	(void)result;
	Spike *s = appstate;
	if (s && s->mixer) MIX_DestroyMixer(s->mixer);
	MIX_Quit();
	if (s && s->gl) SDL_GL_DestroyContext(s->gl);
}
