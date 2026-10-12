-- Order dispatch of the unit step (DispatchMainOrder 0x439420 and DispatchSecondaryOrder
-- 0x439310; atlas P10) and the idle orders reached so far. The tables list the native
-- function of every order the dispatchers call; orders not ported yet raise with that
-- address (or with the dispatcher's address for orders it handles inline). Methods of the
-- game.
local int = require("game.simulation.int")

local band, bor = int.band, int.bor

local M = {}

M.DISPATCHER = 0x439420
M.INLINE = "inline"
M.PROCESS_PERIOD = 8 -- +0x85 counts down to the next periodic order step
M.DIE, M.STOP, M.GUARD, M.PLAYER_GUARD, M.NOTHING, M.HOLD_POSITION = 0, 1, 2, 3, 0x17, 0x6b
M.COMPUTER_AI, M.GUARD_POST = 0x9c, 0xa0
M.GUARD_RANDOM, M.GUARD_DELAY_MAX = 0x1d, 15 -- Order_Guard: RandRange(0, 15) on source 29
M.SEEK_PERIOD = 15
M.STATUS_ATTACKING, M.STATUS_HOLDING = 0x8, 0x8000000
M.STATUS_GROUND_MOVER, M.STATUS_HALTED = 0x20000, 0x8000
M.DAT_SUBUNIT, M.DAT_HERO = 0x10, 0x40
M.CONTROLLER_COMPUTER = 1
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
	[0x14] = M.INLINE, [0x15] = 0x4288f0, [0x17] = M.INLINE, [0x19] = 0x43ba90, [0x1b] = 0x43cf10,
	[0x1c] = 0x427c90, [0x1d] = 0x43cfe0, [0x20] = 0x42f230, [0x23] = 0x427c90, [0x24] = M.INLINE,
	[0x2b] = 0x43be80, [0x2e] = 0x43bc50, [0x2f] = 0x43ccc0, [0x31] = 0x428310, [0x32] = 0x41c290,
	[0x33] = 0x428660, [0x34] = 0x41c410, [0x35] = 0x41c310, [0x36] = 0x427c90, [0x37] = 0x41c3c0,
	[0x38] = 0x41c290, [0x39] = 0x41c290, [0x3a] = 0x41c790, [0x3b] = 0x41c860, [0x3c] = 0x427c90,
	[0x3d] = 0x41c790, [0x3e] = 0x41c790, [0x40] = 0x41c4e0, [0x43] = 0x42f550, [0x45] = 0x41c450,
	[0x46] = 0x43c970, [0x4a] = M.INLINE, [0x4d] = 0x43cd90, [0x4f] = 0x423480, [0x50] = 0x427c90,
	[0x51] = 0x423480, [0x52] = 0x4237a0, [0x53] = 0x4235c0, [0x54] = 0x423ce0, [0x55] = 0x4239a0,
	[0x56] = 0x423c30, [0x57] = 0x423810, [0x5a] = 0x423ce0, [0x5c] = M.INLINE, [0x5d] = M.INLINE,
	[0x5e] = M.INLINE, [0x5f] = M.INLINE, [0x60] = M.INLINE, [0x61] = 0x427290, [0x64] = 0x428a30,
	[0x65] = 0x43cd50, [0x67] = 0x43d250, [0x6b] = 0x41e9b0, [0x6c] = M.INLINE, [0x6f] = M.INLINE,
	[0x70] = M.INLINE, [0x71] = 0x434460, [0x72] = 0x427c90, [0x73] = 0x434460, [0x74] = 0x43c6f0,
	[0x75] = 0x43c810, [0x76] = 0x43c880, [0x77] = 0x434460, [0x78] = 0x434460, [0x79] = 0x434460,
	[0x7a] = 0x434460, [0x7d] = 0x436540, [0x7e] = M.INLINE, [0x7f] = 0x41eba0, [0x80] = 0x41ea40,
	[0x84] = M.INLINE, [0x85] = 0x431200, [0x86] = 0x41ef70, [0x87] = 0x41eeb0, [0x88] = M.INLINE,
	[0x89] = 0x42fc40, [0x8b] = M.INLINE, [0x8c] = M.INLINE, [0x8d] = M.INLINE, [0x8e] = 0x434460,
	[0x8f] = 0x434460, [0x90] = 0x434460, [0x91] = 0x434460, [0x92] = 0x434460, [0x93] = 0x434460,
	[0x94] = 0x430bd0, [0x98] = 0x41ebe0, [0x99] = 0x421440, [0x9a] = 0x4213c0, [0x9c] = 0x48cd70,
	[0x9d] = 0x41e510, [0x9e] = 0x41e890, [0x9f] = 0x48cee0, [0xa0] = 0x41e7f0, [0xa1] = 0x48d120,
	[0xa2] = 0x48cfb0, [0xa3] = 0x41e370, [0xa4] = 0x42f5b0, [0xa7] = 0x428490, [0xa8] = 0x4281a0,
	[0xa9] = 0x428150, [0xaa] = 0x4285a0, [0xab] = 0x4288a0, [0xac] = 0x4281f0, [0xad] = 0x428010,
	[0xaf] = 0x4361b0, [0xb4] = 0x434460, [0xb5] = 0x434460, [0xb6] = 0x430ce0, [0xb8] = 0x430d90,
	[0xb9] = 0x434460, [0xba] = 0x434460, [0xbb] = 0x4286f0,
}

