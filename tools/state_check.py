#!/usr/bin/env python3
"""Phase 3 acceptance: Koprulu's simulation state against the original's state dumps.

For each mission with a dump (tools/research/sc_emulator/state.py; step 1 = the state the
game start leaves before the first object step), Koprulu starts the same mission with the
dump's initial seed (tools/lua/start_state.lua) and both states are compared in the model
of tools/native_state.py / the kp_core dump (tools/lua/oracle.lua), category by category, over the fields
Koprulu prints. The first differences of each category are reported.

--write-golden stores, per mission and category, the CRC-32 of the original's canonical
lines (restricted to Koprulu's fields) in tests/golden/oracle/initial_state/<mission>.lua,
and the missions whose whole state is equal in index.lua; tests/lua/test_initial_state.lua
checks Koprulu against those without the private dumps.

--step selects the object steps compared (1 = the start; "3", "1-61" or "all" dumps):
Koprulu runs the same steps (tools/lua/oracle.lua advance) and each step is compared;
a mission reports its first differing step. From step 3 on, the original has drawn a frame,
which refreshes the image bounds (+0x1c/+0x1e) of the rows in view (0x445de0); those
camera-dependent fields are left out there (Koprulu's presentation refreshes them itself).

--kind names what the dumps are, and so the goldens --write-golden writes: `initial` the catalog missions' starts
(tests/golden/oracle/initial_state), `scenarios` the scenarios of tests/scenarios (tests/golden/oracle/scenarios)
or `runs` campaign missions run idle with a dump at every step (tests/golden/oracle/runs, in the scenarios' shape);
--root defaults to the kind's emulator output.

Usage: python3 tools/state_check.py [--kind initial|scenarios|runs] [--root STATES] [--only terran-m02,...]
       [--step N|A-B|A-B/K|all] [--write-golden]
"""
from __future__ import annotations

import argparse
import json
import logging
import re
import subprocess
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "tools" / "research"))

import native_state  # noqa: E402
from sc_emulator.state import State  # noqa: E402

log = logging.getLogger("state_check")

DEFAULT_ROOT = Path("~/koprulu-private/emu/states")
SCENARIO_ROOT = Path("~/koprulu-private/emu/scenarios")
GOLDEN = ROOT / "tests" / "golden" / "oracle" / "initial_state"
SCENARIO_GOLDEN = ROOT / "tests" / "golden" / "oracle" / "scenarios"
# Campaign missions run idle in the emulator with a dump at every step (`mission NAME WARP --state 1-N`, phase 5):
# their goldens have the scenarios' shape, with the CRCs of the compared steps (`--step 1-2001/20`).
RUN_ROOT = Path("~/koprulu-private/emu/campaign")
RUN_GOLDEN = ROOT / "tests" / "golden" / "oracle" / "runs"
# The kinds of dumps: their default roots, their goldens and the comment of the goldens' index.
KINDS = {
    "initial": (DEFAULT_ROOT, GOLDEN, "Missions whose initial state equals"),
    "scenarios": (SCENARIO_ROOT, SCENARIO_GOLDEN, "Scenarios whose every compared step equals"),
    "runs": (RUN_ROOT, RUN_GOLDEN, "Campaign runs whose every compared step equals"),
}
MISSION_INDEX = ROOT / "generated" / "missions" / "index.txt"
LUA = "luajit"
SHOW = 6


def missions() -> dict[int, str]:
    """mapdata index -> generated mission path (generated/missions/index.txt, line n = index n)."""
    out = {}
    for n, line in enumerate(MISSION_INDEX.read_text().splitlines(), start=1):
        out[n] = line.split()[0]
    return out


def parse_lines(text: str) -> dict[str, dict[str, dict[str, str]]]:
    """category -> key -> fields."""
    model: dict[str, dict[str, dict[str, str]]] = {}
    for line in text.splitlines():
        parts = line.split(" ")
        fields = dict(p.split("=", 1) for p in parts[2:])
        model.setdefault(parts[0], {})[parts[1]] = fields
    return model


def native_model(state: State) -> dict[str, dict[str, dict[str, str]]]:
    m = native_state.Decoder(state).model()
    return {c: {key: {k: str(v) for k, v in f.items()} for key, f in rows} for c, rows in m.items()}


# Fields left out after the first drawn frame: category -> fields.
DRAWN_STEP = 3
# The unit's minimap blink counter (+0x8a, set by NotifyUnitAttacked 0x446f00) counts down as DrawLocalMinimapUnits
# 0x46e490 draws the unit: interface state, as the image bounds are. The same notice of a unit off the screen (the
# camera, 0x422c20) recolors the blink for its talking portrait (+0x8b, RequestUnitTalking 0x4a7200) and silences the
# notices around it (+0x87, 0x4460d0), which the idle loop counts down every 100 ms or more (0x446ff0): nothing of the
# simulation reads them.
DRAWN_FIELDS = {"image": ("x", "y"), "unit": ("last_event_timer", "last_event_color", "attack_notify_timer")}


