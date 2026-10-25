/*
 * kp_core.ui: the interface layer kp_ui (kp_ui.h) for Lua.
 *   kp_core.ui.timers()  a UI timer queue (timer.cpp): q:set(owner, id, period, callback, now) -> ok,
 *                        q:delete(owner, id), q:delete_owner(owner), q:run(now, fn) -> fired (fn(owner, id, callback,
 *                        period) per fired timer, in order), q:order() -> { {owner=, id=, last=, period=}, ... }
 *   kp_core.ui.surface(w, h, fill)  an 8-bit surface: s:bytes() -> its w * h indices, s:fill(index), s:crc32(), s.w, s.h
 *   kp_core.ui.text()    the text engine (kp_ui_text.h): t:select_font(fnt), t:set_rows(bank), t:set{field = value},
 *                        t:get(field), t:char_width(code), t:glyph(code), t:select_color(code),
 *                        t:find_line_break(text) -> consumed, t:span(text) -> consumed, t:layout(text) -> consumed,
 *                        lines, t:align(y) -> y, t:draw(surface, x, y, text) -> rest offset or nil, t:width(text),
 *                        t:fit(text, width), t:plot(surface, x, y, packets, w, h) -> packet bytes consumed
 *                        (fields: x, y, flags, tab, gap, spacing, measuring, wrap, highlight, row, previous_row,
 *                        clip_left/top/right/bottom, ext_left/top/right/bottom, max_width, max_height)
 *   kp_core.ui.new(ui_kpd, fonts_kpd)  the interface (KpUi): u:font_set(game), u:theme(grp path), u:console(race),
 *                        u:menu_button() -> dialog, u:load(bin) -> dialog, u:init(dialog), u:paint(dialog),
 *                        u:present() -> draws (the invalid region's repaint, PresentFrame's dialog layer),
 *                        u:region() -> { {left, top, right, bottom}, ... } (the invalid region's rectangles, right and
 *                        bottom exclusive), u:game_event(kind, a, b, c[, text]) (a game event for the interface),
 *                        u:options([word]) -> word (the game options word 0x6d3148), u:take_release() -> bool
 *                        (a trigger portrait's time ran out: release the game's trigger wait), u:pending_releases() ->
 *                        portrait_ms or nil, camera (the pending ends of the trigger waits a save keeps),
 *                        u:game_loaded() (a load replaced the game: forget them, end the cursor modes),
 *                        u:cursor(id[, counter]),
 *                        u:cursor_animate(), u:cursor_bitmap() -> bytes, w, h, dx, dy (the cursor's frame), u:cursor_key() -> key
 *                        (the frame's id and index, nil hidden), u:game_view_active() -> flag (the canvas's game view
 *                        draws the drag box, the command marker or the placement's preview), u:game_view_update() (the
 *                        game loop's UpdatePlacementCursor 0x47d7e0 at the last event's mouse position, and the drag
 *                        box's end with no button down), u:capture_changed() (the window lost the mouse: the focus went),
 *                        u:active(flag) (the window's activity: no scroll while inactive, the key bytes cleared),
 *                        u:from_save() (the next console's game is a save's: no tips at its start),
 *                        u:player_colours(rows) (tunit.pcx's 96 bytes: twelve rows of eight colours for indices 8..15),
 *                        u:input(type, a, b), u:keys(shift, ctrl, alt, mouse_state), u:screen(x, y), u:cheats(chea),
 *                        u:take_commands() -> { bytes, ... } (the input and the commands it queued), u:pick(x, y),
 *                        u:over_console(flag), u:view(w, h), u:take_camera() -> x, y, u:scroll(cx, cy) -> moved,
 *                        u:edge_scroll(cx, cy) -> id, u:scroll_state() -> {...}, u:tooltip() -> bytes, x, y, w, h, u:scroll_speeds(mouse, key), u:take_warp_cursor() -> flag,
 *                        u:lines() -> bytes, w, h (the text lines' layer, the expired lines dropped),
 *                        u:lines_expire() -> changed (the expired lines dropped; the layer to draw again),
 *                        u:bitmap(dialog) -> bytes, w, h, u:crc32(dialog), u:control(i) -> {left=, top=, right=, bottom=, flags=, id=,
 *                        type=, wuser=, bin=, record=, load=, child=, next=, active=, bin_name= (made from a BIN), text= (when set)}, u:open() ->
 *                        { dialog, ... } (topmost
 *                        first), u:set_colours(bytes) (the 26 UI colour indexes), u:now(tick),
 *                        u:strings(strings_kpd), u:portraits(portraits_kpd), u:sounds(sounds_kpd),
 *                        u:resolve_colours(rgb, cycled), u:talk(unit, duration, sound[, ping, colour]),
 *                        u:minimap(mmni, rgb, cycled, dark, colour_slots) (before u:hud),
 *                        u:hud(game) (the console over a kp_core.game), u:hud_frame() (after each game frame),
 *                        u:tick(now) -> fired (the UI timers due at tick now), u:hud_state() -> { resources =,
 *                        menu_button =, status_unit =, selection_count =, shown_minerals =, shown_gas =, mirror = },
 *                        u:game_effects(rgb, cycled) (the in-game dialogs' blend tables), u:blend_row() -> 256 bytes,
 *                        colour (the canvas's translucent texels over each world index, the colour that blends), u:game_exit() -> code (a result
 *                        dialog's answer: 6 victory, 7 defeat, else 0), u:modeless() -> dialog or -1,
 *                        u:show_result(result) -> dialog (2 defeat, 3 victory, 5 draw; a game event opens it too),
 *                        u:end_game() (EndGame's part: the modeless dialog closed, the streams stopped),
 *                        u:score(data) -> dialog, u:score_game(game, result, name, elapsed) -> dialog (the score
 *                        screen), u:score_state() -> { dialog =, answered =, result =, glue_state =, tab =, elapsed = },
 *                        u:menus_enter(state) -> dialog, u:menus_poll() -> nil or { state =, action =, ... } (the
 *                        glue's loop), u:profiles([list]) -> list, changed, u:campaign(tabl), u:saves(names),
 *                        u:invalidate_all()
 *   kp_core.ui.smk(data) a Smacker video (kp_smk.h): v:decode() -> ok (the next frame), v:pixels() -> w * h indices,
 *                        v.w, v.h, v.frames, v.frame (the next one), v.frame_time (10 microsecond units)
 *   kp_core.ui.canvas()  the screen's canvas (KpUiCanvas): c:resize(w, h), c:begin(), c:console(u, x, y, x0, x1),
 *                        c:filler(u, x, y, w, column), c:dialog(u, dialog, x, y), c:lines(u, x, y),
 *                        c:game_view(u, x, y) (the drag box and the command marker of the view at (x, y)), c:finish() -> changed, x0, y0, x1, y1,
 *                        c:rect(x0, y0, x1, y1) -> the rectangle's RGBA texels (index, 0, 0, opacity), row by row,
 *                        c:pixel(x, y) -> index, opacity, c.w, c.h, c.blends (the frame's translucent texels over the
 *                        world, opacity KP_UI_CANVAS_BLEND_A: u:blend_row() maps them)
 */
#include "kp_core_lua.h"

#include <stdlib.h>
#include <string.h>

#include "../core/kp_kpf.h"
#include "../core/kp_ui.h"
#include "../core/kp_ui_text.h"
#include "../core/kp_smk.h"

#define UI_TIMERS "kp_core.ui.timers"
#define UI_SURFACE "kp_core.ui.surface"
#define UI_TEXT "kp_core.ui.text"
#define UI_UI "kp_core.ui.ui"
#define UI_CANVAS "kp_core.ui.canvas"
#define UI_SMK "kp_core.ui.smk"

static KpUiTimers *check_timers(lua_State *L) { return (KpUiTimers *)luaL_checkudata(L, 1, UI_TIMERS); }

static uint32_t arg_tick(lua_State *L, int i)
{
	lua_Number v = luaL_checknumber(L, i);
	return (uint32_t)(int64_t)v;
}

static int timers_new(lua_State *L)
{
	KpUiTimers *t = (KpUiTimers *)lua_newuserdata(L, sizeof(KpUiTimers));
	kp_ui_timers_init(t);
	luaL_getmetatable(L, UI_TIMERS);
	lua_setmetatable(L, -2);
	return 1;
}

static int timers_set(lua_State *L)
{
	KpUiTimers *t = check_timers(L);
	int ok = kp_ui_set_timer(t, (int64_t)luaL_checknumber(L, 2), (uint16_t)luaL_checknumber(L, 3), arg_tick(L, 4),
		lua_toboolean(L, 5), arg_tick(L, 6));
	lua_pushboolean(L, ok == 0);
	return 1;
}

static int timers_delete(lua_State *L)
{
	kp_ui_delete_timer(check_timers(L), (int64_t)luaL_checknumber(L, 2), (uint16_t)luaL_checknumber(L, 3));
	return 0;
}

static int timers_delete_owner(lua_State *L)
{
	kp_ui_delete_owner_timers(check_timers(L), (int64_t)luaL_checknumber(L, 2));
	return 0;
}

/* The Lua function at stack index 3 does each fired timer's work. */
static void run_fn(void *ctx, int64_t owner, uint16_t id, int callback, uint32_t period)
{
	lua_State *L = (lua_State *)ctx;
	lua_pushvalue(L, 3);
	lua_pushnumber(L, (lua_Number)owner);
	lua_pushnumber(L, (lua_Number)id);
	lua_pushboolean(L, callback);
	lua_pushnumber(L, (lua_Number)period);
	lua_call(L, 4, 0);
}

static int timers_run(lua_State *L)
{
	KpUiTimers *t = check_timers(L);
	uint32_t now = arg_tick(L, 2);
	luaL_checktype(L, 3, LUA_TFUNCTION);
	lua_pushnumber(L, (lua_Number)kp_ui_run_timers(t, now, run_fn, L));
	return 1;
}

static int timers_order(lua_State *L)
{
	KpUiTimers *t = check_timers(L);
	int32_t order[KP_UI_TIMERS];
	int n = kp_ui_timer_order(t, order, KP_UI_TIMERS), i;
	lua_createtable(L, n, 0);
	for (i = 0; i < n; i++) {
		const KpUiTimer *node = &t->nodes[order[i]];
		lua_createtable(L, 0, 4);
		lua_pushnumber(L, (lua_Number)node->owner);
		lua_setfield(L, -2, "owner");
		lua_pushnumber(L, (lua_Number)node->id);
		lua_setfield(L, -2, "id");
		lua_pushnumber(L, (lua_Number)node->last);
		lua_setfield(L, -2, "last");
		lua_pushnumber(L, (lua_Number)node->period);
		lua_setfield(L, -2, "period");
		lua_rawseti(L, -2, i + 1);
	}
	return 1;
}

static const luaL_Reg TIMERS_METHODS[] = {
	{ "set", timers_set }, { "delete", timers_delete }, { "delete_owner", timers_delete_owner },
	{ "run", timers_run }, { "order", timers_order }, { NULL, NULL },
};

/* --- surfaces: the userdata holds the KpUiSurface and then its pixels --- */

static KpUiSurface *check_surface(lua_State *L, int i) { return (KpUiSurface *)luaL_checkudata(L, i, UI_SURFACE); }

static int surface_new(lua_State *L)
{
	int32_t w = (int32_t)luaL_checknumber(L, 1), h = (int32_t)luaL_checknumber(L, 2);
	KpUiSurface *s;
	if (w < 1 || h < 1 || w > 4096 || h > 4096) return luaL_error(L, "surface: %dx%d", (int)w, (int)h);
	s = (KpUiSurface *)lua_newuserdata(L, sizeof(KpUiSurface) + (size_t)w * (size_t)h);
	s->w = w;
	s->h = h;
	s->pixels = (uint8_t *)(s + 1);
	memset(s->pixels, (int)luaL_optnumber(L, 3, 0), (size_t)w * (size_t)h);
	luaL_getmetatable(L, UI_SURFACE);
	lua_setmetatable(L, -2);
	return 1;
}

static int surface_bytes(lua_State *L)
{
	KpUiSurface *s = check_surface(L, 1);
	lua_pushlstring(L, (const char *)s->pixels, (size_t)s->w * (size_t)s->h);
	return 1;
}

static int surface_fill(lua_State *L)
{
	KpUiSurface *s = check_surface(L, 1);
	memset(s->pixels, (int)luaL_checknumber(L, 2), (size_t)s->w * (size_t)s->h);
	return 0;
}

