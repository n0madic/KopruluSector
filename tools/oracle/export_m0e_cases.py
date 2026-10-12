#!/usr/bin/env python3
"""Build the M0E golden manifest and per-case golden data from recorded original observations.

Sources:
- docs/research/evidence/*.json: original 1.17.0.1 functions executed in Unicorn (isolated
  compositions with declared fixture inputs), static witnesses and the full-game emulator summary;
- private full-game emulator outputs (``--emu-root``, default ~/koprulu-private/emu):
  the terran-m01 3000-frame digest trace and the M0E Extractor completion run of
  tools/oracle/emu_m0e_extractor.py.

Every evidence file is hashed; its target executable must be the profile's; the probe scripts and
their recorded source dependencies are re-hashed against tools/research; every case must have
passed with ``actual == expected``. Ledger rows (R01-R37) and atlas entries are resolved from
docs/research, so an unknown row or address fails the export.

Outputs (tests/golden/oracle/m0e/): manifest.json (case index, inputs, observed outputs, scope,
links), index.lua and one Lua data file per case group. ``--check`` fails when any output differs
from what the current sources produce. Emulator-derived files are rebuilt only when the private
emulator outputs exist; otherwise the committed files are kept and their recorded hashes checked.

Usage: python3 tools/oracle/export_m0e_cases.py [--check] [--emu-root DIR]
"""
from __future__ import annotations

import argparse
import json
import logging
import re
import sys
from collections import OrderedDict
from pathlib import Path

from oracle_common import (
    EVIDENCE, GOLDEN, PROFILE, ROOT, TARGET_SHA256, ExportError, canonical_sha256, load_evidence, lua,
    sha256_bytes, sha256_file,
)

log = logging.getLogger("export_m0e_cases")

GENERATOR = "tools/oracle/export_m0e_cases.py"
OUT = GOLDEN / "m0e"
RESEARCH = ROOT / "docs" / "research"
ATLAS = RESEARCH / "atlas"
LEDGER = RESEARCH / "completion-ledger.md"
TOOLS_RESEARCH = ROOT / "tools" / "research"
DEFAULT_EMU_ROOT = Path("~/koprulu-private/emu")
EMU_E0_TRACE = Path("missions/terran-m01/trace.jsonl")
EMU_EXTRACTOR = Path("m0e/extractor-zerg02/result.json")

UNIT_POOL = 0x5DA3A0   # CUnit index 1 (atlas P08); probes and the emulator use the native pool
UNIT_SIZE = 0x150
UNIT_COUNT = 1700

STATUSES = OrderedDict([
    ("original-isolated-complete",
     "Original 1.17.0.1 functions executed in Unicorn to a complete return on declared inputs "
     "(isolated composition: no process start-up, Windows imports, accepted outer frame or whole mission)."),
    ("original-isolated-prefix",
     "Original code executed up to a recorded stop boundary; only the reached part is established, "
     "not the function's result."),
    ("emulator-observed",
     "Observed in the full-game emulator (original StarCraft.exe and storm.dll in Unicorn, Windows API "
     "boundary emulated); original-game-observed within docs/research/full-game-emulation.md limits."),
    ("static",
     "Derived from instructions of the exact executable (atlas, static witnesses); evidence level 3."),
    ("implementation-continuity",
     "No original oracle exists; the implementation's own uninterrupted run is the reference."),
])
INPUT_KINDS = ("authentic-records-with-declared-fixture", "synthetic-fixture", "full-mission", "none")


# ----------------------------------------------------------------------------- references

def atlas_index() -> dict[int, tuple[str, str]]:
    """Function start -> (package, proposed name): package headings ``### 0x00401234 `Name` ``
    and library table rows ``| 0x4d14d4 | `name` |`` (C01)."""
    index: dict[int, tuple[str, str]] = {}
    forms = (re.compile(r"^### 0x([0-9a-f]{8}) `([^`]+)`"), re.compile(r"^\| 0x([0-9a-f]{6,8}) \| `([^`]+)` \|"))
    for path in sorted(ATLAS.glob("*.md")):
        for line in path.read_text().splitlines():
            for form in forms:
                m = form.match(line)
                if m:
                    index.setdefault(int(m.group(1), 16), (path.stem.upper(), m.group(2)))
    return index


def ledger_rows() -> dict[str, str]:
    rows = {}
    for line in LEDGER.read_text().splitlines():
        m = re.match(r"^\| (R\d\d) \| ([^|]+?) \|", line)
        if m:
            rows[m.group(1)] = line
    return rows


class Refs:
    def __init__(self) -> None:
        self.atlas = atlas_index()
        self.ledger = ledger_rows()

    def entries(self, addresses: list[int]) -> list[dict]:
        out = []
        for a in addresses:
            if a not in self.atlas:
                raise ExportError(f"0x{a:x} is not an atlas function entry")
            package, name = self.atlas[a]
            out.append({"address": f"0x{a:06x}", "package": package, "name": name})
        return out

    def rows(self, docs: list[str], extra: tuple[str, ...] = ()) -> list[str]:
        """Ledger rows that cite one of the research documents, plus explicitly added rows."""
        found = {r for r, text in self.ledger.items() for d in docs if f"]({d})" in text}
        for r in extra:
            if r not in self.ledger:
                raise ExportError(f"unknown ledger row {r}")
        return sorted(found | set(extra))


# ----------------------------------------------------------------------------- helpers

def unit_index(address: int) -> int:
    """Native CUnit pool index (1-based) of a guest CUnit address; 0 for null."""
    if address == 0:
        return 0
    off = address - UNIT_POOL
    if off < 0 or off % UNIT_SIZE or off // UNIT_SIZE >= UNIT_COUNT:
        raise ExportError(f"0x{address:x} is not a CUnit address")
    return off // UNIT_SIZE + 1


def rng_summary(v: dict) -> dict:
    if not v.get("independent_integer_LCG_matches", False):
        raise ExportError("RNG validation did not match the independent LCG")
    return {"initial_seed": v["initial_seed"], "total_calls": v["total_calls"], "final_state": v["final_state"],
            "source_counters": [[int(k), n] for k, n in v["source_counters"].items() if n]}


def compact(value: object, limit: int = 600) -> object:
    """Keep a JSON value when small, otherwise replace it by its hash and size."""
    text = json.dumps(value, sort_keys=True, separators=(",", ":"))
    if len(text) <= limit:
        return value
    return {"omitted": True, "canonical_sha256": sha256_bytes(text.encode()), "canonical_bytes": len(text)}


def probe_line(probe: str, needle: str) -> int:
    for n, line in enumerate((TOOLS_RESEARCH / probe).read_text().splitlines(), 1):
        if needle in line:
            return n
    raise ExportError(f"{probe}: {needle!r} not found")


class Evidence:
    """Loaded evidence files with their hashes and verified provenance."""

    def __init__(self) -> None:
        self.files: dict[str, dict] = {}
        self.data: dict[str, dict] = {}
        self._by_hash: dict[str, list[str]] = {}
        for p in sorted(EVIDENCE.glob("*.json")):
            self._by_hash.setdefault(sha256_file(p), []).append(p.name)

    def load(self, name: str, probe: str | None = None) -> dict:
        if name in self.data:
            return self.data[name]
        data, digest = load_evidence(name)
        info: dict = {"sha256": digest, "bytes": (EVIDENCE / name).stat().st_size}
        target = data.get("target_sha256") or data.get("executable_sha256") or data.get("plan", {}).get("target_sha256")
        if target != TARGET_SHA256:
            raise ExportError(f"{name}: target executable {target} is not the profile executable")
        info["target_sha256"] = target
        if probe:
            recorded = data.get("probe_sha256") or data.get("probe_script_sha256")
            current = sha256_file(TOOLS_RESEARCH / probe)
            if recorded != current:
                raise ExportError(f"{name}: probe {probe} hash {current} differs from recorded {recorded}")
            deps = data.get("source_dependencies_sha256", {})
            stale = [k for k, v in deps.items() if sha256_file(TOOLS_RESEARCH / k) != v]
            if stale:
                raise ExportError(f"{name}: source dependencies changed: {stale}")
            info.update(probe=f"tools/research/{probe}", probe_sha256=recorded, source_dependencies_verified=len(deps))
            if not data.get("all_passed", False):
                raise ExportError(f"{name}: not all cases passed")
            for c in data["cases"]:
                if not c.get("passed") or c.get("actual") != c.get("expected"):
                    raise ExportError(f"{name}: case {c.get('case')} did not pass")
            info["flags"] = {k: data[k] for k in ("original_code_executed", "complete_game_executed", "helper_stubs",
                                                   "host_imports_executed", "outer_accepted_frame_executed",
                                                   "complete_cases", "prefix_cases") if k in data}
        linked = {k: self._by_hash[v] for k, v in data.items()
                  if k.endswith("_sha256") and isinstance(v, str) and v in self._by_hash}
        if linked:
            info["linked_evidence"] = dict(sorted(linked.items()))
        self.files[name] = info
        self.data[name] = data
        return data

    def ref(self, name: str, case_index: int | None = None) -> dict:
        r = {"file": f"docs/research/evidence/{name}", "sha256": self.files[name]["sha256"]}
        if case_index is not None:
            c = self.data[name]["cases"][case_index]
            r.update(case_index=case_index, case=c.get("case"), case_sha256=canonical_sha256(c))
        return r


