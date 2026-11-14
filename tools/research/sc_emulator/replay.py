"""Plays a `.rep` in the original through its own menus (phase RP0).

The file is put into the overlay as `<Documents>\\Starcraft\\maps\\replays\\<NAME>.rep` before the start. From a
normal start the driver goes through the glue by input: the main menu's Single Player, the expansion's game mode
(Original or Expansion: `expansion`, the replay's Brood War flag, with `cd` = expansion), a new ID on the login screen, the campaign menu's Load Replay
(glue state 12, `rez\\gluCustm.bin`, RunCreateGameScreen 0x455550 with ECX = 1), the replay picked in the list by
typing its name's first letter, then Ok. StartGame 0x443f40 then reads the file (ReadReplayFile 0x480690) and plays
it (FeedReplayCommands 0x4769b0).

`shots` saves the screen at those frames' ends (`frame-<f>.png`); `presses` presses a key once the game reached a frame
(the replay's buttons: P pause and play, U faster, D slower, stat_txt.tbl 1541..1544) and saves the screen a second
later (`key-<f>-<key>.png`).

Written to <out>: `trace.jsonl` (the frame digest of every frame, as a chain's), `dispatch.jsonl` (every buffer the
dispatcher got from the feed: frame and hex bytes), `replay.json` (the screens visited,
the frames, the end of the replay: FeedReplayCommands' end at the header's frame count calls 0x480e00(1) and the
victory dialog 0x4bcff0), the oracle's logs (scenario.install_logs: commands.jsonl, ui.jsonl, triggers.jsonl, ...), and
with `oracle` the state dumps of the first `state_steps` object steps (state.install) in states/: the shape of
tools/state_check.py --kind replays.
"""
from __future__ import annotations

import json
import logging
import time
from pathlib import Path

from unicorn import x86_const as X

from . import game, process, render, vfs
from .inputs import VK_RETURN, Input
from .session import Session, write_json

log = logging.getLogger(__name__)
REPLAY_DIR = vfs.DOCUMENTS_DIR + "\\Starcraft\\maps\\replays"
REPLAY_END = 0x480E00          # FeedReplayCommands' end of the replay, ECX = 1
VICTORY_DIALOG = 0x4BCFF0
FEED = 0x4769B0
DISPATCH = 0x475D30            # ExecuteTurnCommands: ECX bytes, EDX length, [esp+4] from replay
PROFILE_NAME = "Replay"
DIALOG_LIST = 0x6CE6D8


