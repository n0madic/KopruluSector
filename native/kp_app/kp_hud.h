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

/* The interface's data files (generated/data/<name>.kpd; the interface keeps pointers into them). */
enum { KP_UI_FILE_UI, KP_UI_FILE_FONTS, KP_UI_FILE_STRINGS, KP_UI_FILE_PORTRAITS, KP_UI_FILE_SOUNDS, KP_UI_FILE_CAMPAIGN,
	KP_UI_FILES };
extern const char *const KP_UI_FILE_NAMES[KP_UI_FILES];

/* A new interface: its strings, portraits, sounds, the cheats and the campaign's tables, a language's overlays over
 * them (locale NULL: none; a part refused: ui->locale_problem). files must outlive it. NULL with the error. */
KpUi *kp_interface_new(uint8_t *const files[KP_UI_FILES], const size_t sizes[KP_UI_FILES], const KpUiLocale *locale,
	char *err, size_t err_size);

/* The mission's tileset for the console's colours and the minimap. */
typedef struct {
	const KpTileset *tileset;
	const KpPalettes *palettes;
	int tileset_index;
	const uint8_t *colors; /* the mission's colour slots (8), or NULL */
} KpHudMap;

/* The console's colours over a tileset (the UI colours and the minimap's). */
int kp_hud_colours(KpUi *ui, const KpHudMap *map);

/* What a console's canvas was composed for: the logical screen's size and the open dialogs in order (more than
 * KP_HUD_KEY_DIALOGS: count -1, composed every frame). */
#define KP_HUD_KEY_DIALOGS 64
typedef struct {
	int32_t w, h, count;
	int32_t dialogs[KP_HUD_KEY_DIALOGS];
} KpHudKey;

/* The touch pad (ADR 0014 point 4): in the touch mode a translucent column along the game view's left edge under the
 * resource line, of buttons of at least KP_HUD_PAD_DP dp where the screen's height allows (never shorter than
 * KP_HUD_PAD_MIN_PX logical pixels: the last groups left out instead): Shift (the next click's, or locked), the groups'
 * toggle, then the groups with units in them (folded) or all ten (opened by the toggle). Hidden under a modeless
 * dialog. */
#define KP_HUD_PAD_DP 44
#define KP_HUD_PAD_MIN_PX 12
/* A touch on the minimap within this of a ping's centre is a tap on the ping: the view centred on its event (the
 * Space's last event for the fingers; ours, not the original's). */
#define KP_HUD_PING_DP 24
/* A group's tap folds the opened pad this long after (the original's second recall within 500 ms centres the view on
 * the group: the button stays where it is for it). */
#define KP_HUD_PAD_FOLD_MS 600
/* The virtual keys of a phone's Back (kp_hud_back_key). */
#define KP_HUD_VK_ESCAPE 0x1b
#define KP_HUD_VK_F10 0x79
typedef struct {
	int on, open;                /* shown (the touch mode); the groups opened */
	double px_per_dp;
	int32_t x, y, h, bw, bh, gap; /* placed on the logical screen: the column's top-left and height, a button's size */
	int buttons[KP_UI_PAD_BUTTONS], count; /* the column's buttons top first (KP_UI_PAD_*) */
	int pressed, shift;          /* the pressed button (-1 none), Shift (KP_UI_PAD_SHIFT_*) */
	uint32_t fold_at;            /* the interface's clock when a group's tap folds the opened pad, 0 none */
	uint32_t held;               /* the groups with units the pad was laid out and drawn with */
	uint8_t fill;                /* the blend colour it was drawn with (the in-game dialogs change it) */
	int laid, drawn, dirty;      /* laid out; drawn since its state changed; drawn again since the canvas was composed */
	KpUiSurface surface;
	uint8_t *pixels;
	size_t size;
} KpHudPad;

/* The console of a game. */
typedef struct {
	KpUi *ui;
	KpCanvas *canvas; /* its own, or the glue's (shared) */
	int own_canvas;
	uint16_t buttons;
	int32_t gesture;   /* the panel a press picked: -2 none, -1 the game view, else the dialog */
	int has_start;     /* the game's first view (its start location's, a loaded save's) replaces the host's camera */
	int32_t start_x, start_y;
	KpHudKey composed;
	int game_view, has_composed;
	double scrolled_ms;
	KpHudPad pad;
} KpHud;

#define KP_HUD_CONSOLE_ROWS 80 /* the original's game view is its screen less the console's 80 rows */
#define KP_HUD_SCROLL_MS 10    /* the game loop's scroll step runs when more than this passed (GameLoop 0x4451f0) */

/* The game view of a w x h logical screen. */
void kp_hud_view_size(int32_t w, int32_t h, int32_t *vw, int32_t *vh);
/* The view's top-left (*x, *y) moved so that its centre stays where it was when the logical screen goes from ow x oh
 * to nw x nh (a rotation, a fold, a window resized; plan section 2.1), before the host clamps it to the map. */
