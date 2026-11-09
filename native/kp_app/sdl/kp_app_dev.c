/*
 * The development checks (plan phase S, stage S5; the screenshots, autotest and benchmarks that the oracles and
 * measurements need, and the comparisons the scripts' checks call: docs/formats/script.md): lines "SPIKE ..." on the
 * log for the Python checkers and CI.
 */
#include "kp_app.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SETTLE_FRAMES 3     /* frames between a change and a readback */
#define AUTOTEST_TICKS 48   /* the second composition check runs after this many ticks */

/* The autotest's steps per mission: the terrain at the top-left and the bottom-right view (the world images off), then
 * the composition at the top-left and after AUTOTEST_TICKS ticks. */
enum { AT_START, AT_TERRAIN_TL, AT_TERRAIN_BR, AT_COMPOSE, AT_COMPOSE_TICKS, AT_DONE };

typedef struct {
	int shot_done;
	/* the autotest */
	int step, wait, mission, first, last, missions_done;
	int64_t start_x, start_y; /* the game's first view (its start location) */
	int64_t checked, bad, composed, failures;
	uint8_t *mcrc;
	size_t mcrc_size;
	int mcrc_tileset;
	char bar_hex[2 * KP_BAR_WIDTH * KP_BAR_ROWS + 1]; /* a health bar's pixels for the composition check's log */
	/* the fog's check (kp_dev_fog_plain, kp_dev_check_fog): the view without the fog and what covered it */
	uint8_t *fog_plain, *fog_covered;
	int32_t fog_w, fog_h; /* the logical screen they were read at */
	/* the frame benchmark: per frame its whole time and the flow's (the game, the presentation) and the drawing's */
	int frames;
	double *frame_ms, *flow_ms, *draw_ms;
	double last_frame;
} Dev;

void kp_dev_line(const char *fmt, ...)
{
	char line[2 * KP_BAR_WIDTH * KP_BAR_ROWS + 1024]; /* the longest: a health bar's pixels */
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(line, sizeof line, fmt, ap);
	va_end(ap);
#if defined(SDL_PLATFORM_ANDROID) || defined(SDL_PLATFORM_IOS)
	SDL_Log("SPIKE %s", line);
#else
	printf("SPIKE %s\n", line); /* plain lines for the checkers (tools/viewer_compose_check.py --log) */
	fflush(stdout);
#endif
}

int kp_dev_init(KpApp *a)
{
	Dev *d = (Dev *)calloc(1, sizeof *d);
	if (!d) return -1;
	a->dev = d;
	if (a->flags.autotest) {
		int k, catalog = 0;
		a->flags.fog = 0;
		a->flags.world_only = 1;
		d->first = 0;
		d->last = a->mission_count - 1;
		for (k = 0; k < a->mission_count; k++) {
			if (a->missions[k].catalog && ++catalog == a->flags.autotest_count) d->last = k;
		}
		d->mission = d->first;
		d->step = AT_START;
	}
	return 0;
}

int kp_dev_screenshot(KpApp *a, const char *path)
{
	uint8_t *rgba = (uint8_t *)malloc((size_t)a->flow.win_w * (size_t)a->flow.win_h * 4);
	SDL_Surface *s;
	int32_t r;
	int rc = -1;
	if (!rgba) return -1;
	if (kp_gfx_read_window(a->gfx, a->flow.win_x, a->flow.win_y, a->flow.win_w, a->flow.win_h, rgba) != 0) {
		free(rgba);
		kp_dev_line("screenshot %s: readback failed", path);
		return -1;
	}
	s = SDL_CreateSurface(a->flow.win_w, a->flow.win_h, SDL_PIXELFORMAT_RGBA32);
	if (s) {
		for (r = 0; r < a->flow.win_h; r++) { /* GL rows are bottom-up */
			memcpy((uint8_t *)s->pixels + (size_t)r * (size_t)s->pitch, rgba + (size_t)(a->flow.win_h - 1 - r) * (size_t)a->flow.win_w * 4,
				(size_t)a->flow.win_w * 4);
		}
		if (SDL_SavePNG(s, path)) {
			kp_dev_line("screenshot %s: %dx%d", path, (int)a->flow.win_w, (int)a->flow.win_h);
			rc = 0;
		} else {
			kp_dev_line("screenshot %s: %s", path, SDL_GetError());
		}
		SDL_DestroySurface(s);
	}
	free(rgba);
	return rc;
}

