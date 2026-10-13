-- The local path search (LocalPathSearch 0x491f50; atlas P30,
-- docs/research/native-local-paths.md) and its geometry context (0x69d930): around a node,
-- the terrain contours and the blocking units' edges inside a 64-pixel window are copied to
-- local edge arrays (CollectLocalPathGeometry 0x488320), the nearest edges on the four sides
-- bound the window, and four quadrant generators (0x489880, 0x48a240, 0x489d50, 0x489360) emit
-- the corner points that become neighbour nodes, filtered by the areas already swept
-- (AddVisitedArea 0x492de0, FilterFlagsByVisitedAreas 0x4930b0). Integer widths, scan orders and
-- tie rules are the original's. Methods of the game. native/kp_path is the same search in C
-- (ADR 0007), used when present; this code stays the reference and the fallback.
local heap = require("game.pathing.heap")
local native = require("game.pathing.native")
local int = require("game.simulation.int")
local placement = require("game.units.placement")
local spatial = require("game.units.spatial")

local band, bor = int.band, int.bor
local s16, shr = int.s16, int.shr

local M = {}

M.WINDOW = 0x40 -- the window around a node
M.LOCAL_EDGES = 0x80 -- records of each local edge array
M.MAX_NEIGHBORS = 0x20
M.ARENA = 249 -- 32-byte nodes after the start node (0x69b7a8 .. 0x69d6c8)
M.OPEN_LIMIT = 0x96
M.GOAL_NODES, M.NEAR_NODES = 150, 200 -- node counts after which a reachable goal ends the search
M.MAX_CHAIN = 0x80
M.MAX_COLUMNS, M.MAX_INTERVALS = 0x80, 10
M.NO_POINT = s16(0x8300) -- the empty goal point and target box of 0x4892d0 and 0x491c10
M.START_COST = 0x400000
M.FINAL = 0x1ffe -- "no next region": the segment ends at the goal (0x491c10)
M.REGION_IDS = 0x9c4
-- Direction bits of a node's flags (+0xa): the sides and corners still open.
M.NE, M.SE, M.SW, M.NW, M.N, M.E, M.S, M.W = 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80
-- The edge records of a blocking unit's sides (0x488910): direction byte and info byte.
M.UNIT_EDGES = { [0] = { 0, 0x3d }, { 1, 0x32 }, { 2, 0x3d }, { 3, 0x32 } }

