/* The oracle runs of the native game (kp_runner.h). */
#include "kp_runner.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kp_data.h"
#include "kp_json.h"

#define BYTE_BLOCK 65536

int kp_error_set(KpError *err, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(err->msg, sizeof err->msg, fmt, ap);
	va_end(ap);
	return -1;
}

void kp_out_of_memory(void)
{
	fprintf(stderr, "oracle: out of memory\n");
	abort();
}

void *kp_grow(void *data, int count, int *cap, size_t size)
{
	if (count >= *cap) {
		int n = *cap ? *cap * 2 : 64;
		data = realloc(data, (size_t)n * size);
		if (!data) kp_out_of_memory();
		*cap = n;
	}
	return data;
}

static void join(char *full, size_t size, const char *root, const char *path)
{
	if (path[0] == '/') snprintf(full, size, "%s", path); /* an absolute path stays */
	else snprintf(full, size, "%s/%s", root, path);
}

uint8_t *kp_read_under(const char *root, const char *path, size_t *size, KpError *err)
{
	char full[1024];
	uint8_t *data;
	join(full, sizeof full, root, path);
	data = kp_read_file(full, size);
	if (!data) kp_error_set(err, "oracle: cannot read %s", full);
	return data;
}

static int hex_value(char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

/* Hex digits into bytes (size / 2 of them); -1 for an odd count or a bad digit. */
static int unhex(const char *hex, size_t size, uint8_t *out)
{
	size_t h;
	if (size % 2 != 0) return -1;
	for (h = 0; h < size; h += 2) {
		int hi = hex_value(hex[h]), lo = hex_value(hex[h + 1]);
		if (hi < 0 || lo < 0) return -1;
		out[h / 2] = (uint8_t)(hi * 16 + lo);
	}
	return 0;
}

static const char *const RUN_WORDS[KP_RUN_KINDS] = {
	"select", "mark", "cursor", "onscreen", "alliance", "paused", "release", "skip", "objectives", "crt", "placement",
	"srand", "critter", "menu_pause",
};
/* -1: a selection's units (any count), a placement's order and type or none when it closes (0 or 2) */
static const int RUN_VALUES[KP_RUN_KINDS] = { -1, 3, 2, 2, 3, 0, 0, 0, 1, 0, -1, 1, 1, 1 };

int kp_run_kind(const char *word)
{
	int k;
	for (k = 0; k < KP_RUN_KINDS; k++) {
		if (strcmp(word, RUN_WORDS[k]) == 0) return k;
	}
	return -1;
}

const char *kp_run_word(int kind) { return kind >= 0 && kind < KP_RUN_KINDS ? RUN_WORDS[kind] : "?"; }

int kp_run_values(int kind) { return kind >= 0 && kind < KP_RUN_KINDS ? RUN_VALUES[kind] : 0; }

/* --- replays --- */

void kp_replay_init(KpReplay *r) { memset(r, 0, sizeof *r); }

void kp_replay_clear_commands(KpReplay *r)
{
	int k;
	free(r->commands);
	for (k = 0; k < r->block_count; k++) free(r->byte_blocks[k]);
	free(r->byte_blocks);
	r->commands = NULL;
	r->byte_blocks = NULL;
	r->command_count = r->command_cap = r->block_count = r->block_cap = 0;
	r->block_used = 0;
}

void kp_replay_free(KpReplay *r)
{
	kp_replay_clear_commands(r);
	free(r->ui);
	free(r->crcs);
	memset(r, 0, sizeof *r);
}

/* Room for n command bytes in the replay's current block (a new block when they do not fit). */
static uint8_t *command_bytes(KpReplay *r, size_t n)
{
	uint8_t *at;
	if (!r->block_count || r->block_used + n > BYTE_BLOCK) {
		uint8_t *block = (uint8_t *)malloc(n > BYTE_BLOCK ? n : BYTE_BLOCK);
		if (!block) kp_out_of_memory();
		r->byte_blocks = (uint8_t **)kp_grow(r->byte_blocks, r->block_count, &r->block_cap, sizeof *r->byte_blocks);
		r->byte_blocks[r->block_count++] = block;
		r->block_used = 0;
	}
	at = r->byte_blocks[r->block_count - 1] + r->block_used;
	r->block_used += n;
	return at;
}

void kp_replay_command(KpReplay *r, int64_t turn, int64_t player, const uint8_t *bytes, size_t size)
{
	KpReplayCommand *c;
	uint8_t *copy = command_bytes(r, size ? size : 1);
	if (size) memcpy(copy, bytes, size);
	r->commands = (KpReplayCommand *)kp_grow(r->commands, r->command_count, &r->command_cap, sizeof *r->commands);
	c = &r->commands[r->command_count++];
	memset(c, 0, sizeof *c);
	c->turn = turn;
	c->command.player = player;
	c->command.bytes = copy;
	c->command.size = size;
}

KpReplayUi *kp_replay_ui(KpReplay *r, int kind, int64_t frame)
{
	KpReplayUi *u;
	r->ui = (KpReplayUi *)kp_grow(r->ui, r->ui_count, &r->ui_cap, sizeof *r->ui);
	u = &r->ui[r->ui_count++];
	memset(u, 0, sizeof *u);
	u->kind = kind;
	u->frame = frame;
	return u;
}

void kp_replay_crc(KpReplay *r, int64_t step, const char *category, uint32_t crc)
{
	KpReplayCrc *c;
	r->crcs = (KpReplayCrc *)kp_grow(r->crcs, r->crc_count, &r->crc_cap, sizeof *r->crcs);
	c = &r->crcs[r->crc_count++];
	memset(c, 0, sizeof *c);
	c->step = step;
	snprintf(c->category, sizeof c->category, "%s", category);
	c->crc = crc;
}

int kp_replay_parse(KpReplay *r, const char *text, size_t size, KpError *err)
{
	char *copy = (char *)malloc(size + 1), *line, *next;
	int n = 0, version = 0;
	kp_replay_init(r);
	if (!copy) return kp_error_set(err, "replay: out of memory");
	memcpy(copy, text, size);
	copy[size] = '\0';
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
			long long turn, player;
			uint8_t bytes[512];
			size_t len;
			if (sscanf(line, "command %lld %lld %1023s", &turn, &player, rest) != 3) goto bad;
			len = strlen(rest);
			if (len / 2 > sizeof bytes || unhex(rest, len, bytes) != 0) goto bad;
			kp_replay_command(r, turn, player, bytes, len / 2);
		} else if (kp_run_kind(word) >= 0) {
			int kind = kp_run_kind(word), used;
			const char *p = line + strlen(word);
			long long v;
			KpReplayUi *u;
			if (sscanf(p, "%lld%n", &v, &used) != 1) goto bad;
			u = kp_replay_ui(r, kind, v);
			p += used;
			while (sscanf(p, "%lld%n", &v, &used) == 1) {
				if (u->count >= KP_REPLAY_SELECT) goto bad;
				u->values[u->count++] = v;
				p += used;
			}
			if (u->count != RUN_VALUES[kind] && RUN_VALUES[kind] >= 0) goto bad;
			if (kind == KP_RUN_PLACEMENT && u->count != 0 && u->count != 2) goto bad;
		} else if (strcmp(word, "crc") == 0) {
			long long step;
			unsigned long crc;
			char category[32];
			if (sscanf(line, "crc %lld %31s %lu", &step, category, &crc) != 3) goto bad;
			kp_replay_crc(r, step, category, (uint32_t)crc);
		} else {
			goto bad;
		}
	}
	free(copy);
	if (version != 1 || r->mission[0] == '\0') return kp_error_set(err, "replay: not a KPRP 1 replay");
	return 0;
