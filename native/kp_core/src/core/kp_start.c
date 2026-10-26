/* The start of a native game (kp_start.h): the parts of StartGame
 * 0x443f40 / InitGameWorld 0x443790 that build the simulation state, in the original's order, up to the first
 * object step. */
#include "kp_start.h"

#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kp_game_internal.h"

#define CHK_HUMAN 6
#define CHK_COMPUTER 5
#define FORCES 4
#define FORCE_ALLIED 0x2
#define FORCE_ALLIED_VICTORY 0x4
#define FORCE_VISION 0x8
#define ALLIED 1
#define ALLIED_COMPUTERS 2
#define THG2_RECORD 10
#define THG2_SPRITE 0x1000
#define THG2_DISABLED 0x8000
#define EDGE_CLEAR 0x20410000
#define EDGE_SET 0x800000
#define ADDON 0x2 /* units.dat flag */
/* the tileset's CV5 records and the tile flags */
#define CV5_RECORD 52
#define CV5_FLAG_MASK 0xd8fa
#define FLAG_TILE_HIGH_BIT 0x00400000
#define INITIAL_LOW_WORD 0xffff
/* the mission's UNIT records */
#define UNIT_RECORD 36

static int64_t u8_at(const KpBytes *b, size_t at) { return b->data[at]; }
static int64_t u16_at(const KpBytes *b, size_t at) { return (int64_t)kp_le_u16(b->data + at); }
static int64_t u32_at(const KpBytes *b, size_t at) { return (int64_t)kp_le_u32(b->data + at); }

/* The players: the CHK owners become runtime controllers (human 2, computer 1, others unchanged); the
 * local player is the human slot. */
static int64_t players(KpGame *g, const KpMission *m)
{
	KpPlayers *pl = &g->players;
	int64_t local_player = -1;
	int p;
	for (p = 0; p < KP_PLAYERS; p++) {
		int64_t owner = m->owners[p], controller = owner, force = 0;
		if (owner == CHK_HUMAN) {
			controller = KP_CTRL_HUMAN;
			if (local_player < 0) local_player = p;
		} else if (owner == CHK_COMPUTER) {
			controller = KP_CTRL_COMPUTER;
		}
		if (p >= 8) controller = 0;
		/* The player record's force byte is the FORC entry plus one (LoadForceChunk 0x443320) for the slots in the
		 * game; empty slots keep force 0. */
		if (p < 8 && controller != 0) force = (m->force_player[p] + 1) % 256;
		pl->controller[p] = controller;
		pl->race[p] = m->races[p];
		pl->force[p] = force;
	}
	for (p = 0; p < 4; p++) pl->force_name[p] = m->force_name[p];
	return local_player < 0 ? 0 : local_player;
}

/* InitAlliancesAndVision 0x477610 for a single-player game with the map's force settings. */
static void init_alliances(KpGame *g, const KpMission *m)
{
	KpPlayers *pl = &g->players;
	int p, q, f;
	memset(pl->alliance, 0, sizeof pl->alliance);
	for (p = 0; p <= 7; p++) pl->alliance[p][p] = ALLIED;
	for (p = 11; p >= 0; p--) {
		pl->alliance[11][p] = pl->alliance[p][11] = ALLIED;
		for (q = 7; q >= 0; q--) {
			if (q != p && pl->controller[p] == KP_CTRL_COMPUTER && pl->controller[q] == KP_CTRL_COMPUTER) {
				pl->alliance[p][q] = pl->alliance[q][p] = ALLIED_COMPUTERS;
			}
		}
	}
	for (p = 0; p <= 11; p++) {
		int64_t c = pl->controller[p];
		pl->shared_vision[p] = (int64_t)1 << p;
		if (c == KP_CTRL_RESCUE || c == KP_CTRL_NEUTRAL) {
			for (q = 0; q <= 11; q++) pl->alliance[p][q] = pl->alliance[q][p] = ALLIED;
		}
	}
	for (f = 1; f <= FORCES; f++) {
		int64_t flags = m->force_flags[f - 1], allied = 0, members[8];
		int count = 0, a, b;
		if ((flags & FORCE_ALLIED) != 0) allied = (flags & FORCE_ALLIED_VICTORY) != 0 ? 2 : 1;
		for (p = 0; p <= 7; p++) {
			if (pl->force[p] == f && pl->controller[p] == KP_CTRL_HUMAN) members[count++] = p;
		}
		for (a = 0; a < count; a++) {
			for (b = 0; b < count; b++) {
				if (allied != 0 || members[a] != members[b]) pl->alliance[members[a]][members[b]] = allied;
			}
		}
		for (a = 0; a < count; a++) {
			for (b = 0; b < count; b++) {
				int64_t pa = members[a], qb = members[b];
				if (pa == qb) continue;
				if ((flags & FORCE_VISION) != 0) pl->shared_vision[pa] = kp_bor(pl->shared_vision[pa], (int64_t)1 << qb);
				else pl->shared_vision[pa] = kp_band(pl->shared_vision[pa], kp_bnot((int64_t)1 << qb));
			}
		}
	}
}

