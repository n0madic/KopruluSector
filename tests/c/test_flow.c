/*
 * The product's flow (native/kp_app/kp_flow.c) over a host that records what it is asked and a clock the tests move,
 * on the original data (kp_t_data: a repack or an install): the glue's games from their ready rooms to the console, the game's ends (the result, the
 * in-game menus' exits) and the campaign's next game, the cinematics between them, the saves, the video options and
 * the view. The world, the sound and the drawing are the host's (sdl/kp_app_flow.c in the product).
 */
#include "kp_test_app.h"
#include "kp_test_ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <sys/utime.h>
#else
#include <fcntl.h>
#include <unistd.h>
#include <utime.h>
#ifndef __EMSCRIPTEN__
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#endif
#endif

#include "kp_flow.h"
#include "kp_gametype.h"
#include "kp_ids.h"
#include "kp_mpq.h"
#include "kp_net_lobby.h"
#include "kp_netplay.h"
#include "kp_netsave.h"
#include "kp_oracle.h"
#include "kp_rep.h"
#include "kp_script_flow.h"

#define FRAME_S 0.016            /* a frame of the host's clock */
#define SCREEN_W 1280            /* the drawable (its safe area the whole of it) */
#define SCREEN_H 960
#define MENU_FRAMES 2000         /* a glue screen's slide-in */
#define ROOM_FRAMES 4000         /* a ready room's establishing text (25 s), then its dialog */
#define SETTLE_FRAMES 150        /* a dialog's slide and animations */
#define VERSION_LABEL 10         /* the main menu's version label: its text ours (defs/ui/koprulu-dialogs.json's labels) */
#define LANGUAGE_BUTTON 11       /* the main menu's Language (ours), koprulu\language.bin's OK, Cancel, text, speech */
#define LANGUAGE_OK 1
#define LANGUAGE_CANCEL 3
#define LANGUAGE_TEXT 5
#define LANGUAGE_SPEECH 7
#define END_FRAMES 3000          /* a screen's slide-out to its end */
#define PROTOSS_ROOM 9           /* a ready room's glue state (Terran 7, Zerg 8, Protoss 9) */
#define PROTOSS_ROOM_BIN "rez\\glurdyp.bin"
#define SCORE_CONTINUE 7         /* gluscore.bin's Continue */
#define KEY_ESCAPE 0x1b
#define RESULT_FRAMES 600       /* the result dialog's timer (TickResultDialogTimer 0x4be340) on the host's clock */
#define GAME_SECONDS 75          /* a game's length the score screen tells */
#define RESULT_VICTORY KP_CAMPAIGN_RESULT_VICTORY
#define EXPANSION_PROTOSS01 "expprotoss/protoss01"
#define EXPANSION_PROTOSS02 "expprotoss/protoss02"
#define PROTOSS01 "protoss/protoss01"
/* a Zerg mission history's cinematic row (test_campaign.cinematic_rows_go_on_from_their_row) */
#define HISTORY_ROW 15
#define HISTORY_CINEMATIC 13
#define NO_ROW 9999              /* a mission index of no campaign table */
#define ZERG_ROOM 8
#define CINE_IDS 64
#define VIEW_ROWS_ORIGINAL 400   /* the original's game view: 640 x 400 */

typedef struct {
	KpT *T;
	KpFlow f;
	KpAppFlags flags;
	const KpData *data;
	KpProfile profile;
	KpSaveIdentity identity;
	KpTileset tileset;
	int has_tileset;
	char pref[256];          /* the saves' folder, the test's own (kp_t_temp_dir), and '/' */
	double now;
	/* what the host was asked */
	int quits, profile_saves, syncs, builds, frees, world_frames, ticks, restarts, stops, palettes_set, interfaces;
	int music_track, music_playing; /* the music as the host's mixer has it: the last track asked for, still on */
	int32_t gamma;
	int renderer, renderers; /* the renderer the host was last told to take, and how many times */
	int world;               /* a world stands */
	int fail_world;          /* the next world's build fails */
	int fail_game;           /* the next world's frame fails the game */
	int pack;                /* the host's cinematics' pack holds every movie */
	int movie;               /* a movie plays */
	int cine_starts, cine_count, cine_preludes, cine_ids[CINE_IDS];
	uint8_t available;
	char last_log[512];
	char saves[8][400];      /* the save files the test wrote */
	int save_count;
	KpData language_data;    /* the data with StarCraft: Remastered's ruRU files (with_language) */
	KpFiles install_raw, language_raw;
	int no_speech;           /* with_language's ruRU without its speech */
	int no_language;         /* every language's files hidden (without_language) */
	int language_changes;
	int files_wait;          /* a mission's files: on their way this many more asks (the web's repack group) */
	int files_fail;          /* then they cannot be had */
	int files_asks;
	int language_away;       /* with_language's ruRU behind a group not there yet (the web's): this many more asks */
	int language_asks;
	int picks;               /* Import: the pickers asked for, */
	int pick_kind;           /* the last one's kind (KP_FLOW_PICK_*) */
	int exports;             /* its Export: the last one's name and bytes (malloc'd) */
	char export_name[64];
	uint8_t *export_bytes;
	size_t export_size;
	const char *watch;       /* the log's lines that hold this text are counted in watched (NULL: none) */
	int watched;
	int cursor_shown;        /* the interface's cursor drawn by the last frame */
} Host;

/* --- the host --- */

static double host_now(void *ctx) { return ((Host *)ctx)->now; }

static void host_log(void *ctx, const char *line)
{
	Host *H = (Host *)ctx;
	snprintf(H->last_log, sizeof H->last_log, "%s", line);
	if (H->watch && strstr(line, H->watch)) H->watched++;
}

static uint8_t *host_read(void *ctx, const char *path, size_t *size)
{
	(void)ctx;
	return kp_read_file(path, size);
}

static int host_exists(void *ctx, const char *path)
{
	FILE *file = fopen(path, "rb");
	(void)ctx;
	if (file) fclose(file);
	return file != NULL;
}

static void host_language(void *ctx) { ((Host *)ctx)->language_changes++; }

static void host_save_profile(void *ctx) { ((Host *)ctx)->profile_saves++; }

static void host_sync_files(void *ctx) { ((Host *)ctx)->syncs++; }

static void host_quit(void *ctx) { ((Host *)ctx)->quits++; }

static void host_pick_file(void *ctx, int kind)
{
	((Host *)ctx)->picks++;
	((Host *)ctx)->pick_kind = kind;
}

static void host_export_replay(void *ctx, const char *name, const uint8_t *bytes, size_t size)
{
	Host *H = (Host *)ctx;
	H->exports++;
	snprintf(H->export_name, sizeof H->export_name, "%s", name);
	free(H->export_bytes);
	H->export_bytes = (uint8_t *)malloc(size ? size : 1);
	if (H->export_bytes) memcpy(H->export_bytes, bytes, size);
	H->export_size = size;
}

static int host_mission_files(void *ctx, const KpDataMission *m, char *err, size_t err_size)
{
	Host *H = (Host *)ctx;
	(void)m;
	H->files_asks++;
	if (H->files_wait > 0) {
		H->files_wait--;
		return 0;
	}
	if (H->files_fail) {
		snprintf(err, err_size, "its group cannot be fetched");
		return -1;
	}
	return 1;
}

/* A language's files: ruRU's while language_away are a group's on its way, there after that many asks */
static int host_language_files(void *ctx, const char *locale, int fetch, char *err, size_t err_size)
{
	Host *H = (Host *)ctx;
	(void)err;
	(void)err_size;
	if (H->language_away == 0 || strcmp(locale, "ruRU") != 0) return 1;
	if (!fetch) return 0;
	H->language_asks++;
	return --H->language_away == 0;
}

/* The world of the game started: only its tileset, for the console's map. */
static int host_world_build(void *ctx, char *err, size_t err_size)
{
	Host *H = (Host *)ctx;
	const KpMission *m = kp_session_mission(H->f.session);
	KpError e;
	H->builds++;
	if (H->fail_world) {
		H->fail_world = 0;
		snprintf(err, err_size, "the test's world does not build");
		return -1;
	}
	if (kp_tileset_from_files(&H->tileset, &H->data->files, (int)(m->tileset & 7), &e) != 0) {
		snprintf(err, err_size, "%s", e.msg);
		return -1;
	}
	H->has_tileset = 1;
	H->world = 1;
	return 0;
}

static void host_world_free(void *ctx)
{
	Host *H = (Host *)ctx;
	H->frees++;
	H->world = 0;
	if (H->has_tileset) kp_tileset_free(&H->tileset);
	H->has_tileset = 0;
}

static void host_world_frame(void *ctx, int ticks, double fraction, int restart)
{
	Host *H = (Host *)ctx;
	(void)fraction;
	H->world_frames++;
	H->ticks += ticks;
	H->restarts += restart;
	if (H->fail_game) {
		H->fail_game = 0;
		kp_game_fail(H->f.session->g, "the test's failure");
	}
}

static void host_hud_map(void *ctx, KpHudMap *m)
{
	Host *H = (Host *)ctx;
	const KpMission *mission = kp_session_mission(H->f.session);
	m->tileset = &H->tileset;
	m->palettes = &H->data->palettes;
	m->tileset_index = (int)(mission->tileset & 7);
	m->colors = mission->has_colors ? mission->colors : NULL;
}

static void host_interface(void *ctx, KpUi *ui)
{
	(void)ui;
	((Host *)ctx)->interfaces++;
}

static void host_canvas(void *ctx, KpCanvas *c)
{
	(void)ctx;
	(void)c;
}

static void host_cursor(void *ctx, KpUi *ui) { ((Host *)ctx)->cursor_shown = ui != NULL; }

static int host_pointer(void *ctx, int32_t *x, int32_t *y)
{
	(void)ctx;
	*x = *y = 0;
	return KP_HUD_POINTER_NONE;
}

/* The music as the SDL host's mixer plays it: a track's command starts it (the one before dropped), a stop ends it;
 * stop_sounds leaves it to the interface. */
static void host_sounds(void *ctx, const KpUiSoundCommand *c, size_t n)
{
	Host *H = (Host *)ctx;
	size_t k;
	for (k = 0; k < n; k++) {
		if (c[k].type == KP_UI_SOUND_MUSIC) {
			H->music_track = c[k].sound;
			H->music_playing = 1;
		} else if (c[k].type == KP_UI_SOUND_MUSIC_STOP) {
			H->music_playing = 0;
		}
	}
}

static void host_stop_sounds(void *ctx) { ((Host *)ctx)->stops++; }

static void host_palette(void *ctx, const uint8_t *rgb)
{
	(void)rgb;
	((Host *)ctx)->palettes_set++;
}

static void host_gamma(void *ctx, int32_t gamma) { ((Host *)ctx)->gamma = gamma; }

static void host_renderer(void *ctx, int renderer)
{
	Host *H = (Host *)ctx;
	H->renderer = renderer;
	H->renderers++;
}

static uint8_t host_video_available(void *ctx, const KpUi *ui)
{
	(void)ui;
	return ((Host *)ctx)->available;
}

static int host_cine_has(void *ctx, int id)
{
	(void)id;
	return ((Host *)ctx)->pack;
}

static int host_cine_start(void *ctx, const int *ids, int n, int preludes)
{
	Host *H = (Host *)ctx;
	int k;
	if (!H->pack) return 0;
	H->cine_starts++;
	H->cine_count = n < CINE_IDS ? n : CINE_IDS;
	for (k = 0; k < H->cine_count; k++) H->cine_ids[k] = ids[k];
	H->cine_preludes = preludes;
	H->movie = 1;
	return 1;
}

static int host_cine_active(void *ctx) { return ((Host *)ctx)->movie; }

/* --- the data and the flow over them --- */

/* The file of the logged-in ID's save name. */
static void save_path(const Host *H, const char *name, char *out, size_t size)
{
	char file[KP_SAVE_FILE_NAME];
	kp_save_file_name(kp_profile_save_owner(&H->profile), name, file, sizeof file);
	snprintf(out, size, "%s%s", H->pref, file);
}

/* The flow ended and what it loaded released, before the saves' folder is removed (deferred after it: run first). */
static void release_host(void *p)
{
	Host *H = (Host *)p;
	kp_flow_free(&H->f);
	free(H->export_bytes);
	if (H->has_tileset) kp_tileset_free(&H->tileset);
}

/* A host and its flow over the original data, not started (its flags and profile the test's to set); skips the test
 * without the data (a public checkout's). Released when the test ends. */
static Host *new_host(KpT *T)
{
	static const char *const NEED[] = { KP_T_DATA, "missions/" EXPANSION_PROTOSS01, "missions/" EXPANSION_PROTOSS02,
		"missions/" PROTOSS01, NULL };
	Host *H;
	KpFlow *f;
	KpFlowHost *h;
	kp_t_need(T, NEED);
	H = (Host *)calloc(1, sizeof *H);
	kp_t_ok(T, H != NULL, "out of memory");
	kp_t_defer(T, free, H);
	H->T = T;
	H->now = 1;
	f = &H->f;
	H->data = kp_t_data(T);
	/* the saves in a directory of the test's own: two runs of the suite at once share no file */
	snprintf(H->pref, sizeof H->pref, "%s/", kp_t_temp_dir(T, NULL));
	kp_t_defer(T, release_host, H);
	kp_save_identity(&H->identity, &H->data->files, &H->data->dat);
	kp_profile_init(&H->profile);
	h = &f->host;
	h->ctx = H;
	h->now = host_now;
	h->clock_ms = NULL;
	h->log = host_log;
	h->read = host_read;
	h->save_profile = host_save_profile;
	h->sync_files = host_sync_files;
	h->quit = host_quit;
	h->mission_files = host_mission_files;
	h->language_files = host_language_files;
	h->pick_file = host_pick_file;
	h->export_replay = host_export_replay;
	h->world_build = host_world_build;
	h->world_free = host_world_free;
	h->world_frame = host_world_frame;
	h->hud_map = host_hud_map;
	h->interface = host_interface;
	h->canvas_begin = host_canvas;
	h->canvas = host_canvas;
	h->cursor = host_cursor;
	h->pointer = host_pointer;
	h->sounds = host_sounds;
	h->stop_sounds = host_stop_sounds;
	h->palette = host_palette;
	h->gamma = host_gamma;
	h->video_available = host_video_available;
	h->gpu_name = "Vulkan";
	h->renderer = host_renderer;
	h->cine_has = host_cine_has;
	h->cine_start = host_cine_start;
	h->cine_active = host_cine_active;
	h->exists = host_exists;
	h->language = host_language;
	f->flags = &H->flags;
	f->data = H->data;
	f->identity = &H->identity;
	f->profile = &H->profile;
	f->pref = H->pref;
	f->safe_w = f->full_w = SCREEN_W;
	f->safe_h = f->full_h = SCREEN_H;
	kp_flow_picture(f);
	return H;
}

static void frame(Host *H)
{
	H->now += FRAME_S;
	kp_flow_frame(&H->f);
}

static void frames(Host *H, int n)
{
	while (n-- > 0) frame(H);
}

static void init(Host *H)
{
	char err[512];
	kp_t_ok(H->T, kp_flow_init(&H->f, err, sizeof err) == 0, "the flow: %s", err);
}

static const char *top(const Host *H, char *name, size_t size)
{
	return H->f.ui->open >= 0 ? kp_t_ui_bin(H->f.ui, H->f.ui->open, name, size) : "";
}

/* Frames until the dialog bin is on top (then its slide and animations). */
static void wait_for(Host *H, const char *bin, int n)
{
	char name[128];
	int k;
	for (k = 0; k < n; k++) {
		frame(H);
		if (strcmp(top(H, name, sizeof name), bin) == 0) {
			frames(H, SETTLE_FRAMES);
			return;
		}
	}
	kp_t_fail(H->T, "glue: no %s (at %s; %s)", bin, top(H, name, sizeof name), H->last_log);
}

/* The flow at the glue's main menu, with the cinematics' pack or none. */
static Host *glue_host(KpT *T, int pack)
{
	Host *H = new_host(T);
	H->pack = pack;
	H->profile.options &= ~KP_UI_OPTION_EXPANSION_INTRO; /* a profile past its first start */
	init(H);
	kp_t_ok(T, H->f.glue_on, "the glue");
	wait_for(H, "rez\\glumain.bin", MENU_FRAMES);
	return H;
}

static int mission_row(const Host *H, const char *name)
{
	int k;
	for (k = 0; k < H->data->mission_count; k++) {
		if (strcmp(H->data->missions[k].name, name) == 0) return (int)H->data->missions[k].index;
	}
	kp_t_fail(H->T, "no mission %s in the catalog", name);
}

/* The data's mission of a name. */
static const KpDataMission *mission_entry(const Host *H, const char *name)
{
	int k;
	for (k = 0; k < H->data->mission_count; k++) {
		if (strcmp(H->data->missions[k].name, name) == 0) return &H->data->missions[k];
	}
	kp_t_fail(H->T, "no mission %s in the data", name);
}

static const char *current(const Host *H)
{
	if (H->f.current == KP_FLOW_CUSTOM) return H->f.custom_entry.name;
	return H->f.current >= 0 ? H->data->missions[H->f.current].name : "";
}

/* What a menu screen leaves the host (the menus' end: KP_UI_MENU_*), taken by the next frame. */
static void menus_end(Host *H, int action, int state, int mission, int cinematic, int expansion)
{
	KpUiMenus *m = &H->f.ui->menus;
	m->screen = -1;
	m->action = action;
	m->state = state;
	m->mission = (uint16_t)mission;
	m->cinematic = (uint16_t)cinematic;
	m->expansion = expansion;
	frame(H);
}

/* A key typed: its press, its character (a hotkey's, as text; none for a control key) and its release. */
static void key(Host *H, char ch)
{
	char text[2] = { ch, '\0' };
	uint16_t vk = (uint16_t)(ch >= 'a' && ch <= 'z' ? ch - 32 : ch);
	kp_hud_key(H->f.ui, 1, vk, 0);
	if (ch >= ' ') kp_hud_text(H->f.ui, text);
	kp_hud_key(H->f.ui, 0, vk, 0);
}

/* A press and release at the centre of the top dialog's control id. */
static void click(Host *H, int id)
{
	const KpUi *ui = H->f.ui;
	const KpUiControl *d, *k;
	int32_t c, x, y;
	kp_t_ok(H->T, ui->open >= 0, "glue: no dialog");
	d = &ui->controls[ui->open];
	for (c = d->child; c >= 0 && ui->controls[c].id != id; c = ui->controls[c].next) {}
	kp_t_ok(H->T, c >= 0, "glue: no control %d", id);
	k = &ui->controls[c];
	x = H->f.glue.place_x + d->left + (k->left + k->right) / 2;
	y = H->f.glue.place_y + d->top + (k->top + k->bottom) / 2;
	kp_glue_input(&H->f.glue, KP_UI_MOUSE_MOVE, x, y);
	frame(H);
	kp_glue_input(&H->f.glue, KP_UI_LEFT_DOWN, x, y);
	frame(H);
	kp_glue_input(&H->f.glue, KP_UI_LEFT_UP, x, y);
	frame(H);
}

/* The glue's campaign mission name started from its menu: its ready room. */
static void start_from_menu(Host *H, const char *name)
{
	menus_end(H, KP_UI_MENU_MISSION, PROTOSS_ROOM, mission_row(H, name), 0, strncmp(name, "exp", 3) == 0);
	kp_t_eq_str(H->T, current(H), name, "the mission started");
	kp_t_ok(H->T, H->f.playing && H->f.session && H->world, "its game and world stand");
	kp_t_eq(H->T, H->f.glue.screen, KP_GLUE_ROOM, "its ready room");
	kp_t_eq(H->T, H->f.hud_on, 0, "no console in the room");
}

/* The ready room's Start: the console. */
static void room_start(Host *H)
{
	int k;
	wait_for(H, PROTOSS_ROOM_BIN, ROOM_FRAMES);
	key(H, 's');
	for (k = 0; k < END_FRAMES && !H->f.hud_on; k++) frame(H);
	kp_t_ok(H->T, H->f.hud_on, "the room's Start: the console (%s)", H->last_log);
}

/* The glue's game name in its console. */
static void play(Host *H, const char *name)
{
	start_from_menu(H, name);
	room_start(H);
}

/* A game event the next frame takes. */
static void game_event(Host *H, int64_t kind, int64_t a, const char *text)
{
	kp_event(H->f.session->g, kind, a, 0, 0, text);
	frame(H);
}

/* RunGlueLoop 0x457510's Sleep after a game (kp_glue_sleep): KP_UI_GLUE_SLEEP_MS of the host's clock, black to its
 * last frame (the next screen from the frame after), no cursor, the input dropped; then what the game's end leads to. */
static void glue_sleep(Host *H)
{
	double from = H->now;
	int k, x = H->f.glue.place_x + 320, y = H->f.glue.place_y + 240;
	kp_t_eq(H->T, H->f.glue.screen, KP_GLUE_SLEEP, "the glue's Sleep after the game");
	for (k = 0; k < END_FRAMES && H->f.glue.screen == KP_GLUE_SLEEP; k++) {
		uint8_t index, alpha;
		int32_t px, py;
		kp_glue_input(&H->f.glue, KP_UI_LEFT_DOWN, x, y); /* dropped */
		kp_glue_input(&H->f.glue, KP_UI_LEFT_UP, x, y);
		kp_glue_key(&H->f.glue, 1, 0x1b, 0);
		kp_glue_key(&H->f.glue, 0, 0x1b, 0);
		frame(H);
		if (H->f.glue.screen == KP_GLUE_SLEEP && H->cursor_shown) kp_t_fail(H->T, "a cursor over the Sleep's screen");
		for (py = 0; py < H->f.glue.canvas.h; py += 7)
			for (px = 0; px < H->f.glue.canvas.w; px += 7) {
				kp_canvas_pixel(&H->f.glue.canvas, px, py, &index, &alpha);
				if (index != 0) kp_t_fail(H->T, "the Sleep's screen is black: index %u at %d, %d", index, px, py);
			}
	}
	kp_t_ok(H->T, H->now - from >= KP_UI_GLUE_SLEEP_MS / 1000.0 && H->now - from < KP_UI_GLUE_SLEEP_MS / 1000.0 + 3 * FRAME_S,
		"the Sleep stands %u ms (%.3f s)", (unsigned)KP_UI_GLUE_SLEEP_MS, H->now - from);
}

static void track_save(Host *H, const char *name)
{
	if (H->save_count < (int)(sizeof H->saves / sizeof H->saves[0])) {
		save_path(H, name, H->saves[H->save_count++], sizeof H->saves[0]);
	}
}

/* A save or a load the game asks for: its event taken by the next frame, the request served by the frame after it
 * (before the game's part of the frame, as a load the glue started is). */
static void request(Host *H, int64_t kind, const char *name)
{
	game_event(H, kind, 0, name);
	frame(H);
}

/* The game's save name, written. */
static void save(Host *H, const char *name)
{
	char line[128];
	track_save(H, name);
	request(H, KP_EVENT_SAVE_GAME, name);
	snprintf(line, sizeof line, "saved %s", name);
	kp_t_eq_str(H->T, H->last_log, line, "the save");
}

static void in_menus(const Host *H, int state, const char *what)
{
	kp_t_eq(H->T, H->f.glue.screen, KP_GLUE_MENUS, "%s: the menus", what);
	kp_t_eq(H->T, H->f.ui->menus.state, state, "%s: the menus' state", what);
	kp_t_ok(H->T, !H->f.playing && H->f.session == NULL && !H->world, "%s: no game, session or world", what);
}

/* --- the tests --- */

/* The catalog: the campaign's missions by mapdata index from 1 (kp_campaign_mission_name), then the scenarios of the
 * index text, a scenario's row its base map's and its packs its line's. */
static void missions_number_the_catalog_and_place_the_scenarios(KpT *T)
{
	static const char SCN[] = "marine_group terran/terran01 0 64 64 terran,neutral\nnowhere zerg/zerg99 0 64 64 -\n\n"
		"bad line\n";
	const KpData *d = kp_t_data(T);
	const KpDataMission *terran01, *group, *nowhere;
	KpData *c = (KpData *)malloc(sizeof *c);
	int k, campaign = 0;
	kp_t_ok(T, c != NULL, "out of memory");
	kp_t_defer(T, free, c);
	*c = *d; /* the same data, another catalog */
	kp_data_catalog(c, SCN, sizeof SCN - 1);
	for (k = 0; k < c->mission_count && c->missions[k].catalog; k++) {
		const KpDataMission *m = &c->missions[k];
		kp_t_eq(T, m->index, k + 1, "%s: its mapdata index", m->key);
		kp_t_eq_str(T, m->directory, d->campaign.directories[k + 1], "%s: its directory", m->key);
		kp_t_ok(T, strncmp(m->key, "missions/", 9) == 0 && strcmp(m->key + 9, m->name) == 0, "%s: its key", m->name);
		campaign++;
	}
	kp_t_eq(T, campaign, 64, "the campaign's missions");
	kp_t_eq(T, c->mission_count, campaign + 2, "two scenarios, the line that is none skipped");
	terran01 = kp_data_find(c, "missions/terran/terran01");
	group = kp_data_find(c, "scenarios/marine_group");
	nowhere = kp_data_find(c, "scenarios/nowhere");
	kp_t_ok(T, terran01 && group && nowhere, "found by key");
	kp_t_eq_str(T, group->name, "scenarios/marine_group", NULL);
	kp_t_ok(T, !group->catalog && !group->directory[0], "a scenario: its CHK");
	kp_t_eq(T, group->index, terran01->index, "its base's row");
	kp_t_eq_str(T, group->packs, "terran,neutral", NULL);
	kp_t_eq(T, nowhere->index, 0, "a base outside the catalog: no row");
	kp_t_eq_str(T, nowhere->packs, "", "\"-\": no packs");
	kp_t_ok(T, kp_data_find(c, "terran/terran01") == NULL, "a name is no key");
}

/* The music's tracks (the MUSI rows): the title's, the protoss room's, the protoss games' three. */
enum { MUSIC_TITLE = 22, MUSIC_PROTOSS_ROOM = 12, MUSIC_PROTOSS_GAME_FIRST = 7, MUSIC_PROTOSS_GAME_LAST = 9 };

static void music_is(Host *H, int first, int last, const char *what)
{
	kp_t_ok(H->T, H->music_playing && H->music_track >= first && H->music_track <= last,
		"%s: the music plays track %d..%d (track %d, %s)", what, first, last, H->music_track,
		H->music_playing ? "playing" : "stopped");
}

/* A campaign mission of the glue waits in its ready room (its game paused, the glue's music stopped); Start brings
 * the console of the same interface and runs the game on the host's clock. */
static void room_start_plays_the_game_in_the_glues_console(KpT *T)
{
	Host *H = glue_host(T, 0);
	KpUi *ui;
	int stops, ticks;
	ui = H->f.ui;
	stops = H->stops;
	start_from_menu(H, EXPANSION_PROTOSS01);
	kp_t_ok(T, H->stops > stops, "the glue's sounds stopped for the game's");
	kp_t_ok(T, H->f.has_run && H->f.run.mission == mission_row(H, EXPANSION_PROTOSS01), "a run of its campaign");
	kp_t_eq(T, H->f.glue_room, PROTOSS_ROOM, "the room's state kept for the game's restart");
	frames(H, 50);
	kp_t_eq(T, H->ticks, 0, "no steps in the room");
	music_is(H, MUSIC_PROTOSS_ROOM, MUSIC_PROTOSS_ROOM, "the room");
	room_start(H);
	frame(H);
	music_is(H, MUSIC_PROTOSS_GAME_FIRST, MUSIC_PROTOSS_GAME_LAST, "the game (RunGame stops the room's)");
	kp_t_ok(T, H->f.ui == ui, "the glue's interface runs the console");
	kp_t_eq(T, kp_flow_glue_active(&H->f), 0, "no glue screen over the game");
	kp_t_eq(T, H->f.fixed_screen, 0, "the game's screen");
	ticks = H->ticks;
	frames(H, 100);
	kp_t_ok(T, H->ticks > ticks, "the game steps on the clock");
	kp_t_eq(T, H->quits, 0, NULL);
}

static void canvas_free(void *c)
{
	kp_ui_canvas_free((KpUiCanvas *)c);
	free(c);
}

/* The briefing's text (control -10: drawn into a bitmap of its own, then its rows copied into the room's dialog as it
 * scrolls) brings its glyphs along: with the vector text on, the canvas lists them as any other text's (ADR 0011). They
 * stayed bitmaps, a language's supplement among them, at every scale. */
static void briefing_text_is_vector_text(KpT *T)
{
	enum { TEXT_ID = -10, TEXT_FONT = 1, DIALOG_W = 640, DIALOG_H = 480, GLYPHS_MIN = 10 };
	Host *H = glue_host(T, 0);
	KpUi *ui;
	KpUiCanvas *c;
	const KpUiControl *text = NULL;
	uint8_t codes[256];
	int32_t k, id;
	int n, inside = 0;
	ui = H->f.ui;
	memset(codes, 1, sizeof codes);
	kp_ui_set_vector_text(ui, 1, 1, codes);
	start_from_menu(H, PROTOSS01);
	wait_for(H, PROTOSS_ROOM_BIN, ROOM_FRAMES);
	for (n = 0; n < ROOM_FRAMES && text == NULL; n++) {
		id = kp_ui_find_control(ui, ui->open, TEXT_ID);
		if (id >= 0 && (ui->controls[id].flags & KP_UI_VISIBLE)) text = &ui->controls[id];
		else frame(H);
	}
	kp_t_ok(T, text != NULL, "the briefing's text shown");
	if (text == NULL) return;
	frames(H, SETTLE_FRAMES);
	kp_ui_paint_dialog(ui, ui->open);
	c = (KpUiCanvas *)calloc(1, sizeof *c);
	kp_t_ok(T, c != NULL, "out of memory");
	kp_t_defer(T, canvas_free, c);
	kp_t_ok(T, kp_ui_canvas_resize(c, DIALOG_W, DIALOG_H) == 0, "out of memory");
	kp_ui_canvas_begin(ui, c);
	kp_ui_canvas_screen(ui, c, ui->open, 0, 0);
	kp_ui_canvas_text_end(ui, c);
	kp_ui_canvas_end(c);
	for (k = 0; k < c->glyph_count; k++) {
		const KpUiCanvasGlyph *g = &c->glyphs[k];
		if (g->font == ui->fonts[TEXT_FONT] && g->x >= text->left && g->x <= text->right && g->y >= text->top
			&& g->y <= text->bottom) {
			inside++;
		}
	}
	kp_t_ok(T, inside >= GLYPHS_MIN, "the text's glyphs on the canvas: %d", inside);
	kp_ui_set_vector_text(ui, 0, 0, NULL);
}

/* The vector glyphs of the canvas inside control `text` from row `top` down, composed as the app does each frame. */
static int composed_glyphs_inside(KpUi *ui, KpUiCanvas *c, const KpUiControl *text, int font, int32_t top)
{
	int32_t k;
	int inside = 0;
	kp_ui_paint_dialog(ui, ui->open);
	kp_ui_canvas_begin(ui, c);
	kp_ui_canvas_screen(ui, c, ui->open, 0, 0);
	kp_ui_canvas_text_end(ui, c);
	kp_ui_canvas_end(c);
	for (k = 0; k < c->glyph_count; k++) {
		const KpUiCanvasGlyph *g = &c->glyphs[k];
		inside += g->font == ui->fonts[font] && g->x >= text->left && g->x <= text->right && g->y >= top
			&& g->y <= text->bottom;
	}
	return inside;
}

/* A transcript longer than its control waits 4 s, then scrolls (kp_ui_briefing.c's text_show): its rows that come into
 * view still bring their glyphs. The bitmap they are drawn into is never composed, and the log dropped its glyphs after
 * 120 composes of the wait, so the rows scrolled in were the bitmap's pixels (FNT glyphs; the boxes of 0x7f..0xa0 among
 * them) in a frame of vector text. */
static void scrolled_briefing_text_keeps_its_glyphs(KpT *T)
{
	enum { TEXT_ID = -10, TEXT_FONT = 1, DIALOG_W = 640, DIALOG_H = 480, GLYPHS_MIN = 10, SCROLL_FRAMES = 6000 };
	Host *H = glue_host(T, 0);
	KpUi *ui;
	KpUiCanvas *c;
	const KpUiControl *text = NULL;
	uint8_t codes[256];
	int32_t id, height;
	int n, inside = 0, scrolled = 0, most = 0;
	ui = H->f.ui;
	memset(codes, 1, sizeof codes);
	kp_ui_set_vector_text(ui, 1, 1, codes);
	c = (KpUiCanvas *)calloc(1, sizeof *c);
	kp_t_ok(T, c != NULL, "out of memory");
	if (c == NULL) return;
	kp_t_defer(T, canvas_free, c);
	kp_t_ok(T, kp_ui_canvas_resize(c, DIALOG_W, DIALOG_H) == 0, "out of memory");
	start_from_menu(H, PROTOSS01);
	wait_for(H, PROTOSS_ROOM_BIN, ROOM_FRAMES);
	for (n = 0; n < ROOM_FRAMES && text == NULL; n++) {
		id = kp_ui_find_control(ui, ui->open, TEXT_ID);
		if (id >= 0 && (ui->controls[id].flags & KP_UI_VISIBLE)) text = &ui->controls[id];
		else frame(H);
	}
	kp_t_ok(T, text != NULL, "the briefing's text shown");
	if (text == NULL) return;
	height = text->bottom - text->top + 1;
	/* until half a control's height of new rows scrolled in (its lines' glyphs), a compose each frame */
	for (n = 0; n < SCROLL_FRAMES && !scrolled; n++) {
		(void)composed_glyphs_inside(ui, c, text, TEXT_FONT, text->top);
		frame(H);
		if (text->wuser > most) most = text->wuser;
		scrolled = text->wuser >= height / 2;
	}
	kp_t_ok(T, scrolled, "a transcript scrolled by half its control's height (%d rows) in %d frames: %d", (int)height / 2, n, most);
	/* the rows scrolled in: the control's lower part */
	if (scrolled) inside = composed_glyphs_inside(ui, c, text, TEXT_FONT, text->bottom + 1 - text->wuser);
	kp_t_ok(T, inside >= GLYPHS_MIN, "the glyphs of the rows scrolled in on the canvas: %d", inside);
	kp_ui_set_vector_text(ui, 0, 0, NULL);
}

/* The ready room's Cancel (Escape) leaves no game behind: the campaign menu of the run's campaign, and no LastReplay
 * (the game never reached its console). */
static void room_cancel_leaves_no_game(KpT *T)
{
	Host *H = glue_host(T, 0);
	char path[400];
	FILE *last;
	int k;
	start_from_menu(H, EXPANSION_PROTOSS01);
	wait_for(H, PROTOSS_ROOM_BIN, ROOM_FRAMES);
	key(H, KEY_ESCAPE);
	for (k = 0; k < END_FRAMES && H->f.playing; k++) frame(H);
	in_menus(H, KP_GLUE_EXPANSION_CAMPAIGN, "the room's Cancel");
	snprintf(path, sizeof path, "%sreplays/LastReplay.rep", H->pref);
	last = fopen(path, "rb");
	kp_t_ok(T, last == NULL, "no LastReplay of a game the room cancelled");
	if (last) fclose(last);
	kp_t_eq(T, H->frees >= 1, 1, "the world freed");
	kp_t_ok(T, H->f.ui->game == NULL, "the interface keeps no game");
	frame(H);
	music_is(H, MUSIC_TITLE, MUSIC_TITLE, "the room's Cancel (0x460ee0)");
	kp_t_eq(T, H->f.ui->music.track, MUSIC_TITLE, "the interface's track the host's");
}

/* The victory's answer: the campaign advances (the profile kept), the score screen, then its Continue starts the next
 * mission's ready room with the run the campaign advanced to. */
static void victory_goes_through_the_score_screen_to_the_next_game(KpT *T)
{
	Host *H = glue_host(T, 0);
	KpCampaignRun run;
	KpCampaignProfile progress;
	KpCampaignStep step;
	int64_t cinematics[KP_APP_STEP_CINEMATICS];
	int count = 0, saves, builds, k;
	play(H, EXPANSION_PROTOSS01);
	/* what the campaign gives for the game as it stands */
	run = H->f.run;
	progress = *kp_profile_progress(&H->profile);
	kp_campaign_after_game(&H->data->campaign, &run, &progress, RESULT_VICTORY,
		(const char *)H->f.session->g->trig.next_scenario, &step, cinematics, &count, KP_APP_STEP_CINEMATICS);
	kp_t_eq(T, step.kind, KP_CAMPAIGN_STEP_MISSION, "the campaign goes on");
	saves = H->profile_saves;
	kp_event(H->f.session->g, KP_EVENT_GAME_RESULT, RESULT_VICTORY, 0, 0, NULL);
	game_event(H, KP_EVENT_GAME_EXIT, KP_UI_EXIT_VICTORY, NULL);
	glue_sleep(H);
	kp_t_eq(T, H->f.hud_on, 0, "the console ended");
	kp_t_eq(T, H->f.glue.screen, KP_GLUE_SCORE, "the score screen");
	kp_t_ok(T, H->profile_saves > saves, "the profile kept");
	kp_t_ok(T, memcmp(kp_profile_progress(&H->profile), &progress, sizeof progress) == 0,
		"the profile's progress as the campaign's");
	kp_t_ok(T, H->f.has_next && H->f.next_mission == step.mission, "the next game");
	kp_t_eq(T, H->f.step_cinematic_count, count, "the cinematic rows passed");
	wait_for(H, "rez\\gluscore.bin", MENU_FRAMES);
	builds = H->builds;
	click(H, SCORE_CONTINUE);
	for (k = 0; k < END_FRAMES && H->builds == builds; k++) frame(H);
	kp_t_eq(T, H->f.run.mission, step.mission, "the run the campaign advanced to");
	kp_t_ok(T, memcmp(&H->f.run, &run, sizeof run) == 0, "the run as the campaign's");
	kp_t_eq(T, H->data->missions[H->f.current].index, step.mission, "the next mission");
	kp_t_eq(T, H->f.glue.screen, KP_GLUE_ROOM, "its ready room");
	kp_t_eq(T, H->f.has_continue, 0, "the continuation taken");
	kp_t_eq(T, H->cine_starts, 0, "no pack: no cinematics");
}

/* A won game the campaign has no step for leads from its score screen to the main menu (RunScoreScreen 0x461940: glue
 * state 0 when AdvanceCampaignAfterGame 0x4bb790 returns 0), not to the run's campaign menu nor to the step the game
 * before it ended the campaign with: a game without a run (a mission without a campaign row), and an expansion's run
 * whose row is in no table. */
static void victory_without_a_step_opens_the_main_menu(KpT *T)
{
	int part;
	for (part = 0; part < 2; part++) {
		Host *H = glue_host(T, 0);
		int k;
		play(H, part == 0 ? PROTOSS01 : EXPANSION_PROTOSS01);
		if (part == 0) {
			H->f.has_run = 0;
		} else {
			kp_t_ok(T, H->f.has_run && H->f.run.expansion != 0, "the expansion's run");
			H->f.run.row.table = -1;
			H->f.run.mission = NO_ROW;
		}
		H->f.step_glue = KP_GLUE_EXPANSION_CAMPAIGN; /* the end of the game before's campaign */
		kp_event(H->f.session->g, KP_EVENT_GAME_RESULT, RESULT_VICTORY, 0, 0, NULL);
		game_event(H, KP_EVENT_GAME_EXIT, KP_UI_EXIT_VICTORY, NULL);
		glue_sleep(H);
		kp_t_eq(T, H->f.glue.screen, KP_GLUE_SCORE, "the score screen");
		kp_t_eq(T, H->f.has_next, 0, "no next game");
		kp_t_eq(T, H->f.step_glue, KP_GLUE_MAIN, "no step of its own");
		wait_for(H, "rez\\gluscore.bin", MENU_FRAMES);
		click(H, SCORE_CONTINUE);
		for (k = 0; k < END_FRAMES && H->f.glue.screen != KP_GLUE_MENUS; k++) frame(H);
		in_menus(H, KP_GLUE_MAIN, part == 0 ? "without a run" : "a run without its row");
	}
}