def doc_paths(*docs: str) -> list[str]:
    for d in docs:
        if not (RESEARCH / d).exists():
            raise ExportError(f"missing research document {d}")
    return [f"docs/research/{d}" for d in docs]


# ----------------------------------------------------------------------------- E0

E0_FRAME_ATLAS = [0x445070, 0x4781C0, 0x4B4010, 0x48DD90, 0x4938E0, 0x4B0D70, 0x496C80, 0x48DF50, 0x483400,
                  0x486C80, 0x4ACDF0, 0x438EF0, 0x40A5F0, 0x40C910, 0x418990, 0x4BE500, 0x4BE410]
E0_RNG_ATLAS = [0x4781C0, 0x4781D0, 0x4781E0, 0x4781F0, 0x478200, 0x478240, 0x478270, 0x443F40]
E0_POOL_ATLAS = [0x424860, 0x424900, 0x4256B0, 0x425CD0]


def e0(ev: Evidence, refs: Refs, emu: Path | None, files: dict) -> dict:
    leaf = ev.load("native-leaf-probes.json")
    kinds: dict[str, int] = {}
    for c in leaf["cases"]:
        kinds[c["kind"]] = kinds.get(c["kind"], 0) + 1
    if leaf["checks"] != len(leaf["cases"]) or not leaf["all_expected_results_matched"]:
        raise ExportError("native-leaf-probes.json: check count or result mismatch")
    frame = ev.load("native-economy-frame-static.json")
    gfe = ev.load("full-game-emulation.json")
    cases = [
        {
            "id": "E0.rng_leaf",
            "title": "Game RNG raw/ranged leaf vectors (enable flag, overflow, counters)",
            "status": "original-isolated-complete",
            "inputs": {"kind": "synthetic-fixture", "declared": "seed, enable flag and bounds per vector; entry state 0xffffffff for ranged calls"},
            "evidence": [ev.ref("native-leaf-probes.json")],
            "observed": {"raw_rng_vectors": kinds.get("raw_rng", 0), "ranged_rng_vectors": kinds.get("ranged_rng", 0)},
            "golden": ["tests/golden/oracle/rng_game.lua", "tests/golden/oracle/rng_portrait.lua"],
            "scope": ["isolated leaf functions only; per-frame consumer order is not covered"],
            "research": doc_paths("native-prng.md", "native-frame.md"),
            "ledger": refs.rows(["native-prng.md"], ("R09",)),
            "atlas": refs.entries(E0_RNG_ATLAS),
            "missing": [],
        },
        {
            "id": "E0.frame_order",
            "title": "Object-step phase order, slow AI service countdowns, speed table",
            "status": "static",
            "inputs": {"kind": "none"},
            "evidence": [ev.ref("native-economy-frame-static.json")],
            "observed": {"speed_table_ms": frame["speed_table"]["values"],
                         "speed_table_va": frame["speed_table"]["preferred_va"],
                         "witness_groups": sorted(frame["witnesses"])},
            "scope": ["call order and counter writes from instructions; no executed trace of the whole frame"],
            "research": doc_paths("native-frame.md", "native-triggers.md"),
            "ledger": refs.rows(["native-frame.md"], ("R08", "R25")),
            "atlas": refs.entries(E0_FRAME_ATLAS),
            "missing": ["no isolated original execution of a whole object step; the emulator digest below observes "
                        "elapsed cadence and RNG only at mission scale"],
        },
        {
            "id": "E0.pools_lists",
            "title": "CUnit allocation, recycle and free-list order",
            "status": "static",
            "inputs": {"kind": "none"},
            "evidence": [],
            "observed": {},
            "scope": ["atlas specification only; isolated probes observe the free head (0x665b38 count, free_unit_head) "
                      "in their E3 native_state but no dedicated list-order vector exists"],
            "research": doc_paths("native-populated-lifecycle.md"),
            "ledger": refs.rows([], ("R10",)),
            "atlas": refs.entries(E0_POOL_ATLAS),
            "missing": ["no dedicated original free-list order vector; E3 finite-pool cases constrain it indirectly"],
        },
    ]
    clock = e0_clock(gfe, emu, files)
    cases.append({
        "id": "E0.elapsed_cadence_emulator",
        "title": "Elapsed game time and RNG over 3000 frames of terran-m01 (no input)",
        "status": "emulator-observed",
        "inputs": {"kind": "full-mission", "warp": "terran0", "cd": "original", "frames": 3000,
                   "initial_seed": clock["initial_seed"], "speed_index": clock["speed_index"]},
        "evidence": [ev.ref("full-game-emulation.json")],
        "emulator_source": clock["source"],
        "observed": {k: clock[k] for k in ("elapsed_first_increment_frame", "elapsed_period_frames",
                                           "elapsed_increments", "checkpoints")},
        "golden": ["tests/golden/oracle/m0e/e0_clock_emulator.lua"],
        "scope": ["whole-mission observation; reproducing per-frame RNG requires the complete mission, not E0"],
        "research": doc_paths("full-game-emulation.md", "native-frame.md"),
        "ledger": refs.rows([], ("R08", "R09")),
        "atlas": refs.entries([0x4BE500, 0x4BE410, 0x445070]),
        "missing": [],
    })
    return {"id": "E0", "plan_row": "Clocks/RNG/lists/geometry, creation from source data, reproducible dump",
            "cases": cases}


def e0_clock(gfe: dict, emu: Path | None, files: dict) -> dict:
    """Elapsed cadence and RNG checkpoints of terran-m01; recomputed from the private trace when present."""
    mission = next(m for m in gfe["missions"] if m["name"] == "terran-m01")
    name = "e0_clock_emulator.lua"
    trace = emu / EMU_E0_TRACE if emu else None
    if trace is None or not trace.exists():
        return committed_emulator(name, files)
    rows = [json.loads(line) for line in trace.read_text().splitlines()]
    if [r["f"] for r in rows] != list(range(1, len(rows) + 1)):
        raise ExportError("terran-m01 trace frames are not consecutive from 1")
    increments = [r["f"] for p, r in zip(rows, rows[1:]) if r["el"] != p["el"]]
    periods = {b - a for a, b in zip(increments, increments[1:])}
    if len(periods) != 1:
        raise ExportError(f"terran-m01 elapsed periods are not uniform: {sorted(periods)}")
    checkpoints = {f: rows[f - 1] for f in (1, 1000, 2000, 3000)}
    for f, row in checkpoints.items():
        if mission["checkpoints"][str(f)] != row:
            raise ExportError(f"terran-m01 frame {f} digest differs from full-game-emulation.json")
    data = {
        "source": {"file": "<emu>/" + str(EMU_E0_TRACE), "sha256": sha256_file(trace), "frames": len(rows)},
        "initial_seed": mission["initial_seed"], "speed_index": mission["speed_index"],
        "elapsed_first_increment_frame": increments[0], "elapsed_period_frames": periods.pop(),
        "elapsed_increments": len(increments),
        "checkpoints": [[f, r["rng"], r["el"], r["u"], r["uh"], r["h"]] for f, r in checkpoints.items()],
        "rng_by_frame": [r["rng"] for r in rows],
    }
    files[name] = render_data("E0 elapsed cadence and RNG, full-game emulator terran-m01 (no input)", data,
                              fields={"checkpoints": "frame, rng state 0x67393c, elapsed 0x5349d8, active units, "
                                                     "hidden units, unit digest h (game.py)"})
    return data


def committed_emulator(name: str, files: dict) -> dict:
    """Keep a committed emulator-derived file when the private emulator outputs are absent."""
    path = OUT / name
    if not path.exists():
        raise ExportError(f"{name}: emulator outputs are absent and no committed file exists")
    text = path.read_text()
    files[name] = text
    m = re.search(r"^-- data: (.*)$", text, re.M)
    if not m:
        raise ExportError(f"{name}: committed file has no data summary line")
    return json.loads(m.group(1))


