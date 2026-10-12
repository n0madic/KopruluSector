-- Reads palettes.kpd written by converter/kpf/fixtures.py: pixel (row, column) of each
-- synthetic PCX is (row * 7 + column + salt) % 256, salt = len(tileset) + len(name).
local palettes = require("game.data.palettes")

local DIR = "tests/fixtures/generated/kpf/"

local S = {}

function S.tables(T)
	local p = palettes.parse(T.read(DIR .. "palettes.kpd"), "palettes.kpd")
	local badlands = p.tilesets[0]
	local ofire = badlands.remap.ofire
	T.eq(ofire.rows, 64)
	T.eq(#ofire.data, 64 * 256)
	T.eq(ofire.data:byte(1), 0) -- row 0 is never initialized natively; stored as zero
	local salt = #"badlands" + #"ofire"
	T.eq(ofire.data:byte(256 + 1), salt % 256) -- PCX row 0 column 0 -> table row 1
	T.eq(ofire.data:byte(2 * 256 + 3 + 1), (7 + 3 + salt) % 256)
	T.eq(badlands.remap[5], badlands.remap.trans50)
	T.eq(badlands.remap.trans50.rows, 256)
	T.eq(#badlands.dark, 0x2000)
	T.eq(badlands.dark:byte(256 + 1), (7 + #"badlands" + #"dark") % 256)
	T.eq(#badlands.shift, 256)
	T.eq(p.tilesets[7].shift:byte(1), (#"twilight" + #"shift") % 256)
end

function S.game_tables(T)
	local p = palettes.parse(T.read(DIR .. "palettes.kpd"), "palettes.kpd")
	T.eq(#p.tunit, 0x60)
	T.eq(#p.tminimap, 12)
	T.eq(#p.tselect, 24)
	T.eq(#p.thpbar, 19)
	local salt = #"game\\tunit.pcx"
	T.eq(palettes.player_color(p, 2, 3), (16 + 3 + salt) % 256)
end

return S
