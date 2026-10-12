-- Reads fonts.kpd written by converter/kpf/fixtures.py (run it first).
local fonts = require("game.data.fonts")

local DIR = "tests/fixtures/generated/kpf/"

local S = {}

function S.metrics(T)
	local f = fonts.parse(T.read(DIR .. "fonts.kpd"), "fonts.kpd")
	local font = f.font16x
	T.eq(font.low, 32)
	T.eq(font.high, 34)
	T.eq(font.max_width, 3)
	T.eq(font.max_height, 2)
	T.eq(font:glyph(32), nil) -- space has no glyph
	local w, h, x, y = font:glyph(34)
	T.eq(w, 3)
	T.eq(h, 2)
	T.eq(x, 1)
	T.eq(y, 0)
	T.eq(font:glyph(35), nil)
	T.eq(#f.tfontgam, 192)
	T.eq(#f.tfont, 40)
	T.ok(f.font8 and f.font10 and f.font14 and f.font16)
end

return S
