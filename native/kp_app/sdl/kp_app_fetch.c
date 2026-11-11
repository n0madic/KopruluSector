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
		SDL_Log("fetch: %s %s: HTTP %d", fetch->url, f->range, (int)fetch->status);
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

int kp_app_group(KpApp *a, const char *file, char *err, size_t err_size)
{
	KpAppGroupFetch *g = NULL;
	char path[1200];
	uint8_t *bytes;
	size_t size = 0;
	int k;
	SDL_IOStream *io;
	snprintf(path, sizeof path, "%s/%s", kp_res_repack_dir(a->res), file);
	for (k = 0; k < KP_APP_GROUP_FETCHES && !g; k++)
		if (a->group_fetches[k].fetch && strcmp(a->group_fetches[k].file, file) == 0) g = &a->group_fetches[k];
	/* already there (beside the core on a computer or a phone, in memory once fetched on the web): opened in place */
	if (!g && SDL_GetPathInfo(path, NULL)) return kp_res_add_repack(a->res, path, err, err_size) == 0 ? 1 : -1;
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
		return -1;
	}
	/* on the web the file joins the preloaded core in memory */
	if (!SDL_GetPathInfo(path, NULL)) {
		io = SDL_IOFromFile(path, "wb");
		if (!io || SDL_WriteIO(io, bytes, size) != size) {
			snprintf(err, err_size, "%s cannot be written: %s", path, SDL_GetError());
			if (io) SDL_CloseIO(io);
			free(bytes);
			return -1;
		}
		SDL_CloseIO(io);
	}
	free(bytes);
	return kp_res_add_repack(a->res, path, err, err_size) == 0 ? 1 : -1;
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
	else kp_audio_music_again(a);
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
