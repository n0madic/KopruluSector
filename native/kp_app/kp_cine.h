/*
 * The cinematics' player (ADR 0015; ADR 0018 stage D7, ADR 0019 stage C4): the profile's movies (CINM) as an index of
 * the ones the data has, each movie loaded whole by the host when it begins: a 1.17 install's Smacker file (its frames
 * through their palettes, its sound decoded into a WAV), or a WebM by Remastered's name (an install's, or a repack's,
 * which keeps a 1.17 install's movies transcoded: its VP9 packets decoded with libvpx, its Vorbis sound as an Ogg
 * stream), with its subtitles from Remastered's .srt. The player decodes one movie's frames into pictures at the times
 * the host asks for. Headless: the host plays the movie's sound with its own audio and asks for the picture by the
 * clock (the sound's position). A run (KpCineRun) plays the host's queue of movies one after the other, a press ending
 * the one shown; the host draws its pictures (libvpx, `tools/toolchain/fetch.sh vpx`).
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

#define KP_CINE_CLIPS 32
#define KP_CINE_PRELUDES 16
#define KP_CINE_MAX_SIDE 4096 /* the largest coded width or height a movie may have */
#define KP_CINE_MAX_RATE_TERM 1000000u /* the largest frame rate numerator or denominator (the times' sums stay in 64 bits) */
#define KP_CINE_CUES 128 /* a movie's subtitles at most */
#define KP_CINE_CUE_TEXT 16384 /* and the bytes of their text */

typedef struct {
	char name[65]; /* "smk\<name>", lower case */
	uint8_t id; /* the original's cinematic number (0x508da8's index, 0..27) */
	uint16_t width, height; /* the coded frames */
	uint16_t shown_w, shown_h; /* the size they are shown at (an SD movie coded 320 wide is shown 640: a pixel of 2:1) */
	uint32_t rate_num, rate_den; /* frames per second */
	uint32_t frames;           /* the index's 0: the movie loaded gives its own */
	uint32_t audio_size;       /* its sound's bytes, 0 none (the movie's) */
	uint32_t cue_count;        /* its subtitles (the movie's) */
} KpCineClip;

typedef struct {
	int variant; /* 0 SD, 1 HD2 */
	KpCineClip clips[KP_CINE_CLIPS];
	int count;
	uint8_t preludes[KP_CINE_PRELUDES][2]; /* {cinematic, prelude}: the prelude is played before the cinematic */
	int prelude_count;
} KpCineIndex;


/* The clip of cinematic id (the original's number), or -1. */
int kp_cine_find(const KpCineIndex *ix, int id);

/* The prelude of cinematic id (PlayCinematicWithPrelude 0x458fc0 scans its table from the last entry to the first), or -1. */
int kp_cine_prelude(const KpCineIndex *ix, int id);

/* A subtitle: its time and its UTF-8 text (lines apart by '\n') in its KpCineSubs's text. */
typedef struct {
	uint32_t start_ms, end_ms;
	uint32_t at, size;
} KpCineCue;

/* A movie's subtitles (Remastered's .srt), by their start. */
typedef struct {
	KpCineCue cues[KP_CINE_CUES];
	int count;
	char text[KP_CINE_CUE_TEXT];
} KpCineSubs;

/* The subtitle shown at ms: the last begun at or before it that has not ended (end_ms excluded), or -1. */
int kp_cine_subs_at(const KpCineSubs *s, uint64_t ms);

/* --- the movies: 1.17's Smacker files, WebM by Remastered's names (ADR 0018, stage D7; ADR 0019, stage C4) --- */

#define KP_CINE_NAME 32 /* a movie's name in the profile's table ("Terran1") with its NUL */
/* Remastered's movies by their raw names, a set's folder (variant 0 SD, 1 HD2) and the name in lower case; a repack
 * keeps a 1.17 install's movies, transcoded (kp_convert: kp_movie.h), as Remastered's SD set (ADR 0019, stage C4). */
#define KP_CINE_WEBM_FORMAT "%s%s.webm"
extern const char *const KP_CINE_WEBM_FOLDER[2];

/* The profile's movies (the compiled CINM: defs/campaign/cinematics.json): each one's name, the size and SHA-256 of its
 * copy in the reference 1.17 install, and the {cinematic, prelude} pairs. */
