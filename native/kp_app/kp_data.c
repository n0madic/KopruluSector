/* The original data a process plays from (kp_data.h). */
#include "kp_data.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kp_mpq.h"

const char *const KP_DATA_VECTOR_FONT_FILES[KP_DATA_VECTOR_FONTS][2] = {
	{ "fonts\\Exo2.ttf", "exo2/Exo2.ttf" },
	{ "fonts\\Michroma-Regular.ttf", "michroma/Michroma-Regular.ttf" },
};

int kp_data_dir(const char *root, char *out, size_t size)
{
	static const char *const REPACKS[] = { "generated/repack-single", "generated/repack" };
	size_t k;
	if (kp_res_detect(root, NULL) != KP_RES_SOURCE_NONE) {
		snprintf(out, size, "%s", root);
		return 1;
	}
	for (k = 0; k < sizeof REPACKS / sizeof REPACKS[0]; k++) {
		snprintf(out, size, "%s/%s", root, REPACKS[k]);
		if (kp_res_detect(out, NULL) == KP_RES_SOURCE_REPACK) return 1;
	}
	out[0] = '\0';
	return 0;
}

/* The maps' signature key from a 1.17 install's executable (none from a repack or Remastered: their maps' class 0).
 * TODO: a network lobby that offers the Ladder, Tournament or Server templates on the web or a phone needs the key
 * there too: kp_convert would carry it from the player's install in the repack (never committed), as it carries the
 * rest of the 1.17 data. */
static void map_key(KpData *d, KpRes *res)
{
	char why[KP_ERROR_SIZE];
	size_t size = 0;
	uint8_t *exe = kp_res_executable(res, &size, why, sizeof why);
	d->has_map_key = exe != NULL && kp_map_key_from_executable(exe, size, d->map_key);
	free(exe);
}

int kp_data_build(KpData *d, KpRes *res, const KpUiFile *vector_fonts, int vector_font_count, const char *scenarios,
	size_t scenarios_size, KpError *err)
{
	memset(d, 0, sizeof *d);
	kp_res_files(res, &d->files);
	kp_res_raw_files(res, &d->raw);
	if (!d->files.profile) {
		snprintf(err->msg, sizeof err->msg, "the original data has no compiled profile");
		return -1;
	}
	if (kp_dat_from_files(&d->dat, &d->files, err) != 0
		|| kp_host_shared_from_files(&d->shared, &d->files, &d->dat, err) != 0
		|| kp_palettes_from_files(&d->palettes, &d->files, err) != 0
		|| kp_campaign_from_files(&d->campaign, &d->files, err) != 0
		|| kp_ui_data_from_files(&d->ui, &d->files, &d->dat, vector_fonts, vector_font_count, err) != 0
		|| kp_templates_from_files(&d->templates, &d->files, err) != 0) {
		kp_data_free(d);
		return -1;
	}
	kp_data_catalog(d, scenarios, scenarios_size);
	map_key(d, res);
	return 0;
}

void kp_data_free(KpData *d)
{
	kp_host_shared_free(&d->shared);
	kp_palettes_free(&d->palettes);
	kp_campaign_free(&d->campaign);
	kp_dat_free(&d->dat);
	kp_ui_data_free(&d->ui);
	memset(d, 0, sizeof *d);
}

/* --- the catalog --- */

static KpDataMission *add_mission(KpData *d, const char *name, int catalog)
{
	KpDataMission *m;
	if (d->mission_count >= KP_DATA_MISSIONS) return NULL;
	m = &d->missions[d->mission_count++];
	memset(m, 0, sizeof *m);
	m->catalog = catalog;
	if (catalog) {
		snprintf(m->name, sizeof m->name, "%s", name);
		snprintf(m->key, sizeof m->key, "missions/%s", name);
	} else {
		snprintf(m->name, sizeof m->name, "scenarios/%s", name);
		snprintf(m->key, sizeof m->key, "scenarios/%s", name);
	}
	return m;
}

void kp_data_catalog(KpData *d, const char *scenarios, size_t scenarios_size)
{
	size_t at = 0;
	int index, k;
	d->mission_count = 0;
	/* the mapdata indices from 1 are the catalog's rows (index 0 is no mission) */
	for (index = 1; index < d->campaign.directory_count; index++) {
		char name[64];
		KpDataMission *m;
		if (!kp_campaign_mission_name(&d->campaign, index, name, sizeof name)) continue; /* a row with no scenario */
		if (!(m = add_mission(d, name, 1))) break;
		snprintf(m->directory, sizeof m->directory, "%s", d->campaign.directories[index]);
		m->index = index;
	}
	while (scenarios && at < scenarios_size) {
		char line[512], name[64], base[64], packs[KP_DATA_PACKS];
		size_t end = at, n;
		int tileset, w, h;
		KpDataMission *m;
		while (end < scenarios_size && scenarios[end] != '\n') end++;
		n = end - at < sizeof line - 1 ? end - at : sizeof line - 1;
		memcpy(line, scenarios + at, n);
		line[n] = '\0';
		at = end + 1;
		if (sscanf(line, "%63s %63s %d %d %d %159s", name, base, &tileset, &w, &h, packs) != 6) continue;
		if (!(m = add_mission(d, name, 0))) return;
		if (strcmp(packs, "-") != 0) snprintf(m->packs, sizeof m->packs, "%s", packs);
		for (k = 0; k < d->mission_count - 1; k++)
			if (d->missions[k].catalog && strcmp(d->missions[k].name, base) == 0) m->index = d->missions[k].index;
	}
}

