/* The interface's host (native/kp_app/kp_hud.h: the console of a game, its input and the glue's screens) and the
 * presentation's canvas and layout (native/kp_present: kp_canvas_*, kp_layout_*) over the generated data, on kp_ui's
 * functions and fields and the oracle's games (kp_t_game). The rules of held keys and modifier sides are the SDL
 * host's own (native/kp_app/sdl/kp_app_input.c): their test here is what the window's keys do in kp_ui (kp_hud_key). */
#include "kp_test_ui.h"
#include "kp_hud.h"
#include "kp_touch.h"
#include "kp_ids.h"

#include <math.h>

#define DATA "generated/data/"
#define TERRAN01 "generated/missions/terran/terran01.kpm"
#define ADDON "generated/scenarios/terran_addon.kpm"
#define BUILD_TERRAN "generated/scenarios/build_terran.kpm"
#define OUTCOME "generated/scenarios/trigger_outcome.kpm"
#define TERRAN03 "generated/missions/terran/terran03.kpm"
#define PROTOSS01 "generated/missions/expprotoss/protoss01.kpm"

/* The window procedure's event types (kp_ui.h) the cases use. */
#define KEY_DOWN 0
#define KEY_UP 2
#define MOUSE_MOVE KP_UI_MOUSE_MOVE
#define LEFT_DOWN KP_UI_LEFT_DOWN
#define LEFT_UP KP_UI_LEFT_UP
#define LEFT_DOUBLE KP_UI_LEFT_DOUBLE
#define RIGHT_DOWN KP_UI_RIGHT_DOWN
#define RIGHT_UP KP_UI_RIGHT_UP
#define IDLE KP_UI_IDLE
#define CHAR 15
#define COMMAND 16

#define SHOWN 8 /* a control's shown flag */
#define MARINE 0
#define SCV 7
#define COMMAND_CENTER 106
#define COMSAT 107
#define SUPPLY_DEPOT 109
#define GEYSER 188
#define FLAG 215
#define PLACEMENT_GHOST 581
#define OVERLAY_A KP_UI_CANVAS_OVERLAY_A

static const char *const UI_FILES[] = { DATA "ui.kpd", NULL };
static const char *const MENU_FILES[] = { DATA "ui.kpd", DATA "strings.kpd", TERRAN01, NULL };
static const char *const GLUE_FILES[] = { DATA "ui.kpd", DATA "fonts.kpd", DATA "strings.kpd", DATA "portraits.kpd",
	DATA "sounds.kpd", DATA "campaign.kpd", NULL };

static int32_t half(int32_t v) { return v >= 0 ? v / 2 : -((1 - v) / 2); } /* math.floor(v / 2) */
static int64_t max64(int64_t a, int64_t b) { return a > b ? a : b; }
static int64_t min64(int64_t a, int64_t b) { return a < b ? a : b; }

/* Whether the console's files and the mission exist (the SKIP line for the first missing one). */
static int ready(KpT *T, const char *label, const char *mission)
{
	const char *files[KP_UI_FILES + 3];
	char name[KP_UI_FILES][64];
	int k;
	for (k = 0; k < KP_UI_FILES; k++) {
		snprintf(name[k], sizeof name[k], DATA "%s", KP_UI_FILE_NAMES[k]);
		files[k] = name[k];
	}
	files[KP_UI_FILES] = DATA "palettes.kpd";
	files[KP_UI_FILES + 1] = mission;
	files[KP_UI_FILES + 2] = NULL;
	return kp_t_ready(T, label, files);
}

/* --- the interface and the console of a game over it (with_hud) --- */

static void free_interface(void *p) { kp_ui_free((KpUi *)p); }

/* An interface over the generated data, released when the test ends. */
static KpUi *interface(KpT *T, uint8_t *files[KP_UI_FILES], size_t sizes[KP_UI_FILES])
{
	char path[128], err[256];
	KpUi *ui;
	int k;
	for (k = 0; k < KP_UI_FILES; k++) {
		snprintf(path, sizeof path, DATA "%s", KP_UI_FILE_NAMES[k]);
		files[k] = kp_t_read(T, path, &sizes[k]);
	}
	ui = kp_interface_new(files, sizes, err, sizeof err);
	if (!ui) kp_t_fail(T, "%s", err);
	kp_t_defer(T, free_interface, ui);
	return ui;
}

typedef struct {
	KpOracleGame *og;
	KpGame *g;
	KpUi *ui;
	KpHud hud;
	KpTileset tileset;
	KpPalettes palettes;
	KpHudMap map;
	uint8_t *files[KP_UI_FILES];
	size_t sizes[KP_UI_FILES];
} Hud;

static void free_hud(void *p)
{
	kp_hud_free(&((Hud *)p)->hud);
	free(p);
}

/* The console of game og on a logical screen w x h (the game's first view on it), its interface
 * made at 1000 ms, over the mission's tileset. */
static Hud *with_hud(KpT *T, KpOracleGame *og, int32_t w, int32_t h)
{
	Hud *H = (Hud *)calloc(1, sizeof *H);
	const KpMission *m = &og->data.mission;
	uint8_t *palettes;
	size_t size;
	KpError err;
	kp_t_ok(T, H != NULL, "out of memory");
	H->og = og;
	H->g = og->g;
	H->ui = interface(T, H->files, H->sizes);
	palettes = kp_t_read(T, DATA "palettes.kpd", &size);
	if (kp_palettes_load(&H->palettes, palettes, size, &err) != 0) {
		free(H);
		kp_t_fail(T, "%s", err.msg);
	}
	if (kp_tileset_load(&H->tileset, og->tileset, og->tileset_size, KP_TILESET_NAMES[m->tileset],
		&err) != 0) {
		free(H);
		kp_t_fail(T, "%s", err.msg);
	}
	H->map.tileset = &H->tileset;
	H->map.palettes = &H->palettes;
	H->map.tileset_index = (int)(m->tileset % 8);
	H->map.colors = m->has_colors ? m->colors : NULL;
	if (kp_hud_init(&H->hud, H->ui, NULL, H->g, &H->map, 1000, w, h, 0) != 0) {
		char msg[sizeof H->ui->error];
		snprintf(msg, sizeof msg, "%s", H->ui->error);
		kp_hud_free(&H->hud);
		free(H);
		kp_t_fail(T, "%s", msg);
	}
	kp_t_defer(T, free_hud, H);
	return H;
}

/* A frame's results: the view the interface moved to (moved, x, y) and whether a trigger wait ends. */
typedef struct {
	int moved, released, changed;
	int64_t x, y;
} Frame;

#define NO_CURSOR 0, 0, 0
#define CURSOR(x, y) KP_HUD_POINTER_MOUSE, (x), (y)
#define CAMERA(x, y) 1, (x), (y)
#define KEEP 0, 0, 0 /* the camera as the interface has it (none given) */

/* The console's frame: the w x h logical screen, the clock, the steps, the cursor and the camera (when given). */
static Frame frame(Hud *H, int32_t w, int32_t h, uint32_t now, int steps, int cursor, int64_t cx, int64_t cy,
	int has_camera, int64_t cam_x, int64_t cam_y)
{
	Frame f;
	int64_t x = has_camera ? cam_x : H->ui->input.screen_x, y = has_camera ? cam_y : H->ui->input.screen_y;
	f.changed = kp_hud_frame(&H->hud, w, h, now, steps, cursor, (int32_t)cx, (int32_t)cy, &x, &y, &f.moved,
		&f.released);
	f.x = x;
	f.y = y;
	return f;
}

/* An input event's results: the commands the interface queued and the view it moved to. */
typedef struct {
	int commands, moved;
	int64_t x, y;
} Input;

/* An input event at the logical point (lx, ly) with the view at (cam_x, cam_y). */
static Input input(Hud *H, int event, int64_t lx, int64_t ly, int64_t cam_x, int64_t cam_y)
{
	Input r;
	kp_hud_input(&H->hud, event, (int32_t)lx, (int32_t)ly, cam_x, cam_y);
	r.commands = kp_t_ui_take_commands(H->ui, NULL, NULL);
	r.x = r.y = 0;
	r.moved = kp_hud_take_camera(H->ui, &r.x, &r.y);
	return r;
}

/* input with the key bytes the caller gives (shift, ctrl, alt). */
static Input input_keys(Hud *H, int event, int64_t lx, int64_t ly, int64_t cam_x, int64_t cam_y, int shift, int ctrl,
	int alt)
{
	H->ui->input.keys[0] = (uint8_t)(shift != 0);
	H->ui->input.keys[1] = (uint8_t)(ctrl != 0);
	H->ui->input.keys[2] = (uint8_t)(alt != 0);
	return input(H, event, lx, ly, cam_x, cam_y);
}

/* A key typed: its press, its character and its release. */
static void type_key(KpUi *ui, uint16_t vk, uint16_t ch)
{
	kp_t_ui_input(ui, KEY_DOWN, vk, 0);
	kp_t_ui_input(ui, CHAR, ch, 0);
	kp_t_ui_input(ui, KEY_UP, vk, 0);
}

static void game_event(KpUi *ui, int64_t kind, int64_t a, int64_t b, int64_t c)
{
	kp_ui_game_event(ui, kind, a, b, c, NULL);
}

/* A game event through the host. */
static void hud_event(Hud *H, int64_t kind, int64_t a, int64_t b, int64_t c)
{
	KpEvent e;
	memset(&e, 0, sizeof e);
	e.kind = kind;
	e.a = a;
	e.b = b;
	e.c = c;
	kp_hud_event(&H->hud, &e);
}

/* The last open dialog whose BIN (lower case) is bin, or contains it (part), or -1. */
static int32_t dialog_of(const KpUi *ui, const char *bin, int part)
{
	int32_t d, found = -1;
	char name[128];
	for (d = ui->open; d >= 0; d = ui->controls[d].next) {
		kp_t_ui_bin(ui, d, name, sizeof name);
		if (part ? strstr(name, bin) != NULL : strcmp(name, bin) == 0) found = d;
	}
	return found;
}

/* A dialog's child of the ID, or -1. */
static int32_t child_of(const KpUi *ui, int32_t d, int id)
{
	int32_t c;
	for (c = ui->controls[d].child; c >= 0; c = ui->controls[c].next) {
		if (ui->controls[c].id == id) return c;
	}
	return -1;
}

static uint8_t alpha_at(const KpCanvas *c, int64_t x, int64_t y)
{
	uint8_t index, alpha;
	kp_canvas_pixel(c, (int32_t)x, (int32_t)y, &index, &alpha);
	return alpha;
}

static uint8_t index_at(const KpCanvas *c, int64_t x, int64_t y)
{
	uint8_t index, alpha;
	kp_canvas_pixel(c, (int32_t)x, (int32_t)y, &index, &alpha);
	return index;
}

/* The game view's overlay pixels (the drag box, the marker, the text lines) in a rectangle (inclusive). */
static int overlay_in(const KpCanvas *c, int64_t x0, int64_t y0, int64_t x1, int64_t y1)
{
	int64_t x, y;
	int n = 0;
	for (y = y0; y <= y1; y++) {
		for (x = x0; x <= x1; x++) n += alpha_at(c, x, y) == OVERLAY_A;
	}
	return n;
}

/* The first unit of the local player's with a sprite (alive: hp > 0 too; type >= 0: of that type). */
static int64_t own_unit(KpGame *g, int alive, int64_t type, int skip)
{
	int64_t n;
	for (n = 1; n <= KP_UNITS; n++) {
		if (g->u.sprite[n] == -1 || g->u.owner[n] != g->local_player || (alive && g->u.hp[n] <= 0)) continue;
		if (type >= 0 && g->u.type[n] != type) continue;
		if (skip-- == 0) return n;
	}
	return 0;
}

static KpOracleGame *terran01(KpT *T) { return kp_t_game(T, TERRAN01, 0, 0, NULL); }

/* oracle.runner(game) with no commands: the game's object steps from its start. */
static KpRunner *runner(KpT *T, KpGame *g)
{
	KpRunner *run = (KpRunner *)calloc(1, sizeof *run);
	kp_t_ok(T, run != NULL, "out of memory");
	kp_t_defer(T, free, run);
	kp_runner_init(run, g, kp_t_replay(T));
	return run;
}

/* --- the canvas and the layout --- */

static void free_canvas(void *p)
{
	kp_canvas_free((KpCanvas *)p);
	free(p);
}

static KpCanvas *canvas(KpT *T)
{
	KpCanvas *c = (KpCanvas *)calloc(1, sizeof *c);
	kp_t_ok(T, c != NULL, "out of memory");
	kp_canvas_init(c);
	kp_t_defer(T, free_canvas, c);
	return c;
}

/* A dialog's panel (its BIN as the dialog has it, its original rectangle, the backdrop flag). */
static void panel_of(const KpUi *ui, int32_t d, KpPanel *p)
{
	const KpUiControl *k = &ui->controls[d];
	memset(p, 0, sizeof *p);
	p->dialog = d;
	kp_oracle_ui_bin_name(ui, k->bin, 0, p->bin, sizeof p->bin);
	p->rect[0] = k->left;
	p->rect[1] = k->top;
	p->rect[2] = k->right;
	p->rect[3] = k->bottom;
	p->backdrop = (k->flags & 0x10000000u) != 0;
}

/* The canvas (ADR 0011): the classic layout of a 640 x 480 screen is the original's screen; the menu button lies
 * where the original draws it, the console art around it, the world shows through the console's top. */
static void canvas_composes_the_classic_screen(KpT *T)
{
	KpOracleUi *u;
	KpCanvas *c;
	KpPanel panel;
	const KpUiSurface *bitmap;
	int32_t d, x, y, px = 0, py = 0;
	if (!kp_t_ready(T, "test_hud.canvas_composes_the_classic_screen", UI_FILES)) return;
	u = kp_t_ui(T, 0);
	kp_ui_font_set(u->ui, 1);
	kp_t_ok(T, kp_ui_apply_theme(u->ui, kp_oracle_race_theme(2)) == 0, "ui: no protoss GRP");
	kp_t_ok(T, kp_ui_load_console(u->ui, 2) == 0, "ui: no console art");
	d = kp_ui_create_menu_button(u->ui);
	kp_t_ok(T, d >= 0, "%s", u->ui->error);
	kp_ui_paint_dialog(u->ui, d);
	panel_of(u->ui, d, &panel);
	c = canvas(T);
	kp_t_eq(T, kp_canvas_compose(c, u->ui, 640, 480, &panel, 1, KP_LAYOUT_CLASSIC), 1, NULL);
	kp_t_eq(T, c->dirty[0], 0, NULL);
	kp_t_eq(T, c->dirty[1], 0, NULL);
	kp_t_eq(T, c->dirty[2], 640, NULL);
	kp_t_eq(T, c->dirty[3], 480, NULL);
	kp_t_ok(T, kp_canvas_place(c, d, &px, &py), "the menu button placed");
	kp_t_eq(T, px, 408, NULL);
	kp_t_eq(T, py, 388, NULL);
	bitmap = &u->ui->controls[d].bitmap;
	kp_t_ok(T, bitmap->pixels != NULL && bitmap->w == 88, "the button's bitmap");
	for (y = 0; y <= 19; y++) {
		for (x = 0; x <= 87; x += 7) {
			kp_t_eq(T, index_at(c, 408 + x, 388 + y), bitmap->pixels[y * 88 + x], "the button's pixel %d, %d", x, y);
			kp_t_eq(T, alpha_at(c, 408 + x, 388 + y), 255, NULL);
		}
	}
	kp_t_eq(T, alpha_at(c, 320, 100), 0, "above the console: the world");
	kp_t_eq(T, kp_canvas_compose(c, u->ui, 640, 480, &panel, 1, KP_LAYOUT_CLASSIC), 0,
		"an unchanged frame uploads nothing");
	kp_t_eq(T, kp_canvas_compose(c, u->ui, 1280, 480, &panel, 1, KP_LAYOUT_WIDE), 1, NULL);
	kp_t_eq(T, c->dirty[0], 0, NULL);
	kp_t_eq(T, c->dirty[1], 0, NULL);
	kp_t_eq(T, c->dirty[2], 1280, NULL);
	kp_t_eq(T, c->dirty[3], 480, NULL);
	kp_canvas_compose(c, u->ui, 1280, 480, &panel, 1, KP_LAYOUT_WIDE);
	kp_t_ok(T, kp_canvas_place(c, d, &px, &py), NULL);
	kp_t_eq(T, px, 408 + 640, NULL);
}

/* The item over column x of a filler (its lowest pixel with one): its frame, and its column (-1: the piece itself);
 * -1 and column -2 for none. */
static int filler_item(const KpCanvas *c, int32_t x, int32_t *column)
{
	int32_t y;
	for (y = c->h - 1; y >= 0; y--) {
		uint16_t id = c->canvas.ids[(size_t)y * (size_t)c->w + (size_t)x];
		if (id) {
			*column = c->canvas.items[id].column;
			return c->canvas.items[id].frame;
		}
	}
	*column = -2;
	return -1;
}

/* The HD console's fillers (ADR 0013 R4d) take a piece where it reaches over the gap, as the composite places it: the
 * frames' offsets are HD pixels. HD2's minimap piece (985 wide at -212) reaches to x 386; added unscaled, it ended at
 * 280 and the filler stretched its column 70 (the minimap's inside: a black bar with its frame's top) up to the status
 * panel's group on an 854 x 545 screen. */
static void hd_fillers_take_the_pieces_reach(KpT *T)
{
	static const int16_t PIECES[3][4] = { { -212, 610, 985, 350 }, { 278, 704, 578, 256 }, { 352, 610, 1143, 350 } };
	uint8_t head[7] = { 2, 1, 0, 3, 0, 0, 0 }, aset[KP_HD_UI_PATH + 8], frms[3 * 18], imgs[4];
	KpKpfOut sections[4] = { { "HEAD", head, sizeof head, 0 }, { "ASET", aset, sizeof aset, 0 },
		{ "FRMS", frms, sizeof frms, 0 }, { "IMGS", imgs, sizeof imgs, 0 } };
	KpOracleUi *u;
	KpCanvas *c;
	KpHdUi hd;
	KpError e;
	uint8_t *file;
	size_t size;
	int32_t x, column;
	int k;
	if (!kp_t_ready(T, "test_hud.hd_fillers_take_the_pieces_reach", UI_FILES)) return;
	u = kp_t_ui(T, 0);
	kp_t_ok(T, kp_ui_load_console(u->ui, 0) == 0, "ui: no console art");
	kp_t_ok(T, u->ui->console_file && u->ui->console_file->path_len < KP_HD_UI_PATH, "the console's file");
	memset(aset, 0, sizeof aset);
	memset(frms, 0, sizeof frms);
	memset(imgs, 0, sizeof imgs);
	memcpy(aset, u->ui->console_file->path, u->ui->console_file->path_len);
	aset[KP_HD_UI_PATH + 4] = 3, aset[KP_HD_UI_PATH + 6] = KP_HD_UI_CONSOLE;
	for (k = 0; k < 3; k++) {
		uint8_t *r = frms + 18 * k;
		int f;
		r[12] = KP_HD_UI_BC3;
		for (f = 0; f < 4; f++) { /* x, y at 14, 16; w, h at 8, 10 */
			uint16_t v = (uint16_t)PIECES[k][f];
			int at = f < 2 ? 14 + 2 * f : 8 + 2 * (f - 2);
			r[at] = (uint8_t)v, r[at + 1] = (uint8_t)(v >> 8);
		}
	}
	kp_t_eq(T, kp_kpf_build(KP_HD_UI_KIND, KP_HD_UI_SCHEMA, 0, "test", NULL, sections, 4, &file, &size), 0, "built");
	kp_t_defer(T, free, file);
	kp_t_eq(T, kp_hd_ui_open(&hd, file, size, size, "ui", &e), 0, "opened");
	c = canvas(T);
	kp_canvas_compose(c, u->ui, 854, 545, NULL, 0, KP_LAYOUT_WIDE);
	kp_t_eq(T, kp_ui_canvas_hd(&c->canvas, 1), 0, "the item plane");
	c->hd_ui = &hd;
	kp_canvas_compose(c, u->ui, 854, 545, NULL, 0, KP_LAYOUT_WIDE);
	kp_t_ok(T, c->arrangement.filler_count >= 1 && c->arrangement.fillers[0].x == 138
		&& c->arrangement.fillers[0].x + c->arrangement.fillers[0].w == 292, "the gap between the minimap and status");
	for (x = 138; x < 292; x++) {
		kp_t_eq(T, filler_item(c, x, &column), 0, "x %d: the minimap's piece", (int)x);
		kp_t_eq(T, column, -1, "x %d: the piece itself, no column stretched", (int)x);
	}
	/* wider, the gap outgrows the piece: its column stretched from where the piece ends (x 386.5) */
	kp_canvas_compose(c, u->ui, 1280, 545, NULL, 0, KP_LAYOUT_WIDE);
	kp_t_ok(T, filler_item(c, 386, &column) == 0 && column == -1, "the piece's last pixel");
	kp_t_ok(T, filler_item(c, 387, &column) == 0 && column == 70, "the column past it");
}

/* The HD image log keys its images by a surface's pixels pointer: a destroyed dialog's bitmap is freed and the next
 * dialog's may be the same address, so its buttons came back as ghost frames in the next menu's empty slots. */
static int32_t images_on(const KpUi *ui, const uint8_t *surface)
{
	int32_t k, n = 0;
	for (k = 0; k < ui->image_log.count; k++) n += ui->image_log.images[k].surface == surface;
	return n;
}

static void destroyed_dialog_leaves_no_image(KpT *T)
{
	KpOracleUi *u;
	const uint8_t *bitmap;
	int32_t d;
	if (!kp_t_ready(T, "test_hud.destroyed_dialog_leaves_no_image", UI_FILES)) return;
	u = kp_t_ui(T, 0);
	kp_ui_font_set(u->ui, 1);
	kp_t_ok(T, kp_ui_apply_theme(u->ui, kp_oracle_race_theme(2)) == 0, "ui: no protoss GRP");
	kp_ui_set_hd(u->ui, 1);
	d = kp_ui_create_menu_button(u->ui);
	kp_t_ok(T, d >= 0, "%s", u->ui->error);
	kp_ui_paint_dialog(u->ui, d);
	bitmap = u->ui->controls[d].bitmap.pixels;
	kp_t_ok(T, images_on(u->ui, bitmap) > 0, "the button's art is logged on the dialog's bitmap");
	kp_ui_destroy_dialog(u->ui, d);
	kp_t_eq(T, images_on(u->ui, bitmap), 0, "the freed bitmap's images went");
}

/* A wide layout's game view keeps its view coordinates, which reach into the console dialogs' original rectangles
 * (the status panel's, the minimap's, the command card's): a left press there starts the drag box (its cursor 9) as
 * anywhere else on the game view, not the hidden dialog's. */
static void wide_game_view_clicks_reach_the_game(KpT *T)
{
	/* inside statdata's {138, 388, 407, 479}, the minimap's {0, 315, 137, 479}, the card's {496, 354, 639, 479}; then
	 * a point outside every original rectangle */
	static const int32_t POINTS[4][2] = { { 250, 430 }, { 60, 400 }, { 560, 420 }, { 700, 430 } };
	Hud *H;
	Frame f;
	int k;
	if (!ready(T, "test_hud.wide_game_view_clicks_reach_the_game", TERRAN01)) return;
	H = with_hud(T, terran01(T), 1280, 720);
	f = frame(H, 1280, 720, 1016, 0, NO_CURSOR, CAMERA(0, 0));
	kp_t_eq_str(T, kp_layout_name(H->hud.canvas->arrangement.layout), "wide", NULL);
	for (k = 0; k < 4; k++) {
		input(H, MOUSE_MOVE, POINTS[k][0], POINTS[k][1], f.x, f.y);
		input(H, LEFT_DOWN, POINTS[k][0], POINTS[k][1], f.x, f.y);
		kp_t_eq(T, H->ui->cursor, 9, "the drag box at %d, %d", (int)POINTS[k][0], (int)POINTS[k][1]);
		input(H, LEFT_UP, POINTS[k][0], POINTS[k][1], f.x, f.y);
	}
}

/* The SDL host's text (UTF-8) reaches the window as single-byte characters: Latin-1's code points, nothing for the
 * others, nor for a control character (a browser's Enter brings a carriage return as text too, whose WM_CHAR the key
 * gives). The window's characters land in the message line Enter opened: a carriage return would send it, the Escape
 * close it, the backspace take the 'b' back. */
static void text_reaches_the_window_as_bytes(KpT *T)
{
	Hud *H;
	int32_t edit;
	const uint8_t *text;
	if (!ready(T, "test_hud.text_reaches_the_window_as_bytes", TERRAN01)) return;
	H = with_hud(T, terran01(T), 1280, 720);
	frame(H, 1280, 720, 1016, 0, NO_CURSOR, CAMERA(0, 0));
	kp_t_ok(T, !kp_ui_edit_focused(H->ui), "no edit control has the focus before Enter");
	kp_hud_key(H->ui, 1, 0x0d, 0);
	kp_hud_key(H->ui, 0, 0x0d, 0);
	kp_t_ok(T, H->ui->hud.textbox_mode != 0, "Enter opens the message line");
	kp_t_ok(T, kp_ui_edit_focused(H->ui), "the line is typed into (the phones' soft keyboard)");
	kp_hud_text(H->ui, "a\303\251\342\202\254b\r\t\033\010");
	/* sequences cut short and a stray continuation byte: none, and the byte after each starts its own character */
	kp_hud_text(H->ui, "\303c\342\202d\200e");
	kp_t_ok(T, H->ui->hud.textbox_mode != 0, "no control character: the line stays open");
	edit = child_of(H->ui, H->ui->hud.textbox, 6);
	kp_t_ok(T, edit >= 0, "the message line's edit control");
	text = kp_ui_edit_buffer(H->ui, edit);
	kp_t_ok(T, text != NULL, "its text");
	kp_t_eq_str(T, (const char *)text, "a\351bcde", "97 233 98, then c, d and e");
	kp_hud_key(H->ui, 1, 0x1b, 0);
	kp_hud_key(H->ui, 0, 0x1b, 0);
	kp_t_ok(T, H->ui->hud.textbox_mode == 0, "Escape closes the line");
	kp_t_ok(T, !kp_ui_edit_focused(H->ui), "and the focus is gone with it");
}

/* The SDL host gives the window a held key's press, its repeats and its release, and a modifier's release when its
 * other side is up (its key events carry the repeats and the sides), so what is left to test is what the window does
 * with them (kp_hud_key): the key's byte held through its press and repeats until its release, the modifier byte of the
 * virtual key, and a focus that goes with a key down forgets it (WM_ACTIVATEAPP: kp_ui_set_active). */
