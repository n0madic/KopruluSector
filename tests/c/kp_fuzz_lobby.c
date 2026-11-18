/* The lobby's fuzz targets (kp_fuzz_lobby.h; kp_fuzz.h tells their inputs).
 *
 * lobby: one seat's KpNetGame (kp_netgame.h) with the screens' pass (kp_net_lobby.h) over an SNet that answers from
 * the input: what the receive calls return and the events they dispatch, the players' names, the game's mode and turns
 * in transit, the calls that fail, the clock, the seat's saved game (a save file read by kp_netsave.h, the game a
 * create resumes and a join's lobby finds) and the screens' actions (create, join, chat, races, forces, kicks, bans,
 * swaps, the start, the ready room). What the seat sends is checked (within the provider's message size, to a player
 * or all) and its state stays within its bounds after each op.
 *
 * transfer: the map transfer (kp_transfer.h) alone: packets from the other Storm IDs, the offer's peers, its checks,
 * cancels and resets; its packets well-formed, its saves only of names the transfer may save, its state within bounds.
 *
 * The seeds are recorded from Koprulu's own sessions: the lobbies of kp_net_lobby on the in-process LAN with one seat's
 * answers, passes and actions written as they ran, and transfers between sessions in memory with one side's packets
 * written as they arrived; each seed is replayed at once and must send what its session sent. */
#include "kp_fuzz_lobby.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kp_net.h"
#include "kp_net_lobby.h"
#include "kp_netgame.h"
#include "kp_netsave.h"
#include "kp_transfer.h"

#define FNV_BASIS 0xcbf29ce484222325ull

#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 3, 4)))
#endif
static int failed(char *why, size_t size, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(why, size, fmt, ap);
	va_end(ap);
	return -1;
}

/* --- bytes in and out --- */

typedef struct {
	uint8_t *b;
	size_t n, cap;
	int oom;
} Buf;

static void put(Buf *x, const void *p, size_t n)
{
	if (x->n + n > x->cap) {
		size_t cap = x->cap ? x->cap : 256;
		uint8_t *b;
		while (cap < x->n + n) cap *= 2;
		if (!(b = (uint8_t *)realloc(x->b, cap))) {
			x->oom = 1;
			return;
		}
		x->b = b;
		x->cap = cap;
	}
	if (n) memcpy(x->b + x->n, p, n);
	x->n += n;
}

static void put8(Buf *x, unsigned v)
{
	uint8_t b = (uint8_t)v;
	put(x, &b, 1);
}

static void put16(Buf *x, unsigned v)
{
	uint8_t b[2];
	kp_net_put16(b, (uint16_t)v);
	put(x, b, 2);
}

static void put32(Buf *x, uint32_t v)
{
	uint8_t b[4];
	kp_net_put32(b, v);
	put(x, b, 4);
}

typedef struct {
	const uint8_t *d;
	size_t size, at;
} Reader;

static int get(Reader *r, size_t n, const uint8_t **p)
{
	if (r->size - r->at < n) return 0;
	*p = r->d + r->at;
	r->at += n;
	return 1;
}

static int get8(Reader *r, unsigned *v)
{
	const uint8_t *p;
	if (!get(r, 1, &p)) return 0;
	*v = p[0];
	return 1;
}

static int get16(Reader *r, unsigned *v)
{
	const uint8_t *p;
	if (!get(r, 2, &p)) return 0;
	*v = kp_net_get16(p);
	return 1;
}

static int get32(Reader *r, uint32_t *v)
{
	const uint8_t *p;
	if (!get(r, 4, &p)) return 0;
	*v = kp_net_get32(p);
	return 1;
}

static uint64_t fnv(uint64_t h, const void *p, size_t n) { return kp_net_world_fnv(h, p, n); }

/* ======================================================================================================== transfer */

/* An input: {u8 flags, u16 capacity}, then ops (op & 7 the kind, the rest its bits):
 *   TX_PACKET   a packet from Storm ID bits 3-5 {u16 n, n bytes}
 *   TX_PEER     Storm ID bits 3-5 added to the offer (AddPeerToOfferedFile); bit 6: removed instead
 *               (RemovePeerFromOfferedFile, AbortTransfersOfPeer)
 *   TX_PENDING  {u8 bits} set on the offer; bit 3: cleared instead
 *   TX_CHECK    the progress and AllPeersHaveFile read
 *   TX_CANCEL   the offer cancelled; bit 3: the map offered again
 *   TX_RESET    the session reset and begun again (the map offered when the flags offer it)
 * flags: bit 0 the seat offers the map, bits 1-3 its Storm ID, bit 4 the map is in its maps folder, bit 5 its saves
 * fail. The capacity is clamped to what a provider's message size gives (kp_netgame: max message - 4). */
enum { TX_PACKET, TX_PEER, TX_PENDING, TX_CHECK, TX_CANCEL, TX_RESET };
#define TX_OFFERS 0x01u
#define TX_HAS_MAP 0x10u
#define TX_SAVE_FAILS 0x20u
#define TX_REMOVE 0x40u
#define TX_CLEAR 0x08u
#define TX_HEADER 3
#define TX_CAPACITY_MIN (KP_NETGAME_MIN_MESSAGE - 4)
#define TX_CAPACITY_MAX (KP_NETGAME_TURN_MAX - 4)
#define TX_PACKET_MAX 0x1000 /* a packet's bytes the input gives, at most */

typedef struct Tx Tx;
/* A generator's view of a session's packets (NULL: the fuzzed session's, checked and dropped) */
typedef void (*TxForward)(void *ud, int from, int to, const uint8_t *packet, size_t size);

struct Tx {
	KpTransfer t;
	KpTransferOffer *offer;
	unsigned flags;
	int local;
	uint32_t capacity;
	const uint8_t *map;
	uint64_t digest; /* the packets sent, the files saved */
	int bad;
	char why[160];
	TxForward forward;
	void *forward_ud;
};

static void tx_bad(Tx *x, const char *what, unsigned a, unsigned b)
{
	if (x->bad++) return;
	snprintf(x->why, sizeof x->why, "%s (%u, %u)", what, a, b);
}

static void tx_send(void *ud, int storm, const uint8_t *packet, size_t size)
{
	Tx *x = (Tx *)ud;
	uint32_t s = (uint32_t)size;
	if (storm < 0 || storm >= KP_TRANSFER_PEERS) tx_bad(x, "a packet to no Storm ID", (unsigned)storm, s);
	if (size == 0 || size > x->capacity) tx_bad(x, "a packet past the capacity", s, x->capacity);
	else if (packet[0] > KP_TRANSFER_STOP) tx_bad(x, "a packet of no type", packet[0], s);
	x->digest = fnv(x->digest, &storm, sizeof storm);
	x->digest = fnv(x->digest, &s, sizeof s);
	x->digest = fnv(x->digest, packet, size);
	if (x->forward) x->forward(x->forward_ud, x->local, storm, packet, size);
}

static uint8_t *tx_read(void *ud, const char *path, size_t *size)
{
	Tx *x = (Tx *)ud;
	uint8_t *copy;
	if (strcmp(path, KP_NET_LOBBY_MAP_PATH) != 0 || !(copy = (uint8_t *)malloc(KP_NET_LOBBY_MAP_SIZE))) return NULL;
	memcpy(copy, x->map, KP_NET_LOBBY_MAP_SIZE);
	*size = KP_NET_LOBBY_MAP_SIZE;
	return copy;
}

static int tx_find(void *ud, const char *name, uint32_t size, uint32_t crc, char *out, size_t out_size)
{
	Tx *x = (Tx *)ud;
	if (!(x->flags & TX_HAS_MAP) || strcmp(name, KP_NET_LOBBY_MAP_NAME) != 0 || size != KP_NET_LOBBY_MAP_SIZE ||
		crc != kp_transfer_crc(x->map, KP_NET_LOBBY_MAP_SIZE))
		return 0;
	snprintf(out, out_size, "%s", KP_NET_LOBBY_MAP_PATH);
	return 1;
}

static int tx_save(void *ud, const char *name, const uint8_t *data, size_t size, char *out, size_t out_size)
{
	Tx *x = (Tx *)ud;
	uint32_t s = (uint32_t)size;
	if (!kp_transfer_name_valid(name, KP_TRANSFER_NAME) || !kp_transfer_extension_ok(name))
		tx_bad(x, "a download saved under a refused name", (unsigned)strlen(name), s);
	if (size > KP_TRANSFER_MAX_SIZE) tx_bad(x, "a download past the size bound", s, KP_TRANSFER_MAX_SIZE);
	x->digest = fnv(x->digest, name, strlen(name));
	x->digest = fnv(x->digest, &s, sizeof s);
	x->digest = fnv(x->digest, data, size);
	if (x->flags & TX_SAVE_FAILS) return 0;
	snprintf(out, out_size, KP_NET_LOBBY_DOWNLOADS "%s", name);
	return 1;
}

static void tx_begin(Tx *x)
{
	KpTransferIo io;
	io.send = tx_send;
	io.read = tx_read;
	io.find = tx_find;
	io.save = tx_save;
	io.ud = x;
	kp_transfer_begin(&x->t, &io, x->local, x->capacity);
	x->offer = x->flags & TX_OFFERS ? kp_transfer_offer(&x->t, KP_NET_LOBBY_MAP_PATH) : NULL;
}

