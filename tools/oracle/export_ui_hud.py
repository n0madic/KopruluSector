#!/usr/bin/env python3
"""Export the original's console panels over a game for the golden twin of the panel oracle (plan phase 9B, level 2).

tools/ui_check.py compares kp_ui's panels with the emulator's screen captures where the captures are; CI has neither
the captures nor the runs' logs. For a few short runs this exporter keeps what the comparison needs: the interface's
work in the run (tools/hud_log.py's entries up to the last kept frame) and, per captured frame, the CRC-32 of each
console dialog's crop that has no transparent index (flag 0x2000) and that neither the cursor nor the tooltip box
overlaps, and the tooltip box's crop (the panel "tooltip") where the cursor does not overlap it.

Output: tests/golden/oracle/ui/hud_<run>.lua: {run, create, create_ticks, panels = {{frame, bin, left, top, right,
bottom, crc}, ...}, cases = entries}. tests/lua/test_kp_ui.lua replays the run's scenario golden with the entries and
compares the CRCs.

Usage: python3 tools/oracle/export_ui_hud.py [--check]   (a run whose captures are absent is skipped)
"""
from __future__ import annotations

import argparse
import logging
import sys
import zlib
from pathlib import Path

from oracle_common import GOLDEN, ROOT, render_module

sys.path.insert(0, str(ROOT))
from tools.hud_log import parse_file  # noqa: E402
from tools.research.sc_emulator.uiscreen import ScreenCapture  # noqa: E402

log = logging.getLogger("export_ui_hud")
GENERATOR = "tools/oracle/export_ui_hud.py"
CAPTURES = Path("~/koprulu-private/emu/hud")
# run -> the last frame kept (a race each: Terran, Zerg, Protoss; and the console's tooltips)
RUNS = {"train_terran": 200, "zerg_morph": 300, "protoss_build": 300, "tooltips": 285}
CONSOLE = ("rez\\statres.bin", "rez\\statdata.bin", "rez\\statport.bin", "rez\\stat_f10.bin", "rez\\minimap.bin",
           "rez\\statbtnz.bin", "rez\\statbtnt.bin", "rez\\statbtnp.bin")
TRANSPARENT = 0x2000


def overlaps(a: tuple[int, ...], b: tuple[int, ...]) -> bool:
    return not (a[2] < b[0] or b[2] < a[0] or a[3] < b[1] or b[3] < a[1])


def panels(run: Path, last: int) -> list[list]:
    out = []
    for path in sorted((run / "screens").glob("screen*.cap")):
        cap = ScreenCapture(path)
        frame = cap.header["frame"]
        if frame > last:
            continue
        cur = cap.header["cursor"]
        cx, cy, cw, ch = cur["rect"]
        tip = cap.header.get("tooltip") or {}
        tip_rect = None
        if tip.get("enabled") and tip.get("visible") and tip["rect"][2] > 0:
            tx, ty, tw, th = tip["rect"]
            tip_rect = (tx, ty, tx + tw - 1, ty + th - 1)
            if not (cur["enabled"] and overlaps(tip_rect, (cx, cy, cx + cw - 1, cy + ch - 1))):
                out.append([frame, "tooltip", *tip_rect, zlib.crc32(cap.crop(*tip_rect))])
        for d in cap.header["dialogs"]:
            ref, rect = d["ref"], tuple(d["rect"])
            if not isinstance(ref, list) or ref[0] not in CONSOLE or d["flags"] & TRANSPARENT:
                continue
            if cur["enabled"] and overlaps(rect, (cx, cy, cx + cw - 1, cy + ch - 1)):
                continue
            if tip_rect and overlaps(rect, tip_rect):
                continue
            out.append([frame, ref[0], *rect, zlib.crc32(cap.crop(*rect))])
    return out


def export(name: str, last: int) -> str | None:
    run = CAPTURES / name
    if not (run / "screens").is_dir():
        log.info("%s: no captures, skipped", name)
        return None
    hud = parse_file(run / "input.jsonl")
    entries = [e for e in hud["entries"] if e[0] <= last]
    header = {"run": name, "create": hud["create"], "create_ticks": hud["create_ticks"], "panels": panels(run, last)}
    return render_module(GENERATOR, header, entries)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--check", action="store_true", help="fail when a golden file is stale")
    args = ap.parse_args()
    logging.basicConfig(level=logging.INFO, format="%(message)s")
    stale = 0
    for name, last in RUNS.items():
        text = export(name, last)
        if text is None:
            continue
        output = GOLDEN / "ui" / f"hud_{name}.lua"
        if args.check:
            if not output.exists() or output.read_text() != text:
                log.error("%s is stale; run %s", output.relative_to(ROOT), GENERATOR)
                stale += 1
            continue
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(text)
        log.info("wrote %s", output.relative_to(ROOT))
    return 1 if stale else 0


if __name__ == "__main__":
    sys.exit(main())
