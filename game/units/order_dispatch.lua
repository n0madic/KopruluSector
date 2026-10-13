-- Order dispatch of the unit step (DispatchMainOrder 0x439420 and DispatchSecondaryOrder
-- 0x439310; atlas P10) and the idle orders reached so far. The tables list the native
-- function of every order the dispatchers call; orders not ported yet raise with that
-- address (or with the dispatcher's address for orders it handles inline). Methods of the
-- game.
local directions = require("game.data.directions")
local int = require("game.simulation.int")

local band, bor = int.band, int.bor
local bit_band = int.bit_band

local M = {}

M.DISPATCHER = 0x439420
M.INLINE = "inline"
M.PROCESS_PERIOD = 8 -- +0x85 counts down to the next periodic order step
M.DIE, M.STOP, M.GUARD, M.PLAYER_GUARD, M.NOTHING, M.HOLD_POSITION = 0, 1, 2, 3, 0x17, 0x6b
M.COMPUTER_AI, M.GUARD_POST = 0x9c, 0xa0
M.TURRET_GUARD = 0x04
M.BURROWED, M.RESCUE_PASSIVE, M.NEUTRAL = 0x75, 0xa1, 0xa2
M.LURKER, M.HYDRALISK, M.ORDER_UNIT_MORPH = 0x67, 0x26, 0x2a
M.STATUS_NOT_INTERRUPTIBLE = 0x1000
M.CONTROLLER_HUMAN = 2
M.RESCUE_REACH = 64
M.RESCUE_FLASH, M.RESCUE_SOUND = 45, 0x20
M.STATUS_IN_BUNKER, M.STATUS_GROUNDED_BUILDING = 0x20, 0x2
M.BUNKER_GUARD = 0x05
M.HATCHERY, M.HATCHERIES = 0x83, { [0x84] = true, [0x85] = true } -- Lair and Hive count as complete
M.DAT_RESOURCE_DEPOT = 0x1000
M.TRAP, M.TOWER_GUARD = 0xa7, 0x12
M.ATTACK_MOVE, M.PATROL, M.FOLLOW = 0x0e, 0x98, 0x31
M.FOLLOW_PERIOD, M.FOLLOW_RANGE, M.CHASE_RANGE = 7, 0x60, 0x1c0
M.NYDUS_CANAL, M.SHIELD_BATTERY = 0x86, 0xac
M.NEUTRAL_OWNER = 11
M.CONTROLLER_RESCUE, M.CONTROLLER_NEUTRAL = 3, 4
M.STATUS_AIR = 0x4
M.MEDIC = 0x22
M.STATUS_INVINCIBLE = 0x4000000
M.USER_HIDE = 4 -- user-action bit: hide before dying
M.FLOOR_TRAPS = { [0xcb] = true, [0xd1] = true }
M.STATUS_NO_COLLIDE = 0x200000
M.CLOAK_BITS, M.CLOAK_SHIFT = 0x300, 8
M.DETECTED_BY_NONE = 0x80000000
M.CRITTER, M.KAKARU = 0xa6, 0x5e
M.CRITTER_RANDOM, M.CRITTER_PAUSE_RANDOM = 0x30, 0x2f
M.CRITTER_STEP, M.KAKARU_STEP_MIN, M.KAKARU_STEP_MAX = 0x20, 0xa0, 0x280
M.CRITTER_PAUSE_MAX, M.CRITTER_PAUSE_MIN, M.CRITTER_BLOCKED_PAUSE = 0x4b, 15, 0x4b
M.MOVE, M.MOVE_UNIT = 0x06, 0x33 -- both run OrderMove 0x428660
M.GUARD_RANDOM, M.GUARD_DELAY_MAX = 0x1d, 15 -- Order_Guard: RandRange(0, 15) on source 29
M.SEEK_PERIOD = 15
M.STATUS_ATTACKING, M.STATUS_HOLDING = 0x8, 0x8000000
M.STATUS_CAN_MOVE, M.STATUS_HALTED = 0x20000, 0x8000
M.DAT_SUBUNIT, M.DAT_HERO = 0x10, 0x40
M.CONTROLLER_COMPUTER = 1
M.NO_TYPE = 0xe4
M.GUARD_KIND_POST = 1
M.SECONDARY_HALLUCINATION_END = 0x95

-- Orders handled before the disabled check (the dispatcher's first switch).
M.FIRST = { [0x00] = 0x41e630, [0x30] = 0x42f020, [0x81] = M.INLINE, [0xae] = 0x42f1b0 }

-- Orders run every step (the second switch).
M.EVERY_STEP = {
	[0x04] = 0x41f300, [0x16] = 0x41f170, [0x1a] = 0x43b900, [0x1e] = 0x41a580, [0x1f] = 0x42ee00,
	[0x21] = 0x41a870, [0x22] = 0x41ad30, [0x29] = 0x43bd50, [0x2a] = 0x43bac0, [0x2c] = 0x41a770,
	[0x2d] = 0x43b6e0, [0x41] = 0x41c950, [0x42] = 0x42f640, [0x47] = 0x435bf0, [0x48] = 0x435f00,
	[0x4b] = 0x43ac40, [0x4c] = 0x43ace0, [0x58] = 0x423c70, [0x59] = 0x423cc0, [0x5b] = 0x42f7e0,
	[0x62] = 0x436a80, [0x63] = 0x436be0, [0x69] = 0x42f830, [0x6a] = 0x42f9e0, [0x7c] = 0x4356c0,
	[0x82] = 0x42f510, [0x96] = 0x41b1f0, [0x97] = 0x42e0d0, [0x9b] = 0x4214d0, [0xa5] = 0x42a4d0,
	[0xa6] = 0x427ed0, [0xb0] = 0x436250, [0xb1] = 0x436310, [0xb2] = 0x436430, [0xb3] = 0x436520,
	[0xb7] = 0x42fa70,
}

-- Orders run when the process timer +0x85 expires (the third switch); 0x4be950 is the
-- original's empty function.
M.PERIODIC = {
	[0x01] = 0x428a10, [0x02] = 0x41e700, [0x03] = 0x41ee10, [0x06] = 0x428660, [0x07] = 0x41c910,
	[0x09] = 0x427c90, [0x0a] = 0x41e5c0, [0x0b] = 0x41e400, [0x0c] = 0x4be950, [0x0d] = 0x4be950,
	[0x0e] = 0x41e4a0, [0x0f] = 0x43c5a0, [0x11] = 0x427210, [0x12] = 0x41f090, [0x13] = 0x41f100,
	[0x14] = 0x436cf0, [0x15] = 0x4288f0, [0x17] = M.INLINE, [0x19] = 0x43ba90, [0x1b] = 0x43cf10,
	[0x1c] = 0x427c90, [0x1d] = 0x43cfe0, [0x20] = 0x42f230, [0x23] = 0x427c90, [0x24] = 0x435730,
	[0x2b] = 0x43be80, [0x2e] = 0x43bc50, [0x2f] = 0x43ccc0, [0x31] = 0x428310, [0x32] = 0x41c290,
	[0x33] = 0x428660, [0x34] = 0x41c410, [0x35] = 0x41c310, [0x36] = 0x427c90, [0x37] = 0x41c3c0,
	[0x38] = 0x41c290, [0x39] = 0x41c290, [0x3a] = 0x41c790, [0x3b] = 0x41c860, [0x3c] = 0x427c90,
	[0x3d] = 0x41c790, [0x3e] = 0x41c790, [0x40] = 0x41c4e0, [0x43] = 0x42f550, [0x45] = 0x41c450,
	[0x46] = 0x43c970, [0x4a] = 0x436030, [0x4d] = 0x43cd90, [0x4f] = 0x423480, [0x50] = 0x427c90,
	[0x51] = 0x423480, [0x52] = 0x4237a0, [0x53] = 0x4235c0, [0x54] = 0x423ce0, [0x55] = 0x4239a0,
	[0x56] = 0x423c30, [0x57] = 0x423810, [0x5a] = 0x423ce0, [0x5c] = 0x4376d0, [0x5d] = 0x437b50,
	[0x5e] = 0x4377a0, [0x5f] = 0x437850, [0x60] = 0x437a00, [0x61] = 0x427290, [0x64] = 0x428a30,
	[0x65] = 0x43cd50, [0x67] = 0x43d250, [0x6b] = 0x41e9b0, [0x6c] = M.INLINE, [0x6f] = 0x437ba0,
	[0x70] = 0x437890, [0x71] = 0x434460, [0x72] = 0x427c90, [0x73] = 0x434460, [0x74] = 0x43c6f0,
	[0x75] = 0x43c810, [0x76] = 0x43c880, [0x77] = 0x434460, [0x78] = 0x434460, [0x79] = 0x434460,
	[0x7a] = 0x434460, [0x7d] = 0x436540, [0x7e] = 0x4366f0, [0x7f] = 0x41eba0, [0x80] = 0x41ea40,
	[0x84] = 0x436820, [0x85] = 0x431200, [0x86] = 0x41ef70, [0x87] = 0x41eeb0, [0x88] = M.INLINE,
	[0x89] = 0x42fc40, [0x8b] = 0x436990, [0x8c] = 0x436a60, [0x8d] = 0x438560, [0x8e] = 0x434460,
	[0x8f] = 0x434460, [0x90] = 0x434460, [0x91] = 0x434460, [0x92] = 0x434460, [0x93] = 0x434460,
	[0x94] = 0x430bd0, [0x98] = 0x41ebe0, [0x99] = 0x421440, [0x9a] = 0x4213c0, [0x9c] = 0x48cd70,
	[0x9d] = 0x41e510, [0x9e] = 0x41e890, [0x9f] = 0x48cee0, [0xa0] = 0x41e7f0, [0xa1] = 0x48d120,
	[0xa2] = 0x48cfb0, [0xa3] = 0x41e370, [0xa4] = 0x42f5b0, [0xa7] = 0x428490, [0xa8] = 0x4281a0,
	[0xa9] = 0x428150, [0xaa] = 0x4285a0, [0xab] = 0x4288a0, [0xac] = 0x4281f0, [0xad] = 0x428010,
	[0xaf] = 0x4361b0, [0xb4] = 0x434460, [0xb5] = 0x434460, [0xb6] = 0x430ce0, [0xb8] = 0x430d90,
	[0xb9] = 0x434460, [0xba] = 0x434460, [0xbb] = 0x4286f0,
}

-- The hidden-unit dispatcher's switches (DispatchHiddenOrder 0x439e20, tables 0x439edc and
-- 0x439fbc): orders handled every step, then the periodic ones.
M.HIDDEN_TO_IDLE, M.HIDDEN_NONE = "idle", "none"
M.HIDDEN_FIRST = {
	[0x00] = 0x41e630, [0x03] = M.HIDDEN_TO_IDLE, [0x04] = M.HIDDEN_TO_IDLE, [0x11] = 0x427210,
	[0x16] = M.HIDDEN_TO_IDLE, [0x17] = M.HIDDEN_NONE, [0x18] = M.HIDDEN_NONE, [0x1d] = 0x43cfe0,
	[0x53] = 0x4235c0, [0x5c] = M.HIDDEN_TO_IDLE, [0x61] = 0x427290, [0x7d] = 0x436540, [0x96] = 0x41b1f0,
	[0x97] = 0x42e0d0, [0xa2] = M.HIDDEN_NONE, [0xaf] = M.HIDDEN_NONE, [0xb0] = M.HIDDEN_NONE,
}
M.HIDDEN_PERIODIC = { [0x05] = 0x41e760, [0x60] = 0x437a00, [0x9c] = 0x48d2d0, [0xa1] = 0x48d120 }

-- Secondary orders of DispatchSecondaryOrder 0x439310 (0x25 is handled inline).
M.SECONDARY = {
	[0x25] = 0x439310, [0x26] = 0x41b060, [0x3f] = 0x41c630, [0x44] = 0x42f770, [0x4e] = 0x43d0d0,
	[0x66] = 0x43d1a0, [0x83] = 0x434710,
}
M.SECONDARY_CLOAK, M.SECONDARY_DECLOAK, M.SECONDARY_NOTHING = 0x6d, 0x6e, 0x17

local NO_OP = 0x4be950

local function unported(kind, order, address)
	if address == M.INLINE then address = M.DISPATCHER end
	error(("orders: %s order 0x%02x (0x%x)"):format(kind, order, address), 0)
end

function M.install(G)
	-- Order_Guard 0x41e700: a random delay before the first seek, then the owner's idle
	-- order (ComputerAI or GuardPost for computers, PlayerGuard otherwise). The order byte
	-- changes without the order machinery.
	local function order_guard(self, n)
		local u = self.u
		u.order_timer[n] = self.rng:range(M.GUARD_RANDOM, 0, M.GUARD_DELAY_MAX) % 256
		if self.players[u.owner[n]].controller == M.CONTROLLER_COMPUTER then
			u.order[n] = M.COMPUTER_AI
			local ai = u.ai[n]
			if ai ~= 0 then
				if self.guards.kind[ai - 1] == M.GUARD_KIND_POST then u.order[n] = M.GUARD_POST end
				return
			end
			if bit_band(self.dat.units.special_ability_flags[u.type[n]], M.DAT_HERO) == 0 then return end
		end
		u.order[n] = M.PLAYER_GUARD
	end

	-- Order_PlayerGuard 0x41ee10: attack the remembered target while it stays a valid enemy,
	-- else seek one every 15 steps.
	local function order_player_guard(self, n)
		local u = self.u
		local target = u.auto_target[n]
		if target ~= 0 and u.ai[n] == 0 then
			if self:is_enemy(n, target) and self:can_attack(n, target, true) then
				error("orders: attack of the remembered target (0x4202c0)", 0)
			end
			u.auto_target[n] = 0
		end
		if u.order_timer[n] ~= 0 then return end
		u.order_timer[n] = M.SEEK_PERIOD
		if bit_band(self.dat.units.special_ability_flags[u.type[n]], M.DAT_SUBUNIT) ~= 0 then
			local base = u.subunit[n]
			self:flingy_set_target_waypoint(n, u.target_waypoint_x[base], u.target_waypoint_y[base])
		end
		if self:seek_range(n) ~= 0 and self:acquire_target(n) ~= 0 then
			error("orders: attack of an acquired target (0x4202c0)", 0)
		end
	end

	-- OrderStop 0x428a10.
	local function order_stop(self, n)
		self:stop_movement(n)
		self:reset_to_idle_animation(n)
		self:order_done(n)
	end

	-- Order_HoldPosition 0x41e9b0: stop once, then keep seeking a target in reach.
	local function order_hold_position(self, n)
		local u = self.u
		if u.order_state[n] == 0 then
			self:stop_movement(n)
			u.status[n] = bor(u.status[n], M.STATUS_HOLDING)
			local sub = u.subunit[n]
			if sub ~= 0 then u.status[sub] = bor(u.status[sub], M.STATUS_HOLDING) end
			u.order_state[n] = 1
		end
		-- TryAttackInPlace 0x420bd0 needs a target.
		if u.order_unit[n] ~= 0 then error("orders: attack in place (0x420bd0)", 0) end
		if u.order_timer[n] ~= 0 then return end
		u.order_timer[n] = M.SEEK_PERIOD
		local target = self:acquire_random_target(n)
		u.order_unit[n] = target
		if target ~= 0 then u.queue_timer[n] = 0 end
	end

	-- RequestMoveToPoint 0x43a070: the move target and the next target waypoint.
	function G:request_move_to_point(n, x, y)
		local ok = self:set_move_target_point(n, x, y)
		self:flingy_set_target_waypoint(n, x, y)
		return ok
	end

	-- RequestMoveToUnit 0x43a0a0: the move target and the next target waypoint follow the
	-- target unit.
	function G:request_move_to_unit(n, target)
		local ok = self:set_move_target_unit(n, target)
		local s = self.u.sprite[target]
		self:flingy_set_target_waypoint(n, int.s16(self.img.spr.x[s]), int.s16(self.img.spr.y[s]))
		return ok
	end

	-- MoveTowardTarget 0x438ba0: a target the owner sees, or one within 448 pixels, is
	-- followed; an unseen far target is chased to the last move target, where the order ends.
	-- False when the order ended.
	local function move_toward_target(self, n, target)
		local u = self.u
		if u.recent_order_timer[n] ~= 0 then return true end
		if bit_band(self.img.spr.visibility[u.sprite[target]], 2 ^ u.owner[n]) ~= 0
			or self:unit_box_within(n, M.CHASE_RANGE, target) then
			return self:set_move_target_unit(n, target)
		end
		if self:move_arrival(n) == 1 then
			self:order_done(n)
			return false
		end
		return true
	end

	-- OrderFollow 0x428310 (49): follow the target unit; every 7 steps an enemy becomes a
	-- Guard, a target in another ground region group stops the unit, a target of a rescuable
	-- or neutral player is followed to contact, others to within 96 pixels.
	local function order_follow(self, n)
		local u = self.u
		local target = u.order_unit[n]
		if target == 0 then
			self:order_done(n)
			return
		end
		if u.ai[n] ~= 0 then
			local max = int.sar(self.dat.units.hit_points[u.type[n]], 8)
			local hp = int.sar(u.hp[n] + 0xff, 8)
			if max == 0 then max = hp ~= 0 and hp or 1 end
			if hp == max then
				self:order_done(n)
				return
			end
		end
		if u.order_state[n] == 0 then
			if not self:request_move_to_unit(n, target) then return end
			local tt = u.type[target]
			if tt == M.NYDUS_CANAL or tt == M.SHIELD_BATTERY then
				error("orders: following into a Nydus Canal or to a Shield Battery (0x43c360, 0x42f310)", 0)
			end
			u.order_state[n] = 1
		end
		if u.order_timer[n] ~= 0 then return end
		u.order_timer[n] = M.FOLLOW_PERIOD
		local owner = u.owner[target]
		if owner == M.NEUTRAL_OWNER then owner = self.img.spr.owner[u.sprite[target]] end
		if self.alliance[u.owner[n]][owner] == 0 and self:can_attack(n, target, true) then
			self:issue_order(n, M.GUARD)
			return
		end
		if bit_band(u.status[n], M.STATUS_AIR) == 0 and not self:same_ground_group(n, target) then
			self:stop_movement(n)
			return
		end
		local controller = self.players[u.owner[target]].controller
		if (controller == M.CONTROLLER_RESCUE or controller == M.CONTROLLER_NEUTRAL)
			and not self:unit_box_within(n, 1, target) then
			self:request_move_to_unit(n, target)
			return
		end
		if not self:unit_box_within(n, M.FOLLOW_RANGE, target) then
			move_toward_target(self, n, target)
		elseif bit_band(u.status[target], M.STATUS_GROUNDED_BUILDING) ~= 0 or u.queue_head[n] ~= -1 then
			self:order_done(n)
		else
			self:stop_movement(n)
		end
	end

	-- OrderMove 0x428660: set out once, then end the order at the move target.
	local function order_move(self, n)
		local u = self.u
		local state = u.order_state[n]
		if state == 0 then
			local target = u.order_unit[n]
			if target ~= 0 then
				if self:request_move_to_unit(n, target) then u.order_state[n] = 1 end
			elseif self:request_move_to_point(n, u.order_x[n], u.order_y[n]) then
				u.order_state[n] = 1
			end
		elseif state == 1 then
			local s = u.sprite[n]
			if u.move_x[n] == int.s16(self.img.spr.x[s]) and u.move_y[n] == int.s16(self.img.spr.y[s]) then
				self:order_done(n)
				if u.ai[n] ~= 0 then u.auto_target[n] = 0 end
			end
		end
	end

	-- The inline Nothing order: the next queued order, if any.
	local function order_nothing(self, n)
		if self.u.queue_head[n] ~= -1 then self:order_activate(n) end
	end

	-- Order_TurretGuard 0x41f300: the turret faces its base's facing waypoint, then guards.
	local function order_turret_guard(self, n)
		local u = self.u
		local base = u.subunit[n]
		self:flingy_set_target_waypoint(n, u.target_waypoint_x[base], u.target_waypoint_y[base])
		order_player_guard(self, n)
	end

	-- IsUnitInactive 0x41fe40: no sprite or the Die order.
	local function inactive(self, n)
		return self.u.sprite[n] == -1 or self.u.order[n] == M.DIE
	end

	-- A rescued unit's new order: BunkerGuard inside a bunker, else the human idle order.
	local function rescued_order(self, n, owner)
		local u = self.u
		self:change_owner(n, owner, true)
		self:issue_order(n, bit_band(u.status[n], M.STATUS_IN_BUNKER) ~= 0 and M.BUNKER_GUARD
			or self.dat.units.human_ai_idle[u.type[n]])
	end

	-- Order_RescuePassive 0x48d120: every other step the unit looks for a unit of a human
	-- player within 64 pixels and, when it finds one, joins that player: a complete resource
	-- depot brings every active unit of its player along. The rescuer's player hears the
	-- rescue and the unit's circle flashes (PlayRescueSound 0x446b30).
	local function order_rescue_passive(self, n)
		local u = self.u
		if u.order_timer[n] ~= 0 then return end
		u.order_timer[n] = 1
		local s = u.sprite[n]
		local x, y, r = self.img.spr.x[s], self.img.spr.y[s], M.RESCUE_REACH
		local s16 = int.s16
		local rescuer
		for _, c in ipairs(self:units_in_rect(s16(x - r), s16(y - r), s16(x + r), s16(y + r))) do
			if self.players[u.owner[c]].controller == M.CONTROLLER_HUMAN then
				rescuer = c
				break
			end
		end
		if not rescuer then return end
		local owner = u.owner[rescuer]
		local t = u.type[n]
		local status = u.status[n]
		if (bit_band(status, 1) ~= 0 or M.HATCHERIES[t] or (t == M.HATCHERY and self:is_morphing_zerg_building(n)))
			and bit_band(status, M.STATUS_GROUNDED_BUILDING) ~= 0
			and bit_band(self.dat.units.special_ability_flags[t], M.DAT_RESOURCE_DEPOT) ~= 0 then
			local m = self.player_units[u.owner[n]]
			while m ~= 0 do
				local nxt = u.player_next[m]
				if not inactive(self, m) then rescued_order(self, m, owner) end
				m = nxt
			end
		else
			rescued_order(self, n, owner)
		end
		if u.owner[n] == self.local_player then
			self.img.spr.sel_timer[u.sprite[n]] = M.RESCUE_FLASH
			local sound = self.img.sound
			if sound then sound(M.RESCUE_SOUND + self.players[self.local_player].race, self.img.spr.x[s], self.img.spr.y[s]) end
		end
	end

	-- SetCloakState 0x41d4c0: the two cloak bits of the status (0 visible, 3 cloaked).
	function G:set_cloak_bits(n, v)
		local u = self.u
		u.status[n] = bor(band(u.status[n], int.bnot(M.CLOAK_BITS)), v * 2 ^ M.CLOAK_SHIFT)
	end

	-- OrderTrap 0x428490 (167): a trap hides (no collision, cloaked, floor traps flat), then
	-- looks for prey every 15 steps while it is not disabled.
	local function order_trap(self, n)
		local u = self.u
		local state = u.order_state[n]
		if state == 0 then
			u.status[n] = bor(u.status[n], M.STATUS_NO_COLLIDE)
			if M.FLOOR_TRAPS[u.type[n]] then self.img.spr.elevation[u.sprite[n]] = 0 end
			self:set_cloak_bits(n, 3)
			u.visibility[n] = M.DETECTED_BY_NONE
			u.secondary_timer[n] = 0
			u.order_state[n] = 1
		elseif state == 1 and not self:unit_disabled(n) and u.order_timer[n] == 0 then
			u.order_timer[n] = M.SEEK_PERIOD
			-- TODO: the trap's reveal and attack (RevealTrap 171, AttackUnit, HideTrap 170) arrive
			-- with combat in phase 4.
			if self:acquire_random_target(n) ~= 0 then error("orders: a trap attacks its prey (0x428490)", 0) end
		end
	end

	-- Order_TowerGuard 0x41f090 (0x12): a tower that is not disabled seeks a target every
	-- 15 steps.
	local function order_tower_guard(self, n)
		local u = self.u
		if self:unit_disabled(n) or u.order_timer[n] ~= 0 then return end
		u.order_timer[n] = M.SEEK_PERIOD
		local target = self:acquire_random_target(n)
		u.order_unit[n] = target
		-- TODO: TowerAttack 0x13 and the attack status arrive with combat in phase 4.
		if target ~= 0 then error("orders: a tower attacks (0x41f090)", 0) end
	end

	-- The remembered target is an enemy the unit may attack: visible, not invincible and
	-- detected (the shared test of 0x420600 and Patrol).
	local function attackable_auto_target(self, n)
		local u = self.u
		local a = u.auto_target[n]
		return a ~= 0 and self:is_enemy(n, a) and bit_band(self.img.spr.flags[u.sprite[a]], 0x20) == 0
			and bit_band(u.status[a], M.STATUS_INVINCIBLE) == 0 and not self:is_undetected_by(a, n)
	end

	-- UnlockAndEndOrder 0x429d00.
	local function unlock_and_end(self, n)
		local u = self.u
		u.status[n] = band(u.status[n], int.bnot(M.STATUS_NOT_INTERRUPTIBLE))
		self:order_done(n)
	end

	-- AttackMovePrepare 0x420600: set out to the order target once; false when the move
	-- fails or the remembered target is attacked instead.
	local function attack_move_prepare(self, n)
		local u = self.u
		if u.order_state[n] == 0 then
			if not self:request_move_to_point(n, u.order_x[n], u.order_y[n]) then return false end
			u.order_state[n] = 1
		end
		if attackable_auto_target(self, n) then
			-- TODO: the attack on the remembered target (0x429c40, 0x429da0) arrives with combat in
			-- phase 4, the computer's choice (0x41f6c0) with the AI in phase 7.
			error("orders: attack-move on the remembered target (0x420600)", 0)
		end
		return true
	end

	-- Order_AttackMove 0x41e4a0 (0x0e): every 15 steps the order ends at the move target or
	-- seeks a target on the way (0x4206e0).
	local function order_attack_move(self, n)
		local u = self.u
		if not attack_move_prepare(self, n) or u.order_timer[n] ~= 0 then return end
		u.order_timer[n] = M.SEEK_PERIOD
		local s = u.sprite[n]
		if u.move_x[n] == int.s16(self.img.spr.x[s]) and u.move_y[n] == int.s16(self.img.spr.y[s]) then
			self:order_done(n)
			return
		end
		if self:seek_range(n) == 0 then return end
		if u.ai[n] ~= 0 then error("orders: computer attack-move target (0x41f6c0)", 0) end
		if self:acquire_target(n) ~= 0 then error("orders: attack-move on an acquired target (0x4206e0)", 0) end
	end

	-- Order_Patrol 0x41ebe0 (0x98): the first leg queues the way back; each arrival queues the
	-- next leg (back to the order target after an interception); on the way the unit seeks
	-- targets every 15 steps.
	local function order_patrol(self, n)
		local u = self.u
		local s = u.sprite[n]
		if u.order_state[n] == 0 then
			local head = u.queue_head[n]
			if head == -1 or self.orders.order[head] ~= M.PATROL then
				self:issue_order_packed(n, M.PATROL, u.order_x[n], u.order_y[n], u.order_unit[n])
				self:order_queue(n, M.PATROL, int.s16(self.img.spr.x[s]), int.s16(self.img.spr.y[s]), 0, M.NO_TYPE)
				return
			end
			if not self:request_move_to_point(n, u.order_x[n], u.order_y[n]) then return end
			u.order_state[n] = 1
		end
		if attackable_auto_target(self, n) then
			error("orders: patrol attack on the remembered target (0x41ebe0)", 0)
		end
		if u.order_timer[n] ~= 0 then return end
		u.order_timer[n] = M.SEEK_PERIOD
		if u.type[n] == M.MEDIC then error("orders: patrolling Medic's heal search (0x431ce0)", 0) end
		local x, y = int.s16(self.img.spr.x[s]), int.s16(self.img.spr.y[s])
		if u.move_x[n] == x and u.move_y[n] == y then
			if self:move_arrival(n) == 2 then x, y = u.order_x[n], u.order_y[n] end
			self:order_queue(n, M.PATROL, x, y, 0, M.NO_TYPE)
			unlock_and_end(self, n)
			return
		end
		if self:seek_range(n) ~= 0 and self:acquire_target(n) ~= 0 then
			error("orders: patrol attack on an acquired target (0x41ebe0)", 0)
		end
	end

	-- Order_Neutral 0x48cfb0: a unit attacked by a visible unit of another player turns
	-- hostile; the attacker is forgotten either way.
	local function order_neutral(self, n)
		local u = self.u
		local attacker = u.auto_target[n]
		if attacker == 0 then return end
		if bit_band(self.img.spr.flags[u.sprite[attacker]], 0x20) == 0 and not self:is_undetected_by(attacker, n)
			and u.owner[n] ~= u.owner[attacker] then
			error("orders: a neutral unit turns hostile (0x48cfb0)", 0)
		end
		u.auto_target[n] = 0
	end

	-- Order_Burrowed 0x43c810: a Lurker ends the order at once; a computer unit with an AI
	-- reacts every 7 steps.
	local function order_burrowed(self, n)
		local u = self.u
		local t = u.type[n]
		if t == M.LURKER then
			u.status[n] = band(u.status[n], int.bnot(M.STATUS_NOT_INTERRUPTIBLE))
			self:order_done(n)
			return
		end
		local head = u.queue_head[n]
		if t == M.HYDRALISK and head ~= -1 and self.orders.order[head] == M.ORDER_UNIT_MORPH then
			error("orders: burrowed Hydralisk morph (0x43c810)", 0)
		end
		if u.ai[n] ~= 0 and u.order_timer[n] == 0 then
			error("orders: burrowed unit AI reaction (0x48d3a0, 0x41f6c0)", 0)
		end
	end

	-- OrderCritter 0x427ed0 (166): wander to a random point nearby (Kakaru farther and
	-- without pauses), pause, repeat.
	local function order_critter(self, n)
		local u = self.u
		local state = u.order_state[n]
		local t = u.type[n]
		if state == 0 then
			local dir = self.rng:range(M.CRITTER_RANDOM, 0, 0xff)
			local dist = M.CRITTER_STEP
			if t == M.KAKARU then dist = self.rng:range(M.CRITTER_RANDOM, M.KAKARU_STEP_MIN, M.KAKARU_STEP_MAX) end
			local s = u.sprite[n]
			local x = int.s16(int.sar(directions.x[dir] * dist, 8) + self.img.spr.x[s])
			local y = int.s16(int.sar(directions.y[dir] * dist, 8) + self.img.spr.y[s])
			x, y = self:clamp_to_map(t, x, y)
			u.order_x[n], u.order_y[n] = x, y
			self:set_move_target_point(n, x, y)
			u.order_state[n] = 1
		elseif state == 1 then
			local arrival = self:move_arrival(n)
			if arrival == 0 then return end
			if t == M.KAKARU then
				u.order_state[n] = 0
			elseif arrival == 2 then
				u.order_timer[n] = M.CRITTER_BLOCKED_PAUSE
				u.order_state[n] = 2
			else
				local r = self.rng:range(M.CRITTER_PAUSE_RANDOM, 0, M.CRITTER_PAUSE_MAX)
				u.order_state[n] = 2
				u.order_timer[n] = r > M.CRITTER_PAUSE_MIN and r % 256 or 0
			end
		elseif state == 2 and u.order_timer[n] == 0 then
			u.order_state[n] = 0
		end
	end

	-- Order_Die 0x41e630: a unit marked for hiding hides; a turret gives its main image to its
	-- base's sprite and is destroyed; a hidden unit is destroyed at once.
	-- TODO: the visible Death animation and the hallucination smoke arrive with combat in
	-- phase 4.
	local function order_die(self, n)
		local u, w = self.u, self.img
		if bit_band(u.user_action[n], M.USER_HIDE) ~= 0 then self:unit_hide(n) end
		local sub = u.subunit[n]
		if sub ~= 0 then
			w:sprite_take_main(u.sprite[n], u.sprite[sub])
			self:unit_destroy(sub)
			u.subunit[n] = 0
		end
		u.order_state[n] = 1
		if bit_band(w.spr.flags[u.sprite[n]], 0x20) == 0 then
			error("orders: the Death animation of a visible unit (0x41e630)", 0)
		end
		self:unit_hide(n)
		self:unit_destroy(n)
	end

	-- Orders of the every-step switch; the periodic switch follows them.
	local PORTED_EVERY_STEP = {
		[M.TURRET_GUARD] = order_turret_guard, [M.CRITTER] = order_critter,
	}

	local PORTED = {
		[M.GUARD] = order_guard, [M.PLAYER_GUARD] = order_player_guard, [M.NOTHING] = order_nothing,
		[M.STOP] = order_stop, [M.HOLD_POSITION] = order_hold_position, [M.MOVE] = order_move,
		[M.MOVE_UNIT] = order_move, [M.RESCUE_PASSIVE] = order_rescue_passive, [M.NEUTRAL] = order_neutral,
		[M.BURROWED] = order_burrowed, [M.TRAP] = order_trap,
		[M.TOWER_GUARD] = order_tower_guard, [M.ATTACK_MOVE] = order_attack_move, [M.PATROL] = order_patrol,
		[M.FOLLOW] = order_follow,
	}

	-- The ported handler of an order, run alone as the original-code probes do
	-- (tests/lua/test_orders_native.lua).
	function G:order_handler(order) return PORTED_EVERY_STEP[order] or PORTED[order] end

	-- AlertIfDisabledNearby 0x4af270: a disabled unit asks nearby computer units for help
	-- every 15 steps.
	local function disabled_alert(self, n)
		local u = self.u
		if u.order_timer[n] ~= 0 then return end
		u.order_timer[n] = 15
		if u.lockdown_timer[n] ~= 0 or u.maelstrom_timer[n] ~= 0 or bit_band(u.status[n], M.STATUS_HALTED) ~= 0 then
			error("orders: help request of a disabled unit (0x442b20 with 0x4aaaa0)", 0)
		end
	end

	-- AlertIfAcidSporesOrInterceptor 0x4af2c0.
	local function acid_or_interceptor_alert(self, n)
		local u = self.u
		if u.acid_spores[n] >= 4 then
			error("orders: acid spore alert (0x442b20 with 0x4aaaa0)", 0)
		end
		if not self.interceptor_alerted and u.type[n] == 0x49 and u.union_0xc0[n] ~= 0 then
			error("orders: interceptor alert (0x4af220)", 0)
		end
	end

	-- DispatchMainOrder 0x439420.
	function G:dispatch_main_order(n)
		local u = self.u
		local order = u.order[n]
		if order == M.DIE then
			order_die(self, n)
			return
		end
		if M.FIRST[order] then unported("main", order, M.FIRST[order]) end
		local status = u.status[n]
		if self:unit_disabled(n) then
			disabled_alert(self, n)
			return
		end
		if bit_band(status, M.STATUS_CAN_MOVE) == 0 and bit_band(status, M.STATUS_HALTED) ~= 0 then
			disabled_alert(self, n)
		end
		if PORTED_EVERY_STEP[order] then
			PORTED_EVERY_STEP[order](self, n)
		elseif M.EVERY_STEP[order] then
			unported("main", order, M.EVERY_STEP[order])
		end
		local timer = u.queue_timer[n]
		u.queue_timer[n] = (timer - 1) % 256
		if timer ~= 0 then return end
		u.queue_timer[n] = M.PROCESS_PERIOD
		acid_or_interceptor_alert(self, n)
		u.ai_action[n] = 0
		local f = PORTED[order]
		if f then
			f(self, n)
		elseif M.PERIODIC[order] and M.PERIODIC[order] ~= NO_OP then
			unported("main", order, M.PERIODIC[order])
		end
	end

	-- DispatchHiddenOrder 0x439e20: the orders of a hidden unit (inside a transport, bunker or
	-- building, or not placed). Guard orders become BunkerGuard inside a bunker, else Nothing;
	-- a few orders run every step; the rest only their periodic hidden handlers.
	function G:dispatch_hidden_order(n)
		local u = self.u
		local order = u.order[n]
		local first = M.HIDDEN_FIRST[order]
		if order == M.DIE then
			order_die(self, n)
			return
		elseif first == M.HIDDEN_TO_IDLE then
			self:issue_order(n, bit_band(u.status[n], M.STATUS_IN_BUNKER) ~= 0 and M.BUNKER_GUARD or M.NOTHING)
			return
		elseif first == M.HIDDEN_NONE then
			return
		elseif first then
			unported("hidden", order, first)
		end
		local timer = u.queue_timer[n]
		u.queue_timer[n] = (timer - 1) % 256
		if timer ~= 0 then return end
		u.queue_timer[n] = M.PROCESS_PERIOD
		if order == M.RESCUE_PASSIVE then
			order_rescue_passive(self, n)
		elseif M.HIDDEN_PERIODIC[order] then
			unported("hidden", order, M.HIDDEN_PERIODIC[order])
		end
	end

	-- DispatchSecondaryOrder 0x439310.
	function G:dispatch_secondary_order(n)
		local u = self.u
		local order = u.secondary_order[n]
		if order == M.SECONDARY_HALLUCINATION_END then
			error("orders: secondary order 0x95 (0x439310)", 0)
		end
		if self:unit_disabled(n) then return end
		self:secondary_cloak_order(n, order)
		if M.SECONDARY[order] then unported("secondary", order, M.SECONDARY[order]) end
	end

	-- The Cloak and Decloak secondary orders of both dispatchers (0x439310, UpdateHiddenUnit
	-- 0x43a770): Cloak counts a cloak source of the step (AddToCloakedList 0x4340d0), Decloak
	-- ends (0x436020).
	function G:secondary_cloak_order(n, order)
		if order == M.SECONDARY_CLOAK then
			self:cloak_list_add(n)
		elseif order == M.SECONDARY_DECLOAK then
			self:set_secondary_order(n, M.SECONDARY_NOTHING)
		end
	end
end

return M
