-- The native draw list of the simulation's images (plan §7 phase 2 item 3, ADR 0006), pure
-- Lua. Once per tick:
--   1. every sprite in the map rows gets its image bounds refreshed (RefreshRowSpriteBounds
--      0x4165d0 runs before every redraw of the original; bounds follow the current frame,
--      offsets and sprite position);
--   2. the sprites visible to the local player are ordered by the native depth key
--      (GetSpriteDepthKey 0x4160b0, ascending, as DrawSortedSprites 0x415b30 draws them);
--      equal keys (the key ORs the sprite index into its low 9 bits) go by record;
--   3. each sprite's images are taken from the tail to the head (DrawSprite 0x415a80),
--      skipping hidden ones and those whose draw method has no material yet;
--   4. every drawn image gets its rank in that order and, for destination images, its
--      layer: one more than the highest layer of the earlier destination images whose
--      rectangle meets it.
-- The simulation's records are read, except the bounds, which the original's draw path
-- also writes.
local core = require("game.images.core")
local image = require("game.images.image")
local grpmeta = require("game.data.grpmeta")
local world = require("presentation.world")
local world_depth = require("presentation.backend.world_depth")

local NONE = core.NONE
local F_MIRROR, F_HIDDEN = core.F_MIRROR, core.F_HIDDEN
local has = image.has
local floor = math.floor
local band = (bit or require("bit")).band
local RANK_RANGE = world_depth.RANK_RANGE

local M = {}

M.OVERWRITE, M.DEST = "overwrite", "dest"
-- Draw method -> family. Methods not listed are not drawn yet (TODO: cloak/EMP distortion
-- 2-8, warp texture 12, health bars 11 and size outlines 15 with their materials, plan §7
-- phase 2 item 1).
M.FAMILY = { [0] = M.OVERWRITE, [1] = M.OVERWRITE, [13] = M.OVERWRITE, [14] = M.OVERWRITE, [16] = M.OVERWRITE,
	[9] = M.DEST, [10] = M.DEST, [17] = M.DEST }
M.SPRITE_KEY_SCALE = 4096 -- depth key * scale + sprite record (exact below 2^53)
M.LAYER_CELL = 64 -- world pixels per cell of the destination overlap grid
M.CELL_ROW = 65536 -- grid key = cell row * CELL_ROW + cell column
M.CELL_FIELDS = 5 -- left, top, right, bottom, layer

-- State kept between ticks (sort buffer, overlap grid, previous positions).
function M.new()
	return { order = {}, grid = {}, stamp = 0, prev_x = {}, prev_y = {}, prev_id = {}, live = {} }
end

-- Remap/table row of an image by its draw method and sprite owner (see presentation/images.lua
-- for the `rows` functions).
function M.row_of(rows, method, param, owner)
	if method == 14 then return rows.color(param % 256) end
	if method == 13 then return rows.selection(param % 256) end
	if method == 16 then return rows.shift(param) end
	if method == 9 then return rows.remap(param) end
	if method == 10 then return rows.shadow end
	if method == 17 then return rows.fire(param % 256) end
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

-- Builds the tick's draw list of world `w`. frame_of(grp entry, frame) returns the image's
-- global frame, or nil when its atlas is not loaded. emit(d) is called per drawn image with
-- the reused record d = { key, family, method, owner, param, left, top, w, h, x, y, px, py,
-- frame (global), grp, grp_frame, flip, rank, layer }: left/top/w/h in map pixels, x/y and
-- px/py (previous tick, same image) the frame centre in world coordinates. Returns images
-- drawn and destination layers.
function M.build(state, w, frame_of, emit)
	local spr, img = w.spr, w.img
	local vision = w.local_vision
	local order = state.order
	local n = 0
	for r = 0, w.map_h - 1 do
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
	state.stamp = state.stamp + 1
	local stamp, grid = state.stamp, state.grid
	local live, prev_x, prev_y, prev_id = state.live, state.prev_x, state.prev_y, state.prev_id
	local d = state.record or {}
	state.record = d
	local rank, layers = 0, 0
	for k = 1, n do
		local s = order[k] % M.SPRITE_KEY_SCALE
		local owner = spr.owner[s]
		local i = spr.images[s].tail
		while i ~= NONE do
			local method = img.method[i]
			local family = M.FAMILY[method]
			local g = img.grp[i]
			if family and g > 0 and not has(img.flags[i], F_HIDDEN) and img.frame[i] < w.grp.grps[g].frames then
				local frame = frame_of(g, img.frame[i])
				local _, _, fw, fh = grpmeta.frame(w.grp, g, img.frame[i])
				if frame and fw > 0 and fh > 0 then
					rank = rank + 1
					assert(rank < RANK_RANGE, "draw_list: more ranks than the depth range")
					local left, top = img.x[i], img.y[i]
					local x, y = left + fw / 2, world.world_y(top + fh / 2)
					local px, py = x, y
					-- Interpolate only from the same image's position on the previous tick.
					if live[i] == stamp - 1 and prev_id[i] == img.id[i] then px, py = prev_x[i], prev_y[i] end
					prev_x[i], prev_y[i], prev_id[i], live[i] = x, y, img.id[i], stamp
					local layer = 0
					if family == M.DEST then
						layer = assign_layer(grid, left, top, left + fw, top + fh, stamp)
						if layer > layers then layers = layer end
					end
					d.key, d.family, d.method, d.owner, d.param = i, family, method, owner, img.param[i]
					d.left, d.top, d.w, d.h, d.x, d.y, d.px, d.py = left, top, fw, fh, x, y, px, py
					d.frame, d.grp, d.grp_frame, d.flip = frame, g, img.frame[i], has(img.flags[i], F_MIRROR)
					d.rank, d.layer = rank, layer
					emit(d)
				end
			end
			i = img.prev[i]
		end
	end
	return rank, layers
end

return M
