#!/usr/bin/env python3
"""Phase 3 acceptance: Koprulu's simulation state against the original's state dumps.

For each mission with a dump (tools/research/sc_emulator/state.py; step 1 = the state the
game start leaves before the first object step), Koprulu starts the same mission with the
initial seed of the run's first dump (kp_oracle start_state) and both states are
compared in the model of tools/native_state.py / the kp_core dump, category by category, over the fields
Koprulu prints. The first differences of each category are reported.

--write-golden stores, per mission and category, the CRC-32 of the original's canonical
lines (restricted to Koprulu's fields) in tests/golden/oracle/initial_state/<mission>.json,
and the missions whose whole state is equal in index.json; tests/c/test_initial_state.c
checks Koprulu against those without the private dumps.

--step selects the object steps compared (1 = the start; "3", "1-61" or "all" dumps):
Koprulu runs the same steps (kp_oracle's runner, native/kp_core/tools/kp_runner.c) and each step is compared;
a mission reports its first differing step. From step 3 on, the original has drawn a frame,
which refreshes the image bounds (+0x1c/+0x1e) of the rows in view (0x445de0); those
camera-dependent fields are left out there (Koprulu's presentation refreshes them itself).

--kind names what the dumps are, and so the goldens --write-golden writes: `initial` the catalog missions' starts
(tests/golden/oracle/initial_state), `scenarios` the scenarios of tests/scenarios (tests/golden/oracle/scenarios)
or `runs` campaign missions run idle with a dump at every step (tests/golden/oracle/runs, in the scenarios' shape),
`chains` the games of the campaign chains (tests/golden/oracle/chains; --only takes a chain or a game), `plays` the
scripted wins of the campaign rows (tests/golden/oracle/plays); --root defaults to the kind's emulator output.
--speech LOC: plays the emulator ran with a language's speech (emu/plays-LOC, tests/golden/oracle/plays-LOC), Koprulu
takes the TIME of the language's maps in --speech-data (a StarCraft: Remastered install or a repack with the language's
group; default KOPRULU_SCR).

Usage: python3 tools/state_check.py [--kind initial|scenarios|runs|chains|plays] [--root STATES]
       [--only terran-m02,...] [--step N|A-B|A-B/K|all|all/K] [--write-golden] [--speech LOC [--speech-data DIR]]
"""
from __future__ import annotations

import argparse
import json
import logging
import os
import re
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "tools" / "research"))

import mission_index  # noqa: E402
import native_state  # noqa: E402
import oracle_cli  # noqa: E402
from sc_emulator.state import State  # noqa: E402

sys.path.insert(0, str(ROOT / "tools" / "oracle"))
from oracle_common import EMU_ROOT, golden_json  # noqa: E402

log = logging.getLogger("state_check")

DEFAULT_ROOT = EMU_ROOT / "states"
SCENARIO_ROOT = EMU_ROOT / "scenarios"
GOLDEN = ROOT / "tests" / "golden" / "oracle" / "initial_state"
SCENARIO_GOLDEN = ROOT / "tests" / "golden" / "oracle" / "scenarios"
# Campaign missions run idle in the emulator with a dump at every step (`mission NAME WARP --state 1-N`, phase 5):
# their goldens have the scenarios' shape, with the CRCs of the compared steps (`--step 1-2001/20`).
RUN_ROOT = EMU_ROOT / "campaign"
RUN_GOLDEN = ROOT / "tests" / "golden" / "oracle" / "runs"
# The games of the campaign chains the emulator played through the original's progression with a cheat's victory
# (`chain NAME WARP --oracle DIR`, plan phase 8): DIR/<chain>/<k>-m<mission>/ each, a run's shape; their goldens are
# named <chain>.<k>-m<mission>.
CHAIN_ROOT = EMU_ROOT / "chains-oracle"
CHAIN_GOLDEN = ROOT / "tests" / "golden" / "oracle" / "chains"
CHAIN_GAME = re.compile(r"^\d+-m\d+$")
# The scripted wins of the campaign rows (`play NAME OUT --state ...`, sc_emulator/campaign_plays.py, plan phase 8):
# their goldens keep the continuation fingerprint of every compared step (FINGERPRINT) and the categories' CRCs of
# every FULL_EVERY-th step and the last.
PLAY_ROOT = EMU_ROOT / "plays"
PLAY_GOLDEN = ROOT / "tests" / "golden" / "oracle" / "plays"
FINGERPRINT = "fingerprint"
SYNC_COMMAND = "37"
FULL_EVERY = 500
# The kinds of dumps: their default roots and their goldens (index.json: the runs whose every compared step equals
# the original's).
KINDS = {
    "initial": (DEFAULT_ROOT, GOLDEN),
    "scenarios": (SCENARIO_ROOT, SCENARIO_GOLDEN),
    "runs": (RUN_ROOT, RUN_GOLDEN),
    "chains": (CHAIN_ROOT, CHAIN_GOLDEN),
    "plays": (PLAY_ROOT, PLAY_GOLDEN),
}
# A language's speech (`play ... --speech LOC:MISSION`, sc_emulator/speech.py, localization's stage L5): the plays the
# emulator ran with the language's TIME go to EMU_ROOT/plays-<loc>, their goldens to tests/golden/oracle/plays-<loc>,
# and Koprulu takes the TIME of the language's maps (KP_SPEECH, KP_SPEECH_DATA, kp_oracle_chain_opts; ADR 0019).
SPEECH_KINDS = ("plays",)
RUN_KINDS = ("runs", "chains", "plays")  # campaign missions: their goldens name the mission's key, missions/<name>
SHOW = 6


