/* Storm's network layer SNet over the UDP provider (kp_snet.h; phase N, stage N2, atlas S01 and S02). */
#include "kp_snet.h"

#include <stdlib.h>
#include <string.h>

const KpSnetCaps KP_SNET_UDP_CAPS = {0x24, 3, 504, 16, 256, 100000, 50, 8, 2};

enum { CONTROL, MESSAGES, TURNS, DATAGRAMS };
enum { CMD_CONNECT = 1, CMD_ACCEPT, CMD_CONNECT_ACK, CMD_PING, CMD_PONG, CMD_PLAYER, CMD_JOIN, CMD_GAME, CMD_TEMPLATE,
	CMD_REJECT, CMD_LEAVE, CMD_DROP, CMD_HOST, CMD_MODE, CMD_LADDER, CMD_COUNT };

#define FLAG_ACK 1u
#define FLAG_RESEND 2u
#define FLAG_RELAYED 4u
#define PLAYER_JOINING 4u   /* record +0x118 */
#define PLAYER_DROPPING 8u
#define PING_IDLE 20000u    /* RunNetTimers' idle ping */
#define FORGET 50000u       /* a record without id is deleted after this silence */
#define SEND_LIMIT 0x1f00u  /* the locked send buffer's largest packet */
#define PACKET_MAX 0xffffu  /* the header's u16 length */
#define CAPS_LOCKED_BUFFER 1u
#define MAXMSG_RESERVE 16u  /* ScanProviders keeps maxmsg - 16 */
#define REQUEST_SIZE 13     /* a resend request naming a player */
#define PLAYER_INFO_HEAD 0x24
/* Bounds the original lacks (kp_snet.h): remote records, and packet nodes when a datagram is filed. */

/* A packet node (0x20 bytes in Storm): the packet and its length, the address it came from or goes to, the creation or
 * receipt tick and the timer (+0x1C). */
typedef struct Node {
	struct Node *next;
	KpNetAddr addr;
	uint32_t tick, timer, size;
	uint8_t *p;
} Node;

typedef struct {
	Node *head, *tail;
} List;

/* A player record (0x214 bytes in Storm; atlas S02 "Player records"). */
typedef struct Player {
	struct Player *next;
	char name[KP_SNET_STRING], desc[KP_SNET_STRING];
	KpNetAddr addr;
	uint32_t flags, heard, turn_request, ping, rtt, rtt_max;
	List out[KP_SNET_CLASSES], in[KP_SNET_CLASSES];
	Node *delivered[KP_SNET_CLASSES];
	List history;
	uint16_t send_seq[KP_SNET_CLASSES], recv_seq[KP_SNET_CLASSES], ack2;
	uint16_t ack_owed[KP_SNET_CLASSES], ack_sent[KP_SNET_CLASSES];
	uint32_t ack_timer[KP_SNET_CLASSES];
	uint32_t host, connecting, connect_tick, drop_reason;
	uint16_t drop_turn;
	uint8_t id, old_id;
} Player;

typedef struct Event {
	struct Event *next;
	uint32_t id;
	int32_t player;
	uint32_t size;
	uint8_t *data;
} Event;

struct KpSnet {
	KpSnpUdp *snp;
	KpSnetHooks hooks;
	uint32_t program, version;
	KpSnetCaps caps;                                /* maxmsg already reduced */
	uint32_t t_ack, t_gap, t_ack2, t_heard, t_join; /* 0x15066714, 0x15066718, 0x15066708, 0x15066710, 0x1506670c */
	uint32_t send_clock;                            /* 0x1506b8f4 */
	Player *local, *remotes, *remotes_tail;
	int remote_count, nodes;
	uint8_t local_id;                               /* 0x15066600 */
	int joining;                                    /* 0x1506bad8 */
	int in_join;                                    /* kp_snet_join_game runs: the pump waits */
	KpNetAddr join_addr;                            /* while joining: the joined game's address (kp_snet.h) */
	int seen[CMD_COUNT];                            /* 0x1506baf0 */
	char name[KP_SNET_STRING], stats[KP_SNET_STRING], password[KP_SNET_STRING];
	uint32_t mode, start_tick, game_type, ladder_type, max_players;
	uint8_t *tmpl;
	uint32_t tmpl_size;
	Event *events, *events_tail;
	uint32_t error;
	int signal;
	Player *dispatching; /* the record whose control packets are being dispatched; NULL once deleted */
	uint8_t rx[KP_SNP_DATAGRAM];
	uint8_t scratch[PACKET_MAX];
};

static uint32_t now_of(const KpSnet *s) { return s->hooks.now(s->hooks.ud); }

static int fail(KpSnet *s, uint32_t code)
{
	s->error = code;
	return 0;
}

static uint32_t max_u32(uint32_t a, uint32_t b) { return a > b ? a : b; }

static void keep_min(uint32_t *result, int32_t left)
{
	if ((uint32_t)left < *result) *result = (uint32_t)left;
}

/* SStrCopy(dst, src, 0x80) */
static void copy_string(char *dst, const char *src)
{
	size_t n = src ? strlen(src) : 0;
	if (n > KP_SNET_STRING - 1) n = KP_SNET_STRING - 1;
	if (n) memcpy(dst, src, n);
	dst[n] = 0;
}

/* --- payload reading: a command shorter than its payload reads its missing bytes as zeros --- */

static uint32_t pay32(const uint8_t *p, uint32_t size, uint32_t off)
{
	uint8_t b[4] = {0, 0, 0, 0};
	uint32_t k;
	for (k = 0; k < 4; k++)
		if (off + k < size) b[k] = p[off + k];
	return kp_net_get32(b);
}

/* SStrCopy(dst, the string at off, 0x80) when dst is given; returns the offset after the source string and its NUL. */
static uint32_t pay_string(char *dst, const uint8_t *p, uint32_t size, uint32_t off)
{
	uint32_t n = 0, k;
	if (off < size)
		while (off + n < size && p[off + n]) n++;
	if (dst) {
		k = n < KP_SNET_STRING - 1 ? n : KP_SNET_STRING - 1;
		if (k) memcpy(dst, p + off, k);
		dst[k] = 0;
	}
	return off + n + 1;
}

/* _stricmp(a, the string at off) in the C locale */
static int pay_casecmp(const char *a, const uint8_t *p, uint32_t size, uint32_t off)
{
	for (;; a++, off++) {
		int x = (unsigned char)*a, y = off < size ? p[off] : 0;
		if (x >= 'A' && x <= 'Z') x += 'a' - 'A';
		if (y >= 'A' && y <= 'Z') y += 'a' - 'A';
		if (x != y || !x) return x - y;
	}
}

/* --- packet nodes and lists --- */

static Node *node_new(KpSnet *s, const KpNetAddr *addr, const uint8_t *data, uint32_t size)
{
	Node *n = (Node *)malloc(sizeof *n + size);
	if (!n) return NULL;
	memset(n, 0, sizeof *n);
	n->p = (uint8_t *)(n + 1);
	if (addr) n->addr = *addr;
	n->size = size;
	if (data && size) memcpy(n->p, data, size);
	s->nodes++;
	return n;
}

static void node_free(KpSnet *s, Node *n)
{
	if (!n) return;
	s->nodes--;
	free(n);
}

static uint16_t seq_of(const Node *n) { return (uint16_t)kp_net_get16(n->p + 4); }

static void list_append(List *l, Node *n)
{
	n->next = NULL;
	if (l->tail) l->tail->next = n;
	else l->head = n;
	l->tail = n;
}

static Node *list_pop(List *l)
{
	Node *n = l->head;
	if (!n) return NULL;
	l->head = n->next;
	if (!l->head) l->tail = NULL;
	n->next = NULL;
	return n;
}

/* n after prev (NULL: at the head) */
static void list_insert(List *l, Node *prev, Node *n)
{
	if (!prev) {
		n->next = l->head;
		l->head = n;
	} else {
		n->next = prev->next;
		prev->next = n;
	}
	if (!n->next) l->tail = n;
}

static void list_free(KpSnet *s, List *l)
{
	Node *n;
	while ((n = list_pop(l))) node_free(s, n);
}

static Node *list_find(const List *l, uint16_t seq)
{
	Node *n;
	for (n = l->head; n; n = n->next)
		if (seq_of(n) == seq) return n;
	return NULL;
}

/* --- player records --- */

/* CreatePlayerRecord 0x150204c0 */
static Player *player_new(KpSnet *s, const KpNetAddr *addr)
{
	Player *r = (Player *)calloc(1, sizeof *r);
	if (!r) return NULL;
	r->addr = *addr;
	r->id = r->old_id = KP_SNET_NO_ID;
	r->heard = now_of(s);
	return r;
}

static void player_free(KpSnet *s, Player *r)
{
	int c;
	for (c = 0; c < KP_SNET_CLASSES; c++) {
		list_free(s, &r->out[c]);
		list_free(s, &r->in[c]);
		node_free(s, r->delivered[c]);
	}
	list_free(s, &r->history);
	if (s->dispatching == r) s->dispatching = NULL;
	free(r);
}

static Player *remote_new(KpSnet *s, const KpNetAddr *addr)
{
	Player *r;
	if (s->remote_count >= KP_SNET_RECORDS) return NULL;
	if (!(r = player_new(s, addr))) return NULL;
	if (s->remotes_tail) s->remotes_tail->next = r;
	else s->remotes = r;
	s->remotes_tail = r;
	s->remote_count++;
	return r;
}

