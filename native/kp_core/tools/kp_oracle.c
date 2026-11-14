/* The oracle library (kp_oracle.h). */
#include "kp_oracle.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/core/kp_kpf.h"

/* --- goldens --- */

int kp_golden_load(KpGolden *g, const char *path, KpError *err)
{
	char why[200];
	size_t size;
	uint8_t *text;
	kp_json_doc_init(&g->doc);
	g->root = NULL;
	text = kp_read_file(path, &size);
	if (!text) return kp_error_set(err, "golden: cannot read %s", path);
	g->root = kp_json_parse(&g->doc, (const char *)text, size, why, sizeof why);
	free(text);
	if (!g->root) {
		kp_json_doc_free(&g->doc);
		return kp_error_set(err, "golden: %s: %s", path, why);
	}
	return 0;
}

void kp_golden_free(KpGolden *g)
{
	kp_json_doc_free(&g->doc);
	g->root = NULL;
}

/* A golden's interface entry; a run of the screen's draws ({frame, onscreen, draws = "d0d1..."}: d_k draws at one
 * place on frame + k, tools/state_check.py) expanded. */
static void golden_ui(KpReplay *r, const KpJson *e)
{
	const char *draws = kp_json_field_str(e, "draws", NULL);
	if (draws) {
		int64_t frame = kp_json_field_int(e, "frame", 0);
		const KpJson *at = kp_json_get(e, "onscreen");
		size_t k;
		for (k = 0; draws[k]; k++) {
			int d = draws[k] - '0', i;
			for (i = 0; i < d; i++) {
				KpReplayUi *u = kp_replay_ui(r, KP_RUN_ONSCREEN, frame + (int64_t)k);
				u->values[0] = kp_json_int(kp_json_at(at, 0), 0);
				u->values[1] = kp_json_int(kp_json_at(at, 1), 0);
				u->count = 2;
			}
		}
	} else {
		kp_replay_add_ui_json(r, e); /* as a log line */
	}
}

int kp_replay_from_golden(KpReplay *r, const char *root, const KpJson *golden, KpError *err)
{
	const KpJson *commands = kp_json_get(golden, "commands"), *ui = kp_json_get(golden, "ui");
	const KpJson *steps = kp_json_get(golden, "steps");
	int k, i;
	kp_replay_init(r);
	snprintf(r->mission, sizeof r->mission, "%s", kp_json_field_str(golden, "mission", ""));
	r->seed = (uint32_t)kp_json_field_int(golden, "seed", 0);
	r->expansion = (int)kp_json_field_int(golden, "expansion", 0);
	r->mission_index = kp_oracle_mission_index(root, r->mission);
	for (k = 0; k < kp_json_len(commands); k++) {
		if (kp_replay_add_command_json(r, kp_json_at(commands, k), err) != 0) goto bad;
	}
	for (k = 0; k < kp_json_len(ui); k++) golden_ui(r, kp_json_at(ui, k));
	for (k = 0; steps && k < steps->count && steps->type == KP_JSON_OBJECT; k++) {
		const KpJson *crcs = steps->items[k];
		int64_t step = strtoll(steps->keys[k], NULL, 10);
		for (i = 0; i < kp_json_len(crcs); i++) kp_replay_crc(r, step, crcs->keys[i], (uint32_t)kp_json_int(crcs->items[i], 0));
		if (step > r->last) r->last = step;
	}
	kp_replay_sort_crcs(r);
	return 0;
bad:
	kp_replay_free(r);
	return kp_error_set(err, "golden: a command's bytes are not hex");
}

static int by_step(const void *a, const void *b)
{
	const KpReplayCrc *x = (const KpReplayCrc *)a, *y = (const KpReplayCrc *)b;
	if (x->step != y->step) return x->step < y->step ? -1 : 1;
	return strcmp(x->category, y->category);
}

void kp_replay_sort_crcs(KpReplay *r)
{
	if (r->crc_count) qsort(r->crcs, (size_t)r->crc_count, sizeof *r->crcs, by_step);
}

