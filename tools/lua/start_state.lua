-- Prints the state model (tools/lua/state_model.lua) of a mission at its first object step.
-- Usage (repository root): luajit tools/lua/start_state.lua MISSION.kpm SEED EXPANSION
-- SEED is the game's initial seed (0x673934); EXPANSION is 0 or 1 (byte 0x536720).
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
io.write(table.concat(state_model.lines(g), "\n"), "\n")
