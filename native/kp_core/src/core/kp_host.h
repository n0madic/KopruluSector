/*
 * A host's native game (plan phase S, stage S2): a mission's game with the data it starts from, its generators, the
 * files it keeps and the data identity of its saves (the CRC-32s of the files). The product's session (native/kp_app)
 * and the oracle's tools (kp_runner.h KpOracleGame) start their games here; the host reads the files (from a directory,
 * an Android asset, the web's preloaded data) and hands them over.
 */
#ifndef KP_HOST_H
#define KP_HOST_H

#include "kp_game_data.h"
#include "kp_game.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The files a game loads from: dat.kpd, grpmeta.kpd, iscript.kpd, the mission's .kpm, aiscript.kpd (its tileset's .kpd
 * comes at the start). */
enum { KP_HOST_DAT, KP_HOST_GRP, KP_HOST_ISCRIPT, KP_HOST_MISSION, KP_HOST_AISCRIPT, KP_HOST_FILES };

/* The options of a game start. */
typedef struct {
	int events;            /* the event ring on (a presentation's or a drained oracle run's) */
	int trace;             /* the ring also records the trigger and AI traces (KP_EVENT_TRIGGER, KP_EVENT_AI) */
	int no_units;          /* the map without its preplaced units */
	int64_t mission_index; /* the campaign row 0x526524 (0: a custom map) */
	int64_t cheats;        /* the cheat word of the game before */
	int64_t turn;          /* the turn counter the menus left */
	const KpCarry *carry;  /* what the game before left (kp_carry_take), or NULL */
	int64_t speed;         /* the game speed byte 0x6d3130; KP_NIL: KP_DEFAULT_SPEED */
} KpHostOpts;

void kp_host_opts(KpHostOpts *o);

/* The file at path (malloc'd, NUL terminated past its size), or NULL. */
uint8_t *kp_read_file(const char *path, size_t *size);

typedef struct {
	KpGameData data;
	KpRngGame rng;
	KpRngCrt crt;
	uint8_t *tileset; /* its tileset's file (owned; a presentation's KpTileset may read it in place) */
	size_t tileset_size;
	uint32_t dat_crc; /* dat.kpd's file (the UMS overrides change the data's copy) */
	char name[KP_SAVE_NAME];
	char identity[64]; /* the data identity of save and load: the CRC-32s of the game's files */
	KpGame *g;
} KpHostGame;

/* Loads the game's data from the files (read, not taken: the data keeps its own copies): hg->data.mission tells the
 * tileset the game needs (its era & 7, kp_host_tileset_name). -1 with the error, hg released. */
int kp_host_game_load(KpHostGame *hg, const uint8_t *const files[KP_HOST_FILES], const size_t sizes[KP_HOST_FILES],
	const char *name, KpError *err);
/* The tileset file name (generated/data/tilesets/<name>.kpd) of the loaded mission. */
const char *kp_host_tileset_name(const KpHostGame *hg);
/* Starts the loaded game with its tileset (malloc'd, taken, also on failure; NULL: none read) at the seed and
 * expansion; -1 with the error, hg released. */
int kp_host_game_start(KpHostGame *hg, uint8_t *tileset, size_t tileset_size, uint32_t seed, int expansion,
	const KpHostOpts *opts, KpError *err);
void kp_host_game_free(KpHostGame *hg);

#ifdef __cplusplus
}
#endif

#endif
