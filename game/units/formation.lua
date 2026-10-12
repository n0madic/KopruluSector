-- Group movement of the right-click command (atlas P16): the formation of the selected
-- units around the clicked point (ComputeGroupMoveFormation 0x49c300), each unit's point in
-- it (GetFormationPointForUnit 0x49c5a0) and the destination adjustment of a ground unit
-- (AdjustUnitMoveDestination 0x42c8c0). Methods of the game.
local int = require("game.simulation.int")
local placement = require("game.units.placement")
local route = require("game.pathing.route")

local band, bor = int.band, int.bor
local s16 = int.s16

local M = {}

M.GROUND = 1 -- pathing flag bit 0 (CUnit +0x105)
-- Formation flags (+0x1a of the native formation record).
M.OFFSET_X, M.OFFSET_Y, M.CONVERGE, M.GROUND_GROUP = 1, 2, 4, 8
M.AREA_SCALE = 2.0 -- the double at 0x4efbf8
M.CONVERGE_MARGIN = 0x20
M.GROUND_SPREAD, M.AIR_SPREAD = 0xc0, 0x100
M.MAX_STEPS = 0x10

function M.install(G)
	-- ComputeGroupMoveFormation 0x49c300 over the command player's selection for the point
	-- (x, y); `adjust` asks for a ground destination adjustment. Returns the formation record:
	-- point (+0), goal (+8, the point), ground goal (+0xc), offset (+0x10), area (+0x14),
	-- radius (+0x18) and flags (+0x1a).
	function G:group_formation(x, y, adjust)
		local u, ext = self.u, self.dat.units.unit_extents
		local f = { x = x, y = y, goal_x = x, goal_y = y, ground_x = x, ground_y = y, dx = 0, dy = 0, area = 0,
			radius = 0, flags = 0 }
		local ground, count = false, 0
		local sum_x, sum_y = 0, 0
		local min_x, min_y, max_x, max_y = 0x7fffffff, 0x7fffffff, 0, 0
		local units = self:selected_units(self.command_player)
		for _, n in ipairs(units) do
			local s = u.sprite[n]
			if s ~= -1 then
				if band(u.pathing_flags[n], M.GROUND) ~= 0 then ground = true end
				count = count + 1
				local sx, sy = s16(self.img.spr.x[s]), s16(self.img.spr.y[s])
				sum_x, sum_y = sum_x + sx, sum_y + sy
				min_x, max_x = math.min(min_x, sx), math.max(max_x, sx)
				min_y, max_y = math.min(min_y, sy), math.max(max_y, sy)
				local t = u.type[n]
				local h = s16(ext[t * 4 + 1] + 1 + ext[t * 4 + 3])
				local w = s16(ext[t * 4 + 2] + 1 + ext[t * 4])
				f.area = int.s32(f.area + h * w)
			end
		end
		if count == 0 then return f end
		f.radius = math.floor(math.sqrt(f.area * M.AREA_SCALE)) % 65536
		-- The centroid uses unsigned division of the coordinate sums.
		local cx, cy = s16(int.udiv(sum_x, count)), s16(int.udiv(sum_y, count))
		if ground and adjust then
			if count == 1 then
				f.ground_x, f.ground_y = self:adjust_move_destination(units[1], f.ground_x, f.ground_y)
			else
				f.ground_x, f.ground_y = self:adjust_group_destination(f.ground_x, f.ground_y, f.radius, f.radius, cx, cy)
			end
			f.ground_x = math.max(0, math.min(f.ground_x, self.map_w_px - 1)) -- ClampPointToMapPixels 0x49c210
			f.ground_y = math.max(0, math.min(f.ground_y, self.map_h_px - 1))
		end
		if count < 2 then return f end
		if x <= max_x and min_x <= x and y <= max_y and min_y <= y then
			f.flags, f.dx, f.dy = M.CONVERGE, 0, 0
			return f
		end
		local spread = M.AIR_SPREAD
		if ground then
			f.flags = bor(f.flags, M.GROUND_GROUP)
			spread = M.GROUND_SPREAD
		end
		if max_x - min_x <= spread and max_y - min_y <= spread then
			f.dx, f.dy = s16(f.goal_x - cx), s16(f.goal_y - cy)
			f.flags = bor(f.flags, M.OFFSET_X + M.OFFSET_Y)
		end
		return f
	end

	-- The ground neighbours of region r (GetRegionGroundNeighbors 0x481ab0) contain `other`,
	-- or it is r itself (0x49c270).
	local function same_or_neighbor(self, r, other)
		if r == other then return true end
		for _, q in ipairs(self:region_ground_neighbors(r)) do
			if q == other then return true end
		end
		return false
	end

	-- GetFormationPointForUnit 0x49c5a0: unit n's target point in formation f.
	function G:formation_point(n, f)
		local u = self.u
		local s = u.sprite[n]
		local sx, sy = s16(self.img.spr.x[s]), s16(self.img.spr.y[s])
		local ground = band(u.pathing_flags[n], M.GROUND) ~= 0
		local gx, gy = f.goal_x, f.goal_y
		if ground then gx, gy = f.ground_x, f.ground_y end
		local ox, oy = f.x, f.y
		if band(f.flags, M.CONVERGE) ~= 0 then
			ox = sx < gx and math.max(gx - M.CONVERGE_MARGIN, sx) or math.min(gx + M.CONVERGE_MARGIN, sx)
			oy = sy < gy and math.max(gy - M.CONVERGE_MARGIN, sy) or math.min(gy + M.CONVERGE_MARGIN, sy)
		else
			if band(f.flags, M.OFFSET_X) ~= 0 then ox = s16(sx + f.dx) end
			if band(f.flags, M.OFFSET_Y) ~= 0 then oy = s16(sy + f.dy) end
		end
		local t = u.type[n]
		ox, oy = self:clamp_to_map(t, ox, oy)
		if not ground then return ox, oy end
		local goal_region = self:region_at(gx, gy)
		if self:unit_type_fits(t, ox, oy) and same_or_neighbor(self, goal_region, self:region_at(ox, oy)) then
			return ox, oy
		end
		local d = self:approx_distance(ox, oy, gx, gy)
		if d == 0 then d = 1 end
		local step = int.idiv(d, 4)
		if step < 2 then step = d end
		if step > M.MAX_STEPS then step = M.MAX_STEPS end
		local k = step
		while k <= d do
			local px = s16(int.idiv((gx - ox) * k, d) + ox)
			local py = s16(int.idiv((gy - oy) * k, d) + oy)
			if self:unit_type_fits(t, px, py) and same_or_neighbor(self, goal_region, self:region_at(px, py)) then
				return px, py
			end
			k = k + step
		end
		return gx, gy
	end

	-- AdjustDestinationToReachableFit 0x42c9a0: (x, y) for a box {left, up, right, down}
	-- coming from (ox, oy): into the point's region when it is walkable and connected to the
	-- origin, else into the reachable region nearest to it, then to a spot where the box covers
	-- no unwalkable tile.
	function G:adjust_destination_fit(x, y, box, ox, oy)
		local walkable = self:point_walkable(x, y)
		placement.set_box_extents(self.extent_ctx, box[1], box[2], box[3], box[4]) -- SetBoxExtents 0x489270
		local region
		if walkable then
			region = self:tile_region_first(x, y)
			if self:region_group_of(self:tile_region_first(ox, oy)) ~= self:region_group_of(region) then
				region = nil
			end
		end
		if not region then region = self:reachable_substitute_region(ox, oy, x, y) end
		local c = route.new_context(0, 0, 0, 0, 0)
		c.goal_x, c.goal_y = x, y
		self:snap_goal_into_region(c, region)
		c.ext_left, c.ext_up, c.ext_right, c.ext_down = s16(-box[1]), s16(-box[2]), s16(box[3]), s16(box[4])
		local _, fx, fy = self:footprint_near(c, c.goal_x, c.goal_y)
		return fx, fy
	end

	-- AdjustUnitMoveDestination 0x42c8c0: a point where unit n's type does not fit moves to a
	-- reachable fit for the type's box.
	function G:adjust_move_destination(n, x, y)
		local u = self.u
		local t = u.type[n]
		if self:unit_type_fits(t, x, y) then return x, y end
		local ext = self.dat.units.unit_extents
		local s = u.sprite[n]
		return self:adjust_destination_fit(x, y, { s16(ext[t * 4]), s16(ext[t * 4 + 1]), s16(ext[t * 4 + 2]),
			s16(ext[t * 4 + 3]) }, s16(self.img.spr.x[s]), s16(self.img.spr.y[s]))
	end

	-- AdjustGroupMoveDestination 0x42c930: a point where the group's w x h square does not
	-- fit moves to a reachable fit for it, coming from the group's centroid (ox, oy).
	function G:adjust_group_destination(x, y, w, h, ox, oy)
		if self:box_fits(x, y, w, h) then return x, y end
		local hw, hh = int.idiv(w, 2), int.idiv(h, 2)
		return self:adjust_destination_fit(x, y, { hw, hh, w - hw, h - hh }, ox, oy)
	end
end

return M