/* The tileset container: CV5, VF4 and the megatile flags (MFLG). */
static int load_tileset(KpGame *g, const uint8_t *data, size_t size, KpError *err)
{
	KpKpf c;
	KpKpfSectionExpect sections[1];
	KpKpfExpect want;
	const KpKpfSection *cv5, *vf4, *mflg;
	int64_t *mflags, count, i;
	int r;
	if (kp_kpf_parse(&c, data, size, "tileset", 0, err) != 0) return -1;
	memset(sections, 0, sizeof sections);
	sections[0].fourcc = "MFLG";
	sections[0].algorithm_id = "MFL1";
	sections[0].algorithm_version = 1;
	memset(&want, 0, sizeof want);
	want.kind = "TSET";
	want.schema_version = 1;
	want.sections = sections;
	want.section_count = 1;
	if (kp_kpf_check(&c, &want, err) != 0 || !(cv5 = kp_kpf_require(&c, "CV5 ", err)) || !(vf4 = kp_kpf_require(&c, "VF4 ", err))
		|| !(mflg = kp_kpf_require(&c, "MFLG", err))) {
		kp_kpf_free(&c);
		return -1;
	}
	count = mflg->size / 4;
	mflags = (int64_t *)malloc((size_t)(count > 0 ? count : 1) * sizeof *mflags);
	if (!mflags) {
		kp_kpf_free(&c);
		snprintf(err->msg, sizeof err->msg, "start: out of memory");
		return -1;
	}
	for (i = 0; i < count; i++) mflags[i] = (int64_t)kp_le_u32(mflg->data + 4 * i);
	r = kp_terrain_set_tileset(g, cv5->data, cv5->size, vf4->data, vf4->size, mflags, count);
	free(mflags);
	kp_kpf_free(&c);
	if (r != 0) snprintf(err->msg, sizeof err->msg, "start: out of memory");
	return r;
}

/* ImportMapTiles 0x445720 into the game's tile flags and megatiles, then the edge strips
 * of LoadTileMapSection 0x469170. */