/* The index target top-down (w x h) of the last frame, or NULL. */
static uint8_t *index_rows(KpApp *a)
{
	int32_t w = a->flow.world_w, h = a->flow.world_h, r;
	uint8_t *up = (uint8_t *)malloc((size_t)w * (size_t)h), *down = (uint8_t *)malloc((size_t)w * (size_t)h);
	if (!up || !down || kp_gfx_read_index(a->gfx, 0, 0, w, h, up) != 0) {
		free(up);
		free(down);
		return NULL;
	}
	for (r = 0; r < h; r++) memcpy(down + (size_t)r * (size_t)w, up + (size_t)(h - 1 - r) * (size_t)w, (size_t)w);
	free(up);
	return down;
}

int kp_dev_check_palette(KpApp *a, const uint8_t *rows, int64_t *checked_out, int64_t *bad_out)
{
	int32_t w = a->flow.world_w, h = a->flow.world_h, x, y;
	int32_t img_w = a->flow.win_w, img_h = a->flow.win_h;
	const uint8_t *palette = kp_gfx_palette_rgba(a->gfx);
	uint8_t *rgba = (uint8_t *)malloc((size_t)a->flow.win_w * (size_t)a->flow.win_h * 4), *own = NULL;
	int64_t bad = 0, checked = 0;
	if (a->flow.mode == KP_SCALE_INTEGER) { /* the integer scale's viewport sits at the bottom-left */
		img_w = (int32_t)(w * a->flow.scale + 0.5);
		img_h = (int32_t)(h * a->flow.scale + 0.5);
	}
	if (!rows) rows = own = index_rows(a);
	if (!rgba || !rows || kp_gfx_read_window(a->gfx, a->flow.win_x, a->flow.win_y, a->flow.win_w, a->flow.win_h, rgba) != 0) {
		kp_dev_line("palette check: readback failed");
		free(rgba);
		free(own);
		return -1;
	}
	for (y = 0; y < h; y++) {
		int32_t oy = (int32_t)((y + 0.5) * img_h / h), row = img_h - 1 - oy;
		if (row < 0 || row >= a->flow.win_h) continue; /* the integer scale's image may pass the window by a few pixels */
		for (x = 0; x < w; x++) {
			int32_t ox = (int32_t)((x + 0.5) * img_w / w);
			const uint8_t *got;
			const uint8_t *want;
			if (ox >= a->flow.win_w) continue;
			checked++;
			got = rgba + ((size_t)row * (size_t)a->flow.win_w + (size_t)ox) * 4;
			want = palette + (size_t)rows[(size_t)y * (size_t)w + (size_t)x] * 4;
			if (got[0] != want[0] || got[1] != want[1] || got[2] != want[2]) {
				if (bad < 3) {
					kp_dev_line("palette check: %d,%d index %d: %02x%02x%02x, palette %02x%02x%02x", (int)x, (int)y,
						rows[(size_t)y * (size_t)w + (size_t)x], got[0], got[1], got[2], want[0], want[1], want[2]);
				}
				bad++;
			}
		}
	}
	kp_dev_line("palette check: %lld pixels, %lld mismatches", (long long)checked, (long long)bad);
	free(rgba);
	free(own);
	if (checked_out) *checked_out = checked;
	if (bad_out) *bad_out = bad;
	return 0;
}

/* Whether a drawn tile is megatile `mega` of the world's sheet under creep's edge frame `frame` (0x4c03b0: the frame's
 * pixels but index 0 over the tile). */
static int edge_drawn(const KpAppWorld *w, uint32_t mega, int32_t frame, const uint8_t *block)
{
	int32_t cols = w->tileset.sheet_columns, edge = w->tileset.sheet_count + frame, x, y, tile = KP_WORLD_TILE;
	for (y = 0; y < tile; y++) {
		const uint8_t *m = w->sheet + (size_t)((int32_t)mega / cols * tile + y) * (size_t)w->sheet_w + (size_t)((int32_t)mega % cols * tile);
		const uint8_t *e = w->sheet + (size_t)(edge / cols * tile + y) * (size_t)w->sheet_w + (size_t)(edge % cols * tile);
		for (x = 0; x < tile; x++) {
			if (block[y * tile + x] != (e[x] ? e[x] : m[x])) return 0;
		}
	}
	return 1;
}

