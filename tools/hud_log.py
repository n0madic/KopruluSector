"""The interface's work in an emulator run, for the panel oracle of plan phase 9B (docs/phase9.md, level 2).

The emulator's input log (input.jsonl, tools/research/sc_emulator/uilog.py) says where the original ran its interface:
each UI timer pass, UpdateStatusUi 0x4a7ee0 ("hud"), PresentFrame 0x4c5360 ("present"), the local selections as the
interface set them ("sel") and the talking portraits of the unit responses ("talk"). tools/lua/hud_check.lua replays
them over the game as a list of entries, each {frame, kind, ...}:
  {f, 1, tick}                                    a timer pass at tick
  {f, 2, tick}                                    a present at tick
  {f, 3, n}                                       n UpdateStatusUi calls in a row
  {f, 4, unit, duration, sound, ping, colour, t}  RequestUnitTalking 0x4a7200 at tick t
  {f, 5, unit, ...}                               a local selection
  {f, 6, code, ...}                               a game call of the interface that the run's ui.jsonl also logs:
                                                  1 a paused frame, 2 the trigger wait's release, 3 the Escape
                                                  skip, 4 x y a draw of the screen at tile (x, y) (in a timer
                                                  pass: after the pass), 5 unit a critter's death of its answers,
                                                  6 v a modeless dialog's pause (1) or unpause (0)
  {f, 7, id, counter}                             the cursor as UpdateCursorLayerRect 0x43e610 placed it
  {f, 8, type, a, b, shift, ctrl, alt, mouse state, tick, screen x, screen y}   an input event (uilog.py "ev": the
                                                  fields its sender writes; the screen -1 when the log lacks it)
  {f, 9, id, tick}                                a WM_COMMAND (an accelerator's command id)
  {f, 10, id, x, y}                               UpdateCursor 0x43ea40 chose a new cursor, the system cursor at (x, y)
                                                  (left out when the log lacks it; it may lie off the screen)
  {f, 11, x, y, step, dx, dy, suppressed, cursor x, cursor y, mouse speed, key speed}   UpdateMapScroll 0x49bf10
                                                  left its state so (uilog.py "scroll": the view, the step, the
                                                  last direction, the edge's suppression and the cursor it saw)
  {f, 12, left, top, right, bottom} / {f, 12}      PlaceTooltip 0x4193e0 there / HideTooltip 0x4191d0 of a shown one
  {f, 13, id, attenuation, pan, unit type, path, result, device, effects, tick}   PlaySoundInternal 0x49e4e0 (uilog.py
                                                  "snd": path 0 rejected, 1 queued, 2 started; unit type -1 none)
after the console's creation (CreateStatusUi 0x4a7d80: the creation tick is the resource dialog's first timer) with
the first timer tick of each console dialog ({bin: tick}).
"""
from __future__ import annotations

import json
from pathlib import Path

CREATE_BIN = "rez\\statres.bin"
PASS, PRESENT, HUD, TALK, SELECT, GAME_CALL, CURSOR, INPUT, COMMAND, CURSOR_CHOICE, SCROLL, TIP, SOUND = range(1, 14)
CALLS = {"paused": 1, "release": 2, "skip": 3, "onscreen": 4, "critter": 5, "menu_pause": 6}
VALUED_CALLS = {"critter", "menu_pause"}  # a call with one value (the others are flags or lists)


def game_call(f: int, e: dict) -> list[int]:
    (name, value), = e.items()
    return [f, GAME_CALL, CALLS[name], *(value if isinstance(value, list) else [value] if name in VALUED_CALLS else [])]


