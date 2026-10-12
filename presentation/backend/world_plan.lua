-- Composition plan shared by the world image backend and the render script (game.project
-- [script] shared_state = 1: both see this module). The live backend publishes the plan of
-- its uploaded image data: current.windows[k] = { rank =, last =, layers = } for
-- k = 1..current.count (window k ends with a copy of the target at `rank`, which the
-- distortion images ranked rank..last read), current.layers = destination layers after the
-- last copy (ADR 0006, presentation/draw_list.lua).
local M = {}

M.EMPTY = { windows = {}, count = 0, layers = 0 }
M.current = M.EMPTY

return M
