/*
 * The product's session (plan phase S, stage S2; ADR 0012), headless: the clock (the wall clock's logical steps), the
 * session (the one owner of a mission's native game: its start, its steps with the queued commands, its pause, its
 * save and load at a step boundary), the save file's container and the profile (the campaign's progress, the player's
 * name and saved games). No SDL: the clock is passed in, as kp_ui's is (ADR 0010).
 */
#ifndef KP_SESSION_H
#define KP_SESSION_H

#include <stddef.h>
#include <stdint.h>

#include "kp_host.h"
#include "kp_ui.h"
#include "kp_campaign.h"
#include "kp_record.h"
#include "kp_rep.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --- the clock --- */

#define KP_CLOCK_SPEEDS 7
#define KP_CLOCK_DEFAULT_SPEED 7 /* Fastest */
#define KP_CLOCK_DEFAULT_MAX_CATCHUP 4
extern const int32_t KP_CLOCK_SPEED_MS[KP_CLOCK_SPEEDS]; /* 0x503360: 167, 111, 83, 67, 56, 48, 42 (Slowest..Fastest) */

/* The window's events for the clocks. */
enum { KP_WINDOW_FOCUS_LOST, KP_WINDOW_FOCUS_GAINED, KP_WINDOW_ICONIFIED, KP_WINDOW_DEICONIFIED };

typedef struct {
	int speed; /* 1..7 */
	int multiplier; /* a replay's (SetReplaySpeed 0x481040): Slowest's step times it, the others' divided by it; 1 */
	int max_catchup;
	double accumulator_ms;
	int paused;    /* the game's pause (the player's, or the autopause): resume ends it */
	int suspended; /* the window is iconified: its return ends it */
	int64_t steps;
	double dropped_ms;
	int resets;
	int has_wall;
	double last_wall;
} KpClock;

void kp_clock_init(KpClock *c, int speed, int max_catchup);
int32_t kp_clock_step_ms(const KpClock *c);
/* A replay's multiplier, as the clock paces it: 1..KP_CLOCK_MAX_MULTIPLIER (below 1 is 1, past it the most). The game
 * keeps any multiplier a file's 0x56 gives (the original's); only the wall clock is bounded. */
#define KP_CLOCK_MAX_MULTIPLIER 16 /* ReplayButtonFaster 0x480ee0's most */
void kp_clock_set_multiplier(KpClock *c, int64_t multiplier);
/* -1 for an unknown speed. */
int kp_clock_set_speed(KpClock *c, int speed);
void kp_clock_reset(KpClock *c);
void kp_clock_pause(KpClock *c);
void kp_clock_resume(KpClock *c);
void kp_clock_on_window(KpClock *c, int event, int autopause);
int kp_clock_stopped(const KpClock *c);
/* Advances by a frame of dt seconds: the steps to run now and the interpolation fraction in [0, 1). */
int kp_clock_advance(KpClock *c, double dt, double *alpha);
/* Advances to wall time now (seconds); the first call after creation or a reset only sets the reference. */
int kp_clock_advance_wall(KpClock *c, double now, double *alpha);

/* --- the session --- */

#define KP_SESSION_PENDING 256
#define KP_SESSION_COMMAND_BYTES 256

typedef struct {
	int64_t player;
	uint16_t size;
	uint8_t bytes[KP_SESSION_COMMAND_BYTES];
} KpQueuedCommand;

/* The start of a game: the seed, the expansion byte, the campaign row (0x526524), what the game before left (carry,
 * cheats; has_carry 0 the process's first game), the briefing's objectives (objective_count -1: the briefing's first
 * tick's). */
typedef struct {
	uint32_t seed;
	int expansion;
	int64_t mission_index;
	int has_carry;
	KpCarry carry;
	int64_t cheats;
	int64_t objectives[8];
	int objective_count;
	int speed, max_catchup; /* 0: the clock's defaults */
	int record; /* the game's commands recorded for its replay (kp_session_replay_file) */
	/* a replay's playback (StartGame 0x443f40 step 13): its commands fed from the record, the live commands filtered,
	 * full vision; the file is only read during kp_session_start (seed, expansion, speed and mission come from the
	 * caller, kp_session_replay_start fills them from the header) */
	const KpRep *replay;
	/* tests: the playback without the replay's presentation (the replay flag 0x67cc90 and its full vision change the
	 * shown creep, the minimap's attack blink +0x8a/+0x8b and the images' visibility; with objective_count -1 also
	 * the briefing's objectives, which Load Replay's start has none of), so that its state equals the recorded game's
	 * in every field */
	int replay_as_game;
} KpSessionStart;

