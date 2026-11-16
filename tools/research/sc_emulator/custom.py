"""Plays a custom game in the original through its own Play Custom screen (phase M, stage M0).

The map is put into the overlay as `<Documents>\\Starcraft\\maps\\<NAME>` (with `expansion`:
`maps\\BroodWar\\<NAME>`, the folder InitMapList 0x455aa0 enters on the first visit with Brood War). Its name starts
with `!`, so it sorts before every file of the installation's folder and BuildMapList 0x45ba40 preselects it: the
first entry with the extension scx or scm. From a normal start the driver goes through the glue by input: the main
menu's Single Player, the expansion's game mode, a new ID on the login screen, the campaign menu's Play Custom (glue
state 11, `rez\\gluCustm.bin`, RunCreateGameScreen 0x455550 with ECX = 0).

On the screen it sets the drop-downs as a person does, with the mouse (ComboBoxProc 0x4c63b0: the left button's press
opens the list, the move hovers a row, the release selects it): the game type (control 17) and subtype (18) by their
item text, each opponent slot's type (21..27: Computer or Closed) and race (29..35), and the player's own race (28).
The geometry comes from the controls in memory (rect +0x04, item count +0x46, row height +0x49, opens upward +0x4c
bit 0), so the rows are where the original draws them. Then Ok: a melee game starts at once, a Use Map Settings game
opens the race's ready room, whose Start the driver presses (hotkey S). From then on no input is given.

Written to <out>: `trace.jsonl` (the frame digest of every frame), `setup.json` (the game header 0x6728d0 and its
template 0x67293d..0x67295c,
the player records 0x6726c8 at StartGame 0x443f40's entry and after the first frame, the single-player custom races
0x5264c3/0x526546.., each drop-down's items and selection as set), `custom.json` (the screens visited, the presses, the
result), `play_custom.cap` (the screen at Ok, uiscreen.capture_screen: the 640x480 indices with the cursor still
drawn, its layer-0 rectangle in the header, the palette, the open dialogs), the oracle's logs (scenario.install_logs: commands.jsonl, ui.jsonl, triggers.jsonl, ai.jsonl, ...) and with
`state_steps` the state dumps of those object steps (state.install) in states/.
"""
from __future__ import annotations

import json
import logging
import time
from dataclasses import dataclass, field
from pathlib import Path

from . import game, process, render, uiscreen, vfs
from .inputs import MK_LBUTTON, VK_RETURN, WM_LBUTTONDOWN, WM_LBUTTONUP, Input
from .session import Session, write_json

log = logging.getLogger(__name__)
MAPS_DIR = vfs.DOCUMENTS_DIR + "\\Starcraft\\maps"
DIALOG_LIST = 0x6CE6D8
PROFILE_NAME = "Custom"
START_GAME = 0x443F40
GAME_HEADER = 0x6728D0       # the current game header, 0x8d bytes (a replay header's +0x14)
TEMPLATE = 0x67293D          # the current game header's template, 32 bytes (OnGameTypeSelected 0x456a40)
PLAYERS = 0x6726C8           # 12 player records of 36 bytes: +0 player, +4 Storm ID, +8 type, +9 race, +0xa force
PLAYER_SIZE = 36
CUSTOM_RACES = 0x5264C3      # ApplyCustomGameRaces 0x455020: flag, then the local race 0x526546 and 7 opponents
LOCAL_RACE = 0x526546
CTRL_GAME_TYPE, CTRL_SUBTYPE = 17, 18
CTRL_SLOT_TYPE, CTRL_LOCAL_RACE, CTRL_SLOT_RACE = 21, 28, 29
FLAG_VISIBLE, FLAG_DISABLED = 0x8, 0x2
RACES = {"zerg": "Zerg", "terran": "Terran", "protoss": "Protoss", "random": "Random"}
SLOT_TYPES = {"computer": "Computer", "closed": "Closed"}


