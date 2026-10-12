-- The creep engine (creep.cpp; atlas P42 "Creep engine", P39 receding creep and callbacks;
-- docs/research/native-trigger-creep.md). It rewrites MTXM tile words (creep is CV5 group 1:
-- `(word & 0x7ff0) == 0x10`), keeps the replaced word per tile (backup) and a border byte per
-- non-creep tile, and reports each change to the game's callbacks: the display megatile,
-- the tile flags (0x40000000 creep shown, walkability) and the receding-creep buckets.
-- Methods of the game.
local bytes = require("game.data.bytes")
local int = require("game.simulation.int")

local band, bor, bnot = int.band, int.bor, int.bnot

local M = {}

M.CREEP_GROUP = 0x10
M.SHOWN = 0x40000000
M.RECEDING = 0x10000000
M.OCCUPIED = 0x8000000
M.NO_CREEP, M.UNBUILDABLE = 0x20000000, 0x800000
M.MODE_ADD, M.MODE_REMOVE, M.MODE_BORDER = 0, 1, 2
M.WALK_BITS, M.WALKABLE, M.UNWALKABLE = 0x450000, 0x10000, 0x40000
M.CV5_RECORD = 52
M.RANDOM_SPREAD = 26
M.LARGE = { [132] = true, [133] = true, [144] = true, [146] = true } -- Lair, Hive, Spore and Sunken Colony
M.LARGE_COMPLETED = { [131] = true, [143] = true } -- Hatchery, Creep Colony
M.ELLIPSE = 0x9c4000
M.RECEDE_NODES, M.RECEDE_BUCKETS = 1024, 9
M.RECEDE_DELAY = { [0] = 1, 3, 5, 6, 7, 8, 9, 9, 9 } -- 0x50decc
-- Neighbour table 0x4f0fd8 (the tile itself last).
M.NEIGHBOURS = { { 1, 1 }, { 0, 1 }, { -1, 1 }, { 1, 0 }, { -1, 0 }, { 1, -1 }, { 0, -1 }, { -1, -1 }, { 0, 0 } }

-- BuildCreepShapeTable 0x4bff60 and BuildCreepShapeRanks 0x4c0010.
local function build_shapes()
	local shape, rank = {}, {}
	for u = 0, 511 do
		local b
		if u >= 256 then
			b = 0x7f
		else
			local function has(k) return band(u, 2 ^ k) ~= 0 end
			b = 0
			if has(4) then b = bor(b, 9) end
			if has(3) then b = bor(b, 0x24) end
			if has(6) then b = bor(b, 2) end
			if has(1) then b = bor(b, 0x10) end
			if has(6) and has(7) then b = bor(b, 1) end
			if has(5) and has(6) then b = bor(b, 4) end
			if has(1) and has(2) then b = bor(b, 8) end
			if has(0) and has(1) then b = bor(b, 0x20) end
			if band(b, 0x21) == 0x21 or band(b, 0xc) == 0xc then b = bor(b, 0x40) end
		end
		shape[u] = b
	end
	local present = {}
	for u = 0, 511 do present[shape[u]] = true end
	local counter = 0
	for v = 0, 127 do
		rank[v] = 0
		if present[v] then
			rank[v] = counter % 256
			counter = counter + 1
		end
	end
	return shape, rank
end
M.SHAPE, M.RANK = build_shapes()

