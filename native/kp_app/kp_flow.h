/*
 * The product's flow without SDL (plan phase S, stage S5): the glue's screens (the menus, the ready rooms, the score
 * screens) run the campaign's games, a game's console is the same interface's, the game's end leads to the score
 * screen and the campaign's next game; the saves, the video options, the screen's layout and the view. The platform's
 * part (the clock, the files, the world's presentation and drawing, the sound, the cinematics, the cursor) is the
 * host's (KpFlowHost): sdl/kp_app_flow.c over SDL, a recording host in the tests (tests/c/test_flow.c).
 */
#ifndef KP_FLOW_H
#define KP_FLOW_H

#include "kp_data.h"
#include "kp_hud.h"
#include "kp_locale.h"
#include "kp_map_preview.h"
#include "kp_netgame.h"
#include "kp_netui.h"
#include "kp_seats.h"
#include "kp_session.h"
#include "kp_transfer.h"
#include "kp_watch.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KP_APP_SEED 0x12345678u
#define KP_APP_STEP_CINEMATICS 16 /* the cinematic rows a campaign step passes before its mission or its end */
#define KP_FLOW_CUSTOM (-2) /* KpFlow.current: the custom map's game */
#define KP_FLOW_MAP_PATH 1100 /* a map file's path on the host (the listed folder's bases and a name) */
#define KP_THROTTLE_FPS 60 /* the frames a second of the speed options' Enable CPU throttling */
/* The original's cinematic numbers (0x508da8) the main menu's View Intro plays: Blizzard's logo, then the Brood War
 * intro (mode 0: 24 when the expansion's data is there, which the product's always is) */
enum { KP_APP_CINE_LOGO = 0, KP_APP_CINE_EXPANSION_INTRO = 24 };

#define KP_APP_RES_DIRS 4 /* the folders of original data --data names */


