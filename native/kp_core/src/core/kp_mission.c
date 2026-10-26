/* Mission container loader (kp_mission.h). */
#include "kp_mission.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Derived sections and the producer versions this build reads, and WAVS v2. */
const char *const KP_TILESET_NAMES[8] = { "badlands", "platform", "install", "ashworld", "jungle", "desert", "ice",
	"twilight" };

static const KpKpfSectionExpect SECTIONS[] = {
	{ "WAVS", 2, NULL, 0 },
	{ "WALK", 0, "WLK1", 1 },
	{ "RGNS", 0, "RGN1", 2 },
	{ "CONT", 0, "CNT1", 1 },
	{ "PACK", 0, "PAK1", 1 },
};

static int fail(KpError *err, const char *name, const char *msg)
{
	snprintf(err->msg, sizeof err->msg, "mission: %s: %s", name && name[0] ? name : "<data>", msg);
	return -1;
}

static int section(const KpKpf *c, const char *fourcc, KpBytes *out, KpError *err)
{
	const KpKpfSection *s = kp_kpf_require(c, fourcc, err);
	if (!s) return -1;
	out->data = s->data;
	out->size = s->size;
	return 0;
}

static int parse_meta(KpMission *m, KpBytes meta)
{
	const uint8_t *p = meta.data;
	int i;
	if (meta.size < 65) return -1;
	m->chk_version = kp_le_u16(p);
	m->era = kp_le_u16(p + 2);
	m->width = kp_le_u16(p + 4);
	m->height = kp_le_u16(p + 6);
	memcpy(m->owners, p + 8, 12);
	memcpy(m->races, p + 20, 12);
	memcpy(m->force_player, p + 32, 8);
	for (i = 0; i < 4; i++) m->force_name[i] = (uint16_t)kp_le_u16(p + 40 + 2 * i);
	memcpy(m->force_flags, p + 48, 4);
	m->name_string = (uint16_t)kp_le_u16(p + 52);
	m->description_string = (uint16_t)kp_le_u16(p + 54);
	m->has_colors = (p[64] & KP_MISSION_META_COLR) != 0;
	if (m->has_colors) memcpy(m->colors, p + 56, 8);
	m->tileset = m->era % 8; /* the ERA handler 0x468b00 masks with 7 (expansion on) */
	return 0;
}

static int parse_rules(KpMission *m, KpBytes r)
{
	uint32_t pos = 0;
	int n = 0;
	while (pos < r.size) {
		uint32_t len;
		if (r.size - pos < 8) return -1;
		len = kp_le_u32(r.data + pos + 4);
		if (len > r.size - pos - 8) return -1;
		pos += 8 + len;
		n++;
	}
	m->rules = (KpMissionRule *)calloc(n ? n : 1, sizeof *m->rules);
	if (!m->rules) return -1;
	for (pos = 0; m->rule_count < n; m->rule_count++) {
		KpMissionRule *rule = &m->rules[m->rule_count];
		memcpy(rule->tag, r.data + pos, 4);
		rule->tag[4] = '\0';
		rule->data.size = kp_le_u32(r.data + pos + 4);
		rule->data.data = r.data + pos + 8;
		pos += 8 + rule->data.size;
	}
	return 0;
}

static int parse_sounds(KpMission *m, KpBytes w)
{
	uint32_t i, n;
	if (w.size < 2) return -1;
	n = kp_le_u16(w.data);
	if ((uint64_t)2 + (uint64_t)12 * n > w.size) return -1;
	m->sounds = (KpMissionSound *)calloc(n ? n : 1, sizeof *m->sounds);
	if (!m->sounds) return -1;
	for (i = 0; i < n; i++) {
		const uint8_t *a = w.data + 2 + 12 * i;
		KpMissionSound *s = &m->sounds[i];
		s->string_id = (uint16_t)kp_le_u16(a);
		s->index = (uint16_t)kp_le_u16(a + 2);
		s->length = kp_le_u32(a + 4);
		s->avg_bytes_per_sec = kp_le_u32(a + 8);
	}
	m->sound_count = (int)n;
	return 0;
}

static int parse_regions(KpMissionRegions *r, KpBytes s)
{
	if (s.size < 12) return -1;
	r->data = s;
	r->width = kp_le_u16(s.data);
	r->height = kp_le_u16(s.data + 2);
	r->count = kp_le_u16(s.data + 4);
	r->split_count = kp_le_u16(s.data + 6);
	r->pool_words = kp_le_u32(s.data + 8);
	{
		/* in 64 bits: the counts come from the file */
		uint64_t pool_at = 12 + (uint64_t)r->count * KP_REGION_SIZE;
		uint64_t raster_at = pool_at + 2 * (uint64_t)r->pool_words;
		uint64_t splits_at = raster_at + 2 * (uint64_t)r->width * r->height;
		if (splits_at + (uint64_t)r->split_count * KP_SPLIT_SIZE != s.size) return -1;
		r->regions_at = 12;
		r->pool_at = (uint32_t)pool_at;
		r->raster_at = (uint32_t)raster_at;
		r->splits_at = (uint32_t)splits_at;
	}
	return 0;
}

