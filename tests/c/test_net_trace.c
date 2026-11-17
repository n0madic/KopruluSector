/* test_net_trace: kp_snet's replay of the network oracle's recordings (phase N, stage N2; tools/net_check.py writes
 * tests/golden/oracle/net/ from koprulu-private/emu/net-n2). For each player of each recorded game, Koprulu's SNet
 * and UDP provider are given what the original's were, at the times they were: the datagrams that reached it, and
 * StarCraft's calls of the SNet exports, the calls its event handlers made inside them included; its pump runs where
 * Storm's network thread made a pass (the emulator's scheduler decided when), signalled or not as that pass was, and
 * SNetJoinGame's passes take what the original's took. The clock is the emulator's: a record sets it to its time, and
 * each GetTickCount charges TIME_QUERY_COST_US first; where the emulator preempted the original's pass between two of
 * its reads, the replay passes that time too and gives the arrivals of the gap (io_now). What it sends must be the
 * original's datagrams byte for byte, in their order, each at most WINDOW_MS from the original's time (the provider's
 * own threads answer the game list when the scheduler runs them); what the calls return, the waits the passes compute
 * and the events the calls dispatch must be the original's. */
#include "kp_test.h"

#include <stdarg.h>

#include "kp_json.h"
#include "kp_net.h"
#include "kp_sha256.h"
#include "kp_snet.h"
#include "kp_snp_udp.h"

#define INDEX "tests/golden/oracle/net/index.json"
#define GOLDEN_VERSION 5 /* tools/net_check.py's */
#define GOLDEN_DIR "tests/golden/oracle/net/"
#define TICK_BASE 3600000u      /* the emulator's GetTickCount at virtual time 0 (hle/kernel32.py) */
#define TIME_QUERY_COST_US 50 /* what the emulator's GetTickCount charges the clock before it reads it */
#define WINDOW_MS 50
#define REPORTED 8          /* differences logged per player */
#define DATA_MAX 0x10000
#define SLOTS 12
#define FRAMES 8            /* calls inside calls (an event handler's) */
#define READS 256           /* a pass's clock reads compared with the original's */

/* A call being made: what happened inside it in the original, and the next of those the replay comes to. */
typedef struct {
	const KpJson *items;
	int next, len;
} Frame;

typedef struct {
	KpT *T;
	const char *who;
	const KpJson *calls, *arrivals, *sends;
	int ncalls, narrivals, nsends, nevents;
	int arrival;   /* the next arrival */
	int step;      /* the records run (calls, passes, events), which the differences name */
	int64_t now;   /* the replay's virtual clock (microseconds; GetTickCount is TICK_BASE + now / 1000) */
	KpSnpUdp snp;
	int snp_ready;
	KpSnet *net;
	int sent, dispatched; /* datagrams sent and events dispatched so far */
	int closing;          /* the teardown after the recording's end: its sends are not compared */
	int reads;            /* the clock's reads in the pass being made */
	int64_t read_at[READS];
	const KpJson *pass_clock; /* the original's reads in that pass, and its start */
	int64_t pass_t;
	int foreign; /* the provider's threads run in a pass's preemption: their reads are not the pass's */
	Frame frames[FRAMES];
	int depth;
	long differences;
	uint8_t buf[DATA_MAX];
} Replay;

static uint32_t tick(int64_t t_us) { return TICK_BASE + (uint32_t)(t_us / 1000); }

static int differ(Replay *r, const char *fmt, ...) KP_T_PRINTF(2, 3);
static int differ(Replay *r, const char *fmt, ...)
{
	char msg[512];
	va_list ap;
	if (r->differences++ >= REPORTED) return 0;
	va_start(ap, fmt);
	vsnprintf(msg, sizeof msg, fmt, ap);
	va_end(ap);
	kp_t_log(r->T, "%s at %lld ms, step %d: %s", r->who, (long long)(r->now / 1000), r->step, msg);
	return 0;
}

