/* A host's native game (kp_host.h). */
#include "kp_host.h"

#include "kp_chk.h"
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

/* The CRC-32s of the shared structures' blobs: the saves' data identity. */
static void shared_crcs(KpHostShared *sh)
{
	sh->grp_crc = kp_crc32(sh->data.grp.blob, sh->data.grp.size);
	sh->script_crc = kp_crc32(sh->data.script.blob, sh->data.script.size);
	sh->ai_crc = kp_crc32(sh->data.ai.blob, sh->data.ai.size);
}

int kp_host_shared_from_files(KpHostShared *sh, const KpFiles *files, const KpDat *dat, KpError *err)
{
	memset(sh, 0, sizeof *sh);
	if (kp_grpmeta_from_files(&sh->data.grp, files, dat, err) != 0 || kp_iscript_from_files(&sh->data.script, files, err) != 0
		|| kp_ai_bins_from_files(&sh->data.ai, files, err) != 0) {
		kp_host_shared_free(sh);
		return -1;
	}
	shared_crcs(sh);
	return 0;
}

void kp_host_shared_free(KpHostShared *sh)
{
	kp_game_shared_free(&sh->data);
	memset(sh, 0, sizeof *sh);
}

/* hg emptied for a game under name (NULL: "mission") over sh: 0, or -1 with the error for a name too long. */
static int game_begin(KpHostGame *hg, const KpHostShared *sh, const char *name, KpError *err)
{
	memset(hg, 0, sizeof *hg);
	if (!name) name = "mission";
	if (strlen(name) >= sizeof hg->name) {
		snprintf(err->msg, sizeof err->msg, "game: a name of %d bytes or more", KP_SAVE_NAME);
		return -1;
	}
	snprintf(hg->name, sizeof hg->name, "%s", name);
	hg->shared = sh;
	return 0;
}

int kp_host_game_load_from(KpHostGame *hg, const KpHostShared *sh, const KpDat *dat, KpMission *mission,
	const char *name, KpError *err)
{
	if (game_begin(hg, sh, name, err) != 0) {
		kp_mission_free(mission);
		return -1;
	}
	if (kp_game_data_from(&hg->data, &sh->data, dat, mission, err) != 0) {
		kp_host_game_free(hg);
		return -1;
	}
	hg->dat_crc = kp_crc32(dat->blob, dat->size);
	return 0;
}

