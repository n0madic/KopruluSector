/*
 * The pathing state of the C game (stage N.4): the path record pool, the region graph and its
 * long search, the local path search, unit placement and the destination adjustments of the
 * group move, ported line by line from the original's functions. KpPathing is a member of
 * KpGame.
 *
 * Sources: kp_paths.c (pool, heap, regions, route), kp_local_search.c, kp_placement.c and
 * kp_formation.c (the adjustments).
 */
#ifndef KP_PATHING_H
#define KP_PATHING_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct KpGame;

/* The path record pool: 1024 records of 128 bytes, numbered 1..1024. */
#define KP_PATH_RECORDS 1024
#define KP_PATH_SIZE 0x80
#define KP_PATH_MAX_REQUEST 0x80
#define KP_PATH_SRC 0x00
#define KP_PATH_NEXT 0x04
#define KP_PATH_DEST 0x08
#define KP_PATH_FRAME 0x0c
#define KP_PATH_BLOCKER 0x10
#define KP_PATH_SPEED 0x14
#define KP_PATH_FLAGS 0x18
#define KP_PATH_RETRY 0x19
#define KP_PATH_SLIDE 0x1a
#define KP_PATH_STALE 1
#define KP_PATH_FULL_REGIONS 0x1b
#define KP_PATH_REGIONS 0x1c
#define KP_PATH_REGION_INDEX 0x1d
#define KP_PATH_POINTS 0x1e
#define KP_PATH_POINT_INDEX 0x1f
#define KP_PATH_PAYLOAD 0x20
#define KP_PATH_MAX_POINTS 0x17

/* The region graph */
#define KP_REGION_UNWALKABLE 0x1ffd
#define KP_REGION_SPLIT 0x2000
#define KP_REGION_MAX_REGIONS 50
#define KP_REGION_ARENA 349
#define KP_REGION_OPEN_LIMIT 0x7d
#define KP_REGION_START_COST 0x400000

/* The local path search */
#define KP_LOCAL_WINDOW 0x40
#define KP_LOCAL_EDGES 0x80
#define KP_LOCAL_MAX_NEIGHBORS 0x20
#define KP_LOCAL_ARENA 249
#define KP_LOCAL_OPEN_LIMIT 0x96
#define KP_LOCAL_GOAL_NODES 150
#define KP_LOCAL_NEAR_NODES 200
#define KP_LOCAL_MAX_CHAIN 0x80
#define KP_LOCAL_MAX_COLUMNS 0x80
#define KP_LOCAL_MAX_INTERVALS 10
#define KP_LOCAL_NO_POINT (-0x7d00) /* s16(0x8300) */
#define KP_LOCAL_START_COST 0x400000
#define KP_LOCAL_FINAL 0x1ffe
#define KP_LOCAL_REGION_IDS 0x9c4
#define KP_LOCAL_MAX_RECTS 64 /* swept rectangles of one expansion (at most 8 are made) */
#define KP_LOCAL_HASH 512

/* The route context capacities: a record's counts are signed bytes (unpack reads up to 127). */
#define KP_ROUTE_REGIONS 128
#define KP_ROUTE_POINTS 128

/* A search context (kp_route_new; the native context of 0x48ec50), shared by the region
 * search, the route, the local search and the destination adjustments. */
typedef struct {
	int64_t unit, src_x, src_y, dest_x, dest_y;
	int64_t target, box_left, box_top, box_right, box_bottom;
	int64_t goal_x, goal_y;
	int64_t partial, local_partial, reached, reached_end;
	int64_t regions[KP_ROUTE_REGIONS];
	int64_t region_cursor, region_count;
	int64_t point_x[KP_ROUTE_POINTS], point_y[KP_ROUTE_POINTS];
	int64_t point_index, point_count, full_count;
	int64_t src_region, dst_region;
	int64_t long_nodes, local_nodes, long_peak, local_peak;
	int64_t ext_left, ext_up, ext_right, ext_down; /* set by prepare (kp_paths.c) or the caller */
} KpRoute;