/* Whether the interface's canvas (opaque where the console is) or the cursor lies over a view rectangle. */
static int covered(KpApp *a, int32_t x0, int32_t y0, int32_t w, int32_t h)
{
	int32_t x, y;
	if (a->cursor.shown && x0 < a->mouse_lx + a->cursor.dx + a->cursor.w && x0 + w > a->mouse_lx + a->cursor.dx
		&& y0 < a->mouse_ly + a->cursor.dy + a->cursor.h && y0 + h > a->mouse_ly + a->cursor.dy) {
		return 1;
	}
	if (!a->flow.hud_on) return 0;
	for (y = y0; y < y0 + h; y++) {
		for (x = x0; x < x0 + w; x++) {
			uint8_t index, alpha;
			kp_canvas_pixel(a->flow.hud.canvas, x, y, &index, &alpha);
			if (alpha > 0) return 1;
		}
	}
	return 0;
}

int kp_dev_check_terrain(KpApp *a, const char *label, int64_t *checked_out, int64_t *bad_out)
{
	Dev *d = (Dev *)a->dev;
	const KpAppWorld *w = &a->world;
	int32_t vw = a->flow.world_w, vh = a->flow.world_h, cx, cy, tile = KP_WORLD_TILE;
	int64_t checked = 0, bad = 0, edged = 0;
	uint8_t *rows = index_rows(a), block[32 * 32];
	if (!rows) {
		kp_dev_line("check %s %s: readback failed", a->missions[a->flow.current].name, label);
		return -1;
	}
	if (d->mcrc_tileset != (int)(w->mission->tileset & 7) || !d->mcrc) {
		char path[128];
		free(d->mcrc);
		snprintf(path, sizeof path, "generated/viewer/%s.mcrc", KP_TILESET_NAMES[w->mission->tileset]);
		d->mcrc = kp_app_read(a, path, &d->mcrc_size);
		d->mcrc_tileset = (int)(w->mission->tileset & 7);
	}
	for (cy = (int32_t)((a->flow.cam_y + tile - 1) / tile); cy < (int32_t)((a->flow.cam_y + vh) / tile); cy++) {
		for (cx = (int32_t)((a->flow.cam_x + tile - 1) / tile); cx < (int32_t)((a->flow.cam_x + vw) / tile); cx++) {
			const uint16_t *cell;
			uint32_t mega;
			int32_t y;
			if (cx >= (int32_t)w->mission->width || cy >= (int32_t)w->mission->height) continue;
			if (covered(a, cx * tile - (int32_t)a->flow.cam_x, cy * tile - (int32_t)a->flow.cam_y, tile, tile)) continue;
			for (y = 0; y < tile; y++) {
				memcpy(block + y * tile, rows + (size_t)(cy * tile + y - a->flow.cam_y) * (size_t)vw + (size_t)(cx * tile - a->flow.cam_x),
					(size_t)tile);
			}
			cell = w->cells + 2 * (size_t)(cy * (int32_t)w->mission->width + cx);
			mega = (uint32_t)(cell[0] & 0x7fff);
			checked++;
			if (cell[1]) {
				edged++;
				if (mega >= w->tileset.sheet_count || !edge_drawn(w, mega, (int32_t)cell[1] - 1, block)) bad++;
			} else if (!d->mcrc || 4 * (size_t)mega + 4 > d->mcrc_size
				|| kp_crc32(block, sizeof block) != kp_le_u32(d->mcrc + 4 * mega)) {
				bad++;
			}
		}
	}
	free(rows);
	kp_dev_line("check %s %s: view %dx%d at %d,%d, %lld megatiles (%lld under creep's edges), %lld mismatches",
		a->missions[a->flow.current].name, label, (int)vw, (int)vh, (int)a->flow.cam_x, (int)a->flow.cam_y, (long long)checked,
		(long long)edged, (long long)bad);
	if (checked_out) *checked_out = checked;
	if (bad_out) *bad_out = bad;
	return 0;
}

static void check_terrain(KpApp *a, Dev *d, const char *label)
{
	int64_t checked = 0, bad = 0;
	if (kp_dev_check_terrain(a, label, &checked, &bad) != 0) return;
	d->checked += checked;
	d->bad += bad;
}

/* The composition check: the index target's CRC (rows top-down) with the plan and the draw list of the images meeting
 * the view; tools/viewer_compose_check.py rebuilds the view on the CPU with the original kernels from the retail GRPs
 * and tilesets. */