def parse(text: str) -> dict:
    """{create, create_ticks, entries} of an input.jsonl text."""
    entries: list[list[int]] = []
    owners: dict[int, str] = {}
    ticks: dict[str, int] = {}
    create = owner = None
    early: list[list[int]] = []  # the game's sounds before the console exists (the map's units' messages)
    for line in text.splitlines():
        if not line:
            continue
        r = json.loads(line)
        f = r.get("f")
        if "owner" in r and "ref" in r:
            owners[r["owner"]] = r["ref"][0]
        if (f == 0 and "tset" in r and r["tset"][0] in owners and "glue" not in r
                and owners[r["tset"][0]] not in ticks and not entries):
            ticks[owners[r["tset"][0]]] = r["t"]
        if create is not None:
            if "tpass" in r:
                entries.append([f, PASS, r["tpass"]])
                for _, d in r.get("during", []):  # in order: a fired timer's game calls and sounds
                    if "ui" in d:
                        entries.append(game_call(d["f"], d["ui"]))
                    elif "snd" in d:
                        entries.append([d["f"], SOUND, *d["snd"], *d["dev"], d["t"]])
            elif "ui" in r:
                entries.append(game_call(f, r["ui"]))
            elif "cursor" in r:
                entries.append([f, CURSOR, *r["cursor"]])
            elif "ev" in r and "glue" not in r:
                ev = (r["ev"] + [0, 0])[:3]
                entries.append([f, INPUT, *ev, *r["k"], r["ms"], r["t"], *r.get("scr", [-1, -1])])
            elif "curupd" in r:
                entries.append([f, CURSOR_CHOICE, r["curupd"], *r.get("pos", [])])
            elif "scroll" in r:
                entries.append([f, SCROLL, *r["scroll"], *r["speeds"]])
            elif "tip" in r:
                entries.append([f, TIP, *r["tip"]])
            elif "snd" in r:
                entries.append([f, SOUND, *r["snd"], *r["dev"], r["t"]])
            elif "cmd" in r and "glue" not in r:
                entries.append([f, COMMAND, r["cmd"], 0])
            elif "present" in r:
                entries.append([f, PRESENT, r["present"]])
            elif "hud" in r:
                if entries and entries[-1][1] == HUD and entries[-1][0] == f:
                    entries[-1][2] += 1
                else:
                    entries.append([f, HUD, 1])
            elif "talk" in r:
                ping = r.get("ping", [0, 0])
                entries.append([f, TALK, *r["talk"], ping[0], ping[1], r["t"]])
            elif "sel" in r:
                entries.append([f, SELECT, *r["sel"]])
        elif "snd" in r and "glue" not in r:
            early.append([0, SOUND, *r["snd"], *r["dev"], r["t"]])
        elif "ref" in r and r["ref"][0] == CREATE_BIN:
            owner = r["owner"]
        elif owner is not None and "tset" in r and r["tset"][0] == owner:
            create = r["t"]
    if create is None:
        raise ValueError("no console in the input log")
    return {"create": create, "create_ticks": ticks, "entries": early + entries}


CHEAT_WORD = 0x5432FC  # the cheat word a game starts with (what the run's first state holds)


def first_cheats(run: Path) -> int | None:
    """The cheat word of the run's first state dump (states/s00001.state), or None without one."""
    import struct
    import zlib
    state = run / "states" / "s00001.state"
    if not state.exists():
        return None
    data = state.read_bytes()
    nl = data.index(b"\n")
    header, body = json.loads(data[:nl]), data[nl + 1:]
    for name, addr, off, csize, size in header["blobs"]:
        if name == "data" and addr <= CHEAT_WORD < addr + size:
            return struct.unpack_from("<I", zlib.decompress(body[off:off + csize]), CHEAT_WORD - addr)[0]
    return None


def parse_file(path: Path) -> dict:
    hud = parse(path.read_text())
    cheats = first_cheats(path.parent)
    if cheats is not None:
        hud["cheats"] = cheats
    return hud


def to_text(hud: dict) -> str:
    """The entries as lines for tools/lua/hud_replay.lua's parse_text (a Lua literal of a long run has more constants
    than a Lua function may): "create T", "tick BIN T" per console dialog, then one entry per line."""
    lines = [f"create {hud['create']}"] + ([f"cheats {hud['cheats']}"] if "cheats" in hud else [])
    lines += [f"tick {b} {t}" for b, t in hud["create_ticks"].items()]
    lines += [" ".join(str(v) for v in e) for e in hud["entries"]]
    return "\n".join(lines) + "\n"
