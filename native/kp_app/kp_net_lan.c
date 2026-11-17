/* The in-process LAN (kp_net_lan.h; phase N, stage N2). */
#include "kp_net_lan.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
	uint32_t at;
	uint64_t order;
	int host, socket;
	KpNetAddr from;
	size_t size;
	uint8_t *data;
} Flight;

typedef struct {
	KpNetLan *lan;
	int index;
	uint32_t ip;
	KpNetLanDeliver deliver;
	void *ud;
	int cut;
} Host;

struct KpNetLan {
	uint64_t rng;
	uint32_t now, delay, jitter, loss;
	uint64_t order;
	Host hosts[KP_NET_LAN_HOSTS];
	int host_count;
	Flight *heap; /* a binary heap by (at, order) */
	int count, capacity;
	KpNetLanStats stats;
	KpNetLanTap tap;
	void *tap_ud;
};

/* splitmix64 */
static uint64_t draw(KpNetLan *lan)
{
	uint64_t z = (lan->rng += 0x9e3779b97f4a7c15ull);
	z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
	z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
	return z ^ (z >> 31);
}

KpNetLan *kp_net_lan_new(uint64_t seed, uint32_t start)
{
	KpNetLan *lan = (KpNetLan *)calloc(1, sizeof *lan);
	if (!lan) return NULL;
	lan->rng = seed;
	lan->now = start;
	lan->delay = 1;
	lan->jitter = 1;
	return lan;
}

void kp_net_lan_free(KpNetLan *lan)
{
	int k;
	if (!lan) return;
	for (k = 0; k < lan->count; k++) free(lan->heap[k].data);
	free(lan->heap);
	free(lan);
}

void kp_net_lan_link(KpNetLan *lan, uint32_t delay, uint32_t jitter, uint32_t loss)
{
	lan->delay = delay;
	lan->jitter = jitter;
	lan->loss = loss;
}

static void io_send(void *ud, int socket, const KpNetAddr *to, const uint8_t *data, size_t size);
static uint32_t io_now(void *ud) { return ((Host *)ud)->lan->now; }

int kp_net_lan_add(KpNetLan *lan, uint32_t ip, KpNetLanDeliver deliver, void *ud)
{
	Host *h;
	if (lan->host_count >= KP_NET_LAN_HOSTS) return -1;
	h = &lan->hosts[lan->host_count];
	h->lan = lan;
	h->index = lan->host_count;
	h->ip = ip;
	h->deliver = deliver;
	h->ud = ud;
	return lan->host_count++;
}

void kp_net_lan_cut(KpNetLan *lan, int host, int cut)
{
	if (host >= 0 && host < lan->host_count) lan->hosts[host].cut = cut;
}

KpNetIo kp_net_lan_io(KpNetLan *lan, int host)
{
	KpNetIo io;
	io.send = io_send;
	io.now = io_now;
	io.ud = &lan->hosts[host];
	return io;
}

uint32_t kp_net_lan_now(const KpNetLan *lan) { return lan->now; }

KpNetLanStats kp_net_lan_stats(const KpNetLan *lan) { return lan->stats; }

void kp_net_lan_tap(KpNetLan *lan, KpNetLanTap tap, void *ud)
{
	lan->tap = tap;
	lan->tap_ud = ud;
}

/* (at, order) of a before b; times compared as GetTickCount's, by their difference */
static int earlier(const Flight *a, const Flight *b)
{
	int32_t d = (int32_t)(a->at - b->at);
	return d != 0 ? d < 0 : a->order < b->order;
}

static void heap_push(KpNetLan *lan, const Flight *f)
{
	int k;
	if (lan->count == lan->capacity) {
		int cap = lan->capacity ? 2 * lan->capacity : 64;
		Flight *heap = (Flight *)realloc(lan->heap, (size_t)cap * sizeof *heap);
		if (!heap) {
			free(f->data);
			return;
		}
		lan->heap = heap;
		lan->capacity = cap;
	}
	k = lan->count++;
	while (k > 0 && earlier(f, &lan->heap[(k - 1) / 2])) {
		lan->heap[k] = lan->heap[(k - 1) / 2];
		k = (k - 1) / 2;
	}
	lan->heap[k] = *f;
}

