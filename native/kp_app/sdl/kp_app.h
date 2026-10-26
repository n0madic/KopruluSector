/*
 * kp_app: the product over SDL 3.4 (plan phase S, stage S5; ADR 0012): SDL's callbacks run the window, the GL context,
 * the input, the audio (SDL3_mixer), the files and the saves; the session (kp_session.h), the interface's host
 * (kp_hud.h), the presentation (kp_present.h) and the drawing (kp_gfx.h) do the rest. All its state lives in one KpApp
 * (SDL's appstate); nothing is global.
 */
#ifndef KP_APP_H
#define KP_APP_H

#include <SDL3/SDL.h>
#include <SDL3_mixer/SDL_mixer.h>

#include "kp_gfx.h"
#include "kp_hud.h"
#include "kp_present.h"
#include "kp_session.h"

#define KP_APP_NAME "KopruluSector"
#define KP_APP_MISSIONS 256
#define KP_APP_PACKS 8
#define KP_APP_CHANNELS 8 /* the original's eight sound channels (kp_ui_sound.c) */
#define KP_APP_STREAMS 8
#define KP_APP_SFX 1200
#define KP_APP_TRACKS 32
#define KP_APP_SOUND_PACKS 4 /* sounds.kpd PACK's packs: effects, terran, protoss, zerg */
#define KP_APP_PLAY_CHECKS 4 /* --play-check 1..4 (kp_app_dev.c) */
#define KP_APP_PAN_FULL 10000
#define KP_APP_SEED 0x12345678u
#define KP_APP_PROFILE "profile.kpprof"

/* A mission of the catalog (generated/missions/index.txt) or a test scenario on a catalog map. */
typedef struct {
	char name[64];      /* "terran/terran01", "scenarios/marine_group" */
	char kpm[160];      /* "/generated/missions/terran/terran01.kpm": the saves' mission name */
	int64_t index;      /* the campaign row of the map (0x526524) */
	int catalog;
} KpAppMission;

/* The sound mixer: the interface's channels, the music, the streams and the click. */
typedef struct {
	MIX_Mixer *mixer;
	MIX_Track *channels[KP_APP_CHANNELS], *music, *streams[KP_APP_STREAMS], *click;
	int stream_next;
	MIX_Audio *sfx[KP_APP_SFX];       /* decoded on first play */
	MIX_Audio *tracks[KP_APP_TRACKS];
	MIX_Audio *wavs[256];             /* the mission's trigger WAVs by WAVS index */
	int track, music_volume, has_track;
	const uint8_t *trks;              /* music.kpd TRKS (in KpApp's music bytes), found on the first track */
	size_t trks_size;
	int trks_read;
	int64_t started, streamed;
	/* where the sounds come from */
	const uint8_t *pack_of;           /* sounds.kpd PACK: the pack (1-based, 0 none) of each sfx id */
	uint16_t pack_count;
	/* a sound pack outside a game (the glue's sounds), loaded on its first sound and kept */
	uint8_t *own_bytes[KP_APP_SOUND_PACKS];
	KpPack own[KP_APP_SOUND_PACKS];
	int own_tried[KP_APP_SOUND_PACKS];
} KpAudio;

/* A loaded resource pack: its container's bytes and its pages decoded (8-bit indices). */
typedef struct {
	char name[32];
	uint8_t *bytes;
	size_t size;
	KpPack pack;
	uint8_t **pages;
} KpAppPack;

