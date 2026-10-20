"""Aggregate boot and mission outputs into the project evidence summary.

Reads ``~/koprulu-private/emu/{boot,missions}`` and writes
``docs/research/evidence/full-game-emulation.json`` (small: counts, checkpoint digests,
faults, coverage comparison with the atlas) plus the full union coverage list into the
private workspace.
"""
from __future__ import annotations

import bisect
import json
import re
from pathlib import Path

import capstone
import pefile

from . import coverage, process
from .campaigns import missions

EMU = Path("~/koprulu-private/emu")
EVIDENCE = Path(__file__).resolve().parents[3] / "docs" / "research" / "evidence" / "full-game-emulation.json"
ATLAS_DOCS = Path(__file__).resolve().parents[3] / "docs" / "research" / "atlas"
CHECKPOINTS = ("1", "1000", "2000", "3000")


# H07 runs (no cheats at all): Zerg Z1, Protoss P1 and P6 attempts, and the unit surveys
H07_RUN_PREFIXES = ("zerg-z1-nocheat", "protoss-p1-nocheat", "p6-nocheat", "p6-survey", "p1-survey", "p5-survey",
                    "dark-origin-map")

def _atlas_text() -> str:
    return "\n".join(p.read_text() for p in sorted(ATLAS_DOCS.glob("*.md")))


def classify_outside(blocks: set[int], atlas: dict[int, dict]) -> list[dict]:
    """Executed block starts outside every atlas function body, grouped into runs."""
    pe = pefile.PE(str(process.DEFAULT_EXE))
    img = pe.get_memory_mapped_image()
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    starts = sorted(atlas)
    text = _atlas_text().lower()
    groups: list[list[int]] = []
    for b in sorted(blocks):
        i = bisect.bisect_right(starts, b) - 1
        owner = starts[i] if i >= 0 else None
        if groups and owner is not None and groups[-1][0] == owner:
            groups[-1][1].append(b)
        else:
            groups.append([owner, [b]])
    out = []
    for owner, bs in groups:
        first = bs[0]
        prev = img[first - 0x400000 - 1]
        after_padding = prev in (0xCC, 0xC3, 0x90) or img[first - 0x400000 - 3] == 0xC2
        mentioned = [hex(x) for x in bs if f"0x{x:x}" in text]
        ins = next(md.disasm(img[first - 0x400000:first - 0x400000 + 16], first), None)
        if owner is not None and atlas[owner]["size"] == 0:
            continue  # entry without a known size: its bracket extends to the next entry
        tail_like = ins is None or ins.mnemonic in ("pop", "ret", "jmp", "leave") or ins.op_str.startswith("esp")
        documented = bool(mentioned)
        out.append({
            # a start already described in the atlas text (e.g. a tail attached to a funclet,
            # F03/C01) is not a missing function
            "likely_unlisted_function": bool(after_padding and not tail_like and not documented),
            "documented_in_atlas_text": documented,
            "first_block": hex(first), "blocks": len(bs),
            "containing_atlas_function": hex(owner) if owner else None,
            "containing_name": atlas.get(owner, {}).get("name") if owner else None,
            "atlas_body_end": hex(owner + atlas[owner]["size"]) if owner else None,
            "starts_after_padding_or_ret": bool(after_padding),
            "address_mentioned_in_atlas_text": mentioned[:5],
            "first_instruction": f"{ins.mnemonic} {ins.op_str}" if ins else None,
        })
    return out


