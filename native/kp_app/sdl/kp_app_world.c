/*
 * The playing game's world (plan phase S, stage S5): the mission's tileset and terrain, the lookup rows, the resource
 * packs with their atlas pages (decoded by SDL 3.4's PNG loader, which keeps 8-bit indices exactly: spike 16), the
 * frame table, the image data, the health bars, the fog and the screen palette, uploaded to kp_gfx; per frame the
 * presentation of the game's state.
 */
#include "kp_app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* An 8-bit PNG's indices (w x h, rows top first), or NULL. */
static uint8_t *png_indices(const uint8_t *png, uint32_t size, int32_t *w, int32_t *h)
{
	SDL_Surface *s = SDL_LoadPNG_IO(SDL_IOFromConstMem(png, size), true);
	uint8_t *out = NULL;
	int32_t r;
	if (!s) return NULL;
	if (s->format == SDL_PIXELFORMAT_INDEX8 && (out = (uint8_t *)malloc((size_t)s->w * (size_t)s->h)) != NULL) {
		for (r = 0; r < s->h; r++) memcpy(out + (size_t)r * (size_t)s->w, (const uint8_t *)s->pixels + (size_t)r * (size_t)s->pitch, (size_t)s->w);
		*w = s->w;
		*h = s->h;
	}
	SDL_DestroySurface(s);
	return out;
}

static void free_pack(KpAppPack *p)
{
	int k;
	if (p->pages) {
		for (k = 0; k < p->pack.page_count; k++) free(p->pages[k]);
		free(p->pages);
	}
	kp_pack_free(&p->pack);
	free(p->bytes);
	memset(p, 0, sizeof *p);
}

void kp_world_free(KpApp *a)
{
	KpAppWorld *w = &a->world;
	int k;
	if (!w->active) return;
	kp_audio_stop_all(&a->audio);
	kp_audio_mission(a);
	if (a->gfx) {
		kp_gfx_world(a->gfx, 0);
		kp_gfx_terrain(a->gfx, NULL, 0, 0, 0, NULL, 0, 0);
		kp_gfx_fog(a->gfx, NULL, NULL);
	}
	kp_presenter_free(&w->presenter);
	kp_image_data_free(&w->data);
	kp_frame_table_free(&w->frames);
	free(w->afrm);
	for (k = 0; k < w->pack_count; k++) free_pack(&w->packs[k]);
	kp_lookups_free(&w->lookups);
	kp_pack_free(&w->mission_sounds);
	free(w->mission_sounds_bytes);
	free(w->ids);
	free(w->sheet);
	memset(w, 0, sizeof *w);
}

static int fail(char *err, size_t err_size, const char *fmt, const char *what)
{
	snprintf(err, err_size, fmt, what);
	return -1;
}

static int build(KpApp *a, char *err, size_t err_size)
{
	KpAppWorld *w = &a->world;
	const KpAppMission *entry = &a->missions[a->current];
	KpGame *g = a->session->g;
	const KpGrpMeta *grp = kp_session_grp(a->session);
	const uint8_t *tileset;
	size_t tileset_size;
	const KpPack *packs[KP_APP_PACKS];
	const char *names[KP_APP_PACKS];
	char path[256];
	KpError e;
	int64_t bad;
	int k;
	kp_world_free(a);
	memset(w, 0, sizeof *w);
	w->active = 1;
	/* the session's mission and tileset file */
	w->mission = kp_session_mission(a->session);
	tileset = kp_session_tileset(a->session, &tileset_size);
	kp_host_tileset_path(&a->session->host, path, sizeof path);
	if (kp_tileset_load(&w->tileset, tileset, tileset_size, path, &e) != 0) {
		return fail(err, err_size, "%s", e.msg);
	}
	w->ids = (uint16_t *)malloc(sizeof *w->ids * (size_t)w->mission->width * (size_t)w->mission->height);
	if (!w->ids || kp_terrain_import(w->mission, &w->tileset, w->ids, &bad) != 0) {
		return fail(err, err_size, "mission %s: a tile selects a missing CV5 group", entry->name);
	}
	if (!w->tileset.sheet_png || !(w->sheet = png_indices(w->tileset.sheet_png, w->tileset.sheet_size, &w->sheet_w,
		&w->sheet_h))) {
		return fail(err, err_size, "%s: no megatile sheet (SHET)", path);
	}
	if (kp_lookups_build(&w->lookups, &a->palettes, (int)(w->mission->tileset & 7), w->tileset.rgb, 0, &e) != 0) {
		return fail(err, err_size, "%s", e.msg);
	}
	/* the mission's resource packs (ADR 0004) with their pages */
	for (k = 0; k < w->mission->pack_count && w->pack_count < KP_APP_PACKS; k++) {
		KpAppPack *p = &w->packs[w->pack_count];
		int j;
		snprintf(p->name, sizeof p->name, "%.*s", (int)w->mission->packs[k].size, (const char *)w->mission->packs[k].data);
		snprintf(path, sizeof path, "generated/packs/%s.kpd", p->name);
		if (!(p->bytes = kp_app_read(a, path, &p->size))) continue; /* a pack of sounds only, or none: drawn without it */
		w->pack_count++; /* the world frees it from here on */
		if (kp_pack_load(&p->pack, p->bytes, p->size, path, &e) != 0) return fail(err, err_size, "%s", e.msg);
		p->pages = (uint8_t **)calloc((size_t)(p->pack.page_count ? p->pack.page_count : 1), sizeof *p->pages);
		if (!p->pages) return fail(err, err_size, "%s: out of memory", path);
		for (j = 0; j < p->pack.page_count; j++) {
			int32_t pw, ph;
			p->pages[j] = png_indices(p->pack.pages[j].png, p->pack.pages[j].size, &pw, &ph);
			if (!p->pages[j] || pw != p->pack.page_w || ph != p->pack.page_h) return fail(err, err_size, "%s: a bad page", path);
		}
		packs[w->pack_count - 1] = &p->pack;
		names[w->pack_count - 1] = p->name;
	}
	if (kp_grpmeta_afrm(grp, &w->afrm, &e) != 0
		|| kp_frame_table_build(&w->frames, grp, w->afrm, packs, names, w->pack_count, &e) != 0
		|| kp_image_data_init(&w->data, w->frames.empty, &e) != 0) {
		return fail(err, err_size, "%s", e.msg);
	}
	kp_bars_init(&w->bars, a->palettes.thpbar, a->palettes.thpbar_size);
	if (kp_presenter_init(&w->presenter, g->w, grp, &w->frames, &w->lookups, &w->bars, g, &e) != 0) {
		return fail(err, err_size, "%s", e.msg);
	}
	kp_fog_layer_init(&w->fog);
	/* the triggers' WAVs (a catalog mission's) */
	if (entry->catalog) {
		size_t n = 0;
		snprintf(path, sizeof path, "generated/missions/%s_sounds.kpd", entry->name);
		w->mission_sounds_bytes = kp_app_read(a, path, &n);
		if (w->mission_sounds_bytes && kp_mission_sounds_load(&w->mission_sounds, w->mission_sounds_bytes, n, path, &e) != 0) {
			SDL_Log("%s", e.msg);
		}
	}
	kp_world_upload(a);
	/* the screen palette: the tileset's with its cycles; the brightness option stays */
	{
		int32_t brightness = a->has_screen_palette ? a->screen_palette.brightness : KP_PALETTE_BRIGHTNESS_DEFAULT;
		kp_palette_init(&a->screen_palette, w->tileset.rgb, &w->tileset.cycles);
		kp_palette_set_brightness(&a->screen_palette, brightness);
		a->has_screen_palette = 1;
		a->palette_clock = kp_app_now();
		/* a glue's game shows it when its console starts (a ready room keeps the glue's) */
		if (!a->glue_on) kp_gfx_palette(a->gfx, kp_palette_rgb(&a->screen_palette));
	}
	w->loads = a->session->loads;
	return 0;
}

