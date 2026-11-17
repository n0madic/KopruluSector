/* test_snet: Storm's SNet (kp_snet.h) and the UDP provider (kp_snp_udp.h) over the in-process LAN (kp_net_lan.h)
 * and over the system's sockets (kp_net_sock.h); phase N, stage N2. Seats are whole SNet stacks on hosts 10.0.0.1 and
 * up; the world pumps each one as Storm's pump thread does (when a datagram arrived or a send signalled it, and when
 * its timer ran out) and plays StarCraft's lockstep: two turns in transit, a turn sent for each turn received. */
#include "kp_test.h"

#include "kp_net.h"
#include "kp_net_lan.h"
#include "kp_net_sock.h"
#include "kp_snet.h"
#include "kp_snp_udp.h"

#define PROGRAM 0x53544152u /* 'STAR' */
#define VERSION 0xd5u
#define START 1000000u /* the clock at the start: GetTickCount of a running machine */
#define SEATS 8
#define IP(k) (0x0a000001u + (uint32_t)(k))
#define TURN_SIZE 8
#define IN_TRANSIT 2
#define STEP 10 /* ms between two game frames of the world */
#define MAX_TURNS 1024
#define REASON_LEAVE 3u
#define REASON_DROP 0x40000006u
/* live_bridge_join */
#define LIVE_ENV "KOPRULU_NET_BRIDGE" /* BASE[:HOSTS]: the bridge's first port and its emulated hosts (default 1) */
#define LIVE_LIST_MS 600000            /* the emulated host's game listed within (the emulator boots in real time) */
#define LIVE_PLAY_MS 20000
#define LIVE_TURNS 16                  /* the host's turns received: StarCraft's lobby sends one each 250 ms */
#define KEEP_ALIVE 0x05                /* the lobby's turn: a keep-alive command */

typedef struct World World;

typedef struct {
	World *w;
	int index, host;
	KpSnpUdp snp;
	KpSnet *net;
	KpNetSock *sock; /* a world of sockets: the seat's */
	uint32_t wake;
	int signalled;
	int id; /* the seat's SNet id, -1 outside a game */
	int playing;
	uint32_t sent;              /* turns sent */
	uint32_t received;          /* turns received (successful kp_snet_receive_turns) */
	uint32_t last[SEATS];       /* per slot: the last turn number delivered + 1 (0: none yet) */
	uint64_t turns;             /* the hash of every delivered turn set */
	uint64_t by_turn[MAX_TURNS]; /* `turns` after each turn */
	int templates, players;     /* events 1 and 2 */
	int removed[SEATS];         /* event 3: the local turn at the removal + 1 */
	uint32_t reason[SEATS];
	int messages;
	char message[64][16];
	int message_from[64];
} Seat;

struct World {
	KpT *T;
	KpNetLan *lan; /* NULL: a world of sockets, on the system's clock */
	Seat seat[SEATS];
	int count;
	int in_join; /* the seat inside kp_snet_join_game, -1 */
	uint64_t trace; /* FNV-1a of every datagram sent: time, host, socket, destination, bytes */
	long datagrams;
};

static uint64_t fnv(uint64_t h, const void *p, size_t n)
{
	const uint8_t *b = (const uint8_t *)p;
	while (n--) h = (h ^ *b++) * 0x100000001b3ull;
	return h;
}

static void tap(void *ud, uint32_t at, int host, int socket, const KpNetAddr *to, const uint8_t *data, size_t size)
{
	World *w = (World *)ud;
	w->trace = fnv(w->trace, &at, sizeof at);
	w->trace = fnv(w->trace, &host, sizeof host);
	w->trace = fnv(w->trace, &socket, sizeof socket);
	w->trace = fnv(w->trace, to->b, sizeof to->b);
	w->trace = fnv(w->trace, data, size);
	w->datagrams++;
}

static void seat_deliver(void *ud, int socket, const KpNetAddr *from, const uint8_t *data, size_t size)
{
	Seat *st = (Seat *)ud;
	if (kp_snp_udp_input(&st->snp, socket, from, data, size)) st->signalled = 1;
}

static uint32_t now_of(World *w) { return w->lan ? kp_net_lan_now(w->lan) : kp_net_sock_now(); }

static uint32_t seat_now(void *ud) { return now_of(((Seat *)ud)->w); }

/* The pump thread's passes of the seats that are due: signalled, or their timer ran out. */
static void pump_due(World *w)
{
	int k;
	for (k = 0; k < w->count; k++) {
		Seat *st = &w->seat[k];
		uint32_t now = now_of(w), t;
		int sig;
		if (k == w->in_join) continue; /* SNetJoinGame holds the lock and pumps itself */
		sig = st->signalled | kp_snet_take_signal(st->net);
		if (!sig && (st->wake == KP_SNET_INFINITE || (int32_t)(now - st->wake) < 0)) continue;
		st->signalled = 0;
		t = kp_snet_pump(st->net, sig);
		st->wake = t == KP_SNET_INFINITE ? KP_SNET_INFINITE : now + t;
	}
}

static int frame(Seat *st);

/* A world of sockets until `to` on the system's clock: what arrived at each seat (the joining one too: its provider
 * keeps what arrives for its pump), the passes due, a millisecond's wait while nothing comes. */
static void sockets(World *w, uint32_t to)
{
	int turn = 0;
	for (;;) {
		int k, got = 0, n;
		for (k = 0; k < w->count; k++) {
			n = kp_net_sock_wait(w->seat[k].sock, 0, seat_deliver, &w->seat[k]);
			kp_t_ok(w->T, n >= 0, "seat %d's sockets fail", k);
			got += n;
		}
		pump_due(w);
		if ((int32_t)(kp_net_sock_now() - to) >= 0) return;
		if (!got) {
			turn = (turn + 1) % w->count;
			n = kp_net_sock_wait(w->seat[turn].sock, 1, seat_deliver, &w->seat[turn]);
			kp_t_ok(w->T, n >= 0, "seat %d's sockets fail", turn);
		}
	}
}

/* Runs the network until `to`: deliveries and timers in time order, no game frames. */
static void network(World *w, uint32_t to)
{
	if (!w->lan) {
		sockets(w, to);
		return;
	}
	for (;;) {
		uint32_t next = kp_net_lan_next(w->lan);
		int k;
		pump_due(w);
		for (k = 0; k < w->count; k++) {
			uint32_t wake = w->seat[k].wake;
			if (k != w->in_join && wake != KP_SNET_INFINITE && (next == KP_NET_LAN_IDLE || (int32_t)(wake - next) < 0))
				next = wake;
		}
		if (next == KP_NET_LAN_IDLE || (int32_t)(next - to) > 0) break;
		if ((int32_t)(next - kp_net_lan_now(w->lan)) < 0) next = kp_net_lan_now(w->lan);
		kp_net_lan_run(w->lan, next);
	}
	kp_net_lan_run(w->lan, to);
	pump_due(w);
}

/* Time until `to`: every STEP ms each playing seat's frame, then the network. StarCraft's turn loop runs in the lobby
 * too, and the turn it sends each frame wakes Storm's pump thread: after a retransmission Storm's timers leave the
 * thread waiting for that signal (RunNetTimers puts no time for it in its result). */
