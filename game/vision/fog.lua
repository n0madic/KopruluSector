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
local bit_band = int.bit_band
local TWO32 = 4294967296

local M = {}

M.TILES = 0x10000
M.SIGHTS = 12
M.INNER = 3 -- the unconditional inner square of every template
M.HEIGHT_REQUIRED = { [0] = 0x07000000, [1] = 0x05000000, [2] = 0x01000000 } -- by source height
M.CARGO_VISION = 0x08 -- units.dat flag: vision through the transport (0x4273c0)
M.NUKE = 14
M.REVEAL_CACHE = 512 -- reveal tile runs of each cache generation (G:reveal_sight)
M.AIR = 0x4
M.ALL_PLAYERS = 0xff
M.VISION_CHEATS = 0x4001 -- 0x5432fc bits that keep every tile seen
M.SPRITE_SELECTED, M.SPRITE_DASHED = 0x08, 0x06
M.DOODAD_FIRST, M.DOODAD_LAST = 0xcb, 0xd5 -- map doodads that leave fog ghosts
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
-- Besides the records, a template keeps their fields as flat arrays for RevealSightArea:
-- off, dx, dy, dep1 and dep2 (0 without a second dependency), and snap, the tile values the
-- records leave during one reveal; row_lo[dy], row_hi[dy] are its records' dx range in each
-- row when every row's records are contiguous (rows = true).
function M.templates(stride)
	local t = {}
	for sight = 0, M.SIGHTS - 1 do
		local w = 3 + 2 * sight
		local marks, total = vision_grid(w, w, M.INNER, M.INNER)
		local inner = M.INNER * M.INNER
		local records = vision_records(w, w, marks, total, stride)
		local tpl = { width = w, height = w, inner = inner, outer = total - inner, records = records,
			off = {}, dx = {}, dy = {}, dep1 = {}, dep2 = {}, snap = {} }
		local lo, hi, n = {}, {}, {}
		for k, r in ipairs(records) do
			tpl.off[k], tpl.dx[k], tpl.dy[k] = r.offset, r.dx, r.dy
			tpl.dep1[k], tpl.dep2[k], tpl.snap[k] = r.deps[1] or 0, r.deps[2] or 0, 0xff
			lo[r.dy], hi[r.dy] = math.min(lo[r.dy] or r.dx, r.dx), math.max(hi[r.dy] or r.dx, r.dx)
			n[r.dy] = (n[r.dy] or 0) + 1
		end
		tpl.rows = true
		for dy, c in pairs(n) do
			if c ~= hi[dy] - lo[dy] + 1 then tpl.rows = false end
		end
		tpl.row_lo, tpl.row_hi = lo, hi
		t[sight] = tpl
	end
	return t
end