def render_data(title: str, data: dict, fields: dict | None = None, summary_keys: tuple[str, ...] | None = None) -> str:
    """A Lua data module; the first lines carry the title and a JSON summary (used by --check
    when emulator outputs are absent)."""
    summary = {k: v for k, v in data.items() if summary_keys is None or k in summary_keys}
    summary = {k: v for k, v in summary.items() if k != "rng_by_frame"}
    lines = [f"-- Generated by {GENERATOR}; do not edit.", f"-- {title}",
             "-- data: " + json.dumps(summary, separators=(",", ":"))]
    body = dict(data)
    if fields:
        body["fields"] = fields
    lines.append("return " + lua(body))
    return "\n".join(lines) + "\n"


# ----------------------------------------------------------------------------- E1

E1_ATLAS = [0x445070, 0x487580, 0x485F20, 0x42A380, 0x485020, 0x497A20, 0x494D90, 0x493A60, 0x486A20, 0x4B2640,
            0x482EE0, 0x469460, 0x46A420, 0x46A280, 0x4A98E0, 0x431FE0, 0x432420, 0x432510, 0x412C90, 0x48E9B0,
            0x48EC20, 0x431F70, 0x42BFD0, 0x442CC0, 0x445720, 0x445690]
E1_SCENES = {56: "tutorial_map56", 58: "zerg01_map58", 34: "protoss02_map34", 36: "protoss04_map36"}
AREA_FIELDS = ["x(+0x00)", "y(+0x02)", "mineral_fields(+0x04)", "gas_objects(+0x05)", "start_marker(+0x06)",
               "flags(+0x07)", "remaining_minerals(+0x08)", "remaining_gas(+0x0c)"]


def e1(ev: Evidence, refs: Refs, files: dict) -> dict:
    name = "native-campaign-ai-harvesting-probes.json"
    data = ev.load(name, "probe_native_campaign_ai_harvesting_117.py")
    if data["complete_cases"] != 4 or data["prefix_cases"] != 0 or data["helper_stubs"]:
        raise ExportError(f"{name}: unexpected case composition")
    cases = []
    for i, c in enumerate(data["cases"]):
        scene = E1_SCENES[c["inputs"]["map_index"]]
        golden_name = f"e1_{scene}.lua"
        observed, golden = e1_case(c)
        files[golden_name] = render_data(f"E1 {scene}: natural WorkerAI mining, {name} case {i}", golden,
                                         fields=E1_FIELDS, summary_keys=())
        inp = c["inputs"]
        tb = c["actual"]["town_binding"]
        ph = c["actual"]["physical_harvesting"]
        cases.append({
            "id": f"E1.{scene}",
            "title": f"Campaign Town WorkerAI harvesting, {inp['map']}",
            "status": "original-isolated-complete",
            "inputs": {
                "kind": "authentic-records-with-declared-fixture",
                "map_index": inp["map_index"], "map": inp["map"], "chk_sha256": inp["chk_sha256"],
                "mtxm_sha256": inp["mtxm_sha256"], "tileset": inp["tileset"], "dimensions": inp["dimensions"],
                "terrain_sha256": inp["terrain_sha256"], "graph_snapshot_sha256": inp["graph_snapshot_sha256"],
                "resource_records": [{k: r[k] for k in ("type", "x", "y", "owner", "record_index", "sha256")}
                                     for r in inp["resource_records"]],
                "start_records": [{k: r[k] for k in ("type", "x", "y", "owner", "record_index", "sha256")}
                                  for r in inp["start_records"]],
                "depot_worker_records": tb["actual_depot_worker_records"],
                "town_owner": tb["town"]["owner"],
                "runtime_controller_fixture": inp["runtime_controllers_fixture"],
                "runtime_race_fixture": tb["runtime_race_fixture"],
                "script_bytes_fixture": tb["script_bytes_fixture"],
                "script_rectangle_fixture": tb["script_rectangle_fixture"],
                "terrain_visibility_low_word_fixture": inp["terrain_visibility_low_word_fixture"],
                "elapsed_tick_fixture": ph["elapsed_tick_fixture"],
                "slow_service_countdown_fixture": ph["slow_service_countdown_fixture"],
                "logical_frame_counter_host_supplied": ph["logical_frame_counter_host_supplied"],
                "initial_worker_service_mode": ph["initial_worker_service_mode"],
                "rng_initial_seed": c["execution"]["rng_validation"]["initial_seed"],
                "x87_control_word_fixture": tb["x87_control_word_fixture"], "mxcsr_fixture": tb["mxcsr_fixture"],
                "graph_creation_rerun": inp["graph_creation_rerun"],
                "mission_nonresource_units_loaded": inp["mission_nonresource_units_loaded"],
            },
            "evidence": [ev.ref(name, i)],
            "observed": observed,
            "golden": [f"tests/golden/oracle/m0e/{golden_name}"],
            "scope": [
                "complete original 0x445070 object callbacks; no accepted outer frame, triggers or whole mission",
                "graph read fields reconstructed from an original snapshot; graph generation not rerun",
                "controller/race/script/rectangle/visibility are declared caller inputs, not mission start-up",
                "four terrain preparation fragments are separate and not complete functions",
            ],
            "research": doc_paths("native-campaign-ai-harvesting.md"),
            "ledger": refs.rows(["native-campaign-ai-harvesting.md"]),
            "atlas": refs.entries(E1_ATLAS),
            "missing": [],
        })
    return {"id": "E1", "plan_row": "Four recorded Terran/Protoss Town scenes: Guard -> ComputerAI -> WorkerAI, "
                                    "mineral choice, walking, cargo, two deliveries per worker, conservation",
            "cases": cases}


E1_FIELDS = {
    "workers": "native CUnit pool indices of the genuine workers, in initial order",
    "runs": "per worker: run-length list {first callback, order, stage, cargo type, cargo amount}",
    "positions": "per worker: flat x, y pixel positions after each callback 1..callbacks",
    "amounts": "resource amounts at baseline, then change points {callback, resource index (1-based), amount}",
    "stock": "change points {callback, current stock increase, cumulative increase}",
    "deposits": "native deposit entries {callback, worker index, owner, amount, cargo type, x, y}",
    "worker_ai_services": "{zero-based frame input, worker index, order, ai wait}",
    "areas": "resource-area records " + ", ".join(AREA_FIELDS),
}


def e1_case(c: dict) -> tuple[dict, dict]:
    act, ex = c["actual"], c["execution"]
    ph = act["physical_harvesting"]
    timeline = ph["callback_timeline"]
    if [r["callback"] for r in timeline] != list(range(1, len(timeline) + 1)):
        raise ExportError("E1 callback timeline is not consecutive")
    workers = [unit_index(w["address"]) for w in ph["initial_workers"]]
    runs: dict[int, list] = {w: [] for w in workers}
    positions: dict[int, list] = {w: [] for w in workers}
    baseline = ph["resource_amount_baseline"]
    amounts, stock = [], []
    prev_amounts, prev_stock = baseline, (ph["current_stock_baseline"], ph["cumulative_stock_baseline"])
    for row in timeline:
        cb = row["callback"]
        seen = [unit_index(w["address"]) for w in row["workers"]]
        if sorted(seen) != sorted(workers):
            raise ExportError(f"E1 callback {cb}: worker set changed")
        carried = 0
        for w in row["workers"]:
            ix = unit_index(w["address"])
            key = [w["order"], w["stage"], w["cargo_type"], w["cargo_amount"]]
            if not runs[ix] or runs[ix][-1][1:] != key:
                runs[ix].append([cb] + key)
            positions[ix] += w["position"]
            carried += w["cargo_amount"]
        for k, (a, b) in enumerate(zip(prev_amounts, row["amounts"])):
            if a != b:
                amounts.append([cb, k + 1, b])
        if (row["stock"], row["cumulative"]) != prev_stock:
            stock.append([cb, row["stock"], row["cumulative"]])
        if sum(baseline) - sum(row["amounts"]) != row["cumulative"] + carried:
            raise ExportError(f"E1 callback {cb}: conservation does not hold")
        prev_amounts, prev_stock = row["amounts"], (row["stock"], row["cumulative"])
    deposits = [[d["callback"], unit_index(d["worker"]), d["owner"], d["amount"], d["cargo_type"]] + d["position"]
                for d in ph["native_deposit_entries"]]
    services = [[s["frame_input"], unit_index(s["worker"]), s["order"], s["ai_wait"]]
                for s in ph["original_worker_ai_services"]]
    if any(s["top_level_original_entry"] != "0x445070" for s in ph["original_worker_ai_services"]):
        raise ExportError("E1 WorkerAI service outside the object step")
    by_worker = {unit_index(int(k)): n for k, n in ph["deposits_by_worker"].items()}
    last = timeline[-1]
    rng = rng_summary(ex["rng_validation"])
    observed = {
        "object_callbacks": len(timeline),
        "workers": workers,
        "deposits_by_worker": [[w, by_worker[w]] for w in workers],
        "deposits_total_minerals": sum(d[3] for d in deposits),
        "final_stock": [last["stock"], last["cumulative"]],
        "final_carried": sum(w["cargo_amount"] for w in last["workers"]),
        "resource_decrease": sum(baseline) - sum(last["amounts"]),
        "first_worker_ai_frame_input": {str(w): min(s[0] for s in services if s[1] == w) for w in workers},
        "orders_reached": sorted({r[1] for w in workers for r in runs[w]}),
        "initial_areas": act["initial_areas"], "final_areas": act["final_areas"],
        "area_fields": AREA_FIELDS,
        "resource_area_countdown_final": act["countdown"],
        "rng": rng,
        "complete_call_count": ex["complete_call_count"], "instruction_count": ex["instruction_count"],
        "call_trace_sha256": ex["call_trace_summary"]["canonical_full_trace_sha256"],
        "timeline_sha256": canonical_sha256(timeline),
    }
    golden = {
        "callbacks": len(timeline), "workers": workers,
        "runs": [runs[w] for w in workers], "positions": [positions[w] for w in workers],
        "baseline_amounts": baseline, "amounts": amounts, "stock": stock,
        "deposits": deposits, "worker_ai_services": services,
        "initial_areas": act["initial_areas"], "final_areas": act["final_areas"],
        "final_resource_amounts": act["final_resource_amounts"],
        "rng_initial_seed": rng["initial_seed"], "rng_total_calls": rng["total_calls"],
        "rng_final_state": rng["final_state"], "rng_source_counters": rng["source_counters"],
    }
    return observed, golden