static int surface_crc32(lua_State *L)
{
	KpUiSurface *s = check_surface(L, 1);
	lua_pushnumber(L, (lua_Number)kp_crc32(s->pixels, (size_t)s->w * (size_t)s->h));
	return 1;
}

static int surface_index(lua_State *L)
{
	KpUiSurface *s = check_surface(L, 1);
	const char *key = luaL_checkstring(L, 2);
	if (strcmp(key, "w") == 0) lua_pushnumber(L, s->w);
	else if (strcmp(key, "h") == 0) lua_pushnumber(L, s->h);
	else lua_getfield(L, lua_upvalueindex(1), key);
	return 1;
}

static const luaL_Reg SURFACE_METHODS[] = {
	{ "bytes", surface_bytes }, { "fill", surface_fill }, { "crc32", surface_crc32 }, { NULL, NULL },
};

/* --- the text engine: the userdata's environment table keeps the font string alive --- */

static KpUiText *check_text(lua_State *L) { return (KpUiText *)luaL_checkudata(L, 1, UI_TEXT); }

static const uint8_t *arg_text(lua_State *L, int i)
{
	return (const uint8_t *)luaL_checkstring(L, i); /* Lua strings end with a NUL */
}

static int text_new(lua_State *L)
{
	KpUiText *t = (KpUiText *)lua_newuserdata(L, sizeof(KpUiText));
	kp_ui_text_init(t);
	luaL_getmetatable(L, UI_TEXT);
	lua_setmetatable(L, -2);
	lua_newtable(L);
	lua_setfenv(L, -2);
	return 1;
}

static int text_select_font(lua_State *L)
{
	KpUiText *t = check_text(L);
	size_t n;
	const char *font = luaL_checklstring(L, 2, &n);
	if (kp_ui_text_select_font(t, (const uint8_t *)font, n) != 0) return luaL_error(L, "text: not an FNT image");
	lua_getfenv(L, 1);
	lua_pushvalue(L, 2);
	lua_setfield(L, -2, "font");
	return 0;
}

static int text_set_rows(lua_State *L)
{
	size_t n;
	const char *bank = luaL_checklstring(L, 2, &n);
	kp_ui_text_set_rows(check_text(L), (const uint8_t *)bank, n);
	return 0;
}

#define TEXT_FIELDS(X) X(x) X(y) X(flags) X(tab) X(gap) X(spacing) X(measuring) X(wrap) X(highlight) X(row) \
	X(previous_row) X(clip_left) X(clip_top) X(clip_right) X(clip_bottom) X(ext_left) X(ext_top) X(ext_right) \
	X(ext_bottom) X(max_width) X(max_height)

static int text_set(lua_State *L)
{
	KpUiText *t = check_text(L);
	luaL_checktype(L, 2, LUA_TTABLE);
#define SET(f) lua_getfield(L, 2, #f); if (!lua_isnil(L, -1)) t->f = (TYPEOF_##f)lua_tonumber(L, -1); lua_pop(L, 1);
#define TYPEOF_x int32_t
#define TYPEOF_y int32_t
#define TYPEOF_flags uint8_t
#define TYPEOF_tab uint8_t
#define TYPEOF_gap uint8_t
#define TYPEOF_spacing uint8_t
#define TYPEOF_measuring int
#define TYPEOF_wrap int
#define TYPEOF_highlight uint8_t
#define TYPEOF_row int
#define TYPEOF_previous_row int
#define TYPEOF_clip_left int16_t
#define TYPEOF_clip_top int16_t
#define TYPEOF_clip_right int16_t
#define TYPEOF_clip_bottom int16_t
#define TYPEOF_ext_left int16_t
#define TYPEOF_ext_top int16_t
#define TYPEOF_ext_right int16_t
#define TYPEOF_ext_bottom int16_t
#define TYPEOF_max_width uint8_t
#define TYPEOF_max_height uint8_t
	TEXT_FIELDS(SET)
#undef SET
	return 0;
}

static int text_get(lua_State *L)
{
	KpUiText *t = check_text(L);
	const char *key = luaL_checkstring(L, 2);
#define GET(f) if (strcmp(key, #f) == 0) { lua_pushnumber(L, (lua_Number)t->f); return 1; }
	TEXT_FIELDS(GET)
#undef GET
	return luaL_error(L, "text: no field %s", key);
}

static int text_char_width(lua_State *L)
{
	lua_pushnumber(L, (lua_Number)kp_ui_char_width(check_text(L), (int)luaL_checknumber(L, 2)));
	return 1;
}

static int text_glyph(lua_State *L)
{
	lua_pushnumber(L, (lua_Number)kp_ui_glyph(check_text(L), (int)luaL_checknumber(L, 2)));
	return 1;
}

static int text_select_color(lua_State *L)
{
	kp_ui_select_color(check_text(L), (int)luaL_checknumber(L, 2));
	return 0;
}

static int text_find_line_break(lua_State *L)
{
	lua_pushnumber(L, (lua_Number)kp_ui_find_line_break(check_text(L), arg_text(L, 2), 0));
	return 1;
}

static int text_span(lua_State *L)
{
	size_t n;
	const uint8_t *s = (const uint8_t *)luaL_checklstring(L, 2, &n);
	lua_pushnumber(L, (lua_Number)kp_ui_text_span(check_text(L), s, 0, n));
	return 1;
}

static int text_layout(lua_State *L)
{
	int32_t count = 0;
	size_t stop = kp_ui_layout_lines(check_text(L), arg_text(L, 2), 0, &count);
	lua_pushnumber(L, (lua_Number)stop);
	lua_pushnumber(L, (lua_Number)count);
	return 2;
}

static int text_align(lua_State *L)
{
	int32_t y = (int32_t)luaL_checknumber(L, 2);
	kp_ui_align_vertically(check_text(L), &y);
	lua_pushnumber(L, (lua_Number)y);
	return 1;
}

static int text_draw(lua_State *L)
{
	KpUiText *t = check_text(L);
	size_t rest;
	t->target = check_surface(L, 2);
	rest = kp_ui_draw_text(t, (int32_t)luaL_checknumber(L, 3), (int32_t)luaL_checknumber(L, 4), arg_text(L, 5));
	t->target = NULL;
	if (rest == SIZE_MAX) lua_pushnil(L);
	else lua_pushnumber(L, (lua_Number)rest);
	return 1;
}

static int text_width(lua_State *L)
{
	lua_pushnumber(L, (lua_Number)kp_ui_text_width(check_text(L), arg_text(L, 2)));
	return 1;
}

static int text_fit(lua_State *L)
{
	lua_pushnumber(L, (lua_Number)kp_ui_text_fit_length(check_text(L), arg_text(L, 2),
		(int32_t)luaL_checknumber(L, 3)));
	return 1;
}

static int text_plot(lua_State *L)
{
	KpUiText *t = check_text(L);
	size_t n;
	const char *packets = luaL_checklstring(L, 5, &n);
	lua_pushnumber(L, (lua_Number)kp_ui_plot_glyph(t, check_surface(L, 2), (int32_t)luaL_checknumber(L, 3),
		(int32_t)luaL_checknumber(L, 4), (const uint8_t *)packets, n, (int)luaL_checknumber(L, 6),
		(int)luaL_checknumber(L, 7)));
	return 1;
}

static const luaL_Reg TEXT_METHODS[] = {
	{ "select_font", text_select_font }, { "set_rows", text_set_rows }, { "set", text_set }, { "get", text_get },
	{ "char_width", text_char_width }, { "glyph", text_glyph }, { "select_color", text_select_color },
	{ "find_line_break", text_find_line_break }, { "span", text_span }, { "layout", text_layout },
	{ "align", text_align }, { "draw", text_draw }, { "width", text_width }, { "fit", text_fit },
	{ "plot", text_plot }, { NULL, NULL },
};

/* --- the interface: the userdata holds a KpUi pointer; its environment keeps the containers' strings alive --- */

static KpUi *check_ui(lua_State *L)
{
	KpUi **p = (KpUi **)luaL_checkudata(L, 1, UI_UI);
	if (*p == NULL) luaL_error(L, "ui: freed");
	return *p;
}

static int32_t check_dialog(lua_State *L, KpUi *ui, int i)
{
	int32_t d = (int32_t)luaL_checknumber(L, i);
	if (d < 0 || d >= KP_UI_CONTROLS || !ui->controls[d].used) luaL_error(L, "ui: no control %d", (int)d);
	return d;
}

static int ui_new(lua_State *L)
{
	size_t un, fn;
	const char *u = luaL_checklstring(L, 1, &un), *f = luaL_checklstring(L, 2, &fn);
	char err[256];
	KpUi **p = (KpUi **)lua_newuserdata(L, sizeof(KpUi *));
	*p = NULL;
	luaL_getmetatable(L, UI_UI);
	lua_setmetatable(L, -2);
	lua_newtable(L);
	lua_pushvalue(L, 1);
	lua_setfield(L, -2, "ui");
	lua_pushvalue(L, 2);
	lua_setfield(L, -2, "fonts");
	lua_setfenv(L, -2);
	*p = kp_ui_new((const uint8_t *)u, un, (const uint8_t *)f, fn, err, sizeof err);
	if (*p == NULL) return luaL_error(L, "%s", err);
	return 1;
}

static int ui_gc(lua_State *L)
{
	KpUi **p = (KpUi **)luaL_checkudata(L, 1, UI_UI);
	kp_ui_free(*p);
	*p = NULL;
	return 0;
}

static int ui_font_set(lua_State *L)
{
	kp_ui_font_set(check_ui(L), lua_toboolean(L, 2));
	return 0;
}

static int ui_theme(lua_State *L)
{
	if (kp_ui_apply_theme(check_ui(L), luaL_checkstring(L, 2)) != 0) return luaL_error(L, "ui: no GRP %s", lua_tostring(L, 2));
	return 0;
}

static int ui_console(lua_State *L)
{
	if (kp_ui_load_console(check_ui(L), (int)luaL_checknumber(L, 2)) != 0) return luaL_error(L, "ui: no console art");
	return 0;
}

/* u:console_art() -> bytes, w, h: the console's art (index 0 where the game view shows), or nil. */
static int ui_console_art(lua_State *L)
{
	KpUi *ui = check_ui(L);
	if (ui->console.pixels == NULL) return 0;
	lua_pushlstring(L, (const char *)ui->console.pixels, (size_t)ui->console.w * (size_t)ui->console.h);
	lua_pushnumber(L, ui->console.w);
	lua_pushnumber(L, ui->console.h);
	return 3;
}

static int ui_menu_button(lua_State *L)
{
	KpUi *ui = check_ui(L);
	int32_t d = kp_ui_create_menu_button(ui);
	if (d < 0) return luaL_error(L, "%s", ui->error);
	lua_pushnumber(L, d);
	return 1;
}

static int ui_load(lua_State *L)
{
	KpUi *ui = check_ui(L);
	int32_t d = kp_ui_load_dialog(ui, luaL_checkstring(L, 2));
	if (d < 0) return luaL_error(L, "%s", ui->error);
	lua_pushnumber(L, d);
	return 1;
}

static int ui_init(lua_State *L)
{
	KpUi *ui = check_ui(L);
	kp_ui_init_dialog(ui, check_dialog(L, ui, 2), NULL);
	return 0;
}

static int ui_paint(lua_State *L)
{
	KpUi *ui = check_ui(L);
	kp_ui_paint_dialog(ui, check_dialog(L, ui, 2));
	return 0;
}

static int ui_present(lua_State *L)
{
	lua_pushnumber(L, kp_ui_present(check_ui(L)));
	return 1;
}

static int ui_region(lua_State *L)
{
	int16_t rects[KP_UI_REGION][4];
	int n = kp_ui_region(check_ui(L), rects), i, k;
	lua_createtable(L, n, 0);
	for (i = 0; i < n; i++) {
		lua_createtable(L, 4, 0);
		for (k = 0; k < 4; k++) {
			lua_pushnumber(L, rects[i][k] + (k >= 2));
			lua_rawseti(L, -2, k + 1);
		}
		lua_rawseti(L, -2, i + 1);
	}
	return 1;
}

/* u:game_event(kind, a, b, c[, text]): a game event for the interface (kp_ui_game_event). */
static int ui_game_event(lua_State *L)
{
	kp_ui_game_event(check_ui(L), (int64_t)luaL_checknumber(L, 2), (int64_t)luaL_checknumber(L, 3),
		(int64_t)luaL_checknumber(L, 4), (int64_t)luaL_checknumber(L, 5), luaL_optstring(L, 6, NULL));
	return 0;
}

