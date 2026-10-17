"""Test scenarios in the full game (phase 3 acceptance level C, docs/phase3.md).

A scenario spec (tests/scenarios/<name>.json) names a base campaign mission. The game is
started through that mission's normal start path; at every call of ChkLoadSections 0x4c1200
(the header, version and section loads) the map buffer is replaced by the scenario CHK that
converter/scenarios.py builds from the same base map, so the original plays exactly the map
Koprulu converts. Then:
- every executed game command is logged at RecordCommand 0x478830 with the frame counter, the
  turn counter and the issuing player (commands.jsonl), so Koprulu can replay the same
  command stream;
- every change of the local player's interface selection is logged at SetLocalSelectionArray
  0x49cab0 with the frame counter and the unit numbers (except the removals of the game itself,
  which Koprulu makes as the original does), and every right-click marker at
  MovePlacementCursor 0x417ad0 with the map point (ui.jsonl): the interface draws the
  selection circles and moves the marker sprite before the command runs, and Koprulu replays
  both;
- the state is dumped at the entry of object steps 1..frames+1 (sc_emulator/state.py);
- inputs of the spec are sent as window messages once the frame counter reaches their
  frame (sc_emulator/play.py): {"frame": F, "select": [k, ...]} drags a selection box around
  spec units k (0-based), {"frame": F, "key": "S"} presses a hotkey, {"frame": F, "button": k} clicks
  command-card button k (1-9), {"frame": F,
  "right_click": [x, y]} and {"frame": F, "right_click_unit": k} right-click a map point or a
  unit; {"frame": F, "alliance": [a, b, v]} sets player a's alliance towards b (a fixture of a trigger's). Each input posts its messages over a few virtual milliseconds; the command log records
  when the resulting commands actually ran (inputs.jsonl, the frames the inputs started at);
- every trigger action the action runner 0x4bdf10 dispatches is logged with the frame, the current player, the
  trigger's place in that player's list, the action index and type and the handler's result (triggers.jsonl,
  the trigger trace of plan phase 5), and ui.jsonl also logs the frames of a paused game ("paused"), the
  interface's end of the local player's trigger wait ("release"), the Escape skip ("skip") and, before the game, the
  briefing's mission objectives ("objectives").
Campaign mission runs with state dumps install the same logs (install_logs, `mission NAME WARP --state A-B`).

Usage (from tools/research, private venv):
  python -m sc_emulator scenario ../../tests/scenarios/idle_marines.json [--out-root DIR]
"""
from __future__ import annotations

import json
import logging
import struct
import sys
from pathlib import Path

from . import campaigns, game, process, state
from .mission import run_mission

log = logging.getLogger(__name__)

