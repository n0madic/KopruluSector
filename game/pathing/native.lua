-- The native local path search (native/kp_path, ADR 0007), found by game/native.lua. Without
-- the module the Lua code runs. KP_PATH in the environment chooses: "lua" never uses the
-- module, "check" runs both and compares every result (game/pathing/local_search.lua).
local native = require("game.native")

local M = {}

M.mode = os and os.getenv and os.getenv("KP_PATH") or nil
M.kp_path = M.mode ~= "lua" and native.find("kp_path") or nil

return M
