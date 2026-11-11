/* A colour's index in a palette by the rule StarCraft: Remastered's images are brought back to 1.17's palette-indexed
 * files with (ADR 0018, stage D6; converter/casc/rebuild.py IndexMap): alpha 0 is index 0 (transparent); an opaque
 * pixel takes the lowest index of its colour, a translucent one the highest (1.17's files use their duplicate colours
 * so); a colour the palette lacks the nearest one (the least squared distance, the lowest index of equals). Kept per
 * colour as it is first asked for. */
#ifndef KP_INDEX_MAP_H
#define KP_INDEX_MAP_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KP_INDEX_OPAQUE 255

typedef struct {
	uint8_t palette[768]; /* 256 x R, G, B */
	uint32_t *keys;       /* the colours asked for (KEY_USED | class << 24 | R << 16 | G << 8 | B), 0 empty */
	uint8_t *values;
	size_t cap, count;
} KpIndexMap;

/* 0, -1 out of memory. */
int kp_index_map_init(KpIndexMap *m, const uint8_t palette[768]);
void kp_index_map_free(KpIndexMap *m);
/* The index of a colour; a failure to grow the memo (out of memory) still answers, without keeping it. */
uint8_t kp_index_map(KpIndexMap *m, uint8_t r, uint8_t g, uint8_t b, uint8_t a);

#ifdef __cplusplus
}
#endif

#endif
