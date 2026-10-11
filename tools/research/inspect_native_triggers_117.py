#!/usr/bin/env python3
"""Hash-gated static trigger/campaign witnesses; never loads or executes the EXE.

Reads the scratch CHK extraction produced by inspect_campaigns.py. Only hashes,
integer metadata, native disassembly and next-scenario filenames are emitted.
"""

import argparse
import hashlib
import json
from pathlib import Path
import struct

import capstone
import pefile


TARGET_HASH = "94dea44898a3934cdfae2a8149c0044db8ad1af4b4bc54306cdca3b1612d6ed0"
CONDITIONS = (
    "None CountdownTimer Command Bring Accumulate Kill CommandTheMost "
    "CommandsTheMostAt MostKills HighestScore MostResources Switch ElapsedTime "
    "MissionBriefing Opponents Deaths CommandTheLeast CommandTheLeastAt "
    "LeastKills LowestScore LeastResources Score Always Never"
).split()
ACTIONS = (
    "None Victory Defeat PreserveTrigger Wait PauseGame UnpauseGame Transmission "
    "PlayWAV DisplayTextMessage CenterView CreateUnitWithProperties "
    "SetMissionObjectives SetSwitch SetCountdownTimer RunAIScript "
    "RunAIScriptAtLocation LeaderBoardControl LeaderBoardControlAt "
    "LeaderBoardResources LeaderBoardKills LeaderBoardPoints KillUnit "
    "KillUnitAtLocation RemoveUnit RemoveUnitAtLocation SetResources SetScore "
    "MinimapPing TalkingPortrait MuteUnitSpeech UnmuteUnitSpeech "
    "LeaderboardComputerPlayers LeaderboardGoalControl LeaderboardGoalControlAt "
    "LeaderboardGoalResources LeaderboardGoalKills LeaderboardGoalPoints "
    "MoveLocation MoveUnit LeaderboardGreed SetNextScenario SetDoodadState "
    "SetInvincibility CreateUnit SetDeaths Order Comment GiveUnitsToPlayer "
    "ModifyUnitHitPoints ModifyUnitEnergy ModifyUnitShieldPoints "
    "ModifyUnitResourceAmount ModifyUnitHangerCount PauseTimer UnpauseTimer Draw "
    "SetAllianceStatus DisableDebugMode EnableDebugMode"
).split()
BRIEFING_ACTIONS = (
    "None Wait PlayWAV DisplayTextMessage SetMissionObjectives ShowPortrait "
    "HidePortrait TalkingPortrait Transmission EnableSkipTutorial"
).split()
ENUM_SOURCE = (
    "https://github.com/bwapi/bwapi/blob/"
    "d727fed68558c506163048ea889131d8cbb33915/"
    "bwapi/BWAPI/Source/BW/Triggers.h"
)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def sections(data):
    p = 0
    while p < len(data):
        if p + 8 > len(data):
            raise ValueError("Truncated CHK header")
        tag, n = struct.unpack_from("<4sI", data, p)
        p += 8
        if p + n > len(data):
            raise ValueError("Truncated CHK section")
        yield tag, data[p:p + n]
        p += n


def next_scenario_metadata(path, data):
    parts = list(sections(data))
    strings = next((part for tag, part in parts if tag == b"STR "), b"")
    offsets = ()
    if strings:
        n = struct.unpack_from("<H", strings)[0]
        offsets = struct.unpack_from("<" + str(n) + "H", strings, 2)

    def text(ref):
        if not 1 <= ref <= len(offsets):
            raise ValueError("Invalid next-scenario string reference")
        off = offsets[ref - 1]
        end = strings.find(b"\0", off)
        if end < 0:
            raise ValueError("Unterminated next-scenario string")
        return strings[off:end].decode("latin1")

    result = []
    global_index = 0
    for tag, part in parts:
        if tag != b"TRIG":
            continue
        if len(part) % 2400:
            raise ValueError("Nonintegral TRIG records")
        for off in range(0, len(part), 2400):
            conditions = []
            for ci in range(16):
                values = struct.unpack_from("<IIIHBBBBH", part, off + ci * 20)
                if values[5] == 0:
                    break
                conditions.append(dict(zip(
                    ("location", "player", "amount", "unit", "comparison", "type",
                     "resource_score_switch", "flags", "internal"), values
                )))
            for ai in range(64):
                a = off + 320 + ai * 32
                if part[a + 26] != 41:
                    continue
                ref = struct.unpack_from("<I", part, a + 4)[0]
                result.append({
                    "map_path": path, "map_sha256": sha(data),
                    "trigger_index": global_index, "action_index": ai,
                    "next_scenario": text(ref), "action_flags": part[a + 28],
                    "execution_flags": struct.unpack_from("<I", part, off + 2368)[0],
                    "owners": list(part[off + 2372:off + 2399]),
                    "conditions_until_first_none": conditions,
                    "evidence": "raw trigger candidate; predicate satisfiability unproved",
                })
            global_index += 1
    return result


