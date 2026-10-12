-- presentation/backend/slots.lua: stable slots per key, release and reuse, family ranges.
local slots = require("presentation.backend.slots")

local S = {}

function S.stable_released_and_reused(T)
	local sl = slots.new({ { name = "a", capacity = 3 }, { name = "b", capacity = 2 } })
	T.eq(sl.total, 5)
	sl:begin()
	local a1, a2, b1 = sl:slot("a", 10), sl:slot("a", 11), sl:slot("b", 10)
	T.eq(a1, 0); T.eq(a2, 1); T.eq(b1, 3)
	local released = {}
	sl:finish(function(slot) released[#released + 1] = slot end)
	T.eq(#released, 0)
	sl:begin()
	T.eq(sl:slot("a", 11), a2) -- kept while written every tick
	sl:finish(function(slot) released[#released + 1] = slot end)
	T.eq(#released, 2) -- key 10 in both families
	sl:begin()
	local reused = sl:slot("a", 12)
	T.eq(reused, a1)
	T.raises(function()
		sl:slot("b", 1)
		sl:slot("b", 2)
		sl:slot("b", 3)
	end, "full")
end

return S
