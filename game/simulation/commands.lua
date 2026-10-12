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

M.SELECT, M.RIGHT_CLICK, M.STOP, M.HOLD_POSITION = 0x09, 0x14, 0x1a, 0x2b
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

local function u16(s, at) return s:byte(at) + s:byte(at + 1) * 256 end

function M.install(G)
	-- IssueCommand 0x42a8b0: a queued (shift) order goes behind a busy unit's queue, else it
	-- is issued at once by IssueOrderByKind 0x42a410 (`issue`).
	local function issue_command(self, n, queued, order, issue)
		if queued and self.dat.orders.queueable[order] ~= 0 and not M.IDLE_ORDERS[self.u.order[n]] then
			error("commands: queued order (0x429650)", 0)
		end
		issue()
	end

	-- IssueCommandOnUnit 0x42a880 without a target unit (kind 1, 0x42a380).
	local function issue_command_on_unit(self, n, queued, order)
		issue_command(self, n, queued, order, function() self:issue_order_target(n, order, 0) end)
	end

	-- IssueCommandAtPoint 0x42a850 (kind 2): the order at a point with a fog-of-war type.
	local function issue_command_at_point(self, n, queued, order, fow, x, y)
		issue_command(self, n, queued, order, function() self:issue_order_fow(n, order, fow, x, y) end)
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
			error("commands: right click on a unit (0x421620, 0x4290e0)", 0)
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
				if order == 8 then error("commands: right click attack of a carrier or reaver (0x42a880)", 0) end
				if self:can_issue_order(order, self.command_player, n) == 1 then
					if t ~= 0 then error("commands: right click order on a unit (0x42a880)", 0) end
					local px, py = x, y
					if fow == M.NO_TYPE then px, py = self:formation_point(n, f) end
					issue_command_at_point(self, n, queued, order, fow, px, py)
					if self.second_order ~= M.NO_ORDER then
						issue_command_at_point(self, n, queued, self.second_order, fow, px, py)
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
