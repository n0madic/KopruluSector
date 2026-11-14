/*
 * The flow's host over SDL (kp_flow.h; plan phase S, stage S5): the clock, the data files and the saves' sync, the
 * world's presentation and drawing, the sound, the cinematics, the cursor and the vector text under the flow's glue
 * screens and games. The flow itself (the screens' order, the campaign, the saves, the video options) is kp_flow.c.
 */
#include "kp_app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

double kp_app_wall(void) { return (double)SDL_GetTicksNS() / 1e9; }

/* the fixed clock starts at 1 s: the interface's 0 is "none" */
double kp_app_now(const KpApp *a) { return a->clock_fixed ? 1 + (double)a->clock_frames / KP_FIXED_HZ : kp_app_wall(); }

void kp_app_clock_frame(KpApp *a)
{
	if (a->clock_fixed && !a->clock_hold && !kp_app_cine_active(a)) a->clock_frames++;
}

/* the step's cost: always the wall clock's */
static double clock_ms(void) { return (double)SDL_GetTicksNS() / 1e6; }

static double host_now(void *ctx) { return kp_app_now((const KpApp *)ctx); }

static void host_log(void *ctx, const char *line)
{
	(void)ctx;
	SDL_Log("%s", line);
}

static uint8_t *host_read(void *ctx, const char *path, size_t *size) { return kp_app_read((KpApp *)ctx, path, size); }

static void host_save_profile(void *ctx) { kp_app_save_profile((KpApp *)ctx); }

static void host_sync_files(void *ctx) { kp_app_sync_files((KpApp *)ctx); }

static void host_quit(void *ctx) { ((KpApp *)ctx)->quit = 1; }

static int host_world_build(void *ctx, char *err, size_t err_size) { return kp_world_build((KpApp *)ctx, err, err_size); }

static void host_world_free(void *ctx) { kp_world_free((KpApp *)ctx); }

/* The world after the game's steps (restart: a loaded save, its state at once without motion from the state before,
 * its first frame waiting for its HD pages as a game's first frame does), and the fog over it. */
static void host_world_frame(void *ctx, int ticks, double fraction, int restart)
{
	KpApp *a = (KpApp *)ctx;
	const KpFlow *f = &a->flow;
	if (restart) {
		kp_presenter_restart(&a->world.presenter);
		a->world.hd_prime = 1;
	}
	kp_world_frame(a, ticks, fraction);
	if (a->flags.fog) {
		kp_fog_layer_update(&a->world.fog, f->session->g, f->cam_x, f->cam_y, f->world_w, f->world_h);
		kp_gfx_fog(a->gfx, &a->world.fog, a->data.palettes.tilesets[a->world.mission->tileset & 7].dark);
	} else {
		kp_gfx_fog(a->gfx, NULL, NULL);
	}
}

static void host_hud_map(void *ctx, KpHudMap *m)
{
	KpApp *a = (KpApp *)ctx;
	m->tileset = &a->world.tileset;
	m->palettes = &a->data.palettes;
	m->tileset_index = (int)(a->world.mission->tileset & 7);
	m->colors = a->world.mission->has_colors ? a->world.mission->colors : NULL;
}

static void host_interface(void *ctx, KpUi *ui)
{
	(void)ui;
	kp_cursor_init(&((KpApp *)ctx)->cursor);
}

/* The vector text of large scales (ADR 0011): the interface records its glyphs unless the font mode is the original's,
 * and its canvas takes them out while the mode is on at this scale with the font's atlas made. */
static void vector_text_mode(KpApp *a)
{
	KpUi *ui = a->flow.ui;
	int record, on;
	if (ui == NULL) return;
	if (a->vtext_ui != ui) { /* a new interface: its fonts, or --vector-font's for all */
		const uint8_t *const *files;
		const size_t *sizes;
		const uint8_t *one[1];
		size_t one_size[1];
		int count = kp_ui_vector_fonts(ui, &files, &sizes);
		kp_vtext_free(&a->vtext);
		if (a->flags.vector_font[0]) {
			if (!a->vtext_file) a->vtext_file = SDL_LoadFile(a->flags.vector_font, &a->vtext_file_size);
			one[0] = (const uint8_t *)a->vtext_file;
			one_size[0] = a->vtext_file_size;
			files = one;
			sizes = one_size;
			count = a->vtext_file ? 1 : 0;
			if (!a->vtext_file) SDL_Log("vector text: cannot read %s", a->flags.vector_font);
		}
		if (count > 0 && kp_vtext_init(&a->vtext, files, sizes, count) != 0) SDL_Log("vector text: a font is no TrueType font");
		if (a->vtext.ttf_count > 0 && kp_vtext_add_codes(&a->vtext, ui->supplement_codes, ui->supplement_code_count) != 0) {
			SDL_Log("vector text: out of memory, the supplement's glyphs stay bitmaps");
		}
		kp_ui_set_vector_extra(ui, a->vtext.extra_codes, a->vtext.extra_count);
		a->vtext.size = a->flags.vector_size > 0 ? a->flags.vector_size : 1.0;
		a->vtext_ui = ui;
	}
	record = a->vtext.ttf_count > 0 && a->flags.font_mode != KP_VTEXT_ORIGINAL;
	on = record && kp_vtext_active(a->flags.font_mode, a->flow.scale)
		&& kp_vtext_atlas(&a->vtext, ui->font_files, ui->font_file_sizes, a->flow.scale) == 0;
	if (record != (ui->text.log != NULL) || on != ui->vector_text) kp_ui_set_vector_text(ui, record, on, a->vtext.codes);
}

