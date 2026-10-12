-- Unit placement: the search for a free spot (FindUnitPlacement 0x47dc30), the ground
-- footprint (UnitTypeFitsAt 0x42ed50: map bounds, IsPointWalkable 0x42ea10, the terrain
-- contours of ContourFootprintFits 0x48b780) and the placement collision callback 0x47ea20.
-- docs/research/native-unit-creation.md, "Placement ordering" and "Ground footprint".
-- Methods of the game.
local bytes = require("game.data.bytes")
local int = require("game.simulation.int")
local mission_data = require("game.data.mission")

local band = int.band
local s16 = int.s16

local M = {}

M.SEARCH_RADIUS = 0x80 -- default rectangle: desired point -128 .. +127
M.BOTTOM_RESERVE = 0x20 -- the bottom 32 pixels of the map stay free
M.ERROR_PLACEMENT = 0x3c
M.WALK_ANY, M.WALK_PARTIAL, M.WALK_ALL = 0x400000, 0x20000000, 0x10000
M.STATUS_IGNORED = 0x200000
M.POWERUP = 0x800
-- Producers whose own flyers collide with them (0x47ea20).
M.PRODUCER_CHILDREN = {
	[0x72] = { [8] = true, [0xb] = true, [9] = true, [0xc] = true, [0x3a] = true }, -- Starport
	[0x9b] = { [0x45] = true, [0x54] = true }, -- Robotics Facility
	[0xa7] = { [0x46] = true, [0x48] = true, [0x47] = true, [0x3c] = true }, -- Stargate
}

-- Unpacks the mission's CONT arrays (top, right, bottom, left) into 0-based SoA records.
function M.contours(mission_contours, mission_module)
	local out = {}
	for d = 1, 4 do
		local a = { axis = {}, start = {}, stop = {}, dir = {}, info = {}, count = mission_contours.counts[d] }
		for k = 1, a.count do
			local axis, start, stop, dir, info = mission_module.contour(mission_contours, d, k)
			a.axis[k - 1], a.start[k - 1], a.stop[k - 1], a.dir[k - 1], a.info[k - 1] = axis, start, stop, dir, info
		end
		out[d - 1] = a
	end
	return out
end

-- The 16-bit signed comparison binary searches of 0x48b780: the first record whose axis
-- (doubled, 16-bit) is above `key`.
local function first_above(a, key)
	local count = a.count
	if count == 0 then return 0 end
	local low, high = 0, count
	repeat
		local mid = math.floor((high + low) / 2)
		if s16(a.axis[mid] * 2) <= s16(key) then
			low = mid + 1
		else
			high = mid
		end
	until low >= high
	return high
end

-- SetBoxExtents 0x489160 (and 0x489270 with a box array): the extents of a box reaching
-- left, up, right and down from its point.
function M.set_box_extents(e, left, up, right, down)
	e[0], e[1], e[2], e[3] = s16(up), s16(-right), s16(-down), s16(left)
	e[8], e[9], e[10], e[11] = s16(up + 1), s16(-1 - right), s16(-1 - down), s16(left + 1)
end

-- SetUnitTypeExtents 0x4891c0: the signed extents of a type (or a symmetric radius code
-- with bit 0x8000) at context +0x20 (index 0..3 of e) and the widened ones at +0x30 (8..11).
function M.set_extents(ext, code, e)
	if code < 0x8000 then
		M.set_box_extents(e, s16(ext[code * 4]), s16(ext[code * 4 + 1]), s16(ext[code * 4 + 2]), s16(ext[code * 4 + 3]))
	else
		local r = code % 0x8000
		e[0], e[3], e[1], e[2] = r, r, s16(-r), s16(-r)
		e[8], e[11], e[9], e[10] = s16(r + 1), s16(r + 1), s16(-1 - r), s16(-1 - r)
	end
end

