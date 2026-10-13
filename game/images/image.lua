-- Image record operations (docs/research/atlas/p04.md, "Image state helpers", and p03.md
-- "Image callbacks"). Methods of game.images.world's World; `i` is an image key.
--
-- Representation notes:
-- - Offsets (+0x0e/+0x0f) are kept as signed bytes, world positions (+0x1c/+0x1e) as signed
--   WORDs, the IScript state as numbers.
-- - `grp` is the grpmeta entry of the image's GRP, 0 for none (the free-record marker of
--   the original), or VIRTUAL for the per-record headers of health bars and placement boxes
--   (w.vgrp[i] = { width =, height =, fw =, fh = }).
-- - `param` is the drawing parameter DWORD +0x30. For draw method 9 it holds the remapping
--   index (1..7) instead of the table pointer, as the original's saves do.
local core = require("game.images.core")
local grpmeta = require("game.data.grpmeta")
local bytes = require("game.data.bytes")
local int = require("game.simulation.int")

local NONE = core.NONE
local F_MIRROR, F_HOLD_Y, F_TURNS, F_FULL_ISCRIPT = core.F_MIRROR, core.F_HOLD_Y, core.F_TURNS, core.F_FULL_ISCRIPT
local F_HIDDEN, F_FOLLOW = core.F_HIDDEN, core.F_FOLLOW
local unlink, insert_after, insert_after_head, insert_before =
	core.unlink, core.insert_after, core.insert_after_head, core.insert_before
local u16 = bytes.u16
local floor = math.floor

local M = {}

M.VIRTUAL = -1
M.METHOD_WARP_FIRE = 17
M.WARP_FIRE_PARAM = 0x230
M.WARP_TEXTURE = 210 -- image whose GRP method 12 draws through (native pointer slot 0x543a40)
M.METHOD_REMAP = 9
M.METHOD_CLOAK, M.METHOD_DECLOAK, M.METHOD_DECLOAK_BLEND = 2, 4, 7
M.CLOAK_START, M.DECLOAK_START = 0x300, 0x308 -- parameter byte and timer 3 in the low WORD
M.METHOD_PLAYER_COLOR = 14
M.METHOD_HALLUCINATION = 16
M.LO_COLUMNS = { [0] = "attack_overlay", "damage_overlay", "special_overlay", "landing_dust_overlay",
	"lift_off_dust_overlay" }
M.LO_SPECIAL = 2
M.DAMAGE_SLOTS, M.LO_UNUSED = 22, 127
-- Animations with special handling in PlayImageAnimation 0x411730.
M.ANIM_INIT, M.ANIM_DEATH, M.ANIM_GND_ATTK_INIT, M.ANIM_AIR_ATTK_INIT = 0, 1, 2, 3
M.ANIM_GND_ATTK_RPT, M.ANIM_AIR_ATTK_RPT, M.ANIM_WALKING, M.ANIM_IS_WORKING = 5, 6, 11, 19
-- Unit status bits read by image code (CUnit +0xdc).
M.STATUS_CLOAKED = 0x300
M.STATUS_HALLUCINATION = 0x40000000

local VIRTUAL = M.VIRTUAL

local s8, s16 = int.s8, int.s16
M.s8, M.s16 = s8, s16

local function has(flags, bit) return flags % (bit * 2) >= bit end
M.has = has

local function set(flags, bit, on)
	if has(flags, bit) then
		if not on then return flags - bit end
	elseif on then
		return flags + bit
	end
	return flags
end
M.set = set

-- Unit status with a cloak state (bits 0x300: cloaked or requiring detection).
function M.cloaked(status) return int.band(status, M.STATUS_CLOAKED) ~= 0 end

-- Method 17 keeps its packed state 0x0230 in the low WORD of the drawing parameter; every
-- method installation (0x4116e0, the direction and mirror setters, 0x40fb50) rewrites it.
local function install_method_state(img, i)
	if img.method[i] == M.METHOD_WARP_FIRE then
		local p = img.param[i]
		img.param[i] = p - p % 65536 + M.WARP_FIRE_PARAM
	end
end