bad:
	free(copy);
	return kp_error_set(err, "replay: bad line %d", n);
}

/* Calls each(line, ud) on every non-blank line of a JSON lines text; stops at a failure (-1). */
static int each_json_line(const char *text, size_t size, int (*each)(const KpJson *v, void *ud, KpError *err),
	void *ud, KpError *err)
{
	size_t at = 0;
	int n = 0;
	while (at < size) {
		size_t end = at;
		KpJsonDoc doc;
		KpJson *v;
		char why[160];
		int r;
		while (end < size && text[end] != '\n') end++;
		n++;
		if (end > at && !(end - at == 1 && text[at] == '\r')) {
			kp_json_doc_init(&doc);
			v = kp_json_parse(&doc, text + at, end - at, why, sizeof why);
			r = v ? each(v, ud, err) : kp_error_set(err, "line %d: %s", n, why);
			kp_json_doc_free(&doc);
			if (r != 0) return -1;
		}
		at = end + 1;
	}
	return 0;
}

int kp_replay_add_command_json(KpReplay *r, const KpJson *v, KpError *err)
{
	const char *hex = kp_json_field_str(v, "bytes", "");
	size_t len = strlen(hex);
	uint8_t bytes[512];
	if (len / 2 > sizeof bytes || unhex(hex, len, bytes) != 0) return kp_error_set(err, "commands: bad bytes %s", hex);
	kp_replay_command(r, kp_json_field_int(v, "turn", 0), kp_json_field_int(v, "player", 0), bytes, len / 2);
	return 0;
}