REPO = Path(__file__).resolve().parents[3]
OUT_ROOT = Path("~/koprulu-private/emu/scenarios")
CHK_LOAD_SECTIONS = 0x4C1200  # ECX = CHK bytes, EDX = length
RECORD_COMMAND = 0x478830     # ECX = command bytes, EDX = length
COMMAND_PLAYER = 0x508D44     # issuing player's slot while commands execute
TURN_COUNTER = 0x672658       # turns run (TurnManagerStep 0x476c00); recent selections are stamped with it
SET_LOCAL_SELECTION = 0x49CAB0  # ECX = count, EDX = CUnit pointers: the local player's interface selection
# The game's own removals from the local selection, through SelectUnitsLocally 0x49cf80 (EBP frame) into
# 0x49cab0: RemoveUnitFromSelection 0x4268d0 (a unit dies or leaves) and 0x4a7b80 (a unit leaves sight).
GAME_SELECTION_CALLERS = ((0x4268D0, 0xD2), (0x4A7B80, 0x3A))
MOVE_PLACEMENT_CURSOR = 0x417AD0  # ECX = x, EDX = y: the right-click marker moves to the map point
# HighlightRightClickTarget 0x47fc10 (ECX = x, EDX = y, stack: unit or 0; targeting clicks): the
# interface flashes the fog ghost or unit under the click (sprite timer 31), else moves the
# marker (0x417ad0). RightClickInGame 0x47f300 does the same inline; at 0x47f461, where its three
# branches join, EBX = x, ESI = y, EDI = the unit or 0. Marker moves made by either are logged
# with their "mark" event (by their return address) instead of as "cursor" events.
MARK_TARGET, MARK_TARGET_SIZE = 0x47FC10, 0x4C
# RefreshRememberedTerrainOnscreen 0x40b220: each draw of the game screen refreshes the remembered terrain of the 21x14
# tiles in view from the screen's top-left tile (WORDs 0x5264b0, 0x5264b2); logged as "onscreen" for Koprulu to replay.
TERRAIN_ONSCREEN, SCREEN_TILE = 0x40B220, 0x5264B0
# The alliance table, 12 bytes per player (byte 12 a + b: a's stance towards b, 0 enemy); a spec's "alliance" input sets
# one entry as a fixture of what a trigger would do (plan phase 5) and logs it for Koprulu to apply alike.
ALLIANCES = 0x534914
RIGHT_CLICK_MARKED, RIGHT_CLICK_MARKER_RETURN = 0x47F461, 0x47F45C
UNIT_POOL, UNIT_SIZE = 0x5DA3A0, 0x150
# RunTriggerActions 0x4bdf10: the handler call (ESI = the trigger node, ECX = the action) and its return (EAX).
ACTION_CALL, ACTION_RETURN = 0x4BDF72, 0x4BDF74
TRIGGER_PLAYER = 0x6CA754
# TickPausedUnitFlashes 0x444de0: a frame of a paused game (RunPendingFrames 0x444e30), logged as "paused" for Koprulu
# to run the same paused frames (kp_paused_tick). ReleaseLocalTriggerWait 0x4be3a0: the interface's end of the local
# player's trigger wait (a transmission's portrait timer 0x4a7300, a camera pan's arrival), logged as "release";
# SkipLocalTriggerWaits 0x4bda80 (Escape) as "skip".
PAUSED_FRAME, RELEASE_WAIT, SKIP_WAITS = 0x444DE0, 0x4BE3A0, 0x4BDA80
# BriefingSetObjectives 0x44fdc0 (MBRF action 4, ECX = the action): the briefing sets the local player's mission
# objectives before the game starts (0x5349a4); logged as "objectives" with the string id for Koprulu to set alike.
BRIEFING_OBJECTIVES = 0x44FDC0


def build_chk(spec: dict) -> bytes:
    if str(REPO) not in sys.path:
        sys.path.insert(0, str(REPO))
    from converter import scenarios
    from converter.mpq.profile import Resolver

    with Resolver(process.DEFAULT_EXE.parent, full_hash=False) as resolver:
        return scenarios.scenario_chk(scenarios.base_chk(resolver, spec), spec)


def mission_spec(spec: dict):
    """The campaign row whose map is the scenario's base (generated/missions/index.txt order)."""
    names = (REPO / "generated" / "missions" / "index.txt").read_text().split("\n")
    index = 1 + next(i for i, line in enumerate(names) if line.split(" ")[0] == spec["base"])
    return next(m for m in campaigns.missions(process.DEFAULT_EXE) if m.mapdata_index == index)