/* DeletePlayerRecord 0x15020b10 */
static void remote_delete(KpSnet *s, Player *r)
{
	Player **at = &s->remotes, *prev = NULL;
	while (*at && *at != r) {
		prev = *at;
		at = &(*at)->next;
	}
	if (!*at) return;
	*at = r->next;
	if (s->remotes_tail == r) s->remotes_tail = prev;
	s->remote_count--;
	player_free(s, r);
}

/* FreeAllPlayerRecords 0x15020890 */
static void free_all_records(KpSnet *s)
{
	while (s->remotes) remote_delete(s, s->remotes);
	if (s->local) player_free(s, s->local);
	s->local = NULL;
}

/* EnsureLocalRecord 0x15020ae0: the local record has an all-zero address. */
static Player *ensure_local(KpSnet *s)
{
	if (!s->local) {
		KpNetAddr zero;
		memset(&zero, 0, sizeof zero);
		s->local = player_new(s, &zero);
	}
	return s->local;
}

static Player *find_remote(const KpSnet *s, const KpNetAddr *addr)
{
	Player *r;
	for (r = s->remotes; r; r = r->next)
		if (kp_net_addr_eq(&r->addr, addr)) return r;
	return NULL;
}

/* FindOrCreateRemoteByAddress 0x150209f0 (NULL beyond KP_SNET_RECORDS records) */
static Player *find_or_create(KpSnet *s, const KpNetAddr *addr)
{
	Player *r = find_remote(s, addr);
	return r ? r : remote_new(s, addr);
}

/* FindRecordById 0x15020aa0: the local record first; the low byte is compared. */
static Player *find_by_id(const KpSnet *s, uint32_t id)
{
	uint8_t b = (uint8_t)id;
	Player *r;
	if (b == KP_SNET_NO_ID) return NULL;
	if (s->local && s->local->id == b) return s->local;
	for (r = s->remotes; r; r = r->next)
		if (r->id == b) return r;
	return NULL;
}

/* FindDroppedRecordByOldId 0x15020a50 */
static Player *find_dropped(const KpSnet *s, uint32_t id)
{
	uint8_t b = (uint8_t)id;
	Player *r;
	if (b == KP_SNET_NO_ID) return NULL;
	if (s->local && s->local->old_id == b && s->local->id == KP_SNET_NO_ID) return s->local;
	for (r = s->remotes; r; r = r->next)
		if (r->old_id == b && r->id == KP_SNET_NO_ID) return r;
	return NULL;
}

/* AssignPlayerId 0x15020570 */
static void assign_id(KpSnet *s, Player *r, uint8_t id)
{
	Player *x;
	r->old_id = KP_SNET_NO_ID;
	r->id = id;
	if (s->local && s->local->old_id == id) s->local->old_id = KP_SNET_NO_ID;
	for (x = s->remotes; x; x = x->next)
		if (x->old_id == id) x->old_id = KP_SNET_NO_ID;
}

/* The local seat plus the remotes with an id; 0 outside a game (BuildPlayerCountInfo 0x15021740). */
static uint32_t player_count(const KpSnet *s)
{
	uint32_t n = 1;
	Player *r;
	if (!s->snp || s->local_id == KP_SNET_NO_ID) return 0;
	for (r = s->remotes; r; r = r->next)
		if (r->id != KP_SNET_NO_ID) n++;
	return n;
}

/* --- events (SNetQueueEvent 0x15024800, dispatch 0x150233d0) --- */

static void queue_event(KpSnet *s, uint32_t id, int32_t player, const void *data, uint32_t size)
{
	Event *e = (Event *)malloc(sizeof *e + size);
	if (!e) return;
	e->next = NULL;
	e->id = id;
	e->player = player;
	e->size = data ? size : 0;
	e->data = e->size ? (uint8_t *)(e + 1) : NULL;
	if (e->size) memcpy(e->data, data, e->size);
	if (s->events_tail) s->events_tail->next = e;
	else s->events = e;
	s->events_tail = e;
}

/* Each event is taken from the queue under the driver's lock and handed over with the lock released. */
static void dispatch_events(KpSnet *s)
{
	Event *e;
	while ((e = s->events)) {
		KpSnetEvent ev;
		s->events = e->next;
		if (!s->events) s->events_tail = NULL;
		ev.id = e->id;
		ev.player = e->player;
		ev.data = e->data;
		ev.size = e->size;
		if (s->hooks.unlock) s->hooks.unlock(s->hooks.ud);
		if (s->hooks.event) s->hooks.event(s->hooks.ud, &ev);
		if (s->hooks.lock) s->hooks.lock(s->hooks.ud);
		free(e);
	}
}

static void free_events(KpSnet *s)
{
	while (s->events) {
		Event *e = s->events;
		s->events = e->next;
		free(e);
	}
	s->events_tail = NULL;
}

/* --- sending --- */

/* ProviderSend 0x150219c0: the send clock advances by the packet's transmission time. */
static int provider_send(KpSnet *s, const KpNetAddr *to, const uint8_t *p, uint32_t size)
{
	uint32_t now = now_of(s);
	const KpNetAddr *list[1];
	if (!s->snp) return 0;
	if (now - s->send_clock < 0x7fffffffu) s->send_clock = now;
	s->send_clock += (size + 64) * 1000 / s->caps.bytes_per_sec;
	if ((s->caps.flags & CAPS_LOCKED_BUFFER) && size > SEND_LIMIT) return 0;
	list[0] = to;
	kp_snp_udp_send(s->snp, 1, list, p, size);
	return 1;
}

static void put_header(uint8_t *p, uint32_t size, uint32_t seq, uint32_t ack, uint32_t cls, uint32_t cmd,
	uint32_t sender, uint32_t flags)
{
	kp_net_put16(p, 0);
	kp_net_put16(p + 2, size);
	kp_net_put16(p + 4, seq);
	kp_net_put16(p + 6, ack);
	p[8] = (uint8_t)cls;
	p[9] = (uint8_t)cmd;
	p[10] = (uint8_t)sender;
	p[11] = (uint8_t)flags;
}

static uint8_t sender_id(const KpSnet *s) { return s->local ? s->local->id : KP_SNET_NO_ID; }

/* TransmitPacket 0x15021540: the ack travels with the packet. */
static void transmit(KpSnet *s, Player *r, const uint8_t *p)
{
	unsigned c = p[8] & 3;
	provider_send(s, &r->addr, p, kp_net_get16(p + 2));
	r->ack_sent[c] = (uint16_t)kp_net_get16(p + 6);
	r->ack_timer[c] = 0;
}

/* ResendPacket 0x150211a0: a copy with the record's current ack; the stored packet is unchanged. */
static void resend(KpSnet *s, Player *r, const uint8_t *p)
{
	uint32_t size = kp_net_get16(p + 2);
	unsigned c = p[8] & 3;
	memcpy(s->scratch, p, size);
	kp_net_put16(s->scratch + 6, r->ack_owed[c]);
	kp_net_checksum(s->scratch, size);
	provider_send(s, &r->addr, s->scratch, size);
	r->ack_sent[c] = r->ack_owed[c];
	r->ack_timer[c] = 0;
}

/* A 12-byte ack or gap request of RunNetTimers, sent straight to the provider. */
static void send_bare(KpSnet *s, Player *r, uint32_t seq, unsigned cls, uint32_t flags)
{
	uint8_t p[KP_SNET_HEADER];
	put_header(p, sizeof p, seq, r->ack_owed[cls], cls, 0, sender_id(s), flags);
	kp_net_checksum(p, sizeof p);
	provider_send(s, &r->addr, p, sizeof p);
	r->ack_sent[cls] = r->ack_owed[cls];
	r->ack_timer[cls] = 0;
}

/* SendPacket 0x15021280: numbered per destination and class, queued until acknowledged (loopback: filed at once). */
static Node *send_packet(KpSnet *s, Player *to, unsigned cls, unsigned cmd, const void *data, uint32_t size)
{
	Player *local = ensure_local(s);
	uint32_t len = size + KP_SNET_HEADER, seq;
	Node *n;
	if (!local || !to || len > PACKET_MAX) return NULL;
	if (!(n = node_new(s, &to->addr, NULL, len))) return NULL;
	if (cls == CONTROL && cmd == CMD_CONNECT) seq = 0;
	else seq = to->send_seq[cls]++;
	put_header(n->p, len, seq, to->ack_owed[cls], cls, cmd, local->id, 0);
	if (size) memcpy(n->p + KP_SNET_HEADER, data, size);
	kp_net_checksum(n->p, len);
	n->tick = now_of(s);
	if (to == local) {
		list_append(&local->in[cls], n);
		return n;
	}
	if (cls == DATAGRAMS) {
		transmit(s, to, n->p);
		node_free(s, n);
		return NULL;
	}
	list_append(&to->out[cls], n);
	transmit(s, to, n->p);
	n->timer = s->send_clock;
	return n;
}

static void send_u32(KpSnet *s, Player *to, unsigned cmd, uint32_t v)
{
	uint8_t b[4];
	kp_net_put32(b, v);
	send_packet(s, to, CONTROL, cmd, b, sizeof b);
}