/* u:lines() -> the text lines' 640 x 480 layer (index 0 transparent), w, h, after the expired lines are dropped
 * (ExpireTextLines 0x46afb0 at the interface's tick); nil without a console. */
static int ui_lines(lua_State *L)
{
	KpUi *ui = check_ui(L);
	const KpUiSurface *s;
	kp_ui_lines_expire(ui);
	s = kp_ui_lines_layer(ui);
	if (s == NULL) return 0;
	lua_pushlstring(L, (const char *)s->pixels, (size_t)s->w * (size_t)s->h);
	lua_pushnumber(L, s->w);
	lua_pushnumber(L, s->h);
	return 3;
}

/* u:lines_expire() -> changed: the expired text lines dropped (ExpireTextLines 0x46afb0), whether the layer must be
 * drawn again (a line came or went since it was last drawn). */
static int ui_lines_expire(lua_State *L)
{
	KpUi *ui = check_ui(L);
	kp_ui_lines_expire(ui);
	lua_pushboolean(L, ui->lines.changed);
	return 1;
}

static int ui_bitmap(lua_State *L)
{
	KpUi *ui = check_ui(L);
	KpUiControl *d = &ui->controls[check_dialog(L, ui, 2)];
	if (d->bitmap.pixels == NULL) return 0;
	lua_pushlstring(L, (const char *)d->bitmap.pixels, (size_t)d->bitmap.w * (size_t)d->bitmap.h);
	lua_pushnumber(L, d->bitmap.w);
	lua_pushnumber(L, d->bitmap.h);
	return 3;
}

static int ui_crc32(lua_State *L)
{
	KpUi *ui = check_ui(L);
	KpUiControl *d = &ui->controls[check_dialog(L, ui, 2)];
	if (d->bitmap.pixels == NULL) return 0;
	lua_pushnumber(L, (lua_Number)kp_crc32(d->bitmap.pixels, (size_t)d->bitmap.w * (size_t)d->bitmap.h));
	return 1;
}

static int ui_control(lua_State *L)
{
	KpUi *ui = check_ui(L);
	KpUiControl *c = &ui->controls[check_dialog(L, ui, 2)];
	lua_createtable(L, 0, 16);
#define F(name, v) lua_pushnumber(L, (lua_Number)(v)); lua_setfield(L, -2, name);
	F("left", c->left) F("top", c->top) F("right", c->right) F("bottom", c->bottom) F("flags", c->flags)
	F("id", c->id) F("type", c->type) F("wuser", c->wuser) F("bin", c->bin) F("record", c->record) F("load", c->load)
	F("child", c->child) F("next", c->next) F("active", c->active)
#undef F
	if (c->bin >= 0 && c->bin < ui->bin_count) { /* a list's scroll bar is made in code, without a BIN */
		lua_pushlstring(L, ui->bins[c->bin].name, ui->bins[c->bin].name_len);
		lua_setfield(L, -2, "bin_name");
	}
	if (c->text != NULL) {
		lua_pushstring(L, (const char *)c->text);
		lua_setfield(L, -2, "text");
	}
	return 1;
}

/* u:set_visible(control, show): ShowControl / HideControl (kp_ui_set_visible; a hidden focus passes the focus on). */
static int ui_set_visible(lua_State *L)
{
	KpUi *ui = check_ui(L);
	kp_ui_set_visible(ui, check_dialog(L, ui, 2), lua_toboolean(L, 3));
	return 0;
}

static int ui_open(lua_State *L)
{
	KpUi *ui = check_ui(L);
	int32_t d;
	int k = 0;
	lua_newtable(L);
	for (d = ui->open; d >= 0 && k < KP_UI_CONTROLS; d = ui->controls[d].next) {
		lua_pushnumber(L, d);
		lua_rawseti(L, -2, ++k);
	}
	return 1;
}

static int ui_set_colours(lua_State *L)
{
	KpUi *ui = check_ui(L);
	size_t n;
	const char *c = luaL_checklstring(L, 2, &n);
	memcpy(ui->ui_colours, c, n < sizeof ui->ui_colours ? n : sizeof ui->ui_colours);
	return 0;
}

static int ui_now(lua_State *L)
{
	check_ui(L)->now = arg_tick(L, 2);
	return 0;
}

/* u:cursor(id[, counter]): the cursor (SetCursorGrp 0x43e5e0; the counter set as the oracle's log has it);
 * u:cursor_animate() (AnimateCursor 0x43e410 at the interface's tick); u:cursor_bitmap() -> bytes, w, h, dx, dy (the
 * frame, index 0 transparent, drawn at the mouse + (dx, dy)), or nil; u:cursor_key() -> the frame's key (another key,
 * another bitmap), or nil hidden. */
static int ui_cursor(lua_State *L)
{
	KpUi *ui = check_ui(L);
	kp_ui_cursor_set(ui, (uint16_t)luaL_checknumber(L, 2));
	if (!lua_isnoneornil(L, 3)) ui->cursor_counter = (uint32_t)luaL_checknumber(L, 3);
	return 0;
}

/* u:cursor_update([cx, cy]) -> id, counter: UpdateCursor 0x43ea40 (the game loop's) with the system cursor's position
 * (the last event's when not given), the cursor after it. */
static int ui_cursor_update(lua_State *L)
{
	KpUi *ui = check_ui(L);
	kp_ui_cursor_update(ui, (int32_t)luaL_optnumber(L, 2, ui->mouse_x), (int32_t)luaL_optnumber(L, 3, ui->mouse_y));
	lua_pushnumber(L, ui->cursor);
	lua_pushnumber(L, ui->cursor_counter);
	return 2;
}

static int ui_cursor_animate(lua_State *L)
{
	kp_ui_cursor_animate(check_ui(L));
	return 0;
}

#define CURSOR_BYTES (256 * 256) /* a cursor frame's largest (its GRP's bytes for width and height) */

static int ui_cursor_key(lua_State *L)
{
	int64_t key = kp_ui_cursor_key(check_ui(L));
	if (key < 0) return 0;
	lua_pushnumber(L, (lua_Number)key);
	return 1;
}

static int ui_game_view_active(lua_State *L)
{
	lua_pushboolean(L, kp_ui_game_view_active(check_ui(L)));
	return 1;
}

static int ui_game_view_update(lua_State *L)
{
	kp_ui_game_view_update(check_ui(L));
	return 0;
}

static int ui_capture_changed(lua_State *L)
{
	kp_ui_capture_changed(check_ui(L));
	return 0;
}

/* u:from_save(): the next console's game is a saved game's (0x6a5e38: no tips at its start). */
static int ui_from_save(lua_State *L)
{
	check_ui(L)->from_save = 1;
	return 0;
}

/* u:active(flag): the window's activity (kp_ui_set_active, WM_ACTIVATEAPP). */
static int ui_active(lua_State *L)
{
	kp_ui_set_active(check_ui(L), lua_toboolean(L, 2));
	return 0;
}

static int ui_player_colours(lua_State *L)
{
	KpUi *ui = check_ui(L);
	size_t size;
	const char *rows = luaL_checklstring(L, 2, &size);
	if (size != sizeof ui->player_colours) return luaL_error(L, "ui: player colours take %d bytes", (int)sizeof ui->player_colours);
	memcpy(ui->player_colours, rows, size);
	return 0;
}

static int ui_cursor_bitmap(lua_State *L)
{
	KpUi *ui = check_ui(L);
	uint8_t *pixels = (uint8_t *)lua_newuserdata(L, CURSOR_BYTES); /* not on the C stack */
	int32_t w, h, dx, dy;
	if (kp_ui_cursor_frame(ui, pixels, CURSOR_BYTES, &w, &h, &dx, &dy) != 0) return 0;
	lua_pushlstring(L, (const char *)pixels, (size_t)w * (size_t)h);
	lua_pushnumber(L, w);
	lua_pushnumber(L, h);
	lua_pushnumber(L, dx);
	lua_pushnumber(L, dy);
	return 5;
}

/* u:input(type, a, b): an event of the window procedure (mouse types: a, b = x, y; keys, characters and commands:
 * a = the key, character or command id, b = the modifiers; wheel: a = the parameter); u:keys(shift, ctrl, alt,
 * mouse_state) as the event found them; u:screen(x, y) the game view's top-left in map pixels;
 * u:take_commands() -> { bytes, ... } the commands the interface queued since the last take. */
static int ui_input(lua_State *L)
{
	KpUi *ui = check_ui(L);
	KpUiEvent e;
	memset(&e, 0, sizeof e);
	e.type = (uint16_t)luaL_checknumber(L, 2);
	switch (e.type) {
	case KP_UI_KEY_DOWN: case KP_UI_KEY_REPEAT: case KP_UI_KEY_UP: case KP_UI_CHAR: case KP_UI_COMMAND:
		e.key = (uint16_t)luaL_optnumber(L, 3, 0);
		e.mods = (uint16_t)luaL_optnumber(L, 4, 0);
		e.x = ui->mouse_x;
		e.y = ui->mouse_y;
		break;
	case KP_UI_WHEEL_UP: case KP_UI_WHEEL_DOWN:
		e.param = (uint32_t)luaL_optnumber(L, 3, 0);
		break;
	default:
		e.x = (int16_t)luaL_optnumber(L, 3, ui->mouse_x);
		e.y = (int16_t)luaL_optnumber(L, 4, ui->mouse_y);
		break;
	}
	kp_ui_input_event(ui, &e);
	return 0;
}

/* u:window_key(down, vk[, repeat]) -> translated and u:window_char(code[, repeat]): the window's WM_KEYDOWN/WM_KEYUP
 * and WM_CHAR (the accelerators, the key bytes, the events; kp_ui_keys.c); translated: whether the key's WM_CHAR
 * follows (TranslateMessage: no accelerator took the key and no dialog closed on it). */
static int ui_window_key(lua_State *L)
{
	lua_pushboolean(L, kp_ui_window_key(check_ui(L), lua_toboolean(L, 2), (uint16_t)luaL_checknumber(L, 3),
		lua_toboolean(L, 4)));
	return 1;
}

static int ui_window_char(lua_State *L)
{
	kp_ui_window_char(check_ui(L), (uint16_t)luaL_checknumber(L, 2), lua_toboolean(L, 3));
	return 0;
}

static int ui_keys(lua_State *L)
{
	kp_ui_set_keys(check_ui(L), (uint8_t)luaL_checknumber(L, 2), (uint8_t)luaL_checknumber(L, 3),
		(uint8_t)luaL_checknumber(L, 4), (uint16_t)luaL_optnumber(L, 5, 0));
	return 0;
}

/* u:pick(x, y) -> unit: the unit a click at map (x, y) takes (PickUnitAtPoint 0x4bcd00), 0 none. */
static int ui_pick(lua_State *L)
{
	lua_pushnumber(L, (lua_Number)kp_ui_pick_unit(check_ui(L), (int32_t)luaL_checknumber(L, 2),
		(int32_t)luaL_checknumber(L, 3)));
	return 1;
}

static int ui_screen(lua_State *L)
{
	kp_ui_set_screen(check_ui(L), (int32_t)luaL_checknumber(L, 2), (int32_t)luaL_checknumber(L, 3));
	return 0;
}

/* u:invalid_file_name(name, limit[, allow_path]) -> flag: IsInvalidFileName 0x408300 (kp_ui_invalid_file_name). */
static int ui_invalid_file_name(lua_State *L)
{
	lua_pushboolean(L, kp_ui_invalid_file_name(check_ui(L), luaL_checkstring(L, 2), (size_t)luaL_checknumber(L, 3),
		lua_toboolean(L, 4)));
	return 1;
}

/* u:start_screen(): the game's first view on the local player's start location (kp_ui_start_screen). */
static int ui_start_screen(lua_State *L)
{
	kp_ui_start_screen(check_ui(L));
	return 0;
}

/* u:view(w, h): the game view's size in pixels (640 x 400 the original's; a wider layout's view). */
static int ui_view(lua_State *L)
{
	kp_ui_set_view(check_ui(L), (int32_t)luaL_checknumber(L, 2), (int32_t)luaL_checknumber(L, 3));
	return 0;
}

/* u:over_console(flag): the layout's answer for the next events (nil: the original's console art). */
static int ui_over_console(lua_State *L)
{
	kp_ui_set_over_console(check_ui(L), lua_isnoneornil(L, 2) ? -1 : lua_toboolean(L, 2));
	return 0;
}

