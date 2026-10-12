-- String tables in the native TBL/STR layout: u16 count, u16 offsets, byte blob.
-- Lookup follows GetTblString 0x4c13b0 (map strings use it through GetMapString 0x445670):
-- id 0 -> nil, id past the count -> "", otherwise the bytes up to the first NUL.
-- Bytes are returned unchanged (control codes and legacy encoding included).
local bytes = require("game.data.bytes")

local M = {}

function M.count(blob)
	return bytes.u16(blob, 0)
end

function M.get(blob, id)
	if id == 0 then return nil end
	if id - 1 >= bytes.u16(blob, 0) then return "" end
	local offset = bytes.u16(blob, 2 * id)
	local nul = blob:find("\0", offset + 1, true)
	if not nul then error(("strings: entry %d is not NUL-terminated"):format(id), 2) end
	return blob:sub(offset + 1, nul - 1)
end

return M
