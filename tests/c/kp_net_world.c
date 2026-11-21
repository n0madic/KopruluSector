/* kp_net_world: the network tests' world (kp_net_world.h). */
#include "kp_net_world.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BRIDGE_ATTEMPTS 16 /* kp_net_world_bridge_open's port pairs tried */
#define BRIDGE_GAP 16      /* the first pair's distance above the bridge's ports */

#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 2, 3)))
#endif
static int world_fail(KpNetWorld *w, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(w->why, sizeof w->why, fmt, ap);
	va_end(ap);
	w->failed = 1;
	if (w->fail) w->fail(w->fail_ud, w->why);
	return 0;
}

uint64_t kp_net_world_fnv(uint64_t h, const void *p, size_t n)
{
	const uint8_t *b = (const uint8_t *)p;
	while (n--) h = (h ^ *b++) * 0x100000001b3ull;
	return h;
}

void kp_net_world_tap(void *ud, uint32_t at, int host, int socket, const KpNetAddr *to, const uint8_t *data,
	size_t size)
{
	KpNetWorld *w = (KpNetWorld *)ud;
	w->trace = kp_net_world_fnv(w->trace, &at, sizeof at);
	w->trace = kp_net_world_fnv(w->trace, &host, sizeof host);
	w->trace = kp_net_world_fnv(w->trace, &socket, sizeof socket);
	w->trace = kp_net_world_fnv(w->trace, to->b, sizeof to->b);
	w->trace = kp_net_world_fnv(w->trace, data, size);
	w->datagrams++;
}

void kp_net_world_deliver(void *ud, int socket, const KpNetAddr *from, const uint8_t *data, size_t size)
{
	KpNetSeat *s = (KpNetSeat *)ud;
	if (kp_snp_udp_input(&s->snp, socket, from, data, size)) s->signalled = 1;
}

uint32_t kp_net_world_now(const KpNetWorld *w) { return w->lan ? kp_net_lan_now(w->lan) : kp_net_sock_now(); }

static uint32_t seat_now(void *ud) { return kp_net_world_now(((KpNetSeat *)ud)->w); }

void kp_net_world_pump(KpNetWorld *w)
{
	int k;
	for (k = 0; k < w->count; k++) {
		KpNetSeat *s = w->seat[k];
		uint32_t now = kp_net_world_now(w), t;
		int sig;
		if (k == w->in_join) continue; /* SNetJoinGame holds the lock and pumps itself */
		sig = s->signalled | kp_snet_take_signal(s->net);
		if (!sig && (s->wake == KP_SNET_INFINITE || (int32_t)(now - s->wake) < 0)) continue;
		s->signalled = 0;
		t = kp_snet_pump(s->net, sig);
		s->wake = t == KP_SNET_INFINITE ? KP_SNET_INFINITE : now + t;
	}
}

/* A world of sockets until `to` on the system's clock: what arrived at each seat (the joining one too: its provider
 * keeps what arrives for its pump), the passes due, a millisecond's wait while nothing comes. */
static void sockets(KpNetWorld *w, uint32_t to)
{
	int turn = 0;
	for (;;) {
		int k, got = 0, n;
		for (k = 0; k < w->count; k++) {
			n = kp_net_sock_wait(w->seat[k]->sock, 0, kp_net_world_deliver, w->seat[k]);
			if (n < 0) {
				world_fail(w, "seat %d's sockets fail", k);
				return;
			}
			got += n;
		}
		kp_net_world_pump(w);
		if ((int32_t)(kp_net_sock_now() - to) >= 0) return;
		if (!got) {
			turn = (turn + 1) % w->count;
			n = kp_net_sock_wait(w->seat[turn]->sock, 1, kp_net_world_deliver, w->seat[turn]);
			if (n < 0) {
				world_fail(w, "seat %d's sockets fail", turn);
				return;
			}
		}
	}
}

void kp_net_world_network(KpNetWorld *w, uint32_t to)
{
	if (!w->lan) {
		sockets(w, to);
		return;
	}
	for (;;) {
		uint32_t next = kp_net_lan_next(w->lan);
		int k;
		kp_net_world_pump(w);
		for (k = 0; k < w->count; k++) {
			uint32_t wake = w->seat[k]->wake;
			if (k != w->in_join && wake != KP_SNET_INFINITE && (next == KP_NET_LAN_IDLE || (int32_t)(wake - next) < 0))
				next = wake;
		}
		if (next == KP_NET_LAN_IDLE || (int32_t)(next - to) > 0) break;
		if ((int32_t)(next - kp_net_lan_now(w->lan)) < 0) next = kp_net_lan_now(w->lan);
		kp_net_lan_run(w->lan, next);
	}
	kp_net_lan_run(w->lan, to);
	kp_net_world_pump(w);
}