def install_logs(session, out: Path) -> dict:
    """The oracle's logs of a run (scenario or campaign mission): commands.jsonl, ui.jsonl and triggers.jsonl."""
    m = session.m
    out.mkdir(parents=True, exist_ok=True)
    log_file = open(out / "commands.jsonl", "w")
    st = {"commands": 0}

    def on_command():
        from unicorn import x86_const as X
        data = m.read(m.reg(X.UC_X86_REG_ECX), m.reg(X.UC_X86_REG_EDX))
        log_file.write(json.dumps({"frame": m.r32(game.FRAME_COUNTER), "turn": m.r32(TURN_COUNTER),
                                   "player": m.read(COMMAND_PLAYER, 1)[0], "bytes": data.hex()}) + "\n")
        log_file.flush()
        st["commands"] += 1

    ui_file = open(out / "ui.jsonl", "w")

    def ui_event(e):
        ui_file.write(json.dumps({"frame": m.r32(game.FRAME_COUNTER), **e}) + "\n")
        ui_file.flush()

    session.ui_event = ui_event  # the spec's fixture inputs log through it too

    def on_local_selection():
        from unicorn import x86_const as X
        count, array = m.reg(X.UC_X86_REG_ECX), m.reg(X.UC_X86_REG_EDX)
        caller = m.r32(m.reg(X.UC_X86_REG_EBP) + 4)
        if any(start <= caller < start + size for start, size in GAME_SELECTION_CALLERS):
            return
        ui_event({"select": [(m.r32(array + 4 * k) - UNIT_POOL) // UNIT_SIZE + 1 for k in range(count)]})

    def point_args():
        from unicorn import x86_const as X
        return [(m.reg(r) + 0x8000 & 0xFFFF) - 0x8000 for r in (X.UC_X86_REG_ECX, X.UC_X86_REG_EDX)]  # signed WORDs

    def on_cursor():
        ret = m.r32(m.esp)
        if MARK_TARGET <= ret < MARK_TARGET + MARK_TARGET_SIZE or ret == RIGHT_CLICK_MARKER_RETURN:
            return  # logged with its "mark" event
        ui_event({"cursor": point_args()})

    def log_mark(x, y, p):
        ui_event({"mark": [x, y, (p - UNIT_POOL) // UNIT_SIZE + 1 if p else 0]})

    def on_mark():
        log_mark(*point_args(), m.r32(m.esp + 4))

    def on_right_click_marked():
        from unicorn import x86_const as X
        x, y = [(m.reg(r) + 0x8000 & 0xFFFF) - 0x8000 for r in (X.UC_X86_REG_EBX, X.UC_X86_REG_ESI)]
        log_mark(x, y, m.reg(X.UC_X86_REG_EDI))

    def on_onscreen():
        x, y = struct.unpack("<HH", m.read(SCREEN_TILE, 4))
        ui_event({"onscreen": [x, y]})

    trace_file = open(out / "triggers.jsonl", "w")
    pending = {}

    def on_action_call():
        from unicorn import x86_const as X
        node, p = m.reg(X.UC_X86_REG_ESI), m.r32(TRIGGER_PLAYER)
        pending.update(frame=m.r32(game.FRAME_COUNTER), player=p, trigger=state.trigger_nodes(m, p).index(node),
                       action=m.read(node + 0x967, 1)[0], type=m.read(m.reg(X.UC_X86_REG_ECX) + 0x1A, 1)[0])

    def on_action_return():
        from unicorn import x86_const as X
        trace_file.write(json.dumps({**pending, "result": m.reg(X.UC_X86_REG_EAX)}) + "\n")
        trace_file.flush()

    def on_briefing_objectives():
        from unicorn import x86_const as X
        ui_event({"objectives": m.r32(m.reg(X.UC_X86_REG_ECX) + 4)})

    session.hook(BRIEFING_OBJECTIVES, on_briefing_objectives)
    session.hook(PAUSED_FRAME, lambda: ui_event({"paused": 1}))
    session.hook(RELEASE_WAIT, lambda: ui_event({"release": 1}))
    session.hook(SKIP_WAITS, lambda: ui_event({"skip": 1}))
    session.hook(ACTION_CALL, on_action_call)
    session.hook(ACTION_RETURN, on_action_return)
    session.hook(TERRAIN_ONSCREEN, on_onscreen)
    session.hook(MOVE_PLACEMENT_CURSOR, on_cursor)
    session.hook(MARK_TARGET, on_mark)
    session.hook(RIGHT_CLICK_MARKED, on_right_click_marked)
    session.hook(RECORD_COMMAND, on_command)
    session.hook(SET_LOCAL_SELECTION, on_local_selection)
    return st


def install(session, spec: dict, chk: bytes, out: Path) -> dict:
    m = session.m
    buffer = m.alloc(len(chk))
    m.write(buffer, chk)
    st = install_logs(session, out)
    st["substituted"] = 0

    def on_chk_load():
        from unicorn import x86_const as X
        m.set_reg(X.UC_X86_REG_ECX, buffer)
        m.set_reg(X.UC_X86_REG_EDX, len(chk))
        st["substituted"] += 1

    session.hook(CHK_LOAD_SECTIONS, on_chk_load)
    for old in (out / "states").glob("s*.state"):  # a shorter run must not leave the steps of a longer one
        old.unlink()
    state.install(session, tuple(range(1, spec["frames"] + 2)), out / "states")
    return st


SELECT_MARGIN = 12  # pixels around the selected units' positions


def play_inputs(spec: dict, out: Path):
    """The `play` driver of run_mission for the spec's inputs."""
    def play(session):
        from .play import Player
        pl = Player(session)
        live, units = pl.units(), []
        for su in spec["units"]:  # each spec unit: the nearest live unit of its type not taken yet
            free = [u for u in live if u["type"] == su["type"] and u["index"] not in units]
            units.append(min(free, key=lambda u: abs(u["x"] - su["x"]) + abs(u["y"] - su["y"]))["index"])
        log_file = open(out / "inputs.jsonl", "w")

        def position(k):
            u = next(u for u in pl.units() if u["index"] == units[k])
            return u["x"], u["y"]

        def alive(k):
            return any(u["index"] == units[k] for u in pl.units())

        for inp in spec["inputs"]:
            session.run_until(lambda: pl.frame() >= inp["frame"], 600)
            log_file.write(json.dumps({"frame": pl.frame(), **inp}) + "\n")
            if "select" in inp:
                pts = [position(k) for k in inp["select"] if alive(k)]  # a unit may die before its group is boxed
                if not pts:
                    continue
                x0, y0 = min(x for x, _ in pts) - SELECT_MARGIN, min(y for _, y in pts) - SELECT_MARGIN
                x1, y1 = max(x for x, _ in pts) + SELECT_MARGIN, max(y for _, y in pts) + SELECT_MARGIN
                pl.ensure_visible((x0 + x1) // 2, (y0 + y1) // 2)
                (sx0, sy0), (sx1, sy1) = pl.screen_point(x0, y0), pl.screen_point(x1, y1)
                pl.inp.drag(sx0, sy0, sx1, sy1)
            elif "alliance" in inp:
                a, b, v = inp["alliance"]
                session.m.write(ALLIANCES + 12 * a + b, bytes([v]))
                session.ui_event({"alliance": [a, b, v]})
            elif "key" in inp:
                pl.hotkey(ord(inp["key"].upper()), shift=inp.get("shift", False))
            elif "button" in inp:
                pl.button(inp["button"])
            else:
                clicks = {"right_click": True, "right_click_unit": True, "left_click": False,
                          "left_click_unit": False}
                kind = next((k for k in clicks if k in inp), None)
                if kind is None:
                    raise ValueError(f"unknown scenario input {inp}")
                point = position(inp[kind]) if kind.endswith("_unit") else inp[kind]
                if inp.get("shift"):  # a queued command: Shift held around the click
                    pl.key_down(0x10)
                pl.click_map(*point, right=clicks[kind])
                if inp.get("shift"):
                    pl.key_up(0x10)
        log_file.close()
    return play


def run(spec_path: Path, out_root: Path = OUT_ROOT, calltrace: tuple[int, int, int] | None = None):
    spec = json.loads(Path(spec_path).read_text())
    spec.setdefault("name", Path(spec_path).stem)
    chk = build_chk(spec)
    base = mission_spec(spec)
    base.name = spec["name"]
    st = {}

    def setup(session, out):
        st["run"] = install(session, spec, chk, out)
        if calltrace:
            from . import calltrace as ct
            ct.install(session, *calltrace[:2], out / "calltrace", calltrace[2])

    play = play_inputs(spec, out_root / spec["name"]) if spec.get("inputs") else None
    result = run_mission(base, frames=spec["frames"] + 1, png_frames=(), out_root=out_root, setup=setup, play=play)
    log.info("%s: %s, %d frames, CHK substituted %d times", spec["name"], result.status, result.frames,
             st["run"]["substituted"] if "run" in st else 0)
    return result
