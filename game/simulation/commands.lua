-- Game commands of the turn (ExecuteTurnCommands 0x475d30; atlas P16): the command byte
-- selects a handler. Ported commands run here; every other command raises with its native
-- handler. Methods of the game.
local int = require("game.simulation.int")

local M = {}

M.EXECUTOR = 0x475d30
M.SYNC = 0x37
-- Handlers of the executor's switch.
M.HANDLERS = {
	[0x05] = 0x4720c0, [0x06] = 0x472f50, [0x07] = 0x4729e0, [0x08] = 0x472e20, [0x09] = 0x472fc0,
	[0x0a] = 0x473070, [0x0b] = 0x473170, [0x0c] = 0x472d30, [0x0d] = 0x473280, [0x0e] = 0x4731d0,
	[0x0f] = 0x473310, [0x10] = 0x472c70, [0x11] = 0x472be0, [0x12] = 0x472530, [0x13] = 0x472790,
	[0x14] = 0x472f10, [0x15] = 0x472270, [0x18] = 0x472880, [0x19] = 0x4728b0, [0x1a] = 0x473440,
	[0x1b] = 0x473670, [0x1c] = 0x473710, [0x1d] = 0x472b40, [0x1e] = 0x472e40, [0x1f] = 0x4722c0,
	[0x20] = 0x473540, [0x21] = 0x472560, [0x22] = 0x4726b0, [0x23] = 0x472a50, [0x25] = 0x473930,
	[0x26] = 0x473330, [0x27] = 0x472330, [0x28] = 0x4739a0, [0x29] = 0x472750, [0x2a] = 0x4737b0,
	[0x2b] = 0x4727c0, [0x2c] = 0x472410, [0x2d] = 0x472600, [0x2e] = 0x4724f0, [0x2f] = 0x4728e0,
	[0x30] = 0x472dc0, [0x31] = 0x4735e0, [0x32] = 0x473a60, [0x33] = 0x473630, [0x34] = 0x4734f0,
	[0x35] = 0x473ac0, [0x36] = 0x4733a0, [0x37] = 0x47a510, [0x38] = 0x473b60, [0x39] = 0x473b60,
	[0x3a] = 0x473bc0, [0x3b] = 0x473bc0, [0x55] = 0x476d10, [0x56] = 0x4732f0, [0x57] = 0x472da0,
	[0x58] = 0x472a00, [0x5a] = 0x473870, [0x5c] = 0x46a860,
}

M.SELECT, M.RIGHT_CLICK, M.TARGETED_ORDER, M.STOP, M.HOLD_POSITION = 0x09, 0x14, 0x15, 0x1a, 0x2b
-- Orders the targeted-order command ignores (its jump table 0x49ced4).
M.IGNORED_TARGETED = { [0] = true, [15] = true, [20] = true, [25] = true, [29] = true, [30] = true, [31] = true,
	[32] = true, [33] = true, [36] = true, [46] = true, [71] = true, [72] = true, [73] = true, [80] = true,
	[81] = true, [82] = true, [83] = true, [85] = true, [86] = true, [87] = true, [88] = true, [89] = true,
	[155] = true }
M.NO_TECH = 44
M.ORDER_ATTACK, M.ORDER_ATTACK_MOVE, M.ORDER_HEAL_MOVE = 8, 14, 177
M.ORDER_RALLY_UNIT, M.ORDER_RALLY_POINT, M.ORDER_PICKUP, M.ORDER_CONSUME = 39, 40, 112, 119
M.ORDER_NUKE_LAUNCH = 125
M.ORDER_FOLLOW = 49
M.BEACON_FIRST, M.BEACON_LAST = 0xc2, 0xc7 -- IsBeacon 0x421620
M.DAT_POWERUP, M.STATUS_BURROWED = 0x800, 0x10
-- Orders a grounded building takes without a right-click action (0x49cd1e).
M.BUILDING_TARGETED = { [39] = true, [40] = true, [67] = true, [95] = true }
M.STATUS_GROUNDED_BUILDING = 0x2
M.MEDIC, M.DEFILER, M.UNCLEAN_ONE = 0x22, 0x2e, 0x34
M.ORDER_STOP, M.ORDER_MOVE, M.ORDER_MOVE_FOW = 1, 6, 0x85
M.NO_ORDER = 0x17 -- the "no order" answer of the right-click handlers (and of 0x67cc68)
M.NO_TYPE = 0xe4
M.STATUS_HALLUCINATION = 0x40000000
M.USER_RIGHT_CLICKED = 2 -- CUnit +0x93
-- Current orders that a queued command replaces instead of queueing behind (0x42a8b0).
M.IDLE_ORDERS = { [2] = true, [3] = true, [0x17] = true, [0x5d] = true, [0x98] = true, [0xaf] = true }
-- GetHoldPositionOrder 0x41fae0 by unit type (else HoldPosition 0x6b).
M.HOLD_ORDERS = { [0x48] = 0x39, [0x52] = 0x39, [0x53] = 0x3e, [0x51] = 0x3e, [0x2d] = 0x6c, [0x31] = 0x6c,
	[0x2f] = 0x88, [0x32] = 0x88, [0x22] = 0xb2 }
