-- Test scenarios (phase 3, level C) against the original, object step by object step: each
-- scenario of tests/golden/oracle/scenarios/index.lua (and each campaign run of runs/index.lua) starts from its
-- .kpm with the original's seed, replays the original's command and interface logs
-- (local selections) and is compared
-- at every recorded step with the CRCs of the original's state dumps (tools/state_check.py;
-- from step 3 on without the drawn image bounds), on every runtime (the native game and its dump CRCs cost
-- little); a scenario with triggers also compares its trigger trace (the actions run, plan phase 5 item 8). Needs
-- the converter's generated data and the native core (kp_core); skipped without them.
local native_only = require("tests.lua.native_only")
local oracle = require("tools.lua.oracle")
local events = require("game.simulation.events")

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

-- The runs of an index of goldens (DIR scenarios, or runs: campaign missions run idle) against the original's CRCs.
local function check_runs(T, dir)
	local bad = {}
	for _, name in ipairs(load(T, dir .. "index")) do
		local g = load(T, dir .. name)
		local game = oracle.start_mission(T.read, "generated/" .. g.mission .. ".kpm", g.seed, g.expansion,
			{ trace = g.trace ~= nil })
		local run = oracle.runner(game, commands_of(g.commands), g.ui)
		local trace = {}
		function run.on_event(e)
			if e.kind == events.TRIGGER then trace[#trace + 1] = oracle.trace_line(e) end
		end
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
		if g.trace and #bad == 0 and table.concat(trace, "\n") ~= table.concat(g.trace, "\n") then
			bad[#bad + 1] = name .. " trigger trace"
		end
	end
	return bad
end

function S.scenarios_equal_the_original(T)
	if not native_only.ready(T, "test_scenarios", { oracle.DATA .. "dat.kpd" }) then return end
	local bad = check_runs(T, "")
	T.ok(#bad == 0, "categories differ from the original: " .. table.concat(bad, ", "))
end

-- Campaign missions run idle (tests/golden/oracle/runs: their own triggers, plan phase 5) at the compared steps.
function S.campaign_runs_equal_the_original(T)
	if not native_only.ready(T, "test_scenarios", { oracle.DATA .. "dat.kpd" }) then return end
	local bad = check_runs(T, "../runs/")
	T.ok(#bad == 0, "categories differ from the original: " .. table.concat(bad, ", "))
end

return S
