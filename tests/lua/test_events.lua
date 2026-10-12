-- game/simulation/events.lua: the D15 event ring.
local events = require("game.simulation.events")

local S = {}

local function collect(ring)
	local out = {}
	ring:drain(function(kind, tick, seq, a, b, c, d)
		out[#out + 1] = { kind, tick, seq, a, b, c, d }
	end)
	return out
end

function S.drains_in_emission_order_and_wraps(T)
	local ring = events.new(3)
	ring:emit(events.SOUND, 1, 10, 20, 30)
	ring:emit(events.SOUND, 1, 11)
	local out = collect(ring)
	T.eq(#out, 2)
	T.eq(out[1][4], 10)
	T.eq(out[1][6], 30)
	T.eq(out[2][3], 2) -- sequence numbers keep counting
	T.eq(out[2][5], 0) -- missing arguments are zero
	for k = 1, 3 do ring:emit(events.SOUND, 2, k) end -- wraps around the end
	out = collect(ring)
	T.eq(#out, 3)
	T.eq(out[3][4], 3)
	T.eq(out[3][3], 5)
	T.eq(#collect(ring), 0)
end

function S.overflow_raises(T)
	local ring = events.new(2)
	ring:emit(events.SOUND, 0, 1)
	ring:emit(events.SOUND, 0, 2)
	T.ok(not pcall(ring.emit, ring, events.SOUND, 0, 3))
end

return S
