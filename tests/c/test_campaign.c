/* The campaign progression (kp_campaign.h; plan phase S, stage S7) against the original: the emulator's campaign chains
 * (tests/golden/oracle/campaign_chains.json, tools/oracle/export_campaign_chains.py) played through the progression
 * alone, without games, and the resolver's contract (atlas P41 0x4bba20). Needs the converter's
 * generated/data/campaign.kpd; skipped without it. The games of the chains themselves are test_scenarios'
 * campaign_chains. A load needs no profile rule of its own: kp_ui's load keeps the profile's character record as the
 * original's LoadGame does. */
#include "kp_test.h"

#include <ctype.h>
#include <string.h>

#include "../../native/kp_core/src/core/kp_campaign.h"

#define DATA KP_ORACLE_DATA "campaign.kpd"
#define CHAINS "tests/golden/oracle/campaign_chains.json"
#define MODE_MENU 4   /* the game mode the campaign's end leaves to when it names a glue state (no epilog) */
#define MODE_EPILOG 9 /* the epilog's */

static const char *const FILES[] = { DATA, CHAINS, NULL };

static void free_campaign(void *p)
{
	kp_campaign_free((KpCampaign *)p);
	free(p);
}

static KpCampaign *load(KpT *T)
{
	KpCampaign *c;
	KpError err;
	size_t size;
	uint8_t *data;
	if (!kp_t_ready(T, "test_campaign", FILES)) return NULL;
	data = kp_t_read(T, DATA, &size);
	c = (KpCampaign *)calloc(1, sizeof *c);
	kp_t_ok(T, c != NULL, "out of memory");
	if (kp_campaign_load(c, data, size, DATA, &err) != 0) {
		free(c);
		kp_t_fail(T, "%s", err.msg);
	}
	kp_t_defer(T, free_campaign, c);
	return c;
}

/* The profile's progress as the emulator saves it: the original's Zerg, Terran, Protoss, then the expansion's. */
static void progress(const KpCampaignProfile *p, char *out, size_t size)
{
	snprintf(out, size, "%lld,%lld,%lld,%lld,%lld,%lld", (long long)p->original[0], (long long)p->original[1],
		(long long)p->original[2], (long long)p->expansion[0], (long long)p->expansion[1], (long long)p->expansion[2]);
}

static void tables(KpT *T)
{
	KpCampaign *c = load(T);
	char name[64];
	int selectable = 0, hidden = 0, k, i;
	if (!c) return;
	kp_t_eq(T, c->table_count, 6, "campaign tables");
	for (k = 0; k < c->table_count; k++) {
		for (i = 0; i < c->tables[k].count; i++) {
			const KpCampaignRow *r = &c->tables[k].rows[i];
			if (r->mission != 0 && r->cinematic == 0) {
				if (r->hidden != 0) hidden++;
				else selectable++;
			}
		}
	}
	kp_t_eq(T, selectable, 57, "selectable missions");
	kp_t_eq(T, hidden, 7, "hidden missions");
	kp_t_ok(T, kp_campaign_mission_name(c, 1, name, sizeof name), "mission 1's name");
	kp_t_eq_str(T, name, "terran/tutorial", "mission 1");
	kp_t_ok(T, kp_campaign_mission_name(c, 12, name, sizeof name), "mission 12's name");
	kp_t_eq_str(T, name, "zerg/zerg01", "mission 12");
	kp_t_eq(T, c->carried, 0x28000200, "the cheat bits a new game keeps");
}

static int64_t mission_of(KpT *T, const KpCampaign *c, const KpCampaignRun *cur, const char *name)
{
	const KpCampaignRow *r = kp_campaign_row(c, kp_campaign_resolve(c, cur, name));
	kp_t_ok(T, r != NULL, "%s resolves to a row", name);
	return r->mission;
}

/* ResolveNextScenarioRow 0x4bba20: the extension picks the tables, the basename is a case-insensitive prefix, an
 * index without a playable row in the picked tables goes on to the next (Zerg10.scx: the original's zerg10 first),
 * a name without a dot or without a match keeps the current row. */
static void resolver(KpT *T)
{
	KpCampaign *c = load(T);
	KpCampaignRun cur;
	int64_t here;
	if (!c) return;
	kp_campaign_new_run(&cur, 0, 0);
	cur.row.table = 0;
	cur.row.index = 0;
	here = kp_campaign_row(c, cur.row)->mission;
	kp_t_eq(T, mission_of(T, c, &cur, "Zerg10.scx"), 64, "Zerg10.scx");
	kp_t_eq(T, mission_of(T, c, &cur, "zerg10.SCM"), 21, "zerg10.SCM");
	kp_t_eq(T, mission_of(T, c, &cur, "Terran05a.scx"), 44, "Terran05a.scx");
	kp_t_eq(T, mission_of(T, c, &cur, "terran05b.scx"), 45, "terran05b.scx");
	kp_t_eq(T, mission_of(T, c, &cur, "Zerg04F.SCX"), 57, "Zerg04F.SCX");
	kp_t_eq(T, mission_of(T, c, &cur, "Zerg05"), here, "no dot: the current row");
	kp_t_eq(T, mission_of(T, c, &cur, "nowhere.scx"), here, "no match: the current row");
}

/* Every name a campaign map's Set Next Scenario names: an expansion map's ".scx" resolves to the expansion
 * directory of that basename (its own campaign's), not to the original campaign's map of the same name. */
