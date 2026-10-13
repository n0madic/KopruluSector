-- The oracle runs of the native game (kp_core, ADR 0008): a generated mission started with the original's seed,
-- advanced object step by object step with the original's command and interface logs, and its state lines
-- (kp_core game:dump, the format of tools/native_state.py) hashed per category as tools/state_check.py does.
local mission_data = require("game.data.mission")
local native = require("game.native")
local terrain = require("game.data.terrain")

local M = {}

M.DATA = "generated/data/"

-- Starts a generated mission (path of its .kpm) with the game's initial seed and expansion byte; read(path)
-- returns a file's bytes. Returns the native game at its first object step.
function M.start_mission(read, path, seed, expansion)
	local core = assert(native.find("kp_core"), "oracle: no kp_core module (tools/native/build.sh)")
	local mission = read(path)
	local tileset = terrain.TILESETS[mission_data.parse(mission, path).tileset]
	return core.game.new({
		dat = read(M.DATA .. "dat.kpd"), grp = read(M.DATA .. "grpmeta.kpd"), iscript = read(M.DATA .. "iscript.kpd"),
		mission = mission, tileset = read(M.DATA .. "tilesets/" .. tileset .. ".kpd"),
	}, { seed = seed, expansion = expansion, name = path })
end

-- A run of game g from its start, advanced to the entry of an object step (1 = the start
-- itself). Steps 1 and 2 are PrimeGameLoop's; then RunGame runs the first turn, and each
-- game loop iteration (game/simulation/step.lua run_frame) the interface events of the
-- current frame, the turn, the frame count and the steps; a step's dump is taken after the
-- count. commands: { { turn =, player =, bytes = }, ... } and ui: { { frame =, select =
-- { units } }, { frame =, mark = { x, y, unit } } or { frame =, cursor = { x, y } }, ... } in
-- log order (the emulator's
-- commands.jsonl and ui.jsonl).
function M.runner(g, commands, ui)
	commands, ui = commands or {}, ui or {}
	local run = { g = g, step = 1, next_command = 1, next_ui = 1 }
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

-- Parses the emulator's ui.jsonl text (local selections, right-click target marks and
-- markers).
function M.parse_ui(text)
	local out = {}
	for line in text:gmatch("[^\n]+") do
		local e = { frame = tonumber(line:match('"frame":%s*(%d+)')) }
		local units = line:match('"select":%s*%[([%d,%s]*)%]')
		if units then
			e.select = {}
			for n in units:gmatch("%d+") do e.select[#e.select + 1] = tonumber(n) end
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

-- CRC-32 of each category's lines joined by newlines (tools/state_check.py golden_lines).
function M.crcs(lines)
	local crc32 = require("game.data.crc32")
	local by = {}
	for _, l in ipairs(lines) do
		local c = l:match("^(%S+)")
		by[c] = by[c] or {}
		by[c][#by[c] + 1] = l
	end
	local out = {}
	for c, ls in pairs(by) do out[c] = crc32.of(table.concat(ls, "\n")) end
	return out
end

-- Fields of the original that its drawing refreshes from object step 3 on (the image bounds of the rows in view,
-- 0x445de0); tools/state_check.py leaves them out there.
M.DRAWN_STEP = 3

-- The state lines of native game g; `drawn` leaves out the image bounds of M.DRAWN_STEP on.
function M.lines(g, drawn)
	return g:dump(drawn)
end

-- The category CRCs of native game g's state lines, computed in C (M.crcs of M.lines, without building the lines
-- in Lua).
function M.state_crcs(g, drawn)
	return g:dump_crcs(drawn)
end

return M
