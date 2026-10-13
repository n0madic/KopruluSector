-- The object step and the game frame (StepGameObjects 0x445070, UpdateUnits 0x438ef0, the
-- frame loop of 0x444e30 and PrimeGameLoop 0x445520; atlas P15). Everything runs
-- sequentially in the original's order (ADR 0007). The interface parts of the native step
-- (CancelTargetingUi 0x423120, selection flashes, status refreshes) belong to presentation;
-- simulation contracts that are not ported yet raise with their native address.
-- Methods of the game.
local int = require("game.simulation.int")

local band, bnot = int.band, int.bnot
local bit_band = int.bit_band

local M = {}

M.TILE_PERIOD = 100 -- 0x6663e4: vision is rebuilt every 100 steps
M.ORDER_SPREAD, M.ORDER_SPREAD_PERIOD = 8, 0x96 -- 0x5da390: +0x85 phases
M.SECONDARY_SPREAD, M.SECONDARY_SPREAD_PERIOD = 30, 300 -- 0x5da394: +0x91 phases
M.DETECTION_PERIOD = 0x1e
M.HIT_SLOTS = 32 -- 0x540768: recent hits by slot (0x80 bytes each)
M.STATUS_CLOAKED, M.STATUS_AIR, M.STATUS_HALTED = 0x300, 0x4, 0x8000
M.STATUS_GROUNDED_BUILDING = 0x2
M.STATUS_BURROWED, M.STATUS_REQUIRES_DETECTION, M.STATUS_FREE_CLOAK = 0x10, 0x100, 0x800
M.SECONDARY_CLOAK = 0x6d
M.SPRITE_HIDDEN = 0x20
M.DAT_SUBUNIT = 0x10
-- Secondary orders that UpdateHiddenUnit 0x43a770 runs, by their functions.
M.HIDDEN_SECONDARY = { [0x3f] = 0x41c630 }
M.RACE_PROTOSS = 2
M.CONTROLLER_RESCUE = 3
M.DISRUPTION_WEB, M.NEUTRAL = 0x69, 11

