-- Simulation -> presentation/audio/UI events (plan D15): the event kinds, and a ring of SoA records (kind,
-- tick, sequence number, four numeric arguments) with no allocation in steady state for the viewer's animated map
-- scene (a native game keeps its events in kp_core's ring, game/simulation/manager.lua). The
-- simulation emits during its steps; presentation drains the events of every step it
-- ran since the last frame. One-shot events carry what their consumer needs (a sound's
-- position at emission), so freeing the source later loses nothing.
local M = {}

-- Kinds and their arguments: the native game's event kinds (native/kp_core/src/core/kp_game.h KP_EVENT_*).
M.SOUND = 1 -- a = sfx id, b = x, c = y (map pixels; PlayImageSound 0x40f5a0)
M.ERROR = 2 -- a = player, b = string id (the local player's error notifications, 0x46ae50)
M.TEXT = 3 -- a = player; the text
M.SOUND_SCREEN = 4 -- a = sfx id; a sound without a position (PlaySoundAtUnit 0x446940 without a unit)

-- Default capacity: events of DEFAULT_MAX_CATCHUP steps (game/simulation/clock.lua) with
-- room for every image record playing a sound in each.
M.DEFAULT_CAPACITY = 4 * 5220

local Ring = {}
Ring.__index = Ring

function M.new(capacity)
	capacity = capacity or M.DEFAULT_CAPACITY
	local self = setmetatable({ capacity = capacity, first = 0, count = 0, sequence = 0,
		kind = {}, tick = {}, seq = {}, a = {}, b = {}, c = {}, d = {} }, Ring)
	for k = 0, capacity - 1 do
		self.kind[k], self.tick[k], self.seq[k] = 0, 0, 0
		self.a[k], self.b[k], self.c[k], self.d[k] = 0, 0, 0, 0
	end
	return self
end

-- Appends an event. A full ring raises: an event the consumer never sees would silently
-- change what the player hears or sees.
function Ring:emit(kind, tick, a, b, c, d)
	assert(self.count < self.capacity, "events: ring full; drain more often or raise the capacity")
	local k = (self.first + self.count) % self.capacity
	self.sequence = self.sequence + 1
	self.kind[k], self.tick[k], self.seq[k] = kind, tick, self.sequence
	self.a[k], self.b[k], self.c[k], self.d[k] = a or 0, b or 0, c or 0, d or 0
	self.count = self.count + 1
end

-- Calls fn(kind, tick, sequence, a, b, c, d) for every event present at the call, in
-- emission order. Each event leaves the ring before fn sees it, so an error raised by fn
-- loses that event only and the later ones stay for the next drain.
function Ring:drain(fn)
	for _ = 1, self.count do
		local k = self.first
		self.first = (k + 1) % self.capacity
		self.count = self.count - 1
		fn(self.kind[k], self.tick[k], self.seq[k], self.a[k], self.b[k], self.c[k], self.d[k])
	end
end

return M