static void held_keys_reach_the_window_at_their_repeats(KpT *T)
{
	Hud *H;
	KpUi *ui;
	if (!ready(T, "test_hud.held_keys_reach_the_window_at_their_repeats", TERRAN01)) return;
	H = with_hud(T, terran01(T), 1280, 720);
	ui = H->ui;
	frame(H, 1280, 720, 1016, 0, NO_CURSOR, CAMERA(0, 0));
	kp_hud_key(ui, 1, 'A', 0);
	kp_t_eq(T, ui->input.vk['A'], 1, "the press");
	kp_hud_key(ui, 1, 'A', 1);
	kp_t_eq(T, ui->input.vk['A'], 1, "a repeat");
	kp_hud_key(ui, 0, 'A', 0);
	kp_t_eq(T, ui->input.vk['A'], 0, "the release");
	kp_hud_key(ui, 1, 0x10, 0);
	kp_t_eq(T, ui->input.keys[0], 1, "Shift's byte");
	kp_t_eq(T, ui->input.vk[0x10], 1, NULL);
	kp_hud_key(ui, 0, 0x10, 0);
	kp_t_eq(T, ui->input.keys[0], 0, "Shift's release");
	kp_t_eq(T, ui->input.vk[0x10], 0, NULL);
	/* the focus goes with Shift down, and its release never comes */
	kp_hud_key(ui, 1, 0x10, 0);
	kp_ui_set_active(ui, 0);
	kp_ui_set_active(ui, 1);
	kp_t_eq(T, ui->input.keys[0], 0, "after the focus: the Shift held before it is forgotten");
	kp_t_eq(T, ui->input.vk[0x10], 0, NULL);
	kp_hud_key(ui, 1, 0x10, 0);
	kp_t_eq(T, ui->input.keys[0], 1, "a new press");
	kp_hud_key(ui, 0, 0x10, 0);
	kp_t_eq(T, ui->input.keys[0], 0, "and its release");
}

/* The window's character of a key follows its WM_KEYDOWN only when the pump translates it (no accelerator took the
 * key, no dialog closed on it: 0x6ce660), and only for the keys TranslateMessage gives a character of their own:
 * Enter's opens the message line, a letter's press brings none (the host's text does), and the Escape that closes a
 * credits screen over the line (its press: the credits dialog's handler 0x4149f0) leaves the line open, which a second
 * Escape's character closes. (No ACCL table takes one of the four keys, so a dialog closed on the press is the pump's
 * only untranslated case.) */
static void key_characters_follow_translated_keys(KpT *T)
{
	Hud *H;
	KpUi *ui;
	const char *name = NULL;
	const uint8_t *text;
	int32_t edit, credits;
	uint16_t mission;
	if (!ready(T, "test_hud.key_characters_follow_translated_keys", TERRAN01)) return;
	H = with_hud(T, terran01(T), 1280, 720);
	ui = H->ui;
	frame(H, 1280, 720, 1016, 0, NO_CURSOR, CAMERA(0, 0));
	kp_hud_key(ui, 1, 0x0d, 0);
	kp_hud_key(ui, 0, 0x0d, 0);
	kp_t_ok(T, ui->hud.textbox_mode != 0, "the translated Enter's character 13 opens the message line");
	edit = child_of(ui, ui->hud.textbox, 6);
	kp_t_ok(T, edit >= 0, "the message line's edit control");
	kp_hud_key(ui, 1, 'A', 0);
	kp_hud_key(ui, 0, 'A', 0);
	text = kp_ui_edit_buffer(ui, edit);
	kp_t_eq_str(T, text ? (const char *)text : "(none)", "", "a letter's press brings no character");
	for (mission = 1; mission < 64 && name == NULL; mission++) name = kp_ui_establishing_text(ui, mission, 0);
	kp_t_ok(T, name != NULL, "an establishing text");
	credits = kp_ui_credits_open(ui, name);
	kp_t_ok(T, credits >= 0 && ui->open == credits, "the credits screen over the console");
	kp_hud_key(ui, 1, 0x1b, 0);
	kp_hud_key(ui, 0, 0x1b, 0);
	kp_t_ok(T, ui->credits.done && ui->open != credits, "Escape closed the credits");
	kp_t_ok(T, ui->hud.textbox_mode != 0, "no character after the key the credits closed on: the line stays open");
	kp_hud_key(ui, 1, 0x1b, 0);
	kp_hud_key(ui, 0, 0x1b, 0);
	kp_t_eq(T, ui->hud.textbox_mode, 0, "the next Escape's character closes the line");
}

/* A gesture keeps the coordinates of where it began on a wide screen: a drag the minimap holds goes on in its
 * coordinates off the panel (above the minimap: its top row, the view at the map's top), and a click on a text line
 * over the game view reaches the game (the text is not the console's art). */
static void wide_gestures_keep_their_panel(KpT *T)
{
	Hud *H;
	Frame f;
	Input r;
	const KpUiControl *k;
	int32_t minimap, px = 0, py = 0, x, y, hx = -1, hy = -1;
	if (!ready(T, "test_hud.wide_gestures_keep_their_panel", TERRAN01)) return;
	H = with_hud(T, terran01(T), 1280, 720);
	f = frame(H, 1280, 720, 1016, 0, NO_CURSOR, CAMERA(0, 0));
	minimap = dialog_of(H->ui, "rez\\minimap.bin", 0);
	kp_t_ok(T, minimap >= 0 && kp_canvas_place(H->hud.canvas, minimap, &px, &py), "the minimap placed");
	k = &H->ui->controls[minimap];
	x = px + 70 - k->left; /* the minimap image's bottom rows: the view at the map's bottom */
	y = py + 470 - k->top;
	input(H, MOUSE_MOVE, x, y, f.x, f.y);
	r = input(H, LEFT_DOWN, x, y, f.x, f.y);
	kp_t_ok(T, r.moved && r.y > 0, "the press moved the view down");
	y = py + (k->top - 30) - k->top; /* off the panel, above it */
	r = input(H, MOUSE_MOVE, x, y, f.x, r.y);
	kp_t_ok(T, r.moved, "the drag above the minimap moves the view");
	kp_t_eq(T, r.y, 0, "the drag above the minimap: its top row");
	input(H, LEFT_UP, x, y, f.x, 0);
	/* a text line over the game view: a press on one of its pixels starts the drag box */
	hud_event(H, KP_EVENT_ERROR, H->g->local_player, 1, 0);
	frame(H, 1280, 720, 1032, 0, NO_CURSOR, CAMERA(0, 0));
	for (y = 0; y < H->hud.canvas->arrangement.top && hx < 0; y++) {
		for (x = 0; x < 1280; x++) {
			if (alpha_at(H->hud.canvas, x, y) == OVERLAY_A) {
				hx = x;
				hy = y;
				break;
			}
		}
	}
	kp_t_ok(T, hx >= 0, "the error line drawn");
	input(H, MOUSE_MOVE, hx, hy, 0, 0);
	input(H, LEFT_DOWN, hx, hy, 0, 0);
	kp_t_eq(T, H->ui->cursor, 9, "the drag box from the text line's pixel");
	input(H, LEFT_UP, hx, hy, 0, 0);
}

/* The game screen's drawing after the fog (its layer's callback 0x445ea0) on the canvas: the drag box while the
 * button is down (DrawDragBox 0x4bcc40) and the command marker while it shows (DrawCommandMarker 0x418be0: the marker
 * sprite is hidden, so the world's draw list leaves it out), both gone once they end. */
static void drag(KpT *T, Hud *H, int64_t cx, int64_t cy)
{
	input(H, LEFT_DOWN, 100, 60, cx, cy);
	input(H, MOUSE_MOVE, 300, 200, cx, cy);
	frame(H, 1280, 720, 1048, 0, CURSOR(300, 200), CAMERA(cx, cy));
	kp_t_eq(T, alpha_at(H->hud.canvas, 100, 60), OVERLAY_A, "a box again");
}

static void canvas_draws_the_drag_box_and_the_command_marker(KpT *T)
{
	KpOracleGame *og;
	KpRunner *run;
	Hud *H;
	Frame f;
	const KpCanvas *c;
	uint8_t held[3];
	int64_t step;
	if (!ready(T, "test_hud.canvas_draws_the_drag_box_and_the_command_marker", TERRAN01)) return;
	og = terran01(T);
	run = runner(T, og->g);
	H = with_hud(T, og, 1280, 720);
	f = frame(H, 1280, 720, 1016, 0, NO_CURSOR, CAMERA(0, 0));
	c = H->hud.canvas;
	input(H, MOUSE_MOVE, 100, 60, f.x, f.y);
	input(H, LEFT_DOWN, 100, 60, f.x, f.y);
	input(H, MOUSE_MOVE, 300, 200, f.x, f.y);
	frame(H, 1280, 720, 1032, 0, CURSOR(300, 200), CAMERA(f.x, f.y));
	kp_t_eq(T, alpha_at(c, 100, 60), OVERLAY_A, "the box's corner");
	kp_t_eq(T, alpha_at(c, 200, 60), OVERLAY_A, "its top edge");
	kp_t_eq(T, alpha_at(c, 300, 130), OVERLAY_A, "its right edge");
	kp_t_eq(T, alpha_at(c, 200, 130), 0, "inside: the world");
	kp_t_eq(T, index_at(c, 300, 200), index_at(c, 100, 60), "one colour");
	input(H, LEFT_UP, 300, 200, f.x, f.y);
	frame(H, 1280, 720, 1048, 0, CURSOR(300, 200), CAMERA(f.x, f.y));
	kp_t_eq(T, overlay_in(c, 100, 60, 300, 200), 0, "the box ends with the button");
	/* the focus goes while the button is down: no release comes (OnCaptureChanged 0x43f450) */
	drag(T, H, f.x, f.y);
	kp_hud_capture_lost(&H->hud);
	frame(H, 1280, 720, 1048, 0, CURSOR(300, 200), CAMERA(f.x, f.y));
	kp_t_eq(T, overlay_in(c, 100, 60, 300, 200), 0, "the box ends with the window's mouse");
	kp_t_eq(T, H->hud.buttons, 0, "no button down");
	/* the mouse state says no button while a box is on: the game screen's draw ends it (DrawDragBox 0x4bcc40) */
	drag(T, H, f.x, f.y);
	memcpy(held, H->ui->input.keys, sizeof held);
	kp_ui_set_keys(H->ui, held[0], held[1], held[2], 0);
	frame(H, 1280, 720, 1048, 0, CURSOR(300, 200), CAMERA(f.x, f.y));
	kp_t_eq(T, overlay_in(c, 100, 60, 300, 200), 0, "the box ends with no button down");
	kp_ui_set_keys(H->ui, held[0], held[1], held[2], H->hud.buttons);
	frame(H, 1280, 720, 1048, 0, CURSOR(300, 200), CAMERA(f.x, f.y));
	kp_t_eq(T, overlay_in(c, 100, 60, 300, 200), 0, "ended, not hidden: the button again shows no box");
	input(H, LEFT_UP, 300, 200, f.x, f.y);
	/* the marker at a map point of the view (MovePlacementCursor 0x417ad0) */
	kp_mark_target(og->g, f.x + 400, f.y + 150, 0);
	frame(H, 1280, 720, 1064, 0, CURSOR(300, 200), CAMERA(f.x, f.y));
	kp_t_ok(T, overlay_in(c, 400 - 20, 150 - 16, 400 + 20, 150 + 16) > 50, "the marker drawn around its point");
	for (step = 2; step <= 40; step++) kp_runner_to(run, step); /* its animation ends (opcode 0x2d clears its byte) */
	frame(H, 1280, 720, 1080, 0, CURSOR(300, 200), CAMERA(f.x, f.y));
	kp_t_eq(T, overlay_in(c, 400 - 20, 150 - 16, 400 + 20, 150 + 16), 0, "the marker gone");
}

/* A click on a lone producer of the local player's shows its rally point (FlashRallyPoint 0x41b250 behind
 * IsRallyProducer 0x429250): the marker moves to the point (HighlightRightClickTarget 0x47fc10); a rally point on the
 * producer's own position shows nothing. */
static void click(Hud *H, int64_t px, int64_t py, int64_t cx, int64_t cy, uint32_t now)
{
	input(H, MOUSE_MOVE, px, py, cx, cy);
	input(H, LEFT_DOWN, px, py, cx, cy);
	input(H, LEFT_UP, px, py, cx, cy);
	frame(H, 1280, 720, now, 0, CURSOR(px, py), CAMERA(cx, cy));
}

static void click_on_a_producer_flashes_its_rally_point(KpT *T)
{
	KpGame *g;
	Hud *H;
	int64_t n, cc = 0, x, y, max_y, cx, cy, px, py, rx, ry;
	if (!ready(T, "test_hud.click_on_a_producer_flashes_its_rally_point", TERRAN01)) return;
	H = with_hud(T, terran01(T), 1280, 720);
	g = H->g;
	for (n = 1; n <= KP_UNITS && cc == 0; n++) {
		if (g->u.type[n] == COMMAND_CENTER && g->u.owner[n] == g->local_player && g->u.hp[n] > 0) cc = n;
	}
	kp_t_ok(T, cc != 0, "a command center of the local player's");
	x = g->u.x[cc];
	y = g->u.y[cc];
	max_y = g->map_h_px - 12 * 32 - (640 - 400) + 8; /* the wide view's scroll limit */
	cx = x - 400;
	cy = min64(y - 250, max_y - max_y % 8);
	frame(H, 1280, 720, 1000, 0, NO_CURSOR, CAMERA(0, 0)); /* the start location's view first */
	frame(H, 1280, 720, 1016, 0, NO_CURSOR, CAMERA(cx, cy));
	px = x - cx; /* the producer on the canvas */
	py = y - cy;
	rx = px + 150;
	ry = py;
	kp_t_ok(T, px > 0 && rx < 1280 && py > 0 && py < 400, "the producer and its rally point in the view");
	g->u.rally_x[cc] = x;
	g->u.rally_y[cc] = y;
	click(H, px, py, cx, cy, 1032);
	kp_t_eq(T, g->local_selection[0], cc, "the producer selected");
	kp_t_eq(T, overlay_in(H->hud.canvas, rx - 20, ry - 16, rx + 20, ry + 16), 0, "a rally point on the producer: no marker");
	g->u.rally_x[cc] = x + 150;
	g->u.rally_y[cc] = y;
	click(H, px, py, cx, cy, 1048);
	kp_t_ok(T, overlay_in(H->hud.canvas, rx - 20, ry - 16, rx + 20, ry + 16) > 50, "the marker at the rally point");
}

/* The music's toggle (40038, ToggleMusicMute 0x49ec00 into ApplyVolumeOptions 0x49ea60) silences and closes the
 * stream (CloseMusic 0x46f290), and back on plays the current track again (ResumeMusic 0x46f160); the game's end
 * stops the music (EndGame 0x443db0: StopAllSoundsAndMusic 0x49db40), so the next screen's track starts. */
enum { MUSIC = 3, MUSIC_VOLUME = 4, MUSIC_STOP = 5, SILENT = -10000 };

static int music(KpUi *ui, KpUiSoundCommand *out, int max)
{
	KpUiSoundCommand c[KP_UI_SOUND_COMMANDS];
	size_t n = kp_ui_take_sound_commands(ui, c, KP_UI_SOUND_COMMANDS), k;
	int count = 0;
	for (k = 0; k < n; k++) {
		if (c[k].channel == -1 && c[k].type >= MUSIC && c[k].type <= MUSIC_STOP && count < max) out[count++] = c[k];
	}
	return count;
}

static void music_stops_with_its_option_and_the_game(KpT *T)
{
	Hud *H;
	KpUi *ui;
	KpUiSoundCommand m[16];
	uint16_t track;
	int n;
	if (!ready(T, "test_hud.music_stops_with_its_option_and_the_game", TERRAN01)) return;
	H = with_hud(T, terran01(T), 1280, 720);
	ui = H->ui;
	ui->sound.device = 1;
	ui->sound.effects = 50;
	n = music(ui, m, 16);
	kp_t_eq(T, n, 1, "the game's track");
	kp_t_eq(T, m[0].type, MUSIC, NULL);
	track = m[0].sound;
	kp_t_ui_input(ui, COMMAND, 40038, 0);
	n = music(ui, m, 16);
	kp_t_eq(T, n, 2, "off: two commands");
	kp_t_eq(T, m[0].type, MUSIC_VOLUME, NULL);
	kp_t_eq(T, m[0].volume, SILENT, "the silence");
	kp_t_eq(T, m[1].type, MUSIC_STOP, "the stream closed");
	kp_t_ui_input(ui, COMMAND, 40038, 0);
	n = music(ui, m, 16);
	kp_t_ok(T, n >= 1 && m[0].type == MUSIC, "on: the stream opens again (the volume waits for it)");
	kp_t_eq(T, m[0].sound, track, "the same track");
	kp_ui_end_game(ui);
	n = music(ui, m, 16);
	kp_t_eq(T, n, 1, "the game's end");
	kp_t_eq(T, m[0].type, MUSIC_STOP, "stops the music");
}

/* The Center View trigger's camera pan (StartCameraPan 0x49be10) to a location by the map's bottom right corner: its
 * target, made for the original's 640 x 400 view, lies past a wider view's scroll limits; the pan reaches the view's
 * own target (the location at the view's centre, within the limits), its arrival releases the trigger's wait, and
 * after EndCameraPan 0x49be50 the keys scroll the map again. The original's view keeps the trigger's target. */
typedef struct {
	int steps, moved;
	int64_t x, y;
} Pan;

static Pan pan(KpUi *ui, int64_t tx, int64_t ty, int32_t w, int32_t h, int32_t sx, int32_t sy)
{
	Pan p;
	int k;
	memset(&p, 0, sizeof p);
	kp_ui_set_view(ui, w, h);
	kp_ui_set_screen(ui, sx, sy);
	kp_ui_take_release(ui);
	game_event(ui, KP_EVENT_CAMERA, tx, ty, 0);
	for (k = 1; k <= 400; k++) {
		kp_ui_update_scroll(ui, 320, 240);
		if (kp_ui_take_release(ui)) {
			p.steps = k;
			p.moved = kp_hud_take_camera(ui, &p.x, &p.y);
			return p;
		}
	}
	return p;
}

static void camera_pan_arrives_in_a_wide_view(KpT *T)
{
	Hud *H;
	KpUi *ui;
	KpGame *g;
	Pan p;
	int64_t tx, ty, bottom, left = 0, ex = 0, ey = 0;
	int k, moved;
	if (!ready(T, "test_hud.camera_pan_arrives_in_a_wide_view", TERRAN01)) return;
	H = with_hud(T, terran01(T), 1280, 720);
	ui = H->ui;
	g = H->g;
	tx = g->map_w_px - 641; /* TrigActCenterView 0x4b6350's clamp at the corner */
	ty = g->map_h_px - 401;
	p = pan(ui, tx, ty, 1280, 640, 0, 0);
	kp_t_ok(T, p.steps != 0 && p.moved, "the wide view's pan arrives and releases the trigger's wait");
	kp_t_eq(T, p.x, (g->map_w_px - 1280) - (g->map_w_px - 1280) % 8, "at the view's right limit");
	bottom = g->map_h_px - 12 * 32 - (640 - 400) + 8; /* InitScreenScroll's limit for the taller view */
	kp_t_eq(T, p.y, bottom - bottom % 8, "at the view's bottom limit");
	game_event(ui, KP_EVENT_CAMERA, 0, 0, 1); /* the trigger's next run: EndCameraPan */
	kp_t_ui_input(ui, KEY_DOWN, 0x25, 0);
	for (k = 0; k < 20; k++) kp_ui_update_scroll(ui, 320, 240);
	kp_t_ui_input(ui, KEY_UP, 0x25, 0);
	moved = kp_hud_take_camera(ui, &left, &ey);
	kp_t_ok(T, moved && left < p.x, "the keys scroll the map again");
	p = pan(ui, tx, ty, 640, 400, 0, 0);
	kp_t_ok(T, p.steps != 0 && p.moved, "the original's view's pan arrives");
	kp_t_eq(T, p.x, tx - tx % 8, "at the trigger's target");
	kp_t_eq(T, p.y, ty - ty % 8, NULL);
	/* a view whose sizes are no multiples of 8 (a browser window's), scrolled by the keys to its limits: the limits
	 * keep the original's 8-pixel grid (the pan's steps are 8 pixels or more), so the pan from there arrives too */
	game_event(ui, KP_EVENT_CAMERA, 0, 0, 1);
	kp_ui_set_view(ui, 1283, 643);
	kp_ui_set_screen(ui, 0, 0);
	kp_t_ui_input(ui, KEY_DOWN, 0x23, 0); /* End: down and left; then Page Down: down and right */
	for (k = 0; k < 400; k++) kp_ui_update_scroll(ui, 320, 240);
	kp_t_ui_input(ui, KEY_UP, 0x23, 0);
	kp_t_ui_input(ui, KEY_DOWN, 0x22, 0);
	for (k = 0; k < 400; k++) kp_ui_update_scroll(ui, 320, 240);
	kp_t_ui_input(ui, KEY_UP, 0x22, 0);
	kp_t_ok(T, kp_hud_take_camera(ui, &ex, &ey), "the keys moved the view");
	kp_t_eq(T, ex % 8, 0, "the right limit on the 8-pixel grid");
	kp_t_eq(T, ey % 8, 0, "the bottom limit on the 8-pixel grid");
	kp_t_ok(T, ex > g->map_w_px - 1283 - 8 && ey > g->map_h_px - 12 * 32 - 243 + 8 - 8, "at the limits");
	p = pan(ui, tx, ty, 1283, 643, (int32_t)ex, (int32_t)ey);
	kp_t_ok(T, p.steps != 0, "the pan from the limits arrives and releases the trigger's wait");
}

/* The interface's pending ends of the trigger waits that a save keeps (kp_session.h's PRES): a trigger portrait's
 * milliseconds left and a camera pan's arrival; after a load replaced the game they end without releasing its waits. */
static void pending_releases_reach_the_save(KpT *T)
{
	Hud *H;
	KpUi *ui;
	KpGame *g;
	uint32_t now = 1016, ms = 0;
	int64_t marine;
	int camera = 0, portrait, released = 0, k;
	if (!ready(T, "test_hud.pending_releases_reach_the_save", TERRAN01)) return;
	H = with_hud(T, terran01(T), 1280, 720);
	ui = H->ui;
	g = H->g;
	frame(H, 1280, 720, now, 0, NO_CURSOR, CAMERA(0, 0));
	portrait = kp_ui_pending_releases(ui, &ms, &camera);
	kp_t_ok(T, !portrait && !camera, "none at the start");
	for (marine = 1; marine <= KP_UNITS && g->u.sprite[marine] == -1; marine++) {
	}
	kp_t_ok(T, marine <= KP_UNITS, "a unit");
	game_event(ui, KP_EVENT_PORTRAIT, g->u.type[marine], 3000, 0); /* TrigActTalkingPortrait: the wait's end in 3 s */
	game_event(ui, KP_EVENT_CAMERA, g->map_w_px - 641, g->map_h_px - 401, 0);
	now += 1000;
	frame(H, 1280, 720, now, 0, NO_CURSOR, KEEP);
	portrait = kp_ui_pending_releases(ui, &ms, &camera);
	kp_t_ok(T, portrait && ms > 1900 && ms <= 2000, "the portrait's time left: %u", (unsigned)ms);
	kp_t_ok(T, camera, "the pan's arrival");
	kp_ui_game_loaded(ui); /* a load */
	portrait = kp_ui_pending_releases(ui, &ms, &camera);
	kp_t_ok(T, !portrait && !camera, "forgotten");
	for (k = 0; k < 100; k++) {
		Frame f;
		now += 100;
		f = frame(H, 1280, 720, now, 0, CURSOR(640, 360), KEEP);
		released = released || f.released;
		kp_ui_update_scroll(ui, 320, 240);
		released = released || kp_ui_take_release(ui);
	}
	kp_t_ok(T, !released, "nothing releases the loaded game's waits");
}

/* A loaded save's pending ends of its trigger waits (the save's PRES): the pan's arrival releases the wait in the load's
 * frame, the portrait's end after its milliseconds left, each once; a save made again keeps the portrait's time. */
static void a_load_restores_the_pending_releases(KpT *T)
{
	Hud *H;
	KpUi *ui;
	Frame f;
	uint32_t now = 1016, ms = 0;
	int camera = 0, k, released = 0;
	if (!ready(T, "test_hud.a_load_restores_the_pending_releases", TERRAN01)) return;
	H = with_hud(T, terran01(T), 1280, 720);
	ui = H->ui;
	frame(H, 1280, 720, now, 0, NO_CURSOR, CAMERA(0, 0));
	kp_ui_game_loaded(ui);
	kp_ui_restore_releases(ui, 0, 0, 1);
	f = frame(H, 1280, 720, now += 16, 0, NO_CURSOR, KEEP);
	kp_t_ok(T, f.released, "the pan's arrival in the load's frame");
	f = frame(H, 1280, 720, now += 16, 0, NO_CURSOR, KEEP);
	kp_t_ok(T, !f.released, "once");
	kp_ui_game_loaded(ui);
	kp_ui_restore_releases(ui, 1, 500, 0);
	kp_t_ok(T, kp_ui_pending_releases(ui, &ms, &camera) && ms <= 500 && ms > 400 && !camera,
		"a save made again keeps the portrait's time: %u", (unsigned)ms);
	f = frame(H, 1280, 720, now += 100, 0, NO_CURSOR, KEEP);
	kp_t_ok(T, !f.released, "the portrait's time not over");
	for (k = 0; k < 10; k++) {
		f = frame(H, 1280, 720, now += 100, 0, NO_CURSOR, KEEP);
		released += f.released;
	}
	kp_t_eq(T, released, 1, "the portrait's end releases the wait once");
	kp_ui_game_loaded(ui);
	kp_ui_restore_releases(ui, 1, 0, 0);
	f = frame(H, 1280, 720, now += 16, 0, NO_CURSOR, KEEP);
	kp_t_ok(T, f.released, "a portrait with no time left: at once");
	k = ui->portrait.control;
	ui->portrait.control = -1; /* no portrait whose timer could end the wait */
	kp_ui_game_loaded(ui);
	kp_ui_restore_releases(ui, 1, 500, 0);
	f = frame(H, 1280, 720, now += 16, 0, NO_CURSOR, KEEP);
	ui->portrait.control = k;
	kp_t_ok(T, f.released, "no portrait control: the wait ends at once, not never");
}

/* A load starts the resource panel's animated counts from 0, as the original's console made after the load does
 * (CreateResourceDialog 0x4a78f0), not from the game's before the load. */
static void a_load_starts_the_resource_counts(KpT *T)
{
	Hud *H;
	if (!ready(T, "test_hud.a_load_starts_the_resource_counts", TERRAN01)) return;
	H = with_hud(T, terran01(T), 1280, 720);
	frame(H, 1280, 720, 1016, 0, NO_CURSOR, CAMERA(0, 0));
	H->ui->hud.shown_minerals = 3000;
	H->ui->hud.shown_gas = 700;
	kp_ui_game_loaded(H->ui);
	kp_t_ok(T, H->ui->hud.shown_minerals == 0 && H->ui->hud.shown_gas == 0, "from 0: %d, %d",
		(int)H->ui->hud.shown_minerals, (int)H->ui->hud.shown_gas);
}