/* The canvas's vector glyphs for the frame (NULL canvas: none). */
static void vector_text_draw(KpApp *a, const KpCanvas *c)
{
	if (c != NULL && a->flow.ui != NULL && a->flow.ui->vector_text) {
		kp_vtext_quads(&a->vtext, &c->canvas);
		kp_gfx_text(a->gfx, &a->vtext);
	} else {
		kp_gfx_text(a->gfx, NULL);
	}
}

/* Before a canvas is composed: the vector text's mode and the HD interface's. */
static void host_canvas_begin(void *ctx, KpCanvas *c)
{
	KpApp *a = (KpApp *)ctx;
	vector_text_mode(a);
	kp_hd_ui_mode(a, c);
}

/* The canvas composed (its HD items, its vector glyphs), or none. */
static void host_canvas(void *ctx, KpCanvas *c)
{
	KpApp *a = (KpApp *)ctx;
	if (c) kp_hd_ui_draw(a, c);
	kp_gfx_canvas(a->gfx, c);
	vector_text_draw(a, c);
}

/* The interface's cursor is drawn over the canvas; the system's is hidden over the window while it is. */
static void host_cursor(void *ctx, KpUi *ui)
{
	KpApp *a = (KpApp *)ctx;
	int drawn = kp_cursor_frame(&a->cursor, ui, kp_input_cursor(a) != KP_HUD_POINTER_NONE);
	if (drawn) {
		kp_gfx_cursor(a->gfx, &a->cursor, a->mouse_lx + a->cursor.dx, a->mouse_ly + a->cursor.dy);
	} else {
		kp_gfx_cursor(a->gfx, NULL, 0, 0);
	}
	kp_hd_ui_cursor(a, drawn);
	if ((drawn != 0) == SDL_CursorVisible()) {
		if (drawn) SDL_HideCursor();
		else SDL_ShowCursor();
	}
}

static int host_pointer(void *ctx, int32_t *x, int32_t *y)
{
	KpApp *a = (KpApp *)ctx;
	*x = a->mouse_lx;
	*y = a->mouse_ly;
	return kp_input_cursor(a);
}

static void host_sounds(void *ctx, const KpUiSoundCommand *c, size_t n) { kp_audio_commands((KpApp *)ctx, c, n); }

static void host_stop_sounds(void *ctx) { kp_audio_stop_effects(&((KpApp *)ctx)->audio); }

static void host_palette(void *ctx, const uint8_t *rgb) { kp_gfx_palette(((KpApp *)ctx)->gfx, rgb); }

static void host_gamma(void *ctx, int32_t gamma) { kp_gfx_gamma(((KpApp *)ctx)->gfx, gamma); }

/* The options the player has: the HD variants the data has and this context draws (a computer's x4; a phone's and the
 * web's x2 only; kp_world_hd_pick), the lighting where the context takes such a variant's pages' blocks (decoded pages
 * draw unlit) and the vector text's TrueType fonts. */
static uint8_t host_video_available(void *ctx, const KpUi *ui)
{
	static const struct {
		const char *name;
		uint8_t has;
	} VARIANTS[] = {
		{ "hd2", KP_VIDEO_HAS_HD2 },
#if !defined(SDL_PLATFORM_ANDROID) && !defined(SDL_PLATFORM_IOS) && !defined(__EMSCRIPTEN__)
		{ "hd", KP_VIDEO_HAS_HD },
#endif
	};
	KpApp *a = (KpApp *)ctx;
	uint8_t has = 0;
	size_t k;
	const uint8_t *const *fonts;
	const size_t *font_sizes;
	for (k = 0; k < sizeof VARIANTS / sizeof VARIANTS[0]; k++) {
		KpHdPick p;
		if (!kp_world_hd_pick(a, VARIANTS[k].name, &p)) continue;
		has |= VARIANTS[k].has;
		if (!a->flags.hd_decode && kp_gfx_hd_takes(a->gfx, p.format)) has |= KP_VIDEO_HAS_LIGHTING;
	}
	if (kp_ui_vector_fonts(ui, &fonts, &font_sizes) > 0 || a->flags.vector_font[0]) has |= KP_VIDEO_HAS_VECTOR;
	return has;
}

