-- Test scenarios (phase 3, level C) against the original, object step by object step: each
-- scenario of tests/golden/oracle/scenarios/index.lua starts from generated/scenarios/
-- <name>.kpm with the original's seed, replays the original's command log and is compared
-- at every recorded step with the CRCs of the original's state dumps (tools/state_check.py;
-- from step 3 on without the drawn image bounds). Needs the converter's generated data;
-- skipped without it.
local state_model = require("tools.lua.state_model")

local DIR = "tests/golden/oracle/scenarios/"

local S = {}

local function load(T, name)
	return assert((loadstring or load)(T.read(DIR .. name .. ".lua")))()
end

local function commands_of(rows)
	local out = {}
	for _, r in ipairs(rows) do
		out[#out + 1] = { frame = r[1], player = r[2],
			bytes = (r[3]:gsub("%x%x", function(h) return string.char(tonumber(h, 16)) end)) }
	end
	return out
end

function S.scenarios_equal_the_original(T)
	if not T.has(state_model.DATA .. "dat.kpd") then
		T.env.log("SKIP test_scenarios: no " .. state_model.DATA)
		return
	end
	local bad = {}
	for _, name in ipairs(load(T, "index")) do
		local g = load(T, name)
		local game = state_model.start_mission(T.read, "generated/" .. g.mission .. ".kpm", g.seed, g.expansion)
		local run = state_model.runner(game, commands_of(g.commands))
		local last = 0
		for step in pairs(g.steps) do last = math.max(last, step) end
		for step = 1, last do
			run.to(step)
			local want = g.steps[step]
			if want then
				local crcs = state_model.crcs(state_model.lines(game, step >= state_model.DRAWN_STEP))
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
