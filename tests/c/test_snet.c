/* test_snet: Storm's SNet (kp_snet.h) and the UDP provider (kp_snp_udp.h) over the in-process LAN (kp_net_lan.h)
 * and over the system's sockets (kp_net_sock.h); phase N, stage N2. Seats are whole SNet stacks in the network tests'
 * world (kp_net_world.h), which pumps each one as Storm's pump thread does; each seat's frame plays StarCraft's
 * lockstep: two turns in transit, a turn sent for each turn received. */
#include "kp_test.h"

#include "kp_net_world.h"

#define PROGRAM KP_NET_WORLD_PROGRAM
#define VERSION KP_NET_WORLD_VERSION
#define START KP_NET_WORLD_START
#define SEATS KP_NET_WORLD_SEATS
#define IP(k) KP_NET_WORLD_IP(k)
#define TURN_SIZE 8
#define IN_TRANSIT 2
#define STEP KP_NET_WORLD_STEP
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
	KpNetSeat n; /* first: the world's callbacks cast it back */
	int id;      /* the seat's SNet id, -1 outside a game */
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
	KpNetWorld n; /* its seats are seat[k].n */
	Seat seat[SEATS];
	uint32_t forged; /* strangers_cannot_steer_a_join: the control packets forged during the join */
};

#define fnv kp_net_world_fnv

static uint32_t now_of(World *w) { return kp_net_world_now(&w->n); }
static void pump_due(World *w) { kp_net_world_pump(&w->n); }
static void network(World *w, uint32_t to) { kp_net_world_network(&w->n, to); }
static void advance(World *w, uint32_t to) { kp_net_world_advance(&w->n, to); }

static int frame(Seat *st);

static void world_frame(KpNetSeat *s) { frame((Seat *)s); }

static void world_event(KpNetSeat *s, const KpSnetEvent *e)
{
	Seat *st = (Seat *)s;
	if (e->id == KP_SNET_EVENT_TEMPLATE) st->templates++;
	else if (e->id == KP_SNET_EVENT_PLAYER) st->players++;
	else if (e->id == KP_SNET_EVENT_REMOVED && e->player >= 0 && e->player < SEATS) {
		st->removed[e->player] = (int)kp_snet_local_turn(st->n.net) + 1;
		st->reason[e->player] = e->size == 4 ? kp_net_get32(e->data) : 0xdeadu;
	}
}

static void world_free(void *p)
{
	World *w = (World *)p;
	int k;
	kp_net_world_release(&w->n);
	for (k = 0; k < SEATS; k++) kp_net_sock_close(w->seat[k].n.sock);
	free(w);
}

static World *world_alloc(KpT *T)
{
	World *w = (World *)calloc(1, sizeof *w);
	kp_t_ok(T, w != NULL, "out of memory");
	kp_t_defer(T, world_free, w);
	kp_net_world_init(&w->n, T, kp_t_fail_text, T);
	w->n.frame = world_frame;
	w->n.event = world_event;
	return w;
}

/* The next seat's SNet stack over `io`. */
static Seat *seat_add(World *w, const KpNetIo *io)
{
	Seat *st = &w->seat[w->n.count];
	st->id = -1;
	kp_net_world_add(&w->n, &st->n, io);
	return st;
}

/* The LAN's seats; broadcasts 0: none crosses from the start (the seats' first game-list requests too). */
static World *world_lan(KpT *T, uint64_t seed, int seats, int broadcasts)
{
	World *w = world_alloc(T);
	int k;
	kp_net_world_lan(&w->n, seed);
	kp_net_lan_broadcasts(w->n.lan, broadcasts);
	for (k = 0; k < seats; k++) {
		w->seat[k].id = -1;
		kp_net_world_add_lan(&w->n, &w->seat[k].n);
	}
	return w;
}

static World *world_new(KpT *T, uint64_t seed, int seats) { return world_lan(T, seed, seats, 1); }

static void host(World *w, int k, const char *name, const char *password, uint32_t max_players)
{
	static const uint8_t tmpl[32] = {2, 0, 1, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		0, 0, 0, 0};
	Seat *st = &w->seat[k];
	int ok = kp_snet_create_game(st->n.net, name, password, "stats", 0x10002, 0, 0, tmpl, sizeof tmpl, max_players,
		"Host", "", &st->id);
	kp_t_ok(w->n.T, ok, "create game: error %#x", kp_snet_error(st->n.net));
	pump_due(w);
}

/* kp_snet_join_game by name now; returns its result (the error in *error). */
static int enter(World *w, int k, const char *name, const char *password, uint32_t *error)
{
	Seat *st = &w->seat[k];
	char player[16];
	int ok;
	snprintf(player, sizeof player, "Seat %d", k);
	w->n.in_join = k;
	ok = kp_snet_join_game(st->n.net, 0, name, password, player, "", &st->id);
	w->n.in_join = -1;
	if (error) *error = ok ? 0 : kp_snet_error(st->n.net);
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
	kp_t_ok(w->n.T, ok, "seat %d's join: error %#x", k, error);
}