/* Hex text into out (at most cap bytes); the byte count, -1 when it is not hex or too long. */
static long unhex(const char *s, uint8_t *out, size_t cap)
{
	size_t n = strlen(s);
	if (n % 2 || n / 2 > cap || kp_hex_decode(s, out, n / 2) != 0) return -1;
	return (long)(n / 2);
}

static void hex(const uint8_t *p, size_t n, char *out, size_t size)
{
	size_t k;
	if (!size) return;
	out[0] = 0;
	for (k = 0; k < n && 2 * k + 2 < size; k++) snprintf(out + 2 * k, 3, "%02x", p[k]);
}

static int same_hex(const uint8_t *p, size_t n, const char *expected)
{
	size_t k;
	if (strlen(expected) != 2 * n) return 0;
	for (k = 0; k < n; k++) {
		uint8_t v;
		if (kp_hex_decode(expected + 2 * k, &v, 1) != 0 || v != p[k]) return 0;
	}
	return 1;
}

static KpNetAddr parse_addr(const char *s)
{
	unsigned a = 0, b = 0, c = 0, d = 0, port = 0;
	if (sscanf(s, "%u.%u.%u.%u:%u", &a, &b, &c, &d, &port) != 5) port = 0;
	return kp_net_addr(a << 24 | b << 16 | c << 8 | d, (uint16_t)port);
}

/* --- the provider's I/O: the sends compared with the original's --- */

static void io_send(void *ud, int socket, const KpNetAddr *to, const uint8_t *data, size_t size)
{
	Replay *r = (Replay *)ud;
	char addr[32], got[96];
	const KpJson *e;
	int k;
	if (r->closing) return;
	e = kp_json_at(r->sends, r->sent);
	k = r->sent++;
	kp_net_addr_text(to, addr, sizeof addr);
	hex(data, size, got, sizeof got);
	if (!e) {
		differ(r, "send %d to %s (%s) beyond the original's %d", k, addr, got, r->nsends);
		return;
	}
	if ((int)kp_json_field_int(e, "socket", -1) != socket || strcmp(kp_json_field_str(e, "to", ""), addr) != 0 ||
		!same_hex(data, size, kp_json_field_str(e, "data", "")))
		differ(r, "send %d: socket %d to %s %s, the original's socket %d to %s %.80s", k, socket, addr, got,
			(int)kp_json_field_int(e, "socket", -1), kp_json_field_str(e, "to", ""), kp_json_field_str(e, "data", ""));
	else {
		int32_t d = (int32_t)(tick(r->now) - tick(kp_json_field_int(e, "t", 0)));
		if (d > WINDOW_MS || d < -WINDOW_MS) differ(r, "send %d (%s to %s) %d ms from the original's", k, got, addr, d);
	}
}

static void deliver(Replay *r);
static void clock_to(Replay *r, int64_t t_us);

/* GetTickCount as the emulator serves it. In a pass, where the original's was preempted before the same read (the
 * emulator's scheduler ran its other threads: the provider's took the game-list datagrams that had arrived, and their
 * reads and the game thread's moved the clock), the arrivals before the preemption's end are given and its time
 * passes first. */
static uint32_t io_now(void *ud)
{
	Replay *r = (Replay *)ud;
	if (r->pass_clock && !r->foreign && r->reads < kp_json_len(r->pass_clock)) {
		int64_t prev = r->reads ? kp_json_int(kp_json_at(kp_json_at(r->pass_clock, r->reads - 1), 1), 0) : r->pass_t;
		int64_t gap = kp_json_int(kp_json_at(kp_json_at(r->pass_clock, r->reads), 1), 0) - TIME_QUERY_COST_US - prev;
		if (gap > 0) {
			int64_t until = r->now + gap;
			r->foreign = 1;
			/* what arrives as the pass resumes waits for the provider's next turn */
			while (r->arrival < r->narrivals && kp_json_field_int(kp_json_at(r->arrivals, r->arrival), "t", 0) < until)
				deliver(r);
			r->foreign = 0;
			clock_to(r, until);
		}
	}
	r->now += TIME_QUERY_COST_US;
	if (!r->foreign) {
		if (r->reads < READS) r->read_at[r->reads] = r->now;
		r->reads++;
	}
	return tick(r->now);
}