# ----------------------------------------------------------------------------- E2

E2_ATLAS = [0x445070, 0x424900, 0x424DA0, 0x494D90, 0x493A60, 0x487580, 0x483400, 0x4865B0, 0x4872F0, 0x4B32A0,
            0x41BDE0, 0x48D320, 0x487990, 0x4AE760, 0x4A9B20, 0x4BE410, 0x4A98E0, 0x425610, 0x482040, 0x482310]
E2_LEAF_ATLAS = [0x482040, 0x482310, 0x483030, 0x487550, 0x485FA0, 0x4835E0]
E2_PROFILES = {"paid": "paid 450/25, availability 1", "cash399": "399/25, availability 1",
               "academy_unavailable": "450/25, Academy availability 0"}
E2_STATE_FIELDS = {
    "c": "callback (complete 0x445070 plus selected 0x4be410 timer path)",
    "pc": "script PC", "wait": "script wait", "active": "script node active (non-zero head)",
    "el": "elapsed 0x5349d8", "stocks": "[minerals, gas] of player 3", "supply": "Terran [provided, used, cap] half-units",
    "completed": "{type, completed count}", "raw": "{type, raw count}", "train_word": "explicit training word",
    "heap": "spending heap entries {priority, kind, type, context = 1 for the Town}",
    "demand": "PlayerAI demand and available budget [6]",
    "requests": "Town request records [flags/count, priority, type, special]",
    "town": "[worker count, builder unit index, main unit index]", "buildings": "BuildingAI unit indices",
    "region": "[state, ground count, military head non-zero]",
    "units": "{pool index, type, owner, order, order state, secondary, secondary state, x, y, hp, status, completed, "
             "queue[5], queue cursor, child index, AI kind, assigned region}",
}


def e2_state(s: dict, town: int, callback: int) -> dict:
    t = s["town"]

    def ctx(v: int) -> int:
        return 1 if v == town else v
    units = [[unit_index(u["address"]), u["type"], u["owner"], u["order"], u["order_state"], u["secondary"],
              u["secondary_state"], u["position"][0], u["position"][1], u["hp"], u["status"], u["completed"],
              u["queue"], u["cursor"], unit_index(u["child"]), u["AI_kind"] or 0, u["assigned_region"] or 0]
             for u in s["units"]]
    return {
        "c": callback, "pc": s["script_PC"], "wait": s["script_wait"], "active": s["script_active"] != 0,
        "el": s["elapsed"], "stocks": s["stocks"], "supply": s["supply"],
        "completed": [[int(k), v] for k, v in s["completed_counts"].items()],
        "raw": [[int(k), v] for k, v in s["raw_counts"].items()],
        "train_word": s["explicit_training_word"],
        "heap": [[p, k, ty, ctx(c)] for p, k, ty, c in s["heap_entries"]],
        "demand": s["demand_and_available"], "requests": t["requests"],
        "town": [t["worker_count"], unit_index(t["builder"]), unit_index(t["main"])],
        "buildings": [unit_index(b["unit"]) for b in t["buildings"]],
        "region": [s["native_region"]["state"], s["native_region"]["ground_count"],
                   s["native_region"]["military_head"] != 0],
        "units": units,
    }


def e2(ev: Evidence, refs: Refs, files: dict) -> dict:
    name = "native-ai-tech-build-probes.json"
    data = ev.load(name, "probe_native_ai_tech_build_117.py")
    if data["complete_cases"] != 3 or data["prefix_cases"] != 0 or data["helper_stubs"]:
        raise ExportError(f"{name}: unexpected case composition")
    line = probe_line("probe_native_ai_tech_build_117.py", "record('original_stock_opcode_paid_tech_chain_'")
    cases = []
    for i, c in enumerate(data["cases"]):
        profile = c["case"].removeprefix("original_stock_opcode_paid_tech_chain_")
        inp, ns = c["inputs"], c["native_state"]
        town = inp["initial"]["town"]["address"]
        timeline = [e2_state(e["state"], town, e["callback"]) for e in inp["timeline"]]
        initial = e2_state(inp["initial"], town, 0)
        final = e2_state(ns, town, inp["actual_object_callbacks"])
        requirements = [[h["callback"] if isinstance(h["callback"], int) else -1, r["requested"], r["producer_type"],
                         r["completed_Academy"], r["completed_Barracks"], r["return_eax"], r["error"]]
                        for h in inp["requirement_return_history"] for r in h["returns"]]
        rng = rng_summary(c["execution"]["rng_validation"])
        golden_name = f"e2_{profile}.lua"
        golden = {"profile": profile, "callbacks": inp["actual_object_callbacks"],
                  "completion_callbacks": [[int(k), v] for k, v in inp["completion_callbacks"].items()],
                  "first_natural_worker_service_callback": inp["first_natural_worker_service"]["callback"],
                  "requirements": requirements, "initial": initial, "timeline": timeline, "final": final,
                  "actual": c["actual"], "rng_total_calls": rng["total_calls"], "rng_final_state": rng["final_state"],
                  "rng_source_counters": rng["source_counters"]}
        files[golden_name] = render_data(f"E2 {profile}: AI paid prerequisite chain, {name} case {i}", golden,
                                         fields=dict(E2_STATE_FIELDS, requirements=(
                                             "0x4b32a0 returns {callback (-1 = after the bounded window), requested, "
                                             "producer type, completed Academy, completed Barracks, eax, error}")),
                                         summary_keys=())
        cases.append({
            "id": f"E2.{profile}",
            "title": f"CC106 + SCV7 declared macro Depot109/Barracks111/Academy112/Firebat32, {E2_PROFILES[profile]}",
            "status": "original-isolated-complete",
            "inputs": {
                "kind": "synthetic-fixture",
                "declared_macro_hex": inp["declared_macro"], "macro_is_synthetic_composition": inp["macro_is_synthetic_composition"],
                "genuine_initial_unit_types": inp["genuine_initial_unit_types"],
                "initial_stocks": inp["initial"]["stocks"], "initial_supply": inp["initial"]["supply"],
                "initial_script_pc_wait_elapsed": [inp["initial"]["script_PC"], inp["initial"]["script_wait"],
                                                   inp["initial"]["elapsed"]],
                "initial_units": [[u[1], u[7], u[8], u[3]] for u in initial["units"]],
                "only_host_progress_input": inp["only_host_progress_input"],
                "rng_initial_seed": rng["initial_seed"],
                "cadence": "each callback = complete 0x445070 then selected 0x4be410(ECX=42) timer path; "
                           "elapsed advances once per 16 timer services",
                "world": "declared clear 4096x4096 one-region world (native-ai-tech-build.md)",
            },
            "evidence": [ev.ref(name, i)],
            "observed": {
                "actual": c["actual"], "actual_fields": ["completed counts by type", "[minerals, gas]", "Town worker count"],
                "actual_source": f"tools/research/probe_native_ai_tech_build_117.py:{line}",
                "object_callbacks": inp["actual_object_callbacks"],
                "completion_callbacks": golden["completion_callbacks"],
                "final_script_pc_wait": [final["pc"], final["wait"]], "final_stocks": final["stocks"],
                "final_supply": final["supply"], "final_heap": final["heap"], "final_demand": final["demand"],
                "rng": rng, "timeline_change_points": len(timeline),
                "timeline_sha256": canonical_sha256(inp["timeline"]),
                "call_trace_sha256": c["execution"]["call_trace_summary"]["canonical_full_trace_sha256"],
            },
            "golden": [f"tests/golden/oracle/m0e/{golden_name}"],
            "scope": [
                "macro is a declared assembly of stock opcodes, not an unchanged campaign script",
                "synthetic clear one-region world; no income, no attack wave",
                "selected slow branches suppressed by declared countdown inputs (0x6c46d4, 0x69b568, 0x6663e4 = 10000)",
                "timeline lists change points only; state between change points equals the previous entry",
            ],
            "research": doc_paths("native-ai-tech-build.md"),
            "ledger": refs.rows(["native-ai-tech-build.md"]),
            "atlas": refs.entries(E2_ATLAS),
            "missing": [],
        })
    cases.append(e2_leaf(ev, refs, files))
    return {"id": "E2", "plan_row": "CC106 + SCV7, fixed cash 450/25, declared macro; 399 minerals and Academy "
                                    "unavailable refusals", "cases": cases,
            "notes": ["The E1 income + E2 construction integration scenario has no original observation: the "
                      "published E1/E2 compositions use different caller inputs, and their fixture worlds are not "
                      "loadable missions, so the full-game emulator cannot replay them. It checks Koprulu's "
                      "architecture only (plan M0E)."]}


