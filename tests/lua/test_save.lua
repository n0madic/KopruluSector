-- Saves at a step boundary (version 1, kp_core game:save/load; plan D17, ADR 0008): a game saved in the middle
-- of every scenario of tests/golden/oracle/scenarios/ and loaded into a game of the same mission, fresh from its
-- start or run on to the scenario's end, continues exactly as the original did (the per-step CRCs of its state dumps), and saving the loaded game gives
-- the same bytes. The initial state of every mission survives a save and a load unchanged. Needs the converter's
-- generated data and the native core; skipped without them.
local native_only = require("tests.lua.native_only")
local oracle = require("tools.lua.oracle")

local DIR = "tests/golden/oracle/"
local DATA = "test data" -- the tests load saves into the same generated data
local WASTELAND_SAVE_STEP = 3700 -- the Barracks of terran01's scripted victory under construction

local S = {}

-- A parallel run's shards of the long tests (tests/lua/runner.lua).
S.SHARDS = { saved_scenarios_continue_as_the_original = 6 }

local function load(T, path)
	return assert((loadstring or load)(T.read(DIR .. path .. ".lua")))()
end

local function commands_of(rows)
	local out = {}
	for _, r in ipairs(rows) do
		out[#out + 1] = { turn = r.turn, player = r.player,
			bytes = (r.bytes:gsub("%x%x", function(h) return string.char(tonumber(h, 16)) end)) }
	end
	return out
end

local function ready(T)
	return native_only.ready(T, "test_save", { oracle.DATA .. "dat.kpd" })
end

