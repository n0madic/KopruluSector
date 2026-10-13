-- Test scenarios (phase 3, level C) against the original, object step by object step: each
-- scenario of tests/golden/oracle/scenarios/index.lua starts from generated/scenarios/
-- <name>.kpm with the original's seed, replays the original's command and interface logs
-- (local selections) and is compared
-- at every recorded step with the CRCs of the original's state dumps (tools/state_check.py;
-- from step 3 on without the drawn image bounds), on every runtime (the native game and its dump CRCs cost
-- little). Needs the converter's generated data and the native core (kp_core); skipped without them.
local native = require("game.native")
local oracle = require("tools.lua.oracle")

local DIR = "tests/golden/oracle/scenarios/"

local S = {}

local function load(T, name)
	return assert((loadstring or load)(T.read(DIR .. name .. ".lua")))()
end

local function commands_of(rows)
	local out = {}
	for _, r in ipairs(rows) do
		out[#out + 1] = { turn = r.turn, player = r.player,
			bytes = (r.bytes:gsub("%x%x", function(h) return string.char(tonumber(h, 16)) end)) }
	end
	return out
end

function S.scenarios_equal_the_original(T)
	if not (T.has(oracle.DATA .. "dat.kpd") and native.find("kp_core")) then
		T.env.log("SKIP test_scenarios: no " .. oracle.DATA .. " or no kp_core module")
		return
	end
	local bad = {}
	for _, name in ipairs(load(T, "index")) do
		local g = load(T, name)
		local game = oracle.start_mission(T.read, "generated/" .. g.mission .. ".kpm", g.seed, g.expansion)
		local run = oracle.runner(game, commands_of(g.commands), g.ui)
		local last = 0
		for step in pairs(g.steps) do last = math.max(last, step) end
		for step = 1, last do
			run.to(step)
			local want = g.steps[step]
			if want then
				local crcs = oracle.state_crcs(game, step >= oracle.DRAWN_STEP)
				for category, crc in pairs(want) do
					if crcs[category] ~= crc then bad[#bad + 1] = ("%s step %d %s"):format(name, step, category) end
				end
				if #bad > 0 then break end
			end
		end
	end
	T.ok(#bad == 0, "categories differ from the original: " .. table.concat(bad, ", "))
end

return S
