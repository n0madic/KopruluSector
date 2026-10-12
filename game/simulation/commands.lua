-- Game commands of the turn (ExecuteTurnCommands 0x475d30; atlas P16): the command byte
-- selects a handler. Ported commands run here; every other command raises with its native
-- handler. Methods of the game.
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

M.SELECT, M.STOP, M.HOLD_POSITION = 0x09, 0x1a, 0x2b
M.ORDER_STOP = 1
-- Current orders that a queued command replaces instead of queueing behind (0x42a8b0).
M.IDLE_ORDERS = { [2] = true, [3] = true, [0x17] = true, [0x5d] = true, [0x98] = true, [0xaf] = true }
-- GetHoldPositionOrder 0x41fae0 by unit type (else HoldPosition 0x6b).
M.HOLD_ORDERS = { [0x48] = 0x39, [0x52] = 0x39, [0x53] = 0x3e, [0x51] = 0x3e, [0x2d] = 0x6c, [0x31] = 0x6c,
	[0x2f] = 0x88, [0x32] = 0x88, [0x22] = 0xb2 }
M.HOLD_POSITION_ORDER = 0x6b

local function u16(s, at) return s:byte(at) + s:byte(at + 1) * 256 end

function M.install(G)
	-- IssueCommandOnUnit 0x42a880 / IssueCommand 0x42a8b0 for an order without a target
	-- point: queued (shift) behind a busy unit's queue, else issued at once (0x42a380).
	local function issue_command(self, n, queued, order)
		if queued and self.dat.orders.queueable[order] ~= 0 and not M.IDLE_ORDERS[self.u.order[n]] then
			error("commands: queued order (0x429650)", 0)
		end
		self:issue_order_target(n, order, 0)
	end

	-- The commands that give every selected unit one order (Stop 0x473440, HoldPosition
	-- 0x4727c0): units that may not take it are skipped (their error message is a no-op).
	local function order_selection(self, queued, order_of)
		for _, n in ipairs(self:selected_units(self.command_player)) do
			local order = order_of(self, n)
			if self:can_issue_order(order, self.command_player, n) == 1 then issue_command(self, n, queued, order) end
		end
	end

	local HANDLERS = {
		[M.SELECT] = function(self, bytes)
			local refs = {}
			for k = 1, bytes:byte(2) do refs[k] = u16(bytes, 1 + 2 * k) end
			self:command_select(self.command_player, refs)
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