/* A game the trigger system ends (the cheats' victory, then their defeat) reaches its score screen with every player:
 * the computers ApplyPendingResults 0x4be020 retired when the human won (11) and the human it retired when they lost
 * (10), as CaptureScoreRoster 0x461e00 lists them; the elapsed time the game's, not less the interface's whole clock
 * (the result's pause kp_core took had no tick of the interface's, issue #4). */
static void score_screen_lists_every_player_and_the_games_time(KpT *T)
{
	int part;
	for (part = 0; part < 2; part++) {
		Host *H = glue_host(T, 0);
		const KpUiScoreData *d;
		KpGame *g;
		double started;
		int k, p, computers = 0, listed = 0;
		play(H, PROTOSS01);
		g = H->f.session->g;
		frames(H, 60);
		H->f.game_started_at -= GAME_SECONDS;
		started = H->f.game_started_at;
		kp_exec_cheat(g, kp_cheats(g) | (part == 0 ? KP_CHEAT_NO_COW_LEVEL : KP_CHEAT_GAME_OVER_MAN));
		for (k = 0; k < RESULT_FRAMES && !H->f.ui->result_open; k++) frame(H);
		kp_t_ok(T, H->f.ui->result_open, "part %d: the result dialog", part);
		game_event(H, KP_EVENT_GAME_EXIT, part == 0 ? KP_UI_EXIT_VICTORY : KP_UI_EXIT_DEFEAT, NULL);
		glue_sleep(H);
		kp_t_eq(T, H->f.glue.screen, KP_GLUE_SCORE, "part %d: the score screen", part);
		d = &H->f.ui->score.data;
		kp_t_ok(T, d->elapsed >= GAME_SECONDS && d->elapsed <= (uint32_t)(H->now - started),
			"part %d: the game's %u s (%u)", part, (unsigned)GAME_SECONDS, (unsigned)d->elapsed);
		for (p = 0; p < 8; p++) {
			int64_t c = g->players.controller[p];
			uint32_t type = d->roster[p].type;
			/* the screen lists the humans and the named computers; the other slots keep their type with Storm ID -1
			 * (CaptureScoreRoster 0x461e00) */
			if (type == KP_CTRL_HUMAN || type == KP_CTRL_DEFEATED_HUMAN || type == KP_CTRL_COMPUTER
				|| type == KP_CTRL_DEFEATED_COMPUTER) {
				listed++;
			} else {
				kp_t_eq(T, d->roster[p].storm, 0xffffffffu, "part %d: player %d unlisted, no Storm ID", part, p);
			}
			if (c == KP_CTRL_DEFEATED_COMPUTER) {
				computers++;
				kp_t_eq(T, d->roster[p].type, KP_CTRL_DEFEATED_COMPUTER, "part %d: player %d, a retired computer", part, p);
				kp_t_ok(T, d->roster[p].name[0] != 0, "part %d: player %d named", part, p);
			}
		}
		kp_t_eq(T, d->roster[g->local_player].type, part == 0 ? KP_CTRL_HUMAN : KP_CTRL_DEFEATED_HUMAN,
			"part %d: the human", part);
		if (part == 0) kp_t_ok(T, computers > 0, "the computers retired by the victory");
		kp_t_eq(T, d->count, listed, "part %d: each listed", part);
		kp_t_ok(T, listed > 1, "part %d: more than the human (%d)", part, listed);
	}
}

/* The first catalog mission whose victory, with the game's next scenario cleared, passes cinematic
 * rows (*count of them) to a step of kind: its name and the run the step leaves (*after), NULL none. */
static const char *mission_with_step(Host *H, int kind, int *count, KpCampaignRun *after)
{
	int k;
	for (k = 0; k < H->data->mission_count; k++) {
		KpCampaignProfile progress = *kp_profile_progress(&H->profile);
		KpCampaignStep step;
		int64_t cinematics[KP_APP_STEP_CINEMATICS];
		const KpDataMission *m = &H->data->missions[k];
		if (!m->catalog || !kp_campaign_run_of_mission(&H->data->campaign, m->index, after)) continue;
		kp_campaign_after_game(&H->data->campaign, after, &progress, RESULT_VICTORY, "", &step, cinematics, count,
			KP_APP_STEP_CINEMATICS);
		if (*count > 0 && step.kind == kind) return m->name;
	}
	return NULL;
}

/* The game's victory, its next scenario cleared as mission_with_step assumed: its score screen. */
static void win(Host *H)
{
	memset(H->f.session->g->trig.next_scenario, 0, sizeof H->f.session->g->trig.next_scenario);
	kp_event(H->f.session->g, KP_EVENT_GAME_RESULT, RESULT_VICTORY, 0, 0, NULL);
	game_event(H, KP_EVENT_GAME_EXIT, KP_UI_EXIT_VICTORY, NULL);
	glue_sleep(H);
}

/* A mission whose victory passes cinematic rows (the game's next scenario cleared, as the scan assumed): the score
 * screen's Continue plays them, each after its prelude, and the next game waits for their end. */
static void score_cinematics_play_before_the_next_game(KpT *T)
{
	Host *H = glue_host(T, 1);
	KpCampaignRun run;
	const char *name;
	int count = 0, k, builds;
	name = mission_with_step(H, KP_CAMPAIGN_STEP_MISSION, &count, &run);
	kp_t_ok(T, name != NULL, "a mission with cinematic rows after it");
	play(H, name);
	win(H);
	kp_t_eq(T, H->f.step_cinematic_count, count, "the rows passed");
	wait_for(H, "rez\\gluscore.bin", MENU_FRAMES);
	builds = H->builds;
	click(H, SCORE_CONTINUE);
	for (k = 0; k < END_FRAMES && !H->movie; k++) frame(H);
	kp_t_eq(T, H->cine_starts, 1, "the cinematics");
	kp_t_eq(T, H->cine_count, count, "every row's");
	kp_t_eq(T, H->cine_preludes, 1, "each after its prelude");
	frames(H, 10);
	kp_t_eq(T, H->builds, builds, "the next game waits for them");
	H->movie = 0;
	frame(H);
	kp_t_eq(T, H->builds, builds + 1, "then it starts");
	kp_t_eq(T, H->f.glue.screen, KP_GLUE_ROOM, "its ready room");
}

/* The in-game menus' Restart: the mission again from its ready room, the run kept. */
static void restart_plays_the_mission_again_from_its_room(KpT *T)
{
	Host *H = glue_host(T, 0);
	KpCampaignRun run;
	int builds;
	play(H, EXPANSION_PROTOSS01);
	run = H->f.run;
	builds = H->builds;
	game_event(H, KP_EVENT_GAME_EXIT, KP_UI_EXIT_RESTART, NULL);
	kp_t_eq(T, H->f.hud_on, 0, "the console ended");
	kp_t_ok(T, H->f.after_game, "the glue goes on after its Sleep");
	kp_t_eq(T, H->builds, builds, "no game during the Sleep");
	glue_sleep(H);
	kp_t_eq(T, H->builds, builds + 1, "the game again");
	kp_t_eq_str(T, current(H), EXPANSION_PROTOSS01, "the same mission");
	kp_t_ok(T, memcmp(&H->f.run, &run, sizeof run) == 0, "the same run");
	kp_t_eq(T, H->f.glue.screen, KP_GLUE_ROOM, "its ready room");
	kp_t_eq(T, H->f.glue_room, PROTOSS_ROOM, NULL);
	kp_t_eq(T, H->f.has_continue, 0, "the continuation taken");
}

/* The in-game menus' Quit Mission: the campaign menu of the run's campaign (the expansion's, the original's). */
static void quit_mission_opens_the_campaign_menu(KpT *T)
{
	Host *H = glue_host(T, 0);
	play(H, EXPANSION_PROTOSS01);
	game_event(H, KP_EVENT_GAME_EXIT, KP_UI_EXIT_QUIT_MISSION, NULL);
	glue_sleep(H);
	in_menus(H, KP_GLUE_EXPANSION_CAMPAIGN, "Quit Mission in the expansion");
	play(H, PROTOSS01);
	game_event(H, KP_EVENT_GAME_EXIT, KP_UI_EXIT_QUIT_MISSION, NULL);
	glue_sleep(H);
	in_menus(H, KP_GLUE_CAMPAIGN, "Quit Mission in the original");
	kp_t_eq(T, H->quits, 0, NULL);
}

/* Load Replay (glue state 12, RunCreateGameScreen 0x455550): the game that ended left <pref>replays/LastReplay.rep
 * (EndGame 0x443db0); the list holds the folder's files with their headers' map, a file that is no replay is refused at
 * Ok (CreateGameFromEntry 0x45bfc0: gluAll 51 or 60, the screen kept), a game that loaded saves, another simulation
 * revision's or other data's (its KPRX tail) is marked unverified with its reason, and LastReplay's Ok plays its
 * mission from the file at once, without a ready room, in a replay's mode. */
enum { TAIL_LOADS, TAIL_REVISION, TAIL_DATA, TAIL_VARIANTS };

/* LastReplay with its tail changed (TAIL_*) as <pref>replays/NAME */
static void write_tail_variant(KpT *T, Host *H, const uint8_t *last, size_t last_size, int variant, const char *name)
{
	KpRep rep;
	KpRepRefusal why;
	KpError err;
	uint8_t *bytes;
	size_t size = 0;
	char path[400];
	kp_t_ok(T, kp_rep_read(&rep, last, last_size, &why, &err) == 0 && rep.tail != NULL, "LastReplay's tail");
	if (rep.tail == NULL) return;
	if (variant == TAIL_LOADS) rep.tail->loads = 1;
	else if (variant == TAIL_REVISION) rep.tail->sim_revision++;
	else rep.tail->data_hash[0] ^= 0xff;
	kp_t_ok(T, kp_rep_write(&rep, &bytes, &size, &err) == 0, "%s", err.msg);
	kp_rep_free(&rep);
	snprintf(path, sizeof path, "%sreplays/%s", H->pref, name);
	kp_t_ok(T, kp_write_file_atomic(path, bytes, size, &err) == 0, "%s", err.msg);
	free(bytes);
}

static void load_replay_lists_refuses_and_plays_the_last_game(KpT *T)
{
	static const struct { const char *name; uint8_t reason; } VARIANTS[TAIL_VARIANTS] = {
		{ "loaded.rep", KP_UI_REPLAY_LOADS }, { "revision.rep", KP_UI_REPLAY_REVISION }, { "data.rep", KP_UI_REPLAY_DATA } };
	Host *H = glue_host(T, 0);
	KpUi *ui;
	KpUiMenus *m;
	FILE *junk;
	uint8_t *bytes;
	size_t size = 0;
	char path[400];
	int k, v, last = -1, broken = -1, variant[TAIL_VARIANTS] = { -1, -1, -1 }, builds;
	play(H, PROTOSS01);
	frames(H, 30);
	game_event(H, KP_EVENT_GAME_EXIT, KP_UI_EXIT_QUIT_MISSION, NULL);
	glue_sleep(H);
	in_menus(H, KP_GLUE_CAMPAIGN, "Quit Mission");
	snprintf(path, sizeof path, "%sreplays/LastReplay.rep", H->pref);
	kp_t_ok(T, (bytes = kp_read_file(path, &size)) != NULL && size > 0, "%s written", path);
	kp_t_ok(T, H->f.identity != NULL, "the data's identity (the tail's data hash)");
	for (v = 0; v < TAIL_VARIANTS; v++) write_tail_variant(T, H, bytes, size, v, VARIANTS[v].name);
	/* a folder with a folder and a replay in it, and an empty folder that sorts before it */
	snprintf(path, sizeof path, "%sreplays/old", H->pref);
	kp_t_ok(T, kp_t_mkdir(path) == 0, "%s", path);
	snprintf(path, sizeof path, "%sreplays/old/deeper", H->pref);
	kp_t_ok(T, kp_t_mkdir(path) == 0, "%s", path);
	snprintf(path, sizeof path, "%sreplays/Empty", H->pref);
	kp_t_ok(T, kp_t_mkdir(path) == 0, "%s", path);
	snprintf(path, sizeof path, "%sreplays/old/kept.rep", H->pref);
	{
		KpError werr;
		kp_t_ok(T, kp_write_file_atomic(path, bytes, size, &werr) == 0, "%s", werr.msg);
	}
	free(bytes);
	snprintf(path, sizeof path, "%sreplays/broken.rep", H->pref);
	kp_t_ok(T, (junk = fopen(path, "wb")) != NULL, "%s", path);
	fputs("no replay", junk);
	fclose(junk);
	ui = H->f.ui;
	m = &ui->menus;
	kp_t_eq(T, ui->list_entries(ui->host, ui), 2 + 2 + TAIL_VARIANTS, "the folder's two folders and files");
	kp_t_ok(T, m->entries[0].kind == KP_UI_ENTRY_FOLDER && strcmp(m->entries[0].file, "Empty") == 0
		&& m->entries[1].kind == KP_UI_ENTRY_FOLDER && strcmp(m->entries[1].file, "old") == 0,
		"the folders first, by name without case, no folder above at the root");
	for (k = 2; k < m->entry_count; k++) kp_t_eq(T, m->entries[k].kind, KP_UI_ENTRY_REPLAY, "then the files");
	kp_t_eq(T, ui->check_replay(ui->host, 0), 51, "a folder is no replay to start");
	/* the folder's list: the folder above, its folder, its replay; its replay read and accepted through its path */
	snprintf(m->map_dir, sizeof m->map_dir, "old");
	kp_t_eq(T, ui->list_entries(ui->host, ui), 3, "old's three entries");
	kp_t_eq(T, m->entries[0].kind, KP_UI_ENTRY_UP, "the folder above first");
	kp_t_ok(T, m->entries[1].kind == KP_UI_ENTRY_FOLDER && strcmp(m->entries[1].file, "deeper") == 0, "its folder");
	kp_t_ok(T, m->entries[2].kind == KP_UI_ENTRY_REPLAY && strcmp(m->entries[2].file, "kept.rep") == 0
		&& m->entries[2].status == 0 && m->entries[2].humans == 1, "its replay read");
	kp_t_eq(T, ui->check_replay(ui->host, 2), 0, "its replay accepted");
	m->map_dir[0] = 0;
	(void)ui->list_entries(ui->host, ui);
	for (k = 0; k < m->entry_count; k++) {
		if (strcmp(m->entries[k].file, "LastReplay.rep") == 0) last = k;
		if (strcmp(m->entries[k].file, "broken.rep") == 0) broken = k;
		for (v = 0; v < TAIL_VARIANTS; v++) {
			if (strcmp(m->entries[k].file, VARIANTS[v].name) == 0) variant[v] = k;
		}
	}
	kp_t_ok(T, last >= 0 && broken >= 0, "all listed");
	kp_t_eq(T, m->entries[last].status, 0, "LastReplay read");
	kp_t_eq(T, m->entries[last].unverified, KP_UI_REPLAY_VERIFIED, "this build's game without loads verified");
	for (v = 0; v < TAIL_VARIANTS; v++) {
		kp_t_ok(T, variant[v] >= 0, "%s listed", VARIANTS[v].name);
		if (variant[v] < 0) continue;
		kp_t_eq(T, m->entries[variant[v]].status, 0, "%s read", VARIANTS[v].name);
		kp_t_eq(T, m->entries[variant[v]].unverified, VARIANTS[v].reason, "%s unverified with its reason", VARIANTS[v].name);
	}
	kp_t_ok(T, m->entries[last].map[0] != 0, "its map's name");
	kp_t_eq(T, m->entries[last].humans, 1, "one human");
	kp_t_ok(T, m->entries[broken].status != 0, "the other refused");
	k = ui->check_replay(ui->host, broken);
	kp_t_ok(T, k == 51 || k == 60, "refused at Ok with gluAll 51 or 60, not %d", k);
	kp_t_eq(T, ui->check_replay(ui->host, last), 0, "LastReplay accepted");
	builds = H->builds;
	m->entry_count = 0; /* the flow lists the folder again (the screen's refresh), as the choice's index is its */
	(void)ui->list_entries(ui->host, ui);
	m->replay_choice = last;
	menus_end(H, KP_UI_MENU_REPLAY, KP_GLUE_CAMPAIGN, 0, 0, 0);
	kp_t_eq(T, H->builds, builds + 1, "the replay's game");
	kp_t_eq_str(T, current(H), PROTOSS01, "its mission");
	kp_t_ok(T, H->f.playing_replay, "played from the file");
	kp_t_ok(T, H->f.session->g->w->replay, "in a replay's mode");
	kp_t_ok(T, H->f.hud_on && !kp_flow_glue_active(&H->f), "the console at once, no ready room");
	kp_t_eq(T, H->quits, 0, NULL);
}

/* A custom map's replay (campaign row 0: its map is its CHK, its setup its header): Load Replay accepts it and plays its
 * game from the file, a game of the replay's own map; one whose map its game does not load is refused (gluAll 51). */
static void load_replay_plays_a_custom_maps_replay(KpT *T)
{
	Host *H = glue_host(T, 0);
	KpUi *ui;
	KpUiMenus *m;
	KpRep rep;
	KpError err;
	uint8_t *bytes, *out;
	size_t size = 0, out_size;
	char path[400];
	int k, custom = -1, unloadable = -1;
	play(H, PROTOSS01);
	frames(H, 30);
	game_event(H, KP_EVENT_GAME_EXIT, KP_UI_EXIT_QUIT_MISSION, NULL);
	glue_sleep(H);
	in_menus(H, KP_GLUE_CAMPAIGN, "Quit Mission");
	snprintf(path, sizeof path, "%sreplays/LastReplay.rep", H->pref);
	kp_t_ok(T, (bytes = kp_read_file(path, &size)) != NULL && size > 0, "%s written", path);
	kp_t_ok(T, kp_rep_read(&rep, bytes, size, NULL, &err) == 0, "%s", err.msg);
	free(bytes);
	rep.header[KP_REP_CAMPAIGN] = rep.header[KP_REP_CAMPAIGN + 1] = 0; /* no campaign row: a custom map's game */
	kp_t_ok(T, kp_rep_write(&rep, &out, &out_size, &err) == 0, "%s", err.msg);
	snprintf(path, sizeof path, "%sreplays/custom.rep", H->pref);
	kp_t_ok(T, kp_write_file_atomic(path, out, out_size, &err) == 0, "%s", err.msg);
	free(out);
	rep.chk_size = 8; /* a map cut to its first section's header: no game reads it */
	kp_t_ok(T, kp_rep_write(&rep, &out, &out_size, &err) == 0, "%s", err.msg);
	kp_rep_free(&rep);
	snprintf(path, sizeof path, "%sreplays/unloadable.rep", H->pref);
	kp_t_ok(T, kp_write_file_atomic(path, out, out_size, &err) == 0, "%s", err.msg);
	free(out);
	ui = H->f.ui;
	m = &ui->menus;
	(void)ui->list_entries(ui->host, ui);
	for (k = 0; k < m->entry_count; k++) {
		if (strcmp(m->entries[k].file, "custom.rep") == 0) custom = k;
		if (strcmp(m->entries[k].file, "unloadable.rep") == 0) unloadable = k;
	}
	kp_t_ok(T, custom >= 0 && unloadable >= 0, "both listed");
	kp_t_eq(T, ui->check_replay(ui->host, unloadable), 51, "a custom map its game does not load refused");
	kp_t_eq(T, ui->check_replay(ui->host, custom), 0, "a custom map's replay accepted");
	m->replay_choice = custom;
	menus_end(H, KP_UI_MENU_REPLAY, KP_GLUE_CAMPAIGN, 0, 0, 0);
	kp_t_eq(T, H->f.current, KP_FLOW_CUSTOM, "the custom map's game");
	kp_t_ok(T, strncmp(H->f.custom_entry.key, "maps/", 5) == 0, "its key: %s", H->f.custom_entry.key);
	kp_t_ok(T, H->f.playing_replay && H->f.session && H->f.session->g && H->f.session->g->w->replay, "played from the file");
	kp_t_eq(T, kp_scalar(H->f.session->g, KP_MISSION_INDEX), 0, "row 0");
	kp_t_eq(T, H->f.session->host.data.mission.load, KP_CHK_UMS, "its map read for Use Map Settings (the template's)");
	kp_t_ok(T, H->f.hud_on, "the console at once");
	frames(H, 30);
	kp_t_ok(T, H->f.session->g != NULL && !H->f.session->failed, "it plays: %s", H->f.session->error);
	kp_t_eq(T, H->quits, 0, NULL);
}

/* A file's bytes equal to want's. */
static int file_equals(const char *path, const uint8_t *want, size_t size)
{
	size_t n = 0;
	uint8_t *got = kp_read_file(path, &n);
	int same = got != NULL && n == size && memcmp(got, want, size) == 0;
	free(got);
	return same;
}

/* Load Replay's Import and Export (Koprulu's, over the platform's file picker): the names a picked file gives its copy,
 * a replay copied into the listed folder under a free name, a file that is no replay refused, and Export handing the
 * host the listed replay's bytes under its name. */
static void load_replay_imports_and_exports(KpT *T)
{
	static const struct { const char *source, *name; } NAMES[] = {
		{ "/home/me/My game.rep", "My game.rep" },
		{ "C:\\Users\\me\\Games\\zerg.REP", "zerg.rep" },
		{ "content://com.android.providers.downloads.documents/document/primary%3ADownload%2FMy%20game.rep", "My game.rep" },
		{ "content://media/external/file/42", "42.rep" },
		{ "x", "x.rep" },
		{ "", "Replay.rep" },
		{ "/tmp/.rep", "Replay.rep" },
		{ "/tmp/a<b>c|d?.rep", "a_b_c_d_.rep" },
		{ "/tmp/trailing. .rep", "trailing.rep" },
		{ "/tmp/A replay name of forty bytes or so.rep", "A replay name of forty byte.rep" },
	};
	Host *H = glue_host(T, 0);
	KpUi *ui;
	KpUiMenus *m;
	uint8_t *bytes;
	size_t size = 0, k;
	char path[400], name[64];
	int at = -1, i;
	for (k = 0; k < sizeof NAMES / sizeof NAMES[0]; k++) {
		kp_flow_replay_import_name(NAMES[k].source, name, sizeof name);
		kp_t_eq_str(T, name, NAMES[k].name, "%s", NAMES[k].source);
	}
	play(H, PROTOSS01);
	frames(H, 30);
	game_event(H, KP_EVENT_GAME_EXIT, KP_UI_EXIT_QUIT_MISSION, NULL);
	glue_sleep(H);
	in_menus(H, KP_GLUE_CAMPAIGN, "Quit Mission");
	snprintf(path, sizeof path, "%sreplays/LastReplay.rep", H->pref);
	kp_t_ok(T, (bytes = kp_read_file(path, &size)) != NULL && size > 0, "%s written", path);
	if (!bytes) return;
	ui = H->f.ui;
	m = &ui->menus;
	kp_t_ok(T, ui->import_file != NULL && ui->export_replay != NULL, "the buttons offered with the host's picker");
	ui->import_file(ui->host);
	kp_t_eq(T, H->picks, 1, "Import asks the host's picker");
	kp_t_eq(T, H->pick_kind, KP_FLOW_PICK_REPLAY, "for a replay");
	kp_t_eq(T, kp_flow_import_replay(&H->f, NAMES[2].source, bytes, size, name, sizeof name), 0, "a replay imported");
	kp_t_eq_str(T, name, "My game.rep", "named by the URI's last part");
	snprintf(path, sizeof path, "%sreplays/My game.rep", H->pref);
	kp_t_ok(T, file_equals(path, bytes, size), "copied byte for byte");
	kp_t_eq(T, kp_flow_import_replay(&H->f, NAMES[0].source, bytes, size, name, sizeof name), 0, "again");
	kp_t_eq_str(T, name, "My game (2).rep", "a taken name gets a copy's number");
	kp_t_eq(T, kp_flow_import_replay(&H->f, NAMES[9].source, bytes, size, name, sizeof name), 0, "a long name");
	kp_t_eq(T, kp_flow_import_replay(&H->f, NAMES[9].source, bytes, size, name, sizeof name), 0, "twice");
	kp_t_eq_str(T, name, "A replay name of forty (2).rep", "the stem cut for the number too (at a word's end, no space left)");
	kp_t_ok(T, strlen(name) < 32, "below the original's 32 bytes");
	kp_t_ok(T, kp_flow_import_replay(&H->f, "/tmp/notes.rep", (const uint8_t *)"no replay", 9, name, sizeof name) != 0
		&& name[0] == 0, "a file that is no replay refused");
	snprintf(path, sizeof path, "%sreplays/notes.rep", H->pref);
	kp_t_ok(T, kp_read_file(path, &k) == NULL, "and not copied");
	snprintf(path, sizeof path, "%sreplays/old", H->pref);
	kp_t_ok(T, kp_t_mkdir(path) == 0, "%s", path);
	snprintf(m->map_dir, sizeof m->map_dir, "old");
	kp_t_eq(T, kp_flow_import_replay(&H->f, NAMES[1].source, bytes, size, name, sizeof name), 0, "into a folder");
	snprintf(path, sizeof path, "%sreplays/old/zerg.rep", H->pref);
	kp_t_ok(T, file_equals(path, bytes, size), "the listed folder's copy");
	m->map_dir[0] = 0;
	(void)ui->list_entries(ui->host, ui);
	for (i = 0; i < m->entry_count; i++) {
		if (strcmp(m->entries[i].file, "My game (2).rep") == 0) at = i;
	}
	kp_t_ok(T, at >= 0 && m->entries[at].status == 0, "the copy listed and read");
	if (at >= 0) ui->export_replay(ui->host, at);
	kp_t_eq(T, H->exports, 1, "Export hands the host the replay");
	kp_t_eq_str(T, H->export_name, "My game (2).rep", "under its name");
	kp_t_ok(T, H->export_size == size && H->export_bytes && memcmp(H->export_bytes, bytes, size) == 0, "its bytes");
	ui->export_replay(ui->host, 0);
	kp_t_eq(T, H->exports, 1, "a folder is not exported");
	free(bytes);
}

/* The score's Save Replay over the product's files (kp_flow.c's save-replay host): the dialog lists <pref>replays'
 * .rep files but LastReplay; a name's file is there or not; LastReplay.rep is copied to a new name's file and over an
 * existing one, the files synced; a delete removes the name's file; a name that is no file of the folder (a separator,
 * "..", none) is refused, and so is a copy with no LastReplay.rep. */
/* path written with size bytes of bytes. */
static void write_bytes(KpT *T, const char *path, const uint8_t *bytes, size_t size)
{
	KpError err;
	kp_t_ok(T, kp_write_file_atomic(path, bytes, size, &err) == 0, "%s", err.msg);
}

static void save_replay_writes_the_replays_folder(KpT *T)
{
	static const uint8_t LAST[] = "the last game's replay", OLD[] = "an older replay";
	static const char *const REFUSED[] = { "a/b", "a\\b", "..", ".", "" };
	Host *H = glue_host(T, 0);
	KpUi *ui = H->f.ui;
	char dir[400], path[400];
	int syncs, list;
	size_t k;
	snprintf(dir, sizeof dir, "%sreplays", H->pref);
	kp_t_ok(T, kp_t_mkdir(dir) == 0, "%s", dir);
	snprintf(path, sizeof path, "%s/LastReplay.rep", dir);
	write_bytes(T, path, LAST, sizeof LAST);
	snprintf(path, sizeof path, "%s/old game.rep", dir);
	write_bytes(T, path, OLD, sizeof OLD);
	snprintf(path, sizeof path, "%s/notes.txt", dir);
	write_bytes(T, path, OLD, sizeof OLD);
	kp_t_ok(T, ui->list_replays && ui->replay_exists && ui->copy_last_replay && ui->delete_replay, "the host's");

	kp_t_ok(T, kp_ui_replay_save_open(ui, 1, 1) >= 0, "the dialog over the main menu");
	frames(H, 2);
	list = kp_ui_find_control(ui, ui->open, 1);
	kp_t_eq(T, kp_ui_list_count(ui, list), 1, "old game alone");
	kp_t_eq_str(T, (const char *)kp_ui_list_text(ui, list, 0), "old game", NULL);
	ui->result = -3;
	kp_ui_end_dialog(ui, ui->open);
	frames(H, 2);

	kp_t_ok(T, ui->replay_exists(ui->host, "old game"), "old game's there");
	kp_t_ok(T, !ui->replay_exists(ui->host, "new one"), "new one's not");
	syncs = H->syncs;
	kp_t_ok(T, ui->copy_last_replay(ui->host, "new one"), "a new name");
	snprintf(path, sizeof path, "%s/new one.rep", dir);
	kp_t_ok(T, file_equals(path, LAST, sizeof LAST), "LastReplay's bytes");
	kp_t_ok(T, ui->copy_last_replay(ui->host, "old game"), "over an existing one");
	snprintf(path, sizeof path, "%s/old game.rep", dir);
	kp_t_ok(T, file_equals(path, LAST, sizeof LAST), "replaced");
	kp_t_eq(T, H->syncs - syncs, 2, "each copy synced");
	kp_t_ok(T, ui->delete_replay(ui->host, "new one"), "deleted");
	snprintf(path, sizeof path, "%s/new one.rep", dir);
	kp_t_ok(T, !ui->replay_exists(ui->host, "new one") && kp_read_file(path, &k) == NULL, "gone");
	kp_t_ok(T, !ui->delete_replay(ui->host, "new one"), "no file to delete");
	for (k = 0; k < sizeof REFUSED / sizeof REFUSED[0]; k++) {
		kp_t_ok(T, !ui->copy_last_replay(ui->host, REFUSED[k]), "copy to \"%s\" refused", REFUSED[k]);
		kp_t_ok(T, !ui->delete_replay(ui->host, REFUSED[k]), "delete of \"%s\" refused", REFUSED[k]);
		kp_t_ok(T, !ui->replay_exists(ui->host, REFUSED[k]), "\"%s\" is no file", REFUSED[k]);
	}
	snprintf(path, sizeof path, "%s/LastReplay.rep", dir);
	kp_t_eq(T, remove(path), 0, "%s", path);
	kp_t_ok(T, !ui->copy_last_replay(ui->host, "another"), "no LastReplay: nothing copied");
	snprintf(path, sizeof path, "%s/another.rep", dir);
	kp_t_ok(T, kp_read_file(path, &k) == NULL, "no file");
}

/* The save-replay dialog's list in the order the original's FindFirstFileA enumerates the folder on NTFS
 * (EnumerateReplayFiles 0x4803d0, kp_list_replays): the UTF-16 units after NTFS's upper case, so '_' after the letters
 * and a character past the Basic Multilingual Plane (its surrogates) before U+E000; a name starting with '.' is listed
 * only on Windows, where hidden is an attribute. */
static void save_replay_lists_in_the_file_systems_order(KpT *T)
{
	static const char *const FILES[] = { "_under", "Zebra", "\xd0\xaf\xd1\x80\xd1\x83\xd1\x81", "apple",
		"\xd1\x8f\xd0\xb1\xd0\xbb\xd0\xbe\xd0\xba\xd0\xbe", "old game", ".dot", "LastReplay" };
	static const char *const LISTED[] = {
#ifdef _WIN32
		".dot",
#endif
		"apple", "old game", "Zebra", "_under", "\xd1\x8f\xd0\xb1\xd0\xbb\xd0\xbe\xd0\xba\xd0\xbe",
		"\xd0\xaf\xd1\x80\xd1\x83\xd1\x81" };
	static const uint8_t BYTES[] = "a replay";
	Host *H = glue_host(T, 0);
	KpUi *ui = H->f.ui;
	char dir[400], path[400];
	int list;
	size_t k, n = sizeof LISTED / sizeof LISTED[0];
	kp_t_ok(T, kp_compare_ntfs("_", "a") > 0 && kp_compare_nocase("_", "a") < 0, "NTFS's upper case, not lower");
	kp_t_ok(T, kp_compare_ntfs("\xf0\x90\x90\x80", "\xee\x80\x80") < 0, "surrogates before U+E000");
	kp_t_ok(T, kp_compare_ntfs("\xc3\xa9", "\xc3\x89") == 0 && kp_compare_ntfs("\xc3\x9f", "\xc3\x9e") > 0,
		"Latin-1 folded, but the sharp s");
	snprintf(dir, sizeof dir, "%sreplays", H->pref);
	kp_t_ok(T, kp_t_mkdir(dir) == 0, "%s", dir);
	for (k = 0; k < sizeof FILES / sizeof FILES[0]; k++) {
		snprintf(path, sizeof path, "%s/%s.rep", dir, FILES[k]);
		write_bytes(T, path, BYTES, sizeof BYTES);
	}
	kp_t_ok(T, kp_ui_replay_save_open(ui, 1, 1) >= 0, "the dialog over the main menu");
	frames(H, 2);
	list = kp_ui_find_control(ui, ui->open, 1);
	kp_t_eq(T, kp_ui_list_count(ui, list), (int)n, "every replay but LastReplay");
	for (k = 0; k < n && k < (size_t)kp_ui_list_count(ui, list); k++) {
		kp_t_eq_str(T, (const char *)kp_ui_list_text(ui, list, (int)k), LISTED[k], "row %lu", (unsigned long)k);
	}
}

/* The map transfer's files (kp_flow_transfer_io): FindLocalCopy 0x441080 searches the user's maps folder as
 * FindFileByName 0x499450 does, its files in the file system's order (NTFS's), then its subfolders in that order, the
 * name compared without case and the size and CRC-32 checked; SaveDownloadedFile 0x4418c0 writes maps/download/ and
 * the next search finds it there. */
static void map_transfer_finds_and_saves_the_users_maps(KpT *T)
{
	static const uint8_t MAP[] = "the offered map's bytes", OTHER[] = "another map, same size!";
	static const uint8_t NEW[] = "a downloaded map";
	static const char *const FOLDERS[] = { "_under", "Zeta", "alpha", ".hidden" }; /* and download, the save's */
	static const char *const LISTED[] = { "alpha", "download", "Zeta", "_under" };
	Host *H = glue_host(T, 0);
	KpTransferIo io = kp_flow_transfer_io(&H->f);
	uint32_t size = (uint32_t)sizeof MAP, crc = kp_transfer_crc(MAP, sizeof MAP);
	char maps[400], path[400], out[400], download[400];
	char **names;
	uint8_t *back;
	size_t n = 0, k;
	int count = 0;
	kp_t_eq(T, (int)sizeof OTHER, (int)sizeof MAP, "the other map has the offer's size");
	/* a download goes to maps/download/, the folders made when missing */
	snprintf(maps, sizeof maps, "%smaps", H->pref);
	kp_t_ok(T, io.save(io.ud, "new.scx", NEW, sizeof NEW, out, sizeof out), "the download saved");
	snprintf(download, sizeof download, "%s/download/new.scx", maps);
	kp_t_eq_str(T, out, download, "the download's path");
	back = kp_read_file(download, &n);
	kp_t_ok(T, back != NULL && n == sizeof NEW && memcmp(back, NEW, n) == 0, "the download's bytes");
	free(back);
	back = io.read(io.ud, download, &n);
	kp_t_ok(T, back != NULL && n == sizeof NEW, "the download read back");
	free(back);
	kp_t_ok(T, io.find(io.ud, "NEW.SCX", (uint32_t)sizeof NEW, kp_transfer_crc(NEW, sizeof NEW), out, sizeof out),
		"the download found, its name without case");
	kp_t_eq_str(T, out, download, "the download found where it was saved");
	/* the folders in NTFS's order (upper case: '_' after the letters), the hidden one left out */
	for (k = 0; k < sizeof FOLDERS / sizeof FOLDERS[0]; k++) {
		snprintf(path, sizeof path, "%s/%s", maps, FOLDERS[k]);
		kp_t_ok(T, kp_t_mkdir(path) == 0, "%s", path);
		if (FOLDERS[k][0] == '.') kp_t_ok(T, kp_t_hide(path) == 0, "%s hidden", path);
	}
	names = kp_list_ntfs(maps, 1, &count);
	kp_t_eq(T, count, (int)(sizeof LISTED / sizeof LISTED[0]), "the folders but the hidden one");
	for (k = 0; k < (size_t)count && k < sizeof LISTED / sizeof LISTED[0]; k++)
		kp_t_eq_str(T, names[k], LISTED[k], "folder %lu in NTFS's order", (unsigned long)k);
	kp_list_files_free(names, count);
	kp_t_ok(T, !io.find(io.ud, "test.scm", size, crc, out, sizeof out), "no copy yet");
	/* a hidden folder's copy is not searched, alpha's has another CRC, download's comes before Zeta's */
	snprintf(path, sizeof path, "%s/.hidden/test.scm", maps);
	write_bytes(T, path, MAP, sizeof MAP);
	snprintf(path, sizeof path, "%s/alpha/test.scm", maps);
	write_bytes(T, path, OTHER, sizeof OTHER);
	kp_t_ok(T, !io.find(io.ud, "test.scm", size, crc, out, sizeof out), "neither the hidden copy nor the other map");
	snprintf(path, sizeof path, "%s/Zeta/Test.SCM", maps);
	write_bytes(T, path, MAP, sizeof MAP);
	kp_t_ok(T, io.find(io.ud, "test.scm", size, crc, out, sizeof out), "Zeta's copy");
	kp_t_eq_str(T, out, path, "Zeta's copy");
	snprintf(path, sizeof path, "%s/download/TEST.scm", maps);
	write_bytes(T, path, MAP, sizeof MAP);
	kp_t_ok(T, io.find(io.ud, "test.scm", size, crc, out, sizeof out), "download's copy");
	kp_t_eq_str(T, out, path, "download's copy comes before Zeta's");
	snprintf(path, sizeof path, "%s/test.scm", maps);
	write_bytes(T, path, MAP, sizeof MAP);
	kp_t_ok(T, io.find(io.ud, "test.scm", size, crc, out, sizeof out), "the folder's own copy");
	kp_t_eq_str(T, out, path, "the folder's files come before its subfolders");
	kp_t_ok(T, !io.find(io.ud, "test.scm", size + 1, crc, out, sizeof out), "another size");
	kp_t_ok(T, !io.find(io.ud, "test.scm", size, crc ^ 1u, out, sizeof out), "another CRC");
}

/* A saved network game's file of the test's own: a game ID, an RNG state (told apart by it), Brood War's byte */
static void write_net_save(KpT *T, const char *path, uint32_t id, uint32_t rng, int version, int expansion)
{
	KpNetGameSave s;
	uint8_t *bytes;
	size_t size = 0;
	memset(&s, 0, sizeof s);
	s.header.save_id = id;
	snprintf(s.header.name, sizeof s.header.name, "Test Game");
	snprintf(s.header.title, sizeof s.header.title, "Test Map");
	s.header.players = 2;
	s.header.max_players = 4;
	s.header.width = s.header.height = 96;
	s.header.tileset = 4;
	s.rng_state = rng;
	bytes = kp_net_lobby_save_file(&s, version, "Test", expansion, &size);
	kp_t_ok(T, bytes != NULL, "out of memory");
	write_bytes(T, path, bytes, size);
	free(bytes);
}

