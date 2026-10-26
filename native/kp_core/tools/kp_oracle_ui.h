/*
 * The interface's oracle (plan phase S, stage S1): an interface (kp_ui) with the converted files it keeps pointers
 * into, the console's colours over a mission's tileset, the console over a replayed game run where the emulator's input
 * log says the original ran its interface (tools/hud_log.py's entries), and the trace streams the glue's replays
 * compare (tools/room_check.py's records).
 */
#ifndef KP_ORACLE_UI_H
#define KP_ORACLE_UI_H

#include "kp_oracle.h"
#include "../src/core/kp_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The files an interface may read (generated/data/<name>), as flags of kp_oracle_ui_new. */
enum {
	KP_ORACLE_UI_STRINGS = 1, KP_ORACLE_UI_PORTRAITS = 2, KP_ORACLE_UI_SOUNDS = 4, KP_ORACLE_UI_CHEATS = 8,
	KP_ORACLE_UI_CAMPAIGN = 16
};

typedef struct {
	KpUi *ui;
	uint8_t *files[6]; /* ui.kpd, fonts.kpd, strings.kpd, portraits.kpd, sounds.kpd, campaign.kpd */
	size_t sizes[6];
	/* the console's colours' files (kp_oracle_ui_colours) */
	uint8_t *tileset, *palettes;
	size_t tileset_size, palettes_size;
	uint32_t *ticks[3]; /* the clocks kp_ui reads (set ticks, video ticks, page deadlines; kp_oracle_trace_clocks) */
	uint32_t *time_seeds;
} KpOracleUi;

/* The interface over generated/data/ui.kpd and fonts.kpd under root, with the files of flags; -1 with the error. */
int kp_oracle_ui_new(KpOracleUi *u, const char *root, int flags, KpError *err);
void kp_oracle_ui_free(KpOracleUi *u);
/* A section of campaign.kpd (KP_ORACLE_UI_CAMPAIGN or _CHEATS loaded it), or NULL. */
const uint8_t *kp_oracle_ui_campaign_section(const KpOracleUi *u, const char *fourcc, size_t *size);

/* The tileset's palette and the indices it colour-cycles (tilesets/<name>.kpd WPE and CYCL; kp_tileset.c). */
int kp_oracle_tileset_colours(const uint8_t *tileset, size_t size, uint8_t rgb[768], uint8_t cycled[256], KpError *err);

/* The console's colours over the mission's tileset (the UI colours and the in-game dialogs' blend tables), and with
 * minimap the minimap's data too (the tileset's MMNI, the palettes' dark rows of the tileset and the mission's colour
 * slots). */
int kp_oracle_ui_colours(KpOracleUi *u, const char *root, const KpMission *m, int minimap, KpError *err);

/* The console's theme of a race (dlgs\<race>.grp; any other race: Terran's). */
const char *kp_oracle_race_theme(int64_t race);

/* --- the console over a replayed game --- */

/* tools/hud_log.py's entry kinds (an entry: frame, kind, values...). */
enum {
	KP_HUD_PASS = 1, KP_HUD_PRESENT, KP_HUD_HUD, KP_HUD_TALK, KP_HUD_SELECT, KP_HUD_GAME_CALL, KP_HUD_CURSOR,
	KP_HUD_INPUT, KP_HUD_COMMAND, KP_HUD_CURSOR_CHOICE, KP_HUD_SCROLL, KP_HUD_TIP, KP_HUD_SOUND
};
#define KP_HUD_VALUES 24
#define KP_HUD_BINS 8

typedef struct {
	int64_t v[KP_HUD_VALUES]; /* v[0] the frame, v[1] the kind, then its values */
	int count;
} KpHudEntry;

/* tools/hud_log.py's to_text lines: the console's creation tick, the cheat word, the console dialogs' first timer
 * ticks and the entries. */
typedef struct {
	int64_t create, cheats;
	int has_create, has_cheats;
	char bins[KP_HUD_BINS][32];
	int64_t bin_ticks[KP_HUD_BINS];
	int bin_count;
	KpHudEntry *entries;
	int count, cap;
} KpHudLog;

