/*
 * The cinematics' pack (generated/video/cinematics-<sd|hd2>.kpd, kind CINE, docs/formats/packs.md; ADR 0015) and its
 * player: the index read from the file's head and its small sections, and a player that decodes one movie's VP9
 * packets with libvpx into RGBA frames at the times the host asks for. Headless: the host reads the file through a
 * callback and plays the movie's Ogg Vorbis (kp_cine_audio) with its own audio; it asks for the picture by the clock
 * (the sound's position). A run (KpCineRun) plays the host's queue of movies one after the other, a press ending the
 * one shown; the host draws its pictures (libvpx, `tools/toolchain/fetch.sh vpx`).
 */
#ifndef KP_CINE_H
#define KP_CINE_H

#include <stddef.h>
#include <stdint.h>

#include "kp_kpf.h"
#include "kp_present.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KP_CINE_KIND "CINE"
#define KP_CINE_SCHEMA 2 /* converter/emit/cinematics.py's SCHEMA: SUBS since 2 */
#define KP_CINE_SCHEMA_OLDEST 1 /* a pack of schema 1 is read too: its movies have no subtitles */
#define KP_CINE_CLIPS 32
#define KP_CINE_PRELUDES 16
#define KP_CINE_HEAD 4096 /* the file's first bytes the index needs: the header and the section table */
#define KP_CINE_MAX_SIDE 4096 /* the largest coded width or height a clip may have */
#define KP_CINE_MAX_RATE_TERM 1000000u /* the largest frame rate numerator or denominator (the times' sums stay in 64 bits) */
#define KP_CINE_CUES 128 /* a clip's subtitles at most (the converter's CUES_MAX) */
#define KP_CINE_CUE_TEXT 16384 /* and the bytes of their text (CUE_TEXT_MAX) */

typedef struct {
	char name[65]; /* "smk\<name>", lower case */
	uint8_t id; /* the original's cinematic number (0x508da8's index, 0..27) */
	uint16_t width, height; /* the coded frames */
	uint16_t shown_w, shown_h; /* the size they are shown at (an SD movie coded 320 wide is shown 640: a pixel of 2:1) */
	uint32_t rate_num, rate_den; /* frames per second */
	uint32_t frames;
	uint64_t video_offset; /* the packets, one after the other, in the file */
	uint32_t video_size;
	uint64_t frame_table; /* the file offset of the frames' u32 sizes (bit 31: a key frame) */
	uint64_t audio_offset; /* the movie's Ogg Vorbis */
	uint32_t audio_size; /* 0: no sound */
	uint32_t cue_first, cue_count; /* its subtitles in SUBS (count 0: none) */
} KpCineClip;

typedef struct {
	int variant; /* 0 SD, 1 HD2 */
	KpCineClip clips[KP_CINE_CLIPS];
	int count;
	uint8_t preludes[KP_CINE_PRELUDES][2]; /* {cinematic, prelude}: the prelude is played before the cinematic */
	int prelude_count;
	char subtitles[5];      /* SUBS's locale ("ruRU"), "" when the pack has none */
	uint64_t cues_at;       /* the file offset of SUBS's cues (16 bytes each) */
	uint64_t text_at;       /* and of their text */
	uint32_t text_size;     /* the text's bytes */
	const uint8_t *subs_overlay; /* a language's SUBS in memory: cues_at and text_at are offsets in it, else NULL */
} KpCineIndex;

#define KP_CINE_READ_WAIT 1 /* a read's bytes are on their way (the web's fetches): the player asks again later */

/* Reads size bytes at offset of the file into out; 0 on success, KP_CINE_READ_WAIT when they have not come yet (only
 * the packets' reads may answer it: the index, the frame tables and the subtitles are read at once), else a failure. */
typedef int (*KpCineRead)(void *ctx, uint64_t offset, uint8_t *out, size_t size);

/* The index of the pack of file_size bytes whose first head_size are in head (the sections there are taken from it,
 * the others read); -1 with err (and no clips) when it is not a CINE container of KP_CINE_SCHEMA_OLDEST to
 * KP_CINE_SCHEMA, a section is missing or cut short, a clip lies outside DATA or FRMS, has a side of 0 or above
 * KP_CINE_MAX_SIDE, a rate term of 0 or above KP_CINE_MAX_RATE_TERM, or a clip number repeats, or its SUBS (when there
 * is one) does not add up: its size, the clips' cues one after the other, more than KP_CINE_CUES for a clip. */
