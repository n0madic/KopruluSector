/* The interface layer kp_ui (plan phase 9, ADR 0010): the original's interface rules that read the game or draw its
 * generators, outside the game's own state. Its clock is passed in (the emulator's virtual ticks in the oracle, the
 * frame's milliseconds in the product); it never reads a wall clock.
 *
 * This header starts with the UI timer queue of timer.cpp (atlas P43 0x4c5840..0x4c5b70): the deadline-sorted
 * queue of 28-byte nodes keyed by (owner control, u16 ID), the fired queue a pass moves them to, and the search keys
 * 0x6cf7a0/0x6cf7a4 that SetUiTimer only stores when the active queue is not empty. */
#ifndef KP_UI_H
#define KP_UI_H

#include <stddef.h>
#include <stdint.h>

#include "kp_kpf.h"
#include "kp_ui_text.h"
#include "kp_smk.h"
#include "kp_rng.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KP_UI_TIMERS 512 /* nodes; the original allocates them (SMemAlloc), a full pool fails the set */
#define KP_UI_NONE (-1)

/* A timer node: +0x04 owner, +0x08 callback (here a flag: the layer above calls the owner's callback), +0x0c last
 * fire tick, +0x10 period, +0x14 ID, +0x18 smoothed lateness (written, never read). */
typedef struct {
	int32_t next;
	int64_t owner;
	int callback;
	uint32_t last, period;
	uint16_t id;
	int32_t lateness;
} KpUiTimer;

/* An optional trace of the queue's operations for the oracle (the input log's tset, tdel, tdelall and tpass). */
#define KP_UI_TIMER_TRACE 16384
enum { KP_UI_TRACE_SET = 1, KP_UI_TRACE_DEL, KP_UI_TRACE_DELALL, KP_UI_TRACE_FIRE, KP_UI_TRACE_PASS };
struct KpUiControl;
typedef struct {
	uint8_t kind;
	int64_t owner;
	uint16_t id;
	uint32_t period, tick;
	int callback;
	int32_t bin, record;              /* the owner's BIN and record when the record was made (-1 none) */
} KpUiTimerRecord;
typedef struct {
	KpUiTimerRecord rec[KP_UI_TIMER_TRACE];
	int count;
	const struct KpUiControl *controls; /* the owners' controls (KP_UI_CONTROLS) */
} KpUiTimerTrace;

typedef struct {
	KpUiTimer nodes[KP_UI_TIMERS];
	KpUiTimerTrace *trace;            /* NULL: no trace */
	const uint32_t *set_ticks;        /* the oracle's clock: the original's GetTickCount at each SetUiTimer, in order */
	int set_tick_count, set_tick_next;
	int32_t active, fired, free_list; /* 0x6cf798, 0x6cf79c; the free nodes */
	int64_t key_owner;                /* 0x6cf7a0 */
	uint16_t key_id;                  /* 0x6cf7a4 */
	int in_pass;                      /* RunUiTimers runs (a callback is under way) */
	int suspend;                      /* a callback entered a modal loop: the pass waits for its end */
	struct { int32_t next; uint32_t now; int fired; } suspended[8]; /* the passes a modal loop interrupted */
	int suspended_count;
} KpUiTimers;

/* A fired timer's work: the callback (callback != 0) or the owner's user event 8 with the ID; ctx is the caller's. */
typedef void (*KpUiTimerFn)(void *ctx, int64_t owner, uint16_t id, int callback, uint32_t period);

void kp_ui_timers_init(KpUiTimers *t);
/* SetUiTimer 0x4c5960 at tick now: reuses the node of (owner, id), else allocates one; restarts its period from now.
 * Returns -1 when the pool is full. */
int kp_ui_set_timer(KpUiTimers *t, int64_t owner, uint16_t id, uint32_t period, int callback, uint32_t now);
/* DeleteUiTimer 0x4c58f0: removes the first node of (owner, id), active queue first. */
void kp_ui_delete_timer(KpUiTimers *t, int64_t owner, uint16_t id);
/* DeleteTimersOfOwner 0x4c5840: every node of the owner. */
void kp_ui_delete_owner_timers(KpUiTimers *t, int64_t owner);
/* RunUiTimers 0x4c5a20 at tick now: fires the due timers in order through fn, then sorts the fired ones back.
 * Returns the number fired. */
int kp_ui_run_timers(KpUiTimers *t, uint32_t now, KpUiTimerFn fn, void *ctx);
/* The pass a modal loop interrupted (a callback set t->suspend) goes on once the loop ended: the rest of its due
 * timers at its tick, then the fired ones sorted back. Returns the number fired. */
int kp_ui_resume_timers(KpUiTimers *t, KpUiTimerFn fn, void *ctx);
/* The milliseconds left at tick now before the active timer of (owner, id) fires: 1 and *ms, 0 without one. */
int kp_ui_timer_remaining(const KpUiTimers *t, int64_t owner, uint16_t id, uint32_t now, uint32_t *ms);
/* The active queue's nodes in order (for tests and saves): up to max node indexes into out; returns the count. */
int kp_ui_timer_order(const KpUiTimers *t, int32_t *out, int max);

/* --- the dialog framework (dlg.cpp, ctrl.cpp; atlas P42, P43) --- */

#define KP_UI_CONTROLS 2048
#define KP_UI_WIDGET_SIZE 60 /* a BIN widget record of ui.kpd DLGS */
#define KP_UI_FONTS 4      /* the font set's slots 0x6d3024..0x6d3030 */
#define KP_UI_VECTOR_FONTS 2 /* ui.kpd VFNT: the vector text's TrueType fonts (not the original's) */
#define KP_UI_FNT_FONTS 5  /* the FNT font files: font8, font10, font14, font16, font16x */
#define KP_UI_THEMES 16
#define KP_UI_EVENT_TYPES 19
#define KP_UI_SCREEN_W 640  /* the original's screen */
#define KP_UI_EDITS 4       /* the edit controls open at once */
#define KP_UI_EDIT_SIZE 256
#define KP_UI_SCREEN_H 480
#define KP_UI_REGION 256     /* the invalid region's rectangles (0x6ce784; Storm's region gives up to 255) */

/* Control flags +0x18. */
#define KP_UI_DIRTY 0x1u
#define KP_UI_DISABLED 0x2u
#define KP_UI_OWNS_BIN 0x4u
#define KP_UI_VISIBLE 0x8u
#define KP_UI_EVENTS 0x10u
#define KP_UI_SHARED_BITMAP 0x20u
#define KP_UI_HOVER 0x80u
#define KP_UI_FOCUS_LINE 0x1000u
#define KP_UI_TRANSPARENT 0x2000u
#define KP_UI_NO_SPACING 0x10000u
#define KP_UI_HOTKEY_PREFIX 0x200u   /* the text's first byte is the hotkey */
#define KP_UI_PLAIN 0x20000u
#define KP_UI_TRANSLUCENT 0x40000u
#define KP_UI_INITIALIZING 0x04000000u
#define KP_UI_PRESSED 0x40000000u
#define KP_UI_CONSUMED 0x80000000u

#define KP_UI_MOUSE_BUTTONS 0x2a /* the mouse state word 0x6d315c's buttons: left 2, right 8, middle 0x20 */

/* Event types (the window procedure 0x43f600, atlas P13) and user codes (type 14). */
enum {
	KP_UI_KEY_DOWN, KP_UI_KEY_REPEAT, KP_UI_KEY_UP, KP_UI_MOUSE_MOVE, KP_UI_LEFT_DOWN, KP_UI_LEFT_UP, KP_UI_LEFT_DOUBLE,
	KP_UI_RIGHT_DOWN, KP_UI_RIGHT_UP, KP_UI_RIGHT_DOUBLE, KP_UI_MIDDLE_DOWN, KP_UI_MIDDLE_UP, KP_UI_MIDDLE_DOUBLE,
	KP_UI_IDLE, KP_UI_USER, KP_UI_CHAR, KP_UI_COMMAND, KP_UI_WHEEL_UP, KP_UI_WHEEL_DOWN
};
enum {
	KP_UI_CREATE, KP_UI_DESTROY, KP_UI_CLOSE, KP_UI_ACTIVATE, KP_UI_HIT_TEST, KP_UI_FOCUS, KP_UI_FOCUS_LOST,
	KP_UI_RECENTRE, KP_UI_TIMER, KP_UI_MOUSE_ENTER, KP_UI_POST_LOAD, KP_UI_VALUE_CHANGED, KP_UI_CODE_12, KP_UI_SHOW,
	KP_UI_HIDE
};

/* The console's state (status.cpp, statres.cpp; atlas P36, P37). */
#define KP_UI_SLOTS 12
/* The console's dialogs in CreateStatusUi 0x4a7d80's order. */
enum { KP_UI_HUD_PORTRAIT, KP_UI_HUD_LEADERBOARD, KP_UI_HUD_RESOURCES, KP_UI_HUD_STATUS, KP_UI_HUD_CARD, KP_UI_HUD_MENU,
	KP_UI_HUD_TEXTBOX, KP_UI_HUD_MINIMAP, KP_UI_HUD_DIALOGS };
typedef struct {
	int32_t resources;               /* 0x6ab3f4: the resource dialog (statres.bin) */
	int32_t shown_minerals, shown_gas; /* 0x6ab3f8, 0x6ab3fc: the animated values shown */
	char resource_text[8][32];       /* 0x6ab400 + 32 * control index */
	int64_t selection[KP_UI_SLOTS];  /* 0x6ab4c8: the selection's snapshot */
	int selection_count;             /* 0x6ab4c0 */
	int64_t status_unit;             /* 0x6ab4c4: the status panel's leader */
	int rebuild;                     /* 0x6ab4fc: rebuild the snapshot next frame */
	int status_refresh, portrait_refresh, card_refresh; /* 0x6aab30, 0x6ab3a0, 0x6aaafc */
	int64_t selection_serial, refresh_serial, load_serial; /* the game's counters as last seen (kp_ui_hud_frame) */
	uint32_t create_tick[KP_UI_HUD_DIALOGS]; /* the tick each console dialog is created at (0: ui->now), for the oracle */
	int32_t leaderboard;             /* 0x6ab270: the leaderboard dialog (statlb.bin) */
	int32_t textbox;                 /* 0x6be65c: the text box (<race>textbox.bin) */
	uint8_t textbox_mode;            /* 0x6be658: 0 closed, 1 a single player's message line */
	uint8_t lb_state, lb_expanded;   /* 0x6ab274 (0 hidden, 1 values, 2 greed), 0x6ab278 */
	char lb_values[8][16];           /* 0x6ab280: a row's value (greed: its minerals) */
	char lb_gas[8][16];              /* 0x6ab300: a greed row's gas */
} KpUiHud;

/* A movie as SVid plays it (storm.dll SVidPlayBegin 454, SVidPlayContinueSingle 457, SVidPlayEnd 458 over Smackw32):
 * decoded into the caller's bitmap, its frames timed by SmackWait (10 microsecond units of timeGetTime). */
typedef struct {
	int open;
	KpSmk smk;
	uint32_t next, period;           /* the Smack record's +0x420 (0xffffffff before the first wait), +0x424 */
	int ready;                       /* +0x41c: the frame is due */
	int loop;                        /* SVidPlayBegin's flag 0x40000: the last frame starts the movie again */
	uint8_t *zoom;                   /* flag 0x200 without full lines (bit 1): the caller's bitmap of twice the movie's
	                                  * size, each frame's rows doubled across into its even rows (scanline mode) */
	uint8_t *frame;                  /* the movie's own frame when zoomed */
	const char *path;                /* the HD interface (kp_ui_video_image): the movie's file and the frame shown
	                                  * last (-1 none), kept after its end (a still portrait) */
	size_t path_len;
	int32_t shown;
} KpUiVideo;

/* The console's portrait (statport.cpp; atlas P36 0x4a65d0..0x4a7420). */
typedef struct {
	int32_t dialog, control;         /* 0x6ab398, 0x6ab39c */
	KpUiVideo video;                 /* 0x6ab390 */
	int first;                       /* 0x6ab394 */
	uint8_t fade;                    /* 0x6ab3a1 */
	int fresh, talking, trigger;     /* 0x6ab38c, 0x6ab3cc, 0x6ab3d0 */
	uint8_t previous_talking;        /* 0x6ab3ec */
	uint32_t talk_sound;             /* 0x6ab3e8 */
	int release;                     /* a trigger portrait's time is over: the local player's trigger wait ends
	                                  * (0x4a7300 -> 0x4be3a0), which the caller does (kp_ui_take_release) */
	uint32_t static_draws;           /* the fade-in static's draws so far (for the oracle's diagnostics) */
	uint32_t mode;                   /* 0x50d7c8: 0 none, 1 still, 2 animated */
	KpRngPortrait rng;               /* 0x50d7cc */
	KpUiSurface bitmap, overlay;     /* 0x6ab3a4 (the movie's), 0x6ab3ac (game\<race>conover.pcx) */
	const uint8_t *hotkeys;          /* 0x6ab3e4 */
	int64_t camera_x, camera_y;      /* 0x527014, 0x527018: the last transmission's screen for the original's view (a
	                                  * talk with a ping), the hotkey's (kp_ui_view_target) */
} KpUiPortrait;

/* The console's minimap (minimap.cpp; atlas P24). */
#define KP_UI_PINGS 16
typedef struct {
	int active;                      /* the 16-byte records 0x671e98: +0 active, +4 x, +8 y, +0xc frame, +0xe colour */
	int32_t x, y;
	uint16_t frame;
	uint8_t colour;
} KpUiPing;

typedef struct {
	int32_t dialog, control;         /* 0x671a8c, 0x671f98 (the image) */
	int32_t w, h;                    /* 0x671fb8, 0x671fbc */
	uint32_t divisor, shift;         /* 0x671fa8, 0x671fb0 */
	uint8_t *terrain, *image;        /* 0x671fcc, 0x671fd4 */
	uint8_t palette_map[256];        /* 0x672078 */
	int terrain_hidden;              /* 0x671a89 */
	int colour_mode;                 /* 0x671a88 */
	int32_t hovered;                 /* 0x671f9c: the dialog's control under the cursor (-1 none) */
	int rebuild;                     /* 0x671fa2 */
	int64_t terrain_serial;          /* the game's minimap_terrain as last seen */
	int blink_phase, blinked;        /* 0x671a8a, 0x671a8b */
	uint16_t view_x, view_y;         /* 0x671fa0, 0x671fa4: the screen's tile last drawn */
	int16_t clip[4];                 /* 0x508bb0 */
	KpUiPing pings[KP_UI_PINGS];
	const uint8_t *mmni, *dark;      /* tileset.kpd MMNI; the tileset's dark.pcx (32 x 256) */
	size_t mmni_size;
	uint8_t player_colours[16];      /* 0x5290b6: game\tminimap.pcx after the map's colour slots */
	uint8_t colour_table[12];        /* 0x536722: the colour slot a moved row took (SetScoreColorIndex 0x461b70) */
	uint8_t blink_rows[19 * 8];      /* 0x671fe0: game\tblink.pcx */
} KpUiMinimap;

/* The on-screen text lines (atlas P23 0x46a760..0x46b070): the 11-line message ring (0..10), line 11 (420, 24) and
 * the error line (12), drawn on the game screen's layer, not in a dialog. */
#define KP_UI_LINES 13
#define KP_UI_LINE_SIZE 218
typedef struct {
	uint8_t text[KP_UI_LINES][KP_UI_LINE_SIZE]; /* 0x66ff00 */
	uint32_t expiry[KP_UI_LINES];               /* 0x670a14 */
	uint8_t colour[KP_UI_LINES];                /* 0x670a48 */
	uint8_t flag[KP_UI_LINES];                  /* 0x670a58: 1 a trigger's text */
	uint8_t next;                               /* 0x670a12: the ring's next slot, its oldest line */
	uint16_t height, spacing;                   /* 0x670a6c, 0x670a68 */
	KpUiSurface layer;                          /* the lines over a transparent 640 x 480 (index 0) */
	int changed;                                /* the layer is out of date */
} KpUiLines;

/* The sound effects (kp_ui_sound.c; snd.cpp, atlas P34, P35): the sound records 0x6a61d0, the DirectSound buffers (a
 * sound's cached one at its id, the temporary duplicates after them), the eight channels 0x6aa990, the calls the
 * oracle compares and the plays and stops for the caller's mixer. */
#define KP_UI_SFX 1144
#define KP_UI_BUFFERS (KP_UI_SFX + 32)
#define KP_UI_CHANNELS 8
#define KP_UI_PRELOAD 1024
#define KP_UI_SOUND_COMMANDS 64
#define KP_UI_SOUND_CALLS 64
#define KP_UI_PLAYED_EVENTS 8
#define KP_UI_EFFECTS_VOLUME 50       /* InitDefaultVolumes 0x49dfa0: the effects' and the music's percent */
#define KP_UI_MUSIC_VOLUME 25
enum { KP_UI_SOUND_REJECTED, KP_UI_SOUND_QUEUED, KP_UI_SOUND_STARTED };   /* PlaySoundInternal's paths */
enum { KP_UI_SOUND_PLAY = 1, KP_UI_SOUND_STOP, KP_UI_SOUND_STREAM = 6, KP_UI_SOUND_STREAMS_STOP, KP_UI_SOUND_STREAM_SFX,
	KP_UI_SOUND_CLICK };
/* (KP_UI_SOUND_STREAM: a trigger's WAV, its map string id in sound, PlayStreamedSoundFile 0x49e810; the streams'
 * stop: StopAllStreamedSounds 0x49db50; KP_UI_SOUND_STREAM_SFX: an sfxdata file streamed, its id in sound,
 * PlayStreamedSfx 0x49e7c0; KP_UI_SOUND_CLICK: the UI click's own buffer, PlayUiClickSound 0x49e460) */