#define KP_SESSION_REPLAY_SLOTS 8 /* the player records the feed matches and the players they play as: 0..7 */
typedef struct {
	KpHostGame host;
	KpGame *g; /* host.g, NULL when the game did not start */
	KpClock clock;
	KpQueuedCommand pending[KP_SESSION_PENDING];
	int pending_count;
	int64_t tick;  /* logical steps since the game's start (its turns: a paused game's step runs one too) */
	int64_t loads; /* saves loaded: the presentation restarts its motion when this changes */
	double step_ms, step_max_ms;
	int64_t start_frame, start_turn;
	int failed;
	char error[KP_ERROR_SIZE];
	/* The replay: the record of a recorded game or of the playback (owned, NULL none) */
	KpRecorder *rec;
	int playing;            /* a replay's playback */
	int replay_ended;       /* the playback passed the header's frame count (0x480e00, the victory dialog) */
	uint32_t replay_frames; /* the header's frame count */
	uint32_t replay_storm[KP_SESSION_REPLAY_SLOTS], replay_slot[KP_SESSION_REPLAY_SLOTS]; /* their Storm IDs, players (0x6726c8) */
	int64_t replay_player;  /* the player the feed's commands run as (0x508d48), kept when a Storm ID has no record */
	KpCommand *list;        /* owned: a step's commands (the record's, then the queued ones) */
	int list_cap;
	/* a recorded game's header as its start left it (kp_session_replay_game; the frame count is written with the
	 * file) and the saves loaded since (each restored the record a save kept; one without it left the record empty) */
	uint8_t rec_header[KP_REP_HEADER_SIZE];
	int rec_loads, rec_lost;
} KpSession;

void kp_session_start_defaults(KpSessionStart *s);
/* Loads the game's data over the shared data (which outlives the session) under the mission's name (its saves carry
 * it), built from the original files (ADR 0018, stage D5): a copy of the data set's DAT and the mission (moved into the
 * session; *mission left empty, also on failure); with files the mission's original CV5 and VF4, which kp_session_start
 * needs (kp_host_game_tileset_files). -1 with failed set and the reason. */
int kp_session_load_from(KpSession *s, const KpHostShared *sh, const KpDat *dat, KpMission *mission, const char *name,
	const KpFiles *files);
/* Starts the loaded game over its tileset's CV5 and VF4 and primes it (the briefing's objectives, PrimeGameLoop's two
 * object steps and RunGame's first turn). A game that does not start leaves failed set with the
 * reason and its data released (one that only fails to prime keeps it); 0 when it stands. */
int kp_session_start(KpSession *s, const KpSessionStart *start);
/* A presentation's view of the session's game data, which stays while the game does (until kp_session_free): the
 * mission and the shared GRP metadata. NULL before a start that stood. */
const KpMission *kp_session_mission(const KpSession *s);
const KpGrpMeta *kp_session_grp(const KpSession *s);
void kp_session_free(KpSession *s);
/* Queues a command of player (-1: the local player) for the next step; -1 when the queue is full or it is too long. */
int kp_session_command(KpSession *s, const uint8_t *bytes, size_t size, int64_t player);
/* One logical step with the queued commands; 0 once the game has stopped. now_ms (for the step's cost) may be 0. */
int kp_session_step(KpSession *s, double (*clock_ms)(void));
/* Advances the wall clock to now (seconds) and runs the steps it is due: their number, *alpha the presentation's
 * interpolation (1 once the game has stopped). */
int kp_session_advance(KpSession *s, double now, double *alpha, double (*clock_ms)(void));
void kp_session_pause(KpSession *s);
void kp_session_resume(KpSession *s);
void kp_session_set_speed(KpSession *s, int speed);
void kp_session_release_wait(KpSession *s);
/* The steps one clock step runs (RunPendingFrames 0x444e30's count 0x50335c): 10 in a replay at a multiplier of 8 or
 * more and a speed above Slowest, else 1. */
int kp_session_steps_per_tick(const KpSession *s);
/* The playback's start fields from a replay's header (seed, expansion, the campaign row, the speed) and from Koprulu's
 * tail (the carried globals and the cheats at the start); -1 when the record holds a command the playback refuses (a
 * network player's leave 0x57 or chat 0x5c, outside V1). */
