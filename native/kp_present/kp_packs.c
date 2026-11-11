/*
 * The resource packs (docs/formats/packs.md; plan phase S, stage S3): a pack, packed from its GRPs when a mission
 * starts (kp_pack_build, kp_pages.c), holds its atlases' frame tables over its pages (the palette indices of
 * KP_PACK_PAGE_SIZE squared, 0 transparent); a sounds container (kind MSND: a language's speech) holds OGG sounds by
 * their id. The frame table puts every loaded atlas's frames behind one another: grpmeta's AFRM gives each GRP entry its
 * first frame within its atlas.
 */
#include "kp_present_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FRAME_RECORD 10
#define MSND_KIND "MSND"
#define MSND_SCHEMA 1

int kp_present_fail(KpError *err, const char *name, const char *msg)
{
	snprintf(err->msg, sizeof err->msg, "%s: %s", name, msg);
	return -1;
}

static int load_sounds(KpPack *p, const KpKpfSection *s, const char *name, KpError *err)
{
	size_t pos = 2;
	int k, count;
	if (s->size < 2) return kp_present_fail(err, name, "SNDS truncated");
	count = (int)kp_le_u16(s->data);
	p->sounds = (KpPackSound *)calloc((size_t)(count ? count : 1), sizeof *p->sounds);
	if (!p->sounds) return kp_present_fail(err, name, "out of memory");
	for (k = 0; k < count; k++) {
		uint32_t size;
		if (pos + 6 > s->size) return kp_present_fail(err, name, "SNDS truncated");
		p->sounds[k].id = (uint16_t)kp_le_u16(s->data + pos);
		size = kp_le_u32(s->data + pos + 2);
		if (size > s->size - (pos + 6)) return kp_present_fail(err, name, "SNDS truncated");
		p->sounds[k].ogg = s->data + pos + 6;
		p->sounds[k].size = size;
		pos += 6 + size;
	}
	p->sound_count = count;
	return 0;
}

int kp_mission_sounds_load(KpPack *p, const uint8_t *data, size_t size, const char *name, KpError *err)
{
	KpKpf c;
	KpKpfExpect expect;
	const KpKpfSection *snds;
	int rc = -1;
	memset(p, 0, sizeof *p);
	if (kp_kpf_parse(&c, data, size, name, 0, err) != 0) return -1;
	memset(&expect, 0, sizeof expect);
	expect.kind = MSND_KIND;
	expect.schema_version = MSND_SCHEMA;
	if (kp_kpf_check(&c, &expect, err) == 0 && (snds = kp_kpf_require(&c, "SNDS", err)) != NULL) {
		rc = load_sounds(p, snds, name, err);
	}
	kp_kpf_free(&c);
	if (rc != 0) kp_pack_free(p);
	return rc;
}

void kp_pack_free(KpPack *p)
{
	free(p->atlases);
	free(p->sounds);
	free(p->owned);
	memset(p, 0, sizeof *p);
}

void kp_pack_frame(const KpPack *p, uint32_t i, KpPackFrame *out)
{
	const uint8_t *r = p->frames + (size_t)i * FRAME_RECORD;
	out->page = (uint16_t)kp_le_u16(r);
	out->x = (uint16_t)kp_le_u16(r + 2);
	out->y = (uint16_t)kp_le_u16(r + 4);
	out->w = (uint16_t)kp_le_u16(r + 6);
	out->h = (uint16_t)kp_le_u16(r + 8);
}

const KpPackSound *kp_pack_sound(const KpPack *p, uint32_t id)
{
	int lo = 0, hi = p->sound_count - 1;
	while (lo <= hi) { /* sorted by id */
		int mid = (lo + hi) / 2;
		if (p->sounds[mid].id == id) return &p->sounds[mid];
		if (p->sounds[mid].id < id) lo = mid + 1;
		else hi = mid - 1;
	}
	return NULL;
}

/* --- the frame table --- */

int kp_grpmeta_afrm(const KpGrpMeta *g, uint32_t **out, KpError *err)
{
	uint32_t *used, e;
	*out = NULL;
	/* converter/kpf/kpd.py atlas_frame_offsets: the frames of the atlas's earlier entries, in GRPS order */
	if (!(used = (uint32_t *)calloc((size_t)g->atlas_count + 1, sizeof *used))
		|| !(*out = (uint32_t *)malloc(sizeof **out * (g->grp_count ? g->grp_count : 1)))) {
		free(used);
		snprintf(err->msg, sizeof err->msg, "grpmeta: out of memory");
		return -1;
	}
	for (e = 1; e <= g->grp_count; e++) {
		uint32_t a = g->grps[e].atlas <= g->atlas_count ? g->grps[e].atlas : 0;
		(*out)[e - 1] = used[a];
		used[a] += g->grps[e].frames;
	}
	free(used);
	return 0;
}