function M.install(G)
	G.region_cell = mission_data.region_cell

	-- Region group of a region (the connectivity of the region map, +0x1e of a region).
	function G:region_group(region)
		local r = self.regions
		return bytes.u16(r.data, r.regions_at + region * mission_data.REGION_SIZE + 2)
	end

	-- SetUnitTypeExtents 0x4891c0 on the global contour context.
	function G:contour_extents(code)
		M.set_extents(self.dat.units.unit_extents, code, self.extent_ctx)
	end

	-- ContourFootprintFits 0x48b780 for the prepared extents at (x, y).
	function G:contour_fits(x, y)
		local e, c = self.extent_ctx, self.contours
		local top, right, bottom, left = c[0], c[1], c[2], c[3]
		-- Top contours before y that still reach it.
		local k = first_above(top, y * 2 - 1)
		while k > 0 do
			local p = k - 1
			if s16(e[top.dir[p]] + top.axis[p]) < y then break end
			if not (x < s16(top.start[p] + e[1]) or s16(top.stop[p] + e[3]) < x) then return false end
			k = p
		end
		-- Right contours after x that still reach it.
		k = first_above(right, x * 2 + 1)
		while k < right.count do
			if x < s16(e[right.dir[k]] + right.axis[k]) then break end
			if s16(right.start[k] + e[2]) <= y and y <= s16(right.stop[k] + e[0]) then return false end
			k = k + 1
		end
		-- Bottom contours after y.
		k = first_above(bottom, y * 2 + 1)
		while k < bottom.count do
			if y < s16(e[bottom.dir[k]] + bottom.axis[k]) then break end
			if s16(bottom.start[k] + e[1]) <= x and x <= s16(bottom.stop[k] + e[3]) then return false end
			k = k + 1
		end
		-- Left contours before x.
		k = first_above(left, x * 2 - 1)
		while k > 0 do
			local p = k - 1
			if s16(e[left.dir[p]] + left.axis[p]) < x then return true end
			if not (y < s16(left.start[p] + e[2]) or s16(left.stop[p] + e[0]) < y) then return false end
			k = p
		end
		return true
	end

	-- IsPointWalkable 0x42ea10.
	function G:point_walkable(x, y)
		local i = int.sar(x, 5) + int.sar(y, 5) * self.map_w
		local f = self.tiles[i]
		if band(f, M.WALK_ANY) ~= 0 then return true end
		if band(f, M.WALK_PARTIAL) == 0 then return band(f, M.WALK_ALL) ~= 0 end
		local mega = band(self.megatiles[i], 0x7fff)
		return band(self:minitile_flags(mega, band(int.sar(x, 3), 3), band(int.sar(y, 3), 3)), 1) ~= 0
	end

	-- UnitTypeFitsAt 0x42ed50 (through 0x412c30).
	function G:unit_type_fits(t, x, y)
		local ext = self.dat.units.unit_extents
		x, y = s16(x), s16(y)
		if not (s16(ext[t * 4]) <= x and s16(ext[t * 4 + 2]) + x < self.map_w_px) then return false end
		if not (s16(ext[t * 4 + 1]) <= y and s16(ext[t * 4 + 3]) + y < self.map_h_px) then return false end
		if not self:point_walkable(x, y) then return false end
		self:contour_extents(t)
		return self:contour_fits(x, y)
	end

	-- IsBoxPlacementLegal 0x42ec70: a w x h box centred on (x, y) lies inside the map on
	-- walkable ground clear of the terrain contours.
	function G:box_fits(x, y, w, h)
		local left, top = s16(x - int.idiv(w, 2)), s16(y - int.idiv(h, 2))
		if left < 0 or left + w >= self.map_w_px or top < 0 or top + h >= self.map_h_px then return false end
		if not self:point_walkable(x, y) then return false end
		M.set_box_extents(self.extent_ctx, int.idiv(w - 1, 2), int.idiv(h - 1, 2), w - int.idiv(w, 2) - 1,
			h - int.idiv(h, 2) - 1)
		return self:contour_fits(x, y)
	end

	-- IsUnitDead 0x41fe20: no sprite, or a finished Death order.
	function G:unit_is_dead(n)
		local u = self.u
		return u.sprite[n] == -1 or (u.order[n] == 0 and u.order_state[n] == 1)
	end

	-- PlacementCollisionCallback 0x47ea20: does unit c block `wanted`?
	function G:placement_blocks(c, wanted)
		if self:unit_is_dead(c) then return false end
		local u, flags = self.u, self.dat.units.special_ability_flags
		local cs, ws = u.status[c], u.status[wanted]
		if band(cs, M.STATUS_IGNORED) ~= 0 or band(ws, M.STATUS_IGNORED) ~= 0 then return false end
		local wt, ct = u.type[wanted], u.type[c]
		if band(flags[wt], M.POWERUP) ~= 0 then
			return band(cs, 2) ~= 0 or band(flags[ct], M.POWERUP) ~= 0
		end
		local children = M.PRODUCER_CHILDREN[ct]
		if children and children[wt] then return true end
		return band(int.shr(int.bxor(ws, cs), 2), 1) == 0
	end

	-- The first unit in the rectangle that blocks `n` (0x443110 with 0x47ea20), or 0.
	function G:placement_blocker(n, left, top, right, bottom)
		for _, c in ipairs(self:spatial_query(left, top, right, bottom)) do
			if self:placement_blocks(c, n) then return c end
		end
		return 0
	end

	-- RegionAt 0x481b10: the region of a pixel, through a split tile's 8x8 cell mask.
	function G:region_at(x, y)
		local r = self.regions
		local v = self.region_cell(r, int.sar(x, 5), int.sar(y, 5))
		if v < 0x2000 then return v end
		local s, at = r.data, r.splits_at + (v - 0x2000) * 6
		local mask = bytes.u16(s, at)
		local bit = band(int.sar(y, 3), 3) * 4 + band(int.sar(x, 3), 3)
		if band(mask, 2 ^ bit) ~= 0 then return bytes.u16(s, at + 4) end
		return bytes.u16(s, at + 2)
	end

	-- Reachable 0x48e980 / SameRegionGroup 0x481d00: a ground unit (pathing flag bit 0)
	-- only accepts a point of its own region group.
	function G:placement_reachable(n, x, y)
		if band(self.u.pathing_flags[n], 1) == 0 then return true end
		local s = self.u.sprite[n]
		local here = self:region_at(s16(self.img.spr.x[s]), s16(self.img.spr.y[s]))
		local there = self:region_at(s16(x), s16(y))
		return self:region_group(here) == self:region_group(there)
	end

	-- GetUnitBoundsAt 0x409d20: the box of type t centred on (x, y), signed WORDs.
	function G:type_box(t, x, y)
		local ext = self.dat.units.unit_extents
		return s16(x - ext[t * 4]), s16(y - ext[t * 4 + 1]), s16(x + ext[t * 4 + 2]), s16(y + ext[t * 4 + 3])
	end
	local box = G.type_box

	-- IsBoxInValidBounds 0x47e300.
	local function valid(self, l, t, r, b)
		return l >= 0 and r < self.map_w_px and t >= 0 and b < self.map_h_px - M.BOTTOM_RESERVE
	end

	local function accept(self, n, x, y, extra)
		if not self:placement_reachable(n, x, y) then return false end
		if band(self.u.status[n], 4) ~= 0 then return true end
		if not self:unit_type_fits(self.u.type[n], x, y) then return false end
		if extra then error("placement: additional point (0x4928f0)", 0) end
		return true
	end

	local function align8(pos, step)
		local r = band(pos, 7)
		if r ~= 0 then step = step + 8 - r end
		return step
	end

	-- PlacementRingScan 0x47e340: one ring (x, y, w, h) aligned to 8 pixels and clipped to
	-- the search rectangle, scanned bottom left->right, right bottom->top, top right->left,
	-- left top->bottom; a blocker makes the scan jump past it.
	function G:placement_ring(n, x, y, w, h, rect, extra)
		local t = self.u.type[n]
		local ext = self.dat.units.unit_extents
		local width = s16(ext[t * 4 + 2] + 1 + ext[t * 4])
		local x0 = s16(band(x, 0xfff8))
		if x0 < rect[1] then x0 = rect[1] end
		local x1 = s16(band(x + w + 7, 0xfff8))
		if x1 > rect[3] then x1 = rect[3] end
		local y0 = s16(band(y, 0xfff8))
		if y0 < rect[2] then y0 = rect[2] end
		local y1 = s16(band(y + h + 7, 0xfff8))
		if y1 > rect[4] then y1 = rect[4] end
		local start = x0
		if w > width * 2 then start = s16(band(width + 7, 0xfffffff8) + x0) end
		-- Bottom side.
		local cx = start
		local l, tp, r, b = box(self, t, cx, y1)
		while cx <= x1 do
			local step = 8
			if valid(self, l, tp, r, b) then
				local c = self:placement_blocker(n, l, tp, r, b)
				if c == 0 then
					if accept(self, n, cx, y1, extra) then return cx, y1 end
				else
					local _, _, cr = self:unit_rect(c)
					step = align8(cx + (cr - l + 1), cr - l + 1)
				end
			end
			cx = s16(cx + step)
			l, r = s16(l + step), s16(r + step)
		end
		-- Right side.
		local cy = y1
		l, tp, r, b = box(self, t, x1, cy)
		while cy >= y0 do
			local step = 8
			if valid(self, l, tp, r, b) then
				local c = self:placement_blocker(n, l, tp, r, b)
				if c == 0 then
					if accept(self, n, x1, cy, extra) then return x1, cy end
				else
					local _, ct = self:unit_rect(c)
					step = align8(cy - (b - ct + 1), b - ct + 1)
				end
			end
			cy = s16(cy - step)
			tp, b = s16(tp - step), s16(b - step)
		end
		-- Top side.
		cx = x1
		l, tp, r, b = box(self, t, cx, y0)
		while cx >= x0 do
			local step = 8
			if valid(self, l, tp, r, b) then
				local c = self:placement_blocker(n, l, tp, r, b)
				if c == 0 then
					if accept(self, n, cx, y0, extra) then return cx, y0 end
				else
					local cl = self:unit_rect(c)
					step = align8(cx - (r - cl + 1), r - cl + 1)
				end
			end
			cx = s16(cx - step)
			l, r = s16(l - step), s16(r - step)
		end
		-- Left side.
		cy = y0
		l, tp, r, b = box(self, t, x0, cy)
		while cy <= y1 do
			local step = 8
			if valid(self, l, tp, r, b) then
				local c = self:placement_blocker(n, l, tp, r, b)
				if c == 0 then
					if accept(self, n, x0, cy, extra) then return x0, cy end
				else
					local _, _, _, cb = self:unit_rect(c)
					step = align8(cy + (cb - tp + 1), cb - tp + 1)
				end
			end
			cy = s16(cy + step)
			tp, b = s16(tp + step), s16(b + step)
		end
		return nil
	end

	-- FindUnitPlacement 0x47dc30: the desired point when nothing blocks it (and, on the
	-- ground, the footprint fits), else rings growing by 16 pixels from the blocker's size
	-- until one finds a spot or the ring covers the search rectangle. Returns x, y or nil
	-- with the error word set. rect = { left, top, right, bottom } or nil for +-128.
	function G:find_unit_placement(n, x, y, rect, displace, extra)
		rect = rect or { s16(x - M.SEARCH_RADIUS), s16(y - M.SEARCH_RADIUS), s16(x + 0x7f), s16(y + 0x7f) }
		if rect[2] < 0 then rect[2] = 0 end
		if self.map_h_px - M.BOTTOM_RESERVE <= rect[4] then rect[4] = s16(self.map_h_px - M.BOTTOM_RESERVE - 1) end
		if rect[1] < 0 then rect[1] = 0 end
		if self.map_w_px <= rect[3] then rect[3] = s16(self.map_w_px - 1) end
		local u, ext = self.u, self.dat.units.unit_extents
		local t = u.type[n]
		local top, bottom = s16(y - ext[t * 4 + 1]), s16(ext[t * 4 + 3] + y)
		local left, right = s16(x - ext[t * 4]), s16(ext[t * 4 + 2] + x)
		local blocker = self:placement_blocker(n, left, top, right, bottom)
		if blocker == 0 and left >= 0 and right < self.map_w_px and top >= 0
			and bottom < self.map_h_px - M.BOTTOM_RESERVE then
			if band(u.status[n], 4) ~= 0 or self:unit_type_fits(t, x, y) then
				if extra then error("placement: additional point (0x4928f0)", 0) end
				return x, y
			end
			if not displace then
				self.error_code = M.ERROR_PLACEMENT
				return nil
			end
		end
		local w, h = 8, 8
		if blocker ~= 0 then
			local bt = u.type[blocker]
			local bw = s16(ext[bt * 4 + 2]) + s16(ext[t * 4]) + 2
			local bh = s16(ext[bt * 4 + 3]) + s16(ext[t * 4 + 1]) + 2
			w = bw < 9 and 8 or bw % 65536
			h = bh < 9 and 8 or bh % 65536
		end
		local h2 = h * 2 % 65536
		while true do
			local sw, sh = s16(w), s16(h)
			if x - sw < rect[1] and rect[3] < sw + x and y - sh < rect[2] and rect[4] < sh + y then break end
			local px, py = self:placement_ring(n, s16(x - w), s16(y - h), s16(w * 2), s16(h2), rect, extra)
			h2 = (h2 + 0x20) % 65536
			w, h = (w + 16) % 65536, (h + 16) % 65536
			if px then return px, py end
		end
		self.error_code = M.ERROR_PLACEMENT
		return nil
	end
end

return M
