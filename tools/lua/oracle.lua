-- The oracle runs of the native game (kp_core, ADR 0008): a generated mission started with the original's seed,
-- advanced object step by object step with the original's command and interface logs, and its state lines
-- (kp_core game:dump, the format of tools/native_state.py) hashed per category as tools/state_check.py does.
local mission_data = require("game.data.mission")
local native = require("game.native")
local terrain = require("game.data.terrain")

local M = {}

M.DATA = "generated/data/"

-- The files of a native game of a generated mission (path of its .kpm): { dat =, grp =, iscript =, aiscript =, mission =,
-- tileset = }; read(path) returns a file's bytes.
function M.blobs(read, path)
	local mission = read(path)
	local tileset = terrain.TILESETS[mission_data.parse(mission, path).tileset]
	return { dat = read(M.DATA .. "dat.kpd"), grp = read(M.DATA .. "grpmeta.kpd"), iscript = read(M.DATA .. "iscript.kpd"),
		aiscript = read(M.DATA .. "aiscript.kpd"),
		mission = mission, tileset = read(M.DATA .. "tilesets/" .. tileset .. ".kpd") }
end

-- The rows of a generated index (generated/missions/index.txt, generated/scenarios/index.txt): name -> first field
-- after it and line number; read once per read function (a weak cache).
local indexes = setmetatable({}, { __mode = "k" })
local function index_rows(read, path)
	local cache = indexes[read]
	if not cache then
		cache = {}
		indexes[read] = cache
	end
	if not cache[path] then
		local rows, n = {}, 0
		local ok, text = pcall(read, path)
		for line in (ok and text or ""):gmatch("[^\n]+") do
			n = n + 1
			local name, base = line:match("^(%S+)%s+(%S+)")
			if name then rows[name] = { line = n, base = base } end
		end
		cache[path] = rows
	end
	return cache[path]
end

-- The campaign mission index (0x526524: the mapdata.dat row, the line of generated/missions/index.txt) of a generated
-- mission or of a scenario's base mission (its row of generated/scenarios/index.txt), 0 when it has none.
function M.mission_index(read, path)
	local name = path:match("generated/missions/(.+)%.kpm$")
	local scenario = path:match("generated/scenarios/(.+)%.kpm$")
	if scenario then
		local row = index_rows(read, "generated/scenarios/index.txt")[scenario]
		name = row and row.base
	end
	local row = name and index_rows(read, "generated/missions/index.txt")[name]
	return row and row.line or 0
end

-- Starts a generated mission (path of its .kpm) with the game's initial seed and expansion byte; read(path)
-- returns a file's bytes. Returns the native game at its first object step. Its event ring is off (no presentation)
-- unless opts.events is true; opts.trace also records the trigger trace (KP_EVENT_TRIGGER) in it.
function M.start_mission(read, path, seed, expansion, opts)
	local core = assert(native.find("kp_core"), "oracle: no kp_core module (tools/native/build.sh)")
	return core.game.new(M.blobs(read, path), { seed = seed, expansion = expansion, name = path,
		events = opts ~= nil and (opts.events == true or opts.trace == true), trace = opts ~= nil and opts.trace == true,
		mission_index = M.mission_index(read, path) })
end

