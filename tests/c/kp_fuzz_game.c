/* The network game's fuzz target (kp_fuzz_game.h; kp_fuzz.h tells its input).
 *
 * game: one seat's network game after its start, the turns and messages Storm hands it coming from the input. The seat
 * hosts a two-player game (Storm IDs 0 and 1, the Use Map Settings template) of SEAT_MAP in a session over KP_TURNS_NET
 * (kp_session_net_begin, then each frame kp_session_net_messages and kp_session_step as the product runs them); its
 * first turn runs once a process on Storm's echo and every input starts from that snapshot (the game's state, the
 * session's and the network game's).
 *
 * An input: {u8 flags}, then ops (op & 7 the kind, the rest its bits):
 *   G_FRAME    (op >> 3) + 1 frames of the game: its messages received, then its step (a waiting step runs no frame)
 *   G_TURNS    a result of SNetReceiveTurns {u8 slots}, for each slot's bit {u32 status, u16 n, n bytes}; bit 3: the
 *              call fails, the slots' status words alone
 *   G_MESSAGE  a message for SNetReceiveMessage {u8 sender, u16 n, n bytes} (an in-game run: type 0 chat, 1 a timed
 *              line, 2 refused, 3 or more its end)
 *   G_TRANSIT  SNetGetTurnsInTransit's answer from now on {u8 turns}; 0xff: the call fails
 *   G_COMMAND  a local command {u8 n, n bytes} (QueueTurnCommand 0x476c80)
 *   G_CHAT     a local chat line {u16 recipients, u8 n, n bytes} (SendInGameChat 0x473be0, slash commands among them)
 *   G_FAIL     the next call of each kind of the mask fails {u8 mask: F_*}
 *   G_CLOCK    the clock moved {u16 ms}; bit 3: then timeout.bin's Drop Players (kp_netgame_timeout_drop)
 * flags: bit 0 Storm's echo: with no result queued, SNetReceiveTurns hands the seat's oldest sent turn back as Storm
 * ID 0's and a keep-alive as the peer's (an idle peer: a copy of the seat's turn would carry the seat's sync, whose
 * visibility bits are its player's, and the check would rightly mark the peer out of sync). Queued results come first, each in
 * the order given; a result or a message past its queue's room is dropped. A fatal report (the latch) ends the input.
 *
 * Its properties: what the seat sends stays within the provider's message size, its outgoing turn and its turn's list
 * within their bounds, and the game never stops (every seat runs every seat's turn, so a game the game's own checks
 * stop is a stop for every seat: a finding, as the commands target's network game has it). */
#include "kp_fuzz_game.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kp_ids.h"
#include "kp_net.h"
#include "kp_netgame.h"
#include "kp_runner.h"
#include "kp_session.h"
#include "kp_snet.h"
#include "../../native/kp_core/src/core/kp_mission.h"
#include "../../native/kp_core/src/core/kp_start.h"

#define ROOT "."
#define SEAT_MAP "scenarios/build_terran"
#define SEAT_SEED 0x4b504e47u
#define SEAT_SPEED 6           /* the game header's speed: the fastest */
#define SEAT_GAME "Fuzz Game"
#define PEER 1                 /* the peer's Storm ID and slot */
#define BEGIN_TRIES 64         /* kp_session_net_begin's polls until the first turn ran */
#define G_TURN_RESULTS 16
#define G_MESSAGES 32
#define G_SENT 16              /* the echo's ring of the seat's sent turns */
#define G_BYTES 0x400          /* a turn's or a message's bytes kept, at most */
#define G_FRAMES 512           /* an input's frames, at most */
#define G_PLAYERS 8
#define G_NAME 24
#define G_TEXT 256
#define G_ECHO 0x01u
#define G_TURNS_FAIL 0x08u
#define G_CLOCK_DROP 0x08u
#define G_TRANSIT_FAILS 0xffu
#define TURN_PRESENT (KP_SNET_PRESENT | KP_SNET_HAS_TURN | KP_SNET_RESPONDING)
#define LAG_MS 3000            /* past the turns' alive time (KP_NETGAME_ALIVE_MS): timeout.bin opens */
#define TICK_MS 1001           /* past its one-second tick */
#define TIMEOUT_TICKS 46       /* its 45 seconds counted down, and one more */

