/*
 * kp_convert hd-ui-table (ADR 0019, stage C6): the HD interface's placement table, defs/scr/hd-ui-<profile>.json,
 * from a StarCraft: Remastered install by the Python converter's heuristics (hd_ui.py until stage C6; the names below
 * are its functions'), which live here alone: where a GRP's HD frame lies when not at its usual place (place_frame:
 * its opaque texels' agreement with the SD frame's), which PCXs' HD art is the SD picture drawn again (same_picture:
 * their luminance gradients' correlation), where a console's centre piece lies (place_piece: its gradients against
 * the SD console's). The runtime and kp_convert repack lay the interface out by it (kp_hd_ui_scr.c). Built with -ffp-contract=off (CMakeLists.txt): the measures add up as
 * Python's doubles did.
 */
#include "kp_hd_ui_table.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kp_grp.h"
#include "kp_hd_scr.h"
#include "kp_hd_ui_scr.h"
#include "kp_json.h"
#include "kp_scr.h"
#include "kp_ui.h"
#include "kp_ui_data.h"

#define SAME_PICTURE 0.5  /* the least gradient correlation of a PCX's HD art with the SD picture (measured: the same
                           * pictures 0.76-1.0, those drawn anew -0.42-0.39) */
#define PLACE_MARGIN 0.3  /* how much better than its usual place another must agree to take it (measured on HD2: the
                           * frames the usual place misses gain 0.33-1.0, those drawn anew at most 0.18) */
#define PLACE_EXACT 0.95  /* an agreement that takes another place outright (the frame drawn again there) */
#define OPAQUE 128        /* an HD texel's alpha from which it counts as opaque */
#define GRADIENT_ALPHA 200 /* a texel's alpha from which its luminance enters a gradient */
#define PIECE_GRADIENTS 200 /* the fewest gradients a piece's offset is judged by */
#define KEY_SIZE 16

static int fail(KpError *err, const char *what, const char *why)
{
	snprintf(err->msg, sizeof err->msg, "hd-ui-table: %s: %s", what, why);
	return -1;
}

static double luma(double r, double g, double b) { return 0.3 * r + 0.59 * g + 0.11 * b; }

/* --- place_frame --- */

/* The texels two masks share, b moved by (dx, dy) over a (a byte a texel, 1 opaque). */
static int64_t overlap(const uint8_t *a, int32_t aw, int32_t ah, const uint8_t *b, int32_t bw, int32_t bh, int32_t dx,
	int32_t dy)
{
	int64_t total = 0;
	int32_t x, y;
	for (y = 0; y < bh; y++) {
		int32_t ay = y + dy;
		if (ay < 0 || ay >= ah) continue;
		for (x = 0; x < bw; x++) {
			int32_t ax = x + dx;
			if (b[(size_t)y * (size_t)bw + (size_t)x] && ax >= 0 && ax < aw) total += a[(size_t)ay * (size_t)aw + (size_t)ax];
		}
	}
	return total;
}

typedef struct {
	const uint8_t *a, *b;
	int32_t aw, ah, bw, bh;
	int64_t na, nb;
} Masks;

/* The masks' agreement at a place (intersection over union). */
static double agreement(const Masks *m, const int32_t at[2])
{
	int64_t shared = overlap(m->a, m->aw, m->ah, m->b, m->bw, m->bh, at[0], at[1]);
	return m->na + m->nb > shared ? (double)shared / (double)(m->na + m->nb - shared) : 0.0;
}

/* Where an HD frame (its RGBA, w x h) lies on the SD frame it replaces (mask: 1 opaque, sw x sh at (sx, sy) of a
 * canvas cw x ch): its usual place, or another of hd_ui.py's where its opaque texels agree PLACE_MARGIN better, or
 * PLACE_EXACT (1: moved). */
static int place_frame(const uint8_t *mask, int32_t sx, int32_t sy, int32_t sw, int32_t sh, int32_t cw, int32_t ch,
	const uint8_t *rgba, int32_t w, int32_t h, int32_t scale, int32_t out[2])
{
	int32_t places[KP_HD_UI_SCR_PLACES][2], usual, side, x, y, l, k, other = -1;
	uint8_t *a, *b;
	Masks m;
	double best = 0, there, here;
	usual = kp_hd_ui_scr_places(scale, sx, sy, sw, sh, cw, ch, w, h, places);
	side = usual == KP_HD_UI_SCR_ON_CANVAS ? h : w;
	a = (uint8_t *)calloc((size_t)(sw * scale) * (size_t)(sh * scale) + 1, 1);
	b = (uint8_t *)calloc((size_t)side * (size_t)h + 1, 1);
	if (!a || !b) {
		free(a);
		free(b);
		return -1;
	}
	memset(&m, 0, sizeof m);
	for (y = 0; y < sh * scale; y++) { /* the SD mask at the variant's scale */
		for (x = 0; x < sw * scale; x++) a[(size_t)y * (size_t)(sw * scale) + (size_t)x] = mask[(size_t)(y / scale) * (size_t)sw + (size_t)(x / scale)];
	}
	for (y = 0; y < h; y++) { /* the HD image's opaque texels, a wireframe's layers ORed */
		for (x = 0; x < w; x++) {
			if (rgba[((size_t)y * (size_t)w + (size_t)x) * 4 + 3] >= OPAQUE) b[(size_t)y * (size_t)side + (size_t)(x % side)] = 1;
		}
	}
	m.a = a;
	m.b = b;
	m.aw = sw * scale;
	m.ah = sh * scale;
	m.bw = side;
	m.bh = h;
	for (l = 0; l < m.aw * m.ah; l++) m.na += a[l];
	for (l = 0; l < m.bw * m.bh; l++) m.nb += b[l];
	for (k = 0; k < KP_HD_UI_SCR_PLACES; k++) { /* the best of the other places (the first of equals) */
		double v;
		if (places[k][0] == places[usual][0] && places[k][1] == places[usual][1]) continue;
		v = agreement(&m, places[k]);
		if (other < 0 || v > best) {
			other = k;
			best = v;
		}
	}
	there = other >= 0 ? best : 0;
	here = agreement(&m, places[usual]);
	free(a);
	free(b);
	if (other >= 0 && (there >= here + PLACE_MARGIN || (there >= PLACE_EXACT && PLACE_EXACT > here))) {
		out[0] = places[other][0];
		out[1] = places[other][1];
		return 1;
	}
	out[0] = places[usual][0];
	out[1] = places[usual][1];
	return 0;
}

/* --- same_picture --- */

/* The correlation of the luminance gradients of an SD picture (indices and palette) and its HD art centred on it
 * (every other logical pixel, the HD art sampled at it). */
static double same_picture(const uint8_t *pixels, const uint8_t *palette, int32_t sw, int32_t sh, const uint8_t *rgba,
	int32_t w, int32_t h, int32_t scale)
{
	int32_t ox, oy, x, y;
	double num = 0, sd_sum = 0, hd_sum = 0;
	kp_hd_ui_scr_centred(scale, sw, sh, w, h, &ox, &oy);
#define SD_LUMA(px, py) luma(palette[3 * pixels[(size_t)(py) * (size_t)sw + (size_t)(px)]], \
	palette[3 * pixels[(size_t)(py) * (size_t)sw + (size_t)(px)] + 1], palette[3 * pixels[(size_t)(py) * (size_t)sw + (size_t)(px)] + 2])
	for (y = 0; y < sh - 2; y += 2) {
		for (x = 0; x < sw - 2; x += 2) {
			int32_t hx0 = x * scale - ox, hy0 = y * scale - oy, hx1 = (x + 2) * scale - ox, hy2 = (y + 2) * scale - oy;
			double h0, h1, h2, s0, sx, sy, gx, gy;
			const uint8_t *t;
			if (hx0 < 0 || hx0 >= w || hy0 < 0 || hy0 >= h || hx1 < 0 || hx1 >= w || hy2 < 0 || hy2 >= h) continue;
			t = rgba + ((size_t)hy0 * (size_t)w + (size_t)hx0) * 4;
			h0 = luma(t[0], t[1], t[2]);
			t = rgba + ((size_t)hy0 * (size_t)w + (size_t)hx1) * 4;
			h1 = luma(t[0], t[1], t[2]);
			t = rgba + ((size_t)hy2 * (size_t)w + (size_t)hx0) * 4;
			h2 = luma(t[0], t[1], t[2]);
			s0 = SD_LUMA(x, y);
			sx = SD_LUMA(x + 2, y) - s0;
			sy = SD_LUMA(x, y + 2) - s0;
			gx = h1 - h0;
			gy = h2 - h0;
			num += sx * gx + sy * gy;
			sd_sum += sx * sx + sy * sy;
			hd_sum += gx * gx + gy * gy;
		}
	}
#undef SD_LUMA
	return sd_sum > 0 && hd_sum > 0 ? num / pow(sd_sum * hd_sum, 0.5) : 0.0;
}

/* --- place_piece --- */

/* The luminance gradients (right and down differences) of an RGBA image where a texel and its neighbours are opaque
 * enough (gx, gy and a flag a texel; malloc'd). */
static double *gradients(const uint8_t *rgba, int32_t w, int32_t h)
{
	double *l = (double *)malloc((size_t)w * (size_t)h * sizeof *l + 1), *g = (double *)calloc((size_t)w * (size_t)h * 3 + 1, sizeof *g);
	int32_t x, y;
	if (!l || !g) {
		free(l);
		free(g);
		return NULL;
	}
	for (y = 0; y < h; y++) {
		for (x = 0; x < w; x++) {
			const uint8_t *t = rgba + ((size_t)y * (size_t)w + (size_t)x) * 4;
			l[(size_t)y * (size_t)w + (size_t)x] = t[3] >= GRADIENT_ALPHA ? luma(t[0], t[1], t[2]) : -1.0;
		}
	}
	for (y = 0; y + 1 < h; y++) {
		for (x = 0; x + 1 < w; x++) {
			double c = l[(size_t)y * (size_t)w + (size_t)x], right = l[(size_t)y * (size_t)w + (size_t)x + 1];
			double down = l[(size_t)(y + 1) * (size_t)w + (size_t)x];
			double *o = g + ((size_t)y * (size_t)w + (size_t)x) * 3;
			if (c >= 0 && right >= 0 && down >= 0) {
				o[0] = right - c;
				o[1] = down - c;
				o[2] = 1;
			}
		}
	}
	free(l);
	return g;
}

/* An RGBA image averaged down by scale (hd_ui.py logical): malloc'd (w / scale) x (h / scale). */
static uint8_t *logical(const uint8_t *rgba, int32_t w, int32_t h, int32_t scale)
{
	int32_t lw = w / scale, lh = h / scale, x, y, dx, dy, c;
	uint8_t *out = (uint8_t *)calloc((size_t)lw * (size_t)lh * 4 + 1, 1); /* zeroed: an image narrower than scale has none */
	if (!out) return NULL;
	for (y = 0; y < lh; y++) {
		for (x = 0; x < lw; x++) {
			uint32_t acc[4] = { 0, 0, 0, 0 };
			for (dy = 0; dy < scale; dy++) {
				for (dx = 0; dx < scale; dx++) {
					for (c = 0; c < 4; c++) acc[c] += rgba[((size_t)(y * scale + dy) * (size_t)w + (size_t)(x * scale + dx)) * 4 + (size_t)c];
				}
			}
			for (c = 0; c < 4; c++) out[((size_t)y * (size_t)lw + (size_t)x) * 4 + (size_t)c] = (uint8_t)(acc[c] / (uint32_t)(scale * scale));
		}
	}
	return out;
}

/* The x offset of a console piece (logical, its top at row top) in the SD console where its gradients over the
 * centre group's columns agree best with the SD console's (their mean dot product), within KP_HD_UI_SCR_SEARCH of the
 * group's first column: 0, or -1 when no offset is judged by enough gradients. */
static int place_piece(const uint8_t *sd, int32_t sw, int32_t sh, const uint8_t *piece, int32_t pw, int32_t ph, int32_t top,
	int32_t *out)
{
	double *sg = gradients(sd, sw, sh), *pg = gradients(piece, pw, ph), best = -INFINITY;
	int32_t ox, x, y, found = 0;
	if (!sg || !pg) {
		free(sg);
		free(pg);
		return -1;
	}
	for (ox = KP_UI_CONSOLE_CUT_1 - KP_HD_UI_SCR_SEARCH; ox <= KP_UI_CONSOLE_CUT_1 + KP_HD_UI_SCR_SEARCH; ox++) {
		double total = 0, score;
		int64_t n = 0;
		for (y = 0; y < ph; y++) {
			for (x = 0; x < pw; x++) {
				const double *p = pg + ((size_t)y * (size_t)pw + (size_t)x) * 3, *s;
				int32_t at = x + ox, row = y + top;
				if (p[2] == 0 || at < KP_UI_CONSOLE_CUT_1 || at >= KP_UI_CONSOLE_CUT_2 || at < 0 || at >= sw || row < 0 || row >= sh) continue;
				s = sg + ((size_t)row * (size_t)sw + (size_t)at) * 3;
				if (s[2] == 0) continue;
				total += p[0] * s[0] + p[1] * s[1];
				n++;
			}
		}
		score = n > PIECE_GRADIENTS ? total / (double)n : -INFINITY;
		if (!found || score > best) {
			best = score;
			*out = ox;
			found = 1;
		}
	}
	free(sg);
	free(pg);
	return best == -INFINITY ? -1 : 0;
}

/* --- the table --- */

typedef struct {
	KpRes *r;
	const KpFiles *sd;
	int variant;
	int32_t scale;
	char (*files)[KP_HD_UI_SCR_NAME]; /* written, in order */
	int file_count, file_cap;
	KpBuf moved;      /* the moved frames' JSON rows */
	int moved_count, drawn_anew, kept;
	int32_t centre[3];
	KpError *err;
} Table;

/* A file the table takes (once), its content key. */
static int take(Table *t, const char *file)
{
	int k;
	for (k = 0; k < t->file_count; k++) {
		if (strcmp(t->files[k], file) == 0) return 0;
	}
	if (t->file_count == t->file_cap) {
		int cap = t->file_cap ? 2 * t->file_cap : 64;
		char(*g)[KP_HD_UI_SCR_NAME] = (char(*)[KP_HD_UI_SCR_NAME])realloc(t->files, (size_t)cap * sizeof *g);
		if (!g) return fail(t->err, file, "out of memory");
		t->files = g;
		t->file_cap = cap;
	}
	snprintf(t->files[t->file_count++], sizeof t->files[0], "%s", file);
	return 0;
}

/* A file of the install read whole: malloc'd, or NULL with err. */
static uint8_t *raw(Table *t, const char *file, size_t *size)
{
	char why[256];
	uint8_t *b = kp_res_raw_read(t->r, file, size, why, sizeof why);
	if (!b) fail(t->err, file, why[0] ? why : "not in the install");
	return b;
}

static int moved_row(Table *t, const char *path, size_t len, uint32_t frame, const int32_t at[2])
{
	char row[KP_HD_UI_PATH * 2 + 8];
	size_t i, n = 0;
	for (i = 0; i < len && n + 3 < sizeof row; i++) { /* the SD path lowercased, JSON-escaped */
		char c = path[i] >= 'A' && path[i] <= 'Z' ? (char)(path[i] - 'A' + 'a') : path[i];
		if (c == '\\' || c == '"') row[n++] = '\\';
		row[n++] = c;
	}
	row[n] = 0;
	kp_buf_printf(&t->moved, "%s    [\"%s\", %u, %d, %d]", t->moved_count ? ",\n" : "", row, (unsigned)frame, (int)at[0],
		(int)at[1]);
	t->moved_count++;
	return 0;
}

/* An SD GRP's frames' masks (1 opaque): its rows decoded, else a dialog GRP's plain rows (index 0 transparent); NULL
 * when it is neither (hd_ui.py sd_frames). */
static uint8_t **grp_masks(const uint8_t *grp, size_t size, uint32_t frames)
{
	uint8_t **m = (uint8_t **)calloc(frames + 1, sizeof *m);
	uint32_t k;
	int plain = 0;
	if (!m) return NULL;
	for (k = 0; k < frames && !plain; k++) {
		KpGrpFrame f;
		size_t i;
		kp_grp_frame(grp, k, &f);
		if (!(m[k] = (uint8_t *)malloc((size_t)f.w * (size_t)f.h + 1))
			|| kp_grp_decode(grp, size, k, NULL, m[k], NULL) != 0) {
			plain = 1;
			break;
		}
		for (i = 0; i < (size_t)f.w * f.h; i++) m[k][i] = m[k][i] != 0;
	}
	if (plain) {
		for (k = 0; k < frames; k++) {
			KpGrpFrame f;
			size_t i;
			kp_grp_frame(grp, k, &f);
			free(m[k]);
			m[k] = NULL;
			if ((uint64_t)f.offset + (uint64_t)f.w * f.h > size || !(m[k] = (uint8_t *)malloc((size_t)f.w * (size_t)f.h + 1))) break;
			for (i = 0; i < (size_t)f.w * f.h; i++) m[k][i] = grp[f.offset + i] != 0;
		}
		if (k < frames) {
			for (k = 0; k < frames; k++) free(m[k]);
			free(m);
			return NULL;
		}
	}
	return m;
}

static int grp_entry(Table *t, const char *path, size_t len, const char *file)
{
	uint8_t *sd, *hd, **masks;
	size_t sd_size, hd_size;
	uint32_t frames, cw, ch, k, count;
	size_t at = 8;
	int rc = -1;
	if (!(sd = kp_files_need(t->sd, path, &sd_size, t->err))) return -1;
	if (kp_grp_header(sd, sd_size, &frames, &cw, &ch) != 0 || !(masks = grp_masks(sd, sd_size, frames))) {
		free(sd);
		fprintf(stderr, "hd-ui-table: %s: not a GRP: left in SD\n", path);
		return 0;
	}
	if (!(hd = raw(t, file, &hd_size))) goto done;
	count = hd_size >= 8 ? kp_le_u16(hd + 4) : 0;
	if (hd_size < 8 || kp_le_u32(hd) != hd_size || count != frames) {
		fprintf(stderr, "hd-ui-table: %s has %u HD frames for %u SD ones: left in SD\n", file, (unsigned)count, (unsigned)frames);
		rc = 0;
		goto done;
	}
	for (k = 0; k < count; k++) {
		KpGrpFrame g;
		uint32_t length;
		int32_t w, h, place[2];
		uint8_t *rgba;
		int moved;
		if (at + 12 > hd_size || (length = kp_le_u32(hd + at + 8)) > hd_size - at - 12
			|| !(rgba = kp_hd_ui_scr_dds_rgba(hd + at + 12, length, &w, &h))) {
			fail(t->err, file, "an image truncated, or not BC1, BC3 or RGBA");
			goto done;
		}
		kp_grp_frame(sd, k, &g);
		moved = place_frame(masks[k], g.x, g.y, g.w, g.h, (int32_t)cw, (int32_t)ch, rgba, w, h, t->scale, place);
		free(rgba);
		if (moved < 0 || (moved && moved_row(t, path, len, k, place) != 0)) {
			if (moved < 0) fail(t->err, file, "out of memory");
			goto done;
		}
		at += 12 + length;
	}
	rc = take(t, file);
done:
	for (k = 0; k < frames; k++) free(masks[k]);
	free(masks);
	free(sd);
	free(hd);
	return rc;
}

static int pcx_entry(Table *t, const char *path, const char *file, int picture)
{
	uint8_t *raw_sd, *pixels = NULL, *hd = NULL, *rgba = NULL, palette[768];
	size_t sd_size, hd_size;
	int32_t sw, sh, w, h;
	int rc = -1, has_palette;
	if (!(raw_sd = kp_files_need(t->sd, path, &sd_size, t->err))) return -1;
	has_palette = kp_scr_pcx_palette(raw_sd, sd_size, palette) == 0;
	free(raw_sd);
	if (!(pixels = kp_files_pcx(t->sd, path, &sw, &sh, t->err)) || !(hd = raw(t, file, &hd_size))) goto done;
	if (!(rgba = kp_hd_ui_scr_dds_rgba(hd, hd_size, &w, &h))) {
		fail(t->err, file, "not a DDS image of BC1, BC3 or RGBA");
		goto done;
	}
	if (!has_palette || (!picture && same_picture(pixels, palette, sw, sh, rgba, w, h, t->scale) < SAME_PICTURE)) {
		t->drawn_anew++;
		rc = 0;
	} else {
		rc = take(t, file);
	}
done:
	free(pixels);
	free(hd);
	free(rgba);
	return rc;
}

static int console_entry(Table *t, int race)
{
	char sd_file[KP_HD_UI_SCR_NAME], file[KP_HD_UI_SCR_NAME];
	uint8_t *sd = NULL, *hd = NULL, *sd_rgba = NULL, *rgba = NULL, *piece = NULL;
	size_t size;
	int32_t sw, sh, w, h, k;
	int rc = -1;
	kp_hd_ui_scr_console_file(t->variant, race, -1, sd_file, sizeof sd_file);
	kp_hd_ui_scr_console_file(t->variant, race, 1, file, sizeof file);
	if (!(sd = raw(t, sd_file, &size)) || !(sd_rgba = kp_hd_ui_scr_dds_rgba(sd, size, &sw, &sh))
		|| !(hd = raw(t, file, &size)) || !(rgba = kp_hd_ui_scr_dds_rgba(hd, size, &w, &h))
		|| !(piece = logical(rgba, w, h, t->scale))) {
		if (!t->err->msg[0]) fail(t->err, file, "not a DDS image of BC1, BC3 or RGBA, or out of memory");
		goto done;
	}
	if (place_piece(sd_rgba, sw, sh, piece, w / t->scale, h / t->scale, sh - h / t->scale, &t->centre[race]) != 0) {
		fail(t->err, file, "a console piece matches no part of the SD console");
		goto done;
	}
	if (take(t, sd_file) != 0) goto done;
	for (k = 0; k < 3; k++) {
		kp_hd_ui_scr_console_file(t->variant, race, k, file, sizeof file);
		if (take(t, file) != 0) goto done;
	}
	rc = 0;
done:
	free(sd);
	free(hd);
	free(sd_rgba);
	free(rgba);
	free(piece);
	return rc;
}

/* One SD path of kp_ui_data_art_paths (hd_ui.py generate's loop). */
static int entry(void *ctx, const char *path, size_t len)
{
	Table *t = (Table *)ctx;
	char file[KP_HD_UI_SCR_NAME];
	int race, kind = kp_hd_ui_scr_classify(path, len, t->variant, file, sizeof file, &race);
	switch (kind) {
	case KP_HD_UI_SCR_CONSOLE: return console_entry(t, race);
	case KP_HD_UI_SCR_KEPT_SD: t->kept++; return 0;
	case KP_HD_UI_SCR_GRP: return kp_res_raw_held(t->r, file) ? grp_entry(t, path, len, file) : 0;
	case KP_HD_UI_SCR_PCX:
	case KP_HD_UI_SCR_PICTURE: return kp_res_raw_held(t->r, file) ? pcx_entry(t, path, file, kind == KP_HD_UI_SCR_PICTURE) : 0;
	default: return 0;
	}
}

char *kp_hd_ui_table_json(KpRes *r, const KpFiles *sd, size_t *size, KpError *err)
{
	KpBuf out;
	int v, k, rc = 0;
	kp_buf_init(&out);
	kp_buf_printf(&out, "{\n \"comment\": \"StarCraft: Remastered's HD interface over profile %s's (ADR 0019, stage C6; "
		"native/kp_app/kp_hd_ui_scr.h): per variant the HD files its layout takes (the pictures among them the SD ones "
		"drawn again) and their content keys (CASC's MD5 of their bytes), the GRP frames off their usual place (the SD "
		"path, the frame, x, y in HD pixels from the SD frame's top-left) and each console's centre piece's column "
		"(logical: zerg, terran, protoss). Written by kp_convert hd-ui-table DIR.\",\n \"profile\": \"%s\",\n",
		kp_res_profile(r)->id, kp_res_profile(r)->id);
	for (v = 0; v < KP_HD_SCR_VARIANTS && rc == 0; v++) {
		Table t;
		memset(&t, 0, sizeof t);
		t.r = r;
		t.sd = sd;
		t.variant = v;
		t.scale = kp_hd_scr_variant_scale(v);
		t.err = err;
		err->msg[0] = 0;
		if (kp_ui_data_art_paths(sd, entry, &t, err) != 0) {
			rc = -1;
		} else {
			kp_buf_printf(&out, " \"%s\": {\n  \"files\": [\n", kp_hd_scr_variant_name(v));
			for (k = 0; k < t.file_count && rc == 0; k++) {
				uint8_t key[KEY_SIZE];
				char hex[2 * KEY_SIZE + 1];
				int i;
				if (kp_res_raw_key(r, t.files[k], key) != 0) {
					rc = fail(err, t.files[k], "no content key");
					break;
				}
				for (i = 0; i < KEY_SIZE; i++) snprintf(hex + 2 * i, 3, "%02x", key[i]);
				kp_buf_printf(&out, "   [\"%s\", \"%s\"]%s\n", t.files[k], hex, k + 1 < t.file_count ? "," : "");
			}
			kp_buf_printf(&out, "  ],\n  \"moved\": [\n%s\n  ],\n  \"consoles\": [%d, %d, %d]\n }%s\n", kp_buf_text(&t.moved),
				(int)t.centre[0], (int)t.centre[1], (int)t.centre[2], v + 1 < KP_HD_SCR_VARIANTS ? "," : "");
			printf("hd-ui-table %s: %d files, %d frames moved, %d pictures drawn anew, %d kept in SD\n",
				kp_hd_scr_variant_name(v), t.file_count, t.moved_count, t.drawn_anew, t.kept);
		}
		free(t.files);
		kp_buf_free(&t.moved);
	}
	kp_buf_str(&out, "}\n");
	if (rc != 0) {
		kp_buf_free(&out);
		return NULL;
	}
	kp_buf_text(&out);
	*size = out.size;
	return out.data;
}
