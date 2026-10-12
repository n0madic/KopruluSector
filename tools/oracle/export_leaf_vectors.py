#!/usr/bin/env python3
"""Export original-code probe vectors from docs/research/evidence into Lua golden files.

The evidence JSON comes from executing original StarCraft 1.17.0.1 functions in
Unicorn. Only numeric inputs/outputs are exported; provenance (evidence file
SHA-256, target exe SHA-256, function addresses) is written into each file.

Usage: python3 tools/oracle/export_leaf_vectors.py [--check]
"""
from __future__ import annotations

import argparse
import logging
import sys

from oracle_common import GOLDEN, ROOT, TARGET_SHA256, load_evidence, render_module

log = logging.getLogger("export_leaf_vectors")
GENERATOR = "tools/oracle/export_leaf_vectors.py"


def game_rng() -> str:
    data, digest = load_evidence("native-leaf-probes.json")
    cases = []
    for c in data["cases"]:
        if c["kind"] == "raw_rng":
            # observed = result, state after, total counter, source counter
            cases.append({"kind": "raw", "enabled": c["enabled"], "seed": c["seed"], "observed": c["observed"]})
        elif c["kind"] == "ranged_rng":
            # observed = result, state after, source counter; entry state is 0xffffffff
            cases.append({"kind": "range", "enabled": c["enabled"], "bounds": c["bounds"], "observed": c["observed"]})
    header = {
        "source": "docs/research/evidence/native-leaf-probes.json",
        "source_sha256": digest,
        "target_sha256": TARGET_SHA256,
        "functions": ["0x478200 Rand15", "0x478270 RandRange"],
        "status": "original-code-executed (isolated leaf functions)",
    }
    return render_module(GENERATOR, header, cases)


def portrait_rng() -> str:
    data, digest = load_evidence("native-audiovisual-probes.json")
    cases = []
    for c in data["cases"]:
        if c["case"] != "complete_ui_rng_rejection":
            continue
        if not c["passed"] or not c["execution"]["complete_return"]:
            raise SystemExit("portrait RNG evidence contains a failed or incomplete case")
        i = c["inputs"]
        # actual = returned value, x, y, z after the call
        cases.append({"seed": i["seed"], "low": i["low"], "high": i["high"], "mask": i["mask"],
                      "rejections": i["rejection_iterations"], "actual": c["actual"]})
    header = {
        "source": "docs/research/evidence/native-audiovisual-probes.json",
        "source_sha256": digest,
        "target_sha256": TARGET_SHA256,
        "functions": ["0x4a6e00 PortraitRandom"],
        "status": "original-code-executed (complete returns)",
    }
    return render_module(GENERATOR, header, cases)


OUTPUTS = {"rng_game.lua": game_rng, "rng_portrait.lua": portrait_rng}


def main() -> int:
    logging.basicConfig(level=logging.INFO, format="%(message)s")
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--check", action="store_true", help="fail if a golden file is out of date")
    args = parser.parse_args()
    GOLDEN.mkdir(parents=True, exist_ok=True)
    stale = []
    for name, build in OUTPUTS.items():
        text = build()
        path = GOLDEN / name
        if args.check:
            if not path.exists() or path.read_text() != text:
                stale.append(name)
            continue
        path.write_text(text)
        log.info("wrote %s", path.relative_to(ROOT))
    if stale:
        log.error("out of date: %s", ", ".join(stale))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