int kp_session_replay_start(KpSessionStart *st, const KpRep *rep);
/* What a recorded game's header holds as the game starts (the players' types, races and forces; the human's name
 * "Anonymous" and the computers' none: the caller with the interface's network.tbl names them, then sets the header
 * with kp_session_replay_header). kp_session_start with record does both with these names. */
void kp_session_replay_game(KpSession *s, uint32_t seed, KpRepGame *game);
void kp_session_replay_header(KpSession *s, const KpRepGame *game);
/* The recorded game's replay at its current frame: the header kept since the start with the frame count, the record
 * (closed, SaveRecordedCommands 0x478980), the mission's CHK and the tail (NULL: none). -1 without a record or out
 * of memory; out is then empty. */
int kp_session_replay_file(KpSession *s, const KpRepTail *tail, KpRep *out, KpError *err);
int kp_session_can_skip(KpSession *s);
void kp_session_skip(KpSession *s);

/* --- the save file: KPF kind SAVE with CORE, CMDS, CAMP and PRES --- */

#define KP_SAVE_KIND "SAVE"
#define KP_SAVE_SCHEMA 1
#define KP_PRES_VALUES 8

/* Runs fn(ud) under the game's failure handler (a C failure of the game returns here): -1 when it failed, the message
 * in g->w->error. */
int kp_game_guard(KpGame *g, void (*fn)(void *ud), void *ud);
/* kp_game_guard over the session's game, whose failure stops the game as a step's does (s->failed, s->error; the
 * interface's input and frames of a stopped game still run): 0, or -1 when fn failed or there is no game. */
int kp_session_guard(KpSession *s, void (*fn)(void *ud), void *ud);

/* The campaign part of a save: the run and the profile's six progress words (the original's Zerg, Terran, Protoss,
 * then the expansion's). */
typedef struct {
	int present;
	KpCampaignRun run;
	int64_t progress[6];
} KpSaveCampaign;

/* The presentation's state a save keeps (the interface's ends of the trigger waits), name=integer pairs. */
typedef struct {
	int count;
	char names[KP_PRES_VALUES][32];
	int64_t values[KP_PRES_VALUES];
} KpSavePresentation;

/* The data set a save belongs to (ADR 0018 item 6): the profile's id and the SHA-256 of the data set's DAT; a load
 * checks the CRC-32s of the rest the games are built from (kp_host.c: grpmeta, iscript, the AI scripts, the tileset's
 * CV5 and VF4, the mission). Each is built from the original files, the same from a 1.17 install, Remastered's or a
 * repack, so a save goes from one to the other; one made over the converted containers is refused. */
typedef struct {
	char profile_id[17];
	uint8_t source_hash[32];
} KpSaveIdentity;

/* The identity of the data built from files (their profile) with the data set's DAT. */
void kp_save_identity(KpSaveIdentity *id, const KpFiles *files, const KpDat *dat);
/* The save of the session's game at its step boundary with its queued commands, the campaign (NULL none) and the
 * presentation's state (NULL none); *out malloc'd. A stopped game has no save. */
int kp_save_encode(KpSession *s, const KpSaveIdentity *id, const KpSaveCampaign *camp, const KpSavePresentation *pres,
	uint8_t **out, size_t *size, KpError *err);
/* The mission a save belongs to (its native save's name) and its campaign part; -1 for a damaged save or one of other
 * data. */
int kp_save_mission(const uint8_t *save, size_t size, const KpSaveIdentity *id, char *mission, size_t mission_size,
	KpSaveCampaign *camp, KpError *err);
/* Restores a save into the session's game (Manager:load: the queued commands, the tick, the player's speed, loads
 * counted); the campaign and presentation it carried. -1 with err and the game unchanged. */
int kp_save_load(KpSession *s, const uint8_t *save, size_t size, const KpSaveIdentity *id, KpSaveCampaign *camp,
	KpSavePresentation *pres, KpError *err);
/* A presentation value by name, or fallback. */
int64_t kp_save_pres_get(const KpSavePresentation *p, const char *name, int64_t fallback);
void kp_save_pres_set(KpSavePresentation *p, const char *name, int64_t value);

/* Writes a save file without ever leaving a truncated one or losing the old one: the bytes go to path.tmp
 * first (checked, flushed to the disk, closed) and replace path in one step only then (rename; MoveFileEx with
 * MOVEFILE_REPLACE_EXISTING on Windows); when that fails, the old file stays and the new save stays in path.tmp. -1 with
 * the reason. */
