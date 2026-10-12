-- Reads containers written by converter/kpf/fixtures.py (run it first).
local kpf = require("game.data.kpf")
local bytes = require("game.data.bytes")
local crc32 = require("game.data.crc32")

local DIR = "tests/fixtures/generated/kpf/"

local function expect(T)
	local chunk = assert(loadstring or load)(T.read(DIR .. "expect.lua"))
	return chunk()
end

local function basic(T)
	return T.read(DIR .. "basic.kpd")
end

local function patched(data, offset, value)
	return data:sub(1, offset) .. string.char(value) .. data:sub(offset + 2)
end

local S = {}

function S.header(T)
	local e = expect(T)
	local data = basic(T)
	T.eq(#data, e.basic_size)
	local c = kpf.parse(data, "basic.kpd", { verify_crc = true })
	T.eq(c.kind, "TEST")
	T.eq(c.schema_version, 3)
	T.eq(c.converter_version, 7)
	T.eq(c.profile_id, "bw-1.17.0.1")
	T.eq(c.source_hash, e.source_hash)
	T.eq(#c.sections, 5)
	T.eq(c.sections[1].fourcc, "INTS")
	T.eq(c.sections[5].fourcc, "WALK")
end

function S.section_payloads(T)
	local e = expect(T)
	local c = kpf.parse(basic(T), "basic.kpd")
	local ints = c:section("INTS")
	T.eq(#ints, #e.ints * 4)
	for i, v in ipairs(e.ints) do
		T.eq(bytes.u32(ints, (i - 1) * 4), v, "INTS[" .. i .. "]")
	end
	local all = c:section("BYTE")
	T.eq(#all, 256)
	for i = 0, 255 do T.eq(bytes.u8(all, i), i) end
	T.eq(c:info("BYTE").schema_version, 2)
	T.eq(c:section("ODD "), "abc")
	T.eq(c:section("EMPT"), "")
	T.ok(c:has("WALK") and not c:has("NONE"))
end

function S.derived_metadata(T)
	local c = kpf.parse(basic(T), "basic.kpd")
	local w = c:info("WALK")
	T.eq(w.class, kpf.CLASS_DERIVED)
	T.eq(w.algorithm_id, "WLK1")
	T.eq(w.algorithm_version, 5)
	T.eq(w.flags, 0x8001)
	T.eq(#w.inputs_hash, 16)
end

function S.check_expectations(T)
	local c = kpf.parse(basic(T), "basic.kpd")
	c:check({ kind = "TEST", schema_version = 3, profile_id = "bw-1.17.0.1",
		sections = { WALK = { algorithm_id = "WLK1", algorithm_version = 5 }, BYTE = { schema_version = 2 } } })
	T.raises(function() c:check({ schema_version = 4 }) end, "regenerate")
	T.raises(function() c:check({ profile_id = "bw-1.16.1" }) end, "profile")
	T.raises(function() c:check({ source_hash = string.rep("\0", 32) }) end, "different data set")
	T.raises(function() c:check({ sections = { WALK = { algorithm_id = "WLK1", algorithm_version = 6 } } }) end,
		"WLK1 v5")
	T.raises(function() c:check({ sections = { INTS = { algorithm_id = "X", algorithm_version = 1 } } }) end,
		"not derived")
	T.raises(function() c:section("NONE") end, "missing section NONE")
end

function S.rejects_bad_files(T)
	local data = basic(T)
	T.raises(function() kpf.parse("short", "x") end, "shorter")
	T.raises(function() kpf.parse("XXXX" .. data:sub(5), "x") end, "bad magic")
	T.raises(function() kpf.parse(data:sub(1, -5), "x") end, "truncated")
	T.raises(function() kpf.parse(patched(data, 8, 2), "x") end, "version 2")
	-- Flip the last payload byte of WALK: only detected with verify_crc.
	local c = kpf.parse(data, "x")
	local w = c:info("WALK")
	local pos = w.offset + w.size - 1
	local corrupt = patched(data, pos, (bytes.u8(data, pos) + 1) % 256)
	kpf.parse(corrupt, "x")
	T.raises(function() kpf.parse(corrupt, "x", { verify_crc = true }) end, "checksum")
end

function S.large_file(T)
	local e = expect(T)
	local data = T.read(DIR .. "large.kpd")
	T.eq(#data, e.large_size)
	local c = kpf.parse(data, "large.kpd")
	local blob = c:section("BLOB")
	T.eq(#blob, e.large_payload_size)
	T.eq(blob:sub(-16), e.large_tail)
	T.eq(c:info("BLOB").crc32, e.large_crc32)
end

function S.crc32_known(T)
	T.eq(crc32.of(""), 0)
	T.eq(crc32.of("123456789"), 0xCBF43926)
	T.eq(crc32.of("xx123456789yy", 3, 11), 0xCBF43926)
end

return S
