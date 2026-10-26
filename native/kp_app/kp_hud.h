/*
 * The interface's host (plan phase S, stage S2), headless: the interface (kp_ui) made with its data, the console of a
 * game over it (the panels composed by the layout of the logical screen into the canvas, the repaint, the input in the
 * original's coordinates) and the glue's screens (the menus, the ready rooms, the score screen) of the same interface.
 * The host passes the clock and the window's input in; the canvas, the cursor and the palette come out for the drawing.
 */
#ifndef KP_HUD_H
#define KP_HUD_H

#include "kp_present.h"
#include "kp_session.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The window procedure's event types (kp_ui.h). */
enum { KP_HUD_MOUSE_MOVE = 3, KP_HUD_LEFT_DOWN = 4, KP_HUD_LEFT_UP = 5, KP_HUD_RIGHT_DOWN = 7, KP_HUD_RIGHT_UP = 8,
	KP_HUD_IDLE = 13 };

/* The interface's data files (generated/data/<name>.kpd; the interface keeps pointers into them). */
enum { KP_UI_FILE_UI, KP_UI_FILE_FONTS, KP_UI_FILE_STRINGS, KP_UI_FILE_PORTRAITS, KP_UI_FILE_SOUNDS, KP_UI_FILE_CAMPAIGN,
	KP_UI_FILES };
extern const char *const KP_UI_FILE_NAMES[KP_UI_FILES];

/* A new interface: its strings, portraits, sounds, the cheats and the campaign's tables.
 * files must outlive it. NULL with the error. */
KpUi *kp_interface_new(uint8_t *const files[KP_UI_FILES], const size_t sizes[KP_UI_FILES], char *err, size_t err_size);

/* The mission's tileset for the console's colours and the minimap. */
typedef struct {
	const KpTileset *tileset;
	const KpPalettes *palettes;
	int tileset_index;
	const uint8_t *colors; /* the mission's colour slots (8), or NULL */
} KpHudMap;

/* The console's colours over a tileset (the UI colours and the minimap's). */
int kp_hud_colours(KpUi *ui, const KpHudMap *map);

/* The console of a game. */
typedef struct {
	KpUi *ui;
	KpCanvas *canvas; /* its own, or the glue's (shared) */
	int shared, own_canvas;
	uint16_t buttons;
	int32_t gesture;   /* the panel a press picked: -2 none, -1 the game view, else the dialog */
	int has_start;
	int32_t start_x, start_y;
	char composed[512];
	int game_view, has_composed;
	int32_t w, h;
	double scrolled_ms;
	int layout;        /* the layout forced, -1 by the screen's size */
} KpHud;

#define KP_HUD_CONSOLE_ROWS 80 /* the original's game view is its screen less the console's 80 rows */
#define KP_HUD_SCROLL_MS 10    /* the game loop's scroll step runs when more than this passed (GameLoop 0x4451f0) */

/* The game view of a w x h logical screen. */
void kp_hud_view_size(int32_t w, int32_t h, int32_t *vw, int32_t *vh);
/* The console over game g of interface ui (shared: the glue's canvas; NULL its own), the clock at its creation, the
 * logical screen of the game's first view (w 0: none), from a save (no tips at its start). -1 with ui->error. */
int kp_hud_init(KpHud *h, KpUi *ui, KpCanvas *shared, KpGame *g, const KpHudMap *map, uint32_t now_ms, int32_t w,
	int32_t hh, int from_save);
void kp_hud_free(KpHud *h);
/* A game event for the interface (the text lines, the minimap's pings, the result dialog). */
void kp_hud_event(KpHud *h, const KpEvent *e);
/* Each frame: the interface's timers at now_ms, its work after each of the frame's game steps, the cursor update and
 * map scroll from the camera (*cam_x, *cam_y; moved by the interface) with the system cursor at the logical point
 * (has_cursor), the placement's preview, the repaint and the canvas composed for the w x h logical screen. Returns
 * whether the canvas changed; *released: a trigger wait ends; *moved: the camera moved. */
