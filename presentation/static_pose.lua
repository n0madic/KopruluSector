-- Static pose of an image for the map viewer: the frame and mirror state its Init animation
-- reaches before the first wait. This is a viewer approximation, not the IScript VM (plan
-- phase 2, game/images): it follows playfram/playframtile/setflipstate/goto and stops at
-- wait/waitrand/end or after a step limit; every other opcode is skipped by its operand size
-- (atlas P04 interpreter table, defs/iscript/bw-1.17.0.1.json).
local bytes = require("game.data.bytes")

local u8, u16 = bytes.u8, bytes.u16

local M = {}

local SOUNDS = -1
local SIZE = {
	[0x00] = 2, [0x01] = 2, [0x02] = 1, [0x03] = 1, [0x04] = 2, [0x05] = 1, [0x06] = 2, [0x07] = 2, [0x08] = 4,
	[0x09] = 4, [0x0a] = 2, [0x0b] = 2, [0x0d] = 4, [0x0e] = 4, [0x0f] = 4, [0x10] = 4, [0x11] = 4, [0x12] = 2,
	[0x13] = 4, [0x14] = 4, [0x15] = 3, [0x16] = 0, [0x17] = 1, [0x18] = 2, [0x19] = SOUNDS, [0x1a] = 4,
	[0x1b] = 0, [0x1c] = SOUNDS, [0x1d] = 0, [0x1e] = 3, [0x1f] = 1, [0x20] = 1, [0x21] = 0, [0x22] = 1,
	[0x23] = 1, [0x24] = 1, [0x25] = 1, [0x26] = 0, [0x27] = 0, [0x28] = 1, [0x29] = 1, [0x2a] = 0, [0x2b] = 1,
	[0x2c] = 1, [0x2d] = 0, [0x2e] = 0, [0x2f] = 0, [0x30] = 0, [0x31] = 1, [0x32] = 0, [0x33] = 0, [0x34] = 1,
	[0x35] = 2, [0x36] = 0, [0x37] = 2, [0x38] = 1, [0x39] = 2, [0x3a] = 4, [0x3b] = 6, [0x3c] = 6, [0x3d] = 2,
	[0x3f] = 2, [0x40] = 2, [0x41] = 1, [0x42] = 4, [0x44] = 0,
}
local PLAYFRAM, PLAYFRAMTILE, WAIT, WAITRAND, GOTO, END, SETFLIPSTATE = 0x00, 0x01, 0x05, 0x06, 0x07, 0x16, 0x17
local STEP_LIMIT = 256

-- code: iscript.bin; header: header offset of the image's script; direction: 0..31
-- (32-direction units); turns: images.dat gfx_turns (nonzero adds the direction to the
-- frame, mirrored for 17..31); tileset: ERA & 7 for playframtile.
-- Returns frame, flip.
function M.resolve(code, header, direction, turns, tileset)
	local pc = u16(code, header + 8) -- animation 0: Init
	local frameset, flip = 0, false
	for _ = 1, STEP_LIMIT do
		if pc == 0 then break end
		local op = u8(code, pc)
		if op == PLAYFRAM then
			frameset = u16(code, pc + 1)
		elseif op == PLAYFRAMTILE then
			frameset = (u16(code, pc + 1) + tileset) % 65536
		elseif op == SETFLIPSTATE then
			flip = u8(code, pc + 1) ~= 0
		elseif op == WAIT or op == WAITRAND or op == END then
			break
		end
		if op == GOTO then
			pc = u16(code, pc + 1)
		else
			local size = SIZE[op] or 0 -- 0x0c, 0x3e, 0x43 and above 0x44 are one-byte no-ops
			if size == SOUNDS then size = 1 + 2 * u8(code, pc + 1) end
			pc = pc + 1 + size
		end
	end
	if turns == 0 then return frameset, flip end
	local q = direction % 32
	if q <= 16 then return frameset + q, false end
	return frameset + 32 - q, true
end

return M