static void advance(World *w, uint32_t to)
{
	while ((int32_t)(now_of(w) - to) < 0) {
		uint32_t next = now_of(w) + STEP;
		int k;
		for (k = 0; k < w->count; k++)
			if (k != w->in_join) frame(&w->seat[k]);
		if ((int32_t)(next - to) > 0) next = to;
		network(w, next);
	}
}

static void seat_sleep(void *ud, uint32_t ms)
{
	Seat *st = (Seat *)ud;
	advance(st->w, now_of(st->w) + ms);
}

static void seat_event(void *ud, const KpSnetEvent *e)
{
	Seat *st = (Seat *)ud;
	if (e->id == KP_SNET_EVENT_TEMPLATE) st->templates++;
	else if (e->id == KP_SNET_EVENT_PLAYER) st->players++;
	else if (e->id == KP_SNET_EVENT_REMOVED && e->player >= 0 && e->player < SEATS) {
		st->removed[e->player] = (int)kp_snet_local_turn(st->net) + 1;
		st->reason[e->player] = e->size == 4 ? kp_net_get32(e->data) : 0xdeadu;
	}
}

static void world_free(void *p)
{
	World *w = (World *)p;
	int k;
	for (k = 0; k < w->count; k++) {
		kp_snet_free(w->seat[k].net);
		kp_snp_udp_free(&w->seat[k].snp);
	}
	for (k = 0; k < SEATS; k++) kp_net_sock_close(w->seat[k].sock);
	kp_net_lan_free(w->lan);
	free(w);
}

static World *world_alloc(KpT *T)
{
	World *w = (World *)calloc(1, sizeof *w);
	kp_t_ok(T, w != NULL, "out of memory");
	kp_t_defer(T, world_free, w);
	w->T = T;
	w->in_join = -1;
	w->trace = 0xcbf29ce484222325ull;
	return w;
}

/* The next seat's SNet stack over `io`. */
static Seat *seat_add(World *w, const KpNetIo *io)
{
	Seat *st = &w->seat[w->count];
	KpSnetHooks h;
	st->w = w;
	st->index = w->count;
	st->id = -1;
	st->wake = KP_SNET_INFINITE;
	kp_snp_udp_init(&st->snp, io, PROGRAM, VERSION, 8);
	memset(&h, 0, sizeof h);
	h.now = seat_now;
	h.sleep = seat_sleep;
	h.event = seat_event;
	h.ud = st;
	st->net = kp_snet_new(&st->snp, &h, PROGRAM, VERSION);
	kp_t_ok(w->T, st->net != NULL, "out of memory");
	w->count++;
	return st;
}

static World *world_new(KpT *T, uint64_t seed, int seats)
{
	World *w = world_alloc(T);
	int k;
	w->lan = kp_net_lan_new(seed, START);
	kp_t_ok(T, w->lan != NULL, "out of memory");
	kp_net_lan_tap(w->lan, tap, w);
	for (k = 0; k < seats; k++) {
		Seat *st = &w->seat[k];
		KpNetIo io;
		st->host = kp_net_lan_add(w->lan, IP(k), seat_deliver, st);
		io = kp_net_lan_io(w->lan, st->host);
		seat_add(w, &io);
	}
	return w;
}

static void host(World *w, int k, const char *name, const char *password, uint32_t max_players)
{
	static const uint8_t tmpl[32] = {2, 0, 1, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0};
	Seat *st = &w->seat[k];
	int ok = kp_snet_create_game(st->net, name, password, "stats", 0x10002, 0, 0, tmpl, sizeof tmpl, max_players,
		"Host", "", &st->id);
	kp_t_ok(w->T, ok, "create game: error %#x", kp_snet_error(st->net));
	pump_due(w);
}

/* kp_snet_join_game by name now; returns its result (the error in *error). */
static int enter(World *w, int k, const char *name, const char *password, uint32_t *error)
{
	Seat *st = &w->seat[k];
	char player[16];
	int ok;
	snprintf(player, sizeof player, "Seat %d", k);
	w->in_join = k;
	ok = kp_snet_join_game(st->net, 0, name, password, player, "", &st->id);
	w->in_join = -1;
	if (error) *error = ok ? 0 : kp_snet_error(st->net);
	pump_due(w);
	return ok;
}

/* kp_snet_join_game by name once the advertisements arrived and a host takes a connect from the seat's address again
 * (it ignores one from an address it took one from less than max(4L, 1000) ms ago: a repeat). */
static int try_join(World *w, int k, const char *name, const char *password, uint32_t *error)
{
	advance(w, now_of(w) + 1500);
	return enter(w, k, name, password, error);
}

static void join(World *w, int k, const char *name)
{
	uint32_t error;
	int ok = try_join(w, k, name, "", &error); /* before the message reads error: arguments have no order */
	kp_t_ok(w->T, ok, "seat %d's join: error %#x", k, error);
}

static void send_turn(Seat *st)
{
	uint8_t t[TURN_SIZE];
	int ok;
	t[0] = (uint8_t)st->index;
	t[1] = 0x05;
	kp_net_put32(t + 2, st->sent);
	t[6] = 0xab;
	t[7] = 0xcd;
	ok = kp_snet_send_turn(st->net, t, sizeof t); /* before the message reads the error */
	kp_t_ok(st->w->T, ok, "seat %d sends: error %#x", st->index, kp_snet_error(st->net));
	st->sent++;
}

static void start_playing(Seat *st)
{
	int k;
	st->playing = 1;
	for (k = 0; k < IN_TRANSIT; k++) send_turn(st);
}

/* One frame of a seat: a turn received (each present slot's turn checked) and the next one sent. */
static int frame(Seat *st)
{
	const uint8_t *data[SEATS];
	uint32_t sizes[SEATS], status[SEATS];
	int k;
	if (!st->playing) return 0;
	if (!kp_snet_receive_turns(st->net, 0, SEATS, data, sizes, status)) {
		uint32_t e = kp_snet_error(st->net);
		kp_t_ok(st->w->T, e == KP_SNET_ERROR_NO_DATA, "seat %d receives: error %#x", st->index, e);
		return 0;
	}
	for (k = 0; k < SEATS; k++) {
		if (!data[k]) continue;
		kp_t_ok(st->w->T, sizes[k] == TURN_SIZE && data[k][6] == 0xab, "seat %d: slot %d's turn is %u bytes", st->index, k,
			sizes[k]);
		if (st->last[k])
			kp_t_ok(st->w->T, kp_net_get32(data[k] + 2) == st->last[k], "seat %d: slot %d's turn %u after %u", st->index,
				k, kp_net_get32(data[k] + 2), st->last[k] - 1);
		st->last[k] = kp_net_get32(data[k] + 2) + 1;
		st->turns = fnv(st->turns, &k, sizeof k);
		st->turns = fnv(st->turns, data[k], sizes[k]);
	}
	st->turns = fnv(st->turns, "|", 1);
	if (st->received < MAX_TURNS) st->by_turn[st->received] = st->turns;
	st->received++;
	send_turn(st);
	return 1;
}

static void play(World *w, uint32_t ms) { advance(w, now_of(w) + ms); }

