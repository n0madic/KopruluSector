-- The interface's colours over a tileset's palette (plan phases 9B, 9E): ResolveUiColours 0x4cccb0 picks the 26 UI colours
-- (0x6d2368) among the palette entries that the tileset does not colour-cycle (0x66fd20, the ranges of tileset.kpd
-- CYCL as 0x4697c0 tests them).
local M = {}

-- 256 bytes: 1 for an index in one of the tileset's cycling ranges ({ { low, high }, ... }), else 0.
function M.cycled(cycles)
	local flags = {}
	for i = 0, 255 do
		local f = 0
		for _, c in ipairs(cycles) do
			if i >= c[1] and i <= c[2] then f = 1 end
		end
		flags[i + 1] = string.char(f)
	end
	return table.concat(flags)
end

-- Sets the interface's colours (a kp_core.ui.new interface) from a parsed tileset (game/data/terrain.lua), and the
-- in-game dialogs' blend tables over its palette without the cycled entries (StartDialogPaletteEffect 0x47bca0).
function M.apply(ui, tileset)
	local cycled = M.cycled(tileset.cycles)
	ui:resolve_colours(tileset.rgb, cycled)
	ui:game_effects(tileset.rgb, cycled)
end

-- Gives the interface its minimap's data: the tileset (tileset_data: the tileset.kpd bytes, tileset: parsed), the
-- palettes (game/data/palettes.lua parsed, the tileset's index) and the mission's colour slots (game/data/mission.lua
-- colors, or nil).
function M.minimap(ui, tileset_data, tileset, palettes, index, colors)
	local kpf = require("game.data.kpf")
	local mmni = kpf.parse(tileset_data, "tileset"):section("MMNI")
	local slots = colors and string.char(unpack(colors)) or nil
	ui:minimap(mmni, tileset.rgb, M.cycled(tileset.cycles), palettes.tilesets[index].dark, slots)
end

return M
