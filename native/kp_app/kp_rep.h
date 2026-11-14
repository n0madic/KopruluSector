/*
 * StarCraft 1.17 replays (.rep; docs/formats/rep.md): the file WriteReplayFile 0x480080 writes and ReadReplayFile
 * 0x480690 reads, as four compressed blocks (WriteCompressedBlock 0x4137d0: CRC-32 without the final inversion, the
 * chunk count, 8 KiB chunks PKWARE DCL imploded (binary, 1 KiB dictionary) or raw): the magic "reRS", the 633-byte
 * header, the command record (its used byte count, then the bytes when it is above 0) and the map (the CHK's size,
 * then the CHK). Bytes after the map's block are never read by the original; Koprulu puts its KPRX tail there (the
 * data and simulation it recorded with and the globals the game before left, which the header does not hold).
 *
 * No I/O: the caller reads and writes the bytes. The reader checks every length against the bytes it has.
 */
#ifndef KP_REP_H
#define KP_REP_H

#include <stddef.h>
#include <stdint.h>

#include "kp_kpf.h"
#include "kp_start.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KP_REP_MAGIC 0x53526572u /* "reRS" */
#define KP_REP_HEADER_SIZE 0x279
#define KP_REP_PLAYERS 12
#define KP_REP_PLAYER_SIZE 36
#define KP_REP_NAME 25
#define KP_REP_CHUNK 0x2000
#define KP_REP_MAX_CHK (16u << 20)    /* a CHK larger than this is refused (the original allocates any size) */
#define KP_REP_MAX_RECORD (64u << 20) /* the same for the command record */
/* Header offsets (P28 0x480080 and its accessors 0x480540-0x480680) */
#define KP_REP_BROOD_WAR 0x00
#define KP_REP_FRAMES 0x01
#define KP_REP_CAMPAIGN 0x05
#define KP_REP_SEED_RECORD 0x07 /* 13 bytes: command byte, the seed (+8), 8 player bytes */
#define KP_REP_SEED 0x08
#define KP_REP_GAME 0x14 /* the 141-byte game header 0x6728d0 */
#define KP_REP_GAME_SIZE 0x8d
#define KP_REP_SLOTS 0x39  /* game header +0x25 */
#define KP_REP_SPEED 0x3a  /* game header +0x26 */
#define KP_REP_PRESENT 0x46 /* nonzero, checked by the reader (WriteReplayFile writes 1) */
#define KP_REP_GAME_NAME 0x18 /* the game header's player name (game +4), 24 bytes with its NUL */
#define KP_REP_GAME_NAME_SIZE 24
#define KP_REP_MAP_SIZE 0x34  /* game +0x20: the map's width and height (WORDs) */
#define KP_REP_TILESET 0x44   /* game +0x30 (WORD) */
#define KP_REP_MAP_NAME 0x61  /* game +0x4d: the scenario's name, 32 bytes */
#define KP_REP_MAP_NAME_SIZE 32
#define KP_REP_PLAYER_RECORDS 0xa1
#define KP_REP_COLORS 0x251
#define KP_REP_FORCES 0x271

/* Why a file was refused, matching the original's messages where it has one. */
typedef enum {
	KP_REP_OK = 0,
	KP_REP_DAMAGED,   /* gluAll 63: not a replay, a bad block, a length past the file */
	KP_REP_EXPANSION, /* gluAll 60: a Brood War replay without Brood War */
	KP_REP_NOT_CAMPAIGN, /* outside V1: a melee or custom map's replay (campaign row 0) or a row we do not have */
	KP_REP_OTHER_MAP, /* the CHK is not the campaign row's map */
	KP_REP_NO_MEMORY,
} KpRepRefusal;

typedef struct {
	uint32_t slot;
	int32_t storm; /* -1: not a network player */
	uint8_t type, race, force;
	char name[KP_REP_NAME]; /* NUL-terminated within the record */
} KpRepPlayer;

/* Koprulu's tail (KPRX): what the game was recorded with and what it started from. */
#define KP_REP_TAIL_MAGIC "KPRX"
#define KP_REP_TAIL_VERSION 1
/* The simulation's revision: raised when kp_core's behaviour changes for the same commands, so that a replay
 * recorded before is shown as unchecked. */