typedef struct {
	uint8_t used, playing;
	uint16_t sound;
	uint32_t size, start;            /* the data's bytes; the play's start (ms) */
	int32_t volume, pan;             /* SetVolume (hundredths of a decibel), SetPan */
} KpUiBuffer;
typedef struct {
	int16_t buffer;                  /* +0 (-1 none) */
	uint16_t unit_type;              /* +4 (0xffff none) */
	uint8_t flags, priority, release; /* +6, +7, +8 */
	uint32_t id;                     /* +0xc */
	int32_t volume, pan;             /* +0x10 (the attenuation), +0x14 */
} KpUiChannel;
typedef struct {
	uint32_t id;
	int32_t attenuation, pan, unit_type; /* unit type -1: no unit */
	int8_t path, result;
} KpUiSoundCall;
typedef struct {
	uint8_t type;                    /* KP_UI_SOUND_PLAY, KP_UI_SOUND_STOP */
	int8_t channel;
	uint16_t sound;
	int32_t volume, pan;
} KpUiSoundCommand;
typedef struct {
	uint8_t device;                  /* 0x6aa954: a DirectSound device */
	uint32_t effects, music;         /* 0x6d3140, 0x6d313c (percent) */
	uint32_t effects_saved, music_saved; /* 0x6aaa50, 0x6aa98c: the volumes the toggles (40039, 40038) turned off */
	int32_t master;                  /* 0x6aa96c: subtracted from every buffer's volume */
	uint8_t flags[KP_UI_SFX];        /* 0x51d8c8: sfxdata's flags and the runtime bits 0x04, 0x08 */
	uint32_t size[KP_UI_SFX], last_start[KP_UI_SFX], duration[KP_UI_SFX]; /* records +0, +4 (as -now), +8 */
	int16_t buffer[KP_UI_SFX];       /* +0xc: the cached buffer, -1 none */
	KpUiBuffer buffers[KP_UI_BUFFERS];
	KpUiChannel channels[KP_UI_CHANNELS];
	uint32_t cache_total, cache_limit; /* 0x6aa95c, 0x5093c4 */
	uint16_t evict_cursor;           /* 0x6aaa58 */
	uint16_t preload[KP_UI_PRELOAD]; /* 0x6aaa60 */
	int preload_count;
	KpUiSoundCall calls[KP_UI_SOUND_CALLS];
	int call_count;
	KpUiSoundCommand commands[KP_UI_SOUND_COMMANDS];
	int command_count;
	/* gamemap.cpp's answers (kp_ui_voice.c) */
	uint32_t previous;               /* 0x66697c: the last random answer */
	int64_t select_unit;             /* 0x66726c */
	uint32_t annoyed;                /* 0x667270 */
	uint32_t cooldown[3];            /* 0x667274 (the selection's), 0x667278, 0x66727c (under attack) */
	uint32_t tick_counter;           /* 0x667284 */
	uint8_t quiet;                   /* 0x667280 (EnterQuietMode 0x446930) */
	uint8_t voices;                  /* the interface plays the game's and its own sounds (an older oracle log
	                                  * without sound records gives them, their CRT draws and talks itself) */
	struct { int64_t unit; uint32_t duration, sound; uint8_t ping, colour; } talks[KP_UI_SOUND_CALLS];
	int talk_count;                  /* the talking requests (RequestUnitTalking 0x4a7200) since the last take */
	int32_t call_type;               /* the unit's type at the game's call while its event plays (-1: the unit's own) */
	struct { int64_t kind, a, b, c; } played[KP_UI_PLAYED_EVENTS];
	int played_count;                /* the game's sound events the interface played inside its own call to the game,
	                                  * skipped when the host passes them on */
} KpUiSound;

/* The music (kp_ui_music.c; modeless.cpp, atlas P24): the track table 0x508c58 and the stream's state. */
#define KP_UI_MUSIC_TRACKS 32
enum { KP_UI_SOUND_MUSIC = 3, KP_UI_SOUND_MUSIC_VOLUME, KP_UI_SOUND_MUSIC_STOP }; /* sound commands of channel -1 */
typedef struct {
	uint8_t kind[KP_UI_MUSIC_TRACKS], fade[KP_UI_MUSIC_TRACKS], next[KP_UI_MUSIC_TRACKS];
	uint32_t length_ms[KP_UI_MUSIC_TRACKS];
	int track_count;
	uint16_t track;                  /* 0x672580 */
	uint8_t open, playing, quiet;    /* 0x67257c (a stream), 0x67258c, 0x67258d */
	uint8_t resumed;                 /* 0x6aaa5c: the volume code's music is on (ApplyVolumeOptions 0x49ea60) */
	int32_t volume, fading;          /* 0x672584 (the target), 0x672588 (the fade's) */
	uint8_t fade_active;             /* Win32 timer 3 */
	uint32_t fade_due, poll_due, start;
	int rotation;                    /* 0x503354: the skirmish games' rotation (-1 before the first) */
} KpUiMusic;

/* The tooltip box (kp_ui_tooltip.c; ctxthelp.cpp, atlas P06): its bitmap 0x5c9994 and screen layer 1. */
#define KP_UI_TOOLTIP_W 160
#define KP_UI_TOOLTIP_H 92
typedef struct {
	KpUiSurface bitmap;              /* 0x5c9994 */
	uint8_t pixels[KP_UI_TOOLTIP_W * KP_UI_TOOLTIP_H];
	uint8_t enabled, visible;        /* layer 1's enabled byte 0x6d0ba4; 0x5c999c */
	uint8_t changed;                 /* drawn or hidden since the caller last looked */
	int16_t x, y, w, h;              /* layer 1: 0x6d0ba6.. (640, 400 while hidden) */
	int16_t previous[4];             /* 0x6cf788: the rectangle PresentFrame invalidated last */
} KpUiTooltip;

/* The game options word 0x6d3148: the trigger texts shown without their "always" flag (subtitles; off in a fresh
 * profile, as the emulator's run has it). */
#define KP_UI_OPTION_SPEECH 0x001u      /* the sound options' check boxes: unit speech (the advisor's), acknowledgements, */
#define KP_UI_OPTION_ANSWERS 0x002u     /* building sounds */
#define KP_UI_OPTION_BUILDINGS 0x004u
#define KP_UI_OPTION_TIPS 0x100u        /* Show Tips at Startup */
#define KP_UI_OPTION_SUBTITLES 0x400u
#define KP_UI_OPTIONS_DEFAULT 7u        /* 0x4ce5a0's caller: 0x6d3148 |= 7, the three sound options on */
#define KP_UI_VIEW_W 640                /* the game view 0x666430 = {0, 0, 639, 399} */
#define KP_UI_VIEW_H 400
#define KP_UI_GAMMA_DEFAULT 100         /* LoadVideoSettings 0x4be790: "Gamma", "UnitPortraits" 2 */
#define KP_UI_GAMMA_MIN 60              /* GammaSliderProc 0x4be8d0 */
#define KP_UI_GAMMA_MAX 140
enum { KP_UI_PORTRAITS_NONE, KP_UI_PORTRAITS_STILL, KP_UI_PORTRAITS_ANIMATED }; /* 0x50d7c8 (the default animated) */
/* The video options' choices (our own dialog koprulu\video.bin, in place of the original's rez\video.bin; the colour
 * cycling it offered stays on): per radio group KP_VIDEO_AUTO (the art's or the scale's choice) or a value below. The
 * dialog changes them in ui->video_options, the host applies them (the world's art, the fog, the water, the text). */
#define KP_VIDEO_AUTO (-1)
enum { KP_GRAPHICS_ORIGINAL, KP_GRAPHICS_HD2, KP_GRAPHICS_HD }; /* the original's indexed art, Remastered's x2, x4 */
enum { KP_FOG_ORIGINAL, KP_FOG_SMOOTH };                        /* the fog's style */
enum { KP_WATER_STILL, KP_WATER_ANIMATED };                     /* the water masks' animation */
enum { KP_VTEXT_ORIGINAL, KP_VTEXT_VECTOR };                    /* the FNT fonts' bitmaps, or vector glyphs */
/* video_options.available: the choices the host can honour, the others disabled (an imported HD variant, which the
 * animated water needs too for its water masks; the vector text's TrueType fonts) */
#define KP_VIDEO_HAS_HD2 0x1u
#define KP_VIDEO_HAS_HD 0x2u
#define KP_VIDEO_HAS_VECTOR 0x4u
/* koprulu\video.bin's IDs (defs/ui/koprulu-dialogs.json): the gamma slider, then a radio group per choice whose first
 * button is Auto and each next one the group's next value (KP_VIDEO_CHOICE_ID); the portraits' group the modes from
 * none (KP_VIDEO_ID_PORTRAITS + KP_UI_PORTRAITS_*). A gap between the groups' IDs parts them (SelectOptionInGroup). */
#define KP_VIDEO_ID_GAMMA 1
#define KP_VIDEO_ID_GRAPHICS 10
#define KP_VIDEO_ID_FOG 20
#define KP_VIDEO_ID_WATER 30
#define KP_VIDEO_ID_FONT 40
#define KP_VIDEO_ID_PORTRAITS 50
#define KP_VIDEO_GROUP_IDS 10 /* the IDs a group spans */
#define KP_VIDEO_CHOICE_ID(group, value) ((int16_t)((group) + 1 + (value)))
typedef struct {
	int8_t graphics, fog, water, font; /* KP_VIDEO_AUTO or the group's value */
	uint8_t available;                 /* KP_VIDEO_HAS_* */
	uint32_t serial;                   /* bumped at each change the dialog makes (a preview, Cancel's revert) */
	uint32_t commits;                  /* bumped at OK: the host keeps the options (the profile) */
} KpUiVideoOptions;
/* The options block 0x6d3130's fields the options dialogs change (SnapshotOptions 0x47bd80): the game speed, the
 * scroll speeds, the music and effects volumes, the tip index, the option bits. */
struct KpUiOptionsBlock {
	int32_t speed;
	uint32_t mouse_scroll, key_scroll, music, effects, tip, bits;
};

/* The string tables of strings.kpd (GetTblString 0x4c13b0). */
enum { KP_UI_STAT_TXT, KP_UI_NETWORK, KP_UI_GLUALL, KP_UI_GLUHIST, KP_UI_TIPS, KP_UI_HELP, KP_UI_TBLS };

/* The 20-byte event record: user code, parameter, key, modifiers, type, cursor. */
typedef struct {
	uint32_t code, param;
	uint16_t key, mods, type;
	int16_t x, y;
} KpUiEvent;


typedef struct KpUi KpUi;

/* The accelerator tables (kp_ui_keys.c; LoadAccelerators 0x43f210): the game's (0x665e40: Local.dll 101 and the
 * executable's 113), the text entry's (0x665e44: 102) and the default (0x665e48: 103); ACCEL fVirt, key, cmd. */
#define KP_UI_ACCELS 256
enum { KP_UI_ACCEL_GAME, KP_UI_ACCEL_TEXT, KP_UI_ACCEL_DEFAULT, KP_UI_ACCEL_TABLES };
typedef struct {
	uint16_t fvirt[KP_UI_ACCELS], key[KP_UI_ACCELS], cmd[KP_UI_ACCELS];
	int count;
} KpUiAccelTable;
enum { KP_UI_HANDLER_GAME, KP_UI_HANDLER_DEFAULT }; /* the WM_COMMAND handlers 0x4487a0, 0x448f20 (0x665e30) */
typedef struct {
	KpUiAccelTable tables[KP_UI_ACCEL_TABLES];
	int active;                       /* 0x665de8 */
	int handler;                      /* 0x665e30 */
	int saved_active, saved_handler;  /* 0x665e4c, 0x665e50 (SaveAccelerators 0x43f180) */
} KpUiKeys;


/* The in-game input (kp_ui_input.c; atlas P13, P16, P25, P41): the input procedures, the drag box, the key bytes
 * and the mouse, the network selection's mirror and the commands the interface queues for the game. */
#define KP_UI_COMMAND_BYTES 4096
#define KP_UI_COMMANDS 128
typedef void (*KpUiProc)(KpUi *ui, KpUiEvent *e);
/* The map's scrolling (kp_ui_scroll.c; scroll.cpp, atlas P34): the keys' and the edge's acceleration, the Center View
 * trigger's camera pan and the saved screen positions. */
#define KP_UI_SCROLL_SPEED 3          /* the options' default scroll speeds (0..6) */
typedef struct {
	uint32_t step;                    /* 0x6a5eec: the acceleration, 0..6 */
	int32_t dx, dy;                   /* 0x6a5ee4, 0x6a5ee8: the last direction */
	uint32_t suppressed;              /* 0x6a5ecc: the keys scrolled; the edge waits for the cursor to move */
	int32_t cursor_x, cursor_y;       /* 0x503220, 0x503224: the cursor's position GetEdgeScrollCursorId last saw */
	uint8_t pan;                      /* 0x6a5ed4: the camera pan's lock */
	uint8_t pan_release;              /* 0x6a5ed8 = 0x4be3a0: the pan's arrival ends the trigger wait */
	uint8_t release;                  /* it arrived (or a new pan replaced it): kp_ui_take_release's */
	uint8_t warp_cursor;              /* EndCameraPan 0x49be50 moved the cursor to the view's centre (320, 200): the
	                                   * caller's (the system cursor) */
	int32_t pan_x, pan_y;             /* 0x6a5edc, 0x6a5ee0: for the layout's view (pan_target) */
	int32_t target_x, target_y;       /* the trigger's target for the original's 640 x 400 view */
	int16_t saved[3][2];              /* 0x526550: the screen positions of F2..F4 */
	int32_t middle_x, middle_y;       /* 0x667294, 0x667298: the middle button's drag anchor */
} KpUiScroll;
typedef struct {
	KpUiProc proc[KP_UI_EVENT_TYPES]; /* 0x665df0 */
	KpUiProc left, right;             /* 0x66728c, 0x667290: the cursor mode's click handlers */
	void (*reset)(KpUi *ui);          /* 0x665dec */
	int box;                          /* 0x6ca720: the drag box is active */
	int16_t anchor_x, anchor_y;       /* the drag's anchor (screen) */
	int16_t box_left, box_top, box_right, box_bottom;
	uint8_t double_click;             /* 0x6ca730 */
	uint8_t keys[3];                  /* 0x665e68: Shift, Ctrl, Alt held */
	uint8_t vk[256];                  /* 0x665e58: the key-down bytes of the keys the events name (the scroll keys).
	                                   * TODO: a key pressed while a mouse button is held reaches the
	                                   * bytes but no event (the window procedure 0x43f600); the log has no record */
	uint16_t mouse_state;             /* 0x6d315c: KP_UI_MOUSE_BUTTONS held, bit 0 moved */
	int32_t screen_x, screen_y;       /* 0x543218, 0x54321c: the game view's top-left in map pixels */
	KpUiScroll scroll;
	uint8_t targeting;                /* 0x6be650 */
	uint8_t target_orders[3];         /* 0x6be651..: on a unit, on the ground, on a fogged unit */
	uint16_t minimap_cursor;          /* 0x667288 */
	uint16_t placement_parent;        /* add-on mode (0x6751f8): the parent's type (0x6751f0[0]), else 0 */
	uint8_t double_group;             /* 0x67264c: the group of the last recall (0xff none), for the double tap */
	uint32_t double_tick;             /* 0x672650 */
	int8_t over_console;              /* -1: IsPointOverConsole 0x43e890 tests the original's console art (a point in
	                                   * the original's coordinates: on a panel); 0, 1: the layout's answer for a point
	                                   * on none of its panels (the console's dialogs see it outside them) */
	uint8_t camera_moved;             /* the interface moved the view (screen_x, screen_y; 0x40b450): the caller's */
	uint8_t reseed;                   /* a typed line reseeded the CRT generator with time(NULL) (0x40dee0): the
	                                   * caller's (kp_interface_crt_seed with its clock) */
	int64_t mirror[12];               /* 0x67261c: the selection the network last got */
	uint8_t commands[KP_UI_COMMAND_BYTES]; /* the queued commands (QueueGameCommand 0x485bd0), back to back */
	uint16_t command_size[KP_UI_COMMANDS];
	int command_count, command_first;
	size_t command_used;
} KpUiInput;
/* The building placement's preview (kp_ui_placebox.c; placebox.cpp, atlas P27): the bitmaps of the placed type (k 1)
 * and of an add-on's parent (k 0) with their ghosts and stippled cells, and screen layers 3 + k that show them. */
typedef struct {
	KpUiSurface bitmap[2];            /* 0x675200 + 8k (owned) */
	uint16_t type[2];                 /* 0x6751f0 */
	uint8_t cells[2][48];             /* 0x675188 + 48k: the validator's codes of the footprint's cells, rows of six */
	uint8_t drawn[2][48];             /* 0x675210 + 48k: the codes the cells were stippled with (0x0c: none yet) */
	int32_t x[2], y[2];               /* layers 3 + k: the bitmaps' places on the game view */
	uint8_t shown[2];                 /* the layers enabled */
	int64_t tile_x, tile_y;           /* 0x6751ec, 0x6751ee: the tile under the cursor */
	int64_t error;                    /* 0x6751fc: CheckPlacementAt 0x47db80 there */
} KpUiPlacement;
typedef struct KpUiControl KpUiControl;
struct KpGame;

/* A timer's callback (SetUiTimer's procedure, ECX = the owner); kp_ui_callback numbers them for the timer nodes. */
typedef void (*KpUiCallback)(KpUi *ui, int32_t owner);
#define KP_UI_CALLBACKS 32

/* A status control's record (lUser): the 12-byte one of InitStatusSlotRecord 0x4a2570 (+0 the GRP, +4 the frame, +6
 * the kind, +8 the ID), a selection box's 8-byte one (+0 the unit, +4 its type), a cargo box's or the wireframe's
 * unit. Units are game unit indexes (0 none). */
typedef struct {
	const uint8_t *grp;
	size_t grp_size;
	uint16_t frame, kind, id;
	int64_t unit;
} KpUiSlot;

/* Interact procedure +0x2a (fastcall control, event): nonzero = handled. Draw procedure +0x2e (control, x offset,
 * y offset, clip in dialog-local coordinates). */
typedef int (*KpUiInteract)(KpUi *ui, KpUiControl *c, KpUiEvent *e);
typedef void (*KpUiDraw)(KpUi *ui, KpUiControl *c, int32_t x, int32_t y, const int16_t clip[4]);

/* A control or dialog, the fields of its 86-byte BIN record as the original keeps them in place (atlas P19, P42).
 * Links are pool indexes (-1 none). A dialog's rectangle is on the screen; its children's are inside the dialog. */
