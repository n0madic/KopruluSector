-- The general DAT requirement evaluator (EvaluateDatRequirements 0x407570) over the built-in
-- tables of dat.kpd, and its technology-use wrapper (CanUseTech 0x407f90).
-- docs/research/native-dat-requirements.md. Methods of the game.
local int = require("game.simulation.int")

local band = int.band

local M = {}

M.END, M.OR = 0xffff, 0xff01
M.TECHS = 44
M.CHEAT_TECH = 0x1000 -- cheat flag of "medieval man"-style research bypass (0x5432fc)
-- Error words (0x6be654) set by the evaluator and the wrappers.
M.ERR_NONE, M.ERR_OWNER, M.ERR_UNAVAILABLE, M.ERR_INCOMPLETE = 0, 1, 2, 0x14
M.ERR_DISABLED, M.ERR_RANGE_TECH = 10, 0x11
M.ERR_HALLUCINATION, M.ERR_NO_RECIPE = 22, 23
M.ERR_UNREQUIRED, M.ERR_MISSING, M.ERR_STATE = 7, 8, 5
M.ERR_GROUND_ONLY, M.ERR_NOT_GROUND, M.ERR_PROPERTY = 6, 7, 19
M.ERR_PRODUCER, M.ERR_EXPANSION, M.ERR_DISALLOWED = 25, 26, 21
M.BURROWING, M.LURKER = 11, 0x67
M.NO_WEAPON = 0x82
M.CARRIER, M.GANTRITHOR, M.REAVER, M.WARBRINGER = 0x48, 0x52, 0x53, 0x51
-- DAT offset arrays of the four domains (filled lazily from the tables).
M.OFFSETS = { orders = { "orders", "requirements" }, research = { "techdata", "research_requirements" },
	tech_use = { "techdata", "use_requirements" }, upgrades = { "upgrades", "requirements" },
	units = { "units", "requirements" } }
M.UPGRADE_IDS = 61

