-- Reads portraits.kpd written by converter/kpf/fixtures.py (run it first).
local portraits = require("game.data.portraits")

local DIR = "tests/fixtures/generated/kpf/"

local S = {}

function S.rows_and_variants(T)
	local p = portraits.parse(T.read(DIR .. "portraits.kpd"), "portraits.kpd")
	T.eq(p.rows[0].idle.first, 1)
	T.eq(p.rows[0].idle.mask, 5)
	T.eq(portraits.variant(p, 0, "idle", 0), 1)
	T.eq(portraits.variant(p, 0, "idle", 1), nil) -- variant 1 does not exist
	T.eq(portraits.variant(p, 0, "idle", 2), 2)
	T.eq(portraits.variant(p, 0, "talking", 1), 3)
	T.eq(portraits.variant(p, 1, "idle", 0), nil)
end

function S.movies(T)
	local p = portraits.parse(T.read(DIR .. "portraits.kpd"), "portraits.kpd")
	T.eq(#p.smks, 3)
	T.eq(p.smks[3].path, "portrait\\t1.smk")
	T.eq(p.smks[1].width, 60)
	T.eq(p.smks[1].ms_per_frame, 100)
	T.eq(p.smks[2].palettes[2], 2)
	T.eq(#p.palettes, 2)
	T.eq(p.palettes[2]:byte(768), 1)
end

return S
