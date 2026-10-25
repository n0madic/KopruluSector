-- The console's panels over a replayed game (plan phase 9B, the panel oracle of docs/phase9.md): a scenario golden's
-- game runs in kp_core with kp_ui's console over it, and the interface's work runs where the emulator's input log
-- says the original did it (tools/hud_log.py's entries: the UI timer passes at their ticks, each UpdateStatusUi
-- 0x4a7ee0 and PresentFrame 0x4c5360, the local selections as the interface set them and the talking portraits of
-- the unit responses, phase 9D's, given here so that the portrait is checked on its own); the game's events go to
-- the interface after each object step. At each wanted frame the interface is handed to on_frame at the first
-- present of that frame, where the emulator took its screen capture.
local oracle = require("tools.lua.oracle")
local terrain = require("game.data.terrain")
local mission_data = require("game.data.mission")
local colours = require("presentation.ui.colours")
local palettes = require("game.data.palettes")

local M = {}

local RACE_THEMES = { [0] = "dlgs\\zerg.grp", [1] = "dlgs\\terran.grp", [2] = "dlgs\\protoss.grp" }
local PASS, PRESENT, HUD, TALK, SELECT, GAME_CALL, CURSOR, INPUT, COMMAND, CURSOR_CHOICE, SCROLL, TIP, SOUND = 1, 2, 3, 4, 5,
	6, 7, 8, 9, 10, 11, 12, 13
-- (tools/hud_log.py)
local CALLS = { "paused", "release", "skip", "onscreen", "critter", "menu_pause" }
local IDLE_EVENT = 13 -- an input entry's type (kp_ui.h KP_UI_IDLE)

-- tools/hud_log.py's to_text lines as { create =, create_ticks =, entries = }.
function M.parse_text(text)
	local hud = { create_ticks = {}, entries = {} }
	for line in text:gmatch("[^\n]+") do
		local create = line:match("^create (%d+)$")
		local cheats = line:match("^cheats (%d+)$")
		local bin, tick = line:match("^tick (%S+) (%d+)$")
		if cheats then
			hud.cheats = tonumber(cheats)
		elseif create then
			hud.create = tonumber(create)
		elseif bin then
			hud.create_ticks[bin] = tonumber(tick)
		else
			local e = {}
			for n in line:gmatch("%-?%d+") do e[#e + 1] = tonumber(n) end
			hud.entries[#hud.entries + 1] = e
		end
	end
	return hud
end

-- opts: core (kp_core), read (path -> bytes), golden (a scenario golden), hud ({ create =, create_ticks =, entries = }),
-- frames (ascending), on_frame(frame, ui, game), and optionally on_entry(entry, ui, game) before each entry (a trace),
-- on_present(tick, ui, game) after each repaint of the log (PresentFrame 0x4c5360),
-- input = true (the interface takes the logged input events itself: its local selections are checked against the
-- log's with on_select(frame, logged units, game) instead of applied (those right after an input event), and the commands it queues go to
-- on_command(frame, bytes, game); with the log's scroll records the interface scrolls the view itself, its state checked
-- with on_scroll(frame, state, entry), and the input events' views are checked with on_screen(frame, x, y, entry)
-- instead of set; each release of the trigger wait the log has is checked with on_release(frame, the interface's
-- releases since the last); with the log's sound records the interface's sound calls and talking requests are checked
-- in order with on_sound(frame, call, entry) and on_talk(frame, talk, entry), the unmatched ones at the end with
-- on_sound_end(sounds wanted, sounds made, talks wanted, talks made); the tooltip the original left after each run of its tooltip records is checked with
-- on_tip(frame, the interface's {x, y, w, h} or nil, the original's {left, top, right, bottom} or nil)), commands and ui (the run's own commands.jsonl and
-- ui.jsonl texts, which a newer run has: its screen's draws, which older goldens lack).
function M.run(opts)
	local read, golden, hud = opts.read, opts.golden, opts.hud
	-- The interface's local selections and its game calls (tools/hud_log.py) are the entries' (at their place among
	-- the presents); the run's interface events have the same ones, which are left out so that each runs once.
	local placed = {}
	local function count(key) placed[key] = (placed[key] or 0) + 1 end
	for _, e in ipairs(hud.entries) do
		if e[2] == SELECT then count(e[1] .. " select " .. table.concat(e, ",", 3)) end
		if e[2] == GAME_CALL then count(e[1] .. " " .. CALLS[e[3]] .. " " .. table.concat(e, ",", 4)) end
	end
	local function key_of(e)
		if e.select then return e.frame .. " select " .. table.concat(e.select, ",") end
		if e.paused then return e.frame .. " paused " end
		if e.release then return e.frame .. " release " end
		if e.skip then return e.frame .. " skip " end
		if e.critter then return e.frame .. " critter " .. e.critter end
		if e.menu_pause then return e.frame .. " menu_pause " .. e.menu_pause end
		if e.onscreen then return e.frame .. " onscreen " .. table.concat(e.onscreen, ",") end
	end
	-- With the log's sound records the interface plays the sounds itself: the voices' CRT draws and the talking
	-- requests are its own, checked against the log's (on_sound, on_talk) instead of applied
	local voices
	for _, e in ipairs(hud.entries) do
		if e[2] == SOUND then voices = e break end
	end
	voices = opts.input and voices
	local events = {}
	for _, e in ipairs(oracle.expand_ui(opts.ui and oracle.parse_ui(opts.ui) or golden.ui or {})) do
		local key = key_of(e)
		if opts.input and (e.mark or e.placement or e.cursor or e.menu_pause) then -- the interface's own clicks mark
			-- and place, its modeless dialogs pause
		elseif voices and (e.crt or e.critter) then -- the voices' choices and a critter's death of them
		elseif key and (placed[key] or 0) > 0 then
			placed[key] = placed[key] - 1
		else
			events[#events + 1] = e
		end
	end
	local game = oracle.start_mission(read, "generated/" .. golden.mission .. ".kpm", golden.seed, golden.expansion,
		{ events = true, cheats = hud.cheats })
	local run = oracle.runner(game, opts.commands and oracle.parse_commands(opts.commands)
		or oracle.golden_commands(golden.commands), events)
	local ui = opts.core.ui.new(read("generated/data/ui.kpd"), read("generated/data/fonts.kpd"))
	local race = game.players[game.local_player].race
	ui:strings(read("generated/data/strings.kpd"))
	ui:portraits(read("generated/data/portraits.kpd"))
	ui:sounds(read("generated/data/sounds.kpd"))
	ui:cheats(require("game.data.kpf").parse(read("generated/data/campaign.kpd"), "campaign.kpd"):section("CHEA"))
	do
		local path = "generated/" .. golden.mission .. ".kpm"
		local mission = mission_data.parse(read(path), path)
		local name = terrain.TILESETS[mission.tileset]
		local data = read("generated/data/tilesets/" .. name .. ".kpd")
		local tileset = terrain.parse_tileset(data, name)
		colours.apply(ui, tileset)
		colours.minimap(ui, data, tileset, palettes.parse(read("generated/data/palettes.kpd"), "palettes.kpd"),
			mission.tileset, mission.colors)
	end
	ui:font_set(true)
	ui:theme(RACE_THEMES[race] or RACE_THEMES[1])
	ui:now(hud.create)
	ui:hud(game, hud.create_ticks)

	-- The original's game loop: the interface events, the turn and the frame count, the object step, then until the
	-- next count its idle loop: UpdateStatusUi, PresentFrame and the message loop's timer passes, all stamped with
	-- the counter of that time; so after object step F the entries stamped F (and any before) run.
	if voices then ui:sound_device(voices[9] == 1, voices[10]) else ui:voices(false) end
	-- the interface's sounds and talks against the log's, in order (the game's sounds reach the interface after the
	-- game's step, where the log has them inside it)
	local sounds_want, sounds_have, talks_want, talks_have = {}, {}, {}, {}
	local function match(want, have, check)
		while #want > 0 and #have > 0 do
			local w, h = table.remove(want, 1), table.remove(have, 1)
			if check then check(w.frame, h, w.entry) end
		end
	end
	local function collect(frame)
		if not voices then return end
		for _, c in ipairs(ui:take_sound_calls()) do sounds_have[#sounds_have + 1] = c end
		for _, t in ipairs(ui:take_talks()) do talks_have[#talks_have + 1] = t end
		match(sounds_want, sounds_have, opts.on_sound)
		match(talks_want, talks_have, opts.on_talk)
	end
	local entries, frames = hud.entries, opts.frames
	local next_entry, next_want = 1, 1
	local scrolled -- a scroll record was seen: from then on the interface's view is its own
	local scrolled_early -- the scroll record already run before the release it made
	local interface_select -- the last selection entry was the interface's
	local function scroll(e)
		ui:scroll_speeds(e[11], e[12])
		if not scrolled then
			scrolled = true
			ui:screen(e[3], e[4]) -- the view before the interface's own
			return
		end
		-- the record's cursor is where the system cursor was (GetCursorPos: input, not the interface's); the game loop's
		-- UpdateCursor 0x43ea40 looked at it just before (its choice is CURSOR_CHOICE's)
		ui:edge_scroll(e[9], e[10])
		ui:scroll(e[9], e[10])
		if opts.on_scroll then opts.on_scroll(e[1], ui:scroll_state(), e) end
	end
	local tip_want -- the tooltip the original left, to check after the event the records belong to
	local function check_tip()
		local _, x, y, w, h = ui:tooltip()
		opts.on_tip(tip_want.frame, x and { x, y, w, h }, tip_want.rect)
		tip_want = nil
	end
	local function drain()
		for _, e in ipairs(oracle.events(game)) do ui:game_event(e.kind, e.a, e.b, e.c, e.text) end
	end
	run.after_objects = function()
		if voices then
			-- the step's sounds start when the original ran the step: the tick of the step's first sound
			for k = next_entry, #entries do
				local e = entries[k]
				if e[1] > game.frame then break end
				if e[2] == SOUND then
					ui:now(e[11])
					break
				end
			end
		end
		drain()
		-- the commands the game's own work makes the interface send (a unit out of the selection, KP_EVENT_SELECTION_SENT)
		if opts.input then
			for _, c in ipairs(ui:take_commands()) do opts.on_command(game.frame, c, game) end
		end
		while entries[next_entry] and entries[next_entry][1] <= game.frame do
			local e = entries[next_entry]
			local kind = e[2]
			if opts.on_entry then opts.on_entry(e, ui, game) end
			if kind == PASS then
				ui:tick(e[3])
			elseif kind == HUD then
				for _ = 1, e[3] do ui:hud_frame() end
			elseif kind == PRESENT then
				-- the screen's draw refreshes the images' bounds the clicks test (RecomputeAllSpriteBounds 0x415700)
				if opts.input then game.img:refresh_bounds() end
				ui:now(e[3])
				ui:present()
				if opts.on_present then opts.on_present(e[3], ui, game) end
				if frames[next_want] and e[1] >= frames[next_want] then
					opts.on_frame(frames[next_want], ui, game)
					next_want = next_want + 1
				end
			elseif kind == TALK and voices then
				talks_want[#talks_want + 1] = { frame = e[1], entry = e }
			elseif kind == SOUND and voices then
				sounds_want[#sounds_want + 1] = { frame = e[1], entry = e }
			elseif kind == TALK then
				ui:now(e[8])
				ui:talk(e[3], e[4], e[5], e[6] ~= 0, e[7])
			elseif kind == SELECT and opts.input then
				-- a selection right after an input event (not the idle event, whose handler selects nothing) or a
				-- command is the interface's, and so are the ones right after it; the game's own (a unit that boards,
				-- dies) happen inside its step, where the check cannot see them one by one
				local back = next_entry - 1 -- past the cursor's redraws, which the handler's work may make
				while entries[back] and (entries[back][2] == CURSOR or entries[back][2] == CURSOR_CHOICE) do back = back - 1 end
				local prev = entries[back]
				local mine = prev and ((prev[2] == INPUT and prev[3] ~= IDLE_EVENT) or prev[2] == COMMAND
					or (prev[2] == SELECT and interface_select))
				interface_select = mine
				if opts.on_select and mine then
					opts.on_select(e[1], { unpack(e, 3) }, game)
				end
			elseif kind == SELECT then
				game:select_locally({ unpack(e, 3) })
			elseif kind == INPUT and opts.input then
				ui:now(e[10])
				ui:keys(e[6], e[7], e[8], e[9])
				if scrolled then
					-- the log's view is the one after the dialogs took the event (DispatchUiEvent's GetTickCount)
				elseif e[11] >= 0 then
					ui:screen(e[11], e[12])
				else
					ui:screen((game.screen_x or 0) * 32, (game.screen_y or 0) * 32)
				end
				ui:input(e[3], e[4], e[5])
				if scrolled and e[11] >= 0 and opts.on_screen then opts.on_screen(e[1], e[11], e[12], e) end
				if tip_want then check_tip() end
				for _, c in ipairs(ui:take_commands()) do opts.on_command(e[1], c, game) end
			elseif kind == COMMAND and opts.input then
				ui:input(16, e[3], 0)
				for _, c in ipairs(ui:take_commands()) do opts.on_command(e[1], c, game) end
			elseif kind == CURSOR_CHOICE and opts.input then
				-- where the log has the original's choice change, with the system cursor where it saw it
				local id = e[4] and ui:cursor_update(e[4], e[5]) or ui:cursor_update()
				if opts.on_cursor then opts.on_cursor(e[1], id, e[3], game) end
			elseif kind == SCROLL and opts.input then
				if e ~= scrolled_early then scroll(e) end
			elseif kind == TIP and opts.input then
				-- the tooltip as the work left it, after the last of a run of records; the dialogs take an event
				-- before the log has it (DispatchUiEvent's GetTickCount), so an event's tooltip is checked after it
				local nxt = entries[next_entry + 1]
				if opts.on_tip and not (nxt and nxt[2] == TIP) then
					tip_want = { frame = e[1], rect = e[3] and { e[3], e[4], e[5], e[6] } }
					if not (nxt and nxt[2] == INPUT) then check_tip() end
				end
			elseif kind == CURSOR then
				ui:cursor(e[3], e[4])
			elseif kind == GAME_CALL and opts.input and (CALLS[e[3]] == "skip" or CALLS[e[3]] == "menu_pause") then
				-- the interface's own Escape skips and its modeless dialogs' pauses
			elseif kind == GAME_CALL and voices and CALLS[e[3]] == "critter" then
				-- the interface's own select sounds kill the critter
			elseif kind == GAME_CALL then
				local nxt = entries[next_entry + 1]
				if opts.input and CALLS[e[3]] == "release" then
					local st = ui:scroll_state()
					if nxt and nxt[2] == SCROLL then
						-- the camera pan's arrival releases the wait inside UpdateMapScroll, before the log has its state
						scroll(nxt)
						scrolled_early = nxt
					elseif st.pan == 1 and st.x == st.pan_x and st.y == st.pan_y then
						-- a pan to where the view already is: UpdateMapScroll releases it without a change to log
						ui:scroll(st.cursor_x, st.cursor_y)
					end
				end
				if opts.input and CALLS[e[3]] == "release" and opts.on_release then
					-- the end of the trigger portrait or the camera pan's arrival: the interface's to say
					opts.on_release(e[1], ui:take_release())
				end
				local call = { frame = e[1] }
				call[CALLS[e[3]]] = CALLS[e[3]] == "critter" and e[4] or e[4] and { e[4], e[5] } or true
				run.apply_ui(call)
				drain() -- what the call does (a paused frame's triggers, the skip) reaches the interface at once
			end
			next_entry = next_entry + 1
			collect(e[1])
		end
		collect(game.frame)
	end
	local step = 1
	while frames[next_want] do
		step = step + 1
		run.to(step)
		if not entries[next_entry] and frames[next_want] then
			error("hud_replay: the log ends before frame " .. frames[next_want])
		end
	end
	collect(game.frame)
	if voices and opts.on_sound_end then opts.on_sound_end(sounds_want, sounds_have, talks_want, talks_have) end
	return ui, game
end

return M
