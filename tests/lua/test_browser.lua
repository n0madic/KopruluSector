-- viewer/browser.lua on the converted data (skipped without generated/data): image and
-- animation stepping, a shown sprite ticking, faults kept.
local browser = require("viewer.browser")
local dat = require("game.data.dat")
local grpmeta = require("game.data.grpmeta")
local scripts = require("game.data.scripts")
local core = require("game.images.core")

local DATA = "generated/data/"

local S = {}

local function context(T)
	if not T.has(DATA .. "iscript.kpd") then return nil end
	return {
		dat = dat.parse(T.read(DATA .. "dat.kpd"), "dat.kpd"),
		grp = grpmeta.parse(T.read(DATA .. "grpmeta.kpd"), "grpmeta.kpd"),
		iscript = scripts.parse_iscript(T.read(DATA .. "iscript.kpd"), "iscript.kpd"),
	}
end

function S.steps_through_images_and_animations(T)
	local ctx = context(T)
	if not ctx then return end
	local b = browser.new(ctx, 64, 64)
	T.ok(b:showable(0)) -- the first image (zerg_avenger) has a GRP and an Init animation
	b:next_image(1)
	T.ok(b.image > 0 and b:showable(b.image))
	b:next_image(-1)
	b:next_image(-1)
	T.ok(b:showable(b.image))
	b.image = 76 -- a zerg unit image with a full header
	b:next_anim(1)
	T.ok(b.anim > 0 and b:has_anim(76, b.anim))
	b:next_direction(-1)
	T.eq(b.direction, browser.DIRECTIONS - 1)
end

function S.shows_and_ticks_an_image(T)
	local ctx = context(T)
	if not ctx then return end
	local b = browser.new(ctx, 64, 64)
	local host_image = ctx.dat.sprites.image[browser.HOST_SPRITE]
	b.image = 1
	local ok, fault = b:show(512, 512)
	T.ok(ok, fault)
	T.ok(b.sprite ~= core.NONE)
	T.eq(b.world.img.id[b.world.spr.main[b.sprite]], 1)
	for _ = 1, 24 do b:step() end
	T.eq(b.fault, nil)
	T.eq(ctx.dat.sprites.image[browser.HOST_SPRITE], host_image) -- restored after the creation
end

return S