int kp_world_build(KpApp *a, char *err, size_t err_size)
{
	if (build(a, err, err_size) == 0) {
		kp_audio_drop_packs(&a->audio); /* the world's packs serve the sounds now */
		return 0;
	}
	kp_world_free(a); /* no half-built world stays active */
	return -1;
}

void kp_world_upload(KpApp *a)
{
	KpAppWorld *w = &a->world;
	const uint8_t **pages;
	int k, j, page_count = 0;
	if (!w->active) return;
	/* every loaded page, in the frame table's page order */
	pages = (const uint8_t **)malloc(sizeof *pages * (size_t)(w->frames.page_count > 0 ? w->frames.page_count : 1));
	if (!pages) {
		SDL_Log("world: out of memory for the atlas pages");
		return;
	}
	for (k = 0; k < w->pack_count; k++) {
		for (j = 0; j < w->packs[k].pack.page_count && page_count < w->frames.page_count; j++) {
			pages[page_count++] = w->packs[k].pages[j];
		}
	}
	kp_gfx_lookups(a->gfx, &w->lookups);
	kp_gfx_pages(a->gfx, page_count, KP_PACK_PAGE_SIZE, KP_PACK_PAGE_SIZE, pages);
	free(pages);
	kp_gfx_frame_table(a->gfx, &w->frames);
	kp_gfx_image_data(a->gfx, &w->data);
	kp_gfx_bars(a->gfx, &w->bars);
	kp_gfx_world(a->gfx, 1);
	kp_gfx_terrain(a->gfx, w->sheet, w->sheet_w, w->sheet_h, w->tileset.sheet_columns, w->ids,
		(int32_t)w->mission->width, (int32_t)w->mission->height);
}

KpRect kp_world_view(const KpApp *a)
{
	KpRect v;
	v.left = (int32_t)a->cam_x;
	v.top = (int32_t)a->cam_y;
	v.right = (int32_t)a->cam_x + a->world_w;
	v.bottom = (int32_t)a->cam_y + a->world_h;
	return v;
}

void kp_world_frame(KpApp *a, int ticks, double fraction)
{
	KpAppWorld *w = &a->world;
	KpRect view = kp_world_view(a);
	int32_t row, rows;
	if (!w->active) return;
	if (ticks > 0) {
		if (kp_presenter_sync(&w->presenter, &w->data, &view, 0, 0) < 0) SDL_Log("images: %s", w->presenter.error.msg);
	} else if (kp_presenter_needs_replan(&w->presenter, &view)) {
		kp_presenter_sync(&w->presenter, &w->data, &view, 1, 0);
	}
	if (kp_image_data_take(&w->data, &row, &rows)) kp_gfx_image_rows(a->gfx, &w->data, row, rows);
	kp_gfx_bar_slots(a->gfx, &w->bars);
	w->bars.dirty_lo = w->bars.dirty_hi = 0;
	a->interp = fraction;
}

void kp_world_draw(KpApp *a)
{
	KpGfxFrame f;
	memset(&f, 0, sizeof f);
	f.win_w = a->win_w;
	f.win_h = a->win_h;
	f.mode = a->mode;
	f.scale = a->scale;
	f.world_w = a->world_w;
	f.world_h = a->world_h;
	f.cam_x = a->cam_x;
	f.cam_y = a->cam_y;
	f.interp = a->world.active ? a->interp : 1;
	f.plan = a->world.active ? &a->world.data.plan : NULL;
	f.world_only = a->flags.world_only;
	kp_gfx_frame(a->gfx, &f);
}