def tool(name: str) -> list[str]:
    """The command line of a kp_oracle subcommand (tools/oracle_cli.py)."""
    return oracle_cli.command(name)


def missions() -> dict[int, str]:
    """mapdata index -> mission name, the catalog of the 1.17 install (tools/mission_index.py)."""
    return {m.index: m.name for m in mission_index.catalog(mission_index.require())}


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


def mission_key(mission: str) -> str:
    """The key kp_oracle starts a mission by (kp_data.h): a catalog name's "missions/<name>", a scenario's
    "scenarios/<name>" (a key stays)."""
    return mission if mission.startswith(("scenarios/", "missions/")) else f"missions/{mission}"


def plays_golden(golden: Path) -> bool:
    """The plays' goldens, the base's or a speech's (plays-<loc>)."""
    return golden.parent == PLAY_GOLDEN.parent and golden.name.split("-")[0] == PLAY_GOLDEN.name


def chain_env(turn: int, carry: Path | None) -> dict[str, str]:
    """The chain context of a chain's game for the oracle tools (kp_oracle_chain_opts): KP_TURN, the turn counter
    the menus left, and KP_CARRY, the file of the game before's carry (KpCarry)."""
    return {"KP_TURN": str(turn), **({"KP_CARRY": str(carry)} if carry else {})}


def koprulu_model(mission: str, seed: int, expansion: int, step: int = 1,
                  commands: Path | None = None, env: dict[str, str] | None = None) -> tuple[str, dict]:
    """`mission` is a catalog name (terran/terran01) or scenarios/<name>;
    `commands` the emulator's command log of the run, replayed by Koprulu; `env` the chain context (chain_env)."""
    argv = tool("start_state") + [mission_key(mission), str(seed), str(expansion), str(step)]
    if commands is not None and commands.exists():
        argv.append(str(commands))
        ui = commands.with_name("ui.jsonl")
        if ui.exists():
            argv.append(str(ui))
    p = subprocess.run(argv, cwd=ROOT, capture_output=True, text=True, env={**os.environ, **(env or {})})
    if p.returncode != 0:
        return p.stderr.strip().splitlines()[0] if p.stderr.strip() else f"exit {p.returncode}", {}
    return "", parse_lines(p.stdout)


TRACE_FIELDS = ("frame", "player", "trigger", "action", "type", "result")
AI_TRACE_FIELDS = ("frame", "player", "slot", "bin", "pc", "op")
AI_TRACE_PREFIX = "ai "
# The interface events without values: a frame of a paused game, the end of the local trigger wait, the Escape skip,
# a CRT rand draw (sc_emulator/scenario.py CRT_RAND).
FLAG_EVENTS = ("paused", "release", "skip", "crt")
MAX_RUN_DRAWS = 9  # a compact golden's draws of one frame in a run: one digit


