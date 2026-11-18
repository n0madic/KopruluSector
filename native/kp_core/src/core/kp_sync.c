/* The sync command 0x37 of a network game (kp_sync.h). */
#include "kp_sync.h"

#include <string.h>

#include "kp_game_internal.h"

#define BUILDING 0x1 /* units.dat special ability flags 0x521660 */
#define SUBUNIT 0x10
#define STAREDIT_AVAILABLE 0x1 /* units.dat StarEdit availability 0x524708 */
#define CLOAKED 0x300           /* CUnit +0xdc: requires detection, cloaked */
#define DISRUPTION_WEB 105
#define DARK_SWARM 202
#define FIRST_DOOR 203 /* 203..213: the doors and traps */
#define LAST_DOOR 213
#define TERRAIN_FLAGS 0xbfffffffu /* HashNextTerrainRow 0x47a290's mask of a tile's flags */

static uint32_t rol(uint32_t v, unsigned n) { return (v << n) | (v >> (32 - n)); }

/* sx: the sign-extended WORD of a coordinate */
static uint32_t sx(int64_t v) { return (uint32_t)kp_s16((uint32_t)v); }

uint32_t kp_sync_unit_hash(KpGame *g, int64_t n)
{
	const KpUnits *u = &g->u;
	int32_t s = kp_unit_sprite(g, n);
	uint32_t h = rol((uint32_t)u->type[n] & 0xffff, 3) ^ (uint32_t)u->shields[n];
	h = rol(h, 3) ^ ((uint32_t)u->energy[n] & 0xffff);
	h = rol(h, 3) ^ sx(g->w->spr.x[s]);
	h = rol(h, 3) ^ sx(g->w->spr.y[s]);
	h = rol(h, 3) ^ (uint32_t)kp_sar(kp_s32((uint32_t)u->hp[n] + 0xffu), 8);
	return rol(h, 3);
}

/* HashResourcesAndRng 0x47a250 */
static uint32_t resources_hash(KpGame *g)
{
	uint32_t h = 0;
	int p;
	for (p = KP_PLAYERS - 1; p >= 0; p--) {
		h = rol(h ^ (uint32_t)g->players.minerals[p], 3);
		h = rol(h ^ (uint32_t)g->players.gas[p], 3);
	}
	h = rol(rol(h, 3) ^ g->rng->state, 3);
	return rol(h ^ (uint32_t)kp_scalar(g, KP_FRAME), 3);
}

/* HashNextTerrainRow 0x47a290 */
static uint32_t terrain_row_hash(KpSync *s, KpGame *g)
{
	uint32_t h = 0, r = s->terrain_row;
	int64_t x;
	if ((int64_t)r < g->map_h)
		for (x = 0; x < g->map_w; x++) h = rol(h ^ ((uint32_t)g->terrain.tiles[(int64_t)r * g->map_w + x] & TERRAIN_FLAGS), 3);
	s->terrain_row = (int64_t)r + 1 < g->map_h ? r + 1 : 0;
	return h;
}

uint32_t kp_sync_value(KpSync *s, KpGame *g, int kind)
{
	uint32_t h, t, v, vision = (uint32_t)g->local_vision;
	if (kind == KP_SYNC_UNITS) {
		int64_t n;
		h = 0;
		for (n = g->lists[KP_LIST_ACTIVE].head; n != 0; n = g->u.next[n]) h = rol(h ^ kp_sync_unit_hash(g, n), 3);
	} else if (kind == KP_SYNC_RESOURCES) {
		h = resources_hash(g);
	} else {
		h = s->index;
	}
	/* the explored mask 0x67266c is the vision mask 0x672668 in the unexplored byte */
	t = terrain_row_hash(s, g) ^ (((vision << 8) ^ vision) << ((8 - (uint32_t)g->local_player) & 31));
	v = h ^ t;
	return v ^ (v >> 16);
}

void kp_sync_advance(KpSync *s, KpGame *g)
{
	KpSyncRecord *rec;
	uint32_t v;
	s->index = s->index + 1 < KP_SYNC_RING ? (uint8_t)(s->index + 1) : 0;
	s->pos = s->pos + 1 < s->length ? (uint8_t)(s->pos + 1) : 0;
	rec = &s->ring[s->index];
	v = kp_sync_value(s, g, s->schedule[s->pos]);
	rec->minimap_xor = s->minimap_xor;
	rec->minimap_count = s->minimap_count;
	rec->hash = (uint16_t)v;
	rec->kind = s->schedule[s->pos];
	/* GetSpriteVisibilityChecksum 0x4175e0 */
	rec->sprites = s->sprites;
	rec->row = s->sprite_row;
	memcpy(rec->parity, s->parity, sizeof rec->parity);
}

void kp_sync_reset(KpSync *s, KpGame *g, int multiplayer)
{
	int64_t t;
	memset(s, 0, sizeof *s);
	/* InitSpriteVisibilityChecksum 0x4163a0: the flingy sprites of the units that are neither buildings nor
	 * unavailable to StarEdit */
	for (t = 0; t < KP_UNIT_TYPES; t++) {
		int64_t sprite;
		if (kp_band(KP_DAT(g, units, special_ability_flags, t), BUILDING) != 0
			|| kp_band(KP_DAT(g, units, staredit_availability_flags, t), STAREDIT_AVAILABLE) == 0)
			continue;
		sprite = KP_DAT(g, flingy, sprite, KP_DAT(g, units, graphics, t));
		if (sprite >= 0 && sprite < KP_SYNC_SPRITE_IDS) s->hashed[sprite] = 1;
	}
	if (multiplayer) {
		s->schedule[0] = KP_SYNC_UNITS;
		s->schedule[1] = KP_SYNC_RESOURCES;
		s->length = 2;
	}
	kp_sync_advance(s, g);
}

