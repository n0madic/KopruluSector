-- Unit movement states (ExecuteUnitMovement 0x42ad30 and its state functions; atlas P10).
-- The state byte +0x97 selects a function that returns 1 to run the next state in the same
-- step or 0 to stop. States reached so far are ported; every other state raises with the
-- native address of its function (plan §7 phase 3: no movement no-ops). Methods of the game.
local int = require("game.simulation.int")
local pool = require("game.pathing.pool")

local band, bor, bnot = int.band, int.bor, int.bnot
local s16 = int.s16

local M = {}

M.INIT, M.NOBRK, M.DEAD, M.HIDDEN_IN_TRANSPORT = 0, 1, 2, 4
M.HIDDEN, M.AT_REST, M.DORMANT, M.CHECK_ILLEGAL = 6, 9, 10, 12
M.REPLACED_ON_LEGAL, M.ROUTE = 13, 14
M.ARRIVED, M.FAILED, M.START_MOVE, M.RETRY, M.BLOCKED_REPATH, M.REPATH_STALE, M.REPATH = 11, 15, 17, 18, 19, 21, 22
M.REPATH_FAILED, M.FOLLOW, M.SCOUT, M.REFOLLOW = 23, 25, 26, 27
M.BLOCKED_BY_UNIT, M.BLOCKED_BY_TERRAIN = 28, 34
M.FACE_TARGET, M.REPATH_MOVERS, M.WAIT_FREE, M.SLIDE_START, M.SLIDE_PREP, M.SLIDE = 20, 24, 29, 30, 31, 32
M.FORCE_FREE = 33 -- MoveState_ForceMoveFree 0x42bb00
M.STATUS_INTERCEPT = 0x80000
M.ANIM_WALKING_TO_IDLE = 0x0c
M.SCOUT_DELAY, M.SCOUT_RADIUS, M.SCOUT_STEPS = 0x1e, 0x20, 0x1e
M.PATH_RANDOM, M.PATH_RETRY_MAX = 0x33, 2 -- MoveState_StartPath: RandRange(0, 2) on source 51
M.USER_RIGHT_CLICKED = 2
M.STATUS_NOBRK, M.STATUS_IN_BUILDING, M.STATUS_BURROWED = 0x2000, 0x20, 0x10
M.STATUS_GROUND_MOVER, M.STATUS_AIR_MOVER = 0x20000, 0x10000
M.STATUS_COLLIDING, M.STATUS_NOT_AIR = 0x40000, 0x100000
M.STATUS_IGNORED = 0x200000
M.STATUS_LOCKS = 0x3000
M.SPRITE_HIDDEN = 0x20
M.DAT_SUBUNIT = 0x10

-- The functions of the states that are not ported yet, for their errors (the dispatcher's
-- jump table 0x42af44; states 18, 21, 22 and 27 are inline branches of the dispatcher).
local NATIVE = {
	[3] = 0x42c610, [4] = 0x40cf50, [5] = 0x40cf50, [7] = 0x42b7f0, [13] = 0x42be70, [14] = 0x42be10,
	[15] = 0x42b370, [16] = 0x42bfd0, [33] = 0x42bb00,
	[34] = 0x42b700, [35] = 0x42c470,
}

local function unported(state, address)
	error(("movement: state %d (0x%x)"):format(state, address), 0)
end