/* --- the datagrams that arrive --- */

/* The clock moved to t_us (never back). */
static void clock_to(Replay *r, int64_t t_us)
{
	if (t_us > r->now) r->now = t_us;
}

/* Arrival k given to the provider (its game-list socket's answers go out at once). */
static void give(Replay *r, int k)
{
	const KpJson *a = kp_json_at(r->arrivals, k);
	KpNetAddr from = parse_addr(kp_json_field_str(a, "from", ""));
	long n = unhex(kp_json_field_str(a, "data", ""), r->buf, sizeof r->buf);
	kp_t_ok(r->T, n >= 0, "%s: arrival %d's data", r->who, k);
	if (r->snp_ready) kp_snp_udp_input(&r->snp, (int)kp_json_field_int(a, "socket", 0), &from, r->buf, (size_t)n);
}

/* The next arrival at its time; one of port 6112 only when a pass takes it. */
static void deliver(Replay *r)
{
	const KpJson *a = kp_json_at(r->arrivals, r->arrival);
	clock_to(r, kp_json_field_int(a, "t", 0));
	if (!kp_json_get(a, "pass")) give(r, r->arrival);
	r->arrival++;
}

/* The arrivals until t_us, and the clock there. */
static void advance(Replay *r, int64_t t_us)
{
	while (r->arrival < r->narrivals && kp_json_field_int(kp_json_at(r->arrivals, r->arrival), "t", 0) <= t_us)
		deliver(r);
	clock_to(r, t_us);
}

/* A pass's datagrams queued in the provider (its ReceivePackets takes them). The provider's threads queued them
 * before, on clock reads of their own: the pass's are not charged with them. */
static void give_taken(Replay *r, const KpJson *pass)
{
	const KpJson *take = kp_json_get(pass, "take");
	int64_t now = r->now;
	int k;
	for (k = 0; k < kp_json_len(take); k++) give(r, (int)kp_json_int(kp_json_at(take, k), -1));
	r->now = now;
}

static int is(const KpJson *x, const char *fn) { return x && strcmp(kp_json_field_str(x, "fn", ""), fn) == 0; }

/* The next record inside the call being made, NULL at its end or outside the calls. */
static const KpJson *peek(const Replay *r)
{
	const Frame *f = r->depth ? &r->frames[r->depth - 1] : NULL;
	return f && f->next < f->len ? kp_json_at(f->items, f->next) : NULL;
}

static const KpJson *take_next(Replay *r)
{
	const KpJson *x = peek(r);
	if (x) {
		r->frames[r->depth - 1].next++;
		r->step++;
	}
	return x;
}

static void run(Replay *r, const KpJson *x);

/* SNetJoinGame's waits: its next pass in the original (after a Sleep(10)) is the clock and the datagrams queued
 * meanwhile; a first pass before it (the next PumpUntilControl's, which no sleep precedes) the replay has made already,
 * with nothing queued for it. */
static void hook_sleep(void *ud, uint32_t ms)
{
	Replay *r = (Replay *)ud;
	const KpJson *x;
	while (is(peek(r), "join_pass") && kp_json_field_int(peek(r), "first", 0)) {
		x = take_next(r);
		if (kp_json_len(kp_json_get(x, "take")))
			differ(r, "SNetJoinGame's first pass at %lld us took datagrams the replay cannot queue for it",
				(long long)kp_json_field_int(x, "t", 0));
	}
	if (is(peek(r), "join_pass")) {
		x = take_next(r);
		advance(r, kp_json_field_int(x, "t", 0));
		give_taken(r, x);
		return;
	}
	advance(r, r->now + (int64_t)ms * 1000); /* a wait the original did not make (the differences say so) */
}

