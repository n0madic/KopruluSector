/* The campaign progression of the original (kp_campaign.h). */
#include "kp_campaign.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ROW_SIZE 8
#define ORIGINAL_EXTENSION ".scm"

static int fail(KpError *err, const char *name, const char *what)
{
	snprintf(err->msg, sizeof err->msg, "campaign: %s: %s", name, what);
	return -1;
}

int kp_campaign_load(KpCampaign *c, const uint8_t *data, size_t size, const char *name, KpError *err)
{
	KpKpf k;
	KpKpfExpect want;
	const KpKpfSection *d;
	const uint8_t *t, *ch;
	uint32_t tn = 0, cn = 0;
	int rc;
	memset(c, 0, sizeof *c);
	if (kp_kpf_parse(&k, data, size, name, 0, err) != 0) return -1;
	memset(&want, 0, sizeof want);
	want.kind = KP_CAMPAIGN_KIND;
	want.schema_version = KP_CAMPAIGN_SCHEMA;
	if (kp_kpf_check(&k, &want, err) != 0 || !(t = kp_kpf_table(&k, "TABL", &tn, err))
		|| !(d = kp_kpf_require(&k, "MAPD", err)) || !(ch = kp_kpf_table(&k, "CHEA", &cn, err))) {
		kp_kpf_free(&k);
		return -1;
	}
	rc = kp_campaign_from(c, t, tn, d->data, d->size, ch, cn, name, err);
	kp_kpf_free(&k);
	return rc;
}

int kp_campaign_from(KpCampaign *c, const uint8_t *tabl, size_t tabl_size, const uint8_t *mapd, size_t mapd_size,
	const uint8_t *chea, size_t chea_size, const char *name, KpError *err)
{
	size_t at, rows = 0, bytes = 0;
	int n;
	char *heap, *text;
	KpCampaignRow *row;
	memset(c, 0, sizeof *c);
	/* the sizes first: every row, every directory with its terminator */
	if (tabl_size < 1 || tabl[0] > KP_CAMPAIGN_TABLES) goto bad;
	for (n = 0, at = 1; n < tabl[0]; n++) {
		size_t count;
		if (at + 4 > tabl_size) goto bad;
		count = kp_le_u16(tabl + at + 2);
		at += 4 + count * ROW_SIZE;
		if (at > tabl_size) goto bad;
		rows += count;
	}
	if (mapd_size < 2 || kp_le_u16(mapd) > KP_CAMPAIGN_MISSIONS) goto bad;
	for (n = 0, at = 2; n < (int)kp_le_u16(mapd); n++) {
		size_t len;
		if (at + 2 > mapd_size) goto bad;
		len = kp_le_u16(mapd + at);
		if (at + 2 + len > mapd_size) goto bad;
		bytes += len + 1;
		at += 2 + len;
	}
	if (chea_size < 4) goto bad;
	heap = (char *)malloc(rows * sizeof(KpCampaignRow) + bytes + 1);
	if (!heap) {
		return fail(err, name, "out of memory");
	}
	c->owned = heap;
	row = (KpCampaignRow *)heap;
	text = heap + rows * sizeof(KpCampaignRow);
	c->table_count = tabl[0];
	for (n = 0, at = 1; n < c->table_count; n++) {
		KpCampaignTable *tab = &c->tables[n];
		int i;
		tab->expansion = tabl[at];
		tab->race = tabl[at + 1];
		tab->count = (int)kp_le_u16(tabl + at + 2);
		tab->rows = row;
		at += 4;
		for (i = 0; i < tab->count; i++, row++, at += ROW_SIZE) {
			row->label = (uint16_t)kp_le_u16(tabl + at);
			row->mission = (uint16_t)kp_le_u16(tabl + at + 2);
			row->cinematic = (uint16_t)kp_le_u16(tabl + at + 4);
			row->race = tabl[at + 6];
			row->hidden = tabl[at + 7];
		}
	}
	c->directory_count = (int)kp_le_u16(mapd);
	for (n = 0, at = 2; n < c->directory_count; n++) {
		size_t len = kp_le_u16(mapd + at);
		memcpy(text, mapd + at + 2, len);
		text[len] = '\0';
		c->directories[n] = text;
		text += len + 1;
		at += 2 + len;
	}
	c->carried = kp_le_u32(chea);
	return 0;
bad:
	memset(c, 0, sizeof *c);
	return fail(err, name, "malformed TABL, MAPD or CHEA section");
}

