-- Selection image constants and the bar kind of a unit (docs/research/native-health-bars.md, atlas/p05.md): the
-- health-bar and placement-box records, health bars of selected sprites and selection circles live in the native
-- core's images world (kp_core, ADR 0008).
local image = require("game.images.image")

local M = {}

M.HEALTH_IMAGE, M.BOX_IMAGE = 0x230, 0x247 -- images of the health-bar and box records
M.METHOD_HEALTH, M.METHOD_BOX, M.METHOD_SELECTION, M.METHOD_SHADOW = 11, 15, 13, 10
M.CIRCLE_FIRST, M.CIRCLE_LAST, M.CIRCLE_DASHED_FIRST = 0x231, 0x23a, 0x23b -- selection circle images by size
M.SF_CIRCLE, M.SF_SELECTED = 0x01, 0x08 -- sprite flags
M.BAR_SEGMENT, M.BAR_MIN_WIDTH = 3, 19
M.BAR_ROWS, M.BAR_SHIELD_ROWS, M.BAR_ENERGY_ROWS = 5, 7, 6
M.BAR_Y_GAP = 8 -- rows between the selection circle's middle and the bar
M.SPELLCASTER = 0x200000 -- units.dat special ability flag
M.HALLUCINATION = image.STATUS_HALLUCINATION
M.BROODLING = 40
M.BAR_ENERGY, M.BAR_TIMER = "energy", "timer"

local has = image.has

-- The bar below the hit points of 0x40f6e0 and 0x464d20: BAR_ENERGY for a spellcaster that is
-- not a hallucination, BAR_TIMER for a hallucination or a broodling, else nil.
function M.bar_kind(ability_flags, status, unit_type)
	local hallucination = has(status, M.HALLUCINATION)
	if has(ability_flags, M.SPELLCASTER) and not hallucination then return M.BAR_ENERGY end
	if hallucination or unit_type == M.BROODLING then return M.BAR_TIMER end
	return nil
end

return M
