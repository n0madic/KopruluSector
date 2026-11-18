/* The network oracle's recordings as SNet (kp_oracle_net.h). */
#include "kp_oracle_net.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kp_net.h"
#include "kp_sha256.h"
#include "kp_snet.h"
#include "kp_snp_udp.h"

#define DATA_MAX 0x10000
#define CAPS_SIZE 0x24
#define FLAT_DEPTH 8 /* calls inside calls (an event handler's) */
#define PASS_MS 1    /* the milliseconds a pass of the original's screens spans after its first clock read */
#if defined(__GNUC__) || defined(__clang__)
#define PRINTF_LIKE(a, b) __attribute__((format(printf, a, b)))
#else
#define PRINTF_LIKE(a, b)
#endif

long kp_oracle_net_unhex(const char *s, uint8_t *out, size_t cap)
{
	size_t n = strlen(s);
	if (n % 2 || n / 2 > cap || kp_hex_decode(s, out, n / 2) != 0) return -1;
	return (long)(n / 2);
}

const char *kp_oracle_net_fn(const KpJson *c) { return kp_json_field_str(c, "fn", ""); }
int kp_oracle_net_is(const KpJson *c, const char *fn) { return c && strcmp(kp_oracle_net_fn(c), fn) == 0; }

uint32_t kp_oracle_net_arg(const KpJson *c, int k)
{
	return (uint32_t)kp_json_int(kp_json_at(kp_json_get(c, "args"), k), 0);
}

const char *kp_oracle_net_string(const KpJson *c, int k)
{
	char key[8];
	const KpJson *v;
	snprintf(key, sizeof key, "%d", k);
	v = kp_json_get(kp_json_get(c, "strings"), key);
	return v && kp_json_is(v, KP_JSON_STRING) ? kp_json_str(v, "") : NULL;
}

int kp_oracle_net_caps(const KpJson *c, KpSnetCaps *caps)
{
	uint8_t b[CAPS_SIZE];
	if (kp_oracle_net_unhex(kp_json_field_str(c, "caps", ""), b, sizeof b) != (long)sizeof b) return 0;
	caps->size = kp_net_get32(b);
	caps->flags = kp_net_get32(b + 4);
	caps->maxmsg = kp_net_get32(b + 8);
	caps->maxqueue = kp_net_get32(b + 12);
	caps->maxplayers = kp_net_get32(b + 16);
	caps->bytes_per_sec = kp_net_get32(b + 20);
	caps->latency = kp_net_get32(b + 24);
	caps->turns_per_sec = kp_net_get32(b + 28);
	caps->turns_in_transit = kp_net_get32(b + 32);
	return 1;
}

int kp_oracle_net_turns(const KpJson *c, int count, uint8_t (*buffers)[KP_NETGAME_TURN_MAX], const uint8_t **data,
	uint32_t *sizes, uint32_t *status)
{
	const KpJson *players = kp_json_get(c, "players");
	int k;
	for (k = 0; k < count; k++) {
		data[k] = NULL;
		sizes[k] = status[k] = 0;
	}
	for (k = 0; k < kp_json_len(players) && k < count; k++) {
		const KpJson *p = kp_json_at(players, k);
		long n = kp_oracle_net_unhex(kp_json_field_str(p, "data", ""), buffers[k], sizeof buffers[k]);
		status[k] = (uint32_t)strtoul(kp_json_field_str(p, "status", "0"), NULL, 16);
		sizes[k] = (uint32_t)kp_json_field_int(p, "size", 0);
		if (n < 0 || (uint32_t)n != sizes[k]) return 0;
		data[k] = sizes[k] ? buffers[k] : NULL;
	}
	return 1;
}

void kp_oracle_net_status(const KpJson *c, int count, uint32_t *status)
{
	const KpJson *words = kp_json_get(c, "status");
	int k;
	for (k = 0; k < count && k < kp_json_len(words); k++)
		status[k] = (uint32_t)strtoul(kp_json_str(kp_json_at(words, k), "0"), NULL, 16);
}

