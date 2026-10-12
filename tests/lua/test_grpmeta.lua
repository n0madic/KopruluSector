-- Reads grpmeta.kpd written by converter/kpf/fixtures.py (run it first). The expected hit
-- strings are the Python twin of 0x40fd50 run on the raw GRP bytes.
local grpmeta = require("game.data.grpmeta")

local DIR = "tests/fixtures/generated/kpf/"

local function parse(T)
	return grpmeta.parse(T.read(DIR .. "grpmeta.kpd"), "grpmeta.kpd")
end

local function expect(T)
	return assert(loadstring or load)(T.read(DIR .. "expect.lua"))()
end

local S = {}

function S.entries_and_atlases(T)
	local g = parse(T)
	T.eq(g.tbl_count, 5)
	T.eq(g.grp_of_tbl[1], 1)
	T.eq(g.grp_of_tbl[4], 1) -- same file, other case
	T.eq(g.grp_of_tbl[3], 2)
	T.eq(g.grp_of_tbl[2], nil)
	T.eq(g.grp_of_tbl[5], nil)
	T.eq(g.lo_of_tbl[2], 1)
	T.eq(#g.grps, 2)
	local t = g.grps[1]
	T.eq(t.path, "unit\\terran\\fix.grp")
	T.eq(t.frames, 3)
	T.eq(t.width, 40)
	T.eq(t.height, 30)
	T.eq(t.animation, "terran_fix")
	T.eq(t.first_frame, 0)
	T.eq(g.grps[2].first_frame, 3)
	T.eq(g.atlases[t.atlas].name, "terran")
	T.eq(g.atlases[t.atlas].mask, false)
	local e = g.atlases[g.grps[2].atlas]
	T.eq(e.name, "effects")
	T.eq(e.pack, "effects")
	T.eq(e.mask, true) -- the effects fixture draws an opaque index 0
end

function S.frames(T)
	local g = parse(T)
	local x, y, w, h = grpmeta.frame(g, 1, 0)
	T.eq(x, 3)
	T.eq(y, 4)
	T.eq(w, 23)
	T.eq(h, 9)
	x, y, w, h = grpmeta.frame(g, 1, 1)
	T.eq(w, 200)
	T.eq(h, 4)
	x, y, w, h = grpmeta.frame(g, 1, 2)
	T.eq(w, 0)
	T.eq(h, 0)
	x, y, w, h = grpmeta.frame(g, 2, 0)
	T.eq(x, 1)
	T.eq(w, 4)
end

function S.pixel_opaque_matches_python_twin(T)
	local g = parse(T)
	local hits = expect(T).grpmeta_hits
	local n = 0
	for entry = 1, #g.grps do
		for frame = 0, g.grps[entry].frames - 1 do
			n = n + 1
			local _, _, w, h = grpmeta.frame(g, entry, frame)
			local want, i = hits[n], 0
			for y = -1, h do
				for x = -1, w do
					i = i + 1
					local got = grpmeta.pixel_opaque(g, entry, frame, x, y)
					if got ~= tonumber(want:sub(i, i)) then
						error(("entry %d frame %d pixel (%d, %d): got %d"):format(entry, frame, x, y, got))
					end
				end
			end
			T.eq(i, #want)
		end
	end
	T.eq(n, #hits)
end

function S.run_boundaries(T)
	local g = parse(T)
	-- Frame 1 row 0: leading empty transparent packet, literal 2, repeat 3, transparent 133.
	T.eq(grpmeta.pixel_opaque(g, 1, 1, 0, 0), 0) -- x = 0 takes the first (empty, transparent) packet
	T.eq(grpmeta.pixel_opaque(g, 1, 1, 5, 0), 1) -- boundary 5 belongs to the preceding opaque run
	T.eq(grpmeta.pixel_opaque(g, 1, 1, 6, 0), 0)
	T.eq(grpmeta.pixel_opaque(g, 1, 1, 138, 0), 0) -- end of the transparent 5 + 127 + 1
	T.eq(grpmeta.pixel_opaque(g, 1, 1, 139, 0), 1)
	T.eq(grpmeta.pixel_opaque(g, 1, 1, 200, 0), 0) -- outside the frame
end

function S.lo_points(T)
	local g = parse(T)
	local lo = g.los[1]
	T.eq(lo.path, "unit\\terran\\fix.lol")
	T.eq(lo.frames, 2)
	T.eq(lo.points, 2)
	local x, y = grpmeta.lo_point(lo, 0, 0)
	T.eq(x, -3)
	T.eq(y, 4)
	x, y = grpmeta.lo_point(lo, 0, 1)
	T.eq(x, 127)
	T.eq(y, -128)
	x, y = grpmeta.lo_point(lo, 1, 0)
	T.eq(x, 0)
	T.eq(y, -1)
end

function S.image_lookup(T)
	local g = parse(T)
	local images = { grp_file = { [0] = 4, [1] = 0 }, attack_overlay = { [0] = 2, [1] = 0 } }
	T.eq(grpmeta.image_grp(g, images, 0), 1)
	T.eq(grpmeta.image_grp(g, images, 1), nil)
	T.eq(grpmeta.image_lo(g, images, "attack_overlay", 0), 1)
	T.eq(grpmeta.image_lo(g, images, "attack_overlay", 1), nil)
end

return S
