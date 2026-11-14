/*
 * The replay's command record (atlas P26 net_record.cpp): the recorder RecordCommand 0x478340 fills during a game,
 * and the playback feed FeedReplayCommands 0x4769b0 reads, with the original's buffers and quirks:
 *
 *   - blocks {DWORD frame, BYTE payload size, {BYTE Storm ID, command}*}: a block is closed when the next command
 *     would take its payload to 0xff bytes or the frame changes, an empty block is removed, a command of 0x100 bytes
 *     or more and the twelve ids of 0x4ef2fc are not recorded, a command that does not fit the buffer is dropped
 *     (50,000 bytes at first, 10,000 more whenever fewer than 0x200 are left);
 *   - saving closes the open block but keeps its frame (SaveRecordedCommands 0x478980), so commands of the same
 *     frame recorded later are appended without a new header;
 *   - the feed reads a frame one call early when the next recorded frame is ahead of the game, into the 0x200-byte
 *     static buffers; a frame that needs more grows a heap copy that the early return frees, so the next call
 *     dispatches the frame's count from the static buffers (1.17's command loss, docs/research/replay-network-notes.md);
 *   - the last recorded block is never dispatched (ReadRecordedFrame 0x478480 returns -1 at the end of the data
 *     without storing the count).
 *
 * Not modelled: a frame of more than 0x200 entries, whose ids the original writes past 0x672be8 into the command
 * bytes (KP_RECORD_MAX_ENTRIES ends the data there instead).
 *
 * The dispatcher's walk of a recorded entry (ExecuteTurnCommands 0x475d30 with "from replay" 1) is
 * kp_record_split: an entry may hold several commands or a cut one.
 */
#ifndef KP_RECORD_H
#define KP_RECORD_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KP_RECORD_START 50000 /* CreateRecorder 0x4788b0 */
#define KP_RECORD_GROW 10000  /* GrowRecordBuffer 0x4785c0 */
#define KP_RECORD_SCRATCH 0x200 /* FeedReplayCommands' static buffers 0x672de8 / 0x672fe8 / 0x672be8 */
#define KP_RECORD_MAX_ENTRIES 0x10000

/* One command buffer the feed hands to the dispatcher: the recorded Storm ID and the bytes (pointing into the
 * recorder). */
typedef struct {
	uint8_t storm;
	const uint8_t *bytes;
	size_t size;
} KpRecordEntry;

typedef struct KpRecorder {
	uint8_t *buf; /* owned */
	size_t used, cap;
	int recording, playing;
	size_t open;        /* the open block's payload start (0: none) */
	int64_t open_frame; /* its frame (-1 at first) */
	size_t cursor;      /* the playback's read position */
	/* FeedReplayCommands' state: the frame of the buffered commands (0x508d68, -1 none), their count (0x672980),
	 * the static buffers and whether the last read grew a heap record */
	int64_t buffered_frame;
	int count;
	uint8_t ids[KP_RECORD_MAX_ENTRIES];
	uint8_t data[KP_RECORD_SCRATCH];
	uint32_t lengths[KP_RECORD_SCRATCH];
	uint8_t *exec; /* owned: the bytes of the entries kp_record_feed returned last */
	size_t exec_size, exec_cap;
	KpRecordEntry *entries; /* owned: the entries kp_record_feed returned last */
	int entries_cap;
} KpRecorder;

/* CreateRecorder 0x4788b0: an empty record, recording. -1 out of memory. */
int kp_record_init(KpRecorder *r);
void kp_record_free(KpRecorder *r);
/* RecordCommand 0x478340: one command (its id byte first) of Storm player `storm` at `frame`. */
void kp_record_command(KpRecorder *r, int64_t frame, uint8_t storm, const uint8_t *cmd, size_t size);
/* SaveRecordedCommands 0x478980's first step: closes the open block (its size byte), keeping its frame. Done before
 * the record's bytes are taken for a save or a replay file. */
void kp_record_close(KpRecorder *r);
/* The record as the file holds it (closed first). */
const uint8_t *kp_record_bytes(KpRecorder *r, size_t *size);
/* LoadRecordedCommands 0x478640 then BeginReplayPlayback 0x4789f0 and ResetReplayFeed 0x476d80: plays `bytes`. -1
 * out of memory. */
int kp_record_load(KpRecorder *r, const uint8_t *bytes, size_t size);
/* A recorder's state from a save: the bytes (saved with the open block closed, kp_record_close) and the frame the closed
 * block kept, which the same frame's next commands go on without a header for. -1 out of memory, r unchanged. */
int kp_record_restore(KpRecorder *r, const uint8_t *bytes, size_t size, int64_t open_frame);

/* FeedReplayCommands 0x4769b0 at frame f (before the frame's step, as RunPendingFrames calls it): returns 1 when
 * the replay ended (f past `frames`, the header's count), else 0 with the buffers to dispatch now in *out (owned by
 * r, valid until the next call; *count of them, 0 also when out of memory). Each entry is one ExecuteTurnCommands
 * call's buffer. */
int kp_record_feed(KpRecorder *r, int64_t f, uint32_t frames, const KpRecordEntry **out, int *count);

/* The dispatcher's walk of one buffer (0x475d30): calls fn for each whole command until the buffer ends, an id
 * outside its table stops it or a command does not fit what is left. Returns the commands given. */
int kp_record_split(const uint8_t *b, size_t size, void (*fn)(void *ud, const uint8_t *cmd, size_t n), void *ud);
/* 1 for an id the recorder never writes (RecordCommand 0x478340's table 0x4ef2fc: save, load, restart, the playback's
 * own commands); 0 otherwise. */
int kp_record_excluded(uint8_t id);
/* The original's length of command `b` with its id byte (0x4720c0, 0x4ef048), -1 for an id it does not know. */
int kp_record_command_length(const uint8_t *b, size_t size);

#ifdef __cplusplus
}
#endif

#endif