static void checksum_vectors(KpT *T)
{
	/* computed from the atlas's description of FletcherSum and ComputePacketChecksum */
	static const uint8_t ping[12] = {0x67, 0x7b, 0x0c, 0, 5, 0, 7, 0, 0, 4, 1, 0};
	static const uint8_t turn[13] = {0xb2, 0x21, 0x0d, 0, 0x34, 0x12, 0x78, 0x56, 2, 0, 3, 0, 5};
	uint8_t p[13];
	memcpy(p, ping, sizeof ping);
	p[0] = p[1] = 0;
	kp_net_checksum(p, sizeof ping);
	kp_t_ok(T, memcmp(p, ping, sizeof ping) == 0, "ping's checksum %02x%02x", p[0], p[1]);
	kp_t_ok(T, kp_net_sum(ping, sizeof ping) == 0, "ping's sum");
	memcpy(p, turn, sizeof turn);
	p[0] = p[1] = 0;
	kp_net_checksum(p, sizeof turn);
	kp_t_ok(T, memcmp(p, turn, sizeof turn) == 0, "turn's checksum %02x%02x", p[0], p[1]);
	p[12] ^= 1;
	kp_t_ok(T, kp_net_sum(p, sizeof turn) != 0, "a changed byte fails the sum");
}

typedef struct {
	int count;
	uint32_t id, players, max_players;
	char name[KP_SNET_STRING];
} Listing;

static int list_game(void *ud, const KpSnetGame *g)
{
	Listing *l = (Listing *)ud;
	if (!l->count++) {
		l->id = g->id;
		l->players = g->players;
		l->max_players = g->max_players;
		snprintf(l->name, sizeof l->name, "%s", g->name);
	}
	return 1;
}

static Listing listing(World *w, int k)
{
	Listing l;
	uint32_t hint = 0;
	memset(&l, 0, sizeof l);
	kp_t_ok(w->T, kp_snet_enum_games(w->seat[k].net, list_game, &l, &hint), "enum");
	kp_t_ok(w->T, hint == KP_SNP_ENUM_HINT, "the hint %u", hint);
	return l;
}

static void game_list_follows_the_advertisements(KpT *T)
{
	World *w = world_new(T, 1, 3);
	Listing l;
	host(w, 0, "Koprulu", "", 4);
	advance(w, now_of(w) + 100);
	l = listing(w, 1);
	kp_t_ok(T, l.count == 1 && strcmp(l.name, "Koprulu") == 0 && l.players == 1 && l.max_players == 4,
		"seat 1 lists %d games, the first \"%s\" %u/%u", l.count, l.name, l.players, l.max_players);
	/* the host's own broadcast comes back to it, as on Windows */
	kp_t_ok(T, listing(w, 0).count == 1, "the host lists its own game");
	/* a joined player is advertised */
	join(w, 1, "Koprulu");
	advance(w, now_of(w) + 100);
	l = listing(w, 2);
	kp_t_ok(T, l.count == 1 && l.players == 2, "seat 2 lists %d games, %u players", l.count, l.players);
	/* entries expire after 10 s without an advertisement (the last answered the request of the listing above); a
	 * request (each unlock past 400 ms) brings it back */
	advance(w, now_of(w) + KP_SNP_GAME_LIFETIME + 100);
	l = listing(w, 2);
	kp_t_ok(T, l.count == 0, "the game listed %d times after 10 s of silence", l.count);
	advance(w, now_of(w) + 100);
	kp_t_ok(T, listing(w, 2).count == 1, "the game listed again after the request");
	/* a full or closed game is not listed */
	kp_t_ok(T, kp_snet_set_game_mode(w->seat[0].net, KP_SNET_MODE_CLOSED | KP_SNET_MODE_ADVERTISED, 0), "set mode");
	advance(w, now_of(w) + 100);
	kp_t_ok(T, listing(w, 2).count == 0, "a closed game is listed");
	/* leaving stops the advertisement */
	kp_t_ok(T, kp_snet_set_game_mode(w->seat[0].net, KP_SNET_MODE_ADVERTISED, 0), "set mode");
	advance(w, now_of(w) + 100);
	kp_t_ok(T, listing(w, 2).count == 1, "the reopened game is listed");
	kp_t_ok(T, kp_snet_leave_game(w->seat[0].net, REASON_LEAVE), "leave");
	advance(w, now_of(w) + 100);
	kp_t_ok(T, listing(w, 2).count == 0, "the left game is listed");
}

/* A provider alone: what it sends is kept (the last datagram), its clock is `now`. */
typedef struct {
	uint32_t now;
	uint8_t last[KP_SNP_LIST_DATAGRAM];
	size_t last_size;
} Alone;

static void alone_send(void *ud, int socket, const KpNetAddr *to, const uint8_t *data, size_t size)
{
	Alone *a = (Alone *)ud;
	(void)socket;
	(void)to;
	if (size > sizeof a->last) size = sizeof a->last;
	memcpy(a->last, data, size);
	a->last_size = size;
}

static uint32_t alone_now(void *ud) { return ((Alone *)ud)->now; }

/* Advertisements from more addresses than KP_SNP_GAMES: the list keeps that many, the rest counted, until entries
 * expire; an advertiser already listed is still heard. */
static void game_list_is_bounded(KpT *T)
{
	Alone a;
	KpSnpUdp sender, p;
	KpNetIo io;
	const KpSnpGame *g;
	uint8_t ad[KP_SNP_LIST_DATAGRAM];
	size_t ad_size;
	int k, count;
	memset(&a, 0, sizeof a);
	a.now = START;
	io.send = alone_send;
	io.now = alone_now;
	io.ud = &a;
	kp_snp_udp_init(&sender, &io, PROGRAM, VERSION, 8);
	kp_t_ok(T, kp_snp_udp_advertise(&sender, "Flood", "", KP_SNET_MODE_ADVERTISED, NULL, 0) && a.last_size > 0,
		"an advertisement");
	memcpy(ad, a.last, a.last_size); /* the next provider's first game-list request is sent at its start */
	ad_size = a.last_size;
	kp_snp_udp_init(&p, &io, PROGRAM, VERSION, 8);
	for (k = 0; k < KP_SNP_GAMES + 44; k++) {
		KpNetAddr from = kp_net_addr(IP(k), KP_NET_LIST_PORT);
		kp_snp_udp_input(&p, KP_NET_SOCKET_LIST, &from, ad, ad_size);
	}
	for (count = 0, g = kp_snp_udp_lock_games(&p); g; g = g->next) count++;
	kp_snp_udp_unlock_games(&p);
	kp_t_ok(T, count == KP_SNP_GAMES && p.crowded == 44, "%d games listed, %ld crowded out", count, p.crowded);
	{ /* a listed advertiser's next advertisement replaces its entry */
		KpNetAddr from = kp_net_addr(IP(0), KP_NET_LIST_PORT);
		kp_snp_udp_input(&p, KP_NET_SOCKET_LIST, &from, ad, ad_size);
		kp_t_ok(T, p.crowded == 44, "a listed advertiser crowded out");
	}
	a.now += KP_SNP_GAME_LIFETIME + 1;
	kp_t_ok(T, kp_snp_udp_lock_games(&p) == NULL, "the entries expired");
	kp_snp_udp_unlock_games(&p);
	{
		KpNetAddr from = kp_net_addr(IP(KP_SNP_GAMES + 50), KP_NET_LIST_PORT);
		kp_snp_udp_input(&p, KP_NET_SOCKET_LIST, &from, ad, ad_size);
		kp_t_ok(T, (g = kp_snp_udp_lock_games(&p)) != NULL && g->next == NULL, "a new advertiser listed again");
		kp_snp_udp_unlock_games(&p);
	}
	kp_snp_udp_free(&p);
	kp_snp_udp_free(&sender);
}

