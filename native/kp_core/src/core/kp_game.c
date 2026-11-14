/* The game (kp_game.h): creation, failures, the DAT reads, the scalars and the events. */
#include "kp_game_internal.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

KpGame *kp_game_new(KpImages *w, KpRngGame *rng)
{
	KpGame *g = (KpGame *)calloc(1, sizeof *g);
	if (!g) return NULL;
	g->w = w;
	g->rng = rng;
	return g;
}

static void free_regions(KpGame *g)
{
	int d;
	free((void *)g->regions.data.data);
	memset(&g->regions, 0, sizeof g->regions);
	for (d = 0; d < 4; d++) {
		free(g->contours[d].axis);
		memset(&g->contours[d], 0, sizeof g->contours[d]);
	}
}

void kp_game_free(KpGame *g)
{
	if (!g) return;
	kp_pathing_free(g);
	kp_terrain_free(g);
	if (g->own_world) kp_images_free(g->own_world);
	free_regions(g);
	kp_triggers_free(g);
	kp_ai_bins_free(&g->ai_program);
	free(g);
}

int kp_game_set_regions(KpGame *g, const uint8_t *data, size_t size, const KpMissionRegions *fields)
{
	uint8_t *copy = (uint8_t *)malloc(size ? size : 1);
	if (!copy) return -1;
	memcpy(copy, data, size);
	free((void *)g->regions.data.data);
	g->regions = *fields;
	g->regions.data.data = copy;
	g->regions.data.size = (uint32_t)size;
	return 0;
}

int kp_game_set_contour_side(KpGame *g, int d, int32_t count)
{
	KpContourSide *c = &g->contours[d];
	int64_t *b = (int64_t *)calloc((size_t)(count > 0 ? count : 1) * 5, sizeof *b);
	if (!b) return -1;
	free(c->axis);
	c->count = count;
	c->axis = b;
	c->start = b + count;
	c->stop = b + count * 2;
	c->dir = b + count * 3;
	c->info = b + count * 4;
	return 0;
}

void kp_game_fail(KpGame *g, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(g->w->error, sizeof g->w->error, fmt, ap);
	va_end(ap);
	kp_images_rethrow(g->w);
}

int64_t kp_game_unit(KpGame *g, int64_t n)
{
	if (n < 1 || n > KP_UNITS) kp_game_fail(g, "unit: record %lld out of range", (long long)n);
	return n;
}

int64_t kp_game_flingy(KpGame *g, int64_t n)
{
	if (n < 1 || n > KP_FLINGIES) kp_game_fail(g, "flingy: record %lld out of range", (long long)n);
	return n;
}

int64_t kp_idiv64(KpGame *g, int64_t a, int64_t b)
{
	if (b == 0) kp_game_fail(g, "integer division by zero");
	if (a == -2147483648LL && b == -1) kp_game_fail(g, "integer overflow in division");
	return a / b;
}

int64_t kp_udiv64(KpGame *g, int64_t a, int64_t b)
{
	if (b == 0 || (uint32_t)b == 0) kp_game_fail(g, "integer division by zero");
	return (int64_t)((uint32_t)a / (uint32_t)b);
}

int64_t kp_urem64(KpGame *g, int64_t a, int64_t b)
{
	if (b == 0 || (uint32_t)b == 0) kp_game_fail(g, "integer division by zero");
	return (int64_t)((uint32_t)a % (uint32_t)b);
}

void kp_list64_unlink(int64_t *prev, int64_t *next, KpList *list, int64_t node, int64_t none)
{
	if (list->head == node) list->head = (int32_t)next[node];
	if (list->tail == node) list->tail = (int32_t)prev[node];
	if (prev[node] != none) next[prev[node]] = next[node];
	if (next[node] != none) prev[next[node]] = prev[node];
	next[node] = none;
	prev[node] = none;
}

void kp_list64_insert_after(int64_t *prev, int64_t *next, KpList *list, int64_t existing, int64_t node, int64_t none)
{
	if (list->tail == existing) list->tail = (int32_t)node;
	prev[node] = existing;
	next[node] = next[existing];
	if (next[existing] != none) prev[next[existing]] = node;
	next[existing] = node;
}

void kp_list64_insert_after_head(int64_t *prev, int64_t *next, KpList *list, int64_t node, int64_t none)
{
	if (list->head == none) {
		list->head = (int32_t)node;
		list->tail = (int32_t)node;
		return;
	}
	kp_list64_insert_after(prev, next, list, list->head, node, none);
}