def kpm_path(mission: str) -> Path:
    """A catalog mission's generated .kpm (generated/missions/<name>.kpm) or a scenario's (scenarios/<name>)."""
    if mission.startswith("scenarios/"):
        return ROOT / "generated" / (mission + ".kpm")
    return ROOT / "generated" / "missions" / (mission + ".kpm")


def koprulu_model(mission: str, seed: int, expansion: int, step: int = 1,
                  commands: Path | None = None) -> tuple[str, dict]:
    """`mission` is a catalog name (generated/missions/<name>.kpm) or scenarios/<name>;
    `commands` the emulator's command log of the run, replayed by Koprulu."""
    argv = [LUA, "tools/lua/start_state.lua", str(kpm_path(mission)), str(seed), str(expansion), str(step)]
    if commands is not None and commands.exists():
        argv.append(str(commands))
        ui = commands.with_name("ui.jsonl")
        if ui.exists():
            argv.append(str(ui))
    p = subprocess.run(argv, cwd=ROOT, capture_output=True, text=True)
    if p.returncode != 0:
        return p.stderr.strip().splitlines()[0] if p.stderr.strip() else f"exit {p.returncode}", {}
    return "", parse_lines(p.stdout)


TRACE_FIELDS = ("frame", "player", "trigger", "action", "type", "result")
# The interface events without values: a frame of a paused game, the end of the local trigger wait, the Escape skip.
FLAG_EVENTS = ("paused", "release", "skip")


def koprulu_trace(mission: str, seed: int, expansion: int, step: int, commands: Path) -> tuple[str, list[str]]:
    """Koprulu's trigger trace up to the entry of object step `step` (tools/lua/trigger_trace.lua)."""
    argv = [LUA, "tools/lua/trigger_trace.lua", str(kpm_path(mission)), str(seed), str(expansion), str(step),
            str(commands)]
    ui = commands.with_name("ui.jsonl")
    if ui.exists():
        argv.append(str(ui))
    p = subprocess.run(argv, cwd=ROOT, capture_output=True, text=True)
    if p.returncode != 0:
        return p.stderr.strip() or f"exit {p.returncode}", []
    return "", p.stdout.splitlines()


def original_trace(path: Path, frame: int) -> list[str]:
    """The emulator's trigger trace (triggers.jsonl) before the trigger step of frame `frame`."""
    rows = [json.loads(r) for r in path.read_text().splitlines()]
    return [" ".join(str(r[k]) for k in TRACE_FIELDS) for r in rows if r["frame"] < frame]


def drop_drawn(model: dict) -> None:
    for category, names in DRAWN_FIELDS.items():
        for fields in model.get(category, {}).values():
            for name in names:
                fields.pop(name, None)


def compare(native: dict, koprulu: dict) -> dict[str, list[str]]:
    diffs: dict[str, list[str]] = {}
    for category, rows in koprulu.items():
        nat = native.get(category, {})
        out = []
        for key in list(dict.fromkeys(list(nat) + list(rows))):
            if key not in rows:
                out.append(f"{category} {key}: only in the original")
                continue
            if key not in nat:
                out.append(f"{category} {key}: only in Koprulu")
                continue
            for name, value in rows[key].items():
                if name not in nat[key]:
                    out.append(f"{category} {key}: field {name} unknown to the decoder")
                elif nat[key][name] != value:
                    a, b = nat[key][name], value
                    if len(a) > 60:
                        i = next((k for k in range(min(len(a), len(b))) if a[k] != b[k]), min(len(a), len(b)))
                        a, b = f"...{a[max(0, i - 20):i + 40]}", f"...{b[max(0, i - 20):i + 40]}"
                    out.append(f"{category} {key}.{name}: original {a} koprulu {b}")
        if out:
            diffs[category] = out
    return diffs


def golden_lines(native: dict, koprulu: dict) -> dict[str, int]:
    """CRC-32 of the original's canonical lines per category, over Koprulu's field names."""
    out = {}
    for category, rows in koprulu.items():
        text = "\n".join(" ".join([category, key] + [f"{n}={f[n]}" for n in sorted(rows.get(key, f))])
                         for key, f in native.get(category, {}).items())
        out[category] = zlib.crc32(text.encode())
    return out


