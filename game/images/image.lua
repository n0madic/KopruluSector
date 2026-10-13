-- Image constants and flag helpers of the original's image records (docs/research/atlas/p04.md, "Image state
-- helpers", and p03.md "Image callbacks"); the records live in the native core's images world (kp_core, ADR 0008).
--
-- Representation notes:
-- - `grp` is the grpmeta entry of the image's GRP, 0 for none (the free-record marker of
--   the original), or VIRTUAL for the per-record headers of health bars and placement boxes.
-- - `param` is the drawing parameter DWORD +0x30. For draw method 9 it holds the remapping
--   index (1..7) instead of the table pointer, as the original's saves do.
local int = require("game.simulation.int")

local M = {}

M.VIRTUAL = -1
M.METHOD_WARP_FIRE = 17
M.WARP_FIRE_PARAM = 0x230
M.WARP_TEXTURE = 210 -- image whose GRP method 12 draws through (native pointer slot 0x543a40)
M.METHOD_REMAP = 9
M.METHOD_CLOAK, M.METHOD_DECLOAK, M.METHOD_DECLOAK_BLEND = 2, 4, 7
M.CLOAK_START, M.DECLOAK_START = 0x300, 0x308 -- parameter byte and timer 3 in the low WORD
M.METHOD_PLAYER_COLOR = 14
M.METHOD_HALLUCINATION = 16
M.LO_COLUMNS = { [0] = "attack_overlay", "damage_overlay", "special_overlay", "landing_dust_overlay",
	"lift_off_dust_overlay" }
M.LO_SPECIAL = 2
M.DAMAGE_SLOTS, M.LO_UNUSED = 22, 127
-- Animations with special handling in PlayImageAnimation 0x411730.
M.ANIM_INIT, M.ANIM_DEATH, M.ANIM_GND_ATTK_INIT, M.ANIM_AIR_ATTK_INIT = 0, 1, 2, 3
M.ANIM_GND_ATTK_RPT, M.ANIM_AIR_ATTK_RPT, M.ANIM_WALKING, M.ANIM_IS_WORKING = 5, 6, 11, 19
-- Unit status bits read by image code (CUnit +0xdc).
M.STATUS_CLOAKED = 0x300
M.STATUS_HALLUCINATION = 0x40000000

local s8, s16 = int.s8, int.s16
M.s8, M.s16 = s8, s16

local function has(flags, bit) return flags % (bit * 2) >= bit end
M.has = has

local function set(flags, bit, on)
	if has(flags, bit) then
		if not on then return flags - bit end
	elseif on then
		return flags + bit
	end
	return flags
end
M.set = set

-- Unit status with a cloak state (bits 0x300: cloaked or requiring detection).
function M.cloaked(status) return int.band(status, M.STATUS_CLOAKED) ~= 0 end

return M
