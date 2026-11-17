/* The network fuzz targets (kp_fuzz_net.h; kp_fuzz.h tells their inputs). One stack is fuzzed, at 10.0.0.1: the UDP
 * provider alone (udp) or Storm's SNet over it (snet). Its peers are 10.0.1.1 to 10.0.1.15, and what they send is the
 * input's; its own sends are checked (well-formed, no larger than their socket takes) and dropped. The seeds are
 * written by the same interpreter while the stack plays with a whole peer on the in-process LAN: each op is recorded
 * as it is run, so the seed replays the session exactly. */
#include "kp_fuzz_net.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kp_net.h"
#include "kp_net_lan.h"
#include "kp_snet.h"
#include "kp_snp_udp.h"

#define PROGRAM 0x53544152u /* 'STAR' */
#define VERSION 0xd5u
#define START 1000000u      /* the stack's clock at an input's start */
#define SELF_IP 0x0a000001u /* the stack's address */
#define PEER_IP 0x0a000101u /* peer k's: 10.0.1.(k + 1) */
#define SELF_PEER 15u       /* the peer bits naming the stack itself (its broadcasts come back to it) */
#define MAX_PLAYERS 8       /* the provider's, and the turn slots received */
#define GAME_NAME "Koprulu"
#define MESSAGES 64         /* the messages one receive op takes, at most */
#define REASON_LEAVE 3u
#define REASON_DROP 0x40000006u
#define LAN_SEED 0x4b504e46u
#define SEED_MAX 32768      /* a seed's bytes, at most */
#define SEED_STEP 50        /* ms between a seed's frames */
#define SEED_TURNS 40       /* the frames a seed's game plays */
#define TURN_BYTES 8

/* An input: a first byte (snet: bits 0-3 the players of a game the stack creates at once, 0 none), then ops, op & 7
 * their kind and the rest their bits:
 *   OP_GAME, OP_LIST  a datagram on port 6112 or 6111 from peer bits 3-6 (15: the stack itself), {u16 n, n bytes};
 *                     OP_FIX: its length field and checksum made right
 *   OP_CLOCK          the clock moved {u16 ms}; inside SNetJoinGame's wait, the datagram ops before it arrived
 *                     during the wait, and it ends the wait
 *   OP_PUMP           snet: the pump thread's pass (bit 3: signalled); udp: Storm takes the queued datagrams
 *   OP_TURN           snet: SNetSendTurn {u8 n, n bytes}; udp: an advertisement {u8 mode, u8 n, name, u8 n, stat
 *                     string, u8 n, extra bytes}
 *   OP_RECEIVE        snet: SNetReceiveTurns, then SNetReceiveMessage until none; udp: the advertisement stopped
 *   OP_MESSAGE        snet: SNetSendMessage to bits 3-6 minus 2 {u8 n, n bytes}; udp: a datagram sent to peer bits
 *                     3-6 {u8 n, n bytes}
 *   OP_CALL           snet: CALL_* of bits 3-6 (their arguments next); udp: the game list walked, a game's info by
 *                     {u8 id} and by the first one's name */
enum { OP_GAME, OP_LIST, OP_CLOCK, OP_PUMP, OP_TURN, OP_RECEIVE, OP_MESSAGE, OP_CALL };
#define OP_FIX 0x80u
enum { CALL_INFO, CALL_PLAYERS, CALL_NAME, CALL_CAPS, CALL_TRANSIT, CALL_MODE, CALL_DROP, CALL_LEAVE, CALL_DISCONNECT,
	CALL_ENUM, CALL_JOIN, CALL_CREATE };

typedef struct Gen Gen;

/* What a run did: the bytes it received summed (they are read: a stale pointer is ASan's), its sends hashed, the
 * remote turns it received. A seed replayed does what its session did. */
typedef struct {
	uint32_t received, sent, turns;
} Digest;

typedef struct {
	KpSnpUdp snp;
	KpSnet *net; /* NULL: the udp target */
	uint32_t now;
	uint32_t wake;  /* when the pump's timer runs out (KP_SNET_INFINITE: none) */
	int signalled;  /* a datagram queued or a send signalled since the last pass */
	const uint8_t *d;
	size_t size, at;
	int depth; /* event handlers inside each other */
	int failed;
	char *why;
	size_t why_size;
	Digest digest;
	Gen *gen;      /* writing a seed */
	uint8_t pkt[0x10000];
} Stack;

#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 2, 3)))
#endif
static void fail(Stack *st, const char *fmt, ...)
{
	va_list ap;
	if (st->failed++) return;
	va_start(ap, fmt);
	vsnprintf(st->why, st->why_size, fmt, ap);
	va_end(ap);
}

