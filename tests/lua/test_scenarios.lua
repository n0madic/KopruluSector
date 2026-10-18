-- Test scenarios (phase 3, level C) against the original, object step by object step: each
-- scenario of tests/golden/oracle/scenarios/index.lua (and each campaign run of runs/index.lua) starts from its
-- .kpm with the original's seed, replays the original's command and interface logs
-- (local selections) and is compared
-- at every recorded step with the CRCs of the original's state dumps (tools/state_check.py;
-- from step 3 on without the drawn image bounds), on every runtime (the native game and its dump CRCs cost
-- little); a scenario with triggers also compares its trigger trace (the actions run, plan phase 5 item 8), and one
-- with AI scripts its AI trace (the opcodes run, plan phase 7) by the trace's CRC-32. Needs
-- the converter's generated data and the native core (kp_core); skipped without them.
local native_only = require("tests.lua.native_only")
local oracle = require("tools.lua.oracle")
local events = require("game.simulation.events")
local crc32 = require("game.data.crc32")

local DIR = "tests/golden/oracle/scenarios/"

local S = {}

-- A parallel run's shards of the long tests (tests/lua/runner.lua).
S.SHARDS = { scenarios_equal_the_original = 6, campaign_runs_equal_the_original = 4 }

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

-- The differences of one golden run (a scenario, or a campaign mission run idle) from the original: its first
-- differing step's categories, or its trigger trace.
-- A golden run's game and its runner with the original's logs.
local function start_run(T, path, opts)
	local g = load(T, path)
	local game = oracle.start_mission(T.read, "generated/" .. g.mission .. ".kpm", g.seed, g.expansion, opts)
	return g, game, oracle.runner(game, commands_of(g.commands), g.ui)
end

local function check_run(T, path, name)
	local g = load(T, path)
	local _, game, run = start_run(T, path, { trace = g.trace ~= nil or g.ai_trace ~= nil })
	local trace, ai_trace, bad = {}, {}, {}
	function run.on_event(e)
		if e.kind == events.TRIGGER then
			trace[#trace + 1] = oracle.trace_line(e)
		elseif e.kind == events.AI then
			ai_trace[#ai_trace + 1] = oracle.ai_trace_line(e)
		end
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
			if #bad > 0 then return bad end
		end
	end
	if g.trace and table.concat(trace, "\n") ~= table.concat(g.trace, "\n") then bad[#bad + 1] = name .. " trigger trace" end
	if g.ai_trace and (#ai_trace ~= g.ai_trace.count or crc32.of(table.concat(ai_trace, "\n")) ~= g.ai_trace.crc) then
		bad[#bad + 1] = name .. " AI trace"
	end
	return bad
end

-- The runs of an index of goldens (DIR scenarios, or runs: campaign missions run idle) against the original's CRCs,
-- each checked on its own.
local function check_runs(T, dir)
	local bad = {}
	for k, name in ipairs(load(T, dir .. "index")) do
		if T.mine(k) then
			for _, b in ipairs(check_run(T, dir .. name, name)) do bad[#bad + 1] = b end
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

-- The interface's Escape skips only while a trigger of the local player still runs (kp_trigger_skippable): a finished
-- trigger keeps its executing flag 0x01 next to the disabled flag 0x08, which the original's skip marks too.
function S.escape_skips_running_triggers_only(T)
	if not native_only.ready(T, "test_scenarios", { oracle.DATA .. "dat.kpd" }) then return end
	local function flags(game, copy)
		for _, line in ipairs(game:dump()) do
			local f = line:match("^trigger " .. copy .. " .*flags=(%d+)")
			if f then return tonumber(f) end
		end
	end
	local _, game, run = start_run(T, "trigger_basics")
	run.to(5)
	T.eq(game:trigger_skippable(), true, "a Wait runs")
	run.to(128)
	T.eq(flags(game, "0.1"), 9, "a finished trigger keeps 0x01")
	T.eq(game:trigger_skippable(), false, "only finished triggers")
	_, game, run = start_run(T, "trigger_cinematic_full")
	run.to(5)
	T.eq(game:trigger_skippable(), false, "a finished trigger and an unskippable transmission")
end

return S