/* The region graph rg of regions_init (arrays over region ids 0..count-1, allocated by
 * kp_regions_init). neighbors[nb_at[i] .. nb_at[i] + nb_count[i] - 1] is region i's list. */
typedef struct {
	int64_t count;
	int64_t *klass, *group, *ground, *nb_count, *nb_at, *center_x, *center_y, *left, *top, *right, *bottom;
	int64_t *neighbors;
	int64_t *node; /* run-time slot: the region search's node (arena index, -1 for none) */
	int64_t *mark; /* run-time slot: the local search's neighbour-region counter */
	int64_t *block; /* the allocation */
} KpRegionGraph;

/* A node of the region search arena. */
typedef struct {
	int64_t region, parent, heur, cost, prio, closed;
} KpRegionNode;

/* A local edge array (the geometry context's +0x2d0 arrays). */
typedef struct {
	int64_t count;
	int64_t axis[KP_LOCAL_EDGES + 1], start[KP_LOCAL_EDGES + 1], stop[KP_LOCAL_EDGES + 1];
	int64_t dir[KP_LOCAL_EDGES + 1], info[KP_LOCAL_EDGES + 1];
} KpEdgeArray;

typedef struct {
	int64_t x, y, flags, goal;
} KpNeighbor;

/* The geometry context lg (0x69d930). active_local[d]: lg.active[d] is the local array loc[d]
 * (else the global contours g->contours[d], lg.saved[d]). edge[d] is -1 for none. */
typedef struct {
	int active_local[4];
	KpEdgeArray loc[4];
	int64_t ext[12];
	int64_t edge[4];
	KpNeighbor nb[KP_LOCAL_MAX_NEIGHBORS];
	int64_t nb_count;
	int64_t rects[KP_LOCAL_MAX_RECTS][4];
	int64_t rect_count;
	int64_t x, y, top, right, bottom, left;
	int64_t unit, goal_x, goal_y, target, box[4], goal_found, mask;
} KpLocalGeometry;

/* A y interval list of the swept areas. */
typedef struct {
	int64_t count;
	int64_t y0[KP_LOCAL_MAX_INTERVALS + 1], y1[KP_LOCAL_MAX_INTERVALS + 1];
} KpIntervals;

/* The swept areas of the local search. Entries persist between searches; the *_hw fields
 * are the highest index ever written (a read above it fails). */
typedef struct {
	int64_t count;
	int64_t xs[KP_LOCAL_MAX_COLUMNS + 2], colmap[KP_LOCAL_MAX_COLUMNS + 2];
	KpIntervals lists[KP_LOCAL_MAX_COLUMNS + 1];
	int64_t xs_hw, colmap_hw, lists_hw;
} KpVisited;

/* A node of the local search: 0 the start node, 1..KP_LOCAL_ARENA the arena. */
typedef struct {
	int64_t x, y, parent, region, flags, depth, in_next, goal, near, closed, heur, cost, prio;
} KpLocalNode;

typedef struct {
	/* The path record pool (the free head, subject and switch are the KP_PATH_* scalars) */
	uint8_t path_bytes[KP_PATH_RECORDS * KP_PATH_SIZE];
	/* The extents context of placement (kp_placement.c; indices 0..3 and 8..11) */
	int64_t extent_ctx[12];
	/* The region graph */
	KpRegionGraph rg;
	KpRegionNode rnodes[KP_REGION_ARENA + 2]; /* 0..ARENA: the arena; ARENA + 1: the heap sentinel */
	int64_t rheap[KP_REGION_OPEN_LIMIT + 2];
	int64_t rheap_count;
	/* The local path search */
	KpLocalGeometry lg;
	KpVisited visited;
	KpLocalNode lnodes[KP_LOCAL_ARENA + 2]; /* ARENA + 1: the heap sentinel */
	int64_t lnode_count;                     /* arena nodes in use */
	int64_t lheap[KP_LOCAL_OPEN_LIMIT + 2];
	int64_t lheap_count;
	int64_t hash_x[KP_LOCAL_HASH], hash_y[KP_LOCAL_HASH], hash_node[KP_LOCAL_HASH];
} KpPathing;

