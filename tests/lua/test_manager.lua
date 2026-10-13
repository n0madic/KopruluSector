-- game/simulation/manager.lua: the mission manager over the native game. The clock, the command queue, the event
-- delivery and a stopping step run on a stand-in core; the saves and loads on a real game of a scenario map
-- (needs the converter's generated data and the kp_core module; skipped without them).
local events = require("game.simulation.events")
local manager = require("game.simulation.manager")
local native = require("game.native")
local oracle = require("tools.lua.oracle")

local MAP = "generated/scenarios/marine_group.kpm"
local SEED = 0x12345678
local STEP_S = 0.042 -- the "fastest" speed

local S = {}

-- A core whose games record their frames and return the queued events; raise_at makes that frame raise.
local function stand_in(raise_at)
	local core = { games = {} }
	core.game = {
		new = function(blobs, options)
			local g = { blobs = blobs, options = options, img = {}, local_player = 3, frame = 0, frames = {}, queued = {} }
			function g:prime() self.primed = true end
			function g:run_turn() self.frame = self.frame + 1 end
			function g:run_frame(commands)
				self.frame = self.frame + 1
				self.frames[#self.frames + 1] = commands
				if self.frame == raise_at then error("stand-in failure") end
			end
			function g:events(out) -- the flat layout of kp_core's events(out)
				for k, e in ipairs(self.queued) do
					local o = (k - 1) * manager.EVENT_FIELDS
					out[o + 1], out[o + 2], out[o + 3], out[o + 4] = e.kind, self.frame, k, e.a
					out[o + 5], out[o + 6], out[o + 7] = e.b, e.c, e.text or false
				end
				local n = #self.queued
				self.queued = {}
				return n
			end
			core.games[#core.games + 1] = g
			return g
		end,
	}
	return core
end

local function collect(ring)
	local out = {}
	ring:drain(function(kind, tick, _, a, b, c, _, text) out[#out + 1] = { kind, tick, a, b, c, text } end)
	return out
end

function S.starts_and_primes_the_game(T)
	local blobs = { mission = "m" }
	local m = manager.new(blobs, { core = stand_in(), seed = 7, expansion = 1, name = "x" })
	T.ok(m.failed == nil)
	T.ok(m.game.primed, "primed")
	T.eq(m.game.blobs, blobs)
	T.eq(m.game.options.seed, 7)
	T.eq(m.game.options.expansion, 1)
	T.eq(m.tick, 0)
	T.ok(manager.new(blobs, { core = { game = { new = function() error("bad mission") end } } }).failed:find("bad mission"),
		"a game that does not start")
end

function S.the_wall_clock_runs_the_steps(T)
	local m = manager.new({}, { core = stand_in() })
	T.eq(m:advance(100), 0, "the first call sets the reference")
	local ran, fraction = m:advance(100 + 3.5 * STEP_S)
	T.eq(ran, 3)
	T.ok(math.abs(fraction - 0.5) < 1e-6, "the fraction")
	T.eq(m.tick, 3)
	T.eq(m:advance(200), m.clock.max_catchup, "the catch-up limit")
	m:pause()
	T.eq(m:advance(300), 0, "paused")
	m:resume()
	T.eq(m:advance(400), 0, "the time away is dropped")
	T.eq(m:advance(400 + 1.5 * STEP_S), 1)
end

function S.commands_go_out_in_the_next_step(T)
	local m = manager.new({}, { core = stand_in() })
	m:command("\9\1\0\0")
	m:command("\20", 5)
	m:step()
	m:step()
	local first = m.game.frames[1]
	T.eq(#first, 2)
	T.eq(first[1].player, 3, "the local player's")
	T.eq(first[1].bytes, "\9\1\0\0")
	T.eq(first[2].player, 5)
	T.eq(#m.game.frames[2], 0, "sent once")
end

function S.the_games_events_reach_the_presentation(T)
	local m = manager.new({}, { core = stand_in() })
	m.game.queued = {
		{ kind = events.SOUND, a = 12, b = 300, c = 400 },
		{ kind = events.ERROR, a = 3, b = 870, c = 0 },
		{ kind = events.TEXT, a = 3, b = 0, c = 0, text = "hello" },
	}
	m:step()
	local out = collect(m.events)
	T.eq(#out, 3)
	T.eq(out[1][1], events.SOUND)
	T.eq(out[1][2], 1, "stamped with the tick of the emission")
	T.eq(out[1][4], 300)
	T.eq(out[2][1], events.ERROR)
	T.eq(out[2][4], 870)
	T.eq(out[3][1], events.TEXT)
	T.eq(out[3][6], "hello")
	T.eq(out[1][6], nil, "no text")
	m.game.queued = { { kind = events.SOUND, a = 1, b = 2, c = 3 }, { kind = events.SOUND, a = 4, b = 5, c = 6 } }
	local seen = {}
	T.ok(not pcall(m.events.drain, m.events, function(_, _, _, a)
		seen[#seen + 1] = a
		error("consumer")
	end), "a raising consumer")
	m.events:drain(function(_, _, _, a) seen[#seen + 1] = a end)
	T.eq(table.concat(seen, ","), "1,4", "the later event stays for the next drain")
end

function S.a_raising_step_stops_the_game(T)
	local m = manager.new({}, { core = stand_in(4) }) -- run_turn is frame 1
	T.ok(m:step() and m:step())
	T.ok(not m:step(), "the third step raises")
	T.ok(m.failed:find("stand-in failure", 1, true), m.failed)
	T.ok(not m:step(), "no further steps")
	m:advance(0)
	T.eq(m:advance(10), 0, "the clock runs no steps")
	T.eq(#m.game.frames, 3)
end

local function ready(T)
	if T.has(oracle.DATA .. "dat.kpd") and T.has(MAP) and native.find("kp_core") then return true end
	T.env.log("SKIP test_manager: no generated data or no kp_core module")
	return false
end

-- The Marines of the scenario walk away: a select command of the first unit and a right click.
local function order(m)
	local g = m.game
	local n = g.lists.active.head
	local ref = g:unit_ref(n)
	m:command(string.char(0x09, 1, ref % 256, math.floor(ref / 256)))
	m:step()
	m:command(string.char(0x14, 0x14, 0x05, 0x56, 0x04, 0, 0, 0xe4, 0, 0)) -- to (1300, 1110)
end

function S.a_loaded_save_continues_as_the_saved_game(T)
	if not ready(T) then return end
	local blobs = oracle.blobs(T.read, MAP)
	local opts = { seed = SEED, name = MAP, data = "test data" }
	local m = manager.new(blobs, opts)
	T.ok(m.failed == nil, tostring(m.failed))
	order(m)
	for _ = 1, 20 do m:step() end
	local bytes = m:save()
	local tick = m.tick
	for _ = 1, 40 do m:step() end
	local expected = m.game:dump_crcs()

	local other = manager.new(blobs, opts)
	other:command("\9\0")
	other:load(bytes)
	T.eq(other.tick, tick, "the saved tick")
	T.eq(#other.pending, 0, "the queued commands are dropped")
	for _ = 1, 40 do other:step() end
	T.ok(other.failed == nil, tostring(other.failed))
	local got = other.game:dump_crcs()
	for category, crc in pairs(expected) do T.eq(got[category], crc, category) end
	T.eq(other:save(), m:save(), "the same save")

	local game = other.game
	local foreign = manager.new(blobs, { seed = SEED, name = "another mission", data = "test data" })
	T.ok(not pcall(foreign.load, foreign, bytes), "another mission's save is refused")
	T.ok(not pcall(other.load, other, bytes:sub(1, 100)), "a truncated save is refused")
	T.eq(other.game, game, "the game stays")
end

return S
