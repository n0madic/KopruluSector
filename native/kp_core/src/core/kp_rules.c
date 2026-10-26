/* The effects of the UMS override sections (kp_start.h; the C twin of converter/derive/rules.py). */
#include "kp_ids.h"
#include "kp_start.h"

#include <stdio.h>
#include <string.h>

#define BW_UPGRADES 46
#define BW_TECHS 24

static const int UNIT_FIELD_SIZES[KP_RULE_UNIT_FIELDS] = { 4, 2, 1, 2, 2, 2, 2 };

/* LAYOUT[tag] = { count, extra, length }. */
static const struct {
	const char *tag;
	int count, extra, length;
} LAYOUT[] = {
	{ "UNIS", 228, 100, 0xfd0 }, { "UNIx", 228, 130, 0x1048 }, { "UPGS", 46, 0, 0x256 }, { "UPGx", 61, 1, 0x31a },
	{ "TECS", 24, 0, 0xd8 }, { "TECx", 44, 0, 0x18c }, { "UPGR", 46, 0, 0x6d4 }, { "PUPx", 61, 0, 0x90e },
	{ "PTEC", 24, 0, 0x390 }, { "PTEx", 44, 0, 0x688 }, { "PUNI", 228, 0, 0x1644 },
};

static int64_t uint_at(const uint8_t *s, int at, int width)
{
	if (width == 1) return s[at];
	if (width == 2) return (int64_t)s[at] | (int64_t)s[at + 1] << 8;
	return (int64_t)((uint32_t)s[at] | (uint32_t)s[at + 1] << 8 | (uint32_t)s[at + 2] << 16 | (uint32_t)s[at + 3] << 24);
}

static int64_t dat_unit(const KpDatColumn *c, int u) { return kp_dat_has(c, (uint32_t)u) ? (int64_t)kp_dat_value(c, (uint32_t)u) : -1; }

static int fail(KpError *err, const char *msg)
{
	snprintf(err->msg, sizeof err->msg, "%s", msg);
	return -1;
}

static void init_effects(KpRuleEffects *e)
{
	int64_t *all = &e->units[0][0];
	size_t k, n = sizeof *e / sizeof *all;
	for (k = 0; k < n; k++) all[k] = KP_RULE_UNSET;
}

/* The effects of one override section. */
static int apply(KpRuleEffects *e, const KpMissionRule *r, const KpDatColumn *subunit1, const KpDatColumn *ground,
	const KpDatColumn *air, int expansion, KpError *err)
{
	const uint8_t *raw = r->data.data;
	int count, extra, length, k, u, p, f, found = -1;
	char msg[KP_ERROR_SIZE];
	for (k = 0; k < (int)(sizeof LAYOUT / sizeof LAYOUT[0]); k++) {
		if (strcmp(LAYOUT[k].tag, r->tag) == 0) found = k;
	}
	if (found < 0) {
		snprintf(msg, sizeof msg, "rules: unknown section %s", r->tag);
		return fail(err, msg);
	}
	count = LAYOUT[found].count;
	extra = LAYOUT[found].extra;
	length = LAYOUT[found].length;
	if ((int)r->data.size != length) {
		snprintf(msg, sizeof msg, "rules: %s is %d bytes, expected %d", r->tag, (int)r->data.size, length);
		return fail(err, msg);
	}
	if (strcmp(r->tag, "UNIS") == 0 || strcmp(r->tag, "UNIx") == 0) {
		int offsets[KP_RULE_UNIT_FIELDS], offset = KP_RULE_UNITS;
		for (f = 0; f < KP_RULE_UNIT_FIELDS; f++) {
			offsets[f] = offset;
			offset += KP_RULE_UNITS * UNIT_FIELD_SIZES[f];
		}
		for (u = KP_RULE_UNITS - 1; u >= 0; u--) {
			int64_t sub, weapon_unit, weapons[2];
			if (raw[u] != 0) continue;
			for (f = 0; f < KP_RULE_UNIT_FIELDS; f++) {
				e->units[f][u] = uint_at(raw, offsets[f] + u * UNIT_FIELD_SIZES[f], UNIT_FIELD_SIZES[f]);
			}
			sub = dat_unit(subunit1, u);
			weapon_unit = sub == KP_TYPE_NONE ? u : sub;
			weapons[0] = dat_unit(ground, (int)weapon_unit);
			weapons[1] = dat_unit(air, (int)weapon_unit);
			for (k = 0; k < 2; k++) {
				int64_t w = weapons[k];
				if (w == KP_WEAPON_NONE) continue;
				if (w < 0 || w >= extra) {
					snprintf(msg, sizeof msg, "rules: %s custom unit %d uses weapon %lld past the native buffer", r->tag, u,
						(long long)w);
					return fail(err, msg);
				}
				e->weapon_damage[w] = uint_at(raw, offset + 2 * (int)w, 2);
				e->weapon_bonus[w] = uint_at(raw, offset + 2 * (extra + (int)w), 2);
			}
		}
	} else if (strcmp(r->tag, "UPGS") == 0 || strcmp(r->tag, "UPGx") == 0 || strcmp(r->tag, "TECS") == 0
		|| strcmp(r->tag, "TECx") == 0) {
		int upgrade = r->tag[0] == 'U', fields = upgrade ? KP_RULE_UPGRADE_FIELDS : KP_RULE_TECH_FIELDS;
		for (u = 0; u < count; u++) {
			if (raw[u] != 0) continue;
			for (f = 0; f < fields; f++) {
				int64_t v = uint_at(raw, count + extra + (f * count + u) * 2, 2);
				if (upgrade) e->upgrades[f][u] = v;
				else e->techs[f][u] = v;
			}
		}
	} else if (strcmp(r->tag, "PUNI") == 0) {
		for (p = 0; p < KP_RULE_PLAYERS; p++) {
			for (u = 0; u < KP_RULE_UNITS; u++) {
				int i = p * KP_RULE_UNITS + u;
				e->availability[p][u] = raw[13 * KP_RULE_UNITS + i] != 0 ? raw[12 * KP_RULE_UNITS + u] : raw[i];
			}
		}
	} else {
		int upgrade = strcmp(r->tag, "UPGR") == 0 || strcmp(r->tag, "PUPx") == 0, gated = upgrade ? BW_UPGRADES : BW_TECHS;
		for (p = 0; p < KP_RULE_PLAYERS; p++) {
			for (u = 0; u < count; u++) {
				int i = p * count + u, fk;
				int defaulted = raw[26 * count + i] != 0;
				for (fk = 0; fk < 2; fk++) {
					f = fk == 0 ? 1 : 0;
					if (!(f == 1 && u >= gated && !expansion)) {
						int64_t v = defaulted ? raw[(24 + f) * count + u] : raw[f * 12 * count + i];
						if (upgrade) e->upgrade_levels[f][p][u] = v;
						else e->tech_states[f][p][u] = v;
					}
				}
			}
		}
	}
	return 0;
}

int kp_rules_effects(KpRuleEffects *e, const KpMission *m, const KpDat *dat, int expansion, KpError *err)
{
	const KpDatColumn *subunit1 = kp_dat_column(dat, "units", "subunit1");
	const KpDatColumn *ground = kp_dat_column(dat, "units", "ground_weapon");
	const KpDatColumn *air = kp_dat_column(dat, "units", "air_weapon");
	int k;
	init_effects(e);
	if (!subunit1 || !ground || !air) return fail(err, "rules: the units table lacks its weapon columns");
	for (k = 0; k < m->rule_count; k++) {
		if (apply(e, &m->rules[k], subunit1, ground, air, expansion, err) != 0) return -1;
	}
	return 0;
}
