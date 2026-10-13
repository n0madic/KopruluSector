-- Composition plan shared by the world image backend and the render script (game.project
-- [script] shared_state = 1: both see this module). The live backend publishes the plan of
-- its uploaded image data: current.windows[k] = { rank =, last =, layers = } for
-- k = 1..current.count (window k ends with a copy of the target at `rank`, which the
-- distortion images ranked rank..last read), current.layers = destination layers after the
-- last copy (ADR 0006, presentation/draw_list.lua).
--
-- Command budget: Defold 1.13 records at most COMMAND_BUDGET render commands per frame
-- (engine.cpp, fixed); a full buffer raises and drops the rest of the frame. The draw list
-- builds only plans whose commands() fit, so the render script draws every plan as it is.
-- The command counts follow draw_images in render/koprulu.render_script; keep them equal.
local M = {}

M.EMPTY = { windows = {}, count = 0, layers = 0 }
M.current = M.EMPTY

M.COMMAND_BUDGET = 1024
M.FRAME_COMMANDS = 96 -- the passes other than draw_images (about 45), with room to spare
M.IMAGE_COMMANDS = 27 -- draw_images' fixed commands, the last window's overwriting pass included
M.WINDOW_COMMANDS = 10 -- a window's overwriting pass, its copy with the restore (8) and its distortion pass
M.LAYER_COMMANDS = 9 -- a destination layer: its copy with the restore (8) and its pass
-- Commands left to the windows and layers of a plan.
M.PLAN_BUDGET = M.COMMAND_BUDGET - M.FRAME_COMMANDS - M.IMAGE_COMMANDS

-- Commands of the windows and layers of a plan (count windows with windows[k].layers each,
-- then `layers` after the last copy).
function M.commands(windows, count, layers)
	local n = layers * M.LAYER_COMMANDS
	for k = 1, count do n = n + M.WINDOW_COMMANDS + windows[k].layers * M.LAYER_COMMANDS end
	return n
end

return M