static Flight heap_pop(KpNetLan *lan)
{
	Flight top = lan->heap[0], last = lan->heap[--lan->count];
	int k = 0;
	for (;;) {
		int c = 2 * k + 1;
		if (c >= lan->count) break;
		if (c + 1 < lan->count && earlier(&lan->heap[c + 1], &lan->heap[c])) c++;
		if (!earlier(&lan->heap[c], &last)) break;
		lan->heap[k] = lan->heap[c];
		k = c;
	}
	if (lan->count) lan->heap[k] = last;
	return top;
}

static void route(KpNetLan *lan, const Host *src, const Host *dst, int socket, const KpNetAddr *from,
	const uint8_t *data, size_t size)
{
	Flight f;
	uint32_t delay = 0;
	lan->stats.sent++;
	if (dst != src) {
		if (src->cut || dst->cut) {
			lan->stats.cut++;
			return;
		}
		if (lan->loss && draw(lan) % 1000 < lan->loss) {
			lan->stats.lost++;
			return;
		}
		delay = lan->delay + (lan->jitter ? (uint32_t)(draw(lan) % ((uint64_t)lan->jitter + 1)) : 0);
	}
	memset(&f, 0, sizeof f);
	f.at = lan->now + delay;
	f.order = lan->order++;
	f.host = dst->index;
	f.socket = socket;
	f.from = *from;
	f.size = size;
	if (!(f.data = (uint8_t *)malloc(size ? size : 1))) return;
	if (size) memcpy(f.data, data, size);
	heap_push(lan, &f);
}

static void io_send(void *ud, int socket, const KpNetAddr *to, const uint8_t *data, size_t size)
{
	Host *src = (Host *)ud;
	KpNetLan *lan = src->lan;
	uint32_t ip = kp_net_addr_ip(to);
	uint16_t port = kp_net_addr_port(to);
	int dst_socket = port == KP_NET_LIST_PORT ? KP_NET_SOCKET_LIST : port == KP_NET_GAME_PORT ? KP_NET_SOCKET_GAME : -1;
	KpNetAddr from = kp_net_addr(src->ip, socket == KP_NET_SOCKET_LIST ? KP_NET_LIST_PORT : KP_NET_GAME_PORT);
	int k, routed = 0;
	if (lan->tap) lan->tap(lan->tap_ud, lan->now, src->index, socket, to, data, size);
	if (dst_socket < 0) {
		lan->stats.unreachable++;
		return;
	}
	for (k = 0; k < lan->host_count; k++) {
		Host *h = &lan->hosts[k];
		if (ip != KP_NET_BROADCAST && h->ip != ip) continue;
		route(lan, src, h, dst_socket, &from, data, size);
		routed = 1;
	}
	if (!routed) lan->stats.unreachable++;
}

uint32_t kp_net_lan_next(const KpNetLan *lan) { return lan->count ? lan->heap[0].at : KP_NET_LAN_IDLE; }

int kp_net_lan_run(KpNetLan *lan, uint32_t to)
{
	int n = 0;
	while (lan->count && (int32_t)(lan->heap[0].at - to) <= 0) {
		Flight f = heap_pop(lan);
		Host *h = &lan->hosts[f.host];
		if ((int32_t)(f.at - lan->now) > 0) lan->now = f.at;
		lan->stats.delivered++;
		n++;
		if (h->deliver) h->deliver(h->ud, f.socket, &f.from, f.data, f.size);
		free(f.data);
	}
	if ((int32_t)(to - lan->now) > 0) lan->now = to;
	return n;
}
