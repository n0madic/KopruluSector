#!/usr/bin/env python3
"""Check research artifact consistency, without executing original code.

This is a freshness/structure check, not a gameplay or algorithm-parity test.
Historical validation records stay labeled with their original evidence scope.
"""

import argparse
import ast
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
from urllib.parse import unquote

TARGET_HASH = "94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0"
PROBES = {
    "native-leaf-probes.json": "probe_native_leaf_117.py",
    "native-ai-opcode-probes.json": "inspect_ai_opcodes_117.py",
    "native-trigger-leaf-probes.json": "probe_native_triggers_117.py",
    "native-briefing-loader-probes.json": "probe_native_briefing_loader_117.py",
    "native-combat-ai-probes.json": "probe_native_combat_ai_117.py",
    "native-production-probes.json": "inspect_native_production_117.py",
    "native-workers-probes.json": "probe_native_workers_117.py",
    "native-map-overrides-probes.json": "probe_native_map_overrides_117.py",
    "native-requirements-probes.json": "inspect_native_requirements_117.py",
    "native-unit-ai-probes.json": "probe_native_unit_ai_117.py",
    "native-dat-requirements-probes.json": "inspect_native_dat_requirements_117.py",
    "native-unit-properties-probes.json": "probe_native_unit_properties_117.py",
    "native-military-ai-probes.json": "probe_native_military_ai_117.py",
    "native-training-probes.json": "inspect_native_training_117.py",
    "native-trigger-counters-probes.json": "probe_native_trigger_counters_117.py",
    "native-morph-selection-probes.json": "inspect_native_morph_selection_117.py",
    "native-placement-probes.json": "probe_native_placement_117.py",
    "native-trigger-ranking-probes.json": "probe_native_trigger_ranking_117.py",
    "native-damage-probes.json": "probe_native_damage_117.py",
    "native-research-upgrade-probes.json": "inspect_native_research_upgrade_117.py",
    "native-spending-dispatch-probes.json": "inspect_native_spending_dispatch_117.py",
    "native-primary-orders-probes.json": "probe_native_primary_orders_117.py",
    "native-trigger-unit-counts-probes.json": "probe_native_trigger_unit_counts_117.py",
    "native-default-production-probes.json": "inspect_native_default_production_117.py",
    "native-guard-lifecycle-probes.json": "inspect_native_guard_lifecycle_117.py",
    "native-ai-placement-probes.json": "probe_native_ai_placement_117.py",
    "native-guard-transfer-probes.json": "inspect_native_guard_transfer_117.py",
    "native-training-lifecycle-probes.json": "probe_native_training_lifecycle_117.py",
    "native-ai-removal-probes.json": "inspect_native_ai_removal_117.py",
    "native-trigger-unit-actions-probes.json": "probe_native_trigger_unit_actions_117.py",
    "native-region-paths-probes.json": "probe_native_region_paths_117.py",
    "native-military-arrival-probes.json": "inspect_native_military_arrival_117.py",
    "native-campaign-attack-probes.json": "inspect_native_campaign_attack_117.py",
    "native-unit-creation-probes.json": "probe_native_unit_creation_117.py",
    "native-trigger-ownership-probes.json": "probe_native_trigger_ownership_117.py",
    "native-region-generation-probes.json": "probe_native_region_generation_117.py",
    "native-campaign-wave-probes.json": "inspect_native_campaign_wave_117.py",
    "native-terrain-import-probes.json": "probe_native_terrain_import_117.py",
    "native-strategic-targets-probes.json": "inspect_native_strategic_targets_117.py",
    "native-trigger-move-probes.json": "probe_native_trigger_move_117.py",
    "native-image-initialization-probes.json": "probe_native_image_initialization_117.py",
    "native-attack-consumers-probes.json": "inspect_native_attack_consumers_117.py",
    "native-campaign-regions-probes.json": "probe_native_campaign_regions_117.py",
    "native-enemy-strength-probes.json": "inspect_native_enemy_strength_117.py",
    "native-campaign-path-limits-probes.json": "probe_native_campaign_path_limits_117.py",
    "native-trigger-grounded-move-probes.json": "probe_native_trigger_grounded_move_117.py",
    "native-contours-probes.json": "probe_native_contours_117.py",
    "native-contour-growth-probes.json": "probe_native_contour_growth_117.py",
    "native-campaign-recruitment-probes.json": "inspect_native_campaign_recruitment_117.py",
    "native-radius-recruitment-probes.json": "inspect_native_radius_recruitment_117.py",
    "native-populated-lifecycle-probes.json": "probe_native_populated_lifecycle_117.py",
    "native-ai-routes-probes.json": "inspect_native_ai_routes_117.py",
    "native-trigger-move-images-probes.json": "probe_native_trigger_move_images_117.py",
    "native-donor-ranking-probes.json": "inspect_native_donor_ranking_117.py",
    "native-contour-fit-probes.json": "probe_native_contour_fit_117.py",
    "native-terrain-sweep-probes.json": "probe_native_terrain_sweep_117.py",
    "native-campaign-terrain-sweep-probes.json": "probe_native_campaign_terrain_sweep_117.py",
    "native-movement-collision-probes.json": "probe_native_movement_collision_117.py",
    "native-ai-support-probes.json": "inspect_native_ai_support_117.py",
    "native-trigger-creep-probes.json": "probe_native_trigger_creep_117.py",
    "native-region-states-probes.json": "inspect_native_region_states_117.py",
    "native-defense-recipes-probes.json": "inspect_native_defense_recipes_117.py",
    "native-unit-death-probes.json": "probe_native_unit_death_117.py",
    "native-active-iscript-probes.json": "probe_native_active_iscript_117.py",
    "native-trigger-move-links-probes.json": "probe_native_trigger_move_links_117.py",
    "native-path-storage-probes.json": "probe_native_path_storage_117.py",
    "native-reaction-spells-probes.json": "inspect_native_reaction_spells_117.py",
    "native-image-geometry-probes.json": "probe_native_image_geometry_117.py",
    "native-local-paths-probes.json": "probe_native_local_paths_117.py",
    "native-spell-families-probes.json": "inspect_native_spell_families_117.py",
    "native-pixel-blitters-probes.json": "probe_native_pixel_blitters_117.py",
    "native-projectiles-probes.json": "probe_native_projectiles_117.py",
    "native-local-heaps-probes.json": "probe_native_local_heaps_117.py",
    "native-spell-effects-probes.json": "inspect_native_spell_effects_117.py",
    "native-campaign-local-paths-probes.json": "probe_native_campaign_local_paths_117.py",
    "native-path-blockers-probes.json": "probe_native_path_blockers_117.py",
    "native-pixel-effects-probes.json": "probe_native_pixel_effects_117.py",
    "native-attack-orders-probes.json": "probe_native_attack_orders_117.py",
    "native-path-progress-probes.json": "probe_native_path_progress_117.py",
    "native-spell-fields-probes.json": "probe_native_spell_fields_117.py",
    "native-health-bars-probes.json": "probe_native_health_bars_117.py",
    "native-movement-fsm-probes.json": "probe_native_movement_fsm_117.py",
    "native-palette-bootstrap-probes.json": "probe_native_palette_bootstrap_117.py",
    "native-computer-combat-probes.json": "probe_native_computer_combat_117.py",
    "native-spell-casting-probes.json": "probe_native_spell_casting_117.py",
    "native-blocked-movement-probes.json": "probe_native_blocked_movement_117.py",
    "native-status-effects-probes.json": "probe_native_status_effects_117.py",
    "native-text-probes.json": "probe_native_text_117.py",
    "native-storm-probes.json": "probe_native_storm_117.py",
    "native-font-decoder-probes.json": "probe_native_font_decoder_117.py",
    "native-support-casts-probes.json": "probe_native_support_casts_117.py",
    "native-weapon-effects-probes.json": "probe_native_weapon_effects_117.py",
    "native-harvesting-probes.json": "probe_native_harvesting_117.py",
    "native-hallucination-probes.json": "probe_native_hallucination_117.py",
    "native-audiovisual-probes.json": "probe_native_audiovisual_117.py",
    "native-subunit-scarab-probes.json": "probe_native_subunit_scarab_117.py",
    "native-gas-harvesting-probes.json": "probe_native_gas_harvesting_117.py",
    "native-dialog-bin-probes.json": "probe_native_dialog_bin_117.py",
    "native-ability-casts-probes.json": "probe_native_ability_casts_117.py",
    "native-carrier-probes.json": "probe_native_carrier_117.py",
    "native-construction-probes.json": "probe_native_construction_117.py",
    "native-mission-units-probes.json": "probe_native_mission_units_117.py",
    "native-transport-probes.json": "probe_native_transport_117.py",
    "native-bw-casts-probes.json": "probe_native_bw_casts_117.py",
    "native-towns-probes.json": "probe_native_towns_117.py",
    "native-special-initialization-probes.json": "probe_native_special_initialization_117.py",
    "native-bunker-probes.json": "probe_native_bunker_117.py",
    "native-special-orders-probes.json": "probe_native_special_orders_117.py",
    "native-powerup-lifecycle-probes.json": "probe_native_powerup_lifecycle_117.py",
    "native-nydus-probes.json": "probe_native_nydus_117.py",
    "native-ai-construction-probes.json": "probe_native_ai_construction_117.py",
    "native-automatic-requests-probes.json": "probe_native_automatic_requests_117.py",
    "native-nuke-probes.json": "probe_native_nuke_117.py",
    "native-trap-combat-probes.json": "probe_native_trap_combat_117.py",
    "native-resource-areas-probes.json": "probe_native_resource_areas_117.py",
    "native-resource-generation-probes.json": "probe_native_resource_generation_117.py",
    "native-ai-nuke-probes.json": "probe_native_ai_nuke_117.py",
    "native-special-producers-probes.json": "probe_native_special_producers_117.py",
    "native-zerg-construction-probes.json": "probe_native_zerg_construction_117.py",
    "native-resource-reference-ties-probes.json": "probe_native_resource_reference_ties_117.py",
    "native-powerup-goals-probes.json": "probe_native_powerup_goals_117.py",
    "native-campaign-resource-areas-probes.json": "probe_native_campaign_resource_areas_117.py",
    "native-campaign-resource-towns-probes.json": "probe_native_campaign_resource_towns_117.py",
    "native-campaign-protoss-towns-probes.json": "probe_native_campaign_protoss_towns_117.py",
    "native-ai-wave-probes.json": "probe_native_ai_wave_117.py",
    "native-carried-construction-probes.json": "probe_native_carried_construction_117.py",
    "native-protoss-construction-probes.json": "probe_native_protoss_construction_117.py",
    "native-ai-paid-roster-probes.json": "probe_native_ai_paid_roster_117.py",
    "native-campaign-ai-harvesting-probes.json": "probe_native_campaign_ai_harvesting_117.py",
    "native-extractor-probes.json": "probe_native_extractor_117.py",
    "native-assimilator-probes.json": "probe_native_assimilator_117.py",
    "native-ai-tech-build-probes.json": "probe_native_ai_tech_build_117.py",
    "native-refinery-probes.json": "probe_native_refinery_117.py",
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--out", type=Path)
    args = parser.parse_args()
    root = args.root.resolve()
    docs = root / "docs/research"
    evidence = docs / "evidence"
    tools = root / "tools/research"
    output = args.out or evidence / "verification.json"

    script_hashes = {}
    for script in sorted(tools.glob("*.py")):
        data = script.read_bytes()
        ast.parse(data, filename=str(script))
        script_hashes[script.name] = hashlib.sha256(data).hexdigest()

    document_links = 0
    for document in sorted(docs.glob("*.md")):
        content = document.read_text()
        if re.search(r"[А-Яа-яЁё]", content):
            raise ValueError(f"English-documentation check failed: {document.name}")
        for target in re.findall(r"\]\(([^)]+)\)", content):
            if target.startswith(("http://", "https://", "app://", "codex://")):
                continue
            target = unquote(target.split("#", 1)[0].strip("<>"))
            if not target:
                continue
            if not (document.parent / target).exists():
                raise ValueError(f"Missing local target in {document.name}: {target}")
            document_links += 1

    reports = {}
    for path in sorted(evidence.glob("*.json")):
        if path == output or path.name == "verification.json":
            continue
        data = path.read_bytes()
        obj = json.loads(data)
        if "target_sha256" in obj and obj["target_sha256"] != TARGET_HASH:
            raise ValueError(f"Unexpected original EXE hash in {path.name}")
        reports[path.name] = {"sha256": hashlib.sha256(data).hexdigest(), "json_valid": True}

    probes = {}
    asset_report = json.loads((evidence / "terrain-assets.json").read_text())
    if asset_report["script_sha256"] != script_hashes["inspect_terrain_assets.py"]:
        raise ValueError("Stale terrain asset report")
    if asset_report["manifest_sha256"] != hashlib.sha256((evidence / "retail-manifest.json").read_bytes()).hexdigest():
        raise ValueError("Stale terrain asset manifest dependency")
    if asset_report["total_campaign_cells"] != sum(row["cells"] for row in asset_report["campaigns"]):
        raise ValueError("Terrain campaign cell count mismatch")
    if asset_report["bad_campaign_reference_cells"] != sum(row["bad_reference_cells"] for row in asset_report["campaigns"]):
        raise ValueError("Terrain campaign reference count mismatch")
    for image_asset_name in ("native-image-assets.json", "native-unit-death-assets.json", "native-projectiles-assets.json", "native-pixel-effect-assets.json", "native-attack-orders-assets.json", "native-spell-fields-assets.json", "native-health-image-assets.json", "native-movement-fsm-assets.json", "native-computer-combat-assets.json", "native-spell-casting-assets.json", "native-blocked-movement-assets.json", "native-status-effects-assets.json", "native-storm-assets.json", "native-support-casts-assets.json", "native-weapon-effects-assets.json", "native-harvest-assets.json", "native-hallucination-assets.json", "native-subunit-scarab-assets.json", "native-gas-harvest-assets.json", "native-ability-casts-assets.json", "native-carrier-assets.json", "native-construction-assets.json", "native-mission-unit-assets.json", "native-transport-assets.json", "native-bw-casts-assets.json", "native-special-unit-assets.json", "native-bunker-assets.json", "native-special-order-assets.json", "native-nydus-assets.json", "native-nuke-assets.json", "native-trap-combat-assets.json", "native-ai-nuke-assets.json", "native-zerg-construction-assets.json", "native-powerup-goals-assets.json", "native-campaign-resource-assets.json", "native-ai-wave-assets.json", "native-carried-construction-assets.json", "native-protoss-construction-assets.json", "native-ai-paid-roster-assets.json", "native-assimilator-assets.json", "native-ai-tech-build-assets.json"):
        image_assets = json.loads((evidence / image_asset_name).read_text())
        if image_assets["extractor_sha256"] != script_hashes["inspect_native_image_assets_117.py"]:
            raise ValueError(f"Stale native image asset report: {image_asset_name}")
        if image_assets["retail_manifest_sha256"] != hashlib.sha256((evidence / "retail-manifest.json").read_bytes()).hexdigest():
            raise ValueError(f"Stale native image asset manifest dependency: {image_asset_name}")
    for pixel_asset_name in ("native-pixel-assets.json", "native-health-palette-assets.json", "native-palette-bootstrap-assets.json", "native-text-assets.json", "native-font-decoder-assets.json", "native-hallucination-string-assets.json", "native-audiovisual-assets.json", "native-dialog-bin-assets.json", "native-carrier-string-assets.json", "native-mission-notification-assets.json", "native-transport-string-assets.json", "native-bunker-string-assets.json", "native-nydus-string-assets.json", "native-protoss-construction-string-assets.json", "native-assimilator-string-assets.json"):
        pixel_assets = json.loads((evidence / pixel_asset_name).read_text())
        if pixel_assets["extractor_sha256"] != script_hashes["inspect_native_pixel_assets_117.py"]:
            raise ValueError(f"Stale native pixel asset report: {pixel_asset_name}")
        if pixel_assets["retail_manifest_sha256"] != hashlib.sha256((evidence / "retail-manifest.json").read_bytes()).hexdigest():
            raise ValueError(f"Stale native pixel asset manifest dependency: {pixel_asset_name}")
    for report, script in PROBES.items():
        obj = json.loads((evidence / report).read_text())
        if obj.get("probe_script_sha256", obj.get("probe_sha256")) != script_hashes[script]:
            raise ValueError(f"Stale original probe evidence: {report} vs {script}")
        compaction = obj.get("generated_call_trace_compaction")
        if compaction:
            if script_hashes.get(compaction["script"]) != compaction["script_sha256"]:
                raise ValueError(f"Stale native trace compaction: {report}")
            if (compaction["original_instructions_executed_by_this_compactor"] is not False
                    or compaction["case_outputs_and_assertions_preserved"] is not True
                    or len(compaction["original_probe_report_sha256"]) != 64):
                raise ValueError(f"Invalid native trace compaction boundary: {report}")
            summaries = []
            def collect_trace_summaries(node):
                if isinstance(node, dict):
                    if "call_trace_summary" in node:
                        summaries.append(node["call_trace_summary"])
                    for value in node.values():
                        collect_trace_summaries(value)
                elif isinstance(node, list):
                    for value in node:
                        collect_trace_summaries(value)
            collect_trace_summaries(obj)
            if (len(summaries) != compaction["trace_arrays_summarized"]
                    or sum(value["original_entry_count"] for value in summaries)
                    != compaction["trace_entries_summarized"]):
                raise ValueError(f"Native trace compaction count mismatch: {report}")
            for summary in summaries:
                counts = summary["callee_counts"]
                if (any(not isinstance(n, int) or n < 0 for n in counts.values())
                        or sum(counts.values()) != summary["original_entry_count"]
                        or len(summary["canonical_full_trace_sha256"]) != 64
                        or len(summary["first_entries"]) > 8
                        or len(summary["last_entries"]) > 8):
                    raise ValueError(f"Invalid compact native trace: {report}")
        dependency_hashes = dict(obj.get("dependency_hashes", {}))
        dependency_hashes.update(obj.get("dependency_script_sha256", {}))
        dependency_hashes.update(obj.get("extractor_dependencies", {}))
        dependency_hashes.update(obj.get("source_dependencies_sha256", {}))
        if "inspector_sha256" in obj:
            dependency_hashes[script.replace("probe_", "inspect_", 1)] = obj["inspector_sha256"]
        if "extractor_dependency_sha256" in obj:
            dependency_hashes[script.replace("probe_", "inspect_", 1)] = obj["extractor_dependency_sha256"]
        for dependency, recorded_hash in dependency_hashes.items():
            if script_hashes.get(dependency) != recorded_hash:
                raise ValueError(f"Stale probe dependency: {report} vs {dependency}")
        count = obj.get("total_native_checks", obj.get("case_count", obj.get("checks", obj.get("passed_cases", len(obj.get("cases", []))))))
        if not isinstance(count, int) or count <= 0:
            raise ValueError(f"Missing probe count: {report}")
        passed = obj.get("all_expected_results_matched", obj.get("all_matched", obj.get("all_passed", obj.get("passed", obj.get("passed_cases", obj.get("cases_passed"))) == count)))
        aggregate_fields = ("all_expected_results_matched", "all_matched", "all_passed", "passed", "passed_cases", "cases_passed")
        if not any(key in obj for key in aggregate_fields) and obj.get("cases"):
            passed = all(case.get("passed") is True for case in obj["cases"])
        if isinstance(passed, int) and not isinstance(passed, bool):
            passed = passed == count
        if passed is not True:
            raise ValueError(f"Probe result is not passing: {report}")
        if obj.get("failed_cases", obj.get("failed", 0)) != 0:
            raise ValueError(f"Recorded failed probes: {report}")
        expected_cases = obj.get("case_count", obj.get("synthetic_checks", obj.get("checks", count)))
        if "cases" in obj and len(obj["cases"]) != expected_cases:
            raise ValueError(f"Case-array count mismatch: {report}")
        complete_case_count = obj.get("complete_cases", obj.get("complete_original_function_cases"))
        if complete_case_count is not None and "prefix_cases" in obj:
            suffix_case_count = obj.get("pure_original_suffix_cases", 0)
            host_composition_count = obj.get("host_composition_cases", 0)
            fragment_case_count = obj.get("fragment_cases", 0)
            if complete_case_count + obj["prefix_cases"] + fragment_case_count + suffix_case_count + host_composition_count != expected_cases:
                raise ValueError(f"Complete/prefix count mismatch: {report}")
        if "complete_case_count" in obj and "prefix_case_count" in obj:
            if obj["complete_case_count"] + obj["prefix_case_count"] != expected_cases:
                raise ValueError(f"Complete/partial case count mismatch: {report}")
            actual_complete = sum(case.get("execution", {}).get("complete_return") is True for case in obj.get("cases", []))
            if actual_complete != obj["complete_case_count"]:
                raise ValueError(f"Complete-return case count mismatch: {report}")
        for case in obj.get("cases", []):
            if case.get("passed") is False or case.get("matched") is False:
                raise ValueError(f"Recorded failed individual case: {report}")
        if "synthetic_cases" in obj and "campaign_parameter_cases" in obj:
            case_groups = [value for key, value in obj.items()
                           if key.endswith("_cases") and isinstance(value, list)]
            if sum(len(group) for group in case_groups) != count:
                raise ValueError(f"Synthetic/campaign case-count mismatch: {report}")
        if "ranking_cases" in obj and "opponents_cases" in obj:
            if len(obj["ranking_cases"]) + len(obj["opponents_cases"]) != count:
                raise ValueError(f"Ranking/opponents case-count mismatch: {report}")
        for manifest_field in ("manifest_sha256", "retail_manifest_sha256"):
            if manifest_field in obj:
                manifest_hash = hashlib.sha256((evidence / "retail-manifest.json").read_bytes()).hexdigest()
                if obj[manifest_field] != manifest_hash:
                    raise ValueError(f"Stale campaign manifest dependency: {report}")
        if "terrain_report_sha256" in obj:
            terrain_hash = hashlib.sha256((evidence / "terrain-assets.json").read_bytes()).hexdigest()
            if obj["terrain_report_sha256"] != terrain_hash:
                raise ValueError(f"Stale terrain asset dependency: {report}")
        if "campaign_regions_report_sha256" in obj:
            graph_hash = hashlib.sha256((evidence / "native-campaign-regions-probes.json").read_bytes()).hexdigest()
            if obj["campaign_regions_report_sha256"] != graph_hash:
                raise ValueError(f"Stale original campaign graph dependency: {report}")
        for field, dependency in (("terrain_import_report_sha256", "native-terrain-import-probes.json"),
                                  ("contours_report_sha256", "native-contours-probes.json"),
                                  ("contour_fit_report_sha256", "native-contour-fit-probes.json")):
            if field in obj and obj[field] != hashlib.sha256((evidence / dependency).read_bytes()).hexdigest():
                raise ValueError(f"Stale original input dependency: {report} vs {dependency}")
        if "asset_manifest_sha256" in obj:
            death_asset_reports = {"native-unit-death-probes.json", "native-active-iscript-probes.json"}
            image_asset_name = ("native-unit-death-assets.json" if report in death_asset_reports else
                                "native-projectiles-assets.json" if report == "native-projectiles-probes.json" else
                                "native-attack-orders-assets.json" if report == "native-attack-orders-probes.json" else
                                "native-spell-fields-assets.json" if report == "native-spell-fields-probes.json" else
                                "native-movement-fsm-assets.json" if report == "native-movement-fsm-probes.json" else
                                "native-palette-bootstrap-assets.json" if report == "native-palette-bootstrap-probes.json" else
                                "native-computer-combat-assets.json" if report == "native-computer-combat-probes.json" else
                                "native-spell-casting-assets.json" if report == "native-spell-casting-probes.json" else
                                "native-blocked-movement-assets.json" if report == "native-blocked-movement-probes.json" else
                                "native-status-effects-assets.json" if report == "native-status-effects-probes.json" else
                                "native-text-assets.json" if report == "native-text-probes.json" else
                                "native-storm-assets.json" if report == "native-storm-probes.json" else
                                "native-font-decoder-assets.json" if report == "native-font-decoder-probes.json" else
                                "native-support-casts-assets.json" if report == "native-support-casts-probes.json" else
                                "native-weapon-effects-assets.json" if report == "native-weapon-effects-probes.json" else
                                "native-harvest-assets.json" if report == "native-harvesting-probes.json" else
                                "native-hallucination-assets.json" if report == "native-hallucination-probes.json" else
                                "native-audiovisual-assets.json" if report == "native-audiovisual-probes.json" else
                                "native-subunit-scarab-assets.json" if report == "native-subunit-scarab-probes.json" else
                                "native-gas-harvest-assets.json" if report == "native-gas-harvesting-probes.json" else
                                "native-dialog-bin-assets.json" if report == "native-dialog-bin-probes.json" else
                                "native-ability-casts-assets.json" if report == "native-ability-casts-probes.json" else
                                "native-carrier-assets.json" if report == "native-carrier-probes.json" else
                                "native-construction-assets.json" if report in {"native-construction-probes.json", "native-towns-probes.json", "native-ai-construction-probes.json", "native-automatic-requests-probes.json", "native-resource-areas-probes.json", "native-resource-generation-probes.json", "native-resource-reference-ties-probes.json"} else
                                "native-mission-unit-assets.json" if report == "native-mission-units-probes.json" else
                                "native-transport-assets.json" if report == "native-transport-probes.json" else
                                "native-bw-casts-assets.json" if report == "native-bw-casts-probes.json" else
                                "native-bunker-assets.json" if report == "native-bunker-probes.json" else
                                "native-special-unit-assets.json" if report == "native-special-initialization-probes.json" else
                                "native-special-order-assets.json" if report in {"native-special-orders-probes.json", "native-powerup-lifecycle-probes.json"} else
                                "native-nydus-assets.json" if report == "native-nydus-probes.json" else
                                "native-nuke-assets.json" if report == "native-nuke-probes.json" else
                                "native-trap-combat-assets.json" if report in {"native-trap-combat-probes.json", "native-special-producers-probes.json"} else
                                "native-ai-nuke-assets.json" if report == "native-ai-nuke-probes.json" else
                                "native-zerg-construction-assets.json" if report == "native-zerg-construction-probes.json" else
                                "native-powerup-goals-assets.json" if report == "native-powerup-goals-probes.json" else
                                "native-ai-wave-assets.json" if report == "native-ai-wave-probes.json" else
                                "native-carried-construction-assets.json" if report in {"native-carried-construction-probes.json", "native-extractor-probes.json", "native-refinery-probes.json"} else
                                "native-protoss-construction-assets.json" if report == "native-protoss-construction-probes.json" else
                                "native-assimilator-assets.json" if report == "native-assimilator-probes.json" else
                                "native-ai-tech-build-assets.json" if report == "native-ai-tech-build-probes.json" else
                                "native-ai-paid-roster-assets.json" if report == "native-ai-paid-roster-probes.json" else
                                "native-campaign-resource-assets.json" if report in {"native-campaign-resource-areas-probes.json", "native-campaign-resource-towns-probes.json", "native-campaign-protoss-towns-probes.json", "native-campaign-ai-harvesting-probes.json"} else
                                "native-image-assets.json")
            image_asset_path = evidence / image_asset_name
            if obj["asset_manifest_sha256"] != hashlib.sha256(image_asset_path.read_bytes()).hexdigest():
                raise ValueError(f"Stale native image asset dependency: {report}")
        if "font_manifest_sha256" in obj:
            font_asset_path = evidence / "native-text-assets.json"
            if obj["font_manifest_sha256"] != hashlib.sha256(font_asset_path.read_bytes()).hexdigest():
                raise ValueError(f"Stale native text font asset dependency: {report}")
        if "notification_manifest_sha256" in obj:
            notification_path = evidence / "native-mission-notification-assets.json"
            if obj["notification_manifest_sha256"] != hashlib.sha256(notification_path.read_bytes()).hexdigest():
                raise ValueError(f"Stale native notification asset dependency: {report}")
            notification_assets = json.loads(notification_path.read_text())
            if obj["network_tbl_sha256"] != notification_assets["assets"]["rez\\network.tbl"]["sha256"]:
                raise ValueError(f"Stale native notification table dependency: {report}")
        if "string_asset_manifest_sha256" in obj:
            string_asset_name = {"native-hallucination-probes.json": "native-hallucination-string-assets.json", "native-ability-casts-probes.json": "native-hallucination-string-assets.json", "native-carrier-probes.json": "native-carrier-string-assets.json", "native-transport-probes.json": "native-transport-string-assets.json", "native-bunker-probes.json": "native-bunker-string-assets.json", "native-bw-casts-probes.json": "native-hallucination-string-assets.json", "native-nydus-probes.json": "native-nydus-string-assets.json", "native-nuke-probes.json": "native-hallucination-string-assets.json", "native-ai-nuke-probes.json": "native-hallucination-string-assets.json", "native-ai-wave-probes.json": "native-hallucination-string-assets.json", "native-ai-paid-roster-probes.json": "native-hallucination-string-assets.json", "native-zerg-construction-probes.json": "native-zerg-construction-string-assets.json", "native-protoss-construction-probes.json": "native-protoss-construction-string-assets.json", "native-assimilator-probes.json": "native-assimilator-string-assets.json", "native-ai-tech-build-probes.json": "native-hallucination-string-assets.json"}[report]
            string_asset_path = evidence / string_asset_name
            if obj["string_asset_manifest_sha256"] != hashlib.sha256(string_asset_path.read_bytes()).hexdigest():
                raise ValueError(f"Stale native string asset dependency: {report}")
            string_assets = json.loads(string_asset_path.read_text())
            if "network_tbl_sha256" in obj and obj["network_tbl_sha256"] != string_assets["assets"]["rez\\network.tbl"]["sha256"]:
                raise ValueError(f"Stale native network table dependency: {report}")
        if "pixel_manifest_sha256" in obj:
            if obj["pixel_manifest_sha256"] != hashlib.sha256((evidence / "native-pixel-assets.json").read_bytes()).hexdigest():
                raise ValueError(f"Stale native pixel asset dependency: {report}")
        if "effect_manifest_sha256" in obj:
            if obj["effect_manifest_sha256"] != hashlib.sha256((evidence / "native-pixel-effect-assets.json").read_bytes()).hexdigest():
                raise ValueError(f"Stale native pixel effect asset dependency: {report}")
        if report == "native-health-bars-probes.json":
            for field, dependency in (("image_manifest_sha256", "native-health-image-assets.json"),
                                      ("palette_manifest_sha256", "native-health-palette-assets.json")):
                if obj.get(field) != hashlib.sha256((evidence / dependency).read_bytes()).hexdigest():
                    raise ValueError(f"Stale native health asset dependency: {report} vs {dependency}")
        probes[report] = {"script": script, "script_sha256_matches": True,
                          "recorded_checks": count, "recorded_results_passed": True,
                          "evidence_scope": "isolated original functions/fragments; not full gameplay"}
        if "fragment_cases" in obj:
            probes[report]["recorded_complete_original_cases"] = complete_case_count
            probes[report]["recorded_import_prefix_checks"] = obj["prefix_cases"]
            probes[report]["recorded_bounded_fragment_checks"] = obj["fragment_cases"]
        if "arithmetic_fragment_case_count" in obj:
            probes[report]["recorded_arithmetic_fragment_checks"] = obj["arithmetic_fragment_case_count"]
            probes[report]["recorded_import_prefix_checks"] = obj["prefix_case_count"] - obj["arithmetic_fragment_case_count"]
            probes[report]["recorded_complete_original_cases"] = obj["complete_case_count"]
        if obj.get("host_composition_cases"):
            probes[report]["recorded_host_composition_checks"] = obj["host_composition_cases"]
            probes[report]["recorded_complete_original_cases"] = complete_case_count
            probes[report]["evidence_scope"] = "complete original leaves plus explicit host resource compositions; not full native wrappers/gameplay"

    host_codecs = {}
    for report, script in {"native-audiovisual-codec.json": "probe_audiovisual_codec_resources.py"}.items():
        obj = json.loads((evidence / report).read_text())
        if obj.get("original_code_executed") is not False or obj.get("complete_game_executed") is not False:
            raise ValueError(f"Incorrect host codec evidence layer: {report}")
        if obj["probe_script_sha256"] != script_hashes[script] or obj["inspector_sha256"] != script_hashes["inspect_native_audiovisual_117.py"]:
            raise ValueError(f"Stale host codec script dependency: {report}")
        asset_path = evidence / "native-audiovisual-assets.json"
        if obj["asset_manifest_sha256"] != hashlib.sha256(asset_path.read_bytes()).hexdigest():
            raise ValueError(f"Stale host codec asset dependency: {report}")
        assets = json.loads(asset_path.read_text())["assets"]
        resources = obj["resources"]
        if obj["host_resource_checks"] != len(resources) or not resources:
            raise ValueError(f"Host codec count mismatch: {report}")
        for resource in resources:
            if resource.get("passed") is not True or resource.get("original_code_executed") is not False:
                raise ValueError(f"Unsuccessful or misclassified host codec resource: {report}")
            if resource["input_sha256"] != assets[resource["path"]]["sha256"]:
                raise ValueError(f"Stale host codec input: {resource['path']}")
        host_codecs[report] = {"script": script, "recorded_host_resource_checks": len(resources), "recorded_results_passed": True,
                               "evidence_scope": "host third-party media decoding; no original decoder or runtime execution"}

    historical = None
    if output.exists():
        previous = json.loads(output.read_text())
        historical = previous.get("initial_static_and_derived_batch")
        if historical is None and "rng_luajit_vectors" in previous:
            historical = previous
    result = {
        "generated_by": str(Path(__file__).relative_to(root)),
        "checked_on": datetime.now(timezone.utc).date().isoformat(),
        "scope": "AST, English docs, local link existence, JSON, recorded probe freshness/counts",
        "original_instructions_executed_in_recorded_probes": True,
        "complete_original_game_executed": False,
        "verification_command_executes_original_code": False,
        "python_scripts_syntax_parsed": len(script_hashes),
        "english_documents_checked": len(list(docs.glob("*.md"))),
        "local_links_checked": document_links,
        "scripts": script_hashes, "evidence_files": reports, "original_probe_freshness": probes,
        "recorded_original_check_total": sum(x["recorded_checks"] - x.get("recorded_host_composition_checks", 0) for x in probes.values()),
        "recorded_host_resource_composition_check_total": sum(x.get("recorded_host_composition_checks", 0) for x in probes.values()),
        "host_codec_freshness": host_codecs,
        "recorded_host_codec_resource_check_total": sum(x["recorded_host_resource_checks"] for x in host_codecs.values()),
        "limits": "Passing artifact consistency does not close unresolved helpers or establish complete campaigns, visuals, audio or save/load.",
    }
    if historical is not None:
        result["initial_static_and_derived_batch"] = historical
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({"scripts": len(script_hashes), "documents": result["english_documents_checked"],
                      "links": document_links, "recorded_original_checks": result["recorded_original_check_total"],
                      "recorded_host_resource_compositions": result["recorded_host_resource_composition_check_total"],
                      "recorded_host_codec_resources": result["recorded_host_codec_resource_check_total"],
                      "output": str(output)}))


if __name__ == "__main__":
    main()