static void send_turn(Seat *st)
{
	uint8_t t[TURN_SIZE];
	int ok;
	t[0] = (uint8_t)st->n.index;
	t[1] = 0x05;
	kp_net_put32(t + 2, st->sent);
	t[6] = 0xab;
	t[7] = 0xcd;
	ok = kp_snet_send_turn(st->n.net, t, sizeof t); /* before the message reads the error */
	kp_t_ok(st->n.w->T, ok, "seat %d sends: error %#x", st->n.index, kp_snet_error(st->n.net));
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
	if (!kp_snet_receive_turns(st->n.net, 0, SEATS, data, sizes, status)) {
		uint32_t e = kp_snet_error(st->n.net);
		kp_t_ok(st->n.w->T, e == KP_SNET_ERROR_NO_DATA, "seat %d receives: error %#x", st->n.index, e);
		return 0;
	}
	for (k = 0; k < SEATS; k++) {
		if (!data[k]) continue;
		kp_t_ok(st->n.w->T, sizes[k] == TURN_SIZE && data[k][6] == 0xab, "seat %d: slot %d's turn is %u bytes",
			st->n.index, k, sizes[k]);
		if (st->last[k])
			kp_t_ok(st->n.w->T, kp_net_get32(data[k] + 2) == st->last[k], "seat %d: slot %d's turn %u after %u",
				st->n.index, k, kp_net_get32(data[k] + 2), st->last[k] - 1);
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
	kp_t_ok(w->n.T, kp_snet_enum_games(w->seat[k].n.net, list_game, &l, &hint), "enum");
	kp_t_ok(w->n.T, hint == KP_SNP_ENUM_HINT, "the hint %u", hint);
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
	kp_t_ok(T, kp_snet_set_game_mode(w->seat[0].n.net, KP_SNET_MODE_CLOSED | KP_SNET_MODE_ADVERTISED, 0), "set mode");
	advance(w, now_of(w) + 100);
	kp_t_ok(T, listing(w, 2).count == 0, "a closed game is listed");
	/* leaving stops the advertisement */
	kp_t_ok(T, kp_snet_set_game_mode(w->seat[0].n.net, KP_SNET_MODE_ADVERTISED, 0), "set mode");
	advance(w, now_of(w) + 100);
	kp_t_ok(T, listing(w, 2).count == 1, "the reopened game is listed");
	kp_t_ok(T, kp_snet_leave_game(w->seat[0].n.net, REASON_LEAVE), "leave");
	advance(w, now_of(w) + 100);
	kp_t_ok(T, listing(w, 2).count == 0, "the left game is listed");
}

/* A provider alone: what it sends is kept (the last datagram, its socket and destination; the count), its clock is
 * `now`. */
typedef struct {
	uint32_t now;
	uint8_t last[KP_SNP_LIST_DATAGRAM];
	size_t last_size;
	int last_socket, sends;
	KpNetAddr last_to;
} Alone;

static void alone_send(void *ud, int socket, const KpNetAddr *to, const uint8_t *data, size_t size)
{
	Alone *a = (Alone *)ud;
	if (size > sizeof a->last) size = sizeof a->last;
	memcpy(a->last, data, size);
	a->last_size = size;
	a->last_socket = socket;
	a->last_to = *to;
	a->sends++;
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

/* The host's address as the player gives it (kp_net_host_parse) and a dotted IPv4 address (kp_net_ipv4_parse). */
static void host_address_parsing(KpT *T)
{
	static const struct {
		const char *text;
		int ok;
		const char *name;
		uint16_t port;
	} HOSTS[] = {
		{"game.example.net", 1, "game.example.net", 6111},
		{"  10.0.0.5:7000 ", 1, "10.0.0.5", 7000},
		{"a_b-c:65534", 1, "a_b-c", 65534},
		{"host:007", 1, "host", 7},
		{"", 0, NULL, 0},
		{"   ", 0, NULL, 0},
		{":7000", 0, NULL, 0},
		{"host:", 0, NULL, 0},
		{"host:0", 0, NULL, 0},
		{"host:65535", 0, NULL, 0},
		{"host:99999999999", 0, NULL, 0},
		{"host:70a", 0, NULL, 0},
		{"host:1:2", 0, NULL, 0},
		{"ho st", 0, NULL, 0},
		{"[::1]", 0, NULL, 0},
		{"h\xc3\xb6st", 0, NULL, 0},
		{"1.2.3", 0, NULL, 0},
		{"192.168.1:7000", 0, NULL, 0},
		{"123", 0, NULL, 0},
		{"256.1.1.1", 0, NULL, 0},
		{"1.2.3.4x", 1, "1.2.3.4x", 6111},
		{"3com.example", 1, "3com.example", 6111},
	};
	static const struct {
		const char *text;
		int ok;
		uint32_t ip;
	} IPS[] = {
		{"10.0.0.5", 1, 0x0a000005u},
		{"255.255.255.255", 1, 0xffffffffu},
		{"0.0.0.0", 1, 0},
		{"256.0.0.1", 0, 0},
		{"0001.2.3.4", 0, 0},
		{"1.2.3", 0, 0},
		{"1.2.3.4.5", 0, 0},
		{"1..2.3", 0, 0},
		{"1.2.3.4 ", 0, 0},
		{"a.b.c.d", 0, 0},
		{"", 0, 0},
	};
	char name[KP_NET_HOST], text[KP_NET_HOST + 8];
	uint16_t port;
	uint32_t ip;
	size_t k;
	for (k = 0; k < sizeof HOSTS / sizeof HOSTS[0]; k++) {
		int ok = kp_net_host_parse(HOSTS[k].text, name, sizeof name, &port);
		kp_t_ok(T, ok == HOSTS[k].ok && (!ok || (strcmp(name, HOSTS[k].name) == 0 && port == HOSTS[k].port)),
			"\"%s\": %d, \"%s\" port %u", HOSTS[k].text, ok, ok ? name : "", ok ? port : 0);
	}
	memset(text, 'n', KP_NET_HOST - 1);
	text[KP_NET_HOST - 1] = 0;
	kp_t_ok(T, kp_net_host_parse(text, name, sizeof name, &port), "a name of %d characters", KP_NET_HOST - 1);
	strcat(text, "n");
	kp_t_ok(T, !kp_net_host_parse(text, name, sizeof name, &port), "a name of %d characters", KP_NET_HOST);
	kp_t_ok(T, !kp_net_host_parse("host", name, KP_NET_HOST - 1, &port), "a short name buffer");
	for (k = 0; k < sizeof IPS / sizeof IPS[0]; k++) {
		int ok = kp_net_ipv4_parse(IPS[k].text, &ip);
		kp_t_ok(T, ok == IPS[k].ok && (!ok || ip == IPS[k].ip), "\"%s\": %d, %#x", IPS[k].text, ok, ok ? ip : 0);
	}
}

/* A lookup's first answer (kp_net_lookup_poll), and the answer it gives within LOOKUP_MS */
#define LOOKUP_MS 10000
static int lookup(const char *text, int *first, KpNetAddr *a, char *error, size_t size)
{
	KpNetLookup *l = kp_net_lookup_start(text);
	uint32_t until = kp_net_sock_now() + LOOKUP_MS;
	int r;
	*first = -2;
	if (!l) return -2;
	r = *first = kp_net_lookup_poll(l, a, error, size);
	while (r < 0 && (int32_t)(until - kp_net_sock_now()) > 0) r = kp_net_lookup_poll(l, a, error, size);
	kp_net_lookup_free(l);
	return r;
}

/* A host's address looked up off the caller's thread (kp_net_lookup_*): a dotted address and a text that is no address
 * answered at once, a name by the resolver's thread (none on the web); lookups given up while they resolve free
 * themselves (ASan's leak check sees one that does not). */
static void host_address_lookups(KpT *T)
{
	KpNetLookup *gone[4];
	KpNetAddr a;
	char error[128];
	int first, r;
	size_t k;
	error[0] = 0;
	r = lookup("127.0.0.1:7000", &first, &a, error, sizeof error);
	kp_t_ok(T, first == 1 && r == 1 && kp_net_addr_ip(&a) == KP_NET_LOOPBACK_IP && kp_net_addr_port(&a) == 7000,
		"a dotted address at once: %d %s", first, error);
	r = lookup("no address!", &first, &a, error, sizeof error);
	kp_t_ok(T, first == 0 && r == 0 && strstr(error, "is not an address") != NULL, "no address at once: %s", error);
	r = lookup("localhost", &first, &a, error, sizeof error);
#ifdef __EMSCRIPTEN__
	kp_t_ok(T, first == 0 && r == 0 && strstr(error, "no resolver") != NULL, "no name on the web: %s", error);
#else
	kp_t_ok(T, r == 1 && (kp_net_addr_ip(&a) >> 24) == 127 && kp_net_addr_port(&a) == KP_NET_LIST_PORT,
		"localhost by the resolver's thread: %d %s", r, error); /* the hosts file's: no network needed */
#endif
	for (k = 0; k < sizeof gone / sizeof gone[0]; k++) {
		gone[k] = kp_net_lookup_start(k % 2 ? "localhost:6200" : "localhost");
		kp_t_ok(T, gone[k] != NULL, "lookup %u started", (unsigned)k);
	}
	for (k = 0; k < sizeof gone / sizeof gone[0]; k++) kp_net_lookup_free(gone[k]);
	kp_net_lookup_free(NULL);
	r = lookup("localhost", &first, &a, error, sizeof error); /* time for the threads given up to end */
	kp_t_ok(T, r >= 0, "a lookup after those given up: %d", r);
}

/* The port of the game the provider lists for an advertisement from `from` (0: none listed). */
static uint16_t listed_port(KpSnpUdp *p, uint32_t ip, uint16_t port, const uint8_t *ad, size_t ad_size)
{
	KpNetAddr from = kp_net_addr(ip, port);
	const KpSnpGame *g;
	uint16_t at = 0;
	kp_snp_udp_input(p, KP_NET_SOCKET_LIST, &from, ad, ad_size);
	for (g = kp_snp_udp_lock_games(p); g; g = g->next)
		if (kp_net_addr_ip(&g->addr) == ip) at = kp_net_addr_port(&g->addr);
	kp_snp_udp_unlock_games(p);
	return at;
}

/* Koprulu's host by its address (kp_snp_udp_direct): a game-list request to it at once and with each request after the
 * broadcast; its game at its port + 1, every other advertiser's at port 6112 as the original lists them. */
static void host_by_address_ports(KpT *T)
{
	Alone a;
	KpSnpUdp sender, p;
	KpNetIo io;
	KpNetAddr host = kp_net_addr(IP(5), 7000), bad;
	uint8_t ad[KP_SNP_LIST_DATAGRAM];
	size_t ad_size;
	int sends;
	memset(&a, 0, sizeof a);
	a.now = START;
	io.send = alone_send;
	io.now = alone_now;
	io.ud = &a;
	kp_snp_udp_init(&sender, &io, PROGRAM, VERSION, 8);
	kp_t_ok(T, kp_snp_udp_advertise(&sender, "Far", "", KP_SNET_MODE_ADVERTISED, NULL, 0) && a.last_size > 0,
		"an advertisement");
	memcpy(ad, a.last, a.last_size);
	ad_size = a.last_size;
	kp_snp_udp_init(&p, &io, PROGRAM, VERSION, 8);
	bad = kp_net_addr(IP(5), 0);
	kp_t_ok(T, !kp_snp_udp_direct(&p, &bad), "port 0 taken");
	bad = kp_net_addr(IP(5), 0xffff);
	kp_t_ok(T, !kp_snp_udp_direct(&p, &bad), "port 65535 taken (its game would be at port 0)");
	sends = a.sends;
	kp_t_ok(T, kp_snp_udp_direct(&p, &host), "the host's address refused");
	kp_t_ok(T, a.sends == sends + 1 && a.last_socket == KP_NET_SOCKET_LIST && kp_net_addr_eq(&a.last_to, &host) &&
			a.last_size == KP_SNP_HEADER && kp_net_get16(a.last + 4) == KP_SNP_REQUEST && kp_net_sum(a.last, a.last_size) == 0,
		"the request at once: %d sends, socket %d, %lu bytes", a.sends - sends, a.last_socket, (unsigned long)a.last_size);
	/* the next request goes to the broadcast and then to the host */
	a.now += KP_SNP_REQUEST_GAP + 1;
	sends = a.sends;
	kp_snp_udp_unlock_games(&p);
	kp_t_ok(T, a.sends == sends + 2 && kp_net_addr_eq(&a.last_to, &host), "%d requests after the gap", a.sends - sends);
	kp_t_ok(T, listed_port(&p, IP(5), 7000, ad, ad_size) == 7001, "the host's game at its port + 1");
	kp_t_ok(T, listed_port(&p, IP(6), 7000, ad, ad_size) == KP_NET_GAME_PORT, "another advertiser's at 6112");
	host = kp_net_addr(IP(7), KP_NET_LIST_PORT);
	kp_t_ok(T, kp_snp_udp_direct(&p, &host), "the host on port 6111");
	kp_t_ok(T, listed_port(&p, IP(7), KP_NET_LIST_PORT, ad, ad_size) == KP_NET_GAME_PORT, "6111's game at 6112");
	kp_t_ok(T, kp_snp_udp_direct(&p, NULL), "no host");
	sends = a.sends;
	a.now += KP_SNP_REQUEST_GAP + 1;
	kp_snp_udp_unlock_games(&p);
	kp_t_ok(T, a.sends == sends + 1 && kp_net_addr_ip(&a.last_to) == KP_NET_BROADCAST, "the broadcast alone");
	kp_t_ok(T, listed_port(&p, IP(8), 7000, ad, ad_size) == KP_NET_GAME_PORT, "no host: port 6112");
	kp_snp_udp_free(&p);
	kp_snp_udp_free(&sender);
}

/* A network without broadcasts (the internet): the joiners list the host's game only by its address, join it and play
 * in lockstep, the joiners' Storms talking to each other directly. */
static void host_by_address_without_broadcasts(KpT *T)
{
	World *w = world_lan(T, 21, 3, 0);
	KpNetAddr at = kp_net_addr(IP(0), KP_NET_LIST_PORT);
	int k;
	host(w, 0, "Koprulu", "", 3);
	advance(w, now_of(w) + 1000);
	kp_t_ok(T, listing(w, 1).count == 0, "listed without broadcasts");
	kp_t_ok(T, listing(w, 0).count == 1, "the host's own broadcast comes back to it");
	for (k = 1; k < 3; k++) kp_t_ok(T, kp_snp_udp_direct(&w->seat[k].n.snp, &at), "seat %d's host address", k);
	advance(w, now_of(w) + 100);
	for (k = 1; k < 3; k++) kp_t_ok(T, listing(w, k).count == 1, "seat %d lists the host's game", k);
	join(w, 1, "Koprulu");
	join(w, 2, "Koprulu");
	for (k = 0; k < 3; k++) start_playing(&w->seat[k]);
	play(w, 2000);
	for (k = 0; k < 3; k++) kp_t_ok(T, w->seat[k].received > 50, "seat %d received %u turns", k, w->seat[k].received);
	for (k = 1; k < 3; k++)
		kp_t_ok(T, w->seat[k].by_turn[40] == w->seat[0].by_turn[40], "seat %d's turns differ from the host's", k);
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
	kp_net_lan_link(w->n.lan, 2, 6, 100);
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
	st = kp_net_lan_stats(w->n.lan);
	kp_t_ok(T, st.lost > 50, "only %ld datagrams lost", st.lost);
	*turns = w->seat[0].by_turn[least - 1];
	return w->n.trace;
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
	kp_net_lan_link(w->n.lan, 2, 8, 150);
	host(w, 0, "Koprulu", "", 3);
	start_playing(&w->seat[0]);
	join(w, 1, "Koprulu");
	start_playing(&w->seat[1]);
	join(w, 2, "Koprulu");
	start_playing(&w->seat[2]);
	for (k = 0; k < 20; k++) {
		snprintf(text, sizeof text, "m%d", k);
		kp_t_ok(T, kp_snet_send_message(w->seat[0].n.net, k % 2 ? -1 : -2, text, (uint32_t)strlen(text) + 1),
			"send message");
	}
	snprintf(text, sizeof text, "direct");
	kp_t_ok(T, kp_snet_send_message(w->seat[1].n.net, 2, text, (uint32_t)strlen(text) + 1), "send to 2");
	kp_t_ok(T, !kp_snet_send_message(w->seat[1].n.net, 5, text, 1) && kp_snet_error(w->seat[1].n.net) ==
		KP_SNET_ERROR_BAD_PLAYER, "send to an unknown player");
	advance(w, now_of(w) + 5000);
	for (k = 0; k < 20; k++) {
		kp_t_ok(T, kp_snet_receive_message(w->seat[1].n.net, &from, &data, &size), "seat 1's message %d", k);
		snprintf(text, sizeof text, "m%d", k);
		kp_t_ok(T, from == 0 && strcmp((const char *)data, text) == 0, "seat 1's message %d: \"%s\" from %d", k,
			(const char *)data, from);
	}
	kp_t_ok(T, !kp_snet_receive_message(w->seat[1].n.net, &from, &data, &size) &&
		kp_snet_error(w->seat[1].n.net) == KP_SNET_ERROR_NO_DATA, "seat 1 has more messages");
	/* the loopback ones: odd messages went to the host too */
	for (k = 1; k < 20; k += 2) {
		kp_t_ok(T, kp_snet_receive_message(w->seat[0].n.net, &from, &data, &size), "the host's message %d", k);
		snprintf(text, sizeof text, "m%d", k);
		kp_t_ok(T, from == 0 && strcmp((const char *)data, text) == 0, "the host's message: \"%s\"", (const char *)data);
	}
	/* seat 2: the host's twenty and seat 1's one, each sender's in order */
	{
		int host_n = 0, direct = 0;
		while (kp_snet_receive_message(w->seat[2].n.net, &from, &data, &size)) {
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
	kp_net_lan_link(w->n.lan, 2, 10, 50);
	host(w, 0, "Koprulu", "", 4);
	for (k = 1; k < 4; k++) join(w, k, "Koprulu");
	for (k = 0; k < 4; k++) start_playing(&w->seat[k]);
	play(w, 1500);
	w->seat[2].playing = 0;
	kp_t_ok(T, kp_snet_leave_game(w->seat[2].n.net, REASON_LEAVE), "leave");
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
	kp_t_ok(T, kp_snet_leave_game(w->seat[0].n.net, REASON_LEAVE), "leave");
	play(w, 2000);
	kp_t_ok(T, kp_snet_is_host(w->seat[1].n.net, 1) == 1 && kp_snet_is_host(w->seat[2].n.net, 1) == 1 &&
			kp_snet_is_host(w->seat[2].n.net, 2) == 0,
		"host flags after the host left: %d %d %d", kp_snet_is_host(w->seat[1].n.net, 1),
		kp_snet_is_host(w->seat[2].n.net, 1), kp_snet_is_host(w->seat[2].n.net, 2));
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
		kp_net_lan_cut(v->n.lan, 0, 1);
		before = now_of(v);
		v->n.in_join = 1;
		ok = kp_snet_join_game(v->seat[1].n.net, 0, "Open", "", "Seat", "", &v->seat[1].id);
		v->n.in_join = -1;
		error = kp_snet_error(v->seat[1].n.net);
		kp_t_ok(T, !ok && error == KP_SNET_ERROR_UNREACHABLE, "an unreachable host: error %#x", error);
		kp_t_ok(T, now_of(v) - before >= 5000 && now_of(v) - before < 5100, "the join waited %u ms", now_of(v) - before);
		kp_t_ok(T, kp_snet_local_id(v->seat[1].n.net) == KP_SNET_NO_ID, "the failed joiner has an id");
	}
	/* a closed game refuses */
	{
		World *v = world_new(T, 9, 2);
		host(v, 0, "Closed", "", 4);
		kp_t_ok(T, kp_snet_set_game_mode(v->seat[0].n.net, KP_SNET_MODE_CLOSED | KP_SNET_MODE_ADVERTISED, 0), "close");
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
	kp_net_lan_cut(w->n.lan, w->seat[2].n.host, 1);
	w->seat[2].playing = 0;
	play(w, 2000);
	/* the others wait: seat 2 is present, without its turn, not heard for a second */
	kp_t_ok(T, !kp_snet_receive_turns(w->seat[0].n.net, 0, SEATS, data, sizes, status), "the host has its turn");
	kp_t_ok(T, (status[2] & (KP_SNET_PRESENT | KP_SNET_HAS_TURN | KP_SNET_RESPONDING)) == KP_SNET_PRESENT,
		"seat 2's status %#x", status[2]);
	kp_t_ok(T, (status[1] & KP_SNET_RESPONDING) && (status[0] & KP_SNET_RESPONDING), "the others' status");
	ok = kp_snet_drop_player(w->seat[0].n.net, 2, REASON_DROP);
	kp_t_ok(T, ok, "drop: error %#x", kp_snet_error(w->seat[0].n.net));
	kp_t_ok(T, !kp_snet_drop_player(w->seat[0].n.net, 0, REASON_DROP) &&
		kp_snet_error(w->seat[0].n.net) == KP_SNET_ERROR_PARAMETER, "the local seat dropped");
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
	sends.host = w->seat[0].n.host;
	sends.cls = 1;
	kp_net_lan_tap(w->n.lan, record_sends, &sends);
	kp_net_lan_cut(w->n.lan, w->seat[1].n.host, 1);
	kp_t_ok(T, kp_snet_send_message(w->seat[0].n.net, 1, text, sizeof text), "send");
	network(w, now_of(w) + 2000);
	kp_t_ok(T, sends.count == 2, "%d sends of the message in 2 s without wake-ups", sends.count);
	sends.count = 0;
	for (k = 0; k < 2000; k++) {
		w->seat[0].n.signalled = 1;
		network(w, now_of(w) + 1);
	}
	kp_t_ok(T, sends.count >= 6, "%d sends of the message in 2 s", sends.count);
	for (k = 1; k < sends.count; k++)
		kp_t_ok(T, sends.at[k] - sends.at[k - 1] == interval, "resend %d after %u ms, not %u", k,
			sends.at[k] - sends.at[k - 1], interval);
	kp_net_lan_cut(w->n.lan, w->seat[1].n.host, 0);
	advance(w, now_of(w) + 1000);
	{
		int from;
		const uint8_t *data;
		uint32_t size;
		kp_t_ok(T, kp_snet_receive_message(w->seat[1].n.net, &from, &data, &size) && from == 0 && size == sizeof text,
			"the message arrived after the cable came back");
	}
	/* acknowledged: no more resends */
	sends.count = 0;
	advance(w, now_of(w) + 2000);
	kp_t_ok(T, sends.count == 0, "%d resends after the ack", sends.count);
	kp_net_lan_tap(w->n.lan, kp_net_world_tap, &w->n);
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
		kp_net_world_deliver(&w->seat[0].n, KP_NET_SOCKET_GAME, &from, d, n);
		if (k % 50 == 0) play(w, STEP);
	}
	play(w, 2000);
	kp_t_ok(T, w->seat[0].received > 100 && w->seat[1].received > 100, "turns received: %u and %u",
		w->seat[0].received, w->seat[1].received);
	kp_t_ok(T, kp_snet_remote_count(w->seat[0].n.net) <= KP_SNET_RECORDS, "%d records", kp_snet_remote_count(w->seat[0].n.net));
}

/* A datagram of Storm's from a host in no game (its socket B at `ip`) to seat `to`, delivered at once. */
static void forge(World *w, int to, uint32_t ip, unsigned cls, unsigned cmd, unsigned seq, unsigned sender,
	unsigned flags, const uint8_t *payload, uint32_t size)
{
	uint8_t d[KP_SNP_DATAGRAM];
	KpNetAddr from = kp_net_addr(ip, KP_NET_GAME_PORT);
	kp_net_put16(d, 0);
	kp_net_put16(d + 2, KP_SNET_HEADER + size);
	kp_net_put16(d + 4, seq);
	kp_net_put16(d + 6, 0);
	d[8] = (uint8_t)cls;
	d[9] = (uint8_t)cmd;
	d[10] = (uint8_t)sender;
	d[11] = (uint8_t)flags;
	if (size) memcpy(d + KP_SNET_HEADER, payload, size);
	kp_net_checksum(d, KP_SNET_HEADER + size);
	kp_net_world_deliver(&w->seat[to].n, KP_NET_SOCKET_GAME, &from, d, KP_SNET_HEADER + size);
}

#define STRANGER(k) (0x0a000100u + (uint32_t)(k)) /* 10.0.1.k: hosts in no game */

/* Hosts in no game cannot steer a running game (kp_snet.h's departures, the network review of 2026-11-21): a game info,
 * a player info, a drop and a new host from their addresses are ignored, a relayed turn "of the host" is not the host's,
 * and their floods of class 3 and of turns far ahead leave the seats' own turns room. Storm takes all of them. */
static void strangers_cannot_steer_a_game(KpT *T)
{
	World *w = world_new(T, 22, 2);
	uint8_t p[0x40], t[TURN_SIZE] = {0, 0x05, 0, 0, 0, 0, 0xee, 0xee};
	uint32_t before[2], k;
	host(w, 0, "Koprulu", "", 2);
	join(w, 1, "Koprulu");
	start_playing(&w->seat[0]);
	start_playing(&w->seat[1]);
	play(w, 1000);
	memset(p, 0, sizeof p);
	kp_net_put32(p, 1);          /* game info: the joiner's own id, its turns reset */
	kp_net_put32(p + 4, 8);
	kp_net_put32(p + 8, 0x4000);
	forge(w, 1, STRANGER(1), 0, 8, 0, KP_SNET_NO_ID, 0, p, 23);
	memset(p, 0, sizeof p);      /* player info: a player 5 at 10.0.0.77 */
	kp_net_put32(p, 0x26);
	kp_net_put32(p + 4, 5);
	kp_net_put32(p + 16, kp_snet_local_turn(w->seat[1].n.net));
	memcpy(p + 20, kp_net_addr(0x0a00004du, KP_NET_GAME_PORT).b, KP_NET_ADDR);
	forge(w, 1, STRANGER(2), 0, 6, 0, KP_SNET_NO_ID, 0, p, 0x26);
	memset(p, 0, sizeof p);      /* the host dropped at the joiner's turn */
	kp_net_put32(p + 4, kp_snet_local_turn(w->seat[1].n.net));
	kp_net_put32(p + 8, REASON_DROP);
	forge(w, 1, STRANGER(3), 0, 12, 0, KP_SNET_NO_ID, 0, p, 12);
	kp_net_put32(p, 1);          /* the joiner made host */
	forge(w, 1, STRANGER(4), 0, 13, 0, KP_SNET_NO_ID, 0, p, 4);
	forge(w, 1, STRANGER(5), 2, 0, kp_snet_local_turn(w->seat[1].n.net) + 40, 0, 4, t, sizeof t); /* "relayed" */
	before[0] = w->seat[0].received;
	before[1] = w->seat[1].received;
	for (k = 0; k < KP_SNET_NODES + 4096; k++) {
		if (k % 2) forge(w, 0, STRANGER(6), 3, 0, k, KP_SNET_NO_ID, 0, NULL, 0);
		else forge(w, 0, STRANGER(7 + k % 64), 2, 0, 100 + k, KP_SNET_NO_ID, 0, t, sizeof t);
		if (k % 256 == 255) play(w, 1); /* within the provider's queue */
	}
	play(w, 2000);
	for (k = 0; k < 2; k++)
		kp_t_ok(T, w->seat[k].received - before[k] > 100, "seat %u received %u turns after the strangers", k,
			w->seat[k].received - before[k]);
	kp_t_ok(T, kp_snet_local_id(w->seat[1].n.net) == 1 && w->seat[1].removed[0] == 0 && w->seat[0].removed[1] == 0,
		"the joiner's id %d, removals %d and %d", kp_snet_local_id(w->seat[1].n.net), w->seat[1].removed[0],
		w->seat[0].removed[1]);
	kp_t_ok(T, w->seat[1].players == 0 && kp_snet_is_host(w->seat[1].n.net, 0) == 1 &&
			kp_snet_is_host(w->seat[1].n.net, 1) == 0 && kp_snet_is_host(w->seat[1].n.net, 5) == -1,
		"the joiner's player events %d, hosts %d %d %d", w->seat[1].players, kp_snet_is_host(w->seat[1].n.net, 0),
		kp_snet_is_host(w->seat[1].n.net, 1), kp_snet_is_host(w->seat[1].n.net, 5));
}

/* While seat 1 joins, a host in no game sends it a reject each frame: Storm's join would end refused at the first. */
static void frame_forging_rejects(KpNetSeat *s)
{
	World *w = (World *)s->w;
	frame((Seat *)s);
	if (s->index == 0 && w->n.in_join == 1) {
		forge(w, 1, STRANGER(1), 0, 10, w->forged, KP_SNET_NO_ID, 0, NULL, 0);
		forge(w, 1, STRANGER(2), 0, 9, w->forged, KP_SNET_NO_ID, 0, NULL, 0);
		w->forged++;
	}
}

static void strangers_cannot_steer_a_join(KpT *T)
{
	World *w = world_new(T, 23, 2);
	kp_net_lan_link(w->n.lan, 50, 0, 0); /* the host's answers take several of the join's passes */
	w->n.frame = frame_forging_rejects;
	host(w, 0, "Koprulu", "", 2);
	join(w, 1, "Koprulu");
	kp_t_ok(T, w->forged > 0, "nothing forged during the join");
	kp_t_ok(T, w->seat[1].id == 1 && w->seat[1].templates == 1, "the joiner's id %d, template events %d",
		w->seat[1].id, w->seat[1].templates);
}

#define FORGED_ADDRESSES (KP_SNET_RECORDS + 1000) /* past the records' bound */
#define FORGED(k) (0x0b000000u + (uint32_t)(k))  /* 11.0.k.k: addresses no seat has */

/* Forged addresses cannot keep a new player out (kp_snet.h's departures): datagrams that fail the header's checks make
 * no record, and once valid ones from more addresses than KP_SNET_RECORDS fill the records, a joiner's address takes a
 * stranger's place. With the bound alone the third seat's join found no room on the host. */
static void forged_addresses_cannot_keep_a_player_out(KpT *T)
{
	World *w = world_new(T, 24, 3);
	uint8_t bad[KP_SNET_HEADER];
	int before, k;
	host(w, 0, "Koprulu", "", 3);
	join(w, 1, "Koprulu");
	before = kp_snet_remote_count(w->seat[0].n.net);
	memset(bad, 0, sizeof bad);
	kp_net_put16(bad + 2, KP_SNET_HEADER);
	bad[8] = 3;
	kp_net_checksum(bad, sizeof bad);
	bad[0] ^= 0x5a; /* a checksum Storm refuses */
	for (k = 0; k < 500; k++) {
		KpNetAddr from = kp_net_addr(FORGED(k), KP_NET_GAME_PORT);
		kp_net_world_deliver(&w->seat[0].n, KP_NET_SOCKET_GAME, &from, bad, sizeof bad);
		if (k % 256 == 255) play(w, 1); /* within the provider's queue */
	}
	play(w, 10);
	kp_t_eq(T, kp_snet_remote_count(w->seat[0].n.net), before, "a refused datagram makes no record");
	for (k = 0; k < FORGED_ADDRESSES; k++) {
		forge(w, 0, FORGED(k), 3, 0, 0, KP_SNET_NO_ID, 0, NULL, 0);
		if (k % 256 == 255) play(w, 1);
	}
	play(w, 10);
	kp_t_eq(T, kp_snet_remote_count(w->seat[0].n.net), KP_SNET_RECORDS, "the forged addresses fill the records");
	join(w, 2, "Koprulu");
	kp_t_ok(T, w->seat[2].id == 2, "the third seat's id %d", w->seat[2].id);
	kp_t_ok(T, kp_snet_remote_count(w->seat[0].n.net) <= KP_SNET_RECORDS, "%d records",
		kp_snet_remote_count(w->seat[0].n.net));
	kp_t_ok(T, kp_snet_is_host(w->seat[2].n.net, 0) == 1 && kp_snet_is_host(w->seat[2].n.net, 1) == 0,
		"the joiner knows the host and the first joiner: %d %d", kp_snet_is_host(w->seat[2].n.net, 0),
		kp_snet_is_host(w->seat[2].n.net, 1));
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
	kp_t_skip(T, "no UDP sockets on the web (kp_netplay.h)");
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
			if (!(w->seat[k].n.sock = kp_net_sock_open(&c, error, sizeof error))) break;
			kp_t_ok(T, kp_net_sock_port(w->seat[k].n.sock, KP_NET_SOCKET_GAME) == base + 2u * (uint32_t)k + 1u,
				"seat %d's socket B at %u", k, kp_net_sock_port(w->seat[k].n.sock, KP_NET_SOCKET_GAME));
		}
		if (k == 3) break;
		while (k--) {
			kp_net_sock_close(w->seat[k].n.sock);
			w->seat[k].n.sock = NULL;
		}
	}
	kp_t_ok(T, attempt < 32, "no six free ports: %s", error);
	for (k = 0; k < 3; k++) {
		KpNetIo io = kp_net_sock_io(w->seat[k].n.sock);
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
	kp_t_ok(T, kp_snet_leave_game(w->seat[2].n.net, REASON_LEAVE), "leave");
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
		kp_t_ok(T, kp_net_sock_send_errors(w->seat[k].n.sock) == 0, "seat %d: %ld datagrams refused", k,
			kp_net_sock_send_errors(w->seat[k].n.sock));
}

/* Two programs on the system's sockets, each the one host of its own map: no broadcast crosses, so the joiner lists
 * the host's game only by the host's address, a port that is not 6111 (its game at the next port), joins it and plays
 * in lockstep (Koprulu's host by its address, stage N5). */
static void sockets_reach_a_host_by_address(KpT *T)
{
	World *w;
	char error[128] = "";
	int attempt, k = 0;
	uint32_t base = 0, until, e = 0;
	KpNetAddr at;
#ifdef __EMSCRIPTEN__
	kp_t_skip(T, "no UDP sockets on the web (kp_netplay.h)");
#endif
	w = world_alloc(T);
	for (attempt = 0; attempt < 32; attempt++) {
		KpNetSockConfig c;
		base = 20000u + (kp_net_sock_now() / 2u + (uint32_t)attempt * 7919u) % 20000u * 2u;
		memset(&c, 0, sizeof c);
		c.map_hosts = 1;
		for (k = 0; k < 2; k++) {
			c.map_base = (uint16_t)(base + 2u * (uint32_t)k);
			if (!(w->seat[k].n.sock = kp_net_sock_open(&c, error, sizeof error))) break;
		}
		if (k == 2) break;
		while (k--) {
			kp_net_sock_close(w->seat[k].n.sock);
			w->seat[k].n.sock = NULL;
		}
	}
	kp_t_ok(T, attempt < 32, "no four free ports: %s", error);
	for (k = 0; k < 2; k++) {
		KpNetIo io = kp_net_sock_io(w->seat[k].n.sock);
		seat_add(w, &io);
	}
	host(w, 0, "Koprulu", "", 2);
	advance(w, now_of(w) + 600);
	kp_t_ok(T, listing(w, 1).count == 0, "the game listed across the maps by a broadcast");
	at = kp_net_addr(KP_NET_LOOPBACK_IP, (uint16_t)base);
	kp_t_ok(T, kp_snp_udp_direct(&w->seat[1].n.snp, &at), "the host's address");
	until = now_of(w) + 3000;
	while (listing(w, 1).count == 0 && (int32_t)(now_of(w) - until) < 0) advance(w, now_of(w) + 20);
	kp_t_ok(T, enter(w, 1, "Koprulu", "", &e), "the join: error %#x", e);
	for (k = 0; k < 2; k++) start_playing(&w->seat[k]);
	play(w, 1000);
	for (k = 0; k < 2; k++) kp_t_ok(T, w->seat[k].received > 20, "seat %d received %u turns", k, w->seat[k].received);
	kp_t_ok(T, w->seat[0].by_turn[15] == w->seat[1].by_turn[15], "the seats' turns differ");
	for (k = 0; k < 2; k++)
		kp_t_ok(T, kp_net_sock_send_errors(w->seat[k].n.sock) == 0, "seat %d: %ld datagrams refused", k,
			kp_net_sock_send_errors(w->seat[k].n.sock));
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
	int ok = kp_snet_send_turn(st->n.net, &turn, 1); /* before the message reads the error */
	kp_t_ok(T, ok, "a turn: error %#x", kp_snet_error(st->n.net));
}

static void live_bridge_join(KpT *T)
{
	const char *env = getenv(LIVE_ENV);
	World *w;
	Seat *st;
	KpNetIo io;
	Listing l;
	char error[128] = "";
	uint32_t until, e = 0, host_turns = 0, sets = 0;
	uint8_t first[64];
	size_t first_size = 0;
	int k, ok;
	if (!env || !*env) kp_t_skip(T, "no " LIVE_ENV " (the emulator's --bridge base port)");
	w = world_alloc(T);
	w->seat[0].n.sock = kp_net_world_bridge_open(env, 0, error, sizeof error);
	kp_t_ok(T, w->seat[0].n.sock != NULL, "the seat's sockets: %s", error);
	io = kp_net_sock_io(w->seat[0].n.sock);
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
		if (!kp_snet_receive_turns(st->n.net, 0, SEATS, data, sizes, status)) {
			kp_t_ok(T, kp_snet_error(st->n.net) == KP_SNET_ERROR_NO_DATA, "receive: error %#x", kp_snet_error(st->n.net));
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
	ok = kp_snet_leave_game(st->n.net, REASON_LEAVE);
	kp_t_ok(T, ok, "leave: error %#x", kp_snet_error(st->n.net));
	network(w, now_of(w) + 500);
	kp_t_ok(T, kp_net_sock_send_errors(w->seat[0].n.sock) == 0, "%ld datagrams refused",
		kp_net_sock_send_errors(w->seat[0].n.sock));
}

static void api_errors(KpT *T)
{
	World *w = world_new(T, 13, 1);
	KpSnet *s = w->seat[0].n.net;
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
	{ "host_address_parsing", host_address_parsing, 0 },
	{ "host_address_lookups", host_address_lookups, 0 },
	{ "host_by_address_ports", host_by_address_ports, 0 },
	{ "host_by_address_without_broadcasts", host_by_address_without_broadcasts, 0 },
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
	{ "strangers_cannot_steer_a_game", strangers_cannot_steer_a_game, 0 },
	{ "strangers_cannot_steer_a_join", strangers_cannot_steer_a_join, 0 },
	{ "forged_addresses_cannot_keep_a_player_out", forged_addresses_cannot_keep_a_player_out, 0 },
	{ "sockets_carry_a_game", sockets_carry_a_game, 0 },
	{ "sockets_reach_a_host_by_address", sockets_reach_a_host_by_address, 0 },
	{ "live_bridge_join", live_bridge_join, 0 },
	{ "api_errors", api_errors, 0 },
};

KP_SUITE(kp_suite_snet, "test_snet", TESTS);
