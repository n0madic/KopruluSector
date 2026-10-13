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
		paused = false,
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

-- Call on focus loss, iconify or app suspend.
function Clock:pause()
	self.paused = true
end

-- Call on focus gain / resume: the time spent away is discarded.
function Clock:resume()
	self.paused = false
	self.accumulator_ms = 0
	self.last_wall = nil
	self.resets = self.resets + 1
end

-- Window events (plan D8): iconifying (a suspend) pauses the clock and the return resumes it, discarding the time
-- away; with `autopause` (mobile devices) losing the focus pauses it too, and regaining it resumes.
M.FOCUS_LOST, M.FOCUS_GAINED = "focus_lost", "focus_gained"
M.ICONIFIED, M.DEICONIFIED = "iconified", "deiconified"

function Clock:on_window(event, autopause)
	if event == M.ICONIFIED or (event == M.FOCUS_LOST and autopause) then
		self:pause()
	elseif self.paused and (event == M.DEICONIFIED or event == M.FOCUS_GAINED) then
		self:resume()
	end
end

-- Advances to wall time `now` (seconds). The first call after creation or resume only
-- sets the reference. Returns the same values as advance.
function Clock:advance_wall(now)
	local last = self.last_wall
	self.last_wall = now
	if self.paused or not last then return 0, self.accumulator_ms / M.SPEED_MS[self.speed] end
	return self:advance(now - last)
end

-- Advances by a rendered frame of `dt` seconds. Returns the number of logical steps
-- to run now and the interpolation fraction in [0, 1) for presentation.
function Clock:advance(dt)
	if self.paused then return 0, 0 end
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
