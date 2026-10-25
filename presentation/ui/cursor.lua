-- The interface's cursor (ADR 0011): the original's screen layer 0 (DrawCursor 0x43e490 at the mouse, over every other
-- layer). kp_ui gives the frame (index 0 transparent) and its place relative to the mouse; in Defold the frame goes
-- into an RGBA texture (palette index in R, opacity in A, rows top first) that the canvas pass writes over the canvas
-- at the frame's rectangle (render script message kp_ui_cursor), so the canvas the input reads holds no cursor.
local M = {}

M.TEXTURE_PATH = "/kp/ui_cursor.texturec"
M.SIZE = 128 -- a cursor GRP's box (the mouse at its middle, kp_ui_cursor.c), the texture's size

-- Each index byte as its RGBA texel: index 0 transparent, the others opaque.
local TEXELS = {}
for i = 0, 255 do TEXELS[string.char(i)] = i == 0 and "\0\0\0\0" or string.char(i, 0, 0, 255) end

local Cursor = {}
Cursor.__index = Cursor

-- core: the kp_core module (its copy_to_buffer).
function M.new(core)
	return setmetatable({ core = core }, Cursor)
end

local function params(w, h, x, y)
	return { type = graphics.TEXTURE_TYPE_2D, width = w, height = h, format = graphics.TEXTURE_FORMAT_RGBA,
		x = x, y = y }
end

-- The browser's own cursor goes once the interface draws its own.
-- TODO: the desktop and Android bundles keep the system cursor (Defold has no call to hide it; a native extension
-- such as defos would)
local function hide_system_cursor(self)
	if self.system_hidden then return end
	self.system_hidden = true
	if html5 then html5.run("document.getElementById('canvas').style.cursor = 'none'") end
end

-- Each frame: interface ui's cursor at the logical screen's point (x, y), or no cursor (ui nil: no interface shows; x
-- nil: the mouse has not reached the window yet).
function Cursor:frame(ui, x, y)
	local key = ui and x and ui:cursor_key()
	if key ~= self.key then
		self.key, self.w = key, nil
		local bytes, w, h, dx, dy = nil, 0, 0, 0, 0
		if key then bytes, w, h, dx, dy = ui:cursor_bitmap() end
		if bytes and w <= M.SIZE and h <= M.SIZE then
			local buf = buffer.create(w * h, { { name = hash("data"), type = buffer.VALUE_TYPE_UINT8, count = 4 } })
			self.core.copy_to_buffer((bytes:gsub(".", TEXELS)), buf)
			local fresh = not self.texture
			if fresh then
				local empty = buffer.create(M.SIZE * M.SIZE,
					{ { name = hash("data"), type = buffer.VALUE_TYPE_UINT8, count = 4 } })
				resource.create_texture(M.TEXTURE_PATH, params(M.SIZE, M.SIZE), empty)
				self.texture = true
			end
			resource.set_texture(M.TEXTURE_PATH, params(w, h, 0, 0), buf)
			self.w, self.h, self.dx, self.dy, self.fresh = w, h, dx, dy, self.fresh or fresh
			hide_system_cursor(self)
		end
	end
	if self.w then
		msg.post("@render:", "kp_ui_cursor", { texture = M.TEXTURE_PATH, fresh = self.fresh, x = x + self.dx,
			y = y + self.dy, w = self.w, h = self.h })
		self.fresh = false
	elseif self.shown ~= false then
		msg.post("@render:", "kp_ui_cursor", {})
	end
	self.shown = self.w ~= nil
end

-- Stops drawing the cursor and releases its texture.
function Cursor:release()
	msg.post("@render:", "kp_ui_cursor", {})
	if self.texture then resource.release(M.TEXTURE_PATH) end
	self.texture, self.key, self.w, self.shown = nil, nil, nil, false
end

return M
