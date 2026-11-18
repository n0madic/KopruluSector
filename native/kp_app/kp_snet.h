/*
 * Storm's network layer SNet (phase N, stage N2; ADR 0026; atlas S01 and S02, docs/formats/storm-udp.md) over the UDP
 * provider (kp_snp_udp.h), as a state machine: the exports StarCraft 1.17 calls, with their arguments, results and
 * Storm's error codes, and the pump thread's work (kp_snet_pump), which its driver runs when a datagram arrived or the
 * timer it returned ran out. Every byte it sends is the original's: Koprulu plays with Koprulu and with StarCraft 1.17.
 *
 * Time is the hooks' millisecond clock (GetTickCount). Nothing here waits, except kp_snet_join_game, which pumps the
 * provider itself as SNetJoinGame does (PumpUntilControl 0x15023f40) and waits through the hooks' sleep between its
 * passes. Events (SEvt 'SNET' type 1) go to the hooks' handler, outside the driver's lock (the hooks' unlock and lock),
 * from kp_snet_receive_turns, kp_snet_receive_message and kp_snet_join_game, as Storm dispatches them.
 *
 * Where it differs from Storm, only for inputs Storm mishandles or for state no caller reads:
 *   - a control command shorter than its payload reads its missing bytes as zeros (Storm reads past it);
 *   - at most KP_SNET_RECORDS remote records and KP_SNET_NODES filed packets: a datagram beyond them is dropped
 *     (Storm allocates for every forged address and every packet until its memory runs out);
 *   - a turn or message above 0xFFFF - 12 bytes is refused with 0x57 (Storm's 16-bit length wraps);
 *   - a control command whose handler deletes the record being dispatched ends that dispatch (Storm reads on in the
 *     freed record);
 *   - SNetReceiveTurns's slots outside 0..254 have no record (Storm compares an id's low byte: slot 256 + k is player
 *     k again, whose turn it takes twice);
 *   - the name table (SNetGetPlayerNames and SNetReportGameResult, Battle.net's) is not kept, nor the performance
 *     counters; the base player is 0 (StarCraft never sets it);
 *   - SNetSendDatagram and SNetReceiveDatagram (class 3, which StarCraft never calls) are not ported: a datagram of
 *     that class is filed and never read, as nothing reads it in StarCraft;
 *   - SNetLeaveGame does not sleep its max(L, 50) ms at the end (the driver may).
 */
#ifndef KP_SNET_H
#define KP_SNET_H

#include <stddef.h>
#include <stdint.h>

#include "kp_net.h"
#include "kp_snp_udp.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Storm's error codes (SErrGetLastError) */
#define KP_SNET_ERROR_NO_MEMORY 8u
#define KP_SNET_ERROR_PARAMETER 0x57u
#define KP_SNET_ERROR_NOT_OWNER 0x120u       /* SNetSetGameMode by a seat that is not host */
#define KP_SNET_ERROR_NO_PROVIDER 0x4b4u
#define KP_SNET_ERROR_UNREACHABLE 0x4d0u     /* the join's handshake timed out */
#define KP_SNET_ERROR_REJECTED 0x85100067u   /* the host refused the join (full, closed, password, no id) */
#define KP_SNET_ERROR_NOT_FOUND 0x85100068u  /* no listed game of that id or name */
#define KP_SNET_ERROR_GAME_ENDED 0x85100069u /* the local seat left while the events were dispatched */
#define KP_SNET_ERROR_BAD_PLAYER 0x8510006au
#define KP_SNET_ERROR_NO_DATA 0x8510006bu    /* no turn or message waiting */
#define KP_SNET_ERROR_NOT_IN_GAME 0x85100070u
#define KP_SNET_ERROR_VERSION 0x85100079u    /* the advertised game's program or version differs */

#define KP_SNET_INFINITE 0xffffffffu /* kp_snet_pump: no timer pending */
#define KP_SNET_NO_ID 0xffu
#define KP_SNET_CLASSES 4
#define KP_SNET_HEADER 12
#define KP_SNET_STRING 128 /* names, descriptions, the game's name, stat string and password (0x80) */
#define KP_SNET_RECORDS 1024 /* remote records, at most */
#define KP_SNET_NODES 16384  /* filed packets, at most */