M.HOLD_POSITION_ORDER = 0x6b
M.QUEUE_MAX, M.QUEUE_POOL_LIMIT = 8, 1800
M.STRING_QUEUE_FULL, M.STRING_QUEUE_POOL_FULL = 0x367, 0x369 -- stat_txt lines of 0x42a8b0

local function u16(s, at) return s:byte(at) + s:byte(at + 1) * 256 end

function M.install(G)
	-- IssueCommand 0x42a8b0: a queued (shift) order goes behind a busy unit's queue (at most 8
	-- highlighted orders, and while fewer than 1800 order nodes are in use), else it is issued
	-- at once (IssueOrderByKind 0x42a410). With a target the order takes the target's position
	-- (kind 1, IssueCommandOnUnit 0x42a880), else the point and fog-of-war type (kind 2,
	-- IssueCommandAtPoint 0x42a850).
	local KIND_UNIT, KIND_POINT = 1, 2

	local function issue_command(self, n, queued, order, kind, x, y, target, fow)
		local u = self.u
		if queued and self.dat.orders.queueable[order] ~= 0 and not M.IDLE_ORDERS[u.order[n]] then
			if u.queue_count[n] >= M.QUEUE_MAX then
				self:report_error(u.owner[n], M.STRING_QUEUE_FULL)
			elseif self.orders.allocated >= M.QUEUE_POOL_LIMIT then
				self:report_error(u.owner[n], M.STRING_QUEUE_POOL_FULL)
			elseif kind == KIND_UNIT then
				local tx, ty = 0, 0
				if target ~= 0 then
					local s = u.sprite[target]
					tx, ty = self.img.spr.x[s], self.img.spr.y[s]
				end
				self:order_queue(n, order, tx, ty, target, M.NO_TYPE)
			else
				self:order_queue(n, order, x, y, 0, fow)
			end
		elseif kind == KIND_UNIT then
			self:issue_order_target(n, order, target)
		else
			self:issue_order_fow(n, order, fow, x, y)
		end
	end

	local function issue_command_on_unit(self, n, queued, order, target)
		issue_command(self, n, queued, order, KIND_UNIT, 0, 0, target or 0, M.NO_TYPE)
	end

	local function issue_command_at_point(self, n, queued, order, fow, x, y)
		issue_command(self, n, queued, order, KIND_POINT, x, y, 0, fow)
	end

	-- The right-click handlers by GetUnitRightClickAction 0x422d90 (table 0x508e18): the
	-- order for unit n at (x, y) on `target` (0 for none) with fog-of-war type `fow`, and the
	-- target it keeps.
	local function right_click_none() return M.NO_ORDER end
	local RIGHT_CLICK = {
		[0] = right_click_none, -- 0x47ef40
		-- RightClickOrderNormal 0x47ef50.
		[1] = function(self, n, _, _, target, fow)
			if int.band(self.u.status[n], 2) ~= 0 then return M.NO_ORDER, target end
			if target == 0 then return fow ~= M.NO_TYPE and M.ORDER_MOVE_FOW or M.ORDER_MOVE, target end
			local tt = self.u.type[target]
			if (tt >= M.BEACON_FIRST and tt <= M.BEACON_LAST)
				or int.band(self.dat.units.special_ability_flags[tt], M.DAT_POWERUP) ~= 0 then
				return M.ORDER_MOVE, target
			end
			if self:is_enemy(n, target) then return M.ORDER_ATTACK, target end
			if self:is_transport(target) then error("commands: right click on a transport (0x437440)", 0) end
			return int.band(self.u.status[target], M.STATUS_BURROWED) ~= 0 and M.ORDER_MOVE or M.ORDER_FOLLOW, target
		end,
		[2] = function() error("commands: right click of a rally producer (0x47f070)", 0) end,
		[3] = function() error("commands: right click of a burrowed Lurker (0x47f010)", 0) end,
		[4] = function() error("commands: right click of a harvester (0x47f2c0)", 0) end,
		[5] = function() error("commands: right click of a repairer (0x47f180)", 0) end,
	}

	-- ExecuteRightClickCommand 0x47f680: the command player's selection moves or acts at
	-- (x, y) or on `target`.
	local function right_click(self, x, y, target, fow, queued)
		local u = self.u
		local f = self:group_formation(x, y, true)
		if target ~= 0 then
			fow = M.NO_TYPE
		elseif fow ~= M.NO_TYPE then
			error("commands: right click on a fog-of-war thingy (0x418210)", 0)
		end
		for _, n in ipairs(self:selected_units(self.command_player)) do
			local action = self:right_click_action(n)
			if int.band(u.status[n], M.STATUS_HALLUCINATION) ~= 0 and (action == 4 or action == 5) then action = 1 end
			self.second_order = M.NO_ORDER
			local order, t = RIGHT_CLICK[action](self, n, x, y, target, fow)
			if order == 0x27 then
				error("commands: right click into a transport (0x41bbf0)", 0)
			elseif order ~= M.NO_ORDER and n ~= target then
				if order == 0x28 then error("commands: right click of a transport (0x41bbc0)", 0) end
				if order == M.ORDER_ATTACK then error("commands: right-click attack of an enemy (0x47f7b3, phase 4)", 0) end
				if self:can_issue_order(order, self.command_player, n) == 1 then
					if t ~= 0 then
						issue_command_on_unit(self, n, queued, order, t)
						if self.second_order ~= M.NO_ORDER then
							issue_command_on_unit(self, n, queued, self.second_order, t)
						end
					else
						local px, py = x, y
						if fow == M.NO_TYPE then px, py = self:formation_point(n, f) end
						issue_command_at_point(self, n, queued, order, fow, px, py)
						if self.second_order ~= M.NO_ORDER then
							issue_command_at_point(self, n, queued, self.second_order, fow, px, py)
						end
					end
					u.user_action[n] = int.bor(u.user_action[n], M.USER_RIGHT_CLICKED)
				end
			end
		end
	end

	-- The commands that give every selected unit one order (Stop 0x473440, HoldPosition
	-- 0x4727c0): units that may not take it are skipped (their error message is a no-op).
	local function order_selection(self, queued, order_of)
		for _, n in ipairs(self:selected_units(self.command_player)) do
			local order = order_of(self, n)
			if self:can_issue_order(order, self.command_player, n) == 1 then issue_command_on_unit(self, n, queued, order) end
		end
	end

	-- ExecuteTargetedOrderForSelection 0x49cc40: every selected unit that may take the order
	-- (CanIssueOrder, or CanUseTech for a spell) gets it on the target or at the point (its
	-- formation point for a point without a fog-of-war type); Attack becomes the type's
	-- attack-unit or attack-move order, for its turret too.
	local function targeted_order(self, x, y, target, fow, order, queued)
		local u, d = self.u, self.dat
		local f = self:group_formation(x, y, d.orders.obstructable[order] ~= 0)
		if M.IGNORED_TARGETED[order] then return end
		for _, n in ipairs(self:selected_units(self.command_player)) do
			local tech = d.orders.technology_energy[order]
			local ok
			if tech == M.NO_TECH then
				ok = self:can_issue_order(order, self.command_player, n)
			else
				ok = self:can_use_tech(tech, self.command_player, n)
			end
			local status = u.status[n]
			local building = int.band(status, M.STATUS_GROUNDED_BUILDING) ~= 0
			local t = u.type[n]
			if ok ~= 1 then
				-- CommandErrorFeedback 0x472210: interface feedback only.
			elseif n == target and not ((building and (order == M.ORDER_RALLY_UNIT or order == M.ORDER_RALLY_POINT))
				or ((t == M.DEFILER or t == M.UNCLEAN_ONE) and order == M.ORDER_CONSUME)
				or (self:is_transport(n) and order == M.ORDER_PICKUP)) then
				-- a unit does not target itself
			elseif not (building and M.BUILDING_TARGETED[order]) and self:right_click_action(n) == 0 then
				-- units that take no right click take no targeted order
			elseif u.order[n] == M.ORDER_NUKE_LAUNCH and order ~= 0 then
				-- a launching silo keeps its order
			else
				local main, sub = order, M.NO_ORDER
				local skip = false
				if t == M.MEDIC and (order == M.ORDER_ATTACK_MOVE or order == M.ORDER_ATTACK) then
					main = M.ORDER_HEAL_MOVE
				elseif order == M.ORDER_ATTACK then
					main = target ~= 0 and d.units.attack_unit[t] or d.units.attack_move[t]
					skip = main == M.NO_ORDER
					local turret = u.subunit[n]
					if not skip and turret ~= 0 and int.band(d.units.special_ability_flags[u.type[turret]], 0x10) ~= 0 then
						sub = target ~= 0 and d.units.attack_unit[u.type[turret]] or d.units.attack_move[u.type[turret]]
					end
				end
				if not skip then
					if main == M.ORDER_RALLY_UNIT or main == M.ORDER_RALLY_POINT then
						error("commands: rally point of a targeted order (0x41bbf0, 0x41bbc0)", 0)
					end
					local q = queued ~= 0
					if target ~= 0 then
						issue_command_on_unit(self, n, q, main, target)
						if sub ~= M.NO_ORDER then issue_command_on_unit(self, u.subunit[n], q, sub, target) end
					else
						local px, py = x, y
						if fow == M.NO_TYPE then px, py = self:formation_point(n, f) end
						issue_command_at_point(self, n, q, main, fow, px, py)
						if sub ~= M.NO_ORDER then issue_command_at_point(self, u.subunit[n], q, sub, fow, px, py) end
					end
					u.user_action[n] = int.bor(u.user_action[n], M.USER_RIGHT_CLICKED)
				end
			end
		end
	end

	local HANDLERS = {
		[M.SELECT] = function(self, bytes)
			local refs = {}
			for k = 1, bytes:byte(2) do refs[k] = u16(bytes, 1 + 2 * k) end
			self:command_select(self.command_player, refs)
		end,
		-- ExecRightClick 0x472f10: a point inside the map, a packed target and a type.
		[M.RIGHT_CLICK] = function(self, bytes)
			local x, y = u16(bytes, 2), u16(bytes, 4)
			if x >= self.map_w_px or y >= self.map_h_px then return end
			right_click(self, x, y, self:unit_from_ref(u16(bytes, 6)), u16(bytes, 8), bytes:byte(10) ~= 0)
		end,
		-- ExecTargetedOrder 0x472270: a point inside the map, a packed target, a type, the order
		-- and the queued flag.
		[M.TARGETED_ORDER] = function(self, bytes)
			local x, y = u16(bytes, 2), u16(bytes, 4)
			if x >= self.map_w_px or y >= self.map_h_px then return end
			targeted_order(self, int.s16(x), int.s16(y), self:unit_from_ref(u16(bytes, 6)), u16(bytes, 8), bytes:byte(10),
				bytes:byte(11))
		end,
		[M.STOP] = function(self, bytes)
			order_selection(self, bytes:byte(2) ~= 0, function() return M.ORDER_STOP end)
		end,
		[M.HOLD_POSITION] = function(self, bytes)
			order_selection(self, bytes:byte(2) ~= 0, function(g, n)
				return M.HOLD_ORDERS[g.u.type[n]] or M.HOLD_POSITION_ORDER
			end)
		end,
	}

	-- One command of `player` (its bytes as a string); the player is the command player
	-- (0x508d44) and selection row (0x508d48) while it runs.
	function G:execute_command(player, bytes)
		local id = bytes:byte(1)
		-- CheckSyncCommand 0x47a510 compares the sync values of the players' games; a game
		-- run alone has none to compare, and the check changes no game state.
		if id == M.SYNC then return end
		local f = HANDLERS[id]
		if not f then
			error(("commands: command 0x%02x of player %d (0x%x)"):format(id, player, M.HANDLERS[id] or M.EXECUTOR), 0)
		end
		self.command_player = player
		f(self, bytes)
	end

	-- TurnManagerStep 0x476c00 in a game run alone: the turn's commands, then the turn count
	-- (0x672658) that stamps recent selections.
	function G:run_turn(commands)
		for _, c in ipairs(commands) do self:execute_command(c.player, c.bytes) end
		self.turn = self.turn + 1
	end
end

return M