def aggregate() -> dict:
    atlas = coverage.load_atlas()
    boot = json.loads((EMU / "boot" / "boot.json").read_text()) if (EMU / "boot" / "boot.json").exists() else {}
    specs = {s.name: s for s in missions(process.DEFAULT_EXE)}
    union: dict[int, list[str]] = {}
    outside: set[int] = set()
    mission_rows = []
    for name, spec in specs.items():
        d = EMU / "missions" / name
        if not (d / "summary.json").exists():
            mission_rows.append({"name": name, "warp": spec.warp, "status": "not run"})
            continue
        s = json.loads((d / "summary.json").read_text())
        c = json.loads((d / "coverage.json").read_text())
        for a in c["executed_functions"]:
            union.setdefault(int(a, 16), []).append(name)
        outside |= {int(x, 16) for x in c["summary"].get("blocks_outside_atlas_bodies", [])}
        mission_rows.append({
            "name": name, "warp": spec.warp, "start": "pending-mission call" if spec.inject else "warp",
            "mapdata_index": s["mapdata_index"], "map_title": s["map_title"], "cd": s["cd"],
            "status": s["status"], "frames": s["frames"], "fault": s["fault"], "wall_seconds": s["wall_seconds"],
            "virtual_ms": s["virtual_ms"], "initial_seed": s["initial_seed"], "speed_index": s["speed_index"],
            "sound_requests": s["sounds"], "executed_functions": s["executed_functions"],
            "guest_exceptions": len(s["exceptions"]), "message_boxes": s["message_boxes"],
            "unimplemented_apis": s["unimplemented_apis"],
            "checkpoints": {k: s["checkpoints"][k] for k in CHECKPOINTS if k in s["checkpoints"]},
        })
    for a in boot.get("executed_functions", []):
        union.setdefault(int(a, 16), []).append("boot")
    h01 = set(union)
    extra_runs = sorted(EMU.glob("chains/*/coverage.json")) + sorted(EMU.glob("long/*/coverage.json")) + \
        sorted(EMU.glob("input_demo/coverage.json"))
    for f in extra_runs:
        c = json.loads(f.read_text())
        tag = f"{f.parent.parent.name}/{f.parent.name}"
        for a in c["executed_functions"]:
            union.setdefault(int(a, 16), []).append(tag)
        outside |= {int(x, 16) for x in c["summary"].get("blocks_outside_atlas_bodies", [])}
    new_h02 = sorted(set(union) - h01)
    h02_set = set(union)
    h07_funcs: set[int] = set()
    for f in sorted(EMU.glob("h03/*/coverage.json")):
        c = json.loads(f.read_text())
        tag = f"h03/{f.parent.name}"
        h07 = f.parent.name.startswith(H07_RUN_PREFIXES)
        for a in c["executed_functions"]:
            union.setdefault(int(a, 16), []).append(tag)
            if h07:
                h07_funcs.add(int(a, 16))
        outside |= {int(x, 16) for x in c["summary"].get("blocks_outside_atlas_bodies", [])}
    new_h03 = sorted(set(union) - h02_set)
    new_h07 = sorted(a for a in h07_funcs if all(r.startswith(tuple(f"h03/{p}" for p in H07_RUN_PREFIXES))
                                               for r in union[a]))
    outside |= {int(x, 16) for x in boot.get("coverage", {}).get("blocks_outside_atlas_bodies", [])}
    by_status: dict[str, int] = {}
    for a in union:
        st = str(atlas.get(a, {}).get("status"))
        by_status[st] = by_status.get(st, 0) + 1
    excluded = [{"addr": hex(a), "name": atlas[a]["name"], "package": atlas[a]["package"],
                 "runs": len(union[a]), "example_run": union[a][0]}
                for a in sorted(union) if atlas.get(a, {}).get("status") == "excluded"]
    no_status = [{"addr": hex(a), "package": atlas[a]["package"]} for a in sorted(union)
                 if atlas.get(a, {}).get("status") is None]
    total_by_status: dict[str, int] = {}
    for a, x in atlas.items():
        total_by_status[str(x["status"])] = total_by_status.get(str(x["status"]), 0) + 1
    full = {"executed_functions_union": {hex(a): v for a, v in sorted(union.items())},
            "outside_blocks": [hex(b) for b in sorted(outside)],
            "outside_groups": classify_outside(outside, atlas)}
    (EMU / "coverage_union.json").write_text(json.dumps(full, indent=1))
    ok = [r for r in mission_rows if r.get("status") == "ok"]
    result = {
        "description": "Full-game emulation oracle for StarCraft 1.17.0.1 (Unicorn, original StarCraft.exe and "
                       "storm.dll code, HLE Windows boundary). See docs/research/full-game-emulation.md.",
        "executable_sha256": process.EXE_SHA256,
        "boot": {k: boot.get(k) for k in ("stop_reason", "virtual_ms", "main_menu_entered_ms", "game_mode",
                                          "glue_state", "wall_seconds")} |
                {"executed_functions": len(boot.get("executed_functions", [])),
                 "coverage_by_status": boot.get("coverage", {}).get("by_status")},
        "missions": mission_rows,
        "mission_totals": {"listed": len(specs), "run": sum(1 for r in mission_rows if r.get("status") != "not run"),
                           "reached_target_frames": len(ok),
                           "frames_total": sum(r.get("frames", 0) for r in mission_rows)},
        "coverage": {
            "atlas_functions": len(atlas), "atlas_by_status": total_by_status,
            "executed_atlas_functions_union": len(union), "executed_by_status": by_status,
            "executed_excluded": excluded,
            "executed_without_atlas_status": no_status,
            "executed_code_outside_atlas_function_bodies": len(groups := classify_outside(outside, atlas)),
            "likely_unlisted_functions_executed": [g for g in groups if g["likely_unlisted_function"]],
        },
    }
    def newly(funcs):
        return {
            "count": len(funcs),
            "by_status": {st: sum(1 for a in funcs if str(atlas.get(a, {}).get("status")) == st)
                          for st in sorted({str(atlas.get(a, {}).get("status")) for a in funcs})},
            "excluded": [{"addr": hex(a), "name": atlas[a]["name"], "package": atlas[a]["package"],
                          "example_run": union[a][0]} for a in funcs if atlas.get(a, {}).get("status") == "excluded"],
            "without_atlas_entry": [{"addr": hex(a), "example_run": union[a][0]} for a in funcs
                                    if atlas.get(a, {}).get("status") is None],
        }
    result["coverage"]["newly_executed_by_endings_input_long_runs"] = newly(new_h02)
    result["coverage"]["newly_executed_by_scripted_play_h03"] = newly(new_h03)
    result["coverage"]["executed_only_by_no_cheat_runs_h07"] = newly(new_h07) | {
        "runs": sorted(f.parent.name for f in EMU.glob("h03/*/coverage.json")
                       if f.parent.name.startswith(H07_RUN_PREFIXES)),
        "functions": [{"addr": hex(a), "name": atlas.get(a, {}).get("name"),
                       "status": atlas.get(a, {}).get("status")} for a in new_h07]}
    result.update(h02_sections())
    result["scripted_play_h03"] = h03_sections()
    EVIDENCE.write_text(json.dumps(result, indent=1))
    return result