static int label_is(const KpLabel *l, const char *s) { return strlen(s) == l->size && memcmp(l->text, s, l->size) == 0; }

int kp_pack_atlas_find(const KpGrpMeta *g, uint32_t a, const KpPack *const *packs, const char *const *pack_names,
	int pack_count, int *k, int *j)
{
	for (*k = 0; *k < pack_count; (*k)++) {
		if (!label_is(&g->atlases[a].pack, pack_names[*k])) continue;
		for (*j = 0; *j < packs[*k]->atlas_count; (*j)++) {
			if (label_is(&g->atlases[a].name, packs[*k]->atlases[*j].name)) return 1;
		}
	}
	return 0;
}

int kp_frame_table_build(KpFrameTable *t, const KpGrpMeta *g, const uint32_t *afrm, const KpPack *const *packs,
	const char *const *pack_names, int pack_count, KpError *err)
{
	int32_t total = 0, pages = 0, *page_base;
	uint32_t a;
	int k;
	memset(t, 0, sizeof *t);
	t->atlas_count = (int32_t)g->atlas_count;
	t->atlas_base = (int32_t *)malloc(sizeof *t->atlas_base * (g->atlas_count + 1));
	t->atlas_in = (int32_t *)malloc(sizeof *t->atlas_in * 2 * (g->atlas_count + 1));
	page_base = (int32_t *)malloc(sizeof *page_base * (size_t)(pack_count ? pack_count : 1));
	if (!t->atlas_base || !t->atlas_in || !page_base) {
		free(page_base);
		kp_frame_table_free(t);
		snprintf(err->msg, sizeof err->msg, "frame table: out of memory");
		return -1;
	}
	for (k = 0; k < pack_count; k++) {
		page_base[k] = pages;
		pages += packs[k]->page_count;
	}
	t->page_count = pages;
	t->pack_count = pack_count;
	/* the atlases in grpmeta order, each found in its pack */
	for (a = 1; a <= g->atlas_count; a++) {
		int j;
		t->atlas_base[a] = -1;
		if (kp_pack_atlas_find(g, a, packs, pack_names, pack_count, &k, &j)) {
			t->atlas_base[a] = total;
			t->atlas_in[2 * a] = k;
			t->atlas_in[2 * a + 1] = j;
			total += (int32_t)packs[k]->atlases[j].count;
		}
	}
	t->empty = total;
	t->height = ((total + 1) * 2 + KP_FRAME_TABLE_WIDTH - 1) / KP_FRAME_TABLE_WIDTH;
	t->texels = (float *)calloc((size_t)KP_FRAME_TABLE_WIDTH * (size_t)t->height * 4, sizeof *t->texels);
	if (!t->texels) {
		free(page_base);
		kp_frame_table_free(t);
		snprintf(err->msg, sizeof err->msg, "frame table: out of memory");
		return -1;
	}
	for (a = 1; a <= g->atlas_count; a++) {
		const KpPackAtlas *pa;
		uint32_t f;
		if (t->atlas_base[a] < 0) continue;
		k = t->atlas_in[2 * a];
		pa = &packs[k]->atlases[t->atlas_in[2 * a + 1]];
		for (f = 0; f < pa->count; f++) {
			KpPackFrame fr;
			float *o = &t->texels[(size_t)(t->atlas_base[a] + (int32_t)f) * 8];
			kp_pack_frame(packs[k], pa->first + f, &fr);
			o[0] = fr.x;
			o[1] = fr.y;
			o[2] = fr.w;
			o[3] = fr.h;
			o[4] = (float)(page_base[k] + fr.page);
		}
	}
	free(page_base);
	t->afrm = afrm;
	t->grp_count = g->grp_count;
	return 0;
}

void kp_frame_table_free(KpFrameTable *t)
{
	free(t->texels);
	free(t->atlas_base);
	free(t->atlas_in);
	memset(t, 0, sizeof *t);
}

int32_t kp_frame_table_frame(const KpFrameTable *t, const KpGrpMeta *g, int32_t grp, int32_t frame)
{
	int32_t atlas, base;
	if (grp == KP_GRP_VIRTUAL) return t->empty;
	if (grp < 1 || (uint32_t)grp > g->grp_count) return -1;
	atlas = (int32_t)g->grps[grp].atlas;
	if (atlas < 1 || atlas > t->atlas_count || (base = t->atlas_base[atlas]) < 0) return -1;
	return base + (int32_t)t->afrm[grp - 1] + frame;
}
