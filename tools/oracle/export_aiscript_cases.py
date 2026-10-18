#!/usr/bin/env python3
"""Export the original's AIScript VM probes into a Lua golden file (plan phase 7A).

docs/research/evidence/native-ai-opcode-probes.json holds 89 isolated runs of the original's interpreter
ExecuteAiScript 0x493a60, its scheduler RunAiScripts 0x4938e0, the pool (InitAiScriptPool 0x493330,
CreateAiScript 0x494d90, FreeAiScript 0x493240), the directory lookup FindAiScriptOffset 0x494d20 and the helpers
AddAttackUnits 0x4839f0, the defense list helpers 0x482550..0x482e40 and FindScriptSpellcaster 0x4932a0
(tools/research/inspect_ai_opcodes_117.py). The evidence keeps only part of each fixture (`fixture_inputs`); the
rest lives in the probe script's case builders. This exporter mirrors those builders as data: each case gets its
kind, its fixture (the bytecode the probe ran, the tables it wrote) and the values the original produced;
tests/lua/test_aiscript.lua builds the same fixture in a native game and compares. The probe script is hashed
against the evidence's `probe_script_sha256`, so a changed probe fails the export until this mirror is updated.

Output: tests/golden/oracle/aiscript_cases.lua.

Usage: python3 tools/oracle/export_aiscript_cases.py [--check]
"""
from __future__ import annotations

import argparse
import logging
import sys
from typing import Any

from oracle_common import GOLDEN, ROOT, TARGET_SHA256, ExportError, load_evidence, render_module, sha256_file

log = logging.getLogger("export_aiscript_cases")
GENERATOR = "tools/oracle/export_aiscript_cases.py"
EVIDENCE = "native-ai-opcode-probes.json"
PROBE = "tools/research/inspect_ai_opcodes_117.py"
OUTPUT = "aiscript_cases.lua"

# The defense list helpers in the probe's order and the list each fills (kp_game.h KP_AI_DEFENSE_LISTS: build gg,
# ag, ga, aa, then use gg, ag, ga, aa).
DEFENSE_LISTS = {"0x482670": 0, "0x482610": 1, "0x4825b0": 2, "0x482550": 3,
                 "0x482e40": 4, "0x482de0": 5, "0x482d80": 6, "0x482d20": 7}

# The probe's bytecode (inspect_ai_opcodes_117.py isolated_probes).
TRAINING_CODE = [None, 3, 5, 0, 2, 7, 0]  # opcode, count 3, unit 5, then wait 7
SEED_CODE = [0x69, 0x78, 0x56, 0x34, 0x12, 0x02, 0x07, 0x00]


def placed(size: int, parts: dict[int, list[int]]) -> list[int]:
    """`size` zero bytes with each part's bytes at its offset."""
    out = [0] * size
    for at, data in parts.items():
        out[at:at + len(data)] = data
    return out


SHARED_RETURN = {"code": placed(32, {0: [0x40, 0x0A, 0, 0x5A, 0x02, 0x07, 0], 10: [0x02, 0, 0, 0x41]}),
                 "other": placed(32, {0: [0x5A, 0x40, 0x14, 0, 0x02, 0x0B, 0], 20: [0x02, 0, 0, 0x41]})}
ENEMY_RESOURCES = placed(20, {0: [0x5E, 0x64, 0, 0x32, 0, 0x0C, 0, 0x02, 0x01, 0], 12: [0x02, 0x02, 0]})
MULTIRUN = placed(20, {0: [0x48, 0x0A, 0, 0x02, 0, 0, 0x24], 10: [0x02, 0x07, 0]})
KILL_THREAD = [0x61, 0x02, 0x64, 0x00]


