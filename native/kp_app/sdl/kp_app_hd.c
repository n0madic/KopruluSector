/*
 * The HD pages' loader (ADR 0013, stages R4a and R4f): a thread reads the pages kp_hd_cache asks for from the HD
 * packs' files (and decodes them where the context does not take their blocks), the main thread uploads what arrived. Without threads
 * (the web) a page's two block ranges are fetched when it is asked for (kp_app_fetch.c: HTTP Range requests) and it
 * is a result once both came (stage R4b).
 */
#include "kp_app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
	int32_t page, page_size;
	uint32_t diffuse_offset, team_offset, light_offset[KP_HD_LIGHTS], generation;
	int format, decode, lit;
	char path[1200];
	KpFetch *diffuse, *team, *light[KP_HD_LIGHTS]; /* without a thread: its blocks on their way */
} Request;

struct KpHdLoader {
	SDL_Thread *thread;
	SDL_Mutex *lock;
	SDL_Condition *wake, *done;
	Request *requests;
	int32_t request_head, request_count, request_cap;
	KpHdLoaded *results; /* a ring as the requests' */
	int32_t result_head, result_count, result_cap;
	int busy, quit;
};

static void drop(KpHdLoaded *r);

/* A page of its blocks read (taken; NULL: not read): decoded (RGBA8 diffuse, the team mask's red) when asked, with
 * its lighting layers when lit, or ok 0. */
static KpHdLoaded complete(const Request *r, uint8_t *diffuse, uint8_t *team, uint8_t *const light[KP_HD_LIGHTS])
{
	KpHdLoaded out;
	size_t pixels = (size_t)r->page_size * (size_t)r->page_size;
	int k, lit = 1;
	memset(&out, 0, sizeof out);
	out.page = r->page;
	out.generation = r->generation;
	out.diffuse = diffuse;
	out.team = team;
	for (k = 0; k < KP_HD_LIGHTS && r->lit; k++) {
		out.light[k] = light[k];
		lit = lit && light[k];
	}
	if (out.diffuse && out.team && r->decode) {
		uint8_t *rgba = (uint8_t *)malloc(pixels * 4), *mask = (uint8_t *)malloc(pixels);
		if (rgba && mask && kp_hd_page_decode(r->format, out.diffuse, out.team, r->page_size, rgba, mask) == 0) {
			free(out.diffuse);
			free(out.team);
			out.diffuse = rgba;
			out.team = mask;
			out.ok = 1;
		} else {
			free(rgba);
			free(mask);
		}
	} else {
		out.ok = out.diffuse && out.team && lit;
	}
	if (!out.ok) drop(&out);
	return out;
}

/* One page read from its file (the loader's thread). */
static KpHdLoaded load(const Request *r)
{
	SDL_IOStream *io = SDL_IOFromFile(r->path, "rb");
	uint8_t *diffuse = NULL, *team = NULL, *light[KP_HD_LIGHTS] = { NULL };
	int k;
	if (io) {
		diffuse = kp_io_read_at(io, r->diffuse_offset, kp_hd_layer_bytes(r->format, KP_HD_LAYER_DIFFUSE, r->page_size));
		team = kp_io_read_at(io, r->team_offset, kp_hd_layer_bytes(r->format, KP_HD_LAYER_TEAM, r->page_size));
		for (k = 0; k < KP_HD_LIGHTS && r->lit; k++) {
			light[k] = kp_io_read_at(io, r->light_offset[k], kp_hd_layer_bytes(r->format, KP_HD_LAYER_LIGHT + k, r->page_size));
		}
		SDL_CloseIO(io);
	}
	return complete(r, diffuse, team, light);
}

static int push_result(KpHdLoader *l, const KpHdLoaded *r)
{
	if (l->result_count == l->result_cap) { /* grow the ring, unrolled from its head */
		int32_t cap = l->result_cap ? 2 * l->result_cap : 16, k;
		KpHdLoaded *grown = (KpHdLoaded *)malloc(sizeof *grown * (size_t)cap);
		if (!grown) return -1;
		for (k = 0; k < l->result_count; k++) grown[k] = l->results[(l->result_head + k) % l->result_cap];
		free(l->results);
		l->results = grown;
		l->result_head = 0;
		l->result_cap = cap;
	}
	l->results[(l->result_head + l->result_count) % l->result_cap] = *r;
	l->result_count++;
	return 0;
}

static void drop(KpHdLoaded *r)
{
	int k;
	free(r->diffuse);
	free(r->team);
	r->diffuse = r->team = NULL;
	for (k = 0; k < KP_HD_LIGHTS; k++) {
		free(r->light[k]);
		r->light[k] = NULL;
	}
}

/* A request's fetches still out, dropped as they end. */
static void forget(Request *r)
{
	int k;
	kp_fetch_free(r->diffuse);
	kp_fetch_free(r->team);
	for (k = 0; k < KP_HD_LIGHTS; k++) kp_fetch_free(r->light[k]);
}

static int SDLCALL run(void *data)
{
	KpHdLoader *l = (KpHdLoader *)data;
	SDL_LockMutex(l->lock);
	for (;;) {
		Request r;
		KpHdLoaded out;
		while (!l->quit && l->request_count == 0) SDL_WaitCondition(l->wake, l->lock);
		if (l->quit) break;
		r = l->requests[l->request_head];
		l->request_head = (l->request_head + 1) % l->request_cap;
		l->request_count--;
		l->busy = 1;
		SDL_UnlockMutex(l->lock);
		out = load(&r);
		SDL_LockMutex(l->lock);
		l->busy = 0;
		if (push_result(l, &out) != 0) drop(&out);
		SDL_SignalCondition(l->done);
	}
	SDL_UnlockMutex(l->lock);
	return 0;
}

