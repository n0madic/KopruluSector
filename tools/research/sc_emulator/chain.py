"""Play a campaign from a start mission through the game's own end-of-mission path.

For every mission: open its ready room, press Start (hotkey S), run ``frames`` frames with
no input, then type a cheat in the in-game message line (Enter, text, Enter):
"there is no cow level" makes the mission's victory conditions succeed. The victory
dialog is answered with its hotkey (V); a defeat dialog closes itself after 20 s (P41).
The score screen is left with Ok (hotkey O). AdvanceCampaignAfterGame (P41 0x4bb790)
then chooses the next row: the next ready room, a cinematic (skipped with Escape, the
player's own skip key), or the end of the campaign (campaign menu, or epilog mode 9).

Observed per mission: frame of the committed result (trigger result byte of the local
player), the RequestGameExit code, victory/defeat dialog, glue/mode states visited
(sampled every 100 ms of virtual time), the next mission chosen (the following
CreateCampaignMissionGame call) and the profile progress bytes written by
SaveCharacterFile (P03 0x40d830: record +0x28..+0x3f, six u32 = highest mission per
race, original then expansion).

With `oracle` set, every game of the chain also writes the logs Koprulu replays and the state dumps of its first
`state_steps` object steps (scenario.install_logs, state.install) into oracle/<k>-m<mission>/: a new directory when
a mission is created, and when a lost one restarts (mode 5, which creates no mission). With `glue_ms` too, each glue
screen (the ready rooms, the score screens) is captured that many virtual ms after it opens (uiscreen.py) into
oracle/screens/. With `ready_ms` above 2000, Start waits for the briefing's end, at most that long.
"""
from __future__ import annotations

import json
import logging
import struct
import time
from pathlib import Path

from unicorn import x86_const as X

from . import game, process, render
from .inputs import VK_ESCAPE, VK_RETURN, Input
from .session import Session, write_json

log = logging.getLogger(__name__)
OUT_ROOT = (Path.home() / "koprulu-private/emu/chains")
LOCAL_SLOT = 0x508D50
END_DIALOG_OPEN = 0x6CA73C
HOOK_CREATE_MISSION = 0x4BB6C0     # CX = mission
HOOK_ADVANCE = 0x4BB790            # ECX = won
HOOK_SAVE_PROFILE = 0x40D830       # ECX = record
HOOK_GAME_EXIT = 0x444E00          # CX = exit code
HOOK_VICTORY_DIALOG = 0x4BCFF0
HOOK_DEFEAT_DIALOG = 0x4BCF80
HOOK_SCORE_SCREEN = 0x461940
HOOK_NEXT_SCENARIO = 0x4BBA20      # ResolveNextScenarioRow, ECX = file name


