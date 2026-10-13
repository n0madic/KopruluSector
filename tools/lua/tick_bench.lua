-- Simulation step timing (plan §10: a step within 4 ms on the target mobile interpreter, none
-- above 8 ms). Runs a mission or scenario with the original's command and interface logs and
-- reports the CPU time of each object step of the native game (kp_core step_frame, visibility included) over the
-- run; the device measure is kp_bench (ADR 0008).
-- Usage (repository root):
--   luajit [-joff] tools/lua/tick_bench.lua MISSION.kpm SEED STEPS [COMMANDS.jsonl [UI.jsonl]]
--   tools/toolchain/lua5.1 tools/lua/tick_bench.lua ...
package.path = "./?.lua;" .. package.path
if not bit then
	local ok, lib = pcall(require, "bit")
	if ok then
		bit = lib
	else
		bit = dofile("tests/lua/bitshim.lua")
		package.loaded.bit = bit
	end
end

local oracle = require("tools.lua.oracle")

local function read(path)
	local f = assert(io.open(path, "rb"))
	local s = f:read("*a")
	f:close()
	return s
end

local path, seed, steps = arg[1], tonumber(arg[2]), tonumber(arg[3])
local commands = arg[4] and oracle.parse_commands(read(arg[4])) or nil
local ui = arg[5] and oracle.parse_ui(read(arg[5])) or nil
local g = oracle.start_mission(read, path, seed, 0)
local run = oracle.runner(g, commands, ui)
local times, at = {}, {}
local clock = os.clock
local SLOWEST = 8
for step = 2, steps do
	local t0 = clock()
	run.to(step)
	times[#times + 1] = (clock() - t0) * 1000
	at[#times] = step
end
local units = 0
local n = g.lists.active.head
while n ~= 0 do
	units = units + 1
	n = g.u.next[n]
end
local order = {}
for k = 1, #times do order[k] = k end
table.sort(order, function(a, b) return times[a] > times[b] end)
local slowest = {}
for k = 1, math.min(SLOWEST, #order) do slowest[k] = ("%d: %.1f"):format(at[order[k]], times[order[k]]) end
table.sort(times)
local sum = 0
for _, t in ipairs(times) do sum = sum + t end
local function pct(p) return times[math.max(1, math.ceil(#times * p))] end
local backend = jit and (jit.version .. (jit.status() and " JIT on" or " JIT off")) or _VERSION
print(("%s: %d steps, %d active units; mean %.3f ms, median %.3f ms, p99 %.3f ms, max %.3f ms"):format(backend,
	#times, units, sum / #times, pct(0.5), pct(0.99), times[#times]))
print("slowest steps (step: ms): " .. table.concat(slowest, ", "))
