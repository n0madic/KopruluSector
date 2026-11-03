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

#ifdef KP_CINEMATICS
#include "kp_cine.h"
#endif
#include "kp_flow.h"
#include "kp_gfx.h"
#include "kp_hud.h"
#include "kp_music.h"
#include "kp_present.h"
#include "kp_session.h"
#include "kp_touch.h"

#define KP_APP_NAME "KopruluSector"
#define KP_APP_MISSIONS 256
#define KP_APP_PACKS 8
#define KP_APP_CHANNELS 8 /* the original's eight sound channels (kp_ui_sound.c) */
#define KP_APP_STREAMS 8
#define KP_APP_SFX 1200
#define KP_APP_WAVS 256 /* a mission's trigger WAVs by WAVS index */
#define KP_APP_SOUND_PACKS 4 /* sounds.kpd PACK's packs: effects, terran, protoss, zerg */
#define KP_APP_PAN_FULL 10000
#define KP_APP_PROFILE "profile.kpprof"
#define KP_APP_WINDOW_W 1280 /* the desktop window's first size (--window W H) */
#define KP_APP_WINDOW_H 960

/* The sound mixer: the interface's channels, the music, the streams and the click. */
typedef struct {
	MIX_Mixer *mixer;
	MIX_Track *channels[KP_APP_CHANNELS], *music, *streams[KP_APP_STREAMS], *click;
#ifdef KP_CINEMATICS
	MIX_Track *cine;                  /* a cinematic's Ogg Vorbis (kp_app_cine.c) */
#endif
	int stream_next;
	MIX_Audio *sfx[KP_APP_SFX];       /* decoded on first play */
	MIX_Audio *wavs[KP_APP_WAVS];     /* the mission's trigger WAVs by WAVS index */
	int track, music_volume, has_track;
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

/* Byte ranges of the data files read without waiting (kp_app_fetch.c, stage R4b): on the web HTTP Range requests to
 * the files beside the page, elsewhere a read done at once. */
#define KP_APP_WEB_DATA "/data/" /* the web build's data root (its preloaded files; the HD2 art is fetched) */
#define KP_APP_WEB_SIZES "generated/hd/hd2/sizes.txt" /* "path size" per fetched file (tools/web_hd_heads.py) */
enum { KP_FETCH_PENDING, KP_FETCH_DONE, KP_FETCH_FAILED };
typedef struct KpFetch KpFetch;
/* Starts reading size bytes at offset of a data file (its full path, kp_app_path; size 0: the whole file); NULL out of
 * memory. */
KpFetch *kp_fetch_start(const char *path, uint32_t offset, uint32_t size);
int kp_fetch_state(const KpFetch *f); /* KP_FETCH_*; NULL: failed */
/* A request that ended: its bytes (NULL when it failed; the caller frees them), the request freed; NULL and kept while
 * it is pending. */
uint8_t *kp_fetch_take(KpFetch *f, size_t *size);
/* A request no longer wanted (one still out is dropped when it ends). */
void kp_fetch_free(KpFetch *f);

/* The HD pages' loader (kp_app_hd.c, stage R4a): a thread reading the pages asked for from the HD packs' files (without
 * threads, the web's, fetched as byte ranges). */
typedef struct KpHdLoader KpHdLoader;
typedef struct {
	int32_t page;
	uint32_t generation; /* the request's: a result of an earlier generation is dropped */
	int ok;
	uint8_t *diffuse, *team; /* the page's blocks, or its decoded texels; malloc'd */
	uint8_t *light[KP_HD_LIGHTS]; /* a lit page's lighting layers' blocks (NULL: not asked for); malloc'd */
} KpHdLoaded;

KpHdLoader *kp_hd_loader_new(void);
void kp_hd_loader_free(KpHdLoader *l);
/* Asks for a page: its blocks (of KP_HD_FORMAT_* format) at the offsets of the file at path, decoded (RGBA8 diffuse,
 * the team mask's red) when decode is set, else with light (not NULL) its lighting layers' blocks at those offsets. */
int kp_hd_loader_request(KpHdLoader *l, int32_t page, const char *path, uint32_t diffuse_offset, uint32_t team_offset,
	const uint32_t *light, int32_t page_size, int format, int decode, uint32_t generation);
/* A page read, if any (wait: until one is, while any is asked for); 0 none. */
int kp_hd_loader_result(KpHdLoader *l, KpHdLoaded *out, int wait);
void kp_hd_loaded_free(KpHdLoaded *r);

/* An HD portrait being shown (kp_app_hd_ui.c, stage R4e): its clip read whole, decoded frame by frame into its place
 * in the interface's atlas (one place a slot, KP_HD_VIDEO_MAX square). */
#define KP_APP_HD_CLIPS 6 /* the console's portrait and the briefing's four, one spare */
#define KP_APP_HD_UI_FETCHES 16 /* the interface's frames fetched at once (the web) */
typedef struct {
	char path[KP_HD_UI_PATH]; /* the SD movie's path; "" a free slot */
	uint8_t *bytes;
	KpSmk smk;
	uint8_t *rgba;
	KpFetch *fetch;           /* its clip on its way (the web) */
	uint32_t size;
	int32_t uploaded;         /* the frame in the atlas, -1 none */
	uint32_t generation;      /* the atlas's generation it was put in */
	uint32_t used;            /* when it was last shown (the slot of the oldest is taken) */
} KpAppHdClip;

/* The HD interface (kp_app_hd_ui.c, stage R4d): ui.kpd of the variant --graphics and the scale pick, opened from its
 * head (its images read as they are first shown), and the atlas of the images shown; the HD portraits (R4e). */
typedef struct {
	char variant[8];
	uint8_t *head;          /* NULL: off */
	KpHdUi index;
	char path[1200];
	KpHdUiAtlas atlas;
	uint32_t gfx_generation; /* the atlas's generation kp_gfx's texture was made for */
	float *items;           /* the item table of the frame */
	uint8_t *states;        /* and per item what became of its HD image (KP_HD_UI_ITEM_*) */
	int32_t item_cap, item_count;
	int failed[2];          /* a variant not imported: hd, hd2 */
	uint8_t *video_head;    /* portraits.kpd's head, NULL: none */
	KpHdVideo video;
	char video_path[1200];
	KpAppHdClip clips[KP_APP_HD_CLIPS];
	uint32_t clock;
	KpFetch *fetches[KP_APP_HD_UI_FETCHES]; /* frames on their way (the web) */
	int32_t fetch_frames[KP_APP_HD_UI_FETCHES];
	uint32_t fetch_asked[KP_APP_HD_UI_FETCHES]; /* the draw that last asked for each */
	uint32_t draws;         /* counts kp_hd_ui_draw's frames */
	uint8_t *failed_frames; /* per frame: it could not be read (not asked for again) */
} KpAppHdUi;

/* The world of the playing game: its presentation and drawing data. */
typedef struct {
	int active;
	const KpMission *mission; /* the session's (its host data outlives the world): packs, sounds, colours */
	KpTileset tileset;        /* over the session's tileset file */
	uint16_t *ids;            /* the megatile word the game shows per cell */
	uint16_t *cells;          /* per cell its megatile word and creep edge (kp_terrain_edge): the terrain map */
	/* the cells as of the game's terrain then (sync_terrain): the steps change it, and between them only what
	 * bumps the game's minimap_terrain (the remembered terrain refreshed in view, a load) */
	int terrain_synced;
	int64_t terrain_serial;
	uint8_t *sheet;           /* the megatiles, then creep's edge frames (edge_count) */
	int32_t sheet_w, sheet_h, edge_count;
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
	/* StarCraft: Remastered's HD art (ADR 0013): its packs beside the SD ones (their first bytes; the pages streamed
	 * into kp_gfx's slots: stage R4a), its frame table, the map's megatiles */
	int hd_scale; /* HD pixels a logical pixel (2: HD2, 4: HD); 0 none */
	KpHdPack hd_packs[KP_APP_PACKS];
	uint8_t *hd_bytes[KP_APP_PACKS];       /* each pack's header and frames */
	char hd_paths[KP_APP_PACKS][1200];     /* each pack's file */
	int32_t hd_page_base[KP_APP_PACKS + 1]; /* each pack's first page among all; [pack_count]: the page count */
	KpHdCache hd_cache;
	KpHdLoader *hd_loader;
	uint32_t hd_generation; /* of the slots' contents: a new context makes a new one */
	int hd_decode;          /* the pages go up decoded (no S3TC) */
	int hd_lit;             /* the lit pages' lighting layers go up too: their sprites drawn lit */
	int hd_lighting;        /* the slots were made to be lit (hd_lit_wanted: hd_slots again when the options change it) */
	int hd_prime;           /* the first frame waits for its pages */
	KpFrameTable hd_frames;
	uint8_t *hd_tileset_bytes;
	uint8_t *hd_water_bytes; /* water.kpd (NULL: still water) */
	KpFetch *hd_fetches[2];  /* the tileset and water.kpd on their way (the web); the HD world is drawn once they came */
	char hd_tileset_path[256];
	KpHdWater hd_water;
	double hd_clock;         /* when the world was built: the water's clock starts */
	KpHdTileset hd_tileset;
	KpHdTerrain hd_terrain;
	/* stage R5: the original's art drawn through the RGBA path for the modern effects (--fog smooth, --water on), with
	 * the water masks of the HD tileset when they are imported */
	int fx;
	/* the art the options asked for when it was loaded (kp_world_frame loads it again when they ask for another): the
	 * imported HD variant first tried (NULL none), whether the HD art was chosen, the water's surface */
	const char *art_variant;
	int art_hd, art_water;
} KpAppWorld;

/* --fog original|smooth (KP_FOG_*): the original's dark.pcx rows or StarCraft: Remastered's smooth darkening; --water
 * on|off (KP_WATER_*): the water's surface where the tileset's water masks are imported. By default (KP_VIDEO_AUTO) each
 * follows the art (the original's: the original's fog and still water; the HD art's: both); the development checks keep
 * the original's. Either effect over the original's art draws it through the RGBA path (ADR 0013, stage R5). The video
 * options change them in the game (kp_ui.h's KpUiVideoOptions). */
#define KP_FX_SCALE_MAX 4 /* the RGBA target's pixels a logical pixel at most for the original's art */

/* --graphics (KP_GRAPHICS_*): the world in the original's indexed art, in StarCraft: Remastered's HD art (generated/hd/,
 * made from the player's install by `python3 -m converter hd`) at x2 (HD2; a phone's in ETC2: tools/product.sh) or x4
 * (HD, a computer's), or auto (KP_VIDEO_AUTO): HD from KP_HD_AUTO_SCALE up when it is there, HD (x4) above
 * KP_HD_X4_SCALE on a computer, else HD2. */
#define KP_HD_AUTO_SCALE 2.0
#define KP_HD_X4_SCALE 3.0
/* The memory the HD pages' slots may take (bytes of texture), and the pages uploaded a frame at most. */
#if defined(SDL_PLATFORM_ANDROID) || defined(SDL_PLATFORM_IOS)
#define KP_HD_BUDGET (384u << 20)
#else
#define KP_HD_BUDGET (768u << 20)
#endif
#define KP_HD_SLOTS_MAX 256 /* GLES 3.0's least GL_MAX_ARRAY_TEXTURE_LAYERS */
#define KP_HD_UPLOADS_A_FRAME 4
#define KP_HD_PRIME_PAGES 64 /* the pages the first frame waits for at most */

/* kp_app_cine.c: the cinematics' state (ADR 0015) */
#define KP_APP_CINE_QUEUE (2 * KP_APP_STEP_CINEMATICS) /* --cine-play's movies: a step's rows each after its prelude */
#ifdef KP_CINEMATICS
typedef struct {
	KpCineRun run;          /* the movies' queue and the one playing (kp_cine.c), on SDL's clock */
	char path[1200];        /* the pack's file */
} KpAppCine;
#endif

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
	KpMusicIndex music_index;  /* packs/music.kpd's tracks: read from the file when one plays */
	uint8_t *music_bytes;      /* the whole container: the web only (its files are in memory already), else NULL */
	size_t music_size;
	KpProfile profile;
	KpCursor cursor;
	/* the flow: the glue, the games, the saves, the video options, the screen's layout and the view (kp_flow.h) */
	KpFlow flow;
	KpAppWorld world;
#ifdef KP_CINEMATICS
	KpAppCine cine;
#endif
	KpAppHdUi hd_ui;         /* the HD interface's art (stage R4d) */
	uint8_t *web_sizes;      /* the web: the fetched HD2 files' sizes (KP_APP_WEB_SIZES), read when first needed */
	size_t web_sizes_size;
	int web_sizes_read;
	/* the screen (the rest is the flow's) */
	float density;          /* pixels per window point */
	int has_mouse;
	int32_t mouse_lx, mouse_ly;
	/* the touch layer (ADR 0014): the last real device chose the mode; the cursor shows only while a finger aims */
	KpTouch touch;
	int touch_mode, touch_aim;
	int32_t touch_screen[3]; /* the screen its gesture began on: the glue's or the game's, the logical size */
	/* the vector text (ADR 0011): the font of the interface it was made from */
	KpVText vtext;
	const KpUi *vtext_ui;
	void *vtext_file;      /* --vector-font's bytes */
	size_t vtext_file_size;
	double interp;
	/* the keys' modifier sides held */
	int held_sides[6];
	/* the virtual key a phone's Back pressed (its release lets the same go) */
	uint16_t back_vk;
	/* the keys and the mouse buttons pressed while a cinematic played: their releases are the movie's, not the screen's
	 * after it (a bit a scancode; SDL_BUTTON_LMASK, SDL_BUTTON_RMASK) */
	uint8_t cine_keys[SDL_SCANCODE_COUNT / 8];
	uint32_t cine_buttons;
	/* the development checks */
	void *dev;
	double started;
};

