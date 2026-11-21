/* The game types' start and victory (kp_gametype.h). */
#include "kp_gametype.h"

#include <stdio.h>
#include <string.h>

#include "kp_game_internal.h"
#include "kp_template.h"

#define RNG_RANDOM_RACE 0x90  /* ResolveRandomRaces 0x478ee0, PickRandomRace 0x47a030 */
#define RNG_START_SLOT 0x21   /* RandomizeStartSlots 0x479a00 */
#define RNG_COLOR 0x1c        /* ShufflePlayerColors 0x41d460 */
#define RNG_TEAM_SLOT 0x20    /* SetupTeamGameSlots 0x4b2db0 */
#define RACE_SWAPS 16
#define START_WORKERS 4
#define BEACON_BASE 197       /* a Capture the Flag beacon: 197 + the slot's race */
#define BEACON_SIZE 0x40      /* its site search's window (0x448210) */
#define DAT_WORKER 0x8        /* the units.dat flag 0x448210 looks for */
#define DAT_CREEP 0x20000     /* requires creep */
#define GROUP_NEUTRAL 0x80    /* units.dat's group flags 0x523c40 */
#define FORCE_RANDOM_START 0x1
#define FORCES 4
#define TRIGGER_RECORD 2400

/* the AI scripts StartCustomLevelAIScripts 0x4486a0 starts by race: Brood War's, then the original's */
static const uint32_t MELEE_SCRIPTS[2][3] = {
	{ 0x75434d5a, 0x75434d54, 0x75434d50 }, /* ZMCu TMCu PMCu */
	{ 0x78434d5a, 0x78434d54, 0x78434d50 }, /* ZMCx TMCx PMCx */
};
/* the town hall and worker of each race (CreateStartTownHall 0x47df00, CreateMeleeStartUnits 0x47e100) */
static const int64_t TOWN_HALLS[3] = { KP_TYPE_HATCHERY, KP_TYPE_COMMAND_CENTER, KP_TYPE_NEXUS };
static const int64_t WORKERS[3] = { KP_TYPE_DRONE, KP_TYPE_SCV, KP_TYPE_PROBE };
/* the minerals and critters a melee map keeps (IsRecordExcludedByMeleeFilter 0x469600) */
static const int64_t MELEE_KEPT[] = { 176, 177, 178, 188, 89, 90, 93, 94, 95, 96 };

static int tbyte(const KpGameSetup *s, int at) { return s->template_record[at]; }
static uint32_t tdword(const KpGameSetup *s, int at)
{
	const uint8_t *b = s->template_record + at;
	return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
}

int kp_setup_ums(const KpGameSetup *s) { return kp_template_ums(s->template_record); }
int kp_setup_team_game(const KpGameSetup *s) { return s->multiplayer != 0 && tbyte(s, KP_TEMPLATE_TEAMS) != 0; }
int kp_setup_top_vs_bottom(const KpGameSetup *s) { return tbyte(s, KP_TEMPLATE_TYPE) == KP_GAME_TYPE_TOP_VS_BOTTOM; }
int kp_setup_ctf(const KpGameSetup *s) { return tbyte(s, KP_TEMPLATE_VICTORY) == KP_VICTORY_CTF; }
int kp_setup_sudden_death(const KpGameSetup *s) { return tbyte(s, KP_TEMPLATE_VICTORY) == KP_VICTORY_SUDDEN_DEATH; }
int kp_setup_tournament(const KpGameSetup *s) { return tbyte(s, KP_TEMPLATE_TOURNAMENT) != 0; }
int kp_setup_cheats(const KpGameSetup *s) { return tbyte(s, KP_TEMPLATE_CHEATS) == 1; }
int kp_setup_team_size(const KpGameSetup *s)
{
	switch (tbyte(s, KP_TEMPLATE_TEAMS)) {
	case 2: return 4;
	case 3: return 3;
	case 4: return 2;
	default: return -1;
	}
}
int kp_setup_team_count(const KpGameSetup *s) { return kp_setup_top_vs_bottom(s) ? 2 : tbyte(s, KP_TEMPLATE_TEAMS); }
int64_t kp_setup_record_player(const KpGameSetup *s, int64_t record)
{
	int64_t f;
	if (!kp_setup_team_game(s) || record < 0 || record >= 8 || s->type[record] != KP_CTRL_HUMAN) return record;
	f = s->force[record];
	/* a team game's humans are in forces 1..4; force 0 reads leader[0], the 0 RecordSlotSummary 0x47e290 stores */
	return f >= 0 && f <= 4 ? s->leader[f] : record;
}
int kp_setup_alliances_allowed(const KpGameSetup *s)
{
	return tbyte(s, KP_TEMPLATE_ALLIES) == 1 && tbyte(s, KP_TEMPLATE_TOURNAMENT) == 0;
}
int kp_setup_melee_ai(const KpGameSetup *s)
{
	int r = tbyte(s, KP_TEMPLATE_RESTRICTION);
	return (r == 1 || r == 3) && !kp_setup_ums(s);
}