def e2_leaf(ev: Evidence, refs: Refs, files: dict) -> dict:
    name = "native-leaf-probes.json"
    data = ev.load(name)
    kinds = ("resource_percentages", "build_insert", "spending_heap_push", "spending_heap_pop", "spending_unit_limit")
    vectors = [{k: v for k, v in c.items() if k != "instructions"} for c in data["cases"] if c["kind"] in kinds]
    counts = {k: sum(1 for c in vectors if c["kind"] == k) for k in kinds}
    files["e2_spending_leaf.lua"] = render_data(
        f"E2 spending heap, demand percentages, request insertion and unit-limit leaf vectors, {name}",
        {"vectors": vectors, "counts": counts}, summary_keys=("counts",))
    return {
        "id": "E2.spending_leaf",
        "title": "Spending heap push/pop, demand percentages, Town request insertion, unit-limit predicate",
        "status": "original-isolated-complete",
        "inputs": {"kind": "synthetic-fixture", "declared": "per-vector inputs as recorded (native-economy.md)"},
        "evidence": [ev.ref(name)],
        "observed": {"vectors": counts},
        "golden": ["tests/golden/oracle/m0e/e2_spending_leaf.lua"],
        "scope": ["isolated leaf functions; the whole allocator and spending loop are not executed here"],
        "research": doc_paths("native-economy.md"),
        "ledger": refs.rows(["native-economy.md"], ("R27",)),
        "atlas": refs.entries(E2_LEAF_ATLAS),
        "missing": [],
    }


# ----------------------------------------------------------------------------- E3

# Field labels of each probe's recorded ``actual`` tuple, transcribed from the probe source
# (the line is located by the record() needle). "minerals"/"gas" are player 3 entries of
# 0x5263d0/0x526400; "units" is the allocated CUnit count 0x665b38; supply triplets are the
# Protoss arrays 0x529544/0x529574/0x5295a4 (atlas P05); CUnit offsets follow atlas P12.
ADMISSION = ["queue admission 0x41b4b0 eax", "minerals", "producer.queue[0] +0x98"]
PYLON_BUILD = ["[minerals, gas] after build admission", "minerals", "gas", "building.type +0x64",
               "building.completed +0xdc&1", "building.hp +0x08", "building.shields +0x60", "supply provided",
               "supply used", "supply max", "units"]
POOL_CANCEL = ["drone.type", "after.completed", "after.hp", "minerals", "gas", "after.order", "live count 41",
               "live count 142", "completed count 41", "completed count 142", "unit count unchanged"]
TOWN_POOL = ["unit.type", "unit.completed", "ai.kind +0x08", "ai.town +0x14", "town workers before",
             "town workers during", "town.worker_count +0x1a", "minerals"]
CARRIED_DEPOT = ["minerals", "gas", "worker.carried_item +0xc0", "worker.cargo +0xe0", "item.carrier +0xd4",
                 "positive Drop entries", "units", "Bring goal 0x4b8f20 eax"]
CARRIED_POOL = ["minerals", "gas", "worker.type", "units", "Bring goal 0x4b8f20 eax", "unit-finder depth 0x5d6e48"]
ASSIM_RESTORE = ["minerals", "gas", "geyser.type", "geyser.owner +0x4c", "geyser.amount +0xd0", "geyser.hp",
                 "units", "supply provided", "supply used"]
ASSIM_FINITE = ["minerals after admission", "minerals", "geyser.type", "geyser.owner", "geyser.amount",
                "probe.queue[0]", "units"]
EXTRACTOR_FAIL = ["minerals", "worker.type", "worker.carried_item", "worker.cargo", "worker.queue[0]",
                  "positive Drop entries", "Bring goal"]
EXTRACTOR_CANCEL = ["minerals", "geyser.type", "geyser.owner", "geyser.amount", "geyser.hp", "units",
                    "replacement Drone created", "item hidden", "Bring goal"]

