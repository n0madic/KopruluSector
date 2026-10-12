-- The collision checks of a unit's prepared step (atlas P10,
-- docs/research/native-movement-collision.md): the units swept by the move (FindUnitsSwept
-- 0x4217f0), the first that blocks it (FindFirstUnitBlocker 0x42d900), and the terrain check
-- against the unit's cached safe rectangle (+0x108..+0x10e), which ContractSafeRect 0x48b9e0
-- rebuilds from the terrain contours around the unit (CheckTerrainSweepCollision 0x42da50).
-- Methods of the game.
local int = require("game.simulation.int")

local band = int.band
local s16 = int.s16
local shr = int.shr

local M = {}

M.STATUS_PATHED, M.STATUS_IGNORED = 0x100000, 0x200000
M.SPRITE_HIDDEN = 0x20
M.SAFE_RADIUS = 0x80

function M.install(G)
	-- FindUnitsSwept 0x4217f0: the units whose bounds the box of unit n crosses when moved by
	-- (dx, dy), scanned from the unit's own bound entries outwards (a unit can appear twice).
	function G:units_swept(n, dx, dy)
		local u, sp = self.u, self.spatial
		if band(self.img.spr.flags[u.sprite[n]], M.SPRITE_HIDDEN) ~= 0 then dx, dy = 0, 0 end
		local out = {}
		local xa, ya, xu, yu = sp.x_at, sp.y_at, sp.x_unit, sp.y_unit
		local function scan(at, units, k, step, limit_ok, lo, hi, cross_at, c_lo, c_hi)
			while k >= 0 and k < sp.count do
				if not limit_ok(at[k]) then break end
				local c = units[k]
				if c ~= n and lo <= cross_at[u[c_hi][c]] and cross_at[u[c_lo][c]] <= hi then out[#out + 1] = c end
				k = k + step
			end
		end
		if dx >= 1 then
			local r = s16(xa[u.bound_right[n]])
			local t, b = s16(ya[u.bound_top[n]]), s16(ya[u.bound_bottom[n]])
			local k = u.bound_right[n]
			while xa[k - 1] == r do k = k - 1 end
			local lim = s16(r + dx)
			scan(xa, xu, k, 1, function(v) return v <= lim end, s16(t + dy), s16(b + dy), ya, "bound_top", "bound_bottom")
		elseif dx < 0 then
			local l = s16(xa[u.bound_left[n]])
			local t, b = s16(ya[u.bound_top[n]]), s16(ya[u.bound_bottom[n]])
			local k = u.bound_left[n]
			while xa[k + 1] == l do k = k + 1 end
			local lim = s16(dx + l)
			scan(xa, xu, k, -1, function(v) return v >= lim end, s16(t + dy), s16(b + dy), ya, "bound_top", "bound_bottom")
		end
		if dy >= 1 then
			local b = s16(ya[u.bound_bottom[n]])
			local l, r = s16(xa[u.bound_left[n]]), s16(xa[u.bound_right[n]])
			local k = u.bound_bottom[n]
			while ya[k - 1] == b do k = k - 1 end
			local lim = s16(b + dy)
			scan(ya, yu, k, 1, function(v) return v <= lim end, s16(l + dx), s16(r + dx), xa, "bound_left", "bound_right")
		elseif dy < 0 then
			local t = s16(ya[u.bound_top[n]])
			local l, r = s16(xa[u.bound_left[n]]), s16(xa[u.bound_right[n]])
			local k = u.bound_top[n]
			while ya[k + 1] == t do k = k + 1 end
			local lim = s16(dy + t)
			scan(ya, yu, k, -1, function(v) return v >= lim end, s16(l + dx), s16(r + dx), xa, "bound_left", "bound_right")
		end
		return out
	end

	-- FindFirstUnitBlocker 0x42d900 for the prepared step (self.mv): the first swept ground
	-- unit that unit n collides with and that moves on the ground itself, or 0.
	local function first_blocker(self, n)
		local u, mv = self.u, self.mv
		local s = u.sprite[n]
		local dx = mv.next_x - s16(self.img.spr.x[s])
		local dy = mv.next_y - s16(self.img.spr.y[s])
		for _, c in ipairs(self:units_swept(n, dx, dy)) do
			if band(u.pathing_flags[c], 1) ~= 0 and band(u.status[c], M.STATUS_IGNORED) == 0 and c ~= n
				and self:units_collide(c, n) and band(u.status[c], M.STATUS_PATHED) ~= 0 then
				return c
			end
		end
		return 0
	end

	-- CheckGroundUnitCollision 0x42da30.
	function G:ground_unit_collision(n)
		if band(self.u.status[n], M.STATUS_PATHED) == 0 then return 0 end
		return first_blocker(self, n)
	end

	local function in_rect(u, n, x, y)
		return x >= u.contour_left[n] and x <= u.contour_right[n] and y >= u.contour_top[n] and y <= u.contour_bottom[n]
	end

	-- CheckTerrainOrBoundsCollision 0x42d990: true when the prepared step leaves the map (a
	-- unit without ground pathing) or crosses terrain (CheckTerrainSweepCollision 0x42da50).
	function G:terrain_collision(n)
		local u, mv, ext = self.u, self.mv, self.dat.units.unit_extents
		local t = u.type[n]
		if band(u.status[n], M.STATUS_PATHED) == 0 then
			if mv.next_x - s16(ext[t * 4]) < 0 or mv.next_y - s16(ext[t * 4 + 1]) < 0 then return true end
			if s16(ext[t * 4 + 2]) + mv.next_x >= self.map_w_px then return true end
			return s16(ext[t * 4 + 3]) + mv.next_y >= self.map_h_px
		end
		local s = u.sprite[n]
		local sx, sy = s16(self.img.spr.x[s]), s16(self.img.spr.y[s])
		local nx, ny = s16(sx + mv.next_x - sx), s16(sy + mv.next_y - sy)
		if in_rect(u, n, nx, ny) then return false end
		u.contour_top[n], u.contour_left[n] = s16(sy - M.SAFE_RADIUS), s16(sx - M.SAFE_RADIUS)
		u.contour_bottom[n], u.contour_right[n] = s16(sy + M.SAFE_RADIUS), s16(sx + M.SAFE_RADIUS)
		self:contour_extents(t)
		self:contract_safe_rect(n, sx, sy)
		if in_rect(u, n, nx, ny) then return false end
		return not self:sweep_contours_clear(sx, sy, nx, ny)
	end

	-- ContractSafeRect 0x48b9e0: unit n's safe rectangle shrinks around (x, y) until no
	-- terrain contour (with the prepared extents of the global contour context) lies inside.
	function G:contract_safe_rect(n, x, y)
		local u, c, e = self.u, self.contours, self.extent_ctx
		local left, top, right, bottom = u.contour_left[n], u.contour_top[n], u.contour_right[n], u.contour_bottom[n]
		local function first_above(a, key)
			local low, high = 0, a.count
			while low < high do
				local mid = shr(low + high, 1)
				if s16(key) >= s16(a.axis[mid] * 2) then low = mid + 1 else high = mid end
			end
			return high
		end
		local function edge(a, k)
			return s16(e[a.dir[k]] + a.axis[k]), s16(e[band(a.info[k], 3)] + a.start[k]),
				s16(e[band(shr(a.info[k], 2), 3)] + a.stop[k])
		end
		local ta, ra, ba, la = c[0], c[1], c[2], c[3]
		local tk = ta.count == 0 and 0 or first_above(ta, (y - e[0]) * 2 + 1)
		local rk = ra.count == 0 and 0 or first_above(ra, (x - e[1]) * 2 - 1)
		local bk = ba.count == 0 and 0 or first_above(ba, (y - e[2]) * 2 - 1)
		local lk = la.count == 0 and 0 or first_above(la, (x - e[3]) * 2 + 1)
		repeat
			local progress
			repeat
				progress = false
				if tk > 0 then
					progress = true
					local A, S, T = edge(ta, tk - 1)
					if A < top then
						tk = 0
					else
						tk = tk - 1
						if T >= left and S <= right then
							if x < S and y - A < S - x then
								right = S - 1
							elseif T < x and y - A < x - T then
								left = T + 1
							else
								top = A + 1
							end
						end
					end
				end
				if rk < ra.count then
					progress = true
					local A, S, T = edge(ra, rk)
					if A > right then
						rk = ra.count
					else
						rk = rk + 1
						if T >= top and S <= bottom then
							if y < S and A - x < S - y then
								bottom = S - 1
							elseif T < y and A - x < y - T then
								top = T + 1
							else
								right = A - 1
							end
						end
					end
				end
				if bk < ba.count then
					progress = true
					local A, S, T = edge(ba, bk)
					if A > bottom then
						bk = ba.count
					else
						bk = bk + 1
						if T >= left and S <= right then
							if x < S and A - y < S - x then
								right = S - 1
							elseif T < x and A - y < x - T then
								left = T + 1
							else
								bottom = A - 1
							end
						end
					end
				end
				if lk <= 0 then break end
				local A, S, T = edge(la, lk - 1)
				if A < left then
					lk = 0
				else
					lk = lk - 1
					if T >= top and S <= bottom then
						if y < S and x - A < S - y then
							bottom = S - 1
						elseif T < y and x - A < y - T then
							top = T + 1
						else
							left = A + 1
						end
					end
				end
			until false
		until not progress
		u.contour_left[n], u.contour_top[n], u.contour_right[n], u.contour_bottom[n] =
			s16(left), s16(top), s16(right), s16(bottom)
	end

	-- FindUnitsAdjacentMoving 0x421dc0: the units next to unit n within (rx, ry), scanned
	-- from its bound entries outwards (right, left, below, above).
	function G:units_adjacent(n, rx, ry)
		local u, sp = self.u, self.spatial
		if band(self.img.spr.flags[u.sprite[n]], M.SPRITE_HIDDEN) ~= 0 then return {} end
		local xa, ya, xu, yu = sp.x_at, sp.y_at, sp.x_unit, sp.y_unit
		local out = {}
		local l, r = s16(xa[u.bound_left[n]]), s16(xa[u.bound_right[n]])
		local t, b = s16(ya[u.bound_top[n]]), s16(ya[u.bound_bottom[n]])
		local function y_overlap(c)
			return s16(t - ry) <= ya[u.bound_bottom[c]] and ya[u.bound_top[c]] < s16(b + ry)
		end
		local function x_overlap(c)
			return l <= xa[u.bound_right[c]] and xa[u.bound_left[c]] < r
		end
		for k = u.bound_right[n] + 1, sp.count - 1 do
			if s16(rx + r) <= xa[k] then break end
			local c = xu[k]
			if c ~= n and y_overlap(c) then out[#out + 1] = c end
		end
		for k = u.bound_left[n] - 1, 0, -1 do
			if xa[k] < s16(l - rx) then break end
			local c = xu[k]
			if c ~= n and y_overlap(c) then out[#out + 1] = c end
		end
		for k = u.bound_bottom[n] + 1, sp.count - 1 do
			if s16(ry + b) <= ya[k] then break end
			local c = yu[k]
			if c ~= n and x_overlap(c) then out[#out + 1] = c end
		end
		for k = u.bound_top[n] - 1, 0, -1 do
			if ya[k] < s16(t - ry) then break end
			local c = yu[k]
			if c ~= n and x_overlap(c) then out[#out + 1] = c end
		end
		return out
	end

	-- CollectMovingNeighbors 0x42e1c0: up to `max` moving ground units next to unit n that it
	-- collides with.
	function G:moving_neighbors(n, radius, max)
		local u = self.u
		local out = {}
		for _, c in ipairs(self:units_adjacent(n, radius, radius)) do
			local s = u.sprite[c]
			local at_rest = u.move_x[c] == s16(self.img.spr.x[s]) and u.move_y[c] == s16(self.img.spr.y[s])
			if not at_rest and band(u.status[c], M.STATUS_PATHED) ~= 0 and band(u.pathing_flags[c], 1) ~= 0
				and band(u.status[c], M.STATUS_IGNORED) == 0 and (n == 0 or (c ~= n and self:units_collide(c, n))) then
				out[#out + 1] = c
				if #out == max then break end
			end
		end
		return out
	end

	-- SweepContoursClear 0x48bec0: no terrain contour (with the prepared extents of the global
	-- contour context) crosses the step from (x1, y1) to (x2, y2). A contour whose span covers
	-- the step's span blocks it unless its corner bits (info 0x10, 0x20) open both ends past it.
	function G:sweep_contours_clear(x1, y1, x2, y2)
		local c, e = self.contours, self.extent_ctx
		local xlo, xhi = x1, x2
		if x1 >= x2 then xlo, xhi = x2, x1 end
		local ylo, yhi = y2, y1
		if y1 < y2 then ylo, yhi = y1, y2 end
		local function span(a, k)
			return s16(e[a.dir[k]] + a.axis[k]), s16(e[band(a.info[k], 3)] + a.start[k]),
				s16(e[band(shr(a.info[k], 2), 3)] + a.stop[k]), a.info[k]
		end
		local function blocks(lo, hi, info, a, b)
			if lo > b or a > hi then return false end
			return (lo < a and b < hi) or band(info, 0x10) == 0 or lo <= a or band(info, 0x20) == 0 or b <= hi
		end
		local function first_above(a, key)
			local low, high = 0, a.count
			while low < high do
				local mid = shr(low + high, 1)
				if s16(key) >= s16(a.axis[mid] * 2) then low = mid + 1 else high = mid end
			end
			return high
		end
		if y2 < y1 then
			local a = c[0]
			for k = first_above(a, (y1 - e[0]) * 2 + 1) - 1, 0, -1 do
				local axis, lo, hi, info = span(a, k)
				if axis < y2 then break end
				if blocks(lo, hi, info, xlo, xhi) then return false end
			end
		elseif y1 < y2 then
			local a = c[2]
			for k = first_above(a, (y1 - e[2]) * 2 - 1), a.count - 1 do
				local axis, lo, hi, info = span(a, k)
				if axis > y2 then break end
				if blocks(lo, hi, info, xlo, xhi) then return false end
			end
		end
		if x2 < x1 then
			local a = c[3]
			for k = first_above(a, (x1 - e[3]) * 2 + 1) - 1, 0, -1 do
				local axis, lo, hi, info = span(a, k)
				if axis < x2 then return true end
				if blocks(lo, hi, info, ylo, yhi) then return false end
			end
		elseif x1 < x2 then
			local a = c[1]
			for k = first_above(a, (x1 - e[1]) * 2 - 1), a.count - 1 do
				local axis, lo, hi, info = span(a, k)
				if axis > x2 then break end
				if blocks(lo, hi, info, ylo, yhi) then return false end
			end
		end
		return true
	end
end

return M