static void two_seats_play_in_lockstep(KpT *T)
{
	World *w = world_new(T, 2, 2);
	host(w, 0, "Koprulu", "", 2);
	join(w, 1, "Koprulu");
	kp_t_ok(T, w->seat[0].id == 0 && w->seat[1].id == 1, "ids %d and %d", w->seat[0].id, w->seat[1].id);
	kp_t_ok(T, w->seat[1].templates == 1, "the joiner's template events: %d", w->seat[1].templates);
	start_playing(&w->seat[0]);
	start_playing(&w->seat[1]);
	play(w, 3000);
	/* the host's event 2 waits for its next kp_snet_receive_turns */
	kp_t_ok(T, w->seat[0].players == 1 && w->seat[1].players == 0, "player events: %d and %d", w->seat[0].players,
		w->seat[1].players);
	kp_t_ok(T, w->seat[0].received > 100 && w->seat[1].received + 2 >= w->seat[0].received &&
			w->seat[0].received + 2 >= w->seat[1].received,
		"turns received: %u and %u", w->seat[0].received, w->seat[1].received);
	kp_t_ok(T, w->seat[0].last[1] > 100 && w->seat[1].last[0] > 100, "each seat has the other's turns");
}

/* n seats on a lossy link: each seat gets the same turns in the same order; the run is the seed's. */
static uint64_t lossy_game(KpT *T, uint64_t seed, int n, uint64_t *turns)
{
	World *w = world_new(T, seed, n);
	KpNetLanStats st;
	uint32_t least;
	int k;
	kp_net_lan_link(w->lan, 2, 6, 100);
	host(w, 0, "Koprulu", "", (uint32_t)n);
	for (k = 1; k < n; k++) join(w, k, "Koprulu");
	for (k = 0; k < n; k++) start_playing(&w->seat[k]);
	play(w, 8000);
	for (k = 0; k < n; k++) kp_t_ok(T, w->seat[k].received > 40, "seat %d received %u turns", k, w->seat[k].received);
	/* the turns the slowest seat has seen are every seat's */
	least = w->seat[0].received;
	for (k = 1; k < n; k++)
		if (w->seat[k].received < least) least = w->seat[k].received;
	if (least > MAX_TURNS) least = MAX_TURNS;
	for (k = 1; k < n; k++)
		kp_t_ok(T, w->seat[k].by_turn[least - 1] == w->seat[0].by_turn[least - 1], "seat %d's turns differ from seat 0's",
			k);
	st = kp_net_lan_stats(w->lan);
	kp_t_ok(T, st.lost > 50, "only %ld datagrams lost", st.lost);
	*turns = w->seat[0].by_turn[least - 1];
	return w->trace;
}

static void four_seats_with_loss_are_deterministic(KpT *T)
{
	uint64_t a, b, ta, tb;
	a = lossy_game(T, 7, 4, &ta);
	b = lossy_game(T, 7, 4, &tb);
	kp_t_ok(T, a == b && ta == tb, "two runs of one seed differ");
}

static void joiner_enters_a_running_game(KpT *T)
{
	World *w = world_new(T, 3, 3);
	host(w, 0, "Koprulu", "", 3);
	start_playing(&w->seat[0]);
	play(w, 500);
	kp_t_ok(T, w->seat[0].received > 20, "the host alone received %u turns", w->seat[0].received);
	join(w, 1, "Koprulu");
	/* the joiner is joining until the host's turns reach its start turn */
	kp_t_ok(T, w->seat[0].players == 0, "the host's player events before the start turn: %d", w->seat[0].players);
	start_playing(&w->seat[1]);
	play(w, 500);
	kp_t_ok(T, w->seat[0].players == 1, "the host's player events after the start turn: %d", w->seat[0].players);
	join(w, 2, "Koprulu");
	start_playing(&w->seat[2]);
	play(w, 2000);
	kp_t_ok(T, w->seat[2].last[0] && w->seat[2].last[1] && w->seat[0].last[2] && w->seat[1].last[2],
		"the third seat and the others exchange turns");
	kp_t_ok(T, w->seat[1].players == 1 && w->seat[2].players == 0,
		"player events: seat 1 %d (seat 2 joined), seat 2 %d (the others were in before it)", w->seat[1].players,
		w->seat[2].players);
}

static void messages_keep_their_order(KpT *T)
{
	World *w = world_new(T, 4, 3);
	char text[16];
	int k, from;
	const uint8_t *data;
	uint32_t size;
	kp_net_lan_link(w->lan, 2, 8, 150);
	host(w, 0, "Koprulu", "", 3);
	start_playing(&w->seat[0]);
	join(w, 1, "Koprulu");
	start_playing(&w->seat[1]);
	join(w, 2, "Koprulu");
	start_playing(&w->seat[2]);
	for (k = 0; k < 20; k++) {
		snprintf(text, sizeof text, "m%d", k);
		kp_t_ok(T, kp_snet_send_message(w->seat[0].net, k % 2 ? -1 : -2, text, (uint32_t)strlen(text) + 1),
			"send message");
	}
	snprintf(text, sizeof text, "direct");
	kp_t_ok(T, kp_snet_send_message(w->seat[1].net, 2, text, (uint32_t)strlen(text) + 1), "send to 2");
	kp_t_ok(T, !kp_snet_send_message(w->seat[1].net, 5, text, 1) && kp_snet_error(w->seat[1].net) ==
		KP_SNET_ERROR_BAD_PLAYER, "send to an unknown player");
	advance(w, now_of(w) + 5000);
	for (k = 0; k < 20; k++) {
		kp_t_ok(T, kp_snet_receive_message(w->seat[1].net, &from, &data, &size), "seat 1's message %d", k);
		snprintf(text, sizeof text, "m%d", k);
		kp_t_ok(T, from == 0 && strcmp((const char *)data, text) == 0, "seat 1's message %d: \"%s\" from %d", k,
			(const char *)data, from);
	}
	kp_t_ok(T, !kp_snet_receive_message(w->seat[1].net, &from, &data, &size) &&
		kp_snet_error(w->seat[1].net) == KP_SNET_ERROR_NO_DATA, "seat 1 has more messages");
	/* the loopback ones: odd messages went to the host too */
	for (k = 1; k < 20; k += 2) {
		kp_t_ok(T, kp_snet_receive_message(w->seat[0].net, &from, &data, &size), "the host's message %d", k);
		snprintf(text, sizeof text, "m%d", k);
		kp_t_ok(T, from == 0 && strcmp((const char *)data, text) == 0, "the host's message: \"%s\"", (const char *)data);
	}
	/* seat 2: the host's twenty and seat 1's one, each sender's in order */
	{
		int host_n = 0, direct = 0;
		while (kp_snet_receive_message(w->seat[2].net, &from, &data, &size)) {
			if (from == 1) {
				direct++;
				kp_t_ok(T, strcmp((const char *)data, "direct") == 0, "seat 1's message");
				continue;
			}
			snprintf(text, sizeof text, "m%d", host_n++);
			kp_t_ok(T, from == 0 && strcmp((const char *)data, text) == 0, "seat 2's message \"%s\"", (const char *)data);
		}
		kp_t_ok(T, host_n == 20 && direct == 1, "seat 2 got %d and %d", host_n, direct);
	}
}