void kp_list64_insert_before(int64_t *prev, int64_t *next, KpList *list, int64_t existing, int64_t node, int64_t none)
{
	if (list->head == existing) list->head = (int32_t)node;
	prev[node] = prev[existing];
	next[node] = existing;
	if (prev[existing] != none) next[prev[existing]] = node;
	prev[existing] = node;
}

const KpScalarInfo KP_SCALARS[KP_SCALAR_COUNT] = {
	{ "units_allocated", 0 }, { "error_code", 0 }, { "cloaked_head", 0 }, { "tile_countdown", 0 },
	{ "update_tiles", 1 }, { "move_reveal", 1 }, { "order_spread", 0 }, { "secondary_spread", 0 },
	{ "interceptor_alerted", 1 }, { "hit_slot", 0 }, { "frame", 0 }, { "turn", 0 }, { "elapsed", 0 },
	{ "command_player", 0 }, { "second_order", 0 }, { "order_marker", 1 },
	{ "power_dirty", 1 }, { "placement_active", 1 }, { "cheats", 0 },
	{ "requirement_error", 0 }, { "path_free_head", 0 }, { "path_subject", 0 },
	{ "path_all_units", 0 }, { "screen_x", 0 }, { "screen_y", 0 }, { "screen_w", 0 }, { "screen_h", 0 },
	{ "paused", 0 }, { "speed", 0 }, { "mission_index", 0 }, { "placement_order", 0 }, { "placement_type", 0 },
};

int64_t kp_scalar(const KpGame *g, KpScalar s) { return g->scalar_present[s] ? g->scalar[s] : KP_NIL; }

void kp_scalar_set(KpGame *g, KpScalar s, int64_t v)
{
	g->scalar[s] = v;
	g->scalar_present[s] = 1;
}

void kp_scalar_clear(KpGame *g, KpScalar s)
{
	g->scalar[s] = 0;
	g->scalar_present[s] = 0;
}

int64_t kp_cheats(KpGame *g)
{
	int64_t c = kp_scalar(g, KP_CHEATS);
	return c == KP_NIL ? 0 : c;
}

/* ChooseYesSound 0x447130 and ChooseWhatSound 0x447190 draw from the CRT generator the creep tile art uses. */
void kp_interface_crt_draw(KpGame *g)
{
	if (g->crt == NULL) kp_game_fail(g, "game: an interface CRT draw without the crt generator");
	kp_rng_crt_rand(g->crt);
}

void kp_interface_crt_seed(KpGame *g, int64_t seed)
{
	if (g->crt == NULL) kp_game_fail(g, "game: an interface CRT seed without the crt generator");
	kp_rng_crt_init(g->crt, (uint32_t)kp_u32v(seed));
}

/* GetUpgradeLevel 0x49a470 (upgrades[player][id] or 0). */
int64_t kp_upgrade_level(KpGame *g, int64_t player, int64_t id)
{
	if (player < 0 || player >= KP_PLAYERS) kp_game_fail(g, "game: no player %lld", (long long)player);
	if (id < 0 || id >= KP_UPGRADE_IDS) return 0;
	return g->players.upgrades[player][id];
}

static int64_t player_slot(KpGame *g, int64_t p)
{
	if (p < 0 || p >= KP_PLAYERS) kp_game_fail(g, "game: no player %lld", (long long)p);
	return p;
}

int64_t kp_controller(KpGame *g, int64_t p) { return g->players.controller[player_slot(g, p)]; }
int64_t kp_alliance(KpGame *g, int64_t a, int64_t b) { return g->players.alliance[player_slot(g, a)][player_slot(g, b)]; }

/* Player a's alliance towards b (the table 0x534914: what a trigger's Set Alliance Status writes). */
void kp_set_alliance(KpGame *g, int64_t a, int64_t b, int64_t v) { g->players.alliance[player_slot(g, a)][player_slot(g, b)] = v; }
int64_t kp_shared_vision(KpGame *g, int64_t p) { return g->players.shared_vision[player_slot(g, p)]; }

