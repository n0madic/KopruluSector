-- Prints the state model (tools/lua/state_model.lua) of a mission at an object step.
-- Usage (repository root):
--   luajit tools/lua/start_state.lua MISSION.kpm SEED EXPANSION [STEP [COMMANDS.jsonl [UI.jsonl]]]
-- SEED is the game's initial seed (0x673934); EXPANSION is 0 or 1 (byte 0x536720); STEP is
-- the object step whose entry is printed (1, the default, is the game start); COMMANDS is
-- the emulator's command log of the run and UI its interface log, replayed on the way.
package.path = "./?.lua;" .. package.path
bit = bit or require("bit")

local state_model = require("tools.lua.state_model")

local function read(path)
	local f = assert(io.open(path, "rb"))
	local s = f:read("*a")
	f:close()
	return s
end

local g = state_model.start_mission(read, arg[1], tonumber(arg[2]), tonumber(arg[3] or "0"))
local commands = arg[5] and state_model.parse_commands(read(arg[5])) or nil
local ui = arg[6] and state_model.parse_ui(read(arg[6])) or nil
state_model.advance(g, tonumber(arg[4] or "1"), commands, ui)
io.write(table.concat(state_model.lines(g), "\n"), "\n")
