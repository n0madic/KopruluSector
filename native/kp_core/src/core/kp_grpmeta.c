/* grpmeta.kpd loader (kp_grpmeta.h). */
#include "kp_grpmeta.h"

#include "kp_grp.h"

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

/* --- grpmeta from the original files (ADR 0018, stage D3): converter/kpf/kpd.py grp_set and build_grpmeta --- */

#define IMAGES_TBL "arr\\images.tbl"
#define ATLAS_AREA_BUDGET 11500000u /* converter/packs.py: frame-rectangle pixels per atlas */
#define PACKS 7

static const char *const PACK_NAMES[PACKS] = { "terran", "protoss", "zerg", "neutral", "effects", "doodads", "other" };
/* the images.dat columns naming LO files (kpd.LO_COLUMNS) */
static const char *const LO_COLUMNS[] = { "attack_overlay", "damage_overlay", "special_overlay", "landing_dust_overlay",
	"lift_off_dust_overlay", "shield_overlay" };

static int from_fail(KpError *err, const char *what)
{
	snprintf(err->msg, sizeof err->msg, "grpmeta: %s", what);
	return -1;
}

/* lo.check: the header, every frame's offset, its points inside the file */
static int lo_check(const uint8_t *lo, size_t size)
{
	uint32_t frames, points, f;
	if (size < 8) return -1;
	frames = kp_le_u32(lo);
	points = kp_le_u32(lo + 4);
	if (8 + 4 * (uint64_t)frames > size) return -1;
	for (f = 0; f < frames; f++)
		if ((uint64_t)kp_le_u32(lo + 8 + 4 * (size_t)f) + 2 * (uint64_t)points > size) return -1;
	return 0;
}

/* Python's str.lower on a latin-1 byte */
static uint8_t latin1_lower(uint8_t c)
{
	if (c >= 'A' && c <= 'Z') return (uint8_t)(c + 32);
	if (c >= 0xc0 && c <= 0xde && c != 0xd7) return (uint8_t)(c + 32);
	return c;
}

static int starts_lower(const uint8_t *s, uint32_t n, const char *prefix)
{
	uint32_t k, m = (uint32_t)strlen(prefix);
	if (n < m) return 0;
	for (k = 0; k < m; k++)
		if (latin1_lower(s[k]) != (uint8_t)prefix[k]) return 0;
	return 1;
}

/* converter/packs.py pack_of_grp */
static int pack_of_grp(const uint8_t *path, uint32_t n)
{
	int k;
	static const char *const RACES[4] = { "unit\\terran\\", "unit\\protoss\\", "unit\\zerg\\", "unit\\neutral\\" };
	for (k = 0; k < 4; k++)
		if (starts_lower(path, n, RACES[k])) return k;
	if (starts_lower(path, n, "unit\\thingy\\tileset\\")) return 5;
	if (starts_lower(path, n, "unit\\thingy\\") || starts_lower(path, n, "unit\\bullet\\")) return 4;
	return 6;
}

typedef struct {
	uint8_t *data; /* the file */
	size_t size;
	uint32_t name; /* images.tbl's entry (1-based) of its first index */
	uint32_t frames, width, height;
	uint64_t area, opac;
	int pack, number, opaque_zero; /* number: 0 the pack's one atlas, else pack_<number> */
	uint32_t atlas; /* 1-based */
} GrpFile;

/* The deduplicated files of a set of images.tbl indices: each index's file position (0-based) and the files' first
 * indices in order (kpd.grp_set dedupe: "unit\\" + the entry, compared lower case). */