/* Seat `leaver` leaves after `turns` frames; every other seat removes it at the same turn. */
static void leave_is_seen_at_the_same_turn(KpT *T)
{
	World *w = world_new(T, 5, 4);
	int k, at = 0;
	kp_net_lan_link(w->lan, 2, 10, 50);
	host(w, 0, "Koprulu", "", 4);
	for (k = 1; k < 4; k++) join(w, k, "Koprulu");
	for (k = 0; k < 4; k++) start_playing(&w->seat[k]);
	play(w, 1500);
	w->seat[2].playing = 0;
	kp_t_ok(T, kp_snet_leave_game(w->seat[2].net, REASON_LEAVE), "leave");
	play(w, 3000);
	for (k = 0; k < 4; k++) {
		if (k == 2) continue;
		kp_t_ok(T, w->seat[k].removed[2] > 0 && w->seat[k].reason[2] == REASON_LEAVE,
			"seat %d: removal at %d, reason %#x", k, w->seat[k].removed[2] - 1, w->seat[k].reason[2]);
		if (!at) at = w->seat[k].removed[2];
		kp_t_ok(T, w->seat[k].removed[2] == at, "seat %d removed the leaver at turn %d, another at %d", k,
			w->seat[k].removed[2] - 1, at - 1);
		kp_t_ok(T, w->seat[k].received > w->seat[2].received + 20, "seat %d plays on (%u turns)", k,
			w->seat[k].received);
	}
	/* the turns of the leaver end where it left: its last turn number at each seat */
	kp_t_ok(T, w->seat[0].last[2] == w->seat[1].last[2] && w->seat[1].last[2] == w->seat[3].last[2] &&
			w->seat[0].last[2] == w->seat[2].sent,
		"the leaver's last turns %u %u %u, sent %u", w->seat[0].last[2], w->seat[1].last[2], w->seat[3].last[2],
		w->seat[2].sent);
}

static void host_leaves_and_the_lowest_id_hosts(KpT *T)
{
	World *w = world_new(T, 6, 3);
	Listing l;
	int k;
	host(w, 0, "Koprulu", "", 3);
	join(w, 1, "Koprulu");
	join(w, 2, "Koprulu");
	for (k = 0; k < 3; k++) start_playing(&w->seat[k]);
	play(w, 1000);
	w->seat[0].playing = 0;
	kp_t_ok(T, kp_snet_leave_game(w->seat[0].net, REASON_LEAVE), "leave");
	play(w, 2000);
	kp_t_ok(T, kp_snet_is_host(w->seat[1].net, 1) == 1 && kp_snet_is_host(w->seat[2].net, 1) == 1 &&
			kp_snet_is_host(w->seat[2].net, 2) == 0,
		"host flags after the host left: %d %d %d", kp_snet_is_host(w->seat[1].net, 1),
		kp_snet_is_host(w->seat[2].net, 1), kp_snet_is_host(w->seat[2].net, 2));
	/* the new host advertises the game: a fourth player could find it */
	advance(w, now_of(w) + 200);
	l = listing(w, 2);
	kp_t_ok(T, l.count == 1 && l.players == 2, "the game listed %d times with %u players", l.count, l.players);
}

static void joins_are_refused(KpT *T)
{
	World *w = world_new(T, 7, 4);
	uint32_t error;
	int ok;
	/* each join before its check: the message's error is read with no order among the arguments */
	host(w, 0, "Koprulu", "Secret", 2);
	ok = try_join(w, 1, "Koprulu", "wrong", &error);
	kp_t_ok(T, !ok && error == KP_SNET_ERROR_REJECTED, "a wrong password: error %#x", error);
	ok = try_join(w, 1, "koprulu", "SECRET", &error);
	kp_t_ok(T, ok, "the password is compared without case: error %#x", error);
	ok = try_join(w, 2, "Koprulu", "Secret", &error);
	kp_t_ok(T, !ok && error == KP_SNET_ERROR_REJECTED, "a full game: error %#x", error);
	ok = try_join(w, 2, "Nowhere", "", &error);
	kp_t_ok(T, !ok && error == KP_SNET_ERROR_NOT_FOUND, "no such game: error %#x", error);
	/* the host's cable pulled: the join's first wait runs out after max(12L, 5000) ms */
	{
		uint32_t before;
		World *v = world_new(T, 8, 2);
		host(v, 0, "Open", "", 2);
		advance(v, now_of(v) + 500);
		kp_net_lan_cut(v->lan, 0, 1);
		before = now_of(v);
		v->in_join = 1;
		ok = kp_snet_join_game(v->seat[1].net, 0, "Open", "", "Seat", "", &v->seat[1].id);
		v->in_join = -1;
		error = kp_snet_error(v->seat[1].net);
		kp_t_ok(T, !ok && error == KP_SNET_ERROR_UNREACHABLE, "an unreachable host: error %#x", error);
		kp_t_ok(T, now_of(v) - before >= 5000 && now_of(v) - before < 5100, "the join waited %u ms", now_of(v) - before);
		kp_t_ok(T, kp_snet_local_id(v->seat[1].net) == KP_SNET_NO_ID, "the failed joiner has an id");
	}
	/* a closed game refuses */
	{
		World *v = world_new(T, 9, 2);
		host(v, 0, "Closed", "", 4);
		kp_t_ok(T, kp_snet_set_game_mode(v->seat[0].net, KP_SNET_MODE_CLOSED | KP_SNET_MODE_ADVERTISED, 0), "close");
		ok = try_join(v, 1, "Closed", "", &error);
		kp_t_ok(T, !ok && error == KP_SNET_ERROR_REJECTED, "a closed game: error %#x", error);
	}
}

