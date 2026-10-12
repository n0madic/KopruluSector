-- Single-image animation browser of the viewer (plan §7 phase 2 item 5), pure Lua: one
-- sprite showing any image of images.dat in any of its IScript animations and directions,
-- in a world of its own. The sprite stands for a unit under the viewer hooks
-- (viewer/map_world.lua), so unit opcodes only turn it; an animation that needs more raises
-- and the browser shows the fault and stops ticking.
local core = require("game.images.core")
local world = require("game.images.world")
local grpmeta = require("game.data.grpmeta")
local bytes = require("game.data.bytes")
local rng = require("game.simulation.rng")
local map_world = require("viewer.map_world")

local NONE = core.NONE

local M = {}

M.IMAGES = 999 -- images.dat entries
M.HOST_SPRITE = 0 -- the sprite created and given the browsed image (as tests/lua/iscript_trace.lua)
M.DIRECTIONS = 32
M.HEADING_STEP = 8 -- heading units per direction (256 / DIRECTIONS)
M.UNIT = 1 -- the token of the browsed sprite under the viewer hooks

-- ctx: { dat =, grp =, iscript = } (parsed .kpd); map size in megatiles; available(image
-- id) (optional) limits the images stepped through (e.g. to loaded atlases). Returns the
-- browser; show() creates its sprite.
function M.new(ctx, map_width, map_height, available)
	return setmetatable({ ctx = ctx, map_width = map_width, map_height = map_height, image = 0, anim = 0,
		direction = 0, available = available }, { __index = M })
end

-- IScript header offset and animation slots of an image (nil, 0 without a script).
function M:header(image_id)
	local isc = self.ctx.iscript
	local h = isc.headers[self.ctx.dat.images.iscript_id[image_id]]
	if not h then return nil, 0 end
	return h, isc.slots[bytes.u8(isc.code, h + 4)] or 0
end

-- True when animation `anim` of the image has an entry (a nonzero PC).
function M:has_anim(image_id, anim)
	local h, n = self:header(image_id)
	return h ~= nil and anim < n and bytes.u16(self.ctx.iscript.code, h + 8 + 2 * anim) ~= 0
end

-- True when the image can be shown: a GRP, an Init animation and available.
function M:showable(image_id)
	local g = grpmeta.image_grp(self.ctx.grp, self.ctx.dat.images, image_id)
	return g ~= nil and g > 0 and self:has_anim(image_id, 0) and (self.available == nil or self.available(image_id))
end

-- Rebuilds the world with the current image at map point x, y, its current animation and
-- direction. Returns true, or false and the fault.
function M:show(x, y)
	local ctx = self.ctx
	local units = { [M.UNIT] = { heading = self.direction * M.HEADING_STEP } }
	local g = rng.game(map_world.SEED)
	g:seed(map_world.SEED)
	g:set_enabled(1)
	local w = world.new({ dat = ctx.dat, grp = ctx.grp, iscript = ctx.iscript, rng = g,
		hooks = map_world.viewer_hooks(units), map_width = self.map_width, map_height = self.map_height })
	w:init_pools()
	self.world, self.units, self.fault, self.sprite = w, units, nil, NONE
	units[M.UNIT].sprite = w.free_sprites.head
	local sprites = ctx.dat.sprites
	local saved = sprites.image[M.HOST_SPRITE]
	sprites.image[M.HOST_SPRITE] = self.image
	w.unit, w.flingy = M.UNIT, M.UNIT
	local ok, s = pcall(w.sprite_create, w, M.HOST_SPRITE, x, y, 0)
	sprites.image[M.HOST_SPRITE] = saved
	if ok and s ~= NONE then
		self.sprite = s
		ok, s = pcall(function()
			w:sprite_set_facing(self.sprite, units[M.UNIT].heading)
			if self.anim ~= 0 then w:sprite_play(self.sprite, self.anim, true) end
		end)
	end
	w.unit, w.flingy = nil, nil
	if not ok then self.fault = tostring(s) end
	return self.fault == nil, self.fault
end

-- One tick: the sprite's images under the browsed unit's context.
function M:step()
	if self.fault or self.sprite == NONE then return end
	local w = self.world
	w.unit, w.flingy = M.UNIT, M.UNIT
	local ok, err = pcall(w.sprite_update, w, self.sprite)
	w.unit, w.flingy = nil, nil
	if not ok then
		self.fault = tostring(err)
	elseif w.spr.images[self.sprite].head == NONE then
		self.sprite = NONE
	end
end

-- Next/previous showable image from the current one (step +1 or -1).
function M:next_image(step)
	local id = self.image
	for _ = 1, M.IMAGES do
		id = (id + step) % M.IMAGES
		if self:showable(id) then break end
	end
	self.image, self.anim = id, 0
end

-- Next/previous animation with an entry.
function M:next_anim(step)
	local _, n = self:header(self.image)
	local a = self.anim
	for _ = 1, n do
		a = (a + step) % n
		if self:has_anim(self.image, a) then break end
	end
	self.anim = a
end

function M:next_direction(step)
	self.direction = (self.direction + step) % M.DIRECTIONS
end

return M