static int before(uint32_t a, uint32_t b) { return (int32_t)(a - b) < 0; }

/* FNV-1a's step over n bytes */
static uint32_t fnv(uint32_t h, const uint8_t *p, size_t n)
{
	size_t k;
	for (k = 0; k < n; k++) h = (h ^ p[k]) * 16777619u;
	return h;
}

static unsigned take8(Stack *st) { return st->at < st->size ? st->d[st->at++] : 0u; }

static unsigned take16(Stack *st)
{
	unsigned lo = take8(st);
	return lo | take8(st) << 8;
}

/* n bytes of the input (fewer at its end), NULL for none */
static const uint8_t *take(Stack *st, size_t *n)
{
	const uint8_t *p;
	if (*n > st->size - st->at) *n = st->size - st->at;
	if (!*n) return NULL;
	p = st->d + st->at;
	st->at += *n;
	return p;
}

/* {u8 n, n bytes} as a string (at most 255 characters, cut at a zero) */
static void take_string(Stack *st, char *out)
{
	size_t n = take8(st);
	const uint8_t *b = take(st, &n);
	if (n) memcpy(out, b, n);
	out[n] = 0;
}

static void sum(Stack *st, const uint8_t *p, uint32_t n)
{
	uint32_t k;
	for (k = 0; k < n; k++) st->digest.received += p[k];
}

static KpNetAddr peer_addr(unsigned peer, int socket)
{
	uint32_t ip = peer == SELF_PEER ? SELF_IP : PEER_IP + peer;
	return kp_net_addr(ip, (uint16_t)(socket == KP_NET_SOCKET_LIST ? KP_NET_LIST_PORT : KP_NET_GAME_PORT));
}

/* --- the seeds' world (below): the stack's sends reach the LAN, its ops are written --- */

struct Gen {
	KpNetLan *lan;
	int self, peer;  /* the LAN's hosts */
	KpNetIo self_io; /* the stack's sends into the LAN */
	KpSnpUdp snp;    /* the peer, a whole stack */
	KpSnet *net;
	int snp_ready;
	uint32_t wake;
	int signalled;
	Stack *st;
	uint8_t *buf; /* the seed */
	size_t size;
	int full;
	uint8_t op[3 + 0x10000];
};

static void gen_run(Gen *g, uint32_t to, int in_join);
static void gen_clock(Gen *g, uint32_t ms);

/* --- the stack --- */

static void run_op(Stack *st);

/* What the stack sends: a game-list datagram (and SNet's packets) with its length and checksum right, no larger than
 * their socket's buffer. The udp target's port-6112 datagrams are its input's. */
static void stack_send(void *ud, int socket, const KpNetAddr *to, const uint8_t *data, size_t size)
{
	Stack *st = (Stack *)ud;
	int list = socket == KP_NET_SOCKET_LIST;
	if (list || st->net) {
		size_t head = list ? KP_SNP_HEADER : KP_SNET_HEADER, most = list ? KP_SNP_LIST_DATAGRAM : 0xffff;
		if (size < head || size > most || kp_net_get16(data + 2) != size || kp_net_sum(data, size) != 0)
			fail(st, "a malformed datagram of %u bytes sent on socket %d", (unsigned)size, socket);
	}
	st->digest.sent = fnv(st->digest.sent ^ (uint32_t)socket, data, size);
	if (st->gen) st->gen->self_io.send(st->gen->self_io.ud, socket, to, data, size);
}

static uint32_t stack_now(void *ud) { return ((Stack *)ud)->now; }

/* SNetJoinGame's waits: the datagrams that arrive meanwhile are the next ops of the input, and a clock op ends the
 * wait (a seed's moves its clock by 0). */
static void stack_sleep(void *ud, uint32_t ms)
{
	Stack *st = (Stack *)ud;
	st->now += ms;
	if (st->gen) {
		gen_run(st->gen, st->now, 1);
		gen_clock(st->gen, 0);
		return;
	}
	while (!st->failed && st->at < st->size && (st->d[st->at] & 7) <= OP_LIST) run_op(st);
	if (!st->failed && st->at < st->size && (st->d[st->at] & 7) == OP_CLOCK) run_op(st);
}