/* BuildPlayerInfoMessage 0x150232c0: command 6 about r. */
static uint32_t player_info(uint8_t *out, const Player *r, uint32_t start)
{
	size_t n = strlen(r->name), d = strlen(r->desc);
	uint32_t size = (uint32_t)(PLAYER_INFO_HEAD + n + 1 + d + 1);
	kp_net_put32(out, size);
	kp_net_put32(out + 4, r->id);
	kp_net_put32(out + 8, r->host);
	kp_net_put32(out + 12, r->flags);
	kp_net_put32(out + 16, start);
	memcpy(out + 20, r->addr.b, KP_NET_ADDR);
	memcpy(out + PLAYER_INFO_HEAD, r->name, n + 1);
	memcpy(out + PLAYER_INFO_HEAD + n + 1, r->desc, d + 1);
	return size;
}

/* AdvertiseGame 0x15021be0 */
static int advertise(KpSnet *s)
{
	uint8_t rec[12];
	if (!s->snp) return 0;
	kp_net_put32(rec, sizeof rec);
	kp_net_put32(rec + 4, player_count(s));
	kp_net_put32(rec + 8, s->max_players);
	/* Storm reads the clock here for the game's age in seconds, which the slot passes on and only Battle.net's
	 * provider uses: the read itself is kept, as the other reads are where Storm's are (on_datagram) */
	(void)now_of(s);
	return kp_snp_udp_advertise(s->snp, s->name, s->stats, s->mode, rec, sizeof rec);
}

/* --- receiving --- */

/* ProcessAck 0x15021030 */
static void process_ack(KpSnet *s, Player *r, unsigned cls, uint16_t ack)
{
	List *q = &r->out[cls];
	int freed = 0;
	while (q->head) {
		uint16_t seq = seq_of(q->head);
		if (seq == ack || (uint16_t)(ack - seq) > 0x7fff) break;
		node_free(s, list_pop(q));
		freed = 1;
	}
	if (cls == TURNS) r->ack2 = ack;
	if (freed && q->head) {
		uint32_t now = now_of(s);
		if (now - q->head->timer < 0x7fffffffu) q->head->timer = now;
	}
}

static int ack_eligible(const Player *x, uint16_t u)
{
	return x->id != KP_SNET_NO_ID && (!(x->flags & PLAYER_JOINING) || (int32_t)((uint32_t)u - x->recv_seq[TURNS]) >= 0);
}

/* The collective turn ack (ReceivePackets step 9): the first turn some eligible remote has not delivered. */
static void ack_turns(KpSnet *s, Player *r)
{
	uint16_t u = r->ack_owed[TURNS];
	Player *x;
	if ((uint16_t)(r->recv_seq[TURNS] - r->ack_owed[TURNS]) < 0x7fff) u = r->recv_seq[TURNS];
	if (!s->remotes) return;
	for (;;) {
		int held = 0, all = 1;
		for (x = s->remotes; x; x = x->next) {
			if (!ack_eligible(x, u)) continue;
			if (list_find(&x->in[TURNS], u)) held = 1;
			else all = 0;
		}
		if (!held || !all) break;
		u++;
	}
	for (x = s->remotes; x; x = x->next) {
		if (u == x->ack_owed[TURNS] || !ack_eligible(x, u)) continue;
		x->ack_owed[TURNS] = u;
		if (!x->ack_timer[TURNS]) x->ack_timer[TURNS] = now_of(s);
	}
}

static void on_control(KpSnet *s, const KpNetAddr *from, const uint8_t *p, uint32_t size, uint8_t sender, unsigned cmd);

/* Koprulu's: whether a control command from record r (the datagram came from `from`) counts (kp_snet.h). An ignored
 * one is consumed like any other but neither handled nor seen by a join's wait. */
static int control_accepted(const KpSnet *s, const Player *r, const KpNetAddr *from, unsigned cmd)
{
	switch (cmd) {
	case CMD_ACCEPT:
	case CMD_GAME:
	case CMD_TEMPLATE:
	case CMD_REJECT:
		return s->joining && kp_net_addr_eq(from, &s->join_addr);
	case CMD_PLAYER:
		return s->joining ? kp_net_addr_eq(from, &s->join_addr) : r->id != KP_SNET_NO_ID;
	case CMD_DROP:
	case CMD_HOST:
	case CMD_MODE:
	case CMD_LADDER:
		return r->id != KP_SNET_NO_ID;
	default:
		return 1;
	}
}

/* DispatchControlPackets 0x15023e40: the in-sequence control packets, each to its handler. */
static void dispatch_control(KpSnet *s, Player *r)
{
	s->dispatching = r;
	while (r->in[CONTROL].head && seq_of(r->in[CONTROL].head) == r->recv_seq[CONTROL]) {
		Node *n = list_pop(&r->in[CONTROL]);
		unsigned cmd = n->p[9];
		if (!control_accepted(s, r, &n->addr, cmd)) {
			node_free(s, n);
			r->recv_seq[CONTROL]++;
			continue;
		}
		if (cmd < CMD_COUNT) s->seen[cmd] = 1;
		on_control(s, &n->addr, n->p + KP_SNET_HEADER, n->size - KP_SNET_HEADER, n->p[10], cmd);
		node_free(s, n);
		/* a handler deleted the record (the original goes on with freed memory) */
		if (s->dispatching != r) break;
		r->recv_seq[CONTROL]++;
	}
	s->dispatching = NULL;
}

/* A resend request (ReceivePackets step 6); never filed. */
static void on_resend_request(KpSnet *s, Player *r, const uint8_t *d, uint32_t len, unsigned cls, uint16_t seq)
{
	Player *local = s->local, *x;
	Node *n = NULL;
	if (len == KP_SNET_HEADER || d[12] == s->local_id) {
		if (cls == TURNS) {
			n = list_find(&local->in[TURNS], seq);
			if (!n) n = list_find(&local->history, seq);
		}
		if (!n) n = list_find(&r->out[cls], seq);
		if (n) resend(s, r, n->p);
		return;
	}
	if (cls != TURNS) return;
	if (!(x = find_by_id(s, d[12])) && !(x = find_dropped(s, d[12]))) return;
	if (x->id == KP_SNET_NO_ID && x->old_id == d[12]) {
		uint8_t b[12];
		kp_net_put32(b, x->old_id);
		kp_net_put32(b + 4, x->drop_turn);
		kp_net_put32(b + 8, x->drop_reason);
		send_packet(s, r, CONTROL, CMD_DROP, b, sizeof b);
	}
	n = list_find(&x->in[TURNS], seq);
	if (!n) n = list_find(&x->history, seq);
	if (!n) return;
	n->p[11] |= FLAG_RELAYED;
	kp_net_checksum(n->p, kp_net_get16(n->p + 2));
	resend(s, r, n->p);
}

/* Koprulu's: a relayed packet counts only in class 2 and only from the address of a record with a player id (kp_snet.h):
 * an honest seat relays a turn only to a seat that asked it, and asks only seats it holds with an id. */
static int relay_accepted(const KpSnet *s, const KpNetAddr *from, unsigned cls)
{
	const Player *x = cls == TURNS ? find_remote(s, from) : NULL;
	return x && x->id != KP_SNET_NO_ID;
}

/* Koprulu's bounds on what a record files (kp_snet.h): the packets in its receive queues, then the whole stack's. */
static int may_file(const KpSnet *s, const Player *r)
{
	uint32_t filed = 0, cap = r->id != KP_SNET_NO_ID ? KP_SNET_PEER_FILED : KP_SNET_UNKNOWN_FILED;
	unsigned c;
	if (s->nodes >= (r->id != KP_SNET_NO_ID ? KP_SNET_NODES : KP_SNET_NODES / 2)) return 0;
	for (c = 0; c < KP_SNET_CLASSES; c++) {
		const Node *n;
		for (n = r->in[c].head; n; n = n->next)
			if (++filed >= cap) return 0;
	}
	return 1;
}

/* ReceivePackets 0x15021fa0 for one datagram. It reads the clock anew where Storm calls GetTickCount (a connection's
 * repeat check, the sender heard, an old packet's ack timer, a new packet's time, the ack timer started), never
 * once for the datagram: a clock that moves while it is read (the network oracle's) gives the original's values. */