static void check_compose(KpApp *a, Dev *d, const char *label)
{
	KpAppWorld *w = &a->world;
	const KpImagesPresenter *p = &w->presenter;
	const KpGrpMeta *grp = p->grp;
	int32_t vw = a->flow.world_w, vh = a->flow.world_h, k, count = 0;
	uint8_t *rows = index_rows(a);
	char name[128], windows[2048];
	size_t n = 0;
	if (!rows) {
		kp_dev_line("compose: readback failed");
		return;
	}
	snprintf(name, sizeof name, "%s%s", a->missions[a->flow.current].name, label);
	for (k = 0; k < p->collected_count; k++) {
		const KpDrawn *i = &p->collected[k];
		int32_t x = i->left - (int32_t)a->flow.cam_x, y = i->top - (int32_t)a->flow.cam_y;
		if (x < vw && y < vh && x + i->w > 0 && y + i->h > 0) count++;
	}
	kp_dev_line("compose %s %d %d %d %d %d %u %d", name, (int)(w->mission->tileset & 7), (int)a->flow.cam_x, (int)a->flow.cam_y,
		(int)vw, (int)vh, (unsigned)kp_crc32(rows, (size_t)vw * (size_t)vh), (int)count);
	free(rows);
	{ /* the map cells meeting the view as the game shows them, a row a line: each one's megatile word and creep border
		 * byte (kp_creep.c) */
		const KpTerrain *t = &a->flow.session->g->terrain;
		int32_t mw = (int32_t)w->mission->width, mh = (int32_t)w->mission->height, tile = KP_WORLD_TILE, cx, cy;
		int32_t cx0 = (int32_t)a->flow.cam_x / tile, cx1 = ((int32_t)a->flow.cam_x + vw + tile - 1) / tile;
		int32_t cy1 = ((int32_t)a->flow.cam_y + vh + tile - 1) / tile;
		char cells[6 * 256 + 1];
		if (cx1 > mw) cx1 = mw;
		if (cy1 > mh) cy1 = mh;
		for (cy = (int32_t)a->flow.cam_y / tile; cy < cy1; cy++) {
			size_t o = 0;
			for (cx = cx0; cx < cx1 && cx - cx0 < 256; cx++) {
				o += (size_t)snprintf(cells + o, sizeof cells - o, "%04x%02x", (unsigned)(t->megatiles[cy * mw + cx] & 0xffff),
					(unsigned)(t->creep_border[cy * mw + cx] & 0xff));
			}
			kp_dev_line("terrain %s %d %d %s", name, (int)cy, (int)cx0, cells);
		}
	}
	windows[0] = 0;
	for (k = 0; k < w->data.plan.count && n + 24 < sizeof windows; k++) {
		n += (size_t)snprintf(windows + n, sizeof windows - n, "%s%d:%d", k ? "," : "", (int)w->data.plan.windows[k].rank,
			(int)w->data.plan.windows[k].layers);
	}
	kp_dev_line("plan %s %d %d %d %s", name, (int)w->data.plan.count, (int)w->data.plan.layers, (int)p->overflow,
		windows);
	for (k = 0; k < p->collected_count; k++) {
		const KpDrawn *i = &p->collected[k];
		int32_t x = i->left - (int32_t)a->flow.cam_x, y = i->top - (int32_t)a->flow.cam_y;
		char path[128];
		if (!(x < vw && y < vh && x + i->w > 0 && y + i->h > 0)) continue;
		if (i->virtual_ || i->grp < 1) {
			snprintf(path, sizeof path, "virtual");
		} else {
			const KpLabel *l = &grp->grps[i->grp].path;
			snprintf(path, sizeof path, "%.*s", (int)l->size, (const char *)l->text);
		}
		kp_dev_line("draw %s %d %d %d %d %s %d %d %d %d %d %d %d", name, (int)i->rank, (int)i->method, (int)i->owner,
			(int)i->param, path, (int)i->frame, (int)i->left, (int)i->top, (int)i->flip, (int)i->row, (int)i->window,
			(int)i->layer);
		if (i->method == 11) { /* the bar's pixels as the presentation drew them ("--" where nothing is drawn) */
			char *hex = d->bar_hex;
			int32_t bx, by;
			size_t o = 0;
			for (by = 0; by < i->h; by++) {
				for (bx = 0; bx < i->w; bx++) {
					const uint8_t *t = &w->bars.pixels[((size_t)(i->row * KP_BAR_ROWS + by) * KP_BAR_WIDTH + (size_t)bx) * 2];
					o += (size_t)snprintf(hex + o, sizeof d->bar_hex - o, t[1] ? "%02x" : "--", t[0]);
				}
			}
			kp_dev_line("bar %s %d %d %d %s", name, (int)i->rank, (int)i->w, (int)i->h, hex);
		}
	}
	d->composed++;
}

