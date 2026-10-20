-- The fog of war of the presentation (kp_core.game.fog_view, native/kp_core/src/core/kp_fog_view.c) on synthetic
-- tile flags. The original's own fog passes are the oracle of tools/fog_check.py (the emulator's captures); these
-- cases keep the rules that need no capture: the levels of seen, explored and unexplored tiles, the blur at an edge
-- between them, the uniform and black blocks of the darkening, the blocks a dirty update reports, the minimap's three map sizes and its rows. Needs
-- the kp_core module; skipped without it.
local native = require("game.native")
local chunks = require("presentation.minimap_chunks")

local S = {}

local SEEN, EXPLORED = 31, 15
local LOCAL = 0 -- the local player's bit

local function core(T)
	local c = native.find("kp_core")
	if not c then T.ok(true, "no kp_core") end
	return c
end

local function u32(v) return string.char(v % 256, math.floor(v / 256) % 256, math.floor(v / 65536) % 256, math.floor(v / 16777216) % 256) end

-- Tile flags of a w x h map from flag(tx, ty): byte 0 the players that do not see the tile, byte 1 those that have
-- not explored it.
local function tiles(w, h, flag)
	local t = {}
	for y = 0, h - 1 do
		for x = 0, w - 1 do t[#t + 1] = u32(flag(x, y)) end
	end
	return table.concat(t)
end

local NOT_SEEN, NOT_EXPLORED = 2 ^ LOCAL, 2 ^ (LOCAL + 8)

local function dark()
	local t = {}
	for level = 0, 31 do
		for i = 0, 255 do t[#t + 1] = string.char((i + level) % 256) end
	end
	return table.concat(t)
end

function S.levels_of_seen_explored_and_unexplored_tiles(T)
	local c = core(T)
	if not c then return end
	-- the left half of a 64 x 64 map seen, the right half explored, the bottom rows unexplored
	local flags = tiles(64, 64, function(x, y)
		if y >= 48 then return NOT_SEEN + NOT_EXPLORED end
		return x < 32 and 0 or NOT_SEEN
	end)
	local view = c.game.fog_view()
	local changed = view:update(flags, 64, 64, NOT_SEEN, NOT_EXPLORED, false, 0, 0, 0, 0, 640, 400)
	T.ok(changed, "a first update changes the map")
	local raw, smooth, cols, rows = view:grids()
	T.eq(cols, 24)
	T.eq(rows, 17)
	T.eq(raw:byte(1 * cols + 2), SEEN, "a seen tile")
	T.eq(raw:byte(1 * cols + 23), SEEN, "column 22 is tile 21: seen")
	local levels, w, h = view:levels()
	T.eq(w, 88)
	T.eq(h, 60)
	T.eq(levels:byte(1), SEEN, "the camera's first block")
	T.eq(view:update(flags, 64, 64, NOT_SEEN, NOT_EXPLORED, false, 0, 0, 0, 0, 640, 400), false, "nothing changed")
	-- a view over the seen and explored halves: the blur steps from 31 to 15 across the edge
	view:update(flags, 64, 64, NOT_SEEN, NOT_EXPLORED, false, 0, 0, 800, 0, 640, 400)
	raw, smooth, cols = view:grids()
	local edge = {}
	for c0 = 1, 22 do edge[#edge + 1] = smooth:byte(5 * cols + c0 + 1) end
	T.eq(edge[1], SEEN)
	T.eq(edge[#edge], EXPLORED)
	for k = 2, #edge do T.ok(edge[k] <= edge[k - 1], "the levels fall across the edge") end
	-- the darkening: a seen block keeps its pixels, an explored one takes dark row 15, an unexplored one is black
	local pixels, d = string.rep(string.char(100), 640 * 400), dark()
	local function shaded(cam_x, cam_y)
		view:update(flags, 64, 64, NOT_SEEN, NOT_EXPLORED, false, 0, 0, cam_x, cam_y, 640, 400)
		return view:shade(cam_x, cam_y, d, pixels, 640, 400):byte(640 * 200 + 320 + 1)
	end
	T.eq(shaded(0, 0), 100, "a seen block")
	T.eq(shaded(1200, 0), 100 + EXPLORED, "an explored block through dark row 15")
	T.eq(shaded(0, 1536 + 64), 0, "an unexplored block is black")
end

-- A change of the tiles under an unmoved camera reports the blocks it changed (UpdateFogMaskDirty 0x46c2d0), which are
-- all the presentation uploads: every level outside them stays as it was, and the whole map equals a fresh view's.
function S.dirty_update_reports_the_changed_blocks(T)
    local c = core(T)
    if not c then return end
    local hidden = { x = 5, y = 5 } -- the tile that becomes unexplored
    local function flags(with_hidden)
        return tiles(64, 64, function(x, y)
            if with_hidden and x == hidden.x and y == hidden.y then return NOT_SEEN + NOT_EXPLORED end
            return 0
        end)
    end
    local view = c.game.fog_view()
    view:update(flags(false), 64, 64, NOT_SEEN, NOT_EXPLORED, false, 0, 0, 0, 0, 640, 400)
    local before = view:levels()
    local changed, x0, y0, x1, y1 = view:update(flags(true), 64, 64, NOT_SEEN, NOT_EXPLORED, false, 0, 0, 0, 0, 640, 400)
    T.ok(changed, "the changed tile changes the map")
    -- the tile is grid cell 6 (the grids start a tile left of the camera's); the blur reaches cells 5..7, and a cell's
    -- change reaches the blocks of the tiles left of and above it: blocks 12..27 on both axes
    T.eq(table.concat({ x0, y0, x1, y1 }, " "), "12 12 28 28", "the changed blocks")
    local after, w, h = view:levels()
    local fresh = c.game.fog_view()
    fresh:update(flags(true), 64, 64, NOT_SEEN, NOT_EXPLORED, false, 0, 0, 0, 0, 640, 400)
    T.eq(after, (fresh:levels()), "the levels equal a fresh view's")
    local outside = 0
    for by = 0, h - 1 do
        for bx = 0, w - 1 do
            if not (bx >= x0 and bx < x1 and by >= y0 and by < y1) then
                local i = by * w + bx + 1
                if before:byte(i) ~= after:byte(i) then outside = outside + 1 end
            end
        end
    end
    T.eq(outside, 0, "no level outside the changed blocks changed")
    T.ok(after ~= before, "levels inside them did")
end

function S.minimap_sizes_and_rows(T)
	local c = core(T)
	if not c then return end
	local d = dark()
	for _, side in ipairs({ 64, 128, 256 }) do
		local flags = tiles(side, side, function(x)
			if x < side / 4 then return 0 end -- seen
			if x < side / 2 then return NOT_SEEN end -- explored
			return NOT_SEEN + NOT_EXPLORED -- unexplored
		end)
		local scale = side == 64 and 2 or side == 128 and 1 or 0.5
		local w = side * scale
		local terrain = string.rep(string.char(40), w * w)
		local out, ow, oh = c.game.fog_view():minimap(flags, side, side, NOT_SEEN, NOT_EXPLORED, false, 0, 0, terrain, d, 7)
		T.eq(ow, w, "the minimap's width for a side of " .. side)
		T.eq(oh, w)
		T.eq(out:byte(1), 40, "a seen tile keeps its colour")
		T.eq(out:byte(w * 3 / 8 + 1), 40 + EXPLORED, "an explored tile through dark row 15")
		T.eq(out:byte(w - 1 + 1), 7, "an unexplored tile takes the unexplored colour")
	end
end

-- The minimap image reaches the GUI script as chunks that fit a message and assemble to the same bytes; a lost chunk
-- drops the image, a newer image replaces an unfinished one.
function S.minimap_chunks(T)
	local rgb = {}
	for i = 1, 3 * 128 * 128 do rgb[i] = string.char(i % 251) end
	local img = { version = 7, w = 128, h = 128, rgb = table.concat(rgb) }
	local parts = chunks.split(img)
	T.eq(#parts, math.ceil(#img.rgb / chunks.CHUNK_BYTES))
	for _, p in ipairs(parts) do T.ok(#p.data <= chunks.CHUNK_BYTES and #p.data % 3 == 0) end
	local state, got = {}, nil
	for _, p in ipairs(parts) do got = chunks.assemble(state, p) end
	T.ok(got ~= nil and got.rgb == img.rgb and got.w == 128 and got.h == 128, "the image assembles")
	state = {}
	T.eq(chunks.assemble(state, parts[1]), nil)
	T.eq(chunks.assemble(state, parts[3]), nil, "a missing chunk drops the image")
	T.eq(chunks.assemble(state, parts[#parts]), nil)
	local newer = chunks.split({ version = 8, w = 1, h = 1, rgb = "abc" })
	T.eq(chunks.assemble(state, parts[1]), nil)
	local replaced = chunks.assemble(state, newer[1])
	T.ok(replaced ~= nil and replaced.rgb == "abc", "a newer image replaces an unfinished one")
end

return S