/* An event: the original's next record inside the call being made, then the calls its handler made. */
static void hook_event(void *ud, const KpSnetEvent *e)
{
	Replay *r = (Replay *)ud;
	const KpJson *x;
	char got[96];
	int k = r->dispatched++;
	hex(e->data, e->size, got, sizeof got);
	while (is(peek(r), "pump")) run(r, take_next(r));
	if (!is(peek(r), "event")) {
		differ(r, "event %d (%u, player %d, %s) beyond the original's", k, e->id, e->player, got);
		return;
	}
	x = take_next(r);
	if ((uint32_t)kp_json_field_int(x, "id", 0) != e->id || (int32_t)kp_json_field_int(x, "player", -9) != e->player ||
		!same_hex(e->data, e->size, kp_json_field_str(x, "data", "")))
		differ(r, "event %d: %u player %d %s, the original's %d player %d %.80s", k, e->id, e->player, got,
			(int)kp_json_field_int(x, "id", 0), (int)kp_json_field_int(x, "player", -9), kp_json_field_str(x, "data", ""));
	while ((x = peek(r)) && !is(x, "event") && !is(x, "join_pass")) run(r, take_next(r));
}

/* --- the calls --- */

static uint32_t arg(const KpJson *c, int k) { return (uint32_t)kp_json_int(kp_json_at(kp_json_get(c, "args"), k), 0); }

static const char *string_arg(const KpJson *c, int k)
{
	char key[8];
	const KpJson *v;
	snprintf(key, sizeof key, "%d", k);
	v = kp_json_get(kp_json_get(c, "strings"), key);
	return kp_json_is(v, KP_JSON_STRING) ? kp_json_str(v, "") : NULL;
}

static int count_game(void *ud, const KpSnetGame *g)
{
	(void)g;
	(*(int *)ud)++;
	return 1;
}

static void start_provider(Replay *r, const KpJson *c)
{
	uint8_t program[0x80];
	long n = unhex(kp_json_field_str(c, "program", ""), program, sizeof program);
	KpNetIo io;
	KpSnetHooks h;
	uint32_t max_players;
	kp_t_ok(r->T, n >= 0x1c, "%s: the program data (%ld bytes)", r->who, n);
	if (r->net) {
		kp_snet_free(r->net);
		kp_snp_udp_free(&r->snp);
	}
	io.send = io_send;
	io.now = io_now;
	io.ud = r;
	max_players = kp_net_get32(program + 0x18);
	kp_snp_udp_init(&r->snp, &io, kp_net_get32(program + 0xc), kp_net_get32(program + 0x10),
		max_players > 256 ? 256 : max_players);
	r->snp_ready = 1;
	memset(&h, 0, sizeof h);
	h.now = io_now;
	h.sleep = hook_sleep;
	h.event = hook_event;
	h.ud = r;
	r->net = kp_snet_new(&r->snp, &h, kp_net_get32(program + 0xc), kp_net_get32(program + 0x10));
	kp_t_ok(r->T, r->net != NULL, "out of memory");
}

static void stop_provider(Replay *r)
{
	if (!r->net) return;
	kp_snet_free(r->net);
	kp_snp_udp_free(&r->snp);
	r->net = NULL;
	r->snp_ready = 0;
}

/* ReceiveTurns's outputs against the original's players [{id, status, size, data}]. */
static void check_turns(Replay *r, const KpJson *c, const uint8_t **data, const uint32_t *sizes, const uint32_t *status,
	int count)
{
	const KpJson *players = kp_json_get(c, "players");
	int k;
	for (k = 0; k < count && k < kp_json_len(players); k++) {
		const KpJson *p = kp_json_at(players, k);
		uint32_t word = (uint32_t)strtoul(kp_json_field_str(p, "status", "0"), NULL, 16);
		const char *d = kp_json_field_str(p, "data", "");
		if (status[k] != word || sizes[k] != (uint32_t)kp_json_field_int(p, "size", 0) ||
			(*d && (!data[k] || !same_hex(data[k], sizes[k], d))))
			differ(r, "SNetReceiveTurns: slot %d status %#x size %u, the original's %#x size %u %.40s",
				k, status[k], sizes[k], word, (unsigned)kp_json_field_int(p, "size", 0), d);
	}
}

/* A pass of Storm's network thread: the datagrams the provider had queued for it, the packets read when it was
 * signalled, the timers run; the wait it returns. */