/* IsMorphingIntoAdvancedZergBuilding 0x43c040. */
int kp_is_morphing_zerg_building(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t q;
	if (kp_band(u->status[n], 1) != 0) return 0;
	q = *kp_queue_entry(g, n, u->queue_slot[n]);
	return q == KP_TYPE_HIVE || q == KP_TYPE_LAIR || q == KP_TYPE_GREATER_SPIRE || q == KP_TYPE_SPORE_COLONY
		|| q == KP_TYPE_SUNKEN_COLONY;
}

/* ApplyCreationSpeedUpgrades 0x43a8e0. */
static int64_t speed_upgrade(int64_t t)
{
	switch (t) {
	case KP_TYPE_VULTURE: case KP_TYPE_JIM_RAYNOR_VULTURE: return KP_UPG_ION_THRUSTERS;
	case KP_TYPE_HYDRALISK: return KP_UPG_MUSCULAR_AUGMENTS;
	case KP_TYPE_ULTRALISK: return KP_UPG_ANABOLIC_SYNTHESIS;
	case KP_TYPE_OVERLORD: return KP_UPG_PNEUMATIZED_CARAPACE;
	case KP_TYPE_ZEALOT: return KP_UPG_LEG_ENHANCEMENTS;
	case KP_TYPE_SHUTTLE: return KP_UPG_GRAVITIC_DRIVE;
	case KP_TYPE_SCOUT: return KP_UPG_GRAVITIC_THRUSTERS;
	case KP_TYPE_OBSERVER: return KP_UPG_GRAVITIC_BOOSTERS;
	}
	return -1;
}

static int always_fast(int64_t t)
{
	return t == KP_TYPE_HUNTER_KILLER || t == KP_TYPE_YGGDRASILL || t == KP_TYPE_FENIX_ZEALOT || t == KP_TYPE_MOJO
		|| t == KP_TYPE_ARTANIS || t == KP_TYPE_LURKER;
}

void kp_apply_creation_upgrades(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t t = u->type[n], p = u->owner[n], up = speed_upgrade(t);
	if (!(up >= 0 || always_fast(t) || t == KP_TYPE_ZERGLING || t == KP_TYPE_DEVOURING_ONE
		|| t == KP_TYPE_TORRASQUE)) return;
	if (up >= 0) {
		if (kp_upgrade_level(g, p, up) != 0) kp_set_speed_upgrade(g, n);
	} else if (t == KP_TYPE_ZERGLING) { /* Zergling: Metabolic Boost, then Adrenal Glands */
		if (kp_upgrade_level(g, p, KP_UPG_METABOLIC_BOOST) != 0) kp_set_speed_upgrade(g, n);
		if (kp_upgrade_level(g, p, KP_UPG_ADRENAL_GLANDS) != 0) u->status[n] = kp_bor(u->status[n], 0x20000000);
	} else if (t == KP_TYPE_DEVOURING_ONE) {
		kp_set_speed_upgrade(g, n);
		u->status[n] = kp_bor(u->status[n], 0x20000000);
	} else if (t == KP_TYPE_TORRASQUE) {
		if (g->expansion != 0) kp_set_speed_upgrade(g, n);
	} else if (always_fast(t)) {
		kp_set_speed_upgrade(g, n);
	}
	kp_recalculate_speed(g, n);
}

static const char *const DAT_TABLES[KP_COL_COUNT] = {
#define KP_DAT_TABLE(t, c) #t,
	KP_DAT_COLUMNS(KP_DAT_TABLE)
#undef KP_DAT_TABLE
};
static const char *const DAT_NAMES[KP_COL_COUNT] = {
#define KP_DAT_NAME(t, c) #c,
	KP_DAT_COLUMNS(KP_DAT_NAME)
#undef KP_DAT_NAME
};

int kp_game_use_dat(KpGame *g, const KpDat *dat, KpError *err)
{
	int k;
	for (k = 0; k < KP_COL_COUNT; k++) {
		g->col[k] = kp_dat_column(dat, DAT_TABLES[k], DAT_NAMES[k]);
		if (!g->col[k]) {
			snprintf(err->msg, sizeof err->msg, "game: no DAT column %s.%s", DAT_TABLES[k], DAT_NAMES[k]);
			return -1;
		}
	}
	g->cdat = dat;
	return 0;
}

int64_t kp_dat_read(KpGame *g, int col, int64_t i)
{
	const KpDatColumn *c = g->col[col];
	if (!c) kp_game_fail(g, "game: no DAT");
	/* A missing value is KP_NIL, which compares unequal to every DAT value. */
	if (i < 0 || i > (int64_t)UINT32_MAX || !kp_dat_has(c, (uint32_t)i)) return KP_NIL;
	return (int64_t)kp_dat_value(c, (uint32_t)i);
}