/* The events, and what StarCraft's handlers call inside them: a joiner's name and a lobby message, the counts. */
static void stack_event(void *ud, const KpSnetEvent *e)
{
	Stack *st = (Stack *)ud;
	char name[KP_SNET_STRING];
	int first, last, active;
	if (e->id < KP_SNET_EVENT_TEMPLATE || e->id > KP_SNET_EVENT_TEXT || e->player < -1 || e->player > 255 ||
		(e->size && !e->data) || e->size > 0xffff)
		fail(st, "event %u of player %d, %u bytes", e->id, e->player, e->size);
	if (e->size && e->data) sum(st, e->data, e->size);
	if (st->depth++ == 0) {
		if (e->id == KP_SNET_EVENT_PLAYER) {
			(void)kp_snet_player_name(st->net, e->player, name, sizeof name);
			(void)kp_snet_send_message(st->net, -2, "joined", 6);
		}
		(void)kp_snet_num_players(st->net, &first, &last, &active);
	}
	st->depth--;
}

static int stack_init(Stack *st, int snet, const uint8_t *d, size_t size, char *why, size_t why_size, Gen *gen)
{
	KpNetIo io;
	KpSnetHooks h;
	memset(st, 0, sizeof *st);
	st->now = START;
	st->wake = KP_SNET_INFINITE;
	st->d = d;
	st->size = size;
	st->why = why;
	st->why_size = why_size;
	st->gen = gen;
	io.send = stack_send;
	io.now = stack_now;
	io.ud = st;
	kp_snp_udp_init(&st->snp, &io, PROGRAM, VERSION, MAX_PLAYERS);
	if (!snet) return 0;
	memset(&h, 0, sizeof h);
	h.now = stack_now;
	h.sleep = stack_sleep;
	h.event = stack_event;
	h.ud = st;
	if (!(st->net = kp_snet_new(&st->snp, &h, PROGRAM, VERSION))) {
		kp_snp_udp_free(&st->snp);
		return -1;
	}
	return 0;
}

static void stack_free(Stack *st)
{
	if (st->net) kp_snet_free(st->net);
	st->net = NULL;
	kp_snp_udp_free(&st->snp);
}

/* The bounds: the provider's queue and game list, SNet's records and its local id. */
static void check(Stack *st)
{
	const KpSnpDatagram *q;
	const KpSnpGame *g;
	int n = 0, id;
	for (q = st->snp.queue; q; q = q->next) n++;
	if (n != st->snp.queued || n > KP_SNP_QUEUE) fail(st, "%d datagrams queued, %d counted", n, st->snp.queued);
	for (n = 0, g = st->snp.games; g; g = g->next) n++;
	if (n > KP_SNP_GAMES) fail(st, "%d games listed", n);
	if (!st->net) return;
	id = kp_snet_local_id(st->net);
	if (kp_snet_remote_count(st->net) > KP_SNET_RECORDS) fail(st, "%d records", kp_snet_remote_count(st->net));
	if (id < 0 || id > (int)KP_SNET_NO_ID) fail(st, "the local id %d", id);
}

static void op_datagram(Stack *st, unsigned op)
{
	int socket = (op & 7) == OP_LIST ? KP_NET_SOCKET_LIST : KP_NET_SOCKET_GAME;
	size_t n = take16(st);
	const uint8_t *b = take(st, &n);
	KpNetAddr from = peer_addr(op >> 3 & 15, socket);
	if (n) memcpy(st->pkt, b, n);
	if ((op & OP_FIX) && n >= 4) {
		kp_net_put16(st->pkt + 2, (uint32_t)n);
		kp_net_checksum(st->pkt, n);
	}
	if (kp_snp_udp_input(&st->snp, socket, &from, st->pkt, n)) st->signalled = 1;
}

static void op_pump(Stack *st, unsigned op)
{
	KpNetAddr from;
	uint32_t size, wait;
	if (st->net) {
		wait = kp_snet_pump(st->net, (int)(op >> 3 & 1));
		st->wake = wait == KP_SNET_INFINITE ? KP_SNET_INFINITE : st->now + wait;
		st->signalled = kp_snet_take_signal(st->net);
		return;
	}
	while (kp_snp_udp_receive(&st->snp, &from, st->pkt, &size)) {
		if (size > KP_SNP_DATAGRAM) fail(st, "a queued datagram of %u bytes", size);
		else sum(st, st->pkt, size);
	}
	st->signalled = 0;
}

