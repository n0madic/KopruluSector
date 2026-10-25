"""The interface's behaviour oracle of plan phase 9C (docs/phase9.md, level 1): kp_ui takes each run's input events.

Each scenario run under --root (the emulator's `scenario` runs: <name>/input.jsonl, commands.jsonl, ui.jsonl; with
--kind plays the campaign plays, `play`) is replayed by tools/lua/input_check.lua from its golden
(tests/golden/oracle/<kind>/<name>.lua): the input events
the original's window procedure dispatched (tools/hud_log.py) go through kp_ui, whose local selections must equal
the original interface's and whose queued commands the run's local commands. A run without its golden or its logs
is skipped (and counted). Exit status 1 when a run differs or no run was checked.

Usage: python3 tools/input_check.py --root DIR [--kind scenarios|plays] [--states DIR] [--scenario NAME ...]
       [--luajit luajit] [--jobs N]
"""
from __future__ import annotations

import argparse
import logging
import os
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from tools.hud_log import first_cheats, parse_file, to_text  # noqa: E402

log = logging.getLogger("input_check")


OK, DIFFERS, SKIPPED = "ok", "differs", "skipped"


def check_run(run: Path, luajit: str, kind: str = "scenarios", states: Path | None = None) -> str:
    golden = ROOT / "tests" / "golden" / "oracle" / kind / f"{run.name}.lua"
    if not golden.exists() or not (run / "commands.jsonl").exists() or not (run / "input.jsonl").exists():
        log.warning("%s: no golden or no run logs, skipped", run.name)
        return SKIPPED
    with tempfile.TemporaryDirectory() as tmp:
        hud = Path(tmp) / "hud.txt"
        parsed = parse_file(run / "input.jsonl")
        if "cheats" not in parsed and states is not None:  # the run's states are elsewhere (its first recording)
            cheats = first_cheats(states / run.name)
            if cheats is not None:
                parsed["cheats"] = cheats
        hud.write_text(to_text(parsed))
        r = subprocess.run([luajit, "tools/lua/input_check.lua", str(golden), str(hud), str(run)], cwd=ROOT,
                           capture_output=True, text=True)
    for line in (r.stdout + r.stderr).splitlines():
        (log.info if r.returncode == 0 else log.error)("%s: %s", run.name, line)
    return OK if r.returncode == 0 else DIFFERS


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--root", required=True, type=Path, help="the emulator's scenario runs")
    ap.add_argument("--scenario", action="append", default=[], help="only these runs")
    ap.add_argument("--luajit", default=os.environ.get("LUAJIT", "luajit"))
    ap.add_argument("--jobs", type=int, default=1, help="runs checked at once")
    ap.add_argument("--kind", default="scenarios", choices=("scenarios", "plays"),
                    help="the goldens the runs replay (tests/golden/oracle/<kind>)")
    ap.add_argument("--states", type=Path, help="where the runs' first state dumps are when not with the runs (their "
                    "cheat word: <states>/<run>/states/s00001.state)")
    a = ap.parse_args(argv)
    logging.basicConfig(level=logging.INFO, format="%(levelname)s %(message)s")
    runs = [a.root / n for n in a.scenario] if a.scenario else sorted(p for p in a.root.iterdir()
                                                                         if (p / "input.jsonl").exists())
    with ThreadPoolExecutor(max_workers=max(1, a.jobs)) as pool:
        results = list(pool.map(lambda r: check_run(r, a.luajit, a.kind, a.states), runs))
    failed, skipped = results.count(DIFFERS), results.count(SKIPPED)
    print(f"input_check: {len(runs)} runs, {failed} differ, {skipped} skipped")
    return 1 if failed or skipped == len(runs) else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
