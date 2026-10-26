/* The damage table of original code (phase 4 oracle): WeaponDamage of every weapon at every unit type, with the target
 * owner's upgrades at levels 0 and 3, equals the rows of tests/golden/oracle/damage_table.json
 * (tools/research/probe_damage_table_117.py ran the original's 0x41dfc0 on the DAT of an expansion game). The target is
 * one unit retyped for each row, as the probe's CUnit: player 0, incomplete, hit points 0x40000000, full shields; the
 * shot is the weapon's damage << 8, divisor 1, direction 0, no attacker, player 1. */
#include "kp_test.h"

#include <stdio.h>

#define GOLDEN "tests/golden/oracle/damage_table.json"
#define MISSION "generated/scenarios/medic_heal.kpm" /* an expansion game: the Brood War DAT */
#define ZEALOT 65                                    /* a sprite with a shield LO for the shield hit image */
#define UNIT_TYPES 228
#define WEAPONS 130
#define UPGRADES 61
#define HP 0x40000000
#define TARGET_PLAYER 0
#define ATTACKING_PLAYER 1

static void damage_table_equals_the_original(KpT *T)
{
	static const char *const FILES[] = { KP_ORACLE_DATA "dat.kpd", MISSION, NULL };
	const KpJson *golden, *levels, *rows;
	KpOracleGame *og;
	KpGame *g;
	KpBuf bad, row;
	int64_t n;
	int l;
	if (!kp_t_ready(T, "test_damage_table", FILES)) return;
	golden = kp_t_golden(T, GOLDEN);
	levels = kp_json_get(golden, "levels");
	rows = kp_json_get(golden, "rows");
	og = kp_t_game(T, MISSION, 1, 1, NULL);
	g = og->g;
	n = kp_unit_create(g, ZEALOT, 1000, 1000, TARGET_PLAYER);
	kp_t_ok(T, n != 0, "the target unit");
	kp_buf_init(&bad);
	kp_buf_init(&row);
	for (l = 0; l < kp_json_len(levels); l++) {
		int64_t level = kp_json_int(kp_json_at(levels, l), 0), t, id;
		char key[16];
		const KpJson *want;
		snprintf(key, sizeof key, "%lld", (long long)level);
		want = kp_json_get(rows, key);
		for (id = 0; id < UPGRADES; id++) g->players.upgrades[TARGET_PLAYER][id] = level;
		for (t = 0; t < UNIT_TYPES; t++) {
			int64_t shields = kp_t_dat(T, og, "units", "shield_enabled", t) != 0
				? kp_t_dat(T, og, "units", "shield_amount", t) * 256 : 0;
			int64_t w;
			row.size = 0;
			g->u.type[n] = t;
			for (w = 0; w < WEAPONS; w++) {
				g->u.hp[n] = HP;
				g->u.shields[n] = shields;
				g->u.status[n] = 0;
				kp_weapon_damage(g, n, kp_t_dat(T, og, "weapons", "damage_amount", w) * 256, w, 1, 0, 0, ATTACKING_PLAYER);
				kp_buf_printf(&row, "%s%lld/%lld", w ? " " : "", (long long)(HP - g->u.hp[n]), (long long)(shields - g->u.shields[n]));
			}
			if ((int64_t)kp_crc32((const uint8_t *)kp_buf_text(&row), row.size) != kp_json_int(kp_json_at(want, (int)t), -1)) {
				kp_buf_printf(&bad, "%slevel %lld unit type %lld", bad.size ? ", " : "", (long long)level, (long long)t);
			}
		}
	}
	kp_buf_free(&row);
	kp_t_defer(T, free, bad.data);
	kp_t_eq_str(T, bad.size ? bad.data : "", "", "rows that differ from the original");
}

static const KpTest TESTS[] = {
	{ "damage_table_equals_the_original", damage_table_equals_the_original, 0 },
};
KP_SUITE(kp_suite_damage_table, "test_damage_table", TESTS);