/* The development switches (command-line flags). */
typedef struct {
	char data[512];        /* --data DIR: the converted data's root (generated/ under it) */
	/* --data DIR, any number: the folders of the original data (a repack, an install; kp_res, ADR 0018) */
	char res_dirs[KP_APP_RES_DIRS][512];
	int res_dir_count;
	char res_log[512];     /* --res-log PATH: the list of the original files read, written when the program ends */
	char mission[64];      /* --mission NAME: a game of it at once (no glue) */
	char map[KP_FLOW_MAP_PATH]; /* --map PATH: a custom map's game at once (no glue; ADR 0024) */
	char map_template[32]; /* --map-template NAME: its game type, a templates.lst name ("": Melee) */
	int seats;             /* --seats N: --map's game as a network game of N seats in one process (kp_seats.h), 0 none */
	/* --lan-port PORT: the LAN provider's socket A (B the next port; 0, or outside 0..65535 with a log line: 6111);
	 * --lan-map BASE:HOSTS:SELF: Koprulu's games on one machine (kp_net_sock.h's map: host SELF of 10.0.0.1..HOSTS on
	 * 127.0.0.1's ports from BASE, -1 a program the map does not hold, on --lan-port) */
	int lan_port;
	char lan_map[32];
	/* --lan-host ADDRESS: a host the LAN provider also asks for its game, "name[:port]" (kp_net.h; the profile's, which
	 * the join screen's Address sets, when not given) */
	char lan_host[KP_NET_HOST];
	/* the viewers of a network game (kp_watch.h): --watch ADDRESS ("name[:port]", the port KP_WATCH_PORT when not
	 * given) plays the game that host serves, at once without the glue as --map; --watch-delay SECONDS how far the
	 * games this program hosts are served behind (< 0: KP_WATCH_DELAY_SECONDS, 0 none); --watch-port PORT their
	 * server's port (0: KP_WATCH_PORT) */
	char watch[KP_NET_HOST];
	double watch_delay;
	int watch_port;
	/* Create Game reads StarCraft 1.17's saved network games (.mlt, .mlx; kp_netsave.h) besides Koprulu's own, and a
	 * resumed lobby finds them: the network oracle's runs only (no option sets it; the product resumes Koprulu's) */
	int original_saves;
	int play;              /* the mission in play mode (with the console) */
	int scale_mode;        /* --scale-mode auto|integer|sharp */
	int font_mode;         /* --font original|vector|auto (KP_VTEXT_*, KP_VIDEO_AUTO): the vector text of large scales */
	int graphics;          /* --graphics original|hd2|hd|auto (KP_GRAPHICS_*, KP_VIDEO_AUTO): the world's art */
	int hd_budget;         /* --hd-budget MB: the HD pages' slots' memory (0: KP_HD_BUDGET; streaming checks) */
	int hd_decode;         /* --hd-decode: the HD pages decoded as without S3TC (a phone's path on a computer) */
	int lighting;          /* --lighting on|off|auto (KP_LIGHTING_*, KP_VIDEO_AUTO: on): the HD sprites' lighting layers */
	int renderer;          /* --gfx gl|gpu|auto (KP_RENDERER_*, KP_VIDEO_AUTO: the platform's, ADR 0028): the drawing's API */
	int gfx_debug;         /* --gfx-debug: SDL_GPU's checks of every call */
	char locale[8];        /* --locale ruRU: a language's overlays over the enUS base (ADR 0016); "": the player's (the
	                        * glue's start: kp_flow_init chooses and sets it) or none (the checks' and --mission's) */
	char speech[8];        /* --speech ruRU: a language's speech over the base's (ADR 0016, stage L5: its missions' TIME,
	                        * spoken lines and unit voices); "": the player's (the glue's start chooses) or none */
	char system_locales[64]; /* not a flag: the system's preferred languages, "ru_RU,en_US" (the host's) */
	char vector_font[512]; /* --vector-font PATH: a TrueType file instead of ui.kpd's VFNT (font comparisons) */
	double vector_size;    /* --vector-size F: the vector glyphs' cap height over the FNT font's (1) */
	int fog;               /* --fog off */
	int fog_style;         /* --fog original|smooth (KP_FOG_*; KP_VIDEO_AUTO: the art's, stage R5) */
	int water;             /* --water on|off (KP_WATER_*; KP_VIDEO_AUTO: the art's, stage R5) */
	int video_given;       /* KP_VIDEO_GIVEN_*: the video options the command line set for the run (kp_session.h) */
	int world_only;        /* --world-only: no canvas (index captures) */
	int console_edges;     /* --console edges|ornaments: the console's pieces without their ornaments (a phone's default) */
	int autotest, autotest_count;
	char screenshot[512];  /* --screenshot PATH */
	double screenshot_after;
	int camera;            /* --camera X,Y: the view's top-left map pixel, held (the screenshots) */
	int32_t camera_x, camera_y;
	int bench_frames;      /* --bench-frames N: frame times of a game, then exit */
	int frame_cap;         /* --frame-cap HZ: at most this many frames a second, over the speed options' CPU throttle */
	char cinematics[8];    /* --cinematics auto|sd|hd2|off: the cinematics' pack (ADR 0015; empty: auto) */
	char subtitles[8];     /* --subtitles on|off: the subtitles option for the run (empty: the profile's) */
	char cine_check[512];  /* --cine-check DIR: every cinematic of the pack decoded to its end, first and last pictures saved */
	char cine_play[64];    /* --cine-play LIST: cinematic numbers (comma separated) played at once, then the menu */
	int vsync;
	int win_w, win_h;
	int headless;          /* --headless: a hidden window, no vsync, no sound device, no message box (the desktops) */
	char script[512];      /* --script FILE: a scripted run (kp_script.h, ADR 0017), its files in --script-out DIR */
	char script_out[512];
	char script_live[512]; /* --script-live FIFO: a script's commands a line at a time from the FIFO (the desktops'
	                        * POSIX ones), after --script's file if one is given */
	int originals;         /* a script's "options originals": the development checks' mode (kp_flow_dev_checks) */
	int index_video;       /* a script's "video originals": the index path's video options too (the autotest's; the play scripts') */
} KpAppFlags;

