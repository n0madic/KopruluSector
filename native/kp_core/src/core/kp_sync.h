/*
 * The sync command 0x37 of a network game (phase N, stage N4; ADR 0026; atlas P27 0x47a250..0x47a570, P05
 * 0x417540..0x417610, P24 0x46c8e0 and 0x46d510, P09 0x427520): the ring of 16 sync records a seat fills at its turn
 * boundaries, the 7 bytes it sends after each of its turns, and the check of another seat's command against the record
 * of the same ring index.
 *
 * A record holds:
 *   - a hash of the shared state (ComputeSyncValue 0x47a360): by the schedule {1, 2} the units' (HashUnitState 0x427520
 *     over the active list) or the resources' with the random sequence and the frame (0x47a250), mixed with one row
 *     of the tile flags each time (0x47a290) and the seat's vision masks;
 *   - the minimap's two statistics (DrawMinimapUnits 0x46d510): the parity of the units each player sees, and the low
 *     byte of how many the seat's minimap shows;
 *   - the sprites' visibility parities per row (ComputeSpriteVisibilityChecksum 0x417540) and the seat's checksum byte
 *     of the 14 rows from the top of its view.
 *
 * The original takes the minimap's statistics at the turn boundary (PollTurn 0x444cc0) and the sprites' in the draw
 * that follows the first frame after it (BuildSpriteDrawList 0x416a00). Koprulu takes both inside the step: the
 * minimap's after the turn's commands (kp_sync_capture_minimap), the sprites' after the turn's first frame
 * (kp_sync_capture_sprites), with the view's first row given by the interface. A seat that draws late, twice or never
 * thus sends what the original would have sent.
 *
 * KpSync is the seat's network state, not the game's: neither saved nor hashed (the view's row and the seat's vision
 * mask are its own). Its functions read the game and change nothing in it.
 */
#ifndef KP_SYNC_H
#define KP_SYNC_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct KpGame;

#define KP_SYNC_ID 0x37
#define KP_SYNC_SIZE 7       /* the command's bytes (ExecuteTurnCommands 0x475d30's length of 0x37) */
#define KP_SYNC_RING 16      /* 0x673f28: the records */
#define KP_SYNC_SCHEDULE 32  /* 0x674fe8: the kinds' schedule */
#define KP_SYNC_ROWS 256     /* the sprites' rows (0x5c59d8) */
#define KP_SYNC_VIEW_ROWS 14 /* the rows of the seat's checksum byte, from the first row of its view */
#define KP_SYNC_VIEW_ABOVE 4 /* PrepareSpriteDrawRows 0x445de0: the view's first row, 4 above its top tile */
#define KP_SYNC_SPRITE_IDS 517 /* sprites.dat's entries: the flags 0x5aef30 by sprite id */

/* The kinds of a record's hash */
enum { KP_SYNC_INDEX, KP_SYNC_UNITS, KP_SYNC_RESOURCES };

/* A ring record (0x10c bytes at 0x673f28) */
typedef struct {
	uint16_t hash;                /* +0: the low 16 bits of ComputeSyncValue */
	uint8_t kind;                 /* +2 */
	uint8_t sprites;              /* +3: the seat's sprites' checksum byte */
	uint8_t minimap_xor;          /* +4: the low byte of the units' visibility masks' XOR */
	uint8_t minimap_count;        /* +5: the low byte of the units the seat's minimap shows */
	uint32_t row;                 /* +8: the first row of the seat's checksum */
	uint8_t parity[KP_SYNC_ROWS]; /* +0xc: per row, the parity of each player's view of the hashed sprites */
} KpSyncRecord;

typedef struct {
	KpSyncRecord ring[KP_SYNC_RING];
	uint8_t index;                       /* 0x673f22 */
	uint8_t pos;                         /* 0x673f23: the schedule's position */
	uint8_t length;                      /* 0x673f24: the schedule's length (0 alone, 2 in a network game) */
	uint8_t schedule[KP_SYNC_SCHEDULE];  /* 0x674fe8 */
	uint8_t minimap_xor, minimap_count;  /* 0x673f20, 0x673f21: the last boundary's minimap statistics */
	uint32_t terrain_row;                /* 0x675008: the row of the tile flags hashed next */
	/* the sprites' checksum: the request 0x5c59d0, the byte 0x5c59d4, the row 0x5c5ad8 and the parities 0x5c59d8 of the
	 * last one served (the zeros of InitSpriteVisibilityChecksum 0x4163a0 before) */
	int requested;
	uint8_t sprites;
	uint32_t sprite_row;
	uint8_t parity[KP_SYNC_ROWS];
	/* InitSpriteVisibilityChecksum 0x4163a0's flags 0x5aef30: the sprite ids whose visibility is hashed */
	uint8_t hashed[KP_SYNC_SPRITE_IDS];
} KpSync;

/* SetGameLoopActive 0x476e00's ResetSyncRing 0x47a400 at the game loop's start: the ring and the schedule cleared, the
 * schedule {1, 2} in a network game, the sprites' checksum cleared (InitSpriteVisibilityChecksum 0x4163a0 at the
 * game's start), then the first record filled (AdvanceSyncRing). */
void kp_sync_reset(KpSync *s, struct KpGame *g, int multiplayer);
/* AdvanceSyncRing 0x47a490 after each executed turn: the next record, of the schedule's next kind, with the minimap's
 * statistics and the sprites' checksum taken before. */
void kp_sync_advance(KpSync *s, struct KpGame *g);
/* ComputeSyncValue 0x47a360 of a kind (it hashes the next terrain row). */
uint32_t kp_sync_value(KpSync *s, struct KpGame *g, int kind);
/* QueueSyncCommand 0x47a300: the 7 bytes of the current record into out. */
void kp_sync_command(const KpSync *s, uint8_t out[KP_SYNC_SIZE]);
/* CheckSyncCommand 0x47a510 with CheckSyncVisibilityHash 0x47a570: 1 when another seat's command (KP_SYNC_SIZE bytes)
 * matches this seat's record of its ring index, the sender's game player `player` (0x508d44, 0..7). */
int kp_sync_check(const KpSync *s, const uint8_t *cmd, int64_t player);
/* PollTurn 0x444cc0's multiplayer branch after a turn: RedrawMinimap 0x46e0e0's two statistics (DrawMinimapUnits
 * 0x46d510 over the game's units and the seat's vision mask), captured (CaptureMinimapDrawStats 0x47a2f0), and the
 * sprites' checksum requested (RequestSpriteVisibilityChecksum 0x417610). */
void kp_sync_capture_minimap(KpSync *s, struct KpGame *g);
/* The draw after a frame (BuildSpriteDrawList 0x416a00): a pending request served, the view's first row `first_row`
 * (kp_sync_view_row). Without a request nothing changes, as an extra draw changes nothing. */
void kp_sync_capture_sprites(KpSync *s, struct KpGame *g, uint32_t first_row);
/* PrepareSpriteDrawRows 0x445de0's first row of a view whose top tile row is `screen_tile_y`. */
uint32_t kp_sync_view_row(int64_t screen_tile_y);
/* HashUnitState 0x427520 of unit n. */
uint32_t kp_sync_unit_hash(struct KpGame *g, int64_t n);

#ifdef __cplusplus
}
#endif

#endif
