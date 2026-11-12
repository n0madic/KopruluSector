"""Command-line entry: ``python -m sc_emulator <command> ...`` (run from tools/research)."""
from __future__ import annotations

import argparse
import json
import os
import logging
import sys
import time
from pathlib import Path

from . import process


def cmd_boot(a):
    cfg = process.Config(log_api=a.log_api)
    m = process.build(cfg)
    if a.trace_storm:
        from .debug import trace_storm
        trace_storm(m, a.trace_storm)
    process.start_program(m)
    t0 = time.time()

    def stop():
        if a.sample:
            logging.info("sample t=%.0fs eip=%#x clock=%dms stack=%s", time.time() - t0, m.eip, m.clock_us // 1000,
                         m.backtrace(6))
        return time.time() - t0 > a.seconds
    try:
        m.run(stop=stop)
    except Exception as e:  # report and keep the summary
        logging.exception("run ended: %s", e)
    print("threads:", [(hex(t.tid), t.state) for t in m.threads])
    print("clock ms:", m.clock_us // 1000, "eip", hex(m.eip))
    for k, v in sorted(m.api_counts.items(), key=lambda x: -x[1])[:60]:
        print(f"{v:8d} {k}")
    print("unimplemented:", m.unimplemented)
    print("missing procs:", m.services.get("missing_procs"))


def cmd_boot2(a):
    from .session import Session, MAIN_MENU_FN, GAME_MODE, GLUE_STATE, OUT_DIR, write_json
    from . import coverage
    s = Session(process.Config(log_api=a.log_api))
    m = s.m
    state = {"menu_entered": None, "polls_at_menu": 0}

    def on_menu():
        if state["menu_entered"] is None:
            state["menu_entered"] = m.clock_us
            state["polls_at_menu"] = m.services["wm"].idle_polls
            logging.info("main menu entered at %d ms", m.clock_us // 1000)
    s.hook(MAIN_MENU_FN, on_menu)
    s.start()
    wm = m.services["wm"]
    pred = lambda: state["menu_entered"] is not None and wm.idle_polls - state["polls_at_menu"] > a.idle_polls
    t0 = time.time()
    reason = s.run_until(pred, a.seconds)
    atlas = coverage.load_atlas()
    summ = s.coverage_summary(atlas)
    fs = m.services["fs"]
    out = {"stop_reason": reason, "wall_seconds": round(time.time() - t0, 1), "virtual_ms": m.clock_us // 1000,
           "game_mode": m.r32(GAME_MODE), "glue_state": m.r32(GLUE_STATE),
           "main_menu_entered_ms": None if state["menu_entered"] is None else state["menu_entered"] // 1000,
           "files_opened": sorted(set(fs.opened)), "api_calls": dict(sorted(m.api_counts.items())),
           "storm_files_requested": s.files,
           "threads": [{"tid": hex(t.tid), "start": hex(t.start), "state": t.state} for t in m.threads],
           "coverage": summ,
           "executed_functions": [hex(x) for x in s.executed_functions(atlas)]}
    write_json(OUT_DIR / "boot" / "boot.json", out)
    print(json.dumps({k: v for k, v in out.items() if k not in ("files_opened", "api_calls", "executed_functions",
                                                                 "coverage")}, indent=1))
    print("coverage:", {k: (v if not isinstance(v, list) else len(v)) for k, v in summ.items()})


def cmd_mission(a):
    from .mission import MissionSpec, run_mission
    spec = MissionSpec(a.name, a.warp, a.cd)
    if a.inject:
        spec.inject = tuple(int(x) for x in a.inject.split(","))
    setup, png_frames = None, (1, 500, 1000, 2000, 3000)
    if a.capture:
        from . import capture
        frames = tuple(int(x) for x in a.capture.split(","))
        setup, png_frames = (lambda s, out: capture.install(s, frames, out / "captures")), ()
    if a.state:
        from . import state
        from .scenario import install_logs
        frames = parse_steps(a.state)
        previous = setup

        def setup(s, out):
            if previous:
                previous(s, out)
            for old in (out / "states").glob("s*.state"):  # a shorter run must not leave the steps of a longer one
                old.unlink()
            state.install(s, frames, out / "states")
            install_logs(s, out)  # the commands, interface events and trigger trace Koprulu replays
        png_frames = ()
    if a.calltrace:
        from . import calltrace
        start, stop, windows = parse_trace(a.calltrace)
        previous_trace = setup

        def setup(s, out):
            if previous_trace:
                previous_trace(s, out)
            calltrace.install(s, start, stop, out / "calltrace", windows)
        png_frames = ()
    kwargs = {"out_root": Path(a.out_root)} if a.out_root else {}
    r = run_mission(spec, frames=a.frames, max_wall=a.max_wall, sound_device=not a.no_sound, setup=setup,
                    png_frames=png_frames, **kwargs)
    print(r.status, r.frames, r.wall_seconds, r.fault)


def parse_steps(text: str) -> tuple[int, ...]:
    """Comma-separated object step numbers or ranges (A-B, or A-B/K for every K-th step from A)."""
    out = []
    for part in text.split(","):
        part, _, stride = part.partition("/")
        a, _, b = part.partition("-")
        out += range(int(a), int(b or a) + 1, int(stride or 1))
    return tuple(out)


def parse_trace(text: str) -> tuple[int, int, int]:
    """START,STOP[,WINDOWS]: hex code addresses and a decimal window count."""
    parts = text.split(",")
    return int(parts[0], 16), int(parts[1], 16), int(parts[2]) if len(parts) > 2 else 1


def cmd_scenario(a):
    from .scenario import run, OUT_ROOT
    trace = parse_trace(a.calltrace) if a.calltrace else None
    r = run(Path(a.spec), Path(a.out_root) if a.out_root else OUT_ROOT, trace)
    print(r.status, r.frames, r.wall_seconds, r.fault)


def cmd_play(a):
    """A scripted play of scenarios.py (e.g. wasteland) with the state dumps and logs of a campaign run, for
    tools/state_check.py --kind runs."""
    from . import scenarios, state
    from .scenario import install_logs
    out = Path(a.out_root or Path(__file__).resolve().parents[3] / "koprulu-private/emu/campaign") / a.out_name
    steps = parse_steps(a.state) if a.state else ()
    if a.out_root:  # scenarios.py's own records (screenshots, result, trace) go beside the dumps: emu/h03 holds the
        scenarios.OUT = Path(a.out_root) / "records"  # reference runs (h03/terran-wasteland is M1), which a play with
        # another timing (--speech) must not overwrite

    def setup(s):
        for old in (out / "states").glob("s*.state"):  # a shorter run must not leave the steps of a longer one
            old.unlink()
        if a.speech:  # before the dumps: step 1's must have the speech's TIME
            from . import speech
            locale, _, mission = a.speech.partition(":")
            speech.install(s, locale, mission, Path(a.scr))
        state.install(s, steps, out / "states")
        logs = install_logs(s, out)
        if a.screens or a.glue_ms:
            from . import uiscreen
            uiscreen.install(s, out / "screens", tuple(int(x) for x in a.screens.split(",") if x), a.glue_ms,
                             names=logs["uilog"]["dialog_ref"])
        if a.fog:
            from . import fogcap
            fogcap.install(s, tuple(int(x) for x in a.fog.split(",")), out / "fog", forced=not a.fog_partial)

    scenarios.SETUP = setup
    # the play's own records go next to the reference runs under another name, which they must not overwrite
    from . import campaign_plays, campaign_plays_xzerg
    play = (getattr(scenarios, a.name, None) or campaign_plays.PLAYS.get(a.name)
            or campaign_plays_xzerg.PLAYS[a.name])
    print(play(out_name=a.out_name))


def cmd_campaigns(a):
    from .batch import run_all
    run_all(jobs=a.jobs, frames=a.frames, max_wall=a.max_wall, only=[x for x in a.only.split(",") if x],
            capture=a.capture, state=a.state, out_root=Path(a.out_root) if a.out_root else None)


def cmd_apis(a):
    """Print the HLE function table (Markdown) used by the documentation."""
    from .hle import REGISTRY
    seen = {}
    for (dll, key), spec in REGISTRY.items():
        if isinstance(key, int):
            continue
        seen.setdefault(dll, []).append(spec)
    for dll in sorted(seen):
        print(f"\n#### {dll.upper()}\n")
        print("| Function | Args | Semantics |")
        print("|---|---:|---|")
        for spec in sorted(seen[dll], key=lambda x: x.name.lower()):
            doc = " ".join((spec.doc or _generic_doc(spec.name)).split()).replace("|", "\\|")
            print(f"| `{spec.name}` | {'varargs' if spec.nargs < 0 else spec.nargs} | {doc} |")


def _generic_doc(name):
    if name.endswith("::AddRef"):
        return "Increments the object's reference count."
    if name.endswith("::Release"):
        return "Decrements the reference count (objects are never freed)."
    if name.endswith("::QueryInterface"):
        return "E_NOINTERFACE (only the base interfaces exist)."
    return ""


def cmd_aggregate(a):
    from .aggregate import aggregate
    r = aggregate()
    print(json.dumps(r["mission_totals"]), json.dumps(r["coverage"]["executed_by_status"]))


def cmd_chain(a):
    from .chain import run_chain
    inj = tuple(int(x) for x in a.inject.split(",")) if a.inject else None
    out_root = Path(a.out_root) if a.out_root else None
    r = run_chain(a.name, a.warp, a.cd, frames=a.frames, cheat=a.cheat, max_missions=a.max_missions,
                  max_wall=a.max_wall, inject=inj, stop_after_first=a.single,
                  **({"out_root": out_root} if out_root else {}),
                  oracle=Path(a.oracle) / a.name if a.oracle else None, state_steps=a.state_steps,
                  glue_ms=tuple(int(x) for x in a.glue_ms.split(",") if x), ready_ms=a.ready_ms)
    print(r["finished"], [(x["mission"], x.get("result"), x.get("result_frame"), x.get("next_mission"))
                          for x in r["missions"]], r["wall_seconds"])


def cmd_menus(a):
    from .menus import run_menus
    r = run_menus(Path(a.out), a.cd, settle_ms=a.settle_ms, glue_ms=tuple(int(x) for x in a.glue_ms.split(",") if x))
    print(len(r["steps"]), r["steps"][-1:] if r["steps"] else None, r["wall_seconds"])


def main(argv=None):
    ap = argparse.ArgumentParser(prog="sc_emulator")
    ap.add_argument("-v", "--verbose", action="store_true")
    sub = ap.add_subparsers(dest="cmd", required=True)
    b = sub.add_parser("boot")
    b.add_argument("--seconds", type=float, default=60)
    b.add_argument("--log-api", action="store_true")
    b.add_argument("--trace-storm", default="")
    b.add_argument("--sample", action="store_true")
    b2 = sub.add_parser("boot2")
    b2.add_argument("--seconds", type=float, default=1800)
    b2.add_argument("--idle-polls", type=int, default=3000)
    b2.add_argument("--log-api", action="store_true")
    mi = sub.add_parser("mission")
    mi.add_argument("name")
    mi.add_argument("warp")
    mi.add_argument("--cd", default="original")
    mi.add_argument("--frames", type=int, default=3000)
    mi.add_argument("--max-wall", type=float, default=7200)
    mi.add_argument("--no-sound", action="store_true")
    mi.add_argument("--inject", default="", help="mission,race,expansion for the pending-mission call")
    mi.add_argument("--capture", default="", help="comma-separated frames to capture (sc_emulator/capture.py)")
    mi.add_argument("--calltrace", default="", help="START,STOP[,WINDOWS]: hex code addresses of the traced windows")
    mi.add_argument("--state", default="", help="comma-separated object step numbers or ranges A-B[/K] to dump the "
                    "state at (sc_emulator/state.py), with the logs Koprulu replays (scenario.py install_logs)")
    mi.add_argument("--out-root", default="", help="output root instead of the missions directory")
    mn = sub.add_parser("menus", help="the glue's campaign path driven by input (menus.py)")
    mn.add_argument("out")
    mn.add_argument("--cd", default="expansion")
    mn.add_argument("--settle-ms", type=int, default=3500)
    mn.add_argument("--glue-ms", default="3000")
    ch = sub.add_parser("chain")
    ch.add_argument("name")
    ch.add_argument("warp")
    ch.add_argument("--cd", default="original")
    ch.add_argument("--frames", type=int, default=300)
    ch.add_argument("--cheat", default="there is no cow level")
    ch.add_argument("--max-missions", type=int, default=20)
    ch.add_argument("--max-wall", type=float, default=7200)
    ch.add_argument("--inject", default="")
    ch.add_argument("--single", action="store_true", help="stop when the next mission is created")
    ch.add_argument("--out-root", default="", help="output root instead of emu/chains")
    ch.add_argument("--oracle", default="", help="also write each game's replay logs and state dumps to DIR/<name>")
    ch.add_argument("--state-steps", type=int, default=1000, help="with --oracle: dump the first N object steps")
    ch.add_argument("--glue-ms", default="", help="with --oracle: capture each glue screen these many "
                    "virtual ms after it opens (comma-separated)")
    ch.add_argument("--ready-ms", type=int, default=2000, help="above 2000: press Start once the briefing ended, "
                    "at most this many virtual ms after the ready room opened")
    sc = sub.add_parser("scenario")
    sc.add_argument("spec", help="tests/scenarios/<name>.json")
    sc.add_argument("--out-root", default="", help="output root instead of emu/scenarios")
    sc.add_argument("--calltrace", default="", help="START,STOP[,WINDOWS]: hex code addresses of the traced windows")
    pp = sub.add_parser("play")
    pp.add_argument("name", help="a scripted play of sc_emulator/scenarios.py (e.g. wasteland) or of "
                    "campaign_plays.py (e.g. terran_m03)")
    pp.add_argument("out_name", help="the run's directory under the output root, e.g. terran-wasteland")
    pp.add_argument("--state", default="", help="object step numbers or ranges A-B to dump the state at")
    pp.add_argument("--out-root", default="", help="output root instead of emu/campaign")
    pp.add_argument("--fog", default="", help="comma-separated frames of fog captures (sc_emulator/fogcap.py)")
    pp.add_argument("--fog-partial", action="store_true", help="capture the game's own partial redraws")
    pp.add_argument("--speech", default="", help="LOC:CAMPAIGN/MISSION (ruRU:terran/terran01): the triggers take the "
                    "TIME of the language's map in --scr (sc_emulator/speech.py)")
    pp.add_argument("--scr", default=os.environ.get("KOPRULU_SCR", "/Applications/StarCraft"),
                    help="the StarCraft: Remastered install --speech reads (default: KOPRULU_SCR)")
    pp.add_argument("--screens", default="", help="comma-separated frames of screen captures (sc_emulator/uiscreen.py)")
    pp.add_argument("--glue-ms", type=int, default=0, help="capture each glue screen this many virtual ms after it "
                    "opens (uiscreen.py)")
    sub.add_parser("apis")
    sub.add_parser("aggregate")
    ca = sub.add_parser("campaigns")
    ca.add_argument("--jobs", type=int, default=8)
    ca.add_argument("--frames", type=int, default=3000)
    ca.add_argument("--max-wall", type=float, default=5400)
    ca.add_argument("--only", default="", help="comma-separated mission names")
    ca.add_argument("--capture", default="", help="comma-separated frames to capture in every mission")
    ca.add_argument("--state", default="", help="comma-separated object step numbers to dump the state at in every mission")
    ca.add_argument("--out-root", default="", help="output root instead of the missions directory")
    a = ap.parse_args(argv)
    logging.basicConfig(level=logging.DEBUG if a.verbose else logging.INFO, format="%(levelname)s %(name)s: %(message)s")
    {"boot": cmd_boot, "boot2": cmd_boot2, "mission": cmd_mission, "campaigns": cmd_campaigns, "apis": cmd_apis, "aggregate": cmd_aggregate, "chain": cmd_chain, "menus": cmd_menus, "scenario": cmd_scenario, "play": cmd_play}[a.cmd](a)


if __name__ == "__main__":
    sys.exit(main())
