/* The replays of the command-line tools (kp_runner.h). */
#include "kp_runner.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Tileset file names by ERA & 7 (game/data/terrain.lua TILESETS). */
static const char *const TILESETS[8] = { "badlands", "platform", "install", "ashworld", "jungle", "desert", "ice", "twilight" };

uint8_t *kp_read_file(const char *path, size_t *size)
{
	FILE *f = fopen(path, "rb");
	uint8_t *data;
	long n;
	if (!f) return NULL;
	fseek(f, 0, SEEK_END);
	n = ftell(f);
	fseek(f, 0, SEEK_SET);
	data = (uint8_t *)malloc(n > 0 ? (size_t)n : 1);
	if (data && fread(data, 1, (size_t)n, f) != (size_t)n) {
		free(data);
		data = NULL;
	}
	*size = data ? (size_t)n : 0;
	fclose(f);
	return data;
}

static int fail(KpError *err, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(err->msg, sizeof err->msg, fmt, ap);
	va_end(ap);
	return -1;
}

static int hex_value(char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

/* The interface events of a replay (export_replay.lua) and their counts of values (a selection has any). */
enum { UI_SELECT, UI_MARK, UI_CURSOR, UI_ONSCREEN, UI_ALLIANCE, UI_PAUSED, UI_RELEASE, UI_SKIP, UI_OBJECTIVES, UI_KINDS };
static const char *const UI_WORDS[UI_KINDS] = {
	"select", "mark", "cursor", "onscreen", "alliance", "paused", "release", "skip", "objectives",
};
static const int UI_VALUES[UI_KINDS] = { -1, 3, 2, 2, 3, 0, 0, 0, 1 };

static int ui_kind(const char *word)
{
	int k;
	for (k = 0; k < UI_KINDS; k++) {
		if (strcmp(word, UI_WORDS[k]) == 0) return k;
	}
	return -1;
}

int kp_replay_parse(KpReplay *r, const char *text, size_t size, KpError *err)
{
	char *copy = (char *)malloc(size + 1), *line, *next;
	int lines = 0, n = 0, version = 0;
	size_t k, byte_total = 0;
	memset(r, 0, sizeof *r);
	if (!copy) return fail(err, "replay: out of memory");
	memcpy(copy, text, size);
	copy[size] = '\0';
	for (k = 0; k < size; k++) {
		if (copy[k] == '\n') lines++;
	}
	byte_total = size; /* hex digits bound the command bytes */
	r->commands = (KpReplayCommand *)calloc((size_t)lines + 1, sizeof *r->commands);
	r->ui = (KpReplayUi *)calloc((size_t)lines + 1, sizeof *r->ui);
	r->crcs = (KpReplayCrc *)calloc((size_t)lines + 1, sizeof *r->crcs);
	r->bytes = (uint8_t *)malloc(byte_total / 2 + 1);
	if (!r->commands || !r->ui || !r->crcs || !r->bytes) {
		free(copy);
		return fail(err, "replay: out of memory");
	}
	byte_total = 0;
	for (line = copy; line; line = next) { /* blank lines are skipped, not the end */
		char word[32], rest[1024];
		next = strchr(line, '\n');
		if (next) *next++ = '\0';
		n++;
		if (sscanf(line, "%31s", word) != 1) continue;
		if (strcmp(word, "KPRP") == 0) {
			if (sscanf(line, "KPRP %d", &version) != 1 || version != 1) goto bad;
		} else if (strcmp(word, "mission") == 0) {
			if (sscanf(line, "mission %511s", r->mission) != 1) goto bad;
		} else if (strcmp(word, "seed") == 0) {
			unsigned long v;
			if (sscanf(line, "seed %lu", &v) != 1) goto bad;
			r->seed = (uint32_t)v;
		} else if (strcmp(word, "expansion") == 0) {
			if (sscanf(line, "expansion %d", &r->expansion) != 1) goto bad;
		} else if (strcmp(word, "index") == 0) {
			long long v;
			if (sscanf(line, "index %lld", &v) != 1) goto bad;
			r->mission_index = v;
		} else if (strcmp(word, "last") == 0) {
			long long v;
			if (sscanf(line, "last %lld", &v) != 1) goto bad;
			r->last = v;
		} else if (strcmp(word, "command") == 0) {
			KpReplayCommand *c = &r->commands[r->command_count];
			long long turn, player;
			size_t h, len;
			if (sscanf(line, "command %lld %lld %1023s", &turn, &player, rest) != 3) goto bad;
			len = strlen(rest);
			if (len % 2 != 0) goto bad;
			c->turn = turn;
			c->command.player = player;
			c->command.bytes = r->bytes + byte_total;
			c->command.size = len / 2;
			for (h = 0; h < len; h += 2) {
				int hi = hex_value(rest[h]), lo = hex_value(rest[h + 1]);
				if (hi < 0 || lo < 0) goto bad;
				r->bytes[byte_total++] = (uint8_t)(hi * 16 + lo);
			}
			r->command_count++;
		} else if (ui_kind(word) >= 0) {
			KpReplayUi *u = &r->ui[r->ui_count];
			const char *p = line + strlen(word);
			long long v;
			int used;
			u->kind = ui_kind(word);
			if (sscanf(p, "%lld%n", &v, &used) != 1) goto bad;
			u->frame = v;
			p += used;
			while (sscanf(p, "%lld%n", &v, &used) == 1) {
				if (u->count >= KP_REPLAY_SELECT) goto bad;
				u->values[u->count++] = v;
				p += used;
			}
			if (u->count != UI_VALUES[u->kind] && u->kind != UI_SELECT) goto bad;
			r->ui_count++;
		} else if (strcmp(word, "crc") == 0) {
			KpReplayCrc *c = &r->crcs[r->crc_count];
			long long step;
			unsigned long crc;
			if (sscanf(line, "crc %lld %31s %lu", &step, c->category, &crc) != 3) goto bad;
			c->step = step;
			c->crc = (uint32_t)crc;
			r->crc_count++;
		} else {
			goto bad;
		}
	}
	free(copy);
	if (version != 1 || r->mission[0] == '\0') return fail(err, "replay: not a KPRP 1 replay");
	return 0;
bad:
	free(copy);
	return fail(err, "replay: bad line %d", n);
}

void kp_replay_free(KpReplay *r)
{
	free(r->commands);
	free(r->ui);
	free(r->crcs);
	free(r->bytes);
	memset(r, 0, sizeof *r);
}

static uint8_t *load(const char *root, const char *path, size_t *size, KpError *err)
{
	char full[1024];
	uint8_t *data;
	snprintf(full, sizeof full, "%s/%s", root, path);
	data = kp_read_file(full, size);
	if (!data) fail(err, "replay: cannot read %s", full);
	return data;
}

int kp_replay_game_start(KpReplayGame *rg, const char *root, const KpReplay *r, KpError *err)
{
	KpGameFiles f;
	KpStartInput in;
	char tileset[128];
	memset(rg, 0, sizeof *rg);
	memset(&f, 0, sizeof f);
	f.mission_name = r->mission;
	if (!(rg->files[0] = load(root, "generated/data/dat.kpd", &f.dat_size, err))) return -1;
	if (!(rg->files[1] = load(root, "generated/data/grpmeta.kpd", &f.grp_size, err))) return -1;
	if (!(rg->files[2] = load(root, "generated/data/iscript.kpd", &f.iscript_size, err))) return -1;
	if (!(rg->files[3] = load(root, r->mission, &f.mission_size, err))) return -1;
	f.dat = rg->files[0];
	f.grp = rg->files[1];
	f.iscript = rg->files[2];
	f.mission = rg->files[3];
	if (kp_game_data_load(&rg->data, &f, err) != 0) return -1;
	snprintf(tileset, sizeof tileset, "generated/data/tilesets/%s.kpd", TILESETS[rg->data.mission.era % 8]);
	if (!(rg->files[4] = load(root, tileset, &f.tileset_size, err))) return -1;
	f.tileset = rg->files[4];
	kp_game_seed(&rg->rng, &rg->crt, r->seed);
	kp_game_start_input(&in, &rg->data, &f, &rg->rng, &rg->crt, r->expansion);
	in.no_events = 1; /* no presentation */
	in.mission_index = r->mission_index;
	rg->g = kp_start(&in, err);
	return rg->g ? 0 : -1;
}

void kp_replay_game_free(KpReplayGame *rg)
{
	int k;
	if (rg->g) kp_game_free(rg->g);
	kp_game_data_free(&rg->data);
	for (k = 0; k < 5; k++) free(rg->files[k]);
	memset(rg, 0, sizeof *rg);
}

void kp_runner_init(KpRunner *run, KpGame *g, const KpReplay *r)
{
	memset(run, 0, sizeof *run);
	run->g = g;
	run->r = r;
	run->step = 1;
}

/* The commands of the turns up to the game's turn (oracle.lua runner turn_commands). */
static int turn_commands(KpRunner *run, KpCommand *list)
{
	KpGame *g = run->g;
	int count = 0;
	while (run->next_command < run->r->command_count && run->r->commands[run->next_command].turn <= kp_scalar(g, KP_TURN)) {
		if (count >= KP_TURN_COMMANDS) kp_game_fail(g, "replay: more than %d commands in a turn", KP_TURN_COMMANDS);
		list[count++] = run->r->commands[run->next_command++].command;
	}
	return count;
}

static void turn(KpRunner *run)
{
	KpCommand list[KP_TURN_COMMANDS];
	kp_run_turn(run->g, list, turn_commands(run, list));
}

static void paused_tick(KpRunner *run)
{
	KpCommand list[KP_TURN_COMMANDS];
	kp_paused_tick(run->g, list, turn_commands(run, list));
}

/* The interface events of the current frame, the turn and the frame count (oracle.lua runner loop_head). */
static void loop_head(KpRunner *run)
{
	KpGame *g = run->g;
	while (run->next_ui < run->r->ui_count && run->r->ui[run->next_ui].frame <= kp_scalar(g, KP_FRAME)) {
		const KpReplayUi *u = &run->r->ui[run->next_ui++];
		switch (u->kind) {
		case UI_SELECT: kp_select_locally(g, u->values, u->count); break;
		case UI_MARK: kp_mark_target(g, u->values[0], u->values[1], u->values[2]); break;
		case UI_CURSOR: kp_move_placement_cursor(g, u->values[0], u->values[1]); break;
		case UI_ONSCREEN:
			kp_refresh_terrain_onscreen(g, u->values[0], u->values[1], KP_SCREEN_TILES_W, KP_SCREEN_TILES_H);
			break;
		case UI_ALLIANCE: kp_set_alliance(g, u->values[0], u->values[1], u->values[2]); break;
		case UI_PAUSED: paused_tick(run); break;
		case UI_RELEASE: kp_trigger_release_wait(g); break;
		case UI_OBJECTIVES: kp_briefing_objectives(g, u->values[0]); break;
		default: kp_trigger_skip(g); break;
		}
	}
	turn(run);
	kp_scalar_set(g, KP_FRAME, kp_scalar(g, KP_FRAME) + 1);
}

void kp_runner_to(KpRunner *run, int64_t step)
{
	/* The briefing's objectives come before the game: the leading ones of the log, taken once by a run from the
	 * start (a run continued from a later step has already taken them). */
	if (run->step == 1 && !run->begun) {
		while (run->next_ui < run->r->ui_count && run->r->ui[run->next_ui].kind == UI_OBJECTIVES) {
			kp_briefing_objectives(run->g, run->r->ui[run->next_ui++].values[0]);
		}
		run->begun = 1;
	}
	while (run->step < step) {
		if (run->step == 1) {
			kp_step_objects(run->g);
		} else if (run->step == 2) {
			kp_step_objects(run->g);
			turn(run);
			loop_head(run);
		} else {
			kp_step_frame(run->g);
			loop_head(run);
		}
		run->step++;
	}
}