@dataclass
class CustomSpec:
    map_bytes: bytes
    map_name: str                    # the overlay file's name, without the leading "!"; its extension scm or scx
    expansion: bool = False
    game_type: str = ""              # the game type list's item text (e.g. "Melee"); "" keeps the default
    subtype: str = ""                # the subtype list's item text; "" keeps the default
    race: str = ""                   # the player's own race (RACES); "" keeps Random
    slots: list[tuple[str, str]] = field(default_factory=list)  # opponents in slot order: (SLOT_TYPES, RACES)


def _slot_records(m) -> list[dict]:
    out = []
    for p in range(12):
        b = m.read(PLAYERS + PLAYER_SIZE * p, PLAYER_SIZE)
        out.append({"player": int.from_bytes(b[0:4], "little"), "storm": int.from_bytes(b[4:8], "little", signed=True),
                    "type": b[8], "race": b[9], "force": b[10],
                    "name": b[11:].split(b"\0", 1)[0].decode("latin-1")})
    return out


def _setup_snapshot(m) -> dict:
    return {"frame": m.r32(game.FRAME_COUNTER), "template": m.read(TEMPLATE, 32).hex(),
            "game_header": m.read(GAME_HEADER, 0x8D).hex(), "players": _slot_records(m),
            "custom_races_flag": m.read(CUSTOM_RACES, 1)[0], "custom_races": list(m.read(LOCAL_RACE, 8))}


class Controls:
    """The topmost dialog's controls in memory (the P19 control fields)."""

    def __init__(self, m):
        self.m = m

    def find(self, ident: int) -> int:
        root = self.m.r32(DIALOG_LIST)
        c = self.m.r32(root + 0x42) if root else 0
        while c:
            if self.m.r16(c + 0x20) == ident:
                return c
            c = self.m.r32(c)
        return 0

    def rect(self, c: int) -> tuple[int, int, int, int]:
        return tuple(int.from_bytes(self.m.read(c + 4 + 2 * k, 2), "little", signed=True) for k in range(4))

    def usable(self, c: int) -> bool:
        flags = self.m.r32(c + 0x18)
        return bool(flags & FLAG_VISIBLE) and not flags & FLAG_DISABLED

    def items(self, c: int) -> list[str]:
        count, texts = self.m.read(c + 0x46, 1)[0], self.m.r32(c + 0x3A)
        return [self.m.cstr(self.m.r32(texts + 4 * k)).decode("latin-1") for k in range(count)]

    def selected(self, c: int) -> int:
        return self.m.read(c + 0x48, 1)[0]


class ComboError(RuntimeError):
    pass