/* The SCV's build card by its hotkeys (B, then R or S) as kp_ui's events, each press released by the frames after it
 * (SchedulePressRelease 0x4c3570). */
typedef struct {
	Hud *H;
	uint32_t now;
	int idle; /* the pump's idle event before each frame */
} Keys;

static void key_frames(Keys *k)
{
	int n;
	for (n = 0; n < 3; n++) {
		k->now += 300;
		if (k->idle) kp_t_ui_idle(k->H->ui);
		frame(k->H, 1280, 720, k->now, 0, CURSOR(600, 300), KEEP);
	}
}

static void keys(Keys *k, const uint16_t (*list)[2], int count)
{
	int n;
	for (n = 0; n < count; n++) {
		type_key(k->H->ui, list[n][0], list[n][1]);
		key_frames(k);
	}
}

static const uint16_t KEY_B[1][2] = { { 66, 98 } };
static const uint16_t KEY_M[1][2] = { { 77, 109 } };
static const uint16_t KEYS_BR[2][2] = { { 66, 98 }, { 82, 114 } };
static const uint16_t KEYS_BRR[3][2] = { { 66, 98 }, { 82, 114 }, { 82, 114 } };

/* A load during a placement (ResetPerGameState 0x444230, InitPlacementLayers 0x47d000): the placement closes with its
 * ghosts on the Geysers, and the interface leaves the mode. */
static int ghosts(KpGame *g, const int64_t *geysers, int count)
{
	const KpImages *w = g->w;
	int k, n = 0;
	for (k = 0; k < count; k++) {
		int32_t i = w->spr.images[g->u.sprite[geysers[k]]].head;
		while (i != -1) {
			n += w->img.id[i] == PLACEMENT_GHOST;
			i = w->img.next[i];
		}
	}
	return n;
}

static void a_load_ends_the_placement(KpT *T)
{
	Hud *H;
	KpGame *g;
	Keys k;
	int64_t scv, geysers[64], n;
	int count = 0;
	if (!ready(T, "test_hud.a_load_ends_the_placement", ADDON)) return;
	H = with_hud(T, kp_t_game(T, ADDON, 1, 0, NULL), 1280, 720);
	g = H->g;
	scv = own_unit(g, 0, SCV, 0);
	for (n = 1; n <= KP_UNITS; n++) {
		if (g->u.sprite[n] != -1 && g->u.type[n] == GEYSER && count < 64) geysers[count++] = n;
	}
	kp_t_ok(T, scv != 0 && count > 0, "an SCV and the Geysers");
	g->players.minerals[g->local_player] = 1000;
	kp_select_locally(g, &scv, 1);
	memset(&k, 0, sizeof k);
	k.H = H;
	k.now = 1316;
	frame(H, 1280, 720, k.now, 1, CURSOR(600, 300), CAMERA(0, 0));
	keys(&k, KEYS_BRR, 3); /* B, then R twice: the build card shows by the first R, the second starts the placement */
	kp_t_eq(T, ghosts(g, geysers, count), count, "the placement's ghosts on the Geysers");
	kp_ui_game_loaded(H->ui);
	k.now += 300;
	frame(H, 1280, 720, k.now, 0, CURSOR(600, 300), KEEP);
	kp_t_eq(T, ghosts(g, geysers, count), 0, "the load ends the placement and its ghosts");
	kp_t_ok(T, !kp_ui_game_view_active(H->ui), "and the preview");
}

/* A loaded save's view (LoadGameFromSave 0x499890's SetScreenPosition 0x40b450): the next frame's camera whatever
 * the host's was, clamped to the map and aligned to 8 pixels. */
static void a_load_places_the_saved_view(KpT *T)
{
	Hud *H;
	Frame f;
	int64_t x = 301, y = 403;
	uint32_t now = 1016;
	if (!ready(T, "test_hud.a_load_places_the_saved_view", TERRAN01)) return;
	H = with_hud(T, terran01(T), 1280, 720);
	frame(H, 1280, 720, now, 0, NO_CURSOR, CAMERA(0, 0));
	kp_ui_game_loaded(H->ui);
	kp_hud_place_camera(&H->hud, &x, &y);
	kp_t_ok(T, x == 296 && y == 400, "aligned to 8 pixels: %lld, %lld", (long long)x, (long long)y);
	f = frame(H, 1280, 720, now += 16, 0, NO_CURSOR, CAMERA(0, 0)); /* the host's camera still the view before */
	kp_t_ok(T, f.moved && f.x == 296 && f.y == 400, "the saved view: moved %d to %lld, %lld", f.moved, (long long)f.x,
		(long long)f.y);
	f = frame(H, 1280, 720, now += 16, 0, NO_CURSOR, CAMERA(296, 400));
	kp_t_ok(T, !f.moved && f.x == 296 && f.y == 400, "once");
	x = y = 1 << 20;
	kp_hud_place_camera(&H->hud, &x, &y);
	kp_t_ok(T, x == H->g->map_w_px - H->ui->view_w && y < H->g->map_h_px && y > 0, "clamped to the map: %lld, %lld",
		(long long)x, (long long)y);
}

static int lines_shown(const KpUi *ui)
{
	int k;
	for (k = 0; k < KP_UI_LINES; k++) {
		if (ui->lines.text[k][0] != 0) return 1;
	}
	return 0;
}

/* A load's minimap shows the loaded game at once (its console made after the load in the original), not the game before
 * it until the redraw timer: here every tile becomes seen and the minimap takes it in the load's frame. The game before's
 * text lines go with it (InitGameWorld 0x443790's ResetTextLines 0x46ae30). */
static void a_load_redraws_the_minimap(KpT *T)
{
	Hud *H;
	KpGame *g;
	KpUiMinimap *m;
	uint8_t *before;
	size_t n;
	int64_t k, hidden = (int64_t)1 << 8 | 1; /* the local player's not-explored and not-visible bits */
	uint32_t now = 1016;
	if (!ready(T, "test_hud.a_load_redraws_the_minimap", TERRAN01)) return;
	H = with_hud(T, terran01(T), 1280, 720);
	g = H->g;
	m = &H->ui->minimap;
	frame(H, 1280, 720, now, 0, NO_CURSOR, CAMERA(0, 0));
	kp_t_ok(T, m->image != NULL, "the minimap's image");
	n = (size_t)m->w * (size_t)m->h;
	before = (uint8_t *)malloc(n);
	kp_t_ok(T, before != NULL, "out of memory");
	kp_t_defer(T, free, before);
	memcpy(before, m->image, n);
	hidden <<= g->local_player;
	for (k = 0; k < g->map_w * g->map_h; k++) g->terrain.tiles[k] &= ~hidden;
	g->minimap_terrain++;
	kp_ui_minimap_ping(H->ui, 64, 64, 0);
	kp_ui_lines_add(H->ui, (const uint8_t *)"before the load", 1, now + 10000, 0);
	frame(H, 1280, 720, now += 16, 0, NO_CURSOR, KEEP);
	kp_t_ok(T, lines_shown(H->ui), "a text line");
	kp_t_ok(T, memcmp(before, m->image, n) == 0, "the redraw timer has not come");
	kp_ui_game_loaded(H->ui);
	kp_t_ok(T, memcmp(before, m->image, n) != 0, "the loaded game's minimap at once");
	kp_t_ok(T, !m->pings[0].active, "without the game before's pings");
	kp_t_ok(T, !lines_shown(H->ui), "nor its text lines");
}

/* A right click on the command card's panel ends the command mode wherever the layout puts the panel (GameRightClick
 * 0x448eb0 at the card's rectangle 0x5033d0, CancelCommandMode 0x446050): a build card goes back to the selection's
 * own, then a placement ends, then the targeting. */
static void right_click(KpT *T, Keys *k, int32_t card)
{
	KpUi *ui = k->H->ui;
	const KpUiControl *b = NULL;
	int32_t c, px = 0, py = 0, x, y;
	kp_t_ok(T, kp_canvas_place(k->H->hud.canvas, card, &px, &py), "the card placed");
	/* on a shown button: an empty slot is the card dialog's own (DialogDefaultInteract 0x4c1470) */
	for (c = ui->controls[card].child; c >= 0; c = ui->controls[c].next) {
		b = &ui->controls[c];
		if ((b->flags & SHOWN) != 0 && b->id >= 1 && b->id <= 9) break;
	}
	kp_t_ok(T, c >= 0, "a shown button");
	x = px + half(b->left + b->right); /* the button's rectangle is the dialog's */
	y = py + half(b->top + b->bottom);
	input(k->H, MOUSE_MOVE, x, y, 0, 0);
	input(k->H, RIGHT_DOWN, x, y, 0, 0);
	input(k->H, RIGHT_UP, x, y, 0, 0);
	key_frames(k);
}

static void right_click_on_the_card_ends_the_mode(KpT *T)
{
	Hud *H;
	KpUi *ui;
	KpGame *g;
	Keys k;
	int64_t scv;
	int32_t card, own;
	if (!ready(T, "test_hud.right_click_on_the_card_ends_the_mode", ADDON)) return;
	H = with_hud(T, kp_t_game(T, ADDON, 1, 0, NULL), 1280, 720);
	ui = H->ui;
	g = H->g;
	scv = own_unit(g, 0, SCV, 0);
	kp_t_ok(T, scv != 0, "an SCV");
	g->players.minerals[g->local_player] = 1000;
	kp_select_locally(g, &scv, 1);
	memset(&k, 0, sizeof k);
	k.H = H;
	k.now = 1316;
	k.idle = 1; /* the pump's idle event: a hotkey's pressed button fires (SchedulePressRelease 0x4c3570) */
	frame(H, 1280, 720, k.now, 1, CURSOR(600, 300), CAMERA(0, 0));
	card = dialog_of(ui, "rez\\statbtn", 1);
	kp_t_ok(T, card >= 0 && H->hud.canvas->arrangement.layout == KP_LAYOUT_WIDE, "the wide layout's command card");
	own = ui->card.set;
	keys(&k, KEY_B, 1); /* B: the build card */
	kp_t_ok(T, ui->card.set != own, "the build card");
	/* the console art off the panels at the card rectangle's logical point: the logical screen's coordinates, not
	 * the card's (the wide layout's card lies elsewhere), and the normal mode's right click over the art does nothing */
	kp_ui_set_over_console(ui, 1);
	kp_t_ui_input(ui, MOUSE_MOVE, 560, 400);
	kp_t_ui_input(ui, RIGHT_DOWN, 560, 400);
	kp_t_ui_input(ui, RIGHT_UP, 560, 400);
	kp_t_ok(T, ui->card.set != own, "the console art's right click keeps the build card");
	right_click(T, &k, card);
	kp_t_eq(T, ui->card.set, own, "back to the selection's card");
	keys(&k, KEYS_BR, 2); /* B, R: the Refinery's placement */
	kp_t_ok(T, kp_ui_game_view_active(ui), "the placement");
	right_click(T, &k, card);
	kp_t_ok(T, !kp_ui_game_view_active(ui), "the placement ends");
	keys(&k, KEY_M, 1); /* M: the move's targeting */
	kp_t_ok(T, ui->input.targeting != 0, "the targeting");
	right_click(T, &k, card);
	kp_t_eq(T, ui->input.targeting, 0, "the targeting ends");
}

/* Portrait 82 (the Flag's) takes its sprite owner's colour (ShowPortrait 0x4a6f10, MapPlayerColor 0x41d560): 82 on
 * for the players' colours, 0x62 on for a colour of 8 or more. */
static int64_t portrait_of(Hud *H, int64_t flag, int64_t other, int32_t owner, uint32_t *now)
{
	KpGame *g = H->g;
	const KpUiPortrait *p = &H->ui->portrait;
	g->w->spr.owner[g->u.sprite[flag]] = owner;
	kp_select_locally(g, &other, 1);
	*now += 100;
	frame(H, 1280, 720, *now, 1, NO_CURSOR, CAMERA(0, 0));
	kp_select_locally(g, &flag, 1);
	*now += 100;
	frame(H, 1280, 720, *now, 1, NO_CURSOR, CAMERA(0, 0));
	return p->control >= 0 ? H->ui->controls[p->control].wuser : 0xffff;
}

static void flag_portrait_takes_its_owners_colour(KpT *T)
{
	Hud *H;
	int64_t flag, other;
	uint32_t now = 1016;
	if (!ready(T, "test_hud.flag_portrait_takes_its_owners_colour", TERRAN01)) return;
	H = with_hud(T, terran01(T), 1280, 720);
	flag = own_unit(H->g, 1, -1, 0);
	other = own_unit(H->g, 1, -1, 1);
	kp_t_ok(T, flag != 0 && other != 0, "two units of the local player's");
	kp_t_eq(T, kp_t_dat(T, H->og, "units", "portrait", FLAG), 82, NULL);
	H->g->u.type[flag] = FLAG;
	kp_t_eq(T, portrait_of(H, flag, other, 0, &now), 82, "player 1's colour");
	kp_t_eq(T, portrait_of(H, flag, other, 5, &now), 87, "player 6's colour");
	kp_t_eq(T, portrait_of(H, flag, other, 11, &now), 0x62 + 11, "a neutral colour's row");
}

/* The status panel's tooltip follows a new layout under a still mouse (RefreshStatusHoverTooltip 0x4a2b70): a
 * Marine's weapon slot hovered shows its tooltip, the Supply Depot's layout has no such slot and the tooltip goes. */
static void select_unit(Hud *H, int64_t n, uint32_t *now)
{
	kp_select_locally(H->g, &n, 1);
	*now += 100;
	frame(H, 1280, 720, *now, 1, NO_CURSOR, CAMERA(0, 0));
}

static void shown_children(const KpUi *ui, int32_t d, uint8_t *out)
{
	int32_t c;
	memset(out, 0, KP_UI_CONTROLS);
	for (c = ui->controls[d].child; c >= 0; c = ui->controls[c].next) out[c] = (ui->controls[c].flags & SHOWN) != 0;
}

static int tooltip_shown(KpUi *ui)
{
	int32_t x, y, w = 0, h = 0;
	const KpUiSurface *t = kp_ui_tooltip(ui, &x, &y, &w, &h);
	return t != NULL && w > 0 && h > 0 && w <= t->w && h <= t->h;
}

static void status_tooltip_follows_the_panels_layout(KpT *T)
{
	static uint8_t depot_shows[KP_UI_CONTROLS], marine_shows[KP_UI_CONTROLS];
	Hud *H;
	KpUi *ui;
	int64_t marine, depot;
	int32_t panel, c, slot = -1, px = 0, py = 0;
	uint32_t now = 1016;
	if (!ready(T, "test_hud.status_tooltip_follows_the_panels_layout", TERRAN01)) return;
	H = with_hud(T, terran01(T), 1280, 720);
	ui = H->ui;
	marine = own_unit(H->g, 1, MARINE, 0);
	depot = own_unit(H->g, 1, SUPPLY_DEPOT, 0);
	kp_t_ok(T, marine != 0 && depot != 0, "a Marine and a Supply Depot");
	panel = dialog_of(ui, "rez\\statdata.bin", 0);
	kp_t_ok(T, panel >= 0, "the status panel");
	select_unit(H, depot, &now);
	shown_children(ui, panel, depot_shows);
	select_unit(H, marine, &now);
	shown_children(ui, panel, marine_shows);
	kp_t_ok(T, kp_canvas_place(H->hud.canvas, panel, &px, &py), "the panel placed");
	for (c = ui->controls[panel].child; c >= 0 && slot < 0; c = ui->controls[c].next) {
		const KpUiControl *k = &ui->controls[c];
		if (!marine_shows[c] || depot_shows[c]) continue;
		input(H, MOUSE_MOVE, px + half(k->left + k->right), py + half(k->top + k->bottom), 0, 0);
		if (tooltip_shown(ui)) slot = c;
	}
	kp_t_ok(T, slot >= 0, "a slot of the Marine's with a tooltip, none of the Depot's");
	select_unit(H, depot, &now);
	kp_t_ok(T, !tooltip_shown(ui), "the Depot's layout: no tooltip under the still mouse");
}

/* The leaderboard's expansion (command 40053, Ctrl+R: ToggleLeaderboardExpanded 0x4a6260) hides the rows after the
 * first at once and shows them again. */
static int shown_count(const KpUi *ui, int32_t d)
{
	int32_t c;
	int n = 0;
	for (c = ui->controls[d].child; c >= 0; c = ui->controls[c].next) n += (ui->controls[c].flags & SHOWN) != 0;
	return n;
}

static void leaderboard_key_folds_its_rows(KpT *T)
{
	KpOracleGame *og;
	KpRunner *run;
	Hud *H;
	int32_t board;
	int64_t step;
	uint32_t now = 1016;
	int expanded, folded;
	if (!ready(T, "test_hud.leaderboard_key_folds_its_rows", OUTCOME)) return;
	og = kp_t_game(T, OUTCOME, 0, 0, NULL);
	run = runner(T, og->g);
	H = with_hud(T, og, 1280, 720);
	board = dialog_of(H->ui, "rez\\statlb.bin", 0);
	kp_t_ok(T, board >= 0, "the leaderboard");
	for (step = 2; step <= 40; step++) {
		kp_runner_to(run, step);
		now += 42;
		frame(H, 1280, 720, now, 1, NO_CURSOR, CAMERA(0, 0));
	}
	expanded = shown_count(H->ui, board);
	kp_t_ui_input(H->ui, COMMAND, 40053, 0);
	folded = shown_count(H->ui, board);
	kp_t_ok(T, folded < expanded, "folded: %d of %d controls shown", folded, expanded);
	kp_t_ui_input(H->ui, COMMAND, 40053, 0);
	kp_t_eq(T, shown_count(H->ui, board), expanded, "expanded again");
}

/* The trigger countdown (terran03's 30 minutes) is the resource panel's control -10 (SetCountdownText 0x4a7960): the
 * game's second pass formats it (UpdateCountdownText 0x4bde30, "m:ss" or "h:mm:ss"), a countdown at 0 hides it
 * (HideCountdownText 0x4a79a0). */
static const char *countdown_shown(KpT *T, Hud *H, int32_t panel, uint32_t *now)
{
	int32_t k;
	frame(H, 1280, 720, *now += 42, 1, NO_CURSOR, CAMERA(0, 0));
	frame(H, 1280, 720, *now += 42, 1, NO_CURSOR, CAMERA(0, 0));
	k = child_of(H->ui, panel, -10);
	kp_t_ok(T, k >= 0, "the countdown's control");
	return (H->ui->controls[k].flags & SHOWN) != 0 ? (const char *)H->ui->controls[k].text : NULL;
}

static void countdown_is_drawn_in_the_resource_panel(KpT *T)
{
	KpOracleGame *og;
	KpRunner *run;
	Hud *H;
	int32_t panel;
	int64_t step;
	uint32_t now = 1016;
	const char *text;
	if (!ready(T, "test_hud.countdown_is_drawn_in_the_resource_panel", TERRAN03)) return;
	og = kp_t_game(T, TERRAN03, 0, 0, NULL);
	run = runner(T, og->g);
	H = with_hud(T, og, 1280, 720);
	panel = dialog_of(H->ui, "rez\\statres.bin", 0);
	kp_t_ok(T, panel >= 0, "the resource panel");
	text = countdown_shown(T, H, panel, &now);
	kp_t_ok(T, text == NULL, "hidden before the countdown is set");
	for (step = 2; step <= 40; step++) {
		kp_runner_to(run, step);
		text = countdown_shown(T, H, panel, &now);
	}
	kp_t_ok(T, text != NULL, "terran03's countdown is shown");
	if (text != NULL) kp_t_ok(T, strncmp(text, "29:", 3) == 0, "the 30 minutes run down: %s", text);
	og->g->trig.countdown = 3725;
	og->g->trig.countdown_text = 3725;
	kp_t_ok(T, strcmp(countdown_shown(T, H, panel, &now), "1:02:05") == 0, "an hour's text");
	og->g->trig.countdown_text = 0;
	kp_t_ok(T, strcmp(countdown_shown(T, H, panel, &now), "0:00") == 0, "the final 0:00");
	og->g->trig.countdown_text = -1;
	kp_t_ok(T, countdown_shown(T, H, panel, &now) == NULL, "hidden with the countdown at 0");
}

/* The mission warp (HandleMissionWarpCheat 0x40e030 under Ophelia's cheat): "terran 2" typed in the text box ends the
 * game as Quit Mission does and the glue's next entry goes to that mission's ready room (SetPendingGlueMission
 * 0x457e10, RunGlueLoop 0x457510); without the cheat, or past the campaign's rows, the line is nothing. */
static void line(Hud *H, const char *text, uint32_t *now)
{
	int k;
	for (k = 0; k < 3; k++) {
		if (k != 1) {
			kp_hud_key(H->ui, 1, 0x0d, 0);
			kp_hud_key(H->ui, 0, 0x0d, 0);
		} else {
			kp_hud_text(H->ui, text);
		}
		*now += 50;
		frame(H, 1280, 720, *now, 0, NO_CURSOR, KEEP);
	}
}

static void mission_warp_goes_to_the_ready_room(KpT *T)
{
	enum { OPHELIA = 0x200, QUIT_MISSION = 4, TERRAN_ROOM = 7, CAMPAIGN = 6 };
	Hud *H;
	KpUi *ui;
	const uint8_t *tabl;
	uint32_t now = 1016;
	size_t at = 1, rows = 0, tabl_size;
	int k, left = 2, found = 0, mission;
	if (!ready(T, "test_hud.mission_warp_goes_to_the_ready_room", TERRAN01)) return;
	H = with_hud(T, terran01(T), 1280, 720);
	ui = H->ui;
	frame(H, 1280, 720, now, 1, NO_CURSOR, CAMERA(0, 0));
	line(H, "terran 2", &now);
	kp_t_eq(T, ui->game_exit, 0, "no warp without Ophelia's cheat");
	kp_scalar_set(H->g, KP_CHEATS, OPHELIA);
	line(H, "terran 99", &now);
	kp_t_eq(T, ui->game_exit, 0, "past the campaign's rows: nothing");
	line(H, "terran 4294967298", &now);
	kp_t_eq(T, ui->game_exit, 0, "a number past 32 bits: nothing (not row 2)");
	line(H, "terran 2", &now);
	kp_t_eq(T, ui->game_exit, QUIT_MISSION, "the game ends as Quit Mission");
	kp_ui_end_game(ui);
	kp_ui_menus_enter(ui, CAMPAIGN);
	kp_t_ok(T, kp_ui_menus_poll(ui), "the glue's entry ends at once");
	kp_t_eq(T, ui->menus.action, KP_UI_MENU_MISSION, "with the mission");
	kp_t_eq(T, ui->menus.state, TERRAN_ROOM, "for the Terran ready room");
	kp_t_eq(T, ui->menus.expansion, 0, NULL);
	mission = ui->menus.mission;
	ui->menus.action = KP_UI_MENU_NONE;
	/* the original campaign's Terran rows from mission 0 (Boot Camp): the third row neither a cinematic nor hidden */
	tabl = kp_kpf_section(H->files[KP_UI_FILE_CAMPAIGN], H->sizes[KP_UI_FILE_CAMPAIGN], "TABL", &tabl_size);
	kp_t_ok(T, tabl != NULL, "campaign.kpd's TABL");
	for (k = 0; k < tabl[0]; k++) {
		size_t n = (size_t)tabl[at + 2] + (size_t)tabl[at + 3] * 256;
		if (tabl[at] == 0 && tabl[at + 1] == 1) {
			rows = at + 4;
			found = 1;
		}
		at += 4 + 8 * n;
	}
	kp_t_ok(T, found, "the original campaign's Terran rows");
	for (k = 0;; k++) {
		const uint8_t *r = tabl + rows + 8 * (size_t)k;
		if (r[4] + r[5] * 256 == 0 && r[7] == 0) {
			if (left == 0) break;
			left--;
		}
	}
	kp_t_eq(T, mission, tabl[rows + 8 * (size_t)k + 2] + tabl[rows + 8 * (size_t)k + 3] * 256, "the row's mission");
	kp_ui_menus_enter(ui, CAMPAIGN);
	kp_t_eq(T, kp_ui_menus_poll(ui), 0, "once: the next entry is the campaign menu");
}

/* The window's activity (WM_ACTIVATEAPP): a held arrow key scrolls the map; the focus goes and its release never
 * comes, the key bytes are cleared and the map stands (UpdateMapScroll 0x49bf10 while inactive, then no key held). */
static int scrolled(KpUi *ui)
{
	int64_t x, y;
	int k;
	for (k = 0; k < 10; k++) kp_ui_update_scroll(ui, 320, 240); /* the cursor at the logical screen's centre: no edge */
	return kp_hud_take_camera(ui, &x, &y);
}

static void inactive_window_stops_the_scroll(KpT *T)
{
	Hud *H;
	KpUi *ui;
	int64_t x, y;
	if (!ready(T, "test_hud.inactive_window_stops_the_scroll", TERRAN01)) return;
	H = with_hud(T, terran01(T), 1280, 720);
	ui = H->ui;
	kp_ui_set_view(ui, 1280, 640);
	kp_ui_set_screen(ui, 0, 0);
	kp_hud_take_camera(ui, &x, &y);
	kp_t_ui_input(ui, KEY_DOWN, 0x27, 0);
	kp_t_ok(T, scrolled(ui), "the held key scrolls");
	kp_ui_set_active(ui, 0);
	kp_t_ok(T, !scrolled(ui), "inactive: the map stands");
	kp_ui_set_active(ui, 1);
	kp_t_ok(T, !scrolled(ui), "active again: the key's byte went with the focus");
}

/* A type selection (Ctrl+click: FilterSelectableUnits 0x4bc5b0) takes a unit cloaked for free (IsCloakingForFree
 * 0x4a04e0: an Arbiter's field, status 0x800) with its uncloaked kind; a unit cloaked on its own stays out. */
static void cloak(KpGame *g, int64_t n, int64_t bits)
{
	g->u.status[n] |= bits;
	g->u.visibility[n] = 0xff; /* detected: the local player sees it */
}

