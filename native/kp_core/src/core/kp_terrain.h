/* The tile state of the C game (stage N.5): tile flags, megatiles, the creep engine and the vision templates
 * (kp_terrain.c, kp_fog.c, kp_creep.c), and the cloaking and detection of units (kp_detection.c). KpTerrain is
 * a member of KpGame. */
#ifndef KP_TERRAIN_H
#define KP_TERRAIN_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct KpGame;
struct KpImages;

#define KP_TILES 0x10000 /* the native arrays always hold 256 x 256 entries */
#define KP_SIGHTS 12
#define KP_SIGHT_RECORDS ((3 + 2 * (KP_SIGHTS - 1)) * (3 + 2 * (KP_SIGHTS - 1)))
#define KP_RECEDE_BUCKETS 9
#define KP_RECEDE_NODES 1024

/* GenerateVisionRecords 0x466bd0 for the map stride: record k (0-based)
 * with its tile offset, its position and the 1-based indices of its dependencies (dep2 0 for none). */
typedef struct {
	int64_t width, height, inner, outer;
	int64_t off[KP_SIGHT_RECORDS], dx[KP_SIGHT_RECORDS], dy[KP_SIGHT_RECORDS];
	int64_t dep1[KP_SIGHT_RECORDS], dep2[KP_SIGHT_RECORDS], snap[KP_SIGHT_RECORDS];
} KpVisionTemplate;

typedef struct {
	/* g.tiles, g.megatiles, g.raw_tiles (MTXM words), g.creep_backup, g.creep_border (0 where unset) */
	int64_t tiles[KP_TILES], megatiles[KP_TILES], raw_tiles[KP_TILES];
	int64_t creep_backup[KP_TILES], creep_border[KP_TILES];
	/* g.recede (InitRecedingCreepPool 0x4b44a0) */
	int64_t recede_timer, recede_free, recede_retry, recede_buckets[KP_RECEDE_BUCKETS];
	/* its nodes 1..1024 (0 none): bucket links, tile and bucket; the bucket heads (bucket 9 is the free list, its count
	 * recede_free); the node of each receding tile, in place of the original's hash of 1024 chains */
	int64_t recede_next[KP_RECEDE_NODES + 1], recede_prev[KP_RECEDE_NODES + 1], recede_x[KP_RECEDE_NODES + 1];
	int64_t recede_y[KP_RECEDE_NODES + 1], recede_bucket[KP_RECEDE_NODES + 1], recede_head[KP_RECEDE_BUCKETS + 1];
	int64_t recede_node[KP_TILES];
	/* g.terrain_changed of kp_refresh_remembered_terrain: unset until its first run, then a tile set */
	int has_terrain_changed;
	uint8_t terrain_changed[KP_TILES];
	/* the tileset (g.tileset_data: cv5 and vf4 strings, mflags[megatile]); owned copies */
	uint8_t *cv5, *vf4;
	size_t cv5_size, vf4_size;
	int64_t *mflags;
	int64_t mflags_count;
	/* g.vision_templates (fog_init) */
	int has_templates;
	KpVisionTemplate templates[KP_SIGHTS];
} KpTerrain;

struct KpGame;

/* The tileset of g.tileset_data (copied); -1 when out of memory. mflags[0..count-1]. */
int kp_terrain_set_tileset(struct KpGame *g, const uint8_t *cv5, size_t cv5_size, const uint8_t *vf4,
	size_t vf4_size, const int64_t *mflags, int64_t mflags_count);
void kp_terrain_free(struct KpGame *g);
/* The megatile flags of a tileset's VF4 (the counting rules at 0x445562; scdata/derive/terrain.py megatile_flags,
 * the tileset container's MFLG): one per 32-byte record of 16 mini-tile words, size / 32 of them into out. */
void kp_terrain_megatile_flags(const uint8_t *vf4, size_t size, uint32_t *out);
/* The images world's tile flags read from the game's (KpTileFlags with the game as context). */
int kp_game_tile_flags(struct KpImages *w, void *ctx, int32_t index, uint32_t *flags);

/* kp_terrain.c */
int64_t kp_megatile_at(struct KpGame *g, int64_t i);
int64_t kp_cv5_megatile(struct KpGame *g, int64_t word);
int64_t kp_minitile_flags(struct KpGame *g, int64_t mega, int64_t mx, int64_t my);

