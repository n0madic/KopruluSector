/* The tile state of the C game (stage N.5): tile flags, megatiles, the creep engine and the vision templates
 * (game/vision/fog.lua, game/terrain/creep.lua and the terrain lookups of game/simulation/game.lua, the
 * specification). KpTerrain is a member of KpGame. */
#ifndef KP_TERRAIN_H
#define KP_TERRAIN_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KP_TILES 0x10000 /* the native arrays always hold 256 x 256 entries */
#define KP_SIGHTS 12
#define KP_SIGHT_RECORDS ((3 + 2 * (KP_SIGHTS - 1)) * (3 + 2 * (KP_SIGHTS - 1)))
#define KP_RECEDE_BUCKETS 9

/* GenerateVisionRecords 0x466bd0 for the map stride: record k (0-based here; the Lua lists are 1-based)
 * with its tile offset, its position and the 1-based indices of its dependencies (dep2 0 for none). */
typedef struct {
	int64_t width, height, inner, outer;
	int64_t off[KP_SIGHT_RECORDS], dx[KP_SIGHT_RECORDS], dy[KP_SIGHT_RECORDS];
	int64_t dep1[KP_SIGHT_RECORDS], dep2[KP_SIGHT_RECORDS], snap[KP_SIGHT_RECORDS];
} KpVisionTemplate;

typedef struct {
	/* g.tiles, g.megatiles, g.raw_tiles (MTXM words), g.creep_backup, g.creep_border (nil there = 0 here) */
	int64_t tiles[KP_TILES], megatiles[KP_TILES], raw_tiles[KP_TILES];
	int64_t creep_backup[KP_TILES], creep_border[KP_TILES];
	/* g.recede (InitRecedingCreepPool 0x4b44a0) */
	int64_t recede_timer, recede_free, recede_retry, recede_buckets[KP_RECEDE_BUCKETS];
	/* g.terrain_changed of refresh_remembered_terrain: nil until its first run, then a tile set */
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

/* game/simulation/game.lua */
int64_t kp_megatile_at(struct KpGame *g, int64_t i);
int64_t kp_cv5_megatile(struct KpGame *g, int64_t word);
int64_t kp_minitile_flags(struct KpGame *g, int64_t mega, int64_t mx, int64_t my);

/* game/vision/fog.lua */
void kp_visibility_bits_set(struct KpGame *g);
void kp_fog_init(struct KpGame *g);
void kp_tiles_or(struct KpGame *g, int64_t tx, int64_t ty, int64_t w, int64_t h, int64_t mask);
void kp_tiles_clear(struct KpGame *g, int64_t tx, int64_t ty, int64_t w, int64_t h, int64_t mask);
int64_t kp_terrain_height(struct KpGame *g, int64_t x, int64_t y);
void kp_reveal_cache_flush(struct KpGame *g);
void kp_reveal_sight(struct KpGame *g, int64_t x, int64_t y, int air, int64_t mask, int64_t sight);
void kp_refresh_remembered_terrain(struct KpGame *g);
void kp_reset_tile_visibility(struct KpGame *g);
int64_t kp_unit_sight(struct KpGame *g, int64_t n, int ignore_blind);
int kp_unit_has_visible_cargo(struct KpGame *g, int64_t n);
int kp_is_transport(struct KpGame *g, int64_t n);
void kp_unit_update_vision(struct KpGame *g, int64_t n);
void kp_unit_update_sprite_visibility(struct KpGame *g, int64_t n);

/* game/vision/detection.lua (units_in_rect and unit_box_within are in kp_spatial.c) */
int kp_is_detector(struct KpGame *g, int64_t n);
int kp_has_status_effect(struct KpGame *g, int64_t n);
int64_t kp_detection_mask(struct KpGame *g, int64_t n);
void kp_update_detection(struct KpGame *g, int64_t n);
void kp_forget_target(struct KpGame *g, int64_t m, int64_t n);
void kp_clear_lost_targets(struct KpGame *g, int64_t n, int64_t players);
void kp_begin_cloak(struct KpGame *g, int64_t n, int64_t sound);

/* game/terrain/creep.lua */
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
void kp_creep_place_initial(struct KpGame *g, int64_t t, int64_t cx, int64_t cy);

#ifdef __cplusplus
}
#endif

#endif
