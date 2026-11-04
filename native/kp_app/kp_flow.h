/*
 * The product's flow without SDL (plan phase S, stage S5): the glue's screens (the menus, the ready rooms, the score
 * screens) run the campaign's games, a game's console is the same interface's, the game's end leads to the score
 * screen and the campaign's next game; the saves, the video options, the screen's layout and the view. The platform's
 * part (the clock, the files, the world's presentation and drawing, the sound, the cinematics, the cursor) is the
 * host's (KpFlowHost): sdl/kp_app_flow.c over SDL, a recording host in the tests (tests/c/test_flow.c).
 */
#ifndef KP_FLOW_H
#define KP_FLOW_H

#include "kp_hud.h"
#include "kp_session.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KP_APP_PLAY_CHECKS 5 /* --play-check 1..5 (kp_app_dev.c) */
#define KP_APP_SEED 0x12345678u
#define KP_APP_STEP_CINEMATICS 16 /* the cinematic rows a campaign step passes before its mission or its end */
/* The original's cinematic numbers (0x508da8) the main menu's View Intro plays: Blizzard's logo, then the Brood War
 * intro (mode 0: 24 when the expansion's data is there, which the product's always is) */
enum { KP_APP_CINE_LOGO = 0, KP_APP_CINE_EXPANSION_INTRO = 24 };

/* A mission of the catalog (generated/missions/index.txt) or a test scenario on a catalog map. */
typedef struct {
	char name[64];      /* "terran/terran01", "scenarios/marine_group" */
	char kpm[160];      /* "/generated/missions/terran/terran01.kpm": the saves' mission name */
	int64_t index;      /* the campaign row of the map (0x526524) */
	int catalog;
} KpAppMission;

/* The development switches (command-line flags). */
typedef struct {
	char data[512];        /* --data DIR: the converted data's root (generated/ under it) */
	char mission[64];      /* --mission NAME: a game of it at once (no glue) */
	int play;              /* the mission in play mode (with the console) */
	int scale_mode;        /* --scale-mode auto|integer|sharp */
	int font_mode;         /* --font original|vector|auto (KP_VTEXT_*, KP_VIDEO_AUTO): the vector text of large scales */
	int graphics;          /* --graphics original|hd2|hd|auto (KP_GRAPHICS_*, KP_VIDEO_AUTO): the world's art */
	int hd_budget;         /* --hd-budget MB: the HD pages' slots' memory (0: KP_HD_BUDGET; streaming checks) */
	int hd_decode;         /* --hd-decode: the HD pages decoded as without S3TC (a phone's path on a computer) */
	int lighting;          /* --lighting on|off|auto (KP_LIGHTING_*, KP_VIDEO_AUTO: on): the HD sprites' lighting layers */
	char vector_font[512]; /* --vector-font PATH: a TrueType file instead of ui.kpd's VFNT (font comparisons) */
	double vector_size;    /* --vector-size F: the vector glyphs' cap height over the FNT font's (1) */
	int fog;               /* --fog off */
	int fog_style;         /* --fog original|smooth (KP_FOG_*; KP_VIDEO_AUTO: the art's, stage R5) */
	int water;             /* --water on|off (KP_WATER_*; KP_VIDEO_AUTO: the art's, stage R5) */
	int video_given;       /* KP_VIDEO_GIVEN_*: the video options the command line set for the run (kp_session.h) */
	int world_only;        /* --world-only: no canvas (index captures) */
	int autotest, autotest_count;
	int play_check;        /* --play-check N */
	char screenshot[512];  /* --screenshot PATH */
	char video_check[512]; /* --video-check DIR: the video options clicked through in --mission's game (kp_app_dev.c) */
	char hd_check[512];    /* --hd-check DIR: the HD interface's coverage of --mission's screens and menus (kp_app_dev.c) */
	char save_check[512];  /* --save-check DIR: --mission's game saved and loaded back, in it and from its file (kp_app_dev.c) */
	char touch_check[512]; /* --touch-check DIR: --mission's game played by synthetic fingers (kp_app_dev.c) */
	double screenshot_after;
	int camera;            /* --camera X,Y: the view's top-left map pixel, held (the screenshots) */
	int32_t camera_x, camera_y;
	int bench_frames;      /* --bench-frames N: frame times of a game, then exit */
	char cinematics[8];    /* --cinematics auto|sd|hd2|off: the cinematics' pack (ADR 0015; empty: auto) */
	char subtitles[8];     /* --subtitles on|off: the cinematics' subtitles at the start (empty: the profile's) */
	char cine_check[512];  /* --cine-check DIR: every cinematic of the pack decoded to its end, first and last pictures saved */
	char cine_play[64];    /* --cine-play LIST: cinematic numbers (comma separated) played at once, then the menu */
	int vsync;
	int win_w, win_h;
} KpAppFlags;