def case(c: dict) -> dict[str, Any]:
    kind, inputs, observed = c["kind"], c["fixture_inputs"], c["observed"]
    out: dict[str, Any] = {"kind": kind, "expected": observed}
    if kind == "attack_roster":
        out.update(zip(("player", "count", "unit", "occupied"), inputs))
    elif kind == "defense_roster":
        if inputs[0] not in DEFENSE_LISTS:
            raise ExportError(f"defense_roster: unknown helper {inputs[0]}")
        out.update(list=DEFENSE_LISTS[inputs[0]], **dict(zip(("count", "unit", "occupied"), inputs[1:])))
    elif kind == "training_opcode":
        opcode = inputs[0]
        out.update(code=[opcode] + TRAINING_CODE[1:], **dict(zip(("all5", "all30", "complete5", "complete30"), inputs[1:])))
    elif kind == "seed_opcode":
        out.update(code=SEED_CODE, state=99)
    elif kind == "cross_bin_shared_return":
        out.update(SHARED_RETURN)
    elif kind == "enemy_resource_first_owner":
        out.update(code=ENEMY_RESOURCES, owners=[1, 2] if inputs[0] == 0 else [2, 1],
                   minerals={1: 0, 2: 500}, gas={1: 0, 2: 500})
    elif kind == "spell_unit_eligibility":
        out.update(zip(("type", "energy", "order", "minimum"), inputs))
        out["expected"] = observed != 0  # the probe's unit, or 0
    elif kind == "spell_unit_first_eligible":
        out.update(units=[{"order": order, "energy": (80 - 10 * k) * 256} for k, order in enumerate((3, 0xA0, 2))],
                   minimum=50, first=1)  # the probe's second record (unit + 0x200)
        out["expected"] = 1
    elif kind == "bin_id_resolution":
        out.update(zip(("ai_offset", "ai_id", "bw_id", "bw_offset", "requested"), inputs))
    elif kind == "multirun_scheduler":
        out.update(code=MULTIRUN)
        parent, child = observed[2], observed[5]  # pointers: the parent record and the new head
        if parent == child:
            raise ExportError("multirun_scheduler: the child is the parent")
        out["expected"] = observed[:2] + ["parent"] + observed[3:5] + ["child"]
    elif kind == "flagged_slot_reuse":
        out["expected"] = [observed[0], observed[1], -1 if observed[2] == 0 else observed[2], observed[3]]
    elif kind == "delayed_cross_player_kill":
        out.update(code=KILL_THREAD, victim_player=5, victim_flags=2, victim_wait=2, unmarked_wait=100)
    else:
        raise ExportError(f"unknown probe kind {kind}")
    return out


def build() -> str:
    data, digest = load_evidence(EVIDENCE)
    probe = sha256_file(ROOT / PROBE)
    if data.get("probe_script_sha256") != probe:
        raise ExportError(f"{PROBE} changed since {EVIDENCE} was made: update this mirror and the evidence")
    if not data.get("all_expected_results_matched") or data["checks"] != len(data["cases"]):
        raise ExportError(f"{EVIDENCE}: not every probe matched its static contract")
    header = {
        "source": f"docs/research/evidence/{EVIDENCE}",
        "source_sha256": digest,
        "probe": PROBE,
        "target_sha256": TARGET_SHA256,
        "functions": ["0x493a60 ExecuteAiScript", "0x4938e0 RunAiScripts", "0x493330 InitAiScriptPool",
                      "0x494d90 CreateAiScript", "0x493240 FreeAiScript", "0x494d20 FindAiScriptOffset",
                      "0x4839f0 AddAttackUnits", "0x482550..0x482e40 defense lists", "0x4932a0 FindScriptSpellcaster"],
        "status": "original-code-executed (isolated interpreter, scheduler, pool and helpers)",
    }
    return render_module(GENERATOR, header, [case(c) for c in data["cases"]])


def main() -> int:
    logging.basicConfig(level=logging.INFO, format="%(message)s")
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--check", action="store_true", help="fail if the golden file is out of date")
    args = parser.parse_args()
    text = build()
    path = GOLDEN / OUTPUT
    if args.check:
        if not path.exists() or path.read_text() != text:
            log.error("out of date: %s", OUTPUT)
            return 1
        return 0
    GOLDEN.mkdir(parents=True, exist_ok=True)
    path.write_text(text)
    log.info("wrote %s (%d cases)", path.relative_to(ROOT), text.count("\n\t\t{"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
