-- The interface's layouts as data (ADR 0011, docs/phase9.md): where the panels and the console art go on a logical
-- screen of any size. presentation/ui/layout.lua reads it.
--
-- The console is the original's 640 x 480 console art (game\<race>console.pcx) with its dialogs; it is cut into
-- groups at columns no panel crosses (the statfluf.bin dialogs, which only repeat the console art, are not panels:
-- the console art is drawn whole). A layout places each group: "centre" keeps the original's arrangement centred,
-- "left" and "right" pin the group to that edge. Between groups that a layout moves apart, a filler stretches one
-- column of the console art over the gap. Panels outside the console are pinned to a corner or centred; the modeless
-- dialogs keep their place on the original's screen centred on the logical one.
return {
	console = {
		width = 640, height = 480,
		groups = {
			{ name = "left", x0 = 0, x1 = 138 },    -- the minimap
			{ name = "right", x0 = 138, x1 = 640 }, -- the status panel, portrait, menu button and command card
		},
		filler_column = 70, -- the console column a filler stretches
		-- the art's top row over each group (the highest of the three races' consoles: game\<race>console.pcx)
		tops = { left = 290, right = 311 },
	},
	-- panel: the dialog's original rectangle decides its group (console panels) or its anchor (others)
	panels = {
		["rez\\minimap.bin"] = { group = "left" },
		["rez\\statdata.bin"] = { group = "right" },
		["rez\\statport.bin"] = { group = "right" },
		["rez\\stat_f10.bin"] = { group = "right" },
		["rez\\statbtnt.bin"] = { group = "right" },
		["rez\\statbtnz.bin"] = { group = "right" },
		["rez\\statbtnp.bin"] = { group = "right" },
		["rez\\statres.bin"] = { anchor = "top_right" },
		["rez\\statlb.bin"] = { anchor = "top_left" },
		["rez\\ttextbox.bin"] = { anchor = "bottom_centre" },
		["rez\\ztextbox.bin"] = { anchor = "bottom_centre" },
		["rez\\ptextbox.bin"] = { anchor = "bottom_centre" },
		-- the text lines' layer, the whole original screen; its rows over the console art no higher than the original's
		lines = { anchor = "top_left", lift = true },
	},
	-- the modeless dialogs over the game (the in-game menus, their message boxes, the result dialogs): the original's
	-- 640 x 480 screen centred on the logical screen
	centred = { "rez\\gamemenu.bin", "rez\\abrtmenu.bin", "rez\\objctdlg.bin", "rez\\options.bin",
		"rez\\helpmenu.bin", "rez\\help.bin", "rez\\quit.bin", "rez\\quit2mnu.bin", "rez\\quitrepl.bin",
		"rez\\restart.bin", "rez\\savegame.bin", "rez\\loadgame.bin", "rez\\snd_dlg.bin", "rez\\spd_dlg.bin",
		"rez\\video.bin", "rez\\ok.bin", "rez\\okcancel.bin", "rez\\wait.bin", "rez\\tips_dlg.bin",
		"rez\\wmission.bin", "rez\\lmission.bin" },
	-- a group's place: "centre" (the original's arrangement centred), "left", "right" (pinned to that edge), "above"
	-- (pinned left on top of the other groups' art: the narrow screens' second row); panels = the layout's own anchors
	layouts = {
		classic = { groups = { left = "centre", right = "centre" } },
		wide = { groups = { left = "left", right = "right" } },
		-- a portrait screen narrower than the console: the status panel, portrait and command card along the bottom,
		-- the minimap above them, the leaderboard under the resources
		narrow = { groups = { left = "above", right = "left" }, min_w = 502,
			panels = { ["rez\\statlb.bin"] = { anchor = "top_left", dy = 20 } } },
	},
	-- the layout of a logical screen w x h
	choose = function(w, h)
		if w < 640 then return "narrow" end
		if w >= 800 then return "wide" end
		return "classic"
	end,
}