/* u:take_camera() -> x, y or nil: the view the interface moved to (a group's centre, the minimap). */
static int ui_take_camera(lua_State *L)
{
	KpUi *ui = check_ui(L);
	if (!ui->input.camera_moved) return 0;
	ui->input.camera_moved = 0;
	lua_pushnumber(L, ui->input.screen_x);
	lua_pushnumber(L, ui->input.screen_y);
	return 2;
}

/* u:scroll(cursor_x, cursor_y) -> moved: UpdateMapScroll 0x49bf10 with the system cursor's position;
 * u:scroll_state() -> { x, y, step, dx, dy, suppressed, cursor_x, cursor_y, pan }; u:scroll_speeds(mouse, key) (0..6);
 * u:take_warp_cursor() -> true once the camera pan's end moved the cursor to the view's centre. */
static int ui_scroll(lua_State *L)
{
	lua_pushboolean(L, kp_ui_update_scroll(check_ui(L), (int32_t)luaL_checknumber(L, 2), (int32_t)luaL_checknumber(L, 3)));
	return 1;
}

/* u:edge_scroll(cx, cy) -> id: GetEdgeScrollCursorId 0x43e7b0 as UpdateCursor 0x43ea40 calls it before each scroll
 * (the cursor position it keeps, the suppression it ends). */
static int ui_edge_scroll(lua_State *L)
{
	lua_pushnumber(L, kp_ui_edge_scroll(check_ui(L), (int32_t)luaL_checknumber(L, 2), (int32_t)luaL_checknumber(L, 3),
		NULL, NULL));
	return 1;
}

static void set_int(lua_State *L, const char *k, lua_Number v)
{
	lua_pushnumber(L, v);
	lua_setfield(L, -2, k);
}

static int ui_scroll_state(lua_State *L)
{
	KpUi *ui = check_ui(L);
	const KpUiScroll *s = &ui->input.scroll;
	lua_newtable(L);
	set_int(L, "x", ui->input.screen_x);
	set_int(L, "y", ui->input.screen_y);
	set_int(L, "step", s->step);
	set_int(L, "dx", s->dx);
	set_int(L, "dy", s->dy);
	set_int(L, "suppressed", s->suppressed);
	set_int(L, "cursor_x", s->cursor_x);
	set_int(L, "cursor_y", s->cursor_y);
	set_int(L, "pan", s->pan);
	set_int(L, "pan_x", s->pan_x);
	set_int(L, "pan_y", s->pan_y);
	return 1;
}

static int ui_scroll_speeds(lua_State *L)
{
	KpUi *ui = check_ui(L);
	ui->mouse_scroll_speed = (uint32_t)luaL_checknumber(L, 2);
	ui->key_scroll_speed = (uint32_t)luaL_checknumber(L, 3);
	return 0;
}

static int ui_take_warp_cursor(lua_State *L)
{
	KpUi *ui = check_ui(L);
	lua_pushboolean(L, ui->input.scroll.warp_cursor);
	ui->input.scroll.warp_cursor = 0;
	return 1;
}

/* u:tooltip() -> bytes, x, y, w, h or nil: the tooltip box shown (its w x h indices, layer 1's place). */
static int ui_tooltip(lua_State *L)
{
	KpUi *ui = check_ui(L);
	int32_t x, y, w, h, row;
	const KpUiSurface *t = kp_ui_tooltip(ui, &x, &y, &w, &h);
	luaL_Buffer b;
	if (t == NULL || w <= 0 || h <= 0 || w > t->w || h > t->h) return 0;
	luaL_buffinit(L, &b);
	for (row = 0; row < h; row++) luaL_addlstring(&b, (const char *)t->pixels + (size_t)row * (size_t)t->w, (size_t)w);
	luaL_pushresult(&b);
	lua_pushnumber(L, x);
	lua_pushnumber(L, y);
	lua_pushnumber(L, w);
	lua_pushnumber(L, h);
	return 5;
}

/* u:sound_device(device, effects): a DirectSound device or none and the effects volume (percent; 0x6aa954,
 * 0x6d3140); u:take_sound_calls() -> { { id, attenuation, pan, unit_type, path, result }, ... } (PlaySoundInternal
 * 0x49e4e0's calls); u:take_talks() -> { { unit, duration, sound, ping, colour }, ... } (RequestUnitTalking 0x4a7200);
 * u:take_sound_commands() -> { { type, channel, sound, volume, pan }, ... } (the plays and stops for the mixer). */
static int ui_sound_device(lua_State *L)
{
	KpUi *ui = check_ui(L);
	ui->sound.device = (uint8_t)lua_toboolean(L, 2);
	ui->sound.effects = (uint32_t)luaL_optnumber(L, 3, ui->sound.effects);
	return 0;
}

/* u:voices(flag): whether the interface plays the sounds (an older oracle log gives them itself). */
static int ui_voices(lua_State *L)
{
	check_ui(L)->sound.voices = (uint8_t)lua_toboolean(L, 2);
	return 0;
}

static void push_row(lua_State *L, int k, const lua_Number *v, int n)
{
	int i;
	lua_createtable(L, n, 0);
	for (i = 0; i < n; i++) {
		lua_pushnumber(L, v[i]);
		lua_rawseti(L, -2, i + 1);
	}
	lua_rawseti(L, -2, k);
}

static int ui_take_sound_calls(lua_State *L)
{
	KpUiSoundCall calls[KP_UI_SOUND_CALLS];
	size_t n = kp_ui_take_sound_calls(check_ui(L), calls, KP_UI_SOUND_CALLS), k;
	lua_createtable(L, (int)n, 0);
	for (k = 0; k < n; k++) {
		lua_Number v[6];
		v[0] = (lua_Number)calls[k].id;
		v[1] = (lua_Number)calls[k].attenuation;
		v[2] = (lua_Number)calls[k].pan;
		v[3] = (lua_Number)calls[k].unit_type;
		v[4] = (lua_Number)calls[k].path;
		v[5] = (lua_Number)calls[k].result;
		push_row(L, (int)k + 1, v, 6);
	}
	return 1;
}

static int ui_take_talks(lua_State *L)
{
	KpUi *ui = check_ui(L);
	int k;
	lua_createtable(L, ui->sound.talk_count, 0);
	for (k = 0; k < ui->sound.talk_count; k++) {
		lua_Number v[5];
		v[0] = (lua_Number)ui->sound.talks[k].unit;
		v[1] = (lua_Number)ui->sound.talks[k].duration;
		v[2] = (lua_Number)ui->sound.talks[k].sound;
		v[3] = (lua_Number)ui->sound.talks[k].ping;
		v[4] = (lua_Number)ui->sound.talks[k].colour;
		push_row(L, k + 1, v, 5);
	}
	ui->sound.talk_count = 0;
	return 1;
}

static int ui_take_sound_commands(lua_State *L)
{
	KpUiSoundCommand c[KP_UI_SOUND_COMMANDS];
	size_t n = kp_ui_take_sound_commands(check_ui(L), c, KP_UI_SOUND_COMMANDS), k;
	lua_createtable(L, (int)n, 0);
	for (k = 0; k < n; k++) {
		lua_Number v[5];
		v[0] = (lua_Number)c[k].type;
		v[1] = (lua_Number)c[k].channel;
		v[2] = (lua_Number)c[k].sound;
		v[3] = (lua_Number)c[k].volume;
		v[4] = (lua_Number)c[k].pan;
		push_row(L, (int)k + 1, v, 5);
	}
	return 1;
}

static int ui_take_commands(lua_State *L)
{
	KpUi *ui = check_ui(L);
	uint8_t buf[KP_UI_COMMAND_BYTES];
	size_t n;
	int k = 0;
	lua_newtable(L);
	while ((n = kp_ui_take_command(ui, buf, sizeof buf)) != 0) {
		lua_pushlstring(L, (const char *)buf, n);
		lua_rawseti(L, -2, ++k);
	}
	return 1;
}

/* u:cheats(chea): campaign.kpd's CHEA section (kept alive with the interface). */
static int ui_cheats(lua_State *L)
{
	KpUi *ui = check_ui(L);
	size_t n;
	const char *s = luaL_checklstring(L, 2, &n);
	lua_getfenv(L, 1);
	lua_pushvalue(L, 2);
	lua_setfield(L, -2, "cheats");
	lua_pop(L, 1);
	if (kp_ui_set_cheats(ui, (const uint8_t *)s, n) != 0) return luaL_error(L, "ui: a malformed CHEA section");
	return 0;
}

/* u:take_release() -> bool: a trigger portrait's time ran out (the caller releases the trigger wait). */
static int ui_pending_releases(lua_State *L)
{
	uint32_t ms = 0;
	int camera = 0;
	if (kp_ui_pending_releases(check_ui(L), &ms, &camera)) lua_pushnumber(L, ms);
	else lua_pushnil(L);
	lua_pushboolean(L, camera);
	return 2;
}

static int ui_game_loaded(lua_State *L)
{
	kp_ui_game_loaded(check_ui(L));
	return 0;
}

static int ui_take_release(lua_State *L)
{
	lua_pushboolean(L, kp_ui_take_release(check_ui(L)));
	return 1;
}

/* u:portrait_mode([mode]) -> mode: the portraits' mode (0x50d7c8: 0 none, 1 still, 2 animated), set when given
 * (SetPortraitMode 0x4a6e90: the status unit's portrait again). */
static int ui_portrait_mode(lua_State *L)
{
	KpUi *ui = check_ui(L);
	if (!lua_isnoneornil(L, 2)) kp_ui_portrait_set_mode(ui, (uint32_t)luaL_checknumber(L, 2));
	lua_pushnumber(L, ui->portrait.mode);
	return 1;
}

/* u:options([word]) -> word: the game options word (0x6d3148, KP_UI_OPTION_*), set when given. */
static int ui_options(lua_State *L)
{
	KpUi *ui = check_ui(L);
	if (!lua_isnoneornil(L, 2)) ui->options = (uint32_t)luaL_checknumber(L, 2);
	lua_pushnumber(L, ui->options);
	return 1;
}

/* u:strings(strings_kpd): the string tables (kept alive with the interface). */
static int ui_strings(lua_State *L)
{
	KpUi *ui = check_ui(L);
	size_t n;
	const char *k = luaL_checklstring(L, 2, &n);
	lua_getfenv(L, 1);
	lua_pushvalue(L, 2);
	lua_setfield(L, -2, "strings");
	lua_pop(L, 1);
	if (kp_ui_set_strings(ui, (const uint8_t *)k, n) != 0) return luaL_error(L, "ui: %s", ui->error);
	return 0;
}

/* u:portraits(portraits_kpd): the portrait movies (kept alive with the interface). */
static int ui_portraits(lua_State *L)
{
	KpUi *ui = check_ui(L);
	size_t n;
	const char *k = luaL_checklstring(L, 2, &n);
	lua_getfenv(L, 1);
	lua_pushvalue(L, 2);
	lua_setfield(L, -2, "portraits");
	lua_pop(L, 1);
	if (kp_ui_set_portraits(ui, (const uint8_t *)k, n) != 0) return luaL_error(L, "ui: %s", ui->error);
	return 0;
}

/* u:sounds(sounds_kpd) */
static int ui_sounds(lua_State *L)
{
	KpUi *ui = check_ui(L);
	size_t n;
	const char *k = luaL_checklstring(L, 2, &n);
	lua_getfenv(L, 1);
	lua_pushvalue(L, 2);
	lua_setfield(L, -2, "sounds");
	lua_pop(L, 1);
	if (kp_ui_set_sounds(ui, (const uint8_t *)k, n) != 0) return luaL_error(L, "ui: %s", ui->error);
	return 0;
}

/* u:talk(unit, duration, sound[, ping, colour]): RequestUnitTalking 0x4a7200. */
static int ui_talk(lua_State *L)
{
	kp_ui_portrait_talk(check_ui(L), (int64_t)luaL_checknumber(L, 2), arg_tick(L, 3), lua_toboolean(L, 5),
		(uint8_t)luaL_optinteger(L, 6, 0), arg_tick(L, 4));
	return 0;
}

/* u:portrait_state() -> { open =, path =, frame =, frames =, next =, ready =, fade =, fresh =, talking =, unit =,
 * portrait = } (for tests and the oracle's diagnostics) */
