-- Loads Koprulu containers inside Defold (plan §4.1). Large files use
-- sys.load_buffer_async so loading does not block a frame; small files and
-- headless tools use the synchronous path. Whether sections stay strings or
-- become buffer views is decided by the phase-0 data-access spike; for now the
-- buffer is copied into one string and parsed by game.data.kpf.
local kpf = require("game.data.kpf")

local M = {}

local DATA_STREAM = "data"

local function to_string(buf)
	return buffer.get_bytes(buf, DATA_STREAM)
end

-- Returns the whole resource as a string or raises a "kpf: ..." error. The format
-- readers (game.data.mission, dat, terrain, ...) take this string.
function M.read(path)
	local ok, buf = pcall(sys.load_buffer, path)
	if not ok or not buf then
		error("kpf: " .. path .. ": cannot load (" .. tostring(buf) .. ")", 0)
	end
	return to_string(buf)
end

-- Returns a parsed container or raises a "kpf: ..." error.
function M.load(path, opts)
	local ok, buf = pcall(sys.load_buffer, path)
	if not ok or not buf then
		error("kpf: " .. path .. ": cannot load (" .. tostring(buf) .. ")", 0)
	end
	return kpf.parse(to_string(buf), path, opts)
end

-- callback(self, container_or_nil, err_or_nil). Returns the request id.
function M.load_async(path, callback, opts)
	return sys.load_buffer_async(path, function(self, _, result)
		if result.status ~= sys.REQUEST_STATUS_FINISHED then
			callback(self, nil, "kpf: " .. path .. ": cannot load (status " .. tostring(result.status) .. ")")
			return
		end
		local ok, c = pcall(kpf.parse, to_string(result.buffer), path, opts)
		if ok then
			callback(self, c, nil)
		else
			callback(self, nil, c)
		end
	end)
end

return M
