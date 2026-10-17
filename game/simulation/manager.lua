-- The mission manager (plan D7, D8; ADR 0008 §6): the one owner of a mission's native game (kp_core). It starts
-- the game, runs its logical steps synchronously on the wall clock (game/simulation/clock.lua: the speed table,
-- the catch-up limit, pause and resume), queues the player's commands for the next step, hands the game's events
-- to the presentation straight from kp_core's ring (events:drain, no table per event) and saves and loads the game
-- at a step boundary.
--
-- A step that raises stops the game: the manager keeps the error (failed) and the game's last state, and runs no
-- further steps.
local clock = require("game.simulation.clock")
local native = require("game.native")

local M = {}

local Manager = {}
Manager.__index = Manager

-- The game's events for the presentation, with the drain of game/simulation/events.lua: the game's ring (kp_core)
-- is drained into one reused array, kp_core.game.EVENT_FIELDS values per event (kind, frame, sequence, a, b, c, the
-- text or false). The ring fails when full: the manager's owner drains it every rendered frame.
local Events = {}
Events.__index = Events

-- Calls fn(kind, tick, sequence, a, b, c, d, text) for every event of the game, in emission order; tick is the
-- game's frame count at the emission from the manager's tick 0, d is 0 and text nil for events without one. Each
-- event is taken before fn sees it, so an error raised by fn loses that event only and the later ones stay for the
-- next drain.
function Events:drain(fn)
	local m = self.manager
	if not m.game then return end
	local buffer, n = self.buffer, m.core.game.EVENT_FIELDS
	while true do
		if self.at > self.count then
			self.count, self.at = m.game:events(buffer), 1
			if self.count == 0 then return end
		end
		local o = (self.at - 1) * n
		self.at = self.at + 1
		fn(buffer[o + 1], buffer[o + 2] - m.start_frame, buffer[o + 3], buffer[o + 4], buffer[o + 5], buffer[o + 6], 0,
			buffer[o + 7] or nil)
	end
end

-- Drops the events taken from the game and not yet handed out.
function Events:clear()
	self.count, self.at = 0, 1
end

-- PrimeGameLoop's two object steps and RunGame's first turn (kp_core kp_prime, kp_run_turn).
local function prime(g)
	g:prime()
	g:run_turn({})
end

