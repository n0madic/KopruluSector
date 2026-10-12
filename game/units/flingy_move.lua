-- The flingy step of a moving unit (atlas P10): direction towards the waypoint
-- (UpdateDesiredDirection 0x40cf50), turning (TurnVelocityDirection 0x40d450,
-- TurnTowardDesiredHeading 0x40d1a0), speed (ProgressSpeed 0x40d260), the walking animation
-- switches (UpdateWalkingFlags 0x40d110) and the next position (PrepareNextPosition 0x40d020),
-- prepared by PrepareFlingyMovement 0x40bcd0 and published by CommitFlingyMovement 0x40bdb0 and
-- CommitUnitMovement 0x42ab10. The prepared values live in the step globals 0x543280..0x5432fb
-- (self.mv). Methods of the game.
local bytes = require("game.data.bytes")
local directions = require("game.data.directions")
local int = require("game.simulation.int")

local band, bor = int.band, int.bor
local s8, s16, s32 = int.s8, int.s16, int.s32
local sar = int.sar

local M = {}

M.FLAG_TURNING, M.FLAG_MOVING, M.FLAG_STOPPING, M.FLAG_WALK_IDLE = 0x01, 0x02, 0x04, 0x08
M.FLAG_PATHING, M.FLAG_WAYPOINT_SLOW, M.FLAG_NEAR_GOAL = 0x10, 0x20, 0x40
M.CONTROL_FLINGY, M.CONTROL_ISCRIPT = 0, 2
M.SLOW_SPEED = 0xc0
M.TURN_SLOWDOWN = 0x20 -- the waypoint distance below which a turn slows the unit
M.ANIM_WALKING, M.ANIM_WALKING_TO_IDLE = 0x0b, 0x0c
-- Flingy types whose turn speed grows by one per turn (0x40d1a0).
M.GROWING_TURN_FIRST, M.GROWING_TURN_LAST = 0x8d, 0xab
M.GROWING_TURN2_FIRST, M.GROWING_TURN2_COUNT = 0xc9, 6

