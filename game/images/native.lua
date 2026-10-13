-- game.images.world over the native core (stage N.1 of ADR 0008): a module with the API of
-- game/images/world.lua whose worlds are kp_core.images worlds (native/kp_core/src/lua/
-- kp_images_lua.c). The C world copies its data from the Lua tables of opts.dat, opts.grp and
-- opts.iscript (one copy per set of tables, shared by the worlds over it, as the Lua tables are);
-- Lua code keeps reading those tables through w.dat, w.grp and w.script.
-- Until the unit code moves to C, the tests run the Lua unit code over this world.
local core = require("game.images.core")

local M = {}

local EMPTY = {}

-- kp: the kp_core module.
function M.module(kp)
	local copies = setmetatable({}, { __mode = "k" }) -- [dat][grp][iscript] -> C data
	local mod = setmetatable({}, { __index = core })
	function mod.new(opts)
		local dat, grp, script = opts.dat or EMPTY, opts.grp or EMPTY, opts.iscript or EMPTY
		local by_grp = copies[dat] or setmetatable({}, { __mode = "k" })
		copies[dat] = by_grp
		local by_script = by_grp[grp] or setmetatable({}, { __mode = "k" })
		by_grp[grp] = by_script
		local data = by_script[script]
		if not data then
			data = kp.images.data(dat, grp, script)
			by_script[script] = data
		end
		return kp.images.new(data, opts)
	end
	return mod
end

-- The world module tools use: the C world when KP_IMAGES=native is set (and kp_core is
-- built), else game/images/world.lua.
function M.world_module()
	if os and os.getenv and os.getenv("KP_IMAGES") == "native" then
		local kp = require("game.native").find("kp_core") or error("KP_IMAGES=native: no kp_core module", 0)
		return M.module(kp)
	end
	return require("game.images.world")
end

return M