static const KpUiMapEntry *listed_entry(const KpUi *ui, const char *file)
{
	int k;
	for (k = 0; k < ui->menus.entry_count; k++)
		if (strcmp(ui->menus.entries[k].file, file) == 0) return &ui->menus.entries[k];
	return NULL;
}

/* Create Game's saved network games of the original (the oracle's original_saves; the product's are Koprulu's own,
 * koprulu_network_saves_list_and_resume). The list adds the game's saves (.mlt; .mlx with Brood War) as save entries in
 * multiplayer; LoadSaveEntryInfo 0x45d5e0 reads one (the map's title, the display name, players, size, tileset) or
 * refuses it (66 unreadable, 67 another version): without original_saves an original's file is unreadable. A lobby resumes a save by
 * its ID (kp_flow_find_save): FindMultiplayerSaveById 0x4995e0 searches the user's maps folder (its files in NTFS's
 * order, then its subfolders) for the ID, LoadSavedGameByName 0x498bd0 then opens the first file of that display name
 * and extension without case: a namesake found earlier wins, as the original's does. */
static void network_saves_list_and_resume_by_id(KpT *T)
{
	Host *H = glue_host(T, 0);
	KpUi *ui = H->f.ui;
	KpUiMenus *m = &ui->menus;
	const KpUiMapEntry *e;
	KpNetGameSave out;
	char maps[400], path[400];
	int k;
	snprintf(maps, sizeof maps, "%smaps", H->pref);
	(void)kp_t_mkdir(maps);
	snprintf(path, sizeof path, "%s/save", maps);
	kp_t_ok(T, kp_t_mkdir(path) == 0, "%s", path);
	snprintf(path, sizeof path, "%s/save/Resume.mlt", maps);
	write_net_save(T, path, 0x1234u, 0x222u, KP_NETSAVE_VERSION, 0);
	snprintf(path, sizeof path, "%s/save/Resume.mlx", maps);
	write_net_save(T, path, 0x1234u, 0x333u, KP_NETSAVE_VERSION, 1);
	snprintf(path, sizeof path, "%s/Old.mlt", maps);
	write_net_save(T, path, 0x99u, 0x555u, KP_NETSAVE_VERSION - 1, 0);
	snprintf(path, sizeof path, "%s/Damaged.mlt", maps);
	write_bytes(T, path, (const uint8_t *)"not a save", 10);
	/* the lists */
	m->state = KP_GLUE_PLAY_CUSTOM;
	m->map_dir[0] = 0;
	m->expansion = 0;
	m->multiplayer = 1;
	kp_t_ok(T, ui->list_entries(ui->host, ui) > 0, "the list");
	e = listed_entry(ui, "Old.mlt");
	kp_t_ok(T, e && e->kind == KP_UI_ENTRY_SAVE, "the original's save listed without the oracle's original saves");
	if (e) ui->load_entry(ui->host, ui, (int)(e - m->entries));
	kp_t_ok(T, e && e->status == 1 && e->description == 66, "but unreadable");
	kp_t_ok(T, !kp_flow_find_save(&H->f, 0x1234u, &out), "no save found without them");
	H->flags.original_saves = 1;
	m->multiplayer = 0;
	kp_t_ok(T, ui->list_entries(ui->host, ui) > 0, "the list");
	kp_t_ok(T, listed_entry(ui, "Old.mlt") == NULL, "no saves listed in single player");
	kp_t_ok(T, !kp_flow_find_save(&H->f, 0x1234u, &out), "no save found in single player");
	m->multiplayer = 1;
	kp_t_ok(T, ui->list_entries(ui->host, ui) > 0, "the list");
	for (k = 0; k < m->entry_count; k++)
		if (m->entries[k].kind == KP_UI_ENTRY_SAVE) ui->load_entry(ui->host, ui, k);
	e = listed_entry(ui, "Old.mlt");
	kp_t_ok(T, e && e->kind == KP_UI_ENTRY_SAVE && e->status == 1 && e->save_title == 47 && e->description == 67,
		"another version's save");
	e = listed_entry(ui, "Damaged.mlt");
	kp_t_ok(T, e && e->kind == KP_UI_ENTRY_SAVE && e->status == 1 && e->save_title == 47 && e->description == 66,
		"an unreadable save");
	snprintf(m->map_dir, sizeof m->map_dir, "save");
	kp_t_ok(T, ui->list_entries(ui->host, ui) > 0, "the save folder's list");
	kp_t_ok(T, listed_entry(ui, "Resume.mlx") == NULL, "Brood War's save is not the original game's");
	e = listed_entry(ui, "Resume.mlt");
	kp_t_ok(T, e && e->kind == KP_UI_ENTRY_SAVE, "the save listed");
	if (e) ui->load_entry(ui->host, ui, (int)(e - m->entries));
	kp_t_ok(T, e && e->status == 0 && e->save_title == 47, "the save reads");
	if (e) {
		kp_t_eq_str(T, e->map, "Test Map", "the save's map title");
		kp_t_eq_str(T, e->text, "Resume", "the save's display name");
		kp_t_ok(T, e->game_players == 2 && e->players == 4 && e->width == 96 && e->height == 96 && e->tileset == 4,
			"the save's players, size and tileset");
	}
	/* a lobby's resume by ID */
	kp_t_ok(T, kp_flow_find_save(&H->f, 0x1234u, &out), "the save found by its ID");
	kp_t_eq(T, out.rng_state, 0x222u, "the subfolder's save");
	kp_t_ok(T, !kp_flow_find_save(&H->f, 0x4321u, &out), "no save of another ID");
	snprintf(path, sizeof path, "%s/RESUME.MLT", maps);
	write_net_save(T, path, 0x77u, 0x444u, KP_NETSAVE_VERSION, 0);
	kp_t_ok(T, kp_flow_find_save(&H->f, 0x1234u, &out), "the ID's display name opens its namesake");
	kp_t_ok(T, out.rng_state == 0x444u && out.header.save_id == 0x77u, "the folder's namesake comes first");
	m->expansion = 1;
	kp_t_ok(T, kp_flow_find_save(&H->f, 0x1234u, &out), "Brood War's save found by its ID");
	kp_t_eq(T, out.rng_state, 0x333u, "Brood War's save");
}

/* Play Custom (glue state 11) against the original's screens (stage M0's custom goldens). Each game is its own
 * process's first in the emulator, so each gets a host of its own. The game's map is copied into the player's maps
 * folder under a name starting with '!', as the emulator's driver did: it sorts before the install's files, so the list
 * preselects it (with Brood War in maps/BroodWar, the folder the first visit enters). The drop-downs are set in the
 * golden's order. Then the type, subtype and slot lists equal the original's (their items, selection and whether they
 * take input), the races Ok applies equal the original's custom races (a melee template's), and Ok's lobby equals the
 * records the original's StartGame took. */
#define CUSTOM_SCREEN "rez\\glucustm.bin"
#define CUSTOM_OK 12
#define CUSTOM_PICK_FRAMES 30    /* a drop-down's pick, then the screen's 100 ms timers */
#define CUSTOM_PROFILE "Custom"  /* the emulator's profile's name */
#define CUSTOM_FIRST_LIST 20     /* the original's lists the golden's screen holds: 17, 18 and 20..35 */
#define CUSTOM_LAST_LIST 35
#define CUSTOM_TYPE_LIST 17
#define CUSTOM_SUBTYPE_LIST 18

/* The flow at the glue's main menu with ID name logged in: the profile's, and the menus' as the login screen leaves
 * them (the screens reached from the campaign menu show it). */
static Host *logged_in_host(KpT *T, const char *name)
{
	Host *H = new_host(T);
	H->profile.options &= ~KP_UI_OPTION_EXPANSION_INTRO;
	H->profile.character_count = 1;
	snprintf(H->profile.characters[0].name, sizeof H->profile.characters[0].name, "%s", name);
	snprintf(H->profile.name, sizeof H->profile.name, "%s", name);
	init(H);
	wait_for(H, "rez\\glumain.bin", MENU_FRAMES);
	snprintf(H->f.ui->menus.player, sizeof H->f.ui->menus.player, "%s", name);
	return H;
}

#define MAIN_SINGLE 3        /* the main menu's Single Player (RunMainMenu 0x45a8b0's result 3) */
#define GAME_MODE_ORIGINAL 6 /* gluGameMode's games (ChooseGameMode 0x45a750's results 6 and 7) */
#define GAME_MODE_EXPANSION 7
#define LOGIN_OK 4
#define CAMPAIGN_CUSTOM 10   /* the campaign menus' Play Custom (glue state 11) */

/* Play Custom as a person reaches it, each screen ending before the next opens. The glue's loop never opens a state's
 * screen over another's: entering the menus over an open screen (kp_glue_menus) would leave it under the next one,
 * drawing the cached background that the next theme's screen frees (EndGlueScreen 0x457b50 counts no screen open).
 * From the main menu: Single Player, the game (gluGameMode, when the data has Brood War), the login's Ok; then the
 * campaign menu's Play Custom. */
static void to_play_custom(Host *H, int expansion)
{
	char name[128];
	int k;
	if (H->f.ui->menus.state == KP_GLUE_MAIN) {
		click(H, MAIN_SINGLE);
		for (k = 0; k < MENU_FRAMES; k++) {
			top(H, name, sizeof name);
			if (strcmp(name, "rez\\glugamemode.bin") == 0 || strcmp(name, "rez\\glulogin.bin") == 0) break;
			frame(H);
		}
		if (strcmp(name, "rez\\glugamemode.bin") == 0) {
			frames(H, SETTLE_FRAMES);
			click(H, expansion ? GAME_MODE_EXPANSION : GAME_MODE_ORIGINAL);
		}
		wait_for(H, "rez\\glulogin.bin", MENU_FRAMES);
		click(H, LOGIN_OK);
	}
	wait_for(H, expansion ? "rez\\gluexpcmpgn.bin" : "rez\\glucmpgn.bin", MENU_FRAMES);
	click(H, CAMPAIGN_CUSTOM);
	wait_for(H, CUSTOM_SCREEN, MENU_FRAMES);
}

static int32_t open_control(const KpUi *ui, int id)
{
	int32_t c;
	if (ui->open < 0) return -1;
	for (c = ui->controls[ui->open].child; c >= 0 && ui->controls[c].id != id; c = ui->controls[c].next) {}
	return c;
}

/* Item k of the top dialog's combo box c picked as the emulator's driver picks it (ComboBoxProc 0x4c63b0): the press
 * on the field opens the list, the move hovers the row (ListRowAtCursor 0x4c62d0: the rows below the field, or from
 * the top of a list that opens upward), the release selects it. */
static void combo_pick(Host *H, int32_t c, int k)
{
	const KpUi *ui = H->f.ui;
	const KpUiControl *d = &ui->controls[ui->open], *b = &ui->controls[c];
	int32_t ox = H->f.glue.place_x + d->left, oy = H->f.glue.place_y + d->top;
	int32_t x = ox + (b->left + b->right) / 2, y = oy + (b->top + b->bottom) / 2, row, start, ty;
	kp_glue_input(&H->f.glue, KP_UI_MOUSE_MOVE, x, y);
	frame(H);
	kp_glue_input(&H->f.glue, KP_UI_LEFT_DOWN, x, y);
	frame(H);
	row = b->list->row_h;
	start = (b->list->up & 1) ? b->top : b->top + row; /* opened: the rectangle covers the rows */
	ty = oy + start + row * k + row / 2;
	kp_glue_input(&H->f.glue, KP_UI_MOUSE_MOVE, x, ty);
	frame(H);
	kp_glue_input(&H->f.glue, KP_UI_LEFT_UP, x, ty);
	frame(H);
}

/* The screen at Ok, the glue's screen with its popups composited as menus_replay composes it, against the original's
 * capture there (the golden's lobby.capture: the CRC-32 of the 640 x 480 indices and of each band of rows, the
 * cursor's rectangle 0 in both; the captures stay private). A golden recorded before the capture has none. */
static void custom_screen_is_the_originals(KpUi *ui, const KpJson *capture, const char *name, KpBuf *bad)
{
	const KpJson *bands = kp_json_get(capture, "bands"), *cursor = kp_json_get(capture, "cursor");
	const KpUiSurface *s;
	uint8_t *pixels;
	int32_t d, bottom = -1, y;
	int rows = (int)kp_json_field_int(capture, "band_rows", 0), k, n;
	size_t row, size;
	if (!capture) return;
	for (d = ui->open; d >= 0; d = ui->controls[d].next) bottom = d;
	if (bottom < 0) {
		kp_buf_printf(bad, "%s: no screen at Ok; ", name);
		return;
	}
	kp_ui_add_dirty_rect(ui, 0, 0, KP_UI_SCREEN_W - 1, KP_UI_SCREEN_H - 1);
	kp_ui_present(ui);
	s = &ui->controls[bottom].bitmap;
	if (!s->pixels || s->w != KP_UI_SCREEN_W || s->h != KP_UI_SCREEN_H || rows <= 0) {
		kp_buf_printf(bad, "%s: no 640 x 480 screen at Ok; ", name);
		return;
	}
	size = (size_t)s->w * (size_t)s->h;
	pixels = (uint8_t *)malloc(size);
	if (!pixels) {
		kp_buf_printf(bad, "%s: out of memory; ", name);
		return;
	}
	memcpy(pixels, s->pixels, size);
	if (kp_json_len(cursor) == 4) { /* where the original's cursor was, 0 as in the golden */
		int32_t cx = (int32_t)kp_json_int(kp_json_at(cursor, 0), 0), cy = (int32_t)kp_json_int(kp_json_at(cursor, 1), 0);
		int32_t cw = (int32_t)kp_json_int(kp_json_at(cursor, 2), 0), ch = (int32_t)kp_json_int(kp_json_at(cursor, 3), 0);
		for (y = cy; y < cy + ch && y < s->h; y++) {
			if (cx >= 0 && cw > 0 && cx + cw <= s->w && y >= 0) memset(pixels + (size_t)y * (size_t)s->w + (size_t)cx, 0, (size_t)cw);
		}
	}
	if (kp_crc32(pixels, size) != (uint32_t)kp_json_field_int(capture, "crc", -1)) {
		kp_buf_printf(bad, "%s: the screen at Ok differs in rows", name);
		row = (size_t)s->w * (size_t)rows;
		n = kp_json_len(bands);
		for (k = 0; k < n && (size_t)k * row < size; k++) {
			/* the last band as Python's slice takes it: what is left of the screen when rows does not divide it */
			size_t at = (size_t)k * row, len = size - at < row ? size - at : row;
			if (kp_crc32(pixels + at, len) != (uint32_t)kp_json_int(kp_json_at(bands, k), -1)) {
				kp_buf_printf(bad, " %d-%d", k * rows, (int)((at + len) / (size_t)s->w) - 1);
			}
		}
		kp_buf_printf(bad, "; ");
		if (getenv("KP_TEST_CUSTOM_SCREENS")) { /* Koprulu's screen as indices, to compare with the private capture */
			char path[512];
			FILE *out;
			snprintf(path, sizeof path, "%s/%s.idx", getenv("KP_TEST_CUSTOM_SCREENS"), name);
			if ((out = fopen(path, "wb")) != NULL) {
				fwrite(pixels, 1, size, out);
				fclose(out);
			}
		}
	}
	free(pixels);
}

/* The screen's list id against the golden's (its items, raw selection, visible and enabled). */
static void custom_list_is_the_originals(const KpUi *ui, const KpJson *screen, int id, const char *name, KpBuf *bad)
{
	char key[8];
	const KpJson *want, *items;
	const KpUiControl *k;
	int32_t c;
	int j, n, usable, selected;
	snprintf(key, sizeof key, "%d", id);
	if ((want = kp_json_get(screen, key)) == NULL) return; /* the original's dialog had no such control */
	if ((c = open_control(ui, id)) < 0 || ui->controls[c].list == NULL) {
		kp_buf_printf(bad, "%s: no list %d; ", name, id);
		return;
	}
	k = &ui->controls[c];
	items = kp_json_get(want, "items");
	n = kp_ui_list_count(ui, c);
	if (n != kp_json_len(items)) kp_buf_printf(bad, "%s: list %d has %d items, the original's %d; ", name, id, n, kp_json_len(items));
	for (j = 0; j < n && j < kp_json_len(items); j++) {
		const char *text = (const char *)kp_ui_list_text(ui, c, j), *original = kp_json_str(kp_json_at(items, j), "");
		if (strcmp(text ? text : "", original) != 0) kp_buf_printf(bad, "%s: list %d item %d \"%s\", the original's \"%s\"; ", name, id, j, text ? text : "", original);
	}
	selected = k->list->selected;
	if (selected != kp_json_field_int(want, "selected", -1)) {
		kp_buf_printf(bad, "%s: list %d selects %d, the original's %lld; ", name, id, selected,
			(long long)kp_json_field_int(want, "selected", -1));
	}
	usable = (k->flags & KP_UI_VISIBLE) && !(k->flags & KP_UI_DISABLED);
	if (usable != kp_json_truthy(kp_json_get(want, "usable"))) {
		kp_buf_printf(bad, "%s: list %d %s input, the original's %s; ", name, id, usable ? "takes" : "takes no",
			usable ? "not" : "does");
	}
}

/* Ok's races (ApplyCustomGameRaces 0x455020) and lobby against the golden's setup. */
static void custom_lobby_is_the_originals(const Host *H, const KpJson *v, const char *name, KpBuf *bad)
{
	const KpUiCreate *c = &H->f.ui->menus.create;
	const KpGameSetup *got = &H->f.custom_setup;
	const KpJson *races = kp_json_get(v, "custom_races");
	KpGameSetup want;
	KpError err;
	int p;
	if (kp_oracle_setup_from_json(v, &want, &err) != 0) {
		kp_buf_printf(bad, "%s: %s; ", name, err.msg);
		return;
	}
	if (want.template_record[KP_TEMPLATE_VICTORY] == KP_VICTORY_MELEE) { /* another template leaves them as they were */
		int same = c->melee && kp_json_int(kp_json_at(races, 0), -1) == c->local_race;
		for (p = 0; p + 1 < kp_json_len(races) && p < KP_UI_CUSTOM_RACES; p++) {
			same &= kp_json_int(kp_json_at(races, p + 1), -1) == c->races[p];
		}
		if (!same) kp_buf_printf(bad, "%s: Ok's races are not the original's custom races; ", name);
	}
	if (!H->f.has_custom_setup) {
		kp_buf_printf(bad, "%s: no lobby (%s); ", name, H->last_log);
		return;
	}
	if (memcmp(got->template_record, want.template_record, KP_TEMPLATE_SIZE) != 0 || got->slots != want.slots) {
		kp_buf_printf(bad, "%s: the template or slot count; ", name);
	}
	for (p = 0; p < KP_SETUP_SLOTS; p++) {
		if (got->storm[p] != want.storm[p] || got->type[p] != want.type[p] || got->race[p] != want.race[p]
			|| got->force[p] != want.force[p] || strcmp(got->name[p], want.name[p]) != 0) {
			kp_buf_printf(bad, "%s: slot %d (type %lld race %lld force %lld storm %lld \"%s\", the original's %lld %lld "
				"%lld %lld \"%s\"); ", name, p, (long long)got->type[p], (long long)got->race[p], (long long)got->force[p],
				(long long)got->storm[p], got->name[p], (long long)want.type[p], (long long)want.race[p],
				(long long)want.force[p], (long long)want.storm[p], want.name[p]);
		}
	}
}

static void play_custom_screens_are_the_originals(KpT *T)
{
	const char *root = getenv("KP_TEST_ORIGINAL");
	const KpJson *index;
	KpBuf *bad;
	int k;
	if (!root || !root[0]) kp_t_skip(T, "no KP_TEST_ORIGINAL (the custom games' maps)");
	index = kp_t_golden(T, "tests/golden/oracle/custom/index.json");
	bad = kp_t_buf(T);
	for (k = 0; k < kp_json_len(index); k++) {
		char path[1024], copy[1024], golden[256];
		const char *name = kp_json_str(kp_json_at(index, k), ""), *mission, *file;
		const KpJson *g, *lobby, *combos, *screen;
		Host *H;
		KpUi *ui;
		KpError err;
		uint8_t *bytes;
		size_t size = 0;
		int expansion, j, id;
		snprintf(golden, sizeof golden, "tests/golden/oracle/custom/%s.json", name);
		g = kp_t_golden(T, golden);
		lobby = kp_json_get(g, "lobby");
		combos = kp_json_get(lobby, "combos");
		screen = kp_json_get(lobby, "screen");
		mission = kp_json_field_str(g, "mission", "");
		expansion = kp_json_field_int(g, "expansion", 0) != 0;
		file = strrchr(mission, '/') ? strrchr(mission, '/') + 1 : mission;
		if (!screen || strncmp(mission, "maps/", 5) != 0) {
			kp_buf_printf(bad, "%s: no lobby or map in the golden; ", name);
			continue;
		}
		H = logged_in_host(T, CUSTOM_PROFILE);
		ui = H->f.ui;
		ui->import_file = NULL; /* Import is Koprulu's: the screen as a platform without a file picker shows it */
		snprintf(H->flags.res_dirs[0], sizeof H->flags.res_dirs[0], "%s", root);
		H->flags.res_dir_count = 1;
		snprintf(path, sizeof path, "%s/%s", root, mission);
		snprintf(copy, sizeof copy, "%smaps", H->pref);
		kp_t_ok(T, kp_t_mkdir(copy) == 0, "%s", copy);
		if (expansion) {
			snprintf(copy, sizeof copy, "%smaps/BroodWar", H->pref);
			kp_t_ok(T, kp_t_mkdir(copy) == 0, "%s", copy);
		}
		snprintf(copy, sizeof copy, "%smaps/%s!%s", H->pref, expansion ? "BroodWar/" : "", file);
		kp_t_ok(T, (bytes = kp_read_file(path, &size)) != NULL, "%s: %s", name, path);
		kp_t_ok(T, kp_write_file_atomic(copy, bytes, size, &err) == 0, "%s", err.msg);
		free(bytes);
		to_play_custom(H, expansion);
		/* custom.py goes through the menus by keys, so its cursor is where the emulator starts it (GetCursorPos: the
		 * screen's centre) until a drop-down is pressed; this test clicked, and a control under its pointer is lit */
		kp_glue_input(&H->f.glue, KP_UI_MOUSE_MOVE, H->f.glue.place_x + KP_UI_SCREEN_W / 2,
			H->f.glue.place_y + KP_UI_SCREEN_H / 2);
		frame(H);
		for (j = 0; j < kp_json_len(combos); j++) {
			const KpJson *pick = kp_json_at(combos, j);
			int32_t c = open_control(ui, (int)kp_json_field_int(pick, "control", -1));
			int item = (int)kp_json_field_int(pick, "selected", -1);
			if (c < 0 || ui->controls[c].list == NULL) {
				kp_buf_printf(bad, "%s: no list %lld to pick; ", name, (long long)kp_json_field_int(pick, "control", -1));
				continue;
			}
			if (ui->controls[c].list->selected != item) combo_pick(H, c, item); /* the driver presses only then */
			frames(H, CUSTOM_PICK_FRAMES);
		}
		custom_list_is_the_originals(ui, screen, CUSTOM_TYPE_LIST, name, bad);
		custom_list_is_the_originals(ui, screen, CUSTOM_SUBTYPE_LIST, name, bad);
		for (id = CUSTOM_FIRST_LIST; id <= CUSTOM_LAST_LIST; id++) custom_list_is_the_originals(ui, screen, id, name, bad);
		custom_screen_is_the_originals(ui, kp_json_get(lobby, "capture"), name, bad);
		click(H, CUSTOM_OK);
		for (j = 0; j < END_FRAMES && !H->f.has_custom_setup; j++) frame(H);
		custom_lobby_is_the_originals(H, kp_json_get(g, "setup"), name, bad);
		if (H->f.has_custom_setup && kp_template_ums(H->f.custom_setup.template_record)) {
			kp_t_ok(T, H->f.glue.screen == KP_GLUE_ROOM, "%s: Use Map Settings opens the ready room", name);
		} else if (H->f.has_custom_setup) {
			kp_t_ok(T, H->f.current == KP_FLOW_CUSTOM && H->f.hud_on, "%s: a melee game at once (%s)", name, H->last_log);
		}
	}
	kp_t_ok(T, bad->size == 0, "Play Custom differs from the original's: %s", kp_buf_text(bad));
}

/* A Play Custom game's end after its score screen (AdvanceCampaignAfterGame 0x4bb790 in custom mode:
 * CreateNextScenarioGame 0x4bb360, whatever the result). Without a next scenario it opens the campaign menu of the
 * screen's expansion byte. With one, the map of that name in the finished map's folder is played as a Use Map
 * Settings game from the ready room of the player's race (protoss01's scenario as a custom map: the Protoss room). A
 * next scenario that is no map reports network.tbl's 0x6a over the campaign menu (FlushDeferredNetError 0x474980). */
#define NEXT_FIRST "first.scm"
#define NEXT_SECOND "second.scm"
#define TYPE_UMS_TEXT "Use Map Settings"
#define NET_NEXT_SCENARIO 0x6a

/* An archive at path holding chk as its scenario. */
static void write_map_at(Host *H, const char *path, const uint8_t *chk, size_t size)
{
	char err[KP_ERROR_SIZE];
	KpMpq *m;
	kp_t_ok(H->T, (m = kp_mpq_create(path, 4, err, sizeof err)) != NULL, "%s", err);
	kp_t_ok(H->T, kp_mpq_add(m, "staredit\\scenario.chk", chk, size, KP_MPQ_ZLIB, err, sizeof err) == 0, "%s", err);
	kp_t_ok(H->T, kp_mpq_close(m) == 0, "%s written", path);
}

/* <pref>maps/name: an archive holding chk as its scenario. */
static void write_custom_map(Host *H, const char *name, const uint8_t *chk, size_t size)
{
	char path[512];
	snprintf(path, sizeof path, "%smaps/%s", H->pref, name);
	write_map_at(H, path, chk, size);
}

/* Play Custom's game of the preselected map (the folder's first) with Use Map Settings, up to its console. */
static void play_custom_ums(Host *H)
{
	KpUi *ui = H->f.ui;
	int32_t types;
	int k, ums = -1;
	to_play_custom(H, 0);
	types = open_control(ui, CUSTOM_TYPE_LIST);
	kp_t_ok(H->T, types >= 0, "the type list");
	for (k = 0; k < kp_ui_list_count(ui, types); k++) {
		if (strcmp((const char *)kp_ui_list_text(ui, types, k), TYPE_UMS_TEXT) == 0) ums = k;
	}
	kp_t_ok(H->T, ums >= 0, "Use Map Settings offered");
	kp_ui_list_select(ui, types, ums);
	frames(H, CUSTOM_PICK_FRAMES);
	click(H, CUSTOM_OK);
	room_start(H);
	kp_t_eq(H->T, H->f.current, KP_FLOW_CUSTOM, "the custom map's game");
}

/* The game won with next (a next scenario's name, "" none), then the score screen's Continue. */
static void custom_win(Host *H, const char *next)
{
	uint8_t *name = H->f.session->g->trig.next_scenario;
	memset(name, 0, KP_NEXT_SCENARIO);
	memcpy(name, next, strlen(next));
	kp_event(H->f.session->g, KP_EVENT_GAME_RESULT, RESULT_VICTORY, 0, 0, NULL);
	game_event(H, KP_EVENT_GAME_EXIT, KP_UI_EXIT_VICTORY, NULL);
	glue_sleep(H);
	wait_for(H, "rez\\gluscore.bin", MENU_FRAMES);
	click(H, SCORE_CONTINUE);
}

static void custom_games_end_in_the_next_scenario_or_the_campaign_menu(KpT *T)
{
	Host *H = logged_in_host(T, "Fenix");
	const KpDataMission *m = mission_entry(H, PROTOSS01);
	char path[256], name[128];
	uint8_t *chk;
	size_t size = 0;
	KpError err;
	int k;
	snprintf(path, sizeof path, "%s\\staredit\\scenario.chk", m->directory);
	kp_t_ok(T, (chk = kp_files_need(&H->data->files, path, &size, &err)) != NULL, "%s", err.msg);
	snprintf(path, sizeof path, "%smaps", H->pref);
	kp_t_ok(T, kp_t_mkdir(path) == 0, "%s", path);
	write_custom_map(H, NEXT_FIRST, chk, size);
	write_custom_map(H, NEXT_SECOND, chk, size);
	free(chk);
	/* no next scenario: the campaign menu */
	play_custom_ums(H);
	custom_win(H, "");
	for (k = 0; k < END_FRAMES && H->f.glue.screen != KP_GLUE_MENUS; k++) frame(H);
	in_menus(H, KP_GLUE_CAMPAIGN, "a custom game's end without a next scenario");
	/* a next scenario: its game from the Protoss room, as Use Map Settings */
	play_custom_ums(H);
	custom_win(H, NEXT_SECOND);
	for (k = 0; k < END_FRAMES && H->f.glue.screen != KP_GLUE_ROOM; k++) frame(H);
	kp_t_eq(T, H->f.glue.screen, KP_GLUE_ROOM, "the next scenario's ready room (%s)", H->last_log);
	kp_t_eq(T, H->f.glue_room, PROTOSS_ROOM, "the player's race's");
	kp_t_eq_str(T, current(H), NEXT_SECOND, "the next scenario's map");
	kp_t_ok(T, kp_template_ums(H->f.custom_setup.template_record), "with the Use Map Settings template");
	snprintf(name, sizeof name, "%smaps/%s", H->pref, NEXT_SECOND);
	kp_t_eq_str(T, H->f.custom_path, name, "its file, the next one's folder");
	room_start(H);
	/* a next scenario that is no map: 0x6a over the campaign menu */
	custom_win(H, "missing.scm");
	for (k = 0; k < END_FRAMES && H->f.glue.screen != KP_GLUE_MENUS; k++) frame(H);
	in_menus(H, KP_GLUE_CAMPAIGN, "a next scenario that is no map");
	kp_t_eq(T, H->f.ui->menus.net_error, NET_NEXT_SCENARIO, "network.tbl's 0x6a deferred");
	wait_for(H, "rez\\glupok.bin", MENU_FRAMES);
	kp_t_eq(T, H->f.ui->menus.net_error, 0, "shown by the campaign menu");
	/* a next scenario out of the map's folder, though its path names a map: 0x6a as well */
	key(H, '\r'); /* the popup's Ok */
	play_custom_ums(H);
	custom_win(H, "../maps/" NEXT_FIRST);
	for (k = 0; k < END_FRAMES && H->f.glue.screen != KP_GLUE_MENUS; k++) frame(H);
	in_menus(H, KP_GLUE_CAMPAIGN, "a next scenario out of the folder");
	kp_t_eq(T, H->f.ui->menus.net_error, NET_NEXT_SCENARIO, "network.tbl's 0x6a deferred (%s)", H->last_log);
	kp_t_eq(T, H->quits, 0, NULL);
}

/* A custom game's save names its map as StripKnownDirectoryFromMapPath 0x49a130 does: "maps/" and its path below the
 * maps' folder. Its load finds the file again (ResolveGameFile 0x45cb90): the file's sounds when its scenario is the
 * save's, and the game's end goes on to the next scenario of its folder (CreateNextScenarioGame 0x4bb360). A file
 * changed since: the save's scenario alone, the next scenario still in the folder. */
#define CUSTOM_SUB "sub/"

/* The menus' load screen's Ok on the save of name (the logged-in ID's list), its game loaded in that frame. */
static void glue_load(Host *H, const char *name)
{
	KpUiMenus *m = &H->f.ui->menus;
	int k;
	for (k = 0; k < m->save_count && strcmp(m->saves[k], name) != 0; k++) {}
	kp_t_ok(H->T, k < m->save_count, "%s listed", name);
	m->save_choice = k;
	menus_end(H, KP_UI_MENU_LOAD_SAVE, m->state, 0, 0, 0);
}

static void custom_saves_find_their_maps_folder(KpT *T)
{
	Host *H = logged_in_host(T, "Fenix");
	const KpDataMission *m = mission_entry(H, PROTOSS01);
	static const uint8_t extra[8] = { 'X', 'T', 'R', 'A', 0, 0, 0, 0 }; /* a section no reader knows, empty */
	char path[512], name[KP_SAVE_NAME], why[256];
	uint8_t *chk, *changed, *bytes;
	size_t size = 0, n = 0;
	KpError err;
	int k;
	snprintf(path, sizeof path, "%s\\staredit\\scenario.chk", m->directory);
	kp_t_ok(T, (chk = kp_files_need(&H->data->files, path, &size, &err)) != NULL, "%s", err.msg);
	snprintf(path, sizeof path, "%smaps", H->pref);
	kp_t_ok(T, kp_t_mkdir(path) == 0, "%s", path);
	snprintf(path, sizeof path, "%smaps/" CUSTOM_SUB, H->pref);
	kp_t_ok(T, kp_t_mkdir(path) == 0, "%s", path);
	write_custom_map(H, CUSTOM_SUB NEXT_FIRST, chk, size);
	write_custom_map(H, CUSTOM_SUB NEXT_SECOND, chk, size);
	snprintf(path, sizeof path, "%smaps/" CUSTOM_SUB NEXT_FIRST, H->pref);
	kp_t_eq(T, kp_flow_play_map(&H->f, path, TYPE_UMS_TEXT, why, sizeof why), 0, "%s", why);
	for (k = 0; k < END_FRAMES && !H->f.hud_on; k++) frame(H);
	kp_t_eq_str(T, H->f.custom_key, "maps/" CUSTOM_SUB NEXT_FIRST, "the saves' name: the path below the maps' folder");
	save(H, "customsave");
	save_path(H, "customsave", path, sizeof path);
	kp_t_ok(T, (bytes = kp_read_file(path, &n)) != NULL, "%s", path);
	kp_t_eq(T, kp_save_mission(bytes, n, H->f.identity, name, sizeof name, NULL, &err), 0, "%s", err.msg);
	kp_t_eq_str(T, name, "maps/" CUSTOM_SUB NEXT_FIRST, "the native save's mission");
	free(bytes);
	game_event(H, KP_EVENT_GAME_EXIT, KP_UI_EXIT_QUIT_MISSION, NULL);
	glue_sleep(H);
	for (k = 0; k < END_FRAMES && H->f.glue.screen != KP_GLUE_MENUS; k++) frame(H);
	frames(H, 2);
	glue_load(H, "customsave");
	kp_t_ok(T, H->f.session != NULL, "the saved game (%s)", H->last_log);
	if (!H->f.session) return;
	kp_t_eq(T, H->f.session->loads, 1, "the save loaded (%s)", H->last_log);
	snprintf(path, sizeof path, "%smaps/" CUSTOM_SUB NEXT_FIRST, H->pref);
	kp_t_eq_str(T, H->f.custom_path, path, "the file the save's name resolves to");
	kp_t_ok(T, H->f.custom_map && H->f.custom_map->mpq, "the file's archive: its sounds");
	custom_win(H, NEXT_SECOND);
	for (k = 0; k < END_FRAMES && H->f.glue.screen != KP_GLUE_ROOM; k++) frame(H);
	kp_t_eq(T, H->f.glue.screen, KP_GLUE_ROOM, "the next scenario's ready room after a load (%s)", H->last_log);
	snprintf(path, sizeof path, "%smaps/" CUSTOM_SUB NEXT_SECOND, H->pref);
	kp_t_eq_str(T, H->f.custom_path, path, "the next scenario of the loaded map's folder");
	room_start(H);
	/* a map changed since its save */
	save(H, "customchanged");
	game_event(H, KP_EVENT_GAME_EXIT, KP_UI_EXIT_QUIT_MISSION, NULL);
	glue_sleep(H);
	for (k = 0; k < END_FRAMES && H->f.glue.screen != KP_GLUE_MENUS; k++) frame(H);
	frames(H, 2);
	kp_t_ok(T, (changed = (uint8_t *)malloc(size + sizeof extra)) != NULL, NULL);
	memcpy(changed, chk, size);
	memcpy(changed + size, extra, sizeof extra);
	write_custom_map(H, CUSTOM_SUB NEXT_SECOND, changed, size + sizeof extra);
	free(changed);
	glue_load(H, "customchanged");
	kp_t_ok(T, H->f.session != NULL, "the saved game (%s)", H->last_log);
	if (!H->f.session) return;
	kp_t_eq(T, H->f.session->loads, 1, "the save loaded (%s)", H->last_log);
	kp_t_ok(T, H->f.custom_map && !H->f.custom_map->mpq, "the save's scenario alone");
	kp_t_eq_str(T, H->f.custom_path, path, "the file's path all the same");
	custom_win(H, NEXT_FIRST);
	for (k = 0; k < END_FRAMES && H->f.glue.screen != KP_GLUE_ROOM; k++) frame(H);
	kp_t_eq(T, H->f.glue.screen, KP_GLUE_ROOM, "its next scenario's ready room (%s)", H->last_log);
	snprintf(path, sizeof path, "%smaps/" CUSTOM_SUB NEXT_FIRST, H->pref);
	kp_t_eq_str(T, H->f.custom_path, path, "in the same folder");
	free(chk);
	kp_t_eq(T, H->quits, 0, NULL);
}

/* A file's access and modification times set to t: 0, or -1 */
static int set_file_time(const char *path, time_t t)
{
#ifdef _WIN32
	struct _utimbuf u;
	u.actime = u.modtime = t;
	return _utime(path, &u);
#else
	struct utimbuf u;
	u.actime = u.modtime = t;
	return utime(path, &u);
#endif
}

static int file_exists(const char *path)
{
	FILE *in = fopen(path, "rb");
	if (in) fclose(in);
	return in != NULL;
}

/* Koprulu's own network saves (kp_netsave.h): a game's save with NETG in maps/save, as every seat writes one. Create
 * Game lists it in multiplayer and reads its header from NETG (the map's title, the display name from the file's name);
 * a lobby resuming its ID finds it (kp_flow_find_save, the file kept for the game's load) in the game it was saved in,
 * not in the other (Brood War); a save of other data is another version (67). In a network game the in-game save
 * dialogs list maps/save's saves of Koprulu newest first, and their delete removes the file there. */