void kp_campaign_free(KpCampaign *c)
{
	free(c->owned);
	memset(c, 0, sizeof *c);
}

static KpCampaignRef no_row(void)
{
	KpCampaignRef r;
	r.table = -1;
	r.index = 0;
	return r;
}

void kp_campaign_new_run(KpCampaignRun *run, int expansion, int64_t mission)
{
	memset(run, 0, sizeof *run);
	run->expansion = expansion;
	run->row = no_row();
	run->mission = mission;
}

void kp_campaign_new_profile(KpCampaignProfile *p) { memset(p, 0, sizeof *p); }

const KpCampaignRow *kp_campaign_row(const KpCampaign *c, KpCampaignRef ref)
{
	if (ref.table < 0 || ref.table >= c->table_count || ref.index < 0 || ref.index >= c->tables[ref.table].count) {
		return NULL;
	}
	return &c->tables[ref.table].rows[ref.index];
}

/* FindCampaignRowByMission 0x4bbae0: in the three tables of a group, the first row of the mission with the cinematic
 * (0: a playable row, the original's lookup; else the history's cinematic row). */
static KpCampaignRef find_row(const KpCampaign *c, int group, int64_t mission, int64_t cinematic)
{
	int k, i;
	for (k = group * KP_CAMPAIGN_TABLES_PER_GROUP; k < (group + 1) * KP_CAMPAIGN_TABLES_PER_GROUP && k < c->table_count;
		k++) {
		const KpCampaignTable *t = &c->tables[k];
		for (i = 0; i < t->count; i++) {
			if (t->rows[i].mission == 0) break;
			if (t->rows[i].cinematic == cinematic && t->rows[i].mission == mission) {
				KpCampaignRef r;
				r.table = k;
				r.index = i;
				return r;
			}
		}
	}
	return no_row();
}

/* The current row (0x4bb790 step 3): the cached one when it is the game's mission, else looked up in the original's
 * tables, then in the expansion's. */
static KpCampaignRef current_row(const KpCampaign *c, const KpCampaignRun *run)
{
	const KpCampaignRow *r = kp_campaign_row(c, run->row);
	KpCampaignRef ref;
	if (r && r->mission == run->mission) return run->row;
	ref = find_row(c, 0, run->mission, 0);
	return ref.table >= 0 ? ref : find_row(c, 1, run->mission, 0);
}

/* FindMissionIndexByBasename 0x4bb9d0: from index, the first directory whose basename (after its last "\") starts
 * with name, case-insensitively (SStrCmpI over the name's length); -1 for none. */
static int find_index(const KpCampaign *c, const char *name, size_t length, int index)
{
	for (; index < KP_CAMPAIGN_MISSIONS; index++) {
		const char *dir = index < c->directory_count && c->directories[index] ? c->directories[index] : "";
		const char *slash = strrchr(dir, '\\');
		size_t k;
		if (!slash) continue;
		slash++;
		for (k = 0; k < length; k++) {
			if (!slash[k] || tolower((unsigned char)slash[k]) != tolower((unsigned char)name[k])) break;
		}
		if (k == length) return index;
	}
	return -1;
}

KpCampaignRef kp_campaign_resolve(const KpCampaign *c, const KpCampaignRun *run, const char *name)
{
	const char *dot = strrchr(name, '.');
	int group = 0, index = 0;
	size_t k;
	if (!dot) return run->row;
	if (strlen(dot) != strlen(ORIGINAL_EXTENSION)) group = 1;
	for (k = 0; group == 0 && dot[k]; k++) {
		if (tolower((unsigned char)dot[k]) != ORIGINAL_EXTENSION[k]) group = 1;
	}
	for (;;) {
		KpCampaignRef ref;
		index = find_index(c, name, (size_t)(dot - name), index);
		if (index < 0) return run->row;
		ref = find_row(c, group, index, 0);
		if (ref.table >= 0) return ref;
		index++;
	}
}