/* kp_app_files.c: the data files and the preferences */
uint8_t *kp_app_read(KpApp *a, const char *path, size_t *size);
/* The path under the data root (--data) of a file of the converted data. */
void kp_app_path(const KpApp *a, const char *path, char *out, size_t size);
/* The first `want` bytes of a file (fewer when it is shorter; malloc'd, *size of them) and its size; NULL. */
uint8_t *kp_app_read_head(KpApp *a, const char *path, size_t want, size_t *size, size_t *file_size);
int kp_app_read_missions(KpApp *a);
int kp_app_load_data(KpApp *a, char *err, size_t err_size);
/* size bytes at offset of an open stream, malloc'd (NULL when they are not all in it or cannot be read). */
uint8_t *kp_io_read_at(SDL_IOStream *io, uint64_t offset, size_t size);
/* size bytes at offset of the open stream ctx (an SDL_IOStream *) into out; 0, or -1 when they cannot all be read (the
 * read callback of the indexes read from a file: the music's, the cinematics'). */
int kp_io_read_into(void *ctx, uint64_t offset, uint8_t *out, size_t size);
/* The file at path (its full path) opened with its first bytes in head (*n of them, at most cap) and its size in *size
 * (an index's read: the music's, the cinematics'); NULL when it cannot be opened (*size -1) or read (*size 0 or more,
 * SDL_GetError says why). */