enum { G_FRAME, G_TURNS, G_MESSAGE, G_TRANSIT, G_COMMAND, G_CHAT, G_FAIL, G_CLOCK };
enum { F_SEND_TURN = 1, F_SEND_MESSAGE = 2, F_RECEIVE_TURNS = 4, F_RECEIVE_MESSAGE = 8, F_TRANSIT = 0x10, F_CAPS = 0x20 };

typedef struct {
	uint32_t status[G_PLAYERS];
	uint16_t size[G_PLAYERS];
	uint8_t data[G_PLAYERS][G_BYTES];
	int fails;
} Result;

typedef struct {
	int sender;
	uint16_t size;
	uint8_t data[G_BYTES];
} Message;

/* Storm as the input answers it */
typedef struct {
	Result results[G_TURN_RESULTS];
	int result_head, result_count;
	Message messages[G_MESSAGES];
	int message_head, message_count;
	uint8_t sent[G_SENT][G_BYTES];
	uint16_t sent_size[G_SENT];
	int sent_head, sent_count;
	Result current;            /* the buffers SNetReceiveTurns hands out */
	Message current_message;
	int echo, transit;         /* transit: SNetGetTurnsInTransit's answer, -1 the sent turns waiting */
	unsigned fail;
	uint32_t error, clock;
	int fatal;                 /* a fatal report (the latch) */
	KpFuzzGameLast last;       /* the input's counts (fatal kept above) */
	char bad[128];             /* a property that failed */
} Storm;

typedef struct {
	KpSession *s;
	KpNetGame *ng;
	Storm *storm, *base_storm; /* base: as the snapshot left it (the turns in transit sent before it) */
	uint8_t *snap;
	size_t snap_size;
	KpSession base_session;
	KpNetGame base_game;
	int ready;
} Seat;

static Seat seat;

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

static Storm *st(void *ud) { return (Storm *)ud; }

static int refuse(Storm *x, uint32_t error)
{
	x->error = error;
	return 0;
}

/* A call of a kind of the fail mask fails once */
static int fails(Storm *x, unsigned kind)
{
	if (!(x->fail & kind)) return 0;
	x->fail &= ~kind;
	x->error = KP_SNET_ERROR_PARAMETER;
	return 1;
}

static void bad(Storm *x, const char *what, long a, long b)
{
	if (!x->bad[0]) snprintf(x->bad, sizeof x->bad, "%s (%ld, %ld)", what, a, b);
}

/* --- SNet's calls --- */

static int s_create(void *ud, const char *name, const char *password, const char *stats, uint32_t game_type,
	uint32_t ladder_type, uint32_t mode, const void *tmpl, uint32_t tmpl_size, uint32_t max_players,
	const char *player_name, const char *player_desc, int *player_id)
{
	(void)name, (void)password, (void)stats, (void)game_type, (void)ladder_type, (void)mode, (void)tmpl;
	(void)tmpl_size, (void)max_players, (void)player_name, (void)player_desc, (void)player_id;
	return refuse(st(ud), KP_SNET_ERROR_PARAMETER);
}

static int s_join(void *ud, uint32_t game_id, const char *name, const char *password, const char *player_name,
	const char *player_desc, int *player_id)
{
	(void)game_id, (void)name, (void)password, (void)player_name, (void)player_desc, (void)player_id;
	return refuse(st(ud), KP_SNET_ERROR_PARAMETER);
}

static int s_leave(void *ud, uint32_t reason)
{
	(void)ud, (void)reason;
	return 1;
}

static int s_enum(void *ud, int (*fn)(void *fn_ud, const KpSnetGame *g), void *fn_ud, uint32_t *hint)
{
	(void)fn, (void)fn_ud, (void)hint;
	return refuse(st(ud), KP_SNET_ERROR_NO_DATA);
}

static int s_send_turn(void *ud, const void *data, uint32_t size)
{
	Storm *x = st(ud);
	int at;
	if (seat.ng && size > seat.ng->max_message) bad(x, "a turn past the maximum message", (long)size, (long)seat.ng->max_message);
	if (fails(x, F_SEND_TURN)) return 0;
	if (x->sent_count == G_SENT || size > G_BYTES) return refuse(x, KP_SNET_ERROR_PARAMETER);
	at = (x->sent_head + x->sent_count) % G_SENT;
	if (size) memcpy(x->sent[at], data, size);
	x->sent_size[at] = (uint16_t)size;
	x->sent_count++;
	x->last.turns_sent++;
	return 1;
}