int kp_oracle_net_event(const KpJson *e, KpNetGame *ng)
{
	KpSnetEvent ev;
	uint8_t data[256]; /* a template record, a removal's reason */
	long n;
	if (!kp_oracle_net_is(e, "event")) return 0;
	n = kp_oracle_net_unhex(kp_json_field_str(e, "data", ""), data, sizeof data);
	ev.id = (uint32_t)kp_json_field_int(e, "id", 0);
	ev.player = (int32_t)kp_json_field_int(e, "player", -1);
	ev.data = n > 0 ? data : NULL;
	ev.size = n > 0 ? (uint32_t)n : 0;
	kp_netgame_event(ng, &ev);
	return 1;
}

uint32_t kp_oracle_net_seed(const KpJson *calls)
{
	int k;
	for (k = 0; k < kp_json_len(calls); k++) {
		const KpJson *c = kp_json_at(calls, k);
		uint8_t m[KP_NETGAME_START_INFO];
		if (kp_oracle_net_is(c, "SNetSendTurn") &&
			kp_oracle_net_unhex(kp_json_field_str(c, "given", ""), m, sizeof m) == (long)sizeof m &&
			m[0] == KP_LOBBY_START_INFO_MSG)
			return kp_net_get32(m + 1);
	}
	return 0;
}

int kp_oracle_net_header(const KpJson *create, KpNetGameHeader *h)
{
	memset(h, 0, sizeof *h);
	if (!create || !kp_oracle_net_string(create, 2) || !kp_netgame_stat_parse(kp_oracle_net_string(create, 2), h))
		return 0;
	snprintf(h->name, sizeof h->name, "%s", kp_oracle_net_string(create, 0) ? kp_oracle_net_string(create, 0) : "");
	return 1;
}

/* --- the screens' replay --- */

enum {
	F_CREATE, F_JOIN, F_LEAVE, F_SEND_TURN, F_RECEIVE_TURNS, F_IN_TRANSIT, F_SEND_MESSAGE, F_RECEIVE_MESSAGE,
	F_GAME_INFO, F_PASSWORD, F_SET_MODE, F_PLAYER_NAME, F_PROVIDER_CAPS, F_DROP, F_DISCONNECT, F_ENUM, F_COUNT
};
static const char *const FN[F_COUNT] = { "SNetCreateLadderGame", "SNetJoinGame", "SNetLeaveGame", "SNetSendTurn",
	"SNetReceiveTurns", "SNetGetTurnsInTransit", "SNetSendMessage", "SNetReceiveMessage", "SNetGetGameInfo",
	"SNetGetGameInfo", "SNetSetGameMode", "SNetGetPlayerName", "SNetGetProviderCaps", "SNetDropPlayer",
	"SNetDisconnectAll", "SNetEnumGamesEx" };

static int password_record(const KpJson *c) { return kp_oracle_net_arg(c, 0) == KP_SNET_INFO_PASSWORD; }

/* Whether record c is function f's */
static int of(const KpJson *c, int f)
{
	if (!kp_oracle_net_is(c, FN[f])) return 0;
	if (f == F_GAME_INFO) return !password_record(c);
	if (f == F_PASSWORD) return password_record(c);
	return 1;
}

struct KpOracleNet {
	const KpJson **flat; /* the records in the order the original made them, a call's inner records after it */
	int len;
	int next[F_COUNT];
	KpNetGame *ng;
	const uint32_t *now;
	uint32_t error;
	long differences;
	int log; /* NET_LOG set: each record taken is written to stderr */
	char first[512];
	int *messages, message_count, message_at; /* the successful SNetReceiveMessage records' indices, the next one's */
	/* the game list: the player's provider over the advertisements that reached it (its arrivals on port 6111) */
	KpSnpUdp list;
	const KpJson *arrivals;
	int arrival_next;
	KpSnetCaps caps;
	int has_caps;
	uint8_t message[DATA_MAX];
	uint8_t turns[KP_NETGAME_PLAYERS][KP_NETGAME_TURN_MAX];
};

static int flatten(KpOracleNet *r, const KpJson *list, int depth, int cap)
{
	int k;
	for (k = 0; k < kp_json_len(list); k++) {
		const KpJson *c = kp_json_at(list, k), *inner = kp_json_get(c, "inner");
		if (r->flat && r->len < cap) r->flat[r->len] = c;
		r->len++;
		if (inner && depth + 1 < FLAT_DEPTH) flatten(r, inner, depth + 1, cap);
	}
	return r->len;
}