def koprulu_traces(mission: str, seed: int, expansion: int, step: int, commands: Path,
                   env: dict[str, str] | None = None) -> tuple[str, list[str], list[str]]:
    """Koprulu's trigger and AI traces up to the entry of object step `step` (kp_oracle traces)."""
    argv = tool("traces") + [mission_key(mission), str(seed), str(expansion), str(step), str(commands)]
    ui = commands.with_name("ui.jsonl")
    if ui.exists():
        argv.append(str(ui))
    p = subprocess.run(argv, cwd=ROOT, capture_output=True, text=True, env={**os.environ, **(env or {})})
    if p.returncode != 0:
        return p.stderr.strip() or f"exit {p.returncode}", [], []
    lines = p.stdout.splitlines()
    return ("", [t for t in lines if not t.startswith(AI_TRACE_PREFIX)],
            [t[len(AI_TRACE_PREFIX):] for t in lines if t.startswith(AI_TRACE_PREFIX)])


def original_trace(path: Path, frame: int, fields: tuple[str, ...] = TRACE_FIELDS) -> list[str]:
    """The emulator's trigger trace (triggers.jsonl), or AI trace (ai.jsonl with AI_TRACE_FIELDS), before the trigger
    step of frame `frame` (the AI's opcodes of a frame come in its object step, before that)."""
    rows = [json.loads(r) for r in path.read_text().splitlines()]
    return [" ".join(str(r[k]) for k in fields) for r in rows if r["frame"] < frame]


def first_difference(a: list[str], b: list[str]) -> int:
    return next((k for k in range(min(len(a), len(b))) if a[k] != b[k]), min(len(a), len(b)))


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
    """The initial state's golden: the CRCs of its categories and the briefing's objectives (before the game)."""
    GOLDEN.mkdir(parents=True, exist_ok=True)
    (GOLDEN / f"{name}.json").write_text(golden_json({"mission": mission, "seed": seed, "expansion": expansion,
                                                      "objectives": objectives, "crc": crcs}))


def fingerprint(crcs: dict[str, int]) -> int:
    """The continuation fingerprint of a state (kp_state_fingerprint, native/kp_core/tools/kp_oracle.c): the CRC-32 of
    its category CRCs as lines "<category> <crc>\\n" in category order."""
    return zlib.crc32("".join(f"{c} {crcs[c]}\n" for c in sorted(crcs)).encode())


def ui_entry(e: dict) -> dict:
    """An interface event of a golden: its frame and the field that names it (a flag event as true)."""
    for key in ("select", "mark", "alliance", "placement", "objectives", "srand", "critter", "menu_pause"):
        if key in e:
            return {"frame": e["frame"], key: e[key]}
    flag = next((k for k in FLAG_EVENTS if k in e), None)
    if flag:
        return {"frame": e["frame"], flag: True}
    if "onscreen" in e:
        return {"frame": e["frame"], "onscreen": e["onscreen"][:2]}
    return {"frame": e["frame"], "cursor": e["cursor"][:2]}


