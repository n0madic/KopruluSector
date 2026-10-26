/* Initial mission state (phase 3.1) against the original: the state that StartGame leaves before the first object step,
 * for every mission in tests/golden/oracle/initial_state/index.json, compared category by category with the CRCs of the
 * original's state dumps (tools/state_check.py), with the objectives the original's briefing set before the game (which
 * kp_core's briefing_lead derives from the mission's briefing, as when Start is pressed at once). Needs the converter's
 * generated data; skipped without it. */
#include "kp_test.h"

#include <stdio.h>

#define DIR "tests/golden/oracle/initial_state/"

static void missions_equal_the_original(KpT *T)
{
	static const char *const FILES[] = { KP_ORACLE_DATA "dat.kpd", NULL };
	const KpJson *index;
	KpBuf bad;
	int k;
	if (!kp_t_ready(T, "test_initial_state", FILES)) return;
	index = kp_t_golden(T, DIR "index.json");
	kp_buf_init(&bad);
	for (k = 0; k < kp_json_len(index); k++) {
		const char *name = kp_json_str(kp_json_at(index, k), "");
		const KpJson *g, *objectives, *want;
		char path[512];
		KpOracleGame *game;
		KpStateCrcs crcs;
		int64_t lead;
		int i, n;
		snprintf(path, sizeof path, DIR "%s.json", name);
		g = kp_t_golden(T, path);
		snprintf(path, sizeof path, "generated/missions/%s.kpm", kp_json_field_str(g, "mission", ""));
		game = kp_t_game(T, path, (uint32_t)kp_json_field_int(g, "seed", 0), (int)kp_json_field_int(g, "expansion", 0),
			NULL);
		objectives = kp_json_get(g, "objectives");
		n = kp_json_len(objectives);
		lead = kp_briefing_lead(game->g);
		if (lead != (n ? kp_json_int(kp_json_at(objectives, n - 1), 0) : 0)) {
			kp_buf_printf(&bad, "%s%s briefing_lead %lld", bad.size ? ", " : "", name, (long long)lead);
		}
		for (i = 0; i < n; i++) kp_briefing_objectives(game->g, kp_json_int(kp_json_at(objectives, i), 0));
		kp_t_ok(T, kp_state_crcs(game->g, 0, &crcs) == 0, "%s: out of memory", name);
		want = kp_json_get(g, "crc");
		for (i = 0; i < kp_json_len(want); i++) {
			int found;
			if (kp_state_crc(&crcs, want->keys[i], &found) != (uint32_t)kp_json_int(want->items[i], -1) || !found) {
				kp_buf_printf(&bad, "%s%s %s", bad.size ? ", " : "", name, want->keys[i]);
			}
		}
		kp_t_game_free(T, game);
	}
	kp_t_defer(T, free, bad.data);
	kp_t_ok(T, bad.size == 0, "categories differ from the original: %s", bad.size ? bad.data : "");
}

static const KpTest TESTS[] = {
	{ "missions_equal_the_original", missions_equal_the_original, 0 },
};
KP_SUITE(kp_suite_initial_state, "test_initial_state", TESTS);