static void on_datagram(KpSnet *s, const KpNetAddr *from, const uint8_t *d, uint32_t size)
{
	Player *r, *local = s->local;
	uint32_t flags = size >= KP_SNET_HEADER ? d[11] : 0, len, now;
	unsigned cls, cmd;
	uint16_t seq, ack, next;
	Node *n, *prev, *at;
	if (flags & FLAG_RELAYED) r = find_by_id(s, d[10]);
	else r = find_or_create(s, from);
	if (!r || !local || size < KP_SNET_HEADER) return;
	len = kp_net_get16(d + 2);
	cls = d[8];
	if (len < KP_SNET_HEADER || len > size || cls >= KP_SNET_CLASSES || kp_net_sum(d, len) != 0) return;
	if ((flags & FLAG_RELAYED) && !relay_accepted(s, from, cls)) return;
	if (!s->joining && !(flags & FLAG_RELAYED) && d[10] != KP_SNET_NO_ID && find_by_id(s, d[10]) != r) return;
	seq = (uint16_t)kp_net_get16(d + 4);
	ack = (uint16_t)kp_net_get16(d + 6);
	cmd = d[9];
	if (cls == CONTROL && cmd == CMD_CONNECT) {
		Player *c = find_or_create(s, from);
		int repeat;
		now = now_of(s);
		repeat = c && now - c->connect_tick < s->t_heard;
		if (c && !repeat) remote_delete(s, c);
		if (len == KP_SNET_HEADER + 4 && kp_net_get32(d + KP_SNET_HEADER) == 1 && !repeat && (c = remote_new(s, from))) {
			c->connecting = 1;
			c->recv_seq[CONTROL] = c->ack_owed[CONTROL] = c->send_seq[CONTROL] = 1;
			c->connect_tick = now;
			send_u32(s, c, CMD_ACCEPT, 1);
		}
		return;
	}
	if (!(flags & FLAG_RELAYED)) {
		r->heard = now_of(s);
		process_ack(s, r, cls, ack);
	}
	if (flags & FLAG_ACK) return;
	if (flags & FLAG_RESEND) {
		on_resend_request(s, r, d, len, cls, seq);
		return;
	}
	if (cls == DATAGRAMS) return; /* not filed: nothing reads it (kp_snet.h) */
	next = r->recv_seq[cls];
	if (next != seq && (uint16_t)(next - seq) < 0x7fff) {
		if (cls != TURNS) {
			r->ack_sent[cls] = (uint16_t)(r->ack_owed[cls] - 1);
			r->ack_timer[cls] = now_of(s);
		}
		return;
	}
	if (!may_file(s, r) || !(n = node_new(s, from, d, len))) return;
	n->tick = now = now_of(s);
	n->timer = now - s->t_gap + s->t_ack;
	for (prev = NULL, at = r->in[cls].head; at; prev = at, at = at->next) {
		if (seq_of(at) == seq) {
			node_free(s, n);
			return;
		}
		if ((uint16_t)(seq - seq_of(at)) > 0x7ffe) break;
	}
	list_insert(&r->in[cls], prev, n);
	if (cls == TURNS) {
		ack_turns(s, r);
	} else {
		uint16_t old = r->ack_owed[cls];
		r->ack_owed[cls] = r->recv_seq[cls];
		for (at = r->in[cls].head; at && seq_of(at) == r->ack_owed[cls]; at = at->next) r->ack_owed[cls]++;
		if (r->ack_owed[cls] != old && !r->ack_timer[cls]) r->ack_timer[cls] = now_of(s);
	}
	if (cls == CONTROL) dispatch_control(s, r);
}

static void receive_packets(KpSnet *s)
{
	KpNetAddr from;
	uint32_t size;
	if (!s->snp) return;
	while (kp_snp_udp_receive(s->snp, &from, s->rx, &size)) on_datagram(s, &from, s->rx, size);
}

/* --- control commands --- */

/* Command 6 0x15023660: a player's record, created fresh unless it describes the sender. */
static void on_player_info(KpSnet *s, const KpNetAddr *from, const uint8_t *p, uint32_t size, uint8_t sender)
{
	uint32_t id = pay32(p, size, 4), start = pay32(p, size, 16) & 0xffff, off, k;
	KpNetAddr at = *from;
	Player *r, *local;
	Node *n;
	List *replay[2];
	if (id != sender) {
		for (k = 0; k < KP_NET_ADDR; k++) at.b[k] = 20 + k < size ? p[20 + k] : 0;
		/* found or created, then deleted, as Storm does: a record created here reads the clock */
		if ((r = find_or_create(s, &at))) remote_delete(s, r);
	}
	r = find_or_create(s, &at);
	local = ensure_local(s);
	if (!r || !local) return;
	assign_id(s, r, (uint8_t)id);
	r->flags = pay32(p, size, 12);
	r->host = pay32(p, size, 8);
	r->recv_seq[TURNS] = r->ack_owed[TURNS] = (uint16_t)start;
	r->send_seq[TURNS] = local->send_seq[TURNS];
	if (r->recv_seq[TURNS] != local->recv_seq[TURNS] && (uint16_t)(r->recv_seq[TURNS] - local->recv_seq[TURNS]) < 0x7fff)
		r->flags |= PLAYER_JOINING;
	r->addr = at;
	off = pay_string(r->name, p, size, PLAYER_INFO_HEAD);
	pay_string(r->desc, p, size, off);
	replay[0] = &local->history;
	replay[1] = &local->in[TURNS];
	for (k = 0; k < 2; k++)
		for (n = replay[k]->head; n; n = n->next)
			if ((uint16_t)(seq_of(n) - start) < 0x8000) resend(s, r, n->p);
	if (s->local_id != KP_SNET_NO_ID && !(r->flags & PLAYER_JOINING))
		queue_event(s, KP_SNET_EVENT_PLAYER, (int32_t)id, NULL, 0);
}

/* Command 7 0x15023840 (host): admit the joiner, or command 10. */
static void on_join_request(KpSnet *s, const KpNetAddr *from, const uint8_t *p, uint32_t size)
{
	uint32_t id, desc, pass, start, k, n;
	uint8_t buf[20 + 3 * KP_SNET_STRING], info[PLAYER_INFO_HEAD + 2 * KP_SNET_STRING];
	Player *local, *j, *x;
	Node *t;
	if (player_count(s) >= s->max_players || (s->mode & KP_SNET_MODE_CLOSED)) goto reject;
	desc = pay_string(NULL, p, size, 0);
	pass = pay_string(NULL, p, size, desc);
	if (s->password[0] && pay_casecmp(s->password, p, size, pass) != 0) goto reject;
	for (id = 0; find_by_id(s, id);)
		if ((id = (id + 1) & 0xff) == KP_SNET_NO_ID) goto reject;
	if (id >= s->max_players) goto reject;
	local = ensure_local(s);
	if (!local || !(j = find_or_create(s, from))) goto reject;
	assign_id(s, j, (uint8_t)id);
	j->flags = 0;
	j->connecting = 0;
	pay_string(j->name, p, size, 0);
	pay_string(j->desc, p, size, desc);
	start = local->send_seq[TURNS];
	j->recv_seq[TURNS] = j->ack_owed[TURNS] = j->send_seq[TURNS] = (uint16_t)start;
	if (j->recv_seq[TURNS] != local->recv_seq[TURNS] && (uint16_t)(j->recv_seq[TURNS] - local->recv_seq[TURNS]) < 0x7fff)
		j->flags |= PLAYER_JOINING;
	if (player_count(s) >= s->max_players) s->mode |= KP_SNET_MODE_FULL;
	advertise(s);
	kp_net_put32(buf, id);
	kp_net_put32(buf + 4, s->max_players);
	kp_net_put32(buf + 8, start);
	kp_net_put32(buf + 12, s->mode);
	kp_net_put32(buf + 16, (now_of(s) - s->start_tick) / 1000);
	n = 20;
	k = (uint32_t)strlen(s->name) + 1;
	memcpy(buf + n, s->name, k);
	n += k;
	k = (uint32_t)strlen(s->stats) + 1;
	memcpy(buf + n, s->stats, k);
	n += k;
	k = (uint32_t)strlen(s->password) + 1;
	memcpy(buf + n, s->password, k);
	n += k;
	send_packet(s, j, CONTROL, CMD_GAME, buf, n);
	send_packet(s, j, CONTROL, CMD_PLAYER, info, player_info(info, local, start));
	for (x = s->remotes; x; x = x->next)
		if (x != j && x->id != KP_SNET_NO_ID) send_packet(s, j, CONTROL, CMD_PLAYER, info, player_info(info, x, start));
	send_u32(s, j, CMD_LADDER, s->ladder_type);
	send_packet(s, j, CONTROL, CMD_TEMPLATE, s->tmpl, s->tmpl_size);
	for (x = s->remotes; x; x = x->next)
		if (x != j && x->id != KP_SNET_NO_ID) send_packet(s, x, CONTROL, CMD_PLAYER, info, player_info(info, j, start));
	for (t = local->in[TURNS].head; t; t = t->next) resend(s, j, t->p);
	if (!(j->flags & PLAYER_JOINING)) queue_event(s, KP_SNET_EVENT_PLAYER, (int32_t)id, NULL, 0);
	return;
reject:
	if ((j = find_or_create(s, from))) send_packet(s, j, CONTROL, CMD_REJECT, NULL, 0);
}

/* Command 8 0x15023d20 (joiner): its id, turn counters and the game's data. */
static void on_game_info(KpSnet *s, const uint8_t *p, uint32_t size)
{
	Player *local = ensure_local(s);
	uint16_t start = (uint16_t)pay32(p, size, 8);
	uint32_t off;
	if (local) {
		assign_id(s, local, (uint8_t)pay32(p, size, 0));
		local->recv_seq[TURNS] = local->ack_owed[TURNS] = local->send_seq[TURNS] = start;
	}
	s->max_players = pay32(p, size, 4);
	s->mode = pay32(p, size, 12);
	s->start_tick = now_of(s) - pay32(p, size, 16) * 1000;
	off = pay_string(s->name, p, size, 20);
	off = pay_string(s->stats, p, size, off);
	pay_string(s->password, p, size, off);
}