/* The world's images synchronised now, without interpolation (still readbacks); the next frame uploads them. */
static void sync_now(KpApp *a)
{
	KpRect view = kp_world_view(a);
	kp_presenter_sync(&a->world.presenter, &a->world.data, &view, 0, 1);
}

static void autotest_start(KpApp *a, Dev *d)
{
	char err[512];
	while (d->mission <= d->last && !a->missions[d->mission].catalog) d->mission++;
	if (d->mission > d->last) {
		d->step = AT_DONE;
		return;
	}
	if (kp_flow_start_mission(&a->flow, d->mission, err, sizeof err) != 0) {
		kp_dev_line("mission %s: %s", a->missions[d->mission].name, err);
		d->failures++;
		d->mission++;
		return;
	}
	kp_session_pause(a->flow.session);
	kp_gfx_world(a->gfx, 0); /* the terrain alone */
	a->flow.cam_x = a->flow.cam_y = 0;
	d->step = AT_TERRAIN_TL;
	d->wait = SETTLE_FRAMES;
}

static void autotest_frame(KpApp *a, Dev *d)
{
	if (d->step == AT_START) {
		autotest_start(a, d);
		return;
	}
	if (d->step == AT_DONE) return;
	if (--d->wait > 0) return;
	switch (d->step) {
	case AT_TERRAIN_TL:
		d->start_x = a->flow.cam_x;
		d->start_y = a->flow.cam_y;
		check_terrain(a, d, "top-left");
		a->flow.cam_x = a->flow.cam_y = INT32_MAX;
		kp_flow_clamp_camera(&a->flow);
		d->step = AT_TERRAIN_BR;
		break;
	case AT_TERRAIN_BR:
		check_terrain(a, d, "bottom-right");
		a->flow.cam_x = d->start_x;
		a->flow.cam_y = d->start_y;
		kp_gfx_world(a->gfx, 1);
		sync_now(a);
		d->step = AT_COMPOSE;
		break;
	case AT_COMPOSE: {
		int k;
		check_compose(a, d, "");
		for (k = 0; k < AUTOTEST_TICKS; k++) kp_session_step(a->flow.session, NULL);
		kp_images_refresh_bounds(a->flow.session->g->w);
		sync_now(a);
		d->step = AT_COMPOSE_TICKS;
		break;
	}
	case AT_COMPOSE_TICKS:
		check_compose(a, d, "@48");
		d->missions_done++;
		d->mission++;
		d->step = AT_START;
		break;
	default:
		break;
	}
	d->wait = SETTLE_FRAMES;
}

int kp_dev_fog_plain(KpApp *a)
{
	Dev *d = (Dev *)a->dev;
	free(d->fog_plain);
	free(d->fog_covered);
	d->fog_plain = index_rows(a);
	d->fog_covered = (uint8_t *)calloc((size_t)a->flow.world_w * (size_t)a->flow.world_h, 1);
	d->fog_w = a->flow.world_w;
	d->fog_h = a->flow.world_h;
	if (!d->fog_plain || !d->fog_covered) { /* neither kept: a later fog check finds no plain view */
		free(d->fog_plain);
		free(d->fog_covered);
		d->fog_plain = d->fog_covered = NULL;
		kp_dev_line("fog check: readback failed");
		return -1;
	}
	if (a->flow.hud_on) { /* the canvas then: a text line may go before the next readback */
		int32_t x, y;
		for (y = 0; y < a->flow.world_h; y++) {
			for (x = 0; x < a->flow.world_w; x++) {
				uint8_t index;
				kp_canvas_pixel(a->flow.hud.canvas, x, y, &index, &d->fog_covered[(size_t)y * (size_t)a->flow.world_w + (size_t)x]);
			}
		}
	}
	if (a->cursor.shown) { /* the cursor then: over a unit its animation's next frame may be another size */
		int32_t x, y;
		for (y = a->mouse_ly + a->cursor.dy; y < a->mouse_ly + a->cursor.dy + a->cursor.h; y++) {
			for (x = a->mouse_lx + a->cursor.dx; x < a->mouse_lx + a->cursor.dx + a->cursor.w; x++) {
				if (x >= 0 && y >= 0 && x < a->flow.world_w && y < a->flow.world_h) {
					d->fog_covered[(size_t)y * (size_t)a->flow.world_w + (size_t)x] = 1;
				}
			}
		}
	}
	return 0;
}