static void pass(Replay *r, const KpJson *c)
{
	uint32_t wait, expected = (uint32_t)kp_json_field_int(c, "wait", 0);
	if (!r->net) {
		differ(r, "a pass of the network thread without a provider");
		return;
	}
	const KpJson *clock = kp_json_get(c, "clock");
	int k;
	advance(r, kp_json_field_int(c, "t", 0));
	give_taken(r, c);
	r->reads = 0;
	r->pass_clock = clock;
	r->pass_t = kp_json_field_int(c, "t", 0);
	wait = kp_snet_pump(r->net, (int)kp_json_field_int(c, "signalled", 0));
	r->pass_clock = NULL;
	kp_snet_take_signal(r->net); /* the passes are the original's: a send's signal is in theirs */
	/* the clock's reads: where they differ from the original's (its caller there), the millisecond values differ
	 * once one crosses a millisecond the other does not */
	for (k = 0; k < r->reads && k < READS && k < kp_json_len(clock); k++)
		if (r->read_at[k] != kp_json_int(kp_json_at(kp_json_at(clock, k), 1), 0)) break;
	if (wait != expected || r->reads != kp_json_len(clock) || (k < r->reads && k < READS)) {
		const KpJson *x = kp_json_at(clock, k);
		differ(r, "the network thread's pass waits %#x ms, the original's %#x; %d reads of the clock, the original's "
			"%d, from read %d on (%lld us, the original's %s at %lld us)", wait, expected, r->reads, kp_json_len(clock),
			k, k < r->reads && k < READS ? (long long)r->read_at[k] : -1LL, kp_json_str(kp_json_at(x, 0), "-"),
			(long long)kp_json_int(kp_json_at(x, 1), -1));
	}
}