/* --- the path record pool (kp_paths.c) --- */
void kp_paths_init(struct KpGame *g);
int64_t kp_path_u8(struct KpGame *g, int64_t k, int64_t off);
int64_t kp_path_s8(struct KpGame *g, int64_t k, int64_t off);
void kp_path_w8(struct KpGame *g, int64_t k, int64_t off, int64_t v);
int64_t kp_path_u16(struct KpGame *g, int64_t k, int64_t off);
int64_t kp_path_s16(struct KpGame *g, int64_t k, int64_t off);
void kp_path_w16(struct KpGame *g, int64_t k, int64_t off, int64_t v);
int64_t kp_path_u32(struct KpGame *g, int64_t k, int64_t off);
void kp_path_w32(struct KpGame *g, int64_t k, int64_t off, int64_t v);
void kp_path_point(struct KpGame *g, int64_t k, int64_t off, int64_t *x, int64_t *y);
void kp_path_set_point(struct KpGame *g, int64_t k, int64_t off, int64_t x, int64_t y);
int64_t kp_path_alloc(struct KpGame *g, int64_t size);
void kp_path_release(struct KpGame *g, int64_t k);
void kp_path_free(struct KpGame *g, int64_t n);
/* Returns the record (or 0); *regions and *points are the counts written back. */
int64_t kp_path_alloc_counts(struct KpGame *g, int64_t regions, int64_t points, int64_t *out_regions,
	int64_t *out_points);
void kp_path_mark_stale(struct KpGame *g, int64_t n);
void kp_path_set_retry(struct KpGame *g, int64_t n, int64_t v);
int kp_path_poll_retry(struct KpGame *g, int64_t n);
int64_t kp_path_next_packed(struct KpGame *g, int64_t k);

/* --- the region graph (kp_paths.c) --- */
void kp_regions_init(struct KpGame *g);
void kp_pathing_free(struct KpGame *g); /* frees the region graph of kp_regions_init */
int64_t kp_region_group_of(struct KpGame *g, int64_t id);
int kp_same_ground_group(struct KpGame *g, int64_t a, int64_t b);
int kp_region_ground_neighbors(struct KpGame *g, int64_t id, int64_t *out, int max);
void kp_region_box(struct KpGame *g, int64_t id, int64_t *left, int64_t *top, int64_t *right, int64_t *bottom);
int kp_region_inaccessible(struct KpGame *g, int64_t id);
int64_t kp_reachable_substitute_region(struct KpGame *g, int64_t sx, int64_t sy, int64_t dx, int64_t dy);
int64_t kp_tile_region_first(struct KpGame *g, int64_t x, int64_t y);
int kp_search_region_path(struct KpGame *g, KpRoute *c);
/* mission_data.region_cell (G.region_cell): the raster word of map tile (x, y). */
int64_t kp_region_cell(struct KpGame *g, int64_t x, int64_t y);