static void op_receive(Stack *st)
{
	const uint8_t *data[MAX_PLAYERS], *m;
	uint32_t sizes[MAX_PLAYERS], status[MAX_PLAYERS], size;
	int k, sender;
	if (kp_snet_receive_turns(st->net, 0, MAX_PLAYERS, data, sizes, status)) {
		for (k = 0; k < MAX_PLAYERS; k++) {
			if (!(status[k] & KP_SNET_HAS_TURN)) continue;
			if (sizes[k] > 0xffff || (sizes[k] && !data[k])) fail(st, "slot %d's turn of %u bytes", k, sizes[k]);
			else if (sizes[k]) {
				sum(st, data[k], sizes[k]);
				if (k != kp_snet_local_id(st->net)) st->digest.turns++;
			}
		}
	}
	for (k = 0; k < MESSAGES && kp_snet_receive_message(st->net, &sender, &m, &size); k++) {
		if (sender < 0 || sender > 255 || size > 0xffff || (size && !m)) fail(st, "a message of %u bytes from %d", size, sender);
		else if (size) sum(st, m, size);
	}
	st->signalled |= kp_snet_take_signal(st->net);
}

static int first_game(void *ud, const KpSnetGame *g)
{
	uint32_t *id = (uint32_t *)ud;
	if (!*id) *id = g->id;
	return 1;
}

typedef struct {
	Stack *st;
	uint32_t id;
} Listed;

static int listed_game(void *ud, const KpSnetGame *g)
{
	Listed *l = (Listed *)ud;
	if (!g->name || !g->stats || strlen(g->name) >= KP_SNET_STRING || strlen(g->stats) >= KP_SNET_STRING)
		fail(l->st, "a listed game's strings");
	if (!l->id) l->id = g->id;
	return 1;
}

static void op_call(Stack *st, unsigned call)
{
	KpSnet *s = st->net;
	uint8_t buf[256];
	char name[KP_SNET_STRING + 1], password[256];
	uint32_t n = 0, hint = 0;
	int id = -1, first, last, active;
	switch (call) {
	case CALL_INFO: {
		int type = (int)(take8(st) & 7);
		uint32_t size = take8(st);
		if (kp_snet_game_info(s, type, size ? buf : NULL, size, &n) && size && n > size)
			fail(st, "game info %d: %u bytes written into %u", type, n, size);
		break;
	}
	case CALL_PLAYERS:
		if (kp_snet_num_players(s, &first, &last, &active) && (active < 0 || active > KP_SNET_RECORDS + 1))
			fail(st, "%d active players", active);
		break;
	case CALL_NAME: {
		int who = (int)(int8_t)take8(st);
		uint32_t size = take8(st) % (KP_SNET_STRING + 1);
		memset(name, 'x', sizeof name);
		if (kp_snet_player_name(s, who, name, size) && size && !memchr(name, 0, size))
			fail(st, "player %d's name not ended within %u bytes", who, size);
		break;
	}
	case CALL_CAPS: {
		KpSnetCaps caps;
		int who = (int)(int8_t)take8(st);
		memset(&caps, 0, sizeof caps);
		caps.size = take8(st) & 1 ? (uint32_t)sizeof caps : 0u;
		(void)kp_snet_player_caps(s, who, &caps);
		break;
	}
	case CALL_TRANSIT: (void)kp_snet_turns_in_transit(s, &n); break;
	case CALL_MODE: {
		uint32_t mode = take8(st);
		(void)kp_snet_set_game_mode(s, mode, (int)(take8(st) & 1));
		break;
	}
	case CALL_DROP: (void)kp_snet_drop_player(s, (int)(int8_t)take8(st), REASON_DROP); break;
	case CALL_LEAVE: (void)kp_snet_leave_game(s, REASON_LEAVE); break;
	case CALL_DISCONNECT: (void)kp_snet_disconnect_all(s, REASON_LEAVE); break;
	case CALL_ENUM: {
		Listed l;
		l.st = st;
		l.id = 0;
		(void)kp_snet_enum_games(s, listed_game, &l, &hint);
		break;
	}
	case CALL_JOIN: { /* the first listed game, as StarCraft joins what SNetEnumGamesEx listed */
		uint32_t game = 0;
		take_string(st, password);
		(void)kp_snet_enum_games(s, first_game, &game, &hint);
		if (kp_snet_join_game(s, game, game ? NULL : GAME_NAME, password, "Fuzz", "", &id) &&
			(id < 0 || id >= (int)KP_SNET_NO_ID))
			fail(st, "joined as player %d", id);
		break;
	}
	case CALL_CREATE: {
		uint32_t players = take8(st);
		size_t size = take8(st);
		const uint8_t *tmpl = take(st, &size);
		if (kp_snet_create_game(s, GAME_NAME, "", "", 0, 0, KP_SNET_MODE_ADVERTISED, tmpl, (uint32_t)size, players,
				"Host", "", &id) && id != 0)
			fail(st, "created as player %d", id);
		break;
	}
	default: break;
	}
	st->signalled |= kp_snet_take_signal(s);
}

/* The udp target's game list: walked (its strings ended, its extra bytes within), a game's info by id and by the first
 * one's name; the unlock may broadcast a request. */