static void load_tiles(KpGame *g, const KpMission *m)
{
	KpTerrain *t = &g->terrain;
	int64_t i, count = (int64_t)m->width * m->height, groups = (int64_t)(t->cv5_size / CV5_RECORD);
	int64_t w = m->width, h = m->height;
	if (count > KP_TILES || m->tiles.size < 2 * (size_t)count) kp_game_fail(g, "start: MTXM of %lld tiles", (long long)count);
	for (i = 0; i < count; i++) {
		int64_t raw = u16_at(&m->tiles, (size_t)(2 * i)), group = kp_band(kp_shrv(raw, 4), 0x7ff), base, mega, f, mf;
		if (group >= groups) kp_game_fail(g, "start: tile %lld selects a missing CV5 group", (long long)i);
		base = group * CV5_RECORD;
		mega = (int64_t)kp_le_u16(t->cv5 + base + 20 + kp_band(raw, 15) * 2);
		if (kp_band(mega, 0x7fff) >= t->mflags_count) kp_game_fail(g, "start: megatile %lld has no flags", (long long)mega);
		mf = t->mflags[kp_band(mega, 0x7fff)];
		f = kp_bor(kp_bor(INITIAL_LOW_WORD, kp_shlv(kp_band((int64_t)kp_le_u16(t->cv5 + base + 2), CV5_FLAG_MASK), 16)), mf);
		if (raw >= 0x8000) {
			mega = kp_bor(mega, 0x8000);
			f = kp_bor(f, FLAG_TILE_HIGH_BIT);
		}
		t->megatiles[i] = mega;
		t->raw_tiles[i] = raw;
		/* import() starts from InitFogOfWar's low word; the high word replaces the tile's. */
		t->tiles[i] = kp_bor(kp_band(t->tiles[i], 0xffff), kp_band(f, 0xffff0000));
	}
	kp_tiles_clear(g, 0, h - 2, 5, 2, EDGE_CLEAR);
	kp_tiles_or(g, 0, h - 2, 5, 2, EDGE_SET);
	kp_tiles_clear(g, w - 5, h - 2, 5, 2, EDGE_CLEAR);
	kp_tiles_or(g, w - 5, h - 2, 5, 2, EDGE_SET);
	kp_tiles_clear(g, 0, h - 1, w, 1, EDGE_CLEAR);
	kp_tiles_or(g, 0, h - 1, w, 1, EDGE_SET);
}

/* LoadFogMaskSection 0x469260: low word = mask byte in both bytes. */
static void load_mask(KpGame *g, const KpBytes *mask)
{
	size_t i;
	if (mask->size > KP_TILES) kp_game_fail(g, "start: MASK of %u bytes", (unsigned)mask->size);
	for (i = 0; i < mask->size; i++) {
		g->terrain.tiles[i] = kp_bor(kp_band(g->terrain.tiles[i], 0xffff0000), u8_at(mask, i) * 257);
	}
}

/* LoadDoodadSection 0x468ba0: sprites become lone thingies, units are created (UMS or neutral); the disabled
 * flag applies 0x428f70. */
static void load_doodads(KpGame *g, const KpBytes *s)
{
	size_t at;
	for (at = 0; at + THG2_RECORD <= s->size; at += THG2_RECORD) {
		int64_t id = u16_at(s, at), x = u16_at(s, at + 2), y = u16_at(s, at + 4), owner = u8_at(s, at + 6);
		int64_t flags = u16_at(s, at + 8);
		if ((flags & THG2_SPRITE) != 0) {
			kp_lone_create(g->w, (int32_t)id, (int32_t)x, (int32_t)y, (int32_t)owner);
		} else {
			int64_t n;
			if (id >= KP_TYPE_UPPER_LEVEL_DOOR && id <= KP_TYPE_RIGHT_PIT_DOOR) owner = KP_PLAYER_NEUTRAL;
			n = kp_create_map_unit(g, id, x, y, owner);
			if (n != 0 && (flags & THG2_DISABLED) != 0) kp_unit_disable_doodad(g, n);
		}
	}
}

/* NormalizeAcquisitionRanges 0x425610: a type's acquisition range covers its weapons' maximum ranges (in tiles,
 * >> 5), weapons of its subunit for turrets. */
static void normalize_acquisition(KpGame *g, KpDat *dat)
{
	const KpDatColumn *range = g->col[KP_COL_units_target_acquisition_range];
	int64_t out[KP_UNIT_TYPES];
	int t;
	for (t = 0; t < KP_UNIT_TYPES; t++) {
		int64_t sub = KP_DAT(g, units, subunit1, t), w = sub == KP_TYPE_NONE ? t : sub,
			r = KP_DAT(g, units, target_acquisition_range, w);
		int64_t weapons[2];
		int k;
		weapons[0] = KP_DAT(g, units, ground_weapon, w);
		weapons[1] = KP_DAT(g, units, air_weapon, w);
		for (k = 0; k < 2; k++) {
			if (weapons[k] < KP_WEAPON_NONE) {
				int64_t max = kp_shrv(KP_DAT(g, weapons, maximum_range, weapons[k]), 5);
				if (r < max) r = max;
			}
		}
		out[t] = kp_mod(r, 256);
	}
	for (t = 0; t < KP_UNIT_TYPES; t++) kp_dat_set(dat, range, (uint32_t)t, (uint32_t)out[t]);
}