static void koprulu_network_saves_list_and_resume(KpT *T)
{
	Host *H = logged_in_host(T, "Fenix");
	const KpDataMission *mission = mission_entry(H, PROTOSS01);
	KpUi *ui = H->f.ui;
	KpUiMenus *m = &ui->menus;
	const KpUiMapEntry *e;
	KpNetGameSave rec, out;
	KpSaveIdentity other;
	uint8_t netg[KP_NETSAVE_NETG_SIZE], *chk, *bytes;
	char path[512], why[256];
	size_t size = 0, n = 0;
	KpError err;
	int k;
	snprintf(path, sizeof path, "%s\\staredit\\scenario.chk", mission->directory);
	kp_t_ok(T, (chk = kp_files_need(&H->data->files, path, &size, &err)) != NULL, "%s", err.msg);
	snprintf(path, sizeof path, "%smaps", H->pref);
	kp_t_ok(T, kp_t_mkdir(path) == 0, "%s", path);
	snprintf(path, sizeof path, "%smaps/save", H->pref);
	kp_t_ok(T, kp_t_mkdir(path) == 0, "%s", path);
	write_custom_map(H, NEXT_FIRST, chk, size);
	free(chk);
	snprintf(path, sizeof path, "%smaps/" NEXT_FIRST, H->pref);
	kp_t_eq(T, kp_flow_play_map(&H->f, path, TYPE_UMS_TEXT, why, sizeof why), 0, "%s", why);
	for (k = 0; k < END_FRAMES && !H->f.hud_on; k++) frame(H);
	memset(&rec, 0, sizeof rec);
	rec.header.save_id = 0x5151u;
	snprintf(rec.header.name, sizeof rec.header.name, "Koprulu Game");
	snprintf(rec.header.title, sizeof rec.header.title, "Saved Map");
	rec.header.players = 2;
	rec.header.max_players = 2;
	rec.header.width = rec.header.height = 64;
	rec.header.tileset = 1;
	rec.rng_state = 0x99u;
	rec.local_player = 1;
	kp_netsave_netg_write(&rec, 0, "Resume", netg);
	kp_t_eq(T, kp_save_encode_with(H->f.session, H->f.identity, NULL, NULL, KP_NETSAVE_NETG, netg, sizeof netg, &bytes,
		&n, &err), 0, "%s", err.msg);
	snprintf(path, sizeof path, "%smaps/save/Resume.mlt", H->pref);
	write_bytes(T, path, bytes, n);
	snprintf(path, sizeof path, "%smaps/save/Older.mlt", H->pref);
	write_bytes(T, path, bytes, n);
	free(bytes);
	kp_t_ok(T, set_file_time(path, 1000000000) == 0, "%s's time", path);
	snprintf(path, sizeof path, "%smaps/save/Original.mlt", H->pref);
	write_net_save(T, path, 0x5151u, 0x222u, KP_NETSAVE_VERSION, 0);
	/* Create Game's list */
	m->state = KP_GLUE_PLAY_CUSTOM;
	m->expansion = 0;
	m->multiplayer = 1;
	snprintf(m->map_dir, sizeof m->map_dir, "save");
	kp_t_ok(T, ui->list_entries(ui->host, ui) > 0, "the save folder's list");
	e = listed_entry(ui, "Resume.mlt");
	kp_t_ok(T, e && e->kind == KP_UI_ENTRY_SAVE, "Koprulu's save listed");
	if (e) ui->load_entry(ui->host, ui, (int)(e - m->entries));
	kp_t_ok(T, e && e->status == 0, "and read (status %d, description %d)", e ? e->status : -1, e ? e->description : -1);
	if (e) {
		kp_t_eq_str(T, e->map, "Saved Map", "the save's map title");
		kp_t_eq_str(T, e->text, "Resume", "the display name");
		kp_t_ok(T, e->game_players == 2 && e->width == 64 && e->tileset == 1, "its players, size and tileset");
	}
	e = listed_entry(ui, "Original.mlt");
	if (e) ui->load_entry(ui->host, ui, (int)(e - m->entries));
	kp_t_ok(T, e && e->status == 1 && e->description == 66, "the original's save unreadable in the product");
	/* the lobby's resume by ID: the first file of the ID in the folder's order (Older's name comes first) */
	kp_t_ok(T, kp_flow_find_save(&H->f, 0x5151u, &out), "the save found by its ID (%s)", H->last_log);
	kp_t_ok(T, out.rng_state == 0x99u && out.local_player == 1, "its NETG");
	snprintf(path, sizeof path, "%smaps/save/Older.mlt", H->pref);
	kp_t_eq_str(T, H->f.net_load, path, "the file the game loads");
	kp_t_ok(T, !kp_flow_find_save(&H->f, 0x4444u, &out), "no save of another ID");
	kp_t_eq_str(T, H->f.net_load, "", "and no file to load");
	m->expansion = 1;
	kp_t_ok(T, !kp_flow_find_save(&H->f, 0x5151u, &out), "the original game's save is not Brood War's");
	m->expansion = 0;
	other = *H->f.identity;
	other.source_hash[0] ^= 1;
	H->f.identity = &other;
	e = listed_entry(ui, "Resume.mlt");
	if (e) ui->load_entry(ui->host, ui, (int)(e - m->entries));
	kp_t_ok(T, e && e->status == 1 && e->description == 67, "a save of other data: another version");
	H->f.identity = &H->identity;
	/* the in-game save dialogs of a network game */
	H->f.net_game = 1;
	kp_t_ok(T, ui->delete_save(ui->host, "Missing"), "a missing file is deleted");
	kp_t_eq(T, m->save_count, 2, "maps/save's saves of Koprulu");
	kp_t_ok(T, m->save_count == 2 && strcmp(m->saves[0], "Resume") == 0 && strcmp(m->saves[1], "Older") == 0,
		"newest first");
	kp_t_ok(T, !ui->delete_save(ui->host, "../Resume"), "no file outside maps/save");
	kp_t_ok(T, ui->delete_save(ui->host, "Resume"), "the delete");
	snprintf(path, sizeof path, "%smaps/save/Resume.mlt", H->pref);
	kp_t_ok(T, !file_exists(path), "removes the file");
	kp_t_ok(T, m->save_count == 1 && strcmp(m->saves[0], "Older") == 0, "and the name");
	H->f.net_game = 0;
}

/* A network game whose session fails (a core refusal; a turn that does not split, a resumed save that does not load):
 * the seat leaves Storm's game at once, so the others see it leave instead of waiting for its turns, and the console's
 * error line shows network.tbl's network error (Koprulu's: the original has no refusal). */
#define NETTBL_NETWORK_ERROR 0x51
#define ERROR_LINE (KP_UI_LINES - 1) /* the lines' error slot (kp_ui_lines.c) */

static void a_failed_network_game_leaves_storms_game(KpT *T)
{
	Host *H = logged_in_host(T, "Fenix");
	const uint8_t *text;
	play(H, PROTOSS01);
	if (!H->f.session) return;
	H->f.net_game = 1;
	H->f.net_left = 0;
	frame(H);
	kp_t_eq(T, H->f.net_left, 0, "a game that runs stays in Storm's game");
	H->f.session->failed = 1;
	snprintf(H->f.session->error, sizeof H->f.session->error, "%s", "a test refusal");
	frame(H);
	kp_t_eq(T, H->f.net_left, 1, "the failed game leaves Storm's game");
	kp_t_ok(T, strstr(H->last_log, "a test refusal") != NULL, "and says why (%s)", H->last_log);
	text = kp_ui_tbl(H->f.ui, KP_UI_NETWORK, NETTBL_NETWORK_ERROR);
	kp_t_ok(T, text != NULL && strcmp((const char *)H->f.ui->lines.text[ERROR_LINE], (const char *)text) == 0,
		"the error line: \"%s\"", (const char *)H->f.ui->lines.text[ERROR_LINE]);
	H->f.net_game = 0;
}

/* --- a network game's flow over a Storm of the test's --- */

#define LOOP_TURNS 16          /* turns in transit the loop holds: past the transit's 8 */
#define LOOP_TURN_BYTES 0x400  /* a turn's bytes: past KP_NETGAME_TURN_MAX */
#define LOOP_PEER 1            /* the peer's Storm ID and slot */
#define LOOP_GAME "Koprulu Game"
#define LOOP_PEER_NAME "Zeratul"
#define LOOP_SEED 0x5eedu
#define LOOP_SPEED 6           /* the game header's speed: the fastest */
#define PROTOSS_ROOM 9         /* the race ready room's glue state of a Protoss player */

/* Storm for a network game of which this seat alone sends: SNetReceiveTurns hands its own turns back, as Storm hands
 * every seat its own, and nothing else arrives. The peer stands in the lobby's records only. */
typedef struct {
	uint8_t turn[LOOP_TURNS][LOOP_TURN_BYTES];
	uint32_t size[LOOP_TURNS];
	int head, count;
	uint8_t current[LOOP_TURN_BYTES];
	uint32_t error;
	int leaves;
	int refuse_turns; /* SNetSendTurn fails (a fatal network error: ReportStormError of the send) */
} LoopStorm;

static LoopStorm *loop_of(void *ud) { return (LoopStorm *)ud; }

static int loop_refuse(LoopStorm *l, uint32_t error)
{
	l->error = error;
	return 0;
}

static int loop_create(void *ud, const char *name, const char *password, const char *stats, uint32_t game_type,
	uint32_t ladder_type, uint32_t mode, const void *tmpl, uint32_t tmpl_size, uint32_t max_players,
	const char *player_name, const char *player_desc, int *player_id)
{
	(void)name, (void)password, (void)stats, (void)game_type, (void)ladder_type, (void)mode, (void)tmpl;
	(void)tmpl_size, (void)max_players, (void)player_name, (void)player_desc, (void)player_id;
	return loop_refuse(loop_of(ud), KP_SNET_ERROR_PARAMETER);
}

static int loop_join(void *ud, uint32_t game_id, const char *name, const char *password, const char *player_name,
	const char *player_desc, int *player_id)
{
	(void)game_id, (void)name, (void)password, (void)player_name, (void)player_desc, (void)player_id;
	return loop_refuse(loop_of(ud), KP_SNET_ERROR_PARAMETER);
}

static int loop_leave(void *ud, uint32_t reason)
{
	(void)reason;
	loop_of(ud)->leaves++;
	return 1;
}

static int loop_enum(void *ud, int (*fn)(void *fn_ud, const KpSnetGame *g), void *fn_ud, uint32_t *hint)
{
	(void)fn, (void)fn_ud, (void)hint;
	return loop_refuse(loop_of(ud), KP_SNET_ERROR_NO_DATA);
}

static int loop_send_turn(void *ud, const void *data, uint32_t size)
{
	LoopStorm *l = loop_of(ud);
	int at = (l->head + l->count) % LOOP_TURNS;
	if (l->refuse_turns || l->count == LOOP_TURNS || size > LOOP_TURN_BYTES) return loop_refuse(l, KP_SNET_ERROR_PARAMETER);
	if (size) memcpy(l->turn[at], data, size);
	l->size[at] = size;
	l->count++;
	return 1;
}

static int loop_receive_turns(void *ud, int first, int count, const uint8_t **data, uint32_t *sizes, uint32_t *status)
{
	LoopStorm *l = loop_of(ud);
	int k;
	if (!l->count) return loop_refuse(l, KP_SNET_ERROR_NO_DATA);
	memcpy(l->current, l->turn[l->head], l->size[l->head]);
	for (k = 0; k < count; k++) {
		data[k] = NULL;
		sizes[k] = 0;
		status[k] = 0;
	}
	if (first == 0 && count > 0) { /* this seat is Storm ID 0 */
		data[0] = l->current;
		sizes[0] = l->size[l->head];
		status[0] = KP_SNET_PRESENT | KP_SNET_HAS_TURN | KP_SNET_RESPONDING;
	}
	l->head = (l->head + 1) % LOOP_TURNS;
	l->count--;
	return 1;
}

static int loop_in_transit(void *ud, uint32_t *turns)
{
	*turns = (uint32_t)loop_of(ud)->count;
	return 1;
}

static int loop_send_message(void *ud, int target, const void *data, uint32_t size)
{
	(void)ud, (void)target, (void)data, (void)size;
	return 1;
}

static int loop_receive_message(void *ud, int *sender, const uint8_t **data, uint32_t *size)
{
	(void)sender, (void)data, (void)size;
	return loop_refuse(loop_of(ud), KP_SNET_ERROR_NO_DATA);
}

static int loop_game_info(void *ud, int type, void *buf, uint32_t size, uint32_t *written)
{
	(void)type, (void)buf, (void)size, (void)written;
	return loop_refuse(loop_of(ud), KP_SNET_ERROR_PARAMETER);
}

static int loop_set_mode(void *ud, uint32_t mode, int make_private)
{
	(void)ud, (void)mode, (void)make_private;
	return 1;
}

static int loop_player_name(void *ud, int id, char *buf, uint32_t size)
{
	(void)id, (void)buf, (void)size;
	return loop_refuse(loop_of(ud), KP_SNET_ERROR_BAD_PLAYER);
}

static int loop_caps(void *ud, KpSnetCaps *caps)
{
	(void)ud;
	*caps = KP_SNET_UDP_CAPS;
	return 1;
}

static int loop_drop(void *ud, int id, uint32_t reason)
{
	(void)ud, (void)id, (void)reason;
	return 1;
}

static int loop_disconnect(void *ud, uint32_t reason)
{
	(void)ud, (void)reason;
	return 1;
}

static uint32_t loop_error(void *ud) { return loop_of(ud)->error; }

static KpNetCalls loop_calls(LoopStorm *l)
{
	KpNetCalls c;
	memset(&c, 0, sizeof c);
	c.create_game = loop_create;
	c.join_game = loop_join;
	c.leave_game = loop_leave;
	c.enum_games = loop_enum;
	c.send_turn = loop_send_turn;
	c.receive_turns = loop_receive_turns;
	c.turns_in_transit = loop_in_transit;
	c.send_message = loop_send_message;
	c.receive_message = loop_receive_message;
	c.game_info = loop_game_info;
	c.set_game_mode = loop_set_mode;
	c.player_name = loop_player_name;
	c.provider_caps = loop_caps;
	c.drop_player = loop_drop;
	c.disconnect_all = loop_disconnect;
	c.error = loop_error;
	c.ud = l;
	return c;
}

/* The flow's network game as its chat room leaves it to the start (the host's lobby of protoss01's scenario as a map
 * with Use Map Settings): the flow's own network (kp_netplay) with the game over l, this seat the host in slot 0 and
 * the peer in LOOP_PEER, both humans. The game starts at the menus' end, in the Protoss ready room. */
static void loop_network_game(Host *H, LoopStorm *l)
{
	KpT *T = H->T;
	const KpDataMission *mission = mission_entry(H, PROTOSS01);
	KpNetPlay *p = H->f.netplay;
	KpNetGame *ng = &p->game;
	const KpTemplate *ums = NULL;
	KpNetGameHooks hooks;
	KpNetGameConfig config;
	KpNetCalls calls = loop_calls(l);
	KpMapHeader mh;
	KpNetMap m;
	KpError err;
	uint8_t *chk;
	size_t size = 0;
	char path[512], why[256];
	int k;
	for (k = 0; k < H->data->templates.count && !ums; k++)
		if (strcmp(H->data->templates.list[k].name, TYPE_UMS_TEXT) == 0) ums = &H->data->templates.list[k];
	kp_t_ok(T, ums != NULL, "the " TYPE_UMS_TEXT " template");
	snprintf(path, sizeof path, "%s\\staredit\\scenario.chk", mission->directory);
	kp_t_ok(T, (chk = kp_files_need(&H->data->files, path, &size, &err)) != NULL, "%s", err.msg);
	snprintf(path, sizeof path, "%smaps", H->pref);
	kp_t_ok(T, kp_t_mkdir(path) == 0, "%s", path);
	write_custom_map(H, NEXT_FIRST, chk, size);
	k = kp_map_header(&mh, chk, size, H->data->files.profile, 0, why, sizeof why);
	free(chk);
	kp_t_ok(T, k == 0, "the map's header: %s", why);
	hooks = kp_netui_game_hooks(&p->nu);
	memset(&config, 0, sizeof config);
	snprintf(config.player_name, sizeof config.player_name, "%s", H->profile.name);
	config.templates = &H->data->templates;
	kp_t_eq(T, kp_netgame_init(ng, &calls, &hooks, &config), 0, "the network's game");
	p->game_on = 1;
	p->nu.game = ng;
	snprintf(path, sizeof path, "%smaps/" NEXT_FIRST, H->pref);
	kp_netgame_map(&m, &mh, path, &ng->hooks);
	kp_netgame_set_map(ng, &m);
	snprintf(ng->header.name, sizeof ng->header.name, LOOP_GAME);
	ng->header.max_players = mh.players;
	ng->header.speed = LOOP_SPEED;
	memcpy(ng->header.template_record, ums->record, KP_TEMPLATE_SIZE);
	ng->host = 1;
	ng->seed = LOOP_SEED;
	ng->local_storm = ng->local_slot = ng->local_index = 0;
	for (k = 0; k <= LOOP_PEER; k++) {
		KpNetSlot *s = &ng->slots[k];
		s->storm = k;
		s->type = KP_CTRL_HUMAN;
		snprintf(s->name, sizeof s->name, "%s", k ? LOOP_PEER_NAME : H->profile.name);
		ng->storm_slot[k] = (uint8_t)k;
		ng->players[k].state = 1;
	}
	H->f.ui->menus.multiplayer = 1;
	menus_end(H, KP_UI_MENU_NETWORK, PROTOSS_ROOM, 0, 0, 0);
	kp_t_ok(T, H->f.net_game && H->f.session, "the network's game stands (%s)", H->last_log);
	kp_t_eq(T, H->f.glue.screen, KP_GLUE_ROOM, "in its ready room");
}

/* The ready room's Start of a network game: the room waits for every present player's 0x54, which comes back with
 * this seat's turns. */
static void loop_room_start(Host *H)
{
	int k;
	wait_for(H, PROTOSS_ROOM_BIN, ROOM_FRAMES);
	key(H, 's');
	for (k = 0; k < END_FRAMES && !H->f.hud_on; k++) frame(H);
	kp_t_ok(H->T, H->f.hud_on, "the room's Start: the console (%s)", H->last_log);
}

static void loop_free(void *p) { free(p); }

/* A network game's replay names its humans by the lobby's names and its header by the game's name (it named every human
 * after the local player). A player who leaves in a Use Map Settings game's ready room is a lobby leave there
 * (HandlePlayerLeftLobby 0x477c20), and the original builds its game from the records after the room
 * (RunGameSession 0x467ba0's StartGame): at the room's Start the flow builds the game the room showed again without the
 * player, and the game starts at once; without a leave the room's game goes on. */
static void network_ready_room_builds_the_game_again(KpT *T)
{
	int pass;
	for (pass = 0; pass < 2; pass++) {
		LoopStorm *l = (LoopStorm *)calloc(1, sizeof *l);
		Host *H;
		KpRep rep;
		KpRepPlayer local, peer;
		KpError err;
		const KpGameSetup *s;
		kp_t_ok(T, l != NULL, "out of memory");
		kp_t_defer(T, loop_free, l); /* freed after the flow, whose end leaves Storm's game */
		H = logged_in_host(T, "Fenix");
		if (!H->f.netplay) kp_t_skip(T, "no network play (the web)");
		loop_network_game(H, l);
		/* the replay of the game the room shows */
		memset(&rep, 0, sizeof rep);
		kp_t_ok(T, kp_session_replay_file(H->f.session, NULL, &rep, &err) == 0, "the replay: %s", err.msg);
		kp_rep_player(&rep, 0, &local);
		kp_rep_player(&rep, LOOP_PEER, &peer);
		kp_t_eq_str(T, (const char *)rep.header + KP_REP_GAME_NAME, LOOP_GAME, "the header's name: the game's");
		kp_t_ok(T, local.type == KP_CTRL_HUMAN && strcmp(local.name, "Fenix") == 0, "this seat's human: %s",
			local.name);
		kp_t_ok(T, peer.type == KP_CTRL_HUMAN && strcmp(peer.name, LOOP_PEER_NAME) == 0, "the peer's: %s", peer.name);
		kp_rep_free(&rep);
		if (pass == 0) { /* the peer's Cancel in the room: Storm removes the player (event 3) */
			KpSnetEvent e;
			memset(&e, 0, sizeof e);
			e.id = KP_SNET_EVENT_REMOVED;
			e.player = LOOP_PEER;
			kp_netgame_event(&H->f.netplay->game, &e);
		}
		loop_room_start(H);
		s = &H->f.session->g->setup;
		kp_t_eq(T, H->f.glue.screen != KP_GLUE_ROOM, 1, "no ready room again");
		kp_t_ok(T, s->type[0] == KP_CTRL_HUMAN && s->storm[0] == 0, "this seat plays");
		if (pass == 0) {
			kp_t_ok(T, !(s->type[LOOP_PEER] == KP_CTRL_HUMAN && s->storm[LOOP_PEER] == LOOP_PEER),
				"the game built again without the peer (type %d, Storm ID %d)", (int)s->type[LOOP_PEER],
				(int)s->storm[LOOP_PEER]);
		} else {
			kp_t_ok(T, s->type[LOOP_PEER] == KP_CTRL_HUMAN && s->storm[LOOP_PEER] == LOOP_PEER
				&& strcmp(s->name[LOOP_PEER], LOOP_PEER_NAME) == 0, "the room's game goes on with the peer");
		}
		frames(H, 10);
		kp_t_eq(T, H->f.net_left, 0, "the game runs on (%s)", H->last_log);
		kp_t_eq(T, H->quits, 0, NULL);
	}
}

/* A fatal network error (ReportNetError 0x474a00: a turn Storm does not send) leaves Storm's game and exits the glue to
 * the main menu (glue state 25): in the race ready room its report shows over the room, whose end after the popup's Ok
 * (100) leads to the main menu; in the game the report shows over the stopped game in rez\dlgFatal.bin with Storm's
 * error (ShowFatalErrorDialog 0x43ec00), and its OK ends the game (RequestGameExit 0x444e00(4)) for the main menu. */
static void a_fatal_network_error_returns_to_the_main_menu(KpT *T)
{
	int pass;
	for (pass = 0; pass < 2; pass++) {
		LoopStorm *l = (LoopStorm *)calloc(1, sizeof *l);
		Host *H;
		char name[128];
		int k;
		kp_t_ok(T, l != NULL, "out of memory");
		kp_t_defer(T, loop_free, l);
		H = logged_in_host(T, "Fenix");
		if (!H->f.netplay) kp_t_skip(T, "no network play (the web)");
		loop_network_game(H, l);
		if (pass == 0) { /* the room's turn steps fail before its players are ready */
			l->refuse_turns = 1;
			wait_for(H, "rez\\glupoksplit.bin", ROOM_FRAMES);
			kp_t_ok(T, H->f.netplay->game.error_latch, "the report latched");
			kp_t_eq(T, H->f.glue.screen, KP_GLUE_ROOM, "the room under the report");
			key(H, '\r');
			wait_for(H, "rez\\glumain.bin", END_FRAMES);
			kp_t_eq(T, H->f.ui->result, KP_UI_ROOM_EXIT, "the room ended with 100");
		} else {
			loop_room_start(H);
			frames(H, 10);
			kp_t_ok(T, H->f.hud_on && !H->f.net_left, "the game runs (%s)", H->last_log);
			uint32_t frame_at;
			l->refuse_turns = 1;
			wait_for(H, "rez\\dlgfatal.bin", END_FRAMES);
			kp_t_ok(T, H->f.netplay->game.error_latch, "the report latched");
			kp_t_ok(T, strstr(H->f.ui->fatal.detail, "Storm error") != NULL, "Storm's error: \"%s\"",
				H->f.ui->fatal.detail);
			frame_at = (uint32_t)kp_scalar(H->f.session->g, KP_FRAME);
			frames(H, SETTLE_FRAMES);
			kp_t_ok(T, H->f.hud_on && H->f.fatal_held, "the game under it (%s)", H->last_log);
			kp_t_eq(T, (uint32_t)kp_scalar(H->f.session->g, KP_FRAME), frame_at, "stopped");
			key(H, '\r');
			for (k = 0; k < END_FRAMES && H->f.hud_on; k++) frame(H);
			kp_t_ok(T, !H->f.hud_on, "its OK ended the game (%s)", H->last_log);
			frames(H, SETTLE_FRAMES);
			kp_t_eq_str(T, top(H, name, sizeof name), "rez\\glumain.bin", "the main menu, the report shown once");
			kp_t_eq(T, H->f.ui->menus.net_error, 0, "nothing deferred");
		}
		kp_t_eq(T, H->f.ui->menus.state, KP_GLUE_MAIN, "the main menu's state");
		kp_t_eq(T, kp_ui_menus_glue_exit(H->f.ui, 0), 0, "the glue's exit taken");
		kp_t_eq(T, H->quits, 0, NULL);
	}
}

/* The chat room's preview world through the flow (KpUiNet.lobby_preview -> KpNetUi.preview -> the flow's
 * kp_map_preview_load of the lobby's map, the user's copy): the map's size and its neutral units, released by the room's
 * close (lobby_preview_release); a map that does not load is ReportNetError 0x61, fatal: the report latched, its popup
 * over the room, the glue's exit to the main menu. */
static void a_lobby_map_preview_goes_through_the_flow(KpT *T)
{
	LoopStorm *l = (LoopStorm *)calloc(1, sizeof *l);
	KpUiPreview out;
	KpNetPlay *p;
	Host *H;
	kp_t_ok(T, l != NULL, "out of memory");
	kp_t_defer(T, loop_free, l);
	H = logged_in_host(T, "Fenix");
	if (!H->f.netplay) kp_t_skip(T, "no network play (the web)");
	loop_network_game(H, l);
	p = H->f.netplay;
	memset(&out, 0, sizeof out);
	kp_t_eq(T, p->net.lobby_preview(p->net.host, &out), 1, "the preview world (%s)", H->last_log);
	kp_t_ok(T, H->f.preview.loaded && out.megatiles == H->f.preview.world.megatiles, "the flow's");
	{ /* the lobby's map: Protoss 1's scenario as the user's copy */
		const KpDataMission *mission = mission_entry(H, PROTOSS01);
		KpMapHeader mh;
		KpError err;
		uint8_t *chk;
		size_t size = 0;
		char path[512], why[256];
		snprintf(path, sizeof path, "%s\\staredit\\scenario.chk", mission->directory);
		kp_t_ok(T, (chk = kp_files_need(&H->data->files, path, &size, &err)) != NULL, "%s", err.msg);
		kp_t_eq(T, kp_map_header(&mh, chk, size, H->data->files.profile, 0, why, sizeof why), 0, "%s", why);
		free(chk);
		kp_t_ok(T, out.map_w == mh.width && out.map_h == mh.height, "the lobby's map's size (%dx%d)", out.map_w, out.map_h);
	}
	kp_t_ok(T, out.mmni != NULL && out.rgb != NULL && out.cycled != NULL, "its tileset's colours");
	p->net.lobby_preview_release(p->net.host);
	kp_t_ok(T, !H->f.preview.loaded, "released");
	snprintf(p->game.map_path, sizeof p->game.map_path, "%smaps/missing.scm", H->pref);
	kp_t_eq(T, p->net.lobby_preview(p->net.host, &out), 0, "a map that does not load");
	kp_t_ok(T, p->game.error_latch, "the report latched");
	kp_t_eq(T, kp_ui_menus_glue_exit(H->f.ui, 0), 1, "the glue exits");
	wait_for(H, "rez\\glupok.bin", ROOM_FRAMES);
	key(H, '\r');
	wait_for(H, "rez\\glumain.bin", END_FRAMES);
	kp_t_eq(T, H->f.ui->menus.state, KP_GLUE_MAIN, "the main menu's state");
	kp_t_eq(T, H->quits, 0, NULL);
}

#define SOCKETS_FAILED "the sockets failed" /* kp_netplay's line for a socket that fails */
#define PUMP_FAIL_MS 2000 /* the pump thread sees a failed socket within (it wakes at once) */
#define FREE_PORT_TRIES 32
#define MAX_FD 1024

#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
/* The flow's network with the UDP provider chosen on two free ports of 127.0.0.1 (a map of one host, as Koprulu's own
 * games on one machine); 1 chosen. */
static int choose_udp_on_free_ports(Host *H)
{
	KpNetPlay *p = H->f.netplay;
	int attempt;
	for (attempt = 0; attempt < FREE_PORT_TRIES; attempt++) {
		uint32_t base = 20000u + (kp_net_sock_now() / 2u + (uint32_t)attempt * 7919u) % 20000u * 2u;
		memset(&p->config.sock, 0, sizeof p->config.sock);
		p->config.sock.map_base = (uint16_t)base;
		p->config.sock.map_hosts = 1;
		p->config.sock.map_self = 0;
		if (p->net.select_provider(p->net.host, KP_SNP_UDP_ID)) return 1;
	}
	return 0;
}

/* The descriptor of this process's UDP socket bound to port, -1 none */
static int udp_socket_at(uint16_t port)
{
	int fd;
	for (fd = 0; fd < MAX_FD; fd++) {
		struct sockaddr_in a;
		socklen_t len = sizeof a;
		int type = 0;
		socklen_t type_len = sizeof type;
		if (getsockname(fd, (struct sockaddr *)&a, &len) != 0 || a.sin_family != AF_INET || ntohs(a.sin_port) != port)
			continue;
		if (getsockopt(fd, SOL_SOCKET, SO_TYPE, &type, &type_len) == 0 && type == SOCK_DGRAM) return fd;
	}
	return -1;
}
#endif

/* A provider's socket that fails (a phone's network gone): the reads stop until the provider is chosen again, the
 * player is told network.tbl's network error once and the log says it once (every frame logged it, and a join's waits
 * spun on it); choosing the provider again reads again. The socket is made to fail by a descriptor that is no socket
 * in its place. */
static void a_failing_socket_stops_the_reads(KpT *T)
{
#if defined(_WIN32) || defined(__EMSCRIPTEN__)
	kp_t_skip(T, "a socket made to fail by dup2 (POSIX sockets only)");
#else
	Host *H = logged_in_host(T, "Fenix");
	KpNetPlay *p = H->f.netplay;
	int fd, null_fd, k;
	kp_t_ok(T, p != NULL, "the flow's network");
	kp_t_ok(T, choose_udp_on_free_ports(H), "the UDP provider on free ports (%s)", H->last_log);
	kp_t_ok(T, p->sock && p->snet && !p->sock_failed, "its sockets");
	frames(H, 10);
	kp_t_ok(T, !p->sock_failed, "the sockets read");
	fd = udp_socket_at(kp_net_sock_port(p->sock, KP_NET_SOCKET_GAME));
	kp_t_ok(T, fd >= 0, "socket B's descriptor");
	null_fd = open("/dev/null", O_RDONLY);
	kp_t_ok(T, null_fd >= 0 && dup2(null_fd, fd) == fd, "no socket in socket B's place");
	close(null_fd);
	H->watch = SOCKETS_FAILED;
	H->f.ui->menus.net_error = 0;
	/* the pump thread reads the sockets: woken from its wait, it fails on the descriptor, and a frame tells it */
	kp_t_ok(T, p->pump != NULL, "Storm pumped on its thread");
	(void)kp_net_sock_wake(p->sock);
	for (k = 0; k < PUMP_FAIL_MS && !p->sock_failed; k++) {
		kp_net_sock_sleep(1);
		frame(H);
	}
	kp_t_ok(T, p->sock_failed, "the failure seen (%s)", H->last_log);
	kp_t_ok(T, p->pump == NULL, "the thread joined: the frames pump runs Storm's timers on");
	kp_t_eq(T, H->watched, 1, "and logged");
	kp_t_eq(T, H->f.ui->menus.net_error, NETTBL_NETWORK_ERROR, "the player told the network error");
	H->f.ui->menus.net_error = 0;
	frames(H, 60);
	kp_t_eq(T, H->watched, 1, "logged once");
	kp_t_eq(T, H->f.ui->menus.net_error, 0, "told once");
	kp_t_ok(T, choose_udp_on_free_ports(H), "the provider chosen again (%s)", H->last_log);
	kp_t_ok(T, p->sock && !p->sock_failed, "its new sockets");
	frames(H, 10);
	kp_t_ok(T, !p->sock_failed && H->watched == 1, "read again");
	H->watch = NULL;
#endif
}

/* Storm's pump on a thread of its own (the architecture review of 2026-11-27): with no frame run, the provider's stack
 * answers a peer's connect (control command 1) with Storm's accept (command 2), as Storm's pump thread does while the
 * game's loop is busy (a mission's load, a slow frame); before, nothing answered until the next frame. */
#define STORM_CONTROL 0 /* Storm's packet classes and control commands (atlas S02, docs/formats/storm-udp.md) */
#define STORM_CONNECT 1
#define STORM_ACCEPT 2
#define STORM_PING 4
#define STORM_HEADER 12
#define PUMP_ANSWER_MS 2000
#define CONNECT_RESEND_MS 50
#define PUMP_STRESS_MS 300

static void storm_answers_without_frames(KpT *T)
{
#if defined(_WIN32) || defined(__EMSCRIPTEN__)
	kp_t_skip(T, "a peer's socket in the test (POSIX sockets only)");
#else
	Host *H = logged_in_host(T, "Tassadar");
	KpNetPlay *p = H->f.netplay;
	struct sockaddr_in a;
	struct pollfd w;
	uint8_t packet[STORM_HEADER + 4], got[KP_SNP_DATAGRAM];
	uint32_t until;
	int fd, accepted = 0;
	kp_t_ok(T, choose_udp_on_free_ports(H), "the UDP provider on free ports (%s)", H->last_log);
	kp_t_ok(T, p->pump != NULL && p->wakes, "Storm pumped on its thread, with wakes (%s)", H->last_log);
	fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	kp_t_ok(T, fd >= 0, "the peer's socket");
	memset(&a, 0, sizeof a);
	a.sin_family = AF_INET;
	a.sin_addr.s_addr = htonl(KP_NET_LOOPBACK_IP);
	kp_t_ok(T, bind(fd, (const struct sockaddr *)&a, sizeof a) == 0, "bound");
	memset(packet, 0, sizeof packet);
	kp_net_put16(packet + 2, sizeof packet);
	packet[8] = STORM_CONTROL;
	packet[9] = STORM_CONNECT;
	packet[10] = KP_SNET_NO_ID;
	kp_net_put32(packet + STORM_HEADER, 1);
	kp_net_checksum(packet, sizeof packet);
	a.sin_port = htons(kp_net_sock_port(p->sock, KP_NET_SOCKET_GAME));
	until = kp_net_sock_now() + PUMP_ANSWER_MS;
	/* sent again until answered, as a joiner resends it: Storm files no datagram before its first timer pass made
	 * the local record */
	while (!accepted && (int32_t)(until - kp_net_sock_now()) > 0) {
		long n;
		kp_t_ok(T, sendto(fd, packet, sizeof packet, 0, (const struct sockaddr *)&a, sizeof a) == (long)sizeof packet,
			"the connect sent");
		w.fd = fd;
		w.events = POLLIN;
		w.revents = 0;
		if (poll(&w, 1, CONNECT_RESEND_MS) <= 0) continue;
		n = (long)recv(fd, got, sizeof got, 0);
		accepted = n >= STORM_HEADER && got[8] == STORM_CONTROL && got[9] == STORM_ACCEPT;
	}
	kp_t_ok(T, accepted, "Storm's accept within %d ms, no frame run", PUMP_ANSWER_MS);
	/* the main thread's calls into Storm (the game list, the turns in transit) and its frames while the thread files
	 * the peer's pings: one lock between them (tools/ci.sh's full mode runs this under ThreadSanitizer) */
	packet[9] = STORM_PING;
	kp_net_put16(packet + 2, STORM_HEADER);
	kp_net_checksum(packet, STORM_HEADER);
	until = kp_net_sock_now() + PUMP_STRESS_MS;
	while ((int32_t)(until - kp_net_sock_now()) > 0) {
		uint32_t transit = 0;
		kp_t_ok(T, sendto(fd, packet, STORM_HEADER, 0, (const struct sockaddr *)&a, sizeof a) == STORM_HEADER, "a ping");
		while (recv(fd, got, sizeof got, MSG_DONTWAIT) > 0) {
		}
		kp_netgame_enum(&p->game);
		(void)p->game.net.turns_in_transit(p->game.net.ud, &transit);
		frame(H);
	}
	close(fd);
	kp_t_ok(T, !p->sock_failed, "the sockets read on");
#endif
}

/* Play Custom's Import (kp_flow_import_map): Import asks the host's picker for a map; the picked map is copied into
 * the user's maps folder (made) under the picked file's name, ".scm" or ".scx" kept in lower case, a taken name
 * numbered, a name without either given the screen's (".scm" without Brood War), and the list shows the copy
 * selected; in a listed folder the copy goes to the user's folder of it, made level by level; a file that is no map
 * is refused with the Ok popup and removed again. */
#define CUSTOM_MAP_LIST 5
#define CUSTOM_IMPORT 44

static void play_custom_imports_a_map(KpT *T)
{
	Host *H = logged_in_host(T, "Fenix");
	const KpDataMission *m = mission_entry(H, PROTOSS01);
	KpUi *ui = H->f.ui;
	char path[512], name[64];
	uint8_t *chk, *bytes;
	size_t size = 0, n;
	KpError e;
	int32_t list;
	snprintf(path, sizeof path, "%s\\staredit\\scenario.chk", m->directory);
	kp_t_ok(T, (chk = kp_files_need(&H->data->files, path, &size, &e)) != NULL, "%s", e.msg);
	snprintf(path, sizeof path, "%ssource.scm", H->pref);
	write_map_at(H, path, chk, size);
	free(chk);
	kp_t_ok(T, (bytes = kp_read_file(path, &size)) != NULL, "%s", path);
	to_play_custom(H, 0);
	kp_t_ok(T, open_control(ui, CUSTOM_IMPORT) >= 0, "Play Custom's Import with the host's picker");
	ui->import_file(ui->host);
	kp_t_eq(T, H->pick_kind, KP_FLOW_PICK_MAP, "Import asks for a map");
	kp_t_eq(T, kp_flow_import_map(&H->f, "content://x/document/primary%3ADownload%2FMy%20Map.SCM", bytes, size, name,
		sizeof name), 0, "a map imported (%s)", H->last_log);
	kp_t_eq_str(T, name, "My Map.scm", "named by the URI's last part");
	snprintf(path, sizeof path, "%smaps/My Map.scm", H->pref);
	kp_t_ok(T, file_equals(path, bytes, size), "copied byte for byte into the user's maps folder");
	list = open_control(ui, CUSTOM_MAP_LIST);
	kp_t_ok(T, list >= 0 && kp_ui_list_selection(ui, list) != KP_UI_LIST_NONE
		&& strcmp((const char *)kp_ui_list_text(ui, list, kp_ui_list_selection(ui, list)), "My Map.scm") == 0,
		"listed and selected");
	kp_t_eq(T, kp_flow_import_map(&H->f, "/home/me/My Map.scm", bytes, size, name, sizeof name), 0, "again");
	kp_t_eq_str(T, name, "My Map (2).scm", "a taken name gets a copy's number");
	kp_t_eq(T, kp_flow_import_map(&H->f, "content://media/external/file/42", bytes, size, name, sizeof name), 0,
		"a name without an extension");
	kp_t_eq_str(T, name, "42.scm", "the original's maps' extension without Brood War");
	snprintf(ui->menus.map_dir, sizeof ui->menus.map_dir, "sub/deeper");
	kp_t_eq(T, kp_flow_import_map(&H->f, "/home/me/Fixture.scx", bytes, size, name, sizeof name), 0, "into a folder");
	snprintf(path, sizeof path, "%smaps/sub/deeper/Fixture.scx", H->pref);
	kp_t_ok(T, file_equals(path, bytes, size), "the listed folder's copy, its folders made");
	ui->menus.map_dir[0] = 0;
	kp_t_ok(T, kp_flow_import_map(&H->f, "/tmp/notes.scm", (const uint8_t *)"no map", 6, name, sizeof name) != 0
		&& name[0] == 0, "a file that is no map refused");
	snprintf(path, sizeof path, "%smaps/notes.scm", H->pref);
	kp_t_ok(T, kp_read_file(path, &n) == NULL, "and removed again");
	wait_for(H, "rez\\glupok.bin", MENU_FRAMES);
	kp_t_eq(T, H->picks, 1, NULL);
	free(bytes);
}

/* The maps' class (kp_map_class.h, the whole file read and its signature checked) is asked only by a template that
 * reads it (ValidateGameTemplate 0x45b700's Ladder, Tournament and Server; none offered offline): not by the list's
 * picks nor by an ordinary game's Ok. Without the 1.17 executable's key every map is of class 0, which a Ladder
 * template (types 0x11..0x16, 0x447ed0) refuses with gluAll 52. */
#define TEMPLATE_LADDER 0x11
#define STR_CREATE_FAILED 52
static int class_asks;
static uint32_t (*flow_map_class)(void *host, KpUi *ui, int index);

static uint32_t counted_map_class(void *host, KpUi *ui, int index)
{
	class_asks++;
	return flow_map_class(host, ui, index);
}