static void hand_out(const Result *r, int first, int count, const uint8_t **data, uint32_t *sizes, uint32_t *status)
{
	int k;
	for (k = 0; k < count; k++) {
		int id = first + k;
		data[k] = NULL;
		sizes[k] = 0;
		status[k] = 0;
		if (id < 0 || id >= G_PLAYERS) continue;
		status[k] = r->status[id];
		if (r->size[id]) {
			data[k] = r->data[id];
			sizes[k] = r->size[id];
		}
	}
}

static int s_receive_turns(void *ud, int first, int count, const uint8_t **data, uint32_t *sizes, uint32_t *status)
{
	Storm *x = st(ud);
	if (fails(x, F_RECEIVE_TURNS)) return 0;
	if (x->result_count) {
		x->current = x->results[x->result_head];
		x->result_head = (x->result_head + 1) % G_TURN_RESULTS;
		x->result_count--;
		hand_out(&x->current, first, count, data, sizes, status);
		return x->current.fails ? refuse(x, KP_SNET_ERROR_NO_DATA) : 1;
	}
	if (x->echo && x->sent_count) { /* the seat's own turn, and the peer's in step with it */
		uint16_t n = x->sent_size[x->sent_head];
		memset(&x->current, 0, sizeof x->current);
		memcpy(x->current.data[0], x->sent[x->sent_head], n);
		x->current.data[PEER][0] = KP_LOBBY_KEEP_ALIVE;
		x->current.size[0] = n;
		x->current.size[PEER] = 1;
		x->current.status[0] = x->current.status[PEER] = TURN_PRESENT;
		x->sent_head = (x->sent_head + 1) % G_SENT;
		x->sent_count--;
		hand_out(&x->current, first, count, data, sizes, status);
		return 1;
	}
	return refuse(x, KP_SNET_ERROR_NO_DATA);
}

static int s_in_transit(void *ud, uint32_t *turns)
{
	Storm *x = st(ud);
	if (fails(x, F_TRANSIT) || x->transit == G_TRANSIT_FAILS) return refuse(x, KP_SNET_ERROR_PARAMETER);
	*turns = x->transit >= 0 ? (uint32_t)x->transit : (uint32_t)x->sent_count;
	return 1;
}

static int s_send_message(void *ud, int target, const void *data, uint32_t size)
{
	Storm *x = st(ud);
	(void)data;
	if (seat.ng && size > seat.ng->max_message) bad(x, "a message past the maximum message", (long)size, (long)seat.ng->max_message);
	if (target < -1 || target >= G_PLAYERS) bad(x, "a message to no player", target, 0);
	if (fails(x, F_SEND_MESSAGE)) return 0;
	x->last.messages_sent++;
	return 1;
}

static int s_receive_message(void *ud, int *sender, const uint8_t **data, uint32_t *size)
{
	Storm *x = st(ud);
	if (fails(x, F_RECEIVE_MESSAGE)) return 0;
	if (!x->message_count) return refuse(x, KP_SNET_ERROR_NO_DATA);
	x->current_message = x->messages[x->message_head];
	x->message_head = (x->message_head + 1) % G_MESSAGES;
	x->message_count--;
	x->last.messages_taken++;
	*sender = x->current_message.sender;
	*data = x->current_message.data;
	*size = x->current_message.size;
	return 1;
}

static int s_game_info(void *ud, int type, void *buf, uint32_t size, uint32_t *written)
{
	(void)type, (void)buf, (void)size, (void)written;
	return refuse(st(ud), KP_SNET_ERROR_PARAMETER);
}

static int s_set_mode(void *ud, uint32_t mode, int make_private)
{
	(void)ud, (void)mode, (void)make_private;
	return 1;
}

static int s_player_name(void *ud, int id, char *buf, uint32_t size)
{
	if (id < 0 || id > PEER || size == 0) return refuse(st(ud), KP_SNET_ERROR_BAD_PLAYER);
	snprintf(buf, size, id == 0 ? "Fuzz" : "Peer");
	return 1;
}

