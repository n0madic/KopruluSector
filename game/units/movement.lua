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
M.START_MOVE, M.REPATH = 17, 22
M.STATUS_NOBRK, M.STATUS_IN_BUILDING, M.STATUS_BURROWED = 0x2000, 0x20, 0x10
M.STATUS_GROUND_MOVER, M.STATUS_AIR_MOVER = 0x20000, 0x10000
M.STATUS_COLLIDING, M.STATUS_NOT_AIR = 0x40000, 0x100000
M.STATUS_IGNORED = 0x200000
M.STATUS_LOCKS = 0x3000
M.SPRITE_HIDDEN = 0x20
M.DAT_SUBUNIT = 0x10

-- The functions of the states that are not ported yet, for their errors.
local NATIVE = {
	[3] = 0x42c610, [5] = 0x40cf50, [7] = 0x42b7f0, [11] = 0x42afe0, [13] = 0x42be70, [14] = 0x42be10,
	[15] = 0x42b370, [16] = 0x42bfd0, [17] = 0x42c400, [18] = 0x42ac20, [19] = 0x42c570, [20] = 0x42b2b0,
	[21] = 0x42ac20, [22] = 0x48eb90, [23] = 0x42bf10, [24] = 0x42bf60, [25] = 0x42b8d0, [26] = 0x42c030,
	[27] = 0x42b8d0, [28] = 0x42b3b0, [29] = 0x42c670, [30] = 0x42bb80, [31] = 0x42c390, [32] = 0x42c1e0,
	[33] = 0x42bb00, [34] = 0x42b700, [35] = 0x42c470,
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

	-- MoveState_AtRest 0x42b030.
	local function state_at_rest(self, n)
		local u = self.u
		local x, y = sprite_pos(self, n)
		local at_target = at_move_target(self, n)
		if at_target then
			u.collision_interval[n] = 0
		else
			local c = u.collision_interval[n]
			if c >= 3 then
				u.collision_interval[n] = 2
			elseif c ~= 0 then
				u.collision_interval[n] = c - 1
			end
		end
		local settled = true
		local flags = u.movement_flags[n]
		if band(flags, 4) ~= 0 then
			settled = false
		elseif at_target then
			if band(flags, 2) ~= 0 then
				settled = false
			elseif u.target_waypoint_x[n] ~= u.x[n] or u.target_waypoint_y[n] ~= u.y[n] then
				error("movement: heading check of a unit at rest (0x40ba10)", 0)
			end
		end
		if not settled then
			error("movement: flingy step of a unit at rest (0x40bcd0, 0x42abb0)", 0)
		end
		local status = u.status[n]
		if band(status, M.STATUS_COLLIDING) ~= 0 and band(status, M.STATUS_NOT_AIR) ~= 0 then
			u.movement_state[n] = M.CHECK_ILLEGAL
			return 0
		end
		if not at_target and band(u.movement_flags[n], 4) == 0 then
			u.movement_state[n] = M.START_MOVE
			return 1
		end
		self:flingy_set_speed(n, 0)
		self:flingy_set_target_waypoint(n, x, y)
		u.movement_state[n] = M.DORMANT
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

	local STATES = {
		[M.INIT] = state_init, [M.AT_REST] = state_at_rest, [M.DORMANT] = state_dormant,
		[M.CHECK_ILLEGAL] = state_check_illegal,
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
