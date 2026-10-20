-- The fog of war over the game view (plan phase 8 item 5, atlas P23/P24): kp_core's fog view
-- (kp_core.game.fog_view, native/kp_core/src/core/kp_fog_view.c) builds the original's level map from the local
-- player's tile visibility around the camera; this module keeps it in a texture (one texel per 8x8-pixel block,
-- level in R) and the tileset's dark.pcx rows in another (256 x 32), and tells the render script (kp_fog), whose
-- palette pass darkens the view through them. Only the blocks whose levels changed are uploaded again; a moved
-- camera tile or a resized view rebuilds the whole map.
local gpu_buffers = require("presentation.backend.gpu_buffers")
local lookup = require("presentation.lookup_textures")

local M = {}

M.LEVELS_PATH = "/kp/fog_levels.texturec"
M.DARK_PATH = "/kp/fog_dark.texturec"
M.DARK_ROWS = 32
local ROW = 256

local Fog = {}
Fog.__index = Fog

-- core: the kp_core module; dark: the tileset's dark.pcx rows (32 * 256 bytes, game/data/palettes.lua).
function M.new(core, dark)
	assert(#dark == M.DARK_ROWS * ROW, "fog: dark.pcx rows must be 32 x 256 bytes")
	local rows = {}
	for r = 1, M.DARK_ROWS do rows[r] = dark:sub((r - 1) * ROW + 1, r * ROW) end
	lookup.remap(M.DARK_PATH, rows)
	-- fresh: the next kp_fog names textures the render script has not resolved yet (a new dark texture, a new level map)
	return setmetatable({ view = core.game.fog_view(), dark = dark, width = 0, height = 0, block_x = 0, block_y = 0,
		fresh = true, minimap_version = 0 }, Fog)
end

local function params(w, h, x, y)
	return { type = graphics.TEXTURE_TYPE_2D, width = w, height = h, format = graphics.TEXTURE_FORMAT_RGBA, x = x,
		y = y }
end

-- The levels of a view of w x h pixels whose top-left is the map pixel (cam_x, cam_y), after the game's steps or a
-- camera move; uploads what changed and posts the render script's kp_fog.
function Fog:update(game, cam_x, cam_y, w, h)
	local changed, x0, y0, x1, y1 = self.view:update(game, cam_x, cam_y, w, h)
	if changed then
		local levels, lw, lh, bx, by = self.view:levels()
		if lw ~= self.width or lh ~= self.height then
			if self.width > 0 then resource.release(M.LEVELS_PATH) end
			x0, y0, x1, y1 = 0, 0, lw, lh
		end
		local cw, ch = x1 - x0, y1 - y0
		local buf = buffer.create(cw * ch, gpu_buffers.RGBA8)
		local s = buffer.get_stream(buf, gpu_buffers.STREAM)
		local o = 0
		for y = y0, y1 - 1 do
			for x = x0, x1 - 1 do
				s[o + 1], s[o + 2], s[o + 3], s[o + 4] = levels:byte(y * lw + x + 1), 0, 0, 255
				o = o + 4
			end
		end
		if lw ~= self.width or lh ~= self.height then
			resource.create_texture(M.LEVELS_PATH, params(lw, lh), buf)
			self.width, self.height, self.fresh = lw, lh, true
		else
			resource.set_texture(M.LEVELS_PATH, params(cw, ch, x0, y0), buf)
		end
		self.block_x, self.block_y = bx, by
	end
	self.is_off = false
	msg.post("@render:", "kp_fog", { levels = M.LEVELS_PATH, dark = M.DARK_PATH, fresh = self.fresh, map_x = cam_x,
		map_y = cam_y, block_x = self.block_x, block_y = self.block_y, width = self.width, height = self.height })
	self.fresh = false
end

M.MINIMAP_MS = 1000 -- the minimap's fog is composed by its UI timer 1 (1000 ms, 0x46e8d0)
M.MINIMAP_HEADER = 5 -- <mission>_minimap.idx: u16 width, u16 height, u8 the unexplored colour (converter/emit/minimap.py)

-- The minimap's terrain indices of a mission (the converter's <mission>_minimap.idx): { w =, h =, unexplored =,
-- pixels = }.
function M.minimap_terrain(bytes)
	local b1, b2, b3, b4, c = bytes:byte(1, M.MINIMAP_HEADER)
	return { w = b1 + 256 * b2, h = b3 + 256 * b4, unexplored = c, pixels = bytes:sub(M.MINIMAP_HEADER + 1) }
end

-- Composes the minimap's fog over its terrain (kp_fog_minimap) and maps the indices through the palette rgb (768
-- bytes): { w =, h =, rgb = (w * h * 3 bytes, rows bottom-up as a texture takes them), version = (one more each
-- composition) }, for the GUI script as messages (presentation/minimap_chunks.lua: it runs in a Lua context of its own).
function Fog:minimap(game, terrain, rgb)
	local pixels, w, h = self.view:minimap(game, terrain.pixels, self.dark, terrain.unexplored)
	local colour = {} -- the palette's colours as 3-byte strings, one allocation per index and not per pixel
	for i = 0, 255 do colour[i] = rgb:sub(i * 3 + 1, i * 3 + 3) end
	local out, n = {}, 0
	for y = h - 1, 0, -1 do
		for x = 1, w do
			n = n + 1
			out[n] = colour[pixels:byte(y * w + x)]
		end
	end
	self.minimap_version = self.minimap_version + 1
	return { w = w, h = h, rgb = table.concat(out), version = self.minimap_version }
end

-- The fog off (the view without it); the textures stay for the next update, which the render script resolves again
-- (it drops them with the fog).
function Fog:off()
	if self.is_off then return end
	self.is_off = true
	msg.post("@render:", "kp_fog", {})
end

-- Releases the level map's texture (the dark rows belong to the lookup textures).
function Fog:destroy()
	self:off()
	if self.width > 0 then resource.release(M.LEVELS_PATH) end
	self.width, self.height = 0, 0
end

return M
