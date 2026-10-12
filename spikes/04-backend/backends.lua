-- The two presentation backends compared by spike 4 (plan D9). Both draw the same
-- workload into the index render target with the same materials:
--   sprite_go  : one game object + sprite component per image (factory-created);
--   mesh_batch : two mesh components (normal and destination-reading images) whose
--                vertex buffers are rebuilt from Lua every frame in depth order.
local workload = require("spikes.04-backend.workload")

local M = {}

local FRAMES = workload.FRAMES
local DEST_ROW = 0 -- row of the destination table texture (a shadow-like darkening)

---------------------------------------------------------------------------
-- sprite_go
---------------------------------------------------------------------------
local SpriteGo = {}
SpriteGo.__index = SpriteGo

function M.sprite_go(w, factories)
	local self = setmetatable({ w = w, ids = {}, urls = {}, factories = factories, pos = vmath.vector3() }, SpriteGo)
	for i = 1, w.capacity do
		if w.alive[i] then self:create(i) end
	end
	return self
end

function SpriteGo:create(i)
	local w = self.w
	local id = factory.create(w.dest[i] and self.factories.dest or self.factories.normal)
	local url = msg.url(nil, id, "sprite")
	self.ids[i], self.urls[i] = id, url
	go.set(url, "remap", vmath.vector4(w.dest[i] and DEST_ROW or w.row[i], 0, 0, 0))
end

-- After a simulation tick: spawns, despawns, frame and flip changes.
function SpriteGo:on_tick()
	local w = self.w
	for _, i in ipairs(w.despawned) do
		if self.ids[i] then go.delete(self.ids[i]); self.ids[i], self.urls[i] = nil, nil end
	end
	for _, i in ipairs(w.spawned) do self:create(i) end
	for i = 1, w.capacity do
		local url = self.urls[i]
		if url then
			go.set(url, "cursor", (w.frame[i] + 0.5) / FRAMES)
			sprite.set_hflip(url, w.flip[i])
		end
	end
end

-- Every rendered frame: interpolated positions; z encodes the depth-order rank.
function SpriteGo:sync(alpha)
	local w, pos, ids = self.w, self.pos, self.ids
	local order = w.order
	local n = #order
	for rank = 1, n do
		local i = order[rank]
		pos.x = (w.px[i] + (w.x[i] - w.px[i]) * alpha) / 256
		pos.y = (w.py[i] + (w.y[i] - w.py[i]) * alpha) / 256
		pos.z = rank / (n + 1) - 0.5
		go.set_position(pos, ids[i])
	end
end

function SpriteGo:destroy()
	for i, id in pairs(self.ids) do go.delete(id) end
	self.ids, self.urls = {}, {}
end

---------------------------------------------------------------------------
-- mesh_batch
---------------------------------------------------------------------------
local MeshBatch = {}
MeshBatch.__index = MeshBatch

local STREAMS = {
	{ name = hash("position"), type = buffer.VALUE_TYPE_FLOAT32, count = 3 },
	{ name = hash("texcoord0"), type = buffer.VALUE_TYPE_FLOAT32, count = 2 },
	{ name = hash("remap"), type = buffer.VALUE_TYPE_FLOAT32, count = 4 },
	-- World-space meshes require a normal stream; it is never written.
	{ name = hash("normal"), type = buffer.VALUE_TYPE_FLOAT32, count = 3 },
}

-- frames: array (1-based) of { u0, v0, u1, v1, w, h } in normalised atlas coordinates.
function M.mesh_batch(w, meshes, frames)
	local self = setmetatable({ w = w, meshes = meshes, frames = frames, layers = {} }, MeshBatch)
	for _, layer in ipairs({ "normal", "dest" }) do
		local buf = buffer.create(w.capacity * 6, STREAMS)
		-- buffer.create does not zero its memory; unused vertices must be degenerate.
		for _, s in ipairs(STREAMS) do
			local stream = buffer.get_stream(buf, s.name)
			for k = 1, #stream do stream[k] = 0 end
		end
		self.layers[layer] = {
			buf = buf,
			pos = buffer.get_stream(buf, hash("position")),
			uv = buffer.get_stream(buf, hash("texcoord0")),
			remap = buffer.get_stream(buf, hash("remap")),
			resource = go.get(meshes[layer], "vertices"),
		}
	end
	return self
end

function MeshBatch:on_tick() end