static void map_class_is_asked_only_by_the_templates_that_read_it(KpT *T)
{
	Host *H = logged_in_host(T, "Fenix");
	const KpDataMission *m = mission_entry(H, PROTOSS01);
	KpUi *ui = H->f.ui;
	char path[512];
	uint8_t *chk, type;
	size_t size = 0;
	KpError e;
	int32_t types;
	int k, ums = -1;
	snprintf(path, sizeof path, "%s\\staredit\\scenario.chk", m->directory);
	kp_t_ok(T, (chk = kp_files_need(&H->data->files, path, &size, &e)) != NULL, "%s", e.msg);
	snprintf(path, sizeof path, "%smaps", H->pref);
	kp_t_ok(T, kp_t_mkdir(path) == 0, "%s", path);
	write_custom_map(H, "class.scm", chk, size);
	free(chk);
	to_play_custom(H, 0);
	flow_map_class = ui->map_class;
	kp_t_ok(T, flow_map_class != NULL, "the flow's map_class");
	ui->map_class = counted_map_class;
	class_asks = 0;
	types = open_control(ui, CUSTOM_TYPE_LIST);
	for (k = 0; types >= 0 && k < kp_ui_list_count(ui, types); k++) {
		if (strcmp((const char *)kp_ui_list_text(ui, types, k), TYPE_UMS_TEXT) == 0) ums = k;
	}
	kp_t_ok(T, ums >= 0, "Use Map Settings offered");
	kp_ui_list_select(ui, types, ums);
	frames(H, CUSTOM_PICK_FRAMES);
	kp_t_eq(T, class_asks, 0, "the list's map read without its class");
	kp_t_eq(T, (int)ui->menus.entries[kp_ui_list_data(ui, ui->menus.map_list,
		kp_ui_list_selection(ui, ui->menus.map_list))].map_class, 0, "an ordinary map's until asked");
	type = ui->menus.create.record[0];
	ui->menus.create.record[0] = TEMPLATE_LADDER;
	click(H, CUSTOM_OK);
	wait_for(H, "rez\\glupok.bin", MENU_FRAMES);
	kp_t_eq(T, class_asks, 1, "a Ladder template asks the map's class");
	kp_t_eq_str(T, ui->menus.popup_text, (const char *)kp_ui_tbl(ui, KP_UI_GLUALL, STR_CREATE_FAILED),
		"class 0 without the key: refused");
	key(H, '\r'); /* the popup's Ok */
	wait_for(H, CUSTOM_SCREEN, MENU_FRAMES);
	ui->menus.create.record[0] = type;
	click(H, CUSTOM_OK);
	room_start(H);
	kp_t_eq(T, H->f.current, KP_FLOW_CUSTOM, "Use Map Settings' game");
	kp_t_eq(T, class_asks, 1, "and its Ok asks no class");
}

/* --map plays a custom map at once without the glue (kp_flow_play_map): its template by name, ASCII case folded, Use
 * Map Settings keeping the map's slots (protoss01's scenario as a map: no ready room without the glue); Melee by
 * default, the player and a computer in each other slot of the map, all Random as Play Custom offers them (an
 * install's 4-player map). A type templates.lst lacks, or a file that is no map, refuses to start. */
#define MAP_MELEE "maps/(4)Lost Civilization.scm"
#define MAP_MELEE_SLOTS 4

static void map_flag_plays_a_custom_map_without_the_glue(KpT *T)
{
	const char *root = getenv("KP_TEST_ORIGINAL");
	Host *H = new_host(T);
	const KpDataMission *m = mission_entry(H, PROTOSS01);
	char path[512], err[256];
	uint8_t *chk;
	size_t size = 0;
	KpError e;
	int p, humans = 0, computers = 0;
	snprintf(path, sizeof path, "%s\\staredit\\scenario.chk", m->directory);
	kp_t_ok(T, (chk = kp_files_need(&H->data->files, path, &size, &e)) != NULL, "%s", e.msg);
	snprintf(path, sizeof path, "%smaps", H->pref);
	kp_t_ok(T, kp_t_mkdir(path) == 0, "%s", path);
	write_custom_map(H, NEXT_FIRST, chk, size);
	free(chk);
	snprintf(H->flags.map, sizeof H->flags.map, "%smaps/%s", H->pref, NEXT_FIRST);
	snprintf(H->flags.map_template, sizeof H->flags.map_template, "use map settings");
	init(H);
	kp_t_eq(T, H->f.glue_on, 0, "no glue");
	kp_t_ok(T, H->f.playing && H->f.hud_on, "the console at once (%s)", H->last_log);
	kp_t_eq(T, H->f.current, KP_FLOW_CUSTOM, "the custom map's game");
	kp_t_eq(T, H->flags.play, 1, "in play mode");
	kp_t_ok(T, kp_template_ums(H->f.custom_setup.template_record), "with the Use Map Settings template");
	kp_t_eq_str(T, current(H), NEXT_FIRST, "named by its file");
	frames(H, 100);
	kp_t_ok(T, H->ticks > 0, "the game steps");
	game_event(H, KP_EVENT_GAME_EXIT, KP_UI_EXIT_PROGRAM, NULL);
	kp_t_eq(T, H->quits, 1, "Exit Program ends the program");
	/* refusals */
	if (!(H = new_host(T))) return;
	snprintf(H->flags.map, sizeof H->flags.map, "%smaps/%s", H->pref, NEXT_FIRST);
	snprintf(H->flags.map_template, sizeof H->flags.map_template, "Nonsense");
	kp_t_ok(T, kp_flow_init(&H->f, err, sizeof err) != 0, "no such game type");
	kp_t_eq_str(T, err, "no game type Nonsense in templates.lst", NULL);
	if (!(H = new_host(T))) return;
	snprintf(H->flags.map, sizeof H->flags.map, "%smaps/missing.scm", H->pref);
	kp_t_ok(T, kp_flow_init(&H->f, err, sizeof err) != 0, "no such map");
	kp_t_ok(T, err[0] != 0, "says why");
	/* Melee on an install's map */
	if (!root || !root[0]) kp_t_skip(T, "no KP_TEST_ORIGINAL (the melee map)");
	if (!(H = new_host(T))) return;
	snprintf(H->flags.map, sizeof H->flags.map, "%s/%s", root, MAP_MELEE);
	init(H);
	kp_t_ok(T, H->f.playing && H->f.hud_on, "the melee game at once (%s)", H->last_log);
	kp_t_eq(T, H->f.custom_setup.template_record[KP_TEMPLATE_VICTORY], KP_VICTORY_MELEE, "Melee by default");
	for (p = 0; p < 8; p++) {
		int64_t type = H->f.custom_setup.type[p];
		humans += type == KP_CTRL_HUMAN;
		computers += type == KP_CTRL_LOBBY_COMPUTER;
		if (type == KP_CTRL_HUMAN || type == KP_CTRL_LOBBY_COMPUTER) { /* resolved by the start (0x478ee0) */
			kp_t_eq(T, H->f.custom_setup.race[p], KP_RACE_RANDOM, "slot %d Random in the lobby", p);
			kp_t_ok(T, H->f.session->g->setup.race[p] <= 2, "slot %d's race in the game: %lld", p,
				(long long)H->f.session->g->setup.race[p]);
		}
	}
	kp_t_eq(T, humans, 1, "the player");
	kp_t_eq(T, computers, MAP_MELEE_SLOTS - 1, "a computer in each other slot");
	frames(H, 100);
	kp_t_ok(T, H->ticks > 0, "the game steps");
}

/* A seat's main image's bounds (img.x, img.y: the seat's redraw refreshes them). */
static void unit_bounds(KpGame *g, int64_t unit, int32_t *x, int32_t *y)
{
	int32_t i = g->w->spr.main[g->u.sprite[unit]];
	*x = g->w->img.x[i];
	*y = g->w->img.y[i];
}

#define SEAT_SAVE "seat z\303\251ro" /* the profile's name, in UTF-8 */
#define SEAT_SAVE_FRAMES 30          /* a queued command's turns through the hub, and the save's write */
/* SendSaveGameCommand 0x4740e0's {0x06, time, name}: the name in windows-1252 */
static const uint8_t SEAT_SAVE_COMMAND[] = { 0x06, 0, 0, 0, 0, 's', 'e', 'a', 't', ' ', 'z', 0xe9, 'r', 'o', 0 };

/* Whether the interface's saves list (kp_glue_saves) has name. */
static int has_save(const KpUi *ui, const char *name)
{
	int k;
	for (k = 0; k < ui->menus.save_count; k++) {
		if (strcmp(ui->menus.saves[k], name) == 0) return 1;
	}
	return 0;
}

/* --seats: each hidden seat runs its machine's frame without the window (its console, its view's terrain, its images'
 * bounds), and a seat shown again has the interface, console and view it had. */
static void seats_keep_their_consoles(KpT *T)
{
	static const uint8_t RIGHT_CLICK[10] = { 0x14, 0x14, 0x05, 0x56, 0x04, 0, 0, 0xe4, 0, 0 }; /* to (1300, 1110) */
	Host *H = new_host(T);
	KpMission m;
	KpError err;
	KpUi *first, *second;
	KpGame *g0, *g1;
	char path[512], why[256], menu[128], name[128];
	int64_t units[1], cam_x, cam_y;
	int32_t x0, y0, x1, y1, start_x, start_y;
	uint16_t ref;
	uint8_t select[4];
	if (!kp_oracle_mission(".", "scenarios/burrowed_computer", &m, &err)) kp_t_skip(T, "%s", err.msg);
	snprintf(path, sizeof path, "%smaps", H->pref);
	kp_t_ok(T, kp_t_mkdir(path) == 0, "%s", path);
	write_custom_map(H, "seats.scm", m.blob, m.chk_size);
	kp_mission_free(&m);
	snprintf(H->flags.map, sizeof H->flags.map, "%smaps/seats.scm", H->pref);
	snprintf(H->flags.map_template, sizeof H->flags.map_template, "use map settings");
	H->flags.seats = 2;
	init(H);
	kp_t_ok(T, H->f.playing && H->f.hud_on && H->f.seats.count == 2, "two seats in play (%s)", H->last_log);
	first = H->f.ui;
	second = H->f.seat_views[1].ui;
	kp_t_ok(T, second != NULL && second != first && H->f.seat_views[1].hud_on, "the hidden seat's own console");
	g0 = H->f.seats.session[0]->g;
	g1 = H->f.seats.session[1]->g;
	kp_t_eq(T, kp_t_list_units(g0, KP_LIST_ACTIVE, units, 1), 1, "a unit");
	unit_bounds(g1, units[0], &start_x, &start_y);
	/* seat 0's Marine walks away; both seats run the order */
	ref = kp_ui_unit_ref(g0, units[0]);
	select[0] = 0x09;
	select[1] = 1;
	select[2] = (uint8_t)(ref & 0xff);
	select[3] = (uint8_t)(ref >> 8);
	kp_t_eq(T, kp_session_command(H->f.session, select, sizeof select, -1), 0, "the select queued");
	kp_t_eq(T, kp_session_command(H->f.session, RIGHT_CLICK, sizeof RIGHT_CLICK, -1), 0, "the right click queued");
	frames(H, 120);
	kp_t_ok(T, !H->f.seats.stopped && kp_scalar(g1, KP_FRAME) == kp_scalar(g0, KP_FRAME),
		"the seats at one frame: %s", H->f.seats.report);
	unit_bounds(g0, units[0], &x0, &y0);
	unit_bounds(g1, units[0], &x1, &y1);
	kp_t_ok(T, x0 != start_x || y0 != start_y, "the Marine moved (%d,%d)", (int)x0, (int)y0);
	kp_t_ok(T, x1 == x0 && y1 == y0, "the hidden seat's redraw refreshed its bounds: (%d,%d), shown (%d,%d)", (int)x1,
		(int)y1, (int)x0, (int)y0);
	kp_t_eq(T, second->now, first->now, "the hidden console's frame ran");
	/* a save on seat 0 changes the profile's saves, which seat 1's dialogs list once it is shown. Its command goes
	 * through the hub's dispatcher, so it names the save in windows-1252 as a multiplayer game's does
	 * (kp_ui_saveload.c), and the profile's file is named in UTF-8 */
	kp_t_eq(T, kp_session_command(H->f.session, SEAT_SAVE_COMMAND, sizeof SEAT_SAVE_COMMAND, -1), 0, "the save queued");
	frames(H, SEAT_SAVE_FRAMES);
	kp_t_ok(T, has_save(first, SEAT_SAVE), "saved on seat 0 (%s)", H->last_log);
	kp_t_ok(T, !has_save(second, SEAT_SAVE), "the hidden seat's list as its console started");
	/* seat 1 shown: its own interface, its game menu open; seat 0 and back keeps it */
	cam_x = H->f.seat_views[1].cam_x;
	cam_y = H->f.seat_views[1].cam_y;
	kp_t_eq(T, kp_flow_show_seat(&H->f, 1, why, sizeof why), 0, "%s", why);
	kp_t_ok(T, H->f.ui == second && H->f.seat_views[0].ui == first && H->f.seat_views[1].ui == NULL, "the consoles swapped");
	kp_t_ok(T, has_save(second, SEAT_SAVE), "seat 1's saves as the profile has them");
	kp_t_ok(T, H->f.cam_x == cam_x && H->f.cam_y == cam_y, "the seat's view");
	kp_hud_key(H->f.ui, 1, KP_HUD_VK_F10, 0);
	kp_hud_key(H->f.ui, 0, KP_HUD_VK_F10, 0);
	frames(H, SETTLE_FRAMES);
	snprintf(menu, sizeof menu, "%s", top(H, name, sizeof name));
	kp_t_eq_str(T, menu, "rez\\gamemenu.bin", "seat 1's game menu");
	kp_t_eq(T, kp_flow_show_seat(&H->f, 0, why, sizeof why), 0, "%s", why);
	kp_t_ok(T, H->f.ui == first, "seat 0's interface again");
	kp_t_ok(T, strcmp(top(H, name, sizeof name), menu) != 0, "without seat 1's menu");
	frames(H, 10);
	kp_t_eq(T, kp_flow_show_seat(&H->f, 1, why, sizeof why), 0, "%s", why);
	kp_t_eq_str(T, top(H, name, sizeof name), menu, "seat 1's menu kept");
	frames(H, 10);
	kp_t_ok(T, !H->f.seats.stopped, "no difference: %s", H->f.seats.report);
	game_event(H, KP_EVENT_GAME_EXIT, KP_UI_EXIT_PROGRAM, NULL);
	kp_t_eq(T, H->quits, 1, "Exit Program ends the program");
}

/* The in-game menus' Exit Program ends the program at once, or with exit_to_menu (the web's page) opens the main
 * menu. */
static void exit_program_ends_the_program_or_opens_the_main_menu(KpT *T)
{
	Host *H = glue_host(T, 0);
	play(H, EXPANSION_PROTOSS01);
	game_event(H, KP_EVENT_GAME_EXIT, KP_UI_EXIT_PROGRAM, NULL);
	kp_t_eq(T, H->quits, 1, "the program ends");
	kp_t_eq(T, H->f.after_game, 0, "no glue after it");
	if (!(H = glue_host(T, 0))) return;
	H->f.exit_to_menu = 1;
	play(H, EXPANSION_PROTOSS01);
	game_event(H, KP_EVENT_GAME_EXIT, KP_UI_EXIT_PROGRAM, NULL);
	glue_sleep(H);
	kp_t_eq(T, H->quits, 0, "the page goes on");
	in_menus(H, KP_GLUE_MAIN, "Exit Program on the web");
}

/* The main menu's Exit: the program's end (the main menu behind it), or with exit_to_menu the main menu again. */
static void glue_exit_ends_the_program_or_opens_the_main_menu(KpT *T)
{
	Host *H = glue_host(T, 0);
	menus_end(H, KP_UI_MENU_EXIT, KP_GLUE_MAIN, 0, 0, 0);
	kp_t_eq(T, H->quits, 1, "the program ends");
	in_menus(H, KP_GLUE_MAIN, "Exit");
	H->f.exit_to_menu = 1;
	menus_end(H, KP_UI_MENU_EXIT, KP_GLUE_MAIN, 0, 0, 0);
	kp_t_eq(T, H->quits, 1, "the page goes on");
	in_menus(H, KP_GLUE_MAIN, "Exit on the web");
}

/* A language's name matches the installed language of its language and country, else the first of its language. */
static void languages_match_the_systems_names(KpT *T)
{
	static const struct { const char *wanted; const char *id; } CASES[] = {
		{ "ru_RU", "ruRU" }, { "ru-RU", "ruRU" }, { "ruRU", "ruRU" }, { "ru", "ruRU" }, { "RU_ua", "ruRU" },
		{ "es-MX", "esMX" }, { "es_AR", "esES" }, { "es", "esES" }, { "pt_PT", "ptBR" }, { "en_GB", "enUS" },
		{ "de_DE", "" }, { "x", "" }, { "", "" },
	};
	static const char *const IDS[] = { "enUS", "deDE", "esES", "esMX", "ptBR", "ruRU" };
	KpUiLanguages l;
	size_t k;
	memset(&l, 0, sizeof l);
	for (k = 0; k < sizeof IDS / sizeof IDS[0]; k++) {
		snprintf(l.list[k].id, sizeof l.list[k].id, "%s", IDS[k]);
		l.list[k].text = strcmp(IDS[k], "deDE") != 0; /* deDE listed, not installed */
	}
	l.count = (int)k;
	for (k = 0; k < sizeof CASES / sizeof CASES[0]; k++) {
		int at = kp_flow_match_language(&l, CASES[k].wanted);
		kp_t_eq_str(T, at >= 0 ? l.list[at].id : "", CASES[k].id, "%s", CASES[k].wanted);
	}
}

/* The text and its bytes of the top dialog's control id. */
static const char *control_text(Host *H, int id)
{
	const KpUi *ui = H->f.ui;
	int32_t c = ui->open >= 0 ? kp_ui_find_control(H->f.ui, ui->open, (int16_t)id) : -1;
	kp_t_ok(H->T, c >= 0, "no control %d", id);
	return ui->controls[c].text != NULL ? (const char *)ui->controls[c].text : "";
}

static int control_shown(Host *H, int id)
{
	const KpUi *ui = H->f.ui;
	int32_t c = ui->open >= 0 ? kp_ui_find_control(H->f.ui, ui->open, (int16_t)id) : -1;
	return c >= 0 && (ui->controls[c].flags & KP_UI_VISIBLE) != 0;
}

/* StarCraft: Remastered's files with ruRU's alone of its languages (KpData.raw: a language's texts and speech, ADR
 * 0019): the tests count on one language beside the base. */
#define LOCALES "locales/"
#define ONLY "locales/ruRU/"
#define SPEECH "locales/ruRU/Assets/sound/" /* the speech's files (KP_DATA_SPEECH_MARKER's folder) */

static int shown(const Host *H, const char *name)
{
	if (strncmp(name, LOCALES, sizeof LOCALES - 1) != 0) return 1;
	return !H->no_language && !H->language_away && strncmp(name, ONLY, sizeof ONLY - 1) == 0 && !(H->no_speech && strncmp(name, SPEECH, sizeof SPEECH - 1) == 0);
}

static uint8_t *language_read(void *ctx, const char *name, size_t *size, char *err, size_t err_size)
{
	const Host *H = (const Host *)ctx;
	*size = 0;
	if (err_size) err[0] = 0;
	return shown(H, name) ? H->install_raw.read(H->install_raw.ctx, name, size, err, err_size) : NULL;
}

static uint8_t *language_head(void *ctx, const char *name, size_t want, size_t *got, uint64_t *total, char *err,
	size_t err_size)
{
	const Host *H = (const Host *)ctx;
	*got = 0;
	*total = 0;
	if (err_size) err[0] = 0;
	if (!shown(H, name)) return NULL;
	return H->install_raw.head ? H->install_raw.head(H->install_raw.ctx, name, want, got, total, err, err_size)
		: H->install_raw.read(H->install_raw.ctx, name, got, err, err_size);
}

/* The host's data with Remastered's files read from raw through shown (language_read, language_head). */
static void languages_from(Host *H, const KpFiles *raw)
{
	H->install_raw = *raw;
	H->language_raw = H->install_raw;
	H->language_raw.read = language_read;
	H->language_raw.head = language_head;
	H->language_raw.ctx = H;
	H->language_data = *H->data;
	H->language_data.raw = H->language_raw;
	H->data = H->f.data = &H->language_data;
}

/* The host's data with the language of the Remastered install KP_TEST_SCR (its ruRU files: the texts, the TrueType
 * fonts, the speech unless no_speech); skips the test without it. */
static void with_language(Host *H)
{
	KpFiles raw;
	kp_res_raw_files(kp_t_install(H->T, "KP_TEST_SCR"), &raw);
	languages_from(H, &raw);
}

/* The host's data with no language beside the base, as a 1.17 install's, whatever the suite's data are (a repack of
 * Remastered's has its languages' groups): their files hidden. */
static void without_language(Host *H)
{
	H->no_language = 1;
	languages_from(H, &H->data->raw);
}

/* The main menu's Language over the installed languages (ADR 0016): the glue starts in the system's, the popup's button
 * takes the next and its OK starts the flow over in it, kept in the profile, which the next start takes over the
 * system's; Cancel changes nothing. Without a language in the data (a 1.17 install's: without_language) the button is
 * hidden and the text is the base's, unglyphed. */
static void the_language_is_chosen_in_the_main_menu(KpT *T)
{
	Host *H = new_host(T);
	const KpUi *before;
	int k;
	without_language(H);
	H->profile.options &= ~KP_UI_OPTION_EXPANSION_INTRO;
	snprintf(H->flags.system_locales, sizeof H->flags.system_locales, "ru_RU");
	init(H);
	kp_t_eq_str(T, H->f.flags->locale, "enUS", "no other language in the data: the base, whatever the system's");
	wait_for(H, "rez\\glumain.bin", MENU_FRAMES);
	/* not a hidden control: the original's dialog, which a hidden type 14 would give one FLC timer more (menus_check) */
	kp_t_eq(T, kp_ui_find_control(H->f.ui, H->f.ui->open, LANGUAGE_BUTTON), -1, "no Language");
	H = new_host(T);
	H->no_speech = 1;
	with_language(H);
	H->profile.options &= ~KP_UI_OPTION_EXPANSION_INTRO;
	snprintf(H->flags.system_locales, sizeof H->flags.system_locales, "de_AT,ru_RU,en_US");
	init(H);
	wait_for(H, "rez\\glumain.bin", MENU_FRAMES);
	kp_t_eq_str(T, H->f.flags->locale, "ruRU", "the system's first installed");
	kp_t_ok(T, control_shown(H, LANGUAGE_BUTTON), "Language");
	kp_t_eq_str(T, control_text(H, LANGUAGE_BUTTON), "z\x04Я\x01зык", "in the language");
	kp_t_eq_str(T, control_text(H, VERSION_LABEL), "Ремейк оригинальной версии 1.17.0", "the version label, ours, too");
	click(H, LANGUAGE_BUTTON);
	wait_for(H, "koprulu\\language.bin", MENU_FRAMES);
	kp_t_eq_str(T, control_text(H, LANGUAGE_TEXT), "Русский", "the current language");
	kp_t_ok(T, !control_shown(H, LANGUAGE_SPEECH), "no speech to choose");
	click(H, LANGUAGE_CANCEL);
	wait_for(H, "rez\\glumain.bin", MENU_FRAMES);
	kp_t_eq(T, H->language_changes, 0, "Cancel: none");
	click(H, LANGUAGE_BUTTON);
	wait_for(H, "koprulu\\language.bin", MENU_FRAMES);
	click(H, LANGUAGE_TEXT);
	kp_t_eq_str(T, control_text(H, LANGUAGE_TEXT), "English", "the next installed");
	before = H->f.ui;
	click(H, LANGUAGE_OK);
	for (k = 0; k < MENU_FRAMES && H->language_changes == 0; k++) frame(H);
	kp_t_eq(T, H->language_changes, 1, "the flow started over");
	kp_t_eq(T, H->f.glue.screen, KP_GLUE_MENUS, "the main menu at once: the title is the start's");
	kp_t_eq_str(T, H->profile.text_locale, "enUS", "kept in the profile");
	kp_t_eq_str(T, H->f.flags->locale, "enUS", NULL);
	kp_t_ok(T, H->profile_saves > 0, "the profile written");
	kp_t_ok(T, H->f.glue_on && H->f.ui != NULL, "the glue again (%p, before %p)", (const void *)H->f.ui, (const void *)before);
	wait_for(H, "rez\\glumain.bin", MENU_FRAMES);
	kp_t_eq_str(T, control_text(H, LANGUAGE_BUTTON), "l\x04L\x01" "anguage", "in English");
	kp_t_eq_str(T, control_text(H, VERSION_LABEL), "Remake based on the original version 1.17.0", NULL);
	/* and back: the second start over */
	click(H, LANGUAGE_BUTTON);
	wait_for(H, "koprulu\\language.bin", MENU_FRAMES);
	click(H, LANGUAGE_TEXT);
	click(H, LANGUAGE_OK);
	for (k = 0; k < MENU_FRAMES && H->language_changes == 1; k++) frame(H);
	wait_for(H, "rez\\glumain.bin", MENU_FRAMES);
	kp_t_eq_str(T, H->f.flags->locale, "ruRU", "Russian again");
	kp_t_eq_str(T, control_text(H, LANGUAGE_BUTTON), "z\x04Я\x01зык", NULL);
	/* the profile's over the system's at the next start */
	H = new_host(T);
	H->no_speech = 1;
	with_language(H);
	H->profile.options &= ~KP_UI_OPTION_EXPANSION_INTRO;
	snprintf(H->profile.text_locale, sizeof H->profile.text_locale, "enUS");
	snprintf(H->flags.system_locales, sizeof H->flags.system_locales, "ru_RU");
	init(H);
	kp_t_eq_str(T, H->f.flags->locale, "enUS", "the profile's");
	/* --mission and the checks keep the base's */
	H = new_host(T);
	H->no_speech = 1;
	with_language(H);
	snprintf(H->flags.system_locales, sizeof H->flags.system_locales, "ru_RU");
	H->flags.autotest = 1;
	init(H);
	kp_t_eq_str(T, H->f.flags->locale, "", "a check: the base's");
}

/* A language whose files a group not there yet holds (the web's repack, fetched beside the page): listed with its
 * text and speech; the start's waits for them (kp_flow_languages_ready, asked each frame before kp_flow_init), and one
 * chosen in the main menu stops the glue at the choice until they came, then the flow starts over in it. */
static void a_language_waits_for_its_group(KpT *T)
{
	Host *H = new_host(T);
	int k;
	with_language(H);
	H->profile.options &= ~KP_UI_OPTION_EXPANSION_INTRO;
	snprintf(H->flags.system_locales, sizeof H->flags.system_locales, "ru_RU");
	H->language_away = 3;
	kp_t_eq(T, kp_flow_languages_ready(&H->f), 0, "ruRU's group on its way");
	kp_t_eq_str(T, H->f.flags->locale, "ruRU", "the system's, listed before its files came");
	kp_t_eq_str(T, H->f.flags->speech, "ruRU", "its speech too");
	kp_t_ok(T, strstr(H->last_log, "waiting for their files") != NULL, "said: %s", H->last_log);
	kp_t_eq(T, kp_flow_languages_ready(&H->f), 1, "they came");
	kp_t_eq(T, H->language_asks, 3, "the text's and the speech's asked, then the text's");
	init(H);
	wait_for(H, "rez\\glumain.bin", MENU_FRAMES);
	kp_t_eq_str(T, control_text(H, LANGUAGE_BUTTON), "z\x04Я\x01зык", "in the language");
	kp_t_ok(T, H->f.locale.speech != NULL, "its speech's records");
	/* chosen in the main menu */
	H = new_host(T);
	with_language(H);
	H->profile.options &= ~KP_UI_OPTION_EXPANSION_INTRO;
	snprintf(H->flags.system_locales, sizeof H->flags.system_locales, "en_US");
	H->language_away = 1000;
	kp_t_eq(T, kp_flow_languages_ready(&H->f), 1, "the base's: nothing to wait for");
	init(H);
	wait_for(H, "rez\\glumain.bin", MENU_FRAMES);
	kp_t_ok(T, control_shown(H, LANGUAGE_BUTTON), "Language: ruRU listed");
	click(H, LANGUAGE_BUTTON);
	wait_for(H, "koprulu\\language.bin", MENU_FRAMES);
	click(H, LANGUAGE_TEXT);
	kp_t_eq_str(T, control_text(H, LANGUAGE_TEXT), "Русский", NULL);
	click(H, LANGUAGE_OK);
	for (k = 0; k < MENU_FRAMES && H->language_asks == 0; k++) frame(H);
	kp_t_eq(T, H->language_asks, 1, "its files asked for");
	kp_t_ok(T, strstr(H->last_log, "waiting for their files") != NULL, "said: %s", H->last_log);
	frames(H, 3);
	kp_t_eq(T, H->language_asks, 4, "asked again each frame");
	kp_t_eq(T, H->language_changes, 0, "the glue stopped at the choice");
	H->language_away = 1;
	frame(H);
	kp_t_eq(T, H->language_changes, 1, "they came: the flow started over");
	kp_t_eq_str(T, H->f.flags->locale, "ruRU", NULL);
	wait_for(H, "rez\\glumain.bin", MENU_FRAMES);
	kp_t_eq_str(T, control_text(H, LANGUAGE_BUTTON), "z\x04Я\x01зык", "in the language");
}

/* The speech (stage L5): with a language's speech installed the popup's speech row shows, the speech starts as the
 * text's language's and reads its sound records; its button takes the next installed, and OK keeps it in the profile
 * apart from the text's and starts the flow over in it (the base's: no records); --speech is taken over both, and the
 * checks keep the base's. */
static void the_speech_is_chosen_apart_from_the_text(KpT *T)
{
	Host *H;
	int k;
	H = new_host(T);
	with_language(H);
	H->profile.options &= ~KP_UI_OPTION_EXPANSION_INTRO;
	snprintf(H->flags.system_locales, sizeof H->flags.system_locales, "ru_RU");
	init(H);
	wait_for(H, "rez\\glumain.bin", MENU_FRAMES);
	kp_t_eq_str(T, H->f.flags->speech, "ruRU", "the text's language's speech");
	kp_t_ok(T, H->f.locale.speech != NULL && H->f.locale.speech_size > 0, "its sound records");
	click(H, LANGUAGE_BUTTON);
	wait_for(H, "koprulu\\language.bin", MENU_FRAMES);
	kp_t_ok(T, control_shown(H, LANGUAGE_SPEECH), "a speech to choose");
	kp_t_eq_str(T, control_text(H, LANGUAGE_SPEECH), "Русский", "the current speech");
	click(H, LANGUAGE_SPEECH);
	kp_t_eq_str(T, control_text(H, LANGUAGE_SPEECH), "English", "the next installed");
	kp_t_eq_str(T, control_text(H, LANGUAGE_TEXT), "Русский", "the text's unchanged");
	click(H, LANGUAGE_OK);
	for (k = 0; k < MENU_FRAMES && H->language_changes == 0; k++) frame(H);
	kp_t_eq(T, H->language_changes, 1, "the flow started over");
	kp_t_eq(T, H->f.glue.screen, KP_GLUE_MENUS, "the main menu at once: the title is the start's");
	kp_t_eq_str(T, H->profile.speech_locale, "enUS", "the speech kept in the profile");
	kp_t_eq_str(T, H->profile.text_locale, "ruRU", "the text's apart");
	kp_t_eq_str(T, H->f.flags->speech, "enUS", NULL);
	wait_for(H, "rez\\glumain.bin", MENU_FRAMES);
	kp_t_ok(T, H->f.locale.speech == NULL, "the base's speech: no records");
	/* --speech over the profile's and the text's */
	H = new_host(T);
	with_language(H);
	snprintf(H->profile.speech_locale, sizeof H->profile.speech_locale, "enUS");
	snprintf(H->flags.speech, sizeof H->flags.speech, "ruRU");
	snprintf(H->flags.system_locales, sizeof H->flags.system_locales, "en_US");
	init(H);
	kp_t_eq_str(T, H->f.flags->speech, "ruRU", "--speech");
	kp_t_ok(T, H->f.locale.speech != NULL, "its sound records");
	/* a check keeps the base's */
	H = new_host(T);
	with_language(H);
	snprintf(H->flags.system_locales, sizeof H->flags.system_locales, "ru_RU");
	H->flags.autotest = 1;
	init(H);
	kp_t_eq_str(T, H->f.flags->speech, "", "a check: the base's");
}

/* A mission history's cinematic row: its cinematic (after its prelude), then the game goes on from the row the
 * campaign advanced to, not from the mission's first row; without the pack at once. */
static void history_cinematic_goes_on_from_its_row(KpT *T)
{
	int pack;
	for (pack = 1; pack >= 0; pack--) {
		Host *H = glue_host(T, pack);
		KpCampaignRun run;
		KpCampaignProfile progress;
		KpCampaignStep step;
		int64_t more[KP_APP_STEP_CINEMATICS];
		int count = 0;
		progress = *kp_profile_progress(&H->profile);
		kp_t_ok(T, kp_campaign_after_cinematic(&H->data->campaign, 0, HISTORY_ROW, HISTORY_CINEMATIC, &run, &progress, &step,
			more, &count, KP_APP_STEP_CINEMATICS), "the row");
		kp_t_eq(T, step.kind, KP_CAMPAIGN_STEP_MISSION, "a mission after it");
		menus_end(H, KP_UI_MENU_CINEMATIC, ZERG_ROOM, HISTORY_ROW, HISTORY_CINEMATIC, 0);
		if (pack) {
			kp_t_eq(T, H->cine_starts, 1, "the cinematic");
			kp_t_eq(T, H->cine_ids[0], HISTORY_CINEMATIC, NULL);
			kp_t_eq(T, H->cine_count, count + 1, "and the rows after it");
			kp_t_eq(T, H->cine_preludes, 1, "each after its prelude");
			kp_t_eq(T, H->f.playing, 0, "the game waits for them");
			frames(H, 10);
			kp_t_eq(T, H->builds, 0, NULL);
			H->movie = 0;
			frame(H);
		}
		kp_t_eq(T, H->builds, 1, "the game");
		kp_t_eq(T, H->data->missions[H->f.current].index, step.mission, "the row's mission");
		kp_t_ok(T, memcmp(&H->f.run, &run, sizeof run) == 0, "the run from the row");
		kp_t_eq(T, H->f.glue.screen, KP_GLUE_ROOM, "its ready room");
		kp_t_eq(T, H->f.glue_room, (int)step.glue, "the row's room");
		kp_t_eq(T, H->f.has_start_run, 0, "the row's run taken");
	}
}

/* The epilog of a campaign (the expansion's or the original's text) with no game standing; an Escape in it opens its
 * credits, another in them the main menu. */
static void epilog_to_the_main_menu(Host *H, int expansion, const char *what)
{
	KpUi *ui = H->f.ui;
	const KpUiFile *text = kp_ui_glue_file(ui, expansion ? "rez\\epilogX.txt" : "rez\\epilog.txt");
	int k;
	kp_t_eq(H->T, H->f.glue.screen, KP_GLUE_EPILOG, "%s: the epilog", what);
	kp_t_ok(H->T, text != NULL && ui->credits.text == (const char *)text->data, "%s: the %s epilog", what,
		expansion ? "expansion's" : "original's");
	kp_t_ok(H->T, !H->f.playing && H->f.session == NULL && !H->world, "%s: no game, session or world", what);
	frames(H, SETTLE_FRAMES);
	key(H, KEY_ESCAPE);
	frames(H, SETTLE_FRAMES);
	kp_t_ok(H->T, H->f.glue.screen == KP_GLUE_EPILOG && ui->credits.text == ui->credits.owned, "%s: its credits", what);
	key(H, KEY_ESCAPE);
	for (k = 0; k < END_FRAMES && H->f.glue.screen != KP_GLUE_MENUS; k++) frame(H);
	in_menus(H, KP_GLUE_MAIN, what);
}

/* The victory of a campaign's last mission: the score screen's Continue plays the cinematic rows after it, then the
 * epilog of the run's campaign (mode 9, RunCampaignEpilog 0x414ef0), then the main menu. */
static void campaign_end_plays_its_cinematics_then_the_epilog(KpT *T)
{
	Host *H = glue_host(T, 1);
	KpCampaignRun run;
	const char *name;
	int count = 0, k;
	name = mission_with_step(H, KP_CAMPAIGN_STEP_EPILOG, &count, &run);
	kp_t_ok(T, name != NULL, "a mission whose victory ends its campaign");
	play(H, name);
	win(H);
	kp_t_eq(T, H->f.has_next, 0, "no next game");
	wait_for(H, "rez\\gluscore.bin", MENU_FRAMES);
	click(H, SCORE_CONTINUE);
	for (k = 0; k < END_FRAMES && !H->movie; k++) frame(H);
	kp_t_eq(T, H->cine_starts, 1, "the cinematics");
	kp_t_eq(T, H->cine_count, count, "every row's");
	frames(H, 10);
	kp_t_eq(T, H->f.glue.screen, KP_GLUE_NONE, "the epilog waits for them");
	H->movie = 0;
	frame(H);
	epilog_to_the_main_menu(H, run.expansion, name);
}

/* A mission history's cinematic row that ends its campaign: its cinematic, then the epilog (the original's main loop
 * plays mode 9 when AdvanceCampaignAfterGame 0x4bb790 reaches the end from the row); without the pack at once. */
static void history_cinematic_at_the_campaigns_end_plays_the_epilog(KpT *T)
{
	int pack;
	for (pack = 1; pack >= 0; pack--) {
		Host *H = glue_host(T, pack);
		const KpCampaignRow *row = NULL;
		int t, k, count = 0, expansion = 0;
		for (t = 0; t < H->data->campaign.table_count && !row; t++) {
			const KpCampaignTable *table = &H->data->campaign.tables[t];
			for (k = 0; k < table->count && !row; k++) {
				const KpCampaignRow *r = &table->rows[k];
				KpCampaignProfile progress = *kp_profile_progress(&H->profile);
				KpCampaignRun run;
				KpCampaignStep step;
				int64_t more[KP_APP_STEP_CINEMATICS];
				if (r->cinematic != 0 && kp_campaign_after_cinematic(&H->data->campaign, table->expansion, r->mission,
						r->cinematic, &run, &progress, &step, more, &count, KP_APP_STEP_CINEMATICS)
					&& step.kind == KP_CAMPAIGN_STEP_EPILOG) {
					row = r;
					expansion = table->expansion;
				}
			}
		}
		kp_t_ok(T, row != NULL, "a cinematic row at a campaign's end");
		menus_end(H, KP_UI_MENU_CINEMATIC, ZERG_ROOM, row->mission, row->cinematic, expansion);
		if (pack) {
			kp_t_eq(T, H->cine_starts, 1, "the cinematic");
			kp_t_eq(T, H->cine_ids[0], row->cinematic, NULL);
			kp_t_eq(T, H->cine_count, count + 1, "and the rows after it");
			frames(H, 10);
			kp_t_eq(T, H->f.glue.screen, KP_GLUE_NONE, "the epilog waits for them");
			H->movie = 0;
			frame(H);
		}
		epilog_to_the_main_menu(H, expansion, pack ? "with the pack" : "without the pack");
	}
}

/* The first start (RunMainLoop 0x4679b0, the options' bit 0x800): the logo and the expansion's intro before the title
 * and the main menu, once (the profile written at once); a start without the pack keeps them for one that has it, and
 * the checks and --cine-play's start play none (the title at once). */