/* Command 9 0x15023c50 (joiner): in the game; the template and event 1. */
static void on_template(KpSnet *s, const uint8_t *p, uint32_t size)
{
	Player *local = ensure_local(s);
	if (local) s->local_id = local->id;
	free(s->tmpl);
	s->tmpl = NULL;
	s->tmpl_size = 0;
	if (size && (s->tmpl = (uint8_t *)malloc(size))) {
		memcpy(s->tmpl, p, size);
		s->tmpl_size = size;
		queue_event(s, KP_SNET_EVENT_TEMPLATE, s->local_id, s->tmpl, size);
	}
}

static void on_control(KpSnet *s, const KpNetAddr *from, const uint8_t *p, uint32_t size, uint8_t sender, unsigned cmd)
{
	Player *r;
	uint32_t v;
	switch (cmd) {
	case CMD_ACCEPT: /* 0x150234b0 */
		if (pay32(p, size, 0) == 1 && (r = find_or_create(s, from))) send_u32(s, r, CMD_CONNECT_ACK, 1);
		break;
	case CMD_PING: /* 0x150235f0 */
		if ((r = find_or_create(s, from))) send_packet(s, r, CONTROL, CMD_PONG, p, size);
		break;
	case CMD_PONG: /* 0x15023620 */
		if ((r = find_or_create(s, from))) {
			v = now_of(s) - r->ping;
			r->rtt = v;
			if (v > r->rtt_max) r->rtt_max = v;
		}
		break;
	case CMD_PLAYER:
		on_player_info(s, from, p, size, sender);
		break;
	case CMD_JOIN:
		on_join_request(s, from, p, size);
		break;
	case CMD_GAME:
		on_game_info(s, p, size);
		break;
	case CMD_TEMPLATE:
		on_template(s, p, size);
		break;
	case CMD_LEAVE: /* 0x15023e00 */
		if ((r = find_or_create(s, from)) && r->id != KP_SNET_NO_ID) {
			r->flags |= PLAYER_DROPPING;
			r->drop_turn = (uint16_t)pay32(p, size, 0);
			r->drop_reason = pay32(p, size, 4);
		}
		break;
	case CMD_DROP: /* 0x150234e0 */
		if ((r = find_by_id(s, pay32(p, size, 0)))) {
			r->flags |= PLAYER_DROPPING;
			r->drop_turn = (uint16_t)pay32(p, size, 4);
			r->drop_reason = pay32(p, size, 8);
		}
		break;
	case CMD_HOST: /* 0x15023540: every record of that id becomes host */
		v = pay32(p, size, 0);
		if (s->local) s->local->host = 0;
		for (r = s->remotes; r; r = r->next) r->host = 0;
		if (v != KP_SNET_NO_ID) {
			if (s->local && s->local->id == (uint8_t)v) s->local->host = 1;
			for (r = s->remotes; r; r = r->next)
				if (r->id == (uint8_t)v) r->host = 1;
		}
		break;
	case CMD_MODE: /* 0x15023520: a public game's password is emptied */
		s->mode = pay32(p, size, 0);
		if (!(s->mode & KP_SNET_MODE_PASSWORD)) memset(s->password, 0, 4);
		break;
	case CMD_LADDER: /* 0x150235d0 */
		s->ladder_type = pay32(p, size, 0);
		break;
	default: /* 3 and 10 only mark `seen` */
		break;
	}
}

/* --- timers --- */

/* RunNetTimers 0x15020b80 */
static uint32_t run_timers(KpSnet *s)
{
	uint32_t now = now_of(s), result = KP_SNET_INFINITE;
	Player *r, *next;
	unsigned c;
	ensure_local(s);
	for (r = s->remotes; r; r = next) {
		next = r->next;
		if (r->id == KP_SNET_NO_ID) {
			if (now - r->heard >= FORGET) {
				remote_delete(s, r);
				continue;
			}
			if (!r->connecting) continue;
		}
		if (now - r->ping >= PING_IDLE && !r->connecting) {
			r->ping = now;
			send_packet(s, r, CONTROL, CMD_PING, NULL, 0);
		}
		for (c = 0; c <= TURNS; c++) {
			uint32_t prev = (uint16_t)(r->recv_seq[c] - 1);
			Node *n;
			for (n = r->in[c].head; n && (int32_t)(seq_of(n) - prev) < 2; n = n->next) prev = seq_of(n);
			if (!n) continue;
			if (!n->timer || (int32_t)(s->t_gap + n->timer - now) < 1) {
				n->timer = now;
				send_bare(s, r, (prev + 1) & 0xffff, c, FLAG_RESEND);
			} else {
				keep_min(&result, (int32_t)(s->t_gap + n->timer - now));
			}
		}
		for (c = 0; c <= MESSAGES; c++) {
			Node *n = r->out[c].head;
			int32_t left;
			if (!n) continue;
			left = (int32_t)(n->timer - now + (s->caps.maxmsg + 128 + n->size) * 1000 / s->caps.bytes_per_sec +
				(s->t_gap + 100) * 2);
			if (!n->timer || left < 1) {
				n->timer = now;
				resend(s, r, n->p);
			} else {
				keep_min(&result, left);
			}
		}
		for (c = 0; c <= TURNS; c++) {
			int32_t left;
			if (r->ack_owed[c] == r->ack_sent[c] || !r->ack_timer[c]) continue;
			left = (int32_t)(r->ack_timer[c] - now + (c == TURNS ? s->t_ack2 : s->t_ack));
			if (left < 1) send_bare(s, r, r->ack_owed[c], c, FLAG_ACK);
			else keep_min(&result, left);
		}
	}
	return result;
}

/* ProcessDrops 0x150218e0: players whose drop turn came are removed (event 3); host migration. */
static void process_drops(KpSnet *s)
{
	Player *r, *x, *local = s->local;
	for (r = s->remotes; r; r = r->next) {
		uint8_t reason[4];
		if (!(r->flags & PLAYER_DROPPING) || r->id == KP_SNET_NO_ID ||
			(uint16_t)(r->recv_seq[TURNS] - r->drop_turn) >= 0x8000)
			continue;
		kp_net_put32(reason, r->drop_reason);
		queue_event(s, KP_SNET_EVENT_REMOVED, r->id, reason, sizeof reason);
		r->flags = 0;
		r->old_id = r->id;
		r->id = KP_SNET_NO_ID;
		if (!local) continue;
		if (s->local_id != KP_SNET_NO_ID && !local->host) {
			uint32_t m = 0xffffffffu;
			for (x = s->remotes; x; x = x->next) {
				if (x->id == KP_SNET_NO_ID) continue;
				if (x->host) {
					m = 0;
					break;
				}
				if (x->id < m) m = x->id;
			}
			if (s->local_id < m) {
				local->host = 1;
				for (x = s->remotes; x; x = x->next)
					if (x->id != KP_SNET_NO_ID) send_u32(s, x, CMD_HOST, s->local_id);
			}
		}
		if (local->host) {
			if (player_count(s) < s->max_players) s->mode &= ~KP_SNET_MODE_FULL;
			advertise(s);
		}
	}
}

/* TrimTurnHistory 0x150205c0 (all records) */
static void trim_history(KpSnet *s)
{
	Player *local = ensure_local(s), *r;
	uint16_t t, min_ack;
	if (!local) return;
	t = local->recv_seq[TURNS];
	min_ack = t;
	for (r = s->remotes; r; r = r->next)
		if ((uint16_t)(min_ack - r->ack2) < 0x7fff) min_ack = r->ack2;
	for (r = s->remotes;; r = r->next) {
		Player *x = r ? r : local;
		while (x->in[TURNS].head && seq_of(x->in[TURNS].head) != t && (uint16_t)(t - seq_of(x->in[TURNS].head)) <= 0x7fff)
			list_append(&x->history, list_pop(&x->in[TURNS]));
		while (x->history.head && seq_of(x->history.head) != min_ack &&
			(uint16_t)(min_ack - seq_of(x->history.head)) <= 0x7fff)
			node_free(s, list_pop(&x->history));
		if (!r) break;
	}
}

/* --- exports --- */

KpSnet *kp_snet_new(KpSnpUdp *p, const KpSnetHooks *hooks, uint32_t program, uint32_t version)
{
	KpSnet *s;
	uint32_t l;
	if (!hooks || !hooks->now || !(s = (KpSnet *)calloc(1, sizeof *s))) return NULL;
	s->snp = p;
	s->hooks = *hooks;
	s->program = program;
	s->version = version;
	s->caps = KP_SNET_UDP_CAPS;
	s->caps.maxmsg -= MAXMSG_RESERVE;
	l = s->caps.latency;
	s->t_ack = max_u32(l / 2, 25);
	s->t_gap = max_u32(l, 50);
	s->t_ack2 = max_u32(5 * l, 250);
	s->t_heard = max_u32(4 * l, 1000);
	s->t_join = max_u32(12 * l, 5000);
	s->local_id = KP_SNET_NO_ID;
	return s;
}

void kp_snet_free(KpSnet *s)
{
	if (!s) return;
	if (s->local_id != KP_SNET_NO_ID) kp_snet_leave_game(s, 3);
	free_all_records(s);
	free_events(s);
	free(s->tmpl);
	free(s);
}

uint32_t kp_snet_error(const KpSnet *s) { return s->error; }

uint32_t kp_snet_pump(KpSnet *s, int signalled)
{
	if (!s->snp || s->in_join) return KP_SNET_INFINITE;
	if (signalled) receive_packets(s);
	return run_timers(s);
}