/* StarCraft's turn loop runs in the lobby too, and the turn it sends each frame wakes Storm's pump thread: after a
 * retransmission Storm's timers leave the thread waiting for that signal (RunNetTimers puts no time for it in its
 * result). */
void kp_net_world_advance(KpNetWorld *w, uint32_t to)
{
	while ((int32_t)(kp_net_world_now(w) - to) < 0) {
		uint32_t next = kp_net_world_now(w) + KP_NET_WORLD_STEP;
		int k;
		if (w->frame)
			for (k = 0; k < w->count; k++)
				if (k != w->in_join) w->frame(w->seat[k]);
		if ((int32_t)(next - to) > 0) next = to;
		kp_net_world_network(w, next);
	}
}

static void seat_sleep(void *ud, uint32_t ms)
{
	KpNetSeat *s = (KpNetSeat *)ud;
	kp_net_world_advance(s->w, kp_net_world_now(s->w) + ms);
}

static void seat_event(void *ud, const KpSnetEvent *e)
{
	KpNetSeat *s = (KpNetSeat *)ud;
	if (s->w->event) s->w->event(s, e);
}

void kp_net_world_init(KpNetWorld *w, struct KpT *T, void (*fail)(void *ud, const char *why), void *fail_ud)
{
	memset(w, 0, sizeof *w);
	w->T = T;
	w->fail = fail;
	w->fail_ud = fail_ud;
	w->in_join = -1;
	w->trace = KP_NET_WORLD_FNV_BASIS;
}

void kp_net_world_release(KpNetWorld *w)
{
	int k;
	for (k = 0; k < w->count; k++) {
		kp_snet_free(w->seat[k]->net);
		w->seat[k]->net = NULL;
		kp_snp_udp_free(&w->seat[k]->snp);
	}
	w->count = 0;
	kp_net_lan_free(w->lan);
	w->lan = NULL;
}

int kp_net_world_lan(KpNetWorld *w, uint64_t seed)
{
	w->lan = kp_net_lan_new(seed, KP_NET_WORLD_START);
	if (!w->lan) return world_fail(w, "out of memory");
	kp_net_lan_tap(w->lan, kp_net_world_tap, w);
	return 1;
}

int kp_net_world_add(KpNetWorld *w, KpNetSeat *s, const KpNetIo *io)
{
	KpSnetHooks h;
	if (w->count >= KP_NET_WORLD_SEATS) return world_fail(w, "more than %d seats", KP_NET_WORLD_SEATS);
	s->w = w;
	s->index = w->count;
	s->wake = KP_SNET_INFINITE;
	kp_snp_udp_init(&s->snp, io, KP_NET_WORLD_PROGRAM, KP_NET_WORLD_VERSION, 8);
	memset(&h, 0, sizeof h);
	h.now = seat_now;
	h.sleep = seat_sleep;
	h.event = seat_event;
	h.ud = s;
	s->net = kp_snet_new(&s->snp, &h, KP_NET_WORLD_PROGRAM, KP_NET_WORLD_VERSION);
	w->seat[w->count++] = s; /* registered before the check: kp_net_world_release frees the provider */
	return s->net != NULL ? 1 : world_fail(w, "out of memory");
}

int kp_net_world_add_lan(KpNetWorld *w, KpNetSeat *s)
{
	KpNetIo io;
	s->host = kp_net_lan_add(w->lan, KP_NET_WORLD_IP(w->count), kp_net_world_deliver, s);
	if (s->host < 0) return world_fail(w, "LAN host %d not added", w->count);
	io = kp_net_lan_io(w->lan, s->host);
	return kp_net_world_add(w, s, &io);
}

KpNetSock *kp_net_world_bridge_open(const char *spec, int alone, char *error, size_t size)
{
	KpNetSockConfig c;
	KpNetSock *s = NULL;
	const char *colon = strchr(spec, ':');
	uint16_t base = (uint16_t)strtoul(spec, NULL, 10);
	int hosts = colon ? atoi(colon + 1) : 1, attempt;
	memset(&c, 0, sizeof c);
	for (attempt = 0; attempt < BRIDGE_ATTEMPTS && !s; attempt++) {
		uint16_t port = (uint16_t)(base + 2 * hosts + BRIDGE_GAP + 2 * attempt);
		if (alone) { /* the one host of a map of its own: socket A at `port`, B above it */
			c.map_base = port;
			c.map_hosts = 1;
			c.map_self = 0;
		} else {
			c.map_base = base;
			c.map_hosts = hosts;
			c.map_self = -1;
			c.port = port;
		}
		s = kp_net_sock_open(&c, error, size);
	}
	return s;
}
