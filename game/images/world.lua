-- Image, sprite and lone-thingy state of the simulation (plan §7 phase 2 items 1 and 4, D7, D12).
--
-- Records live in SoA arrays indexed by native record numbers. Every image pool shares one
-- key space so that a sprite's image list can hold images of any pool, as in the original:
--   main 0..4999, health bars 5000..5011, selection circles 5012..5091,
--   placement ghosts 5092..5155, placement boxes 5156..5219.
-- Sprites are records 0..2499, lone thingies 0..499. NONE (-1) is the null link.
--
-- Links follow the native list helpers exactly (docs/research/atlas/p05.md, "List helpers";
-- decompiled 0x4119a0/0x411930/0x411900/0x411970): unlink compares the list's head and tail
-- with the node and clears the node's links; insertion into an empty list does not touch
-- the node's links. Callers depend on these details (for example 0x417220 reads the next
-- link of an image after its animation may have released it).
--
-- The screen rectangle fields of the native image record (+0x20..+0x2a) depend on the
-- camera and belong to presentation; the native dirty bit (flag 0x01) only drives the
-- original's redraw and is not kept.
local core = require("game.images.core")
local int = require("game.simulation.int")

local M = setmetatable({}, { __index = core })

local NONE = core.NONE
local unlink, insert_after, list = core.unlink, core.insert_after, core.list

local World = {}
World.__index = World
M.World = World

local function fill(t, first, last, v)
	for i = first, last do t[i] = v end
	return t
end

-- Fields of a fresh image record: everything zero, no links, no sprite.
local IMAGE_FIELDS = { "id", "method", "dir", "flags", "xoff", "yoff", "header", "pc", "ret", "anim", "wait",
	"frameset", "frame", "x", "y", "grp", "param" }
local SPRITE_FIELDS = { "id", "owner", "selection", "visibility", "elevation", "flags", "sel_timer", "index",
	"width", "height", "x", "y" }

-- opts: dat (game.data.dat tables), grp (game.data.grpmeta), iscript (game.data.scripts
-- parse_iscript), rng (game.simulation.rng game generator), map_width/map_height (tiles),
-- tileset (0..7), hooks (see game/images/iscript.lua), sound (function(id, x, y)),
-- expansion (byte 0x536720), local_player (0x508d50), replay (0x67cc90), replay_reveal (the
-- replay's reveal-map vision option, read with `replay` by 0x417c10), local_vision (the
-- sprite vision mask 0x672668).
function M.new(opts)
	local w = setmetatable({
		dat = opts.dat, grp = opts.grp, script = opts.iscript, rng = opts.rng, hooks = opts.hooks or {},
		sound = opts.sound, tileset = opts.tileset or 0, expansion = opts.expansion or 0,
		local_player = opts.local_player or 0, replay = opts.replay or 0, replay_reveal = opts.replay_reveal or false,
		local_vision = opts.local_vision or 0xff,
		unit = nil, flingy = nil, bullet = nil, -- IScript contexts 0x543300/0x543304/0x543308
	}, World)
	w:set_map(opts.map_width or 256, opts.map_height or 256, opts.tiles)
	local img = { sprite = {}, prev = {}, next = {} }
	for _, f in ipairs(IMAGE_FIELDS) do img[f] = {} end
	w.img = img
	-- Virtual GRP headers of health bars and placement boxes: canvas and frame sizes.
	w.vgrp = {}
	local spr = { main = {}, prev = {}, next = {}, images = {} }
	for _, f in ipairs(SPRITE_FIELDS) do spr[f] = {} end
	for i = 0, M.SPRITES - 1 do spr.images[i] = list() end
	w.spr = spr
	w.thg = { sprite = {}, kind = {}, prev = {}, next = {} }
	w.fog = { sprite = {}, kind = {}, prev = {}, next = {} } -- fog-of-war ghosts (pool B); kind = unit type
	w.free_images, w.free_health, w.free_circles = list(), list(), list()
	w.free_ghosts, w.free_boxes = list(), list()
	w.free_sprites, w.rows = list(), {}
	for r = 0, M.ROWS - 1 do w.rows[r] = list() end
	w.free_lone, w.active_lone = list(), list()
	w.free_fog, w.active_fog = list(), list()
	w:clear_records()
	return w
end

local function clear_list(lst) lst.head, lst.tail = NONE, NONE end