SDL_IOStream *kp_io_open_head(const char *path, uint8_t *head, size_t cap, size_t *n, Sint64 *size);
void kp_app_pref_path(KpApp *a, const char *file, char *out, size_t size);
void kp_app_save_profile(KpApp *a);
void kp_app_load_profile(KpApp *a);
/* The web's files written to IndexedDB (a no-op elsewhere). */
void kp_app_sync_files(KpApp *a);

/* kp_app_audio.c: the mixer of the interface's sound commands */
/* The music container's track index (the web: the container itself, kept whole); -1 when the file cannot be read. */
int kp_app_music_open(KpApp *a, char *err, size_t err_size);
int kp_audio_init(KpAudio *au, const uint8_t *sounds_kpd, size_t size);
void kp_audio_free(KpAudio *au);
void kp_audio_commands(KpApp *a, const KpUiSoundCommand *c, size_t n);
void kp_audio_stop_all(KpAudio *au);
/* A volume in hundredths of a decibel (-10000 and below silent) as a linear gain. */
float kp_audio_gain(int32_t volume);
/* A world's end: its decoded sounds (the trigger WAVs and the sfx) are released. */
void kp_audio_mission(KpApp *a);
/* The mixer's own copies of the sound packs dropped: a world holds them now (the glue reads them again when it needs
 * a sound not yet decoded). */
