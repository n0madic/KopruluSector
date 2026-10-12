-- Reads sounds.kpd written by converter/kpf/fixtures.py (run it first).
local sounds = require("game.data.sounds")

local DIR = "tests/fixtures/generated/kpf/"

local S = {}

function S.records_and_durations(T)
	local s = sounds.parse(T.read(DIR .. "sounds.kpd"), "sounds.kpd")
	T.eq(s.count, 3)
	T.eq(s.present[0], false)
	T.eq(sounds.duration_ms(s, 0), 0)
	T.eq(s.sample_rate[1], 22050)
	T.eq(sounds.duration_ms(s, 1), 1000)
	T.eq(sounds.whole_seconds_ms(s, 1), 1000)
	-- 5,000,000 * 1000 wraps u32 before the division, as the native imul/div.
	T.eq(sounds.duration_ms(s, 2), math.floor(((5000000 * 1000) % 4294967296) / 44100))
end

return S
