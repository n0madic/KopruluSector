/*
 * The product's files (plan phase S, stage S5): the converted data read through SDL (a directory on the desktop, the
 * APK's assets on Android, the preloaded package on the web: the HD art, the languages, the cinematics' packs), the
 * original data every game and the interface are built from (ADR 0018: an install or a repack through kp_res; Android's
 * repack read in place from the APK), and the preferences (the profile and the saves) in SDL's preference path; on the web
 * that path is an IndexedDB mount, written back after every write (FS.syncfs).
 */
#include "kp_app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kp_mpq.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
#if defined(SDL_PLATFORM_ANDROID)
#include <android/asset_manager.h>
#include <unistd.h>
#endif

void kp_app_path(const KpApp *a, const char *path, char *out, size_t size)
{
	if (a->flags.data[0]) snprintf(out, size, "%s/%s", a->flags.data, path);
	else snprintf(out, size, "%s", path);
}

uint8_t *kp_app_read(KpApp *a, const char *path, size_t *size)
{
	char full[1200];
	SDL_IOStream *io;
	Sint64 n;
	uint8_t *data = NULL;
	*size = 0;
	kp_app_path(a, path, full, sizeof full);
	if (!(io = SDL_IOFromFile(full, "rb"))) return NULL;
	/* read into malloc'd bytes, which the game's code frees with free() */
	n = SDL_GetIOSize(io);
	if (n >= 0 && (data = (uint8_t *)malloc((size_t)n + 1)) != NULL) {
		if (SDL_ReadIO(io, data, (size_t)n) == (size_t)n) {
			data[n] = 0;
			*size = (size_t)n;
		} else {
			free(data);
			data = NULL;
		}
	}
	SDL_CloseIO(io);
	return data;
}

uint8_t *kp_io_read_at(SDL_IOStream *io, uint64_t offset, size_t size)
{
	Sint64 n = SDL_GetIOSize(io);
	uint8_t *data;
	if (n < 0 || offset > (uint64_t)n || size > (uint64_t)n - offset) return NULL;
	if (!(data = (uint8_t *)malloc(size ? size : 1))) return NULL;
	if (SDL_SeekIO(io, (Sint64)offset, SDL_IO_SEEK_SET) != (Sint64)offset || SDL_ReadIO(io, data, size) != size) {
		free(data);
		return NULL;
	}
	return data;
}

int kp_io_read_into(void *ctx, uint64_t offset, uint8_t *out, size_t size)
{
	SDL_IOStream *io = (SDL_IOStream *)ctx;
	return SDL_SeekIO(io, (Sint64)offset, SDL_IO_SEEK_SET) == (Sint64)offset && SDL_ReadIO(io, out, size) == size ? 0 : -1;
}

SDL_IOStream *kp_io_open_head(const char *path, uint8_t *head, size_t cap, size_t *n, Sint64 *size)
{
	SDL_IOStream *io = SDL_IOFromFile(path, "rb");
	*n = 0;
	*size = -1;
	if (!io) return NULL;
	*size = SDL_GetIOSize(io);
	if (*size <= 0) {
		if (*size < 0) *size = 0;
		SDL_CloseIO(io);
		return NULL;
	}
	*n = (uint64_t)*size < cap ? (size_t)*size : cap;
	if (SDL_ReadIO(io, head, *n) != *n) {
		SDL_CloseIO(io);
		return NULL;
	}
	return io;
}

#ifdef __EMSCRIPTEN__
/* The size of a file the web fetches (its head preloaded, its size in KP_APP_WEB_SIZES), else size. */
static size_t web_size(KpApp *a, const char *path, size_t size)
{
	size_t len = strlen(path), at = 0;
	if (!a->web_sizes_read) {
		a->web_sizes = kp_app_read(a, KP_APP_WEB_SIZES, &a->web_sizes_size);
		a->web_sizes_read = 1;
	}
	while (a->web_sizes && at + len < a->web_sizes_size) {
		const char *line = (const char *)a->web_sizes + at, *end = memchr(line, '\n', a->web_sizes_size - at);
		size_t n = end ? (size_t)(end - line) : a->web_sizes_size - at;
		if (n > len && memcmp(line, path, len) == 0 && line[len] == ' ') return (size_t)strtoull(line + len + 1, NULL, 10);
		at += n + 1;
	}
	return size;
}
#endif

int kp_app_exists(KpApp *a, const char *path)
{
	char full[1200];
	SDL_IOStream *io;
	kp_app_path(a, path, full, sizeof full);
	if (!(io = SDL_IOFromFile(full, "rb"))) return 0;
	SDL_CloseIO(io);
	return 1;
}