static void op_list(Stack *st)
{
	const KpSnpGame *g;
	KpSnpGame info;
	char first[KP_SNP_STRING] = "";
	uint32_t id = take8(st);
	int count = 0;
	for (g = kp_snp_udp_lock_games(&st->snp); g; g = g->next) {
		if (!memchr(g->name, 0, sizeof g->name) || !memchr(g->stats, 0, sizeof g->stats) ||
			g->extra_size > sizeof g->extra)
			fail(st, "a listed game's fields");
		else if (!count) memcpy(first, g->name, sizeof first);
		count++;
	}
	kp_snp_udp_unlock_games(&st->snp);
	(void)kp_snp_udp_game_info(&st->snp, id, "", &info);
	if (count) (void)kp_snp_udp_game_info(&st->snp, 0, first, &info);
}

static void run_op(Stack *st)
{
	unsigned op = take8(st);
	char a[256], b[256];
	size_t n;
	const uint8_t *p;
	switch (op & 7) {
	case OP_GAME:
	case OP_LIST: op_datagram(st, op); break;
	case OP_CLOCK: st->now += take16(st); break;
	case OP_PUMP: op_pump(st, op); break;
	case OP_TURN:
		if (st->net) {
			n = take8(st);
			p = take(st, &n);
			(void)kp_snet_send_turn(st->net, p, (uint32_t)n);
			st->signalled |= kp_snet_take_signal(st->net);
		} else {
			uint32_t mode = take8(st);
			take_string(st, a);
			take_string(st, b);
			n = take8(st);
			p = take(st, &n);
			(void)kp_snp_udp_advertise(&st->snp, a, b, mode, p, n);
		}
		break;
	case OP_RECEIVE:
		if (st->net) op_receive(st);
		else kp_snp_udp_stop_advertising(&st->snp);
		break;
	case OP_MESSAGE:
		n = take8(st);
		p = take(st, &n);
		if (st->net) {
			(void)kp_snet_send_message(st->net, (int)(op >> 3 & 15) - 2, p, (uint32_t)n);
			st->signalled |= kp_snet_take_signal(st->net);
		} else if (p) {
			KpNetAddr to = peer_addr(op >> 3 & 15, KP_NET_SOCKET_GAME);
			const KpNetAddr *list[1];
			list[0] = &to;
			kp_snp_udp_send(&st->snp, 1, list, p, n);
		}
		break;
	default:
		if (st->net) op_call(st, op >> 3 & 15);
		else op_list(st);
		break;
	}
	check(st);
}

/* The first byte: snet's game created at once. */
static void stack_start(Stack *st)
{
	unsigned players = take8(st) & 15;
	int id;
	if (st->net && players &&
		(!kp_snet_create_game(st->net, GAME_NAME, "", "", 0, 0, KP_SNET_MODE_ADVERTISED, NULL, 0, players, "Host", "",
			&id) || id != 0))
		fail(st, "the game of %u players not created", players);
}

/* An input run; *digest (optional) what it did before the stack's end. */
static int run(int snet, const uint8_t *d, size_t size, char *why, size_t why_size, Digest *digest)
{
	Stack *st = (Stack *)malloc(sizeof *st);
	int rc;
	if (!st || stack_init(st, snet, d, size, why, why_size, NULL) != 0) {
		free(st);
		snprintf(why, why_size, "out of memory");
		return -1;
	}
	stack_start(st);
	while (!st->failed && st->at < st->size) run_op(st);
	if (digest) *digest = st->digest;
	stack_free(st);
	rc = st->failed ? -1 : 0;
	free(st);
	return rc;
}

int kp_fuzz_net_udp(const uint8_t *d, size_t size, char *why, size_t why_size)
{
	return run(0, d, size, why, why_size, NULL);
}

int kp_fuzz_net_snet(const uint8_t *d, size_t size, char *why, size_t why_size)
{
	return run(1, d, size, why, why_size, NULL);
}

/* --- the seeds: the stack (recorded) and a peer on the in-process LAN --- */

/* An op written to the seed, then run by the stack. */
static void emit(Gen *g, const uint8_t *op, size_t n)
{
	Stack *st = g->st;
	if (g->full || st->failed) return;
	if (g->size + n > SEED_MAX) {
		g->full = 1;
		return;
	}
	memcpy(g->buf + g->size, op, n);
	g->size += n;
	st->size = g->size;
	run_op(st);
}

static void emit1(Gen *g, unsigned op)
{
	uint8_t b = (uint8_t)op;
	emit(g, &b, 1);
}

