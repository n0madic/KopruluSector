-- Simulation -> presentation/audio/UI events (plan D15): the event kinds, and a ring of SoA records (kind,
-- tick, sequence number, four numeric arguments) with no allocation in steady state for the viewer's animated map
-- scene (a native game keeps its events in kp_core's ring, game/simulation/manager.lua). The
-- simulation emits during its steps; presentation drains the events of every step it
-- ran since the last frame. One-shot events carry what their consumer needs (a sound's
-- position at emission), so freeing the source later loses nothing.
local M = {}

-- Kinds and their arguments: the native game's event kinds (native/kp_core/src/core/kp_game.h KP_EVENT_*).
-- The sounds' a carries their call's arguments (M.sound_args): the sound in bits 0..15, minVolume in 16..23, enable
-- in 24..31 (0 the constant 1, 1..8 bit 0..7 of the options word 0x6d3148, 0xff the constant 0), then the talking
-- request RequestUnitTalking 0x4a7200 after it: when in 32..33 (0 none, 1 always, 2 when it played), the camera
-- flag in 34, the colour in 40..47.
M.SOUND = 1 -- PlaySoundAtPosition 0x446a20 (PlayImageSound 0x40f5a0): a = the arguments, b = x, c = y (map pixels)
M.ERROR = 2 -- a = player, b = string id, c = 1 a rez\network.tbl string (0x425020), else stat_txt (0x46ae50)
M.UNIT_UNPLACED = 3 -- a = unit type, b = x, c = y: a unit made that could not be placed (network.tbl 0x12, 0x46b070)
-- a sound without a position (PlaySoundAtUnit 0x446940 without a unit, the advisor's ShowErrorWithSound 0x4461f0):
-- a = the arguments, b = the talking request's unit or 0
M.SOUND_SCREEN = 4
M.TRIGGER = 5 -- the trigger trace: a = player, b = trigger's place in its list, c = action << 16 | type << 8 | result
M.TRIGGER_TEXT = 6 -- a = string id, b = milliseconds, c = 1 when shown even without subtitles
M.TRIGGER_WAV = 7 -- a = the WAV's string id
M.PORTRAIT = 8 -- a = unit type, b = milliseconds, c = the unit or 0
M.MINIMAP_PING = 9 -- a = x, b = y, c = color
M.CAMERA = 10 -- a = x, b = y of the screen's top-left; c = 1 when the pan ends
M.TRIGGER_PAUSE = 11 -- a = 1 a trigger paused the game, 0 resumed it
M.QUIET = 12 -- a = 1 unit speech muted, 0 unmuted
M.TRIGGER_SKIP = 13 -- Escape skipped the transmissions
M.GAME_RESULT = 14 -- a = the local player's result: 2 defeat, 3 victory, 5 draw
M.AI = 15 -- the AI trace: a = player, b = script slot << 1 | binary, c = PC << 8 | opcode
-- a = a rez\network.tbl string id shown as a game message (0x46a860): the cheat messages 0x41 "Cheat Enabled" and 0x42
-- "Cheat Disabled" (ApplyCheatFlags 0x40d9d0). TODO: the message line shows it with the interface (plan phase 9).
M.NETWORK_TEXT = 16
-- the game took a unit out of the local selection; the interface tells the network (0x4a7b80): the selection left,
-- 11 bits a unit, units 0..3 in a, 4..7 in b, 8..11 in c
M.SELECTION_SENT = 17
-- PlaySoundAtUnit 0x446940 with a unit the local player hears: a = the arguments, b = the unit (also the talking
-- request's), c = its sprite's position, x & 0xffff | (y & 0xffff) << 16 (M.position), with the unit's type at the
-- call << 32
M.SOUND_UNIT = 18
-- NotifyUnitAttacked 0x446f00 past the game's gates: a = the local player's unit, b = 1 when its status has 0x2 (the
-- voice 0x75 + the local race, else 0x78 + it), c = its sprite's position as SOUND_UNIT's
M.UNIT_ATTACKED = 19
M.SELECTION_GONE = 20 -- RemoveFromSelections 0x49c8b0 took unit a out of every selection (the interface's mirror too)
M.GAME_EXIT = 21 -- RequestGameExit 0x444e00 from a command: a = the exit code (5 a single player's restart)
M.SAVE_GAME = 22 -- ExecSaveGame 0x472f50: a = the time stamp, text = the save's name (the host writes it)
M.LOAD_GAME = 23 -- ExecLoadGame 0x4729e0: a = the save's ID, text = its name (the host loads it)
-- The game ended the local cursor modes: a = 1 CancelTargetingUi 0x423120 (the targeting too), 0 the placement's close
M.CURSOR_MODES = 24

-- A sound event's a as its fields: sfx, minVolume, enable, talk, camera, colour.
function M.sound_args(a)
	local low, high = a % 0x100000000, math.floor(a / 0x100000000)
	return low % 0x10000, math.floor(low / 0x10000) % 0x100, math.floor(low / 0x1000000), high % 4,
		math.floor(high / 4) % 2, math.floor(high / 0x100) % 0x100
end

-- A packed sprite position (SOUND_UNIT's and UNIT_ATTACKED's c) as signed x, y.
function M.position(c)
	local x, y = c % 0x10000, math.floor(c / 0x10000) % 0x10000
	return x >= 0x8000 and x - 0x10000 or x, y >= 0x8000 and y - 0x10000 or y
end

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