int kp_host_game_tileset_files(KpHostGame *hg, const KpFiles *files, KpError *err)
{
	char path[64];
	const char *name = KP_TILESET_NAMES[hg->data.mission.tileset];
	free(hg->cv5);
	free(hg->vf4);
	hg->cv5 = hg->vf4 = NULL;
	hg->cv5_size = hg->vf4_size = 0;
	snprintf(path, sizeof path, "tileset\\%s.cv5", name);
	if (!(hg->cv5 = kp_files_need(files, path, &hg->cv5_size, err))) return -1;
	snprintf(path, sizeof path, "tileset\\%s.vf4", name);
	if (!(hg->vf4 = kp_files_need(files, path, &hg->vf4_size, err))) return -1;
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

/* The language's TIME over one section of trigger records (TRIG or MBRF; the Python converter's rule until stage C2,
 * tools/research/sc_emulator/speech.py's for the original's game): an action alike but for its TIME takes the
 * language's; one that differs in more keeps the mission's whole, and a record whose conditions, flags or actions
 * differ in more is counted in out (the first named). The actions whose TIME changed. */
static int time_merged(uint8_t *own, const uint8_t *theirs, size_t size, KpHostSpeech *out, const char *tag)
{
	const size_t tail = KP_TRIG_ACTIONS_AT + (size_t)KP_TRIG_ACTIONS * KP_TRIG_ACTION_SIZE;
	size_t r;
	int k, changed = 0;
	for (r = 0; r + KP_TRIGGER_RECORD <= size; r += KP_TRIGGER_RECORD) {
		uint8_t *rec = own + r;
		const uint8_t *their = theirs + r;
		int differs = memcmp(rec, their, KP_TRIG_ACTIONS_AT) != 0 || memcmp(rec + tail, their + tail, KP_TRIGGER_RECORD - tail) != 0;
		for (k = 0; k < KP_TRIG_ACTIONS; k++) {
			size_t at = KP_TRIG_ACTIONS_AT + (size_t)k * KP_TRIG_ACTION_SIZE, time = at + KP_ACTION_TIME;
			if (memcmp(rec + at, their + at, KP_ACTION_TIME) != 0
				|| memcmp(rec + time + 4, their + time + 4, KP_TRIG_ACTION_SIZE - KP_ACTION_TIME - 4) != 0) {
				differs = 1;
			} else if (memcmp(rec + time, their + time, 4) != 0) {
				memcpy(rec + time, their + time, 4);
				changed++;
			}
		}
		if (differs && out->differ++ == 0) snprintf(out->first, sizeof out->first, "%s record %lld", tag, (long long)(r / KP_TRIGGER_RECORD));
	}
	return changed;
}

int kp_host_game_speech(KpHostGame *hg, const KpFiles *raw, const char *locale, const char *mission, KpHostSpeech *out,
	KpError *err)
{
	char path[KP_ERROR_SIZE], why[KP_ERROR_SIZE], spec[KP_ERROR_SIZE];
	KpBytes *triggers = &hg->data.mission.triggers, *briefing = &hg->data.mission.briefing;
	const KpChkSection *trig, *mbrf;
	KpChk chk;
	uint8_t *bytes, *merged = NULL;
	size_t size = 0;
	memset(out, 0, sizeof *out);
	snprintf(path, sizeof path, KP_HOST_SPEECH_MAP, locale, mission);
	if (!(bytes = raw->read(raw->ctx, path, &size, why, sizeof why))) {
		if (!why[0]) return 0;
		snprintf(err->msg, sizeof err->msg, "%s", why);
		return -1;
	}
	if (kp_chk_read(&chk, bytes, size, raw->profile, 1, why, sizeof why) != 0) {
		snprintf(err->msg, sizeof err->msg, "%s: %s", path, why);
	} else if (!(trig = kp_chk_section(&chk, "TRIG")) || !(mbrf = kp_chk_section(&chk, "MBRF"))
		|| trig->size != triggers->size || mbrf->size != briefing->size) {
		snprintf(err->msg, sizeof err->msg, "%s: no TRIG and MBRF of the mission's sizes", path);
	} else if (!(merged = (uint8_t *)malloc(triggers->size + briefing->size + 1))) {
		snprintf(err->msg, sizeof err->msg, "%s: out of memory", path);
	} else {
		memcpy(merged, triggers->data, triggers->size);
		memcpy(merged + triggers->size, briefing->data, briefing->size);
		out->times = time_merged(merged, trig->data, trig->size, out, "TRIG");
		out->times += time_merged(merged + triggers->size, mbrf->data, mbrf->size, out, "MBRF");
		snprintf(spec, sizeof spec, "%s %s", locale, mission);
		out->excepted = kp_defs_names_has(raw->profile, "SPEX", spec);
		triggers->data = merged;
		briefing->data = merged + triggers->size;
		free(hg->speech);
		hg->speech = merged;
	}
	free(bytes);
	return merged ? 1 : -1;
}

int kp_host_game_start(KpHostGame *hg, uint32_t seed, int expansion, const KpHostOpts *opts, KpError *err)
{
	KpHostOpts defaults;
	KpStartInput in;
	const KpGameData *d = &hg->data;
	uint32_t tileset_crc;
	if (!opts) {
		kp_host_opts(&defaults);
		opts = &defaults;
	}
	if (!hg->cv5 || !hg->vf4) {
		snprintf(err->msg, sizeof err->msg, "game: no tileset");
		kp_host_game_free(hg);
		return -1;
	}
	/* the tileset's part: the CV5's and the VF4's bytes one after the other */
	tileset_crc = kp_crc32_update(kp_crc32(hg->cv5, hg->cv5_size), hg->vf4, hg->vf4_size);
	snprintf(hg->identity, sizeof hg->identity, "%08x-%08x-%08x-%08x-%08x-%08x", (unsigned)hg->dat_crc,
		(unsigned)hg->shared->grp_crc, (unsigned)hg->shared->script_crc, (unsigned)tileset_crc,
		(unsigned)kp_crc32(d->mission.blob, d->mission.size), (unsigned)hg->shared->ai_crc);
	kp_game_seed(&hg->rng, &hg->crt, seed);
	kp_game_start_input(&in, &hg->data, &hg->rng, &hg->crt, expansion);
	in.cv5 = hg->cv5;
	in.cv5_size = hg->cv5_size;
	in.vf4 = hg->vf4;
	in.vf4_size = hg->vf4_size;
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
	free(hg->cv5);
	free(hg->vf4);
	free(hg->text);
	free(hg->speech);
	memset(hg, 0, sizeof *hg);
}