/* The world of the playing game: its presentation and drawing data. */
typedef struct {
	int active;
	const KpMission *mission; /* the session's (its host data outlives the world): packs, sounds, colours */
	KpTileset tileset;        /* over the session's tileset file */
	uint16_t *ids;
	uint8_t *sheet;
	int32_t sheet_w, sheet_h;
	KpLookups lookups;
	KpAppPack packs[KP_APP_PACKS];
	int pack_count;
	uint32_t *afrm;
	KpFrameTable frames;
	KpImageData data;
	KpBars bars;
	KpImagesPresenter presenter;
	KpFogLayer fog;
	KpPack mission_sounds;
	uint8_t *mission_sounds_bytes;
	int64_t loads;
	/* StarCraft: Remastered's HD art (ADR 0013): its packs beside the SD ones, its frame table, the map's megatiles */
	int hd_scale; /* HD pixels a logical pixel (2: HD2, 4: HD); 0 none */
	KpHdPack hd_packs[KP_APP_PACKS];
	uint8_t *hd_bytes[KP_APP_PACKS];
	KpFrameTable hd_frames;
	uint8_t *hd_tileset_bytes;
	KpHdTileset hd_tileset;
	KpHdTerrain hd_terrain;
} KpAppWorld;

/* --graphics: the world in the original's indexed art, in StarCraft: Remastered's HD art (generated/hd/, made from the
 * player's install by `python3 -m converter hd`), or auto: HD from KP_HD_AUTO_SCALE up when it is there, HD (x4) above
 * KP_HD_X4_SCALE where the platform affords it, else HD2 (x2). */
enum { KP_GRAPHICS_ORIGINAL, KP_GRAPHICS_HD, KP_GRAPHICS_AUTO };
#define KP_HD_AUTO_SCALE 2.0
#define KP_HD_X4_SCALE 3.0

/* The development switches (command-line flags). */
typedef struct {
	char data[512];        /* --data DIR: the converted data's root (generated/ under it) */
	char mission[64];      /* --mission NAME: a game of it at once (no glue) */
	int play;              /* the mission in play mode (with the console) */
	int scale_mode;        /* --scale-mode auto|integer|sharp */
	int font_mode;         /* --font original|vector|auto (KP_VTEXT_*): the vector text of large scales */
	int graphics;          /* --graphics original|hd|auto (KP_GRAPHICS_*): the world's art */
	char vector_font[512]; /* --vector-font PATH: a TrueType file instead of ui.kpd's VFNT (font comparisons) */
	double vector_size;    /* --vector-size F: the vector glyphs' cap height over the FNT font's (1) */
	int fog;               /* --fog off */
	int world_only;        /* --world-only: no canvas (index captures) */
	int autotest, autotest_count;
	int play_check;        /* --play-check N */
	char screenshot[512];  /* --screenshot PATH */
	double screenshot_after;
	int bench_frames;      /* --bench-frames N: frame times of a game, then exit */
	int vsync;
	int win_w, win_h;
} KpAppFlags;

typedef struct KpApp KpApp;

struct KpApp {
	KpAppFlags flags;
	SDL_Window *window;
	SDL_GLContext gl;
	KpGfx *gfx;
	KpAudio audio;
	char pref[1024];
	int quit;
	int recheck_context;   /* back from the background: the GL context is checked on the next frame */
	/* the data every game and the interface read */
	uint8_t *dat, *palettes_bytes, *campaign_bytes, *sounds_bytes;
	size_t dat_size, palettes_size, campaign_size, sounds_size;
	KpHostShared shared; /* grpmeta.kpd, iscript.kpd, aiscript.kpd: parsed once for every game */
	KpPalettes palettes;
	KpCampaign campaign;
	KpSaveIdentity identity;
	uint8_t *ui_files[KP_UI_FILES];
	size_t ui_sizes[KP_UI_FILES];
	KpAppMission missions[KP_APP_MISSIONS];
	int mission_count;
	uint8_t *music_bytes;
	size_t music_size;
	/* the interface: one for the glue and its games */
	KpUi *ui;
	KpGlue glue;
	int glue_on;
	KpHud hud;
	int hud_on;
	KpCursor cursor;
	KpProfile profile;
	/* the game */
	KpSession *session;
	KpAppWorld world;
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
	int next_glue, step_glue;
	KpCarry next_carry;
	int has_continue;
	KpCampaignRun continue_run;
	int64_t continue_cheats;
	KpCarry continue_carry;
	int after_game, after_exit;
	KpCarry after_carry;
	int64_t after_cheats;
	/* the screen */
	int32_t win_w, win_h;   /* the drawable in pixels */
	float density;          /* pixels per window point */
	int mode;
	double scale;
	int32_t world_w, world_h;
	int fixed_screen;
	int64_t cam_x, cam_y;
	int has_mouse;
	int32_t mouse_lx, mouse_ly;
	KpPalette screen_palette;
	int has_screen_palette;
	uint8_t glue_rgb[768];
	/* the vector text (ADR 0011): the font of the interface it was made from */
	KpVText vtext;
	const KpUi *vtext_ui;
	void *vtext_file;      /* --vector-font's bytes */
	size_t vtext_file_size;
	double palette_clock;
	double interp;
	/* the keys' modifier sides held */
	int held_sides[6];
	/* the development checks */
	void *dev;
	double started;
};