-- blobs: { dat =, grp =, iscript =, mission =, tileset = } (the .kpd/.kpm file contents; the game copies what it
-- keeps, so the caller may drop them). opts: seed, expansion, name (the mission's name: the game's saves carry it
-- and its loads check it), data (the converted data's identity, checked by load; by default the game's own,
-- g.data_identity: the CRC-32s of its files), speed, max_catchup, core (the kp_core module; found by game.native by
-- default), mission_index (the campaign row of the mission, 0x526524; 0 for a custom map), objectives (the string ids
-- of the briefing's mission objectives, 0x44fdc0: they carry into the game before its first step).
-- A game that does not start leaves failed set and game nil.
function M.new(blobs, opts)
	opts = opts or {}
	local self = setmetatable({
		options = { seed = opts.seed or 0, expansion = opts.expansion or 0, name = opts.name or "mission" },
		data = opts.data, -- nil: the game's data identity
		clock = clock.new({ speed = opts.speed, max_catchup = opts.max_catchup }),
		pending = {},
		tick = 0, -- logical steps run since the game's start
		loads = 0, -- saves loaded: the presentation restarts its motion when this changes
		step_ms = 0, -- CPU time of the steps (sum) and of the slowest one
		step_max_ms = 0,
	}, Manager)
	self.events = setmetatable({ manager = self, buffer = {}, count = 0, at = 1 }, Events)
	self.core = opts.core or native.find("kp_core")
	if not self.core then
		self.failed = "mission: no kp_core native extension"
		return self
	end
	local ok, g = pcall(self.core.game.new, blobs, self.options)
	if not ok then
		self.failed = tostring(g)
		return self
	end
	self.game, self.world = g, g.img
	g.speed = self.clock.speed - 1 -- the clock's speeds count from 1, the game's speed byte 0x6d3130 from 0
	g.mission_index = opts.mission_index or 0
	for _, id in ipairs(opts.objectives or {}) do g:briefing_objectives(id) end
	local primed, err = pcall(prime, g)
	if not primed then self.failed = tostring(err) end
	self.start_frame = g.frame -- the frame count at tick 0
	return self
end

-- Queues a command (its bytes as the original sends them) of `player` (the local player by default) for the next
-- step.
function Manager:command(bytes, player)
	self.pending[#self.pending + 1] = { player = player or self.game.local_player, bytes = bytes }
end

-- One logical step (an iteration of the game loop, kp_run_frame) with the queued commands. False once the game
-- has stopped.
function Manager:step()
	if self.failed then return false end
	local g, commands = self.game, self.pending
	self.pending = {}
	self.tick = self.tick + 1
	local t0 = os.clock()
	local ok, err = pcall(g.run_frame, g, commands)
	local ms = (os.clock() - t0) * 1000
	self.step_ms = self.step_ms + ms
	if ms > self.step_max_ms then self.step_max_ms = ms end
	if not ok then self.failed = tostring(err) end
	return ok
end

-- Advances the wall clock to `now` (seconds, measured: socket.gettime()) and runs the steps it is due. Returns the
-- number of steps run and the presentation's interpolation fraction (1 once the game has stopped: its last state
-- stands instead of replaying its last step's motion).
function Manager:advance(now)
	local due, fraction = self.clock:advance_wall(now)
	local ran = 0
	while ran < due and self:step() do ran = ran + 1 end
	if self.failed then return ran, 1 end
	return ran, fraction
end

-- The game's pause and the window's events (game/simulation/clock.lua: a suspension never ends a pause).
function Manager:pause() self.clock:pause() end
function Manager:resume() self.clock:resume() end
function Manager:set_speed(speed)
	self.clock:set_speed(speed)
	if self.game then self.game.speed = speed - 1 end
end

-- The interface's part of the triggers, between two steps (kp_core): the end of a transmission's portrait or of a
-- camera pan releases the local player's trigger wait, and Escape skips the transmissions when the local player has
-- running triggers it can skip (can_skip).
function Manager:release_wait() if self.game and not self.failed then self.game:trigger_release_wait() end end
function Manager:can_skip() return self.game ~= nil and not self.failed and self.game:trigger_skippable() end
function Manager:skip() if self.game and not self.failed then self.game:trigger_skip() end end
function Manager:on_window(event, autopause) self.clock:on_window(event, autopause) end

-- The save of the game at this step boundary (kp_core save format, plan D17), under the game's mission name and data
-- identity. A stopped game is not at a step boundary (its failing step stopped half-way): it has no save.
function Manager:save()
	assert(self.game, "mission: no game to save")
	if self.failed then error("mission: the game stopped in a step and cannot be saved: " .. self.failed, 2) end
	return self.game:save(nil, self.data)
end

-- Restores a save of the same mission and data into the game (kp_core loads into a game at any step), so the game
-- and its world stay the same objects for the presentation. The queued commands and the events not yet drained
-- are dropped, a stopped game runs again, the tick becomes the saved game's and the time since the last step is
-- discarded (a pause stays); `loads` counts up, so the presentation draws the loaded state at once and without motion
-- from the state before (presentation/images.lua restart). Raises on a save of another mission or data, or a damaged
-- one, and the game keeps its state.
function Manager:load(bytes)
	assert(self.game, "mission: no game to load into")
	self.game:load(bytes, nil, self.data)
	self.failed = nil
	self.pending = {}
	self.events:clear()
	self.tick = self.game.frame - self.start_frame
	self.clock:reset()
	self.loads = self.loads + 1
end

return M