static void list_send(void *ud, int socket, const KpNetAddr *to, const uint8_t *data, size_t size)
{
	/* the player's game-list requests: the recording holds what answered them */
	(void)ud;
	(void)socket;
	(void)to;
	(void)data;
	(void)size;
}

static uint32_t list_now(void *ud) { return *((const KpOracleNet *)ud)->now; }

KpOracleNet *kp_oracle_net_new(const KpJson *player, KpNetGame *ng, const uint32_t *now)
{
	KpOracleNet *r = (KpOracleNet *)calloc(1, sizeof *r);
	const KpJson *calls = kp_json_get(player, "calls");
	int n, k;
	if (!r) return NULL;
	n = flatten(r, calls, 0, 0);
	r->len = 0;
	if (!(r->flat = (const KpJson **)calloc((size_t)n + 1, sizeof *r->flat))) {
		free(r);
		return NULL;
	}
	flatten(r, calls, 0, n);
	r->ng = ng;
	r->now = now;
	r->log = getenv("NET_LOG") != NULL;
	{
		/* the player's program record (SNetInitializeProvider's): its id +0xC, version +0x10 and players +0x18 */
		uint8_t program[0x3c];
		KpNetIo io;
		long size = kp_oracle_net_unhex(kp_json_field_str(player, "program", ""), program, sizeof program);
		io.send = list_send;
		io.now = list_now;
		io.ud = r;
		kp_snp_udp_init(&r->list, &io, size >= 0x1c ? kp_net_get32(program + 0xc) : 0,
			size >= 0x1c ? kp_net_get32(program + 0x10) : 0, size >= 0x1c ? kp_net_get32(program + 0x18) : 0);
		r->arrivals = kp_json_get(player, "arrivals");
	}
	if (!(r->messages = (int *)calloc((size_t)n + 1, sizeof *r->messages))) {
		kp_oracle_net_free(r);
		return NULL;
	}
	for (k = 0; k < r->len; k++) {
		if (!r->has_caps && kp_oracle_net_is(r->flat[k], FN[F_PROVIDER_CAPS]))
			r->has_caps = kp_oracle_net_caps(r->flat[k], &r->caps);
		if (of(r->flat[k], F_RECEIVE_MESSAGE) && kp_json_field_int(r->flat[k], "result", 0))
			r->messages[r->message_count++] = k;
	}
	return r;
}

void kp_oracle_net_free(KpOracleNet *r)
{
	if (!r) return;
	kp_snp_udp_free(&r->list);
	free(r->messages);
	free(r->flat);
	free(r);
}

int kp_oracle_net_next_message(KpOracleNet *r, uint32_t *tick)
{
	int at;
	while (r->message_at < r->message_count && r->messages[r->message_at] < r->next[F_RECEIVE_MESSAGE])
		r->message_at++;
	if (r->message_at == r->message_count) return -1;
	at = r->messages[r->message_at];
	*tick = KP_ORACLE_NET_TICK(kp_json_field_int(r->flat[at], "t", 0));
	return at;
}

long kp_oracle_net_differences(const KpOracleNet *r, char *first, size_t size)
{
	if (size) snprintf(first, size, "%s", r->first);
	return r->differences;
}

static void differ(KpOracleNet *r, const char *fmt, ...) PRINTF_LIKE(2, 3);
static void differ(KpOracleNet *r, const char *fmt, ...)
{
	va_list ap;
	char msg[400];
	if (r->differences++) return;
	va_start(ap, fmt);
	vsnprintf(msg, sizeof msg, fmt, ap);
	va_end(ap);
	snprintf(r->first, sizeof r->first, "at %u: %s", (unsigned)*r->now, msg);
}

static const KpJson *peek(KpOracleNet *r, int f)
{
	while (r->next[f] < r->len && !of(r->flat[r->next[f]], f)) r->next[f]++;
	return r->next[f] < r->len ? r->flat[r->next[f]] : NULL;
}

/* The events the original's SNet dispatched inside call c, dispatched now (their handlers' calls follow them in the
 * flattened records) */
static void dispatch(KpOracleNet *r, const KpJson *c)
{
	const KpJson *inner = kp_json_get(c, "inner");
	int k;
	for (k = 0; k < kp_json_len(inner); k++) (void)kp_oracle_net_event(kp_json_at(inner, k), r->ng);
}

