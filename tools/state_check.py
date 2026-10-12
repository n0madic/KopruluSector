#!/usr/bin/env python3
"""Phase 3 acceptance: Koprulu's simulation state against the original's state dumps.

For each mission with a dump (tools/research/sc_emulator/state.py; step 1 = the state the
game start leaves before the first object step), Koprulu starts the same mission with the
dump's initial seed (tools/lua/start_state.lua) and both states are compared in the model
of tools/native_state.py / tools/lua/state_model.lua, category by category, over the fields
Koprulu prints. The first differences of each category are reported.

--write-golden stores, per mission and category, the CRC-32 of the original's canonical
lines (restricted to Koprulu's fields) in tests/golden/oracle/initial_state/<mission>.lua,
and the missions whose whole state is equal in index.lua; tests/lua/test_initial_state.lua
checks Koprulu against those without the private dumps.

Usage: python3 tools/state_check.py [--root STATES] [--only terran-m02,...] [--write-golden]
"""
from __future__ import annotations

import argparse
import logging
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
GOLDEN = ROOT / "tests" / "golden" / "oracle" / "initial_state"
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


def koprulu_model(mission: str, seed: int, expansion: int) -> tuple[str, dict]:
    """`mission` is a catalog name (generated/missions/<name>.kpm) or scenarios/<name>."""
    kpm = ROOT / "generated" / (mission if mission.startswith("scenarios/") else "missions/" + mission + ".kpm")
    if mission.startswith("scenarios/"):
        kpm = ROOT / "generated" / (mission + ".kpm")
    p = subprocess.run([LUA, "tools/lua/start_state.lua", str(kpm), str(seed), str(expansion)], cwd=ROOT,
                       capture_output=True, text=True)
    if p.returncode != 0:
        return p.stderr.strip().splitlines()[0] if p.stderr.strip() else f"exit {p.returncode}", {}
    return "", parse_lines(p.stdout)


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


def write_golden(name: str, mission: str, seed: int, expansion: int, crcs: dict[str, int]) -> None:
    GOLDEN.mkdir(parents=True, exist_ok=True)
    lines = [f"-- Initial state of {mission} in the original (tools/state_check.py --write-golden).",
             "return {", f'\tmission = "{mission}", seed = {seed}, expansion = {expansion},', "\tcrc = {"]
    lines += [f"\t\t{c} = {v}," for c, v in sorted(crcs.items())]
    lines += ["\t},", "}", ""]
    (GOLDEN / f"{name}.lua").write_text("\n".join(lines))


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--root", type=Path, default=DEFAULT_ROOT)
    ap.add_argument("--only", default="")
    ap.add_argument("--step", type=int, default=1, help="object step number of the dumps (1 = game start)")
    ap.add_argument("--write-golden", action="store_true")
    a = ap.parse_args()
    logging.basicConfig(level=logging.INFO, format="%(message)s")
    index = missions()
    only = [x for x in a.only.split(",") if x]
    equal = failed = 0
    accepted = []
    for d in sorted(a.root.iterdir()):
        dump = d / "states" / f"s{a.step:05d}.state"
        if not dump.exists() or (only and d.name not in only):
            continue
        state = State(dump)
        scenario = (ROOT / "tests" / "scenarios" / f"{d.name}.json").exists()
        mission = f"scenarios/{d.name}" if scenario else index[state.header["mission"]]
        expansion = state.read(0x536720, 1)[0]
        error, kop = koprulu_model(mission, state.header["initial_seed"], expansion)
        if error:
            failed += 1
            log.info("%-14s %-24s FAILED: %s", d.name, mission, error)
            continue
        nat = native_model(state)
        diffs = compare(nat, kop)
        if diffs:
            failed += 1
            log.info("%-14s %-24s DIFF in %s", d.name, mission, ", ".join(f"{c}({len(v)})" for c, v in diffs.items()))
            for c, v in diffs.items():
                for line in v[:SHOW]:
                    log.info("    %s", line)
        else:
            equal += 1
            accepted.append(d.name)
            log.info("%-14s %-24s equal", d.name, mission)
        if a.write_golden:
            write_golden(d.name, mission, state.header["initial_seed"], expansion, golden_lines(nat, kop))
    if a.write_golden and not only:
        lines = ["-- Missions whose initial state equals the original's (tools/state_check.py).", "return {"]
        lines += [f'\t"{name}",' for name in accepted]
        (GOLDEN / "index.lua").write_text("\n".join(lines + ["}", ""]))
    log.info("%d equal, %d differ or fail", equal, failed)
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
