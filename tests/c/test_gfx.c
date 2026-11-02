/*
 * kp_gfx without a GL context (plan phase S, ADR 0012): its programs' shaders compiled and linked by glslangValidator
 * as GLES 3.0 / WebGL2 and desktop GL core take them, and its passes (kp_gfx_passes.c) over a backend that records
 * the draws (kp_gfx_internal.h; kp_gfx_gl.c is the product's): the depth state each pass draws with, the HD
 * sprites' lighting layers and light.
 */
#define _POSIX_C_SOURCE 200809L /* popen, mkdtemp */
#define _DARWIN_C_SOURCE        /* and macOS's mkdtemp (unistd.h) */
#include "kp_test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "kp_gfx_internal.h"

/* --- the recording backend --- */

#define DRAWS_MAX 256

typedef struct {
	int prog, depth, write;
	float slot_base; /* KP_U_SLOT_BASE's x when it was drawn: the family's first slot */
	int below;       /* KP_S_UI_BELOW bound to KP_TEX_UI_BELOW */
	int lit;         /* KP_S_HD_LIGHT and KP_S_HD_GLOW bound to their textures */
} Draw;

struct KpGfxBackend {
	Draw draws[DRAWS_MAX];
	int count;
	float slot_base[KP_PROGRAMS];
	int made[KP_TEXTURES];
	int32_t w[KP_TEXTURES], h[KP_TEXTURES];
	int format[KP_TEXTURES], layers[KP_TEXTURES];
	uint32_t block_layers[KP_TEXTURES]; /* the layers (below 32) blocks went into */
	int blocks;                         /* kp_gb_takes_blocks: every block format */
	float light[4];                     /* KP_PROG_HD_IMAGES's KP_U_LIGHT */
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
	(void)b, (void)target, (void)vx, (void)vy, (void)vw, (void)vh;
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
	int k;
	(void)vertices;
	if (b->count == DRAWS_MAX) return;
	d = &b->draws[b->count++];
	d->prog = prog;
	d->depth = depth;
	d->write = write;
	d->slot_base = b->slot_base[prog];
	d->below = 0;
	d->lit = 0;
	for (k = 0; k < count; k++) {
		d->below |= bind[k].sampler == KP_S_UI_BELOW && bind[k].texture == KP_TEX_UI_BELOW;
		d->lit += (bind[k].sampler == KP_S_HD_LIGHT && bind[k].texture == KP_TEX_HD_LIGHT)
			+ (bind[k].sampler == KP_S_HD_GLOW && bind[k].texture == KP_TEX_HD_GLOW);
	}
	d->lit = d->lit == 2;
}

void kp_gb_blend(KpGfxBackend *b, int on) { (void)b, (void)on; }

void kp_gb_blend_premultiplied(KpGfxBackend *b) { (void)b; }

int kp_gb_takes_blocks(KpGfxBackend *b, int format)
{
	(void)format;
	return b->blocks;
}

void kp_gb_upload_blocks(KpGfxBackend *b, int tex, int32_t layer, int32_t w, int32_t h, const void *data, size_t size)
{
	(void)w, (void)h, (void)data, (void)size;
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

/* --- the shaders --- */

#define GLSLANG "glslangValidator"
#define GLSLANG_REQUIRED "KP_TEST_GLSLANG" /* set (tools/ci.sh): no glslangValidator fails the test, not skips it */
/* The desktop GL versions the host asks for (KpGfxApi.glsl_version). */
static const int CORE_VERSIONS[] = KP_GFX_GLSL_CORE;

static void remove_dir(void *dir) { (void)remove((const char *)dir); }

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

/* The validator can run here (1), else why not, skipped (0) or with GLSLANG_REQUIRED set failed (-1). */
static int glslang_runs(KpT *T)
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
	kp_t_log(T, "SKIP test_gfx.shaders_compile_for_every_context: %s", why);
	return 0;
}

/* Every program's vertex and fragment shader compile and link after the version and precision lines kp_gfx_gl.c puts
 * before them: GLES 3.0's (WebGL2, the phones) and desktop GL core's. A shader that does not compile stops the app at
 * its start (kp_gb_new); this is the only check before a context runs them. The files go in a directory of this run's
 * own (kp_test processes run beside each other). */
