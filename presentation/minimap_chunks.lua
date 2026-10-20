-- The minimap image with its fog (presentation/fog.lua) as messages: the GUI script that shows it runs in a Lua context
-- of its own, and a message carries about 2 KB at most, so the image travels in chunks.
local M = {}

M.CHUNK_BYTES = 1536 -- a multiple of 3 keeps the RGB pixels whole

-- The messages (kp_minimap_fog) that carry an image { version =, w =, h =, rgb = } to the GUI script: { version =,
-- w =, h =, offset =, total =, data = } in order.
function M.split(img)
	local out, total = {}, #img.rgb
	for offset = 0, total - 1, M.CHUNK_BYTES do
		out[#out + 1] = { version = img.version, w = img.w, h = img.h, offset = offset, total = total,
			data = img.rgb:sub(offset + 1, offset + M.CHUNK_BYTES) }
	end
	return out
end

-- Collects the chunks of an image in `state` (a table the receiver keeps); returns { w =, h =, rgb = } once the last
-- chunk of the image has come, else nil. A chunk out of order drops the image.
function M.assemble(state, chunk)
	if chunk.offset == 0 then
		state.version, state.parts, state.size = chunk.version, {}, 0
	end
	if state.version ~= chunk.version or state.size ~= chunk.offset then
		state.version, state.parts = nil, nil
		return nil
	end
	state.parts[#state.parts + 1] = chunk.data
	state.size = state.size + #chunk.data
	if state.size ~= chunk.total then return nil end
	local rgb = table.concat(state.parts)
	state.version, state.parts = nil, nil
	return { w = chunk.w, h = chunk.h, rgb = rgb }
end

return M