static void make_call(Replay *r, const KpJson *c)
{
	const char *fn = kp_json_field_str(c, "fn", "");
	int expected = kp_json_field_int(c, "result", 0) != 0, result = -1;
	KpSnet *s = r->net;
	if (strcmp(fn, "SNetInitializeProvider") == 0) {
		start_provider(r, c);
		result = 1;
	} else if (strcmp(fn, "SNetDestroy") == 0) {
		stop_provider(r);
		result = 1;
	} else if (!s) {
		differ(r, "%s without a provider", fn);
		return;
	} else if (strcmp(fn, "SNetGetProviderCaps") == 0) {
		KpSnetCaps caps;
		uint8_t raw[0x24];
		int k;
		memset(&caps, 0, sizeof caps);
		caps.size = sizeof caps;
		result = kp_snet_provider_caps(s, &caps);
		for (k = 0; k < 9; k++) kp_net_put32(raw + 4 * k, (&caps.size)[k]);
		if (result && !same_hex(raw, sizeof raw, kp_json_field_str(c, "caps", "")))
			differ(r, "the provider's caps differ from %s", kp_json_field_str(c, "caps", ""));
	} else if (strcmp(fn, "SNetEnumGamesEx") == 0) {
		uint32_t hint = 0;
		int games = 0;
		result = kp_snet_enum_games(s, count_game, &games, &hint);
	} else if (strcmp(fn, "SNetCreateLadderGame") == 0) {
		long n = unhex(kp_json_field_str(c, "template", ""), r->buf, sizeof r->buf);
		int id = -1;
		kp_t_ok(r->T, n >= 0, "%s: the template", r->who);
		result = kp_snet_create_game(s, string_arg(c, 0), string_arg(c, 1), string_arg(c, 2), arg(c, 3), arg(c, 4),
			arg(c, 5), r->buf, (uint32_t)n, arg(c, 8), string_arg(c, 9), string_arg(c, 10), &id);
		if (result && id != (int)kp_json_field_int(c, "player_id", -1))
			differ(r, "the created game's player id %d, the original's %d", id, (int)kp_json_field_int(c, "player_id", -1));
	} else if (strcmp(fn, "SNetJoinGame") == 0) {
		int id = -1;
		if (is(peek(r), "join_pass") && kp_json_field_int(peek(r), "first", 0)) {
			const KpJson *x = take_next(r); /* its first pass: what it takes is queued before the call */
			advance(r, kp_json_field_int(x, "t", 0));
			give_taken(r, x);
		}
		result = kp_snet_join_game(s, arg(c, 0), string_arg(c, 1), string_arg(c, 2), string_arg(c, 3),
			string_arg(c, 4), &id);
		if (result && id != (int)kp_json_field_int(c, "player_id", -1))
			differ(r, "the joined player id %d, the original's %d", id, (int)kp_json_field_int(c, "player_id", -1));
	} else if (strcmp(fn, "SNetLeaveGame") == 0) {
		result = kp_snet_leave_game(s, arg(c, 0));
	} else if (strcmp(fn, "SNetSendTurn") == 0 || strcmp(fn, "SNetSendMessage") == 0) {
		long n = unhex(kp_json_field_str(c, "given", ""), r->buf, sizeof r->buf);
		kp_t_ok(r->T, n >= 0, "%s: step %d's data", r->who, r->step);
		result = strcmp(fn, "SNetSendTurn") == 0 ? kp_snet_send_turn(s, n ? r->buf : NULL, (uint32_t)n)
			: kp_snet_send_message(s, (int)(int32_t)arg(c, 0), n ? r->buf : NULL, (uint32_t)n);
	} else if (strcmp(fn, "SNetReceiveTurns") == 0) {
		const uint8_t *data[SLOTS];
		uint32_t sizes[SLOTS], status[SLOTS];
		int first = (int)arg(c, 0), count = (int)arg(c, 1);
		kp_t_ok(r->T, count >= 0 && count <= SLOTS, "%s: SNetReceiveTurns of %d slots", r->who, count);
		memset((void *)data, 0, sizeof data);
		memset(sizes, 0, sizeof sizes);
		memset(status, 0, sizeof status);
		result = kp_snet_receive_turns(s, first, count, data, sizes, status);
		if (result && expected) check_turns(r, c, data, sizes, status, count);
		if (!result && !expected && kp_json_get(c, "status")) { /* the words StarCraft reads after a failure */
			const KpJson *words = kp_json_get(c, "status");
			int k;
			for (k = 0; k < count && k < kp_json_len(words); k++)
				if (status[k] != (uint32_t)strtoul(kp_json_str(kp_json_at(words, k), ""), NULL, 16)) break;
			if (k < count || kp_json_len(words) != count)
				differ(r, "SNetReceiveTurns failed with slot %d's status %#x, the original's %s", k,
					k < count ? status[k] : 0u, kp_json_str(kp_json_at(words, k), "-"));
		}
	} else if (strcmp(fn, "SNetReceiveMessage") == 0) {
		int sender = -1;
		const uint8_t *data = NULL;
		uint32_t size = 0;
		result = kp_snet_receive_message(s, &sender, &data, &size);
		if (result && expected &&
			(sender != (int)kp_json_field_int(c, "sender", -9) || !same_hex(data, size, kp_json_field_str(c, "data", ""))))
			differ(r, "SNetReceiveMessage: from %d (%u bytes), the original's from %d %.60s", sender,
				size, (int)kp_json_field_int(c, "sender", -9), kp_json_field_str(c, "data", ""));
	} else if (strcmp(fn, "SNetGetTurnsInTransit") == 0) {
		uint32_t n = 0;
		result = kp_snet_turns_in_transit(s, &n);
		if (result && expected && n != (uint32_t)kp_json_field_int(c, "in_transit", -1))
			differ(r, "%u turns in transit, the original's %d", n,
				(int)kp_json_field_int(c, "in_transit", -1));
	} else if (strcmp(fn, "SNetGetGameInfo") == 0) {
		uint32_t written = 0, size = arg(c, 2);
		if (size > sizeof r->buf) size = sizeof r->buf;
		result = kp_snet_game_info(s, (int)arg(c, 0), arg(c, 1) ? r->buf : NULL, size, &written);
		if (result && expected && arg(c, 1)) {
			uint32_t n = arg(c, 3) ? written : size;
			if (n > size) n = size;
			if (!same_hex(r->buf, n, kp_json_field_str(c, "info", "")))
				differ(r, "SNetGetGameInfo(%u): the original's %.60s", arg(c, 0),
					kp_json_field_str(c, "info", ""));
		}
	} else if (strcmp(fn, "SNetGetNumPlayers") == 0) {
		int v[3] = {-9, -9, -9}, k;
		const KpJson *num = kp_json_get(c, "num");
		result = kp_snet_num_players(s, &v[0], &v[1], &v[2]);
		for (k = 0; result && expected && k < 3; k++) {
			const KpJson *x = kp_json_at(num, k);
			if (kp_json_is(x, KP_JSON_NUMBER) && kp_json_int(x, -9) != v[k])
				differ(r, "SNetGetNumPlayers: output %d is %d, the original's %d", k, v[k],
					(int)kp_json_int(x, -9));
		}
	} else if (strcmp(fn, "SNetGetPlayerCaps") == 0) {
		KpSnetCaps caps;
		memset(&caps, 0, sizeof caps);
		caps.size = sizeof caps;
		result = kp_snet_player_caps(s, (int)(int32_t)arg(c, 0), &caps);
	} else if (strcmp(fn, "SNetGetPlayerName") == 0) {
		char name[KP_SNET_STRING + 1];
		uint32_t size = arg(c, 2) < sizeof name ? arg(c, 2) : (uint32_t)sizeof name;
		result = kp_snet_player_name(s, (int)(int32_t)arg(c, 0), name, size);
		if (result && expected && strcmp(name, kp_json_field_str(c, "name", "")) != 0)
			differ(r, "player %d's name \"%s\", the original's \"%s\"", (int)arg(c, 0), name,
				kp_json_field_str(c, "name", ""));
	} else if (strcmp(fn, "SNetSetGameMode") == 0) {
		result = kp_snet_set_game_mode(s, arg(c, 0), (int)arg(c, 1));
	} else if (strcmp(fn, "SNetDropPlayer") == 0) {
		result = kp_snet_drop_player(s, (int)(int32_t)arg(c, 0), arg(c, 1));
	} else if (strcmp(fn, "SNetDisconnectAll") == 0) {
		result = kp_snet_disconnect_all(s, arg(c, 0));
	} else {
		differ(r, "%s is not replayed", fn);
		return;
	}
	if ((result != 0) != expected) differ(r, "%s returned %d, the original %d", fn, result, expected);
}