static void shaders_compile_for_every_context(KpT *T)
{
	static char dir[400]; /* read by its removal at the test's end */
	const char *tmp = getenv("TMPDIR");
	char log[4096], header[64];
	int k, v;
	if (glslang_runs(T) <= 0) return;
	snprintf(dir, sizeof dir, "%s/kp_gfx_XXXXXX", tmp && tmp[0] ? tmp : "/tmp");
	kp_t_ok(T, mkdtemp(dir) != NULL, "%s: no directory", dir);
	kp_t_defer(T, remove_dir, dir);
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

/* --- the passes' depth state --- */

static void gfx_free(void *p) { kp_gfx_free((KpGfx *)p); }

static void lookups_free(void *p) { free(((KpLookups *)p)->tables); }

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
	KpGfxApi api = { 1, 0 };
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

/* The canvas pass uploads the HD interface's planes at the canvas's size: the items and what lay under their SD
 * pixels (KpUiCanvas.below), which the HD composite reads. */
static void hd_composite_reads_what_lay_under(KpT *T)
{
	enum { W = 8, H = 4 };
	static KpCanvas c;
	static uint8_t pixels[W * H * 2];
	static uint16_t ids[W * H], below[W * H];
	KpGfxApi api = { 1, 0 };
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

/* The lit slots (BC blocks only) get the lighting layers, two a slot (the normal map and occlusion in BC3, the
 * specular and emissive colours in BC1), filled only for a page in a lit slot; the HD images' draws bind them, with
 * the half vector of StarCraft: Remastered's light and its eye over the view and the lit slots' count. */
static void hd_lit_slots_light_the_sprites(KpT *T)
{
	static uint8_t diffuse[64 * 64], team[64 * 32], layer[64 * 64];
	uint8_t *const light[KP_HD_LIGHTS] = { layer, layer, layer, layer };
	static const float HALF[3] = { 0.45169f, 0.14614f, 0.88012f }; /* the view 640 x 480 */
	static KpPlan plan;
	KpGfxApi api = { 1, 0 };
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
		&& recorder->layers[KP_TEX_HD_LIGHT] == 4, "the normal maps and occlusion: two BC3 layers a lit slot");
	kp_t_ok(T, recorder->made[KP_TEX_HD_GLOW] && recorder->format[KP_TEX_HD_GLOW] == KP_FMT_BC1
		&& recorder->layers[KP_TEX_HD_GLOW] == 4, "the specular and emissive colours: two BC1 layers a lit slot");
	kp_gfx_hd_page(g, 1, diffuse, team, light);
	kp_t_ok(T, recorder->block_layers[KP_TEX_HD_LIGHT] == 0xc && recorder->block_layers[KP_TEX_HD_GLOW] == 0xc,
		"lit slot 1's layers 2 and 3");
	kp_gfx_hd_page(g, 3, diffuse, team, light);
	kp_t_eq(T, recorder->block_layers[KP_TEX_HD_LIGHT], 0xc, "an unlit slot's page brings none");
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
	kp_t_ok(T, !recorder->made[KP_TEX_HD_LIGHT] && !recorder->made[KP_TEX_HD_GLOW], "ETC2 pages without lighting");
	recorder->blocks = 0;
	kp_t_eq(T, kp_gfx_hd_slots(g, 4, 2, 64, KP_HD_FORMAT_BC, 1), 0, "decoded slots made");
	kp_t_ok(T, !recorder->made[KP_TEX_HD_LIGHT], "decoded pages without lighting");
	recorder->count = 0;
	kp_gfx_frame(g, &f);
	kp_t_eq(T, (int)recorder->light[3], 0, "no lit slots: the sprites unlit");
}

static const KpTest TESTS[] = {
	{ "shaders_compile_for_every_context", shaders_compile_for_every_context, 0 },
	{ "world_passes_keep_their_depth_state", world_passes_keep_their_depth_state, 0 },
	{ "hd_composite_reads_what_lay_under", hd_composite_reads_what_lay_under, 0 },
	{ "hd_lit_slots_light_the_sprites", hd_lit_slots_light_the_sprites, 0 },
};

KP_SUITE(kp_suite_gfx, "test_gfx", TESTS);
