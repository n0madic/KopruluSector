"""Synthetic mouse input through the game's own selection and order path.

Starts a mission (default Wasteland), runs ``wait_frames`` frames, then drag-selects a
screen rectangle with the left button and right-clicks a map point. The game's window
procedure, input procedures, selection code (P41 drag box) and right-click order code
(P28) handle the messages; the resulting commands go through the turn pipeline.
Recorded: the local selection array 0x6a6070 before/after, and for the selected units
the order (+0x4d), position (+0x28) and move target (+0x10) every 4 frames.
"""
from __future__ import annotations

import json
import struct
import time
from pathlib import Path

from . import game, process, render
from .inputs import Input
from .session import Session, write_json

OUT = Path("~/koprulu-private/emu/input_demo")
SELECTION = 0x6A6070  # 12 CUnit pointers (P41)


def unit_index(p):
    return (p - game.UNIT_POOL) // game.UNIT_SIZE + 1 if p else 0


def run_demo(warp="terran1", cd="original", wait_frames=100, box=(30, 60, 230, 190), target=(420, 250),
             follow_frames=200, out=OUT) -> dict:
    out.mkdir(parents=True, exist_ok=True)
    s = Session(process.Config(command_line=f'"C:\\StarCraft\\StarCraft.exe" ophelia {warp}', cd=cd))
    m = s.m
    inp = Input(s)
    rows: list[dict] = []
    st = {"watch": [], "last": -1}

    def selection():
        return [p for p in struct.unpack("<12I", m.read(SELECTION, 48)) if p]

    def on_frame():
        f = m.r32(game.FRAME_COUNTER)
        if f == st["last"]:
            return
        st["last"] = f
        if st["watch"] and f % 4 == 0:
            units = []
            for p in st["watch"]:
                x, y = struct.unpack("<HH", m.read(p + game.U_POS, 4))
                tx, ty = struct.unpack("<HH", m.read(p + 0x10, 4))
                units.append({"unit": unit_index(p), "type": m.r16(p + game.U_TYPE), "order": m.r8(p + game.U_ORDER),
                              "x": x, "y": y, "move_target": [tx, ty]})
            rows.append({"f": f, "units": units})
    s.hook(game.FRAME_END_PC, on_frame)
    s.start()
    s.run_until(lambda: m.r32(game.GLUE_STATE) in (7, 8, 9) and m.r32(game.READY_ROOM_DIALOG) != 0, 900)
    inp.wait(2000)
    inp.key(ord("S"))
    s.run_until(lambda: m.r16(game.GAME_MODE) == 3 and m.r32(game.FRAME_COUNTER) >= wait_frames, 900)
    render.save_primary_surface(m, out / "before.png")
    before = selection()
    inp.drag(*box)
    inp.wait(300)
    after_drag = selection()
    st["watch"] = after_drag
    render.save_primary_surface(m, out / "selected.png")
    f_click = m.r32(game.FRAME_COUNTER)
    inp.click(*target, right=True)
    end = f_click + follow_frames
    s.run_until(lambda: m.r32(game.FRAME_COUNTER) >= end, 900)
    render.save_primary_surface(m, out / "after.png")
    result = {"warp": warp, "box": box, "right_click": target, "frame_of_right_click": f_click,
              "selection_before": [unit_index(p) for p in before],
              "selection_after_drag": [{"unit": unit_index(p), "type": m.r16(p + game.U_TYPE)} for p in after_drag],
              "input_log": inp.log, "unit_rows": rows}
    from . import coverage
    atlas = coverage.load_atlas()
    write_json(out / "coverage.json", {"executed_functions": [hex(a) for a in s.executed_functions(atlas)],
                                       "summary": s.coverage_summary(atlas)})
    write_json(out / "input_demo.json", result)
    return result
