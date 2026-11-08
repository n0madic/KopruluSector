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
  0x49cab0 with the frame counter and the unit numbers (except the changes of the game itself,
  which Koprulu makes as the original does), and every right-click marker at
  MovePlacementCursor 0x417ad0 with the map point (ui.jsonl): the interface draws the
  selection circles and moves the marker sprite before the command runs, and Koprulu replays
  both;
- the state is dumped at the entry of object steps 1..frames+1 (sc_emulator/state.py);
- inputs of the spec are sent as window messages once the frame counter reaches their
  frame (sc_emulator/play.py): {"frame": F, "select": [k, ...]} drags a selection box around
  spec units k (0-based; "margin": the box's pixels around them, 12 by default) ({"frame": F, "select_at": [x, y]} one around a map point), {"frame": F, "key": "S"} presses a hotkey (or a named key: "Escape"), {"frame": F, "button": k} clicks
  command-card button k (1-9), {"frame": F, "hold": "Right", "for": N} holds a named key down for N frames,
  {"frame": F, "mouse": [x, y]} moves the cursor to a screen point and leaves it there, {"frame": F, "hover":
  ["card"|"status"|"minimap", id]} leaves it over the middle of a console control, {"frame": F,
  "right_click": [x, y]} and {"frame": F, "right_click_unit": k} right-click a map point or a
  unit; {"frame": F, "alliance": [a, b, v]} sets player a's alliance towards b (a fixture of a trigger's). Each input posts its messages over a few virtual milliseconds; the command log records
  when the resulting commands actually ran (inputs.jsonl, the frames the inputs started at);
- every trigger action the action runner 0x4bdf10 dispatches is logged with the frame, the current player, the
  trigger's place in that player's list, the action index and type and the handler's result (triggers.jsonl,
  the trigger trace of plan phase 5), and ui.jsonl also logs the frames of a paused game ("paused"), the modeless
  dialogs' pause ("menu_pause"), the
  interface's end of the local player's trigger wait ("release"), the Escape skip ("skip"), a critter's death of its
  answers ("critter": the unit) and, before the game, the
  briefing's mission objectives ("objectives");
- every AIScript opcode the interpreter 0x493a60 dispatches is logged with the frame, the script's player, pool slot,
  BIN, PC and opcode (ai.jsonl, the AI trace of plan phase 7).
Campaign mission runs with state dumps install the same logs (install_logs, `mission NAME WARP --state A-B`).

Usage (from tools/research, private venv):
  python -m sc_emulator scenario ../../tests/scenarios/idle_marines.json [--out-root DIR]