static int command_line(const KpJson *v, void *ud, KpError *err) { return kp_replay_add_command_json((KpReplay *)ud, v, err); }

int kp_replay_add_commands_jsonl(KpReplay *r, const char *text, size_t size, KpError *err)
{
	return each_json_line(text, size, command_line, r, err);
}

/* The values of an array member into u (at most KP_REPLAY_SELECT). */
static void array_values(KpReplayUi *u, const KpJson *a)
{
	int k;
	for (k = 0; k < kp_json_len(a) && u->count < KP_REPLAY_SELECT; k++) u->values[u->count++] = kp_json_int(kp_json_at(a, k), 0);
}

void kp_replay_add_ui_json(KpReplay *r, const KpJson *v)
{
	int64_t frame = kp_json_field_int(v, "frame", 0);
	const KpJson *x;
	KpReplayUi *u;
	if ((x = kp_json_get(v, "select"))) {
		array_values(kp_replay_ui(r, KP_RUN_SELECT, frame), x);
	} else if ((x = kp_json_get(v, "alliance"))) {
		array_values(kp_replay_ui(r, KP_RUN_ALLIANCE, frame), x);
	} else if ((x = kp_json_get(v, "onscreen"))) {
		u = kp_replay_ui(r, KP_RUN_ONSCREEN, frame);
		array_values(u, x);
		u->count = 2;
	} else if ((x = kp_json_get(v, "objectives"))) {
		kp_replay_ui(r, KP_RUN_OBJECTIVES, frame)->values[0] = kp_json_int(x, 0);
		r->ui[r->ui_count - 1].count = 1;
	} else if ((x = kp_json_get(v, "menu_pause"))) {
		kp_replay_ui(r, KP_RUN_MENU_PAUSE, frame)->values[0] = kp_json_int(x, 0);
		r->ui[r->ui_count - 1].count = 1;
	} else if (kp_json_get(v, "paused")) {
		kp_replay_ui(r, KP_RUN_PAUSED, frame);
	} else if (kp_json_get(v, "release")) {
		kp_replay_ui(r, KP_RUN_RELEASE, frame);
	} else if (kp_json_get(v, "skip")) {
		kp_replay_ui(r, KP_RUN_SKIP, frame);
	} else if ((x = kp_json_get(v, "critter"))) {
		kp_replay_ui(r, KP_RUN_CRITTER, frame)->values[0] = kp_json_int(x, 0);
		r->ui[r->ui_count - 1].count = 1;
	} else if (kp_json_get(v, "crt")) {
		kp_replay_ui(r, KP_RUN_CRT, frame);
	} else if ((x = kp_json_get(v, "srand"))) {
		kp_replay_ui(r, KP_RUN_SRAND, frame)->values[0] = kp_json_int(x, 0);
		r->ui[r->ui_count - 1].count = 1;
	} else if ((x = kp_json_get(v, "placement"))) {
		u = kp_replay_ui(r, KP_RUN_PLACEMENT, frame);
		if (kp_json_len(x) >= 2) array_values(u, x);
		u->count = u->count >= 2 ? 2 : 0;
	} else if ((x = kp_json_get(v, "mark"))) {
		u = kp_replay_ui(r, KP_RUN_MARK, frame);
		array_values(u, x);
		u->count = 3;
	} else {
		u = kp_replay_ui(r, KP_RUN_CURSOR, frame);
		array_values(u, kp_json_get(v, "cursor"));
		u->count = 2;
	}
}