static void tx_init(Tx *x, unsigned flags, unsigned capacity, const uint8_t *map)
{
	memset(x, 0, sizeof *x);
	x->flags = flags;
	x->local = (int)(flags >> 1 & 7);
	x->capacity = capacity < TX_CAPACITY_MIN ? TX_CAPACITY_MIN : capacity > TX_CAPACITY_MAX ? TX_CAPACITY_MAX : capacity;
	x->map = map;
	x->digest = FNV_BASIS;
	tx_begin(x);
}

/* The state's bounds */
static void tx_check(Tx *x)
{
	int k;
	if (x->t.active_count < 0 || x->t.active_count > KP_TRANSFER_MAX_ACTIVE)
		tx_bad(x, "active transfers past the bound", (unsigned)x->t.active_count, KP_TRANSFER_MAX_ACTIVE);
	if (x->t.offer_count < 0 || x->t.offer_count > KP_TRANSFER_OFFERS)
		tx_bad(x, "offers past the bound", (unsigned)x->t.offer_count, KP_TRANSFER_OFFERS);
	for (k = 0; k < x->t.active_count && k < KP_TRANSFER_MAX_ACTIVE; k++) {
		const KpTransferActive *a = x->t.active[k];
		if (a && (a->received > a->size || a->size > KP_TRANSFER_MAX_SIZE))
			tx_bad(x, "a transfer past its file", a->received, a->size);
	}
}

/* One op: 0 at the input's end */
static int tx_op(Tx *x, Reader *r)
{
	unsigned op, n, v;
	const uint8_t *p;
	if (!get8(r, &op)) return 0;
	switch (op & 7) {
	case TX_PACKET:
		if (!get16(r, &n) || n > TX_PACKET_MAX || !get(r, n, &p)) return 0;
		kp_transfer_dispatch(&x->t, p, n, (int)(op >> 3 & 7));
		break;
	case TX_PEER:
		if (!x->offer) break;
		if (op & TX_REMOVE) {
			kp_transfer_remove_peer(x->offer, (int)(op >> 3 & 7));
			kp_transfer_abort_peer(&x->t, (int)(op >> 3 & 7));
		} else {
			kp_transfer_add_peer(&x->t, x->offer, (int)(op >> 3 & 7));
		}
		break;
	case TX_PENDING:
		if (!get8(r, &v)) return 0;
		if (!x->offer) break;
		if (op & TX_CLEAR) kp_transfer_clear_pending(x->offer, v);
		else kp_transfer_set_pending(x->offer, v);
		break;
	case TX_CHECK: {
		uint8_t pct = 0;
		if (kp_transfer_progress(&x->t, &pct) && pct > 100) tx_bad(x, "a progress past 100%", pct, 100);
		if (x->offer) (void)kp_transfer_all_have(x->offer);
		break;
	}
	case TX_CANCEL:
		if (x->offer) kp_transfer_cancel(&x->t, x->offer);
		x->offer = (op & TX_CLEAR) && (x->flags & TX_OFFERS) ? kp_transfer_offer(&x->t, KP_NET_LOBBY_MAP_PATH) : NULL;
		break;
	case TX_RESET:
		kp_transfer_reset(&x->t);
		tx_begin(x);
		break;
	default: break;
	}
	tx_check(x);
	return !x->bad;
}

/* An input run: 0, or -1 (why); its digest into *digest when not NULL */
static int tx_run(const uint8_t *d, size_t size, char *why, size_t why_size, uint64_t *digest)
{
	Tx *x;
	Reader r;
	unsigned flags, capacity;
	uint8_t map[KP_NET_LOBBY_MAP_SIZE];
	int rc = 0;
	r.d = d;
	r.size = size;
	r.at = 0;
	if (!get8(&r, &flags) || !get16(&r, &capacity)) return 0;
	if (!(x = (Tx *)calloc(1, sizeof *x))) return 0;
	kp_net_lobby_map_bytes(map, sizeof map, KP_NET_LOBBY_MAP_SEED);
	tx_init(x, flags, capacity, map);
	while (tx_op(x, &r)) {
	}
	if (x->bad) rc = failed(why, why_size, "transfer: %s", x->why);
	if (digest) *digest = x->digest;
	kp_transfer_reset(&x->t);
	free(x);
	return rc;
}

int kp_fuzz_transfer_run(const uint8_t *d, size_t size, char *why, size_t why_size)
{
	return tx_run(d, size, why, why_size, NULL);
}

/* ========================================================================================================== lobby */

/* An input: {u8 flags, u16 the provider's maximum message, u8 its turns in transit}, then ops (op & 15 the kind, the
 * rest its bits):
 *   L_CLOCK    the clock moved {u16 ms}
 *   L_PASS     the screens' pass of a frame (kp_net_lobby_frame)
 *   L_MESSAGE  a message for SNetReceiveMessage {u8 sender, u16 n, n bytes}
 *   L_TURNS    a result of SNetReceiveTurns {u8 slots}, for each slot's bit {u32 status, u16 n, n bytes}; bit 4: the
 *              call fails, the slots' status words alone
 *   L_EVENT    an event {u8 id, u8 player, u8 n, n bytes} that bits 4-5 dispatch: 0 inside the next SNetReceiveTurns,
 *              1 the next SNetReceiveMessage, 2 the next SNetJoinGame, 3 at once
 *   L_NAME     SNetGetPlayerName's answer for an id from now on {u8 id, u8 n, n bytes}; n 0: the call fails
 *   L_MODE     the next SNetGetGameInfo's mode {u32 mode}
 *   L_TRANSIT  the next SNetGetTurnsInTransit's count {u8 turns}; 0xff: the call fails
 *   L_FAIL     the next call of each kind of the mask fails {u8 mask: F_*}
 *   L_ACTION   the screens' action of bits 4-7 (A_*) and its arguments
 *   L_SAVE     the seat's saved game, a save file {u16 n, n bytes} probed and read (kp_netsave.h: the original's, or
 *              Koprulu's container with NETG; bit 4: in Brood War); read, it is the game the next create resumes and
 *              the save a join's lobby finds, else the seat has none
 * flags: bit 0 the seat hosts, bits 1-3 the Storm ID SNet gives it, bit 4 the Use Map Settings template, bit 5 the map
 * is in its maps folder. Answers wait in queues, each in the order given; an answer past a queue's room is dropped. A
 * mode's or a count's queue run dry answers its last again. */
enum { L_CLOCK, L_PASS, L_MESSAGE, L_TURNS, L_EVENT, L_NAME, L_MODE, L_TRANSIT, L_FAIL, L_ACTION, L_SAVE };
enum { W_TURNS, W_MESSAGE, W_JOIN, W_NOW };
enum { F_CREATE = 1, F_JOIN = 2, F_SEND_TURN = 4, F_SEND_MESSAGE = 8, F_GAME_INFO = 0x10, F_SET_MODE = 0x20,
	F_DROP = 0x40, F_LEAVE = 0x80 };
/* The screens' actions: create {}, join {} (the listed test game, else the test header), list {u8 count, each {u32 id,
 * u8 players, u8 max, u8 n, name, u8 n, stat string}}, chat {u8 n, text}, race {u8 slot, u8 race}, team {u8},
 * force {u8}, ban {u8 slot}, slot {u8 slot, u8 action}, swap {u8 a, u8 b}, public, start (when the checks allow it),
 * ready (the ready room's Start), briefing abort, leave (the room closed), cancel */
enum { A_CREATE, A_JOIN, A_LIST, A_CHAT, A_RACE, A_TEAM, A_FORCE, A_BAN, A_SLOT, A_SWAP, A_PUBLIC, A_START, A_READY,
	A_BRIEFING_ABORT, A_LEAVE, A_CANCEL };
#define L_HOST 0x01u
#define L_UMS 0x10u
#define L_HAS_MAP 0x20u
#define L_TURNS_FAIL 0x10u
#define L_SAVE_EXPANSION 0x10u
#define L_SAVE_FILE "fuzz.mlt" /* the save file's name (its display name "fuzz") */
#define L_TRANSIT_FAILS 0xffu
#define L_HEADER 4
#define EVENTS 32
#define TURN_RESULTS 16
#define MESSAGES 32
#define GAMES 8
#define EVENT_DATA 255
#define STATES 16
#define MAX_PLAYERS 8

typedef struct {
	uint64_t seq;
	int where;
	uint32_t id;
	int32_t player;
	uint32_t size;
	uint8_t data[EVENT_DATA];
} QEvent;

typedef struct {
	uint64_t seq;
	int ok;
	uint32_t status[MAX_PLAYERS], size[MAX_PLAYERS];
	uint8_t data[MAX_PLAYERS][KP_NETGAME_TURN_MAX];
} QTurns;

typedef struct {
	uint64_t seq;
	int sender;
	uint32_t size;
	uint8_t data[KP_NETGAME_TURN_MAX];
} QMessage;

typedef struct {
	uint32_t id, players, max_players;
	char name[KP_NETGAME_GAME_NAME + 8];
	char stats[KP_NETGAME_STATS];
} QGame;