static int s_caps(void *ud, KpSnetCaps *caps)
{
	if (fails(st(ud), F_CAPS)) return 0;
	*caps = KP_SNET_UDP_CAPS;
	return 1;
}

static int s_drop(void *ud, int id, uint32_t reason)
{
	(void)id, (void)reason;
	st(ud)->last.drops++;
	return 1;
}

static int s_disconnect(void *ud, uint32_t reason)
{
	(void)ud, (void)reason;
	return 1;
}

static uint32_t s_error(void *ud) { return st(ud)->error; }

static KpNetCalls calls(Storm *x)
{
	KpNetCalls c;
	memset(&c, 0, sizeof c);
	c.create_game = s_create;
	c.join_game = s_join;
	c.leave_game = s_leave;
	c.enum_games = s_enum;
	c.send_turn = s_send_turn;
	c.receive_turns = s_receive_turns;
	c.turns_in_transit = s_in_transit;
	c.send_message = s_send_message;
	c.receive_message = s_receive_message;
	c.game_info = s_game_info;
	c.set_game_mode = s_set_mode;
	c.player_name = s_player_name;
	c.provider_caps = s_caps;
	c.drop_player = s_drop;
	c.disconnect_all = s_disconnect;
	c.error = s_error;
	c.ud = x;
	return c;
}

/* --- the game's hooks --- */

static uint32_t h_now(void *ud) { return st(ud)->clock; }

static void h_error(void *ud, int index, const char *detail, int fatal, int in_game)
{
	(void)index, (void)detail, (void)in_game;
	if (fatal) st(ud)->fatal = 1;
}

/* --- the seat --- */

void kp_fuzz_game_free(void)
{
	if (seat.s) {
		kp_session_free(seat.s);
		free(seat.s);
	}
	if (seat.ng) {
		kp_netgame_free(seat.ng);
		free(seat.ng);
	}
	free(seat.storm);
	free(seat.base_storm);
	free(seat.snap);
	memset(&seat, 0, sizeof seat);
}

/* The host's lobby as its chat room leaves it to the start: the seat in slot 0, the peer in slot PEER, both humans */
static int seat_lobby(KpNetGame *ng, Storm *x, char *why, size_t size)
{
	KpNetGameHooks hooks;
	KpNetGameConfig config;
	KpNetCalls c = calls(x);
	int k;
	memset(&hooks, 0, sizeof hooks);
	hooks.now = h_now;
	hooks.error = h_error;
	hooks.ud = x;
	memset(&config, 0, sizeof config);
	snprintf(config.player_name, sizeof config.player_name, "Fuzz");
	if (kp_netgame_init(ng, &c, &hooks, &config) != 0) return failed(why, size, "the network game: no caps");
	snprintf(ng->header.name, sizeof ng->header.name, SEAT_GAME);
	ng->header.max_players = PEER + 1;
	ng->header.speed = SEAT_SPEED;
	memcpy(ng->header.template_record, KP_TEMPLATE_UMS, KP_TEMPLATE_SIZE);
	ng->host = 1;
	ng->seed = SEAT_SEED;
	ng->local_storm = ng->local_slot = ng->local_index = 0;
	for (k = 0; k <= PEER; k++) {
		KpNetSlot *s = &ng->slots[k];
		s->storm = k;
		s->type = KP_CTRL_HUMAN;
		snprintf(s->name, sizeof s->name, k ? "Peer" : "Fuzz");
		ng->storm_slot[k] = (uint8_t)k;
		ng->players[k].state = 1;
	}
	return 0;
}

