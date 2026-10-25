#!/usr/bin/env python3
"""Export the CRC-32 of the original's interface panels from emulator screen captures (plan phase 9, level 2).

A screen capture (tools/research/sc_emulator/uiscreen.py, `play/scenario --screens`) holds the 640 x 480 screen as
the original composed it and the open dialogs with their rectangles. The panel oracle compares a panel kp_ui draws
into its own surface with the crop of the capture at that dialog's rectangle (ADR 0011): this exporter keeps the
crop's CRC-32, not its pixels. A crop the cursor overlaps (the capture's layer-0 rectangle) is left out.

Output: tests/golden/oracle/ui/panels.lua, one case per captured panel:
  {run, frame, race, bin, load, left, top, right, bottom, crc}
tests/lua/test_kp_ui.lua draws each panel it knows (the static ones first) and compares the CRC.

Usage: python3 tools/oracle/export_ui_panels.py [--check]   (a run whose captures are absent is skipped)
"""
from __future__ import annotations

import argparse
import logging
import sys
import zlib
from pathlib import Path

from oracle_common import GOLDEN, ROOT, render_module

sys.path.insert(0, str(ROOT / "tools" / "research"))
from sc_emulator.uiscreen import ScreenCapture  # noqa: E402

log = logging.getLogger("export_ui_panels")
GENERATOR = "tools/oracle/export_ui_panels.py"
OUTPUT = GOLDEN / "ui" / "panels.lua"
EMU = Path("~/koprulu-private/emu")
# run -> (the captures' directory, the local player's race: 0 Zerg, 1 Terran, 2 Protoss)
RUNS = {"build_terran": (EMU / "scenarios" / "build_terran" / "screens", 2)}
PANELS = ("rez\\stat_f10.bin",)  # the dialogs whose drawing kp_ui has


def overlaps(a: tuple[int, int, int, int], b: tuple[int, int, int, int]) -> bool:
    return not (a[2] < b[0] or b[2] < a[0] or a[3] < b[1] or b[3] < a[1])


def cases() -> list[list]:
    out = []
    for run, (directory, race) in RUNS.items():
        if not directory.exists():
            log.info("%s: no captures, skipped", run)
            continue
        for path in sorted(directory.glob("screen*.cap")):
            cap = ScreenCapture(path)
            cx, cy, cw, ch = cap.header["cursor"]["rect"]
            cursor = (cx, cy, cx + cw - 1, cy + ch - 1)
            for d in cap.header["dialogs"]:
                ref = d["ref"]
                if not isinstance(ref, list) or ref[1] != 0 or ref[0] not in PANELS:
                    continue
                rect = tuple(d["rect"])
                if cap.header["cursor"]["enabled"] and overlaps(rect, cursor):
                    continue
                load = ref[2] if len(ref) > 2 else 0  # captures before the load index name the first load
                out.append([run, cap.header["frame"], race, ref[0], load, *rect, zlib.crc32(cap.crop(*rect))])
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--check", action="store_true", help="fail when the golden file is stale")
    args = ap.parse_args()
    logging.basicConfig(level=logging.INFO, format="%(message)s")
    found = cases()
    if not found:
        log.info("no captures, skipped")
        return 0
    text = render_module(GENERATOR, {}, found)
    if args.check:
        if not OUTPUT.exists() or OUTPUT.read_text() != text:
            log.error("%s is stale; run %s", OUTPUT.relative_to(ROOT), GENERATOR)
            return 1
        return 0
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    OUTPUT.write_text(text)
    log.info("wrote %s (%d panels)", OUTPUT.relative_to(ROOT), len(found))
    return 0


if __name__ == "__main__":
    sys.exit(main())