KpHdLoader *kp_hd_loader_new(void)
{
	KpHdLoader *l = (KpHdLoader *)calloc(1, sizeof *l);
	if (!l) return NULL;
	l->lock = SDL_CreateMutex();
	l->wake = SDL_CreateCondition();
	l->done = SDL_CreateCondition();
	if (l->lock && l->wake && l->done) l->thread = SDL_CreateThread(run, "kp_hd_loader", l);
	if (!l->thread) SDL_Log("hd: no loader thread (%s): the pages are read when asked for", SDL_GetError());
	return l;
}

void kp_hd_loader_free(KpHdLoader *l)
{
	int32_t k;
	if (!l) return;
	if (l->thread) {
		SDL_LockMutex(l->lock);
		l->quit = 1;
		SDL_SignalCondition(l->wake);
		SDL_UnlockMutex(l->lock);
		SDL_WaitThread(l->thread, NULL);
	}
	for (k = 0; k < l->request_count; k++) forget(&l->requests[(l->request_head + k) % l->request_cap]);
	for (k = 0; k < l->result_count; k++) drop(&l->results[(l->result_head + k) % l->result_cap]);
	free(l->results);
	free(l->requests);
	if (l->done) SDL_DestroyCondition(l->done);
	if (l->wake) SDL_DestroyCondition(l->wake);
	if (l->lock) SDL_DestroyMutex(l->lock);
	free(l);
}

int kp_hd_loader_request(KpHdLoader *l, int32_t page, const char *path, uint32_t diffuse_offset, uint32_t team_offset,
	const uint32_t *light, int32_t page_size, int format, int decode, uint32_t generation)
{
	Request r;
	int rc = 0, k;
	memset(&r, 0, sizeof r);
	r.page = page;
	r.page_size = page_size;
	r.diffuse_offset = diffuse_offset;
	r.team_offset = team_offset;
	r.lit = light != NULL && !decode; /* decoded pages are drawn unlit */
	for (k = 0; k < KP_HD_LIGHTS && r.lit; k++) r.light_offset[k] = light[k];
	r.format = format;
	r.decode = decode;
	r.generation = generation;
	snprintf(r.path, sizeof r.path, "%s", path);
	if (!l->thread) { /* fetched now, a result once every range came */
		r.diffuse = kp_fetch_start(path, diffuse_offset, (uint32_t)kp_hd_layer_bytes(format, KP_HD_LAYER_DIFFUSE, page_size));
		r.team = kp_fetch_start(path, team_offset, (uint32_t)kp_hd_layer_bytes(format, KP_HD_LAYER_TEAM, page_size));
		for (k = 0; k < KP_HD_LIGHTS && r.lit; k++) {
			r.light[k] = kp_fetch_start(path, r.light_offset[k], (uint32_t)kp_hd_layer_bytes(format, KP_HD_LAYER_LIGHT + k, page_size));
		}
	}
	if (l->thread) SDL_LockMutex(l->lock);
	if (l->request_count == l->request_cap) { /* grow the ring, unrolled from its head */
		int32_t cap = l->request_cap ? 2 * l->request_cap : 32;
		Request *grown = (Request *)malloc(sizeof *grown * (size_t)cap);
		if (!grown) {
			rc = -1;
		} else {
			for (k = 0; k < l->request_count; k++) grown[k] = l->requests[(l->request_head + k) % l->request_cap];
			free(l->requests);
			l->requests = grown;
			l->request_head = 0;
			l->request_cap = cap;
		}
	}
	if (rc == 0) {
		l->requests[(l->request_head + l->request_count) % l->request_cap] = r;
		l->request_count++;
	} else {
		forget(&r);
	}
	if (l->thread) {
		SDL_SignalCondition(l->wake);
		SDL_UnlockMutex(l->lock);
	}
	return rc;
}

int kp_hd_loader_result(KpHdLoader *l, KpHdLoaded *out, int wait)
{
	int got = 0;
	if (!l->thread) { /* no thread: the requests whose ranges came, in order (wait cannot block the web's frame) */
		while (l->request_count > 0) {
			Request *r = &l->requests[l->request_head];
			KpHdLoaded loaded;
			uint8_t *light[KP_HD_LIGHTS] = { NULL };
			int k, pending = kp_fetch_state(r->diffuse) == KP_FETCH_PENDING || kp_fetch_state(r->team) == KP_FETCH_PENDING;
			for (k = 0; k < KP_HD_LIGHTS && r->lit; k++) pending = pending || kp_fetch_state(r->light[k]) == KP_FETCH_PENDING;
			if (pending) break;
			for (k = 0; k < KP_HD_LIGHTS && r->lit; k++) light[k] = kp_fetch_take(r->light[k], NULL);
			loaded = complete(r, kp_fetch_take(r->diffuse, NULL), kp_fetch_take(r->team, NULL), light);
			l->request_head = (l->request_head + 1) % l->request_cap;
			l->request_count--;
			if (push_result(l, &loaded) != 0) drop(&loaded);
		}
	} else {
		SDL_LockMutex(l->lock);
		while (wait && l->result_count == 0 && (l->request_count > 0 || l->busy)) SDL_WaitCondition(l->done, l->lock);
	}
	if (l->result_count > 0) {
		*out = l->results[l->result_head];
		l->result_head = (l->result_head + 1) % l->result_cap;
		l->result_count--;
		got = 1;
	}
	if (l->thread) SDL_UnlockMutex(l->lock);
	return got;
}

void kp_hd_loaded_free(KpHdLoaded *r) { drop(r); }
