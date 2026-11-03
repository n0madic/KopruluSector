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
} KpSessionStart;

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
} KpSession;

void kp_session_start_defaults(KpSessionStart *s);
/* Loads the game's data over the shared data (which outlives the session): dat.kpd (read, not taken) and the
 * mission's .kpm (malloc'd, taken, also on failure) under the mission's name (its saves carry it); the loaded mission
 * names the tileset kp_session_start takes (kp_host_tileset_path). -1 with failed set and the reason. */
int kp_session_load(KpSession *s, const KpHostShared *sh, const uint8_t *dat, size_t dat_size, uint8_t *mission,
	size_t mission_size, const char *name);
/* Starts the loaded game with its tileset (malloc'd, taken; NULL: none read) and primes it (the briefing's objectives,
 * PrimeGameLoop's two object steps and RunGame's first turn). A game that does not start leaves failed set with the
 * reason and its data released (one that only fails to prime keeps it); 0 when it stands. */
int kp_session_start(KpSession *s, uint8_t *tileset, size_t tileset_size, const KpSessionStart *start);
/* A presentation's view of the session's game data, which stays while the game does (until kp_session_free): the
 * mission, the shared GRP metadata and the tileset's file (*size its bytes). NULL before a start that stood. */
const KpMission *kp_session_mission(const KpSession *s);
const KpGrpMeta *kp_session_grp(const KpSession *s);
const uint8_t *kp_session_tileset(const KpSession *s, size_t *size);
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

/* The data set a save belongs to: dat.kpd's profile id and source hash. */
typedef struct {
	char profile_id[17];
	uint8_t source_hash[32];
} KpSaveIdentity;

int kp_save_identity(KpSaveIdentity *id, const uint8_t *dat, size_t size, KpError *err);
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

/* --- the profile: the login's IDs with their campaign progress, the logged-in ID, the saved games and the video
 * options (kind PROF) --- */

#define KP_PROFILE_SAVES 254
#define KP_PROFILE_NAME 32
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
typedef struct {
	char name[KP_PROFILE_NAME]; /* the logged-in ID's; "" before the login and after its deletion */
	int character_count;
	KpProfileCharacter characters[KP_PROFILE_CHARACTERS]; /* in the login list's order */
	KpCampaignProfile unnamed;  /* a run's without a login (a tool's mission): not written */
	int save_count;
	char saves[KP_PROFILE_SAVES][64]; /* newest first */
	KpVideoSettings video;            /* the defaults in a profile older than its section */
} KpProfile;

void kp_profile_init(KpProfile *p);
/* The logged-in ID's progress (its name compared without case), else the unnamed one. */
KpCampaignProfile *kp_profile_progress(KpProfile *p);
/* The IDs as the login list (at most max), their count. */
int kp_profile_login_list(const KpProfile *p, KpUiProfile *list, int max);
/* The login list back after its screen (its IDs created, deleted, their progress raised by the menus): the IDs become
 * the list's; player, when given, is the logged-in ID; a logged-in ID no longer listed leaves none. */
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
/* A save's file name: the name in lower case with every byte but a letter or digit as _XX. */
void kp_save_file_name(const char *name, char *out, size_t size);
/* The profile's saved game names: added first (when new, case-insensitively), removed. */
void kp_profile_add_save(KpProfile *p, const char *name);
void kp_profile_remove_save(KpProfile *p, const char *name);

#ifdef __cplusplus
}
#endif

#endif
