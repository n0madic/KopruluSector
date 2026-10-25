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
	T.eq(u.glue["glue\\x.pcx"], "abc")
end

function S.tables(T)
	local t = ui.parse(T.read(DIR .. "ui.kpd"), "ui.kpd").tables
	T.eq(#t.button_sets, 2)
	T.eq(t.button_sets[1].link, nil)
	T.eq(t.button_sets[2].link, 0)
	local b = t.button_sets[2].buttons[1]
	T.eq(b.position, 7)
	T.eq(b.condition, 0x49f850)
	T.eq(b.action, 0x47c420)
	T.eq(b.condition_value, 10)
	T.eq(b.disabled_string, 355)
	T.eq(t.status_layouts[1].draw, 0x4a4e40)
	T.eq(#t.fluff, 2)
	T.eq(t.fluff[2][2].w, 28)
	T.eq(t.cursors[2], "Illegal.grp")
	T.eq(t.edge_scroll[1], 18)
	T.eq(t.rects.minimap[3], 133)
	T.eq(t.music[2].file, "music\\zerg1.wav")
	T.eq(t.music[2].next, 2)
	T.eq(t.attenuation[1], -4000)
	T.eq(t.accelerators[2].module, "StarCraft.exe")
	T.eq(t.accelerators[2].entries[2].command, 39921)
end

return S