void kp_dat_write(KpGame *g, int col, int64_t i, int64_t v)
{
	const KpDatColumn *c = g->col[col];
	/* The game's DAT is its own (kp_game_use_dat): the original writes some values while it runs. */
	if (!c || i < 0 || i > (int64_t)UINT32_MAX || !kp_dat_set((KpDat *)g->cdat, c, (uint32_t)i, (uint32_t)v)) {
		kp_game_fail(g, "game: DAT value %lld of column %d", (long long)i, col);
	}
}

int64_t kp_dat_read_named(KpGame *g, const char *table, const char *column, int64_t i)
{
	const KpDatColumn *c;
	if (!g->cdat) kp_game_fail(g, "game: no DAT");
	c = kp_dat_column(g->cdat, table, column);
	if (!c) kp_game_fail(g, "game: no DAT column %s.%s", table, column);
	if (i < 0 || i > (int64_t)UINT32_MAX || !kp_dat_has(c, (uint32_t)i)) return KP_NIL;
	return (int64_t)kp_dat_value(c, (uint32_t)i);
}

int64_t kp_dat_requirement(KpGame *g, const char *domain, int64_t k)
{
	int d;
	if (!g->cdat) kp_game_fail(g, "game: no DAT");
	for (d = 0; d < KP_DAT_REQUIREMENTS; d++) {
		const KpDatRequirements *r = &g->cdat->requirements[d];
		if (r->name && strcmp(r->name, domain) == 0) {
			if (!r->words || k < 0 || k >= (int64_t)r->count) return KP_NIL;
			return (int64_t)r->words[2 * k] + (int64_t)r->words[2 * k + 1] * 256;
		}
	}
	kp_game_fail(g, "game: no requirement domain %s", domain);
}

void kp_event(KpGame *g, int64_t kind, int64_t a, int64_t b, int64_t c, const char *text)
{
	KpEvents *e = &g->events;
	int64_t k;
	if (e->off) return;
	if (e->count >= KP_EVENTS) kp_game_fail(g, "events: ring full; drain more often or raise the capacity");
	if (text && e->texts_waiting >= KP_EVENT_TEXTS) kp_game_fail(g, "events: %d texts waiting; drain more often", KP_EVENT_TEXTS);
	k = (e->first + e->count) % KP_EVENTS;
	e->sequence++;
	e->kind[k] = kind;
	e->frame[k] = kp_scalar(g, KP_FRAME) == KP_NIL ? 0 : kp_scalar(g, KP_FRAME);
	e->seq[k] = e->sequence;
	e->a[k] = a;
	e->b[k] = b;
	e->c[k] = c;
	e->text[k] = -1;
	if (text) {
		int32_t slot = (int32_t)(e->text_next % KP_EVENT_TEXTS);
		e->text_next++;
		snprintf(e->texts[slot], KP_EVENT_TEXT_SIZE, "%s", text);
		e->text[k] = slot;
		e->texts_waiting++;
	}
	e->count++;
}

int kp_event_take(KpGame *g, KpEvent *out)
{
	KpEvents *e = &g->events;
	int64_t i = e->first;
	if (e->count == 0) return 0;
	e->first = (i + 1) % KP_EVENTS;
	e->count--;
	out->kind = e->kind[i];
	out->frame = e->frame[i];
	out->seq = e->seq[i];
	out->a = e->a[i];
	out->b = e->b[i];
	out->c = e->c[i];
	out->text = NULL;
	if (e->text[i] >= 0) {
		out->text = e->texts[e->text[i]];
		e->texts_waiting--;
	}
	return 1;
}

#define SENT_BITS 11
#define SENT_PER_FIELD 4 /* 44 bits a field: the 12 slots in an event's three arguments */

void kp_event_selection_sent(KpGame *g)
{
	int64_t f[3] = { 0, 0, 0 };
	int k;
	for (k = 0; k < KP_SLOTS && g->local_selection[k] != 0; k++) {
		f[k / SENT_PER_FIELD] |= g->local_selection[k] << (SENT_BITS * (k % SENT_PER_FIELD));
	}
	kp_event(g, KP_EVENT_SELECTION_SENT, f[0], f[1], f[2], NULL);
}