/* Mode bits (SNetSetGameMode, command 0x0E, the advertisement) */
#define KP_SNET_MODE_PASSWORD 1u
#define KP_SNET_MODE_FULL 2u
#define KP_SNET_MODE_ADVERTISED 4u
#define KP_SNET_MODE_CLOSED 8u
/* The status words of kp_snet_receive_turns */
#define KP_SNET_PRESENT 0x10000u
#define KP_SNET_HAS_TURN 0x20000u
#define KP_SNET_RESPONDING 0x40000u

/* SNetGetGameInfo's types */
enum { KP_SNET_INFO_NAME = 1, KP_SNET_INFO_PASSWORD, KP_SNET_INFO_STATS, KP_SNET_INFO_MODE, KP_SNET_INFO_TEMPLATE,
	KP_SNET_INFO_MAX_PLAYERS };
/* The events (SNetRegisterEventHandler): the host's template, a player became present, a player was removed (data:
 * the u32 reason), the provider's external text (never over UDP). */
enum { KP_SNET_EVENT_TEMPLATE = 1, KP_SNET_EVENT_PLAYER = 2, KP_SNET_EVENT_REMOVED = 3, KP_SNET_EVENT_TEXT = 4 };

/* CAPS: {size 0x24, flags, maxmsg, maxqueue, maxplayers, bytes a second, latency, turns a second, turns in transit} */
typedef struct {
	uint32_t size, flags, maxmsg, maxqueue, maxplayers, bytes_per_sec, latency, turns_per_sec, turns_in_transit;
} KpSnetCaps;

/* The UDP provider's caps as caps.dat holds them (Storm's record keeps maxmsg - 16) */
extern const KpSnetCaps KP_SNET_UDP_CAPS;

typedef struct {
	uint32_t id;
	int32_t player;
	const uint8_t *data;
	uint32_t size;
} KpSnetEvent;

typedef struct {
	uint32_t (*now)(void *ud);                     /* GetTickCount */
	void (*sleep)(void *ud, uint32_t ms);          /* the join's waits; the network runs meanwhile */
	void (*event)(void *ud, const KpSnetEvent *e); /* the game's event handlers */
	void (*unlock)(void *ud);                      /* the driver's lock around the events' dispatch (NULL: none) */
	void (*lock)(void *ud);
	void *ud;
} KpSnetHooks;

/* SNetEnumGamesEx's record (0x1C bytes): {4, id, name, stat string, game type, players, max players}. */
typedef struct {
	uint32_t id;
	const char *name, *stats;
	uint32_t game_type, players, max_players;
} KpSnetGame;

typedef struct KpSnet KpSnet;

/* SNetInitializeProvider 0x15025f00 for the UDP provider p (initialised by the caller): the program id ('STAR' or
 * 'SEXP') and version (0xD5) the joins compare. The hooks' clock is required (every timer and the join's waits run on
 * it). NULL out of memory or without it. */
KpSnet *kp_snet_new(KpSnpUdp *p, const KpSnetHooks *hooks, uint32_t program, uint32_t version);
/* SNetDestroy 0x15024ca0 (a game left with reason 3 first) and the state freed. */
void kp_snet_free(KpSnet *s);
/* SErrGetLastError: the code the last failed call set. */
uint32_t kp_snet_error(const KpSnet *s);

/* The pump thread 0x15022780's pass: when `signalled` (a datagram arrived, or a send woke it) the provider's packets
 * are read and filed (ReceivePackets 0x15021fa0), then the timers run (RunNetTimers 0x15020b80): acks, gap requests,
 * retransmissions, pings. Returns the milliseconds to the next timer, KP_SNET_INFINITE for none: the driver calls
 * again then, or when a datagram arrives. Does nothing while kp_snet_join_game runs (it pumps itself). */
uint32_t kp_snet_pump(KpSnet *s, int signalled);
/* Whether a send since the last pump signalled the pump event (SNetSendTurn, SNetSendMessage); cleared by the call. */
int kp_snet_take_signal(KpSnet *s);

