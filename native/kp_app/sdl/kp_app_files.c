/*
 * The product's files (plan phase S, stage S5): the converted data read through SDL (a directory on the desktop, the
 * APK's assets on Android, the preloaded package on the web), the mission index, and the preferences (the profile and
 * the saves) in SDL's preference path; on the web that path is an IndexedDB mount, written back after every write
 * (FS.syncfs).
 */
#include "kp_app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
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

/* --locale's overlays: the glyphs for any language, the text for one that is not the base's (ADR 0016). One that is
 * missing leaves the base's, with one line. */
static void read_locale(KpApp *a)
{
	const char *locale = a->flags.locale;
	char path[128];
	if (locale[0] == 0) return;
	if (!(a->locale.glyphs = kp_app_read(a, "generated/locale/glyphs.kpd", &a->locale.glyphs_size))) {
		SDL_Log("language %s: no generated/locale/glyphs.kpd", locale);
	}
	if (strcmp(locale, KP_LOCALE_BASE) == 0) return;
	snprintf(path, sizeof path, "generated/locale/%s/strings.kpd", locale);
	if (!(a->locale.strings = kp_app_read(a, path, &a->locale.strings_size))) SDL_Log("language %s: no %s", locale, path);
	snprintf(path, sizeof path, "generated/locale/%s/ui.kpd", locale);
	if (!(a->locale.ui = kp_app_read(a, path, &a->locale.ui_size))) SDL_Log("language %s: no %s", locale, path);
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

int kp_app_read_missions(KpApp *a)
{
	size_t size, scenarios_size = 0;
	char *text = (char *)kp_app_read(a, "generated/missions/index.txt", &size), *scenarios;
	if (!text) return -1;
	scenarios = (char *)kp_app_read(a, "generated/scenarios/index.txt", &scenarios_size);
	a->mission_count = kp_flow_missions(a->missions, KP_APP_MISSIONS, text, size, scenarios, scenarios_size);
	free(text);
	free(scenarios);
	return 0;
}

static int need(KpApp *a, const char *path, uint8_t **out, size_t *size, char *err, size_t err_size)
{
	if ((*out = kp_app_read(a, path, size)) != NULL) return 0;
	snprintf(err, err_size, "cannot read %s%s%s: %s", a->flags.data, a->flags.data[0] ? "/" : "", path, SDL_GetError());
	return -1;
}

int kp_app_load_data(KpApp *a, char *err, size_t err_size)
{
	KpError e;
	uint8_t *grp = NULL, *iscript = NULL, *aiscript = NULL;
	size_t grp_size, iscript_size, aiscript_size;
	int k, rc = -1;
	/* the shared data parsed once (its loaders copy what they keep) */
	if (need(a, "generated/data/grpmeta.kpd", &grp, &grp_size, err, err_size) == 0
		&& need(a, "generated/data/iscript.kpd", &iscript, &iscript_size, err, err_size) == 0
		&& need(a, "generated/data/aiscript.kpd", &aiscript, &aiscript_size, err, err_size) == 0) {
		rc = kp_host_shared_load(&a->shared, grp, grp_size, iscript, iscript_size, aiscript, aiscript_size, &e);
		if (rc != 0) snprintf(err, err_size, "%s", e.msg);
	}
	free(grp);
	free(iscript);
	free(aiscript);
	if (rc != 0) return -1;
	if (need(a, "generated/data/dat.kpd", &a->dat, &a->dat_size, err, err_size) != 0
		|| need(a, "generated/data/palettes.kpd", &a->palettes_bytes, &a->palettes_size, err, err_size) != 0
		|| need(a, "generated/data/campaign.kpd", &a->campaign_bytes, &a->campaign_size, err, err_size) != 0
		|| need(a, "generated/data/sounds.kpd", &a->sounds_bytes, &a->sounds_size, err, err_size) != 0
		|| kp_app_music_open(a, err, err_size) != 0) {
		return -1;
	}
	for (k = 0; k < KP_UI_FILES; k++) {
		char path[128];
		snprintf(path, sizeof path, "generated/data/%s", KP_UI_FILE_NAMES[k]);
		if (need(a, path, &a->ui_files[k], &a->ui_sizes[k], err, err_size) != 0) return -1;
	}
	read_locale(a);
	a->console_art = kp_app_read(a, "generated/data/console.kpd", &a->console_art_size);
	if (kp_palettes_load(&a->palettes, a->palettes_bytes, a->palettes_size, &e) != 0
		|| kp_campaign_load(&a->campaign, a->campaign_bytes, a->campaign_size, "campaign.kpd", &e) != 0
		|| kp_save_identity(&a->identity, a->dat, a->dat_size, &e) != 0) {
		snprintf(err, err_size, "%s", e.msg);
		return -1;
	}
	if (kp_app_read_missions(a) != 0) {
		snprintf(err, err_size, "cannot read the mission index generated/missions/index.txt");
		return -1;
	}
	return 0;
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
	if (kp_flow_dev_checks(&a->flags)) return; /* the checks' glue, victory and saves are not the player's */
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
