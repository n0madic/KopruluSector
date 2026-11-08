"""Run one campaign mission through the game's own start path and observe it frame by frame.

Start path (all original code): the command line carries the "ophelia" cheat and a
mission-warp token such as ``terran1`` or ``xprotoss3``. StarCraft's ParseCommandLine
feeds both to ProcessCheatText (P03 0x40dee0); the warp records a pending mission
(0x457e10). The glue loop (0x457510) consumes it at entry: CreateCampaignMissionGame
(P41 0x4bb6c0) loads the map header and creates the local game, and the mission's
ready room (briefing, P21) opens. The driver then presses the ready room's Start button
with a synthetic key press (its hotkey "S"), which sends the start command through the
turn pipeline exactly like a click. From then on no input is given.

Each frame (after the trigger scheduler of RunPendingFrames) a digest is appended to
``trace.jsonl``; sound and music requests are logged to ``sounds.jsonl``; PNGs are
saved at selected frames.
"""
from __future__ import annotations

import json
import logging
import time
from dataclasses import dataclass, field
from pathlib import Path

from . import coverage, game, process, render
from .session import Session, write_json

log = logging.getLogger(__name__)
OUT_ROOT = (Path.home() / "koprulu-private/emu/missions")
WM_KEYDOWN, WM_KEYUP, WM_CHAR = 0x100, 0x101, 0x102


@dataclass
class MissionSpec:
    name: str            # output directory name, e.g. "terran01"
    warp: str            # mission-warp token, e.g. "terran1"
    cd: str              # "original" or "expansion"
    mapdata_index: int | None = None
    label: str = ""
    # (mission, race, expansion): instead of a warp token, call the game's own pending-mission
    # setter 0x457e10 at the first entry of the glue loop (used for mission 63, which the
    # warp cheat refuses)
    inject: tuple | None = None


@dataclass
class MissionResult:
    spec: MissionSpec
    frames: int = 0
    status: str = "not started"
    fault: str | None = None
    wall_seconds: float = 0
    checkpoints: dict = field(default_factory=dict)
    end_mode: int | None = None
    sounds: int = 0
    pngs: list = field(default_factory=list)


