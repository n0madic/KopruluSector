-- presentation/draw_list.lua on a hand-built world (no retail data): native draw order,
-- the simulation's bounds refresh, destination layers, composition windows, the command
-- budget, method rows, skipped images and interpolation identity.
local core = require("game.images.core")
local native_only = require("tests.lua.native_only")
local world = native_only.world
local draw_list = require("presentation.draw_list")
local presentation_world = require("presentation.world")
local world_plan = require("presentation.backend.world_plan")

local NONE = core.NONE
local S = {}

local function u32(v) return string.char(v % 256, math.floor(v / 256) % 256, math.floor(v / 65536) % 256, 0) end

-- One GRP entry, 64x64 canvas, three frames: (x, y, w, h).
local FRAMES = { { 10, 20, 8, 6 }, { 30, 4, 8, 6 }, { 0, 0, 0, 0 } }

local function fake_grp()
	local frms = { string.rep("\0", 4) }
	for _, f in ipairs(FRAMES) do frms[#frms + 1] = string.char(f[1], f[2], f[3], f[4]) .. u32(0) end
	return { grps = { { frames = #FRAMES, width = 64, height = 64, atlas = 1, animation = "a", first_frame = 0 } },
		frms = table.concat(frms) }
end

local function new_world()
	local w = world.new({ dat = {}, grp = fake_grp(), iscript = { code = "", headers = {} }, map_width = 16,
		map_height = 16 })
	w:init_pools()
	return w
end

-- A sprite at (x, y) with images given head to tail as { method =, frame =, flags = }.
local function sprite(w, x, y, images, opts)
	opts = opts or {}
	local spr, img = w.spr, w.img
	local s = w.free_sprites.head
	core.unlink(spr.prev, spr.next, w.free_sprites, s)
	spr.x[s], spr.y[s], spr.elevation[s] = x, y, opts.elevation or 4
	spr.visibility[s], spr.owner[s] = opts.visibility or 0xff, opts.owner or 0
	core.insert_after_head(spr.prev, spr.next, w.rows[w:sprite_row(s)], s)
	local keys = {}
	for k, def in ipairs(images) do
		local i = w:image_alloc()
		img.id[i], img.grp[i], img.frame[i], img.method[i] = 100 + k, 1, def.frame or 0, def.method or 0
		img.flags[i], img.sprite[i] = def.flags or 0, s
		local lst = spr.images[s]
		if lst.head == NONE then core.insert_after_head(img.prev, img.next, lst, i)
		else core.insert_after(img.prev, img.next, lst, lst.tail, i) end
		if k == 1 then spr.main[s] = i end
		keys[k] = i
	end
	return s, keys
end

-- The end of a simulation tick (World:refresh_bounds), then the draw list.
local function build(state, w, view)
	w:refresh_bounds()
	local out = {}
	local drawn, layers = draw_list.build(state, w, function(_, frame) return frame end, function(d)
		out[#out + 1] = { key = d.key, rank = d.rank, layer = d.layer, left = d.left, top = d.top, px = d.px,
			x = d.x, family = d.family, kind = d.kind, window = d.window }
	end, view)
	return out, drawn, layers
end

function S.native_order_tail_to_head_and_ranks(T)
	local w = new_world()
	local _, low = sprite(w, 200, 100, { {}, { method = 10 } }) -- y 100: drawn after
	local _, high = sprite(w, 100, 50, { {} })
	local out, drawn = build(draw_list.new(), w)
	T.eq(drawn, 3)
	T.eq(out[1].key, high[1])
	T.eq(out[2].key, low[2]) -- tail (shadow) before the head
	T.eq(out[3].key, low[1])
	for k = 1, 3 do T.eq(out[k].rank, k) end
	T.eq(out[2].family, draw_list.DEST)
end

function S.bounds_follow_the_current_frame(T)
	local w = new_world()
	local _, keys = sprite(w, 100, 100, { {} })
	local state = draw_list.new()
	local out = build(state, w)
	T.eq(out[1].left, 100 + 10 - 32)
	T.eq(out[1].top, 100 + 20 - 32)
	w.img.frame[keys[1]] = 1 -- playfram: a frame with another rectangle
	out = build(state, w)
	T.eq(out[1].left, 100 + 30 - 32)
	T.eq(out[1].top, 100 + 4 - 32)
	T.eq(w.img.x[keys[1]], 100 + 30 - 32) -- the tick's refresh writes the bounds like the original
	-- The draw list only reads them: a replan for any view leaves the records alone.
	w.img.frame[keys[1]] = 0
	draw_list.build(state, w, function(_, f) return f end, function() end,
		{ left = 0, top = 0, right = 640, bottom = 480 }, true)
	T.eq(w.img.x[keys[1]], 100 + 30 - 32)
end

function S.overlapping_destination_images_get_layers(T)
	local w = new_world()
	sprite(w, 100, 100, { { method = 10 } })
	sprite(w, 104, 101, { { method = 10 } }) -- overlaps the first
	sprite(w, 400, 300, { { method = 10 } }) -- apart
	local out, _, layers = build(draw_list.new(), w)
	T.eq(layers, 2)
	T.eq(out[1].layer, 1)
	T.eq(out[2].layer, 2)
	T.eq(out[3].layer, 1)
	-- Cells are reused on the next tick: the same scene gives the same layers.
	local state = draw_list.new()
	build(state, w)
	out, _, layers = build(state, w)
	T.eq(layers, 2)
	T.eq(out[3].layer, 1)
end

function S.distortion_images_close_windows(T)
	local w = new_world()
	sprite(w, 100, 100, { { method = 10 } })
	sprite(w, 100, 102, { { method = 3 }, { method = 10 } }) -- shadow, then the cloaked main
	sprite(w, 100, 104, { { method = 10 }, { method = 8 } })
	local state = draw_list.new()
	local out, drawn, layers = build(state, w)
	T.eq(drawn, 5)
	T.eq(state.window_count, 2)
	T.eq(state.windows[1].rank, 3)
	T.eq(state.windows[1].layers, 2) -- two overlapping shadows before the first distortion
	T.eq(out[3].family, draw_list.DISTORT)
	T.eq(out[3].kind, draw_list.KIND_BRIGHT)
	T.eq(out[3].window, 1)
	T.eq(out[4].family, draw_list.DISTORT) -- the EMP image (tail) closes window 2 at once
	T.eq(out[4].kind, draw_list.KIND_RAW)
	T.eq(state.windows[2].rank, 4)
	T.eq(state.windows[2].layers, 0)
	T.eq(out[5].window, 3)
	T.eq(out[5].layer, 1) -- a fresh overlap grid in the new window
	T.eq(layers, 1)
	-- The next tick without distortions has no windows.
	w.img.method[out[3].key], w.img.method[out[4].key] = 0, 0
	build(state, w)
	T.eq(state.window_count, 0)
end

-- With the view known, a distortion reads the current copy when nothing drawn since that
-- copy meets the pixels it reads and those stay inside the view.
function S.distortions_share_a_copy_when_nothing_between_meets_them(T)
	local view = { left = 0, top = 0, right = 700, bottom = 480 }
	local w = new_world()
	sprite(w, 100, 100, { { method = 2 } })
	sprite(w, 300, 101, { { method = 2 } }) -- apart from the first: shares its copy
	sprite(w, 108, 102, { {} }) -- meets the next one's read
	sprite(w, 104, 103, { { method = 2 } })
	sprite(w, 400, 104, { { method = 8 } }) -- EMP: reads 255 pixels ahead, meets nothing
	local state = draw_list.new()
	local out = build(state, w, view)
	T.eq(state.window_count, 2)
	T.eq(state.windows[1].rank, 1)
	T.eq(state.windows[1].last, 2)
	T.eq(out[2].window, 1)
	T.eq(state.windows[2].rank, 4)
	T.eq(state.windows[2].last, 5)
	T.eq(out[3].window, 2) -- the plain image after copy 1
	-- Reading past the view's right edge (wrap) takes a copy of its own.
	state = draw_list.new()
	out = build(state, w, { left = 0, top = 0, right = 420, bottom = 480 })
	T.eq(state.window_count, 3)
	-- Without a view every distortion takes its own copy.
	state = draw_list.new()
	build(state, w)
	T.eq(state.window_count, 4)
	-- Outside the view a distortion reads whatever copy there is.
	state = draw_list.new()
	build(state, w, { left = 0, top = 0, right = 200, bottom = 480 })
	T.eq(state.window_count, 2)
end

function S.replan_keeps_the_tick_interpolation(T)
	local w = new_world()
	local s = sprite(w, 100, 100, { { method = 2 } })
	local state = draw_list.new()
	build(state, w, { left = 0, top = 0, right = 640, bottom = 480 })
	w.spr.x[s] = 110
	local out = build(state, w, { left = 0, top = 0, right = 640, bottom = 480 })
	T.eq(state.distortions, 1)
	local px = out[1].px
	T.ok(px ~= out[1].x)
	out = build(state, w) -- an ordinary build starts a new tick
	T.eq(out[1].px, out[1].x)
	-- A replan of the same tick keeps the start position.
	state = draw_list.new()
	w.spr.x[s] = 100
	build(state, w)
	w.spr.x[s] = 110
	build(state, w)
	local replanned = {}
	w:refresh_bounds()
	draw_list.build(state, w, function(_, f) return f end, function(d) replanned[#replanned + 1] = d.px end,
		{ left = 0, top = 0, right = 640, bottom = 480 }, true)
	T.eq(replanned[1], px)
end

function S.destination_layers_only_near_the_view(T)
	local w = new_world()
	sprite(w, 100, 100, { { method = 10 } })
	sprite(w, 104, 101, { { method = 10 } })
	sprite(w, 1000, 900, { { method = 10 } }) -- far from the view
	local out, _, layers = build(draw_list.new(), w, { left = 0, top = 0, right = 320, bottom = 240 })
	T.eq(layers, 2)
	T.eq(out[3].layer, 0) -- not drawn, no copy spent
	out = build(draw_list.new(), w)
	T.eq(out[3].layer, 1)
end

-- 0x416bd0: a sprite whose main image draws with method 5..7 sets the trans50 colour rows
-- for the images drawn after it, across ticks; sprites far above the view are not drawn.
function S.trans50_colour_follows_the_drawn_sprites(T)
	local w = new_world()
	local _, a = sprite(w, 100, 100, { { method = 6 } }, { owner = 3 })
	local _, b = sprite(w, 100, 200, { {}, { method = 6 } }, { owner = 5 }) -- main image method 0
	local state = draw_list.new()
	local seen = {}
	w:refresh_bounds()
	draw_list.build(state, w, function(_, f) return f end, function(d) seen[d.key] = d.trans50 end)
	T.eq(seen[a[1]], 3)
	T.eq(seen[b[2]], 3) -- the earlier sprite's colours stay
	w.img.method[a[1]] = 0
	w:refresh_bounds()
	draw_list.build(state, w, function(_, f) return f end, function(d) seen[d.key] = d.trans50 end)
	T.eq(seen[b[2]], 3) -- and across ticks
	T.eq(draw_list.build(draw_list.new(), w, function(_, f) return f end, function() end,
		{ left = 0, top = 400, right = 640, bottom = 800 }), 0) -- rows 3 and 6: above row 12 - 4
end

function S.rows_of_the_effect_methods(T)
	local rows = { owner = function(o) return o + 1 end, remap = function(k) return 100 + k end,
		warp = function(p) return 500 + p end, outline = function() return 77 end }
	T.eq(draw_list.row_of(rows, 6, 0, 0, false), 0) -- trans50 rows unchanged
	T.eq(draw_list.row_of(rows, 6, 0, 0, 4), 5) -- rows 8..15 of owner 4's colours
	T.eq(draw_list.row_of(rows, 9, draw_list.TRANS50, 0, 4), 5)
	T.eq(draw_list.row_of(rows, 9, 3, 0, 4), 103)
	T.eq(draw_list.row_of(rows, 5, 0x1305, 2, false), 3 * draw_list.PARAM_SPAN + 5) -- parameter byte only
	T.eq(draw_list.row_of(rows, 7, 0x1305, 2, 1), 2 * draw_list.COLOR_SPAN + 3 * draw_list.PARAM_SPAN + 5)
	T.eq(draw_list.row_of(rows, 2, 0xff, 0), draw_list.PARAM_SPAN + 0xff)
	T.eq(draw_list.row_of(rows, 3, 7, 0), 0)
	T.eq(draw_list.row_of(rows, 12, 4, 0), 504)
	T.eq(draw_list.row_of(rows, 15, 0, 0), 77)
	T.eq(draw_list.METHODS[4][2], draw_list.KIND_CLOAK)
	T.eq(draw_list.METHODS[7][2], draw_list.KIND_CLOAK_BLEND)
	T.eq(draw_list.METHODS[11][2], draw_list.KIND_BAR)
	T.eq(draw_list.METHODS[18], nil) -- no draw method 18
end

function S.hidden_unsupported_empty_and_invisible_are_skipped(T)
	local w = new_world()
	sprite(w, 100, 100, { { flags = core.F_HIDDEN }, { method = 18 }, { frame = 2 }, {} })
	sprite(w, 300, 100, { {} }, { visibility = 0 })
	local _, drawn = build(draw_list.new(), w)
	T.eq(drawn, 1)
end

function S.interpolation_only_from_the_same_image(T)
	local w = new_world()
	local s, keys = sprite(w, 100, 100, { {} })
	local state = draw_list.new()
	build(state, w)
	w.spr.x[s] = 110
	local out = build(state, w)
	T.eq(out[1].px, 100 + 10 - 32 + 4)
	T.eq(out[1].x, 110 + 10 - 32 + 4)
	w.spr.x[s] = 120
	w.img.id[keys[1]] = 999 -- the record now holds another image
	out = build(state, w)
	T.eq(out[1].px, out[1].x)
	T.eq(presentation_world.world_y(0), presentation_world.TOP)
	-- After a restart (a loaded game) the same image does not move from its position before.
	w.spr.x[s] = 130
	draw_list.restart(state)
	out = build(state, w)
	T.eq(out[1].px, out[1].x, "no motion across a restart")
	w.spr.x[s] = 140
	out = build(state, w)
	T.eq(out[1].px, 130 + 10 - 32 + 4, "motion again on the next tick")
end

-- A distortion far from the view cannot meet it after a camera move inside the margin; one
-- near it can (presentation/images.lua replans for those only).
function S.distortions_near_the_view_are_counted(T)
	local w = new_world()
	sprite(w, 100, 100, { { method = 3 } })
	sprite(w, 3000, 3000, { { method = 3 } }) -- far below and right of the view
	local state = draw_list.new()
	build(state, w, { left = 0, top = 0, right = 320, bottom = 240 })
	T.eq(state.distortions, 2)
	T.eq(state.distortions_near, 1)
end

-- Plans stay inside the render script's command budget: past it, destination images take
-- the window's top layer and distortions read the last copy, both counted as degraded.
function S.plans_fit_the_command_budget(T)
	local w = new_world()
	-- 200 shadows on one spot: 200 overlapping destination layers without the budget.
	for k = 1, 200 do sprite(w, 100, 100 + k % 2, { { method = 10 } }) end
	local state = draw_list.new()
	local _, _, layers = build(state, w)
	T.ok(world_plan.commands(state.windows, state.window_count, layers) <= world_plan.PLAN_BUDGET)
	T.ok(state.degraded > 0)
	T.ok(layers > 1)
	-- 200 distortions each needing its own copy (no view): the windows stop at the budget.
	w = new_world()
	for k = 1, 200 do sprite(w, 10 * k, 100, { { method = 10 }, { method = 3 } }) end
	state = draw_list.new()
	_, _, layers = build(state, w)
	T.ok(world_plan.commands(state.windows, state.window_count, layers) <= world_plan.PLAN_BUDGET)
	T.ok(state.window_count > 1 and state.window_count < 200)
	T.ok(state.degraded > 0)
	-- A small scene is not degraded.
	w = new_world()
	sprite(w, 100, 100, { { method = 10 }, { method = 3 } })
	state = draw_list.new()
	build(state, w)
	T.eq(state.degraded, 0)
end

return native_only.guard(S, "test_draw_list")
