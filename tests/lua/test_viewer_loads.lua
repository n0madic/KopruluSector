-- viewer/loads.lua: proxy and pack load state across mission switches with loads in flight.
local loads = require("viewer.loads")

local S = {}

local function recorder()
	local log = {}
	local function op(kind)
		return function(name) log[#log + 1] = kind .. " " .. name end
	end
	return log, { load = op("load"), unload = op("unload"), enable = op("enable") }
end

function S.proxy_switch_while_loading(T)
	local log, ops = recorder()
	local p = loads.proxies(ops)
	T.eq(p:want("badlands"), false)
	-- The next mission picks another tileset before badlands has loaded.
	T.eq(p:want("jungle"), false)
	T.eq(table.concat(log, ","), "load badlands,load jungle")
	-- badlands arrives first: it is not wanted, so it is unloaded, not enabled.
	T.eq(p:on_loaded("badlands"), false)
	T.eq(log[#log], "unload badlands")
	T.eq(p:loaded("jungle"), false)
	T.eq(p:on_loaded("jungle"), true)
	T.eq(log[#log], "enable jungle")
	T.ok(p:loaded("jungle"))
	p:on_unloaded("badlands")
	T.eq(#log, 4)
end

function S.proxy_back_to_an_unloading_tileset(T)
	local log, ops = recorder()
	local p = loads.proxies(ops)
	p:want("badlands")
	p:on_loaded("badlands")
	p:want("jungle") -- unloads badlands, loads jungle
	-- Back to badlands while it is still unloading: it is not filled, and reloads after.
	T.eq(p:want("badlands"), false)
	T.eq(table.concat(log, ","), "load badlands,enable badlands,unload badlands,load jungle")
	p:on_unloaded("badlands")
	T.eq(log[#log], "load badlands")
	T.eq(p:on_loaded("jungle"), false)
	T.eq(log[#log], "unload jungle")
	T.eq(p:on_loaded("badlands"), true)
	T.ok(p:loaded("badlands") and not p:loaded("jungle"))
end

local function pack_ops()
	local log, pending, ready = {}, {}, {}
	return log, pending, ready, {
		load = function(name, done)
			log[#log + 1] = "load " .. name
			pending[name] = done
		end,
		unload = function(name) log[#log + 1] = "unload " .. name end,
		ready = function(failed) ready[#ready + 1] = failed end,
	}
end

function S.packs_ready_once_all_loaded(T)
	local log, pending, ready, ops = pack_ops()
	local p = loads.packs(ops)
	p:want({ "terran", "effects" })
	T.eq(#ready, 0)
	pending.terran(true)
	T.eq(#ready, 0)
	pending.effects(true)
	T.eq(#ready, 1)
	T.ok(p:loaded("terran") and p:loaded("effects"))
	-- The next mission keeps effects and swaps terran for zerg.
	p:want({ "zerg", "effects" })
	T.eq(log[#log - 1], "unload terran")
	T.eq(log[#log], "load zerg")
	pending.zerg(false)
	T.eq(#ready, 2)
	T.ok(ready[2].zerg)
end

function S.stale_pack_load_does_not_build_the_next_mission(T)
	local log, pending, ready, ops = pack_ops()
	local p = loads.packs(ops)
	p:want({ "terran", "effects" })
	pending.effects(true)
	-- Switch mission before terran has loaded; the new mission has not asked for packs yet.
	p:cancel()
	pending.terran(true)
	T.eq(#ready, 0)
	T.eq(log[#log], "unload terran")
	T.ok(not p:loaded("terran"))
	-- A load still in flight for a pack the next mission wants is waited for, not repeated.
	p:want({ "protoss", "effects" })
	local loads_of_protoss = 0
	for _, line in ipairs(log) do
		if line == "load protoss" then loads_of_protoss = loads_of_protoss + 1 end
	end
	T.eq(loads_of_protoss, 1)
	p:cancel()
	p:want({ "protoss", "effects" })
	T.eq(#ready, 0)
	pending.protoss(true)
	T.eq(#ready, 1)
	T.eq(log[#log], "load protoss")
end

return S
