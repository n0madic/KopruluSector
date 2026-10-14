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
local lines = { "KPRP 1", "mission generated/" .. g.mission .. ".kpm", "seed " .. g.seed, "expansion " .. g.expansion }
local last = 0
for step in pairs(g.steps) do last = math.max(last, step) end
lines[#lines + 1] = "last " .. last
for _, c in ipairs(g.commands) do lines[#lines + 1] = ("command %d %d %s"):format(c.turn, c.player, c.bytes) end
for _, e in ipairs(g.ui or {}) do
	if e.select then
		lines[#lines + 1] = "select " .. e.frame .. (#e.select > 0 and (" " .. table.concat(e.select, " ")) or "")
	elseif e.mark then
		lines[#lines + 1] = ("mark %d %d %d %d"):format(e.frame, e.mark[1], e.mark[2], e.mark[3])
	elseif e.alliance then
		lines[#lines + 1] = ("alliance %d %d %d %d"):format(e.frame, e.alliance[1], e.alliance[2], e.alliance[3])
	elseif e.onscreen then
		lines[#lines + 1] = ("onscreen %d %d %d"):format(e.frame, e.onscreen[1], e.onscreen[2])
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
