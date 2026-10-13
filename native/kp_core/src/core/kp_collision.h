/* The collision checks and the air repulsion of the C game (stage N.4): kp_collision.c ports
 * game/units/collision.lua, kp_repulse.c game/units/repulse.lua (the specifications). KpCollision
 * is a member of KpGame.
 *
 * kp_game.h includes this header before it defines its record counts, so the sizes below are
 * spelled out (kp_collision.c checks them against KP_UNITS and KP_SPATIAL_CAPACITY). */
#ifndef KP_COLLISION_H
#define KP_COLLISION_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* repulse.lua: the air density grid self.density (0x67cf60), cell = repulse_x + repulse_y * 0xab with
 * repulse_x, repulse_y in 0..255 (AddAirDensity stores them modulo 256), so cells 0..255 + 255 * 0xab. */
#define KP_DENSITY_COLUMNS 0xab
#define KP_DENSITY_CELLS (255 + 255 * KP_DENSITY_COLUMNS + 1)

/* The longest lists of units_swept (two scans of at most the entry count each) and units_adjacent (four
 * scans), and of a spatial query (each unit once): KP_SPATIAL_CAPACITY = 0x6a40 / 8, KP_UNITS = 1700. */
#define KP_COLLISION_ENTRIES (0x6a40 / 8)
#define KP_COLLISION_SWEPT_MAX (2 * KP_COLLISION_ENTRIES)
#define KP_COLLISION_ADJACENT_MAX (4 * KP_COLLISION_ENTRIES)
#define KP_COLLISION_QUERY_MAX 1700

typedef struct {
	/* g.density[c] of the Lua game (nil there = 0 here); values 0..0xff. */
	int64_t density[KP_DENSITY_CELLS];
	/* Scratch lists of the collision checks (not game state). */
	int64_t swept[KP_COLLISION_SWEPT_MAX];
	int64_t adjacent[KP_COLLISION_ADJACENT_MAX];
	int64_t query[KP_COLLISION_QUERY_MAX];
} KpCollision;

/* kp_game.h defines the KpGame typedef after including this header. */
struct KpGame;

/* The empty density grid (game.lua M.new: density = {}). */
void kp_collision_init(struct KpGame *g);

/* game/units/collision.lua */
int kp_units_swept(struct KpGame *g, int64_t n, int64_t dx, int64_t dy, int64_t *out, int max);
int64_t kp_ground_unit_collision_any(struct KpGame *g, int64_t n);
int64_t kp_ground_unit_collision(struct KpGame *g, int64_t n);
int kp_terrain_collision(struct KpGame *g, int64_t n);
int kp_terrain_sweep_collision(struct KpGame *g, int64_t n, int64_t dx, int64_t dy);
void kp_contract_safe_rect(struct KpGame *g, int64_t n, int64_t x, int64_t y);
int kp_units_adjacent(struct KpGame *g, int64_t n, int64_t rx, int64_t ry, int64_t *out, int max);
/* `limit` is the Lua argument `max` (the neighbour count that stops the scan); out_max the capacity of out. */
int kp_moving_neighbors(struct KpGame *g, int64_t n, int64_t radius, int64_t limit, int64_t *out, int out_max);
int64_t kp_position_blocked(struct KpGame *g, int64_t n, int64_t x, int64_t y);
int kp_footprint_touches(struct KpGame *g, int64_t c, int64_t cx, int64_t cy, int64_t t, int64_t x, int64_t y);
int64_t kp_cardinal_side(struct KpGame *g, int64_t n, int64_t c);
int kp_blocker_heading_away(struct KpGame *g, int64_t n, int64_t c);
int64_t kp_terrain_slide_direction(struct KpGame *g, int64_t n);
int64_t kp_slide_direction(struct KpGame *g, int64_t n, int64_t c);
int kp_sweep_contours_clear(struct KpGame *g, int64_t x1, int64_t y1, int64_t x2, int64_t y2);

/* game/units/repulse.lua */
void kp_air_density_add(struct KpGame *g, int64_t n);
void kp_air_density_remove(struct KpGame *g, int64_t n);
int kp_air_repulsion(struct KpGame *g, int64_t n);
void kp_air_repulsion_settle(struct KpGame *g, int64_t n, int drifted);

#ifdef __cplusplus
}
#endif

#endif
