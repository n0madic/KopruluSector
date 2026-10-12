-- Sprite and lone-thingy operations (docs/research/atlas/p05.md "Sprites" and "Thingies",
-- p06.md 0x418990/0x418b50). Methods of game.images.world's World; `s` is a sprite record,
-- `t` a lone-thingy record. Sprite x/y are kept as raw WORDs, as in the record.
local core = require("game.images.core")
local image = require("game.images.image")
local int = require("game.simulation.int")

local NONE = core.NONE
local SF_HIDDEN, SF_NOBRK = core.SF_HIDDEN, core.SF_NOBRK
local unlink, insert_after, insert_after_head, insert_before =
	core.unlink, core.insert_after, core.insert_after_head, core.insert_before
local has, s16 = image.has, image.s16
local floor = math.floor
local bor = (bit or require("bit")).bor

local M = {}

M.CURSOR_SPRITE = 318
M.THINGY_LONE = 1 -- thingy +8 at creation
M.ELEVATION_DEFAULT = 4
M.VISIBLE_ALL = 0xff
-- UpdateLoneSpriteThingy 0x418b50: sprite ids using explored-based visibility.
M.EXPLORED_LAST, M.EXPLORED_RANGE_FIRST, M.EXPLORED_RANGE_LAST = 0x81, 0x182, 0x1e0

local trunc_div = int.idiv

