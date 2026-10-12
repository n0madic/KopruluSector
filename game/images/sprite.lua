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
local bor, band = (bit or require("bit")).bor, (bit or require("bit")).band

local M = {}

M.CURSOR_SPRITE = 318
M.SF_SELECTED = 0x08
-- 0x4bc730: unit types whose fog ghosts the interface never targets.
M.UNTARGETED_GHOSTS = { [14] = true, [85] = true, [105] = true, [202] = true, [205] = true, [206] = true,
	[207] = true, [208] = true }
M.METHOD_HEALTH, M.METHOD_SELECTION = 11, 13 -- not copied into fog ghosts (0x415c00)
M.THINGY_LONE = 1 -- thingy +8 at creation
M.ELEVATION_DEFAULT = 4
M.VISIBLE_ALL = 0xff
-- UpdateLoneSpriteThingy 0x418b50: sprite ids using explored-based visibility.
M.EXPLORED_LAST, M.EXPLORED_RANGE_FIRST, M.EXPLORED_RANGE_LAST = 0x81, 0x182, 0x1e0

local trunc_div = int.idiv

function M.install(W)
	-- Main image creation at a list position: shared part of 0x4151f0/0x415260/0x4152d0 and
	-- of the copy 0x415400. `where` places the image in a nonempty list; `copy` sets up the
	-- script without running the Init animation or the bounds.
	local function create_image(w, s, id, xoff, yoff, dir, where, copy)
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
		if copy then w:image_init_script(i) else w:image_init_drawing(i) end
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

	-- CreateImageCopyOnTop 0x415400: an image on top of the sprite's list, without its Init
	-- animation.
	function W:sprite_image_copy_on_top(s, id, xoff, yoff, dir)
		return create_image(self, s, id, xoff, yoff, dir, on_top, true)
	end

	-- CloneSprite 0x415c00: a sprite of the same id, position and owner whose images copy the
	-- source's (tail to head, so the order holds) with their frames and mirroring, without
	-- health bars and selection circles. NONE when a pool runs out.
	function W:sprite_clone(src)
		local spr, img = self.spr, self.img
		local s = self:sprite_create(spr.id[src], s16(spr.x[src]), s16(spr.y[src]), spr.owner[src])
		if s == NONE then return NONE end
		local i = spr.images[s].head
		while i ~= NONE do
			local n = img.next[i]
			self:image_release(i)
			i = n
		end
		spr.main[s] = NONE
		i = spr.images[src].tail
		while i ~= NONE do
			local m = img.method[i]
			if m ~= M.METHOD_HEALTH and m ~= M.METHOD_SELECTION then
				local c = self:sprite_image_copy_on_top(s, img.id[i], img.xoff[i] % 256, img.yoff[i] % 256, img.dir[i])
				if c == NONE then
					self:sprite_destroy(s)
					return NONE
				end
				img.frameset[c], img.frame[c] = img.frameset[i], img.frame[i]
				self:image_set_mirror(c, has(img.flags[i], core.F_MIRROR) and 1 or 0)
				self:image_bounds(c)
				if i == spr.main[src] then spr.main[s] = c end
			end
			i = img.prev[i]
		end
		return s
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
	-- Lone thingies (pool A) and fog-of-war ghosts (pool B).
	---------------------------------------------------------------------------

	-- InitThingyPools 0x4183e0: both free lists in the order 0, 499, ..., 1, then the hidden
	-- placement-cursor thingy (sprite 318 at 0, 0) from pool A.
	function W:init_thingies()
		local thg = self.thg
		local free = self.free_lone
		free.head, free.tail = 0, 0
		for t = 1, core.LONE - 1 do insert_after(thg.prev, thg.next, free, 0, t) end
		local fog = self.fog
		self.free_fog.head, self.free_fog.tail = 0, 0
		for t = 1, core.FOG - 1 do insert_after(fog.prev, fog.next, self.free_fog, 0, t) end
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

	-- Cloak drawing of a sprite's images (cloak.cpp; the unit side is game/vision/detection.lua).
	-- HideUncloakableImages 0x416210: images without draw_if_cloaked are hidden.
	function W:sprite_hide_uncloakable(s)
		local img, draw = self.img, self.dat.images.draw_if_cloaked
		local i = self.spr.images[s].head
		while i ~= NONE do
			if draw[img.id[i]] == 0 then self:image_hide(i) end
			i = img.next[i]
		end
	end

	-- BeginSpriteCloaking 0x415770: methods 0, 4 and 7 start the cloaking effect.
	function W:sprite_begin_cloak(s)
		self:sprite_hide_uncloakable(s)
		local img = self.img
		local i = self.spr.images[s].head
		while i ~= NONE do
			local m = img.method[i]
			if m == 0 or m == 4 or m == 7 then self:image_begin_cloak(i) end
			i = img.next[i]
		end
	end

	-- SetSpriteCloakMethods 0x415e50 (mode 1 cloaking 2, mode 2 decloaking 4 with the
	-- parameter bytes; mode 3 cloaked 3 for methods 0, 2 and 4).
	function W:sprite_set_cloak_mode(s, mode, p0, p1)
		self:sprite_hide_uncloakable(s)
		local img = self.img
		local i = self.spr.images[s].head
		while i ~= NONE do
			local m = img.method[i]
			if mode == 1 and m == 0 then
				self:image_set_method(i, 2)
				img.param[i] = img.param[i] - img.param[i] % 65536 + p0 + p1 * 256
			elseif mode == 2 and m == 0 then
				self:image_set_method(i, 4)
				img.param[i] = img.param[i] - img.param[i] % 65536 + p0 + p1 * 256
			elseif mode == 3 and (m == 0 or m == 2 or m == 4) then
				self:image_set_method(i, 3)
			end
			i = img.next[i]
		end
	end

	-- ShowCloakedSprite 0x417400 (the local player detects it): hidden sprites' images
	-- (flag 0x40) drop methods 2..7 to 0; visible ones show cloaked methods as 5..7 (0 as 6).
	function W:sprite_cloak_detected(s)
		local img = self.img
		local i = self.spr.images[s].head
		if has(self.spr.flags[s], 0x40) then
			while i ~= NONE do
				local m = img.method[i]
				if m > 1 and m < 8 then self:image_set_method(i, 0) end
				i = img.next[i]
			end
			return
		end
		while i ~= NONE do
			local m = img.method[i]
			if m >= 2 and m <= 4 then
				self:image_set_method(i, m + 3)
			elseif m == 0 then
				self:image_set_method(i, 6)
			end
			i = img.next[i]
		end
	end

	-- HideCloakedSprite 0x4161c0 (detection lost): 5..7 back to 2..4, or cloaked for hidden
	-- sprites.
	function W:sprite_cloak_undetected(s)
		if has(self.spr.flags[s], 0x40) then
			self:sprite_set_cloak_mode(s, 3, 0, 0)
			return
		end
		local img = self.img
		local i = self.spr.images[s].head
		while i ~= NONE do
			local m = img.method[i]
			if m > 4 and m < 8 then self:image_set_method(i, m - 3) end
			i = img.next[i]
		end
	end

	-- UpdateThingyVisibilityRect 0x417d20 on the thingy's sprite s: seen-based over a width x
	-- height footprint. Returns true when the sprite stops being visible to the local vision.
	function W:sprite_visibility_rect(s, width_px, height_px)
		local spr = self.spr
		if s == NONE or has(spr.flags[s], SF_HIDDEN) then return false end
		local width = floor((width_px + 31) / 32)
		local left = trunc_div(s16(spr.x[s]) - trunc_div(width_px, 2), 32)
		local height = floor((height_px + 31) / 32)
		local top = trunc_div(s16(spr.y[s]) - trunc_div(height_px, 2), 32)
		if left < 0 then
			width = width + left
			if width < 1 then return false end
			left = 0
		end
		if top < 0 then
			height = height + top
			if height < 1 then return false end
			top = 0
		end
		if width + left > self.map_w then
			width = self.map_w - left
			if width <= 0 then return false end
		end
		if height + top > self.map_h then
			height = self.map_h - top
			if height <= 0 then return false end
		end
		local mask = rect_mask(self, self.hidden_at, left, top, width, height)
		local old = spr.visibility[s]
		if old == mask then return false end
		self:sprite_set_visibility(s, mask)
		return band(old, self.local_vision) ~= 0 and band(mask, self.local_vision) == 0
	end

	function W:thingy_visibility_rect(t, width_px, height_px)
		return self:sprite_visibility_rect(self.thg.sprite[t], width_px, height_px)
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

	-- CreateFogThingy 0x417b10: a ghost of the sprite for a unit type, or NONE.
	function W:fog_create(unit_type, src)
		local fog = self.fog
		local t = self.free_fog.head
		if t == NONE then return NONE end
		unlink(fog.prev, fog.next, self.free_fog, t)
		local s = self:sprite_clone(src)
		fog.kind[t], fog.sprite[t] = M.THINGY_LONE, s
		if s == NONE then
			insert_after_head(fog.prev, fog.next, self.free_fog, t)
			return NONE
		end
		fog.kind[t] = unit_type
		insert_after_head(fog.prev, fog.next, self.active_fog, t)
		return t
	end

	-- FogThingyAtPoint 0x417f70: the first fog ghost (of a type the interface can target,
	-- 0x4bc730) whose sprite box, centred on its type's placement box, holds the point.
	function W:fog_at_point(x, y)
		local fog, spr = self.fog, self.spr
		local box = self.dat.units.staredit_placement_size
		local t = self.active_fog.head
		while t ~= NONE do
			local k = fog.kind[t]
			if not M.UNTARGETED_GHOSTS[k] then
				local s = fog.sprite[t]
				local dx = (trunc_div(s16(box[k * 2]), 2) + s16(spr.x[s]) - x) % 4294967296
				local dy = (trunc_div(s16(box[k * 2 + 1]), 2) + s16(spr.y[s]) - y) % 4294967296
				if dx < spr.width[s] and dy < spr.height[s] then return t end
			end
			t = fog.next[t]
		end
		return NONE
	end

	-- FlashSprite 0x43a540: while the sprite's flash timer runs, its selection circle (in
	-- `color`) shows in the steps whose timer has bit 3 set; a sprite the local player cannot
	-- see stops flashing; at the end only a selected sprite keeps its circle.
	function W:sprite_flash(s, color)
		local spr = self.spr
		if spr.sel_timer[s] == 0 then return end
		local t = spr.sel_timer[s] - 1
		if band(spr.visibility[s], self.local_vision) == 0 then t = 0 end
		spr.sel_timer[s] = t
		if band(t, 8) ~= 0 or (t == 0 and band(spr.flags[s], M.SF_SELECTED) ~= 0) then
			self:sprite_add_circle(s, color)
		else
			self:sprite_remove_circle(s)
		end
	end

	-- AreaHiddenFromLocal 0x418eb0: no tile of the rectangle is seen by the local vision.
	local function hidden_from_local(w, left, top, width, height)
		if w.replay ~= 0 then error("images: fog ghosts in a replay (0x418eb0)", 0) end
		local flags, map_w, vision = w.tile_flags, w.map_w, w.local_vision
		for y = top + height - 1, top, -1 do
			for x = left + width - 1, left, -1 do
				if band(flags[y * map_w + x], vision) == 0 then return false end
			end
		end
		return true
	end

	-- UpdateFogThingy 0x418c20: the ghost flashes in its owner's circle colour and goes once
	-- the local player sees any tile of its unit type's placement box.
	function W:fog_update(t)
		local fog, spr = self.fog, self.spr
		local s = fog.sprite[t]
		self:sprite_flash(s, self.circle_colors[spr.owner[s]])
		local box = self.dat.units.staredit_placement_size
		local k = fog.kind[t]
		local bw, bh = s16(box[k * 2]), s16(box[k * 2 + 1])
		local left = trunc_div(s16(spr.x[s]) - trunc_div(bw, 2), 32)
		local top = trunc_div(s16(spr.y[s]) - trunc_div(bh, 2), 32)
		if hidden_from_local(self, left, top, trunc_div(bw + 31, 32), trunc_div(bh + 31, 32)) then return end
		self:sprite_remove_circle(s)
		self:sprite_destroy(s)
		fog.sprite[t] = NONE
		unlink(fog.prev, fog.next, self.active_fog, t)
		insert_after_head(fog.prev, fog.next, self.free_fog, t)
	end

	-- UpdateThingies 0x418990: pool A, then the fog ghosts.
	function W:update_thingies()
		local nxt = self.thg.next
		local t = self.active_lone.head
		while t ~= NONE do
			local n = nxt[t]
			self:lone_update(t)
			t = n
		end
		nxt = self.fog.next
		t = self.active_fog.head
		while t ~= NONE do
			local n = nxt[t]
			self:fog_update(t)
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