def run_replay(out: Path, rep: bytes, name: str = "A", cd: str = "original", expansion: bool = False,
               max_wall: float = 7200,
               extra_frames: int = 48, oracle: bool = False, state_steps: int = 1000,
               shots: tuple[int, ...] = (), presses: tuple[tuple[int, str], ...] = ()) -> dict:
    out.mkdir(parents=True, exist_ok=True)
    s = Session(process.Config(command_line='"C:\\StarCraft\\StarCraft.exe"', cd=cd))
    m = s.m
    m.services["fs"].preload(f"{REPLAY_DIR}\\{name}.rep", rep)
    inp = Input(s)
    if oracle:
        from . import state
        state.install(s, tuple(range(1, state_steps + 1)), out / "states")
    with open(out / "trace.jsonl", "w") as trace, open(out / "dispatch.jsonl", "w") as dispatch:
        st = {"last_frame": -1, "end_frame": None, "victory_frame": None, "feeds": 0}
        events: list[dict] = []

        def ev(kind, **kw):
            e = {"t": m.clock_us // 1000, "kind": kind, "frame": m.r32(game.FRAME_COUNTER), **kw}
            events.append(e)
            log.info("replay %s", e)

        def on_frame_end():
            f = m.r32(game.FRAME_COUNTER)
            if f == st["last_frame"]:
                return
            st["last_frame"] = f
            if f in shots:
                render.save_primary_surface(m, out / f"frame-{f}.png")
                # the viewer's globals: local slot 0x508d50, local vision 0x672668, replay vision 0x67cc84, reveal 0x67cc88
                ev("shot", local=m.r32(0x508D50), vision=m.r32(0x672668), replay_vision=m.r32(0x67CC84),
                   reveal=m.r32(0x67CC88))
            trace.write(json.dumps(game.digest(m), separators=(",", ":")) + "\n")

        def on_end():
            if st["end_frame"] is None:
                st["end_frame"] = m.r32(game.FRAME_COUNTER)
                ev("replay_end")

        def on_victory():
            if st["victory_frame"] is None:
                st["victory_frame"] = m.r32(game.FRAME_COUNTER)
                ev("victory_dialog")

        def on_dispatch():
            if m.r32(m.esp + 4) != 1:
                return
            n = m.reg(X.UC_X86_REG_EDX)
            data = m.read(m.reg(X.UC_X86_REG_ECX), min(n, 0x10000)) if n else b""
            dispatch.write(json.dumps({"f": m.r32(game.FRAME_COUNTER), "b": data.hex()}) + "\n")

        s.hook(game.FRAME_END_PC, on_frame_end)
        s.hook(DISPATCH, on_dispatch)
        s.hook(REPLAY_END, on_end)
        s.hook(VICTORY_DIALOG, on_victory)
        from . import scenario
        logs = scenario.install_logs(s, out)
        s.start()
        t0 = time.time()
        # (glue state, its topmost dialog's BIN, keys or None for the new ID's name)
        # the game mode popup comes with either CD (the installation has BrooDat.mpq)
        steps = [(0, "rez\\gluMain.bin", "S"), (0, "rez\\gluGameMode.bin", "E" if expansion else "O")]
        campaign = 22 if expansion else 6
        steps += [(5, "rez\\gluPEdit.bin", None), (5, "rez\\gluLogin.bin", "O"),
                  (campaign, "rez\\gluExpCmpgn.bin" if expansion else "rez\\gluCmpgn.bin", "R"),
                  (12, "rez\\gluCustm.bin", name[0] + "O")]
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
                    log.info("replay screen %s", visited[-1])
                    continue
                if m.clock_us - since >= settle_ms * 1000 and cur[0] == 4 and cur[1] == want_state \
                        and top_bin() == want_bin.lower():
                    return True
            return False

        try:
            for state, bin_name, keys in steps:
                if not settle(state, bin_name):
                    finished = f"the screen {bin_name} did not come"
                    break
                ev("press", glue=state, dialog=bin_name, keys=keys)
                if keys is None:
                    inp.text(PROFILE_NAME)
                    inp.key(VK_RETURN)
                    continue
                if state == 12:
                    render.save_primary_surface(m, out / "load_replay.png")
                for k, c in enumerate(keys):
                    if k:
                        inp.wait(500)
                    inp.key(ord(c))
            pending = sorted(presses)
            while finished is None and time.time() - t0 < max_wall:
                inp.wait(100)
                while pending and st["last_frame"] >= pending[0][0]:
                    f, k = pending.pop(0)
                    ev("key", keys=k)
                    inp.key(ord(k.upper()))
                    inp.wait(1000)
                    render.save_primary_surface(m, out / f"key-{f}-{k}.png")
                if st["end_frame"] is not None and (st["last_frame"] >= st["end_frame"] + extra_frames
                                                    or st["victory_frame"] is not None
                                                    and m.clock_us // 1000 - events[-1]["t"] >= 5000):
                    render.save_primary_surface(m, out / "end.png")
                    finished = "replay ended"
                elif m.r16(game.GAME_MODE) == 4 and st["last_frame"] > 0:
                    finished = f"back in the glue (state {m.r32(game.GLUE_STATE)})"
        except Exception as e:  # noqa: BLE001 - the run's fault is its result
            log.exception("replay failed")
            finished = f"fault: {str(e)[:1500]}"
    result = {"finished": finished or "wall-time limit", "wall_seconds": round(time.time() - t0, 1),
              "virtual_ms": m.clock_us // 1000, "visited": visited, "events": events, "end_frame": st["end_frame"],
              "victory_frame": st["victory_frame"], "last_frame": st["last_frame"], "input_log": inp.log,
              "files_written": sorted(m.services["fs"].overlay_names.values())}
    write_json(out / "replay.json", result)
    return result
