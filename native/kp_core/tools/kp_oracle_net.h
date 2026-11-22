/*
 * The network oracle's recordings as SNet (phase N, stage N3): a player's calls of tests/golden/oracle/net/<game>.json
 * (tools/net_check.py, from the emulator's runs of `python -m sc_emulator net`) answering KpNetGame's SNet calls.
 *
 * The decoders (hex, caps, a turn's players, the host's seed) serve the strict replay of test_lobby_trace, where every
 * call must be the original's next one. KpOracleNet is the screens' replay (kp_oracle menus_replay): the chat room
 * drives the lobby at the moments the menus' trace gives (its idle events and turns), not at each pass of the
 * original's message loop, so the replay is gated by the clock instead of the calls' order:
 *   - each SNet function takes the original's records of its own in order (the arguments and the data it is given are
 *     compared with the record's; a call past the original's last is a difference);
 *   - SNetReceiveTurns and SNetReceiveMessage take the records whose time has come (GetTickCount, KP_ORACLE_NET_TICK
 *     of the record's t): they skip the original's failed calls, and answer the first successful one, or fail with
 *     no data when none has come yet;
 *   - the events the original's SNet dispatched inside a call are dispatched when its record is taken;
 *   - the screens' own calls answer from their latest record (SNetGetGameInfo's password), or from the provider's caps
 *     (SNetGetPlayerCaps: the recording keeps no player's caps; a LAN's round trips stay under the latency bars' first
 *     step).
 */
#ifndef KP_ORACLE_NET_H
#define KP_ORACLE_NET_H

#include <stddef.h>
#include <stdint.h>

#include "kp_json.h"
#include "kp_netgame.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KP_ORACLE_NET_VERSION 8        /* tools/net_check.py's golden version */
#define KP_ORACLE_NET_TICK_BASE 3600000u /* the emulator's GetTickCount at virtual time 0 (hle/kernel32.py) */
/* GetTickCount at a record's t (microseconds of virtual time) */
#define KP_ORACLE_NET_TICK(t_us) (KP_ORACLE_NET_TICK_BASE + (uint32_t)((t_us) / 1000))

/* The hex text s into out: its byte count, -1 when it is not hex or longer than cap. */
long kp_oracle_net_unhex(const char *s, uint8_t *out, size_t cap);
/* A record's "fn", whether it is fn, its argument k (0 none) and its string argument k (NULL none) */
const char *kp_oracle_net_fn(const KpJson *c);
int kp_oracle_net_is(const KpJson *c, const char *fn);
uint32_t kp_oracle_net_arg(const KpJson *c, int k);
const char *kp_oracle_net_string(const KpJson *c, int k);
/* Whether the n bytes at p are the hex text expected; a message whether it is the original's: SendForceNames 0x471e60
 * copies each 30-byte name with what its buffer held past the NUL, so a 'K' compares its names as strings. */
int kp_oracle_net_same_hex(const uint8_t *p, size_t n, const char *expected);
int kp_oracle_net_same_message(const uint8_t *m, uint32_t size, const char *expected);
/* An SNetGetProviderCaps record's caps: 1, 0 when malformed. */
int kp_oracle_net_caps(const KpJson *c, KpSnetCaps *caps);
/* A successful SNetReceiveTurns record's turns of count players into buffers: 1, 0 when malformed. */
int kp_oracle_net_turns(const KpJson *c, int count, uint8_t (*buffers)[KP_NETGAME_TURN_MAX], const uint8_t **data,
	uint32_t *sizes, uint32_t *status);
/* A failed SNetReceiveTurns record's status words (what StarCraft reads after the failure). */
void kp_oracle_net_status(const KpJson *c, int count, uint32_t *status);
/* An event record ("fn": "event") dispatched to ng: 1, 0 when the record is not an event. */
int kp_oracle_net_event(const KpJson *e, KpNetGame *ng);
/* The host's start information's seed (its time(NULL)) in its calls, 0 none. */
uint32_t kp_oracle_net_seed(const KpJson *calls);
/* The game's header from a create record's stat string and name: 1, 0 when it has none. */
int kp_oracle_net_header(const KpJson *create, KpNetGameHeader *h);

typedef struct KpOracleNet KpOracleNet;

/* The screens' replay of player (a golden's players[k]) for the game at ng (its events are dispatched there; it may be
 * initialized later with the calls), at the clock *now (GetTickCount). NULL out of memory. */
KpOracleNet *kp_oracle_net_new(const KpJson *player, KpNetGame *ng, const uint32_t *now);
void kp_oracle_net_free(KpOracleNet *r);
KpNetCalls kp_oracle_net_calls(KpOracleNet *r);
/* The differences from the original's calls so far and the first's text. */
long kp_oracle_net_differences(const KpOracleNet *r, char *first, size_t size);
/* The next successful SNetReceiveMessage record not taken yet: its index in the original's records (-1 none) and its
 * tick. The original's message loop took each as it came (PollLobby 0x474f00, one per pass of the chat room's modal
 * loop): the screens' replay runs a pass of the room at each. */
int kp_oracle_net_next_message(KpOracleNet *r, uint32_t *tick);

#ifdef __cplusplus
}
#endif

#endif
