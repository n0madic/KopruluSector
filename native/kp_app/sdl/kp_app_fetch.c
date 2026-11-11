/*
 * Byte ranges of the data files read without waiting (ADR 0013, stage R4b): on the web an HTTP Range request
 * (emscripten_fetch; a server answering 200 with the whole file has the range cut out of it), elsewhere a read done at
 * once. The HD art reads its pages, interface frames, portrait clips, tileset and water this way, so the web build
 * fetches what it shows instead of preloading the HD2 art.
 */
#include "kp_app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __EMSCRIPTEN__
#include <emscripten/fetch.h>
#endif

struct KpFetch {
	int state;            /* KP_FETCH_* */
	uint8_t *data;
	size_t size;
	uint32_t offset, want; /* want 0: the whole file */
	int abandoned;        /* freed by its owner while the request was out: the callback frees it */
	int whole_file;       /* a range answered with the whole file */
	char range[48];
};

#ifdef __EMSCRIPTEN__
static void finish(emscripten_fetch_t *fetch)
{
	KpFetch *f = (KpFetch *)fetch->userData;
	const uint8_t *body = (const uint8_t *)fetch->data;
	size_t n = (size_t)fetch->numBytes, from = 0;
	if (f->abandoned) {
		emscripten_fetch_close(fetch);
		free(f);
		return;
	}
	f->state = KP_FETCH_FAILED;
	if (fetch->status == 200 && f->want) { /* the whole file: the range cut out of it */
		f->whole_file = 1;
		from = f->offset;
		n = from <= n ? n - from : 0;
		if (n > f->want) n = f->want;
	}
	if ((fetch->status == 200 || fetch->status == 206) && (!f->want || n == f->want) && (body || n == 0)
		&& (f->data = (uint8_t *)malloc(n ? n : 1)) != NULL) {
		if (n) memcpy(f->data, body + from, n);
		f->size = n;
		f->state = KP_FETCH_DONE;
	} else {
		SDL_Log("fetch: %s%s%s: HTTP %d", fetch->url, f->range[0] ? " " : "", f->range, (int)fetch->status);
	}
	emscripten_fetch_close(fetch);
}
#endif

KpFetch *kp_fetch_start(const char *path, uint32_t offset, uint32_t size)
{
	KpFetch *f = (KpFetch *)calloc(1, sizeof *f);
	if (!f) return NULL;
	f->offset = offset;
	f->want = size;
	f->state = KP_FETCH_PENDING;
	if (size) snprintf(f->range, sizeof f->range, "bytes=%u-%u", offset, offset + size - 1);
#ifdef __EMSCRIPTEN__
	{
		emscripten_fetch_attr_t attr;
		const char *headers[3] = { "Range", f->range, NULL };
		size_t root = strlen(KP_APP_WEB_DATA);
		emscripten_fetch_attr_init(&attr);
		strcpy(attr.requestMethod, "GET");
		attr.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;
		attr.onsuccess = finish;
		attr.onerror = finish;
		attr.userData = f;
		if (size) attr.requestHeaders = headers; /* copied by emscripten_fetch */
		/* the data root's files lie beside the page */
		(void)emscripten_fetch(&attr, strncmp(path, KP_APP_WEB_DATA, root) == 0 ? path + root : path);
	}
#else
	{
		SDL_IOStream *io = SDL_IOFromFile(path, "rb");
		Sint64 n = io ? SDL_GetIOSize(io) : -1;
		size_t take = size ? size : (size_t)(n > 0 ? n : 0);
		f->data = io ? kp_io_read_at(io, offset, take) : NULL;
		f->size = f->data ? take : 0;
		f->state = f->data ? KP_FETCH_DONE : KP_FETCH_FAILED;
		if (io) SDL_CloseIO(io);
	}
#endif
	return f;
}

int kp_fetch_state(const KpFetch *f) { return f ? f->state : KP_FETCH_FAILED; }

int kp_fetch_whole_file(const KpFetch *f) { return f ? f->whole_file : 0; }

uint8_t *kp_fetch_take(KpFetch *f, size_t *size)
{
	uint8_t *data = NULL;
	if (size) *size = 0;
	if (!f || f->state == KP_FETCH_PENDING) return NULL;
	if (f->state == KP_FETCH_DONE) {
		data = f->data;
		if (size) *size = f->size;
	}
	free(f);
	return data;
}

void kp_fetch_free(KpFetch *f)
{
	if (!f) return;
	if (f->state == KP_FETCH_PENDING) { /* out on the web: its callback frees it */
		f->abandoned = 1;
		return;
	}
	free(f->data);
	free(f);
}

/* --- the repack's groups (ADR 0018, stage D8b) --- */

/* A group that cannot be had (the reason in err): its files are the install's when there is one (1, logged), else -1 */
static int group_failed(KpApp *a, const char *err)
{
	if (kp_res_install(a->res) == KP_RES_SOURCE_NONE) return -1;
	SDL_Log("data: %s (its files fall back to the install)", err);
	return 1;
}