local function emit(L, v, x0, y0, x1, y1, u0, v0, u1, v1, row)
	local pos, uv, rm = L.pos, L.uv, L.remap
	-- two triangles: (x0,y0)(x1,y0)(x1,y1) (x0,y0)(x1,y1)(x0,y1)
	local p, t, m = v * 3, v * 2, v * 4
	pos[p + 1], pos[p + 2], pos[p + 3] = x0, y0, 0
	pos[p + 4], pos[p + 5], pos[p + 6] = x1, y0, 0
	pos[p + 7], pos[p + 8], pos[p + 9] = x1, y1, 0
	pos[p + 10], pos[p + 11], pos[p + 12] = x0, y0, 0
	pos[p + 13], pos[p + 14], pos[p + 15] = x1, y1, 0
	pos[p + 16], pos[p + 17], pos[p + 18] = x0, y1, 0
	uv[t + 1], uv[t + 2] = u0, v1
	uv[t + 3], uv[t + 4] = u1, v1
	uv[t + 5], uv[t + 6] = u1, v0
	uv[t + 7], uv[t + 8] = u0, v1
	uv[t + 9], uv[t + 10] = u1, v0
	uv[t + 11], uv[t + 12] = u0, v0
	for k = 0, 5 do rm[m + k * 4 + 1] = row end
end

function MeshBatch:sync(alpha)
	local w, frames = self.w, self.frames
	local normal, dest = self.layers.normal, self.layers.dest
	local nv, dv = 0, 0
	local order = w.order
	for rank = 1, #order do
		local i = order[rank]
		local f = frames[w.frame[i] + 1]
		local x = (w.px[i] + (w.x[i] - w.px[i]) * alpha) / 256
		local y = (w.py[i] + (w.y[i] - w.py[i]) * alpha) / 256
		local hw, hh = f[5] / 2, f[6] / 2
		local u0, u1 = f[1], f[3]
		if w.flip[i] then u0, u1 = u1, u0 end
		if w.dest[i] then
			emit(dest, dv, x - hw, y - hh, x + hw, y + hh, u0, f[2], u1, f[4], DEST_ROW)
			dv = dv + 6
		else
			emit(normal, nv, x - hw, y - hh, x + hw, y + hh, u0, f[2], u1, f[4], w.row[i])
			nv = nv + 6
		end
	end
	-- Vertices used last frame but not now collapse to a degenerate point.
	for _, pair in ipairs({ { normal, nv }, { dest, dv } }) do
		local L, used = pair[1], pair[2]
		local pos = L.pos
		for k = used * 3 + 1, (L.used or 0) * 3 do pos[k] = 0 end
		L.used = used
		resource.set_buffer(L.resource, L.buf)
	end
end

function MeshBatch:destroy()
	for _, L in pairs(self.layers) do
		local pos = L.pos
		for k = 1, #pos do pos[k] = 0 end
		resource.set_buffer(L.resource, L.buf)
	end
end

---------------------------------------------------------------------------
-- mesh_texture: static (slot, corner) meshes; per-image data in a float texture
---------------------------------------------------------------------------
local MeshTexture = {}
MeshTexture.__index = MeshTexture

local DATA_WIDTH = 256
local RGBA32F = { { name = hash("rgba"), type = buffer.VALUE_TYPE_FLOAT32, count = 4 } }

local function float_texture(path, width, height, buf)
	local params = { type = graphics.TEXTURE_TYPE_2D, width = width, height = height, format = graphics.TEXTURE_FORMAT_RGBA32F }
	return resource.create_texture(path, params, buf), params
end

local MESH_STREAMS = {
	{ name = hash("position"), type = buffer.VALUE_TYPE_FLOAT32, count = 3 },
	{ name = hash("normal"), type = buffer.VALUE_TYPE_FLOAT32, count = 3 },
}

