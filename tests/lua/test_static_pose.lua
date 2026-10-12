-- Viewer static pose from a synthetic iscript: header at 4, Init animation at 20.
local pose = require("presentation.static_pose")

local function w(v) return string.char(v % 256, math.floor(v / 256)) end

local function script(init)
	-- 0: (unused dword), 4: SCPE header type 0 with animation 0 at 20
	local header = "SCPE" .. string.char(0, 0, 0, 0) .. w(20) .. w(0)
	local code = string.rep("\0", 4) .. header
	code = code .. string.rep("\0", 20 - #code) .. init
	return code
end

local S = {}

function S.playfram_until_wait(T)
	-- playfram 17; playsndrand 2 sounds; wait 1; playfram 99 (not reached)
	local code = script(string.char(0x00) .. w(17) .. string.char(0x19, 2) .. w(5) .. w(6) .. string.char(0x05, 1, 0x00) .. w(99))
	T.eq(pose.resolve(code, 4, 0, 0, 0), 17)
	local frame, flip = pose.resolve(code, 4, 5, 1, 0)
	T.eq(frame, 22)
	T.eq(flip, false)
	frame, flip = pose.resolve(code, 4, 20, 1, 0)
	T.eq(frame, 17 + 12)
	T.eq(flip, true)
end

function S.goto_tile_and_flip(T)
	-- 20: goto 26; 23: (junk); 26: playframtile 4; setflipstate 1; end
	local code = script(string.char(0x07) .. w(26) .. string.char(0xff, 0xff, 0xff) .. string.char(0x01) .. w(4)
		.. string.char(0x17, 1, 0x16))
	local frame, flip = pose.resolve(code, 4, 0, 0, 3)
	T.eq(frame, 7)
	T.eq(flip, true)
end

return S
