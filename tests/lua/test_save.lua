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
S.SHARDS = { saved_scenarios_continue_as_the_original = 6, saved_plays_continue_as_the_original = 8 }

local function load(T, path)
	return assert((loadstring or load)(T.read(DIR .. path .. ".lua")))()
end

local function ready(T)
	return native_only.ready(T, "test_save", { oracle.DATA .. "dat.kpd" })
end

-- Loads the save of `game` (golden g's game, run to step `mid` by `run`) into games of the same mission run to each step
-- of `intos`; each must save the same bytes again and continue as the original's per-step CRCs up to step `stop`.
local function continue_from(T, name, g, game, run, mid, intos, bad, stop)
	local path = "generated/" .. g.mission .. ".kpm"
	local commands = oracle.golden_commands(g.commands)
	local last = oracle.last_step(g)
	local bytes = game:save(g.mission, DATA)
	for _, into in ipairs(intos) do -- the game that takes the save: fresh, or run on to the end
		local loaded = oracle.start_mission(T.read, path, g.seed, g.expansion)
		oracle.runner(loaded, commands, g.ui).to(into == "last" and last or into)
		loaded:load(bytes, g.mission, DATA)
		if loaded:save(g.mission, DATA) ~= bytes then bad[#bad + 1] = name .. " round trip" end
		local cont = oracle.runner(loaded, commands, g.ui)
		cont.step, cont.next_command, cont.next_ui = run.step, run.next_command, run.next_ui
		for step = mid + 1, math.min(stop or last, last) do
			cont.to(step)
			local want = g.steps[step]
			if want then
				local crcs = oracle.state_crcs(loaded, step >= oracle.DRAWN_STEP)
				for _, category in ipairs(oracle.step_differences(crcs, want)) do
					bad[#bad + 1] = ("%s into %s step %d %s"):format(name, into, step, category)
				end
				if #bad > 0 then break end
			end
		end
	end
end

-- golden g's game started and run to step `mid`, with its runner.
local function game_at(T, g, mid)
	local game = oracle.start_mission(T.read, "generated/" .. g.mission .. ".kpm", g.seed, g.expansion)
	local run = oracle.runner(game, oracle.golden_commands(g.commands), g.ui)
	run.to(mid)
	return game, run
end

-- Saves golden g's game at step `mid` and loads it into games of the same mission run to each step of `intos`.
local function check_continuations(T, name, g, mid, intos, bad)
	local game, run = game_at(T, g, mid)
	continue_from(T, name, g, game, run, mid, intos, bad)
end

function S.saved_scenarios_continue_as_the_original(T)
	if not ready(T) then return end
	local bad = {}
	for k, name in ipairs(load(T, "scenarios/index")) do
		if T.mine(k) then
			local g = load(T, "scenarios/" .. name)
			check_continuations(T, name, g, math.floor(oracle.last_step(g) / 2), { 1, "last" }, bad)
		end
	end
	T.ok(#bad == 0, "continuations after a load differ: " .. table.concat(bad, ", "))
end

-- terran01 played to victory, saved while its Barracks is being built, Marines trained and SCVs harvest (plan phase
-- 6 item 7), loaded into a fresh game.
function S.saved_wasteland_continues_as_the_original(T)
	if not ready(T) then return end
	local bad = {}
	check_continuations(T, "terran-wasteland", load(T, "plays/terran-wasteland"), WASTELAND_SAVE_STEP, { 1 }, bad)
	T.ok(#bad == 0, "continuations after a load differ: " .. table.concat(bad, ", "))
end

-- The scripted wins of the campaign rows (plan phase 8 item 4): each saved at every PLAY_SAVE_EVERY-th step and loaded
-- into a fresh game, which continues as the original did up to the next save step; the first save continues to the
-- play's end (its fights, productions, transports and the AI's waves in between). One game per play runs on from save
-- to save.
local PLAY_SAVE_EVERY = 2000
local STATUS_COMPLETED, STATUS_IN_TRANSPORT = 0x1, 0x40
local NO_UNIT = 228 -- an empty queue entry
local AI_ROSTER_SLOTS = 8 * 64 -- the players' attack rosters (g.pai.roster)

-- The unfinished processes of plan phase 8 item 4 the play saves must catch: where a save of a play catches each
-- (found by scanning the plays at every PLAY_SAVE_EVERY-th step; the test below keeps them true).
local INTERRUPTED = {
	construction = { play = "protoss-m22", step = 2000 }, training = { play = "protoss-m22", step = 2000 },
	cargo = { play = "protoss-m29", step = 2000 }, hidden = { play = "protoss-m22", step = 2000 },
	ai_roster = { play = "protoss-m22", step = 2000 },
}

-- The unfinished processes in game g at a save: a building under construction (or warping, morphing), a queue
-- training, units in a transport or a Bunker, units in the hidden list (a worker in a refinery, a unit being made),
-- a computer's attack roster gathering. Sets found[kind] = true.
local function interrupted(g, found)
	local u, n = g.u, g.lists.active.head
	while n ~= 0 do
		if bit.band(u.status[n], STATUS_COMPLETED) == 0 then found.construction = true end
		if u.queue0[n] ~= NO_UNIT and u.queue0[n] ~= 0 and bit.band(u.status[n], STATUS_COMPLETED) ~= 0 then
			found.training = true
		end
		if bit.band(u.status[n], STATUS_IN_TRANSPORT) ~= 0 or u.loaded0[n] ~= 0 then found.cargo = true end
		n = u.next[n]
	end
	if g.lists.hidden.head ~= 0 then found.hidden = true end
	for k = 0, AI_ROSTER_SLOTS - 1 do
		if g.pai.roster[k] ~= 0 then found.ai_roster = true break end
	end
end

function S.saved_plays_continue_as_the_original(T)
	if not ready(T) or not T.has(DIR .. "plays/index.lua") then return end
	local bad = {}
	for k, name in ipairs(load(T, "plays/index")) do
		if T.mine(k) then
			local g = load(T, "plays/" .. name)
			local last = oracle.last_step(g)
			local game, run = game_at(T, g, 1)
			for mid = PLAY_SAVE_EVERY, last - 1, PLAY_SAVE_EVERY do
				run.to(mid)
				continue_from(T, name, g, game, run, mid, { 1 }, bad, mid == PLAY_SAVE_EVERY and last or mid + PLAY_SAVE_EVERY)
			end
		end
	end
	T.ok(#bad == 0, "continuations after a load differ: " .. table.concat(bad, ", "))
end

-- The play saves between them catch every kind of unfinished process of INTERRUPTED (plan phase 8 item 4); a shard of
-- the test above sees only some plays, so this test runs the listed saves of its own, whichever shard runs it.
function S.saved_plays_catch_every_interrupted_process(T)
	if not ready(T) or not T.has(DIR .. "plays/index.lua") then return end
	local found = {} -- by "play@step": the processes of that save, each save's game run once
	for kind, at in pairs(INTERRUPTED) do
		local key = at.play .. "@" .. at.step
		if not found[key] then
			found[key] = {}
			interrupted(game_at(T, load(T, "plays/" .. at.play), at.step), found[key])
		end
		T.ok(found[key][kind], ("%s at step %d: a save in the middle of %s"):format(at.play, at.step, kind))
	end
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