-- frames: as for mesh_batch. meshes: { normal = url, dest = url } using *_vt materials.
-- Two texels per slot, layout in render/materials/indexed_vt.vp. Slots are written on
-- simulation ticks only, and only the values that changed; frames between ticks just set
-- the interpolation constant.
function M.mesh_texture(w, meshes, frames, tag)
	local self = setmetatable({ w = w, cap = w.capacity, empty_code = #frames * 2, meshes = meshes }, MeshTexture)
	-- Frame table: two texels per frame plus a zero-size frame for unused slots.
	local fbuf = buffer.create((#frames + 1) * 2, RGBA32F)
	local fs = buffer.get_stream(fbuf, hash("rgba"))
	-- buffer.create does not zero its memory; the last frame must be the zero-size one.
	for k = 1, #fs do fs[k] = 0 end
	for k, f in ipairs(frames) do
		local o = (k - 1) * 8
		fs[o + 1], fs[o + 2], fs[o + 3], fs[o + 4] = f[1], f[2], f[3], f[4]
		fs[o + 5], fs[o + 6] = f[5], f[6]
	end
	self.frame_table = float_texture("/sp04/frames_" .. tag .. ".texturec", (#frames + 1) * 2, 1, fbuf)
	-- Image data texture covering 2 * capacity slots (normal, then destination).
	local slots = 2 * self.cap
	self.data_h = math.ceil(slots * 2 / DATA_WIDTH)
	self.data_buf = buffer.create(DATA_WIDTH * self.data_h, RGBA32F)
	self.data = buffer.get_stream(self.data_buf, hash("rgba"))
	-- shadow[k] mirrors data element k (1-based) so unchanged values are not rewritten:
	-- every stream element access is a C call (spike 10).
	self.shadow = {}
	local d, sh = self.data, self.shadow
	-- buffer.create does not zero its memory: every element is written once here, so the
	-- shadow starts equal to the buffer.
	for k = 1, DATA_WIDTH * self.data_h * 4 do d[k], sh[k] = 0, 0 end
	for slot = 0, slots - 1 do
		local o = slot * 8 + 5
		d[o], sh[o] = self.empty_code, self.empty_code
	end
	self.data_path, self.data_params = float_texture("/sp04/data_" .. tag .. ".texturec", DATA_WIDTH, self.data_h, self.data_buf)
	-- Static meshes: slot index and corner per vertex.
	for layer, base in pairs({ normal = 0, dest = self.cap }) do
		local buf = buffer.create(self.cap * 6, MESH_STREAMS)
		-- buffer.create does not zero its memory (z and the normals are never set below).
		for _, st in ipairs(MESH_STREAMS) do
			local stream = buffer.get_stream(buf, st.name)
			for k = 1, #stream do stream[k] = 0 end
		end
		local pos = buffer.get_stream(buf, hash("position"))
		for slot = 0, self.cap - 1 do
			for corner = 0, 5 do
				local o = (slot * 6 + corner) * 3
				pos[o + 1], pos[o + 2] = base + slot, corner
			end
		end
		resource.set_buffer(go.get(meshes[layer], "vertices"), buf)
		local first = layer == "normal" and 2 or 3
		go.set(meshes[layer], "texture" .. first, self.data_path)
		go.set(meshes[layer], "texture" .. (first + 1), self.frame_table)
	end
	self.used = { normal = 0, dest = 0 }
	self.interp = vmath.vector4(1, 0, 0, 0)
	return self
end

-- Writes the tick state in depth order and uploads once if anything changed.
function MeshTexture:on_tick()
	local w, d, sh, cap = self.w, self.data, self.shadow, self.cap
	local px, py, x, y, frame, flip, row, dest = w.px, w.py, w.x, w.y, w.frame, w.flip, w.row, w.dest
	local nn, dn, writes = 0, 0, 0
	local order = w.order
	for rank = 1, #order do
		local i = order[rank]
		local slot
		if dest[i] then slot = cap + dn; dn = dn + 1 else slot = nn; nn = nn + 1 end
		local o = slot * 8
		local v1, v2 = x[i], y[i]
		local v3, v4 = v1 - px[i], v2 - py[i]
		local v5 = (dest[i] and DEST_ROW or row[i]) * 65536 + frame[i] * 2 + (flip[i] and 1 or 0)
		if sh[o + 1] ~= v1 then d[o + 1] = v1; sh[o + 1] = v1; writes = writes + 1 end
		if sh[o + 2] ~= v2 then d[o + 2] = v2; sh[o + 2] = v2; writes = writes + 1 end
		if sh[o + 3] ~= v3 then d[o + 3] = v3; sh[o + 3] = v3; writes = writes + 1 end
		if sh[o + 4] ~= v4 then d[o + 4] = v4; sh[o + 4] = v4; writes = writes + 1 end
		if sh[o + 5] ~= v5 then d[o + 5] = v5; sh[o + 5] = v5; writes = writes + 1 end
	end
	local empty = self.empty_code
	for slot = nn, self.used.normal - 1 do
		local o = slot * 8 + 5
		if sh[o] ~= empty then d[o] = empty; sh[o] = empty; writes = writes + 1 end
	end
	for slot = cap + dn, cap + self.used.dest - 1 do
		local o = slot * 8 + 5
		if sh[o] ~= empty then d[o] = empty; sh[o] = empty; writes = writes + 1 end
	end
	self.used.normal, self.used.dest = nn, dn
	self.writes = writes
	if writes > 0 and not self.pending then self.pending, self.pending_frames = true, 0 end
end

-- A tick's data is uploaded on the frame after the tick, so the Lua writes and the
-- fixed-cost set_texture (spike 15) never land on the same frame. On the tick frame the
-- GPU still holds the previous tick, drawn at interp = 1: its final positions equal the
-- new tick's positions at alpha = 0, so the picture holds for one frame and never jumps.
-- A pending upload waits at most one frame, also under catch-up.
function MeshTexture:sync(alpha)
	self.upload_ms = nil
	local interp = alpha
	if self.pending then
		if self.pending_frames >= 1 then
			local t0 = socket.gettime()
			resource.set_texture(self.data_path, self.data_params, self.data_buf)
			self.upload_ms = (socket.gettime() - t0) * 1000
			self.pending = false
		else
			self.pending_frames = self.pending_frames + 1
			interp = 1
		end
	end
	self.interp.x = interp
	go.set(self.meshes.normal, "interp", self.interp)
	go.set(self.meshes.dest, "interp", self.interp)
end

function MeshTexture:destroy()
	local d = self.data
	for slot = 0, 2 * self.cap - 1 do d[slot * 8 + 5] = self.empty_code end
	resource.set_texture(self.data_path, self.data_params, self.data_buf)
end

return M
