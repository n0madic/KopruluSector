local clock = require("game.simulation.clock")

local S = {}

function S.fastest_steps_at_42ms(T)
	local c = clock.new()
	local total = 0
	for _ = 1, 600 do total = total + c:advance(1 / 60) end -- 10 s at 60 Hz
	T.eq(total, math.floor(10000 / 42))
	T.eq(c.dropped_ms, 0)
end

function S.speed_table(T)
	T.eq(#clock.SPEED_MS, 7)
	T.eq(clock.SPEED_MS[1], 167)
	T.eq(clock.SPEED_MS[7], 42)
	local c = clock.new({ speed = 1 })
	T.eq(c:step_ms(), 167)
	T.raises(function() c:set_speed(8) end, "unknown game speed")
end

function S.interpolation_fraction(T)
	local c = clock.new()
	local n, alpha = c:advance(0.021)
	T.eq(n, 0)
	T.ok(math.abs(alpha - 0.5) < 1e-9)
	n, alpha = c:advance(0.042)
	T.eq(n, 1)
	T.ok(math.abs(alpha - 0.5) < 1e-9)
end

function S.catch_up_is_bounded(T)
	local c = clock.new({ max_catchup = 4 })
	local n = c:advance(2.0) -- a 2 s stall
	T.eq(n, 4)
	T.ok(c.accumulator_ms < 42, "backlog beyond the limit is dropped")
	T.ok(c.dropped_ms > 0)
	n = c:advance(0)
	T.eq(n, 0)
end

function S.suspend_discards_background_time(T)
	local c = clock.new()
	c:advance(0.030)
	c:pause()
	T.eq((c:advance(30)), 0) -- frames while suspended do nothing
	c:resume()
	T.eq(c.accumulator_ms, 0)
	T.eq((c:advance(0.041)), 0)
	T.eq((c:advance(0.001)), 1)
	T.eq(c.resets, 1)
end

function S.wall_reference_resets_on_resume(T)
	local c = clock.new()
	T.eq((c:advance_wall(100.0)), 0) -- first call only sets the reference
	T.eq((c:advance_wall(100.042)), 1)
	c:pause()
	T.eq((c:advance_wall(103.0)), 0)
	c:resume()
	T.eq((c:advance_wall(103.5)), 0) -- the 3 s in the background are not replayed
	T.eq((c:advance_wall(103.542)), 1)
	T.eq(c.dropped_ms, 0)
end

function S.negative_dt_ignored(T)
	local c = clock.new()
	T.eq((c:advance(-1)), 0)
	T.eq(c.accumulator_ms, 0)
end

return S