function M.install(G)
	-- The step globals that RunGame 0x445310 and the start leave for the first step.
	function G:step_init()
		self.tile_countdown = 1 -- 0x6663e4 (RunGame)
		self.update_tiles = false -- 0x6663ec
		self.move_reveal = false -- 0x665b44
		self.order_spread = 10 -- 0x5da390 (InitializeUnitSystem)
		self.secondary_spread = M.ORDER_SPREAD_PERIOD -- 0x5da394
		self.interceptor_alerted = false -- 0x6b6900
		self.hit_slot = 0 -- 0x541768 (ClearLurkerHitHistory 0x40a150)
		self.hits = {}
		for k = 0, M.HIT_SLOTS - 1 do self.hits[k] = {} end
		self.frame = 0 -- 0x52651c
		self.turn = 0 -- 0x672658 (RunGame)
	end

	-- The native list walks: `each` reads a unit's next link after visiting it, `each_saved`
	-- before (the loops that may remove the visited unit).
	local function each(self, list, f)
		local nxt = self.u.next
		local n = self.lists[list].head
		while n ~= 0 do
			f(n)
			n = nxt[n]
		end
	end

	local function each_saved(self, list, f)
		local nxt = self.u.next
		local n = self.lists[list].head
		while n ~= 0 do
			local m = nxt[n]
			f(n)
			n = m
		end
	end

	local function set_context(self, n)
		self.img.unit, self.img.flingy = n, n
	end

	-- UpdateDisruptionWebCoverage 0x438cd0: units outside a web lose the halted flag.
	local function disruption_web(self)
		local u = self.u
		each(self, "active", function(n)
			local status = u.status[n]
			if bit_band(status, M.STATUS_AIR) == 0 or bit_band(status, M.STATUS_HALTED) ~= 0 then
				u.status[n] = band(status, bnot(M.STATUS_HALTED))
				if self:is_transport(n) then
					for k = 0, 7 do
						if u["loaded" .. k][n] ~= 0 then error("step: web coverage of loaded units (0x4381d0)", 0) end
					end
				elseif u.subunit[n] ~= 0 then
					local sub = u.subunit[n]
					u.status[sub] = band(u.status[sub], bnot(M.STATUS_HALTED))
				end
			end
		end)
		if self.count_completed[M.DISRUPTION_WEB * 12 + M.NEUTRAL] ~= 0 then
			error("step: disruption web coverage (0x43a880)", 0)
		end
	end

	-- UpdateUnit 0x43a720, the body of UpdateUnits' main loop over the active and revealer
	-- lists: the unit's timers and orders, then its subunit's (as the current unit), then the
	-- sprite.
	local function update_unit(self, n)
		local u, w = self.u, self.img
		local subunit = bit_band(self.dat.units.special_ability_flags[u.type[n]], M.DAT_SUBUNIT) ~= 0
		self:flash_selection(n)
		self:update_unit_timers(n)
		self:dispatch_main_order(n)
		self:dispatch_secondary_order(n)
		local sub = u.subunit[n]
		if sub ~= 0 and not subunit then
			w.unit = sub
			update_unit(self, sub)
			w.unit = n
		end
		self:step_unit_sprite(n)
	end

	-- UpdateHiddenUnit 0x43a770: a hidden unit's subunit (as the current unit), then its
	-- movement, timers, hidden orders and the secondary orders a hidden unit runs, and its
	-- sprite.
	local function update_hidden_unit(self, n)
		local u, w = self.u, self.img
		local sub = u.subunit[n]
		if sub ~= 0 and bit_band(self.dat.units.special_ability_flags[u.type[n]], M.DAT_SUBUNIT) == 0 then
			w.unit = sub
			update_hidden_unit(self, sub)
			w.unit = n
		end
		self:execute_movement(n)
		self:update_unit_timers(n)
		self:dispatch_hidden_order(n)
		local second = M.HIDDEN_SECONDARY[u.secondary_order[n]]
		if second then error(("step: secondary order 0x%02x of a hidden unit (0x%x)"):format(u.secondary_order[n], second), 0) end
		self:secondary_cloak_order(n, u.secondary_order[n])
		self:step_unit_sprite(n)
	end

	-- UpdateCloakList 0x434310: after the steps counted this step's cloak sources (+0x96), a
	-- unit without one leaves the list and decloaks; a unit cloaked by others only that has
	-- cloaked itself loses the free-cloak bit; a counted unit not yet cloaked cloaks. The
	-- status panel refresh (0x4a7ed0) on a change is interface only.
	local function update_cloak_list(self)
		local u = self.u
		local n = self.cloaked_head
		while n ~= 0 do
			local next = u.burrow_next[n]
			if self:cloak_list_remove(n) then
				error("step: decloaking a unit without cloak source (0x41d360)", 0)
			end
			local status = u.status[n]
			if bit_band(status, M.STATUS_BURROWED) == 0 and u.secondary_order[n] == M.SECONDARY_CLOAK
				and u.is_cloaked[n] == 1 and bit_band(status, M.STATUS_FREE_CLOAK) ~= 0 then
				u.status[n] = status - M.STATUS_FREE_CLOAK
			end
			if bit_band(u.status[n], M.STATUS_REQUIRES_DETECTION) == 0 then
				error("step: cloaking a unit of the cloaked list (0x41d200)", 0)
			end
			n = next
		end
	end

	-- UpdateProtossPower 0x430670: after a pylon or Protoss building changed, every Protoss
	-- building checks its power.
	local function update_power(self)
		if not self.power_dirty then return end
		local u = self.u
		each(self, "active", function(n)
			if bit_band(u.status[n], M.STATUS_GROUNDED_BUILDING) ~= 0 and self:unit_race(n) == M.RACE_PROTOSS
				and self.players[u.owner[n]].controller ~= M.CONTROLLER_RESCUE then
				error("step: Protoss power (0x430000)", 0)
			end
		end)
		self.power_dirty = false
	end

	-- AdvanceHitHistorySlot 0x40a880.
	local function advance_hits(self)
		self.hit_slot = (self.hit_slot + 1) % M.HIT_SLOTS
		self.hits[self.hit_slot] = {}
	end

	-- UpdateUnits 0x438ef0.
	function G:update_units()
		local u = self.u
		self.interceptor_alerted = false
		self.order_spread = self.order_spread - 1
		if self.order_spread == 0 then
			local v = 0
			each(self, "active", function(n)
				u.queue_timer[n] = v
				v = (v + 1) % M.ORDER_SPREAD
			end)
			self.order_spread = M.ORDER_SPREAD_PERIOD
		end
		self.secondary_spread = self.secondary_spread - 1
		if self.secondary_spread == 0 then
			local v = 0
			each(self, "active", function(n)
				u.secondary_timer[n] = v
				v = (v + 1) % M.SECONDARY_SPREAD
			end)
			self.secondary_spread = M.SECONDARY_SPREAD_PERIOD
		end
		disruption_web(self)
		self:update_pending_units(set_context)
		each(self, "active", function(n)
			set_context(self, n)
			self:prepare_movement(n)
		end)
		if self.update_tiles then
			each_saved(self, "special", function(n)
				set_context(self, n)
				self:unit_update_vision(n)
			end)
		end
		each(self, "active", function(n)
			self:unit_update_sprite_visibility(n)
			if bit_band(u.status[n], M.STATUS_CLOAKED) ~= 0 then
				u.is_cloaked[n] = 0
				if u.secondary_timer[n] == 0 then
					self:update_detection(n)
					u.secondary_timer[n] = M.DETECTION_PERIOD
				else
					u.secondary_timer[n] = u.secondary_timer[n] - 1
				end
			end
		end)
		each_saved(self, "active", function(n)
			set_context(self, n)
			update_unit(self, n)
		end)
		each_saved(self, "hidden", function(n)
			if bit_band(u.status[n], M.STATUS_CLOAKED) ~= 0 then u.is_cloaked[n] = 0 end
			set_context(self, n)
			update_hidden_unit(self, n)
		end)
		update_cloak_list(self)
		update_power(self)
		advance_hits(self)
		each_saved(self, "special", function(n)
			set_context(self, n)
			update_unit(self, n)
		end)
		set_context(self, nil)
	end

	-- UpdateBullets 0x40a5f0 and UpdateIndependentFlingies 0x40c910: no bullets or
	-- independent flingies exist before combat.
	local function update_bullets(self)
		if self.bullets_active then error("step: bullets (0x40a5f0)", 0) end
	end

	-- StepGameObjects 0x445070.
	function G:step_objects()
		self.rng:set_enabled(1)
		self:creep_recede_tick()
		self:ai_step()
		if self.tile_countdown == 0 then self.tile_countdown = M.TILE_PERIOD end
		self.tile_countdown = self.tile_countdown - 1
		self.update_tiles = self.tile_countdown == 0
		if self.update_tiles then
			self:refresh_remembered_terrain()
			self:reset_tile_visibility()
		end
		self:update_units()
		update_bullets(self)
		self.img:update_thingies()
		self.rng:set_enabled(0)
	end

	-- PrimeGameLoop 0x445520: two object steps before the first frame, without a frame
	-- count or a trigger step.
	function G:prime()
		self:step_objects()
		self:step_objects()
	end

	-- The object step and the trigger step (with the generator enabled) of a counted frame.
	function G:step_frame()
		self:step_objects()
		self.rng:set_enabled(1)
		self:trigger_step()
		self.rng:set_enabled(0)
	end

	-- One iteration of the game loop 0x444e30: the turn with its commands (PollTurn), the
	-- frame count, then the steps.
	function G:run_frame(commands)
		self:run_turn(commands or {})
		self.frame = self.frame + 1
		self:step_frame()
	end
end

return M
