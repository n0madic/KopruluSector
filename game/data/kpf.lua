-- Reader for the Koprulu container (.kpd/.kpm). Layout: docs/formats/container.md.
-- Writer: converter/kpf/container.py. Works on a Lua string holding the whole file.
local bytes = require("game.data.bytes")
local crc32 = require("game.data.crc32")

local u16, u32, cstr = bytes.u16, bytes.u32, bytes.cstr

local M = {}

M.MAGIC = "KPRF"
M.CONTAINER_VERSION = 1
M.HEADER_SIZE = 128
M.ENTRY_SIZE = 48
M.CLASS_CANONICAL = 0
M.CLASS_DERIVED = 1

local REGENERATE = "regenerate the data with the converter"

local function fail(name, msg)
	error("kpf: " .. (name or "<data>") .. ": " .. msg, 0)
end

local Container = {}
Container.__index = Container

-- Parses `data`; `name` is used only in error messages.
-- opts.verify_crc checks every section checksum (costly for large files).
function M.parse(data, name, opts)
	opts = opts or {}
	if type(data) ~= "string" then fail(name, "expected a string, got " .. type(data)) end
	if #data < M.HEADER_SIZE then fail(name, "file shorter than the container header") end
	if data:sub(1, 4) ~= M.MAGIC then fail(name, "not a Koprulu container (bad magic)") end
	local version, header_size, entry_size = u16(data, 8), u16(data, 10), u16(data, 12)
	if version ~= M.CONTAINER_VERSION or header_size ~= M.HEADER_SIZE or entry_size ~= M.ENTRY_SIZE then
		fail(name, ("container version %d is not supported by this build (expects %d); %s")
			:format(version, M.CONTAINER_VERSION, REGENERATE))
	end
	local count = u16(data, 14)
	local table_offset, file_size = u32(data, 104), u32(data, 108)
	if file_size ~= #data then
		fail(name, ("file is %d bytes, header says %d (truncated or corrupt)"):format(#data, file_size))
	end
	if table_offset + count * M.ENTRY_SIZE > #data then fail(name, "section table out of bounds") end

	local c = setmetatable({
		name = name,
		data = data,
		kind = data:sub(5, 8),
		schema_version = u32(data, 16),
		converter_version = u32(data, 20),
		profile_id = cstr(data, 24, 16),
		source_hash = data:sub(41, 72),
		provenance_hash = data:sub(73, 104),
		sections = {},
		by_name = {},
	}, Container)

	for i = 0, count - 1 do
		local e = table_offset + i * M.ENTRY_SIZE
		local s = {
			fourcc = data:sub(e + 1, e + 4),
			class = bytes.u8(data, e + 4),
			flags = u16(data, e + 6),
			offset = u32(data, e + 8),
			size = u32(data, e + 12),
			schema_version = u32(data, e + 16),
			crc32 = u32(data, e + 20),
			algorithm_id = data:sub(e + 25, e + 28),
			algorithm_version = u32(data, e + 28),
			inputs_hash = data:sub(e + 33, e + 48),
		}
		if s.offset % 4 ~= 0 or s.offset + s.size > #data then
			fail(name, "section " .. s.fourcc .. " out of bounds or misaligned")
		end
		if c.by_name[s.fourcc] then fail(name, "duplicate section " .. s.fourcc) end
		if opts.verify_crc and crc32.of(data, s.offset + 1, s.offset + s.size) ~= s.crc32 then
			fail(name, "section " .. s.fourcc .. " checksum mismatch (corrupt file)")
		end
		c.sections[#c.sections + 1] = s
		c.by_name[s.fourcc] = s
	end
	return c
end

function Container:has(fourcc)
	return self.by_name[fourcc] ~= nil
end

function Container:info(fourcc)
	local s = self.by_name[fourcc]
	if not s then fail(self.name, "missing section " .. fourcc .. "; " .. REGENERATE) end
	return s
end

-- Section payload as a new string.
function Container:section(fourcc)
	local s = self:info(fourcc)
	return self.data:sub(s.offset + 1, s.offset + s.size)
end

-- Verifies that the file matches what the running code expects.
-- expect = { kind=, schema_version=, profile_id=, source_hash=,
--            sections = { FOURCC = { schema_version=, algorithm_id=, algorithm_version= } } }
function Container:check(expect)
	local name = self.name
	if expect.kind and self.kind ~= expect.kind then
		fail(name, ("file kind %q, expected %q"):format(self.kind, expect.kind))
	end
	if expect.schema_version and self.schema_version ~= expect.schema_version then
		fail(name, ("schema version %d, this build expects %d; %s")
			:format(self.schema_version, expect.schema_version, REGENERATE))
	end
	if expect.profile_id and self.profile_id ~= expect.profile_id then
		fail(name, ("data profile %q, this build expects %q"):format(self.profile_id, expect.profile_id))
	end
	if expect.source_hash and self.source_hash ~= expect.source_hash then
		fail(name, "built from a different data set than the other loaded files; " .. REGENERATE)
	end
	for fourcc, want in pairs(expect.sections or {}) do
		local s = self:info(fourcc)
		if want.schema_version and s.schema_version ~= want.schema_version then
			fail(name, ("section %s schema version %d, expected %d; %s")
				:format(fourcc, s.schema_version, want.schema_version, REGENERATE))
		end
		if want.algorithm_id then
			if s.class ~= M.CLASS_DERIVED then fail(name, "section " .. fourcc .. " is not derived") end
			if s.algorithm_id ~= want.algorithm_id or s.algorithm_version ~= want.algorithm_version then
				fail(name, ("derived section %s was built by %s v%d, this build expects %s v%d; %s")
					:format(fourcc, s.algorithm_id, s.algorithm_version, want.algorithm_id,
						want.algorithm_version, REGENERATE))
			end
		end
	end
	return self
end

-- The container of `spec` as bytes, laid out as converter/kpf/container.py writes it: spec = { kind =, schema_version =,
-- converter_version = (0 by default), profile_id =, source_hash = (32 bytes, zeros by default), sections =
-- { { fourcc =, payload =, schema_version = (1) }, ... } } (canonical sections). For the files the game writes itself
-- (game/save).
function M.build(spec)
	local le16, le32 = bytes.le16, bytes.le32
	local function fixed(s, size)
		assert(#s <= size, "kpf: a header field of " .. #s .. " bytes does not fit " .. size)
		return s .. string.rep("\0", size - #s)
	end
	local sections, cursor = spec.sections, M.HEADER_SIZE + M.ENTRY_SIZE * #spec.sections
	local offsets = {}
	for k, s in ipairs(sections) do
		cursor = cursor + (-cursor) % 4
		offsets[k] = cursor
		cursor = cursor + #s.payload
	end
	local size = cursor + (-cursor) % 4
	local out = { M.MAGIC, spec.kind, le16(M.CONTAINER_VERSION), le16(M.HEADER_SIZE), le16(M.ENTRY_SIZE), le16(#sections),
		le32(spec.schema_version), le32(spec.converter_version or 0), fixed(spec.profile_id, 16),
		fixed(spec.source_hash or "", 32), string.rep("\0", 32), le32(M.HEADER_SIZE), le32(size), string.rep("\0", 16) }
	for k, s in ipairs(sections) do
		out[#out + 1] = s.fourcc .. string.char(M.CLASS_CANONICAL, 0) .. le16(0) .. le32(offsets[k]) .. le32(#s.payload)
			.. le32(s.schema_version or 1) .. le32(crc32.of(s.payload)) .. string.rep("\0", 24)
	end
	local at = M.HEADER_SIZE + M.ENTRY_SIZE * #sections
	for k, s in ipairs(sections) do
		out[#out + 1] = string.rep("\0", offsets[k] - at) .. s.payload
		at = offsets[k] + #s.payload
	end
	out[#out + 1] = string.rep("\0", size - at)
	return table.concat(out)
end

return M