int kp_dev_check_fog(KpApp *a, int64_t *compared_out, int64_t *bad_out)
{
	Dev *d = (Dev *)a->dev;
	int32_t w = a->flow.world_w, h = a->flow.world_h, x, y;
	uint8_t *got, *want;
	int64_t compared = 0, darkened = 0, bad = 0;
	int rc = -1;
	if (!d->fog_plain) {
		kp_dev_line("fog check: no view without the fog before it");
		return -1;
	}
	if (d->fog_w != w || d->fog_h != h) {
		kp_dev_line("fog check: the screen is %dx%d, the view without the fog %dx%d", (int)w, (int)h, (int)d->fog_w,
			(int)d->fog_h);
		return -1;
	}
	got = index_rows(a);
	want = (uint8_t *)malloc((size_t)w * (size_t)h);
	if (!got || !want) {
		kp_dev_line("fog check: readback failed");
	} else {
		memcpy(want, d->fog_plain, (size_t)w * (size_t)h);
		kp_fog_view_shade(&a->world.fog.view, a->flow.cam_x, a->flow.cam_y, a->palettes.tilesets[a->world.mission->tileset & 7].dark,
			want, w, h, w);
		for (y = 0; y < h; y++) {
			for (x = 0; x < w; x++) {
				size_t i = (size_t)y * (size_t)w + (size_t)x;
				uint8_t index, alpha = 0;
				/* the interface's canvas covers the world where it is opaque: the fog lies under it */
				if (a->flow.hud_on) kp_canvas_pixel(a->flow.hud.canvas, x, y, &index, &alpha);
				if (alpha > 0 || (d->fog_covered && d->fog_covered[i])) continue;
				/* the cursor lies over the fog too */
				if (a->cursor.shown && x >= a->mouse_lx + a->cursor.dx && x < a->mouse_lx + a->cursor.dx + a->cursor.w
					&& y >= a->mouse_ly + a->cursor.dy && y < a->mouse_ly + a->cursor.dy + a->cursor.h) {
					continue;
				}
				compared++;
				bad += got[i] != want[i];
				darkened += want[i] != d->fog_plain[i];
			}
		}
		kp_dev_line("fog check: %lld pixels, %lld darkened, %lld mismatches", (long long)compared, (long long)darkened,
			(long long)bad);
		if (compared_out) *compared_out = compared;
		if (bad_out) *bad_out = bad;
		rc = 0;
	}
	free(got);
	free(want);
	return rc;
}

static int by_value(const void *x, const void *y)
{
	double a = *(const double *)x, b = *(const double *)y;
	return a < b ? -1 : a > b;
}

/* "mean / p95 / max" of n values (sorted in place). */
static void stats(double *v, int n, char *out, size_t size)
{
	double sum = 0;
	int k;
	qsort(v, (size_t)n, sizeof *v, by_value);
	for (k = 0; k < n; k++) sum += v[k];
	snprintf(out, size, "%.3f / %.3f / %.3f ms", sum / n, v[(int)(n * 0.95) < n ? (int)(n * 0.95) : n - 1], v[n - 1]);
}