/* --- a unit's route (kp_paths.c) --- */
void kp_route_new(KpRoute *c, int64_t unit, int64_t sx, int64_t sy, int64_t dx, int64_t dy);
int kp_path_search_init(struct KpGame *g, KpRoute *c);
int kp_path_search_continue(struct KpGame *g, KpRoute *c);
int kp_units_at_point(struct KpGame *g, int64_t n, int64_t x, int64_t y, int64_t *out, int max);
/* Returns found; (*fx, *fy) is the point found or where the spiral ended. */
int kp_footprint_near(struct KpGame *g, const KpRoute *c, int64_t x, int64_t y, int64_t *fx, int64_t *fy);
int kp_path_segment(struct KpGame *g, KpRoute *c);
int kp_snap_goal_into_region(struct KpGame *g, KpRoute *c, int64_t region);
void kp_path_set_direct(struct KpGame *g, int64_t n, int64_t nx, int64_t ny, int64_t dx, int64_t dy);
/* Returns ok; the point only on 1. */
int kp_next_path_point(struct KpGame *g, int64_t n, int64_t tx, int64_t ty, int64_t *x, int64_t *y);
void kp_move_to_intercept(struct KpGame *g, int64_t n, int64_t x, int64_t y);
int kp_path_keeps_waypoint(struct KpGame *g, int64_t n);
void kp_set_path_waypoint(struct KpGame *g, int64_t n, int64_t x, int64_t y);
int kp_advance_path_waypoint(struct KpGame *g, int64_t n, int64_t tx, int64_t ty);

/* --- the local path search (kp_local_search.c) --- */
void kp_local_geometry_init(struct KpGame *g);
int kp_path_obstacle(struct KpGame *g, int64_t c, int64_t mover);
int kp_units_collide(struct KpGame *g, int64_t a, int64_t b);
int kp_local_path_search(struct KpGame *g, KpRoute *c, int64_t next_region, int64_t gx, int64_t gy);

/* --- placement (kp_placement.c) --- */
void kp_set_box_extents(int64_t *e, int64_t left, int64_t up, int64_t right, int64_t down);
void kp_set_extents(struct KpGame *g, int64_t code, int64_t *e); /* SetUnitTypeExtents over units.dat extents */
int64_t kp_region_group(struct KpGame *g, int64_t region);
void kp_contour_extents(struct KpGame *g, int64_t code);
int kp_contour_fits(struct KpGame *g, int64_t x, int64_t y);
int kp_point_walkable(struct KpGame *g, int64_t x, int64_t y);
int kp_unit_type_fits(struct KpGame *g, int64_t t, int64_t x, int64_t y);
int kp_box_fits(struct KpGame *g, int64_t x, int64_t y, int64_t w, int64_t h);
int kp_placement_blocks(struct KpGame *g, int64_t c, int64_t wanted);
int64_t kp_placement_blocker(struct KpGame *g, int64_t n, int64_t left, int64_t top, int64_t right, int64_t bottom);
int64_t kp_region_at(struct KpGame *g, int64_t x, int64_t y);
int kp_placement_reachable(struct KpGame *g, int64_t n, int64_t x, int64_t y);
void kp_type_box(struct KpGame *g, int64_t t, int64_t x, int64_t y, int64_t *left, int64_t *top, int64_t *right,
	int64_t *bottom);
/* rect: {left, top, right, bottom}. Returns 1 with the point, 0 for none. */
int kp_placement_ring(struct KpGame *g, int64_t n, int64_t x, int64_t y, int64_t w, int64_t h, const int64_t *rect,
	int64_t *px, int64_t *py);
/* rect: {left, top, right, bottom} (clamped in place) or NULL for +-128. Returns 1 with the point, 0 for none
 * (the error word set). */
int kp_find_unit_placement(struct KpGame *g, int64_t n, int64_t x, int64_t y, int64_t *rect, int displace,
	int64_t *px, int64_t *py);

/* --- the destination adjustments (kp_formation.c) --- */
/* box: {left, up, right, down} */
void kp_adjust_destination_fit(struct KpGame *g, int64_t x, int64_t y, const int64_t *box, int64_t ox, int64_t oy,
	int64_t *rx, int64_t *ry);
void kp_adjust_move_destination(struct KpGame *g, int64_t n, int64_t x, int64_t y, int64_t *rx, int64_t *ry);
void kp_adjust_group_destination(struct KpGame *g, int64_t x, int64_t y, int64_t w, int64_t h, int64_t ox, int64_t oy,
	int64_t *rx, int64_t *ry);

#ifdef __cplusplus
}
#endif

#endif
