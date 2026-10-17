-- Prints the trigger trace of a mission's native game (KP_EVENT_TRIGGER, plan phase 5 item 8) up to an object step,
-- one action per line as "frame player trigger action type result", the shape of the emulator's triggers.jsonl.
-- Usage (repository root):
--   luajit tools/lua/trigger_trace.lua MISSION.kpm SEED EXPANSION STEP [COMMANDS.jsonl [UI.jsonl]]
package.path = "./?.lua;" .. package.path
bit = bit or require("bit")

local oracle = require("tools.lua.oracle")
local events = require("game.simulation.events")

local function read(path)
	local f = assert(io.open(path, "rb"))
	local s = f:read("*a")
	f:close()
	return s
end

local g = oracle.start_mission(read, arg[1], tonumber(arg[2]), tonumber(arg[3] or "0"), { trace = true })
local commands = arg[5] and oracle.parse_commands(read(arg[5])) or nil
local ui = arg[6] and oracle.parse_ui(read(arg[6])) or nil
local run = oracle.runner(g, commands, ui)
local out = {}
function run.on_event(e)
	if e.kind == events.TRIGGER then out[#out + 1] = oracle.trace_line(e) end
end
run.to(tonumber(arg[4]))
io.write(table.concat(out, "\n"), #out > 0 and "\n" or "")