function M.install(W)
	-- Main image creation at a list position: shared part of 0x4151f0/0x415260/0x4152d0.
	-- `where` places the image in a nonempty list.
	local function create_image(w, s, id, xoff, yoff, dir, where)
		local i = w:image_alloc()
		if i == NONE then return NONE end
		local img, spr = w.img, w.spr
		local lst = spr.images[s]
		if lst.head == NONE then
			spr.main[s] = i
			insert_after_head(img.prev, img.next, lst, i)
		else
			where(img, lst, spr.main[s], i)
		end
		w:image_init_record(i, id, s, xoff, yoff)
		w:image_init_drawing(i)
		w:image_set_direction(i, dir)
		return i
	end

	local function above_main(img, lst, main, i) insert_before(img.prev, img.next, lst, main, i) end
	local function on_top(img, lst, _, i) insert_before(img.prev, img.next, lst, lst.head, i) end
	local function below_main(img, lst, main, i) insert_after(img.prev, img.next, lst, main, i) end

	-- CreateImageAboveMain 0x4151f0 / CreateImageOnTop 0x415260 / CreateImageBelowMain 0x4152d0.
	function W:sprite_image_above_main(s, id, xoff, yoff, dir)
		return create_image(self, s, id, xoff, yoff, dir, above_main)
	end

	function W:sprite_image_on_top(s, id, xoff, yoff, dir)
		return create_image(self, s, id, xoff, yoff, dir, on_top)
	end

	function W:sprite_image_below_main(s, id, xoff, yoff, dir)
		return create_image(self, s, id, xoff, yoff, dir, below_main)
	end

	-- SetSpriteVisibility 0x417280 (the redraw side effects are presentation's).
	function W:sprite_set_visibility(s, mask)
		self.spr.visibility[s] = mask % 256
	end

	-- InitSprite 0x4162f0: x and y are WORDs compared unsigned with the map pixel size.
	function W:sprite_init(s, id, x, y, owner)
		x, y = x % 65536, y % 65536
		if x >= self.map_w_px or y >= self.map_h_px then return false end
		local spr = self.spr
		spr.owner[s] = owner % 256
		spr.id[s] = id
		spr.elevation[s], spr.flags[s] = M.ELEVATION_DEFAULT, 0
		spr.x[s], spr.y[s] = x, y
		spr.visibility[s] = M.VISIBLE_ALL
		spr.sel_timer[s] = 0
		if self.dat.sprites.is_visible[id] == 0 then
			spr.flags[s] = SF_HIDDEN
			self:sprite_set_visibility(s, 0)
		end
		local i = self:sprite_image_above_main(s, self.dat.sprites.image[id], 0, 0, 0)
		if i == NONE then return false end
		local _, _, _, _, cw, ch = self:frame_rect(spr.main[s], 0)
		spr.width[s], spr.height[s] = math.min(cw, 255), math.min(ch, 255)
		return true
	end

	-- CreateSprite 0x4154a0: returns the sprite or NONE.
	function W:sprite_create(id, x, y, owner)
		local s = self.free_sprites.head
		if s == NONE then return NONE end
		local spr = self.spr
		unlink(spr.prev, spr.next, self.free_sprites, s)
		if not self:sprite_init(s, id, x, y, owner) then
			insert_after_head(spr.prev, spr.next, self.free_sprites, s)
			return NONE
		end
		insert_after_head(spr.prev, spr.next, self.rows[self:sprite_row(s)], s)
		return s
	end

	-- DestroySprite 0x416720.
	function W:sprite_destroy(s)
		local img, spr = self.img, self.spr
		local i = spr.images[s].head
		while i ~= NONE do
			local n = img.next[i]
			self:image_release(i)
			i = n
		end
		unlink(spr.prev, spr.next, self.rows[self:sprite_row(s)], s)
		insert_after_head(spr.prev, spr.next, self.free_sprites, s)
	end

	-- MoveSprite 0x417330.
	function W:sprite_move(s, x, y)
		local spr = self.spr
		x, y = x % 65536, y % 65536
		if spr.x[s] == x and spr.y[s] == y then return end
		local old = self:sprite_row(s)
		spr.x[s], spr.y[s] = x, y
		local new = self:sprite_row(s)
		if new ~= old then
			unlink(spr.prev, spr.next, self.rows[old], s)
			insert_after_head(spr.prev, spr.next, self.rows[new], s)
		end
	end

	-- UpdateSpriteImages 0x417860: update callback and IScript of every image, head to
	-- tail with the next link saved first; an emptied sprite returns to the free list.
	function W:sprite_update(s)
		local img, spr = self.img, self.spr
		local lst = spr.images[s]
		local i = lst.head
		while i ~= NONE do
			local n = img.next[i]
			self:image_update(i)
			self:run_iscript(i)
			i = n
			if lst.head == NONE then
				unlink(spr.prev, spr.next, self.rows[self:sprite_row(s)], s)
				insert_after_head(spr.prev, spr.next, self.free_sprites, s)
			end
		end
	end

	-- PlaySpriteAnimation 0x417220: the next link is read after each animation.
	function W:sprite_play(s, anim, force)
		local img = self.img
		local i = self.spr.images[s].head
		while i ~= NONE do
			if force or not has(self.spr.flags[s], SF_NOBRK) then self:image_play(i, anim) end
			i = img.next[i]
		end
	end

	-- SetSpriteDirectionFrame 0x415f00 / SetSpriteFacing 0x417190.
	function W:sprite_set_direction(s, d)
		local img = self.img
		local i = self.spr.images[s].head
		while i ~= NONE do
			self:image_set_direction(i, d)
			i = img.next[i]
		end
	end

	function W:sprite_set_facing(s, heading)
		local img = self.img
		local i = self.spr.images[s].head
		while i ~= NONE do
			self:image_direction_from_angle(i, heading)
			i = img.next[i]
		end
	end

	-- RefreshSpriteImageBounds 0x416520 (world part): bounds of every image from its current
	-- frame, offsets and the sprite position. An image whose frame lies outside its GRP keeps
	-- its last bounds (the original reads past the frame table there).
	function W:sprite_refresh_bounds(s)
		local img = self.img
		local i = self.spr.images[s].head
		while i ~= NONE do
			local g = img.grp[i]
			if (g > 0 and img.frame[i] < self.grp.grps[g].frames) or (g == image.VIRTUAL and self.vgrp[i]) then
				self:image_bounds(i)
			end
			i = img.next[i]
		end
	end

	-- RefreshRowSpriteBounds 0x4165d0 over every row, at the end of a tick. The original's
	-- draw callback runs it before each redraw for the rows from 4 above the view's top to
	-- the map's bottom (0x445de0), which makes the bounds depend on the camera; refreshing every
	-- row once per tick gives the same bounds for every drawn sprite while keeping the
	-- simulation independent of the view. Presentation only reads them.
	function W:refresh_bounds()
		local nxt = self.spr.next
		for r = 0, self.map_h - 1 do
			local s = self.rows[r].head
			while s ~= NONE do
				self:sprite_refresh_bounds(s)
				s = nxt[s]
			end
		end
	end

	-- GetSpriteDepthKey 0x4160b0: unsigned 32-bit draw-order key.
	function W:sprite_depth_key(s)
		local spr = self.spr
		local elev = spr.elevation[s]
		local key = int.shl(elev, 13)
		if elev <= 4 then key = int.bor(key, int.u32(s16(spr.y[s]))) end
		key = int.bor(int.shl(key, 5), int.band(spr.flags[s], core.SF_DEPTH))
		return int.bor(int.shl(key, 9), spr.index[s])
	end

	---------------------------------------------------------------------------
	-- Lone thingies (pool A). TODO: fog thingies (pool B) arrive with fog of war in phase 3.
	---------------------------------------------------------------------------

	-- InitThingyPools 0x4183e0 (pool A part): free order 0, 499, ..., 1, then the hidden
	-- placement-cursor thingy (sprite 318 at 0, 0).
	function W:init_thingies()
		local thg = self.thg
		local free = self.free_lone
		free.head, free.tail = 0, 0
		for t = 1, core.LONE - 1 do insert_after(thg.prev, thg.next, free, 0, t) end
		local t = free.head
		unlink(thg.prev, thg.next, free, t)
		thg.kind[t] = M.THINGY_LONE
		local s = self:sprite_create(M.CURSOR_SPRITE, 0, 0, 0)
		thg.sprite[t] = s
		if s == NONE then
			insert_after_head(thg.prev, thg.next, free, t)
			error("images: no sprite for the placement cursor")
		end
		insert_after_head(thg.prev, thg.next, self.active_lone, t)
		self.cursor = t
		self.spr.flags[s] = has(self.spr.flags[s], SF_HIDDEN) and self.spr.flags[s] or self.spr.flags[s] + SF_HIDDEN
		self:sprite_set_visibility(s, 0)
	end

	-- CreateLoneThingy 0x417b80: returns the thingy or NONE.
	function W:lone_create(id, x, y, owner)
		local thg = self.thg
		local t = self.free_lone.head
		if t == NONE then return NONE end
		unlink(thg.prev, thg.next, self.free_lone, t)
		thg.kind[t] = M.THINGY_LONE
		local s = self:sprite_create(id, x, y, owner)
		thg.sprite[t] = s
		if s == NONE then
			insert_after_head(thg.prev, thg.next, self.free_lone, t)
			return NONE
		end
		insert_after_head(thg.prev, thg.next, self.active_lone, t)
		return t
	end

	-- UpdateThingyVisibility 0x417e50: visibility from the tile under the sprite centre.
	function W:thingy_visibility(t)
		local s = self.thg.sprite[t]
		if s == NONE or has(self.spr.flags[s], SF_HIDDEN) then return end
		local tx = trunc_div(s16(self.spr.x[s]), 32)
		local ty = trunc_div(s16(self.spr.y[s]), 32)
		local mask = 255 - self:hidden_at(tx, ty)
		if self.spr.visibility[s] ~= mask then self:sprite_set_visibility(s, mask) end
	end

	-- OR of complemented tile bytes over a tile rectangle (0x418d00 explored / 0x418d80 seen).
	local function rect_mask(w, reader, x, y, width, height)
		local mask = 0
		for row = y + height - 1, y, -1 do
			for col = x + width - 1, x, -1 do
				mask = bor(mask, 255 - reader(w, col, row))
			end
		end
		return mask % 256
	end

	-- UpdateFogThingyVisibility 0x417c10: explored-based, over the sprite's own size.
	function W:thingy_visibility_explored(t)
		local spr = self.spr
		local s = self.thg.sprite[t]
		if has(spr.flags[s], SF_HIDDEN) then return end
		local sw, sh = spr.width[s], spr.height[s]
		local width = floor((sw + 31) / 32)
		local left = trunc_div(s16(spr.x[s]) - floor(sw / 2), 32)
		local height = floor((sh + 31) / 32)
		local top = trunc_div(s16(spr.y[s]) - floor(sh / 2), 32)
		if left < 0 then width, left = width + left, 0
		elseif width + left > self.map_w then width = self.map_w - left end
		if top < 0 then height, top = height + top, 0
		elseif height + top > self.map_h then height = self.map_h - top end
		local mask
		if self.replay ~= 0 and self.replay_reveal then mask = 0xff
		else mask = rect_mask(self, self.unexplored_at, left, top, width, height) end
		if spr.visibility[s] ~= mask then self:sprite_set_visibility(s, mask) end
	end

	-- UpdateThingyVisibilityRect 0x417d20: seen-based over a width x height footprint.
	function W:thingy_visibility_rect(t, width_px, height_px)
		local spr = self.spr
		local s = self.thg.sprite[t]
		if s == NONE or has(spr.flags[s], SF_HIDDEN) then return end
		local width = floor((width_px + 31) / 32)
		local left = trunc_div(s16(spr.x[s]) - trunc_div(width_px, 2), 32)
		local height = floor((height_px + 31) / 32)
		local top = trunc_div(s16(spr.y[s]) - trunc_div(height_px, 2), 32)
		if left < 0 then
			width = width + left
			if width < 1 then return end
			left = 0
		end
		if top < 0 then
			height = height + top
			if height < 1 then return end
			top = 0
		end
		if width + left > self.map_w then
			width = self.map_w - left
			if width <= 0 then return end
		end
		if height + top > self.map_h then
			height = self.map_h - top
			if height <= 0 then return end
		end
		local mask = rect_mask(self, self.hidden_at, left, top, width, height)
		if spr.visibility[s] ~= mask then self:sprite_set_visibility(s, mask) end
	end

	-- UpdateLoneSpriteThingy 0x418b50.
	function W:lone_update(t)
		local thg, spr = self.thg, self.spr
		local s = thg.sprite[t]
		local id = spr.id[s]
		if id <= M.EXPLORED_LAST or (id >= M.EXPLORED_RANGE_FIRST and id <= M.EXPLORED_RANGE_LAST) then
			self:thingy_visibility_explored(t)
		else
			self:thingy_visibility_rect(t, spr.width[s], spr.height[s])
		end
		if thg.sprite[t] ~= NONE then
			self:sprite_update(thg.sprite[t])
			if spr.images[thg.sprite[t]].head == NONE then thg.sprite[t] = NONE end
		end
		if thg.sprite[t] == NONE then
			unlink(thg.prev, thg.next, self.active_lone, t)
			insert_after_head(thg.prev, thg.next, self.free_lone, t)
		end
	end

	-- UpdateThingies 0x418990 (pool A).
	function W:update_thingies()
		local nxt = self.thg.next
		local t = self.active_lone.head
		while t ~= NONE do
			local n = nxt[t]
			self:lone_update(t)
			t = n
		end
	end

	-- CreateSpriteRelativeToImage 0x410320: lone thingy at the sprite position plus the
	-- image offset and the delta (owner 0), then its elevation and visibility.
	function W:image_spawn_sprite(i, id, dx, dy, elevation)
		local img, spr = self.img, self.spr
		local s = img.sprite[i]
		local x = s16(spr.x[s]) + img.xoff[i] + dx
		local y = s16(spr.y[s]) + img.yoff[i] + dy
		local t = self:lone_create(id, x, y, 0)
		if t == NONE then return NONE end
		spr.elevation[self.thg.sprite[t]] = elevation % 256
		self:thingy_visibility(t)
		return t
	end
end

return M
