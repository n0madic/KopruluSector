-- The region graph at run time (SAI regions; atlas P30, docs/research/native-region-paths.md)
-- and the long path search over it (SearchRegionPath 0x491300). The regions come from the
-- mission's RGNS section (docs/formats/kpm.md) as native-shaped arrays; the search keeps the
-- original's node arena, heap and tie rules. Methods of the game.
local bytes = require("game.data.bytes")
local heap = require("game.pathing.heap")
local int = require("game.simulation.int")
local mission_data = require("game.data.mission")

local sar, s16 = int.sar, int.s16

local M = {}

M.UNWALKABLE = 0x1ffd
M.SPLIT = 0x2000
M.MAX_REGIONS = 50 -- region ids a search keeps (+0x20 of the search context)
M.ARENA = 349 -- nodes after the initial one (the 24-byte stack arena of 0x491300)
M.OPEN_LIMIT = 0x7d -- the search stops with 125 open nodes
M.START_COST = 0x400000 -- the initial node's heuristic and priority (constant 0x4efa00)

function M.install(G)
	-- The region arrays of the mission's RGNS section: class (+0), group (+2), ground
	-- neighbour count (+6), neighbour count (+7), neighbours (+0xc), fixed8 centre (+0x10),
	-- bounds (+0x18) and the run-time slot +8: the region search's node back-reference (node)
	-- and the local search's neighbour-region counter (mark).
	function G:regions_init()
		local r = self.regions
		local s = r.data
		local rg = { count = r.count, class = {}, group = {}, ground = {}, neighbors = {}, center_x = {}, center_y = {},
			left = {}, top = {}, right = {}, bottom = {}, node = {}, mark = {} }
		for i = 0, r.count - 1 do
			local a = r.regions_at + i * mission_data.REGION_SIZE
			rg.class[i], rg.group[i] = bytes.u16(s, a), bytes.u16(s, a + 2)
			rg.ground[i] = bytes.u8(s, a + 6)
			local count, offset = bytes.u8(s, a + 7), bytes.u32(s, a + 12)
			local list = {}
			for k = 0, count - 1 do list[k] = bytes.u16(s, r.pool_at + 2 * (offset + k)) end
			list.count = count
			rg.neighbors[i] = list
			rg.center_x[i], rg.center_y[i] = bytes.s32(s, a + 16), bytes.s32(s, a + 20)
			rg.left[i], rg.top[i] = bytes.s16(s, a + 24), bytes.s16(s, a + 26)
			rg.right[i], rg.bottom[i] = bytes.s16(s, a + 28), bytes.s16(s, a + 30)
		end
		self.rg = rg
	end

	-- GetRegionGroup 0x481c90.
	function G:region_group_of(id) return self.rg.group[id] end

	-- SameRegionGroup 0x481cb0: the regions under the two units' sprites are in one group.
	function G:same_ground_group(a, b)
		local spr, u = self.img.spr, self.u
		local sa, sb = u.sprite[a], u.sprite[b]
		local ga = self.rg.group[self:region_at(s16(spr.x[sb]), s16(spr.y[sb]))]
		return self.rg.group[self:region_at(s16(spr.x[sa]), s16(spr.y[sa]))] == ga
	end

	-- GetRegionGroundNeighbors 0x481ab0: the ground prefix of the neighbour list.
	function G:region_ground_neighbors(id)
		local list, out = self.rg.neighbors[id], {}
		for k = 0, self.rg.ground[id] - 1 do out[#out + 1] = list[k] end
		return out
	end

	-- GetRegionBox 0x481a00: left, top, right, bottom.
	function G:region_box(id)
		local rg = self.rg
		return rg.left[id], rg.top[id], rg.right[id], rg.bottom[id]
	end

	-- IsRegionIdInaccessible 0x481c10: an unwalkable region (split ids are not).
	function G:region_inaccessible(id)
		return id < M.SPLIT and self.rg.class[id] == M.UNWALKABLE
	end

	-- FindReachableSubstituteRegion 0x48e690: the last region of the route from (sx, sy)
	-- towards (dx, dy), or the destination's region.
	function G:reachable_substitute_region(sx, sy, dx, dy)
		local c = { src_x = sx, src_y = sy, dest_x = dx, dest_y = dy, goal_x = dx, goal_y = dy, regions = {},
			src_region = self:region_at(sx, sy), dst_region = self:region_at(dx, dy) }
		if c.src_region == c.dst_region then return c.dst_region end
		if not self:search_region_path(c) then return c.dst_region end
		return c.regions[c.region_count - 1]
	end

	-- GetTileRegionFirst 0x481c40: the region of the tile at pixel (x, y), the walkable
	-- region of a split tile.
	function G:tile_region_first(x, y)
		local r = self.regions
		local v = self.region_cell(r, sar(x, 5), sar(y, 5))
		if v < M.SPLIT then return v end
		return bytes.u16(r.data, r.splits_at + (v - M.SPLIT) * 6 + 2)
	end

	local function approx(self, x1, y1, x2, y2) return self:approx_distance(x1, y1, x2, y2) end

	-- The cost of entering a region outside the source group doubles (the original's
	-- wrapping shift and signed truncation).
	local function scaled(cost) return int.idiv(int.s32(int.shl(cost, 9)), 256) end

	-- SearchRegionPath 0x491300 on search context c (route.lua): the region ids from the
	-- source region (c.src_region) towards the destination (c.dst_region) at c.regions[0..],
	-- with c.region_count, c.full_count, c.region_cursor = -1, the partial byte c.partial and
	-- the counters c.long_nodes and c.long_peak. False when the ids are equal (the context is
	-- left as it was) or no region was stored.
	function G:search_region_path(c)
		local rg = self.rg
		local src, target = c.src_region, c.dst_region
		if src == target then return false end
		c.partial = 0
		local restarted = false
		local sx, sy = int.shl(c.src_x, 8), int.shl(c.src_y, 8)
		local dx, dy = int.shl(c.dest_x, 8), int.shl(c.dest_y, 8)
		local src_group = rg.group[src]
		local best, forward, used, arena
		while true do
			c.long_nodes, c.long_peak = 0, 0
			forward = src_group == rg.group[target] or restarted
			local start, goal, gx, gy
			if forward then
				start, goal, gx, gy = src, target, dx, dy
			else
				start, goal, gx, gy = target, src, sx, sy
			end
			-- The node position of a region: the source or destination point for those
			-- regions, else the region's centre.
			local function position(id)
				if id == src then return sx, sy end
				if id == target then return dx, dy end
				return rg.center_x[id], rg.center_y[id]
			end
			local open = heap.new()
			local first = { closed = 0, region = start, parent = nil, heur = M.START_COST, cost = 0, prio = M.START_COST }
			arena = { [0] = first }
			used = 1
			rg.node[start] = first
			heap.insert(open, first)
			local found
			local exhausted = open.count == 0
			while not exhausted do
				local cur = heap.pop(open)
				if cur.region == goal then
					found = cur
					break
				end
				cur.closed = 1
				local id = cur.region
				local list = rg.neighbors[id]
				local count = rg.group[id] == src_group and rg.ground[id] or list.count
				local cx, cy = position(id)
				for k = 0, count - 1 do
					local nb = list[k]
					local node = rg.node[nb]
					if node == nil then
						node = { region = nb, parent = cur }
						arena[used] = node
						used = used + 1
						rg.node[nb] = node
						local px, py = position(nb)
						local cost = approx(self, cx, cy, px, py)
						if rg.group[nb] ~= src_group then cost = scaled(cost) end
						node.heur = approx(self, px, py, gx, gy)
						node.closed = 0
						node.cost = int.s32(cur.cost + cost)
						node.prio = int.s32(node.heur + node.cost)
						heap.insert(open, node)
						if open.count == M.OPEN_LIMIT or used == M.ARENA + 1 then break end
					elseif cur.parent ~= node then
						local px, py = position(node.region)
						local cost = approx(self, px, py, cx, cy)
						if rg.group[node.region] ~= src_group then cost = scaled(cost) end
						local new = int.s32(cur.cost + cost)
						if new < node.cost then
							node.parent = cur
							node.cost = new
							local prio = int.s32(node.heur + new)
							if node.closed == 0 then
								heap.change(open, node, prio)
							else
								node.prio = prio
								node.closed = 0
								heap.insert(open, node)
							end
							if open.count == M.OPEN_LIMIT then break end
						end
					end
				end
				if c.long_peak < open.count then c.long_peak = open.count end
				if open.count == M.OPEN_LIMIT or used == M.ARENA + 1 then
					-- Out of capacity: the best open node is the result.
					if open.count ~= 0 then found = heap.pop(open) end
					break
				end
				exhausted = open.count == 0
			end
			best = found
			if best == nil then
				-- No route: the visited node nearest the goal (allocation order on ties); the
				-- initial node's distance is measured from its region's centre.
				c.partial = 1
				best = first
				local d = approx(self, rg.center_x[start], rg.center_y[start], gx, gy)
				first.heur = d
				for k = 1, used - 1 do
					if arena[k].heur < d then best, d = arena[k], arena[k].heur end
				end
			end
			if forward or best.region == goal then break end
			-- The reverse search did not reach the source: search forward towards the region
			-- it reached (the partial byte stays).
			for k = 0, used - 1 do rg.node[arena[k].region] = nil end
			target = best.region
			restarted = true
		end
		c.long_nodes = used
		local n = 0
		if forward then
			local node = best
			while node do
				n = n + 1
				node = node.parent
			end
			local last = n - 1
			if last >= M.MAX_REGIONS then
				for _ = 1, last - (M.MAX_REGIONS - 1) do best = best.parent end
				last = M.MAX_REGIONS - 1
			end
			local node2 = best
			for k = last, 0, -1 do
				c.regions[k] = node2.region
				node2 = node2.parent
			end
		else
			local node = best
			while node and rg.group[node.region] == src_group do
				if n < M.MAX_REGIONS then c.regions[n] = node.region end
				node = node.parent
				n = n + 1
			end
		end
		c.full_count = n % 256
		c.region_count = math.min(n, M.MAX_REGIONS)
		c.region_cursor = -1
		for k = 0, used - 1 do rg.node[arena[k].region] = nil end
		return n > 0
	end
end

return M
