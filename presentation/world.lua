-- World coordinates shared by the presentation modules: the terrain template is
-- TEMPLATE x TEMPLATE tiles of TILE pixels; map row 0 is the top row, so map pixel
-- (px, py) is world (px, TOP - py) with world y growing upwards.
local M = {}

M.TEMPLATE = 256
M.TILE = 32
M.TOP = M.TEMPLATE * M.TILE

function M.world_y(map_y)
	return M.TOP - map_y
end

return M