typedef struct {
	struct {
		char name[KP_CINE_NAME];
		uint32_t size;
		uint8_t sha256[32];
	} movies[KP_CINE_CLIPS];
	int count;
	uint8_t preludes[KP_CINE_PRELUDES][2];
	int prelude_count;
} KpCineCatalog;

/* The catalog of a CINM table: 0, or -1 when it is cut short, too long or names a prelude past its movies. */
int kp_cine_catalog(KpCineCatalog *c, const uint8_t *table, size_t size);
/* Whether a copy of size bytes is taken for movie k: its size is the reference copy's (a movie only shows and plays,
 * so it is not hashed when it loads; ADR 0018): 0, or -1. */
int kp_cine_catalog_check(const KpCineCatalog *c, int k, size_t size);
/* The index of the data's movies (variant 0 SD or 1 HD2): a clip "smk\<name>" numbered as the catalog for each movie
 * has(ctx, k) finds, its size, rate and frames those of the movie the host loads (KpCineHost.load); the catalog's
 * preludes. */
void kp_cine_index_movies(KpCineIndex *ix, int variant, const KpCineCatalog *c, int (*has)(void *ctx, int k), void *ctx);

/* The subtitles of a SubRip file (Remastered's locales/<locale>/Assets/Smk/<name>.srt): UTF-8 (a BOM dropped), blocks apart by a blank line, each its number, its times (H:MM:SS,mmm --> ...,
 * '.' too) and its text (its lines right-stripped, a cue without text left out), by their start (equal ones in the
 * file's order). 0, or -1 with err (and none) for a block that does not read, a cue that ends before it starts, more
 * than KP_CINE_CUES cues or KP_CINE_CUE_TEXT bytes of text. */
int kp_cine_subs_srt(const uint8_t *data, size_t size, KpCineSubs *s, KpError *err);

typedef struct KpCineMovie KpCineMovie;

/* A Smacker movie in memory (data malloc'ed, taken: freed with the movie, or at once when NULL is returned) named as
 * the index's clip: its size, its rate (the header's frame time), its frames (a ring movie's extra frame left out), its
 * first sound track decoded whole into a WAV file. NULL with err when it is no Smacker 2 or 4 file, is larger than the
 * player takes, or its sound does not decode. */
KpCineMovie *kp_cine_movie_smk(uint8_t *data, size_t size, const KpCineClip *named, KpError *err);
/* A WebM in memory (read, not kept; kp_webm_streams): its VP9 packets and frame table, its rate (the track's frame
 * duration, else its frames' times), its Vorbis sound as an Ogg stream. NULL with err when it does not demux, is
 * larger than the player takes, or a frame's time strays more than half a frame from the constant rate. */
KpCineMovie *kp_cine_movie_webm(const uint8_t *data, size_t size, const KpCineClip *named, KpError *err);
void kp_cine_movie_free(KpCineMovie *m);
const KpCineClip *kp_cine_movie_clip(const KpCineMovie *m);
/* The movie's sound as a file in memory (a WAV, an Ogg Vorbis) for the host's mixer, or NULL with none. */
const uint8_t *kp_cine_movie_sound(const KpCineMovie *m, size_t *size);
/* The movie's subtitles from a SubRip file (kp_cine_subs_srt): 0, or -1 with err and none. */
int kp_cine_movie_subtitles(KpCineMovie *m, const uint8_t *srt, size_t size, KpError *err);
const KpCineSubs *kp_cine_movie_subs(const KpCineMovie *m);


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

typedef struct KpCinePlayer KpCinePlayer;

enum { KP_CINE_IDLE, KP_CINE_FRAME, KP_CINE_END };

/* A player of a movie (which must outlive it; one player at a time): an SMK's frames are decoded by kp_smk from its
 * first, its picture the indices through the frame's palette as 4:4:4 sRGB planes (G, B, R); a WebM's VP9 packets are
 * decoded in place. NULL with err when its frame table disagrees with its packets (an empty packet, a first frame that
 * is not a key frame) or the decoder does not start. */
KpCinePlayer *kp_cine_open_movie(KpCineMovie *m, KpError *err);
void kp_cine_free(KpCinePlayer *p);