int kp_cine_index(KpCineIndex *ix, const uint8_t *head, size_t head_size, size_t file_size, KpCineRead read, void *ctx,
	KpError *err);

/* The clip of cinematic id (the original's number), or -1. */
int kp_cine_find(const KpCineIndex *ix, int id);

/* The prelude of cinematic id (PlayCinematicWithPrelude 0x458fc0 scans its table from the last entry to the first), or -1. */
int kp_cine_prelude(const KpCineIndex *ix, int id);

/* A subtitle: its time and its UTF-8 text (lines apart by '\n') in its KpCineSubs's text. */
typedef struct {
	uint32_t start_ms, end_ms;
	uint32_t at, size;
} KpCineCue;

/* A clip's subtitles (Remastered's .srt, by the converter), by their start. */
typedef struct {
	KpCineCue cues[KP_CINE_CUES];
	int count;
	char text[KP_CINE_CUE_TEXT];
} KpCineSubs;

/* Clip k's subtitles into s (count 0: it has none); -1 with err (and none) when they cannot be read, a cue ends
 * before it starts or starts before the one before it, or their text runs past SUBS's or spans more than
 * KP_CINE_CUE_TEXT bytes. */
int kp_cine_subs_load(const KpCineIndex *ix, int k, KpCineRead read, void *ctx, KpCineSubs *s, KpError *err);
/* A language's subtitles over the index (generated/locale/<loc>/cinematics.kpd, kept, not copied: SUBS with a row for
 * every movie of the table, by clip number; ADR 0016): 0, or -1 with err and the pack's own left. */
int kp_cine_subs_overlay(KpCineIndex *ix, const uint8_t *data, size_t size, KpError *err);
/* The subtitle shown at ms: the last begun at or before it that has not ended (end_ms excluded), or -1. */
int kp_cine_subs_at(const KpCineSubs *s, uint64_t ms);

/* The pack's file read over asynchronous requests of its byte ranges (the web, ADR 0015 item 4): the first head_size
 * bytes at hand (the index, the frame tables, the subtitles), the rest fetched chunk by chunk, a few chunks ahead of the
 * reads once a chunk has come (a server that answers a range with the whole file costs that file once, not once a
 * request). The fetcher's request is the host's: start gives it (NULL: failed), state answers 0 while it is out, above 0
 * when it has come, below 0 when it failed; take gives its bytes (malloc'ed, the caller's) and ends it; drop ends it
 * unread, whatever its state. */
#define KP_CINE_STREAM_CHUNK (1024u * 1024u)
#define KP_CINE_STREAM_SLOTS 4 /* the chunk the read begins in and the ones after it: at least 3 MB ahead of a read */

typedef struct {
	void *ctx;
	void *(*start)(void *ctx, uint64_t offset, uint32_t size);
	int (*state)(void *ctx, void *request);
	uint8_t *(*take)(void *ctx, void *request, size_t *size);
	void (*drop)(void *ctx, void *request);
} KpCineFetcher;

typedef struct {
	KpCineFetcher fetcher;
	const uint8_t *head; /* the caller's, outliving the stream */
	size_t head_size;
	uint64_t file_size;
	int chunk_came; /* the ranges are answered: the chunks ahead may be asked for */
	struct {
		int used, failed;
		uint64_t chunk;
		void *request; /* out */
		uint8_t *data; /* come */
	} slots[KP_CINE_STREAM_SLOTS];
} KpCineStream;

/* A KpCineRead over the stream (ctx): 0 with the bytes when they are in the head or their chunks have come,
 * KP_CINE_READ_WAIT when not yet, -1 past the file's end, on a chunk that failed (no second try) or on a read longer than
 * the slots hold. Chunks before the read and past the slots are let go, so the reads go forward. */
int kp_cine_stream_read(void *ctx, uint64_t offset, uint8_t *out, size_t size);
/* The requests still out dropped, the chunks freed. */
void kp_cine_stream_free(KpCineStream *s);

/* The time frame n is due at, in milliseconds (rounded up: at that time kp_cine_advance has decoded frame n). */
uint64_t kp_cine_frame_ms(const KpCineClip *clip, uint32_t n);

