#!/usr/bin/env python3
"""Export the original's UI timer passes of an emulator run into a JSON golden file (plan phase 9).

The full-game emulator's interface input log (tools/research/sc_emulator/uilog.py, a run's input.jsonl) records every
UI timer the original set, deleted and fired (SetUiTimer 0x4c5960, DeleteUiTimer 0x4c58f0, DeleteTimersOfOwner
0x4c5840, RunUiTimers 0x4c5a20; atlas P43), with the ticks and the order, and what each fired timer's work did to the
queue. tests/c/test_kp_ui.c replays them through kp_ui's queue and expects the same timers fired in the same order.

Output: tests/golden/oracle/ui/timers_<run>.json, the records (`cases`) in order:
  {"set", owner, id, period, callback, tick}, {"del", owner, id}, {"delall", owner},
  {"pass", tick, {{owner, id}, ...}, {{k, record}, ...}}   (the k-th fired timer, from 0, made those records)

Usage: python3 tools/oracle/export_ui_timers.py [--check]   (a run whose log is absent is skipped)
"""
from __future__ import annotations

import argparse
import json
import logging
import sys
from pathlib import Path

from oracle_common import GOLDEN, ROOT, cases_module, golden_stale, sha256_file, write_golden

log = logging.getLogger("export_ui_timers")
GENERATOR = "tools/oracle/export_ui_timers.py"
EMU = Path("~/koprulu-private/emu")
RUNS = {"build_terran": EMU / "scenarios" / "build_terran" / "input.jsonl"}


def timer_record(r: dict) -> list | None:
    if "tset" in r:
        owner, timer, period, callback = r["tset"]
        return ["set", owner, timer, period, callback, r["t"]]
    if "tdel" in r:
        return ["del", *r["tdel"]]
    if "tdelall" in r:
        return ["delall", r["tdelall"]]
    return None


def build(path: Path) -> dict:
    records, fires = [], 0
    for line in path.read_text().splitlines():
        r = json.loads(line)
        if "tpass" in r:
            during = [[k, x] for k, rec in r.get("during", []) if (x := timer_record(rec)) is not None]
            records.append(["pass", r["tpass"], r["fired"], during])
            fires += len(r["fired"])
        elif (x := timer_record(r)) is not None:
            records.append(x)
    return cases_module({"run": path.parent.name, "source_sha256": sha256_file(path), "fires": fires}, records)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--check", action="store_true", help="fail when a golden file is stale")
    args = ap.parse_args()
    logging.basicConfig(level=logging.INFO, format="%(message)s")
    stale = 0
    for name, path in RUNS.items():
        out = GOLDEN / "ui" / f"timers_{name}.json"
        if not path.exists():
            log.info("%s: no emulator log, skipped", name)
            continue
        data = build(path)
        if args.check:
            if golden_stale(out, data):
                log.error("%s is stale; run %s", out.relative_to(ROOT), GENERATOR)
                stale += 1
            continue
        out.parent.mkdir(parents=True, exist_ok=True)
        write_golden(out, data)
        log.info("wrote %s", out.relative_to(ROOT))
    return 1 if stale else 0


if __name__ == "__main__":
    sys.exit(main())