struct KpUiControl {
	int32_t next;                 /* +0x00: the next sibling; for an open dialog the next one in the open list */
	int16_t left, top, right, bottom; /* +0x04 */
	uint16_t image_w, image_h;    /* +0x0c */
	uint8_t *image;               /* +0x10: the control's (or dialog's) image, e.g. the console art under it */
	const uint8_t *text;          /* +0x14 (NUL-terminated) */
	uint32_t flags;               /* +0x18 */
	uint16_t word1c, delay;       /* +0x1c, +0x1e */
	int16_t id;                   /* +0x20 */
	uint16_t type, wuser;         /* +0x22, +0x24 */
	uint32_t user;                /* +0x26 */
	KpUiInteract interact;        /* +0x2a */
	KpUiDraw draw;                /* +0x2e */
	int32_t parent;               /* +0x32 */
	uint16_t words36[4];          /* +0x36: a dialog's bitmap size; an option button's or check box's checked byte */
	int32_t active;               /* +0x3e: a dialog's focused control */
	int32_t child;                /* +0x42: a dialog's first child */
	int32_t hover;                /* +0x46 */
	uint16_t extra[4];            /* +0x46.. of a flic button */
	KpUiSurface bitmap;           /* a dialog's off-screen bitmap (+0x36), allocated by InitDialog */
	KpUiSlot slot;                /* a status control's record (lUser) */
	int owns_image;
	uint8_t console_capture;          /* its image a capture of the console art (kp_ui_capture_console) */
	/* identity for the oracle and the layout: the BIN's index in ui.kpd, the record index, the load number */
	int32_t bin, record, load;
	int used;
	int32_t clips;                /* a flic button's overlays (+0x42): the first of ui->flc, -1 none */
	int16_t glow, pan;            /* a flic button's +0x3e (0..100) and +0x40 */
	uint8_t text_format;          /* +0x4e */
	struct KpUiList *list;        /* a list box's items (type 12) */
	struct KpUiScrollBar *bar;    /* a scroll bar's or slider's state (types 6, 7) */
};

/* A dialog BIN of ui.kpd: its widget records and their count. */
typedef struct {
	const char *name;
	size_t name_len;
	const uint8_t *widgets, *smks;
	int widget_count, smk_count;
	int loads;
} KpUiBin;

typedef struct {
	const char *path;
	size_t path_len;
	const uint8_t *data;
	size_t size;
} KpUiFile;

/* A wireframe's parts as its GRP's indices (kp_ui_status.c's colour maps): four hit point parts, two shield parts and
 * the outline, whose colours the HD wireframe's layers take (StarCraft: Remastered's wirefram.dds.grp: a layer each,
 * the outline's the whole drawing); the outline's four shades follow (the HD outline takes the brightest). */
#define KP_UI_WIRE_PARTS 7
#define KP_UI_WIRE_OUTLINE 4
#define KP_UI_WIRE_COLOURS (KP_UI_WIRE_PARTS - 1 + KP_UI_WIRE_OUTLINE)
extern const uint8_t KP_UI_WIRE_INDEXES[KP_UI_WIRE_COLOURS];

/* The console's columns cut into three groups (kp_layout.c), as StarCraft: Remastered cuts its HD console: the
 * minimap, the status panel, the portrait with the menu button and the command card. */
#define KP_UI_CONSOLE_CUT_1 138
#define KP_UI_CONSOLE_CUT_2 408

#define KP_UI_TINTS 16 /* the indices a tinted image keeps (a colour map's slice: 16 entries) */
/* The HD interface's record of the images drawn on the interface's surfaces (not the original's: StarCraft:
 * Remastered's art, ADR 0013 R4d; kp_ui_images.c, kp_ui_set_hd): a frame of an SD interface file, its rectangle on the
 * surface and the pixels it wrote; a pixel is the image's while it holds what the image wrote. Blits carry them. */
typedef struct {
	const uint8_t *surface;       /* the surface's pixels (a key, never read) and size */
	int32_t surface_w, surface_h;
	const char *path;             /* the SD file (ui.kpd's path) and frame */
	size_t path_len;
	int32_t frame;
	int32_t x, y, w, h;           /* the frame's rectangle on the surface */
	int32_t item[4];              /* its item's rectangle on the surface (x, y, w, h): the frame's, or a picture's
	                               * whole (a console's part: the console's) */
	int32_t canvas[4];            /* a GRP frame's whole GRP rectangle on the surface (the frame's offset taken out) */
	uint8_t parts[KP_UI_WIRE_COLOURS]; /* a wireframe's parts' colours (the colour map's entries KP_UI_WIRE_INDEXES) */
	uint8_t *values;              /* w x h: what it wrote (written: mask) */
	uint8_t *mask;
	int tinted;                   /* drawn through a colour map */
	int tiled;                    /* its frame drawn on every cell of a grid of the frame's size over its rectangle,
	                               * from item (a dialog's background, kp_ui_image_tiles) */
	uint8_t tint_index[KP_UI_TINTS]; /* its most written indices and their counts (the tint's colour) */
	uint32_t tint_count[KP_UI_TINTS];
	uint32_t serial;              /* the order of the draws */
	int written;
} KpUiImage;

/* A picture the interface decoded from a file (a PCX), recognised as the source of a blit (kp_ui_picture). */
#define KP_UI_PICTURES 64
typedef struct {
	const uint8_t *pixels;
	int32_t w, h;
	uint64_t sample;              /* a sparse hash of its pixels: a buffer reused at its address is not it */
	const KpUiFile *file;
} KpUiPicture;

#define KP_UI_IMAGES_MAX 4096 /* the images the log keeps: past it the oldest are dropped */
typedef struct {
	KpUiImage *images;
	int32_t count, capacity;
	uint32_t serial;
	KpUiPicture pictures[KP_UI_PICTURES];
	int32_t picture_count;
	KpUiImage *open;              /* the image being drawn (kp_ui_image_begin) */
	uint16_t *owners;             /* a composed surface's owners (kp_ui_image_owners) */
	int32_t *writers;             /* and per pixel the image that wrote it (+1) */
	size_t owners_size;
	uint32_t dropped;             /* the images dropped for new ones with the log full (KP_UI_IMAGES_MAX) */
} KpUiImageLog;

/* The command card (kp_ui_card.c; statcmd.cpp, atlas P35 0x4a0620..0x4a1950): its dialog, the button set it shows
 * (the current, the forced and the merged one), the order it highlights, its tooltip's costs and its art. */
#define KP_UI_BUTTON_SETS 250
#define KP_UI_SET_NONE 228
#define KP_UI_REWRITTEN 256
typedef struct {
	int32_t dialog;                  /* 0x6aaae8: rez\statbtn<race>.bin */
	uint16_t set, forced, merged;    /* 0x6aaaf0, 0x6aaaf8, 0x6aaaf4 (228 none) */
	uint8_t order, suborder;         /* 0x6aaaf2, 0x6aaaf3: the status unit's orders the highlight was made for */
	uint16_t highlight;              /* 0x6aab00: the highlighted icon */
	int32_t hovered;                 /* 0x6aaaec */
	uint16_t costs[8], level;        /* 0x6aab04.., the tooltip's cost line; its upgrade level */
	const KpUiFile *buttons, *icons; /* unit\cmdbtns\<race>cmdbtns.grp, cmdicons.grp */
	uint8_t slices[96];              /* 0x6aaa88: unit\cmdbtns\ticon.pcx, six remaps of 16 */
} KpUiCard;

/* A theme (CreateTheme 0x4c2ca0): the GRP and its metric words. */
typedef struct {
	const uint8_t *grp;
	size_t grp_size;
	uint16_t option_size, check_size, tile_piece, bar_piece; /* 0x6ce6ec, 0x6ce6f0, 0x6ce75c, 0x6ce760 */
	uint16_t up_w, up_h, track_h;    /* 0x6ce700, 0x6ce6f8: frame 16 (the up arrow); 0x6ce704: frame 29's height */
	uint16_t row_w, row_h, wide_row_w; /* 0x6ce714, 0x6ce718: frame 53 (a list's tall row); 0x6ce730: frame 77's width */
	uint16_t left_w, left_h, piece_w; /* 0x6ce6fc, 0x6ce6f4: frame 22 (the left arrow); 0x6ce708: frame 32's width */
	uint16_t thumb_w, thumb_h;       /* 0x6ce70c, 0x6ce710: frame 28 (a scroll bar's thumb) */
	uint16_t slider_piece;           /* 0x6ce74c: frame 92's width (a slider's track piece) */
	uint16_t slider_thumb_w, slider_thumb_h, tick_w; /* 0x6ce750, 0x6ce754: frame 99; 0x6ce758: frame 97's width */
} KpUiTheme;
/* A scroll bar's or slider's state (types 6, 7; scrlbar.cpp): the change callback +0x36 with lUser, the position and
 * range +0x3a.., the action's step +0x40 and flag +0x42, the page step +0x46, the repeat countdown +0x47, a slider's
 * thumb frame +0x48 and tick step +0x49, the dragged thumb's offset +0x4a. */
typedef void (*KpUiScrollChanged)(struct KpUi *ui, uint32_t user, int32_t value);
typedef struct KpUiScrollBar {
	KpUiScrollChanged changed;
	int16_t pos, min, max, step;
	uint32_t action;
	uint8_t page, countdown, thumb, tick;
	int16_t drag;
} KpUiScrollBar;
/* A list box's items (lbox.cpp, atlas P43 0x4c5ff0..0x4c7320): the three arrays of InitListBox 0x4c68c0 and the
 * control's bytes +0x46.. (count, selection, page, last top, top, hover). */
#define KP_UI_LIST_CAPACITY 254
#define KP_UI_LIST_NONE 0xff
typedef struct KpUiList {
	const uint8_t *text[KP_UI_LIST_CAPACITY];
	uint8_t *owned[KP_UI_LIST_CAPACITY]; /* a text duplicated by flag 0x10 */
	uint8_t flags[KP_UI_LIST_CAPACITY];
	uint32_t data[KP_UI_LIST_CAPACITY];
	uint8_t count, capacity, selected, page, last_top, top, hover; /* +0x46, +0x47, +0x48, +0x4a, +0x4b, +0x4d, +0x4e */
	int16_t row_h;                    /* +0x50 */
	int32_t bar;                      /* +0x36: the vertical scroll bar (-1 none) */
	void (*draw_item)(KpUi *ui, KpUiControl *c, int index, const int16_t row[4], int32_t dx, int32_t dy); /* +0x52 */
} KpUiList;

/* The status panel (statdata.cpp, statwire.cpp; atlas P35..P37). */
typedef struct {
	int32_t panel;                   /* 0x6aab34: statdata.bin */
	uint8_t mode;                    /* 0x6aab31: the layout shown */
	int32_t hovered;                 /* 0x6aab38 */
	uint32_t hovered_user;           /* 0x6aab3c */
	int32_t wireframe;               /* 0x6ab528: the big wireframe control */
	int32_t hp[KP_UI_SLOTS];         /* 0x6aab68: the unit's HP ([0]), the multi-selection's; the cargo's from [1] */
	uint16_t energy;                 /* 0x6aab98: the energy's high byte */
	uint8_t progress;                /* 0x6aab9a: StatusNeedsProgressRefresh 0x4a5320 */
	uint16_t types[KP_UI_SLOTS];     /* 0x6aab9c: the multi-selection's types */
	int32_t shields;                 /* 0x6aabb4: shields >> 8 */
	uint16_t cargo[8];               /* 0x6aabb8: the cargo's unit references */
	uint32_t secondary;              /* 0x6aabc8: the hangar count, supply, resource amount or timer / 10 */
	uint16_t upgrades;               /* 0x6aabcc: the shown upgrade levels' sum */
	char slot_text[5][8];            /* 0x6aab40 (0x50d660) */
	char hp_text[32], energy_text[32]; /* 0x6ab0f4, 0x6ab114 */
	char lines[5][260];              /* 0x6aabd0, 0x6aacd8, 0x6aade0, 0x6aaee8, 0x6aaff0 */
	uint8_t ticon[96];               /* 0x6aaa88: unit\cmdbtns\ticon.pcx's first 96 pixels */
	uint8_t twire[24];               /* 0x6ab50c: game\twire.pcx's first 24 pixels */
	KpUiSurface bar_empty, bar_full; /* 0x6aab18, 0x6aab20: game\<race>pbrempt.pcx, pbrfull.pcx */
	const KpUiFile *cmdbtns, *cmdicons, *tranwire, *grpwire, *wirefram; /* 0x6aaa70, 0x6aaa74, 0x6aab28, 0x6aab2c,
	                                                                          0x6ab524 */
} KpUiStatus;

/* The blend tables of a palette (effects.cpp, kp_ui_effects.c): the glue's slots of weight 1, 2, 3, the game's slot 2
 * of weight 4 (StartDialogPaletteEffect 0x47bca0). */
#define KP_UI_EFFECT_SLOTS 3
#define KP_UI_EFFECT_ROW KP_UI_TEXT_ROWS /* DrawTextWithOpacity's row 0x6d06c8 */
enum { KP_UI_BLIT_COPY = 0, KP_UI_BLIT_KEYED = 1, KP_UI_BLIT_OPACITY = 3 };
typedef struct {
	uint8_t palette[1024];        /* 0x6cf7c0: the palette the tables were built for */
	uint8_t *tables[KP_UI_EFFECT_SLOTS];
	int built;                    /* a palette was given */
	uint8_t slot_built[KP_UI_EFFECT_SLOTS]; /* 0x6cfbc0 */
	int level[2];                 /* the single-colour sets A (0x6cfbc3) and B (0x6cfd24): their levels */
	uint8_t colour[2];            /* 0x50ef88, 0x50ef90: the colour each blends */
	uint8_t image_percent, combo_percent, dialog_percent; /* SetDialogEffectColors 0x4c4d50: 0x6ce77c, 0x6ce780 */
} KpUiEffects;

#define KP_UI_RAND_LOG 4096
/* The score screen (gluscore.cpp, atlas P21 0x461940..0x463350): what it reads of the finished game (the roster
 * CaptureScoreRoster 0x461e00 keeps, the score arrays, the colour tables, the results and the elapsed seconds), its
 * rows and its tab. */
#define KP_UI_SCORE_SLOTS 8
enum { KP_UI_SCORE_MINERALS, KP_UI_SCORE_GAS, KP_UI_SCORE_CUMULATIVE_GAS, KP_UI_SCORE_CUMULATIVE_MINERALS,
	KP_UI_SCORE_MEN_PRODUCED, KP_UI_SCORE_MEN_LOST, KP_UI_SCORE_MEN_KILLED, KP_UI_SCORE_UNIT_SCORE,
	KP_UI_SCORE_KILL_SCORE, KP_UI_SCORE_BUILDINGS_CONSTRUCTED, KP_UI_SCORE_BUILDINGS_LOST, KP_UI_SCORE_BUILDINGS_RAZED,
	KP_UI_SCORE_STRUCTURE_SCORE, KP_UI_SCORE_RAZING_SCORE, KP_UI_SCORE_ARRAYS };
typedef struct {
	char name[25], force[30];        /* +0x00, +0x19 */
	uint32_t index, storm, type;     /* +0x38 the score index, +0x3c the Storm player, +0x40 the slot type (0 none) */
	uint8_t race;                    /* +0x44 */
} KpUiRosterEntry;
typedef struct {
	KpUiRosterEntry roster[KP_UI_SCORE_SLOTS]; /* 0x66c710, by slot */
	int count;                       /* 0x66ca6c: the listed entries */
	int32_t arrays[KP_UI_SCORE_ARRAYS][12];
	uint8_t colours[12], player_colours[8]; /* 0x536722, 0x5264fc */
	int result;                      /* ComputeGameResults 0x477270's local result: 1 victory, 2 defeat, 3 draw */
	int race;                        /* the local player's race (0 zerg, 1 terran, 2 protoss) */
	uint32_t local_storm, elapsed;   /* 0x508d58; GetElapsedSeconds 0x47a780 */
	int replay;                      /* 0x66b414 */
} KpUiScoreData;
typedef struct {
	int valid, roster;               /* +0x14, +0x18 */
	int32_t value[4], counter[4], target[4], step[4]; /* the three columns, then the total */
	char text[4][16];
} KpUiScoreRow;
typedef struct {
	KpUiScoreData data;
	KpUiScoreRow rows[KP_UI_SCORE_SLOTS]; /* 0x66b430 */
	int32_t dialog, maxima[3];       /* 0x66b41c, 0x66b424 */
	int tab, index, theme;           /* 0x66b420; the score directory's index (race * 2 + victory) and its theme */
	const KpUiFile *icons;           /* 0x66c954: <dir>iScore.grp */
	uint8_t minimap[12];             /* 0x66ca5c: the first pixels of <dir>tminimap.pcx */
	KpUiSurface main, inset, box, untab; /* <dir>pMain.pcx, pInset.pcx, scorebox.pcx, untab.pcx */
	char elapsed_text[64];
	int answered;                    /* the dialog's result is in ui->result */
} KpUiScore;

/* The glue's loop and its menus (kp_ui_menus.c; glues.cpp 0x457510, glumain.cpp, glulogin.cpp, glupopup.cpp, the
 * campaign menus, the load screen; atlas P18..P21, P41): the screens' modal runs with their continuations, the
 * profiles and saved games the host lists, and the work a screen leaves the host (a mission, a cinematic, the exit). */
#define KP_UI_PROFILES 250
#define KP_UI_SAVES 254
#define KP_UI_MODALS 8
#define KP_UI_NAME 25
typedef void (*KpUiModalDone)(KpUi *ui, int32_t result);
typedef struct {
	char name[KP_UI_NAME];
	uint32_t time;                   /* the record's +0x10: the list's order */
	uint32_t progress[6];            /* +0x28: the highest mission of original Zerg, Terran, Protoss, then expansion */
} KpUiProfile;
enum { KP_UI_MENU_NONE, KP_UI_MENU_STATE, KP_UI_MENU_EXIT, KP_UI_MENU_INTRO, KP_UI_MENU_CREDITS, KP_UI_MENU_MISSION,
	KP_UI_MENU_CINEMATIC, KP_UI_MENU_LOAD_SAVE };
typedef struct {
	int state;                       /* 0x66a274 */
	int action;                      /* what the ended screen leaves the host (KP_UI_MENU_*) */
	uint16_t mission, cinematic;     /* KP_UI_MENU_MISSION's, KP_UI_MENU_CINEMATIC's (0x526524, 0x50e0d0) */
	int32_t screen;                  /* the screen's dialog (-1 none) */
	struct { int32_t dialog; KpUiModalDone done; int32_t saved_result; int suspended; } modal[KP_UI_MODALS];
	int modal_count;
	int32_t popup;                   /* 0x66b0d8 */
	char popup_text[256], popup_text2[256], edit_text[256]; /* 0x66b0f8, 0x66b1f8, 0x66b2f8 */
	const uint8_t *edit_extra;       /* 0x66b400 */
	int edit_max;
	KpUiSurface popup_image;         /* 0x66b3f8: the popup's background */
	KpUiProfile profiles[KP_UI_PROFILES]; /* 0x5081a0, in the list's order */
	int profile_count;
	int profiles_changed;            /* a profile was created or deleted: the host saves the list */
	char player[KP_UI_NAME];         /* 0x672690 */
	int expansion, multiplayer;      /* 0x536720, 0x672664 */
	char saves[KP_UI_SAVES][KP_UI_NAME + 8]; /* the saved games' names (glue\..\save) */
	int save_count;
	int32_t save_choice;             /* 0x66ad98: the save to load */
	const uint8_t *campaign;         /* campaign.kpd TABL (the bytes outlive the interface) */
	size_t campaign_size;
	int hints[3];                    /* 0x668a2c / 0x66a8a4: Zerg, Terran, Protoss */
	int race;                        /* the episode StartCampaignEpisode started */
	int32_t history_progress, history_row; /* 0x66b0ec, 0x66b0e4 */
	int history_table;
	char version[32];                /* 0x508620 */
	uint32_t time_seed;              /* time(NULL) for the CRT's reseeds (the host's clock; the oracle's from its log) */
	const uint32_t *time_seeds;      /* the oracle's: the value of each reseeding burst in order */
	int time_seed_count, time_seed_next;
	struct { int pending, race, expansion; uint16_t mission; } warp; /* 0x66a878, 0x66a880, 0x66a884, 0x66a87c */
} KpUiMenus;

