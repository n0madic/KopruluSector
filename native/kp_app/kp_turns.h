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
 * TODO: a network game's source (a third kind): its frame can be not ready yet (the step waits, the clock keeps no
 * missed steps), and its seats are the session's players; a viewer is REPLAY over a growing record.
 */
#ifndef KP_TURNS_H
#define KP_TURNS_H

#include <stddef.h>
#include <stdint.h>

#include "kp_game.h"
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

enum { KP_TURNS_LOCAL, KP_TURNS_REPLAY };
/* kp_turns_frame's status: the frame's commands; the replay's end, given once at the frame its record passed the
 * header's frame count (the live commands still come); out of memory (no commands). */
enum { KP_TURNS_READY, KP_TURNS_ENDED, KP_TURNS_NO_MEMORY };

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
	uint32_t feed_storm[KP_TURNS_SLOTS], feed_slot[KP_TURNS_SLOTS];
	int64_t feed_player;
	KpCommand *list; /* owned: the frame's commands kp_turns_frame gave */
	int list_cap;
} KpTurns;

/* A replay's source: its record loaded (BeginReplayPlayback 0x4789f0), its players' Storm IDs. t must be zeroed (a
 * zeroed KpTurns is a LOCAL source). -1 out of memory, t then LOCAL again. */
int kp_turns_replay(KpTurns *t, const KpRep *rep);
/* Once the game started: the local player, whom LOCAL's recording watches (Storm ID 0) and whom the feed's commands
 * run as until a Storm ID names a record. */
void kp_turns_begin(KpTurns *t, int64_t local_player);
void kp_turns_free(KpTurns *t);
int kp_turns_playing(const KpTurns *t);
/* The replay's end passed (kp_turns_frame gave KP_TURNS_ENDED); 0 for LOCAL. */
int kp_turns_ended(const KpTurns *t);
/* The header's frame count of the replay; 0 for LOCAL. */
uint32_t kp_turns_frames(const KpTurns *t);

/* A command of player for the next step: 0, or -1 when the queue is full, it is too long, or the playback does not let
 * it through (REPLAY: the keep-alive, restart, pause and resume, the sync, the network's 0x37-0x3b, latency and the
 * replay speed only, ExecuteTurnCommands 0x475d30's replay filter). */
int kp_turns_submit(KpTurns *t, const uint8_t *bytes, size_t size, int64_t player);
/* The commands of game frame `frame` in *list (owned by t, valid until kp_turns_done; the queued ones point into
 * the queue) and their count: KP_TURNS_READY, KP_TURNS_ENDED or KP_TURNS_NO_MEMORY (*count 0). */
int kp_turns_frame(KpTurns *t, int64_t frame, const KpCommand **list, int *count);
/* The frame's commands ran: the queue is empty. */
void kp_turns_done(KpTurns *t);
/* RecordCommand 0x478340 of a frame's executed commands before the frame's step: each command of a buffer whose player
 * has a Storm ID (kp_turns_begin), walked as the dispatcher walks it, at frame `frame`. */
void kp_turns_record(const KpTurns *t, KpRecorder *rec, int64_t frame, const KpCommand *list, int count);

#ifdef __cplusplus
}
#endif

#endif
