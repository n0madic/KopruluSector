-- Presentation of the simulation's images (plan §7 phase 2 item 3, ADR 0006): once per tick
-- the native draw list (presentation/draw_list.lua) goes into the world_images backend,
-- each image with the remap or table row of its draw method.
local draw_list = require("presentation.draw_list")
local image = require("game.images.image")

local M = {}

M.OVERWRITE, M.DEST, M.DISTORT = draw_list.OVERWRITE, draw_list.DEST, draw_list.DISTORT
local VIRTUAL_SIZE = 256 -- a virtual frame's size travels as width * 256 + height

-- w: game.images world; grp: game.data.grpmeta; frames: world_images.frame_table; rows: the
-- `rows` of presentation/world_lookups.lua (owner, color, selection, shift, remap, shadow,
-- fire, outline); the warp texture row (method 12: the global frame of texture frame
-- `param`) and the health bar row (method 11: its slot in opts.bars) are added here.
-- opts.bars: presentation/bars.lua; opts.bar_of(unit) -> the unit's bar values
-- (presentation/health_bar.lua from_unit), for health-bar images (their parameter is the unit).
function M.new(w, grp, frames, rows, opts)
	opts = opts or {}
	local self = setmetatable({ w = w, grp = grp, frames = frames, state = draw_list.new(),
		first_frame = {}, layers = 0, drawn = 0, bars = opts.bars, bar_of = opts.bar_of }, { __index = M })
	local texture = w:grp_of(image.WARP_TEXTURE)
	self.rows = setmetatable({ warp = function(param)
		return assert(self.frame_of(texture, param), "images: warp texture frame is not loaded")
	end }, { __index = rows })
	-- Global first frame of a GRP entry, hashed once per entry; false when its atlas is not loaded.
	self.frame_of = function(g, frame)
		if g == image.VIRTUAL then return frames.empty end
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
		local row, layer = nil, d.layer
		if d.kind == draw_list.KIND_BAR then
			assert(self.bars and self.bar_of, "images: a health bar needs opts.bars and opts.bar_of")
			row = self.bars:put(d.key, self.bar_of(d.param), d.w, d.h)
		else
			row = draw_list.row_of(self.rows, d.method, d.param, d.owner, d.trans50)
		end
		if d.virtual then layer = d.w * VIRTUAL_SIZE + d.h end
		self.backend:put(d.family, d.kind, d.key, d.x, d.y, d.px, d.py, d.frame, d.flip, d.rank, row, layer)
		local collect = self.collect
		if collect then
			collect[#collect + 1] = { key = d.key, left = d.left, top = d.top, w = d.w, h = d.h, grp = d.grp,
				frame = d.grp_frame, global = d.frame, flip = d.flip, method = d.method, rank = d.rank, row = row,
				layer = d.layer, window = d.window, owner = d.owner, param = d.param, virtual = d.virtual }
		end
	end
	return self
end

-- Writes the tick's draw list into the backend with its composition windows; `view` = { left,
-- top, right, bottom } is the map rectangle the index target shows; `replan` rebuilds the
-- same tick for a moved view (draw_list.build).
-- Returns images drawn and the destination layers of the last window. `collect`
-- (optional, for checks) receives { key =, left =, top =, w =, h =, grp =, frame =, global =,
-- flip =, method =, rank =, row =, layer =, window =, owner =, param =, virtual = } per drawn
-- image, map pixels.
function M:sync(backend, collect, view, replan)
	self.backend, self.collect = backend, collect
	backend:begin()
	if self.bars then
		backend:attach(self.bars)
		self.bars:begin()
	end
	local drawn, layers = draw_list.build(self.state, self.w, self.frame_of, self.emit, view, replan)
	if self.bars then
		self.bars:finish()
		if self.bars.dirty then backend:touch() end
	end
	backend:finish(self.state.windows, self.state.window_count, layers)
	self.backend, self.collect = nil, nil
	self.drawn, self.layers = drawn, layers
	if view then
		local planned = self.planned or {}
		planned.left, planned.top, planned.right, planned.bottom = view.left, view.top, view.right, view.bottom
		self.planned = planned
	else
		self.planned = nil
	end
	return drawn, layers
end

-- True when the composition plan was made for another view than `view`: distortion images
-- depend on the exact view, destination layers on the view within draw_list.LAYER_MARGIN.
function M:needs_replan(view)
	local p = self.planned
	if view == nil or p == nil then return false end
	if self.state.distortions > 0 and (view.left ~= p.left or view.top ~= p.top) then return true end
	local m = draw_list.LAYER_MARGIN
	return view.left < p.left - m or view.top < p.top - m or view.right > p.right + m or view.bottom > p.bottom + m
end

return M
