-- Presentation of the simulation's images (plan §7 phase 2 item 3, ADR 0006): once per
-- tick, the sprites visible to the local player are ordered by the native depth key
-- (GetSpriteDepthKey 0x4160b0, ascending, as DrawSortedSprites 0x415b30 draws them) and each
-- sprite's images are taken from the tail to the head (DrawSprite 0x415a80), skipping
-- hidden ones. Every image gets its rank in that order, which becomes its depth, and goes
-- to the mesh family of its draw method. Destination images are split into layers so that
-- no two images of a layer overlap: each layer reads a fresh copy of the index target.
--
-- The simulation is only read. Sprites with equal native keys (the key ORs the sprite index
-- into its low 9 bits, ADR 0006) are ordered by record.
local core = require("game.images.core")
local image = require("game.images.image")
local grpmeta = require("game.data.grpmeta")
local world = require("presentation.world")
local world_images = require("presentation.backend.world_images")

local NONE = core.NONE
local F_MIRROR, F_HIDDEN = core.F_MIRROR, core.F_HIDDEN
local has = image.has
local floor = math.floor
local band = (bit or require("bit")).band
local RANK_RANGE = world_images.RANK_RANGE

local M = {}

M.OVERWRITE, M.DEST = "overwrite", "dest"
-- Draw method -> family. Methods not listed are not drawn yet (TODO: cloak/EMP
-- distortion 2-8, warp texture 12, health bars 11 and size outlines 15 with their
-- materials, plan §7 phase 2 item 1).
M.FAMILY = { [0] = M.OVERWRITE, [1] = M.OVERWRITE, [13] = M.OVERWRITE, [14] = M.OVERWRITE, [16] = M.OVERWRITE,
	[9] = M.DEST, [10] = M.DEST, [17] = M.DEST }
M.SPRITE_KEY_SCALE = 4096 -- depth key * scale + sprite record (exact below 2^53)
M.LAYER_CELL = 64 -- world pixels per cell of the destination overlap grid

-- rows: { owner = function(owner) -> remap row, color = function(param) -> row (method 14),
-- selection = function(param) -> row (method 13), shift = function(param) -> row (method 16),
-- remap = function(remap index) -> 2 * table row + 1 (method 9), shadow = 2 * table row,
-- fire = function(param) -> 2 * table row (method 17) }.
function M.new(w, grp, frames, rows)
	return setmetatable({ w = w, grp = grp, frames = frames, rows = rows, prev_x = {}, prev_y = {}, prev_id = {}, live = {},
		order = {}, layers = 1, drawn = 0 }, { __index = M })
end

-- Global frame of image i in the frame table, nil when its atlas is not loaded.
local function frame_of(p, i)
	local w, g = p.w, p.grp
	local entry = g.grps[w.img.grp[i]]
	local map = p.frames.first[entry.atlas - 1]
	if not map then return nil end
	local first = map[hash(entry.animation)]
	if not first then return nil end
	return first + w.img.frame[i]
end

-- Remap/table row of an image by its draw method and sprite owner.
local function row_of(p, i, method, owner)
	local rows, param = p.rows, p.w.img.param[i]
	if method == 14 then return rows.color(param % 256) end
	if method == 13 then return rows.selection(param % 256) end
	if method == 16 then return rows.shift(param) end
	if method == 9 then return rows.remap(param) end
	if method == 10 then return rows.shadow end
	if method == 17 then return rows.fire(param % 256) end
	return rows.owner(owner)
end