/* A player record (0x6726c8: +4 the Storm ID, +8 type, +9 race, +0xa force, +0xb name; +0 its slot, which the steps
 * below leave equal to the record's index). */
typedef struct {
	int64_t storm, type, race, force;
	char name[KP_SETUP_NAME];
} Record;

static void get(const KpGameSetup *s, int p, Record *r)
{
	r->storm = s->storm[p];
	r->type = s->type[p];
	r->race = s->race[p];
	r->force = s->force[p];
	memcpy(r->name, s->name[p], sizeof r->name);
}

static void put(KpGameSetup *s, int p, const Record *r)
{
	s->storm[p] = r->storm;
	s->type[p] = r->type;
	s->race[p] = r->race;
	s->force[p] = r->force;
	memcpy(s->name[p], r->name, sizeof r->name);
}

/* The setup records 0x673d48 LoadMapHeader 0x46b5c0 makes of the map: OWNR's types, SIDE's races (User Select made
 * Random), the forces of the slots of types 3..7 (ApplyForcesToSlots 0x4432d0: FORC's byte plus one), slots 8..11
 * empty; Storm ID -1, no name. */
static void map_records(const KpMission *m, Record out[KP_SETUP_SLOTS])
{
	int p;
	memset(out, 0, sizeof(Record) * KP_SETUP_SLOTS);
	for (p = 0; p < KP_SETUP_SLOTS; p++) {
		out[p].storm = -1;
		if (p >= 8) continue;
		out[p].type = m->owners[p];
		out[p].race = m->races[p] == KP_RACE_USER_SELECT ? KP_RACE_RANDOM : m->races[p];
		if (out[p].type >= 3 && out[p].type <= 7) out[p].force = (m->force_player[p] + 1) % 256;
	}
}

static int occupied(int64_t type) /* 0x4795b0: 2, 1, 3, 4 or 7 */
{
	return type == KP_CTRL_HUMAN || type == KP_CTRL_COMPUTER || type == KP_CTRL_RESCUE || type == KP_CTRL_RESCUE_ACTIVE
		|| type == KP_CTRL_NEUTRAL;
}

static int available(int64_t type) /* 0x4795e0: 6, 5, 3, 4 or 7 */
{
	return type == KP_CTRL_OPEN || type == KP_CTRL_LOBBY_COMPUTER || type == KP_CTRL_RESCUE
		|| type == KP_CTRL_RESCUE_ACTIVE || type == KP_CTRL_NEUTRAL;
}

void kp_setup_custom_races(KpGameSetup *s, int64_t local, int64_t local_race, const int64_t races[KP_CUSTOM_RACES])
{
	int p, k;
	if (kp_setup_ums(s)) return; /* 0x447db0 */
	for (p = 7; p >= 0; p--) { /* OpenComputerSlots 0x4bb4c0 */
		if (s->type[p] == KP_CTRL_LOBBY_COMPUTER) s->type[p] = KP_CTRL_OPEN;
	}
	if (local >= 0 && local < KP_SETUP_SLOTS) s->race[local] = local_race; /* SetLocalSlotRace 0x4bb530 */
	for (k = 0; k < KP_CUSTOM_RACES; k++) {
		if (races[k] == KP_RACE_NONE) continue;
		for (p = 7; p >= 0; p--) { /* AssignCampaignComputerSlot 0x4bb4e0 */
			if (s->type[p] != KP_CTRL_OPEN) continue;
			s->type[p] = KP_CTRL_LOBBY_COMPUTER;
			s->race[p] = races[k];
			break;
		}
	}
}

static void resolve_random_races(KpGameSetup *s, KpRngGame *rng);

int kp_setup_lobby(KpGameSetup *s, const KpMission *m, const uint8_t template_record[KP_TEMPLATE_SIZE], const char *name,
	int64_t local_race, const int64_t races[KP_CUSTOM_RACES], uint32_t rng_state)
{
	static const int64_t NONE[KP_CUSTOM_RACES] = { KP_RACE_NONE, KP_RACE_NONE, KP_RACE_NONE, KP_RACE_NONE, KP_RACE_NONE,
		KP_RACE_NONE, KP_RACE_NONE, KP_RACE_NONE };
	int p, local = -1;
	memset(s, 0, sizeof *s);
	memcpy(s->template_record, template_record, KP_TEMPLATE_SIZE);
	for (p = 0; p < KP_SETUP_SLOTS; p++) { /* LoadMapHeader 0x46b5c0's step 3 and LoadForceChunk 0x443320 */
		s->storm[p] = -1;
		if (p >= 8) continue;
		s->type[p] = m->owners[p];
		s->race[p] = m->races[p];
		if (s->race[p] == KP_RACE_USER_SELECT) {
			s->race[p] = KP_RACE_RANDOM;
			s->user_select[p] = 1;
		}
		s->force[p] = s->type[p] != 0 ? (m->force_player[p] + 1) % 256 : 0;
		if (s->type[p] != 0) s->slots++; /* the game header's +0x25: the map's player slots */
		if (local < 0 && s->type[p] == KP_CTRL_OPEN) local = p; /* FindFreeJoinSlot 0x4794e0 (no team game offline) */
	}
	if (local < 0) return -1;
	s->type[local] = KP_CTRL_HUMAN;
	snprintf(s->name[local], sizeof s->name[local], "%.*s", KP_PLAYER_NAME - 1, name ? name : "");
	s->storm[local] = 0;
	if (s->template_record[KP_TEMPLATE_VICTORY] == KP_VICTORY_MELEE) { /* IsTemplateMelee 0x447d30 */
		kp_setup_custom_races(s, local, local_race == KP_RACE_NONE ? s->race[local] : local_race, races ? races : NONE);
	} else if (local_race != KP_RACE_NONE) { /* SetLocalRaceAndResolve 0x479c90 */
		KpRngGame rng;
		memset(&rng, 0, sizeof rng);
		rng.state = rng_state;
		rng.enabled = 1;
		s->race[local] = local_race;
		resolve_random_races(s, &rng);
	}
	return 0;
}

