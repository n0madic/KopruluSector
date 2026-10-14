/* grpmeta.kpd loader (kp_grpmeta.h). */
#include "kp_grpmeta.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NONE16 0xffffu

typedef struct {
	const uint8_t *data;
	uint32_t size, pos;
} Reader;

static int need(const Reader *r, uint32_t n) { return r->size - r->pos >= n; }

static int label(Reader *r, KpLabel *out)
{
	uint32_t len;
	if (!need(r, 1)) return -1;
	len = r->data[r->pos];
	if (r->size - r->pos - 1 < len) return -1;
	out->text = r->data + r->pos + 1;
	out->size = len;
	r->pos += 1 + len;
	return 0;
}

static int fail(KpError *err, const char *section)
{
	snprintf(err->msg, sizeof err->msg, "grpmeta: %s section is malformed", section);
	return -1;
}

static void *table(size_t count, size_t item)
{
	return calloc(count + 1, item); /* entry 0 unused */
}

int kp_grpmeta_load(KpGrpMeta *g, const uint8_t *data, size_t size, const char *name, const KpKpfExpect *expect,
	KpError *err)
{
	KpKpf c;
	KpKpfExpect want;
	const KpKpfSection *s;
	Reader r;
	uint32_t i;
	memset(g, 0, sizeof *g);
	g->blob = (uint8_t *)malloc(size ? size : 1);
	if (!g->blob) {
		snprintf(err->msg, sizeof err->msg, "grpmeta: out of memory");
		return -1;
	}
	memcpy(g->blob, data, size);
	g->size = size;
	if (kp_kpf_parse(&c, g->blob, size, name, 0, err) != 0) {
		kp_grpmeta_free(g);
		return -1;
	}
	memset(&want, 0, sizeof want);
	if (expect) want = *expect;
	want.kind = KP_GRPMETA_KIND;
	want.schema_version = KP_GRPMETA_SCHEMA;
	if (kp_kpf_check(&c, &want, err) != 0) goto failed;

	if (!(s = kp_kpf_require(&c, "FRMS", err))) goto failed;
	g->frms = s->data;
	g->frms_size = s->size;
	if (!(s = kp_kpf_require(&c, "OPAC", err))) goto failed;
	g->opac = s->data;
	g->opac_size = s->size;

	if (!(s = kp_kpf_require(&c, "TBLX", err))) goto failed;
	if (s->size < 2) goto bad_tblx;
	g->tbl_count = kp_le_u16(s->data);
	if ((uint64_t)s->size < 2 + 4 * (uint64_t)g->tbl_count) goto bad_tblx;
	g->grp_of_tbl = (uint16_t *)table(g->tbl_count, sizeof *g->grp_of_tbl);
	g->lo_of_tbl = (uint16_t *)table(g->tbl_count, sizeof *g->lo_of_tbl);
	if (!g->grp_of_tbl || !g->lo_of_tbl) goto oom;

	if (!(s = kp_kpf_require(&c, "ATLS", err))) goto failed;
	r.data = s->data;
	r.size = s->size;
	r.pos = 1;
	if (s->size < 1) goto bad_atls;
	g->atlas_count = s->data[0];
	if (!(g->atlases = (KpAtlas *)table(g->atlas_count, sizeof *g->atlases))) goto oom;
	for (i = 1; i <= g->atlas_count; i++) {
		if (!need(&r, 1)) goto bad_atls;
		g->atlases[i].mask = r.data[r.pos] & 1;
		r.pos++;
		if (label(&r, &g->atlases[i].pack) != 0 || label(&r, &g->atlases[i].name) != 0) goto bad_atls;
	}

	if (!(s = kp_kpf_require(&c, "GRPS", err))) goto failed;
	r.data = s->data;
	r.size = s->size;
	r.pos = 2;
	if (s->size < 2) goto bad_grps;
	g->grp_count = kp_le_u16(s->data);
	if (!(g->grps = (KpGrpEntry *)table(g->grp_count, sizeof *g->grps))) goto oom;
	for (i = 1; i <= g->grp_count; i++) {
		KpGrpEntry *e = &g->grps[i];
		if (label(&r, &e->path) != 0 || !need(&r, 7)) goto bad_grps;
		e->frames = (uint16_t)kp_le_u16(r.data + r.pos);
		e->width = (uint16_t)kp_le_u16(r.data + r.pos + 2);
		e->height = (uint16_t)kp_le_u16(r.data + r.pos + 4);
		e->atlas = r.data[r.pos + 6] + 1u;
		r.pos += 7;
		if (label(&r, &e->animation) != 0 || !need(&r, 4)) goto bad_grps;
		e->first_frame = kp_le_u32(r.data + r.pos);
		r.pos += 4;
	}

	if (!(s = kp_kpf_require(&c, "LOFS", err))) goto failed;
	r.data = s->data;
	r.size = s->size;
	r.pos = 2;
	if (s->size < 2) goto bad_lofs;
	g->lo_count = kp_le_u16(s->data);
	if (!(g->los = (KpLoEntry *)table(g->lo_count, sizeof *g->los))) goto oom;
	for (i = 1; i <= g->lo_count; i++) {
		KpLoEntry *e = &g->los[i];
		if (label(&r, &e->path) != 0 || !need(&r, 4)) goto bad_lofs;
		e->size = kp_le_u32(r.data + r.pos);
		r.pos += 4;
		if (!need(&r, e->size) || e->size < 8) goto bad_lofs;
		e->data = r.data + r.pos;
		e->frames = kp_le_u32(e->data);
		e->points = kp_le_u32(e->data + 4);
		r.pos += e->size;
	}

	if (kp_grpmeta_check(g, err) != 0) goto failed;
	/* TBLX entries name existing GRPs and LOs. */
	s = kp_kpf_find(&c, "TBLX");
	for (i = 1; i <= g->tbl_count; i++) {
		uint32_t grp = kp_le_u16(s->data + 4 * i - 2), lo = kp_le_u16(s->data + 4 * i);
		if (grp != NONE16) {
			if (grp >= g->grp_count) goto bad_tblx;
			g->grp_of_tbl[i] = (uint16_t)(grp + 1);
		}
		if (lo != NONE16) {
			if (lo >= g->lo_count) goto bad_tblx;
			g->lo_of_tbl[i] = (uint16_t)(lo + 1);
		}
	}
	kp_kpf_free(&c);
	return 0;
bad_tblx:
	fail(err, "TBLX");
	goto failed;
bad_atls:
	fail(err, "ATLS");
	goto failed;
bad_grps:
	fail(err, "GRPS");
	goto failed;
bad_lofs:
	fail(err, "LOFS");
	goto failed;
oom:
	snprintf(err->msg, sizeof err->msg, "grpmeta: out of memory");
failed:
	kp_kpf_free(&c);
	kp_grpmeta_free(g);
	return -1;
}