function M.install(G)
	-- The lazy offset initializers (0x407d50, 0x407f20, 0x4080d0, 0x408280): each record id
	-- gets the word index after it; unset entries become 0 (no recipe).
	function G:requirement_offsets(domain)
		self.req_offsets = self.req_offsets or {}
		local offsets = self.req_offsets[domain]
		if offsets then return offsets end
		local col = M.OFFSETS[domain]
		offsets = {}
		for id, v in pairs(self.dat[col[1]][col[2]]) do offsets[id] = v end
		self.req_offsets[domain] = offsets
		return offsets
	end

	local function ensure_offsets(self, domain, id)
		local offsets = self:requirement_offsets(domain)
		if offsets[id] ~= M.END then return offsets end
		local words = self.dat.requirements[domain]
		local k = 1
		while words[k] ~= M.END do
			local record = words[k]
			if domain ~= "upgrades" or record < M.UPGRADE_IDS then offsets[record] = k + 1 end
			k = k + 1
			while words[k] ~= M.END do k = k + 1 end
			k = k + 1
		end
		for i, v in pairs(offsets) do if v == M.END then offsets[i] = 0 end end
		return offsets
	end
	G.ensure_requirement_offsets = ensure_offsets

	local function fail(self, code, result)
		self.requirement_error = code
		return result
	end

	-- Tech states of a player (0x49a400 available, 0x49a420 researched).
	function G:tech_available(player, tech) return (self.techs[player].available[tech] or 0) ~= 0 end
	function G:tech_researched(player, tech) return (self.techs[player].researched[tech] or 0) ~= 0 end

	-- GetUnitRightClickAction 0x422d90: a burrowed Lurker attacks (3); a grounded rally
	-- producer without an action sets its rally point (2).
	function G:right_click_action(n)
		local u = self.u
		local t = u.type[n]
		if t == M.LURKER and band(u.status[n], 0x10) ~= 0 then return 3 end
		local action = self.dat.units.right_click_action[t]
		if action == 0 and band(u.status[n], 2) ~= 0 and self.RALLY_PRODUCERS[t] then action = 2 end
		return action
	end

	-- CanUnitMove 0x427e60: not a grounded building and a right-click action other than none
	-- and the burrowed Lurker's.
	function G:unit_can_move(n)
		if band(self.u.status[n], 2) ~= 0 then return false end
		local action = self:right_click_action(n)
		return action ~= 0 and action ~= 3
	end

	-- CanUnitAttack 0x41d9c0: a weapon of the unit or its subunit, or interceptors or
	-- scarabs in its hangar.
	local function weaponless(self, n)
		local u, d = self.u, self.dat.units
		local t = u.type[n]
		return (t == M.LURKER and band(u.status[n], 0x10) == 0) or d.ground_weapon[t] == M.NO_WEAPON
	end
	function G:unit_can_attack(n)
		local u, d = self.u, self.dat.units
		local t = u.type[n]
		if not weaponless(self, n) or d.air_weapon[t] ~= M.NO_WEAPON then return true end
		local hangar = u.union_0xc8[n]
		if (t == M.CARRIER or t == M.GANTRITHOR) and (band(hangar, 0xff) + band(int.shr(hangar, 8), 0xff)) % 256 ~= 0 then
			return true
		end
		if (t == M.REAVER or t == M.WARBRINGER) and band(hangar, 0xff) ~= 0 then return true end
		local sub = u.subunit[n]
		if sub == 0 then return false end
		if weaponless(self, sub) then return d.air_weapon[u.type[sub]] ~= M.NO_WEAPON end
		return true
	end

	-- EvaluateDatRequirements 0x407570: groups of tokens joined by 0xff01 (OR) and
	-- sequenced (AND). Returns 1, 0 (refused) or -1 (a prerequisite is missing).
	function G:evaluate_requirements(context, player, n, words, at)
		local u, d = self.u, self.dat.units
		local t = u.type[n]
		local flags = d.special_ability_flags[t]
		local completed, all = self.count_completed, self.count_all
		local any_missing, mismatch, hallucination_ok = true, false, false
		local k = at
		local w = words[k]
		while true do
			if w == M.END then
				if band(u.status[n], 0x40000000) ~= 0 and not hallucination_ok then
					return fail(self, M.ERR_HALLUCINATION, 0)
				end
				return 1
			end
			local nothing, count = true, 0
			while true do
				if w == 0xff02 then
					k = k + 1
					local want = words[k]
					if completed[want * 12 + player] ~= 0 then nothing = false end
					if want == t then
						count, mismatch, any_missing = count + 1, false, false
					elseif any_missing then
						mismatch = true
					end
				elseif w == 0xff03 then
					k = k + 1
					local i = words[k] * 12 + player
					count = count + completed[i] + all[i]
				elseif w == 0xff05 then
					if band(u.status[n], 2) == 0 then return fail(self, M.ERR_UNREQUIRED, 0) end
					count = count + 1
				elseif w == 0xff06 then
					if band(u.status[n], 2) ~= 0 then return fail(self, M.ERR_GROUND_ONLY, 0) end
					count = count + 1
				elseif w == 0xff09 then
					if u.union_0xc8[n] % 256 ~= 44 then return fail(self, M.ERR_STATE, 0) end
					count = count + 1
				elseif w == 0xff0a then
					if int.shr(u.union_0xc8[n], 8) % 256 ~= 61 then return fail(self, M.ERR_STATE, 0) end
					count = count + 1
				elseif w == 0xff0d then
					if u.union_0xd0[n] ~= 0 then return fail(self, M.ERR_MISSING, 0) end
					count = count + 1
				elseif w == 0xff0f then
					if context == M.BURROWING and t == M.LURKER then
						count = count + 1
					elseif self:tech_researched(player, context) or band(self.cheats, M.CHEAT_TECH) ~= 0 then
						count = count + 1
					end
				elseif w == 0xff11 then
					if band(u.status[n], 0x10) ~= 0 and u.ai[n] == 0 then return fail(self, M.ERR_STATE, 0) end
					count = count + 1
				elseif w == 0xff12 then
					if band(u.status[n], 2) ~= 0 then return fail(self, M.ERR_PROPERTY, 0) end
					count = count + 1
				elseif w == 0xff13 then
					if band(u.status[n], 2) == 0 then return fail(self, M.ERR_PROPERTY, 0) end
					count = count + 1
				elseif w == 0xff16 or w == 0xff17 or w == 0xff19 or w == 0xff1a then
					local bit = ({ [0xff16] = 0x8, [0xff17] = 0x20, [0xff19] = 0x800, [0xff1a] = 0x10 })[w]
					if band(flags, bit) == 0 then return fail(self, M.ERR_PROPERTY, 0) end
					count = count + 1
				elseif w == 0xff18 then
					if self:is_transport(n) then count = count + 1 end
				elseif w == 0xff1b then
					if u.union_0xc0[n] % 256 == 0 then return fail(self, M.ERR_MISSING, 0) end
					count = count + 1
				elseif w == 0xff1c then
					if band(flags, 0x40) ~= 0 then count = count + 1 end
				elseif w == 0xff1e then
					hallucination_ok = true
					count = count + 1
				elseif w == 0xff1f then
					local level = self:upgrade_level(player, context)
					local marker = level == 1 and 0xff20 or (level == 2 and 0xff21 or 0xff1f)
					if words[k] ~= marker then
						repeat k = k + 1 until words[k] == marker
					end
					count = count + 1
				elseif w == 0xff22 then
					return fail(self, M.ERR_DISALLOWED, -1)
				elseif w == 0xff23 then
					return fail(self, M.ERR_NO_RECIPE, 0)
				elseif w == 0xff24 then
					if self.expansion == 0 then return fail(self, M.ERR_EXPANSION, 0) end
					count = count + 1
				elseif w == 0xff26 then
					if band(u.status[n], 0x10) == 0 then return fail(self, M.ERR_STATE, 0) end
					count = count + 1
				elseif w == 0xff1d then
					if self:right_click_action(n) == 0 then return fail(self, M.ERR_MISSING, 0) end
					count = count + 1
				elseif w == 0xff14 then
					if not self:unit_can_move(n) then return fail(self, M.ERR_PROPERTY, 0) end
					count = count + 1
				elseif w == 0xff15 then
					if not self:unit_can_attack(n) then return fail(self, M.ERR_PROPERTY, 0) end
					count = count + 1
				elseif w == 0xff07 or w == 0xff08 then
					error(("requirements: token 0x%x (0x407570)"):format(w), 0)
				else
					count = count + completed[w * 12 + player]
				end
				if words[k + 1] ~= M.OR then break end
				k = k + 2
				w = words[k]
			end
			if mismatch then return fail(self, M.ERR_PRODUCER, 0) end
			if count == 0 and nothing then return fail(self, M.ERR_MISSING, -1) end
			k = k + 1
			w = words[k]
		end
	end

	-- RequiredParentType 0x4b3850: the unit a type is built from when its production recipe
	-- starts with "current unit is" (0xff02), else 228.
	function G:required_parent_type(t)
		local offsets = ensure_offsets(self, "units", t)
		local k = offsets[t]
		local words = self.dat.requirements.units
		if k ~= 0 and words[k] == 0xff02 then return words[k + 1] end
		return 228
	end

	-- IsUnitDisabled 0x407b50.
	function G:unit_disabled(n)
		local u = self.u
		return band(u.status[n], 0x400) ~= 0 or u.lockdown_timer[n] ~= 0 or u.stasis_timer[n] ~= 0
			or u.maelstrom_timer[n] ~= 0
	end

	-- CanIssueOrder 0x407bb0.
	M.ORDERS = 0xbd
	local GHOSTS = { [1] = true, [0x10] = true, [100] = true, [99] = true, [0x68] = true } -- 0x407b80
	function G:can_issue_order(order, player, n)
		self.requirement_error = M.ERR_NONE
		if order >= M.ORDERS then return fail(self, 0xf, 0) end
		local offsets = ensure_offsets(self, "orders", order)
		local u = self.u
		if player ~= u.owner[n] then return fail(self, M.ERR_OWNER, 0) end
		if order ~= 0x27 and order ~= 0x28 and band(u.status[n], 1) == 0 then return fail(self, M.ERR_INCOMPLETE, 0) end
		if self:unit_disabled(n) then return fail(self, M.ERR_DISABLED, 0) end
		local t, current = u.type[n], u.order[n]
		if band(u.status[n], 0x10) ~= 0 and t ~= M.LURKER and order ~= 0x76 then return fail(self, 0xb, 0) end
		if (t == 7 and current == 0x21 and order ~= 1) or (GHOSTS[t] and current == 0x81)
			or ((t == 0x44 or t == 0x3f) and current == 0x6a) then
			return fail(self, M.ERR_MISSING, 0)
		end
		if offsets[order] == 0 then return fail(self, M.ERR_NO_RECIPE, 0) end
		return self:evaluate_requirements(order, player, n, self.dat.requirements.orders, offsets[order])
	end

	-- CanUseTech 0x407f90.
	function G:can_use_tech(tech, player, n)
		self.requirement_error = M.ERR_NONE
		if tech >= M.TECHS then return fail(self, M.ERR_RANGE_TECH, 0) end
		local offsets = ensure_offsets(self, "tech_use", tech)
		local u = self.u
		if player ~= u.owner[n] then return fail(self, M.ERR_OWNER, 0) end
		if band(u.status[n], 1) == 0 then return fail(self, M.ERR_INCOMPLETE, 0) end
		if self:unit_disabled(n) then return fail(self, M.ERR_DISABLED, 0) end
		if band(u.status[n], 0x40000000) ~= 0 then return fail(self, M.ERR_HALLUCINATION, 0) end
		if not self:tech_available(player, tech) and band(self.cheats, M.CHEAT_TECH) == 0 then
			return fail(self, M.ERR_UNAVAILABLE, 0)
		end
		if offsets[tech] == 0 then return fail(self, M.ERR_NO_RECIPE, 0) end
		return self:evaluate_requirements(tech, player, n, self.dat.requirements.tech_use, offsets[tech])
	end
end

return M