function M.install(G)
	function G:move_context_init()
		self.mv = { next_x = 0, next_y = 0, old_x = 0, old_y = 0, next_halt_x = 0, next_halt_y = 0, step = 0,
			walk_start = 0, walk_stop = 0, flags_before = 0, flags_after = 0 }
	end

	-- The velocity of speed v in direction heading2 (+0x4a).
	local function velocity(u, n, v)
		local h = u.heading2[n]
		u.speed_x[n] = sar(int.mul(directions.x[h], v), 8)
		u.speed_y[n] = sar(int.mul(directions.y[h], v), 8)
	end

	-- SetCurrentSpeed 0x40cb90.
	function G:flingy_set_current_speed(n, v)
		local u = self.u
		if u.speed1[n] == v then return end
		u.speed1[n] = v
		velocity(u, n, v)
	end

	local function stop_speed(u, n)
		u.speed1[n], u.speed_x[n], u.speed_y[n] = 0, 0, 0
	end

	-- AccelerateSpeed 0x40bd10: speed plus delta, within 0 and the top speed.
	local function accelerate(self, n, delta)
		local u = self.u
		local v = s32(u.speed1[n] + delta)
		local top = u.top_speed[n]
		if v > top then
			if u.speed1[n] ~= top then
				u.speed1[n] = top
				velocity(u, n, top)
			end
		elseif v < 0 then
			if u.speed1[n] ~= 0 then stop_speed(u, n) end
		elseif u.speed1[n] ~= v then
			u.speed1[n] = v
			velocity(u, n, v)
		end
	end

	-- GetHaltDistance 0x40bf00.
	function G:flingy_halt_distance(n)
		local u, f = self.u, self.dat.flingy
		local v = u.speed2[n]
		if v == 0 or u.movement_type[n] ~= 0 then return 0 end
		local t = u.flingy[n]
		if v == f.speed[t] and u.acceleration[n] == f.acceleration[t] then return f.halt_distance[t] end
		return int.idiv(s32(int.mul(v, v)), u.acceleration[n] * 2)
	end

	-- SlopeToDirectionOffset 0x40b940.
	local function slope_offset(slope)
		local negative = slope < 0
		if negative then slope = -slope end
		local lo, hi, mid = 0, 0x40, 0x20
		repeat
			if slope % 4294967296 > directions.SLOPES[mid] then lo = mid + 1 else hi = mid end
			mid = int.shr(lo + hi, 1)
		until hi == lo
		if negative then return -mid end
		return mid
	end

	-- GetDirectionFromPoints 0x40ba10: the direction byte from (x1, y1) towards (x2, y2).
	function G:direction_to(x1, y1, x2, y2)
		local dx, dy = x2 - x1, y2 - y1
		if dx == 0 then return dy <= 0 and 0 or 0x80 end
		local a = slope_offset(int.idiv(s32(int.shl(dy, 8)), dx))
		if dx >= 0 then return (a + 0x40) % 256 end
		a = a + 0xc0
		if a == 0x100 then a = 0 end
		return a % 256
	end

	-- TurnVelocityDirection 0x40d450: heading2 turns towards the desired direction, by half the
	-- turn speed for flingy-controlled units.
	local function turn_velocity(u, n)
		local want, cur = u.velocity_direction2[n], u.velocity_direction[n]
		local h
		if want == cur then
			h = cur
		else
			local step = u.turn_speed[n]
			if u.movement_type[n] ~= M.CONTROL_ISCRIPT then step = int.shr(step, 1) end
			local diff = s8(want - cur)
			if diff >= step then
				diff = step
			elseif diff <= -step then
				diff = -step
			end
			h = (cur + diff) % 256
		end
		if u.heading2[n] ~= h then
			u.heading2[n] = h
			velocity(u, n, u.speed1[n])
		end
	end

	-- UpdateDesiredDirection 0x40cf50: the direction byte +0x4b towards the waypoint (or the
	-- target waypoint, or the heading when both are reached).
	local function update_desired(self, n)
		local u = self.u
		local px, py = u.x[n], u.y[n]
		if u.waypoint_x[n] == px and u.waypoint_y[n] == py then
			if u.target_waypoint_x[n] == px and u.target_waypoint_y[n] == py then
				u.velocity_direction2[n] = u.heading[n]
			else
				u.velocity_direction2[n] = self:direction_to(px, py, u.target_waypoint_x[n], u.target_waypoint_y[n])
			end
		else
			u.velocity_direction2[n] = self:direction_to(px, py, u.waypoint_x[n], u.waypoint_y[n])
		end
		turn_velocity(u, n)
	end

	local function heading_gap(a, b)
		local d = band(a - b, 0xff)
		if d > 0x80 then return 0x100 - d end
		return d
	end

	-- ProgressSpeed 0x40d260.
	local function progress_speed(self, n)
		local u = self.u
		local mt = u.movement_type[n]
		local at_move = u.move_x[n] == u.x[n] and u.move_y[n] == u.y[n]
		if mt == 0 then
			if at_move and u.speed2[n] < M.SLOW_SPEED and band(u.movement_flags[n], 0x30) == 0 then
				u.movement_flags[n] = band(u.movement_flags[n], 0xfb)
				self:flingy_set_current_speed(n, 0)
				return
			end
		elseif mt == 1 or mt == 2 then
			if at_move then
				u.movement_flags[n] = band(u.movement_flags[n], 0xfb)
				if u.speed1[n] ~= 0 then stop_speed(u, n) end
				return
			end
			if mt == 2 then
				local f = u.movement_flags[n]
				if band(f, M.FLAG_TURNING) ~= 0 and heading_gap(u.heading[n], u.velocity_direction2[n]) >= 0x20 then
					if band(f, M.FLAG_MOVING) == 0 then return end
					u.movement_flags[n] = band(f, 0xf9)
					self:flingy_set_current_speed(n, 0)
					return
				end
				self:flingy_set_current_speed(n, u.speed2[n])
				return
			end
		end
		if u.speed1[n] ~= u.speed2[n] then
			u.speed1[n] = u.speed2[n]
			velocity(u, n, u.speed2[n])
		end
		local dist = self:approx_distance(u.x[n], u.y[n], u.waypoint_x[n], u.waypoint_y[n])
		local want = u.velocity_direction2[n]
		if u.heading2[n] ~= want and dist % 4294967296 < M.TURN_SLOWDOWN then
			local turn = u.turn_speed[n]
			local gap = heading_gap(want, u.heading2[n])
			local need = int.idiv(turn - 1 + gap * 2, turn)
			need = int.shr(int.mul(need, u.speed2[n]) % 4294967296, 8)
			need = int.shr(need * 3, 1)
			if need > dist % 4294967296 then
				accelerate(self, n, -u.acceleration[n])
				return
			end
		end
		local f = u.movement_flags[n]
		if mt == 0 and (band(f, M.FLAG_PATHING) == 0 or band(f, M.FLAG_NEAR_GOAL) ~= 0)
			and (band(f, M.FLAG_WAYPOINT_SLOW) == 0 or u.waypoint_x[n] ~= u.move_x[n] or u.waypoint_y[n] ~= u.move_y[n]) then
			local h = self:flingy_halt_distance(n)
			local d = self:approx_distance(u.halt_x[n], u.halt_y[n], int.shl(u.waypoint_x[n], 8), int.shl(u.waypoint_y[n], 8))
			if h % 4294967296 >= d % 4294967296 then
				accelerate(self, n, -u.acceleration[n])
				return
			end
		end
		accelerate(self, n, u.acceleration[n])
	end

	-- UpdateWalkingFlags 0x40d110: the walking animation switches of the step.
	local function update_walking(self, n)
		local u, mv = self.u, self.mv
		local f = u.movement_flags[n]
		mv.walk_start, mv.walk_stop = 0, 0
		local moving = band(f, M.FLAG_MOVING) ~= 0
		local at_move = u.move_x[n] == u.x[n] and u.move_y[n] == u.y[n]
		if moving and at_move then
			f = band(f, 0xfd)
			u.movement_flags[n] = f
			if band(f, M.FLAG_WALK_IDLE) == 0 then mv.walk_stop = 1 end
			return
		end
		if moving or at_move then return end
		if u.movement_type[n] ~= M.CONTROL_ISCRIPT or band(f, M.FLAG_WALK_IDLE) == 0 then
			u.movement_flags[n] = bor(f, M.FLAG_MOVING)
		end
		mv.walk_start = band(u.movement_flags[n], M.FLAG_WALK_IDLE) == 0 and 1 or 0
	end

	-- PrepareNextPosition 0x40d020.
	local function prepare_next(self, n)
		local u, mv = self.u, self.mv
		mv.step = u.speed1[n]
		if band(u.movement_flags[n], M.FLAG_MOVING) == 0 or u.speed1[n] == 0 then
			mv.next_x, mv.next_y = u.x[n], u.y[n]
			mv.next_halt_x, mv.next_halt_y = u.halt_x[n], u.halt_y[n]
			return
		end
		local d = self:approx_distance(u.halt_x[n], u.halt_y[n], int.shl(u.waypoint_x[n], 8), int.shl(u.waypoint_y[n], 8))
		if u.speed1[n] < d then
			mv.next_halt_x = s32(u.speed_x[n] + u.halt_x[n])
			mv.next_halt_y = s32(u.speed_y[n] + u.halt_y[n])
			mv.next_x, mv.next_y = s16(sar(mv.next_halt_x, 8)), s16(sar(mv.next_halt_y, 8))
		else
			mv.next_x, mv.next_y = u.waypoint_x[n], u.waypoint_y[n]
			mv.next_halt_x, mv.next_halt_y = int.shl(mv.next_x, 8), int.shl(mv.next_y, 8)
			mv.step = d
		end
		if u.movement_type[n] == M.CONTROL_ISCRIPT and u.speed1[n] ~= 0 then stop_speed(u, n) end
	end

	-- The common tail of the step preparations: the walking switches and the next position;
	-- the movement flags they compute wait in flags_after until the commit.
	local function prepare_tail(self, n)
		local u, mv = self.u, self.mv
		update_walking(self, n)
		prepare_next(self, n)
		mv.flags_after = u.movement_flags[n]
		u.movement_flags[n] = mv.flags_before
	end

	-- PrepareFlingyMovement 0x40bcd0: the step's direction, speed and next position.
	function G:prepare_flingy_movement(n)
		self.mv.flags_before = self.u.movement_flags[n]
		update_desired(self, n)
		progress_speed(self, n)
		prepare_tail(self, n)
	end

	-- PrepareMovementWithDirection 0x40bbc0: a step whose velocity points in direction `dir`.
	function G:prepare_movement_in_direction(n, dir)
		local u = self.u
		self.mv.flags_before = u.movement_flags[n]
		if u.heading2[n] ~= dir then
			u.heading2[n] = dir
			velocity(u, n, u.speed1[n])
		end
		progress_speed(self, n)
		prepare_tail(self, n)
	end

	-- TurnTowardDesiredHeading 0x40d1a0: the heading +0x21 turns by the turn speed.
	local function turn_heading(self, n)
		local u = self.u
		local f = u.movement_flags[n]
		if band(f, M.FLAG_MOVING) ~= 0 and band(f, M.FLAG_TURNING) == 0 then return end
		local h, want = u.heading[n], u.velocity_direction2[n]
		local diff = s8(want - h)
		local turn = u.turn_speed[n]
		if diff > s8(turn) then
			u.heading[n] = (turn + h) % 256
		elseif diff < -s8(turn) then
			u.heading[n] = (h - turn) % 256
		else
			u.heading[n] = (h + diff) % 256
		end
		local fl = u.flingy[n]
		if (fl >= M.GROWING_TURN_FIRST and fl <= M.GROWING_TURN_LAST)
			or (fl - M.GROWING_TURN2_FIRST) % 65536 < M.GROWING_TURN2_COUNT then
			u.turn_speed[n] = (turn + 1) % 256
		end
		if u.velocity_direction[n] == want and u.heading[n] == want then
			u.movement_flags[n] = band(f, 0xfe)
		end
	end

	-- CommitFlingyMovement 0x40bdb0: the prepared position, speed and flags become the unit's;
	-- true when the position changed.
	local function commit_flingy(self, n)
		local u, mv, w = self.u, self.mv, self.img
		local moved = not (u.x[n] == mv.next_x and u.y[n] == mv.next_y)
		mv.old_x, mv.old_y = u.x[n], u.y[n]
		u.velocity_direction[n] = u.heading2[n]
		u.movement_flags[n] = mv.flags_after
		u.speed2[n] = u.speed1[n]
		u.x[n], u.y[n] = mv.next_x, mv.next_y
		u.halt_x[n], u.halt_y[n] = mv.next_halt_x, mv.next_halt_y
		local s = u.sprite[n]
		w:sprite_move(s, u.x[n], u.y[n])
		turn_heading(self, n)
		w:sprite_set_facing(s, u.heading[n])
		if mv.walk_stop ~= 0 then
			w:sprite_play(s, M.ANIM_WALKING_TO_IDLE, false)
		elseif mv.walk_start ~= 0 then
			w:sprite_play(s, M.ANIM_WALKING, true)
		end
		return moved
	end

	-- TurnStep 0x40ced0: the turn from direction `cur` towards `want`, at most `turn`.
	local function turn_step(cur, want, turn)
		local diff = s8(want - cur)
		if diff >= turn then return turn end
		if diff <= -turn then return -turn end
		return diff
	end

	-- ForecastPositions 0x42e6c0 (one point): where unit n will be after `steps` steps. A unit
	-- moved by its IScript runs a copy of its main image's script in the no-op mode (the
	-- Walking animation 0xb) and moves by the speeds of its `move` opcodes once it faces the
	-- waypoint; a flingy mover moves by its flingy speed.
	function G:forecast_position(n, steps)
		local u, w = self.u, self.img
		local s = u.sprite[n]
		local x, y = s16(w.spr.x[s]), s16(w.spr.y[s])
		local want = self:direction_to(x, y, u.waypoint_x[n], u.waypoint_y[n])
		local cur = u.velocity_direction[n]
		local turn = self:turn_radius_modified(n)
		local speed = u.speed2[n]
		local hx, hy = u.halt_x[n], u.halt_y[n]
		local accel = self:acceleration_modified(n)
		if u.movement_type[n] ~= M.CONTROL_ISCRIPT then
			-- 0x42e890: a flingy mover turns by half its turn radius per step while it moves,
			-- accelerating to its top speed.
			local top = u.top_speed[n]
			local half = int.idiv(turn, 2)
			local vx, vy, recompute = 0, 0, true
			for _ = 1, steps do
				if want ~= cur then
					cur = (cur + turn_step(cur, want, half)) % 256
					recompute = true
				end
				if speed < top then
					speed = speed + accel
					if speed > top then speed = top end
					recompute = true
				end
				if recompute then
					recompute = false
					vx = sar(int.mul(directions.x[cur], speed), 8)
					vy = sar(int.mul(directions.y[cur], speed), 8)
				end
				hx, hy = s32(hx + vx), s32(hy + vy)
			end
			return s16(sar(hx, 8)), s16(sar(hy, 8))
		end
		local main = w.spr.main[s]
		local st = { header = w.img.header[main], pc = w.img.pc[main], ret = w.img.ret[main], wait = w.img.wait[main] }
		local function run()
			local saved = w.unit
			w.unit = n
			local v = w:run_iscript(main, st, true, true)
			w.unit = saved
			return v
		end
		local recompute = true
		if w.img.anim[main] ~= M.ANIM_WALKING then
			-- SetForecastAnimation 0x42e9b0: the Walking entry of the script, when it has one.
			local code = w.script.code
			if M.ANIM_WALKING <= bytes.u16(code, st.header + 4) then
				local entry = bytes.u16(code, st.header + 8 + 2 * M.ANIM_WALKING)
				st.pc = entry
				if entry ~= 0 then st.wait, st.ret = 0, 0 end
			end
			local v = run()
			if v then speed = v end
		end
		local vx, vy = 0, 0
		for _ = 1, steps do
			local move = true
			if want ~= cur then
				cur = (cur + turn_step(cur, want, turn)) % 256
				recompute = true
				move = cur == want
			end
			if move and speed ~= 0 then
				if recompute then
					recompute = false
					vx = sar(int.mul(directions.x[cur], speed), 8)
					vy = sar(int.mul(directions.y[cur], speed), 8)
				end
				hx, hy = s32(hx + vx), s32(hy + vy)
			end
			speed = run() or 0
		end
		return s16(sar(hx, 8)), s16(sar(hy, 8))
	end

	G.flingy_turn_velocity = function(self, n) turn_velocity(self.u, n) end -- 0x40d450
	G.flingy_update_desired = update_desired -- 0x40cf50
	G.flingy_progress_speed = progress_speed -- 0x40d260
	G.flingy_turn_heading = turn_heading -- 0x40d1a0
	G.flingy_update_walking = update_walking -- 0x40d110

	-- RecomputeVelocity 0x40cb60.
	function G:flingy_recompute_velocity(n)
		velocity(self.u, n, self.u.speed1[n])
	end

	-- CommitFlingyTurnOnly 0x40bee0: the unit turns in place.
	function G:flingy_commit_turn(n)
		local u = self.u
		u.velocity_direction[n] = u.heading2[n]
		turn_heading(self, n)
		self.img:sprite_set_facing(u.sprite[n], u.heading[n])
	end

	-- PrepareMovementWithDirectionAndSpeed 0x40bc30: a step in direction `dir` at `speed`.
	function G:prepare_movement_toward(n, dir, speed)
		local u = self.u
		self.mv.flags_before = u.movement_flags[n]
		if u.heading2[n] ~= dir then
			u.heading2[n] = dir
			velocity(u, n, u.speed1[n])
		end
		u.speed2[n] = speed
		if u.speed1[n] ~= speed then
			u.speed1[n] = speed
			velocity(u, n, speed)
		end
		prepare_tail(self, n)
	end

	-- CommitUnitMovement 0x42ab10: the flingy commit, the spatial index and the vision refresh
	-- when the unit changed tile.
	function G:commit_unit_movement(n)
		if not commit_flingy(self, n) then return false end
		local mv = self.mv
		self:spatial_move(n, mv.next_x - mv.old_x, mv.next_y - mv.old_y)
		if int.idiv(mv.old_x, 32) ~= int.idiv(mv.next_x, 32) or int.idiv(mv.old_y, 32) ~= int.idiv(mv.next_y, 32) then
			self.move_reveal = true
		end
		return true
	end
end

return M
