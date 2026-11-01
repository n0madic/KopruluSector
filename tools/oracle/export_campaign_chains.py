#!/usr/bin/env python3
"""Export the emulator's campaign chains into a JSON golden file (plan phase 8).

docs/research/evidence/full-game-emulation.json (`campaign_chains`, `single_mission_endings`, written by
tools/research/sc_emulator/aggregate.py from the private chain runs) records, for every chain the full-game emulator
played through the original's campaign progression (each mission won with "there is no cow level", one lost with
"game over man"), each mission's result, the name its SetNextScenario left, the next mission AdvanceCampaignAfterGame
0x4bb790 created and the profile progress it saved, and how the chain ended. tests/c/test_campaign.c plays the
same chains through kp_campaign (native/kp_core/src/core/kp_campaign.c). docs/research/evidence/native-triggers-static.json adds every Set Next Scenario
name of the campaign maps' triggers (`next_scenario_candidates`) with the map that names it.

Output: tests/golden/oracle/campaign_chains.json.

Usage: python3 tools/oracle/export_campaign_chains.py [--check]
"""
from __future__ import annotations

import argparse
import logging
import re
import sys

from oracle_common import GOLDEN, cases_module, load_evidence, update_goldens

log = logging.getLogger("export_campaign_chains")
EVIDENCE = "full-game-emulation.json"
TRIGGERS = "native-triggers-static.json"
STAREDIT = "\\staredit"
OUTPUT = "campaign_chains.json"
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


def build() -> dict:
    data, digest = load_evidence(EVIDENCE)
    cases = [chain_case(c) for c in data["campaign_chains"] + data["single_mission_endings"]]
    triggers, triggers_digest = load_evidence(TRIGGERS)
    names = sorted({(c["map_path"].split(STAREDIT)[0], c["next_scenario"]) for c in triggers["next_scenario_candidates"]})
    return cases_module({"evidence": EVIDENCE, "evidence_sha256": digest, "triggers": TRIGGERS,
                         "triggers_sha256": triggers_digest,
                         "next_scenarios": [{"map": m, "name": n} for m, n in names]}, cases)


def main() -> int:
    logging.basicConfig(level=logging.INFO, format="%(message)s")
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--check", action="store_true", help="fail if a golden file is out of date")
    args = parser.parse_args()
    return update_goldens({GOLDEN / OUTPUT: build()}, args.check, "tools/oracle/export_campaign_chains.py")


if __name__ == "__main__":
    sys.exit(main())
