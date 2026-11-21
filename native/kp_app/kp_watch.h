/*
 * A network game's viewer (Koprulu's own; no 1.17 counterpart): the host of a network game serves its replay as it
 * grows, a viewer plays it as a replay whose record's end grows (kp_turns_watch, kp_session_watch_grow).
 *
 * The protocol, UDP on its own port (KP_WATCH_PORT, outside Storm's 6111/6112), little-endian, every datagram behind
 * the magic "KPW1" and a type byte:
 *
 *   - REQUEST {u32 game (0: not known yet), u8 part, u32 offset} padded to the size of the answer it asks for;
 *   - DATA {u32 game, u8 part, u32 offset, u32 the part's size, u32 frames, u8 final, u16 n, u32 CRC-32 of the n
 *     bytes, the n bytes}.
 *
 * Part 0 is the replay file at the game's start (header, empty record, CHK, tail: kp_rep_write), part 1 the record's
 * published bytes. The server keeps no state per viewer: each REQUEST gets one DATA, never larger than the REQUEST
 * (a forged source gains nothing from it), with the part's bytes from `offset` (none past the part's end: then the
 * answer only tells the size and the frames). A viewer keeps what follows what it holds and drops the rest, asks
 * again every KP_WATCH_POLL_MS (at once after new bytes), and gives up after KP_WATCH_TIMEOUT_MS without an answer
 * (KP_WATCH_FIRST_TIMEOUT_MS before the first: the host's game may not have started).
 * A REQUEST of another game is answered with the server's game and no bytes: the viewer stops.
 *
 * Publication. The host marks the record after each step (its frame count and its size then: chat lines are recorded
 * at the next frame, after the mark) and publishes the record as it stood `delay` frames ago: frames F, the record up
 * to the end of frame F - 1, which no command can change any more (the open block's size byte, written when it
 * closes, is set in the server's copy). A paused game's steps run no frame and mark nothing: what they record is the
 * next frame's. At the game's end the whole record is published, final. A save's load in the game takes its frame
 * back: the server then ends the game at what it published.
 */
#ifndef KP_WATCH_H
#define KP_WATCH_H

#include <stddef.h>
#include <stdint.h>

#include "kp_record.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KP_WATCH_PORT 6120            /* the server's UDP port unless told otherwise */
#define KP_WATCH_DELAY_SECONDS 60     /* how far behind the game the viewers are, by default */
#define KP_WATCH_MAGIC "KPW1"
#define KP_WATCH_MAGIC_SIZE 4
#define KP_WATCH_REQUEST 1
#define KP_WATCH_DATA 2
#define KP_WATCH_PART_START 0  /* the replay file at the game's start */
#define KP_WATCH_PART_RECORD 1 /* the published record */
#define KP_WATCH_PARTS 2
#define KP_WATCH_REQUEST_HEADER 14 /* magic, type, game, part, offset */
#define KP_WATCH_DATA_HEADER 29    /* magic, type, game, part, offset, size, frames, final, n, CRC */
/* The fields' offsets: the type, the fields a REQUEST and a DATA share, then a DATA's own */
#define KP_WATCH_AT_TYPE KP_WATCH_MAGIC_SIZE
#define KP_WATCH_AT_GAME 5
#define KP_WATCH_AT_PART 9
#define KP_WATCH_AT_OFFSET 10
#define KP_WATCH_AT_SIZE 14
#define KP_WATCH_AT_FRAMES 18
#define KP_WATCH_AT_FINAL 22
#define KP_WATCH_AT_COUNT 23
#define KP_WATCH_AT_CRC 25
#define KP_WATCH_CHUNK 1024        /* the most bytes one DATA carries */
#define KP_WATCH_SMALL 256         /* what a viewer that holds everything known asks for at once */
#define KP_WATCH_BURST 8           /* REQUESTs a viewer sends at once while it knows more bytes wait */
#define KP_WATCH_POLL_MS 100
#define KP_WATCH_TIMEOUT_MS 10000        /* no answer this long from a server that answered: gone */
#define KP_WATCH_FIRST_TIMEOUT_MS 300000 /* no answer at all this long: no game (a host still in its lobby has none) */
#define KP_WATCH_START_MAX (32u << 20) /* the largest start file a viewer takes */

