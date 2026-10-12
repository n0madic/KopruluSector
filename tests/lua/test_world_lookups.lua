-- presentation/world_lookups.lua on synthetic palettes data: row layout, brightness table,
-- bank padding and the selection rows.
local palettes = require("game.data.palettes")
local world_lookups = require("presentation.world_lookups")
local draw_list = require("presentation.draw_list")

local S = {}

local function fill(n, f)
	local t = {}
	for k = 0, n - 1 do t[k + 1] = string.char(f(k) % 256) end
	return table.concat(t)
end

local function fake_palettes()
	local remap = {}
	for k, name in ipairs(palettes.REMAP_TABLES) do
		local t = { rows = k, data = fill(k * 256, function(i) return i + k end) }
		remap[k], remap[name] = t, t
	end
	return {
		tilesets = { [0] = { remap = remap, dark = fill(palettes.DARK_SIZE, function(i) return 255 - i end),
			shift = fill(256, function(i) return i + 1 end) } },
		tunit = fill(96, function(i) return 100 + i end),
		tselect = fill(24, function(i) return 200 + i end),
	}
end

function S.brightness_follows_0x445f30(T)
	local rgb = fill(768, function(i) return i % 3 == 0 and 255 or 0 end) -- pure red
	local row = world_lookups.brightness_row(rgb)
	T.eq(row:byte(1), math.floor((77 * 255 + 4096) / 8192))
	local white = world_lookups.brightness_row(string.rep("\255", 768))
	T.eq(white:byte(1), 8) -- (256 * 255 + 4096) >> 13
	T.eq(world_lookups.brightness_row(string.rep("\0", 768)):byte(256), 0)
end

function S.rows_and_banks(T)
	local pal = fake_palettes()
	local l = world_lookups.build(pal, 0, string.rep("\255", 768))
	-- identity, 12 player rows, shift, 3 selection rows, brightness
	T.eq(#l.remap, 1 + world_lookups.PLAYER_ROWS + 1 + 3 + 1)
	T.eq(l.rows.owner(13), 2)
	T.eq(l.remap[l.rows.owner(0) + 1]:byte(9), 100) -- tunit on index 8
	T.eq(l.remap[l.rows.owner(0) + 1]:byte(8), 7)
	T.eq(l.remap[l.rows.shift() + 1]:byte(1), 1)
	local sel = l.rows.selection(2)
	T.eq(l.remap[sel + 1]:byte(2), 200 + 16) -- tselect row 2 on index 1
	T.eq(l.remap[sel + 1]:byte(10), 9)
	T.eq(l.remap[l.bright + 1]:byte(256), 8)
	T.ok(not pcall(l.rows.selection, 3), "only three tselect rows")
	-- Banks of BANK_ROWS rows, decoded rows first, then identity; then the shadow row.
	T.eq(#l.tables, #palettes.REMAP_TABLES * world_lookups.BANK_ROWS + 1)
	local trans50 = (draw_list.TRANS50 - 1) * world_lookups.BANK_ROWS
	T.eq(l.trans50, 2 * trans50 + 1)
	T.eq(l.tables[trans50 + 1]:byte(1), draw_list.TRANS50 % 256)
	T.eq(l.tables[trans50 + draw_list.TRANS50 + 1]:byte(1), 0) -- first padding row: identity
	T.eq(l.rows.shadow, 2 * (#l.tables - 1))
	T.eq(l.tables[#l.tables]:byte(1), (255 - world_lookups.SHADOW_DARK_ROW * 256) % 256)
	T.eq(l.rows.outline(), 0)
	T.ok(not pcall(l.rows.warp, 0), "the warp row comes from presentation/images.lua")
end

return S
