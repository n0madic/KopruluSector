/* The tileset and the terrain lookups of the C game (kp_terrain.h; game/simulation/game.lua). */
#include "kp_game_internal.h"

#include <stdlib.h>
#include <string.h>

static uint8_t *copy_bytes(const uint8_t *src, size_t size)
{
	uint8_t *p = (uint8_t *)malloc(size ? size : 1);
	if (p && size) memcpy(p, src, size);
	return p;
}

void kp_terrain_free(KpGame *g)
{
	KpTerrain *t = &g->terrain;
	free(t->cv5);
	free(t->vf4);
	free(t->mflags);
	t->cv5 = t->vf4 = NULL;
	t->mflags = NULL;
	t->cv5_size = t->vf4_size = 0;
	t->mflags_count = 0;
}

int kp_terrain_set_tileset(KpGame *g, const uint8_t *cv5, size_t cv5_size, const uint8_t *vf4, size_t vf4_size,
	const int64_t *mflags, int64_t mflags_count)
{
	KpTerrain *t = &g->terrain;
	uint8_t *c = copy_bytes(cv5, cv5_size), *v = copy_bytes(vf4, vf4_size);
	int64_t *m = (int64_t *)malloc((size_t)(mflags_count > 0 ? mflags_count : 1) * sizeof *m);
	if (!c || !v || !m) {
		free(c);
		free(v);
		free(m);
		return -1;
	}
	if (mflags_count > 0) memcpy(m, mflags, (size_t)mflags_count * sizeof *m);
	kp_terrain_free(g);
	t->cv5 = c;
	t->cv5_size = cv5_size;
	t->vf4 = v;
	t->vf4_size = vf4_size;
	t->mflags = m;
	t->mflags_count = mflags_count;
	return 0;
}

/* The little-endian u16 at byte offset at of a tileset string (string.byte; a missing byte fails). */
static int64_t u16_at(KpGame *g, const uint8_t *s, size_t size, int64_t at, const char *what)
{
	if (!s || at < 0 || at + 2 > (int64_t)size) kp_game_fail(g, "terrain: %s offset %lld outside the data", what, (long long)at);
	return (int64_t)s[at] + (int64_t)s[at + 1] * 256;
}

static int64_t tile(KpGame *g, const int64_t *a, int64_t i, const char *what)
{
	if (i < 0 || i >= KP_TILES) kp_game_fail(g, "terrain: %s[%lld] is nil", what, (long long)i);
	return a[i];
}

/* Megatile of tile index i for terrain queries (0x445690's lookup): the tile word that creep replaced (the
 * creep engine's backup map, 0x66696c) or else the MTXM word, through CV5. */
int64_t kp_megatile_at(KpGame *g, int64_t i)
{
	int64_t word = tile(g, g->terrain.creep_backup, i, "creep_backup");
	if (word == 0) word = tile(g, g->terrain.raw_tiles, i, "raw_tiles");
	return kp_cv5_megatile(g, word);
}

/* The megatile of an MTXM tile word through CV5 (group * 26 + subtile WORDs from +0x14). */
int64_t kp_cv5_megatile(KpGame *g, int64_t word)
{
	int64_t group = kp_band(kp_shrv(word, 4), 0x7ff);
	return u16_at(g, g->terrain.cv5, g->terrain.cv5_size, group * 52 + 20 + kp_band(word, 15) * 2, "cv5");
}

/* The VF4 mini-tile flags of a megatile cell. */
int64_t kp_minitile_flags(KpGame *g, int64_t mega, int64_t mx, int64_t my)
{
	return u16_at(g, g->terrain.vf4, g->terrain.vf4_size, ((mega * 4 + my) * 4 + mx) * 2, "vf4");
}

int kp_game_tile_flags(KpImages *w, void *ctx, int32_t index, uint32_t *flags)
{
	KpGame *g = (KpGame *)ctx;
	(void)w;
	if (index < 0 || index >= KP_TILES) return 0;
	*flags = (uint32_t)g->terrain.tiles[index];
	return 1;
}
