-- Phase-0 spike 10: cost of reading packed data from Lua.
-- Runs on the command line (LuaJIT, LuaJIT -joff, PUC 5.1) and inside Defold, where
-- the caller also passes a buffer stream accessor.
local M = {}

local byte = string.byte

local N = 1000000 -- u16 elements read per pass (2 MB of data)

local function make_string(n)
	-- Deterministic bytes without building a huge table.
	local parts, chunk = {}, {}
	for i = 1, 4096 do chunk[i] = string.char((i * 37) % 256) end
	local block = table.concat(chunk)
	for i = 1, math.ceil(n * 2 / #block) do parts[i] = block end
	return table.concat(parts):sub(1, n * 2)
end

local function bench(clock, label, fn, out)
	collectgarbage("collect")
	local t0 = clock()
	local checksum = fn()
	local ms = (clock() - t0) * 1000
	out[#out + 1] = { label = label, ms = ms, checksum = checksum }
end

-- Sequential and strided u16 reads from a string with string.byte.
local function string_seq(s, n)
	local sum = 0
	for i = 0, n - 1 do
		local lo, hi = byte(s, i * 2 + 1, i * 2 + 2)
		sum = (sum + lo + hi * 256) % 4294967296
	end
	return sum
end

local function string_random(s, n)
	local sum, idx = 0, 1
	for _ = 1, n do
		idx = (idx * 1103515245 + 12345) % 2147483648
		local i = idx % n
		local lo, hi = byte(s, i * 2 + 1, i * 2 + 2)
		sum = (sum + lo + hi * 256) % 4294967296
	end
	return sum
end

local function soa_seq(t, n)
	local sum = 0
	for i = 1, n do sum = (sum + t[i]) % 4294967296 end
	return sum
end

local function soa_random(t, n)
	local sum, idx = 0, 1
	for _ = 1, n do
		idx = (idx * 1103515245 + 12345) % 2147483648
		sum = (sum + t[idx % n + 1]) % 4294967296
	end
	return sum
end

-- opts.clock: function returning seconds; opts.stream: optional accessor for a Defold
-- buffer stream of uint16 (1-based) holding the same values.
function M.run(opts)
	local clock, out = opts.clock, {}
	local s = make_string(N)

	local t0 = clock()
	local soa = {}
	for i = 0, N - 1 do
		local lo, hi = byte(s, i * 2 + 1, i * 2 + 2)
		soa[i + 1] = lo + hi * 256
	end
	out[#out + 1] = { label = "unpack string -> SoA table (1M u16)", ms = (clock() - t0) * 1000 }

	collectgarbage("collect")
	local before = collectgarbage("count")
	local probe = {}
	for i = 1, N do probe[i] = soa[i] end
	local table_kb = collectgarbage("count") - before
	probe = nil
	out[#out + 1] = { label = "SoA table memory for 1M numbers (KB)", value = table_kb }

	bench(clock, "string.byte u16 sequential 1M", function() return string_seq(s, N) end, out)
	bench(clock, "string.byte u16 random 1M", function() return string_random(s, N) end, out)
	bench(clock, "SoA table sequential 1M", function() return soa_seq(soa, N) end, out)
	bench(clock, "SoA table random 1M", function() return soa_random(soa, N) end, out)
	if opts.stream then
		local stream = opts.stream(s)
		bench(clock, "buffer stream sequential 1M", function() return soa_seq(stream, N) end, out)
		bench(clock, "buffer stream random 1M", function() return soa_random(stream, N) end, out)
	end

	-- All readers must agree.
	local ref
	for _, r in ipairs(out) do
		if r.checksum and r.label:find("sequential") then
			ref = ref or r.checksum
			assert(r.checksum == ref, "checksum mismatch in " .. r.label)
		end
	end
	return out
end

function M.format(results)
	local lines = {}
	for _, r in ipairs(results) do
		if r.ms then
			lines[#lines + 1] = ("%s: %.1f ms"):format(r.label, r.ms)
		else
			lines[#lines + 1] = ("%s: %.0f"):format(r.label, r.value)
		end
	end
	return lines
end

M.N = N
M.make_string = make_string
return M
