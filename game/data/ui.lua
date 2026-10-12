-- ui.kpd reader (kind "UI  "). Layout: docs/formats/kpd.md; writer: converter/kpf/kpd.py.
-- Dialogs are the BIN widget trees after the native fixup rules (hotkey lowercase,
-- type-14 coordinate additions, shared bitmaps); widgets keep native traversal order.
local bytes = require("game.data.bytes")
local kpf = require("game.data.kpf")

local u8, u16, u32, s16 = bytes.u8, bytes.u16, bytes.u32, bytes.s16

local M = {}

M.KIND = "UI  "
M.SCHEMA = 1
M.WIDGET_SIZE = 52
M.SMK_SIZE = 28
local NONE16, NONE32 = 0xffff, 0xffffffff

local function pool_string(pool, offset, length)
	if offset == NONE32 then return nil end
	return pool:sub(offset + 1, offset + length)
end

local function words(s, at, n)
	local t = {}
	for i = 1, n do t[i] = u16(s, at + 2 * (i - 1)) end
	return t
end

local function parse_dialogs(d, pool)
	local out, pos = {}, 2
	for _ = 1, u16(d, 0) do
		local len = u8(d, pos)
		local name = d:sub(pos + 2, pos + 1 + len)
		pos = pos + 1 + len
		local nw, ns = u16(d, pos), u16(d, pos + 2)
		pos = pos + 4
		local widgets = {}
		for i = 1, nw do
			local a = pos + (i - 1) * M.WIDGET_SIZE
			local parent, first = u16(d, a), u16(d, a + 28)
			widgets[i] = {
				parent = parent ~= NONE16 and parent + 1 or nil, -- 1-based index
				type = u16(d, a + 2), id = u16(d, a + 4), flags = u32(d, a + 6),
				x1 = s16(d, a + 10), y1 = s16(d, a + 12), x2 = s16(d, a + 14), y2 = s16(d, a + 16),
				width = u16(d, a + 18), height = u16(d, a + 20),
				text = pool_string(pool, u32(d, a + 22), u16(d, a + 26)),
				first_smk = first ~= NONE16 and first + 1 or nil, smk_count = u16(d, a + 30),
				responsive = words(d, a + 32, 4), extra = words(d, a + 40, 4),
				shared_bitmap = u32(d, a + 48) ~= NONE32 and u32(d, a + 48) or nil,
			}
		end
		pos = pos + nw * M.WIDGET_SIZE
		local smks = {}
		for i = 1, ns do
			local a = pos + (i - 1) * M.SMK_SIZE
			smks[i] = { flags = u16(d, a), filename = pool_string(pool, u32(d, a + 2), u16(d, a + 6)),
				offset_x = s16(d, a + 8), offset_y = s16(d, a + 10) }
		end
		pos = pos + ns * M.SMK_SIZE
		out[name] = { widgets = widgets, smks = smks }
	end
	return out
end

-- Returns { dialogs = { ["rez\\gluMain.bin"] = { widgets=, smks= } }, theme_fonts = <12 x 48>,
-- theme_effects = { size=, data= }, grps = { [path] = <raw GRP header> } }.
function M.parse(data, name, expect)
	local c = kpf.parse(data, name)
	expect = expect or {}
	c:check({ kind = M.KIND, schema_version = M.SCHEMA, profile_id = expect.profile_id,
		source_hash = expect.source_hash })
	local out = { dialogs = parse_dialogs(c:section("DLGS"), c:section("POOL")), theme_fonts = c:section("TFNT") }
	local te = c:section("TEFF")
	out.theme_effects = { size = u16(te, 0), data = te:sub(3) }
	local g, grps, pos = c:section("DGRP"), {}, 2
	for _ = 1, u16(g, 0) do
		local len = u8(g, pos)
		local path = g:sub(pos + 2, pos + 1 + len)
		local size = u32(g, pos + 1 + len)
		grps[path] = g:sub(pos + 6 + len, pos + 5 + len + size)
		pos = pos + 5 + len + size
	end
	out.grps = grps
	return out
end

return M
