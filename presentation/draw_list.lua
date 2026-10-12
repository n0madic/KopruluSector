-- The native draw list of the simulation's images (plan §7 phase 2 item 3, ADR 0006), pure
-- Lua. Once per tick:
--   1. every sprite in the map rows gets its image bounds refreshed (RefreshRowSpriteBounds
--      0x4165d0 runs before every redraw of the original; bounds follow the current frame,
--      offsets and sprite position);
--   2. the sprites visible to the local player are ordered by the native depth key
--      (GetSpriteDepthKey 0x4160b0, ascending, as DrawSortedSprites 0x415b30 draws them);
--      equal keys (the key ORs the sprite index into its low 9 bits) go by record;
--   3. each sprite's images are taken from the tail to the head (DrawSprite 0x415a80),
--      skipping hidden ones and those whose draw method has no material;
--   4. every drawn image gets its rank in that order. Distortion images (cloak, EMP) read
--      the target through a copy: a distortion opens a composition window (everything ranked
--      before it is composed, then the target is copied) unless it can read the copy of the
--      current window, i.e. no image ranked after that copy meets the pixels it reads and
--      those stay inside the view (no wrap to the other edge). Destination images get a
--      layer inside their window: one more than the highest layer of the earlier
--      destination images of the window whose rectangle meets it.
-- The simulation's records are read, except the bounds, which the original's draw path
-- also writes.
local core = require("game.images.core")
local image = require("game.images.image")
local grpmeta = require("game.data.grpmeta")
local world = require("presentation.world")
local world_depth = require("presentation.backend.world_depth")

local NONE = core.NONE
local VIRTUAL = image.VIRTUAL
local F_MIRROR, F_HIDDEN = core.F_MIRROR, core.F_HIDDEN
local has = image.has
local floor = math.floor
local band = (bit or require("bit")).band
local RANK_RANGE = world_depth.RANK_RANGE

local M = {}

-- Material families: overwriting images write their pixel; destination images map the
-- pixel below through a table; distortion images read displaced pixels of the target.
M.OVERWRITE, M.DEST, M.DISTORT = "overwrite", "dest", "distort"
-- Kinds inside a family (the shader's branch, packed into the image's frame code).
M.KIND_REMAP, M.KIND_WARP, M.KIND_OUTLINE, M.KIND_BAR = 0, 1, 2, 3 -- overwrite
M.KIND_TABLE, M.KIND_CLOAK_BLEND, M.KIND_TRANS50 = 0, 1, 2 -- dest: table; trans50 blend then threshold; trans50
M.KIND_BRIGHT, M.KIND_RAW, M.KIND_CLOAK = 0, 1, 2 -- distort: by brightness, by raw index, then threshold
M.KINDS = 4
-- Draw method -> { family, kind } (installed draw pointers 0x5030d4, docs/research/
-- native-pixel-effects.md).
M.METHODS = {
	[0] = { M.OVERWRITE, M.KIND_REMAP }, [1] = { M.OVERWRITE, M.KIND_REMAP },
	[13] = { M.OVERWRITE, M.KIND_REMAP }, [14] = { M.OVERWRITE, M.KIND_REMAP }, [16] = { M.OVERWRITE, M.KIND_REMAP },
	[12] = { M.OVERWRITE, M.KIND_WARP }, [15] = { M.OVERWRITE, M.KIND_OUTLINE }, [11] = { M.OVERWRITE, M.KIND_BAR },
	[6] = { M.DEST, M.KIND_TRANS50 }, [9] = { M.DEST, M.KIND_TABLE }, [10] = { M.DEST, M.KIND_TABLE },
	[17] = { M.DEST, M.KIND_TABLE }, [5] = { M.DEST, M.KIND_CLOAK_BLEND }, [7] = { M.DEST, M.KIND_CLOAK_BLEND },
	[3] = { M.DISTORT, M.KIND_BRIGHT }, [8] = { M.DISTORT, M.KIND_RAW },
	[2] = { M.DISTORT, M.KIND_CLOAK }, [4] = { M.DISTORT, M.KIND_CLOAK },
}
M.TRANS50 = 5 -- remap table of the cloak blends 5-7 (DAT_00508890; images.dat remapping 5)
-- DrawSprite 0x415a80 calls 0x416bd0: when the sprite's main image draws with method 5..7,
-- trans50 rows 8..15 become the rows of the owner's player colours, and stay so until the
-- next such sprite. An image reading trans50 therefore maps its source through the colour
-- map of the last such owner (none yet: the identity).
M.TRANS50_SETTERS = { [5] = true, [6] = true, [7] = true }
M.PARAM_SPAN = 256 -- threshold row code = owner row * PARAM_SPAN + parameter byte
M.COLOR_SPAN = 65536 -- cloak blend row code += trans50 colour row * COLOR_SPAN
-- Sprites more than this many rows above the view's top row are not drawn (0x445de0).
M.ROWS_ABOVE = 4
M.REMAP = 9
M.SPRITE_KEY_SCALE = 4096 -- depth key * scale + sprite record (exact below 2^53)
M.LAYER_CELL = 64 -- world pixels per cell of the destination overlap grid
M.CELL_ROW = 65536 -- grid key = cell row * CELL_ROW + cell column
M.CELL_FIELDS = 5 -- left, top, right, bottom, layer
-- Destination images get layers only within this many pixels around the view (the others
-- are not drawn: no copy is spent on them); presentation/images.lua replans when the view
-- leaves that margin.
M.LAYER_MARGIN = 128
-- Farthest pixel a distortion reads: the brightness table tops out at 8 ((256 * 255 +
-- 4096) >> 13, 0x445f30); method 8 reads up to the raw index.
M.DISTORT_REACH = { [M.KIND_BRIGHT] = 8, [M.KIND_CLOAK] = 8, [M.KIND_RAW] = 255 }

