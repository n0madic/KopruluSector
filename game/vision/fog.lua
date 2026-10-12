-- Tile flags and line of sight (los.cpp, mask.cpp; docs/research/native-unit-creation.md
-- "Original vision templates and the air-mask defect"). Methods of the game.
--
-- G.tiles[ty * map_width + tx] is the tile flag DWORD of 0x666454: byte 0 = players that do
-- not see the tile, byte 1 = players that have not explored it, high bits from the terrain
-- (0x00010000 walkable, 0x00400000 high tile bit, 0x01000000 view blocker, 0x02000000 mid
-- and 0x04000000 high ground, 0x08000000 building occupancy, 0x20000000 partly walkable).
-- The native array always holds 256 x 256 entries.
local int = require("game.simulation.int")

local band, bor, bnot = int.band, int.bor, int.bnot

local M = {}

M.TILES = 0x10000
M.SIGHTS = 12
M.INNER = 3 -- the unconditional inner square of every template
M.HEIGHT_REQUIRED = { [0] = 0x07000000, [1] = 0x05000000, [2] = 0x01000000 } -- by source height
M.CARGO_VISION = 0x08 -- units.dat flag: vision through the transport (0x4273c0)
M.NUKE = 14
M.AIR = 0x4
M.ALL_PLAYERS = 0xff
M.VISION_CHEATS = 0x4001 -- 0x5432fc bits that keep every tile seen
M.DIRECTIONS = { { 1, 0 }, { 0, 1 }, { -1, 0 }, { 0, -1 } } -- 0x4ee644/0x4ee648

-- GenerateVisionGrid 0x466990: the cells of a w x h grid inside the sight shape (the inner
-- rectangle and the integer circle of radius w / 2). Returns marks[cell] and their count.
local function vision_grid(w, h, iw, ih)
	local marks = {}
	for k = 0, w * h - 1 do marks[k] = false end
	local pos = int.idiv((h - ih) * w, 2) + int.idiv(w - iw, 2)
	for _ = 1, ih do
		for _ = 1, iw do
			marks[pos] = true
			pos = pos + 1
		end
		pos = pos + (w - iw)
	end
	local r = int.idiv(w, 2)
	local y0 = int.idiv(h, 2) - r
	local left, right, top, bottom = 0, r * 2, r, r
	local err, threshold = r * r, (r - 1) * r -- local_20, local_24 (unsigned)
	local step, step2 = 0, r * 2 -- local_1c, local_2c
	local i, limit = 0, r -- local_28, local_18
	local function row(y, x0, x1)
		for x = x0, x1 do marks[(y0 + y) * w + x] = true end
	end
	repeat
		if left <= right then
			row(top, left, right)
			row(bottom, left, right)
		end
		if top <= bottom then
			for y = top, bottom do marks[(y0 + left) * w + y] = true end
			for y = top, bottom do marks[(y0 + right) * w + y] = true end
		end
		step = step + 2
		bottom = bottom + 1
		err = (err + 1 - step) % 4294967296
		top = top - 1
		i = i + 1
		if err <= threshold then
			limit = limit - 1
			step2 = step2 - 2
			right = right - 1
			left = left + 1
			threshold = (threshold - step2) % 4294967296
		end
	until i > limit
	local count = 0
	for k = 0, w * h - 1 do if marks[k] then count = count + 1 end end
	return marks, count
end
M.vision_grid = vision_grid

