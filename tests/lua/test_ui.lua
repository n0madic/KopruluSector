-- Reads ui.kpd written by converter/kpf/fixtures.py (the synthetic dialog of test_ui.py).
local ui = require("game.data.ui")

local DIR = "tests/fixtures/generated/kpf/"

local S = {}

function S.dialog(T)
	local u = ui.parse(T.read(DIR .. "ui.kpd"), "ui.kpd")
	local d = u.dialogs["rez\\x.bin"]
	T.eq(#d.widgets, 3)
	local root, button, html = d.widgets[1], d.widgets[2], d.widgets[3]
	T.eq(root.parent, nil)
	T.eq(root.type, 0)
	T.eq(button.parent, 1)
	T.eq(button.id, 7)
	T.eq(button.text, "quit")
	T.eq(button.flags, 0x200)
	T.eq(html.type, 14)
	T.eq(html.x1, 100)
	T.eq(html.extra[1], 101)
	T.eq(html.extra[4], 54)
	T.eq(html.first_smk, 1)
	T.eq(d.smks[1].filename, "port.smk")
	T.eq(d.smks[1].offset_y, 6)
end

function S.themes_and_grps(T)
	local u = ui.parse(T.read(DIR .. "ui.kpd"), "ui.kpd")
	T.eq(#u.theme_fonts, 12 * 48)
	T.eq(u.theme_effects.size, 2)
	T.eq(#u.grps["a.grp"], 14)
end

return S