/* The next record of function f, taken with its events dispatched; NULL (a difference) past the original's last */
static const KpJson *take(KpOracleNet *r, int f)
{
	const KpJson *c = peek(r, f);
	if (!c) {
		differ(r, "%s called past the original's last", FN[f]);
		r->error = KP_SNET_ERROR_NO_DATA;
		return NULL;
	}
	r->next[f]++;
	if (r->log) fprintf(stderr, "net %u: %s %.80s\n", (unsigned)*r->now, FN[f], kp_json_field_str(c, "given", ""));
	dispatch(r, c);
	return c;
}

static int result_of(KpOracleNet *r, const KpJson *c)
{
	int result = (int)kp_json_field_int(c, "result", 0);
	if (!result) r->error = KP_SNET_ERROR_NO_DATA;
	return result;
}

/* Whether record c's time has come: the run's idle events and turns are a pass's first clock read, and the pass's
 * calls follow it within the millisecond after (the emulator charges each read 50 us: SNetReceiveTurns comes two reads
 * after the poll's), so a record of the next millisecond is still the pass's */
static int come(const KpOracleNet *r, const KpJson *c)
{
	return (int32_t)(KP_ORACLE_NET_TICK(kp_json_field_int(c, "t", 0)) - *r->now) <= PASS_MS;
}

/* SNetReceiveTurns' and SNetReceiveMessage's records whose time has come up to the first successful one (NULL none
 * yet; the last failed one in *failed) */
static const KpJson *receive(KpOracleNet *r, int f, const KpJson **failed)
{
	const KpJson *c;
	*failed = NULL;
	while ((c = peek(r, f)) != NULL && come(r, c)) {
		r->next[f]++;
		if (r->log && kp_json_field_int(c, "result", 0))
			fprintf(stderr, "net %u: %s %.80s\n", (unsigned)*r->now, FN[f], kp_json_field_str(c, "data", ""));
		dispatch(r, c);
		if (kp_json_field_int(c, "result", 0)) return c;
		*failed = c;
	}
	r->error = KP_SNET_ERROR_NO_DATA;
	return NULL;
}

int kp_oracle_net_same_hex(const uint8_t *p, size_t n, const char *expected)
{
	size_t k;
	if (strlen(expected) != 2 * n) return 0;
	for (k = 0; k < n; k++) {
		uint8_t v;
		if (kp_hex_decode(expected + 2 * k, &v, 1) != 0 || v != p[k]) return 0;
	}
	return 1;
}

static int same_string(const char *s, const char *expected) { return strcmp(s ? s : "", expected ? expected : "") == 0; }

static KpOracleNet *rn(void *ud) { return (KpOracleNet *)ud; }

static int s_create(void *ud, const char *name, const char *password, const char *stats, uint32_t game_type,
	uint32_t ladder_type, uint32_t mode, const void *tmpl, uint32_t tmpl_size, uint32_t max_players,
	const char *player_name, const char *player_desc, int *player_id)
{
	KpOracleNet *r = rn(ud);
	const KpJson *c = take(r, F_CREATE);
	if (!c) return 0;
	if (!same_string(name, kp_oracle_net_string(c, 0)) || !same_string(password, kp_oracle_net_string(c, 1)) ||
		!same_string(stats, kp_oracle_net_string(c, 2)) || game_type != kp_oracle_net_arg(c, 3) ||
		ladder_type != kp_oracle_net_arg(c, 4) || mode != kp_oracle_net_arg(c, 5) || tmpl_size != kp_oracle_net_arg(c, 7) ||
		max_players != kp_oracle_net_arg(c, 8) ||
		!kp_oracle_net_same_hex((const uint8_t *)tmpl, tmpl_size, kp_json_field_str(c, "template", "")) ||
		!same_string(player_name, kp_oracle_net_string(c, 9)) || !same_string(player_desc, kp_oracle_net_string(c, 10)))
		differ(r, "SNetCreateLadderGame(\"%s\", \"%s\", type %#x, mode %#x, %u players), the original's (\"%s\", \"%s\", %#x, "
			"%#x, %u)", name, stats, game_type, mode, max_players, kp_oracle_net_string(c, 0) ? kp_oracle_net_string(c, 0) : "",
			kp_oracle_net_string(c, 2) ? kp_oracle_net_string(c, 2) : "", kp_oracle_net_arg(c, 3), kp_oracle_net_arg(c, 5),
			kp_oracle_net_arg(c, 8));
	*player_id = (int)kp_json_field_int(c, "player_id", -1);
	return result_of(r, c);
}