/* A call at its time: the passes before it took Storm's lock, the call (its events run what happened inside it),
 * then whatever inside it the replay did not come to. */
static void call(Replay *r, const KpJson *c)
{
	Frame *f;
	advance(r, kp_json_field_int(c, "t", 0));
	if (r->depth == FRAMES) {
		differ(r, "%s inside %d calls", kp_json_field_str(c, "fn", ""), FRAMES);
		return;
	}
	f = &r->frames[r->depth++];
	f->items = kp_json_get(c, "inner");
	f->len = kp_json_len(f->items);
	f->next = 0;
	while (is(peek(r), "pump")) run(r, take_next(r));
	make_call(r, c);
	while (peek(r)) run(r, take_next(r));
	r->depth--;
}

static void run(Replay *r, const KpJson *x)
{
	if (is(x, "pump")) {
		pass(r, x);
	} else if (is(x, "event")) {
		differ(r, "the original's event %d (player %d, %.40s) was not dispatched", (int)kp_json_field_int(x, "id", 0),
			(int)kp_json_field_int(x, "player", -9), kp_json_field_str(x, "data", ""));
	} else if (is(x, "join_pass")) {
		advance(r, kp_json_field_int(x, "t", 0));
		differ(r, "a pass of SNetJoinGame the replay did not make");
	} else {
		call(r, x);
	}
}

static int count_events(const KpJson *items)
{
	int k, n = 0;
	for (k = 0; k < kp_json_len(items); k++) {
		const KpJson *x = kp_json_at(items, k);
		n += is(x, "event") + count_events(kp_json_get(x, "inner"));
	}
	return n;
}

/* --- the replay --- */

typedef struct {
	const KpJson *player;
	const char *game;
} Job;