typedef struct {
	KpNetLobbySeat seat;
	KpTemplates templates;
	uint8_t map[KP_NET_LOBBY_MAP_SIZE];
	unsigned flags;
	uint32_t maxmsg, caps_transit;
	uint32_t clock;
	uint64_t seq;
	QEvent events[EVENTS];
	int event_count;
	QTurns turns[TURN_RESULTS];
	int turn_count;
	QMessage messages[MESSAGES];
	int message_count;
	uint8_t turn_out[MAX_PLAYERS][KP_NETGAME_TURN_MAX];
	uint8_t message_out[KP_NETGAME_TURN_MAX];
	char names[MAX_PLAYERS][KP_NETGAME_NAME];
	uint32_t mode, transit;          /* the last answers */
	uint32_t modes[STATES];
	int mode_count;
	uint8_t transits[STATES];
	int transit_count;
	unsigned fail;
	uint32_t error;
	QGame games[GAMES];
	int game_count;
	uint64_t digest; /* what the seat sent */
	KpNetGameSave save; /* the seat's saved game (L_SAVE) */
	int bad;
	char why[200];
} Lobby;

static void l_bad(Lobby *x, const char *what, long a, long b)
{
	if (x->bad++) return;
	snprintf(x->why, sizeof x->why, "%s (%ld, %ld)", what, a, b);
}

static void l_digest(Lobby *x, uint32_t tag, const void *p, size_t n)
{
	x->digest = fnv(x->digest, &tag, sizeof tag);
	x->digest = fnv(x->digest, p, n);
}

/* A call of a kind in the fail mask fails once */
static int fails(Lobby *x, unsigned kind)
{
	if (!(x->fail & kind)) return 0;
	x->fail &= ~kind;
	x->error = KP_SNET_ERROR_NOT_IN_GAME;
	return 1;
}

/* The queued events of a call (where) older than `before`, dispatched in their order */
static void dispatch(Lobby *x, int where, uint64_t before)
{
	int k = 0;
	while (k < x->event_count) {
		QEvent e;
		KpSnetEvent ev;
		if (x->events[k].where != where || x->events[k].seq >= before) {
			k++;
			continue;
		}
		e = x->events[k];
		memmove(&x->events[k], &x->events[k + 1], sizeof x->events[0] * (size_t)(x->event_count - k - 1));
		x->event_count--;
		ev.id = e.id;
		ev.player = e.player;
		ev.data = e.size ? e.data : NULL;
		ev.size = e.size;
		kp_netgame_event(&x->seat.game, &ev);
	}
}

static int f_create(void *ud, const char *name, const char *password, const char *stats, uint32_t game_type,
	uint32_t ladder_type, uint32_t mode, const void *tmpl, uint32_t tmpl_size, uint32_t max_players,
	const char *player_name, const char *player_desc, int *player_id)
{
	Lobby *x = (Lobby *)ud;
	KpNetGameHeader h;
	(void)password;
	(void)game_type;
	(void)ladder_type;
	(void)tmpl;
	(void)player_desc;
	memset(&h, 0, sizeof h);
	if (!kp_netgame_stat_parse(stats, &h)) l_bad(x, "an advertised stat string that does not parse", 0, 0);
	if (tmpl_size != KP_TEMPLATE_SIZE || max_players > MAX_PLAYERS) l_bad(x, "a game's template or players", tmpl_size, max_players);
	l_digest(x, 1, name, strlen(name));
	l_digest(x, 1, stats, strlen(stats));
	l_digest(x, 1, player_name, strlen(player_name));
	l_digest(x, 1, &mode, sizeof mode);
	if (fails(x, F_CREATE)) return 0;
	*player_id = (int)(x->flags >> 1 & 7);
	return 1;
}

static int f_join(void *ud, uint32_t game_id, const char *name, const char *password, const char *player_name,
	const char *player_desc, int *player_id)
{
	Lobby *x = (Lobby *)ud;
	(void)name;
	(void)password;
	(void)player_desc;
	l_digest(x, 2, &game_id, sizeof game_id);
	l_digest(x, 2, player_name, strlen(player_name));
	if (fails(x, F_JOIN)) return 0;
	*player_id = (int)(x->flags >> 1 & 7);
	dispatch(x, W_JOIN, UINT64_MAX);
	return 1;
}

static int f_leave(void *ud, uint32_t reason)
{
	Lobby *x = (Lobby *)ud;
	l_digest(x, 3, &reason, sizeof reason);
	return !fails(x, F_LEAVE);
}

static int f_enum(void *ud, int (*fn)(void *fn_ud, const KpSnetGame *g), void *fn_ud, uint32_t *hint)
{
	Lobby *x = (Lobby *)ud;
	int k;
	for (k = 0; k < x->game_count; k++) {
		KpSnetGame g;
		g.id = x->games[k].id;
		g.name = x->games[k].name;
		g.stats = x->games[k].stats;
		g.game_type = 0;
		g.players = x->games[k].players;
		g.max_players = x->games[k].max_players;
		if (!fn(fn_ud, &g)) break;
	}
	if (hint) *hint = 0;
	return 1;
}

static int f_send_turn(void *ud, const void *data, uint32_t size)
{
	Lobby *x = (Lobby *)ud;
	if (size > x->seat.game.max_message || (size && !data))
		l_bad(x, "a turn past the maximum message", size, (long)x->seat.game.max_message);
	l_digest(x, 4, data, size > x->seat.game.max_message ? 0 : size);
	return !fails(x, F_SEND_TURN);
}

static int f_receive_turns(void *ud, int first, int count, const uint8_t **data, uint32_t *sizes, uint32_t *status)
{
	Lobby *x = (Lobby *)ud;
	QTurns *t;
	int k;
	if (first < 0 || count < 0 || first + count > MAX_PLAYERS) {
		l_bad(x, "SNetReceiveTurns past the players", first, count);
		return 0;
	}
	dispatch(x, W_TURNS, x->turn_count ? x->turns[0].seq : UINT64_MAX);
	for (k = 0; k < count; k++) {
		data[k] = NULL;
		sizes[k] = status[k] = 0;
	}
	x->error = KP_SNET_ERROR_NO_DATA;
	if (!x->turn_count) return 0;
	t = &x->turns[0];
	for (k = 0; k < count; k++) {
		status[k] = t->status[first + k];
		if (!t->ok) continue;
		sizes[k] = t->size[first + k];
		memcpy(x->turn_out[k], t->data[first + k], sizes[k]);
		data[k] = sizes[k] ? x->turn_out[k] : NULL;
	}
	k = t->ok;
	memmove(&x->turns[0], &x->turns[1], sizeof x->turns[0] * (size_t)(x->turn_count - 1));
	x->turn_count--;
	return k;
}

/* The head of a queue of answers taken, 0 when it ran dry */
static int pop(void *q, int *count, size_t size, void *out)
{
	if (!*count) return 0;
	memcpy(out, q, size);
	memmove(q, (uint8_t *)q + size, size * (size_t)(*count - 1));
	(*count)--;
	return 1;
}

static int f_in_transit(void *ud, uint32_t *turns)
{
	Lobby *x = (Lobby *)ud;
	uint8_t t;
	if (pop(x->transits, &x->transit_count, 1, &t)) {
		if (t == L_TRANSIT_FAILS) {
			x->error = KP_SNET_ERROR_NOT_IN_GAME;
			return 0;
		}
		x->transit = t;
	}
	*turns = x->transit;
	return 1;
}

static int f_send_message(void *ud, int target, const void *data, uint32_t size)
{
	Lobby *x = (Lobby *)ud;
	if (target < -1 || target >= MAX_PLAYERS) l_bad(x, "a message to no player", target, size);
	if (size == 0 || size > x->seat.game.max_message || !data)
		l_bad(x, "a message past the maximum message", size, (long)x->seat.game.max_message);
	l_digest(x, 5, &target, sizeof target);
	l_digest(x, 5, data, size > x->seat.game.max_message ? 0 : size);
	return !fails(x, F_SEND_MESSAGE);
}

static int f_receive_message(void *ud, int *sender, const uint8_t **data, uint32_t *size)
{
	Lobby *x = (Lobby *)ud;
	QMessage *m;
	dispatch(x, W_MESSAGE, x->message_count ? x->messages[0].seq : UINT64_MAX);
	if (!x->message_count) {
		x->error = KP_SNET_ERROR_NO_DATA;
		return 0;
	}
	m = &x->messages[0];
	memcpy(x->message_out, m->data, m->size);
	*sender = m->sender;
	*data = x->message_out;
	*size = m->size;
	memmove(&x->messages[0], &x->messages[1], sizeof x->messages[0] * (size_t)(x->message_count - 1));
	x->message_count--;
	return 1;
}

static int f_game_info(void *ud, int type, void *buf, uint32_t size, uint32_t *written)
{
	Lobby *x = (Lobby *)ud;
	if (fails(x, F_GAME_INFO)) return 0;
	if (type != KP_SNET_INFO_MODE || size < 4) {
		x->error = KP_SNET_ERROR_PARAMETER;
		return 0;
	}
	(void)pop(x->modes, &x->mode_count, sizeof x->modes[0], &x->mode);
	kp_net_put32((uint8_t *)buf, x->mode);
	if (written) *written = 4;
	return 1;
}

static int f_set_mode(void *ud, uint32_t mode, int make_private)
{
	Lobby *x = (Lobby *)ud;
	l_digest(x, 6, &mode, sizeof mode);
	l_digest(x, 6, &make_private, sizeof make_private);
	if (fails(x, F_SET_MODE)) return 0;
	x->mode = mode;
	return 1;
}