/* PickRandomRace 0x47a030 with the picker 0x673ef8..0x673f04 (used Zerg, Terran, Protoss; races left): the k-th of m
 * Random slots. */
typedef struct {
	int used[3];
	int left;
} Picker;

static int64_t pick_race(KpRngGame *rng, Picker *pk, int k, int m)
{
	int64_t r;
	if (pk->left == 1) {
		r = pk->used[0] == 0 ? 0 : pk->used[1] != 0 ? 2 : 1;
		pk->used[0] = pk->used[1] = pk->used[2] = 0;
		pk->left = 3;
		if ((k == 2 && m == 3) || (k == 5 && m == 6)) r = kp_rng_game_range(rng, RNG_RANDOM_RACE, 0, 2);
		return r;
	}
	r = kp_rng_game_range(rng, RNG_RANDOM_RACE, 0, (uint32_t)(pk->left - 1));
	if (pk->left == 2) {
		if (r == 0) {
			if (pk->used[0] != 0) r = 1;
		} else if (r == 1) {
			if (pk->used[2] == 0) r = 2;
		}
		/* a one-in-eight fully random second pick of two */
		if (k == 1 && m == 2 && kp_rng_game_range(rng, RNG_RANDOM_RACE, 0, 7) == 4) r = kp_rng_game_range(rng, RNG_RANDOM_RACE, 0, 2);
	}
	pk->left--;
	pk->used[r] = 1;
	return r;
}

/* ResolveRandomRaces 0x478ee0: a race for each Random slot, the humans' and computers' balanced over the races. */
static void resolve_random_races(KpGameSetup *s, KpRngGame *rng)
{
	Picker pk;
	/* the original's list is stack memory: a team game that turns more slots Random than it counted reads entries it
	 * never wrote; here they are Zerg */
	uint8_t list[8] = { 0 };
	int m = 0, k = 0, p, q, j;
	pk.used[0] = pk.used[1] = pk.used[2] = 0;
	pk.left = 3;
	for (p = 7; p >= 0; p--) {
		if (s->race[p] == KP_RACE_RANDOM && (s->type[p] == KP_CTRL_HUMAN || s->type[p] == KP_CTRL_LOBBY_COMPUTER)) m++;
	}
	for (p = 7; p >= 0; p--) {
		if (s->race[p] != KP_RACE_RANDOM) continue;
		if (s->type[p] == KP_CTRL_HUMAN || s->type[p] == KP_CTRL_LOBBY_COMPUTER) {
			list[k] = (uint8_t)pick_race(rng, &pk, k, m);
			k++;
		} else {
			s->race[p] = kp_rng_game_range(rng, RNG_RANDOM_RACE, 0, 2);
		}
		if (kp_setup_team_game(s) && (s->type[p] == KP_CTRL_COMPUTER || s->type[p] == KP_CTRL_LOBBY_COMPUTER)) {
			for (q = 0; q < 8; q++) {
				if (s->force[q] == s->force[p]) s->race[q] = s->race[p];
			}
		}
	}
	for (j = 0; m != 0 && j < RACE_SWAPS; j++) {
		uint32_t a = kp_rng_game_range(rng, RNG_RANDOM_RACE, 0, (uint32_t)(m - 1));
		uint32_t b = kp_rng_game_range(rng, RNG_RANDOM_RACE, 0, (uint32_t)(m - 1));
		uint8_t t = list[a & 7];
		list[a & 7] = list[b & 7];
		list[b & 7] = t;
	}
	for (p = 7, j = 0; p >= 0; p--) {
		if (s->race[p] == KP_RACE_RANDOM && (s->type[p] == KP_CTRL_HUMAN || s->type[p] == KP_CTRL_LOBBY_COMPUTER))
			s->race[p] = list[j++ & 7];
	}
}

