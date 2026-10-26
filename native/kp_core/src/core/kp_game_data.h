/* The data a native game starts from (stage N.6): the read-only data its process's games share (grpmeta.kpd,
 * iscript.kpd, aiscript.kpd), its own dat.kpd copy and mission, and the seeding of the generators as the oracle's runs
 * seed them. Shared by the host's game (kp_host.h) and the command-line tools
 * (kp_runner.h: kp_oracle, kp_replay, kp_bench). */
#ifndef KP_GAME_DATA_H
#define KP_GAME_DATA_H

#include <stddef.h>
#include <stdint.h>

#include "kp_start.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The data every game of a process reads and none changes: grpmeta.kpd, iscript.kpd and aiscript.kpd, loaded once. */
typedef struct {
	KpGrpMeta grp;
	KpIScript script;
	KpAiBins ai;
} KpGameShared;

/* The files (read, not taken); -1 with the error in err, s released. */
int kp_game_shared_load(KpGameShared *s, const uint8_t *grp, size_t grp_size, const uint8_t *iscript,
	size_t iscript_size, const uint8_t *aiscript, size_t aiscript_size, KpError *err);
void kp_game_shared_free(KpGameShared *s);

/* A game's own data over the shared data (which outlives it): dat.kpd's copy (the UMS overrides change it) and the
 * mission. */
typedef struct {
	KpDat dat;
	KpMission mission;
	const KpGameShared *shared;
} KpGameData;

/* Loads dat.kpd (read, not taken) and the mission's .kpm (malloc'd, taken, also on failure) under the mission's name;
 * -1 with the error in err, d released. */
int kp_game_data_load(KpGameData *d, const KpGameShared *shared, const uint8_t *dat, size_t dat_size,
	uint8_t *mission, size_t mission_size, const char *name, KpError *err);
void kp_game_data_free(KpGameData *d);

/* The game generator and the CRT seeded with the game's initial seed, the game generator enabled. */
void kp_game_seed(KpRngGame *rng, KpRngCrt *crt, uint32_t seed);

/* kp_start over loaded data (the start input's dat, grp, script, AI script binaries, mission) and the tileset's file. */
void kp_game_start_input(KpStartInput *in, KpGameData *d, const uint8_t *tileset, size_t tileset_size, KpRngGame *rng,
	KpRngCrt *crt, int expansion);

#ifdef __cplusplus
}
#endif

#endif