static void type_selection_takes_the_units_cloaked_for_free(KpT *T)
{
	enum { CLOAKED = 0x100, FREE_CLOAK = 0x800 };
	Hud *H;
	KpGame *g;
	int64_t a, b, c, x, y, max_y, cx, cy;
	int k, chosen_a = 0, chosen_b = 0, chosen_c = 0;
	if (!ready(T, "test_hud.type_selection_takes_the_units_cloaked_for_free", TERRAN01)) return;
	H = with_hud(T, terran01(T), 1280, 720);
	g = H->g;
	a = own_unit(g, 1, MARINE, 0);
	b = own_unit(g, 1, MARINE, 1);
	c = own_unit(g, 1, MARINE, 2);
	kp_t_ok(T, a != 0 && b != 0 && c != 0, "three Marines");
	x = g->u.x[a];
	y = g->u.y[a];
	max_y = g->map_h_px - 12 * 32 - (640 - 400) + 8;
	cx = max64(0, x - 640);
	cy = max64(0, min64(y - 300, max_y - max_y % 8));
	frame(H, 1280, 720, 1000, 0, NO_CURSOR, CAMERA(0, 0));
	frame(H, 1280, 720, 1016, 0, NO_CURSOR, CAMERA(cx, cy));
	cloak(g, b, CLOAKED + FREE_CLOAK);
	cloak(g, c, CLOAKED);
	kp_t_ok(T, g->u.x[b] - cx >= 0 && g->u.x[b] - cx < 1280 && g->u.y[b] - cy >= 0 && g->u.y[b] - cy < 640, "in the view");
	kp_t_ok(T, g->u.x[c] - cx >= 0 && g->u.x[c] - cx < 1280 && g->u.y[c] - cy >= 0 && g->u.y[c] - cy < 640, "in the view");
	input_keys(H, MOUSE_MOVE, x - cx, y - cy, cx, cy, 0, 1, 0);
	input_keys(H, LEFT_DOWN, x - cx, y - cy, cx, cy, 0, 1, 0);
	input_keys(H, LEFT_UP, x - cx, y - cy, cx, cy, 0, 1, 0);
	for (k = 0; k < 12; k++) {
		chosen_a |= g->local_selection[k] == a;
		chosen_b |= g->local_selection[k] == b;
		chosen_c |= g->local_selection[k] == c;
	}
	kp_t_ok(T, chosen_a, "the clicked Marine");
	kp_t_ok(T, chosen_b, "the Marine cloaked for free");
	kp_t_ok(T, !chosen_c, "not the Marine cloaked on its own");
}

/* A double click on a selected unit selects its kind on the screen (ClickSelectAtPoint 0x4bbe70: the unit's selected
 * flag and the press as a double click 0x6ca730); a single click on it selects it alone. */
static void double_click_selects_the_kind_on_the_screen(KpT *T)
{
	Hud *H;
	KpGame *g;
	int64_t a, b, c, x, y, max_y, cx, cy;
	int k, chosen_a = 0, chosen_b = 0, chosen_c = 0, count = 0;
	if (!ready(T, "test_hud.double_click_selects_the_kind_on_the_screen", TERRAN01)) return;
	H = with_hud(T, terran01(T), 1280, 720);
	g = H->g;
	a = own_unit(g, 1, MARINE, 0);
	b = own_unit(g, 1, MARINE, 1);
	c = own_unit(g, 1, MARINE, 2);
	kp_t_ok(T, a != 0 && b != 0 && c != 0, "three Marines");
	x = g->u.x[a];
	y = g->u.y[a];
	max_y = g->map_h_px - 12 * 32 - (640 - 400) + 8;
	cx = max64(0, x - 640);
	cy = max64(0, min64(y - 300, max_y - max_y % 8));
	frame(H, 1280, 720, 1000, 0, NO_CURSOR, CAMERA(0, 0));
	frame(H, 1280, 720, 1016, 0, NO_CURSOR, CAMERA(cx, cy));
	input_keys(H, MOUSE_MOVE, x - cx, y - cy, cx, cy, 0, 0, 0);
	input_keys(H, LEFT_DOWN, x - cx, y - cy, cx, cy, 0, 0, 0);
	input_keys(H, LEFT_UP, x - cx, y - cy, cx, cy, 0, 0, 0);
	for (k = 0; k < 12; k++) count += g->local_selection[k] != 0;
	kp_t_eq(T, count, 1, "one click selects the unit alone");
	input_keys(H, LEFT_DOUBLE, x - cx, y - cy, cx, cy, 0, 0, 0);
	input_keys(H, LEFT_UP, x - cx, y - cy, cx, cy, 0, 0, 0);
	for (k = 0; k < 12; k++) {
		chosen_a |= g->local_selection[k] == a;
		chosen_b |= g->local_selection[k] == b;
		chosen_c |= g->local_selection[k] == c;
	}
	kp_t_ok(T, chosen_a && chosen_b && chosen_c, "the double click takes every Marine on the screen");
}

/* The touch layer over the console (ADR 0014): what gives the gestures' clicks to the console. */
typedef struct {
	Hud *H;
	int64_t cx, cy;
	int commands, moved;
	uint8_t first[KP_UI_COMMAND_BYTES];
} TouchHost;

static int touch_target(void *ud, int32_t x, int32_t y)
{
	TouchHost *t = (TouchHost *)ud;
	return kp_hud_touch_target(&t->H->hud, x, y, t->cx, t->cy);
}

static void touch_mouse(void *ud, int event, int32_t x, int32_t y)
{
	TouchHost *t = (TouchHost *)ud;
	size_t size = 0;
	kp_hud_input(&t->H->hud, event, x, y, t->cx, t->cy);
	t->commands += kp_t_ui_take_commands(t->H->ui, t->commands ? NULL : t->first, t->commands ? NULL : &size);
	t->moved |= kp_hud_take_camera(t->H->ui, &t->cx, &t->cy); /* the host's camera, as kp_app_input.c takes it */
}

static void touch_cancel(void *ud) { kp_hud_capture_lost(&((TouchHost *)ud)->H->hud); }

static void touch_button(void *ud, int moment, int32_t x, int32_t y)
{
	TouchHost *t = (TouchHost *)ud;
	size_t size = 0;
	kp_hud_pad_button(&t->H->hud, moment, x, y);
	t->commands += kp_t_ui_take_commands(t->H->ui, t->commands ? NULL : t->first, t->commands ? NULL : &size);
}

static int selected_count(const KpGame *g)
{
	int k, n = 0;
	for (k = 0; k < 12; k++) n += g->local_selection[k] != 0;
	return n;
}

/* A tap at the game view's point (px, py) and its lift a frame later. */
static void tap_at(KpTouch *t, int64_t px, int64_t py, uint32_t now)
{
	kp_touch_event(t, KP_TOUCH_DOWN, 1, (double)px, (double)py, now);
	kp_touch_event(t, KP_TOUCH_UP, 1, (double)px, (double)py, now + 60);
}

/* The smart tap: a tap on one of the player's own units selects it, a tap on the ground with them selected is the
 * right click's order, a long press on another of one's own units orders onto it (a right click there), the console
 * is a panel's. */
static void touch_taps_select_and_order(KpT *T)
{
	Hud *H;
	KpGame *g;
	KpTouch touch;
	KpTouchHost host;
	TouchHost th;
	int64_t a, b, x, y, max_y, cx, cy, gx = 0, gy = 0, bx, by;
	int k, found = 0;
	if (!ready(T, "test_hud.touch_taps_select_and_order", TERRAN01)) return;
	H = with_hud(T, terran01(T), 1280, 720);
	g = H->g;
	a = own_unit(g, 1, MARINE, 0);
	b = own_unit(g, 1, MARINE, 1);
	kp_t_ok(T, a != 0 && b != 0, "two Marines");
	x = g->u.x[a];
	y = g->u.y[a];
	max_y = g->map_h_px - 12 * 32 - (640 - 400) + 8;
	cx = max64(0, x - 640);
	cy = max64(0, min64(y - 300, max_y - max_y % 8));
	frame(H, 1280, 720, 1000, 0, NO_CURSOR, CAMERA(0, 0));
	frame(H, 1280, 720, 1016, 0, NO_CURSOR, CAMERA(cx, cy));
	memset(&th, 0, sizeof th);
	th.H = H;
	th.cx = cx;
	th.cy = cy;
	memset(&host, 0, sizeof host);
	host.ud = &th;
	host.target = touch_target;
	host.mouse = touch_mouse;
	host.cancel = touch_cancel;
	kp_touch_init(&touch, &host);
	kp_t_eq(T, kp_hud_touch_target(&H->hud, (int32_t)(x - cx), (int32_t)(y - cy), cx, cy), KP_TOUCH_SELECT,
		"nothing selected: a select tap");
	kp_t_eq(T, kp_hud_touch_target(&H->hud, 640, 719, cx, cy), KP_TOUCH_PANEL, "the console's");
	tap_at(&touch, x - cx, y - cy, 2000);
	kp_t_eq(T, selected_count(g), 1, "the tap selects the Marine alone");
	kp_t_ok(T, g->local_selection[0] == a, NULL);
	kp_t_eq(T, th.commands, 1, "the selection's command only");
	frame(H, 1280, 720, 3000, 1, NO_CURSOR, CAMERA(cx, cy)); /* the status panel takes the selection */
	kp_t_eq(T, kp_hud_touch_target(&H->hud, (int32_t)(x - cx), (int32_t)(y - cy), cx, cy), KP_TOUCH_SELECT,
		"one's own unit: still a select tap");
	for (k = 1; k < 12 && !found; k++) { /* a point of the ground with no unit, on the screen */
		gx = x - cx + 40 * k;
		gy = y - cy;
		found = gx < 1200 && kp_ui_pick_unit(H->ui, (int32_t)(gx + cx), (int32_t)(gy + cy)) == 0;
	}
	kp_t_ok(T, found, "ground beside the Marine");
	kp_t_eq(T, kp_hud_touch_target(&H->hud, (int32_t)gx, (int32_t)gy, cx, cy), KP_TOUCH_ORDER,
		"the ground with one's own unit selected: an order tap");
	th.commands = 0;
	tap_at(&touch, gx, gy, 4000);
	kp_t_eq(T, th.commands, 1, "the tap orders");
	kp_t_eq(T, th.first[0], 0x14, "the right click's command");
	kp_t_eq(T, selected_count(g), 1, "the selection stays");
	th.commands = 0;
	bx = g->u.x[b] - cx;
	by = g->u.y[b] - cy;
	kp_t_ok(T, bx >= 0 && bx < 1280 && by >= 0 && by < 640, "another Marine on the screen");
	kp_touch_event(&touch, KP_TOUCH_DOWN, 2, (double)bx, (double)by, 6000);
	kp_touch_frame(&touch, 6000 + KP_TOUCH_LONG_MS);
	kp_touch_event(&touch, KP_TOUCH_UP, 2, (double)bx, (double)by, 6600);
	kp_t_eq(T, th.commands, 1, "the long press on another of one's own units orders onto it");
	kp_t_eq(T, th.first[0], 0x14, NULL);
	kp_t_ok(T, g->local_selection[0] == a && selected_count(g) == 1, "the selection stays");
}

/* A tap on the pad's button at its centre (KP_UI_PAD_*): pressed, then tapped. */
static void pad_tap(Hud *H, int button)
{
	int32_t x = 0, y = 0;
	kp_hud_pad_point(&H->hud, button, &x, &y);
	kp_hud_pad_button(&H->hud, KP_TOUCH_BUTTON_PRESS, x, y);
	kp_hud_pad_button(&H->hud, KP_TOUCH_BUTTON_TAP, x, y);
}

/* The touch pad (ADR 0014 point 4): a column along the game view's left edge under the resource line; folded it shows
 * Shift, its toggle and the groups with units in them, the toggle opens all ten; a long press on a group's button
 * assigns the selection to it (Ctrl and the digit) and folds the pad, which shows the group from then on; a tap
 * recalls it; Shift's tap is the next clicks' Shift; the pad leaves the canvas with the touch mode. Shift lies on top,
 * over the toggle, so that the opening leaves it in place. */
static void touch_pad_assigns_and_recalls_groups(KpT *T)
{
	Hud *H;
	KpGame *g;
	int64_t a;
	int32_t x = 0, y = 0, rx = 0, ry = 0, sx = 0, sy = 0;
	uint8_t first[KP_UI_COMMAND_BYTES];
	size_t size = 0;
	int n;
	if (!ready(T, "test_hud.touch_pad_assigns_and_recalls_groups", TERRAN01)) return;
	H = with_hud(T, terran01(T), 1280, 720);
	g = H->g;
	a = own_unit(g, 1, MARINE, 0);
	kp_t_ok(T, a != 0, "a Marine");
	H->ui->effects.colour[0] = 1; /* the blend colour the glue's theme sets (1 in every record) */
	kp_hud_pad(&H->hud, 1, 1.5);
	frame(H, 1280, 720, 1000, 0, NO_CURSOR, CAMERA(0, 0));
	frame(H, 1280, 720, 1016, 0, NO_CURSOR, CAMERA(0, 0)); /* laid out under the resource line composed */
	kp_t_ok(T, H->hud.canvas->overlay == &H->hud.pad.surface, "the pad over the panels");
	kp_t_ok(T, H->hud.pad.bw >= (int32_t)(KP_HUD_PAD_DP * 1.5), "a button of %d dp: %d pixels", KP_HUD_PAD_DP,
		H->hud.pad.bw);
	kp_t_ok(T, kp_canvas_place(H->hud.canvas, H->ui->hud.resources, &rx, &ry), "the resource line placed");
	kp_t_ok(T, H->hud.pad.y > ry + (H->ui->controls[H->ui->hud.resources].bottom - H->ui->controls[H->ui->hud.resources].top),
		"under the resource line: %d", H->hud.pad.y);
	kp_t_ok(T, H->hud.pad.x < 20, "along the left edge");
	kp_t_eq(T, alpha_at(H->hud.canvas, H->hud.pad.x + 3, H->hud.pad.y + 3), KP_UI_CANVAS_BLEND_A,
		"a button's fill blended over the world");
	kp_t_eq(T, alpha_at(H->hud.canvas, H->hud.pad.x, H->hud.pad.y), KP_UI_CANVAS_OVERLAY_A, "its outline opaque");
	kp_t_eq(T, H->hud.pad.count, 2, "folded: Shift and the toggle (no group has units)");
	kp_t_ok(T, H->hud.pad.buttons[0] == KP_UI_PAD_SHIFT && H->hud.pad.buttons[1] == KP_UI_PAD_TOGGLE,
		"Shift on top, the toggle under it");
	kp_t_ok(T, !kp_hud_pad_point(&H->hud, 0, &x, &y), "an empty group not shown folded");
	pad_tap(H, KP_UI_PAD_TOGGLE);
	frame(H, 1280, 720, 1032, 0, NO_CURSOR, CAMERA(0, 0));
	kp_t_ok(T, H->hud.pad.open && H->hud.pad.count == 12, "opened: all ten groups (%d buttons)", H->hud.pad.count);
	kp_t_ok(T, H->hud.pad.y + H->hud.pad.h < 720 - 80, "over the game view");
	kp_t_ok(T, kp_hud_pad_point(&H->hud, 0, &x, &y), "group 1's button");
	kp_t_eq(T, kp_hud_pad_at(&H->hud, x, y), 0, NULL);
	kp_t_eq(T, kp_hud_touch_target(&H->hud, x, y, 0, 0), KP_TOUCH_BUTTON, "the pad's touch is a button's");
	kp_t_eq(T, kp_hud_pad_at(&H->hud, x, y + H->hud.pad.bh / 2 + H->hud.pad.gap / 2), -1, "the gap between two buttons");
	kp_select_locally(g, &a, 1);
	frame(H, 1280, 720, 1048, 1, NO_CURSOR, CAMERA(0, 0));
	kp_hud_pad_button(&H->hud, KP_TOUCH_BUTTON_PRESS, x, y);
	kp_hud_pad_button(&H->hud, KP_TOUCH_BUTTON_LONG, x, y);
	kp_hud_pad_button(&H->hud, KP_TOUCH_BUTTON_END, x, y);
	n = kp_t_ui_take_commands(H->ui, first, &size);
	kp_t_eq(T, n, 1, "the long press assigns");
	kp_t_ok(T, size == 3 && first[0] == 0x13 && first[1] == 0 && first[2] == 1, "group 1 assigned");
	kp_t_ok(T, !H->hud.pad.open, "and folds the pad");
	kp_t_ui_take_commands(H->ui, NULL, NULL);
	g->groups[g->local_player][1][0] = kp_ui_unit_ref(g, a); /* the assignment's command run */
	frame(H, 1280, 720, 1064, 0, NO_CURSOR, CAMERA(0, 0));
	kp_t_eq(T, H->hud.pad.count, 3, "folded: Shift, the toggle and group 1");
	kp_t_ok(T, kp_hud_pad_point(&H->hud, 0, &x, &y), "group 1 shown folded");
	kp_t_ok(T, kp_hud_pad_point(&H->hud, KP_UI_PAD_SHIFT, &sx, &sy), NULL);
	kp_hud_pad_button(&H->hud, KP_TOUCH_BUTTON_PRESS, x, y);
	kp_hud_pad_button(&H->hud, KP_TOUCH_BUTTON_TAP, sx, sy);
	kp_t_eq(T, kp_t_ui_take_commands(H->ui, NULL, NULL), 0, "a lift on another button does nothing");
	pad_tap(H, KP_UI_PAD_SHIFT);
	kp_t_ok(T, H->hud.pad.shift && H->ui->input.keys[0] == 1, "Shift on");
	pad_tap(H, KP_UI_PAD_SHIFT);
	kp_t_ok(T, !H->hud.pad.shift && H->ui->input.keys[0] == 0, "Shift off");
	kp_hud_pad(&H->hud, 0, 1.5);
	frame(H, 1280, 720, 1080, 0, NO_CURSOR, CAMERA(0, 0));
	kp_t_ok(T, H->hud.canvas->overlay == NULL, "no pad off the touch mode");
	kp_t_eq(T, kp_hud_pad_at(&H->hud, H->hud.pad.x + 1, H->hud.pad.y + 1), -1, NULL);
}

/* A group whose units all died keeps their refs (the original's recall skips them): the pad counts it empty, as its
 * recall selects nothing, and the folded pad leaves it out. */
static void touch_pad_drops_a_dead_group(KpT *T)
{
	Hud *H;
	KpGame *g;
	int64_t a;
	int32_t x = 0, y = 0;
	if (!ready(T, "test_hud.touch_pad_drops_a_dead_group", TERRAN01)) return;
	H = with_hud(T, terran01(T), 1280, 720);
	g = H->g;
	a = own_unit(g, 1, MARINE, 0);
	kp_t_ok(T, a != 0, "a Marine");
	kp_hud_pad(&H->hud, 1, 1.5);
	g->groups[g->local_player][1][0] = kp_ui_unit_ref(g, a);
	frame(H, 1280, 720, 1000, 0, NO_CURSOR, CAMERA(0, 0));
	kp_t_eq(T, (int)kp_ui_groups_held(H->ui), 1 << 1, "group 1 held");
	kp_t_ok(T, kp_hud_pad_point(&H->hud, 0, &x, &y), "group 1 shown folded");
	g->u.order[a] = KP_ORD_DIE; /* the Marine dead, its ref still in the group */
	g->u.order_state[a] = 1;
	kp_t_ok(T, g->groups[g->local_player][1][0] != 0, "the group keeps the ref");
	kp_t_eq(T, (int)kp_ui_groups_held(H->ui), 0, "group 1 empty");
	frame(H, 1280, 720, 1016, 0, NO_CURSOR, CAMERA(0, 0));
	kp_t_ok(T, !kp_hud_pad_point(&H->hud, 0, &x, &y), "and no longer shown folded");
}

/* A long press on a group's button that holds units assigns the selection to it anew, through the fingers' automaton,
 * with the pad's Shift on too (the Fold7: Ctrl, Shift and the digit assigned nothing); Shift stays on after. */
static void touch_pad_reassigns_a_held_group(KpT *T)
{
	Hud *H;
	KpGame *g;
	KpTouch touch;
	KpTouchHost host;
	TouchHost th;
	int64_t a, b;
	int32_t x = 0, y = 0;
	if (!ready(T, "test_hud.touch_pad_reassigns_a_held_group", TERRAN01)) return;
	H = with_hud(T, terran01(T), 1280, 720);
	g = H->g;
	a = own_unit(g, 1, MARINE, 0);
	b = own_unit(g, 1, MARINE, 1);
	kp_t_ok(T, a != 0 && b != 0, "two Marines");
	g->groups[g->local_player][1][0] = kp_ui_unit_ref(g, a); /* group 1 holds the first */
	kp_hud_pad(&H->hud, 1, 1.5);
	frame(H, 1280, 720, 1000, 0, NO_CURSOR, CAMERA(0, 0));
	frame(H, 1280, 720, 1016, 0, NO_CURSOR, CAMERA(0, 0));
	kp_t_ok(T, kp_hud_pad_point(&H->hud, 0, &x, &y), "group 1 shown folded");
	memset(&th, 0, sizeof th);
	th.H = H;
	memset(&host, 0, sizeof host);
	host.ud = &th;
	host.target = touch_target;
	host.mouse = touch_mouse;
	host.cancel = touch_cancel;
	host.button = touch_button;
	kp_touch_init(&touch, &host);
	kp_select_locally(g, &b, 1);
	frame(H, 1280, 720, 1032, 1, NO_CURSOR, CAMERA(0, 0));
	kp_t_ui_take_commands(H->ui, NULL, NULL);
	pad_tap(H, KP_UI_PAD_SHIFT); /* Shift on: a long press assigns all the same */
	kp_touch_event(&touch, KP_TOUCH_DOWN, 1, x, y, 2000);
	frame(H, 1280, 720, 2016, 0, NO_CURSOR, CAMERA(0, 0));
	kp_touch_frame(&touch, 2000 + KP_TOUCH_LONG_MS);
	frame(H, 1280, 720, 2000 + KP_TOUCH_LONG_MS, 0, NO_CURSOR, CAMERA(0, 0));
	kp_touch_event(&touch, KP_TOUCH_UP, 1, x, y, 2600);
	kp_t_eq(T, th.commands, 1, "the long press on the held group assigns");
	kp_t_ok(T, th.first[0] == 0x13 && th.first[1] == 0 && th.first[2] == 1, "group 1 assigned anew");
	kp_t_ok(T, H->hud.pad.shift && H->ui->input.keys[0] == 1, "Shift stays on");
}

/* The pad's Shift is the next click's: an order with it on is queued and turns it off, so does a selection's change; a
 * pad's button that queues nothing (the toggle) leaves it on. A long press locks it: every order queued, drawn with a
 * third outline, until a tap turns it off. */
static void touch_pad_shift_lasts_one_click(KpT *T)
{
	Hud *H;
	KpGame *g;
	KpTouch touch;
	KpTouchHost host;
	TouchHost th;
	int64_t a, b, x, y, max_y, cx, cy, gx = 0, gy = 0;
	int32_t sx = 0, sy = 0;
	int k, found = 0;
	if (!ready(T, "test_hud.touch_pad_shift_lasts_one_click", TERRAN01)) return;
	H = with_hud(T, terran01(T), 1280, 720);
	g = H->g;
	a = own_unit(g, 1, MARINE, 0);
	b = own_unit(g, 1, MARINE, 1);
	kp_t_ok(T, a != 0 && b != 0, "two Marines");
	x = g->u.x[a];
	y = g->u.y[a];
	max_y = g->map_h_px - 12 * 32 - (640 - 400) + 8;
	cx = max64(0, x - 640);
	cy = max64(0, min64(y - 300, max_y - max_y % 8));
	kp_hud_pad(&H->hud, 1, 1.5);
	frame(H, 1280, 720, 1000, 0, NO_CURSOR, CAMERA(0, 0));
	frame(H, 1280, 720, 1016, 0, NO_CURSOR, CAMERA(cx, cy));
	memset(&th, 0, sizeof th);
	th.H = H;
	th.cx = cx;
	th.cy = cy;
	memset(&host, 0, sizeof host);
	host.ud = &th;
	host.target = touch_target;
	host.mouse = touch_mouse;
	host.cancel = touch_cancel;
	host.button = touch_button;
	kp_touch_init(&touch, &host);
	tap_at(&touch, x - cx, y - cy, 1100); /* the Marine selected */
	kp_t_ok(T, g->local_selection[0] == a, "the Marine selected");
	frame(H, 1280, 720, 1200, 1, NO_CURSOR, CAMERA(cx, cy));
	for (k = 1; k < 12 && !found; k++) { /* a point of the ground with no unit, on the screen */
		gx = x - cx + 40 * k;
		gy = y - cy;
		found = gx < 1200 && kp_ui_pick_unit(H->ui, (int32_t)(gx + cx), (int32_t)(gy + cy)) == 0;
	}
	kp_t_ok(T, found, "ground beside the Marine");
	pad_tap(H, KP_UI_PAD_SHIFT);
	kp_t_ok(T, H->hud.pad.shift && H->ui->input.keys[0] == 1, "Shift on");
	pad_tap(H, KP_UI_PAD_TOGGLE);
	pad_tap(H, KP_UI_PAD_TOGGLE);
	kp_t_ok(T, H->hud.pad.shift, "the toggle queues nothing: Shift stays on");
	th.commands = 0;
	tap_at(&touch, gx, gy, 2000);
	kp_t_eq(T, th.commands, 1, "the tap orders");
	kp_t_ok(T, th.first[0] == 0x14 && th.first[9] == 1, "the right click queued (its Shift byte)");
	kp_t_ok(T, !H->hud.pad.shift && H->ui->input.keys[0] == 0, "and Shift off");
	frame(H, 1280, 720, 2100, 0, NO_CURSOR, CAMERA(cx, cy));
	pad_tap(H, KP_UI_PAD_SHIFT);
	th.commands = 0;
	tap_at(&touch, g->u.x[b] - cx, g->u.y[b] - cy, 3000);
	kp_t_ok(T, th.commands >= 1, "the tap on another Marine changes the selection");
	kp_t_ok(T, !H->hud.pad.shift && H->ui->input.keys[0] == 0, "and Shift off");
	th.commands = 0;
	tap_at(&touch, gx, gy, 4000);
	kp_t_ok(T, th.commands == 1 && th.first[9] == 0, "the next order not queued");
	kp_hud_pad_point(&H->hud, KP_UI_PAD_SHIFT, &sx, &sy);
	kp_touch_event(&touch, KP_TOUCH_DOWN, 1, sx, sy, 5000);
	kp_touch_frame(&touch, 5000 + KP_TOUCH_LONG_MS);
	kp_touch_event(&touch, KP_TOUCH_UP, 1, sx, sy, 5600);
	kp_t_ok(T, H->hud.pad.shift == KP_UI_PAD_SHIFT_LOCKED && H->ui->input.keys[0] == 1, "the long press locks Shift");
	for (k = 0; k < 2; k++) {
		th.commands = 0;
		tap_at(&touch, gx, gy, (uint32_t)(6000 + 1000 * k));
		kp_t_ok(T, th.commands == 1 && th.first[9] == 1, "locked: order %d queued", k + 1);
	}
	kp_t_eq(T, H->hud.pad.shift, KP_UI_PAD_SHIFT_LOCKED, "and Shift still locked");
	frame(H, 1280, 720, 8000, 0, NO_CURSOR, CAMERA(cx, cy));
	kp_t_ok(T, index_at(H->hud.canvas, H->hud.pad.x + 2, H->hud.pad.y + H->hud.pad.bh / 2)
		== H->ui->ui_colours[16], "drawn locked: a third outline");
	pad_tap(H, KP_UI_PAD_SHIFT);
	kp_t_ok(T, H->hud.pad.shift == KP_UI_PAD_SHIFT_OFF && H->ui->input.keys[0] == 0, "a tap unlocks it");
}