-- A run of game g from its start, advanced to the entry of an object step (1 = the start
-- itself). Steps 1 and 2 are PrimeGameLoop's; then RunGame runs the first turn, and each
-- game loop iteration (kp_core kp_run_frame) the interface events of the
-- current frame, the turn, the frame count and the steps; a step's dump is taken after the
-- count. commands: { { turn =, player =, bytes = }, ... } and ui: { { frame =, select =
-- { units } }, { frame =, mark = { x, y, unit } }, { frame =, cursor = { x, y } } or { frame =, onscreen = { x, y } }
-- (a draw of the game screen), { frame =, alliance = { a, b, v } } (a fixture), { frame =, paused = true },
-- { frame =, release = true }, { frame =, skip = true }, { frame =, crt = true } (a CRT rand draw of the interface,
-- which the creep tile art shares), { frame =, placement = { order, type } or {} } (the building placement mode starts
-- or closes), { frame =, objectives = string } (the leading ones are taken before step 1), ... } in
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
	local function turn_commands()
		local list = {}
		while commands[run.next_command] and commands[run.next_command].turn <= g.turn do
			list[#list + 1] = commands[run.next_command]
			run.next_command = run.next_command + 1
		end
		return list
	end
	local function turn()
		g:run_turn(turn_commands())
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
			elseif e.alliance then
				g:set_alliance(e.alliance[1], e.alliance[2], e.alliance[3])
			elseif e.paused then
				g:paused_tick(turn_commands())
			elseif e.release then
				g:trigger_release_wait()
			elseif e.skip then
				g:trigger_skip()
			elseif e.objectives then
				g:briefing_objectives(e.objectives)
			elseif e.crt then
				g:interface_crt_draw() -- a unit voice's choice (ChooseYesSound 0x447130)
			elseif e.placement then
				if e.placement[1] then g:start_placement(e.placement[1], e.placement[2]) else g:cancel_placement() end
			else
				g:move_placement_cursor(e.cursor[1], e.cursor[2])
			end
			run.next_ui = run.next_ui + 1
		end
		turn()
		g.frame = g.frame + 1
	end
	function run.to(step)
		-- The briefing's objectives come before the game: the leading ones of the log, taken once by a run from
		-- the start (a run continued from a later step has already taken them).
		if run.step == 1 and not run.begun then
			while ui[run.next_ui] and ui[run.next_ui].objectives do
				g:briefing_objectives(ui[run.next_ui].objectives)
				run.next_ui = run.next_ui + 1
			end
			run.begun = true
		end
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

-- Parses the emulator's ui.jsonl text (local selections, right-click target marks, markers, the screen's draws, the
-- frames of a paused game, the interface's ends of trigger waits, its CRT rand draws and the briefing's objectives).
function M.parse_ui(text)
	local out = {}
	for line in text:gmatch("[^\n]+") do
		local e = { frame = tonumber(line:match('"frame":%s*(%d+)')) }
		local units = line:match('"select":%s*%[([%d,%s]*)%]')
		if units then
			e.select = {}
			for n in units:gmatch("%d+") do e.select[#e.select + 1] = tonumber(n) end
		elseif line:match('"alliance"') then
			local a, b, v = line:match('"alliance":%s*%[%s*(%d+),%s*(%d+),%s*(%d+)%s*%]')
			e.alliance = { tonumber(a), tonumber(b), tonumber(v) }
		elseif line:match('"onscreen"') then
			local x, y = line:match('"onscreen":%s*%[%s*(%d+),%s*(%d+)%s*%]')
			e.onscreen = { tonumber(x), tonumber(y) }
		elseif line:match('"objectives"') then
			e.objectives = tonumber(line:match('"objectives":%s*(%d+)'))
		elseif line:match('"paused"') then
			e.paused = true
		elseif line:match('"release"') then
			e.release = true
		elseif line:match('"skip"') then
			e.skip = true
		elseif line:match('"crt"') then
			e.crt = true
		elseif line:match('"placement"') then
			local order, t = line:match('"placement":%s*%[%s*(%d+),%s*(%d+)%s*%]')
			e.placement = order and { tonumber(order), tonumber(t) } or {}
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

-- The trigger trace line of a KP_EVENT_TRIGGER record (tools/lua/traces.lua, the emulator's triggers.jsonl):
-- "frame player trigger action type result".
function M.trace_line(e)
	return string.format("%d %d %d %d %d %d", e.frame, e.a, e.b, bit.rshift(e.c, 16), bit.band(bit.rshift(e.c, 8), 0xff),
		bit.band(e.c, 0xff))
end

-- The AI trace line of a KP_EVENT_AI record (tools/lua/traces.lua, the emulator's ai.jsonl): "frame player slot bin pc
-- op".
function M.ai_trace_line(e)
	return string.format("%d %d %d %d %d %d", e.frame, e.a, bit.rshift(e.b, 1), bit.band(e.b, 1), math.floor(e.c / 256),
		e.c % 256)
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
