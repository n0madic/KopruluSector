-- presentation/palette.lua: the original's colour cycling, brightness gamma, grayscale and
-- fade rules (docs/research/native-palette-bootstrap.md).
local palette = require("presentation.palette")
local crc32 = require("game.data.crc32")

local S = {}

-- CRC-32 of the gamma tables of brightness 60..140 (256 bytes each): the byte predictions
-- that the original 0x4cf760 matched for every level and input byte in the palette research
-- batch (host pow over the same formula).
local GAMMA_CRC = 37115800

local function ramp()
	local t = {}
	for i = 0, 255 do t[i + 1] = string.char(i, (i * 3) % 256, 255 - i) end
	return table.concat(t)
end

function S.gamma_tables_match_the_original(T)
	local all = {}
	for v = palette.BRIGHTNESS_MIN, palette.BRIGHTNESS_MAX do
		local t = palette.gamma_table(v)
		for c = 0, 255 do all[#all + 1] = string.char(t[c]) end
	end
	T.eq(crc32.of(table.concat(all)), GAMMA_CRC)
	local t60 = palette.gamma_table(60)
	T.eq(t60[128], 97)
	T.eq(palette.gamma_table(140)[1], 9)
end

function S.brightness_option_and_output(T)
	local p = palette.new(ramp(), 1)
	T.eq(p:rgb(), ramp())
	p:set_brightness(141) -- out of range: 100
	T.eq(p.brightness, 100)
	p:set_brightness(60)
	local out = p:rgb()
	T.eq(out:byte(128 * 3 + 1), palette.gamma_table(60)[128])
	p:set_brightness(59)
	T.eq(p:rgb(), ramp())
end

function S.badlands_cycles_rotate_every_eighth_driver_call(T)
	local p = palette.new(ramp(), 0)
	for _ = 1, 7 do T.ok(not p:cycle(), "counters 8 -> 1 change nothing") end
	T.ok(p:cycle())
	local rgb = p:current()
	-- 1..6 rotate right: entry 1 gets old entry 6, entry 2 old entry 1.
	T.eq(rgb:byte(1 * 3 + 1), 6)
	T.eq(rgb:byte(2 * 3 + 1), 1)
	T.eq(rgb:byte(6 * 3 + 1), 5)
	T.eq(rgb:byte(7 * 3 + 1), 13) -- 7..13
	T.eq(rgb:byte(248 * 3 + 1), 254) -- the high range in the second pass
	T.eq(rgb:byte(0 * 3 + 1), 0)
	T.eq(rgb:byte(14 * 3 + 1), 14)
	T.eq(rgb:byte(255 * 3 + 1), 255)
end

function S.mask_disables_records_and_empty_tilesets(T)
	local p = palette.new(ramp(), 5) -- desert: mask 3, two cycles
	for _ = 1, 8 do p:cycle() end
	T.eq(p:current():byte(1 * 3 + 1), 13)
	local q = palette.new(ramp(), 2) -- installation: nothing
	for _ = 1, 16 do T.ok(not q:cycle()) end
	T.eq(q:current(), ramp())
end

function S.advance_uses_the_tick_period_and_pauses(T)
	local p = palette.new(ramp(), 0)
	T.ok(not p:advance(7 * palette.CYCLE_PERIOD_MS))
	T.ok(not p:advance(palette.CYCLE_PERIOD_MS, true), "paused: the driver does not run")
	T.ok(p:advance(palette.CYCLE_PERIOD_MS))
end

function S.grayscale_truncates_each_channel(T)
	T.eq(palette.gray(255, 255, 255), 253)
	local p = palette.new(string.rep("\255\255\255", 256), 1)
	p:grayscale()
	T.eq(p:current():byte(1), 253)
	T.eq(p:current():byte(3), 253)
end

function S.fade_shows_the_target_after_its_wait(T)
	local p = palette.new(ramp(), 1)
	local black = string.rep("\0", 768)
	p:fade_to(black, 4)
	T.eq(p:current(), ramp())
	p:advance(palette.FADE_WAIT_MS - 1)
	T.eq(p:current(), ramp())
	T.ok(p:advance(1))
	T.eq(p:current(), black)
	p:fade_to(ramp(), 0)
	T.eq(p:current(), ramp())
end

return S
