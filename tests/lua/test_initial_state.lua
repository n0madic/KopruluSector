-- Initial mission state (phase 3.1) against the original: the state that StartGame leaves
-- before the first object step, for every mission in tests/golden/oracle/initial_state/
-- index.lua, compared category by category with the CRCs of the original's state dumps
-- (tools/state_check.py). Needs the converter's generated data; skipped without it. LuaJIT
-- with the JIT checks every mission, the interpreters every STRIDE-th one (about 10 s per
-- large map on PUC Lua with the bit shim).
local state_model = require("tools.lua.state_model")
local oracle = require("tools.lua.oracle")

local DIR = "tests/golden/oracle/initial_state/"
local STRIDE = 8

local S = {}

local function load(T, name)
	return assert((loadstring or load)(T.read(DIR .. name .. ".lua")))()
end

function S.missions_equal_the_original(T)
	if not T.has(state_model.DATA .. "dat.kpd") then
		T.env.log("SKIP test_initial_state: no " .. state_model.DATA)
		return
	end
	local bad = {}
	local stride = (jit and jit.status and jit.status()) and 1 or STRIDE
	local index = load(T, "index")
	for k = 1, #index, stride do
		local name = index[k]
		local g = load(T, name)
		local game = state_model.start_mission(T.read, "generated/missions/" .. g.mission .. ".kpm", g.seed, g.expansion)
		local crcs = oracle.crcs(state_model.lines(game))
		for category, crc in pairs(g.crc) do
			if crcs[category] ~= crc then bad[#bad + 1] = name .. " " .. category end
		end
	end
	T.ok(#bad == 0, "categories differ from the original: " .. table.concat(bad, ", "))
end

return S