static int host_cine_has(void *ctx, int id) { return kp_app_cine_has((const KpApp *)ctx, id); }

static int host_cine_start(void *ctx, const int *ids, int n, int preludes)
{
	return kp_app_cine_start((KpApp *)ctx, ids, n, preludes);
}

static int host_cine_active(void *ctx) { return kp_app_cine_active((const KpApp *)ctx); }

static int host_exists(void *ctx, const char *path) { return kp_app_exists((KpApp *)ctx, path); }

/* The interface and its language replaced (the main menu's languages, ADR 0016): the vector glyphs of the old one go
 * (the next movie reads the new language's subtitles itself). */
static void host_language(void *ctx)
{
	KpApp *a = (KpApp *)ctx;
	kp_vtext_free(&a->vtext);
	a->vtext_ui = NULL;
}

/* A mission's files: there unless its repack's group is not open yet (the web's, fetched beside the page: kp_app_group);
 * a test scenario is the converted data's. */
static int host_mission_files(void *ctx, const KpDataMission *m, char *err, size_t err_size)
{
	KpApp *a = (KpApp *)ctx;
	char prefix[sizeof m->directory + 1], file[KP_APP_GROUP_FILE];
	if (!m->directory[0]) return 1;
	snprintf(prefix, sizeof prefix, "%s\\", m->directory);
	if (!kp_res_group_wanted(a->res, prefix, file, sizeof file)) return 1;
	return kp_app_group(a, file, err, err_size);
}

/* A language's files: there unless its repack's group is not open yet (the web's, fetched beside the page: kp_app_group;
 * without fetch, 0 tells there is one to fetch) */
static int host_language_files(void *ctx, const char *locale, int fetch, char *err, size_t err_size)
{
	KpApp *a = (KpApp *)ctx;
	char prefix[64], file[KP_APP_GROUP_FILE];
	snprintf(prefix, sizeof prefix, "locales/%s/", locale);
	if (!kp_res_group_wanted(a->res, prefix, file, sizeof file)) return 1;
	return fetch ? kp_app_group(a, file, err, err_size) : 0;
}

/* The flow's host, data and profile (once, before its languages are asked for or it starts) */
static void flow_setup(KpApp *a)
{
	KpFlow *f = &a->flow;
	KpFlowHost *h = &f->host;
	if (h->ctx == a) return;
	kp_app_load_profile(a);
	h->ctx = a;
	h->now = host_now;
	h->clock_ms = clock_ms;
	h->log = host_log;
	h->read = host_read;
	h->save_profile = host_save_profile;
	h->sync_files = host_sync_files;
	h->quit = host_quit;
	h->world_build = host_world_build;
	h->world_free = host_world_free;
	h->world_frame = host_world_frame;
	h->hud_map = host_hud_map;
	h->interface = host_interface;
	h->canvas_begin = host_canvas_begin;
	h->canvas = host_canvas;
	h->cursor = host_cursor;
	h->pointer = host_pointer;
	h->sounds = host_sounds;
	h->stop_sounds = host_stop_sounds;
	h->palette = host_palette;
	h->gamma = host_gamma;
	h->video_available = host_video_available;
	h->cine_has = host_cine_has;
	h->cine_start = host_cine_start;
	h->cine_active = host_cine_active;
	h->exists = host_exists;
	h->language = host_language;
	h->mission_files = host_mission_files;
	h->language_files = host_language_files;
	f->flags = &a->flags;
	kp_app_pick_setup(a);
	f->data = &a->data;
	f->identity = &a->identity;
	f->console_art = a->console_art;
	f->console_art_size = a->console_art_size;
	f->profile = &a->profile;
	f->pref = a->pref;
#ifdef __EMSCRIPTEN__
	f->exit_to_menu = 1; /* the page goes on with the main menu */
#endif
}

int kp_app_flow_languages(KpApp *a)
{
	flow_setup(a);
	return kp_flow_languages_ready(&a->flow);
}

int kp_app_flow_init(KpApp *a, char *err, size_t err_size)
{
	flow_setup(a);
	return kp_flow_init(&a->flow, err, err_size);
}

void kp_app_flow_frame(KpApp *a)
{
	kp_app_pick_frame(a);
	kp_flow_frame(&a->flow);
}

void kp_app_flow_free(KpApp *a)
{
	kp_flow_free(&a->flow);
	kp_app_pick_free();
	kp_vtext_free(&a->vtext);
	a->vtext_ui = NULL;
	SDL_free(a->vtext_file);
	a->vtext_file = NULL;
}