int kp_fuzz_game_init(const KpData *data, char *why, size_t why_size)
{
	KpSessionStart start;
	KpMission mission;
	KpError err;
	int k, r = 0;
	if (seat.ready) return 0;
	seat.s = (KpSession *)calloc(1, sizeof *seat.s);
	seat.ng = (KpNetGame *)calloc(1, sizeof *seat.ng);
	seat.storm = (Storm *)calloc(1, sizeof *seat.storm);
	seat.base_storm = (Storm *)calloc(1, sizeof *seat.base_storm);
	if (!seat.s || !seat.ng || !seat.storm || !seat.base_storm) {
		kp_fuzz_game_free();
		return failed(why, why_size, "out of memory");
	}
	seat.storm->echo = 1;
	seat.storm->transit = -1;
	if (seat_lobby(seat.ng, seat.storm, why, why_size) != 0) {
		kp_fuzz_game_free();
		return -1;
	}
	if (kp_netgame_game_caps(seat.ng) != 0) {
		kp_fuzz_game_free();
		return failed(why, why_size, "the provider's caps");
	}
	if (!kp_oracle_mission(ROOT, SEAT_MAP, &mission, &err)) {
		kp_fuzz_game_free();
		return failed(why, why_size, "%s: %s", SEAT_MAP, err.msg);
	}
	if (kp_session_load_from(seat.s, &data->shared, &data->dat, &mission, SEAT_MAP, &data->files) != 0) {
		failed(why, why_size, "%s: %s", SEAT_MAP, seat.s->error);
		kp_fuzz_game_free();
		return -1;
	}
	kp_session_start_defaults(&start);
	start.seed = seat.ng->seed;
	start.speed = seat.ng->header.speed + 1;
	start.has_setup = 1;
	kp_netgame_game_setup(seat.ng, &start.setup);
	start.local_storm = seat.ng->local_storm;
	start.net = seat.ng;
	if (kp_session_start(seat.s, &start) != 0) {
		failed(why, why_size, "%s: %s", SEAT_MAP, seat.s->error);
		kp_fuzz_game_free();
		return -1;
	}
	for (k = 0; k < BEGIN_TRIES && r == 0; k++) r = kp_session_net_begin(seat.s);
	if (r != 1) {
		failed(why, why_size, "the first turn: %s", r < 0 ? seat.s->error : "not there");
		kp_fuzz_game_free();
		return -1;
	}
	seat.snap_size = kp_snapshot_size(seat.s->g);
	if (!(seat.snap = (uint8_t *)malloc(seat.snap_size)) || kp_snapshot(seat.s->g, seat.snap, seat.snap_size, &err) != 0) {
		kp_fuzz_game_free();
		return failed(why, why_size, "no snapshot");
	}
	seat.base_session = *seat.s;
	seat.base_game = *seat.ng;
	*seat.base_storm = *seat.storm;
	memset(&seat.base_storm->last, 0, sizeof seat.base_storm->last);
	seat.ready = 1;
	return 0;
}

/* The seat back at its snapshot: the game's state, the session's and the network game's (their owned lists kept),
 * Storm's with the turns in transit sent before it, the echo as the flags give it */
