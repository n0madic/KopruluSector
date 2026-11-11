/*
 * The product's files (plan phase S, stage S5): the converted data read through SDL (a directory on the desktop, the
 * APK's assets on Android, the preloaded package on the web), the mission index, and the preferences (the profile and
 * the saves) in SDL's preference path; on the web that path is an IndexedDB mount, written back after every write
 * (FS.syncfs).
 */
#include "kp_app.h"
#include "kp_sha256.h"

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

/* The vector fonts of the interface's text at large scales (ADR 0011): SIL OFL fonts, not original data, as ui.kpd's
 * VFNT names them (converter/kpf/kpd.py VECTOR_FONTS); the original data's interface reads them from the program's
 * fonts/ folder, else the source tree's third_party/fonts/ (the working directory's). */
static const char *const VECTOR_FONTS[KP_APP_VECTOR_FONTS][2] = {
	{ "fonts\\Exo2.ttf", "exo2/Exo2.ttf" },
	{ "fonts\\Michroma-Regular.ttf", "michroma/Michroma-Regular.ttf" },
};

/* TODO: D8's bundles carry the fonts beside the repack; until then a product without them draws no vector text. */
static int read_vector_fonts(KpApp *a, KpUiFile *out)
{
	const char *base = SDL_GetBasePath();
	int k, n = 0;
	for (k = 0; k < KP_APP_VECTOR_FONTS; k++) {
		char path[1200];
		size_t size = 0;
		snprintf(path, sizeof path, "%sfonts/%s", base ? base : "", VECTOR_FONTS[k][1]);
		if (!(a->vector_fonts[k] = SDL_LoadFile(path, &size))) {
			snprintf(path, sizeof path, "third_party/fonts/%s", VECTOR_FONTS[k][1]);
			a->vector_fonts[k] = SDL_LoadFile(path, &size);
		}
		if (!a->vector_fonts[k]) {
			SDL_Log("data: no %s beside the program or in third_party/fonts: the text drawn without it", VECTOR_FONTS[k][1]);
			continue;
		}
		out[n].path = VECTOR_FONTS[k][0];
		out[n].path_len = strlen(VECTOR_FONTS[k][0]);
		out[n].data = (const uint8_t *)a->vector_fonts[k];
		out[n].size = size;
		n++;
	}
	return n;
}

/* The save identity of the original data (ADR 0018, stage D5): the profile's id and the SHA-256 of the data set's DAT
 * (the games' CRCs of the rest are checked on a load). TODO: D8's identity of the simulation's inputs, the same from
 * either data. */
static void files_identity(KpSaveIdentity *id, const KpFiles *files, const KpDat *dat)
{
	memset(id, 0, sizeof *id);
	snprintf(id->profile_id, sizeof id->profile_id, "%s", files->profile->id);
	kp_sha256(dat->blob, dat->size, id->source_hash);
}

/* The data every game and the interface read, built from the original files (ADR 0018, stage D5); the missions are the
 * campaign's (mapdata.dat's directories) and the converted data's test scenarios when it has them. */
static int load_original(KpApp *a, char *err, size_t err_size)
{
	KpError e;
	KpUiFile fonts[KP_APP_VECTOR_FONTS];
	int font_count;
	size_t scenarios_size = 0;
	char *scenarios;
	kp_res_files(a->res, &a->files);
	if (!a->files.profile) {
		snprintf(err, err_size, "data: the original data has no compiled profile");
		return -1;
	}
	font_count = read_vector_fonts(a, fonts);
	if (kp_dat_from_files(&a->dat_proto, &a->files, &e) != 0
		|| kp_host_shared_from_files(&a->shared, &a->files, &a->dat_proto, &e) != 0
		|| kp_palettes_from_files(&a->palettes, &a->files, &e) != 0
		|| kp_campaign_from_files(&a->campaign, &a->files, &e) != 0
		|| kp_ui_data_from_files(&a->ui_data, &a->files, &a->dat_proto, fonts, font_count, &e) != 0) {
		snprintf(err, err_size, "data: %s", e.msg);
		return -1;
	}
	files_identity(&a->identity, &a->files, &a->dat_proto);
	a->musi = kp_ui_data_table(&a->ui_data, "MUSI", &a->musi_size);
	a->console_art = kp_app_read(a, "generated/data/console.kpd", &a->console_art_size);
	scenarios = (char *)kp_app_read(a, "generated/scenarios/index.txt", &scenarios_size);
	a->mission_count = kp_flow_missions_campaign(a->missions, KP_APP_MISSIONS, &a->campaign, scenarios, scenarios_size);
	free(scenarios);
	return 0;
}

int kp_app_load_data(KpApp *a, char *err, size_t err_size)
{
	KpError e;
	uint8_t *grp = NULL, *iscript = NULL, *aiscript = NULL;
	size_t grp_size, iscript_size, aiscript_size;
	int k, rc = -1;
	if (a->res) return load_original(a, err, err_size);
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

void kp_app_data_free(KpApp *a)
{
	int k;
	kp_host_shared_free(&a->shared);
	kp_palettes_free(&a->palettes);
	kp_campaign_free(&a->campaign);
	kp_dat_free(&a->dat_proto);
	kp_ui_data_free(&a->ui_data);
	for (k = 0; k < KP_APP_VECTOR_FONTS; k++) SDL_free(a->vector_fonts[k]);
	for (k = 0; k < KP_UI_FILES; k++) free(a->ui_files[k]);
	free(a->dat);
	free(a->palettes_bytes);
	free(a->campaign_bytes);
	free(a->sounds_bytes);
	free(a->console_art);
	free(a->music_bytes);
	memset(&a->files, 0, sizeof a->files);
}

static void res_log(void *ctx, const char *line)
{
	(void)ctx;
	SDL_Log("%s", line);
}

int kp_app_res_open(KpApp *a, char *err, size_t err_size)
{
	KpResConfig config;
	const char *dirs[KP_APP_RES_DIRS];
	int k;
	if (!a->flags.res_dir_count) return 0;
	for (k = 0; k < a->flags.res_dir_count; k++) dirs[k] = a->flags.res_dirs[k];
	memset(&config, 0, sizeof config);
	config.log = res_log;
	if (!(a->res = kp_res_open(dirs, a->flags.res_dir_count, &config, err, err_size))) return -1;
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