-- GenerateVisionRecords 0x466bd0: records in rings around the centre (directions right,
-- down, left, up), each with its tile offset for `stride` and one or two earlier records
-- on the way back to the centre. Record k = { dx, dy, offset, deps = { a[, b] } } (1-based
-- indices into the record list; record 1 is the centre).
local function vision_records(w, h, marks, total, stride)
	local cell = {} -- grid cell -> record index
	local function at(x, y) return (int.idiv(h, 2) + y) * w + int.idiv(w, 2) + x end
	local records = { { dx = 0, dy = 0, offset = 0, deps = {} } }
	cell[at(0, 0)] = 1
	local count, side = 1, 2
	local x, y = -1, -1
	local lens = { [0] = h, w, h, w, h }
	while count < total do
		for k = 0, 3 do
			local dx, dy = M.DIRECTIONS[k + 1][1], M.DIRECTIONS[k + 1][2]
			local n
			local skip_first = false
			if side <= lens[k] then
				y = y + dy
				n = lens[k + 1] - 1
				if n > side then n = side end
				skip_first = true
			else
				x = x + dx * side
				y = y + dy * side
				n = 1
			end
			repeat
				if skip_first then
					skip_first = false
				else
					if marks[at(x, y)] then
						local rec = { dx = x, dy = y, offset = y * stride + x }
						if not (x == 0 and y == 0) then
							local ax = x == 0 and 0 or (x > 0 and x - 1 or x + 1)
							local ay = y == 0 and 0 or (y > 0 and y - 1 or y + 1)
							local mx, my = math.abs(ax), math.abs(ay)
							if mx == my or (x == 0 and dx ~= 0) or (y == 0 and dy ~= 0) then
								rec.deps = { cell[at(ax, ay)] }
							else
								local first = cell[at(ax, ay)]
								if my < mx then
									ay = ay + (y < 0 and -1 or 1)
								else
									ax = ax + (x < 0 and -1 or 1)
								end
								rec.deps = { first, cell[at(ax, ay)] }
							end
						end
						records[#records + 1] = rec
						cell[at(x, y)] = #records
						count = count + 1
					end
					y = y + dy
					n = n - 1
				end
				x = x + dx
			until n == 0
			x = x - dx
			y = y - dy
		end
		if side < w - 1 then x = x - 1 end
		if side < h - 1 then y = y - 1 end
		side = side + 2
	end
	return records
end
M.vision_records = vision_records

-- Templates for a map stride (0x466e40 over the descriptors 0x508a50: w = h = 3 + 2 sight).
function M.templates(stride)
	local t = {}
	for sight = 0, M.SIGHTS - 1 do
		local w = 3 + 2 * sight
		local marks, total = vision_grid(w, w, M.INNER, M.INNER)
		local inner = M.INNER * M.INNER
		t[sight] = { width = w, height = w, inner = inner, outer = total - inner,
			records = vision_records(w, w, marks, total, stride) }
	end
	return t
end

function M.install(G)
	-- InitFogOfWar 0x46c1e0 (tile part): every tile unseen and unexplored.
	function G:fog_init()
		local tiles = self.tiles
		for i = 0, M.TILES - 1 do tiles[i] = bor(tiles[i] or 0, 0xffff) end
		self.vision_templates = M.templates(self.map_w)
	end

	-- SetTileFlagsRect 0x412c40 / ClearTileFlagsRect 0x4134a0.
	function G:tiles_or(tx, ty, w, h, mask)
		local tiles, stride = self.tiles, self.map_w
		for y = ty, ty + h - 1 do
			for x = tx, tx + w - 1 do
				local i = y * stride + x
				tiles[i] = bor(tiles[i], mask)
			end
		end
	end

	function G:tiles_clear(tx, ty, w, h, mask)
		local tiles, stride, keep = self.tiles, self.map_w, bnot(mask)
		for y = ty, ty + h - 1 do
			for x = tx, tx + w - 1 do
				local i = y * stride + x
				tiles[i] = band(tiles[i], keep)
			end
		end
	end

	-- GetTerrainHeightAt 0x445690: 2 high, 1 mid, 0 low from the mini-tile flags.
	function G:terrain_height(x, y)
		local tx, ty = int.sar(x, 5), int.sar(y, 5)
		local i = self.map_w * ty + tx
		local mega = self:megatile_at(i)
		local flags = self:minitile_flags(mega, band(int.sar(x, 3), 3), band(int.sar(y, 3), 3))
		if band(flags, 4) ~= 0 then return 2 end
		return band(int.shr(flags, 1), 1)
	end

	-- RevealSightArea 0x46bbb0 with the four callbacks 0x46c620 (ground), 0x46c570 (air),
	-- 0x46c6a0 (ground, clipped) and 0x46c5b0 (air, clipped).
	function G:reveal_sight(x, y, air, mask, sight)
		if mask >= 0x100 then return end
		local clear = band(bnot(mask * 256), bnot(mask))
		local required = mask
		if not air then required = bor(mask, M.HEIGHT_REQUIRED[self:terrain_height(x, y)]) end
		local tx, ty = int.sar(x, 5), int.sar(y, 5)
		local tpl = self.vision_templates[sight]
		local hw, hh = int.idiv(tpl.width, 2), int.idiv(tpl.height, 2)
		local clipped = tx - hw < 0 or self.map_w <= tx + hw or ty - hh < 0 or self.map_h <= ty + hh
		local tiles, base = self.tiles, self.map_w * ty + tx
		local recs = tpl.records
		local w, h = self.map_w, self.map_h
		local function inside(r)
			return (r.dx + tx) % 4294967296 < w and (r.dy + ty) % 4294967296 < h
		end
		if air then
			for k = 1, tpl.outer do
				local r = recs[k]
				if not clipped or inside(r) then
					local i = base + r.offset
					tiles[i] = band(tiles[i], clear)
				end
			end
			return
		end
		for k = 1, tpl.inner do
			local r = recs[k]
			r.snapshot = 0xff
			if not clipped or inside(r) then
				local i = base + r.offset
				tiles[i] = band(tiles[i], clear)
				r.snapshot = tiles[i]
			end
		end
		for k = tpl.inner + 1, tpl.inner + tpl.outer do
			local r = recs[k]
			r.snapshot = 0xff
			if not clipped or inside(r) then
				for d = #r.deps, 1, -1 do
					if band(recs[r.deps[d]].snapshot, required) == 0 then
						local i = base + r.offset
						tiles[i] = band(tiles[i], clear)
						r.snapshot = tiles[i]
						break
					end
				end
			end
		end
	end

	-- RefreshRememberedTerrainOffscreen 0x40af30 (every 100 steps): a tile the local player
	-- sees shows its current MTXM megatile once it is off the screen (the drawing code does
	-- the tiles on the screen). Only creep makes the two differ; that refresh needs the
	-- camera (0x5264b0) and the creep display update (0x4c0810, 0x4c0ba0), not ported yet.
	function G:refresh_remembered_terrain()
		local tiles, mega, raw, vision = self.tiles, self.megatiles, self.raw_tiles, self.local_vision
		for i = 0, self.map_w * self.map_h - 1 do
			if band(tiles[i], vision) == 0 and band(mega[i], 0x7fff) ~= self:cv5_megatile(raw[i]) then
				error("fog: remembered terrain of a changed tile (0x40af30)", 0)
			end
		end
	end

	-- ResetTileVisibility 0x46c180: every tile unseen again before the units' sight is
	-- revealed anew, unless a vision cheat is on.
	function G:reset_tile_visibility()
		if band(self.cheats, M.VISION_CHEATS) ~= 0 then return end
		local tiles = self.tiles
		for i = 0, self.map_w * self.map_h - 1 do tiles[i] = bor(tiles[i], 0xff) end
	end

	-- GetUnitSightRange 0x423150 (the sight index of the vision templates).
	function G:unit_sight(n, ignore_blind)
		local u, d = self.u, self.dat.units
		if band(u.status[n], 2) ~= 0 and band(u.status[n], 1) == 0 and not self:is_morphing_zerg_building(n) then
			return 4
		end
		if not ignore_blind and u.blind[n] ~= 0 then return 2 end
		local upgrade = self.SIGHT_UPGRADES[u.type[n]]
		if upgrade and self:upgrade_level(u.owner[n], upgrade) ~= 0 then return 11 end
		return d.sight_range[u.type[n]]
	end

	-- UnitHasVisibleCargo 0x4273c0.
	function G:unit_has_visible_cargo(n)
		local u, d = self.u, self.dat.units
		if band(d.special_ability_flags[u.type[n]], M.CARGO_VISION) == 0 then
			if not self:is_transport(n) then return false end
			-- 0x438290: a loaded unit seeing through its transport.
			for k = 0, 7 do
				local c = self:unit_from_ref(u["loaded" .. k][n])
				if c ~= 0 and band(d.special_ability_flags[u.type[c]], M.CARGO_VISION) ~= 0
					and band(u.union_units[c], 1) ~= 0 and u.union_0xc0[c] ~= 0 and u.type[u.union_0xc0[c]] == 0xd7 then
					return true
				end
			end
			return false
		end
		local parent = u.union_0xc0[n]
		return parent ~= 0 and band(u.union_units[n], 1) ~= 0 and u.type[parent] == 0xd7
	end

	-- IsTransport 0x4381f0.
	function G:is_transport(n)
		local u = self.u
		if band(u.status[n], 0x40000000) ~= 0 then return false end
		if u.type[n] == 0x2a and self:upgrade_level(u.owner[n], self.OVERLORD_TRANSPORT) == 0 then return false end
		return self.dat.units.space_provided[u.type[n]] ~= 0
	end

	-- UpdateUnitVision 0x423260.
	function G:unit_update_vision(n)
		local u = self.u
		local owner = u.owner[n]
		if not (owner < 8 or u.parasites[n] ~= 0) or u.type[n] == M.NUKE then return end
		local mask
		if not self:unit_has_visible_cargo(n) and not (u.type[n] == 0xd7 and u.order[n] == 0x11) then
			local parasites = u.parasites[n]
			mask = parasites
			if owner < 8 then mask = bor(mask, self.shared_vision[owner]) end
			for p = 0, 11 do
				if band(parasites, 2 ^ p) ~= 0 then mask = bor(mask, self.shared_vision[p]) end
			end
		else
			mask = M.ALL_PLAYERS
		end
		local s = u.sprite[n]
		self:reveal_sight(int.s16(self.img.spr.x[s]), int.s16(self.img.spr.y[s]), band(u.status[n], M.AIR) ~= 0,
			mask, self:unit_sight(n, false))
	end

	-- UpdateUnitSpriteVisibility 0x438c20.
	function G:unit_update_sprite_visibility(n)
		local u, d, w = self.u, self.dat.units, self.img
		local t = u.type[n]
		local changed = w:sprite_visibility_rect(u.sprite[n], int.s16(d.staredit_placement_size[t * 2]),
			int.s16(d.staredit_placement_size[t * 2 + 1]))
		local sub = u.subunit[n]
		if sub ~= 0 and band(w.spr.flags[u.sprite[sub]], 0x20) == 0 then
			w:sprite_set_visibility(u.sprite[sub], w.spr.visibility[u.sprite[n]])
		end
		if changed then
			local s = u.sprite[n]
			if band(w.spr.flags[s], 0x0f) ~= 0 then
				error("vision: visibility change of a selected sprite (0x4a7b80/0x416d50)", 0)
			end
			if (band(u.status[n], 2) ~= 0 or (t > 0xca and t < 0xd6)) and not self:unit_is_dead(n) then
				error("vision: fog ghost of a building (0x417b10)", 0)
			end
		end
	end
end

return M