/* A flic button's overlay (flc.cpp): a node of the BIN's SMK list with its clip. */
#define KP_UI_FLC_CLIPS 64
#define KP_UI_VIDEO_TRACE 256
#define KP_UI_VIDEO_NAME 80
#define KP_UI_FLC_LOG 8192
typedef struct {
	int used;
	int32_t next;
	uint32_t flags;               /* +0x04 */
	uint16_t level;               /* +0x08: the fade level 0..100 */
	const char *name;             /* +0x0a */
	uint16_t x, y, w, h;          /* +0x12, +0x14, +0x16 */
	uint8_t *pixels;              /* +0x1a */
	KpUiVideo video;              /* +0x0e */
	int order;                    /* for the oracle: the clips numbered in their first continuation's order, -1 */
} KpUiFlcClip;

/* The glue's screens (glues.cpp, kp_ui_glue.c). */
#define KP_UI_SLIDES 16
#define KP_UI_CONTROLS_HIDDEN 64
typedef struct {
	int theme;                    /* 0x503e88: the theme on screen, -1 none */
	int dialog_theme;
	uint8_t palette[1024];        /* 0x666568 (R, G, B, 0) */
	uint32_t palette_serial;      /* bumped when the host should upload the palette (and fade in) */
	KpUiSurface background;       /* the cached background */
	uint8_t background_palette[1024];
	int background_theme;
	int nested;                   /* 0x66a284 */
	int32_t dialog;               /* 0x66a318 */
	struct { int16_t id; uint8_t dir; } desc[KP_UI_SLIDES]; /* 0x66a288: the slides */
	int desc_count;               /* 0x66a28c */
	int32_t slide_control[KP_UI_SLIDES];
	uint16_t slide_distance[KP_UI_SLIDES]; /* 0x66a2e0 */
	uint8_t slide_active[KP_UI_SLIDES];
	int slide_delay;              /* 0x66a30c: 20 ms ticks */
	int closing;                  /* 0x66a308 */
	int32_t hidden_from;          /* 0x66a310 */
	int32_t hidden[KP_UI_CONTROLS_HIDDEN]; /* 0x66a314: the controls the slide-in hid */
	int hidden_count;
	uint8_t slide_sound;          /* 0x5080c8 */
	uint32_t last_over;           /* 0x66a888 */
	int sounds_loaded;            /* 0x66a8a0 */
	KpRngCrt crt;                 /* the process's CRT generator before a game's seed (the menus' draws) */
	uint32_t rand_log[KP_UI_RAND_LOG]; /* for the oracle (ui->video_trace): its state before each draw */
	int rand_count;
} KpUiGlue;

/* A ready room's portrait slot (glubrfport.cpp: the 20-byte state of controls 15-18). */
typedef struct {
	KpUiVideo video;              /* +0x08 */
	uint8_t flags;                /* +0x0c: bit 0 talking, bit 1 pending show */
	uint16_t portrait;            /* +0x0e */
	uint8_t previous;             /* +0x10 */
	uint8_t off_x, off_y;         /* +0x11, +0x12 */
	KpUiSurface bitmap;           /* 120 x 112 */
} KpUiBriefSlot;

/* The ready room and its briefing (glurdy.cpp, kp_ui_briefing.c). */
#define KP_UI_BRIEF_LOG 4096
enum { KP_UI_ROOM_NONE, KP_UI_ROOM_TEXT, KP_UI_ROOM_OPEN };
typedef struct {
	int race;                     /* 0 zerg, 1 terran, 2 protoss */
	int stage;                    /* KP_UI_ROOM_*: the establishing text, the room */
	int32_t dialog;               /* 0x66b40c */
	int64_t current;              /* the dialog's lUser: the current record, -1 none */
	uint8_t wait;                 /* 0x667bac */
	KpUiSurface text;             /* 0x66b404 */
	KpUiSurface frames[4], talking_frames[4]; /* 0x667bc4, 0x667be4 */
	KpUiBriefSlot slots[4];
	int polling, ready, sent;     /* the idle callback PollBriefingReady 0x461120, Start's command 0x54 queued, sent */
	uint32_t ready_tick;          /* 0x6737ec: the last turn step */
	int32_t result;
	struct { int32_t record; uint8_t index, type, result; uint32_t tick; } log[KP_UI_BRIEF_LOG]; /* for the oracle */
	int log_count;
} KpUiBriefing;

/* A credits screen (credits.cpp, kp_ui_credits.c): its script, position controls and pending background. */
#define KP_UI_CREDITS_TEXT 2048
typedef struct {
	int32_t dialog, current;      /* rez\credits.bin, the current position control 0x5aef20 (-1 none) */
	const char *text;             /* the script (ui.kpd GFIL rez\<name>.txt): 0x5aef0c.. */
	size_t size, at;
	char texts[7][KP_UI_CREDITS_TEXT]; /* the position controls' texts (SMemAlloc'd natively) */
	uint8_t fade_speed;           /* 0x5aef25 */
	uint32_t display_time;        /* 0x5aef28 */
	uint32_t deadline;            /* 0x5aef1c */
	int pending;                  /* 0x5aef24: a background waits for the next page */
	int aborted, done;            /* 0x5aef2c; the dialog closed */
	KpUiSurface background;
	uint8_t palette[1024];
} KpUiCredits;

typedef void (*KpUiSoundHook)(KpUi *ui, int event);

struct KpUi {
	KpKpf ui_kpf, fonts_kpf;
	const uint8_t *ui_data, *pool;
	size_t ui_size, pool_size;
	KpUiBin *bins;
	int bin_count;
	KpUiFile *grps, *pcx, *glue_files, *ghosts;
	int grp_count, pcx_count, glue_file_count; /* glue_files: ui.kpd GFIL, the glue's backgrounds, bitmaps and movies */
	int ghost_count;                 /* ui.kpd GHST: frame 0 of the buildings' GRPs, "ghost\\<images.dat id>" */
	uint8_t player_colours[12][8];   /* the players' colour rows for indices 8..15 (tunit.pcx; SetDrawColor 0x41d740) */
	const uint8_t *font_files[KP_UI_FNT_FONTS]; /* font8, font10, font14, font16, font16x */
	size_t font_file_sizes[KP_UI_FNT_FONTS];
	const uint8_t *game_rows;        /* tFontGam.pcx, 192 bytes */
	size_t game_rows_size;
	const uint8_t *fonts[KP_UI_FONTS];
	size_t font_sizes[KP_UI_FONTS];
	KpUiText text;
	KpUiGlyphLog glyph_log;       /* the vector text's record of the plotted glyphs (kp_ui_set_vector_text) */
	KpUiImageLog image_log;       /* the HD interface's record of the images drawn (kp_ui_set_hd) */
	int hd;                       /* the image log is on */
	int vector_text;              /* the canvas puts the vector font's glyphs in the live glyphs' place */
	uint8_t vector_codes[256];    /* the byte codes the vector font draws */
	uint8_t vector_font_ok[KP_UI_FNT_FONTS]; /* the FNT fonts with a cap height (the vector font fits glyphs to it) */
	const uint8_t *vector_fonts[KP_UI_VECTOR_FONTS]; /* ui.kpd VFNT: the TrueType files (the text's, font16x's) */
	size_t vector_font_sizes[KP_UI_VECTOR_FONTS];
	int vector_font_count;
	KpUiTheme theme;              /* the applied theme */
	KpUiControl controls[KP_UI_CONTROLS];
	int32_t free_control;
	int32_t open;                 /* 0x6ce6d8: the open dialogs, topmost first */
	int32_t pending;              /* 0x6ce6d4: dialogs waiting for destruction */
	int32_t capture[KP_UI_EVENT_TYPES]; /* 0x6ce668 */
	struct {
		int32_t control;              /* -1 none */
		uint32_t deadline;
	} press[4];                       /* the press slots (SchedulePressRelease 0x4c3570): a hotkey's press shown */
	struct {
		int32_t control;              /* -1 free */
		uint8_t text[KP_UI_EDIT_SIZE];
		uint8_t cursor, scroll;       /* +0x37 (the cursor, the scroll's first byte) */
		uint8_t mode;                 /* +0x36: 0x01 modified, 0x10 insert, the high nibble the colour style */
		uint8_t margin_x, margin_y;   /* +0x38, +0x39 */
		uint8_t typing;               /* +0x3c: the mouse cursor hidden while typing */
		uint16_t avail;               /* +0x3a */
	} edits[KP_UI_EDITS];             /* the edit controls' state (edit.cpp allocates the texts) */
	int32_t open_count;           /* 0x6ce658 */
	int abort;                    /* 0x6ce660: a dialog closed during the message's dispatch (kp_ui_window_key) */
	int32_t result;               /* 0x6ce6dc: a dialog's result (the OK and Cancel IDs) */
	int16_t mouse_x, mouse_y;     /* 0x6d3154, 0x6d3158 */
	uint32_t now;                 /* the tick of the current call (the clock is passed in) */
	uint8_t remap[256];           /* 0x501021: the colour map of the RLE blitter (identity, 8..15 the player colour) */
	uint8_t ui_colours[26];       /* 0x6d2368: the UI colours' nearest palette indexes (0x4cccb0) */
	KpUiSurface console;          /* 0x6ab504: the console art of the game's race (LoadConsoleBitmap 0x4a7e20) */
	const KpUiFile *console_file; /* its PCX in ui.kpd (the HD interface's console pieces) */
	int32_t menu_button;          /* 0x6ab380: the menu button dialog (stat_f10.bin) */
	int32_t menu_hovered;            /* 0x6ab384: the menu button under the cursor (-1 none) */
	KpUiTimers timers;
	int16_t region[KP_UI_REGION][4]; /* 0x6ce784: the invalid rectangles, multiples of 16, right/bottom exclusive */
	int region_count;
	KpUiCallback callbacks[KP_UI_CALLBACKS];
	int callback_count;
	struct KpGame *game;          /* the game the console reads (kp_ui_hud_create); never written */
	KpUiHud hud;
	KpUiStatus status;
	KpUiPortrait portrait;
	KpUiMinimap minimap;
	uint32_t options;                /* 0x6d3148: the game options word (KP_UI_OPTION_*) */
	uint32_t mouse_scroll_speed, key_scroll_speed; /* 0x6d3134, 0x6d3138: the options' scroll speeds, 0..6 */
	uint32_t tip_index;              /* 0x6d3144 */
	uint8_t tips_automatic;          /* 0x6c46e0: the tips opened at a mission's start (the mission's tip) */
	uint32_t gamma;                  /* GetGamma 0x4cf870 (60..140; the host applies it to the palette) */
	KpUiVideoOptions video_options;  /* the video options' choices (the host applies them) */
	/* The game view in pixels: the original's 640 x 400 (0x666430, the screen above the console); a wider layout's view
	 * keeps that height and widens (plan section 2a; kp_ui_set_view) */
	int32_t view_w, view_h;
	uint8_t cpu_throttle, cpu_throttle_saved; /* 0x6aaa64, 0x6aaa68 */
	struct KpUiOptionsBlock options_snapshot; /* the open options dialog's (+0x26) */
	uint8_t options_preview;         /* the snapshot's +0x26: idle events to the volumes' or the gamma's preview */
	struct { uint32_t gamma, portrait; KpUiVideoOptions options; } video_saved; /* 0x6ca798, 0x6ca79c; ours */
	uint16_t cursor;                 /* 0x665d74: the cursor's id (cursor\<name>.grp of the CURS table) */
	uint8_t cursor_theme;            /* the glue theme + 1 whose glue\<theme>\arrow.grp InitGlueDialog 0x457f00 set as the
	                                  * cursor's GRP (SetCursorGrp 0x43e5e0), 0 none: it stays until the id changes */
	uint32_t cursor_counter, cursor_next; /* 0x665d58, 0x665d5c: its animation counter and next tick */
	uint8_t cursor_hidden;           /* HideCursorLayer 0x43e590 / ShowCursorLayer 0x43e600: no cursor drawn */
	uint8_t cursor_locked;           /* 0x665d68 (IsCursorLocked 0x43e980): the cursor keeps its id and the map does
	                                  * not scroll (LockCursor, UnlockCursor 0x43ea30: the modeless dialogs) */
	uint8_t inactive;                /* !0x66fd24: the window is not the active application's (the map does not scroll) */
	uint8_t from_save;               /* 0x6a5e38 (WasLoadedFromSave 0x499870): the next console's game is a save's */
	KpUiLines lines;
	KpUiTooltip tooltip;
	KpUiSound sound;
	KpUiMusic music;
	const uint8_t *attenuation;      /* ui.kpd VOLT: 0x4efc00, an i32 per percent */
	int attenuation_count;
	KpUiKeys keys;
	int32_t modeless;                /* 0x672578: the modeless dialog over the game (-1 none) */
	KpUiScore score;
	KpUiMenus menus;
	uint8_t result_open, result_answered; /* 0x6ca73c, 0x6ca734: a result dialog shown, answered (the answer is
	                                       * forgotten at a new game's console, kp_ui_hud_create) */
	uint8_t result_kind;             /* the result it shows (KP_EVENT_GAME_RESULT's a) */
	uint8_t menu_depth;              /* 0x671a81: the in-game menus' nesting */
	struct {                         /* the in-game message boxes (okcancel.cpp) */
		char text[256], wait[256];   /* 0x675050, 0x675054 */
		void (*done)(struct KpUi *ui, int32_t result); /* 0x675060 */
		uint8_t open;
	} box;
	struct { uint8_t index; char name[32]; } save; /* 0x6a5e9e, 0x6a5e80: the save dialog's remembered row and name */
	struct { int kind; char name[32]; } save_request; /* a request of the save dialogs for the host's files */
	void (*menu_handler)(struct KpUi *ui, int32_t dialog); /* 0x675068: the open in-game menu's handler */
	uint32_t pause_tick;             /* 0x675180: the interface clock at the pause (PauseGame 0x47cbb0) */
	uint32_t paused_seconds;         /* the seconds UnpauseGame 0x47cc10 took off the game's elapsed clock (0x47a620;
	                                  * kp_ui_score_data), 0 at a new game's console */
	uint16_t game_exit;              /* 0x666420: RequestGameExit 0x444e00's code for the host (0 none; 2 Exit Program,
	                                  * 4 Quit Mission, 5 Restart, 6 the victory's, 7 the defeat's; 0 again at a new
	                                  * game's console) */
	KpUiInput input;
	KpUiPlacement placement;
	KpUiCard card;
	KpUiEffects effects;
	KpUiFlcClip flc[KP_UI_FLC_CLIPS];
	KpUiGlue glue;
	KpUiBriefing briefing;
	KpUiCredits credits;
	const uint8_t *establishing;     /* ui.kpd ESTB: 0x4ebba8's {name, mission key} and the finale 0x4ebda8 last */
	size_t establishing_size;
	KpUiSoundHook ui_sound;          /* 0x6ce664: the controls' sound hook (SetControlSoundHook 0x4c3750) */
	const uint8_t *theme_fonts;      /* ui.kpd TFNT: the themes' colour banks, 48 bytes each */
	const uint8_t *theme_effects;    /* ui.kpd TEFF: the themes' tEffect.pcx pixels, theme_effect_size each */
	size_t theme_effect_size;
	int video_trace;                 /* the videos begun, for the oracle (kp_ui_video_begin), missing files too */
	char videos[KP_UI_VIDEO_TRACE][KP_UI_VIDEO_NAME];
	int video_count;
	struct { int32_t clip, result, shown, frame; } flc_log[KP_UI_FLC_LOG]; /* AdvanceFlicButton's continuations */
	uint32_t page_log[KP_UI_VIDEO_TRACE];  /* ShowNextCreditsPage's ticks */
	int page_log_count;
	struct { int32_t id, result, shown, frame; } bpc_log[KP_UI_FLC_LOG]; /* the briefing portraits' continuations */
	int bpc_log_count;
	const uint32_t *video_ticks;     /* the oracle's clock of the glue's movie continuations (the original's
	                                  * timeGetTime at each), in order; NULL: ui->now */
	int video_tick_count, video_tick_next;
	const uint32_t *page_deadlines;  /* the oracle's credits deadlines (GetTickCount after a page's fades plus its time) */
	int page_deadline_count, page_deadline_next;
	int flc_log_count, flc_orders;
	KpKpf portraits_kpf;
	const uint8_t *portrait_rows;    /* portraits.kpd ROWS: per portdata row {idle first, mask, talking first, mask} */
	int portrait_row_count;
	KpUiFile *portrait_files;        /* portraits.kpd FILE, in SMKS order */
	int portrait_file_count;
	KpKpf sounds_kpf;
	const uint8_t *sounds;           /* sounds.kpd SFX records (22 bytes each, by sfx id) */
	int sound_count;
	const uint8_t *status_layouts;   /* STLY: per unit type {u32 needsRefresh, u32 draw} of 0x50ba50 */
	int status_layout_count;
	const uint8_t *cheats;           /* campaign.kpd CHEA (kp_ui_set_cheats) */
	size_t cheats_size;
	const uint8_t *button_sets;      /* BTNS (0x50ae98): per set u16 linked set, u16 count, the 20-byte buttons */
	uint32_t button_set_at[KP_UI_BUTTON_SETS]; /* each set's offset in button_sets */
	uint32_t rewritten[KP_UI_REWRITTEN];  /* PopulateCommandCard 0x4a1080: the records whose disabled string the
	                                       * requirement error 21 overwrote with 0x2fa (in the static table: for the
	                                       * process's life), as offsets in button_sets */
	int rewritten_count;
	int button_set_count;
	KpKpf strings_kpf;
	const uint8_t *tbl[KP_UI_TBLS];  /* strings.kpd's tables (kp_ui_set_strings) */
	size_t tbl_size[KP_UI_TBLS];
	char error[256];
};

