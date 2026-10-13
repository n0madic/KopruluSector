-- Order node pool and unit order queues (docs/research/native-primary-orders.md; atlas P05
-- order pool, P09 queue). 2000 nodes of { prev, next, order, FoW unit type, x, y, target },
-- records 0..1999; free list 0x5aef00/0x5aef04, allocated count 0x5aef08. A unit's queue is
-- +0x74/+0x78 (node or -1); +0x84 counts the highlighted orders. Methods of the game.
local core = require("game.images.core")
local int = require("game.simulation.int")

local band, bor = int.band, int.bor
local bit_band = int.bit_band
local NONE = core.NONE
local unlink, insert_after, insert_after_head, insert_before = core.unlink, core.insert_after,
	core.insert_after_head, core.insert_before

local M = {}

M.NODES = 2000
M.NO_TYPE = 228
M.NO_HIGHLIGHT = 0xffff
M.DEATH, M.UNBURROW, M.BURROWED, M.COMPUTER_AI = 0, 118, 117, 156
M.SPIDER_MINE, M.LURKER = 13, 103
M.GUARD, M.ATTACK_FIXED_RANGE = 2, 11
M.STATUS_BURROWED, M.STATUS_ORDER_LOCK = 0x10, 0x1000
M.STATUS_LOCKS = 0x3000
M.STATUS_ACTIVATION_CLEAR = 0xf7ffeff7
M.SUBUNIT = 0x10
M.CLOAK = 109
M.STRING_QUEUE_FULL = 0x368 -- stat_txt index reported when the pool is empty