static void set_column(KpGame *g, KpDat *dat, const char *table, const char *column, const int64_t *values, int count)
{
	const KpDatColumn *c = kp_dat_column(dat, table, column);
	int k;
	if (!c) kp_game_fail(g, "start: no DAT column %s.%s", table, column);
	for (k = 0; k < count; k++) {
		if (values[k] != KP_RULE_UNSET && !kp_dat_set(dat, c, (uint32_t)k, (uint32_t)values[k])) {
			kp_game_fail(g, "start: %s.%s has no entry %d", table, column, k);
		}
	}
}

/* The players' units, techs and upgrades before the map's sections (LoadMapDatDefaults 0x469ed0, after the DAT load):
 * every unit and tech available, no tech researched, the upgrades at level 0 with upgrades.dat's maxima. */
static void init_techs_upgrades(KpGame *g)
{
	int p, id;
	for (p = 0; p < KP_PLAYERS; p++) {
		for (id = 0; id < KP_UNIT_TYPES; id++) g->players.unit_available[p][id] = 1;
		for (id = 0; id < KP_TECHS; id++) {
			g->players.tech_available[p][id] = 1;
			g->players.tech_researched[p][id] = 0;
		}
		for (id = 0; id < KP_UPGRADES; id++) {
			g->players.upgrade_max[p][id] = KP_DAT(g, upgrades, max_repeats, id);
			g->players.upgrades[p][id] = 0;
		}
	}
}

/* The UMS override effects: the effective DAT columns and the players' starting upgrades and techs. */
static void apply_rules(KpGame *g, KpDat *dat, const KpRuleEffects *e)
{
	static const char *const UNIT_COLUMNS[KP_RULE_UNIT_FIELDS] = { "hit_points", "shield_amount", "armor", "build_time",
		"mineral_cost", "vespene_cost", "unit_map_string" };
	/* LoadUpgradeSettingsExSection 0x468250 and LoadTechSettingsExSection 0x468430 (and their base twins) write the
	 * costs, times and energy of the map's custom upgrades and techs into the DAT arrays */
	static const char *const UPGRADE_COLUMNS[KP_RULE_UPGRADE_FIELDS] = { "mineral_cost_base", "mineral_cost_factor",
		"vespene_cost_base", "vespene_cost_factor", "research_time_base", "research_time_factor" };
	static const char *const TECH_COLUMNS[KP_RULE_TECH_FIELDS] = { "mineral_cost", "vespene_cost", "research_time",
		"energy_required" };
	int f, p, id;
	for (f = 0; f < KP_RULE_UNIT_FIELDS; f++) set_column(g, dat, "units", UNIT_COLUMNS[f], e->units[f], KP_RULE_UNITS);
	set_column(g, dat, "weapons", "damage_amount", e->weapon_damage, KP_RULE_WEAPONS);
	set_column(g, dat, "weapons", "damage_bonus", e->weapon_bonus, KP_RULE_WEAPONS);
	for (f = 0; f < KP_RULE_UPGRADE_FIELDS; f++) set_column(g, dat, "upgrades", UPGRADE_COLUMNS[f], e->upgrades[f], KP_RULE_UPGRADES);
	for (f = 0; f < KP_RULE_TECH_FIELDS; f++) set_column(g, dat, "techdata", TECH_COLUMNS[f], e->techs[f], KP_RULE_TECHS);
	for (p = 0; p < KP_RULE_PLAYERS; p++) {
		for (id = 0; id < KP_RULE_UNITS; id++) {
			if (e->availability[p][id] != KP_RULE_UNSET) g->players.unit_available[p][id] = e->availability[p][id];
		}
		for (id = 0; id < KP_RULE_UPGRADES; id++) {
			if (e->upgrade_levels[0][p][id] != KP_RULE_UNSET) g->players.upgrade_max[p][id] = e->upgrade_levels[0][p][id];
			if (e->upgrade_levels[1][p][id] != KP_RULE_UNSET) g->players.upgrades[p][id] = e->upgrade_levels[1][p][id];
		}
		for (id = 0; id < KP_RULE_TECHS; id++) {
			if (e->tech_states[0][p][id] != KP_RULE_UNSET) g->players.tech_available[p][id] = e->tech_states[0][p][id];
			if (e->tech_states[1][p][id] != KP_RULE_UNSET) g->players.tech_researched[p][id] = e->tech_states[1][p][id];
		}
	}
}