def write_scenario_golden(name: str, mission: str, seed: int, expansion: int, commands: Path,
                          steps: dict[int, dict[str, int]], trace: list[str] | None = None,
                          end_frame: int | None = None, golden: Path = SCENARIO_GOLDEN,
                          ai_trace: list[str] | None = None, extra: dict | None = None) -> None:
    """Per-step CRCs of a scenario (or a campaign run) in the original, with its command and interface
    logs (the interface events before `end_frame`, the frame of the last compared step: the replay never reaches the
    later ones), for tests/c/test_scenarios.c."""
    golden.mkdir(parents=True, exist_ok=True)
    # a chain's game: the game before it and the menus' turn counter (extra)
    data: dict = {"mission": mission, "seed": seed, "expansion": expansion, **(extra or {}), "commands": [], "ui": []}
    # A chain's game and a play leave out the sync commands (CheckSyncCommand 0x47a510 changes no game state) and keep
    # a run of the screen's draws at one place, adjacent in the log on consecutive frames, as one entry with the draws
    # of each frame (kp_oracle.c golden_ui expands it): the log's order stays, and a frame's repeated draws stay (a
    # draw's creep borders change the tiles around it, so a second draw is not known to change nothing).
    compact = golden == CHAIN_GOLDEN or plays_golden(golden)
    if commands.exists():
        for row in commands.read_text().splitlines():
            c = json.loads(row)
            if compact and c["bytes"].startswith(SYNC_COMMAND):
                continue
            data["commands"].append({"turn": c["turn"], "player": c["player"], "bytes": c["bytes"]})
    ui = commands.with_name("ui.jsonl")
    if ui.exists():
        events = [json.loads(row) for row in ui.read_text().splitlines()]
        events = [e for e in events if end_frame is None or e["frame"] < end_frame]
        runs: list[list] = []  # [event, the draws of each frame from its frame on]
        for e in events:
            last = runs[-1] if runs else None
            if compact and last and "onscreen" in e and "onscreen" in last[0] and e["onscreen"] == last[0]["onscreen"]:
                k = e["frame"] - last[0]["frame"]
                if k == len(last[1]) - 1 and last[1][k] < MAX_RUN_DRAWS:
                    last[1][k] += 1
                    continue
                if k == len(last[1]):
                    last[1].append(1)
                    continue
            runs.append([e, [1]])
        for e, draws in runs:
            if draws != [1]:
                data["ui"].append({"frame": e["frame"], "onscreen": e["onscreen"][:2], "draws": "".join(map(str, draws))})
            else:
                data["ui"].append(ui_entry(e))
    if trace is not None:  # the trigger trace up to the last step: "frame player trigger action type result"
        data["trace"] = list(trace)
    if ai_trace is not None:  # the AI trace up to the last step ("frame player slot bin pc op" lines), by its CRC
        text = "\n".join(ai_trace)
        data["ai_trace"] = {"count": len(ai_trace), "crc": zlib.crc32(text.encode())}
    data["steps"] = {}
    last = max(steps) if steps else 0
    for step, crcs in sorted(steps.items()):
        if plays_golden(golden) and (step - 1) % FULL_EVERY != 0 and step != last:
            data["steps"][step] = {FINGERPRINT: fingerprint(crcs)}
        else:
            data["steps"][step] = dict(crcs)
    (golden / f"{name}.json").write_text(golden_json(data))


def steps_of(spec: str, root: Path) -> list[int]:
    """N, A-B, A-B/K (every K-th step from A, and B), "all" or "all/K" (every K-th of the dumps from step 1, and the
    last)."""
    if spec.startswith("all"):
        dumps = sorted(int(f.stem[1:]) for f in root.glob("s*.state"))
        k = int(spec.partition("/")[2] or 1)
        if k < 1:
            raise ValueError(f"step range {spec!r}: all/K needs a stride K >= 1")
        out = [s for s in dumps if (s - 1) % k == 0]
        return out if not dumps or (out and out[-1] == dumps[-1]) else out + [dumps[-1]]
    if "-" in spec:
        span, _, stride = spec.partition("/")
        a, b = (int(x) for x in span.split("-"))
        k = int(stride or 1)
        if a > b or k < 1:
            raise ValueError(f"step range {spec!r}: A-B needs A <= B and a stride K >= 1")
        out = list(range(a, b + 1, k))
        return out if out[-1] == b else out + [b]
    return [int(spec)]


def chain_carry(d: Path, index: dict, out: Path, env: dict[str, str]) -> str:
    """Runs a chain's game with the current chain context through its last dumped step (the step whose trigger pass
    opens the result dialog and pauses the game: the dumps end there) and writes its carry (KpCarry) to `out` (for the
    next game); returns an error or "" (kp_oracle play --mission)."""
    dumps = sorted(int(f.stem[1:]) for f in (d / "states").glob("s*.state"))
    first = State(d / "states" / "s00001.state")
    argv = tool("play") + ["--mission", mission_key(index[first.header["mission"]]),
                           str(first.header["initial_seed"]), str(first.read(native_state.EXPANSION, 1)[0]),
                           str(dumps[-1] + 1),
                           str(d / "commands.jsonl"), str(d / "ui.jsonl"), "--carry-out", str(out)]
    p = subprocess.run(argv, cwd=ROOT, capture_output=True, text=True, env={**os.environ, **(env or {})})
    return "" if p.returncode == 0 else (p.stderr.strip() or f"exit {p.returncode}")


