-- Presentation of the simulation's images (plan §7 phase 2 item 3, ADR 0006): once per tick
-- the native draw list (presentation/draw_list.lua) goes into the world_images backend,
-- each image with the remap or table row of its draw method.
local draw_list = require("presentation.draw_list")

local M = {}

M.OVERWRITE, M.DEST = draw_list.OVERWRITE, draw_list.DEST

-- w: game.images world; grp: game.data.grpmeta; frames: world_images.frame_table; rows: {
-- owner = function(owner) -> remap row, color = function(param) -> row (method 14),
-- selection = function(param) -> row (method 13), shift = function(param) -> row (method 16),
-- remap = function(remap index) -> 2 * table row + 1 (method 9), shadow = 2 * table row,
-- fire = function(param) -> 2 * table row (method 17) }.
function M.new(w, grp, frames, rows)
	local self = setmetatable({ w = w, grp = grp, frames = frames, rows = rows, state = draw_list.new(),
		first_frame = {}, layers = 0, drawn = 0 }, { __index = M })
	-- Global first frame of a GRP entry, hashed once per entry; false when its atlas is not loaded.
	self.frame_of = function(g, frame)
		local first = self.first_frame[g]
		if first == nil then
			local entry = grp.grps[g]
			local map = frames.first[entry.atlas - 1]
			first = map and map[hash(entry.animation)] or false
			self.first_frame[g] = first
		end
		return first and first + frame or nil
	end
	self.emit = function(d)
		local row = draw_list.row_of(self.rows, d.method, d.param, d.owner)
		self.backend:put(d.family, d.key, d.x, d.y, d.px, d.py, d.frame, d.flip, d.rank, row, d.layer)
		local collect = self.collect
		if collect then
			collect[#collect + 1] = { key = d.key, left = d.left, top = d.top, w = d.w, h = d.h, grp = d.grp,
				frame = d.grp_frame, global = d.frame, flip = d.flip, method = d.method, rank = d.rank, row = row,
				layer = d.layer, owner = d.owner, param = d.param }
		end
	end
	return self
end

-- Writes the tick's draw list into the backend; returns images drawn and destination
-- layers (the render script makes one copy of the index target per layer). `collect`
-- (optional, for checks) receives { key =, left =, top =, w =, h =, grp =, frame =, global =,
-- flip =, method =, rank =, row =, layer =, owner =, param = } per drawn image, map pixels.
function M:sync(backend, collect)
	self.backend, self.collect = backend, collect
	backend:begin()
	local drawn, layers = draw_list.build(self.state, self.w, self.frame_of, self.emit)
	backend:finish()
	self.backend, self.collect = nil, nil
	self.drawn, self.layers = drawn, layers
	return drawn, layers
end

return M