typedef struct {
	int64_t serial, related, unit;
} Relation;

/* The relation records of LoadUnitSection (0x4693d0 Nydus, 0x469360 Terran buildings): resolved newest first,
 * each with the nearest older record whose related serial is its serial (0x4698e0, 0x469810); a matched older
 * record leaves the list (0x46a320). pair(a, b) with kind 0 (Nydus) or 1 (add-ons). */
static void resolve_relations(KpGame *g, Relation *nodes, int count, int kind)
{
	while (count > 0) {
		Relation node = nodes[--count];
		int k;
		for (k = count - 1; k >= 0; k--) {
			if (nodes[k].related == node.serial) {
				Relation other = nodes[k];
				memmove(&nodes[k], &nodes[k + 1], (size_t)(count - k - 1) * sizeof *nodes);
				count--;
				if (kind == 0) {
					kp_set_union_unit(g, node.unit, KP_UNION_PEER, other.unit);
					kp_set_union_unit(g, other.unit, KP_UNION_PEER, node.unit);
				} else {
					int64_t building = node.unit, addon = other.unit;
					if ((KP_DAT(g, units, special_ability_flags, g->u.type[building]) & ADDON) != 0) {
						building = other.unit;
						addon = node.unit;
					}
					if (g->u.type[building] == kp_required_parent_type(g, g->u.type[addon])) kp_attach_addon(g, building, addon);
				}
				break;
			}
		}
	}
}

/* LoadUnitSection 0x468f30 (the melee filter and the user-select gate pass every record of a
 * campaign game: melee 0, no user-select slots). */
static void load_units(KpGame *g, const KpBytes *s, Relation *nydus, Relation *addons)
{
	size_t count = s->size / UNIT_RECORD, i;
	int nydus_count = 0, addon_count = 0;
	int64_t n;
	for (i = 0; i < count; i++) {
		size_t a = i * UNIT_RECORD;
		int64_t type = u16_at(s, a + 8), owner = u8_at(s, a + 16), controller;
		if (type == KP_TYPE_START_LOCATION) { /* a start location creates no unit; its owner's position (0x46a280) */
			if (owner >= KP_PLAYERS) kp_game_fail(g, "start: a start location of player %lld", (long long)owner);
			g->players.start_x[owner] = u16_at(s, a + 4);
			g->players.start_y[owner] = u16_at(s, a + 6);
			continue;
		}
		controller = owner <= 7 ? g->players.controller[owner] : 0;
		if (owner > 7 || (controller != 0 && (controller < 5 || controller == KP_CTRL_NEUTRAL))) {
			n = kp_create_map_unit(g, type, u16_at(s, a + 4), u16_at(s, a + 6), owner);
			if (n != 0) {
				KpUnitRecord r;
				int64_t serial = u32_at(s, a), related = u32_at(s, a + 32);
				r.valid_states = u16_at(s, a + 12);
				r.valid_properties = u16_at(s, a + 14);
				r.hp_percent = u8_at(s, a + 17);
				r.shield_percent = u8_at(s, a + 18);
				r.energy_percent = u8_at(s, a + 19);
				r.resources = u32_at(s, a + 20);
				r.hangar = u16_at(s, a + 24);
				r.states = u16_at(s, a + 26);
				kp_apply_unit_record(g, n, &r);
				if (related != 0 && type == KP_TYPE_NYDUS_CANAL) {
					nydus[nydus_count].serial = serial;
					nydus[nydus_count].related = related;
					nydus[nydus_count++].unit = n;
				}
				if (related != 0 && kp_unit_race(g, n) == KP_RACE_TERRAN && (g->u.status[n] & 2) != 0) {
					addons[addon_count].serial = serial;
					addons[addon_count].related = related;
					addons[addon_count++].unit = n;
				}
			}
		}
	}
	resolve_relations(g, nydus, nydus_count, 0);
	resolve_relations(g, addons, addon_count, 1);
	n = g->lists[KP_LIST_ACTIVE].head;
	while (n != 0) {
		kp_unit_update_sprite_visibility(g, n);
		n = g->u.next[n];
	}
	kp_scalar_set(g, KP_POWER_DIRTY, 1);
}

