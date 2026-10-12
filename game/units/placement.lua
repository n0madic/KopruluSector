-- Unit placement: the search for a free spot (FindUnitPlacement 0x47dc30), the ground
-- footprint (UnitTypeFitsAt 0x42ed50: map bounds, IsPointWalkable 0x42ea10, the terrain
-- contours of ContourFootprintFits 0x48b780) and the placement collision callback 0x47ea20.
-- docs/research/native-unit-creation.md, "Placement ordering" and "Ground footprint".
-- Methods of the game.
local int = require("game.simulation.int")

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
		local a = { axis = {}, start = {}, stop = {}, dir = {}, count = mission_contours.counts[d] }
		for k = 1, a.count do
			local axis, start, stop, dir = mission_module.contour(mission_contours, d, k)
			a.axis[k - 1], a.start[k - 1], a.stop[k - 1], a.dir[k - 1] = axis, start, stop, dir
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

function M.install(G)
	-- SetUnitTypeExtents 0x4891c0: the signed extents of a type (or a symmetric radius code
	-- with bit 0x8000) at context +0x20 (index 0..3) and the widened ones at +0x30 (8..11).
	function G:contour_extents(code)
		local e = self.extent_ctx
		if code < 0x8000 then
			local ext = self.dat.units.unit_extents
			local left, up = s16(ext[code * 4]), s16(ext[code * 4 + 1])
			local right, down = s16(ext[code * 4 + 2]), s16(ext[code * 4 + 3])
			e[0], e[1], e[2], e[3] = up, s16(-right), s16(-down), left
			e[8], e[9], e[10], e[11] = s16(up + 1), s16(-1 - right), s16(-1 - down), s16(left + 1)
		else
			local r = code % 0x8000
			e[0], e[3], e[1], e[2] = r, r, s16(-r), s16(-r)
			e[8], e[11], e[9], e[10] = s16(r + 1), s16(r + 1), s16(-1 - r), s16(-1 - r)
		end
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

	-- FindUnitPlacement 0x47dc30: the desired point when nothing blocks it (and, on the
	-- ground, the footprint fits), else the ring search. Returns x, y or nil with the error
	-- word set. rect = { left, top, right, bottom } or nil for +-128 around the point.
	function G:find_unit_placement(n, x, y, rect, displace, extra)
		if extra then error("placement: additional point (0x4928f0)", 0) end
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
			if band(u.status[n], 4) ~= 0 or self:unit_type_fits(t, x, y) then return x, y end
			if not displace then
				self.error_code = M.ERROR_PLACEMENT
				return nil
			end
		end
		error(("placement: ring search (0x47e340) for type %d at (%d, %d)"):format(t, x, y), 0)
	end
end

return M
