/* The data a native game starts from (stage N.6): the .kpd/.kpm files loaded into owned copies, and the seeding of
 * the generators as tools/lua/oracle.lua start_mission does. Shared by the Lua binding and the command-line tools
 * (kp_replay, kp_bench). */
#ifndef KP_GAME_DATA_H
#define KP_GAME_DATA_H

#include <stddef.h>
#include <stdint.h>

#include "kp_start.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	KpDat dat; /* the game's own copy: the UMS overrides change it */
	KpGrpMeta grp;
	KpIScript script;
	KpAiBins ai;
	KpMission mission;
} KpGameData;

/* The file contents of a game: dat.kpd, grpmeta.kpd, iscript.kpd, aiscript.kpd, the mission's .kpm and its tileset's
 * .kpd. */
typedef struct {
	const uint8_t *dat, *grp, *iscript, *aiscript, *mission, *tileset;
	size_t dat_size, grp_size, iscript_size, aiscript_size, mission_size, tileset_size;
	const char *mission_name;
} KpGameFiles;

/* Loads the files (d zeroed by the caller); -1 with the error in err (kp_game_data_free releases what loaded). */
int kp_game_data_load(KpGameData *d, const KpGameFiles *f, KpError *err);
void kp_game_data_free(KpGameData *d);

/* The game generator and the CRT seeded with the game's initial seed, the game generator enabled. */
void kp_game_seed(KpRngGame *rng, KpRngCrt *crt, uint32_t seed);

/* kp_start over loaded data (the start input's dat, grp, script, AI script binaries, mission and tileset). */
void kp_game_start_input(KpStartInput *in, KpGameData *d, const KpGameFiles *f, KpRngGame *rng, KpRngCrt *crt,
	int expansion);

#ifdef __cplusplus
}
#endif

#endif