static int ui_portrait_state(lua_State *L)
{
	KpUi *ui = check_ui(L);
	KpUiPortrait *p = &ui->portrait;
	lua_createtable(L, 0, 11);
#define F(name, v) lua_pushnumber(L, (lua_Number)(v)); lua_setfield(L, -2, name);
	F("open", p->video.open) F("frame", p->video.open ? p->video.smk.frame : -1)
	F("frames", p->video.open ? p->video.smk.frames : 0) F("next", p->video.next) F("ready", p->video.ready)
	F("fade", p->fade) F("fresh", p->fresh) F("talking", p->talking)
	F("unit", p->control >= 0 ? ui->controls[p->control].slot.unit : 0)
	F("portrait", p->control >= 0 ? ui->controls[p->control].wuser : 0xffff)
	F("rng_x", p->rng.x) F("rng_y", p->rng.y) F("rng_z", p->rng.z) F("static_draws", p->static_draws)
#undef F
	if (p->video.open) {
		int i;
		for (i = 0; i < ui->portrait_file_count; i++) {
			if (ui->portrait_files[i].data == p->video.smk.data) {
				lua_pushlstring(L, ui->portrait_files[i].path, ui->portrait_files[i].path_len);
				lua_setfield(L, -2, "path");
			}
		}
	}
	return 1;
}

/* u:minimap(mmni, rgb768, cycled256, dark8192, colour_slots8 or nil): the minimap's data (kept alive with the
 * interface). */
static int ui_minimap(lua_State *L)
{
	KpUi *ui = check_ui(L);
	size_t mn, rn, cn, dn, sn = 0;
	const char *mmni = luaL_checklstring(L, 2, &mn), *rgb = luaL_checklstring(L, 3, &rn), *cyc = luaL_checklstring(L, 4, &cn);
	const char *dark = luaL_checklstring(L, 5, &dn), *slots = lua_isnoneornil(L, 6) ? NULL : luaL_checklstring(L, 6, &sn);
	if (rn < 768 || cn < 256 || dn < 32 * 256 || (slots != NULL && sn < 8)) return luaL_error(L, "ui: minimap data sizes");
	lua_getfenv(L, 1);
	lua_newtable(L);
	lua_pushvalue(L, 2);
	lua_rawseti(L, -2, 1);
	lua_pushvalue(L, 5);
	lua_rawseti(L, -2, 2);
	lua_setfield(L, -2, "minimap");
	lua_pop(L, 1);
	if (kp_ui_set_minimap(ui, (const uint8_t *)mmni, mn, (const uint8_t *)rgb, (const uint8_t *)cyc,
		(const uint8_t *)dark, (const uint8_t *)slots) != 0) {
		return luaL_error(L, "ui: no tminimap.pcx or tblink.pcx");
	}
	return 0;
}

/* u:resolve_colours(rgb768, cycled256 or nil) */
static int ui_resolve_colours(lua_State *L)
{
	KpUi *ui = check_ui(L);
	size_t n, m = 0;
	const char *rgb = luaL_checklstring(L, 2, &n), *cycled = lua_isnoneornil(L, 3) ? NULL : luaL_checklstring(L, 3, &m);
	if (n < 768 || (cycled != NULL && m < 256)) return luaL_error(L, "ui: a palette of 768 bytes and 256 flags");
	kp_ui_resolve_colours(ui, (const uint8_t *)rgb, (const uint8_t *)cycled);
	return 0;
}

/* u:hud(game, ticks): the console over the game (kp_ui_hud_create; the game is kept alive with the interface);
 * ticks (optional): { [dialog BIN] = creation tick } for the console's dialogs. */
static int ui_hud(lua_State *L)
{
	static const char *const BINS[KP_UI_HUD_DIALOGS] = { "rez\\statport.bin", "rez\\statlb.bin", "rez\\statres.bin",
		"rez\\statdata.bin", "rez\\statbtn", "rez\\stat_f10.bin", "textbox", "rez\\minimap.bin" };
	KpUi *ui = check_ui(L);
	struct KpGame *g = lua_isnoneornil(L, 2) ? NULL : kp_lua_check_game(L, 2);
	uint32_t ticks[KP_UI_HUD_DIALOGS];
	int k, have = 0;
	memset(ticks, 0, sizeof ticks);
	if (lua_istable(L, 3)) {
		for (k = 0; k < KP_UI_HUD_DIALOGS; k++) {
			lua_getfield(L, 3, BINS[k]);
			if (lua_isnumber(L, -1)) {
				ticks[k] = (uint32_t)lua_tonumber(L, -1);
				have = 1;
			}
			lua_pop(L, 1);
		}
	}
	lua_getfenv(L, 1);
	lua_pushvalue(L, 2);
	lua_setfield(L, -2, "game");
	lua_pop(L, 1);
	if (kp_ui_hud_create(ui, g, have ? ticks : NULL) != 0) return luaL_error(L, "ui: the console: %s", ui->error);
	return 0;
}

/* u:game_effects(rgb768, cycled256): the in-game dialogs' blend tables (StartDialogPaletteEffect 0x47bca0). */
static int ui_game_effects(lua_State *L)
{
	KpUi *ui = check_ui(L);
	size_t n, m;
	const char *rgb = luaL_checklstring(L, 2, &n), *cycled = luaL_checklstring(L, 3, &m);
	if (n < 768 || m < 256) return luaL_error(L, "ui: a palette of 768 bytes and 256 flags");
	if (kp_ui_game_effects(ui, (const uint8_t *)rgb, (const uint8_t *)cycled) != 0) return luaL_error(L, "ui: out of memory");
	return 0;
}

/* u:blend_row() -> 256 bytes, colour: single-colour set A's result over each index (the canvas's
 * KP_UI_CANVAS_BLEND_A) and the colour that blends. */
static int ui_blend_row(lua_State *L)
{
	KpUi *ui = check_ui(L);
	uint8_t row[256];
	kp_ui_canvas_blend_row(ui, row);
	lua_pushlstring(L, (const char *)row, sizeof row);
	lua_pushinteger(L, ui->effects.colour[0]);
	return 2;
}

/* u:glue_palette() -> 768 bytes (R, G, B per index), serial: the glue's palette and its counter, bumped when the host
 * should upload it (a screen's theme, the credits' pages). */
static int ui_glue_palette(lua_State *L)
{
	KpUi *ui = check_ui(L);
	uint8_t rgb[768];
	int k;
	for (k = 0; k < 256; k++) {
		rgb[3 * k] = ui->glue.palette[4 * k];
		rgb[3 * k + 1] = ui->glue.palette[4 * k + 1];
		rgb[3 * k + 2] = ui->glue.palette[4 * k + 2];
	}
	lua_pushlstring(L, (const char *)rgb, sizeof rgb);
	lua_pushnumber(L, ui->glue.palette_serial);
	return 2;
}

/* u:game_exit() -> code: the exit code a result dialog's answer left (0 none, 6 victory, 7 defeat), u:modeless() ->
 * the modeless dialog or -1, u:show_result(result) -> dialog (2 defeat, 3 victory, 5 draw; -1 when not shown). */
static int ui_game_exit(lua_State *L)
{
	lua_pushinteger(L, check_ui(L)->game_exit);
	return 1;
}

static int ui_modeless(lua_State *L)
{
	lua_pushinteger(L, check_ui(L)->modeless);
	return 1;
}

static int ui_end_game(lua_State *L)
{
	kp_ui_end_game(check_ui(L));
	return 0;
}

static const char *const SCORE_ARRAY_NAMES[KP_UI_SCORE_ARRAYS] = { "minerals", "gas", "cumulative_gas",
	"cumulative_minerals", "men_produced", "men_lost", "men_killed", "unit_score", "kill_score", "buildings_constructed",
	"buildings_lost", "buildings_razed", "structure_score", "razing_score" };

static lua_Number field_number(lua_State *L, int t, const char *key)
{
	lua_Number v;
	lua_getfield(L, t, key);
	v = lua_tonumber(L, -1);
	lua_pop(L, 1);
	return v;
}

static void field_bytes(lua_State *L, int t, const char *key, uint8_t *out, int n)
{
	int k;
	lua_getfield(L, t, key);
	for (k = 0; k < n && lua_istable(L, -1); k++) {
		lua_rawgeti(L, -1, k + 1);
		out[k] = (uint8_t)lua_tonumber(L, -1);
		lua_pop(L, 1);
	}
	lua_pop(L, 1);
}

/* u:score(data) -> dialog: the score screen over a table as uilog.py's "score" record has it ({ roster = { {name=,
 * force=, index=, storm=, type=, race=} x 8 }, count=, arrays = { name = {12} }, colours = {12}, player_colours = {8} })
 * plus result (1 victory, 2 defeat, 3 draw), race, local_storm, elapsed, replay. */
static int ui_score(lua_State *L)
{
	KpUi *ui = check_ui(L);
	KpUiScoreData d;
	int k, j;
	luaL_checktype(L, 2, LUA_TTABLE);
	memset(&d, 0, sizeof d);
	lua_getfield(L, 2, "roster");
	for (k = 0; k < KP_UI_SCORE_SLOTS && lua_istable(L, -1); k++) {
		const char *str;
		lua_rawgeti(L, -1, k + 1);
		if (lua_istable(L, -1)) {
			int e = lua_gettop(L);
			lua_getfield(L, e, "name");
			str = lua_tostring(L, -1);
			snprintf(d.roster[k].name, sizeof d.roster[k].name, "%s", str ? str : "");
			lua_pop(L, 1);
			lua_getfield(L, e, "force");
			str = lua_tostring(L, -1);
			snprintf(d.roster[k].force, sizeof d.roster[k].force, "%s", str ? str : "");
			lua_pop(L, 1);
			d.roster[k].index = (uint32_t)field_number(L, e, "index");
			d.roster[k].storm = (uint32_t)field_number(L, e, "storm");
			d.roster[k].type = (uint32_t)field_number(L, e, "type");
			d.roster[k].race = (uint8_t)field_number(L, e, "race");
		}
		lua_pop(L, 1);
	}
	lua_pop(L, 1);
	lua_getfield(L, 2, "arrays");
	for (k = 0; k < KP_UI_SCORE_ARRAYS && lua_istable(L, -1); k++) {
		lua_getfield(L, -1, SCORE_ARRAY_NAMES[k]);
		for (j = 0; j < 12 && lua_istable(L, -1); j++) {
			lua_rawgeti(L, -1, j + 1);
			d.arrays[k][j] = (int32_t)(int64_t)lua_tonumber(L, -1);
			lua_pop(L, 1);
		}
		lua_pop(L, 1);
	}
	lua_pop(L, 1);
	field_bytes(L, 2, "colours", d.colours, 12);
	field_bytes(L, 2, "player_colours", d.player_colours, 8);
	d.count = (int)field_number(L, 2, "count");
	d.result = (int)field_number(L, 2, "result");
	d.race = (int)field_number(L, 2, "race");
	d.local_storm = (uint32_t)field_number(L, 2, "local_storm");
	d.elapsed = (uint32_t)field_number(L, 2, "elapsed");
	d.replay = (int)field_number(L, 2, "replay");
	lua_pushinteger(L, kp_ui_score_open(ui, &d));
	return 1;
}

/* u:score_game(game, result, player name, elapsed seconds) -> dialog: the score screen of a finished game
 * (KP_EVENT_GAME_RESULT's result). */
static int ui_score_game(lua_State *L)
{
	KpUi *ui = check_ui(L);
	struct KpGame *g = kp_lua_check_game(L, 2);
	KpUiScoreData d;
	kp_ui_score_data(ui, g, (int)luaL_checkinteger(L, 3), luaL_optstring(L, 4, ""), (uint32_t)luaL_optnumber(L, 5, 0), &d);
	lua_pushinteger(L, kp_ui_score_open(ui, &d));
	return 1;
}

/* u:score_state() -> { dialog =, answered =, result =, glue_state =, tab =, elapsed = (the seconds it shows) } */
static int ui_score_state(lua_State *L)
{
	KpUi *ui = check_ui(L);
	lua_createtable(L, 0, 5);
	lua_pushinteger(L, ui->score.dialog);
	lua_setfield(L, -2, "dialog");
	lua_pushboolean(L, ui->score.answered);
	lua_setfield(L, -2, "answered");
	lua_pushinteger(L, ui->result);
	lua_setfield(L, -2, "result");
	lua_pushinteger(L, kp_ui_score_glue_state(ui));
	lua_setfield(L, -2, "glue_state");
	lua_pushinteger(L, ui->score.tab);
	lua_setfield(L, -2, "tab");
	lua_pushnumber(L, ui->score.data.elapsed);
	lua_setfield(L, -2, "elapsed");
	return 1;
}

/* u:menus_enter(state) -> dialog or -1: the glue state's screen (kp_ui_menus_enter). */
static int ui_menus_enter(lua_State *L)
{
	lua_pushinteger(L, kp_ui_menus_enter(check_ui(L), (int)luaL_checkinteger(L, 2)));
	return 1;
}