static void next_scenario_names(KpT *T)
{
	KpCampaign *c = load(T);
	const KpJson *names;
	KpCampaignRun cur;
	int k;
	if (!c) return;
	names = kp_json_get(kp_t_golden(T, CHAINS), "next_scenarios");
	kp_t_eq(T, kp_json_len(names), 31, "the names");
	kp_campaign_new_run(&cur, 0, 0);
	cur.row.table = 0;
	cur.row.index = 0;
	for (k = 0; k < kp_json_len(names); k++) {
		const KpJson *n = kp_json_at(names, k);
		const char *name = kp_json_field_str(n, "name", ""), *map = kp_json_field_str(n, "map", "");
		const char *slash = strrchr(map, '\\'), *dot = strrchr(name, '.');
		KpCampaignRef ref = kp_campaign_resolve(c, &cur, name);
		const KpCampaignRow *r;
		char want[128];
		size_t at, i;
		kp_t_ok(T, ref.table != cur.row.table || ref.index != cur.row.index, "%s resolves", name);
		r = kp_campaign_row(c, ref);
		kp_t_ok(T, r != NULL && slash != NULL && dot != NULL, "%s: a row", name);
		/* the map's directory and the name's basename in lower case */
		at = (size_t)(slash - map) + 1;
		kp_t_ok(T, at + (size_t)(dot - name) < sizeof want, "%s: a short name", name);
		memcpy(want, map, at);
		for (i = 0; name + i < dot; i++) want[at + i] = (char)tolower((unsigned char)name[i]);
		want[at + i] = '\0';
		kp_t_ok(T, r->mission < c->directory_count && c->directories[r->mission] != NULL, "%s: a directory", name);
		kp_t_eq_str(T, c->directories[r->mission], want, "%s %s", map, name);
	}
}

/* Each chain from its first mission: after every game the score screen's RecordNextMissionReached 0x4bb970, then
 * AdvanceCampaignAfterGame 0x4bb790 (with the name the mission's SetNextScenario left); a cinematic is played and
 * the advance runs again. The next mission, the profile's progress and the chain's end are the original's. */
static void chains(KpT *T)
{
	KpCampaign *c = load(T);
	const KpJson *cases;
	int k, m;
	if (!c) return;
	cases = kp_json_get(kp_t_golden(T, CHAINS), "cases");
	kp_t_ok(T, kp_json_len(cases) > 0, "the chains");
	for (k = 0; k < kp_json_len(cases); k++) {
		const KpJson *ch = kp_json_at(cases, k), *missions = kp_json_get(ch, "missions");
		const char *name = kp_json_field_str(ch, "name", "");
		int track_progress = strncmp(name, "exp", 3) == 0 || strncmp(name, "orig", 4) == 0;
		KpCampaignProfile *profile = (KpCampaignProfile *)malloc(sizeof *profile);
		KpCampaignRun run;
		kp_t_ok(T, profile != NULL, "out of memory");
		kp_t_defer(T, free, profile);
		kp_campaign_new_profile(profile);
		kp_campaign_new_run(&run, (int)kp_json_field_int(ch, "expansion", 0),
			kp_json_field_int(kp_json_at(missions, 0), "mission", -1));
		for (m = 0; m < kp_json_len(missions); m++) {
			const KpJson *g = kp_json_at(missions, m), *want = kp_json_get(g, "progress");
			int64_t mission = kp_json_field_int(g, "mission", -1), result = kp_json_field_int(g, "result", -1);
			int64_t cinematics[8];
			int count;
			KpCampaignStep step;
			kp_t_eq(T, run.mission, mission, "%s mission %lld", name, (long long)mission);
			kp_campaign_after_game(c, &run, profile, result, kp_json_field_str(g, "next_scenario", NULL), &step, cinematics,
				&count, 8);
			if (result != KP_CAMPAIGN_RESULT_VICTORY) {
				kp_t_eq(T, step.kind, KP_CAMPAIGN_STEP_RESTART, "%s mission %lld: a lost mission restarts", name,
					(long long)mission);
			} else if (kp_json_get(g, "next_mission")) {
				kp_t_eq(T, step.kind, KP_CAMPAIGN_STEP_MISSION, "%s mission %lld: the next game", name, (long long)mission);
				kp_t_eq(T, step.mission, kp_json_field_int(g, "next_mission", -1), "%s mission %lld", name, (long long)mission);
			} else if (m == kp_json_len(missions) - 1 && kp_json_get(ch, "end_mode")) {
				int epilog = step.kind == KP_CAMPAIGN_STEP_EPILOG;
				kp_t_ok(T, epilog || step.kind == KP_CAMPAIGN_STEP_GLUE, "%s mission %lld: the campaign ends with a step", name,
					(long long)mission);
				kp_t_eq(T, epilog ? MODE_EPILOG : MODE_MENU, kp_json_field_int(ch, "end_mode", -1),
					"%s mission %lld: the end's mode", name, (long long)mission);
				if (!epilog) {
					kp_t_eq(T, step.glue, kp_json_field_int(ch, "end_glue", -1), "%s mission %lld: the end's glue state", name,
						(long long)mission);
				}
			}
			if (want && track_progress) {
				char got[128], expected[128];
				size_t at = 0;
				int i;
				progress(profile, got, sizeof got);
				expected[0] = '\0';
				for (i = 0; i < kp_json_len(want) && at < sizeof expected; i++) {
					at += (size_t)snprintf(expected + at, sizeof expected - at, "%s%lld", i ? "," : "",
						(long long)kp_json_int(kp_json_at(want, i), -1));
				}
				kp_t_eq_str(T, got, expected, "%s mission %lld: progress", name, (long long)mission);
			}
		}
	}
}

static const KpTest TESTS[] = {
	{ "tables", tables, 0 },
	{ "resolver", resolver, 0 },
	{ "next_scenario_names", next_scenario_names, 0 },
	{ "chains", chains, 0 },
};
KP_SUITE(kp_suite_campaign, "test_campaign", TESTS);
