-- A unit's route (atlas P30; docs/research/native-local-paths.md, native-path-progress.md):
-- the search context of NextPathPoint 0x48ec50 (0x16c bytes on its stack), its initialization
-- (InitUnitPathSearch 0x491210, ContinueLocalPath 0x4919b0), the segment builder
-- (BuildLocalPathSegment 0x491c10), the packed record (PackPathRecord 0x48e730,
-- UnpackPathRecord 0x48e7e0, SetDirectPath 0x48ef10) and the waypoint consumers
-- (CanKeepCurrentWaypoint 0x48ee40, AdvancePathWaypoint 0x48eb90). Methods of the game.
local directions = require("game.data.directions")
local int = require("game.simulation.int")
local local_search = require("game.pathing.local_search")
local pool = require("game.pathing.pool")

local band, bor = int.band, int.bor
local bit_band = int.bit_band
local s16 = int.s16

local M = {}

M.STATUS_PATHED = 0x100000 -- ground units find paths; others fly straight (0x48eb90)
M.STATUS_INTERCEPT = 0x80000
M.FLAG_PATHING, M.FLAG_NEAR_GOAL = 0x10, 0x40 -- movement flags +0x20
M.NEAR_GOAL = 0x10
M.KEEP_DISTANCE = 0x10
M.UNTARGETED_FIRST, M.UNTARGETED_LAST = 0xc2, 0xc7 -- 0x421620: types never a route's target

-- A new search context; the fields carry their native offsets.
local function new_context(unit, sx, sy, dx, dy)
	return {
		unit = unit, src_x = sx, src_y = sy, dest_x = dx, dest_y = dy, -- +0, +4, +8
		target = 0, box_left = 0, box_top = 0, box_right = 0, box_bottom = 0, -- +0xc, +0x10
		goal_x = 0, goal_y = 0, -- +0x18
		partial = 0, local_partial = 0, reached = 0, reached_end = 0, -- +0x1c..+0x1f
		regions = {}, region_cursor = 0, region_count = 0, -- +0x20, +0x84, +0x86
		points = {}, point_index = 0, point_count = 0, full_count = 0, -- +0x88, +0x150, +0x152, +0x154
		src_region = 0, dst_region = 0, -- +0x160, +0x162
		long_nodes = 0, local_nodes = 0, long_peak = 0, local_peak = 0, -- +0x164..+0x16a
	}
end
M.new_context = new_context

-- The spiral steps of 0x492c00 and 0x492a90 (table 0x509400: x, then 0x509408: y).
local DX, DY = { [0] = 1, 0, -1, 0 }, { [0] = 0, 1, 0, -1 }