/* What the flow asks of the platform; every call but clock_ms gets ctx. */
typedef struct {
	void *ctx;
	/* the wall clock in seconds; the session's step budget in milliseconds (kp_session_advance; NULL: no budget) */
	double (*now)(void *ctx);
	double (*clock_ms)(void);
	void (*log)(void *ctx, const char *line);
	/* a file under the data's root (malloc'd: the languages' packs, the test scenarios), NULL when it cannot be read */
	uint8_t *(*read)(void *ctx, const char *path, size_t *size);
	void (*save_profile)(void *ctx);
	void (*sync_files)(void *ctx); /* the saves' files written (the web's IndexedDB) */
	void (*quit)(void *ctx);       /* the program ends after this frame */
	/* the playing game's world (its presentation and drawing): built once its game started, freed with it; a frame's
	 * after the steps (restart: a save was loaded, its state shown at once); the console's map of its tileset */
	int (*world_build)(void *ctx, char *err, size_t err_size);
	void (*world_free)(void *ctx);
	void (*world_frame)(void *ctx, int ticks, double fraction, int restart);
	void (*hud_map)(void *ctx, KpHudMap *m);
	/* the interface: a new one (its cursor), a canvas before it is composed and once it is (NULL: none this frame), the
	 * interface's cursor over the frame (ui NULL: none), the pointer a console frame follows (KP_HUD_POINTER_*, its
	 * logical point) */
	void (*interface)(void *ctx, KpUi *ui);
	void (*canvas_begin)(void *ctx, KpCanvas *c);
	void (*canvas)(void *ctx, KpCanvas *c);
	void (*cursor)(void *ctx, KpUi *ui);
	int (*pointer)(void *ctx, int32_t *x, int32_t *y);
	/* the sound: the interface's commands; the sounds stopped (not the music: its track and stop are the commands') */
	void (*sounds)(void *ctx, const KpUiSoundCommand *c, size_t n);
	void (*stop_sounds)(void *ctx);
	/* the screen's palette (RGB, 256 entries) and the gamma of the HD interface's art */
	void (*palette)(void *ctx, const uint8_t *rgb);
	void (*gamma)(void *ctx, int32_t gamma);
	/* the video options the player has (KP_VIDEO_HAS_*) with the interface ui; SDL_GPU's API ("Metal", "Vulkan") for
	 * the renderer's button, NULL where it is not built */
	uint8_t (*video_available)(void *ctx, const KpUi *ui);
	const char *gpu_name;
	/* the cinematics (kp_app.h's kp_app_cine_has, kp_app_cine_start, kp_app_cine_active) */
	int (*cine_has)(void *ctx, int id);
	int (*cine_start)(void *ctx, const int *ids, int n, int preludes);
	int (*cine_active)(void *ctx);
	/* whether a file of the converted data is there, unread (a language's packs) */
	int (*exists)(void *ctx, const char *path);
	/* the interface and its language are replaced (the main menu's languages): the host drops what it kept of them */
	void (*language)(void *ctx);
	/* whether a mission's files are there (the web fetches its repack's group first: kp_res_group_wanted): 1 they are,
	 * 0 on their way (the flow asks again each frame), -1 they cannot be had (the reason in err); NULL: always there */
	int (*mission_files)(void *ctx, const KpDataMission *m, char *err, size_t err_size);
	/* whether a language's files (its texts and speech) are there (the web fetches its repack's group first): 1 they
	 * are, or nothing names them; 0 not yet, a group of the data names them (with fetch: on their way, the flow asks
	 * again each frame); -1 they cannot be had (the reason in err); NULL: always there */
	int (*language_files)(void *ctx, const char *locale, int fetch, char *err, size_t err_size);
	/* Import (Load Replay's and Play Custom's) and Load Replay's Export (NULL: no file picker on the platform, the
	 * buttons stay hidden): a picker for a file of the kind (KP_FLOW_PICK_*: a .rep, an .scm or .scx) to read, answered
	 * later by kp_flow_import_replay or kp_flow_import_map (or not at all when it is cancelled); a picker for where a
	 * replay's bytes go, under its name (the host copies the bytes) */
	void (*pick_file)(void *ctx, int kind);
	void (*export_replay)(void *ctx, const char *name, const uint8_t *bytes, size_t size);
	/* the LAN provider's sockets opened (1) and closed (0): Android's Wi-Fi multicast lock (NULL: nothing to do) */
	void (*lan_lock)(void *ctx, int on);
} KpFlowHost;