/* The movies cinematics ids[0..n) play, one after the other, into queue: each cinematic the index has, after its
 * prelude (kp_cine_prelude) when preludes is set and the index has that too (a prelude without its cinematic is left
 * out); the first of them that fit in cap (a cinematic and its prelude both or neither, none after one left out), their
 * count, and all of them in *wanted (when not NULL). */
int kp_cine_queue(const KpCineIndex *ix, const int *ids, int n, int preludes, int *queue, int cap, int *wanted);

#define KP_CINE_AUDIO_WAIT_MS 300 /* a sound that has not begun by then leaves the movie to the wall clock until it does */
#define KP_CINE_CLOCK_GAP_MS 250  /* a longer pause between two frames (the app in the background) is not the movie's time */

/* A movie's clock: the sound's position while it plays (also when it began late: the pictures wait for it), else the
 * wall clock from where the sound left the movie or from its begin; never backwards. */
typedef struct {
	uint64_t t0;                            /* the ticks when the movie began */
	int audio;                              /* the movie has a sound that may keep its clock */
	uint64_t wall_base_ms, wall_base_ticks; /* the wall clock's start: where the sound left the movie, or its begin */
	uint64_t last_ms, last_ticks;           /* the movie's time and the ticks of the frame that asked for it */
} KpCineClock;

void kp_cine_clock_begin(KpCineClock *c, uint64_t ticks, int audio);
/* The movie's time at ticks (milliseconds): playing, whether the sound plays, and sound_ms its position (0 or less:
 * none yet); read only while c->audio is set (it is cleared once the sound has ended or never began). A pause of more
 * than KP_CINE_CLOCK_GAP_MS since the last call does not count. */
uint64_t kp_cine_clock_ms(KpCineClock *c, uint64_t ticks, int playing, int64_t sound_ms);
/* The time since the last call (of either) went in waiting for the movie's data: none of it is the movie's. */
void kp_cine_clock_hold(KpCineClock *c, uint64_t ticks);

typedef struct KpCinePlayer KpCinePlayer;

enum { KP_CINE_IDLE, KP_CINE_FRAME, KP_CINE_END, KP_CINE_WAIT };

/* A player of clip k of the index: the frame table is read, the decoder started. NULL with err when the clip cannot be
 * read, its table disagrees with its size (the packets do not add up to video_size, the first is not a key frame), or the
 * decoder does not start. The index and the callback must outlive it. */
KpCinePlayer *kp_cine_open(const KpCineIndex *ix, int k, KpCineRead read, void *ctx, KpError *err);
void kp_cine_free(KpCinePlayer *p);

/* Brings the picture to time_ms: decodes the packets due (all of them, a little at a time: a movie behind the clock
 * catches up over several calls), KP_CINE_FRAME when a new picture is there (kp_cine_picture, kp_cine_rgba),
 * KP_CINE_IDLE when it is not, KP_CINE_END once the last frame's time has passed, KP_CINE_WAIT when a packet due has not
 * come (KP_CINE_READ_WAIT; nothing lost, the next call goes on from it), -1 with err on a packet that cannot be read or decoded or a picture that is not 8-bit planar: 4:4:4 (sRGB or YCbCr), 4:4:0, 4:2:2 or 4:2:0. */
int kp_cine_advance(KpCinePlayer *p, uint64_t time_ms, KpError *err);

/* The picture of the last KP_CINE_FRAME as its planes (KpVideoPicture: Y, Cb, Cr or G, B, R, the matrix of
 * kp_cine_rgba) into out, valid until the next kp_cine_advance; -1 when a packet decoded since gave none. */
int kp_cine_picture(const KpCinePlayer *p, KpVideoPicture *out);
/* The same picture as RGBA (width x height, 4 bytes a pixel, opaque) converted on the CPU at the first call (the checks'
 * reference and pictures); NULL when there is none. */
const uint8_t *kp_cine_rgba(KpCinePlayer *p);
const KpCineClip *kp_cine_clip(const KpCinePlayer *p);
uint32_t kp_cine_decoded(const KpCinePlayer *p); /* the packets decoded so far */

/* --- a run of movies: the host's cinematics, one after the other (PlayCinematic 0x47ec60's loop) --- */

/* the movies queued: a mission history's cinematic and the campaign step's rows after it (1 + kp_flow.h's
 * KP_APP_STEP_CINEMATICS), each after its prelude */
#define KP_CINE_RUN_QUEUE (2 * (1 + 16))