/* The pad's fill stays the blend colour's: the in-game dialogs set their border's colour as the blend's
 * (SetDialogBorderColor 0x47bd40), so the pad is drawn again with it once the game menu closed (the fill drawn with
 * the colour before was no longer the blend's and went opaque: the Fold7's Victory, the pad's buttons flickered); under
 * the dialogs, the result's too, it is gone. */
static void touch_pad_follows_the_blend_colour(KpT *T)
{
	Hud *H;
	uint32_t now = 1000;
	uint8_t before;
	int k;
	if (!ready(T, "test_hud.touch_pad_follows_the_blend_colour", TERRAN01)) return;
	H = with_hud(T, terran01(T), 1280, 720);
	H->ui->effects.colour[0] = 1; /* the glue theme's */
	kp_hud_pad(&H->hud, 1, 1.5);
	frame(H, 1280, 720, now, 0, NO_CURSOR, CAMERA(0, 0));
	frame(H, 1280, 720, now += 16, 0, NO_CURSOR, CAMERA(0, 0));
	kp_t_eq(T, alpha_at(H->hud.canvas, H->hud.pad.x + 3, H->hud.pad.y + 3), KP_UI_CANVAS_BLEND_A, "the fill blended");
	before = H->ui->effects.colour[0];
	for (k = 0; k < 2; k++) { /* the game menu opened and closed */
		uint16_t vk = k == 0 ? KP_HUD_VK_F10 : KP_HUD_VK_ESCAPE;
		int f;
		kp_hud_key(H->ui, 1, vk, 0);
		kp_hud_key(H->ui, 0, vk, 0);
		for (f = 0; f < 10; f++) {
			kp_t_ui_idle(H->ui);
			frame(H, 1280, 720, now += 16, 0, NO_CURSOR, KEEP);
		}
		if (k == 0) kp_t_ok(T, H->ui->modeless >= 0 && H->hud.canvas->overlay == NULL, "no pad under the game menu");
	}
	kp_t_ok(T, H->ui->modeless < 0 && H->ui->effects.colour[0] != before, "closed: its border's blend colour stays");
	kp_t_eq(T, alpha_at(H->hud.canvas, H->hud.pad.x + 3, H->hud.pad.y + 3), KP_UI_CANVAS_BLEND_A,
		"the pad back: the fill blended");
	kp_t_eq(T, index_at(H->hud.canvas, H->hud.pad.x + 3, H->hud.pad.y + 3), H->ui->effects.colour[0],
		"with the blend colour");
	kp_t_ok(T, kp_ui_show_result(H->ui, 3) >= 0, "the victory");
	frame(H, 1280, 720, now += 16, 1, NO_CURSOR, KEEP);
	kp_t_ok(T, H->hud.canvas->overlay == NULL, "no pad under the victory");
}

/* A save loaded from the glue's load screen starts its game with that screen's blend colour (PalCs's, 254), an index of
 * the tileset's colour cycles (248..254): the pad's translucent texels carry the colour's nearest entry outside the
 * cycles, which the HD picture mixes into the world (the raw index cycled the buttons' fill on the Fold7); a colour
 * outside the cycles (a ready room's, 138) stays itself. */
static void touch_pad_blend_colour_outside_the_cycles(KpT *T)
{
	enum { THEME_PALCS = 1, THEME_PALRT = 2, EFFECT_INDEX = 1 };
	static const int THEMES[2] = { THEME_PALCS, THEME_PALRT };
	Hud *H;
	uint32_t now = 1000;
	int k;
	if (!ready(T, "test_hud.touch_pad_blend_colour_outside_the_cycles", TERRAN01)) return;
	H = with_hud(T, terran01(T), 1280, 720);
	kp_t_ok(T, H->ui->theme_effects != NULL && H->ui->theme_effect_size > EFFECT_INDEX, "the themes' effect colours");
	kp_hud_pad(&H->hud, 1, 1.5);
	for (k = 0; k < 2; k++) {
		uint8_t colour = H->ui->theme_effects[H->ui->theme_effect_size * (size_t)THEMES[k] + EFFECT_INDEX], index;
		H->ui->effects.colour[0] = colour;
		frame(H, 1280, 720, now += 16, 0, NO_CURSOR, CAMERA(0, 0));
		kp_t_eq(T, alpha_at(H->hud.canvas, H->hud.pad.x + 3, H->hud.pad.y + 3), KP_UI_CANVAS_BLEND_A, "the fill blended");
		index = index_at(H->hud.canvas, H->hud.pad.x + 3, H->hud.pad.y + 3);
		if (k == 0) {
			kp_t_ok(T, H->tileset.cycled[colour], "PalCs's colour %d cycles", colour);
			kp_t_ok(T, !H->tileset.cycled[index], "the fill's index %d outside the cycles", index);
			kp_t_eq(T, index, kp_ui_nearest_colour(H->tileset.rgb, H->tileset.cycled, H->tileset.rgb[3 * colour],
				H->tileset.rgb[3 * colour + 1], H->tileset.rgb[3 * colour + 2]), "the colour's nearest outside them");
		} else {
			kp_t_ok(T, !H->tileset.cycled[colour], "the ready room's colour %d does not cycle", colour);
			kp_t_eq(T, index, colour, "kept");
		}
	}
}

/* A second tap on a group's button within the original's 500 ms centres the view on the group (the second recall), in
 * the opened pad too: its fold waits for the second tap, so the button has not moved. */
static void touch_pad_second_tap_centres_the_group(KpT *T)
{
	Hud *H;
	KpGame *g;
	int64_t a, x, y;
	int32_t bx = 0, by = 0, bx2 = 0, by2 = 0;
	int open;
	if (!ready(T, "test_hud.touch_pad_second_tap_centres_the_group", TERRAN01)) return;
	H = with_hud(T, terran01(T), 1280, 720);
	g = H->g;
	a = own_unit(g, 1, MARINE, 0);
	kp_t_ok(T, a != 0, "a Marine");
	g->groups[g->local_player][3][0] = kp_ui_unit_ref(g, a); /* group 3 holds the Marine */
	kp_hud_pad(&H->hud, 1, 1.5);
	for (open = 1; open >= 0; open--) {
		uint32_t now = open ? 1000 : 5000;
		x = g->u.x[a] > 1500 ? 0 : g->map_w_px - 1280; /* the view away from the Marine */
		y = 0;
		frame(H, 1280, 720, now, 0, NO_CURSOR, CAMERA(x, y));
		frame(H, 1280, 720, now + 16, 0, NO_CURSOR, CAMERA(x, y));
		if (open) pad_tap(H, KP_UI_PAD_TOGGLE);
		frame(H, 1280, 720, now + 32, 0, NO_CURSOR, CAMERA(x, y));
		kp_t_eq(T, H->hud.pad.open, open, open ? "opened" : "folded");
		kp_t_ok(T, kp_hud_pad_point(&H->hud, 2, &bx, &by), "group 3's button");
		pad_tap(H, 2);
		kp_t_ui_take_commands(H->ui, NULL, NULL);
		kp_t_ok(T, !kp_hud_take_camera(H->ui, &x, &y), "the first tap recalls the group only");
		frame(H, 1280, 720, now + 300, 0, NO_CURSOR, CAMERA(x, y));
		kp_t_ok(T, kp_hud_pad_point(&H->hud, 2, &bx2, &by2) && bx2 == bx && by2 == by, "the button where it was");
		pad_tap(H, 2);
		kp_t_ui_take_commands(H->ui, NULL, NULL);
		kp_t_ok(T, kp_hud_take_camera(H->ui, &x, &y), "%s: the second tap centres the view", open ? "opened" : "folded");
		kp_t_ok(T, g->u.x[a] >= x && g->u.x[a] < x + 1280 && g->u.y[a] >= y && g->u.y[a] < y + 640, "on the Marine");
		frame(H, 1280, 720, now + 316 + KP_HUD_PAD_FOLD_MS, 0, NO_CURSOR, CAMERA(x, y));
		kp_t_ok(T, !H->hud.pad.open, "folded after");
	}
}

/* A tap near a minimap ping centres the view on its event (the Space's last event for the fingers; ours), a touch away
 * from it is the minimap's. */
static void touch_ping_centres_its_event(KpT *T)
{
	Hud *H;
	KpUi *ui;
	const KpUiControl *d, *c;
	const KpUiPing *ping;
	int32_t px = 0, py = 0, lx, ly, mx, my;
	int64_t x, y;
	if (!ready(T, "test_hud.touch_ping_centres_its_event", TERRAN01)) return;
	H = with_hud(T, terran01(T), 1280, 720);
	ui = H->ui;
	kp_hud_pad(&H->hud, 1, 1.5);
	frame(H, 1280, 720, 1000, 0, NO_CURSOR, CAMERA(0, 0));
	mx = (int32_t)H->g->map_w_px / 2 + 13;
	my = (int32_t)H->g->map_h_px / 2 + 7;
	kp_ui_minimap_ping(ui, (uint32_t)mx, (uint32_t)my, 0);
	frame(H, 1280, 720, 1016, 0, NO_CURSOR, CAMERA(0, 0));
	ping = &ui->minimap.pings[0];
	kp_t_ok(T, ping->active, "the ping");
	d = &ui->controls[ui->minimap.dialog];
	c = &ui->controls[ui->minimap.control];
	kp_t_ok(T, kp_canvas_place(H->hud.canvas, ui->minimap.dialog, &px, &py), "the minimap placed");
	lx = px + (c->left + ping->x + 0x40) - d->left + 3; /* a little off the ping's centre */
	ly = py + (315 + c->top + ping->y + 0x40) - d->top - 2;
	kp_t_eq(T, kp_hud_touch_target(&H->hud, lx, ly, 0, 0), KP_TOUCH_PANEL_BUTTON, "the touch near the ping is its");
	kp_hud_pad_button(&H->hud, KP_TOUCH_BUTTON_PRESS, lx, ly);
	kp_hud_pad_button(&H->hud, KP_TOUCH_BUTTON_TAP, lx, ly);
	kp_t_ok(T, kp_hud_take_camera(ui, &x, &y), "the tap moved the view");
	kp_t_ok(T, x + 640 - mx < 8 && mx - (x + 640) <= 8 && y + 320 - my < 8 && my - (y + 320) <= 8,
		"centred on the event (%d,%d): the view at %d,%d", (int)mx, (int)my, (int)x, (int)y);
	kp_t_eq(T, kp_hud_touch_target(&H->hud, lx + 60, ly, 0, 0), KP_TOUCH_PANEL, "away from it: the minimap's");
	{ /* a slide from the ping is the minimap's drag: the view where the finger went, not on the event */
		KpTouch touch;
		KpTouchHost host;
		TouchHost th;
		memset(&th, 0, sizeof th);
		th.H = H;
		memset(&host, 0, sizeof host);
		host.ud = &th;
		host.target = touch_target;
		host.mouse = touch_mouse;
		host.cancel = touch_cancel;
		host.button = touch_button;
		kp_touch_init(&touch, &host);
		kp_touch_set_density(&touch, 1.5);
		kp_touch_event(&touch, KP_TOUCH_DOWN, 1, lx, ly, 2000);
		kp_touch_event(&touch, KP_TOUCH_MOVE, 1, lx + 30, ly, 2010);
		kp_touch_event(&touch, KP_TOUCH_UP, 1, lx + 30, ly, 2020);
		kp_t_ok(T, th.moved, "the drag moved the view");
		kp_t_ok(T, th.cx + 640 - mx > 200, "along the drag, off the event: the view at %d", (int)th.cx);
	}
	ui->minimap.pings[0].active = 0;
	kp_t_eq(T, kp_hud_touch_target(&H->hud, lx, ly, 0, 0), KP_TOUCH_PANEL, "the ping gone: the minimap's");
}

/* The pad keeps out of the way: the fold waits while a button is pressed (laid out again it would move the button
 * from under the finger), it is gone under a modeless dialog (the game menu's buttons take their taps), a group's
 * button types nothing into a focused message line, the window's key bytes cleared turn its Shift off, and a short view
 * leaves the last groups out rather than reach the console. */
static void touch_pad_stays_out_of_the_way(KpT *T)
{
	Hud *H;
	KpUi *ui;
	KpGame *g;
	int64_t a;
	int32_t x = 0, y = 0, edit;
	const uint8_t *text;
	uint32_t now = 1000;
	int k;
	char name[128];
	if (!ready(T, "test_hud.touch_pad_stays_out_of_the_way", TERRAN01)) return;
	H = with_hud(T, terran01(T), 1280, 720);
	ui = H->ui;
	g = H->g;
	a = own_unit(g, 1, MARINE, 0);
	kp_t_ok(T, a != 0, "a Marine");
	g->groups[g->local_player][3][0] = kp_ui_unit_ref(g, a);
	kp_hud_pad(&H->hud, 1, 1.5);
	frame(H, 1280, 720, now, 0, NO_CURSOR, CAMERA(0, 0));
	frame(H, 1280, 720, now += 16, 0, NO_CURSOR, CAMERA(0, 0));
	pad_tap(H, KP_UI_PAD_TOGGLE);
	frame(H, 1280, 720, now += 16, 0, NO_CURSOR, CAMERA(0, 0));
	pad_tap(H, 2); /* group 3 recalled: the pad folds a moment later */
	kp_t_ui_take_commands(ui, NULL, NULL);
	kp_t_ok(T, kp_hud_pad_point(&H->hud, 4, &x, &y), "group 5's button");
	kp_hud_pad_button(&H->hud, KP_TOUCH_BUTTON_PRESS, x, y);
	frame(H, 1280, 720, now += 2 * KP_HUD_PAD_FOLD_MS, 0, NO_CURSOR, CAMERA(0, 0));
	kp_t_ok(T, H->hud.pad.open && kp_hud_pad_at(&H->hud, x, y) == 4, "not folded under the pressed button");
	kp_hud_pad_button(&H->hud, KP_TOUCH_BUTTON_END, x, y);
	frame(H, 1280, 720, now += 16, 0, NO_CURSOR, CAMERA(0, 0));
	kp_t_ok(T, !H->hud.pad.open, "folded once it is let go");
	/* the game menu */
	kp_t_ok(T, kp_hud_pad_point(&H->hud, KP_UI_PAD_SHIFT, &x, &y), NULL);
	kp_hud_key(ui, 1, KP_HUD_VK_F10, 0);
	kp_hud_key(ui, 0, KP_HUD_VK_F10, 0);
	for (k = 0; k < 10; k++) {
		kp_t_ui_idle(ui);
		frame(H, 1280, 720, now += 16, 0, NO_CURSOR, KEEP);
	}
	kp_t_ok(T, ui->modeless >= 0 && strcmp(kp_t_ui_bin(ui, ui->modeless, name, sizeof name), "rez\\gamemenu.bin") == 0,
		"the game menu");
	kp_t_ok(T, H->hud.canvas->overlay == NULL, "no pad over it");
	kp_t_eq(T, kp_hud_pad_at(&H->hud, x, y), -1, "nor its buttons");
	kp_t_ok(T, kp_hud_touch_target(&H->hud, x, y, 0, 0) != KP_TOUCH_BUTTON, NULL);
	kp_hud_key(ui, 1, KP_HUD_VK_ESCAPE, 0);
	kp_hud_key(ui, 0, KP_HUD_VK_ESCAPE, 0);
	for (k = 0; k < 10; k++) {
		kp_t_ui_idle(ui);
		frame(H, 1280, 720, now += 16, 0, NO_CURSOR, KEEP);
	}
	kp_t_ok(T, ui->modeless < 0 && H->hud.canvas->overlay == &H->hud.pad.surface, "the pad back with the menu closed");
	/* the message line */
	kp_hud_key(ui, 1, 0x0d, 0);
	kp_hud_key(ui, 0, 0x0d, 0);
	kp_t_ok(T, kp_ui_edit_focused(ui), "the message line typed into");
	kp_t_ui_take_commands(ui, NULL, NULL);
	pad_tap(H, 2);
	kp_t_ok(T, kp_hud_pad_point(&H->hud, 2, &x, &y), NULL);
	kp_hud_pad_button(&H->hud, KP_TOUCH_BUTTON_PRESS, x, y);
	kp_hud_pad_button(&H->hud, KP_TOUCH_BUTTON_LONG, x, y);
	kp_hud_pad_button(&H->hud, KP_TOUCH_BUTTON_END, x, y);
	edit = child_of(ui, ui->hud.textbox, 6);
	text = edit >= 0 ? kp_ui_edit_buffer(ui, edit) : NULL;
	kp_t_ok(T, text != NULL && text[0] == 0, "no digit typed: \"%s\"", text ? (const char *)text : "?");
	kp_t_eq(T, kp_t_ui_take_commands(ui, NULL, NULL), 0, "no group recalled or assigned");
	kp_hud_key(ui, 1, 0x1b, 0);
	kp_hud_key(ui, 0, 0x1b, 0);
	/* the focus lost */
	pad_tap(H, KP_UI_PAD_SHIFT);
	kp_t_ok(T, H->hud.pad.shift == KP_UI_PAD_SHIFT_ONCE && ui->input.keys[0] == 1, "Shift on");
	kp_ui_set_active(ui, 0);
	kp_hud_keys_cleared(&H->hud);
	kp_t_ok(T, H->hud.pad.shift == KP_UI_PAD_SHIFT_OFF && ui->input.keys[0] == 0, "off with the key bytes");
	kp_ui_set_active(ui, 1);
	/* a short view: the opened column above the console */
	kp_hud_pad(&H->hud, 1, 4);
	frame(H, 1280, 480, now += 16, 0, NO_CURSOR, CAMERA(0, 0));
	pad_tap(H, KP_UI_PAD_TOGGLE);
	frame(H, 1280, 480, now += 16, 0, NO_CURSOR, CAMERA(0, 0));
	kp_t_ok(T, H->hud.pad.open && H->hud.pad.bh == KP_HUD_PAD_MIN_PX, "opened, its buttons the shortest: %d",
		H->hud.pad.bh);
	kp_t_ok(T, H->hud.pad.count < KP_UI_PAD_BUTTONS && H->hud.pad.y + H->hud.pad.h <= 480 - KP_HUD_CONSOLE_ROWS,
		"%d buttons down to %d", H->hud.pad.count, H->hud.pad.y + H->hud.pad.h);
}

/* A tap on the minimap moves the view there, a drag on it moves it on, as the mouse's press and drag: on a wide
 * screen and on a phone's narrow one. */
static void touch_minimap_moves_the_view(KpT *T)
{
	static const int32_t screens[][2] = { { 1280, 720 }, { 540, 1151 }, { 385, 822 } };
	size_t n;
	int f2;
	for (n = 0; n < sizeof screens / sizeof screens[0]; n++) {
		Hud *H;
		Frame f;
		KpTouch touch;
		KpTouchHost host;
		TouchHost th;
		const KpUiControl *k;
		int32_t w = screens[n][0], h = screens[n][1], minimap, px = 0, py = 0, x, y;
		if (!ready(T, "test_hud.touch_minimap_moves_the_view", TERRAN01)) return;
		H = with_hud(T, terran01(T), w, h);
		f = frame(H, w, h, 1016, 0, NO_CURSOR, CAMERA(0, 0));
		minimap = dialog_of(H->ui, "rez\\minimap.bin", 0);
		kp_t_ok(T, minimap >= 0 && kp_canvas_place(H->hud.canvas, minimap, &px, &py), "%dx%d: the minimap placed", w, h);
		k = &H->ui->controls[minimap];
		memset(&th, 0, sizeof th);
		th.H = H;
		th.cx = f.x;
		th.cy = f.y;
		memset(&host, 0, sizeof host);
		host.ud = &th;
		host.target = touch_target;
		host.mouse = touch_mouse;
		host.cancel = touch_cancel;
		kp_touch_init(&touch, &host);
		x = px + 70 - k->left; /* the minimap image's bottom rows: the view at the map's bottom */
		y = py + 470 - k->top;
		kp_t_eq(T, kp_hud_touch_target(&H->hud, x, y, f.x, f.y), KP_TOUCH_PANEL, "%dx%d: the minimap a panel's", w, h);
		kp_touch_event(&touch, KP_TOUCH_DOWN, 1, (double)x, (double)y, 2000);
		for (f2 = 0; f2 < 6; f2++) {
			frame(H, w, h, 2016 + 16 * (uint32_t)f2, 1, NO_CURSOR, CAMERA(th.cx, th.cy));
			kp_touch_frame(&touch, 2016 + 16 * (uint32_t)f2);
		}
		kp_touch_event(&touch, KP_TOUCH_UP, 1, (double)x, (double)y, 2120);
		frame(H, w, h, 2140, 1, NO_CURSOR, CAMERA(th.cx, th.cy));
		kp_t_ok(T, th.moved && th.cy > 0, "%dx%d: the tap moved the view down (%d,%d)", w, h, (int)th.cx, (int)th.cy);
	}
}

/* A tap on the selected unit's portrait centres the view on the unit (CenterScreenOnUnit 0x422b10), as the mouse's
 * click there. */
static void touch_portrait_centres_the_view(KpT *T)
{
	Hud *H;
	KpGame *g;
	KpTouch touch;
	KpTouchHost host;
	TouchHost th;
	const KpUiControl *c;
	int64_t a, cx, cy;
	int32_t dialog, px = 0, py = 0, x, y;
	if (!ready(T, "test_hud.touch_portrait_centres_the_view", TERRAN01)) return;
	H = with_hud(T, terran01(T), 1280, 720);
	g = H->g;
	a = own_unit(g, 1, MARINE, 0);
	kp_t_ok(T, a != 0, "a Marine");
	cx = max64(0, g->u.x[a] - 640);
	cy = max64(0, g->u.y[a] - 300) & ~7;
	frame(H, 1280, 720, 1000, 0, NO_CURSOR, CAMERA(cx, cy));
	memset(&th, 0, sizeof th);
	th.H = H;
	th.cx = cx;
	th.cy = cy;
	memset(&host, 0, sizeof host);
	host.ud = &th;
	host.target = touch_target;
	host.mouse = touch_mouse;
	host.cancel = touch_cancel;
	kp_touch_init(&touch, &host);
	tap_at(&touch, g->u.x[a] - cx, g->u.y[a] - cy, 1500);
	frame(H, 1280, 720, 1600, 1, NO_CURSOR, CAMERA(cx, cy));
	kp_t_ok(T, H->ui->hud.status_unit == a, "the Marine's status");
	/* the view away from the Marine */
	th.cx = g->u.x[a] > 2000 ? 0 : g->map_w_px - 1280;
	th.cy = 0;
	th.moved = 0;
	frame(H, 1280, 720, 1700, 1, NO_CURSOR, CAMERA(th.cx, th.cy));
	c = &H->ui->controls[H->ui->portrait.control];
	kp_t_ok(T, c->slot.unit == a, "the Marine's portrait");
	dialog = c->parent;
	kp_t_ok(T, kp_canvas_place(H->hud.canvas, dialog, &px, &py), "the portrait's dialog placed");
	x = px + (c->left + c->right) / 2; /* a control's rectangle is its dialog's */
	y = py + (c->top + c->bottom) / 2;
	kp_t_eq(T, kp_hud_touch_target(&H->hud, x, y, th.cx, th.cy), KP_TOUCH_PANEL, "the portrait a panel's");
	tap_at(&touch, x, y, 2000);
	kp_t_ok(T, th.moved, "the tap moved the view");
	kp_t_ok(T, g->u.x[a] >= th.cx && g->u.x[a] < th.cx + 1280 && g->u.y[a] >= th.cy && g->u.y[a] < th.cy + 640,
		"the Marine in the view: (%d,%d) at (%d,%d)", (int)g->u.x[a], (int)g->u.y[a], (int)th.cx, (int)th.cy);
}

/* A phone's Back in a game: F10 opens the game menu (the game's Escape would skip the transmissions), then Escape
 * closes it. */
static void back_opens_and_closes_the_game_menu(KpT *T)
{
	Hud *H;
	KpUi *ui;
	uint32_t now = 1016;
	uint16_t vk;
	int k, pass;
	char name[128];
	if (!ready(T, "test_hud.back_opens_and_closes_the_game_menu", TERRAN01)) return;
	H = with_hud(T, terran01(T), 1280, 720);
	ui = H->ui;
	frame(H, 1280, 720, now, 1, NO_CURSOR, CAMERA(0, 0));
	for (pass = 0; pass < 2; pass++) {
		vk = kp_hud_back_key(ui);
		kp_t_eq(T, vk, pass == 0 ? KP_HUD_VK_F10 : KP_HUD_VK_ESCAPE, pass == 0 ? "the game: F10" : "the menu: Escape");
		kp_hud_key(ui, 1, vk, 0);
		kp_hud_key(ui, 0, vk, 0);
		for (k = 0; k < 10; k++) {
			now += 16;
			kp_t_ui_idle(ui);
			frame(H, 1280, 720, now, 0, NO_CURSOR, KEEP);
		}
		if (pass == 0) {
			kp_t_ok(T, ui->modeless >= 0 && strcmp(kp_t_ui_bin(ui, ui->modeless, name, sizeof name), "rez\\gamemenu.bin") == 0,
				"Back opened the game menu");
		} else {
			kp_t_ok(T, ui->modeless < 0, "Back closed the game menu");
		}
	}
}

/* A trigger's camera pan goes on with no pointer (the touch mode's frames): it arrives, its wait ends and the minimap
 * moves the view again (the pan holds the view while it runs). Before, only the mouse's frames stepped it: in the touch
 * mode the pan never ended, the minimap and the portrait did nothing and the trigger waited forever. It arrives from a
 * view off the original's 8-pixel grid too (a two-finger pan's, a rotation's kept centre): its smallest step is 8
 * pixels, so a view 3 pixels from the target never got there and the pan held the view for good. */