function M.install(G)
	local function new_array()
		return { axis = {}, start = {}, stop = {}, dir = {}, info = {}, count = 0 }
	end

	-- The geometry context 0x69d930: active edge arrays (+0), the saved global ones (+0x174),
	-- the type extents (+0x20) and widened extents (+0x30) in ext[0..3] and ext[8..11], the
	-- subjects (+0x3c..+0x4e), the node (+0x38), its window (+0x60..+0x66), the nearest edges
	-- (+0x50..+0x5c), the neighbour list (+0x70..), the swept rectangles (+0x18c..) and the
	-- local edge arrays (+0x2d0..).
	function G:local_geometry_init()
		self.lg = {
			active = {}, saved = {}, loc = { [0] = new_array(), new_array(), new_array(), new_array() },
			ext = {}, edge = {}, nb = {}, nb_count = 0, rects = {}, rect_count = 0,
		}
		self.visited = { xs = {}, colmap = {}, lists = {}, count = 0 }
	end

	local function copy(a, k, b, j)
		b.axis[j], b.start[j], b.stop[j], b.dir[j], b.info[j] = a.axis[k], a.start[k], a.stop[k], a.dir[k], a.info[k]
	end

	-- The extent of record k's ends: ext[info & 3] for the start, ext[info >> 2 & 3] for the end.
	local function lo(lg, a, k) return lg.ext[band(a.info[k], 3)] end
	local function hi(lg, a, k) return lg.ext[band(shr(a.info[k], 2), 3)] end

	-- InsertLocalEdge 0x488f00: record (axis, start, stop, dir, info) into local array a in
	-- (axis, start) order from index `cursor`, merging an overlapping or touching record of the
	-- same axis. Returns the record's index (the next cursor).
	local function insert_edge(lg, a, cursor, axis, start, stop, dir, info)
		local e = lg.ext
		local rlo = e[band(info, 3)] + start
		local p = cursor
		while p < a.count do
			if a.axis[p] > axis or (a.axis[p] == axis and hi(lg, a, p) + a.stop[p] >= rlo) then
				if a.axis[p] == axis and hi(lg, a, p) + 1 + a.stop[p] >= rlo
					and lo(lg, a, p) - 1 + a.start[p] <= e[band(shr(info, 2), 3)] + stop then
					if a.start[p] > start then a.start[p] = start end
					if a.stop[p] < stop then a.stop[p] = stop end
					return p
				end
				for k = a.count, p + 1, -1 do copy(a, k - 1, a, k) end
				break
			end
			p = p + 1
		end
		if a.count >= M.LOCAL_EDGES then error("local search: local edge array full (0x488f00)", 0) end
		a.axis[p], a.start[p], a.stop[p], a.dir[p], a.info[p] = axis, start, stop, dir, info
		a.count = a.count + 1
		return p
	end

	-- SearchEdgeAxis 0x48b720 (with flag 1): the first record whose axis is >= v.
	local function first_from(a, v)
		if a.count == 0 then return 0 end
		local key = s16(v * 2 - 1)
		local low, high = 0, a.count
		while low < high do
			local mid = math.floor((low + high) / 2)
			if key >= s16(a.axis[mid] * 2) then low = mid + 1 else high = mid end
		end
		return high
	end

	-- CollectLocalTerrainContours 0x488600: the active (global) contours that reach the
	-- window, side by side, into the local arrays.
	local function collect_terrain(lg)
		local e = lg.ext
		local function side(d, from, limit, keep)
			local src, dst = lg.active[d], lg.loc[d]
			local cursor = 0
			for k = first_from(src, from), src.count - 1 do
				if src.axis[k] > limit then break end
				if keep(e[band(src.info[k], 3)] + src.start[k], e[band(shr(src.info[k], 2), 3)] + src.stop[k]) then
					cursor = insert_edge(lg, dst, cursor, src.axis[k], src.start[k], src.stop[k], src.dir[k], src.info[k])
				end
			end
		end
		local function x_range(s, t) return s <= lg.right and t >= lg.left end
		local function y_range(s, t) return s <= lg.bottom and t >= lg.top end
		side(0, lg.top - e[0] - 1, s16(lg.y - e[0] - 1), x_range)
		side(1, lg.x - e[1] + 1, s16(lg.right - e[1] + 1), y_range)
		side(2, lg.y - e[2] + 1, s16(lg.bottom - e[2] + 1), x_range)
		side(3, lg.left - e[3] - 1, s16(lg.x - e[3] - 1), y_range)
	end

	-- IsPathObstacleUnit 0x42e490: a unit at rest (or the collision subject 0x69b57c, or any
	-- unit while 0x69b578 is set) that is not a door, walks on the ground and is not ignored,
	-- and that `mover` (when given) collides with.
	function G:path_obstacle(c, mover)
		local u = self.u
		local s = u.sprite[c]
		local at_rest = u.move_x[c] == s16(self.img.spr.x[s]) and u.move_y[c] == s16(self.img.spr.y[s])
		if not (at_rest or c == self.path_subject or self.path_all_units ~= 0) then return false end
		local t = u.type[c]
		if t >= 0xcd and t <= 0xd0 then return false end -- IsDoorType 0x4290a0
		if band(u.pathing_flags[c], 1) == 0 or band(u.status[c], 0x200000) ~= 0 then return false end
		if mover == 0 then return true end
		return c ~= mover and self:units_collide(c, mover)
	end

	-- CanUnitsCollide 0x42ebf0.
	function G:units_collide(a, b)
		local u = self.u
		if band(u.status[a], 0x400000) ~= 0 and self:is_enemy(b, a) then return false end
		if u.type[b] == 0x23 then
			if band(u.status[a], 2) == 0 then return false end
		elseif u.type[a] == 0x23 then
			return false
		end
		if band(u.status[b], 0x800000) == 0 or band(u.status[a], 2) ~= 0 then return true end
		if band(u.status[a], 0x800000) == 0 then return false end
		if u.order[b] == 0x54 then return false end
		return u.order[a] == 0x52
	end

	-- AddLocalUnitEdges 0x488910: the sides of the blocking units in the spatial index that
	-- face the node inside the window.
	local function collect_units(self, lg)
		local e, sp, u = lg.ext, self.spatial, self.u
		local function side(d, at, units, from, limit, inclusive_limit, field, cross_at, c_lo, c_hi, keep)
			local dst = lg.loc[d]
			local ed = M.UNIT_EDGES[d]
			for k = spatial.search(at, sp.count, from, true), sp.count - 1 do
				if inclusive_limit and at[k] > limit or not inclusive_limit and at[k] >= limit then break end
				local n = units[k]
				if u[field][n] == k then
					local start, stop = s16(cross_at[u[c_lo][n]]), s16(cross_at[u[c_hi][n]])
					if keep(start, stop) and (n == lg.target or self:path_obstacle(n, lg.unit)) then
						insert_edge(lg, dst, 0, s16(at[k]), start, stop, ed[1], ed[2])
					end
				end
			end
		end
		local function x_range(s, t) return s16(e[1] + s) <= lg.right and s16(e[3] + t) >= lg.left end
		local function y_range(s, t) return s16(e[2] + s) <= lg.bottom and s16(e[0] + t) >= lg.top end
		side(0, sp.y_at, sp.y_unit, lg.top - e[0] - 1, s16(lg.y - e[0]), false, "bound_bottom", sp.x_at,
			"bound_left", "bound_right", x_range)
		side(1, sp.x_at, sp.x_unit, lg.x - e[1], s16(lg.right - e[1] + 1), true, "bound_left", sp.y_at,
			"bound_top", "bound_bottom", y_range)
		side(2, sp.y_at, sp.y_unit, lg.y - e[2], s16(lg.bottom - e[2] + 1), true, "bound_top", sp.x_at,
			"bound_left", "bound_right", x_range)
		side(3, sp.x_at, sp.x_unit, lg.left - e[3] - 1, s16(lg.x - e[3]), false, "bound_right", sp.y_at,
			"bound_top", "bound_bottom", y_range)
	end

	-- The record k of array a overlaps [a1, a2] on the cross axis (the edge finders).
	local function overlaps(lg, a, k, a1, a2)
		local l = s16(lo(lg, a, k) + a.start[k])
		local h = s16(hi(lg, a, k) + a.stop[k])
		if l < a1 then l = a1 end
		if h > a2 then h = a2 end
		return l <= h
	end

	-- FindTopEdge 0x48c5e0 / FindLeftEdge 0x48c400: the nearest edge before `pos` (downward
	-- scan) down to `limit`; FindRightEdge 0x48c4e0 / FindBottomEdge 0x48c300: after it
	-- (upward scan) up to `limit`. Return the record index or nil.
	local function edge_before(lg, d, pos, a1, a2, limit)
		local a, e = lg.active[d], lg.ext[d]
		if a.count == 0 then return nil end
		local key = s16((pos - e) * 2 + 1)
		local low, high = 0, a.count
		while low < high do
			local mid = math.floor((low + high) / 2)
			if key >= s16(a.axis[mid] * 2) then low = mid + 1 else high = mid end
		end
		limit = s16(limit - e)
		for k = high - 1, 0, -1 do
			if a.axis[k] < limit then return nil end
			if overlaps(lg, a, k, a1, a2) then return k end
		end
		return nil
	end
	local function edge_after(lg, d, pos, a1, a2, limit)
		local a, e = lg.active[d], lg.ext[d]
		local low, high = 0, a.count
		if a.count ~= 0 then
			local key = s16((pos - e) * 2 - 1)
			while low < high do
				local mid = math.floor((low + high) / 2)
				if key >= s16(a.axis[mid] * 2) then low = mid + 1 else high = mid end
			end
		end
		if high == a.count then return nil end
		limit = s16(limit - e)
		for k = high, a.count - 1 do
			if a.axis[k] > limit then return nil end
			if overlaps(lg, a, k, a1, a2) then return k end
		end
		return nil
	end
	local function top_edge(lg, y, x1, x2, limit) return edge_before(lg, 0, y, x1, x2, limit) end
	local function right_edge(lg, x, y1, y2, limit) return edge_after(lg, 1, x, y1, y2, limit) end
	local function bottom_edge(lg, y, x1, x2, limit) return edge_after(lg, 2, y, x1, x2, limit) end
	local function left_edge(lg, x, y1, y2, limit) return edge_before(lg, 3, x, y1, y2, limit) end

	-- AddPathNeighbor 0x488220: a candidate point with the directions still open from it; the
	-- goal point or a point on the target's box becomes a goal entry.
	local function add(lg, x, y, flags)
		x, y = s16(x), s16(y)
		if x == lg.x and y == lg.y then return end
		local goal = false
		if lg.target ~= 0 then
			local l, t, r, b = lg.box[0], lg.box[1], lg.box[2], lg.box[3]
			if (x == l or x == r) and y >= t and y <= b then
				goal = true
			elseif (y == t or y == b) and x >= l and x <= r then
				goal = true
			end
		end
		if not goal and x == lg.goal_x and y == lg.goal_y then goal = true end
		if goal then
			flags = 0xff
			lg.goal_found = 1
		end
		if lg.nb_count >= M.MAX_NEIGHBORS then return end
		if not goal then
			for k = 0, lg.nb_count - 1 do
				local q = lg.nb[k]
				if q.x == x and q.y == y then
					q.flags = band(q.flags, flags)
					return
				end
			end
		end
		lg.nb[lg.nb_count] = { x = x, y = y, flags = flags, goal = goal and 1 or 0 }
		lg.nb_count = lg.nb_count + 1
	end

	-- CheckGoalInArea 0x489070: the goal point inside a swept rectangle is a goal entry.
	local function check_goal(lg, l, t, r, b)
		if l > r or t > b then return end
		if lg.goal_x > r or lg.goal_x < l or lg.goal_y > b or lg.goal_y < t then return end
		if lg.nb_count >= M.MAX_NEIGHBORS then error("local search: neighbour list overflow (0x489070)", 0) end
		lg.nb[lg.nb_count] = { x = lg.goal_x, y = lg.goal_y, flags = 0xff, goal = 1 }
		lg.nb_count = lg.nb_count + 1
		lg.goal_found = 1
	end

	-- AppendVisitedArea 0x4890e0, after CheckGoalInArea.
	local function sweep(lg, l, t, r, b)
		check_goal(lg, l, t, r, b)
		lg.rects[lg.rect_count] = { s16(l), s16(t), s16(r), s16(b) }
		lg.rect_count = lg.rect_count + 1
	end

	-- MakeOrderedRect 0x48a7a0.
	local function sweep_points(lg, x1, y1, x2, y2)
		if x1 > x2 then x1, x2 = x2, x1 end
		if y1 > y2 then y1, y2 = y2, y1 end
		sweep(lg, x1, y1, x2, y2)
	end

	-- A record moved out by the widened extents (ExpandEdgeOuter 0x489320): axis, start, end
	-- and the info byte.
	local function expand(lg, a, k)
		local w = lg.ext
		return s16(a.axis[k] + w[8 + a.dir[k]]), s16(a.start[k] + w[8 + band(a.info[k], 3)]),
			s16(a.stop[k] + w[8 + band(shr(a.info[k], 2), 3)]), a.info[k]
	end

	-- GenerateNeighborsNorthEast 0x489880.
	local function north_east(lg)
		local e = lg.ext
		local cx, cy, top, right = lg.x, lg.y, lg.top, lg.right
		local e_top, e_right = lg.edge[0], lg.edge[1]
		local y_cur, x_right = cy, right
		local A, S, T, info = 0, 0, 0, 0
		if cx < right then
			local ylim, re_y = top, top
			if e_right then
				local a = lg.active[1]
				re_y = s16(e[band(a.info[e_right], 3)] - 1 + a.start[e_right])
				if re_y > top then ylim = re_y end
			end
			local rx = right
			local k = top_edge(lg, s16(cy - 1), cx, right, s16(ylim - 1))
			local flags = 0x3b
			if k then
				A, S, T, info = expand(lg, lg.active[0], k)
				ylim = A
				if right < T then
					flags = 0x22
				elseif right == T then
					flags = band(info, 0x20) ~= 0 and 0x33 or 0
				end
			end
			if e_right then
				flags = band(flags, 0xfd)
				if re_y < ylim then flags = band(flags, 0xde) end
			end
			if ylim < cy then add(lg, right, cy, 0xe7) else flags = bor(flags, 0x44) end
			add(lg, right, ylim, flags)
			if ylim == top then add(lg, cx, ylim, 0xdd) end
			if k then
				if right > T then add(lg, T, A, 0x11) end
				if S < cx then
					rx = cx
				else
					while true do
						rx = S
						add(lg, S, A, 0x18)
						k = k - 1
						if k < 0 then break end
						A, S, T, info = expand(lg, lg.active[0], k)
						if A ~= ylim or T < cx then break end
						if right >= T then add(lg, T, A, 0x11) end
						if S < cx then
							rx = cx
							break
						end
					end
				end
			end
			sweep_points(lg, cx, ylim, right, cy)
			y_cur, x_right = ylim, rx
			if not (ylim > top) then return end
		elseif not (cy > top) then
			return
		end
		local xlim, te_x = x_right, x_right
		if e_top then
			local a = lg.active[0]
			te_x = s16(e[band(shr(a.info[e_top], 2), 3)] + 1 + a.stop[e_top])
			if te_x < x_right then xlim = te_x end
		end
		local k = right_edge(lg, s16(cx + 1), top, y_cur, s16(xlim + 1))
		local flags = 0x3b
		if k then
			A, S, T, info = expand(lg, lg.active[1], k)
			xlim = A
			if top > S then
				flags = 0x18
			elseif top == S then
				flags = band(info, 0x10) ~= 0 and 0x39 or 0
			end
		end
		if e_top then
			flags = band(flags, 0xf7)
			if te_x > xlim then flags = band(flags, 0xde) end
		end
		if xlim > cx then add(lg, cx, top, 0xdd) else flags = bor(flags, 0x84) end
		add(lg, xlim, top, flags)
		if xlim < x_right then add(lg, xlim, y_cur, 0x01) end
		if k then
			if top < S then add(lg, A, S, 0x21) end
			local a = lg.active[1]
			while T <= y_cur do
				add(lg, A, T, 0x22)
				k = k + 1
				if k >= a.count then break end
				A, S, T, info = expand(lg, a, k)
				if A ~= xlim or S > y_cur then
					add(lg, xlim, y_cur, 0x21)
					break
				end
				if top <= S then add(lg, A, S, 0x21) end
			end
		end
		sweep_points(lg, cx, top, xlim, y_cur)
	end

	-- GenerateNeighborsSouthEast 0x48a240.
	local function south_east(lg)
		local e = lg.ext
		local cx, cy, bottom, right = lg.x, lg.y, lg.bottom, lg.right
		local e_bottom, e_right = lg.edge[2], lg.edge[1]
		local y_cur, x_right = cy, right
		local A, S, T, info = 0, 0, 0, 0
		if cx < right then
			local ylim, re_y = bottom, bottom
			if e_right then
				local a = lg.active[1]
				re_y = s16(e[band(shr(a.info[e_right], 2), 3)] + 1 + a.stop[e_right])
				if re_y < bottom then ylim = re_y end
			end
			local x_after = right
			local k = bottom_edge(lg, s16(cy + 1), cx, right, s16(ylim + 1))
			local flags = 0x67
			if k then
				local a = lg.active[2]
				A, S, T, info = expand(lg, a, k)
				if cx < S then add(lg, S, A, 0x44) end
				ylim, x_after = A, S
				while T < right do
					add(lg, T, A, 0x42)
					k = k + 1
					if k >= a.count then break end
					if e[a.dir[k]] + a.axis[k] - 1 ~= ylim then break end
					if e[band(a.info[k], 3)] + a.start[k] - 1 >= right then break end
					A, S, T, info = expand(lg, a, k)
					if right >= S then add(lg, S, A, 0x44) end
				end
				if right < T then
					flags = 0x21
				elseif right == T then
					flags = band(info, 0x20) ~= 0 and 0x63 or 0
				end
			end
			if e_right then
				flags = band(flags, 0xfe)
				if re_y > ylim then flags = band(flags, 0xdd) end
			end
			if ylim > cy then add(lg, right, cy, 0xbb) else flags = bor(flags, 0x18) end
			add(lg, right, ylim, flags)
			if ylim == bottom then add(lg, cx, ylim, 0xde) end
			sweep_points(lg, cx, cy, right, ylim)
			x_right, y_cur = x_after, ylim
			if ylim >= bottom then return end
		elseif cy >= bottom then
			return
		end
		local xlim, te_x = x_right, x_right
		if e_bottom then
			local a = lg.active[2]
			te_x = s16(e[band(shr(a.info[e_bottom], 2), 3)] + 1 + a.stop[e_bottom])
			if te_x < x_right then xlim = te_x end
		end
		local k = right_edge(lg, s16(cx + 1), y_cur, bottom, s16(xlim + 1))
		local flags = 0x67
		if k then
			local a = lg.active[1]
			A, S, T, info = expand(lg, a, k)
			if A < x_right then add(lg, A, y_cur, 0x02) end
			if S > y_cur then add(lg, A, S, 0x21) end
			xlim = A
			while T < bottom do
				add(lg, A, T, 0x22)
				k = k + 1
				if k >= a.count then break end
				if e[a.dir[k]] + a.axis[k] - 1 ~= xlim then break end
				if e[band(a.info[k], 3)] + a.start[k] - 1 >= bottom then break end
				A, S, T, info = expand(lg, a, k)
				if y_cur <= S and S <= bottom then add(lg, A, S, 0x21) end
			end
			if bottom < T then
				flags = 0x44
			elseif bottom == T then
				flags = band(info, 0x20) ~= 0 and 0x66 or 0
			end
		end
		if e_bottom then
			flags = band(flags, 0xfb)
			if te_x > xlim then flags = band(flags, 0xbd) end
		end
		if xlim > cx then add(lg, cx, bottom, 0xde) else flags = bor(flags, 0x88) end
		add(lg, xlim, bottom, flags)
		sweep_points(lg, cx, y_cur, xlim, bottom)
	end

	-- GenerateNeighborsSouthWest 0x489d50.
	local function south_west(lg)
		local e = lg.ext
		local cx, cy, bottom, left = lg.x, lg.y, lg.bottom, lg.left
		local e_bottom, e_left = lg.edge[2], lg.edge[3]
		local y_cur, x_left = cy, left
		local A, S, T, info = 0, 0, 0, 0
		if cx > left then
			local ylim, le_y = bottom, bottom
			if e_left then
				local a = lg.active[3]
				le_y = s16(e[band(shr(a.info[e_left], 2), 3)] + 1 + a.stop[e_left])
				if le_y < bottom then ylim = le_y end
			end
			local rx = left
			local k = bottom_edge(lg, s16(cy + 1), left, cx, s16(ylim + 1))
			local flags = 0xce
			if k then
				A, S, T, info = expand(lg, lg.active[2], k)
				ylim = A
				if left > S then
					flags = 0x88
				elseif left == S then
					flags = band(info, 0x10) ~= 0 and 0xcc or 0
				end
			end
			if e_left then
				flags = band(flags, 0xf7)
				if le_y > ylim then flags = band(flags, 0x7b) end
			end
			if ylim > cy then add(lg, left, cy, 0xbd) else flags = bor(flags, 0x11) end
			add(lg, left, ylim, flags)
			if ylim == bottom then add(lg, cx, ylim, 0x77) end
			if k then
				local a = lg.active[2]
				if left < S then add(lg, S, A, 0x44) end
				local done = false
				while T <= cx do
					add(lg, T, A, 0x42)
					rx = T
					k = k + 1
					if k >= a.count then
						done = true
						break
					end
					A, S, T, info = expand(lg, a, k)
					if A ~= ylim or S > cx then
						done = true
						break
					end
					add(lg, S, A, 0x44)
				end
				if not done then
					add(lg, cx, A, 0x21)
					rx = cx
				end
			end
			sweep_points(lg, left, cy, cx, ylim)
			y_cur, x_left = ylim, rx
			if ylim >= bottom then return end
		elseif cy >= bottom then
			return
		end
		local xlim, te = x_left, x_left
		if e_bottom then
			local a = lg.active[2]
			te = s16(e[band(a.info[e_bottom], 3)] - 1 + a.start[e_bottom])
			if te > x_left then xlim = te end
		end
		local k = left_edge(lg, s16(cx - 1), y_cur, bottom, s16(xlim - 1))
		local flags = 0xce
		if k then
			A, S, T, info = expand(lg, lg.active[3], k)
			xlim = A
			if bottom < T then
				flags = 0x42
			elseif bottom == T then
				flags = band(info, 0x20) ~= 0 and 0xc6 or 0
			end
		end
		if e_bottom then
			flags = band(flags, 0xfd)
			if te < xlim then flags = band(flags, 0xbb) end
		end
		if xlim < cx then add(lg, cx, bottom, 0x77) else flags = bor(flags, 0x22) end
		add(lg, xlim, bottom, flags)
		if xlim > x_left then add(lg, xlim, y_cur, 0x04) end
		if k then
			local a = lg.active[3]
			if bottom > T then add(lg, A, T, 0x84) end
			while S >= y_cur do
				add(lg, A, S, 0x88)
				k = k - 1
				if k < 0 then break end
				A, S, T, info = expand(lg, a, k)
				if A ~= xlim or T < y_cur then
					add(lg, xlim, y_cur, 0x84)
					break
				end
				if bottom >= T then add(lg, A, T, 0x84) end
			end
		end
		sweep_points(lg, xlim, y_cur, cx, bottom)
	end

	-- GenerateNeighborsNorthWest 0x489360.
	local function north_west(lg)
		local e = lg.ext
		local cx, cy, top, left = lg.x, lg.y, lg.top, lg.left
		local e_top, e_left = lg.edge[0], lg.edge[3]
		local y_cur, x_left = cy, left
		local A, S, T, info = 0, 0, 0, 0
		if cx > left then
			local ylim, le = top, top
			if e_left then
				local a = lg.active[3]
				le = s16(e[band(a.info[e_left], 3)] - 1 + a.start[e_left])
				if le > top then ylim = le end
			end
			local x_after = left
			local k = top_edge(lg, s16(cy - 1), left, cx, s16(ylim - 1))
			local flags = 0x9d
			if k then
				local a = lg.active[0]
				A, S, T, info = expand(lg, a, k)
				if cx > T then add(lg, T, A, 0x11) else add(lg, cx, A, 0x22) end
				ylim, x_after = A, T
				while left < S do
					add(lg, S, A, 0x18)
					k = k - 1
					if k < 0 then break end
					if e[a.dir[k]] + a.axis[k] + 1 ~= ylim then break end
					if e[band(shr(a.info[k], 2), 3)] + a.stop[k] + 1 <= left then break end
					A, S, T, info = expand(lg, a, k)
					if left <= T then add(lg, T, A, 0x11) end
				end
				if left > S then
					flags = 0x84
				elseif left == S then
					flags = band(info, 0x10) ~= 0 and 0x9c or 0
				end
			end
			if e_left then
				flags = band(flags, 0xfb)
				if le < ylim then flags = band(flags, 0x77) end
			end
			if ylim < cy then add(lg, left, cy, 0xee) else flags = bor(flags, 0x44) end
			add(lg, left, ylim, flags)
			if ylim == top then add(lg, cx, ylim, 0x7b) end
			sweep_points(lg, left, ylim, cx, cy)
			y_cur, x_left = ylim, x_after
			if ylim <= top then return end
		elseif cy <= top then
			return
		end
		local xlim, te = x_left, x_left
		if e_top then
			local a = lg.active[0]
			te = s16(e[band(a.info[e_top], 3)] - 1 + a.start[e_top])
			if te > x_left then xlim = te end
		end
		local k = left_edge(lg, s16(cx - 1), top, y_cur, s16(xlim - 1))
		local flags = 0x9d
		if k then
			local a = lg.active[3]
			A, S, T, info = expand(lg, a, k)
			if A > x_left then add(lg, A, y_cur, 0x08) end
			if T < y_cur then add(lg, A, T, 0x84) end
			xlim = A
			while S > top do
				add(lg, A, S, 0x88)
				k = k - 1
				if k < 0 then break end
				if e[a.dir[k]] + a.axis[k] + 1 ~= xlim then break end
				if e[band(shr(a.info[k], 2), 3)] + a.stop[k] + 1 <= top then break end
				A, S, T, info = expand(lg, a, k)
				if top <= T then add(lg, A, T, 0x84) end
			end
			if top > S then
				flags = 0x11
			elseif top == S then
				flags = band(info, 0x10) ~= 0 and 0x99 or 0
			end
		end
		if e_top then
			flags = band(flags, 0xfe)
			if te < xlim then flags = band(flags, 0xe7) end
		end
		if xlim < cx then add(lg, cx, top, 0x7b) else flags = bor(flags, 0x21) end
		add(lg, xlim, top, flags)
		sweep_points(lg, xlim, top, cx, y_cur)
	end

	-- CollectLocalPathGeometry 0x488320 at (x, y) for the open directions `mask`.
	local function collect(self, lg, x, y, mask)
		local map_w, map_h = self.map_w_px, self.map_h_px
		lg.x, lg.y = x, y
		lg.nb_count, lg.rect_count = 0, 0
		lg.top, lg.right, lg.left, lg.bottom = s16(y - M.WINDOW), s16(x + M.WINDOW), s16(x - M.WINDOW), s16(y + M.WINDOW)
		if lg.top < 0 then lg.top = 0 end
		if lg.left < 0 then lg.left = 0 end
		if lg.bottom >= map_h then lg.bottom = map_h - 1 end
		if lg.right >= map_w then lg.right = map_w - 1 end
		if band(mask, 0x19) == 0 then lg.top = y end
		if band(mask, 0x23) == 0 then lg.right = x end
		if band(mask, 0x46) == 0 then lg.bottom = y end
		if band(mask, 0x8c) == 0 then lg.left = x end
		collect_terrain(lg)
		collect_units(self, lg)
		for d = 0, 3 do lg.active[d] = lg.loc[d] end
		local e = lg.ext
		local k = top_edge(lg, y, x, x, s16(lg.top - 1))
		lg.edge[0] = k
		if k then lg.top = s16(e[0] + 1 + lg.active[0].axis[k]) end
		k = right_edge(lg, x, y, y, s16(lg.right + 1))
		lg.edge[1] = k
		if k then lg.right = s16(e[1] - 1 + lg.active[1].axis[k]) end
		k = bottom_edge(lg, y, x, x, s16(lg.bottom + 1))
		lg.edge[2] = k
		if k then lg.bottom = s16(e[2] - 1 + lg.active[2].axis[k]) end
		k = left_edge(lg, x, y, y, s16(lg.left - 1))
		lg.edge[3] = k
		if k then lg.left = s16(e[3] + 1 + lg.active[3].axis[k]) end
		local top, right, bottom, left = lg.top, lg.right, lg.bottom, lg.left
		if top == y and right == x then mask = band(mask, 0xfe) end
		if bottom == y then
			if right == x then mask = band(mask, 0xfd) end
			if left == x then mask = band(mask, 0xfb) end
		end
		if top == y then
			if left == x then mask = band(mask, 0xf7) end
			mask = band(mask, 0xef)
		end
		if right == x then mask = band(mask, 0xdf) end
		if bottom == y then mask = band(mask, 0xbf) end
		if left == x then mask = band(mask, 0x7f) end
		if band(mask, M.N) ~= 0 then
			mask = bor(mask, 9)
			add(lg, x, top, lg.edge[0] and 0xa6 or 0xbf)
		end
		if band(mask, M.E) ~= 0 then
			mask = bor(mask, 3)
			add(lg, right, y, lg.edge[1] and 0x5c or 0x7f)
		end
		if band(mask, M.S) ~= 0 then
			mask = bor(mask, 6)
			add(lg, x, bottom, lg.edge[2] and 0xa9 or 0xef)
		end
		if band(mask, M.W) ~= 0 then
			mask = bor(mask, 0xc)
			add(lg, left, y, lg.edge[3] and 0x53 or 0xdf)
		end
		lg.mask = mask
	end

	-- The swept areas (0x69ec08: column starts; 0x69ee10: column -> interval list;
	-- 0x69f018: lists of at most 10 y intervals).
	local function visited_reset(self)
		local v = self.visited
		v.count = 1
		v.xs[0], v.xs[1] = 0, self.map_w_px
		v.colmap[0], v.colmap[1] = 0, -1
		v.lists[0] = { count = 0, y0 = {}, y1 = {} }
	end

	local function column_of(v, x)
		local c = 0
		while x >= v.xs[c + 1] do c = c + 1 end
		return c
	end

	-- A copy of list `slot` as the new list v.count (the column split's memcpy).
	local function clone_list(v, slot)
		local src = v.lists[slot]
		local dst = { count = src.count, y0 = {}, y1 = {} }
		for i = 0, src.count - 1 do dst.y0[i], dst.y1[i] = src.y0[i], src.y1[i] end
		v.lists[v.count] = dst
	end

	-- Shifts the column entries c..count one up (the memmoves of 0x492de0), opening a
	-- boundary at c + 1.
	local function open_column(v, c)
		for i = v.count, c, -1 do
			v.xs[i + 1] = v.xs[i]
			v.colmap[i + 1] = v.colmap[i]
		end
	end

	-- AddVisitedArea 0x492de0: rectangle {left, top, right, bottom}; false when full.
	local function visited_add(v, r)
		local x0, y0, x1, y1 = r[1], r[2], r[3], r[4]
		local c = column_of(v, x0)
		if v.xs[c] < x0 then
			if v.count == M.MAX_COLUMNS then return false end
			local slot = v.colmap[c]
			open_column(v, c)
			v.colmap[c] = v.count
			clone_list(v, slot)
			v.count = v.count + 1
			c = c + 1
			v.xs[c] = x0
		end
		if x0 > x1 then return true end
		while true do
			local list = v.lists[v.colmap[c]]
			if x1 < v.xs[c + 1] - 1 then
				if v.count == M.MAX_COLUMNS then return false end
				local slot = v.colmap[c]
				open_column(v, c)
				v.colmap[c + 1] = v.count
				clone_list(v, slot)
				v.count = v.count + 1
				v.xs[c + 1] = x1 + 1
			end
			local n = list.count
			local i, k = 0, n
			while k > 0 and y0 > list.y1[i] + 1 do
				k = k - 1
				i = i + 1
			end
			if k == 0 then
				if n == M.MAX_INTERVALS then return false end
				list.y0[i], list.y1[i] = y0, y1
				list.count = n + 1
			elseif y1 < list.y0[i] - 1 or y0 > list.y1[i] + 1 then
				if n == M.MAX_INTERVALS then return false end
				for j = n, i + 1, -1 do list.y0[j], list.y1[j] = list.y0[j - 1], list.y1[j - 1] end
				list.y0[i], list.y1[i] = y0, y1
				list.count = n + 1
			else
				if list.y0[i] > y0 then list.y0[i] = y0 end
				if list.y1[i] < y1 then list.y1[i] = y1 end
				-- Coalesce over the whole list, from its first interval.
				local j = 0
				for _ = 1, list.count - 1 do
					if list.y0[j + 1] - 1 <= list.y1[j] then
						if list.y1[j] < list.y1[j + 1] then list.y1[j] = list.y1[j + 1] end
						for m = j + 1, list.count - 2 do list.y0[m], list.y1[m] = list.y0[m + 1], list.y1[m + 1] end
						list.count = list.count - 1
					else
						j = j + 1
					end
				end
			end
			c = c + 1
			if v.xs[c] > x1 then return true end
		end
	end

	-- The first interval of `list` that ends at or after y, or nil.
	local function interval_at(list, y)
		for i = 0, list.count - 1 do
			if y <= list.y1[i] then return i end
		end
		return nil
	end

	-- FilterFlagsByVisitedAreas 0x4930b0: the directions of point (x, y) into swept areas
	-- are cleared from `flags`.
	local function visited_filter(v, x, y, flags)
		local c = column_of(v, x)
		local list = v.lists[v.colmap[c]]
		if list.count == 0 then return flags end
		local i = interval_at(list, y)
		if not i then return flags end
		local m = list.y0[i] >= y and 0x19 or 0
		if list.y1[i] <= y then m = bor(m, 0x46) end
		if v.xs[c] == x then
			m = bor(m, 0x8c)
			if c > 0 then
				local l2 = v.lists[v.colmap[c - 1]]
				local j = l2.count > 0 and interval_at(l2, y)
				if j and l2.y0[j] <= y and l2.y1[j] >= y then
					m = band(m, 0x7f)
					if l2.y0[j] < y then m = band(m, 0xf7) end
					if l2.y1[j] > y then m = band(m, 0xfb) end
				end
			end
		end
		if v.xs[c + 1] - 1 == x then
			m = bor(m, 0x23)
			if c + 1 < v.count then
				local l2 = v.lists[v.colmap[c + 1]]
				if l2.count > 0 then
					local j = interval_at(l2, y)
					if not j then return band(flags, m) end
					if l2.y0[j] <= y and l2.y1[j] >= y then
						m = band(m, 0xdf)
						if l2.y0[j] < y then m = band(m, 0xfe) end
						if l2.y1[j] > y then m = band(m, 0xfd) end
					end
				end
			end
		end
		return band(flags, m)
	end

	-- ExpandLocalNode 0x491ba0: the neighbour points of node `node`.
	local function expand_node(self, lg, node)
		for d = 0, 3 do lg.active[d] = lg.saved[d] end -- RestoreGlobalContours 0x489110
		for d = 0, 3 do lg.loc[d].count = 0 end -- ResetLocalEdges 0x488ee0
		lg.goal_found = 0
		collect(self, lg, node.x, node.y, node.flags)
		-- GenerateLocalNeighbors 0x48a750 (CheckGoalInArea never stops a generator).
		if band(lg.mask, M.NE) ~= 0 then north_east(lg) end
		if band(lg.mask, M.SE) ~= 0 then south_east(lg) end
		if band(lg.mask, M.SW) ~= 0 then south_west(lg) end
		if band(lg.mask, M.NW) ~= 0 then north_west(lg) end
		for k = 0, lg.rect_count - 1 do visited_add(self.visited, lg.rects[k]) end
	end

	-- StoreLocalPathPoints 0x492860: the chain goal -> parents -> source (chain[1] is the goal,
	-- chain[n] the source; positions as {x, y}) into c.points without the source, from the
	-- source side; beyond 50 points the first 49 and the goal.
	local function store_points(c, chain)
		local n = #chain
		c.point_index = 0
		if n < 3 then
			if n == 2 then c.points[0] = chain[1] end
			c.point_count = n - 1
			return
		end
		local k, j = 0, n - 1 -- chain[j] is the node after the source
		while j >= 2 and k <= 0x30 do
			c.points[k] = chain[j]
			k = k + 1
			j = j - 1
		end
		c.points[k] = chain[1]
		c.point_count = k + 1
	end

	-- LocalPathSearch 0x491f50 on search context c (route.lua) towards (gx, gy), the segment
	-- leading into region `next_region` (M.FINAL for the goal). True when points were stored.
	local function lua_search(self, c, next_region, gx, gy)
		local rg, lg, u = self.rg, self.lg, self.u
		local final = gx == c.goal_x and gy == c.goal_y
		local scale_other = next_region < M.REGION_IDS and rg.class[next_region] ~= 0x1ffd
		c.local_nodes, c.local_peak = 0, 0
		local src_region = c.src_region
		local goal_region = self:region_at(c.goal_x, c.goal_y)
		local list = rg.neighbors[next_region < M.REGION_IDS and next_region or goal_region]
		local around = next_region < M.REGION_IDS and next_region or goal_region
		local near_count = rg.ground[around]
		for k = 0, near_count - 1 do
			if list[k] ~= src_region then rg.mark[list[k]] = 1 end
		end
		local gx8, gy8 = int.shl(gx, 8), int.shl(gy, 8)
		for d = 0, 3 do lg.saved[d], lg.active[d] = self.contours[d], self.contours[d] end -- 0x488e10
		placement.set_extents(self.dat.units.unit_extents, u.type[c.unit], lg.ext) -- 0x4891c0
		-- SetSearchSubjects 0x4892d0.
		lg.unit, lg.goal_x, lg.goal_y, lg.target = c.unit, gx, gy, c.target
		lg.box = { [0] = c.box_left, c.box_top, c.box_right, c.box_bottom }
		visited_reset(self)
		local open = heap.new()
		local start = { x = c.src_x, y = c.src_y, region = src_region, near = 0, in_next = 0, goal = 0, closed = 0,
			flags = 0xff, depth = 0, heur = M.START_COST, cost = 0, prio = M.START_COST }
		local nodes = {}
		-- The first node at each position (the original finds it with a scan of the arena).
		local at_position = {}
		local function position_key(x, y) return (x + 0x8000) * 0x10000 + (y + 0x8000) end
		heap.insert(open, start)
		local goal_hit, done, goal_count, near_open = false, false, 0, 0
		local best
		local reached_goal = false
		-- Nodes in use, the start node included (the original measures next - 0x69b788).
		local function arena_used() return #nodes + 1 end
		local function approx(x1, y1, x2, y2) return self:approx_distance(x1, y1, x2, y2) end
		local finished = false
		local cur = start
		while true do
			cur = heap.pop(open)
			if cur.goal ~= 0 then
				reached_goal = true
				best = cur
				break
			end
			if cur.flags ~= 0 then cur.flags = visited_filter(self.visited, cur.x, cur.y, cur.flags) end
			if goal_count > 0 and not goal_hit and (not final or arena_used() >= M.GOAL_NODES) then done = true end
			if near_open ~= 0 and not goal_hit and arena_used() >= M.NEAR_NODES then done = true end
			if cur.in_next ~= 0 and done then
				best = cur
				break
			end
			cur.closed = 1
			if cur.in_next ~= 0 then goal_count = goal_count - 1 end
			if cur.near ~= 0 then
				near_open = near_open - 1
				if done then
					finished = true
					break
				end
			end
			if not goal_hit and not done and cur.flags ~= 0 then
				expand_node(self, lg, cur)
				for k = 0, lg.nb_count - 1 do
					local q = lg.nb[k]
					local region = self:region_at(q.x, q.y)
					local in_next = region == next_region
					local near = rg.mark[region] ~= nil and rg.mark[region] ~= 0
					local skip = false
					if q.goal ~= 0 then
						goal_hit = true
					elseif q.flags ~= 0 then
						q.flags = visited_filter(self.visited, q.x, q.y, q.flags)
						if q.flags == 0 then skip = true end
					end
					if not skip then
						local qflags = q.flags
						local key = position_key(q.x, q.y)
						local node = at_position[key]
						if node then
							if cur.parent ~= node then
								local cost = approx(int.shl(q.x, 8), int.shl(q.y, 8), int.shl(cur.x, 8), int.shl(cur.y, 8))
								if scale_other and region ~= next_region and region ~= src_region then
									cost = int.idiv(int.s32(int.shl(cost, 9)), 256)
								end
								local new = int.s32(cur.cost + cost)
								if new < node.cost then
									node.parent = cur
									node.depth = (cur.depth + 1) % 256
									node.cost = new
									local prio = int.s32(node.heur + new)
									if node.closed == 0 then
										heap.change(open, node, prio)
									else
										node.prio = prio
										node.flags = qflags
										node.closed = 0
										node.goal = q.goal
										heap.insert(open, node)
										if in_next then goal_count = goal_count + 1 end
										if near then near_open = near_open + 1 end
									end
									if open.count == M.OPEN_LIMIT then break end
								elseif node.closed == 0 then
									node.flags = band(node.flags, qflags)
								end
							end
						else
							node = { x = q.x, y = q.y, parent = cur, region = region, flags = qflags,
								depth = (cur.depth + 1) % 256 }
							nodes[#nodes + 1] = node
							at_position[key] = node
							local cost = approx(int.shl(cur.x, 8), int.shl(cur.y, 8), int.shl(q.x, 8), int.shl(q.y, 8))
							if scale_other and region ~= next_region and region ~= src_region then
								cost = int.idiv(int.s32(int.shl(cost, 9)), 256)
							end
							node.heur = approx(int.shl(q.x, 8), int.shl(q.y, 8), gx8, gy8)
							node.cost = int.s32(cur.cost + cost)
							node.prio = int.s32(node.heur + node.cost)
							node.in_next = in_next and 1 or 0
							node.goal = q.goal
							node.near = near and 1 or 0
							node.closed = qflags == 0 and 1 or 0
							if node.closed == 0 or q.goal ~= 0 then
								node.closed = 0
								heap.insert(open, node)
								if in_next then goal_count = goal_count + 1 end
								if near then near_open = near_open + 1 end
							end
							if open.count == M.OPEN_LIMIT or #nodes == M.ARENA then break end
						end
					end
				end
				if c.local_peak < open.count then c.local_peak = open.count end
				if (open.count == M.OPEN_LIMIT or #nodes == M.ARENA) and not goal_hit and goal_count == 0 then
					finished = true
					break
				end
			end
			if open.count == 0 then
				finished = true
				break
			end
		end
		if finished then
			-- The search ended without reaching the goal: choose the node to head for.
			c.local_partial = 1
			local reach, near_hits = 0, 0
			for _, n in ipairs(nodes) do
				local m = rg.mark[n.region]
				if m and m ~= 0 then rg.mark[n.region] = m + 1 end
				if n.closed == 0 then
					local f = n.flags
					if f ~= 0 then f = visited_filter(self.visited, n.x, n.y, f) end
					if f == 0 then
						n.flags = 0
						n.closed = 1
						if n.in_next ~= 0 then goal_count = goal_count - 1 end
					else
						n.flags = f
						reach = reach + 1
						if n.region == goal_region then near_hits = near_hits + 1 end
					end
				end
			end
			if final then
				reach = near_hits
				for k = 0, near_count - 1 do
					local id = list[k]
					if id ~= src_region then
						local m = rg.mark[id] or 0
						if m < 2 then
							reach = reach + 1
							break
						end
						reach = reach - int.idiv(m, 2)
						if reach < 0 then reach = 0 end
					end
				end
			end
			local reachable = reach == 0 and goal_count == 0
			start.heur = approx(int.shl(start.x, 8), int.shl(start.y, 8), gx8, gy8)
			best = start
			if reachable then
				local limit = goal_count ~= 0 and M.START_COST or start.heur
				for _, n in ipairs(nodes) do
					if n.heur < limit then
						local ok
						if final then
							ok = true
						elseif goal_count ~= 0 then
							ok = n.region == goal_region
						else
							ok = n.region == src_region
						end
						if ok then best, limit = n, n.heur end
					end
				end
				c.reached, c.reached_end = 1, 1
			else
				local limit = M.START_COST
				for _, n in ipairs(nodes) do
					local score
					if n.region == goal_region or n.region == next_region then
						score = int.idiv(n.cost, 2) + n.heur
					elseif rg.mark[n.region] and rg.mark[n.region] ~= 0 then
						score = int.idiv(n.heur * 3, 2)
					elseif n.region == src_region then
						score = n.heur * 2
					else
						score = n.heur * 4
					end
					if n.closed ~= 0 then score = int.shl(score, 5) end
					if score < limit then best, limit = n, score end
				end
			end
		elseif reached_goal then
			c.reached = 1
		end
		c.local_nodes = #nodes + 1
		if best then
			local chain, n = {}, best
			while n and #chain < M.MAX_CHAIN do
				chain[#chain + 1] = { n.x, n.y }
				n = n.parent
			end
			store_points(c, chain)
		end
		for k = 0, near_count - 1 do
			if list[k] ~= src_region then rg.mark[list[k]] = 0 end
		end
		return c.point_count > 0
	end

	-- The fields of a search context that a local search writes.
	local RESULTS = { "point_count", "point_index", "local_nodes", "local_peak", "local_partial", "reached",
		"reached_end" }

	local function copy_context(c)
		local copy = {}
		for k, v in pairs(c) do copy[k] = v end
		copy.points = {}
		for k, v in pairs(c.points) do copy.points[k] = v end
		return copy
	end

	-- The native search (native/kp_path) gives the same results; KP_PATH=check compares them.
	local function checked_search(self, c, next_region, gx, gy)
		local copy = copy_context(c)
		local ok = lua_search(self, c, next_region, gx, gy)
		local native_ok = native.kp_path.search(self, copy, next_region, gx, gy)
		local diff = {}
		if ok ~= native_ok then diff[#diff + 1] = ("result %s/%s"):format(tostring(ok), tostring(native_ok)) end
		for _, k in ipairs(RESULTS) do
			if c[k] ~= copy[k] then diff[#diff + 1] = ("%s %s/%s"):format(k, tostring(c[k]), tostring(copy[k])) end
		end
		for k = 0, c.point_count - 1 do
			local a, b = c.points[k], copy.points[k]
			if not b or a[1] ~= b[1] or a[2] ~= b[2] then diff[#diff + 1] = ("point %d"):format(k) end
		end
		if #diff > 0 then
			error(("local search: the native search differs (unit %d to %d,%d): %s"):format(c.unit, gx, gy,
				table.concat(diff, ", ")), 0)
		end
		return ok
	end

	if native.kp_path and native.mode == "check" then
		G.local_path_search = checked_search
	elseif native.kp_path then
		local search = native.kp_path.search
		function G:local_path_search(c, next_region, gx, gy) return search(self, c, next_region, gx, gy) end
	else
		G.local_path_search = lua_search
	end
end

return M