E3_FAMILIES = [
    {
        "family": "protoss_construction", "evidence": "native-protoss-construction-probes.json",
        "probe": "probe_native_protoss_construction_117.py", "doc": "native-protoss-construction.md",
        "title": "Paid Probe -> Pylon warp, cancel, admission, pool failures, late recheck, Town services, power",
        "atlas": [0x424900, 0x424DA0, 0x41B4B0, 0x42A1E0, 0x42A340, 0x42EE00, 0x42F230, 0x41BCB0, 0x42F020,
                  0x411840, 0x40E650, 0x42F5B0, 0x42F270, 0x430670, 0x430610, 0x41A340, 0x486200],
        "labels": [
            ("paid_probe_to_pylon156_", "record('paid_probe_to_pylon156_'", PYLON_BUILD),
            ("paid_pylon_cancel_", "record('paid_pylon_cancel_'",
             ["minerals", "gas", "units", "supply provided", "supply used", "supply max", "psi provider head 0x665b54"]),
            ("paid_probe_queue_balance_", "record('paid_probe_queue_balance_'", ADMISSION),
            ("paid_probe_pylon_queue_balance_", "record('paid_probe_pylon_queue_balance_'", ADMISSION),
            ("pylon_after_payment_", "record('pylon_after_payment_'",
             ["[minerals, gas] after build admission", "minerals", "gas", "units", "supply provided", "supply used"]),
            ("pylon_late_resource_recheck", "record('pylon_late_resource_recheck",
             ["[minerals, gas] after build admission", "minerals", "gas", "probe.queue[0]"]),
            ("native_town_paid_probe_pylon_", "record('native_town_paid_probe_pylon_'",
             ["minerals", "town.worker_count +0x1a", "worker_ai.kind +0x08", "building has AI +0x134"]),
            ("paid_pylon_gateway_native_power_loss", "record('paid_pylon_gateway_native_power_loss",
             ["[minerals, gas] after first Pylon admission", "[minerals, gas] after replacement admission",
              "minerals", "gateway.type", "gateway.completed", "gateway.unpowered +0xdc&0x400", "supply provided",
              "supply used", "supply max"]),
        ],
    },
    {
        "family": "zerg_construction", "evidence": "native-zerg-construction-probes.json",
        "probe": "probe_native_zerg_construction_117.py", "doc": "native-zerg-construction.md",
        "title": "Larva -> Drone, Drone -> Spawning Pool conversion, cancel timing, admission, late payment, finite pool",
        "atlas": [0x43D0D0, 0x43D360, 0x43C490, 0x43D910, 0x43D990, 0x41B4B0, 0x42A2C0, 0x43BAC0, 0x43BD50,
                  0x42A340, 0x43BA90, 0x43C970, 0x43B900, 0x4086B0, 0x408650, 0x43C000, 0x43BF10, 0x43B6E0,
                  0x41A340, 0x43B380, 0x486BA0, 0x487900],
        "labels": [
            ("paid_drone_from_native_spawned_larva", "record('paid_drone_from_native_spawned_larva'",
             ["larva unit.type", "minerals", "larva unit.completed"]),
            ("paid_spawning_pool_cancel_", "record('paid_spawning_pool_cancel_'", POOL_CANCEL),
            ("paid_drone_admission_minerals49_rejected", "record('paid_drone_admission_minerals49_rejected'",
             ADMISSION + ["drone.type"]),
            ("paid_pool_queue_admission_stock_", "record('paid_pool_queue_admission_stock_'", ADMISSION + ["drone.type"]),
            ("admitted_pool_later_payment_rejects", "checks=[",
             ["drone.type", "drone.completed", "drone.order +0x4d", "minerals", "completed count 142 (0x52c0c4)",
              "0x4086b0 called", "0x43c000 called"]),
            ("paid_drone_to_spawning_pool142_natural_completion", "record('paid_drone_to_spawning_pool142",
             ["[minerals, gas] at admission", "minerals", "gas", "drone.type", "drone.completed", "drone.hp"]),
            ("native_town_paid_drone_pool_", "got=[r(u,drone+0x64,2)", TOWN_POOL),
        ],
    },
    {
        "family": "carried_construction", "evidence": "native-carried-construction-probes.json",
        "probe": "probe_native_carried_construction_117.py", "doc": "native-carried-construction.md",
        "title": "Carried-item SCV Depot and Drone Pool: complete, cancel, finite allocation, admission, late payment",
        "atlas": [0x469460, 0x445070, 0x43C000, 0x43BF80, 0x4B9820, 0x4B8F20, 0x4B44A0, 0x43A270, 0x421600,
                  0x43B900, 0x41BCB0],
        "labels": [
            ("carried_SCV_paid_Depot_", "observed=[r(u,0x5263D0+12),r(u,0x526400+12),r(u,worker+0xC0)", CARRIED_DEPOT),
            ("carried_Drone_paid_Pool_Drop_origin_", "observed=[r(u,0x5263D0+12),r(u,0x526400+12),r(u,worker+0x64,2)",
             CARRIED_POOL),
            ("carried_Drone_late_Pool_payment", "record('carried_Drone_late_Pool_payment",
             ["minerals", "worker.type", "worker.carried_item", "worker.cargo", "positive Drop entries", "units",
              "worker.queue[0]", "item.carrier", "item sprite hidden +0x0e&0x20", "worker.order"]),
            ("carried_Drone_Pool_admission_rejects199", "record('carried_Drone_Pool_admission_rejects199'",
             ["worker.type", "worker.carried_item", "worker.cargo", "worker.queue[0]", "minerals", "units"]),
        ],
    },
    {
        "family": "assimilator", "evidence": "native-assimilator-probes.json",
        "probe": "probe_native_assimilator_117.py", "doc": "native-assimilator.md",
        "title": "Paid Assimilator conversion, warp, gas delivery, cancel/Death restoration, admission, finite pools, "
                 "late cost, occupied rejection, carried Probe",
        "atlas": [0x41B4B0, 0x42A340, 0x42EE00, 0x41BCB0, 0x41B3F0, 0x47CD70, 0x4152D0, 0x40BE80, 0x42F020,
                  0x4237A0, 0x4235C0, 0x41A340, 0x423400, 0x425270],
        "labels": [
            ("paid_probe_assimilator_conversion", "actual=[admission_resources",
             ["[minerals, gas] after admission", "minerals", "gas", "geyser.type", "geyser.owner", "geyser.amount",
              "units", "probe.cargo_amount +0xcf", "probe.cargo +0xe0", "cumulative gas 0x526430"]),
            ("paid_assimilator_restore_geyser_", "actual=[r(u,0x5263d0+12),r(u,0x526400+12),r(u,geyser+0x64,2)",
             ASSIM_RESTORE),
            ("paid_assimilator_admission_", "record('paid_assimilator_admission_'", ADMISSION),
            ("paid_assimilator_finite_", "actual=[admitted", ASSIM_FINITE),
            ("paid_assimilator_late_cost", "actual=[admitted", ASSIM_FINITE),
            ("paid_second_probe_occupied", "record('paid_second_probe_occupied",
             ["minerals", "geyser.type", "geyser.amount", "second probe.queue[0]", "units"]),
            ("paid_carried_Probe_Assimilator", "actual=[r(u,0x5263d0+12),r(u,geyser+0x64,2),r(u,geyser+0xd0,2),r(u,probe+0xc0)",
             ["minerals", "geyser.type", "geyser.amount", "probe.carried_item +0xc0", "probe.cargo", "item.carrier",
              "item sprite hidden", "departed goal", "goal after", "0x4272c0 calls", "units"]),
        ],
    },
    {
        "family": "refinery", "evidence": "native-refinery-probes.json",
        "probe": "probe_native_refinery_117.py", "doc": "native-refinery.md",
        "title": "Paid Refinery conversion, gas deposit and restoration, carried SCV complete/cancel, late cost",
        "atlas": [0x469460, 0x445070, 0x4B9820, 0x4B8F20, 0x486200, 0x42A4D0, 0x425270, 0x424900, 0x423400,
                  0x41BCB0, 0x41B4B0, 0x41A340, 0x4086B0, 0x408650, 0x408490],
        "labels": [
            ("paid_Refinery_paid_gas_restoration", "observed=[r(u,0x5263D0+12),r(u,0x526400+12),r(u,0x526430+12)",
             ["minerals", "gas", "cumulative gas", "geyser.type", "geyser.owner", "geyser.amount", "geyser.hp",
              "units", "worker.cargo", "worker.cargo_amount"]),
            ("paid_Refinery_carried_complete", "int(bool(r(u,geyser+0xDC)&1)),r(u,0x665B38),r(u,worker+0xC0)",
             ["minerals", "geyser.type", "geyser.owner", "geyser.amount", "geyser.completed", "units",
              "worker.carried_item", "worker.cargo", "item hidden", "Bring goal"]),
            ("paid_Refinery_carried_cancel", "r(u,geyser+8),r(u,0x665B38),r(u,worker+0xC0)",
             ["minerals", "geyser.type", "geyser.owner", "geyser.amount", "geyser.hp", "units",
              "worker.carried_item", "worker.cargo", "item hidden", "Bring goal"]),
            ("paid_Refinery_late_cost_failure", "observed=[r(u,0x5263D0+12),r(u,geyser+0x64,2),r(u,geyser+0xD0,2),r(u,worker+0x98,2)",
             ["minerals", "geyser.type", "geyser.amount", "worker.queue[0]", "worker.cargo", "Bring goal"]),
        ],
    },
    {
        "family": "extractor", "evidence": "native-extractor-probes.json",
        "probe": "probe_native_extractor_117.py", "doc": "native-extractor.md",
        "title": "Extractor in-place conversion, cancel with/without replacement, late cost, missing/occupied geyser, "
                 "completion prefix",
        "atlas": [0x41B3F0, 0x43BF80, 0x43B900, 0x43B380, 0x425270, 0x4C0960, 0x4152D0, 0x43B6E0, 0x43C220,
                  0x4C0790, 0x408490, 0x4086B0, 0x408650, 0x424AD0, 0x43A270, 0x421600, 0x424DA0, 0x43C130,
                  0x4C0DC0, 0x4C0CA0, 0x4C0C40, 0x4D14D4, 0x4D744C],
        "labels": [
            ("carried_Extractor_", "observed=[r(u,0x5263D0+12),r(u,worker+0x64,2)", EXTRACTOR_FAIL),
            ("paid_carried_Extractor_conversion", "observed=[950,",
             ["literal 950 written by the probe expression (not a memory read)", "geyser.type", "geyser.owner",
              "geyser.amount", "worker.order", "item.carrier", "item hidden", "units"]),
            ("paid_carried_Extractor_cancel", "int(bool(replacement))", EXTRACTOR_CANCEL),
            ("paid_carried_Extractor_creep_prefix",
             "observed=[r(u,0x5263D0+12),r(u,geyser+0x64,2),r(u,geyser+0x4C,1),r(u,geyser+0xD0,2),int(bool(r(u,geyser+0xDC)&1)),r(u,item+0xD4)];",
             ["minerals", "geyser.type", "geyser.owner", "geyser.amount", "geyser.completed", "item.carrier"]),
        ],
    },
]