function M.install(W)
	-- GRP entry for an image id (images.dat GRP column through images.tbl), 0 for none.
	function W:grp_of(id)
		return grpmeta.image_grp(self.grp, self.dat.images, id) or 0
	end

	-- Frame count (WORD 0 of the GRP header).
	function W:grp_frames(i)
		local g = self.img.grp[i]
		if g == VIRTUAL then return 1 end
		if g == 0 then error("images: image " .. i .. " has no GRP", 2) end
		return self.grp.grps[g].frames
	end

	-- Frame x, y, width, height and canvas width, height of image i's current frame.
	function W:frame_rect(i, frame)
		local g = self.img.grp[i]
		frame = frame or self.img.frame[i]
		if g == VIRTUAL then
			local v = self.vgrp[i]
			return 0, 0, v.fw, v.fh, v.width, v.height
		end
		if g == 0 then error("images: image " .. i .. " has no GRP", 2) end
		local e = self.grp.grps[g]
		if frame >= e.frames then
			error(("images: frame %d of image %d (id %d) beyond its %d frames"):format(frame, i, self.img.id[i], e.frames), 2)
		end
		local fx, fy, fw, fh = grpmeta.frame(self.grp, g, frame)
		return fx, fy, fw, fh, e.width, e.height
	end

	-- GetImageLoOffset 0x40f5d0: LO `kind` point `point` at image i's frame, x negated
	-- when the image is mirrored. The original reads unchecked.
	function W:lo_offset(i, kind, point)
		local img = self.img
		local id = img.id[i]
		local lo = self.grp.los[grpmeta.image_lo(self.grp, self.dat.images, M.LO_COLUMNS[kind], id) or 0]
		if not lo then error(("images: image id %d has no LO of kind %d"):format(id, kind), 2) end
		local x, y = grpmeta.lo_point(lo, img.frame[i], point)
		if has(img.flags[i], F_MIRROR) then x = -x end
		return x, y
	end

	-- Damage-overlay slots of image `id` (the table 0x543310 filled at image loading,
	-- docs/research/atlas/p04.md): twice the number of the first 22 points of frame 0 of its
	-- damage LO that are not (127, 127); 0 without a damage LO.
	function W:damage_overlay_slots(id)
		local cache = self.overlay_slots
		if not cache then
			cache = {}
			self.overlay_slots = cache
		end
		local v = cache[id]
		if v then return v end
		v = 0
		local lo_id = grpmeta.image_lo(self.grp, self.dat.images, "damage_overlay", id)
		local lo = lo_id and self.grp.los[lo_id]
		if lo then
			local count = 0
			for point = 0, M.DAMAGE_SLOTS - 1 do
				if point < lo.points then
					local x, y = grpmeta.lo_point(lo, 0, point)
					if x ~= M.LO_UNUSED or y ~= M.LO_UNUSED then count = count + 1 end
				end
			end
			v = (count * 2) % 256
		end
		cache[id] = v
		return v
	end

	-- Special-LO point 0 of the sprite's main image (follow-main offsets), x negated when
	-- the main image is mirrored: shared part of 0x40f280, 0x40f3a0 and 0x410380.
	local function follow_point(w, i)
		local img = w.img
		local main = w.spr.main[img.sprite[i]]
		local lo = w.grp.los[grpmeta.image_lo(w.grp, w.dat.images, M.LO_COLUMNS[M.LO_SPECIAL], img.id[main]) or 0]
		if not lo then error(("images: image id %d has no special LO"):format(img.id[main]), 2) end
		local x, y = grpmeta.lo_point(lo, img.frame[main], 0)
		if has(img.flags[main], F_MIRROR) then x = s8(-x) end
		return x, y
	end

	-- SetImageDrawMethod 0x4116e0 (callbacks follow from method and mirror bit).
	function W:image_set_method(i, method)
		self.img.method[i] = method
		install_method_state(self.img, i)
	end

	-- The "reinstall callbacks" step of the direction/mirror setters: only method 17 has a
	-- visible effect on the record.
	local reinstall = install_method_state

	-- SetImageScriptHeader 0x40f340; a missing id is fatal in the original.
	function W:image_set_header(i, script_id)
		local h = self.script.headers[script_id]
		if not h then error(("iscript: script %d does not exist"):format(script_id), 2) end
		self.img.header[i] = h
	end

	-- InitImageRecord 0x40fc50.
	function W:image_init_record(i, id, sprite, xoff, yoff)
		local img, images = self.img, self.dat.images
		img.id[i] = id
		img.grp[i] = self:grp_of(id)
		img.flags[i] = (images.gfx_turns[id] % 2) * F_TURNS + (images.clickable[id] % 2) * core.F_CLICKABLE
		img.frameset[i], img.frame[i], img.dir[i] = 0, 0, 0
		img.xoff[i], img.yoff[i] = s8(xoff), s8(yoff)
		img.sprite[i] = sprite
		img.param[i] = 0
		img.header[i], img.pc[i], img.ret[i], img.anim[i], img.wait[i] = 0, 0, 0, 0, 0
		local fn = images.draw_function[id]
		if fn == M.METHOD_PLAYER_COLOR then img.param[i] = self.spr.owner[sprite] end
		if fn == M.METHOD_REMAP then img.param[i] = images.remapping[id] end
	end

	-- The DAT method, full-IScript flag and script header of a new image (the shared part of
	-- 0x40fb50 and the copy of 0x415400).
	function W:image_init_script(i)
		local img, images = self.img, self.dat.images
		local id = img.id[i]
		img.method[i] = images.draw_function[id]
		install_method_state(img, i)
		img.flags[i] = set(img.flags[i], F_FULL_ISCRIPT, images.use_full_iscript[id] % 2 == 1)
		self:image_set_header(i, images.iscript_id[id])
	end

	-- InitImageDrawingAndScript 0x40fb50: the script set up, then the Init animation (runs
	-- the VM) and bounds.
	function W:image_init_drawing(i)
		self:image_init_script(i)
		self:image_play(i, M.ANIM_INIT)
		self:image_bounds(i)
	end

	-- PlayImageAnimation 0x411730. A world's `play_probe` (tests) is called first with the
	-- requested animation, where the original-code probes break.
	function W:image_play(i, anim)
		if self.play_probe then self.play_probe(self, i, anim) end
		local img = self.img
		local cur = img.anim[i]
		if anim == M.ANIM_DEATH and cur == M.ANIM_DEATH then return end
		if not has(img.flags[i], F_FULL_ISCRIPT) and anim ~= M.ANIM_INIT and anim ~= M.ANIM_DEATH then return end
		if anim == cur and (anim == M.ANIM_WALKING or anim == M.ANIM_IS_WORKING) then return end
		if anim == M.ANIM_GND_ATTK_RPT then
			if cur ~= M.ANIM_GND_ATTK_RPT and cur ~= M.ANIM_GND_ATTK_INIT then anim = M.ANIM_GND_ATTK_INIT end
		elseif anim == M.ANIM_AIR_ATTK_RPT then
			if cur ~= M.ANIM_AIR_ATTK_RPT and cur ~= M.ANIM_AIR_ATTK_INIT then anim = M.ANIM_AIR_ATTK_INIT end
		end
		img.anim[i] = anim
		img.pc[i] = u16(self.script.code, img.header[i] + 8 + anim * 2)
		img.wait[i], img.ret[i] = 0, 0
		self:run_iscript(i)
	end

	-- UpdateImageBounds 0x40ee70, world part: frame rectangle relative to the sprite.
	function W:image_bounds(i)
		local img = self.img
		local s = img.sprite[i]
		local fx, fy, fw, _, cw, ch = self:frame_rect(i)
		local half_w, half_h = floor(cw / 2), floor(ch / 2)
		local x = s16(self.spr.x[s]) + img.xoff[i]
		if has(img.flags[i], F_MIRROR) then x = x + half_w - fw - fx else x = x + fx - half_w end
		img.x[i] = s16(x)
		if not has(img.flags[i], F_HOLD_Y) then
			img.y[i] = s16(s16(self.spr.y[s]) + img.yoff[i] + fy - half_h)
		end
	end

	-- SyncImageFrame 0x40ee50.
	function W:image_sync_frame(i)
		local img = self.img
		local f = (img.frameset[i] + img.dir[i]) % 65536
		if img.frame[i] ~= f then img.frame[i] = f end
	end

	-- SetImageFrameSet 0x411690 (IScript playfram).
	function W:image_set_frameset(i, f)
		local img = self.img
		if img.frameset[i] == f then return end
		img.frameset[i] = f % 65536
		local frame = (img.dir[i] + img.frameset[i]) % 65536
		if img.frame[i] ~= frame then img.frame[i] = frame end
	end

	-- RefreshFollowMainOffset 0x410380.
	function W:image_refresh_follow(i)
		local x, y = follow_point(self, i)
		local img = self.img
		if img.xoff[i] ~= x or img.yoff[i] ~= y then img.xoff[i], img.yoff[i] = x, y end
	end

	-- SetImageDirection 0x40f1c0: d is a byte 0..32 (values above 16 mirror).
	function W:image_set_direction(i, d)
		local img = self.img
		local flags = img.flags[i]
		if not has(flags, F_TURNS) then return end
		d = d % 256
		local mirror = d > 16
		if mirror then d = (32 - d) % 256 end
		if has(flags, F_MIRROR) == mirror and img.dir[i] == d then return end
		img.dir[i] = d
		img.flags[i] = set(flags, F_MIRROR, mirror)
		reinstall(img, i)
		local frame = (img.frameset[i] + d) % 65536
		if img.frame[i] ~= frame then img.frame[i] = frame end
		if has(img.flags[i], F_FOLLOW) then self:image_refresh_follow(i) end
	end

	-- SetImageDirectionFromAngle 0x411580: 256-angle heading.
	function W:image_direction_from_angle(i, angle)
		local img = self.img
		if has(img.flags[i], F_FOLLOW) then self:image_refresh_follow(i) end
		if not has(img.flags[i], F_TURNS) then return end
		local q = floor((angle % 256 + 4) / 8)
		local mirror = q > 16
		if mirror then q = 32 - q end
		if has(img.flags[i], F_MIRROR) == mirror and img.dir[i] == q then return end
		img.dir[i] = q
		img.flags[i] = set(img.flags[i], F_MIRROR, mirror)
		reinstall(img, i)
		local frame = (img.frameset[i] + q) % 65536
		if img.frame[i] ~= frame then img.frame[i] = frame end
	end

	-- SetImageMirror 0x40f280 (IScript setflipstate): compares the mirror bit with the
	-- whole byte argument.
	function W:image_set_mirror(i, f)
		local img = self.img
		local cur = has(img.flags[i], F_MIRROR) and 1 or 0
		if cur == f then return end
		img.flags[i] = set(img.flags[i], F_MIRROR, f % 2 == 1)
		reinstall(img, i)
		if has(img.flags[i], F_FOLLOW) then
			local x, y = follow_point(self, i)
			if img.xoff[i] ~= x or img.yoff[i] ~= y then img.xoff[i], img.yoff[i] = x, y end
		end
	end

	-- SetImageFollowMain 0x40f3a0: compares flag 0x80 with the whole byte argument.
	function W:image_set_follow(i, enable)
		local img = self.img
		local cur = has(img.flags[i], F_FOLLOW) and 1 or 0
		if cur == enable then return end
		img.flags[i] = set(img.flags[i], F_FOLLOW, enable % 2 == 1)
		if enable ~= 0 then
			local x, y = follow_point(self, i)
			if img.xoff[i] ~= x or img.yoff[i] ~= y then img.xoff[i], img.yoff[i] = x, y end
		end
	end

	-- SetImageOffset 0x4117d0 (setpos): low bytes.
	function W:image_set_offset(i, x, y)
		local img = self.img
		x, y = s8(x), s8(y)
		if img.xoff[i] ~= x or img.yoff[i] ~= y then img.xoff[i], img.yoff[i] = x, y end
	end

	-- HideImage 0x40f890 / ShowImage 0x411820.
	function W:image_hide(i)
		local img = self.img
		if not has(img.flags[i], F_HIDDEN) then img.flags[i] = img.flags[i] + F_HIDDEN end
	end

	function W:image_show(i)
		local img = self.img
		if has(img.flags[i], F_HIDDEN) then img.flags[i] = img.flags[i] - F_HIDDEN end
	end

	-- AllocateImage 0x40eba0: the main free head, or NONE.
	function W:image_alloc()
		local i = self.free_images.head
		if i == NONE then return NONE end
		unlink(self.img.prev, self.img.next, self.free_images, i)
		return i
	end

	-- ReleaseImage 0x40fe10: unlink from the sprite, clear the GRP marker, push after the
	-- main free head. The sprite and other fields stay.
	function W:image_release(i)
		local img = self.img
		unlink(img.prev, img.next, self.spr.images[img.sprite[i]], i)
		img.grp[i] = 0
		insert_after_head(img.prev, img.next, self.free_images, i)
	end

	-- BeginCloaking 0x40ed00: method 2, parameter 0 with timer 3 (the low WORD; the high
	-- WORD is kept).
	function W:image_begin_cloak(i)
		local img = self.img
		img.method[i] = M.METHOD_CLOAK
		local p = img.param[i]
		img.param[i] = p - p % 65536 + M.CLOAK_START
	end

	-- BeginDecloaking 0x40ed30: method 4 from a method below 4, otherwise 7; parameter 8 with
	-- timer 3. Its method-17 branch writes 0x230 and then 0x308 over it.
	function W:image_begin_decloak(i)
		local img = self.img
		img.method[i] = img.method[i] < M.METHOD_DECLOAK and M.METHOD_DECLOAK or M.METHOD_DECLOAK_BLEND
		local p = img.param[i]
		img.param[i] = p - p % 65536 + M.DECLOAK_START
	end

	-- CopyCloakEffectFromImage 0x40ef80.
	function W:image_copy_cloak(i, source)
		local img = self.img
		local m = img.method[source]
		if m < 2 or m > 7 then return end
		img.method[i] = m
		local p = img.param[i]
		img.param[i] = p - p % 65536 + img.param[source] % 65536
	end

	-- CreateRelativeOverlayImage 0x4101b0: new image `id` before (overlay) or after
	-- (underlay) reference image `ref`; returns the image or NONE.
	function W:image_create_relative(ref, id, xoff, yoff, before)
		local i = self.free_images.head
		if i == NONE then return NONE end
		local img = self.img
		unlink(img.prev, img.next, self.free_images, i)
		local s = img.sprite[ref]
		local lst = self.spr.images[s]
		if before then insert_before(img.prev, img.next, lst, ref, i)
		else insert_after(img.prev, img.next, lst, ref, i) end
		self:image_init_record(i, id, s, xoff, yoff)
		self:image_init_drawing(i)
		local unit = self.unit
		if img.method[i] == 0 and unit then
			if int.band(self:hook("unit_status")(self, unit), M.STATUS_HALLUCINATION) ~= 0
				and (self.replay ~= 0 or self:hook("unit_owner")(self, unit) == self.local_player) then
				img.method[i] = M.METHOD_HALLUCINATION
				img.param[i] = 0
			end
		end
		if has(img.flags[i], F_TURNS) then
			local d = img.dir[ref]
			if has(img.flags[ref], F_MIRROR) then d = 32 - d end
			self:image_set_direction(i, d)
		end
		local frame = (img.frameset[i] + img.dir[i]) % 65536
		if img.frame[i] ~= frame then img.frame[i] = frame end
		if unit and M.cloaked(self:hook("unit_status")(self, unit)) then
			if self.dat.images.draw_if_cloaked[id] == 0 then
				if not has(img.flags[i], F_HIDDEN) then
					img.flags[i] = img.flags[i] + F_HIDDEN
					return i
				end
			elseif img.method[i] == 0 then
				self:image_copy_cloak(i, self.spr.main[s])
			end
		end
		return i
	end

	-- Update callbacks of the drawing methods (0x503044 table): 2/5 cloaking 0x40e570,
	-- 4/7 decloaking 0x40e5f0, 17 warp-in 0x40e650; the others are no-ops.
	function W:image_update(i)
		local m = self.img.method[i]
		if m == 2 or m == 5 then self:image_update_cloaking(i)
		elseif m == 4 or m == 7 then self:image_update_decloaking(i)
		elseif m == M.METHOD_WARP_FIRE then self:image_update_warp(i) end
	end

	local function param_bytes(img, i)
		local p = img.param[i]
		return p % 256, floor(p / 256) % 256, p - p % 65536
	end

	function W:image_update_cloaking(i)
		local img = self.img
		local value, timer, high = param_bytes(img, i)
		if timer ~= 0 then
			img.param[i] = high + (timer - 1) * 256 + value
			return
		end
		value = (value + 1) % 256
		img.param[i] = high + 3 * 256 + value
		if value >= 8 then
			self:image_set_method(i, img.method[i] + 1)
			self:unit_set_cloak_state(3)
		end
	end

	function W:image_update_decloaking(i)
		local img = self.img
		local value, timer, high = param_bytes(img, i)
		if timer ~= 0 then
			img.param[i] = high + (timer - 1) * 256 + value
			return
		end
		img.param[i] = high + 3 * 256 + (value - 1) % 256
		if value == 0 then
			self:image_set_method(i, 0)
			self:unit_set_cloak_state(0)
			img.param[i] = img.param[i] - img.param[i] % 256 + 0xff
		end
	end

	function W:image_update_warp(i)
		local img = self.img
		local value, timer, high = param_bytes(img, i)
		if timer ~= 0 then
			img.param[i] = high + (timer - 1) * 256 + value
			return
		end
		img.param[i] = high + 2 * 256 + value
		if value < 63 then
			img.param[i] = img.param[i] + 1
			return
		end
		if img.anim[i] ~= M.ANIM_DEATH then
			img.anim[i] = M.ANIM_DEATH
			img.pc[i] = u16(self.script.code, img.header[i] + 8 + 2 * M.ANIM_DEATH)
			img.wait[i], img.ret[i] = 0, 0
			self:run_iscript(i)
		end
		self:flingy_signal(1, 0)
	end
end

return M