"""
from __future__ import annotations

import json
import logging
import os
import struct
import sys
from pathlib import Path

from . import campaigns, game, process, state, uilog
from .mission import run_mission

log = logging.getLogger(__name__)

REPO = Path(__file__).resolve().parents[3]
OUT_ROOT = (Path(__file__).resolve().parents[3] / "koprulu-private/emu/scenarios")
CHK_LOAD_SECTIONS = 0x4C1200  # ECX = CHK bytes, EDX = length
HOVER_DIALOGS = {"card": 0x6AAAE8, "status": 0x6AAB34, "minimap": 0x671A8C}  # a "hover" input's dialogs (P35, P24)
NAMED_KEYS = {"Enter": 0x0D, "Escape": 0x1B, "PageUp": 0x21, "PageDown": 0x22, "End": 0x23, "Home": 0x24, "Left": 0x25, "Up": 0x26,
              "Right": 0x27, "Down": 0x28, "F1": 0x70, "F10": 0x79}  # the virtual-key codes of a spec's named keys
RECORD_COMMAND = 0x478830     # ECX = command bytes, EDX = length
COMMAND_PLAYER = 0x508D44     # issuing player's slot while commands execute
TURN_COUNTER = 0x672658       # turns run (TurnManagerStep 0x476c00); recent selections are stamped with it
SET_LOCAL_SELECTION = 0x49CAB0  # ECX = count, EDX = CUnit pointers: the local player's interface selection
# The game's own changes of the local selection, through SelectUnitsLocally 0x49cf80 (EBP frame) into 0x49cab0:
# RemoveUnitFromSelection 0x4268d0 (a unit dies or leaves), 0x4a7b80 (a unit leaves sight) and AddUnitToLocalSelection
# 0x43c090 (the twin of a two-unit egg whose first unit is selected, 0x43b5e0, within the object step).
GAME_SELECTION_CALLERS = ((0x4268D0, 0xD2), (0x4A7B80, 0x3A), (0x43C090, 0xA0))
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
# the interface's game calls whose place among the interface's work input.jsonl marks (uilog.py "ui")
UI_MARKED = {"paused", "release", "skip", "onscreen", "critter", "menu_pause"}
# PauseGame 0x47cbb0 / UnpauseGame 0x47cc10 called by OpenModelessDialog 0x46ecc0 and CloseModelessDialog 0x46ec70
# (a single player's game stands under the in-game menus and the result dialogs; the trigger action Pause Game's own
# calls are the game's): logged as "menu_pause" 1 / 0 for Koprulu to set the game's pause flag alike. The caller is
# the return address in the modeless functions' range.
PAUSE_GAME, UNPAUSE_GAME, MODELESS_CODE = 0x47CBB0, 0x47CC10, (0x46EC70, 0x46EDA0)
# TickPausedUnitFlashes 0x444de0: a frame of a paused game (RunPendingFrames 0x444e30), logged as "paused" for Koprulu
# to run the same paused frames (kp_paused_tick). ReleaseLocalTriggerWait 0x4be3a0: the interface's end of the local
# player's trigger wait (a transmission's portrait timer 0x4a7300, a camera pan's arrival), logged as "release";
# SkipLocalTriggerWaits 0x4bda80 (Escape) as "skip".
PAUSED_FRAME, RELEASE_WAIT, SKIP_WAITS = 0x444DE0, 0x4BE3A0, 0x4BDA80
# PlayUnitSelectSound 0x446c30 kills a critter selected too often (KillUnit 0x42a4d0 at 0x446dbb, ESI = the unit,
# then the lone sprite 267): logged as "critter" with the unit for Koprulu to do alike (kp_critter_annoyed).
CRITTER_KILL = 0x446DB9
# BriefingSetObjectives 0x44fdc0 (MBRF action 4, ECX = the action): the briefing sets the local player's mission
# objectives before the game starts (0x5349a4); logged as "objectives" with the string id for Koprulu to set alike.
BRIEFING_OBJECTIVES = 0x44FDC0
# CRT rand 0x4d14d4 (per-thread holdrand) draws for the creep tile art (NewCreepTileVariant 0x4c0c40, part of the
# simulation's terrain) and for the interface: the unit voices' choice (ChooseYesSound 0x447130, ChooseWhatSound
# 0x447190) and the other presentation callers (atlas C01). A draw by anything but the creep callback is logged as
# "crt" for Koprulu to draw alike at the same frame, so that the creep art that follows stays the original's.
# The draws of the menus before the game's seed (srand 0x4d14f5 with the game seed at game start, 0x443f40) do not
# reach the game: frame-0 draws wait until the first event or object step past frame 0, and a reseed drops them (a
# frame-0 event, e.g. the briefing's objectives before the seed, does not release them; Koprulu takes all frame-0
# events before its first step, where only the "crt" ones draw, so their order among those does not matter). That
# step is watched one instruction into StepObjects (STEP_BODY), apart from the state dumps' hook at its entry.
CRT_RAND, CRT_SRAND = 0x4D14D4, 0x4D14F5
# ProcessCheatText 0x40dee0 runs on every line the player types (ToggleTextbox 0x4b3bd0) and on the command line's
# tokens (ParseCommandLine 0x4661b0, before any game): srand(0x75), four rand() for its key, then srand(time(NULL)),
# which leaves the CRT generator at the virtual time. Its srand calls are logged as "srand" with the seed (its rand
# calls as "crt") for Koprulu to reseed alike (kp_interface_crt_seed); those of frame 0 wait like the frame-0 draws.
PROCESS_CHEAT_TEXT, PROCESS_CHEAT_TEXT_SIZE = 0x40DEE0, 0xC0
STEP_BODY = game.STEP_OBJECTS + 1  # past `push ebp`
CREEP_VARIANT, CREEP_VARIANT_SIZE = 0x4C0C40, 0x60
# StartPlacement 0x47d3a0 (CL = order, DX = type) and ClosePlacement 0x47d580: the interface's building placement mode,
# which shows a Protoss builder's Psi fields and which the game closes when the builder can no longer place; logged as
# "placement" ([order, type], or [] for the close, which the original only makes of an active mode) for Koprulu to
# replay (kp_build_mode.c). The game's own closes (CanPlaceCurrent 0x47db60 after an order or a removal, an emptied
# selection 0x423120, a result dialog's CancelCursorModes 0x448c20) come within StepObjects and the trigger scheduler
# RunTriggerScheduler 0x4be500 that follows it in the game loop (0x444f56..GAME_STEP_END); Koprulu makes those itself
# at the same point of its step, so only the closes outside that window, the interface's, are logged.
START_PLACEMENT, CLOSE_PLACEMENT = 0x47D3A0, 0x47D580
GAME_STEP_END = 0x444F76  # past `call 0x4be500` in the game loop
# ExecuteAiScript 0x493a60 past its opcode read (ESI = the script, ECX = the PC past the opcode, BL = the opcode):
# every AIScript opcode the interpreter dispatches is logged with the frame, the script's player and pool slot, its
# BIN (flags bit 0), PC and opcode (ai.jsonl, the AI trace of plan phase 7).
AI_OPCODE = 0x493A8E
AI_SCRIPT_POOL, AI_SCRIPT_SIZE = 0x6A1A18, 0x34


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


LOG_FILES = ("commands", "ui", "triggers", "ai", "input")


def install_logs(session, out: Path) -> dict:
    """The oracle's logs of a run (scenario or campaign mission): commands.jsonl, ui.jsonl, triggers.jsonl,
    ai.jsonl and the interface's input (input.jsonl, uilog.py). The returned state's "rotate"(out) moves them to
    another directory (the next game of a chain)."""
    m = session.m
    files = {}
    st = {"commands": 0}

    def rotate(new_out: Path):
        for f in files.values():
            f.close()
        new_out.mkdir(parents=True, exist_ok=True)
        files.update({name: open(new_out / f"{name}.jsonl", "w") for name in LOG_FILES})
        st["pregame"] = True

    rotate(out)
    st["rotate"] = rotate

    def on_command():
        from unicorn import x86_const as X
        data = m.read(m.reg(X.UC_X86_REG_ECX), m.reg(X.UC_X86_REG_EDX))
        files["commands"].write(json.dumps({"frame": m.r32(game.FRAME_COUNTER), "turn": m.r32(TURN_COUNTER),
                                   "player": m.read(COMMAND_PLAYER, 1)[0], "bytes": data.hex()}) + "\n")
        files["commands"].flush()
        st["commands"] += 1

    crt_pending = []

    def write_event(frame, e):
        files["ui"].write(json.dumps({"frame": frame, **e}) + "\n")

    def flush_crt():
        for frame, e in crt_pending:
            write_event(frame, e)
        crt_pending.clear()

    in_game_step = [False]

    def current_frame() -> int:
        """The game's frame; 0 before its first step (a chain's next game starts while the counter still holds the
        frames of the game before it: its menus' events belong before the new game's frame 0)."""
        return 0 if st.get("pregame") else m.r32(game.FRAME_COUNTER)

    def ui_event(e):
        frame = current_frame()
        if frame != 0:
            flush_crt()
        write_event(frame, e)
        files["ui"].flush()
        if UI_MARKED & e.keys() and "uilog" in st:
            st["uilog"]["record"]({"ui": e})

    def on_step_body():
        in_game_step[0] = True
        st["pregame"] = False
        if crt_pending and m.r32(game.FRAME_COUNTER) != 0:
            flush_crt()
            files["ui"].flush()

    def on_game_step_end():
        in_game_step[0] = False

    def on_close_placement():
        if not in_game_step[0]:
            ui_event({"placement": []})

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

    pending = {}

    def on_action_call():
        from unicorn import x86_const as X
        node, p = m.reg(X.UC_X86_REG_ESI), m.r32(TRIGGER_PLAYER)
        pending.update(frame=m.r32(game.FRAME_COUNTER), player=p, trigger=state.trigger_nodes(m, p).index(node),
                       action=m.read(node + 0x967, 1)[0], type=m.read(m.reg(X.UC_X86_REG_ECX) + 0x1A, 1)[0])

    def on_action_return():
        from unicorn import x86_const as X
        files["triggers"].write(json.dumps({**pending, "result": m.reg(X.UC_X86_REG_EAX)}) + "\n")
        files["triggers"].flush()

    def on_ai_opcode():
        from unicorn import x86_const as X
        script = m.reg(X.UC_X86_REG_ESI)
        files["ai"].write(json.dumps({"frame": m.r32(game.FRAME_COUNTER), "player": m.r32(script + 0x10),
                                  "slot": (script - AI_SCRIPT_POOL) // AI_SCRIPT_SIZE, "bin": m.r32(script + 0x30) & 1,
                                  "pc": m.reg(X.UC_X86_REG_ECX) - 1, "op": m.reg(X.UC_X86_REG_EBX) & 0xFF}) + "\n")
        files["ai"].flush()

    def on_briefing_objectives():
        from unicorn import x86_const as X
        ui_event({"objectives": m.r32(m.reg(X.UC_X86_REG_ECX) + 4)})

    def on_crt_rand():
        caller, frame = m.r32(m.esp), current_frame()
        if CREEP_VARIANT <= caller < CREEP_VARIANT + CREEP_VARIANT_SIZE:
            return
        if frame == 0:
            crt_pending.append((frame, {"crt": 1}))
        else:
            ui_event({"crt": 1})

    def on_crt_srand():
        if not PROCESS_CHEAT_TEXT <= m.r32(m.esp) < PROCESS_CHEAT_TEXT + PROCESS_CHEAT_TEXT_SIZE:
            crt_pending.clear()  # the game's seed drops the menus' draws and reseeds
        elif current_frame() == 0:  # e.g. the command line's cheats: kept only past the game's seed
            crt_pending.append((0, {"srand": m.r32(m.esp + 4)}))
        else:
            ui_event({"srand": m.r32(m.esp + 4)})

    def on_start_placement():
        from unicorn import x86_const as X
        ui_event({"placement": [m.reg(X.UC_X86_REG_ECX) & 0xFF, m.reg(X.UC_X86_REG_EDX) & 0xFFFF]})

    def write_input(rec):
        files["input"].write(json.dumps(rec) + "\n")
        files["input"].flush()

    st["uilog"] = uilog.install(session, write_input, current_frame)
    session.hook(BRIEFING_OBJECTIVES, on_briefing_objectives)
    session.hook(START_PLACEMENT, on_start_placement)
    session.hook(CLOSE_PLACEMENT, on_close_placement)
    session.hook(GAME_STEP_END, on_game_step_end)
    session.hook(CRT_RAND, on_crt_rand)
    session.hook(CRT_SRAND, on_crt_srand)
    session.hook(STEP_BODY, on_step_body)
    session.hook(PAUSED_FRAME, lambda: ui_event({"paused": 1}))

    def on_menu_pause(value):
        def hook():
            caller = m.r32(m.esp)
            if MODELESS_CODE[0] <= caller < MODELESS_CODE[1] and m.r16(game.GAME_MODE) == 3:
                ui_event({"menu_pause": value})
        return hook

    session.hook(PAUSE_GAME, on_menu_pause(1))
    session.hook(UNPAUSE_GAME, on_menu_pause(0))
    session.hook(RELEASE_WAIT, lambda: ui_event({"release": 1}))
    session.hook(SKIP_WAITS, lambda: ui_event({"skip": 1}))
    def on_critter_kill():
        from unicorn import x86_const as X
        ui_event({"critter": (m.reg(X.UC_X86_REG_ESI) - UNIT_POOL) // UNIT_SIZE + 1})

    session.hook(CRITTER_KILL, on_critter_kill)
    session.hook(ACTION_CALL, on_action_call)
    session.hook(ACTION_RETURN, on_action_return)
    session.hook(AI_OPCODE, on_ai_opcode)
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
    if os.environ.get("SC_SCREENS"):  # SC_SCREENS=F1,F2,...: screen captures of a scenario run (uiscreen.py)
        from . import uiscreen
        uiscreen.install(session, out / "screens", tuple(int(x) for x in os.environ["SC_SCREENS"].split(",")),
                         names=st["uilog"]["dialog_ref"], debug_png=bool(os.environ.get("SC_SCREENS_PNG")),
                         modeless=True)
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
            if "after_ms" in inp:  # virtual time after the previous input (a menu pauses the game: no frames pass)
                pl.wait(inp["after_ms"])
            else:
                session.run_until(lambda: pl.frame() >= inp["frame"], 600)
            log_file.write(json.dumps({"frame": pl.frame(), **inp}) + "\n")
            if "select" in inp or "select_at" in inp:
                # spec units, or whatever stands at a map point (a unit the game made)
                pts = [tuple(inp["select_at"])] if "select_at" in inp else [position(k) for k in inp["select"] if alive(k)]
                if not pts:
                    continue
                margin = inp.get("margin", SELECT_MARGIN)
                x0, y0 = min(x for x, _ in pts) - margin, min(y for _, y in pts) - margin
                x1, y1 = max(x for x, _ in pts) + margin, max(y for _, y in pts) + margin
                pl.ensure_visible((x0 + x1) // 2, (y0 + y1) // 2)
                (sx0, sy0), (sx1, sy1) = pl.screen_point(x0, y0), pl.screen_point(x1, y1)
                pl.inp.drag(sx0, sy0, sx1, sy1)
            elif "alliance" in inp:
                a, b, v = inp["alliance"]
                session.m.write(ALLIANCES + 12 * a + b, bytes([v]))
                session.ui_event({"alliance": [a, b, v]})
            elif "key" in inp:
                key = inp["key"]
                pl.hotkey(NAMED_KEYS[key] if len(key) > 1 else ord(key.upper()), shift=inp.get("shift", False))
            elif "hold" in inp:  # a key held down for some frames (the scroll keys)
                vk = NAMED_KEYS[inp["hold"]]
                pl.key_down(vk)
                session.run_until(lambda: pl.frame() >= inp["frame"] + inp["for"], 600)
                pl.key_up(vk)
            elif "mouse" in inp:  # the cursor moved to a screen point and left there (the edge scroll)
                pl.inp.move(*inp["mouse"])
            elif "hover" in inp:  # the cursor left over a console control (its tooltip)
                which, cid = inp["hover"]
                l, t, r, b = pl.control_rect(HOVER_DIALOGS[which], cid)
                pl.inp.move((l + r) // 2, (t + b) // 2)
            elif "button" in inp:
                pl.button(inp["button"])
            elif "click" in inp:  # a left click at a screen point (a dialog's button)
                pl.inp.click(*inp["click"])
                pl.wait(200)
            elif "chars" in inp:  # characters typed into the focused control (a dialog's edit box)
                pl.inp.text(inp["chars"])
                pl.wait(200)
            elif "text" in inp:  # a typed line (a cheat): Enter, the text, Enter (scenarios.type_cheats)
                pl.inp.key(0x0D)
                pl.wait(200)
                pl.inp.text(inp["text"])
                pl.wait(200)
                pl.inp.key(0x0D)
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
