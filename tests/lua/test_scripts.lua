-- Reads iscript.kpd and aiscript.kpd written by converter/kpf/fixtures.py (run it first).
local scripts = require("game.data.scripts")

local DIR = "tests/fixtures/generated/kpf/"

local S = {}

function S.iscript_headers(T)
	local isc = scripts.parse_iscript(T.read(DIR .. "iscript.kpd"), "iscript.kpd")
	T.eq(isc.headers[5], 4) -- first match wins: (5, header 0) precedes (5, header 1)
	T.eq(isc.headers[3], 16)
	T.eq(isc.code:sub(isc.headers[3] + 1, isc.headers[3] + 4), "SCPE")
	T.eq(isc.slots[0], 2)
	T.eq(isc.slots[12], 14)
	T.eq(isc.slots[1], nil)
end

function S.ai_resolution(T)
	local ai = scripts.parse_aiscript(T.read(DIR .. "aiscript.kpd"), "aiscript.kpd")
	local cases = {
		{ "Ter5", 16, scripts.BIN_AISCRIPT },
		{ "Redi", 24, scripts.BIN_BWSCRIPT },
		{ "Both", 20, scripts.BIN_AISCRIPT },
		{ "BwOn", 28, scripts.BIN_BWSCRIPT },
		{ "Zero", nil, nil },
		{ "None", nil, nil },
	}
	for _, c in ipairs(cases) do
		local offset, bin = scripts.resolve_ai(ai, c[1])
		T.eq(offset, c[2], c[1] .. " offset")
		T.eq(bin, c[3], c[1] .. " bin")
	end
end

function S.kinds(T)
	T.raises(function() scripts.parse_iscript(T.read(DIR .. "aiscript.kpd"), "aiscript.kpd") end, "file kind")
end

return S