-- Saves golden g's game at step `mid` and loads it into games of the same mission run to each step of `intos`; each
-- must save the same bytes again and continue as the original's per-step CRCs.
local function check_continuations(T, name, g, mid, intos, bad)
	local path = "generated/" .. g.mission .. ".kpm"
	local commands = commands_of(g.commands)
	local last = 0
	for step in pairs(g.steps) do last = math.max(last, step) end
	local game = oracle.start_mission(T.read, path, g.seed, g.expansion)
	local run = oracle.runner(game, commands, g.ui)
	run.to(mid)
	local bytes = game:save(g.mission, DATA)
	for _, into in ipairs(intos) do -- the game that takes the save: fresh, or run on to the end
		local loaded = oracle.start_mission(T.read, path, g.seed, g.expansion)
		oracle.runner(loaded, commands, g.ui).to(into == "last" and last or into)
		loaded:load(bytes, g.mission, DATA)
		if loaded:save(g.mission, DATA) ~= bytes then bad[#bad + 1] = name .. " round trip" end
		local cont = oracle.runner(loaded, commands, g.ui)
		cont.step, cont.next_command, cont.next_ui = run.step, run.next_command, run.next_ui
		for step = mid + 1, last do
			cont.to(step)
			local want = g.steps[step]
			if want then
				local crcs = oracle.state_crcs(loaded, step >= oracle.DRAWN_STEP)
				for category, crc in pairs(want) do
					if crcs[category] ~= crc then bad[#bad + 1] = ("%s into %s step %d %s"):format(name, into, step, category) end
				end
				if #bad > 0 then break end
			end
		end
	end
end

function S.saved_scenarios_continue_as_the_original(T)
	if not ready(T) then return end
	local bad = {}
	for k, name in ipairs(load(T, "scenarios/index")) do
		if T.mine(k) then
			local g = load(T, "scenarios/" .. name)
			local last = 0
			for step in pairs(g.steps) do last = math.max(last, step) end
			check_continuations(T, name, g, math.floor(last / 2), { 1, "last" }, bad)
		end
	end
	T.ok(#bad == 0, "continuations after a load differ: " .. table.concat(bad, ", "))
end

-- terran01 played to victory, saved while its Barracks is being built, Marines trained and SCVs harvest (plan phase
-- 6 item 7), loaded into a fresh game.
function S.saved_wasteland_continues_as_the_original(T)
	if not ready(T) then return end
	local bad = {}
	check_continuations(T, "terran-wasteland", load(T, "runs/terran-wasteland"), WASTELAND_SAVE_STEP, { 1 }, bad)
	T.ok(#bad == 0, "continuations after a load differ: " .. table.concat(bad, ", "))
end

-- Campaign runs whose computers are in the middle of AI work at step AI_SAVE_STEP (plan phase 7E), saved there and
-- loaded into a fresh game: scripts waiting (wait, wait_build, attack_do), towns building and training, attack groups
-- gathering, region states 3 and 7, nukes armed.
local AI_SAVE_STEP = 1001
local AI_SAVE_RUNS = { "terran-m04", "protoss-m28", "xterran-m44", "xterran-m46", "xzerg-m60" }

function S.saved_ai_runs_continue_as_the_original(T)
	if not ready(T) then return end
	local bad = {}
	for _, name in ipairs(AI_SAVE_RUNS) do
		check_continuations(T, name, load(T, "runs/" .. name), AI_SAVE_STEP, { 1 }, bad)
	end
	T.ok(#bad == 0, "continuations after a load differ: " .. table.concat(bad, ", "))
end

function S.initial_states_survive_a_save(T)
	if not ready(T) then return end
	local bad = {}
	for _, name in ipairs(load(T, "initial_state/index")) do
		local g = load(T, "initial_state/" .. name)
		local path = "generated/missions/" .. g.mission .. ".kpm"
		local game = oracle.start_mission(T.read, path, g.seed, g.expansion)
		local bytes = game:save(g.mission, DATA)
		local loaded = oracle.start_mission(T.read, path, g.seed, g.expansion):load(bytes, g.mission, DATA)
		if table.concat(oracle.lines(loaded), "\n") ~= table.concat(oracle.lines(game), "\n") then
			bad[#bad + 1] = name
		end
	end
	T.ok(#bad == 0, "initial states changed by a save and a load: " .. table.concat(bad, ", "))
end

function S.loads_reject_other_saves(T)
	if not ready(T) then return end
	local function fresh() return oracle.start_mission(T.read, "generated/scenarios/marine_group.kpm", 1, 0) end
	local bytes = fresh():save("a", DATA)
	local function load_into(game, b, mission, data) return pcall(game.load, game, b, mission, data) end
	local ok, err = load_into(fresh(), bytes, "b", DATA)
	T.ok(not ok and tostring(err):find("mission a"), "another mission's save is refused: " .. tostring(err))
	ok, err = load_into(fresh(), bytes, "a", "other")
	T.ok(not ok and tostring(err):find("other game data"), "a save of other data is refused: " .. tostring(err))
	local damaged = bytes:sub(1, 100) .. string.char((bytes:byte(101) + 1) % 256) .. bytes:sub(102)
	ok, err = load_into(fresh(), damaged, "a", DATA)
	T.ok(not ok and tostring(err):find("fingerprint"), "a damaged save is refused: " .. tostring(err))
	ok, err = load_into(fresh(), bytes:sub(1, 50), "a", DATA)
	T.ok(not ok, "a truncated save is refused: " .. tostring(err))
	-- A save whose header passes but whose state does not fit the game (another map under the same names) is refused
	-- before anything is written: the game keeps its state.
	local other = oracle.start_mission(T.read, "generated/missions/" .. load(T, "initial_state/" .. load(T,
		"initial_state/index")[1]).mission .. ".kpm", 1, 0)
	local before = table.concat(oracle.lines(other), "\n")
	ok, err = load_into(other, bytes, "a", DATA)
	T.ok(not ok, "a save of another map is refused: " .. tostring(err))
	T.eq(table.concat(oracle.lines(other), "\n"), before, "the refused save changed nothing")
	-- The default data identity is the game's files (g.data_identity): it travels with a save made without one.
	local game = fresh()
	T.ok(game.data_identity:match("^%x+%-%x+%-%x+%-%x+%-%x+%-%x+$"), "the identity: " .. tostring(game.data_identity))
	T.ok(load_into(fresh(), game:save("a"), "a"), "the same files load")
	ok, err = load_into(other, game:save("a"), "a")
	T.ok(not ok and tostring(err):find("other game data"), "other files are refused: " .. tostring(err))
end

return S