static void first_start_plays_the_logo_and_the_intro_once(KpT *T)
{
	Host *H = new_host(T);
	uint32_t played;
	int part;
	char name[128];
	H->pack = 1;
	kp_t_ok(T, (H->profile.options & KP_UI_OPTION_EXPANSION_INTRO) != 0, "a new profile");
	init(H);
	kp_t_eq(T, H->cine_starts, 1, "the cinematics");
	kp_t_eq(T, H->cine_count, 2, NULL);
	kp_t_eq(T, H->cine_ids[0], KP_APP_CINE_LOGO, NULL);
	kp_t_eq(T, H->cine_ids[1], KP_APP_CINE_EXPANSION_INTRO, NULL);
	kp_t_eq(T, H->cine_preludes, 0, "no preludes");
	kp_t_eq(T, H->profile.options & KP_UI_OPTION_EXPANSION_INTRO, 0, "once");
	kp_t_eq(T, H->profile_saves, 1, "the profile written at once");
	kp_t_eq(T, H->f.glue.screen, KP_GLUE_NONE, "no screen while they play");
	kp_t_eq_str(T, top(H, name, sizeof name), "", "no main menu behind them");
	frames(H, 10);
	kp_t_eq(T, H->f.glue.screen, KP_GLUE_NONE, NULL);
	H->movie = 0;
	frame(H);
	kp_t_eq(T, H->f.glue.screen, KP_GLUE_TITLE, "the title after the intro (THEN_TITLE)");
	kp_t_eq_str(T, top(H, name, sizeof name), "rez\\titledlg.bin", NULL);
	wait_for(H, "rez\\glumain.bin", MENU_FRAMES);
	in_menus(H, KP_GLUE_MAIN, "after the intro");
	played = H->profile.options;
	for (part = 0; part < 4; part++) {
		static const char *const WHAT[4] = { "the next start", "no pack", "options originals", "--cine-play" };
		if (!(H = new_host(T))) return;
		H->pack = part != 1;
		if (part == 0) H->profile.options = played;
		if (part == 2) H->flags.originals = 1;
		if (part == 3) snprintf(H->flags.cine_play, sizeof H->flags.cine_play, "3");
		init(H);
		kp_t_eq(T, H->f.glue.screen, KP_GLUE_TITLE, "%s: the title at once", WHAT[part]);
		kp_t_eq(T, H->cine_starts, 0, "%s: no cinematics", WHAT[part]);
		kp_t_eq(T, H->profile_saves, 0, "%s: the profile as it was", WHAT[part]);
		if (part > 0) {
			kp_t_ok(T, (H->profile.options & KP_UI_OPTION_EXPANSION_INTRO) != 0, "%s: the intro kept", WHAT[part]);
		}
		wait_for(H, "rez\\glumain.bin", MENU_FRAMES);
		in_menus(H, KP_GLUE_MAIN, WHAT[part]);
	}
}

/* The check boxes come from the profile at the start (--subtitles over the subtitles' without writing it), also into a
 * glue-less game's own interface; the player's changes go back to it and are written; the development checks keep the
 * originals' and write none; S over a movie flips the same subtitles. */
static void options_follow_the_profile_and_the_flag(KpT *T)
{
	Host *H;
	int part;
	for (part = 0; part < 5; part++) {
		static const char *const WHAT[5] = { "a new profile", "a profile with the subtitles", "--subtitles on",
			"--subtitles off over a profile with them", "--subtitles auto over a profile with them" };
		static const char *const FLAG[5] = { "", "", "on", "off", "auto" };
		const uint32_t kept = KP_UI_OPTION_ANSWERS | KP_UI_OPTION_TIPS
			| (part == 1 || part >= 3 ? KP_UI_OPTION_SUBTITLES : 0);
		if (!(H = new_host(T))) return;
		H->profile.options = (H->profile.options & ~KP_UI_OPTIONS_KEPT) | (part == 0 ? KP_UI_OPTIONS_DEFAULT : kept);
		snprintf(H->flags.subtitles, sizeof H->flags.subtitles, "%s", FLAG[part]);
		init(H);
		kp_t_eq(T, kp_flow_subtitles(&H->f), part == 1 || part == 2 || part == 4, "%s", WHAT[part]);
		kp_t_eq(T, H->f.ui->options & (KP_UI_OPTIONS_KEPT & ~KP_UI_OPTION_SUBTITLES),
			(part == 0 ? KP_UI_OPTIONS_DEFAULT : kept) & ~KP_UI_OPTION_SUBTITLES, "%s: the other boxes the profile's",
			WHAT[part]);
		kp_t_eq(T, H->profile.options & KP_UI_OPTIONS_KEPT, part == 0 ? KP_UI_OPTIONS_DEFAULT : kept,
			"%s: the profile as it was", WHAT[part]);
		if (part == 3) { /* the player's change of another box writes that box only */
			wait_for(H, "rez\\glumain.bin", MENU_FRAMES);
			H->profile_saves = 0;
			H->f.ui->options ^= KP_UI_OPTION_SPEECH;
			H->f.ui->options_committed |= KP_UI_SECTION_SOUND; /* its dialog's OK */
			frames(H, 2);
			kp_t_eq(T, H->profile_saves, 1, "a box changed: the profile written once");
			kp_t_eq(T, H->profile.options & KP_UI_OPTIONS_KEPT, kept | KP_UI_OPTION_SPEECH,
				"the speech kept, the subtitles the profile's");
			kp_flow_toggle_subtitles(&H->f);
			kp_t_ok(T, kp_flow_subtitles(&H->f), "S shows them");
			kp_flow_toggle_subtitles(&H->f);
			kp_t_ok(T, !kp_flow_subtitles(&H->f) && !(H->profile.options & KP_UI_OPTION_SUBTITLES),
				"S hides them again: the player's choice now kept");
			kp_t_eq(T, H->profile_saves, 3, "each written");
		}
	}
	if (!(H = new_host(T))) return; /* --mission: the game's interface of its own has the profile's */
	H->profile.options = (H->profile.options & ~KP_UI_OPTIONS_KEPT) | KP_UI_OPTION_ANSWERS | KP_UI_OPTION_SUBTITLES;
	snprintf(H->flags.mission, sizeof H->flags.mission, "%s", EXPANSION_PROTOSS01);
	init(H);
	kp_t_eq(T, H->interfaces, 2, "--mission: the console's interface of its own");
	kp_t_eq(T, H->f.ui->options & KP_UI_OPTIONS_KEPT, KP_UI_OPTION_ANSWERS | KP_UI_OPTION_SUBTITLES,
		"--mission: the profile's boxes");
	H->profile_saves = 0;
	frames(H, 10);
	kp_t_eq(T, H->profile_saves, 0, "--mission: the profile as it was");
	kp_t_eq(T, H->profile.options & KP_UI_OPTIONS_KEPT, KP_UI_OPTION_ANSWERS | KP_UI_OPTION_SUBTITLES, NULL);
	if (!(H = new_host(T))) return; /* the checks keep the originals' boxes and write none */
	H->profile.options |= KP_UI_OPTION_SUBTITLES;
	H->flags.originals = 1;
	init(H);
	kp_t_eq(T, H->f.ui->options & KP_UI_OPTIONS_KEPT, KP_UI_OPTIONS_DEFAULT, "options originals: the originals' boxes");
	H->profile_saves = 0;
	kp_flow_toggle_subtitles(&H->f);
	kp_t_ok(T, kp_flow_subtitles(&H->f), "options originals: S shows them");
	kp_t_eq(T, H->profile_saves, 0, "without writing the profile");
	kp_t_eq(T, H->profile.options & KP_UI_OPTIONS_KEPT, KP_UI_OPTIONS_DEFAULT | KP_UI_OPTION_SUBTITLES, NULL);
}

/* The sound options' first box clicked (its proc flips the bit). */
static void click_speech_box(KpT *T, KpUi *ui)
{
	int32_t box = kp_ui_find_control(ui, ui->modeless, 3);
	uint32_t before = ui->options;
	kp_t_ok(T, box >= 0, "the speech box");
	if (box < 0) return;
	(void)kp_ui_send_user(ui, box, KP_UI_CLOSE, 0);
	kp_t_eq(T, ui->options ^ before, KP_UI_OPTION_SPEECH, "the click flips the box");
}

/* A box clicked is written once its dialog closed with OK: a crash under the open dialog, or its Cancel, leaves the
 * profile as it was and writes nothing. */
static void a_box_is_written_at_its_dialogs_ok(KpT *T)
{
	enum { ID_OK = -2, ID_CANCEL = -3 };
	Host *H = new_host(T);
	KpUi *ui;
	uint32_t was;
	init(H);
	wait_for(H, "rez\\glumain.bin", MENU_FRAMES);
	ui = H->f.ui;
	was = H->profile.options;
	H->profile_saves = 0;
	kp_ui_options_dialog(ui, KP_UI_OPTIONS_SOUND);
	click_speech_box(T, ui);
	frames(H, 2);
	kp_t_eq(T, H->profile_saves, 0, "open: nothing written");
	kp_t_eq(T, H->profile.options, was, "open: the profile as it was");
	ui->result = ID_CANCEL;
	(void)kp_ui_send_user(ui, ui->modeless, KP_UI_CLOSE, 0);
	kp_t_eq(T, ui->modeless, -1, "Cancel closes it");
	kp_t_eq(T, ui->options & KP_UI_OPTION_SPEECH, was & KP_UI_OPTION_SPEECH, "Cancel puts the box back");
	frames(H, 2);
	kp_t_eq(T, H->profile_saves, 0, "Cancel: nothing written");
	kp_t_eq(T, H->profile.options, was, NULL);
	kp_ui_options_dialog(ui, KP_UI_OPTIONS_SOUND);
	click_speech_box(T, ui);
	frames(H, 2);
	kp_t_eq(T, H->profile_saves, 0, "open again: nothing written");
	ui->result = ID_OK;
	(void)kp_ui_send_user(ui, ui->modeless, KP_UI_CLOSE, 0);
	kp_t_eq(T, ui->modeless, -1, "OK closes it");
	frames(H, 2);
	kp_t_eq(T, H->profile_saves, 1, "OK: written once");
	kp_t_eq(T, H->profile.options, was ^ KP_UI_OPTION_SPEECH, "OK: the box kept");
	was = H->profile.options;
	H->profile_saves = 0;
	kp_ui_options_dialog(ui, KP_UI_OPTIONS_SOUND);
	click_speech_box(T, ui);
	kp_ui_close_modeless(ui, 1); /* closed with no answer (the game's end) */
	frames(H, 2);
	kp_t_eq(T, ui->modeless, -1, "closed");
	kp_t_eq(T, ui->options & KP_UI_OPTION_SPEECH, was & KP_UI_OPTION_SPEECH, "the box put back");
	kp_t_eq(T, H->profile_saves, 0, "no answer: nothing written");
	kp_t_eq(T, H->profile.options, was, NULL);
	kp_flow_toggle_subtitles(&H->f);
	kp_t_eq(T, H->profile_saves, 1, "S writes");
	kp_t_eq(T, H->profile.options, was ^ KP_UI_OPTION_SUBTITLES, "its box alone");
	was = H->profile.options;
	H->profile_saves = 0;
	ui->options ^= KP_UI_OPTION_ANSWERS; /* out of its dialog */
	ui->options_committed |= KP_UI_SECTION_SPEED; /* another dialog's OK */
	frames(H, 2);
	kp_t_eq(T, H->profile_saves, 0, "another section's OK: nothing written");
	kp_t_eq(T, H->profile.options, was, "the box not its");
	ui->options_committed |= KP_UI_SECTION_VIDEO;
	frames(H, 2);
	kp_t_eq(T, H->profile_saves, 1, "the video's OK: written once");
	kp_t_eq(T, H->profile.options, was, "the box still not its");
}

/* The profile's options of the sound and speed dialogs and the tips, none of the originals'. */
static const KpGameSettings KEPT_SETTINGS = { 40, 60, 1, 5, 2, 7, 1, 4, 0 };

static void settings_are_in_the_interface(KpT *T, const KpUi *ui, const KpGameSettings *s, const char *what)
{
	kp_t_eq(T, ui->sound.music, s->music, "%s: the music", what);
	kp_t_eq(T, ui->sound.effects, s->effects, "%s: the effects", what);
	kp_t_eq(T, ui->mouse_scroll_speed, s->mouse_scroll, "%s: the mouse's scroll", what);
	kp_t_eq(T, ui->key_scroll_speed, s->key_scroll, "%s: the keys' scroll", what);
	kp_t_eq(T, ui->multi_mouse_scroll_speed, s->multi_mouse_scroll, "%s: the multiplayer mouse's scroll", what);
	kp_t_eq(T, ui->multi_key_scroll_speed, s->multi_key_scroll, "%s: the multiplayer keys' scroll", what);
	kp_t_eq(T, ui->tip_index, s->tip, "%s: the tip", what);
	kp_t_eq(T, ui->cpu_throttle, s->cpu_throttle, "%s: the throttle", what);
}

/* The settings reach the glue's interface and a glue-less game's own; the checks keep the originals' and write none. */
static void settings_come_from_the_profile(KpT *T)
{
	Host *H;
	KpProfile fresh; /* the originals' defaults */
	kp_profile_init(&fresh);
	if (!(H = new_host(T))) return;
	H->profile.settings = KEPT_SETTINGS;
	init(H);
	settings_are_in_the_interface(T, H->f.ui, &KEPT_SETTINGS, "the glue");
	kp_t_eq(T, H->f.ui->music.resumed, 1, "the music on");
	if (!(H = new_host(T))) return;
	H->profile.settings = KEPT_SETTINGS;
	H->profile.settings.music = 0;
	init(H);
	kp_t_eq(T, H->f.ui->music.resumed, 0, "a profile's silence: the music closed");
	if (!(H = new_host(T))) return;
	H->profile.settings = KEPT_SETTINGS;
	snprintf(H->flags.mission, sizeof H->flags.mission, "%s", EXPANSION_PROTOSS01);
	init(H);
	settings_are_in_the_interface(T, H->f.ui, &KEPT_SETTINGS, "--mission's console");
	if (!(H = new_host(T))) return;
	H->profile.settings = KEPT_SETTINGS;
	H->flags.originals = 1;
	init(H);
	settings_are_in_the_interface(T, H->f.ui, &fresh.settings, "options originals");
	H->profile_saves = 0;
	H->f.ui->sound.music = 70;
	H->f.ui->mouse_scroll_speed = 6;
	H->f.ui->speed_chosen = 1;
	H->f.ui->options_committed |= KP_UI_SECTION_SOUND | KP_UI_SECTION_SPEED | KP_UI_SECTION_TIPS;
	frames(H, 2);
	kp_t_eq(T, H->profile_saves, 0, "options originals: its OK writes nothing");
	kp_t_eq(T, H->profile.settings.music, KEPT_SETTINGS.music, "options originals: the profile as it was");
	kp_t_eq(T, H->profile.settings.mouse_scroll, KEPT_SETTINGS.mouse_scroll, NULL);
	kp_t_eq(T, H->profile.settings.game_speed, KEPT_SETTINGS.game_speed, NULL);
}

/* Each game starts at the profile's speed, a campaign's and --mission's; a check at the clock's. */
static void games_start_at_the_profiles_speed(KpT *T)
{
	Host *H;
	if (!(H = glue_host(T, 0))) return;
	H->profile.settings.game_speed = 2;
	play(H, EXPANSION_PROTOSS01);
	kp_t_eq(T, H->f.session->clock.speed, 3, "the clock of a game from the menus");
	kp_t_eq(T, kp_scalar(H->f.session->g, KP_SPEED), 2, "its game speed");
	if (!(H = new_host(T))) return;
	H->profile.settings.game_speed = 0;
	snprintf(H->flags.mission, sizeof H->flags.mission, "%s", EXPANSION_PROTOSS01);
	init(H);
	kp_t_eq(T, H->f.session->clock.speed, 1, "--mission's clock");
	if (!(H = new_host(T))) return;
	H->profile.settings.game_speed = 0;
	snprintf(H->flags.mission, sizeof H->flags.mission, "%s", EXPANSION_PROTOSS01);
	H->flags.originals = 1;
	init(H);
	kp_t_eq(T, H->f.session->clock.speed, KP_CLOCK_DEFAULT_SPEED, "options originals: the clock's own");
}

/* A measured or captured game is not the player's: the tips' dialog would pause it, a silence close its music, the
 * speed change what its frames show; nor does it write the player's options. */
static void measured_runs_keep_the_originals(KpT *T)
{
	KpProfile fresh;
	int k;
	kp_profile_init(&fresh);
	for (k = 0; k < 2; k++) {
		const char *what = k == 0 ? "--bench-frames" : "--screenshot";
		Host *H = new_host(T);
		H->profile.settings = KEPT_SETTINGS;
		H->profile.settings.music = 0;
		H->profile.settings.game_speed = 0;
		H->profile.options |= KP_UI_OPTION_TIPS;
		snprintf(H->flags.mission, sizeof H->flags.mission, "%s", EXPANSION_PROTOSS01);
		if (k == 0) H->flags.bench_frames = 1;
		else snprintf(H->flags.screenshot, sizeof H->flags.screenshot, "%s", "shot.png");
		init(H);
		kp_t_eq(T, H->f.ui->modeless, -1, "%s: no tips' dialog over its game", what);
		kp_t_eq(T, H->f.ui->options & KP_UI_OPTION_TIPS, 0, "%s: the originals' box", what);
		kp_t_eq(T, H->f.ui->music.resumed, 1, "%s: its music on", what);
		settings_are_in_the_interface(T, H->f.ui, &fresh.settings, what);
		kp_t_eq(T, H->f.session->clock.speed, KP_CLOCK_DEFAULT_SPEED, "%s: the clock's own", what);
		H->profile_saves = 0;
		H->f.ui->sound.music = 70;
		H->f.ui->options ^= KP_UI_OPTION_SPEECH;
		H->f.ui->options_committed |= KP_UI_SECTION_SOUND;
		frames(H, 2);
		kp_t_eq(T, H->profile_saves, 0, "%s: its OK writes nothing", what);
		kp_t_eq(T, H->profile.settings.effects, KEPT_SETTINGS.effects, "%s: the profile as it was", what);
	}
}

/* The frame limit follows the interface's check box, also before its OK; the checks and benches have only the flag. */
static void frame_cap_follows_the_throttle(KpT *T)
{
	static const struct {
		const char *what;
		int bench, flag, throttle, want;
	} CASES[] = {
		{ "off", 0, 0, 0, 0 },
		{ "on", 0, 0, 1, KP_THROTTLE_FPS },
		{ "off, the flag", 0, 30, 0, 30 },
		{ "on, the flag over it", 0, 30, 1, 30 },
		{ "a bench's box", 1, 0, 1, 0 },
		{ "a bench's flag", 1, 30, 0, 30 },
	};
	size_t k;
	for (k = 0; k < sizeof CASES / sizeof *CASES; k++) {
		Host *H = new_host(T);
		H->flags.bench_frames = CASES[k].bench;
		H->flags.frame_cap = CASES[k].flag;
		init(H);
		H->f.ui->cpu_throttle = (uint8_t)CASES[k].throttle;
		kp_t_eq(T, kp_flow_frame_cap(&H->f), CASES[k].want, "%s", CASES[k].what);
	}
	{
		Host *H = new_host(T);
		H->profile.settings = KEPT_SETTINGS; /* its throttle on */
		init(H);
		kp_t_eq(T, kp_flow_frame_cap(&H->f), KP_THROTTLE_FPS, "the profile's box limits the glue too");
	}
}

/* A slider of the open dialog at a value. */
static void slider_at(KpT *T, KpUi *ui, int id, int32_t value)
{
	int32_t c = kp_ui_find_control(ui, ui->modeless, (int16_t)id);
	kp_t_ok(T, c >= 0, "the slider %d", id);
	if (c >= 0) (void)kp_ui_send_user(ui, c, KP_UI_VALUE_CHANGED, (uint32_t)value);
}

static uint32_t slider_pos(KpT *T, KpUi *ui, int id)
{
	int32_t c = kp_ui_find_control(ui, ui->modeless, (int16_t)id);
	kp_t_ok(T, c >= 0, "the slider %d", id);
	return c >= 0 ? (uint32_t)kp_ui_scrollbar_pos(&ui->controls[c]) : 0xffffffffu;
}

static int slider_enabled(KpT *T, KpUi *ui, int id)
{
	int32_t c = kp_ui_find_control(ui, ui->modeless, (int16_t)id);
	kp_t_ok(T, c >= 0, "the slider %d", id);
	return c >= 0 && !(ui->controls[c].flags & KP_UI_DISABLED);
}

static void answer_dialog(KpT *T, KpUi *ui, int32_t answer)
{
	ui->result = answer;
	(void)kp_ui_send_user(ui, ui->modeless, KP_UI_CLOSE, 0);
	kp_t_eq(T, ui->modeless, -1, "the answer closes it");
}

static void settings_written(KpT *T, Host *H, const KpGameSettings *want, int saves, const char *what)
{
	const KpGameSettings *s = &H->profile.settings;
	frames(H, 2);
	kp_t_eq(T, H->profile_saves, saves, "%s: the writes", what);
	kp_t_eq(T, s->music, want->music, "%s: the music", what);
	kp_t_eq(T, s->effects, want->effects, "%s: the effects", what);
	kp_t_eq(T, s->mouse_scroll, want->mouse_scroll, "%s: the mouse's scroll", what);
	kp_t_eq(T, s->key_scroll, want->key_scroll, "%s: the keys' scroll", what);
	kp_t_eq(T, s->multi_mouse_scroll, want->multi_mouse_scroll, "%s: the multiplayer mouse's scroll", what);
	kp_t_eq(T, s->multi_key_scroll, want->multi_key_scroll, "%s: the multiplayer keys' scroll", what);
	kp_t_eq(T, s->game_speed, want->game_speed, "%s: the game speed", what);
	kp_t_eq(T, s->tip, want->tip, "%s: the tip", what);
	kp_t_eq(T, s->cpu_throttle, want->cpu_throttle, "%s: the throttle", what);
}

/* Open dialog, Cancel, closed with no answer: nothing written, all put back; OK: its own section, once. */
static void settings_are_written_at_their_dialogs_ok(KpT *T)
{
	enum { ID_OK = -2, ID_CANCEL = -3, SOUND_MUSIC = 1, SOUND_EFFECTS = 2, SPEED_GAME = 1, SPEED_MOUSE = 2, SPEED_KEY = 3 };
	Host *H;
	KpUi *ui;
	KpGameSettings want;
	if (!(H = glue_host(T, 0))) return;
	H->profile.settings = KEPT_SETTINGS;
	want = H->profile.settings;
	kp_ui_options_dialog(H->f.ui, KP_UI_OPTIONS_SPEED); /* no game: its slider says nothing of the game speed */
	H->f.ui->mouse_scroll_speed = want.mouse_scroll;
	H->f.ui->key_scroll_speed = want.key_scroll;
	H->f.ui->multi_mouse_scroll_speed = want.multi_mouse_scroll;
	H->f.ui->multi_key_scroll_speed = want.multi_key_scroll;
	H->f.ui->cpu_throttle = (uint8_t)want.cpu_throttle;
	answer_dialog(T, H->f.ui, ID_OK);
	frames(H, 2);
	kp_t_eq(T, H->profile.settings.game_speed, want.game_speed, "no game: the game speed as it was");
	play(H, EXPANSION_PROTOSS01); /* the game speed's slider needs a game */
	ui = H->f.ui;
	ui->mouse_scroll_speed = want.mouse_scroll;
	ui->key_scroll_speed = want.key_scroll;
	ui->multi_mouse_scroll_speed = want.multi_mouse_scroll;
	ui->multi_key_scroll_speed = want.multi_key_scroll;
	ui->sound.music = want.music;
	ui->sound.effects = want.effects;
	ui->tip_index = want.tip;
	ui->cpu_throttle = (uint8_t)want.cpu_throttle;
	H->profile_saves = 0;

	/* the speed dialog */
	kp_ui_options_dialog(ui, KP_UI_OPTIONS_SPEED);
	ui->mouse_scroll_speed = 6;
	ui->key_scroll_speed = 0;
	ui->cpu_throttle = !ui->cpu_throttle;
	slider_at(T, ui, SPEED_GAME, 5);
	settings_written(T, H, &want, 0, "the speed dialog open");
	answer_dialog(T, ui, ID_CANCEL);
	settings_are_in_the_interface(T, ui, &want, "the speed dialog's Cancel");
	settings_written(T, H, &want, 0, "the speed dialog's Cancel");
	kp_ui_options_dialog(ui, KP_UI_OPTIONS_SPEED);
	ui->mouse_scroll_speed = 6;
	kp_ui_close_modeless(ui, 1); /* the game's end */
	settings_are_in_the_interface(T, ui, &want, "the speed dialog with no answer");
	settings_written(T, H, &want, 0, "the speed dialog with no answer");
	kp_ui_options_dialog(ui, KP_UI_OPTIONS_SPEED);
	slider_at(T, ui, SPEED_GAME, 5);
	ui->mouse_scroll_speed = 6;
	ui->key_scroll_speed = 0;
	ui->cpu_throttle = 0;
	answer_dialog(T, ui, ID_OK);
	want.mouse_scroll = 6;
	want.key_scroll = 0;
	want.cpu_throttle = 0;
	want.game_speed = 5;
	settings_written(T, H, &want, 1, "the speed dialog's OK");

	/* an OK that changed nothing writes nothing */
	kp_ui_options_dialog(ui, KP_UI_OPTIONS_SPEED);
	slider_at(T, ui, SPEED_GAME, 5);
	answer_dialog(T, ui, ID_OK);
	settings_written(T, H, &want, 1, "an OK with no change");

	/* the sound dialog */
	kp_ui_options_dialog(ui, KP_UI_OPTIONS_SOUND);
	slider_at(T, ui, SOUND_MUSIC, 80);
	slider_at(T, ui, SOUND_EFFECTS, 90);
	answer_dialog(T, ui, ID_CANCEL);
	kp_t_eq(T, ui->sound.music, want.music, "Cancel puts the music back");
	settings_written(T, H, &want, 1, "the sound dialog's Cancel");
	kp_ui_options_dialog(ui, KP_UI_OPTIONS_SOUND);
	slider_at(T, ui, SOUND_MUSIC, 80);
	slider_at(T, ui, SOUND_EFFECTS, 90);
	answer_dialog(T, ui, ID_OK);
	want.music = 80;
	want.effects = 90;
	settings_written(T, H, &want, 2, "the sound dialog's OK");

	/* the toggle's silence lasts for the run: another dialog's OK does not keep it */
	ui->sound.music = 0;
	ui->sound.effects = 0;
	kp_ui_options_dialog(ui, KP_UI_OPTIONS_TIPS);
	ui->tip_index = 9;
	answer_dialog(T, ui, ID_OK);
	want.tip = 9;
	settings_written(T, H, &want, 3, "the tips' OK under a silence");
	kp_ui_options_dialog(ui, KP_UI_OPTIONS_SPEED);
	answer_dialog(T, ui, ID_OK);
	settings_written(T, H, &want, 3, "the speed's OK under a silence");

	/* under the multiplayer byte the scroll sliders show and move the multiplayer pair (InitSpeedSlider 0x49f240,
	 * SpeedSliderChanged 0x49f360), the game speed's is disabled */
	ui->menus.multiplayer = 1;
	kp_ui_options_dialog(ui, KP_UI_OPTIONS_SPEED);
	kp_t_eq(T, slider_pos(T, ui, SPEED_MOUSE), want.multi_mouse_scroll, "the multiplayer mouse's slider");
	kp_t_eq(T, slider_pos(T, ui, SPEED_KEY), want.multi_key_scroll, "the multiplayer keys' slider");
	kp_t_ok(T, !slider_enabled(T, ui, SPEED_GAME), "the game speed's slider disabled");
	slider_at(T, ui, SPEED_MOUSE, 2);
	slider_at(T, ui, SPEED_KEY, 6);
	kp_t_eq(T, ui->multi_mouse_scroll_speed, 2, "the multiplayer mouse's speed at once");
	kp_t_eq(T, ui->multi_key_scroll_speed, 6, "the multiplayer keys' speed at once");
	kp_t_eq(T, *kp_ui_scroll_speed(ui, 1), 6, "the view's");
	kp_t_eq(T, ui->mouse_scroll_speed, want.mouse_scroll, "the single player's mouse speed as it was");
	kp_t_eq(T, ui->key_scroll_speed, want.key_scroll, "the single player's key speed as it was");
	answer_dialog(T, ui, ID_CANCEL);
	kp_t_eq(T, ui->multi_mouse_scroll_speed, want.multi_mouse_scroll, "Cancel puts the multiplayer pair back");
	kp_t_eq(T, ui->multi_key_scroll_speed, want.multi_key_scroll, NULL);
	kp_ui_options_dialog(ui, KP_UI_OPTIONS_SPEED);
	slider_at(T, ui, SPEED_MOUSE, 2);
	slider_at(T, ui, SPEED_KEY, 6);
	answer_dialog(T, ui, ID_OK);
	want.multi_mouse_scroll = 2;
	want.multi_key_scroll = 6;
	settings_written(T, H, &want, 4, "the multiplayer speed dialog's OK");
	ui->menus.multiplayer = 0;
	kp_t_eq(T, *kp_ui_scroll_speed(ui, 1), want.key_scroll, "out of multiplayer the single player's");

	/* the next run: the file written, read back, in a new interface */
	{
		uint8_t *bytes = NULL;
		size_t size = 0;
		KpError err;
		Host *next = new_host(T);
		if (!next) return;
		kp_t_eq(T, kp_profile_encode(&H->profile, &bytes, &size), 0, "the profile's bytes");
		kp_t_eq(T, kp_profile_decode(&next->profile, bytes, size, &err), 0, "read back");
		free(bytes);
		init(next);
		settings_are_in_the_interface(T, next->f.ui, &want, "the next run");
		kp_t_eq(T, next->profile.settings.game_speed, want.game_speed, "the next run's game speed");
	}
}

/* View Intro plays the logo and the expansion's intro, then opens the main menu; --cine-play's do the same. */
static void view_intro_plays_then_opens_the_main_menu(KpT *T)
{
	static const int IDS[] = { 3, 4 };
	Host *H = glue_host(T, 1);
	kp_t_ok(T, H->f.glue.cinematics, "View Intro is offered with the pack");
	menus_end(H, KP_UI_MENU_INTRO, KP_GLUE_MAIN, 0, 0, 0);
	kp_t_eq(T, H->cine_starts, 1, "the cinematics");
	kp_t_eq(T, H->cine_count, 2, NULL);
	kp_t_eq(T, H->cine_ids[0], KP_APP_CINE_LOGO, NULL);
	kp_t_eq(T, H->cine_ids[1], KP_APP_CINE_EXPANSION_INTRO, NULL);
	kp_t_eq(T, H->cine_preludes, 0, "no preludes");
	kp_t_eq(T, H->f.glue.screen, KP_GLUE_NONE, "no screen while they play");
	frames(H, 10);
	kp_t_eq(T, H->f.glue.screen, KP_GLUE_NONE, NULL);
	H->movie = 0;
	frame(H);
	in_menus(H, KP_GLUE_MAIN, "after the intro");
	kp_t_eq(T, kp_flow_cinematics(&H->f, IDS, 2), 1, "--cine-play's play");
	kp_t_eq(T, H->cine_ids[0], 3, NULL);
	H->movie = 0;
	frame(H);
	in_menus(H, KP_GLUE_MAIN, "after --cine-play's");
	if (!(H = glue_host(T, 0))) return;
	kp_t_eq(T, H->f.glue.cinematics, 0, "no pack: View Intro is no event");
	kp_t_eq(T, kp_flow_cinematics(&H->f, IDS, 2), 0, "nothing plays");
	in_menus(H, KP_GLUE_MAIN, "--cine-play without the pack");
}

/* A mission the glue names that the catalog lacks leaves nothing behind (no scene, no continuation): the main menu. */
static void glue_mission_outside_the_catalog_leaves_nothing(KpT *T)
{
	Host *H = glue_host(T, 0);
	H->f.has_continue = 1; /* as score_next leaves it */
	menus_end(H, KP_UI_MENU_MISSION, PROTOSS_ROOM, KP_DATA_MISSIONS + 1, 0, 1);
	in_menus(H, KP_GLUE_MAIN, "no such mission");
	kp_t_eq(T, H->f.has_continue, 0, "no continuation");
	kp_t_eq(T, H->builds, 0, NULL);
	kp_t_ok(T, strstr(H->last_log, "no catalog mission") != NULL, "%s", H->last_log);
}

/* A game that does not start (its world fails) leaves no session, world, console or save waiting: the glue's main
 * menu, or the saved game's load given up. */
static void start_that_fails_leaves_no_scene(KpT *T)
{
	Host *H = glue_host(T, 0);
	char why[256];
	H->fail_world = 1;
	menus_end(H, KP_UI_MENU_MISSION, PROTOSS_ROOM, mission_row(H, EXPANSION_PROTOSS01), 0, 1);
	in_menus(H, KP_GLUE_MAIN, "a failed start");
	kp_t_eq(T, H->f.hud_on, 0, NULL);
	play(H, EXPANSION_PROTOSS01);
	save(H, "flowfail");
	game_event(H, KP_EVENT_GAME_EXIT, KP_UI_EXIT_QUIT_MISSION, NULL);
	glue_sleep(H);
	H->fail_world = 1;
	kp_t_ok(T, kp_flow_load_saved(&H->f, "flowfail", why, sizeof why) != 0, "the saved game does not start");
	kp_t_ok(T, strstr(why, "does not build") != NULL, "%s", why);
	kp_t_eq_str(T, H->f.load_on_start, "", "no save waits for a game");
	kp_t_eq_str(T, H->f.pending_load, "", NULL);
	kp_t_ok(T, !H->f.playing && H->f.session == NULL && !H->world, "no game");
}

/* The game's save: its file in the preferences, its name first in the profile's list and the interface's, the files
 * synchronised; the glue's Load Saved starts its mission with the console at once and loads it before the first
 * frame, which shows the loaded state at once. */
static void save_is_written_and_the_glue_loads_it_before_the_first_frame(KpT *T)
{
	Host *H = glue_host(T, 0);
	char path[400];
	uint8_t *bytes;
	size_t size;
	int syncs, builds;
	play(H, EXPANSION_PROTOSS01);
	syncs = H->syncs;
	save(H, "flowsave");
	save_path(H, "flowsave", path, sizeof path);
	kp_t_ok(T, (bytes = kp_read_file(path, &size)) != NULL, "%s written", path);
	free(bytes);
	kp_t_ok(T, H->syncs > syncs, "the files synchronised");
	kp_t_eq(T, H->profile.save_count, 1, NULL);
	kp_t_eq_str(T, H->profile.saves[0].name, "flowsave", NULL);
	kp_t_eq(T, H->f.ui->menus.save_count, 1, "the interface's list");
	game_event(H, KP_EVENT_GAME_EXIT, KP_UI_EXIT_QUIT_MISSION, NULL);
	glue_sleep(H);
	in_menus(H, KP_GLUE_EXPANSION_CAMPAIGN, "Quit Mission");
	frame(H);
	music_is(H, MUSIC_TITLE, MUSIC_TITLE, "the menus after the game");
	builds = H->builds;
	H->restarts = 0;
	H->f.ui->menus.save_choice = 0;
	menus_end(H, KP_UI_MENU_LOAD_SAVE, KP_GLUE_EXPANSION_CAMPAIGN, 0, 0, 1);
	kp_t_eq(T, H->builds, builds + 1, "the saved game's");
	kp_t_eq_str(T, current(H), EXPANSION_PROTOSS01, "its mission");
	kp_t_ok(T, H->f.hud_on && !kp_flow_glue_active(&H->f), "the console at once, no ready room");
	kp_t_eq(T, H->f.session->loads, 1, "the save loaded in the same frame");
	kp_t_eq(T, H->restarts, 1, "the first frame shows the loaded state");
	kp_t_eq_str(T, H->last_log, "loaded flowsave", NULL);
	frame(H);
	music_is(H, MUSIC_PROTOSS_GAME_FIRST, MUSIC_PROTOSS_GAME_LAST, "the loaded game after the menus' title");
}

/* A load in the game of another mission's save starts that mission's game, which loads it too. */
static void load_of_another_missions_save_starts_its_game(KpT *T)
{
	Host *H = glue_host(T, 0);
	int builds;
	play(H, EXPANSION_PROTOSS01);
	save(H, "flowother");
	game_event(H, KP_EVENT_GAME_EXIT, KP_UI_EXIT_QUIT_MISSION, NULL);
	glue_sleep(H);
	play(H, PROTOSS01);
	builds = H->builds;
	request(H, KP_EVENT_LOAD_GAME, "flowother");
	kp_t_eq(T, H->builds, builds + 1, "the other mission's game");
	kp_t_eq_str(T, current(H), EXPANSION_PROTOSS01, NULL);
	kp_t_ok(T, H->f.hud_on && H->f.session->loads == 1, "its console, the save loaded");
	kp_t_eq_str(T, H->last_log, "loaded flowother", NULL);
	kp_t_ok(T, H->f.has_run && H->f.run.mission == mission_row(H, EXPANSION_PROTOSS01), "the save's run");
}

/* A load in the game that fails: a save that is not there leaves the game playing (ExecLoadGame 0x4729e0: nothing when
 * LoadSavedGameByName 0x498bd0 finds none); another mission's save whose game does not start leaves the menu of the
 * campaign played, the expansion's for the original's save. */
static void load_that_fails_keeps_the_game_or_opens_the_campaign_menu(KpT *T)
{
	Host *H = glue_host(T, 0);
	play(H, PROTOSS01);
	save(H, "flowgone");
	request(H, KP_EVENT_LOAD_GAME, "flownone");
	kp_t_ok(T, H->f.playing && H->f.hud_on, "the game goes on");
	kp_t_ok(T, strstr(H->last_log, "load failed: no save") != NULL, "%s", H->last_log);
	kp_t_ok(T, H->f.ui->box.open, "the player told (kp_ui_load_failed)");
	kp_t_eq_str(T, H->f.ui->box.text, (const char *)kp_ui_tbl(H->f.ui, KP_UI_GLUALL, 66), "gluAll 66");
	game_event(H, KP_EVENT_GAME_EXIT, KP_UI_EXIT_QUIT_MISSION, NULL);
	glue_sleep(H);
	play(H, EXPANSION_PROTOSS01);
	H->fail_world = 1;
	request(H, KP_EVENT_LOAD_GAME, "flowgone");
	in_menus(H, KP_GLUE_EXPANSION_CAMPAIGN, "the other mission's failed start");
}

/* The load screen's save that does not load opens the load screen again (RunLoadGameScreen 0x459970 leaves the state
 * when LoadSavedGameByName 0x498bd0 fails), which keeps the expansion its Back returns to and tells why once it stood
 * (gluAll 66 in an OK popup). */
static void glue_load_that_fails_opens_the_load_screen_again(KpT *T)
{
	Host *H = glue_host(T, 0);
	kp_profile_add_save(&H->profile, "flowmissing");
	kp_glue_saves(H->f.ui, &H->profile);
	H->f.ui->menus.save_choice = 0;
	menus_end(H, KP_UI_MENU_LOAD_SAVE, KP_GLUE_EXPANSION_CAMPAIGN, 0, 0, 1);
	kp_t_eq_str(T, H->last_log, "glue: load failed: no save", NULL);
	in_menus(H, KP_GLUE_LOAD_SCREEN, "the failed load");
	kp_t_eq(T, H->f.ui->menus.expansion, 1, "the expansion's");
	wait_for(H, "rez\\glupok.bin", MENU_FRAMES);
	kp_t_eq_str(T, H->f.ui->menus.popup_text, (const char *)kp_ui_tbl(H->f.ui, KP_UI_GLUALL, KP_UI_GLUALL_SAVE_DAMAGED),
		"the popup's text");
	kp_t_eq(T, H->f.ui->menus.refusal, 0, "shown once");
}