/* A KpUi over ui.kpd and fonts.kpd (the bytes must outlive it); kp_ui_free releases it. NULL with err on failure. */
KpUi *kp_ui_new(const uint8_t *ui_kpd, size_t ui_size, const uint8_t *fonts_kpd, size_t fonts_size, char *err,
	size_t err_size);
void kp_ui_free(KpUi *ui);
/* The game's font set (LoadFontSetGame 0x465c40: font8, font10, font16, font16x) or the glue's (0x465ce0: font10,
 * font14, font16, font16x), and the text colour rows of tFontGam.pcx (game) or a theme's tFont bank. */
void kp_ui_font_set(KpUi *ui, int game);
/* CreateTheme/ApplyTheme 0x4c2ca0/0x4c31d0 with a GRP of ui.kpd (dlgs\<race>.grp, glue\<theme>\Dlg.grp). */
int kp_ui_apply_theme(KpUi *ui, const char *grp_path);
const KpUiFile *kp_ui_file(const KpUiFile *files, int count, const char *path);
/* A glue file of ui.kpd GFIL by its archive path (case-insensitive), NULL when absent. */
const KpUiFile *kp_ui_glue_file(KpUi *ui, const char *path);
/* LoadDialog 0x4c33a0: the root's index, or -1 (unknown BIN or a full pool). */
int32_t kp_ui_load_dialog(KpUi *ui, const char *bin);
/* InitDialog 0x4c2a90 with an interact procedure or NULL. */
void kp_ui_init_dialog(KpUi *ui, int32_t dialog, KpUiInteract interact);
/* DestroyDialog: the user code 1 and the release of the dialog's controls. */
void kp_ui_destroy_dialog(KpUi *ui, int32_t dialog);
/* SendUserEvent 0x4c2540. */
int kp_ui_send_user(KpUi *ui, int32_t control, uint32_t code, uint32_t param);
/* FindControlById 0x4c2200: a dialog's child with the ID, or -1. */
int32_t kp_ui_find_control(KpUi *ui, int32_t dialog, int16_t id);
/* Repaints a dialog into its bitmap, the dialog first and then its visible children (PaintDialogLayer 0x4c4d90 with
 * the whole dialog as the dirty region). */
void kp_ui_paint_dialog(KpUi *ui, int32_t dialog);
/* AddDirtyRect 0x4c5490: an inclusive screen rectangle into the invalid region (outward to multiples of 16). */
void kp_ui_add_dirty_rect(KpUi *ui, int32_t l, int32_t t, int32_t r, int32_t b);
/* The sound effects (kp_ui_sound.c): the state with or without a device (the sfxdata flags from the game, once it
 * is set: kp_ui_sound_flags); PlaySoundInternal 0x49e4e0 (unit: a game unit or 0) with PlaySound 0x49e740 and
 * PlaySoundAtVolume 0x49e780 (percent of the effects volume, through VOLT); the preload list 0x49e3f0 (NULL frees
 * it); StopAllSounds 0x49eca0; the plays and stops for the mixer and the calls made, since the last take. */
void kp_ui_sound_init(KpUi *ui, int device);
void kp_ui_sound_flags(KpUi *ui);
int kp_ui_play_sound_internal(KpUi *ui, uint32_t id, int32_t attenuation, int32_t pan, int64_t unit);
int kp_ui_play_sound(KpUi *ui, uint32_t id);
/* PlayUiClickSound 0x49e460: sound 15 from its own buffer (0x6a61cc, no channel's), which the host does not start
 * again while it sounds (GetStatus playing: nothing). */
void kp_ui_play_click(KpUi *ui);
int kp_ui_play_sound_at_volume(KpUi *ui, uint32_t id, int32_t percent, int32_t pan, int64_t unit);
int32_t kp_ui_attenuation(const KpUi *ui, int32_t percent);
void kp_ui_preload(KpUi *ui, const uint16_t *list, int count);
void kp_ui_stop_sounds(KpUi *ui);
/* A trigger's WAV (its map string) streamed, and the streams stopped. */
void kp_ui_stream(KpUi *ui, uint16_t string);
/* PlayStreamedSfx 0x49e7c0: sfxdata's file of id streamed as PlayStreamedSoundFile does. */
void kp_ui_stream_sfx(KpUi *ui, uint16_t id);
void kp_ui_stop_streams(KpUi *ui);
size_t kp_ui_take_sound_commands(KpUi *ui, KpUiSoundCommand *out, size_t capacity);
size_t kp_ui_take_sound_calls(KpUi *ui, KpUiSoundCall *out, size_t capacity);
/* The game's sounds and the units' answers (kp_ui_voice.c): PlaySoundAtUnit 0x446940 (enable: the option bits or 1;
 * returns whether it played), PlaySoundAtPosition 0x446a20, a talking request after a voice (its length from the
 * sound), PlayUnitYesSound 0x446120, SelectUnitsLocally 0x49cf80's voice (PlayUnitSelectSound 0x446c30 of the unit
 * SelectionSoundPrefers 0x49c140 picks) and TickSoundCooldowns 0x446ff0. */
int kp_ui_sound_at_unit(KpUi *ui, uint32_t sfx, int64_t unit, uint32_t enable, int32_t min_volume);
int kp_ui_sound_at_position(KpUi *ui, uint32_t sfx, int32_t x, int32_t y, uint32_t enable, int32_t min_volume);
void kp_ui_voice_talk(KpUi *ui, int64_t unit, uint32_t sfx, int ping, uint8_t colour);
void kp_ui_yes_sound(KpUi *ui, int64_t unit);
void kp_ui_selection_sound(KpUi *ui, const int64_t *units, int n);
void kp_ui_tick_sounds(KpUi *ui);
/* The advisor's voices and the errors with sounds: the advisor's playback (no option, no position), ShowErrorWithSound
 * 0x4461f0, ShowErrorWithRaceBuzz 0x446760, ShowWorkerPlacementError 0x446450 (string -1: PlayPlacementErrorVoice
 * 0x446570; base 0x9f and 0x8d) and PlayBuildingPlacedSound 0x446e90. */
int kp_ui_advisor_sound(KpUi *ui, uint32_t id);
void kp_ui_error_sound(KpUi *ui, int64_t player, uint32_t sound, uint32_t string);
void kp_ui_error_buzz(KpUi *ui, int64_t unit, uint32_t string, int64_t player);
void kp_ui_worker_voice(KpUi *ui, int64_t unit, uint32_t base, int32_t string);
void kp_ui_placed_sound(KpUi *ui, int64_t type);
/* InitSoundState 0x446800 at the game's start (the game set): the answers' state and the local race's preload list. */
void kp_ui_sound_start_game(KpUi *ui);
/* The game's sound events (KP_EVENT_SOUND, _SCREEN, _UNIT, _UNIT_ATTACKED, the quiet mode, the messages' sounds). */
void kp_ui_sound_event(KpUi *ui, int64_t kind, int64_t a, int64_t b, int64_t c);
/* 1 (and forgotten) when the interface played this game event already inside its own call to the game. */
int kp_ui_sound_played(KpUi *ui, int64_t kind, int64_t a, int64_t b, int64_t c);
/* The music (kp_ui_music.c): ui.kpd MUSI and sounds.kpd MUSL (kp_ui_new and kp_ui_set_sounds load them),
 * PlayMusicTrack 0x46ef10, StopMusicFadeOut 0x46f040, the quiet mode, SetMusicVolume 0x46f170 (hundredths of a
 * decibel), the fade-in timer and PollMusicEnd (kp_ui_music_tick at ui->now, every frame) and a game's first track. */
void kp_ui_music_tables(KpUi *ui, const uint8_t *musi, size_t size);
void kp_ui_music_lengths(KpUi *ui, const uint8_t *musl, size_t size);
void kp_ui_music_play(KpUi *ui, uint16_t track);
void kp_ui_music_stop(KpUi *ui);
void kp_ui_music_close(KpUi *ui);
/* ToggleLeaderboardExpanded 0x4a6260 (command 40053): the leaderboard's other rows (kp_ui_hud.c). */
void kp_ui_toggle_leaderboard(KpUi *ui);
void kp_ui_music_quiet(KpUi *ui, int quiet);
void kp_ui_music_volume(KpUi *ui, int32_t volume);
void kp_ui_music_tick(KpUi *ui);
void kp_ui_music_start_game(KpUi *ui);
/* VOLT's entry k (clamped), the attenuation of k percent of full volume. */
int32_t kp_ui_attenuation_at(const KpUi *ui, int32_t k);
/* The tooltip (kp_ui_tooltip.c): InitTooltip 0x419220, HideTooltip 0x4191d0, ShowTooltipText 0x418f60 (the box at
 * (x, y), kept on the screen), RenderCommandTooltip 0x4a0a30's frame (the text, the caller's extra lines growing its
 * extent {l, t, r, b}, the box's bottom left at (x, bottom_y), kept left of the screen's edge), PresentFrame
 * 0x4c5360's invalidation of the layer, and the box shown (NULL hidden). */
void kp_ui_tooltip_init(KpUi *ui);
void kp_ui_tooltip_hide(KpUi *ui);
void kp_ui_tooltip_text(KpUi *ui, int32_t x, int32_t y, const uint8_t *text);
void kp_ui_tooltip_over(KpUi *ui, int32_t x, int32_t bottom_y, const uint8_t *text,
	void (*extra)(KpUi *ui, int16_t rect[4]));
void kp_ui_tooltip_invalidate(KpUi *ui);
const KpUiSurface *kp_ui_tooltip(KpUi *ui, int32_t *x, int32_t *y, int32_t *w, int32_t *h);
/* The invalid region's rectangles as PaintDialogLayer 0x4c4d90 gets them from Storm (SRgnGetRectsf 529: the ORed
 * rectangles cut, merged once and sorted by bottom, then left), inclusive, at most KP_UI_REGION - 1. */
int kp_ui_region(const KpUi *ui, int16_t out[KP_UI_REGION][4]);
/* PresentFrame 0x4c5360's dialog layer (PaintDialogLayer 0x4c4d90): the invalid region (InvalidateControl 0x4c4ae0,
 * AddDirtyRect 0x4c5490) as banded rectangles; per rectangle, the visible dialogs from the bottom draw themselves and
 * their visible children that overlap it, clipped to it (CollectPaintTargets 0x4c4910, DrawControlClipped
 * 0x4c51c0); the region is then emptied. Returns the number of draw calls. */
int kp_ui_present(KpUi *ui);
/* The default procedures of a control type (the tables 0x4f10f8, 0x4f1148). */
KpUiInteract kp_ui_type_interact(int type);
KpUiDraw kp_ui_type_draw(int type);
/* The stock drawing helpers the panels' own draw procedures use (kp_ui_draw.c). */
void kp_ui_draw_theme_frame(KpUi *ui, int frame, int32_t x, int32_t y, const int16_t clip[4], uint32_t flags);
void kp_ui_draw_control_text(KpUi *ui, KpUiControl *c, int32_t x, int32_t y, const int16_t *clip, uint8_t format);
void kp_ui_update_focus(KpUi *ui, KpUiControl *c);
/* FocusControl 0x4c2730. */
void kp_ui_focus_control(KpUi *ui, int32_t c);
void kp_ui_blit_opaque(KpUi *ui, int32_t x, int32_t y, const uint8_t *pixels, int32_t w, int32_t h,
	const int16_t src[4]);

/* ResolveUiColours 0x4cccb0: the 26 UI colours (0x6d2368) as the nearest entries of a palette (768 bytes RGB) that are
 * not colour-cycled (cycled: 256 flags, 0x66fd20; NULL none). */
void kp_ui_resolve_colours(KpUi *ui, const uint8_t *rgb, const uint8_t *cycled);
/* The string tables of strings.kpd (the bytes must outlive the interface): 0 or -1. kp_ui_tbl: GetTblString
 * 0x4c13b0, id 0 -> NULL, an id past the count -> "", else the entry (NUL-terminated). */
int kp_ui_set_strings(KpUi *ui, const uint8_t *kpd, size_t size);
const uint8_t *kp_ui_tbl(const KpUi *ui, int table, uint32_t id);

/* ShowControl 0x4c2890, HideControl 0x4c22f0, SetControlVisible 0x4c2900, MarkControlDirty 0x4c2840. */
void kp_ui_show_control(KpUi *ui, int32_t control);
void kp_ui_hide_control(KpUi *ui, int32_t control);
void kp_ui_set_visible(KpUi *ui, int32_t control, int show);
void kp_ui_mark_dirty(KpUi *ui, int32_t control);
/* BindInteractProcedures 0x4c25b0: child ID k gets table[k - 1] (the original does not check the bound). */
void kp_ui_bind_interacts(KpUi *ui, int32_t dialog, const KpUiInteract *table, int count);
/* The timer callbacks: kp_ui_callback gives fn's number for kp_ui_set_timer (0 when the table is full); kp_ui_tick
 * runs the due timers at tick now (RunUiTimers 0x4c5a20): a callback with its owner, else the owner's user event 8
 * with the timer's ID. Returns the number fired. */
int kp_ui_callback(KpUi *ui, KpUiCallback fn);
int kp_ui_tick(KpUi *ui, uint32_t now);
/* Draws a frame of a raw-bitmap GRP (the dialog GRPs, icons.grp) at (x, y) of the target: BlitBitmapTransparent
 * 0x4cf530 (index 0 transparent) or BlitOpaque 0x4ce720 of the source rectangle src (NULL: the whole frame). Returns
 * the frame's size in w, h (GetGrpFrameBitmap 0x4cfda0), or -1 for a missing GRP. */
int kp_ui_grp_bitmap(const uint8_t *grp, size_t size, int frame, int32_t *w, int32_t *h, const uint8_t **pixels);
void kp_ui_blit_transparent(KpUi *ui, int32_t x, int32_t y, const uint8_t *pixels, int32_t w, int32_t h,
	const int16_t src[4]);
/* DrawGrpFrameRemapped 0x405e1e: an RLE frame of a unit-style GRP (cmdicons.grp, wirefram.grp, ...) at (x, y) of the
 * target, crop {left, top, width, height}, every opaque pixel through ui->remap (0x501021); a frame at or past the
 * count is frame 0 (GetGrpFrame 0x463bd0). Pixels outside the target are not written (the original does not clip). */
void kp_ui_draw_grp_rle(KpUi *ui, int32_t x, int32_t y, const uint8_t *grp, size_t size, int frame, const int32_t crop[4]);
/* GetGrpFrame 0x463bd0: the frame's width and height (frame 0 past the count); -1 for a missing GRP. */
int kp_ui_grp_frame_size(const uint8_t *grp, size_t size, int frame, int32_t *w, int32_t *h);
/* DrawThreePartBar 0x4c43e0 with the theme's GRP: the left cap, the middle pieces and the right cap of frames f, f + 1,
 * f + 2 at (x, y), the clip's right edge at its left + total. BlitBitmapClipped 0x4c3c70 of a bitmap from (sx, sy)
 * into the inclusive rectangle, the mode from the flags (0x2000 index 0 transparent, 0x40000 translucent). */
void kp_ui_three_part_bar(KpUi *ui, int32_t x, int32_t y, int32_t total, int32_t piece, int frame,
	const int16_t clip[4], uint32_t flags);
void kp_ui_blit_clipped(KpUi *ui, const uint8_t *pixels, int32_t w, int32_t h, int32_t sx, int32_t sy,
	const int16_t rect[4], uint32_t flags);
/* FillRect 0x4ce9c0 with colour 0x6d0c50 = colour (clipped to the target). */
void kp_ui_fill_rect(KpUi *ui, int32_t x, int32_t y, int32_t w, int32_t h, uint8_t colour);

/* --- the console (status.cpp, statmisc.cpp; atlas P36, P37) --- */

/* LoadConsoleBitmap 0x4a7e20: the console art of race 0 Zerg, 1 Terran, 2 Protoss, 3 a replay's. */
int kp_ui_load_console(KpUi *ui, int race);
/* CaptureConsoleBackground 0x4a7c40: the control's image = the console art under it, its size from +0x36/+0x38. */
void kp_ui_capture_console(KpUi *ui, int32_t control);
/* CreateMenuButtonDialog 0x4a6590 (stat_f10.bin with MenuDialogInteract 0x4a63e0): the dialog's index or -1. */
int32_t kp_ui_create_menu_button(KpUi *ui);
/* SetUserWordAndFlags 0x46c820: wUser from the tiled buttons' art table 0x508ba0[index], the flags ORed in. */
void kp_ui_set_user_word(KpUiControl *c, int index, uint32_t flags);
/* MenuButtonProc 0x46c7d0: an in-game menu's button (tiled art; its activation goes to the parent with its ID). */
int kp_ui_menu_button_proc(KpUi *ui, KpUiControl *c, KpUiEvent *e);
/* The in-game menus (kp_ui_gamemenu.c): the menu to open (OpenGameMenu 0x47b300, OpenHelpMenu 0x47b320,
 * OpenOptionsMenu 0x47b340, OpenQuitMissionMenu 0x47b380, OpenQuitMenu 0x47b360). */
enum { KP_UI_GAME_MENU, KP_UI_GAME_MENU_HELP, KP_UI_GAME_MENU_OPTIONS, KP_UI_GAME_MENU_QUIT_MISSION, KP_UI_GAME_MENU_QUIT,
	KP_UI_GAME_MENU_SAVE, KP_UI_GAME_MENU_LOAD };
void kp_ui_game_menu(KpUi *ui, int which);
/* The options dialogs (kp_ui_options.c): OpenSoundOptions 0x49ed20, OpenSpeedOptions 0x49f060, OpenVideoOptions
 * 0x4be5c0; ApplyVolumeOptions 0x49ea60. */
enum { KP_UI_OPTIONS_SPEED = 1, KP_UI_OPTIONS_SOUND, KP_UI_OPTIONS_VIDEO, KP_UI_OPTIONS_TIPS, KP_UI_OPTIONS_TIPS_AUTOMATIC };
void kp_ui_options_dialog(KpUi *ui, int which);
/* The save and load dialogs (kp_ui_saveload.c): OpenSaveGameDialog 0x49b0b0 / OpenLoadGameDialog 0x49af50 (load =
 * 1); the wait box of SaveGameFile 0x498d20 around the host's save; a save dialog's request for the host's files
 * (KP_UI_SAVE_DELETE: the name's file deleted). */
enum { KP_UI_SAVE_NONE, KP_UI_SAVE_DELETE };
void kp_ui_save_dialog(KpUi *ui, int load);
/* DeleteSaveFile 0x498b30: the host deletes the save's file, the list loses the name. */
void kp_ui_delete_save(KpUi *ui, const char *name);
void kp_ui_saving(KpUi *ui, const char *name);
/* IsInvalidFileName 0x408300: 1 when name (limit bytes at most) is not a file name the game takes: unterminated, a
 * control byte, a character of network.tbl string 17 (without / and \ when allow_path) after a drive prefix, an
 * empty name, or a DOS device name (0x5025b8) before its last dot. */