/* The kinds of file Import picks: Load Replay's replays, Play Custom's maps. */
enum { KP_FLOW_PICK_REPLAY = 1, KP_FLOW_PICK_MAP = 2 };

struct KpNetPlay;

typedef struct KpFlow {
	KpFlowHost host;
	/* what the product loaded for every game (not owned; set before kp_flow_init): the original data (ADR 0018: the
	 * missions' catalog, the data set's DAT each game copies, the shared game data, the campaign, the interface's
	 * data) and the saves' identity */
	KpAppFlags *flags;
	const KpData *data;
	const KpSaveIdentity *identity;
	/* the language (ADR 0016; ADR 0019, stage C3: kp_flow_init builds it from the data's own files): its texts, the
	 * glyphs and the speech's sound records, all of it as the interface takes it; the profile's languages (LANG) the data
	 * holds; another choice of the main menu's to start over in */
	KpUiLocale locale;
	KpLocale text;
	KpLocaleGlyphs glyphs;
	uint8_t *speech_sounds;
	KpUiLanguages languages;
	int language_chosen;  /* the flow starts over in it once its files are there */
	int language_waiting; /* for them (said once) */
	int language_restart; /* kp_flow_init after start_over: the main menu at once, no title */
	const uint8_t *console_art; /* console.kpd (kp_ui_set_console_art), or NULL */
	size_t console_art_size;
	KpProfile *profile;    /* loaded; the host's save_profile writes it */
	const char *pref;      /* the preferences' directory with its separator: the saves' files are in it */
	int exit_to_menu;      /* the glue's Exit and a glue game's Exit Program open the main menu (the web's page goes on) */
	/* the screen: the drawable and its safe area (the host's, in pixels from the left and bottom), the picture's
	 * rectangle (kp_flow_picture), the logical screen (kp_layout_screen), the view's top-left map pixel */
	int32_t safe_x, safe_y, safe_w, safe_h;
	int32_t full_w, full_h;
	int32_t win_x, win_y, win_w, win_h;
	int mode;
	double scale;
	int32_t world_w, world_h;
	int fixed_screen;      /* a glue screen keeps the original's 640 x 480 */
	int64_t cam_x, cam_y;
	int32_t game_w, game_h; /* the game's last logical screen (0: none): a new size keeps its view's centre */
	/* the interface: one for the glue and its games */
	KpUi *ui;
	KpGlue glue;
	int glue_on;
	struct KpNetPlay *netplay; /* the multiplayer screens' network (kp_netplay.h; ui->net), NULL none: the web, no glue */
	KpMapPreview preview;      /* the chat room's preview world (KpNetUi.preview) */
	uint32_t options_seen; /* the interface's KP_UI_OPTIONS_KEPT as last kept (the profile's, a flag's over them) */
	KpHud hud;
	int hud_on;
	/* the video options: the profile's with the flags' over them (the art, the fog, the water, the text and the lighting
	 * in flags), the dialog's changes applied each frame */
	int32_t gamma, portraits;
	KpVideoSettings video_flags;          /* the art, fog, water, text, lighting the flags gave (kp_profile_commit_video) */
	uint8_t video_available;              /* KP_VIDEO_HAS_* */
	uint32_t video_serial; /* the interface's counter last applied */
	uint32_t video_commits; /* the video options' OKs kept */
	/* the game's screen palette (the world's tileset's, with its cycles on real time) and a glue screen's */
	KpPalette screen_palette;
	int has_screen_palette;
	double palette_clock;
	uint8_t glue_rgb[768];
	/* the game */
	KpSession *session;
	KpSeats seats;         /* --seats: every seat's session over the turn hub, the shown one's the session's */
	/* --seats: each hidden seat's interface, console and view, kept while another seat is shown (the shown seat's are
	 * ui, hud and cam_x/cam_y) */
	struct {
		KpUi *ui;
		KpHud hud;
		int hud_on;
		int64_t cam_x, cam_y;
	} seat_views[KP_HUB_SEATS];
	int world;             /* the host's world stands */
	int64_t loads;         /* the session's loads the world has shown */
	int current;           /* the mission shown, -1 none, KP_FLOW_CUSTOM the custom map's (custom_entry) */
	int waiting;           /* the mission current waits for its files (host.mission_files) before it starts */
	int playing;
	int result;            /* the game's result for the interface's result dialog, 0 none */
	int glue_room;         /* the ready room's glue state of the game started from the glue, 0 none */
	/* the network's game (stage N4): the game in play is the chat room's (its turns netplay's KpNetGame), its first
	 * turn ran (RunGame 0x445310's step 8: the frames run after it), Storm's game left (EndGame 0x443db0) */
	int net_game, net_running, net_left;
	/* the viewers (kp_watch.h): the socket (owned, NULL none), the server of the network game this seat hosts (kept
	 * after the game: its viewers fetch its end), and this program's viewer: the host's address being looked up, the
	 * address, the viewer, its playback asked for (build_scene takes it), begun, and whether the game in play is it;
	 * watch_sock_failed: the socket's failure said (once per socket) */
	struct KpNetSock *watch_sock;
	int watch_sock_failed;
	KpWatchServer watch_server;
	int watch_serving;
	struct KpNetLookup *watch_lookup;
	KpNetAddr watch_addr;
	KpWatchViewer watch_viewer;
	int watching, watch_known, watch_start, watch_begun, watch_game;
	int fatal_held;              /* ShowFatalErrorDialog 0x43ec00's modal run: the session's clock paused under it */
	char pending_save[KP_PROFILE_SAVE_NAME], pending_load[KP_PROFILE_SAVE_NAME], load_on_start[KP_PROFILE_SAVE_NAME];
	uint32_t pending_save_id; /* the save command's DWORD (a network save's ID, FindMultiplayerSaveById 0x4995e0) */
	/* the network save this seat's lobby found (its path; kp_flow_find_save), "" none; a resumed game's start loads it
	 * before the first turn (net_resume) */
	char net_load[KP_FLOW_MAP_PATH];
	int net_resume;
	double game_started_at;
	/* the campaign's run */
	int has_run;
	KpCampaignRun run;
	int has_next;
	KpCampaignRun next_run;
	int64_t next_mission, next_cheats;
	int next_glue;
	int step_glue;         /* the menu the score screen leads to when no game follows (KP_GLUE_*) */
	int step_epilog;       /* the campaign ended in the epilog (KP_CAMPAIGN_STEP_EPILOG): it comes before that menu */
	int step_expansion;    /* the ended run's expansion byte (0x536720): the epilog's text and credits */
	KpCarry next_carry;
	int has_continue;
	KpCampaignRun continue_run;
	int64_t continue_cheats;
	KpCarry continue_carry;
	int has_start_run;     /* the run the next game starts with when it continues none (a history's cinematic row's:
	                        * its cached row, not the mission's first row) */
	KpCampaignRun start_run;
	/* a game's end, opened once the glue's Sleep is over (kp_glue_sleep): an in-game menu's exit (after_game), or the
	 * result's score screen (after_score) with what the game's end knew */
	int after_game, after_exit;
	KpCarry after_carry;
	int64_t after_cheats;
	int after_score, after_result, after_from_save, after_network, after_local;
	uint32_t after_elapsed;
	uint8_t after_storm_results[KP_NETGAME_PLAYERS];
	/* the cinematics the flow queued and what follows them: the mission to start, the main menu to open, or the next
	 * step after a score screen */
	struct { int kind; int64_t mission; int state; } cine_then;
	int step_cinematics[KP_APP_STEP_CINEMATICS], step_cinematic_count; /* the cinematic rows between the finished
	                                                                      * mission and the next step */
	/* the recorded game's KPRX tail (owned: what it started from) and whether its LastReplay was written (EndGame
	 * 0x443db0 writes it once a game that started ends) */
	KpRepTail *rep_tail;
	int replay_saved;
	KpRep *replay_play;  /* Load Replay's file the next game plays (its bytes freed once the session has them) */
	int playing_replay;  /* the game is a replay's playback (no campaign, no ready room, no LastReplay) */
	/* the custom map played when current is KP_FLOW_CUSTOM (ADR 0024): its entry (row 0, no catalog, key "maps/<name>"),
	 * its map (open while a game plays it: its sounds), the load its scenario is read for and the game's setup (a
	 * replay's comes from its header) */
	KpDataMission custom_entry;
	KpDataMap *custom_map;
	KpChkLoad custom_load;
	int has_custom_setup;
	KpGameSetup custom_setup;
	/* the custom map's file (GetCurrentMapPath 0x46b0b0; "" for a replay's map; a save's resolved again, as
	 * ResolveGameFile 0x45cb90 does, whether or not it is there), its saves' name ("maps/" and the path below the maps'
	 * folder, StripKnownDirectoryFromMapPath 0x49a130) and the next scenario its game's triggers named when it ended
	 * (0x526526, UTF-8; "" none): a Play Custom game's end goes on to it (CreateNextScenarioGame 0x4bb360) */
	char custom_path[KP_FLOW_MAP_PATH];
	char custom_key[KP_SAVE_NAME];
	char custom_next[2 * KP_NEXT_SCENARIO];
	/* the maps' signature key (kp_data_map_key), read when a template first asks a map's class: 0 not yet, 1 read,
	 * -1 none */
	int map_key_state;
	uint8_t map_key[KP_MAP_KEY_SIZE];
	/* the gluCustm screen's list: each entry's folder among the listed folder's bases (entry_bases: the user's, then the
	 * data folders') */
	uint8_t entry_base[KP_UI_MAP_ENTRIES];
	/* the game RNG's state when the last game ended (0 before the process's first): a custom game's lobby resolves its
	 * random races from it (SetLocalRaceAndResolve 0x479c90) */
	uint32_t rng_state;
} KpFlow;