uint8_t *kp_app_read_head(KpApp *a, const char *path, size_t want, size_t *size, size_t *file_size)
{
	char full[1200];
	SDL_IOStream *io;
	Sint64 n;
	uint8_t *data = NULL;
	*size = *file_size = 0;
	kp_app_path(a, path, full, sizeof full);
	if (!(io = SDL_IOFromFile(full, "rb"))) return NULL;
	n = SDL_GetIOSize(io);
	if (n >= 0) {
		size_t take = (size_t)n < want ? (size_t)n : want;
		if ((data = (uint8_t *)malloc(take ? take : 1)) != NULL && SDL_ReadIO(io, data, take) == take) {
			*size = take;
			*file_size = (size_t)n;
#ifdef __EMSCRIPTEN__
			*file_size = web_size(a, path, *file_size); /* its head preloaded, the rest fetched */
#endif
		} else {
			free(data);
			data = NULL;
		}
	}
	SDL_CloseIO(io);
	return data;
}

/* The vector fonts of the interface's text at large scales (ADR 0011): SIL OFL fonts, not original data, under the
 * names the interface looks them up by (converter/kpf/kpd.py VECTOR_FONTS); read from the data root's fonts/ (a
 * bundle's, tools/product.sh), else the source tree's third_party/fonts/ (the working directory's). */

/* A file outside the data root (the source tree's) in malloc'd bytes, or NULL. */
static uint8_t *read_plain(const char *path, size_t *size)
{
	void *bytes = SDL_LoadFile(path, size);
	uint8_t *copy;
	if (!bytes) return NULL;
	if ((copy = (uint8_t *)malloc(*size ? *size : 1)) != NULL) memcpy(copy, bytes, *size);
	SDL_free(bytes);
	return copy;
}

static int read_vector_fonts(KpApp *a, KpUiFile *out)
{
	int k, n = 0;
	for (k = 0; k < KP_DATA_VECTOR_FONTS; k++) {
		const char *const *font = KP_DATA_VECTOR_FONT_FILES[k];
		char path[1200];
		size_t size = 0;
		snprintf(path, sizeof path, "fonts/%s", font[1]);
		if (!(a->vector_fonts[k] = kp_app_read(a, path, &size))) {
			snprintf(path, sizeof path, "third_party/fonts/%s", font[1]);
			a->vector_fonts[k] = read_plain(path, &size);
		}
		if (!a->vector_fonts[k]) {
			SDL_Log("data: no fonts/%s in the data or third_party/fonts: the text drawn without it", font[1]);
			continue;
		}
		out[n].path = font[0];
		out[n].path_len = strlen(font[0]);
		out[n].data = (const uint8_t *)a->vector_fonts[k];
		out[n].size = size;
		n++;
	}
	return n;
}

/* The data every game and the interface read, built from the original files (ADR 0018, kp_data.h); the missions are
 * the campaign's (mapdata.dat's directories) and the test scenarios when the data's root has them. */
int kp_app_load_data(KpApp *a, char *err, size_t err_size)
{
	KpError e;
	KpUiFile fonts[KP_DATA_VECTOR_FONTS];
	int font_count, rc;
	size_t scenarios_size = 0;
	char *scenarios;
	if (!a->res) {
		snprintf(err, err_size, "no original data: --data names a StarCraft 1.17.0.1 install, StarCraft: Remastered's or "
			"a repack (kp_convert repack)");
		return -1;
	}
	font_count = read_vector_fonts(a, fonts);
	scenarios = (char *)kp_app_read(a, KP_DATA_SCENARIOS "index.txt", &scenarios_size);
	rc = kp_data_build(&a->data, a->res, fonts, font_count, scenarios, scenarios_size, &e);
	free(scenarios);
	if (rc != 0) {
		snprintf(err, err_size, "data: %s", e.msg);
		return -1;
	}
	kp_save_identity(&a->identity, &a->data.files, &a->data.dat);
	a->musi = kp_ui_data_table(&a->data.ui, "MUSI", &a->musi_size);
	a->console_art = kp_app_read(a, "generated/data/console.kpd", &a->console_art_size);
	return 0;
}

void kp_app_data_free(KpApp *a)
{
	int k;
	kp_data_free(&a->data);
	for (k = 0; k < KP_DATA_VECTOR_FONTS; k++) free(a->vector_fonts[k]);
	free(a->console_art);
}

static void res_log(void *ctx, const char *line)
{
	(void)ctx;
	SDL_Log("%s", line);
}

#if defined(SDL_PLATFORM_ANDROID)
#define REPACK_ASSETS "generated/repack/" /* tools/product.sh: the repack's groups in the APK's assets */

/* KpResConfig.repack_file over the APK's assets: a group stored uncompressed (tools/native/android_app.sh's
 * noCompress) as the APK's descriptor, the group's offset and its length (AAsset_openFileDescriptor64), which StormLib
 * reads in place (kp_mpq_slice_name); the descriptors stay open while the data is (kp_app_res_close). The groups' bytes
 * are not hashed: the APK's signature covers them, which the system checks when it installs the APK. */