#define KP_REP_SIM_REVISION 1
typedef struct {
	uint32_t version, sim_revision;
	uint8_t data_hash[32]; /* the save identity's source hash: the data set's DAT (kp_save_identity) */
	int32_t has_carry;
	KpCarry carry;
	int64_t cheats;
	int32_t objective_count;
	int64_t objectives[8];
	int32_t loads; /* saves loaded during the game: the playback is shown unverified (each load restored its record) */
	int32_t lost;  /* a load without a record (a save from before the replays): the record starts there */
} KpRepTail;

typedef struct {
	uint8_t header[KP_REP_HEADER_SIZE];
	uint8_t *record; /* owned */
	size_t record_size;
	uint8_t *chk; /* owned */
	size_t chk_size;
	int has_tail; /* a KPRX tail was found and read */
	KpRepTail *tail; /* owned, NULL without one */
	size_t extra_size; /* bytes after the map that are no KPRX tail (kept unread) */
} KpRep;

/* What a campaign game's header holds (the single player game header 0x6728d0 and player records 0x6726c8 as
 * WriteReplayFile 0x480080 finds them; 1.17's files show the rest constant: one active slot, the Use Map Settings type
 * 0x0a/1 with its template, the colours 0..7, the force bytes 0). */
typedef struct {
	int brood_war;
	uint32_t frames;
	int campaign;
	uint32_t seed;
	const char *player_name; /* the game header's (the logged-in ID; "Anonymous", network.tbl 72, without one) */
	int map_w, map_h, tileset, speed;
	const char *map_name;    /* the scenario's name (SPRP) in the map's encoding */
	KpRepPlayer players[8];  /* slot = the index; storm -1 but the human's 0 */
} KpRepGame;

/* The 633-byte header of game into r->header. */
void kp_rep_header(KpRep *r, const KpRepGame *game);

/* Reads a whole file. -1 with the reason in *why (NULL allowed) and the message in err; r is then empty. */
int kp_rep_read(KpRep *r, const uint8_t *data, size_t size, KpRepRefusal *why, KpError *err);
/* The file of r (tail written when r->tail is set); *out is malloc'd. -1 out of memory. */
int kp_rep_write(const KpRep *r, uint8_t **out, size_t *size, KpError *err);
void kp_rep_free(KpRep *r);

/* The header's fields */
uint32_t kp_rep_frames(const KpRep *r);
int kp_rep_campaign(const KpRep *r);
uint32_t kp_rep_seed(const KpRep *r);
int kp_rep_speed(const KpRep *r);
int kp_rep_brood_war(const KpRep *r);
void kp_rep_player(const KpRep *r, int index, KpRepPlayer *p);
void kp_rep_set_player(KpRep *r, int index, const KpRepPlayer *p);
void kp_rep_set_frames(KpRep *r, uint32_t frames);
void kp_rep_set_seed(KpRep *r, uint32_t seed);

/* One block (ReadCompressedBlock 0x413ae0 / WriteCompressedBlock 0x4137d0). The reader reads exactly n bytes into
 * out at *pos and advances it; -1 on any bad length or checksum. The writer appends the block to *buf (grown with
 * realloc; *cap its capacity); -1 out of memory. */
int kp_rep_block_read(const uint8_t *data, size_t size, size_t *pos, uint8_t *out, size_t n);
int kp_rep_block_write(uint8_t **buf, size_t *len, size_t *cap, const uint8_t *in, size_t n);
/* A block's chunk (CompressChunk 0x413940): n <= KP_REP_CHUNK bytes imploded into scratch (KP_REP_CHUNK bytes) when that
 * is shorter, else the raw chunk; *out points at the bytes and the count is returned. work holds kp_rep_work_size()
 * bytes in any state: it is cleared first, so the bytes depend on the input alone. */
size_t kp_rep_work_size(void);
size_t kp_rep_chunk(const uint8_t *in, size_t n, uint8_t *scratch, void *work, const uint8_t **out);
/* The blocks' checksum: CRC-32 from 0xffffffff, no final inversion (0x402160). */
uint32_t kp_rep_crc(const uint8_t *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif
