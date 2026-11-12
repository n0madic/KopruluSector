/*
 * The HD interface from a StarCraft: Remastered install (ADR 0019, stage C6; kp_hd_ui_scr.h): the Python converter's
 * layout (hd_ui.py until stage C6; the names below are its functions') with its decisions (where a frame moved, which
 * pictures are the SD ones drawn again, where a console's centre piece lies) read from the placement table, and the
 * consoles' CONW. Built with -ffp-contract=off (CMakeLists.txt): a wing tile's crossfade rounds as Python's doubles
 * did.
 */
#include "kp_hd_ui_scr.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kp_grp.h"
#include "kp_hd_scr.h"
#include "kp_kpf.h"
#include "kp_scr.h"
#include "kp_ui.h"
#include "kp_ui_data.h"

#define DDS_HEADER 128
#define DDS_PF_FOURCC 0x4
#define DDS_PF_RGB 0x40
#define CONTAINER_HEADER 8   /* a *.dds.grp: u32 size, u16 count, u16 version */
#define CONTAINER_ENTRY 12   /* per image: u32 0, u16 width, u16 height, u32 size, then the DDS */
#define ASSET_RECORD (KP_HD_UI_PATH + 8)
#define FRAME_RECORD 18
#define HEAD_SIZE 7
#define KEY_SIZE 16
#define RACES 3
#define PIECES 3
#define TILES 2
#define WIDE 853           /* the screen width Remastered's console pieces are drawn for (16:9 at 480 rows) */
#define SCREEN_H 480
#define TILE_BLEND 12      /* logical columns a tile's end crossfades into its start */
#define PLAN_SCALE 2       /* the variant CONW's art comes from (HD2: every install has it) */

/* The consoles: their PCX stems, races, and the wings' part a tile repeats (logical columns of the left and the right
 * piece: past the junction with the block beside it, as far as the art runs) */
static const struct {
	const char *stem, *race;
	int32_t wing[TILES][2];
} CONSOLES[RACES] = {
	{ "zconsole", "zerg", { { 320, 492 }, { 5, 210 } } },
	{ "tconsole", "terran", { { 270, 492 }, { 0, 227 } } },
	{ "pconsole", "protoss", { { 280, 488 }, { 0, 212 } } },
};
static const char *const PIECE_NAMES[PIECES] = { "left", "center", "right" };

static int fail(KpError *err, const char *what, const char *why)
{
	snprintf(err->msg, sizeof err->msg, "hd ui: %s: %s", what, why);
	return -1;
}

static char lower(char c) { return c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c; }

/* Python's floor division (the places of images larger than their SD frame are negative). */
static int32_t floor_div(int32_t a, int32_t b) { return a / b - (a % b != 0 && (a < 0) != (b < 0)); }

/* --- the placement table (SHUI, tools/defs_c.py: a variant's files and their content keys, its moved frames, its
 * consoles' centre pieces) --- */

typedef struct {
	const uint8_t *p;
	size_t n, at;
	int bad;
} Rd;

static const uint8_t *rd_bytes(Rd *r, size_t n)
{
	const uint8_t *b = r->p + r->at;
	if (r->bad || n > r->n - r->at) {
		r->bad = 1;
		return NULL;
	}
	r->at += n;
	return b;
}

static uint32_t rd_u8(Rd *r)
{
	const uint8_t *b = rd_bytes(r, 1);
	return b ? b[0] : 0;
}

static uint32_t rd_u16(Rd *r)
{
	const uint8_t *b = rd_bytes(r, 2);
	return b ? kp_le_u16(b) : 0;
}

/* A u8-counted name. */
static const char *rd_name(Rd *r, size_t *n)
{
	*n = rd_u8(r);
	return (const char *)rd_bytes(r, *n);
}

typedef struct {
	Rd files, moved;         /* each at its first record */
	uint32_t file_count, moved_count;
	int32_t centre[RACES];   /* the centre pieces' x (logical), by CONSOLES' order */
} Table;

/* The variant's part of the profile's table: 0, or -1 with err. */
static int table_open(const KpDefsProfile *profile, int variant, Table *t, KpError *err)
{
	const KpDefsTable *d = profile ? kp_defs_table(profile, KP_HD_UI_SCR_TABLE) : NULL;
	const char *want = kp_hd_scr_variant_name(variant);
	Rd r;
	uint32_t variants, k, i;
	memset(t, 0, sizeof *t);
	if (!d) return fail(err, KP_HD_UI_SCR_TABLE, "no placement table compiled for the profile");
	memset(&r, 0, sizeof r);
	r.p = d->data;
	r.n = d->size;
	variants = rd_u8(&r);
	for (k = 0; k < variants && !r.bad; k++) {
		size_t n, len;
		const char *name = rd_name(&r, &n);
		int mine = name && n == strlen(want) && memcmp(name, want, n) == 0;
		t->file_count = rd_u16(&r);
		t->files = r;
		for (i = 0; i < t->file_count; i++) {
			rd_name(&r, &len);
			rd_bytes(&r, KEY_SIZE);
		}
		t->moved_count = rd_u16(&r);
		t->moved = r;
		for (i = 0; i < t->moved_count; i++) {
			rd_name(&r, &len);
			rd_bytes(&r, 6);
		}
		for (i = 0; i < RACES; i++) t->centre[i] = (int16_t)rd_u16(&r);
		if (mine && !r.bad) return 0;
	}
	return fail(err, KP_HD_UI_SCR_TABLE, r.bad ? "malformed" : "no part for the variant");
}

/* A file's content key in the table, NULL when the table does not take the file. */
static const uint8_t *table_key(const Table *t, const char *file)
{
	Rd r = t->files;
	uint32_t i;
	size_t want = strlen(file);
	for (i = 0; i < t->file_count; i++) {
		size_t n;
		const char *name = rd_name(&r, &n);
		const uint8_t *key = rd_bytes(&r, KEY_SIZE);
		if (r.bad) return NULL;
		if (n == want && memcmp(name, file, n) == 0) return key;
	}
	return NULL;
}

/* Where a GRP's frame moved (x, y in HD pixels from its SD frame's top-left): 1, or 0 at its usual place. */
static int table_moved(const Table *t, const char *path, int32_t frame, int32_t *x, int32_t *y)
{
	Rd r = t->moved;
	uint32_t i;
	size_t want = strlen(path);
	for (i = 0; i < t->moved_count; i++) {
		size_t n;
		const char *name = rd_name(&r, &n);
		const uint8_t *b = rd_bytes(&r, 6);
		if (r.bad) return 0;
		if (n == want && memcmp(name, path, n) == 0 && (int32_t)kp_le_u16(b) == frame) {
			*x = (int16_t)kp_le_u16(b + 2);
			*y = (int16_t)kp_le_u16(b + 4);
			return 1;
		}
	}
	return 0;
}

/* --- the SD files' HD art (hd_ui.py hd_path, sd_only, console_picture) --- */

/* A path lowercased with '/' separators into out: 0, or -1 when it does not fit. */
static int slashed(const char *path, size_t len, char *out, size_t size)
{
	size_t i;
	if (len >= size) return -1;
	for (i = 0; i < len; i++) out[i] = path[i] == '\\' ? '/' : lower(path[i]);
	out[len] = 0;
	return 0;
}

static const char *base_name(const char *p)
{
	const char *s = strrchr(p, '/');
	return s ? s + 1 : p;
}

static int ends_with(const char *s, const char *tail)
{
	size_t n = strlen(s), m = strlen(tail);
	return n >= m && strcmp(s + n - m, tail) == 0;
}

/* The race of a console's letter ('z', 't', 'p'), NULL none. */
static const char *letter_race(char c)
{
	int k;
	for (k = 0; k < RACES; k++) {
		if (CONSOLES[k].stem[0] == c) return CONSOLES[k].race;
	}
	return NULL;
}

/* A race's progress bar (game\tpbrempt.pcx, tpbrfull.pcx): Remastered keeps its HD art with the race's console. */
static int console_picture(const char *p)
{
	const char *name = base_name(p);
	return strncmp(p, "game/", 5) == 0 && letter_race(name[0])
		&& (strcmp(name + 1, "pbrempt.pcx") == 0 || strcmp(name + 1, "pbrfull.pcx") == 0);
}

/* The score screens' closing strip of an unselected tab: Remastered's art for it is a wider strip for its own layout
 * (hd_ui.py SD_ONLY). */
static int sd_only(const char *p) { return strcmp(base_name(p), "untab.pcx") == 0; }

int kp_hd_ui_scr_hd_name(const char *path, size_t len, char *out, size_t size)
{
	char p[KP_HD_UI_PATH + 8], stem[KP_HD_UI_PATH + 8];
	const char *name, *race;
	size_t n;
	int k, w;
	if (slashed(path, len, p, sizeof p) != 0) return -1;
	name = base_name(p);
	n = strlen(name);
	snprintf(stem, sizeof stem, "%.*s", n >= 4 ? (int)(n - 4) : 0, name);
	if (strcmp(p, "unit/cmdbtns/cmdicons.grp") == 0) {
		w = snprintf(out, size, "unit/cmdicons/cmdicons.dds.grp");
	} else if (strncmp(p, "unit/cmdbtns/", 13) == 0 && ends_with(name, "cmdbtns.grp") && (race = letter_race(name[0]))) {
		w = snprintf(out, size, "game/consoles/%s/cmdbtns.dds.grp", race);
	} else if (strncmp(p, "dlgs/", 5) == 0 && n > 4) {
		for (k = 0; k < RACES && strcmp(stem, CONSOLES[k].race) != 0; k++) {}
		if (k == RACES) goto plain;
		w = snprintf(out, size, "game/consoles/%s/dlg.dds.grp", stem);
	} else if (console_picture(p)) {
		w = snprintf(out, size, "game/consoles/%s/%s.dds", letter_race(name[0]), stem + 1);
	} else {
	plain:
		if (ends_with(p, ".grp")) w = snprintf(out, size, "%.*s.dds.grp", (int)(strlen(p) - 4), p);
		else if (ends_with(p, ".pcx")) w = snprintf(out, size, "%.*s.dds", (int)(strlen(p) - 4), p);
		else return -1;
	}
	return w > 0 && (size_t)w < size ? 0 : -1;
}

int kp_hd_ui_scr_held(KpRes *r, int variant)
{
	char name[KP_HD_UI_SCR_NAME];
	snprintf(name, sizeof name, "%sunit/cmdicons/cmdicons.dds.grp", kp_hd_scr_variant_prefix(variant));
	return kp_res_install(r) == KP_RES_SOURCE_CASC && kp_res_raw_held(r, name);
}

int kp_hd_ui_scr_classify(const char *path, size_t len, int variant, char *file, size_t size, int *race)
{
	char p[KP_HD_UI_PATH + 8], hd[KP_HD_UI_SCR_NAME];
	const char *name;
	int k, w;
	*race = -1;
	file[0] = 0;
	if (slashed(path, len, p, sizeof p) != 0) return KP_HD_UI_SCR_NONE; /* too long for ASET */
	name = base_name(p);
	for (k = 0; k < RACES; k++) {
		size_t n = strlen(CONSOLES[k].stem);
		if (strncmp(name, CONSOLES[k].stem, n) == 0 && strlen(name) == n + 4) {
			*race = k;
			return KP_HD_UI_SCR_CONSOLE;
		}
	}
	if (kp_hd_ui_scr_hd_name(path, len, hd, sizeof hd) != 0) return KP_HD_UI_SCR_NONE;
	if (sd_only(p)) return KP_HD_UI_SCR_KEPT_SD;
	w = snprintf(file, size, "%s%s", kp_hd_scr_variant_prefix(variant), hd);
	if (w <= 0 || (size_t)w >= size) return KP_HD_UI_SCR_NONE;
	if (ends_with(hd, ".dds.grp")) return KP_HD_UI_SCR_GRP;
	return console_picture(p) ? KP_HD_UI_SCR_PICTURE : KP_HD_UI_SCR_PCX;
}

void kp_hd_ui_scr_console_file(int variant, int race, int piece, char *out, size_t size)
{
	if (piece < 0) snprintf(out, size, "SD/game/consoles/%s/console.dds", CONSOLES[race].race);
	else snprintf(out, size, "%sgame/consoles/%s/console_%s.dds", kp_hd_scr_variant_prefix(variant), CONSOLES[race].race,
		PIECE_NAMES[piece]);
}

int kp_hd_ui_scr_clip_name(const char *path, size_t len, int variant, char *out, size_t size)
{
	static const char PORTRAIT[] = "portrait/"; /* the portrait set's (portdata.dat's): the glue's movies stay SD */
	char p[KP_HD_UI_PATH + 8];
	int w;
	if (len < 4 || slashed(path, len - 4, p, sizeof p) != 0 /* without ".smk" */
		|| strncmp(p, PORTRAIT, sizeof PORTRAIT - 1) != 0) {
		return -1;
	}
	w = snprintf(out, size, "%s%s.webm", kp_hd_scr_variant_prefix(variant), p);
	return w > 0 && (size_t)w < size ? 0 : -1;
}

void kp_hd_ui_scr_centred(int32_t scale, int32_t sw, int32_t sh, int32_t w, int32_t h, int32_t *x, int32_t *y)
{
	*x = floor_div(scale * sw - w, 2);
	*y = floor_div(scale * sh - h, 2);
}

int kp_hd_ui_scr_places(int32_t scale, int32_t sx, int32_t sy, int32_t sw, int32_t sh, int32_t cw, int32_t ch, int32_t w,
	int32_t h, int32_t places[KP_HD_UI_SCR_PLACES][2])
{
	int wire = h > 0 && w == KP_HD_UI_WIRE_LAYERS * h;
	int32_t side = wire ? h : w;
	kp_hd_ui_scr_centred(scale, sw, sh, side, h, &places[KP_HD_UI_SCR_ON_FRAME][0], &places[KP_HD_UI_SCR_ON_FRAME][1]);
	places[KP_HD_UI_SCR_AT_ORIGIN][0] = -scale * sx;
	places[KP_HD_UI_SCR_AT_ORIGIN][1] = -scale * sy;
	kp_hd_ui_scr_centred(scale, cw - 2 * sx, ch - 2 * sy, side, h, &places[KP_HD_UI_SCR_ON_CANVAS][0],
		&places[KP_HD_UI_SCR_ON_CANVAS][1]);
	return wire ? KP_HD_UI_SCR_ON_CANVAS : KP_HD_UI_SCR_ON_FRAME;
}

/* --- the layout --- */

/* A DDS header's size and format (KP_HD_UI_BC1, _BC3, or KP_HD_UI_PNG for 32-bit RGBA): 0, or -1 for another. */
static int dds_header(const uint8_t *h, int32_t *w, int32_t *ht, int *format)
{
	uint32_t flags = kp_le_u32(h + 80);
	if (memcmp(h, "DDS ", 4) != 0) return -1;
	*ht = (int32_t)kp_le_u32(h + 12);
	*w = (int32_t)kp_le_u32(h + 16);
	if (flags & DDS_PF_FOURCC) {
		if (memcmp(h + 84, "DXT1", 4) == 0) *format = KP_HD_UI_BC1;
		else if (memcmp(h + 84, "DXT5", 4) == 0) *format = KP_HD_UI_BC3;
		else return -1;
		return 0;
	}
	*format = KP_HD_UI_PNG;
	return (flags & DDS_PF_RGB) && kp_le_u32(h + 88) == 32 && kp_le_u32(h + 92) == 0xFFu && kp_le_u32(h + 96) == 0xFF00u
		&& kp_le_u32(h + 100) == 0xFF0000u && kp_le_u32(h + 104) == 0xFF000000u ? 0 : -1;
}

typedef struct {
	KpRes *r;
	KpResReader *h;
	const KpFiles *sd;
	Table table;
	KpHdUiScr *s;
	const char *prefix;
	uint8_t *aset, *frms;
	size_t asset_cap, frame_cap, source_cap, file_cap;
	int32_t asset_count, frame_count;
	KpError *err;
} Build;

static int grow(uint8_t **p, size_t *cap, size_t need)
{
	uint8_t *g;
	size_t c = *cap ? *cap : 64;
	if (need <= *cap) return 0;
	while (c < need) c *= 2;
	if (!(g = (uint8_t *)realloc(*p, c))) return -1;
	*p = g;
	*cap = c;
	return 0;
}

/* A file of the install read at offset through the build's reader. */
static int read_at(Build *b, const char *file, uint64_t offset, void *out, size_t size)
{
	KpResPlace p = b->s->install;
	snprintf(p.name, sizeof p.name, "%s", file);
	return kp_res_reader_read(b->h, &p, offset, out, size);
}

/* The HD file's index among the layout's files, added when new; -1 out of memory. */
static int32_t file_index(Build *b, const char *file)
{
	KpHdUiScr *s = b->s;
	int32_t k;
	for (k = 0; k < s->file_count; k++) {
		if (strcmp(s->files[k], file) == 0) return k;
	}
	if (grow((uint8_t **)&s->files, &b->file_cap, (size_t)(s->file_count + 1) * sizeof *s->files) != 0) return -1;
	snprintf(s->files[s->file_count], sizeof s->files[0], "%s", file);
	return s->file_count++;
}

/* Whether the table takes the HD file and the install's copy is the one it was made from (its content key); a copy of
 * other bytes is counted and left in SD. */
static int taken(Build *b, const char *file)
{
	const uint8_t *want = table_key(&b->table, file);
	uint8_t key[KEY_SIZE];
	if (!want) return 0;
	if (kp_res_raw_key(b->r, file, key) == 0 && memcmp(key, want, KEY_SIZE) == 0) return 1;
	if (b->s->skipped++ == 0) snprintf(b->s->first_skipped, sizeof b->s->first_skipped, "%s", file);
	return 0;
}

/* A frame record (ui.kpd FRMS: offset and size those of its DDS in its HD file) and its source. */
static int add_frame(Build *b, uint32_t offset, uint32_t size, int32_t w, int32_t h, int format, int32_t x, int32_t y,
	int32_t file, int side, int32_t piece, int race)
{
	KpHdUiScr *s = b->s;
	uint8_t *r;
	if (grow(&b->frms, &b->frame_cap, (size_t)(b->frame_count + 1) * FRAME_RECORD) != 0) return -1;
	if (grow((uint8_t **)&s->sources, &b->source_cap, (size_t)(b->frame_count + 1) * sizeof *s->sources) != 0) return -1;
	r = b->frms + (size_t)b->frame_count * FRAME_RECORD;
	kp_le_put32(r, offset);
	kp_le_put32(r + 4, size);
	kp_le_put16(r + 8, (uint32_t)w);
	kp_le_put16(r + 10, (uint32_t)h);
	r[12] = (uint8_t)format;
	r[13] = 0;
	kp_le_put16(r + 14, (uint32_t)(uint16_t)(int16_t)x);
	kp_le_put16(r + 16, (uint32_t)(uint16_t)(int16_t)y);
	s->sources[b->frame_count].file = file;
	s->sources[b->frame_count].side = side;
	s->sources[b->frame_count].piece = piece;
	s->sources[b->frame_count].race = race;
	b->frame_count++;
	return 0;
}

/* An asset record (ui.kpd ASET: its SD path lowercased, NUL padded) over the frames added since first. */
static int add_asset(Build *b, const char *path, size_t len, int32_t first, int flags)
{
	uint8_t *r;
	size_t i;
	if (len >= KP_HD_UI_PATH) return fail(b->err, path, "path too long for ASET");
	if (grow(&b->aset, &b->asset_cap, (size_t)(b->asset_count + 1) * ASSET_RECORD) != 0) return fail(b->err, path, "out of memory");
	r = b->aset + (size_t)b->asset_count * ASSET_RECORD;
	memset(r, 0, ASSET_RECORD);
	for (i = 0; i < len; i++) r[i] = (uint8_t)lower(path[i]);
	kp_le_put32(r + KP_HD_UI_PATH, (uint32_t)first);
	kp_le_put16(r + KP_HD_UI_PATH + 4, (uint32_t)(b->frame_count - first));
	kp_le_put16(r + KP_HD_UI_PATH + 6, (uint32_t)flags);
	b->asset_count++;
	return 0;
}

/* A DDS file's header read: its size, format and length. */
static int read_dds(Build *b, const char *file, int32_t *w, int32_t *h, int *format, uint64_t *size)
{
	KpResPlace p = b->s->install;
	uint8_t head[DDS_HEADER];
	snprintf(p.name, sizeof p.name, "%s", file);
	if (kp_res_reader_size(b->h, &p, size) != 0 || *size < DDS_HEADER || *size > UINT32_MAX
		|| kp_res_reader_read(b->h, &p, 0, head, sizeof head) != 0) {
		return fail(b->err, file, "cannot be read");
	}
	return dds_header(head, w, h, format) == 0 ? 0 : fail(b->err, file, "not a DDS image of BC1, BC3 or RGBA");
}

/* A race's console asset (hd_ui.py console_frames): its pieces at the bottom of the screen, the left and the right at
 * a WIDE screen's edges around the 640 columns, the centre one where the table puts it; then the wings' tiles at their
 * pieces' rows. Left out when the table does not take each of its files. */
static int console_asset(Build *b, const char *path, size_t len, int race)
{
	char file[KP_HD_UI_SCR_NAME];
	int32_t first = b->frame_count, scale = b->s->scale, k, w[PIECES], h[PIECES], y[PIECES], x[PIECES];
	int format[PIECES];
	uint64_t size[PIECES];
	kp_hd_ui_scr_console_file(b->s->variant, race, -1, file, sizeof file);
	if (!taken(b, file)) return 0;
	for (k = 0; k < PIECES; k++) {
		kp_hd_ui_scr_console_file(b->s->variant, race, k, file, sizeof file);
		if (!taken(b, file)) return 0;
		if (read_dds(b, file, &w[k], &h[k], &format[k], &size[k]) != 0) return -1;
		x[k] = k == 0 ? -((WIDE - 640) / 2) : k == 1 ? b->table.centre[race] : 640 + (WIDE - 640 + 1) / 2 - w[k] / scale;
		y[k] = SCREEN_H - h[k] / scale; /* the SD console's first row and the piece's in it: their heights cancel */
	}
	for (k = 0; k < PIECES; k++) {
		int32_t f;
		kp_hd_ui_scr_console_file(b->s->variant, race, k, file, sizeof file);
		if ((f = file_index(b, file)) < 0
			|| add_frame(b, 0, (uint32_t)size[k], w[k], h[k], format[k], x[k] * scale, y[k] * scale, f, 0, -1, race) != 0) {
			return fail(b->err, file, "out of memory");
		}
	}
	for (k = 0; k < TILES; k++) {
		int32_t piece = k == 0 ? 0 : 2, cols = CONSOLES[race].wing[k][1] - CONSOLES[race].wing[k][0];
		if (add_frame(b, 0, 0, (cols - TILE_BLEND) * scale, h[piece], KP_HD_UI_PNG, 0, y[piece] * scale,
				b->s->sources[first + piece].file, 1 + k, first + piece, race) != 0) {
			return fail(b->err, path, "out of memory");
		}
	}
	return add_asset(b, path, len, first, KP_HD_UI_CONSOLE);
}

/* A GRP's asset: each frame of its *.dds.grp at its usual place (centred on the SD frame; a wireframe's on the GRP's
 * canvas) or where the table moved it. Left out when the frame counts differ. */
static int grp_asset(Build *b, const char *path, size_t len, const char *file)
{
	uint8_t *sd, head[CONTAINER_HEADER], rec[CONTAINER_ENTRY + DDS_HEADER];
	size_t sd_size;
	uint32_t frames, canvas_w, canvas_h, k, count;
	uint64_t at = CONTAINER_HEADER;
	int32_t first = b->frame_count, f, scale = b->s->scale;
	char lowered[KP_HD_UI_PATH + 8];
	if (!(sd = kp_files_need(b->sd, path, &sd_size, b->err))) return -1;
	if (kp_grp_header(sd, sd_size, &frames, &canvas_w, &canvas_h) != 0 || read_at(b, file, 0, head, sizeof head) != 0) {
		free(sd);
		return fail(b->err, path, "its GRP or its HD art cannot be read");
	}
	count = kp_le_u16(head + 4);
	if (count != frames) { /* the table takes none such (hd_ui.py: left in SD) */
		free(sd);
		return 0;
	}
	for (k = 0; k < len; k++) lowered[k] = lower(path[k]); /* the table's paths: the SD path lowercased (ASET's) */
	lowered[len] = 0;
	if ((f = file_index(b, file)) < 0) {
		free(sd);
		return fail(b->err, file, "out of memory");
	}
	for (k = 0; k < count; k++) {
		KpGrpFrame g;
		int32_t w, h, x, y;
		int format;
		uint32_t length;
		if (read_at(b, file, at, rec, sizeof rec) != 0 || dds_header(rec + CONTAINER_ENTRY, &w, &h, &format) != 0) {
			free(sd);
			return fail(b->err, file, "an image cannot be read, or is not BC1, BC3 or RGBA");
		}
		length = kp_le_u32(rec + 8);
		kp_grp_frame(sd, k, &g);
		if (!table_moved(&b->table, lowered, (int32_t)k, &x, &y)) { /* its usual place */
			int32_t places[KP_HD_UI_SCR_PLACES][2];
			int usual = kp_hd_ui_scr_places(scale, g.x, g.y, g.w, g.h, (int32_t)canvas_w, (int32_t)canvas_h, w, h, places);
			x = places[usual][0];
			y = places[usual][1];
		}
		if (add_frame(b, (uint32_t)(at + CONTAINER_ENTRY), length, w, h, format, x, y, f, 0, -1, -1) != 0) {
			free(sd);
			return fail(b->err, file, "out of memory");
		}
		at += CONTAINER_ENTRY + (uint64_t)length;
	}
	free(sd);
	return add_asset(b, path, len, first, 0);
}

/* A PCX's asset: its HD picture centred on it (the table takes those that are the SD picture drawn again). */
static int pcx_asset(Build *b, const char *path, size_t len, const char *file)
{
	int32_t sd_w, sd_h, w, h, x, y, f, first = b->frame_count, scale = b->s->scale;
	uint8_t *sd;
	size_t sd_size;
	int format;
	uint64_t size;
	if (!(sd = kp_files_need(b->sd, path, &sd_size, b->err))) return -1;
	f = kp_ui_pcx(sd, sd_size, &sd_w, &sd_h, NULL, 0);
	free(sd);
	if (f != 0) return fail(b->err, path, "not a PCX");
	if (read_dds(b, file, &w, &h, &format, &size) != 0) return -1;
	kp_hd_ui_scr_centred(scale, sd_w, sd_h, w, h, &x, &y);
	if ((f = file_index(b, file)) < 0 || add_frame(b, 0, (uint32_t)size, w, h, format, x, y, f, 0, -1, -1) != 0) {
		return fail(b->err, file, "out of memory");
	}
	return add_asset(b, path, len, first, 0);
}

/* One SD path of kp_ui_data_art_paths (hd_ui.py generate's loop). */
static int add_path(void *ctx, const char *path, size_t len)
{
	Build *b = (Build *)ctx;
	char file[KP_HD_UI_SCR_NAME];
	int race, kind = kp_hd_ui_scr_classify(path, len, b->s->variant, file, sizeof file, &race);
	if (kind == KP_HD_UI_SCR_CONSOLE) return console_asset(b, path, len, race);
	if (kind == KP_HD_UI_SCR_NONE || kind == KP_HD_UI_SCR_KEPT_SD) return 0;
	if (!taken(b, file)) return 0; /* not drawn again (a picture of Remastered's own layout), absent, or other bytes */
	return kind == KP_HD_UI_SCR_GRP ? grp_asset(b, path, len, file) : pcx_asset(b, path, len, file);
}

void kp_hd_ui_scr_free(KpHdUiScr *s)
{
	free(s->head);
	free(s->sources);
	free(s->files);
	memset(s, 0, sizeof *s);
}

/* A layout begun: its table, the install's place. */
static int begin(Build *b, KpRes *r, KpResReader *h, const KpFiles *sd, int variant, KpHdUiScr *out, KpError *err)
{
	char probe[KP_HD_UI_SCR_NAME];
	memset(out, 0, sizeof *out);
	memset(b, 0, sizeof *b);
	out->variant = variant;
	out->scale = kp_hd_scr_variant_scale(variant);
	b->r = r;
	b->h = h;
	b->sd = sd;
	b->s = out;
	b->err = err;
	b->prefix = kp_hd_scr_variant_prefix(variant);
	if (table_open(kp_res_profile(r), variant, &b->table, err) != 0) return -1;
	snprintf(probe, sizeof probe, "%sunit/cmdicons/cmdicons.dds.grp", b->prefix);
	if (!kp_res_raw_place(r, probe, &out->install) || out->install.kind != KP_RES_SOURCE_CASC) {
		return fail(err, probe, "no Remastered install holds it");
	}
	return 0;
}

/* The layout's records into its head and index (ok), the build's buffers freed: 0, or -1 with err. */
static int end(Build *b, int ok)
{
	KpHdUiScr *out = b->s;
	size_t aset = (size_t)b->asset_count * ASSET_RECORD, frms = (size_t)b->frame_count * FRAME_RECORD;
	if (ok && !(out->head = (uint8_t *)malloc(HEAD_SIZE + aset + frms + 1))) ok = fail(b->err, "the layout", "out of memory") == 0;
	if (ok) {
		out->head[0] = (uint8_t)out->scale;
		kp_le_put16(out->head + 1, (uint32_t)b->asset_count);
		kp_le_put32(out->head + 3, (uint32_t)b->frame_count);
		if (aset) memcpy(out->head + HEAD_SIZE, b->aset, aset);
		if (frms) memcpy(out->head + HEAD_SIZE + aset, b->frms, frms);
		out->aset_at = HEAD_SIZE;
		out->frms_at = HEAD_SIZE + aset;
		out->index.scale = out->scale;
		out->index.asset_count = b->asset_count;
		out->index.frame_count = b->frame_count;
		out->index.assets = out->head + out->aset_at;
		out->index.frames = out->head + out->frms_at;
		out->index.images = 0;
	}
	free(b->aset);
	free(b->frms);
	b->aset = b->frms = NULL;
	if (!ok) kp_hd_ui_scr_free(out);
	return ok ? 0 : -1;
}

int kp_hd_ui_scr_build(KpRes *r, KpResReader *h, const KpFiles *sd, int variant, KpHdUiScr *out, KpError *err)
{
	Build b;
	if (begin(&b, r, h, sd, variant, out, err) != 0) return end(&b, 0);
	return end(&b, kp_ui_data_art_paths(sd, add_path, &b, err) == 0);
}

/* --- the images --- */

/* A frame's DDS read from its HD file: malloc'd, or NULL with err. */
static uint8_t *frame_dds(const KpHdUiScr *s, KpResReader *h, int32_t frame, KpHdUiFrame *f, KpError *err)
{
	KpResPlace p = s->install;
	uint8_t *dds;
	kp_hd_ui_frame(&s->index, frame, f);
	snprintf(p.name, sizeof p.name, "%s", s->files[s->sources[frame].file]);
	if (f->size < DDS_HEADER || !(dds = (uint8_t *)malloc(f->size))) {
		fail(err, p.name, "an image too small, or out of memory");
		return NULL;
	}
	if (kp_res_reader_read(h, &p, f->offset, dds, f->size) != 0) {
		free(dds);
		fail(err, p.name, "an image cannot be read");
		return NULL;
	}
	return dds;
}

/* A DDS's image: its blocks (BC1, BC3) or its RGBA8 rows, malloc'd. */
static uint8_t *dds_image(const uint8_t *dds, size_t size, const char *file, size_t *out_size, KpError *err)
{
	int32_t w, h;
	int format;
	size_t need;
	uint8_t *out;
	if (size < DDS_HEADER || dds_header(dds, &w, &h, &format) != 0) {
		fail(err, file, "not a DDS image of BC1, BC3 or RGBA");
		return NULL;
	}
	need = format == KP_HD_UI_PNG ? (size_t)w * (size_t)h * 4
		: (size_t)((w + 3) / 4) * (size_t)((h + 3) / 4) * (format == KP_HD_UI_BC3 ? 16 : 8);
	if (need > size - DDS_HEADER || !(out = (uint8_t *)malloc(need ? need : 1))) {
		fail(err, file, "an image truncated, or out of memory");
		return NULL;
	}
	memcpy(out, dds + DDS_HEADER, need);
	*out_size = need;
	return out;
}

/* An image of format (w x h) decoded to RGBA8: blocks decoded and cropped, RGBA8 as it is (taken). */
static uint8_t *decoded(uint8_t *image, int32_t w, int32_t h, int format)
{
	int32_t w4 = (w + 3) / 4 * 4, h4 = (h + 3) / 4 * 4, r;
	uint8_t *full, *rgba;
	if (format == KP_HD_UI_PNG) return image;
	full = (uint8_t *)malloc((size_t)w4 * (size_t)h4 * 4);
	rgba = (uint8_t *)malloc((size_t)w * (size_t)h * 4 + 1);
	if (full && rgba) {
		kp_bcn_decode(image, w4, h4, format == KP_HD_UI_BC3, full);
		for (r = 0; r < h; r++) memcpy(rgba + (size_t)r * (size_t)w * 4, full + (size_t)r * (size_t)w4 * 4, (size_t)w * 4);
	} else {
		free(rgba);
		rgba = NULL;
	}
	free(full);
	free(image);
	return rgba;
}

uint8_t *kp_hd_ui_scr_dds_rgba(const uint8_t *dds, size_t size, int32_t *w, int32_t *h)
{
	KpError e;
	size_t n;
	int format;
	uint8_t *image;
	if (size < DDS_HEADER || dds_header(dds, w, h, &format) != 0 || !(image = dds_image(dds, size, "a DDS", &n, &e))) return NULL;
	return decoded(image, *w, *h, format);
}

/* A wing's tile (hd_ui.py wing_tile) of a piece's RGBA image (iw x ih): the columns' part repeating without a seam,
 * its period the part less TILE_BLEND; the part's far end crossfades into its start (premultiplied), so the tile's
 * first columns (side 1: its last) are also the piece's, over which they are drawn. Malloc'd, period x ih. */
static uint8_t *wing_tile(const uint8_t *rgba, int32_t iw, int32_t ih, const int32_t columns[2], int side, int32_t scale)
{
	int32_t x0 = columns[0] * scale, width = columns[1] * scale - x0, blend = TILE_BLEND * scale;
	int32_t period = width - blend, kept = x0 + (side ? blend : 0), x, y, c;
	uint8_t *out = (uint8_t *)malloc((size_t)period * (size_t)ih * 4 + 1);
	if (!out) return NULL;
	for (y = 0; y < ih; y++) {
		memcpy(out + (size_t)y * (size_t)period * 4, rgba + ((size_t)y * (size_t)iw + (size_t)kept) * 4, (size_t)period * 4);
		for (x = 0; x < blend; x++) {
			const uint8_t *a = rgba + ((size_t)y * (size_t)iw + (size_t)(x0 + (side ? width - 1 - x : x))) * 4;
			const uint8_t *e = rgba + ((size_t)y * (size_t)iw + (size_t)(x0 + (side ? width - 1 - (period + x) : period + x))) * 4;
			double k = (double)(x + 1) / (double)blend, alpha = (double)a[3] * k + (double)e[3] * (1 - k);
			uint8_t *o = out + ((size_t)y * (size_t)period + (size_t)(side ? period - 1 - x : x)) * 4;
			for (c = 0; c < 3; c++) {
				double v = alpha != 0 ? nearbyint(((double)(a[c] * a[3]) * k + (double)(e[c] * e[3]) * (1 - k)) / alpha) : 0;
				o[c] = (uint8_t)(v > 255 ? 255 : v);
			}
			o[3] = (uint8_t)nearbyint(alpha);
		}
	}
	return out;
}

/* A tile frame's RGBA8 texels, cut from its piece's image. */
static uint8_t *tile_rgba(const KpHdUiScr *s, KpResReader *h, int32_t frame, KpError *err)
{
	const KpHdUiScrSource *src = &s->sources[frame];
	const char *file = s->files[src->file];
	KpHdUiFrame piece;
	uint8_t *dds, *image, *rgba, *tile;
	size_t size;
	int race = src->race;
	if (race < 0 || race >= RACES) {
		fail(err, file, "not a console's piece");
		return NULL;
	}
	if (!(dds = frame_dds(s, h, src->piece, &piece, err))) return NULL;
	image = dds_image(dds, piece.size, file, &size, err);
	free(dds);
	if (!image || !(rgba = decoded(image, piece.w, piece.h, piece.format))) {
		if (image) fail(err, file, "out of memory");
		return NULL;
	}
	tile = wing_tile(rgba, piece.w, piece.h, CONSOLES[race].wing[src->side - 1], src->side - 1, s->scale);
	free(rgba);
	if (!tile) fail(err, file, "out of memory");
	return tile;
}

uint8_t *kp_hd_ui_scr_image(const KpHdUiScr *s, KpResReader *h, int32_t frame, size_t *size, KpError *err)
{
	KpHdUiFrame f;
	uint8_t *dds, *image;
	if (frame < 0 || frame >= s->index.frame_count) {
		fail(err, "a frame", "out of the layout");
		return NULL;
	}
	if (s->sources[frame].side) {
		kp_hd_ui_frame(&s->index, frame, &f);
		*size = (size_t)f.w * (size_t)f.h * 4;
		return tile_rgba(s, h, frame, err);
	}
	if (!(dds = frame_dds(s, h, frame, &f, err))) return NULL;
	image = dds_image(dds, f.size, s->files[s->sources[frame].file], size, err);
	free(dds);
	return image;
}

uint8_t *kp_hd_ui_scr_rgba(const KpHdUiScr *s, KpResReader *h, int32_t frame, KpError *err)
{
	KpHdUiFrame f;
	size_t size;
	uint8_t *image = kp_hd_ui_scr_image(s, h, frame, &size, err);
	if (!image) return NULL;
	kp_hd_ui_frame(&s->index, frame, &f);
	if (!(image = decoded(image, f.w, f.h, f.format))) fail(err, s->files[s->sources[frame].file], "out of memory");
	return image;
}

/* --- the consoles' art (CONW) --- */

#define NEAREST_BITS 12 /* art_cells' nearest colours kept by mean colour: a console's cells repeat a few */

/* The place in the race's colours of the nearest to (r, g, b), the first of the nearest. */
static int nearest(const uint8_t (*rgb)[3], int colours, int32_t r, int32_t g, int32_t b)
{
	int32_t best_d = INT32_MAX;
	int best = 0, i;
	for (i = 0; i < colours; i++) {
		int32_t dr = rgb[i][0] - r, dg = rgb[i][1] - g, db = rgb[i][2] - b, d = dr * dr + dg * dg + db * db;
		if (d < best_d) {
			best_d = d;
			best = i;
		}
	}
	return best;
}

/* An RGBA image of PLAN_SCALE at one CONW texel per logical pixel (hd_ui.py art_cells): 0 none, else 1 + the place in
 * the race's colours of the nearest to the texels' colour (their alpha-weighted mean), KP_UI_CONSOLE_FRINGE where they
 * cover less than half. A mean seen before takes its nearest from a small table (the search over 127 colours was most
 * of the consoles' build at start); without memory for it each is searched. */
static void art_cells(const uint8_t *rgba, int32_t w, int32_t h, const uint8_t (*rgb)[3], int colours, uint8_t *out)
{
	const int32_t s = PLAN_SCALE;
	uint32_t *seen = (uint32_t *)calloc((size_t)1 << NEAREST_BITS, sizeof *seen); /* 1, the mean's 24 bits, its 7 */
	int32_t x, y, dx, dy, c;
	for (y = 0; y < h / s; y++) {
		for (x = 0; x < w / s; x++) {
			uint32_t acc[3] = { 0, 0, 0 }, alpha = 0, mean, key, *slot;
			int best;
			for (dy = 0; dy < s; dy++) {
				for (dx = 0; dx < s; dx++) {
					const uint8_t *t = rgba + ((size_t)(y * s + dy) * (size_t)w + (size_t)(x * s + dx)) * 4;
					alpha += t[3];
					for (c = 0; c < 3; c++) acc[c] += (uint32_t)t[c] * t[3];
				}
			}
			out[(size_t)y * (size_t)(w / s) + (size_t)x] = 0;
			if (!alpha) continue;
			mean = (acc[0] / alpha) << 16 | (acc[1] / alpha) << 8 | acc[2] / alpha;
			key = 1u << 24 | mean;
			slot = seen ? &seen[(mean * 2654435761u) >> (32 - NEAREST_BITS)] : NULL;
			if (slot && *slot >> 7 == key) {
				best = (int)(*slot & 0x7f);
			} else {
				best = nearest(rgb, colours, (int32_t)(mean >> 16), (int32_t)(mean >> 8 & 0xff), (int32_t)(mean & 0xff));
				if (slot) *slot = key << 7 | (uint32_t)best;
			}
			out[(size_t)y * (size_t)(w / s) + (size_t)x] = (uint8_t)((1 + best) | (alpha < 128u * s * s ? KP_UI_CONSOLE_FRINGE : 0));
		}
	}
	free(seen);
}

/* A tile's first copy's left column in the console PCX (logical; hd_ui.py tile_origin), over its piece at piece_x. */
static int32_t tile_origin(int32_t piece_x, const int32_t columns[2], int side)
{
	int32_t period = columns[1] - columns[0] - TILE_BLEND;
	return side ? piece_x + columns[0] - period + TILE_BLEND : piece_x + columns[1] - TILE_BLEND;
}

/* A CONW piece: i16 x, y, u16 width, height, period, then its cells. */
static int put_piece(uint8_t **out, size_t *size, size_t *cap, int32_t x, int32_t y, const uint8_t *rgba, int32_t w,
	int32_t h, int32_t period, const uint8_t (*rgb)[3], int colours)
{
	int32_t cw = w / PLAN_SCALE, ch = h / PLAN_SCALE;
	uint8_t *p;
	if (grow(out, cap, *size + 10 + (size_t)cw * (size_t)ch) != 0) return -1;
	p = *out + *size;
	kp_le_put16(p, (uint32_t)(uint16_t)(int16_t)x);
	kp_le_put16(p + 2, (uint32_t)(uint16_t)(int16_t)y);
	kp_le_put16(p + 4, (uint32_t)cw);
	kp_le_put16(p + 6, (uint32_t)ch);
	kp_le_put16(p + 8, (uint32_t)period);
	art_cells(rgba, w, h, rgb, colours, p + 10);
	*size += 10 + (size_t)cw * (size_t)ch;
	return 0;
}

uint8_t *kp_hd_ui_scr_console(KpRes *r, KpResReader *h, const KpFiles *sd, size_t *size, KpError *err)
{
	KpHdUiScr layout;
	KpKpfOut section;
	Build b;
	uint8_t *conw = NULL, *out = NULL, *pieces[PIECES];
	size_t n = 1, cap = 0;
	int race, k, ok;
	char path[32];
	memset(pieces, 0, sizeof pieces);
	if (begin(&b, r, h, sd, KP_HD_SCR_HD2, &layout, err) != 0) {
		end(&b, 0);
		return NULL;
	}
	for (ok = 1, race = 0; race < RACES && ok; race++) { /* the consoles' assets alone */
		snprintf(path, sizeof path, "game\\%s.pcx", CONSOLES[race].stem);
		ok = console_asset(&b, path, strlen(path), race) == 0;
	}
	if (end(&b, ok) != 0 || grow(&conw, &cap, 1) != 0) goto failed;
	conw[0] = RACES;
	for (race = 0; race < RACES; race++) {
		int32_t first, count, pw, ph;
		int flags, colours = 0;
		uint32_t counts[256];
		uint8_t palette[768], table[KP_UI_CONSOLE_COLOURS], rgb[KP_UI_CONSOLE_COLOURS][3], *pixels, *raw;
		KpHdUiFrame f[PIECES];
		size_t i;
		snprintf(path, sizeof path, "game\\%s.pcx", CONSOLES[race].stem);
		if (kp_hd_ui_asset(&layout.index, path, strlen(path), &first, &count, &flags) < 0) {
			fail(err, path, layout.skipped ? "its HD art is not the placement table's" : "no HD art for it");
			goto failed;
		}
		/* its colours: the most used indices of its PCX but 0, by count (then index) */
		if (!(raw = kp_files_need(sd, path, &i, err))) goto failed;
		pixels = kp_scr_pcx_palette(raw, i, palette) == 0 ? kp_files_pcx_pixels(raw, i, &pw, &ph) : NULL;
		free(raw);
		if (!pixels) {
			fail(err, path, "not a PCX with a palette");
			goto failed;
		}
		memset(counts, 0, sizeof counts);
		for (i = 0; i < (size_t)pw * (size_t)ph; i++) counts[pixels[i]]++;
		free(pixels);
		counts[0] = 0;
		memset(table, 0, sizeof table);
		while (colours < KP_UI_CONSOLE_COLOURS) {
			int best = -1, v;
			for (v = 1; v < 256; v++) {
				if (counts[v] && (best < 0 || counts[v] > counts[best])) best = v;
			}
			if (best < 0) break;
			table[colours] = (uint8_t)best;
			memcpy(rgb[colours++], palette + 3 * best, 3);
			counts[best] = 0;
		}
		if (grow(&conw, &cap, n + 1 + KP_UI_CONSOLE_COLOURS) != 0) goto oom;
		conw[n] = (uint8_t)CONSOLES[race].stem[0];
		memcpy(conw + n + 1, table, sizeof table);
		n += 1 + KP_UI_CONSOLE_COLOURS;
		for (k = 0; k < PIECES; k++) { /* the pieces, then the tiles cut from the left and the right one */
			kp_hd_ui_frame(&layout.index, first + k, &f[k]);
			if (!(pieces[k] = kp_hd_ui_scr_rgba(&layout, h, first + k, err))) goto failed;
			if (put_piece(&conw, &n, &cap, floor_div(f[k].x, PLAN_SCALE), floor_div(f[k].y, PLAN_SCALE), pieces[k], f[k].w,
					f[k].h, 0, (const uint8_t(*)[3])rgb, colours) != 0) {
				goto oom;
			}
		}
		for (k = 0; k < TILES; k++) {
			int32_t piece = k == 0 ? 0 : 2, tw = (CONSOLES[race].wing[k][1] - CONSOLES[race].wing[k][0] - TILE_BLEND) * PLAN_SCALE;
			uint8_t *tile = wing_tile(pieces[piece], f[piece].w, f[piece].h, CONSOLES[race].wing[k], k, PLAN_SCALE);
			int put = tile ? put_piece(&conw, &n, &cap, tile_origin(floor_div(f[piece].x, PLAN_SCALE), CONSOLES[race].wing[k], k),
						 floor_div(f[piece].y, PLAN_SCALE), tile, tw, f[piece].h, tw / PLAN_SCALE,
						 (const uint8_t(*)[3])rgb, colours) : -1;
			free(tile);
			if (put != 0) goto oom;
		}
		for (k = 0; k < PIECES; k++) {
			free(pieces[k]);
			pieces[k] = NULL;
		}
	}
	section.fourcc = KP_UI_CONSOLE_KIND;
	section.payload = conw;
	section.size = n;
	section.schema_version = 0;
	if (kp_kpf_build(KP_UI_CONSOLE_KIND, KP_UI_CONSOLE_SCHEMA, 0, kp_res_profile(r)->id, NULL, &section, 1, &out, size) != 0) {
		goto oom;
	}
	free(conw);
	kp_hd_ui_scr_free(&layout);
	return out;
oom:
	fail(err, "the consoles' art", "out of memory");
failed:
	for (k = 0; k < PIECES; k++) free(pieces[k]);
	free(conw);
	kp_hd_ui_scr_free(&layout);
	return NULL;
}

uint8_t *kp_hd_ui_scr_console_art(KpRes *r, const KpFiles *sd, size_t *size, KpError *err)
{
	KpResReader *h;
	uint8_t *out;
	char why[KP_ERROR_SIZE];
	err->msg[0] = 0;
	if (kp_res_raw_held(r, KP_HD_UI_SCR_REPACK_CONSOLE)) {
		if (!(out = kp_res_raw_read(r, KP_HD_UI_SCR_REPACK_CONSOLE, size, why, sizeof why))) {
			fail(err, KP_HD_UI_SCR_REPACK_CONSOLE, why[0] ? why : "cannot be read");
		}
		return out;
	}
	if (!kp_hd_ui_scr_held(r, KP_HD_SCR_HD2)) return NULL;
	if (!(h = kp_res_reader_new())) {
		fail(err, "the consoles' art", "out of memory");
		return NULL;
	}
	out = kp_hd_ui_scr_console(r, h, sd, size, err);
	kp_res_reader_free(h);
	return out;
}
