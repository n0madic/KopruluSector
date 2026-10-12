-- viewer/browser.lua and the viewer worlds of viewer/map_world.lua on the converted data
-- (skipped without generated/data): image and animation stepping, a shown sprite ticking,
-- faults kept, a dropped sprite's records back in their pools.
local browser = require("viewer.browser")
local map_world = require("viewer.map_world")
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

local function contains(w, list, i)
	local k = list.head
	while k ~= core.NONE do
		if k == i then return true end
		k = w.img.next[k]
	end
	return false
end

-- A unit whose script raised is dropped with its health bar: the bar record goes back to
-- the health-bar pool, its main image to the main pool.
function S.dropped_sprite_returns_records_to_their_pools(T)
	local ctx = context(T)
	if not ctx then return end
	local units, selected = {}, {}
	local w = map_world.new_world(ctx, units, selected, { map_width = 64, map_height = 64 })
	local MARINE = 0
	units[1] = { type = MARINE, heading = 0, sprite = w.free_sprites.head }
	w.unit, w.flingy = 1, 1
	local s = w:sprite_create(ctx.dat.flingy.sprite[ctx.dat.units.graphics[MARINE]], 512, 512, 0)
	w.unit, w.flingy = nil, nil
	selected[s] = 1
	local main = w.spr.main[s]
	local bar = w:health_bar_create(s)
	T.ok(bar >= core.HEALTH_FIRST and bar < core.CIRCLES_FIRST)
	map_world.drop_sprite(w, s)
	T.ok(contains(w, w.free_health, bar))
	T.ok(not contains(w, w.free_images, bar))
	T.ok(contains(w, w.free_images, main))
	T.eq(w.img.grp[main], 0)
	T.ok(w.vgrp[bar] ~= nil) -- the virtual header stays with the record
end

return S
