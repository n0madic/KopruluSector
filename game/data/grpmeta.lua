-- grpmeta.kpd reader (kind GRPM). Layout: docs/formats/kpd.md; writer: converter/kpf/kpd.py.
-- GRP frame geometry, the opacity runs of the native hit test, atlas names and LO files,
-- keyed by images.tbl index: images.dat `grp_file` and the six overlay columns (dat.kpd)
-- hold those indices, so `g.grp_of_tbl[dat.images.grp_file[id]]` is image id's GRP entry.
local bytes = require("game.data.bytes")
local kpf = require("game.data.kpf")

local u8, u16, u32, s8 = bytes.u8, bytes.u16, bytes.u32, bytes.s8
local byte = string.byte

local M = {}

M.KIND = "GRPM"
M.SCHEMA = 1
M.ATLAS_MASK = 1
M.FRAME_RECORD = 8
local NONE16 = 0xffff
local RUN_LENGTH = 128 -- run byte: bit 7 transparent, bits 0-6 length

local function label(s, pos)
	local len = u8(s, pos)
	return s:sub(pos + 2, pos + 1 + len), pos + 1 + len
end

-- Returns {
--   tbl_count =, grp_of_tbl = { [images.tbl index] = grp entry }, lo_of_tbl = { [index] = lo entry },
--   atlases = { { name=, pack=, mask=bool } },
--   grps = { { path=, frames=, width=, height=, atlas=, animation=, first_frame= } },
--   los = { { path=, data=, frames=, points= } },
--   frms = <FRMS payload>, opac = <OPAC payload> }.
-- Entries and atlases are 1-based; frames and images.tbl indices keep their native numbering
-- (frames from 0, images.tbl from 1).
function M.parse(data, name, expect)
	local c = kpf.parse(data, name)
	expect = expect or {}
	c:check({ kind = M.KIND, schema_version = M.SCHEMA, profile_id = expect.profile_id,
		source_hash = expect.source_hash })
	local g = { grp_of_tbl = {}, lo_of_tbl = {}, atlases = {}, grps = {}, los = {} }
	local t = c:section("TBLX")
	g.tbl_count = u16(t, 0)
	for i = 1, g.tbl_count do
		local grp, lo = u16(t, 4 * i - 2), u16(t, 4 * i)
		if grp ~= NONE16 then g.grp_of_tbl[i] = grp + 1 end
		if lo ~= NONE16 then g.lo_of_tbl[i] = lo + 1 end
	end
	local a, pos = c:section("ATLS"), 1
	for i = 1, u8(a, 0) do
		local flags = u8(a, pos)
		local pack, atlas
		pack, pos = label(a, pos + 1)
		atlas, pos = label(a, pos)
		g.atlases[i] = { name = atlas, pack = pack, mask = flags % 2 == M.ATLAS_MASK }
	end
	local s = c:section("GRPS")
	pos = 2
	for i = 1, u16(s, 0) do
		local e = {}
		e.path, pos = label(s, pos)
		e.frames, e.width, e.height = u16(s, pos), u16(s, pos + 2), u16(s, pos + 4)
		e.atlas = u8(s, pos + 6) + 1
		e.animation, pos = label(s, pos + 7)
		e.first_frame = u32(s, pos)
		pos = pos + 4
		g.grps[i] = e
	end
	local l = c:section("LOFS")
	pos = 2
	for i = 1, u16(l, 0) do
		local e = {}
		e.path, pos = label(l, pos)
		local size = u32(l, pos)
		e.data = l:sub(pos + 5, pos + 4 + size)
		e.frames, e.points = u32(e.data, 0), u32(e.data, 4)
		pos = pos + 4 + size
		g.los[i] = e
	end
	g.frms, g.opac = c:section("FRMS"), c:section("OPAC")
	return g
end

-- GRP entry of an images.dat id (`images` is dat.parse(...).images), or nil.
function M.image_grp(g, images, id)
	return g.grp_of_tbl[images.grp_file[id]]
end

-- LO entry of an images.dat id for an overlay column ("attack_overlay", ..., "shield_overlay"), or nil.
function M.image_lo(g, images, column, id)
	return g.lo_of_tbl[images[column][id]]
end

local function record(g, entry, frame)
	return 4 + M.FRAME_RECORD * (g.grps[entry].first_frame + frame)
end

-- Frame rectangle exactly as stored in the GRP: x, y, width, height (frame from 0).
function M.frame(g, entry, frame)
	local a = record(g, entry, frame)
	local f = g.frms
	return u8(f, a), u8(f, a + 1), u8(f, a + 2), u8(f, a + 3)
end

-- `IsGrpPixelOpaque` 0x40fd50: 1 when frame pixel (x, y) is opaque for hit testing, else 0.
-- Out-of-frame coordinates (unsigned compare natively) return 0. The loop subtracts run
-- lengths while x > 0 and returns the class of the last run read, so a pixel exactly on a
-- run boundary belongs to the preceding run. `frame` must be < the entry's frame count.
function M.pixel_opaque(g, entry, frame, x, y)
	local a = record(g, entry, frame)
	local f = g.frms
	if x < 0 or y < 0 or x >= u8(f, a + 2) or y >= u8(f, a + 3) then return 0 end
	local opac = g.opac
	local base = u32(f, a + 4)
	local pos = base + u16(opac, base + 2 * y) + 1 -- 1-based string position
	local run
	repeat
		run = byte(opac, pos)
		pos = pos + 1
		x = x - run % RUN_LENGTH
	until x <= 0
	return run >= RUN_LENGTH and 0 or 1
end

-- Signed (x, y) of LO point `point` in `frame` (both from 0), unmirrored, read unchecked
-- like the native getters.
function M.lo_point(lo, frame, point)
	local d = lo.data
	local at = u32(d, 8 + 4 * frame) + 2 * point
	return s8(d, at), s8(d, at + 1)
end

return M