static int ui_entry(const KpJson *v, void *ud, KpError *err)
{
	(void)err;
	kp_replay_add_ui_json((KpReplay *)ud, v);
	return 0;
}

int kp_replay_add_ui_jsonl(KpReplay *r, const char *text, size_t size, KpError *err)
{
	return each_json_line(text, size, ui_entry, r, err);
}

/* --- the games --- */

void kp_oracle_opts(KpOracleOpts *o)
{
	memset(o, 0, sizeof *o);
	kp_host_opts(&o->host);
	o->host.mission_index = KP_NIL;
}

/* The original data under a root (kp_data_dir: the root itself when it is a source, else its repack), opened and
 * built once per root and kept for the process: its games point into it. */
typedef struct DataRoot {
	struct DataRoot *next;
	char root[256];
	KpRes *res;
	KpData data;
	uint8_t *fonts[KP_DATA_VECTOR_FONTS];
} DataRoot;

static DataRoot *roots; /* the roots opened, kept for the process */

static int data_root_open(DataRoot *r, const char *root, KpError *err)
{
	KpResConfig config;
	KpUiFile fonts[KP_DATA_VECTOR_FONTS];
	char dir[600], why[600];
	const char *dirs[1];
	uint8_t *scenarios;
	size_t size, scenarios_size = 0;
	int k, count = 0, rc;
	if (!kp_data_dir(root, dir, sizeof dir)) {
		return kp_error_set(err, "oracle: no original data under %s (a repack: make repack; an install)", root);
	}
	dirs[0] = dir;
	memset(&config, 0, sizeof config);
	if (!(r->res = kp_res_open(dirs, 1, &config, why, sizeof why))) return kp_error_set(err, "oracle: %s", why);
	for (k = 0; k < KP_DATA_VECTOR_FONTS; k++) { /* the source tree's (an absent one: the text drawn without it) */
		char path[128];
		snprintf(path, sizeof path, "third_party/fonts/%s", KP_DATA_VECTOR_FONT_FILES[k][1]);
		if (!(r->fonts[k] = kp_read_under(root, path, &size, err))) continue;
		fonts[count].path = KP_DATA_VECTOR_FONT_FILES[k][0];
		fonts[count].path_len = strlen(KP_DATA_VECTOR_FONT_FILES[k][0]);
		fonts[count].data = r->fonts[k];
		fonts[count++].size = size;
	}
	scenarios = kp_read_under(root, KP_DATA_SCENARIOS "index.txt", &scenarios_size, err);
	rc = kp_data_build(&r->data, r->res, fonts, count, (const char *)scenarios, scenarios ? scenarios_size : 0, err);
	free(scenarios);
	return rc;
}

const KpData *kp_oracle_data(const char *root, KpError *err)
{
	DataRoot *r;
	int k;
	for (r = roots; r; r = r->next) {
		if (strcmp(r->root, root) == 0) return &r->data;
	}
	if (!(r = (DataRoot *)calloc(1, sizeof *r))) kp_out_of_memory();
	snprintf(r->root, sizeof r->root, "%s", root);
	if (data_root_open(r, root, err) != 0) {
		if (r->res) kp_res_close(r->res);
		for (k = 0; k < KP_DATA_VECTOR_FONTS; k++) free(r->fonts[k]);
		free(r);
		return NULL;
	}
	r->next = roots;
	roots = r;
	return &r->data;
}

KpResSourceKind kp_oracle_data_origin(const char *root)
{
	KpError err;
	DataRoot *r;
	if (!kp_oracle_data(root, &err)) return KP_RES_SOURCE_NONE;
	for (r = roots; strcmp(r->root, root) != 0; r = r->next) {
	}
	return kp_res_origin(r->res);
}

int64_t kp_oracle_mission_index(const char *root, const char *key)
{
	KpError err;
	const KpData *d = kp_oracle_data(root, &err);
	const KpDataMission *m = d ? kp_data_find(d, key) : NULL;
	return m ? m->index : 0;
}

/* The files a speech is read from (Remastered's names: kp_res_raw_files): the data root's, or (dir not NULL) those of
 * the source in dir (an install, a repack with the language's group), opened once per process. */
