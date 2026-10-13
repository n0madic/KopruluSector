-- Saves at a step boundary (game/save/state.lua; plan §7 phase 3 item 1): a game saved in
-- the middle of every scenario of tests/golden/oracle/scenarios/ and loaded into the same
-- mission fresh from its start continues exactly as the original did (the per-step CRCs of
-- its state dumps), and saving the loaded game gives the same bytes. The initial state of
-- every mission survives a save and a load unchanged. Needs the converter's generated data;
-- skipped without it. The interpreters check every STRIDE-th scenario and mission and every
-- STRIDE-th step, as the other oracle suites.
local save = require("game.save.state")
local state_model = require("tools.lua.state_model")

local DIR = "tests/golden/oracle/"
local STRIDE = 8
local DATA_HASH = 0 -- the tests load saves into the same generated data

local S = {}

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

local function jit_on()
	return jit and jit.status and jit.status()
end

function S.saved_scenarios_continue_as_the_original(T)
	if not T.has(state_model.DATA .. "dat.kpd") then
		T.env.log("SKIP test_save: no " .. state_model.DATA)
		return
	end
	local stride = jit_on() and 1 or STRIDE
	local bad = {}
	local index = load(T, "scenarios/index")
	for k = 1, #index, stride do
		local name = index[k]
		local g = load(T, "scenarios/" .. name)
		local path = "generated/" .. g.mission .. ".kpm"
		local commands = commands_of(g.commands)
		local last = 0
		for step in pairs(g.steps) do last = math.max(last, step) end
		local mid = math.floor(last / 2)
		local game = state_model.start_mission(T.read, path, g.seed, g.expansion)
		local run = state_model.runner(game, commands, g.ui)
		run.to(mid)
		local bytes = save.encode(game, g.mission, DATA_HASH)
		local loaded = save.load(state_model.start_mission(T.read, path, g.seed, g.expansion), bytes, g.mission,
			DATA_HASH)
		if save.encode(loaded, g.mission, DATA_HASH) ~= bytes then bad[#bad + 1] = name .. " round trip" end
		local cont = state_model.runner(loaded, commands, g.ui)
		cont.step, cont.next_command, cont.next_ui = run.step, run.next_command, run.next_ui
		for step = mid + 1, last do
			cont.to(step)
			local want = g.steps[step]
			if want and (step % stride == 0 or step == last) then
				local crcs = state_model.crcs(state_model.lines(loaded, step >= state_model.DRAWN_STEP))
				for category, crc in pairs(want) do
					if crcs[category] ~= crc then bad[#bad + 1] = ("%s step %d %s"):format(name, step, category) end
				end
				if #bad > 0 then break end
			end
		end
	end
	T.ok(#bad == 0, "continuations after a load differ: " .. table.concat(bad, ", "))
end

function S.initial_states_survive_a_save(T)
	if not T.has(state_model.DATA .. "dat.kpd") then return end
	local stride = jit_on() and 1 or STRIDE
	local bad = {}
	local index = load(T, "initial_state/index")
	for k = 1, #index, stride do
		local g = load(T, "initial_state/" .. index[k])
		local path = "generated/missions/" .. g.mission .. ".kpm"
		local game = state_model.start_mission(T.read, path, g.seed, g.expansion)
		local bytes = save.encode(game, g.mission, DATA_HASH)
		local loaded = save.load(state_model.start_mission(T.read, path, g.seed, g.expansion), bytes, g.mission,
			DATA_HASH)
		if table.concat(state_model.lines(loaded), "\n") ~= table.concat(state_model.lines(game), "\n") then
			bad[#bad + 1] = index[k]
		end
	end
	T.ok(#bad == 0, "initial states changed by a save and a load: " .. table.concat(bad, ", "))
end

function S.loads_reject_other_saves(T)
	local snapshot = require("game.simulation.snapshot")
	local bytes = snapshot.encode({ version = save.VERSION, mission = "a", data = 1, game = {}, world = {} })
	local ok, err = pcall(save.load, { img = {} }, bytes, "b", 1)
	T.ok(not ok and tostring(err):find("mission a"), "another mission's save is refused")
	ok, err = pcall(save.load, { img = {} }, bytes, "a", 2)
	T.ok(not ok and tostring(err):find("other game data"), "a save of other data is refused")
end

return S