void kp_audio_drop_packs(KpAudio *au);

#ifndef __EMSCRIPTEN__
/* size bytes at start of a file (its full path) as a stream of their own, offset 0 at start (NULL when it cannot be
 * opened or is shorter); closing it closes the file. */
SDL_IOStream *kp_app_range_io(const char *full, Sint64 start, Sint64 size);
#endif

/* kp_app_cine.c: the cinematics (ADR 0015), built with KP_CINEMATICS over libvpx; without it nothing plays and every
 * call below answers as if the pack were absent. */
/* The pack chosen by --cinematics read at start; 0 whether or not there is one (a missing pack is logged). */
int kp_app_cine_open(KpApp *a);
/* Whether cinematic id (the original's number) is in the pack. */
int kp_app_cine_has(const KpApp *a, int id);
/* The cinematics ids[0..n) one after the other (the ones the pack lacks left out), each after its prelude when
 * preludes is set (PlayCinematicWithPrelude 0x458fc0), the music stopped; after the ones playing when some are; 1 when
 * one plays, 0 when none does (nothing changed). */
int kp_app_cine_start(KpApp *a, const int *ids, int n, int preludes);
int kp_app_cine_active(const KpApp *a);
/* Per frame while active: the movie advanced by its clock and its picture drawn (visible 0: no context to draw with,
 * the movie goes on unseen), the movie ended when it is over. */