void kp_dev_timing(KpApp *a, double flow_ms, double draw_ms)
{
	Dev *d = (Dev *)a->dev;
	double now = kp_app_wall();
	int skip = 60; /* the first frames: the start, the first uploads */
	if (!d || a->flags.bench_frames <= 0 || !a->world.active) return;
	if (!d->frame_ms && !d->flow_ms && !d->draw_ms) {
		d->frame_ms = (double *)calloc((size_t)a->flags.bench_frames, sizeof(double));
		d->flow_ms = (double *)calloc((size_t)a->flags.bench_frames, sizeof(double));
		d->draw_ms = (double *)calloc((size_t)a->flags.bench_frames, sizeof(double));
		d->frames = -skip;
	}
	if (!d->frame_ms || !d->flow_ms || !d->draw_ms) return; /* out of memory: no benchmark */
	if (d->frames >= 0 && d->frames < a->flags.bench_frames) {
		d->frame_ms[d->frames] = (now - d->last_frame) * 1000;
		d->flow_ms[d->frames] = flow_ms;
		d->draw_ms[d->frames] = draw_ms;
	}
	d->last_frame = now;
	if (++d->frames == a->flags.bench_frames) {
		char f[64], g[64], h[64];
		stats(d->frame_ms, d->frames, f, sizeof f);
		stats(d->flow_ms, d->frames, g, sizeof g);
		stats(d->draw_ms, d->frames, h, sizeof h);
		kp_dev_line("frame bench %s: %d frames %dx%d (logical %dx%d), vsync %s; frame %s; game and presentation %s; "
			"drawing (CPU) %s; images %d, windows %d, layers %d", a->missions[a->flow.current].name, d->frames,
			(int)a->flow.win_w, (int)a->flow.win_h, (int)a->flow.world_w, (int)a->flow.world_h, a->flags.vsync ? "on" : "off", f, g, h,
			(int)a->world.presenter.drawn, (int)a->world.data.plan.count, (int)a->world.data.plan.layers);
		a->quit = 1;
	}
}

int kp_dev_check_hd(KpApp *a, const char *label, uint32_t dropped)
{
	const KpCanvas *c = kp_flow_glue_active(&a->flow) ? &a->flow.glue.canvas : a->flow.hud.canvas;
	KpHdUiCoverage cov;
	int64_t sd;
	if (!a->hd_ui.head || !c) {
		kp_dev_line("hd check: no HD interface (--data with StarCraft: Remastered's HD art, a scale of 2 or more)");
		return -1;
	}
	kp_hd_ui_coverage(a, c, &cov);
	sd = cov.lost + cov.outside + cov.translucent + cov.waiting + cov.failed + cov.unlogged;
	kp_dev_line("hd check %s: %lld HD pixels; SD over HD art: %lld lost, %lld outside, %lld through, %lld waiting, "
		"%lld unreadable; %lld drawn outside the log; %u log drops; no HD art: %lld pixels%s%s%s%s%s%s%s%s", label,
		(long long)cov.hd, (long long)cov.lost, (long long)cov.outside, (long long)cov.translucent,
		(long long)cov.waiting, (long long)cov.failed, (long long)cov.unlogged,
		a->flow.ui->image_log.dropped - dropped, (long long)cov.no_art, cov.no_art_paths[0] ? " " : "",
		cov.no_art_paths, cov.outside_paths[0] ? "; outside: " : "", cov.outside_paths,
		cov.translucent_paths[0] ? "; through: " : "", cov.translucent_paths,
		cov.unlogged_paths[0] ? "; drawn over outside the log: " : "", cov.unlogged_paths);
	return sd > 0 || a->flow.ui->image_log.dropped != dropped;
}

void kp_dev_frame(KpApp *a)
{
	Dev *d = (Dev *)a->dev;
	double now = kp_app_now(a);
	if (a->script) { /* a script refuses the checks' flags (kp_app_script_load) */
		kp_app_script_frame(a);
		return;
	}
	if (!d) return;
	if (a->flags.autotest) {
		autotest_frame(a, d);
		if (d->step == AT_DONE) {
			kp_dev_line("autotest: %d missions, %lld megatiles checked, %lld mismatches; composed views: %lld; load failures: %lld",
				d->missions_done, (long long)d->checked, (long long)d->bad, (long long)d->composed, (long long)d->failures);
			a->quit = 1;
		}
	}
	if (a->flags.cine_check[0]) {
		kp_app_cine_check(a, a->flags.cine_check);
		a->quit = 1;
		return;
	}
	if (a->flags.camera && a->world.active) {
		a->flow.cam_x = a->flags.camera_x;
		a->flow.cam_y = a->flags.camera_y;
		kp_flow_clamp_camera(&a->flow);
	}
	if (a->flags.screenshot[0] && !d->shot_done && now - a->started >= a->flags.screenshot_after) {
		d->shot_done = 1;
		(void)kp_dev_screenshot(a, a->flags.screenshot);
		if (!a->flags.autotest && !a->flags.bench_frames) a->quit = 1;
	}
}

void kp_dev_free(KpApp *a)
{
	Dev *d = (Dev *)a->dev;
	if (d) {
		free(d->frame_ms);
		free(d->flow_ms);
		free(d->draw_ms);
		free(d->mcrc);
		free(d->fog_plain);
		free(d->fog_covered);
	}
	free(a->dev);
	a->dev = NULL;
}
