-- Lookup rows of the world image materials (ADR 0006), pure Lua: the remap texture (256 x
-- rows, R of (i, row) = i mapped) and the destination table texture, built from
-- palettes.kpd for one tileset, plus the `rows` functions presentation/images.lua uses.
--
-- Remap rows: identity; one per player colour (tunit, indices 8..15); shift; one per
-- selection colour (tselect, indices 1..8); the brightness of every palette index
-- (0x445f30: (77 R + 151 G + 28 B + 4096) >> 13 of the tileset palette, 0..8), read by the
-- cloak threshold passes.
-- Table rows: every blend bank (palettes.REMAP_TABLES) spans BANK_ROWS rows: the native
-- allocation's leading row, the decoded PCX rows, then identity rows (the original reads
-- unrelated heap memory past the decoded rows; identity keeps the destination); then the
-- shadow row (dark.pcx row 18).
local palettes = require("game.data.palettes")
local draw_list = require("presentation.draw_list")

local M = {}

M.PLAYER_ROWS = 12
M.PLAYER_FIRST, M.PLAYER_COLORS = 8, 8
M.SELECTION_FIRST, M.SELECTION_COLORS = 1, 8
M.BANK_ROWS = 256 -- one per source index or fire parameter byte
M.SHADOW_DARK_ROW = 18 -- dark.pcx row of the shadow kernel (docs/research/native-pixel-blitters.md)
M.BRIGHT_WEIGHTS = { 77, 151, 28 }
M.BRIGHT_ROUND, M.BRIGHT_SHIFT = 4096, 8192

function M.identity_row()
	local t = {}
	for i = 0, 255 do t[i + 1] = string.char(i) end
	return table.concat(t)
end

-- Identity row with indices first..first+#values-1 replaced (player colour, selection).
function M.override_row(first, values)
	local row = M.identity_row()
	return row:sub(1, first) .. values .. row:sub(first + #values + 1)
end

-- Brightness table 0x666460 of a 768-byte RGB palette.
function M.brightness_row(rgb)
	local t = {}
	local wr, wg, wb = M.BRIGHT_WEIGHTS[1], M.BRIGHT_WEIGHTS[2], M.BRIGHT_WEIGHTS[3]
	for i = 0, 255 do
		local r, g, b = rgb:byte(i * 3 + 1, i * 3 + 3)
		t[i + 1] = string.char(math.floor((wr * r + wg * g + wb * b + M.BRIGHT_ROUND) / M.BRIGHT_SHIFT))
	end
	return table.concat(t)
end

-- pal: game.data.palettes; tileset: 0..7; rgb: the tileset palette (768 bytes);
-- opts.warp(param) -> global frame of the warp texture (method 12), opts.outline_color.
-- Returns { remap = rows, tables = rows, rows = { owner, color, selection, shift, remap,
-- shadow, fire, warp, outline }, trans50 = row code of the cloak blends, bright = remap row
-- of the brightness table }.
function M.build(pal, tileset, rgb, opts)
	opts = opts or {}
	local ts = pal.tilesets[tileset]
	local identity = M.identity_row()
	local remap = { identity }
	for k = 1, #pal.tunit do
		-- 0x416bd0 copies trans50 rows in place; the draw list's colour-mapped read equals it
		-- only while no player colour is itself one of the rows 8..15 (none in stock data).
		local c = pal.tunit:byte(k)
		assert(c < M.PLAYER_FIRST or c >= M.PLAYER_FIRST + M.PLAYER_COLORS,
			"world_lookups: a tunit colour inside the player rows")
	end
	for p = 0, M.PLAYER_ROWS - 1 do
		local first = p * M.PLAYER_COLORS + 1
		remap[#remap + 1] = M.override_row(M.PLAYER_FIRST, pal.tunit:sub(first, first + M.PLAYER_COLORS - 1))
	end
	local shift_row = #remap
	remap[#remap + 1] = ts.shift
	local selection_base = #remap
	local selections = math.floor(#pal.tselect / M.SELECTION_COLORS)
	for k = 0, selections - 1 do
		local first = k * M.SELECTION_COLORS + 1
		remap[#remap + 1] = M.override_row(M.SELECTION_FIRST, pal.tselect:sub(first, first + M.SELECTION_COLORS - 1))
	end
	local bright = #remap
	remap[#remap + 1] = M.brightness_row(rgb)

	local tables, base = {}, {}
	for k, name in ipairs(palettes.REMAP_TABLES) do
		local t = ts.remap[name]
		assert(t.rows <= M.BANK_ROWS, "world_lookups: blend bank " .. name .. " is taller than a bank")
		base[k] = #tables
		for r = 0, t.rows - 1 do tables[#tables + 1] = t.data:sub(r * 256 + 1, r * 256 + 256) end
		for _ = t.rows, M.BANK_ROWS - 1 do tables[#tables + 1] = identity end
	end
	local shadow = #tables
	tables[#tables + 1] = ts.dark:sub(M.SHADOW_DARK_ROW * 256 + 1, M.SHADOW_DARK_ROW * 256 + 256)

	local ofire = base[1]
	local outline_color = opts.outline_color or 0
	local rows = {
		owner = function(owner) return owner % M.PLAYER_ROWS + 1 end,
		color = function(color) return color % M.PLAYER_ROWS + 1 end,
		-- The selection wrapper 0x40e900 copies row `param` of 0x594054 (tselect); the
		-- original has no other rows.
		selection = function(k)
			assert(k < selections, "world_lookups: selection colour row out of tselect")
			return selection_base + k
		end,
		shift = function() return shift_row end,
		remap = function(index) return 2 * base[index] + 1 end,
		shadow = 2 * shadow,
		fire = function(p) return 2 * (ofire + p) end,
		warp = opts.warp or function() error("world_lookups: no warp texture", 0) end,
		-- Method 15 draws in the global line colour 0x6d237a of its consumer.
		outline = function() return outline_color end,
	}
	return { remap = remap, tables = tables, rows = rows, trans50 = rows.remap(draw_list.TRANS50), bright = bright }
end

return M
