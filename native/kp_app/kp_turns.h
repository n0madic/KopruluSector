/*
 * The turn source (phase T, stage T1; ADR 0022): where a step's commands come from. The session asks it for each
 * frame's commands instead of gathering them itself:
 *
 *   - LOCAL, a game played here: the interface's commands queued for the next step (the session's queue of
 *     ExecuteTurnCommands 0x475d30's turn buffer), each a whole buffer of the player it was queued for;
 *   - REPLAY, a replay's playback (StartGame 0x443f40 step 13): the record's commands of the frame
 *     (FeedReplayCommands 0x4769b0), each buffer walked as the dispatcher walks it and played as the player whose
 *     record has its Storm ID, then the live commands the playback lets through (table 0x47636c).
 *
 * The recorder of a recorded game watches the executed list (kp_turns_record): the commands of the seats a person
 * plays, under their Storm IDs (a game played alone: the local player, ID 0, RecordCommand 0x478340's).
 *
 *   - SEATS, a seat of a network game (phase M, stage M6, ADR 0025): the turn hub's (kp_hub.h), whose frames run a
 *     turn every frames per turn, between turns without one, or wait for a seat's turn (the step waits; the clock
 *     keeps no missed steps); every seat's commands are recorded under its Storm ID.
 *   - NET, a seat of a network game over SNet (phase N, stage N4): the network game's turns (kp_netgame.h), polled as
 *     the hub's every frames per turn; the session runs its frames itself (kp_session_step: the turn, the sync ring
 *     and the frame are one guarded step), the local commands go into the outgoing turn.
 *
 *   - NET over a replay, a hosted replay watched over the network (CreateGameFromEntry 0x45bfc0's kind 0x80): each
 *     frame the record's commands first (RunPendingFrames 0x444e30 step 1, kp_turns_feed), then the network's turn
 *     at its frame, whose commands the replay's filter passes (0x47636c, kp_netgame.h).
 *
 *   - REPLAY over a growing record, a viewer of a network game (kp_watch.h): the record's bytes arrive as the game
 *     goes on, each time with the frame every block up to which it holds (kp_turns_watch_grow); frame f runs once
 *     the blocks up to f + 1 are there, else waits (KP_TURNS_WAIT) until more arrives or the record is whole (its end
 *     then the replay's, its last block run too: kp_record_grow). The block after f is so in the record whenever the feed reads ahead at f, as in a file:
 *     read one call early, a frame of more than 0x200 bytes loses its commands as 1.17's does (kp_record.h), read at
 *     its own frame it would not.
 */
#ifndef KP_TURNS_H
#define KP_TURNS_H

#include <stddef.h>
#include <stdint.h>

#include "kp_game.h"
#include "kp_hub.h"
#include "kp_netgame.h"
#include "kp_record.h"
#include "kp_rep.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KP_TURNS_PENDING 256
#define KP_TURNS_COMMAND_BYTES 256
#define KP_TURNS_SLOTS 8 /* the players a record's Storm IDs map to and the seats a recording watches: 0..7 */

typedef struct {
	int64_t player;
	uint16_t size;
	uint8_t bytes[KP_TURNS_COMMAND_BYTES];
} KpQueuedCommand;

enum { KP_TURNS_LOCAL, KP_TURNS_REPLAY, KP_TURNS_SEATS, KP_TURNS_NET };
/* kp_turns_frame's status: the frame's commands; the replay's end, given once at the frame its record passed the
 * header's frame count (the live commands still come); out of memory (no commands); SEATS: a frame between turns
 * (no turn, no commands), the seat waits for a turn (the frame does not run). */
enum { KP_TURNS_READY, KP_TURNS_ENDED, KP_TURNS_NO_MEMORY, KP_TURNS_BETWEEN, KP_TURNS_WAIT };

typedef struct {
	int kind;
	KpQueuedCommand pending[KP_TURNS_PENDING]; /* the commands queued for the next step */
	int pending_count;
	int32_t storm[KP_TURNS_SLOTS]; /* the Storm ID a recording keeps each player's commands under, -1 none */
	/* REPLAY: the record (owned), the header's frame count, whether the record passed it (0x480e00), the records'
	 * Storm IDs and players (0x6726c8) and the player the feed's commands run as (0x508d48, kept when a Storm ID has
	 * no record) */
	KpRecorder *feed;
	uint32_t frames;
	int ended;
	int growing, whole; /* a viewer's growing record (kp_turns_watch) and whether it is whole */
	uint32_t feed_storm[KP_TURNS_SLOTS], feed_slot[KP_TURNS_SLOTS];
	int64_t feed_player;
	KpCommand *list; /* owned: the frame's commands kp_turns_frame gave */
	int list_cap;
	/* SEATS: the hub (not owned) and the seat */
	KpTurnHub *hub;
	int seat;
	/* NET: the network game (not owned), its frames per turn and the frames to its next poll (PollTurn 0x444cc0's
	 * 0x666404[speed] and 0x6663fc) */
	KpNetGame *net;
	int frames_per_turn, countdown;
} KpTurns;