static int s_join(void *ud, uint32_t game_id, const char *name, const char *password, const char *player_name,
	const char *player_desc, int *player_id)
{
	KpOracleNet *r = rn(ud);
	const KpJson *c = take(r, F_JOIN);
	if (!c) return 0;
	if (game_id != kp_oracle_net_arg(c, 0) || !same_string(name, kp_oracle_net_string(c, 1)) ||
		!same_string(password, kp_oracle_net_string(c, 2)) || !same_string(player_name, kp_oracle_net_string(c, 3)) ||
		!same_string(player_desc, kp_oracle_net_string(c, 4)))
		differ(r, "SNetJoinGame(%u, \"%s\"), the original's (%u, \"%s\")", game_id, player_name, kp_oracle_net_arg(c, 0),
			kp_oracle_net_string(c, 3) ? kp_oracle_net_string(c, 3) : "");
	*player_id = (int)kp_json_field_int(c, "player_id", -1);
	return result_of(r, c);
}

static int s_leave(void *ud, uint32_t reason)
{
	KpOracleNet *r = rn(ud);
	const KpJson *c = take(r, F_LEAVE);
	if (!c) return 0;
	if (reason != kp_oracle_net_arg(c, 0)) differ(r, "SNetLeaveGame(%#x), the original's %#x", reason, kp_oracle_net_arg(c, 0));
	return result_of(r, c);
}

/* The game-list datagrams that reached the player before time t_us (its port-6111 arrivals) filed by its provider */
static void heard(KpOracleNet *r, int64_t t_us)
{
	uint8_t data[KP_SNP_LIST_DATAGRAM];
	for (; r->arrival_next < kp_json_len(r->arrivals); r->arrival_next++) {
		const KpJson *a = kp_json_at(r->arrivals, r->arrival_next);
		unsigned ip[4], port;
		long n;
		if (kp_json_field_int(a, "t", 0) >= t_us) break;
		if (kp_json_field_int(a, "socket", -1) != KP_NET_SOCKET_LIST) continue;
		n = kp_oracle_net_unhex(kp_json_field_str(a, "data", ""), data, sizeof data);
		if (n < 0 || sscanf(kp_json_field_str(a, "from", ""), "%u.%u.%u.%u:%u", &ip[0], &ip[1], &ip[2], &ip[3], &port) != 5) {
			differ(r, "a malformed arrival");
			continue;
		}
		{
			KpNetAddr from = kp_net_addr((ip[0] & 0xff) << 24 | (ip[1] & 0xff) << 16 | (ip[2] & 0xff) << 8 | (ip[3] & 0xff),
				(uint16_t)port);
			(void)kp_snp_udp_input(&r->list, KP_NET_SOCKET_LIST, &from, data, (size_t)n);
		}
	}
}

static int s_enum(void *ud, int (*fn)(void *fn_ud, const KpSnetGame *g), void *fn_ud, uint32_t *hint)
{
	/* the games the player's Storm listed: the advertisements it had heard by the original's call */
	KpOracleNet *r = rn(ud);
	const KpJson *c = take(r, F_ENUM);
	if (hint) *hint = 0;
	if (!c) return 0;
	heard(r, kp_json_field_int(c, "t", 0));
	if (!kp_snet_list_games(&r->list, NULL, fn, fn_ud, hint)) {
		r->error = KP_SNET_ERROR_NO_MEMORY;
		return 0;
	}
	return result_of(r, c);
}

static int s_send_turn(void *ud, const void *data, uint32_t size)
{
	KpOracleNet *r = rn(ud);
	const KpJson *c = take(r, F_SEND_TURN);
	if (!c) return 0;
	if (!kp_oracle_net_same_hex((const uint8_t *)data, size, kp_json_field_str(c, "given", "")))
		differ(r, "SNetSendTurn: %u bytes from %02x, the original's %.120s", size, size ? *(const uint8_t *)data : 0,
			kp_json_field_str(c, "given", ""));
	return result_of(r, c);
}