void kp_hud_keep_focus(int32_t ow, int32_t oh, int32_t nw, int32_t nh, int64_t *x, int64_t *y);
/* The console over game g of interface ui (shared: the glue's canvas; NULL its own), the clock at its creation, the
 * logical screen of the game's first view (w 0: none), from a save (no tips at its start). -1 with ui->error. */
int kp_hud_init(KpHud *h, KpUi *ui, KpCanvas *shared, KpGame *g, const KpHudMap *map, uint32_t now_ms, int32_t w,
	int32_t hh, int from_save);
void kp_hud_free(KpHud *h);
/* A game event for the interface (the text lines, the minimap's pings, the result dialog). */
void kp_hud_event(KpHud *h, const KpEvent *e);
/* The cursor a frame updates: none, the pointer's alone (a finger aiming: no edge scroll), or the mouse's (the edge
 * scroll's at the system cursor's point, else the pointer's, and the map's scroll). */
enum { KP_HUD_POINTER_NONE, KP_HUD_POINTER_AIM, KP_HUD_POINTER_MOUSE };
/* Each frame: the interface's timers at now_ms, its work after each of the frame's game steps, the cursor update
 * (cursor: KP_HUD_POINTER_*) and map scroll from the camera (*cam_x, *cam_y; moved by the interface) with the system
 * cursor at the logical point (cx, cy), the placement's preview, the touch pad, the repaint and the canvas composed for
 * the w x h logical screen. Returns whether the canvas changed; *released: a trigger wait ends; *moved: the camera
 * moved. */
int kp_hud_frame(KpHud *h, int32_t w, int32_t hh, uint32_t now_ms, int steps, int cursor, int32_t cx, int32_t cy,
	int64_t *cam_x, int64_t *cam_y, int *moved, int *released);
/* What a touch at the logical point does with the view at (cam_x, cam_y), as kp_touch.h's KP_TOUCH_*: a button of the
 * touch pad's, a ping on the minimap (a panel's button: a slide is the minimap's drag), a panel's or the console art's, else kp_ui_touch_target's answer for the game
 * view. */
int kp_hud_touch_target(KpHud *h, int32_t lx, int32_t ly, int64_t cam_x, int64_t cam_y);
/* Whether a building's placement goes on in the game. */
int kp_hud_placing(const KpUi *ui);
/* The virtual key a phone's Back is in a game: Escape where it closes or cancels something (a menu over the paused
 * game, a target's or a placement's aiming, a message's text box), else F10, the game menu (the game's Escape would
 * skip the transmissions instead). */
uint16_t kp_hud_back_key(const KpUi *ui);
/* The touch pad shown (on: the touch mode) with the logical screen's pixels per dp; laid out and drawn by the frame. */
void kp_hud_pad(KpHud *h, int on, double px_per_dp);
/* The pad's button at the logical point (KP_UI_PAD_*), else -1. */
int kp_hud_pad_at(const KpHud *h, int32_t lx, int32_t ly);
/* The logical point at the centre of the pad's button (KP_UI_PAD_*) where it is shown; 0 where it is not. */
int kp_hud_pad_point(const KpHud *h, int button, int32_t *lx, int32_t *ly);
/* A pad button's moment (kp_touch.h's KP_TOUCH_BUTTON_*) at the logical point: a group's tap recalls it (a second
 * within the original's double tap centres the view on it), its long press assigns the selection to it (Ctrl and the
 * digit), the tap folding the opened groups a moment later (KP_HUD_PAD_FOLD_MS; not while a button is pressed), the
 * long press at once; a group does nothing while a text box has the focus (its digit would be typed). Shift's tap turns
 * it on for the next click that queues a command or off, its long press locks it on until a tap (the clicks' Shift:
 * orders queued, the selection added to); the toggle's tap opens or folds the groups. A tap on a minimap ping (no pad
 * button) centres the view on the ping's event. */
void kp_hud_pad_button(KpHud *h, int moment, int32_t lx, int32_t ly);
/* The window's key bytes cleared (the focus lost or back, kp_ui_set_active): the pad's Shift goes off with them. */
void kp_hud_keys_cleared(KpHud *h);
/* A key of the window (down, its virtual-key code, a repeat) and the characters of a text event (UTF-8): those of the
 * Basic Multilingual Plane; control characters (Enter's carriage return comes from the key itself) and malformed
 * sequences (kp_utf8_next) are left out, and a non-ASCII one outside an edit box too. */
void kp_hud_key(KpUi *ui, int down, uint16_t vk, int repeating);
void kp_hud_text(KpUi *ui, const char *utf8);
/* With vk the key that brought the text: outside an edit box a non-ASCII character is a hotkey on a non-Latin layout, and
 * the hotkeys are ASCII letters, so it stands for the letter of that physical key (ADR 0016). */