def bonus_control_metadata(path, data):
    """Record the local bonus gate's numeric predicates and timer/switch writes."""
    if path != "campaign\\expzerg\\zerg09\\staredit\\scenario.chk":
        return []
    result = []
    trigger_index = 0
    for tag, part in sections(data):
        if tag != b"TRIG":
            continue
        for off in range(0, len(part), 2400):
            selected = []
            for ai in range(64):
                a = off + 320 + 32 * ai
                values = struct.unpack_from("<6IH4BH", part, a)
                kind = values[7]
                if kind != 14 and not (kind == 13 and values[5] in (4, 8)):
                    continue
                selected.append({"action_index": ai, "fields": dict(zip(
                    ("location", "string", "sound", "time", "player", "number_switch",
                     "unit", "type", "modifier", "flags", "padding", "internal"), values
                ))})
            if selected:
                conditions = []
                for ci in range(16):
                    values = struct.unpack_from("<IIIHBBBBH", part, off + ci * 20)
                    if values[5] == 0:
                        break
                    conditions.append(dict(zip(
                        ("location", "player", "amount", "unit", "comparison", "type",
                         "resource_score_switch", "flags", "internal"), values
                    )))
                result.append({"map_path": path, "map_sha256": sha(data),
                               "trigger_index": trigger_index,
                               "conditions_until_first_none": conditions,
                               "selected_actions": selected})
            trigger_index += 1
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--data", type=Path, required=True,
                        help="Scratch extraction directory from inspect_campaigns.py")
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    data = args.exe.read_bytes()
    if sha(data) != TARGET_HASH:
        parser.error("Unknown EXE hash: reviewed 1.17 addresses do not apply")
    manifest_bytes = args.manifest.read_bytes()
    manifest = json.loads(manifest_bytes)
    pe = pefile.PE(data=data)
    base = pe.OPTIONAL_HEADER.ImageBase
    if base != 0x400000:
        parser.error("Unexpected preferred image base")
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True

    def offset(va):
        return pe.get_offset_from_rva(va - base)

    def disassemble(lo, hi):
        result = []
        for ins in md.disasm(data[offset(lo):offset(lo) + hi - lo], lo):
            result.append({
                "va": hex(ins.address), "rva": hex(ins.address - base),
                "file_offset": hex(offset(ins.address)), "bytes": ins.bytes.hex(),
                "mnemonic": ins.mnemonic, "operands": ins.op_str,
            })
        return result

    def table(va, names):
        targets = struct.unpack_from("<" + str(len(names)) + "I", data, offset(va))
        return [{"id": i, "name_from_bwapi": name, "va": hex(target),
                 "rva": hex(target - base)} for i, (name, target) in
                enumerate(zip(names, targets))]

    blocks = {
        "frame_trigger_caller": (0x444F5B, 0x444F7D),
        "scheduler": (0x4BE500, 0x4BE5B3),
        "wait_and_elapsed_timers": (0x4BE410, 0x4BE4FD),
        "trigger_initialization": (0x4BE240, 0x4BE340),
        "frame_progress_gate": (0x47CB60, 0x47CB66),
        "condition_evaluator": (0x4BDED0, 0x4BDF0C),
        "action_runner": (0x4BDF10, 0x4BDFC8),
        "player_list_runner": (0x4BDFD0, 0x4BE016),
        "owner_expansion": (0x4BDB00, 0x4BDDB8),
        "owner_expansion_chk_caller": (0x468E3D, 0x468E7C),
        "player_iterator": (0x4B3160, 0x4B322F),
        "wait": (0x4B5FD0, 0x4B6016),
        "transmission": (0x4B60E0, 0x4B6235),
        "preserve": (0x4B5FC0, 0x4B5FCB),
        "skip_ui_flag": (0x4BD560, 0x4BD572),
        "wait_active_read": (0x4BD580, 0x4BD587),
        "player_exclusion": (0x4BD590, 0x4BD5C9),
        "preserve_setter": (0x4BD5D0, 0x4BD5DD),
        "wait_setter_and_current_player": (0x4BDA60, 0x4BDA76),
        "pause_flag_setter": (0x4BD480, 0x4BD49D),
        "victory_defeat_preserve_wait_pause": (0x4B5EF0, 0x4B60E0),
        "run_ai_actions": (0x4B6760, 0x4B67B3),
        "set_next_scenario_action": (0x4B5900, 0x4B5922),
        "campaign_progression": (0x4BB790, 0x4BB91F),
        "campaign_find_map": (0x4BBAE0, 0x4BBB13),
        "campaign_resolve_filename": (0x4BB9D0, 0x4BBAD1),
        "campaign_number_and_suffix": (0x4BBB60, 0x4BBC18),
        "campaign_unlock_profile": (0x4BBC20, 0x4BBCAB),
        "briefing_chk_loader": (0x468E80, 0x468F24),
        "briefing_owner_expansion": (0x4607E0, 0x4609B3),
        "briefing_runner": (0x44FEE0, 0x44FF5D),
        "briefing_list_runner": (0x460EA0, 0x460EDF),
        "briefing_wait": (0x44FCD0, 0x44FD06),
        "briefing_transmission": (0x44FE30, 0x44FEB7),
        "briefing_timer_schedule": (0x4611A0, 0x4611D9),
        "briefing_wait_clear": (0x44FF60, 0x44FF68),
    }
    condition_table = table(0x50E058, CONDITIONS)
    action_table = table(0x50DF68, ACTIONS)
    briefing_table = table(0x503908, BRIEFING_ACTIONS)
    # These are bounded disassembly windows, not reconstructed complete functions.
    # Next table entry provides a useful upper bound except for the final handler.
    all_entries = condition_table + action_table + briefing_table
    unique_targets = sorted({int(e["va"], 16) for e in all_entries})
    handler_windows = {}
    for va in unique_targets:
        following = next((x for x in unique_targets if x > va), va + 0x100)
        hi = min(following, va + 0x400)
        handler_windows[hex(va)] = {
            "end_va_exclusive": hex(hi),
            "scope": "bounded static window; may include padding or other helper entries",
            "instructions": disassemble(va, hi),
        }

    catalog = {r["index"]: r["path"] for r in manifest["map_catalog"]}
    campaign_names = ("zerg", "terran", "protoss", "expzerg", "expterran", "expprotoss")
    pointers = struct.unpack_from("<6I", data, offset(0x50E0B8))
    campaigns = []
    for name, va in zip(campaign_names, pointers):
        rows = []
        for i in range(64):
            label, map_index, movie, race, hidden = struct.unpack_from("<HHHBB", data, offset(va + 8 * i))
            row = {"row": i, "va": hex(va + 8 * i), "rva": hex(va + 8 * i - base),
                   "file_offset": hex(offset(va + 8 * i)), "label_id_unresolved": label,
                   "map_index": map_index, "movie_id": movie, "race": race,
                   "hidden": hidden, "catalog_path": catalog.get(map_index),
                   "selectable_by_number": bool(map_index and not movie and not hidden)}
            rows.append(row)
            if map_index == 0:
                break
        else:
            raise ValueError("Campaign sentinel not found")
        campaigns.append({"name": name, "table_va": hex(va), "rows": rows})

    next_scenarios = []
    bonus_controls = []
    briefing_histogram = {}
    for row in manifest["campaign_found"]:
        raw = (args.data / row["path"].replace("\\", "/")).read_bytes()
        if sha(raw) != row["sha256"]:
            raise ValueError("CHK does not match manifest: " + row["path"])
        next_scenarios.extend(next_scenario_metadata(row["path"], raw))
        bonus_controls.extend(bonus_control_metadata(row["path"], raw))
        for tag, part in sections(raw):
            if tag != b"MBRF":
                continue
            if len(part) % 2400:
                raise ValueError("Nonintegral MBRF")
            hist = {}
            for off in range(0, len(part), 2400):
                for ai in range(64):
                    kind = part[off + 320 + 32 * ai + 26]
                    if kind:
                        hist[kind] = hist.get(kind, 0) + 1
            briefing_histogram[row["path"]] = hist

    result = {
        "target_sha256": TARGET_HASH, "target_version": "1.17.0.1",
        "image_base": hex(base), "evidence": "static disassembly and CHK metadata; no execution",
        "manifest_sha256": sha(manifest_bytes), "enum_source": ENUM_SOURCE,
        "condition_table_va": "0x50e058", "action_table_va": "0x50df68",
        "briefing_action_table_va": "0x503908", "condition_handlers": condition_table,
        "action_handlers": action_table, "briefing_action_handlers": briefing_table,
        "handler_windows": handler_windows,
        "witnesses": {name: disassemble(*bounds) for name, bounds in blocks.items()},
        "speed_delta_table_va": "0x503360",
        "speed_deltas": list(struct.unpack_from("<8I", data, offset(0x503360))),
        "owner_jump_table_va": "0x4bddb8",
        "owner_jump_targets": [hex(x) for x in struct.unpack_from("<3I", data, offset(0x4BDDB8))],
        "owner_jump_map": list(data[offset(0x4BDDC4):offset(0x4BDDC4) + 14]),
        "campaign_pointer_table_va": "0x50e0b8", "campaigns": campaigns,
        "next_scenario_candidates": next_scenarios,
        "bonus_control_triggers": bonus_controls,
        "briefing_action_histograms": briefing_histogram,
        "limitations": [
            "No original code was executed, so these are not runtime traces.",
            "Native wrappers do not establish all helper-side unit selection and mutation semantics.",
            "Next-scenario candidates include disabled actions and unsatisfied predicates.",
            "UI timer semantics behind briefing helper 0x4c5960 remain a separate dependency.",
            "Label identifiers and all campaign end-state meanings need independent string/UI validation.",
        ],
    }
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({
        "condition_handlers": len(condition_table), "action_handlers": len(action_table),
        "briefing_handlers": len(briefing_table),
        "selectable_campaign_rows": sum(r["selectable_by_number"] for c in campaigns for r in c["rows"]),
        "next_scenario_candidates": len(next_scenarios), "out": str(args.out),
    }))


if __name__ == "__main__":
    main()