/* A datagram back to where the REQUEST came from (the server), or to the server (the viewer). */
typedef void (*KpWatchSend)(void *ud, const uint8_t *data, size_t size);

typedef struct {
	uint32_t frames;
	size_t used;
} KpWatchMark;

typedef struct {
	uint32_t game; /* never 0 */
	uint8_t *start; /* owned */
	size_t start_size;
	uint8_t *record; /* owned: the published bytes */
	size_t record_size, record_cap;
	uint32_t frames; /* published: the record holds every block before this frame */
	int final;
	uint32_t delay; /* frames */
	KpWatchMark *marks; /* owned: the marks not published yet, oldest first from mark_head */
	size_t mark_head, mark_count, mark_cap;
	int marked;         /* a mark was taken */
	uint32_t last_mark; /* the frames of the last mark */
	long answers;
} KpWatchServer;

/* A game's server: its id (0 made 1), the start file (copied) and the delay in frames. -1 out of memory. */
int kp_watch_server_init(KpWatchServer *s, uint32_t game, const uint8_t *start, size_t start_size, uint32_t delay);
void kp_watch_server_free(KpWatchServer *s);
/* After a step: the recorder and its game's frame count. What the delay lets out is published. A step that ran no
 * frame (a paused game's) marks nothing; a frame count below the last mark's (a save's load) ends the game at what was
 * published. -1 out of memory (the server then publishes nothing more). */
int kp_watch_server_mark(KpWatchServer *s, const KpRecorder *rec, uint32_t frames);
/* The game's end: the whole record (NULL: what was published) and its frame count, final. -1 out of memory. */
int kp_watch_server_end(KpWatchServer *s, const KpRecorder *rec, uint32_t frames);
/* A datagram that arrived: a REQUEST is answered through send; anything else is ignored. */
void kp_watch_server_receive(KpWatchServer *s, const uint8_t *data, size_t size, KpWatchSend send, void *ud);

typedef enum {
	KP_WATCH_FETCHING, /* the start file is coming */
	KP_WATCH_LIVE,     /* the start file is here, the record grows */
	KP_WATCH_ENDED,    /* the record is whole (the game ended) */
	KP_WATCH_GONE,     /* no answer for KP_WATCH_TIMEOUT_MS, or the server went on to another game */
	KP_WATCH_FAILED    /* gone before the start file came (no answer at all for KP_WATCH_FIRST_TIMEOUT_MS), out of
	                    * memory, or a start file too large */
} KpWatchStatus;

typedef struct {
	KpWatchStatus status;
	uint32_t game;
	uint8_t *start; /* owned */
	size_t start_size, start_got;
	uint8_t *record; /* owned */
	size_t record_got, record_cap;
	size_t known[KP_WATCH_PARTS]; /* the parts' sizes the last answers told */
	size_t ready_size; /* the record's bytes whose frames are known */
	uint32_t ready_frames;
	int ready_final;
	size_t taken; /* given to the playback (kp_watch_viewer_take) */
	uint32_t taken_frames;
	int taken_whole;
	uint32_t last_request, last_heard;
	int asked, fresh;
} KpWatchViewer;

void kp_watch_viewer_init(KpWatchViewer *v, uint32_t now);
void kp_watch_viewer_free(KpWatchViewer *v);
/* The clock's turn: REQUESTs when due, the timeout. */
void kp_watch_viewer_poll(KpWatchViewer *v, uint32_t now, KpWatchSend send, void *ud);
/* A datagram from the server. */
void kp_watch_viewer_receive(KpWatchViewer *v, const uint8_t *data, size_t size, uint32_t now);
/* The start file once whole (else NULL). */
const uint8_t *kp_watch_viewer_start(const KpWatchViewer *v, size_t *size);
/* What the playback has not taken yet: 1 with the new bytes, the frames they let run and whether the record is now
 * whole (the game ended, or the server is gone: then at what came), 0 nothing new. */
int kp_watch_viewer_take(KpWatchViewer *v, const uint8_t **bytes, size_t *size, uint32_t *frames, int *whole);

/* The delay in frames for `seconds` at a frame's `step_ms` (kp_clock_step_ms); 0 for none. */
uint32_t kp_watch_delay_frames(double seconds, double step_ms);

#ifdef __cplusplus
}
#endif

#endif