function M.install(G)
	local function sprite_pos(self, n)
		local s = self.u.sprite[n]
		return s16(self.img.spr.x[s]), s16(self.img.spr.y[s])
	end

	-- The move target is the sprite position (the always-true third term of the native
	-- test, 0x80000 status as 1 or 2, is left out).
	local function at_move_target(self, n)
		local x, y = sprite_pos(self, n)
		return self.u.move_x[n] == x and self.u.move_y[n] == y
	end

	-- TryMoveToLegalPosition 0x42ca80: 1 when the unit was moved off a blocked spot.
	function G:try_move_to_legal(n)
		local u = self.u
		if band(u.status[n], M.STATUS_NOT_AIR) == 0 then return 0 end
		local x, y = sprite_pos(self, n)
		if self:position_blocked(n, x, y) == 0 then return 0 end
		if band(u.status[n], M.STATUS_LOCKS) == 0 and band(u.movement_flags[n], 8) == 0 then
			error("movement: moving a blocked unit to a legal spot (0x42ce60)", 0)
		end
		u.status[n] = bor(u.status[n], M.STATUS_COLLIDING)
		return 0
	end

	-- MoveState_Init 0x42bcc0.
	local function state_init(self, n)
		local u = self.u
		local w = self.img
		local s = u.sprite[n]
		u.contour_left[n], u.contour_top[n], u.contour_right[n], u.contour_bottom[n] = 0, 0, 0, 0
		u.pathing_flags[n] = bor(band(u.pathing_flags[n], 0xfc), w.spr.elevation[s] < 12 and 1 or 0)
		local subunit = band(self.dat.units.special_ability_flags[u.type[n]], M.DAT_SUBUNIT) ~= 0
		if not subunit and band(u.status[n], M.STATUS_NOBRK) ~= 0 then
			u.movement_state[n] = M.NOBRK
			return 1
		end
		if self:unit_is_dead(n) then
			u.movement_state[n] = M.DEAD
			return 1
		end
		local status = u.status[n]
		if band(status, M.STATUS_IN_BUILDING) ~= 0 then
			u.movement_state[n] = M.HIDDEN_IN_TRANSPORT
			return 1
		end
		if band(w.spr.flags[s], M.SPRITE_HIDDEN) ~= 0 then
			error("movement: initial state of a hidden unit (0x41a250, 0x42c7a0)", 0)
		end
		if band(status, M.STATUS_BURROWED) == 0 then
			if band(status, M.STATUS_GROUND_MOVER) ~= 0 then
				u.movement_state[n] = band(u.pathing_flags[n], 1) * 2 + 7
				return 1
			end
			if band(status, M.STATUS_AIR_MOVER) ~= 0 then
				u.movement_state[n] = subunit and 3 or 5
				return 1
			end
			if band(u.pathing_flags[n], 1) ~= 0 then
				error("movement: initial route of a unit without a mover flag (0x41a250)", 0)
			end
		end
		u.movement_state[n] = M.DEAD
		return 1
	end

	-- CommitGroundMovementOrStop 0x42abb0: the prepared step unless a unit or terrain blocks
	-- it.
	local function commit_or_stop(self, n)
		if self:ground_unit_collision(n) == 0 and not self:terrain_collision(n) then
			return self:commit_unit_movement(n)
		end
		error("movement: stopping a blocked unit at rest (0x40be80, 0x4229a0, 0x43a490)", 0)
	end

	-- MoveState_AtRest 0x42b030: a unit that still turns or stops takes a flingy step; one
	-- that is away from its move target starts a path.
	local function state_at_rest(self, n)
		local u = self.u
		local at_target = at_move_target(self, n)
		if at_target then
			u.collision_interval[n] = 0
		else
			local c = u.collision_interval[n]
			if c > 2 then
				u.collision_interval[n] = 2
			elseif c ~= 0 then
				u.collision_interval[n] = c - 1
			end
		end
		local settled = true
		local flags = u.movement_flags[n]
		local step = band(flags, 4) ~= 0
		if not step and at_target then
			if band(flags, 2) ~= 0 then
				step = true
			elseif u.target_waypoint_x[n] ~= u.x[n] or u.target_waypoint_y[n] ~= u.y[n] then
				local x, y = sprite_pos(self, n)
				local d = self:direction_to(x, y, u.target_waypoint_x[n], u.target_waypoint_y[n])
				step = d ~= u.heading[n] or d ~= u.velocity_direction[n]
			end
		end
		if step then
			settled = false
			self:prepare_flingy_movement(n)
			commit_or_stop(self, n)
		end
		local status = u.status[n]
		if band(status, M.STATUS_COLLIDING) ~= 0 and band(status, M.STATUS_NOT_AIR) ~= 0 then
			u.movement_state[n] = M.CHECK_ILLEGAL
			return 0
		end
		if not at_move_target(self, n) and band(u.movement_flags[n], 4) == 0 then
			u.movement_state[n] = M.START_MOVE
			return 1
		end
		if settled then
			local x, y = sprite_pos(self, n)
			self:flingy_set_speed(n, 0)
			self:flingy_set_target_waypoint(n, x, y)
			u.movement_state[n] = M.DORMANT
		end
		return 0
	end

	-- MoveState_CheckIllegal 0x42b1b0.
	local function state_check_illegal(self, n)
		local u = self.u
		u.status[n] = band(u.status[n], bnot(M.STATUS_COLLIDING))
		if self:try_move_to_legal(n) ~= 0 then
			u.pathing_flags[n] = bor(u.pathing_flags[n], 2)
			u.movement_state[n] = M.REPLACED_ON_LEGAL
			return 0
		end
		u.pathing_flags[n] = band(u.pathing_flags[n], 0xf9)
		if not at_move_target(self, n) and band(u.movement_flags[n], 4) == 0 then
			u.movement_state[n] = M.REPATH
			return 1
		end
		u.movement_state[n] = M.AT_REST
		return 1
	end

	-- MoveState_Dormant 0x42b240.
	local function state_dormant(self, n)
		local u = self.u
		local status = u.status[n]
		if (band(status, M.STATUS_COLLIDING) == 0 or band(status, M.STATUS_NOT_AIR) == 0) and at_move_target(self, n)
			and u.target_waypoint_x[n] == u.x[n] and u.target_waypoint_y[n] == u.y[n] then
			return 0
		end
		u.movement_state[n] = M.AT_REST
		return 1
	end

	-- UpdateMovementState 0x42ac20: the state for a reached or changed move target; false
	-- when the unit keeps its state. `repath` lets a unit without a path ask for one.
	function G:update_movement_state(n, repath)
		local u = self.u
		if at_move_target(self, n) or band(u.movement_flags[n], 4) ~= 0 then
			u.movement_state[n] = M.ARRIVED
			return true
		end
		local status = u.status[n]
		if band(status, M.STATUS_COLLIDING) ~= 0 and band(status, M.STATUS_NOT_AIR) ~= 0 then
			self:path_free(n)
			u.movement_state[n] = M.CHECK_ILLEGAL
			return true
		end
		local k = u.path[n]
		local state
		if repath and k == 0 then
			state = M.REPATH
		elseif k ~= 0 and band(self:path_u8(k, pool.FLAGS), 1) ~= 0 then
			state = M.REPATH_STALE
		else
			return false
		end
		local cur = u.movement_state[n]
		if cur >= 0x1c and cur <= 0x23 then state = M.BLOCKED_REPATH end
		u.movement_state[n] = state
		return true
	end

	-- MoveState_StartPath 0x42c400: the first waypoint of the path to the move target; a
	-- right-clicked walker waits a random 0..2 steps first.
	local function state_start_path(self, n)
		local u = self.u
		if not self:advance_path_waypoint(n, u.move_x[n], u.move_y[n]) then
			u.movement_state[n] = M.FAILED
			return 0
		end
		local action = u.user_action[n]
		if band(action, M.USER_RIGHT_CLICKED) ~= 0 then
			u.user_action[n] = band(action, bnot(M.USER_RIGHT_CLICKED))
			if u.movement_type[n] == 2 then
				self:path_set_retry(n, self.rng:range(M.PATH_RANDOM, 0, M.PATH_RETRY_MAX))
				u.movement_state[n] = M.RETRY
				return 1
			end
		end
		u.movement_state[n] = M.FOLLOW
		return 1
	end

	-- The dispatcher's inline state 18: wait out the path's retry timer.
	local function state_retry(self, n)
		if self:update_movement_state(n, true) then return 1 end
		local k = self.u.path[n]
		local t = self:path_u8(k, pool.RETRY)
		if t ~= 0 then
			self:path_w8(k, pool.RETRY, t - 1)
			return 0
		end
		self.u.movement_state[n] = M.FOLLOW
		return 1
	end

	-- The dispatcher's inline state 21: a stale path is dropped unless the target changed.
	local function state_repath_stale(self, n)
		local k = self.u.path[n]
		self:path_w8(k, pool.FLAGS, band(self:path_u8(k, pool.FLAGS), 0xfe))
		if self:update_movement_state(n, true) then return 1 end
		self:path_free(n)
		self.u.movement_state[n] = M.REPATH_FAILED
		return 1
	end

	-- The dispatcher's inline state 22: a new path; the step ends either way.
	local function state_repath(self, n)
		local u = self.u
		u.movement_state[n] = self:advance_path_waypoint(n, u.move_x[n], u.move_y[n]) and M.FOLLOW or M.FAILED
		return 0
	end

	-- MoveState_AtMoveTarget 0x42afe0: the path is released and the waypoint becomes the move
	-- target.
	local function state_arrived(self, n)
		local u = self.u
		self:path_free(n)
		if u.waypoint_x[n] ~= u.move_x[n] or u.waypoint_y[n] ~= u.move_y[n] then
			u.waypoint_x[n], u.waypoint_y[n] = u.move_x[n], u.move_y[n]
		end
		if band(u.status[n], M.STATUS_NOT_AIR) ~= 0 and band(u.movement_flags[n], 4) == 0 then
			u.movement_state[n] = M.CHECK_ILLEGAL
		else
			u.movement_state[n] = M.AT_REST
		end
		return 1
	end

	-- MoveState_FollowPath 0x42b8d0: one step along the path; a blocking unit or terrain
	-- hands over to the collision states, a smaller step first tried when only units block.
	local function state_follow_path(self, n)
		local u, mv = self.u, self.mv
		if self:update_movement_state(n, true) then return 1 end
		if not self:advance_path_waypoint(n, u.move_x[n], u.move_y[n]) then
			u.movement_state[n] = M.REPATH
			return 1
		end
		local speed = u.speed2[n]
		self:prepare_flingy_movement(n)
		if u.movement_type[n] == 0 then speed = u.speed1[n] end
		local blocker = self:ground_unit_collision(n)
		local terrain = self:terrain_collision(n)
		if blocker ~= 0 then
			local through = false
			if not terrain and (u.speed_x[n] >= 0x20 or u.speed_y[n] >= 0x20) and mv.step == u.speed1[n] then
				local hx, hy, nx, ny = mv.next_halt_x, mv.next_halt_y, mv.next_x, mv.next_y
				for _, div in ipairs({ 2, 4 }) do
					mv.next_halt_x = int.idiv(u.speed_x[n], div) + u.halt_x[n]
					mv.next_halt_y = int.idiv(u.speed_y[n], div) + u.halt_y[n]
					mv.next_x, mv.next_y = s16(int.sar(mv.next_halt_x, 8)), s16(int.sar(mv.next_halt_y, 8))
					if self:ground_unit_collision(n) == 0 then
						through = true
						break
					end
				end
				if not through then
					mv.next_halt_x, mv.next_halt_y, mv.next_x, mv.next_y = hx, hy, nx, ny
				end
			end
			if not through then
				local k = u.path[n]
				self:path_w32(k, pool.BLOCKER, self:unit_ref(blocker))
				self:path_w32(k, pool.SPEED, speed)
				u.movement_state[n] = M.BLOCKED_BY_UNIT
				return 1
			end
		elseif terrain then
			self:path_w32(u.path[n], pool.SPEED, speed)
			u.movement_state[n] = M.BLOCKED_BY_TERRAIN
			return 1
		end
		local x, y = sprite_pos(self, n)
		if mv.next_x ~= x or mv.next_y ~= y then
			local c = u.collision_interval[n]
			if c > 2 then
				u.collision_interval[n] = 2
			elseif c ~= 0 then
				u.collision_interval[n] = c - 1
			end
		end
		self:commit_unit_movement(n)
		if at_move_target(self, n) then
			u.movement_state[n] = M.ARRIVED
			return 0
		end
		if self:path_poll_retry(n) then
			self:path_set_retry(n, M.SCOUT_DELAY)
			u.movement_state[n] = M.SCOUT
		end
		return 0
	end

	-- MoveState_ScoutPath 0x42c030: when a moving neighbour's forecast position (30 steps ahead)
	-- meets the unit's own, the unit goes to state 27.
	local function state_scout_path(self, n)
		local u = self.u
		if self:update_movement_state(n, true) then return 1 end
		local near = self:moving_neighbors(n, M.SCOUT_RADIUS, M.SCOUT_RADIUS)
		local meets = 0
		if #near ~= 0 then
			local mx, my = self:forecast_position(n, M.SCOUT_STEPS)
			local points = {}
			for k, c in ipairs(near) do points[k] = { self:forecast_position(c, M.SCOUT_STEPS) } end
			local ext = self.dat.units.unit_extents
			local t = u.type[n]
			for k, c in ipairs(near) do
				local ct = u.type[c]
				local px, py = points[k][1], points[k][2]
				local top = s16(py - ext[ct * 4 + 1] + (-1 - ext[t * 4 + 3]))
				local bottom = s16(ext[ct * 4 + 3] + py + ext[t * 4 + 1] + 1)
				local right = s16(ext[ct * 4 + 2] + px + ext[t * 4] + 1)
				local left = s16(px - ext[ct * 4] + (-1 - ext[t * 4 + 2]))
				if mx >= left and mx <= right and my >= top and my <= bottom then meets = meets + 1 end
			end
		end
		u.movement_state[n] = meets > 0 and M.REFOLLOW or M.FOLLOW
		return 1
	end

	-- The collision states keep the blocking unit (as a reference), the step speed and the
	-- slide direction in the path record.
	local BLOCKER, SPEED, SLIDE = pool.BLOCKER, pool.SPEED, pool.SLIDE

	local function bump_interval(u, n)
		if u.collision_interval[n] ~= 0xff then u.collision_interval[n] = u.collision_interval[n] + 1 end
	end

	-- StopWalkingAnimation 0x42c810.
	local function stop_walking(self, n)
		local u = self.u
		if band(u.movement_flags[n], 2) ~= 0 and u.collision_interval[n] > 2 and band(u.status[n], M.STATUS_LOCKS) == 0 then
			u.movement_flags[n] = band(u.movement_flags[n], 0xfd)
			self.img:sprite_play(u.sprite[n], M.ANIM_WALKING_TO_IDLE, true)
		end
	end

	-- StopAtCurrentTarget 0x43a490.
	local function stop_at_target(self, n)
		local u = self.u
		if band(u.pathing_flags[n], 2) ~= 0 then u.pathing_flags[n] = bor(u.pathing_flags[n], 4) end -- 0x42ad10
		self:stop_flingy(n)
		local x, y = self:clamp_to_map(u.type[n], u.move_x[n], u.move_y[n])
		if x ~= u.move_x[n] or y ~= u.move_y[n] then
			u.move_x[n], u.move_y[n] = x, y
			u.waypoint_x[n], u.waypoint_y[n] = x, y
		end
		u.recent_order_timer[n] = 15
	end

	-- HaltAtTarget 0x42c7a0.
	local function halt_at_target(self, n)
		local u = self.u
		self:flingy_set_speed(n, 0)
		stop_at_target(self, n)
		local x, y = sprite_pos(self, n)
		self:set_move_target_point(n, x, y)
		u.status[n] = bor(u.status[n], M.STATUS_INTERCEPT)
		u.user_action[n] = band(u.user_action[n], 0xfd)
	end

	-- The waypoint direction check of the waiting states: false when the unit still turns.
	local function faces_waypoint(self, n)
		local u = self.u
		local x, y = sprite_pos(self, n)
		return u.velocity_direction[n] == self:direction_to(x, y, u.waypoint_x[n], u.waypoint_y[n])
	end

	-- MoveState_FixCollision 0x42b3b0 (28): what to do about the blocking unit.
	local function state_fix_collision(self, n)
		local u = self.u
		if self:update_movement_state(n, true) then return 1 end
		bump_interval(u, n)
		local k = u.path[n]
		local c = self:unit_from_ref(self:path_u32(k, BLOCKER))
		if c == 0 or band(self.img.spr.flags[u.sprite[c]], 0x20) ~= 0 or band(u.pathing_flags[c], 1) == 0
			or band(u.status[c], M.STATUS_IGNORED) ~= 0 or c == n or not self:units_collide(c, n) then
			u.movement_state[n] = M.FOLLOW
			return 0
		end
		local t = u.type[n]
		local cs = u.sprite[c]
		local cx, cy = s16(self.img.spr.x[cs]), s16(self.img.spr.y[cs])
		local action
		if u.move_unit[n] ~= 0 then error("movement: blocking unit in reach of the target (0x4229a0)", 0) end
		if self:footprint_touches(c, cx, cy, t, u.move_x[n], u.move_y[n]) then
			if u.collision_interval[c] < 0x1e
				and not self:footprint_touches(c, u.move_x[c], u.move_y[c], t, u.move_x[n], u.move_y[n]) then
				action = self:blocker_heading_away(n, c) and 4 or 6
			else
				action = (u.move_unit[n] ~= 0 and 1 or 0) + 2
			end
		elseif self:footprint_touches(c, cx, cy, t, u.waypoint_x[n], u.waypoint_y[n]) then
			if u.collision_interval[c] < 0x1e
				and not self:footprint_touches(c, u.move_x[c], u.move_y[c], t, u.waypoint_x[n], u.waypoint_y[n]) then
				action = self:blocker_heading_away(n, c) and 4 or 6
			else
				action = 3
			end
		end
		if not action and band(u.movement_flags[c], 2) ~= 0 then
			local side = self:cardinal_side(n, c)
			local v = u.velocity_direction[c]
			if (side == 0 and (v <= 0x40 or v >= 0xc0)) or (side == 1 and v <= 0x80)
				or (side == 2 and v >= 0x40 and v <= 0xc0) or (side == 3 and (v == 0 or v >= 0x80)) then
				action = 6
			end
		end
		if not action then
			local cstate = u.collision_interval[c] > 2 and M.WAIT_FREE or u.movement_state[c]
			if not at_move_target(self, c) and band(u.movement_flags[c], 2) ~= 0 and cstate ~= M.WAIT_FREE then
				action = (cstate == M.FOLLOW or cstate == M.SCOUT) and 6 or 4
			else
				action = self:slide_direction(n, c) >= 0 and 7 or 5
			end
		end
		if (self.frame - self:path_u32(k, pool.FRAME)) % 4294967296 < 7 and action >= 3 and action <= 5 then
			stop_walking(self, n)
			return 0
		end
		if action == 1 or action == 2 then
			halt_at_target(self, n)
			if action == 1 then u.status[n] = band(u.status[n], bnot(M.STATUS_INTERCEPT)) end
			u.movement_state[n] = M.ARRIVED
		elseif action == 3 then
			u.movement_state[n] = M.REPATH_FAILED
		elseif action == 4 then
			u.movement_state[n] = M.REPATH_MOVERS
		elseif action == 5 then
			u.movement_state[n] = M.BLOCKED_REPATH
			self:flingy_set_speed(n, 0)
		elseif action == 6 then
			u.movement_state[n] = M.WAIT_FREE
		elseif action == 7 then
			self:path_w32(k, BLOCKER, self:unit_ref(c))
			self:path_w8(k, SLIDE, self:slide_direction(n, c))
			u.movement_state[n] = M.SLIDE_PREP
		end
		return 0
	end

	-- MoveState_WaitFree 0x42c670 (29): wait while the blocker stays; after 25 tries a coin
	-- toss slides or re-paths.
	local function state_wait_free(self, n)
		local u = self.u
		bump_interval(u, n)
		if self:update_movement_state(n, true) then return 1 end
		self:flingy_set_speed(n, self:path_u32(u.path[n], SPEED))
		self:prepare_flingy_movement(n)
		if self:ground_unit_collision_any(n) == 0 then
			if self:terrain_collision(n) then
				u.movement_state[n] = M.REPATH_FAILED
				return 0
			end
			self:commit_unit_movement(n)
			u.movement_state[n] = M.FOLLOW
			return 0
		end
		stop_walking(self, n)
		self:flingy_set_speed(n, 0)
		if not faces_waypoint(self, n) then self:flingy_commit_turn(n) end
		if u.collision_interval[n] >= 0x19 then
			if self.rng:raw(M.PATH_RANDOM) < 0x3fff then
				u.movement_state[n] = M.FORCE_FREE
				return 0
			end
			self:flingy_set_speed(n, 0)
			u.movement_state[n] = M.REPATH_MOVERS
		end
		return 0
	end

	-- MoveState_FaceTarget 0x42b2b0 (20): turn towards the waypoint before following again.
	local function state_face_target(self, n)
		local u = self.u
		if self:update_movement_state(n, true) then return 1 end
		if at_move_target(self, n) or band(u.movement_flags[n], 4) ~= 0 then
			u.movement_state[n] = M.ARRIVED
			return 1
		end
		if not faces_waypoint(self, n) then
			self:prepare_flingy_movement(n)
			self:flingy_commit_turn(n)
			return 0
		end
		if band(self:path_u8(u.path[n], pool.FLAGS), 1) ~= 0 then
			u.movement_state[n] = M.REPATH_STALE
		else
			u.movement_state[n] = M.FOLLOW
		end
		return 1
	end

	-- MoveState_RepathMovers 0x42bf60 (24): a new path that treats every unit as an obstacle.
	local function state_repath_movers(self, n)
		local u = self.u
		self:path_free(n)
		local mx, my, mu = u.move_x[n], u.move_y[n], u.move_unit[n]
		self.path_all_units = 1
		local ok = self:advance_path_waypoint(n, mx, my)
		self.path_all_units = 0
		if ok and band(u.status[n], M.STATUS_INTERCEPT) == 0 then
			u.movement_state[n] = M.FACE_TARGET
			return 1
		end
		u.move_x[n], u.move_y[n], u.move_unit[n] = mx, my, mu
		u.movement_state[n] = M.BLOCKED_REPATH
		return 0
	end

	-- The blocker of the stored path becomes the path obstacle (0x69b57c) and the path goes.
	local function drop_blocked_path(self, n)
		local k = self.u.path[n]
		if k ~= 0 then
			self.path_subject = self:unit_from_ref(self:path_u32(k, BLOCKER))
			self:path_free(n)
		end
	end

	-- MoveState_RepathBlocked 0x42c570 (19).
	local function state_repath_blocked(self, n)
		local u = self.u
		drop_blocked_path(self, n)
		if not at_move_target(self, n) and band(u.movement_flags[n], 4) == 0 then
			local ok = self:advance_path_waypoint(n, u.move_x[n], u.move_y[n])
			self.path_subject = 0
			if not ok then
				u.movement_state[n] = M.FAILED
				return 1
			end
			u.movement_state[n] = M.FACE_TARGET
			return 0
		end
		u.movement_state[n] = M.ARRIVED
		return 1
	end

	-- MoveState_RepathAround 0x42bf10 (23).
	local function state_repath_around(self, n)
		local u = self.u
		drop_blocked_path(self, n)
		local ok = self:advance_path_waypoint(n, u.move_x[n], u.move_y[n])
		self.path_subject = 0
		u.movement_state[n] = ok and M.FOLLOW or M.FAILED
		return 1
	end

	-- MoveState_GetFree 0x42bb80 (30).
	local function state_slide_check(self, n)
		local u = self.u
		bump_interval(u, n)
		if self:update_movement_state(n, true) then return 1 end
		self:flingy_set_speed(n, self:path_u32(u.path[n], SPEED))
		self:prepare_flingy_movement(n)
		local c = self:ground_unit_collision_any(n)
		if c == 0 then
			if self:terrain_collision(n) then
				u.movement_state[n] = M.REPATH_FAILED
				return 0
			end
			self:commit_unit_movement(n)
			u.movement_state[n] = M.FOLLOW
			return 0
		end
		stop_walking(self, n)
		if not faces_waypoint(self, n) then
			self:flingy_commit_turn(n)
			return 0
		end
		self:path_free(n)
		if self:slide_direction(n, c) < 0 then
			u.movement_state[n] = M.REPATH_MOVERS
			return 0
		end
		error("movement: slide record written through a released path (0x42bb80)", 0)
	end

	-- MoveState_SlidePrep 0x42c390 (31): the slide speed (at most 0x100 for flingy movers).
	local function state_slide_prep(self, n)
		local u = self.u
		if self:update_movement_state(n, true) then return 1 end
		local k = u.path[n]
		if u.movement_type[n] == 0 then
			local v = int.s32(self:path_u32(k, SPEED))
			if v >= 0x200 then
				self:path_w32(k, SPEED, int.idiv(v, 2))
			elseif v > 0x100 then
				self:path_w32(k, SPEED, 0x100)
			end
		end
		local v = int.s32(self:path_u32(k, SPEED))
		u.speed1[n], u.speed2[n] = v, v
		u.heading2[n] = u.velocity_direction[n]
		self:flingy_recompute_velocity(n)
		u.movement_state[n] = M.SLIDE
		return 1
	end

	-- MoveState_SlideFree 0x42c1e0 (32): slide sideways in steps of at most 0x100 while the
	-- same unit blocks the way to the waypoint.
	local function state_slide_free(self, n)
		local u = self.u
		local next_state = M.SLIDE
		local after = 0
		bump_interval(u, n)
		if self:update_movement_state(n, true) then return 1 end
		local k = u.path[n]
		local c = self:unit_from_ref(self:path_u32(k, BLOCKER))
		u.movement_flags[n] = bor(u.movement_flags[n], 1)
		u.velocity_direction2[n] = self:path_u8(k, SLIDE)
		self:flingy_turn_velocity(n)
		u.velocity_direction[n] = u.heading2[n]
		self:flingy_turn_heading(n)
		self.img:sprite_set_facing(u.sprite[n], u.heading[n])
		if u.movement_type[n] == 2 then
			u.speed1[n] = u.top_speed[n]
		else
			self:flingy_progress_speed(n)
		end
		local dir = u.velocity_direction[n]
		local speed = u.speed1[n]
		u.speed2[n] = speed
		local left = speed
		while left > 0 do
			local step = left > 0x100 and 0x100 or left
			left = left - 0x100
			u.movement_flags[n] = band(u.movement_flags[n], 0xfe)
			self:prepare_movement_toward(n, self:path_u8(k, SLIDE), step)
			if self:ground_unit_collision_any(n) ~= 0 or self:terrain_collision(n) then
				next_state = M.FORCE_FREE
				break
			end
			self:commit_unit_movement(n)
			u.velocity_direction[n] = dir
			self:flingy_update_desired(n)
			self:prepare_movement_toward(n, u.heading2[n], speed)
			after = self:ground_unit_collision_any(n)
			if after ~= c then break end
		end
		u.speed1[n], u.speed2[n] = speed, speed
		u.heading2[n], u.velocity_direction[n] = dir, dir
		self:flingy_recompute_velocity(n)
		if next_state ~= M.SLIDE then
			u.movement_state[n] = next_state
			return 0
		end
		if after == 0 then
			u.movement_flags[n] = bor(u.movement_flags[n], 1)
			k = u.path[n]
			if k ~= 0 and (self.frame - self:path_u32(k, pool.FRAME)) % 4294967296 < 0x96 then
				u.movement_state[n] = M.FOLLOW
				return 0
			end
			u.movement_state[n] = M.BLOCKED_REPATH
		end
		return 0
	end

	local STATES = {
		[M.INIT] = state_init, [M.AT_REST] = state_at_rest, [M.DORMANT] = state_dormant,
		[M.CHECK_ILLEGAL] = state_check_illegal, [M.START_MOVE] = state_start_path, [M.RETRY] = state_retry,
		[M.REPATH_STALE] = state_repath_stale, [M.REPATH] = state_repath, [M.ARRIVED] = state_arrived,
		[M.FOLLOW] = state_follow_path, [M.SCOUT] = state_scout_path,
		[M.BLOCKED_BY_UNIT] = state_fix_collision, [M.WAIT_FREE] = state_wait_free,
		[M.FACE_TARGET] = state_face_target, [M.REPATH_MOVERS] = state_repath_movers,
		[M.BLOCKED_REPATH] = state_repath_blocked, [M.REPATH_FAILED] = state_repath_around,
		[M.SLIDE_START] = state_slide_check, [M.SLIDE_PREP] = state_slide_prep, [M.SLIDE] = state_slide_free,
		[M.REFOLLOW] = function(self, n)
			self.u.movement_state[n] = M.FOLLOW
			return 1
		end,
	}

	-- ExecuteUnitMovement 0x42ad30. move_reveal (0x665b44) tells PrepareUnitMovement to
	-- refresh the unit's vision; the states of hidden units clear it.
	function G:execute_movement(n)
		local u = self.u
		self.move_reveal = self.update_tiles
		while true do
			local state = u.movement_state[n]
			local r
			if STATES[state] then
				r = STATES[state](self, n)
			elseif state == M.NOBRK then
				if band(u.status[n], M.STATUS_NOBRK) ~= 0 then return end
				u.movement_state[n] = M.INIT
				r = 1
			elseif state == M.HIDDEN then
				self.move_reveal = false
				return
			elseif state == M.HIDDEN_IN_TRANSPORT then
				unported(state, 0x40cf50)
			elseif NATIVE[state] then
				unported(state, NATIVE[state])
			else
				return -- 2 (dead), 8 and the unused states
			end
			if r == 0 then return end
		end
	end

	-- StopFlingy 0x40ce10: the move target becomes the position unless the unit is already
	-- turning towards a stop.
	function G:stop_flingy(n)
		local u = self.u
		local flags = u.movement_flags[n]
		if band(flags, 4) ~= 0 then return end
		u.move_x[n], u.move_y[n] = u.x[n], u.y[n]
		u.movement_flags[n] = band(flags, 0x8f)
		u.move_unit[n] = 0
		u.waypoint_x[n], u.waypoint_y[n] = u.move_x[n], u.move_y[n]
		if band(flags, 2) ~= 0 and u.movement_type[n] == 0 then
			error("movement: stopping a moving unit at its halt distance (0x40bf00)", 0)
		end
	end

	-- StopMovementAndFace 0x43a4e0 (AbortMoveToLegal 0x42ad10 always lets it run).
	function G:stop_movement(n)
		local u = self.u
		if band(u.pathing_flags[n], 2) ~= 0 then u.pathing_flags[n] = bor(u.pathing_flags[n], 4) end
		self:stop_flingy(n)
		local x, y = self:clamp_to_map(u.type[n], u.move_x[n], u.move_y[n])
		if x ~= u.move_x[n] or y ~= u.move_y[n] then
			u.move_x[n], u.move_y[n] = x, y
			u.waypoint_x[n], u.waypoint_y[n] = x, y
		end
		u.recent_order_timer[n] = 15
		self:flingy_set_target_waypoint(n, u.move_x[n], u.move_y[n])
	end

	-- PrepareUnitMovement 0x43a5f0.
	function G:prepare_movement(n)
		local u = self.u
		self:execute_movement(n)
		if self.move_reveal then self:unit_update_vision(n) end
		if band(u.status[n], 1) == 0 or u.subunit[n] == 0 then return end
		if band(self.dat.units.special_ability_flags[u.type[n]], M.DAT_SUBUNIT) ~= 0 then return end
		error("movement: subunit of a moving unit (0x43a800)", 0)
	end
end

return M
