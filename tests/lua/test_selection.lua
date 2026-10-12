-- game/images/selection.lua on the converted data (skipped without generated/data): pool
-- records, health bars (0x40eb60/0x40f6e0/0x40f540) and selection circles (0x40ec90/0x411460).
local core = require("game.images.core")
local world = require("game.images.world")
local selection = require("game.images.selection")
local image = require("game.images.image")
local dat = require("game.data.dat")
local grpmeta = require("game.data.grpmeta")
local scripts = require("game.data.scripts")
local rng = require("game.simulation.rng")

local DATA = "generated/data/"
local MARINE, DRAGOON, GHOST = 0, 66, 1

local S = {}

local function scene(T)
	if not T.has(DATA .. "iscript.kpd") then return nil end
	local d = dat.parse(T.read(DATA .. "dat.kpd"), "dat.kpd")
	local units, selected = {}, {}
	local g = rng.game(1)
	g:seed(1)
	g:set_enabled(1)
	local w = world.new({ rng = g, dat = d, grp = grpmeta.parse(T.read(DATA .. "grpmeta.kpd"), "grpmeta.kpd"),
		iscript = scripts.parse_iscript(T.read(DATA .. "iscript.kpd"), "iscript.kpd"), map_width = 64, map_height = 64,
		hooks = {
			selection_unit = function(_, s) return selected[s] end,
			unit_type = function(_, u) return units[u].type end,
			unit_status = function(_, u) return units[u].status end,
		} })
	w:init_pools()
	local function unit(t, status)
		local s = w:sprite_create(d.flingy.sprite[d.units.graphics[t]], 512, 512, 0)
		units[#units + 1] = { type = t, status = status or 0 }
		selected[s] = #units
		return s
	end
	return w, unit, d
end

local function ids(w, s)
	local out, i = {}, w.spr.images[s].head
	while i ~= core.NONE do
		out[#out + 1] = w.img.id[i]
		i = w.img.next[i]
	end
	return table.concat(out, ",")
end

function S.pool_records(T)
	local w = scene(T)
	if not w then return end
	local i = core.HEALTH_FIRST + 3
	T.eq(w.img.id[i], selection.HEALTH_IMAGE)
	T.eq(w.img.method[i], selection.METHOD_HEALTH)
	T.eq(w.img.grp[i], image.VIRTUAL)
	T.ok(w.img.header[i] > 0)
	T.eq(w.img.method[core.BOXES_FIRST], selection.METHOD_BOX)
end

function S.health_bars(T)
	local w, unit, d = scene(T)
	if not w then return end
	local s = unit(MARINE)
	local head = w.spr.images[s].head
	local i = w:health_bar_create(s)
	T.eq(i, core.HEALTH_FIRST) -- the pool's head
	T.eq(w.spr.images[s].head, i) -- before the old head: drawn last
	T.eq(w.img.next[i], head)
	local bar = d.sprites.health_bar[w.spr.id[s]]
	local v = w.vgrp[i]
	T.eq(v.fw, math.max(selection.BAR_MIN_WIDTH, bar - (bar - 1) % 3))
	T.eq(v.fh, 5) -- no shields, no energy
	T.eq(w.img.param[i], 1)
	local circle = w:grp_of(selection.CIRCLE_FIRST + d.sprites.selection_circle_image[w.spr.id[s]])
	local _, _, _, ch = grpmeta.frame(w.grp, circle, 0)
	T.eq(w.img.yoff[i], math.floor(ch / 2) + 8 + d.sprites.selection_circle_offset[w.spr.id[s]])
	T.eq(w.vgrp[w:health_bar_create(unit(DRAGOON))].fh, 7) -- shields
	T.eq(w.vgrp[w:health_bar_create(unit(GHOST))].fh, 5 + 6) -- energy
	T.eq(w.vgrp[w:health_bar_create(unit(MARINE, selection.HALLUCINATION))].fh, 5 + 6) -- timer
	w:health_bar_release(i)
	T.eq(w.spr.images[s].head, head)
	-- Allocated 5000, 5011, 5010, 5009; the released record goes right after the free head.
	T.eq(w.free_health.head, core.HEALTH_FIRST + core.HEALTH - 4)
	T.eq(w.img.next[w.free_health.head], i)
end

function S.selection_circles(T)
	local w, unit, d = scene(T)
	if not w then return end
	local s = unit(DRAGOON) -- a shadow tail: the circle goes before it
	local tail = w.spr.images[s].tail
	T.eq(w.img.method[tail], selection.METHOD_SHADOW)
	local c = w:selection_circle_create(s, 2, selection.CIRCLE_FIRST)
	T.eq(c, core.CIRCLES_FIRST)
	T.eq(w.img.next[c], tail)
	T.eq(w.img.id[c], selection.CIRCLE_FIRST + d.sprites.selection_circle_image[w.spr.id[s]])
	T.eq(w.img.method[c], selection.METHOD_SELECTION)
	T.eq(w.img.param[c], 2)
	local before = ids(w, s)
	T.ok(before:find(tostring(w.img.id[c]), 1, true) ~= nil)
end

return S