static int seat_reset(unsigned flags, char *why, size_t size)
{
	KpSession *s = seat.s;
	KpNetGame *ng = seat.ng;
	KpCommand *turn_list = s->turns.list, *game_list = ng->list;
	int turn_cap = s->turns.list_cap, game_cap = ng->list_cap;
	KpEvent e;
	KpError err;
	*s = seat.base_session;
	s->turns.list = turn_list;
	s->turns.list_cap = turn_cap;
	*ng = seat.base_game;
	ng->list = game_list;
	ng->list_cap = game_cap;
	if (kp_restore(s->g, seat.snap, seat.snap_size, &err) != 0) return failed(why, size, "the snapshot's restore: %s", err.msg);
	while (kp_event_take(s->g, &e)) {
	}
	*seat.storm = *seat.base_storm;
	seat.storm->echo = (flags & G_ECHO) != 0;
	return 0;
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

/* A SNetReceiveTurns result {u8 slots, each {u32 status, u16 n, n bytes}} queued */
static int read_turns(Storm *x, Reader *r, unsigned op)
{
	Result t;
	unsigned slots, k;
	memset(&t, 0, sizeof t);
	if (!get8(r, &slots)) return 0;
	for (k = 0; k < G_PLAYERS; k++) {
		const uint8_t *p;
		unsigned n;
		if (!(slots >> k & 1)) continue;
		if (!get32(r, &t.status[k]) || !get16(r, &n) || !get(r, n, &p)) return 0;
		t.size[k] = (uint16_t)(n < G_BYTES ? n : G_BYTES);
		memcpy(t.data[k], p, t.size[k]);
	}
	t.fails = (op & G_TURNS_FAIL) != 0;
	if (x->result_count < G_TURN_RESULTS) x->results[(x->result_head + x->result_count++) % G_TURN_RESULTS] = t;
	return 1;
}

static int read_message(Storm *x, Reader *r)
{
	Message m;
	const uint8_t *p;
	unsigned sender, n;
	if (!get8(r, &sender) || !get16(r, &n) || !get(r, n, &p)) return 0;
	m.sender = (int)sender;
	m.size = (uint16_t)(n < G_BYTES ? n : G_BYTES);
	memcpy(m.data, p, m.size);
	if (x->message_count < G_MESSAGES) x->messages[(x->message_head + x->message_count++) % G_MESSAGES] = m;
	return 1;
}

/* The seat's state within its bounds, its game still running */
static int check(char *why, size_t size)
{
	const KpNetGame *ng = seat.ng;
	if (seat.storm->bad[0]) return failed(why, size, "%s", seat.storm->bad);
	if (seat.s->failed) return failed(why, size, "the network game stopped: %s", seat.s->error);
	if (ng->out_size > ng->max_message || ng->out_size > KP_NETGAME_TURN_MAX)
		return failed(why, size, "the outgoing turn past its bound (%u)", (unsigned)ng->out_size);
	if (ng->list_count < 0 || ng->list_count > ng->list_cap)
		return failed(why, size, "the turn's list past its room (%d of %d)", ng->list_count, ng->list_cap);
	return 0;
}

int kp_fuzz_game_run(const uint8_t *d, size_t size, char *why, size_t why_size)
{
	Reader r;
	unsigned flags, op;
	int frames = 0;
	if (!seat.ready) return 0;
	r.d = d;
	r.size = size;
	r.at = 0;
	if (!get8(&r, &flags)) return 0;
	if (seat_reset(flags, why, why_size) != 0) return -1;
	while (!seat.storm->fatal && get8(&r, &op)) {
		Storm *x = seat.storm;
		unsigned a, b;
		const uint8_t *p;
		switch (op & 7) {
		case G_FRAME: {
			int k;
			for (k = 0; k <= (int)(op >> 3) && frames < G_FRAMES && !x->fatal; k++, frames++) {
				kp_session_net_messages(seat.s);
				if (seat.s->failed) break;
				x->last.frames += kp_session_step(seat.s, NULL) != 0;
				if (seat.s->failed) break;
			}
			break;
		}
		case G_TURNS:
			if (!read_turns(x, &r, op)) return check(why, why_size);
			break;
		case G_MESSAGE:
			if (!read_message(x, &r)) return check(why, why_size);
			break;
		case G_TRANSIT:
			if (!get8(&r, &a)) return check(why, why_size);
			x->transit = (int)a;
			break;
		case G_COMMAND:
			if (!get8(&r, &a) || !get(&r, a, &p)) return check(why, why_size);
			(void)kp_netgame_command(seat.ng, p, a);
			break;
		case G_CHAT: {
			char text[G_TEXT];
			if (!get16(&r, &b) || !get8(&r, &a) || !get(&r, a, &p)) return check(why, why_size);
			memcpy(text, p, a < sizeof text ? a : sizeof text - 1);
			text[a < sizeof text ? a : sizeof text - 1] = 0;
			kp_netgame_game_chat(seat.ng, text, (uint16_t)b);
			break;
		}
		case G_FAIL:
			if (!get8(&r, &a)) return check(why, why_size);
			x->fail |= a;
			break;
		case G_CLOCK:
			if (!get16(&r, &a)) return check(why, why_size);
			x->clock += a;
			if (op & G_CLOCK_DROP) (void)kp_netgame_timeout_drop(seat.ng);
			break;
		default: break;
		}
		if (check(why, why_size) != 0) return -1;
	}
	return check(why, why_size);
}

const KpFuzzGameLast *kp_fuzz_game_last(void)
{
	static KpFuzzGameLast none;
	if (!seat.storm) return &none;
	seat.storm->last.fatal = seat.storm->fatal;
	return &seat.storm->last;
}

/* --- seeds --- */

typedef struct {
	uint8_t b[512];
	size_t n;
} Seed;

static void s8(Seed *s, unsigned v)
{
	if (s->n < sizeof s->b) s->b[s->n++] = (uint8_t)v;
}

static void s16(Seed *s, unsigned v)
{
	s8(s, v & 0xff);
	s8(s, v >> 8 & 0xff);
}

static void s32(Seed *s, uint32_t v)
{
	s16(s, v & 0xffff);
	s16(s, v >> 16);
}

static void sbytes(Seed *s, const void *p, size_t n)
{
	size_t k;
	for (k = 0; k < n; k++) s8(s, ((const uint8_t *)p)[k]);
}

static void frames(Seed *s, int n)
{
	while (n > 0) {
		int k = n > 32 ? 32 : n;
		s8(s, G_FRAME | (unsigned)(k - 1) << 3);
		n -= k;
	}
}

/* An in-game message {type, an unwritten byte, the text and its NUL} from a sender */
static void message(Seed *s, int sender, int type, const char *text)
{
	size_t n = strlen(text) + 1;
	s8(s, G_MESSAGE);
	s8(s, (unsigned)sender);
	s16(s, (unsigned)(n + 2));
	s8(s, (unsigned)type);
	s8(s, 0);
	sbytes(s, text, n);
}

static void chat(Seed *s, unsigned recipients, const char *text)
{
	s8(s, G_CHAT);
	s16(s, recipients);
	s8(s, (unsigned)strlen(text));
	sbytes(s, text, strlen(text));
}

int kp_fuzz_game_seeds(KpFuzzGameSeed fn, void *ud)
{
	static const uint8_t STOP[2] = { 0x1a, 0 }, SELECT_NONE[2] = { 0x09, 0 };
	Seed s;
	int count = 0, k;
	/* the echo's turns, frame after frame */
	memset(&s, 0, sizeof s);
	s8(&s, G_ECHO);
	frames(&s, 96);
	fn(ud, "echo", s.b, s.n);
	count++;
	/* the peer's chat line, its timed line, a refused type, then the seat's line to all and a whisper */
	memset(&s, 0, sizeof s);
	s8(&s, G_ECHO);
	frames(&s, 8);
	message(&s, PEER, 0, "gg");
	message(&s, PEER, 1, "hello");
	message(&s, PEER, 2, "refused");
	frames(&s, 8);
	chat(&s, 0xff, "hello");
	chat(&s, 0xff, "/whisper Peer hi");
	frames(&s, 16);
	fn(ud, "chat", s.b, s.n);
	count++;
	/* the seat's commands in its turns */
	memset(&s, 0, sizeof s);
	s8(&s, G_ECHO);
	frames(&s, 8);
	s8(&s, G_COMMAND);
	s8(&s, sizeof SELECT_NONE);
	sbytes(&s, SELECT_NONE, sizeof SELECT_NONE);
	s8(&s, G_COMMAND);
	s8(&s, sizeof STOP);
	sbytes(&s, STOP, sizeof STOP);
	frames(&s, 32);
	fn(ud, "commands", s.b, s.n);
	count++;
	/* the peer present but not responding (no echo: its last status stays), the seat waiting, timeout.bin opened and
	 * counted down, then its Drop Players: the peer dropped */
	memset(&s, 0, sizeof s);
	s8(&s, 0);
	s8(&s, G_TURNS | G_TURNS_FAIL);
	s8(&s, 1u | 1u << PEER);
	s32(&s, TURN_PRESENT);
	s16(&s, 0);
	s32(&s, KP_SNET_PRESENT);
	s16(&s, 0);
	frames(&s, 4);
	s8(&s, G_CLOCK);
	s16(&s, LAG_MS);
	frames(&s, 1);
	for (k = 0; k < TIMEOUT_TICKS; k++) {
		s8(&s, G_CLOCK);
		s16(&s, TICK_MS);
		frames(&s, 1);
	}
	s8(&s, G_CLOCK | G_CLOCK_DROP);
	s16(&s, 0);
	frames(&s, 4);
	fn(ud, "leave", s.b, s.n);
	count++;
	/* the turns' receive failing past no data: the fatal report ends it */
	memset(&s, 0, sizeof s);
	s8(&s, G_ECHO);
	frames(&s, 8);
	s8(&s, G_FAIL);
	s8(&s, F_RECEIVE_TURNS);
	frames(&s, 16);
	fn(ud, "receive-fails", s.b, s.n);
	count++;
	/* the turn's send failing: the fatal report ends it */
	memset(&s, 0, sizeof s);
	s8(&s, G_ECHO);
	frames(&s, 8);
	s8(&s, G_FAIL);
	s8(&s, F_SEND_TURN);
	frames(&s, 16);
	fn(ud, "send-fails", s.b, s.n);
	count++;
	return count;
}
