/* Helpers shared by the pathing sources (kp_paths.c, kp_local_search.c, kp_placement.c, kp_formation.c); not part
 * of the library's API. */
#ifndef KP_PATHING_INTERNAL_H
#define KP_PATHING_INTERNAL_H

#include <stddef.h>

#include "kp_game_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* bytes.u8/u16/u32 (n = 1, 2, 4 little-endian bytes) at byte offset off of the RGNS string, with the index rules
 * of string.byte (a negative position counts from the end); fails where the Lua arithmetic meets a missing byte. */
int64_t kp_rgns_read(KpGame *g, int64_t off, int n);

/* The signed 16-bit position of unit n's sprite (s16(spr.x[s]), s16(spr.y[s])). */
void kp_sprite_pos(KpGame *g, int64_t n, int64_t *x, int64_t *y);

/* The region graph's arrays, checked: a region id outside 0..count-1 fails (nil in Lua). */
int64_t kp_rg_check(KpGame *g, int64_t id);

/* The binary heap of game/pathing/heap.lua over node indices: slots[1..*count], slots[0] the sentinel node
 * (whose priority is -1). The priority of node i is the int64_t at base + i * size + offset. */
typedef struct {
	int64_t *slots;
	int64_t *count;
	char *base;
	size_t size, offset;
	int64_t sentinel;
	int64_t capacity; /* slots available after slot 0 */
} KpHeap;

void kp_heap_insert(KpGame *g, const KpHeap *h, int64_t node);
void kp_heap_sift_down(const KpHeap *h, int64_t i);
int64_t kp_heap_pop(const KpHeap *h);
void kp_heap_change(const KpHeap *h, int64_t node, int64_t prio);

#ifdef __cplusplus
}
#endif

#endif