/* RandomizeStartSlots 0x479a00 over the slots of mask (NULL: every slot, unless the template fixes the positions):
 * the occupied slots' records shuffled over the slots the map offers. */
static void randomize_start_slots(KpGameSetup *s, const Record map[KP_SETUP_SLOTS], const uint8_t *mask, KpRngGame *rng)
{
	static const uint8_t ALL[8] = { 1, 1, 1, 1, 1, 1, 1, 1 };
	Record list[8];
	int n = 0, p, k;
	if (!mask) {
		if (tbyte(s, KP_TEMPLATE_FIXED_POSITIONS) != 0) return;
		mask = ALL;
	}
	memset(list, 0, sizeof list);
	for (p = 0; p < 8; p++) {
		if (occupied(s->type[p]) && mask[p] != 0) get(s, p, &list[n++]);
	}
	for (p = 0; p < 8; p++) {
		if (mask[p] != 0) put(s, p, &map[p]);
	}
	for (k = n - 1; k >= 0; k--) {
		uint8_t candidates[8];
		int count = 0;
		uint32_t j;
		for (p = 0; p < 8; p++) {
			if (available(s->type[p]) && mask[p] != 0) candidates[count++] = (uint8_t)p;
		}
		/* RandRange(0, -1) is 0 (no candidate: the original then reads its zeroed list's first byte, slot 0) */
		j = kp_rng_game_range(rng, RNG_START_SLOT, 0, (uint32_t)(count - 1));
		put(s, count > 0 ? candidates[j & 7] : 0, &list[k]);
	}
	for (p = 7; p >= 0; p--) {
		if (!occupied(s->type[p])) s->type[p] = KP_CTRL_NONE;
	}
}

/* ShuffleUmsForceStarts 0x479430: the random starts within each force flagged for them, the user-select slots apart
 * from the others; 1 when a slot took part. */
static int shuffle_ums_force_starts(KpGameSetup *s, const KpMission *m, const Record map[KP_SETUP_SLOTS], KpRngGame *rng)
{
	int f, p, any = 0;
	for (f = 1; f <= FORCES; f++) {
		uint8_t chosen[8] = { 0 }, others[8] = { 0 };
		if ((m->force_flags[f - 1] & FORCE_RANDOM_START) == 0) continue; /* 0x443630 */
		for (p = 7; p >= 0; p--) {
			int64_t t = map[p].type;
			if (s->force[p] != f) continue;
			if (t != KP_CTRL_HUMAN && t != KP_CTRL_COMPUTER && t != KP_CTRL_OPEN && t != KP_CTRL_LOBBY_COMPUTER) continue;
			any = 1;
			if (s->user_select[p] != 0) chosen[p] = 1;
			else others[p] = 1;
		}
		randomize_start_slots(s, map, chosen, rng);
		randomize_start_slots(s, map, others, rng);
	}
	return any;
}

/* ShufflePlayerColors 0x41d460: a permutation of the eight colours, unless the template fixes the positions and no
 * slot took part in a force's random starts. */
static void shuffle_colors(KpGameSetup *s, int any, KpRngGame *rng)
{
	uint8_t left[8];
	int i, k;
	if (tbyte(s, KP_TEMPLATE_FIXED_POSITIONS) != 0 && !any) return;
	for (i = 0; i < 8; i++) left[i] = (uint8_t)i;
	for (i = 7; i >= 0; i--) {
		k = (int)kp_rng_game_range(rng, RNG_COLOR, 0, (uint32_t)i);
		s->color[i] = left[k];
		memmove(left + k, left + k + 1, (size_t)(i - k));
	}
}

int kp_setup_colours_valid(const KpGameSetup *s)
{
	int p;
	for (p = 0; p < 8; p++) {
		if (s->color[p] < 0 || s->color[p] > 7) return 0;
	}
	return 1;
}

int64_t kp_setup_storm_slot(const KpGameSetup *s, int64_t storm)
{
	int p;
	for (p = 7; p >= 0; p--) {
		if (s->type[p] == KP_CTRL_HUMAN && s->storm[p] == storm) return p;
	}
	return -1;
}

/* SetupTeamGameSlots 0x4b2db0: each force's leader (its lowest Storm ID among the humans and computers, a computer's
 * -1 the highest; the lower slot on a tie) keeps its record and takes a random slot the map offers, in the forces'
 * order (none drawn with fixed positions); the other humans, rescuable and neutral records take the first free slots,
 * the other computers of a force leave. The team plays as its leader's slot. */