int kp_snet_take_signal(KpSnet *s)
{
	int v = s->signal;
	s->signal = 0;
	return v;
}

int kp_snet_create_game(KpSnet *s, const char *name, const char *password, const char *stats, uint32_t game_type,
	uint32_t ladder_type, uint32_t mode, const void *tmpl, uint32_t tmpl_size, uint32_t max_players,
	const char *player_name, const char *player_desc, int *player_id)
{
	Player *local;
	if (player_id) *player_id = -1;
	if (!name || !*name || !max_players || !player_id) return fail(s, KP_SNET_ERROR_PARAMETER);
	if (!s->snp) return fail(s, KP_SNET_ERROR_NO_PROVIDER);
	if (!player_name || !*player_name) return fail(s, KP_SNET_ERROR_PARAMETER); /* the provider names nobody */
	if (s->local_id != KP_SNET_NO_ID) kp_snet_leave_game(s, 2);
	free_all_records(s);
	copy_string(s->name, name);
	copy_string(s->stats, stats);
	copy_string(s->password, password);
	s->start_tick = now_of(s);
	s->game_type = game_type;
	s->ladder_type = ladder_type;
	free(s->tmpl);
	s->tmpl = NULL;
	s->tmpl_size = 0;
	if (tmpl && tmpl_size && (s->tmpl = (uint8_t *)malloc(tmpl_size))) {
		memcpy(s->tmpl, tmpl, tmpl_size);
		s->tmpl_size = tmpl_size;
	}
	s->max_players = max_players;
	s->mode = mode | (password && *password ? KP_SNET_MODE_PASSWORD : 0) | (max_players < 2 ? KP_SNET_MODE_FULL : 0);
	if (!(local = ensure_local(s))) return fail(s, KP_SNET_ERROR_NO_MEMORY);
	local->host = 1;
	assign_id(s, local, 0);
	copy_string(local->name, player_name);
	copy_string(local->desc, player_desc);
	s->local_id = 0;
	*player_id = 0;
	if (!advertise(s)) {
		kp_snet_leave_game(s, 1);
		return fail(s, KP_SNET_ERROR_PARAMETER);
	}
	s->mode |= KP_SNET_MODE_ADVERTISED;
	return 1;
}

/* PumpUntilControl 0x15023f40: until one of the commands was dispatched, or the timeout. */
static int pump_until(KpSnet *s, const unsigned *cmds, int count, uint32_t timeout)
{
	uint32_t start = now_of(s);
	int first = 1, k;
	memset(s->seen, 0, sizeof s->seen);
	for (;;) {
		if (!first && s->hooks.sleep) s->hooks.sleep(s->hooks.ud, 10);
		first = 0;
		receive_packets(s);
		run_timers(s);
		for (k = 0; k < count; k++)
			if (cmds[k] < CMD_COUNT && s->seen[cmds[k]]) return 1;
		if (timeout != KP_SNET_INFINITE && now_of(s) - start >= timeout) return 0;
	}
}

int kp_snet_join_game(KpSnet *s, uint32_t game_id, const char *name, const char *password, const char *player_name,
	const char *player_desc, int *player_id)
{
	static const unsigned accept[] = {CMD_ACCEPT}, answer[] = {CMD_TEMPLATE, CMD_REJECT};
	KpSnpGame g;
	Player *host, *local;
	uint8_t buf[3 * KP_SNET_STRING];
	uint32_t n, k;
	Node *c;
	if (!player_id) return fail(s, KP_SNET_ERROR_PARAMETER);
	*player_id = -1;
	if (!s->snp) return fail(s, KP_SNET_ERROR_NO_PROVIDER);
	if (!name) name = "";
	if (!password) password = "";
	if (!player_desc) player_desc = "";
	if (!player_name || !*player_name) return fail(s, KP_SNET_ERROR_PARAMETER);
	if (s->local_id != KP_SNET_NO_ID) kp_snet_leave_game(s, 2);
	free_all_records(s);
	if (game_id == 0 && !*name) return fail(s, KP_SNET_ERROR_PARAMETER);
	if (!kp_snp_udp_game_info(s->snp, game_id, name, &g)) return fail(s, KP_SNET_ERROR_NOT_FOUND);
	if (g.program != s->program || g.version != s->version) return fail(s, KP_SNET_ERROR_VERSION);
	s->joining = 1;
	s->in_join = 1;
	s->join_addr = g.addr;
	if (!(host = find_or_create(s, &g.addr))) {
		s->joining = s->in_join = 0;
		kp_snet_leave_game(s, 1);
		return fail(s, KP_SNET_ERROR_NO_MEMORY);
	}
	host->connecting = 1;
	host->recv_seq[CONTROL] = host->ack_owed[CONTROL] = host->send_seq[CONTROL] = 1;
	kp_net_put32(buf, 1);
	if ((c = send_packet(s, host, CONTROL, CMD_CONNECT, buf, 4))) {
		resend(s, host, c->p);
		resend(s, host, c->p);
	}
	process_ack(s, host, CONTROL, 1);
	if (!pump_until(s, accept, 1, s->t_join)) goto unreachable;
	n = 0;
	k = (uint32_t)strlen(player_name) < KP_SNET_STRING - 1 ? (uint32_t)strlen(player_name) : KP_SNET_STRING - 1;
	memcpy(buf + n, player_name, k);
	buf[n + k] = 0;
	n += k + 1;
	k = (uint32_t)strlen(player_desc) < KP_SNET_STRING - 1 ? (uint32_t)strlen(player_desc) : KP_SNET_STRING - 1;
	memcpy(buf + n, player_desc, k);
	buf[n + k] = 0;
	n += k + 1;
	k = (uint32_t)strlen(password) < KP_SNET_STRING - 1 ? (uint32_t)strlen(password) : KP_SNET_STRING - 1;
	memcpy(buf + n, password, k);
	buf[n + k] = 0;
	n += k + 1;
	send_packet(s, find_or_create(s, &g.addr), CONTROL, CMD_JOIN, buf, n);
	if (!pump_until(s, answer, 2, s->t_join)) {
		if ((host = find_remote(s, &g.addr))) host->connecting = 0;
		goto unreachable;
	}
	if (s->local_id == KP_SNET_NO_ID) {
		if ((host = find_remote(s, &g.addr))) host->connecting = 0;
		s->joining = s->in_join = 0;
		kp_snet_leave_game(s, 1);
		return fail(s, KP_SNET_ERROR_REJECTED);
	}
	s->game_type = 0; /* the UDP provider's record +0x24 */
	if ((local = ensure_local(s))) {
		local->id = s->local_id;
		copy_string(local->name, player_name);
		copy_string(local->desc, player_desc);
	}
	*player_id = s->local_id;
	s->in_join = 0;
	s->signal = 1; /* datagrams arrived meanwhile: the pump thread runs once the lock is free */
	dispatch_events(s);
	if ((host = find_remote(s, &g.addr))) host->connecting = 0;
	s->joining = 0;
	if (s->local_id == KP_SNET_NO_ID) return fail(s, KP_SNET_ERROR_GAME_ENDED);
	return 1;
unreachable:
	s->joining = s->in_join = 0;
	kp_snet_leave_game(s, 1);
	return fail(s, KP_SNET_ERROR_UNREACHABLE);
}

int kp_snet_leave_game(KpSnet *s, uint32_t reason)
{
	Player *local, *r;
	if (!s->snp) return fail(s, KP_SNET_ERROR_NO_PROVIDER);
	if (!(local = ensure_local(s))) return fail(s, KP_SNET_ERROR_NO_MEMORY);
	if (local->host) kp_snp_udp_stop_advertising(s->snp);
	for (r = s->remotes; r; r = r->next) {
		uint8_t b[8];
		Node *n;
		if (r->id == KP_SNET_NO_ID) continue;
		kp_net_put32(b, r->send_seq[TURNS]);
		kp_net_put32(b + 4, reason);
		if ((n = send_packet(s, r, CONTROL, CMD_LEAVE, b, sizeof b))) {
			resend(s, r, n->p);
			resend(s, r, n->p);
		}
	}
	free(s->tmpl);
	s->tmpl = NULL;
	s->tmpl_size = 0;
	s->name[0] = s->stats[0] = s->password[0] = 0;
	s->mode = 0;
	s->ladder_type = 0;
	s->local_id = KP_SNET_NO_ID;
	s->max_players = 0;
	free_all_records(s);
	return 1;
}

/* What SNetEnumGamesEx hands over of a listed game, copied before the list is unlocked: the advertisement's player
 * record (Storm's extra bytes) read for its counts. */
typedef struct {
	uint32_t id, players, max_players;
	char name[KP_SNP_STRING], stats[KP_SNP_STRING];
} ListedGame;

/* SNetEnumGamesEx lists no game with a password, full or closed (Storm's mask 0xB). */
static int listed(const KpSnpGame *e)
{
	return !(e->mode & (KP_SNET_MODE_PASSWORD | KP_SNET_MODE_FULL | KP_SNET_MODE_CLOSED));
}