static int s_receive_turns(void *ud, int first, int count, const uint8_t **data, uint32_t *sizes, uint32_t *status)
{
	KpOracleNet *r = rn(ud);
	const KpJson *failed, *c;
	int k;
	if (count > KP_NETGAME_PLAYERS) count = KP_NETGAME_PLAYERS;
	for (k = 0; k < count; k++) {
		data[k] = NULL;
		sizes[k] = status[k] = 0;
	}
	c = receive(r, F_RECEIVE_TURNS, &failed);
	if (!c) {
		if (failed) kp_oracle_net_status(failed, count, status);
		return 0;
	}
	if ((uint32_t)first != kp_oracle_net_arg(c, 0) || (uint32_t)count != kp_oracle_net_arg(c, 1))
		differ(r, "SNetReceiveTurns(%d, %d), the original's (%u, %u)", first, count, kp_oracle_net_arg(c, 0),
			kp_oracle_net_arg(c, 1));
	if (!kp_oracle_net_turns(c, count, r->turns, data, sizes, status)) {
		differ(r, "SNetReceiveTurns: the original's malformed turns");
		return 0;
	}
	return 1;
}

static int s_in_transit(void *ud, uint32_t *turns)
{
	KpOracleNet *r = rn(ud);
	const KpJson *c = take(r, F_IN_TRANSIT);
	if (!c) return 0;
	*turns = (uint32_t)kp_json_field_int(c, "in_transit", 0);
	return result_of(r, c);
}

int kp_oracle_net_same_message(const uint8_t *m, uint32_t size, const char *expected)
{
	uint8_t e[KP_CHAT_FORCE_NAMES_SIZE];
	int f;
	if (!size || m[0] != KP_CHAT_FORCE_NAMES) return kp_oracle_net_same_hex(m, size, expected);
	if (size != KP_CHAT_FORCE_NAMES_SIZE || kp_oracle_net_unhex(expected, e, sizeof e) != KP_CHAT_FORCE_NAMES_SIZE) return 0;
	for (f = 0; f < KP_NETGAME_FORCES; f++)
		if (strncmp((const char *)m + 1 + KP_NETGAME_FORCE_NAME * f, (const char *)e + 1 + KP_NETGAME_FORCE_NAME * f,
				KP_NETGAME_FORCE_NAME) != 0)
			return 0;
	return 1;
}

/* The first bytes of data as hex text (a difference's) */
static const char *hex_of(const uint8_t *p, uint32_t size, char *out, size_t cap)
{
	size_t k, n = size < (cap - 1) / 2 ? size : (cap - 1) / 2;
	for (k = 0; k < n; k++) snprintf(out + 2 * k, 3, "%02x", p[k]);
	out[2 * n] = 0;
	return out;
}

static int s_send_message(void *ud, int target, const void *data, uint32_t size)
{
	KpOracleNet *r = rn(ud);
	const KpJson *c = take(r, F_SEND_MESSAGE);
	const uint8_t *m = (const uint8_t *)data;
	char mine[121];
	if (!c) return 0;
	if (target != (int)(int32_t)kp_oracle_net_arg(c, 0) || !kp_oracle_net_same_message(m, size, kp_json_field_str(c, "given", "")))
		differ(r, "SNetSendMessage(%d): %s, the original's (%d) %.120s", target, hex_of(m, size, mine, sizeof mine),
			(int)(int32_t)kp_oracle_net_arg(c, 0), kp_json_field_str(c, "given", ""));
	return result_of(r, c);
}

static int s_receive_message(void *ud, int *sender, const uint8_t **data, uint32_t *size)
{
	KpOracleNet *r = rn(ud);
	const KpJson *failed, *c = receive(r, F_RECEIVE_MESSAGE, &failed);
	long n;
	if (!c) return 0;
	n = kp_oracle_net_unhex(kp_json_field_str(c, "data", ""), r->message, sizeof r->message);
	if (n < 0) {
		differ(r, "SNetReceiveMessage: the original's malformed data");
		r->error = KP_SNET_ERROR_NO_DATA;
		return 0;
	}
	*sender = (int)kp_json_field_int(c, "sender", -1);
	*data = r->message;
	*size = (uint32_t)n;
	return 1;
}