static int f_player_name(void *ud, int id, char *buf, uint32_t size)
{
	Lobby *x = (Lobby *)ud;
	if (id < 0 || id >= MAX_PLAYERS || !x->names[id][0] || !size) {
		x->error = KP_SNET_ERROR_BAD_PLAYER;
		return 0;
	}
	snprintf(buf, size, "%s", x->names[id]);
	return 1;
}

static int f_caps(void *ud, KpSnetCaps *caps)
{
	Lobby *x = (Lobby *)ud;
	*caps = KP_SNET_UDP_CAPS;
	caps->maxmsg = x->maxmsg;
	caps->turns_in_transit = x->caps_transit;
	return 1;
}

static int f_drop(void *ud, int id, uint32_t reason)
{
	Lobby *x = (Lobby *)ud;
	if (id < 0 || id >= MAX_PLAYERS) l_bad(x, "a drop of no player", id, (long)reason);
	l_digest(x, 7, &id, sizeof id);
	l_digest(x, 7, &reason, sizeof reason);
	return !fails(x, F_DROP);
}

static int f_disconnect(void *ud, uint32_t reason)
{
	l_digest((Lobby *)ud, 8, &reason, sizeof reason);
	return 1;
}

static uint32_t f_error(void *ud) { return ((Lobby *)ud)->error; }

static KpNetCalls fake_calls(Lobby *x)
{
	KpNetCalls c;
	memset(&c, 0, sizeof c);
	c.create_game = f_create;
	c.join_game = f_join;
	c.leave_game = f_leave;
	c.enum_games = f_enum;
	c.send_turn = f_send_turn;
	c.receive_turns = f_receive_turns;
	c.turns_in_transit = f_in_transit;
	c.send_message = f_send_message;
	c.receive_message = f_receive_message;
	c.game_info = f_game_info;
	c.set_game_mode = f_set_mode;
	c.player_name = f_player_name;
	c.provider_caps = f_caps;
	c.drop_player = f_drop;
	c.disconnect_all = f_disconnect;
	c.error = f_error;
	c.ud = x;
	return c;
}

/* --- the screens' actions, the replay's and the recording's --- */

static int template_of(unsigned flags) { return flags & L_UMS ? KP_NET_LOBBY_UMS : KP_NET_LOBBY_MELEE; }

/* An action other than the list's (its games given first): 0 when its arguments run out */
static int act(KpNetLobbySeat *s, const KpTemplates *t, unsigned flags, int kind, Reader *r)
{
	KpNetGame *ng = &s->game;
	int room = s->phase == KP_NET_LOBBY_ROOM;
	unsigned a = 0, b = 0;
	switch (kind) {
	case A_CREATE:
		if ((flags & L_HOST) && s->phase == KP_NET_LOBBY_MENU) kp_net_lobby_create(s, t, template_of(flags));
		break;
	case A_JOIN:
		if (!(flags & L_HOST) && s->phase == KP_NET_LOBBY_MENU) {
			const KpNetGameHeader *g = kp_net_lobby_listed(ng);
			KpNetGameHeader h;
			if (!g) {
				kp_net_lobby_header(&h, t->list[template_of(flags)].record);
				snprintf(h.creator, sizeof h.creator, "%s", KP_NET_LOBBY_NAMES[0]);
				g = &h;
			}
			kp_net_lobby_enter(s, g);
		}
		break;
	case A_CHAT: {
		const uint8_t *p;
		char text[256];
		if (!get8(r, &a) || !get(r, a, &p)) return 0;
		memcpy(text, p, a);
		text[a] = 0;
		if (room) kp_netgame_chat(ng, text);
		break;
	}
	case A_RACE:
		if (!get8(r, &a) || !get8(r, &b)) return 0;
		if (room) kp_netgame_request_race(ng, (int)(a % KP_NETGAME_SLOTS), (int)b);
		break;
	case A_TEAM:
		if (!get8(r, &a)) return 0;
		if (room) kp_netgame_request_team(ng, (int)(a % (KP_NETGAME_FORCES + 1)));
		break;
	case A_FORCE:
		if (!get8(r, &a)) return 0;
		if (room) kp_netgame_request_force(ng, (int)(a % (KP_NETGAME_FORCES + 1)));
		break;
	case A_BAN:
		if (!get8(r, &a)) return 0;
		if (room) kp_netgame_ban(ng, (int)a);
		break;
	case A_SLOT:
		if (!get8(r, &a) || !get8(r, &b)) return 0;
		if (room) kp_netgame_slot_action(ng, (int)a, (int)b);
		break;
	case A_SWAP:
		if (!get8(r, &a) || !get8(r, &b)) return 0;
		if (room) kp_netgame_request_swap(ng, (int)a, (int)b);
		break;
	case A_PUBLIC:
		if (room) kp_netgame_request_public(ng);
		break;
	case A_START:
		if (room && ng->host && kp_netgame_start_check(ng) == 0) kp_netgame_start(ng);
		break;
	case A_READY:
		if (s->phase == KP_NET_LOBBY_READY_ROOM) s->press_start = 1;
		break;
	case A_BRIEFING_ABORT:
		if (s->phase == KP_NET_LOBBY_READY_ROOM) {
			kp_netgame_briefing_start(ng, 1);
			s->phase = KP_NET_LOBBY_LEFT;
		}
		break;
	case A_LEAVE:
		if (room) {
			ng->in_chat_room = 0;
			kp_netgame_abort(ng);
			kp_netgame_release(ng);
			s->phase = KP_NET_LOBBY_LEFT;
		}
		break;
	case A_CANCEL:
		if (room) kp_netgame_cancel(ng);
		break;
	default: break;
	}
	return 1;
}

/* The list's games {u8 count, each {u32 id, u8 players, u8 max, u8 n, name, u8 n, stats}} into the answers */
static int read_games(Lobby *x, Reader *r)
{
	unsigned count, k;
	if (!get8(r, &count)) return 0;
	x->game_count = 0;
	for (k = 0; k < count; k++) {
		QGame g;
		unsigned players, max, n;
		const uint8_t *p;
		memset(&g, 0, sizeof g);
		if (!get32(r, &g.id) || !get8(r, &players) || !get8(r, &max) || !get8(r, &n) || !get(r, n, &p)) return 0;
		memcpy(g.name, p, n < sizeof g.name ? n : sizeof g.name - 1);
		if (!get8(r, &n) || !get(r, n, &p)) return 0;
		memcpy(g.stats, p, n < sizeof g.stats ? n : sizeof g.stats - 1);
		g.players = players;
		g.max_players = max;
		if (x->game_count < GAMES) x->games[x->game_count++] = g;
	}
	return 1;
}

/* The seat's state within its bounds */
static void l_check(Lobby *x)
{
	const KpNetGame *ng = &x->seat.game;
	int k;
	if (ng->lobby_state > KP_LOBBY_START) l_bad(x, "a lobby state past the start", ng->lobby_state, 0);
	if (ng->local_slot < -1 || ng->local_slot >= KP_NETGAME_SLOTS) l_bad(x, "a local slot past the slots", ng->local_slot, 0);
	if (ng->local_storm < -1 || ng->local_storm >= KP_NETGAME_PLAYERS) l_bad(x, "a local Storm ID past 7", ng->local_storm, 0);
	if (ng->ban_count < 0 || ng->ban_count > KP_NETGAME_BANS) l_bad(x, "bans past the bound", ng->ban_count, 0);
	if (ng->listed_count < 0 || ng->listed_count > KP_NETGAME_GAMES) l_bad(x, "games past the bound", ng->listed_count, 0);
	if (ng->out_size > ng->max_message) l_bad(x, "a turn past the maximum message", (long)ng->out_size, (long)ng->max_message);
	if (ng->transfer.active_count > KP_TRANSFER_MAX_ACTIVE || ng->transfer.offer_count > KP_TRANSFER_OFFERS)
		l_bad(x, "transfers past the bound", ng->transfer.active_count, ng->transfer.offer_count);
	for (k = 0; k < KP_NETGAME_SLOTS; k++)
		if (!memchr(ng->slots[k].name, 0, KP_NETGAME_NAME)) l_bad(x, "a slot's name without its NUL", k, 0);
	for (k = 0; k < KP_NETGAME_PLAYERS; k++)
		if (!memchr(ng->players[k].name, 0, KP_NETGAME_NAME)) l_bad(x, "a player's name without its NUL", k, 0);
	for (k = 0; k < KP_NETGAME_FORCES; k++)
		if (!memchr(ng->force_names[k], 0, KP_NETGAME_FORCE_NAME)) l_bad(x, "a force's name without its NUL", k, 0);
	if (x->seat.bad_saves) l_bad(x, "a download saved under a refused name", x->seat.bad_saves, 0);
}