static void a_silent_seat_is_dropped(KpT *T)
{
	World *w = world_new(T, 10, 3);
	const uint8_t *data[SEATS];
	uint32_t sizes[SEATS], status[SEATS];
	int k, ok;
	host(w, 0, "Koprulu", "", 3);
	join(w, 1, "Koprulu");
	join(w, 2, "Koprulu");
	for (k = 0; k < 3; k++) start_playing(&w->seat[k]);
	play(w, 1000);
	kp_net_lan_cut(w->lan, w->seat[2].host, 1);
	w->seat[2].playing = 0;
	play(w, 2000);
	/* the others wait: seat 2 is present, without its turn, not heard for a second */
	kp_t_ok(T, !kp_snet_receive_turns(w->seat[0].net, 0, SEATS, data, sizes, status), "the host has its turn");
	kp_t_ok(T, (status[2] & (KP_SNET_PRESENT | KP_SNET_HAS_TURN | KP_SNET_RESPONDING)) == KP_SNET_PRESENT,
		"seat 2's status %#x", status[2]);
	kp_t_ok(T, (status[1] & KP_SNET_RESPONDING) && (status[0] & KP_SNET_RESPONDING), "the others' status");
	ok = kp_snet_drop_player(w->seat[0].net, 2, REASON_DROP);
	kp_t_ok(T, ok, "drop: error %#x", kp_snet_error(w->seat[0].net));
	kp_t_ok(T, !kp_snet_drop_player(w->seat[0].net, 0, REASON_DROP) &&
		kp_snet_error(w->seat[0].net) == KP_SNET_ERROR_PARAMETER, "the local seat dropped");
	play(w, 2000);
	for (k = 0; k < 2; k++)
		kp_t_ok(T, w->seat[k].removed[2] && w->seat[k].reason[2] == REASON_DROP, "seat %d: removal %d, reason %#x", k,
			w->seat[k].removed[2], w->seat[k].reason[2]);
	kp_t_ok(T, w->seat[0].received > 100 && w->seat[1].received > 100, "the others play on: %u and %u turns",
		w->seat[0].received, w->seat[1].received);
}

/* Storm's timers: a message to a silent seat is retransmitted every (maxmsg + 128 + length)·1000/bytesPerSec +
 * 2·(max(L, 50) + 100) ms while the pump thread is woken (here every millisecond); without a wake-up after a
 * retransmission the thread waits for good. */
typedef struct {
	uint32_t at[64];
	int count, host;
	uint8_t cls;
} Sends;

static void record_sends(void *ud, uint32_t at, int host_index, int socket, const KpNetAddr *to, const uint8_t *data,
	size_t size)
{
	Sends *sends = (Sends *)ud;
	(void)to;
	if (host_index != sends->host || socket != KP_NET_SOCKET_GAME || size < KP_SNET_HEADER || data[8] != sends->cls ||
		data[11] != 0 || sends->count >= 64)
		return;
	sends->at[sends->count++] = at;
}

static void timers_are_storms(KpT *T)
{
	World *w = world_new(T, 11, 2);
	static const char text[] = "hello";
	uint32_t interval = (488 + 128 + KP_SNET_HEADER + sizeof text) * 1000 / 100000 + 2 * (50 + 100);
	Sends sends;
	int k;
	host(w, 0, "Koprulu", "", 2);
	join(w, 1, "Koprulu");
	advance(w, now_of(w) + 1000);
	memset(&sends, 0, sizeof sends);
	sends.host = w->seat[0].host;
	sends.cls = 1;
	kp_net_lan_tap(w->lan, record_sends, &sends);
	kp_net_lan_cut(w->lan, w->seat[1].host, 1);
	kp_t_ok(T, kp_snet_send_message(w->seat[0].net, 1, text, sizeof text), "send");
	network(w, now_of(w) + 2000);
	kp_t_ok(T, sends.count == 2, "%d sends of the message in 2 s without wake-ups", sends.count);
	sends.count = 0;
	for (k = 0; k < 2000; k++) {
		w->seat[0].signalled = 1;
		network(w, now_of(w) + 1);
	}
	kp_t_ok(T, sends.count >= 6, "%d sends of the message in 2 s", sends.count);
	for (k = 1; k < sends.count; k++)
		kp_t_ok(T, sends.at[k] - sends.at[k - 1] == interval, "resend %d after %u ms, not %u", k,
			sends.at[k] - sends.at[k - 1], interval);
	kp_net_lan_cut(w->lan, w->seat[1].host, 0);
	advance(w, now_of(w) + 1000);
	{
		int from;
		const uint8_t *data;
		uint32_t size;
		kp_t_ok(T, kp_snet_receive_message(w->seat[1].net, &from, &data, &size) && from == 0 && size == sizeof text,
			"the message arrived after the cable came back");
	}
	/* acknowledged: no more resends */
	sends.count = 0;
	advance(w, now_of(w) + 2000);
	kp_t_ok(T, sends.count == 0, "%d resends after the ack", sends.count);
	kp_net_lan_tap(w->lan, tap, w);
}

/* Garbage on port 6112 (short, bad lengths, bad sums, unknown classes, spoofed senders) changes nothing. */
static void garbage_is_ignored(KpT *T)
{
	World *w = world_new(T, 12, 2);
	uint64_t rng = 99;
	int k;
	host(w, 0, "Koprulu", "", 2);
	join(w, 1, "Koprulu");
	start_playing(&w->seat[0]);
	start_playing(&w->seat[1]);
	for (k = 0; k < 3000; k++) {
		uint8_t d[64];
		size_t n, j;
		/* a sender byte no seat has: Storm trusts what names a peer (relays, control commands) */
		KpNetAddr from = kp_net_addr(IP(3 + k % 200), (uint16_t)(k % 2 ? KP_NET_GAME_PORT : 1234));
		rng = rng * 6364136223846793005ull + 1442695040888963407ull;
		n = (size_t)(rng >> 33) % sizeof d;
		for (j = 0; j < n; j++) {
			rng = rng * 6364136223846793005ull + 1442695040888963407ull;
			d[j] = (uint8_t)(rng >> 40);
		}
		if (n >= KP_SNET_HEADER) d[10] |= 0x80;
		if (n >= KP_SNET_HEADER && k % 3 == 0) {
			kp_net_put16(d + 2, (uint32_t)n);
			d[8] &= 3;
			d[11] &= 7;
			kp_net_checksum(d, n);
		}
		seat_deliver(&w->seat[0], KP_NET_SOCKET_GAME, &from, d, n);
		if (k % 50 == 0) play(w, STEP);
	}
	play(w, 2000);
	kp_t_ok(T, w->seat[0].received > 100 && w->seat[1].received > 100, "turns received: %u and %u",
		w->seat[0].received, w->seat[1].received);
	kp_t_ok(T, kp_snet_remote_count(w->seat[0].net) <= KP_SNET_RECORDS, "%d records", kp_snet_remote_count(w->seat[0].net));
}

/* Three seats on the system's sockets over 127.0.0.1: the two hosts of a map (Koprulu's own games on one machine) and
 * a program the map does not hold (Koprulu against the network oracle's bridge), on the two ports above the map's.
 * They list and join the game, play in lockstep, and see a leave at one turn. */
