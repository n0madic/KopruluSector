-- Presentation of the simulation's images (plan §7 phase 2 item 3, ADR 0006): once per tick
-- the native draw list (presentation/draw_list.lua) goes into the world_images backend,
-- each image with the remap or table row of its draw method.
local draw_list = require("presentation.draw_list")
local image = require("game.images.image")

local M = {}

M.OVERWRITE, M.DEST, M.DISTORT = draw_list.OVERWRITE, draw_list.DEST, draw_list.DISTORT

-- w: game.images world; grp: game.data.grpmeta; frames: world_images.frame_table; rows: the
-- `rows` of presentation/world_lookups.lua (owner, color, selection, shift, remap, shadow,
-- fire, outline); the warp texture row (method 12: the global frame of texture frame
-- `param`) is added here.
function M.new(w, grp, frames, rows)
	local self = setmetatable({ w = w, grp = grp, frames = frames, state = draw_list.new(),
		first_frame = {}, layers = 0, drawn = 0 }, { __index = M })
	local texture = w:grp_of(image.WARP_TEXTURE)
	self.rows = setmetatable({ warp = function(param)
		return assert(self.frame_of(texture, param), "images: warp texture frame is not loaded")
	end }, { __index = rows })
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
		self.backend:put(d.family, d.kind, d.key, d.x, d.y, d.px, d.py, d.frame, d.flip, d.rank, row, d.layer)
		local collect = self.collect
		if collect then
			collect[#collect + 1] = { key = d.key, left = d.left, top = d.top, w = d.w, h = d.h, grp = d.grp,
				frame = d.grp_frame, global = d.frame, flip = d.flip, method = d.method, rank = d.rank, row = row,
				layer = d.layer, window = d.window, owner = d.owner, param = d.param }
		end
	end
	return self
end

-- Writes the tick's draw list into the backend with its composition windows; `view` = { left,
-- top, right, bottom } is the map rectangle the index target shows; `replan` rebuilds the
-- same tick for a moved view (draw_list.build).
-- Returns images drawn and the destination layers of the last window. `collect`
-- (optional, for checks) receives { key =, left =, top =, w =, h =, grp =, frame =, global =,
-- flip =, method =, rank =, row =, layer =, window =, owner =, param = } per drawn image, map pixels.
function M:sync(backend, collect, view, replan)
	self.backend, self.collect = backend, collect
	backend:begin()
	local drawn, layers = draw_list.build(self.state, self.w, self.frame_of, self.emit, view, replan)
	backend:finish(self.state.windows, self.state.window_count, layers)
	self.backend, self.collect = nil, nil
	self.drawn, self.layers = drawn, layers
	self.view_left, self.view_top = view and view.left, view and view.top
	return drawn, layers
end

-- True when the composition plan depends on a view other than `view`: there are
-- distortion images and the view moved since the last sync.
function M:needs_replan(view)
	return self.state.distortions > 0 and view ~= nil and (view.left ~= self.view_left or view.top ~= self.view_top)
end

return M