/* RecordCampaignProgress 0x4bbc20: the row's race's field rises to its mission. */
static void record_progress(const KpCampaign *c, const KpCampaignRun *run, KpCampaignProfile *p, KpCampaignRef ref)
{
	const KpCampaignRow *r = kp_campaign_row(c, ref);
	int64_t *field = run->expansion != 0 ? p->expansion : p->original;
	if (r && field[r->race] < r->mission) field[r->race] = r->mission;
}

void kp_campaign_record_next_reached(const KpCampaign *c, const KpCampaignRun *run, KpCampaignProfile *p)
{
	KpCampaignRef ref = current_row(c, run);
	const KpCampaignRow *r = kp_campaign_row(c, ref);
	if (!r || r->mission == 0) return;
	ref.index++;
	record_progress(c, run, p, ref);
}

/* The glue state of the ready room of a race (any other race: Protoss's). */
static int64_t ready_room(int64_t race)
{
	static const int64_t ROOMS[3] = { 8, 7, 9 }; /* Zerg, Terran, Protoss */
	return race >= 0 && race < 3 ? ROOMS[race] : ROOMS[2];
}

void kp_campaign_advance(const KpCampaign *c, KpCampaignRun *run, KpCampaignProfile *p, int won, KpCampaignStep *out)
{
	KpCampaignRef ref;
	const KpCampaignRow *r;
	memset(out, 0, sizeof *out);
	out->kind = KP_CAMPAIGN_STEP_MENU;
	if (!won) {
		out->kind = KP_CAMPAIGN_STEP_RESTART;
		return;
	}
	ref = current_row(c, run);
	r = kp_campaign_row(c, ref);
	if (!r || r->mission == 0) return;
	if (run->pending[0]) {
		KpCampaignRun at = *run;
		at.row = ref;
		ref = kp_campaign_resolve(c, &at, run->pending);
		run->pending[0] = '\0';
	} else {
		ref.index++;
	}
	run->row = ref;
	record_progress(c, run, p, ref);
	r = kp_campaign_row(c, ref);
	if (!r) return;
	if (r->mission == 0) {
		run->row = no_row();
		if (run->expansion != 0) {
			if (r->race != 0) {
				out->kind = KP_CAMPAIGN_STEP_GLUE;
				out->glue = KP_CAMPAIGN_GLUE_EXPANSION_MENU;
			} else {
				out->kind = KP_CAMPAIGN_STEP_EPILOG;
			}
			return;
		}
		if (r->race == 2) {
			out->kind = KP_CAMPAIGN_STEP_EPILOG;
		} else {
			out->kind = KP_CAMPAIGN_STEP_GLUE;
			out->glue = KP_CAMPAIGN_GLUE_MENU;
		}
		return;
	}
	run->mission = r->mission;
	out->mission = r->mission;
	if (r->cinematic != 0) {
		out->kind = KP_CAMPAIGN_STEP_CINEMATIC;
		out->cinematic = r->cinematic;
		return;
	}
	out->kind = KP_CAMPAIGN_STEP_MISSION;
	out->race = r->race;
	out->glue = ready_room(r->race);
}

/* AdvanceCampaignAfterGame 0x4bb790 through the cinematic rows it meets (mode 10 plays each, then advances again). */
static void advance_past_cinematics(const KpCampaign *c, KpCampaignRun *run, KpCampaignProfile *p, int won,
	KpCampaignStep *out, int64_t *cinematics, int *count, int max)
{
	kp_campaign_advance(c, run, p, won, out);
	while (out->kind == KP_CAMPAIGN_STEP_CINEMATIC) {
		if (*count < max) cinematics[(*count)++] = out->cinematic;
		kp_campaign_advance(c, run, p, 1, out);
	}
}

void kp_campaign_after_game(const KpCampaign *c, KpCampaignRun *run, KpCampaignProfile *p, int64_t result,
	const char *next_scenario, KpCampaignStep *out, int64_t *cinematics, int *count, int max)
{
	snprintf(run->pending, sizeof run->pending, "%s", next_scenario ? next_scenario : "");
	*count = 0;
	kp_campaign_record_next_reached(c, run, p);
	advance_past_cinematics(c, run, p, result == KP_CAMPAIGN_RESULT_VICTORY, out, cinematics, count, max);
}

