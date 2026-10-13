-- Saves at a step boundary (version 1, kp_core game:save/load; plan D17, ADR 0008): a game saved in the middle
-- of every scenario of tests/golden/oracle/scenarios/ and loaded into the same mission fresh from its start
-- continues exactly as the original did (the per-step CRCs of its state dumps), and saving the loaded game gives
-- the same bytes. The initial state of every mission survives a save and a load unchanged. Needs the converter's
-- generated data and the native core; skipped without them.
local native = require("game.native")
local oracle = require("tools.lua.oracle")

local DIR = "tests/golden/oracle/"
local DATA = "test data" -- the tests load saves into the same generated data

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

local function ready(T)
	if T.has(oracle.DATA .. "dat.kpd") and native.find("kp_core") then return true end
	T.env.log("SKIP test_save: no " .. oracle.DATA .. " or no kp_core module")
	return false
end

function S.saved_scenarios_continue_as_the_original(T)
	if not ready(T) then return end
	local bad = {}
	for _, name in ipairs(load(T, "scenarios/index")) do
		local g = load(T, "scenarios/" .. name)
		local path = "generated/" .. g.mission .. ".kpm"
		local commands = commands_of(g.commands)
		local last = 0
		for step in pairs(g.steps) do last = math.max(last, step) end
		local mid = math.floor(last / 2)
		local game = oracle.start_mission(T.read, path, g.seed, g.expansion)
		local run = oracle.runner(game, commands, g.ui)
		run.to(mid)
		local bytes = game:save(g.mission, DATA)
		local loaded = oracle.start_mission(T.read, path, g.seed, g.expansion):load(bytes, g.mission, DATA)
		if loaded:save(g.mission, DATA) ~= bytes then bad[#bad + 1] = name .. " round trip" end
		local cont = oracle.runner(loaded, commands, g.ui)
		cont.step, cont.next_command, cont.next_ui = run.step, run.next_command, run.next_ui
		for step = mid + 1, last do
			cont.to(step)
			local want = g.steps[step]
			if want then
				local crcs = oracle.state_crcs(loaded, step >= oracle.DRAWN_STEP)
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
end

return S
