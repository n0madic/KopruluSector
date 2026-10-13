/*
 * The replays of the command-line tools (kp_replay, kp_bench; ADR 0008): a scenario's mission, seed, command and
 * interface logs and the original's state CRCs (tools/lua/export_replay.lua writes them from the golden files), and
 * the run of a native game through them, object step by object step, as tools/lua/oracle.lua runner does.
 *
 * Replay text, one record per line:
 *   KPRP 1
 *   mission <path of the .kpm from the repository root>
 *   seed <initial seed>
 *   expansion <0 or 1>
 *   last <last object step>
 *   command <turn> <player> <hex bytes>
 *   select <frame> <unit>...
 *   mark <frame> <x> <y> <unit>
 *   cursor <frame> <x> <y>
 *   crc <step> <category> <crc32>
 */
#ifndef KP_RUNNER_H
#define KP_RUNNER_H

#include "../src/core/kp_game_data.h"
#include "../src/core/kp_game.h"

#define KP_REPLAY_SELECT 12

typedef struct {
	int64_t turn;
	KpCommand command;
} KpReplayCommand;

typedef struct {
	int kind; /* 0 select, 1 mark, 2 cursor */
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
	int64_t last;
	KpReplayCommand *commands;
	int command_count;
	KpReplayUi *ui;
	int ui_count;
	KpReplayCrc *crcs;
	int crc_count;
	uint8_t *bytes; /* the commands' bytes */
} KpReplay;

/* Parses a replay text; -1 with the error in err. */
int kp_replay_parse(KpReplay *r, const char *text, size_t size, KpError *err);
void kp_replay_free(KpReplay *r);

/* A game of the replay's mission: the files under root (generated/data/... and the mission path), loaded and
 * started with the replay's seed and expansion. */
typedef struct {
	KpGameData data;
	KpRngGame rng;
	KpRngCrt crt;
	uint8_t *files[5];
	KpGame *g;
} KpReplayGame;

int kp_replay_game_start(KpReplayGame *rg, const char *root, const KpReplay *r, KpError *err);
void kp_replay_game_free(KpReplayGame *rg);

/* The run of a game through a replay (tools/lua/oracle.lua runner); kp_runner_to runs under the caller's failure
 * handler (kp_images_begin) and drops the game's events after every step. */
typedef struct {
	KpGame *g;
	const KpReplay *r;
	int64_t step;
	int next_command, next_ui;
} KpRunner;

void kp_runner_init(KpRunner *run, KpGame *g, const KpReplay *r);
void kp_runner_to(KpRunner *run, int64_t step);

/* The file at path (malloc'd), or NULL. */
uint8_t *kp_read_file(const char *path, size_t *size);

#endif
