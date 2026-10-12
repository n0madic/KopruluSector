-- Slot allocation of the world image backend (pure Lua). Each family owns a fixed range of
-- slots in the shared image-data texture; an image key keeps its slot while it is written
-- on every tick, and slots not written during a tick are freed for reuse.
local M = {}

local Slots = {}
Slots.__index = Slots

-- families: array of { name =, capacity = }; ranges follow in order from slot 0.
function M.new(families)
	local self = setmetatable({ families = {}, total = 0, tick = 0, seen = {} }, Slots)
	for k, f in ipairs(families) do
		local entry = { name = f.name, base = self.total, capacity = f.capacity, slot_of = {}, free = {}, next_slot = 0 }
		self.families[k], self.families[f.name] = entry, entry
		self.total = self.total + f.capacity
	end
	return self
end

function Slots:begin()
	self.tick = self.tick + 1
end

-- Global slot of `key` in `family` for this tick (allocated when new).
function Slots:slot(family, key)
	local f = self.families[family]
	local slot = f.slot_of[key]
	if not slot then
		local n = #f.free
		if n > 0 then
			slot, f.free[n] = f.free[n], nil
		else
			assert(f.next_slot < f.capacity, "slots: family " .. family .. " is full")
			slot = f.next_slot
			f.next_slot = slot + 1
		end
		f.slot_of[key] = slot
	end
	local global = f.base + slot
	self.seen[global] = self.tick
	return global
end

-- Frees the slots not used this tick; release(global slot) is called for each.
function Slots:finish(release)
	for _, f in ipairs(self.families) do
		for key, slot in pairs(f.slot_of) do
			if self.seen[f.base + slot] ~= self.tick then
				f.slot_of[key] = nil
				f.free[#f.free + 1] = slot
				release(f.base + slot)
			end
		end
	end
end

return M