/* One op: 0 at the input's end */
static int l_op(Lobby *x, Reader *r)
{
	unsigned op, a, b, n, k;
	const uint8_t *p;
	if (!get8(r, &op)) return 0;
	x->seq++;
	switch (op & 15) {
	case L_CLOCK:
		if (!get16(r, &a)) return 0;
		x->clock += a;
		break;
	case L_PASS:
		kp_net_lobby_frame(&x->seat);
		break;
	case L_MESSAGE:
		if (!get8(r, &a) || !get16(r, &n) || n > x->maxmsg || !get(r, n, &p)) return 0;
		if (x->message_count < MESSAGES && n) {
			QMessage *m = &x->messages[x->message_count++];
			m->seq = x->seq;
			m->sender = (int)(a & 7);
			m->size = n;
			memcpy(m->data, p, n);
		}
		break;
	case L_TURNS: {
		QTurns t;
		unsigned mask;
		memset(&t, 0, sizeof t);
		t.ok = !(op & L_TURNS_FAIL);
		t.seq = x->seq;
		if (!get8(r, &mask)) return 0;
		for (k = 0; k < MAX_PLAYERS; k++) {
			if (!(mask >> k & 1)) continue;
			if (!get32(r, &t.status[k])) return 0;
			if (!t.ok) continue;
			if (!get16(r, &n) || n > x->maxmsg || !get(r, n, &p)) return 0;
			t.size[k] = n;
			memcpy(t.data[k], p, n);
		}
		if (x->turn_count < TURN_RESULTS) x->turns[x->turn_count++] = t;
		break;
	}
	case L_EVENT: {
		QEvent e;
		if (!get8(r, &a) || !get8(r, &b) || !get8(r, &n) || !get(r, n, &p)) return 0;
		e.seq = x->seq;
		e.where = (int)(op >> 4 & 3);
		e.id = a;
		e.player = (int32_t)(int8_t)b;
		e.size = n;
		memcpy(e.data, p, n);
		if (e.where == W_NOW) {
			KpSnetEvent ev;
			ev.id = e.id;
			ev.player = e.player;
			ev.data = n ? e.data : NULL;
			ev.size = n;
			kp_netgame_event(&x->seat.game, &ev);
		} else if (x->event_count < EVENTS) {
			x->events[x->event_count++] = e;
		}
		break;
	}
	case L_NAME:
		if (!get8(r, &a) || !get8(r, &n) || !get(r, n, &p)) return 0;
		if (a < MAX_PLAYERS) {
			memset(x->names[a], 0, sizeof x->names[a]);
			memcpy(x->names[a], p, n < KP_NETGAME_NAME ? n : KP_NETGAME_NAME - 1);
		}
		break;
	case L_MODE: {
		uint32_t mode;
		if (!get32(r, &mode)) return 0;
		if (x->mode_count < STATES) x->modes[x->mode_count++] = mode;
		break;
	}
	case L_TRANSIT:
		if (!get8(r, &a)) return 0;
		if (x->transit_count < STATES) x->transits[x->transit_count++] = (uint8_t)a;
		break;
	case L_FAIL:
		if (!get8(r, &a)) return 0;
		x->fail = a;
		break;
	case L_SAVE:
		if (!get16(r, &n) || !get(r, n, &p)) return 0;
		if (kp_netsave_koprulu(p, n)) /* Koprulu's own network save: its container and NETG (the product's) */
			x->seat.save = kp_netsave_koprulu_read(p, n, L_SAVE_FILE, (op & L_SAVE_EXPANSION) != 0, &x->save, NULL)
					== KP_NETSAVE_VALID
				? &x->save : NULL;
		else
			x->seat.save = kp_netsave_probe(p, n, L_SAVE_FILE, NULL, NULL) == KP_NETSAVE_VALID &&
					kp_netsave_read(p, n, (op & L_SAVE_EXPANSION) != 0, &x->save)
				? &x->save : NULL;
		break;
	case L_ACTION:
		if ((op >> 4) == A_LIST) {
			if (!read_games(x, r)) return 0;
			if (x->seat.phase == KP_NET_LOBBY_MENU) kp_netgame_enum(&x->seat.game);
		} else if (!act(&x->seat, &x->templates, x->flags, (int)(op >> 4), r)) {
			return 0;
		}
		break;
	default: break;
	}
	l_check(x);
	return !x->bad;
}

/* The seat's session in a digest: what it sent, its phase, its lobby state, its seed */
static uint64_t l_digest_end(const Lobby *x)
{
	uint64_t h = x->digest;
	int32_t v[3];
	v[0] = x->seat.phase;
	v[1] = x->seat.game.lobby_state;
	v[2] = (int32_t)x->seat.game.seed;
	return fnv(h, v, sizeof v);
}

static int l_run(const uint8_t *d, size_t size, char *why, size_t why_size, uint64_t *digest)
{
	Lobby *x;
	Reader r;
	KpNetCalls calls;
	unsigned flags, maxmsg, transit;
	int rc = 0;
	r.d = d;
	r.size = size;
	r.at = 0;
	if (!get8(&r, &flags) || !get16(&r, &maxmsg) || !get8(&r, &transit)) return 0;
	if (!(x = (Lobby *)calloc(1, sizeof *x))) return 0;
	x->flags = flags;
	x->maxmsg = maxmsg > KP_NETGAME_TURN_MAX ? KP_NETGAME_TURN_MAX : maxmsg;
	x->caps_transit = transit;
	x->transit = transit;
	x->clock = KP_NET_WORLD_START;
	x->digest = FNV_BASIS;
	kp_net_lobby_templates(&x->templates);
	kp_net_lobby_map_bytes(x->map, sizeof x->map, KP_NET_LOBBY_MAP_SEED);
	x->seat.clock = &x->clock;
	calls = fake_calls(x);
	if (kp_net_lobby_seat_init(&x->seat, KP_NET_LOBBY_NAMES[flags >> 1 & 7], &x->templates, x->map,
			(flags & L_HOST) || (flags & L_HAS_MAP), &calls)) {
		while (l_op(x, &r)) {
		}
	}
	if (x->bad) rc = failed(why, why_size, "lobby: %s", x->why);
	if (digest) *digest = l_digest_end(x);
	kp_net_lobby_seat_free(&x->seat);
	free(x);
	return rc;
}

int kp_fuzz_lobby_run(const uint8_t *d, size_t size, char *why, size_t why_size)
{
	return l_run(d, size, why, why_size, NULL);
}

/* ========================================================================================================== seeds */

/* --- transfer: sessions in memory, one side's packets recorded --- */

#define TX_SESSIONS 3
#define TX_IN_FLIGHT 256

typedef struct {
	int from, to;
	size_t size;
	uint8_t data[KP_NETGAME_TURN_MAX];
} TxPacket;

typedef struct {
	Tx side[TX_SESSIONS];
	int count, recorded;
	TxPacket flight[TX_IN_FLIGHT];
	int in_flight, lost;
	Buf seed;
} TxGen;

static void txg_forward(void *ud, int from, int to, const uint8_t *packet, size_t size)
{
	TxGen *g = (TxGen *)ud;
	TxPacket *p;
	if (g->in_flight >= TX_IN_FLIGHT || size > sizeof p->data) {
		g->lost++;
		return;
	}
	p = &g->flight[g->in_flight++];
	p->from = from;
	p->to = to;
	p->size = size;
	memcpy(p->data, packet, size);
}

/* An op of the recorded side, written to the seed and run */
static void txg_op(TxGen *g, const uint8_t *op, size_t n)
{
	Reader r;
	put(&g->seed, op, n);
	r.d = op;
	r.size = n;
	r.at = 0;
	(void)tx_op(&g->side[g->recorded], &r);
}

/* The packets in flight delivered until none is left; the recorded side's written as TX_PACKET ops */
static void txg_deliver(TxGen *g)
{
	while (g->in_flight) {
		TxPacket p = g->flight[0];
		int k;
		memmove(&g->flight[0], &g->flight[1], sizeof g->flight[0] * (size_t)(g->in_flight - 1));
		g->in_flight--;
		for (k = 0; k < g->count; k++) {
			if (g->side[k].local != p.to) continue;
			if (k == g->recorded) {
				uint8_t op[3 + KP_NETGAME_TURN_MAX];
				op[0] = (uint8_t)(TX_PACKET | (unsigned)p.from << 3);
				kp_net_put16(op + 1, (uint16_t)p.size);
				memcpy(op + 3, p.data, p.size);
				txg_op(g, op, 3 + p.size);
			} else {
				kp_transfer_dispatch(&g->side[k].t, p.data, p.size, p.from);
			}
		}
	}
}

/* A side's peer added (the recorded side's as its op) */
static void txg_add_peer(TxGen *g, int k, int storm)
{
	if (k == g->recorded) {
		uint8_t op = (uint8_t)(TX_PEER | (unsigned)storm << 3);
		txg_op(g, &op, 1);
	} else if (g->side[k].offer) {
		kp_transfer_add_peer(&g->side[k].t, g->side[k].offer, storm);
	}
	txg_deliver(g);
}

/* A transfer seed: sides of the flags given, the offering side (0) adding the others as peers; the recorded side's
 * session written. Its bytes into *seed (the caller frees), its digest into *digest; 0, or -1 (why). */
