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

/* The data a process's games share (grpmeta.kpd, iscript.kpd, aiscript.kpd: kp_game_shared_load) with their CRC-32s
 * for the saves' identity. It must outlive every game started over it. */
typedef struct {
	KpGameShared data;
	uint32_t grp_crc, script_crc, ai_crc;
} KpHostShared;

/* The files (read, not taken); -1 with the error, sh released. */
int kp_host_shared_load(KpHostShared *sh, const uint8_t *grp, size_t grp_size, const uint8_t *iscript,
	size_t iscript_size, const uint8_t *aiscript, size_t aiscript_size, KpError *err);
void kp_host_shared_free(KpHostShared *sh);

typedef struct {
	KpGameData data;
	uint8_t *text; /* a language's overlay of the mission's strings (owned; kp_host_game_text) */
	KpRngGame rng;
	KpRngCrt crt;
	uint8_t *tileset; /* its tileset's file (owned; a presentation's KpTileset may read it in place) */
	size_t tileset_size;
	const KpHostShared *shared;
	uint32_t dat_crc; /* dat.kpd's file (the UMS overrides change the data's copy) */
	char name[KP_SAVE_NAME];
	char identity[64]; /* the data identity of save and load: the CRC-32s of the game's files */
	KpGame *g;
} KpHostGame;

/* Loads the game's data over the shared data: dat.kpd (read, not taken) and the mission's .kpm (malloc'd, taken, also
 * on failure) under the mission's name (its saves carry it; NULL: "mission"). hg->data.mission tells the tileset the
 * game needs (its era & 7, kp_host_tileset_path). -1 with the error, hg released. */
int kp_host_game_load(KpHostGame *hg, const KpHostShared *sh, const uint8_t *dat, size_t dat_size, uint8_t *mission,
	size_t mission_size, const char *name, KpError *err);
/* A language's text over a loaded game, before its start: the MISN overlay's STRS (ADR 0016), taken and freed with the
 * game. -1 with the error (the mission's own text stays) when it is corrupt, of another kind or schema than a mission
 * or has other strings than the mission. */
int kp_host_game_text(KpHostGame *hg, uint8_t *overlay, size_t size, KpError *err);
/* The tileset file's path under the data root (KP_TILESET_PATH) of the loaded mission. */
void kp_host_tileset_path(const KpHostGame *hg, char *out, size_t size);
/* Starts the loaded game with its tileset (malloc'd, taken, also on failure; NULL: none read) at the seed and
 * expansion; -1 with the error, hg released. */
int kp_host_game_start(KpHostGame *hg, uint8_t *tileset, size_t tileset_size, uint32_t seed, int expansion,
	const KpHostOpts *opts, KpError *err);
void kp_host_game_free(KpHostGame *hg);

#ifdef __cplusplus
}
#endif

#endif
