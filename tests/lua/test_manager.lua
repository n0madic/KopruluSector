-- game/simulation/manager.lua: the mission manager over the native game. The clock, the command queue, the event
-- delivery and a stopping step run on a stand-in core; the saves and loads on a real game of a scenario map
-- (needs the converter's generated data and the kp_core module; skipped without them).
local events = require("game.simulation.events")
local manager = require("game.simulation.manager")
local native_only = require("tests.lua.native_only")
local oracle = require("tools.lua.oracle")
local save = require("game.save")

local MAP = "generated/scenarios/marine_group.kpm"
local SEED = 0x12345678
local STEP_S = 0.042 -- the "fastest" speed

local S = {}

-- A core whose games record their frames and return the queued events; raise_at makes that frame raise.
local function stand_in(raise_at)
	local core = { games = {} }
	core.game = {
		EVENT_FIELDS = 7, -- kp_core's layout of events(out)
		new = function(blobs, options)
			local g = { blobs = blobs, options = options, img = {}, local_player = 3, frame = 0, turn = 0, frames = {},
				queued = {}, lead = 0 }
			function g:prime() self.primed = true end
			function g:briefing_lead() return self.lead end
			function g:briefing_objectives(id) self.objectives = (self.objectives or "") .. id .. (self.primed and "p" or "") end
			function g:run_turn()
				self.frame, self.turn = self.frame + 1, self.turn + 1
			end
			function g:run_frame(commands)
				self.frame, self.turn = self.frame + 1, self.turn + 1
				self.frames[#self.frames + 1] = commands
				if self.frame == raise_at then error("stand-in failure") end
			end
			function g:events(out) -- the flat layout of kp_core's events(out)
				for k, e in ipairs(self.queued) do
					local o = (k - 1) * core.game.EVENT_FIELDS
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

function S.game_speed_and_mission_index(T)
	local m = manager.new({ mission = "m" }, { core = stand_in(), speed = 5, mission_index = 12 })
	T.eq(m.game.options.speed, 4, "the game's speed byte counts from 0")
	T.eq(m.game.options.mission_index, 12)
	m:set_speed(7)
	T.eq(m.clock.speed, 7)
	T.eq(m.game.speed, 6)
end

function S.trigger_interface_calls(T)
	local m = manager.new({ mission = "m" }, { core = stand_in(), objectives = { 13, 14 } })
	T.eq(m.game.objectives, "1314") -- before the prime
	-- without them, the briefing's first tick's
	local core = stand_in()
	local new = core.game.new
	function core.game.new(...)
		local g = new(...)
		g.lead = 21
		return g
	end
	T.eq(manager.new({ mission = "m" }, { core = core }).game.objectives, "21")
	T.eq(manager.new({ mission = "m" }, { core = stand_in() }).game.objectives, nil, "a briefing without objectives")
	local calls = {}
	local skippable = false
	function m.game:trigger_release_wait() calls[#calls + 1] = "release" end
	function m.game:trigger_skippable() return skippable end
	function m.game:trigger_skip() calls[#calls + 1] = "skip" end
	T.eq(m:can_skip(), false)
	skippable = true
	T.eq(m:can_skip(), true)
	m:release_wait()
	m:skip()
	T.eq(table.concat(calls, ","), "release,skip")
end

function S.starts_and_primes_the_game(T)
	local blobs = { mission = "m" }
	local m = manager.new(blobs, { core = stand_in(), seed = 7, expansion = 1, name = "x" })
	T.ok(m.failed == nil)
	T.ok(m.game.primed, "primed")
	T.eq(m.game.blobs, blobs)
	T.eq(m.blobs, nil, "the game keeps its copies, the manager no files")
	T.eq(m.game.options.seed, 7)
	T.eq(m.game.options.expansion, 1)
	T.eq(m.tick, 0)
	T.ok(manager.new(blobs, { core = { game = { new = function() error("bad mission") end } } }).failed:find("bad mission"),
		"a game that does not start")
	-- Without a game (here as without the kp_core module: no core either) there is nothing to drain or advance.
	local none = manager.new(blobs, { core = { game = { new = function() error("no game") end } } })
	none.core = nil
	local drained = 0
	none.events:drain(function() drained = drained + 1 end)
	T.eq(drained, 0, "no events without a game")
	T.eq(none:advance(1), 0)
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
		{ kind = events.UNIT_UNPLACED, a = 3, b = 0, c = 0, text = "hello" },
	}
	m:step()
	local out = collect(m.events)
	T.eq(#out, 3)
	T.eq(out[1][1], events.SOUND)
	T.eq(out[1][2], 1, "stamped with the tick of the emission")
	T.eq(out[1][4], 300)
	T.eq(out[2][1], events.ERROR)
	T.eq(out[2][4], 870)
	T.eq(out[3][1], events.UNIT_UNPLACED)
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
	local ran, fraction = m:advance(10 + 0.5 * STEP_S)
	T.eq(ran, 0, "the clock runs no steps")
	T.eq(fraction, 1, "the last state stands")
	T.eq(#m.game.frames, 3)
	function m.game:save() return "save" end
	local ok, err = pcall(m.save, m)
	T.ok(not ok and tostring(err):find("stopped in a step", 1, true), "a stopped game has no save: " .. tostring(err))
end

local function ready(T)
	return native_only.ready(T, "test_manager.saves", { oracle.DATA .. "dat.kpd", MAP })
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
	local opts = { seed = SEED, name = MAP } -- the data identity is the game's files
	local m = manager.new(blobs, opts)
	T.ok(m.failed == nil, tostring(m.failed))
	order(m)
	for _ = 1, 20 do m:step() end
	local bytes = m:save()
	local tick = m.tick
	for _ = 1, 40 do m:step() end
	local expected = m.game:dump_crcs()

	-- The save goes into a game that has run on: the same game and world objects take it.
	local other = manager.new(blobs, opts)
	for _ = 1, 70 do other:step() end
	other:set_speed(3)
	local world = other.world
	other:command("\9\0")
	other:load(bytes)
	T.eq(other.world, world, "the world stays, for the presentation")
	T.eq(other.tick, tick, "the saved tick")
	T.eq(other.game.speed, 2, "the player's speed, not the saved game's")
	other:set_speed(m.clock.speed)
	T.eq(#other.pending, 0, "the queued commands are dropped")
	for _ = 1, 40 do other:step() end
	T.ok(other.failed == nil, tostring(other.failed))
	local got = other.game:dump_crcs()
	for category, crc in pairs(expected) do T.eq(got[category], crc, category) end
	T.eq(other:save(), m:save(), "the same save")

	local game, before = other.game, other.game:dump_crcs()
	local foreign = manager.new(blobs, { seed = SEED, name = "another mission" })
	T.ok(not pcall(foreign.load, foreign, bytes), "another mission's save is refused")
	T.ok(not pcall(other.load, other, bytes:sub(1, 100)), "a truncated save is refused")
	local changed = manager.new(blobs, { seed = SEED, name = MAP, data = "other data" })
	T.ok(not pcall(changed.load, changed, bytes), "a save of other data is refused")
	T.eq(other.game, game, "the game stays")
	for category, crc in pairs(before) do T.eq(other.game:dump_crcs()[category], crc, "unchanged " .. category) end
end

-- A game save (game/save.lua) keeps the commands queued for the next step and the campaign; its load runs on with
-- them as the saved game would have, and refuses a save of another data set before the game changes.
function S.a_game_save_keeps_the_queued_commands_and_the_campaign(T)
	if not ready(T) then return end
	local blobs = oracle.blobs(T.read, MAP)
	local opts = { seed = SEED, name = MAP }
	local identity = save.identity(blobs.dat)
	local m = manager.new(blobs, opts)
	order(m) -- the right click is queued, not run
	local campaign = { run = { expansion = 1, row = { table = 5, index = 3 }, mission = 42, pending = "Terran05a.scx" },
		profile = { original = { [0] = 0, [1] = 12, [2] = 0 }, expansion = { [0] = 0, [1] = 43, [2] = 0 } } }
	local bytes = save.encode(m, identity, campaign, { portrait_ms = 1500 })
	local mission, carried = save.mission(bytes, identity)
	T.eq(mission, MAP, "the save names its mission (the glue's Load Saved starts it)")
	T.eq(carried.run.mission, 42)
	for _ = 1, 30 do m:step() end
	local expected = m.game:dump_crcs()

	local other = manager.new(blobs, opts)
	for _ = 1, 9 do other:step() end
	local got, pres = save.load(other, bytes, identity)
	T.eq(pres.portrait_ms, 1500, "the presentation's state")
	T.eq(#other.pending, 1, "the queued command")
	T.eq(got.run.mission, 42)
	T.eq(got.run.row.table, 5)
	T.eq(got.run.row.index, 3)
	T.eq(got.run.pending, "Terran05a.scx")
	T.eq(got.profile.expansion[1], 43)
	T.eq(got.profile.original[1], 12)
	for _ = 1, 30 do other:step() end
	for category, crc in pairs(expected) do T.eq(other.game:dump_crcs()[category], crc, category) end
	T.eq(save.encode(other, identity, got), save.encode(m, identity, got), "the same save again")

	local none = save.decode(save.encode(other, identity, nil), identity)
	T.eq(none.campaign, nil, "no campaign")
	local before = other.game:dump_crcs()
	T.ok(not pcall(save.load, other, bytes, { profile_id = identity.profile_id, source_hash = string.rep("x", 32) }),
		"a save of another data set is refused")
	T.ok(not pcall(save.load, other, bytes:sub(1, #bytes - 8) .. "damaged!", identity), "a damaged save is refused")
	for category, crc in pairs(before) do T.eq(other.game:dump_crcs()[category], crc, "unchanged " .. category) end
end

-- A save file is replaced whole or not at all: the new bytes land through a temporary file, a failure names its
-- reason and leaves the old file, and a rename that fails never loses the new save.
function S.a_save_file_is_written_whole(T)
	local path = os.tmpname()
	T.ok(save.write_file(path, "first"), "written")
	T.ok(save.write_file(path, "second"), "replaced")
	local f = assert(io.open(path, "rb"))
	T.eq(f:read("*a"), "second")
	f:close()
	T.eq(io.open(path .. ".tmp", "rb"), nil, "no temporary file stays")
	local ok, reason = save.write_file(path .. "/no/such/dir/file", "x")
	T.eq(ok, nil)
	T.ok(type(reason) == "string", "the reason: " .. tostring(reason))
	T.eq(io.open(path, "rb") ~= nil, true, "the old file stays")
	os.remove(path)
	-- The renames on a file system in memory: one that replaces the target (POSIX), one that refuses an existing target
	-- (Windows) and one that always fails.
	local function memory(rename_mode)
		local files = { save = "old" }
		local fs = {
			open = function(name)
				return { write = function(_, d) files[name] = d return true end, close = function() return true end }
			end,
			remove = function(name) files[name] = nil return true end,
			rename = function(from, to)
				if rename_mode == "fails" or (rename_mode == "refuses" and files[to]) then return nil, "rename refused" end
				files[to], files[from] = files[from], nil
				return true
			end,
		}
		return files, fs
	end
	for _, mode in ipairs({ "replaces", "refuses" }) do
		local files, fs = memory(mode)
		T.ok(save.write_file("save", "new", fs), mode)
		T.eq(files.save, "new", mode .. ": the new save")
		T.eq(files["save.tmp"], nil, mode .. ": no temporary file stays")
	end
	local files, fs = memory("fails")
	ok, reason = save.write_file("save", "new", fs)
	T.eq(ok, nil, "a failed rename fails the write")
	T.eq(files["save.tmp"], "new", "the new save stays in the temporary file")
	T.ok(tostring(reason):find("save.tmp", 1, true) ~= nil, "the reason names it: " .. tostring(reason))
end

return S