static void team_slots(KpGameSetup *s, const Record map[KP_SETUP_SLOTS], KpRngGame *rng)
{
	Record leaders[FORCES], members[8];
	int64_t chosen[FORCES];
	int nl = 0, nm = 0, f, p, i;
	/* RestoreSetupSlotTypes 0x4719e0 changes the map's records only: map_records holds them as the lobby began */
	for (f = FORCES; f >= 1; f--) {
		uint32_t lowest = 0xffffffffu;
		chosen[f - 1] = KP_NO_LEADER;
		for (p = 7; p >= 0; p--) {
			if ((s->type[p] == KP_CTRL_HUMAN || s->type[p] == KP_CTRL_COMPUTER) && s->force[p] == f
				&& (uint32_t)s->storm[p] <= lowest) {
				lowest = (uint32_t)s->storm[p];
				chosen[f - 1] = p;
			}
		}
	}
	memset(leaders, 0, sizeof leaders);
	for (i = 0; i < FORCES; i++) {
		if (chosen[i] == KP_NO_LEADER) continue;
		get(s, (int)chosen[i], &leaders[nl++]);
		put(s, (int)chosen[i], &map[chosen[i]]); /* RestoreSlotFromMapSetup 0x479c40 */
	}
	memset(members, 0, sizeof members);
	for (p = 0; p < 8; p++) {
		if (!occupied(s->type[p])) continue;
		if (s->type[p] == KP_CTRL_COMPUTER) s->type[p] = KP_CTRL_NONE;
		else get(s, p, &members[nm++]);
	}
	for (f = 1; f <= FORCES; f++) s->leader[f] = KP_NO_LEADER;
	for (p = KP_SETUP_SLOTS - 1; p >= 0; p--) put(s, p, &map[p]); /* CopyMapSetupToSlots 0x479bf0 */
	for (i = 0; i < nl; i++) {
		uint8_t list[8] = { 0 };
		int n = 0, t;
		for (p = 0; p < 8; p++) {
			if (available(s->type[p])) list[n++] = (uint8_t)p;
		}
		/* RandRange(0, -1) is 0: no slot left reads the zeroed list's slot 0 */
		t = tbyte(s, KP_TEMPLATE_FIXED_POSITIONS) != 0 ? list[0]
			: list[kp_rng_game_range(rng, RNG_TEAM_SLOT, 0, (uint32_t)(n - 1)) & 7];
		put(s, t, &leaders[i]);
		s->leader[leaders[i].force] = t;
	}
	for (i = 0; i < nm; i++) {
		uint8_t list[8] = { 0 };
		int n = 0;
		for (p = 0; p < 8; p++) {
			if (!occupied(s->type[p])) list[n++] = (uint8_t)p;
		}
		put(s, list[0], &members[i]);
	}
	for (p = 7; p >= 0; p--) {
		if (!occupied(s->type[p])) s->type[p] = KP_CTRL_NONE;
	}
}

void kp_gametype_slots(KpGame *g, const KpMission *m, KpRngGame *rng)
{
	KpGameSetup *s = &g->setup;
	Record map[KP_SETUP_SLOTS];
	int p, any = 0;
	/* the user-select flags 0x673f08 the lobby's LoadMapHeader 0x46b5c0 set from the map: SIDE's User Select */
	for (p = 0; p < 8; p++) s->user_select[p] = m->races[p] == KP_RACE_USER_SELECT;
	for (p = 0; p < 8; p++) s->color[p] = p; /* LoadPlayerColorTables 0x41d570 */
	resolve_random_races(s, rng);
	/* RecordSlotSummary 0x47e290: the races in the lobby's order (and leader[0]'s 0) */
	for (p = 0; p < 8; p++) s->race_summary[p] = s->race[p];
	s->leader[0] = 0;
	/* SetupGameSlots 0x479340 */
	for (p = KP_SETUP_SLOTS - 1; p >= 0; p--) {
		if (s->type[p] == KP_CTRL_LOBBY_COMPUTER) s->type[p] = KP_CTRL_COMPUTER;
		else if (s->type[p] == KP_CTRL_OPEN || s->type[p] == KP_CTRL_CLOSED) s->type[p] = KP_CTRL_NONE;
	}
	map_records(m, map);
	if (kp_setup_team_game(s)) {
		if (kp_setup_team_size(s) < 0) kp_game_fail(g, "start: %d teams (error 0x5d)", tbyte(s, KP_TEMPLATE_TEAMS));
		team_slots(s, map, rng);
	} else if (kp_setup_ums(s)) {
		any = shuffle_ums_force_starts(s, m, map, rng);
	} else {
		randomize_start_slots(s, map, NULL, rng);
	}
	shuffle_colors(s, any, rng);
	s->started = 1;
}

void kp_gametype_resources(KpGame *g)
{
	KpPlayers *pl = &g->players;
	int64_t minerals = tbyte(&g->setup, KP_TEMPLATE_RESOURCES) == 1 ? (int64_t)tdword(&g->setup, KP_TEMPLATE_RESOURCE_VALUE) : 0;
	int p;
	for (p = 0; p < KP_PLAYERS; p++) {
		pl->minerals[p] = pl->cumulative_minerals[p] = minerals;
		pl->gas[p] = pl->cumulative_gas[p] = 0;
	}
}

int kp_template_unit_excluded(const uint8_t *template_record, int64_t t, int64_t owner)
{
	size_t k;
	if (template_record[KP_TEMPLATE_STARTING_UNITS] == 0) return 0;
	if (owner != KP_PLAYER_NEUTRAL) return 1;
	for (k = 0; k < sizeof MELEE_KEPT / sizeof MELEE_KEPT[0]; k++) {
		if (MELEE_KEPT[k] == t) return 0;
	}
	return 1;
}

