-- iscript.kpd and aiscript.kpd readers. Layout: docs/formats/kpd.md.
-- Bytecode stays one Lua string per binary; VMs read it by byte offset (plan §4.3).
local bytes = require("game.data.bytes")
local kpf = require("game.data.kpf")

local u16, u32 = bytes.u16, bytes.u32

local M = {}

M.ISCRIPT_KIND = "ISCR"
M.AISCRIPT_KIND = "AISC"
M.SCHEMA = 1
M.BIN_AISCRIPT = 0
M.BIN_BWSCRIPT = 1

local function open(data, name, kind, expect)
	local c = kpf.parse(data, name)
	expect = expect or {}
	c:check({ kind = kind, schema_version = M.SCHEMA, profile_id = expect.profile_id,
		source_hash = expect.source_hash })
	return c
end

-- Returns { code = <iscript.bin>, headers = { [iscript id] = header offset } }.
-- A header's type byte is code offset +4, animation PCs are u16 from header +8.
function M.parse_iscript(data, name, expect)
	local c = open(data, name, M.ISCRIPT_KIND, expect)
	local h = c:section("HDRS")
	local headers = {}
	for i = 0, u16(h, 0) - 1 do headers[u16(h, 2 + 4 * i)] = u16(h, 4 + 4 * i) end
	return { code = c:section("CODE"), headers = headers }
end

-- Returns { aiscript = <bin>, bwscript = <bin> }.
function M.parse_aiscript(data, name, expect)
	local c = open(data, name, M.AISCRIPT_KIND, expect)
	return { aiscript = c:section("AISC"), bwscript = c:section("BWSC") }
end

local function find(bin, id, stride)
	local pos = u32(bin, 0)
	while true do
		local rid = bin:sub(pos + 1, pos + 4)
		if rid == "\0\0\0\0" then return nil end
		if rid == id then return u32(bin, pos + 4) end
		pos = pos + stride
	end
end

-- FindAiScriptOffset 0x494d20: id is the four-byte string (e.g. "Ter5").
-- Returns offset, bin selector; or nil when neither binary has a nonzero offset.
function M.resolve_ai(ai, id)
	local offset = find(ai.aiscript, id, 16)
	if offset and offset ~= 0 then return offset, M.BIN_AISCRIPT end
	offset = find(ai.bwscript, id, 8)
	if offset and offset ~= 0 then return offset, M.BIN_BWSCRIPT end
	return nil
end

return M