/* A replay's source: its record loaded (BeginReplayPlayback 0x4789f0), its players' Storm IDs. t must be zeroed (a
 * zeroed KpTurns is a LOCAL source). -1 out of memory, t then LOCAL again. */
int kp_turns_replay(KpTurns *t, const KpRep *rep);
/* A viewer's source: a replay (its header and map; its record, if any, the first bytes) whose record grows
 * (kp_turns_watch_grow), no frame of it known yet. -1 out of memory, t then LOCAL again. */
int kp_turns_watch(KpTurns *t, const KpRep *rep);
/* The viewer's record grown by `size` bytes, holding every block up to frame frames - 1 (the viewer then plays up to
 * frame frames - 2); `whole`: the game ended, `frames` its count. -1 out of memory (nothing taken), or for a source that is not a
 * viewer's. */
int kp_turns_watch_grow(KpTurns *t, const uint8_t *bytes, size_t size, uint32_t frames, int whole);
/* A hosted replay's seat: the network game's turns (as kp_turns_net) over the replay's record (as kp_turns_replay).
 * -1 out of memory, t then LOCAL again. */
int kp_turns_net_replay(KpTurns *t, KpNetGame *net, const KpRep *rep);
/* A network game's seat: its turns the hub's (kept until kp_turns_free; t zeroed). */
void kp_turns_seats(KpTurns *t, KpTurnHub *hub, int seat);
/* A network game's seat: its turns the network game's (kept until kp_turns_free; t zeroed). */
void kp_turns_net(KpTurns *t, KpNetGame *net);
/* A network game's seat once the network game began (kp_netgame_game_begin): its Storm IDs' records, under which the
 * recording keeps their commands, and the frames per turn of the game's speed (0..6) with the provider's turns per
 * second. */
void kp_turns_net_begin(KpTurns *t, int64_t speed);
/* Once the game started: the local player, whom LOCAL's recording watches (Storm ID 0) and whom the feed's commands
 * run as until a Storm ID names a record. */
void kp_turns_begin(KpTurns *t, int64_t local_player);
void kp_turns_free(KpTurns *t);
int kp_turns_playing(const KpTurns *t);
/* The replay's end passed (kp_turns_frame gave KP_TURNS_ENDED); 0 for LOCAL. */
int kp_turns_ended(const KpTurns *t);
/* The header's frame count of the replay; 0 for LOCAL. */
uint32_t kp_turns_frames(const KpTurns *t);

/* A command of player (0..7) for the next step: 0, or -1 when the queue is full, it is empty or too long, the player is
 * none of the seats, or the playback does not let it through (REPLAY and a hosted replay: the keep-alive, restart, pause and resume, the
 * sync, the network's 0x37-0x3b, latency and the replay speed only, ExecuteTurnCommands 0x475d30's replay filter).
 * SEATS: the seat's command for its outgoing turn, whatever the player (kp_hub_submit); NET the same
 * (kp_netgame_command). */
int kp_turns_submit(KpTurns *t, const uint8_t *bytes, size_t size, int64_t player);
/* The commands of game frame `frame` in *list (owned by t, valid until kp_turns_done; the queued ones point into
 * the queue) and their count: KP_TURNS_READY, KP_TURNS_ENDED or KP_TURNS_NO_MEMORY (*count 0). */
int kp_turns_frame(KpTurns *t, int64_t frame, const KpCommand **list, int *count);
/* A playback's record of frame `frame` alone (FeedReplayCommands 0x4769b0; the network's turns are polled apart): as
 * kp_turns_frame, nothing without a record. */
int kp_turns_feed(KpTurns *t, int64_t frame, const KpCommand **list, int *count);
/* The frame's commands ran: the queue is empty. */
void kp_turns_done(KpTurns *t);
/* RecordCommand 0x478340 of a frame's executed commands before the frame's step: each command of a buffer whose player
 * has a Storm ID (kp_turns_begin), walked as the dispatcher walks it, at frame `frame`. */
void kp_turns_record(const KpTurns *t, KpRecorder *rec, int64_t frame, const KpCommand *list, int count);

#ifdef __cplusplus
}
#endif

#endif