int kp_gametype_unit_excluded(KpGame *g, int64_t t, int64_t owner)
{
	if (tbyte(&g->setup, KP_TEMPLATE_STARTING_UNITS) != 0) return kp_template_unit_excluded(g->setup.template_record, t, owner); /* 0x469600 */
	if (kp_setup_ums(&g->setup) && owner >= 0 && owner < 8 && g->setup.user_select[owner] != 0) {
		return (KP_DAT(g, units, staredit_group_flags, t) & GROUP_NEUTRAL) == 0;
	}
	return 0;
}

static int64_t start_x(KpGame *g, int64_t p) { return kp_s16v(g->players.start_x[p]); }
/* units.dat's extents of t (0x522c38: left, up, right, down WORDs) */
static int64_t extent(KpGame *g, int64_t t, int k) { return KP_DAT(g, units, unit_extents, t * 4 + k); }
static int64_t start_y(KpGame *g, int64_t p) { return kp_s16v(g->players.start_y[p]); }

/* CreateStartTownHall 0x47df00: the race's town hall at p's start location snapped to the 32-pixel grid, the units
 * under its footprint killed first; a Zerg's creep and two larvae. */
static void start_town_hall(KpGame *g, int64_t race, int64_t p)
{
	int64_t t = race >= 0 && race < 3 ? TOWN_HALLS[race] : (int64_t)KP_TYPE_NONE, n, hw, hh, x, y, s;
	int64_t units[KP_UNITS];
	int count, i;
	hw = kp_s16v(KP_DAT(g, units, staredit_placement_size, t * 2)) / 2;
	hh = kp_s16v(KP_DAT(g, units, staredit_placement_size, t * 2 + 1)) / 2;
	x = kp_band(kp_band(g->players.start_x[p] - hw, 0xffe0) + hw, 0xffff);
	y = kp_band(kp_band(g->players.start_y[p] - hh, 0xffe0) + hh, 0xffff);
	/* FindUnitOverlappingRect 0x443110 with KillUnitFlagged 0x47def0 (which never stops the search) */
	count = kp_spatial_query(g, kp_s16v(x - hw), kp_s16v(y - hh), kp_s16v(x + hw - 1), kp_s16v(y + hh - 1), units, KP_UNITS);
	for (i = 0; i < count; i++) kp_kill_flagged(g, units[i]);
	n = kp_unit_create(g, t, x, y, p);
	if (n == 0) return;
	(void)kp_unit_complete(g, n);
	if (kp_creep_provider_state(g->u.type[n], 1) == 0 && (KP_DAT(g, units, special_ability_flags, g->u.type[n]) & DAT_CREEP) == 0)
		return;
	s = kp_unit_sprite(g, n);
	kp_creep_place_initial(g, g->u.type[n], kp_s16v(g->w->spr.x[s]), kp_s16v(g->w->spr.y[s]));
	kp_larva_spawn_start(g, n); /* SpawnStartLarva 0x47e020, twice */
	kp_larva_spawn_start(g, n);
}

/* CreateStartOverlord 0x47e080: a Zerg's Overlord a Hatchery's size away from the start location, towards the map's
 * middle. */
static void start_overlord(KpGame *g, int64_t p)
{
	int64_t dx = kp_s16v(extent(g, KP_TYPE_HATCHERY, 0) + extent(g, KP_TYPE_HATCHERY, 2) + 1);
	int64_t dy = kp_s16v(extent(g, KP_TYPE_HATCHERY, 1) + extent(g, KP_TYPE_HATCHERY, 3) + 1);
	int64_t sx = start_x(g, p), sy = start_y(g, p), n;
	int64_t x = sx < g->map_w_px / 2 ? sx + dx : sx - dx, y = sy < g->map_h_px / 2 ? sy + dy : sy - dy;
	n = kp_unit_create(g, KP_TYPE_OVERLORD, x, y, p);
	if (n != 0) (void)kp_unit_complete(g, n);
}

/* ForceHasHuman 0x4b2d20 / ForceHasComputer 0x4b2cf0: a slot 0..7 of force f with a human (or a computer, a lobby's
 * too). */
static int force_has(KpGame *g, int64_t f, int computer)
{
	int p;
	for (p = 7; p >= 0; p--) {
		int64_t c = g->players.controller[p];
		if (g->players.force[p] != f) continue;
		if (computer ? c == KP_CTRL_COMPUTER || c == KP_CTRL_LOBBY_COMPUTER : c == KP_CTRL_HUMAN) return 1;
	}
	return 0;
}

static int team_size(KpGame *g)
{
	int size = kp_setup_team_size(&g->setup);
	if (size < 0) kp_game_fail(g, "teams: %d teams (error 0x5d)", tbyte(&g->setup, KP_TEMPLATE_TEAMS));
	return size;
}

