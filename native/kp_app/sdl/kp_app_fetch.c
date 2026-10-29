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
