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

static void free_hd(KpAppWorld *w)
{
	int k;
	kp_hd_loader_free(w->hd_loader); /* its thread stops first */
	w->hd_loader = NULL;
	kp_hd_cache_free(&w->hd_cache);
	for (k = 0; k < KP_APP_PACKS; k++) {
		free(w->hd_bytes[k]);
		w->hd_bytes[k] = NULL;
		memset(&w->hd_packs[k], 0, sizeof w->hd_packs[k]);
	}
	kp_frame_table_free(&w->hd_frames);
	kp_hd_terrain_free(&w->hd_terrain);
	for (k = 0; k < 2; k++) {
		kp_fetch_free(w->hd_fetches[k]);
		w->hd_fetches[k] = NULL;
	}
	free(w->hd_tileset_bytes);
	w->hd_tileset_bytes = NULL;
	free(w->hd_water_bytes);
	w->hd_water_bytes = NULL;
	memset(&w->hd_water, 0, sizeof w->hd_water);
	memset(&w->hd_tileset, 0, sizeof w->hd_tileset);
	w->hd_scale = 0;
	w->hd_lit = 0;
}

void kp_world_free(KpApp *a)
{
	KpAppWorld *w = &a->world;
	int k;
	if (!w->active) return;
	kp_audio_stop_all(&a->audio);
	kp_audio_mission(a);
	if (a->gfx) {
		kp_gfx_hd(a->gfx, 0);
		kp_gfx_world(a->gfx, 0);
		kp_gfx_terrain(a->gfx, NULL, 0, 0, 0, 0, NULL, 0, 0);
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
	free(w->cells);
	free(w->sheet);
	free_hd(w);
	memset(w, 0, sizeof *w);
}

static int fail(char *err, size_t err_size, const char *fmt, const char *what)
{
	snprintf(err, err_size, fmt, what);
	return -1;
}

/* An HD pack's header and frames (its pages stay in the file). */
static int open_hd_pack(KpApp *a, int k, const char *path)
{
	KpAppWorld *w = &a->world;
	size_t n, file_size, need;
	KpError e;
	uint8_t *first = kp_app_read_head(a, path, KP_HD_PACK_FIRST_READ, &n, &file_size);
	if (!first) return -1; /* not imported */
	need = kp_hd_pack_head_size(first, n, file_size);
	free(first);
	if (need == 0 || !(w->hd_bytes[k] = kp_app_read_head(a, path, need, &n, &file_size)) || n != need) {
		SDL_Log("world: %s: not an HD pack", path);
		return -1;
	}
	if (kp_hd_pack_open(&w->hd_packs[k], w->hd_bytes[k], n, file_size, path, &e) != 0) {
		SDL_Log("world: %s", e.msg);
		return -1;
	}
	kp_app_path(a, path, w->hd_paths[k], sizeof w->hd_paths[k]);
	return 0;
}

/* One HD variant's art for the world's packs and tileset (generated/hd/<variant>/); -1 when it is not all there. */
/* The HD world's tileset and water once they are read (fetched on the web, the original drawn meanwhile): its
 * terrain, water and page loader. 1 while they are on their way, 0 done, -1 when the tileset cannot be read or
 * loaded. */
static int hd_finish(KpApp *a)
{
	KpAppWorld *w = &a->world;
	size_t n;
	KpError e;
	if (kp_fetch_state(w->hd_fetches[0]) == KP_FETCH_PENDING || kp_fetch_state(w->hd_fetches[1]) == KP_FETCH_PENDING) {
		return 1;
	}
	w->hd_tileset_bytes = kp_fetch_take(w->hd_fetches[0], &n);
	w->hd_fetches[0] = NULL;
	if (!w->hd_tileset_bytes) {
		SDL_Log("world: %s could not be read", w->hd_tileset_path);
		return -1;
	}
	if (kp_hd_tileset_load(&w->hd_tileset, w->hd_tileset_bytes, n, w->hd_tileset_path, &e) != 0) {
		SDL_Log("world: %s", e.msg);
		return -1;
	}
	if (w->fx) w->hd_tileset.tiles = NULL; /* under the original's art the water masks alone: no HD tiles */
	if (kp_hd_terrain_build(&w->hd_terrain, &w->hd_tileset, w->ids, (int32_t)w->mission->width,
			(int32_t)w->mission->height, &e) != 0) {
		SDL_Log("world: %s", e.msg);
		return -1;
	}
	SDL_Log("world: HD terrain atlas: %d layers of %d x %d", (int)w->hd_terrain.layers, (int)w->hd_terrain.w,
		(int)w->hd_terrain.h);
	w->hd_water_bytes = kp_fetch_take(w->hd_fetches[1], &n);
	w->hd_fetches[1] = NULL;
	if (w->hd_water_bytes && kp_hd_water_load(&w->hd_water, w->hd_water_bytes, n, "water.kpd", &e) != 0) {
		SDL_Log("world: %s (the water stays still)", e.msg);
		free(w->hd_water_bytes);
		w->hd_water_bytes = NULL;
	}
	w->hd_clock = kp_app_now();
	if (w->fx) return 0; /* the water masks under the original's art: no HD pages */
	if (!(w->hd_loader = kp_hd_loader_new())) return -1;
	w->hd_scale = w->hd_packs[0].scale;
	return 0;
}

/* Starts reading a variant's tileset and water.kpd (fetched on the web), then finishes when they are read: 0 (or on
 * their way), -1 when they are not there. */
static int load_tileset(KpApp *a, const char *variant)
{
	KpAppWorld *w = &a->world;
	char tileset[256], path[256], full[1200];
	const char *base;
	kp_host_tileset_path(&a->flow.session->host, tileset, sizeof tileset);
	base = strrchr(tileset, '/');
	snprintf(w->hd_tileset_path, sizeof w->hd_tileset_path, "generated/hd/%s/tilesets/%s", variant,
		base ? base + 1 : tileset);
	kp_app_path(a, w->hd_tileset_path, full, sizeof full);
	w->hd_fetches[0] = kp_fetch_start(full, 0, 0);
	snprintf(path, sizeof path, "generated/hd/%s/water.kpd", variant);
	kp_app_path(a, path, full, sizeof full);
	w->hd_fetches[1] = kp_fetch_start(full, 0, 0);
	return w->hd_fetches[0] && hd_finish(a) >= 0 ? 0 : -1;
}

static int load_hd_variant(KpApp *a, const char *variant, const KpGrpMeta *grp, const KpPack *const *packs,
	const char *const *names)
{
	KpAppWorld *w = &a->world;
	const KpHdPack *hd[KP_APP_PACKS];
	char path[256];
	KpError e;
	int k;
	for (k = 0; k < w->pack_count; k++) {
		snprintf(path, sizeof path, "generated/hd/%s/packs/%s.kpd", variant, names[k]);
		if (open_hd_pack(a, k, path) != 0) return -1;
		if (k > 0 && (w->hd_packs[k].scale != w->hd_packs[0].scale
			|| w->hd_packs[k].page_size != w->hd_packs[0].page_size || w->hd_packs[k].format != w->hd_packs[0].format)) {
			SDL_Log("world: HD packs of two scales, page sizes or formats");
			return -1;
		}
		w->hd_page_base[k + 1] = w->hd_page_base[k] + w->hd_packs[k].page_count;
		hd[k] = &w->hd_packs[k];
	}
	if (w->pack_count == 0) return -1;
	if (kp_hd_frame_table_build(&w->hd_frames, &w->frames, grp, packs, hd, names, w->pack_count, &e) != 0) {
		SDL_Log("world: %s", e.msg);
		return -1;
	}
	return load_tileset(a, variant);
}

const char *kp_app_hd_variant(const KpApp *a, int k)
{
	int g = a->flags.graphics;
	if (g == KP_GRAPHICS_ORIGINAL) return NULL;
	if (g == KP_VIDEO_AUTO && a->flow.scale < KP_HD_AUTO_SCALE) return NULL;
#if defined(SDL_PLATFORM_ANDROID) || defined(SDL_PLATFORM_IOS) || defined(__EMSCRIPTEN__)
	return k == 0 ? "hd2" : NULL; /* a phone's memory: HD2 only; the web serves HD2 only */
#else
	{
		static const char *const VARIANTS[2] = { "hd", "hd2" };
		int first = g == KP_GRAPHICS_HD || (g == KP_VIDEO_AUTO && a->flow.scale > KP_HD_X4_SCALE) ? 0 : 1;
		if (g == KP_GRAPHICS_HD2) return k == 0 ? VARIANTS[1] : NULL;
		return k < 2 ? VARIANTS[(first + k) % 2] : NULL;
	}
#endif
}

/* The modern effects the world asks for (stage R5): the smooth fog, the water's surface; by default the art's. */
static int fx_smooth(const KpApp *a)
{
	return a->flags.fog_style == KP_VIDEO_AUTO ? a->world.hd_scale != 0 : a->flags.fog_style == KP_FOG_SMOOTH;
}

static int fx_water(const KpApp *a)
{
	return a->flags.water == KP_VIDEO_AUTO ? a->world.hd_scale != 0 : a->flags.water == KP_WATER_ANIMATED;
}

/* StarCraft: Remastered's HD art (ADR 0013) when --graphics asks for it and the player imported it; else, when the
 * effects ask for them, the original's art through the RGBA path with the water masks of an imported variant. */
static int load_hd(KpApp *a, const KpGrpMeta *grp, const KpPack *const *packs, const char *const *names)
{
	static const char *const MASKS[2] = { "hd2", "hd" };
	const char *variant;
	int k;
	for (k = 0; (variant = kp_app_hd_variant(a, k)) != NULL; k++) {
		if (load_hd_variant(a, variant, grp, packs, names) == 0) {
			SDL_Log("world: StarCraft: Remastered's %s art", variant);
			return 1;
		}
		free_hd(&a->world);
	}
	if (!fx_smooth(a) && !fx_water(a)) return 0;
	a->world.fx = 1;
	SDL_Log("world: the original's art with the modern effects (fog %s, water %s)", fx_smooth(a) ? "smooth" : "original",
		fx_water(a) ? "on" : "off");
	for (k = 0; fx_water(a) && k < 2; k++) { /* the water masks: HD2's are enough */
#if defined(SDL_PLATFORM_ANDROID) || defined(SDL_PLATFORM_IOS) || defined(__EMSCRIPTEN__)
		if (k > 0) break; /* HD2 only there */
#endif
		if (load_tileset(a, MASKS[k]) == 0) return 0;
		free_hd(&a->world);
	}
	if (fx_water(a)) SDL_Log("world: no water masks imported (python3 -m converter hd): the water stays still");
	return 0;
}

/* The HD variant the options' art comes to: the first they ask for that the player imported (video_available), NULL
 * for the original's. Another one asked for (auto at another scale) that is not there changes nothing. */
static const char *art_wanted(const KpApp *a)
{
	const char *v;
	int k;
	for (k = 0; (v = kp_app_hd_variant(a, k)) != NULL; k++) {
		if (a->flow.video_available & (strcmp(v, "hd") == 0 ? KP_VIDEO_HAS_HD : KP_VIDEO_HAS_HD2)) return v;
	}
	return NULL;
}

/* The HD art or the effects as the options ask (load_hd), and what they asked for. load_hd: 1 when the HD art was
 * chosen (on the web its tileset may still be on its way), else 0. */
static void load_art(KpApp *a, const KpGrpMeta *grp, const KpPack *const *packs, const char *const *names)
{
	a->world.art_hd = load_hd(a, grp, packs, names);
	a->world.art_variant = art_wanted(a);
	a->world.art_water = fx_water(a);
}

/* Whether the options (or the scale: auto) ask for other art than the world has: another HD variant or none, or over
 * the original's art the effects' path on or off, or the water masks. The HD art draws its fog and water as they ask
 * each frame. */
static int art_changed(const KpApp *a)
{
	const KpAppWorld *w = &a->world;
	const char *v = art_wanted(a);
	if ((v == NULL) != (w->art_variant == NULL) || (v != NULL && strcmp(v, w->art_variant) != 0)) return 1;
	if (w->art_hd) return 0; /* also while the web still fetches its tileset (hd_scale 0 until then) */
	return (fx_smooth(a) || fx_water(a)) != w->fx || (w->fx && fx_water(a) != w->art_water);
}

/* The world's art again, while the game goes on: kp_gfx's objects deleted (no HD textures stay), the art loaded as at
 * the start and everything uploaded again (the HD interface's too). */
static void reload_art(KpApp *a)
{
	KpAppWorld *w = &a->world;
	const KpPack *packs[KP_APP_PACKS];
	const char *names[KP_APP_PACKS];
	char err[256];
	int k;
	free_hd(w);
	w->fx = 0;
	for (k = 0; k < w->pack_count; k++) {
		packs[k] = &w->packs[k].pack;
		names[k] = w->packs[k].name;
	}
	load_art(a, kp_session_grp(a->flow.session), packs, names);
	if (kp_gfx_reset(a->gfx, err, sizeof err) != 0) {
		SDL_Log("%s (nothing is drawn any more)", err);
		return;
	}
	kp_world_upload(a);
	kp_hd_ui_lost(a);
}

/* Creep's edge frames (the tileset's EDGE) into the sheet's cells after its megatiles, the sheet grown by the rows they
 * need; none when the tileset has none or they cannot be read. */
static void sheet_edges(KpAppWorld *w)
{
	int32_t ew, eh, cols = w->tileset.sheet_columns, first = w->tileset.sheet_count, n = w->tileset.edge_count, rows, k, y;
	uint8_t *edges;
	w->edge_count = 0;
	if (!w->tileset.edge_png || n <= 0 || cols <= 0) return;
	if (!(edges = png_indices(w->tileset.edge_png, w->tileset.edge_size, &ew, &eh))) return;
	rows = (first + n + cols - 1) / cols * KP_WORLD_TILE;
	if (ew != n * KP_WORLD_TILE || eh != KP_WORLD_TILE || w->sheet_w != cols * KP_WORLD_TILE) {
		SDL_Log("world: the tileset's creep edges do not fit its sheet: drawn without them");
		free(edges);
		return;
	}
	if (rows > w->sheet_h) {
		uint8_t *grown = (uint8_t *)realloc(w->sheet, (size_t)w->sheet_w * (size_t)rows);
		if (!grown) {
			free(edges);
			return;
		}
		memset(grown + (size_t)w->sheet_w * (size_t)w->sheet_h, 0, (size_t)w->sheet_w * (size_t)(rows - w->sheet_h));
		w->sheet = grown;
		w->sheet_h = rows;
	}
	for (k = 0; k < n; k++) {
		int32_t cell = first + k, x0 = (cell % cols) * KP_WORLD_TILE, y0 = (cell / cols) * KP_WORLD_TILE;
		for (y = 0; y < KP_WORLD_TILE; y++) {
			memcpy(w->sheet + (size_t)(y0 + y) * (size_t)w->sheet_w + (size_t)x0,
				edges + (size_t)y * (size_t)ew + (size_t)k * KP_WORLD_TILE, KP_WORLD_TILE);
		}
	}
	free(edges);
	w->edge_count = n;
}

static int build(KpApp *a, char *err, size_t err_size)
{
	KpAppWorld *w = &a->world;
	const KpAppMission *entry = &a->missions[a->flow.current];
	KpGame *g = a->flow.session->g;
	const KpGrpMeta *grp = kp_session_grp(a->flow.session);
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
	w->mission = kp_session_mission(a->flow.session);
	tileset = kp_session_tileset(a->flow.session, &tileset_size);
	kp_host_tileset_path(&a->flow.session->host, path, sizeof path);
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
	sheet_edges(w);
	{ /* the cells as imported; the game's display megatiles and creep's edges replace them each frame (sync_terrain) */
		size_t n = (size_t)w->mission->width * (size_t)w->mission->height, i;
		if (!(w->cells = (uint16_t *)calloc(2 * n, sizeof *w->cells))) return fail(err, err_size, "%s", "out of memory");
		for (i = 0; i < n; i++) w->cells[2 * i] = w->ids[i];
	}
	if (kp_lookups_build(&w->lookups, &a->palettes, (int)(w->mission->tileset & 7), w->tileset.rgb, 0, &e) != 0) {
		return fail(err, err_size, "%s", e.msg);
	}
	/* the mission's resource packs (ADR 0004) with their pages */
	if (w->mission->pack_count > KP_APP_PACKS) {
		snprintf(err, err_size, "%s: names %d packs, the world holds %d", entry->name, w->mission->pack_count, KP_APP_PACKS);
		return -1;
	}
	for (k = 0; k < w->mission->pack_count; k++) {
		KpAppPack *p = &w->packs[w->pack_count];
		int j;
		if (w->mission->packs[k].size >= sizeof p->name) {
			snprintf(err, err_size, "%s: a pack name of %d bytes", entry->name, (int)w->mission->packs[k].size);
			return -1;
		}
		snprintf(p->name, sizeof p->name, "%.*s", (int)w->mission->packs[k].size, (const char *)w->mission->packs[k].data);
		snprintf(path, sizeof path, "generated/packs/%s.kpd", p->name);
		/* a pack the mission names but the data lacks would leave its sprites undrawn without a word */
		if (!(p->bytes = kp_app_read(a, path, &p->size))) return fail(err, err_size, "%s: cannot read the pack", path);
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
	load_art(a, grp, packs, names);
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
	/* the screen palette: the tileset's with its cycles, in the gamma option */
	{
		kp_palette_init(&a->flow.screen_palette, w->tileset.rgb, &w->tileset.cycles);
		kp_palette_set_brightness(&a->flow.screen_palette, a->flow.gamma);
		a->flow.has_screen_palette = 1;
		a->flow.palette_clock = kp_app_now();
		/* a glue's game shows it when its console starts (a ready room keeps the glue's) */
		if (!a->flow.glue_on) kp_gfx_palette(a->gfx, kp_palette_rgb(&a->flow.screen_palette));
	}
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

int32_t kp_world_hd_lit_pages(const KpApp *a)
{
	int32_t k, n = 0;
	for (k = 0; k < a->world.pack_count; k++) n += a->world.hd_packs[k].lit_pages;
	return n;
}

/* Whether the HD sprites are to be lit: the video options' lighting (Auto: on) where the pages go up as their BC blocks
 * and the packs have lit pages (decoded and ETC2 pages draw unlit, whatever the options ask). */
static int hd_lit_wanted(const KpApp *a)
{
	const KpAppWorld *w = &a->world;
	return a->flags.lighting != KP_LIGHTING_OFF && !w->hd_decode && w->hd_packs[0].format == KP_HD_FORMAT_BC
		&& kp_world_hd_lit_pages(a) > 0;
}

/* The HD pages' slots the budget affords and their cache, the lit pages' own slots with their lighting layers (BC
 * blocks only, when the lighting is on; unlit when the GPU cannot hold them); their contents are gone (a new context,
 * the lighting changed): every page is loaded again. -1 out of memory. */
static int hd_slots(KpApp *a)
{
	KpAppWorld *w = &a->world;
	int32_t hd_pages = w->hd_page_base[w->pack_count], size = w->hd_packs[0].page_size, slots = 0, lit_slots = 0;
	int32_t lit_pages = kp_world_hd_lit_pages(a), k, j;
	int format = w->hd_packs[0].format, lighting;
	size_t pixels = (size_t)size * (size_t)size, page_bytes, lit_bytes;
	size_t budget = a->flags.hd_budget > 0 ? (size_t)a->flags.hd_budget << 20 : (size_t)KP_HD_BUDGET;
	uint8_t *lit = NULL;
	w->hd_decode = a->flags.hd_decode || !kp_gfx_hd_takes(a->gfx, format);
	page_bytes = w->hd_decode ? pixels * 5 : KP_HD_PAGE_DIFFUSE_BYTES(size) + KP_HD_PAGE_TEAM_BYTES(size);
	lit_bytes = page_bytes;
	for (j = 0; j < KP_HD_LIGHTS; j++) lit_bytes += KP_HD_PAGE_LIGHT_BYTES(size, j);
	w->hd_lighting = hd_lit_wanted(a);
	lighting = w->hd_lighting && (lit = (uint8_t *)calloc((size_t)hd_pages, 1)) != NULL;
	for (k = 0; k < w->pack_count && lighting; k++) {
		for (j = 0; j < w->hd_packs[k].lit_pages; j++) lit[w->hd_page_base[k] + j] = 1;
	}
	w->hd_generation++;
	w->hd_lit = 0;
	for (;; lighting = 0) { /* with the lighting layers, else (the GPU short of memory for them) without */
		slots = kp_hd_slots_split(budget, page_bytes, lit_bytes, hd_pages, lighting ? lit_pages : 0, KP_HD_SLOTS_MAX,
			&lit_slots);
		kp_hd_cache_free(&w->hd_cache);
		if (kp_hd_cache_init(&w->hd_cache, hd_pages, slots) != 0
			|| (lit_slots > 0 && kp_hd_cache_lit(&w->hd_cache, lit, lit_slots) != 0)) {
			free(lit);
			return -1;
		}
		if (kp_gfx_hd_slots(a->gfx, slots, lit_slots, size, format, !w->hd_decode) == 0) break;
		if (!lighting) {
			free(lit);
			return -1;
		}
		SDL_Log("world: out of memory for the HD lighting layers: the sprites unlit");
	}
	free(lit);
	w->hd_lit = lit_slots > 0;
	kp_gfx_hd_page_slots(a->gfx, w->hd_cache.slot_of, hd_pages);
	kp_hd_cache_take_dirty(&w->hd_cache);
	w->hd_prime = 1;
	SDL_Log("world: %d HD page slots of %d pages (%s), %d lit of %d", (int)slots, (int)hd_pages,
		w->hd_decode ? "decoded" : format == KP_HD_FORMAT_ETC2 ? "ETC2 blocks" : "BC blocks", (int)lit_slots,
		(int)lit_pages);
	return 0;
}

/* The HD pages' slots (hd_slots), else, out of memory, the world without its HD art. */
static void hd_slots_or_drop(KpApp *a)
{
	if (hd_slots(a) == 0) return;
	SDL_Log("world: out of memory for the HD pages: the original's art");
	free_hd(&a->world);
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
	kp_gfx_terrain(a->gfx, w->sheet, w->sheet_w, w->sheet_h, w->tileset.sheet_columns, w->tileset.sheet_count, w->cells,
		(int32_t)w->mission->width, (int32_t)w->mission->height);
	if (w->hd_scale) hd_slots_or_drop(a);
	if (w->hd_scale) {
		kp_gfx_hd_frame_table(a->gfx, &w->hd_frames, w->hd_scale);
		kp_gfx_hd_colors(a->gfx, &w->lookups, w->tileset.rgb, a->palettes.tminimap, a->palettes.tminimap_size);
		kp_gfx_hd_terrain(a->gfx, &w->hd_terrain);
		kp_gfx_hd_water(a->gfx, w->hd_water_bytes ? &w->hd_water : NULL);
		{ /* the HD world's tone follows the screen palette's fades and brightness; the colour cycles do not count */
			uint8_t skip[256];
			int32_t c, i;
			memset(skip, 0, sizeof skip);
			skip[0] = 1;
			for (c = 0; c < w->tileset.cycles.count; c++) {
				for (i = w->tileset.cycles.low[c]; i <= w->tileset.cycles.high[c]; i++) skip[i] = 1;
			}
			kp_gfx_hd_tone_base(a->gfx, w->tileset.rgb, skip);
		}
	} else if (w->fx) { /* the original's art in RGBA: its shadows' row; the water masks when they came */
		kp_gfx_hd_colors(a->gfx, &w->lookups, w->tileset.rgb, a->palettes.tminimap, a->palettes.tminimap_size);
		if (w->hd_tileset_bytes) {
			kp_gfx_hd_terrain(a->gfx, &w->hd_terrain);
			kp_gfx_hd_water(a->gfx, w->hd_water_bytes ? &w->hd_water : NULL);
		}
	}
	kp_gfx_hd(a->gfx, w->hd_scale != 0 || w->fx);
}

KpRect kp_world_view(const KpApp *a)
{
	KpRect v;
	v.left = (int32_t)a->flow.cam_x;
	v.top = (int32_t)a->flow.cam_y;
	v.right = (int32_t)a->flow.cam_x + a->flow.world_w;
	v.bottom = (int32_t)a->flow.cam_y + a->flow.world_h;
	return v;
}

/* The HD pages the drawn images show (again when the image data changed: `changed`): asked of the loader, and those
 * it read uploaded (KP_HD_UPLOADS_A_FRAME a frame; the first frame with images waits for its own). */
static void stream_hd(KpApp *a, int changed)
{
	KpAppWorld *w = &a->world;
	KpHdCache *c = &w->hd_cache;
	KpHdLoaded r;
	int32_t page, uploads = 0, limit = KP_HD_UPLOADS_A_FRAME;
	int prime;
	if (!c->slot_of) return;
	if (changed || w->hd_prime) {
		kp_hd_cache_tick(c);
		kp_hd_cache_want_images(c, &w->data, &w->hd_frames);
	}
	while (kp_hd_cache_take(c, &page)) {
		int k = 0;
		int32_t local;
		uint32_t diffuse, team, light[KP_HD_LIGHTS];
		while (k + 1 < w->pack_count && page >= w->hd_page_base[k + 1]) k++;
		local = page - w->hd_page_base[k];
		kp_hd_page_offsets(&w->hd_packs[k], local, &diffuse, &team, light);
		if (kp_hd_loader_request(w->hd_loader, page, w->hd_paths[k], diffuse, team,
			w->hd_lit && local < w->hd_packs[k].lit_pages ? light : NULL, w->hd_packs[k].page_size,
			w->hd_packs[k].format, w->hd_decode, w->hd_generation) != 0) {
			kp_hd_cache_failed(c, page);
		}
	}
	prime = w->hd_prime && kp_hd_cache_pending(c) > 0;
	if (prime || (w->hd_prime && changed)) w->hd_prime = 0; /* a frame whose images have their pages waits for none */
	if (prime) limit = KP_HD_PRIME_PAGES;
	while (uploads < limit && kp_hd_loader_result(w->hd_loader, &r, prime)) {
		int32_t slot, evicted;
		if (r.generation != w->hd_generation) {
			kp_hd_loaded_free(&r);
			continue;
		}
		if (!r.ok) {
			SDL_Log("world: HD page %d could not be read: drawn in SD", (int)r.page);
			kp_hd_cache_failed(c, r.page);
			kp_hd_loaded_free(&r);
			continue;
		}
		slot = kp_hd_cache_place(c, r.page, &evicted);
		if (slot >= 0) {
			kp_gfx_hd_page(a->gfx, slot, r.diffuse, r.team, r.light[0] ? r.light : NULL);
			uploads++;
		}
		kp_hd_loaded_free(&r);
	}
	if (kp_hd_cache_take_dirty(c)) kp_gfx_hd_page_slots(a->gfx, c->slot_of, c->pages);
}

/* The terrain the game shows (its display megatiles: creep, the remembered terrain; and creep's edges over the tiles
 * beside it: kp_terrain_edge), uploaded when it changed; the HD atlas built again when it shows a megatile the atlas
 * lacks. */
static void sync_terrain(KpApp *a)
{
	KpAppWorld *w = &a->world;
	const int64_t *shown = a->flow.session->g->terrain.megatiles, *borders = a->flow.session->g->terrain.creep_border;
	int32_t n = (int32_t)w->mission->width * (int32_t)w->mission->height, i;
	int changed = 0, cells_changed = 0;
	KpError e;
	for (i = 0; i < n; i++) {
		uint16_t id = (uint16_t)(shown[i] & 0xffff), edge = kp_terrain_edge(id, borders[i], w->edge_count);
		if (w->ids[i] != id) {
			w->ids[i] = id;
			changed = 1;
		}
		if (w->cells[2 * i] != id || w->cells[2 * i + 1] != edge) {
			w->cells[2 * i] = id;
			w->cells[2 * i + 1] = edge;
			cells_changed = 1;
		}
	}
	if (!cells_changed) return;
	kp_gfx_terrain(a->gfx, NULL, 0, 0, 0, 0, w->cells, (int32_t)w->mission->width, (int32_t)w->mission->height);
	if (!changed || !w->hd_tileset_bytes) return;
	if (kp_hd_terrain_relink(&w->hd_terrain, w->ids) == 0) {
		kp_gfx_hd_terrain_slots(a->gfx, &w->hd_terrain);
		return;
	}
	kp_hd_terrain_free(&w->hd_terrain);
	if (kp_hd_terrain_build(&w->hd_terrain, &w->hd_tileset, w->ids, (int32_t)w->mission->width,
			(int32_t)w->mission->height, &e) != 0) {
		SDL_Log("world: %s", e.msg);
		return;
	}
	kp_gfx_hd_terrain_atlas(a->gfx, &w->hd_terrain); /* the tileset's edges are up already */
}

void kp_world_frame(KpApp *a, int ticks, double fraction)
{
	KpAppWorld *w = &a->world;
	KpRect view = kp_world_view(a);
	KpRowRange ranges[KP_IMAGE_DATA_RUNS];
	int ranges_count;
	int changed;
	if (!w->active) return;
	if (art_changed(a)) reload_art(a);
	if (w->hd_scale && hd_lit_wanted(a) != w->hd_lighting) { /* the video options' lighting changed: the slots again */
		hd_slots_or_drop(a);
		if (!w->hd_scale) kp_world_upload(a);
	}
	if (ticks > 0) {
		if (kp_presenter_sync(&w->presenter, &w->data, &view, 0, 0) < 0) SDL_Log("images: %s", w->presenter.error.msg);
	} else if (kp_presenter_needs_replan(&w->presenter, &view)) {
		kp_presenter_sync(&w->presenter, &w->data, &view, 1, 0);
	}
	if (w->presenter.skipped != w->presenter.skipped_before && w->presenter.skipped > 0) {
		SDL_Log("images: %d left out, the first: %s", (int)w->presenter.skipped, w->presenter.skip.msg);
	}
	changed = kp_image_data_take(&w->data, ranges, &ranges_count);
	if (changed) kp_gfx_image_rows(a->gfx, &w->data, ranges, ranges_count);
	kp_gfx_bar_slots(a->gfx, &w->bars);
	w->bars.dirty_lo = w->bars.dirty_hi = 0;
	/* a frame without a step leaves the terrain as it was unless the view's refresh or a load changed it */
	if (ticks > 0 || !w->terrain_synced || a->flow.session->g->minimap_terrain != w->terrain_serial) {
		sync_terrain(a);
		w->terrain_synced = 1;
		w->terrain_serial = a->flow.session->g->minimap_terrain;
	}
	if (w->hd_fetches[0]) { /* the HD tileset arrived (the web): the world drawn in HD from here */
		int done = hd_finish(a);
		if (done == 0) kp_world_upload(a);
		if (done < 0) free_hd(w);
	}
	if (w->hd_scale) stream_hd(a, changed);
	a->interp = fraction;
}

void kp_world_draw(KpApp *a)
{
	KpGfxFrame f;
	memset(&f, 0, sizeof f);
	f.win_w = a->flow.win_w;
	f.win_h = a->flow.win_h;
	f.win_x = a->flow.win_x;
	f.win_y = a->flow.win_y;
	f.full_w = a->flow.full_w;
	f.full_h = a->flow.full_h;
	f.mode = a->flow.mode;
	f.scale = a->flow.scale;
	f.world_w = a->flow.world_w;
	f.world_h = a->flow.world_h;
	f.cam_x = a->flow.cam_x;
	f.cam_y = a->flow.cam_y;
	f.interp = a->world.active ? a->interp : 1;
	f.plan = a->world.active ? &a->world.data.plan : NULL;
	f.world_only = a->flags.world_only;
	f.time = a->world.active && (a->world.hd_scale || a->world.fx) ? kp_app_now() - a->world.hd_clock : 0;
	if (a->world.active && a->world.fx) { /* the original's art at the whole screen scale, KP_FX_SCALE_MAX at most */
		int32_t s = (int32_t)(a->flow.scale + 1e-6);
		kp_gfx_effects(a->gfx, s < 1 ? 1 : s > KP_FX_SCALE_MAX ? KP_FX_SCALE_MAX : s, fx_smooth(a), fx_water(a));
	} else {
		kp_gfx_effects(a->gfx, 0, 1, a->world.active ? fx_water(a) : 1);
	}
	kp_gfx_frame(a->gfx, &f);
}