/* The flow over its host and data (set in f before): the interface and the video options; --mission's game, or the
 * glue's main menu. 0, else -1 with err. */
int kp_flow_init(KpFlow *f, char *err, size_t err_size);
/* The start's languages chosen (read_locale's choice: --locale's, the glue's profile's or system's) and whether their
 * files are there, which the host asks each frame before kp_flow_init until they are: 1, else 0 while they are on their
 * way (host.language_files: the web fetches their groups); one that cannot be had is 1 too (said: the base's parts). */
int kp_flow_languages_ready(KpFlow *f);
/* A frame: the glue's screen or the game's (its steps on the host's clock, its console), the saves asked for. */
void kp_flow_frame(KpFlow *f);
void kp_flow_free(KpFlow *f);
int kp_flow_start_mission(KpFlow *f, int index, char *err, size_t err_size);
/* The custom map's game (custom_map, custom_load, custom_setup set before; ADR 0024): 0, else -1 with err. */
int kp_flow_start_custom(KpFlow *f, char *err, size_t err_size);
/* --map's game: the map at path under the template named (templates.lst's, ASCII case folded; NULL or "": Melee), the
 * player's slot and every other a melee template's map has a computer, all Random, as Play Custom's screen offers them
 * (RefreshComputerSlots 0x456420, FillRaceList 0x456170); another template's slots as the map has them. Started at once,
 * without a ready room. 0, else -1 with err. */