static void sockets_carry_a_game(KpT *T)
{
	World *w;
	char error[128] = "";
	int attempt, k = 0, at = 0;
#ifdef __EMSCRIPTEN__
	kp_t_skip(T, "no UDP sockets on the web (kp_net_sock.h: its datagrams go through the relay of stage N5)");
#endif
	w = world_alloc(T);
	for (attempt = 0; attempt < 32; attempt++) {
		/* six free ports from an even base: the map's four and the outsider's two */
		uint32_t base = 20000u + (kp_net_sock_now() / 2u + (uint32_t)attempt * 7919u) % 20000u * 2u;
		KpNetSockConfig c;
		memset(&c, 0, sizeof c);
		c.map_base = (uint16_t)base;
		c.map_hosts = 2;
		for (k = 0; k < 3; k++) {
			c.map_self = k < 2 ? k : -1;
			c.port = k < 2 ? 0 : (uint16_t)(base + 4);
			if (!(w->seat[k].sock = kp_net_sock_open(&c, error, sizeof error))) break;
			kp_t_ok(T, kp_net_sock_port(w->seat[k].sock, KP_NET_SOCKET_GAME) == base + 2u * (uint32_t)k + 1u,
				"seat %d's socket B at %u", k, kp_net_sock_port(w->seat[k].sock, KP_NET_SOCKET_GAME));
		}
		if (k == 3) break;
		while (k--) {
			kp_net_sock_close(w->seat[k].sock);
			w->seat[k].sock = NULL;
		}
	}
	kp_t_ok(T, attempt < 32, "no six free ports: %s", error);
	for (k = 0; k < 3; k++) {
		KpNetIo io = kp_net_sock_io(w->seat[k].sock);
		seat_add(w, &io);
	}
	host(w, 0, "Koprulu", "", 3);
	for (k = 1; k < 3; k++) {
		uint32_t until = now_of(w) + 3000, e = 0;
		int ok;
		while (listing(w, k).count == 0 && (int32_t)(now_of(w) - until) < 0) advance(w, now_of(w) + 20);
		ok = enter(w, k, "Koprulu", "", &e);
		kp_t_ok(T, ok, "seat %d's join: error %#x", k, e);
	}
	kp_t_ok(T, w->seat[1].id == 1 && w->seat[2].id == 2, "ids %d and %d", w->seat[1].id, w->seat[2].id);
	for (k = 0; k < 3; k++) start_playing(&w->seat[k]);
	play(w, 1000);
	for (k = 0; k < 3; k++) kp_t_ok(T, w->seat[k].received > 20, "seat %d received %u turns", k, w->seat[k].received);
	w->seat[2].playing = 0;
	kp_t_ok(T, kp_snet_leave_game(w->seat[2].net, REASON_LEAVE), "leave");
	play(w, 1000);
	for (k = 0; k < 2; k++) {
		kp_t_ok(T, w->seat[k].removed[2] > 0 && w->seat[k].reason[2] == REASON_LEAVE, "seat %d: removal at %d, reason %#x",
			k, w->seat[k].removed[2] - 1, w->seat[k].reason[2]);
		if (!at) at = w->seat[k].removed[2];
		kp_t_ok(T, w->seat[k].removed[2] == at, "seat %d removed the leaver at turn %d, another at %d", k,
			w->seat[k].removed[2] - 1, at - 1);
	}
	kp_t_ok(T, w->seat[0].last[2] == w->seat[2].sent && w->seat[1].last[2] == w->seat[2].sent,
		"the leaver's last turns %u and %u, sent %u", w->seat[0].last[2], w->seat[1].last[2], w->seat[2].sent);
	{
		uint32_t least = w->seat[0].received < w->seat[1].received ? w->seat[0].received : w->seat[1].received;
		if (least > MAX_TURNS) least = MAX_TURNS;
		kp_t_ok(T, w->seat[0].by_turn[least - 1] == w->seat[1].by_turn[least - 1], "the seats' turns differ");
	}
	for (k = 0; k < 3; k++)
		kp_t_ok(T, kp_net_sock_send_errors(w->seat[k].sock) == 0, "seat %d: %ld datagrams refused", k,
			kp_net_sock_send_errors(w->seat[k].sock));
}

/* Koprulu against the original (phase N, stage N2): the network oracle's emulated StarCraft 1.17 hosts a game on
 * the loopback bridge (tools/research/sc_emulator net MAP live --players Alpha@10.0.0.1:host --wait-players 2
 * --bridge BASE --realtime) and a seat on the system's sockets, a program the bridge's map does not hold, lists it,
 * joins it through Storm's handshake (the host's template arrives) and plays the lobby's lockstep: two keep-alive
 * turns ahead, one for each turn set received, as StarCraft's lobby does. The host's turns keep coming only while its
 * Storm takes the seat's. StarCraft's lobby protocol is stage N3's: the host may drop the seat after its lobby
 * timeout, past this test's turns. Skipped without KOPRULU_NET_BRIDGE. */
/* The lobby's turn, a keep-alive command, as StarCraft sends it. */
static void keep_alive(KpT *T, Seat *st)
{
	static const uint8_t turn = KEEP_ALIVE;
	int ok = kp_snet_send_turn(st->net, &turn, 1); /* before the message reads the error */
	kp_t_ok(T, ok, "a turn: error %#x", kp_snet_error(st->net));
}

static void live_bridge_join(KpT *T)
{
	const char *env = getenv(LIVE_ENV), *colon;
	World *w;
	Seat *st;
	KpNetSockConfig c;
	KpNetIo io;
	Listing l;
	char error[128] = "";
	uint32_t until, e = 0, host_turns = 0, sets = 0;
	uint8_t first[64];
	size_t first_size = 0;
	int attempt, k, ok;
	if (!env || !*env) kp_t_skip(T, "no " LIVE_ENV " (the emulator's --bridge base port)");
	w = world_alloc(T);
	memset(&c, 0, sizeof c);
	c.map_base = (uint16_t)strtoul(env, NULL, 10);
	c.map_hosts = (colon = strchr(env, ':')) ? atoi(colon + 1) : 1;
	c.map_self = -1;
	for (attempt = 0; attempt < 16 && !w->seat[0].sock; attempt++) { /* two ports of its own above the bridge's */
		c.port = (uint16_t)(c.map_base + 2 * c.map_hosts + 16 + 2 * attempt);
		w->seat[0].sock = kp_net_sock_open(&c, error, sizeof error);
	}
	kp_t_ok(T, w->seat[0].sock != NULL, "the seat's sockets: %s", error);
	io = kp_net_sock_io(w->seat[0].sock);
	st = seat_add(w, &io);
	until = now_of(w) + LIVE_LIST_MS;
	do {
		network(w, now_of(w) + 100);
		l = listing(w, 0);
	} while (!l.count && (int32_t)(now_of(w) - until) < 0);
	kp_t_ok(T, l.count > 0, "no game listed in %d s", LIVE_LIST_MS / 1000);
	kp_t_log(T, "listed: \"%s\", %u of %u players", l.name, l.players, l.max_players);
	advance(w, now_of(w) + 1500); /* the host takes a connect again after max(4L, 1000) ms */
	ok = enter(w, 0, l.name, "", &e); /* before the message reads e */
	kp_t_ok(T, ok, "the join: error %#x", e);
	kp_t_ok(T, st->templates == 1, "%d templates", st->templates);
	kp_t_log(T, "joined as player %d", st->id);
	for (k = 0; k < IN_TRANSIT; k++) keep_alive(T, st);
	until = now_of(w) + LIVE_PLAY_MS;
	while (host_turns < LIVE_TURNS && (int32_t)(now_of(w) - until) < 0) {
		const uint8_t *data[SEATS];
		uint32_t sizes[SEATS], status[SEATS];
		network(w, now_of(w) + STEP);
		if (!kp_snet_receive_turns(st->net, 0, SEATS, data, sizes, status)) {
			kp_t_ok(T, kp_snet_error(st->net) == KP_SNET_ERROR_NO_DATA, "receive: error %#x", kp_snet_error(st->net));
			continue;
		}
		sets++;
		for (k = 0; k < SEATS; k++) {
			if (k == st->id || !data[k]) continue;
			if (!host_turns++ && sizes[k] <= sizeof first) memcpy(first, data[k], first_size = sizes[k]);
		}
		keep_alive(T, st);
	}
	{
		char hex[2 * sizeof first + 1];
		size_t j;
		for (j = 0; j < first_size; j++) snprintf(hex + 2 * j, 3, "%02x", first[j]);
		hex[2 * first_size] = 0;
		kp_t_log(T, "%u turn sets, %u of the host's turns (the first: %s)", sets, host_turns, hex);
	}
	kp_t_ok(T, host_turns >= LIVE_TURNS, "%u of the host's turns in %d s", host_turns, LIVE_PLAY_MS / 1000);
	ok = kp_snet_leave_game(st->net, REASON_LEAVE);
	kp_t_ok(T, ok, "leave: error %#x", kp_snet_error(st->net));
	network(w, now_of(w) + 500);
	kp_t_ok(T, kp_net_sock_send_errors(w->seat[0].sock) == 0, "%ld datagrams refused",
		kp_net_sock_send_errors(w->seat[0].sock));
}