/* The steps of the start after kp_game_new, under the game's failure handler. */
static void start_steps(KpGame *g, const KpStartInput *in, const KpRuleEffects *effects, Relation *nydus, Relation *addons)
{
	const KpMission *m = in->mission;
	init_alliances(g, m);
	kp_selection_init(g);
	kp_step_init(g);
	kp_scalar_set(g, KP_TURN, in->turn);
	kp_triggers_init(g, m, in->carry);
	kp_ai_init(g);
	/* SetupVictoryConditions 0x448530: a campaign (not melee) game marks every player's AI as campaign AI. */
	kp_ai_regions_init(g, 1);
	if (in->carry) memcpy(g->ai_reach_source, in->carry->ai_reach_source, sizeof g->ai_reach_source);
	kp_guard_init(g);
	kp_fog_init(g);
	kp_creep_init(g);
	kp_paths_init(g);
	kp_regions_init(g);
	kp_local_geometry_init(g);
	kp_move_context_init(g);
	kp_images_init_pools(g->w);
	kp_images_init_thingies(g->w);
	kp_power_init(g);
	kp_orders_init(g);
	kp_counts_init(g);
	normalize_acquisition(g, in->dat);
	kp_units_init(g);
	kp_bullets_init(g);
	kp_strength_init(g);
	load_tiles(g, m);
	load_doodads(g, &m->sprites);
	load_mask(g, &m->mask);
	init_techs_upgrades(g);
	apply_rules(g, in->dat, effects);
	if (!in->no_units) {
		load_units(g, &m->units, nydus, addons);
	} else {
		KpBytes none;
		none.data = m->units.data;
		none.size = 0;
		load_units(g, &none, nydus, addons);
	}
	kp_ai_areas_generate(g); /* FinishAiMapSetup 0x48dd80 (its InitAiRegions 0x4b0640 mode 1 copies, no state) */
}

/* The mission's contours from its contour records (kp_mission.h). */
static int load_contours(KpGame *g, const KpMission *m)
{
	int d;
	uint32_t k;
	for (d = 0; d < 4; d++) {
		const KpMissionContours *c = &m->contours;
		KpContourSide *side;
		if (kp_game_set_contour_side(g, d, (int32_t)c->counts[d]) != 0) return -1;
		side = &g->contours[d];
		for (k = 0; k < c->counts[d]; k++) {
			const uint8_t *r = c->data.data + c->at[d] + 8 * k;
			side->axis[k] = (int16_t)kp_le_u16(r);
			side->start[k] = (int16_t)kp_le_u16(r + 2);
			side->stop[k] = (int16_t)kp_le_u16(r + 4);
			side->dir[k] = r[6];
			side->info[k] = r[7];
		}
	}
	return 0;
}