/* The row of the interface's saves named name, or -1. */
static int save_row(const Host *H, const char *name)
{
	int k;
	for (k = 0; k < H->f.ui->menus.save_count; k++) {
		if (strcmp(H->f.ui->menus.saves[k], name) == 0) return k;
	}
	return -1;
}

/* The load screens' reading of a save at Ok (check_save, Koprulu's: saves do not migrate, ADR 0021): this build's save
 * loads (0); a missing file and a damaged one are gluAll 66, a save of another build gluAll 67 (the statuses of
 * ValidateSaveFile 0x499750), each logged with why. */
static void check_save_tells_why_a_save_does_not_load(KpT *T)
{
	static const struct { const char *name; uint16_t want; const char *log; } ROWS[] = {
		{ "flowgood", 0, NULL }, { "flowcut", 66, "refused" }, { "flowold", 67, "another build" },
		{ "flowmissing", 66, "no file" } };
	Host *H = glue_host(T, 0);
	KpUi *ui;
	KpError err;
	char path[400];
	uint8_t *bytes, *old;
	size_t size = 0, old_size = 0, k;
	play(H, PROTOSS01);
	save(H, "flowgood");
	save(H, "flowcut");
	save(H, "flowold");
	save_path(H, "flowgood", path, sizeof path);
	kp_t_ok(T, (bytes = kp_read_file(path, &size)) != NULL, "%s", path);
	kp_t_defer(T, free, bytes);
	kp_t_ok(T, size > 100, "%s: %lu bytes", path, (unsigned long)size);
	save_path(H, "flowcut", path, sizeof path);
	kp_t_ok(T, kp_write_file_atomic(path, bytes, 100, &err) == 0, "%s", err.msg);
	old = kp_t_save_rebuilt(T, bytes, size, KP_SAVE_SCHEMA - 1, NULL, 0, &old_size);
	save_path(H, "flowold", path, sizeof path);
	kp_t_ok(T, kp_write_file_atomic(path, old, old_size, &err) == 0, "%s", err.msg);
	game_event(H, KP_EVENT_GAME_EXIT, KP_UI_EXIT_QUIT_MISSION, NULL);
	glue_sleep(H);
	kp_profile_add_save(&H->profile, "flowmissing");
	ui = H->f.ui;
	kp_glue_saves(ui, &H->profile);
	for (k = 0; k < sizeof ROWS / sizeof ROWS[0]; k++) {
		int row = save_row(H, ROWS[k].name);
		kp_t_ok(T, row >= 0, "%s listed", ROWS[k].name);
		H->last_log[0] = 0;
		kp_t_eq(T, ui->check_save(ui->host, row), ROWS[k].want, "%s", ROWS[k].name);
		if (ROWS[k].log) kp_t_ok(T, strstr(H->last_log, ROWS[k].log) != NULL, "%s: %s", ROWS[k].name, H->last_log);
	}
	kp_t_eq(T, ui->check_save(ui->host, ui->menus.save_count), KP_UI_GLUALL_SAVE_DAMAGED, "no such row");
}

/* The interface's delete: a save's file and its name in the profile (a missing file counts as deleted); a file that
 * stays keeps its name. */
static void delete_keeps_the_name_of_a_file_that_stays(KpT *T)
{
	Host *H = new_host(T);
	char held[400], inner[460];
	FILE *f;
	init(H);
	kp_profile_add_save(&H->profile, "flowstays");
	kp_profile_add_save(&H->profile, "flowmissing");
	kp_t_eq(T, H->f.ui->delete_save(H->f.ui->host, "flowmissing"), 1, "a missing file is deleted");
	kp_t_eq(T, H->profile.save_count, 1, NULL);
	kp_t_eq_str(T, H->profile.saves[0].name, "flowstays", NULL);
	kp_t_eq(T, H->profile_saves, 1, "the profile kept");
	/* a directory with a file in it cannot be removed as a file (removed with the saves' folder) */
	save_path(H, "flowstays", held, sizeof held);
	snprintf(inner, sizeof inner, "%s/held", held);
	kp_t_ok(T, kp_t_mkdir(held) == 0, "%s", held);
	kp_t_ok(T, (f = fopen(inner, "wb")) != NULL, "%s", inner);
	fclose(f);
	kp_t_eq(T, H->f.ui->delete_save(H->f.ui->host, "flowstays"), 0, "the file stays");
	kp_t_eq(T, H->profile.save_count, 1, "its name stays");
	kp_t_ok(T, strstr(H->last_log, "cannot delete") != NULL, "%s", H->last_log);
}

/* The flow over a profile of two IDs, Fenix logged in, at the glue's main menu. */
static Host *two_ids_host(KpT *T)
{
	Host *H = new_host(T);
	H->profile.character_count = 2;
	snprintf(H->profile.characters[0].name, sizeof H->profile.characters[0].name, "Fenix");
	snprintf(H->profile.characters[1].name, sizeof H->profile.characters[1].name, "Zeratul");
	snprintf(H->profile.name, sizeof H->profile.name, "Fenix");
	init(H);
	wait_for(H, "rez\\glumain.bin", MENU_FRAMES);
	return H;
}

/* Each ID's saves (the original's save\<ID>\, BuildSaveDirectory 0x499e80): one ID's name is another file than the
 * same name of another ID's, each lists and loads its own, and the login's delete of an ID deletes its files only
 * (DeleteProfileSaves 0x498a70). */
static void saves_are_each_ids_and_leave_with_it(KpT *T)
{
	Host *H = two_ids_host(T);
	char fenix[400], zeratul[400];
	uint8_t *bytes;
	size_t size;
	play(H, PROTOSS01);
	save(H, "flowsame");
	save_path(H, "flowsame", fenix, sizeof fenix);
	game_event(H, KP_EVENT_GAME_EXIT, KP_UI_EXIT_QUIT_MISSION, NULL);
	glue_sleep(H);
	snprintf(H->f.ui->menus.player, sizeof H->f.ui->menus.player, "Zeratul"); /* the login's other ID */
	play(H, PROTOSS01);
	kp_t_eq_str(T, H->profile.name, "Zeratul", NULL);
	kp_t_eq(T, H->f.ui->menus.save_count, 0, "none of Fenix's listed");
	request(H, KP_EVENT_LOAD_GAME, "flowsame");
	kp_t_ok(T, strstr(H->last_log, "load failed: no save") != NULL, "Fenix's save is not Zeratul's: %s", H->last_log);
	save(H, "flowsame");
	save_path(H, "flowsame", zeratul, sizeof zeratul);
	kp_t_ok(T, strcmp(fenix, zeratul) != 0, "another file: %s", zeratul);
	kp_t_eq(T, H->profile.save_count, 2, "both IDs' saves");
	kp_t_eq(T, H->f.ui->menus.save_count, 1, "Zeratul's one listed");
	H->f.ui->delete_profile_saves(H->f.ui->host, "zeratul");
	kp_t_ok(T, (bytes = kp_read_file(zeratul, &size)) == NULL, "the deleted ID's file gone");
	free(bytes);
	kp_t_ok(T, (bytes = kp_read_file(fenix, &size)) != NULL, "the other ID's stays");
	free(bytes);
	kp_t_eq(T, H->profile.save_count, 1, NULL);
	kp_t_eq_str(T, H->profile.saves[0].owner, "Fenix", NULL);
}

/* The glue's load takes its row of the logged-in ID's list, not of every ID's: the pool's newest save is another ID's. */
static void glue_load_takes_the_row_of_the_logged_in_ids_list(KpT *T)
{
	Host *H = two_ids_host(T);
	play(H, PROTOSS01);
	save(H, "flowfenix");
	game_event(H, KP_EVENT_GAME_EXIT, KP_UI_EXIT_QUIT_MISSION, NULL);
	glue_sleep(H);
	snprintf(H->f.ui->menus.player, sizeof H->f.ui->menus.player, "Zeratul");
	play(H, PROTOSS01);
	save(H, "flowzeratul");
	game_event(H, KP_EVENT_GAME_EXIT, KP_UI_EXIT_QUIT_MISSION, NULL);
	glue_sleep(H);
	kp_t_eq_str(T, H->profile.saves[0].name, "flowzeratul", "the pool's newest, Zeratul's");
	snprintf(H->f.ui->menus.player, sizeof H->f.ui->menus.player, "Fenix"); /* Fenix logs in again */
	H->f.ui->menus.save_choice = 0;
	menus_end(H, KP_UI_MENU_LOAD_SAVE, KP_GLUE_CAMPAIGN, 0, 0, 0);
	kp_t_eq_str(T, H->last_log, "loaded flowfenix", "the first of Fenix's list");
	kp_t_eq_str(T, current(H), PROTOSS01, NULL);
}

/* A profile before schema 3: its saves' files get their IDs' names at the start and the profile is written; a save
 * whose file is not there leaves the list, one renamed already (by a check's run, which does not write the profile)
 * stays. */
static void old_save_files_get_their_ids_names(KpT *T)
{
	Host *H = new_host(T);
	char old[400], file[200];
	uint8_t *bytes;
	size_t size;
	KpError err;
	H->profile.character_count = 1;
	snprintf(H->profile.characters[0].name, sizeof H->profile.characters[0].name, "Fenix");
	snprintf(H->profile.name, sizeof H->profile.name, "Fenix");
	kp_profile_add_save(&H->profile, "flowlost");
	kp_profile_add_save(&H->profile, "flowdone");
	kp_profile_add_save(&H->profile, "flowold");
	H->profile.old_save_files = 1;
	kp_save_old_file_name("flowold", file, sizeof file);
	snprintf(old, sizeof old, "%s%s", H->pref, file);
	track_save(H, "flowold");
	snprintf(H->saves[H->save_count++], sizeof H->saves[0], "%s", old); /* both removed at the end */
	kp_t_eq(T, kp_write_file_atomic(old, (const uint8_t *)"old", 3, &err), 0, "%s", err.msg);
	track_save(H, "flowdone");
	kp_t_eq(T, kp_write_file_atomic(H->saves[2], (const uint8_t *)"new", 3, &err), 0, "%s", err.msg);
	init(H);
	kp_t_ok(T, (bytes = kp_read_file(H->saves[0], &size)) != NULL && size == 3, "renamed to %s", H->saves[0]);
	free(bytes);
	kp_t_ok(T, (bytes = kp_read_file(old, &size)) == NULL, "the old name gone");
	free(bytes);
	kp_t_eq(T, H->profile.old_save_files, 0, "once");
	kp_t_eq(T, H->profile.save_count, 2, "the save without a file leaves the list");
	kp_t_eq_str(T, H->profile.saves[0].name, "flowold", NULL);
	kp_t_eq_str(T, H->profile.saves[1].name, "flowdone", "the one renamed already stays");
	kp_t_eq(T, H->profile_saves, 1, "the profile written");
	kp_t_eq(T, H->f.ui->menus.save_count, 2, "listed");
}

/* A failure in the game's frame stops the game (its last state kept, no more steps) and the flow goes on: the
 * console runs and its Quit Mission leaves for the menus. */
static void game_failure_stops_the_game_and_the_flow_goes_on(KpT *T)
{
	Host *H = glue_host(T, 0);
	int ticks;
	play(H, EXPANSION_PROTOSS01);
	H->fail_game = 1;
	frame(H);
	kp_t_ok(T, H->f.session->failed, "the game stopped");
	kp_t_ok(T, strstr(H->f.session->error, "the test's failure") != NULL, "%s", H->f.session->error);
	kp_t_ok(T, strstr(H->last_log, "stopped: ") != NULL, "%s", H->last_log);
	ticks = H->ticks;
	frames(H, 100);
	kp_t_eq(T, H->ticks, ticks, "no more steps");
	kp_t_ok(T, H->f.playing && H->f.hud_on, "the console goes on");
	game_event(H, KP_EVENT_GAME_EXIT, KP_UI_EXIT_QUIT_MISSION, NULL);
	glue_sleep(H);
	in_menus(H, KP_GLUE_EXPANSION_CAMPAIGN, "Quit Mission after the failure");
}

/* At the start the profile's video options under the ones the flags gave, the interface's dialog showing them; the
 * development checks keep the originals'. */
static void video_options_come_from_the_profile_under_the_flags(KpT *T)
{
	int check;
	for (check = 0; check < 2; check++) {
		Host *H = new_host(T);
		KpUi *ui;
		H->profile.video.gamma = 150;
		H->profile.video.portraits = KP_UI_PORTRAITS_STILL;
		H->profile.video.graphics = KP_GRAPHICS_HD2;
		H->profile.video.fog = KP_FOG_SMOOTH;
		H->profile.video.water = KP_WATER_ANIMATED;
		H->profile.video.font = KP_VTEXT_VECTOR;
		H->profile.video.lighting = KP_LIGHTING_OFF;
		H->profile.video.renderer = KP_RENDERER_GPU;
		H->flags.graphics = KP_GRAPHICS_ORIGINAL;
		H->flags.fog_style = H->flags.water = H->flags.font_mode = H->flags.lighting = KP_VIDEO_AUTO;
		H->flags.renderer = KP_VIDEO_AUTO;
		H->flags.video_given = KP_VIDEO_GIVEN_GRAPHICS;
		H->flags.originals = check;
		H->available = KP_VIDEO_HAS_HD2 | KP_VIDEO_HAS_VECTOR;
		init(H);
		ui = H->f.ui;
		kp_t_eq(T, H->flags.graphics, KP_GRAPHICS_ORIGINAL, "the flag's art");
		kp_t_eq(T, ui->video_options.graphics, KP_GRAPHICS_ORIGINAL, NULL);
		kp_t_eq(T, ui->video_options.available, KP_VIDEO_HAS_HD2 | KP_VIDEO_HAS_VECTOR, "the host's choices");
		kp_t_eq_str(T, ui->video_options.gpu_name, "Vulkan", "SDL_GPU's button the host's API");
		if (check) {
			kp_t_eq(T, H->f.gamma, KP_UI_GAMMA_DEFAULT, "a check's gamma");
			kp_t_eq(T, H->f.portraits, KP_UI_PORTRAITS_ANIMATED, NULL);
			kp_t_eq(T, H->flags.fog_style, KP_VIDEO_AUTO, "a check keeps the flags");
			kp_t_eq(T, H->flags.lighting, KP_VIDEO_AUTO, NULL);
			kp_t_eq(T, H->flags.renderer, KP_VIDEO_AUTO, NULL);
		} else {
			kp_t_eq(T, H->f.gamma, 150, "the profile's gamma");
			kp_t_eq(T, H->gamma, 150, "on the host's HD art");
			kp_t_eq(T, ui->gamma, 150, "in the dialog");
			kp_t_eq(T, H->f.portraits, KP_UI_PORTRAITS_STILL, NULL);
			kp_t_eq(T, ui->portrait.mode, KP_UI_PORTRAITS_STILL, NULL);
			kp_t_eq(T, H->flags.fog_style, KP_FOG_SMOOTH, "the profile's fog");
			kp_t_eq(T, H->flags.water, KP_WATER_ANIMATED, NULL);
			kp_t_eq(T, H->flags.font_mode, KP_VTEXT_VECTOR, NULL);
			kp_t_eq(T, H->flags.lighting, KP_LIGHTING_OFF, NULL);
			kp_t_eq(T, H->flags.renderer, KP_RENDERER_GPU, "the profile's renderer (the host's start read it too)");
			kp_t_eq(T, kp_flow_software_gpu(&H->flags), 0, "the profile's Vulkan wants a hardware device");
			kp_t_eq(T, ui->video_options.fog, KP_FOG_SMOOTH, NULL);
			kp_t_eq(T, ui->video_options.renderer, KP_RENDERER_GPU, NULL);
		}
	}
}

/* A software Vulkan device draws only under a given --gfx gpu: not under auto or gl, nor for the profile's choice the
 * flow copied into the flags (ADR 0028). */
static void software_gpu_only_under_the_given_flag(KpT *T)
{
	static const struct {
		int renderer, given, software;
	} cases[] = {
		{ KP_RENDERER_GPU, 1, 1 },
		{ KP_RENDERER_GPU, 0, 0 },
		{ KP_RENDERER_GL, 1, 0 },
		{ KP_VIDEO_AUTO, 1, 0 },
		{ KP_VIDEO_AUTO, 0, 0 },
	};
	size_t k;
	for (k = 0; k < sizeof cases / sizeof cases[0]; k++) {
		KpAppFlags fl;
		memset(&fl, 0, sizeof fl);
		fl.renderer = cases[k].renderer;
		fl.video_given = KP_VIDEO_GIVEN_GRAPHICS | (cases[k].given ? KP_VIDEO_GIVEN_RENDERER : 0);
		kp_t_eq(T, kp_flow_software_gpu(&fl), cases[k].software, "renderer %d, given %d", cases[k].renderer,
			cases[k].given);
	}
}

/* The dialog's changes apply at once (the flags, the gamma); its OK keeps them in the profile, but not a check's. */
static void video_dialog_applies_and_keeps_its_options(KpT *T)
{
	int check;
	for (check = 0; check < 2; check++) {
		Host *H = new_host(T);
		KpUiVideoOptions *o;
		int saves;
		H->flags.originals = check;
		init(H);
		o = &H->f.ui->video_options;
		o->fog = KP_FOG_SMOOTH;
		o->water = KP_WATER_ANIMATED;
		o->renderer = KP_RENDERER_GL;
		H->f.ui->gamma = 70;
		o->serial++;
		frame(H);
		kp_t_eq(T, H->flags.fog_style, KP_FOG_SMOOTH, "applied at once");
		kp_t_eq(T, H->flags.renderer, KP_RENDERER_GL, "the renderer chosen");
		kp_t_eq(T, H->renderers, 0, "not taken under the open dialog (each choice would make the window again)");
		kp_t_eq(T, H->flags.water, KP_WATER_ANIMATED, NULL);
		kp_t_eq(T, H->f.gamma, 70, "the gamma");
		kp_t_eq(T, H->gamma, 70, "on the host's HD art");
		saves = H->profile_saves;
		H->f.ui->options_committed |= KP_UI_SECTION_VIDEO;
		frame(H);
		kp_t_ok(T, H->renderers == 1 && H->renderer == KP_RENDERER_GL, "the renderer taken at the OK (%d times, %d)",
			H->renderers, H->renderer);
		frame(H);
		kp_t_eq(T, H->renderers, 1, "once");
		if (check) {
			kp_t_eq(T, H->profile.video.fog, KP_VIDEO_AUTO, "a check's options are not the player's");
			kp_t_eq(T, H->profile.video.gamma, KP_UI_GAMMA_DEFAULT, NULL);
		} else {
			kp_t_eq(T, H->profile.video.fog, KP_FOG_SMOOTH, "kept at OK");
			kp_t_eq(T, H->profile.video.water, KP_WATER_ANIMATED, NULL);
			kp_t_eq(T, H->profile.video.renderer, KP_RENDERER_GL, NULL);
			kp_t_eq(T, H->profile.video.gamma, 70, NULL);
			kp_t_ok(T, H->profile_saves > saves, "the profile written");
		}
	}
}

/* The view keeps to the console's limits (InitScreenScroll's, on the 8-pixel grid), and a new screen size keeps its
 * centre. */
static void camera_keeps_to_the_view_and_its_centre(KpT *T)
{
	Host *H = glue_host(T, 0);
	const KpMission *m;
	int64_t max_x, max_y, x, y;
	int32_t vw, vh, w, h;
	play(H, EXPANSION_PROTOSS01);
	m = kp_session_mission(H->f.session);
	kp_hud_view_size(H->f.world_w, H->f.world_h, &vw, &vh);
	max_x = (int64_t)m->width * KP_WORLD_TILE - vw;
	max_y = ((int64_t)m->height - 12) * KP_WORLD_TILE + 8 - (vh - VIEW_ROWS_ORIGINAL);
	max_x -= max_x % 8;
	max_y -= max_y % 8;
	H->f.cam_x = H->f.cam_y = 1 << 20;
	kp_flow_clamp_camera(&H->f);
	kp_t_eq(T, H->f.cam_x, max_x, "the view's right limit");
	kp_t_eq(T, H->f.cam_y, max_y, "its bottom limit");
	H->f.cam_x = H->f.cam_y = -5;
	kp_flow_clamp_camera(&H->f);
	kp_t_ok(T, H->f.cam_x == 0 && H->f.cam_y == 0, "its top-left");
	H->f.cam_x = x = 800;
	H->f.cam_y = y = 600;
	w = H->f.world_w;
	h = H->f.world_h;
	H->f.safe_w = H->f.full_w = SCREEN_W + 640;
	frame(H);
	kp_t_ok(T, H->f.world_w != w, "a wider screen");
	kp_hud_keep_focus(w, h, H->f.world_w, H->f.world_h, &x, &y);
	kp_t_eq(T, H->f.cam_x, x, "the view keeps its centre");
	kp_t_eq(T, H->f.cam_y, y, NULL);
}

/* A flow freed before its host was set (the product's quit after a start that failed before the flow's) asks the host
 * nothing. */
static void free_before_the_host_asks_it_nothing(KpT *T)
{
	KpFlow *f = (KpFlow *)calloc(1, sizeof *f);
	kp_t_ok(T, f != NULL, "out of memory");
	kp_t_defer(T, free, f);
	kp_flow_free(f);
	kp_t_ok(T, f->ui == NULL && !f->glue_on && !f->world && f->session == NULL, "nothing left");
}

/* --mission plays the mission at once without the glue: the console with an interface of its own; a name the
 * catalog lacks, or a play check out of range, refuses to start. */
static void mission_flag_plays_without_the_glue(KpT *T)
{
	Host *H = new_host(T);
	char err[256];
	snprintf(H->flags.mission, sizeof H->flags.mission, "%s", EXPANSION_PROTOSS01);
	init(H);
	kp_t_eq(T, H->f.glue_on, 0, "no glue");
	kp_t_ok(T, H->f.playing && H->f.hud_on, "the console at once");
	kp_t_eq(T, H->interfaces, 2, "the console's interface of its own");
	kp_t_eq(T, H->flags.play, 1, "in play mode");
	kp_t_ok(T, H->f.has_run, "a run of its campaign");
	frames(H, 100);
	kp_t_ok(T, H->ticks > 0, "the game steps");
	game_event(H, KP_EVENT_GAME_EXIT, KP_UI_EXIT_PROGRAM, NULL);
	kp_t_eq(T, H->quits, 1, "Exit Program ends the program");
	if (!(H = new_host(T))) return;
	snprintf(H->flags.mission, sizeof H->flags.mission, "nowhere/none");
	kp_t_ok(T, kp_flow_init(&H->f, err, sizeof err) != 0, "no such mission");
	kp_t_eq_str(T, err, "no mission nowhere/none", NULL);
}

/* A mission whose files are on their way (the web's repack group, kp_app_group) starts once they came, the frames
 * before it no game; one whose files cannot be had says why: the glue's main menu, a run of --mission ended. */
static void mission_waits_for_its_files(KpT *T)
{
	Host *H = new_host(T);
	int k;
	snprintf(H->flags.mission, sizeof H->flags.mission, "%s", EXPANSION_PROTOSS01);
	H->files_wait = 3;
	init(H);
	kp_t_ok(T, H->f.waiting && !H->f.playing && H->f.session == NULL, "waiting, no game yet");
	kp_t_ok(T, strstr(H->last_log, "waiting for its files") != NULL, "%s", H->last_log);
	for (k = 0; k < 2; k++) {
		frame(H);
		kp_t_ok(T, H->f.waiting && !H->f.playing && H->builds == 0, "frame %d: still waiting", k);
	}
	frame(H);
	kp_t_ok(T, !H->f.waiting && H->f.playing && H->f.hud_on && H->builds == 1, "started once they came");
	kp_t_eq(T, H->files_asks, 4, "asked at the start and on each frame until then");
	frames(H, 50);
	kp_t_ok(T, H->ticks > 0, "the game steps");
	kp_t_eq(T, H->quits, 0, NULL);
	/* a run of --mission whose files cannot be had ends */
	if (!(H = new_host(T))) return;
	snprintf(H->flags.mission, sizeof H->flags.mission, "%s", EXPANSION_PROTOSS01);
	H->files_wait = 1;
	H->files_fail = 1;
	init(H);
	frame(H);
	kp_t_ok(T, !H->f.waiting && !H->f.playing && H->f.session == NULL && !H->world, "no game");
	kp_t_ok(T, strstr(H->last_log, "its group cannot be fetched") != NULL, "%s", H->last_log);
	kp_t_eq(T, H->quits, 1, "the run ends");
	/* the glue's: back at the main menu */
	if (!(H = glue_host(T, 0))) return;
	H->files_wait = 1;
	H->files_fail = 1;
	menus_end(H, KP_UI_MENU_MISSION, PROTOSS_ROOM, mission_row(H, EXPANSION_PROTOSS01), 0, 1);
	kp_t_ok(T, H->f.waiting, "waiting");
	frame(H);
	in_menus(H, KP_GLUE_MAIN, "files that cannot be had");
	kp_t_ok(T, strstr(H->last_log, "its group cannot be fetched") != NULL, "%s", H->last_log);
	kp_t_eq(T, H->quits, 0, NULL);
}

/* A script's name resolved as the parse does it, with its argument. */
static KpScriptRef script_ref(KpT *T, KpScriptKind kind, const char *word, int64_t arg)
{
	KpScriptName name;
	KpScriptRef ref;
	kp_t_ok(T, kp_script_flow_name(kind, word, &name) == 0, "no name %s", word);
	ref.id = name.id;
	ref.arg = arg;
	return ref;
}

static KpScriptStep script_step(KpScriptOp op, int flag, int32_t x, int32_t y)
{
	KpScriptStep s;
	memset(&s, 0, sizeof s);
	s.op = op;
	s.flag = flag;
	s.p.x = x;
	s.p.y = y;
	return s;
}

/* A script's names over the flow (kp_script_flow.c): --mission's game, the camera and the pause, the game menu by F10
 * and Return to Game clicked at its control's point; the glue's main menu is no game. */
static void script_names_hold_over_the_flow(KpT *T)
{
	Host *H = new_host(T);
	KpScriptName name;
	KpScriptRef game, nodialog, any, menu, paused, tick, cam_x, cam_y;
	KpScriptPoint p;
	KpScriptStep s;
	char why[256] = "";
	int64_t v, w;
	int32_t x, y, vw, vh;
	int k;
	kp_t_ok(T, kp_script_flow_name(KP_SCRIPT_NAME_DIALOG, "GameMenu", &name) == 0, "a dialog's name in any case");
	kp_t_ok(T, kp_script_flow_name(KP_SCRIPT_NAME_DIALOG, "gamemenu.bin", &name) != 0, "its name without .bin");
	kp_t_ok(T, kp_script_flow_name(KP_SCRIPT_NAME_CONTROL, "gamemenu.nothing", &name) != 0, "no such control");
	kp_t_ok(T, kp_script_flow_name(KP_SCRIPT_NAME_VALUE, "camera", &name) != 0, "no such value");
	kp_t_ok(T, kp_script_flow_name(KP_SCRIPT_NAME_COND, "dialog", &name) == 0 && name.arg == KP_SCRIPT_ARG_OPT_NAME
		&& name.arg_kind == KP_SCRIPT_NAME_DIALOG, "dialog takes a dialog's name");
	game = script_ref(T, KP_SCRIPT_NAME_COND, "game", 0);
	nodialog = script_ref(T, KP_SCRIPT_NAME_COND, "nodialog", 0);
	any = script_ref(T, KP_SCRIPT_NAME_COND, "dialog", -1);
	menu = script_ref(T, KP_SCRIPT_NAME_COND, "dialog", script_ref(T, KP_SCRIPT_NAME_DIALOG, "gamemenu", 0).id);
	paused = script_ref(T, KP_SCRIPT_NAME_COND, "paused", 0);
	tick = script_ref(T, KP_SCRIPT_NAME_VALUE, "tick", 0);
	cam_x = script_ref(T, KP_SCRIPT_NAME_VALUE, "camera.x", 0);
	cam_y = script_ref(T, KP_SCRIPT_NAME_VALUE, "camera.y", 0);

	snprintf(H->flags.mission, sizeof H->flags.mission, "%s", EXPANSION_PROTOSS01);
	init(H);
	for (k = 0; k < END_FRAMES && !kp_script_flow_cond(&H->f, &game, why, sizeof why); k++) frame(H);
	kp_t_ok(T, kp_script_flow_cond(&H->f, &game, why, sizeof why), "the game in play: %s", why);
	kp_t_ok(T, kp_script_flow_cond(&H->f, &nodialog, why, sizeof why), "no dialog: %s", why);
	kp_t_ok(T, !kp_script_flow_cond(&H->f, &paused, why, sizeof why), "not paused");
	kp_t_eq_str(T, why, "the game runs", NULL);
	kp_t_ok(T, kp_script_flow_value(&H->f, NULL, &tick, &v, why, sizeof why) == 0 && v >= 0, "the tick: %s", why);

	/* the camera: the view's centre */
	s = script_step(KP_SCRIPT_CAMERA, KP_SCRIPT_CAMERA_AT, 1200, 1400);
	kp_t_eq(T, kp_script_flow_act(&H->f, NULL, &s, why, sizeof why), 0, "camera: %s", why);
	kp_t_ok(T, kp_script_flow_value(&H->f, NULL, &cam_x, &v, why, sizeof why) == 0, "%s", why);
	kp_t_ok(T, kp_script_flow_value(&H->f, NULL, &cam_y, &w, why, sizeof why) == 0, "%s", why);
	kp_t_ok(T, v == 1200 && w == 1400, "the view's centre at 1200,1400, not %lld,%lld", (long long)v, (long long)w);
	kp_hud_view_size(H->f.world_w, H->f.world_h, &vw, &vh);
	kp_t_eq(T, H->f.cam_x, 1200 - vw / 2, "its left");
	/* the points over this view: the world's 1200,1400 at its centre, center, view 100% the screen's last pixel */
	memset(&p, 0, sizeof p);
	p.kind = KP_SCRIPT_AT_WORLD;
	p.x = 1200;
	p.y = 1400;
	kp_t_ok(T, kp_script_flow_point(&H->f, NULL, &p, &x, &y, why, sizeof why) == 0 && x == vw / 2 && y == vh / 2,
		"world 1200 1400 at the view's centre, not %d,%d (%s)", (int)x, (int)y, why);
	p.kind = KP_SCRIPT_AT_CENTER;
	p.x = p.y = 0;
	kp_t_ok(T, kp_script_flow_point(&H->f, NULL, &p, &x, &y, why, sizeof why) == 0 && x == vw / 2 && y == vh / 2,
		"center, not %d,%d (%s)", (int)x, (int)y, why);
	p.kind = KP_SCRIPT_AT_VIEW;
	p.x = p.y = 100;
	kp_t_ok(T, kp_script_flow_point(&H->f, NULL, &p, &x, &y, why, sizeof why) == 0 && x == H->f.world_w - 1 && y == H->f.world_h - 1,
		"view 100%% 100%%: the screen's last pixel, not %d,%d (%s)", (int)x, (int)y, why);
	s = script_step(KP_SCRIPT_CAMERA, KP_SCRIPT_CAMERA_BY, 64, -64);
	kp_t_eq(T, kp_script_flow_act(&H->f, NULL, &s, why, sizeof why), 0, "camera by: %s", why);
	kp_t_ok(T, kp_script_flow_value(&H->f, NULL, &cam_x, &v, why, sizeof why) == 0 && v == 1264, "moved by 64");
	kp_t_ok(T, kp_script_flow_value(&H->f, NULL, &cam_y, &w, why, sizeof why) == 0 && w == 1336, "and by -64");
	s = script_step(KP_SCRIPT_CAMERA, KP_SCRIPT_CAMERA_MAX, 0, 0);
	kp_t_eq(T, kp_script_flow_act(&H->f, NULL, &s, why, sizeof why), 0, "camera max: %s", why);
	v = H->f.cam_x;
	w = H->f.cam_y;
	H->f.cam_x = H->f.cam_y = 1 << 20;
	kp_flow_clamp_camera(&H->f);
	kp_t_ok(T, H->f.cam_x == v && H->f.cam_y == w, "the view at the clamp's bottom-right");

	/* the pause */
	s = script_step(KP_SCRIPT_PAUSE, 0, 0, 0);
	kp_t_eq(T, kp_script_flow_act(&H->f, NULL, &s, why, sizeof why), 0, "pause: %s", why);
	kp_t_ok(T, kp_script_flow_cond(&H->f, &paused, why, sizeof why), "paused");
	kp_t_ok(T, kp_script_flow_value(&H->f, NULL, &tick, &v, why, sizeof why) == 0, "%s", why);
	frames(H, 10);
	kp_t_ok(T, kp_script_flow_value(&H->f, NULL, &tick, &w, why, sizeof why) == 0 && w == v, "the tick stands");
	s = script_step(KP_SCRIPT_RESUME, 0, 0, 0);
	kp_t_eq(T, kp_script_flow_act(&H->f, NULL, &s, why, sizeof why), 0, "resume: %s", why);
	kp_t_ok(T, !kp_script_flow_cond(&H->f, &paused, why, sizeof why), "resumed");
	frames(H, 10);
	kp_t_ok(T, kp_script_flow_value(&H->f, NULL, &tick, &v, why, sizeof why) == 0 && v > w, "the game steps again");
	s = script_step(KP_SCRIPT_KEY, 0, 0, 0);
	kp_t_eq(T, kp_script_flow_act(&H->f, NULL, &s, why, sizeof why), KP_SCRIPT_FLOW_NONE, "a key is the host's");

	/* F10's game menu, its Return to Game clicked at the point a script takes */
	memset(&p, 0, sizeof p);
	p.kind = KP_SCRIPT_AT_CONTROL;
	p.name = script_ref(T, KP_SCRIPT_NAME_CONTROL, "gamemenu.return", 0).id;
	kp_t_ok(T, kp_script_flow_point(&H->f, NULL, &p, &x, &y, why, sizeof why) != 0, "no menu open");
	kp_t_ok(T, strstr(why, "no gamemenu.return open") != NULL, "%s", why);
	kp_hud_key(H->f.ui, 1, KP_HUD_VK_F10, 0);
	kp_hud_key(H->f.ui, 0, KP_HUD_VK_F10, 0);
	frames(H, SETTLE_FRAMES);
	kp_t_ok(T, kp_script_flow_cond(&H->f, &menu, why, sizeof why), "the game menu: %s", why);
	kp_t_ok(T, kp_script_flow_cond(&H->f, &any, why, sizeof why), "a dialog");
	kp_t_ok(T, !kp_script_flow_cond(&H->f, &nodialog, why, sizeof why), "not none");
	kp_t_ok(T, strncmp(why, "open: gamemenu", 14) == 0, "the topmost named first: %s", why);
	kp_t_ok(T, kp_script_flow_point(&H->f, NULL, &p, &x, &y, why, sizeof why) == 0, "its point: %s", why);
	kp_hud_input(&H->f.hud, KP_UI_MOUSE_MOVE, x, y, H->f.cam_x, H->f.cam_y);
	frame(H);
	kp_hud_input(&H->f.hud, KP_UI_LEFT_DOWN, x, y, H->f.cam_x, H->f.cam_y);
	frame(H);
	kp_hud_input(&H->f.hud, KP_UI_LEFT_UP, x, y, H->f.cam_x, H->f.cam_y);
	frames(H, SETTLE_FRAMES);
	kp_t_ok(T, kp_script_flow_cond(&H->f, &nodialog, why, sizeof why), "Return to Game at %d,%d closed it: %s", (int)x,
		(int)y, why);
	p.kind = KP_SCRIPT_AT_XY;
	p.x = H->f.world_w;
	p.y = 0;
	kp_t_ok(T, kp_script_flow_point(&H->f, NULL, &p, &x, &y, why, sizeof why) != 0, "a point past the screen's right");
	kp_t_ok(T, strstr(why, "off the screen") != NULL, "%s", why);

	/* the glue's main menu */
	if (!(H = glue_host(T, 0))) return;
	kp_t_ok(T, !kp_script_flow_cond(&H->f, &game, why, sizeof why), "no game in the glue");
	kp_t_eq_str(T, why, "the glue", NULL);
	kp_t_ok(T, kp_script_flow_value(&H->f, NULL, &tick, &v, why, sizeof why) != 0, "no tick");
	menu.arg = script_ref(T, KP_SCRIPT_NAME_DIALOG, "gluMain", 0).id;
	kp_t_ok(T, kp_script_flow_cond(&H->f, &menu, why, sizeof why), "its screen: %s", why);
	kp_t_ok(T, kp_script_flow_cond(&H->f, &nodialog, why, sizeof why), "no popup over it: %s", why);
	/* a replay or a saved game started under the glue's open screen would leave the menu over it: the script goes
	 * through Load Replay's or Load Saved's screen instead */
	s = script_step(KP_SCRIPT_REPLAY, 0, 0, 0);
	snprintf(s.text, sizeof s.text, "LastReplay");
	kp_t_ok(T, kp_script_flow_act(&H->f, NULL, &s, why, sizeof why) < 0, "replay refused under the glue");
	kp_t_eq_str(T, why, "the glue's screen is open: go through Load Replay", NULL);
	s = script_step(KP_SCRIPT_LOAD_FILE, 0, 0, 0);
	snprintf(s.text, sizeof s.text, "save");
	kp_t_ok(T, kp_script_flow_act(&H->f, NULL, &s, why, sizeof why) < 0, "load-file refused under the glue");
	kp_t_eq_str(T, why, "the glue's screen is open: go through Load Saved", NULL);
	kp_t_ok(T, kp_flow_glue_active(&H->f), "the main menu stays");
}

/* --- a script run over the flow: the test's host gives its inputs to the glue and the console as the product's
 * (sdl/kp_app_script.c) does through its window's events --- */

#define SCRIPT_FRAMES 40000      /* a script's frames at most (the waits' 60 s are 3750 frames each) */
#define SCRIPT_OBSERVED 131072   /* the ui lines kept */
#define STATE_LINES_MIN 100      /* a game's state has many more */
#define KEY_RETURN 0x0d

typedef struct {
	Host *H;
	KpScript s;
	int parsed;
	char ui[SCRIPT_OBSERVED];    /* every ui observation's lines, each after "== NAME" */
	size_t ui_used;
	int state_lines;
	char last_log[512], failed[512];
} ScriptRun;

static void free_script_run(void *p)
{
	ScriptRun *R = (ScriptRun *)p;
	if (R->parsed) kp_script_free(&R->s);
	free(R);
}

static void keep_ui(void *ud, const char *line, size_t length)
{
	ScriptRun *R = (ScriptRun *)ud;
	if (R->ui_used + length + 2 >= sizeof R->ui) return;
	memcpy(R->ui + R->ui_used, line, length);
	R->ui_used += length;
	R->ui[R->ui_used++] = '\n';
	R->ui[R->ui_used] = '\0';
}

static void count_state(void *ud, const char *line, size_t length)
{
	(void)line;
	(void)length;
	((ScriptRun *)ud)->state_lines++;
}

