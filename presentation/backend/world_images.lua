-- World image backend (ADR 0002, composition by depth rank in ADR 0006): one mesh per
-- material family, all reading one RGBA32F image-data texture (render/materials/world.vp):
--   image_data[2 slot]     = (x, y, x - previous x, y - previous y) in 1/256 world pixels
--   image_data[2 slot + 1] = (frame * 2 + hflip, rank, row, layer)
-- and one frame table of every loaded atlas:
--   frame_table[2 f] = (u0, v0, u1, v1), frame_table[2 f + 1] = (width, height, rotated, atlas).
-- Each family owns a fixed slot range; an image keeps its slot while it stays in the family
-- (slots are stable by image key), so a still image changes nothing. Values are written
-- only when they change (a shadow table mirrors the stream) and the texture is uploaded
-- on the frame after the write, at most once per frame (spike 15).
local mesh_texture = require("presentation.backend.mesh_texture")

local M = {}

M.DATA_WIDTH = 256
M.FRAME_WIDTH = 1024
M.ATLAS_SLOTS = 6
M.RANK_RANGE = 8192 -- render/materials/world.vp

local Backend = {}
Backend.__index = Backend

local RGBA32F = { { name = hash("rgba"), type = buffer.VALUE_TYPE_FLOAT32, count = 4 } }
local MESH_STREAMS = {
	{ name = hash("position"), type = buffer.VALUE_TYPE_FLOAT32, count = 3 },
	{ name = hash("normal"), type = buffer.VALUE_TYPE_FLOAT32, count = 3 },
}
local STREAM = hash("rgba")

local function texture_params(width, height)
	return { type = graphics.TEXTURE_TYPE_2D, width = width, height = height, format = graphics.TEXTURE_FORMAT_RGBA32F }
end

local function zero_buffer(count)
	local buf = buffer.create(count, RGBA32F)
	local s = buffer.get_stream(buf, STREAM)
	for k = 1, #s do s[k] = 0 end
	return buf, s
end

-- atlases: array (atlas slot 0..5 at position slot + 1) of atlas resource paths or false.
-- Returns { path = frame table texture, first = { [slot] = { [animation hash] = global
-- frame } }, textures = { [slot] = atlas texture path }, rotated = { [global frame] = true
-- for frames bob packed rotated }, empty = zero-size frame index }.
function M.frame_table(atlases, tag)
	local all, first, textures, rotated = {}, {}, {}, {}
	for slot = 0, M.ATLAS_SLOTS - 1 do
		local path = atlases[slot + 1]
		if path then
			local frames, local_first, texture = mesh_texture.atlas_frames(path)
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
	local buf, s = zero_buffer(M.FRAME_WIDTH * height)
	for k, f in ipairs(all) do
		local o = (k - 1) * 8
		for j = 1, 8 do s[o + j] = f[j] end
	end
	local path = "/kp/frame_table_" .. tag .. ".texturec"
	return { path = resource.create_texture(path, texture_params(M.FRAME_WIDTH, height), buf), first = first,
		textures = textures, rotated = rotated, empty = empty, count = empty }
end

-- opts: families = array of { name =, mesh = url (a world family material with its own
-- .buffer), capacity = }, empty = zero-size frame index, tag = unique name.
function M.new(opts)
	local self = setmetatable({ families = {}, empty = opts.empty, used = {} }, Backend)
	local total = 0
	for k, f in ipairs(opts.families) do
		self.families[k] = { name = f.name, mesh = f.mesh, base = total, capacity = f.capacity, slot_of = {},
			free = {}, next_slot = 0 }
		self.families[f.name] = self.families[k]
		total = total + f.capacity
	end
	self.slots = total
	self.data_h = math.ceil(total * 2 / M.DATA_WIDTH)
	self.data_buf, self.data = zero_buffer(M.DATA_WIDTH * self.data_h)
	self.shadow = {}
	local d, sh = self.data, self.shadow
	for k = 1, #d do sh[k] = 0 end
	for slot = 0, total - 1 do
		local o = slot * 8 + 5
		d[o], sh[o] = self.empty * 2, self.empty * 2
	end
	self.data_params = texture_params(M.DATA_WIDTH, self.data_h)
	self.data_path = resource.create_texture("/kp/image_data_" .. opts.tag .. ".texturec", self.data_params,
		self.data_buf)
	for _, f in ipairs(self.families) do
		local mbuf = buffer.create(f.capacity * 6, MESH_STREAMS)
		for _, st in ipairs(MESH_STREAMS) do
			local stream = buffer.get_stream(mbuf, st.name)
			for k = 1, #stream do stream[k] = 0 end
		end
		local pos = buffer.get_stream(mbuf, hash("position"))
		for slot = 0, f.capacity - 1 do
			for corner = 0, 5 do
				local o = (slot * 6 + corner) * 3
				pos[o + 1], pos[o + 2] = slot, corner
			end
		end
		resource.set_buffer(go.get(f.mesh, "vertices"), mbuf)
		go.set(f.mesh, "slot_base", vmath.vector4(f.base, 0, 0, 0))
	end
	self.seen = {}
	return self
end

-- Starts a tick's writes.
function Backend:begin()
	self.writes = 0
	self.tick = (self.tick or 0) + 1
end

local function write(self, o, v)
	if self.shadow[o] ~= v then
		self.data[o], self.shadow[o] = v, v
		self.writes = self.writes + 1
	end
end

-- One image of the tick: family name, image key, position and previous position (world
-- pixels, centre of the frame rectangle), global frame, flip, rank, row and layer.
function Backend:put(family, key, x, y, px, py, frame, flip, rank, row, layer)
	local f = self.families[family]
	local slot = f.slot_of[key]
	if not slot then
		slot = table.remove(f.free)
		if not slot then
			assert(f.next_slot < f.capacity, "world_images: family " .. family .. " is full")
			slot = f.next_slot
			f.next_slot = slot + 1
		end
		f.slot_of[key] = slot
	end
	self.seen[f.base + slot] = self.tick
	local o = (f.base + slot) * 8
	x, y = x * 256, y * 256
	write(self, o + 1, x)
	write(self, o + 2, y)
	write(self, o + 3, x - px * 256)
	write(self, o + 4, y - py * 256)
	write(self, o + 5, frame * 2 + (flip and 1 or 0))
	write(self, o + 6, rank)
	write(self, o + 7, row)
	write(self, o + 8, layer)
end

-- Ends the tick: slots not written this tick are emptied and freed.
function Backend:finish()
	for _, f in ipairs(self.families) do
		for key, slot in pairs(f.slot_of) do
			if self.seen[f.base + slot] ~= self.tick then
				f.slot_of[key] = nil
				f.free[#f.free + 1] = slot
				write(self, (f.base + slot) * 8 + 5, self.empty * 2)
			end
		end
	end
	if self.writes > 0 and not self.pending then self.pending, self.pending_frames = true, 0 end
	return self.writes
end

-- Per frame: uploads pending data on the frame after the write. Returns the interpolation
-- to use (1 on a write frame, whose texture still holds the previous tick).
function Backend:sync(alpha)
	if self.pending then
		if self.pending_frames >= 1 then
			resource.set_texture(self.data_path, self.data_params, self.data_buf)
			self.pending = false
		else
			self.pending_frames = self.pending_frames + 1
			return 1
		end
	end
	return alpha or 1
end

function Backend:destroy()
	resource.release(self.data_path)
end

return M
