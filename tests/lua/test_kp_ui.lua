-- The interface layer kp_ui (ADR 0010) through kp_core.ui: the UI timer queue of timer.cpp (atlas P43) on synthetic
-- cases, and the original's timer passes of an emulator run replayed (tests/golden/oracle/ui/timers_*.lua, written
-- by tools/oracle/export_ui_timers.py from the run's input.jsonl).
local native_only = require("tests.lua.native_only")

local S = {}

local TIMER_GOLDENS = { "tests/golden/oracle/ui/timers_build_terran.lua" }

local function timers() return native_only.core.ui.timers() end

local function owners_of(q)
	local out = {}
	for i, n in ipairs(q:order()) do out[i] = n.owner .. "/" .. n.id end
	return table.concat(out, " ")
end

function S.timers_sort_by_due_time(T)
	local q = timers()
	assert(q:set(1, 1, 100, false, 1000)) -- due 1100
	assert(q:set(2, 1, 50, false, 1000))  -- due 1050
	assert(q:set(3, 1, 50, false, 1000))  -- due 1050: before the equal one
	T.eq(owners_of(q), "3/1 2/1 1/1")
end

function S.timers_fire_when_strictly_late(T)
	local q = timers()
	assert(q:set(1, 7, 100, true, 1000))
	local fired = {}
	T.eq(q:run(1100, function() fired[#fired + 1] = 1 end), 0) -- lateness 0 does not fire
	T.eq(q:run(1101, function(owner, id, callback, period)
		fired[#fired + 1] = { owner, id, callback, period }
	end), 1)
	T.eq(fired[1][1], 1)
	T.eq(fired[1][2], 7)
	T.eq(fired[1][3], true)
	T.eq(fired[1][4], 100)
	T.eq(q:order()[1].last, 1101) -- the next period starts at the pass's tick: missed periods are not caught up
end

function S.timers_replaced_by_own_work(T)
	local q = timers()
	assert(q:set(1, 1, 10, false, 0))
	assert(q:set(2, 1, 10, false, 0))
	local seen = {}
	q:run(20, function(owner)
		seen[#seen + 1] = owner
		if owner == 2 then q:delete(1, 1) end -- deletes the next node: the walk stops (0x4c5ae0)
	end)
	T.eq(table.concat(seen, ","), "2")
	T.eq(owners_of(q), "2/1")
end

function S.timers_stale_search_keys(T)
	-- SetUiTimer stores its search keys only when the active queue is not empty (0x4c5975): with an empty active
	-- queue during a pass, it searches the fired queue with the keys of the search before, here the delete's 5/2,
	-- so it misses the fired node of 5/1 and makes a second one
	local q = timers()
	assert(q:set(5, 1, 10, false, 0))
	assert(q:set(5, 2, 20, false, 0))
	q:run(30, function(owner, id)
		if id == 2 then
			q:delete(5, 2) -- the active queue is empty now; 5/1 waits in the fired queue
			assert(q:set(5, 1, 100, false, 30))
		end
	end)
	T.eq(owners_of(q), "5/1 5/1")
end

function S.timers_delete_owner(T)
	local q = timers()
	for id = 1, 3 do assert(q:set(4, id, 100 * id, false, 0)) end
	assert(q:set(5, 1, 100, false, 0))
	q:delete_owner(4)
	T.eq(owners_of(q), "5/1")
end

local function apply(q, r)
	if r[1] == "set" then
		assert(q:set(r[2], r[3], r[4], r[5] == 1, r[6]), "timer pool full")
	elseif r[1] == "del" then
		q:delete(r[2], r[3])
	elseif r[1] == "delall" then
		q:delete_owner(r[2])
	end
end

function S.timers_replay_the_original(T)
	for _, name in ipairs(TIMER_GOLDENS) do
		local g = assert((loadstring or load)(T.read(name)))()
		local q = timers()
		local fires = 0
		for n, r in ipairs(g.cases) do
			if r[1] == "pass" then
				local k = 0
				local fired = q:run(r[2], function(owner, id)
					k = k + 1
					local want = r[3][k]
					if not want or want[1] ~= owner or want[2] ~= id then
						error(("%s record %d (tick %d): fire %d is %d/%d, the original's %s"):format(name, n, r[2], k,
							owner, id, want and (want[1] .. "/" .. want[2]) or "none"), 0)
					end
					for _, d in ipairs(r[4]) do
						if d[1] == k - 1 then apply(q, d[2]) end
					end
				end)
				if fired ~= #r[3] then
					error(("%s record %d: %d fired, the original %d"):format(name, n, fired, #r[3]), 0)
				end
				fires = fires + fired
			else
				apply(q, r)
			end
		end
		T.eq(fires, g.fires)
	end
end

-- The text engine (kp_ui_text.c) against the original's probe cases (tests/golden/oracle/ui/text_cases.lua, written
-- by tools/oracle/export_text_cases.py from docs/research/evidence/native-text-probes.json), on the converted fonts.
local TEXT_CASES = "tests/golden/oracle/ui/text_cases.lua"
local DATA = "generated/data/"
local SENTINEL = 0xa5

local function unhex(h)
	return (h:gsub("..", function(b) return string.char(tonumber(b, 16)) end))
end

-- The probe's context (export_text_cases.py): pen (5, 4), clip 2..120 x ..1000, the bottom extent at the pen.
local function setup(t, font, over)
	t:select_font(font)
	t:set({ x = 5, y = 4, clip_left = 2, clip_right = 120, clip_bottom = 1000, ext_bottom = 4, measuring = 1,
		wrap = 1, row = 0, previous_row = 1, spacing = 1 })
	if over then t:set(over) end
end

function S.text_matches_the_original_probes(T)
	if not T.has(DATA .. "fonts.kpd") then
		T.env.log("SKIP test_kp_ui.text_matches_the_original_probes: no generated/data")
		return
	end
	local fonts_data = require("game.data.fonts")
	local strings = require("game.data.strings")
	local kpf = require("game.data.kpf")
	local fk = kpf.parse(T.read(DATA .. "fonts.kpd"), "fonts.kpd")
	local fonts = {}
	for name, section in pairs(fonts_data.FONTS) do fonts[name] = fk:section(section) end
	local banks = { TFGM = fk:section("TFGM"), TFNT = fk:section("TFNT") }
	local sk = kpf.parse(T.read(DATA .. "strings.kpd"), "strings.kpd")
	local core = native_only.core.ui
	local g = assert((loadstring or load)(T.read(TEXT_CASES)))()
	local counts = {}
	for n, c in ipairs(g.cases) do
		local kind, t = c[1], core.text()
		local function check(what, got, want)
			if got ~= want then
				error(("text case %d (%s %s): %s %s, the original's %s"):format(n, kind, tostring(c[2]), what,
					tostring(got), tostring(want)), 0)
			end
		end
		counts[kind] = (counts[kind] or 0) + 1
		if kind == "glyph" then
			setup(t, fonts[c[2]])
			check("offset", t:glyph(c[3]), c[4])
		elseif kind == "width" then
			setup(t, fonts[c[2]], { spacing = c[4] })
			check("width", t:char_width(c[3]), c[5])
		elseif kind == "span" then
			setup(t, fonts[c[2]])
			check("consumed", t:span(string.char(c[3]) .. "A\0"), c[6])
			check("x", t:get("x"), c[4])
			check("bottom", t:get("ext_bottom"), c[5])
		elseif kind == "wrap" then
			setup(t, fonts[c[2]], { clip_right = c[4] })
			check("consumed", t:find_line_break(unhex(c[3])), c[5])
			check("x", t:get("x"), c[6])
		elseif kind == "tbl" then
			setup(t, fonts.font10)
			check("consumed", t:find_line_break(strings.get(sk:section(c[2]), c[3])), c[4])
			check("x", t:get("x"), c[5])
		elseif kind == "tab" then
			setup(t, fonts[c[2]], { x = c[3], clip_left = c[4], clip_right = 32767, wrap = c[5] })
			check("consumed", t:find_line_break("A\tB\nREST"), c[6])
			check("x", t:get("x"), c[7])
		elseif kind == "lines" then
			setup(t, fonts[c[2]], { flags = c[3] })
			local consumed, count = t:layout(unhex(c[4]))
			check("consumed", consumed, c[5])
			check("lines", count, c[6])
			check("x", t:get("x"), c[7])
			check("y", t:get("y"), c[8])
			check("bounds", table.concat({ t:get("ext_left"), t:get("ext_top"), t:get("ext_right"), t:get("ext_bottom") },
				","), table.concat({ c[9], c[10], c[11], c[12] }, ","))
		elseif kind == "highlight" then
			setup(t, fonts[c[2]], { highlight = 65 })
			t:span(unhex(c[3]))
			check("x", t:get("x"), c[4])
			check("bottom", t:get("ext_bottom"), c[5])
		elseif kind == "valign" then
			setup(t, fonts.font8, { flags = c[2], clip_bottom = 31, y = c[4] })
			check("y", t:align(c[3]), c[5])
		elseif kind == "colour" then
			setup(t, fonts.font8)
			t:select_color(c[2])
			check("row", t:get("row"), c[3])
			check("previous", t:get("previous_row"), c[4])
		elseif kind == "grey" then
			setup(t, fonts.font8, { measuring = 0, row = c[3] })
			t:span(string.char(c[2]) .. "\0")
			check("row", t:get("row"), c[4])
			check("previous", t:get("previous_row"), c[5])
		elseif kind == "packets" or kind == "zero" then
			local w, h, pad, packets, bank
			if kind == "packets" then
				local f = fonts[c[2]]
				local off = f:byte(9 + 4 * (c[3] - f:byte(5))) + 256 * f:byte(10 + 4 * (c[3] - f:byte(5)))
					+ 65536 * f:byte(11 + 4 * (c[3] - f:byte(5)))
				w, h, pad, packets, bank = f:byte(off + 1), f:byte(off + 2), c[5], f:sub(off + 5), banks[c[4]]
			else
				w, h, pad, packets, bank = c[2], c[3], c[4], unhex(c[5]) .. string.rep("\1", 96), banks.TFGM
			end
			setup(t, fonts.font8)
			t:set_rows(bank)
			local s = core.surface(w + pad, h, SENTINEL)
			check("consumed", t:plot(s, 0, 0, packets, w, h), c[7])
			check("crc", s:crc32(), c[6])
		end
	end
	T.eq(counts.packets, 4460)
	T.eq(counts.tbl, 1749)
end

-- The panels kp_ui draws against the original's (tests/golden/oracle/ui/panels.lua, written by
-- tools/oracle/export_ui_panels.py from the emulator's screen captures): the CRC-32 of each panel's surface equals
-- that of the capture's crop at the dialog's rectangle (ADR 0011, level 2).
local PANELS = "tests/golden/oracle/ui/panels.lua"
local RACE_GRPS = { [0] = "dlgs\\zerg.grp", [1] = "dlgs\\terran.grp", [2] = "dlgs\\protoss.grp" }

local function new_ui(T)
	return native_only.core.ui.new(T.read(DATA .. "ui.kpd"), T.read(DATA .. "fonts.kpd"))
end

function S.panels_equal_the_original(T)
	if not T.has(DATA .. "ui.kpd") then
		T.env.log("SKIP test_kp_ui.panels_equal_the_original: no generated/data")
		return
	end
	local g = assert((loadstring or load)(T.read(PANELS)))()
	local checked = 0
	for _, c in ipairs(g.cases) do
		local run, frame, race, bin = c[1], c[2], c[3], c[4]
		if bin == "rez\\stat_f10.bin" then
			local u = new_ui(T)
			u:font_set(true)
			u:theme(RACE_GRPS[race])
			u:console(race)
			local d = u:menu_button()
			local k = u:control(d)
			T.eq(table.concat({ k.left, k.top, k.right, k.bottom }, ","), table.concat({ c[6], c[7], c[8], c[9] }, ","))
			u:paint(d)
			if u:crc32(d) ~= c[10] then
				error(("%s frame %d: %s differs from the original's"):format(run, frame, bin), 0)
			end
			checked = checked + 1
		end
	end
	T.eq(checked > 0, true)
end

-- The canvas (ADR 0011): the classic layout of a 640 x 480 screen is the original's screen; the menu button lies
-- where the original draws it, the console art around it, the world shows through the console's top.
function S.canvas_composes_the_classic_screen(T)
	if not T.has(DATA .. "ui.kpd") then
		T.env.log("SKIP test_kp_ui.canvas_composes_the_classic_screen: no generated/data")
		return
	end
	local canvas = require("presentation.ui.canvas")
	local u = new_ui(T)
	u:font_set(true)
	u:theme(RACE_GRPS[2])
	u:console(2)
	local d = u:menu_button()
	u:paint(d)
	local k = u:control(d)
	local c = canvas.new(native_only.core, u)
	local panel = { dialog = d, bin = k.bin_name, rect = { k.left, k.top, k.right, k.bottom } }
	local x0, y0, x1, y1, a = c:compose(640, 480, { panel }, "classic")
	T.eq(table.concat({ x0, y0, x1, y1 }, ","), "0,0,640,480")
	T.eq(a.places[d].x, 408)
	T.eq(a.places[d].y, 388)
	local bitmap = u:bitmap(d)
	for y = 0, 19 do
		for x = 0, 87, 7 do
			local index, alpha = c.canvas:pixel(408 + x, 388 + y)
			T.eq(index, bitmap:byte(y * 88 + x + 1))
			T.eq(alpha, 255)
		end
	end
	local _, alpha = c.canvas:pixel(320, 100)
	T.eq(alpha, 0) -- above the console: the world
	x0 = c:compose(640, 480, { panel }, "classic")
	T.eq(x0, nil) -- an unchanged frame uploads nothing
	x0, y0, x1, y1 = c:compose(1280, 480, { panel }, "wide")
	T.eq(table.concat({ x0, y0, x1, y1 }, ","), "0,0,1280,480")
	local a2 = select(5, c:compose(1280, 480, { panel }, "wide"))
	T.eq(a2.places[d].x, 408 + 640)
end

-- The console over a replayed game (tools/lua/hud_replay.lua): the golden twin of tools/ui_check.py, a few short runs
-- (tests/golden/oracle/ui/hud_*.lua, tools/oracle/export_ui_hud.py) whose console dialogs equal the original's crops.
local HUD_RUNS = { "train_terran", "zerg_morph", "protoss_build", "tooltips" }

function S.hud_panels_equal_the_original(T)
	if not T.has(DATA .. "ui.kpd") then
		T.env.log("SKIP test_kp_ui.hud_panels_equal_the_original: no generated/data")
		return
	end
	local checked = 0
	for _, name in ipairs(HUD_RUNS) do
		local path = "tests/golden/oracle/ui/hud_" .. name .. ".lua"
		if T.has(path) then
			local hud_replay = require("tools.lua.hud_replay")
			local hud = assert((loadstring or load)(T.read(path)))()
			hud.entries = hud.cases
			local want, frames = {}, {}
			for _, p in ipairs(hud.panels) do
				if not want[p[1]] then
					want[p[1]] = {}
					frames[#frames + 1] = p[1]
				end
				want[p[1]][p[2]] = p
			end
			table.sort(frames)
			local before = checked
			hud_replay.run({
				core = native_only.core, read = T.read, hud = hud, frames = frames, input = true,
				on_command = function() end,
				golden = assert((loadstring or load)(T.read("tests/golden/oracle/scenarios/" .. name .. ".lua")))(),
				on_frame = function(frame, u)
					local tip = want[frame].tooltip
					if tip then
						local bytes, x, y, w, h = u:tooltip()
						T.eq(table.concat({ x or -1, y or -1, (x or 0) + (w or 0) - 1, (y or 0) + (h or 0) - 1 }, ","),
							table.concat({ tip[3], tip[4], tip[5], tip[6] }, ","))
						if require("game.data.crc32").of(bytes) ~= tip[7] then
							error(("%s frame %d: the tooltip differs from the original's"):format(name, frame), 0)
						end
						checked = checked + 1
					end
					for _, d in ipairs(u:open()) do
						local k = u:control(d)
						local p = want[frame][k.bin_name]
						if p then
							T.eq(table.concat({ k.left, k.top, k.right, k.bottom }, ","), table.concat({ p[3], p[4], p[5], p[6] }, ","))
							if u:crc32(d) ~= p[7] then
								error(("%s frame %d: %s differs from the original's"):format(name, frame, k.bin_name), 0)
							end
							checked = checked + 1
						end
					end
				end,
			})
			T.eq(checked - before, #hud.panels) -- every captured panel is one Koprulu draws
		end
	end
	if checked == 0 then T.env.log("SKIP test_kp_ui.hud_panels_equal_the_original: no hud goldens") end
end

-- The window's keyboard goes through the accelerator tables (TranslateAcceleratorA, ACCL): '-' is the game speed's
-- command (40058 -> {0x0f, speed - 1}), Ctrl+1 assigns control group 1 ({0x13, 0, 1}), a plain key is a key event.
function S.keys_translate_through_the_accelerators(T)
	local path = "tests/golden/oracle/ui/hud_tooltips.lua"
	if not T.has(DATA .. "ui.kpd") or not T.has(path) then
		T.env.log("SKIP test_kp_ui.keys_translate_through_the_accelerators: no generated/data or hud golden")
		return
	end
	local hud = assert((loadstring or load)(T.read(path)))()
	hud.entries = hud.cases
	local checked = false
	require("tools.lua.hud_replay").run({
		core = native_only.core, read = T.read, hud = hud, frames = { 20 }, input = true, on_command = function() end,
		golden = assert((loadstring or load)(T.read("tests/golden/oracle/scenarios/tooltips.lua")))(),
		on_frame = function(_, u, game)
			u:take_commands()
			u:window_char(45)
			local speed = game.speed
			local got = u:take_commands()
			T.eq(#got, speed > 0 and 1 or 0)
			if speed > 0 then T.eq(got[1], string.char(0x0f, speed - 1)) end
			T.eq(u:window_key(true, 0x11), true)
			T.eq(u:window_key(true, 0x31), false) -- Ctrl+1: the accelerator's key has no WM_CHAR
			u:window_key(false, 0x31)
			u:window_key(false, 0x11)
			got = u:take_commands()
			T.eq(got[1], string.char(0x13, 0, 1))
			checked = true
		end,
	})
	T.eq(checked, true)
end

-- The game's deselections reach the network as the interface's selection commands (SendSelectionCommands 0x474130):
-- KP_EVENT_SELECTION_SENT carries the selection the call left (four 11-bit units a field), each event its own command;
-- KP_EVENT_SELECTION_GONE takes a dead unit out of the network's mirror (0x472100), with no command of its own.
function S.game_deselections_send_selection_commands(T)
	local path = "tests/golden/oracle/ui/hud_tooltips.lua"
	if not T.has(DATA .. "ui.kpd") or not T.has(path) then
		T.env.log("SKIP test_kp_ui.game_deselections_send_selection_commands: no generated/data or hud golden")
		return
	end
	local events = require("game.simulation.events")
	local hud = assert((loadstring or load)(T.read(path)))()
	hud.entries = hud.cases
	local checked = false
	local function pack(units)
		local f = { 0, 0, 0 }
		for k, n in ipairs(units) do
			local i = math.floor((k - 1) / 4) + 1
			f[i] = f[i] + n * 2 ^ (11 * ((k - 1) % 4))
		end
		return f[1], f[2], f[3]
	end
	require("tools.lua.hud_replay").run({
		core = native_only.core, read = T.read, hud = hud, frames = { 20 }, input = true, on_command = function() end,
		golden = assert((loadstring or load)(T.read("tests/golden/oracle/scenarios/tooltips.lua")))(),
		on_frame = function(_, u, game)
			local own = {}
			for n = 1, 1700 do
				if game.u.hp[n] > 0 and game.u.owner[n] == 0 and (game.u.type[n] == 106 or game.u.type[n] == 7 or game.u.type[n] == 0) then
					own[#own + 1] = n
				end
			end
			T.eq(#own, 3)
			u:take_commands()
			u:game_event(events.SELECTION_SENT, pack(own))
			local got = u:take_commands()
			T.eq(#got, 1)
			T.eq(got[1]:byte(1), 0x09) -- three new units: one select command
			T.eq(got[1]:byte(2), 3)
			u:game_event(events.SELECTION_SENT, pack({ own[1], own[2] }))
			got = u:take_commands()
			T.eq(#got, 1)
			T.eq(got[1]:byte(1), 0x0b) -- one of three out: a remove command
			T.eq(#got[1], 4)
			u:game_event(events.SELECTION_GONE, own[2], 0, 0)
			u:game_event(events.SELECTION_SENT, pack({ own[1] }))
			T.eq(#u:take_commands(), 0) -- the mirror lost the dead unit already
			checked = true
		end,
	})
	T.eq(checked, true)
end

-- The result dialogs (kp_ui_result.c, atlas P41): the victory's OK (its hotkey V) leaves exit code 6; the defeat's
-- 20-second timer without an answer leaves 7 and destroys the dialog; EndGame closes the modeless dialog.
function S.result_dialogs_leave_exit_codes(T)
	if not T.has(DATA .. "ui.kpd") then
		T.env.log("SKIP test_kp_ui.result_dialogs_leave_exit_codes: no generated/data")
		return
	end
	local VICTORY, DEFEAT, KEY_DOWN, KEY_UP, CHAR, IDLE = 3, 2, 0, 2, 15, 13
	local u = new_ui(T)
	u:font_set(true)
	u:theme(RACE_GRPS[1])
	u:now(1000)
	local d = u:show_result(VICTORY)
	T.ok(d >= 0)
	T.eq(u:modeless(), d)
	T.eq(u:show_result(VICTORY), -1) -- once
	T.eq(u:control(d).bin_name:lower(), "rez\\wmission.bin")
	u:input(KEY_DOWN, 0x56, 0)
	u:input(CHAR, 0x76, 0)
	u:input(KEY_UP, 0x56, 0)
	u:now(1100)
	u:input(IDLE, 320, 240) -- the scheduled press releases
	T.eq(u:game_exit(), 6)
	u:end_game()
	T.eq(u:modeless(), -1)

	u = new_ui(T)
	u:font_set(true)
	u:theme(RACE_GRPS[0])
	u:now(1000)
	d = u:show_result(DEFEAT)
	T.eq(u:control(d).bin_name:lower(), "rez\\lmission.bin")
	u:tick(21000) -- exactly 20 s: not yet late
	T.eq(u:game_exit(), 0)
	u:tick(21001)
	T.eq(u:game_exit(), 7)
	T.eq(u:modeless(), -1)
end

-- One interface runs the glue's games one after another (the product): a new game's console forgets the last game's
-- exit code, its result dialog's answer and its paused seconds (RunGame 0x445310, InitTriggerState 0x4be240's 0x4bcf30,
-- StartElapsedClock 0x47a7c0), and the score's seconds leave the paused ones out (GetElapsedSeconds 0x47a780).
function S.next_game_forgets_the_last_result(T)
	local path = "generated/missions/terran/terran01.kpm"
	if not T.has(path) or not T.has(DATA .. "ui.kpd") then
		T.env.log("SKIP test_kp_ui.next_game_forgets_the_last_result: no generated data")
		return
	end
	local VICTORY, DEFEAT, KEY_DOWN, KEY_UP, CHAR, IDLE = 3, 2, 0, 2, 15, 13
	local oracle = require("tools.lua.oracle")
	local u = new_ui(T)
	u:strings(T.read(DATA .. "strings.kpd"))
	u:font_set(true)
	u:theme(RACE_GRPS[1])
	local now = 1000
	local function frames(n)
		for _ = 1, n do
			now = now + 16
			u:now(now)
			u:tick(now)
			u:input(IDLE)
			u:present()
		end
	end
	u:now(now)
	local game = oracle.start_mission(T.read, path, 0, 0, {})
	u:hud(game)
	u:window_key(true, 0x79, false) -- F10: the game menu pauses the game
	u:window_key(false, 0x79, false)
	frames(10)
	T.ok(u:modeless() >= 0, "the game menu")
	now = now + 5000
	u:window_key(true, 0x1b, false) -- Escape: back to the game 5 s later
	u:window_char(27)
	u:window_key(false, 0x1b, false)
	frames(10)
	T.eq(u:modeless(), -1)
	T.ok(u:show_result(VICTORY) >= 0)
	u:input(KEY_DOWN, 0x56, 0)
	u:input(CHAR, 0x76, 0)
	u:input(KEY_UP, 0x56, 0)
	frames(10)
	T.eq(u:game_exit(), 6)
	u:end_game()
	u:score_game(game, VICTORY, "Player", 20)
	T.eq(u:score_state().elapsed, 15, "the 5 paused seconds left out")
	game = oracle.start_mission(T.read, path, 0, 0, {})
	u:hud(game)
	T.eq(u:game_exit(), 0)
	T.ok(u:show_result(DEFEAT) >= 0, "the next game's result dialog")
	u:end_game()
	u:score_game(game, DEFEAT, "Player", 20)
	T.eq(u:score_state().elapsed, 20, "no paused seconds of the last game")
end

-- EndGame 0x443db0's ResetInputHandlers 0x43f330: the game's input procedures leave with the game, so the glue's idle
-- events and clicks after end_game reach no procedure of the freed game (a crash on Exit Program before).
function S.end_game_resets_the_input_procedures(T)
	local path = "generated/missions/terran/terran01.kpm"
	if not T.has(path) or not T.has(DATA .. "ui.kpd") then
		T.env.log("SKIP test_kp_ui.end_game_resets_the_input_procedures: no generated data")
		return
	end
	local LEFT_DOWN, LEFT_UP, RIGHT_DOWN, IDLE, CHAR, COMMAND = 4, 5, 7, 13, 15, 16
	local game = require("tools.lua.oracle").start_mission(T.read, path, 0, 0, {})
	local u = new_ui(T)
	u:strings(T.read(DATA .. "strings.kpd"))
	u:font_set(true)
	u:theme(RACE_GRPS[1])
	u:now(1000)
	u:hud(game)
	u:input(IDLE, 320, 240)
	u:end_game()
	for _, e in ipairs({ { IDLE, 320, 240 }, { LEFT_DOWN, 100, 100 }, { LEFT_UP, 100, 100 }, { RIGHT_DOWN, 100, 100 },
		{ CHAR, 0x61, 0 }, { COMMAND, 0, 0 } }) do
		u:input(e[1], e[2], e[3])
	end
	T.eq(#u:take_commands(), 0)
end

-- The game's first view (HandleStartLocationRecord 0x46a280, InitScreenPosition 0x445810): the local player's start
-- location's tile less (10, 6) for the original's 640 x 400 view, less half a wider view's tiles, clamped to the map.
-- viewer/play_hud.lua's console over a game of the mission at path on a logical screen { w, h }, without textures:
-- run(hud) with the data loader reading the test's files.
local function with_hud(T, game, path, screen, run)
	local loader = require("game.data.loader")
	local saved = loader.read
	loader.read = function(p) return T.read(p:sub(2)) end
	local ok, err = pcall(function()
		local terrain = require("game.data.terrain")
		local mission = require("game.data.mission").parse(T.read(path), path)
		local name = terrain.TILESETS[mission.tileset]
		local data = T.read(DATA .. "tilesets/" .. name .. ".kpd")
		local hud = require("viewer.play_hud").new(native_only.core, game, { tileset = terrain.parse_tileset(data, name),
			data = data, palettes = require("game.data.palettes").parse(T.read(DATA .. "palettes.kpd"), "palettes.kpd"),
			index = mission.tileset, colors = mission.colors }, 1000, nil, screen)
		hud.canvas.upload = function() end -- no textures
		run(hud)
	end)
	loader.read = saved
	if not ok then error(err, 0) end
end

function S.first_view_on_the_start_location(T)
	local path = "generated/missions/terran/terran01.kpm"
	if not T.has(path) or not T.has(DATA .. "ui.kpd") then
		T.env.log("SKIP test_kp_ui.first_view_on_the_start_location: no generated data")
		return
	end
	local game = require("tools.lua.oracle").start_mission(T.read, path, 0, 0, {})
	local u = new_ui(T)
	u:strings(T.read(DATA .. "strings.kpd"))
	u:now(1000)
	u:hud(game)
	local sx, sy = game.players[game.local_player].start_x, game.players[game.local_player].start_y
	local function want(half_w, half_h, map)
		local x = math.max(0, math.floor(sx / 32) - half_w) * 32
		local y = math.max(0, math.floor(sy / 32) - half_h) * 32
		return math.min(x, map.x), math.min(y, map.y)
	end
	local map = { x = game.map_w * 32 - 640, y = (game.map_h - 12) * 32 + 8 }
	u:start_screen()
	local x, y = u:take_camera()
	local wx, wy = want(10, 6, map)
	T.eq(x, wx - wx % 8)
	T.eq(y, wy - wy % 8)
	u:view(1024, 496)
	u:start_screen()
	x, y = u:take_camera()
	map = { x = game.map_w * 32 - 1024, y = (game.map_h - 12) * 32 + 8 - 96 }
	wx, wy = want(16, 7, map)
	T.eq(x, wx - wx % 8)
	T.eq(y, wy - wy % 8)
	-- the product's console (viewer/play_hud.lua): its first frame keeps the start's view over the viewer's camera,
	-- still at the previous game's (0, 0)
	with_hud(T, game, path, { 1024, 576 }, function(hud)
		x, y = hud:frame(1024, 576, 1016, 0, nil, nil, 0, 0)
		T.eq(x, wx - wx % 8)
		T.eq(y, wy - wy % 8)
		T.eq(hud:frame(1024, 576, 1032, 0, nil, nil, x, y), nil) -- once: then the viewer's camera
	end)
end

-- IsInvalidFileName 0x408300 (the new ID's and the save's names): control bytes, network.tbl string 17's characters,
-- an empty name and the DOS device names before the last dot.
function S.file_names_the_game_refuses(T)
	if not T.has(DATA .. "ui.kpd") then
		T.env.log("SKIP test_kp_ui.file_names_the_game_refuses: no generated/data")
		return
	end
	local u = new_ui(T)
	u:strings(T.read(DATA .. "strings.kpd"))
	local cases = {
		{ "Kop", false }, { "my save 1", false }, { "console", false }, { "com10", false }, { "lpt", false },
		{ "con", true }, { "CON", true }, { "con.txt", true }, { "nul.a.b", false }, { "clock$", true },
		{ "a*b", true }, { "a/b", true }, { "a?", true }, { "", true }, { "a\1b", true }, { "c:name", false },
	}
	for _, c in ipairs(cases) do
		T.eq(u:invalid_file_name(c[1], 28), c[2], c[1])
	end
	T.eq(u:invalid_file_name("dir/con", 28, true), true)
	T.eq(u:invalid_file_name("dir/name", 28, true), false)
	T.eq(u:invalid_file_name("abcdefghij", 5), true) -- not terminated within the limit
end

-- A wider layout's game view (u:view, plan section 2a): the pointer over a unit right of the original's 640 columns
-- chooses the unit's cursor (GetPointerCursorId 0x43e6d0's view rectangle), which the original's 640 x 400 view
-- leaves to the arrow.
function S.view_widens_the_game_area(T)
	local path = "generated/missions/terran/terran01.kpm"
	if not T.has(path) or not T.has(DATA .. "ui.kpd") then
		T.env.log("SKIP test_kp_ui.view_widens_the_game_area: no generated data")
		return
	end
	local game = require("tools.lua.oracle").start_mission(T.read, path, 0, 0, {})
	local u = new_ui(T)
	u:strings(T.read(DATA .. "strings.kpd"))
	u:font_set(true)
	u:theme(RACE_GRPS[1])
	u:now(1000)
	u:hud(game)
	local px, py -- a map point of a unit the local player owns
	for y = 64, 64 * 32, 16 do
		for x = 700, 64 * 32, 16 do
			local n = u:pick(x, y)
			if n ~= 0 and game.u.owner[n] == game.local_player and not px then px, py = x, y end
		end
	end
	T.ok(px ~= nil, "a unit of the local player right of x 700")
	local sx, sy = px - 800, math.max(0, py - 200)
	local lx, ly = px - sx, py - sy
	local function cursor()
		u:screen(sx, sy)
		u:over_console(false)
		u:input(3, lx, ly) -- the mouse moves over the unit
		return (u:cursor_update(math.floor(lx * 640 / 1024), ly))
	end
	T.eq(cursor(), 0) -- the original's view ends at x 639
	u:view(1024, 400)
	T.eq(cursor(), 6) -- the local player's unit (GetUnitHoverCursorId 0x43ea90)
end

-- A wide layout's game view (viewer/play_hud.lua) keeps its view coordinates, which reach into the console dialogs'
-- original rectangles (the status panel's, the minimap's, the command card's): a left press there starts the drag
-- box (its cursor 9) as anywhere else on the game view, not the hidden dialog's.
function S.wide_game_view_clicks_reach_the_game(T)
	local path = "generated/missions/terran/terran01.kpm"
	if not T.has(path) or not T.has(DATA .. "ui.kpd") then
		T.env.log("SKIP test_kp_ui.wide_game_view_clicks_reach_the_game: no generated data")
		return
	end
	local game = require("tools.lua.oracle").start_mission(T.read, path, 0, 0, {})
	with_hud(T, game, path, { 1280, 720 }, function(hud)
		local play_hud = require("viewer.play_hud")
		local cx, cy = hud:frame(1280, 720, 1016, 0, nil, nil, 0, 0)
		T.eq(hud.arrangement.name, "wide")
		-- inside statdata's {138, 388, 407, 479}, the minimap's {0, 315, 137, 479}, the card's {496, 354, 639, 479};
		-- then a point outside every original rectangle
		for _, p in ipairs({ { 250, 430 }, { 60, 400 }, { 560, 420 }, { 700, 430 } }) do
			hud:input(play_hud.MOUSE_MOVE, p[1], p[2], cx, cy)
			hud:input(play_hud.LEFT_DOWN, p[1], p[2], cx, cy)
			T.eq(hud.ui:hud_state().cursor, 9, ("the drag box at %d, %d"):format(p[1], p[2]))
			hud:input(play_hud.LEFT_UP, p[1], p[2], cx, cy)
		end
	end)
end

-- A loaded save (kp_load) replaces the selection under the console: the console takes the loaded game's (its status
-- unit), as it does after any selection of the game's, and the network's mirror is the loaded game's row (the
-- selection commands go by it).
function S.loaded_save_refreshes_the_console(T)
	local path = "generated/missions/terran/terran01.kpm"
	if not T.has(path) or not T.has(DATA .. "ui.kpd") then
		T.env.log("SKIP test_kp_ui.loaded_save_refreshes_the_console: no generated data")
		return
	end
	local game = require("tools.lua.oracle").start_mission(T.read, path, 0, 0, {})
	local u = new_ui(T)
	u:strings(T.read(DATA .. "strings.kpd"))
	u:font_set(true)
	u:theme(RACE_GRPS[1])
	u:now(1000)
	u:hud(game)
	local own = {}
	for n = 1, 1700 do
		if game.u.owner[n] == game.local_player and game.u.type[n] == 0 then own[#own + 1] = n end
	end
	T.ok(#own >= 2, "two Marines of the local player")
	game:select_locally({ own[1] })
	u:hud_frame()
	T.eq(u:hud_state().status_unit, own[1])
	local bytes = game:save()
	game:select_locally({ own[2] })
	u:hud_frame()
	T.eq(u:hud_state().status_unit, own[2])
	game:command_select(game.local_player, { game:unit_ref(own[1]) }) -- the network's row the save keeps
	local row = game:save()
	game:command_select(game.local_player, { game:unit_ref(own[2]) })
	game:load(bytes)
	u:hud_frame()
	T.eq(u:hud_state().status_unit, own[1], "the loaded game's selection")
	game:load(row)
	u:hud_frame()
	local mirror, want = u:hud_state().mirror, game.selections[game.local_player]
	T.ok(mirror[1] ~= 0, "the network's mirror holds the loaded row")
	for k = 1, 12 do T.eq(mirror[k], want[k - 1], ("the mirror's slot %d"):format(k)) end -- the row is 0-based
end

-- A wider view centres a unit at its own centre (CenterScreenOnUnit 0x422b10 by tiles: 10, 6 of the original's
-- 640 x 400; 20, 10 of 1280 x 640).
function S.wide_view_centres_the_status_unit(T)
	local path = "generated/missions/terran/terran01.kpm"
	if not T.has(path) or not T.has(DATA .. "ui.kpd") then
		T.env.log("SKIP test_kp_ui.wide_view_centres_the_status_unit: no generated data")
		return
	end
	local COMMAND, CENTRE_STATUS = 16, 40036
	local game = require("tools.lua.oracle").start_mission(T.read, path, 0, 0, {})
	local u = new_ui(T)
	u:strings(T.read(DATA .. "strings.kpd"))
	u:font_set(true)
	u:theme(RACE_GRPS[1])
	u:now(1000)
	u:hud(game)
	u:view(1280, 640)
	local unit
	for n = 1, 1700 do -- a unit whose centred view stays inside the map's limits
		local tx, ty = game.u.type[n] and game.u.x[n] and math.floor(game.u.x[n] / 32), game.u.y[n] and math.floor(game.u.y[n] / 32)
		if tx and game.u.type[n] < 228 and tx >= 21 and tx <= 43 and ty >= 11 and ty <= 50 then unit = n break end
	end
	T.ok(unit ~= nil, "a unit away from the map's edges")
	game:select_locally({ unit })
	u:hud_frame()
	u:screen(0, 0)
	u:input(COMMAND, CENTRE_STATUS, 0)
	local x, y = u:take_camera()
	T.eq(x, (math.floor(game.u.x[unit] / 32) - 20) * 32)
	T.eq(y, (math.floor(game.u.y[unit] / 32) - 10) * 32)
end

-- Defold's text (UTF-8) reaches the window as single-byte characters: Latin-1's code points, nothing for the others,
-- nor for a control character (a browser's Enter brings a carriage return as text too, whose WM_CHAR the key gives).
function S.text_reaches_the_window_as_bytes(T)
	local got = {}
	local fake = { window_char = function(_, c) got[#got + 1] = c end }
	require("viewer.play_hud").text(fake, "a\195\169\226\130\172b\r\t\27\8")
	T.eq(table.concat(got, " "), "97 233 98")
end

-- A held key's action comes every frame in Defold: the window gets its press, its repeats and its release only; a
-- modifier's release goes to the window when its other side is up, and a release no window took still counts.
function S.held_keys_reach_the_window_at_their_repeats(T)
	local stub = rawget(_G, "hash") == nil
	if stub then rawset(_G, "hash", function(name) return "hash:" .. name end) end
	package.loaded["viewer.play_keys"] = nil
	local ok, err = pcall(function()
		local keys = require("viewer.play_keys")
		local a, lshift, rshift = hash("vk_a"), hash("vk_lshift"), hash("vk_rshift")
		local function event(id, action)
			keys.track(id, action)
			return keys.forward(id, action)
		end
		T.eq(event(a, { pressed = true, repeated = true }), true)
		T.eq(event(a, {}), false, "a held frame between the repeats")
		T.eq(event(a, { repeated = true }), true)
		T.eq(event(a, { released = true }), true)
		keys.track(lshift, { pressed = true }) -- left Shift down and up while no window takes the keys
		keys.track(lshift, { released = true })
		T.eq(event(rshift, { pressed = true }), true)
		T.eq(event(rshift, { released = true }), true, "the right Shift's release: the left one is up")
		T.eq(event(lshift, { pressed = true }), true)
		T.eq(event(rshift, { pressed = true }), true)
		T.eq(event(lshift, { released = true }), false, "the right Shift still holds the virtual key")
		T.eq(event(rshift, { released = true }), true)
		-- the focus goes with left Shift down, and its release never comes: the right one's still reaches the window
		T.eq(event(lshift, { pressed = true }), true)
		keys.reset()
		T.eq(event(rshift, { pressed = true }), true)
		T.eq(event(rshift, { released = true }), true, "after the focus: the left Shift held before it is forgotten")
	end)
	package.loaded["viewer.play_keys"] = nil
	if stub then rawset(_G, "hash", nil) end
	if not ok then error(err, 0) end
end

-- The window's character of a key follows its WM_KEYDOWN only when the pump translates it (no accelerator took the
-- key, no dialog closed on it: 0x6ce660).
function S.key_characters_follow_translated_keys(T)
	local chars = {}
	local translated = true
	local fake = { window_key = function() return translated end, window_char = function(_, c) chars[#chars + 1] = c end }
	local play_hud = require("viewer.play_hud")
	play_hud.key(fake, true, 0x0d, false)
	translated = false
	play_hud.key(fake, true, 0x1b, false)
	T.eq(table.concat(chars, " "), "13")
end

-- A gesture keeps the coordinates of where it began on a wide screen: a drag the minimap holds goes on in its
-- coordinates off the panel (above the minimap: its top row, the view at the map's top), and a click on a text line
-- over the game view reaches the game (the text is not the console's art).
function S.wide_gestures_keep_their_panel(T)
	local path = "generated/missions/terran/terran01.kpm"
	if not T.has(path) or not T.has(DATA .. "ui.kpd") then
		T.env.log("SKIP test_kp_ui.wide_gestures_keep_their_panel: no generated data")
		return
	end
	local game = require("tools.lua.oracle").start_mission(T.read, path, 0, 0, {})
	with_hud(T, game, path, { 1280, 720 }, function(hud)
		local play_hud = require("viewer.play_hud")
		local events = require("game.simulation.events")
		local cx, cy = hud:frame(1280, 720, 1016, 0, nil, nil, 0, 0)
		local minimap
		for _, d in ipairs(hud.ui:open()) do
			if hud.ui:control(d).bin_name:lower() == "rez\\minimap.bin" then minimap = d end
		end
		local place, k = hud.arrangement.places[minimap], hud.ui:control(minimap)
		local function at(ox, oy) return place.x + ox - k.left, place.y + oy - k.top end
		local x, y = at(70, 470) -- the minimap image's bottom rows: the view at the map's bottom
		hud:input(play_hud.MOUSE_MOVE, x, y, cx, cy)
		local _, _, bottom = hud:input(play_hud.LEFT_DOWN, x, y, cx, cy)
		T.ok(bottom and bottom > 0, "the press moved the view down")
		x, y = at(70, k.top - 30) -- off the panel, above it
		local _, _, top = hud:input(play_hud.MOUSE_MOVE, x, y, cx, bottom)
		T.eq(top, 0, "the drag above the minimap: its top row")
		hud:input(play_hud.LEFT_UP, x, y, cx, 0)
		-- a text line over the game view: a press on one of its pixels starts the drag box
		hud:event(events.ERROR, game.local_player, 1, 0)
		hud:frame(1280, 720, 1032, 0, nil, nil, 0, 0)
		local c, hit = hud.canvas.canvas, nil
		for py = 0, hud.arrangement.top - 1 do
			for px = 0, 1279 do
				local _, alpha = c:pixel(px, py)
				if alpha == 254 then hit = { px, py } break end
			end
			if hit then break end
		end
		T.ok(hit ~= nil, "the error line drawn")
		hud:input(play_hud.MOUSE_MOVE, hit[1], hit[2], 0, 0)
		hud:input(play_hud.LEFT_DOWN, hit[1], hit[2], 0, 0)
		T.eq(hud.ui:hud_state().cursor, 9, "the drag box from the text line's pixel")
		hud:input(play_hud.LEFT_UP, hit[1], hit[2], 0, 0)
	end)
end

-- The game screen's drawing after the fog (its layer's callback 0x445ea0) on the canvas: the drag box while the button
-- is down (DrawDragBox 0x4bcc40) and the command marker while it shows (DrawCommandMarker 0x418be0: the marker sprite
-- is hidden, so the world's draw list leaves it out), both gone once they end.
function S.canvas_draws_the_drag_box_and_the_command_marker(T)
	local path = "generated/missions/terran/terran01.kpm"
	if not T.has(path) or not T.has(DATA .. "ui.kpd") then
		T.env.log("SKIP test_kp_ui.canvas_draws_the_drag_box_and_the_command_marker: no generated data")
		return
	end
	local oracle = require("tools.lua.oracle")
	local game = oracle.start_mission(T.read, path, 0, 0, {})
	local run = oracle.runner(game)
	with_hud(T, game, path, { 1280, 720 }, function(hud)
		local play_hud = require("viewer.play_hud")
		local cx, cy = hud:frame(1280, 720, 1016, 0, nil, nil, 0, 0)
		local c = hud.canvas.canvas
		local function alpha(x, y) return select(2, c:pixel(x, y)) end
		local function overlay_in(x0, y0, x1, y1)
			local n = 0
			for y = y0, y1 do
				for x = x0, x1 do
					if alpha(x, y) == 254 then n = n + 1 end
				end
			end
			return n
		end
		hud:input(play_hud.MOUSE_MOVE, 100, 60, cx, cy)
		hud:input(play_hud.LEFT_DOWN, 100, 60, cx, cy)
		hud:input(play_hud.MOUSE_MOVE, 300, 200, cx, cy)
		hud:frame(1280, 720, 1032, 0, 300, 200, cx, cy)
		T.eq(alpha(100, 60), 254, "the box's corner")
		T.eq(alpha(200, 60), 254, "its top edge")
		T.eq(alpha(300, 130), 254, "its right edge")
		T.eq(alpha(200, 130), 0, "inside: the world")
		local box_index = c:pixel(100, 60)
		T.eq(c:pixel(300, 200), box_index, "one colour")
		hud:input(play_hud.LEFT_UP, 300, 200, cx, cy)
		hud:frame(1280, 720, 1048, 0, 300, 200, cx, cy)
		T.eq(overlay_in(100, 60, 300, 200), 0, "the box ends with the button")
		local function drag()
			hud:input(play_hud.LEFT_DOWN, 100, 60, cx, cy)
			hud:input(play_hud.MOUSE_MOVE, 300, 200, cx, cy)
			hud:frame(1280, 720, 1048, 0, 300, 200, cx, cy)
			T.eq(alpha(100, 60), 254, "a box again")
		end
		-- the focus goes while the button is down: no release comes (OnCaptureChanged 0x43f450)
		drag()
		hud:capture_lost()
		hud:frame(1280, 720, 1048, 0, 300, 200, cx, cy)
		T.eq(overlay_in(100, 60, 300, 200), 0, "the box ends with the window's mouse")
		T.eq(hud.buttons, 0, "no button down")
		-- the mouse state says no button while a box is on: the game screen's draw ends it (DrawDragBox 0x4bcc40)
		drag()
		local held = hud.ui:hud_state().keys
		hud.ui:keys(held[1], held[2], held[3], 0)
		hud:frame(1280, 720, 1048, 0, 300, 200, cx, cy)
		T.eq(overlay_in(100, 60, 300, 200), 0, "the box ends with no button down")
		hud.ui:keys(held[1], held[2], held[3], hud.buttons)
		hud:frame(1280, 720, 1048, 0, 300, 200, cx, cy)
		T.eq(overlay_in(100, 60, 300, 200), 0, "ended, not hidden: the button again shows no box")
		hud:input(play_hud.LEFT_UP, 300, 200, cx, cy)
		-- the marker at a map point of the view (MovePlacementCursor 0x417ad0)
		game:mark_target(cx + 400, cy + 150, 0)
		hud:frame(1280, 720, 1064, 0, 300, 200, cx, cy)
		T.ok(overlay_in(400 - 20, 150 - 16, 400 + 20, 150 + 16) > 50, "the marker drawn around its point")
		for step = 2, 40 do run.to(step) end -- its animation ends (opcode 0x2d clears the marker's byte)
		hud:frame(1280, 720, 1080, 0, 300, 200, cx, cy)
		T.eq(overlay_in(400 - 20, 150 - 16, 400 + 20, 150 + 16), 0, "the marker gone")
	end)
end

-- A click on a lone producer of the local player's shows its rally point (FlashRallyPoint 0x41b250 behind
-- IsRallyProducer 0x429250): the marker moves to the point (HighlightRightClickTarget 0x47fc10); a rally point on the
-- producer's own position shows nothing.
function S.click_on_a_producer_flashes_its_rally_point(T)
	local path = "generated/missions/terran/terran01.kpm"
	if not T.has(path) or not T.has(DATA .. "ui.kpd") then
		T.env.log("SKIP test_kp_ui.click_on_a_producer_flashes_its_rally_point: no generated data")
		return
	end
	local COMMAND_CENTER = 106
	local game = require("tools.lua.oracle").start_mission(T.read, path, 0, 0, {})
	with_hud(T, game, path, { 1280, 720 }, function(hud)
		local play_hud = require("viewer.play_hud")
		local cc
		for n = 1, 1700 do
			if game.u.type[n] == COMMAND_CENTER and game.u.owner[n] == game.local_player and game.u.hp[n] > 0 then
				cc = n
				break
			end
		end
		T.ok(cc ~= nil, "a command center of the local player's")
		local x, y = game.u.x[cc], game.u.y[cc]
		local max_y = game.map_h_px - 12 * 32 - (640 - 400) + 8 -- the wide view's scroll limit
		local cx, cy = x - 400, math.min(y - 250, max_y - max_y % 8)
		hud:frame(1280, 720, 1000, 0, nil, nil, 0, 0) -- the start location's view first
		hud:frame(1280, 720, 1016, 0, nil, nil, cx, cy)
		local px, py = x - cx, y - cy -- the producer on the canvas
		local rx, ry = px + 150, py
		T.ok(px > 0 and rx < 1280 and py > 0 and py < 400, "the producer and its rally point in the view")
		local c = hud.canvas.canvas
		local function overlay_in(x0, y0, x1, y1)
			local n = 0
			for yy = y0, y1 do
				for xx = x0, x1 do
					if select(2, c:pixel(xx, yy)) == 254 then n = n + 1 end
				end
			end
			return n
		end
		local function click(now)
			hud:input(play_hud.MOUSE_MOVE, px, py, cx, cy)
			hud:input(play_hud.LEFT_DOWN, px, py, cx, cy)
			hud:input(play_hud.LEFT_UP, px, py, cx, cy)
			hud:frame(1280, 720, now, 0, px, py, cx, cy)
		end
		game.u.rally_x[cc], game.u.rally_y[cc] = x, y
		click(1032)
		T.eq(game.local_selection[0], cc, "the producer selected")
		T.eq(overlay_in(rx - 20, ry - 16, rx + 20, ry + 16), 0, "a rally point on the producer: no marker")
		game.u.rally_x[cc], game.u.rally_y[cc] = x + 150, y
		click(1048)
		T.ok(overlay_in(rx - 20, ry - 16, rx + 20, ry + 16) > 50, "the marker at the rally point")
	end)
end

-- The music's toggle (40038, ToggleMusicMute 0x49ec00 into ApplyVolumeOptions 0x49ea60) silences and closes the
-- stream (CloseMusic 0x46f290), and back on plays the current track again (ResumeMusic 0x46f160); the game's end
-- stops the music (EndGame 0x443db0: StopAllSoundsAndMusic 0x49db40), so the next screen's track starts.
function S.music_stops_with_its_option_and_the_game(T)
	local path = "generated/missions/terran/terran01.kpm"
	if not T.has(path) or not T.has(DATA .. "ui.kpd") then
		T.env.log("SKIP test_kp_ui.music_stops_with_its_option_and_the_game: no generated data")
		return
	end
	local COMMAND, TOGGLE_MUSIC = 16, 40038
	local MUSIC, MUSIC_VOLUME, MUSIC_STOP, SILENT = 3, 4, 5, -10000
	local game = require("tools.lua.oracle").start_mission(T.read, path, 0, 0, {})
	with_hud(T, game, path, { 1280, 720 }, function(hud)
		local u = hud.ui
		u:sound_device(true, 50)
		local function music()
			local out = {}
			for _, c in ipairs(u:take_sound_commands()) do
				if c[2] == -1 and c[1] >= MUSIC and c[1] <= MUSIC_STOP then out[#out + 1] = c end
			end
			return out
		end
		local started = music()
		T.eq(#started, 1, "the game's track")
		T.eq(started[1][1], MUSIC)
		local track = started[1][3]
		u:input(COMMAND, TOGGLE_MUSIC, 0)
		local off = music()
		T.eq(#off, 2, "off: two commands")
		T.eq(off[1][1], MUSIC_VOLUME)
		T.eq(off[1][4], SILENT, "the silence")
		T.eq(off[2][1], MUSIC_STOP, "the stream closed")
		u:input(COMMAND, TOGGLE_MUSIC, 0)
		local on = music()
		T.eq(on[1] and on[1][1], MUSIC, "on: the stream opens again (the volume waits for it)")
		T.eq(on[1] and on[1][3], track, "the same track")
		u:end_game()
		local ended = music()
		T.eq(#ended, 1, "the game's end")
		T.eq(ended[1][1], MUSIC_STOP, "stops the music")
	end)
end

-- The Center View trigger's camera pan (StartCameraPan 0x49be10) to a location by the map's bottom right corner: its
-- target, made for the original's 640 x 400 view, lies past a wider view's scroll limits; the pan reaches the view's
-- own target (the location at the view's centre, within the limits), its arrival releases the trigger's wait, and
-- after EndCameraPan 0x49be50 the keys scroll the map again. The original's view keeps the trigger's target.
function S.camera_pan_arrives_in_a_wide_view(T)
	local path = "generated/missions/terran/terran01.kpm"
	if not T.has(path) or not T.has(DATA .. "ui.kpd") then
		T.env.log("SKIP test_kp_ui.camera_pan_arrives_in_a_wide_view: no generated data")
		return
	end
	local events = require("game.simulation.events")
	local KEY_DOWN, KEY_UP, VK_LEFT = 0, 2, 0x25
	local game = require("tools.lua.oracle").start_mission(T.read, path, 0, 0, {})
	with_hud(T, game, path, { 1280, 720 }, function(hud)
		local ui = hud.ui
		local tx, ty = game.map_w_px - 641, game.map_h_px - 401 -- TrigActCenterView 0x4b6350's clamp at the corner
		local function pan(w, h, sx, sy)
			ui:view(w, h)
			ui:screen(sx or 0, sy or 0)
			ui:take_release()
			ui:game_event(events.CAMERA, tx, ty, 0)
			for k = 1, 400 do
				ui:scroll(320, 240)
				if ui:take_release() then
					local x, y = ui:take_camera()
					return k, x, y
				end
			end
		end
		local steps, x, y = pan(1280, 640)
		T.ok(steps ~= nil, "the wide view's pan arrives and releases the trigger's wait")
		T.eq(x, (game.map_w_px - 1280) - (game.map_w_px - 1280) % 8, "at the view's right limit")
		local bottom = game.map_h_px - 12 * 32 - (640 - 400) + 8 -- InitScreenScroll's limit for the taller view
		T.eq(y, bottom - bottom % 8, "at the view's bottom limit")
		ui:game_event(events.CAMERA, 0, 0, 1) -- the trigger's next run: EndCameraPan
		ui:input(KEY_DOWN, VK_LEFT, 0)
		for _ = 1, 20 do ui:scroll(320, 240) end
		ui:input(KEY_UP, VK_LEFT, 0)
		local left = ui:take_camera()
		T.ok(left and left < x, "the keys scroll the map again")
		steps, x, y = pan(640, 400)
		T.ok(steps ~= nil, "the original's view's pan arrives")
		T.eq(x, tx - tx % 8, "at the trigger's target")
		T.eq(y, ty - ty % 8)
		-- a view whose sizes are no multiples of 8 (a browser window's), scrolled by the keys to its limits: the limits
		-- keep the original's 8-pixel grid (the pan's steps are 8 pixels or more), so the pan from there arrives too
		ui:game_event(events.CAMERA, 0, 0, 1)
		ui:view(1283, 643)
		ui:screen(0, 0)
		ui:input(KEY_DOWN, 0x23, 0) -- End: down and left; then Page Down: down and right
		for _ = 1, 400 do ui:scroll(320, 240) end
		ui:input(KEY_UP, 0x23, 0)
		ui:input(KEY_DOWN, 0x22, 0)
		for _ = 1, 400 do ui:scroll(320, 240) end
		ui:input(KEY_UP, 0x22, 0)
		local ex, ey = ui:take_camera()
		T.eq(ex % 8, 0, "the right limit on the 8-pixel grid")
		T.eq(ey % 8, 0, "the bottom limit on the 8-pixel grid")
		T.ok(ex > game.map_w_px - 1283 - 8 and ey > game.map_h_px - 12 * 32 - 243 + 8 - 8, "at the limits")
		steps = pan(1283, 643, ex, ey)
		T.ok(steps ~= nil, "the pan from the limits arrives and releases the trigger's wait")
	end)
end

-- The interface's pending ends of the trigger waits that a save keeps (game/save.lua PRES): a trigger portrait's
-- milliseconds left and a camera pan's arrival; after a load replaced the game they end without releasing its waits.
function S.pending_releases_reach_the_save(T)
	local path = "generated/missions/terran/terran01.kpm"
	if not T.has(path) or not T.has(DATA .. "ui.kpd") then
		T.env.log("SKIP test_kp_ui.pending_releases_reach_the_save: no generated data")
		return
	end
	local events = require("game.simulation.events")
	local game = require("tools.lua.oracle").start_mission(T.read, path, 0, 0, {})
	with_hud(T, game, path, { 1280, 720 }, function(hud)
		local ui, now = hud.ui, 1016
		hud:frame(1280, 720, now, 0, nil, nil, 0, 0)
		T.eq(next(hud:pending_releases()), nil, "none at the start")
		local marine
		for n = 1, 1700 do
			if game.u.sprite[n] ~= -1 then marine = n break end
		end
		ui:game_event(events.PORTRAIT, game.u.type[marine], 3000, 0) -- TrigActTalkingPortrait: the wait's end in 3 s
		ui:game_event(events.CAMERA, game.map_w_px - 641, game.map_h_px - 401, 0)
		now = now + 1000
		hud:frame(1280, 720, now, 0, nil, nil)
		local pres = hud:pending_releases()
		T.ok(pres.portrait_ms and pres.portrait_ms > 1900 and pres.portrait_ms <= 2000, "the portrait's time left: "
			.. tostring(pres.portrait_ms))
		T.eq(pres.camera_ms, 0, "the pan's arrival")
		hud:game_loaded() -- a load
		T.eq(next(hud:pending_releases()), nil, "forgotten")
		local released = false
		for _ = 1, 100 do
			now = now + 100
			local _, _, r = hud:frame(1280, 720, now, 0, 640, 360)
			released = released or r
			ui:scroll(320, 240)
			released = released or ui:take_release()
		end
		T.ok(not released, "nothing releases the loaded game's waits")
	end)
end

-- A load during a placement (ResetPerGameState 0x444230, InitPlacementLayers 0x47d000): the placement closes with its
-- ghosts on the Geysers, and the interface leaves the mode.
function S.a_load_ends_the_placement(T)
	local path = "generated/scenarios/terran_addon.kpm"
	if not T.has(path) or not T.has(DATA .. "ui.kpd") then
		T.env.log("SKIP test_kp_ui.a_load_ends_the_placement: no generated data")
		return
	end
	local KEY_DOWN, KEY_UP, CHAR, SCV, GEYSER, GHOST = 0, 2, 15, 7, 188, 581
	local game = require("tools.lua.oracle").start_mission(T.read, path, 1, 0, {})
	with_hud(T, game, path, { 1280, 720 }, function(hud)
		local scv, geysers = nil, {}
		for n = 1, 1700 do
			if game.u.sprite[n] ~= -1 and game.u.type[n] == SCV and game.u.owner[n] == game.local_player then scv = scv or n end
			if game.u.sprite[n] ~= -1 and game.u.type[n] == GEYSER then geysers[#geysers + 1] = n end
		end
		local function ghosts()
			local w, k = game.img, 0
			for _, n in ipairs(geysers) do
				local i = w.spr.images[game.u.sprite[n]].head
				while i ~= -1 do
					if w.img.id[i] == GHOST then k = k + 1 end
					i = w.img.next[i]
				end
			end
			return k
		end
		game.minerals[game.local_player] = 1000
		game:select_locally({ scv })
		local now = 1316
		hud:frame(1280, 720, now, 1, 600, 300, 0, 0)
		-- B, then R twice: the build card shows by the first R, the second starts the Refinery's placement
		for _, k in ipairs({ { 66, 98 }, { 82, 114 }, { 82, 114 } }) do
			hud.ui:input(KEY_DOWN, k[1])
			hud.ui:input(CHAR, k[2])
			hud.ui:input(KEY_UP, k[1])
			for _ = 1, 3 do
				now = now + 300
				hud:frame(1280, 720, now, 0, 600, 300)
			end
		end
		T.eq(ghosts(), #geysers, "the placement's ghosts on the Geysers")
		hud:game_loaded()
		now = now + 300
		hud:frame(1280, 720, now, 0, 600, 300)
		T.eq(ghosts(), 0, "the load ends the placement and its ghosts")
		T.ok(not hud.ui:game_view_active(), "and the preview")
	end)
end

-- A right click on the command card's panel ends the command mode wherever the layout puts the panel (GameRightClick
-- 0x448eb0 at the card's rectangle 0x5033d0, CancelCommandMode 0x446050): a build card goes back to the selection's
-- own, then a placement ends, then the targeting.
function S.right_click_on_the_card_ends_the_mode(T)
	local path = "generated/scenarios/terran_addon.kpm"
	if not T.has(path) or not T.has(DATA .. "ui.kpd") then
		T.env.log("SKIP test_kp_ui.right_click_on_the_card_ends_the_mode: no generated data")
		return
	end
	local KEY_DOWN, KEY_UP, CHAR, IDLE, SCV = 0, 2, 15, 13, 7
	local game = require("tools.lua.oracle").start_mission(T.read, path, 1, 0, {})
	with_hud(T, game, path, { 1280, 720 }, function(hud)
		local play_hud = require("viewer.play_hud")
		local ui = hud.ui
		local scv
		for n = 1, 1700 do
			if game.u.sprite[n] ~= -1 and game.u.type[n] == SCV and game.u.owner[n] == game.local_player then
				scv = n
				break
			end
		end
		game.minerals[game.local_player] = 1000
		game:select_locally({ scv })
		local now = 1316
		hud:frame(1280, 720, now, 1, 600, 300, 0, 0)
		local function frames()
			for _ = 1, 3 do
				now = now + 300
				ui:input(IDLE) -- the pump's idle event: a hotkey's pressed button fires (SchedulePressRelease 0x4c3570)
				hud:frame(1280, 720, now, 0, 600, 300)
			end
		end
		local function keys(list)
			for _, k in ipairs(list) do
				ui:input(KEY_DOWN, k[1])
				ui:input(CHAR, k[2])
				ui:input(KEY_UP, k[1])
				frames()
			end
		end
		local card
		for _, d in ipairs(ui:open()) do
			if ui:control(d).bin_name:lower():find("rez\\statbtn", 1, true) then card = d end
		end
		T.ok(card ~= nil and hud.arrangement.name == "wide", "the wide layout's command card")
		-- on a shown button: an empty slot is the card dialog's own (DialogDefaultInteract 0x4c1470)
		local function right_click()
			local place, d = hud.arrangement.places[card], ui:control(card)
			local c, k = d.child, nil
			while c >= 0 do
				k = ui:control(c)
				if bit.band(k.flags, 8) ~= 0 and k.id >= 1 and k.id <= 9 then break end
				c = k.next
			end
			T.ok(c >= 0, "a shown button")
			local x = place.x + math.floor((k.left + k.right) / 2) -- the button's rectangle is the dialog's
			local y = place.y + math.floor((k.top + k.bottom) / 2)
			hud:input(play_hud.MOUSE_MOVE, x, y, 0, 0)
			hud:input(play_hud.RIGHT_DOWN, x, y, 0, 0)
			hud:input(play_hud.RIGHT_UP, x, y, 0, 0)
			frames()
		end
		local own = ui:hud_state().card_set
		keys({ { 66, 98 } }) -- B: the build card
		T.ok(ui:hud_state().card_set ~= own, "the build card")
		-- the console art off the panels at the card rectangle's logical point: the logical screen's coordinates, not
		-- the card's (the wide layout's card lies elsewhere), and the normal mode's right click over the art does nothing
		ui:over_console(true)
		ui:input(play_hud.MOUSE_MOVE, 560, 400)
		ui:input(play_hud.RIGHT_DOWN, 560, 400)
		ui:input(play_hud.RIGHT_UP, 560, 400)
		T.ok(ui:hud_state().card_set ~= own, "the console art's right click keeps the build card")
		right_click()
		T.eq(ui:hud_state().card_set, own, "back to the selection's card")
		keys({ { 66, 98 }, { 82, 114 } }) -- B, R: the Refinery's placement
		T.ok(ui:game_view_active(), "the placement")
		right_click()
		T.ok(not ui:game_view_active(), "the placement ends")
		keys({ { 77, 109 } }) -- M: the move's targeting
		T.ok(ui:hud_state().targeting ~= 0, "the targeting")
		right_click()
		T.eq(ui:hud_state().targeting, 0, "the targeting ends")
	end)
end

-- Portrait 82 (the Flag's) takes its sprite owner's colour (ShowPortrait 0x4a6f10, MapPlayerColor 0x41d560): 82 on for
-- the players' colours, 0x62 on for a colour of 8 or more.
function S.flag_portrait_takes_its_owners_colour(T)
	local path = "generated/missions/terran/terran01.kpm"
	if not T.has(path) or not T.has(DATA .. "ui.kpd") then
		T.env.log("SKIP test_kp_ui.flag_portrait_takes_its_owners_colour: no generated data")
		return
	end
	local FLAG = 215
	local game = require("tools.lua.oracle").start_mission(T.read, path, 0, 0, {})
	with_hud(T, game, path, { 1280, 720 }, function(hud)
		local units = {}
		for n = 1, 1700 do
			if game.u.sprite[n] ~= -1 and game.u.owner[n] == game.local_player and game.u.hp[n] > 0 then
				units[#units + 1] = n
			end
		end
		T.ok(#units >= 2, "two units of the local player's")
		T.eq(game.dat.units.portrait[FLAG], 82)
		local flag, other = units[1], units[2]
		game.u.type[flag] = FLAG
		local now = 1016
		local function portrait_of(owner)
			game.img.spr.owner[game.u.sprite[flag]] = owner
			game:select_locally({ other })
			now = now + 100
			hud:frame(1280, 720, now, 1, nil, nil, 0, 0)
			game:select_locally({ flag })
			now = now + 100
			hud:frame(1280, 720, now, 1, nil, nil, 0, 0)
			return hud.ui:portrait_state().portrait
		end
		T.eq(portrait_of(0), 82, "player 1's colour")
		T.eq(portrait_of(5), 87, "player 6's colour")
		T.eq(portrait_of(11), 0x62 + 11, "a neutral colour's row")
	end)
end

-- The status panel's tooltip follows a new layout under a still mouse (RefreshStatusHoverTooltip 0x4a2b70): a Marine's
-- weapon slot hovered shows its tooltip, the Supply Depot's layout has no such slot and the tooltip goes.
function S.status_tooltip_follows_the_panels_layout(T)
	local path = "generated/missions/terran/terran01.kpm"
	if not T.has(path) or not T.has(DATA .. "ui.kpd") then
		T.env.log("SKIP test_kp_ui.status_tooltip_follows_the_panels_layout: no generated data")
		return
	end
	local MARINE, DEPOT = 0, 109
	local game = require("tools.lua.oracle").start_mission(T.read, path, 0, 0, {})
	with_hud(T, game, path, { 1280, 720 }, function(hud)
		local play_hud = require("viewer.play_hud")
		local ui = hud.ui
		local marine, depot
		for n = 1, 1700 do
			if game.u.sprite[n] ~= -1 and game.u.owner[n] == game.local_player and game.u.hp[n] > 0 then
				if game.u.type[n] == MARINE then marine = marine or n end
				if game.u.type[n] == DEPOT then depot = depot or n end
			end
		end
		T.ok(marine and depot, "a Marine and a Supply Depot")
		local now = 1016
		local function select(n)
			game:select_locally({ n })
			now = now + 100
			hud:frame(1280, 720, now, 1, nil, nil, 0, 0)
		end
		local panel
		for _, d in ipairs(ui:open()) do
			if ui:control(d).bin_name:lower() == "rez\\statdata.bin" then panel = d end
		end
		local function shown()
			local out, c = {}, ui:control(panel).child
			while c >= 0 do
				local k = ui:control(c)
				if bit.band(k.flags, 8) ~= 0 then out[c] = k end
				c = k.next
			end
			return out
		end
		select(depot)
		local depot_shows = shown()
		select(marine)
		local place, slot = hud.arrangement.places[panel], nil
		for c, k in pairs(shown()) do
			if not depot_shows[c] and not slot then
				local x = place.x + math.floor((k.left + k.right) / 2) -- the control's rectangle is the dialog's
				local y = place.y + math.floor((k.top + k.bottom) / 2)
				hud:input(play_hud.MOUSE_MOVE, x, y, 0, 0)
				if ui:tooltip() then slot = c end
			end
		end
		T.ok(slot ~= nil, "a slot of the Marine's with a tooltip, none of the Depot's")
		select(depot)
		T.eq(ui:tooltip(), nil, "the Depot's layout: no tooltip under the still mouse")
	end)
end

-- The leaderboard's expansion (command 40053, Ctrl+R: ToggleLeaderboardExpanded 0x4a6260) hides the rows after the
-- first at once and shows them again.
function S.leaderboard_key_folds_its_rows(T)
	local path = "generated/scenarios/trigger_outcome.kpm"
	if not T.has(path) or not T.has(DATA .. "ui.kpd") then
		T.env.log("SKIP test_kp_ui.leaderboard_key_folds_its_rows: no generated data")
		return
	end
	local COMMAND, TOGGLE = 16, 40053
	local oracle = require("tools.lua.oracle")
	local game = oracle.start_mission(T.read, path, 0, 0, {})
	local run = oracle.runner(game)
	with_hud(T, game, path, { 1280, 720 }, function(hud)
		local ui = hud.ui
		local board
		for _, d in ipairs(ui:open()) do
			if ui:control(d).bin_name:lower() == "rez\\statlb.bin" then board = d end
		end
		T.ok(board ~= nil, "the leaderboard")
		local function shown()
			local n, c = 0, ui:control(board).child
			while c >= 0 do
				local k = ui:control(c)
				if bit.band(k.flags, 8) ~= 0 then n = n + 1 end
				c = k.next
			end
			return n
		end
		local now = 1016
		for step = 2, 40 do
			run.to(step)
			now = now + 42
			hud:frame(1280, 720, now, 1, nil, nil, 0, 0)
		end
		local expanded = shown()
		ui:input(COMMAND, TOGGLE, 0)
		local folded = shown()
		T.ok(folded < expanded, ("folded: %d of %d controls shown"):format(folded, expanded))
		ui:input(COMMAND, TOGGLE, 0)
		T.eq(shown(), expanded, "expanded again")
	end)
end

-- The mission warp (HandleMissionWarpCheat 0x40e030 under Ophelia's cheat): "terran 2" typed in the text box ends the
-- game as Quit Mission does and the glue's next entry goes to that mission's ready room (SetPendingGlueMission
-- 0x457e10, RunGlueLoop 0x457510); without the cheat, or past the campaign's rows, the line is nothing.
function S.mission_warp_goes_to_the_ready_room(T)
	local path = "generated/missions/terran/terran01.kpm"
	if not T.has(path) or not T.has(DATA .. "ui.kpd") or not T.has(DATA .. "campaign.kpd") then
		T.env.log("SKIP test_kp_ui.mission_warp_goes_to_the_ready_room: no generated data")
		return
	end
	local OPHELIA, QUIT_MISSION, MISSION, TERRAN_ROOM, CAMPAIGN = 0x200, 4, 5, 7, 6
	local game = require("tools.lua.oracle").start_mission(T.read, path, 0, 0, {})
	with_hud(T, game, path, { 1280, 720 }, function(hud)
		local play_hud = require("viewer.play_hud")
		local ui = hud.ui
		local now = 1016
		hud:frame(1280, 720, now, 1, nil, nil, 0, 0)
		local function line(text)
			for _, t in ipairs({ "\r", text, "\r" }) do
				if t == "\r" then
					play_hud.key(ui, true, 0x0d, false)
					play_hud.key(ui, false, 0x0d, false)
				else
					play_hud.text(ui, t)
				end
				now = now + 50
				hud:frame(1280, 720, now, 0, nil, nil)
			end
		end
		line("terran 2")
		T.eq(ui:game_exit(), 0, "no warp without Ophelia's cheat")
		game.cheats = OPHELIA
		line("terran 99")
		T.eq(ui:game_exit(), 0, "past the campaign's rows: nothing")
		line("terran 4294967298")
		T.eq(ui:game_exit(), 0, "a number past 32 bits: nothing (not row 2)")
		line("terran 2")
		T.eq(ui:game_exit(), QUIT_MISSION, "the game ends as Quit Mission")
		ui:end_game()
		ui:menus_enter(CAMPAIGN)
		local p = ui:menus_poll()
		T.ok(p ~= nil, "the glue's entry ends at once")
		T.eq(p.action, MISSION, "with the mission")
		T.eq(p.state, TERRAN_ROOM, "for the Terran ready room")
		T.eq(p.expansion, 0)
		-- the original campaign's Terran rows from mission 0 (Boot Camp): the third row neither a cinematic nor hidden
		local tabl = require("game.data.kpf").parse(T.read(DATA .. "campaign.kpd"), "campaign.kpd"):section("TABL")
		local at, rows = 2, nil
		for _ = 1, tabl:byte(1) do
			local n = tabl:byte(at + 2) + tabl:byte(at + 3) * 256
			if tabl:byte(at) == 0 and tabl:byte(at + 1) == 1 then rows = { at = at + 4, n = n } end
			at = at + 4 + 8 * n
		end
		local k, left = 0, 2
		while true do
			local r = rows.at + 8 * k
			local cinematic, hidden = tabl:byte(r + 4) + tabl:byte(r + 5) * 256, tabl:byte(r + 7)
			if cinematic == 0 and hidden == 0 then
				if left == 0 then break end
				left = left - 1
			end
			k = k + 1
		end
		local r = rows.at + 8 * k
		T.eq(p.mission, tabl:byte(r + 2) + tabl:byte(r + 3) * 256, "the row's mission")
		ui:menus_enter(CAMPAIGN)
		T.eq(ui:menus_poll(), nil, "once: the next entry is the campaign menu")
	end)
end

-- The window's activity (WM_ACTIVATEAPP): a held arrow key scrolls the map; the focus goes and its release never
-- comes, the key bytes are cleared and the map stands (UpdateMapScroll 0x49bf10 while inactive, then no key held).
function S.inactive_window_stops_the_scroll(T)
	local path = "generated/missions/terran/terran01.kpm"
	if not T.has(path) or not T.has(DATA .. "ui.kpd") then
		T.env.log("SKIP test_kp_ui.inactive_window_stops_the_scroll: no generated data")
		return
	end
	local KEY_DOWN, VK_RIGHT = 0, 0x27
	local game = require("tools.lua.oracle").start_mission(T.read, path, 0, 0, {})
	with_hud(T, game, path, { 1280, 720 }, function(hud)
		local ui = hud.ui
		ui:view(1280, 640)
		ui:screen(0, 0)
		ui:take_camera()
		local function scrolled()
			for _ = 1, 10 do ui:scroll(320, 240) end -- the cursor at the logical screen's centre: no edge
			return ui:take_camera()
		end
		ui:input(KEY_DOWN, VK_RIGHT, 0)
		T.ok(scrolled() ~= nil, "the held key scrolls")
		ui:active(false)
		T.eq(scrolled(), nil, "inactive: the map stands")
		ui:active(true)
		T.eq(scrolled(), nil, "active again: the key's byte went with the focus")
	end)
end

-- "Show Tips at Startup" opens the tips at a new game's start (RunGame 0x445310), not at a saved game's
-- (WasLoadedFromSave 0x499870), and the mark goes with that console (ClearLoadedFromSave 0x499880).
function S.tips_wait_for_a_new_game(T)
	local path = "generated/missions/terran/terran01.kpm"
	if not T.has(path) or not T.has(DATA .. "ui.kpd") then
		T.env.log("SKIP test_kp_ui.tips_wait_for_a_new_game: no generated data")
		return
	end
	local TIPS = 0x100
	local oracle = require("tools.lua.oracle")
	local function console(from_save)
		local game = oracle.start_mission(T.read, path, 0, 0, {})
		local u = new_ui(T)
		u:strings(T.read(DATA .. "strings.kpd"))
		u:font_set(true)
		u:theme(RACE_GRPS[1])
		u:now(1000)
		u:options(bit.bor(u:options(), TIPS))
		if from_save then u:from_save() end
		u:hud(game)
		local d = u:modeless()
		return u, d >= 0 and u:control(d).bin_name:lower() or nil, game
	end
	local _, fresh = console(false)
	T.eq(fresh, "rez\\tips_dlg.bin", "a new game's tips")
	local u, loaded = console(true)
	T.eq(loaded, nil, "none for a saved game")
	u:end_game()
	u:hud(oracle.start_mission(T.read, path, 0, 0, {}))
	local d = u:modeless()
	T.eq(d >= 0 and u:control(d).bin_name:lower() or nil, "rez\\tips_dlg.bin", "the next new game's again")
end

-- A type selection (Ctrl+click: FilterSelectableUnits 0x4bc5b0) takes a unit cloaked for free (IsCloakingForFree
-- 0x4a04e0: an Arbiter's field, status 0x800) with its uncloaked kind; a unit cloaked on its own stays out.
function S.type_selection_takes_the_units_cloaked_for_free(T)
	local path = "generated/missions/terran/terran01.kpm"
	if not T.has(path) or not T.has(DATA .. "ui.kpd") then
		T.env.log("SKIP test_kp_ui.type_selection_takes_the_units_cloaked_for_free: no generated data")
		return
	end
	local MARINE, CLOAKED, FREE_CLOAK = 0, 0x100, 0x800
	local game = require("tools.lua.oracle").start_mission(T.read, path, 0, 0, {})
	with_hud(T, game, path, { 1280, 720 }, function(hud)
		local play_hud = require("viewer.play_hud")
		local marines = {}
		for n = 1, 1700 do
			if game.u.sprite[n] ~= -1 and game.u.owner[n] == game.local_player and game.u.type[n] == MARINE
				and game.u.hp[n] > 0 then
				marines[#marines + 1] = n
			end
		end
		T.ok(#marines >= 3, "three Marines")
		local a, b, c = marines[1], marines[2], marines[3]
		local x, y = game.u.x[a], game.u.y[a]
		local max_y = game.map_h_px - 12 * 32 - (640 - 400) + 8
		local cx, cy = math.max(0, x - 640), math.max(0, math.min(y - 300, max_y - max_y % 8))
		hud:frame(1280, 720, 1000, 0, nil, nil, 0, 0)
		hud:frame(1280, 720, 1016, 0, nil, nil, cx, cy)
		local function cloak(n, bits)
			game.u.status[n] = bit.bor(game.u.status[n], bits)
			game.u.visibility[n] = 0xff -- detected: the local player sees it
		end
		cloak(b, CLOAKED + FREE_CLOAK)
		cloak(c, CLOAKED)
		local px, py = x - cx, y - cy
		for _, n in ipairs({ b, c }) do
			local vx, vy = game.u.x[n] - cx, game.u.y[n] - cy
			T.ok(vx >= 0 and vx < 1280 and vy >= 0 and vy < 640, "in the view")
		end
		hud:input(play_hud.MOUSE_MOVE, px, py, cx, cy, false, true, false)
		hud:input(play_hud.LEFT_DOWN, px, py, cx, cy, false, true, false)
		hud:input(play_hud.LEFT_UP, px, py, cx, cy, false, true, false)
		local chosen = {}
		for k = 0, 11 do chosen[game.local_selection[k]] = true end
		T.ok(chosen[a], "the clicked Marine")
		T.ok(chosen[b], "the Marine cloaked for free")
		T.ok(not chosen[c], "not the Marine cloaked on its own")
	end)
end

-- The portraits' mode (SetPortraitMode 0x4a6e90) shows the status unit's portrait again in the new mode: its idle
-- movie opens anew (ShowPortrait 0x4a6f10(1)), with the idle variant's draw of the portrait generator.
function S.portrait_mode_shows_the_portrait_again(T)
	local path = "generated/missions/terran/terran01.kpm"
	if not T.has(path) or not T.has(DATA .. "ui.kpd") then
		T.env.log("SKIP test_kp_ui.portrait_mode_shows_the_portrait_again: no generated data")
		return
	end
	local game = require("tools.lua.oracle").start_mission(T.read, path, 0, 0, {})
	with_hud(T, game, path, { 1280, 720 }, function(hud)
		local ui = hud.ui
		local unit
		for n = 1, 1700 do
			if game.u.sprite[n] ~= -1 and game.u.owner[n] == game.local_player and game.u.hp[n] > 0 then unit = n break end
		end
		game:select_locally({ unit })
		local now = 1016
		for _ = 1, 20 do
			now = now + 100
			hud:frame(1280, 720, now, 1, nil, nil, 0, 0)
		end
		local before = ui:portrait_state()
		T.ok(before.open == 1 and before.frame > 0, "the idle movie plays")
		T.eq(ui:portrait_mode(), 2, "animated")
		ui:portrait_mode(2)
		local after = ui:portrait_state()
		T.eq(after.open, 1, "the movie again")
		T.ok(after.frame < before.frame, "from its start")
		T.ok(after.rng_x ~= before.rng_x or after.rng_y ~= before.rng_y or after.rng_z ~= before.rng_z,
			"the idle variant drawn again")
		ui:portrait_mode(0)
		T.eq(ui:portrait_mode(), 0, "none")
	end)
end

-- A paused game's minimap takes no press (MinimapImageProc 0x46d6a0 at IsGamePaused 0x47cb60): a left press leaves
-- the view, a right press orders nothing; unpaused both work.
function S.paused_minimap_takes_no_press(T)
	local path = "generated/missions/terran/terran01.kpm"
	if not T.has(path) or not T.has(DATA .. "ui.kpd") then
		T.env.log("SKIP test_kp_ui.paused_minimap_takes_no_press: no generated data")
		return
	end
	local game = require("tools.lua.oracle").start_mission(T.read, path, 0, 0, {})
	with_hud(T, game, path, { 1280, 720 }, function(hud)
		local play_hud = require("viewer.play_hud")
		local unit
		for n = 1, 1700 do
			if game.u.sprite[n] ~= -1 and game.u.owner[n] == game.local_player and game.u.hp[n] > 0 then unit = n break end
		end
		game:select_locally({ unit })
		local cx, cy = hud:frame(1280, 720, 1016, 1, nil, nil, 0, 0)
		local minimap
		for _, d in ipairs(hud.ui:open()) do
			if hud.ui:control(d).bin_name:lower() == "rez\\minimap.bin" then minimap = d end
		end
		local place, k = hud.arrangement.places[minimap], hud.ui:control(minimap)
		local x, y = place.x + 70 - k.left, place.y + 470 - k.top -- the minimap image's bottom rows
		local function press(down, up)
			hud:input(play_hud.MOUSE_MOVE, x, y, cx, cy)
			local commands, vx, vy = hud:input(down, x, y, cx, cy)
			hud:input(up, x, y, cx, cy)
			return commands, vy
		end
		game.paused = 1
		local _, moved = press(play_hud.LEFT_DOWN, play_hud.LEFT_UP)
		T.eq(moved, nil, "paused: the view stays")
		local orders = press(play_hud.RIGHT_DOWN, play_hud.RIGHT_UP)
		T.eq(#orders, 0, "paused: no order")
		game.paused = 0
		_, moved = press(play_hud.LEFT_DOWN, play_hud.LEFT_UP)
		T.ok(moved and moved > 0, "the view moves down")
		orders = press(play_hud.RIGHT_DOWN, play_hud.RIGHT_UP)
		T.ok(#orders > 0, "the right press orders the selection")
	end)
end

-- A click on the portrait centres the view on its speaker (PortraitControlInteract 0x4a6d30: user code 2 with a unit,
-- CenterScreenOnUnit 0x422b10): the selected unit's portrait.
function S.portrait_click_centres_its_unit(T)
	local path = "generated/missions/terran/terran01.kpm"
	if not T.has(path) or not T.has(DATA .. "ui.kpd") then
		T.env.log("SKIP test_kp_ui.portrait_click_centres_its_unit: no generated data")
		return
	end
	local game = require("tools.lua.oracle").start_mission(T.read, path, 0, 0, {})
	with_hud(T, game, path, { 1280, 720 }, function(hud)
		local play_hud = require("viewer.play_hud")
		local unit
		for n = 1, 1700 do
			if game.u.sprite[n] ~= -1 and game.u.owner[n] == game.local_player then unit = n break end
		end
		T.ok(unit ~= nil, "a unit of the local player")
		game:select_locally({ unit })
		local now = 1016
		local cx, cy = hud:frame(1280, 720, now, 1, nil, nil, 0, 0)
		now = now + 100
		hud:frame(1280, 720, now, 0, nil, nil, 0, 0)
		hud.ui:take_camera()
		local portrait
		for _, d in ipairs(hud.ui:open()) do
			if hud.ui:control(d).bin_name:lower() == "rez\\statport.bin" then portrait = d end
		end
		local place, d = hud.arrangement.places[portrait], hud.ui:control(portrait)
		local c, k = d.child, nil
		while c >= 0 do
			k = hud.ui:control(c)
			if k.id == 1 then break end
			c = k.next
		end
		local x = place.x + math.floor((k.left + k.right) / 2) -- the control's rectangle is the dialog's
		local y = place.y + math.floor((k.top + k.bottom) / 2)
		hud:input(play_hud.MOUSE_MOVE, x, y, 0, 0)
		hud:input(play_hud.LEFT_DOWN, x, y, 0, 0)
		local _, mx, my = hud:input(play_hud.LEFT_UP, x, y, 0, 0)
		-- the unit's tile at the wide view's centre, within the view's scroll limits (SetScreenPosition 0x40b450)
		local s = game.u.sprite[unit]
		local tx, ty = math.floor(game.img.spr.x[s] / 32), math.floor(game.img.spr.y[s] / 32)
		local max_x, max_y = game.map_w_px - 1280, game.map_h_px - 12 * 32 - (640 - 400) + 8
		local function clamp(v, hi) v = math.max(0, math.min(v, hi)) return v - v % 8 end
		T.eq(mx, clamp((tx - 1280 / 32 / 2) * 32, max_x), "the view's x")
		T.eq(my, clamp((ty - 640 / 32 / 2) * 32, max_y), "the view's y")
		T.ok(mx ~= 0 or my ~= 0, "the view moved")
	end)
end

-- The portrait's hotkey (the space, statport.bin's control -10; PortraitHotkey 0x4a6760) moves the view to the last
-- transmission: a trigger portrait at a unit keeps the original's view's screen with the unit at its centre (0x527014),
-- which a wide view moves so the unit stays at its centre.
function S.portrait_hotkey_centres_the_last_transmission(T)
	local path = "generated/missions/terran/terran01.kpm"
	if not T.has(path) or not T.has(DATA .. "ui.kpd") then
		T.env.log("SKIP test_kp_ui.portrait_hotkey_centres_the_last_transmission: no generated data")
		return
	end
	local events = require("game.simulation.events")
	local CHAR, SPACE = 15, 0x20
	local game = require("tools.lua.oracle").start_mission(T.read, path, 0, 0, {})
	with_hud(T, game, path, { 1280, 720 }, function(hud)
		local ui = hud.ui
		ui:view(1280, 640)
		local unit
		for n = 1, 1700 do
			local x, y = game.u.x[n], game.u.y[n]
			if game.u.sprite[n] ~= -1 and x > 1000 and y > 700 and x < game.map_w_px - 700 and y < game.map_h_px - 400 then
				unit = n
				break
			end
		end
		T.ok(unit ~= nil, "a unit away from the map's edges")
		ui:game_event(events.PORTRAIT, game.u.type[unit], 2000, unit)
		ui:screen(0, 0)
		ui:take_camera()
		ui:input(CHAR, SPACE, 0)
		local x, y = ui:take_camera()
		local sx, sy = game.img.spr.x[game.u.sprite[unit]], game.img.spr.y[game.u.sprite[unit]]
		T.eq(x, (sx - 640) - (sx - 640) % 8, "the unit at the wide view's centre")
		T.eq(y, (sy - 320) - (sy - 320) % 8)
	end)
end

-- The text lines are the game screen's (DrawTextLines 0x46acd0 in its layer's callback 0x445ea0): the drag box drawn
-- after them lies over them, and the console art over both; a short narrow screen, whose minimap art rises higher than
-- the original's, lifts them as much (presentation/ui/layouts.lua).
function S.canvas_draws_the_text_lines_with_the_game_view(T)
	local path = "generated/missions/terran/terran01.kpm"
	if not T.has(path) or not T.has(DATA .. "ui.kpd") then
		T.env.log("SKIP test_kp_ui.canvas_draws_the_text_lines_with_the_game_view: no generated data")
		return
	end
	local events = require("game.simulation.events")
	local game = require("tools.lua.oracle").start_mission(T.read, path, 0, 0, {})
	with_hud(T, game, path, { 1280, 720 }, function(hud)
		local play_hud = require("viewer.play_hud")
		local c, now = hud.canvas.canvas, 1016
		local function frame(w, h, x, y)
			now = now + 16
			return hud:frame(w, h, now, 0, x, y, 0, 0)
		end
		-- the text lines' pixels on a w x h screen after an event (the lines before expired: 7 s), and those of them on
		-- the console art's opaque pixels without the line
		local function line_pixels(w, h, kind, a, b, c3)
			now = now + 8000
			frame(w, h)
			local art = {}
			for y = 0, h - 1 do
				for x = 0, math.min(w, 640) - 1 do
					if select(2, c:pixel(x, y)) == 255 then art[y * 65536 + x] = true end
				end
			end
			hud:event(kind, a, b, c3)
			frame(w, h)
			local shown, on_art, bottom = 0, 0, -1
			for y = 0, h - 1 do
				for x = 0, math.min(w, 640) - 1 do
					if select(2, c:pixel(x, y)) == 254 then
						shown, bottom = shown + 1, y
						if art[y * 65536 + x] then on_art = on_art + 1 end
					end
				end
			end
			return shown, on_art, bottom
		end
		local cx, cy = frame(1280, 720)
		hud:event(events.ERROR, game.local_player, 1, 0)
		frame(1280, 720)
		local hit
		for y = 0x127, 0x127 + 12 do
			for x = 10, 400 do
				if select(2, c:pixel(x, y)) == 254 then hit = { x, y } break end
			end
			if hit then break end
		end
		T.ok(hit ~= nil, "the error line drawn")
		local line_index = c:pixel(hit[1], hit[2])
		hud:input(play_hud.MOUSE_MOVE, hit[1] - 40, hit[2], cx, cy)
		hud:input(play_hud.LEFT_DOWN, hit[1] - 40, hit[2], cx, cy)
		hud:input(play_hud.MOUSE_MOVE, hit[1] + 40, hit[2] + 30, cx, cy)
		frame(1280, 720, hit[1] + 40, hit[2] + 30)
		local box_index = c:pixel(hit[1], hit[2])
		T.ok(box_index ~= line_index, "the box's top edge over the line's pixel")
		T.eq(box_index, c:pixel(hit[1] + 40, hit[2] + 15), "in the box's colour")
		hud:input(play_hud.LEFT_UP, hit[1] + 40, hit[2] + 30, cx, cy)
		-- the order of the composition: the lines, the game view's overlays, then the console art
		local real, calls = c, {}
		hud.canvas.canvas = setmetatable({}, { __index = function(_, name)
			return function(_, ...)
				if name == "lines" or name == "game_view" or name == "console" then calls[#calls + 1] = name end
				return real[name](real, ...)
			end
		end })
		hud:event(events.ERROR, game.local_player, 1, 0)
		frame(1280, 720)
		hud.canvas.canvas = real
		T.eq(table.concat(calls, " ", 1, 3), "lines game_view console", "the lines under the box and the console")
		-- a ring line (a unit that could not be placed: the ring's bottom row) on a short narrow screen stays whole,
		-- above the console's art
		local wide = line_pixels(1280, 720, events.UNIT_UNPLACED, 0, 10, 20)
		local shown, on_art, bottom = line_pixels(540, 600, events.UNIT_UNPLACED, 0, 10, 20)
		T.ok(hud.arrangement.top < 290, "the narrow screen's minimap art rises higher than the original's")
		T.eq(shown, wide, "the ring line whole")
		T.eq(on_art, 0, "none of it over the art")
		T.ok(bottom < hud.arrangement.top, "above the console's art")
	end)
end

-- The placement's preview (placebox.cpp): the hotkeys B and S of an SCV start a Supply Depot's placement, and the
-- canvas's game view shows its ghost and its footprint's cells stippled (StipplePlacementCell 0x405c12: half of each
-- 32 x 32 cell) at the tile under the mouse, which follows the mouse and goes with the placement.
function S.canvas_draws_the_placement_preview(T)
	local path = "generated/scenarios/build_terran.kpm"
	if not T.has(path) or not T.has(DATA .. "ui.kpd") then
		T.env.log("SKIP test_kp_ui.canvas_draws_the_placement_preview: no generated data")
		return
	end
	local KEY_DOWN, KEY_UP, CHAR, SCV = 0, 2, 15, 7
	local game = require("tools.lua.oracle").start_mission(T.read, path, 946684819, 0, {})
	with_hud(T, game, path, { 1280, 720 }, function(hud)
		local play_hud = require("viewer.play_hud")
		local scv
		for n = 1, 1700 do
			if game.u.type[n] == SCV and game.u.owner[n] == game.local_player then scv = n break end
		end
		T.ok(scv ~= nil, "an SCV of the local player")
		game.minerals[game.local_player] = 1000
		game:select_locally({ scv })
		local cx, cy = math.max(0, game.u.x[scv] - 640), math.max(0, game.u.y[scv] - 320)
		local now = 1016
		local function frame(steps, x, y)
			now = now + 300 -- each press releases (SchedulePressRelease 0x4c3570) before the next
			hud:frame(1280, 720, now, steps, x, y, cx, cy)
		end
		frame(1, 600, 300)
		for _, k in ipairs({ { 66, 98 }, { 83, 115 } }) do
			hud.ui:input(KEY_DOWN, k[1])
			hud.ui:input(CHAR, k[2])
			hud.ui:input(KEY_UP, k[1])
			frame(0, 600, 300)
		end
		local c = hud.canvas.canvas
		local function stippled(x0, y0, x1, y1)
			local counts, best, colour = {}, 0, nil
			for y = y0, y1 do
				for x = x0, x1 do
					local i, a = c:pixel(x, y)
					if a == 254 then
						counts[i] = (counts[i] or 0) + 1
						if counts[i] > best then best, colour = counts[i], i end
					end
				end
			end
			return best, colour
		end
		hud:input(play_hud.MOUSE_MOVE, 700, 300, cx, cy)
		frame(0, 700, 300)
		T.ok(hud.ui:game_view_active(), "the preview shows")
		-- the text box's prompt (SetTextboxPrompt 0x4b38c0: control 5, network.tbl 0x40) while the text box is closed
		local function prompt()
			for _, d in ipairs(hud.ui:open()) do
				if hud.ui:control(d).bin_name:lower():find("textbox", 1, true) then
					local c = hud.ui:control(d).child
					while c >= 0 do
						local k = hud.ui:control(c)
						if k.id == 5 then return bit.band(k.flags, 8) ~= 0 and k.text or nil end
						c = k.next
					end
				end
			end
		end
		T.eq(prompt(), "Select Location", "the placement's prompt")
		-- the depot's 3 x 2 cells at the tile under the mouse, half of their pixels in one stipple colour
		local tx, ty = math.floor((700 + cx) / 32), math.floor((300 + cy) / 32)
		local x0, y0 = tx * 32 - cx, ty * 32 - cy
		T.eq(stippled(x0, y0, x0 + 3 * 32 - 1, y0 + 2 * 32 - 1), 3 * 2 * 512, "the cells stippled")
		hud:input(play_hud.MOUSE_MOVE, 300, 200, cx, cy)
		frame(0, 300, 200)
		T.ok(stippled(x0 + 8, y0 + 8, x0 + 3 * 32 - 9, y0 + 2 * 32 - 9) < 100, "the preview followed the mouse")
		hud:input(play_hud.RIGHT_DOWN, 300, 200, cx, cy) -- CancelPlacementRightClick 0x448760
		hud:input(play_hud.RIGHT_UP, 300, 200, cx, cy)
		frame(0, 300, 200)
		T.ok(not hud.ui:game_view_active(), "the preview goes with the placement")
		T.eq(prompt(), nil, "and its prompt")
		T.eq(stippled(250, 150, 450, 330), 0, "nothing left on the canvas")
	end)
end

-- The game menu composited onto the backdrop (BlitDialogToScreen 0x4c5630) lies under the console: the console art's
-- holes keep it, in a layout whose console starts below the menu's rectangle too.
function S.canvas_keeps_a_menu_under_the_console_art(T)
	local path = "generated/missions/terran/terran01.kpm"
	if not T.has(path) or not T.has(DATA .. "ui.kpd") then
		T.env.log("SKIP test_kp_ui.canvas_keeps_a_menu_under_the_console_art: no generated data")
		return
	end
	local canvas = require("presentation.ui.canvas")
	local game = require("tools.lua.oracle").start_mission(T.read, path, 0, 0, {})
	local u = new_ui(T)
	u:strings(T.read(DATA .. "strings.kpd"))
	u:font_set(true)
	u:theme(RACE_GRPS[1])
	local now = 1000
	u:now(now)
	u:hud(game)
	u:window_key(true, 0x79, false) -- F10: the game menu
	u:window_key(false, 0x79, false)
	for _ = 1, 10 do
		now = now + 16
		u:now(now)
		u:tick(now)
		u:input(13)
		u:present()
	end
	local d = u:modeless()
	T.eq(u:control(d).bin_name:lower(), "rez\\gamemenu.bin")
	local panels = {}
	for _, o in ipairs(u:open()) do
		local k = u:control(o)
		table.insert(panels, 1, { dialog = o, bin = k.bin_name, rect = { k.left, k.top, k.right, k.bottom },
			backdrop = bit.band(k.flags, 0x10000000) ~= 0 })
	end
	local c = canvas.new(native_only.core, u)
	local a = select(5, c:compose(1024, 576, panels))
	local place, bitmap, w, h = a.places[d], u:bitmap(d)
	local drawn = 0
	for y = 0, h - 1, 4 do
		for x = 0, w - 1, 4 do
			local _, alpha = c.canvas:pixel(place.x + x, place.y + y)
			if bitmap:byte(y * w + x + 1) ~= 0 and alpha ~= 0 then drawn = drawn + 1 end
			if bitmap:byte(y * w + x + 1) ~= 0 then drawn = drawn - 1 end
		end
	end
	T.eq(drawn, 0) -- every pixel of the menu shows (opaque or blended over the world)
end

-- The game menu's translucent background (BlitSingleColourBlend 0x4ca9b0 onto the screen buffer) blends with the
-- text lines under it as the original's screen holds them: on the canvas, not with the world in the render pass.
function S.canvas_blends_a_menu_over_the_text_lines(T)
	local path = "generated/missions/terran/terran01.kpm"
	if not T.has(path) or not T.has(DATA .. "ui.kpd") then
		T.env.log("SKIP test_kp_ui.canvas_blends_a_menu_over_the_text_lines: no generated data")
		return
	end
	local UNPLACED, MARINE, IDLE, BLEND_A = 3, 0, 13, 128
	local layout = require("presentation.ui.layout")
	local game = require("tools.lua.oracle").start_mission(T.read, path, 0, 0, {})
	with_hud(T, game, path, { 640, 480 }, function(hud)
		local ui = hud.ui
		local now = 1016
		hud:frame(640, 480, now, 1, nil, nil, 0, 0)
		for _ = 1, 11 do ui:game_event(UNPLACED, MARINE, 1234, 567) end -- the message rows
		ui:window_key(true, 0x79, false) -- F10: the game menu
		ui:window_key(false, 0x79, false)
		for _ = 1, 10 do
			now = now + 16
			ui:input(IDLE)
			hud:frame(640, 480, now, 0, nil, nil)
		end
		local d = ui:modeless()
		T.ok(d >= 0 and ui:control(d).bin_name:lower() == "rez\\gamemenu.bin", "the game menu")
		local lines, lw, lh = ui:lines()
		local row, colour = ui:blend_row()
		local place, bitmap, w, h = hud.arrangement.places[d], ui:bitmap(d)
		local lx, ly = layout.place(hud.arrangement, "lines", { 0, 0, 639, 479 })
		local over, wrong, world = 0, 0, 0
		for y = 0, h - 1 do
			for x = 0, w - 1 do
				if bitmap:byte(y * w + x + 1) == colour then
					local sx, sy = place.x + x, place.y + y
					local ox, oy = sx - lx, sy - ly
					local line = ox >= 0 and oy >= 0 and ox < lw and oy < lh and lines:byte(oy * lw + ox + 1) or 0
					local index, alpha = hud.canvas.canvas:pixel(sx, sy)
					if line ~= 0 then
						over = over + 1
						if alpha ~= 255 or index ~= row:byte(line + 1) then wrong = wrong + 1 end
					elseif alpha == BLEND_A then
						world = world + 1
					end
				end
			end
		end
		T.ok(over > 0, "text line pixels under the menu")
		T.eq(wrong, 0, "each blended with its line pixel")
		T.ok(world > 0, "and the rest over the world")
	end)
end

-- The in-game menus over a game's console (kp_ui_gamemenu.c): F10 opens the game menu and pauses the game, J the
-- objectives, Escape back to the game menu, E the end-mission menu, Q its quit-mission confirmation and Q again quits:
-- the exit code 4, the game unpaused.
function S.game_menus_quit_the_mission(T)
	local path = "generated/missions/terran/terran01.kpm"
	if not T.has(path) or not T.has(DATA .. "ui.kpd") then
		T.env.log("SKIP test_kp_ui.game_menus_quit_the_mission: no generated data")
		return
	end
	local game = require("tools.lua.oracle").start_mission(T.read, path, 0, 0, {})
	local u = new_ui(T)
	u:strings(T.read(DATA .. "strings.kpd"))
	u:font_set(true)
	u:theme(RACE_GRPS[1])
	local now = 1000
	u:now(now)
	u:hud(game)
	local function frames(n)
		for _ = 1, n do
			now = now + 16
			u:now(now)
			u:tick(now)
			u:input(13) -- the pump's idle event
			u:present()
		end
	end
	local function key(vk, ch)
		u:window_key(true, vk, false)
		if ch then u:window_char(ch:byte()) end
		u:window_key(false, vk, false)
		frames(10)
	end
	local function modeless()
		local d = u:modeless()
		return d >= 0 and u:control(d).bin_name:lower() or nil
	end
	frames(5)
	key(0x79) -- F10: the console's MENU
	T.eq(modeless(), "rez\\gamemenu.bin")
	T.eq(game.paused, 1)
	u:sound_device(true, 50)
	u:take_sound_commands()
	key(0x4a, "j")
	T.eq(modeless(), "rez\\objctdlg.bin")
	local clicks = 0
	for _, c in ipairs(u:take_sound_commands()) do
		if c[1] == 9 and c[3] == 15 then clicks = clicks + 1 end
	end
	T.eq(clicks, 1, "the fired button's UI click (GameDialogCallback 0x443f20, PlayUiClickSound 0x49e460)")
	key(0x1b, "\27")
	T.eq(modeless(), "rez\\gamemenu.bin")
	key(0x45, "e")
	T.eq(modeless(), "rez\\abrtmenu.bin")
	key(0x51, "q")
	T.eq(modeless(), "rez\\quit2mnu.bin")
	key(0x51, "q")
	T.eq(modeless(), nil)
	T.eq(u:game_exit(), 4)
	T.eq(game.paused, 0)
end

-- A dialog's keyboard focus (gamemenu.bin): Tab walks its ring of children to the next focusable control and
-- Shift+Tab back (DialogCharInteract 0x4c1cb0, TabFocus 0x4c2220), the arrows the same (DialogDefaultInteract
-- 0x4c1470), and a hidden focus passes on to the next focusable control (ControlDefaultInteract 0x4c1e90 code 6).
function S.dialog_keys_walk_the_focus(T)
	local path = "generated/missions/terran/terran01.kpm"
	if not T.has(path) or not T.has(DATA .. "ui.kpd") then
		T.env.log("SKIP test_kp_ui.dialog_keys_walk_the_focus: no generated data")
		return
	end
	local game = require("tools.lua.oracle").start_mission(T.read, path, 0, 0, {})
	local u = new_ui(T)
	u:strings(T.read(DATA .. "strings.kpd"))
	u:font_set(true)
	u:theme(RACE_GRPS[1])
	local now = 1000
	u:now(now)
	u:hud(game)
	local function frames(n)
		for _ = 1, n do
			now = now + 16
			u:now(now)
			u:tick(now)
			u:input(13) -- the pump's idle event
			u:present()
		end
	end
	local function key(vk, ch)
		u:window_key(true, vk, false)
		if ch then u:window_char(ch:byte()) end
		u:window_key(false, vk, false)
		frames(2)
	end
	frames(5)
	key(0x79) -- F10: the console's MENU
	local d = u:modeless()
	T.eq(u:control(d).bin_name:lower(), "rez\\gamemenu.bin")
	local ring, c = {}, u:control(d).child
	while c >= 0 do
		ring[#ring + 1] = c
		c = u:control(c).next
	end
	local function focusable(k) return bit.band(u:control(k).flags, 0x1a) == 0x18 end
	local function step(from, dir) -- the next (1) or previous (-1) focusable control of the ring
		local at
		for i, k in ipairs(ring) do
			if k == from then at = i end
		end
		for n = 1, #ring do
			local k = ring[(at - 1 + dir * n) % #ring + 1]
			if focusable(k) then return k end
		end
	end
	local function focus() return u:control(d).active end
	local first = focus()
	T.ok(first >= 0 and focusable(first), "the menu's focus")
	local second = step(first, 1)
	T.ok(second ~= first, "another focusable control")
	key(0x09, "\t")
	T.eq(focus(), second, "Tab: the next")
	u:keys(1, 0, 0)
	key(0x09, "\t")
	u:keys(0, 0, 0)
	T.eq(focus(), first, "Shift+Tab: back")
	key(0x28)
	T.eq(focus(), second, "the down arrow: the next")
	key(0x25)
	T.eq(focus(), first, "the left arrow: back")
	u:set_visible(first, false)
	T.eq(focus(), second, "a hidden focus passes on")
end

-- The viewer's glue (viewer/play_glue.lua) over the generated data: the main menu, Single Player by the mouse, the
-- game mode's Expansion and the login's Ok by their hotkeys, a new ID typed into the popup and its Ok clicked, the
-- expansion campaign's Protoss episode: its first mission's ready room, the profile named and its progress raised; the
-- room's Start and the score screen's Continue end their screens.
function S.glue_menus_start_a_campaign_mission(T)
	if not T.has(DATA .. "campaign.kpd") then
		T.env.log("SKIP test_kp_ui.glue_menus_start_a_campaign_mission: no generated/data")
		return
	end
	local loader = require("game.data.loader")
	local saved = loader.read
	loader.read = function(path) return T.read(path:sub(2)) end
	local ok, err = pcall(function()
		local play_glue = require("viewer.play_glue")
		local profile = { original = { [0] = 0, [1] = 0, [2] = 0 }, expansion = { [0] = 0, [1] = 0, [2] = 0 } }
		local now = 1000
		local g = play_glue.new(native_only.core, profile, now)
		g.canvas.upload = function() end -- no textures
		local u, last = g.ui, nil
		local function frame()
			now = now + 16
			last = g:frame(800, 600, now) or last
		end
		local function top() local o = u:open(); return o[1] and u:control(o[1]).bin_name:lower() end
		local function wait_for(bin, frames)
			for _ = 1, frames or 2000 do
				frame()
				if top() == bin then
					for _ = 1, 150 do frame() end -- its slide and animations
					return
				end
			end
			error("glue: no " .. bin .. " (at " .. tostring(top()) .. ")")
		end
		local function key(ch)
			local vk = ch:upper():byte()
			g:key(true, vk, false)
			g:text(ch)
			g:key(false, vk, false)
		end
		local function click(id)
			local d = u:control(u:open()[1])
			local c = d.child
			while c >= 0 do
				local k = u:control(c)
				if k.id == id then
					local x = g.place.x + d.left + math.floor((k.left + k.right) / 2)
					local y = g.place.y + d.top + math.floor((k.top + k.bottom) / 2)
					g:input(3, x, y); frame(); g:input(4, x, y); frame(); g:input(5, x, y); frame()
					return
				end
				c = k.next
			end
			error("glue: no control " .. id)
		end
		wait_for("rez\\glumain.bin")
		do -- the margins of the 800 x 600 screen widen the menu's edges, dimmed (kp_ui_canvas_margins)
			local rgb = u:glue_palette()
			local function shade(i) -- the nearest colour at a quarter of the brightness, the lowest index on a tie
				local r, gr, b = math.floor(rgb:byte(3 * i + 1) / 4), math.floor(rgb:byte(3 * i + 2) / 4),
					math.floor(rgb:byte(3 * i + 3) / 4)
				local best, best_d
				for j = 0, 255 do
					local dr, dg, db = rgb:byte(3 * j + 1) - r, rgb:byte(3 * j + 2) - gr, rgb:byte(3 * j + 3) - b
					local d = dr * dr + dg * dg + db * db
					if not best_d or d < best_d then best, best_d = j, d end
				end
				return best
			end
			local checked = 0
			for y = g.place.y, g.place.y + 479, 7 do
				local edge = g.canvas.canvas:pixel(g.place.x, y)
				local index, alpha = g.canvas.canvas:pixel(0, y)
				T.eq(alpha, 255)
				T.eq(index, shade(edge))
				if edge ~= index then checked = checked + 1 end
			end
			T.ok(checked > 0, "some margin pixels differ from their edge")
		end
		click(3) -- Single Player
		wait_for("rez\\glugamemode.bin")
		key("e")
		wait_for("rez\\glupedit.bin")
		g:text("Kop")
		click(1) -- the popup's Ok
		wait_for("rez\\glulogin.bin")
		key("o")
		wait_for("rez\\gluexpcmpgn.bin")
		key("p")
		do -- the campaign menu slides out (its hotkey's press fires first): nothing on it takes events
			-- (SlideOutGlueDialog 0x457a40, ClearControlFlag10 0x457500 over the dialog and its controls)
			local function taking()
				local d = u:open()[1]
				if not d or u:control(d).bin_name:lower() ~= "rez\\gluexpcmpgn.bin" then return nil end
				local c, n = d, 0
				while c >= 0 do
					local k = u:control(c)
					if bit.band(k.flags, 0x10) ~= 0 then n = n + 1 end
					c = k.type == 0 and k.child or k.next
				end
				return n
			end
			T.ok(taking() > 0, "the menu takes events")
			local sliding = false
			for _ = 1, 30 do
				frame()
				if taking() == 0 then sliding = true break end
			end
			T.ok(sliding, "while it slides out no control takes events")
		end
		for _ = 1, 600 do
			frame()
			if last and last.mission then break end
		end
		T.eq(last and last.mission, 32)
		T.eq(last.state, 9)
		T.eq(profile.name, "Kop")
		T.eq(profile.expansion[2], 32)
		-- the mission's ready room (its establishing text first), Start, then the score screen and its Continue
		local path = "generated/missions/expprotoss/protoss01.kpm"
		if not T.has(path) then return T.env.log("SKIP the room and the score: no " .. path) end
		local game = require("tools.lua.oracle").start_mission(T.read, path, 0, 1, {})
		g:room(game, last.state)
		wait_for("rez\\glurdyp.bin", 4000) -- after the establishing text's 25 s
		last = nil
		key("s")
		for _ = 1, 3000 do
			frame()
			if last then break end
		end
		T.eq(last and last.room, "start")
		-- the room's slide-out hid the cursor; the game shows it again (RunGame 0x445310: ShowCursorLayer 0x43e600)
		T.eq(u:cursor_key(), nil, "no cursor after the room")
		u:hud(game)
		T.ok(u:cursor_key() ~= nil, "the game's cursor")
		u:end_game()
		g:score(game, 3, 754)
		wait_for("rez\\gluscore.bin")
		last = nil
		click(7) -- Continue
		for _ = 1, 3000 do
			frame()
			if last then break end
		end
		T.eq(last and last.score, true)
	end)
	loader.read = saved
	if not ok then error(err, 0) end
end

-- The glue's load screen (RunLoadGameScreen 0x459970) lists the player's saved games; Delete asks (gluAll 0x16) and
-- deletes the chosen one (DeleteSelectedSave 0x4599e0: the host's file, the list again), Ok ends the screen with the
-- save to load.
function S.glue_load_screen_deletes_and_loads(T)
	if not T.has(DATA .. "campaign.kpd") then
		T.env.log("SKIP test_kp_ui.glue_load_screen_deletes_and_loads: no generated/data")
		return
	end
	local loader = require("game.data.loader")
	local saved = loader.read
	loader.read = function(path) return T.read(path:sub(2)) end
	local ok, err = pcall(function()
		local play_glue = require("viewer.play_glue")
		local profile = { original = { [0] = 0, [1] = 0, [2] = 0 }, expansion = { [0] = 0, [1] = 0, [2] = 0 } }
		local now = 1000
		local g = play_glue.new(native_only.core, profile, now)
		g.canvas.upload = function() end -- no textures
		local u, last = g.ui, nil
		local function frame()
			now = now + 16
			last = g:frame(800, 600, now) or last
		end
		local function top() local o = u:open(); return o[1] and u:control(o[1]).bin_name:lower() end
		local function wait_for(bin)
			for _ = 1, 2000 do
				frame()
				if top() == bin then
					for _ = 1, 150 do frame() end
					return
				end
			end
			error("glue: no " .. bin .. " (at " .. tostring(top()) .. ")")
		end
		-- a press and release at the control's point (dx, dy from its top-left; its centre by default)
		local function click(id, dx, dy)
			local d = u:control(u:open()[1])
			local c = d.child
			while c >= 0 do
				local k = u:control(c)
				if k.id == id then
					local x = g.place.x + d.left + k.left + (dx or math.floor((k.right - k.left) / 2))
					local y = g.place.y + d.top + k.top + (dy or math.floor((k.bottom - k.top) / 2))
					g:input(3, x, y); frame(); g:input(4, x, y); frame(); g:input(5, x, y); frame()
					return
				end
				c = k.next
			end
			error("glue: no control " .. id)
		end
		local LIST, OK, DELETE = 6, 4, 7
		g:saves({ "alpha", "beta" })
		wait_for("rez\\glumain.bin")
		g:menus(13) -- the load screen
		wait_for("rez\\gluload.bin")
		click(LIST, 8, 4) -- the first saved game
		click(DELETE)
		wait_for("rez\\glupokcancel.bin")
		click(1) -- the popup's Ok
		for _ = 1, 30 do frame() end
		local request = u:take_save_request()
		T.eq(request and request.kind, "delete", "the host deletes the file")
		T.eq(request and request.name, "alpha")
		T.eq(top(), "rez\\gluload.bin", "back on the load screen")
		click(LIST, 8, 4) -- now "beta"
		click(OK)
		for _ = 1, 600 do
			frame()
			if last and last.load then break end
		end
		T.eq(last and last.load, 0, "the screen ends with the first of the list to load")
	end)
	loader.read = saved
	if not ok then error(err, 0) end
end

native_only.guard(S, "test_kp_ui")

return S