static void a_pan_ends_without_a_pointer(KpT *T)
{
	static const int64_t STARTS[][2] = { { 0, 0 }, { 203, 85 } };
	size_t n;
	for (n = 0; n < sizeof STARTS / sizeof STARTS[0]; n++) {
		Hud *H;
		Frame f;
		int64_t x = 0, y = 0;
		uint32_t now = 1016;
		int k, released = 0;
		if (!ready(T, "test_hud.a_pan_ends_without_a_pointer", TERRAN01)) return;
		H = with_hud(T, terran01(T), 1280, 720);
		frame(H, 1280, 720, now, 0, NO_CURSOR, CAMERA(0, 0)); /* the game's first view replaces the host's */
		now += 16;
		f = frame(H, 1280, 720, now, 0, NO_CURSOR, CAMERA(STARTS[n][0], STARTS[n][1]));
		x = f.x;
		y = f.y;
		kp_ui_start_pan(H->ui, 1600, 1200);
		for (k = 0; k < 400 && H->ui->input.scroll.pan; k++) {
			now += 16;
			f = frame(H, 1280, 720, now, 0, NO_CURSOR, CAMERA(x, y));
			x = f.x;
			y = f.y;
			released |= f.released;
			if (H->ui->input.scroll.pan && x == H->ui->input.scroll.pan_x && y == H->ui->input.scroll.pan_y) {
				kp_ui_end_pan(H->ui); /* the trigger's wait ended: the game ends the pan (its released callback) */
			}
		}
		kp_t_ok(T, released, "from %d,%d: the pan's wait released (the view at %d,%d)", (int)STARTS[n][0],
			(int)STARTS[n][1], (int)x, (int)y);
		kp_t_ok(T, x > 0 && y > 0, "the view went toward the target: %d,%d", (int)x, (int)y);
		kp_t_ok(T, !H->ui->input.scroll.pan, "the pan ended");
	}
}

/* A placement's touch: a point on the preview's footprint is its ghost's (the tap that places it), one off it moves it
 * (kp_ui_placement_at, phase 10). A Supply Depot (3 x 2 tiles) at the pointer's tile. */
static void touch_placement_knows_its_ghost(KpT *T)
{
	Hud *H;
	if (!ready(T, "test_hud.touch_placement_knows_its_ghost", TERRAN01)) return;
	H = with_hud(T, terran01(T), 1280, 720);
	frame(H, 1280, 720, 1016, 0, NO_CURSOR, CAMERA(0, 0));
	kp_ui_start_placement(H->ui, 30, 109);
	input(H, MOUSE_MOVE, 400, 300, 0, 0);
	frame(H, 1280, 720, 1032, 0, KP_HUD_POINTER_AIM, 400, 300, CAMERA(0, 0));
	kp_t_eq(T, kp_hud_touch_target(&H->hud, 410, 310, 0, 0), KP_TOUCH_GHOST, "on the footprint");
	kp_t_eq(T, kp_hud_touch_target(&H->hud, 479, 351, 0, 0), KP_TOUCH_GHOST, "its last pixel (from 384,288)");
	kp_t_eq(T, kp_hud_touch_target(&H->hud, 480, 300, 0, 0), KP_TOUCH_PLACE, "past its right");
	kp_t_eq(T, kp_hud_touch_target(&H->hud, 350, 250, 0, 0), KP_TOUCH_PLACE, "off it");
	kp_ui_end_placement(H->ui);
	kp_t_eq(T, kp_hud_touch_target(&H->hud, 410, 310, 0, 0), KP_TOUCH_SELECT, "the placement over");
}

/* An add-on's placement (the Command Center's Comsat Station) starts with its preview beside the parent as it stands,
 * not at the cursor: the pointer is on the tile the add-on takes, the preview's footprint holds the point the host
 * parks it at once, and the tile is one the placement accepts (phase 10). */
static void addon_preview_starts_beside_its_parent(KpT *T)
{
	Hud *H;
	int64_t cc;
	int32_t x, y, again_x, again_y, sprite, px;
	int64_t tile_x, tile_y;
	uint32_t now = 1016;
	Frame f;
	if (!ready(T, "test_hud.addon_preview_starts_beside_its_parent", TERRAN01)) return;
	H = with_hud(T, terran01(T), 1280, 720);
	cc = own_unit(H->g, 1, COMMAND_CENTER, 0);
	kp_t_ok(T, cc != 0, "a Command Center");
	sprite = kp_unit_sprite(H->g, cc);
	frame(H, 1280, 720, now, 0, NO_CURSOR, CAMERA(0, 0)); /* the game's first view replaces the host's */
	now += 16;
	f = frame(H, 1280, 720, now, 0, NO_CURSOR, CAMERA(H->g->w->spr.x[sprite] - 600, H->g->w->spr.y[sprite] - 300));
	kp_select_locally(H->g, &cc, 1);
	now += 100;
	frame(H, 1280, 720, now, 1, NO_CURSOR, CAMERA(f.x, f.y));
	kp_t_eq(T, H->ui->hud.status_unit, cc, "the Command Center is the status unit");
	input(H, MOUSE_MOVE, 5, 700, f.x, f.y); /* the pointer on the console, as at a button's press */
	kp_ui_start_placement(H->ui, 36, COMSAT);
	kp_t_eq(T, H->ui->input.placement_parent, COMMAND_CENTER, "the parent's preview too");
	kp_t_ok(T, H->ui->placement.shown[1], "the add-on's preview shows with the pointer on the console");
	kp_t_eq(T, H->ui->placement.error, 0, "the parent's own place takes its add-on");
	kp_t_eq(T, H->ui->placement.tile_x * 32, H->g->w->spr.x[sprite] + 64, "beside the parent's right edge");
	kp_t_ok(T, kp_ui_placement_take_anchor(H->ui, &x, &y), "the host takes the point");
	kp_t_ok(T, kp_ui_placement_at(H->ui, x, y), "the point is on the preview");
	kp_t_ok(T, !kp_ui_placement_take_anchor(H->ui, &again_x, &again_y), "once");
	kp_t_eq(T, kp_hud_touch_target(&H->hud, x, y, f.x, f.y), KP_TOUCH_GHOST, "a tap there is the preview's");
	/* a touch on a panel (the cancel button) puts the pointer there in its dialog's coordinates, inside the larger view:
	 * the preview stays */
	tile_x = H->ui->placement.tile_x;
	tile_y = H->ui->placement.tile_y;
	px = H->ui->placement.x[1];
	input(H, MOUSE_MOVE, 5, 700, f.x, f.y);
	now += 16;
	frame(H, 1280, 720, now, 1, KP_HUD_POINTER_AIM, 5, 700, CAMERA(f.x, f.y));
	kp_t_ok(T, H->ui->placement.tile_x == tile_x && H->ui->placement.tile_y == tile_y && H->ui->placement.x[1] == px,
		"the preview stays while the pointer is on a panel");
	kp_t_ok(T, H->ui->placement.shown[1], "and shows");
	kp_ui_end_placement(H->ui);
	kp_t_ok(T, !kp_ui_placement_take_anchor(H->ui, &x, &y), "none after the placement");
}

/* The original's interface keeps the pointer where it is: without the layout host's choice (ui->addon_anchor) an
 * add-on's preview starts at the pointer and the layout's answer for it stays (the anchor's 0 stuck in a host that never
 * answers, the UI oracle's, and kept the console's dialogs out of every later click); with it, the add-on's tile in the
 * view's partial first column, which ScreenToPlacementTile snaps to the next tile, gets no anchor. */
static void addon_anchor_is_the_layouts_alone(KpT *T)
{
	Hud *H;
	int64_t cc, edge;
	int32_t x, y, sprite;
	int16_t mouse_x, mouse_y;
	int8_t over;
	uint32_t now = 1016;
	Frame f;
	if (!ready(T, "test_hud.addon_anchor_is_the_layouts_alone", TERRAN01)) return;
	H = with_hud(T, terran01(T), 1280, 720);
	cc = own_unit(H->g, 1, COMMAND_CENTER, 0);
	kp_t_ok(T, cc != 0, "a Command Center");
	sprite = kp_unit_sprite(H->g, cc);
	frame(H, 1280, 720, now, 0, NO_CURSOR, CAMERA(0, 0)); /* the game's first view replaces the host's */
	now += 16;
	f = frame(H, 1280, 720, now, 0, NO_CURSOR, CAMERA(H->g->w->spr.x[sprite] - 600, H->g->w->spr.y[sprite] - 300));
	kp_select_locally(H->g, &cc, 1);
	now += 100;
	frame(H, 1280, 720, now, 1, NO_CURSOR, CAMERA(f.x, f.y));
	input(H, MOUSE_MOVE, 5, 700, f.x, f.y); /* the pointer on the console, as at a button's press */
	H->ui->addon_anchor = 0;
	mouse_x = H->ui->mouse_x;
	mouse_y = H->ui->mouse_y;
	over = H->ui->input.over_console;
	kp_ui_start_placement(H->ui, 36, COMSAT);
	kp_t_eq(T, H->ui->input.placement_parent, COMMAND_CENTER, "an add-on's placement");
	kp_t_ok(T, !kp_ui_placement_take_anchor(H->ui, &x, &y), "no anchor");
	kp_t_ok(T, H->ui->mouse_x == mouse_x && H->ui->mouse_y == mouse_y, "the pointer stays");
	kp_t_eq(T, H->ui->input.over_console, over, "the layout's answer stays");
	kp_ui_end_placement(H->ui);
	H->ui->addon_anchor = 1;
	edge = H->g->w->spr.x[sprite] + 64 + 8; /* the add-on's first tile starts 64 pixels right of the centre */
	now += 16;
	frame(H, 1280, 720, now, 0, NO_CURSOR, CAMERA(edge, f.y));
	kp_t_eq(T, H->ui->input.screen_x, edge, "the view 8 pixels into the add-on's first tile");
	kp_ui_start_placement(H->ui, 36, COMSAT);
	kp_t_ok(T, !kp_ui_placement_take_anchor(H->ui, &x, &y), "a tile no view point gives back has no anchor");
	kp_ui_end_placement(H->ui);
}

/* A rotation keeps the view's centre (plan section 2.1): 1280 x 720 to 720 x 1280 and back. */
static void a_new_screen_keeps_the_view_centre(KpT *T)
{
	int64_t x = 1000, y = 800;
	kp_hud_keep_focus(1280, 720, 720, 1280, &x, &y);
	kp_t_eq(T, x, 1000 + 280, "the centre's column: 1000 + 640 = 1280 + 360");
	kp_t_eq(T, y, 800 - 280, "and its row: 800 + 320 = 520 + 600");
	kp_hud_keep_focus(720, 1280, 1280, 720, &x, &y);
	kp_t_ok(T, x == 1000 && y == 800, "back where it was");
}

/* The portraits' mode (SetPortraitMode 0x4a6e90) shows the status unit's portrait again in the new mode: its idle
 * movie opens anew (ShowPortrait 0x4a6f10(1)), with the idle variant's draw of the portrait generator. */
static void portrait_mode_shows_the_portrait_again(KpT *T)
{
	Hud *H;
	KpUiPortrait *p;
	int64_t unit;
	uint32_t now = 1016;
	int k, frame_before;
	KpRngPortrait rng;
	if (!ready(T, "test_hud.portrait_mode_shows_the_portrait_again", TERRAN01)) return;
	H = with_hud(T, terran01(T), 1280, 720);
	p = &H->ui->portrait;
	unit = own_unit(H->g, 1, -1, 0);
	kp_t_ok(T, unit != 0, "a unit");
	kp_select_locally(H->g, &unit, 1);
	for (k = 0; k < 20; k++) {
		now += 100;
		frame(H, 1280, 720, now, 1, NO_CURSOR, CAMERA(0, 0));
	}
	frame_before = p->video.open ? (int)p->video.smk.frame : -1;
	rng = p->rng;
	kp_t_ok(T, p->video.open == 1 && frame_before > 0, "the idle movie plays");
	kp_t_eq(T, p->mode, 2, "animated");
	kp_ui_portrait_set_mode(H->ui, 2);
	kp_t_eq(T, p->video.open, 1, "the movie again");
	kp_t_ok(T, (int)p->video.smk.frame < frame_before, "from its start");
	kp_t_ok(T, p->rng.x != rng.x || p->rng.y != rng.y || p->rng.z != rng.z, "the idle variant drawn again");
	kp_ui_portrait_set_mode(H->ui, 0);
	kp_t_eq(T, p->mode, 0, "none");
}

/* A paused game's minimap takes no press (MinimapImageProc 0x46d6a0 at IsGamePaused 0x47cb60): a left press leaves
 * the view, a right press orders nothing; unpaused both work. */
static Input press(Hud *H, int down, int up, int32_t x, int32_t y, int64_t cx, int64_t cy)
{
	Input r;
	input(H, MOUSE_MOVE, x, y, cx, cy);
	r = input(H, down, x, y, cx, cy);
	input(H, up, x, y, cx, cy);
	return r;
}

static void paused_minimap_takes_no_press(KpT *T)
{
	Hud *H;
	Frame f;
	Input r;
	int64_t unit;
	int32_t minimap, px = 0, py = 0, x, y;
	if (!ready(T, "test_hud.paused_minimap_takes_no_press", TERRAN01)) return;
	H = with_hud(T, terran01(T), 1280, 720);
	unit = own_unit(H->g, 1, -1, 0);
	kp_t_ok(T, unit != 0, "a unit");
	kp_select_locally(H->g, &unit, 1);
	f = frame(H, 1280, 720, 1016, 1, NO_CURSOR, CAMERA(0, 0));
	minimap = dialog_of(H->ui, "rez\\minimap.bin", 0);
	kp_t_ok(T, minimap >= 0 && kp_canvas_place(H->hud.canvas, minimap, &px, &py), "the minimap placed");
	x = px + 70 - H->ui->controls[minimap].left; /* the minimap image's bottom rows */
	y = py + 470 - H->ui->controls[minimap].top;
	kp_scalar_set(H->g, KP_PAUSED, 1);
	r = press(H, LEFT_DOWN, LEFT_UP, x, y, f.x, f.y);
	kp_t_ok(T, !r.moved, "paused: the view stays");
	r = press(H, RIGHT_DOWN, RIGHT_UP, x, y, f.x, f.y);
	kp_t_eq(T, r.commands, 0, "paused: no order");
	kp_scalar_set(H->g, KP_PAUSED, 0);
	r = press(H, LEFT_DOWN, LEFT_UP, x, y, f.x, f.y);
	kp_t_ok(T, r.moved && r.y > 0, "the view moves down");
	r = press(H, RIGHT_DOWN, RIGHT_UP, x, y, f.x, f.y);
	kp_t_ok(T, r.commands > 0, "the right press orders the selection");
}

/* A click on the portrait centres the view on its speaker (PortraitControlInteract 0x4a6d30: user code 2 with a unit,
 * CenterScreenOnUnit 0x422b10): the selected unit's portrait. */
static int64_t clamp8(int64_t v, int64_t hi)
{
	v = max64(0, min64(v, hi));
	return v - v % 8;
}

static void portrait_click_centres_its_unit(KpT *T)
{
	Hud *H;
	KpGame *g;
	Input r;
	int64_t unit, x, y, tx, ty;
	int32_t portrait, c, px = 0, py = 0;
	int32_t s;
	const KpUiControl *k;
	if (!ready(T, "test_hud.portrait_click_centres_its_unit", TERRAN01)) return;
	H = with_hud(T, terran01(T), 1280, 720);
	g = H->g;
	unit = own_unit(g, 0, -1, 0);
	kp_t_ok(T, unit != 0, "a unit of the local player");
	kp_select_locally(g, &unit, 1);
	frame(H, 1280, 720, 1016, 1, NO_CURSOR, CAMERA(0, 0));
	frame(H, 1280, 720, 1116, 0, NO_CURSOR, CAMERA(0, 0));
	kp_hud_take_camera(H->ui, &x, &y);
	portrait = dialog_of(H->ui, "rez\\statport.bin", 0);
	kp_t_ok(T, portrait >= 0 && kp_canvas_place(H->hud.canvas, portrait, &px, &py), "the portrait placed");
	c = child_of(H->ui, portrait, 1);
	kp_t_ok(T, c >= 0, "the portrait's control");
	k = &H->ui->controls[c];
	x = px + half(k->left + k->right); /* the control's rectangle is the dialog's */
	y = py + half(k->top + k->bottom);
	input(H, MOUSE_MOVE, x, y, 0, 0);
	input(H, LEFT_DOWN, x, y, 0, 0);
	r = input(H, LEFT_UP, x, y, 0, 0);
	/* the unit's tile at the wide view's centre, within the view's scroll limits (SetScreenPosition 0x40b450) */
	s = (int32_t)g->u.sprite[unit];
	tx = g->w->spr.x[s] / 32;
	ty = g->w->spr.y[s] / 32;
	kp_t_ok(T, r.moved, "the view moved");
	kp_t_eq(T, r.x, clamp8((tx - 1280 / 32 / 2) * 32, g->map_w_px - 1280), "the view's x");
	kp_t_eq(T, r.y, clamp8((ty - 640 / 32 / 2) * 32, g->map_h_px - 12 * 32 - (640 - 400) + 8), "the view's y");
	kp_t_ok(T, r.x != 0 || r.y != 0, "the view moved");
}

/* The portrait's hotkey (the space, statport.bin's control -10; PortraitHotkey 0x4a6760), a character of the host's
 * text, moves the view to the last transmission: a trigger portrait at a unit keeps the original's view's screen with the unit at its centre
 * (0x527014), which a wide view moves so the unit stays at its centre. */
static void portrait_hotkey_centres_the_last_transmission(KpT *T)
{
	Hud *H;
	KpUi *ui;
	KpGame *g;
	int64_t n, unit = 0, x = 0, y = 0, sx, sy;
	if (!ready(T, "test_hud.portrait_hotkey_centres_the_last_transmission", TERRAN01)) return;
	H = with_hud(T, terran01(T), 1280, 720);
	ui = H->ui;
	g = H->g;
	kp_ui_set_view(ui, 1280, 640);
	for (n = 1; n <= KP_UNITS && unit == 0; n++) {
		if (g->u.sprite[n] != -1 && g->u.x[n] > 1000 && g->u.y[n] > 700 && g->u.x[n] < g->map_w_px - 700
			&& g->u.y[n] < g->map_h_px - 400) {
			unit = n;
		}
	}
	kp_t_ok(T, unit != 0, "a unit away from the map's edges");
	game_event(ui, KP_EVENT_PORTRAIT, g->u.type[unit], 2000, unit);
	kp_ui_set_screen(ui, 0, 0);
	kp_hud_take_camera(ui, &x, &y);
	kp_hud_text(ui, " "); /* the host's text: the view it moved is the host's to take (kp_app_input.c) */
	kp_t_ok(T, kp_hud_take_camera(ui, &x, &y), "the view moved");
	sx = g->w->spr.x[g->u.sprite[unit]];
	sy = g->w->spr.y[g->u.sprite[unit]];
	kp_t_eq(T, x, (sx - 640) - (sx - 640) % 8, "the unit at the wide view's centre");
	kp_t_eq(T, y, (sy - 320) - (sy - 320) % 8, NULL);
}

/* The text lines are the game screen's (DrawTextLines 0x46acd0 in its layer's callback 0x445ea0): the drag box drawn
 * after them lies over them, and the console art over both; a short narrow screen, whose minimap art rises higher than
 * the original's, lifts them as much (kp_layout.c). (The order is checked by what the canvas holds: the box's pixels
 * over a line's and the console's art over the box's. The lines never reach the art, which lifts them on the narrow
 * screens.) */
typedef struct {
	Hud *H;
	uint32_t now;
	uint8_t *art; /* the console art's opaque pixels of the last line_pixels' screen */
} Lines;

static Frame lines_frame(Lines *L, int32_t w, int32_t h, int has_cursor, int64_t x, int64_t y)
{
	L->now += 16;
	return frame(L->H, w, h, L->now, 0, has_cursor, x, y, CAMERA(0, 0));
}

/* The text lines' pixels on a w x h screen after an event (the lines before expired: 7 s), those of them on the
 * console art's opaque pixels without the line, and the lowest row of them. */
static int line_pixels(Lines *L, int32_t w, int32_t h, int64_t kind, int64_t a, int64_t b, int64_t c, int *on_art,
	int32_t *bottom)
{
	const KpCanvas *cv = L->H->hud.canvas;
	int32_t x, y, cols = w < 640 ? w : 640;
	int shown = 0;
	L->now += 8000;
	lines_frame(L, w, h, NO_CURSOR);
	for (y = 0; y < h; y++) {
		for (x = 0; x < cols; x++) L->art[(size_t)y * 640 + (size_t)x] = alpha_at(cv, x, y) == 255;
	}
	hud_event(L->H, kind, a, b, c);
	lines_frame(L, w, h, NO_CURSOR);
	*on_art = 0;
	*bottom = -1;
	for (y = 0; y < h; y++) {
		for (x = 0; x < cols; x++) {
			if (alpha_at(cv, x, y) != OVERLAY_A) continue;
			shown++;
			*bottom = y;
			*on_art += L->art[(size_t)y * 640 + (size_t)x];
		}
	}
	return shown;
}

static void canvas_draws_the_text_lines_with_the_game_view(KpT *T)
{
	Hud *H;
	Lines L;
	Frame f;
	const KpCanvas *c;
	int32_t x, y, hx = -1, hy = -1, bottom, art = 0, box = 0;
	int wide, shown, on_art;
	uint8_t line_index, box_index;
	if (!ready(T, "test_hud.canvas_draws_the_text_lines_with_the_game_view", TERRAN01)) return;
	H = with_hud(T, terran01(T), 1280, 720);
	c = H->hud.canvas;
	L.H = H;
	L.now = 1016;
	L.art = (uint8_t *)calloc(640 * 720, 1);
	kp_t_ok(T, L.art != NULL, "out of memory");
	kp_t_defer(T, free, L.art);
	f = lines_frame(&L, 1280, 720, NO_CURSOR);
	hud_event(H, KP_EVENT_ERROR, H->g->local_player, 1, 0);
	lines_frame(&L, 1280, 720, NO_CURSOR);
	for (y = 0x127; y <= 0x127 + 12 && hx < 0; y++) {
		for (x = 10; x <= 400; x++) {
			if (alpha_at(c, x, y) == OVERLAY_A) {
				hx = x;
				hy = y;
				break;
			}
		}
	}
	kp_t_ok(T, hx >= 0, "the error line drawn");
	line_index = index_at(c, hx, hy);
	input(H, MOUSE_MOVE, hx - 40, hy, f.x, f.y);
	input(H, LEFT_DOWN, hx - 40, hy, f.x, f.y);
	input(H, MOUSE_MOVE, hx + 40, hy + 30, f.x, f.y);
	lines_frame(&L, 1280, 720, CURSOR(hx + 40, hy + 30));
	box_index = index_at(c, hx, hy);
	kp_t_ok(T, box_index != line_index, "the box's top edge over the line's pixel");
	kp_t_eq(T, box_index, index_at(c, hx + 40, hy + 15), "in the box's colour");
	input(H, LEFT_UP, hx + 40, hy + 30, f.x, f.y);
	/* the order of the composition: the lines, the game view's overlays (the box over the line above), then the
	 * console art: a drag box down into the original screen's console art lies under the art */
	lines_frame(&L, 640, 480, NO_CURSOR);
	for (y = 200; y <= 395; y++) art += alpha_at(c, 20, y) == 255;
	kp_t_ok(T, art > 0, "the console art on the box's left edge");
	input(H, MOUSE_MOVE, 20, 200, 0, 0);
	input(H, LEFT_DOWN, 20, 200, 0, 0);
	input(H, MOUSE_MOVE, 130, 395, 0, 0);
	lines_frame(&L, 640, 480, CURSOR(130, 395));
	for (y = 200; y <= 395; y++) {
		box += alpha_at(c, 20, y) == OVERLAY_A;
		art -= alpha_at(c, 20, y) == 255;
	}
	input(H, LEFT_UP, 130, 395, 0, 0);
	kp_t_ok(T, box > 0, "the box's left edge drawn");
	kp_t_eq(T, art, 0, "the console art over the box");
	/* a ring line (a unit that could not be placed: the ring's bottom row) on a short narrow screen stays whole, above
	 * the console's art */
	wide = line_pixels(&L, 1280, 720, KP_EVENT_UNIT_UNPLACED, 0, 10, 20, &on_art, &bottom);
	shown = line_pixels(&L, 540, 600, KP_EVENT_UNIT_UNPLACED, 0, 10, 20, &on_art, &bottom);
	kp_t_ok(T, c->arrangement.top < 290, "the narrow screen's minimap art rises higher than the original's");
	kp_t_eq(T, shown, wide, "the ring line whole");
	kp_t_eq(T, on_art, 0, "none of it over the art");
	kp_t_ok(T, bottom < c->arrangement.top, "above the console's art");
}

/* The placement's preview (placebox.cpp): the hotkeys B and S of an SCV start a Supply Depot's placement, and the
 * canvas's game view shows its ghost and its footprint's cells stippled (StipplePlacementCell 0x405c12: half of each
 * 32 x 32 cell) at the tile under the mouse, which follows the mouse and goes with the placement. */
static int stippled(const KpCanvas *c, int64_t x0, int64_t y0, int64_t x1, int64_t y1)
{
	int counts[256], best = 0;
	int64_t x, y;
	memset(counts, 0, sizeof counts);
	for (y = y0; y <= y1; y++) {
		for (x = x0; x <= x1; x++) {
			uint8_t index, alpha;
			kp_canvas_pixel(c, (int32_t)x, (int32_t)y, &index, &alpha);
			if (alpha == OVERLAY_A && ++counts[index] > best) best = counts[index];
		}
	}
	return best;
}

/* The text box's prompt (SetTextboxPrompt 0x4b38c0: control 5, network.tbl 0x40) while the text box is closed, or
 * NULL. */
static const char *prompt(const KpUi *ui)
{
	int32_t d = dialog_of(ui, "textbox", 1), c;
	if (d < 0 || (c = child_of(ui, d, 5)) < 0) return NULL;
	return (ui->controls[c].flags & SHOWN) != 0 && ui->controls[c].text ? (const char *)ui->controls[c].text : NULL;
}