static int parse_contours(KpMissionContours *c, KpBytes s)
{
	uint32_t pos = 16;
	int d;
	if (s.size < 16) return -1;
	c->data = s;
	for (d = 0; d < 4; d++) {
		c->counts[d] = kp_le_u16(s.data + 2 * d);
		c->capacities[d] = kp_le_u16(s.data + 8 + 2 * d);
		c->at[d] = pos;
		pos += c->counts[d] * KP_CONTOUR_SIZE; /* at most 4 * 65535 * 8: no overflow */
	}
	return pos == s.size ? 0 : -1;
}

static int parse_packs(KpMission *m, KpBytes s)
{
	uint32_t pos = 1, i, n;
	if (s.size < 1) return -1;
	n = s.data[0];
	if (n > sizeof m->packs / sizeof m->packs[0]) return -1;
	for (i = 0; i < n; i++) {
		if (pos >= s.size || s.data[pos] > s.size - pos - 1) return -1;
		m->packs[i].size = s.data[pos];
		m->packs[i].data = s.data + pos + 1;
		pos += 1 + s.data[pos];
	}
	m->pack_count = (int)n;
	return 0;
}

int kp_mission_load(KpMission *m, const uint8_t *data, size_t size, const char *name, const KpKpfExpect *expect,
	KpError *err)
{
	KpKpf c;
	KpKpfExpect want;
	KpBytes meta, rules, sounds, regions, contours, packs;
	memset(m, 0, sizeof *m);
	m->blob = (uint8_t *)malloc(size ? size : 1);
	if (!m->blob) return fail(err, name, "out of memory");
	memcpy(m->blob, data, size);
	m->size = size;
	if (kp_kpf_parse(&c, m->blob, size, name, 0, err) != 0) {
		kp_mission_free(m);
		return -1;
	}
	memset(&want, 0, sizeof want);
	if (expect) want = *expect;
	want.kind = KP_MISSION_KIND;
	want.schema_version = KP_MISSION_SCHEMA;
	want.sections = SECTIONS;
	want.section_count = (int)(sizeof SECTIONS / sizeof SECTIONS[0]);
	if (kp_kpf_check(&c, &want, err) != 0) goto failed;
	if (section(&c, "META", &meta, err) || section(&c, "MTXM", &m->tiles, err) || section(&c, "MASK", &m->mask, err)
		|| section(&c, "SPRT", &m->sprites, err) || section(&c, "UNIT", &m->units, err)
		|| section(&c, "LOCS", &m->locations, err) || section(&c, "STRS", &m->strings, err)
		|| section(&c, "TRIG", &m->triggers, err) || section(&c, "MBRF", &m->briefing, err)
		|| section(&c, "UPRP", &m->properties, err) || section(&c, "RULE", &rules, err)
		|| section(&c, "WAVS", &sounds, err) || section(&c, "WALK", &m->walk, err)
		|| section(&c, "RGNS", &regions, err) || section(&c, "CONT", &contours, err)
		|| section(&c, "PACK", &packs, err)) {
		goto failed;
	}
	if (parse_meta(m, meta) != 0) {
		fail(err, name, "META section is malformed");
		goto failed;
	}
	if (parse_rules(m, rules) != 0) {
		fail(err, name, "RULE section is malformed");
		goto failed;
	}
	if (parse_sounds(m, sounds) != 0) {
		fail(err, name, "WAVS section is malformed");
		goto failed;
	}
	if (parse_regions(&m->regions, regions) != 0) {
		snprintf(err->msg, sizeof err->msg, "mission: RGNS size mismatch");
		goto failed;
	}
	if (parse_contours(&m->contours, contours) != 0) {
		snprintf(err->msg, sizeof err->msg, "mission: CONT size mismatch");
		goto failed;
	}
	if (parse_packs(m, packs) != 0) {
		fail(err, name, "PACK section is malformed");
		goto failed;
	}
	if (m->tiles.size != (uint64_t)2 * m->width * m->height) {
		snprintf(err->msg, sizeof err->msg, "mission: %s: MTXM size does not match %ux%u", name ? name : "<data>",
			(unsigned)m->width, (unsigned)m->height);
		goto failed;
	}
	kp_kpf_free(&c);
	return 0;
failed:
	kp_kpf_free(&c);
	kp_mission_free(m);
	return -1;
}

void kp_mission_free(KpMission *m)
{
	free(m->rules);
	free(m->sounds);
	free(m->blob);
	memset(m, 0, sizeof *m);
}
