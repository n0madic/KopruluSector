-- game/images without retail data: native list helpers, new-game pool order, depth key and
-- sprite rows. The IScript VM and image/sprite creation are checked against the original
-- code by test_iscript_golden.
local core = require("game.images.core")
local native_only = require("tests.lua.native_only")
local world = native_only.world

local NONE = core.NONE

local S = {}

local function links(n)
	local prev, next = {}, {}
	for i = 0, n - 1 do prev[i], next[i] = NONE, NONE end
	return prev, next
end

local function walk(next, lst)
	local out, i = {}, lst.head
	while i ~= NONE do
		out[#out + 1] = i
		i = next[i]
	end
	return table.concat(out, ",")
end

function S.list_helpers_follow_the_native_ones(T)
	local prev, next = links(8)
	local l = core.list()
	core.insert_after_head(prev, next, l, 0)
	T.eq(l.head, 0); T.eq(l.tail, 0)
	core.insert_after_head(prev, next, l, 1) -- after the head
	core.insert_after_head(prev, next, l, 2)
	T.eq(walk(next, l), "0,2,1")
	T.eq(l.tail, 1)
	core.insert_before(prev, next, l, 0, 3) -- new head
	T.eq(walk(next, l), "3,0,2,1")
	T.eq(l.head, 3)
	core.insert_after(prev, next, l, 1, 4) -- new tail
	T.eq(l.tail, 4)
	core.unlink(prev, next, l, 3)
	T.eq(walk(next, l), "0,2,1,4")
	T.eq(prev[3], NONE); T.eq(next[3], NONE)
	core.unlink(prev, next, l, 4)
	T.eq(l.tail, 1)
	-- Unlinking a node that is not in the list only rewires its neighbours (native
	-- compares the head and tail with the node).
	core.unlink(prev, next, core.list(), 2)
	T.eq(walk(next, l), "0,1")
end

local function new_world()
	return world.new({ dat = {}, grp = {}, iscript = { code = "", headers = {} }, map_width = 64, map_height = 48 })
end

function S.new_game_pools_hand_out_record_0_then_descending(T)
	local w = new_world()
	w:init_pools()
	T.eq(w:image_alloc(), 0)
	T.eq(w:image_alloc(), 4999)
	T.eq(w:image_alloc(), 4998)
	T.eq(w.free_sprites.head, 0)
	T.eq(w.spr.next[0], 2499)
	T.eq(w.free_sprites.tail, 1)
	T.eq(w.spr.index[0], 0)
	T.eq(w.spr.index[2499], 2499)
	T.eq(w.free_health.head, core.HEALTH_FIRST)
	T.eq(w.img.next[core.HEALTH_FIRST], core.HEALTH_FIRST + core.HEALTH - 1)
	T.eq(w.free_boxes.tail, core.BOXES_FIRST + 1)
	-- A second initialisation zeroes and rebuilds everything.
	w.img.id[17] = 99
	w:init_pools()
	T.eq(w.img.id[17], 0)
	T.eq(w:image_alloc(), 0)
end

function S.depth_key(T)
	local w = new_world()
	w:init_pools()
	local s = 5
	w.spr.index[s] = 300
	w.spr.elevation[s] = 4
	w.spr.flags[s] = 0x10 + 0x20
	w.spr.y[s] = 100
	-- ((((4 << 13) | 100) << 5) | 0x10) << 9 | 300
	T.eq(w:sprite_depth_key(s), ((4 * 8192 + 100) * 32 + 16) * 512 + 300)
	w.spr.elevation[s] = 5 -- y ignored above elevation 4
	T.eq(w:sprite_depth_key(s), ((5 * 8192) * 32 + 16) * 512 + 300)
	w.spr.elevation[s], w.spr.flags[s], w.spr.y[s] = 0, 0, 65535 -- y = -1 sign-extends
	T.eq(w:sprite_depth_key(s), (4294967295 * 32 * 512 + 300) % 4294967296)
end

-- 0x40ed00/0x40ed30 write the low parameter WORD (value, timer 3) and keep the high WORD;
-- the cloak callbacks then step the value every fourth update.
function S.cloak_initialisers_and_callbacks(T)
	local w = new_world()
	w:init_pools()
	local i = w:image_alloc()
	w.img.method[i], w.img.param[i] = 0, 0x12340000 + 0xffff
	w:image_begin_cloak(i)
	T.eq(w.img.method[i], 2)
	T.eq(w.img.param[i], 0x12340000 + 0x300)
	local cloak_states = {}
	w.unit = 1
	w.hooks.set_cloak_state = function(_, _, n) cloak_states[#cloak_states + 1] = n end
	for _ = 1, 4 * 8 - 1 do w:image_update(i) end
	T.eq(w.img.method[i], 2)
	T.eq(w.img.param[i] % 256, 7)
	w:image_update(i)
	T.eq(w.img.method[i], 3) -- value 8: method 2 -> 3, unit cloak state 3
	T.eq(cloak_states[1], 3)
	w:image_begin_decloak(i)
	T.eq(w.img.method[i], 4) -- below 4 -> 4
	T.eq(w.img.param[i] % 65536, 0x308)
	for _ = 1, 4 * 8 do w:image_update(i) end
	T.eq(w.img.method[i], 4)
	T.eq(w.img.param[i] % 256, 0)
	for _ = 1, 4 do w:image_update(i) end
	T.eq(w.img.method[i], 0) -- switches when the old value was zero, leaving 0xff
	T.eq(w.img.param[i] % 256, 0xff)
	T.eq(cloak_states[2], 0)
	w.img.method[i] = 6
	w:image_begin_decloak(i)
	T.eq(w.img.method[i], 7) -- 4 and above -> 7
end

function S.sprite_rows_truncate_and_clamp(T)
	local w = new_world()
	local s = 1
	for _, c in ipairs({ { 0, 0 }, { 31, 0 }, { 32, 1 }, { 65535, 0 }, { 65536 - 33, 0 }, { 47 * 32, 47 }, { 60 * 32, 47 } }) do
		w.spr.y[s] = c[1]
		T.eq(w:sprite_row(s), c[2], "row of y " .. c[1])
	end
end

return native_only.guard(S, "test_images")
