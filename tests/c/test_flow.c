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

#include "kp_flow.h"
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
/* the console's game ends (kp_ui's game_exit: RequestGameExit 0x444e00) and a result (KP_EVENT_GAME_RESULT's a) */
#define EXIT_PROGRAM 2
#define EXIT_QUIT_MISSION 4
#define EXIT_RESTART 5
#define EXIT_VICTORY 6
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
} Host;

/* --- the host --- */

static double host_now(void *ctx) { return ((Host *)ctx)->now; }

static void host_log(void *ctx, const char *line)
{
	Host *H = (Host *)ctx;
	snprintf(H->last_log, sizeof H->last_log, "%s", line);
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

static void host_cursor(void *ctx, KpUi *ui)
{
	(void)ctx;
	(void)ui;
}

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

static const char *current(const Host *H) { return H->f.current >= 0 ? H->data->missions[H->f.current].name : ""; }

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

/* The ready room's Cancel (Escape) leaves no game behind: the campaign menu of the run's campaign. */
static void room_cancel_leaves_no_game(KpT *T)
{
	Host *H = glue_host(T, 0);
	int k;
	start_from_menu(H, EXPANSION_PROTOSS01);
	wait_for(H, PROTOSS_ROOM_BIN, ROOM_FRAMES);
	key(H, KEY_ESCAPE);
	for (k = 0; k < END_FRAMES && H->f.playing; k++) frame(H);
	in_menus(H, KP_GLUE_EXPANSION_CAMPAIGN, "the room's Cancel");
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
	game_event(H, KP_EVENT_GAME_EXIT, EXIT_VICTORY, NULL);
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
		game_event(H, KP_EVENT_GAME_EXIT, EXIT_VICTORY, NULL);
		kp_t_eq(T, H->f.glue.screen, KP_GLUE_SCORE, "the score screen");
		kp_t_eq(T, H->f.has_next, 0, "no next game");
		kp_t_eq(T, H->f.step_glue, KP_GLUE_MAIN, "no step of its own");
		wait_for(H, "rez\\gluscore.bin", MENU_FRAMES);
		click(H, SCORE_CONTINUE);
		for (k = 0; k < END_FRAMES && H->f.glue.screen != KP_GLUE_MENUS; k++) frame(H);
		in_menus(H, KP_GLUE_MAIN, part == 0 ? "without a run" : "a run without its row");
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
	game_event(H, KP_EVENT_GAME_EXIT, EXIT_VICTORY, NULL);
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
	game_event(H, KP_EVENT_GAME_EXIT, EXIT_RESTART, NULL);
	kp_t_eq(T, H->f.hud_on, 0, "the console ended");
	kp_t_ok(T, H->f.after_game, "the glue goes on at the next frame");
	frame(H);
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
	game_event(H, KP_EVENT_GAME_EXIT, EXIT_QUIT_MISSION, NULL);
	frame(H);
	in_menus(H, KP_GLUE_EXPANSION_CAMPAIGN, "Quit Mission in the expansion");
	play(H, PROTOSS01);
	game_event(H, KP_EVENT_GAME_EXIT, EXIT_QUIT_MISSION, NULL);
	frame(H);
	in_menus(H, KP_GLUE_CAMPAIGN, "Quit Mission in the original");
	kp_t_eq(T, H->quits, 0, NULL);
}

/* The in-game menus' Exit Program ends the program at once, or with exit_to_menu (the web's page) opens the main
 * menu. */
static void exit_program_ends_the_program_or_opens_the_main_menu(KpT *T)
{
	Host *H = glue_host(T, 0);
	play(H, EXPANSION_PROTOSS01);
	game_event(H, KP_EVENT_GAME_EXIT, EXIT_PROGRAM, NULL);
	kp_t_eq(T, H->quits, 1, "the program ends");
	kp_t_eq(T, H->f.after_game, 0, "no glue after it");
	if (!(H = glue_host(T, 0))) return;
	H->f.exit_to_menu = 1;
	play(H, EXPANSION_PROTOSS01);
	game_event(H, KP_EVENT_GAME_EXIT, EXIT_PROGRAM, NULL);
	frame(H);
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
static const KpGameSettings KEPT_SETTINGS = { 40, 60, 1, 5, 2, 7, 1 };

static void settings_are_in_the_interface(KpT *T, const KpUi *ui, const KpGameSettings *s, const char *what)
{
	kp_t_eq(T, ui->sound.music, s->music, "%s: the music", what);
	kp_t_eq(T, ui->sound.effects, s->effects, "%s: the effects", what);
	kp_t_eq(T, ui->mouse_scroll_speed, s->mouse_scroll, "%s: the mouse's scroll", what);
	kp_t_eq(T, ui->key_scroll_speed, s->key_scroll, "%s: the keys' scroll", what);
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
	kp_t_eq(T, s->game_speed, want->game_speed, "%s: the game speed", what);
	kp_t_eq(T, s->tip, want->tip, "%s: the tip", what);
	kp_t_eq(T, s->cpu_throttle, want->cpu_throttle, "%s: the throttle", what);
}

/* Open dialog, Cancel, closed with no answer: nothing written, all put back; OK: its own section, once. */
static void settings_are_written_at_their_dialogs_ok(KpT *T)
{
	enum { ID_OK = -2, ID_CANCEL = -3, SOUND_MUSIC = 1, SOUND_EFFECTS = 2, SPEED_GAME = 1 };
	Host *H;
	KpUi *ui;
	KpGameSettings want;
	if (!(H = glue_host(T, 0))) return;
	H->profile.settings = KEPT_SETTINGS;
	want = H->profile.settings;
	kp_ui_options_dialog(H->f.ui, KP_UI_OPTIONS_SPEED); /* no game: its slider says nothing of the game speed */
	H->f.ui->mouse_scroll_speed = want.mouse_scroll;
	H->f.ui->key_scroll_speed = want.key_scroll;
	H->f.ui->cpu_throttle = (uint8_t)want.cpu_throttle;
	answer_dialog(T, H->f.ui, ID_OK);
	frames(H, 2);
	kp_t_eq(T, H->profile.settings.game_speed, want.game_speed, "no game: the game speed as it was");
	play(H, EXPANSION_PROTOSS01); /* the game speed's slider needs a game */
	ui = H->f.ui;
	ui->mouse_scroll_speed = want.mouse_scroll;
	ui->key_scroll_speed = want.key_scroll;
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
	game_event(H, KP_EVENT_GAME_EXIT, EXIT_QUIT_MISSION, NULL);
	frame(H);
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
	game_event(H, KP_EVENT_GAME_EXIT, EXIT_QUIT_MISSION, NULL);
	frame(H);
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
	game_event(H, KP_EVENT_GAME_EXIT, EXIT_QUIT_MISSION, NULL);
	frame(H);
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
	game_event(H, KP_EVENT_GAME_EXIT, EXIT_QUIT_MISSION, NULL);
	frame(H);
	play(H, EXPANSION_PROTOSS01);
	H->fail_world = 1;
	request(H, KP_EVENT_LOAD_GAME, "flowgone");
	in_menus(H, KP_GLUE_EXPANSION_CAMPAIGN, "the other mission's failed start");
}

/* The load screen's save that does not load opens the load screen again (RunLoadGameScreen 0x459970 leaves the state
 * when LoadSavedGameByName 0x498bd0 fails), which keeps the expansion its Back returns to. */
static void glue_load_that_fails_opens_the_load_screen_again(KpT *T)
{
	Host *H = glue_host(T, 0);
	kp_profile_add_save(&H->profile, "flowmissing");
	kp_glue_saves(H->f.ui, &H->profile);
	H->f.ui->menus.save_choice = 0;
	menus_end(H, KP_UI_MENU_LOAD_SAVE, KP_GLUE_EXPANSION_CAMPAIGN, 0, 0, 1);
	kp_t_eq_str(T, H->last_log, "glue: load failed", NULL);
	in_menus(H, KP_GLUE_LOAD_SCREEN, "the failed load");
	kp_t_eq(T, H->f.ui->menus.expansion, 1, "the expansion's");
	wait_for(H, "rez\\gluload.bin", MENU_FRAMES);
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
	game_event(H, KP_EVENT_GAME_EXIT, EXIT_QUIT_MISSION, NULL);
	frame(H);
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
	game_event(H, KP_EVENT_GAME_EXIT, EXIT_QUIT_MISSION, NULL);
	frame(H);
	snprintf(H->f.ui->menus.player, sizeof H->f.ui->menus.player, "Zeratul");
	play(H, PROTOSS01);
	save(H, "flowzeratul");
	game_event(H, KP_EVENT_GAME_EXIT, EXIT_QUIT_MISSION, NULL);
	frame(H);
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
	game_event(H, KP_EVENT_GAME_EXIT, EXIT_QUIT_MISSION, NULL);
	frame(H);
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
		H->flags.graphics = KP_GRAPHICS_ORIGINAL;
		H->flags.fog_style = H->flags.water = H->flags.font_mode = H->flags.lighting = KP_VIDEO_AUTO;
		H->flags.video_given = KP_VIDEO_GIVEN_GRAPHICS;
		H->flags.originals = check;
		H->available = KP_VIDEO_HAS_HD2 | KP_VIDEO_HAS_VECTOR;
		init(H);
		ui = H->f.ui;
		kp_t_eq(T, H->flags.graphics, KP_GRAPHICS_ORIGINAL, "the flag's art");
		kp_t_eq(T, ui->video_options.graphics, KP_GRAPHICS_ORIGINAL, NULL);
		kp_t_eq(T, ui->video_options.available, KP_VIDEO_HAS_HD2 | KP_VIDEO_HAS_VECTOR, "the host's choices");
		if (check) {
			kp_t_eq(T, H->f.gamma, KP_UI_GAMMA_DEFAULT, "a check's gamma");
			kp_t_eq(T, H->f.portraits, KP_UI_PORTRAITS_ANIMATED, NULL);
			kp_t_eq(T, H->flags.fog_style, KP_VIDEO_AUTO, "a check keeps the flags");
			kp_t_eq(T, H->flags.lighting, KP_VIDEO_AUTO, NULL);
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
			kp_t_eq(T, ui->video_options.fog, KP_FOG_SMOOTH, NULL);
		}
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
		H->f.ui->gamma = 70;
		o->serial++;
		frame(H);
		kp_t_eq(T, H->flags.fog_style, KP_FOG_SMOOTH, "applied at once");
		kp_t_eq(T, H->flags.water, KP_WATER_ANIMATED, NULL);
		kp_t_eq(T, H->f.gamma, 70, "the gamma");
		kp_t_eq(T, H->gamma, 70, "on the host's HD art");
		saves = H->profile_saves;
		H->f.ui->options_committed |= KP_UI_SECTION_VIDEO;
		frame(H);
		if (check) {
			kp_t_eq(T, H->profile.video.fog, KP_VIDEO_AUTO, "a check's options are not the player's");
			kp_t_eq(T, H->profile.video.gamma, KP_UI_GAMMA_DEFAULT, NULL);
		} else {
			kp_t_eq(T, H->profile.video.fog, KP_FOG_SMOOTH, "kept at OK");
			kp_t_eq(T, H->profile.video.water, KP_WATER_ANIMATED, NULL);
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
	game_event(H, KP_EVENT_GAME_EXIT, EXIT_PROGRAM, NULL);
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
	{ "score_cinematics_play_before_the_next_game", score_cinematics_play_before_the_next_game, 0 },
	{ "restart_plays_the_mission_again_from_its_room", restart_plays_the_mission_again_from_its_room, 0 },
	{ "quit_mission_opens_the_campaign_menu", quit_mission_opens_the_campaign_menu, 0 },
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
	{ "delete_keeps_the_name_of_a_file_that_stays", delete_keeps_the_name_of_a_file_that_stays, 0 },
	{ "saves_are_each_ids_and_leave_with_it", saves_are_each_ids_and_leave_with_it, 0 },
	{ "glue_load_takes_the_row_of_the_logged_in_ids_list", glue_load_takes_the_row_of_the_logged_in_ids_list, 0 },
	{ "old_save_files_get_their_ids_names", old_save_files_get_their_ids_names, 0 },
	{ "game_failure_stops_the_game_and_the_flow_goes_on", game_failure_stops_the_game_and_the_flow_goes_on, 0 },
	{ "video_options_come_from_the_profile_under_the_flags", video_options_come_from_the_profile_under_the_flags, 0 },
	{ "video_dialog_applies_and_keeps_its_options", video_dialog_applies_and_keeps_its_options, 0 },
	{ "camera_keeps_to_the_view_and_its_centre", camera_keeps_to_the_view_and_its_centre, 0 },
	{ "free_before_the_host_asks_it_nothing", free_before_the_host_asks_it_nothing, 0 },
	{ "mission_flag_plays_without_the_glue", mission_flag_plays_without_the_glue, 0 },
	{ "mission_waits_for_its_files", mission_waits_for_its_files, 0 },
	{ "script_names_hold_over_the_flow", script_names_hold_over_the_flow, 0 },
	{ "a_script_walks_the_glues_menus", a_script_walks_the_glues_menus, 0 },
	{ "a_script_observes_a_game", a_script_observes_a_game, 0 },
	{ "script_acts_over_a_game", script_acts_over_a_game, 0 },
};

KP_SUITE(kp_suite_flow, "test_flow", TESTS);
