-- Wall-clock → logical-step scheduler for the mission manager (plan D8, §0.2).
--
-- The simulation itself never reads the wall clock (verified for the original: the
-- trigger elapsed counter and waits advance per logical step, docs/research
-- full-game-emulation.md). This clock only decides how many logical steps to run in
-- a rendered frame and the interpolation fraction for presentation.
--   * Step length comes from the profile speed table 0x503360:
--     167/111/83/67/56/48/42 ms (Slowest..Fastest).
--   * At most `max_catchup` steps run per frame; the excess backlog is dropped so a
--     stall never turns into a burst of hundreds of steps.
--   * Focus loss / suspend pauses the clock; resume resets the accumulator and the
--     wall-time reference, so time spent in the background is not replayed.
--   * Feed it measured wall time (advance_wall with socket.gettime()): spike 14 showed
--     that Defold's update dt after a 1 s stall is ~0.01 s, so dt cannot drive catch-up.
local M = {}

M.SPEED_MS = { 167, 111, 83, 67, 56, 48, 42 } -- index 1 = Slowest, 7 = Fastest
M.DEFAULT_SPEED = 7
M.DEFAULT_MAX_CATCHUP = 4

local Clock = {}
Clock.__index = Clock

function M.new(opts)
	opts = opts or {}
	local self = setmetatable({
		speed = opts.speed or M.DEFAULT_SPEED,
		max_catchup = opts.max_catchup or M.DEFAULT_MAX_CATCHUP,
		accumulator_ms = 0,
		paused = false,     -- the game's pause (the player's, or the autopause): resume() ends it
		suspended = false,  -- the window is iconified: its return ends it
		steps = 0,          -- logical steps issued since creation
		dropped_ms = 0,     -- backlog discarded by the catch-up limit
		resets = 0,
	}, Clock)
	return self
end

function Clock:step_ms()
	return M.SPEED_MS[self.speed]
end

function Clock:set_speed(speed)
	assert(M.SPEED_MS[speed], "unknown game speed " .. tostring(speed))
	self.speed = speed
end

-- Discards the accumulated time and the wall-time reference (the time spent away, a load).
function Clock:reset()
	self.accumulator_ms = 0
	self.last_wall = nil
	self.resets = self.resets + 1
end

-- The game's pause; it lasts until resume(), whatever the window does.
function Clock:pause()
	self.paused = true
end

-- Ends the game's pause: the time spent paused is discarded.
function Clock:resume()
	self.paused = false
	self:reset()
end

-- Window events (plan D8): iconifying suspends the clock and the return ends the suspension, discarding the time
-- away; a suspension never ends the game's pause. With `autopause` (mobile devices) losing the focus pauses the
-- game, and it stays paused until its owner resumes it.
M.FOCUS_LOST, M.FOCUS_GAINED = "focus_lost", "focus_gained"
M.ICONIFIED, M.DEICONIFIED = "iconified", "deiconified"

function Clock:on_window(event, autopause)
	if event == M.ICONIFIED then
		self.suspended = true
	elseif event == M.DEICONIFIED and self.suspended then
		self.suspended = false
		self:reset()
	elseif event == M.FOCUS_LOST and autopause then
		self:pause()
	end
end

-- Whether the clock stands: paused or suspended.
function Clock:stopped()
	return self.paused or self.suspended
end

-- Advances to wall time `now` (seconds). The first call after creation or resume only
-- sets the reference. Returns the same values as advance.
function Clock:advance_wall(now)
	local last = self.last_wall
	self.last_wall = now
	if self:stopped() or not last then return 0, self.accumulator_ms / M.SPEED_MS[self.speed] end
	return self:advance(now - last)
end

-- Advances by a rendered frame of `dt` seconds. Returns the number of logical steps
-- to run now and the interpolation fraction in [0, 1) for presentation.
function Clock:advance(dt)
	if self:stopped() then return 0, 0 end
	if dt < 0 then dt = 0 end
	local step = M.SPEED_MS[self.speed]
	local acc = self.accumulator_ms + dt * 1000
	local n = math.floor(acc / step)
	if n > self.max_catchup then
		local excess = (n - self.max_catchup) * step
		self.dropped_ms = self.dropped_ms + excess
		acc = acc - excess
		n = self.max_catchup
	end
	acc = acc - n * step
	self.accumulator_ms = acc
	self.steps = self.steps + n
	return n, acc / step
end

return M