/* A group's archive added: 1, else group_failed */
static int group_add(KpApp *a, const char *path, char *err, size_t err_size)
{
	return kp_res_add_repack(a->res, path, err, err_size) == 0 ? 1 : group_failed(a, err);
}

#ifdef __EMSCRIPTEN__
/* A group on the web: one already in memory opened in place, else fetched beside the page: 0 while its fetch is out
 * (or every slot is), then group_add once written into the preloaded core's folder in memory, else group_failed */
static int group_fetch(KpApp *a, const char *file, const char *path, char *err, size_t err_size)
{
	KpAppGroupFetch *g = NULL;
	uint8_t *bytes;
	size_t size = 0;
	int k;
	SDL_IOStream *io;
	for (k = 0; k < KP_APP_GROUP_FETCHES && !g; k++)
		if (a->group_fetches[k].fetch && strcmp(a->group_fetches[k].file, file) == 0) g = &a->group_fetches[k];
	/* already there (preloaded with the core, or fetched before): opened in place */
	if (!g && SDL_GetPathInfo(path, NULL)) return group_add(a, path, err, err_size);
	for (k = 0; k < KP_APP_GROUP_FETCHES && !g; k++) {
		if (a->group_fetches[k].fetch) continue;
		if (!(a->group_fetches[k].fetch = kp_fetch_start(path, 0, 0))) break;
		g = &a->group_fetches[k];
		snprintf(g->file, sizeof g->file, "%s", file);
		SDL_Log("data: fetching %s", file);
	}
	if (!g) return 0; /* every slot out: asked again on the next frame */
	if (kp_fetch_state(g->fetch) == KP_FETCH_PENDING) return 0;
	bytes = kp_fetch_take(g->fetch, &size);
	g->fetch = NULL;
	if (!bytes) {
		snprintf(err, err_size, "%s cannot be read (the repack's group beside %s)", path, "koprulu-core.mpq");
		return group_failed(a, err);
	}
	io = SDL_IOFromFile(path, "wb");
	if (!io || SDL_WriteIO(io, bytes, size) != size) {
		snprintf(err, err_size, "%s cannot be written: %s", path, SDL_GetError());
		if (io) SDL_CloseIO(io);
		free(bytes);
		return group_failed(a, err);
	}
	SDL_CloseIO(io);
	free(bytes);
	return group_add(a, path, err, err_size);
}
#endif

int kp_app_group(KpApp *a, const char *file, char *err, size_t err_size)
{
	char path[1200];
	const char *dir = kp_res_repack_dir(a->res);
	if (!dir[0]) { /* the program's own repack (Android's APK): its groups were all opened with it */
		snprintf(err, err_size, "the repack's %s is not in the program's data", file);
		return group_failed(a, err);
	}
	snprintf(path, sizeof path, "%s/%s", dir, file);
#ifdef __EMSCRIPTEN__
	return group_fetch(a, file, path, err, err_size);
#else
	/* beside the core, opened in place; fetched on the web alone */
	if (SDL_GetPathInfo(path, NULL)) return group_add(a, path, err, err_size);
	snprintf(err, err_size, "%s is not there (the repack's group beside %s)", path, "koprulu-core.mpq");
	return group_failed(a, err);
#endif
}

/* The music's group came (or its files fall back to the install): the tracks' lengths read again, the data's (a later
 * interface's) and the interface's, whose track asked for meanwhile starts; before, each had no file and no length. */
static void music_came(KpApp *a)
{
	KpError e;
	e.msg[0] = 0;
	if (kp_ui_data_music_lengths(&a->data.ui, &a->data.files, &e) != 0) {
		SDL_Log("data: the music's lengths: %s", e.msg);
		return;
	}
	if (!a->flow.ui) return;
	kp_ui_music_lengths(a->flow.ui, a->data.ui.music_lengths.data, a->data.ui.music_lengths.size);
	kp_ui_music_again(a->flow.ui);
}

void kp_app_groups_frame(KpApp *a)
{
	char err[600];
	int rc;
	if (!a->res) return;
	if (!a->music_group[0]) { /* asked once: the music's group, when the core names one that is not open */
		if (!kp_res_group_wanted(a->res, "music\\", a->music_group, sizeof a->music_group)) {
			snprintf(a->music_group, sizeof a->music_group, "-");
		}
	}
	if (strcmp(a->music_group, "-") == 0) return;
	if ((rc = kp_app_group(a, a->music_group, err, sizeof err)) == 0) return;
	if (rc < 0) SDL_Log("data: %s (the game plays without music)", err);
	else music_came(a);
	snprintf(a->music_group, sizeof a->music_group, "-");
}

void kp_app_groups_free(KpApp *a)
{
	int k;
	for (k = 0; k < KP_APP_GROUP_FETCHES; k++) {
		kp_fetch_free(a->group_fetches[k].fetch);
		a->group_fetches[k].fetch = NULL;
	}
}
