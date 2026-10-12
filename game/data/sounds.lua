-- sounds.kpd reader (kind SNDS). Layout: docs/formats/kpd.md; writer: converter/kpf/kpd.py.
-- RIFF metadata of every sfxdata sound, for the native duration arithmetic.
local bytes = require("game.data.bytes")
local kpf = require("game.data.kpf")

local u16, u32 = bytes.u16, bytes.u32

local M = {}

M.KIND = "SNDS"
M.SCHEMA = 1
M.RECORD = 22
M.PRESENT = 1

-- Returns { count=, present = {}, sample_rate = {}, avg_bytes_per_sec = {}, channels = {},
-- bits = {}, data_length = {} } indexed by sfx id (0-based).
function M.parse(data, name, expect)
	local c = kpf.parse(data, name)
	expect = expect or {}
	c:check({ kind = M.KIND, schema_version = M.SCHEMA, profile_id = expect.profile_id,
		source_hash = expect.source_hash })
	local s = c:section("SFX ")
	local out = { count = u16(s, 0), present = {}, channels = {}, sample_rate = {}, avg_bytes_per_sec = {},
		bits = {}, data_length = {} }
	for id = 0, out.count - 1 do
		local a = 2 + id * M.RECORD
		out.present[id] = u16(s, a) % 2 == M.PRESENT
		out.channels[id] = u16(s, a + 4)
		out.sample_rate[id] = u32(s, a + 6)
		out.avg_bytes_per_sec[id] = u32(s, a + 10)
		out.bits[id] = u16(s, a + 16)
		out.data_length[id] = u32(s, a + 18)
	end
	return out
end

-- Cached duration of `0x49da70`: u32(bytes * 1000) / nAvgBytesPerSec, the product wrapping.
-- Lua numbers are doubles, so the product (< 2^53) is exact before the u32 wrap.
function M.duration_ms(s, id)
	local rate = s.avg_bytes_per_sec[id]
	if not s.present[id] or rate == 0 then return 0 end
	return math.floor(((s.data_length[id] * 1000) % 4294967296) / rate)
end

-- Whole-second duration of `0x446740`.
function M.whole_seconds_ms(s, id)
	return math.floor(M.duration_ms(s, id) / 1000) * 1000
end

return M
