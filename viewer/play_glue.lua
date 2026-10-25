-- The viewer's glue screens (plan phase 9E E6): one interface (kp_ui) runs the original's menus (kp_ui_menus.c: the
-- main menu, the game mode, the login with the profile, the campaign menus with the mission history, the load
-- screen), the ready room of each game the viewer starts and the score screen after it; the console of the game is
-- the same interface's (viewer/play_hud.lua with `shared`). The screen's dialog, its popups composited, is drawn at
-- the logical screen's centre through the canvas with the glue's palette.
local play_hud = require("viewer.play_hud")
local canvas = require("presentation.ui.canvas")
local layout = require("presentation.ui.layout")

local M = {}

-- menus_poll's actions (kp_ui.h KP_UI_MENU_*) and the glue states the viewer enters.
local ACTION_EXIT, ACTION_MISSION, ACTION_LOAD_SAVE = 2, 5, 7
local IDLE = 13 -- the window procedure's idle event type (kp_ui.h KP_UI_IDLE)
M.MAIN, M.CAMPAIGN, M.EXPANSION_CAMPAIGN = 0, 6, 22
-- a ready room's glue state -> its race for u:ready_room (Terran, Zerg, Protoss)
M.ROOM_RACES = { [7] = 1, [8] = 0, [9] = 2 }
local RESULT_START = 101 -- the ready room's Start once the turn steps confirmed it (kp_ui_briefing.c)
local DEFAULT_NAME = "Player"

local Glue = {}
Glue.__index = Glue

-- The profile (game/campaign.lua, with the login's name) as the interface's character list.
local function characters(profile)
	if not profile.name then return {} end
	local o, x = profile.original, profile.expansion
	return { { name = profile.name, progress = { o[0] or 0, o[1] or 0, o[2] or 0, x[0] or 0, x[1] or 0, x[2] or 0 } } }
end

-- core: the kp_core module; profile: the viewer's profile (its name and progress); now_ms: the interface's clock.
-- The glue starts at the main menu.
function M.new(core, profile, now_ms)
	local ui = play_hud.interface(core)
	ui:profiles(characters(profile))
	ui:now(now_ms)
	local self = setmetatable({ core = core, ui = ui, canvas = canvas.new(core, ui), profile = profile, buttons = 0,
		place = { x = 0, y = 0 } }, Glue)
	self:menus(M.MAIN)
	return self
end

-- The glue state's screen (kp_ui_menus_enter).
function Glue:menus(state)
	self.ui:profiles(characters(self.profile))
	self.ui:menus_enter(state)
	self.screen, self.composed = "menus", nil -- the shared canvas may hold a game's frame
end

-- The ready room of the started game (its glue state's race); the game waits for the room's Start.
function Glue:room(game, state)
	self.ui:ready_room(game, M.ROOM_RACES[state] or 2)
	self.screen, self.room_opened, self.composed = "room", false, nil
end

-- The score screen of the finished game (result: 2 defeat, 3 victory), the player's name and the game's seconds.
function Glue:score(game, result, elapsed)
	self.ui:score_game(game, result, self.profile.name or DEFAULT_NAME, elapsed)
	self.screen, self.composed = "score", nil
end

-- The profile's progress the menus raised (an episode's start), merged into the viewer's profile; the login's name.
local function take_profile(self, player)
	if player and player ~= "" then self.profile.name = player end
	for _, c in ipairs(self.ui:profiles()) do
		if c.name == self.profile.name then
			local o, x = self.profile.original, self.profile.expansion
			for race = 0, 2 do
				o[race] = math.max(o[race] or 0, c.progress[race + 1])
				x[race] = math.max(x[race] or 0, c.progress[race + 4])
			end
		end
	end
end

-- What the screen on show ended with: { mission = key, state = the room's glue state, expansion = } (a game to start
-- and its room), { load = index } (the load screen's saved game, 0-based in the list of Glue:saves), { room = "start"
-- | "cancel" }, { score = true } (the score screen closed) or { exit = true }; nil while it goes on.
local function poll(self)
	local ui = self.ui
	if self.screen == "menus" then
		local p = ui:menus_poll()
		if not p then return nil end
		take_profile(self, p.player)
		if p.action == ACTION_MISSION and M.ROOM_RACES[p.state] then
			self.screen = nil
			return { mission = p.mission, state = p.state, expansion = p.expansion, profile_changed = true }
		elseif p.action == ACTION_EXIT then
			self.screen = nil
			return { exit = true }
		elseif p.action == ACTION_LOAD_SAVE then -- the load screen's saved game (RunLoadGameScreen 0x459970)
			self.screen = nil
			return { load = p.save, profile_changed = true }
		end
		-- the next state's screen (a cinematic row is skipped: no SMK cinematics in V1, plan phase 9)
		ui:menus_enter(p.state)
		return { profile_changed = true }
	elseif self.screen == "room" then
		ui:ready_continue()
		ui:ready_idle()
		local st = ui:room_state()
		if st.open then
			self.room_opened = true
		elseif self.room_opened then
			self.screen = nil
			return { room = st.result == RESULT_START and "start" or "cancel" }
		end
	elseif self.screen == "score" then
		local st = ui:score_state()
		local open = false
		for _, d in ipairs(ui:open()) do open = open or d == st.dialog end
		if st.answered and not open then
			self.screen = nil
			return { score = true }
		end
	end
	return nil
end

-- Each viewer frame while a glue screen shows: the interface's timers at now_ms, the screen's work, the repaint and
-- the screen's dialog composed at the centre of the w x h logical screen. Returns what the screen ended with (poll)
-- and the glue's palette (768 bytes) when it changed.
function Glue:frame(w, h, now_ms)
	local ui = self.ui
	ui:now(now_ms)
	ui:tick(now_ms)
	ui:input(IDLE) -- the message pump's idle event (0x4401c0): a hotkey's press completes, the credits' pages turn
	local event = poll(self)
	local draws, open = ui:present(), ui:open()
	local rgb, serial = ui:glue_palette()
	self.place = { x = math.floor((w - layout.MIN_W) / 2), y = math.floor((h - layout.MIN_H) / 2) }
	-- composed again only when the screen changed: a repaint, another dialog, the size, the palette (the margins' shade)
	local key = ("%dx%d %s %s"):format(w, h, tostring(open[#open]), tostring(serial))
	if draws > 0 or key ~= self.composed then
		self.composed = key
		self.canvas:upload(self.canvas:compose_screen(w, h, open[#open], self.place.x, self.place.y))
	end
	if serial ~= self.palette_serial then
		self.palette_serial = serial
		return event, rgb
	end
	return event, nil
end

-- The palette goes up again with the next frame (another palette replaced it on the screen).
function Glue:invalidate_palette() self.palette_serial = nil end

-- An input event at the logical screen's point (lx, ly): the screen's dialog has the original's coordinates.
function Glue:input(event, lx, ly)
	self.buttons = play_hud.buttons(self.buttons, event)
	local held = self.ui:hud_state().keys
	self.ui:keys(held[1], held[2], held[3], self.buttons)
	self.ui:input(event, lx - self.place.x, ly - self.place.y)
end

-- The player's saved games the load screen lists (newest last).
function Glue:saves(names) self.ui:saves(names) end

function Glue:key(down, vk, repeating) play_hud.key(self.ui, down, vk, repeating) end
function Glue:text(text) play_hud.text(self.ui, text) end

-- The window lost the mouse with a button down (Hud:capture_lost, the same interface's).
function Glue:capture_lost()
	self.buttons = 0
	self.ui:capture_changed()
end

function Glue:release() self.canvas:release() end

return M