void kp_hud_text_key(KpUi *ui, const char *utf8, uint16_t vk);
/* An input event at the logical point (lx, ly) with the view's top-left at map (cam_x, cam_y). */
void kp_hud_input(KpHud *h, int event, int32_t lx, int32_t ly, int64_t cam_x, int64_t cam_y);
/* The commands the interface queued, into the session (the local player's). */
void kp_hud_take_commands(KpUi *ui, KpSession *s);
/* The view the interface moved to: 1 and its top-left, else 0. */
int kp_hud_take_camera(KpUi *ui, int64_t *x, int64_t *y);
/* A loaded save's view, (*x, *y) as the save kept it (LoadGameFromSave 0x499890's SetScreenPosition 0x40b450:
 * clamped to the map, aligned to 8 pixels; *x, *y become the placed view): the next frame's camera, as the start
 * location's is a new game's. */
void kp_hud_place_camera(KpHud *h, int64_t *x, int64_t *y);
/* The window lost the mouse with a button down: no release comes (OnCaptureChanged 0x43f450). */
void kp_hud_capture_lost(KpHud *h);
/* The mouse state's button bits (0x6d315c) after a window event. */
uint16_t kp_hud_buttons(uint16_t buttons, int event);

/* --- the glue's screens --- */

enum { KP_GLUE_NONE, KP_GLUE_MENUS, KP_GLUE_ROOM, KP_GLUE_SCORE, KP_GLUE_CREDITS, KP_GLUE_EPILOG };
#define KP_GLUE_MAIN 0
#define KP_GLUE_CAMPAIGN 6
#define KP_GLUE_LOAD_SCREEN 13 /* its Back goes to the campaign menu it came from */
#define KP_GLUE_EXPANSION_CAMPAIGN 22

/* What a glue screen ended with. */
enum { KP_GLUE_GOES_ON, KP_GLUE_PROFILE, KP_GLUE_MISSION, KP_GLUE_EXIT, KP_GLUE_LOAD, KP_GLUE_ROOM_START,
	KP_GLUE_ROOM_CANCEL, KP_GLUE_SCORE_DONE, KP_GLUE_INTRO, KP_GLUE_LANGUAGE };
typedef struct {
	int kind;
	int profile_changed;
	int64_t mission;
	int state, expansion;
	int32_t save; /* KP_GLUE_LOAD: the load screen's saved game, 0-based in the profile's list */
	int cinematic; /* KP_GLUE_MISSION: the cinematic the mission history's row plays first (with its prelude), 0 none */
} KpGlueEvent;

typedef struct {
	KpUi *ui;
	KpCanvas canvas;
	KpProfile *profile;
	uint16_t buttons;
	int32_t place_x, place_y;
	int screen; /* KP_GLUE_* */
	int cinematics; /* the host plays cinematics: View Intro is KP_GLUE_INTRO (else the main menu opens again) */
	int room_opened;
	int32_t composed_w, composed_h, composed_bottom; /* the screen composed: its size, its bottom dialog */
	uint32_t composed_serial;                        /* and the palette's serial (the margins) */
	int has_composed;
	uint32_t palette_serial;
	int has_palette;
} KpGlue;

/* The glue over an interface (kp_interface_new) and the profile (its name and progress), no screen yet (the first
 * start's intro may come before the main menu: kp_glue_menus opens it). */
void kp_glue_init(KpGlue *g, KpUi *ui, KpProfile *profile, uint32_t now_ms);
void kp_glue_free(KpGlue *g);
void kp_glue_menus(KpGlue *g, int state);
/* The campaign's epilog (mode 9, RunCampaignEpilog 0x414ef0): the expansion's or the original's text, then its
 * credits, then the main menu (RunMainLoop 0x4679b0: glue state 0); the main menu at once when it cannot open. */
void kp_glue_epilog(KpGlue *g, int expansion);
/* The ready room of the started game (its glue state's race); the game waits for the room's Start. */
int kp_glue_room(KpGlue *g, KpGame *game, int state);
/* The score screen of the finished game (result: 2 defeat, 3 victory), the game's seconds. */
void kp_glue_score(KpGlue *g, KpGame *game, int result, uint32_t elapsed);
/* Each frame while a glue screen shows: what it ended with, whether the canvas changed (*changed) and the glue's
 * palette when it changed (rgb, *palette). */
KpGlueEvent kp_glue_frame(KpGlue *g, int32_t w, int32_t h, uint32_t now_ms, int *changed, uint8_t rgb[768],
	int *palette);
void kp_glue_input(KpGlue *g, int event, int32_t lx, int32_t ly);
/* The logged-in ID's saved games as the interface's list (the load screen's, the in-game dialogs'). */
void kp_glue_saves(KpUi *ui, const KpProfile *profile);
void kp_glue_capture_lost(KpGlue *g);

#ifdef __cplusplus
}
#endif

#endif