/* The leader of team t (GetTeamLeader 0x4b2be0) when the team has a human or a computer, else -1. */
static int64_t team_leader(KpGame *g, int64_t t)
{
	int64_t p;
	if (!force_has(g, t, 0) && !force_has(g, t, 1)) return -1;
	p = g->setup.leader[t];
	if (p < 0 || p >= 8) kp_game_fail(g, "teams: force %lld has no leader", (long long)t);
	return p;
}

/* CreateTeamStartingUnits 0x47eb00: each team's leader gets the town hall of its block's first lobby race and four
 * workers of the block's races in turn (RecordSlotSummary 0x47e290's lobby order; an index past slot 7 reads the
 * block's second), at its start location; the members own nothing. */
static void team_start_units(KpGame *g)
{
	const KpGameSetup *s = &g->setup;
	int64_t mode = tbyte(s, KP_TEMPLATE_STARTING_UNITS), t, p, base, race;
	int k;
	for (t = 1; t <= kp_setup_team_count(s); t++) {
		if ((p = team_leader(g, t)) < 0) continue;
		base = (team_size(g) * (t - 1)) & 0xff;
		if (base >= 8) kp_game_fail(g, "teams: team %lld's block %lld", (long long)t, (long long)base);
		if (mode == 2) {
			start_town_hall(g, s->race_summary[base], p);
			if (s->race_summary[base] == KP_RACE_ZERG) start_overlord(g, p);
		}
		for (k = 0; k < START_WORKERS; k++) {
			int64_t at = base + k % team_size(g), n, worker;
			race = at < 8 ? s->race_summary[at] : s->race_summary[base + 1];
			if (mode != 1 && mode != 2) continue;
			worker = race >= 0 && race < 3 ? WORKERS[race] : (int64_t)KP_TYPE_NONE;
			n = kp_unit_create(g, worker, start_x(g, p), start_y(g, p), p);
			if (n != 0) (void)kp_unit_complete(g, n);
		}
	}
}

/* CountTeamRace 0x4b2c00: the lobby races of team t's block that are race (an index past slot 7 reads the block's
 * first). */
static int64_t team_race_count(KpGame *g, int64_t t, int64_t race)
{
	int64_t size = team_size(g), base = (size * (t - 1)) & 0xff, count = 0, k;
	for (k = 0; k < size; k++) count += g->setup.race_summary[base + k < 8 ? base + k : base] == race;
	return count & 0xff;
}

void kp_gametype_supply_max(KpGame *g, int64_t max[3][KP_PLAYERS])
{
	int64_t t, race, p;
	for (race = 0; race < 3; race++) {
		for (p = 0; p < KP_PLAYERS; p++) max[race][p] = kp_setup_team_game(&g->setup) ? 0 : KP_SUPPLY_MAX;
	}
	if (!kp_setup_team_game(&g->setup)) return;
	for (t = 1; t <= FORCES; t++) {
		p = g->setup.leader[t];
		if (p == KP_NO_LEADER) continue;
		if (p < 0 || p >= KP_PLAYERS) kp_game_fail(g, "teams: leader %lld", (long long)p);
		for (race = 0; race < 3; race++) {
			int64_t n = team_race_count(g, t, race) * KP_SUPPLY_MAX;
			max[race][p] = n < KP_SUPPLY_MAX ? KP_SUPPLY_MAX : n;
		}
	}
}

void kp_gametype_start_units(KpGame *g)
{
	const KpGameSetup *s = &g->setup;
	int p, k;
	if (kp_setup_team_game(s)) {
		team_start_units(g);
		return;
	}
	for (p = 7; p >= 0; p--) { /* CreateMeleeStartUnits 0x47e100 */
		int64_t type = g->players.controller[p], race = g->players.race[p], mode, worker;
		if (type != KP_CTRL_HUMAN && type != KP_CTRL_COMPUTER) continue;
		mode = tbyte(s, KP_TEMPLATE_STARTING_UNITS);
		if (kp_setup_ums(s) && s->user_select[p] != 0) mode = 2; /* IsUserSelectSlot 0x479610 */
		if (mode != 1 && mode != 2) continue;
		if (mode == 2) {
			start_town_hall(g, race, p);
			if (race == KP_RACE_ZERG) start_overlord(g, p);
		}
		worker = race >= 0 && race < 3 ? WORKERS[race] : (int64_t)KP_TYPE_NONE;
		for (k = 0; k < START_WORKERS; k++) {
			int64_t n = kp_unit_create(g, worker, start_x(g, p), start_y(g, p), p);
			if (n != 0) (void)kp_unit_complete(g, n);
		}
	}
}

/* CreateFlagBeaconForPlayer 0x448210: the beacon of p's race at a site near p's start location, found from p's first
 * worker. */