int kp_snet_enum_games(KpSnet *s, int (*fn)(void *ud, const KpSnetGame *g), void *ud, uint32_t *hint)
{
	if (!fn) return fail(s, KP_SNET_ERROR_PARAMETER);
	if (hint) *hint = 0;
	if (!s->snp) return fail(s, KP_SNET_ERROR_NO_PROVIDER);
	return kp_snet_list_games(s->snp, &s->hooks, fn, ud, hint) ? 1 : fail(s, KP_SNET_ERROR_NO_MEMORY);
}

int kp_snet_list_games(KpSnpUdp *snp, const KpSnetHooks *hooks, int (*fn)(void *ud, const KpSnetGame *g), void *ud,
	uint32_t *hint)
{
	const KpSnpGame *head, *e;
	ListedGame *copy;
	int count = 0, k;
	for (e = head = kp_snp_udp_lock_games(snp); e; e = e->next) count += listed(e);
	copy = count ? (ListedGame *)malloc((size_t)count * sizeof *copy) : NULL;
	if (count && !copy) {
		kp_snp_udp_unlock_games(snp);
		return 0;
	}
	for (k = 0, e = head; e; e = e->next) {
		if (!listed(e)) continue;
		copy[k].id = e->id;
		copy[k].players = pay32(e->extra, e->extra_size, 4);
		copy[k].max_players = pay32(e->extra, e->extra_size, 8);
		memcpy(copy[k].name, e->name, sizeof copy[k].name);
		memcpy(copy[k].stats, e->stats, sizeof copy[k].stats);
		k++;
	}
	kp_snp_udp_unlock_games(snp);
	if (hint) *hint = KP_SNP_ENUM_HINT;
	if (hooks && hooks->unlock) hooks->unlock(hooks->ud);
	for (k = 0; k < count; k++) {
		KpSnetGame g;
		g.id = copy[k].id;
		g.name = copy[k].name;
		g.stats = copy[k].stats;
		g.game_type = 0;
		g.players = copy[k].players;
		g.max_players = copy[k].max_players;
		if (!fn(ud, &g)) break;
	}
	if (hooks && hooks->lock) hooks->lock(hooks->ud);
	free(copy);
	return 1;
}

int kp_snet_send_turn(KpSnet *s, const void *data, uint32_t size)
{
	Player *local = s->local, *r;
	if (!data || !size || size > PACKET_MAX - KP_SNET_HEADER) return fail(s, KP_SNET_ERROR_PARAMETER);
	if (!s->snp) return fail(s, KP_SNET_ERROR_NO_PROVIDER);
	if (s->local_id == KP_SNET_NO_ID || !local) return fail(s, KP_SNET_ERROR_NOT_IN_GAME);
	for (r = s->remotes; r; r = r->next)
		if (r->id != KP_SNET_NO_ID && r->send_seq[TURNS] == local->send_seq[TURNS]) send_packet(s, r, TURNS, 0, data, size);
	send_packet(s, local, TURNS, 0, data, size);
	s->signal = 1;
	return 1;
}

/* The record of a turn slot (kp_snet_receive_turns): the local seat's, or the remote's of that id. A slot outside
 * 0..254 has none: Storm compares an id's low byte, so slot 256 + k would be player k again and its turn taken
 * twice. */
static Player *slot_player(const KpSnet *s, Player *local, int64_t slot)
{
	if (slot < 0 || slot >= (int64_t)KP_SNET_NO_ID) return NULL;
	return slot == s->local_id ? local : find_by_id(s, (uint32_t)slot);
}

int kp_snet_receive_turns(KpSnet *s, int first, int count, const uint8_t **data, uint32_t *sizes, uint32_t *status)
{
	Player *local, *r, *p;
	uint32_t now;
	int64_t slot;
	uint16_t t;
	int ready = 1, k;
	if (count > 0) {
		if (data) memset((void *)data, 0, (size_t)count * sizeof *data);
		if (sizes) memset(sizes, 0, (size_t)count * sizeof *sizes);
		if (status) memset(status, 0, (size_t)count * sizeof *status);
	}
	if (count <= 0 || !data || !sizes || !status) return fail(s, KP_SNET_ERROR_PARAMETER);
	if (!s->snp) return fail(s, KP_SNET_ERROR_NO_PROVIDER);
	if (s->local_id == KP_SNET_NO_ID) return fail(s, KP_SNET_ERROR_NOT_IN_GAME);
	process_drops(s);
	if (!(local = ensure_local(s))) return fail(s, KP_SNET_ERROR_NOT_IN_GAME);
	t = local->recv_seq[TURNS];
	for (r = s->remotes; r; r = r->next)
		if ((r->flags & PLAYER_JOINING) && r->recv_seq[TURNS] == t) {
			r->flags &= ~PLAYER_JOINING;
			queue_event(s, KP_SNET_EVENT_PLAYER, r->id, NULL, 0);
		}
	dispatch_events(s);
	if (s->local_id == KP_SNET_NO_ID || !(local = s->local)) return fail(s, KP_SNET_ERROR_GAME_ENDED);
	trim_history(s);
	now = now_of(s);
	for (k = 0; k < count; k++) {
		uint32_t word = 0;
		slot = (int64_t)first + k;
		p = slot_player(s, local, slot);
		if (p && !(p->flags & PLAYER_JOINING)) {
			word = KP_SNET_PRESENT;
			if (p->in[TURNS].head && seq_of(p->in[TURNS].head) == t) word |= KP_SNET_HAS_TURN;
			else ready = 0;
			if (slot == s->local_id || now - p->heard < s->t_heard ||
				((p->flags & PLAYER_DROPPING) && (word & KP_SNET_HAS_TURN)))
				word |= KP_SNET_RESPONDING;
		}
		status[k] = word;
	}
	for (k = 0; k < count; k++) {
		slot = (int64_t)first + k;
		if (!(status[k] & KP_SNET_PRESENT) || slot == s->local_id || !(p = slot_player(s, local, slot))) continue;
		if (!(status[k] & KP_SNET_HAS_TURN) &&
			(!p->turn_request || (now - p->turn_request >= s->t_gap && now - p->turn_request < 0x80000000u))) {
			p->turn_request = now;
			for (r = s->remotes; r; r = r->next) {
				uint8_t q[REQUEST_SIZE];
				uint32_t size = r == p ? KP_SNET_HEADER : REQUEST_SIZE;
				if (r->id == KP_SNET_NO_ID || (p->id == KP_SNET_NO_ID && r != p)) continue;
				put_header(q, size, p->recv_seq[TURNS], r->ack_owed[TURNS], TURNS, 0, local->id, FLAG_RESEND);
				q[12] = p->id;
				kp_net_checksum(q, size);
				transmit(s, r, q);
			}
		}
		if (!p->out[CONTROL].head && now - p->heard >= s->t_heard / 2 && now - p->ping >= s->t_heard / 2) {
			p->ping = now;
			send_packet(s, p, CONTROL, CMD_PING, NULL, 0);
		}
	}
	if (!ready) return fail(s, KP_SNET_ERROR_NO_DATA);
	for (k = 0; k < count; k++) {
		Node *n;
		p = slot_player(s, local, (int64_t)first + k);
		if (!p || !(status[k] & KP_SNET_HAS_TURN)) continue;
		n = list_pop(&p->in[TURNS]);
		p->recv_seq[TURNS] = (uint16_t)(t + 1);
		list_append(&p->history, n);
		data[k] = n->p + KP_SNET_HEADER;
		sizes[k] = n->size - KP_SNET_HEADER;
	}
	local->recv_seq[TURNS] = (uint16_t)(t + 1);
	return 1;
}

int kp_snet_turns_in_transit(KpSnet *s, uint32_t *turns)
{
	Player *local;
	if (!turns) return fail(s, KP_SNET_ERROR_PARAMETER);
	*turns = 0;
	if (!s->snp) return fail(s, KP_SNET_ERROR_NO_PROVIDER);
	if (s->local_id == KP_SNET_NO_ID) return fail(s, KP_SNET_ERROR_NOT_IN_GAME);
	if (!(local = ensure_local(s))) return fail(s, KP_SNET_ERROR_NO_MEMORY);
	*turns = (uint16_t)(local->send_seq[TURNS] - local->recv_seq[TURNS]);
	return 1;
}

int kp_snet_send_message(KpSnet *s, int target, const void *data, uint32_t size)
{
	Player *r;
	if (!data || !size || size > PACKET_MAX - KP_SNET_HEADER) return fail(s, KP_SNET_ERROR_PARAMETER);
	if (!s->snp) return fail(s, KP_SNET_ERROR_NO_PROVIDER);
	if (s->local_id == KP_SNET_NO_ID) return fail(s, KP_SNET_ERROR_NOT_IN_GAME);
	if (target == -1 || target == -2) {
		for (r = s->remotes; r; r = r->next)
			if (r->id != KP_SNET_NO_ID) send_packet(s, r, MESSAGES, 0, data, size);
		if (target == -1 && (r = ensure_local(s))) send_packet(s, r, MESSAGES, 0, data, size);
	} else {
		if (!(r = find_by_id(s, (uint32_t)target))) return fail(s, KP_SNET_ERROR_BAD_PLAYER);
		send_packet(s, r, MESSAGES, 0, data, size);
	}
	s->signal = 1;
	return 1;
}