/* u:menus_poll() -> nil, or { state =, action =, mission =, cinematic =, player =, expansion = } once the screen
 * ended (action: 1 the next state, 2 exit, 3 intro, 4 credits, 5 a mission, 6 a cinematic, 7 a saved game). */
static int ui_menus_poll(lua_State *L)
{
	KpUi *ui = check_ui(L);
	KpUiMenus *m = &ui->menus;
	if (!kp_ui_menus_poll(ui)) return 0;
	lua_createtable(L, 0, 7);
	lua_pushinteger(L, m->state);
	lua_setfield(L, -2, "state");
	lua_pushinteger(L, m->action);
	lua_setfield(L, -2, "action");
	lua_pushinteger(L, m->mission);
	lua_setfield(L, -2, "mission");
	lua_pushinteger(L, m->cinematic);
	lua_setfield(L, -2, "cinematic");
	lua_pushstring(L, m->player);
	lua_setfield(L, -2, "player");
	lua_pushinteger(L, m->expansion);
	lua_setfield(L, -2, "expansion");
	lua_pushinteger(L, m->save_choice);
	lua_setfield(L, -2, "save");
	m->action = KP_UI_MENU_NONE;
	return 1;
}

/* u:profiles({ { name =, time =, progress = { 6 } }, ... }) sets the profiles the login screen lists;
 * u:profiles() -> the same table, changed (a profile was created or deleted since the last call). */
static int ui_profiles(lua_State *L)
{
	KpUi *ui = check_ui(L);
	KpUiMenus *m = &ui->menus;
	int k, j;
	if (lua_istable(L, 2)) {
		m->profile_count = 0;
		for (k = 0; k < KP_UI_PROFILES; k++) {
			const char *name;
			lua_rawgeti(L, 2, k + 1);
			if (!lua_istable(L, -1)) {
				lua_pop(L, 1);
				break;
			}
			memset(&m->profiles[k], 0, sizeof m->profiles[k]);
			lua_getfield(L, -1, "name");
			name = lua_tostring(L, -1);
			snprintf(m->profiles[k].name, sizeof m->profiles[k].name, "%s", name ? name : "");
			lua_pop(L, 1);
			m->profiles[k].time = (uint32_t)field_number(L, lua_gettop(L), "time");
			lua_getfield(L, -1, "progress");
			for (j = 0; j < 6 && lua_istable(L, -1); j++) {
				lua_rawgeti(L, -1, j + 1);
				m->profiles[k].progress[j] = (uint32_t)lua_tonumber(L, -1);
				lua_pop(L, 1);
			}
			lua_pop(L, 2);
			m->profile_count++;
		}
		return 0;
	}
	lua_createtable(L, m->profile_count, 0);
	for (k = 0; k < m->profile_count; k++) {
		lua_createtable(L, 0, 3);
		lua_pushstring(L, m->profiles[k].name);
		lua_setfield(L, -2, "name");
		lua_pushnumber(L, m->profiles[k].time);
		lua_setfield(L, -2, "time");
		lua_createtable(L, 6, 0);
		for (j = 0; j < 6; j++) {
			lua_pushnumber(L, m->profiles[k].progress[j]);
			lua_rawseti(L, -2, j + 1);
		}
		lua_setfield(L, -2, "progress");
		lua_rawseti(L, -2, k + 1);
	}
	lua_pushboolean(L, m->profiles_changed);
	m->profiles_changed = 0;
	return 2;
}

/* u:campaign(tabl): campaign.kpd's TABL section (kept alive with the interface). */
static int ui_campaign(lua_State *L)
{
	KpUi *ui = check_ui(L);
	size_t n;
	const char *t = luaL_checklstring(L, 2, &n);
	lua_getfenv(L, 1);
	lua_pushvalue(L, 2);
	lua_setfield(L, -2, "campaign");
	lua_pop(L, 1);
	ui->menus.campaign = (const uint8_t *)t;
	ui->menus.campaign_size = n;
	return 0;
}

/* u:take_save_request() -> nil or { kind = "delete", name = }: a save dialog's request for the host's files. */
static int ui_take_save_request(lua_State *L)
{
	KpUi *ui = check_ui(L);
	if (ui->save_request.kind == KP_UI_SAVE_NONE) return 0;
	lua_createtable(L, 0, 2);
	lua_pushstring(L, "delete");
	lua_setfield(L, -2, "kind");
	lua_pushstring(L, ui->save_request.name);
	lua_setfield(L, -2, "name");
	ui->save_request.kind = KP_UI_SAVE_NONE;
	return 1;
}

/* u:saves({ name, ... }): the player's saved games the load screen and the save dialogs list. */
static int ui_saves(lua_State *L)
{
	KpUi *ui = check_ui(L);
	int k;
	luaL_checktype(L, 2, LUA_TTABLE);
	ui->menus.save_count = 0;
	for (k = 0; k < KP_UI_SAVES; k++) {
		const char *name;
		lua_rawgeti(L, 2, k + 1);
		name = lua_tostring(L, -1);
		lua_pop(L, 1);
		if (name == NULL) break;
		snprintf(ui->menus.saves[k], sizeof ui->menus.saves[k], "%s", name);
		ui->menus.save_count++;
	}
	return 0;
}

/* u:timer_order() -> { { owner =, id =, due = }, ... }: the UI timers' queue in order (for the oracle's diagnostics). */
static int ui_timer_order(lua_State *L)
{
	KpUi *ui = check_ui(L);
	int32_t order[KP_UI_TIMERS];
	int n = kp_ui_timer_order(&ui->timers, order, KP_UI_TIMERS), k;
	lua_createtable(L, n, 0);
	for (k = 0; k < n; k++) {
		const KpUiTimer *t = &ui->timers.nodes[order[k]];
		lua_createtable(L, 0, 3);
		lua_pushnumber(L, (lua_Number)t->owner);
		lua_setfield(L, -2, "owner");
		if (t->owner >= 0 && t->owner < KP_UI_CONTROLS && ui->controls[t->owner].used) {
			lua_pushinteger(L, ui->controls[t->owner].record);
			lua_setfield(L, -2, "record");
		}
		lua_pushnumber(L, t->id);
		lua_setfield(L, -2, "id");
		lua_pushnumber(L, (lua_Number)(t->last + t->period));
		lua_setfield(L, -2, "due");
		lua_rawseti(L, -2, k + 1);
	}
	return 1;
}

/* u:time_seeds({ seed, ... }) (the oracle's: time(NULL) of each reseeding burst of the menus' CRT, in order) or
 * u:time_seeds(seed) (the host's clock). */
static int ui_time_seeds(lua_State *L)
{
	KpUi *ui = check_ui(L);
	if (lua_isnumber(L, 2)) {
		ui->menus.time_seed = (uint32_t)lua_tonumber(L, 2);
		return 0;
	}
	luaL_checktype(L, 2, LUA_TTABLE);
	{
		int n = (int)lua_objlen(L, 2), k;
		uint32_t *v = (uint32_t *)lua_newuserdata(L, sizeof(uint32_t) * (size_t)(n > 0 ? n : 1));
		for (k = 0; k < n; k++) {
			lua_rawgeti(L, 2, k + 1);
			v[k] = (uint32_t)lua_tonumber(L, -1);
			lua_pop(L, 1);
		}
		lua_getfenv(L, 1);
		lua_pushvalue(L, -2);
		lua_setfield(L, -2, "time_seeds");
		lua_pop(L, 2);
		ui->menus.time_seeds = v;
		ui->menus.time_seed_count = n;
		ui->menus.time_seed_next = 0;
	}
	return 0;
}

/* u:invalidate_all(): the whole screen dirty (the next u:present() repaints every open dialog). */
static int ui_invalidate_all(lua_State *L)
{
	kp_ui_add_dirty_rect(check_ui(L), 0, 0, KP_UI_SCREEN_W - 1, KP_UI_SCREEN_H - 1);
	return 0;
}

static int ui_show_result(lua_State *L)
{
	lua_pushinteger(L, kp_ui_show_result(check_ui(L), (int)luaL_checkinteger(L, 2)));
	return 1;
}

/* u:ready_room(game, race) -> the room's dialog: the ready room of race (0 zerg, 1 terran, 2 protoss) over the game it
 * briefs (kp_ui_ready_room_open). */
static int ui_ready_room(lua_State *L)
{
	KpUi *ui = check_ui(L);
	struct KpGame *g = kp_lua_check_game(L, 2);
	int32_t d;
	lua_getfenv(L, 1);
	lua_pushvalue(L, 2);
	lua_setfield(L, -2, "game");
	lua_pop(L, 1);
	ui->game = g;
	if (kp_ui_ready_room_open(ui, (int)luaL_checkinteger(L, 3)) < 0) return luaL_error(L, "ui: the ready room: %s", ui->error);
	d = kp_ui_ready_room_continue(ui);
	lua_pushinteger(L, d);
	return 1;
}

/* u:ready_continue() -> the room's dialog once the establishing text closed, else -1. */
static int ui_ready_continue(lua_State *L)
{
	lua_pushinteger(L, kp_ui_ready_room_continue(check_ui(L)));
	return 1;
}

static int ui_ready_idle(lua_State *L)
{
	kp_ui_ready_room_idle(check_ui(L));
	return 0;
}

static int ui_ready_turn(lua_State *L)
{
	kp_ui_ready_room_turn(check_ui(L));
	return 0;
}

/* u:room_state() -> { dialog =, current =, wait =, result =, open = (the dialog is in the open list) } */
static int ui_room_state(lua_State *L)
{
	KpUi *ui = check_ui(L);
	int32_t d;
	int open = 0;
	for (d = ui->open; d >= 0; d = ui->controls[d].next) open |= d == ui->briefing.dialog;
	lua_createtable(L, 0, 5);
	lua_pushnumber(L, ui->briefing.dialog);
	lua_setfield(L, -2, "dialog");
	lua_pushnumber(L, (lua_Number)ui->briefing.current);
	lua_setfield(L, -2, "current");
	lua_pushnumber(L, ui->briefing.wait);
	lua_setfield(L, -2, "wait");
	lua_pushnumber(L, ui->result);
	lua_setfield(L, -2, "result");
	lua_pushboolean(L, open);
	lua_setfield(L, -2, "open");
	return 1;
}

/* u:take_brief() -> { { record, index, type, result, tick }, ... } (RunBriefingTrigger's actions since the last take) */
static int ui_take_brief(lua_State *L)
{
	KpUi *ui = check_ui(L);
	int k;
	lua_createtable(L, ui->briefing.log_count, 0);
	for (k = 0; k < ui->briefing.log_count; k++) {
		lua_Number v[5];
		v[0] = ui->briefing.log[k].record;
		v[1] = ui->briefing.log[k].index;
		v[2] = ui->briefing.log[k].type;
		v[3] = ui->briefing.log[k].result;
		v[4] = ui->briefing.log[k].tick;
		push_row(L, k + 1, v, 5);
	}
	ui->briefing.log_count = 0;
	return 1;
}

/* u:glue_crt(holdrand): the menus' CRT generator's state (the process's before a game's seed). */
static int ui_glue_crt(lua_State *L)
{
	KpUi *ui = check_ui(L);
	kp_rng_crt_init(&ui->glue.crt, (uint32_t)luaL_checknumber(L, 2));
	return 0;
}

/* u:trace(on): the timer queue's and the videos' trace for the oracle. */
static int ui_trace(lua_State *L)
{
	KpUi *ui = check_ui(L);
	if (lua_toboolean(L, 2)) {
		if (ui->timers.trace == NULL) ui->timers.trace = (KpUiTimerTrace *)calloc(1, sizeof *ui->timers.trace);
		if (ui->timers.trace == NULL) return luaL_error(L, "ui: out of memory");
		ui->timers.trace->controls = ui->controls;
		ui->video_trace = 1;
	} else {
		free(ui->timers.trace);
		ui->timers.trace = NULL;
		ui->video_trace = 0;
	}
	return 0;
}

/* u:take_trace() -> { { kind, owner, id, period, callback, tick, bin, record }, ... }, { video file, ... } since the last take
 * (kind: 1 set, 2 delete, 3 delete the owner's, 4 fire, 5 a pass), the flic clips' continuations { clip, result,
 * shown, frame }, the credits pages' ticks, the briefing portraits' continuations { id, result, shown, frame } and the
 * menus' CRT states before each draw. */