static int txg_seed(const unsigned *flags, int count, int recorded, uint8_t **seed, size_t *size, uint64_t *digest,
	char *why, size_t why_size)
{
	TxGen *g = (TxGen *)calloc(1, sizeof *g);
	uint8_t map[KP_NET_LOBBY_MAP_SIZE], check = TX_CHECK;
	int k, rc = 0;
	if (!g) return failed(why, why_size, "out of memory");
	kp_net_lobby_map_bytes(map, sizeof map, KP_NET_LOBBY_MAP_SEED);
	g->count = count;
	g->recorded = recorded;
	put8(&g->seed, flags[recorded]);
	put16(&g->seed, KP_SNET_UDP_CAPS.maxmsg - 4);
	for (k = 0; k < count; k++) {
		tx_init(&g->side[k], flags[k], KP_SNET_UDP_CAPS.maxmsg - 4, map);
		g->side[k].forward = txg_forward;
		g->side[k].forward_ud = g;
	}
	/* the offering seat's own join request adds it first (kp_netgame: 0x40 from every seat, the host's own included):
	 * its own status makes it the uploader */
	for (k = 0; k < count; k++) txg_add_peer(g, 0, g->side[k].local);
	txg_op(g, &check, 1);
	if (g->lost) rc = failed(why, why_size, "transfer seed: %d packets past the flight", g->lost);
	for (k = 0; k < count && !rc; k++)
		if (g->side[k].bad) rc = failed(why, why_size, "transfer seed: side %d: %s", k, g->side[k].why);
	if (g->seed.oom) rc = failed(why, why_size, "out of memory");
	*digest = g->side[recorded].digest;
	*seed = g->seed.b;
	*size = g->seed.n;
	if (rc) free(g->seed.b);
	for (k = 0; k < count; k++) kp_transfer_reset(&g->side[k].t);
	free(g);
	return rc;
}

/* --- lobby: KpNetLobby on the LAN, one seat's answers, passes and actions recorded --- */

typedef struct {
	KpNetLobby l;
	int k;               /* the recorded seat */
	KpNetCalls real;     /* its kp_snet's */
	Buf seed, pending;   /* the seed; the answers of the pass or action running */
	int in_step;
	int where;           /* the SNet call running (W_*) */
	uint32_t last;       /* the seed's clock */
	KpNetLobbySeat *before; /* the recorded seat before its pass */
	char names[MAX_PLAYERS][KP_NETGAME_NAME];
	unsigned flags, maxmsg, caps_transit;
	Buf games;           /* the list's answer */
	int game_count;
	int (*enum_fn)(void *fn_ud, const KpSnetGame *g);
	void *enum_ud;
	uint64_t digest;     /* what the seat sent, as the replay's fakes hash it */
	KpNetGameSave saves[2]; /* the resumed game's saves: the recorded seat's, another seat's */
	char why[160];
	int bad;
} LGen;

static void lg_fail(LGen *g, unsigned kind)
{
	put8(&g->pending, L_FAIL);
	put8(&g->pending, kind);
}

static void lg_digest(LGen *g, uint32_t tag, const void *p, size_t n)
{
	g->digest = fnv(g->digest, &tag, sizeof tag);
	g->digest = fnv(g->digest, p, n);
}

static int r_create(void *ud, const char *name, const char *password, const char *stats, uint32_t game_type,
	uint32_t ladder_type, uint32_t mode, const void *tmpl, uint32_t tmpl_size, uint32_t max_players,
	const char *player_name, const char *player_desc, int *player_id)
{
	LGen *g = (LGen *)ud;
	int ok;
	lg_digest(g, 1, name, strlen(name));
	lg_digest(g, 1, stats, strlen(stats));
	lg_digest(g, 1, player_name, strlen(player_name));
	lg_digest(g, 1, &mode, sizeof mode);
	ok = g->real.create_game(g->real.ud, name, password, stats, game_type, ladder_type, mode, tmpl, tmpl_size,
		max_players, player_name, player_desc, player_id);
	if (!ok) lg_fail(g, F_CREATE);
	else g->flags = (g->flags & ~0x0eu) | (unsigned)(*player_id & 7) << 1;
	return ok;
}

static int r_join(void *ud, uint32_t game_id, const char *name, const char *password, const char *player_name,
	const char *player_desc, int *player_id)
{
	LGen *g = (LGen *)ud;
	int ok;
	lg_digest(g, 2, &game_id, sizeof game_id);
	lg_digest(g, 2, player_name, strlen(player_name));
	g->where = W_JOIN;
	ok = g->real.join_game(g->real.ud, game_id, name, password, player_name, player_desc, player_id);
	g->where = W_NOW;
	if (!ok) lg_fail(g, F_JOIN);
	else g->flags = (g->flags & ~0x0eu) | (unsigned)(*player_id & 7) << 1;
	return ok;
}

static int r_leave(void *ud, uint32_t reason)
{
	LGen *g = (LGen *)ud;
	int ok;
	lg_digest(g, 3, &reason, sizeof reason);
	ok = g->real.leave_game(g->real.ud, reason);
	if (!ok) lg_fail(g, F_LEAVE);
	return ok;
}

static int r_enum_game(void *ud, const KpSnetGame *game)
{
	LGen *g = (LGen *)ud;
	size_t n;
	put32(&g->games, game->id);
	put8(&g->games, game->players);
	put8(&g->games, game->max_players);
	n = strlen(game->name ? game->name : "");
	put8(&g->games, (unsigned)n);
	put(&g->games, game->name, n);
	n = strlen(game->stats ? game->stats : "");
	put8(&g->games, (unsigned)n);
	put(&g->games, game->stats, n);
	g->game_count++;
	return g->enum_fn(g->enum_ud, game);
}

static int r_enum(void *ud, int (*fn)(void *fn_ud, const KpSnetGame *g), void *fn_ud, uint32_t *hint)
{
	LGen *g = (LGen *)ud;
	g->enum_fn = fn;
	g->enum_ud = fn_ud;
	return g->real.enum_games(g->real.ud, r_enum_game, g, hint);
}

static int r_send_turn(void *ud, const void *data, uint32_t size)
{
	LGen *g = (LGen *)ud;
	int ok;
	lg_digest(g, 4, data, size);
	ok = g->real.send_turn(g->real.ud, data, size);
	if (!ok) lg_fail(g, F_SEND_TURN);
	return ok;
}

static int r_receive_turns(void *ud, int first, int count, const uint8_t **data, uint32_t *sizes, uint32_t *status)
{
	LGen *g = (LGen *)ud;
	unsigned mask = 0;
	int ok, k;
	g->where = W_TURNS;
	ok = g->real.receive_turns(g->real.ud, first, count, data, sizes, status);
	g->where = W_NOW;
	for (k = 0; k < count; k++)
		if (status[k] || (ok && sizes[k])) mask |= 1u << (first + k);
	put8(&g->pending, L_TURNS | (ok ? 0 : L_TURNS_FAIL));
	put8(&g->pending, mask);
	for (k = 0; k < count; k++) {
		if (!(mask >> (first + k) & 1)) continue;
		put32(&g->pending, status[k]);
		if (!ok) continue;
		put16(&g->pending, sizes[k]);
		put(&g->pending, data[k], sizes[k]);
	}
	return ok;
}

static int r_in_transit(void *ud, uint32_t *turns)
{
	LGen *g = (LGen *)ud;
	int ok = g->real.turns_in_transit(g->real.ud, turns);
	put8(&g->pending, L_TRANSIT);
	put8(&g->pending, !ok ? L_TRANSIT_FAILS : *turns >= L_TRANSIT_FAILS ? L_TRANSIT_FAILS - 1 : *turns);
	return ok;
}

static int r_send_message(void *ud, int target, const void *data, uint32_t size)
{
	LGen *g = (LGen *)ud;
	int ok;
	lg_digest(g, 5, &target, sizeof target);
	lg_digest(g, 5, data, size);
	ok = g->real.send_message(g->real.ud, target, data, size);
	if (!ok) lg_fail(g, F_SEND_MESSAGE);
	return ok;
}

static int r_receive_message(void *ud, int *sender, const uint8_t **data, uint32_t *size)
{
	LGen *g = (LGen *)ud;
	int ok;
	g->where = W_MESSAGE;
	ok = g->real.receive_message(g->real.ud, sender, data, size);
	g->where = W_NOW;
	if (ok) {
		put8(&g->pending, L_MESSAGE);
		put8(&g->pending, (unsigned)*sender);
		put16(&g->pending, *size);
		put(&g->pending, *data, *size);
	}
	return ok;
}

static int r_game_info(void *ud, int type, void *buf, uint32_t size, uint32_t *written)
{
	LGen *g = (LGen *)ud;
	int ok = g->real.game_info(g->real.ud, type, buf, size, written);
	if (!ok) {
		lg_fail(g, F_GAME_INFO);
	} else if (type == KP_SNET_INFO_MODE && size >= 4) {
		put8(&g->pending, L_MODE);
		put32(&g->pending, kp_net_get32((const uint8_t *)buf));
	}
	return ok;
}

static int r_set_mode(void *ud, uint32_t mode, int make_private)
{
	LGen *g = (LGen *)ud;
	int ok;
	lg_digest(g, 6, &mode, sizeof mode);
	lg_digest(g, 6, &make_private, sizeof make_private);
	ok = g->real.set_game_mode(g->real.ud, mode, make_private);
	if (!ok) lg_fail(g, F_SET_MODE);
	return ok;
}

