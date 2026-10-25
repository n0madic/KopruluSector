-- Writes the replay of a scenario golden file (tests/golden/oracle/scenarios/<name>.lua) for the native tools
-- kp_replay and kp_bench (native/kp_core/tools/kp_runner.h: KPRP 1 text).
-- Usage (repository root): luajit tools/lua/export_replay.lua NAME OUT
package.path = "./?.lua;" .. package.path

local name, out = arg[1], arg[2]
local function read(path)
	local f = assert(io.open(path, "rb"))
	local s = f:read("*a")
	f:close()
	return s
end

local g = assert(loadstring(read("tests/golden/oracle/scenarios/" .. name .. ".lua")))()
local oracle = require("tools.lua.oracle")
local lines = { "KPRP 1", "mission generated/" .. g.mission .. ".kpm", "seed " .. g.seed, "expansion " .. g.expansion,
	"index " .. oracle.mission_index(read, "generated/" .. g.mission .. ".kpm") }
lines[#lines + 1] = "last " .. oracle.last_step(g)
for _, c in ipairs(g.commands) do lines[#lines + 1] = ("command %d %d %s"):format(c.turn, c.player, c.bytes) end
for _, e in ipairs(oracle.expand_ui(g.ui or {})) do
	if e.select then
		lines[#lines + 1] = "select " .. e.frame .. (#e.select > 0 and (" " .. table.concat(e.select, " ")) or "")
	elseif e.mark then
		lines[#lines + 1] = ("mark %d %d %d %d"):format(e.frame, e.mark[1], e.mark[2], e.mark[3])
	elseif e.alliance then
		lines[#lines + 1] = ("alliance %d %d %d %d"):format(e.frame, e.alliance[1], e.alliance[2], e.alliance[3])
	elseif e.onscreen then
		lines[#lines + 1] = ("onscreen %d %d %d"):format(e.frame, e.onscreen[1], e.onscreen[2])
	elseif e.objectives then
		lines[#lines + 1] = ("objectives %d %d"):format(e.frame, e.objectives)
	elseif e.srand then
		lines[#lines + 1] = ("srand %d %d"):format(e.frame, e.srand)
	elseif e.critter then
		lines[#lines + 1] = ("critter %d %d"):format(e.frame, e.critter)
	elseif e.menu_pause then
		lines[#lines + 1] = ("menu_pause %d %d"):format(e.frame, e.menu_pause)
	elseif e.paused or e.release or e.skip or e.crt then
		lines[#lines + 1] = ("%s %d"):format(e.paused and "paused" or e.release and "release" or e.skip and "skip" or "crt",
			e.frame)
	elseif e.placement then
		lines[#lines + 1] = "placement " .. e.frame .. (#e.placement > 0 and (" " .. table.concat(e.placement, " ")) or "")
	else
		lines[#lines + 1] = ("cursor %d %d %d"):format(e.frame, e.cursor[1], e.cursor[2])
	end
end
local steps = {}
for step in pairs(g.steps) do steps[#steps + 1] = step end
table.sort(steps)
for _, step in ipairs(steps) do
	local cats = {}
	for c in pairs(g.steps[step]) do cats[#cats + 1] = c end
	table.sort(cats)
	for _, c in ipairs(cats) do lines[#lines + 1] = ("crc %d %s %d"):format(step, c, g.steps[step][c]) end
end
local f = assert(io.open(out, "wb"))
f:write(table.concat(lines, "\n"), "\n")
f:close()