int kp_ui_invalid_file_name(KpUi *ui, const char *name, size_t limit, int allow_path);
/* MenuButtonActivateProc 0x46c7a0: the activation goes to the parent with the control's ID as the result. */
int kp_ui_menu_button_activate_proc(KpUi *ui, KpUiControl *c, KpUiEvent *e);
void kp_ui_apply_volumes(KpUi *ui);
/* SetPortraitMode 0x4a6e90: 0 none, 1 still, 2 animated (the shown portrait again). */
void kp_ui_portrait_set_mode(KpUi *ui, uint32_t mode);
/* PauseGame 0x47cbb0 / UnpauseGame 0x47cc10 of a single player's game (kp_core's KP_PAUSED, the trigger pause's). */
void kp_ui_pause_game(KpUi *ui);
void kp_ui_unpause_game(KpUi *ui, int discount);

/* --- the in-game console over a game (kp_ui_hud.c; plan phase 9B) --- */

/* CreateStatusUi 0x4a7d80 for the game's local player: the console art, the resource dialog, the menu button ...;
 * the game must outlive the interface (or kp_ui_hud_create(ui, NULL, NULL) detaches it). create_ticks (NULL, or
 * KP_UI_HUD_DIALOGS ticks, 0 for ui->now) give each dialog's creation tick, as the oracle's log has them. Returns 0
 * or -1. */
int kp_ui_hud_create(KpUi *ui, struct KpGame *game, const uint32_t *create_ticks);
/* FreeConsoleUi 0x4a7bc0: the console's dialogs destroyed, the game detached (EndGame's part). */
void kp_ui_hud_free(KpUi *ui);
/* Whether dialog d is one of the console's dialogs (FreeConsoleUi 0x4a7bc0's list). */
int kp_ui_is_console_dialog(KpUi *ui, int32_t d);
/* The console's work after a game frame (UpdateStatusUi 0x4a7ee0 and the selection's refreshes 0x4a7b60). */
void kp_ui_hud_frame(KpUi *ui);
/* The portrait (kp_ui_portrait.c): portraits.kpd (the bytes must outlive the interface; 0 or -1), LoadPortraitData
 * 0x4a6a40, UpdatePortraitForSelection 0x4a7420, and RequestUnitTalking 0x4a7200 (duration in ms, 0 with a sound id
 * below 1144: the sound's own length in whole seconds, TalkingDurationPending 0x4a7190; ping: the last event's screen
 * position, the unit's minimap blink in colour (a tblink row) and a ping at its sprite). */
int kp_ui_set_portraits(KpUi *ui, const uint8_t *kpd, size_t size);
int kp_ui_portrait_create(KpUi *ui);
void kp_ui_portrait_frame(KpUi *ui);
void kp_ui_portrait_talk(KpUi *ui, int64_t unit, uint32_t duration, int ping, uint8_t colour, uint32_t sound);
/* ShowTriggerPortrait 0x4a7320 (a unit type's portrait talking for duration ms; x -1 or the speaker's position for the
 * last event's screen), HideTriggerPortrait 0x4a73e0 (the Escape skip), and whether a trigger portrait's time ran out
 * since the last call, or the camera pan arrived (the wait is the caller's to release: kp_trigger_release_wait). */
void kp_ui_portrait_trigger(KpUi *ui, int64_t type, int32_t x, int32_t y, uint32_t duration);
void kp_ui_portrait_trigger_stop(KpUi *ui);
int kp_ui_take_release(KpUi *ui);
/* sounds.kpd (the bytes must outlive the interface; 0 or -1) and a sound's cached duration in ms (0x49da70:
 * u32(data length * 1000) / nAvgBytesPerSec; 0 for a sound the native parser rejected). */
int kp_ui_set_sounds(KpUi *ui, const uint8_t *kpd, size_t size);
uint32_t kp_ui_sound_duration(const KpUi *ui, uint32_t sound);
/* SVid over a movie of portraits.kpd (kp_ui_portrait.c): Begin (0, -1 for a missing file), ContinueSingle at now (ms;
 * returns 0 when the movie ended, *updated when a frame was decoded), End. */
int kp_ui_video_begin(KpUi *ui, KpUiVideo *v, const KpUiFile *file, uint8_t *target);
int kp_ui_video_continue(KpUiVideo *v, int force, int *updated, uint32_t now);
/* SVidPlayBegin with the 2x zoom of flag 0x200 in scanline mode into target (2w x 2h). */
int kp_ui_video_begin_zoomed(KpUi *ui, KpUiVideo *v, const KpUiFile *file, uint8_t *target);
/* The clock of a glue movie's continuation: the oracle's next scripted tick, else ui->now. */
uint32_t kp_ui_video_clock(KpUi *ui);
void kp_ui_video_end(KpUiVideo *v);
/* The HD interface (ADR 0013 R4e): surface s, the movie's bitmap, as a whole an image of the movie's frame shown
 * (its HD portrait's frame); nothing while the log is off or no frame was shown. */
void kp_ui_video_image(KpUi *ui, const KpUiVideo *v, KpUiSurface *s);
/* The minimap (kp_ui_minimap.c): its data (the bytes must outlive the interface: the tileset's MMNI, its palette
 * (768 RGB bytes) and cycled flags (256), dark.pcx (32 x 256), the map's colour slots (COLR, 8 bytes, or NULL:
 * ApplyColorSlotPermutation 0x41d5d0 over game\tminimap.pcx)), then InitMinimap 0x46e260 (0 or -1) and a ping
 * (0x46e8f0: map pixels, a tblink row). */
int kp_ui_set_minimap(KpUi *ui, const uint8_t *mmni, size_t mmni_size, const uint8_t *rgb, const uint8_t *cycled,
	const uint8_t *dark, const uint8_t *colour_slots);
int kp_ui_minimap_create(KpUi *ui);
/* The minimap of the game that replaced the one it showed (a load): the view's tile and no pings (InitMinimap 0x46e260),
 * the terrain built and the image drawn again (MinimapImageInit 0x46d7e0). */
void kp_ui_minimap_reset(KpUi *ui);
void kp_ui_minimap_ping(KpUi *ui, uint32_t x, uint32_t y, uint8_t colour_row);
/* The text lines (kp_ui_lines.c): ResetTextLines 0x46ae30 (0 or -1: the layer), AddTextLines 0x46ab40 (expiry: a
 * tick; flag 1 a trigger's text), SetErrorLine 0x46ae70, ClearTriggerTextLines 0x46aef0, ExpireTextLines 0x46afb0 (at
 * ui->now), and the layer as DrawTextLines 0x46acd0 draws it (index 0 transparent; NULL before the reset). */
int kp_ui_lines_reset(KpUi *ui);
void kp_ui_lines_add(KpUi *ui, const uint8_t *text, uint8_t colour, uint32_t expiry, uint8_t flag);
void kp_ui_error_line(KpUi *ui, const uint8_t *text);
void kp_ui_lines_clear_trigger(KpUi *ui);
void kp_ui_lines_expire(KpUi *ui);
const KpUiSurface *kp_ui_lines_layer(KpUi *ui);
/* A game event (KP_EVENT_*, kp_game.h) for the interface at ui->now: the error line (ERROR: a stat_txt id;
 * NETWORK_TEXT: a network.tbl id, DisplayChatMessage 0x46a860 from sender -1), the system and trigger texts
 * (DisplaySystemMessage 0x46b070 of a unit that could not be placed, DisplayTriggerText 0x46aec0), the trigger
 * portraits (ShowTriggerPortrait 0x4a7320), the skip's ending of both and the trigger pings (AddMinimapPing 0x46e8f0);
 * other kinds are not the interface's (yet). */
void kp_ui_game_event(KpUi *ui, int64_t kind, int64_t a, int64_t b, int64_t c, const char *text);
/* GetUnitTypeName 0x428ce0: the map's name of a unit type, else its stat_txt name ("" past the types). */
const uint8_t *kp_ui_unit_type_name(KpUi *ui, int64_t t);
/* The cursor (cur.cpp; atlas P13 0x43e410..0x43e7b0; kp_ui_cursor.c): SetCursorGrp 0x43e5e0 by id (the animation
 * goes on from its counter), AnimateCursor 0x43e410 at ui->now (a frame every 100 ms), and the frame as DrawCursor
 * 0x43e490 draws it: w * h indices (0 transparent) into out (capacity bytes) and its place relative to the mouse
 * (dx, dy: the frame's offsets less 63). Returns 0, or -1 hidden or without a cursor GRP or room. */
void kp_ui_cursor_set(KpUi *ui, uint16_t id);
/* SetCursorGrp 0x43e5e0 with the glue theme's arrow.grp (LoadGlueCursors 0x4584e0): its indices are the theme's
 * palette's, so the cursor keeps its colours on every glue screen. A new cursor id, or the game's console, drops it. */
void kp_ui_cursor_set_theme(KpUi *ui, int theme);
/* The theme's directory name under glue\ (PalMm .. PalNl), NULL for a theme out of range. */
const char *kp_ui_glue_theme_dir(int theme);
/* UpdateCursor 0x43ea40 (the game loop's): the edge scroll's cursor (GetEdgeScrollCursorId 0x43e7b0 at the system
 * cursor's position cursor_x, cursor_y) or the pointer's (GetPointerCursorId 0x43e6d0 with GetUnitHoverCursorId
 * 0x43ea90 at the last event's), then AnimateCursor. */
void kp_ui_cursor_update(KpUi *ui, int32_t cursor_x, int32_t cursor_y);
/* The pointer's cursor alone (no edge scroll: a finger aiming at the screen's edge), then AnimateCursor. */
void kp_ui_pointer_update(KpUi *ui);
void kp_ui_cursor_animate(KpUi *ui);
/* What a touch of the game view at its point (x, y) does (phase 10, not the original's: ADR 0014), as KP_TOUCH_*
 * of kp_touch.h numbers them (0 is a panel's, the host's answer): 1 select (a unit of the local player's under it, or
 * none of the player's selected), 2 order (the player's units selected), 3 aim (a target or a placement is being
 * chosen). Reads only. */
enum { KP_UI_TOUCH_PANEL, KP_UI_TOUCH_SELECT, KP_UI_TOUCH_ORDER, KP_UI_TOUCH_AIM };
int kp_ui_touch_target(KpUi *ui, int32_t x, int32_t y);
/* The dialog framework's pieces the glue uses: UpdateHover 0x4c1c20, SchedulePressRelease 0x4c3570, the controls'
 * sound hook (event 0 over, 1 click), EndDialog 0x4c2b50 (the result already in ui->result). */
void kp_ui_update_hover(KpUi *ui, int32_t dialog, const KpUiEvent *e);
void kp_ui_schedule_press(KpUi *ui, int32_t c);
void kp_ui_ui_sound(KpUi *ui, int event);
void kp_ui_close_dialog(KpUi *ui, int32_t dialog);
/* The modeless dialogs over the game and the result dialogs (kp_ui_result.c): OpenModelessDialog 0x46ecc0 (the
 * dialog, -1 when its BIN is missing), CloseModelessDialog 0x46ec70, ModelessDialogDefaultProc 0x46eda0, the
 * in-game dialogs' DrawDialogTileFrame 0x47bb70 and SetDialogBorderColor 0x47bd40, and ShowDefeatDialog 0x4bcf80 /
 * ShowVictoryDialog 0x4bcff0 of a KP_EVENT_GAME_RESULT (2 defeat, 3 victory, 5 draw); the answer leaves
 * ui->game_exit. */
int32_t kp_ui_open_modeless(KpUi *ui, const char *bin, KpUiInteract proc);
void kp_ui_close_modeless(KpUi *ui, int destroy);
int kp_ui_modeless_default(KpUi *ui, KpUiControl *d, KpUiEvent *e);
void kp_ui_tile_frame(KpUi *ui, int32_t dialog);
void kp_ui_border_colour(KpUi *ui, int32_t dialog);
int32_t kp_ui_show_result(KpUi *ui, int result);
/* EndGame 0x443db0's interface part, after the host saw ui->game_exit. */
void kp_ui_end_game(KpUi *ui);
/* The score screen (kp_ui_score.c): RunScoreScreen 0x461940 over the data (its dialog, -1 when its files are missing;
 * the glue state 14..19 and the music are the score directory's), the data of a finished game (the local player's
 * name; elapsed: the seconds from its start to its end, less the paused seconds the interface took off), whether the
 * dialog was answered (OK: ui->result 7) and the release. */
int32_t kp_ui_score_open(KpUi *ui, const KpUiScoreData *data);
void kp_ui_score_data(KpUi *ui, struct KpGame *g, int result, const char *player_name, uint32_t elapsed,
	KpUiScoreData *out);
int kp_ui_score_glue_state(const KpUi *ui);
/* The glue's loop (kp_ui_menus.c): the screen of a glue state (0 main menu, 5 login, 6 and 22 the campaign menus, 13
 * load; its dialog, or -1 for a state the host runs: the ready rooms, the score); once its modal run ended
 * kp_ui_menus_poll returns 1 with menus.state the next state and menus.action the host's work. */
int32_t kp_ui_menus_enter(KpUi *ui, int state);
int kp_ui_menus_poll(KpUi *ui);
/* ParseMissionWarpNumber 0x4bbb60: a "5" or "5b" after a campaign's warp prefix as the row's mission (*mission), 1 or 0;
 * SetPendingGlueMission 0x457e10 records it for the glue's next entry (kp_ui_menus_enter). */
int kp_ui_warp_mission(KpUi *ui, const char *text, int race, int base, int expansion, uint16_t *mission);
void kp_ui_set_pending_mission(KpUi *ui, uint16_t mission, int race, int expansion);
void kp_ui_menus_free(KpUi *ui);
/* RunModal 0x4c35c0 of a loaded dialog with its procedure; done(result) runs once the dialog is destroyed. */
int32_t kp_ui_modal(KpUi *ui, const char *bin, KpUiInteract proc, KpUiModalDone done);
void kp_ui_modal_destroyed(KpUi *ui, int32_t dialog);
/* RunUiTimers 0x4c5a20's callbacks (kp_ui_tick) and the resumption of a pass a modal loop interrupted. */
void kp_ui_resume_pass(KpUi *ui);
void kp_ui_score_free(KpUi *ui);
/* The list boxes (kp_ui_list.c): ListBoxProc 0x4c6480 and DrawListBox 0x4c6740 (the stock type 12), AddListItem
 * 0x4c61d0 (flags 0x10: the text duplicated; the index or 0xff), InsertListItem 0x4c6a70, DeleteListItem 0x4c6f60,
 * ClearList 0x4c6ef0, SelectListItem 0x4c7250, SetListRedraw 0x4c7230, GetListSelection 0x4c6890. */
int kp_ui_list_proc(KpUi *ui, KpUiControl *c, KpUiEvent *e);
void kp_ui_list_draw(KpUi *ui, KpUiControl *c, int32_t x, int32_t y, const int16_t clip[4]);
int kp_ui_list_add(KpUi *ui, int32_t list, const uint8_t *text, uint8_t flags);
void kp_ui_list_insert(KpUi *ui, int32_t list, int index, const uint8_t *text, uint8_t flags);
void kp_ui_list_delete(KpUi *ui, int32_t list, int index);
void kp_ui_list_clear(KpUi *ui, int32_t list);
void kp_ui_list_select(KpUi *ui, int32_t list, int index);
void kp_ui_list_redraw(KpUi *ui, int32_t list, int on);
int kp_ui_list_selection(const KpUi *ui, int32_t list);
int kp_ui_list_count(const KpUi *ui, int32_t list);
/* GetListItemText 0x4c6850 ("" outside the items). */
const uint8_t *kp_ui_list_text(const KpUi *ui, int32_t list, int index);
void kp_ui_list_set_data(KpUi *ui, int32_t list, int index, uint32_t data);
uint32_t kp_ui_list_data(const KpUi *ui, int32_t list, int index);
void kp_ui_list_free(KpUiControl *c);
/* The scroll bars and sliders (kp_ui_scrollbar.c): ScrollBarProc 0x4c9630, the draw procedures 0x4c8d10 / 0x4c9060,
 * GetScrollPos 0x4c9920, SetScrollPos 0x4c9ae0, SetScrollRange 0x4c9b20, SetScrollSteps 0x4c9ad0, the callback. */
int kp_ui_scrollbar_proc(KpUi *ui, KpUiControl *c, KpUiEvent *e);
void kp_ui_scrollbar_draw(KpUi *ui, KpUiControl *c, int32_t x, int32_t y, const int16_t clip[4]);
int16_t kp_ui_scrollbar_pos(KpUiControl *c);
void kp_ui_scrollbar_set_pos(KpUi *ui, int32_t bar, int32_t value);
void kp_ui_scrollbar_set_range(KpUi *ui, int32_t bar, int32_t min, int32_t max);
void kp_ui_scrollbar_set_steps(KpUi *ui, int32_t bar, uint8_t page, uint8_t tick);
void kp_ui_scrollbar_set_callback(KpUi *ui, int32_t bar, KpUiScrollChanged changed);
void kp_ui_scrollbar_free(KpUiControl *c);
/* CreateScrollBar 0x4c9580 of a tall list, linked after it (its index, -1 when the pool is full). */
int32_t kp_ui_create_scroll_bar(KpUi *ui, int32_t list);
/* CancelDragBox 0x4bccc0: an active drag box ends and the game's input handlers are installed again. */
void kp_ui_cancel_drag_box(KpUi *ui);
/* OnCaptureChanged 0x43f450: the window lost the mouse (the focus went) with a button down: the buttons are up and the
 * game's input procedures are installed again (InstallGameInputHandlers 0x448dd0: a drag box ends), outside a game
 * none (ResetInputHandlers 0x43f330). */
void kp_ui_capture_changed(KpUi *ui);
/* MainWindowProc 0x43f600's WM_ACTIVATEAPP: the application's activity (0x66fd24; UpdateMapScroll 0x49bf10 stands
 * while inactive) and the key bytes cleared, the held keys' releases never coming. The game's frames go on: plan D8
 * keeps a desktop game running without the focus (kp_session.h's clock). */
void kp_ui_set_active(KpUi *ui, int active);
/* The blends (kp_ui_effects.c): InitEffects' level of a percentage, SetEffectPalette 0x4ca660 (0 or -1), a pixel's
 * blend at a level, BlitWithEffect 0x4ca110's modes (KP_UI_BLIT_*) and DrawTextWithOpacity 0x4ca290's colour row. */
