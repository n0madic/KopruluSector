-- Unit spatial index: two arrays of unit bounds sorted by coordinate (x: 0x5c99b8, y:
-- 0x5d03f8), each entry { unit, coordinate }; count 0x5c99b0 is the entry count of each array
-- (two bounds per indexed unit). A unit keeps the positions of its four bounds (+0x13c left,
-- +0x140 right, +0x144 top, +0x148 bottom; -1 while not indexed). Methods of the game
-- (game/simulation/game.lua); docs/research/native-movement-collision.md and
-- native-unit-creation.md describe the contracts, the decompiled functions the details.
--
-- Entries are stored 0-based like the native arrays: sx_unit[k], sx_at[k] for k in
-- 0..count-1. Unused entries hold -1 in both fields (the memset of 0x419560).
local int = require("game.simulation.int")

local s16 = int.s16

local M = {}

M.CAPACITY = 0x6a40 / 8 -- entries of each array (1700 units * 2 bounds)
M.SUBUNIT = 0x10 -- units.dat special ability flag: subunits are never indexed

-- Lower bound of 0x419500 and of the inline searches of 0x4195a0: the first entry whose
-- coordinate is >= v (inclusive) or > v (exclusive), over entries 0..count-1.
local function search(at, count, v, inclusive)
	local key = inclusive and v * 2 - 1 or v * 2 + 1
	local low, high, sum = 0, count, count
	while low < high do
		local mid = math.floor(sum / 2)
		if key < at[mid] * 2 then
			sum, high = mid + low, mid
		else
			low, sum = mid + 1, mid + 1 + high
		end
	end
	return high
end
M.search = search