/* What the flow asks of the platform; every call but clock_ms gets ctx. */
typedef struct {
	void *ctx;
	/* the wall clock in seconds; the session's step budget in milliseconds (kp_session_advance; NULL: no budget) */
	double (*now)(void *ctx);
	double (*clock_ms)(void);
	void (*log)(void *ctx, const char *line);
	/* a file of the converted data (malloc'd), NULL when it cannot be read */
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
	/* the sound: the interface's commands, every sound stopped */
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
} KpFlowHost;

typedef struct KpFlow {
	KpFlowHost host;
	/* what the product loaded for every game (not owned; set before kp_flow_init) */
	KpAppFlags *flags;
	const KpAppMission *missions;
	int mission_count;
	const uint8_t *dat;
	size_t dat_size;
	const KpHostShared *shared;
	const KpCampaign *campaign;
	const KpSaveIdentity *identity;
	uint8_t *const *ui_files;
	const size_t *ui_sizes;
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
	int cine_subtitles;  /* the cinematics' subtitles are shown: --subtitles, else the profile's choice */
	KpHud hud;
	int hud_on;
	/* the video options: the profile's with the flags' over them (the art, the fog, the water, the text and the lighting
	 * in flags), the dialog's changes applied each frame */
	int32_t gamma, portraits;
	KpVideoSettings video_flags;          /* the art, fog, water, text, lighting the flags gave (kp_profile_commit_video) */
	uint8_t video_available;              /* KP_VIDEO_HAS_* */
	uint32_t video_serial, video_commits; /* the interface's counters last applied */
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
	int playing;
	int result;            /* the game's result for the interface's result dialog, 0 none */
	int glue_room;         /* the ready room's glue state of the game started from the glue, 0 none */
	char pending_save[64], pending_load[64], load_on_start[64];
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
} KpFlow;

/* The flow over its host and data (set in f before): the interface and the video options; --mission's game, or the
 * glue's main menu. 0, else -1 with err. */
int kp_flow_init(KpFlow *f, char *err, size_t err_size);
/* A frame: the glue's screen or the game's (its steps on the host's clock, its console), the saves asked for. */
void kp_flow_frame(KpFlow *f);
void kp_flow_free(KpFlow *f);
int kp_flow_start_mission(KpFlow *f, int index, char *err, size_t err_size);
/* A saved game started from its file, as the glue's Load Saved starts it (0, else -1 with why). */
int kp_flow_load_saved(KpFlow *f, const char *name, char *why, size_t why_size);
void kp_flow_clamp_camera(KpFlow *f);
/* The picture's rectangle (win_x..win_h) for the screen shown: the safe area, or the whole drawable on a fixed screen. */
void kp_flow_picture(KpFlow *f);
/* --cine-play: the cinematics ids[0..n) at once, then the glue's main menu again (its music, which the movies
 * stopped); 1 when one plays. */
int kp_flow_cinematics(KpFlow *f, const int *ids, int n);
/* The cinematics' subtitles shown or hidden (the S key while a movie plays): the choice kept in the profile
 * (KP_UI_OPTION_CINE_SUBTITLES_OFF) and written. */
void kp_flow_toggle_subtitles(KpFlow *f);
/* Whether a glue screen shows (it gets the input, the game does not). */
int kp_flow_glue_active(const KpFlow *f);
/* Whether the flags run a development check (--autotest, --play-check, --video-check, --hd-check, --save-check,
 * --touch-check, --cine-check): its options are the originals' or the defaults, never the player's, it writes no
 * profile and plays no first start's intro. */
int kp_flow_dev_checks(const KpAppFlags *fl);
/* A new interface takes the video options (its dialog shows them; the gamma and the portraits are its own). */
void kp_flow_video_to_ui(KpFlow *f, KpUi *ui);
/* The missions of the catalog's index (generated/missions/index.txt: "name tileset width height" a line, the campaign
 * row its number among the missions from 1: the lines that are no mission count none) and the test scenarios'
 * (generated/scenarios/index.txt: "name base tileset width height", the row of their base; NULL none) into
 * out[0..cap): their count. */
int kp_flow_missions(KpAppMission *out, int cap, const char *catalog, size_t catalog_size, const char *scenarios,
	size_t scenarios_size);

#ifdef __cplusplus
}
#endif

#endif