def briefing_objectives(ui: Path) -> list[int]:
    """The objectives the briefing set before the game (the leading "objectives" events of ui.jsonl)."""
    out = []
    if ui.exists():
        for row in ui.read_text().splitlines():
            e = json.loads(row)
            if "objectives" not in e:
                break
            out.append(e["objectives"])
    return out


def write_golden(name: str, mission: str, seed: int, expansion: int, crcs: dict[str, int],
                 objectives: list[int]) -> None:
    GOLDEN.mkdir(parents=True, exist_ok=True)
    lines = [f"-- Initial state of {mission} in the original (tools/state_check.py --write-golden).",
             "return {", f'\tmission = "{mission}", seed = {seed}, expansion = {expansion},',
             f"\tobjectives = {{ {', '.join(map(str, objectives))} }}, -- the briefing's, before the game", "\tcrc = {"]
    lines += [f"\t\t{c} = {v}," for c, v in sorted(crcs.items())]
    lines += ["\t},", "}", ""]
    (GOLDEN / f"{name}.lua").write_text("\n".join(lines))


def write_scenario_golden(name: str, mission: str, seed: int, expansion: int, commands: Path,
                          steps: dict[int, dict[str, int]], trace: list[str] | None = None,
                          end_frame: int | None = None, golden: Path = SCENARIO_GOLDEN) -> None:
    """Per-step CRCs of a scenario (or a campaign run) in the original, with its command and interface
    logs (the interface events before `end_frame`, the frame of the last compared step: the replay never reaches the
    later ones), for tests/lua/test_scenarios.lua."""
    golden.mkdir(parents=True, exist_ok=True)
    lines = [f"-- Run {name} in the original, per object step (tools/state_check.py --write-golden).",
             "return {", f'\tmission = "{mission}", seed = {seed}, expansion = {expansion},', "\tcommands = {"]
    if commands.exists():
        for row in commands.read_text().splitlines():
            c = json.loads(row)
            lines.append(f'\t\t{{ turn = {c["turn"]}, player = {c["player"]}, bytes = "{c["bytes"]}" }},')
    lines += ["\t},", "\tui = {"]
    ui = commands.with_name("ui.jsonl")
    if ui.exists():
        for row in ui.read_text().splitlines():
            e = json.loads(row)
            if end_frame is not None and e["frame"] >= end_frame:
                continue
            if "select" in e:
                lines.append(f'\t\t{{ frame = {e["frame"]}, select = {{ {", ".join(map(str, e["select"]))} }} }},')
            elif "mark" in e:
                lines.append(f'\t\t{{ frame = {e["frame"]}, mark = {{ {", ".join(map(str, e["mark"]))} }} }},')
            elif "alliance" in e:
                lines.append(f'\t\t{{ frame = {e["frame"]}, alliance = {{ {", ".join(map(str, e["alliance"]))} }} }},')
            elif "objectives" in e:
                lines.append(f'\t\t{{ frame = {e["frame"]}, objectives = {e["objectives"]} }},')
            elif any(k in e for k in FLAG_EVENTS):
                flag = next(k for k in FLAG_EVENTS if k in e)
                lines.append(f'\t\t{{ frame = {e["frame"]}, {flag} = true }},')
            elif "onscreen" in e:
                lines.append(f'\t\t{{ frame = {e["frame"]}, onscreen = {{ {e["onscreen"][0]}, {e["onscreen"][1]} }} }},')
            else:
                lines.append(f'\t\t{{ frame = {e["frame"]}, cursor = {{ {e["cursor"][0]}, {e["cursor"][1]} }} }},')
    lines.append("\t},")
    if trace is not None:  # the trigger trace up to the last step: "frame player trigger action type result"
        lines += ["\ttrace = {"] + [f'\t\t"{t}",' for t in trace] + ["\t},"]
    lines += ["\tsteps = {"]
    for step, crcs in sorted(steps.items()):
        lines.append(f"\t\t[{step}] = {{ " + ", ".join(f"{c} = {v}" for c, v in sorted(crcs.items())) + " },")
    lines += ["\t},", "}", ""]
    (golden / f"{name}.lua").write_text("\n".join(lines))


def steps_of(spec: str, root: Path) -> list[int]:
    """N, A-B, A-B/K (every K-th step from A, and B) or "all"."""
    if spec == "all":
        return sorted(int(f.stem[1:]) for f in root.glob("s*.state"))
    if "-" in spec:
        span, _, stride = spec.partition("/")
        a, b = (int(x) for x in span.split("-"))
        k = int(stride or 1)
        if a > b or k < 1:
            raise ValueError(f"step range {spec!r}: A-B needs A <= B and a stride K >= 1")
        out = list(range(a, b + 1, k))
        return out if out[-1] == b else out + [b]
    return [int(spec)]