def chain_context(d: Path, name: str, before: dict[Path, tuple[Path, str]], index: dict,
                  carry_dir: Path) -> tuple[dict, dict[str, str], str]:
    """The context of a chain's game: its golden's extra fields (the menus' turn counter, the game it carries from) and
    the environment of the oracle tools. The games before it in its chain, each run with its own context, leave its carry.
    The last value is an error or ""."""
    games, game = [], d
    while game in before:
        games.append(before[game])
        game = before[game][0]
    turn = native_state.Decoder(State(d / "states" / "s00001.state")).u32(native_state.TURN_COUNTER)
    extra: dict = {"turn": turn}
    carry_file = None
    for prev, prev_name in reversed(games):
        out = carry_dir / f"{prev.parent.name}.{prev.name}.carry"
        if not out.exists():
            prev_turn = native_state.Decoder(State(prev / "states" / "s00001.state")).u32(native_state.TURN_COUNTER)
            error = chain_carry(prev, index, out, chain_env(prev_turn, carry_file))
            if error:
                return extra, {}, f"the game before ({prev_name}) FAILED: {error}"
        carry_file = out
        extra["carry_from"] = prev_name
    return extra, chain_env(turn, carry_file), ""


def check_mission(d: Path, index: dict, steps: list[int], write: bool, kind: str,
                  name: str | None = None, extra: dict | None = None,
                  env: dict[str, str] | None = None, golden: Path | None = None) -> tuple[bool, str | None]:
    """Compares the mission's dumps at `steps`; returns (equal, golden name). A scenario or a campaign run writes a
    golden of every compared step, a mission's start one of step 1. `name`: the golden's (the directory's)."""
    run = kind in RUN_KINDS
    name = name or d.name
    scenario = kind != "initial"
    scenario_crcs: dict[int, dict[str, int]] = {}
    # The game's seed is the start's: a later dump's header holds the seed variable as the game left it (an AI
    # script's set_randomseed writes it), not the one the game started with.
    first = d / "states" / "s00001.state"
    seed = State(first).header["initial_seed"] if first.exists() else None
    for step in steps:
        dump = d / "states" / f"s{step:05d}.state"
        if not dump.exists():
            log.info("%-14s step %d: no dump", name, step)
            return False, name
        state = State(dump)
        if seed is None:
            seed = state.header["initial_seed"]
        mission = f"scenarios/{name}" if kind == "scenarios" else index[state.header["mission"]]
        expansion = state.read(native_state.EXPANSION, 1)[0]
        error, kop = koprulu_model(mission, seed, expansion, step, d / "commands.jsonl", env)
        if error:
            log.info("%-14s %-24s step %d FAILED: %s", name, mission, step, error)
            return False, name
        nat = native_model(state)
        if step >= DRAWN_STEP:
            drop_drawn(nat)
            drop_drawn(kop)
        diffs = compare(nat, kop)
        if write and scenario:
            scenario_crcs[step] = golden_lines(nat, kop)
        elif write and step == 1:
            write_golden(name, mission, seed, expansion, golden_lines(nat, kop),
                         briefing_objectives(d / "ui.jsonl"))
        if diffs:
            log.info("%-14s %-24s step %d DIFF in %s", name, mission, step,
                     ", ".join(f"{c}({len(v)})" for c, v in diffs.items()))
            for c, v in diffs.items():
                for line in v[:SHOW]:
                    log.info("    %s", line)
            return False, name
    trace = ai_trace = None
    if (d / "triggers.jsonl").exists() or (d / "ai.jsonl").exists():
        error, trace, ai_trace = koprulu_traces(mission, seed, expansion, steps[-1],
                                                d / "commands.jsonl", env)
        if error:
            log.info("%-14s traces FAILED: %s", name, error)
            return False, name
        for what, path, fields, mine in (("trigger trace", d / "triggers.jsonl", TRACE_FIELDS, trace),
                                         ("AI trace", d / "ai.jsonl", AI_TRACE_FIELDS, ai_trace)):
            if not path.exists():
                continue
            want = original_trace(path, state.header["frame"], fields)
            if mine != want:
                at = first_difference(mine, want)
                log.info("%-14s %s differs at %d: original %s koprulu %s", name, what, at, want[at:at + 3],
                         mine[at:at + 3])
                return False, name
        if not (d / "triggers.jsonl").exists():
            trace = None
        if not (d / "ai.jsonl").exists():
            ai_trace = None
    if write and scenario:
        write_scenario_golden(name, f"missions/{mission}" if run else mission, seed, expansion,
                              d / "commands.jsonl", scenario_crcs, trace,
                              # a chain's game runs one step more (its result step: the next game's carry), with the
                              # interface events of its last frame; a play runs on to its result past the last compared
                              # step (its transmissions end there), with all its events
                              None if kind == "plays" else state.header["frame"] + (kind == "chains"),
                              golden or KINDS[kind][1],
                              ai_trace, extra)
    log.info("%-14s equal at steps %s", name, f"{steps[0]}..{steps[-1]}" if len(steps) > 1 else steps[0])
    return True, name


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--kind", choices=KINDS, default="initial", help="what the dumps are (and which goldens they write)")
    ap.add_argument("--root", type=Path, help="the emulator's dumps (default: the kind's)")
    ap.add_argument("--only", default="")
    ap.add_argument("--step", default="1", help='object steps: N, A-B, A-B/K or "all" (1 = game start)')
    ap.add_argument("--write-golden", action="store_true")
    ap.add_argument("--speech", default="", help="a language whose speech the plays ran with (sc_emulator play "
                    "--speech): its dumps, its goldens and its maps' TIME taken by Koprulu")
    ap.add_argument("--speech-data", default=os.environ.get("KOPRULU_SCR", ""),
                    help="where Koprulu reads the language's maps: a StarCraft: Remastered install or a repack with the "
                    "language's group (default: KOPRULU_SCR)")
    a = ap.parse_args()
    logging.basicConfig(level=logging.INFO, format="%(message)s")
    index = missions()
    only = [x for x in a.only.split(",") if x]
    default_root, golden = KINDS[a.kind]
    if a.speech:
        if a.kind not in SPEECH_KINDS:
            ap.error(f"--speech goes with --kind {' or '.join(SPEECH_KINDS)}")
        default_root, golden = (default_root.with_name(f"{default_root.name}-{a.speech}"),
                                golden.with_name(f"{golden.name}-{a.speech}"))
        if a.write_golden:
            golden.mkdir(parents=True, exist_ok=True)
        if not a.speech_data:
            ap.error("--speech needs --speech-data or KOPRULU_SCR")
        os.environ["KP_SPEECH"], os.environ["KP_SPEECH_DATA"] = a.speech, a.speech_data
    root = a.root or default_root
    runs = [(d, d.name) for d in sorted(root.iterdir())]
    if a.kind == "chains":  # root/<chain>/<k>-m<mission> (the menus before the first game log into pre-game)
        runs = [(g, f"{c.name}.{g.name}") for c in sorted(root.iterdir()) if c.is_dir() for g in sorted(c.iterdir())
                if CHAIN_GAME.match(g.name)]
    before: dict[Path, tuple[Path, str]] = {}  # a chain's game -> the game before it, its name
    if a.kind == "chains":
        for chain in sorted({d.parent for d, _ in runs}):
            games = [(d, n) for d, n in runs if d.parent == chain and (d / "states" / "s00001.state").exists()]
            for (prev, prev_name), (d, _) in zip(games, games[1:]):
                before[d] = (prev, prev_name)
    with tempfile.TemporaryDirectory(prefix="chain-carry-") as carry_tmp:
        return check_runs(a, ap, runs, before, index, only, golden, Path(carry_tmp))


