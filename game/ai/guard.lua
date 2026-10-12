-- Guard AI records (atlas P39: InitGuardAi 0x4b2640 over 0x4b1a60, AttachGuardRecord 0x4b2010).
-- One block of 1000 records of 32 bytes (0x6b6908) serves every player: a free list from
-- record 0 upwards (head at block + 32000) and one list per player (0x6be614 + 8 p) of the
-- records that guard a post: { kind (+8), unit (+0xc), type (+0x10), post (+0x12/+0x14 and
-- +0x16/+0x18) }. A unit's AI pointer (+0x134) is record k + 1 here (0 = none). The guard
-- behaviour itself belongs to the AI phase. Methods of the game.
local int = require("game.simulation.int")

local M = {}

M.RECORDS = 1000
M.NONE = -1
M.KIND_GUARD = 1
M.CONTROLLER_COMPUTER = 1
M.AI_NO_GUARD = 0x2 -- units.dat ai_internal flag

function M.install(G)
	-- 0x4b1a60 on the shared block, then empty player lists.
	function G:guard_init()
		local g = { next = {}, prev = {}, kind = {}, unit = {}, type = {}, x1 = {}, y1 = {}, x2 = {}, y2 = {},
			flag9 = {}, heads = {} }
		for k = 0, M.RECORDS - 1 do
			g.next[k] = k + 1 < M.RECORDS and k + 1 or M.NONE
			g.prev[k] = k > 0 and k - 1 or M.NONE
			g.kind[k], g.unit[k], g.type[k], g.flag9[k] = 0, 0, 0, 0
			g.x1[k], g.y1[k], g.x2[k], g.y2[k] = 0, 0, 0, 0
		end
		g.free = 0
		for p = 0, 7 do g.heads[p] = M.NONE end
		self.guards = g
	end

	-- AttachGuardRecord 0x4b2010: a computer player's unit takes the matching unattached
	-- record of its type and post, else a new record from the free list.
	function G:guard_attach(n)
		local u, g = self.u, self.guards
		local p = u.owner[n]
		if self.players[p].controller ~= M.CONTROLLER_COMPUTER then return end
		local t = u.type[n]
		if int.band(self.dat.units.ai_internal[t], M.AI_NO_GUARD) ~= 0 then return end
		local s = u.sprite[n]
		local x, y = int.s16(self.img.spr.x[s]), int.s16(self.img.spr.y[s])
		local k = g.heads[p]
		while k ~= M.NONE do
			if g.unit[k] == 0 and g.type[k] == t and g.x2[k] == x and g.y2[k] == y then break end
			k = g.next[k]
		end
		if k == M.NONE then
			k = g.free
			if k == M.NONE then return end
			g.free = g.next[k]
			if g.free ~= M.NONE then g.prev[g.free] = M.NONE end
			g.prev[k] = M.NONE
			g.next[k] = g.heads[p]
			if g.heads[p] ~= M.NONE then g.prev[g.heads[p]] = k end
			g.heads[p] = k
			g.kind[k] = M.KIND_GUARD
			g.type[k] = t
		end
		g.unit[k] = n
		g.x1[k], g.y1[k], g.x2[k], g.y2[k] = x, y, x, y
		g.flag9[k] = 0
		u.ai[n] = k + 1
	end
end

return M