def select_combo(inp: Input, ctl: Controls, ident: int, text: str) -> dict:
    """Selects the item `text` of combo box `ident` by the mouse: press on the field, move to the row, release."""
    m = inp.m
    c = ctl.find(ident)
    if not c:
        raise ComboError(f"control {ident} not found")
    if not ctl.usable(c):
        raise ComboError(f"control {ident} is hidden or disabled")
    items = ctl.items(c)
    if text not in items:
        raise ComboError(f"control {ident} has no item {text!r} (items {items})")
    k = items.index(text)
    if ctl.selected(c) == k:
        return {"control": ident, "items": items, "selected": k, "pressed": False}
    left, top, right, bottom = ctl.rect(c)
    x, y = (left + right) // 2, (top + bottom) // 2
    inp.move(x, y)
    inp.wait(150)
    inp.wm.post(inp.hwnd, WM_LBUTTONDOWN, MK_LBUTTON, inp._pos(x, y))
    inp.wait(120)
    left, top, right, bottom = ctl.rect(c)  # opened: the rectangle covers the rows (OpenComboBox 0x4c5f00)
    row = m.read(c + 0x49, 1)[0]
    upward = m.read(c + 0x4C, 1)[0] & 1
    start = top if upward else top + row  # ListRowAtCursor 0x4c62d0
    ty = start + row * k + row // 2
    for i in range(1, 5):
        inp.move(x, y + (ty - y) * i // 4, MK_LBUTTON)
        inp.wait(40)
    inp.wait(100)
    inp.wm.post(inp.hwnd, WM_LBUTTONUP, 0, inp._pos(x, ty))
    inp._note("combo", control=ident, item=k, text=text, x=x, y=ty)
    inp.wait(300)
    got = ctl.selected(c)
    if got != k:
        raise ComboError(f"control {ident}: selected {got} instead of {k} ({text!r})")
    return {"control": ident, "items": items, "selected": k, "pressed": True}


def run_custom(out: Path, spec: CustomSpec, frames: int = 2000, max_wall: float = 7200,
               state_steps: tuple[int, ...] = (), shots: tuple[int, ...] = ()) -> dict:
    out.mkdir(parents=True, exist_ok=True)
    cd = "expansion" if spec.expansion else "original"
    s = Session(process.Config(command_line='"C:\\StarCraft\\StarCraft.exe"', cd=cd))
    m = s.m
    folder = MAPS_DIR + ("\\BroodWar" if spec.expansion else "")
    m.services["fs"].preload(f"{folder}\\!{spec.map_name}", spec.map_bytes)
    inp = Input(s)
    ctl = Controls(m)
    setup: dict = {"combos": []}
    if state_steps:
        from . import state
        for old in (out / "states").glob("s*.state"):
            old.unlink()
        state.install(s, state_steps, out / "states")
    st = {"last_frame": -1}
    events: list[dict] = []

    def ev(kind, **kw):
        e = {"t": m.clock_us // 1000, "kind": kind, "frame": m.r32(game.FRAME_COUNTER), **kw}
        events.append(e)
        log.info("custom %s", e)

    with open(out / "trace.jsonl", "w") as trace:
        def on_frame_end():
            f = m.r32(game.FRAME_COUNTER)
            if f == st["last_frame"]:
                return
            st["last_frame"] = f
            if "after_first_frame" not in setup:
                setup["after_first_frame"] = _setup_snapshot(m)
            if f in shots:
                render.save_primary_surface(m, out / f"frame-{f}.png")
            trace.write(json.dumps(game.digest(m), separators=(",", ":")) + "\n")
            if f >= frames:
                m.request_stop()

        def on_start_game():
            if "start_game" not in setup:
                setup["start_game"] = _setup_snapshot(m)

        s.hook(game.FRAME_END_PC, on_frame_end)
        s.hook(START_GAME, on_start_game)
        from . import scenario
        logs = scenario.install_logs(s, out)
        surface = uiscreen.track_surface(s)
        s.start()
        t0 = time.time()
        campaign = 22 if spec.expansion else 6
        steps = [(0, "rez\\gluMain.bin", "S"), (0, "rez\\gluGameMode.bin", "E" if spec.expansion else "O"),
                 (5, "rez\\gluPEdit.bin", None), (5, "rez\\gluLogin.bin", "O"),
                 (campaign, "rez\\gluExpCmpgn.bin" if spec.expansion else "rez\\gluCmpgn.bin", "U"),
                 (11, "rez\\gluCustm.bin", "")]
        visited: list[list] = []
        finished = None

        def top_bin():
            ref = logs["uilog"]["dialog_ref"](m.r32(DIALOG_LIST))
            return ref[0].lower() if isinstance(ref, list) else None

        def settle(want_state, want_bin, settle_ms=2500):
            last, since = None, m.clock_us
            while time.time() - t0 < max_wall:
                inp.wait(100)
                cur = (m.r16(game.GAME_MODE), m.r32(game.GLUE_STATE), m.r32(DIALOG_LIST))
                if cur != last:
                    last, since = cur, m.clock_us
                    visited.append([m.clock_us // 1000, cur[0], cur[1], top_bin()])
                    log.info("custom screen %s", visited[-1])
                    continue
                if m.clock_us - since >= settle_ms * 1000 and cur[0] == 4 and cur[1] == want_state \
                        and top_bin() == want_bin.lower():
                    return True
            return False

        def wait_for(pred, limit_s):
            return s.run_until(pred, min(limit_s, max(1.0, max_wall - (time.time() - t0)))) == "predicate"

        try:
            for state_no, bin_name, keys in steps:
                if not settle(state_no, bin_name):
                    finished = f"the screen {bin_name} did not come"
                    break
                ev("press", glue=state_no, dialog=bin_name, keys=keys)
                if keys is None:
                    inp.text(PROFILE_NAME)
                    inp.key(VK_RETURN)
                    continue
                for k, ch in enumerate(keys):
                    if k:
                        inp.wait(500)
                    inp.key(ord(ch))
            if finished is None:
                if spec.game_type:
                    setup["combos"].append(select_combo(inp, ctl, CTRL_GAME_TYPE, spec.game_type))
                    inp.wait(1500)  # the map's information and slots are refreshed on a 100 ms timer
                if spec.subtype:
                    setup["combos"].append(select_combo(inp, ctl, CTRL_SUBTYPE, spec.subtype))
                    inp.wait(500)
                for i, (kind, race) in enumerate(spec.slots):
                    setup["combos"].append(select_combo(inp, ctl, CTRL_SLOT_TYPE + i, SLOT_TYPES[kind]))
                    if kind == "computer":
                        setup["combos"].append(select_combo(inp, ctl, CTRL_SLOT_RACE + i, RACES[race]))
                if spec.race:
                    setup["combos"].append(select_combo(inp, ctl, CTRL_LOCAL_RACE, RACES[spec.race]))
                setup["screen"] = {str(i): {"items": ctl.items(c), "selected": ctl.selected(c), "usable": ctl.usable(c)}
                                   for i in (CTRL_GAME_TYPE, CTRL_SUBTYPE, *range(20, 36))
                                   if (c := ctl.find(i))}
                render.save_primary_surface(m, out / "play_custom.png")
                uiscreen.capture_screen(m, surface, out / "play_custom.cap", {"glue_state": 11},
                                        names=logs["uilog"]["dialog_ref"])
                ev("ok")
                inp.key(ord("O"))
                if not wait_for(lambda: m.r16(game.GAME_MODE) == 3 or m.r32(game.GLUE_STATE) in (7, 8, 9)
                                and m.r32(game.READY_ROOM_DIALOG) != 0, 900):
                    finished = f"no game after Ok (mode {m.r16(game.GAME_MODE)}, glue {m.r32(game.GLUE_STATE)})"
                elif m.r16(game.GAME_MODE) != 3:  # a Use Map Settings game: the ready room
                    ready_at = m.clock_us
                    wait_for(lambda: m.clock_us - ready_at > 2_000_000, 300)
                    ev("ready_room_start", glue=m.r32(game.GLUE_STATE))
                    inp.key(ord("S"))
                    def started():
                        return m.r16(game.GAME_MODE) == 3 and m.r32(game.FRAME_COUNTER) > 0
                    # A run of --frames 1 stops in the frame hook inside this wait: the game started all the same.
                    if not wait_for(started, 900) and not started():
                        finished = "the game did not start from the ready room"
            if finished is None:
                wait_for(lambda: st["last_frame"] >= frames or m.r16(game.GAME_MODE) != 3, max_wall)
                if st["last_frame"] >= frames:
                    finished = "ok"
                else:
                    finished = (f"game ended at frame {st['last_frame']} (mode {m.r16(game.GAME_MODE)}, exit code "
                                f"{m.r16(game.GAME_EXIT_CODE)})")
        except ComboError as e:
            render.save_primary_surface(m, out / "combo_error.png")
            finished = f"combo: {e}"
        except Exception as e:  # noqa: BLE001 - the run's fault is its result
            log.exception("custom game failed")
            finished = f"fault: {str(e)[:1500]}"
    write_json(out / "setup.json", setup)
    result = {"finished": finished or "wall-time limit", "wall_seconds": round(time.time() - t0, 1),
              "virtual_ms": m.clock_us // 1000, "visited": visited, "events": events, "last_frame": st["last_frame"],
              "map": spec.map_name, "expansion": spec.expansion, "game_type": spec.game_type, "subtype": spec.subtype,
              "race": spec.race, "slots": spec.slots, "input_log": inp.log}
    write_json(out / "custom.json", result)
    return result
