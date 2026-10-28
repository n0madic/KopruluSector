/*
 * The development checks (plan phase S, stage S5; the screenshots, autotest, play checks and benchmarks that the
 * oracles and measurements need): lines "SPIKE ..." on the log for the Python checkers.
 */
#include "kp_app.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SETTLE_FRAMES 3     /* frames between a change and a readback */
#define AUTOTEST_TICKS 48   /* the second composition check runs after this many ticks */

/* The autotest's steps per mission: the terrain at the top-left and the bottom-right view (the world images off), then
 * the composition at the top-left and after AUTOTEST_TICKS ticks. */
enum { AT_START, AT_TERRAIN_TL, AT_TERRAIN_BR, AT_COMPOSE, AT_COMPOSE_TICKS, AT_NEXT, AT_DONE };

typedef struct {
	int shot_done;
	/* the autotest */
	int step, wait, mission, first, last, missions_done;
	int64_t start_x, start_y; /* the game's first view (its start location) */
	int64_t checked, bad, composed, failures;
	uint8_t *mcrc;
	size_t mcrc_size;
	int mcrc_tileset;
	/* the play check */
	int pc_stage, pc_wait, pc_selected, pc_shot;
	int64_t pc_creep; /* the creep tiles when the box was drawn */
	int64_t pc_units[12];
	double pc_linger;
	uint8_t *pc_plain, *pc_covered;
	/* the frame benchmark: per frame its whole time and the flow's (the game, the presentation) and the drawing's */
	int frames;
	double *frame_ms, *flow_ms, *draw_ms;
	double last_frame;
	/* the video check */
	int vc_stage, vc_step, vc_wait, vc_started, vc_offered, vc_before, vc_kept, vc_failures;
	int vc_stage_seen, vc_stage_frames; /* the stage the frames below were counted in */
	KpUiVideoOptions vc_start; /* the options at the start (Cancel puts them back) */
	int vc_start_gamma, vc_start_portraits;
	char bar_hex[2 * KP_BAR_WIDTH * KP_BAR_ROWS + 1]; /* a health bar's pixels for the composition check's log */
	/* the HD interface's check */
	int hc_step, hc_stage, hc_wait, hc_frames, hc_escapes, hc_failures;
	int hc_typing; /* the victory cheat's line: 1 its text next, 2 its Return next */
	double hc_since, hc_until, hc_skipped; /* the step's wait began; its screen settled; the last skip */
	uint32_t hc_dropped; /* the image log's drops at the step's start */
	/* the save check */
	int sc_stage, sc_wait, sc_frames, sc_failures;
	int sc_after; /* the stage after a loaded game's trigger wait ended */
	int64_t sc_x, sc_y, sc_tick; /* the saved view and step */
	uint32_t sc_portrait_ms;     /* the trigger portrait's time left at the save */
	double sc_deadline;          /* the loaded game's trigger wait ends before this */
	/* the touch check */
	int tc_stage, tc_failures, tc_steps;
	double tc_at;       /* the stage's next move is due */
	int64_t tc_unit;    /* the unit tapped */
	int64_t tc_ux, tc_uy, tc_cam_x, tc_cam_y;
	int32_t tc_px, tc_py; /* the pad button pressed */
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

/* The window's frame as a PNG (the drawn back buffer, before the swap). */
static void screenshot(KpApp *a, const char *path)
{
	uint8_t *rgba = (uint8_t *)malloc((size_t)a->win_w * (size_t)a->win_h * 4);
	SDL_Surface *s;
	int32_t r;
	if (!rgba) return;
	if (kp_gfx_read_window(a->gfx, a->safe_x, a->safe_y, a->win_w, a->win_h, rgba) != 0) {
		free(rgba);
		kp_dev_line("screenshot %s: readback failed", path);
		return;
	}
	s = SDL_CreateSurface(a->win_w, a->win_h, SDL_PIXELFORMAT_RGBA32);
	if (s) {
		for (r = 0; r < a->win_h; r++) { /* GL rows are bottom-up */
			memcpy((uint8_t *)s->pixels + (size_t)r * (size_t)s->pitch, rgba + (size_t)(a->win_h - 1 - r) * (size_t)a->win_w * 4,
				(size_t)a->win_w * 4);
		}
		SDL_SavePNG(s, path);
		SDL_DestroySurface(s);
		kp_dev_line("screenshot %s: %dx%d", path, (int)a->win_w, (int)a->win_h);
	}
	free(rgba);
}

/* The index target top-down (w x h) of the last frame, or NULL. */
static uint8_t *index_rows(KpApp *a)
{
	int32_t w = a->world_w, h = a->world_h, r;
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

/* The palette pass's check: each logical pixel's colour in the window (the window pixel at its centre, inside the
 * texel's nearest region of the sharp upscale) against the palette's entry of its index (rows top-down, w x h). */
static void check_palette(KpApp *a, const uint8_t *rows)
{
	int32_t w = a->world_w, h = a->world_h, x, y;
	int32_t img_w = a->win_w, img_h = a->win_h;
	const uint8_t *palette = kp_gfx_palette_rgba(a->gfx);
	uint8_t *rgba = (uint8_t *)malloc((size_t)a->win_w * (size_t)a->win_h * 4);
	int64_t bad = 0, checked = 0;
	if (a->mode == KP_SCALE_INTEGER) { /* the integer scale's viewport sits at the bottom-left */
		img_w = (int32_t)(w * a->scale + 0.5);
		img_h = (int32_t)(h * a->scale + 0.5);
	}
	if (!rgba || kp_gfx_read_window(a->gfx, a->safe_x, a->safe_y, a->win_w, a->win_h, rgba) != 0) {
		kp_dev_line("palette check: readback failed");
		free(rgba);
		return;
	}
	for (y = 0; y < h; y++) {
		int32_t oy = (int32_t)((y + 0.5) * img_h / h), row = img_h - 1 - oy;
		if (row < 0 || row >= a->win_h) continue; /* the integer scale's image may pass the window by a few pixels */
		for (x = 0; x < w; x++) {
			int32_t ox = (int32_t)((x + 0.5) * img_w / w);
			const uint8_t *got;
			const uint8_t *want;
			if (ox >= a->win_w) continue;
			checked++;
			got = rgba + ((size_t)row * (size_t)a->win_w + (size_t)ox) * 4;
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
	if (!a->hud_on) return 0;
	for (y = y0; y < y0 + h; y++) {
		for (x = x0; x < x0 + w; x++) {
			uint8_t index, alpha;
			kp_canvas_pixel(a->hud.canvas, x, y, &index, &alpha);
			if (alpha > 0) return 1;
		}
	}
	return 0;
}

/* The byte check of the terrain against the converter's CRC of each megatile (generated/viewer/<tileset>.mcrc), or
 * under creep's edge against the world's sheet: every megatile fully inside the view that the interface leaves
 * uncovered. */
static void check_terrain(KpApp *a, Dev *d, const char *label)
{
	const KpAppWorld *w = &a->world;
	int32_t vw = a->world_w, vh = a->world_h, cx, cy, tile = KP_WORLD_TILE;
	int64_t checked = 0, bad = 0, edged = 0;
	uint8_t *rows = index_rows(a), block[32 * 32];
	if (!rows) {
		kp_dev_line("check %s %s: readback failed", a->missions[a->current].name, label);
		return;
	}
	if (d->mcrc_tileset != (int)(w->mission->tileset & 7) || !d->mcrc) {
		char path[128];
		free(d->mcrc);
		snprintf(path, sizeof path, "generated/viewer/%s.mcrc", KP_TILESET_NAMES[w->mission->tileset]);
		d->mcrc = kp_app_read(a, path, &d->mcrc_size);
		d->mcrc_tileset = (int)(w->mission->tileset & 7);
	}
	for (cy = (int32_t)((a->cam_y + tile - 1) / tile); cy < (int32_t)((a->cam_y + vh) / tile); cy++) {
		for (cx = (int32_t)((a->cam_x + tile - 1) / tile); cx < (int32_t)((a->cam_x + vw) / tile); cx++) {
			const uint16_t *cell;
			uint32_t mega;
			int32_t y;
			if (cx >= (int32_t)w->mission->width || cy >= (int32_t)w->mission->height) continue;
			if (covered(a, cx * tile - (int32_t)a->cam_x, cy * tile - (int32_t)a->cam_y, tile, tile)) continue;
			for (y = 0; y < tile; y++) {
				memcpy(block + y * tile, rows + (size_t)(cy * tile + y - a->cam_y) * (size_t)vw + (size_t)(cx * tile - a->cam_x),
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
		a->missions[a->current].name, label, (int)vw, (int)vh, (int)a->cam_x, (int)a->cam_y, (long long)checked,
		(long long)edged, (long long)bad);
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
	int32_t vw = a->world_w, vh = a->world_h, k, count = 0;
	uint8_t *rows = index_rows(a);
	char name[128], windows[2048];
	size_t n = 0;
	if (!rows) {
		kp_dev_line("compose: readback failed");
		return;
	}
	snprintf(name, sizeof name, "%s%s", a->missions[a->current].name, label);
	for (k = 0; k < p->collected_count; k++) {
		const KpDrawn *i = &p->collected[k];
		int32_t x = i->left - (int32_t)a->cam_x, y = i->top - (int32_t)a->cam_y;
		if (x < vw && y < vh && x + i->w > 0 && y + i->h > 0) count++;
	}
	kp_dev_line("compose %s %d %d %d %d %d %u %d", name, (int)(w->mission->tileset & 7), (int)a->cam_x, (int)a->cam_y,
		(int)vw, (int)vh, (unsigned)kp_crc32(rows, (size_t)vw * (size_t)vh), (int)count);
	free(rows);
	{ /* the map cells meeting the view as the game shows them, a row a line: each one's megatile word and creep border
		 * byte (kp_creep.c) */
		const KpTerrain *t = &a->session->g->terrain;
		int32_t mw = (int32_t)w->mission->width, mh = (int32_t)w->mission->height, tile = KP_WORLD_TILE, cx, cy;
		int32_t cx0 = (int32_t)a->cam_x / tile, cx1 = ((int32_t)a->cam_x + vw + tile - 1) / tile;
		int32_t cy1 = ((int32_t)a->cam_y + vh + tile - 1) / tile;
		char cells[6 * 256 + 1];
		if (cx1 > mw) cx1 = mw;
		if (cy1 > mh) cy1 = mh;
		for (cy = (int32_t)a->cam_y / tile; cy < cy1; cy++) {
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
		int32_t x = i->left - (int32_t)a->cam_x, y = i->top - (int32_t)a->cam_y;
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
	if (kp_flow_start_mission(a, d->mission, err, sizeof err) != 0) {
		kp_dev_line("mission %s: %s", a->missions[d->mission].name, err);
		d->failures++;
		d->mission++;
		return;
	}
	kp_session_pause(a->session);
	kp_gfx_world(a->gfx, 0); /* the terrain alone */
	a->cam_x = a->cam_y = 0;
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
		d->start_x = a->cam_x;
		d->start_y = a->cam_y;
		check_terrain(a, d, "top-left");
		a->cam_x = a->cam_y = INT32_MAX;
		kp_flow_clamp_camera(a);
		d->step = AT_TERRAIN_BR;
		break;
	case AT_TERRAIN_BR:
		check_terrain(a, d, "bottom-right");
		a->cam_x = d->start_x;
		a->cam_y = d->start_y;
		kp_gfx_world(a->gfx, 1);
		sync_now(a);
		d->step = AT_COMPOSE;
		break;
	case AT_COMPOSE: {
		int k;
		check_compose(a, d, "");
		for (k = 0; k < AUTOTEST_TICKS; k++) kp_session_step(a->session, NULL);
		kp_images_refresh_bounds(a->session->g->w);
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

/* The play checks: a box over units of a scenario, a right click away from them, then the game's steps until `steps`.
 * 1: the five Marines of marine_group; 2: twelve Marines of bench_200, whose paths are searched in one step; 3: the
 * front of the human player's half of bench_combat walks into the computer's half and both sides fight; 4: the first
 * campaign mission, whose triggers play their WAVs; 5: three Marines kill a computer's Creep Colony beside its Spawning
 * Pool (creep_recede), whose creep the pool does not keep recedes. Then the terrain's check (the view without the images
 * and the fog: every megatile, creep's edges included, as the game shows it now) and the fog's: the view without and
 * with the fog. */
static const struct {
	const char *mission;
	int32_t box[4], target[2];
	int64_t steps;
} PLAY_CHECKS[KP_APP_PLAY_CHECKS] = {
	{ "scenarios/marine_group", { 980, 1080, 1090, 1150 }, { 1300, 1110 }, 140 },
	{ "scenarios/bench_200", { 990, 1350, 1520, 1370 }, { 1700, 1150 }, 140 },
	{ "scenarios/bench_combat", { 1100, 1250, 1210, 1460 }, { 1486, 1319 }, 600 },
	{ "terran/terran01", { 300, 3700, 400, 3800 }, { 500, 3700 }, 600 },
	{ "scenarios/creep_recede", { 970, 1060, 1030, 1180 }, { 1216, 1088 }, 1200 },
};
enum { PC_START, PC_BOX, PC_RIGHT, PC_RUN, PC_TERRAIN, PC_FOG_OFF, PC_FOG_ON, PC_LINGER, PC_DONE };

static void units_by_owner(KpGame *g, int64_t counts[12])
{
	int64_t n;
	memset(counts, 0, sizeof(int64_t) * 12);
	for (n = g->lists[KP_LIST_ACTIVE].head; n > 0; n = g->u.next[n]) {
		if (g->u.owner[n] >= 0 && g->u.owner[n] < 12) counts[g->u.owner[n]]++;
	}
}

static void play_check(KpApp *a, Dev *d)
{
	int c = a->flags.play_check - 1;
	KpSession *s = a->session;
	if (d->pc_stage == PC_START) {
		char err[512];
		int k;
		for (k = 0; k < a->mission_count; k++) {
			if (strcmp(a->missions[k].name, PLAY_CHECKS[c].mission) == 0) break;
		}
		if (k == a->mission_count || kp_flow_start_mission(a, k, err, sizeof err) != 0) {
			kp_dev_line("play check: no game of %s", PLAY_CHECKS[c].mission);
			d->pc_stage = PC_DONE;
			return;
		}
		d->pc_stage = PC_BOX;
		d->pc_wait = SETTLE_FRAMES;
		return;
	}
	if (!s || !s->g) return;
	if (d->pc_stage == PC_BOX) {
		const int32_t *b = PLAY_CHECKS[c].box;
		int k;
		/* the first frames place the start location's view (the console's first frame, which a world still waiting for
		 * its HD pages may delay past the settling frames); then the camera between the box and the target */
		if (--d->pc_wait > 0 || a->hud.has_start) return;
		a->cam_x = (b[0] + PLAY_CHECKS[c].target[0]) / 2 - a->world_w / 2;
		a->cam_y = (b[1] + b[3]) / 2 - a->world_h / 2;
		kp_flow_clamp_camera(a);
		units_by_owner(s->g, d->pc_units);
		{
			int64_t receding;
			d->pc_creep = kp_creep_tiles(s->g, &receding);
		}
		kp_input_mouse(a, KP_UI_LEFT_DOWN, b[0] - (int32_t)a->cam_x, b[1] - (int32_t)a->cam_y);
		kp_input_mouse(a, KP_UI_LEFT_UP, b[2] - (int32_t)a->cam_x, b[3] - (int32_t)a->cam_y);
		d->pc_selected = 0;
		for (k = 0; k < KP_SLOTS; k++) d->pc_selected += s->g->local_selection[k] != 0;
		d->pc_stage = PC_RIGHT;
	} else if (d->pc_stage == PC_RIGHT && s->tick >= 2) {
		int32_t x = PLAY_CHECKS[c].target[0] - (int32_t)a->cam_x, y = PLAY_CHECKS[c].target[1] - (int32_t)a->cam_y;
		kp_input_mouse(a, KP_UI_RIGHT_DOWN, x, y);
		kp_input_mouse(a, KP_UI_RIGHT_UP, x, y);
		d->pc_stage = PC_RUN;
	} else if (d->pc_stage == PC_RUN && (s->failed || s->tick >= PLAY_CHECKS[c].steps)) {
		int64_t counts[12];
		char units[512], xs[512];
		size_t n = 0, m = 0;
		int k;
		xs[0] = units[0] = 0;
		for (k = 0; k < KP_SLOTS; k++) {
			int64_t u = s->g->local_selection[k];
			if (u && m + 24 < sizeof xs) m += (size_t)snprintf(xs + m, sizeof xs - m, "%s%lld,%lld", m ? " " : "",
				(long long)s->g->u.x[u], (long long)s->g->u.y[u]);
		}
		units_by_owner(s->g, counts);
		for (k = 0; k < 12; k++) {
			if ((counts[k] || d->pc_units[k]) && n + 32 < sizeof units) {
				n += (size_t)snprintf(units + n, sizeof units - n, "%s%d: %lld -> %lld", n ? ", " : "", k,
					(long long)d->pc_units[k], (long long)counts[k]);
			}
		}
		kp_dev_line("play check %s: %d selected by the box, step %lld, %s%s; selection at %s; units by owner %s; steps %.2f ms "
			"mean, %.2f ms max; native core; %lld sounds started, %lld streams, music track %d%s",
			PLAY_CHECKS[c].mission, d->pc_selected, (long long)s->tick, s->failed ? "stopped: " : "running",
			s->failed ? s->error : "", xs, units, s->step_ms / (double)(s->tick > 0 ? s->tick : 1), s->step_max_ms,
			(long long)a->audio.started, (long long)a->audio.streamed, a->audio.track,
			a->audio.has_track ? "" : " (not playing)");
		/* the terrain's check, then the fog's: the view without and with the fog, the game paused between them */
		kp_session_pause(s);
		a->flags.fog = 0;
		kp_gfx_world(a->gfx, 0); /* the terrain alone */
		d->pc_stage = PC_TERRAIN;
		d->pc_wait = SETTLE_FRAMES;
	} else if (d->pc_stage == PC_TERRAIN) {
		int64_t receding, now;
		if (--d->pc_wait > 0) return;
		check_terrain(a, d, "after the play");
		now = kp_creep_tiles(s->g, &receding);
		kp_dev_line("creep check: %lld creep tiles when the box was drawn, %lld now, %lld receding", (long long)d->pc_creep,
			(long long)now, (long long)receding);
		kp_gfx_world(a->gfx, 1);
		d->pc_stage = PC_FOG_OFF;
		d->pc_wait = SETTLE_FRAMES;
	} else if (d->pc_stage == PC_FOG_OFF) {
		if (--d->pc_wait > 0) return;
		free(d->pc_plain);
		free(d->pc_covered);
		d->pc_plain = index_rows(a);
		d->pc_covered = (uint8_t *)calloc((size_t)a->world_w * (size_t)a->world_h, 1);
		if (d->pc_covered && a->hud_on) { /* the canvas then: a text line may go before the next readback */
			int32_t x, y;
			for (y = 0; y < a->world_h; y++) {
				for (x = 0; x < a->world_w; x++) {
					uint8_t index;
					kp_canvas_pixel(a->hud.canvas, x, y, &index, &d->pc_covered[(size_t)y * (size_t)a->world_w + (size_t)x]);
				}
			}
		}
		a->flags.fog = 1;
		d->pc_stage = PC_FOG_ON;
		d->pc_wait = SETTLE_FRAMES;
	} else if (d->pc_stage == PC_FOG_ON) {
		int32_t w = a->world_w, h = a->world_h, x, y;
		uint8_t *got, *want;
		int64_t compared = 0, darkened = 0, bad = 0;
		if (--d->pc_wait > 0) return;
		got = index_rows(a);
		want = (uint8_t *)malloc((size_t)w * (size_t)h);
		if (!got || !want || !d->pc_plain) {
			kp_dev_line("fog check: readback failed");
		} else {
			memcpy(want, d->pc_plain, (size_t)w * (size_t)h);
			kp_fog_view_shade(&a->world.fog.view, a->cam_x, a->cam_y, a->palettes.tilesets[a->world.mission->tileset & 7].dark,
				want, w, h, w);
			for (y = 0; y < h; y++) {
				for (x = 0; x < w; x++) {
					size_t i = (size_t)y * (size_t)w + (size_t)x;
					uint8_t index, alpha;
					/* the interface's canvas covers the world where it is opaque: the fog lies under it */
					kp_canvas_pixel(a->hud.canvas, x, y, &index, &alpha);
					if ((a->hud_on && alpha > 0) || (d->pc_covered && d->pc_covered[i])) continue;
					/* the cursor lies over the fog too */
					if (a->cursor.shown && x >= a->mouse_lx + a->cursor.dx && x < a->mouse_lx + a->cursor.dx + a->cursor.w
						&& y >= a->mouse_ly + a->cursor.dy && y < a->mouse_ly + a->cursor.dy + a->cursor.h) {
						continue;
					}
					compared++;
					bad += got[i] != want[i];
					darkened += want[i] != d->pc_plain[i];
				}
			}
			kp_dev_line("fog check: %lld pixels, %lld darkened, %lld mismatches", (long long)compared, (long long)darkened,
				(long long)bad);
			check_palette(a, got);
		}
		free(got);
		free(want);
		kp_session_resume(s);
		d->pc_stage = PC_LINGER;
		d->pc_linger = kp_app_now() + 3;
	} else if (d->pc_stage == PC_LINGER) {
		if (a->flags.screenshot[0] && !d->pc_shot && kp_app_now() > d->pc_linger - 2) {
			d->pc_shot = 1;
			screenshot(a, a->flags.screenshot);
		}
		if (kp_app_now() >= d->pc_linger) d->pc_stage = PC_DONE;
	}
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
	double now = kp_app_now();
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
			"drawing (CPU) %s; images %d, windows %d, layers %d", a->missions[a->current].name, d->frames,
			(int)a->win_w, (int)a->win_h, (int)a->world_w, (int)a->world_h, a->flags.vsync ? "on" : "off", f, g, h,
			(int)a->world.presenter.drawn, (int)a->world.data.plan.count, (int)a->world.data.plan.layers);
		a->quit = 1;
	}
}

/* The video check (--video-check DIR): a game of --mission, its video options opened by the keys (F10, O, V), then
 * each of its choices clicked where the dialog shows it; after each the choice is checked (the option's value, the
 * world's art: an HD variant's scale or the original's, the effects' path; a choice the player cannot have stays
 * disabled and changes nothing), logged, and the window saved as DIR/<step>.png. Cancel puts every option back, then
 * HD (2x) and OK keep it (a check: the profile is not written). The last line counts the failures. */
#define VC_START_FRAMES 90 /* frames before the menu: the world's first view drawn */
#define VC_SETTLE 45       /* frames after a click: the art loaded again, its first pages up */
#define VC_KEY_FRAMES 10   /* frames between the steps (a menu's key: then its dialog awaited) */
#define VC_GAMMA_CLICKS 5  /* the slider's track pages 10 a click: 100 to its 140 */
#define VC_STAGE_FRAMES 1200 /* frames a stage may wait for its dialog before the check gives up */
#define VC_HD2_SCALE 2     /* the HD variants' art scales (the world's hd_scale) */
#define VC_HD_SCALE 4
enum { VC_START, VC_OPTIONS, VC_VIDEO, VC_CLICK, VC_SHOT, VC_CANCELLED, VC_DONE };
static const struct {
	const char *label;
	int16_t id; /* the option button clicked (kp_ui.h's KP_VIDEO_ID_*); 0 the gamma slider's right end */
} VIDEO_STEPS[] = {
	{ "hd2", KP_VIDEO_CHOICE_ID(KP_VIDEO_ID_GRAPHICS, KP_GRAPHICS_HD2) },
	{ "hd", KP_VIDEO_CHOICE_ID(KP_VIDEO_ID_GRAPHICS, KP_GRAPHICS_HD) },
	{ "original", KP_VIDEO_CHOICE_ID(KP_VIDEO_ID_GRAPHICS, KP_GRAPHICS_ORIGINAL) },
	{ "smooth-fog", KP_VIDEO_CHOICE_ID(KP_VIDEO_ID_FOG, KP_FOG_SMOOTH) },
	{ "animated-water", KP_VIDEO_CHOICE_ID(KP_VIDEO_ID_WATER, KP_WATER_ANIMATED) },
	{ "vector-text", KP_VIDEO_CHOICE_ID(KP_VIDEO_ID_FONT, KP_VTEXT_VECTOR) },
	{ "gamma", 0 },
	{ "still-portraits", KP_VIDEO_ID_PORTRAITS + KP_UI_PORTRAITS_STILL },
	{ "auto", KP_VIDEO_CHOICE_ID(KP_VIDEO_ID_GRAPHICS, KP_VIDEO_AUTO) },
};
#define VIDEO_STEP_COUNT ((int)(sizeof VIDEO_STEPS / sizeof VIDEO_STEPS[0]))
#define VC_KEEP_ID KP_VIDEO_CHOICE_ID(KP_VIDEO_ID_GRAPHICS, KP_GRAPHICS_HD2) /* after Cancel: HD (2x), then OK */

static void vc_key(KpApp *a, uint16_t vk, uint16_t ch)
{
	kp_ui_window_key(a->ui, 1, vk, 0);
	if (ch) kp_ui_window_char(a->ui, ch, 0);
	kp_ui_window_key(a->ui, 0, vk, 0);
}

/* The video options' control `id` (0: the gamma slider), or -1. */
static int32_t vc_control(const KpApp *a, int16_t id)
{
	int32_t d = a->ui->modeless;
	return d >= 0 ? kp_ui_find_control(a->ui, d, id ? id : KP_VIDEO_ID_GAMMA) : -1;
}

/* A click on the control (its centre; the slider's right end), as the mouse gives it. */
static int vc_click(KpApp *a, int16_t id)
{
	int32_t c = vc_control(a, id), x, y;
	const KpUiControl *k;
	if (c < 0 || !kp_canvas_place(a->hud.canvas, a->ui->modeless, &x, &y)) return -1;
	k = &a->ui->controls[c];
	x += id ? (k->left + k->right) / 2 : k->right - 2;
	y += (k->top + k->bottom) / 2;
	kp_input_mouse(a, KP_UI_MOUSE_MOVE, x, y);
	kp_input_mouse(a, KP_UI_LEFT_DOWN, x, y);
	kp_input_mouse(a, KP_UI_LEFT_UP, x, y);
	return 0;
}

/* Whether the open modeless dialog is the one titled `name` (a menu button's activation follows its press). */
static int vc_dialog(const KpApp *a, const char *name)
{
	int32_t d = a->ui->modeless;
	return d >= 0 && a->ui->controls[d].text && strcmp((const char *)a->ui->controls[d].text, name) == 0;
}

/* An option button's group (its first ID, KP_VIDEO_ID_*). */
static int vc_group(int16_t id) { return id / KP_VIDEO_GROUP_IDS * KP_VIDEO_GROUP_IDS; }

/* The option a button's group sets (the portraits' mode for its group). */
static int vc_value(const KpApp *a, int16_t id)
{
	const KpUiVideoOptions *o = &a->ui->video_options;
	switch (vc_group(id)) {
	case KP_VIDEO_ID_GRAPHICS: return o->graphics;
	case KP_VIDEO_ID_FOG: return o->fog;
	case KP_VIDEO_ID_WATER: return o->water;
	case KP_VIDEO_ID_FONT: return o->font;
	default: return (int)a->ui->portrait.mode;
	}
}

/* The value a button sets (KP_VIDEO_CHOICE_ID's, the portraits' mode). */
static int vc_expected(int16_t id)
{
	return vc_group(id) == KP_VIDEO_ID_PORTRAITS ? id - KP_VIDEO_ID_PORTRAITS : id - vc_group(id) - 1;
}

static void vc_log(KpApp *a, const char *label)
{
	const KpUiVideoOptions *o = &a->ui->video_options;
	kp_dev_line("video check %s: graphics %d, fog %d, water %d, font %d, gamma %d, portraits %d; serial %u, commits %u; "
		"world art %s (scale %d, effects %d), interface %s, vector text %s", label, o->graphics, o->fog, o->water,
		o->font, (int)a->ui->gamma, (int)a->ui->portrait.mode, o->serial, o->commits,
		a->world.hd_scale ? a->world.art_variant : "original", (int)a->world.hd_scale, a->world.fx,
		a->hd_ui.variant[0] ? a->hd_ui.variant : "SD", a->ui->vector_text ? "on" : "off");
}

static void vc_fail(Dev *d, const char *what, int got, int want)
{
	d->vc_failures++;
	kp_dev_line("video check: %s %d, wanted %d", what, got, want);
}

/* The step's choice took (and the world's art follows it), or a disabled one changed nothing. */
static void vc_verify(KpApp *a, Dev *d, int16_t id)
{
	int want;
	if (id == 0) {
		if ((int)a->ui->gamma != KP_UI_GAMMA_MAX) vc_fail(d, "gamma", (int)a->ui->gamma, KP_UI_GAMMA_MAX);
		return;
	}
	want = d->vc_offered ? vc_expected(id) : d->vc_before;
	if (vc_value(a, id) != want) vc_fail(d, d->vc_offered ? "the choice" : "a disabled choice", vc_value(a, id), want);
	if (vc_group(id) == KP_VIDEO_ID_GRAPHICS && d->vc_offered && vc_expected(id) != KP_VIDEO_AUTO) {
		int scale = vc_expected(id) == KP_GRAPHICS_HD2 ? VC_HD2_SCALE : vc_expected(id) == KP_GRAPHICS_HD ? VC_HD_SCALE : 0;
		if (a->world.hd_scale != scale) vc_fail(d, "the world's art scale", (int)a->world.hd_scale, scale);
	}
}

static void vc_shot(KpApp *a, const char *label)
{
	char path[1024];
	snprintf(path, sizeof path, "%s/%s.png", a->flags.video_check, label);
	screenshot(a, path);
}

/* The last line (the failures), and the program's end. */
static void vc_end(KpApp *a, Dev *d)
{
	kp_dev_line("video check: %d steps, Cancel and OK, %d failures", VIDEO_STEP_COUNT, d->vc_failures);
	a->quit = 1;
}

static void video_check(KpApp *a, Dev *d)
{
	if (!a->world.active || !a->hud_on || a->hud.has_start) return;
	if (d->vc_stage != d->vc_stage_seen) {
		d->vc_stage_seen = d->vc_stage;
		d->vc_stage_frames = 0;
	} else if (++d->vc_stage_frames > VC_STAGE_FRAMES) { /* its dialog never came */
		vc_fail(d, "stalled at stage", d->vc_stage, VC_DONE);
		vc_end(a, d);
		return;
	}
	if (d->vc_wait > 0) {
		d->vc_wait--;
		return;
	}
	d->vc_wait = VC_KEY_FRAMES;
	switch (d->vc_stage) {
	case VC_START: /* the game settled: F10, Options, Video */
		if (!d->vc_started) {
			d->vc_started = 1;
			d->vc_wait = VC_START_FRAMES;
			break;
		}
		vc_log(a, "start");
		vc_shot(a, "start");
		d->vc_start = a->ui->video_options;
		d->vc_start_gamma = (int)a->ui->gamma;
		d->vc_start_portraits = (int)a->ui->portrait.mode;
		vc_key(a, 0x79, 0);
		d->vc_stage = VC_OPTIONS;
		break;
	case VC_OPTIONS:
		if (!vc_dialog(a, "GameMenu")) break;
		vc_key(a, 0x4f, 'o');
		d->vc_stage = VC_VIDEO;
		break;
	case VC_VIDEO:
		if (!vc_dialog(a, "Options")) break;
		vc_key(a, 0x56, 'v');
		d->vc_stage = VC_CLICK;
		break;
	case VC_CLICK: { /* a step's choice; after Cancel the last round's HD (2x) */
		int16_t id = d->vc_step < VIDEO_STEP_COUNT ? VIDEO_STEPS[d->vc_step].id : VC_KEEP_ID;
		int32_t c;
		int k;
		if (!vc_dialog(a, "Video")) break;
		c = vc_control(a, id);
		d->vc_offered = c >= 0 && !(a->ui->controls[c].flags & KP_UI_DISABLED);
		d->vc_before = id ? vc_value(a, id) : (int)a->ui->gamma;
		for (k = 0; k < (id ? 1 : VC_GAMMA_CLICKS); k++) {
			if (vc_click(a, id) != 0) vc_fail(d, "no control", id, id);
		}
		d->vc_stage = VC_SHOT;
		d->vc_wait = VC_SETTLE;
		break;
	}
	case VC_SHOT:
		if (d->vc_step < VIDEO_STEP_COUNT) {
			vc_verify(a, d, VIDEO_STEPS[d->vc_step].id);
			vc_log(a, VIDEO_STEPS[d->vc_step].label);
			vc_shot(a, VIDEO_STEPS[d->vc_step].label);
			d->vc_stage = ++d->vc_step < VIDEO_STEP_COUNT ? VC_CLICK : VC_CANCELLED;
			if (d->vc_stage == VC_CANCELLED) vc_key(a, 0x43, 'c'); /* Cancel: all back */
		} else {
			d->vc_kept = d->vc_offered ? vc_expected(VC_KEEP_ID) : d->vc_before;
			vc_key(a, 0x4f, 'o'); /* OK: kept */
			d->vc_stage = VC_DONE;
		}
		d->vc_wait = VC_SETTLE;
		break;
	case VC_CANCELLED: { /* back in the options menu: every option as at the start; then Video again */
		const KpUiVideoOptions *o = &a->ui->video_options, *s = &d->vc_start;
		if (!vc_dialog(a, "Options")) break;
		vc_log(a, "cancelled");
		vc_shot(a, "cancelled");
		if (o->graphics != s->graphics || o->fog != s->fog || o->water != s->water || o->font != s->font) {
			vc_fail(d, "Cancel left the art", o->graphics, s->graphics);
		}
		if ((int)a->ui->gamma != d->vc_start_gamma) vc_fail(d, "Cancel left the gamma", (int)a->ui->gamma, d->vc_start_gamma);
		if ((int)a->ui->portrait.mode != d->vc_start_portraits) {
			vc_fail(d, "Cancel left the portraits", (int)a->ui->portrait.mode, d->vc_start_portraits);
		}
		d->vc_stage = VC_VIDEO;
		break;
	}
	case VC_DONE:
		if (a->ui->modeless >= 0) break; /* OK: back to the game */
		vc_log(a, "kept");
		vc_shot(a, "kept");
		if (a->ui->video_options.graphics != d->vc_kept) vc_fail(d, "OK's art", a->ui->video_options.graphics, d->vc_kept);
		if (a->ui->video_options.commits != d->vc_start.commits + 1) {
			vc_fail(d, "OK's commits", (int)a->ui->video_options.commits, (int)d->vc_start.commits + 1);
		}
		vc_end(a, d);
		break;
	default: break;
	}
}

/* The HD interface's check (--hd-check DIR): each step shows a screen of the interface with the HD interface on and
 * counts the canvas's pixels the composite shows in SD although they are an image's with HD art (kp_hd_ui_coverage:
 * lost without an item, outside their HD image with nothing under it, waiting, unreadable) and the image log's drops;
 * the files with no HD art are listed. With --mission (and --play) the steps are that game's screens (a worker, the
 * build menu, a building's placement by the console, the main building, a group, every in-game menu and dialog);
 * without it the glue's, walked by clicks from the main menu (the game type, the profiles, both campaign menus, the
 * load screen, the Terran episode's text and briefing, its game, its victory by the cheat and the score screen). Each screen
 * is saved as DIR/<step>.png; the last line counts the screens with SD over HD art. The player's profile is never
 * written (kp_app_save_profile). */
#define HC_SETTLE 30         /* frames a screen settles before it is counted, and seconds at least: */
#define HC_SETTLE_S 0.5
#define HC_START_S 2.0       /* a game's first view */
#define HC_SCORE_S 5.0       /* the score screen's counters running up */
#define HC_KEY_FRAMES 10     /* frames after an Escape, between the cheat's keys */
#define HC_SKIP_S 1.0        /* seconds between the keys that skip an episode's text */
#define HC_STEP_S 30.0       /* seconds a step waits for its screen (the checks run without vsync) */
#define HC_ESCAPES 6         /* Escapes that close the game's dialogs before a step */
#define HC_GROUP_RADIUS 48   /* a group's box around one of its units (logical pixels) */
#define HC_CLICK_RADIUS 4    /* a unit's own box */
#define HC_KEY_ESCAPE 0x1b
#define HC_KEY_RETURN 0x0d
#define HC_KEY_BUILD 'B'     /* the worker's basic build menu (each race's) */
#define HC_VICTORY_CHEAT "there is no cow level" /* KP_CHEAT_NO_COW_LEVEL: the mission won */
/* the glue's states and buttons (kp_ui_menus.c, kp_ui_briefing.c) */
#define HC_STATE_MAIN 0
#define HC_STATE_LOGIN 5
#define HC_STATE_CAMPAIGN 6
#define HC_STATE_LOAD 13
#define HC_STATE_EXP_CAMPAIGN 22
#define HC_MAIN_SINGLE 3     /* the main menu's Single Player */
#define HC_LOGIN_OK 4
#define HC_LOGIN_CANCEL 5
#define HC_TYPE_ORIGINAL 6   /* the game type popup's buttons */
#define HC_TYPE_EXPANSION 7
#define HC_CAMPAIGN_LOAD 5   /* a campaign menu's Load Saved, its player change, its Terran episode */
#define HC_CAMPAIGN_LOGIN 9
#define HC_EPISODE_TERRAN 7
#define HC_LOAD_CANCEL 5
#define HC_POPUP_OK 1        /* the mission history's Ok */
#define HC_BRIEFING_START 13
enum { HC_RACES = 3 };
/* Per race (Zerg, Terran, Protoss, as the worker found tells): its worker, main building, a fighter and the key of a
 * building the basic build menu places (units.dat ids). */
static const struct {
	int worker, main, fighter;
	char place;
} HC_RACE[HC_RACES] = { { 41, 131, 37, 'H' }, { 7, 106, 0, 'S' }, { 64, 154, 65, 'P' } };
enum { HC_WORKER, HC_MAIN, HC_FIGHTER };
/* a step's action, and what it waits for */
enum { HC_NONE, HC_CLICK, HC_POPUP_CLICK, HC_SKIP, HC_START, HC_SELECT, HC_GROUP, HC_KEY, HC_PLACE, HC_MENU, HC_OPTIONS, HC_VICTORY,
	HC_ANSWER };
enum { HC_W_SETTLE, HC_W_GAME, HC_W_STATE, HC_W_POPUP, HC_W_EPISODE, HC_W_ROOM_ENTERED, HC_W_ROOM, HC_W_DIALOG,
	HC_W_SCORE };
enum { HC_PREPARE, HC_WAIT, HC_SETTLING };
typedef struct {
	const char *label; /* NULL: a step on the way, not counted */
	int action, arg;   /* a control's ID, a unit's role (HC_WORKER ...), a key, kp_ui_game_menu's or kp_ui_options_dialog's */
	int wait, wait_arg; /* HC_W_*: a glue state */
} HcStep;
static const HcStep HC_GAME_STEPS[] = {
	{ "start", HC_NONE, 0, HC_W_GAME, 0 },
	{ "worker", HC_SELECT, HC_WORKER, HC_W_SETTLE, 0 },
	{ "build-card", HC_KEY, HC_KEY_BUILD, HC_W_SETTLE, 0 },
	{ "placement", HC_PLACE, 0, HC_W_SETTLE, 0 },
	{ "main-building", HC_SELECT, HC_MAIN, HC_W_SETTLE, 0 },
	{ "group", HC_GROUP, HC_FIGHTER, HC_W_SETTLE, 0 },
	{ "game-menu", HC_MENU, KP_UI_GAME_MENU, HC_W_DIALOG, 0 },
	{ "options", HC_MENU, KP_UI_GAME_MENU_OPTIONS, HC_W_DIALOG, 0 },
	{ "speed", HC_OPTIONS, KP_UI_OPTIONS_SPEED, HC_W_DIALOG, 0 },
	{ "sound", HC_OPTIONS, KP_UI_OPTIONS_SOUND, HC_W_DIALOG, 0 },
	{ "video", HC_OPTIONS, KP_UI_OPTIONS_VIDEO, HC_W_DIALOG, 0 },
	{ "tips", HC_OPTIONS, KP_UI_OPTIONS_TIPS, HC_W_DIALOG, 0 },
	{ "help-menu", HC_MENU, KP_UI_GAME_MENU_HELP, HC_W_DIALOG, 0 },
	{ "save", HC_MENU, KP_UI_GAME_MENU_SAVE, HC_W_DIALOG, 0 },
	{ "load", HC_MENU, KP_UI_GAME_MENU_LOAD, HC_W_DIALOG, 0 },
	{ "quit-mission", HC_MENU, KP_UI_GAME_MENU_QUIT_MISSION, HC_W_DIALOG, 0 },
	{ "quit", HC_MENU, KP_UI_GAME_MENU_QUIT, HC_W_DIALOG, 0 },
};
static const HcStep HC_GLUE_STEPS[] = {
	{ "main-menu", HC_NONE, 0, HC_W_STATE, HC_STATE_MAIN },
	{ "game-type", HC_CLICK, HC_MAIN_SINGLE, HC_W_POPUP, 0 },
	{ "profiles", HC_CLICK, HC_TYPE_EXPANSION, HC_W_STATE, HC_STATE_LOGIN },
	{ "expansion-campaign", HC_CLICK, HC_LOGIN_OK, HC_W_STATE, HC_STATE_EXP_CAMPAIGN },
	{ "load-screen", HC_CLICK, HC_CAMPAIGN_LOAD, HC_W_STATE, HC_STATE_LOAD },
	{ NULL, HC_CLICK, HC_LOAD_CANCEL, HC_W_STATE, HC_STATE_EXP_CAMPAIGN },
	{ NULL, HC_CLICK, HC_CAMPAIGN_LOGIN, HC_W_STATE, HC_STATE_LOGIN },
	{ NULL, HC_CLICK, HC_LOGIN_CANCEL, HC_W_STATE, HC_STATE_MAIN },
	{ NULL, HC_CLICK, HC_MAIN_SINGLE, HC_W_POPUP, 0 },
	{ NULL, HC_CLICK, HC_TYPE_ORIGINAL, HC_W_STATE, HC_STATE_LOGIN },
	{ "campaign", HC_CLICK, HC_LOGIN_OK, HC_W_STATE, HC_STATE_CAMPAIGN },
	{ "episode", HC_CLICK, HC_EPISODE_TERRAN, HC_W_EPISODE, 0 },
	{ "episode-text", HC_POPUP_CLICK, HC_POPUP_OK, HC_W_ROOM_ENTERED, 0 },
	{ "briefing", HC_SKIP, 0, HC_W_ROOM, 0 },
	{ "game", HC_START, 0, HC_W_GAME, 0 },
	{ "victory", HC_VICTORY, 0, HC_W_DIALOG, 0 },
	{ "score", HC_ANSWER, 0, HC_W_SCORE, 0 },
};

static const HcStep *hc_steps(const KpApp *a, int *count)
{
	if (a->flags.mission[0]) {
		*count = (int)(sizeof HC_GAME_STEPS / sizeof HC_GAME_STEPS[0]);
		return HC_GAME_STEPS;
	}
	*count = (int)(sizeof HC_GLUE_STEPS / sizeof HC_GLUE_STEPS[0]);
	return HC_GLUE_STEPS;
}

/* The local player's race (HC_RACE's index) by its first worker; -1 none. */
static int hc_race(const KpGame *g)
{
	int64_t n;
	int r;
	for (n = g->lists[KP_LIST_ACTIVE].head; n > 0; n = g->u.next[n]) {
		if (g->u.owner[n] != g->local_player) continue;
		for (r = 0; r < HC_RACES; r++) {
			if (g->u.type[n] == HC_RACE[r].worker) return r;
		}
	}
	return -1;
}

/* A box over the local player's unit of the role (radius around it, the camera on it); 0 when it has none. */
static int hc_select(KpApp *a, int role, int32_t radius)
{
	const KpGame *g = a->session ? a->session->g : NULL;
	int64_t n;
	int32_t x, y;
	int race, type;
	if (!g || (race = hc_race(g)) < 0) return 0;
	type = role == HC_WORKER ? HC_RACE[race].worker : role == HC_MAIN ? HC_RACE[race].main : HC_RACE[race].fighter;
	for (n = g->lists[KP_LIST_ACTIVE].head; n > 0 && (g->u.type[n] != type || g->u.owner[n] != g->local_player);
		n = g->u.next[n]) {}
	if (n <= 0) return 0;
	a->cam_x = g->u.x[n] - a->world_w / 2;
	a->cam_y = g->u.y[n] - a->world_h / 2;
	kp_flow_clamp_camera(a);
	x = (int32_t)(g->u.x[n] - (int64_t)a->cam_x);
	y = (int32_t)(g->u.y[n] - (int64_t)a->cam_y);
	kp_input_mouse(a, KP_UI_LEFT_DOWN, x - radius, y - radius);
	kp_input_mouse(a, KP_UI_LEFT_UP, x + radius, y + radius);
	return 1;
}

static void hc_key(KpApp *a, uint16_t vk, uint16_t ch)
{
	kp_ui_window_key(a->ui, 1, vk, 0);
	if (ch) kp_ui_window_char(a->ui, ch, 0);
	kp_ui_window_key(a->ui, 0, vk, 0);
}

/* A key of the game's (its message line), as kp_hud takes it. */
static void hc_game_key(KpApp *a, uint16_t vk)
{
	kp_hud_key(a->ui, 1, vk, 0);
	kp_hud_key(a->ui, 0, vk, 0);
}

/* The open dialogs (the glue's screen and the popups over it). */
static int hc_open_count(const KpUi *ui)
{
	int32_t d;
	int n = 0;
	for (d = ui->open; d >= 0; d = ui->controls[d].next) n++;
	return n;
}

/* A click on control `id` of the topmost open dialog that has it, a popup composited into the screen at its place
 * within it (the screen, the open list's last, placed on canvas c); -1 when none has it. */
static int hc_click(KpApp *a, const KpCanvas *c, int16_t id)
{
	KpUi *ui = a->ui;
	int32_t d, screen = -1, k = -1, x, y;
	for (d = ui->open; d >= 0; d = ui->controls[d].next) screen = d;
	for (d = ui->open; d >= 0 && (k = kp_ui_find_control(ui, d, id)) < 0; d = ui->controls[d].next) {}
	if (k < 0 || screen < 0 || !kp_canvas_place(c, screen, &x, &y)) return -1;
	x += ui->controls[d].left - ui->controls[screen].left + (ui->controls[k].left + ui->controls[k].right) / 2;
	y += ui->controls[d].top - ui->controls[screen].top + (ui->controls[k].top + ui->controls[k].bottom) / 2;
	kp_input_mouse(a, KP_UI_MOUSE_MOVE, x, y);
	kp_input_mouse(a, KP_UI_LEFT_DOWN, x, y);
	kp_input_mouse(a, KP_UI_LEFT_UP, x, y);
	return 0;
}

/* Whether the glue's panels stand still: no panel on its way in or out (its distance left), the controls a slide-in
 * hid shown again (closing stays set past a screen without slides). */
static int hc_glue_still(const KpUi *ui)
{
	const KpUiGlue *g = &ui->glue;
	int k;
	for (k = 0; k < g->desc_count; k++) {
		if (g->slide_active[k] && g->slide_control[k] >= 0 && ui->controls[g->slide_control[k]].wuser != 0) return 0;
	}
	return g->hidden_count == 0 && g->slide_delay == 0;
}

/* Whether the step's screen is up (a glue screen's panels in place). */
static int hc_ready(const KpApp *a, const HcStep *s)
{
	int glue = kp_flow_glue_active(a) && hc_glue_still(a->ui);
	switch (s->wait) {
	case HC_W_GAME: return !glue && a->world.active && a->hud_on;
	case HC_W_STATE:
		return glue && a->glue.screen == KP_GLUE_MENUS && a->ui->menus.state == s->wait_arg && a->ui->menus.screen >= 0
			&& hc_open_count(a->ui) == 1;
	case HC_W_POPUP: return glue && hc_open_count(a->ui) > 1;
	case HC_W_EPISODE: return glue && (hc_open_count(a->ui) > 1 || a->glue.screen == KP_GLUE_ROOM);
	case HC_W_ROOM_ENTERED: return glue && a->glue.screen == KP_GLUE_ROOM; /* an episode's text first, or the room */
	case HC_W_ROOM: return glue && a->glue.screen == KP_GLUE_ROOM && a->glue.room_opened;
	case HC_W_DIALOG: return a->ui->modeless >= 0;
	case HC_W_SCORE: return glue && a->glue.screen == KP_GLUE_SCORE;
	default: return 1;
	}
}

/* Whether the step acts in a game (whose dialogs are closed first). */
static int hc_in_game(int action) { return action >= HC_SELECT && action <= HC_OPTIONS; }

static void hd_check(KpApp *a, Dev *d)
{
	KpHdUiCoverage cov;
	const HcStep *steps, *s;
	const KpCanvas *c;
	int64_t sd;
	int count, measured = 0, k;
	char path[1100];
	if (!a->ui) return;
	steps = hc_steps(a, &count);
	for (k = 0; k < count; k++) measured += steps[k].label != NULL;
	if (d->hc_wait > 0 || kp_app_now() < d->hc_until) {
		if (d->hc_wait > 0) d->hc_wait--;
		return;
	}
	if (d->hc_step >= count) {
		kp_dev_line("hd check: %d screens, %d with SD over HD art", measured, d->hc_failures);
		a->quit = 1;
		return;
	}
	s = &steps[d->hc_step];
	c = kp_flow_glue_active(a) ? &a->glue.canvas : a->hud.canvas;
	switch (d->hc_stage) {
	case HC_PREPARE: /* in the game: its dialogs closed, the placement ended; then the step's action */
		if (hc_in_game(s->action) && a->ui->modeless >= 0) {
			if (d->hc_escapes++ >= HC_ESCAPES) {
				kp_dev_line("hd check %s: a dialog stays open", s->label);
				d->hc_failures++;
				d->hc_step = count;
				return;
			}
			hc_key(a, HC_KEY_ESCAPE, HC_KEY_ESCAPE);
			d->hc_wait = HC_KEY_FRAMES;
			return;
		}
		d->hc_escapes = 0;
		if (hc_in_game(s->action) && s->action != HC_KEY && s->action != HC_PLACE) kp_ui_end_placement(a->ui);
		d->hc_dropped = a->ui->image_log.dropped;
		d->hc_frames = 0;
		d->hc_since = d->hc_skipped = kp_app_now();
		switch (s->action) {
		case HC_CLICK:
			if (hc_click(a, c, (int16_t)s->arg) != 0) kp_dev_line("hd check: no control %d to click", s->arg);
			break;
		case HC_POPUP_CLICK: /* the mission history when it shows (a profile with progress) */
			if (hc_open_count(a->ui) > 1) (void)hc_click(a, c, (int16_t)s->arg);
			break;
		case HC_SKIP: /* an episode's text (the room not open yet) goes on with a click */
			if (!a->glue.room_opened) {
				kp_input_mouse(a, KP_UI_LEFT_DOWN, a->world_w / 2, a->world_h / 2);
				kp_input_mouse(a, KP_UI_LEFT_UP, a->world_w / 2, a->world_h / 2);
			}
			break;
		case HC_START: (void)hc_click(a, c, HC_BRIEFING_START); break;
		case HC_SELECT:
		case HC_GROUP:
			if (!hc_select(a, s->arg, s->action == HC_GROUP ? HC_GROUP_RADIUS : HC_CLICK_RADIUS)) {
				kp_dev_line("hd check %s: the player has no such unit", s->label);
			}
			break;
		case HC_KEY: hc_key(a, (uint16_t)s->arg, (uint16_t)(s->arg | 0x20)); break;
		case HC_PLACE: { /* the race's building key, the mouse over the map above the console's middle */
			int race = a->session && a->session->g ? hc_race(a->session->g) : -1;
			if (race >= 0) hc_key(a, (uint16_t)HC_RACE[race].place, (uint16_t)(HC_RACE[race].place | 0x20));
			kp_input_mouse(a, KP_UI_MOUSE_MOVE, a->world_w * 3 / 5, a->world_h * 7 / 10);
			break;
		}
		case HC_MENU: kp_ui_game_menu(a->ui, s->arg); break;
		case HC_OPTIONS: kp_ui_options_dialog(a->ui, s->arg); break;
		case HC_VICTORY: /* the message line opened (the game's keys); its text and Return in the frames after */
			hc_game_key(a, HC_KEY_RETURN);
			d->hc_typing = 1;
			break;
		case HC_ANSWER: hc_key(a, HC_KEY_RETURN, HC_KEY_RETURN); break; /* the result dialog's OK */
		default: break;
		}
		d->hc_stage = HC_WAIT;
		break;
	case HC_WAIT: /* its screen */
		if (d->hc_typing && d->hc_frames++ % HC_KEY_FRAMES == HC_KEY_FRAMES - 1) { /* the cheat's line, then Return */
			if (d->hc_typing++ == 1) kp_hud_text(a->ui, HC_VICTORY_CHEAT);
			else {
				hc_game_key(a, HC_KEY_RETURN);
				d->hc_typing = 0;
			}
			return;
		}
		if (s->action == HC_SKIP && !hc_ready(a, s) && kp_app_now() - d->hc_skipped >= HC_SKIP_S) {
			hc_key(a, HC_KEY_ESCAPE, HC_KEY_ESCAPE); /* the text typed out at the first, gone at the next */
			d->hc_skipped = kp_app_now();
		}
		if (!hc_ready(a, s) && kp_app_now() - d->hc_since < HC_STEP_S) return;
		if (!hc_ready(a, s)) {
			kp_dev_line("hd check %s: its screen never came", s->label ? s->label : "(a step on the way)");
			snprintf(path, sizeof path, "%s/stalled-%d.png", a->flags.hd_check, d->hc_step);
			screenshot(a, path);
			d->hc_failures++;
			d->hc_step = count; /* the steps after it need it */
			return;
		}
		d->hc_stage = HC_SETTLING;
		d->hc_wait = HC_SETTLE;
		d->hc_until = kp_app_now() + (s->wait == HC_W_GAME ? HC_START_S : s->wait == HC_W_SCORE ? HC_SCORE_S : HC_SETTLE_S);
		break;
	case HC_SETTLING:
		d->hc_stage = HC_PREPARE;
		d->hc_step++;
		if (!s->label) break;
		if (!a->hd_ui.head) {
			kp_dev_line("hd check: no HD interface (--data with StarCraft: Remastered's HD art, a scale of 2 or more)");
			d->hc_failures++;
			d->hc_step = count;
			return;
		}
		kp_hd_ui_coverage(a, c, &cov);
		sd = cov.lost + cov.outside + cov.waiting + cov.failed;
		kp_dev_line("hd check %s: %lld HD pixels; SD over HD art: %lld lost, %lld outside, %lld waiting, %lld unreadable; "
			"%u log drops; no HD art: %lld pixels%s%s%s%s", s->label, (long long)cov.hd, (long long)cov.lost,
			(long long)cov.outside, (long long)cov.waiting, (long long)cov.failed, a->ui->image_log.dropped - d->hc_dropped,
			(long long)cov.no_art, cov.no_art_paths[0] ? " " : "", cov.no_art_paths, cov.outside_paths[0] ? "; outside: " : "",
			cov.outside_paths);
		if (sd > 0 || a->ui->image_log.dropped != d->hc_dropped) d->hc_failures++;
		snprintf(path, sizeof path, "%s/%s.png", a->flags.hd_check, s->label);
		screenshot(a, path);
		break;
	default: break;
	}
}

/* The save check (--save-check DIR with --mission): the tips closed, the game runs SC_RUN_TICKS steps and on until a
 * trigger portrait keeps the local player's triggers waiting, and pauses; its view goes to the map's bottom-right and
 * the game is saved; the view goes to the map's top-left and the save loads in the game, then from its file (the glue's
 * Load Saved). The frame of each load shows the saved view, the minimap's view at its tile and the saved step
 * (DIR/<load>.png), and the loaded game's trigger wait ends once the portrait's time left has passed (SC_RELEASE_S more
 * at most); the save file goes at the end. */
#define SC_NAME "koprulu save check"
#define SC_RUN_TICKS 48
#define SC_FRAMES 60000  /* the frames the check may take */
#define SC_RELEASE_S 3.0 /* the seconds past the portrait's time left the wait may take to end */
enum { SC_START, SC_SAVE, SC_AWAY, SC_LOAD, SC_LOADED, SC_FILE, SC_FILE_LOADED, SC_RELEASE, SC_DONE };

static int sc_waiting(const KpSession *s) { return s->g->trig.wait_active[s->g->local_player] != 0; }

/* The loaded game's trigger wait, then the stage `after`. */
static void sc_release(Dev *d, int after)
{
	d->sc_after = after;
	d->sc_deadline = kp_app_now() + d->sc_portrait_ms / 1000.0 + SC_RELEASE_S;
	d->sc_stage = SC_RELEASE;
}

static void sc_verify(KpApp *a, Dev *d, const char *label)
{
	const KpUiMinimap *m = &a->ui->minimap;
	int64_t tick = a->session ? a->session->tick : -1;
	char path[600];
	int ok = a->cam_x == d->sc_x && a->cam_y == d->sc_y && m->view_x == d->sc_x / KP_WORLD_TILE
		&& m->view_y == d->sc_y / KP_WORLD_TILE && tick >= d->sc_tick && tick <= d->sc_tick + 2;
	kp_dev_line("save check %s: view %lld,%lld (saved %lld,%lld), minimap view tile %u,%u, step %lld (saved %lld)%s", label,
		(long long)a->cam_x, (long long)a->cam_y, (long long)d->sc_x, (long long)d->sc_y, (unsigned)m->view_x,
		(unsigned)m->view_y, (long long)tick, (long long)d->sc_tick, ok ? "" : " FAILED");
	if (!ok) d->sc_failures++;
	snprintf(path, sizeof path, "%s/%s.png", a->flags.save_check, label);
	screenshot(a, path);
}

static void save_check(KpApp *a, Dev *d)
{
	KpSession *s = a->session;
	char why[256];
	if (++d->sc_frames > SC_FRAMES) {
		kp_dev_line("save check: stalled at stage %d", d->sc_stage);
		d->sc_failures++;
		d->sc_stage = SC_DONE;
	}
	if (d->sc_stage != SC_DONE && (!s || !s->g || !a->world.active || !a->hud_on || --d->sc_wait > 0)) return;
	switch (d->sc_stage) {
	case SC_START: { /* the game's first view placed, its steps run, a portrait's wait: the view at the bottom-right */
		int camera;
		if (s->clock.paused) kp_session_resume(s); /* the window's autopause: the check runs in the background */
		if (a->ui->modeless >= 0) { /* the tips at the game's start, which pause it: Return closes them */
			vc_key(a, 0x0d, 0x0d);
			d->sc_wait = SETTLE_FRAMES;
			return;
		}
		if (a->hud.has_start || s->tick < SC_RUN_TICKS || !sc_waiting(s)
			|| !kp_ui_pending_releases(a->ui, &d->sc_portrait_ms, &camera)) {
			return;
		}
		kp_session_pause(s);
		a->cam_x = a->cam_y = INT32_MAX;
		kp_flow_clamp_camera(a);
		d->sc_stage = SC_SAVE;
		d->sc_wait = SETTLE_FRAMES;
		break;
	}
	case SC_SAVE:
		d->sc_x = a->cam_x;
		d->sc_y = a->cam_y;
		d->sc_tick = s->tick;
		snprintf(a->pending_save, sizeof a->pending_save, "%s", SC_NAME);
		d->sc_stage = SC_AWAY;
		d->sc_wait = SETTLE_FRAMES;
		break;
	case SC_AWAY: /* the view elsewhere (the minimap's view timer takes it), then the load */
		a->cam_x = a->cam_y = 0;
		kp_flow_clamp_camera(a);
		d->sc_stage = SC_LOAD;
		d->sc_wait = 30;
		break;
	case SC_LOAD:
		snprintf(a->pending_load, sizeof a->pending_load, "%s", SC_NAME);
		d->sc_stage = SC_LOADED;
		break;
	case SC_LOADED: /* the load's frame */
		sc_verify(a, d, "in-game");
		sc_release(d, SC_FILE);
		break;
	case SC_FILE:
		if (kp_flow_load_saved(a, SC_NAME, why, sizeof why) != 0) {
			kp_dev_line("save check: the save's game did not start: %s", why);
			d->sc_failures++;
			d->sc_stage = SC_DONE;
			break;
		}
		d->sc_stage = SC_FILE_LOADED;
		break;
	case SC_FILE_LOADED:
		sc_verify(a, d, "from-file");
		sc_release(d, SC_DONE);
		break;
	case SC_RELEASE:
		if (sc_waiting(s) && kp_app_now() < d->sc_deadline) return;
		kp_dev_line("save check: the loaded game's trigger wait %s (the portrait's %u ms left at the save)",
			sc_waiting(s) ? "never ended FAILED" : "ended", (unsigned)d->sc_portrait_ms);
		if (sc_waiting(s)) d->sc_failures++;
		d->sc_stage = d->sc_after;
		break;
	default: {
		char file[200], path[1100];
		kp_save_file_name(SC_NAME, file, sizeof file);
		kp_app_pref_path(a, file, path, sizeof path);
		remove(path);
		kp_profile_remove_save(&a->profile, SC_NAME);
		kp_dev_line("save check: 2 loads, %d failures", d->sc_failures);
		a->quit = 1;
		break;
	}
	}
}

/* --- the touch check (phase 10, ADR 0014): --mission's game played by synthetic fingers through the touch layer --- */

enum { TC_START, TC_TAP, TC_TAPPED, TC_ORDER, TC_ORDERED, TC_BOX, TC_BOXED, TC_OPEN, TC_ASSIGN, TC_ASSIGN_LIFT,
	TC_ASSIGNED, TC_PAN, TC_PAN_MOVE, TC_PANNED, TC_DONE };
#define TC_SETTLE_S 0.3   /* a gesture's result shows within this */
#define TC_STALL_S 60.0   /* the game's first view comes within this */
#define TC_PAN_PX 120     /* the two fingers' way */

static void tc_shot(KpApp *a, const char *label)
{
	char path[600];
	snprintf(path, sizeof path, "%s/%s.png", a->flags.touch_check, label);
	screenshot(a, path);
}

static void tc_result(KpApp *a, Dev *d, const char *step, int ok, const char *fmt, ...)
{
	char detail[256];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(detail, sizeof detail, fmt, ap);
	va_end(ap);
	kp_dev_line("touch check %s: %s%s", step, detail, ok ? "" : " FAILED");
	if (!ok) d->tc_failures++;
	d->tc_steps++;
	tc_shot(a, step);
}

/* The local player's unit of the most on the screen of its kind nearest the view's centre (a Marine, an SCV), 0 none. */
static int64_t tc_own_unit(const KpApp *a)
{
	const KpGame *g = a->session->g;
	int32_t vw, vh;
	int64_t n, best = 0, best_d = INT64_MAX;
	kp_hud_view_size(a->world_w, a->world_h, &vw, &vh);
	for (n = g->lists[KP_LIST_ACTIVE].head; n > 0; n = g->u.next[n]) {
		int64_t x = g->u.x[n] - a->cam_x, y = g->u.y[n] - a->cam_y, dd;
		if (g->u.owner[n] != g->local_player || x < 16 || y < 16 || x >= vw - 16 || y >= vh - 16) continue;
		if (kp_ui_pick_unit(a->ui, (int32_t)g->u.x[n], (int32_t)g->u.y[n]) != n) continue; /* a tap there takes it */
		dd = (x - vw / 2) * (x - vw / 2) + (y - vh / 2) * (y - vh / 2);
		if (dd < best_d) {
			best_d = dd;
			best = n;
		}
	}
	return best;
}

static int tc_selected(const KpGame *g, int64_t unit)
{
	int k, found = 0;
	for (k = 0; k < 12; k++) found |= g->local_selection[k] == unit;
	return found;
}

static void tc_tap(KpApp *a, int64_t id, double x, double y)
{
	kp_input_finger(a, KP_TOUCH_DOWN, id, x, y);
	kp_input_finger(a, KP_TOUCH_UP, id, x, y);
}

static void touch_check(KpApp *a, Dev *d)
{
	KpSession *s = a->session;
	double now = kp_app_now();
	if (d->tc_stage != TC_DONE && now - a->started > TC_STALL_S && d->tc_stage == TC_START) {
		kp_dev_line("touch check: the game never came FAILED");
		d->tc_failures++;
		d->tc_stage = TC_DONE;
	}
	if (d->tc_stage != TC_DONE && (!s || !s->g || !a->world.active || !a->hud_on || now < d->tc_at)) return;
	if (s && s->clock.paused) kp_session_resume(s); /* the window's autopause: the check runs in the background */
	switch (d->tc_stage) {
	case TC_START:
		if (a->ui->modeless >= 0) { /* the tips at the game's start: Return closes them */
			vc_key(a, 0x0d, 0x0d);
			d->tc_at = now + TC_SETTLE_S;
			return;
		}
		if (a->hud.has_start || s->tick < 24) return;
		kp_input_finger(a, KP_TOUCH_CANCEL, 0, 0, 0); /* the touch mode, its pad laid out next frame */
		d->tc_stage = TC_TAP;
		d->tc_at = now + TC_SETTLE_S;
		break;
	case TC_TAP: /* a tap on one of the player's own units selects it */
		d->tc_unit = tc_own_unit(a);
		if (d->tc_unit == 0) {
			tc_result(a, d, "tap", 0, "no unit of the player's on the screen");
			d->tc_stage = TC_DONE;
			break;
		}
		d->tc_ux = s->g->u.x[d->tc_unit] - a->cam_x;
		d->tc_uy = s->g->u.y[d->tc_unit] - a->cam_y;
		tc_tap(a, 1, (double)d->tc_ux, (double)d->tc_uy);
		d->tc_stage = TC_TAPPED;
		d->tc_at = now + TC_SETTLE_S;
		break;
	case TC_TAPPED:
		tc_result(a, d, "tap", s->g->local_selection[0] == d->tc_unit && !a->cursor.shown,
			"unit %lld at %lld,%lld selected %s, cursor %s", (long long)d->tc_unit, (long long)d->tc_ux,
			(long long)d->tc_uy, s->g->local_selection[0] == d->tc_unit ? "yes" : "no", a->cursor.shown ? "shown" : "hidden");
		d->tc_stage = TC_ORDER;
		break;
	case TC_ORDER: { /* a tap on the ground beside it orders it there (the right click) */
		int32_t vw, vh;
		int64_t gx = d->tc_ux + 96, gy = d->tc_uy;
		kp_hud_view_size(a->world_w, a->world_h, &vw, &vh);
		if (gx >= vw - 8) gx = d->tc_ux - 96;
		tc_tap(a, 2, (double)gx, (double)gy);
		d->tc_stage = TC_ORDERED;
		d->tc_at = now + 1.5;
		break;
	}
	case TC_ORDERED: {
		int64_t moved = (s->g->u.x[d->tc_unit] - a->cam_x - d->tc_ux) * (s->g->u.x[d->tc_unit] - a->cam_x - d->tc_ux)
			+ (s->g->u.y[d->tc_unit] - a->cam_y - d->tc_uy) * (s->g->u.y[d->tc_unit] - a->cam_y - d->tc_uy);
		tc_result(a, d, "order", moved >= 16 * 16, "the unit went %.0f pixels", sqrt((double)moved));
		d->tc_stage = TC_BOX;
		break;
	}
	case TC_BOX: { /* a drag around it boxes it in */
		double x = (double)(s->g->u.x[d->tc_unit] - a->cam_x), y = (double)(s->g->u.y[d->tc_unit] - a->cam_y);
		kp_input_finger(a, KP_TOUCH_DOWN, 3, x - 40, y - 40);
		kp_input_finger(a, KP_TOUCH_MOVE, 3, x, y);
		kp_input_finger(a, KP_TOUCH_MOVE, 3, x + 40, y + 40);
		kp_input_finger(a, KP_TOUCH_UP, 3, x + 40, y + 40);
		d->tc_stage = TC_BOXED;
		d->tc_at = now + TC_SETTLE_S;
		break;
	}
	case TC_BOXED:
		tc_result(a, d, "box", tc_selected(s->g, d->tc_unit), "the unit %sin the box's selection",
			tc_selected(s->g, d->tc_unit) ? "" : "not ");
		d->tc_stage = TC_OPEN;
		break;
	case TC_OPEN: /* the pad's groups opened by its toggle (folded, an empty group is not shown) */
		if (!kp_hud_pad_point(&a->hud, KP_UI_PAD_TOGGLE, &d->tc_px, &d->tc_py)) {
			tc_result(a, d, "pad", 0, "no touch pad");
			d->tc_stage = TC_PAN;
			break;
		}
		kp_input_finger(a, KP_TOUCH_DOWN, 4, d->tc_px, d->tc_py);
		kp_input_finger(a, KP_TOUCH_UP, 4, d->tc_px, d->tc_py);
		d->tc_stage = TC_ASSIGN;
		d->tc_at = now + TC_SETTLE_S;
		break;
	case TC_ASSIGN: /* group 1's button held: the selection assigned */
		if (!kp_hud_pad_point(&a->hud, 0, &d->tc_px, &d->tc_py)) {
			tc_result(a, d, "pad", 0, "the toggle showed no group 1 (%d buttons)", a->hud.pad.count);
			d->tc_stage = TC_PAN;
			break;
		}
		kp_input_finger(a, KP_TOUCH_DOWN, 4, d->tc_px, d->tc_py);
		d->tc_stage = TC_ASSIGN_LIFT;
		d->tc_at = now + KP_TOUCH_LONG_MS / 1000.0 + 0.2;
		break;
	case TC_ASSIGN_LIFT:
		kp_input_finger(a, KP_TOUCH_UP, 4, d->tc_px, d->tc_py);
		d->tc_stage = TC_ASSIGNED;
		d->tc_at = now + TC_SETTLE_S;
		break;
	case TC_ASSIGNED: { /* folded again, the assigned group still shown */
		uint32_t held = kp_ui_groups_held(a->ui);
		int32_t gx, gy;
		int shown = kp_hud_pad_point(&a->hud, 0, &gx, &gy);
		tc_result(a, d, "pad", (held & 2) != 0 && !a->hud.pad.open && shown,
			"group 1 %s, the pad %s with %d buttons (%dx%d at %d,%d), group 1 %s", held & 2 ? "assigned" : "empty",
			a->hud.pad.open ? "open" : "folded", a->hud.pad.count, (int)a->hud.pad.bw, (int)a->hud.pad.bh,
			(int)a->hud.pad.x, (int)a->hud.pad.y, shown ? "shown" : "not shown");
		d->tc_stage = TC_PAN;
		break;
	}
	case TC_PAN: { /* two fingers drag the view */
		int32_t vw, vh;
		kp_hud_view_size(a->world_w, a->world_h, &vw, &vh);
		a->cam_x += TC_PAN_PX; /* room to move left */
		kp_flow_clamp_camera(a);
		d->tc_cam_x = a->cam_x;
		d->tc_cam_y = a->cam_y;
		kp_input_finger(a, KP_TOUCH_DOWN, 5, vw / 2.0 - 100, vh / 2.0);
		kp_input_finger(a, KP_TOUCH_DOWN, 6, vw / 2.0 + 100, vh / 2.0);
		d->tc_stage = TC_PAN_MOVE;
		break;
	}
	case TC_PAN_MOVE: {
		int32_t vw, vh;
		kp_hud_view_size(a->world_w, a->world_h, &vw, &vh);
		kp_input_finger(a, KP_TOUCH_MOVE, 5, vw / 2.0 - 100 + TC_PAN_PX, vh / 2.0);
		kp_input_finger(a, KP_TOUCH_MOVE, 6, vw / 2.0 + 100 + TC_PAN_PX, vh / 2.0);
		kp_input_finger(a, KP_TOUCH_UP, 5, vw / 2.0 - 100 + TC_PAN_PX, vh / 2.0);
		kp_input_finger(a, KP_TOUCH_UP, 6, vw / 2.0 + 100 + TC_PAN_PX, vh / 2.0);
		d->tc_stage = TC_PANNED;
		d->tc_at = now + TC_SETTLE_S;
		break;
	}
	case TC_PANNED:
		tc_result(a, d, "pan", a->cam_x == d->tc_cam_x - TC_PAN_PX && a->cam_y == d->tc_cam_y,
			"the view from %lld,%lld to %lld,%lld", (long long)d->tc_cam_x, (long long)d->tc_cam_y, (long long)a->cam_x,
			(long long)a->cam_y);
		d->tc_stage = TC_DONE;
		break;
	default:
		kp_dev_line("touch check: %d steps, %d failures", d->tc_steps, d->tc_failures);
		a->quit = 1;
		break;
	}
}

void kp_dev_frame(KpApp *a)
{
	Dev *d = (Dev *)a->dev;
	double now = kp_app_now();
	if (!d) return;
	if (a->flags.autotest) {
		autotest_frame(a, d);
		if (d->step == AT_DONE) {
			kp_dev_line("autotest: %d missions, %lld megatiles checked, %lld mismatches; composed views: %lld; load failures: %lld",
				d->missions_done, (long long)d->checked, (long long)d->bad, (long long)d->composed, (long long)d->failures);
			a->quit = 1;
		}
	}
	if (a->flags.play_check >= 1 && a->flags.play_check <= KP_APP_PLAY_CHECKS) {
		play_check(a, d);
		if (d->pc_stage == PC_DONE) a->quit = 1;
		return;
	}
	if (a->flags.video_check[0]) {
		video_check(a, d);
		return;
	}
	if (a->flags.hd_check[0]) {
		hd_check(a, d);
		return;
	}
	if (a->flags.save_check[0]) {
		save_check(a, d);
		return;
	}
	if (a->flags.touch_check[0]) {
		touch_check(a, d);
		return;
	}
	if (a->flags.camera && a->world.active) {
		a->cam_x = a->flags.camera_x;
		a->cam_y = a->flags.camera_y;
		kp_flow_clamp_camera(a);
	}
	if (a->flags.screenshot[0] && !d->shot_done && now - a->started >= a->flags.screenshot_after) {
		d->shot_done = 1;
		screenshot(a, a->flags.screenshot);
		if (!a->flags.autotest && !a->flags.play_check && !a->flags.bench_frames) a->quit = 1;
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
		free(d->pc_plain);
		free(d->pc_covered);
	}
	free(a->dev);
	a->dev = NULL;
}