void kp_grpmeta_free(KpGrpMeta *g)
{
	free(g->grp_of_tbl);
	free(g->lo_of_tbl);
	free(g->atlases);
	free(g->grps);
	free(g->los);
	free(g->blob);
	memset(g, 0, sizeof *g);
}

void kp_grpmeta_frame(const KpGrpMeta *g, uint32_t entry, uint32_t frame, int *x, int *y, int *w, int *h)
{
	const uint8_t *f = g->frms + 4 + KP_GRPMETA_FRAME_RECORD * ((size_t)g->grps[entry].first_frame + frame);
	*x = f[0];
	*y = f[1];
	*w = f[2];
	*h = f[3];
}

/* IsGrpPixelOpaque 0x40fd50 over the opacity runs (bit 7 transparent, bits 0-6 the length): the class of the run at
 * which "x -= length while x > 0" stops; 0 outside the frame. */
int kp_grpmeta_pixel_opaque(const KpGrpMeta *g, uint32_t entry, uint32_t frame, int64_t x, int64_t y)
{
	const uint8_t *f = g->frms + 4 + KP_GRPMETA_FRAME_RECORD * ((size_t)g->grps[entry].first_frame + frame);
	uint32_t base = kp_le_u32(f + 4), pos;
	uint8_t run;
	if (x < 0 || y < 0 || x >= f[2] || y >= f[3]) return 0;
	pos = base + (uint32_t)(g->opac[base + 2 * (size_t)y] | g->opac[base + 2 * (size_t)y + 1] << 8);
	do {
		run = g->opac[pos++];
		x -= run & 0x7f;
	} while (x > 0);
	return (run & 0x80) == 0;
}

int kp_grpmeta_lo_point(const KpLoEntry *lo, uint32_t frame, int32_t point, int *x, int *y)
{
	int64_t at;
	if ((uint64_t)8 + 4 * (uint64_t)frame + 4 > lo->size) return 0;
	at = (int64_t)kp_le_u32(lo->data + 8 + 4 * (size_t)frame) + 2 * (int64_t)point;
	if (at < 0 || at + 2 > (int64_t)lo->size) return 0;
	*x = (int8_t)lo->data[at];
	*y = (int8_t)lo->data[at + 1];
	return 1;
}

int kp_grpmeta_alloc(KpGrpMeta *g, uint32_t tbl_count, uint32_t grp_count, uint32_t lo_count, size_t bytes,
	KpError *err)
{
	memset(g, 0, sizeof *g);
	g->tbl_count = tbl_count;
	g->grp_count = grp_count;
	g->lo_count = lo_count;
	g->grp_of_tbl = (uint16_t *)table(tbl_count, sizeof *g->grp_of_tbl);
	g->lo_of_tbl = (uint16_t *)table(tbl_count, sizeof *g->lo_of_tbl);
	g->atlases = (KpAtlas *)table(0, sizeof *g->atlases);
	g->grps = (KpGrpEntry *)table(grp_count, sizeof *g->grps);
	g->los = (KpLoEntry *)table(lo_count, sizeof *g->los);
	g->blob = (uint8_t *)calloc(bytes ? bytes : 1, 1);
	g->size = bytes;
	if (!g->grp_of_tbl || !g->lo_of_tbl || !g->atlases || !g->grps || !g->los || !g->blob) {
		kp_grpmeta_free(g);
		snprintf(err->msg, sizeof err->msg, "grpmeta: out of memory");
		return -1;
	}
	return 0;
}

int kp_grpmeta_check(const KpGrpMeta *g, KpError *err)
{
	uint32_t i;
	for (i = 1; i <= g->grp_count; i++) {
		const KpGrpEntry *e = &g->grps[i];
		if (4 + KP_GRPMETA_FRAME_RECORD * ((uint64_t)e->first_frame + e->frames) > g->frms_size) return fail(err, "FRMS");
	}
	return 0;
}