int kp_ui_effect_level(uint32_t percent);
int kp_ui_effects_palette(KpUi *ui, const uint8_t *palette);
/* InitEffects 0x4ca420 with a descriptor: every slot of the levels given is rebuilt (weight = its level) without the
 * excluded entries (256 flags, NULL none). 0 or -1. */
int kp_ui_effects_descriptor(KpUi *ui, const uint8_t *palette, const uint8_t *excluded, const uint8_t *levels,
	int level_count);
/* StartDialogPaletteEffect 0x47bca0 over the game's palette (768 bytes RGB) and its colour-cycled entries (256 flags):
 * 60 % without the cycled entries, and the dialogs' effect percentages (0x4c4d50) 60, 60, 60. */
int kp_ui_game_effects(KpUi *ui, const uint8_t *rgb, const uint8_t *cycled);
/* A single-colour set's blend row (256 bytes): the result over each destination index. */
void kp_ui_effect_row(const KpUi *ui, int set, uint8_t out[256]);
void kp_ui_effects_free(KpUi *ui);
uint8_t kp_ui_blend(const KpUi *ui, int level, uint8_t src, uint8_t dst);
/* BlitSingleColourBlend 0x4ca9b0 of a pixel with set 0 (A) or 1 (B): 0 keeps dst, the set's colour blends into dst at
 * the set's level, any other colour is copied. */
uint8_t kp_ui_single_colour(const KpUi *ui, int set, uint8_t src, uint8_t dst);
void kp_ui_blit_effect(const KpUi *ui, uint8_t *dst, int32_t dst_pitch, const uint8_t *src, int32_t src_pitch,
	int32_t w, int32_t h, int mode, uint32_t percent);
void kp_ui_text_opacity_row(const KpUi *ui, uint8_t out[8], const uint8_t row[8], const uint8_t base[8],
	uint32_t percent);
/* The flic buttons (kp_ui_flc.c): FlcButtonProc 0x4c82f0, the text button's 0x4c83b0, FlcAnimationProc 0x4c82b0,
 * StartDialogFlcTimers 0x4c84f0, StopDialogFlcTimers 0x4c84c0. */
int kp_ui_flc_button_proc(KpUi *ui, KpUiControl *c, KpUiEvent *e);
int kp_ui_text_button_proc(KpUi *ui, KpUiControl *c, KpUiEvent *e);
int kp_ui_flc_animation_proc(KpUi *ui, KpUiControl *c, KpUiEvent *e);
void kp_ui_flc_start_timers(KpUi *ui, int32_t dialog);
/* AdvanceFlicButton 0x4c89b0: a flic button's clips continued, its glow faded (timer 0x48's callback). */
void kp_ui_flc_advance(KpUi *ui, int32_t owner);
void kp_ui_flc_stop_timers(KpUi *ui, int32_t dialog);
/* The glue (kp_ui_glue.c): its state at kp_ui_new, InitGlueDialog 0x457f00 (procedures by control ID, the theme index
 * of the twelve), ReleaseGlueDialog 0x458380, ActivateGlueScreen 0x4579b0, EndGlueScreen 0x457b50, the slides
 * StartGlueSlideIn 0x458100 ({ID, direction} pairs, the delay in ms) and SlideOutGlueDialog 0x457a40, LoadGlueSounds
 * 0x458740, the 50 ms timer 0x4574f0, the palette on screen (R, G, B, 0 x 256) and the serial of its changes. */
void kp_ui_glue_init(KpUi *ui);
void kp_ui_glue_free(KpUi *ui);
void kp_ui_glue_init_dialog(KpUi *ui, int32_t dialog, const KpUiInteract *procs, int count, int theme);
void kp_ui_glue_release_dialog(KpUi *ui, int32_t dialog);
void kp_ui_glue_activate(KpUi *ui, int32_t dialog, int theme);
void kp_ui_glue_end(KpUi *ui, int next_theme);
void kp_ui_glue_slide_in(KpUi *ui, int32_t dialog, const uint16_t *table, int count, uint16_t delay);
void kp_ui_glue_slide_out(KpUi *ui, int32_t dialog);
/* The menus' CRT rand 0x4d14d4 (logged for the oracle). */
uint32_t kp_ui_glue_rand(KpUi *ui);
void kp_ui_glue_sounds(KpUi *ui);
void kp_ui_glue_cursor_tick(KpUi *ui, int32_t owner);
const uint8_t *kp_ui_glue_palette(KpUi *ui, uint32_t *serial);
/* The ready rooms (kp_ui_briefing.c): RunTerranReadyRoom 0x461420 and its twins up to the dialog over the game the
 * room briefs (ui->game: its briefing list, strings and objectives; race 0 zerg, 1 terran, 2 protoss), the idle
 * event's PollBriefingReady 0x461120, the VM's record of an action for the oracle, and the release. The result is
 * ui->result once the dialog closed (101 start, 14 cancel, 19 skip). */
int kp_ui_ready_room_open(KpUi *ui, int race);
/* The room after its establishing text closed: its dialog, -1 while the text runs (also the room's dialog once open). */
int32_t kp_ui_ready_room_continue(KpUi *ui);
void kp_ui_ready_room_idle(KpUi *ui);
/* One network turn step of the room (0x476c00 from PollBriefingReady; the idle event's every 250 ms). */
void kp_ui_ready_room_turn(KpUi *ui);
void kp_ui_brief_log(KpUi *ui, int64_t record, int index, int type, int result);
void kp_ui_ready_room_free(KpUi *ui);
/* The credits screens (kp_ui_credits.c): RunCreditsScreen 0x415010 of rez\<name>.txt up to its dialog (the dialog
 * closes itself at the script's end or Escape: ui->credits.done), the establishing (finale = 0) or finale text's name
 * of a mission key (ShowMissionEstablishingText 0x414f70, ShowMissionFinaleText 0x414fc0; NULL none), the release. */
int32_t kp_ui_credits_open(KpUi *ui, const char *name);
const char *kp_ui_establishing_text(KpUi *ui, uint16_t mission, int finale);
void kp_ui_credits_free(KpUi *ui);
/* The portrait's pieces the briefing shares (kp_ui_portrait.c): ResolveUnitPortrait 0x4a65d0 and the movie of a
 * portdata row's variant (NULL when absent). */
uint16_t kp_ui_resolve_portrait(KpUi *ui, int64_t type);
const KpUiFile *kp_ui_portrait_movie(KpUi *ui, uint32_t row, int talking, int variant);
/* The video trace's entry of a path (SVidPlayBegin's name, for the oracle). */
void kp_ui_video_trace(KpUi *ui, const char *path, size_t len);
/* A portrait variant's movie name from an existing variant of its row and kind (its digit replaced), for the trace of
 * a missing file: 0, or -1 when the kind has no movie at all. */
int kp_ui_portrait_movie_name(KpUi *ui, uint32_t row, int talking, int variant, char *out, size_t size);
int kp_ui_cursor_frame(KpUi *ui, uint8_t *out, size_t capacity, int32_t *w, int32_t *h, int32_t *dx, int32_t *dy);
/* The frame kp_ui_cursor_frame would draw as a key (the cursor's glue theme << 32 | its id << 16 | the frame's index;
 * another key, another frame), or -1 hidden or without a cursor GRP. */
int64_t kp_ui_cursor_key(KpUi *ui);
/* The command card's button conditions (kp_ui_conditions.c; atlas P35 0x49f410..0x4a0490, the replay's 0x480ea0..
 * 0x480f90): the condition at the original's address with its value (the record's condition value, low byte as CL)
 * for the player (EDX, 0x508d50) and the status unit (the stack argument); it reads the console's selection
 * (ui->hud.selection, selection_count, status_unit). Returns 0 hidden, < 0 disabled (the requirement evaluators leave
 * their error in the game's KP_REQUIREMENT_ERROR, 0x6be654), > 0 enabled. */
int kp_ui_button_condition(KpUi *ui, uint32_t address, uint8_t value, int64_t player, int64_t status_unit);
/* The command card (kp_ui_card.c): InitCommandCard 0x4a0f20 (0 or -1), RefreshCommandCard 0x4a1760 (UpdateStatusUi's,
 * each frame) and SetCommandCardOverride 0x4a11e0 (a forced set: 229 targeting, 230 placement, the build menus,
 * 228 none). The button actions (kp_ui_actions.c; atlas P27 0x47c310..0x47ca50): the action at the original's address
 * with the button's value and the Shift flag; BeginTargeting 0x4b28b0, FinishTargeting 0x4b2850 (the order for a
 * click: the unit's, the ground's or the fogged unit's), CancelTargeting 0x4b2900 and StartPlacement 0x47d3a0. */
int kp_ui_card_create(KpUi *ui);
/* The progress bars of the status panel and the command card (0x4a1c70, 0x4a0920): a draw procedure. */
void kp_ui_draw_progress(KpUi *ui, KpUiControl *c, int32_t x, int32_t y, const int16_t clip[4]);
void kp_ui_card_refresh(KpUi *ui);
void kp_ui_card_modes(KpUi *ui); /* ComputeCommandCardModes 0x4a18d0 */
void kp_ui_card_override(KpUi *ui, uint16_t set);
/* UpdateCommandTooltip 0x4a1240 for the card's control c (-1 none) and HideCommandTooltip 0x4a0ef0. */
void kp_ui_card_tooltip(KpUi *ui, int32_t c);
void kp_ui_card_hide_tooltip(KpUi *ui);
void kp_ui_button_action(KpUi *ui, uint32_t address, uint16_t value, uint8_t shift);
void kp_ui_begin_targeting(KpUi *ui, uint8_t unit_order, uint8_t ground_order, uint8_t fog_order);
uint8_t kp_ui_finish_targeting(KpUi *ui, int64_t unit, uint16_t fog_type);
void kp_ui_cancel_targeting(KpUi *ui);
void kp_ui_start_placement(KpUi *ui, uint8_t order, uint16_t type);
/* EndPlacement 0x47d300(0, 0, 1) when the placement is active. */
void kp_ui_end_placement(KpUi *ui);
/* The game ended the cursor modes (KP_EVENT_CURSOR_MODES): the placement's handlers, command card and minimap cursor
 * when they are on, and with targeting the targeting (CancelTargetingUi 0x423120). */
void kp_ui_cursor_modes_ended(KpUi *ui, int targeting);
/* The input (kp_ui_input.c): an event of the window procedure (its mouse routines 0x43f480/0x43f520, the mouse move
 * 0x4400d0, the idle event 0x4401c0, keys and characters 0x43f600): the dialogs first (DispatchUiEvent 0x4c2bb0),
 * then the input procedure of its type; the key bytes and the mouse state as the event found them; the game view's
 * origin; InstallGameInputHandlers 0x448dd0 (a mode change); and the commands queued since the last take, one per
 * call (its size, 0 none; the bytes into out). */
int kp_ui_dispatch(KpUi *ui, KpUiEvent *e);
/* SetEditText 0x4c8110: an edit control's text (up to KP_UI_EDIT_SIZE bytes). EnableControl 0x4c2910 /
 * DisableControl 0x4c2280. */
void kp_ui_edit_set_text(KpUi *ui, int32_t control, const uint8_t *text);
/* EditBoxProc 0x4c7880 and DrawEditControl 0x4c3b40 (kp_ui_edit.c): the stock type 8. */
int kp_ui_edit_proc(KpUi *ui, KpUiControl *c, KpUiEvent *e);
void kp_ui_edit_draw(KpUi *ui, KpUiControl *c, int32_t x, int32_t y, const int16_t clip[4]);
void kp_ui_set_enabled(KpUi *ui, int32_t control, int enable);
/* Whether the text is typed into an edit control: one is shown in a shown dialog (the phones show their soft keyboard
 * then). */
int kp_ui_edit_focused(const KpUi *ui);
/* An edit control's text buffer (KP_UI_EDIT_SIZE bytes, its text), taken on first use; NULL with none free. */
uint8_t *kp_ui_edit_buffer(KpUi *ui, int32_t control);
/* An edit box's text inset +0x38 (SaveNameEditProc 0x49b4d0 sets 3). */
void kp_ui_edit_set_margin(KpUi *ui, int32_t control, uint8_t x);
/* HitTestDialog 0x4c2090: the control under the event's cursor, the dialog itself, or -1. */
int32_t kp_ui_hit_test(KpUi *ui, int32_t dialog, const KpUiEvent *e);
/* SetEventCapture 0x4c3370 (control or -1 for an event type) and ReleaseControlReferences 0x4c3770. */
void kp_ui_set_capture(KpUi *ui, int32_t control, int type);
void kp_ui_release_refs(KpUi *ui, int32_t control);
/* The minimap's orders (kp_ui_input.c): MinimapTargetClick 0x46d3a0 and RightClickOnMinimap 0x47f8d0 at a map point. */
void kp_ui_minimap_target(KpUi *ui, int32_t x, int32_t y);
/* PickUnitAtPoint 0x4bcd00: the unit a click at map (x, y) takes, or 0. */
int64_t kp_ui_pick_unit(KpUi *ui, int32_t x, int32_t y);
void kp_ui_minimap_right_click(KpUi *ui, int32_t x, int32_t y);
/* SelectControlFont 0x4c3660: the font of the control's flags into ui->text. */
void kp_ui_select_control_font(KpUi *ui, const KpUiControl *c);
/* The cheats (campaign.kpd CHEA: the bits a new game keeps, then per cheat its bit of the cheat word 0x5432fc and its
 * lowercase text; the bytes must outlive the interface): 0 or -1. A typed line that is a cheat toggles its bit
 * (ProcessCheatText 0x40dee0 over MatchCheatHash 0x40dc80 and MatchExtraCheatHash 0x40dfa0: the hashed texts are the
 * texts). */
int kp_ui_set_cheats(KpUi *ui, const uint8_t *chea, size_t size);
/* DispatchUiEvent 0x4c2bb0's step 4: the press slots due at ui->now get user code 3; returns their number. */
int kp_ui_press_slots(KpUi *ui);
void kp_ui_input_event(KpUi *ui, KpUiEvent *e);
/* The window's keyboard (kp_ui_keys.c): ACCL's tables (kp_ui_new loads them), the active one (KP_UI_ACCEL_*), and a
 * WM_KEYDOWN/WM_KEYUP or WM_CHAR as the message pump and MainWindowProc 0x43f600 take it: an accelerator's command
 * event, else the key bytes and the key or character event (none while a mouse button is held). The modifiers are
 * the key bytes of Shift, Ctrl and Alt, which the keys' own messages keep. kp_ui_window_key returns whether the pump's
 * TranslateMessage follows (the key's WM_CHAR): not for an accelerator's key, nor when a dialog closed while the key
 * was dispatched (0x6ce660, which also ends the pump's pass). */
void kp_ui_load_accelerators(KpUi *ui, const uint8_t *accl, size_t size);
void kp_ui_set_accelerators(KpUi *ui, int table);
int kp_ui_window_key(KpUi *ui, int down, uint16_t vk, int repeat);
void kp_ui_window_char(KpUi *ui, uint16_t code, int repeat);
void kp_ui_set_keys(KpUi *ui, uint8_t shift, uint8_t ctrl, uint8_t alt, uint16_t mouse_state);
void kp_ui_set_screen(KpUi *ui, int32_t x, int32_t y);
/* The game view's size in pixels (default 640 x 400): the view rectangle of the cursor's choice, the placement, the units on
 * screen, the scroll's limits, the minimap's rectangle and the sounds' distance. */
void kp_ui_set_view(KpUi *ui, int32_t w, int32_t h);
/* SetScreenPosition 0x40b450: the view's top-left clamped to the map and aligned to 8 pixels, unless the camera pan
 * holds it; camera_moved when it changed. */
void kp_ui_set_screen_position(KpUi *ui, int32_t x, int32_t y);
/* SetTextboxPrompt 0x4b38c0: the text box's prompt (control 5; NULL none), shown while the text box is closed: the
 * targeting's (network.tbl 0x3f) and the placement's (0x40). */
void kp_ui_set_textbox_prompt(KpUi *ui, const uint8_t *text);
/* CenterScreenOnUnit 0x422b10: the unit's tile at the view's centre (nothing for a unit without a sprite). */
void kp_ui_centre_on_unit(KpUi *ui, int64_t unit);
/* The game's first view: on the local player's start location (0x46a280, InitScreenPosition 0x445810); the camera
 * moves (kp_ui take_camera). */
void kp_ui_start_screen(KpUi *ui);
/* UpdateMapScroll 0x49bf10, which the game loop runs every 10 ms or more of its clock (not every frame): the scroll
 * keys, else the cursor at the screen's edge (the system cursor's position cursor_x, cursor_y: GetCursorPos), move the
 * view with the step's acceleration; the camera pan moves it to its target. Returns 1 when the view moved. */
int kp_ui_update_scroll(KpUi *ui, int32_t cursor_x, int32_t cursor_y);
/* The middle button's scroll: BeginMiddleButtonScroll 0x448cb0 and the middle-up procedure 0x448dc0. */
void kp_ui_middle_down(KpUi *ui, KpUiEvent *e);
void kp_ui_middle_up(KpUi *ui, KpUiEvent *e);
/* The interface's pending ends of the game's trigger waits, which a save keeps (kp_session.h's PRES): the trigger
 * portrait's milliseconds left (1 and *portrait_ms, 0 without one: 0x4a7300) and whether a camera pan's arrival will
 * release one (*camera). The original's save keeps none (InitTriggerState 0x4be240 clears the waits before a load),
 * Koprulu's keeps the waits so a continuation equals the uninterrupted game. kp_ui_game_loaded: a load replaced the
 * game, whose pending ends the interface forgets (the pan and the trigger portrait end without releasing), and the
 * cursor modes end as ResetPerGameState 0x444230 ends them before a load (InitPlacementLayers 0x47d000: the placement
 * closes with its ghosts; the targeting); the text lines go (InitGameWorld 0x443790 resets them for a load too), and the
 * minimap starts again on the loaded game (kp_ui_minimap_reset), as the original's console, made after the load, starts
 * on it (else the game before the load shows until the redraw timer). */
int kp_ui_pending_releases(const KpUi *ui, uint32_t *portrait_ms, int *camera);
void kp_ui_game_loaded(KpUi *ui);
/* A loaded save's pending ends of its trigger waits (after kp_ui_game_loaded): the trigger portrait's end in
 * portrait_ms (its timer, without the portrait, whose unit the save does not keep), a camera pan's arrival at once (the
 * saved view is its target's); each releases the wait through kp_ui_take_release as the uninterrupted game's would. */
