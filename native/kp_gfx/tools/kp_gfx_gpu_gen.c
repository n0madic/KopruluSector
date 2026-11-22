/*
 * kp_gfx_gpu_gen: the SDL_GPU backend's shaders (ADR 0028) from kp_gfx_shaders.c. Each program's stages go through
 * the transform (kp_gfx_gpu_glsl.c), glslangValidator (Vulkan GLSL 450 to SPIR-V), spirv-val, and spirv-cross (SPIR-V to
 * MSL, the bindings as decorated); where Xcode's metal compiler is, it compiles the MSL too. The output is the committed
 * native/kp_gfx/kp_gfx_gpu_shaders.c: the platforms' builds need none of these tools, and test_gfx checks the file
 * against the transform (its reflection and the SHA-256 of each stage's text).
 *
 *   kp_gfx_gpu_gen OUT.c           write the file
 *   kp_gfx_gpu_gen --check OUT.c   compare it with what these tools write (only where their versions are the file's)
 */
#define _POSIX_C_SOURCE 200809L /* popen, mkdtemp */
#define _DARWIN_C_SOURCE        /* macOS declares mkdtemp only past POSIX's level */
#include "kp_gfx_gpu_glsl.h"
#include "kp_gfx_internal.h"
#include "kp_sha256.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define TEXT_BYTES (64 * 1024)
#define STAGES_MAX (2 * KP_PROGRAMS)
#define MSL_VERSION "20100" /* MSL 2.1: macOS 10.14, SDL_GPU's Metal floor */
#define METAL_STD "macos-metal2.1"

static const struct {
	const char *src, *name;
} SOURCES[] = {
	{ KP_GFX_FULLSCREEN_VS, "KP_GFX_FULLSCREEN_VS" }, { KP_GFX_TERRAIN_FS, "KP_GFX_TERRAIN_FS" },
	{ KP_GFX_WORLD_VS, "KP_GFX_WORLD_VS" }, { KP_GFX_WORLD_FS, "KP_GFX_WORLD_FS" },
	{ KP_GFX_DEST_FS, "KP_GFX_DEST_FS" }, { KP_GFX_DISTORT_FS, "KP_GFX_DISTORT_FS" },
	{ KP_GFX_FOG_FS, "KP_GFX_FOG_FS" }, { KP_GFX_UI_FS, "KP_GFX_UI_FS" },
	{ KP_GFX_PALETTE_FS, "KP_GFX_PALETTE_FS" }, { KP_GFX_SHARP_FS, "KP_GFX_SHARP_FS" },
	{ KP_GFX_TEXT_VS, "KP_GFX_TEXT_VS" }, { KP_GFX_TEXT_FS, "KP_GFX_TEXT_FS" },
	{ KP_GFX_HD_TERRAIN_FS, "KP_GFX_HD_TERRAIN_FS" }, { KP_GFX_HD_WORLD_VS, "KP_GFX_HD_WORLD_VS" },
	{ KP_GFX_HD_WORLD_FS, "KP_GFX_HD_WORLD_FS" }, { KP_GFX_HD_FOG_FS, "KP_GFX_HD_FOG_FS" },
	{ KP_GFX_HD_COMPOSITE_FS, "KP_GFX_HD_COMPOSITE_FS" }, { KP_GFX_VIDEO_FS, "KP_GFX_VIDEO_FS" },
};

typedef struct {
	const char *src, *vs; /* the stage's source; a fragment stage's vertex stage */
	int vertex;
	KpGfxGpuStage stage;
	char sha[2 * KP_SHA256_SIZE + 1];
	uint8_t *spirv;
	size_t spirv_size;
	char *msl;
} Stage;

typedef struct {
	char *data;
	size_t len, cap;
} Buf;

static void buf_put(Buf *b, const char *s, size_t n)
{
	if (b->len + n + 1 > b->cap) {
		size_t cap = b->cap ? b->cap : 1 << 16;
		while (b->len + n + 1 > cap) cap *= 2;
		b->data = (char *)realloc(b->data, cap);
		if (!b->data) {
			fprintf(stderr, "kp_gfx_gpu_gen: out of memory\n");
			exit(2);
		}
		b->cap = cap;
	}
	memcpy(b->data + b->len, s, n);
	b->len += n;
	b->data[b->len] = 0;
}

