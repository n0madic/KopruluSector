/* A host's native game (kp_host.h). */
#include "kp_host.h"

#include "kp_game_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint8_t *kp_read_file(const char *path, size_t *size)
{
	FILE *f = fopen(path, "rb");
	uint8_t *data = NULL;
	long n;
	*size = 0;
	if (!f) return NULL;
	if (fseek(f, 0, SEEK_END) == 0 && (n = ftell(f)) >= 0 && fseek(f, 0, SEEK_SET) == 0) {
		data = (uint8_t *)malloc((size_t)n + 1);
		if (data && fread(data, 1, (size_t)n, f) == (size_t)n) {
			data[n] = 0;
			*size = (size_t)n;
		} else {
			free(data);
			data = NULL;
		}
	}
	fclose(f);
	return data;
}

void kp_host_opts(KpHostOpts *o)
{
	memset(o, 0, sizeof *o);
	o->speed = KP_NIL;
}

int kp_host_shared_load(KpHostShared *sh, const uint8_t *grp, size_t grp_size, const uint8_t *iscript,
	size_t iscript_size, const uint8_t *aiscript, size_t aiscript_size, KpError *err)
{
	memset(sh, 0, sizeof *sh);
	if (kp_game_shared_load(&sh->data, grp, grp_size, iscript, iscript_size, aiscript, aiscript_size, err) != 0) return -1;
	sh->grp_crc = kp_crc32(sh->data.grp.blob, sh->data.grp.size);
	sh->script_crc = kp_crc32(sh->data.script.blob, sh->data.script.size);
	sh->ai_crc = kp_crc32(sh->data.ai.blob, sh->data.ai.size);
	return 0;
}

void kp_host_shared_free(KpHostShared *sh)
{
	kp_game_shared_free(&sh->data);
	memset(sh, 0, sizeof *sh);
}

int kp_host_game_load(KpHostGame *hg, const KpHostShared *sh, const uint8_t *dat, size_t dat_size, uint8_t *mission,
	size_t mission_size, const char *name, KpError *err)
{
	memset(hg, 0, sizeof *hg);
	if (!name) name = "mission";
	if (strlen(name) >= sizeof hg->name) {
		snprintf(err->msg, sizeof err->msg, "game: a name of %d bytes or more", KP_SAVE_NAME);
		free(mission);
		return -1;
	}
	snprintf(hg->name, sizeof hg->name, "%s", name);
	hg->shared = sh;
	if (kp_game_data_load(&hg->data, &sh->data, dat, dat_size, mission, mission_size, hg->name, err) != 0) {
		kp_host_game_free(hg);
		return -1;
	}
	hg->dat_crc = kp_crc32(dat, dat_size);
	return 0;
}

/* A language's overlay of a mission parsed into c: 0, else -1 with the error and the overlay freed (it is damaged, or of
 * another kind or schema than a mission). */
static int overlay_parse(KpKpf *c, uint8_t *overlay, size_t size, const char *what, KpError *err)
{
	if (kp_kpf_parse(c, overlay, size, what, 1, err) != 0) {
		free(overlay);
		return -1;
	}
	if (memcmp(c->kind, KP_MISSION_KIND, 4) != 0 || c->schema_version != KP_MISSION_SCHEMA) {
		snprintf(err->msg, sizeof err->msg, "%s: another kind or schema than a mission's", what);
		kp_kpf_free(c);
		free(overlay);
		return -1;
	}
	return 0;
}

int kp_host_game_text(KpHostGame *hg, uint8_t *overlay, size_t size, KpError *err)
{
	KpKpf c;
	const KpKpfSection *strs;
	KpBytes *own = &hg->data.mission.strings;
	int rc = -1;
	if (overlay_parse(&c, overlay, size, "mission text", err) != 0) return -1;
	strs = kp_kpf_find(&c, "STRS");
	if (strs == NULL || strs->size < 2 || own->size < 2 || kp_le_u16(strs->data) != kp_le_u16(own->data)) {
		snprintf(err->msg, sizeof err->msg, "mission text: no STRS or another string count than the mission's");
	} else {
		own->data = strs->data;
		own->size = strs->size;
		free(hg->text);
		hg->text = overlay;
		rc = 0;
	}
	kp_kpf_free(&c);
	if (rc != 0) free(overlay);
	return rc;
}

/* Whether a speech's records are the mission's but for their actions' TIME (converter speech changes nothing else):
 * the index of the first record that differs in more, -1 none. */