def check_runs(a: argparse.Namespace, ap: argparse.ArgumentParser, runs: list[tuple[Path, str]],
               before: dict[Path, tuple[Path, str]], index: dict, only: list[str], golden: Path,
               carry_dir: Path) -> int:
    equal = failed = 0
    accepted, rejected = [], []
    for d, name in runs:
        if not (d / "states").is_dir() or (only and name not in only and d.parent.name not in only):
            continue
        extra, env = None, None
        if a.kind == "chains" and (d / "states" / "s00001.state").exists():
            extra, env, error = chain_context(d, name, before, index, carry_dir)
            if error:
                log.info("%-14s %s", name, error)
                failed += 1
                rejected.append(name)
                continue
        try:
            steps = steps_of(a.step, d / "states")
        except ValueError as e:
            ap.error(str(e))
        if not steps:
            log.info("%-14s no dumps", name)
            failed += 1
            rejected.append(name)
            continue
        ok, name = check_mission(d, index, steps, a.write_golden, a.kind, name, extra, env, golden)
        if ok:
            equal += 1
            accepted.append(name)
        else:
            failed += 1
            rejected.append(name)
    if a.write_golden:
        path = golden / "index.json"
        if only and path.exists():
            # A partial run updates its entries of the index and keeps the others.
            kept = set(json.loads(path.read_text())) - set(rejected)
            accepted = sorted(kept | set(accepted))
        path.write_text(golden_json(accepted))
    log.info("%d equal, %d differ or fail", equal, failed)
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