/* kp_fog.c */
void kp_fog_init(struct KpGame *g);
void kp_tiles_or(struct KpGame *g, int64_t tx, int64_t ty, int64_t w, int64_t h, int64_t mask);
void kp_tiles_clear(struct KpGame *g, int64_t tx, int64_t ty, int64_t w, int64_t h, int64_t mask);
int64_t kp_terrain_height(struct KpGame *g, int64_t x, int64_t y);
void kp_reveal_sight(struct KpGame *g, int64_t x, int64_t y, int air, int64_t mask, int64_t sight);
void kp_refresh_remembered_terrain(struct KpGame *g);
/* The original's game screen in tiles (RefreshRememberedTerrainOnscreen 0x40b220). */
#define KP_SCREEN_TILES_W 21
#define KP_SCREEN_TILES_H 14
void kp_refresh_terrain_onscreen(struct KpGame *g, int64_t x0, int64_t y0, int64_t w, int64_t h);
void kp_reset_tile_visibility(struct KpGame *g);
/* ApplyVisionCheats 0x46c070: the one-shot tile rewrite of black sheep wall (kp_fog.c). */
void kp_apply_vision_cheats(struct KpGame *g);
int64_t kp_unit_sight(struct KpGame *g, int64_t n, int ignore_blind);
int kp_unit_has_visible_cargo(struct KpGame *g, int64_t n);
int kp_is_transport(struct KpGame *g, int64_t n);
void kp_unit_update_vision(struct KpGame *g, int64_t n);
void kp_unit_update_sprite_visibility(struct KpGame *g, int64_t n);

/* kp_detection.c (kp_units_in_rect and kp_unit_box_within are in kp_spatial.c) */
int kp_is_detector(struct KpGame *g, int64_t n);
int kp_has_status_effect(struct KpGame *g, int64_t n);
int64_t kp_detection_mask(struct KpGame *g, int64_t n);
void kp_update_detection(struct KpGame *g, int64_t n);
void kp_forget_target(struct KpGame *g, int64_t m, int64_t n);
void kp_clear_lost_targets(struct KpGame *g, int64_t n, int64_t players);
void kp_begin_cloak(struct KpGame *g, int64_t n, int64_t sound);
void kp_decloak(struct KpGame *g, int64_t n, int64_t sound);

/* kp_creep.c */
void kp_creep_init(struct KpGame *g);
void kp_creep_recede_tick(struct KpGame *g);
void kp_receding_neighbours(struct KpGame *g, int64_t x, int64_t y);
void kp_creep_apply(struct KpGame *g, int64_t x, int64_t y, int64_t mode, int64_t arg);
void kp_creep_refresh_borders(struct KpGame *g, int64_t x, int64_t y);
int kp_tile_cannot_have_creep(struct KpGame *g, int64_t x, int64_t y);
/* returns `large`; the bounds through the out-parameters */
int kp_creep_bounds(struct KpGame *g, int64_t t, int64_t cx, int64_t cy, int completed, int64_t *left, int64_t *top,
	int64_t *right, int64_t *bottom);
int64_t kp_creep_spread_once(struct KpGame *g, int64_t t, int64_t cx, int64_t cy);
int kp_creep_remove_provider(struct KpGame *g, int64_t t, int64_t cx, int64_t cy, int completed);
int kp_creep_cancel_receding(struct KpGame *g, int64_t x, int64_t y);
int kp_creep_retry_provider(struct KpGame *g, int64_t n);
void kp_creep_clear_area(struct KpGame *g, int64_t t, int64_t cx, int64_t cy);
void kp_creep_place_initial(struct KpGame *g, int64_t t, int64_t cx, int64_t cy);
int kp_creep_provider_state(int64_t t, int completed);
/* The map's creep tiles (CV5 group 1) and, in *receding, how many of them are receding (the development checks). */
int64_t kp_creep_tiles(const struct KpGame *g, int64_t *receding);
void kp_creep_stamp_building(struct KpGame *g, int64_t t, int64_t cx, int64_t cy);
void kp_creep_restore_provider(struct KpGame *g, int64_t n);

#ifdef __cplusplus
}
#endif

#endif