void kp_ui_restore_releases(KpUi *ui, int portrait, uint32_t portrait_ms, int camera);
/* StartCameraPan 0x49be10 (the Center View trigger's, its arrival releasing the wait) and EndCameraPan 0x49be50. */
void kp_ui_start_pan(KpUi *ui, int32_t x, int32_t y);
void kp_ui_end_pan(KpUi *ui);
/* A screen position the original computes for its 640 x 400 view (a point at that view's centre) for the layout's
 * view: moved by half the view's extra size, so the point stays at the centre (unchanged for the original's view). */
void kp_ui_view_target(const KpUi *ui, int32_t *x, int32_t *y);
/* The cursor set to the view's centre (0x4ce8c0(320, 200)). */
void kp_ui_warp_cursor(KpUi *ui);
/* GetEdgeScrollCursorId 0x43e7b0 at the system cursor's position: the edge's direction (dx, dy, unwritten while the
 * keys' suppression lasts and the cursor has not moved) and the edge cursor's id or 0. */
uint16_t kp_ui_edge_scroll(KpUi *ui, int32_t cursor_x, int32_t cursor_y, int *dx, int *dy);
/* The layout's answer to "is the event over the console" for the next events: 0 or 1 for a point on none of the
 * layout's panels, -1 for a point mapped onto one (the original's console art decides). */
void kp_ui_set_over_console(KpUi *ui, int over);
/* ResetInputHandlers 0x43f330 (no input procedures, the default keys) and the game's procedures over it. */
void kp_ui_reset_input(KpUi *ui);
void kp_ui_install_game_input(KpUi *ui);
size_t kp_ui_take_command(KpUi *ui, uint8_t *out, size_t capacity);
/* QueueGameCommand 0x485bd0 (a command of the interface for the game's next turns) and SelectUnitsWithGroupRecall
 * 0x4bce70 (the local selection, its voice when sound and the network's). */
void kp_ui_queue_command(KpUi *ui, const uint8_t *bytes, size_t size);
void kp_ui_select_units(KpUi *ui, const int64_t *units, int n, int sound);
/* SendSelectionCommands 0x474130 of the game's local selection (KP_EVENT_SELECTION_SENT: the game's removal 0x4a7b80). */
void kp_ui_selection_sent(KpUi *ui, const int64_t *units, int n);
/* KP_EVENT_SELECTION_GONE: the unit leaves the network selection's mirror. */
void kp_ui_selection_gone(KpUi *ui, int64_t unit);
/* UnitToPackedRef 0x47bd60: the unit's index and its uniqueness << 11, the commands' unit reference. */
uint16_t kp_ui_unit_ref(struct KpGame *g, int64_t n);
/* The status panel (kp_ui_status.c): InitStatusPanel 0x4a1f10 with LoadWireframeData 0x4a8260 (0 or -1), and the
 * per-frame UpdateStatusDataPanel 0x4a2aa0. */
int kp_ui_status_create(KpUi *ui);
void kp_ui_status_frame(KpUi *ui);

/* --- the canvas: the panels composed for the screen (ADR 0011) --- */

/* The logical screen's canvas: two bytes per pixel, the palette index and the opacity (0, 255,
 * KP_UI_CANVAS_OVERLAY_A or KP_UI_CANVAS_BLEND_A), rows top first.
 * A frame is composed into `next` (kp_ui_canvas_begin, the draws, kp_ui_canvas_end); the end compares it with
 * `pixels`, copies the changed rows and leaves their rectangle in dirty (x0, y0, x1, y1 exclusive). */
/* A glyph of the vector text placed on the canvas (not the original's: the text of large scales, ADR 0011): the
 * composition put what lay under its bitmap in its place; the render draws the vector font's glyph there. */
typedef struct {
	int32_t x, y;                 /* the pen on the canvas */
	const uint8_t *font;          /* the FNT font it was plotted with */
	uint8_t code, advance, fill, shadow, has_shadow;
	uint8_t box[4];               /* its bitmap as its FNT header: width, height, x and y from the pen */
	int whole;                    /* all its glyph's pixels, none covered by a later draw */
	int32_t clip[4];              /* else where it is drawn: its uncovered pixels' box, or its part's rectangle (x0, y0,
	                               * x1, y1 exclusive) */
	uint16_t seq;                 /* the draw that placed it */
	size_t glyph;                 /* its entry in the glyph log while the frame is composed (0 after) */
} KpUiCanvasGlyph;

/* An item of the HD interface on the canvas (not the original's: ADR 0013 R4d): a frame of an SD interface file drawn
 * there, which the render draws from StarCraft: Remastered's art where the item plane names it. */
#define KP_UI_CANVAS_ITEMS 2048
typedef struct {
	const char *path;             /* the SD file: a GRP's or a PCX's path in ui.kpd, the console's PCX */
	size_t path_len;
	int32_t frame;                /* the GRP's frame; a console's group; 0 a PCX */
	int32_t x, y, w, h;           /* the SD frame's rectangle on the canvas; a console: its PCX's top-left and size */
	int32_t column;               /* a filler: the console column stretched over the rectangle; -1 */
	int tinted;                   /* drawn through a colour map: the HD image in the brightest of these indices */
	uint8_t tint_index[KP_UI_TINTS];
	uint32_t tint_count[KP_UI_TINTS];
	uint16_t under;               /* the item its rectangle lies over (an icon's button), drawn under it; 0 none */
	int tiled;                    /* a tiled image's (KpUiImage): its HD image repeats every w x h from its rectangle */
	int32_t canvas[4];            /* a GRP frame's whole GRP rectangle on the canvas */
	uint8_t parts[KP_UI_WIRE_COLOURS]; /* a wireframe's parts' colours (palette indices) */
} KpUiCanvasItem;

typedef struct {
	int32_t w, h;
	uint8_t *pixels, *next;
	uint16_t *owner;              /* the draw of the frame that last wrote each pixel and whether a translucent dialog
	                               * blended over it since (the vector text's cover; kept while `text`) */
	uint16_t seq;                 /* the frame's draws so far */
	int text;                     /* the frame composes the vector text */
	KpUiCanvasGlyph *glyphs;      /* the frame's vector glyphs (kp_ui_canvas_text_end leaves the visible ones) */
	int32_t glyph_count, glyph_capacity;
	KpUiCanvasGlyph *spare;       /* the glyphs kp_ui_canvas_text_end makes of them */
	int32_t spare_capacity;
	uint8_t tint_row[256];        /* the frame's translucent blend of the vector glyphs' colours */
	int tint_valid;
	uint8_t *source;              /* a surface with its live glyphs taken out */
	size_t source_size;
	int32_t dirty[4];
	int32_t blends;               /* the frame's KP_UI_CANVAS_BLEND_A texels */
	int32_t hd_lost;              /* the frame's pixels of logged images left without an item (the items full): SD */
	uint8_t shade[256];           /* the margins' darkening of the glue palette (kp_ui_canvas_margins) */
	uint32_t shade_serial;        /* the glue palette's serial it was built for plus 1, 0 none */
	uint8_t *scratch;             /* a GRP frame decoded for the game view (kp_ui_canvas_game_view) */
	size_t scratch_size;
	/* the HD interface's items (not the original's: StarCraft: Remastered's art, ADR 0013 R4d; kp_ui_canvas_hd): per
	 * pixel the item of the draw that wrote it (0 none), composed into ids_next as the pixels are */
	uint16_t *ids, *ids_next;
	uint16_t item;                /* the item of the draws from here on (kp_ui_canvas_item) */
	KpUiCanvasItem items[KP_UI_CANVAS_ITEMS + 1]; /* the frame's items, from 1 */
	int32_t item_count;
} KpUiCanvas;

/* The vector text (ADR 0011, 2026-10-26): `record` keeps the glyph log of what the text engine plots (the surfaces
 * keep the original's pixels); `on` has the canvas take the live glyphs whose byte is in codes[256] out of the
 * surfaces it composes and list them for the render. Recording off frees the log; a change of `record` paints the open
 * dialogs again (their text into the log, or out of it). */
void kp_ui_set_vector_text(KpUi *ui, int record, int on, const uint8_t *codes);
/* ui.kpd VFNT's TrueType files (the text's, then font16x's wide one) and their count (0: none). */
int kp_ui_vector_fonts(const KpUi *ui, const uint8_t *const **files, const size_t **sizes);
/* The frame's vector glyphs: those a later draw covers whole are dropped, the others get their clip, and the pixels a
 * translucent dialog blended over are a glyph of their own in the blended colours; then the log forgets the glyphs no
 * compose found live for a while. Called before kp_ui_canvas_end. */
void kp_ui_canvas_text_end(KpUi *ui, KpUiCanvas *c);

/* Resizes the canvas (all transparent); the whole canvas is dirty. Returns -1 out of memory. */
int kp_ui_canvas_resize(KpUiCanvas *c, int32_t w, int32_t h);
/* The HD interface's item plane on (allocated at the canvas's size, kept through resizes) or off. */
int kp_ui_canvas_hd(KpUiCanvas *c, int on);
/* The HD interface's image log on or off (off frees it): the canvas's dialogs give their images' pixels to items. */
void kp_ui_set_hd(KpUi *ui, int on);
/* An image about to be drawn on the target surface (its file's data, frame and rectangle; NULL: not logged), the
 * pixels it writes, its end (tinted: drawn through a colour map). */
KpUiImage *kp_ui_image_begin(KpUi *ui, const uint8_t *file_data, int32_t frame, int32_t x, int32_t y, int32_t w,
	int32_t h);
void kp_ui_image_put(KpUiImage *im, int32_t x, int32_t y, uint8_t value);
void kp_ui_image_end(KpUi *ui, KpUiImage *im, int tinted);
/* Surface s (the target is set to it) as a whole an image of a file's frame (its pixels as they are, index 0 not
 * written when keyed) whose item lies at item (x, y, w, h) on s: a picture, a part of the console. */
void kp_ui_image_whole(KpUi *ui, KpUiSurface *s, const KpUiFile *f, int32_t frame, const int32_t item[4], int keyed);
/* A picture decoded from file f into pixels (w x h): a blit from it draws an image of f (kept while the HD interface
 * is off too: decoded once, drawn later). */
void kp_ui_picture(KpUi *ui, const uint8_t *pixels, int32_t w, int32_t h, const KpUiFile *f);
/* The last image drawn on a surface (its pixels), or NULL: the cursor's frame. */
const KpUiImage *kp_ui_image_last(const KpUi *ui, const uint8_t *surface);
/* A draw outside the images wrote rect (l, t, r, b exclusive) of surface t: every pixel, or with src only where src's
 * pixel (from sx, sy) is not 0 (a keyed blit). Those pixels are no longer any earlier image's, even where the
 * draw wrote the value the image had (a pixel holding what an image wrote is that image's, kp_ui_image_owners); an
 * image left with no pixel goes. The image being drawn (kp_ui_image_begin) keeps its pixels. */
void kp_ui_image_erase(KpUi *ui, const KpUiSurface *t, const int32_t rect[4], const KpUiSurface *src, int32_t sx,
	int32_t sy);
/* The images of surface s drawn after serial `after` (frames tiled over it, kp_ui_tile_frame) made one tiled image per
 * frame: the frames w x h with no offset, on the grid of the first of them. A dialog's background is some 2,700 such
 * 8 x 8 frames, which filled the log and the canvas's items before the console's. */
void kp_ui_image_tiles(KpUi *ui, const KpUiSurface *s, uint32_t after, int32_t w, int32_t h);
/* A blit's carrying of its source's images into the target (src's rectangle at sx, sy to rect on t, keyed: index 0
 * not written), read back from t after it; the target's images lose the pixels it wrote (kp_ui_image_erase). */
void kp_ui_image_blit(KpUi *ui, const KpUiSurface *src, const KpUiSurface *t, int32_t sx, int32_t sy,
	const int32_t rect[4], int keyed);
/* Per pixel of surface s (composed with the pixels `src`, at canvas x, y) the canvas item of the image that owns it
 * (0 none): the last image whose pixel it still is, or the smallest image whose rectangle holds it and lies over
 * that one (the larger image that wrote most of its rectangle, its pieces of the same picture counted as one; its item
 * then lies over the owner's, `under`) when that one was drawn first or the pixel is still the smaller one's, or a
 * tinted image over the plain background of its rectangle; a tinted GRP frame's rectangle is its whole GRP's (its HD
 * art reaches past the SD frame). The items are added to the canvas; NULL when s has no images. Images that own
 * no pixel are dropped. */
const uint16_t *kp_ui_image_owners(KpUi *ui, KpUiCanvas *c, const KpUiSurface *s, const uint8_t *src, int32_t x,
	int32_t y);
void kp_ui_image_log_free(KpUiImageLog *log);
/* The HD item of the draws from here on (0: none). */
void kp_ui_canvas_item(KpUiCanvas *c, uint16_t item);
/* A new item of the frame (its number, 0 when the plane is off or the frame has KP_UI_CANVAS_ITEMS). */
uint16_t kp_ui_canvas_add_item(KpUiCanvas *c, const KpUiCanvasItem *item);
void kp_ui_canvas_free(KpUiCanvas *c);
/* A new frame of interface ui (NULL: none): `next` all transparent, the vector text composed while ui's is on. */
void kp_ui_canvas_begin(const KpUi *ui, KpUiCanvas *c);
/* The frame's changes: returns 1 when something changed (dirty holds their rectangle), else 0. */
int kp_ui_canvas_end(KpUiCanvas *c);
/* The console art's columns [x0, x1) at canvas (x + x0, y): its index 0 is transparent. */
void kp_ui_canvas_console(KpUi *ui, KpUiCanvas *c, int32_t x, int32_t y, int32_t x0, int32_t x1);
/* A filler: the console art's column `column` stretched over w columns at canvas (x, y). */
void kp_ui_canvas_filler(KpUi *ui, KpUiCanvas *c, int32_t x, int32_t y, int32_t w, int32_t column);
/* An opaque rectangle of one index (a fixed screen's margins over the world). */
void kp_ui_canvas_fill(KpUiCanvas *c, int32_t x, int32_t y, int32_t w, int32_t h, uint8_t index);
/* A fixed screen's margins around its dialog at canvas (x, y) (a glue screen, plan section 2a: the theme's widened and
 * dimmed background): each margin pixel repeats the screen's nearest edge pixel, darkened to the glue palette's
 * nearest colour at a quarter of its brightness. Not the original's (ADR 0011 approximation). */
void kp_ui_canvas_margins(KpUi *ui, KpUiCanvas *c, int32_t dialog, int32_t x, int32_t y);
/* A dialog's bitmap at canvas (x, y): opaque, or with index 0 transparent when the dialog has flag 0x2000
 * (BlitDialogToScreen 0x4c5630 blits with the dialog's flags); a translucent dialog composited onto the backdrop (flags
 * 0x10040000, the in-game dialogs) keeps index 0 transparent and blends single-colour set A's colour into what lies
 * under it: into opaque canvas pixels here, over the world as opacity KP_UI_CANVAS_BLEND_A, which the render pass
 * maps through kp_ui_canvas_blend_row (256 bytes: the result over each world index). */
#define KP_UI_CANVAS_BLEND_A 128
void kp_ui_canvas_dialog(KpUi *ui, KpUiCanvas *c, int32_t dialog, int32_t x, int32_t y);
void kp_ui_canvas_blend_row(const KpUi *ui, uint8_t out[256]);
/* The layers over the game view (the text lines, the tooltip, the drag box, the command marker) are drawn opaque with
 * this opacity, which the render
 * pass treats as 255: the host tells them from the console art and the panels (IsPointOverConsole 0x43e890 tests the
 * console's art only, so a click on a text line reaches the game). */
#define KP_UI_CANVAS_OVERLAY_A 254
/* A surface at canvas (x, y) with its index 0 transparent (the text lines' layer), KP_UI_CANVAS_OVERLAY_A. */
void kp_ui_canvas_layer(KpUi *ui, KpUiCanvas *c, const KpUiSurface *s, int32_t x, int32_t y);
/* The tooltip box shown (kp_ui_tooltip) at canvas (x, y), opaque over what is there (KP_UI_CANVAS_OVERLAY_A). */
void kp_ui_canvas_tooltip(KpUi *ui, KpUiCanvas *c, int32_t x, int32_t y);
/* What the game-screen layer's draw callback 0x445ea0 draws after the sprites and the fog, for the game view whose
 * top-left lies at canvas (x, y), clipped to the view (KP_UI_CANVAS_OVERLAY_A): the drag box while a mouse button is
 * down (DrawDragBox 0x4bcc40) and the command marker while it shows (DrawCommandMarker 0x418be0; the world's draw list
 * leaves out its hidden sprite), then the placement's preview of screen layers 4 and 3. The canvas composes them under
 * the console. */
void kp_ui_canvas_game_view(KpUi *ui, KpUiCanvas *c, int32_t x, int32_t y);
/* Whether kp_ui_canvas_game_view draws anything now (the drag box, the marker or the placement's preview changes from
 * frame to frame). */
int kp_ui_game_view_active(const KpUi *ui);
/* The game screen's work each frame: UpdateGameUi 0x445020's placement preview (kp_ui_placement_update), then its
 * draw's DrawDragBox 0x4bcc40, which ends a drag box with no button down (CancelDragBox 0x4bccc0). */
void kp_ui_game_view_update(KpUi *ui);
/* The placement's preview (kp_ui_placebox.c): PreparePlacementBitmap 0x47cec0 (bitmap k of the type with its ghost,
 * DrawPlacementGhostImage 0x47cd80), UpdatePlacementCursor 0x47d7e0 (UpdateGameUi 0x445020's, each pass of the game
 * loop, and the placement's start and click: CheckPlacementAt at the tile under the last event's mouse position and
 * DrawPlacementGrid 0x47d920; outside the game view the layers hide and the tile and the error stay) and
 * FreePlacementBitmaps 0x47ce20 with the layers hidden. */
void kp_ui_placement_prepare(KpUi *ui, int k, uint16_t type);
void kp_ui_placement_update(KpUi *ui);
/* ScreenToPlacementTile 0x47d490: the tile under the game view's point (x, y) and the point snapped to the tile's
 * top-left on the view (a negative coordinate, the view's partial first column or row, moves on by a tile). Returns 0
 * outside the view. */
int kp_ui_placement_tile(const KpUi *ui, int32_t *x, int32_t *y, int64_t *tx, int64_t *ty);
void kp_ui_placement_free(KpUi *ui);

/* Decodes an 8-bit PCX (a run-length image with a trailing palette) into pixels (w * h bytes); returns 0 or -1. */
int kp_ui_pcx(const uint8_t *data, size_t size, int32_t *w, int32_t *h, uint8_t *pixels, size_t capacity);

#ifdef __cplusplus
}
#endif

#endif