static void canvas_draws_the_placement_preview(KpT *T)
{
	static const uint16_t KEYS[2][2] = { { 66, 98 }, { 83, 115 } };
	Hud *H;
	KpGame *g;
	const KpCanvas *c;
	int64_t scv = 0, n, cx, cy, tx, ty, x0, y0;
	uint32_t now = 1016;
	int k;
	const char *p;
	if (!ready(T, "test_hud.canvas_draws_the_placement_preview", BUILD_TERRAN)) return;
	H = with_hud(T, kp_t_game(T, BUILD_TERRAN, 946684819u, 0, NULL), 1280, 720);
	g = H->g;
	c = H->hud.canvas;
	for (n = 1; n <= KP_UNITS && scv == 0; n++) {
		if (g->u.type[n] == SCV && g->u.owner[n] == g->local_player) scv = n;
	}
	kp_t_ok(T, scv != 0, "an SCV of the local player");
	g->players.minerals[g->local_player] = 1000;
	kp_select_locally(g, &scv, 1);
	cx = max64(0, g->u.x[scv] - 640);
	cy = max64(0, g->u.y[scv] - 320);
	now += 300; /* each press releases (SchedulePressRelease 0x4c3570) before the next */
	frame(H, 1280, 720, now, 1, CURSOR(600, 300), CAMERA(cx, cy));
	for (k = 0; k < 2; k++) {
		type_key(H->ui, KEYS[k][0], KEYS[k][1]);
		now += 300;
		frame(H, 1280, 720, now, 0, CURSOR(600, 300), CAMERA(cx, cy));
	}
	input(H, MOUSE_MOVE, 700, 300, cx, cy);
	now += 300;
	frame(H, 1280, 720, now, 0, CURSOR(700, 300), CAMERA(cx, cy));
	kp_t_ok(T, kp_ui_game_view_active(H->ui), "the preview shows");
	p = prompt(H->ui);
	kp_t_eq_str(T, p ? p : "(none)", "Select Location", "the placement's prompt");
	/* the depot's 3 x 2 cells at the tile under the mouse, half of their pixels in one stipple colour */
	tx = (700 + cx) / 32;
	ty = (300 + cy) / 32;
	x0 = tx * 32 - cx;
	y0 = ty * 32 - cy;
	kp_t_eq(T, stippled(c, x0, y0, x0 + 3 * 32 - 1, y0 + 2 * 32 - 1), 3 * 2 * 512, "the cells stippled");
	input(H, MOUSE_MOVE, 300, 200, cx, cy);
	now += 300;
	frame(H, 1280, 720, now, 0, CURSOR(300, 200), CAMERA(cx, cy));
	kp_t_ok(T, stippled(c, x0 + 8, y0 + 8, x0 + 3 * 32 - 9, y0 + 2 * 32 - 9) < 100, "the preview followed the mouse");
	input(H, RIGHT_DOWN, 300, 200, cx, cy); /* CancelPlacementRightClick 0x448760 */
	input(H, RIGHT_UP, 300, 200, cx, cy);
	now += 300;
	frame(H, 1280, 720, now, 0, CURSOR(300, 200), CAMERA(cx, cy));
	kp_t_ok(T, !kp_ui_game_view_active(H->ui), "the preview goes with the placement");
	kp_t_ok(T, prompt(H->ui) == NULL, "and its prompt");
	kp_t_eq(T, stippled(c, 250, 150, 450, 330), 0, "nothing left on the canvas");
}

/* The game menu composited onto the backdrop (BlitDialogToScreen 0x4c5630) lies under the console: the console art's
 * holes keep it, in a layout whose console starts below the menu's rectangle too. */
static void canvas_keeps_a_menu_under_the_console_art(KpT *T)
{
	KpOracleGame *og;
	KpOracleUi *u;
	KpUi *ui;
	KpCanvas *c;
	KpPanel panels[KP_CANVAS_PANELS];
	const KpUiSurface *bitmap;
	int32_t d, o, x, y, px = 0, py = 0;
	uint32_t now = 1000;
	int k, count = 0, drawn = 0;
	char name[128];
	if (!kp_t_ready(T, "test_hud.canvas_keeps_a_menu_under_the_console_art", MENU_FILES)) return;
	og = terran01(T);
	u = kp_t_ui(T, KP_ORACLE_UI_STRINGS);
	ui = u->ui;
	kp_ui_font_set(ui, 1);
	kp_t_ok(T, kp_ui_apply_theme(ui, kp_oracle_race_theme(1)) == 0, "ui: no terran GRP");
	ui->now = now;
	kp_t_ok(T, kp_ui_hud_create(ui, og->g, NULL) == 0, "ui: the console: %s", ui->error);
	kp_ui_window_key(ui, 1, 0x79, 0); /* F10: the game menu */
	kp_ui_window_key(ui, 0, 0x79, 0);
	for (k = 0; k < 10; k++) {
		now += 16;
		ui->now = now;
		kp_ui_tick(ui, now);
		kp_t_ui_idle(ui);
		kp_ui_present(ui);
	}
	d = ui->modeless;
	kp_t_ok(T, d >= 0, "the game menu");
	kp_t_eq_str(T, kp_t_ui_bin(ui, d, name, sizeof name), "rez\\gamemenu.bin", NULL);
	for (o = ui->open; o >= 0 && count < KP_CANVAS_PANELS; o = ui->controls[o].next) count++;
	k = count;
	for (o = ui->open; o >= 0 && k > 0; o = ui->controls[o].next) panel_of(ui, o, &panels[--k]); /* bottom first */
	c = canvas(T);
	kp_canvas_compose(c, ui, 1024, 576, panels, count, -1);
	kp_t_ok(T, kp_canvas_place(c, d, &px, &py), "the menu placed");
	bitmap = &ui->controls[d].bitmap;
	kp_t_ok(T, bitmap->pixels != NULL, "the menu's bitmap");
	for (y = 0; y < bitmap->h; y += 4) {
		for (x = 0; x < bitmap->w; x += 4) {
			if (bitmap->pixels[y * bitmap->w + x] == 0) continue;
			drawn += alpha_at(c, px + x, py + y) != 0 ? 0 : 1;
		}
	}
	kp_t_eq(T, drawn, 0, "every pixel of the menu shows (opaque or blended over the world)");
}

/* The game menu's translucent background (BlitSingleColourBlend 0x4ca9b0 onto the screen buffer) blends with the
 * text lines under it as the original's screen holds them: on the canvas, not with the world in the render pass. */
static void canvas_blends_a_menu_over_the_text_lines(KpT *T)
{
	static const int32_t ALL[4] = { 0, 0, 639, 479 };
	Hud *H;
	KpUi *ui;
	const KpCanvas *c;
	const KpUiSurface *lines, *bitmap;
	uint8_t row[256], colour;
	uint32_t now = 1016;
	int32_t d, x, y, px = 0, py = 0, lx = 0, ly = 0;
	int k, over = 0, wrong = 0, world = 0;
	char name[128];
	if (!ready(T, "test_hud.canvas_blends_a_menu_over_the_text_lines", TERRAN01)) return;
	H = with_hud(T, terran01(T), 640, 480);
	ui = H->ui;
	c = H->hud.canvas;
	frame(H, 640, 480, now, 1, NO_CURSOR, CAMERA(0, 0));
	for (k = 0; k < 11; k++) game_event(ui, KP_EVENT_UNIT_UNPLACED, MARINE, 1234, 567); /* the message rows */
	kp_ui_window_key(ui, 1, 0x79, 0); /* F10: the game menu */
	kp_ui_window_key(ui, 0, 0x79, 0);
	for (k = 0; k < 10; k++) {
		now += 16;
		kp_t_ui_idle(ui);
		frame(H, 640, 480, now, 0, NO_CURSOR, KEEP);
	}
	d = ui->modeless;
	kp_t_ok(T, d >= 0 && strcmp(kp_t_ui_bin(ui, d, name, sizeof name), "rez\\gamemenu.bin") == 0, "the game menu");
	kp_ui_lines_expire(ui);
	lines = kp_ui_lines_layer(ui);
	kp_t_ok(T, lines != NULL, "the text lines");
	kp_ui_canvas_blend_row(ui, row);
	colour = ui->effects.colour[0];
	kp_t_ok(T, kp_canvas_place(c, d, &px, &py), "the menu placed");
	kp_t_ok(T, kp_layout_place(&c->arrangement, "lines", ALL, &lx, &ly), "the lines placed");
	bitmap = &ui->controls[d].bitmap;
	for (y = 0; y < bitmap->h; y++) {
		for (x = 0; x < bitmap->w; x++) {
			int32_t sx = px + x, sy = py + y, ox = sx - lx, oy = sy - ly;
			uint8_t line = 0, index, alpha;
			if (bitmap->pixels[y * bitmap->w + x] != colour) continue;
			if (ox >= 0 && oy >= 0 && ox < lines->w && oy < lines->h) line = lines->pixels[oy * lines->w + ox];
			kp_canvas_pixel(c, sx, sy, &index, &alpha);
			if (line != 0) {
				over++;
				if (alpha != 255 || index != row[line]) wrong++;
			} else if (alpha == KP_UI_CANVAS_BLEND_A) {
				world++;
			}
		}
	}
	kp_t_ok(T, over > 0, "text line pixels under the menu");
	kp_t_eq(T, wrong, 0, "each blended with its line pixel");
	kp_t_ok(T, world > 0, "and the rest over the world");
	/* with the vector text the lines' glyphs under the menu stay, in the menu's blend of their colours (ADR 0011) */
	{
		const KpUiCanvas *v = &c->canvas;
		uint8_t codes[256], fills[256];
		int tinted = 0, plain = 0;
		memset(codes, 1, sizeof codes);
		memset(fills, 0, sizeof fills);
		kp_ui_set_vector_text(ui, 1, 1, codes);
		for (k = 0; k < 11; k++) game_event(ui, KP_EVENT_UNIT_UNPLACED, MARINE, 1234, 567);
		now += 16;
		kp_t_ui_idle(ui);
		frame(H, 640, 480, now, 0, NO_CURSOR, KEEP);
		for (k = 0; k < v->glyph_count; k++) {
			const KpUiCanvasGlyph *g = &v->glyphs[k];
			int32_t gx = g->x + g->box[2] - px, gy = g->y + g->box[3] - py;
			if (gx >= 0 && gy >= 0 && gx < bitmap->w && gy < bitmap->h) continue;
			fills[g->fill] = 1; /* outside the menu: the lines' own colours */
		}
		for (k = 0; k < v->glyph_count; k++) {
			const KpUiCanvasGlyph *g = &v->glyphs[k];
			int32_t gx = g->x + g->box[2] - px, gy = g->y + g->box[3] - py, f, child;
			int blended = 0, own = 0;
			if (gx < 0 || gy < 0 || gx >= bitmap->w || gy >= bitmap->h || bitmap->pixels[gy * bitmap->w + gx] != colour) {
				continue;
			}
			/* the menu's own text (its buttons' and title's, painted again once the log is on) is not the lines' */
			for (child = ui->controls[d].child; child >= 0; child = ui->controls[child].next) {
				const KpUiControl *cc = &ui->controls[child];
				own |= gx >= cc->left && gx <= cc->right && gy >= cc->top && gy <= cc->bottom;
			}
			if (own) continue;
			for (f = 0; f < 256; f++) blended |= fills[f] && row[f] == g->fill;
			if (blended) tinted++;
			else plain++;
		}
		kp_t_ok(T, tinted > 0, "the lines' glyphs under the menu, blended");
		kp_t_eq(T, plain, 0, "none of them in its own colours");
	}
}

/* --- the glue --- */

typedef struct {
	KpT *T;
	KpUi *ui;
	KpGlue *glue;
	KpProfile *profile;
	uint32_t now;
	KpGlueEvent last;
	int has_last;
	uint8_t *files[KP_UI_FILES];
	size_t sizes[KP_UI_FILES];
} Glue;

static void free_glue(void *p)
{
	kp_glue_free((KpGlue *)p);
	free(p);
}

static Glue *glue(KpT *T)
{
	Glue *G = (Glue *)calloc(1, sizeof *G);
	kp_t_ok(T, G != NULL, "out of memory");
	kp_t_defer(T, free, G);
	G->T = T;
	G->ui = interface(T, G->files, G->sizes);
	G->profile = (KpProfile *)calloc(1, sizeof *G->profile);
	kp_t_ok(T, G->profile != NULL, "out of memory");
	kp_t_defer(T, free, G->profile);
	kp_profile_init(G->profile);
	G->glue = (KpGlue *)calloc(1, sizeof *G->glue);
	kp_t_ok(T, G->glue != NULL, "out of memory");
	G->now = 1000;
	kp_glue_init(G->glue, G->ui, G->profile, G->now);
	kp_t_defer(T, free_glue, G->glue);
	return G;
}

/* A frame of the 800 x 600 screen at the clock's time: the last event the screens ended with. */
static void glue_frame_now(Glue *G)
{
	uint8_t rgb[768];
	int changed, palette;
	KpGlueEvent e;
	e = kp_glue_frame(G->glue, 800, 600, G->now, &changed, rgb, &palette);
	if (e.kind != KP_GLUE_GOES_ON) {
		G->last = e;
		G->has_last = 1;
	}
}

static void glue_frame(Glue *G)
{
	G->now += 16;
	glue_frame_now(G);
}

/* The first of needle in size bytes of text (a script has no terminator), NULL if none. */
static const char *find_in(const char *text, size_t size, const char *needle)
{
	size_t n = strlen(needle), k;
	for (k = 0; text != NULL && k + n <= size; k++) {
		if (memcmp(text + k, needle, n) == 0) return text + k;
	}
	return NULL;
}

static const char *top(Glue *G, char *name, size_t size)
{
	return G->ui->open >= 0 ? kp_t_ui_bin(G->ui, G->ui->open, name, size) : "";
}

static void wait_for(Glue *G, const char *bin, int frames)
{
	char name[128];
	int k;
	for (k = 0; k < frames; k++) {
		glue_frame(G);
		if (strcmp(top(G, name, sizeof name), bin) == 0) {
			for (k = 0; k < 150; k++) glue_frame(G); /* its slide and animations */
			return;
		}
	}
	kp_t_fail(G->T, "glue: no %s (at %s)", bin, top(G, name, sizeof name));
}

/* A key typed: its press, its character (as text) and its release. */
static void glue_key(Glue *G, char ch)
{
	char text[2];
	uint16_t vk = (uint16_t)(ch >= 'a' && ch <= 'z' ? ch - 32 : ch);
	text[0] = ch;
	text[1] = '\0';
	kp_hud_key(G->ui, 1, vk, 0);
	kp_hud_text(G->ui, text);
	kp_hud_key(G->ui, 0, vk, 0);
}

/* A press and release at the control's point of the top dialog (dx, dy from its top-left; dx < 0: its centre). */
static void glue_click(Glue *G, int id, int32_t dx, int32_t dy)
{
	const KpUiControl *d, *k;
	int32_t c, x, y;
	kp_t_ok(G->T, G->ui->open >= 0, "glue: no dialog");
	d = &G->ui->controls[G->ui->open];
	c = child_of(G->ui, G->ui->open, id);
	if (c < 0) kp_t_fail(G->T, "glue: no control %d", id);
	k = &G->ui->controls[c];
	if (dx < 0) {
		x = G->glue->place_x + d->left + half(k->left + k->right);
		y = G->glue->place_y + d->top + half(k->top + k->bottom);
	} else {
		x = G->glue->place_x + d->left + k->left + dx;
		y = G->glue->place_y + d->top + k->top + dy;
	}
	kp_glue_input(G->glue, MOUSE_MOVE, x, y);
	glue_frame(G);
	kp_glue_input(G->glue, LEFT_DOWN, x, y);
	glue_frame(G);
	kp_glue_input(G->glue, LEFT_UP, x, y);
	glue_frame(G);
}

/* The glue's campaign menu: the controls taking events (flag 0x10) of the dialog and its children; -1 when another
 * screen is on top. */
static int taking(Glue *G)
{
	const KpUi *ui = G->ui;
	char name[128];
	int32_t c;
	int n = 0;
	if (ui->open < 0 || strcmp(kp_t_ui_bin(ui, ui->open, name, sizeof name), "rez\\gluexpcmpgn.bin") != 0) return -1;
	for (c = ui->open; c >= 0;) {
		const KpUiControl *k = &ui->controls[c];
		n += (k->flags & 0x10) != 0;
		c = k->type == 0 ? k->child : k->next;
	}
	return n;
}

/* The nearest colour of the glue palette at a quarter of entry i's brightness, the lowest index on a tie. */
static int shade(const uint8_t *rgb, int i)
{
	int r = rgb[3 * i] / 4, g = rgb[3 * i + 1] / 4, b = rgb[3 * i + 2] / 4, best = 0, best_d = -1, j;
	for (j = 0; j < 256; j++) {
		int dr = rgb[3 * j] - r, dg = rgb[3 * j + 1] - g, db = rgb[3 * j + 2] - b, d = dr * dr + dg * dg + db * db;
		if (best_d < 0 || d < best_d) {
			best = j;
			best_d = d;
		}
	}
	return best;
}

/* The glue over the generated data: the main menu, Single Player by the mouse, the game mode's Expansion and
 * the login's Ok by their hotkeys, a new ID typed into the popup and its Ok clicked, the expansion campaign's Protoss
 * episode: its first mission's ready room, the profile named and its progress raised; the room's Start and the score
 * screen's Continue end their screens. */
static void glue_menus_start_a_campaign_mission(KpT *T)
{
	Glue *G;
	KpUi *ui;
	KpOracleGame *game;
	uint8_t rgb[768];
	int32_t y;
	int k, checked = 0, sliding = 0;
	if (!kp_t_ready(T, "test_hud.glue_menus_start_a_campaign_mission", GLUE_FILES)) return;
	G = glue(T);
	ui = G->ui;
	wait_for(G, "rez\\glumain.bin", 2000);
	/* the margins of the 800 x 600 screen widen the menu's edges, dimmed (kp_ui_canvas_margins) */
	for (k = 0; k < 256; k++) memcpy(&rgb[3 * k], &ui->glue.palette[4 * k], 3);
	for (y = G->glue->place_y; y <= G->glue->place_y + 479; y += 7) {
		uint8_t edge = index_at(&G->glue->canvas, G->glue->place_x, y), index, alpha;
		kp_canvas_pixel(&G->glue->canvas, 0, y, &index, &alpha);
		kp_t_eq(T, alpha, 255, NULL);
		kp_t_eq(T, index, shade(rgb, edge), NULL);
		checked += edge != index;
	}
	kp_t_ok(T, checked > 0, "some margin pixels differ from their edge");
	glue_click(G, 3, -1, -1); /* Single Player */
	wait_for(G, "rez\\glugamemode.bin", 2000);
	glue_key(G, 'e');
	wait_for(G, "rez\\glupedit.bin", 2000);
	kp_hud_text(ui, "Kop");
	glue_click(G, 1, -1, -1); /* the popup's Ok */
	wait_for(G, "rez\\glulogin.bin", 2000);
	glue_key(G, 'o');
	wait_for(G, "rez\\gluexpcmpgn.bin", 2000);
	glue_key(G, 'p');
	/* the campaign menu slides out (its hotkey's press fires first): nothing on it takes events (SlideOutGlueDialog
	 * 0x457a40, ClearControlFlag10 0x457500 over the dialog and its controls) */
	kp_t_ok(T, taking(G) > 0, "the menu takes events");
	for (k = 0; k < 30 && !sliding; k++) {
		glue_frame(G);
		sliding = taking(G) == 0;
	}
	kp_t_ok(T, sliding, "while it slides out no control takes events");
	for (k = 0; k < 600 && !(G->has_last && G->last.kind == KP_GLUE_MISSION); k++) glue_frame(G);
	kp_t_ok(T, G->has_last && G->last.kind == KP_GLUE_MISSION, "the mission to start");
	kp_t_eq(T, G->last.mission, 32, NULL);
	kp_t_eq(T, G->last.state, 9, NULL);
	kp_t_eq_str(T, G->profile->name, "Kop", NULL);
	kp_t_eq(T, G->profile->progress.expansion[2], 32, NULL);
	/* the mission's ready room (its establishing text first), Start, then the score screen and its Continue */
	if (!kp_t_has(T, PROTOSS01)) {
		kp_t_log(T, "SKIP the room and the score: no " PROTOSS01);
		return;
	}
	game = kp_t_game(T, PROTOSS01, 0, 1, NULL);
	kp_t_ok(T, kp_glue_room(G->glue, game->g, G->last.state) == 0, "the ready room: %s", ui->error);
	wait_for(G, "rez\\glurdyp.bin", 4000); /* after the establishing text's 25 s */
	G->has_last = 0;
	glue_key(G, 's');
	for (k = 0; k < 3000 && !G->has_last; k++) glue_frame(G);
	kp_t_ok(T, G->has_last && G->last.kind == KP_GLUE_ROOM_START, "the room's Start");
	/* the room's slide-out hid the cursor; the game shows it again (RunGame 0x445310: ShowCursorLayer 0x43e600) */
	kp_t_ok(T, kp_ui_cursor_key(ui) < 0, "no cursor after the room");
	kp_t_ok(T, kp_ui_hud_create(ui, game->g, NULL) == 0, "ui: the console: %s", ui->error);
	kp_t_ok(T, kp_ui_cursor_key(ui) >= 0, "the game's cursor");
	kp_t_eq(T, ui->cursor_theme, 0, "the game's arrow, not the room's");
	kp_ui_end_game(ui);
	kp_glue_score(G->glue, game->g, 3, 754);
	wait_for(G, "rez\\gluscore.bin", 2000);
	G->has_last = 0;
	glue_click(G, 7, -1, -1); /* Continue */
	for (k = 0; k < 3000 && !G->has_last; k++) glue_frame(G);
	kp_t_ok(T, G->has_last && G->last.kind == KP_GLUE_SCORE_DONE, "the score screen's end");
}

/* A ready room ended by the phone's Back (Escape) with the game gone after it: the glue goes on to its menus and the
 * game's idle procedure (GameIdleThunk) is nowhere to be found. */
static void glue_room_cancel_by_back_leaves_no_game_input(KpT *T)
{
	Glue *G;
	KpUi *ui;
	KpOracleGame *game;
	int k;
	if (!kp_t_ready(T, "test_hud.glue_room_cancel_by_back_leaves_no_game_input", GLUE_FILES)) return;
	if (!kp_t_has(T, PROTOSS01)) {
		kp_t_log(T, "SKIP the room's Back: no " PROTOSS01);
		return;
	}
	G = glue(T);
	ui = G->ui;
	game = kp_t_game(T, PROTOSS01, 0, 1, NULL);
	kp_t_ok(T, kp_glue_room(G->glue, game->g, 9) == 0, "the ready room: %s", ui->error);
	wait_for(G, "rez\\glurdyp.bin", 4000);
	kp_t_ok(T, ui->input.proc[KP_UI_IDLE] == NULL, "no game input in the room");
	/* the back gesture's edge swipe: a finger down that the system takes over (the touch cancelled) */
	kp_glue_input(G->glue, MOUSE_MOVE, G->glue->place_x + 2, G->glue->place_y + 2);
	kp_glue_input(G->glue, LEFT_DOWN, G->glue->place_x + 2, G->glue->place_y + 2);
	kp_glue_capture_lost(G->glue);
	kp_t_ok(T, ui->input.proc[KP_UI_IDLE] == NULL, "no game input in the room after a cancelled touch");
	G->has_last = 0;
	kp_hud_key(ui, 1, 0x1b, 0);
	kp_hud_key(ui, 0, 0x1b, 0);
	for (k = 0; k < 3000 && !G->has_last; k++) glue_frame(G);
	kp_t_ok(T, G->has_last && G->last.kind == KP_GLUE_ROOM_CANCEL, "Back cancels the room");
	ui->game = NULL; /* the host's destroy_scene */
	kp_glue_menus(G->glue, KP_GLUE_CAMPAIGN);
	for (k = 0; k < 20; k++) glue_frame(G); /* the idle event of each frame reaches no game */
	kp_t_ok(T, ui->input.proc[KP_UI_IDLE] == NULL, "no game input after the room");
}

/* The screens' work for the host: View Intro is an event of its own once the host plays cinematics (else the main menu
 * opens again, as it did), and a mission history's cinematic row ends as its mission with the cinematic to play first
 * (it left a blank screen: no state of the glue loop took it). */
static void glue_hands_the_cinematics_to_the_host(KpT *T)
{
	Glue *G;
	KpUiMenus *m;
	if (!kp_t_ready(T, "test_hud.glue_hands_the_cinematics_to_the_host", GLUE_FILES)) return;
	G = glue(T);
	m = &G->ui->menus;
	wait_for(G, "rez\\glumain.bin", 2000);
	G->glue->cinematics = 0; /* a host that plays none: the main menu again */
	m->screen = -1;
	m->action = KP_UI_MENU_INTRO;
	G->has_last = 0;
	glue_frame(G);
	kp_t_ok(T, G->has_last && G->last.kind == KP_GLUE_PROFILE, "View Intro without cinematics is no event");
	wait_for(G, "rez\\glumain.bin", 2000);
	G->glue->cinematics = 1;
	m->screen = -1;
	m->action = KP_UI_MENU_INTRO;
	G->has_last = 0;
	glue_frame(G);
	kp_t_ok(T, G->has_last && G->last.kind == KP_GLUE_INTRO, "View Intro is the host's");
	kp_t_eq(T, G->glue->screen, KP_GLUE_NONE, "no screen until the host opens the menu again");
	kp_glue_menus(G->glue, KP_GLUE_MAIN);
	m->screen = -1;
	m->action = KP_UI_MENU_CINEMATIC;
	m->state = 8; /* the Zerg ready room, the state the campaign menu ended in */
	m->mission = 15; /* the Zerg table's row of cinematic 13 (test_campaign.cinematic_rows_go_on_from_their_row) */
	m->cinematic = 13;
	G->has_last = 0;
	G->ui->glue.crt.holdrand = 0x1234;
	glue_frame(G);
	kp_t_ok(T, G->has_last && G->last.kind == KP_GLUE_MISSION, "a cinematic row is the host's");
	kp_t_eq(T, G->last.mission, 15, NULL);
	kp_t_eq(T, G->last.state, 8, NULL);
	kp_t_eq(T, G->last.cinematic, 13, "the row's cinematic and mission: the host goes on from the row");
	kp_t_eq(T, G->ui->glue.crt.holdrand, 0x1234, "the glue draws nothing for it: the mission's draws are the host's");
	{ /* CreateCampaignMissionGame's two decodes end reseeded with the clock */
		KpRngCrt want;
		m->time_seeds = NULL;
		m->time_seed = 777;
		kp_rng_crt_init(&want, 777);
		kp_ui_menus_mission_draws(G->ui);
		kp_t_eq(T, G->ui->glue.crt.holdrand, want.holdrand, "the host's draws leave the clock's seed");
	}
}

/* The main menu keeps the animations of Multiplayer and Campaign Editor (outside V1) but not their labels or any
 * response to a click; the other buttons keep both. */
static void main_menu_buttons_the_remake_lacks_are_inert(KpT *T)
{
	Glue *G;
	KpUi *ui;
	enum { ID_SINGLE = 3, ID_MULTI = 4, ID_EDITOR = 5, ID_INTRO = 8 };
	int32_t multi, editor, k;
	if (!kp_t_ready(T, "test_hud.main_menu_buttons_the_remake_lacks_are_inert", GLUE_FILES)) return;
	G = glue(T);
	ui = G->ui;
	wait_for(G, "rez\\glumain.bin", 2000);
	multi = child_of(ui, ui->open, ID_MULTI);
	editor = child_of(ui, ui->open, ID_EDITOR);
	kp_t_ok(T, multi >= 0 && editor >= 0, "Multiplayer and Campaign Editor");
	kp_t_ok(T, ui->controls[multi].text == NULL && ui->controls[editor].text == NULL, "no labels");
	kp_t_ok(T, ui->controls[multi].clips >= 0 && ui->controls[editor].clips >= 0, "their animations stay");
	kp_t_ok(T, ui->controls[child_of(ui, ui->open, ID_SINGLE)].text != NULL, "Single Player keeps its label");
	kp_t_ok(T, ui->controls[child_of(ui, ui->open, ID_INTRO)].text != NULL, "View Intro keeps its label");
	for (k = 0; k < 2; k++) {
		int32_t c = k == 0 ? multi : editor;
		glue_click(G, k == 0 ? ID_MULTI : ID_EDITOR, -1, -1);
		kp_t_ok(T, !(ui->controls[c].flags & KP_UI_PRESSED), "a click presses nothing");
		kp_t_ok(T, ui->menus.screen >= 0 && ui->menus.action == KP_UI_MENU_NONE, "a click ends no screen");
	}
	glue_key(G, 'm');
	glue_key(G, 'c');
	for (k = 0; k < 30; k++) glue_frame(G);
	kp_t_ok(T, ui->menus.screen >= 0 && ui->menus.action == KP_UI_MENU_NONE, "no hotkey ends the main menu");
}

