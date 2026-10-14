/* The DAT columns the C game reads (KP_DAT): one enum entry per (table, column). */
#ifndef KP_DAT_COLUMNS_H
#define KP_DAT_COLUMNS_H

#define KP_DAT_COLUMNS(X) \
	X(flingy, acceleration) \
	X(flingy, halt_distance) \
	X(flingy, movement_control) \
	X(flingy, speed) \
	X(flingy, sprite) \
	X(flingy, turn_radius) \
	X(images, damage_overlay) \
	X(orders, changes_subunit_order) \
	X(orders, disabled_maintain_unit_target) \
	X(orders, flee_unreturnable_damage) \
	X(orders, highlight_icon) \
	X(orders, interruptable) \
	X(orders, obstructable) \
	X(orders, queueable) \
	X(orders, targeting) \
	X(orders, technology_energy) \
	X(orders, iscript_animation) \
	X(orders, use_weapon_targeting) \
	X(orders, waypoints_slowdown) \
	X(sprites, image) \
	X(techdata, energy_required) \
	X(units, ai_internal) \
	X(units, air_weapon) \
	X(units, armor) \
	X(units, armor_upgrade) \
	X(units, attack_move) \
	X(units, attack_unit) \
	X(units, build_score) \
	X(units, build_time) \
	X(units, comp_ai_idle) \
	X(units, construction_animation) \
	X(units, destroy_score) \
	X(units, elevation_level) \
	X(units, graphics) \
	X(units, ground_weapon) \
	X(units, hit_points) \
	X(units, human_ai_idle) \
	X(units, max_air_hits) \
	X(units, max_ground_hits) \
	X(units, mineral_cost) \
	X(units, return_to_idle) \
	X(units, right_click_action) \
	X(units, shield_amount) \
	X(units, shield_enabled) \
	X(units, sight_range) \
	X(units, space_provided) \
	X(units, space_required) \
	X(units, special_ability_flags) \
	X(units, staredit_availability_flags) \
	X(units, staredit_group_flags) \
	X(units, staredit_placement_size) \
	X(units, subunit1) \
	X(units, subunit2) \
	X(units, supply_provided) \
	X(units, supply_required) \
	X(units, target_acquisition_range) \
	X(units, unit_direction) \
	X(units, unit_extents) \
	X(units, unit_size) \
	X(units, unknown_flags) \
	X(units, vespene_cost) \
	X(weapons, attack_angle) \
	X(weapons, damage_amount) \
	X(weapons, damage_bonus) \
	X(weapons, damage_factor) \
	X(weapons, damage_upgrade) \
	X(weapons, explosion_type) \
	X(weapons, forward_offset) \
	X(weapons, graphics) \
	X(weapons, inner_splash_range) \
	X(weapons, launch_spin) \
	X(weapons, maximum_range) \
	X(weapons, medium_splash_range) \
	X(weapons, minimum_range) \
	X(weapons, outer_splash_range) \
	X(weapons, remove_after) \
	X(weapons, target_error_message) \
	X(weapons, target_flags) \
	X(weapons, upward_offset) \
	X(weapons, weapon_behavior) \
	X(weapons, weapon_cooldown) \
	X(weapons, weapon_type)

#define KP_DAT_ENUM(t, c) KP_COL_##t##_##c,
enum { KP_DAT_COLUMNS(KP_DAT_ENUM) KP_COL_COUNT };
#undef KP_DAT_ENUM

#endif