int64_t kp_selection_sent_unit(int64_t a, int64_t b, int64_t c, int k)
{
	int64_t f = k < SENT_PER_FIELD ? a : k < 2 * SENT_PER_FIELD ? b : c;
	return (f >> (SENT_BITS * (k % SENT_PER_FIELD))) & ((1 << SENT_BITS) - 1);
}

int kp_event_peek(KpGame *g, int64_t index, KpEvent *out)
{
	KpEvents *e = &g->events;
	int64_t i;
	if (index < 0 || index >= e->count) return 0;
	i = (e->first + index) % KP_EVENTS;
	out->kind = e->kind[i];
	out->frame = e->frame[i];
	out->seq = e->seq[i];
	out->a = e->a[i];
	out->b = e->b[i];
	out->c = e->c[i];
	out->text = e->text[i] >= 0 ? e->texts[e->text[i]] : NULL;
	return 1;
}

int64_t kp_event_sequence(KpGame *g) { return g->events.sequence; }

void kp_set_local_player(KpGame *g, int64_t p)
{
	if (p < 0 || p >= KP_PLAYERS) kp_game_fail(g, "game: no player %lld", (long long)p);
	g->local_player = p;
	g->local_vision = (int64_t)1 << p;
	g->w->local_player = (int32_t)p;
	if (!g->w->replay) g->w->local_vision = (int32_t)g->local_vision;
}

void kp_replay_set_vision(KpGame *g, int64_t mask, int reveal)
{
	int64_t p, n;
	if (!g->w->replay) return;
	mask &= 0xff;
	if (reveal) mask = 0xff;
	for (p = 0; p < 8; p++) {
		if (kp_player_finished(g, p)) mask &= ~((int64_t)1 << p);
	}
	g->local_vision = mask;
	g->w->local_vision = (int32_t)mask;
	g->w->replay_reveal = reveal ? 1 : 0;
	for (n = g->lists[KP_LIST_ACTIVE].head; n != 0; n = g->u.next[n]) kp_unit_update_sprite_visibility(g, n);
}

void kp_report_error(KpGame *g, int64_t player, int64_t id)
{
	if (player == g->local_player) kp_event(g, KP_EVENT_ERROR, player, id, 0, NULL);
}

/* ShowErrorWithSound 0x4461f0: for the local player outside a replay, the advisor's sound (none for 0), then the
 * error line. */
void kp_report_error_sound(KpGame *g, int64_t player, int64_t sound, int64_t id)
{
	if (player != g->local_player || g->w->replay) return;
	kp_advisor_sound(g, sound, 0, KP_TALK_NONE, 0, 0);
	kp_event(g, KP_EVENT_ERROR, player, id, 0, NULL);
}

void kp_report_unplaced(KpGame *g, int64_t t, int64_t x, int64_t y)
{
	kp_event(g, KP_EVENT_UNIT_UNPLACED, t, x, y, NULL);
}

/* A sound event's a: the sound, the call's minVolume and enable, the talking request after it (kp_game.h). */
static int64_t sound_args(int64_t sfx, int64_t min_volume, int64_t enable, int64_t talk, int64_t camera, int64_t colour)
{
	return kp_band(sfx, 0xffff) | kp_band(min_volume, 0xff) << KP_SOUND_MIN_VOLUME_SHIFT
		| kp_band(enable, 0xff) << KP_SOUND_ENABLE_SHIFT | kp_band(talk, 3) << KP_SOUND_TALK_SHIFT
		| kp_band(camera, 1) << KP_SOUND_CAMERA_SHIFT | kp_band(colour, 0xff) << KP_SOUND_COLOUR_SHIFT;
}

int64_t kp_unit_sprite_packed(KpGame *g, int64_t n)
{
	KpImages *w = g->w;
	int32_t s = kp_unit_sprite(g, n);
	return kp_band(w->spr.x[s], 0xffff) | kp_band(w->spr.y[s], 0xffff) << 16;
}

/* PlaySoundAtPosition 0x446a20 (EDX = 1, stack [packed x | y << 16, 0] from both callers, PlayImageSound 0x40f5a0
 * and OrderCastRecall 0x42fc40): sound 0 or a point outside the map (unsigned WORDs) plays nothing; else the local
 * player hears it at a tile they see (in a replay, one the replay's vision sees, or with the replay's reveal
 * 0x67cc88). */
