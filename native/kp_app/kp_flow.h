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
#include "kp_session.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KP_APP_SEED 0x12345678u
#define KP_APP_STEP_CINEMATICS 16 /* the cinematic rows a campaign step passes before its mission or its end */
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
	int play;              /* the mission in play mode (with the console) */
	int scale_mode;        /* --scale-mode auto|integer|sharp */
	int font_mode;         /* --font original|vector|auto (KP_VTEXT_*, KP_VIDEO_AUTO): the vector text of large scales */
	int graphics;          /* --graphics original|hd2|hd|auto (KP_GRAPHICS_*, KP_VIDEO_AUTO): the world's art */
	int hd_budget;         /* --hd-budget MB: the HD pages' slots' memory (0: KP_HD_BUDGET; streaming checks) */
	int hd_decode;         /* --hd-decode: the HD pages decoded as without S3TC (a phone's path on a computer) */
	int lighting;          /* --lighting on|off|auto (KP_LIGHTING_*, KP_VIDEO_AUTO: on): the HD sprites' lighting layers */
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
	/* the video options the player has (KP_VIDEO_HAS_*) with the interface ui */
	uint8_t (*video_available)(void *ctx, const KpUi *ui);
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
	/* Load Replay's Import and Export (NULL: no file picker on the platform, the buttons stay hidden): a picker for a
	 * .rep to read, answered later by kp_flow_import_replay (or not at all when it is cancelled); a picker for where a
	 * replay's bytes go, under its name (the host copies the bytes) */
	void (*pick_replay)(void *ctx);
	void (*export_replay)(void *ctx, const char *name, const uint8_t *bytes, size_t size);
} KpFlowHost;

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
	int world;             /* the host's world stands */
	int64_t loads;         /* the session's loads the world has shown */
	int current;           /* the mission shown, -1 none */
	int waiting;           /* the mission current waits for its files (host.mission_files) before it starts */
	int playing;
	int result;            /* the game's result for the interface's result dialog, 0 none */
	int glue_room;         /* the ready room's glue state of the game started from the glue, 0 none */
	char pending_save[KP_PROFILE_SAVE_NAME], pending_load[KP_PROFILE_SAVE_NAME], load_on_start[KP_PROFILE_SAVE_NAME];
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
	int after_game, after_exit;
	KpCarry after_carry;
	int64_t after_cheats;
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
/* The language of languages wanted names ("ru_RU", "ru-RU", "ruRU", "ru"; ADR 0016) that is installed for the text:
 * its language and country, else the first of its language; -1 none. */
int kp_flow_match_language(const KpUiLanguages *languages, const char *wanted);
/* A new interface takes the video options (its dialog shows them; the gamma and the portraits are its own). */
void kp_flow_video_to_ui(KpFlow *f, KpUi *ui);

#ifdef __cplusplus
}
#endif

#endif