int kp_replay_step_crcs(const KpReplay *r, int64_t step, int *count)
{
	int lo = 0, hi = r->crc_count;
	*count = 0;
	while (lo < hi) { /* the first CRC of the step or after it */
		int mid = (lo + hi) / 2;
		if (r->crcs[mid].step < step) lo = mid + 1;
		else hi = mid;
	}
	if (lo >= r->crc_count || r->crcs[lo].step != step) return -1;
	while (lo + *count < r->crc_count && r->crcs[lo + *count].step == step) (*count)++;
	return lo;
}

/* --- states --- */

static void add_crc(void *ud, const char *category, uint32_t crc)
{
	KpStateCrcs *c = (KpStateCrcs *)ud;
	if (c->count >= KP_ORACLE_CATEGORIES) return;
	snprintf(c->names[c->count], sizeof c->names[0], "%s", category);
	c->crcs[c->count++] = crc;
}

int kp_state_crcs(KpGame *g, int drawn, KpStateCrcs *out)
{
	out->count = 0;
	return kp_dump_crcs(g, drawn, add_crc, out);
}

uint32_t kp_state_crc(const KpStateCrcs *c, const char *category, int *found)
{
	int k;
	for (k = 0; k < c->count; k++) {
		if (strcmp(c->names[k], category) == 0) {
			if (found) *found = 1;
			return c->crcs[k];
		}
	}
	if (found) *found = 0;
	return 0;
}

static int by_name(const void *a, const void *b) { return strcmp(*(const char *const *)a, *(const char *const *)b); }

uint32_t kp_state_fingerprint(const KpStateCrcs *c)
{
	const char *names[KP_ORACLE_CATEGORIES];
	KpBuf lines;
	uint32_t crc;
	int k;
	for (k = 0; k < c->count; k++) names[k] = c->names[k];
	qsort(names, (size_t)c->count, sizeof *names, by_name);
	kp_buf_init(&lines);
	for (k = 0; k < c->count; k++) kp_buf_printf(&lines, "%s %u\n", names[k], (unsigned)kp_state_crc(c, names[k], NULL));
	crc = kp_crc32((const uint8_t *)kp_buf_text(&lines), lines.size);
	kp_buf_free(&lines);
	return crc;
}

int kp_step_differences(const KpStateCrcs *c, const KpReplayCrc *want, int count, KpBuf *out)
{
	int k, bad = 0;
	for (k = 0; k < count; k++) {
		if (strcmp(want[k].category, KP_ORACLE_FINGERPRINT) == 0) {
			if (kp_state_fingerprint(c) != want[k].crc) {
				kp_buf_str(out, " " KP_ORACLE_FINGERPRINT);
				return 1;
			}
			return 0;
		}
	}
	for (k = 0; k < count; k++) {
		int found;
		if (kp_state_crc(c, want[k].category, &found) != want[k].crc || !found) {
			kp_buf_printf(out, " %s", want[k].category);
			bad++;
		}
	}
	return bad;
}

void kp_trace_line(const KpEvent *e, char *out, size_t size)
{
	snprintf(out, size, "%lld %lld %lld %lld %lld %lld", (long long)e->frame, (long long)e->a, (long long)e->b,
		(long long)((uint64_t)e->c >> 16), (long long)((e->c >> 8) & 0xff), (long long)(e->c & 0xff));
}

void kp_ai_trace_line(const KpEvent *e, char *out, size_t size)
{
	snprintf(out, size, "%lld %lld %lld %lld %lld %lld", (long long)e->frame, (long long)e->a, (long long)(e->b >> 1),
		(long long)(e->b & 1), (long long)(e->c / 256), (long long)(e->c % 256));
}

static void add_line(void *ud, const char *line, size_t length)
{
	KpBuf *out = (KpBuf *)ud;
	kp_buf_add(out, line, length);
	kp_buf_char(out, '\n');
}

int kp_state_lines(KpGame *g, int drawn, KpBuf *out) { return kp_dump(g, drawn, add_line, out); }