static int r_player_name(void *ud, int id, char *buf, uint32_t size)
{
	LGen *g = (LGen *)ud;
	int ok = g->real.player_name(g->real.ud, id, buf, size);
	size_t n;
	if (id < 0 || id >= MAX_PLAYERS || !size) return ok;
	/* the answer from now on, written when it changed (a failure: no name) */
	n = ok ? kp_net_text_len(buf, size) : 0;
	if (n >= KP_NETGAME_NAME) n = KP_NETGAME_NAME - 1;
	if (strlen(g->names[id]) == n && memcmp(g->names[id], buf, n) == 0) return ok;
	memset(g->names[id], 0, sizeof g->names[id]);
	memcpy(g->names[id], buf, n);
	put8(&g->pending, L_NAME);
	put8(&g->pending, (unsigned)id);
	put8(&g->pending, (unsigned)n);
	put(&g->pending, buf, n);
	return ok;
}

static int r_caps(void *ud, KpSnetCaps *caps)
{
	LGen *g = (LGen *)ud;
	int ok = g->real.provider_caps(g->real.ud, caps);
	g->maxmsg = caps->maxmsg;
	g->caps_transit = caps->turns_in_transit;
	return ok;
}

static int r_drop(void *ud, int id, uint32_t reason)
{
	LGen *g = (LGen *)ud;
	int ok;
	lg_digest(g, 7, &id, sizeof id);
	lg_digest(g, 7, &reason, sizeof reason);
	ok = g->real.drop_player(g->real.ud, id, reason);
	if (!ok) lg_fail(g, F_DROP);
	return ok;
}

static int r_disconnect(void *ud, uint32_t reason)
{
	LGen *g = (LGen *)ud;
	lg_digest(g, 8, &reason, sizeof reason);
	return g->real.disconnect_all(g->real.ud, reason);
}

static uint32_t r_error(void *ud) { return ((LGen *)ud)->real.error(((LGen *)ud)->real.ud); }

static KpNetCalls recorder_calls(LGen *g)
{
	KpNetCalls c;
	memset(&c, 0, sizeof c);
	c.create_game = r_create;
	c.join_game = r_join;
	c.leave_game = r_leave;
	c.enum_games = r_enum;
	c.send_turn = r_send_turn;
	c.receive_turns = r_receive_turns;
	c.turns_in_transit = r_in_transit;
	c.send_message = r_send_message;
	c.receive_message = r_receive_message;
	c.game_info = r_game_info;
	c.set_game_mode = r_set_mode;
	c.player_name = r_player_name;
	c.provider_caps = r_caps;
	c.drop_player = r_drop;
	c.disconnect_all = r_disconnect;
	c.error = r_error;
	c.ud = g;
	return c;
}

/* The seed's clock brought to now */
static void lg_clock(LGen *g)
{
	uint32_t now = kp_net_lobby_now(&g->l);
	while (now != g->last) {
		uint32_t d = now - g->last > 0xffffu ? 0xffffu : now - g->last;
		put8(&g->seed, L_CLOCK);
		put16(&g->seed, d);
		g->last += d;
	}
}

/* A pass or an action of the recorded seat: its answers, then its op */
static void lg_begin(LGen *g)
{
	g->pending.n = 0;
	g->in_step = 1;
}

static void lg_end(LGen *g, const uint8_t *op, size_t n, int always)
{
	g->in_step = 0;
	if (!always && !g->pending.n) return;
	lg_clock(g);
	put(&g->seed, g->pending.b, g->pending.n);
	put(&g->seed, op, n);
	if (g->pending.oom) g->seed.oom = 1;
}

static void lg_frame(KpNetSeat *s)
{
	LGen *g = (LGen *)s->w; /* the world is the generator's first member */
	KpNetLobbySeat *st = (KpNetLobbySeat *)s;
	uint8_t op = L_PASS;
	uint64_t sent = g->digest;
	if (s->index != g->k) {
		kp_net_lobby_frame(st);
		return;
	}
	/* a pass that changed nothing, sent nothing and took no answer is left out: the replay's would do as little */
	memcpy(g->before, st, sizeof *st);
	lg_begin(g);
	kp_net_lobby_frame(st);
	lg_end(g, &op, 1, g->digest != sent || memcmp(g->before, st, sizeof *st) != 0);
}

static void lg_event(KpNetSeat *s, const KpSnetEvent *e)
{
	LGen *g = (LGen *)s->w;
	KpNetLobbySeat *st = (KpNetLobbySeat *)s;
	if (!st->inited) return;
	if (s->index == g->k) {
		Buf *b = g->in_step ? &g->pending : &g->seed;
		uint32_t n = e->size > EVENT_DATA ? EVENT_DATA : e->size;
		if (e->size > EVENT_DATA && !g->bad++) snprintf(g->why, sizeof g->why, "an event of %u bytes", e->size);
		if (!g->in_step) lg_clock(g);
		put8(b, L_EVENT | (unsigned)g->where << 4);
		put8(b, e->id);
		put8(b, (unsigned)(uint8_t)e->player);
		put8(b, n);
		put(b, e->data, n);
	}
	kp_netgame_event(&st->game, e);
}

/* An action of the recorded seat with its arguments, written and run */
static void lg_act(LGen *g, int kind, const uint8_t *args, size_t n)
{
	KpNetLobbySeat *s = &g->l.seat[g->k];
	uint8_t op[260];
	Reader r;
	op[0] = (uint8_t)(L_ACTION | (unsigned)kind << 4);
	if (n > 0) memcpy(op + 1, args, n); /* an action without arguments passes NULL */
	r.d = args;
	r.size = n;
	r.at = 0;
	lg_begin(g);
	(void)act(s, &g->l.templates, g->flags, kind, &r);
	lg_end(g, op, n + 1, 1);
}

/* The recorded seat's saved game: its file written as an L_SAVE op */
static void lg_save(LGen *g, const KpNetGameSave *save)
{
	size_t n = 0;
	uint8_t *file = kp_net_lobby_save_file(save, KP_NETSAVE_VERSION, "fuzz", 0, &n);
	if (!file || n > 0xffffu) {
		if (!g->bad++) snprintf(g->why, sizeof g->why, "a save file of %zu bytes", n);
		free(file);
		return;
	}
	lg_clock(g);
	put8(&g->seed, L_SAVE);
	put16(&g->seed, (unsigned)n);
	put(&g->seed, file, n);
	free(file);
	g->l.seat[g->k].save = save;
}

static void lg_chat(LGen *g, const char *text)
{
	uint8_t args[256];
	size_t n = strlen(text);
	args[0] = (uint8_t)n;
	memcpy(args + 1, text, n);
	lg_act(g, A_CHAT, args, n + 1);
}

/* The recorded joiner's Join screen: the list's wait, its refresh (the list's answer written as the op's arguments),
 * the join */
static int lg_join(LGen *g)
{
	KpNetLobbySeat *s = &g->l.seat[g->k];
	Buf op;
	kp_net_lobby_advance(&g->l, KP_NET_LOBBY_LIST_MS);
	g->games.n = 0;
	g->game_count = 0;
	lg_begin(g);
	kp_netgame_enum(&s->game);
	memset(&op, 0, sizeof op);
	put8(&op, L_ACTION | A_LIST << 4);
	put8(&op, (unsigned)g->game_count);
	put(&op, g->games.b, g->games.n);
	if (op.oom || g->games.oom) g->seed.oom = 1;
	lg_end(g, op.b, op.n, 1);
	free(op.b);
	lg_act(g, A_JOIN, NULL, 0);
	return s->phase == KP_NET_LOBBY_ROOM;
}

static void lg_free(LGen *g)
{
	kp_net_lobby_free(&g->l);
	free(g->before);
	free(g->seed.b);
	free(g->pending.b);
	free(g->games.b);
	free(g);
}

/* A lobby of `seats` seats with seat k recorded, the recorder's calls set up (its seat inited by the script) */
static LGen *lg_new(uint64_t seed, int seats, int k, char *why, size_t why_size)
{
	LGen *g = (LGen *)calloc(1, sizeof *g);
	if (!g || !(g->before = (KpNetLobbySeat *)malloc(sizeof *g->before))) {
		free(g);
		failed(why, why_size, "out of memory");
		return NULL;
	}
	if (!kp_net_lobby_init(&g->l, NULL, NULL, NULL, seed, seats, 0)) {
		failed(why, why_size, "lobby seed: %s", g->l.n.why);
		lg_free(g);
		return NULL;
	}
	g->l.n.frame = lg_frame;
	g->l.n.event = lg_event;
	g->k = k;
	g->real = kp_netgame_snet_calls(g->l.seat[k].n.net);
	g->last = KP_NET_WORLD_START;
	g->where = W_NOW;
	g->digest = FNV_BASIS;
	return g;
}

/* The recorded seat's KpNetGame (the recorder's calls) as a host or a joiner without the map */
static int lg_seat(LGen *g, int has_map)
{
	KpNetCalls calls = recorder_calls(g);
	KpNetLobbySeat *s = &g->l.seat[g->k];
	g->flags = (has_map ? L_HAS_MAP : 0);
	return kp_net_lobby_seat_init(s, KP_NET_LOBBY_NAMES[g->k], &g->l.templates, g->l.map, has_map, &calls);
}

