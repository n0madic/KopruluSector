-- campaign.kpd reader (converter/campaign.py; layout: docs/formats/kpd.md): the executable's campaign tables, the
-- mission directories of mapdata.dat/mapdata.tbl and the cheat texts.
local bytes = require("game.data.bytes")
local kpf = require("game.data.kpf")

local u8, u16, u32 = bytes.u8, bytes.u16, bytes.u32

local M = {}

M.KIND = "CAMP"
M.SCHEMA = 1
local ROW_SIZE = 8

-- Returns {
--   tables = { [1..6] = { expansion = 0|1, race =, rows = { { label =, mission =, cinematic =, race =, hidden = } } } }
--     in the executable's pointer order (0x50e0b8 Zerg, Terran, Protoss; then 0x50e0c4's), each with its terminator
--     row (mission 0),
--   directories = { [0..64] = "campaign\\terran\\terran01" or "" } (0x51abb8 by mapdata index),
--   cheats = { { bit =, text = } }, carried = the cheat bits a new game keeps }.
function M.parse(data, name, expect)
	local c = kpf.parse(data, name)
	expect = expect or {}
	c:check({ kind = M.KIND, schema_version = M.SCHEMA, profile_id = expect.profile_id,
		source_hash = expect.source_hash })
	local out = { tables = {}, directories = {}, cheats = {} }
	local t = c:section("TABL")
	local at = 1
	for k = 1, u8(t, 0) do
		local tab = { expansion = u8(t, at), race = u8(t, at + 1), rows = {} }
		local count = u16(t, at + 2)
		at = at + 4
		for i = 1, count do
			tab.rows[i] = { label = u16(t, at), mission = u16(t, at + 2), cinematic = u16(t, at + 4),
				race = u8(t, at + 6), hidden = u8(t, at + 7) }
			at = at + ROW_SIZE
		end
		out.tables[k] = tab
	end
	local d = c:section("MAPD")
	at = 2
	for k = 0, u16(d, 0) - 1 do
		local len = u16(d, at)
		out.directories[k] = d:sub(at + 3, at + 2 + len)
		at = at + 2 + len
	end
	local ch = c:section("CHEA")
	out.carried = u32(ch, 0)
	at = 6
	for k = 1, u16(ch, 4) do
		local len = u8(ch, at + 4)
		out.cheats[k] = { bit = u32(ch, at), text = ch:sub(at + 6, at + 5 + len) }
		at = at + 5 + len
	end
	return out
end

return M
