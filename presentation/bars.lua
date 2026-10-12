-- Health bar texture of the world images (draw method 11): every drawn bar gets a slot of
-- ROWS texture rows; presentation/health_bar.lua draws it there (R = palette index, A = 255
-- where the original writes, 0 elsewhere) and render/materials/world.fp copies it at the
-- image's rank. A slot is redrawn only when its bar changes; the texture goes up with the
-- image data that refers to it (world_images Backend:sync calls upload()).
local health_bar = require("presentation.health_bar")
local slots = require("presentation.backend.slots")
local gpu_buffers = require("presentation.backend.gpu_buffers")

local M = {}

M.WIDTH = 256 -- texels per row: the widest frame (255) fits
M.ROWS = 16 -- texture rows per slot (render/materials/world.fp BAR_ROWS): 13 bar rows fit
M.SLOTS = 12 -- the health-bar pool (game/images/core.lua HEALTH)
M.FAMILY = "bars"

local Bars = {}
Bars.__index = Bars

-- thpbar: the 19 thpbar.pcx entries (string); tag: unique texture name, or nil for no
-- Defold texture (tests read self.pixels: R, A per texel, 1-based like a buffer stream).
function M.new(thpbar, tag)
	local self = setmetatable({ slots = slots.new({ { name = M.FAMILY, capacity = M.SLOTS } }), keys = {},
		dirty = false, thpbar = {} }, Bars)
	for k = 0, #thpbar - 1 do self.thpbar[k] = thpbar:byte(k + 1) end
	local texels = M.WIDTH * M.ROWS * M.SLOTS
	if tag then
		self.buf, self.pixels = gpu_buffers.zeroed(texels, gpu_buffers.RGBA8)
		self.params = { type = graphics.TEXTURE_TYPE_2D, width = M.WIDTH, height = M.ROWS * M.SLOTS,
			format = graphics.TEXTURE_FORMAT_RGBA }
		self.path = resource.create_texture("/kp/bars_" .. tag .. ".texturec", self.params, self.buf)
	else
		self.pixels = {}
		for k = 1, texels * 4 do self.pixels[k] = 0 end
	end
	-- Canvas of health_bar.draw over the current slot.
	local canvas = { thpbar = self.thpbar }
	local px = self.pixels
	local function put(x, y, color)
		local o = ((canvas.base + y) * M.WIDTH + x) * 4
		px[o + 1], px[o + 4] = color, 255
	end
	function canvas.hline(_, x, y, n, color)
		for k = 0, n - 1 do put(x + k, y, color) end
	end
	function canvas.vline(_, x, y, n, color)
		for k = 0, n - 1 do put(x, y + k, color) end
	end
	self.canvas = canvas
	return self
end

function Bars:begin()
	self.slots:begin()
end

local function signature(bar, w, h)
	local e = bar.energy
	return table.concat({ bar.width, bar.shields and 1 or 0, bar.hp, bar.hp_max, bar.sh or 0, bar.sh_max or 0,
		e and e[1] or -1, e and e[2] or -1, w, h }, ",")
end

-- Slot of image `key` showing `bar` (health_bar.from_unit) in a w x h frame; draws it when
-- it changed.
function Bars:put(key, bar, w, h)
	-- One slot per record of the health-bar pool: never full.
	local slot = assert(self.slots:slot(M.FAMILY, key), "bars: more bars than health-bar records")
	local sig = signature(bar, w, h)
	if self.keys[slot] ~= sig then
		self.keys[slot] = sig
		local px, base = self.pixels, slot * M.ROWS
		for o = base * M.WIDTH * 4, (base + M.ROWS) * M.WIDTH * 4 - 1, 4 do px[o + 1], px[o + 4] = 0, 0 end
		self.canvas.base = base
		health_bar.draw(self.canvas, 0, 0, { left = 0, top = 0, w = w, h = h }, bar)
		self.dirty = true
	end
	return slot
end

-- Frees the slots not used this tick.
function Bars:finish()
	self.slots:finish(function(slot) self.keys[slot] = nil end)
end

-- Uploads the texture when a bar changed since the last upload.
function Bars:upload()
	if self.dirty and self.path then resource.set_texture(self.path, self.params, self.buf) end
	self.dirty = false
end

function Bars:destroy()
	if self.path then resource.release(self.path) end
end

return M