int kp_hud_log_parse(KpHudLog *h, const char *text, size_t size, KpError *err);
/* The log of a console golden (tests/golden/oracle/ui/hud_*.json, tools/oracle/export_ui_hud.py: create,
 * create_ticks, cases as the entries). */
void kp_hud_log_from_golden(KpHudLog *h, const KpJson *g);
void kp_hud_log_free(KpHudLog *h);

/* The run's observers (hud_replay's opts.on_*), each optional; e is the entry the call belongs to. */
typedef struct KpHudReplay KpHudReplay;
typedef struct {
	void (*on_frame)(void *ud, KpHudReplay *r, int64_t frame);
	void (*on_entry)(void *ud, KpHudReplay *r, const KpHudEntry *e);
	void (*on_present)(void *ud, KpHudReplay *r, int64_t tick);
	void (*on_select)(void *ud, KpHudReplay *r, int64_t frame, const int64_t *units, int count);
	void (*on_command)(void *ud, KpHudReplay *r, int64_t frame, const uint8_t *bytes, size_t size);
	void (*on_cursor)(void *ud, KpHudReplay *r, int64_t frame, int64_t have, int64_t want);
	void (*on_scroll)(void *ud, KpHudReplay *r, int64_t frame, const KpHudEntry *e);
	void (*on_screen)(void *ud, KpHudReplay *r, int64_t frame, int64_t x, int64_t y, const KpHudEntry *e);
	void (*on_release)(void *ud, KpHudReplay *r, int64_t frame, int released);
	/* the tooltip shown (have: x, y, w, h; NULL none) and the original's (want: left, top, right, bottom; NULL) */
	void (*on_tip)(void *ud, KpHudReplay *r, int64_t frame, const int32_t *have, const int64_t *want);
	/* a sound call (id, attenuation, pan, unit type, path, result) and a talking request (unit, duration, sound,
	 * ping, colour) against the log's entries */
	void (*on_sound)(void *ud, KpHudReplay *r, int64_t frame, const int64_t *have, const KpHudEntry *e);
	void (*on_talk)(void *ud, KpHudReplay *r, int64_t frame, const int64_t *have, const KpHudEntry *e);
	/* the unmatched ones at the end */
	void (*on_sound_end)(void *ud, KpHudReplay *r);
	void *ud;
} KpHudObservers;

typedef struct {
	const KpJson *golden;   /* a scenario golden */
	const KpHudLog *hud;
	const int64_t *frames;  /* ascending */
	int frame_count;
	int input;              /* the interface takes the logged input events itself */
	const char *commands, *ui; /* the run's commands.jsonl and ui.jsonl texts, or NULL (the golden's) */
	size_t commands_size, ui_size;
} KpHudReplayOpts;

/* A queued sound or talk of the matching (frame and entry, or the interface's values). */
typedef struct {
	int64_t frame;
	const KpHudEntry *entry;
	int64_t v[6];
} KpHudPending;

struct KpHudReplay {
	KpOracleUi ui;
	KpOracleGame game;
	KpReplay replay;
	KpRunner run;
	const KpHudReplayOpts *opts;
	const KpHudObservers *obs;
	int next_entry, next_want;
	int voices;           /* the log's sound records: the interface plays the sounds itself */
	const KpHudEntry *voice_entry;
	int scrolled;         /* a scroll record was seen: from then on the interface's view is its own */
	const KpHudEntry *scrolled_early; /* the scroll record already run before the release it made */
	int interface_select; /* the last selection entry was the interface's */
	int tip_wanted;       /* the tooltip the original left, to check after the event the records belong to */
	int64_t tip_frame, tip_rect[4];
	int tip_has_rect;
	/* the sounds and talks wanted (the log's) and made (the interface's), matched in order */
	KpHudPending *sounds_want, *sounds_have, *talks_want, *talks_have;
	int sw, sh, tw, th, sw_at, sh_at, tw_at, th_at, cap_sw, cap_sh, cap_tw, cap_th;
	KpError error;
	int failed;
};

