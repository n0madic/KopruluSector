/*
 * The oracle library (plan phase S, stage S1): the goldens of tests/golden/oracle (their JSON twins), the state CRCs
 * and fingerprints of a native game, the trigger and AI traces, the golden runs with their checks, the campaign chains
 * with the games' carry, and the chain context of the oracle tools. kp_runner.h starts the games and runs them;
 * kp_oracle_ui.h adds the interface's replays.
 */
#ifndef KP_ORACLE_H
#define KP_ORACLE_H

#include "kp_json.h"
#include "kp_runner.h"
#include "../src/core/kp_campaign.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KP_ORACLE_DATA "generated/data/"
/* Fields of the original that its drawing refreshes from object step 3 on (the image bounds of the rows in view,
 * 0x445de0); tools/state_check.py leaves them out there. */
#define KP_ORACLE_DRAWN_STEP 3
#define KP_ORACLE_FINGERPRINT "fingerprint"

/* A golden file (a JSON twin) loaded. */
typedef struct {
	KpJsonDoc doc;
	KpJson *root;
} KpGolden;

/* Loads the golden (JSON) at path; -1 with the error in err. */
int kp_golden_load(KpGolden *g, const char *path, KpError *err);
void kp_golden_free(KpGolden *g);

/* The replay of a golden run (tools/state_check.py write_scenario_golden: mission, seed, expansion, commands, ui
 * with its runs of draws expanded, the steps' CRCs): its mission path
 * "generated/<mission>.kpm" under the repository root, the mission index looked up under root. */
int kp_replay_from_golden(KpReplay *r, const char *root, const KpJson *golden, KpError *err);
/* The CRCs of step `step` in r (sorted by kp_replay_sort_crcs): *count of them from the returned index, -1 none. */
void kp_replay_sort_crcs(KpReplay *r);
int kp_replay_step_crcs(const KpReplay *r, int64_t step, int *count);

/* The category CRCs of a game's state lines (kp_dump_crcs; tools/state_check.py golden_lines). */
#define KP_ORACLE_CATEGORIES 96
typedef struct {
	int count;
	char names[KP_ORACLE_CATEGORIES][32];
	uint32_t crcs[KP_ORACLE_CATEGORIES];
} KpStateCrcs;

/* The state CRCs of g (drawn: without the image bounds of KP_ORACLE_DRAWN_STEP on); -1 out of memory. */
int kp_state_crcs(KpGame *g, int drawn, KpStateCrcs *out);
/* A category's CRC; 0 when the state has none (*found 0). */
uint32_t kp_state_crc(const KpStateCrcs *c, const char *category, int *found);
/* The continuation fingerprint of a state: the CRC-32 of its category CRCs as lines "<category> <crc>\n" in category
 * order (tools/state_check.py fingerprint; the play goldens keep it at every compared step). */
uint32_t kp_state_fingerprint(const KpStateCrcs *c);
/* The differences of a state from a golden step's CRCs (want[0..count-1]): each differing category appended to out
 * as " <category>", or " fingerprint" for a step that keeps only the fingerprint; returns how many. */
int kp_step_differences(const KpStateCrcs *c, const KpReplayCrc *want, int count, KpBuf *out);

/* The trigger trace line of a KP_EVENT_TRIGGER event ("frame player trigger action type result", the emulator's
 * triggers.jsonl) and the AI trace line of a KP_EVENT_AI event ("frame player slot bin pc op", ai.jsonl). */
void kp_trace_line(const KpEvent *e, char *out, size_t size);
void kp_ai_trace_line(const KpEvent *e, char *out, size_t size);

/* The state lines of a game (kp_dump), each line followed by "\n" in out; -1 out of memory. */
int kp_state_lines(KpGame *g, int drawn, KpBuf *out);

/* The chain context of the oracle tools (tools/state_check.py --kind chains): the environment's KP_CARRY (a file
 * holding the game before's KpCarry) and KP_TURN, into opts (carry then points into *carry); -1 with the error. */
int kp_oracle_chain_opts(KpOracleOpts *opts, KpCarry *carry, KpError *err);

/* --- golden runs --- */

/* A golden run's game and its runner over the golden's replay, with the trigger and AI traces collected when asked
 * (the golden's own trace and ai_trace are what test_scenarios compares). */
typedef struct {
	KpReplay replay;
	KpOracleGame game;
	KpRunner run;
	int tracing;
	KpBuf trace, ai_trace; /* the trace lines joined by "\n" */
	int64_t trace_count, ai_count;
	/* An event observer of the caller's (after the traces have taken theirs). */
	void (*on_event)(void *ud, const KpEvent *e);
	void *ud;
} KpGoldenRun;

/* Starts golden g's run (opts as kp_oracle_game_start's; trace: collect the traces); -1 with the error. */
int kp_golden_run_start(KpGoldenRun *gr, const char *root, const KpJson *g, const KpOracleOpts *opts, int trace,
	KpError *err);
void kp_golden_run_free(KpGoldenRun *gr);

/* Runs golden g to its last compared step (test_scenarios check_run, under the caller's failure handler), comparing
 * each compared step's state; appends "<name> step <n> <category>" for each difference at the first differing step,
 * then "<name> trigger trace" and "<name> AI trace" when the traces differ. Returns the differences. */
int kp_golden_run_check(KpGoldenRun *gr, const KpJson *g, const char *name, KpBuf *bad);

/* --- campaign chains --- */

/* A chain's game: its golden and name. play(ud, golden, name, opts) plays it (opts: the game before's carry and the
 * menus' turn counter) to the step after which its result stands and returns its game (NULL with the error in
 * why); the chain keeps it until the next game has started. after(ud, name, step, expected) sees each choice. */
typedef struct {
	const KpJson *golden;
	const char *name;
} KpChainGame;

typedef KpOracleGame *(*KpChainPlay)(void *ud, const KpJson *golden, const char *name, const KpOracleOpts *opts,
	KpBuf *why);
typedef void (*KpChainAfter)(void *ud, const char *name, const KpCampaignStep *step, int64_t expected,
	const KpCampaign *data);
typedef void (*KpChainRelease)(void *ud, KpOracleGame *game);

/* A campaign chain's games in order: each played with what the game before
 * left, then the campaign's progression (kp_campaign.h, with the game's own result and next scenario) chooses the
 * next mission, which the next game must be. Returns 0 when the chain holds, else 1 with the failing game's name and
 * why in failed; -1 with the error (campaign.kpd). release frees each game when the chain is done with it. */
int kp_campaign_chain(const char *root, const KpChainGame *games, int count, KpChainPlay play, KpChainAfter after,
	KpChainRelease release, void *ud, KpBuf *failed, KpError *err);

#ifdef __cplusplus
}
#endif

#endif