int kp_flow_play_map(KpFlow *f, const char *path, const char *template_name, char *err, size_t err_size);
/* --seats' seat shown (0 the player's, the others' as kp_seats_setup numbers them): its session's world and a console of
 * its own, the view on its screen; the seat shown before keeps running without an interface. 0, else -1 with why. */
int kp_flow_show_seat(KpFlow *f, int seat, char *why, size_t why_size);
/* A saved game started from its file, as the glue's Load Saved starts it (0, else -1 with why). */
int kp_flow_load_saved(KpFlow *f, const char *name, char *why, size_t why_size);
/* Load Replay's Ok on the replays folder's file name (.rep added when it lacks it): its mission played from the file;
 * -1 with why when it cannot be. */
int kp_flow_play_replay(KpFlow *f, const char *name, char *why, size_t why_size);
/* Load Replay's Import, the host's answer to pick_replay: the picked file's name (a path or a URI, whose last part names
 * the copy) and bytes. A readable replay is copied into the listed folder under a free name of at most 31 bytes (".rep"
 * kept or added, " (2)" and on for a name taken) and the list shows it selected; else Load Replay's popup refuses it.
 * The copy's name in out (NULL: not wanted), 0; or -1. */
int kp_flow_import_replay(KpFlow *f, const char *name, const uint8_t *bytes, size_t size, char *out, size_t out_size);
/* Play Custom's Import, the host's answer to pick_file's KP_FLOW_PICK_MAP: as kp_flow_import_replay, into the listed
 * folder's user base (the user's maps folder, made as needed), ".scm" or ".scx" kept, else the screen's (".scx" with
 * Brood War); the copy is read as the list reads a map (a file that is no map of the screen's is removed again, gluAll
 * 63 or 60). The copy's name in out (NULL: not wanted), 0; or -1. */