function M.install(G)
	-- InitOrderPool 0x413db0: node 0 heads the free list, then 1999, 1998, ..., 1.
	function G:orders_init()
		local o = { prev = {}, next = {}, order = {}, type = {}, x = {}, y = {}, unit = {} }
		for i = 0, M.NODES - 1 do
			o.prev[i], o.next[i], o.order[i], o.type[i], o.x[i], o.y[i], o.unit[i] = NONE, NONE, 0, 0, 0, 0, 0
		end
		o.free = core.list()
		for i = 0, M.NODES - 1 do insert_after_head(o.prev, o.next, o.free, i) end
		o.allocated = 0
		self.orders = o
	end

	-- AllocateOrderNode 0x413c80: the free head with its payload written, or NONE.
	local function allocate(self, order, fow, x, y, target)
		local o = self.orders
		local i = o.free.head
		if i == NONE then return NONE end
		o.order[i], o.type[i], o.x[i], o.y[i], o.unit[i] = order % 256, fow % 65536, int.s16(x), int.s16(y), target
		unlink(o.prev, o.next, o.free, i)
		o.allocated = o.allocated + 1
		return i
	end

	-- FreeOrderNode 0x413d50: after the free head; the payload stays.
	local function free(self, i)
		local o = self.orders
		o.allocated = o.allocated - 1
		insert_after_head(o.prev, o.next, o.free, i)
	end

	local function queue(self, n)
		local u = self.u
		return { head = u.queue_head[n], tail = u.queue_tail[n] }
	end

	local function store(self, n, q)
		self.u.queue_head[n], self.u.queue_tail[n] = q.head, q.tail
	end

	local function highlighted(self, order)
		return self.dat.orders.highlight_icon[order] ~= M.NO_HIGHLIGHT
	end

	-- The decrement of the highlighted count with its 0xff -> 0 fix-up.
	local function unhighlight(self, n, order)
		local u = self.u
		if highlighted(self, order) then u.queue_count[n] = (u.queue_count[n] - 1) % 256 end
		if u.queue_count[n] == 0xff then u.queue_count[n] = 0 end
	end

	local function allocation_failed(self, n)
		self:report_error(self.u.owner[n], M.STRING_QUEUE_FULL)
	end

	-- QueueOrder 0x4295d0 (the kinds of QueueOrderByKind 0x429650): a node behind the queue
	-- without pruning or activation; an empty pool reports "queue full".
	function G:order_queue(n, order, x, y, target, fow)
		local u, o = self.u, self.orders
		local i = allocate(self, order, fow, x, y, target)
		if i == NONE then
			allocation_failed(self, n)
			return
		end
		if highlighted(self, order) then u.queue_count[n] = (u.queue_count[n] + 1) % 256 end
		local q = queue(self, n)
		if q.tail ~= NONE then insert_after(o.prev, o.next, q, q.tail, i) else insert_after_head(o.prev, o.next, q, i) end
		store(self, n, q)
	end

	-- AppendOrderWithPruning 0x429f60: unless the current order is Death, removes tail nodes
	-- that are interruptible (clear_others) or the same order, then appends the new node.
	-- Cloak 109 instead keeps the current order ahead of it.
	function G:order_append(n, order, x, y, target, fow, clear_others)
		local u, o = self.u, self.orders
		if u.order[n] == M.DEATH then return end
		local q = queue(self, n)
		local interruptible = self.dat.orders.interruptable
		while q.tail ~= NONE and ((clear_others and interruptible[o.order[q.tail]] ~= 0) or o.order[q.tail] == order) do
			local tail = q.tail
			unhighlight(self, n, o.order[tail])
			unlink(o.prev, o.next, q, tail)
			free(self, tail)
		end
		store(self, n, q)
		if order == M.CLOAK then
			if q.head == NONE then
				self:order_insert_after(n, u.order[n], u.order_x[n], u.order_y[n], u.order_unit[n], u.order_type[n], NONE)
			else
				self:order_insert_before(n, u.order[n], u.order_x[n], u.order_y[n], u.order_unit[n], u.order_type[n],
					q.head)
			end
			self:order_insert_before(n, M.CLOAK, x, y, target, M.NO_TYPE, u.queue_head[n])
			u.status[n] = band(u.status[n], int.bnot(M.STATUS_ORDER_LOCK))
			self:order_done(n)
			return
		end
		local i = allocate(self, order, fow, x, y, target)
		if i == NONE then
			allocation_failed(self, n)
			return
		end
		if highlighted(self, order) then u.queue_count[n] = (u.queue_count[n] + 1) % 256 end
		q = queue(self, n)
		if q.tail ~= NONE then insert_after(o.prev, o.next, q, q.tail, i) else insert_after_head(o.prev, o.next, q, i) end
		store(self, n, q)
	end

	-- InsertOrderAfter 0x4295d0: after `anchor`, or at the tail for NONE.
	function G:order_insert_after(n, order, x, y, target, fow, anchor)
		local u, o = self.u, self.orders
		local i = allocate(self, order, fow, x, y, target)
		if i == NONE then
			allocation_failed(self, n)
			return
		end
		if highlighted(self, order) then u.queue_count[n] = (u.queue_count[n] + 1) % 256 end
		local q = queue(self, n)
		if anchor == NONE and q.tail == NONE then
			insert_after_head(o.prev, o.next, q, i)
		else
			insert_after(o.prev, o.next, q, anchor == NONE and q.tail or anchor, i)
		end
		store(self, n, q)
	end

	-- InsertOrderBefore 0x429e10: refused for an AI unit with more than 8 highlighted orders.
	function G:order_insert_before(n, order, x, y, target, fow, anchor)
		local u, o = self.u, self.orders
		if u.ai[n] ~= 0 and u.queue_count[n] >= 9 then return end
		local i = allocate(self, order, fow, x, y, target)
		if i == NONE then
			allocation_failed(self, n)
			return
		end
		if highlighted(self, order) then u.queue_count[n] = (u.queue_count[n] + 1) % 256 end
		local q = queue(self, n)
		insert_before(o.prev, o.next, q, anchor, i)
		store(self, n, q)
	end

	-- RemoveOrder 0x429d20: one node out of the queue.
	function G:order_remove(n, node)
		local o = self.orders
		unhighlight(self, n, o.order[node])
		local q = queue(self, n)
		unlink(o.prev, o.next, q, node)
		store(self, n, q)
		free(self, node)
	end

	-- ClearOrderQueue 0x429ac0.
	function G:order_clear_queue(n)
		local o = self.orders
		local q = queue(self, n)
		while q.head ~= NONE do
			local head = q.head
			unhighlight(self, n, o.order[head])
			unlink(o.prev, o.next, q, head)
			free(self, head)
		end
		store(self, n, q)
	end

	-- SetOrderNoTarget 0x4179b0, SetOrderAtPoint 0x417a10, SetOrderFowAtPoint 0x4179e0 and
	-- SetOrderTarget 0x417a50.
	function G:set_order(n, order)
		local u = self.u
		u.order[n], u.order_x[n], u.order_y[n], u.order_state[n], u.order_unit[n] = order, 0, 0, 0, 0
		u.order_type[n] = M.NO_TYPE
	end

	function G:set_order_at(n, order, x, y)
		local u = self.u
		u.order[n], u.order_x[n], u.order_y[n], u.order_unit[n], u.order_state[n] = order, x, y, 0, 0
		u.order_type[n] = M.NO_TYPE
	end

	function G:set_order_fow(n, order, fow, x, y)
		local u = self.u
		u.order[n], u.order_type[n], u.order_x[n], u.order_unit[n], u.order_y[n], u.order_state[n] =
			order, fow, x, 0, y, 0
	end

	function G:set_order_target(n, order, target)
		local u = self.u
		u.order[n], u.order_unit[n] = order, target
		if target ~= 0 then
			local s = u.sprite[target]
			u.order_x[n], u.order_y[n] = int.s16(self.img.spr.x[s]), int.s16(self.img.spr.y[s])
		else
			u.order_x[n], u.order_y[n] = 0, 0
		end
		u.order_state[n], u.order_type[n] = 0, M.NO_TYPE
	end

	-- SetSecondaryOrder 0x42a1e0.
	function G:set_secondary_order(n, order)
		local u = self.u
		if u.secondary_order[n] ~= order then
			u.secondary_order[n] = order
			u.secondary_x[n], u.secondary_y[n], u.build_unit[n], u.secondary_state[n] = 0, 0, 0, 0
		end
	end

	-- ActivateNextOrder 0x42a610.
	function G:order_activate(n)
		local u, o, d = self.u, self.orders, self.dat
		local head = u.queue_head[n]
		if u.ai[n] ~= 0 and head ~= NONE and bit_band(u.status[n], M.STATUS_BURROWED) ~= 0 then
			local order, t = o.order[head], u.type[n]
			if order ~= M.DEATH and order ~= M.UNBURROW and order ~= M.BURROWED and order ~= M.COMPUTER_AI
				and t ~= M.SPIDER_MINE and (t ~= M.LURKER or (order ~= M.GUARD and order ~= M.ATTACK_FIXED_RANGE)) then
				local s = u.sprite[n]
				self:order_insert_before(n, M.UNBURROW, int.s16(self.img.spr.x[s]), int.s16(self.img.spr.y[s]), 0,
					M.NO_TYPE, head)
				u.status[n] = band(u.status[n], int.bnot(M.STATUS_ORDER_LOCK))
				self:order_done(n)
				return
			end
		end
		if head == NONE then return end
		if bit_band(u.status[n], M.STATUS_LOCKS) ~= 0 and o.order[head] ~= M.DEATH then return end
		unhighlight(self, n, o.order[head])
		local q = queue(self, n)
		unlink(o.prev, o.next, q, head)
		store(self, n, q)
		free(self, head)
		local order, fow, x, y, target = o.order[head], o.type[head], o.x[head], o.y[head], o.unit[head]
		u.queue_timer[n] = 0
		u.user_action[n] = band(u.user_action[n], 0xfe)
		u.recent_order_timer[n] = 0
		local status = band(u.status[n], M.STATUS_ACTIVATION_CLEAR)
		if d.orders.interruptable[order] == 0 then status = bor(status, M.STATUS_ORDER_LOCK) end
		u.status[n] = status
		if target == 0 then
			if fow == M.NO_TYPE then self:set_order_at(n, order, x, y) else self:set_order_fow(n, order, fow, x, y) end
		else
			self:set_order_target(n, order, target)
		end
		if u.ai[n] == 0 then u.auto_target[n] = 0 end
		self:reset_to_idle_animation(n)
		local t, sub = u.type[n], u.subunit[n]
		local flags = d.units.special_ability_flags
		if bit_band(flags[t], M.SUBUNIT) ~= 0 or sub == 0 or bit_band(flags[u.type[sub]], M.SUBUNIT) == 0 then return end
		local st = u.type[sub]
		local units = d.units
		if order == units.return_to_idle[t] then
			order = units.return_to_idle[st]
		elseif order == units.attack_unit[t] then
			order = units.attack_unit[st]
		elseif order == units.attack_move[t] then
			order = units.attack_move[st]
		elseif d.orders.changes_subunit_order[order] == 0 then
			return
		end
		if target ~= 0 then
			self:issue_order_packed(sub, order, x, y, target)
		else
			self:issue_order_fow(sub, order, fow, x, y)
		end
	end

	-- EndOrder 0x42a5a0: the next queued order, else the idle order (ComputerAI with an AI).
	function G:order_done(n)
		local u = self.u
		if u.queue_head[n] ~= NONE then
			u.user_action[n] = bor(u.user_action[n], 1)
			self:order_activate(n)
			return
		end
		local order = u.ai[n] == 0 and self.dat.units.return_to_idle[u.type[n]] or M.COMPUTER_AI
		u.user_action[n] = bor(u.user_action[n], 1)
		self:order_append(n, order, 0, 0, 0, M.NO_TYPE, true)
		self:order_activate(n)
	end

	-- KillUnit 0x42a4d0: the queue is replaced by Death at the current order point.
	function G:unit_kill(n)
		local u = self.u
		if u.union_0xc0[n] ~= 0 and bit_band(u.union_units[n], 1) ~= 0 and bit_band(self.dat.units.special_ability_flags[u.type[n]], 8) ~= 0 then
			error("orders: killing a unit that carries a powerup (0x4272c0)", 0)
		end
		self:order_clear_queue(n)
		u.user_action[n] = bor(u.user_action[n], 1)
		self:order_append(n, M.DEATH, u.order_x[n], u.order_y[n], 0, M.NO_TYPE, true)
		self:order_activate(n)
		if u.ai[n] ~= 0 then error("orders: AI removal of a killed unit (0x48dbc0)", 0) end
	end

	-- The issuers 0x42a2c0 (no target), 0x42a340 (point), 0x42a300 (FoW type and point),
	-- 0x42a380 (unit, at its sprite position) and 0x42a3e0 (packed point and unit).
	function G:issue_order(n, order)
		self.u.user_action[n] = bor(self.u.user_action[n], 1)
		self:order_append(n, order, 0, 0, 0, M.NO_TYPE, true)
		self:order_activate(n)
	end

	function G:issue_order_at(n, order, x, y)
		self.u.user_action[n] = bor(self.u.user_action[n], 1)
		self:order_append(n, order, x, y, 0, M.NO_TYPE, true)
		self:order_activate(n)
	end

	function G:issue_order_fow(n, order, fow, x, y)
		self.u.user_action[n] = bor(self.u.user_action[n], 1)
		self:order_append(n, order, x, y, 0, fow, true)
		self:order_activate(n)
	end

	function G:issue_order_target(n, order, target)
		local x, y = 0, 0
		if target ~= 0 then
			local s = self.u.sprite[target]
			x, y = self.img.spr.x[s], self.img.spr.y[s]
		end
		self.u.user_action[n] = bor(self.u.user_action[n], 1)
		self:order_append(n, order, x, y, target, M.NO_TYPE, true)
		self:order_activate(n)
	end

	function G:issue_order_packed(n, order, x, y, target)
		self.u.user_action[n] = bor(self.u.user_action[n], 1)
		self:order_append(n, order, x, y, target, M.NO_TYPE, true)
		self:order_activate(n)
	end
end

return M