/* What a run asks of its host (every callback set): the pack's file for the packets' reads, open while movies play;
 * the run's begin and end (the music and the input are the host's); each movie's sound; the log. */
typedef struct {
	void *ctx;
	uint64_t (*now_ns)(void *ctx);                   /* the clock of the host's events' timestamps, above 0 (0 is
	                                                  * the run's "no picture shown yet") */
	KpCineRead read;                                 /* reads the file open gave (as its ctx) */
	void *(*open)(void *ctx);                        /* the pack's file, NULL when it does not open (the host logs) */
	void (*close)(void *ctx, void *file);
	void (*begin)(void *ctx);                        /* a run begins: before its first movie's sound */
	void (*end)(void *ctx);                          /* the run ended: its last sound stopped, its file closed */
	int (*sound)(void *ctx, const KpCineClip *clip); /* a movie's sound begun (1), or not (0: the wall clock's movie) */
	int (*sound_ms)(void *ctx, int64_t *ms);         /* whether the sound plays, its position in ms (0 or less: none yet) */
	void (*sound_stop)(void *ctx);                   /* the movie's sound stopped and released (none: nothing) */
	void (*sound_pause)(void *ctx, int pause);       /* the movie waits for its data (1), or goes on (0) */
	void (*log)(void *ctx, const char *line);
} KpCineHost;

/* The movies of a run on the host's clock (now_ns): the queue, the one playing, its clock (that clock's milliseconds,
 * the sound's position) and the press that may end it. */
typedef struct {
	KpCineHost host;
	KpCineIndex index;
	int have;                 /* the index is the pack's (the host read it); the host's 0 later: no more movies */
	void *file;               /* open while movies play */
	KpCinePlayer *player;     /* the movie playing (its clip gives the picture's size and shape) */
	int queue[KP_CINE_RUN_QUEUE], queue_len, queue_pos;
	KpCineClock clock;
	int active;
	uint64_t begun_ms;        /* when the movie began (the log's wall: its clock leaves the waits out) */
	uint64_t drawn_ns;        /* when the movie's first picture was shown, 0 not yet */
	uint64_t press_ns;        /* a press from then on ends the movie, 0 none yet: its first picture or its first wait
	                           * (not one before it, for the movie before: two fingers, both buttons) */
	uint32_t shown;           /* pictures shown (the checks) */
	uint32_t movies;          /* the movies begun: a new one's subtitles are new even at the same cue */
	uint64_t time_ms;         /* the movie's time at the last kp_cine_run_frame */
	int waiting;              /* its packets have not come: its clock and its sound held */
	KpCineSubs subs;          /* the movie's subtitles */
} KpCineRun;

/* Whether the pack has cinematic id. */
int kp_cine_run_has(const KpCineRun *r, int id);
/* The movies of cinematics ids[0..n) (kp_cine_queue; the ones past the queue's end logged) after the ones playing,
 * or a run of them begun (the file opened, the host's begin, the first that opens begun with its sound; the ones that
 * do not open logged); 1 when a movie was queued or begun, 0 when none was (nothing changed). */
int kp_cine_run_start(KpCineRun *r, const int *ids, int n, int preludes);
/* The picture of the movie playing now: 1 with a new one in pic (valid until the next call; the host draws it, then
 * calls kp_cine_run_shown), 0 with none (the last one shown stays, black while drawn_ns is 0), also while its packets
 * have not come (its clock and its sound held). A movie that ended, or whose packet does not decode or cannot be read
 * (logged), makes way for the next of the queue, or the run ends. */
int kp_cine_run_frame(KpCineRun *r, KpVideoPicture *pic);
/* The subtitle of the movie playing at its time of the last kp_cine_run_frame (an index of r->subs.cues), or -1. */
int kp_cine_run_cue(const KpCineRun *r);
/* The new picture shown: counted, and the first one's time kept. */
void kp_cine_run_shown(KpCineRun *r);
/* A press at when_ns ends the movie shown, not one made before its first picture (the press that ended the movie
 * before it, the other fingers and buttons of that moment); any press ends a movie that waits for its data, which may
 * have no picture to show for a long time. */
void kp_cine_run_skip(KpCineRun *r, uint64_t when_ns);
/* Every movie of the run ended (the host's end), none queued. */
void kp_cine_run_stop(KpCineRun *r);

#ifdef __cplusplus
}
#endif

#endif