-- Destination layer: one more than the highest layer of an earlier destination image whose
-- rectangle meets this one (grid of LAYER_CELL cells).
local function assign_layer(grid, left, top, right, bottom, stamp)
	local c0, c1 = floor(left / M.LAYER_CELL), floor((right - 1) / M.LAYER_CELL)
	local r0, r1 = floor(top / M.LAYER_CELL), floor((bottom - 1) / M.LAYER_CELL)
	local layer = 0
	for r = r0, r1 do
		for c = c0, c1 do
			local cell = grid[r * 65536 + c]
			if cell and cell.stamp == stamp then
				for k = 1, #cell, 5 do
					if cell[k] < right and left < cell[k + 2] and cell[k + 1] < bottom and top < cell[k + 3] then
						if cell[k + 4] > layer then layer = cell[k + 4] end
					end
				end
			end
		end
	end
	layer = layer + 1
	for r = r0, r1 do
		for c = c0, c1 do
			local key = r * 65536 + c
			local cell = grid[key]
			if not cell or cell.stamp ~= stamp then
				cell = { stamp = stamp }
				grid[key] = cell
			end
			local n = #cell
			cell[n + 1], cell[n + 2], cell[n + 3], cell[n + 4], cell[n + 5] = left, top, right, bottom, layer
		end
	end
	return layer
end

-- Writes the tick's draw list into the backend; returns images drawn and destination
-- layers (the render script makes one copy of the index target per layer). `collect`
-- (optional, for checks) receives { key =, left =, top =, w =, h =, grp =, frame =, global =,
-- flip =, method =, rank =, row =, layer =, owner =, param = } per drawn image, map pixels.
function M:sync(backend, collect)
	local w = self.w
	local spr, img = w.spr, w.img
	local vision = w.local_vision
	local order = self.order
	local n = 0
	for r = 0, w.map_h - 1 do
		local s = w.rows[r].head
		while s ~= NONE do
			if band(spr.visibility[s], vision) ~= 0 then
				n = n + 1
				order[n] = w:sprite_depth_key(s) * M.SPRITE_KEY_SCALE + s
			end
			s = spr.next[s]
		end
	end
	for k = n + 1, #order do order[k] = nil end
	table.sort(order)
	backend:begin()
	self.stamp = (self.stamp or 0) + 1
	self.grid = self.grid or {}
	local rank, layers = 0, 0
	local live, prev_x, prev_y, prev_id = self.live, self.prev_x, self.prev_y, self.prev_id
	local stamp = self.stamp
	for k = 1, n do
		local s = order[k] % M.SPRITE_KEY_SCALE
		local owner = spr.owner[s]
		local i = spr.images[s].tail
		while i ~= NONE do
			local method = img.method[i]
			local family = M.FAMILY[method]
			if family and not has(img.flags[i], F_HIDDEN) and img.grp[i] > 0 then
				local frame = frame_of(self, i)
				local _, _, fw, fh = grpmeta.frame(self.grp, img.grp[i], img.frame[i])
				if frame and fw > 0 and fh > 0 then
					rank = rank + 1
					assert(rank < RANK_RANGE, "images: more ranks than the depth range")
					local left, top = img.x[i], img.y[i]
					local x, y = left + fw / 2, world.world_y(top + fh / 2)
					local px, py = x, y
					-- Interpolate only from the same image's position on the previous tick.
					if live[i] == stamp - 1 and prev_id[i] == img.id[i] then px, py = prev_x[i], prev_y[i] end
					prev_x[i], prev_y[i], prev_id[i], live[i] = x, y, img.id[i], stamp
					local layer = 0
					if family == M.DEST then
						layer = assign_layer(self.grid, left, top, left + fw, top + fh, stamp)
						if layer > layers then layers = layer end
					end
					local flip, row = has(img.flags[i], F_MIRROR), row_of(self, i, method, owner)
					backend:put(family, i, x, y, px, py, frame, flip, rank, row, layer)
					if collect then
						collect[#collect + 1] = { key = i, left = left, top = top, w = fw, h = fh, grp = img.grp[i],
							frame = img.frame[i], global = frame, flip = flip, method = method, rank = rank, row = row,
							layer = layer, owner = owner, param = img.param[i] }
					end
				end
			end
			i = img.prev[i]
		end
	end
	backend:finish()
	self.drawn, self.layers = rank, layers
	return rank, layers
end

return M