static int dedupe(const uint8_t *used, uint32_t count, const uint8_t *const *names, const uint32_t *lens,
	uint16_t *position, uint32_t *first, uint32_t *n_first)
{
	uint32_t i, k, j;
	*n_first = 0;
	for (i = 1; i <= count; i++) {
		if (!used[i]) continue;
		for (k = 0; k < *n_first; k++) {
			uint32_t o = first[k];
			if (lens[o - 1] != lens[i - 1]) continue;
			for (j = 0; j < lens[i - 1] && latin1_lower(names[o - 1][j]) == latin1_lower(names[i - 1][j]); j++) {
			}
			if (j == lens[i - 1]) break;
		}
		if (k == *n_first) {
			if (k >= 0xffff) return -1;
			first[(*n_first)++] = i;
		}
		position[i] = (uint16_t)k;
	}
	return 0;
}

int kp_grpmeta_from_files(KpGrpMeta *g, const KpFiles *files, const KpDat *dat, KpError *err)
{
	static const char UNIT[] = "unit\\";
	const KpDatColumn *grp_file = kp_dat_column(dat, "images", "grp_file");
	uint8_t *tbl = NULL, *grp_used = NULL, *lo_used = NULL;
	const uint8_t **names = NULL;
	uint32_t *lens = NULL, *grp_first = NULL, *lo_first = NULL, count, n_grps = 0, n_los = 0, i, k;
	uint16_t *grp_pos = NULL, *lo_pos = NULL;
	GrpFile *gf = NULL;
	uint8_t **lo_data = NULL;
	size_t *lo_size = NULL, tbl_size, labels = 0, frames = 0, opac = 0, lo_bytes = 0, at;
	uint64_t pack_area[PACKS] = { 0 };
	uint32_t atlas_of[PACKS * 64];
	int a, atlas_count = 0, rc = -1;
	memset(g, 0, sizeof *g);
	memset(atlas_of, 0, sizeof atlas_of);
	if (!grp_file) return from_fail(err, "images.dat has no grp_file column");
	if (!(tbl = kp_files_need(files, IMAGES_TBL, &tbl_size, err))) return -1;
	if (tbl_size < 2 || 2 + 2 * (size_t)kp_le_u16(tbl) > tbl_size) {
		from_fail(err, "arr\\images.tbl is malformed");
		goto done;
	}
	count = kp_le_u16(tbl);
	names = (const uint8_t **)calloc(count + 1, sizeof *names);
	lens = (uint32_t *)calloc(count + 1, sizeof *lens);
	grp_used = (uint8_t *)calloc(count + 1, 1);
	lo_used = (uint8_t *)calloc(count + 1, 1);
	grp_pos = (uint16_t *)calloc(count + 1, sizeof *grp_pos);
	lo_pos = (uint16_t *)calloc(count + 1, sizeof *lo_pos);
	grp_first = (uint32_t *)calloc(count + 1, sizeof *grp_first);
	lo_first = (uint32_t *)calloc(count + 1, sizeof *lo_first);
	if (!names || !lens || !grp_used || !lo_used || !grp_pos || !lo_pos || !grp_first || !lo_first) goto oom;
	for (i = 0; i < count; i++) { /* tbl.decode_tbl: up to the first NUL */
		size_t off = kp_le_u16(tbl + 2 + 2 * i), end = off;
		while (end < tbl_size && tbl[end]) end++;
		names[i] = tbl + (off < tbl_size ? off : tbl_size);
		lens[i] = (uint32_t)(end > off ? end - off : 0);
	}
	/* the indices images.dat uses as GRP and as LO files */
	for (i = 0; i < (uint32_t)grp_file->count; i++) {
		uint32_t v = kp_dat_value(grp_file, (uint32_t)grp_file->first_id * grp_file->per_id + i);
		if (v == 0) continue;
		if (v > count) {
			snprintf(err->msg, sizeof err->msg, "grpmeta: images.tbl index %u out of range 1..%u", (unsigned)v, (unsigned)count);
			goto done;
		}
		grp_used[v] = 1;
	}
	for (k = 0; k < sizeof LO_COLUMNS / sizeof LO_COLUMNS[0]; k++) {
		const KpDatColumn *c = kp_dat_column(dat, "images", LO_COLUMNS[k]);
		if (!c) {
			snprintf(err->msg, sizeof err->msg, "grpmeta: images.dat has no %s column", LO_COLUMNS[k]);
			goto done;
		}
		for (i = 0; i < (uint32_t)c->count; i++) {
			uint32_t v = kp_dat_value(c, (uint32_t)c->first_id * c->per_id + i);
			if (v == 0) continue;
			if (v > count) {
				snprintf(err->msg, sizeof err->msg, "grpmeta: images.tbl index %u out of range 1..%u", (unsigned)v,
					(unsigned)count);
				goto done;
			}
			lo_used[v] = 1;
		}
	}
	if (dedupe(grp_used, count, names, lens, grp_pos, grp_first, &n_grps) != 0
		|| dedupe(lo_used, count, names, lens, lo_pos, lo_first, &n_los) != 0) {
		from_fail(err, "too many files");
		goto done;
	}
	gf = (GrpFile *)calloc(n_grps ? n_grps : 1, sizeof *gf);
	lo_data = (uint8_t **)calloc(n_los ? n_los : 1, sizeof *lo_data);
	lo_size = (size_t *)calloc(n_los ? n_los : 1, sizeof *lo_size);
	if (!gf || !lo_data || !lo_size) goto oom;
	/* the GRPs, decoded (grp.decode's checks), their areas and opacity blocks sized */
	for (k = 0; k < n_grps; k++) {
		GrpFile *e = &gf[k];
		uint32_t n = grp_first[k], fr;
		char path[300];
		if (lens[n - 1] > 250) {
			from_fail(err, "a GRP path longer than 255 bytes");
			goto done;
		}
		memcpy(path, UNIT, 5);
		memcpy(path + 5, names[n - 1], lens[n - 1]);
		path[5 + lens[n - 1]] = '\0';
		e->name = n;
		if (!(e->data = kp_files_need(files, path, &e->size, err))) goto done;
		if (kp_grp_header(e->data, e->size, &e->frames, &e->width, &e->height) != 0) goto bad_grp;
		for (fr = 0; fr < e->frames; fr++) {
			KpGrpFrame r;
			int zero;
			int64_t o;
			if (kp_grp_decode(e->data, e->size, fr, NULL, NULL, &zero) != 0) goto bad_grp;
			e->opaque_zero |= zero;
			kp_grp_frame(e->data, fr, &r);
			e->area += (uint64_t)(r.w ? r.w : 1) * (r.h ? r.h : 1);
			if ((o = kp_grp_opacity(e->data, e->size, fr, NULL, 0)) < 0) goto bad_grp;
			e->opac += (uint64_t)o;
		}
		e->pack = pack_of_grp((const uint8_t *)path, 5 + lens[n - 1]);
		pack_area[e->pack] += e->area;
		labels += 2u * (5 + lens[n - 1]) + 2; /* the path and its animation id */
		frames += e->frames;
		opac += (size_t)e->opac;
		continue;
	bad_grp:
		snprintf(err->msg, sizeof err->msg, "grpmeta: %s is malformed", path);
		goto done;
	}
	for (k = 0; k < n_los; k++) {
		uint32_t n = lo_first[k];
		char path[300];
		if (lens[n - 1] > 250) {
			from_fail(err, "an LO path longer than 255 bytes");
			goto done;
		}
		memcpy(path, UNIT, 5);
		memcpy(path + 5, names[n - 1], lens[n - 1]);
		path[5 + lens[n - 1]] = '\0';
		if (!(lo_data[k] = kp_files_need(files, path, &lo_size[k], err))) goto done;
		if (lo_check(lo_data[k], lo_size[k]) != 0) {
			snprintf(err->msg, sizeof err->msg, "grpmeta: %s is malformed", path);
			goto done;
		}
		labels += 6 + lens[n - 1];
		lo_bytes += lo_size[k];
	}
	/* packs.assign_atlases: a pack over the budget cut into numbered atlases along images.tbl order */
	{
		uint64_t used[PACKS] = { 0 };
		int number[PACKS];
		for (a = 0; a < PACKS; a++) number[a] = pack_area[a] <= ATLAS_AREA_BUDGET ? 0 : 1;
		for (k = 0; k < n_grps; k++) {
			GrpFile *e = &gf[k];
			if (number[e->pack]) {
				if (used[e->pack] && used[e->pack] + e->area > ATLAS_AREA_BUDGET) {
					number[e->pack]++;
					used[e->pack] = 0;
				}
				used[e->pack] += e->area;
				if (number[e->pack] >= 64) {
					from_fail(err, "too many atlases");
					goto done;
				}
			}
			e->number = number[e->pack];
			if (!atlas_of[e->pack * 64 + e->number]) atlas_of[e->pack * 64 + e->number] = (uint32_t)++atlas_count;
			e->atlas = atlas_of[e->pack * 64 + e->number];
		}
	}
	/* the tables and the blob: the labels, FRMS, OPAC, the LO files */
	g->tbl_count = count;
	g->grp_count = n_grps;
	g->lo_count = n_los;
	g->atlas_count = (uint32_t)atlas_count;
	g->grp_of_tbl = (uint16_t *)calloc(count + 1, sizeof *g->grp_of_tbl);
	g->lo_of_tbl = (uint16_t *)calloc(count + 1, sizeof *g->lo_of_tbl);
	g->atlases = (KpAtlas *)calloc((size_t)atlas_count + 1, sizeof *g->atlases);
	g->grps = (KpGrpEntry *)calloc(n_grps + 1, sizeof *g->grps);
	g->los = (KpLoEntry *)calloc(n_los + 1, sizeof *g->los);
	g->size = labels + (size_t)atlas_count * 32 + 4 + KP_GRPMETA_FRAME_RECORD * frames + opac + lo_bytes;
	g->blob = (uint8_t *)calloc(g->size ? g->size : 1, 1);
	if (!g->grp_of_tbl || !g->lo_of_tbl || !g->atlases || !g->grps || !g->los || !g->blob) goto oom;
	for (i = 1; i <= count; i++) {
		if (grp_used[i]) g->grp_of_tbl[i] = (uint16_t)(grp_pos[i] + 1);
		if (lo_used[i]) g->lo_of_tbl[i] = (uint16_t)(lo_pos[i] + 1);
	}
	at = 0;
	for (k = 0; k < n_grps; k++) { /* the atlases in first-use order: their pack, name and mask flag */
		GrpFile *e = &gf[k];
		KpAtlas *t = &g->atlases[e->atlas];
		if (!t->pack.text) {
			const char *pack = PACK_NAMES[e->pack];
			int len;
			t->pack.text = g->blob + at;
			t->pack.size = (uint32_t)strlen(pack);
			memcpy(g->blob + at, pack, t->pack.size);
			at += t->pack.size;
			t->name.text = g->blob + at;
			len = e->number ? snprintf((char *)g->blob + at, 32 - t->pack.size, "%s_%d", pack, e->number)
				: snprintf((char *)g->blob + at, 32 - t->pack.size, "%s", pack);
			t->name.size = (uint32_t)len;
			at += (size_t)(32 - t->pack.size); /* the room reserved per atlas */
		}
		t->mask |= e->opaque_zero;
	}
	{
		uint8_t *frms = g->blob + at, *op;
		uint32_t frame_count = 0, fr;
		size_t opac_at = 0;
		g->frms = frms;
		g->frms_size = (uint32_t)(4 + KP_GRPMETA_FRAME_RECORD * frames);
		at += g->frms_size;
		op = g->blob + at;
		g->opac = op;
		g->opac_size = (uint32_t)opac;
		at += opac;
		frms[0] = (uint8_t)frames;
		frms[1] = (uint8_t)(frames >> 8);
		frms[2] = (uint8_t)(frames >> 16);
		frms[3] = (uint8_t)(frames >> 24);
		for (k = 0; k < n_grps; k++) {
			GrpFile *e = &gf[k];
			KpGrpEntry *ge = &g->grps[k + 1];
			uint32_t n = e->name, len = 5 + lens[n - 1], j;
			ge->path.text = g->blob + at;
			ge->path.size = len;
			memcpy(g->blob + at, UNIT, 5);
			memcpy(g->blob + at + 5, names[n - 1], lens[n - 1]);
			at += len;
			/* atlas.animation_id: lower case, "unit\\" and ".grp" removed, '\\' as '_' */
			ge->animation.text = g->blob + at;
			{
				uint32_t from = starts_lower(ge->path.text, len, "unit\\") ? 5 : 0, to = len;
				if (to - from >= 4 && latin1_lower(ge->path.text[to - 4]) == '.' && latin1_lower(ge->path.text[to - 3]) == 'g'
					&& latin1_lower(ge->path.text[to - 2]) == 'r' && latin1_lower(ge->path.text[to - 1]) == 'p')
					to -= 4;
				for (j = from; j < to; j++) {
					uint8_t c = latin1_lower(ge->path.text[j]);
					g->blob[at++] = c == '\\' ? '_' : c;
				}
				ge->animation.size = to - from;
			}
			ge->frames = (uint16_t)e->frames;
			ge->width = (uint16_t)e->width;
			ge->height = (uint16_t)e->height;
			ge->atlas = e->atlas;
			ge->first_frame = frame_count;
			for (fr = 0; fr < e->frames; fr++) {
				KpGrpFrame r;
				uint8_t *rec = frms + 4 + KP_GRPMETA_FRAME_RECORD * (size_t)(frame_count + fr);
				int64_t o;
				kp_grp_frame(e->data, fr, &r);
				rec[0] = r.x;
				rec[1] = r.y;
				rec[2] = r.w;
				rec[3] = r.h;
				rec[4] = (uint8_t)opac_at;
				rec[5] = (uint8_t)(opac_at >> 8);
				rec[6] = (uint8_t)(opac_at >> 16);
				rec[7] = (uint8_t)(opac_at >> 24);
				o = kp_grp_opacity(e->data, e->size, fr, op + opac_at, opac - opac_at);
				opac_at += (size_t)o;
			}
			frame_count += e->frames;
		}
	}
	for (k = 0; k < n_los; k++) {
		KpLoEntry *e = &g->los[k + 1];
		uint32_t n = lo_first[k], len = 5 + lens[n - 1];
		e->path.text = g->blob + at;
		e->path.size = len;
		memcpy(g->blob + at, UNIT, 5);
		memcpy(g->blob + at + 5, names[n - 1], lens[n - 1]);
		at += len;
		e->data = g->blob + at;
		e->size = (uint32_t)lo_size[k];
		memcpy(g->blob + at, lo_data[k], lo_size[k]);
		at += lo_size[k];
		e->frames = kp_le_u32(e->data);
		e->points = kp_le_u32(e->data + 4);
	}
	g->size = at; /* what the tables use of the room reserved (the CRC of the shared data reads it) */
	rc = kp_grpmeta_check(g, err);
	goto done;
oom:
	snprintf(err->msg, sizeof err->msg, "grpmeta: out of memory");
done:
	if (rc != 0) kp_grpmeta_free(g);
	for (k = 0; gf && k < n_grps; k++) free(gf[k].data);
	for (k = 0; lo_data && k < n_los; k++) free(lo_data[k]);
	free(gf);
	free(lo_data);
	free(lo_size);
	free(names);
	free(lens);
	free(grp_used);
	free(lo_used);
	free(grp_pos);
	free(lo_pos);
	free(grp_first);
	free(lo_first);
	free(tbl);
	return rc;
}
