-- World image backend chosen in ADR 0002 (spike 04): one mesh per atlas whose vertices hold
-- only (slot, corner); per-image data lives in an RGBA32F texture read in the vertex shader
-- (render/materials/indexed_vt.vp):
--   image_data[2 slot]     = (x, y, x - previous x, y - previous y) in 1/256 world pixels
--   image_data[2 slot + 1] = (remap row * 65536 + frame * 2 + hflip, -, -, -)
--   frame_table[2 f]       = (u0, v0, u1, v1), frame_table[2 f + 1] = (width, height, rotated, -)
-- Positions are the centre of the frame rectangle in world pixels (y up). Slots are written
-- only when a value changes (a shadow table mirrors the stream: every stream access is a C
-- call, spike 10) and the texture is uploaded on the frame after the write, never on the
-- write frame, so a tick never pays the fixed set_texture cost twice (spike 15).
local gpu_buffers = require("presentation.backend.gpu_buffers")

local M = {}

local Backend = {}
Backend.__index = Backend

local DATA_WIDTH = 256
local RGBA32F = gpu_buffers.RGBA32F
local TEXTURES = { "texture0", "texture2", "texture3" }

local function float_texture(path, width, height, buf)
	local params = gpu_buffers.rgba32f_params(width, height)
	return resource.create_texture(path, params, buf), params
end

-- Frames of an atlas resource in animation order; returns frames ({ u0, v0, u1, v1, w, h, rotated }),
-- the index of the first frame of each animation id (hash) and the atlas texture.
-- (u0, v1) is the texture coordinate of the frame's bottom-left corner and (u1, v0) of its
-- top-right corner. bob may pack a frame rotated by 90 degrees; then the shader swaps the
-- axes (render/materials/indexed_vt.vp). Orientation checked by reading back the index
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
				"mesh_texture: unexpected atlas frame corners")
			frames[#frames + 1] = { bl.uv[1], tr.uv[2], tr.uv[1], bl.uv[2], g.width, g.height, rotated and 1 or 0 }
		end
	end
	return frames, first, atlas.texture
end

-- opts: mesh (url of a mesh component using indexed_vt.material with its own .buffer),
-- frames (from atlas_frames), texture (atlas texture path), capacity, tag (unique name).
function M.new(opts)
	local self = setmetatable({ mesh = opts.mesh, cap = opts.capacity, empty = #opts.frames * 2, used = 0 }, Backend)
	local frames = opts.frames
	-- Frame table, plus a zero-size frame that collapses unused slots.
	local fbuf, fs = gpu_buffers.zeroed((#frames + 1) * 2, RGBA32F)
	for k, f in ipairs(frames) do
		local o = (k - 1) * 8
		fs[o + 1], fs[o + 2], fs[o + 3], fs[o + 4], fs[o + 5], fs[o + 6], fs[o + 7] = f[1], f[2], f[3], f[4], f[5], f[6], f[7] or 0
	end
	self.frame_table = float_texture("/kp/frames_" .. opts.tag .. ".texturec", (#frames + 1) * 2, 1, fbuf)
	self.data_h = math.ceil(self.cap * 2 / DATA_WIDTH)
	self.data_buf, self.data = gpu_buffers.zeroed(DATA_WIDTH * self.data_h, RGBA32F)
	self.shadow = {}
	local d, sh = self.data, self.shadow
	for k = 1, #d do sh[k] = 0 end
	for slot = 0, self.cap - 1 do
		local o = slot * 8 + 5
		d[o], sh[o] = self.empty, self.empty
	end
	self.data_path, self.data_params = float_texture("/kp/data_" .. opts.tag .. ".texturec", DATA_WIDTH, self.data_h,
		self.data_buf)
	resource.set_buffer(go.get(self.mesh, "vertices"), gpu_buffers.slot_quads(self.cap))
	-- The mesh holds a reference to each texture set on it; destroy restores these so the
	-- created textures (and the atlas) can be freed.
	self.original = {}
	for _, name in ipairs(TEXTURES) do self.original[name] = go.get(self.mesh, name) end
	go.set(self.mesh, "texture0", hash(opts.texture))
	go.set(self.mesh, "texture2", self.data_path)
	go.set(self.mesh, "texture3", self.frame_table)
	self.interp = vmath.vector4(1, 0, 0, 0)
	return self
end

-- images: array in depth order of { x =, y =, px =, py =, frame =, flip =, row = } (px/py:
-- position at the previous tick, defaults to x/y). Writes changed values only.
function Backend:write(images)
	local d, sh, n = self.data, self.shadow, #images
	assert(n <= self.cap, "mesh_texture: more images than capacity")
	local writes = 0
	for i = 1, n do
		local img = images[i]
		local o = (i - 1) * 8
		local x, y = img.x * 256, img.y * 256
		local v3, v4 = x - (img.px or img.x) * 256, y - (img.py or img.y) * 256
		local v5 = (img.row or 0) * 65536 + img.frame * 2 + (img.flip and 1 or 0)
		if sh[o + 1] ~= x then d[o + 1] = x; sh[o + 1] = x; writes = writes + 1 end
		if sh[o + 2] ~= y then d[o + 2] = y; sh[o + 2] = y; writes = writes + 1 end
		if sh[o + 3] ~= v3 then d[o + 3] = v3; sh[o + 3] = v3; writes = writes + 1 end
		if sh[o + 4] ~= v4 then d[o + 4] = v4; sh[o + 4] = v4; writes = writes + 1 end
		if sh[o + 5] ~= v5 then d[o + 5] = v5; sh[o + 5] = v5; writes = writes + 1 end
	end
	for slot = n, self.used - 1 do
		local o = slot * 8 + 5
		if sh[o] ~= self.empty then d[o] = self.empty; sh[o] = self.empty; writes = writes + 1 end
	end
	self.used = n
	if writes > 0 and not self.pending then self.pending, self.pending_frames = true, 0 end
	return writes
end

-- Per frame: upload pending data on the frame after the write and set the interpolation.
function Backend:sync(alpha)
	local interp = alpha or 1
	if self.pending then
		if self.pending_frames >= 1 then
			resource.set_texture(self.data_path, self.data_params, self.data_buf)
			self.pending = false
		else
			self.pending_frames = self.pending_frames + 1
			interp = 1
		end
	end
	self.interp.x = interp
	go.set(self.mesh, "interp", self.interp)
end

function Backend:destroy()
	self:write({})
	resource.set_texture(self.data_path, self.data_params, self.data_buf)
	for _, name in ipairs(TEXTURES) do go.set(self.mesh, name, self.original[name]) end
	resource.release(self.data_path)
	resource.release(self.frame_table)
end

return M