int kp_write_file_atomic(const char *path, const uint8_t *data, size_t size, KpError *err);

/* The files of dir whose name ends in ext (compared without case), folders and hidden files left out, sorted without
 * case (BuildMapList 0x45ba40's order); NULL with count 0 when the folder cannot be read or memory runs out. */
char **kp_list_files(const char *dir, const char *ext, int *count);
/* The folders of dir, hidden ones and those starting with '.' left out, sorted as kp_list_files' (freed by
 * kp_list_files_free). */
char **kp_list_folders(const char *dir, int *count);
void kp_list_files_free(char **names, int count);

/* --- the profile: the login's IDs with their campaign progress, the logged-in ID, the saved games, the video options,
 * the options word and the other options the original keeps in its registry (kind PROF) --- */

#define KP_PROFILE_SAVES KP_UI_SAVES /* an ID's (the load screen's list) */
#define KP_PROFILE_SAVE_POOL 1024    /* every ID's */
#define KP_PROFILE_NAME KP_UI_NAME
#define KP_PROFILE_SAVE_NAME KP_UI_SAVE_NAME
#define KP_PROFILE_LOCALE 8 /* a language's ID ("ruRU", ASCII) and its NUL */
/* A saved game: its ID (the original's save\<name>\ folder, BuildSaveDirectory 0x499e80; "" a run's without a login)
 * and its name. */
typedef struct {
	char owner[KP_PROFILE_NAME];
	char name[KP_PROFILE_SAVE_NAME];
} KpProfileSave;
#define KP_PROFILE_CHARACTERS KP_UI_PROFILES
/* An ID of the login list (the original's characters\<name>.spc record): its name, its creation's order and its
 * progress. */
typedef struct {
	char name[KP_PROFILE_NAME];
	uint32_t time;              /* the list's order: the oldest first */
	KpCampaignProfile progress;
} KpProfileCharacter;
/* The video options (the dialog's, kp_ui.h): the gamma (KP_UI_GAMMA_MIN..MAX), the portraits (0 none, 1 still, 2
 * animated) and the art, fog, water, text and lighting (KP_GRAPHICS_*, KP_FOG_*, KP_WATER_*, KP_VTEXT_*, KP_LIGHTING_*
 * or KP_VIDEO_AUTO). */
typedef struct {
	int32_t gamma, portraits, graphics, fog, water, font, lighting;
} KpVideoSettings;
/* The video options a command-line flag gave for its run (the art, the fog, the water, the text, the lighting): the
 * profile's do not replace them at the start, and they are not kept in the profile unless the dialog changed them. */
#define KP_VIDEO_GIVEN_GRAPHICS 0x1
#define KP_VIDEO_GIVEN_FOG 0x2
#define KP_VIDEO_GIVEN_WATER 0x4
#define KP_VIDEO_GIVEN_FONT 0x8
#define KP_VIDEO_GIVEN_LIGHTING 0x10
/* The options of the sound and speed dialogs and the tips (the original's registry table 0x50f168, CPUThrottle apart):
 * the music's and the effects' percent (0..100), the mouse and key scroll speeds and the game speed (0..6), the tip, the
 * CPU throttle (0 or 1). */
typedef struct {
	uint32_t music, effects, mouse_scroll, key_scroll, game_speed, tip, cpu_throttle;
} KpGameSettings;
typedef struct {
	char name[KP_PROFILE_NAME]; /* the logged-in ID's; "" before the login and after its deletion */
	int character_count;
	KpProfileCharacter characters[KP_PROFILE_CHARACTERS]; /* in the login list's order */
	KpCampaignProfile unnamed;  /* a run's without a login (a tool's mission): not written */
	int save_count;
	KpProfileSave saves[KP_PROFILE_SAVE_POOL]; /* every ID's, newest first */
	int old_save_files;               /* the files still have older names (KP_SAVE_FILES_*; the host renames them,
	                                   * kp_profile_rename_old_saves) */
	KpVideoSettings video;            /* the defaults in a profile older than its section */
	uint32_t options;                 /* the original's options word 0x6d3148 as far as the profile keeps it: the first
	                                   * start's intro (KP_UI_OPTION_EXPANSION_INTRO, set in a new profile and in one
	                                   * older than schema 4) and the dialogs' check boxes (KP_UI_OPTIONS_KEPT, a new
	                                   * profile's KP_UI_OPTIONS_DEFAULT, also in one older than schema 5); the bits of a
	                                   * later build kept */
	KpGameSettings settings;          /* a new profile's in one older than schema 6 */
	char text_locale[KP_PROFILE_LOCALE];   /* the language the player chose for the text ("ruRU"; ADR 0016), "" none:
	                                        * the system's, else the base's; "" in one older than schema 8 */
	char speech_locale[KP_PROFILE_LOCALE]; /* and for the speech */
} KpProfile;