static int apk_repack_file(void *ctx, const char *file, char *out, size_t size)
{
	KpApp *a = (KpApp *)ctx;
	char asset[sizeof REPACK_ASSETS + 128];
	SDL_IOStream *io;
	AAsset *aa;
	off64_t start = 0, length = 0;
	int fd;
	if (a->apk_fd_count >= KP_APP_APK_FDS) {
		snprintf(out, size, "the repack's %s: more than %d files in the APK", file, KP_APP_APK_FDS);
		return -1;
	}
	snprintf(asset, sizeof asset, "%s%s", REPACK_ASSETS, file);
	if (!(io = SDL_IOFromFile(asset, "rb"))) return 0;
	aa = (AAsset *)SDL_GetPointerProperty(SDL_GetIOProperties(io), SDL_PROP_IOSTREAM_ANDROID_AASSET_POINTER, NULL);
	fd = aa ? AAsset_openFileDescriptor64(aa, &start, &length) : -1;
	SDL_CloseIO(io);
	if (fd < 0) {
		snprintf(out, size, "the repack's %s is not stored uncompressed in the APK (tools/native/android_app.sh's "
			"noCompress)", file);
		return -1;
	}
	if (kp_mpq_slice_name(fd, (uint64_t)start, (uint64_t)length, out, size) != 0) {
		close(fd);
		snprintf(out, size, "the repack's %s: its slice's name does not fit", file);
		return -1;
	}
	a->apk_fds[a->apk_fd_count++] = fd;
	return 1;
}
#endif

/* The descriptors apk_repack_file opened (Android), closed */
static void apk_close(KpApp *a)
{
#if defined(SDL_PLATFORM_ANDROID)
	int k;
	for (k = 0; k < a->apk_fd_count; k++) close(a->apk_fds[k]);
	a->apk_fd_count = 0;
#else
	(void)a;
#endif
}

int kp_app_res_open(KpApp *a, char *err, size_t err_size)
{
	KpResConfig config;
	const char *dirs[KP_APP_RES_DIRS];
	int k;
	memset(&config, 0, sizeof config);
	config.log = res_log;
#if defined(SDL_PLATFORM_ANDROID)
	if (!a->flags.res_dir_count) { /* no --data: the APK's repack */
		config.repack_file = apk_repack_file;
		config.repack_ctx = a;
	}
#endif
	if (!a->flags.res_dir_count && !config.repack_file) return 0;
	for (k = 0; k < a->flags.res_dir_count; k++) dirs[k] = a->flags.res_dirs[k];
	if (!(a->res = kp_res_open(dirs, a->flags.res_dir_count, &config, err, err_size))) {
		apk_close(a);
		return -1;
	}
	if (kp_res_install(a->res) == KP_RES_SOURCE_NONE)
		SDL_Log("data: the original data from a repack");
	else
		SDL_Log("data: the original data from the %s%s", kp_res_source_name(kp_res_install(a->res)),
			kp_res_repacks(a->res) ? " and a repack" : "");
	return 0;
}

void kp_app_res_close(KpApp *a)
{
	char err[600];
	int counts[KP_RES_STATUS_COUNT], k;
	if (!a->res) return;
	kp_res_counts(a->res, counts);
	for (k = 0; k < KP_RES_STATUS_COUNT; k++)
		if (counts[k]) SDL_Log("data: %d files %s", counts[k], kp_res_status_name((KpResStatus)k));
	if (a->flags.res_log[0] && kp_res_write_log(a->res, a->flags.res_log, err, sizeof err) != 0) SDL_Log("data: %s", err);
	kp_res_close(a->res);
	a->res = NULL;
	apk_close(a);
}

void kp_app_pref_path(KpApp *a, const char *file, char *out, size_t size) { snprintf(out, size, "%s%s", a->pref, file); }

void kp_app_sync_files(KpApp *a)
{
	(void)a;
#ifdef __EMSCRIPTEN__
	EM_ASM(FS.syncfs(false, function(err) { if (err) console.error("syncfs: " + err); }););
#endif
}

void kp_app_save_profile(KpApp *a)
{
	uint8_t *bytes;
	size_t size;
	char path[1100];
	KpError err;
	/* the checks' glue, victory and saves are not the player's; a script's profile is its own (--script-out's pref/) */
	if (kp_flow_dev_checks(&a->flags) && !kp_flow_scripted(&a->flags)) return;
	if (kp_profile_encode(&a->profile, &bytes, &size) != 0) return;
	kp_app_pref_path(a, KP_APP_PROFILE, path, sizeof path);
	if (kp_write_file_atomic(path, bytes, size, &err) != 0) SDL_Log("campaign: the profile could not be saved: %s", err.msg);
	free(bytes);
	kp_app_sync_files(a);
}

void kp_app_load_profile(KpApp *a)
{
	char path[1100];
	uint8_t *bytes;
	size_t size;
	KpError err;
	kp_app_pref_path(a, KP_APP_PROFILE, path, sizeof path);
	bytes = kp_read_file(path, &size);
	if (!bytes) {
		kp_profile_init(&a->profile);
		return;
	}
	if (kp_profile_decode(&a->profile, bytes, size, &err) != 0) SDL_Log("profile: %s (a new profile)", err.msg);
	free(bytes);
}
