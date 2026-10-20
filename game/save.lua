-- Game saves (plan phase 8 item 4): one Koprulu container (game/data/kpf.lua, kind SAVE) with sections per part:
--   CORE  the native game's save at a step boundary (kp_core save format version 1: the game, its world, the
--         trigger and AI state, the generators; it names the mission and the data identity it belongs to);
--   CMDS  the commands the manager has queued for the next step (game/simulation/manager.lua), u32 count, then per
--         command u8 player, u16 size and its bytes;
--   CAMP  the campaign (game/campaign.lua): u8 present; then u8 expansion, u8 table (0: no cached row), u16 row
--         index, u16 mission, u8 size and the pending next scenario's name, and the profile's six u32 (the original's
--         Zerg, Terran, Protoss, then the expansion's);
--   PRES  the presentation's state the game's continuation needs (the interface's ends of the trigger waits: an
--         unfinished transmission's portrait time, a camera pan), as name=integer lines.
-- The header carries the data's profile id and data-set hash (those of dat.kpd), which a load checks first; the
-- native load then checks the mission and data identity and restores the game only when the whole save is sound.
local bytes = require("game.data.bytes")
local kpf = require("game.data.kpf")

local u8, u16, u32, le16, le32 = bytes.u8, bytes.u16, bytes.u32, bytes.le16, bytes.le32

local M = {}

M.KIND = "SAVE"
M.SCHEMA = 1
local PROFILE_FIELDS = { { "original", 0 }, { "original", 1 }, { "original", 2 }, { "expansion", 0 },
	{ "expansion", 1 }, { "expansion", 2 } }

-- The data set a save belongs to: { profile_id =, source_hash = } of the game's dat.kpd.
function M.identity(dat)
	local c = kpf.parse(dat, "dat.kpd")
	return { profile_id = c.profile_id, source_hash = c.source_hash }
end

local function encode_commands(list)
	local out = { le32(#list) }
	for _, c in ipairs(list) do out[#out + 1] = string.char(c.player) .. le16(#c.bytes) .. c.bytes end
	return table.concat(out)
end

local function decode_commands(s)
	local list, at = {}, 4
	for k = 1, u32(s, 0) do
		local size = u16(s, at + 1)
		list[k] = { player = u8(s, at), bytes = s:sub(at + 4, at + 3 + size) }
		at = at + 3 + size
	end
	return list
end

local function encode_campaign(c)
	if not c then return string.char(0) end
	local run, row = c.run, c.run.row
	local out = { string.char(1, run.expansion, row and row.table or 0), le16(row and row.index or 0), le16(run.mission),
		string.char(#run.pending), run.pending }
	for _, f in ipairs(PROFILE_FIELDS) do out[#out + 1] = le32(c.profile[f[1]][f[2]] or 0) end
	return table.concat(out)
end

local function encode_presentation(t)
	local names, out = {}, {}
	for k in pairs(t or {}) do names[#names + 1] = k end
	table.sort(names)
	for _, k in ipairs(names) do out[#out + 1] = k .. "=" .. math.floor(t[k]) end
	return table.concat(out, "\n")
end

local function decode_presentation(s)
	local t = {}
	for k, v in s:gmatch("([%w_]+)=(%-?%d+)") do t[k] = tonumber(v) end
	return t
end

local function decode_campaign(s)
	if u8(s, 0) == 0 then return nil end
	local tab, n = u8(s, 2), u8(s, 7)
	local run = { expansion = u8(s, 1), row = tab ~= 0 and { table = tab, index = u16(s, 3) } or nil,
		mission = u16(s, 5), pending = s:sub(9, 8 + n) }
	local profile, at = { original = {}, expansion = {} }, 8 + n
	for k, f in ipairs(PROFILE_FIELDS) do profile[f[1]][f[2]] = u32(s, at + 4 * (k - 1)) end
	return { run = run, profile = profile }
end

-- The file operations of write_file (a test replaces them).
local FILES = { open = io.open, rename = os.rename, remove = os.remove }

-- Writes a save file without ever leaving a truncated one or losing the old one: the bytes go to path .. ".tmp" first
-- (checked, closed) and replace path by a rename only then (atomic where the platform renames onto an existing file;
-- where it refuses, the old file is removed and the rename tried again). When the rename still fails, the new save
-- stays in the .tmp file. Returns true, or nil and the reason. fs: io.open, os.rename and os.remove by default.
function M.write_file(path, data, fs)
	fs = fs or FILES
	local tmp = path .. ".tmp"
	local f, err = fs.open(tmp, "wb")
	if not f then return nil, err end
	local ok, werr = f:write(data)
	local closed, cerr = f:close()
	if not (ok and closed) then
		fs.remove(tmp)
		return nil, werr or cerr or "write failed"
	end
	if fs.rename(tmp, path) then return true end
	fs.remove(path)
	local renamed, rerr = fs.rename(tmp, path)
	if not renamed then return nil, ("%s (the new save stays in %s)"):format(tostring(rerr), tmp) end
	return true
end

-- The save of a manager's game at its step boundary with its queued commands, the campaign ({ run =, profile = } or
-- nil) and the presentation's state (a table of integers by name, or nil); identity: M.identity of the game's data.
function M.encode(manager, identity, campaign, presentation)
	return kpf.build({ kind = M.KIND, schema_version = M.SCHEMA, profile_id = identity.profile_id,
		source_hash = identity.source_hash, sections = {
			{ fourcc = "CORE", payload = manager:save() },
			{ fourcc = "CMDS", payload = encode_commands(manager.pending) },
			{ fourcc = "CAMP", payload = encode_campaign(campaign) },
			{ fourcc = "PRES", payload = encode_presentation(presentation) },
		} })
end

-- The parts of a save after checking its container and data set: { core =, commands =, campaign =, presentation = }.
-- Raises on a damaged save or one of other data.
function M.decode(save, identity)
	local c = kpf.parse(save, "save", { verify_crc = true })
	c:check({ kind = M.KIND, schema_version = M.SCHEMA, profile_id = identity.profile_id,
		source_hash = identity.source_hash })
	return { core = c:section("CORE"), commands = decode_commands(c:section("CMDS")),
		campaign = decode_campaign(c:section("CAMP")), presentation = decode_presentation(c:section("PRES")) }
end

-- Restores a save into a manager's game (Manager:load) with its queued commands; returns the campaign and the
-- presentation's state it carried.
function M.load(manager, save, identity)
	local parts = M.decode(save, identity)
	manager:load(parts.core, parts.commands)
	return parts.campaign, parts.presentation
end

return M