static int64_t differs_in_more_than_time(const uint8_t *own, const uint8_t *theirs, size_t size)
{
	size_t r;
	int k;
	for (r = 0; r + KP_TRIGGER_RECORD <= size; r += KP_TRIGGER_RECORD) {
		size_t at = r;
		for (k = 0; k < KP_TRIG_ACTIONS; k++) {
			size_t time = r + KP_TRIG_ACTIONS_AT + (size_t)k * KP_TRIG_ACTION_SIZE + KP_ACTION_TIME;
			if (memcmp(own + at, theirs + at, time - at) != 0) return (int64_t)(r / KP_TRIGGER_RECORD);
			at = time + 4; /* the u32 TIME */
		}
		if (memcmp(own + at, theirs + at, r + KP_TRIGGER_RECORD - at) != 0) return (int64_t)(r / KP_TRIGGER_RECORD);
	}
	return -1;
}

int kp_host_game_speech(KpHostGame *hg, uint8_t *overlay, size_t size, KpError *err)
{
	KpKpf c;
	const KpKpfSection *trig, *mbrf;
	KpBytes *triggers = &hg->data.mission.triggers, *briefing = &hg->data.mission.briefing;
	int64_t trig_at = -1, mbrf_at = -1;
	int rc = -1;
	if (overlay_parse(&c, overlay, size, "mission speech", err) != 0) return -1;
	trig = kp_kpf_find(&c, "TRIG");
	mbrf = kp_kpf_find(&c, "MBRF");
	if (trig == NULL || mbrf == NULL || trig->size != triggers->size || mbrf->size != briefing->size) {
		snprintf(err->msg, sizeof err->msg, "mission speech: no TRIG and MBRF of the mission's sizes");
	} else if ((trig_at = differs_in_more_than_time(triggers->data, trig->data, trig->size)) >= 0
		|| (mbrf_at = differs_in_more_than_time(briefing->data, mbrf->data, mbrf->size)) >= 0) {
		snprintf(err->msg, sizeof err->msg, "mission speech: %s record %lld differs from the mission's in more than TIME "
			"(the speech is of other data)", trig_at >= 0 ? "TRIG" : "MBRF", (long long)(trig_at >= 0 ? trig_at : mbrf_at));
	} else {
		triggers->data = trig->data;
		briefing->data = mbrf->data;
		free(hg->speech);
		hg->speech = overlay;
		rc = 0;
	}
	kp_kpf_free(&c);
	if (rc != 0) free(overlay);
	return rc;
}

void kp_host_tileset_path(const KpHostGame *hg, char *out, size_t size)
{
	snprintf(out, size, KP_TILESET_PATH, KP_TILESET_NAMES[hg->data.mission.tileset]);
}

int kp_host_game_start(KpHostGame *hg, uint8_t *tileset, size_t tileset_size, uint32_t seed, int expansion,
	const KpHostOpts *opts, KpError *err)
{
	KpHostOpts defaults;
	KpStartInput in;
	const KpGameData *d = &hg->data;
	if (!opts) {
		kp_host_opts(&defaults);
		opts = &defaults;
	}
	free(hg->tileset);
	hg->tileset = tileset;
	hg->tileset_size = tileset_size;
	if (!tileset) {
		snprintf(err->msg, sizeof err->msg, "game: no tileset");
		kp_host_game_free(hg);
		return -1;
	}
	snprintf(hg->identity, sizeof hg->identity, "%08x-%08x-%08x-%08x-%08x-%08x", (unsigned)hg->dat_crc,
		(unsigned)hg->shared->grp_crc, (unsigned)hg->shared->script_crc, (unsigned)kp_crc32(tileset, tileset_size),
		(unsigned)kp_crc32(d->mission.blob, d->mission.size), (unsigned)hg->shared->ai_crc);
	kp_game_seed(&hg->rng, &hg->crt, seed);
	kp_game_start_input(&in, &hg->data, tileset, tileset_size, &hg->rng, &hg->crt, expansion);
	in.no_units = opts->no_units;
	in.no_events = !opts->events && !opts->trace;
	in.trace_triggers = opts->trace;
	in.mission_index = opts->mission_index;
	in.cheats = opts->cheats;
	in.carry = opts->carry;
	in.turn = opts->turn;
	if (opts->speed != KP_NIL) in.speed = opts->speed;
	hg->g = kp_start(&in, err);
	if (!hg->g) {
		kp_host_game_free(hg);
		return -1;
	}
	hg->g->ctx = hg;
	return 0;
}

void kp_host_game_free(KpHostGame *hg)
{
	if (hg->g) kp_game_free(hg->g);
	kp_game_data_free(&hg->data);
	free(hg->tileset);
	free(hg->text);
	free(hg->speech);
	memset(hg, 0, sizeof *hg);
}