KpGame *kp_start(const KpStartInput *in, KpError *err)
{
	const KpMission *m = in->mission;
	KpRuleEffects *effects = NULL;
	Relation *nydus = NULL, *addons = NULL;
	KpImages *volatile w = NULL; /* set before the setjmp below, read after a failure */
	KpGame *volatile g = NULL;
	KpTry t;
	int64_t local_player;
	size_t records = m->units.size / UNIT_RECORD + 1;
	effects = (KpRuleEffects *)malloc(sizeof *effects);
	nydus = (Relation *)malloc(records * sizeof *nydus);
	addons = (Relation *)malloc(records * sizeof *addons);
	if (!effects || !nydus || !addons) {
		snprintf(err->msg, sizeof err->msg, "start: out of memory");
		goto failed;
	}
	/* game.new: the images world, then the game over it. */
	w = kp_images_new(in->dat, in->grp, in->script, in->rng, err);
	if (!w) goto failed;
	g = kp_game_new(w, in->rng);
	if (!g) {
		snprintf(err->msg, sizeof err->msg, "start: out of memory");
		goto failed;
	}
	g->own_world = w;
	g->crt = in->crt;
	kp_rng_portrait_init(&g->portrait);
	g->ai_bins = g->ai_game_bins = in->ai_bins;
	if (kp_game_use_dat(g, in->dat, err) != 0) goto failed;
	local_player = players(g, m);
	g->map_w = m->width;
	g->map_h = m->height;
	g->map_w_px = g->map_w * 32;
	g->map_h_px = g->map_h * 32;
	g->map_rect[0] = g->map_rect[1] = 0;
	g->map_rect[2] = kp_mod(g->map_w_px, 65536);
	g->map_rect[3] = kp_mod(g->map_h_px, 65536);
	g->local_player = local_player;
	g->local_vision = (int64_t)1 << local_player;
	g->expansion = in->expansion;
	g->events.off = in->no_events;
	g->events.trace = in->trace_triggers && !in->no_events;
	kp_scalar_set(g, KP_PLACEMENT_ACTIVE, 0);
	kp_scalar_set(g, KP_CLOAKED_HEAD, 0);
	kp_scalar_set(g, KP_CHEATS, kp_band(in->cheats, KP_CHEATS_CARRIED)); /* 0x443f40 step 10 */
	kp_scalar_set(g, KP_SPEED, in->speed);
	kp_scalar_set(g, KP_MISSION_INDEX, in->mission_index);
	kp_scalar_set(g, KP_REQUIREMENT_ERROR, 0);
	w->hook = kp_game_hook;
	w->ctx = g;
	w->tile_flags = kp_game_tile_flags;
	w->tile_ctx = g;
	w->tileset = (int32_t)(m->era % 8); /* the ERA handler 0x468b00 masks with 7 (expansion on) */
	w->expansion = in->expansion;
	w->local_player = (int32_t)local_player;
	w->local_vision = (int32_t)g->local_vision;
	w->has_sound = 1;
	if (m->width < 1 || m->width > 256 || m->height < 1 || m->height > 256) {
		snprintf(err->msg, sizeof err->msg, "images: map size %ux%u outside 1..256", m->width, m->height);
		goto failed;
	}
	kp_images_set_map(w, (int32_t)m->width, (int32_t)m->height);
	if (kp_game_set_regions(g, m->regions.data.data, m->regions.data.size, &m->regions) != 0 || load_contours(g, m) != 0) {
		snprintf(err->msg, sizeof err->msg, "start: out of memory");
		goto failed;
	}
	if (load_tileset(g, in->tileset, in->tileset_size, err) != 0) goto failed;
	if (kp_rules_effects(effects, m, in->dat, in->expansion != 0, err) != 0) goto failed;
	kp_images_begin(w, &t);
	if (setjmp(t.buf)) {
		kp_images_end(w, &t);
		snprintf(err->msg, sizeof err->msg, "%s", w->error);
		goto failed;
	}
	start_steps(g, in, effects, nydus, addons);
	kp_images_end(w, &t);
	free(effects);
	free(nydus);
	free(addons);
	return g;
failed:
	free(effects);
	free(nydus);
	free(addons);
	if (g) kp_game_free(g);
	else if (w) kp_images_free(w);
	return NULL;
}

typedef char carry_reach_size[sizeof ((KpCarry *)0)->ai_reach_source == sizeof ((KpGame *)0)->ai_reach_source ? 1 : -1];

void kp_carry_take(const KpGame *g, KpCarry *c)
{
	const KpTriggers *t = &g->trig;
	memcpy(c->pending, t->pending, sizeof c->pending);
	memcpy(c->objectives, t->objectives, sizeof c->objectives);
	c->player = t->player;
	c->result_delay = t->result;
	c->lb_label = t->lb_label;
	c->count_key = t->count_key;
	c->count_location = t->count_location;
	memcpy(c->count_all, t->count_all, sizeof c->count_all);
	memcpy(c->count_completed, t->count_completed, sizeof c->count_completed);
	memcpy(c->count_pstat, t->count_pstat, sizeof c->count_pstat);
	memcpy(c->ai_reach_source, g->ai_reach_source, sizeof c->ai_reach_source);
}