-- Every record zeroed, every list empty (the memsets of the pool initializers).
function World:clear_records()
	local img, spr, thg = self.img, self.spr, self.thg
	for _, f in ipairs(IMAGE_FIELDS) do fill(img[f], 0, M.IMAGES - 1, 0) end
	fill(img.sprite, 0, M.IMAGES - 1, NONE)
	fill(img.prev, 0, M.IMAGES - 1, NONE)
	fill(img.next, 0, M.IMAGES - 1, NONE)
	for _, f in ipairs(SPRITE_FIELDS) do fill(spr[f], 0, M.SPRITES - 1, 0) end
	fill(spr.main, 0, M.SPRITES - 1, NONE)
	fill(spr.prev, 0, M.SPRITES - 1, NONE)
	fill(spr.next, 0, M.SPRITES - 1, NONE)
	for i = 0, M.SPRITES - 1 do clear_list(spr.images[i]) end
	fill(thg.sprite, 0, M.LONE - 1, NONE)
	fill(thg.kind, 0, M.LONE - 1, 0)
	fill(thg.prev, 0, M.LONE - 1, NONE)
	fill(thg.next, 0, M.LONE - 1, NONE)
	local fog = self.fog
	fill(fog.sprite, 0, M.FOG - 1, NONE)
	fill(fog.kind, 0, M.FOG - 1, 0)
	fill(fog.prev, 0, M.FOG - 1, NONE)
	fill(fog.next, 0, M.FOG - 1, NONE)
	for _, lst in ipairs({ self.free_images, self.free_health, self.free_circles, self.free_ghosts, self.free_boxes,
		self.free_sprites, self.free_lone, self.active_lone, self.free_fog, self.active_fog }) do clear_list(lst) end
	for r = 0, M.ROWS - 1 do clear_list(self.rows[r]) end
	for k in pairs(self.vgrp) do self.vgrp[k] = nil end
	self.cursor = NONE
end

-- Map size in tiles (0x5264b4/0x5264b6) and pixels (0x543230/0x543234); tile flags
-- 0x666454 (one DWORD per tile, index ty * width + tx) keep byte 0 (players not seeing the
-- tile) and byte 1 (players that have not explored it); 0 = seen and explored by everyone.
-- `tiles` (optional) shares the simulation's array (game/vision/fog.lua); entries absent
-- from it read 0.
function World:set_map(width, height, tiles)
	self.map_w, self.map_h = width, height
	self.map_w_px, self.map_h_px = width * M.TILE, height * M.TILE
	self.tile_flags = tiles or {}
end

-- Pools of a new game: InitImagePools 0x40f8b0 (with the health-bar and box records'
-- setup, game/images/selection.lua; worlds without DAT tables, as in unit tests, leave them
-- zero) and InitSpritePool 0x416410, after zeroing every record. Record 0 heads each free
-- list and the others follow in descending order (allocation order 0, n-1, ..., 1).
function World:init_pools()
	self:clear_records()
	local img = self.img
	local function chain(lst, first, count)
		lst.head, lst.tail = first, first
		for i = first + 1, first + count - 1 do insert_after(img.prev, img.next, lst, first, i) end
	end
	chain(self.free_images, 0, M.MAIN)
	chain(self.free_health, M.HEALTH_FIRST, M.HEALTH)
	chain(self.free_circles, M.CIRCLES_FIRST, M.CIRCLES)
	chain(self.free_ghosts, M.GHOSTS_FIRST, M.GHOSTS)
	chain(self.free_boxes, M.BOXES_FIRST, M.BOXES)
	if self.dat.images then self:init_selection_records() end
	local spr = self.spr
	self.free_sprites.head, self.free_sprites.tail = 0, 0
	for i = 1, M.SPRITES - 1 do
		spr.index[i] = i
		insert_after(spr.prev, spr.next, self.free_sprites, 0, i)
	end
end

-- Free list of the pool that owns image record i (records of one key space, see above).
function World:free_list_of(i)
	if i < M.HEALTH_FIRST then return self.free_images end
	if i < M.CIRCLES_FIRST then return self.free_health end
	if i < M.GHOSTS_FIRST then return self.free_circles end
	if i < M.BOXES_FIRST then return self.free_ghosts end
	return self.free_boxes
end

-- Tile flag readers. Like the original's unchecked reads, an x outside 0..width-1 reads a
-- tile of a neighbouring row; only indices outside the whole map read 0.
function World:hidden_at(tx, ty)
	return (self.tile_flags[ty * self.map_w + tx] or 0) % 256
end

function World:unexplored_at(tx, ty)
	return math.floor((self.tile_flags[ty * self.map_w + tx] or 0) / 256) % 256
end

-- Patches a DAT value of the world's tables (shared with every world over them), as the
-- original-code probes patch the loaded DAT in memory (tests/lua/iscript_trace.lua).
function World:set_dat(table_name, column, index, value)
	self.dat[table_name][column][index] = value
end

-- Clamped tile row of a sprite: trunc(signed y / 32) in [0, map height - 1].
function World:sprite_row(s)
	local r = int.idiv(int.s16(self.spr.y[s]), M.TILE)
	if r < 0 then return 0 end
	if r >= self.map_h then return self.map_h - 1 end
	return r
end

-- Extension modules add their methods to World.
require("game.images.image").install(World)
require("game.images.sprite").install(World)
require("game.images.iscript").install(World)
require("game.images.selection").install(World)

return M