function M.install(G)
	-- ClearSpatialIndex 0x419560 (with InitUnitFinder 0x421be0's query state and maximum
	-- unit extents).
	function G:spatial_clear()
		local sp = { x_unit = {}, x_at = {}, y_unit = {}, y_at = {}, count = 0 }
		self.spatial = sp
		for k = 0, M.CAPACITY - 1 do
			sp.x_unit[k], sp.x_at[k], sp.y_unit[k], sp.y_at[k] = -1, -1, -1, -1
		end
		local ext = self.dat.units.unit_extents
		local w, h = 0, 0
		for t = 0, 227 do
			local width = s16(ext[t * 4]) + s16(ext[t * 4 + 2]) + 1
			local height = s16(ext[t * 4 + 3]) + s16(ext[t * 4 + 1]) + 1
			if w < width then w = width % 65536 end
			if h < height then h = height % 65536 end
		end
		sp.max_w, sp.max_h = w, h
		sp.mark = {}
	end

	-- OpenXEntry 0x419e20 / OpenYEntry 0x419eb0: shift entries at..count-1 one up; the
	-- displaced units' bound positions follow (first matching field only).
	local function open(self, units_of, at_of, lo_field, hi_field, at, count)
		local u = self.u
		local lo, hi = u[lo_field], u[hi_field]
		for k = count - 1, at, -1 do
			local n = units_of[k]
			if lo[n] == k then lo[n] = k + 1 elseif hi[n] == k then hi[n] = k + 1 end
			units_of[k + 1], at_of[k + 1] = n, at_of[k]
		end
	end

	-- InsertUnitIntoSpatialIndex 0x4195a0: left and top bounds, then right and bottom, each
	-- before the entries with an equal coordinate.
	function G:spatial_insert(n)
		local u, sp, d = self.u, self.spatial, self.dat.units
		local t = u.type[n]
		if int.band(d.special_ability_flags[t], M.SUBUNIT) ~= 0 then return end
		local ext = d.unit_extents
		local s = u.sprite[n]
		local x, y = s16(self.img.spr.x[s]), s16(self.img.spr.y[s])
		local left, top = x - s16(ext[t * 4]), y - s16(ext[t * 4 + 1])
		local at = search(sp.x_at, sp.count, left, true)
		open(self, sp.x_unit, sp.x_at, "bound_left", "bound_right", at, sp.count)
		u.bound_left[n], sp.x_unit[at], sp.x_at[at] = at, n, left
		at = search(sp.y_at, sp.count, top, true)
		open(self, sp.y_unit, sp.y_at, "bound_top", "bound_bottom", at, sp.count)
		u.bound_top[n], sp.y_unit[at], sp.y_at[at] = at, n, top
		sp.count = sp.count + 1
		local right, bottom = s16(ext[t * 4 + 2]) + x, s16(ext[t * 4 + 3]) + y
		at = search(sp.x_at, sp.count, right, true)
		open(self, sp.x_unit, sp.x_at, "bound_left", "bound_right", at, sp.count)
		u.bound_right[n], sp.x_unit[at], sp.x_at[at] = at, n, right
		at = search(sp.y_at, sp.count, bottom, true)
		open(self, sp.y_unit, sp.y_at, "bound_top", "bound_bottom", at, sp.count)
		u.bound_bottom[n], sp.y_unit[at], sp.y_at[at] = at, n, bottom
		sp.count = sp.count + 1
	end

	-- RemoveXEntry 0x419d00 / RemoveYEntry 0x419d90: entries at+1..last move down one; the
	-- displaced units' bound positions follow (first matching field only).
	local function close(self, units_of, at_of, lo_field, hi_field, at, last)
		local u = self.u
		local lo, hi = u[lo_field], u[hi_field]
		for k = at + 1, last do
			local n = units_of[k]
			if lo[n] == k then lo[n] = k - 1 elseif hi[n] == k then hi[n] = k - 1 end
		end
		for k = at, last - 1 do units_of[k], at_of[k] = units_of[k + 1], at_of[k + 1] end
	end

	-- RemoveUnitFromSpatialIndex 0x419460: right and bottom bounds, then left and top.
	function G:spatial_remove(n)
		local u, sp = self.u, self.spatial
		if u.bound_left[n] < 0 then return end
		sp.count = sp.count - 1
		close(self, sp.x_unit, sp.x_at, "bound_left", "bound_right", u.bound_right[n], sp.count)
		close(self, sp.y_unit, sp.y_at, "bound_top", "bound_bottom", u.bound_bottom[n], sp.count)
		u.bound_right[n], u.bound_bottom[n] = -1, -1
		sp.count = sp.count - 1
		close(self, sp.x_unit, sp.x_at, "bound_left", "bound_right", u.bound_left[n], sp.count)
		close(self, sp.y_unit, sp.y_at, "bound_top", "bound_bottom", u.bound_top[n], sp.count)
		u.bound_left[n], u.bound_top[n] = -1, -1
	end

	-- Swap of entries k and k + step (0x41a090 for x, 0x41a110 for y): the unit at k has its
	-- matching bound field moved by step.
	local function swap(self, units_of, at_of, lo_field, hi_field, k, step)
		local u = self.u
		local n = units_of[k]
		if u[lo_field][n] == k then
			u[lo_field][n] = k + step
		elseif u[hi_field][n] == k then
			u[hi_field][n] = k + step
		end
		units_of[k], units_of[k + step] = units_of[k + step], units_of[k]
		at_of[k], at_of[k + step] = at_of[k + step], at_of[k]
	end

	-- Moves bound `field` of unit n up while the next entry's coordinate is below v (down
	-- while the previous one is above v); the passed unit's bound field steps the other way
	-- (the inline loops of 0x419820).
	local function bubble_up(self, units_of, at_of, lo_field, hi_field, field, n, v)
		local u, sp = self.u, self.spatial
		while u[field][n] + 1 < sp.count do
			local k = u[field][n]
			if v <= at_of[k + 1] then break end
			swap(self, units_of, at_of, lo_field, hi_field, k + 1, -1)
			u[field][n] = k + 1
		end
	end

	local function bubble_down(self, units_of, at_of, lo_field, hi_field, field, n, v)
		local u = self.u
		while u[field][n] > 0 and v < at_of[u[field][n] - 1] do
			local k = u[field][n]
			swap(self, units_of, at_of, lo_field, hi_field, k - 1, 1)
			u[field][n] = k - 1
		end
	end

	-- MoveUnitInSpatialIndex 0x419820: shifts the four bounds by (dx, dy) and restores the
	-- order by adjacent swaps (right bound first when moving right, left bound first when
	-- moving left; the same for y).
	function G:spatial_move(n, dx, dy)
		local u, sp = self.u, self.spatial
		if u.bound_left[n] < 0 or (dx == 0 and dy == 0) then return end
		local xa, ya = sp.x_at, sp.y_at
		local left = xa[u.bound_left[n]] + dx
		xa[u.bound_left[n]] = left
		local right = xa[u.bound_right[n]] + dx
		xa[u.bound_right[n]] = right
		local top = ya[u.bound_top[n]] + dy
		ya[u.bound_top[n]] = top
		local bottom = ya[u.bound_bottom[n]] + dy
		ya[u.bound_bottom[n]] = bottom
		if dx > 0 then
			bubble_up(self, sp.x_unit, xa, "bound_left", "bound_right", "bound_right", n, right)
			bubble_up(self, sp.x_unit, xa, "bound_left", "bound_right", "bound_left", n, left)
		elseif dx < 0 then
			bubble_down(self, sp.x_unit, xa, "bound_left", "bound_right", "bound_left", n, left)
			bubble_down(self, sp.x_unit, xa, "bound_left", "bound_right", "bound_right", n, right)
		end
		if dy > 0 then
			bubble_up(self, sp.y_unit, ya, "bound_top", "bound_bottom", "bound_bottom", n, bottom)
			bubble_up(self, sp.y_unit, ya, "bound_top", "bound_bottom", "bound_top", n, top)
		elseif dy < 0 then
			bubble_down(self, sp.y_unit, ya, "bound_top", "bound_bottom", "bound_top", n, top)
			bubble_down(self, sp.y_unit, ya, "bound_top", "bound_bottom", "bound_bottom", n, bottom)
		end
	end

	-- QueryUnitsOverlappingRect 0x4224c0 (with its release 0x4222b0): units with a bound in
	-- the rectangle's x range (widened to the largest unit width when narrower) whose left
	-- edge is not past the right side, and a bound in the y range likewise, in x-array order.
	-- left, top, right, bottom are signed WORDs. Returns a new array of units.
	function G:spatial_query(left, top, right, bottom)
		local sp, u, ext = self.spatial, self.u, self.dat.units.unit_extents
		local spr_x, spr_y = self.img.spr.x, self.img.spr.y
		local narrow_x, narrow_y = false, false
		local right_search, bottom_search = right % 65536, bottom % 65536
		if right - left + 1 < sp.max_w then
			narrow_x, right_search = true, (sp.max_w - 1 + left) % 65536
		end
		if bottom - top + 1 < sp.max_h then
			narrow_y, bottom_search = true, (sp.max_h - 1 + top) % 65536
		end
		local x0 = search(sp.x_at, sp.count, left, true)
		local x1 = search(sp.x_at, sp.count, right_search, true)
		local y0 = search(sp.y_at, sp.count, top, true)
		local y1 = search(sp.y_at, sp.count, bottom_search, true)
		local out, mark = {}, sp.mark
		if x1 - x0 <= 0 or y1 - y0 <= 0 then return out end
		for k = x0, x1 - 1 do
			local n = sp.x_unit[k]
			if narrow_x and s16(spr_x[u.sprite[n]]) - s16(ext[u.type[n] * 4]) > right then
				mark[n] = 0
			else
				mark[n] = 1
			end
		end
		for k = y0, y1 - 1 do
			local n = sp.y_unit[k]
			if not (narrow_y and s16(spr_y[u.sprite[n]]) - s16(ext[u.type[n] * 4 + 1]) > bottom) then
				if mark[n] == 1 then mark[n] = 3 end
			end
		end
		for k = x0, x1 - 1 do
			local n = sp.x_unit[k]
			if mark[n] == 3 then out[#out + 1] = n end
			mark[n] = 0
		end
		return out
	end

	-- UnitBoundsOverlap 0x419f40 (inclusive, by the indexed bounds).
	function G:spatial_overlap(a, b)
		local u, xa, ya = self.u, self.spatial.x_at, self.spatial.y_at
		return xa[u.bound_left[b]] <= xa[u.bound_right[a]] and xa[u.bound_left[a]] <= xa[u.bound_right[b]]
			and ya[u.bound_top[b]] <= ya[u.bound_bottom[a]] and ya[u.bound_top[a]] <= ya[u.bound_bottom[b]]
	end

	-- Placement rectangle of unit n's type around its sprite (the DAT extents).
	function G:unit_rect(n)
		local t, s = self.u.type[n], self.u.sprite[n]
		local ext = self.dat.units.unit_extents
		local x, y = s16(self.img.spr.x[s]), s16(self.img.spr.y[s])
		return s16(x - ext[t * 4]), s16(y - ext[t * 4 + 1]), s16(x + ext[t * 4 + 2]), s16(y + ext[t * 4 + 3])
	end

	-- UpdateOverlapCollisionFlags 0x42e600 with OverlapCollisionCallback 0x42e670: for each
	-- unit c in n's rectangle: a grounded building c marks n (0x40000); otherwise a ground
	-- c (not 0x800000 unless n is a building) overlapping n marks c.
	function G:update_overlap_flags(n)
		local u = self.u
		local status = u.status
		for _, c in ipairs(self:spatial_query(self:unit_rect(n))) do
			local cs = status[c]
			if int.band(cs, 2) ~= 0 then
				status[n] = int.bor(status[n], 0x40000)
			elseif int.band(cs, 4) == 0 and (int.band(cs, 0x800000) == 0 or int.band(status[n], 2) ~= 0) then
				if self:spatial_overlap(c, n) then status[c] = int.bor(cs, 0x40000) end
			end
		end
	end
end

return M
