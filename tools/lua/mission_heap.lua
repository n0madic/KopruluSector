-- Lua heap after loading a mission with the game data it needs (plan §10, budget from
-- spike 10: peak <= 2 x mission file size, resident ~1 x plus unpacked DAT columns).
-- Run from the repository root:
--   luajit -joff tools/lua/mission_heap.lua generated/missions/expterran/terran07.kpm
--   tools/toolchain/lua5.1 tools/lua/mission_heap.lua <kpm>
package.path = "./?.lua;" .. package.path
if not bit then
	local ok, lib = pcall(require, "bit")
	bit = ok and lib or dofile("tests/lua/bitshim.lua")
	package.loaded.bit = bit
end

local mission = require("game.data.mission")
local terrain = require("game.data.terrain")
local dat = require("game.data.dat")
local scripts = require("game.data.scripts")
local strings_kpd = require("game.data.kpf")

local function read(path)
	local f = assert(io.open(path, "rb"))
	local data = f:read("*a")
	f:close()
	return data
end

local TILESETS = { [0] = "badlands", "platform", "install", "ashworld", "jungle", "desert", "ice", "twilight" }

-- LuaJIT keeps io.read's temporary buffer and halves it at each full collection, so several
-- cycles are needed before the count reflects only live data (Defold loads via sys.load_buffer).
local function kb()
	for _ = 1, 24 do collectgarbage() end
	return collectgarbage("count")
end

local path = assert(arg[1], "usage: mission_heap.lua <mission.kpm>")
local base = kb()
local peak = base
local function sample()
	peak = math.max(peak, collectgarbage("count"))
end

local kpm = read(path)
sample()
local m = mission.parse(kpm, path)
sample()
local ts = terrain.parse_tileset(read("generated/data/tilesets/" .. TILESETS[m.tileset] .. ".kpd"), "tileset")
sample()
local ids = terrain.import(m, ts)
sample()
local before_dat = kb()
local tables = dat.parse(read("generated/data/dat.kpd"), "dat.kpd")
local dat_soa = kb() - before_dat
sample()
local isc = scripts.parse_iscript(read("generated/data/iscript.kpd"), "iscript.kpd")
local ai = scripts.parse_aiscript(read("generated/data/aiscript.kpd"), "aiscript.kpd")
local text = strings_kpd.parse(read("generated/data/strings.kpd"), "strings.kpd")
sample()
local files = 0
for _, p in ipairs({ path, "generated/data/tilesets/" .. TILESETS[m.tileset] .. ".kpd", "generated/data/iscript.kpd",
	"generated/data/aiscript.kpd", "generated/data/strings.kpd" }) do
	files = files + #read(p) / 1024
end
kpm = nil -- the container string is released after extraction (spike 10)
ids = nil -- the import result is a transient for the tilemap fill
local resident = kb() - base
local backend = jit and (jit.version .. (jit.status() and " JIT on" or " JIT off")) or _VERSION
-- Budget (spike 10): resident ~1 x the loaded files plus the unpacked DAT columns; 10% slack.
local budget = (files + dat_soa) * 1.1
print(("%s %s: files %.0f KB, DAT SoA %.0f KB, resident %.0f KB (budget %.0f KB), sampled peak %.0f KB")
	:format(backend, path, files, dat_soa, resident, budget, peak - base))
if resident > budget then
	error(("resident heap %.0f KB exceeds the budget %.0f KB"):format(resident, budget), 0)
end
assert(m and ts and tables and isc and ai and text)