static void flag_beacon(KpGame *g, int64_t p)
{
	int64_t t = BEACON_BASE + g->setup.race[p], n = g->player_units[p], x, y;
	while (n != 0 && (KP_DAT(g, units, special_ability_flags, g->u.type[n]) & DAT_WORKER) == 0) n = g->u.player_next[n];
	if (n == 0) return;
	if (!kp_ai_place_from(g, n, t, start_x(g, p), start_y(g, p), 0, BEACON_SIZE, &x, &y)) return;
	n = kp_unit_create(g, t, kp_s16v(x), kp_s16v(y), p);
	if (n != 0) kp_unit_complete_in_place(g, n);
}

/* The melee scripts the computers will start, checked before the game as kp_mission_check_ai checks a trigger's
 * (ADR 0023): one whose code reaches fatal_error or reads outside its binary (a modded aiscript.bin, the oracle's patched
 * one) refuses the game. Each race's script once. */
static int check_melee_ai(KpGame *g, KpError *err)
{
	char why[KP_ERROR_SIZE];
	int p, checked = 0;
	if (!g->ai_bins) return 0;
	for (p = 0; p < 8; p++) {
		int64_t race = g->setup.race[p];
		if (g->setup.type[p] != KP_CTRL_COMPUTER || race < 0 || race > 2 || (checked & 1 << race)) continue;
		checked |= 1 << race;
		if (kp_aiscript_check(g->ai_bins, MELEE_SCRIPTS[g->expansion != 0][race], why, sizeof why) != 0) {
			snprintf(err->msg, sizeof err->msg, "start: %s", why);
			return -1;
		}
	}
	return 0;
}

/* The melee AI's start (StartCustomLevelAIScripts 0x4486a0): each computer slot of a race runs its race's script
 * from its start location. */
static void start_melee_ai(KpGame *g)
{
	int p;
	for (p = 0; p < 8; p++) {
		int64_t race = g->setup.race[p], x, y, rect[4];
		if (g->setup.type[p] != KP_CTRL_COMPUTER || race < 0 || race > 2) continue;
		x = start_x(g, p);
		y = start_y(g, p);
		rect[0] = x - 1;
		rect[1] = y - 1;
		rect[2] = x + 1;
		rect[3] = y + 1;
		kp_aiscript_launch(g, MELEE_SCRIPTS[g->expansion != 0][race], p, rect);
	}
}

/* The file of the template's victory (P15's table): its index in KP_VICTORY_FILE_NAMES, -1 none, -2 an unknown
 * value. */
static int victory_file(const KpGameSetup *s)
{
	uint32_t value = tdword(s, KP_TEMPLATE_VALUE);
	switch (tbyte(s, KP_TEMPLATE_VICTORY)) {
	case KP_VICTORY_MELEE: case KP_VICTORY_MELEE_TRIGGERS: return 0;
	case KP_VICTORY_SUDDEN_DEATH: return 1;
	case KP_VICTORY_GREED:
		return value == 2500 ? 2 : value == 5000 ? 3 : value == 7500 ? 4 : value == 10000 ? 5 : -2;
	case KP_VICTORY_SLAUGHTER:
		return value == 15 ? 6 : value == 30 ? 7 : value == 45 ? 8 : value == 60 ? 9 : -2;
	case KP_VICTORY_CTF: return 10;
	default: return -1;
	}
}

int kp_gametype_victory(KpGame *g, const struct KpVictoryFiles *v, KpError *err)
{
	const KpGameSetup *s = &g->setup;
	int file = victory_file(s), p;
	if (kp_setup_melee_ai(s)) {
		if (check_melee_ai(g, err) != 0) return -1;
		start_melee_ai(g);
	}
	if (file == -1) return 0;
	if (file == -2) {
		snprintf(err->msg, sizeof err->msg, "start: victory %d of value %u has no trigger file (error 0x5d)",
			tbyte(s, KP_TEMPLATE_VICTORY), (unsigned)tdword(s, KP_TEMPLATE_VALUE));
		return -1;
	}
	if (tbyte(s, KP_TEMPLATE_VICTORY) == KP_VICTORY_CTF) { /* CreateFlagBeacons 0x4480f0 */
		if (kp_setup_team_game(s)) { /* the teams' leaders, the last team first */
			int64_t t, leader;
			for (t = kp_setup_team_count(s); t >= 1; t--) {
				if ((leader = team_leader(g, t)) >= 0) flag_beacon(g, leader);
			}
		} else {
			for (p = 7; p >= 0; p--) {
				if (s->type[p] == KP_CTRL_HUMAN || s->type[p] == KP_CTRL_COMPUTER) flag_beacon(g, p);
			}
		}
	}
	if (!v || !v->data[file]) {
		snprintf(err->msg, sizeof err->msg, "start: no %s (error 0x5d)", KP_VICTORY_FILE_NAMES[file]);
		return -1;
	}
	/* the embedded 0x447f70: FreeAllTriggerLists 0x4be1d0, then each whole record through 0x4bdb00 */
	kp_triggers_replace(g, v->data[file], v->size[file] / TRIGGER_RECORD * TRIGGER_RECORD);
	return 0;
}