/* SNetCreateLadderGame 0x150249a0: host a game; *player_id 0. tmpl: the template data the joiners' event 1 gets. */
int kp_snet_create_game(KpSnet *s, const char *name, const char *password, const char *stats, uint32_t game_type,
	uint32_t ladder_type, uint32_t mode, const void *tmpl, uint32_t tmpl_size, uint32_t max_players,
	const char *player_name, const char *player_desc, int *player_id);
/* SNetJoinGame 0x15026100: join the listed game (id, or name with id 0); blocks through the hooks' sleep for at most
 * two waits of max(12L, 5000) ms. */
int kp_snet_join_game(KpSnet *s, uint32_t game_id, const char *name, const char *password, const char *player_name,
	const char *player_desc, int *player_id);
/* SNetLeaveGame 0x15026560. */
int kp_snet_leave_game(KpSnet *s, uint32_t reason);
/* SNetEnumGamesEx 0x150251c0: fn(ud, game) for each visible listed game until it returns 0; *hint 500. */
int kp_snet_enum_games(KpSnet *s, int (*fn)(void *ud, const KpSnetGame *g), void *ud, uint32_t *hint);
/* Its listing over a provider's game list, the list unlocked around the calls of fn (hooks' unlock and lock, NULL
 * none): 1, 0 out of memory. The network oracle's replay lists a recording's advertisements with it. */
int kp_snet_list_games(KpSnpUdp *snp, const KpSnetHooks *hooks, int (*fn)(void *ud, const KpSnetGame *g), void *ud,
	uint32_t *hint);

/* SNetSendTurn 0x15027ee0. */
int kp_snet_send_turn(KpSnet *s, const void *data, uint32_t size);
/* SNetReceiveTurns 0x15027110: slots first..first+count-1; data[i] (valid until the next call) and sizes[i] of each
 * present player's turn, status[i] its word. */
int kp_snet_receive_turns(KpSnet *s, int first, int count, const uint8_t **data, uint32_t *sizes, uint32_t *status);
/* SNetGetTurnsInTransit 0x15025d80. */
int kp_snet_turns_in_transit(KpSnet *s, uint32_t *turns);
/* SNetSendMessage 0x15027c20: target -2 every remote, -1 those and the local seat, else a player id. */
int kp_snet_send_message(KpSnet *s, int target, const void *data, uint32_t size);
/* SNetReceiveMessage 0x15026e90: *data valid until the sender's next message is received. */
int kp_snet_receive_message(KpSnet *s, int *sender, const uint8_t **data, uint32_t *size);

/* SNetGetGameInfo 0x15025410 (buf NULL: *written the size needed). */
int kp_snet_game_info(KpSnet *s, int type, void *buf, uint32_t size, uint32_t *written);
/* SNetSetGameMode 0x15028050. */
int kp_snet_set_game_mode(KpSnet *s, uint32_t mode, int make_private);
/* SNetGetNumPlayers 0x150256f0 (each output optional). */
int kp_snet_num_players(KpSnet *s, int *first, int *last, int *active);
/* SNetGetPlayerName 0x15025b00. */
int kp_snet_player_name(KpSnet *s, int id, char *buf, uint32_t size);
/* SNetGetPlayerCaps 0x15025a10 (caps->size must be 0x24). */
int kp_snet_player_caps(KpSnet *s, int id, KpSnetCaps *caps);
/* SNetGetProviderCaps 0x15025c80 (caps->size must be 0x24). */
int kp_snet_provider_caps(KpSnet *s, KpSnetCaps *caps);
/* SNetDropPlayer 0x15024e40. */
int kp_snet_drop_player(KpSnet *s, int id, uint32_t reason);
/* SNetDisconnectAll 0x15024da0. */
int kp_snet_disconnect_all(KpSnet *s, uint32_t reason);

/* --- inspection (tests, the driver's logs) --- */

/* The local id (KP_SNET_NO_ID outside a game), and the local seat's next turn. */
int kp_snet_local_id(const KpSnet *s);
uint32_t kp_snet_local_turn(const KpSnet *s);
/* Whether a record with that id is host (0/1), -1 without one. */
int kp_snet_is_host(const KpSnet *s, int id);
/* The remote records (with or without an id). */
int kp_snet_remote_count(const KpSnet *s);

#ifdef __cplusplus
}
#endif

#endif