function M.install(G)
	-- InitCreepEngine 0x4c0690 (the zeroed backup and border maps).
	function G:creep_init()
		self.creep_backup, self.creep_border = {}, {}
		for i = 0, self.map_w * self.map_h - 1 do self.creep_backup[i], self.creep_border[i] = 0, 0 end
		-- InitRecedingCreepPool 0x4b44a0: 1024 free nodes, empty buckets, both timers 0.
		local buckets = {}
		for b = 0, M.RECEDE_BUCKETS - 1 do buckets[b] = 0 end
		self.recede = { timer = 0, free = M.RECEDE_NODES, buckets = buckets, retry = 0 }
	end

	-- RecedeCreepTick 0x4b4010: one receding tile goes per period, the period shrinking with
	-- the number of receding tiles (0x50decc by free nodes / 128).
	function G:creep_recede_tick()
		local r = self.recede
		if r.timer ~= 0 then
			r.timer = r.timer - 1
			return
		end
		r.timer = M.RECEDE_DELAY[int.shr(r.free, 7)]
		-- RecedeOneCreepTile 0x4b4a40 picks a tile of the lowest non-empty bucket.
		for b = 0, M.RECEDE_BUCKETS - 1 do
			if r.buckets[b] ~= 0 then error("creep: receding creep tile (0x4b4a40)", 0) end
		end
	end

	local function cv5_megatile(self, word) return self:cv5_megatile(word) end

	local function is_creep_word(word) return band(word, 0x7ff0) == M.CREEP_GROUP end

	-- OnCreepTileChanged 0x4b4040 (callback 0).
	local function tile_changed(self, x, y, mode, word)
		local i = x + self.map_w * y
		if mode == M.MODE_BORDER then
			self.megatiles[i] = bor(self.megatiles[i], 0x8000)
			return
		end
		local f = self.tiles[i]
		if band(f, self.local_vision) == 0 then
			self.megatiles[i] = cv5_megatile(self, word)
			if mode == M.MODE_ADD then f = bor(f, M.SHOWN) else f = band(f, bnot(M.SHOWN)) end
		end
		local group = band(int.shr(word, 4), 0x7ff)
		local cv5_flags = bytes.u16(self.tileset_data.cv5, group * M.CV5_RECORD + 2)
		local mega = band(cv5_megatile(self, word), 0x7fff)
		local terrain = bor(int.shl(band(cv5_flags, bnot(5)), 16), self.tileset_data.mflags[mega])
		f = bor(band(f, bnot(M.WALK_BITS)), band(terrain, M.WALK_BITS))
		if band(f, M.WALKABLE) ~= 0 then f = band(f, bnot(M.UNWALKABLE)) end
		if band(f, M.WALK_BITS) == 0 then f = bor(f, M.UNWALKABLE) end
		self.tiles[i] = f
		self:receding_neighbours(x, y)
	end

	-- UpdateNeighborRecedingBuckets 0x4b4230: only receding tiles (none before the pool
	-- is used) have bucket nodes.
	function G:receding_neighbours(x, y)
		for _, d in ipairs({ { -1, -1 }, { 0, -1 }, { 1, -1 }, { -1, 0 }, { 1, 0 }, { -1, 1 }, { 0, 1 }, { 1, 1 } }) do
			local nx, ny = x + d[1], y + d[2]
			if nx >= 0 and nx < self.map_w and ny >= 0 and ny < self.map_h then
				local j = nx + self.map_w * ny
				if is_creep_word(self.raw_tiles[j]) and band(self.tiles[j], M.RECEDING) ~= 0 then
					error("creep: receding creep buckets (0x4b4230)", 0)
				end
			end
		end
	end

	-- ApplyCreepTileChange 0x4c0ca0 with the three mutators (0x4c0c40 add, 0x4c0bc0 remove,
	-- 0x4c0d90 border).
	function G:creep_apply(x, y, mode, arg)
		local i = x + self.map_w * y
		if mode == M.MODE_ADD then
			if self.creep_backup[i] == 0 then self.creep_backup[i] = self.raw_tiles[i] end
			local variant
			if self.crt:rand() % 100 < 4 then
				variant = self.crt:rand() % 7 + 6
			else
				variant = self.crt:rand() % 6
			end
			self.raw_tiles[i] = M.CREEP_GROUP + variant
		elseif mode == M.MODE_REMOVE then
			if self.creep_backup[i] == 0 then return end
			self.raw_tiles[i], self.creep_backup[i] = self.creep_backup[i], 0
		else
			self.creep_border[i] = M.RANK[M.SHAPE[band(arg, 0xff)]]
		end
		tile_changed(self, x, y, mode, self.raw_tiles[i])
	end

	-- TileShowsCreep 0x4b45f0 (callback 1) and IsTileHiddenFromViewer 0x4b4560 (callback 2).
	local function shows_creep(self, x, y) return band(self.tiles[x + self.map_w * y], M.SHOWN) ~= 0 end
	local function hidden_from_viewer(self, x, y) return band(self.tiles[x + self.map_w * y], self.local_vision) ~= 0 end

	-- ForEachCreepNeighbor 0x4c03f0 with RefreshCreepBorderAt 0x4c0470 (and the neighbour
	-- mask of 0x4c0e00): non-creep tiles around (x, y) get their border shape.
	function G:creep_refresh_borders(x, y)
		for _, d in ipairs(M.NEIGHBOURS) do
			local nx, ny = x + d[1], y + d[2]
			if nx >= 0 and nx < self.map_w and ny >= 0 and ny < self.map_h and not shows_creep(self, nx, ny) then
				local mask = 0
				for k, e in ipairs(M.NEIGHBOURS) do
					local mx, my = nx + e[1], ny + e[2]
					if k <= 8 and mx >= 0 and mx < self.map_w and my >= 0 and my < self.map_h
						and shows_creep(self, mx, my) then
						mask = bor(mask, 2 ^ (k - 1))
					end
				end
				self:creep_apply(nx, ny, M.MODE_BORDER, mask)
			end
		end
	end

	-- AddCreepAt: the add mutator and, for seen tiles, the borders around it.
	local function add_creep(self, x, y)
		self:creep_apply(x, y, M.MODE_ADD, 0)
		if not hidden_from_viewer(self, x, y) then self:creep_refresh_borders(x, y) end
	end

	-- TileCannotHaveCreep 0x4b45b0.
	function G:tile_cannot_have_creep(x, y)
		local f = self.tiles[x + self.map_w * y]
		if band(f, bor(M.NO_CREEP, M.UNBUILDABLE)) ~= 0 then return true end
		if (y + 1) % 65536 == self.map_h % 65536 then return false end
		return band(self.tiles[x + self.map_w * (y + 1)], M.UNBUILDABLE) ~= 0
	end

	-- GetCreepAreaBounds 0x4c04f0: tile bounds and whether the area is large.
	function G:creep_bounds(t, cx, cy, completed)
		local large = M.LARGE[t] or (M.LARGE_COMPLETED[t] and completed)
		local left, top, right, bottom
		if large then
			left, right = int.idiv(cx - 320, 32), int.idiv(cx + 320, 32)
			top, bottom = int.idiv(cy - 200, 32), int.idiv(cy + 200, 32)
		else
			local size = self.dat.units.staredit_placement_size
			local w, h = int.idiv(int.s16(size[t * 2]), 32), int.idiv(int.s16(size[t * 2 + 1]), 32)
			left = int.s16(int.idiv(cx, 32)) - int.idiv(w, 2)
			right = left + w - 1
			top = int.s16(int.idiv(cy, 32)) - int.idiv(h, 2)
			bottom = top + h - 1
		end
		if left < 0 then left = 0 elseif right >= self.map_w then right = self.map_w - 1 end
		if top < 0 then top = 0 elseif bottom >= self.map_h then bottom = self.map_h - 1 end
		return left, top, right, bottom, large
	end

	local function outside_ellipse(dx, dy)
		return int.s32(int.mul(int.mul(dx, dx), 100) + int.mul(int.mul(dy, dy), 256)) > M.ELLIPSE
	end

	-- CountCreepNeighbors 0x4c0060.
	local function creep_neighbours(self, x, y)
		local n = 0
		for k = 1, 8 do
			local d = M.NEIGHBOURS[k]
			local nx, ny = (x + d[1]) % 4294967296, (y + d[2]) % 4294967296
			if nx < self.map_w and ny < self.map_h and is_creep_word(self.raw_tiles[nx + self.map_w * ny]) then
				n = n + 1
			end
		end
		return n
	end

	-- SpreadCreepOnce 0x4c0e60: 1 when a tile was added, 2 when only occupied candidates
	-- were seen, else 0.
	function G:creep_spread_once(t, cx, cy)
		local left, top, right, bottom, large = self:creep_bounds(t, cx, cy, true)
		local buckets = {}
		for b = 1, 8 do buckets[b] = {} end
		local result = 0
		for y = top, bottom do
			for x = left, right do
				local i = x + self.map_w * y
				if not is_creep_word(self.raw_tiles[i]) and not self:tile_cannot_have_creep(x, y) then
					if band(self.tiles[i], M.OCCUPIED) ~= 0 then
						result = 2
					else
						local n = creep_neighbours(self, x, y)
						if n >= 1 and n <= 8 and not (large and outside_ellipse(32 * x - cx + 16, 32 * y - cy + 16)) then
							local bucket = buckets[n]
							bucket[#bucket + 1] = { x, y }
						end
					end
				end
			end
		end
		for b = 8, 1, -1 do
			local bucket = buckets[b]
			if #bucket > 0 then
				local r = self.rng:raw(M.RANDOM_SPREAD)
				local pick = bucket[int.urem(int.shr(r, 4), #bucket) + 1]
				add_creep(self, pick[1], pick[2])
				return 1
			end
		end
		return result
	end

	-- PlaceInitialCreep 0x4c0aa0.
	function G:creep_place_initial(t, cx, cy)
		local size = self.dat.units.staredit_placement_size
		local w, h = int.idiv(int.s16(size[t * 2]), 32), int.idiv(int.s16(size[t * 2 + 1]), 32)
		local left = int.idiv(cx, 32) - int.idiv(w, 2)
		local top = int.idiv(cy, 32) - int.idiv(h, 2)
		local x1, y1 = (left + w) % 4294967296, (top + h) % 4294967296
		local y = top % 4294967296
		while y < y1 do
			local x = left % 4294967296
			while x < x1 do
				if not self:tile_cannot_have_creep(x, y) then add_creep(self, x, y) end
				x = x + 1
			end
			y = y + 1
		end
		repeat until self:creep_spread_once(t, cx, cy) ~= 1
	end
end

return M
