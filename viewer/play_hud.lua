-- The viewer's play mode with the original's interface (plan phase 9, ADR 0011): kp_ui runs the local player's console
-- over the game (the resources, the status panel, the portrait, the minimap and the menu button), repaints the invalid
-- region as the original's PresentFrame does, presentation/ui/canvas.lua composes the open dialogs by the layout of
-- the logical screen and the render script writes the canvas over the world.
local loader = require("game.data.loader")
local kpf = require("game.data.kpf")
local canvas = require("presentation.ui.canvas")
local layout = require("presentation.ui.layout")
local colours = require("presentation.ui.colours")
local report = require("spikes.common.report")

local M = {}

local RACE_GRPS = { [0] = "dlgs\\zerg.grp", [1] = "dlgs\\terran.grp", [2] = "dlgs\\protoss.grp" }

-- The window procedure's event types (kp_ui.h) and the mouse state's button bits (0x6d315c).
M.MOUSE_MOVE, M.LEFT_DOWN, M.LEFT_UP, M.RIGHT_DOWN, M.RIGHT_UP, M.CHAR = 3, 4, 5, 7, 8, 15
local BUTTON_BITS = { [4] = 2, [7] = 8 }

local Hud = {}
Hud.__index = Hud

-- The interface's data files, read once (each game's console of the plain viewer makes an interface of its own).
local data = {}
local function read(path)
	if not data[path] then data[path] = loader.read(path) end
	return data[path]
end

-- A new interface (kp_core.ui.new) with the data the console and the glue read: strings, portraits, sounds, the
-- cheats and the campaign's tables.
function M.interface(core)
	local ui = core.ui.new(read("/generated/data/ui.kpd"), read("/generated/data/fonts.kpd"))
	if not data.campaign then data.campaign = kpf.parse(read("/generated/data/campaign.kpd"), "campaign.kpd") end
	ui:strings(read("/generated/data/strings.kpd"))
	ui:portraits(read("/generated/data/portraits.kpd"))
	ui:sounds(read("/generated/data/sounds.kpd"))
	ui:cheats(data.campaign:section("CHEA"))
	ui:campaign(data.campaign:section("TABL"))
	return ui
end

