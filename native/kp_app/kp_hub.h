/*
 * The turn hub (phase M, stage M6; ADR 0025): the seats of one network game in one process, with the network's turns
 * between them as Storm carries them (SNetSendTurn 0x15027ee0, SNetReceiveTurns 0x15027110, atlas S01) and timed as
 * the turn manager times them (atlas P26, P15):
 *
 *   - a seat's commands go into its outgoing turn (QueueTurnCommand 0x476c80): one past the message size sends the
 *     turn first, or is dropped while the seat's turns in transit are 16 - the target or more;
 *   - a seat's frame polls the turn every frames_per_turn frames (PollTurn 0x444cc0, 0x666404[speed]); the frames
 *     between run without a turn;
 *   - at a poll the seat receives the turn every seat sent (ReceiveTurns 0x475ac0; one missing: the seat waits), tops
 *     its turns in transit up to the target (SendTurns 0x476500: 0x672660 and the extra turns 0x672990; the game's
 *     start sends the first ones, RunGame 0x445310), then runs the turn: each seat's commands, the Storm IDs from 7
 *     down to 0, as that seat's record (ExecuteGameTurn 0x4768a0);
 *   - the latency command 0x55 sets the extra turns of each seat that runs it (OnLatencyCommand 0x476d10: at most 2,
 *     the lowest Storm ID's of a turn last): the network's state, never saved.
 *
 * Not carried: the keep-alive 0x05 of an empty turn (an empty turn runs nothing), the sync command 0x37 and its check
 * ResolveDesync 0x47a630 (the seats' shared hash instead, kp_shared_hash), a seat's drop or leave (TODO: SNetLeaveGame
 * and the other seats' HandlePlayerLeftGame 0x477da0 after their receive). A network transport implements the same
 * calls later (one seat here, the others' turns from the network).
 */
#ifndef KP_HUB_H
#define KP_HUB_H

#include <stddef.h>
#include <stdint.h>

#include "kp_game.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KP_HUB_SEATS 8
#define KP_HUB_MESSAGE 512 /* 0x67265c: the provider's message size, at most 0x200 */
#define KP_HUB_TRANSIT_LIMIT 16 /* QueueTurnCommand's bound of the turns in transit (with the target) */
#define KP_HUB_RING 32 /* the turns a seat's stream keeps: past the most a seat can have in transit */
#define KP_HUB_IN_TRANSIT 2 /* 0x672660: max(2, min(8, the provider's turns in transit)), the hub's provider's 2 */
#define KP_HUB_TURNS_PER_SECOND 8 /* the provider's turns per second (caps +0x1c), the hub's provider's 8 */
#define KP_HUB_EXTRA_MAX 2 /* the latency command's most extra turns */

/* kp_hub_frame's answer: the frame runs a turn (its commands), the frame runs between turns, the seat waits for a
 * seat's turn (the frame does not run), out of memory. */
enum { KP_HUB_TURN, KP_HUB_BETWEEN, KP_HUB_WAIT, KP_HUB_NO_MEMORY };

typedef struct {
	uint16_t size;
	uint8_t bytes[KP_HUB_MESSAGE];
} KpHubTurn;

typedef struct {
	int seats;
	int frames_per_turn; /* 0x666404[speed] */
	int in_transit;      /* 0x672660 */
	int started;
	int64_t dropped;     /* the commands QueueTurnCommand dropped */
	/* each seat's record (its commands' KpCommand.player), Storm ID, the turns it sent (the last KP_HUB_RING) and their
	 * count, the turns it received, its outgoing turn, its extra turns 0x672990, the frames to its next poll 0x6663fc
	 * and the list its last turn gave (owned, pointing into the senders' turns) */
	int64_t record[KP_HUB_SEATS];
	int32_t storm[KP_HUB_SEATS];
	uint8_t assigned[KP_HUB_SEATS];
	KpHubTurn sent[KP_HUB_SEATS][KP_HUB_RING];
	int64_t sent_count[KP_HUB_SEATS], received[KP_HUB_SEATS];
	KpHubTurn out[KP_HUB_SEATS];
	int extra[KP_HUB_SEATS];
	int countdown[KP_HUB_SEATS];
	KpCommand *list[KP_HUB_SEATS];
	int list_cap[KP_HUB_SEATS];
} KpTurnHub;

/* The frames per turn of a speed 0..6 with the provider's turns per second (SetSpeedTable 0x445110: 1000 / (the
 * speed's milliseconds per frame * turns), at least 1; turns per second 0 or 20 and more are 20). */
int kp_hub_frames_per_turn(int speed, int turns_per_second);
/* A hub of `seats` seats (1..KP_HUB_SEATS) with frames_per_turn frames a turn (at least 1); NULL out of memory or for a
 * count outside. */
KpTurnHub *kp_hub_new(int seats, int frames_per_turn);
void kp_hub_free(KpTurnHub *h);
/* Seat k's record and Storm ID (distinct, 0..7), before kp_hub_begin; -1 outside. */
int kp_hub_seat(KpTurnHub *h, int seat, int64_t record, int32_t storm);
/* The game's start: every seat sends its first turns (RunGame 0x445310's SendTurns); -1 while a seat has no Storm ID. */
int kp_hub_begin(KpTurnHub *h);
/* A command of the seat for its outgoing turn (QueueTurnCommand 0x476c80): 0, or -1 when it is empty, longer than a
 * message, or dropped. */
int kp_hub_submit(KpTurnHub *h, int seat, const uint8_t *bytes, size_t size);
/* The seat's next frame (PollTurn 0x444cc0 and TurnManagerStep 0x476c00): KP_HUB_TURN with the turn's commands in
 * *list (owned by the hub, valid until the seat's next kp_hub_frame), KP_HUB_BETWEEN, KP_HUB_WAIT (*count 0: the frame
 * does not run; the next call polls again) or KP_HUB_NO_MEMORY. */
int kp_hub_frame(KpTurnHub *h, int seat, const KpCommand **list, int *count);
/* RunGame 0x445310's turn before its loop (TurnManagerStep 0x476c00 called directly: no frames counted), the seat's
 * first: KP_HUB_TURN with its commands, or KP_HUB_WAIT before kp_hub_begin or after the seat's first turn. */
int kp_hub_first_turn(KpTurnHub *h, int seat, const KpCommand **list, int *count);
/* The seat's turns in transit and its target (SendTurns 0x476500's). */
int64_t kp_hub_in_transit(const KpTurnHub *h, int seat);
int kp_hub_target(const KpTurnHub *h, int seat);

#ifdef __cplusplus
}
#endif

#endif