static void replay_free(void *p)
{
	Replay *r = (Replay *)p;
	r->closing = 1;
	stop_provider(r);
	free(r);
}

static void replay_player(KpT *T, void *ud)
{
	const Job *job = (const Job *)ud;
	Replay *r = (Replay *)calloc(1, sizeof *r);
	char who[96];
	int k;
	kp_t_ok(T, r != NULL, "out of memory");
	kp_t_defer(T, replay_free, r);
	snprintf(who, sizeof who, "%s/%s", job->game, kp_json_field_str(job->player, "name", "?"));
	r->T = T;
	r->who = kp_t_strdup(T, who);
	r->calls = kp_json_get(job->player, "calls");
	r->arrivals = kp_json_get(job->player, "arrivals");
	r->sends = kp_json_get(job->player, "sends");
	r->ncalls = kp_json_len(r->calls);
	r->narrivals = kp_json_len(r->arrivals);
	r->nsends = kp_json_len(r->sends);
	r->nevents = count_events(r->calls);
	for (k = 0; k < r->ncalls; k++) {
		r->step++;
		run(r, kp_json_at(r->calls, k));
	}
	if (r->narrivals) advance(r, kp_json_field_int(kp_json_at(r->arrivals, r->narrivals - 1), "t", 0));
	r->closing = 1;
	if (r->sent < r->nsends)
		differ(r, "%d of the original's %d datagrams sent, the next %.80s", r->sent, r->nsends,
			kp_json_field_str(kp_json_at(r->sends, r->sent), "data", ""));
	if (r->dispatched < r->nevents)
		differ(r, "%d of the original's %d events dispatched", r->dispatched, r->nevents);
	kp_t_log(T, "%s: %d calls, %d arrivals, %d of %d sends, %d of %d events, %ld differences", r->who, r->ncalls,
		r->narrivals, r->sent, r->nsends, r->dispatched, r->nevents, r->differences);
	kp_t_ok(T, r->differences == 0, "%s: %ld differences from the original (the first ones logged above)", r->who,
		r->differences);
}

static void recordings_replay(KpT *T)
{
	const KpJson *index = kp_t_golden(T, INDEX), *games = kp_json_get(index, "games");
	int g, k, version, players = 0, failed = 0;
	char msg[512], first[512] = "";
	kp_t_ok(T, kp_json_field_int(index, "version", 0) == GOLDEN_VERSION,
		INDEX ": version %d, the replay's %d (tools/net_check.py writes it anew)", (int)kp_json_field_int(index, "version", 0),
		GOLDEN_VERSION);
	kp_t_ok(T, kp_json_len(games) > 0, "no games in " INDEX);
	for (g = 0; g < kp_json_len(games); g++) {
		char path[256];
		const char *name = kp_json_str(kp_json_at(games, g), "");
		const KpJson *golden, *list;
		snprintf(path, sizeof path, GOLDEN_DIR "%s.json", name);
		golden = kp_t_golden(T, path);
		version = (int)kp_json_field_int(golden, "version", 0);
		/* a game the index lists but an older net_check.py wrote */
		kp_t_ok(T, version == GOLDEN_VERSION, "%s: version %d, the replay's %d (tools/net_check.py writes it anew)",
			path, version, GOLDEN_VERSION);
		if (version != GOLDEN_VERSION) continue;
		list = kp_json_get(golden, "players");
		for (k = 0; k < kp_json_len(list); k++) {
			Job job;
			int mark = kp_t_mark(T);
			job.player = kp_json_at(list, k);
			job.game = name;
			players++;
			if (kp_t_pcall(T, replay_player, &job, msg, sizeof msg) != 0) {
				if (!failed++) snprintf(first, sizeof first, "%s", msg);
			}
			kp_t_release(T, mark);
		}
	}
	kp_t_ok(T, failed == 0, "%d of %d players differ; the first: %s", failed, players, first);
}

static const KpTest TESTS[] = {
	{ "recordings_replay", recordings_replay, 0 },
};

KP_SUITE(kp_suite_net_trace, "test_net_trace", TESTS);
