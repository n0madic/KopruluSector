-- The images world of the native core (ADR 0008): kp_core.images worlds (native/kp_core/src/lua/kp_images_lua.c)
-- for the map scenes of the viewer, the animation browser and the image tests. The C world copies its data from the
-- Lua tables of opts.dat, opts.grp and opts.iscript (one copy per set of tables, shared by the worlds over it, as
-- the Lua tables are); Lua code keeps reading those tables through w.dat, w.grp and w.script. Games own their
-- worlds (kp_core.game.new, g.img).
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

-- The world module of the kp_core module, or nil, error without it.
function M.world()
	local kp = require("game.native").find("kp_core")
	if not kp then return nil, "no kp_core module (tools/native/build.sh; the native extension in Defold)" end
	return M.module(kp)
end

return M