int kp_snet_receive_message(KpSnet *s, int *sender, const uint8_t **data, uint32_t *size)
{
	Player *best = NULL, *r;
	uint32_t best_age = 0, now;
	Node *n;
	int pass;
	if (sender) *sender = -1;
	if (data) *data = NULL;
	if (size) *size = 0;
	if (!data || !size) return fail(s, KP_SNET_ERROR_PARAMETER);
	if (!s->snp) return fail(s, KP_SNET_ERROR_NO_PROVIDER);
	if (s->local_id == KP_SNET_NO_ID) return fail(s, KP_SNET_ERROR_NOT_IN_GAME);
	process_drops(s);
	dispatch_events(s);
	if (s->local_id == KP_SNET_NO_ID) return fail(s, KP_SNET_ERROR_GAME_ENDED);
	now = now_of(s);
	for (pass = 0; pass < 2; pass++) {
		for (r = pass ? ensure_local(s) : s->remotes; r; r = pass ? NULL : r->next) {
			uint32_t oldest, age;
			if (r->id == KP_SNET_NO_ID || (r->flags & PLAYER_JOINING) || !(n = r->in[MESSAGES].head) ||
				seq_of(n) != r->recv_seq[MESSAGES])
				continue;
			oldest = n->tick;
			for (n = n->next; n; n = n->next)
				if (oldest - n->tick < 0x7fffffffu) oldest = n->tick - 1;
			age = now - oldest;
			if (age >= best_age) {
				best_age = age;
				best = r;
			}
		}
	}
	if (!best) return fail(s, KP_SNET_ERROR_NO_DATA);
	n = list_pop(&best->in[MESSAGES]);
	best->recv_seq[MESSAGES]++;
	node_free(s, best->delivered[MESSAGES]);
	best->delivered[MESSAGES] = n;
	if (sender) *sender = best->id;
	*data = n->p + KP_SNET_HEADER;
	*size = n->size - KP_SNET_HEADER;
	return 1;
}

int kp_snet_game_info(KpSnet *s, int type, void *buf, uint32_t size, uint32_t *written)
{
	const void *src;
	uint32_t need, n;
	uint8_t word[4];
	int string = 0;
	if (buf && size) memset(buf, 0, size);
	if (written) *written = 0;
	if (!s->snp) return fail(s, KP_SNET_ERROR_NO_PROVIDER);
	if (s->local_id == KP_SNET_NO_ID) return fail(s, KP_SNET_ERROR_NOT_IN_GAME);
	switch (type) {
	case KP_SNET_INFO_NAME:
	case KP_SNET_INFO_PASSWORD:
	case KP_SNET_INFO_STATS:
		src = type == KP_SNET_INFO_NAME ? s->name : type == KP_SNET_INFO_PASSWORD ? s->password : s->stats;
		need = (uint32_t)strlen((const char *)src) + 1;
		string = 1;
		break;
	case KP_SNET_INFO_MODE:
	case KP_SNET_INFO_MAX_PLAYERS:
		kp_net_put32(word, type == KP_SNET_INFO_MODE ? s->mode : s->max_players);
		src = word;
		need = 4;
		break;
	case KP_SNET_INFO_TEMPLATE:
		if (!s->tmpl) return fail(s, KP_SNET_ERROR_PARAMETER);
		src = s->tmpl;
		need = s->tmpl_size;
		break;
	default:
		return fail(s, KP_SNET_ERROR_PARAMETER);
	}
	if (!buf || !size) {
		if (written) *written = need;
		return 1;
	}
	n = need < size ? need : size;
	memcpy(buf, src, n);
	if (written) *written = n;
	if (string) ((char *)buf)[size - 1] = 0;
	return 1;
}

int kp_snet_set_game_mode(KpSnet *s, uint32_t mode, int make_private)
{
	Player *local, *r;
	if (!s->snp) return fail(s, KP_SNET_ERROR_PARAMETER);
	if (s->local_id == KP_SNET_NO_ID) return fail(s, KP_SNET_ERROR_NOT_IN_GAME);
	if (!(local = ensure_local(s)) || !local->host) return fail(s, KP_SNET_ERROR_NOT_OWNER);
	if (make_private) {
		mode &= ~KP_SNET_MODE_PASSWORD;
		memset(s->password, 0, 4);
	}
	if (mode == s->mode) return 1;
	s->mode = mode;
	for (r = s->remotes; r; r = r->next)
		if (r->id != KP_SNET_NO_ID) send_u32(s, r, CMD_MODE, mode);
	return advertise(s) ? 1 : fail(s, KP_SNET_ERROR_PARAMETER);
}

int kp_snet_num_players(KpSnet *s, int *first, int *last, int *active)
{
	Player *r;
	if (first) *first = -1;
	if (last) *last = -1;
	if (active) *active = 0;
	if (!s->snp) return fail(s, KP_SNET_ERROR_NO_PROVIDER);
	if (s->local_id == KP_SNET_NO_ID) return fail(s, KP_SNET_ERROR_NOT_IN_GAME);
	if (first) *first = s->local_id;
	if (last) *last = s->local_id;
	if (active) *active = 1;
	for (r = s->remotes; r; r = r->next) {
		if (r->id == KP_SNET_NO_ID) continue;
		if (first && r->id < *first) *first = r->id;
		if (last && r->id > *last) *last = r->id;
		if (active) (*active)++;
	}
	return 1;
}

int kp_snet_player_name(KpSnet *s, int id, char *buf, uint32_t size)
{
	Player *r;
	if (!buf || !size) return fail(s, KP_SNET_ERROR_PARAMETER);
	*buf = 0;
	if (!s->snp) return fail(s, KP_SNET_ERROR_NO_PROVIDER);
	if (s->local_id == KP_SNET_NO_ID) return fail(s, KP_SNET_ERROR_NOT_IN_GAME);
	if (!(r = find_by_id(s, (uint32_t)id)) || (r->flags & PLAYER_JOINING)) return fail(s, KP_SNET_ERROR_BAD_PLAYER);
	{
		size_t n = strlen(r->name);
		if (n > size - 1) n = size - 1;
		memcpy(buf, r->name, n);
		buf[n] = 0;
	}
	return 1;
}

int kp_snet_player_caps(KpSnet *s, int id, KpSnetCaps *caps)
{
	Player *r;
	if (!caps || caps->size != sizeof *caps) return fail(s, KP_SNET_ERROR_PARAMETER);
	memset(&caps->flags, 0, sizeof *caps - sizeof caps->size);
	if (!s->snp) return fail(s, KP_SNET_ERROR_NO_PROVIDER);
	if (s->local_id == KP_SNET_NO_ID) return fail(s, KP_SNET_ERROR_NOT_IN_GAME);
	if (!(r = find_by_id(s, (uint32_t)id))) return fail(s, KP_SNET_ERROR_BAD_PLAYER);
	*caps = s->caps;
	if (r->rtt) caps->latency = r->rtt;
	return 1;
}

int kp_snet_provider_caps(KpSnet *s, KpSnetCaps *caps)
{
	if (!caps || caps->size != sizeof *caps) return fail(s, KP_SNET_ERROR_PARAMETER);
	memset(&caps->flags, 0, sizeof *caps - sizeof caps->size);
	if (!s->snp) return fail(s, KP_SNET_ERROR_NO_PROVIDER);
	*caps = s->caps;
	return 1;
}

int kp_snet_drop_player(KpSnet *s, int id, uint32_t reason)
{
	Player *x, *r;
	uint8_t b[12];
	if (!s->snp) return fail(s, KP_SNET_ERROR_NO_PROVIDER);
	if (id == KP_SNET_NO_ID || id == s->local_id) return fail(s, KP_SNET_ERROR_PARAMETER);
	if (s->local_id == KP_SNET_NO_ID) return fail(s, KP_SNET_ERROR_NOT_IN_GAME);
	if (!(x = find_by_id(s, (uint32_t)id))) return fail(s, KP_SNET_ERROR_BAD_PLAYER);
	x->flags |= PLAYER_DROPPING;
	x->drop_turn = x->recv_seq[TURNS];
	x->drop_reason = reason;
	kp_net_put32(b, (uint32_t)id);
	kp_net_put32(b + 4, x->recv_seq[TURNS]);
	kp_net_put32(b + 8, reason);
	for (r = s->remotes; r; r = r->next)
		if (r != x && r->id != KP_SNET_NO_ID) send_packet(s, r, CONTROL, CMD_DROP, b, sizeof b);
	return 1;
}

int kp_snet_disconnect_all(KpSnet *s, uint32_t reason)
{
	Player *r;
	if (!s->snp) return fail(s, KP_SNET_ERROR_NO_PROVIDER);
	if (s->local_id == KP_SNET_NO_ID) return fail(s, KP_SNET_ERROR_NOT_IN_GAME);
	for (r = s->remotes; r; r = r->next) {
		r->flags |= PLAYER_DROPPING;
		r->drop_turn = r->recv_seq[TURNS];
		r->drop_reason = reason;
	}
	return 1;
}

int kp_snet_local_id(const KpSnet *s) { return s->local_id; }

uint32_t kp_snet_local_turn(const KpSnet *s) { return s->local ? s->local->recv_seq[TURNS] : 0; }

int kp_snet_is_host(const KpSnet *s, int id)
{
	const Player *r = find_by_id(s, (uint32_t)id);
	return r ? r->host != 0 : -1;
}

int kp_snet_remote_count(const KpSnet *s) { return s->remote_count; }
