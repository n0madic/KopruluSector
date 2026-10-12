-- Sprite readback check of the viewer's web autotest: isolated static images in the view
-- must show the palette indices of their GRP frames (converter/emit/sprite_probe.py writes
-- the frames of a few probe animations to generated/viewer/sprite_probe.bin). Pixels of the
-- player colour range are skipped (they go through the remap rows), as are transparent ones.
local bytes = require("game.data.bytes")

local M = {}

M.MAGIC = "KPSP"
M.REMAP_FIRST, M.REMAP_LAST = 8, 15

-- Returns { [animation] = { [frame from 0] = { w =, h =, pixels = } } }.
function M.parse(data)
	assert(data:sub(1, 4) == M.MAGIC, "sprite_check: not a sprite probe")
	local out, at = {}, 6
	for _ = 1, bytes.u16(data, 4) do
		local n = data:byte(at + 1)
		local name = data:sub(at + 2, at + 1 + n)
		at = at + 1 + n
		local frames, count = {}, bytes.u16(data, at)
		at = at + 2
		for i = 0, count - 1 do
			local w, h = bytes.u16(data, at), bytes.u16(data, at + 2)
			frames[i] = { w = w, h = h, pixels = data:sub(at + 5, at + 4 + w * h) }
			at = at + 4 + w * h
		end
		out[name] = frames
	end
	return out
end

-- images: array of { left =, top =, w =, h =, rank = (optional draw rank) } (map pixels);
-- returns those whose rectangle meets no image drawn after them (with ranks) or no other
-- image at all (without). Images drawn before (a unit's own shadow) are covered by it.
function M.isolated(images)
	local out = {}
	for i, a in ipairs(images) do
		local alone = true
		for j, b in ipairs(images) do
			if i ~= j and (not a.rank or b.rank > a.rank) and a.left < b.left + b.w and b.left < a.left + a.w
				and a.top < b.top + b.h and b.top < a.top + a.h then
				alone = false
				break
			end
		end
		if alone then out[#out + 1] = a end
	end
	return out
end

-- frame: a probe frame; got: w*h index bytes read back with rows bottom-up (GL order).
-- Returns the number of compared pixels and of mismatches.
function M.compare(frame, flip, got)
	local w, h, px = frame.w, frame.h, frame.pixels
	local checked, bad = 0, 0
	for j = 0, h - 1 do
		local row = (h - 1 - j) * w
		for i = 0, w - 1 do
			local v = px:byte(j * w + (flip and w - 1 - i or i) + 1)
			if v ~= 0 and (v < M.REMAP_FIRST or v > M.REMAP_LAST) then
				checked = checked + 1
				if got:byte(row + i + 1) ~= v then bad = bad + 1 end
			end
		end
	end
	return checked, bad
end

return M
