/* The data a native game starts from (stage N.6): the read-only data its process's games share (grpmeta, iscript, the
 * AI scripts), its own copy of the DAT and its mission, and the seeding of the generators as the oracle's runs seed
 * them. Shared by the host's game (kp_host.h) and the command-line tools
 * (kp_runner.h: kp_oracle, kp_replay, kp_bench). */
#ifndef KP_GAME_DATA_H
#define KP_GAME_DATA_H

#include <stddef.h>
#include <stdint.h>

#include "kp_start.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The data every game of a process reads and none changes: grpmeta, the iscript and the AI scripts, built once from
 * the original files (kp_host_shared_from_files). */
typedef struct {
	KpGrpMeta grp;
	KpIScript script;
	KpAiBins ai;
} KpGameShared;

void kp_game_shared_free(KpGameShared *s);

/* A game's own data over the shared data (which outlives it): its copy of the DAT (the UMS overrides change it) and the
 * mission. */
typedef struct {
	KpDat dat;
	KpMission mission;
	const KpGameShared *shared;
} KpGameData;

/* A game's data over structures built from the original files (ADR 0018, stage D5): a copy of dat (the game's own) and
 * the mission (moved into d: *mission is left empty, also on failure); -1 with the error in err, d released. */
int kp_game_data_from(KpGameData *d, const KpGameShared *shared, const KpDat *dat, KpMission *mission, KpError *err);
void kp_game_data_free(KpGameData *d);

/* The game generator and the CRT seeded with the game's initial seed, the game generator enabled. */
void kp_game_seed(KpRngGame *rng, KpRngCrt *crt, uint32_t seed);

/* kp_start over loaded data (the start input's dat, grp, script, AI script binaries, mission); the caller gives the
 * tileset's CV5 and VF4. */
void kp_game_start_input(KpStartInput *in, KpGameData *d, KpRngGame *rng, KpRngCrt *crt, int expansion);

#ifdef __cplusplus
}
#endif

#endif