/* An op with {u8 n, n bytes} after its head bytes. */
static void emit_bytes(Gen *g, const uint8_t *head, size_t h, const void *data, size_t n)
{
	memcpy(g->op, head, h);
	g->op[h] = (uint8_t)n;
	if (n) memcpy(g->op + h + 1, data, n);
	emit(g, g->op, h + 1 + n);
}

static void gen_clock(Gen *g, uint32_t ms)
{
	uint8_t op[3];
	op[0] = OP_CLOCK;
	kp_net_put16(op + 1, ms);
	emit(g, op, sizeof op);
}

/* What reaches the stack: an op of its datagram. */
static void gen_to_stack(void *ud, int socket, const KpNetAddr *from, const uint8_t *data, size_t size)
{
	Gen *g = (Gen *)ud;
	uint32_t ip = kp_net_addr_ip(from), peer = ip == SELF_IP ? SELF_PEER : ip - PEER_IP;
	if (peer > SELF_PEER || size > 0xffff) {
		fail(g->st, "a seed's datagram from %#x", ip);
		return;
	}
	g->op[0] = (uint8_t)((socket == KP_NET_SOCKET_LIST ? OP_LIST : OP_GAME) | peer << 3);
	kp_net_put16(g->op + 1, (uint32_t)size);
	memcpy(g->op + 3, data, size);
	emit(g, g->op, 3 + size);
}

static void gen_to_peer(void *ud, int socket, const KpNetAddr *from, const uint8_t *data, size_t size)
{
	Gen *g = (Gen *)ud;
	if (g->snp_ready && kp_snp_udp_input(&g->snp, socket, from, data, size)) g->signalled = 1;
}

static uint32_t peer_now(void *ud) { return kp_net_lan_now(((Gen *)ud)->lan); }

static void peer_sleep(void *ud, uint32_t ms)
{
	Gen *g = (Gen *)ud;
	gen_run(g, kp_net_lan_now(g->lan) + ms, 0);
}

static void peer_event(void *ud, const KpSnetEvent *e)
{
	(void)ud;
	(void)e;
}

static void pump_peer(Gen *g)
{
	uint32_t wait = kp_snet_pump(g->net, g->signalled);
	g->signalled = kp_snet_take_signal(g->net);
	g->wake = wait == KP_SNET_INFINITE ? KP_SNET_INFINITE : kp_net_lan_now(g->lan) + wait;
}

/* The LAN and the peer until `to`: the stack's datagrams written as ops, and outside its own join its clock's moves
 * and its pump's passes (when it was signalled or its timer ran out); inside its join its waits move its clock and it
 * pumps itself. */
static void gen_run(Gen *g, uint32_t to, int in_join)
{
	Stack *st = g->st;
	for (;;) {
		uint32_t now = kp_net_lan_now(g->lan), next = to, t = kp_net_lan_next(g->lan);
		int due_peer, due_stack, stack_pumps = !in_join && st->net;
		if (g->full || st->failed) return;
		if (t != KP_NET_LAN_IDLE && before(t, next)) next = t;
		if (g->net && g->wake != KP_SNET_INFINITE && before(g->wake, next)) next = g->wake;
		if (stack_pumps && st->wake != KP_SNET_INFINITE && before(st->wake, next)) next = st->wake;
		if (g->signalled || (stack_pumps && st->signalled) || before(next, now)) next = now;
		if (before(now, next) && !in_join) gen_clock(g, next - st->now);
		/* a host's broadcast reaches itself at once: due with the clock where it is */
		if (before(now, next) || (t != KP_NET_LAN_IDLE && !before(next, t))) kp_net_lan_run(g->lan, next);
		due_peer = g->net && (g->signalled || (g->wake != KP_SNET_INFINITE && !before(next, g->wake)));
		due_stack = stack_pumps && (st->signalled || (st->wake != KP_SNET_INFINITE && !before(next, st->wake)));
		if (due_peer) pump_peer(g);
		if (due_stack) emit1(g, OP_PUMP | (st->signalled ? 8u : 0u));
		if (!due_peer && !due_stack && !before(next, to)) return;
	}
}