/* Show Credits: the remake's makers come first, then the game's credits; Space turns the pages and Escape ends the
 * screen, back to the main menu. */
static void show_credits_begins_with_the_remake(KpT *T)
{
	Glue *G;
	KpUi *ui;
	enum { ID_CREDITS = 9, CENTRE = 5 };
	const char *text, *me, *claude, *original;
	int k, open;
	if (!kp_t_ready(T, "test_hud.show_credits_begins_with_the_remake", GLUE_FILES)) return;
	G = glue(T);
	ui = G->ui;
	wait_for(G, "rez\\glumain.bin", 2000);
	glue_click(G, ID_CREDITS, -1, -1);
	for (k = 0; k < 600 && G->glue->screen != KP_GLUE_CREDITS; k++) glue_frame(G);
	kp_t_eq(T, G->glue->screen, KP_GLUE_CREDITS, "Show Credits opens the credits");
	text = ui->credits.text;
	kp_t_ok(T, text != NULL, "the script");
	me = find_in(text, ui->credits.size, "n0madic");
	claude = find_in(text, ui->credits.size, "Anthropic Claude");
	original = find_in(text, ui->credits.size, "Blizzard Entertainment");
	kp_t_ok(T, me != NULL && claude != NULL, "the remake's makers");
	kp_t_ok(T, original != NULL && me < original && claude < original, "the original's creators after them");
	kp_t_ok(T, strstr(ui->credits.texts[CENTRE], "Koprulu Sector") != NULL, "the first page");
	open = ui->open_count;
	kp_t_ok(T, kp_ui_credits_game_open(ui) >= 0, "Show Credits again");
	kp_t_eq(T, ui->open_count, open, "the screen standing went first");
	kp_t_ok(T, strstr(ui->credits.texts[CENTRE], "Koprulu Sector") != NULL, "from its first page");
	glue_key(G, ' ');
	glue_frame(G);
	kp_t_ok(T, strstr(ui->credits.texts[CENTRE], "n0madic") != NULL, "Space turns to the makers' page");
	glue_key(G, ' ');
	glue_frame(G);
	kp_t_ok(T, strstr(ui->credits.texts[CENTRE], "StarCraft") != NULL, "and on");
	kp_hud_key(ui, 1, 0x1b, 0);
	kp_hud_key(ui, 0, 0x1b, 0);
	for (k = 0; k < 600 && G->glue->screen != KP_GLUE_MENUS; k++) glue_frame(G);
	kp_t_eq(T, G->glue->screen, KP_GLUE_MENUS, "Escape ends the credits");
	kp_t_ok(T, ui->credits.owned == NULL, "the script is released");
	wait_for(G, "rez\\glumain.bin", 2000);
}

/* The canvas after a repaint of the whole screen against the canvas as the frames left it: a dialog that closed
 * leaves nothing of itself (EndDialog 0x4c2b50 marks the dialog dirty, MarkControlDirty 0x4c2840). */
static void glue_ghosts(Glue *G, const char *when, int settle)
{
	enum { W = 800, H = 600 };
	uint8_t *before = (uint8_t *)malloc(2 * (size_t)W * (size_t)H);
	int32_t x, y, differ = 0, first_x = -1, first_y = -1;
	int k;
	kp_t_ok(G->T, before != NULL, "out of memory");
	kp_t_defer(G->T, free, before);
	for (k = 0; k < settle; k++) glue_frame(G);
	for (y = 0; y < H; y++) {
		for (x = 0; x < W; x++) kp_canvas_pixel(&G->glue->canvas, x, y, &before[2 * (y * W + x)], &before[2 * (y * W + x) + 1]);
	}
	kp_ui_add_dirty_rect(G->ui, 0, 0, KP_UI_SCREEN_W - 1, KP_UI_SCREEN_H - 1);
	glue_frame_now(G); /* the same time: no timer fires, the animations keep their frames */
	for (y = 0; y < H; y++) {
		for (x = 0; x < W; x++) {
			uint8_t index, alpha;
			kp_canvas_pixel(&G->glue->canvas, x, y, &index, &alpha);
			if (index == before[2 * (y * W + x)] && alpha == before[2 * (y * W + x) + 1]) continue;
			if (differ++ == 0) {
				first_x = x;
				first_y = y;
			}
		}
	}
	kp_t_ok(G->T, differ == 0, "%s: %d pixels differ from a full repaint, the first at (%d, %d)", when, differ, first_x, first_y);
}

static void glue_leaves_no_ghost(Glue *G, const char *when)
{
	/* the mouse over no control */
	kp_glue_input(G->glue, MOUSE_MOVE, G->glue->place_x + 2, G->glue->place_y + 2);
	glue_ghosts(G, when, 40);
}

/* A screen that closes over another (the game mode's over the main menu, the new ID's popup over the login) and a
 * popup that opens and closes leave the screen below as a full repaint draws it. */
static void glue_closing_a_screen_leaves_no_ghost(KpT *T)
{
	Glue *G;
	KpUi *ui;
	char name[128];
	int k;
	if (!kp_t_ready(T, "test_hud.glue_closing_a_screen_leaves_no_ghost", GLUE_FILES)) return;
	G = glue(T);
	ui = G->ui;
	wait_for(G, "rez\\glumain.bin", 2000);
	glue_leaves_no_ghost(G, "main menu");
	glue_click(G, 3, -1, -1); /* Single Player */
	wait_for(G, "rez\\glugamemode.bin", 2000);
	/* TODO: the game mode's open popup differs from a full repaint in 140 pixels of the logo's glow (a translucent
	 * image blended again over its own pixels by a partial repaint?); only the closes and the new ID's popup are checked */
	kp_hud_key(ui, 1, 0x1b, 0); /* Escape: Cancel */
	kp_hud_key(ui, 0, 0x1b, 0);
	wait_for(G, "rez\\glumain.bin", 2000);
	for (k = 0; k < 40; k++) glue_frame(G);
	glue_leaves_no_ghost(G, "main menu after the game mode's Cancel");
	glue_click(G, 3, -1, -1);
	wait_for(G, "rez\\glugamemode.bin", 2000);
	glue_key(G, 'e');
	for (k = 0; k < 2000 && strcmp(top(G, name, sizeof name), "rez\\glupedit.bin") != 0; k++) glue_frame(G);
	kp_glue_input(G->glue, MOUSE_MOVE, G->glue->place_x + 2, G->glue->place_y + 2);
	for (k = 0; k < 120; k++) { /* while it slides in */
		char when[64];
		glue_frame(G);
		snprintf(when, sizeof when, "the new ID's popup, frame %d", k);
		glue_ghosts(G, when, 0);
	}
	glue_leaves_no_ghost(G, "the new ID's popup");
	kp_hud_key(ui, 1, 0x1b, 0); /* Escape: Cancel */
	kp_hud_key(ui, 0, 0x1b, 0);
	for (k = 0; k < 40; k++) glue_frame(G);
	glue_leaves_no_ghost(G, "after the popup's Cancel");
}

/* Every theme sets its own arrow.grp as the cursor (InitGlueDialog 0x457f00, LoadGlueCursors 0x4584e0): its indices
 * are the theme's palette's, so the arrow is green on each screen's palette. The game's arrow (cursor\\arrow.grp) read
 * through a theme's palette is not: its indices are the tileset's. */
static void green_arrow(KpT *T, Glue *G, const char *screen)
{
	uint8_t pixels[(KP_CURSOR_SIZE * KP_CURSOR_SIZE)];
	int32_t w, h, dx, dy, k, opaque = 0;
	kp_t_ok(T, kp_ui_cursor_frame(G->ui, pixels, sizeof pixels, &w, &h, &dx, &dy) == 0, "%s: a cursor frame", screen);
	for (k = 0; k < w * h; k++) {
		const uint8_t *c = &G->ui->glue.palette[4 * pixels[k]];
		if (pixels[k] == 0) continue;
		opaque++;
		if (c[1] < c[0] || c[1] < c[2]) kp_t_fail(T, "%s: index %d is (%d, %d, %d), not green", screen, pixels[k], c[0], c[1], c[2]);
	}
	kp_t_ok(T, opaque > 100, "%s: the arrow's pixels", screen);
}

static void glue_cursor_keeps_its_colours_on_every_theme(KpT *T)
{
	Glue *G;
	KpUi *ui;
	int64_t main_key;
	uint8_t pixels[(KP_CURSOR_SIZE * KP_CURSOR_SIZE)];
	int32_t w, h, dx, dy;
	if (!kp_t_ready(T, "test_hud.glue_cursor_keeps_its_colours_on_every_theme", GLUE_FILES)) return;
	G = glue(T);
	ui = G->ui;
	wait_for(G, "rez\\glumain.bin", 2000);
	green_arrow(T, G, "main menu");
	main_key = kp_ui_cursor_key(ui);
	glue_click(G, 3, -1, -1); /* Single Player */
	wait_for(G, "rez\\glugamemode.bin", 2000);
	glue_key(G, 'e');
	wait_for(G, "rez\\glupedit.bin", 2000);
	kp_hud_text(ui, "Kop");
	glue_click(G, 1, -1, -1);
	wait_for(G, "rez\\glulogin.bin", 2000);
	green_arrow(T, G, "login");
	kp_t_ok(T, kp_ui_cursor_key(ui) >> 32 != main_key >> 32, "another theme's arrow is another cursor frame");
	glue_key(G, 'o');
	wait_for(G, "rez\\gluexpcmpgn.bin", 2000);
	green_arrow(T, G, "campaign menu");
	/* a new cursor id leaves the theme's arrow for the game's (SetCursorId 0x43e9a0); the same id keeps it */
	kp_ui_cursor_set(ui, 0);
	kp_t_ok(T, ui->cursor_theme != 0, "the same id keeps the theme's arrow");
	kp_ui_cursor_set(ui, 9);
	kp_t_eq(T, ui->cursor_theme, 0, "a new id drops it");
	kp_t_ok(T, kp_ui_cursor_frame(ui, pixels, sizeof pixels, &w, &h, &dx, &dy) == 0, "the drag cursor's frame");
}

/* The host's side of DeleteSaveFile 0x498b30 for the load screen's test: the calls, the last name, the answer. */
static int deletes, delete_result;
static char deleted[32];

static int glue_delete_save(void *host, const char *name)
{
	(void)host;
	deletes++;
	snprintf(deleted, sizeof deleted, "%s", name);
	return delete_result;
}

/* The glue's load screen (RunLoadGameScreen 0x459970) lists the player's saved games; Delete asks (gluAll 0x16) and
 * deletes the chosen one (DeleteSelectedSave 0x4599e0: the host's file, the list again: a file that stays is listed
 * again, with no error box), Ok ends the screen with the save to load. */
static void glue_load_screen_deletes_and_loads(KpT *T)
{
	enum { LIST = 6, OK = 4, DELETE = 7, LOAD_SCREEN = 13 };
	Glue *G;
	KpUi *ui;
	char name[128];
	int k, pass;
	if (!kp_t_ready(T, "test_hud.glue_load_screen_deletes_and_loads", GLUE_FILES)) return;
	G = glue(T);
	ui = G->ui;
	G->profile->save_count = 2;
	snprintf(G->profile->saves[0], sizeof G->profile->saves[0], "alpha");
	snprintf(G->profile->saves[1], sizeof G->profile->saves[1], "beta");
	kp_glue_saves(ui, G->profile);
	ui->delete_save = glue_delete_save;
	deletes = 0;
	wait_for(G, "rez\\glumain.bin", 2000);
	kp_glue_menus(G->glue, LOAD_SCREEN);
	wait_for(G, "rez\\gluload.bin", 2000);
	for (pass = 0; pass < 2; pass++) { /* a file that stays, then one deleted */
		delete_result = pass;
		glue_click(G, LIST, 8, 4); /* the first saved game */
		glue_click(G, DELETE, -1, -1);
		wait_for(G, "rez\\glupokcancel.bin", 2000);
		glue_click(G, 1, -1, -1); /* the popup's Ok */
		for (k = 0; k < 30; k++) glue_frame(G);
		kp_t_eq(T, deletes, pass + 1, "the host deletes the file");
		kp_t_eq_str(T, deleted, "alpha", NULL);
		kp_t_eq(T, ui->menus.save_count, 2 - pass, pass == 0 ? "a file that stays: listed again" : "deleted");
		kp_t_eq_str(T, top(G, name, sizeof name), "rez\\gluload.bin", "back on the load screen");
	}
	ui->delete_save = NULL;
	glue_click(G, LIST, 8, 4); /* now "beta" */
	glue_click(G, OK, -1, -1);
	for (k = 0; k < 600 && !(G->has_last && G->last.kind == KP_GLUE_LOAD); k++) glue_frame(G);
	kp_t_ok(T, G->has_last && G->last.kind == KP_GLUE_LOAD, "the screen ends with a save to load");
	kp_t_eq(T, G->last.save, 0, "the first of the list to load");
}

/* The vector text (ADR 0011, 2026-10-26): the fonts' bytes and modes, and glyphs laid on the original's pens and
 * baseline at the FNT font's cap height (its body rows): a word keeps even letter spacing inside its cells, with the
 * glyphs' drop shadows and the clip of a covered glyph. */
static void vector_text_lays_glyphs_on_the_original_pens(KpT *T)
{
	KpOracleUi *u;
	KpVText *v;
	KpUiCanvas c;
	KpUiCanvasGlyph g[4];
	float hhh[3 * KP_VTEXT_QUAD_FLOATS];
	const uint8_t *const *files;
	const size_t *sizes;
	const float *q;
	int top, cap, k, count;
	if (!kp_t_ready(T, "test_hud.vector_text_lays_glyphs_on_the_original_pens", UI_FILES)) return;
	u = kp_t_ui(T, 0);
	v = (KpVText *)calloc(1, sizeof *v);
	kp_t_ok(T, v != NULL, "out of memory");
	kp_t_defer(T, free, v);
	count = kp_ui_vector_fonts(u->ui, &files, &sizes);
	kp_t_eq(T, count, 2, "ui.kpd's VFNT: the text's font and font16x's wide one");
	kp_t_ok(T, kp_vtext_init(v, files, sizes, count) == 0 && v->ttf_count == 2, "TrueType fonts");
	kp_t_ok(T, v->codes['A'] && v->codes[0xe9] && !v->codes[0x7f] && !v->codes[0x81] && !v->codes[' '],
		"the fonts' Windows-1252 bytes");
	kp_t_ok(T, kp_vtext_active(KP_VIDEO_AUTO, 2.0) && !kp_vtext_active(KP_VIDEO_AUTO, 1.5)
		&& kp_vtext_active(KP_VTEXT_VECTOR, 1.0) && !kp_vtext_active(KP_VTEXT_ORIGINAL, 4.0), "the modes");
	kp_t_ok(T, kp_vtext_atlas(v, u->ui->font_files, u->ui->font_file_sizes, 3.0) == 0 && v->atlas_changed, "the atlas");
	kp_ui_font_cap(u->ui->font_files[1], u->ui->font_file_sizes[1], &top, &cap);
	kp_t_ok(T, top == 3 && cap == 7, "font10's H body: top %d height %d", top, cap);
	kp_ui_font_cap(u->ui->font_files[4], u->ui->font_file_sizes[4], &top, &cap);
	kp_t_ok(T, top == 4 && cap == 10, "font16x's H body: top %d height %d", top, cap);
	memset(&c, 0, sizeof c);
	memset(g, 0, sizeof g);
	for (k = 0; k < 3; k++) { /* "HHH": three cells that follow each other */
		g[k].x = 10 + 7 * k;
		g[k].y = 20;
		g[k].font = u->ui->font_files[1];
		g[k].code = 'H';
		g[k].advance = 7;
		g[k].fill = 100;
		g[k].whole = 1;
		g[k].box[0] = 6; /* the FNT header: width, height, x, y */
		g[k].box[1] = 8;
		g[k].box[3] = 3;
	}
	c.glyphs = g;
	c.glyph_count = 1;
	kp_t_eq(T, kp_vtext_quads(v, &c), 1, NULL);
	q = v->quads;
	kp_t_ok(T, q[0] >= 10.0f && q[2] <= 17.0f + 1e-4f, "inside its cell: %g..%g", (double)q[0], (double)q[2]);
	kp_t_ok(T, q[3] > 30.0f - 0.7f && q[3] < 30.0f + 0.7f, "on the baseline (20 + 3 + 7): bottom %g", (double)q[3]);
	kp_t_ok(T, q[3] - q[1] > 6.3f && q[3] - q[1] < 7.7f, "the cap height: %g", (double)(q[3] - q[1]));
	kp_t_ok(T, q[12] == 100.0f && q[8] < -1000.0f && q[10] > 1000.0f, "its colour and an open clip");
	c.glyph_count = 3;
	kp_t_eq(T, kp_vtext_quads(v, &c), 3, NULL);
	q = v->quads;
	kp_t_ok(T, fabsf((q[16] - q[0]) - (q[32] - q[16])) < 1e-3f, "even letter spacing: %g, %g", (double)(q[16] - q[0]),
		(double)(q[32] - q[16]));
	kp_t_ok(T, q[0] >= 10.0f - 1e-3f && q[34] <= 31.0f + 1e-3f
		&& fabsf((q[0] - 10.0f) - (31.0f - q[34])) < 1.5f, "the word centred in its cells: %g..%g", (double)q[0],
		(double)q[34]);
	/* the quads stay while the glyphs do */
	memcpy(hhh, v->quads, sizeof hhh);
	v->quads_changed = 0;
	kp_t_eq(T, kp_vtext_quads(v, &c), 3, NULL);
	kp_t_ok(T, !v->quads_changed, "the same glyphs: the quads kept");
	/* the edit box's caret: a glyph of no advance at the second H's pen leaves the word as it was, over its H */
	g[3] = g[1];
	g[3].code = '_';
	g[3].advance = 0;
	c.glyph_count = 4;
	kp_t_eq(T, kp_vtext_quads(v, &c), 4, NULL);
	q = v->quads;
	kp_t_ok(T, v->quads_changed && q[0] == hhh[0] && q[16] == hhh[16] && q[48] == hhh[32], "the H's where they were");
	kp_t_ok(T, fabsf((q[32] - v->glyphs[1]['_'].left / 3.0f) - (q[16] - v->glyphs[1]['H'].left / 3.0f)) < 1e-3f,
		"the caret at the second H's pen");
	g[3] = g[2]; /* past the text's end */
	g[3].x = g[2].x + 7;
	g[3].code = '_';
	g[3].advance = 0;
	kp_t_eq(T, kp_vtext_quads(v, &c), 4, NULL);
	q = v->quads;
	kp_t_ok(T, q[0] == hhh[0] && q[16] == hhh[16] && q[32] == hhh[32], "a caret after the word leaves it");
	c.glyph_count = 1;
	g[0].has_shadow = 1;
	g[0].shadow = 5;
	g[0].whole = 0;
	g[0].clip[0] = 12; /* covered left of x 12 */
	g[0].clip[1] = 23;
	g[0].clip[2] = 16;
	g[0].clip[3] = 31;
	kp_t_eq(T, kp_vtext_quads(v, &c), 2, "the shadow under the glyph");
	q = v->quads;
	kp_t_ok(T, q[12] == 5.0f && q[28] == 100.0f, "the shadow first");
	kp_t_ok(T, q[16] - q[0] < -0.49f && q[16] - q[0] > -0.51f, "half a logical pixel right and down at scale 3");
	kp_t_ok(T, q[24] == 12.0f && q[25] < -1000.0f && q[26] > 1000.0f && q[27] > 1000.0f,
		"cut only where its uncovered pixels end short of its bitmap");
	kp_vtext_free(v);
}

static const KpTest TESTS[] = {
	{ "a_load_ends_the_placement", a_load_ends_the_placement, 0 },
	{ "a_load_places_the_saved_view", a_load_places_the_saved_view, 0 },
	{ "a_load_redraws_the_minimap", a_load_redraws_the_minimap, 0 },
	{ "a_load_restores_the_pending_releases", a_load_restores_the_pending_releases, 0 },
	{ "a_load_starts_the_resource_counts", a_load_starts_the_resource_counts, 0 },
	{ "vector_text_lays_glyphs_on_the_original_pens", vector_text_lays_glyphs_on_the_original_pens, 0 },
	{ "camera_pan_arrives_in_a_wide_view", camera_pan_arrives_in_a_wide_view, 0 },
	{ "canvas_blends_a_menu_over_the_text_lines", canvas_blends_a_menu_over_the_text_lines, 0 },
	{ "canvas_composes_the_classic_screen", canvas_composes_the_classic_screen, 0 },
	{ "hd_fillers_take_the_pieces_reach", hd_fillers_take_the_pieces_reach, 0 },
	{ "canvas_draws_the_drag_box_and_the_command_marker", canvas_draws_the_drag_box_and_the_command_marker, 0 },
	{ "canvas_draws_the_placement_preview", canvas_draws_the_placement_preview, 0 },
	{ "canvas_draws_the_text_lines_with_the_game_view", canvas_draws_the_text_lines_with_the_game_view, 0 },
	{ "canvas_keeps_a_menu_under_the_console_art", canvas_keeps_a_menu_under_the_console_art, 0 },
	{ "click_on_a_producer_flashes_its_rally_point", click_on_a_producer_flashes_its_rally_point, 0 },
	{ "flag_portrait_takes_its_owners_colour", flag_portrait_takes_its_owners_colour, 0 },
	{ "glue_load_screen_deletes_and_loads", glue_load_screen_deletes_and_loads, 0 },
	{ "glue_cursor_keeps_its_colours_on_every_theme", glue_cursor_keeps_its_colours_on_every_theme, 0 },
	{ "glue_closing_a_screen_leaves_no_ghost", glue_closing_a_screen_leaves_no_ghost, 0 },
	{ "glue_menus_start_a_campaign_mission", glue_menus_start_a_campaign_mission, 0 },
	{ "glue_room_cancel_by_back_leaves_no_game_input", glue_room_cancel_by_back_leaves_no_game_input, 0 },
	{ "destroyed_dialog_leaves_no_image", destroyed_dialog_leaves_no_image, 0 },
	{ "glue_hands_the_cinematics_to_the_host", glue_hands_the_cinematics_to_the_host, 0 },
	{ "main_menu_buttons_the_remake_lacks_are_inert", main_menu_buttons_the_remake_lacks_are_inert, 0 },
	{ "show_credits_begins_with_the_remake", show_credits_begins_with_the_remake, 0 },
	{ "held_keys_reach_the_window_at_their_repeats", held_keys_reach_the_window_at_their_repeats, 0 },
	{ "inactive_window_stops_the_scroll", inactive_window_stops_the_scroll, 0 },
	{ "key_characters_follow_translated_keys", key_characters_follow_translated_keys, 0 },
	{ "countdown_is_drawn_in_the_resource_panel", countdown_is_drawn_in_the_resource_panel, 0 },
	{ "leaderboard_key_folds_its_rows", leaderboard_key_folds_its_rows, 0 },
	{ "mission_warp_goes_to_the_ready_room", mission_warp_goes_to_the_ready_room, 0 },
	{ "music_stops_with_its_option_and_the_game", music_stops_with_its_option_and_the_game, 0 },
	{ "paused_minimap_takes_no_press", paused_minimap_takes_no_press, 0 },
	{ "pending_releases_reach_the_save", pending_releases_reach_the_save, 0 },
	{ "portrait_click_centres_its_unit", portrait_click_centres_its_unit, 0 },
	{ "portrait_hotkey_centres_the_last_transmission", portrait_hotkey_centres_the_last_transmission, 0 },
	{ "portrait_mode_shows_the_portrait_again", portrait_mode_shows_the_portrait_again, 0 },
	{ "right_click_on_the_card_ends_the_mode", right_click_on_the_card_ends_the_mode, 0 },
	{ "status_tooltip_follows_the_panels_layout", status_tooltip_follows_the_panels_layout, 0 },
	{ "text_reaches_the_window_as_bytes", text_reaches_the_window_as_bytes, 0 },
	{ "type_selection_takes_the_units_cloaked_for_free", type_selection_takes_the_units_cloaked_for_free, 0 },
	{ "double_click_selects_the_kind_on_the_screen", double_click_selects_the_kind_on_the_screen, 0 },
	{ "touch_taps_select_and_order", touch_taps_select_and_order, 0 },
	{ "touch_pad_assigns_and_recalls_groups", touch_pad_assigns_and_recalls_groups, 0 },
	{ "touch_pad_reassigns_a_held_group", touch_pad_reassigns_a_held_group, 0 },
	{ "touch_pad_drops_a_dead_group", touch_pad_drops_a_dead_group, 0 },
	{ "touch_pad_follows_the_blend_colour", touch_pad_follows_the_blend_colour, 0 },
	{ "touch_pad_blend_colour_outside_the_cycles", touch_pad_blend_colour_outside_the_cycles, 0 },
	{ "touch_pad_shift_lasts_one_click", touch_pad_shift_lasts_one_click, 0 },
	{ "touch_pad_second_tap_centres_the_group", touch_pad_second_tap_centres_the_group, 0 },
	{ "touch_ping_centres_its_event", touch_ping_centres_its_event, 0 },
	{ "touch_pad_stays_out_of_the_way", touch_pad_stays_out_of_the_way, 0 },
	{ "touch_minimap_moves_the_view", touch_minimap_moves_the_view, 0 },
	{ "touch_portrait_centres_the_view", touch_portrait_centres_the_view, 0 },
	{ "back_opens_and_closes_the_game_menu", back_opens_and_closes_the_game_menu, 0 },
	{ "a_pan_ends_without_a_pointer", a_pan_ends_without_a_pointer, 0 },
	{ "touch_placement_knows_its_ghost", touch_placement_knows_its_ghost, 0 },
	{ "addon_preview_starts_beside_its_parent", addon_preview_starts_beside_its_parent, 0 },
	{ "addon_anchor_is_the_layouts_alone", addon_anchor_is_the_layouts_alone, 0 },
	{ "a_new_screen_keeps_the_view_centre", a_new_screen_keeps_the_view_centre, 0 },
	{ "wide_game_view_clicks_reach_the_game", wide_game_view_clicks_reach_the_game, 0 },
	{ "wide_gestures_keep_their_panel", wide_gestures_keep_their_panel, 0 },
};
KP_SUITE(kp_suite_hud, "test_hud", TESTS);