static const KpFiles *speech_files(const KpData *d, const char *dir, KpError *err)
{
	static struct SpeechSource {
		struct SpeechSource *next;
		char dir[512];
		KpRes *res;
		KpFiles raw;
	} *sources;
	struct SpeechSource *s;
	KpResConfig config;
	char why[600];
	const char *dirs[1];
	if (!dir || !dir[0]) return &d->raw;
	for (s = sources; s; s = s->next) {
		if (strcmp(s->dir, dir) == 0) return &s->raw;
	}
	if (!(s = (struct SpeechSource *)calloc(1, sizeof *s))) kp_out_of_memory();
	snprintf(s->dir, sizeof s->dir, "%s", dir);
	dirs[0] = dir;
	memset(&config, 0, sizeof config);
	if (!(s->res = kp_res_open(dirs, 1, &config, why, sizeof why))) {
		free(s);
		kp_error_set(err, "oracle: the speech's data: %s", why);
		return NULL;
	}
	kp_res_raw_files(s->res, &s->raw);
	s->next = sources;
	sources = s;
	return &s->raw;
}

/* A language's speech over the loaded campaign mission (ADR 0019, stage C2): its map's TIME, strictly (no map, or one
 * that differs in more than TIME where the profile excepts none, is an error, as the converter's speech was). */
static int lay_speech(KpOracleGame *og, const KpData *d, const char *key, const KpOracleOpts *opts, KpError *err)
{
	static const char MISSIONS[] = "missions/";
	const char *name = key + sizeof MISSIONS - 1;
	const KpFiles *raw;
	KpHostSpeech sp;
	int rc;
	if (strncmp(key, MISSIONS, sizeof MISSIONS - 1) != 0) return kp_error_set(err, "speech: %s is not a campaign mission", key);
	if (!(raw = speech_files(d, opts->speech_data, err)) || (rc = kp_host_game_speech(og, raw, opts->speech, name, &sp, err)) < 0)
		return -1;
	if (rc == 0) return kp_error_set(err, "speech %s: the data has no map of %s", opts->speech, name);
	if (sp.differ && !sp.excepted) {
		return kp_error_set(err, "speech %s: %s: %d records differ from the mission's in more than TIME, the first %s",
			opts->speech, name, sp.differ, sp.first);
	}
	return 0;
}

const KpDataMission *kp_oracle_mission(const char *root, const char *key, KpMission *m, KpError *err)
{
	const KpData *d;
	const KpDataMission *entry;
	uint8_t *chk = NULL;
	size_t chk_size = 0;
	memset(m, 0, sizeof *m);
	if (!(d = kp_oracle_data(root, err))) return NULL;
	if (!(entry = kp_data_find(d, key))) {
		kp_error_set(err, "oracle: no mission %s in the catalog", key);
		return NULL;
	}
	if (!entry->directory[0]) {
		char path[256];
		snprintf(path, sizeof path, KP_DATA_SCENARIOS "%s.chk", key + sizeof "scenarios/" - 1);
		if (!(chk = kp_read_under(root, path, &chk_size, err))) return NULL;
	}
	return kp_data_mission(d, entry, chk, chk_size, m, err) == 0 ? entry : NULL; /* the CHK taken */
}

int kp_oracle_game_start(KpOracleGame *og, const char *root, const char *key, uint32_t seed, int expansion,
	const KpOracleOpts *opts, KpError *err)
{
	KpOracleOpts defaults;
	KpHostOpts ho;
	const KpData *d;
	const KpDataMission *entry;
	KpMission m;
	if (!opts) {
		kp_oracle_opts(&defaults);
		opts = &defaults;
	}
	memset(og, 0, sizeof *og);
	if (!(entry = kp_oracle_mission(root, key, &m, err))) return -1;
	d = kp_oracle_data(root, err);
	if (kp_host_game_load_from(og, &d->shared, &d->dat, &m, opts->name ? opts->name : key, err) != 0) return -1;
	if (kp_host_game_tileset_files(og, &d->files, err) != 0
		|| (opts->speech && lay_speech(og, d, key, opts, err) != 0)) {
		kp_host_game_free(og);
		return -1;
	}
	ho = opts->host;
	if (ho.mission_index == KP_NIL) ho.mission_index = entry->index;
	return kp_host_game_start(og, seed, expansion, &ho, err);
}

void kp_oracle_game_free(KpOracleGame *og) { kp_host_game_free(og); }

