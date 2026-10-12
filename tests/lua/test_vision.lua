-- Line-of-sight templates (game/vision/fog.lua) against the original's generators, and the
-- reveal rules of the ground and air callbacks on small hand-made maps.
local fog = require("game.vision.fog")
local crc32 = require("game.data.crc32")

local S = {}

local function golden(T)
	return assert(loadstring or load)(T.read("tests/golden/oracle/vision/templates.lua"))()
end

function S.templates_equal_the_original(T)
	local want = golden(T)
	for _, stride in ipairs({ 64, 128, 256 }) do
		local t = fog.templates(stride)
		for sight = 0, fog.SIGHTS - 1 do
			local parts = {}
			for _, r in ipairs(t[sight].records) do
				local deps = {}
				for _, d in ipairs(r.deps) do deps[#deps + 1] = tostring(d) end
				parts[#parts + 1] = r.dx .. "," .. r.dy .. "," .. r.offset .. "," .. table.concat(deps, "/") .. ";"
			end
			local w = want[stride][sight]
			T.eq(t[sight].inner, w.inner, "inner count, sight " .. sight)
			T.eq(t[sight].outer, w.outer, "outer count, sight " .. sight)
			T.eq(crc32.of(table.concat(parts)), w.crc, ("records, stride %d sight %d"):format(stride, sight))
		end
	end
end

return S
