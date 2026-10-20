#!/usr/bin/env python3
"""Export the emulator's campaign chains into a Lua golden file (plan phase 8).

docs/research/evidence/full-game-emulation.json (`campaign_chains`, `single_mission_endings`, written by
tools/research/sc_emulator/aggregate.py from the private chain runs) records, for every chain the full-game emulator
played through the original's campaign progression (each mission won with "there is no cow level", one lost with
"game over man"), each mission's result, the name its SetNextScenario left, the next mission AdvanceCampaignAfterGame
0x4bb790 created and the profile progress it saved, and how the chain ended. tests/lua/test_campaign.lua plays the
same chains through game/campaign. docs/research/evidence/native-triggers-static.json adds every Set Next Scenario
name of the campaign maps' triggers (`next_scenario_candidates`) with the map that names it.

Output: tests/golden/oracle/campaign_chains.lua.

Usage: python3 tools/oracle/export_campaign_chains.py [--check]
"""
from __future__ import annotations

import argparse
import logging
import re
import sys

from oracle_common import GOLDEN, ROOT, load_evidence, render_module

log = logging.getLogger("export_campaign_chains")
GENERATOR = "tools/oracle/export_campaign_chains.py"
EVIDENCE = "full-game-emulation.json"
TRIGGERS = "native-triggers-static.json"
STAREDIT = "\\staredit"
OUTPUT = "campaign_chains.lua"
END = re.compile(r"campaign end \(mode (\d+), glue (\d+)\)")
EXPANSION_WARP = "x"  # the warp prefixes xterran, xzerg, xprotoss (atlas P03, the mission-warp table 0x4eba18)


def chain_case(c: dict) -> dict:
    end = END.match(c["finished"])
    missions = []
    for m in c["missions"]:
        if m["result"] is None:  # a single-mission run stops when the next mission starts
            continue
        missions.append({"mission": m["mission"], "result": m["result"], "next_scenario": m.get("next_scenario"),
                         "next_mission": m["next_mission"], "progress": m["profile_progress_saved"]})
    return {"name": c["name"], "expansion": 1 if c["start_warp"].startswith(EXPANSION_WARP) else 0,
            "missions": missions, "end_mode": int(end.group(1)) if end else None,
            "end_glue": int(end.group(2)) if end else None}


def build() -> str:
    data, digest = load_evidence(EVIDENCE)
    cases = [chain_case(c) for c in data["campaign_chains"] + data["single_mission_endings"]]
    triggers, triggers_digest = load_evidence(TRIGGERS)
    names = sorted({(c["map_path"].split(STAREDIT)[0], c["next_scenario"]) for c in triggers["next_scenario_candidates"]})
    return render_module(GENERATOR, {"evidence": EVIDENCE, "evidence_sha256": digest, "triggers": TRIGGERS,
                                     "triggers_sha256": triggers_digest,
                                     "next_scenarios": [{"map": m, "name": n} for m, n in names]}, cases)


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
    log.info("wrote %s (%d chains)", path.relative_to(ROOT), text.count("\n\t\t{"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