static int ui_take_trace(lua_State *L)
{
	KpUi *ui = check_ui(L);
	KpUiTimerTrace *t = ui->timers.trace;
	int k, n = t ? t->count : 0;
	lua_createtable(L, n, 0);
	for (k = 0; k < n; k++) {
		lua_Number v[8];
		v[0] = t->rec[k].kind;
		v[1] = (lua_Number)t->rec[k].owner;
		v[2] = t->rec[k].id;
		v[3] = t->rec[k].period;
		v[4] = t->rec[k].callback;
		v[5] = t->rec[k].tick;
		v[6] = t->rec[k].bin;
		v[7] = t->rec[k].record;
		push_row(L, k + 1, v, 8);
	}
	if (t) t->count = 0;
	lua_createtable(L, ui->video_count, 0);
	for (k = 0; k < ui->video_count; k++) {
		lua_pushstring(L, ui->videos[k]);
		lua_rawseti(L, -2, k + 1);
	}
	ui->video_count = 0;
	lua_createtable(L, ui->flc_log_count, 0);
	for (k = 0; k < ui->flc_log_count; k++) {
		lua_Number v[4];
		v[0] = ui->flc_log[k].clip;
		v[1] = ui->flc_log[k].result;
		v[2] = ui->flc_log[k].shown;
		v[3] = ui->flc_log[k].frame;
		push_row(L, k + 1, v, 4);
	}
	ui->flc_log_count = 0;
	lua_createtable(L, ui->page_log_count, 0);
	for (k = 0; k < ui->page_log_count; k++) {
		lua_pushnumber(L, ui->page_log[k]);
		lua_rawseti(L, -2, k + 1);
	}
	ui->page_log_count = 0;
	lua_createtable(L, ui->bpc_log_count, 0);
	for (k = 0; k < ui->bpc_log_count; k++) {
		lua_Number v[4];
		v[0] = ui->bpc_log[k].id;
		v[1] = ui->bpc_log[k].result;
		v[2] = ui->bpc_log[k].shown;
		v[3] = ui->bpc_log[k].frame;
		push_row(L, k + 1, v, 4);
	}
	ui->bpc_log_count = 0;
	lua_createtable(L, ui->glue.rand_count, 0);
	for (k = 0; k < ui->glue.rand_count; k++) {
		lua_pushnumber(L, ui->glue.rand_log[k]);
		lua_rawseti(L, -2, k + 1);
	}
	ui->glue.rand_count = 0;
	return 6;
}

/* u:page_deadlines({ tick, ... }): the oracle's deadlines of the coming credits pages, in order. */
static int ui_page_deadlines(lua_State *L)
{
	KpUi *ui = check_ui(L);
	int n = (int)lua_objlen(L, 2), k;
	uint32_t *ticks;
	luaL_checktype(L, 2, LUA_TTABLE);
	ticks = (uint32_t *)lua_newuserdata(L, sizeof *ticks * (size_t)(n > 0 ? n : 1));
	for (k = 0; k < n; k++) {
		lua_rawgeti(L, 2, k + 1);
		ticks[k] = (uint32_t)lua_tonumber(L, -1);
		lua_pop(L, 1);
	}
	lua_getfenv(L, 1);
	lua_insert(L, -2);
	lua_setfield(L, -2, "page_deadlines");
	lua_pop(L, 1);
	ui->page_deadlines = ticks;
	ui->page_deadline_count = n;
	ui->page_deadline_next = 0;
	return 0;
}

/* u:video_ticks({ tick, ... }): the oracle's clock for the coming glue movie continuations, in order. */
static int ui_video_ticks(lua_State *L)
{
	KpUi *ui = check_ui(L);
	int n = (int)lua_objlen(L, 2), k;
	uint32_t *ticks;
	luaL_checktype(L, 2, LUA_TTABLE);
	ticks = (uint32_t *)lua_newuserdata(L, sizeof *ticks * (size_t)(n > 0 ? n : 1));
	for (k = 0; k < n; k++) {
		lua_rawgeti(L, 2, k + 1);
		ticks[k] = (uint32_t)lua_tonumber(L, -1);
		lua_pop(L, 1);
	}
	lua_getfenv(L, 1);
	lua_insert(L, -2);
	lua_setfield(L, -2, "video_ticks");
	lua_pop(L, 1);
	ui->video_ticks = ticks;
	ui->video_tick_count = n;
	ui->video_tick_next = 0;
	return 0;
}

/* u:set_ticks({ tick, ... }): the oracle's clock for the coming SetUiTimer calls (the original's ticks, in order). */
static int ui_set_ticks(lua_State *L)
{
	KpUi *ui = check_ui(L);
	int n = (int)lua_objlen(L, 2), k;
	uint32_t *ticks;
	luaL_checktype(L, 2, LUA_TTABLE);
	ticks = (uint32_t *)lua_newuserdata(L, sizeof *ticks * (size_t)(n > 0 ? n : 1));
	for (k = 0; k < n; k++) {
		lua_rawgeti(L, 2, k + 1);
		ticks[k] = (uint32_t)lua_tonumber(L, -1);
		lua_pop(L, 1);
	}
	lua_getfenv(L, 1);
	lua_insert(L, -2);
	lua_setfield(L, -2, "set_ticks"); /* kept alive with the interface */
	lua_pop(L, 1);
	ui->timers.set_ticks = ticks;
	ui->timers.set_tick_count = n;
	ui->timers.set_tick_next = 0;
	return 0;
}

/* u:bin_name(index) -> a BIN's name in ui.kpd */
static int ui_bin_name(lua_State *L)
{
	KpUi *ui = check_ui(L);
	lua_Integer b = luaL_checkinteger(L, 2);
	if (b < 0 || b >= ui->bin_count) return 0;
	lua_pushlstring(L, ui->bins[b].name, ui->bins[b].name_len);
	return 1;
}

/* u:owner_ref(control) -> the BIN's name, the record index, the load number (the input log's owner references). */
static int ui_owner_ref(lua_State *L)
{
	KpUi *ui = check_ui(L);
	lua_Integer c = luaL_checkinteger(L, 2);
	const KpUiControl *k;
	if (c < 0 || c >= KP_UI_CONTROLS || !ui->controls[c].used) return 0;
	k = &ui->controls[c];
	if (k->bin < 0 || k->bin >= ui->bin_count) return 0;
	lua_pushlstring(L, ui->bins[k->bin].name, ui->bins[k->bin].name_len);
	lua_pushinteger(L, k->record);
	lua_pushinteger(L, k->load);
	return 3;
}

static int ui_hud_frame(lua_State *L)
{
	kp_ui_hud_frame(check_ui(L));
	return 0;
}

static int ui_tick(lua_State *L)
{
	lua_pushnumber(L, kp_ui_tick(check_ui(L), arg_tick(L, 2)));
	return 1;
}

/* u:hud_state() -> { resources =, status_unit =, selection_count =, shown_minerals =, shown_gas =, menu_button =,
 * textbox_mode =, card_set =, targeting =, cursor =, keys = { shift, ctrl, alt }, mirror = { unit, ... } (the
 * selection the network last got, 0x67261c: its 12 slots) } */
static int ui_hud_state(lua_State *L)
{
	KpUi *ui = check_ui(L);
	int k;
	lua_createtable(L, 0, 5);
#define F(name, v) lua_pushnumber(L, (lua_Number)(v)); lua_setfield(L, -2, name);
	F("resources", ui->hud.resources) F("status_unit", ui->hud.status_unit) F("selection_count", ui->hud.selection_count)
	F("shown_minerals", ui->hud.shown_minerals) F("shown_gas", ui->hud.shown_gas) F("menu_button", ui->menu_button)
	F("textbox_mode", ui->hud.textbox_mode) F("card_set", ui->card.set) F("targeting", ui->input.targeting)
	F("cursor", ui->cursor)
#undef F
	lua_createtable(L, 3, 0);
	lua_pushnumber(L, ui->input.keys[0]);
	lua_rawseti(L, -2, 1);
	lua_pushnumber(L, ui->input.keys[1]);
	lua_rawseti(L, -2, 2);
	lua_pushnumber(L, ui->input.keys[2]);
	lua_rawseti(L, -2, 3);
	lua_setfield(L, -2, "keys");
	lua_createtable(L, KP_UI_SLOTS, 0);
	for (k = 0; k < KP_UI_SLOTS; k++) {
		lua_pushnumber(L, (lua_Number)ui->input.mirror[k]);
		lua_rawseti(L, -2, k + 1);
	}
	lua_setfield(L, -2, "mirror");
	return 1;
}

/* --- the canvas --- */

static KpUiCanvas *check_canvas(lua_State *L) { return (KpUiCanvas *)luaL_checkudata(L, 1, UI_CANVAS); }

static KpUi *arg_ui(lua_State *L, int i)
{
	KpUi **p = (KpUi **)luaL_checkudata(L, i, UI_UI);
	if (*p == NULL) luaL_error(L, "ui: freed");
	return *p;
}

static int canvas_new(lua_State *L)
{
	KpUiCanvas *c = (KpUiCanvas *)lua_newuserdata(L, sizeof(KpUiCanvas));
	memset(c, 0, sizeof *c);
	luaL_getmetatable(L, UI_CANVAS);
	lua_setmetatable(L, -2);
	return 1;
}

static int canvas_gc(lua_State *L)
{
	kp_ui_canvas_free(check_canvas(L));
	return 0;
}

static int canvas_resize(lua_State *L)
{
	if (kp_ui_canvas_resize(check_canvas(L), (int32_t)luaL_checknumber(L, 2), (int32_t)luaL_checknumber(L, 3)) != 0) {
		return luaL_error(L, "canvas: cannot make %dx%d", (int)lua_tonumber(L, 2), (int)lua_tonumber(L, 3));
	}
	return 0;
}

static int canvas_begin(lua_State *L)
{
	kp_ui_canvas_begin(check_canvas(L));
	return 0;
}

static int canvas_console(lua_State *L)
{
	KpUiCanvas *c = check_canvas(L);
	kp_ui_canvas_console(arg_ui(L, 2), c, (int32_t)luaL_checknumber(L, 3), (int32_t)luaL_checknumber(L, 4),
		(int32_t)luaL_checknumber(L, 5), (int32_t)luaL_checknumber(L, 6));
	return 0;
}

static int canvas_filler(lua_State *L)
{
	KpUiCanvas *c = check_canvas(L);
	kp_ui_canvas_filler(arg_ui(L, 2), c, (int32_t)luaL_checknumber(L, 3), (int32_t)luaL_checknumber(L, 4),
		(int32_t)luaL_checknumber(L, 5), (int32_t)luaL_checknumber(L, 6));
	return 0;
}

/* c:fill(x, y, w, h, index): an opaque rectangle of the index. */
static int canvas_fill(lua_State *L)
{
	kp_ui_canvas_fill(check_canvas(L), (int32_t)luaL_checknumber(L, 2), (int32_t)luaL_checknumber(L, 3),
		(int32_t)luaL_checknumber(L, 4), (int32_t)luaL_checknumber(L, 5), (uint8_t)luaL_checknumber(L, 6));
	return 0;
}

/* c:margins(ui, dialog, x, y): a fixed screen's margins around its dialog at (x, y) (kp_ui_canvas_margins). */
static int canvas_margins(lua_State *L)
{
	KpUiCanvas *c = check_canvas(L);
	KpUi *ui = arg_ui(L, 2);
	int32_t d = (int32_t)luaL_checknumber(L, 3);
	if (d < 0 || d >= KP_UI_CONTROLS || !ui->controls[d].used) return luaL_error(L, "canvas: no dialog %d", (int)d);
	kp_ui_canvas_margins(ui, c, d, (int32_t)luaL_checknumber(L, 4), (int32_t)luaL_checknumber(L, 5));
	return 0;
}

static int canvas_dialog(lua_State *L)
{
	KpUiCanvas *c = check_canvas(L);
	KpUi *ui = arg_ui(L, 2);
	int32_t d = (int32_t)luaL_checknumber(L, 3);
	if (d < 0 || d >= KP_UI_CONTROLS || !ui->controls[d].used) return luaL_error(L, "canvas: no dialog %d", (int)d);
	kp_ui_canvas_dialog(ui, c, d, (int32_t)luaL_checknumber(L, 4), (int32_t)luaL_checknumber(L, 5));
	return 0;
}

/* c:lines(u, x, y): the interface's text lines at (x, y), the expired ones dropped. */
static int canvas_lines(lua_State *L)
{
	KpUiCanvas *c = check_canvas(L);
	KpUi *ui = arg_ui(L, 2);
	kp_ui_lines_expire(ui);
	kp_ui_canvas_layer(c, kp_ui_lines_layer(ui), (int32_t)luaL_checknumber(L, 3), (int32_t)luaL_checknumber(L, 4));
	return 0;
}

