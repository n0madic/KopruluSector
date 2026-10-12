-- Selection images of game.images (methods of World): the health-bar and placement-box
-- records of the virtual pools, health bars of selected sprites and selection circles
-- (docs/research/native-health-bars.md, atlas/p05.md). The selection state that decides when they exist (sprite
-- flags +0x0e, 0x415590) belongs to the unit selection of phase 3.
local core = require("game.images.core")
local image = require("game.images.image")
local grpmeta = require("game.data.grpmeta")

local NONE = core.NONE
local unlink, insert_after, insert_after_head, insert_before =
	core.unlink, core.insert_after, core.insert_after_head, core.insert_before

local M = {}

M.HEALTH_IMAGE, M.BOX_IMAGE = 0x230, 0x247 -- images of the health-bar and box records
M.METHOD_HEALTH, M.METHOD_BOX, M.METHOD_SELECTION, M.METHOD_SHADOW = 11, 15, 13, 10
M.CIRCLE_FIRST, M.CIRCLE_DASHED_FIRST = 0x231, 0x23b -- selection circle images by size
M.BAR_SEGMENT, M.BAR_MIN_WIDTH = 3, 19
M.BAR_ROWS, M.BAR_SHIELD_ROWS, M.BAR_ENERGY_ROWS = 5, 7, 6
M.BAR_Y_GAP = 8 -- rows between the selection circle's middle and the bar
M.SPELLCASTER, M.HALLUCINATION, M.BROODLING = 0x200000, 0x40000000, 40

local floor = math.floor

local function has(v, bit) return v % (2 * bit) >= bit end

function M.install(W)
	-- The record setup of InitImagePools 0x40f8b0 for the health-bar pool (0x40f810: image
	-- 0x230, method 11, its own one-frame header, the image's IScript header) and the box
	-- pool (image 0x247, method 15, its own header, no script).
	function W:init_selection_records()
		local img = self.img
		for i = core.HEALTH_FIRST, core.HEALTH_FIRST + core.HEALTH - 1 do
			img.id[i], img.method[i], img.grp[i] = M.HEALTH_IMAGE, M.METHOD_HEALTH, image.VIRTUAL
			self.vgrp[i] = { width = 0, height = 0, fw = 0, fh = 0 }
			self:image_set_header(i, self.dat.images.iscript_id[M.HEALTH_IMAGE])
		end
		for i = core.BOXES_FIRST, core.BOXES_FIRST + core.BOXES - 1 do
			img.id[i], img.method[i], img.grp[i] = M.BOX_IMAGE, M.METHOD_BOX, image.VIRTUAL
			self.vgrp[i] = { width = 0, height = 0, fw = 0, fh = 0 }
		end
	end

	-- CreateHealthBar 0x40eb60: the free head of the health-bar pool becomes the head of
	-- sprite s (drawn last) and InitHealthBar 0x40f6e0 sizes it for the sprite's selected
	-- unit (hook selection_unit): the width rounded down to a segment plus one (at least 19),
	-- 5 rows (7 with shields) plus 6 with an energy or timer bar, BAR_Y_GAP rows below the
	-- middle of the sprite's selection circle; the unit goes into the parameter; then the
	-- image's Init animation. Returns the image or NONE.
	function W:health_bar_create(s)
		local i = self.free_health.head
		if i == NONE then return NONE end
		local img, spr = self.img, self.spr
		unlink(img.prev, img.next, self.free_health, i)
		local lst = spr.images[s]
		insert_before(img.prev, img.next, lst, lst.head, i)
		local sid = spr.id[s]
		local sprites, units = self.dat.sprites, self.dat.units
		local unit = self:hook("selection_unit")(self, s)
		local t = self:hook("unit_type")(self, unit)
		local status = self:hook("unit_status")(self, unit)
		local circle = self:grp_of(M.CIRCLE_FIRST + sprites.selection_circle_image[sid])
		local _, _, _, circle_h = grpmeta.frame(self.grp, circle, 0)
		img.yoff[i] = image.s8(floor(circle_h / 2) + M.BAR_Y_GAP + sprites.selection_circle_offset[sid])
		local bar = sprites.health_bar[sid]
		-- BYTE arithmetic with the C remainder (toward zero): a zero width gives 1, then 19.
		local w = (bar - math.fmod(bar - 1, M.BAR_SEGMENT)) % 256
		if w < M.BAR_MIN_WIDTH then w = M.BAR_MIN_WIDTH end
		local caster, hallucination = has(units.special_ability_flags[t], M.SPELLCASTER), has(status, M.HALLUCINATION)
		local h = 0
		if (caster and not hallucination) or hallucination or t == M.BROODLING then h = M.BAR_ENERGY_ROWS end
		h = h + (units.shield_enabled[t] ~= 0 and M.BAR_SHIELD_ROWS or M.BAR_ROWS)
		local v = self.vgrp[i]
		v.fw, v.fh, v.width, v.height = w, h, w, h
		img.flags[i], img.sprite[i] = 0, s
		img.param[i] = unit
		self:image_bounds(i)
		self:image_play(i, image.ANIM_INIT)
		return i
	end

	-- RemoveHealthBar 0x40f540: back to the health-bar pool, after its head.
	function W:health_bar_release(i)
		local img = self.img
		unlink(img.prev, img.next, self.spr.images[img.sprite[i]], i)
		insert_after_head(img.prev, img.next, self.free_health, i)
	end

	-- CreateSelectionCircle 0x40ec90 with InitSelectionCircle 0x411460: a circle-pool image
	-- of size image `first` + the sprite's circle size (sprites.dat), just before the
	-- sprite's shadow tail or as its new tail, selection colour row `color` (method 13),
	-- the sprite's circle offset, Init animation. Returns the image or NONE.
	function W:selection_circle_create(s, color, first)
		local i = self.free_circles.head
		if i == NONE then return NONE end
		local img, spr = self.img, self.spr
		unlink(img.prev, img.next, self.free_circles, i)
		local lst = spr.images[s]
		if img.method[lst.tail] == M.METHOD_SHADOW then
			insert_before(img.prev, img.next, lst, lst.tail, i)
		else
			insert_after(img.prev, img.next, lst, lst.tail, i)
		end
		local sid = spr.id[s]
		local id = (first + self.dat.sprites.selection_circle_image[sid]) % 65536
		img.id[i], img.grp[i], img.sprite[i] = id, self:grp_of(id), s
		img.yoff[i] = image.s8(self.dat.sprites.selection_circle_offset[sid])
		img.param[i] = color % 256
		img.method[i] = M.METHOD_SELECTION
		self:image_bounds(i)
		self:image_set_header(i, self.dat.images.iscript_id[id])
		self:image_play(i, image.ANIM_INIT)
		return i
	end
end

return M
