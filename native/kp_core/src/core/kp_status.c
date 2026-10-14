/* Status effects of spells (kp_game.h), ported from the atlas (P08, P13): the status overlays of a unit, Ensnare,
 * Parasite and Spawn Broodlings. */
#include "kp_game_internal.h"

#define DAT_BUILDING 0x1
#define DAT_MEDIUM_OVERLAY 0x2000000
#define DAT_LARGE_OVERLAY 0x4000000
#define STATUS_BURROWED 0x10
#define STATUS_INVINCIBLE 0x4000000
#define STATUS_HALLUCINATION 0x40000000
#define BROODLING 40
#define BROODLING_LIFE 1800
#define HALLUCINATION_LIFE 1350
#define BROOD_ATTEMPTS 2
#define BROOD_SEARCH 32
#define ENSNARE_TIME 75
#define ENSNARE_RADIUS 64
#define ENSNARE_SPRITE 364
#define ENSNARE_ELEVATION 19
#define ENSNARE_OVERLAY 384
#define PARASITE_SOUND 921

/* GetOverlaySize 0x428d90: 0 small, 1 medium, 2 large. */
int64_t kp_overlay_size(KpGame *g, int64_t n)
{
	int64_t f = KP_DAT(g, units, special_ability_flags, g->u.type[n]);
	if (kp_band(f, DAT_MEDIUM_OVERLAY) != 0) return 1;
	if (kp_band(f, DAT_LARGE_OVERLAY) != 0) return 2;
	return 0;
}

/* The sprite a unit's status overlays go on: its subunit's or its parent's when linked (+0x70). */
static int32_t overlay_sprite(KpGame *g, int64_t n)
{
	int64_t linked = g->u.subunit[n];
	return kp_unit_sprite(g, linked != 0 ? linked : n);
}

/* AddOverlayToUnitLinkedSprite 0x422ae0 (with 0x415260: before the image head). */
void kp_add_status_overlay(KpGame *g, int64_t n, int64_t id)
{
	kp_sprite_image_on_top(g->w, overlay_sprite(g, n), (int32_t)id, 0, 0, 0);
}

static int32_t image_in_range(KpImages *w, int32_t s, int64_t low, int64_t high)
{
	int32_t i;
	for (i = w->spr.images[s].head; i != KP_NONE; i = w->img.next[i]) {
		if (w->img.id[i] >= low && w->img.id[i] <= high) return i;
	}
	return KP_NONE;
}

/* RemoveImageRangeFromUnit 0x422e70: the first image of the range on the unit's sprite, else on its linked unit's. */
void kp_remove_status_overlay(KpGame *g, int64_t n, int64_t low, int64_t high)
{
	KpImages *w = g->w;
	int32_t i = image_in_range(w, kp_unit_sprite(g, n), low, high);
	if (i == KP_NONE && g->u.subunit[n] != 0) i = image_in_range(w, kp_unit_sprite(g, g->u.subunit[n]), low, high);
	if (i != KP_NONE) kp_image_release(w, i);
}

/* GetTimedLifeDuration 0x434a90. */
int64_t kp_timed_life(KpGame *g, int64_t n)
{
	if (kp_band(g->u.status[n], STATUS_HALLUCINATION) != 0) return HALLUCINATION_LIFE;
	return g->u.type[n] == BROODLING ? BROODLING_LIFE : 0;
}

/* ApplyEnsnareToUnit 0x43e0e0. */
void kp_apply_ensnare(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	if (u->ensnare_timer[n] == 0 && kp_band(u->status[n], STATUS_BURROWED) == 0) {
		kp_add_status_overlay(g, n, ENSNARE_OVERLAY + kp_overlay_size(g, n));
	}
	u->ensnare_timer[n] = ENSNARE_TIME;
	kp_recalculate_speed(g, n);
}

/* EndEnsnare 0x43dee0. */
void kp_end_ensnare(KpGame *g, int64_t n)
{
	kp_remove_status_overlay(g, n, ENSNARE_OVERLAY, ENSNARE_OVERLAY + 2);
	g->u.ensnare_timer[n] = 0;
	kp_recalculate_speed(g, n);
}

