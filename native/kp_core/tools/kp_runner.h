/*
 * The oracle runs of the native game (ADR 0008; plan phase S, stage S1): a generated mission's game started with the
 * original's seed (KpOracleGame), and its run object step by object step through the original's command and interface
 * logs (KpRunner over a KpReplay). The command-line tools (kp_replay, kp_bench, kp_oracle) and the C suites (tests/c)
 * share them; kp_oracle.h adds the goldens and the checks.
 *
 * A KpReplay comes from a replay text (kp_replay_parse; kp_oracle export_replay writes it from a scenario golden), from
 * a golden (kp_oracle.h kp_replay_from_golden) or from the emulator's logs (kp_replay_add_commands_jsonl,
 * kp_replay_add_ui_jsonl). Replay text, one record per line:
 *   KPRP 1
 *   mission <path of the .kpm from the repository root>
 *   seed <initial seed>
 *   expansion <0 or 1>
 *   index <the campaign mission index 0x526524>    (optional; 0, a custom map, without it)
 *   last <last object step>
 *   command <turn> <player> <hex bytes>
 *   select <frame> <unit>...
 *   mark <frame> <x> <y> <unit>
 *   cursor <frame> <x> <y>
 *   onscreen <frame> <x> <y>
 *   alliance <frame> <player> <other> <value>
 *   paused <frame>, release <frame>, skip <frame>, objectives <frame> <string id>
 *   crt <frame>                                    (a CRT rand draw of the interface)
 *   srand <frame> <seed>                           (a typed line reseeds the CRT generator)
 *   critter <frame> <unit>                         (a critter's death of its answers, PlayUnitSelectSound 0x446c30)
 *   menu_pause <frame> <1 | 0>                     (a modeless dialog's PauseGame 0x47cbb0 / UnpauseGame 0x47cc10)
 *   placement <frame> [<order> <type>]             (the building placement mode starts, or closes)
 *   crc <step> <category> <crc32>
 */
#ifndef KP_RUNNER_H
#define KP_RUNNER_H

#include "kp_json.h"
#include "../src/core/kp_game_data.h"
#include "../src/core/kp_game.h"
#include "../src/core/kp_host.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KP_REPLAY_SELECT 12 /* the most values of an interface event (a selection's units) */

typedef struct {
	int64_t turn;
	KpCommand command;
} KpReplayCommand;

/* The interface events of a run (in the replay text's words). */
enum {
	KP_RUN_SELECT, KP_RUN_MARK, KP_RUN_CURSOR, KP_RUN_ONSCREEN, KP_RUN_ALLIANCE, KP_RUN_PAUSED, KP_RUN_RELEASE,
	KP_RUN_SKIP, KP_RUN_OBJECTIVES, KP_RUN_CRT, KP_RUN_PLACEMENT, KP_RUN_SRAND, KP_RUN_CRITTER, KP_RUN_MENU_PAUSE,
	KP_RUN_KINDS
};

typedef struct {
	/* KP_RUN_*: select (the units), mark (x, y, unit), cursor (x, y), onscreen (a draw of the game screen at its
	 * top-left tile), alliance (player, other, value), paused (a frame of a paused game), release (the end of the
	 * local trigger wait), skip (Escape), objectives (the briefing's, before the game), crt (an interface CRT rand
	 * draw), placement (order and type, or none when it closes), srand (the CRT generator's new seed), critter (the
	 * unit), menu_pause (1 or 0) */
	int kind;
	int64_t frame;
	int64_t values[KP_REPLAY_SELECT];
	int count;
} KpReplayUi;

typedef struct {
	int64_t step;
	char category[32];
	uint32_t crc;
} KpReplayCrc;

typedef struct {
	char mission[512];
	uint32_t seed;
	int expansion;
	int64_t mission_index;
	int64_t last;
	KpReplayCommand *commands;
	int command_count, command_cap;
	KpReplayUi *ui;
	int ui_count, ui_cap;
	KpReplayCrc *crcs;
	int crc_count, crc_cap;
	uint8_t **byte_blocks; /* the commands' bytes (owned blocks) */
	int block_count, block_cap;
	size_t block_used; /* the bytes of the last block in use */
} KpReplay;