void kp_sync_command(const KpSync *s, uint8_t out[KP_SYNC_SIZE])
{
	const KpSyncRecord *rec = &s->ring[s->index];
	out[0] = KP_SYNC_ID;
	out[1] = (uint8_t)((s->index << 4) | s->schedule[s->pos]);
	out[2] = (uint8_t)rec->hash;
	out[3] = (uint8_t)(rec->hash >> 8);
	out[4] = rec->sprites;
	out[5] = (uint8_t)rec->row;
	out[6] = rec->minimap_count;
}

/* The byte of the 14 rows from `first`: h = rol(h ^ row, 1), folded to 8 bits. */
static uint8_t rows_byte(const uint8_t rows[KP_SYNC_ROWS], uint32_t first, uint32_t mask)
{
	/* unsigned as the original's: no row for a first row past 255 */
	uint32_t h = 0, last = first + (KP_SYNC_VIEW_ROWS - 1), r;
	if (last >= KP_SYNC_ROWS) last = KP_SYNC_ROWS - 1;
	for (r = first; r <= last; r++) h = rol(h ^ (rows[r] & mask), 1);
	h ^= h >> 16;
	return (uint8_t)(h ^ (h >> 8));
}

int kp_sync_check(const KpSync *s, const uint8_t *cmd, int64_t player)
{
	const KpSyncRecord *rec = &s->ring[cmd[1] >> 4];
	uint32_t mask;
	if (player < 0 || player >= 32) return 0;
	mask = 1u << player;
	/* CheckSyncVisibilityHash 0x47a570 */
	if (((rec->minimap_xor >> player) & 1) != (cmd[6] & 1u)) return 0;
	if (rows_byte(rec->parity, cmd[5], mask) != cmd[4]) return 0;
	return (uint16_t)(cmd[2] | cmd[3] << 8) == rec->hash && (cmd[1] & 15) == rec->kind;
}

/* DrawMinimapUnits 0x46d510's filters (DrawLocalMinimapUnits 0x46e490 and its two siblings) */
static int minimap_unit(KpGame *g, int64_t n)
{
	int64_t t = g->u.type[n];
	return kp_band(KP_DAT(g, units, special_ability_flags, t), SUBUNIT) == 0 && t != DISRUPTION_WEB && t != DARK_SWARM
		&& (t < FIRST_DOOR || t > LAST_DOOR);
}

void kp_sync_capture_minimap(KpSync *s, KpGame *g)
{
	uint32_t parity = 0, count = 0, vision = (uint32_t)g->local_vision;
	int p;
	for (p = 0; p < KP_PLAYERS; p++) {
		int64_t n;
		for (n = g->player_units[p]; n != 0; n = g->u.player_next[n]) {
			uint32_t mask;
			if (!minimap_unit(g, n)) continue;
			mask = (uint32_t)g->w->spr.visibility[kp_unit_sprite(g, n)] & 0xff;
			if (kp_band(g->u.status[n], CLOAKED) != 0) mask &= (uint32_t)g->u.visibility[n];
			parity ^= mask;
			if ((mask & vision) != 0) count++;
		}
	}
	/* CaptureMinimapDrawStats 0x47a2f0, then RequestSpriteVisibilityChecksum 0x417610 */
	s->minimap_xor = (uint8_t)parity;
	s->minimap_count = (uint8_t)count;
	s->requested = 1;
}

uint32_t kp_sync_view_row(int64_t screen_tile_y)
{
	return screen_tile_y > KP_SYNC_VIEW_ABOVE ? (uint32_t)(screen_tile_y - KP_SYNC_VIEW_ABOVE) : 0;
}

static int hashed_sprite(const KpSync *s, const KpImages *w, int32_t sp)
{
	int32_t id = w->spr.id[sp];
	return id >= 0 && id < KP_SYNC_SPRITE_IDS && s->hashed[id];
}

void kp_sync_capture_sprites(KpSync *s, KpGame *g, uint32_t first_row)
{
	const KpImages *w = g->w;
	uint8_t local[KP_SYNC_ROWS];
	uint32_t vision = (uint32_t)g->local_vision & 0xff, r;
	if (!s->requested) return;
	s->requested = 0;
	memset(s->parity, 0, sizeof s->parity);
	memset(local, 0, sizeof local);
	for (r = 0; r < KP_SYNC_ROWS; r++) {
		int32_t sp, guard;
		for (sp = w->rows[r].head, guard = 0; sp >= 0 && sp < KP_SPRITES && guard < KP_SPRITES; sp = w->spr.next[sp], guard++) {
			uint32_t visibility;
			if (!hashed_sprite(s, w, sp)) continue;
			visibility = (uint32_t)w->spr.visibility[sp] & 0xff;
			/* ComputeSpriteVisibilityChecksum 0x417540's parities over every row */
			s->parity[r] ^= (uint8_t)visibility;
			/* BuildSpriteDrawList 0x416a00 draws the rows from the view's first one to the map's last
			 * (PrepareSpriteDrawRows 0x445de0); PushDrawHeap 0x47eec0 marks a drawn hashed sprite's row */
			if (r >= first_row && (visibility & (uint32_t)g->local_vision) != 0)
				local[kp_images_sprite_row(w, sp)] ^= (uint8_t)vision;
		}
	}
	s->sprites = rows_byte(local, first_row, 0xff);
	s->sprite_row = first_row;
}
