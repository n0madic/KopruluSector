/*
 * The product's flow (native/kp_app/kp_flow.c) over a host that records what it is asked and a clock the tests move,
 * on the generated data: the glue's games from their ready rooms to the console, the game's ends (the result, the
 * in-game menus' exits) and the campaign's next game, the cinematics between them, the saves, the video options and
 * the view. The world, the sound and the drawing are the host's (sdl/kp_app_flow.c in the product).
 */
#define _POSIX_C_SOURCE 200809L /* mkdir, mkdtemp, rmdir */
#define _DARWIN_C_SOURCE        /* and macOS's mkdtemp (unistd.h) */
#include "kp_test_app.h"
#include "kp_test_ui.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "kp_flow.h"

#define DATA "generated/data/"
#define CATALOG "generated/missions/index.txt"
#define SCENARIOS "generated/scenarios/index.txt"
#define MISSIONS 256
#define FRAME_S 0.016            /* a frame of the host's clock */
#define SCREEN_W 1280            /* the drawable (its safe area the whole of it) */
#define SCREEN_H 960
#define MENU_FRAMES 2000         /* a glue screen's slide-in */
#define ROOM_FRAMES 4000         /* a ready room's establishing text (25 s), then its dialog */
#define SETTLE_FRAMES 150        /* a dialog's slide and animations */
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
	KpAppMission missions[MISSIONS];
	KpProfile profile;
	uint8_t *ui_files[KP_UI_FILES];
	size_t ui_sizes[KP_UI_FILES];
	KpCampaign campaign;
	int has_campaign;
	KpSaveIdentity identity;
	KpPalettes palettes;
	KpTileset tileset;
	char dir[200];           /* the test's own directory of the saves ("" none yet) */
	char pref[256];
	double now;
	/* what the host was asked */
	int quits, profile_saves, syncs, builds, frees, world_frames, ticks, restarts, stops, palettes_set, interfaces;
	int32_t gamma;
	int world;               /* a world stands */
	int fail_world;          /* the next world's build fails */
	int fail_game;           /* the next world's frame fails the game */
	int pack;                /* the host's cinematics' pack holds every movie */
	int movie;               /* a movie plays */
	int cine_starts, cine_count, cine_preludes, cine_ids[CINE_IDS];
	uint8_t available;
	char last_log[512];
	char saves[8][400];      /* the save files the test wrote (removed at its end) */
	int save_count;
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

static void host_save_profile(void *ctx) { ((Host *)ctx)->profile_saves++; }

static void host_sync_files(void *ctx) { ((Host *)ctx)->syncs++; }

static void host_quit(void *ctx) { ((Host *)ctx)->quits++; }

/* The world of the game started: only its tileset, for the console's map. */
static int host_world_build(void *ctx, char *err, size_t err_size)
{
	Host *H = (Host *)ctx;
	const KpMission *m = kp_session_mission(H->f.session);
	const uint8_t *bytes;
	size_t size;
	KpError e;
	H->builds++;
	if (H->fail_world) {
		H->fail_world = 0;
		snprintf(err, err_size, "the test's world does not build");
		return -1;
	}
	bytes = kp_session_tileset(H->f.session, &size);
	if (kp_tileset_load(&H->tileset, bytes, size, KP_TILESET_NAMES[m->tileset & 7], &e) != 0) {
		snprintf(err, err_size, "%s", e.msg);
		return -1;
	}
	H->world = 1;
	return 0;
}