static int gen_init(Gen *g, Stack *st, int snet, unsigned players, char *why, size_t why_size)
{
	KpNetIo io;
	KpSnetHooks h;
	g->st = st;
	g->wake = KP_SNET_INFINITE;
	if (!(g->buf = (uint8_t *)malloc(SEED_MAX)) || !(g->lan = kp_net_lan_new(LAN_SEED, START))) return -1;
	g->self = kp_net_lan_add(g->lan, SELF_IP, gen_to_stack, g);
	g->peer = kp_net_lan_add(g->lan, PEER_IP, gen_to_peer, g);
	g->self_io = kp_net_lan_io(g->lan, g->self);
	if (stack_init(st, snet, g->buf, 0, why, why_size, g) != 0) return -1;
	io = kp_net_lan_io(g->lan, g->peer);
	kp_snp_udp_init(&g->snp, &io, PROGRAM, VERSION, MAX_PLAYERS);
	g->snp_ready = 1;
	memset(&h, 0, sizeof h);
	h.now = peer_now;
	h.sleep = peer_sleep;
	h.event = peer_event;
	h.ud = g;
	if (!(g->net = kp_snet_new(&g->snp, &h, PROGRAM, VERSION))) return -1;
	g->buf[g->size++] = (uint8_t)players; /* the first byte */
	st->size = g->size;
	stack_start(st);
	return 0;
}

static void gen_free(Gen *g)
{
	if (g->net) kp_snet_free(g->net);
	if (g->snp_ready) kp_snp_udp_free(&g->snp);
	if (g->st) stack_free(g->st);
	if (g->lan) kp_net_lan_free(g->lan);
	free(g->buf);
}

static void gen_wait(Gen *g, uint32_t ms) { gen_run(g, kp_net_lan_now(g->lan) + ms, 0); }

/* Both sides play: each frame the stack receives and sends a turn (its ops), the peer the same. */
static void gen_play(Gen *g, int frames)
{
	static const uint8_t TURN = OP_TURN, RECEIVE = OP_RECEIVE;
	uint8_t turn[TURN_BYTES];
	const uint8_t *data[MAX_PLAYERS];
	uint32_t sizes[MAX_PLAYERS], status[MAX_PLAYERS];
	int k;
	for (k = 0; k < frames && !g->full; k++) {
		memset(turn, 0, sizeof turn);
		turn[0] = (uint8_t)k;
		emit(g, &RECEIVE, 1);
		emit_bytes(g, &TURN, 1, turn, sizeof turn);
		(void)kp_snet_receive_turns(g->net, 0, MAX_PLAYERS, data, sizes, status);
		turn[1] = 1;
		(void)kp_snet_send_turn(g->net, turn, sizeof turn);
		g->signalled |= kp_snet_take_signal(g->net);
		gen_wait(g, SEED_STEP);
	}
}

/* The stack hosts (the seed's first byte); the peer lists the game, joins it, plays, chats and leaves. */
static int seed_host(Gen *g)
{
	static const uint8_t MESSAGE = OP_MESSAGE | 0u << 3; /* to every remote (-2) */
	uint32_t game = 0, hint;
	int id = -1;
	gen_wait(g, 100);
	(void)kp_snet_enum_games(g->net, first_game, &game, &hint); /* its unlock asks for the list */
	gen_wait(g, 100);
	(void)kp_snet_enum_games(g->net, first_game, &game, &hint);
	if (!game || !kp_snet_join_game(g->net, game, NULL, "", "Peer", "", &id)) return -1;
	g->signalled |= kp_snet_take_signal(g->net);
	gen_play(g, SEED_TURNS);
	emit_bytes(g, &MESSAGE, 1, "gl hf", 5);
	(void)kp_snet_send_message(g->net, -2, "gg", 2);
	g->signalled |= kp_snet_take_signal(g->net);
	gen_wait(g, 200);
	(void)kp_snet_leave_game(g->net, REASON_LEAVE);
	gen_wait(g, 500);
	emit1(g, OP_RECEIVE);
	emit1(g, OP_CALL | CALL_PLAYERS << 3);
	return 0;
}

/* The peer hosts; the stack lists the game, joins it, plays and leaves. */
static int seed_join(Gen *g)
{
	static const uint8_t TMPL[16] = { 'K', 'o', 'p', 'r', 'u', 'l', 'u' };
	static const uint8_t JOIN[2] = { OP_CALL | CALL_JOIN << 3, 0 }; /* no password */
	int id = -1;
	if (!kp_snet_create_game(g->net, GAME_NAME, "", "", 0, 0, KP_SNET_MODE_ADVERTISED, TMPL, sizeof TMPL, 4, "Host", "",
			&id))
		return -1;
	gen_wait(g, 100);
	emit1(g, OP_CALL | CALL_ENUM << 3);
	gen_wait(g, 100);
	emit(g, JOIN, sizeof JOIN);
	if (kp_snet_local_id(g->st->net) == (int)KP_SNET_NO_ID) return -1;
	gen_play(g, SEED_TURNS);
	emit1(g, OP_CALL | CALL_LEAVE << 3);
	gen_wait(g, 500);
	return 0;
}