int kp_oracle_chain_opts(KpOracleOpts *opts, KpCarry *carry, KpError *err)
{
	const char *path = getenv("KP_CARRY"), *turn = getenv("KP_TURN"), *speech = getenv("KP_SPEECH"),
		*speech_data = getenv("KP_SPEECH_DATA"), *replay = getenv("KP_REPLAY");
	if (path && path[0]) {
		size_t size;
		uint8_t *bytes = kp_read_file(path, &size);
		if (!bytes) return kp_error_set(err, "oracle: cannot read %s", path);
		if (size != sizeof *carry) {
			free(bytes);
			return kp_error_set(err, "game: options.carry is not a game's carry");
		}
		memcpy(carry, bytes, sizeof *carry);
		free(bytes);
		opts->host.carry = carry;
	}
	if (turn && turn[0]) opts->host.turn = strtoll(turn, NULL, 10);
	if (speech && speech[0]) opts->speech = speech;
	if (speech_data && speech_data[0]) opts->speech_data = speech_data;
	if (replay && replay[0] == '1') opts->host.replay = 1;
	return 0;
}

/* --- golden runs --- */

static void golden_event(void *ud, const KpEvent *e)
{
	KpGoldenRun *gr = (KpGoldenRun *)ud;
	char line[160];
	if (e->kind == KP_EVENT_TRIGGER) {
		kp_trace_line(e, line, sizeof line);
		if (gr->trace_count++) kp_buf_char(&gr->trace, '\n');
		kp_buf_str(&gr->trace, line);
	} else if (e->kind == KP_EVENT_AI) {
		kp_ai_trace_line(e, line, sizeof line);
		if (gr->ai_count++) kp_buf_char(&gr->ai_trace, '\n');
		kp_buf_str(&gr->ai_trace, line);
	}
	if (gr->on_event) gr->on_event(gr->ud, e);
}

int kp_golden_run_start(KpGoldenRun *gr, const char *root, const KpJson *g, const KpOracleOpts *opts, int trace,
	KpError *err)
{
	KpOracleOpts o;
	memset(gr, 0, sizeof *gr);
	kp_buf_init(&gr->trace);
	kp_buf_init(&gr->ai_trace);
	if (opts) o = *opts;
	else kp_oracle_opts(&o);
	if (trace) o.host.trace = 1;
	o.host.turn = kp_json_field_int(g, "turn", o.host.turn);
	if (kp_json_field_int(g, "replay", 0)) o.host.replay = 1; /* the original playing a replay (--kind replays) */
	if (kp_replay_from_golden(&gr->replay, root, g, err) != 0) return -1;
	o.host.mission_index = gr->replay.mission_index;
	if (kp_oracle_game_start(&gr->game, root, gr->replay.mission, gr->replay.seed, gr->replay.expansion, &o, err) != 0) {
		kp_replay_free(&gr->replay);
		return -1;
	}
	gr->tracing = o.host.trace;
	kp_runner_init(&gr->run, gr->game.g, &gr->replay);
	gr->run.on_event = golden_event;
	gr->run.ud = gr;
	return 0;
}

void kp_golden_run_free(KpGoldenRun *gr)
{
	kp_oracle_game_free(&gr->game);
	kp_replay_free(&gr->replay);
	kp_buf_free(&gr->trace);
	kp_buf_free(&gr->ai_trace);
}

/* A golden's list of strings joined by "\n". */
static void joined(const KpJson *list, KpBuf *out)
{
	int k;
	for (k = 0; k < kp_json_len(list); k++) {
		if (k) kp_buf_char(out, '\n');
		kp_buf_str(out, kp_json_str(kp_json_at(list, k), ""));
	}
}