def check_mission(d: Path, index: dict, steps: list[int], write: bool, kind: str) -> tuple[bool, str | None]:
    """Compares the mission's dumps at `steps`; returns (equal, golden name or None). A scenario or a campaign run
    writes a golden of every compared step, a mission's start one of step 1."""
    run = kind == "runs"
    scenario = kind != "initial"
    scenario_crcs: dict[int, dict[str, int]] = {}
    for step in steps:
        dump = d / "states" / f"s{step:05d}.state"
        if not dump.exists():
            log.info("%-14s step %d: no dump", d.name, step)
            return False, d.name
        state = State(dump)
        mission = f"scenarios/{d.name}" if kind == "scenarios" else index[state.header["mission"]]
        expansion = state.read(0x536720, 1)[0]
        error, kop = koprulu_model(mission, state.header["initial_seed"], expansion, step, d / "commands.jsonl")
        if error:
            log.info("%-14s %-24s step %d FAILED: %s", d.name, mission, step, error)
            return False, d.name
        nat = native_model(state)
        if step >= DRAWN_STEP:
            drop_drawn(nat)
            drop_drawn(kop)
        diffs = compare(nat, kop)
        if write and scenario:
            scenario_crcs[step] = golden_lines(nat, kop)
        elif write and step == 1:
            write_golden(d.name, mission, state.header["initial_seed"], expansion, golden_lines(nat, kop),
                         briefing_objectives(d / "ui.jsonl"))
        if diffs:
            log.info("%-14s %-24s step %d DIFF in %s", d.name, mission, step,
                     ", ".join(f"{c}({len(v)})" for c, v in diffs.items()))
            for c, v in diffs.items():
                for line in v[:SHOW]:
                    log.info("    %s", line)
            return False, d.name
    trace = None
    if (d / "triggers.jsonl").exists():
        error, trace = koprulu_trace(mission, state.header["initial_seed"], expansion, steps[-1], d / "commands.jsonl")
        want = original_trace(d / "triggers.jsonl", state.header["frame"])
        if error or trace != want:
            at = next((k for k in range(min(len(trace), len(want))) if trace[k] != want[k]), min(len(trace), len(want)))
            log.info("%-14s trigger trace %s", d.name, f"FAILED: {error}" if error else
                     f"differs at action {at}: original {want[at:at + 3]} koprulu {trace[at:at + 3]}")
            return False, d.name
    if write and scenario:
        write_scenario_golden(d.name, f"missions/{mission}" if run else mission, state.header["initial_seed"], expansion,
                              d / "commands.jsonl", scenario_crcs, trace, state.header["frame"], KINDS[kind][1])
    log.info("%-14s equal at steps %s", d.name, f"{steps[0]}..{steps[-1]}" if len(steps) > 1 else steps[0])
    return True, d.name


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--kind", choices=KINDS, default="initial", help="what the dumps are (and which goldens they write)")
    ap.add_argument("--root", type=Path, help="the emulator's dumps (default: the kind's)")
    ap.add_argument("--only", default="")
    ap.add_argument("--step", default="1", help='object steps: N, A-B, A-B/K or "all" (1 = game start)')
    ap.add_argument("--write-golden", action="store_true")
    a = ap.parse_args()
    logging.basicConfig(level=logging.INFO, format="%(message)s")
    index = missions()
    only = [x for x in a.only.split(",") if x]
    default_root, golden, what = KINDS[a.kind]
    equal = failed = 0
    accepted, rejected = [], []
    for d in sorted((a.root or default_root).iterdir()):
        if not (d / "states").is_dir() or (only and d.name not in only):
            continue
        try:
            steps = steps_of(a.step, d / "states")
        except ValueError as e:
            ap.error(str(e))
        if not steps:
            log.info("%-14s no dumps", d.name)
            failed += 1
            rejected.append(d.name)
            continue
        ok, name = check_mission(d, index, steps, a.write_golden, a.kind)
        if ok:
            equal += 1
            accepted.append(name)
        else:
            failed += 1
            rejected.append(name)
    if a.write_golden:
        path = golden / "index.lua"
        if only and path.exists():
            # A partial run updates its entries of the index and keeps the others.
            kept = set(re.findall(r'^\t"([^"]+)",$', path.read_text(), re.M)) - set(rejected)
            accepted = sorted(kept | set(accepted))
        lines = [f"-- {what} the original's (tools/state_check.py).", "return {"]
        lines += [f'\t"{name}",' for name in accepted]
        path.write_text("\n".join(lines + ["}", ""]))
    log.info("%d equal, %d differ or fail", equal, failed)
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