/* Runs the replay (under the caller's failure handler on the game's world, which kp_hud_replay_start's game takes:
 * kp_hud_replay_run sets one of its own and returns -1 with the error). */
int kp_hud_replay_start(KpHudReplay *r, const char *root, const KpHudReplayOpts *opts, const KpHudObservers *obs,
	KpError *err);
int kp_hud_replay_run(KpHudReplay *r, KpError *err);
void kp_hud_replay_free(KpHudReplay *r);

/* --- the glue's trace streams --- */

/* A record of a replay file (tools/room_check.py): its first word and the rest of the line. */
typedef struct {
	char word[16];
	const char *line, *rest;
} KpTraceRecord;

/* A replay file: its header (the words given, with a number; kpm with its path), its records and its "data" lines. */
typedef struct {
	char *text;
	char kpm[512];
	const char *header_words[8];
	int64_t header[8];
	int header_count;
	KpTraceRecord *records;
	int count;
} KpTraceFile;

/* Parses a replay file (text owned by the parse); header_words ends with NULL. */
int kp_trace_file_parse(KpTraceFile *f, char *text, const char *const *header_words, KpError *err);
void kp_trace_file_free(KpTraceFile *f);
int64_t kp_trace_header(const KpTraceFile *f, const char *word, int64_t fallback);

/* The streams compared (each a list of lines). */
enum { KP_TRACE_TIMERS, KP_TRACE_BRIEF, KP_TRACE_VIDEOS, KP_TRACE_FLC, KP_TRACE_PAGES, KP_TRACE_BPC, KP_TRACE_RANDS,
	KP_TRACE_STREAMS };

typedef struct {
	char **lines;
	int count, cap;
} KpTraceStream;

typedef struct {
	KpTraceStream s[KP_TRACE_STREAMS];
} KpTraceStreams;

/* The clocks kp_ui replays the original's streams at. */
typedef struct {
	uint32_t *set_ticks, *video_ticks, *page_deadlines;
	int set_count, video_count, page_count;
} KpTraceClocks;

/* keep(line) (NULL: all) chooses the timer records compared. */
typedef int (*KpTraceKeep)(const char *line);

void kp_trace_streams_free(KpTraceStreams *s);
void kp_trace_clocks_free(KpTraceClocks *c);
/* The original's streams and the clocks. */
void kp_trace_want(const KpTraceFile *f, KpTraceKeep keep, KpTraceStreams *want, KpTraceClocks *clocks);
/* Sets kp_ui's oracle clocks (the interface keeps pointers to them: they live in u). */
void kp_trace_clocks(KpOracleUi *u, const KpTraceClocks *c);
/* Moves kp_ui's trace into have. */
void kp_trace_take(KpOracleUi *u, KpTraceStreams *have, KpTraceKeep keep);
/* The streams compared in order: the first difference of each printed (with ROOM_CONTEXT lines around it); returns
 * how many streams differ. */
int kp_trace_compare(const KpTraceStreams *want, const KpTraceStreams *have);

/* The capture lines ("cap TICK PATH NAME CX CY CW CH [DL DT DR DB [UNDER]]"), sorted by tick. */
typedef struct {
	int64_t tick;
	char path[512], name[128], under[512];
	int64_t cursor[4], rect[4];
	int has_rect, has_under;
} KpTraceCapture;

int kp_trace_captures(const KpTraceFile *f, KpTraceCapture **out);
/* Compares an expected picture (pixel(ud, x, y) -> index, or -1 to skip; -2 a value that is never equal) with a
 * capture's indices (640 wide), the cursor masked; prints and returns the number of differing pixels. */
typedef int (*KpTracePixel)(void *ud, int64_t x, int64_t y);
int64_t kp_trace_compare_pixels(const KpTraceCapture *cap, KpTracePixel pixel, void *ud, int64_t x0, int64_t y0,
	int64_t x1, int64_t y1, const char *dump);

#ifdef __cplusplus
}
#endif

#endif
