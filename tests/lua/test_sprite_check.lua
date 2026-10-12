-- Viewer sprite readback check (viewer/sprite_check.lua) on hand-built probe data.
local check = require("viewer.sprite_check")

local function w(v) return string.char(v % 256, math.floor(v / 256) % 256) end

-- One animation "a" with a 3x2 frame (top row 1 9 0, bottom row 2 3 4) and an empty frame.
local PROBE = "KPSP" .. w(1) .. string.char(1) .. "a" .. w(2)
	.. w(3) .. w(2) .. string.char(1, 9, 0, 2, 3, 4) .. w(0) .. w(0)

local S = {}

local function pair(T, want_checked, want_bad, checked, bad)
	T.eq(checked, want_checked)
	T.eq(bad, want_bad)
end

function S.parse(T)
	local p = check.parse(PROBE)
	T.eq(p.a[0].w, 3)
	T.eq(p.a[0].h, 2)
	T.eq(p.a[0].pixels, string.char(1, 9, 0, 2, 3, 4))
	T.eq(p.a[1].w, 0)
	T.eq(p.a[1].pixels, "")
	T.raises(function() check.parse("XXXX") end)
end

function S.compare_skips_transparent_and_remap(T)
	local f = check.parse(PROBE).a[0]
	-- read back rows bottom-up; any value under the transparent and remap pixels
	pair(T, 4, 0, check.compare(f, false, string.char(2, 3, 4, 1, 77, 77)))
	-- drawn upside down: every compared pixel differs
	pair(T, 4, 4, check.compare(f, false, string.char(1, 9, 0, 2, 3, 4)))
	-- mirrored frame: screen column i shows frame column w - 1 - i
	pair(T, 4, 0, check.compare(f, true, string.char(4, 3, 2, 77, 77, 1)))
end

function S.isolated(T)
	local a = { left = 0, top = 0, w = 10, h = 10 }
	local b = { left = 9, top = 9, w = 5, h = 5 }
	local c = { left = 10, top = 0, w = 5, h = 5 } -- touches a's edge only
	local out = check.isolated({ a, b, c })
	T.eq(#out, 1)
	T.ok(out[1] == c)
	T.eq(#check.isolated({ a, c }), 2)
	-- With ranks only later images count: a shadow drawn before its unit does not.
	local shadow = { left = 2, top = 2, w = 10, h = 10, rank = 1 }
	local unit = { left = 0, top = 0, w = 10, h = 10, rank = 2 }
	out = check.isolated({ shadow, unit })
	T.eq(#out, 1)
	T.ok(out[1] == unit)
end

return S