/* c:game_view(u, x, y): the game screen's drag box and command marker for the view at (x, y). */
static int canvas_game_view(lua_State *L)
{
	kp_ui_canvas_game_view(arg_ui(L, 2), check_canvas(L), (int32_t)luaL_checknumber(L, 3), (int32_t)luaL_checknumber(L, 4));
	return 0;
}

/* c:tooltip(u, x, y): the interface's tooltip box at (x, y). */
static int canvas_tooltip(lua_State *L)
{
	kp_ui_canvas_tooltip(arg_ui(L, 2), check_canvas(L), (int32_t)luaL_checknumber(L, 3), (int32_t)luaL_checknumber(L, 4));
	return 0;
}

static int canvas_finish(lua_State *L)
{
	KpUiCanvas *c = check_canvas(L);
	int i;
	lua_pushboolean(L, kp_ui_canvas_end(c));
	for (i = 0; i < 4; i++) lua_pushnumber(L, c->dirty[i]);
	return 5;
}

/* The rectangle as RGBA texels (index, 0, 0, opacity), row by row: the texture format Defold creates at run time. */
static int canvas_rect(lua_State *L)
{
	KpUiCanvas *c = check_canvas(L);
	int32_t x0 = (int32_t)luaL_checknumber(L, 2), y0 = (int32_t)luaL_checknumber(L, 3);
	int32_t x1 = (int32_t)luaL_checknumber(L, 4), y1 = (int32_t)luaL_checknumber(L, 5), x, y;
	size_t n = 4 * (size_t)(x1 - x0) * (size_t)(y1 - y0), o = 0;
	char *out;
	if (x0 < 0 || y0 < 0 || x1 > c->w || y1 > c->h || x0 >= x1 || y0 >= y1) return luaL_error(L, "canvas: bad rectangle");
	out = (char *)lua_newuserdata(L, n);
	for (y = y0; y < y1; y++) {
		const uint8_t *p = c->pixels + 2 * ((size_t)y * (size_t)c->w + (size_t)x0);
		for (x = x0; x < x1; x++, p += 2) {
			out[o++] = (char)p[0];
			out[o++] = 0;
			out[o++] = 0;
			out[o++] = (char)p[1];
		}
	}
	lua_pushlstring(L, out, n);
	return 1;
}

static int canvas_pixel(lua_State *L)
{
	KpUiCanvas *c = check_canvas(L);
	int32_t x = (int32_t)luaL_checknumber(L, 2), y = (int32_t)luaL_checknumber(L, 3);
	const uint8_t *p;
	if (x < 0 || y < 0 || x >= c->w || y >= c->h) return 0;
	p = c->pixels + 2 * ((size_t)y * (size_t)c->w + (size_t)x);
	lua_pushnumber(L, p[0]);
	lua_pushnumber(L, p[1]);
	return 2;
}

static int canvas_index(lua_State *L)
{
	KpUiCanvas *c = check_canvas(L);
	const char *key = luaL_checkstring(L, 2);
	if (strcmp(key, "w") == 0) lua_pushnumber(L, c->w);
	else if (strcmp(key, "h") == 0) lua_pushnumber(L, c->h);
	else if (strcmp(key, "blends") == 0) lua_pushnumber(L, c->blends);
	else lua_getfield(L, lua_upvalueindex(1), key);
	return 1;
}

static const luaL_Reg CANVAS_METHODS[] = {
	{ "resize", canvas_resize }, { "begin", canvas_begin }, { "console", canvas_console }, { "filler", canvas_filler }, { "fill", canvas_fill },
	{ "dialog", canvas_dialog }, { "margins", canvas_margins }, { "lines", canvas_lines }, { "tooltip", canvas_tooltip }, { "game_view", canvas_game_view }, { "finish", canvas_finish }, { "rect", canvas_rect }, { "pixel", canvas_pixel },
	{ NULL, NULL },
};

static const luaL_Reg UI_METHODS[] = {
	{ "font_set", ui_font_set }, { "theme", ui_theme }, { "console", ui_console }, { "console_art", ui_console_art }, { "menu_button", ui_menu_button },
	{ "load", ui_load }, { "init", ui_init }, { "paint", ui_paint }, { "present", ui_present }, { "region", ui_region }, { "game_event", ui_game_event }, { "options", ui_options }, { "portrait_mode", ui_portrait_mode }, { "take_release", ui_take_release }, { "pending_releases", ui_pending_releases }, { "game_loaded", ui_game_loaded }, { "cheats", ui_cheats }, { "input", ui_input }, { "keys", ui_keys }, { "window_key", ui_window_key }, { "window_char", ui_window_char },
	{ "screen", ui_screen }, { "view", ui_view }, { "start_screen", ui_start_screen }, { "invalid_file_name", ui_invalid_file_name }, { "pick", ui_pick }, { "over_console", ui_over_console }, { "take_camera", ui_take_camera }, { "take_commands", ui_take_commands },
	{ "scroll", ui_scroll }, { "edge_scroll", ui_edge_scroll }, { "scroll_state", ui_scroll_state }, { "scroll_speeds", ui_scroll_speeds }, { "take_warp_cursor", ui_take_warp_cursor }, { "tooltip", ui_tooltip },
	{ "sound_device", ui_sound_device }, { "voices", ui_voices }, { "take_sound_calls", ui_take_sound_calls }, { "take_talks", ui_take_talks },
	{ "take_sound_commands", ui_take_sound_commands }, { "cursor", ui_cursor },
	{ "cursor_animate", ui_cursor_animate }, { "cursor_update", ui_cursor_update }, { "cursor_bitmap", ui_cursor_bitmap }, { "cursor_key", ui_cursor_key }, { "game_view_active", ui_game_view_active }, { "game_view_update", ui_game_view_update }, { "capture_changed", ui_capture_changed }, { "active", ui_active }, { "from_save", ui_from_save }, { "player_colours", ui_player_colours }, { "lines", ui_lines }, { "lines_expire", ui_lines_expire }, { "bitmap", ui_bitmap }, { "crc32", ui_crc32 }, { "control", ui_control },
	{ "open", ui_open }, { "set_colours", ui_set_colours }, { "now", ui_now }, { "hud", ui_hud }, { "strings", ui_strings }, { "portraits", ui_portraits },
	{ "resolve_colours", ui_resolve_colours }, { "sounds", ui_sounds }, { "talk", ui_talk },
	{ "portrait_state", ui_portrait_state }, { "minimap", ui_minimap },
	{ "hud_frame", ui_hud_frame }, { "tick", ui_tick }, { "hud_state", ui_hud_state },
	{ "ready_room", ui_ready_room }, { "ready_idle", ui_ready_idle }, { "ready_continue", ui_ready_continue }, { "ready_turn", ui_ready_turn }, { "room_state", ui_room_state },
	{ "take_brief", ui_take_brief }, { "glue_crt", ui_glue_crt }, { "trace", ui_trace }, { "take_trace", ui_take_trace },
	{ "owner_ref", ui_owner_ref }, { "set_ticks", ui_set_ticks }, { "bin_name", ui_bin_name }, { "set_visible", ui_set_visible }, { "video_ticks", ui_video_ticks }, { "page_deadlines", ui_page_deadlines },
	{ "game_effects", ui_game_effects }, { "blend_row", ui_blend_row }, { "glue_palette", ui_glue_palette }, { "game_exit", ui_game_exit }, { "modeless", ui_modeless },
	{ "show_result", ui_show_result }, { "end_game", ui_end_game }, { "score", ui_score }, { "score_game", ui_score_game },
	{ "score_state", ui_score_state }, { "menus_enter", ui_menus_enter }, { "menus_poll", ui_menus_poll },
	{ "profiles", ui_profiles }, { "campaign", ui_campaign }, { "saves", ui_saves }, { "take_save_request", ui_take_save_request }, { "invalidate_all", ui_invalidate_all },
	{ "timer_order", ui_timer_order }, { "time_seeds", ui_time_seeds },
	{ NULL, NULL },
};

/* --- the Smacker decoder --- */

static KpSmk *check_smk(lua_State *L) { return (KpSmk *)luaL_checkudata(L, 1, UI_SMK); }

static int smk_new(lua_State *L)
{
	size_t n;
	const char *d = luaL_checklstring(L, 1, &n);
	KpSmk *s = (KpSmk *)lua_newuserdata(L, sizeof(KpSmk));
	memset(s, 0, sizeof *s);
	luaL_getmetatable(L, UI_SMK);
	lua_setmetatable(L, -2);
	lua_newtable(L);
	lua_pushvalue(L, 1);
	lua_setfield(L, -2, "data");
	lua_setfenv(L, -2);
	if (kp_smk_open(s, (const uint8_t *)d, n) != 0) return luaL_error(L, "smk: malformed");
	return 1;
}

static int smk_gc(lua_State *L)
{
	kp_smk_close(check_smk(L));
	return 0;
}

static int smk_decode(lua_State *L)
{
	lua_pushboolean(L, kp_smk_decode(check_smk(L)) == 0);
	return 1;
}

static int smk_pixels(lua_State *L)
{
	KpSmk *s = check_smk(L);
	lua_pushlstring(L, (const char *)s->pixels, (size_t)s->width * s->height);
	return 1;
}

static int smk_index(lua_State *L)
{
	KpSmk *s = check_smk(L);
	const char *k = luaL_checkstring(L, 2);
	if (strcmp(k, "w") == 0) lua_pushnumber(L, s->width);
	else if (strcmp(k, "h") == 0) lua_pushnumber(L, s->height);
	else if (strcmp(k, "frames") == 0) lua_pushnumber(L, s->frames);
	else if (strcmp(k, "frame") == 0) lua_pushnumber(L, s->frame);
	else if (strcmp(k, "frame_time") == 0) lua_pushnumber(L, kp_smk_frame_time(s));
	else {
		lua_pushvalue(L, lua_upvalueindex(1));
		lua_getfield(L, -1, k);
	}
	return 1;
}

static const luaL_Reg SMK_METHODS[] = { { "decode", smk_decode }, { "pixels", smk_pixels }, { NULL, NULL } };

void kp_ui_lua_push(lua_State *L)
{
	luaL_newmetatable(L, UI_SMK);
	lua_newtable(L);
	luaL_register(L, NULL, SMK_METHODS);
	lua_pushcclosure(L, smk_index, 1);
	lua_setfield(L, -2, "__index");
	lua_pushcfunction(L, smk_gc);
	lua_setfield(L, -2, "__gc");
	lua_pop(L, 1);
	luaL_newmetatable(L, UI_CANVAS);
	lua_newtable(L);
	luaL_register(L, NULL, CANVAS_METHODS);
	lua_pushcclosure(L, canvas_index, 1);
	lua_setfield(L, -2, "__index");
	lua_pushcfunction(L, canvas_gc);
	lua_setfield(L, -2, "__gc");
	lua_pop(L, 1);
	luaL_newmetatable(L, UI_UI);
	lua_newtable(L);
	luaL_register(L, NULL, UI_METHODS);
	lua_setfield(L, -2, "__index");
	lua_pushcfunction(L, ui_gc);
	lua_setfield(L, -2, "__gc");
	lua_pop(L, 1);
	luaL_newmetatable(L, UI_SURFACE);
	lua_newtable(L);
	luaL_register(L, NULL, SURFACE_METHODS);
	lua_pushcclosure(L, surface_index, 1);
	lua_setfield(L, -2, "__index");
	lua_pop(L, 1);
	luaL_newmetatable(L, UI_TEXT);
	lua_newtable(L);
	luaL_register(L, NULL, TEXT_METHODS);
	lua_setfield(L, -2, "__index");
	lua_pop(L, 1);
	luaL_newmetatable(L, UI_TIMERS);
	lua_newtable(L);
	luaL_register(L, NULL, TIMERS_METHODS);
	lua_setfield(L, -2, "__index");
	lua_pop(L, 1);
	lua_newtable(L);
	lua_pushcfunction(L, timers_new);
	lua_setfield(L, -2, "timers");
	lua_pushcfunction(L, surface_new);
	lua_setfield(L, -2, "surface");
	lua_pushcfunction(L, text_new);
	lua_setfield(L, -2, "text");
	lua_pushcfunction(L, ui_new);
	lua_setfield(L, -2, "new");
	lua_pushcfunction(L, canvas_new);
	lua_setfield(L, -2, "canvas");
	lua_pushcfunction(L, smk_new);
	lua_setfield(L, -2, "smk");
}