-- core: the kp_core module; game: the started native game (its local player's race picks the console); map: the
-- mission's tileset for the interface's colours and the minimap ({ tileset = parsed tileset, data = its tileset.kpd
-- bytes, palettes = game/data/palettes.lua parsed, index = the tileset's index, colors = the mission's colour
-- slots or nil }); now_ms: the interface clock at the console's creation; shared (optional): { ui =, canvas = } of
-- the glue (viewer/play_glue.lua), whose interface runs the console too and keeps its canvas; screen (optional): the
-- logical screen { w, h } for the game's first view (the start location's, before the game's first step); from_save:
-- the game is a saved game's (no tips at its start, WasLoadedFromSave 0x499870).
function M.new(core, game, map, now_ms, shared, screen, from_save)
	local ui = shared and shared.ui or M.interface(core)
	if from_save then ui:from_save() end
	local race = game.players[game.local_player].race
	if not RACE_GRPS[race] then race = 1 end
	colours.apply(ui, map.tileset)
	colours.minimap(ui, map.data, map.tileset, map.palettes, map.index, map.colors)
	ui:player_colours(map.palettes.tunit) -- the placement ghost's colours, by owner as the world's images (world_lookups)
	ui:font_set(true)
	ui:theme(RACE_GRPS[race])
	ui:now(now_ms)
	ui:hud(game)
	local start
	if screen then
		ui:view(M.view_size(screen[1], screen[2]))
		ui:start_screen() -- HandleStartLocationRecord 0x46a280, InitScreenPosition 0x445810
		local x, y = ui:take_camera()
		if x then start = { x, y } end
	end
	return setmetatable({ core = core, ui = ui, canvas = shared and shared.canvas or canvas.new(core, ui), buttons = 0,
		shared = shared ~= nil, start = start }, Hud)
end

-- A game event (game/simulation/events.lua) for the interface: the text lines, the minimap's pings.
function Hud:event(kind, a, b, c, text)
	self.ui:game_event(kind, a, b, c, text)
end

local FLAG_BACKDROP = 0x10000000 -- a dialog composited onto the backdrop (the in-game menus): under the console

-- The text lines' layer under the open dialogs in drawing order (bottom first) with their original rectangles; a dialog
-- composited onto the backdrop is marked so (the console's art and dialogs paint over it).
function Hud:panels()
	local open, out = self.ui:open(), { { lines = true, bin = "lines", rect = { 0, 0, 639, 479 } } }
	for i = #open, 1, -1 do
		local k = self.ui:control(open[i])
		out[#out + 1] = { dialog = open[i], bin = k.bin_name, rect = { k.left, k.top, k.right, k.bottom },
			backdrop = bit.band(k.flags, FLAG_BACKDROP) ~= 0 }
	end
	return out
end

-- The game loop's scroll step runs when more than this many milliseconds passed (GameLoop 0x4451f0).
local SCROLL_MS = 10
-- The original's game view is its screen less the console's 80 rows (0x666430: 640 x 400); a wider logical screen's
-- view keeps that height and widens (plan section 2a).
M.CONSOLE_ROWS = 80

-- The game view of a w x h logical screen.
function M.view_size(w, h) return w, h - M.CONSOLE_ROWS end

-- Each viewer frame: the interface's timers at now_ms, its work after each of the frames game steps, the game loop's
-- cursor update and map scroll from the camera's (cam_x, cam_y) with the system cursor at the logical screen's (cx,
-- cy) (the original's screen edges are the logical screen's), the placement's preview at the mouse, the repaint of
-- the invalid region, then the canvas composed for the logical screen w x h
-- and what changed uploaded. Returns the view the interface moved to (x, y or nil) and whether a trigger wait ends.
function Hud:frame(w, h, now_ms, steps, cx, cy, cam_x, cam_y)
	local ui = self.ui
	ui:now(now_ms)
	ui:tick(now_ms)
	ui:view(M.view_size(w, h))
	for _ = 1, steps do ui:hud_frame() end
	-- the start location's view replaces the viewer's camera once (the viewer's is still the previous game's)
	local start = self.start
	if start then cam_x, cam_y, self.start = start[1], start[2], nil end
	if cam_x then ui:screen(cam_x, cam_y) end -- the viewer's camera, which the interface's moves change
	if cx then
		local ox, oy = math.floor(cx * layout.MIN_W / w), math.floor(cy * layout.MIN_H / h)
		ui:cursor_update(ox, oy)
		if now_ms - (self.scrolled_ms or 0) > SCROLL_MS then
			self.scrolled_ms = now_ms
			ui:scroll(ox, oy)
		end
	end
	ui:game_view_update() -- UpdateGameUi 0x445020's preview at the mouse, then the draw's end of a box with no button down
	-- the canvas is composed again only when something on it changed: a repaint, the text lines, the open dialogs, the
	-- screen's size (the tooltip's moves repaint the dialogs under it), the game view's drag box and marker while they
	-- show and once more when they went
	local draws, lines = ui:present(), ui:lines_expire()
	local key = ("%dx%d %s"):format(w, h, table.concat(ui:open(), " "))
	local game_view = ui:game_view_active()
	if draws > 0 or lines or key ~= self.composed or game_view or self.game_view then
		self.game_view = game_view
		self.composed = key
		local x0, y0, x1, y1, arrangement = self.canvas:compose(w, h, self:panels())
		if x0 and (w ~= self.w or h ~= self.h) then
			self.w, self.h = w, h
			report.line(("hud: %dx%d %s layout, canvas rectangle %d,%d-%d,%d"):format(w, h, arrangement.name, x0, y0, x1, y1))
		end
		self.arrangement = arrangement
		self.canvas:upload(x0, y0, x1, y1)
	end
	local vx, vy = ui:take_camera()
	if start and not vx then vx, vy = start[1], start[2] end
	return vx, vy, ui:take_release()
end

-- A key of the window (down, its virtual-key code, a repeat) and the characters of a text event, through the
-- interface's accelerators (kp_ui_keys.c).
-- the keys whose WM_KEYDOWN TranslateMessage follows with a WM_CHAR of their own code (Defold's text has none)
local CHAR_KEYS = { [0x08] = true, [0x09] = true, [0x0d] = true, [0x1b] = true }

function M.key(ui, down, vk, repeating)
	-- no character after an accelerator's key or a key a dialog closed on (the pump skips TranslateMessage)
	if ui:window_key(down, vk, repeating) and down and CHAR_KEYS[vk] then ui:window_char(vk, repeating) end
end

-- Defold's text is UTF-8; the window's characters are single bytes (Latin-1's code points, the code page's for the
-- original): a character past 255 has none and is left out, and so is a control character: the browser's text has
-- Enter's carriage return too, whose WM_CHAR the key itself gives (M.key, TranslateMessage), and a second one closed
-- the message line it had just opened.
function M.text(ui, text)
	local i, n = 1, #text
	while i <= n do
		local b, cp, len = text:byte(i), nil, 1
		if b < 0x80 then
			cp = b
		elseif b >= 0xc0 and b < 0xe0 and i + 1 <= n then
			cp, len = (b - 0xc0) * 64 + (text:byte(i + 1) % 64), 2
		else
			len = b >= 0xf0 and 4 or b >= 0xe0 and 3 or 1
		end
		if cp and cp >= 32 and cp < 256 then ui:window_char(cp) end
		i = i + len
	end
end

-- The mouse state's button bits (0x6d315c) after a window event.
function M.buttons(buttons, event)
	if BUTTON_BITS[event] then buttons = bit.bor(buttons, BUTTON_BITS[event]) end
	if event == M.LEFT_UP then buttons = bit.band(buttons, bit.bnot(2)) end
	if event == M.RIGHT_UP then buttons = bit.band(buttons, bit.bnot(8)) end
	return buttons
end

-- Returns the commands the key or text queued and the view it moved to.
function Hud:key(down, vk, repeating)
	M.key(self.ui, down, vk, repeating)
	return self.ui:take_commands(), self.ui:take_camera()
end

function Hud:text(text)
	M.text(self.ui, text)
	return self.ui:take_commands(), self.ui:take_camera()
end

-- The topmost open dialog the arrangement placed under the logical point (lx, ly), or nil (ui:open() is topmost
-- first, as the interface's dispatch walks them).
local function panel_at(ui, a, lx, ly)
	for _, d in ipairs(ui:open()) do
		local place = a.places[d]
		local k = place and ui:control(d)
		if k and lx >= place.x and lx <= place.x + k.right - k.left and ly >= place.y and ly <= place.y + k.bottom - k.top then
			return d
		end
	end
	return nil
end

local function is_open(ui, d)
	for _, o in ipairs(ui:open()) do
		if o == d then return true end
	end
	return false
end

-- The canvas's opacity of the console art and the panels; the text lines and the tooltip over the game view have
-- KP_UI_CANVAS_OVERLAY_A (kp_ui.h), so a click on them reaches the game (IsPointOverConsole 0x43e890: the art only).
local CONSOLE_A = 255

-- An input event at the logical screen's point (lx, ly; top-left origin) with the view's top-left at map (cam_x,
-- cam_y): a point on a panel goes to it in the original's coordinates (ADR 0011: the panels keep their content, the
-- layout moves them); a point on the console art elsewhere is over the console; the rest is the game view. A button's
-- press picks the panel (or the game view) the gesture keeps until the buttons are up: a drag the minimap or a pressed
-- button holds goes on in that panel's coordinates off it, a drag box over the panels in the view's (the original's one
-- screen gives a gesture one frame of coordinates). Returns the commands the interface queued and the view it moved
-- to (x, y or nil).
function Hud:input(event, lx, ly, cam_x, cam_y, shift, ctrl, alt)
	local ui, a = self.ui, self.arrangement
	local x, y, over = lx, ly, false
	if a then
		local d = self.gesture
		if d == nil or (d and not (a.places[d] and is_open(ui, d))) then d = panel_at(ui, a, lx, ly) end
		if self.buttons == 0 and (event == M.LEFT_DOWN or event == M.RIGHT_DOWN) then self.gesture = d or false end
		if d then
			local k, place = ui:control(d), a.places[d]
			x, y, over = k.left + lx - place.x, k.top + ly - place.y, nil
		elseif lx >= 0 and ly >= 0 and lx < self.canvas.w and ly < self.canvas.h then
			local _, alpha = self.canvas.canvas:pixel(lx, ly)
			over = alpha == CONSOLE_A
		end
	end
	self.buttons = M.buttons(self.buttons, event)
	if self.buttons == 0 then self.gesture = nil end
	ui:over_console(over)
	local held = ui:hud_state().keys -- the key bytes the window's keys keep, unless the caller says otherwise
	if shift ~= nil then held = { shift and 1 or 0, ctrl and 1 or 0, alt and 1 or 0 } end
	ui:keys(held[1], held[2], held[3], self.buttons)
	ui:screen(cam_x, cam_y)
	ui:input(event, x, y)
	return ui:take_commands(), ui:take_camera()
end

-- The interface's pending ends of the game's trigger waits as a save's milliseconds left (game/save.lua PRES): the
-- trigger portrait's time, a camera pan's arrival (released at once after a load: its target's view is the saved one).
function Hud:pending_releases()
	local portrait_ms, camera = self.ui:pending_releases()
	return { portrait_ms = portrait_ms, camera_ms = camera and 0 or nil }
end

-- A load replaced the game: the pan and the trigger portrait of the game before end without releasing its waits, and
-- the placement and the targeting end (ResetPerGameState 0x444230).
function Hud:game_loaded() self.ui:game_loaded() end

-- The window lost the mouse (its focus went) with a button down: no release comes (OnCaptureChanged 0x43f450).
function Hud:capture_lost()
	self.buttons, self.gesture = 0, nil
	self.ui:capture_changed()
end

function Hud:release()
	if not self.shared then self.canvas:release() end
end

return M