int kp_replay_game_start(KpReplayGame *rg, const char *root, const KpReplay *r, KpError *err)
{
	KpOracleOpts o;
	kp_oracle_opts(&o);
	o.host.mission_index = r->mission_index; /* no presentation: the ring stays off */
	return kp_oracle_game_start(rg, root, r->mission, r->seed, r->expansion, &o, err);
}

void kp_replay_game_free(KpReplayGame *rg) { kp_oracle_game_free(rg); }

/* --- the runs --- */

void kp_runner_init(KpRunner *run, KpGame *g, const KpReplay *r)
{
	memset(run, 0, sizeof *run);
	run->g = g;
	run->r = r;
	run->step = 1;
}

/* The commands of the turns up to the game's turn. */
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

void kp_runner_drain(KpRunner *run)
{
	KpEvent e;
	if (!run->on_event) return;
	while (kp_event_take(run->g, &e)) run->on_event(run->ud, &e);
}

void kp_runner_apply(KpRunner *run, const KpReplayUi *u)
{
	KpGame *g = run->g;
	switch (u->kind) {
	case KP_RUN_SELECT: kp_select_locally(g, u->values, u->count); break;
	case KP_RUN_MARK: kp_mark_target(g, u->values[0], u->values[1], u->values[2]); break;
	case KP_RUN_CURSOR: kp_move_placement_cursor(g, u->values[0], u->values[1]); break;
	case KP_RUN_ONSCREEN:
		kp_refresh_terrain_onscreen(g, u->values[0], u->values[1], KP_SCREEN_TILES_W, KP_SCREEN_TILES_H);
		break;
	case KP_RUN_ALLIANCE: kp_set_alliance(g, u->values[0], u->values[1], u->values[2]); break;
	case KP_RUN_PAUSED: {
		KpCommand list[KP_TURN_COMMANDS];
		kp_paused_tick(g, list, turn_commands(run, list));
		break;
	}
	case KP_RUN_RELEASE: kp_trigger_release_wait(g); break;
	case KP_RUN_OBJECTIVES: kp_briefing_objectives(g, u->values[0]); break;
	case KP_RUN_CRT: kp_interface_crt_draw(g); break;
	case KP_RUN_SRAND: kp_interface_crt_seed(g, u->values[0]); break;
	case KP_RUN_CRITTER: kp_critter_annoyed(g, kp_game_unit(g, u->values[0])); break;
	case KP_RUN_MENU_PAUSE: kp_scalar_set(g, KP_PAUSED, u->values[0] != 0); break;
	case KP_RUN_PLACEMENT:
		if (u->count == 2) kp_placement_start(g, u->values[0], u->values[1]);
		else kp_placement_cancel(g);
		break;
	default: kp_trigger_skip(g); break;
	}
}

/* The interface events of the current frame, the turn and the frame count. */
static void loop_head(KpRunner *run)
{
	KpGame *g = run->g;
	while (run->next_ui < run->r->ui_count && run->r->ui[run->next_ui].frame <= kp_scalar(g, KP_FRAME)) {
		kp_runner_apply(run, &run->r->ui[run->next_ui++]);
	}
	turn(run);
	kp_scalar_set(g, KP_FRAME, kp_scalar(g, KP_FRAME) + 1);
}

void kp_runner_to(KpRunner *run, int64_t step)
{
	/* The briefing's objectives come before the game: the leading ones of the log, taken once by a run from the
	 * start (a run continued from a later step has already taken them). */
	if (run->step == 1 && !run->begun) {
		while (run->next_ui < run->r->ui_count && run->r->ui[run->next_ui].kind == KP_RUN_OBJECTIVES) {
			kp_briefing_objectives(run->g, run->r->ui[run->next_ui++].values[0]);
		}
		run->begun = 1;
	}
	while (run->step < step) {
		if (run->step == 1) {
			kp_step_objects(run->g);
		} else if (run->step == 2) {
			kp_step_objects(run->g);
			if (run->after_objects) run->after_objects(run->ud);
			kp_start_turns(run->g);
			turn(run);
			loop_head(run);
		} else {
			kp_step_frame(run->g);
			if (run->after_objects) run->after_objects(run->ud);
			loop_head(run);
		}
		kp_runner_drain(run);
		run->step++;
	}
}
