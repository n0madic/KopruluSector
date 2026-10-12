-- Draw list of an emulator capture (tools/compose_capture.py): rebuilds the captured image
-- and sprite records as a game.images world and runs presentation/draw_list.lua on the
-- captured view. Usage (repository root): luajit tools/lua/capture_draw_list.lua RECORDS.lua
-- Prints "plan <composition windows>", then one line per drawn image meeting the view:
-- "draw rank method owner param grp-path frame left top flip trans50" (path "virtual" for
-- health bars and boxes; trans50: the owner whose colours trans50 rows 8..15 hold, -1 for
-- unchanged rows).
package.path = "./?.lua;" .. package.path
local world = require("game.images.world")
local draw_list = require("presentation.draw_list")
local dat = require("game.data.dat")
local grpmeta = require("game.data.grpmeta")
local scripts = require("game.data.scripts")

local function read(path)
	local f = assert(io.open(path, "rb"))
	local d = f:read("*a")
	f:close()
	return d
end

local cap = dofile(assert(arg[1], "usage: capture_draw_list.lua RECORDS.lua"))
local w = world.new({
	dat = dat.parse(read("generated/data/dat.kpd"), "dat.kpd"),
	grp = grpmeta.parse(read("generated/data/grpmeta.kpd"), "grpmeta.kpd"),
	iscript = scripts.parse_iscript(read("generated/data/iscript.kpd"), "iscript.kpd"),
	map_width = cap.map[1], map_height = cap.map[2], local_vision = cap.vision,
})

local spr, img = w.spr, w.img
for _, s in ipairs(cap.sprites) do
	local k = s.key
	for _, f in ipairs({ "id", "owner", "selection", "visibility", "elevation", "flags", "sel_timer", "index", "width",
		"height", "x", "y" }) do
		spr[f][k] = s[f]
	end
	spr.prev[k], spr.next[k], spr.main[k] = s.prev, s.next, s.main
	spr.images[k].head, spr.images[k].tail = s.head, s.tail
end
for _, i in ipairs(cap.images) do
	local k = i.key
	for _, f in ipairs({ "id", "method", "dir", "flags", "xoff", "yoff", "header", "pc", "ret", "anim", "wait",
		"frameset", "frame", "x", "y", "param" }) do
		img[f][k] = i[f]
	end
	img.prev[k], img.next[k], img.sprite[k] = i.prev, i.next, i.sprite
	if i.virtual then
		img.grp[k] = -1
		w.vgrp[k] = i.virtual
	else
		img.grp[k] = w:grp_of(i.id)
	end
end
for r = 0, w.map_h - 1 do
	local row = cap.rows[r + 1]
	w.rows[r].head, w.rows[r].tail = row[1], row[2]
end

local view = { left = cap.camera[1], top = cap.camera[2], right = cap.camera[1] + cap.view[1],
	bottom = cap.camera[2] + cap.view[2] }
-- The original refreshes the bounds of the drawn rows before DrawSortedSprites (0x4165d0);
-- the simulation does it for every row at the end of its tick.
w:refresh_bounds()
local state = draw_list.new()
local lines = {}
draw_list.build(state, w, function(_, frame) return frame end, function(d)
	if d.left < view.right and d.top < view.bottom and d.left + d.w > view.left and d.top + d.h > view.top then
		lines[#lines + 1] = ("draw %d %d %d %d %s %d %d %d %d %d"):format(d.rank, d.method, d.owner, d.param,
			d.virtual and "virtual" or w.grp.grps[d.grp].path, d.grp_frame, d.left, d.top, d.flip and 1 or 0,
			d.trans50 or -1)
	end
end, view)
print(("plan %d"):format(state.window_count))
for _, l in ipairs(lines) do print(l) end