def _sha(path: Path) -> str | None:
    import hashlib
    return hashlib.sha1(path.read_bytes()).hexdigest() if path.exists() else None


def h02_sections() -> dict:
    """Mission endings (campaign chains, single-mission endings, defeat), input demo, long runs."""
    chains, singles = [], []
    for f in sorted(EMU.glob("chains/*/chain.json")):
        c = json.loads(f.read_text())
        # The name a mission's SetNextScenario left (0x526526 at the resolver 0x4bba20): the chain's events come in
        # order, each mission's after its create_mission.
        next_scenario, current = {}, -1
        for e in c["events"]:
            if e["kind"] == "create_mission":
                current += 1
            elif e["kind"] == "next_scenario":
                next_scenario[current] = e["file"]
        rows = []
        for k, x in enumerate(c["missions"]):
            saves = x.get("profile_saves") or []
            rows.append({"mission": x["mission"], "result": x.get("result"), "result_frame": x.get("result_frame"),
                         "next_mission": x.get("next_mission"), "next_scenario": next_scenario.get(k),
                         "states": [s for i, s in enumerate(x.get("states", []))
                                    if i == 0 or s != x["states"][i - 1]],
                         "events": [e["kind"] + (f"({e['arg']})" if "arg" in e else "") for e in x.get("events", [])],
                         "profile_progress_saved": saves[-1] if saves else None})
        entry = {"name": c["name"], "start_warp": c["warp"], "cheat": c["cheat"],
                 "frames_before_cheat": c["frames_before_cheat"], "finished": c["finished"],
                 "wall_seconds": c["wall_seconds"], "missions": rows,
                 "next_scenario_files": [e["file"] for e in c["events"] if e["kind"] == "next_scenario"],
                 "exceptions": len(c["exceptions"]), "unimplemented_apis": c["unimplemented_apis"]}
        (singles if c["name"].startswith(("single-", "defeat")) else chains).append(entry)
    longs = []
    names = sorted({p.name[:-2] for p in EMU.glob("long/*-a")})
    for n in names:
        a, b = EMU / "long" / f"{n}-a", EMU / "long" / f"{n}-b"
        sa = json.loads((a / "summary.json").read_text()) if (a / "summary.json").exists() else {}
        sb = json.loads((b / "summary.json").read_text()) if (b / "summary.json").exists() else {}
        longs.append({"mission": n, "warp": sa.get("warp"), "status": [sa.get("status"), sb.get("status")],
                      "frames": [sa.get("frames"), sb.get("frames")], "fault": [sa.get("fault"), sb.get("fault")],
                      "wall_seconds": [sa.get("wall_seconds"), sb.get("wall_seconds")],
                      "trace_sha1": [_sha(a / "trace.jsonl"), _sha(b / "trace.jsonl")],
                      "sounds_sha1": [_sha(a / "sounds.jsonl"), _sha(b / "sounds.jsonl")],
                      "identical": _sha(a / "trace.jsonl") == _sha(b / "trace.jsonl") and
                      _sha(a / "sounds.jsonl") == _sha(b / "sounds.jsonl"),
                      "checkpoints": {k: sa.get("checkpoints", {}).get(k) for k in ("1", "10000", "20000")}})
    demo = None
    if (EMU / "input_demo" / "input_demo.json").exists():
        d = json.loads((EMU / "input_demo" / "input_demo.json").read_text())
        rows = d["unit_rows"]
        demo = {k: d[k] for k in ("warp", "box", "right_click", "frame_of_right_click", "selection_before",
                                  "selection_after_drag", "input_log")}
        demo["first_rows_after_click"] = rows[:3]
        demo["last_row"] = rows[-1] if rows else None
    return {"campaign_chains": chains, "single_mission_endings": singles, "long_runs": longs, "input_demo": demo}


def h03_sections() -> list:
    out = []
    for f in sorted(EMU.glob("h03/*/result.json")):
        r = json.loads(f.read_text())
        out.append({"scenario": f.parent.name, "status": r.get("status"), "result": r.get("result"),
                    "frames": r.get("frames"), "wall_seconds": r.get("wall_seconds"),
                    "exceptions": len(r.get("exceptions", [])), "unimplemented_apis": r.get("unimplemented_apis"),
                    "events": [{k: v for k, v in e.items() if k not in ("t", "cells") or k == "cells"}
                               for e in r.get("events", [])]})
    return out
