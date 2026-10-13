-- String tables in the native TBL/STR layout: u16 count, u16 offsets, byte blob.
-- Lookup follows GetTblString 0x4c13b0 (map strings use it through GetMapString 0x445670):
-- only the low 16 bits of the id count; id 0 -> nil, id past the count -> "", otherwise
-- the bytes up to the first NUL.
-- Bytes are returned unchanged (control codes and legacy encoding included).
local bytes = require("game.data.bytes")

local M = {}

function M.count(blob)
	return bytes.u16(blob, 0)
end

function M.get(blob, id)
	id = id % 65536
	if id == 0 then return nil end
	if id - 1 >= bytes.u16(blob, 0) then return "" end
	local offset = bytes.u16(blob, 2 * id)
	local nul = blob:find("\0", offset + 1, true)
	if not nul then error(("strings: entry %d is not NUL-terminated"):format(id), 2) end
	return blob:sub(offset + 1, nul - 1)
end

-- A string's text without the original's control codes (the colour and format bytes 0x01..0x1f; a line break
-- becomes a space): for a plain text display.
function M.plain(s)
	return (s:gsub("\n", " "):gsub("[%z\1-\31\127]", ""))
end

-- 0-based byte offset of entry `id` inside the blob, or nil for id 0 / past the count.
-- For consumers that read past the first NUL (stat_txt has 274 such blocks).
function M.offset(blob, id)
	id = id % 65536
	if id == 0 or id - 1 >= bytes.u16(blob, 0) then return nil end
	return bytes.u16(blob, 2 * id)
end

return M