int kp_flow_import_map(KpFlow *f, const char *name, const uint8_t *bytes, size_t size, char *out, size_t out_size);
/* A picked file's name as an import names its copy (kp_flow_import_replay without the free name's suffix). */
void kp_flow_replay_import_name(const char *source, char *out, size_t size);
void kp_flow_clamp_camera(KpFlow *f);
/* The picture's rectangle (win_x..win_h) for the screen shown: the safe area, or the whole drawable on a fixed screen. */
void kp_flow_picture(KpFlow *f);
/* --cine-play: the cinematics ids[0..n) at once, then the glue's main menu again (its music, which the movies
 * stopped); 1 when one plays. */
int kp_flow_cinematics(KpFlow *f, const int *ids, int n);
/* The subtitles shown or hidden (the S key while a movie plays): the sound options' check box, kept in the profile. */
void kp_flow_toggle_subtitles(KpFlow *f);
/* Whether the cinematics show their subtitles: the sound options' Subtitles (--subtitles over the profile's). */
int kp_flow_subtitles(const KpFlow *f);
/* The most frames a second the app may draw, 0 for no limit: --frame-cap, else KP_THROTTLE_FPS while the speed options'
 * Enable CPU throttling is on (followed at once under the open dialog, as its Cancel puts it back). A run that keeps the
 * originals' options, or a script's, has only the flag. The steps follow the clock, so the game runs at the same speed. */
int kp_flow_frame_cap(const KpFlow *f);
/* Whether a glue screen shows (it gets the input, the game does not). */
int kp_flow_glue_active(const KpFlow *f);
/* Whether the flags run a development check (--autotest, --cine-check, a script's "options originals"): its options
 * are the originals' or the defaults, never the player's, it writes no profile and plays no first start's intro. */
