-- The oracle runs of the native game (kp_core, ADR 0008): a generated mission started with the original's seed,
-- advanced object step by object step with the original's command and interface logs, and its state lines
-- (kp_core game:dump, the format of tools/native_state.py) hashed per category as tools/state_check.py does.
local mission_data = require("game.data.mission")
local native = require("game.native")
local terrain = require("game.data.terrain")

local M = {}

M.DATA = "generated/data/"

-- The files of a native game of a generated mission (path of its .kpm): { dat =, grp =, iscript =, mission =,
-- tileset = }; read(path) returns a file's bytes.
function M.blobs(read, path)
	local mission = read(path)
	local tileset = terrain.TILESETS[mission_data.parse(mission, path).tileset]
	return { dat = read(M.DATA .. "dat.kpd"), grp = read(M.DATA .. "grpmeta.kpd"), iscript = read(M.DATA .. "iscript.kpd"),
		mission = mission, tileset = read(M.DATA .. "tilesets/" .. tileset .. ".kpd") }
end

-- Starts a generated mission (path of its .kpm) with the game's initial seed and expansion byte; read(path)
-- returns a file's bytes. Returns the native game at its first object step. Its event ring is off (no presentation)
-- unless opts.events is true.
function M.start_mission(read, path, seed, expansion, opts)
	local core = assert(native.find("kp_core"), "oracle: no kp_core module (tools/native/build.sh)")
	return core.game.new(M.blobs(read, path), { seed = seed, expansion = expansion, name = path,
		events = (opts and opts.events) == true })
end

-- A run of game g from its start, advanced to the entry of an object step (1 = the start
-- itself). Steps 1 and 2 are PrimeGameLoop's; then RunGame runs the first turn, and each
-- game loop iteration (kp_core kp_run_frame) the interface events of the
-- current frame, the turn, the frame count and the steps; a step's dump is taken after the
-- count. commands: { { turn =, player =, bytes = }, ... } and ui: { { frame =, select =
-- { units } }, { frame =, mark = { x, y, unit } }, { frame =, cursor = { x, y } } or { frame =, onscreen = { x, y } }
-- (a draw of the game screen), ... } in
-- log order (the emulator's
-- commands.jsonl and ui.jsonl). With run.on_event set, the events of a game that records them (start_mission's
-- opts.events) go to it after every step, as records of M.events.
function M.runner(g, commands, ui)
	commands, ui = commands or {}, ui or {}
	local run = { g = g, step = 1, next_command = 1, next_ui = 1 }
	local function drain()
		if run.on_event then
			for _, e in ipairs(M.events(g)) do run.on_event(e) end
		end
	end
	local function turn()
		local list = {}
		while commands[run.next_command] and commands[run.next_command].turn <= g.turn do
			list[#list + 1] = commands[run.next_command]
			run.next_command = run.next_command + 1
		end
		g:run_turn(list)
	end
	local function loop_head()
		while ui[run.next_ui] and ui[run.next_ui].frame <= g.frame do
			local e = ui[run.next_ui]
			if e.select then
				g:select_locally(e.select)
			elseif e.mark then
				g:mark_target(e.mark[1], e.mark[2], e.mark[3])
			elseif e.onscreen then
				g:refresh_terrain_onscreen(e.onscreen[1], e.onscreen[2])
			else
				g:move_placement_cursor(e.cursor[1], e.cursor[2])
			end
			run.next_ui = run.next_ui + 1
		end
		turn()
		g.frame = g.frame + 1
	end
	function run.to(step)
		while run.step < step do
			if run.step == 1 then
				g:step_objects()
			elseif run.step == 2 then
				g:step_objects()
				turn()
				loop_head()
			else
				g:step_frame()
				loop_head()
			end
			drain()
			run.step = run.step + 1
		end
		return g
	end
	return run
end

function M.advance(g, step, commands, ui)
	return M.runner(g, commands, ui).to(step)
end

-- Parses the emulator's commands.jsonl text.
function M.parse_commands(text)
	local out = {}
	for line in text:gmatch("[^\n]+") do
		local frame = tonumber(line:match('"frame":%s*(%d+)'))
		local turn = tonumber(line:match('"turn":%s*(%d+)'))
		local player = tonumber(line:match('"player":%s*(%d+)'))
		local hex = line:match('"bytes":%s*"(%x*)"')
		out[#out + 1] = { frame = frame, turn = turn, player = player,
			bytes = (hex:gsub("%x%x", function(h) return string.char(tonumber(h, 16)) end)) }
	end
	return out
end

-- Parses the emulator's ui.jsonl text (local selections, right-click target marks, markers and the screen's draws).
function M.parse_ui(text)
	local out = {}
	for line in text:gmatch("[^\n]+") do
		local e = { frame = tonumber(line:match('"frame":%s*(%d+)')) }
		local units = line:match('"select":%s*%[([%d,%s]*)%]')
		if units then
			e.select = {}
			for n in units:gmatch("%d+") do e.select[#e.select + 1] = tonumber(n) end
		elseif line:match('"onscreen"') then
			local x, y = line:match('"onscreen":%s*%[%s*(%d+),%s*(%d+)%s*%]')
			e.onscreen = { tonumber(x), tonumber(y) }
		elseif line:match('"mark"') then
			local x, y, unit = line:match('"mark":%s*%[%s*(%-?%d+),%s*(%-?%d+),%s*(%d+)%s*%]')
			e.mark = { tonumber(x), tonumber(y), tonumber(unit) }
		else
			local x, y = line:match('"cursor":%s*%[%s*(%-?%d+),%s*(%-?%d+)%s*%]')
			e.cursor = { tonumber(x), tonumber(y) }
		end
		out[#out + 1] = e
	end
	return out
end

-- Fields of the original that its drawing refreshes from object step 3 on (the image bounds of the rows in view,
-- 0x445de0); tools/state_check.py leaves them out there.
M.DRAWN_STEP = 3

-- The state lines of native game g; `drawn` leaves out the image bounds of M.DRAWN_STEP on.
function M.lines(g, drawn)
	return g:dump(drawn)
end

-- The category CRCs of native game g's state lines (tools/state_check.py golden_lines: each category's lines joined
-- by newlines), computed in C.
function M.state_crcs(g, drawn)
	return g:dump_crcs(drawn)
end

-- The events game g has emitted since the last drain, as records { kind =, frame =, seq =, a =, b =, c =, text = }
-- (the game's events(out) gives them as a flat array of kp_core.game.EVENT_FIELDS values per event).
function M.events(g)
	local flat, out, fields = {}, {}, native.find("kp_core").game.EVENT_FIELDS
	for k = 1, g:events(flat) do
		local o = (k - 1) * fields
		out[k] = { kind = flat[o + 1], frame = flat[o + 2], seq = flat[o + 3], a = flat[o + 4], b = flat[o + 5],
			c = flat[o + 6], text = flat[o + 7] or nil }
	end
	return out
end

return M