void kp_play_sound_at(KpGame *g, int64_t sfx, int64_t x, int64_t y)
{
	KpImages *w = g->w;
	int64_t tile;
	if (sfx == 0 || kp_mod(x, 65536) >= kp_mod(g->map_w_px, 65536) || kp_mod(y, 65536) >= kp_mod(g->map_h_px, 65536)) return;
	tile = g->terrain.tiles[kp_idiv64(g, kp_s16v(y), 32) * g->map_w + kp_idiv64(g, kp_s16v(x), 32)];
	if (w->replay) {
		if (kp_band(w->local_vision, kp_bnot(tile)) == 0 && !w->replay_reveal) return;
	} else if (kp_band(tile, g->local_vision) != 0) {
		return;
	}
	kp_event(g, KP_EVENT_SOUND, sound_args(sfx, 0, KP_SOUND_ON, KP_TALK_NONE, 0, 0), kp_s16v(x), kp_s16v(y), NULL);
}

/* PlaySoundAtUnit 0x446940 (ECX = sound, EDX = unit, stack [enable, minVolume]) with the talking request its caller
 * makes for the unit: sound 0 plays nothing; without a unit the sound has no position (KP_EVENT_SOUND_SCREEN);
 * otherwise the local player hears it when the unit is theirs (0x40fd30) or its sprite's visibility byte meets the
 * local vision (0x672668), in a replay when it meets the replay's vision (0x67cc84, which the world's local_vision
 * holds in a replay as it does for drawing), and the event carries the sprite's position. With a talking request
 * that does not wait for the sound (KP_TALK_ALWAYS), a sound 0 or not heard still has its event, with nothing to play
 * (KP_SOUND_OFF). The sound switch 0x6d3140, enable, the volume (minVolume, the distance from the screen 0x4463e0,
 * each sound's minimum volume 0x51e630, quiet mode) and the talking request belong to the interface. */
void kp_unit_sound(KpGame *g, int64_t sfx, int64_t n, int64_t enable, int64_t min_volume, int64_t talk, int64_t camera,
	int64_t colour)
{
	KpImages *w = g->w;
	int32_t s;
	int64_t seen;
	if (sfx == 0) {
		if (talk != KP_TALK_ALWAYS) return;
		enable = KP_SOUND_OFF;
	}
	if (n == 0) {
		kp_event(g, KP_EVENT_SOUND_SCREEN, sound_args(sfx, min_volume, enable, talk, camera, colour), 0, 0, NULL);
		return;
	}
	s = kp_unit_sprite(g, n);
	seen = kp_band(w->spr.visibility[s], 0xff);
	if (w->replay ? kp_band(seen, w->local_vision) == 0
		: g->u.owner[n] != g->local_player && kp_band(seen, kp_band(g->local_vision, 0xff)) == 0) {
		if (talk != KP_TALK_ALWAYS) return;
		enable = KP_SOUND_OFF;
	}
	kp_event(g, KP_EVENT_SOUND_UNIT, sound_args(sfx, min_volume, enable, talk, camera, colour), n,
		kp_unit_sprite_packed(g, n) | kp_band(g->u.type[n], 0xffff) << 32, NULL);
}

void kp_play_unit_sound(KpGame *g, int64_t sfx, int64_t n)
{
	kp_unit_sound(g, sfx, n, KP_SOUND_ON, 0, KP_TALK_NONE, 0, 0);
}

/* The advisor's playback (atlas P15: ShowErrorWithSound 0x4461f0, PlayResearchCompleteVoice 0x446b70,
 * PlayUpgradeCompleteVoice 0x447060): sound 0 plays nothing; otherwise the sound without a position at max(99, its
 * minimum volume), as PlaySoundAtUnit 0x446940 without a unit with the stack [1, 99]; then the talking request for
 * unit n. */
void kp_advisor_sound(KpGame *g, int64_t sfx, int64_t n, int64_t talk, int64_t camera, int64_t colour)
{
	int64_t enable = KP_SOUND_ON;
	if (sfx == 0) {
		if (talk != KP_TALK_ALWAYS) return;
		enable = KP_SOUND_OFF;
	}
	kp_event(g, KP_EVENT_SOUND_SCREEN, sound_args(sfx, KP_VOICE_VOLUME, enable, talk, camera, colour), n, 0, NULL);
}