/* The keys the test's scripts press (the product's host has SDL's whole keyboard): a letter, Escape, Return, F10. */
static int run_name(void *ud, KpScriptKind kind, const char *word, KpScriptName *out)
{
	(void)ud;
	if (kind != KP_SCRIPT_NAME_KEY) return kp_script_flow_name(kind, word, out);
	out->arg = KP_SCRIPT_ARG_NONE;
	out->arg_kind = KP_SCRIPT_NAME_KEY;
	if (word[0] >= 'A' && word[0] <= 'Z' && !word[1]) out->id = word[0];
	else if (strcmp(word, "Escape") == 0) out->id = KEY_ESCAPE;
	else if (strcmp(word, "Return") == 0) out->id = KEY_RETURN;
	else if (strcmp(word, "F10") == 0) out->id = KP_HUD_VK_F10;
	else return -1;
	return 0;
}

/* The parse's error for text over the flow's names into msg, "" when it parses. */
static void parse_error(const char *text, char *msg, size_t size)
{
	KpScriptHost h;
	KpScript s;
	KpError e;
	memset(&h, 0, sizeof h);
	h.name = run_name;
	if (kp_script_parse(&s, "names.kps", text, strlen(text), &h, &e) == 0) {
		kp_script_free(&s);
		msg[0] = '\0';
		return;
	}
	snprintf(msg, size, "%s", e.msg);
}

static int run_value(void *ud, const KpScriptRun *run, const KpScriptRef *ref, int64_t *out, char *why, size_t size)
{
	return kp_script_flow_value(&((ScriptRun *)ud)->H->f, run, ref, out, why, size);
}

static int run_cond(void *ud, const KpScriptRun *run, const KpScriptRef *ref, char *why, size_t size)
{
	(void)run;
	return kp_script_flow_cond(&((ScriptRun *)ud)->H->f, ref, why, size);
}

/* A mouse event at the logical point: the glue's, else the console's. */
static void run_mouse(Host *H, int event, int32_t x, int32_t y)
{
	if (kp_flow_glue_active(&H->f)) kp_glue_input(&H->f.glue, event, x, y);
	else kp_hud_input(&H->f.hud, event, x, y, H->f.cam_x, H->f.cam_y);
}

static int run_exec(void *ud, KpScriptRun *run, const KpScriptStep *st, char *why, size_t size)
{
	ScriptRun *R = (ScriptRun *)ud;
	Host *H = R->H;
	int32_t x, y;
	char text[2] = { 0, 0 };
	int r;
	switch (st->op) {
	case KP_SCRIPT_KEY:
		kp_hud_key(H->f.ui, 1, (uint16_t)st->id, 0);
		if (st->id >= 'A' && st->id <= 'Z') {
			text[0] = (char)(st->id - 'A' + 'a');
			kp_hud_text(H->f.ui, text);
		}
		kp_hud_key(H->f.ui, 0, (uint16_t)st->id, 0);
		return 0;
	case KP_SCRIPT_TEXT: kp_hud_text(H->f.ui, st->text); return 0;
	case KP_SCRIPT_CLICK:
		if (kp_script_flow_point(&H->f, run, &st->p, &x, &y, why, size) != 0) return -1;
		run_mouse(H, KP_UI_MOUSE_MOVE, x, y);
		run_mouse(H, KP_UI_LEFT_DOWN, x, y);
		run_mouse(H, KP_UI_LEFT_UP, x, y);
		return 0;
	case KP_SCRIPT_UI:
		if (R->ui_used + KP_SCRIPT_STRING + 8 < sizeof R->ui) {
			R->ui_used += (size_t)snprintf(R->ui + R->ui_used, sizeof R->ui - R->ui_used, "== %s\n", st->text);
		}
		return kp_script_flow_ui(&H->f, keep_ui, R, why, size);
	case KP_SCRIPT_STATE: return kp_script_flow_state(&H->f, count_state, R, why, size);
	default:
		r = kp_script_flow_act(&H->f, run, st, why, size);
		if (r != KP_SCRIPT_FLOW_NONE) return r;
		snprintf(why, size, "not in the test's host");
		return -1;
	}
}

static void run_log(void *ud, const char *line)
{
	ScriptRun *R = (ScriptRun *)ud;
	snprintf(R->last_log, sizeof R->last_log, "%s", line);
	if (strstr(line, " FAILED")) snprintf(R->failed, sizeof R->failed, "%s", line);
}

/* The script `text` parsed and run a frame at a time over the host's flow, on the host's clock, to its end. */
static ScriptRun *run_script(Host *H, const char *name, const char *text)
{
	KpT *T = H->T;
	ScriptRun *R = (ScriptRun *)calloc(1, sizeof *R);
	KpScriptHost h;
	KpScriptRun run;
	KpError e;
	int k;
	kp_t_ok(T, R != NULL, "out of memory");
	kp_t_defer(T, free_script_run, R);
	R->H = H;
	memset(&h, 0, sizeof h);
	h.ud = R;
	h.name = run_name;
	h.value = run_value;
	h.cond = run_cond;
	h.exec = run_exec;
	h.log = run_log;
	kp_t_ok(T, kp_script_parse(&R->s, name, text, strlen(text), &h, &e) == 0, "the script: %s", e.msg);
	R->parsed = 1;
	kp_script_start(&run, &R->s);
	for (k = 0; k < SCRIPT_FRAMES && kp_script_frame(&run, &h, (int64_t)(H->now * 1000)); k++) frame(H);
	kp_t_ok(T, run.done, "the script ended in %d frames (at its step %d)", SCRIPT_FRAMES, run.step);
	kp_t_eq(T, run.failures, 0, "%s", R->failed);
	kp_t_eq(T, run.steps, R->s.count, "every step ran: %s", R->last_log);
	return R;
}

/* A script walks the glue's menus as a new player does: Single Player, Brood War, the new ID's popup (no profile yet)
 * and its name typed, the login's OK, the campaign menu's load screen and back, the player's change and the login's
 * Cancel to the main menu; its ui lines name each control as `control` takes it. */
static void a_script_walks_the_glues_menus(KpT *T)
{
	static const char SCRIPT[] =
		"wait screen gluMain\n"
		"ui main\n"
		"click control glumain.single\n"
		"wait dialog gluGameMode and still\n"
		"ui game-type\n"
		"click control glugamemode.expansion\n"
		"wait dialog gluPEdit # a new player: the ID's popup comes by itself\n"
		"text \"Script\"\n"
		"click control gluPEdit.1\n"
		"wait screen gluLogin\n"
		"ui login\n"
		"click control glulogin.ok\n"
		"wait screen gluExpCmpgn\n"
		"click control gluexpcmpgn.load\n"
		"wait screen gluLoad\n"
		"click control gluload.cancel\n"
		"wait screen gluExpCmpgn\n"
		"click control gluexpcmpgn.player\n"
		"wait screen gluLogin\n"
		"click control glulogin.cancel\n"
		"wait screen gluMain\n";
	Host *H = glue_host(T, 0);
	ScriptRun *R;
	KpScriptRef cond;
	char why[256];
	cond = script_ref(T, KP_SCRIPT_NAME_COND, "screen", script_ref(T, KP_SCRIPT_NAME_DIALOG, "gluLogin", 0).id);
	kp_t_ok(T, !kp_script_flow_cond(&H->f, &cond, why, sizeof why), "not the login");
	kp_t_eq_str(T, why, "the glue: gluMain", NULL);
	R = run_script(H, "glue.kps", SCRIPT);
	kp_t_eq_str(T, H->f.ui->menus.profiles[0].name, "Script", "the new ID");
	kp_t_ok(T, strstr(R->ui, "== main\n# ui: the glue, the logical screen ") != NULL, "%.200s", R->ui);
	kp_t_ok(T, strstr(R->ui, "\ndialog gluMain box ") != NULL && strstr(R->ui, " (screen)") != NULL, "the main menu");
	kp_t_ok(T, strstr(R->ui, "\n  gluMain.3 flic-button at ") != NULL, "Single Player's address and kind");
	kp_t_ok(T, strstr(R->ui, " \"Single Player\" key S = glumain.single\n") != NULL, "its text, hotkey and name");
	kp_t_ok(T, strstr(R->ui, "\ndialog gluGameMode box ") != NULL && strstr(R->ui, " (popup)") != NULL, "the game type's popup");
	kp_t_ok(T, strstr(R->ui, "= glugamemode.expansion\n") != NULL, "its Brood War");
	kp_t_ok(T, strstr(R->ui, "    item 0 \"Script\" selected\n") != NULL, "the login's list holds the new ID");
}

/* A script in a game: its values read, its state and ui kept, the game menu by F10 closed at its Return to Game; the
 * console's own dialogs are no dialog to a script, and every value of the flow reads in a game and in the glue. */
static void a_script_observes_a_game(KpT *T)
{
	static const char SCRIPT[] =
		"wait game\n"
		"mark start\n"
		"log start tick camera.x camera.tile.x minimap.x selected units:0 gamma video.graphics video.commits portraits\n"
		"expect nodialog and @start.tick >= 0\n"
		"state start\n"
		"ui start\n"
		"key F10\n"
		"wait dialog gamemenu\n"
		"ui menu\n"
		"click control gamemenu.return\n"
		"wait nodialog\n"
		"wait tick > @start.tick + 4\n";
	static const char *const GAME_VALUES[] = { "tick", "camera.x", "camera.y", "camera.tile.x", "camera.tile.y",
		"minimap.x", "minimap.y", "selected", "creep.tiles", "creep.receding", "trigger.waiting", "portrait.ms",
		"targeting", "placing", "groups.held", "pad.open", "pad.count" };
	static const char *const UI_VALUES[] = { "gamma", "portraits", "video.graphics", "video.fog", "video.water",
		"video.font", "video.lighting", "video.serial", "video.commits", "text.vector" };
	static const struct {
		const char *text, *error; /* "" it parses */
	} PARSES[] = {
		{ "log a units:11 pad.shown:11\n", "" },
		{ "log a units:12\n", "names.kps:1: the value units takes a number 0..11: units:N" },
		{ "log a pad.shown:12\n", "the value pad.shown takes a number 0..11" },
		{ "wait dialog gamemenu\n", "" },
		{ "wait dialog minimap\n", "minimap" },
		{ "wait screen statdata\n", "statdata" },
	};
	Host *H = new_host(T);
	ScriptRun *R;
	KpScriptRef ref;
	KpScriptName name;
	char why[256];
	int64_t v;
	int k, glue;
	snprintf(H->flags.mission, sizeof H->flags.mission, "%s", EXPANSION_PROTOSS01);
	init(H);
	R = run_script(H, "game.kps", SCRIPT);
	kp_t_ok(T, R->state_lines > STATE_LINES_MIN, "the state's lines: %d", R->state_lines);
	kp_t_ok(T, strstr(R->ui, "== start\n# ui: a game, the logical screen ") != NULL, "%.200s", R->ui);
	kp_t_ok(T, strstr(R->ui, "\ndialog minimap box ") != NULL && strstr(R->ui, " (console)") != NULL, "the console's");
	kp_t_ok(T, strstr(R->ui, "\ndialog gamemenu box ") != NULL && strstr(R->ui, " (modeless)") != NULL, "the game menu");
	kp_t_ok(T, strstr(R->ui, "\n  gamemenu.-3 default-button at ") != NULL, "Return to Game's address and kind");
	kp_t_ok(T, strstr(R->ui, " \"Return to Game (Esc)\" key Escape = cancel, gamemenu.return\n") != NULL,
		"its text, hotkey and names");
	ref = script_ref(T, KP_SCRIPT_NAME_COND, "dialog", -1);
	kp_t_ok(T, !kp_script_flow_cond(&H->f, &ref, why, sizeof why), "no dialog over the console");
	kp_t_eq_str(T, why, "open: no dialog", "the console's own are left out");
	kp_t_ok(T, kp_script_flow_name(KP_SCRIPT_NAME_VALUE, "units", &name) == 0 && name.arg == KP_SCRIPT_ARG_INT, "units:N");
	ref.id = name.id;
	ref.arg = 12;
	kp_t_ok(T, kp_script_flow_value(&H->f, NULL, &ref, &v, why, sizeof why) != 0, "no player 12");
	ref.arg = H->f.session->g->local_player;
	kp_t_ok(T, kp_script_flow_value(&H->f, NULL, &ref, &v, why, sizeof why) == 0 && v > 0, "the player's own units");
	kp_t_ok(T, kp_script_flow_name(KP_SCRIPT_NAME_CONTROL, "gamemenu.x", &name) != 0, "DIALOG.ID takes a number");
	kp_t_ok(T, kp_script_flow_name(KP_SCRIPT_NAME_CONTROL, "nodialog.3", &name) != 0, "and a dialog of the table");
	kp_t_ok(T, kp_script_flow_name(KP_SCRIPT_NAME_CONTROL, "minimap.1", &name) == 0, "a console's control by its ID");
	/* the parse refuses an argument past its value's range and the console's dialogs as a condition's */
	for (k = 0; k < (int)(sizeof PARSES / sizeof PARSES[0]); k++) {
		parse_error(PARSES[k].text, why, sizeof why);
		if (!PARSES[k].error[0]) kp_t_eq_str(T, why, "", "%s", PARSES[k].text);
		else kp_t_ok(T, strstr(why, PARSES[k].error) != NULL, "%s: %s", PARSES[k].text, why);
	}

	/* every value reads in a game; in the glue the interface's, none of the game's */
	for (glue = 0; glue < 2; glue++) {
		if (glue && !(H = glue_host(T, 0))) return;
		for (k = 0; k < (int)(sizeof GAME_VALUES / sizeof GAME_VALUES[0]); k++) {
			ref = script_ref(T, KP_SCRIPT_NAME_VALUE, GAME_VALUES[k], -1);
			kp_t_eq(T, kp_script_flow_value(&H->f, NULL, &ref, &v, why, sizeof why) == 0, !glue, "%s in %s: %s", GAME_VALUES[k],
				glue ? "the glue" : "a game", why);
		}
		for (k = 0; k < (int)(sizeof UI_VALUES / sizeof UI_VALUES[0]); k++) {
			ref = script_ref(T, KP_SCRIPT_NAME_VALUE, UI_VALUES[k], -1);
			kp_t_ok(T, kp_script_flow_value(&H->f, NULL, &ref, &v, why, sizeof why) == 0, "%s: %s", UI_VALUES[k], why);
		}
	}
	kp_t_ok(T, kp_script_flow_state(&H->f, count_state, R, why, sizeof why) != 0, "no state in the glue");
	kp_t_eq_str(T, why, "no game", NULL);
}

/* A network lobby as the screens see it (KpUiNet.lobby_view), the test's */
typedef struct {
	KpUiNet net;
	KpUiLobby lobby;
} LobbyNet;

static void lobby_net_view(void *host, KpUiLobby *out) { *out = ((const LobbyNet *)host)->lobby; }

static int64_t flow_value(KpT *T, Host *H, const char *word)
{
	KpScriptRef ref = script_ref(T, KP_SCRIPT_NAME_VALUE, word, 0);
	char why[256] = "";
	int64_t v = -1;
	kp_t_ok(T, kp_script_flow_value(&H->f, NULL, &ref, &v, why, sizeof why) == 0, "%s: %s", word, why);
	return v;
}

/* The network lobby's values (phase N): lobby.humans counts its humans with a Storm ID, lobby.distributed is the
 * host's AllPeersHaveFile 0x440ba0 (the Start's check, which net_host.kps waits for); both 0 with no network game. */
static void script_reads_the_network_lobby(KpT *T)
{
	Host *H = glue_host(T, 0);
	const KpUiNet *own;
	LobbyNet *n;
	int k;
	if (!H) return;
	own = H->f.ui->net;
	kp_t_eq(T, flow_value(T, H, "lobby.humans"), 0, "no network game: no humans");
	kp_t_eq(T, flow_value(T, H, "lobby.distributed"), 0, "and nothing distributed");
	n = (LobbyNet *)calloc(1, sizeof *n);
	kp_t_ok(T, n != NULL, "out of memory");
	kp_t_defer(T, free, n);
	n->net.lobby_view = lobby_net_view;
	n->net.host = n;
	for (k = 0; k < KP_UI_LOBBY_SLOTS; k++) {
		n->lobby.slots[k].storm = -1;
		n->lobby.slots[k].type = KP_CTRL_OPEN;
	}
	n->lobby.slots[0].type = n->lobby.slots[3].type = n->lobby.slots[5].type = KP_CTRL_HUMAN;
	n->lobby.slots[0].storm = 0;
	n->lobby.slots[3].storm = 1; /* slot 5 is a human's without a Storm ID yet */
	n->lobby.slots[1].type = KP_CTRL_LOBBY_COMPUTER;
	n->lobby.host = 1;
	H->f.ui->net = &n->net;
	kp_t_eq(T, flow_value(T, H, "lobby.humans"), 2, "the humans with a Storm ID");
	kp_t_eq(T, flow_value(T, H, "lobby.distributed"), 0, "a joiner still downloading the host's map");
	n->lobby.transfer_done = 1;
	kp_t_eq(T, flow_value(T, H, "lobby.distributed"), 1, "every player holds the map");
	H->f.ui->net = own;
}

/* The network scripts of two programs parse over the flow's names: net_host.kps and net_join.kps, which tools/ci.sh
 * plays, and net_lan_join.kps, which only a desktop and a phone play (docs/phase-network.md, stage N6). */
static void network_scripts_parse(KpT *T)
{
	static const char *const SCRIPTS[] = { "tests/scripts/net_host.kps", "tests/scripts/net_join.kps",
		"tests/scripts/net_lan_join.kps" };
	char why[256];
	int k;
	for (k = 0; k < (int)(sizeof SCRIPTS / sizeof SCRIPTS[0]); k++) {
		size_t size;
		const uint8_t *bytes = kp_t_read(T, SCRIPTS[k], &size);
		char *text = (char *)malloc(size + 1);
		kp_t_ok(T, text != NULL, "out of memory");
		kp_t_defer(T, free, text);
		memcpy(text, bytes, size);
		text[size] = '\0';
		parse_error(text, why, sizeof why);
		kp_t_eq_str(T, why, "", "%s", SCRIPTS[k]);
	}
}

/* The names stage SC5 added over --mission's game: the choices', menus', options dialogs' and units' names, a pick and
 * the points and values over it, select's box over a worker taking it, the race's building key, the game menu's and an
 * options dialog opened and closed by an activated control, a save and a load asked of the flow; in the glue no pick. */
static void script_acts_over_a_game(KpT *T)
{
	Host *H = new_host(T), *G;
	KpScriptName name;
	KpScriptRef game, nodialog, has, first, moved, gone, held, savegame, video;
	KpScriptRun run;
	KpScriptStep s;
	KpScriptPoint p;
	char why[256] = "";
	int64_t v, unit;
	int32_t x, y, box[4];
	int k;
	kp_t_ok(T, kp_script_flow_name(KP_SCRIPT_NAME_CHOICE, "graphics.HD2", &name) == 0
		&& name.id == KP_VIDEO_CHOICE_ID(KP_VIDEO_ID_GRAPHICS, KP_GRAPHICS_HD2), "graphics.hd2 is its button");
	kp_t_ok(T, kp_script_flow_name(KP_SCRIPT_NAME_CHOICE, "portraits.still", &name) == 0
		&& name.id == KP_VIDEO_ID_PORTRAITS + KP_UI_PORTRAITS_STILL, "portraits.still");
	kp_t_ok(T, kp_script_flow_name(KP_SCRIPT_NAME_CHOICE, "graphics.hd3", &name) != 0, "no graphics.hd3");
	kp_t_ok(T, kp_script_flow_name(KP_SCRIPT_NAME_MENU, "save", &name) == 0 && name.id == KP_UI_GAME_MENU_SAVE, "menu save");
	kp_t_ok(T, kp_script_flow_name(KP_SCRIPT_NAME_OPTIONS, "video", &name) == 0 && name.id == KP_UI_OPTIONS_VIDEO,
		"options-dialog video");
	kp_t_ok(T, kp_script_flow_name(KP_SCRIPT_NAME_OPTIONS, "tips-automatic", &name) != 0, "no such options dialog");
	kp_t_ok(T, kp_script_flow_name(KP_SCRIPT_NAME_UNIT, "group", &name) == 0, "select group");
	kp_t_ok(T, kp_script_flow_name(KP_SCRIPT_NAME_VALUE, "moved", &name) == 0 && name.arg == KP_SCRIPT_ARG_PICK,
		"moved takes a pick");
	kp_t_ok(T, kp_script_flow_name(KP_SCRIPT_NAME_VALUE, "group.held", &name) == 0 && name.arg == KP_SCRIPT_ARG_INT
		&& name.arg_max == 9, "group.held takes a group");
	for (k = 0; k < 3; k++) {
		static const char *const GLUE[] = { "episode", "briefing", "score" };
		kp_t_ok(T, kp_script_flow_name(KP_SCRIPT_NAME_COND, GLUE[k], &name) == 0, "the condition %s", GLUE[k]);
	}
	kp_t_ok(T, kp_script_flow_control("video", 12) >= 0 && kp_script_flow_control("nothing", 12) < 0, "video.12's name");
	game = script_ref(T, KP_SCRIPT_NAME_COND, "game", 0);
	nodialog = script_ref(T, KP_SCRIPT_NAME_COND, "nodialog", 0);
	has = script_ref(T, KP_SCRIPT_NAME_VALUE, "selection.has", 0);
	first = script_ref(T, KP_SCRIPT_NAME_VALUE, "selection.first", 0);
	moved = script_ref(T, KP_SCRIPT_NAME_VALUE, "moved", 0);
	held = script_ref(T, KP_SCRIPT_NAME_VALUE, "group.held", 1);
	savegame = script_ref(T, KP_SCRIPT_NAME_COND, "dialog", script_ref(T, KP_SCRIPT_NAME_DIALOG, "savegame", 0).id);
	video = script_ref(T, KP_SCRIPT_NAME_COND, "dialog", script_ref(T, KP_SCRIPT_NAME_DIALOG, "video", 0).id);
	memset(&run, 0, sizeof run);

	/* the glue: no game to pick in */
	G = new_host(T);
	init(G);
	frames(G, 4);
	s = script_step(KP_SCRIPT_PICK, 0, 0, 0);
	kp_t_ok(T, kp_script_flow_act(&G->f, &run, &s, why, sizeof why) != 0, "no pick in the glue");
	kp_t_ok(T, kp_script_flow_value(&G->f, &run, &moved, &v, why, sizeof why) != 0, "no moved in the glue");

	snprintf(H->flags.mission, sizeof H->flags.mission, "%s", EXPANSION_PROTOSS02);
	init(H);
	for (k = 0; k < END_FRAMES && !kp_script_flow_cond(&H->f, &game, why, sizeof why); k++) frame(H);
	kp_t_ok(T, kp_script_flow_cond(&H->f, &game, why, sizeof why), "the game in play: %s", why);
	kp_t_ok(T, kp_script_flow_value(&H->f, &run, &moved, &v, why, sizeof why) != 0, "no pick yet");
	kp_t_eq_str(T, why, "no unit picked", NULL);
	kp_t_eq(T, kp_script_flow_race_key(&H->f), 'P', "the Protoss's building key");

	/* a pick: the player's unit nearest the view's centre; its point, the ground beside it, its values */
	s = script_step(KP_SCRIPT_PICK, 0, 0, 0);
	kp_t_eq(T, kp_script_flow_act(&H->f, &run, &s, why, sizeof why), 0, "pick: %s", why);
	unit = kp_unit_from_ref(H->f.session->g, run.picks[0]);
	kp_t_ok(T, unit > 0 && H->f.session->g->u.owner[unit] == H->f.session->g->local_player, "the player's own unit");
	/* a pick whose record another unit took since (its uniqueness changed) is gone, not that unit */
	run.picks[1] = run.picks[0] ^ 0x800;
	gone = moved;
	gone.arg = 1;
	kp_t_ok(T, kp_script_flow_value(&H->f, &run, &gone, &v, why, sizeof why) != 0, "a stale pick has no value");
	kp_t_eq_str(T, why, "the picked unit is gone", NULL);
	run.picks[1] = 0;
	memset(&p, 0, sizeof p);
	p.kind = KP_SCRIPT_AT_UNIT;
	p.name = 0;
	p.x = 3;
	kp_t_ok(T, kp_script_flow_point(&H->f, &run, &p, &x, &y, why, sizeof why) == 0
		&& x == H->f.session->g->u.x[unit] - H->f.cam_x + 3 && y == H->f.session->g->u.y[unit] - H->f.cam_y,
		"unit u 3 0 at the unit: %s", why);
	p.kind = KP_SCRIPT_AT_BESIDE;
	p.x = 40;
	kp_t_ok(T, kp_script_flow_point(&H->f, &run, &p, &x, &y, why, sizeof why) == 0
		&& llabs((long long)(x - (H->f.session->g->u.x[unit] - H->f.cam_x))) == 40, "beside u 40: %s", why);
	p.name = 1;
	kp_t_ok(T, kp_script_flow_point(&H->f, &run, &p, &x, &y, why, sizeof why) != 0, "no second pick");
	kp_t_ok(T, kp_script_flow_value(&H->f, &run, &moved, &v, why, sizeof why) == 0 && v == 0, "not moved yet");
	kp_t_ok(T, kp_script_flow_value(&H->f, &run, &held, &v, why, sizeof why) == 0 && v == 0, "no group 1");
	p.kind = KP_SCRIPT_AT_PAD_TOGGLE;
	kp_t_ok(T, kp_script_flow_point(&H->f, &run, &p, &x, &y, why, sizeof why) != 0, "no touch pad: %s", why);

	/* select worker 4: the camera on a Probe, a box over it takes it */
	s = script_step(KP_SCRIPT_SELECT, 0, 0, 0);
	s.id = script_ref(T, KP_SCRIPT_NAME_UNIT, "worker", 0).id;
	s.n = 4;
	kp_t_eq(T, kp_script_flow_select(&H->f, &s, box, why, sizeof why), 0, "select worker: %s", why);
	kp_t_ok(T, box[2] - box[0] == 8 && box[3] - box[1] == 8, "a box of radius 4");
	kp_hud_input(&H->f.hud, KP_UI_LEFT_DOWN, box[0], box[1], H->f.cam_x, H->f.cam_y);
	kp_hud_input(&H->f.hud, KP_UI_LEFT_UP, box[2], box[3], H->f.cam_x, H->f.cam_y);
	frames(H, 2);
	kp_t_ok(T, kp_local_selection_count(H->f.session->g) == 1, "the box took the worker");
	kp_t_eq(T, H->f.session->g->u.type[H->f.session->g->local_selection[0]], 64, "a Probe");
	kp_t_ok(T, kp_script_flow_value(&H->f, &run, &has, &v, why, sizeof why) == 0
		&& v == (H->f.session->g->local_selection[0] == unit), "selection.has:u");
	kp_t_ok(T, kp_script_flow_value(&H->f, &run, &first, &v, why, sizeof why) == 0
		&& v == (H->f.session->g->local_selection[0] == unit), "selection.first:u");

	/* select group 48: the box over the unit with the most of the player's units around takes more than one */
	s.id = script_ref(T, KP_SCRIPT_NAME_UNIT, "group", 0).id;
	s.n = 48;
	kp_t_eq(T, kp_script_flow_select(&H->f, &s, box, why, sizeof why), 0, "select group: %s", why);
	kp_hud_input(&H->f.hud, KP_UI_LEFT_DOWN, box[0], box[1], H->f.cam_x, H->f.cam_y);
	kp_hud_input(&H->f.hud, KP_UI_LEFT_UP, box[2], box[3], H->f.cam_x, H->f.cam_y);
	frames(H, 2);
	kp_t_ok(T, kp_local_selection_count(H->f.session->g) >= 2, "the group's box took %d units",
		kp_local_selection_count(H->f.session->g));

	/* a save's name longer than the profile keeps is refused, not cut */
	s = script_step(KP_SCRIPT_SAVE, 0, 0, 0);
	memset(s.text, 'x', sizeof s.text - 1);
	s.text[sizeof s.text - 1] = '\0';
	kp_t_ok(T, kp_script_flow_act(&H->f, &run, &s, why, sizeof why) != 0 && !H->f.pending_save[0], "a long save name");
	kp_t_ok(T, strstr(why, "bytes at most") != NULL, "%s", why);

	/* the save menu, closed by its Cancel activated; the video options likewise */
	s = script_step(KP_SCRIPT_MENU, 0, 0, 0);
	s.id = KP_UI_GAME_MENU_SAVE;
	kp_t_eq(T, kp_script_flow_act(&H->f, &run, &s, why, sizeof why), 0, "menu save: %s", why);
	frames(H, 2);
	kp_t_ok(T, kp_script_flow_cond(&H->f, &savegame, why, sizeof why), "the save dialog: %s", why);
	s = script_step(KP_SCRIPT_ACTIVATE, 0, 0, 0);
	s.id = script_ref(T, KP_SCRIPT_NAME_CONTROL, "cancel", 0).id;
	kp_t_eq(T, kp_script_flow_act(&H->f, &run, &s, why, sizeof why), 0, "activate cancel: %s", why);
	frames(H, 4);
	kp_t_ok(T, kp_script_flow_cond(&H->f, &nodialog, why, sizeof why), "closed: %s", why);
	kp_t_ok(T, kp_script_flow_act(&H->f, &run, &s, why, sizeof why) != 0, "no cancel to activate now");
	s = script_step(KP_SCRIPT_OPTIONS_DIALOG, 0, 0, 0);
	s.id = KP_UI_OPTIONS_VIDEO;
	kp_t_eq(T, kp_script_flow_act(&H->f, &run, &s, why, sizeof why), 0, "options-dialog video: %s", why);
	frames(H, 2);
	kp_t_ok(T, kp_script_flow_cond(&H->f, &video, why, sizeof why), "the video options: %s", why);
	s = script_step(KP_SCRIPT_ACTIVATE, 0, 0, 0);
	s.id = script_ref(T, KP_SCRIPT_NAME_CONTROL, "cancel", 0).id;
	kp_t_eq(T, kp_script_flow_act(&H->f, &run, &s, why, sizeof why), 0, "activate cancel: %s", why);
	frames(H, 4);
	kp_t_ok(T, kp_script_flow_cond(&H->f, &nodialog, why, sizeof why), "closed: %s", why);

	/* a save and a load: asked of the flow's next frame (as its dialogs ask) */
	s = script_step(KP_SCRIPT_SAVE, 0, 0, 0);
	snprintf(s.text, sizeof s.text, "%s", "a script's save");
	kp_t_eq(T, kp_script_flow_act(&H->f, &run, &s, why, sizeof why), 0, "save: %s", why);
	kp_t_eq_str(T, H->f.pending_save, "a script's save", "the save asked");
	s.op = KP_SCRIPT_LOAD;
	kp_t_eq(T, kp_script_flow_act(&H->f, &run, &s, why, sizeof why), 0, "load: %s", why);
	kp_t_eq_str(T, H->f.pending_load, "a script's save", "the load asked");
	H->f.pending_save[0] = H->f.pending_load[0] = '\0';
	s = script_step(KP_SCRIPT_END_PLACEMENT, 0, 0, 0);
	kp_t_eq(T, kp_script_flow_act(&H->f, &run, &s, why, sizeof why), 0, "end-placement: %s", why);
	s = script_step(KP_SCRIPT_CANCEL_TARGETING, 0, 0, 0);
	kp_t_eq(T, kp_script_flow_act(&H->f, &run, &s, why, sizeof why), 0, "cancel-targeting: %s", why);
}

static const KpTest TESTS[] = {
	{ "missions_number_the_catalog_and_place_the_scenarios", missions_number_the_catalog_and_place_the_scenarios, 0 },
	{ "room_start_plays_the_game_in_the_glues_console", room_start_plays_the_game_in_the_glues_console, 0 },
	{ "room_cancel_leaves_no_game", room_cancel_leaves_no_game, 0 },
	{ "briefing_text_is_vector_text", briefing_text_is_vector_text, 0 },
	{ "scrolled_briefing_text_keeps_its_glyphs", scrolled_briefing_text_keeps_its_glyphs, 0 },
	{ "victory_goes_through_the_score_screen_to_the_next_game", victory_goes_through_the_score_screen_to_the_next_game, 0 },
	{ "victory_without_a_step_opens_the_main_menu", victory_without_a_step_opens_the_main_menu, 0 },
	{ "score_screen_lists_every_player_and_the_games_time", score_screen_lists_every_player_and_the_games_time, 0 },
	{ "score_cinematics_play_before_the_next_game", score_cinematics_play_before_the_next_game, 0 },
	{ "restart_plays_the_mission_again_from_its_room", restart_plays_the_mission_again_from_its_room, 0 },
	{ "quit_mission_opens_the_campaign_menu", quit_mission_opens_the_campaign_menu, 0 },
	{ "load_replay_lists_refuses_and_plays_the_last_game", load_replay_lists_refuses_and_plays_the_last_game, 0 },
	{ "load_replay_plays_a_custom_maps_replay", load_replay_plays_a_custom_maps_replay, 0 },
	{ "load_replay_imports_and_exports", load_replay_imports_and_exports, 0 },
	{ "save_replay_writes_the_replays_folder", save_replay_writes_the_replays_folder, 0 },
	{ "save_replay_lists_in_the_file_systems_order", save_replay_lists_in_the_file_systems_order, 0 },
	{ "map_transfer_finds_and_saves_the_users_maps", map_transfer_finds_and_saves_the_users_maps, 0 },
	{ "network_saves_list_and_resume_by_id", network_saves_list_and_resume_by_id, 0 },
	{ "koprulu_network_saves_list_and_resume", koprulu_network_saves_list_and_resume, 0 },
	{ "a_failed_network_game_leaves_storms_game", a_failed_network_game_leaves_storms_game, 0 },
	{ "network_ready_room_builds_the_game_again", network_ready_room_builds_the_game_again, 0 },
	{ "a_fatal_network_error_returns_to_the_main_menu", a_fatal_network_error_returns_to_the_main_menu, 0 },
	{ "a_lobby_map_preview_goes_through_the_flow", a_lobby_map_preview_goes_through_the_flow, 0 },
	{ "a_failing_socket_stops_the_reads", a_failing_socket_stops_the_reads, 0 },
	{ "storm_answers_without_frames", storm_answers_without_frames, 0 },
	{ "play_custom_screens_are_the_originals", play_custom_screens_are_the_originals, 0 },
	{ "custom_games_end_in_the_next_scenario_or_the_campaign_menu", custom_games_end_in_the_next_scenario_or_the_campaign_menu, 0 },
	{ "custom_saves_find_their_maps_folder", custom_saves_find_their_maps_folder, 0 },
	{ "play_custom_imports_a_map", play_custom_imports_a_map, 0 },
	{ "map_flag_plays_a_custom_map_without_the_glue", map_flag_plays_a_custom_map_without_the_glue, 0 },
	{ "seats_keep_their_consoles", seats_keep_their_consoles, 0 },
	{ "map_class_is_asked_only_by_the_templates_that_read_it", map_class_is_asked_only_by_the_templates_that_read_it, 0 },
	{ "exit_program_ends_the_program_or_opens_the_main_menu", exit_program_ends_the_program_or_opens_the_main_menu, 0 },
	{ "glue_exit_ends_the_program_or_opens_the_main_menu", glue_exit_ends_the_program_or_opens_the_main_menu, 0 },
	{ "languages_match_the_systems_names", languages_match_the_systems_names, 0 },
	{ "the_language_is_chosen_in_the_main_menu", the_language_is_chosen_in_the_main_menu, 0 },
	{ "the_speech_is_chosen_apart_from_the_text", the_speech_is_chosen_apart_from_the_text, 0 },
	{ "a_language_waits_for_its_group", a_language_waits_for_its_group, 0 },
	{ "history_cinematic_goes_on_from_its_row", history_cinematic_goes_on_from_its_row, 0 },
	{ "campaign_end_plays_its_cinematics_then_the_epilog", campaign_end_plays_its_cinematics_then_the_epilog, 0 },
	{ "history_cinematic_at_the_campaigns_end_plays_the_epilog", history_cinematic_at_the_campaigns_end_plays_the_epilog,
		0 },
	{ "first_start_plays_the_logo_and_the_intro_once", first_start_plays_the_logo_and_the_intro_once, 0 },
	{ "options_follow_the_profile_and_the_flag", options_follow_the_profile_and_the_flag, 0 },
	{ "a_box_is_written_at_its_dialogs_ok", a_box_is_written_at_its_dialogs_ok, 0 },
	{ "settings_come_from_the_profile", settings_come_from_the_profile, 0 },
	{ "frame_cap_follows_the_throttle", frame_cap_follows_the_throttle, 0 },
	{ "games_start_at_the_profiles_speed", games_start_at_the_profiles_speed, 0 },
	{ "measured_runs_keep_the_originals", measured_runs_keep_the_originals, 0 },
	{ "settings_are_written_at_their_dialogs_ok", settings_are_written_at_their_dialogs_ok, 0 },
	{ "view_intro_plays_then_opens_the_main_menu", view_intro_plays_then_opens_the_main_menu, 0 },
	{ "glue_mission_outside_the_catalog_leaves_nothing", glue_mission_outside_the_catalog_leaves_nothing, 0 },
	{ "start_that_fails_leaves_no_scene", start_that_fails_leaves_no_scene, 0 },
	{ "save_is_written_and_the_glue_loads_it_before_the_first_frame",
		save_is_written_and_the_glue_loads_it_before_the_first_frame, 0 },
	{ "load_of_another_missions_save_starts_its_game", load_of_another_missions_save_starts_its_game, 0 },
	{ "load_that_fails_keeps_the_game_or_opens_the_campaign_menu",
		load_that_fails_keeps_the_game_or_opens_the_campaign_menu, 0 },
	{ "glue_load_that_fails_opens_the_load_screen_again", glue_load_that_fails_opens_the_load_screen_again, 0 },
	{ "check_save_tells_why_a_save_does_not_load", check_save_tells_why_a_save_does_not_load, 0 },
	{ "delete_keeps_the_name_of_a_file_that_stays", delete_keeps_the_name_of_a_file_that_stays, 0 },
	{ "saves_are_each_ids_and_leave_with_it", saves_are_each_ids_and_leave_with_it, 0 },
	{ "glue_load_takes_the_row_of_the_logged_in_ids_list", glue_load_takes_the_row_of_the_logged_in_ids_list, 0 },
	{ "old_save_files_get_their_ids_names", old_save_files_get_their_ids_names, 0 },
	{ "game_failure_stops_the_game_and_the_flow_goes_on", game_failure_stops_the_game_and_the_flow_goes_on, 0 },
	{ "video_options_come_from_the_profile_under_the_flags", video_options_come_from_the_profile_under_the_flags, 0 },
	{ "software_gpu_only_under_the_given_flag", software_gpu_only_under_the_given_flag, 0 },
	{ "video_dialog_applies_and_keeps_its_options", video_dialog_applies_and_keeps_its_options, 0 },
	{ "camera_keeps_to_the_view_and_its_centre", camera_keeps_to_the_view_and_its_centre, 0 },
	{ "free_before_the_host_asks_it_nothing", free_before_the_host_asks_it_nothing, 0 },
	{ "mission_flag_plays_without_the_glue", mission_flag_plays_without_the_glue, 0 },
	{ "mission_waits_for_its_files", mission_waits_for_its_files, 0 },
	{ "script_names_hold_over_the_flow", script_names_hold_over_the_flow, 0 },
	{ "a_script_walks_the_glues_menus", a_script_walks_the_glues_menus, 0 },
	{ "a_script_observes_a_game", a_script_observes_a_game, 0 },
	{ "script_acts_over_a_game", script_acts_over_a_game, 0 },
	{ "script_reads_the_network_lobby", script_reads_the_network_lobby, 0 },
	{ "network_scripts_parse", network_scripts_parse, 0 },
};

KP_SUITE_SPLIT(kp_suite_flow, "test_flow", TESTS);