function M.install(G)
	local function sprite_pos(self, n)
		local s = self.u.sprite[n]
		return s16(self.img.spr.x[s]), s16(self.img.spr.y[s])
	end

	-- The fields both initializers set: regions of the end points, goal, flags, the type's
	-- extents (+0x158: -left, -up, right, down) and the counters.
	local function prepare(self, c)
		c.src_region = self:region_at(c.src_x, c.src_y)
		c.dst_region = self:region_at(c.dest_x, c.dest_y)
		c.goal_x, c.goal_y = c.dest_x, c.dest_y
		c.partial, c.local_partial, c.reached, c.reached_end = 0, 0, 0, 0
		local ext, t = self.dat.units.unit_extents, self.u.type[c.unit]
		c.ext_left, c.ext_up = s16(-ext[t * 4]), s16(-ext[t * 4 + 1])
		c.ext_right, c.ext_down = s16(ext[t * 4 + 2]), s16(ext[t * 4 + 3])
		c.long_nodes, c.local_nodes, c.long_peak, c.local_peak = 0, 0, 0, 0
	end

	-- InitUnitPathSearch 0x491210.
	function G:path_search_init(c)
		prepare(self, c)
		if c.src_region == c.dst_region then
			c.regions[0] = c.src_region
			c.region_cursor, c.region_count, c.full_count = -1, 1, 1
			return self:path_segment(c)
		end
		if not self:search_region_path(c) then return false end
		return self:path_segment(c)
	end

	-- ContinueLocalPath 0x4919b0: the next segment of a stored route.
	function G:path_search_continue(c)
		if c.src_x == c.dest_x and c.src_y == c.dest_y then return false end
		prepare(self, c)
		return self:path_segment(c)
	end

	-- FindUnitsAtPoint 0x421c80: the units whose box, grown by unit n's, contains (x, y).
	function G:units_at_point(n, x, y)
		local sp, u, ext = self.spatial, self.u, self.dat.units.unit_extents
		local hw, hh = int.shr(sp.max_w, 1), int.shr(sp.max_h, 1)
		local t = u.type[n]
		local out = {}
		for _, c in ipairs(self:units_in_rect(s16(x - hw - 1), s16(y - hh - 1), s16(x + 1 + hw), s16(y + 1 + hh))) do
			local cx, cy = sprite_pos(self, c)
			local ct = u.type[c]
			local l = s16(cx - ext[ct * 4] + (-1 - ext[t * 4 + 2]))
			local r = s16(cx + ext[ct * 4 + 2] + ext[t * 4] + 1)
			local tp = s16(cy - ext[ct * 4 + 1] + (-1 - ext[t * 4 + 3]))
			local b = s16(cy + ext[ct * 4 + 3] + ext[t * 4 + 1] + 1)
			if l <= x and x <= r and tp <= y and y <= b then out[#out + 1] = c end
		end
		return out
	end

	-- FindFootprintFitNearPoint 0x492c00: a spiral of 8-pixel steps (rings 1, 3, .., 9)
	-- around (x, y) for a spot where the type's box covers no unwalkable region tile.
	function G:footprint_near(c, x, y)
		local map_w, map_h = self.map_w_px, self.map_h_px
		local k = 1
		while k < 10 do
			for d = 0, 3 do
				for _ = 1, k do
					local x0, y0 = s16(x + c.ext_left), s16(y + c.ext_up)
					local x1, y1 = s16(x + c.ext_right), s16(y + c.ext_down)
					if x0 % 65536 < map_w and y0 % 65536 < map_h and x1 % 65536 < map_w and y1 % 65536 < map_h then
						local fits = true
						for ty = int.idiv(y0, 32), int.idiv(y1, 32) do
							for tx = int.idiv(x1, 32), int.idiv(x0, 32), -1 do
								if self:region_inaccessible(self.region_cell(self.regions, tx, ty)) then
									fits = false
									break
								end
							end
							if not fits then break end
						end
						if fits then return true, x, y end
					end
					x, y = s16(x + DX[d] * 8), s16(y + DY[d] * 8)
				end
				x, y = s16(x - DX[d] * 8), s16(y - DY[d] * 8)
			end
			x, y = s16(x - 8), s16(y - 8)
			k = k + 2
		end
		return false, x, y
	end

	-- BuildLocalPathSegment 0x491c10: the local points from the source towards the end of
	-- the region route (or two regions ahead).
	function G:path_segment(c)
		local count = c.region_count
		local cursor = s16(c.region_cursor + 1)
		c.region_cursor = cursor
		if cursor >= count then return false end
		local src = c.src_region
		if c.regions[cursor] ~= src then
			local k = cursor
			while k < count and c.regions[k] ~= src do k = k + 1 end
			if k >= count then return false end
			c.region_cursor = k
			cursor = k
		end
		local rem = count - cursor
		local final, final_point
		local px, py
		if rem == 1 or rem == 2 or rem <= 3 then
			px, py = c.goal_x, c.goal_y
			final, final_point = true, true
		else
			local r = c.regions[cursor + 2] -- GetRegionCenter 0x481a60
			px, py = s16(int.sar(self.rg.center_x[r], 8)), s16(int.sar(self.rg.center_y[r], 8))
			final, final_point = false, false
		end
		local next_region
		if cursor == count - 1 then
			if src ~= c.dst_region then
				if not self:snap_goal_into_region(c, src) then
					error("route: error in GetClosestReachable (0x4cc190)", 0)
				end
				final, final_point = true, true
			end
			next_region = local_search.FINAL
		else
			next_region = c.regions[cursor + 1]
		end
		c.target = 0
		if final then
			local u = self.u
			c.target = u.move_unit[c.unit]
			if c.target == 0 then
				for _, cand in ipairs(self:units_at_point(c.unit, c.goal_x, c.goal_y)) do
					if self:path_obstacle(cand, c.unit) then
						c.target = cand
						break
					end
				end
			end
			local tt = c.target ~= 0 and u.type[c.target]
			if tt and tt >= M.UNTARGETED_FIRST and tt <= M.UNTARGETED_LAST then
				c.target = 0
			end
			if c.target == 0 then
				c.box_left, c.box_top, c.box_right, c.box_bottom = local_search.NO_POINT, local_search.NO_POINT,
					local_search.NO_POINT, local_search.NO_POINT
				local ok, fx, fy = self:footprint_near(c, px, py)
				if ok and (fx ~= px or fy ~= py) then
					px, py = fx, fy
					c.goal_x, c.goal_y = fx, fy
				end
			else
				local ext = self.dat.units.unit_extents
				local tt, mt = u.type[c.target], u.type[c.unit]
				local tx, ty = sprite_pos(self, c.target)
				c.box_left = s16(tx - ext[tt * 4] + (-1 - ext[mt * 4 + 2]))
				c.box_right = s16(tx + ext[tt * 4 + 2] + ext[mt * 4] + 1)
				c.box_top = s16(ty - ext[tt * 4 + 1] + (-1 - ext[mt * 4 + 3]))
				c.box_bottom = s16(ty + ext[tt * 4 + 3] + ext[mt * 4 + 1] + 1)
				local sx, sy = c.src_x, c.src_y
				local on_x = sx == c.box_left or sx == c.box_right
				local on_y = sy == c.box_top or sy == c.box_bottom
				if (on_x and c.box_top <= sy and sy <= c.box_bottom)
					or (on_y and c.box_left <= sx and sx <= c.box_right) then
					c.points[0] = { sx, sy }
					c.goal_x, c.goal_y = sx, sy
					c.point_index, c.point_count = 0, 1
					c.reached, c.reached_end = 1, 0
					return true
				end
			end
		end
		local ok = self:local_path_search(c, next_region, px, py)
		if ok then
			local last = c.points[c.point_count - 1]
			if c.reached ~= 0 then
				if not final_point and last[1] == px and last[2] == py then
					c.reached = 0
					return ok
				end
			else
				if next_region ~= local_search.FINAL then return ok end
				c.reached, c.reached_end = 1, 1
			end
			c.goal_x, c.goal_y = last[1], last[2]
		end
		return ok
	end

	-- SnapGoalIntoRegion 0x492a90: the goal clamped into the region's bounds, then the centre
	-- of the first tile of the region on a spiral of tiles (rings 1, 3, .., 15) around it, or
	-- the region's centre.
	function G:snap_goal_into_region(c, region)
		local left, top, right, bottom = self:region_box(region)
		local gx, gy = c.goal_x, c.goal_y
		local y = top <= gy and (bottom < gy and bottom or gy) or top
		local x = left <= gx and (right < gx and right or gx) or left
		local tx, ty = s16(int.idiv(x, 32)), s16(int.idiv(y, 32))
		local k = 1
		while k < 16 do
			for d = 0, 3 do
				for _ = 1, k do
					if tx % 65536 < self.map_w and ty % 65536 < self.map_h
						and self:tile_region_first(tx * 32, ty * 32) == region then
						c.goal_x, c.goal_y = s16(tx * 32 + 16), s16(ty * 32 + 16)
						c.reached_end = 1
						return true
					end
					tx, ty = s16(tx + DX[d]), s16(ty + DY[d])
				end
				tx, ty = s16(tx - DX[d]), s16(ty - DY[d])
			end
			tx, ty = s16(tx - 1), s16(ty - 1)
			k = k + 2
		end
		c.goal_x = s16(int.sar(self.rg.center_x[region], 8))
		c.goal_y = s16(int.sar(self.rg.center_y[region], 8))
		c.reached_end = 1
		return true
	end

	-- PackPathRecord 0x48e730 (with ClampPathSizeAndAllocate 0x42c840, which shortens the
	-- context's counts first): the record or 0.
	local function pack(self, c)
		local k, regions, points = self:path_alloc_counts(c.region_count, c.point_count)
		c.region_count, c.point_count = regions, points
		if k == 0 then return 0 end
		self:path_w8(k, pool.RETRY, 0)
		self:path_w32(k, pool.FRAME, self.frame)
		self:path_w8(k, pool.FLAGS, 0)
		self:path_w8(k, pool.POINTS, points)
		self:path_w8(k, pool.POINT_INDEX, 0)
		self:path_w8(k, pool.REGIONS, regions)
		self:path_w8(k, pool.FULL_REGIONS, c.full_count)
		local cursor = int.s8(c.region_cursor % 256)
		local stored = int.s8(regions % 256)
		self:path_w8(k, pool.REGION_INDEX, stored < cursor and stored or cursor)
		local s = int.s8(points % 256)
		for i = 0, s - 1 do self:path_set_point(k, pool.PAYLOAD + 4 * i, c.points[i][1], c.points[i][2]) end
		for i = 0, int.s8(regions % 256) - 1 do self:path_w16(k, pool.PAYLOAD + 4 * s + 2 * i, c.regions[i]) end
		return k
	end

	-- UnpackPathRecord 0x48e7e0: record k of unit n into a new context.
	local function unpack(self, n, k)
		local sx, sy = sprite_pos(self, n)
		local dx, dy = self:path_point(k, pool.DEST)
		local c = new_context(n, sx, sy, dx, dy)
		local s = self:path_s8(k, pool.POINTS)
		for i = 0, s - 1 do c.points[i] = { self:path_point(k, pool.PAYLOAD + 4 * i) } end
		c.point_count = s
		c.point_index = self:path_s8(k, pool.POINT_INDEX)
		local l = self:path_s8(k, pool.REGIONS)
		for i = 0, l - 1 do c.regions[i] = self:path_u16(k, pool.PAYLOAD + 4 * s + 2 * i) end
		c.region_count = l
		c.region_cursor = self:path_s8(k, pool.REGION_INDEX)
		c.full_count = self:path_u8(k, pool.FULL_REGIONS)
		return c
	end

	-- SetDirectPath 0x48ef10: unit n's path becomes the one point (nx, ny) towards (dx, dy).
	function G:path_set_direct(n, nx, ny, dx, dy)
		local u = self.u
		if u.path[n] ~= 0 then self:path_release(u.path[n]) end
		local k = self.path_free_head
		if k ~= 0 then self.path_free_head = self:path_u32(k, 0) end
		u.path[n] = k
		if k == 0 then return end
		self:path_w8(k, pool.REGIONS, 1)
		self:path_w8(k, pool.REGION_INDEX, 0)
		self:path_w8(k, pool.POINTS, 1)
		self:path_w8(k, pool.POINT_INDEX, 0)
		self:path_w8(k, pool.FULL_REGIONS, 1)
		self:path_set_point(k, pool.SRC, sprite_pos(self, n))
		self:path_set_point(k, pool.NEXT, nx, ny)
		self:path_set_point(k, pool.DEST, dx, dy)
		self:path_w16(k, pool.FLAGS, 0)
		self:path_set_point(k, pool.PAYLOAD, nx, ny)
	end

	-- NextPathPoint 0x48ec50: the next point of unit n's path towards (tx, ty), building or
	-- continuing the route when its points are used up. Returns ok, x, y.
	function G:next_path_point(n, tx, ty)
		local u = self.u
		u.movement_flags[n] = bor(band(u.movement_flags[n], 0xbf), M.FLAG_PATHING)
		local k = u.path[n]
		local built = false
		if k ~= 0 then
			self:path_w8(k, pool.POINT_INDEX, self:path_u8(k, pool.POINT_INDEX) + 1)
			if self:path_s8(k, pool.POINT_INDEX) >= self:path_s8(k, pool.POINTS) then
				local c = unpack(self, n, k)
				self:path_free(n)
				local cursor, count = c.region_cursor, c.region_count
				if not (count <= cursor + 1 or (c.full_count ~= count and count <= cursor + 4)) then
					local ok = self:path_search_continue(c)
					if ok and c.region_count ~= 0 and c.point_count ~= 0 then
						built = c
					end
				end
				k = 0
			end
		end
		local c = built
		if k == 0 and not c then
			local sx, sy = sprite_pos(self, n)
			c = new_context(n, sx, sy, tx, ty)
			if not self:path_search_init(c) then return false end
			if c.region_count == 0 or c.point_count == 0 then return false end
		end
		if c then
			if c.reached ~= 0 and (c.goal_x ~= tx or c.goal_y ~= ty) then
				tx, ty = c.goal_x, c.goal_y
				self:move_to_intercept(n, tx, ty)
				u.status[n] = int.bxor(u.status[n], band(int.bxor(int.shl(c.reached_end, 19), u.status[n]),
					M.STATUS_INTERCEPT))
			end
			k = pack(self, c)
			u.path[n] = k
			if k == 0 then return false end
			self:path_set_point(k, pool.SRC, sprite_pos(self, n))
			self:path_set_point(k, pool.DEST, tx, ty)
		end
		local i = self:path_s8(k, pool.POINT_INDEX)
		local px, py = self:path_point(k, pool.PAYLOAD + 4 * i)
		self:path_set_point(k, pool.NEXT, px, py)
		if px == u.move_x[n] and py == u.move_y[n] then
			u.movement_flags[n] = band(u.movement_flags[n], 0xef)
			return true, px, py
		end
		local dx, dy = self:path_point(k, pool.DEST)
		if self:approx_distance(px, py, dx, dy) < M.NEAR_GOAL then
			u.movement_flags[n] = bor(u.movement_flags[n], M.FLAG_NEAR_GOAL)
		end
		return true, px, py
	end

	-- MoveToInterceptMoveTarget 0x420db0: the move target becomes (x, y), or, for a moving
	-- target unit, the point three of the unit's own halt distances ahead of the target in its
	-- direction (DirectionVector 0x40c270) when that lies inside the map; the target unit stays.
	function G:move_to_intercept(n, x, y)
		local u = self.u
		local t = u.move_unit[n]
		if t ~= 0 and bit_band(u.movement_flags[t], 2) ~= 0 then
			local dir, dist = u.velocity_direction[t], self:flingy_halt_distance(n) * 3
			local s = u.sprite[t]
			local ax = s16(self.img.spr.x[s]) + int.sar(int.sar(int.mul(directions.x[dir], dist), 8), 8)
			local ay = s16(self.img.spr.y[s]) + int.sar(int.sar(int.mul(directions.y[dir], dist), 8), 8)
			if ax % 4294967296 < self.map_w_px and ay % 4294967296 < self.map_h_px then x, y = ax, ay end
		end
		self:flingy_set_move_target(n, x, y)
		u.move_unit[n] = t
	end

	-- HeadingDifference 0x40b990.
	local function heading_difference(a, b)
		local d = band(b - a, 0xff)
		if d > 0x80 then return 0x100 - d end
		return d
	end

	-- CanKeepCurrentWaypoint 0x48ee40.
	function G:path_keeps_waypoint(n)
		local u = self.u
		local k = u.path[n]
		if k == 0 then return false end
		local sx, sy = sprite_pos(self, n)
		local nx, ny = self:path_point(k, pool.NEXT)
		if sx == nx and sy == ny then return false end
		if nx == u.move_x[n] and ny == u.move_y[n] then return true end
		if u.movement_type[n] ~= 0 then return true end
		local d = self:approx_distance(sx, sy, nx, ny)
		if d > M.KEEP_DISTANCE then return true end
		if d * 2 >= heading_difference(u.velocity_direction[n], u.velocity_direction2[n]) then return true end
		local s = self:path_s8(k, pool.POINTS)
		if self:path_s8(k, pool.POINT_INDEX) < s - 1 then return false end
		local r = self:path_u16(k, pool.PAYLOAD + 4 * s + 2 * self:path_s8(k, pool.REGION_INDEX))
		return self:region_at(sx, sy) ~= r
	end

	-- SetPathWaypoint 0x40cd30 (through SetFacingWaypointAndNext 0x43a260).
	function G:set_path_waypoint(n, x, y)
		local u = self.u
		if x ~= u.target_waypoint_x[n] or y ~= u.target_waypoint_y[n] then
			u.target_waypoint_x[n], u.target_waypoint_y[n] = s16(x), s16(y)
		end
		if x ~= u.waypoint_x[n] or y ~= u.waypoint_y[n] then
			u.movement_flags[n] = bor(u.movement_flags[n], 1)
			u.waypoint_x[n], u.waypoint_y[n] = s16(x), s16(y)
		end
	end

	-- AdvancePathWaypoint 0x48eb90: unit n heads for the next point of its path to (tx, ty).
	function G:advance_path_waypoint(n, tx, ty)
		if self:path_keeps_waypoint(n) then return true end
		local u = self.u
		if bit_band(u.status[n], M.STATUS_PATHED) == 0 then
			self:set_move_target_point(n, tx, ty)
			self:path_set_direct(n, tx, ty, tx, ty)
			return true
		end
		local ok, px, py = self:next_path_point(n, tx, ty)
		if not ok and u.path[n] == 0 then return false end
		self:set_path_waypoint(n, px, py)
		self:path_keeps_waypoint(n)
		return true
	end
end

return M