def e3_labels(family: dict, case: str) -> tuple[list[str], str]:
    for prefix, needle, labels in family["labels"]:
        if case.startswith(prefix):
            return labels, f"tools/research/{family['probe']}:{probe_line(family['probe'], needle)}"
    raise ExportError(f"{family['family']}: no field labels for case {case}")


def e3(ev: Evidence, refs: Refs, emu: Path | None, files: dict) -> dict:
    cases = []
    for fam in E3_FAMILIES:
        name = fam["evidence"]
        data = ev.load(name, fam["probe"])
        subcases, rows = [], []
        for i, c in enumerate(data["cases"]):
            labels, source = e3_labels(fam, c["case"])
            if len(labels) != len(c["actual"]):
                raise ExportError(f"{c['case']}: {len(labels)} labels for {len(c['actual'])} values")
            ex = c["execution"]
            complete = ex.get("complete_return", True)
            rng = rng_summary(ex["rng_validation"])
            sub = {
                "case": c["case"], "case_index": i, "case_sha256": canonical_sha256(c),
                "status": "original-isolated-complete" if complete else "original-isolated-prefix",
                "complete_return": complete, "boundary_va": ex.get("boundary_va"),
                "inputs": {k: compact(v) for k, v in c["inputs"].items()},
                "actual": c["actual"], "actual_fields": labels, "actual_source": source,
                "complete_call_count": ex.get("complete_call_count"),
                "full_object_callback_calls": ex.get("full_object_callback_calls"),
                "instruction_count": ex.get("instruction_count"),
                "rng": rng, "native_state_sha256": canonical_sha256(c["native_state"]),
            }
            subcases.append(sub)
            rows.append({"case": c["case"], "complete": complete, "actual": c["actual"], "fields": labels,
                         "rng_total_calls": rng["total_calls"], "rng_final_state": rng["final_state"]})
        golden_name = f"e3_{fam['family']}.lua"
        files[golden_name] = render_data(f"E3 {fam['family']}: recorded outputs of {name}",
                                         {"family": fam["family"], "cases": rows}, summary_keys=())
        prefix = [s["case"] for s in subcases if not s["complete_return"]]
        cases.append({
            "id": f"E3.{fam['family']}",
            "title": fam["title"],
            "status": "original-isolated-prefix" if prefix and len(prefix) == len(subcases) else "original-isolated-complete",
            "prefix_subcases": prefix,
            "inputs": {"kind": "authentic-records-with-declared-fixture" if "campaign_inputs" in data or
                       any("actual_geyser_record" in c["inputs"] or "authentic_geyser_record" in c["inputs"]
                           for c in data["cases"]) else "synthetic-fixture",
                       "world": compact(data.get("explicit_world_inputs") or data.get("execution_scope"), 2000)},
            "evidence": [ev.ref(name)],
            "observed": {"subcases": subcases},
            "golden": [f"tests/golden/oracle/m0e/{golden_name}"],
            "scope": [f"see docs/research/{fam['doc']} (evidence and boundaries)",
                      "isolated compositions: no accepted outer frame, Windows imports or whole mission"],
            "research": doc_paths(fam["doc"]),
            "ledger": refs.rows([fam["doc"]]),
            "atlas": refs.entries(fam["atlas"]),
            "missing": (["completion stamping stops before GetLastError at 0x4d7466: no returned completion and no "
                         "later gas harvesting in this evidence; see E3.extractor_completion_emulator"]
                        if fam["family"] == "extractor" else
                        ["positive occupied waiting-list destruction is untested (native-assimilator.md)"]
                        if fam["family"] == "assimilator" else []),
        })
    cases.append(e3_extractor_completion(ev, refs, emu, files))
    return {"id": "E3", "plan_row": "Minimal connected normal/cancel/failure race scenarios: Pylon warp, Drone "
                                    "conversion, Geyser/Assimilator/Refinery/Extractor, carried item, finite pool, "
                                    "live late-cost recheck", "cases": cases}


def e3_extractor_completion(ev: Evidence, refs: Refs, emu: Path | None, files: dict) -> dict:
    name = "e3_extractor_completion_emulator.lua"
    probe = ev.data["native-extractor-probes.json"]
    prefix = next(c for c in probe["cases"] if c["case"] == "paid_carried_Extractor_creep_prefix")
    record = prefix["inputs"]["actual_geyser_record"]
    result_path = emu / EMU_EXTRACTOR if emu else None
    if result_path is None or not result_path.exists():
        data = committed_emulator(name, files)
    else:
        res = json.loads(result_path.read_text())
        if res["status"] != "complete" or res["exceptions"] or res["unimplemented_apis"]:
            raise ExportError(f"{result_path}: run not complete or raised exceptions")
        tl = res["geyser_timeline"]
        start = tl[0]
        if (start["type"], start["owner"], start["position"], start["amount"]) != (
                record["type"], record["owner"], [record["x"], record["y"]], record["amount"]):
            raise ExportError("emulator geyser does not match the probe's Zerg02 record 14")
        conv = next(e for e in tl if e["type"] == 149)
        done = next(e for e in tl if e["completed"] and e["type"] == 149)
        hooks = res["hooks"]
        stamp = [h for h in hooks if h["kind"].startswith("BuildSelf45 calls")]
        ret = [h for h in hooks if h["kind"].startswith("0x43c220 returned")]
        gle = [h for h in hooks if h["kind"].startswith("CRT rand GetLastError")]
        gas = res["gas_timeline"]
        deliveries = [[g["frame"], g["gas"], g["amount"]] for g in gas[1:]]
        extractions = [[e["frame"], e["amount"]] for p, e in zip(tl, tl[1:]) if e["amount"] != p["amount"]]
        data = {
            "source": {"file": "<emu>/" + str(EMU_EXTRACTOR), "sha256": sha256_file(result_path),
                       "driver": "tools/oracle/emu_m0e_extractor.py",
                       "driver_sha256": sha256_file(ROOT / "tools" / "oracle" / "emu_m0e_extractor.py"),
                       "trace_sha256": sha256_file(result_path.parent / "trace.jsonl")},
            "warp": res["warp"], "cd": res["cd"], "frames": res["frames"],
            "geyser_before": [start["type"], start["owner"], start["position"][0], start["position"][1],
                              start["hp"], start["amount"]],
            "conversion_frame": conv["frame"],
            "conversion_state": [conv["type"], conv["owner"], conv["order"], conv["hp"], conv["build_time"], conv["amount"]],
            "drone_branch_frames": [h["frame"] for h in hooks if h["kind"] == "Drone Extractor branch entry"],
            "completion_frame": done["frame"],
            "frames_conversion_to_completion": done["frame"] - conv["frame"],
            "completed_state": [done["type"], done["owner"], done["order"], done["hp"], done["build_time"], done["amount"]],
            "creep_stamp_call_frames": [h["frame"] for h in stamp],
            "creep_stamp_return_frames": [h["frame"] for h in ret],
            "getlasterror_calls_inside_stamp": len(gle),
            "complete_unit_frames": [h["frame"] for h in hooks if h["kind"] == "0x424da0 complete unit"],
            "extractions": extractions, "deliveries": deliveries,
            "harvest_window_frames": res["harvest_frames"],
            "state_timeline": [[e["frame"], e["type"], e["owner"], e["order"], e["hp"], int(e["completed"]),
                                e["build_time"], e["amount"]] for e in tl],
        }
        files[name] = render_data(
            "E3 Extractor completion and first gas deliveries, full-game emulator Zerg02 (window messages only)",
            data, fields={"state": "type, owner, order, hp (1/256), build time, resource amount",
                          "state_timeline": "{frame, type, owner, order, hp, completed, build time, amount} change points",
                          "extractions": "{frame, geyser amount}", "deliveries": "{frame, player gas, geyser amount}"},
            summary_keys=tuple(k for k in data if k != "state_timeline"))
    conversion = next(c for c in probe["cases"] if c["case"] == "paid_carried_Extractor_conversion")

    def state(r: dict) -> list:
        return [r["type"], r["owner"], r["order"], r["hp"], r["time"], r["amount"]]
    comparison = {
        "fields": "type, owner, order, hp (1/256), build time, resource amount",
        "conversion_state": {"probe_conversion_callback": state(conversion["native_state"]["final_resource"]),
                             "emulator_conversion_frame": data["conversion_state"]},
        "completion_after_conversion": {"probe_continuation_callbacks": prefix["inputs"]["continuation_callbacks"],
                                        "emulator_frames": data["frames_conversion_to_completion"]},
        "completed_state": {"probe_prefix_before_import": state(prefix["native_state"]["final_resource"]),
                            "emulator": data["completed_state"]},
    }
    comparison["equal"] = (comparison["conversion_state"]["probe_conversion_callback"] == data["conversion_state"]
                           and prefix["inputs"]["continuation_callbacks"] == data["frames_conversion_to_completion"]
                           and comparison["completed_state"]["probe_prefix_before_import"] == data["completed_state"])
    return {
        "id": "E3.extractor_completion_emulator",
        "title": "Extractor completion stamping returns, completed state, later gas harvesting (full game)",
        "status": "emulator-observed",
        "inputs": {"kind": "full-mission", "warp": "zerg2", "cd": "original",
                   "authentic_geyser_record": {k: record[k] for k in ("map", "map_sha256", "section_index",
                                                                       "record_index", "record_sha256", "type", "x", "y",
                                                                       "owner", "amount")},
                   "player": 3, "worker": "the player's own stock Drone (not carrying an item, unlike the probe)",
                   "input": "window messages: build menu, Extractor button, placement clicks; one Drone right-clicked "
                            "on the completed Extractor"},
        "evidence": [ev.ref("native-extractor-probes.json", probe["cases"].index(prefix))],
        "emulator_source": data["source"],
        "observed": {k: v for k, v in data.items() if k not in ("source", "state_timeline")},
        "probe_comparison": comparison,
        "golden": [f"tests/golden/oracle/m0e/{name}"],
        "scope": ["full Zerg02 mission world (terrain, creep, other units, AI) rather than the probe's flat fixture",
                  "worker carries no item; conversion and completion timing compare with the probe's conversion "
                  "callback and its 607th continuation callback",
                  "frame numbers are emulator frame-counter values at the end-of-frame digest point 0x444f76"],
        "research": doc_paths("native-extractor.md", "full-game-emulation.md"),
        "ledger": refs.rows(["native-extractor.md"]),
        "atlas": refs.entries([0x43B6E0, 0x43C220, 0x424DA0, 0x41B3F0, 0x43BF80, 0x4D14D4, 0x4235C0, 0x4237A0]),
        "missing": [],
    }