int kp_hud_frame(KpHud *h, int32_t w, int32_t hh, uint32_t now_ms, int steps, int has_cursor, int32_t cx, int32_t cy,
	int64_t *cam_x, int64_t *cam_y, int *moved, int *released);
/* A key of the window (down, its virtual-key code, a repeat) and the characters of a text event (UTF-8). */
void kp_hud_key(KpUi *ui, int down, uint16_t vk, int repeating);
void kp_hud_text(KpUi *ui, const char *utf8);
/* An input event at the logical point (lx, ly) with the view's top-left at map (cam_x, cam_y). */
void kp_hud_input(KpHud *h, int event, int32_t lx, int32_t ly, int64_t cam_x, int64_t cam_y);
/* The commands the interface queued, into the session (the local player's). */
void kp_hud_take_commands(KpUi *ui, KpSession *s);
/* The view the interface moved to: 1 and its top-left, else 0. */
int kp_hud_take_camera(KpUi *ui, int64_t *x, int64_t *y);
/* The window lost the mouse with a button down: no release comes (OnCaptureChanged 0x43f450). */
void kp_hud_capture_lost(KpHud *h);
/* The mouse state's button bits (0x6d315c) after a window event. */
uint16_t kp_hud_buttons(uint16_t buttons, int event);

/* --- the glue's screens --- */

enum { KP_GLUE_NONE, KP_GLUE_MENUS, KP_GLUE_ROOM, KP_GLUE_SCORE };
#define KP_GLUE_MAIN 0
#define KP_GLUE_CAMPAIGN 6
#define KP_GLUE_EXPANSION_CAMPAIGN 22

/* What a glue screen ended with. */
enum { KP_GLUE_GOES_ON, KP_GLUE_PROFILE, KP_GLUE_MISSION, KP_GLUE_EXIT, KP_GLUE_LOAD, KP_GLUE_ROOM_START,
	KP_GLUE_ROOM_CANCEL, KP_GLUE_SCORE_DONE };
typedef struct {
	int kind;
	int profile_changed;
	int64_t mission;
	int state, expansion;
	int32_t save; /* KP_GLUE_LOAD: the load screen's saved game, 0-based in the profile's list */
} KpGlueEvent;

typedef struct {
	KpUi *ui;
	KpCanvas canvas;
	KpProfile *profile;
	uint16_t buttons;
	int32_t place_x, place_y;
	int screen; /* KP_GLUE_* */
	int room_opened;
	char composed[256];
	int has_composed;
	uint32_t palette_serial;
	int has_palette;
} KpGlue;

/* The glue over an interface (kp_interface_new) and the profile (its name and progress), at the main menu. */
void kp_glue_init(KpGlue *g, KpUi *ui, KpProfile *profile, uint32_t now_ms);
void kp_glue_free(KpGlue *g);
void kp_glue_menus(KpGlue *g, int state);
/* The ready room of the started game (its glue state's race); the game waits for the room's Start. */
int kp_glue_room(KpGlue *g, KpGame *game, int state);
/* The score screen of the finished game (result: 2 defeat, 3 victory), the game's seconds. */
void kp_glue_score(KpGlue *g, KpGame *game, int result, uint32_t elapsed);
/* Each frame while a glue screen shows: what it ended with, whether the canvas changed (*changed) and the glue's
 * palette when it changed (rgb, *palette). */
KpGlueEvent kp_glue_frame(KpGlue *g, int32_t w, int32_t h, uint32_t now_ms, int *changed, uint8_t rgb[768],
	int *palette);
void kp_glue_invalidate_palette(KpGlue *g);
void kp_glue_input(KpGlue *g, int event, int32_t lx, int32_t ly);
void kp_glue_saves(KpUi *ui, const KpProfile *profile);
void kp_glue_capture_lost(KpGlue *g);

#ifdef __cplusplus
}
#endif

#endif
