-- Unit movement states (ExecuteUnitMovement 0x42ad30 and its state functions; atlas P10).
-- The state byte +0x97 selects a function that returns 1 to run the next state in the same
-- step or 0 to stop. States reached so far are ported; every other state raises with the
-- native address of its function (plan §7 phase 3: no movement no-ops). Methods of the game.
local int = require("game.simulation.int")

local band, bor, bnot = int.band, int.bor, int.bnot
local s16 = int.s16

local M = {}

M.INIT, M.NOBRK, M.DEAD, M.HIDDEN_IN_TRANSPORT = 0, 1, 2, 4
M.HIDDEN, M.AT_REST, M.DORMANT, M.CHECK_ILLEGAL = 6, 9, 10, 12
M.REPLACED_ON_LEGAL, M.ROUTE = 13, 14
M.ARRIVED, M.FAILED, M.START_MOVE, M.RETRY, M.BLOCKED_REPATH, M.REPATH_STALE, M.REPATH = 11, 15, 17, 18, 19, 21, 22
M.REPATH_FAILED, M.FOLLOW, M.SCOUT, M.REFOLLOW = 23, 25, 26, 27
M.BLOCKED_BY_UNIT, M.BLOCKED_BY_TERRAIN = 28, 34
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
	[15] = 0x42b370, [16] = 0x42bfd0, [19] = 0x42c570, [20] = 0x42b2b0, [23] = 0x42bf10, [24] = 0x42bf60,
	[28] = 0x42b3b0, [29] = 0x42c670, [30] = 0x42bb80, [31] = 0x42c390, [32] = 0x42c1e0, [33] = 0x42bb00,
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

	-- IsPositionBlocked 0x42eab0: -1 outside the map or on unwalkable ground, else the
	-- largest ground unit in the way, else 0. Its collision callback 0x42e130 skips units
	-- without ground pathing, ignored units and the unit itself; any other unit needs
	-- CanUnitsCollide 0x42ebf0, which is not ported yet.
	function G:position_blocked(n, x, y)
		local u, ext = self.u, self.dat.units.unit_extents
		local t = u.type[n]
		if x - s16(ext[t * 4]) < 0 or s16(ext[t * 4 + 2]) + x >= self.map_w_px then return -1 end
		if y - s16(ext[t * 4 + 1]) < 0 or s16(ext[t * 4 + 3]) + y >= self.map_h_px then return -1 end
		for _, c in ipairs(self:spatial_query(self:type_box(t, x, y))) do
			if band(u.pathing_flags[c], 1) ~= 0 and band(u.status[c], M.STATUS_IGNORED) == 0 and c ~= n then
				error("movement: ground collision of two units (0x42ebf0)", 0)
			end
		end
		return self:unit_type_fits(t, x, y) and 0 or -1
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
		elseif k ~= 0 and band(self:path_u8(k, 0x18), 1) ~= 0 then
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
		local t = self:path_u8(k, 0x19)
		if t ~= 0 then
			self:path_w8(k, 0x19, t - 1)
			return 0
		end
		self.u.movement_state[n] = M.FOLLOW
		return 1
	end

	-- The dispatcher's inline state 21: a stale path is dropped unless the target changed.
	local function state_repath_stale(self, n)
		local k = self.u.path[n]
		self:path_w8(k, 0x18, band(self:path_u8(k, 0x18), 0xfe))
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
				self:path_w32(k, 0x10, self:unit_ref(blocker))
				self:path_w32(k, 0x14, speed)
				u.movement_state[n] = M.BLOCKED_BY_UNIT
				return 1
			end
		elseif terrain then
			self:path_w32(u.path[n], 0x14, speed)
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

	local STATES = {
		[M.INIT] = state_init, [M.AT_REST] = state_at_rest, [M.DORMANT] = state_dormant,
		[M.CHECK_ILLEGAL] = state_check_illegal, [M.START_MOVE] = state_start_path, [M.RETRY] = state_retry,
		[M.REPATH_STALE] = state_repath_stale, [M.REPATH] = state_repath, [M.ARRIVED] = state_arrived,
		[M.FOLLOW] = state_follow_path, [M.SCOUT] = state_scout_path,
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
