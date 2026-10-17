-- Initial mission state (phase 3.1) against the original: the state that StartGame leaves
-- before the first object step, for every mission in tests/golden/oracle/initial_state/
-- index.lua, compared category by category with the CRCs of the original's state dumps
-- (tools/state_check.py), with the objectives the original's briefing set before the game (which kp_core's
-- briefing_lead derives from the mission's briefing, as when Start is pressed at once). Needs the converter's
-- generated data and the native core (kp_core); skipped without them.
local native_only = require("tests.lua.native_only")
local oracle = require("tools.lua.oracle")

local DIR = "tests/golden/oracle/initial_state/"

local S = {}

local function load(T, name)
	return assert((loadstring or load)(T.read(DIR .. name .. ".lua")))()
end

function S.missions_equal_the_original(T)
	if not native_only.ready(T, "test_initial_state", { oracle.DATA .. "dat.kpd" }) then return end
	local bad = {}
	local index = load(T, "index")
	for k = 1, #index do
		local name = index[k]
		local g = load(T, name)
		local game = oracle.start_mission(T.read, "generated/missions/" .. g.mission .. ".kpm", g.seed, g.expansion)
		local objectives = g.objectives or {}
		if game:briefing_lead() ~= (objectives[#objectives] or 0) then
			bad[#bad + 1] = ("%s briefing_lead %d"):format(name, game:briefing_lead())
		end
		for _, id in ipairs(objectives) do game:briefing_objectives(id) end
		local crcs = oracle.state_crcs(game)
		for category, crc in pairs(g.crc) do
			if crcs[category] ~= crc then bad[#bad + 1] = name .. " " .. category end
		end
	end
	T.ok(#bad == 0, "categories differ from the original: " .. table.concat(bad, ", "))
end

return S