void kp_profile_init(KpProfile *p);
/* The logged-in ID's progress (its name compared without case), else the unnamed one. */
KpCampaignProfile *kp_profile_progress(KpProfile *p);
/* The IDs as the login list (at most max), their count. */
int kp_profile_login_list(const KpProfile *p, KpUiProfile *list, int max);
/* The login list back after its screen (its IDs created, deleted, their progress raised by the menus): the IDs become
 * the list's, the saves of an ID no longer listed leave; player, when given, is the logged-in ID; a logged-in ID no
 * longer listed leaves none. */
void kp_profile_from_login_list(KpProfile *p, const KpUiProfile *list, int count, const char *player);
/* The video options the profile keeps at the dialog's OK: the run's, but the profile's own for an option a flag gave
 * (`given`, KP_VIDEO_GIVEN_*) that still has the flag's value (`flags`). */
void kp_profile_commit_video(KpVideoSettings *profile, const KpVideoSettings *run, const KpVideoSettings *flags,
	int given);
int kp_profile_encode(const KpProfile *p, uint8_t **out, size_t *size);
/* -1 for a damaged profile (p then holds a new one). */
int kp_profile_decode(KpProfile *p, const uint8_t *data, size_t size, KpError *err);
/* Raises the progress to at least other's (a loaded save never takes progress away). */
void kp_profile_merge(KpCampaignProfile *p, const KpCampaignProfile *other);
/* The progress as a save's six words (KpSaveCampaign.progress: Zerg, Terran, Protoss of the original's, then of the
 * expansion's) and back (the other races' none). */
#define KP_PROFILE_WORD_RACES 3
void kp_profile_words(const KpCampaignProfile *p, int64_t words[6]);
void kp_profile_from_words(KpCampaignProfile *p, const int64_t words[6]);
#define KP_SAVE_FILE_NAME 200 /* a save file name's buffer (kp_save_file_name, kp_save_old_file_name) */
#define KP_SAVE_FILES_BEFORE_IDS 1 /* a profile before schema 3: kp_save_old_file_name of the name's windows-1252 bytes */
#define KP_SAVE_FILES_BYTES 2      /* before schema 7, some name not ASCII: of those bytes, with the owner's */
/* A save's file name, save_<owner>-<name>.kpsave: each in lower case with every byte but a letter or digit as _XX (so
 * never a '-'), a part past 84 characters as _h and its FNV-1a 64 in hex (a file name's limit). */
void kp_save_file_name(const char *owner, const char *name, char *out, size_t size);
/* The file of a save before PROF schema 3 (every ID's in one list): save_<name>.kpsave. */
void kp_save_old_file_name(const char *name, char *out, size_t size);
/* The ID whose saves the logged-in one plays (kp_profile_progress's): its name, "" without a login. */
const char *kp_profile_save_owner(const KpProfile *p);
/* The logged-in ID's saved games (kp_profile_save_owner's): their names, newest first, at most max; their count. */
int kp_profile_save_list(const KpProfile *p, char (*names)[KP_PROFILE_SAVE_NAME], int max);
/* The logged-in ID's saved game names: added first (when new, case-insensitively; an ID with KP_PROFILE_SAVES of them,
 * or a full pool, loses its oldest, and a full pool where it has none loses the pool's oldest, another ID's; the file
 * of a save that leaves stays), removed. */
void kp_profile_add_save(KpProfile *p, const char *name);
void kp_profile_remove_save(KpProfile *p, const char *name);
/* DeleteProfileSaves 0x498a70: every save of owner leaves the list, each given to each(ud, save) first (NULL none). */
void kp_profile_drop_saves(KpProfile *p, const char *owner, void (*each)(void *ud, const KpProfileSave *save),
	void *ud);
/* The files of an older profile get their names (old_save_files): rename(ud, from, to) gives 0 when it
 * failed, and that save leaves the list (its file stays as it was). Clears old_save_files; 1 when it had any. */
int kp_profile_rename_old_saves(KpProfile *p, int (*rename_file)(void *ud, const char *from, const char *to),
	void *ud);

#ifdef __cplusplus
}
#endif

#endif
