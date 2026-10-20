-- The fog of war of one view on Koprulu's side (kp_core.game.fog_view), for tools/fog_check.py: reads a request from
-- stdin and writes the levels and the darkened view to stdout.
-- Request: a line "cam_x cam_y view_w view_h map_w map_h not_visible not_explored replay replay_vision replay_reveal",
-- then the tile flags (map_w * map_h u32), the dark.pcx rows (32 * 256 bytes) and the view's pixels before the fog
-- (view_w * view_h bytes). Reply: a line "cols rows level_w level_h", then the raw and blurred grids (cols * rows
-- bytes each), the level map (level_w * level_h) and the darkened pixels.
-- A minimap request: a line "minimap map_w map_h not_visible not_explored replay replay_vision replay_reveal
-- unexplored_color terrain_size", then the tile flags, the minimap's terrain pixels and the dark rows; the reply is the
-- minimap's pixels with its fog.
package.path = "./?.lua;" .. package.path
bit = bit or require("bit")

local native = require("game.native")

local core = assert(native.find("kp_core"), "fog_levels: no kp_core module (tools/native/build.sh)")
local input = io.stdin:read("*a")
local nl = input:find("\n", 1, true)
local f = {}
for n in input:sub(1, nl - 1):gmatch("%d+") do f[#f + 1] = tonumber(n) end
if input:sub(1, 7) == "minimap" then
	local map_w, map_h, size = f[1], f[2], f[9]
	local at = nl + 1
	local tiles = input:sub(at, at + 4 * map_w * map_h - 1)
	at = at + 4 * map_w * map_h
	local terrain = input:sub(at, at + size - 1)
	local dark = input:sub(at + size, at + size + 32 * 256 - 1)
	io.write((core.game.fog_view():minimap(tiles, map_w, map_h, f[3], f[4], f[5] ~= 0, f[6], f[7], terrain, dark, f[8])))
	return
end
local cam_x, cam_y, w, h, map_w, map_h = f[1], f[2], f[3], f[4], f[5], f[6]
local at = nl + 1
local tiles = input:sub(at, at + 4 * map_w * map_h - 1)
at = at + 4 * map_w * map_h
local dark = input:sub(at, at + 32 * 256 - 1)
at = at + 32 * 256
local pixels = input:sub(at, at + w * h - 1)
local view = core.game.fog_view()
view:update(tiles, map_w, map_h, f[7], f[8], f[9] ~= 0, f[10], f[11], cam_x, cam_y, w, h)
local raw, smooth, cols, rows = view:grids()
local levels, level_w, level_h = view:levels()
io.write(("%d %d %d %d\n"):format(cols, rows, level_w, level_h), raw, smooth, levels,
	view:shade(cam_x, cam_y, dark, pixels, w, h))