int kp_flow_dev_checks(const KpAppFlags *fl);
/* Whether a script drives the run (--script, --script-live). */
int kp_flow_scripted(const KpAppFlags *fl);
/* Whether a software Vulkan device (lavapipe, SwiftShader) may draw (ADR 0028): only under a given --gfx gpu (the checks
 * in a container). It draws on the CPU, many times slower than GL on the machine's GPU: auto and the video options'
 * Vulkan, the profile's choice, want a hardware device and fall back to GL without one. */
int kp_flow_software_gpu(const KpAppFlags *fl);
/* The language of languages wanted names ("ru_RU", "ru-RU", "ruRU", "ru"; ADR 0016) that is installed for the text:
 * its language and country, else the first of its language; -1 none. */
int kp_flow_match_language(const KpUiLanguages *languages, const char *wanted);
/* A new interface takes the video options (its dialog shows them; the gamma and the portraits are its own). */
void kp_flow_video_to_ui(KpFlow *f, KpUi *ui);
/* The interface's host hooks over the flow's files (ui->host = f): the map lists (Play Custom's, Create Game's, Load
 * Replay's), the saves, the replays, Import and Export. They read f's data, flags (its data folders' Maps), pref,
 * identity, ui and host (log; the deletes' sync_files, Import's pick_file and export_replay): the menus oracle
 * installs them over a flow of only those (kp_oracle menus_replay). */
void kp_flow_interface_host(KpFlow *f, KpUi *ui);
/* The map of the map list's entry index (LoadMapHeader 0x46b5c0 of its file): its header and the file's path, 1, or 0
 * when it is not a readable map (KpNetUi.entry_map: Create Game's network game). */
int kp_flow_entry_map(KpFlow *f, int index, KpMapHeader *h, char *path, size_t size);
/* The saved network game of the map list's entry index (ValidateSaveFile 0x499750 of its file): its game header, 1, or
 * 0 when it is not a readable save (KpNetUi.entry_save: Create Game's game resuming it). */
int kp_flow_entry_save(KpFlow *f, int index, KpNetGameHeader *h);
/* The replay a Create Game screen's entry names, as a hosted replay's game takes it (KpNetUi.entry_replay): 1, or 0
 * when Load Replay would refuse it (check_replay) or its scenario's header does not read. */
int kp_flow_entry_replay(KpFlow *f, int index, KpNetReplayEntry *out);
/* A viewer of the network game the host at `address` serves ("name[:port]"; kp_watch.h): its replay played as it grows,
 * at once without the glue's screens once the start of it came. 0, or -1 with why (no sockets: the web; a glue
 * screen open; a viewer already). */
int kp_flow_watch(KpFlow *f, const char *address, char *why, size_t why_size);
/* FindMultiplayerSaveById 0x4995e0 (the user's maps folder: its saves of this game's extension, then its subfolders',
 * in FindFirstFileA's order; the first whose header has the ID names the save), LoadSavedGameByName 0x498bd0 (the
 * first file of that name and extension below the folder, in the same order) and ReadSaveHeaderAndGameBlock 0x499c50
 * (KpNetGameHooks.find_save) over Koprulu's network saves (and the original's with KpAppFlags.original_saves): 1 with
 * out read and KpFlow.net_load the file, 0 none. */
int kp_flow_find_save(KpFlow *f, uint32_t id, KpNetGameSave *out);
/* The map transfer's files over the flow's folders (KpNetGameConfig.transfer; its send is the game's): read a disk
 * file, find a local copy of an offered file (FindLocalCopy 0x441080: the user's maps folder, its download folder
 * included, then each data folder's Maps, recursively) and write a finished download into <user>maps/download/
 * (SaveDownloadedFile 0x4418c0). */
KpTransferIo kp_flow_transfer_io(KpFlow *f);

#ifdef __cplusplus
}
#endif

#endif