/* ApplyEnsnareArea 0x43dc90: the cloud and every unit around (x, y) but the source, buildings, invincible and
 * burrowed units (EnsnareAreaCallback 0x43df10). */
void kp_ensnare_area(KpGame *g, int64_t x, int64_t y, int64_t source)
{
	KpUnits *u = &g->u;
	KpImages *w = g->w;
	int32_t t = kp_lone_create(w, ENSNARE_SPRITE, (int32_t)x, (int32_t)y, 0);
	int64_t *found;
	int count, k;
	if (t != KP_NONE) {
		w->spr.elevation[w->thg.sprite[t]] = ENSNARE_ELEVATION;
		kp_thingy_visibility(w, t);
	}
	/* FindUnitInRadiusBox 0x442b20 */
	found = kp_query_rect(g, kp_s16v(x - ENSNARE_RADIUS), kp_s16v(y - ENSNARE_RADIUS), kp_s16v(x + ENSNARE_RADIUS),
		kp_s16v(y + ENSNARE_RADIUS), &count);
	for (k = 0; k < count; k++) {
		int64_t c = found[k];
		if (c == source || kp_band(KP_DAT(g, units, special_ability_flags, u->type[c]), DAT_BUILDING) != 0
			|| kp_band(u->status[c], STATUS_INVINCIBLE | STATUS_BURROWED) != 0) {
			continue;
		}
		kp_apply_ensnare(g, c);
	}
	kp_query_release(g, found);
}

/* The Parasite effect of a bullet (0x4090e0 type 6): player `owner` sees through target. */
void kp_apply_parasite(KpGame *g, int64_t target, int64_t owner, int64_t source)
{
	kp_play_unit_sound(g, PARASITE_SOUND, target);
	g->u.parasites[target] = kp_bor(g->u.parasites[target], (int64_t)1 << (owner & 7));
	if (source != 0) kp_ai_react_to_attack(g, target, source, 0, 0);
}

/* SpawnBroodlings 0x43e120: the target dies and two Broodlings of the Queen's owner take its place. */
void kp_spawn_broodlings(KpGame *g, int64_t queen, int64_t target)
{
	KpUnits *u = &g->u;
	int64_t owner = u->owner[queen];
	if (kp_band(u->status[target], STATUS_HALLUCINATION) == 0) {
		int32_t s = kp_unit_sprite(g, target);
		int64_t x = kp_s16v(g->w->spr.x[s]) - 2, y = kp_s16v(g->w->spr.y[s]) - 2;
		int k;
		for (k = 0; k < BROOD_ATTEMPTS; k++, x += 4, y += 4) {
			int64_t px = x, py = y, b, life;
			if (!kp_unit_type_fits(g, BROODLING, x, y)) {
				int64_t box[4];
				box[0] = kp_s16v(x - BROOD_SEARCH);
				box[1] = kp_s16v(y - BROOD_SEARCH);
				box[2] = kp_s16v(x + BROOD_SEARCH);
				box[3] = kp_s16v(y + BROOD_SEARCH);
				if (!kp_find_unit_placement(g, queen, x, y, box, 0, 0, &px, &py)) continue;
				x = kp_s16v(px);
				y = kp_s16v(py);
			}
			px = x;
			py = y;
			kp_clamp_to_map(g, BROODLING, &px, &py);
			b = kp_unit_create(g, BROODLING, px, py, owner);
			if (b == 0) continue; /* ReportUnitCreationError 0x425020: the interface's */
			kp_unit_complete_in_place(g, b);
			life = kp_timed_life(g, b);
			if (u->remove_timer[b] == 0 || kp_mod(life, 65536) < u->remove_timer[b]) u->remove_timer[b] = kp_mod(life, 65536);
			if (u->ai[queen] != 0) kp_game_fail(g, "status: a computer's Broodlings join its Queen (0x48d320)");
		}
	}
	kp_record_death(g, target, owner);
	kp_unit_kill(g, target);
	kp_increment_kills(g, queen);
}
