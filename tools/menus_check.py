"""The glue menus' oracle of plan phase 9E (docs/phase9.md): kp_ui walks the campaign path the emulator walked.

`python -m sc_emulator menus DIR` (menus.py) records the original's screens driven by their hotkeys (the main menu,
the game mode, a new ID on the login screen, the expansion campaign menu, a ready room, the load screen, back to Exit)
with the input log (DIR/menus/input.jsonl) and the glue captures (DIR/screens). kp_oracle menus_replay opens the
title (rez\titledlg.bin, when the run starts with it) at the tick the original did, runs its loading text's blink and
the idle events of the start's load, closes it where RunGlueLoop 0x457510's DestroyTitleDialog deleted its timer,
opens the main menu at the tick the original did and follows kp_ui's glue loop (kp_ui_menus_enter, kp_ui_menus_poll; a
ready room for a mission) with the original's input and timer passes; the UI timer records, the movies, the flic
buttons' frames, the menus' CRT draws and every capture (the screen's dialog with its popups composited; the title's
palette too) must equal the original's. Exit status 1 when they differ.

Usage (the emulator's venv): python3 tools/menus_check.py --root DIR
"""
from __future__ import annotations

import argparse
import json
import logging
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import oracle_cli  # noqa: E402
import room_check  # noqa: E402

log = logging.getLogger("menus_check")
MAIN_MENU = "rez\\gluMain.bin"
TITLE = "rez\\titledlg.bin"
ROOM_STATES = (7, 8, 9)  # the glue states of the ready rooms (Terran, Zerg, Protoss)


def menus_lines(path: Path) -> tuple[list[str], list[dict]]:
    """The records from the title's first timer on (the main menu's when the run has no title), as kp_oracle
    room_replay's lines: "title TICK" when it has one, "start TICK" the main menu's first timer; a glue state change is
    a line "glue STATE" before the record that has it."""
    owners, records, started, glue, title, start = {}, [], False, None, None, None
    for line in path.read_text().splitlines():
        r = json.loads(line)
        if "owner" in r:
            owners[r["owner"]] = r["ref"]
            continue
        bin_name = owners.get(r["tset"][0], [None])[0] if "tset" in r else None
        if bin_name == TITLE and title is None and start is None:
            title, started = r["t"], True
        if bin_name == MAIN_MENU and start is None:
            start, started = r["t"], True
        if started:
            records.append(r)
    if start is None:
        return [], []
    lines = ([f"title {title}"] if title is not None else []) + [f"start {start}"]
    glue_at = [None]

    def new_room(r: dict) -> bool:
        """The glue entering a ready room's state (a new game's briefing)."""
        before, glue_at[0] = glue_at[0], r.get("glue", glue_at[0])
        return glue_at[0] in ROOM_STATES and before not in ROOM_STATES

    for r in room_check.renumber(records, new_room):
        if "glue" in r and r["glue"] != glue:
            glue = r["glue"]
            lines.append(f"glue {glue}")
        lines += room_check.record_lines(r, owners)
    return lines, records


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--root", required=True, type=Path, help="the menus run's directory (DIR of menus.py)")
    a = ap.parse_args(argv)
    logging.basicConfig(level=logging.INFO, format="%(levelname)s %(message)s")
    lines, records = menus_lines(a.root / "menus" / "input.jsonl")
    if not lines:
        log.error("%s: no main menu", a.root)
        return 1
    ok = room_check.replay("menus", oracle_cli.command("menus_replay"), lines, a.root / "screens", records, "glue*.cap")
    print(f"menus_check: {'equal' if ok else 'differs'}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