/* Brings the picture to time_ms: decodes the packets due (all of them, a little at a time: a movie behind the clock
 * catches up over several calls), KP_CINE_FRAME when a new picture is there (kp_cine_picture, kp_cine_rgba),
 * KP_CINE_IDLE when it is not, KP_CINE_END once the last frame's time has passed, -1 with err on a packet that does not
 * decode or a picture that is not 8-bit planar: 4:4:4 (sRGB or YCbCr), 4:4:0, 4:2:2 or 4:2:0. */
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

#define KP_CINE_LOAD_WAIT 1 /* KpCineHost.load: the movie's bytes are on their way (the web's fetch); asked again later */

/* What a run asks of its host (every callback set): the run's begin and end (the music and the input are the host's);
 * each movie, loaded whole; its sound; the log. */
typedef struct {
	void *ctx;
	uint64_t (*now_ns)(void *ctx);                   /* the clock of the host's events' timestamps, above 0 (0 is
	                                                  * the run's "no picture shown yet") */
	void (*begin)(void *ctx);                        /* a run begins: before its first movie's sound */
	void (*end)(void *ctx);                          /* the run ended: its last sound stopped (and a load the host
	                                                  * had begun, given up) */
	/* the clip's movie into *movie: 0, KP_CINE_LOAD_WAIT while it comes (the run asks for the same clip each frame
	 * until it has, or for another one: the host gives the first up), -1 with err (logged by the run) */
	int (*load)(void *ctx, const KpCineClip *clip, KpCineMovie **movie, KpError *err);
	/* a movie's sound begun (1), or not (0: the wall clock's movie): data, size a WAV or an Ogg Vorbis file in memory,
	 * the movie's until sound_stop */
	int (*sound)(void *ctx, const KpCineClip *clip, const uint8_t *data, size_t size);
	int (*sound_ms)(void *ctx, int64_t *ms);         /* whether the sound plays, its position in ms (0 or less: none yet) */
	void (*sound_stop)(void *ctx);                   /* the movie's sound stopped and released (none: nothing) */
	void (*log)(void *ctx, const char *line);
} KpCineHost;

/* The movies of a run on the host's clock (now_ns): the queue, the one playing, its clock (that clock's milliseconds,
 * the sound's position) and the press that may end it. */
typedef struct {
	KpCineHost host;
	KpCineIndex index;
	int have;                 /* the index is the data's (the host made it) */
	KpCinePlayer *player;     /* the movie playing (its clip gives the picture's size and shape) */
	KpCineMovie *movie;       /* and the movie itself (its sound's bytes), freed after its sound stopped */
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
	int loading;              /* the movie at queue_pos is on its way (KP_CINE_LOAD_WAIT): no player, no sound yet */
	KpCineSubs subs;          /* the movie's subtitles */
} KpCineRun;

/* Whether the data has cinematic id. */
int kp_cine_run_has(const KpCineRun *r, int id);
/* The movies of cinematics ids[0..n) (kp_cine_queue; the ones past the queue's end logged) after the ones playing,
 * or a run of them begun (the host's begin, the first that loads begun with its sound, or loading; the ones that do not
 * load logged); 1 when a movie was queued or begun, 0 when none was (nothing changed). */
int kp_cine_run_start(KpCineRun *r, const int *ids, int n, int preludes);
/* The picture of the movie playing now: 1 with a new one in pic (valid until the next call; the host draws it, then
 * calls kp_cine_run_shown), 0 with none (the last one shown stays, black while drawn_ns is 0), also while the movie
 * loads (it begins once it has). A movie that ended, or that does not load or whose packet does not decode (logged),
 * makes way for the next of the queue, or the run ends. */
int kp_cine_run_frame(KpCineRun *r, KpVideoPicture *pic);
/* The subtitle of the movie playing at its time of the last kp_cine_run_frame (an index of r->subs.cues), or -1. */
int kp_cine_run_cue(const KpCineRun *r);
/* The new picture shown: counted, and the first one's time kept. */
void kp_cine_run_shown(KpCineRun *r);
/* A press at when_ns ends the movie shown, not one made before its first picture (the press that ended the movie
 * before it, the other fingers and buttons of that moment); any press ends a movie that loads, which may have no
 * picture to show for a long time. */
void kp_cine_run_skip(KpCineRun *r, uint64_t when_ns);
/* Every movie of the run ended (the host's end), none queued. */
void kp_cine_run_stop(KpCineRun *r);

#ifdef __cplusplus
}
#endif

#endif