int kp_golden_run_check(KpGoldenRun *gr, const KpJson *g, const char *name, KpBuf *bad)
{
	int64_t step;
	int found = 0;
	const KpJson *trace = kp_json_get(g, "trace"), *ai = kp_json_get(g, "ai_trace");
	for (step = 1; step <= gr->replay.last; step++) {
		int count, at;
		kp_runner_to(&gr->run, step);
		at = kp_replay_step_crcs(&gr->replay, step, &count);
		if (at >= 0) {
			KpStateCrcs crcs;
			KpBuf diffs;
			kp_buf_init(&diffs);
			if (kp_state_crcs(gr->game.g, step >= KP_ORACLE_DRAWN_STEP, &crcs) != 0) {
				kp_buf_printf(bad, "%s%s step %lld: out of memory", bad->size ? ", " : "", name, (long long)step);
				kp_buf_free(&diffs);
				return found + 1;
			}
			if (kp_step_differences(&crcs, &gr->replay.crcs[at], count, &diffs) > 0) {
				const char *p = kp_buf_text(&diffs);
				while (*p == ' ') {
					const char *end = strchr(p + 1, ' ');
					int n = end ? (int)(end - p - 1) : (int)strlen(p + 1);
					kp_buf_printf(bad, "%s%s step %lld %.*s", bad->size ? ", " : "", name, (long long)step, n, p + 1);
					found++;
					p += n + 1;
				}
			}
			kp_buf_free(&diffs);
			if (found) return found;
		}
	}
	if (trace) {
		KpBuf want;
		kp_buf_init(&want);
		joined(trace, &want);
		if (want.size != gr->trace.size || memcmp(kp_buf_text(&want), kp_buf_text(&gr->trace), want.size) != 0) {
			kp_buf_printf(bad, "%s%s trigger trace", bad->size ? ", " : "", name);
			found++;
		}
		kp_buf_free(&want);
	}
	if (ai) {
		uint32_t crc = kp_crc32((const uint8_t *)kp_buf_text(&gr->ai_trace), gr->ai_trace.size);
		if (gr->ai_count != kp_json_field_int(ai, "count", -1) || (int64_t)crc != kp_json_field_int(ai, "crc", -1)) {
			kp_buf_printf(bad, "%s%s AI trace", bad->size ? ", " : "", name);
			found++;
		}
	}
	return found;
}

/* --- campaign chains --- */

int kp_campaign_chain(const char *root, const KpChainGame *games, int count, KpChainPlay play, KpChainAfter after,
	KpChainRelease release, void *ud, KpBuf *failed, KpError *err)
{
	const KpData *d = kp_oracle_data(root, err);
	KpCampaignProfile *profile;
	KpCampaignRun run;
	KpOracleGame *before = NULL;
	KpCarry carry;
	int have_run = 0, k, result = 0;
	int64_t expected = KP_NIL;
	if (!d) return -1;
	if (!(profile = (KpCampaignProfile *)malloc(sizeof *profile))) return kp_error_set(err, "oracle: out of memory");
	kp_campaign_new_profile(profile);
	for (k = 0; k < count; k++) {
		const KpJson *g = games[k].golden;
		KpOracleOpts o;
		KpOracleGame *game;
		KpCampaignStep step;
		int64_t cinematics[16], mission;
		int n;
		KpBuf why;
		mission = kp_oracle_mission_index(root, kp_json_field_str(g, "mission", ""));
		if (expected != KP_NIL && mission != expected) {
			kp_buf_printf(failed, "%s: mission %lld, but the campaign chose %lld", games[k].name, (long long)mission,
				(long long)expected);
			result = 1;
			break;
		}
		kp_oracle_opts(&o);
		if (before) {
			kp_carry_take(before->g, &carry);
			o.host.carry = &carry;
		}
		o.host.turn = kp_json_field_int(g, "turn", 0);
		kp_buf_init(&why);
		game = play(ud, g, games[k].name, &o, &why);
		if (before && release) release(ud, before);
		before = NULL;
		if (!game) {
			kp_buf_printf(failed, "%s: %s", games[k].name, kp_buf_text(&why));
			kp_buf_free(&why);
			result = 1;
			break;
		}
		kp_buf_free(&why);
		if (!have_run) {
			kp_campaign_new_run(&run, (int)kp_json_field_int(g, "expansion", 0), mission);
			have_run = 1;
		}
		kp_campaign_after_game(&d->campaign, &run, profile, game->g->results[game->g->local_player],
			(const char *)game->g->trig.next_scenario, &step, cinematics, &n, 16);
		expected = step.kind == KP_CAMPAIGN_STEP_MISSION || step.kind == KP_CAMPAIGN_STEP_CINEMATIC ? step.mission
			: step.kind == KP_CAMPAIGN_STEP_RESTART ? run.mission : KP_NIL;
		if (after) after(ud, games[k].name, &step, expected, &d->campaign);
		before = game;
	}
	if (before && release) release(ud, before);
	free(profile);
	return result;
}