static void buf_fmt(Buf *b, const char *fmt, ...)
{
	char line[1024];
	va_list ap;
	int n;
	va_start(ap, fmt);
	n = vsnprintf(line, sizeof line, fmt, ap);
	va_end(ap);
	if (n < 0 || (size_t)n >= sizeof line) {
		fprintf(stderr, "kp_gfx_gpu_gen: a line too long\n");
		exit(2);
	}
	buf_put(b, line, (size_t)n);
}

static const char *source_name(const char *src)
{
	size_t k;
	for (k = 0; k < sizeof SOURCES / sizeof SOURCES[0]; k++) {
		if (SOURCES[k].src == src) return SOURCES[k].name;
	}
	return "?";
}

static uint8_t *read_file(const char *path, size_t *size)
{
	FILE *f = fopen(path, "rb");
	uint8_t *data;
	long n;
	if (!f) return NULL;
	if (fseek(f, 0, SEEK_END) != 0 || (n = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0) {
		fclose(f);
		return NULL;
	}
	data = (uint8_t *)malloc((size_t)n + 1);
	if (data && fread(data, 1, (size_t)n, f) != (size_t)n) {
		free(data);
		data = NULL;
	}
	fclose(f);
	if (data) {
		data[n] = 0;
		*size = (size_t)n;
	}
	return data;
}

static int write_file(const char *path, const char *data, size_t size)
{
	FILE *f = fopen(path, "wb");
	if (!f) return -1;
	if (fwrite(data, 1, size, f) != size) {
		fclose(f);
		return -1;
	}
	return fclose(f) == 0 ? 0 : -1;
}

/* Runs a command, its output into log; 0 when it succeeds, else the log printed. */
static int run(const char *dir, const char *fmt, ...)
{
	char command[2048], log[600];
	size_t size = 0;
	uint8_t *out;
	va_list ap;
	int rc;
	va_start(ap, fmt);
	vsnprintf(command, sizeof command, fmt, ap);
	va_end(ap);
	snprintf(log, sizeof log, "%s/log.txt", dir);
	strncat(command, " > '", sizeof command - strlen(command) - 1);
	strncat(command, log, sizeof command - strlen(command) - 1);
	strncat(command, "' 2>&1", sizeof command - strlen(command) - 1);
	rc = system(command);
	if (rc == 0) return 0;
	out = read_file(log, &size);
	fprintf(stderr, "kp_gfx_gpu_gen: failed: %s\n%s\n", command, out ? (const char *)out : "");
	free(out);
	return -1;
}

/* A tool's version: the first line its command prints. */
static void version(const char *command, char *out, size_t size)
{
	FILE *p = popen(command, "r");
	out[0] = 0;
	if (p) {
		if (fgets(out, (int)size, p)) out[strcspn(out, "\r\n")] = 0;
		pclose(p);
	}
	if (!out[0]) snprintf(out, size, "?");
}

static int has_metal(void)
{
	/* xcrun finds a stub where Xcode's Metal toolchain is not downloaded: a shader compiled tells */
	return system("printf 'kernel void k() {}\\n' | xcrun -sdk macosx metal -x metal -c - -o /dev/null > /dev/null 2>&1") == 0;
}

static int compile_stage(Stage *s, int n, const char *dir, int metal)
{
	char text[TEXT_BYTES], err[256], path[600], spv[600], msl[600];
	uint8_t digest[KP_SHA256_SIZE];
	size_t size = 0;
	const char *ext = s->vertex ? "vert" : "frag";
	int len = kp_gfx_gpu_glsl(s->src, s->vs, s->vertex, text, sizeof text, &s->stage, err, sizeof err);
	if (len < 0) {
		fprintf(stderr, "kp_gfx_gpu_gen: %s: %s\n", source_name(s->src), err);
		return -1;
	}
	kp_sha256(text, (size_t)len, digest);
	kp_sha256_hex(digest, s->sha);
	snprintf(path, sizeof path, "%s/stage%d.%s", dir, n, ext);
	snprintf(spv, sizeof spv, "%s/stage%d.spv", dir, n);
	snprintf(msl, sizeof msl, "%s/stage%d.metal", dir, n);
	if (write_file(path, text, (size_t)len) != 0) return -1;
	/* the MSL from SPIR-V with its names; the SPIR-V kept without them */
	if (run(dir, "glslangValidator --target-env vulkan1.0 -o '%s' '%s'", spv, path) != 0) return -1;
	if (run(dir, "spirv-val --target-env vulkan1.0 '%s'", spv) != 0) return -1;
	if (run(dir, "spirv-cross '%s' --msl --msl-version " MSL_VERSION " --msl-decoration-binding --output '%s'", spv,
			msl) != 0)
		return -1;
	if (metal && run(dir, "xcrun -sdk macosx metal -std=" METAL_STD " -c '%s' -o '%s/stage%d.air'", msl, dir, n) != 0)
		return -1;
	if (run(dir, "glslangValidator --target-env vulkan1.0 -g0 -o '%s' '%s'", spv, path) != 0) return -1;
	if (run(dir, "spirv-val --target-env vulkan1.0 '%s'", spv) != 0) return -1;
	s->spirv = read_file(spv, &s->spirv_size);
	s->msl = (char *)read_file(msl, &size);
	if (!s->spirv || !s->msl || s->spirv_size % 4 != 0) {
		fprintf(stderr, "kp_gfx_gpu_gen: %s: no output\n", source_name(s->src));
		return -1;
	}
	return 0;
}

static void put_msl(Buf *b, const char *msl)
{
	const char *s = msl;
	while (*s) {
		const char *e = strchr(s, '\n');
		size_t n = e ? (size_t)(e - s) : strlen(s), k;
		buf_put(b, "\t\"", 2);
		for (k = 0; k < n; k++) {
			char c = s[k];
			if (c == '\\' || c == '"') buf_put(b, "\\", 1);
			buf_put(b, &c, 1);
		}
		buf_put(b, e ? "\\n\"\n" : "\"\n", e ? 4 : 2);
		s += n + (e ? 1 : 0);
	}
}

static void put_bytes(Buf *b, const uint8_t *v, int n)
{
	int k;
	buf_put(b, "{ ", 2);
	for (k = 0; k < n; k++) buf_fmt(b, "%s%d", k ? ", " : "", v[k]);
	buf_put(b, n ? " }" : "0 }", n ? 2 : 3);
}

static void write_output(Buf *b, Stage *stages, int count, const int vs_of[], const int fs_of[], const char *versions)
{
	int n, k;
	buf_fmt(b, "/*\n * Generated by kp_gfx_gpu_gen from kp_gfx_shaders.c (ADR 0028); do not edit: regenerate it with kp_gfx_gpu_gen.\n"
		" * %s\n */\n#include \"kp_gfx_gpu_glsl.h\"\n#include \"kp_gfx_internal.h\"\n\n", versions);
	for (n = 0; n < count; n++) {
		const Stage *s = &stages[n];
		const uint32_t *words = (const uint32_t *)(const void *)s->spirv;
		size_t w, nw = s->spirv_size / 4;
		buf_fmt(b, "/* %d: %s */\nstatic const uint32_t SPIRV_%d[%zu] = {", n, source_name(s->src), n, nw);
		for (w = 0; w < nw; w++) buf_fmt(b, "%s0x%08x,", w % 8 == 0 ? "\n\t" : " ", (unsigned)words[w]);
		buf_fmt(b, "\n};\nstatic const char MSL_%d[] =\n", n);
		put_msl(b, s->msl);
		buf_put(b, "\t;\n\n", 4);
	}
	buf_fmt(b, "const KpGfxGpuShader KP_GFX_GPU_SHADERS[%d] = {\n", count);
	for (n = 0; n < count; n++) {
		const KpGfxGpuStage *g = &stages[n].stage;
		buf_fmt(b, "\t{ SPIRV_%d, sizeof SPIRV_%d, MSL_%d, sizeof MSL_%d - 1,\n\t\t{ %d, %d, %d, ", n, n, n, n, g->samplers,
			g->storage, g->uniforms);
		put_bytes(b, g->sampler, g->samplers);
		buf_put(b, ", ", 2);
		put_bytes(b, g->kind, g->samplers);
		buf_put(b, ", ", 2);
		put_bytes(b, g->storage_of, g->storage);
		buf_fmt(b, " },\n\t\t\"%s\" },\n", stages[n].sha);
	}
	buf_fmt(b, "};\nconst int KP_GFX_GPU_SHADER_COUNT = %d;\n\nconst KpGfxGpuProgram KP_GFX_GPU_PROGRAMS[KP_PROGRAMS] = {\n",
		count);
	for (k = 0; k < KP_PROGRAMS; k++) buf_fmt(b, "\t{ %d, %d },\n", vs_of[k], fs_of[k]);
	buf_put(b, "};\n", 3);
}

int main(int argc, char **argv)
{
	static Stage stages[STAGES_MAX];
	char dir[512], versions[512], glslang[160], cross[160], val[160];
	int vs_of[KP_PROGRAMS], fs_of[KP_PROGRAMS], count = 0, k, n, check, metal, rc = 0;
	const char *out;
	Buf b = { NULL, 0, 0 };
	check = argc == 3 && strcmp(argv[1], "--check") == 0;
	out = argc == 2 + check ? argv[argc - 1] : NULL;
	if (!out || out[0] == '-') { /* a lone --check names no file: it would be written as one named so */
		fprintf(stderr, "usage: kp_gfx_gpu_gen [--check] native/kp_gfx/kp_gfx_gpu_shaders.c\n");
		return 2;
	}
	/* the stages: each vertex stage once, then its programs' fragment stages */
	for (k = 0; k < KP_PROGRAMS; k++) {
		const KpGfxProgramSource *p = &KP_GFX_PROGRAMS[k];
		if (p->prog != k) {
			fprintf(stderr, "kp_gfx_gpu_gen: KP_GFX_PROGRAMS not in KP_PROG_* order\n");
			return 2;
		}
		for (n = 0; n < count && !(stages[n].vertex && stages[n].src == p->vs); n++) {}
		if (n == count) {
			stages[count].src = p->vs;
			stages[count++].vertex = 1;
		}
		vs_of[k] = n;
		stages[count].src = p->fs;
		stages[count].vs = p->vs;
		fs_of[k] = count++;
	}
	{ /* the stages' files in a directory of the run's own (its tools' logs too), removed at the end */
		const char *tmp = getenv("TMPDIR");
		snprintf(dir, sizeof dir, "%s/kp_gfx_gpu_gen.XXXXXX", tmp && tmp[0] ? tmp : "/tmp");
		if (!mkdtemp(dir)) {
			fprintf(stderr, "kp_gfx_gpu_gen: %s not made\n", dir);
			return 2;
		}
	}
	metal = has_metal();
	for (n = 0; n < count && rc == 0; n++) rc = compile_stage(&stages[n], n, dir, metal);
	if (rc != 0) {
		run(dir, "rm -rf '%s'", dir);
		return 1;
	}
	version("glslangValidator --version 2>&1", glslang, sizeof glslang);
	version("spirv-cross --revision 2>&1", cross, sizeof cross);
	version("spirv-val --version 2>&1", val, sizeof val);
	snprintf(versions, sizeof versions, "glslang: %s\n * spirv-cross: %s\n * spirv-val: %s\n * MSL: %s", glslang,
		cross, val, MSL_VERSION);
	write_output(&b, stages, count, vs_of, fs_of, versions);
	if (check) {
		size_t size = 0;
		uint8_t *old = read_file(out, &size);
		if (!old) {
			fprintf(stderr, "kp_gfx_gpu_gen: %s not read\n", out);
			rc = 1;
		} else if (!strstr((const char *)old, versions)) {
			printf("kp_gfx_gpu_gen: %s comes from other tool versions: not compared (test_gfx checks its reflection and"
				" digests)\n", out);
			rc = 0;
		} else if (size != b.len || memcmp(old, b.data, size) != 0) {
			fprintf(stderr, "kp_gfx_gpu_gen: %s is stale: regenerate it (kp_gfx_gpu_gen %s)\n", out, out);
			rc = 1;
		} else {
			printf("kp_gfx_gpu_gen: %s current (%d stages)\n", out, count);
		}
		free(old);
	} else if (write_file(out, b.data, b.len) != 0) {
		fprintf(stderr, "kp_gfx_gpu_gen: %s not written\n", out);
		rc = 1;
	} else {
		printf("kp_gfx_gpu_gen: %s written (%d stages%s)\n", out, count,
			metal ? ", MSL compiled" : "; no Metal toolchain: the MSL compiles when the backend starts");
	}
	run(dir, "rm -rf '%s'", dir);
	return rc;
}