const KpDataMission *kp_data_find(const KpData *d, const char *key)
{
	int k;
	for (k = 0; k < d->mission_count; k++)
		if (strcmp(d->missions[k].key, key) == 0) return &d->missions[k];
	return NULL;
}

int kp_data_mission(const KpData *d, const KpDataMission *entry, uint8_t *chk, size_t chk_size, KpMission *m,
	KpError *err)
{
	uint8_t packs[1 + KP_DATA_PACKS];
	int pack_size;
	if (entry->directory[0]) {
		free(chk);
		return kp_mission_from_files(m, &d->files, entry->directory, err);
	}
	if ((pack_size = kp_mission_packs_payload(entry->packs, packs, sizeof packs)) < 0) {
		free(chk);
		memset(m, 0, sizeof *m);
		snprintf(err->msg, sizeof err->msg, "mission: %s: malformed packs \"%s\"", entry->name, entry->packs);
		return -1;
	}
	return kp_mission_from_chk(m, &d->files, chk, chk_size, entry->name, NULL, packs, (size_t)pack_size, err);
}

/* --- custom maps (ADR 0024) --- */

static uint8_t *map_read(void *ctx, const char *name, size_t *size, char *err, size_t err_size)
{
	const KpDataMap *map = (const KpDataMap *)ctx;
	uint8_t *b;
	if (map->mpq) {
		err[0] = '\0';
		if ((b = kp_mpq_read(map->mpq, name, size, err, err_size)) != NULL || err[0]) return b;
	}
	return map->base->read(map->base->ctx, name, size, err, err_size);
}

static uint8_t *map_head(void *ctx, const char *name, size_t want, size_t *got, uint64_t *total, char *err,
	size_t err_size)
{
	const KpDataMap *map = (const KpDataMap *)ctx;
	uint8_t *b;
	size_t size;
	if (!(map->mpq && kp_mpq_has(map->mpq, name)) && map->base->head)
		return map->base->head(map->base->ctx, name, want, got, total, err, err_size);
	if (!(b = map_read(ctx, name, &size, err, err_size))) return NULL;
	*got = size < want ? size : want;
	*total = size;
	return b;
}

static void map_init(KpDataMap *map, const KpFiles *base, const char *name)
{
	memset(map, 0, sizeof *map);
	map->base = base;
	map->files.read = map_read;
	map->files.head = map_head;
	map->files.ctx = map;
	map->files.profile = base->profile;
	snprintf(map->name, sizeof map->name, "%s", name);
}

int kp_data_map_open(KpDataMap *map, const KpFiles *base, const char *path, KpError *err)
{
	const char *name = path + strlen(path);
	char why[KP_ERROR_SIZE];
	while (name > path && name[-1] != '/' && name[-1] != '\\') name--;
	map_init(map, base, name);
	if (!(map->mpq = kp_mpq_open(path, 0, why, sizeof why))) {
		snprintf(err->msg, sizeof err->msg, "map: %s: %s", map->name, why);
		return -1;
	}
	if (!kp_mpq_has(map->mpq, KP_DATA_MAP_SCENARIO)) {
		snprintf(err->msg, sizeof err->msg, "map: %s: no %s", map->name, KP_DATA_MAP_SCENARIO);
		kp_data_map_close(map);
		return -1;
	}
	return 0;
}

int kp_data_map_from_chk(KpDataMap *map, const KpFiles *base, uint8_t *chk, size_t chk_size, const char *name,
	KpError *err)
{
	map_init(map, base, name);
	if (!chk) {
		snprintf(err->msg, sizeof err->msg, "map: %s: out of memory", map->name);
		return -1;
	}
	map->chk = chk;
	map->chk_size = chk_size;
	return 0;
}

uint8_t *kp_data_map_chk(KpDataMap *map, size_t *size, KpError *err)
{
	char why[KP_ERROR_SIZE];
	uint8_t *b;
	if (!map->mpq) { /* a copy: a game started again (a load, a restart) builds its mission again */
		if (!(b = (uint8_t *)malloc(map->chk_size ? map->chk_size : 1))) {
			snprintf(err->msg, sizeof err->msg, "map: %s: out of memory", map->name);
			return NULL;
		}
		if (map->chk_size) memcpy(b, map->chk, map->chk_size);
		*size = map->chk_size;
		return b;
	}
	why[0] = '\0';
	if (!(b = kp_mpq_read(map->mpq, KP_DATA_MAP_SCENARIO, size, why, sizeof why)))
		snprintf(err->msg, sizeof err->msg, "map: %s: %s", map->name, why[0] ? why : "no scenario");
	return b;
}

int kp_data_map_mission(KpDataMap *map, KpChkLoad load, KpMission *m, KpError *err)
{
	uint8_t packs[1 + KP_DATA_PACKS];
	size_t chk_size;
	uint8_t *chk;
	int pack_size = kp_mission_packs_payload(KP_DATA_MAP_PACKS, packs, sizeof packs);
	memset(m, 0, sizeof *m);
	if (pack_size < 0 || !(chk = kp_data_map_chk(map, &chk_size, err))) return -1;
	return kp_mission_from_map(m, &map->files, chk, chk_size, map->name, load, packs, (size_t)pack_size, err);
}

void kp_data_map_close(KpDataMap *map)
{
	(void)kp_mpq_close(map->mpq);
	free(map->chk);
	memset(map, 0, sizeof *map);
}