static void api_errors(KpT *T)
{
	World *w = world_new(T, 13, 1);
	KpSnet *s = w->seat[0].net;
	KpSnetCaps caps;
	char name[32];
	uint32_t n = 0;
	int id = 5, first, last, active, ok;
	kp_t_ok(T, !kp_snet_send_turn(s, "x", 1) && kp_snet_error(s) == KP_SNET_ERROR_NOT_IN_GAME, "send outside a game");
	kp_t_ok(T, !kp_snet_create_game(s, "", "", "", 0, 0, 0, NULL, 0, 2, "P", "", &id) && id == -1 &&
		kp_snet_error(s) == KP_SNET_ERROR_PARAMETER, "an empty game name");
	memset(&caps, 0, sizeof caps);
	kp_t_ok(T, !kp_snet_provider_caps(s, &caps) && kp_snet_error(s) == KP_SNET_ERROR_PARAMETER, "caps of size 0");
	caps.size = sizeof caps;
	/* each call before its check: a message's outputs are read with no order among the arguments */
	ok = kp_snet_provider_caps(s, &caps);
	kp_t_ok(T, ok && caps.maxmsg == 488 && caps.latency == 50 && caps.turns_in_transit == 2,
		"the provider's caps: maxmsg %u", caps.maxmsg);
	host(w, 0, "Koprulu", "pw", 4);
	ok = kp_snet_game_info(s, KP_SNET_INFO_NAME, NULL, 0, &n);
	kp_t_ok(T, ok && n == 8, "the name's size %u", n);
	kp_t_ok(T, kp_snet_game_info(s, KP_SNET_INFO_NAME, name, 4, &n) && n == 4 && strcmp(name, "Kop") == 0,
		"a cut name \"%s\"", name);
	ok = kp_snet_game_info(s, KP_SNET_INFO_MODE, &n, 4, NULL);
	kp_t_ok(T, ok && n == (KP_SNET_MODE_PASSWORD | KP_SNET_MODE_ADVERTISED), "the mode %#x", n);
	ok = kp_snet_game_info(s, KP_SNET_INFO_TEMPLATE, NULL, 0, &n);
	kp_t_ok(T, ok && n == 32, "the template's size %u", n);
	kp_t_ok(T, !kp_snet_game_info(s, 7, NULL, 0, &n) && kp_snet_error(s) == KP_SNET_ERROR_PARAMETER, "type 7");
	ok = kp_snet_num_players(s, &first, &last, &active);
	kp_t_ok(T, ok && first == 0 && last == 0 && active == 1, "players %d..%d, %d", first, last, active);
	kp_t_ok(T, kp_snet_player_name(s, 0, name, sizeof name) && strcmp(name, "Host") == 0, "the host's name \"%s\"",
		name);
	kp_t_ok(T, !kp_snet_player_name(s, 1, name, sizeof name) && kp_snet_error(s) == KP_SNET_ERROR_BAD_PLAYER,
		"player 1's name");
	kp_t_ok(T, kp_snet_set_game_mode(s, KP_SNET_MODE_PASSWORD | KP_SNET_MODE_ADVERTISED, 1) &&
			kp_snet_game_info(s, KP_SNET_INFO_PASSWORD, name, sizeof name, &n) && name[0] == 0,
		"a private game keeps its password \"%s\"", name);
	ok = kp_snet_turns_in_transit(s, &n);
	kp_t_ok(T, ok && n == 0, "turns in transit %u", n);
	start_playing(&w->seat[0]);
	ok = kp_snet_turns_in_transit(s, &n);
	kp_t_ok(T, ok && n == IN_TRANSIT, "turns in transit %u", n);
	/* the slots outside 0..254 have no record: slot 256 is not the host's slot 0 again (its next turn taken), and a
	 * slot range from -1 does not wrap to none (the call succeeding without the host's turn) */
	{
		const uint8_t *data[257];
		uint32_t sizes[257], status[257];
		ok = kp_snet_receive_turns(s, 0, 257, data, sizes, status);
		kp_t_ok(T, ok && data[0] && kp_net_get32(data[0] + 2) == 0 && !data[256] && status[256] == 0,
			"slots 0 and 256: status %#x and %#x", status[0], status[256]);
		ok = kp_snet_receive_turns(s, -1, 2, data, sizes, status);
		kp_t_ok(T, ok && !data[0] && status[0] == 0 && data[1] && kp_net_get32(data[1] + 2) == 1,
			"slots -1 and 0: status %#x and %#x", status[0], status[1]);
	}
}

static const KpTest TESTS[] = {
	{ "checksum_vectors", checksum_vectors, 0 },
	{ "game_list_follows_the_advertisements", game_list_follows_the_advertisements, 0 },
	{ "game_list_is_bounded", game_list_is_bounded, 0 },
	{ "two_seats_play_in_lockstep", two_seats_play_in_lockstep, 0 },
	{ "four_seats_with_loss_are_deterministic", four_seats_with_loss_are_deterministic, 0 },
	{ "joiner_enters_a_running_game", joiner_enters_a_running_game, 0 },
	{ "messages_keep_their_order", messages_keep_their_order, 0 },
	{ "leave_is_seen_at_the_same_turn", leave_is_seen_at_the_same_turn, 0 },
	{ "host_leaves_and_the_lowest_id_hosts", host_leaves_and_the_lowest_id_hosts, 0 },
	{ "joins_are_refused", joins_are_refused, 0 },
	{ "a_silent_seat_is_dropped", a_silent_seat_is_dropped, 0 },
	{ "timers_are_storms", timers_are_storms, 0 },
	{ "garbage_is_ignored", garbage_is_ignored, 0 },
	{ "sockets_carry_a_game", sockets_carry_a_game, 0 },
	{ "live_bridge_join", live_bridge_join, 0 },
	{ "api_errors", api_errors, 0 },
};

KP_SUITE(kp_suite_snet, "test_snet", TESTS);