/* kp_app_files.c: the data files and the preferences */
uint8_t *kp_app_read(KpApp *a, const char *path, size_t *size);
int kp_app_read_missions(KpApp *a);
int kp_app_load_data(KpApp *a, char *err, size_t err_size);
void kp_app_pref_path(KpApp *a, const char *file, char *out, size_t size);
void kp_app_save_profile(KpApp *a);
void kp_app_load_profile(KpApp *a);
/* The web's files written to IndexedDB (a no-op elsewhere). */
void kp_app_sync_files(KpApp *a);

/* kp_app_audio.c: the mixer of the interface's sound commands */
int kp_audio_init(KpAudio *au, const uint8_t *sounds_kpd, size_t size);
void kp_audio_free(KpAudio *au);
void kp_audio_commands(KpApp *a, const KpUiSoundCommand *c, size_t n);
void kp_audio_stop_all(KpAudio *au);
void kp_audio_mission(KpApp *a);
/* The mixer's own copies of the sound packs dropped: a world holds them now (the glue reads them again when it needs
 * a sound not yet decoded). */
void kp_audio_drop_packs(KpAudio *au);

/* kp_app_world.c: the playing game's world */
int kp_world_build(KpApp *a, char *err, size_t err_size);
void kp_world_free(KpApp *a);
/* The world's textures from its CPU copies (after the build, and again after a lost context). */
void kp_world_upload(KpApp *a);
/* Per frame: the presentation after the game's steps (ticks) or a camera move. */
void kp_world_frame(KpApp *a, int ticks, double fraction);
void kp_world_draw(KpApp *a);
KpRect kp_world_view(const KpApp *a);

/* kp_app_flow.c: the glue and the games */
int kp_flow_init(KpApp *a, char *err, size_t err_size);
void kp_flow_frame(KpApp *a);
void kp_flow_free(KpApp *a);
int kp_flow_start_mission(KpApp *a, int index, char *err, size_t err_size);
void kp_flow_clamp_camera(KpApp *a);
/* Whether a glue screen shows (it gets the input, the game does not). */
int kp_flow_glue_active(const KpApp *a);
double kp_app_now(void);

/* kp_app_input.c: the window's input for the interface */
void kp_input_event(KpApp *a, const SDL_Event *e);
void kp_input_focus(KpApp *a, int active);
/* A mouse event at the logical screen's point (the checks' input too). */
void kp_input_mouse(KpApp *a, int event, int32_t lx, int32_t ly);

/* kp_app_dev.c: the development checks (the autotest, play checks, screenshots, the frame benchmark) */
int kp_dev_init(KpApp *a);
void kp_dev_frame(KpApp *a);
void kp_dev_free(KpApp *a);
/* The frame benchmark's times of a frame (--bench-frames). */
void kp_dev_timing(KpApp *a, double flow_ms, double draw_ms);
/* A line of the checks' log ("SPIKE ..."). */
void kp_dev_line(const char *fmt, ...);

#endif
