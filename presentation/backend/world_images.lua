-- World image backend (ADR 0002, composition by depth rank in ADR 0006): one mesh per
-- material family, all reading one RGBA32F image-data texture (render/materials/world.vp):
--   image_data[2 slot]     = (x, y, x - previous x, y - previous y) in 1/256 world pixels
--   image_data[2 slot + 1] = ((frame * KINDS + kind) * 2 + hflip, rank, row, layer)
-- and one frame table of every loaded atlas:
--   frame_table[2 f] = (u0, v0, u1, v1), frame_table[2 f + 1] = (width, height, rotated, atlas).
-- Each family owns a fixed slot range; an image keeps its slot while it stays in the family
-- (slots are stable by image key), so a still image changes nothing. Values are written
-- only when they change (a shadow table mirrors the stream) and the texture is uploaded
-- on the frame after the write, at most once per frame (spike 15).
local gpu_buffers = require("presentation.backend.gpu_buffers")
local slots = require("presentation.backend.slots")
local world_depth = require("presentation.backend.world_depth")
local draw_list = require("presentation.draw_list")
local world_plan = require("presentation.backend.world_plan")

local M = {}

M.DATA_WIDTH = 256
M.FRAME_WIDTH = 1024
M.ATLAS_SLOTS = 6
M.RANK_RANGE = world_depth.RANK_RANGE
M.TEXELS_PER_SLOT = 2
M.KINDS = draw_list.KINDS

