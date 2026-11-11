/* A colour's index in a palette (kp_index_map.h). */
#include "kp_index_map.h"

#include <stdlib.h>
#include <string.h>

#define KEY_USED 0x80000000u
#define CLASS_OPAQUE 1u
#define CLASS_TRANSLUCENT 2u
#define FIRST_CAP 256

int kp_index_map_init(KpIndexMap *m, const uint8_t palette[768])
{
	memset(m, 0, sizeof *m);
	memcpy(m->palette, palette, sizeof m->palette);
	m->keys = (uint32_t *)calloc(FIRST_CAP, sizeof *m->keys);
	m->values = (uint8_t *)malloc(FIRST_CAP);
	if (!m->keys || !m->values) {
		kp_index_map_free(m);
		return -1;
	}
	m->cap = FIRST_CAP;
	return 0;
}

void kp_index_map_free(KpIndexMap *m)
{
	free(m->keys);
	free(m->values);
	m->keys = NULL;
	m->values = NULL;
	m->cap = m->count = 0;
}

static size_t slot_of(uint32_t key, size_t cap) { return (size_t)((key * 2654435761u) >> 8) & (cap - 1); }

static int grow(KpIndexMap *m)
{
	size_t cap = m->cap * 2, k;
	uint32_t *keys = (uint32_t *)calloc(cap, sizeof *keys);
	uint8_t *values = (uint8_t *)malloc(cap);
	if (!keys || !values) {
		free(keys);
		free(values);
		return -1;
	}
	for (k = 0; k < m->cap; k++) {
		if (m->keys[k]) {
			size_t s = slot_of(m->keys[k], cap);
			while (keys[s]) s = (s + 1) & (cap - 1);
			keys[s] = m->keys[k];
			values[s] = m->values[k];
		}
	}
	free(m->keys);
	free(m->values);
	m->keys = keys;
	m->values = values;
	m->cap = cap;
	return 0;
}

/* The rule for a colour not asked for before */
static uint8_t find(const KpIndexMap *m, uint8_t r, uint8_t g, uint8_t b, int opaque)
{
	int i, found = -1, best = 0;
	int32_t best_d = 0x7fffffff;
	for (i = 0; i < 256; i++) {
		const uint8_t *c = m->palette + 3 * i;
		int32_t dr = (int32_t)c[0] - r, dg = (int32_t)c[1] - g, db = (int32_t)c[2] - b, d = dr * dr + dg * dg + db * db;
		if (d == 0) {
			found = i;
			if (opaque) break; /* the lowest; a translucent pixel takes the highest */
		}
		if (d < best_d) {
			best_d = d;
			best = i;
		}
	}
	return (uint8_t)(found >= 0 ? found : best);
}

uint8_t kp_index_map(KpIndexMap *m, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
	uint32_t key;
	size_t s;
	uint8_t v;
	if (a == 0) return 0;
	key = KEY_USED | (a == KP_INDEX_OPAQUE ? CLASS_OPAQUE : CLASS_TRANSLUCENT) << 24 | (uint32_t)r << 16
		| (uint32_t)g << 8 | b;
	if (m->cap) {
		for (s = slot_of(key, m->cap); m->keys[s]; s = (s + 1) & (m->cap - 1)) {
			if (m->keys[s] == key) return m->values[s];
		}
	}
	v = find(m, r, g, b, a == KP_INDEX_OPAQUE);
	if (m->cap && (m->count + 1) * 2 > m->cap && grow(m) != 0) return v;
	if (m->cap) {
		for (s = slot_of(key, m->cap); m->keys[s]; s = (s + 1) & (m->cap - 1)) {
		}
		m->keys[s] = key;
		m->values[s] = v;
		m->count++;
	}
	return v;
}