static int s_game_info(void *ud, int type, void *buf, uint32_t size, uint32_t *written)
{
	KpOracleNet *r = rn(ud);
	const KpJson *c;
	long n;
	if (type == KP_SNET_INFO_PASSWORD) { /* the screens': the latest record that has come */
		const KpJson *latest = NULL;
		while ((c = peek(r, F_PASSWORD)) != NULL && come(r, c)) {
			latest = c;
			r->next[F_PASSWORD]++;
		}
		c = latest ? latest : peek(r, F_PASSWORD);
		if (!c) {
			r->error = KP_SNET_ERROR_NO_DATA;
			return 0;
		}
	} else if (!(c = take(r, F_GAME_INFO))) {
		return 0;
	} else if ((uint32_t)type != kp_oracle_net_arg(c, 0) || size != kp_oracle_net_arg(c, 2)) {
		differ(r, "SNetGetGameInfo(%d, %u), the original's (%u, %u)", type, size, kp_oracle_net_arg(c, 0),
			kp_oracle_net_arg(c, 2));
	}
	memset(buf, 0, size);
	n = kp_oracle_net_unhex(kp_json_field_str(c, "info", ""), (uint8_t *)buf, size);
	if (written) *written = n > 0 ? (uint32_t)n : 0;
	return result_of(r, c);
}

static int s_set_mode(void *ud, uint32_t mode, int make_private)
{
	KpOracleNet *r = rn(ud);
	const KpJson *c = take(r, F_SET_MODE);
	if (!c) return 0;
	if (mode != kp_oracle_net_arg(c, 0) || (uint32_t)make_private != kp_oracle_net_arg(c, 1))
		differ(r, "SNetSetGameMode(%#x, %d), the original's (%#x, %u)", mode, make_private, kp_oracle_net_arg(c, 0),
			kp_oracle_net_arg(c, 1));
	return result_of(r, c);
}

static int s_player_name(void *ud, int id, char *buf, uint32_t size)
{
	KpOracleNet *r = rn(ud);
	const KpJson *c = take(r, F_PLAYER_NAME);
	if (!c) return 0;
	if ((uint32_t)id != kp_oracle_net_arg(c, 0))
		differ(r, "SNetGetPlayerName(%d), the original's (%u)", id, kp_oracle_net_arg(c, 0));
	if (size) snprintf(buf, size, "%s", kp_json_field_str(c, "name", ""));
	return result_of(r, c);
}

static int s_provider_caps(void *ud, KpSnetCaps *caps)
{
	KpOracleNet *r = rn(ud);
	const KpJson *c = take(r, F_PROVIDER_CAPS);
	if (!c) return 0;
	if (!kp_oracle_net_caps(c, caps)) {
		differ(r, "SNetGetProviderCaps: the original's malformed caps");
		return 0;
	}
	return result_of(r, c);
}

static int s_player_caps(void *ud, int id, KpSnetCaps *caps)
{
	KpOracleNet *r = rn(ud);
	(void)id;
	if (!r->has_caps) {
		r->error = KP_SNET_ERROR_NO_DATA;
		return 0;
	}
	*caps = r->caps;
	return 1;
}

static int s_drop(void *ud, int id, uint32_t reason)
{
	KpOracleNet *r = rn(ud);
	const KpJson *c = take(r, F_DROP);
	if (!c) return 0;
	if ((uint32_t)id != kp_oracle_net_arg(c, 0) || reason != kp_oracle_net_arg(c, 1))
		differ(r, "SNetDropPlayer(%d, %#x), the original's (%u, %#x)", id, reason, kp_oracle_net_arg(c, 0),
			kp_oracle_net_arg(c, 1));
	return result_of(r, c);
}

static int s_disconnect(void *ud, uint32_t reason)
{
	KpOracleNet *r = rn(ud);
	const KpJson *c = take(r, F_DISCONNECT);
	if (!c) return 0;
	if (reason != kp_oracle_net_arg(c, 0))
		differ(r, "SNetDisconnectAll(%#x), the original's %#x", reason, kp_oracle_net_arg(c, 0));
	return result_of(r, c);
}

static uint32_t s_error(void *ud) { return rn(ud)->error; }

KpNetCalls kp_oracle_net_calls(KpOracleNet *r)
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
	c.provider_caps = s_provider_caps;
	c.player_caps = s_player_caps;
	c.drop_player = s_drop;
	c.disconnect_all = s_disconnect;
	c.error = s_error;
	c.ud = r;
	return c;
}