int kp_campaign_after_cinematic(const KpCampaign *c, int expansion, int64_t mission, int64_t cinematic,
	KpCampaignRun *run, KpCampaignProfile *p, KpCampaignStep *out, int64_t *cinematics, int *count, int max)
{
	int group = expansion != 0;
	KpCampaignRef ref = find_row(c, group, mission, cinematic);
	*count = 0;
	memset(out, 0, sizeof *out);
	out->kind = KP_CAMPAIGN_STEP_MENU;
	if (ref.table < 0) return 0;
	kp_campaign_new_run(run, group, mission);
	run->row = ref;
	advance_past_cinematics(c, run, p, 1, out, cinematics, count, max);
	return 1;
}

int kp_campaign_run_of_mission(const KpCampaign *c, int64_t mission, KpCampaignRun *run)
{
	int group;
	for (group = 0; group < 2; group++) {
		KpCampaignRef ref = find_row(c, group, mission, 0);
		if (ref.table >= 0) {
			kp_campaign_new_run(run, group, mission);
			run->row = ref;
			return 1;
		}
	}
	return 0;
}

int kp_campaign_mission_name(const KpCampaign *c, int64_t index, char *out, size_t size)
{
	const char *dir = index >= 0 && index < c->directory_count ? c->directories[index] : NULL;
	const char *at;
	size_t k = 0;
	if (!dir || !dir[0] || size == 0) return 0;
	at = strchr(dir, '\\'); /* the first component goes ("campaign\") */
	at = at ? at + 1 : dir;
	for (; *at && k + 1 < size; at++) out[k++] = *at == '\\' ? '/' : (char)tolower((unsigned char)*at);
	out[k] = '\0';
	return 1;
}

#define MAPDATA_DAT "arr\\mapdata.dat"
#define MAPDATA_TBL "arr\\mapdata.tbl"

int kp_campaign_from_files(KpCampaign *c, const KpFiles *f, KpError *err)
{
	const KpDefsTable *tabl = kp_defs_table(f->profile, "TABL"), *chea = kp_defs_table(f->profile, "CHEA");
	size_t dat_size, tbl_size, at = 2;
	uint8_t *dat = NULL, *tbl = NULL, *mapd = NULL;
	uint32_t n, count;
	int rc = -1;
	memset(c, 0, sizeof *c);
	if (!tabl || !chea) return fail(err, "campaign", "no TABL or CHEA table compiled for the profile");
	if (!(dat = kp_files_need(f, MAPDATA_DAT, &dat_size, err)) || !(tbl = kp_files_need(f, MAPDATA_TBL, &tbl_size, err)))
		goto done;
	/* campaign.directories: the strings 0x51abb8 by mapdata index (u32 TBL ids), "" where the id is 0 */
	if (dat_size < 4 * KP_CAMPAIGN_MISSIONS || tbl_size < 2 || 2 + 2 * (size_t)kp_le_u16(tbl) > tbl_size) {
		fail(err, MAPDATA_DAT, "mapdata.dat or mapdata.tbl is malformed");
		goto done;
	}
	count = kp_le_u16(tbl);
	if (!(mapd = (uint8_t *)malloc(2 + KP_CAMPAIGN_MISSIONS * (2 + tbl_size)))) {
		fail(err, "campaign", "out of memory");
		goto done;
	}
	mapd[0] = KP_CAMPAIGN_MISSIONS;
	mapd[1] = 0;
	for (n = 0; n < KP_CAMPAIGN_MISSIONS; n++) {
		uint32_t ref = kp_le_u32(dat + 4 * n);
		size_t off = 0, end = 0;
		if (ref != 0) {
			if (ref > count) {
				fail(err, MAPDATA_DAT, "a mission's string is outside mapdata.tbl");
				goto done;
			}
			off = kp_le_u16(tbl + 2 * ref);
			if (off > tbl_size) off = tbl_size; /* decode_tbl: an offset past the end is an empty string */
			for (end = off; end < tbl_size && tbl[end]; end++) {
			}
		}
		mapd[at] = (uint8_t)(end - off);
		mapd[at + 1] = (uint8_t)((end - off) >> 8);
		memcpy(mapd + at + 2, tbl + off, end - off);
		at += 2 + (end - off);
	}
	rc = kp_campaign_from(c, tabl->data, tabl->size, mapd, at, chea->data, chea->size, "campaign", err);
done:
	free(dat);
	free(tbl);
	free(mapd);
	return rc;
}
