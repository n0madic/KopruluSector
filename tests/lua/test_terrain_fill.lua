-- presentation/terrain.lua: the cells a fill clears outside the new map.
local saved_hash = hash
hash = hash or tostring -- the module hashes its message ids at load; the CLI has no hash()
local terrain = require("presentation.terrain")
hash = saved_hash

local S = {}

local function cleared(previous, w, h)
	local set = {}
	local n = 0
	terrain.stale_cells(previous, w, h, function(x, y)
		local key = x .. "," .. y
		if not set[key] then n = n + 1 end
		set[key] = true
	end)
	return set, n
end

function S.fresh_template_clears_its_corners_outside_the_map(T)
	local top = terrain.TEMPLATE
	local set, n = cleared(nil, 64, 96)
	T.eq(n, 2)
	T.ok(set["1,1"] and set[top .. "," .. top])
	set, n = cleared(nil, top, top)
	T.eq(n, 0)
	set, n = cleared(nil, top, 128)
	T.eq(n, 1)
	T.ok(set["1,1"])
end

function S.only_the_previous_map_outside_the_new_one(T)
	local top = terrain.TEMPLATE
	local set, n = cleared({ width = 128, height = 96 }, 64, 64)
	T.eq(n, 128 * 96 - 64 * 64)
	T.ok(set["65," .. top] and set["1," .. (top - 64)] and not set["64," .. top])
	set, n = cleared({ width = 64, height = 64 }, 128, 128)
	T.eq(n, 0)
	set, n = cleared({ width = 96, height = 64 }, 64, 96)
	T.eq(n, 32 * 64)
end

return S
