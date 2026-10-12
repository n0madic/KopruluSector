-- GPU buffer helpers shared by the world image backends. buffer.create does not zero its
-- memory (the cause of the phase-0 backend parity differences, ADR 0002), so every buffer
-- handed to the GPU is written in full here.
local M = {}

M.STREAM = hash("rgba")
M.RGBA32F = { { name = M.STREAM, type = buffer.VALUE_TYPE_FLOAT32, count = 4 } }
M.MESH_STREAMS = {
	{ name = hash("position"), type = buffer.VALUE_TYPE_FLOAT32, count = 3 },
	{ name = hash("normal"), type = buffer.VALUE_TYPE_FLOAT32, count = 3 },
}

-- A buffer of `count` elements with every stream zeroed; returns the buffer and its first
-- stream.
function M.zeroed(count, format)
	local buf = buffer.create(count, format)
	local first
	for _, st in ipairs(format) do
		local s = buffer.get_stream(buf, st.name)
		for k = 1, #s do s[k] = 0 end
		first = first or s
	end
	return buf, first
end

function M.rgba32f_params(width, height)
	return { type = graphics.TEXTURE_TYPE_2D, width = width, height = height, format = graphics.TEXTURE_FORMAT_RGBA32F }
end

-- Static (slot, corner) vertices of `capacity` quads for a vertex-texture mesh: position.x =
-- slot, position.y = corner 0..5.
function M.slot_quads(capacity)
	local buf = M.zeroed(capacity * 6, M.MESH_STREAMS)
	local pos = buffer.get_stream(buf, hash("position"))
	for slot = 0, capacity - 1 do
		for corner = 0, 5 do
			local o = (slot * 6 + corner) * 3
			pos[o + 1], pos[o + 2] = slot, corner
		end
	end
	return buf
end

return M
