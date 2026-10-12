-- Load state of the viewer's tileset template proxies and resource packs (ADR 0004). Every
-- request is tracked per name, so a mission switch while loads are in flight never mistakes
-- one proxy or pack for another, and a load that finishes for a mission no longer shown is
-- unloaded instead of being kept or building that mission's scene.
local M = {}

-- Tileset template proxies: one wanted proxy, every other one is unloaded. A proxy's state
-- is nil (unloaded), "loading", "loaded" (and enabled) or "unloading".
-- ops: load(name), unload(name), enable(name).
local Proxies = {}
Proxies.__index = Proxies

function M.proxies(ops)
	return setmetatable({ ops = ops, state = {} }, Proxies)
end

function Proxies:step()
	for name, state in pairs(self.state) do
		if name ~= self.wanted and state == "loaded" then
			self.state[name] = "unloading"
			self.ops.unload(name)
		end
	end
	local wanted = self.wanted
	if wanted and self.state[wanted] == nil then
		self.state[wanted] = "loading"
		self.ops.load(wanted)
	end
end

-- Makes `name` the wanted proxy. Returns true when it is loaded already.
function Proxies:want(name)
	self.wanted = name
	self:step()
	return self:loaded(name)
end

function Proxies:loaded(name)
	return self.state[name] == "loaded"
end

-- proxy_loaded from proxy `name`. Returns true when it is the wanted one (now enabled).
function Proxies:on_loaded(name)
	self.state[name] = "loaded"
	if name == self.wanted then
		self.ops.enable(name)
		return true
	end
	self:step()
	return false
end

-- proxy_unloaded from proxy `name`.
function Proxies:on_unloaded(name)
	self.state[name] = nil
	self:step()
end

-- Resource packs: the wanted set of one mission. ops: load(name, done(ok)), unload(name),
-- ready(failed) once every wanted pack has loaded or failed (failed: { [name] = true }).
local Packs = {}
Packs.__index = Packs

function M.packs(ops)
	return setmetatable({ ops = ops, state = {} }, Packs)
end

function Packs:check()
	if not self.wanted or self.notified then return end
	for name in pairs(self.wanted) do
		if self.state[name] ~= "loaded" and not self.failed[name] then return end
	end
	self.notified = true
	self.ops.ready(self.failed)
end

function Packs:finished(name, ok)
	local wanted = self.wanted and self.wanted[name]
	if not ok then
		self.state[name] = nil
		if wanted then self.failed[name] = true end
	elseif wanted then
		self.state[name] = "loaded"
	else
		-- Loaded for a mission that is no longer shown.
		self.state[name] = nil
		self.ops.unload(name)
	end
	self:check()
end

-- names: the mission's PACK list. Unloads loaded packs it does not name and loads the others.
function Packs:want(names)
	local wanted = {}
	for _, name in ipairs(names) do wanted[name] = true end
	self.wanted, self.failed, self.notified = wanted, {}, false
	for name, state in pairs(self.state) do
		if not wanted[name] and state == "loaded" then
			self.state[name] = nil
			self.ops.unload(name)
		end
	end
	for name in pairs(wanted) do
		if self.state[name] == nil then
			self.state[name] = "loading"
			self.ops.load(name, function(ok) self:finished(name, ok) end)
		end
	end
	self:check()
end

-- The shown mission changed: no ready call until the next want.
function Packs:cancel()
	self.wanted = nil
end

function Packs:loaded(name)
	return self.state[name] == "loaded"
end

return M