function M.install(G)
	-- InitFogOfWar 0x46c1e0 (tile part): every tile unseen and unexplored.
	-- The reveal records of what is already cleared (G:reveal_sight) hold until a visibility
	-- bit is set again: every writer that sets one calls this.
	function G:visibility_bits_set()
		self.reveal_applied, self.reveal_rows = nil, nil
	end

	function G:fog_init()
		local tiles = self.tiles
		for i = 0, M.TILES - 1 do tiles[i] = bor(tiles[i] or 0, 0xffff) end
		self.vision_templates = M.templates(self.map_w)
		self:visibility_bits_set()
	end

	-- SetTileFlagsRect 0x412c40 / ClearTileFlagsRect 0x4134a0.
	function G:tiles_or(tx, ty, w, h, mask)
		local tiles, stride = self.tiles, self.map_w
		if bit_band(mask, 0xffff) ~= 0 then self:visibility_bits_set() end
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
		if bit_band(flags, 4) ~= 0 then return 2 end
		return band(int.shr(flags, 1), 1)
	end

	-- RevealSightArea 0x46bbb0 with the four callbacks 0x46c620 (ground), 0x46c570 (air),
	-- 0x46c6a0 (ground, clipped) and 0x46c5b0 (air, clipped).
	-- The tiles a reveal clears depend only on the source tile, the sight, air or ground, the
	-- source's height class and the terrain's height bits (0x07000000, static during a
	-- mission): within a reveal the own bits are cleared before the dependency tests, so the
	-- other players' bits never decide them. A reveal at a known key therefore replays the
	-- stored tile runs (plan §7 phase 3 item 4: an incremental
	-- update with proven equivalence; every scenario compares the tile flags with the
	-- original's at every step). The cache keeps two generations: a full one becomes the old
	-- generation, whose entries move to the new one when used again, so the reveals in use stay
	-- cached across the turnover.
	-- TODO: terrain changes of phase 6 that touch the height bits must call reveal_cache_flush.
	function G:reveal_cache_flush()
		self.reveal_cache, self.reveal_cache_old, self.reveal_cache_count = {}, {}, 0
	end

	local function record_reveal(self, key, runs)
		if not self.reveal_cache then self:reveal_cache_flush() end
		if self.reveal_cache_count >= M.REVEAL_CACHE then
			self.reveal_cache_old, self.reveal_cache, self.reveal_cache_count = self.reveal_cache, {}, 0
		end
		self.reveal_cache[key] = runs
		self.reveal_cache_count = self.reveal_cache_count + 1
	end

	local function cached_reveal(self, key)
		local cache = self.reveal_cache
		if not cache then return nil end
		local runs = cache[key]
		if runs then return runs end
		runs = self.reveal_cache_old[key]
		if runs then
			self.reveal_cache_old[key] = nil
			record_reveal(self, key, runs)
		end
		return runs
	end

	-- Packed prefix sums of the tiles that block a reveal from each height class (bits
	-- 0x07000000 under HEIGHT_REQUIRED[h], 17 bits per class): a ground reveal whose whole
	-- template square holds none takes the fast path, where every dependency test passes.
	-- TODO: terrain changes of phase 6 that touch the height bits must reset it too.
	local CLASS = { [0] = 1, [1] = 2 ^ 17, [2] = 2 ^ 34 }
	local function blocking_sums(self)
		local p = self.reveal_blocking
		if p then return p end
		local w, h, tiles = self.map_w, self.map_h, self.tiles
		p = {}
		for i = 0, (w + 1) * (h + 1) - 1 do p[i] = 0 end
		for y = 0, h - 1 do
			local row = 0
			for x = 0, w - 1 do
				local f = tiles[y * w + x]
				local v = 0
				for c = 0, 2 do
					if bit_band(f, M.HEIGHT_REQUIRED[c]) ~= 0 then v = v + CLASS[c] end
				end
				row = row + v
				p[(y + 1) * (w + 1) + x + 1] = p[y * (w + 1) + x + 1] + row
			end
		end
		self.reveal_blocking = p
		return p
	end

	local function square_free(self, tx, ty, hw, hh, height)
		local p = blocking_sums(self)
		local stride = self.map_w + 1
		local x0, y0, x1, y1 = tx - hw, ty - hh, tx + hw + 1, ty + hh + 1
		local packed = p[y1 * stride + x1] - p[y0 * stride + x1] - p[y1 * stride + x0] + p[y0 * stride + x0]
		return math.floor(packed / CLASS[height]) % 2 ^ 17 == 0
	end

	-- Clears `bits` in the tiles a..b of a map row from index `row`.
	local function clear_run(tiles, row, a, b, bits)
		for i = row + a, row + b do
			local f = tiles[i]
			tiles[i] = f - bit_band(f, bits)
		end
	end

	-- reveal_rows[bits] keeps for each map row y one run of tiles lo[y]..hi[y] whose `bits`
	-- are clear, so overlapping reveals clear only what lies outside it. Returns lo, hi.
	local function cleared_rows(self, bits)
		local rows = self.reveal_rows
		if not rows then
			rows = {}
			self.reveal_rows = rows
		end
		local run = rows[bits]
		if not run then
			run = { lo = {}, hi = {} }
			rows[bits] = run
		end
		return run.lo, run.hi
	end

	-- Clears `bits` in the tiles a..b of map row y outside its cleared run; the run becomes
	-- their union when the two meet, else a..b.
	local function clear_row(tiles, w, run_lo, run_hi, y, a, b, bits)
		local lo, hi = run_lo[y], run_hi[y]
		if lo and a <= hi + 1 and lo <= b + 1 then
			if a < lo then
				clear_run(tiles, y * w, a, lo - 1, bits)
				run_lo[y] = a
			end
			if hi < b then
				clear_run(tiles, y * w, hi + 1, b, bits)
				run_hi[y] = b
			end
		else
			clear_run(tiles, y * w, a, b, bits)
			run_lo[y], run_hi[y] = a, b
		end
	end

	-- The tile indices of a reveal as runs within map rows: a flat list y, a, b, ...
	local function row_runs(list, w)
		table.sort(list)
		local runs, n = {}, 0
		local k = 1
		while k <= #list do
			local first = list[k]
			local y = math.floor(first / w)
			local last = first
			while k < #list and list[k + 1] == last + 1 and math.floor(list[k + 1] / w) == y do
				k = k + 1
				last = list[k]
			end
			runs[n + 1], runs[n + 2], runs[n + 3] = y, first - y * w, last - y * w
			n = n + 3
			k = k + 1
		end
		return runs
	end

	local function clear_runs(self, runs, bits)
		local run_lo, run_hi = cleared_rows(self, bits)
		local tiles, w = self.tiles, self.map_w
		for k = 1, #runs, 3 do
			clear_row(tiles, w, run_lo, run_hi, runs[k], runs[k + 1], runs[k + 2], bits)
		end
	end

	function G:reveal_sight(x, y, air, mask, sight)
		-- An empty mask clears nothing (the dependency tests decide only which tiles).
		if mask >= 0x100 or mask == 0 then return end
		local bits = mask * 257 -- the mask's seen and explored bits, cleared below
		local height = air and 3 or self:terrain_height(x, y)
		local tx, ty = int.sar(x, 5), int.sar(y, 5)
		local key = ((ty * 256 + tx) * M.SIGHTS + sight) * 4 + height
		-- A reveal only clears bits, so one repeated since the bits were last set changes nothing.
		local applied = self.reveal_applied
		if not applied then
			applied = {}
			self.reveal_applied = applied
		end
		local applied_key = key * 256 + mask
		if applied[applied_key] then return end
		applied[applied_key] = true
		local cached = cached_reveal(self, key)
		if cached then
			clear_runs(self, cached, bits)
			return
		end
		local tpl = self.vision_templates[sight]
		local hw, hh = int.idiv(tpl.width, 2), int.idiv(tpl.height, 2)
		local w, h = self.map_w, self.map_h
		local clipped = tx - hw < 0 or w <= tx + hw or ty - hh < 0 or h <= ty + hh
		-- An unclipped ground reveal with no blocking tile in its square clears the whole
		-- template; it needs no stored list.
		if not air and not clipped and tpl.rows and square_free(self, tx, ty, hw, hh, height) then
			local run_lo, run_hi = cleared_rows(self, bits)
			local tiles, row_lo, row_hi = self.tiles, tpl.row_lo, tpl.row_hi
			for dy = -hh, hh do
				local dx = row_lo[dy]
				if dx then clear_row(tiles, w, run_lo, run_hi, ty + dy, tx + dx, tx + row_hi[dy], bits) end
			end
			return
		end
		-- The records' tiles, then cleared as row runs. A clipped reveal skips the records
		-- outside the map (unsigned compares).
		local tiles, list = self.tiles, {}
		local base = w * ty + tx
		local off, dxs, dys = tpl.off, tpl.dx, tpl.dy
		local inner = tpl.inner
		if air then
			for k = 1, tpl.outer do
				if not clipped or ((dxs[k] + tx) % TWO32 < w and (dys[k] + ty) % TWO32 < h) then
					list[#list + 1] = base + off[k]
				end
			end
		else
			-- snap holds the value each record's tile is left with (its own bits cleared).
			local required = bor(mask, M.HEIGHT_REQUIRED[height])
			local snap, dep1, dep2 = tpl.snap, tpl.dep1, tpl.dep2
			for k = 1, inner do
				local v = 0xff
				if not clipped or ((dxs[k] + tx) % TWO32 < w and (dys[k] + ty) % TWO32 < h) then
					local i = base + off[k]
					v = tiles[i] - bit_band(tiles[i], bits)
					list[#list + 1] = i
				end
				snap[k] = v
			end
			-- An outer record is seen when one of its dependencies (the second checked first)
			-- left none of the required bits.
			for k = inner + 1, inner + tpl.outer do
				local v = 0xff
				if not clipped or ((dxs[k] + tx) % TWO32 < w and (dys[k] + ty) % TWO32 < h) then
					local d2 = dep2[k]
					if (d2 ~= 0 and bit_band(snap[d2], required) == 0) or bit_band(snap[dep1[k]], required) == 0 then
						local i = base + off[k]
						v = tiles[i] - bit_band(tiles[i], bits)
						list[#list + 1] = i
					end
				end
				snap[k] = v
			end
		end
		local runs = row_runs(list, w)
		record_reveal(self, key, runs)
		clear_runs(self, runs, bits)
	end

	-- RefreshRememberedTerrainOffscreen 0x40af30 (every 100 steps): a tile the local player
	-- sees shows its current MTXM megatile once it is off the screen (the drawing code does
	-- the tiles on the screen). Only creep makes the two differ; that refresh needs the
	-- camera (0x5264b0) and the creep display update (0x4c0810, 0x4c0ba0), not ported yet.
	-- Only the tiles whose two megatiles differed at the first refresh or that creep changed
	-- since (terrain_changed, kept by G:creep_apply) can differ, so the others are skipped.
	function G:refresh_remembered_terrain()
		local tiles, mega, raw, vision = self.tiles, self.megatiles, self.raw_tiles, self.local_vision
		local changed = self.terrain_changed
		if not changed then
			changed = {}
			for i = 0, self.map_w * self.map_h - 1 do
				if band(mega[i], 0x7fff) ~= self:cv5_megatile(raw[i]) then changed[i] = true end
			end
			self.terrain_changed = changed
		end
		for i in pairs(changed) do
			if bit_band(tiles[i], vision) == 0 and band(mega[i], 0x7fff) ~= self:cv5_megatile(raw[i]) then
				error("fog: remembered terrain of a changed tile (0x40af30)", 0)
			end
		end
	end

	-- ResetTileVisibility 0x46c180: every tile unseen again before the units' sight is
	-- revealed anew, unless a vision cheat is on.
	function G:reset_tile_visibility()
		if bit_band(self.cheats, M.VISION_CHEATS) ~= 0 then return end
		local tiles = self.tiles
		for i = 0, self.map_w * self.map_h - 1 do tiles[i] = bor(tiles[i], 0xff) end
		self:visibility_bits_set()
	end

	-- GetUnitSightRange 0x423150 (the sight index of the vision templates).
	function G:unit_sight(n, ignore_blind)
		local u, d = self.u, self.dat.units
		if bit_band(u.status[n], 2) ~= 0 and bit_band(u.status[n], 1) == 0 and not self:is_morphing_zerg_building(n) then
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
		if bit_band(d.special_ability_flags[u.type[n]], M.CARGO_VISION) == 0 then
			if not self:is_transport(n) then return false end
			-- 0x438290: a loaded unit seeing through its transport.
			for k = 0, 7 do
				local c = self:unit_from_ref(u["loaded" .. k][n])
				if c ~= 0 and bit_band(d.special_ability_flags[u.type[c]], M.CARGO_VISION) ~= 0
					and bit_band(u.union_units[c], 1) ~= 0 and u.union_0xc0[c] ~= 0 and u.type[u.union_0xc0[c]] == 0xd7 then
					return true
				end
			end
			return false
		end
		local parent = u.union_0xc0[n]
		return parent ~= 0 and bit_band(u.union_units[n], 1) ~= 0 and u.type[parent] == 0xd7
	end

	-- IsTransport 0x4381f0.
	function G:is_transport(n)
		local u = self.u
		if bit_band(u.status[n], 0x40000000) ~= 0 then return false end
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
				if bit_band(parasites, 2 ^ p) ~= 0 then mask = bor(mask, self.shared_vision[p]) end
			end
		else
			mask = M.ALL_PLAYERS
		end
		local s = u.sprite[n]
		self:reveal_sight(int.s16(self.img.spr.x[s]), int.s16(self.img.spr.y[s]), bit_band(u.status[n], M.AIR) ~= 0,
			mask, self:unit_sight(n, false))
	end

	-- UpdateUnitSpriteVisibility 0x438c20.
	function G:unit_update_sprite_visibility(n)
		local u, d, w = self.u, self.dat.units, self.img
		local t = u.type[n]
		local changed = w:sprite_visibility_rect(u.sprite[n], int.s16(d.staredit_placement_size[t * 2]),
			int.s16(d.staredit_placement_size[t * 2 + 1]))
		local sub = u.subunit[n]
		if sub ~= 0 and bit_band(w.spr.flags[u.sprite[sub]], 0x20) == 0 then
			w:sprite_set_visibility(u.sprite[sub], w.spr.visibility[u.sprite[n]])
		end
		if changed then
			-- The local player lost sight of the unit: it leaves the local selection
			-- (DeselectUnitLocally 0x4a7b80), loses its circles, and a building or a map
			-- doodad (203..213) leaves a fog-of-war ghost.
			local s = u.sprite[n]
			if bit_band(w.spr.flags[s], M.SPRITE_SELECTED) ~= 0 then
				local rest = {}
				for k = 0, 11 do
					local m = self.local_selection[k]
					if m == 0 then break end
					if m ~= n then rest[#rest + 1] = m end
				end
				self:select_locally(rest)
			end
			w:sprite_remove_circle(s)
			if bit_band(w.spr.flags[s], M.SPRITE_DASHED) ~= 0 then
				error("vision: shared dashed selection circles of a team game (0x416cd0)", 0)
			end
			if (bit_band(u.status[n], 2) ~= 0 or (t >= M.DOODAD_FIRST and t <= M.DOODAD_LAST)) and not self:unit_is_dead(n) then
				w:fog_create(t, s)
			end
		end
	end
end

return M