-- Frames of an atlas resource in animation order; returns frames ({ u0, v0, u1, v1, w, h, rotated }),
-- the index of the first frame of each animation id (hash) and the atlas texture.
-- (u0, v1) is the texture coordinate of the frame's bottom-left corner and (u1, v0) of its
-- top-right corner. bob may pack a frame rotated by 90 degrees; then the shader swaps the
-- axes (render/materials/world.vp). Orientation checked by reading back the index
-- target in the web viewer (docs/phase1.md, item 7).
function M.atlas_frames(atlas_path)
	local atlas = resource.get_atlas(atlas_path)
	local info = resource.get_texture_info(atlas.texture)
	local frames, first = {}, {}
	for _, anim in ipairs(atlas.animations) do
		first[hash(anim.id)] = #frames
		for _, gi in ipairs(anim.frames) do
			local g = atlas.geometries[gi]
			-- Corners from the quad's vertices (trim mode off: four corners) paired with their
			-- atlas UVs (texture pixels counted from the bottom, while the texture's top row is
			-- v = 0). The vertex y axis points down here: read back, the pairing taken as y up
			-- draws every frame upside down.
			local bl, tr, br
			for k = 1, #g.vertices, 2 do
				local x, y = g.vertices[k], -g.vertices[k + 1]
				local uv = { g.uvs[k] / info.width, 1 - g.uvs[k + 1] / info.height }
				if not bl or x < bl.x or (x == bl.x and y < bl.y) then bl = { x = x, y = y, uv = uv } end
				if not tr or x > tr.x or (x == tr.x and y > tr.y) then tr = { x = x, y = y, uv = uv } end
				if not br or x > br.x or (x == br.x and y < br.y) then br = { x = x, y = y, uv = uv } end
			end
			-- Unrotated, the bottom-right corner shares v with bottom-left; rotated, it shares u.
			local rotated = br.uv[1] == bl.uv[1]
			assert(rotated == (g.rotated == true) and (rotated and br.uv[2] == tr.uv[2] or br.uv[2] == bl.uv[2]),
				"world_images: unexpected atlas frame corners")
			frames[#frames + 1] = { bl.uv[1], tr.uv[2], tr.uv[1], bl.uv[2], g.width, g.height, rotated and 1 or 0 }
		end
	end
	return frames, first, atlas.texture
end

local Backend = {}
Backend.__index = Backend

-- atlases: array (atlas slot 0..5 at position slot + 1) of atlas resource paths or false;
-- `count` is the number of atlases of grpmeta, which must fit the shaders' six samplers.
-- Returns { path = frame table texture, first = { [slot] = { [animation hash] = global
-- frame } }, textures = { [slot] = atlas texture path }, rotated = { [global frame] = true
-- for frames bob packed rotated }, empty = zero-size frame index }.
function M.frame_table(atlases, count, tag)
	assert(count <= M.ATLAS_SLOTS, ("world_images: %d atlases, the shaders sample %d"):format(count, M.ATLAS_SLOTS))
	local all, first, textures, rotated = {}, {}, {}, {}
	for slot = 0, M.ATLAS_SLOTS - 1 do
		local path = atlases[slot + 1]
		if path then
			local frames, local_first, texture = M.atlas_frames(path)
			local base = #all
			local map = {}
			for id, f in pairs(local_first) do map[id] = base + f end
			for _, f in ipairs(frames) do
				all[#all + 1] = { f[1], f[2], f[3], f[4], f[5], f[6], f[7] or 0, slot }
				if f[7] == 1 then rotated[#all - 1] = true end
			end
			first[slot], textures[slot] = map, texture
		end
	end
	local empty = #all
	local texels = (empty + 1) * 2
	local height = math.ceil(texels / M.FRAME_WIDTH)
	local buf, s = gpu_buffers.zeroed(M.FRAME_WIDTH * height, gpu_buffers.RGBA32F)
	for k, f in ipairs(all) do
		local o = (k - 1) * 8
		for j = 1, 8 do s[o + j] = f[j] end
	end
	local path = "/kp/frame_table_" .. tag .. ".texturec"
	return { path = resource.create_texture(path, gpu_buffers.rgba32f_params(M.FRAME_WIDTH, height), buf),
		first = first, textures = textures, rotated = rotated, empty = empty }
end

-- opts: families = array of { name =, mesh = url (a world family material with its own
-- .buffer), capacity = }, empty = zero-size frame index, tag = unique name.
function M.new(opts)
	local self = setmetatable({ empty = opts.empty, slots = slots.new(opts.families) }, Backend)
	local total = self.slots.total
	self.data_h = math.ceil(total * M.TEXELS_PER_SLOT / M.DATA_WIDTH)
	self.data_buf, self.data = gpu_buffers.zeroed(M.DATA_WIDTH * self.data_h, gpu_buffers.RGBA32F)
	self.shadow = {}
	local d, sh = self.data, self.shadow
	for k = 1, #d do sh[k] = 0 end
	for slot = 0, total - 1 do
		local o = slot * 8 + 5
		d[o], sh[o] = self.empty * M.KINDS * 2, self.empty * M.KINDS * 2
	end
	self.data_params = gpu_buffers.rgba32f_params(M.DATA_WIDTH, self.data_h)
	self.data_path = resource.create_texture("/kp/image_data_" .. opts.tag .. ".texturec", self.data_params,
		self.data_buf)
	for _, f in ipairs(opts.families) do
		resource.set_buffer(go.get(f.mesh, "vertices"), gpu_buffers.slot_quads(f.capacity))
		go.set(f.mesh, "slot_base", vmath.vector4(self.slots.families[f.name].base, 0, 0, 0))
	end
	-- A freed slot draws nothing: the zero-size frame, and layer 0 (an overwriting slot's
	-- nonzero layer is a virtual frame size, which world.vp would draw at that size).
	self.release = function(slot)
		self:write(slot * 8 + 5, self.empty * M.KINDS * 2)
		self:write(slot * 8 + 8, 0)
	end
	-- Composition plan of the data being written (pending) and of the uploaded data
	-- (current): plan.windows[k] = { rank =, last =, layers = } for k = 1..plan.count,
	-- plan.layers (presentation/draw_list.lua).
	self.pending_plan = { windows = {}, count = 0, layers = 0 }
	self.plan = { windows = {}, count = 0, layers = 0 }
	world_plan.current = self.plan
	self.attached = {}
	return self
end

-- `obj:upload()` runs with every upload of the image data (textures the data refers to,
-- such as presentation/bars.lua).
function Backend:attach(obj)
	self.attached[obj] = true
end

-- Forces an upload with this tick's data (an attached texture changed alone).
function Backend:touch()
	self.writes = self.writes + 1
end

-- Starts a tick's writes.
function Backend:begin()
	self.writes = 0
	self.slots:begin()
end

function Backend:write(o, v)
	if self.shadow[o] ~= v then
		self.data[o], self.shadow[o] = v, v
		self.writes = self.writes + 1
	end
end

-- One image of the tick: family name, kind inside the family (presentation/draw_list.lua),
-- image key, position and previous position (world pixels, centre of the frame rectangle),
-- global frame, flip, rank, row and layer (for an overwriting virtual image: its frame
-- size, width * 256 + height, render/materials/world.vp). Returns false (and writes nothing)
-- when the family's slots are full.
function Backend:put(family, kind, key, x, y, px, py, frame, flip, rank, row, layer)
	local slot = self.slots:slot(family, key)
	if not slot then return false end
	local o = slot * 8
	x, y = x * 256, y * 256
	self:write(o + 1, x)
	self:write(o + 2, y)
	self:write(o + 3, x - px * 256)
	self:write(o + 4, y - py * 256)
	self:write(o + 5, (frame * M.KINDS + kind) * 2 + (flip and 1 or 0))
	self:write(o + 6, rank)
	self:write(o + 7, row)
	self:write(o + 8, layer)
	return true
end

local function copy_plan(to, windows, count, layers)
	for k = 1, count do
		local w = to.windows[k] or {}
		to.windows[k] = w
		w.rank, w.last, w.layers = windows[k].rank, windows[k].last, windows[k].layers
	end
	to.count, to.layers = count, layers
end

-- Ends the tick: slots not written this tick are emptied and freed; windows[1..count] and
-- layers are the tick's composition plan (draw_list.build).
function Backend:finish(windows, count, layers)
	self.slots:finish(self.release)
	copy_plan(self.pending_plan, windows, count, layers)
	if self.writes > 0 and not self.pending then self.pending, self.pending_frames = true, 0 end
	if not self.pending then copy_plan(self.plan, windows, count, layers) end
	return self.writes
end

-- Per frame: uploads pending data on the frame after the write. Returns the interpolation
-- to use (1 on a write frame, whose texture still holds the previous tick); self.plan is
-- the composition plan of the uploaded data.
function Backend:sync(alpha)
	if self.pending then
		if self.pending_frames >= 1 then
			resource.set_texture(self.data_path, self.data_params, self.data_buf)
			for obj in pairs(self.attached) do obj:upload() end
			self.pending = false
			local p = self.pending_plan
			copy_plan(self.plan, p.windows, p.count, p.layers)
		else
			self.pending_frames = self.pending_frames + 1
			return 1
		end
	end
	return alpha or 1
end

function Backend:destroy()
	if world_plan.current == self.plan then world_plan.current = world_plan.EMPTY end
	resource.release(self.data_path)
end

return M