-- State kept between ticks (sort buffer, overlap grid, positions of the last two ticks,
-- windows, the rectangles drawn since the window's copy, distortion images of the last build).
function M.new()
	return { order = {}, grid = {}, stamp = 0, tick = 0, prev_x = {}, prev_y = {}, prev_id = {}, live = {},
		from_x = {}, from_y = {}, windows = {}, window_count = 0, since = {}, distortions = 0,
		trans50 = false, trans50_start = false }
end

-- Remap/table row of an image by its draw method, parameter, sprite owner and trans50
-- colour owner (TRANS50_SETTERS; false: unchanged rows) (see presentation/images.lua for the
-- `rows` functions). Methods 2, 4, 5 and 7 draw their threshold pass through the owner's
-- colour map (0x501021, set by 0x41d740 per sprite) and compare against the parameter
-- byte; methods 5-7 and 9 with remapping 5 read trans50 through the trans50 colour row.
function M.row_of(rows, method, param, owner, trans50)
	local colour = trans50 and rows.owner(trans50) or 0
	if method == 14 then return rows.color(param % 256) end
	if method == 13 then return rows.selection(param % 256) end
	if method == 16 then return rows.shift(param) end
	if method == 9 then return param == M.TRANS50 and colour or rows.remap(param) end
	if method == 6 then return colour end
	if method == 10 then return rows.shadow end
	if method == 17 then return rows.fire(param % 256) end
	if method == 12 then return rows.warp(param) end
	if method == 15 then return rows.outline() end
	if method == 3 or method == 8 then return 0 end
	if method == 5 or method == 7 then
		return colour * M.COLOR_SPAN + rows.owner(owner) * M.PARAM_SPAN + param % M.PARAM_SPAN
	end
	if method == 2 or method == 4 then
		return rows.owner(owner) * M.PARAM_SPAN + param % M.PARAM_SPAN
	end
	return rows.owner(owner)
end

-- Layer of a destination rectangle; cells are reused across ticks (cell.n counts the live
-- entries of the current stamp).
local function assign_layer(grid, left, top, right, bottom, stamp)
	local c0, c1 = floor(left / M.LAYER_CELL), floor((right - 1) / M.LAYER_CELL)
	local r0, r1 = floor(top / M.LAYER_CELL), floor((bottom - 1) / M.LAYER_CELL)
	local layer = 0
	for r = r0, r1 do
		for c = c0, c1 do
			local cell = grid[r * M.CELL_ROW + c]
			if cell and cell.stamp == stamp then
				for k = 1, cell.n, M.CELL_FIELDS do
					if cell[k] < right and left < cell[k + 2] and cell[k + 1] < bottom and top < cell[k + 3]
						and cell[k + 4] > layer then
						layer = cell[k + 4]
					end
				end
			end
		end
	end
	layer = layer + 1
	for r = r0, r1 do
		for c = c0, c1 do
			local key = r * M.CELL_ROW + c
			local cell = grid[key]
			if not cell then
				cell = { stamp = stamp, n = 0 }
				grid[key] = cell
			elseif cell.stamp ~= stamp then
				cell.stamp, cell.n = stamp, 0
			end
			local n = cell.n
			cell[n + 1], cell[n + 2], cell[n + 3], cell[n + 4], cell[n + 5] = left, top, right, bottom, layer
			cell.n = n + M.CELL_FIELDS
		end
	end
	return layer
end
M.assign_layer = assign_layer

-- Builds the tick's draw list of world `w`. frame_of(grp entry, frame, image) returns the
-- image's global frame, or nil when its atlas is not loaded; virtual images (health bars,
-- boxes: grp entry image.VIRTUAL, sizes in w.vgrp) get theirs from it too. emit(d) is
-- called per drawn image with the reused record d = { key, family, kind, method, owner,
-- param, left, top, w, h, x, y, px, py, frame (global), grp, grp_frame, flip, virtual, rank,
-- layer, window }: left/top/w/h in map pixels, x/y and px/py (previous tick, same image) the
-- frame centre in world coordinates, window the composition window (for a distortion
-- image: the copy it reads).
-- Windows: state.windows[k] = { rank = rank of the distortion image that opened the k-th
-- copy, last = rank of the last distortion reading that copy, layers = destination layers
-- of the window ranked before `rank` }, k = 1..state.window_count; images ranked after
-- `rank` belong to window k + 1. `view` = { left, top, right, bottom } (map pixels) is the
-- target the copies are made of; without it every distortion takes its own copy and every
-- destination image gets a layer; with it destination images farther than LAYER_MARGIN
-- from the view get layer 0 (not drawn). `replan`
-- rebuilds the same tick for another view (the camera moved): interpolation keeps the
-- tick's start positions. Returns images drawn and the destination layers of the last
-- window.
function M.build(state, w, frame_of, emit, view, replan)
	local spr, img = w.spr, w.img
	local vision = w.local_vision
	local order = state.order
	local n = 0
	local first_row = view and math.max(0, floor(view.top / core.TILE) - M.ROWS_ABOVE) or 0
	for r = first_row, w.map_h - 1 do
		local s = w.rows[r].head
		while s ~= NONE do
			w:sprite_refresh_bounds(s)
			if band(spr.visibility[s], vision) ~= 0 then
				n = n + 1
				order[n] = w:sprite_depth_key(s) * M.SPRITE_KEY_SCALE + s
			end
			s = spr.next[s]
		end
	end
	for k = n + 1, #order do order[k] = nil end
	table.sort(order)
	if not replan then state.tick = state.tick + 1 end
	state.stamp = state.stamp + 1
	local tick, grid = state.tick, state.grid
	local live, prev_x, prev_y, prev_id = state.live, state.prev_x, state.prev_y, state.prev_id
	local from_x, from_y = state.from_x, state.from_y
	local distortions = 0
	local windows, since = state.windows, state.since
	local d = state.record or {}
	state.record = d
	local rank, layers, window, drawn_since = 0, 0, 1, 0
	if not replan then state.trans50_start = state.trans50 end
	local trans50 = state.trans50_start
	local margin = M.LAYER_MARGIN
	local ll, lt = view and view.left - margin, view and view.top - margin
	local lr, lb = view and view.right + margin, view and view.bottom + margin
	for k = 1, n do
		local s = order[k] % M.SPRITE_KEY_SCALE
		local owner = spr.owner[s]
		local main = spr.main[s]
		if main ~= NONE and M.TRANS50_SETTERS[img.method[main]] then trans50 = owner end
		local i = spr.images[s].tail
		while i ~= NONE do
			local method = img.method[i]
			local entry = M.METHODS[method]
			local g = img.grp[i]
			local virtual = g == VIRTUAL and w.vgrp[i] ~= nil
			if entry and not has(img.flags[i], F_HIDDEN) and (virtual or (g > 0 and img.frame[i] < w.grp.grps[g].frames)) then
				local family, kind = entry[1], entry[2]
				if method == M.REMAP and img.param[i] == M.TRANS50 then kind = M.KIND_TRANS50 end
				local frame = frame_of(g, img.frame[i], i)
				local fw, fh
				if virtual then
					fw, fh = w.vgrp[i].fw, w.vgrp[i].fh
				else
					local _
					_, _, fw, fh = grpmeta.frame(w.grp, g, img.frame[i])
				end
				if frame and fw > 0 and fh > 0 then
					rank = rank + 1
					assert(rank < RANK_RANGE, "draw_list: more ranks than the depth range")
					local left, top = img.x[i], img.y[i]
					local x, y = left + fw / 2, world.world_y(top + fh / 2)
					local px, py = x, y
					-- Interpolate only from the same image's position on the previous tick.
					if replan then
						if live[i] == tick and prev_id[i] == img.id[i] then px, py = from_x[i], from_y[i] end
					else
						if live[i] == tick - 1 and prev_id[i] == img.id[i] then px, py = prev_x[i], prev_y[i] end
						from_x[i], from_y[i] = px, py
					end
					prev_x[i], prev_y[i], prev_id[i], live[i] = x, y, img.id[i], tick
					local layer = 0
					if family == M.DEST and (not view or (left < lr and ll < left + fw and top < lb and lt < top + fh)) then
						layer = assign_layer(grid, left, top, left + fw, top + fh, state.stamp)
						if layer > layers then layers = layer end
					end
					d.key, d.family, d.kind, d.method, d.owner, d.param = i, family, kind, method, owner, img.param[i]
					d.trans50 = trans50
					d.left, d.top, d.w, d.h, d.x, d.y, d.px, d.py = left, top, fw, fh, x, y, px, py
					d.frame, d.grp, d.grp_frame, d.flip = frame, g, img.frame[i], has(img.flags[i], F_MIRROR)
					d.virtual = virtual
					if family == M.DISTORT then
						distortions = distortions + 1
						-- Pixels read: the frame rows, reach pixels ahead (behind when mirrored).
						local reach = M.DISTORT_REACH[entry[2]]
						local flip = has(img.flags[i], F_MIRROR)
						local rl, rr = flip and left - reach or left, flip and left + fw or left + fw + reach
						-- Outside the view a distortion changes nothing visible: it reads any copy.
						local visible = view == nil or (left < view.right and view.left < left + fw
							and top < view.bottom and view.top < top + fh)
						local join = window > 1 and (not visible or (view ~= nil and rl >= view.left and rr <= view.right))
						for o = 1, (join and visible) and drawn_since or 0, 4 do
							if since[o] < rr and rl < since[o + 2] and since[o + 1] < top + fh and top < since[o + 3] then
								join = false
								break
							end
						end
						if join then
							windows[window - 1].last = rank
						else
							-- A new copy: the next window starts with a fresh overlap grid.
							local opened = windows[window] or {}
							windows[window] = opened
							opened.rank, opened.last, opened.layers = rank, rank, layers
							window, layers, drawn_since = window + 1, 0, 0
							state.stamp = state.stamp + 1
						end
					end
					since[drawn_since + 1], since[drawn_since + 2] = left, top
					since[drawn_since + 3], since[drawn_since + 4] = left + fw, top + fh
					drawn_since = drawn_since + 4
					-- A distortion's window is the copy it reads.
					d.rank, d.layer, d.window = rank, layer, family == M.DISTORT and window - 1 or window
					emit(d)
				end
			end
			i = img.prev[i]
		end
	end
	state.window_count, state.distortions, state.trans50 = window - 1, distortions, trans50
	return rank, layers
end

return M