/* Parses a replay text; -1 with the error in err. */
int kp_replay_parse(KpReplay *r, const char *text, size_t size, KpError *err);
void kp_replay_init(KpReplay *r);
void kp_replay_free(KpReplay *r);
/* Drops the commands (and their bytes). */
void kp_replay_clear_commands(KpReplay *r);
/* Appends a command (its bytes copied) and an interface event (kp_replay_ui appends a zeroed one). */
void kp_replay_command(KpReplay *r, int64_t turn, int64_t player, const uint8_t *bytes, size_t size);
KpReplayUi *kp_replay_ui(KpReplay *r, int kind, int64_t frame);
void kp_replay_crc(KpReplay *r, int64_t step, const char *category, uint32_t crc);
/* The emulator's commands.jsonl and ui.jsonl texts; -1 with the error in err. */
int kp_replay_add_commands_jsonl(KpReplay *r, const char *text, size_t size, KpError *err);
int kp_replay_add_ui_jsonl(KpReplay *r, const char *text, size_t size, KpError *err);
/* One entry of them ({turn, player, bytes} or an interface event); a command's bytes not hex: -1 with the error. */
int kp_replay_add_command_json(KpReplay *r, const KpJson *v, KpError *err);
void kp_replay_add_ui_json(KpReplay *r, const KpJson *v);
/* The kind of an interface event's word ("select", ...), or -1. */
int kp_run_kind(const char *word);
const char *kp_run_word(int kind);
/* The values an event of the kind takes (-1: any count, a selection's units, a placement's 0 or 2). */
int kp_run_values(int kind);

/* The file at path under root (an absolute path stays), or NULL with "oracle: cannot read" in err. */
uint8_t *kp_read_under(const char *root, const char *path, size_t *size, KpError *err);
/* Formats err's message; returns -1. */
int kp_error_set(KpError *err, const char *fmt, ...);
/* Aborts the tool on an allocation failure. */
void kp_out_of_memory(void);
/* Grows an array of `count` elements of `size` bytes to hold one more (realloc'd; aborts out of memory). */
void *kp_grow(void *data, int count, int *cap, size_t size);

/* --- the games of the oracle --- */

/* The options of a game start: the host's (host.mission_index KP_NIL: from generated/missions/index.txt). */
typedef struct {
	KpHostOpts host;
	const char *name; /* the mission's name, the default mission of save and load (NULL: the path) */
} KpOracleOpts;

/* opts with the defaults: the host's, the mission index looked up. */
void kp_oracle_opts(KpOracleOpts *o);

/* A native game with its data, generators and files: the host's game. */
typedef KpHostGame KpOracleGame;

/* Starts the mission at path (the .kpm, relative to root, as are generated/data/...) with the seed and expansion;
 * -1 with the error in err (og released). */
int kp_oracle_game_start(KpOracleGame *og, const char *root, const char *path, uint32_t seed, int expansion,
	const KpOracleOpts *opts, KpError *err);
void kp_oracle_game_free(KpOracleGame *og);
/* The campaign mission index 0x526524 of a generated mission or of a scenario's base mission (generated/missions/
 * index.txt, generated/scenarios/index.txt under root), 0 when it has none. */
int64_t kp_oracle_mission_index(const char *root, const char *path);

/* The replay's game (kp_replay, kp_bench): no events, the replay's mission index. */
typedef KpOracleGame KpReplayGame;
int kp_replay_game_start(KpReplayGame *rg, const char *root, const KpReplay *r, KpError *err);
void kp_replay_game_free(KpReplayGame *rg);

/* --- the runs --- */

/* The run of a game through a replay; kp_runner_to runs under the caller's failure
 * handler (kp_images_begin). With on_event set, the events of a game that records them go to it after every step;
 * after_objects is called after each object step, before the next frame's interface events (where the original's
 * message loop runs the UI timers). */
typedef struct {
	KpGame *g;
	const KpReplay *r;
	int64_t step;
	int next_command, next_ui;
	int begun; /* the leading objectives are taken */
	void (*on_event)(void *ud, const KpEvent *e);
	void (*after_objects)(void *ud);
	void *ud;
} KpRunner;

void kp_runner_init(KpRunner *run, KpGame *g, const KpReplay *r);
void kp_runner_to(KpRunner *run, int64_t step);
/* An interface event of the log applied now (also a caller that places an event itself, hud_replay's game calls). */
void kp_runner_apply(KpRunner *run, const KpReplayUi *u);
/* Drains the game's events to on_event (kp_runner_to does after every step). */
void kp_runner_drain(KpRunner *run);

#ifdef __cplusplus
}
#endif

#endif