static void host_world_free(void *ctx)
{
	Host *H = (Host *)ctx;
	H->frees++;
	H->world = 0;
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
	m->palettes = &H->palettes;
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

static void host_sounds(void *ctx, const KpUiSoundCommand *c, size_t n)
{
	(void)ctx;
	(void)c;
	(void)n;
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

/* The flow ended and what it loaded released, before the files it reads (deferred after them: run first). */
static void release_host(void *p)
{
	Host *H = (Host *)p;
	int k;
	kp_flow_free(&H->f);
	for (k = 0; k < H->save_count; k++) remove(H->saves[k]);
	if (H->dir[0]) rmdir(H->dir);
	if (H->has_campaign) kp_campaign_free(&H->campaign);
}

static int ready(KpT *T)
{
	static const char *const NEED[] = { DATA "dat.kpd", DATA "grpmeta.kpd", DATA "iscript.kpd", DATA "aiscript.kpd",
		DATA "palettes.kpd", DATA "campaign.kpd", DATA "ui.kpd", DATA "fonts.kpd", DATA "strings.kpd",
		DATA "portraits.kpd", DATA "sounds.kpd", CATALOG, "generated/missions/" EXPANSION_PROTOSS01 ".kpm",
		"generated/missions/" EXPANSION_PROTOSS02 ".kpm", "generated/missions/" PROTOSS01 ".kpm", NULL };
	return kp_t_ready(T, T->test, NEED);
}

/* A host and its flow over the generated data, not started (its flags and profile the test's to set); NULL when the
 * data is not there (the SKIP line printed). Released when the test ends. */
static Host *new_host(KpT *T)
{
	Host *H;
	KpFlow *f;
	KpFlowHost *h;
	const uint8_t *campaign, *palettes;
	size_t campaign_size, palettes_size, catalog_size, scenarios_size = 0;
	char *catalog, *scenarios;
	KpError e;
	int k;
	if (!ready(T)) return NULL;
	H = (Host *)calloc(1, sizeof *H);
	kp_t_ok(T, H != NULL, "out of memory");
	kp_t_defer(T, free, H);
	H->T = T;
	H->now = 1;
	f = &H->f;
	/* the files first: the flow's release, deferred after them, runs before theirs */
	for (k = 0; k < KP_UI_FILES; k++) {
		char path[128];
		snprintf(path, sizeof path, DATA "%s", KP_UI_FILE_NAMES[k]);
		H->ui_files[k] = kp_t_read(T, path, &H->ui_sizes[k]);
	}
	f->dat = kp_t_read(T, DATA "dat.kpd", &f->dat_size);
	campaign = kp_t_read(T, DATA "campaign.kpd", &campaign_size);
	palettes = kp_t_read(T, DATA "palettes.kpd", &palettes_size);
	catalog = (char *)kp_t_read(T, CATALOG, &catalog_size);
	scenarios = kp_t_has(T, SCENARIOS) ? (char *)kp_t_read(T, SCENARIOS, &scenarios_size) : NULL;
	kp_t_defer(T, release_host, H);
	kp_t_ok(T, kp_campaign_load(&H->campaign, campaign, campaign_size, "campaign.kpd", &e) == 0, "%s", e.msg);
	H->has_campaign = 1;
	kp_t_ok(T, kp_palettes_load(&H->palettes, palettes, palettes_size, &e) == 0, "%s", e.msg);
	kp_t_ok(T, kp_save_identity(&H->identity, f->dat, f->dat_size, &e) == 0, "%s", e.msg);
	f->mission_count = kp_flow_missions(H->missions, MISSIONS, catalog, catalog_size, scenarios, scenarios_size);
	kp_profile_init(&H->profile);
	/* the saves in a directory of the test's own: two runs of the suite at once share no file */
	snprintf(H->dir, sizeof H->dir, "build/flow_%s_XXXXXX", T->test);
	if (!mkdtemp(H->dir)) {
		H->dir[0] = '\0';
		kp_t_fail(T, "a directory for the saves: %s", strerror(errno));
	}
	snprintf(H->pref, sizeof H->pref, "%s/", H->dir);
	h = &f->host;
	h->ctx = H;
	h->now = host_now;
	h->clock_ms = NULL;
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
	f->flags = &H->flags;
	f->missions = H->missions;
	f->shared = kp_t_shared(T);
	f->campaign = &H->campaign;
	f->identity = &H->identity;
	f->ui_files = H->ui_files;
	f->ui_sizes = H->ui_sizes;
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
	if (!H) return NULL;
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
	for (k = 0; k < H->f.mission_count; k++) {
		if (strcmp(H->missions[k].name, name) == 0) return (int)H->missions[k].index;
	}
	kp_t_fail(H->T, "no mission %s in the catalog", name);
}

static const char *current(const Host *H) { return H->f.current >= 0 ? H->missions[H->f.current].name : ""; }

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

/* The catalog's missions take their line as their campaign row; a scenario the row of its base map. */
static void missions_number_the_catalog_and_place_the_scenarios(KpT *T)
{
	static const char CAT[] = "terran/tutorial 1 64 64\nterran/terran01 0 64 64\n\nbad line\nzerg/zerg01 4 64 64";
	static const char SCN[] = "marine_group terran/terran01 0 64 64\nnowhere zerg/zerg99 0 64 64\n";
	KpAppMission m[8];
	int n = kp_flow_missions(m, 8, CAT, sizeof CAT - 1, SCN, sizeof SCN - 1);
	kp_t_eq(T, n, 5, "three missions, two scenarios");
	kp_t_eq_str(T, m[1].name, "terran/terran01", NULL);
	kp_t_eq_str(T, m[1].kpm, "/generated/missions/terran/terran01.kpm", NULL);
	kp_t_eq(T, m[1].index, 2, "the second line");
	kp_t_eq(T, m[2].index, 3, "the lines that are no mission count none");
	kp_t_ok(T, m[1].catalog && !m[3].catalog, "the catalog's and a scenario");
	kp_t_eq_str(T, m[3].name, "scenarios/marine_group", NULL);
	kp_t_eq_str(T, m[3].kpm, "/generated/scenarios/marine_group.kpm", NULL);
	kp_t_eq(T, m[3].index, 2, "its base's row");
	kp_t_eq(T, m[4].index, 0, "a base outside the catalog: no row");
	kp_t_eq(T, kp_flow_missions(m, 2, CAT, sizeof CAT - 1, SCN, sizeof SCN - 1), 2, "no more than the table holds");
}

/* A campaign mission of the glue waits in its ready room (its game paused, the glue's music stopped); Start brings
 * the console of the same interface and runs the game on the host's clock. */
static void room_start_plays_the_game_in_the_glues_console(KpT *T)
{
	Host *H = glue_host(T, 0);
	KpUi *ui;
	int stops, ticks;
	if (!H) return;
	ui = H->f.ui;
	stops = H->stops;
	start_from_menu(H, EXPANSION_PROTOSS01);
	kp_t_ok(T, H->stops > stops, "the glue's music stopped for the game's");
	kp_t_ok(T, H->f.has_run && H->f.run.mission == mission_row(H, EXPANSION_PROTOSS01), "a run of its campaign");
	kp_t_eq(T, H->f.glue_room, PROTOSS_ROOM, "the room's state kept for the game's restart");
	frames(H, 50);
	kp_t_eq(T, H->ticks, 0, "no steps in the room");
	room_start(H);
	kp_t_ok(T, H->f.ui == ui, "the glue's interface runs the console");
	kp_t_eq(T, kp_flow_glue_active(&H->f), 0, "no glue screen over the game");
	kp_t_eq(T, H->f.fixed_screen, 0, "the game's screen");
	ticks = H->ticks;
	frames(H, 100);
	kp_t_ok(T, H->ticks > ticks, "the game steps on the clock");
	kp_t_eq(T, H->quits, 0, NULL);
}

/* The ready room's Cancel (Escape) leaves no game behind: the campaign menu of the run's campaign. */
static void room_cancel_leaves_no_game(KpT *T)
{
	Host *H = glue_host(T, 0);
	int k;
	if (!H) return;
	start_from_menu(H, EXPANSION_PROTOSS01);
	wait_for(H, PROTOSS_ROOM_BIN, ROOM_FRAMES);
	key(H, KEY_ESCAPE);
	for (k = 0; k < END_FRAMES && H->f.playing; k++) frame(H);
	in_menus(H, KP_GLUE_EXPANSION_CAMPAIGN, "the room's Cancel");
	kp_t_eq(T, H->frees >= 1, 1, "the world freed");
	kp_t_ok(T, H->f.ui->game == NULL, "the interface keeps no game");
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
	if (!H) return;
	play(H, EXPANSION_PROTOSS01);
	/* what the campaign gives for the game as it stands */
	run = H->f.run;
	progress = *kp_profile_progress(&H->profile);
	kp_campaign_after_game(&H->campaign, &run, &progress, RESULT_VICTORY,
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
	kp_t_eq(T, H->missions[H->f.current].index, step.mission, "the next mission");
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
		if (!H) return;
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

/* The first catalog mission (its file there) whose victory, with the game's next scenario cleared, passes cinematic
 * rows (*count of them) to a step of kind: its name and the run the step leaves (*after), NULL none. */
static const char *mission_with_step(Host *H, int kind, int *count, KpCampaignRun *after)
{
	int k;
	for (k = 0; k < H->f.mission_count; k++) {
		KpCampaignProfile progress = *kp_profile_progress(&H->profile);
		KpCampaignStep step;
		int64_t cinematics[KP_APP_STEP_CINEMATICS];
		const KpAppMission *m = &H->missions[k];
		char path[256];
		snprintf(path, sizeof path, "%s", m->kpm + 1);
		if (!m->catalog || !kp_campaign_run_of_mission(&H->campaign, m->index, after) || !kp_t_has(H->T, path)) {
			continue;
		}
		kp_campaign_after_game(&H->campaign, after, &progress, RESULT_VICTORY, "", &step, cinematics, count,
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
	if (!H) return;
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
	if (!H) return;
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
	if (!H) return;
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
	if (!H) return;
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
	if (!H) return;
	menus_end(H, KP_UI_MENU_EXIT, KP_GLUE_MAIN, 0, 0, 0);
	kp_t_eq(T, H->quits, 1, "the program ends");
	in_menus(H, KP_GLUE_MAIN, "Exit");
	H->f.exit_to_menu = 1;
	menus_end(H, KP_UI_MENU_EXIT, KP_GLUE_MAIN, 0, 0, 0);
	kp_t_eq(T, H->quits, 1, "the page goes on");
	in_menus(H, KP_GLUE_MAIN, "Exit on the web");
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
		if (!H) return;
		progress = *kp_profile_progress(&H->profile);
		kp_t_ok(T, kp_campaign_after_cinematic(&H->campaign, 0, HISTORY_ROW, HISTORY_CINEMATIC, &run, &progress, &step,
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
		kp_t_eq(T, H->missions[H->f.current].index, step.mission, "the row's mission");
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
	if (!H) return;
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
		if (!H) return;
		for (t = 0; t < H->campaign.table_count && !row; t++) {
			const KpCampaignTable *table = &H->campaign.tables[t];
			for (k = 0; k < table->count && !row; k++) {
				const KpCampaignRow *r = &table->rows[k];
				KpCampaignProfile progress = *kp_profile_progress(&H->profile);
				KpCampaignRun run;
				KpCampaignStep step;
				int64_t more[KP_APP_STEP_CINEMATICS];
				if (r->cinematic != 0 && kp_campaign_after_cinematic(&H->campaign, table->expansion, r->mission,
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

/* The first start (RunMainLoop 0x4679b0, the options' bit 0x800): the logo and the expansion's intro before the main
 * menu, once (the profile written at once); a start without the pack keeps them for one that has it, and the checks
 * and --cine-play's start play none. */
static void first_start_plays_the_logo_and_the_intro_once(KpT *T)
{
	Host *H = new_host(T);
	uint32_t played;
	int part;
	char name[128];
	if (!H) return;
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
	wait_for(H, "rez\\glumain.bin", MENU_FRAMES);
	in_menus(H, KP_GLUE_MAIN, "after the intro");
	played = H->profile.options;
	for (part = 0; part < 4; part++) {
		static const char *const WHAT[4] = { "the next start", "no pack", "--hd-check", "--cine-play" };
		if (!(H = new_host(T))) return;
		H->pack = part != 1;
		if (part == 0) H->profile.options = played;
		if (part == 2) snprintf(H->flags.hd_check, sizeof H->flags.hd_check, "%s", H->dir);
		if (part == 3) snprintf(H->flags.cine_play, sizeof H->flags.cine_play, "3");
		init(H);
		kp_t_eq(T, H->cine_starts, 0, "%s: no cinematics", WHAT[part]);
		kp_t_eq(T, H->profile_saves, 0, "%s: the profile as it was", WHAT[part]);
		if (part > 0) {
			kp_t_ok(T, (H->profile.options & KP_UI_OPTION_EXPANSION_INTRO) != 0, "%s: the intro kept", WHAT[part]);
		}
		wait_for(H, "rez\\glumain.bin", MENU_FRAMES);
		in_menus(H, KP_GLUE_MAIN, WHAT[part]);
	}
}

/* View Intro plays the logo and the expansion's intro, then opens the main menu; --cine-play's do the same. */
static void view_intro_plays_then_opens_the_main_menu(KpT *T)
{
	static const int IDS[] = { 3, 4 };
	Host *H = glue_host(T, 1);
	if (!H) return;
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
	if (!H) return;
	H->f.has_continue = 1; /* as score_next leaves it */
	menus_end(H, KP_UI_MENU_MISSION, PROTOSS_ROOM, MISSIONS + 1, 0, 1);
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
	if (!H) return;
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
	if (!H) return;
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
}

/* A load in the game of another mission's save starts that mission's game, which loads it too. */
static void load_of_another_missions_save_starts_its_game(KpT *T)
{
	Host *H = glue_host(T, 0);
	int builds;
	if (!H) return;
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
	if (!H) return;
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
	if (!H) return;
	kp_profile_add_save(&H->profile, "flowmissing");
	kp_glue_saves(H->f.ui, &H->profile);
	H->f.ui->menus.save_choice = 0;
	menus_end(H, KP_UI_MENU_LOAD_SAVE, KP_GLUE_EXPANSION_CAMPAIGN, 0, 0, 1);
	kp_t_eq_str(T, H->last_log, "glue: load failed", NULL);
	in_menus(H, KP_GLUE_LOAD_SCREEN, "the failed load");
	kp_t_eq(T, H->f.ui->menus.expansion, 1, "the expansion's");
	wait_for(H, "rez\\gluload.bin", MENU_FRAMES);
}

/* A save's path held by a directory with a file in it (a delete cannot remove it as a file). */
typedef struct {
	char path[400], inner[460];
} HeldSave;

static void release_held(void *p)
{
	const HeldSave *s = (const HeldSave *)p;
	remove(s->inner);
	rmdir(s->path);
}

/* The interface's delete: a save's file and its name in the profile (a missing file counts as deleted); a file that
 * stays keeps its name. */
static void delete_keeps_the_name_of_a_file_that_stays(KpT *T)
{
	Host *H = new_host(T);
	HeldSave *held;
	FILE *f;
	if (!H) return;
	held = (HeldSave *)calloc(1, sizeof *held);
	kp_t_ok(T, held != NULL, "out of memory");
	kp_t_defer(T, free, held);
	init(H);
	kp_profile_add_save(&H->profile, "flowstays");
	kp_profile_add_save(&H->profile, "flowmissing");
	kp_t_eq(T, H->f.ui->delete_save(H->f.ui->host, "flowmissing"), 1, "a missing file is deleted");
	kp_t_eq(T, H->profile.save_count, 1, NULL);
	kp_t_eq_str(T, H->profile.saves[0].name, "flowstays", NULL);
	kp_t_eq(T, H->profile_saves, 1, "the profile kept");
	/* a directory with a file in it cannot be removed as a file (removed when the test ends, passed or failed, and
	 * before it: a run that crashed left it) */
	save_path(H, "flowstays", held->path, sizeof held->path);
	snprintf(held->inner, sizeof held->inner, "%s/held", held->path);
	release_held(held);
	kp_t_ok(T, mkdir(held->path, 0700) == 0, "%s", held->path);
	kp_t_defer(T, release_held, held);
	kp_t_ok(T, (f = fopen(held->inner, "wb")) != NULL, "%s", held->inner);
	fclose(f);
	kp_t_eq(T, H->f.ui->delete_save(H->f.ui->host, "flowstays"), 0, "the file stays");
	kp_t_eq(T, H->profile.save_count, 1, "its name stays");
	kp_t_ok(T, strstr(H->last_log, "cannot delete") != NULL, "%s", H->last_log);
}

/* The flow over a profile of two IDs, Fenix logged in, at the glue's main menu. */
static Host *two_ids_host(KpT *T)
{
	Host *H = new_host(T);
	if (!H) return NULL;
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
	if (!H) return;
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
	if (!H) return;
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
	if (!H) return;
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
	if (!H) return;
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
		if (!H) return;
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
		if (check) snprintf(H->flags.video_check, sizeof H->flags.video_check, "build/flow_video_check");
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
		if (!H) return;
		if (check) snprintf(H->flags.video_check, sizeof H->flags.video_check, "build/flow_video_check");
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
		o->commits++;
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
	if (!H) return;
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
	if (!H) return;
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
	if (!(H = new_host(T))) return;
	H->flags.play_check = KP_APP_PLAY_CHECKS + 1;
	kp_t_ok(T, kp_flow_init(&H->f, err, sizeof err) != 0, "no such check");
	kp_t_ok(T, strstr(err, "no such check") != NULL, "%s", err);
}

static const KpTest TESTS[] = {
	{ "missions_number_the_catalog_and_place_the_scenarios", missions_number_the_catalog_and_place_the_scenarios, 0 },
	{ "room_start_plays_the_game_in_the_glues_console", room_start_plays_the_game_in_the_glues_console, 0 },
	{ "room_cancel_leaves_no_game", room_cancel_leaves_no_game, 0 },
	{ "victory_goes_through_the_score_screen_to_the_next_game", victory_goes_through_the_score_screen_to_the_next_game, 0 },
	{ "victory_without_a_step_opens_the_main_menu", victory_without_a_step_opens_the_main_menu, 0 },
	{ "score_cinematics_play_before_the_next_game", score_cinematics_play_before_the_next_game, 0 },
	{ "restart_plays_the_mission_again_from_its_room", restart_plays_the_mission_again_from_its_room, 0 },
	{ "quit_mission_opens_the_campaign_menu", quit_mission_opens_the_campaign_menu, 0 },
	{ "exit_program_ends_the_program_or_opens_the_main_menu", exit_program_ends_the_program_or_opens_the_main_menu, 0 },
	{ "glue_exit_ends_the_program_or_opens_the_main_menu", glue_exit_ends_the_program_or_opens_the_main_menu, 0 },
	{ "history_cinematic_goes_on_from_its_row", history_cinematic_goes_on_from_its_row, 0 },
	{ "campaign_end_plays_its_cinematics_then_the_epilog", campaign_end_plays_its_cinematics_then_the_epilog, 0 },
	{ "history_cinematic_at_the_campaigns_end_plays_the_epilog", history_cinematic_at_the_campaigns_end_plays_the_epilog,
		0 },
	{ "first_start_plays_the_logo_and_the_intro_once", first_start_plays_the_logo_and_the_intro_once, 0 },
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
};

KP_SUITE(kp_suite_flow, "test_flow", TESTS);
