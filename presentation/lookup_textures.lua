-- Runtime lookup data for the indexed pipeline (plan D10):
--   palette: posted to the render script as a constant array (kp_palette), not a texture
--            (docs/spikes/15-upload.md);
--   remap:   256 x rows RGBA, R of (i, row) = index that i maps to under that row
--            (row 0 identity; player colours replace 8..15, selection 1..8, shift all).
local M = {}

local RGBA = { { name = hash("rgba"), type = buffer.VALUE_TYPE_UINT8, count = 4 } }

local function texture_params(width, height)
	return { type = graphics.TEXTURE_TYPE_2D, width = width, height = height, format = graphics.TEXTURE_FORMAT_RGBA }
end

local function upload(path, width, height, fill)
	local buf = buffer.create(width * height, RGBA)
	local stream = buffer.get_stream(buf, hash("rgba"))
	fill(stream)
	if M.created[path] then
		resource.set_texture(path, texture_params(width, height), buf)
		return hash(path)
	end
	M.created[path] = true
	return resource.create_texture(path, texture_params(width, height), buf)
end

M.created = {}

-- rgb: 768-byte string (R, G, B per index).
function M.palette(rgb)
	assert(#rgb == 768, "palette must be 768 bytes")
	msg.post("@render:", "kp_palette", { rgb = rgb })
end

-- rows: array of 256-byte strings; row k is texture row k.
function M.remap(path, rows)
	return upload(path, 256, #rows, function(s)
		for r = 1, #rows do
			local row = rows[r]
			for i = 0, 255 do
				local o = ((r - 1) * 256 + i) * 4
				s[o + 1], s[o + 2], s[o + 3], s[o + 4] = row:byte(i + 1), 0, 0, 255
			end
		end
	end)
end

function M.identity_row()
	local t = {}
	for i = 0, 255 do t[i + 1] = string.char(i) end
	return table.concat(t)
end

-- Identity row with indices first..first+#values-1 replaced (player colour, selection).
function M.override_row(first, values)
	local row = M.identity_row()
	return row:sub(1, first) .. values .. row:sub(first + #values + 1)
end

return M
