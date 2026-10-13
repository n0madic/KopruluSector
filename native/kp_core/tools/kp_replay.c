/*
 * kp_replay ROOT REPLAY [STEP]: replays a scenario (tools/lua/export_replay.lua) on a native game and compares the
 * category CRCs of its dump with the original's at every recorded step (from object step 3 on without the drawn
 * image bounds, tools/state_check.py). With STEP, prints the dump lines at that step instead. ROOT is the
 * repository root (generated/ under it). Prints one line per step compared; exits 1 at the first difference.
 */
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kp_runner.h"

#define DRAWN_STEP 3

typedef struct {
	const KpReplay *r;
	int64_t step;
	int bad, compared;
} Check;

static void check_crc(void *ud, const char *category, uint32_t crc)
{
	Check *c = (Check *)ud;
	int k;
	for (k = 0; k < c->r->crc_count; k++) {
		const KpReplayCrc *want = &c->r->crcs[k];
		if (want->step == c->step && strcmp(want->category, category) == 0) {
			c->compared++;
			if (want->crc != crc) {
				printf("step %lld %s: %u, the original %u\n", (long long)c->step, category, crc, want->crc);
				c->bad++;
			}
		}
	}
}

static void print_line(void *ud, const char *line, size_t length)
{
	(void)ud;
	fwrite(line, 1, length, stdout);
	fputc('\n', stdout);
}

/* The number of the original's CRCs recorded at a step. */
static int crcs_at(const KpReplay *r, int64_t step)
{
	int k, n = 0;
	for (k = 0; k < r->crc_count; k++) {
		if (r->crcs[k].step == step) n++;
	}
	return n;
}

int main(int argc, char **argv)
{
	KpReplay r;
	KpReplayGame rg;
	KpRunner run;
	KpError err;
	KpTry t;
	uint8_t *text;
	size_t size;
	int64_t step, print_step = argc > 3 ? atoll(argv[3]) : 0;
	int bad = 0, steps = 0, compared = 0;
	if (argc < 3) {
		fprintf(stderr, "usage: kp_replay ROOT REPLAY [STEP]\n");
		return 2;
	}
	text = kp_read_file(argv[2], &size);
	if (!text) {
		fprintf(stderr, "kp_replay: cannot read %s\n", argv[2]);
		return 2;
	}
	if (kp_replay_parse(&r, (const char *)text, size, &err) != 0 || kp_replay_game_start(&rg, argv[1], &r, &err) != 0) {
		fprintf(stderr, "kp_replay: %s\n", err.msg);
		return 2;
	}
	free(text);
	if (!print_step && (r.last < 1 || r.crc_count == 0)) {
		fprintf(stderr, "kp_replay: %s has no steps or no CRCs to compare\n", argv[2]);
		return 2;
	}
	kp_runner_init(&run, rg.g, &r);
	kp_images_begin(rg.g->w, &t);
	if (setjmp(t.buf)) {
		kp_images_end(rg.g->w, &t);
		fprintf(stderr, "kp_replay: step %lld: %s\n", (long long)run.step, rg.g->w->error);
		return 1;
	}
	for (step = 1; step <= (print_step ? print_step : r.last); step++) {
		kp_runner_to(&run, step);
		if (print_step) continue;
		if (crcs_at(&r, step) > 0) {
			Check c;
			c.r = &r;
			c.step = step;
			c.bad = c.compared = 0;
			if (kp_dump_crcs(rg.g, step >= DRAWN_STEP, check_crc, &c) != 0) {
				fprintf(stderr, "kp_replay: out of memory\n");
				return 2;
			}
			/* A category of the original that the dump no longer prints is a difference too. */
			if (c.compared != crcs_at(&r, step)) {
				printf("step %lld: %d of the original's categories missing from the dump\n", (long long)step,
					crcs_at(&r, step) - c.compared);
				c.bad++;
			}
			steps++;
			compared += c.compared;
			bad += c.bad;
			if (bad) break;
		}
	}
	if (print_step && kp_dump(rg.g, print_step >= DRAWN_STEP, print_line, NULL) != 0) return 2;
	kp_images_end(rg.g->w, &t);
	if (!print_step) printf("%s: %d steps, %d categories compared, %d differ\n", r.mission, steps, compared, bad);
	kp_replay_game_free(&rg);
	kp_replay_free(&r);
	return bad ? 1 : 0;
}