-- Secondary orders of DispatchSecondaryOrder 0x439310 (0x25 is handled inline).
M.SECONDARY = {
	[0x25] = 0x439310, [0x26] = 0x41b060, [0x3f] = 0x41c630, [0x44] = 0x42f770, [0x4e] = 0x43d0d0,
	[0x66] = 0x43d1a0, [0x6d] = 0x4340d0, [0x6e] = 0x436020, [0x83] = 0x434710,
}

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
			if band(self.dat.units.special_ability_flags[u.type[n]], M.DAT_HERO) == 0 then return end
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
		if band(self.dat.units.special_ability_flags[u.type[n]], M.DAT_SUBUNIT) ~= 0 then
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

	-- The inline Nothing order: the next queued order, if any.
	local function order_nothing(self, n)
		if self.u.queue_head[n] ~= -1 then self:order_activate(n) end
	end

	local PORTED = {
		[M.GUARD] = order_guard, [M.PLAYER_GUARD] = order_player_guard, [M.NOTHING] = order_nothing,
		[M.STOP] = order_stop, [M.HOLD_POSITION] = order_hold_position,
	}

	-- AlertIfDisabledNearby 0x4af270: a disabled unit asks nearby computer units for help
	-- every 15 steps.
	local function disabled_alert(self, n)
		local u = self.u
		if u.order_timer[n] ~= 0 then return end
		u.order_timer[n] = 15
		if u.lockdown_timer[n] ~= 0 or u.maelstrom_timer[n] ~= 0 or band(u.status[n], M.STATUS_HALTED) ~= 0 then
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
		if M.FIRST[order] then unported("main", order, M.FIRST[order]) end
		local status = u.status[n]
		if self:unit_disabled(n) then
			disabled_alert(self, n)
			return
		end
		if band(status, M.STATUS_GROUND_MOVER) == 0 and band(status, M.STATUS_HALTED) ~= 0 then
			disabled_alert(self, n)
		end
		if M.EVERY_STEP[order] then unported("main", order, M.EVERY_STEP[order]) end
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

	-- DispatchSecondaryOrder 0x439310.
	function G:dispatch_secondary_order(n)
		local u = self.u
		local order = u.secondary_order[n]
		if order == M.SECONDARY_HALLUCINATION_END then
			error("orders: secondary order 0x95 (0x439310)", 0)
		end
		if self:unit_disabled(n) then return end
		if M.SECONDARY[order] then unported("secondary", order, M.SECONDARY[order]) end
	end
end

return M
