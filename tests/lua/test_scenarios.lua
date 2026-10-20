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
local RESULT_VICTORY = 3
local RESULT_STEPS = 100 -- a play's dumps (every 20th step) end when the result dialog pauses the game

local S = {}

-- A parallel run's shards of the long tests (tests/lua/runner.lua).
S.SHARDS = { scenarios_equal_the_original = 6, campaign_runs_equal_the_original = 4,
	campaign_chains_equal_the_original = 4, campaign_plays_equal_the_original = 8 }

local function load(T, name)
	return assert((loadstring or load)(T.read(DIR .. name .. ".lua")))()
end

-- The differences of one golden run (a scenario, or a campaign mission run idle) from the original: its first
-- differing step's categories, or its trigger trace.
-- A golden run's game and its runner with the original's logs.
local function start_run(T, path, opts)
	local g = load(T, path)
	return g, oracle.start_golden(T.read, g, opts)
end

-- opts: start_mission's (a chain's game: the game before it as opts.carry); returns the differences, and the game, its
-- runner and the last compared step.
local function check_run(T, path, name, opts)
	local g = load(T, path)
	opts = opts or {}
	opts.trace, opts.turn = g.trace ~= nil or g.ai_trace ~= nil, g.turn
	local _, game, run = start_run(T, path, opts)
	local trace, ai_trace, bad = {}, {}, {}
	function run.on_event(e)
		if e.kind == events.TRIGGER then
			trace[#trace + 1] = oracle.trace_line(e)
		elseif e.kind == events.AI then
			ai_trace[#ai_trace + 1] = oracle.ai_trace_line(e)
		end
	end
	local last = oracle.last_step(g)
	for step = 1, last do
		run.to(step)
		local want = g.steps[step]
		if want then
			for _, category in ipairs(oracle.step_differences(oracle.state_crcs(game, step >= oracle.DRAWN_STEP), want)) do
				bad[#bad + 1] = ("%s step %d %s"):format(name, step, category)
			end
			if #bad > 0 then return bad, game, run, last end
		end
	end
	if g.trace and table.concat(trace, "\n") ~= table.concat(g.trace, "\n") then bad[#bad + 1] = name .. " trigger trace" end
	if g.ai_trace and (#ai_trace ~= g.ai_trace.count or crc32.of(table.concat(ai_trace, "\n")) ~= g.ai_trace.crc) then
		bad[#bad + 1] = name .. " AI trace"
	end
	return bad, game, run, last
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

-- The scripted wins of the campaign rows (tests/golden/oracle/plays, plan phase 8): the emulator's plays
-- (sc_emulator/campaign_plays.py) at every 20th step by the state's fingerprint and every 500th by its categories, and
-- each play's result is the local player's victory.
function S.campaign_plays_equal_the_original(T)
	if not native_only.ready(T, "test_scenarios", { oracle.DATA .. "dat.kpd" }) then return end
	if not T.has("tests/golden/oracle/plays/index.lua") then return end
	local bad = {}
	for k, name in ipairs(load(T, "../plays/index")) do
		if T.mine(k) then
			local diffs, game, run, last = check_run(T, "../plays/" .. name, name)
			for _, b in ipairs(diffs) do bad[#bad + 1] = b end
			if #diffs == 0 then -- the result comes within the compared stride after the last dumped step
				local step = last
				while game.results[game.local_player] == 0 and step < last + RESULT_STEPS do
					step = step + 1
					run.to(step)
				end
				if game.results[game.local_player] ~= RESULT_VICTORY then bad[#bad + 1] = name .. " no victory" end
			end
		end
	end
	T.ok(#bad == 0, "plays differ from the original: " .. table.concat(bad, ", "))
end

-- The games of the campaign chains (tests/golden/oracle/chains, plan phase 8): the emulator played each chain through
-- the original's progression (every mission won with "there is no cow level", one lost with "game over man"). Each
-- game starts with what the game before it in its chain left (KpCarry: Koprulu's own game before, as the campaign
-- runs them), equals the original at the compared steps, and after its result step the campaign's next mission
-- (game/campaign.lua, with the game's own result and next scenario) is the chain's next game.
function S.campaign_chains_equal_the_original(T)
	if not native_only.ready(T, "test_scenarios", { oracle.DATA .. "dat.kpd", oracle.DATA .. "campaign.kpd" }) then return end
	local chains, order = {}, {}
	for _, name in ipairs(load(T, "../chains/index")) do
		local chain = name:match("^(.-)%.")
		if not chains[chain] then
			chains[chain] = {}
			order[#order + 1] = chain
		end
		table.insert(chains[chain], name)
	end
	local bad = {}
	for k, chain in ipairs(order) do
		if T.mine(k) then
			local games, before = {}, nil
			for _, name in ipairs(chains[chain]) do games[#games + 1] = { g = load(T, "../chains/" .. name), name = name } end
			local failed, why = oracle.campaign_chain(T.read, games, function(g, name, opts)
				if (g.carry_from or "") ~= (before or "") then return nil, "carries from " .. tostring(g.carry_from) end
				local diffs, game, run, last = check_run(T, "../chains/" .. name, name, opts)
				if #diffs > 0 then return nil, table.concat(diffs, ", ") end
				run.to(last + 1) -- the step whose trigger pass opened the result dialog
				before = name
				return game
			end)
			if failed then bad[#bad + 1] = failed .. ": " .. why end
		end
	end
	T.ok(#bad == 0, "chain games differ from the original: " .. table.concat(bad, ", "))
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
