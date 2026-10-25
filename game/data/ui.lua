-- ui.kpd reader (kind "UI  "). Layout: docs/formats/kpd.md; writer: converter/kpf/kpd.py.
-- Dialogs are the BIN widget trees after the native fixup rules (hotkey lowercase,
-- type-14 coordinate additions, shared bitmaps); widgets keep native traversal order.
local bytes = require("game.data.bytes")
local kpf = require("game.data.kpf")

local u8, u16, u32, s16 = bytes.u8, bytes.u16, bytes.u32, bytes.s16

local M = {}

M.KIND = "UI  "
M.SCHEMA = 3
M.BUTTON_SIZE = 20
M.WIDGET_SIZE = 60
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
				-- Raw negative u32: the shared bitmap is at file base + signed value + 0x10.
				shared_bitmap = u32(d, a + 48) ~= NONE32 and u32(d, a + 48) or nil,
				delay = u16(d, a + 54), user = u32(d, a + 56),
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

-- A files section (DGRP, PCXS): { [path] = <the file's bytes> }.
local function files(g)
	local out, pos = {}, 2
	for _ = 1, u16(g, 0) do
		local len = u8(g, pos)
		local path = g:sub(pos + 2, pos + 1 + len)
		local size = u32(g, pos + 1 + len)
		out[path] = g:sub(pos + 6 + len, pos + 5 + len + size)
		pos = pos + 5 + len + size
	end
	return out
end

local function label(s, pos)
	local len = u8(s, pos)
	return s:sub(pos + 2, pos + 1 + len), pos + 1 + len
end

-- The executable's interface tables (converter/ui_tables.py): button sets with their native 20-byte buttons,
-- status layouts, fluff rectangles, cursors, fixed rectangles, music tracks, attenuation, accelerators.
local function parse_tables(c)
	local t = {}
	local b, sets, pos = c:section("BTNS"), {}, 2
	for k = 1, u16(b, 0) do
		local link, n = u16(b, pos), u16(b, pos + 2)
		pos = pos + 4
		local buttons = {}
		for i = 1, n do
			buttons[i] = { position = u16(b, pos), icon = u16(b, pos + 2), condition = u32(b, pos + 4),
				action = u32(b, pos + 8), condition_value = u16(b, pos + 12), action_value = u16(b, pos + 14),
				enabled_string = u16(b, pos + 16), disabled_string = u16(b, pos + 18) }
			pos = pos + M.BUTTON_SIZE
		end
		sets[k] = { link = link ~= NONE16 and link or nil, buttons = buttons } -- sets[set + 1]
	end
	t.button_sets, t.buttons_raw = sets, b
	local l = c:section("STLY")
	t.status_layouts = {}
	for k = 1, u16(l, 0) do
		t.status_layouts[k] = { refresh = u32(l, 2 + 8 * (k - 1)), draw = u32(l, 6 + 8 * (k - 1)) }
	end
	local f = c:section("FLUF")
	t.fluff, pos = {}, 1
	for k = 1, u8(f, 0) do
		local rects = {}
		for i = 1, u8(f, pos) do
			local a = pos + 1 + 8 * (i - 1)
			rects[i] = { x = u16(f, a), y = u16(f, a + 2), w = u16(f, a + 4), h = u16(f, a + 6) }
		end
		pos = pos + 1 + 8 * #rects
		t.fluff[k] = rects -- zerg, terran, protoss, replay
	end
	local cu = c:section("CURS")
	t.cursors, pos = {}, 1
	for k = 1, u8(cu, 0) do t.cursors[k], pos = label(cu, pos) end
	t.edge_scroll = words(cu, pos, 9)
	local r = c:section("RECT")
	t.rects, pos = {}, 1
	for _ = 1, u8(r, 0) do
		local name
		name, pos = label(r, pos)
		t.rects[name] = { s16(r, pos), s16(r, pos + 2), s16(r, pos + 4), s16(r, pos + 6) }
		pos = pos + 8
	end
	local mu = c:section("MUSI")
	t.music, pos = {}, 1
	for k = 1, u8(mu, 0) do
		local file
		file, pos = label(mu, pos)
		t.music[k] = { file = file, kind = u8(mu, pos), fade_in = u8(mu, pos + 1), next = u8(mu, pos + 2) }
		pos = pos + 3
	end
	local v = c:section("VOLT")
	t.attenuation = {}
	for k = 1, u8(v, 0) do
		local x = u32(v, 1 + 4 * (k - 1))
		t.attenuation[k] = x >= 0x80000000 and x - 0x100000000 or x
	end
	local a = c:section("ACCL")
	t.accelerators, pos = {}, 1
	for k = 1, u8(a, 0) do
		local entries = {}
		local n = u16(a, pos + 3)
		for i = 1, n do
			local e = pos + 5 + 6 * (i - 1)
			entries[i] = { flags = u16(a, e), key = u16(a, e + 2), command = u16(a, e + 4) }
		end
		t.accelerators[k] = { module = u8(a, pos) == 1 and "StarCraft.exe" or "Local.dll", id = u16(a, pos + 1),
			entries = entries }
		pos = pos + 5 + 6 * n
	end
	return t
end

-- Returns { dialogs = { ["rez\\gluMain.bin"] = { widgets=, smks= } }, theme_fonts = <12 x 48>,
-- theme_effects = { size=, data= }, grps = { [path] = <GRP> }, pcx = { [path] = <PCX> }, tables = <parse_tables> }.
function M.parse(data, name, expect)
	local c = kpf.parse(data, name)
	expect = expect or {}
	c:check({ kind = M.KIND, schema_version = M.SCHEMA, profile_id = expect.profile_id,
		source_hash = expect.source_hash })
	local out = { dialogs = parse_dialogs(c:section("DLGS"), c:section("POOL")), theme_fonts = c:section("TFNT") }
	local te = c:section("TEFF")
	out.theme_effects = { size = u16(te, 0), data = te:sub(3) }
	local grps = files(c:section("DGRP"))
	out.grps = grps
	out.pcx = files(c:section("PCXS"))
	out.glue = files(c:section("GFIL")) -- the glue's backgrounds, dialog bitmaps, room movies and score art
	out.tables = parse_tables(c)
	return out
end

return M
