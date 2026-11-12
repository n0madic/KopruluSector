/*
 * The HD pages' loader (ADR 0013, stages R4a and R4f; ADR 0019, stage C5): a thread reads the pages kp_hd_cache asks
 * for from their packs' sources (a repack's HD pack, or the anims of a Remastered install the pages are built from) and
 * decodes them where the context does not take their blocks, the main thread uploads what arrived. Without threads (the
 * web) a page's block ranges are fetched from the repack's group beside the page when it is asked for (kp_app_fetch.c:
 * HTTP Range requests) and it is a result once they all came (stage R4b); a source the web does not have is read at
 * once.
 */
#include "kp_app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
	int32_t page, local, page_size;
	const KpHdSource *source;
	uint32_t diffuse_offset, team_offset, light_offset[KP_HD_LIGHTS], generation;
	int format, decode, lit;
	KpFetch *diffuse, *team, *light[KP_HD_LIGHTS]; /* the web's: its blocks on their way */
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
	KpResReader *reader; /* without a thread: the sources read at once */
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

/* One page read from its source through reader (the loader's thread). */
static KpHdLoaded load(const Request *r, KpResReader *reader)
{
	const KpHdSource *s = r->source;
	uint8_t *diffuse, *team, *light[KP_HD_LIGHTS] = { NULL };
	size_t bytes[KP_HD_LAYERS];
	int k, ok;
	for (k = 0; k < KP_HD_LAYERS; k++) bytes[k] = kp_hd_layer_bytes(r->format, k, r->page_size);
	diffuse = (uint8_t *)malloc(bytes[KP_HD_LAYER_DIFFUSE]);
	team = (uint8_t *)malloc(bytes[KP_HD_LAYER_TEAM]);
	ok = diffuse && team && reader;
	for (k = 0; k < KP_HD_LIGHTS && r->lit; k++) ok = ok && (light[k] = (uint8_t *)malloc(bytes[KP_HD_LAYER_LIGHT + k])) != NULL;
	if (ok && s->layout) {
		KpHdScrSheets sheets = s->sheets;
		KpError e;
		sheets.reader = reader;
		if (kp_hd_layout_page(s->layout, r->local, kp_hd_scr_sheets_read, &sheets, diffuse, team, r->lit ? light : NULL, &e) != 0) {
			SDL_Log("hd: page %d: %s", (int)r->page, e.msg);
			ok = 0;
		}
	} else if (ok) {
		ok = kp_res_reader_read(reader, &s->place, r->diffuse_offset, diffuse, bytes[KP_HD_LAYER_DIFFUSE]) == 0
			&& kp_res_reader_read(reader, &s->place, r->team_offset, team, bytes[KP_HD_LAYER_TEAM]) == 0;
		for (k = 0; k < KP_HD_LIGHTS && r->lit && ok; k++) {
			ok = kp_res_reader_read(reader, &s->place, r->light_offset[k], light[k], bytes[KP_HD_LAYER_LIGHT + k]) == 0;
		}
	}
	if (!ok) {
		free(diffuse);
		free(team);
		diffuse = team = NULL;
		for (k = 0; k < KP_HD_LIGHTS; k++) {
			free(light[k]);
			light[k] = NULL;
		}
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
	KpResReader *reader = kp_res_reader_new(); /* the thread's own handles */
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
		out = load(&r, reader);
		SDL_LockMutex(l->lock);
		l->busy = 0;
		if (push_result(l, &out) != 0) drop(&out);
		SDL_SignalCondition(l->done);
	}
	SDL_UnlockMutex(l->lock);
	kp_res_reader_free(reader);
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
	kp_res_reader_free(l->reader);
	if (l->done) SDL_DestroyCondition(l->done);
	if (l->wake) SDL_DestroyCondition(l->wake);
	if (l->lock) SDL_DestroyMutex(l->lock);
	free(l);
}

/* A layer's blocks at offset in a web source's pack, on their way (NULL: past the 4 GB a range can name). */
static KpFetch *fetch_layer(const KpHdSource *s, uint32_t offset, int format, int layer, int32_t page_size)
{
	uint64_t at = s->base + offset, size = kp_hd_layer_bytes(format, layer, page_size);
	return at + size <= UINT32_MAX ? kp_fetch_start(s->url, (uint32_t)at, (uint32_t)size) : NULL;
}

int kp_hd_loader_request(KpHdLoader *l, int32_t page, const KpHdSource *source, int32_t local, uint32_t diffuse_offset,
	uint32_t team_offset, const uint32_t *light, int32_t page_size, int format, int decode, uint32_t generation)
{
	Request r;
	int rc = 0, k;
	memset(&r, 0, sizeof r);
	r.page = page;
	r.local = local;
	r.source = source;
	r.page_size = page_size;
	r.diffuse_offset = diffuse_offset;
	r.team_offset = team_offset;
	r.lit = light != NULL && !decode; /* decoded pages are drawn unlit */
	for (k = 0; k < KP_HD_LIGHTS && r.lit; k++) r.light_offset[k] = light[k];
	r.format = format;
	r.decode = decode;
	r.generation = generation;
	if (!l->thread && source->url[0]) { /* fetched now, a result once every range came */
		r.diffuse = fetch_layer(source, diffuse_offset, format, KP_HD_LAYER_DIFFUSE, page_size);
		r.team = fetch_layer(source, team_offset, format, KP_HD_LAYER_TEAM, page_size);
		for (k = 0; k < KP_HD_LIGHTS && r.lit; k++) {
			r.light[k] = fetch_layer(source, r.light_offset[k], format, KP_HD_LAYER_LIGHT + k, page_size);
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
			if (r->source->url[0]) {
				for (k = 0; k < KP_HD_LIGHTS && r->lit; k++) light[k] = kp_fetch_take(r->light[k], NULL);
				loaded = complete(r, kp_fetch_take(r->diffuse, NULL), kp_fetch_take(r->team, NULL), light);
			} else { /* a source read at once */
				if (!l->reader) l->reader = kp_res_reader_new();
				loaded = load(r, l->reader);
			}
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
