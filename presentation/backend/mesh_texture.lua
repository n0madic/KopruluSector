-- World image backend chosen in ADR 0002 (spike 04): one mesh per atlas whose vertices hold
-- only (slot, corner); per-image data lives in an RGBA32F texture read in the vertex shader
-- (render/materials/indexed_vt.vp):
--   image_data[2 slot]     = (x, y, x - previous x, y - previous y) in 1/256 world pixels
--   image_data[2 slot + 1] = (remap row * 65536 + frame * 2 + hflip, -, -, -)
--   frame_table[2 f]       = (u0, v0, u1, v1), frame_table[2 f + 1] = (width, height, -, -)
-- Positions are the centre of the frame rectangle in world pixels (y up). Slots are written
-- only when a value changes (a shadow table mirrors the stream: every stream access is a C
-- call, spike 10) and the texture is uploaded on the frame after the write, never on the
-- write frame, so a tick never pays the fixed set_texture cost twice (spike 15).
local M = {}

local Backend = {}
Backend.__index = Backend

local DATA_WIDTH = 256
local RGBA32F = { { name = hash("rgba"), type = buffer.VALUE_TYPE_FLOAT32, count = 4 } }
local MESH_STREAMS = {
	{ name = hash("position"), type = buffer.VALUE_TYPE_FLOAT32, count = 3 },
	{ name = hash("normal"), type = buffer.VALUE_TYPE_FLOAT32, count = 3 },
}
local STREAM = hash("rgba")

local function float_texture(path, width, height, buf)
	local params = { type = graphics.TEXTURE_TYPE_2D, width = width, height = height,
		format = graphics.TEXTURE_FORMAT_RGBA32F }
	return resource.create_texture(path, params, buf), params
end

-- Frames of an atlas resource in animation order; returns frames ({ u0, v0, u1, v1, w, h })
-- and the index of the first frame of each animation id (hash).
function M.atlas_frames(atlas_path)
	local atlas = resource.get_atlas(atlas_path)
	local info = resource.get_texture_info(atlas.texture)
	local frames, first = {}, {}
	for _, anim in ipairs(atlas.animations) do
		first[hash(anim.id)] = #frames
		for _, gi in ipairs(anim.frames) do
			local g = atlas.geometries[gi]
			assert(not g.rotated, "mesh_texture: rotated atlas frames are not supported")
			local u0, v0, u1, v1 = math.huge, math.huge, -math.huge, -math.huge
			for k = 1, #g.uvs, 2 do
				-- Atlas UVs are pixels counted from the bottom; the texture's top row is v = 0.
				local u, v = g.uvs[k] / info.width, 1 - g.uvs[k + 1] / info.height
				u0, u1 = math.min(u0, u), math.max(u1, u)
				v0, v1 = math.min(v0, v), math.max(v1, v)
			end
			frames[#frames + 1] = { u0, v1, u1, v0, g.width, g.height }
		end
	end
	return frames, first, atlas.texture
end

-- opts: mesh (url of a mesh component using indexed_vt.material with its own .buffer),
-- frames (from atlas_frames), texture (atlas texture handle), capacity, tag (unique name).
function M.new(opts)
	local self = setmetatable({ mesh = opts.mesh, cap = opts.capacity, empty = #opts.frames * 2, used = 0 }, Backend)
	local frames = opts.frames
	-- Frame table, plus a zero-size frame that collapses unused slots. buffer.create does not
	-- zero its memory, so every element is written.
	local fbuf = buffer.create((#frames + 1) * 2, RGBA32F)
	local fs = buffer.get_stream(fbuf, STREAM)
	for k = 1, #fs do fs[k] = 0 end
	for k, f in ipairs(frames) do
		local o = (k - 1) * 8
		fs[o + 1], fs[o + 2], fs[o + 3], fs[o + 4], fs[o + 5], fs[o + 6] = f[1], f[2], f[3], f[4], f[5], f[6]
	end
	self.frame_table = float_texture("/kp/frames_" .. opts.tag .. ".texturec", (#frames + 1) * 2, 1, fbuf)
	self.data_h = math.ceil(self.cap * 2 / DATA_WIDTH)
	self.data_buf = buffer.create(DATA_WIDTH * self.data_h, RGBA32F)
	self.data = buffer.get_stream(self.data_buf, STREAM)
	self.shadow = {}
	local d, sh = self.data, self.shadow
	for k = 1, DATA_WIDTH * self.data_h * 4 do d[k], sh[k] = 0, 0 end
	for slot = 0, self.cap - 1 do
		local o = slot * 8 + 5
		d[o], sh[o] = self.empty, self.empty
	end
	self.data_path, self.data_params = float_texture("/kp/data_" .. opts.tag .. ".texturec", DATA_WIDTH, self.data_h,
		self.data_buf)
	local mbuf = buffer.create(self.cap * 6, MESH_STREAMS)
	for _, st in ipairs(MESH_STREAMS) do
		local stream = buffer.get_stream(mbuf, st.name)
		for k = 1, #stream do stream[k] = 0 end
	end
	local pos = buffer.get_stream(mbuf, hash("position"))
	for slot = 0, self.cap - 1 do
		for corner = 0, 5 do
			local o = (slot * 6 + corner) * 3
			pos[o + 1], pos[o + 2] = slot, corner
		end
	end
	resource.set_buffer(go.get(self.mesh, "vertices"), mbuf)
	go.set(self.mesh, "texture0", opts.texture)
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
	resource.release(self.data_path)
	resource.release(self.frame_table)
end

return M