/* The seed's bytes: the header, then the ops; the session's digest as the replay computes it */
static uint8_t *lg_bytes(LGen *g, size_t *size, uint64_t *digest)
{
	const KpNetLobbySeat *s = &g->l.seat[g->k];
	uint8_t *b = (uint8_t *)malloc(L_HEADER + g->seed.n);
	int32_t v[3];
	if (!b) return NULL;
	b[0] = (uint8_t)g->flags;
	kp_net_put16(b + 1, (uint16_t)g->maxmsg);
	b[3] = (uint8_t)g->caps_transit;
	memcpy(b + L_HEADER, g->seed.b, g->seed.n);
	*size = L_HEADER + g->seed.n;
	v[0] = s->phase;
	v[1] = s->game.lobby_state;
	v[2] = (int32_t)s->game.seed;
	*digest = fnv(g->digest, v, sizeof v);
	return b;
}

#define LOBBY_SETTLE_MS 3000
#define LOBBY_START_MS 30000

/* The scripts: the recorded seat's role, the others' steps */
enum { S_HOST, S_JOIN, S_UMS, S_KICKED, S_BANNER, S_RESUMED };

static int lg_script(LGen *g, int script)
{
	KpNetLobby *l = &g->l;
	switch (script) {
	case S_HOST: /* the recorded host: Beta without the map, Gamma with it, chat, a race, the start */
		if (!lg_seat(g, 1)) return 0;
		g->flags |= L_HOST;
		lg_act(g, A_CREATE, NULL, 0);
		if (!kp_net_lobby_join(l, 1, 0, NULL) || !kp_net_lobby_join(l, 2, 1, NULL)) return 0;
		kp_net_lobby_advance(l, LOBBY_SETTLE_MS);
		kp_netgame_chat(&l->seat[1].game, "hello from Beta");
		lg_chat(g, "/w Gamma a secret");
		kp_net_lobby_advance(l, LOBBY_SETTLE_MS);
		{
			uint8_t args[2];
			args[0] = (uint8_t)l->seat[0].game.local_slot;
			args[1] = 2;
			lg_act(g, A_RACE, args, 2);
		}
		kp_net_lobby_advance(l, LOBBY_SETTLE_MS);
		lg_act(g, A_START, NULL, 0);
		kp_net_lobby_run_to_start(l, LOBBY_START_MS);
		return l->seat[0].phase == KP_NET_LOBBY_STARTED;
	case S_JOIN: /* the recorded joiner without the map: Gamma joins too, chat, the start */
		if (!kp_net_lobby_host(l, 0, KP_NET_LOBBY_MELEE, NULL) || !lg_seat(g, 0) || !lg_join(g)) return 0;
		if (!kp_net_lobby_join(l, 2, 1, NULL)) return 0;
		kp_net_lobby_advance(l, LOBBY_SETTLE_MS);
		lg_chat(g, "hello from Beta");
		kp_netgame_chat(&l->seat[0].game, "/w Beta welcome");
		kp_net_lobby_advance(l, LOBBY_SETTLE_MS);
		kp_netgame_start(&l->seat[0].game);
		kp_net_lobby_run_to_start(l, LOBBY_START_MS);
		return l->seat[1].phase == KP_NET_LOBBY_STARTED;
	case S_UMS: /* the recorded joiner of a Use Map Settings game: the ready room */
		if (!kp_net_lobby_host(l, 0, KP_NET_LOBBY_UMS, NULL) || !lg_seat(g, 1)) return 0;
		g->flags |= L_UMS;
		if (!lg_join(g)) return 0;
		kp_net_lobby_advance(l, LOBBY_SETTLE_MS);
		kp_netgame_start(&l->seat[0].game);
		kp_net_lobby_run_to_start(l, LOBBY_START_MS);
		l->seat[0].press_start = 1;
		lg_act(g, A_READY, NULL, 0);
		kp_net_lobby_run_to_start(l, LOBBY_START_MS);
		return l->seat[1].phase == KP_NET_LOBBY_STARTED;
	case S_KICKED: /* the recorded joiner booted by the host */
		if (!kp_net_lobby_host(l, 0, KP_NET_LOBBY_MELEE, NULL) || !lg_seat(g, 1) || !lg_join(g)) return 0;
		kp_net_lobby_advance(l, LOBBY_SETTLE_MS);
		kp_netgame_slot_action(&l->seat[0].game, kp_net_lobby_slot_of(l, 0, 1), KP_SLOT_OPEN);
		kp_net_lobby_advance(l, LOBBY_SETTLE_MS);
		return l->seat[1].phase == KP_NET_LOBBY_LEFT;
	case S_BANNER: { /* the recorded host bans Beta, who joins again and is refused, and starts with Gamma */
		uint8_t slot;
		if (!lg_seat(g, 1)) return 0;
		g->flags |= L_HOST;
		lg_act(g, A_CREATE, NULL, 0);
		if (!kp_net_lobby_join(l, 1, 1, NULL) || !kp_net_lobby_join(l, 2, 1, NULL)) return 0;
		kp_net_lobby_advance(l, LOBBY_SETTLE_MS);
		slot = (uint8_t)kp_net_lobby_slot_of(l, 0, 1);
		lg_act(g, A_BAN, &slot, 1);
		kp_net_lobby_advance(l, LOBBY_SETTLE_MS);
		(void)kp_net_lobby_join(l, 1, 1, NULL);
		kp_net_lobby_advance(l, LOBBY_SETTLE_MS);
		lg_act(g, A_START, NULL, 0);
		kp_net_lobby_run_to_start(l, LOBBY_START_MS);
		return l->seat[0].phase == KP_NET_LOBBY_STARTED && l->seat[1].phase == KP_NET_LOBBY_LEFT;
	}
	case S_RESUMED: /* the recorded host resumes a melee save, Beta joins with its own, the start */
		if (!lg_seat(g, 1)) return 0;
		g->flags |= L_HOST;
		kp_net_lobby_melee_save(&g->saves[0], &l->templates, 0);
		kp_net_lobby_melee_save(&g->saves[1], &l->templates, 2);
		lg_save(g, &g->saves[0]);
		l->seat[1].save = &g->saves[1];
		lg_act(g, A_CREATE, NULL, 0);
		if (!kp_net_lobby_join(l, 1, 1, NULL)) return 0;
		kp_net_lobby_advance(l, LOBBY_SETTLE_MS);
		lg_act(g, A_START, NULL, 0);
		kp_net_lobby_run_to_start(l, LOBBY_START_MS);
		return l->seat[0].phase == KP_NET_LOBBY_STARTED && l->seat[1].phase == KP_NET_LOBBY_STARTED;
	default: return 0;
	}
}

int kp_fuzz_lobby_seeds(KpFuzzLobbySeed fn, void *ud, char *why, size_t why_size)
{
	static const struct {
		const char *name;
		int script, seats, recorded;
	} LOBBIES[] = { { "host", S_HOST, 3, 0 }, { "join", S_JOIN, 3, 1 }, { "ums", S_UMS, 2, 1 },
		{ "kicked", S_KICKED, 2, 1 }, { "banner", S_BANNER, 3, 0 }, { "resumed", S_RESUMED, 2, 0 } };
	/* the transfers: the map offered to a seat without it (each side recorded), and to one with it */
	static const unsigned OFFER = TX_OFFERS, SEAT1 = 1u << 1, SEAT2 = 2u << 1;
	static const struct {
		const char *name;
		unsigned flags[TX_SESSIONS];
		int count, recorded;
	} TRANSFERS[] = { { "download", { OFFER, SEAT1, 0 }, 2, 1 }, { "upload", { OFFER, SEAT1, 0 }, 2, 0 },
		{ "local_copy", { OFFER, SEAT1 | TX_HAS_MAP, SEAT2 }, 3, 1 }, { "two_peers", { OFFER, SEAT1, SEAT2 }, 3, 0 } };
	char run_why[256];
	int k, count = 0;
	for (k = 0; k < (int)(sizeof TRANSFERS / sizeof TRANSFERS[0]); k++) {
		uint8_t *b;
		size_t n;
		uint64_t want, got;
		if (txg_seed(TRANSFERS[k].flags, TRANSFERS[k].count, TRANSFERS[k].recorded, &b, &n, &want, why, why_size) != 0)
			return -1;
		if (tx_run(b, n, run_why, sizeof run_why, &got) != 0 || got != want) {
			free(b);
			return failed(why, why_size, "transfer seed %s does not replay its session: %s", TRANSFERS[k].name,
				run_why);
		}
		fn(ud, 0, TRANSFERS[k].name, b, n);
		free(b);
		count++;
	}
	for (k = 0; k < (int)(sizeof LOBBIES / sizeof LOBBIES[0]); k++) {
		LGen *g = lg_new((uint64_t)(k + 1), LOBBIES[k].seats, LOBBIES[k].recorded, why, why_size);
		uint8_t *b;
		size_t n;
		uint64_t want, got;
		if (!g) return -1;
		if (!lg_script(g, LOBBIES[k].script) || g->bad || g->seed.oom) {
			failed(why, why_size, "lobby seed %s: the session did not play (%s)", LOBBIES[k].name, g->why);
			lg_free(g);
			return -1;
		}
		if (!(b = lg_bytes(g, &n, &want))) {
			lg_free(g);
			return failed(why, why_size, "out of memory");
		}
		lg_free(g);
		if (l_run(b, n, run_why, sizeof run_why, &got) != 0 || got != want) {
			free(b);
			return failed(why, why_size, "lobby seed %s does not replay its session: %s", LOBBIES[k].name, run_why);
		}
		fn(ud, 1, LOBBIES[k].name, b, n);
		free(b);
		count++;
	}
	return count;
}