# ----------------------------------------------------------------------------- E4

def e4(ev: Evidence, refs: Refs) -> dict:
    """Save/load continuity has no original oracle; checkpoints are chosen inside recorded cases."""
    e1d = ev.data["native-campaign-ai-harvesting-probes.json"]
    tl = e1d["cases"][0]["actual"]["physical_harvesting"]["callback_timeline"]
    carrying = next(r["callback"] for r in tl if any(w["cargo_amount"] and w["order"] == 90 for w in r["workers"]))
    walking = next(r["callback"] for r in tl if any(w["order"] == 85 for w in r["workers"]))
    e2d = ev.data["native-ai-tech-build-probes.json"]["cases"][0]["inputs"]["timeline"]
    building = next(e["callback"] for e in e2d if e["state"]["raw_counts"]["109"] > e["state"]["completed_counts"]["109"])
    child = next(e["callback"] for e in e2d if e["state"]["raw_counts"]["32"] > e["state"]["completed_counts"]["32"])
    vm_wait = next(e["callback"] for e in e2d if e["state"]["script_wait"] not in (0, 0xFFFFFFFF))
    pdata = ev.data["native-protoss-construction-probes.json"]["cases"][0]["native_state"]["timeline"]
    warp = next(e["tick"] for e in pdata if e["building"] and e["building"]["position"]["order"] == 48)
    checkpoints = [
        {"state": "worker walking to minerals (order 85)", "case": "E1.tutorial_map56", "callback": walking},
        {"state": "worker carrying minerals back (order 90, cargo > 0)", "case": "E1.tutorial_map56", "callback": carrying},
        {"state": "Depot under construction (raw > completed)", "case": "E2.paid", "callback": building},
        {"state": "hidden Firebat child in Train (raw > completed)", "case": "E2.paid", "callback": child},
        {"state": "AIScript VM waiting (wait counter active)", "case": "E2.paid", "callback": vm_wait},
        {"state": "Pylon warp (building order 48)", "case": "E3.protoss_construction/paid_probe_to_pylon156_complete",
         "callback": warp},
    ]
    return {"id": "E4", "plan_row": "Save/load during cargo/walking, construction, warp, hidden Train child and VM wait; "
                                    "continuation equals the uninterrupted run",
            "cases": [{
                "id": "E4.save_load_continuity",
                "title": "Save at a completed step boundary, restore, continue; compare with the uninterrupted run",
                "status": "implementation-continuity",
                "inputs": {"kind": "none", "checkpoints": checkpoints},
                "evidence": [ev.ref("native-campaign-ai-harvesting-probes.json", 0),
                             ev.ref("native-ai-tech-build-probes.json", 0),
                             ev.ref("native-protoss-construction-probes.json", 0)],
                "observed": {"comparison": "authoritative state and event stream after restore equal those of the "
                                           "uninterrupted run from the same checkpoint; the uninterrupted run itself "
                                           "must match the E1/E2/E3 golden data"},
                "scope": ["native save-file compatibility is not required (plan M0E)",
                          "checkpoint callbacks are where the named state holds in the recorded cases"],
                "research": doc_paths("simulation.md", "defold.md"),
                "ledger": refs.rows([], ("R36",)),
                "atlas": refs.entries([0x498D20, 0x499890, 0x499860]),
                "missing": ["no original save/load observation is used or needed; the native save path "
                            "(0x498d20/0x499890) is a reference for the state inventory only"],
            }]}


# ----------------------------------------------------------------------------- output

def build(emu: Path | None) -> dict[str, str]:
    refs = Refs()
    ev = Evidence()
    files: dict[str, str] = {}
    profile = json.loads(PROFILE.read_text())
    if profile["executable"]["sha256"] != TARGET_SHA256:
        raise ExportError("profile executable hash differs from the target")
    stages = [e0(ev, refs, emu, files), e1(ev, refs, files), e2(ev, refs, files), e3(ev, refs, emu, files)]
    stages.append(e4(ev, refs))
    for stage in stages:
        for case in stage["cases"]:
            if case["status"] not in STATUSES or case["inputs"]["kind"] not in INPUT_KINDS:
                raise ExportError(f"{case['id']}: bad status or input kind")
            case["golden_sha256"] = {g: sha256_bytes(files[Path(g).name].encode()) if g.startswith(
                "tests/golden/oracle/m0e/") else sha256_file(ROOT / g) for g in case.get("golden", [])}
    manifest = {
        "schema": 1,
        "generator": GENERATOR,
        "profile": {"id": profile["id"], "file": str(PROFILE.relative_to(ROOT)), "sha256": sha256_file(PROFILE),
                    "executable_sha256": TARGET_SHA256},
        "statuses": STATUSES,
        "input_kinds": list(INPUT_KINDS),
        "evidence": {f"docs/research/evidence/{k}": v for k, v in sorted(ev.files.items())},
        "stages": stages,
    }
    files["manifest.json"] = json.dumps(manifest, indent=1) + "\n"
    names = sorted(n for n in files if n.endswith(".lua") and n != "index.lua")
    files["index.lua"] = f"-- Generated by {GENERATOR}; do not edit.\nreturn " + lua(
        {"files": names, "manifest": "tests/golden/oracle/m0e/manifest.json"}) + "\n"
    return files


def main() -> int:
    logging.basicConfig(level=logging.INFO, format="%(message)s")
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--check", action="store_true", help="fail if an output is out of date")
    parser.add_argument("--emu-root", type=Path, default=DEFAULT_EMU_ROOT,
                        help="private full-game emulator output directory")
    args = parser.parse_args()
    emu = args.emu_root if args.emu_root.exists() else None
    if emu is None:
        log.warning("emulator outputs not found at %s; keeping committed emulator-derived files", args.emu_root)
    files = build(emu)
    stale = []
    OUT.mkdir(parents=True, exist_ok=True)
    expected = set(files)
    present = {p.name for p in OUT.iterdir() if p.is_file()}
    for name, text in sorted(files.items()):
        path = OUT / name
        if args.check:
            if not path.exists() or path.read_text() != text:
                stale.append(name)
            continue
        path.write_text(text)
        log.info("wrote %s (%d bytes)", path.relative_to(ROOT), len(text))
    extra = sorted(present - expected)
    if extra:
        log.error("unexpected files in %s: %s", OUT.relative_to(ROOT), ", ".join(extra))
        return 1
    if stale:
        log.error("out of date: %s", ", ".join(stale))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