/* The provider alone: an advertisement, a request and another's advertisement on the list, datagrams queued and
 * taken, a send, the list after its entries expired. */
static int seed_provider(Gen *g)
{
	static const uint8_t PACKET[16] = { 0, 0, 16, 0, 1, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0 };
	static const uint8_t ADVERTISE[] = { OP_TURN, KP_SNET_MODE_ADVERTISED, 7, 'K', 'o', 'p', 'r', 'u', 'l', 'u', 4, ',',
		',', '1', ',', 12, 12, 0, 0, 0, 1, 0, 0, 0, 4, 0, 0, 0 };
	static const uint8_t SEND = OP_MESSAGE | 0u << 3; /* to the peer */
	uint8_t list[2];
	emit(g, ADVERTISE, sizeof ADVERTISE);
	memset(g->op, 0, 3 + KP_SNP_HEADER); /* a game-list request from the peer, its length and checksum fixed */
	g->op[0] = OP_LIST | OP_FIX | 0u << 3;
	kp_net_put16(g->op + 1, KP_SNP_HEADER);
	kp_net_put16(g->op + 3 + 4, KP_SNP_REQUEST);
	kp_net_put32(g->op + 3 + 8, PROGRAM);
	kp_net_put32(g->op + 3 + 12, VERSION);
	emit(g, g->op, 3 + KP_SNP_HEADER);
	/* the peer's request at its start and its advertisement reach the stack through the LAN */
	if (!kp_snp_udp_advertise(&g->snp, "Peer", ",,", KP_SNET_MODE_ADVERTISED, NULL, 0)) return -1;
	kp_net_lan_run(g->lan, kp_net_lan_now(g->lan) + SEED_STEP);
	gen_clock(g, SEED_STEP);
	g->op[0] = OP_GAME | OP_FIX | 1u << 3;
	kp_net_put16(g->op + 1, sizeof PACKET);
	memcpy(g->op + 3, PACKET, sizeof PACKET);
	emit(g, g->op, 3 + sizeof PACKET);
	emit1(g, OP_PUMP);
	list[0] = OP_CALL;
	list[1] = 1; /* the game of id 1 */
	emit(g, list, sizeof list);
	emit_bytes(g, &SEND, 1, PACKET, sizeof PACKET);
	gen_clock(g, KP_SNP_GAME_LIFETIME + 1);
	list[1] = 0;
	emit(g, list, sizeof list);
	emit1(g, OP_RECEIVE);
	return 0;
}

int kp_fuzz_net_seeds(KpFuzzNetSeed fn, void *ud, char *why, size_t why_size)
{
	static const struct {
		int snet;
		unsigned players; /* the first byte */
		const char *name;
		int (*script)(Gen *g);
	} SEEDS[] = { { 0, 0, "provider", seed_provider }, { 1, 4, "host", seed_host }, { 1, 0, "join", seed_join } };
	int k;
	for (k = 0; k < (int)(sizeof SEEDS / sizeof SEEDS[0]); k++) {
		Gen *g = (Gen *)calloc(1, sizeof *g);
		Stack *st = (Stack *)calloc(1, sizeof *st);
		int rc = -1;
		why[0] = 0;
		if (g && st && gen_init(g, st, SEEDS[k].snet, SEEDS[k].players, why, why_size) == 0) {
			Digest got;
			rc = SEEDS[k].script(g);
			if (st->failed || g->full) rc = -1;
			/* the seed replayed does what its session did; a game's turns from the peer arrived */
			if (rc == 0 && run(SEEDS[k].snet, g->buf, g->size, why, why_size, &got) != 0) rc = -1;
			if (rc == 0 && memcmp(&got, &st->digest, sizeof got) != 0) {
				snprintf(why, why_size, "%s: replayed, %u/%u received, %#x/%#x sent, %u/%u turns", SEEDS[k].name,
					got.received, st->digest.received, got.sent, st->digest.sent, got.turns, st->digest.turns);
				rc = -1;
			}
			if (rc == 0 && SEEDS[k].snet && got.turns < SEED_TURNS / 2) {
				snprintf(why, why_size, "%s: %u of the peer's turns arrived", SEEDS[k].name, got.turns);
				rc = -1;
			}
			if (rc == 0) fn(ud, SEEDS[k].snet, SEEDS[k].name, g->buf, g->size);
		}
		if (rc != 0 && !why[0])
			snprintf(why, why_size, "%s: %s", SEEDS[k].name, g && g->full ? "the seed outgrew its bytes"
				: "the session did not go through");
		if (g) gen_free(g);
		free(g);
		free(st);
		if (rc != 0) return -1;
	}
	return k;
}