def run_mission(spec: MissionSpec, frames: int = 3000, png_frames=(1, 500, 1000, 2000, 3000),
                checkpoint_every: int = 500, max_wall: float = 7200, sound_device: bool = True,
                out_root: Path = OUT_ROOT, atlas=None, setup=None, play=None) -> MissionResult:
    """`setup(session, out)` (optional) installs extra hooks before the program starts;
    `play(session)` (optional) drives the game with input once it runs, before the remaining
    frames."""
    out = out_root / spec.name
    out.mkdir(parents=True, exist_ok=True)
    res = MissionResult(spec)
    t0 = time.time()
    cmd = '"C:\\StarCraft\\StarCraft.exe"' + ("" if spec.inject else f" ophelia {spec.warp}")
    cfg = process.Config(command_line=cmd, cd=spec.cd, sound_device=sound_device)
    s = Session(cfg)
    m = s.m
    wm = m.services["wm"]
    trace = open(out / "trace.jsonl", "w")
    sounds = open(out / "sounds.jsonl", "w")
    state = {"last_frame": -1, "frames": 0, "start_pressed": False, "game_seen": False}

    def on_frame_end():
        f = m.r32(game.FRAME_COUNTER)
        if f == state["last_frame"]:
            return
        state["last_frame"] = f
        d = game.digest(m)
        trace.write(json.dumps(d, separators=(",", ":")) + "\n")
        state["frames"] += 1
        if f in png_frames:
            p1, p2 = out / f"frame{f:05d}_screen.png", out / f"frame{f:05d}_present.png"
            render.save_screen_bitmap(m, p1)
            render.save_primary_surface(m, p2)
            res.pngs += [p1.name, p2.name]
        if f % checkpoint_every == 0 or f == 1:
            res.checkpoints[f] = d
        if f % 100 == 0:
            log.info("%s frame %d wall %.1fs blocks %d units %d", spec.name, f, time.time() - t0,
                     m.native.blocks_executed(), d["u"])
        if f >= frames:
            m.request_stop()

    def sound_hook(name):
        def fn():
            ev = game.sound_event(m, name)
            sounds.write(json.dumps(ev, separators=(",", ":")) + "\n")
            res.sounds += 1
        return fn

    s.hook(game.FRAME_END_PC, on_frame_end)
    if spec.inject:
        done = []

        def inject():
            if not done:
                done.append(1)
                mission, race, exp = spec.inject
                m.inject_call(game.SET_PENDING_MISSION, (exp,), {"ecx": mission, "edx": race})
        s.hook(game.GLUE_LOOP, inject)
    for addr, name in game.SOUND_HOOKS.items():
        s.hook(addr, sound_hook(name))
    if setup:
        setup(s, out)
    s.start()

    def phase(pred, limit):
        remaining = max_wall - (time.time() - t0)
        return s.run_until(pred, min(limit, remaining))

    try:
        # 1. ready room open
        r = phase(lambda: m.r32(game.GLUE_STATE) in (7, 8, 9) and m.r32(game.READY_ROOM_DIALOG) != 0, 900)
        if r != "predicate":
            res.status = f"ready room not reached ({r}); glue state {m.r32(game.GLUE_STATE)}"
            return res
        ready_at = m.clock_us
        phase(lambda: m.clock_us - ready_at > 2_000_000, 300)
        # 2. press Start (hotkey S) and wait for the game loop
        from .inputs import Input
        Input(s).key(ord("S"))
        state["start_pressed"] = True
        r = phase(lambda: m.r16(game.GAME_MODE) == 3 and m.r32(game.FRAME_COUNTER) > 0, 900)
        if r != "predicate":
            res.status = f"game did not start ({r}); mode {m.r16(game.GAME_MODE)} glue {m.r32(game.GLUE_STATE)}"
            return res
        # 3. scripted input, then the remaining frames
        if play:
            play(s)
        r = phase(lambda: state["frames"] >= frames or m.r16(game.GAME_MODE) != 3, max_wall)
        res.end_mode = m.r16(game.GAME_MODE)
        if state["frames"] >= frames:
            res.status = "ok"
        elif m.r16(game.GAME_MODE) != 3:
            res.status = f"game ended at frame {state['last_frame']} (exit code {m.r16(game.GAME_EXIT_CODE)})"
        else:
            res.status = f"stopped: {r}"
    except Exception as e:  # faults are results, not crashes of the harness
        log.exception("mission %s failed", spec.name)
        res.status = "fault"
        res.fault = str(e)[:2000]
    finally:
        res.frames = state["frames"]
        res.wall_seconds = round(time.time() - t0, 1)
        trace.close()
        sounds.close()
        atlas = atlas or coverage.load_atlas()
        executed = s.executed_functions(atlas)
        summ = s.coverage_summary(atlas)
        write_json(out / "coverage.json", {"executed_functions": [hex(a) for a in executed], "summary": summ})
        write_json(out / "summary.json", {
            "name": spec.name, "warp": spec.warp, "cd": spec.cd, "mapdata_index": m.r16(game.MISSION_INDEX),
            "map_title": m.cstr(game.MAP_NAME).decode("latin-1"), "frames": res.frames, "status": res.status,
            "fault": res.fault, "wall_seconds": res.wall_seconds, "virtual_ms": m.clock_us // 1000,
            "initial_seed": m.r32(game.RNG_INITIAL_SEED), "checkpoints": res.checkpoints, "sounds": res.sounds,
            "pngs": res.pngs, "exceptions": m.services.get("exceptions", []),
            "message_boxes": m.services.get("message_boxes", []), "dialogs": m.services.get("dialogs", []),
            "dsound_events": len(m.services.get("dsound_log", [])),
            "unimplemented_apis": sorted(m.unimplemented), "executed_functions": len(executed),
            "speed_index": m.r32(game.SPEED_INDEX), "storm_files_requested": len(s.files)})
        (out / "files.txt").write_text("\n".join(s.files))
        res.spec.mapdata_index = m.r16(game.MISSION_INDEX)
    return res