def run_chain(name: str, warp: str, cd: str, frames: int = 300, cheat: str = "there is no cow level",
              max_missions: int = 20, max_wall: float = 7200, inject=None, stop_after_first=False,
              out_root: Path = OUT_ROOT, oracle: Path | None = None, state_steps: int = 1000,
              glue_ms=0, ready_ms: int = 2000) -> dict:
    out = out_root / name
    out.mkdir(parents=True, exist_ok=True)
    t0 = time.time()
    cmd = '"C:\\StarCraft\\StarCraft.exe"' + ("" if inject else f" ophelia {warp}")
    s = Session(process.Config(command_line=cmd, cd=cd))
    m = s.m
    inp = Input(s)
    trace = open(out / "trace.jsonl", "w")
    events: list[dict] = []
    missions: list[dict] = []
    st = {"cur": None, "last_frame": -1, "frames": 0, "need_cheat": False, "cheat_done": False,
          "result_frame": None}
    games = {"count": 0}
    if oracle is not None:
        from . import scenario, state
        logs = scenario.install_logs(s, oracle / "pre-game")
        dumps = state.install(s, tuple(range(1, state_steps + 1)), oracle / "pre-game" / "states")
        if glue_ms:
            from . import uiscreen
            uiscreen.install(s, oracle / "screens", (), glue_ms, names=logs["uilog"]["dialog_ref"])

    def new_game(mission: int):
        if oracle is None:
            return
        games["count"] += 1
        d = oracle / f"{games['count']:02d}-m{mission:02d}"
        logs["rotate"](d)
        dumps["rotate"](d / "states")

    def ev(kind, **kw):
        e = {"t": m.clock_us // 1000, "kind": kind, "frame": m.r32(game.FRAME_COUNTER), **kw}
        events.append(e)
        log.info("%s %s", name, e)
        return e

    def on_frame_end():
        f = m.r32(game.FRAME_COUNTER)
        if f == st["last_frame"]:
            return
        st["last_frame"] = f
        d = game.digest(m)
        d["mission"] = st["cur"]["mission"] if st["cur"] else None
        trace.write(json.dumps(d, separators=(",", ":")) + "\n")
        st["frames"] += 1
        local = m.r32(LOCAL_SLOT)
        if st["cur"] is not None and st["result_frame"] is None and local < 8 and d["res"][local]:
            st["result_frame"] = f
            st["cur"]["result_frame"] = f
            st["cur"]["result"] = d["res"][local]
        if st["frames"] >= frames and not st["cheat_done"]:
            st["need_cheat"] = True

    def on_create():
        mission = m.reg(X.UC_X86_REG_ECX) & 0xFFFF
        rec = {"mission": mission, "created_t": m.clock_us // 1000}
        if missions and "next_mission" not in missions[-1]:
            missions[-1]["next_mission"] = mission
        missions.append(rec)
        st.update(cur=rec, frames=0, cheat_done=False, need_cheat=False, result_frame=None)
        ev("create_mission", mission=mission)
        new_game(mission)

    def on_save_profile():
        rec = m.reg(X.UC_X86_REG_ECX)
        prog = list(struct.unpack("<6I", m.read(rec + 0x28, 24)))
        e = ev("save_profile", name=m.cstr(rec + 0x0C).decode("latin-1"), progress=prog,
               record_hex=m.read(rec, 0x70).hex())
        if st["cur"] is not None:
            st["cur"].setdefault("profile_saves", []).append(prog)

    def simple(kind, reg=None):
        def fn():
            kw = {} if reg is None else {"arg": m.reg(reg) & 0xFFFF}
            e = ev(kind, **kw)
            if st["cur"] is not None:
                st["cur"].setdefault("events", []).append({k: v for k, v in e.items() if k != "t"})
        return fn

    def on_next_scenario():
        ev("next_scenario", file=m.cstr(m.reg(X.UC_X86_REG_ECX)).decode("latin-1"))

    s.hook(game.FRAME_END_PC, on_frame_end)
    s.hook(HOOK_CREATE_MISSION, on_create)
    s.hook(HOOK_SAVE_PROFILE, on_save_profile)
    s.hook(HOOK_GAME_EXIT, simple("game_exit", X.UC_X86_REG_ECX))
    advance = simple("advance_campaign", X.UC_X86_REG_ECX)

    def on_advance():
        advance()
        if m.reg(X.UC_X86_REG_ECX) == 0 and st["cur"] is not None:  # the lost mission restarts (mode 5)
            st.update(frames=0, cheat_done=False, need_cheat=False, result_frame=None)
            new_game(st["cur"]["mission"])

    s.hook(HOOK_ADVANCE, on_advance)
    s.hook(HOOK_VICTORY_DIALOG, simple("victory_dialog"))
    s.hook(HOOK_DEFEAT_DIALOG, simple("defeat_dialog"))
    s.hook(HOOK_SCORE_SCREEN, simple("score_screen"))
    s.hook(HOOK_NEXT_SCENARIO, on_next_scenario)
    if inject:
        done = []

        def do_inject():
            if not done:
                done.append(1)
                mission, race, exp = inject
                m.inject_call(game.SET_PENDING_MISSION, (exp,), {"ecx": mission, "edx": race})
        s.hook(game.GLUE_LOOP, do_inject)
    s.start()

    states: list[list] = []
    last = None
    ready_seen_at = None
    score_seen_at = None
    pressed_room = None
    end_dialog_at = None
    finished = None
    try:
        while time.time() - t0 < max_wall:
            inp.wait(100)
            mode, glue = m.r16(game.GAME_MODE), m.r32(game.GLUE_STATE)
            cur = (mode, glue if mode == 4 else None)
            if cur != last:
                states.append([m.clock_us // 1000, mode, glue if mode == 4 else None,
                               st["cur"]["mission"] if st["cur"] else None])
                if st["cur"] is not None:
                    st["cur"].setdefault("states", []).append(list(cur))
                last = cur
                ready_seen_at = score_seen_at = end_dialog_at = None
            if mode == 4 and glue in (7, 8, 9) and m.r32(game.READY_ROOM_DIALOG):
                dlg = m.r32(game.READY_ROOM_DIALOG)
                if pressed_room != (dlg, len(missions)):
                    ready_seen_at = ready_seen_at or m.clock_us
                    waited = m.clock_us - ready_seen_at
                    # Start once the briefing list ran out (the dialog's current record +0x26 is 0) or after its time
                    if waited > ready_ms * 1000 or (waited > 2_000_000 and (ready_ms <= 2000 or m.r32(dlg + 0x26) == 0)):
                        inp.key(ord("S"))
                        pressed_room = (dlg, len(missions))
            elif mode == 3:
                if st["need_cheat"] and not st["cheat_done"]:
                    st["cheat_done"] = True
                    ev("cheat", text=cheat)
                    render.save_primary_surface(m, out / f"m{st['cur']['mission']:02d}_before_cheat.png")
                    inp.key(VK_RETURN)
                    inp.wait(200)
                    inp.text(cheat)
                    inp.wait(200)
                    inp.key(VK_RETURN)
                if m.r32(END_DIALOG_OPEN):
                    end_dialog_at = end_dialog_at or m.clock_us
                    if m.clock_us - end_dialog_at > 1_500_000 and m.clock_us - end_dialog_at < 1_700_000:
                        render.save_primary_surface(m, out / f"m{st['cur']['mission']:02d}_end_dialog.png")
                        inp.key(ord("V"))  # victory button; the defeat dialog closes itself after 20 s
            elif mode == 4 and 14 <= glue <= 19:
                score_seen_at = score_seen_at or m.clock_us
                if m.clock_us - score_seen_at > 3_000_000:
                    render.save_primary_surface(m, out / f"m{st['cur']['mission']:02d}_score.png")
                    inp.key(ord("O"))
                    score_seen_at = m.clock_us + 10_000_000  # press once, retry after 10 s
            elif mode == 10:
                inp.key(VK_ESCAPE)
                inp.wait(900)
            elif (mode == 4 and glue in (6, 22, 0)) or mode == 9:
                if missions and missions[-1].get("result_frame") is not None:
                    finished = f"campaign end (mode {mode}, glue {glue})"
                    break
            if stop_after_first and mode == 3 and missions and any(
                    e["kind"] == "advance_campaign" and e.get("arg") == 0 for e in missions[-1].get("events", [])):
                ev("mission_restarted_after_defeat")
                finished = "mission restarted after defeat"
                break
            if stop_after_first and len(missions) >= 2:
                finished = "stopped after first mission"
                break
            if len(missions) > max_missions:
                finished = "mission limit"
                break
    except Exception as e:
        log.exception("chain %s failed", name)
        finished = f"fault: {str(e)[:1500]}"
    trace.close()
    result = {"name": name, "warp": warp, "cd": cd, "cheat": cheat, "frames_before_cheat": frames,
              "finished": finished or "wall-time limit", "wall_seconds": round(time.time() - t0, 1),
              "virtual_ms": m.clock_us // 1000, "missions": missions, "states": states, "events": events,
              "input_log": inp.log, "exceptions": m.services.get("exceptions", []),
              "unimplemented_apis": sorted(m.unimplemented), "files_written": sorted(
                  m.services["fs"].overlay_names.values())}
    from . import coverage
    atlas = coverage.load_atlas()
    write_json(out / "coverage.json", {"executed_functions": [hex(a) for a in s.executed_functions(atlas)],
                                       "summary": s.coverage_summary(atlas)})
    write_json(out / "chain.json", result)
    s.files_written = result["files_written"]
    return result
