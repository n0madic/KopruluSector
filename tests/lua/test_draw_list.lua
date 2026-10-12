-- presentation/draw_list.lua on a hand-built world (no retail data): native draw order,
-- bounds refresh, destination layers, skipped images and interpolation identity.
local core = require("game.images.core")
local world = require("game.images.world")
local draw_list = require("presentation.draw_list")
local presentation_world = require("presentation.world")

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

local function build(state, w)
	local out = {}
	local drawn, layers = draw_list.build(state, w, function(_, frame) return frame end, function(d)
		out[#out + 1] = { key = d.key, rank = d.rank, layer = d.layer, left = d.left, top = d.top, px = d.px,
			x = d.x, family = d.family }
	end)
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
	T.eq(w.img.x[keys[1]], 100 + 30 - 32) -- the refresh writes the bounds like the original
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

function S.hidden_unsupported_empty_and_invisible_are_skipped(T)
	local w = new_world()
	sprite(w, 100, 100, { { flags = core.F_HIDDEN }, { method = 2 }, { frame = 2 }, {} })
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
end

return S