void kp_app_cine_frame(KpApp *a, int visible);
/* --cine-check: every movie of the pack decoded to its end at once (no clock), its first and last pictures saved into
 * dir as PNG, its middle one also drawn by the GPU and compared with the CPU's; the "SPIKE cine check" lines (kp_dev_line);
 * the movies that failed. */
int kp_app_cine_check(KpApp *a, const char *dir);
/* A press at when_ns (an event's timestamp) ends the movie playing early (the next one of the queue begins) when it
 * came after the movie's first picture was drawn: a press of the moment the movie began is the movie's before it. */
void kp_app_cine_skip(KpApp *a, uint64_t when_ns);
/* The whole queue ended at once. */
void kp_app_cine_stop(KpApp *a);
void kp_app_cine_free(KpApp *a);

/* kp_app_world.c: the playing game's world */
int kp_world_build(KpApp *a, char *err, size_t err_size);
void kp_world_free(KpApp *a);
/* The world's textures from its CPU copies (after the build, and again after a lost context). */
void kp_world_upload(KpApp *a);
/* Per frame: the presentation after the game's steps (ticks) or a camera move. */
void kp_world_frame(KpApp *a, int ticks, double fraction);
void kp_world_draw(KpApp *a);
/* The HD packs' lit pages (their sprites drawn lit when the slots hold their lighting layers). */
int32_t kp_world_hd_lit_pages(const KpApp *a);
/* The k-th HD variant to try ("hd", "hd2") for --graphics and the scale, or NULL past them or without HD. */
const char *kp_app_hd_variant(const KpApp *a, int k);
/* Before a canvas is composed: the HD interface on or off (its variant's art open, the canvas's item plane). */
void kp_hd_ui_mode(KpApp *a, KpCanvas *c);
/* After: its items resolved, their images placed and uploaded, the item table to kp_gfx (c NULL: none). */
void kp_hd_ui_draw(KpApp *a, KpCanvas *c);
void kp_hd_ui_lost(KpApp *a);
/* The HD interface's coverage of the canvas composed last (the development checks; kp_hd_ui_count_coverage with its
 * items' HD frames read again for their alpha). */
void kp_hd_ui_coverage(const KpApp *a, const KpCanvas *c, KpHdUiCoverage *out);
/* The cursor's HD frame for kp_gfx (drawn: the interface draws a cursor this frame). */
void kp_hd_ui_cursor(KpApp *a, int drawn);
void kp_hd_ui_free(KpApp *a);
KpRect kp_world_view(const KpApp *a);

/* kp_app_flow.c: the flow's host over SDL (kp_flow.h has the flow) */
/* The flow over the loaded data: the profile, the interface, --mission's game or the glue's main menu. */
int kp_app_flow_init(KpApp *a, char *err, size_t err_size);
void kp_app_flow_frame(KpApp *a);
void kp_app_flow_free(KpApp *a);
double kp_app_now(void);

/* kp_app_input.c: the window's input for the interface */
void kp_input_event(KpApp *a, const SDL_Event *e);
void kp_input_focus(KpApp *a, int active);
/* The phones' soft keyboard follows the interface's focused edit control (a frame's end); a tap on it brings the
 * keyboard back after the user dismissed it. The desktops type with the hardware keyboard: no-op. */
void kp_input_text_sync(KpApp *a, int tapped);
/* A mouse event at the logical screen's point (the checks' input too). */
void kp_input_mouse(KpApp *a, int event, int32_t lx, int32_t ly);
/* The touch layer's host (once, before the first event). */
void kp_input_init(KpApp *a);
/* Each frame: the gestures' density and clock (a long press), a gesture cancelled when its screen changed. */
void kp_input_frame(KpApp *a);
/* A finger event (KP_TOUCH_DOWN ...) at the logical screen's point (the touch check's input too). */
void kp_input_finger(KpApp *a, int type, int64_t id, double lx, double ly);
/* The cursor a console frame updates (KP_HUD_*): the mouse's, a finger's aim, none. */
int kp_input_cursor(const KpApp *a);

/* kp_app_dev.c: the development checks (the autotest, play checks, screenshots, the frame benchmark) */
int kp_dev_init(KpApp *a);
void kp_dev_frame(KpApp *a);
void kp_dev_free(KpApp *a);
/* The frame benchmark's times of a frame (--bench-frames). */
void kp_dev_timing(KpApp *a, double flow_ms, double draw_ms);
/* A line of the checks' log ("SPIKE ..."). */
void kp_dev_line(const char *fmt, ...);

#endif
