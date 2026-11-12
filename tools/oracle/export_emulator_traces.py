#!/usr/bin/env python3
"""Export full-game emulator traces as versioned golden data (plan §0.1, §7 phase 1 item 6).

Sources: the private outputs of tools/research/sc_emulator (``--emu-root``, default
oracle_common.EMU_ROOT: KOPRULU_EMU or the maintainer's koprulu-private/emu), described in docs/research/full-game-emulation.md:
- missions/<run>/: 3000-frame runs of every catalog mission without input after Start
  (trace.jsonl per-frame digests, sounds.jsonl sound/music requests, summary.json,
  frameNNNNN_screen.png / _present.png);
- h03/terran-wasteland/: the scripted Wasteland victory without cheats, the M1 reference
  (trace.jsonl, result.json with the input log, events and outcome, PNGs).

Outputs (tests/golden/oracle/missions/):
- manifest.json: per run the catalog mission, CD, warp, initial CRT seed, speed, frame count,
  SHA-256 of the trace/sound files, digest checkpoints every 500 frames and the last frame,
  sound-request counts per function, PNG file hashes and sizes, for scripted runs the
  outcome and the input-log hash, and the SHA-256 of each JSON data file below;
- m1_wasteland.json: every per-frame digest of the M1 run, its input log, events and outcome;
- terran01_no_input.json: every per-frame digest and sound request of the catalog Wasteland
  run without input;
- index.json: the data files.
Game pixels are not redistributed: PNGs are referenced by hash only.

``--check`` fails when an output differs from what the sources produce. Without the private
emulator outputs the committed files are kept and only their internal consistency is checked:
the manifest's run count and M1 outcome, and every data file against its recorded hash.

Usage: python3 tools/oracle/export_emulator_traces.py [--check] [--emu-root DIR]
"""
from __future__ import annotations

import argparse
import json
import logging
import sys
from pathlib import Path

from oracle_common import EMU_ROOT, GOLDEN, ROOT, ExportError, golden_json, sha256_bytes, sha256_file

sys.path.insert(0, str(ROOT / "tools" / "research"))
from scdata import raster  # noqa: E402  (the repository root is the import root)

log = logging.getLogger("export_emulator_traces")

GENERATOR = "tools/oracle/export_emulator_traces.py"
OUT = GOLDEN / "missions"
CHECKPOINT_EVERY = 500
M1_RUN = "h03/terran-wasteland"
DATA_FILES = ("m1_wasteland.json", "terran01_no_input.json", "index.json")
NO_INPUT_DETAIL = "terran-m02"  # catalog index 2: terran\terran01 (Wasteland)
DIGEST_FIELDS = ("f", "rng", "u", "uh", "h", "cd", "el", "w", "min", "gas", "res", "t")
DIGEST_SPEC = {
    "point": "0x444f76, in RunPendingFrames right after the trigger scheduler 0x4be500 returns, once per frame counter",
    "f": "frame counter 0x52651c", "rng": "game RNG state 0x67393c",
    "u": "units in the active list (head 0x665ae0)", "uh": "units in the hidden list (head 0x665af8)",
    "h": "SHA-1, first 16 hex digits, over each unit of both lists in list order: type +0x64, owner +0x4c, "
         "position +0x28, HP +0x08, order +0x4d, pool index",
    "cd": "trigger countdown 0x5349d4", "el": "elapsed seconds 0x5349d8",
    "w": "per player trigger wait: remaining ms 0x6ca768[p] when 0x6ca760[p] is set, else -1",
    "min": "minerals 0x5263d0[0..7]", "gas": "gas 0x526400[0..7]", "res": "committed trigger results 0x5349e0[0..7]",
    "t": "emulator virtual milliseconds (not game state)",
    "reference": "docs/research/full-game-emulation.md#frame-digest",
}


def _jsonl(path: Path) -> list[dict]:
    return [json.loads(line) for line in path.read_text().splitlines() if line.strip()]


def _catalog() -> dict[int, str]:
    manifest = json.loads((ROOT / "docs/research/evidence/retail-manifest.json").read_text())
    out = {}
    for row in manifest["map_catalog"]:
        if row["path"] != "null":
            out[row["index"]] = "/".join(row["path"].split("\\")[1:]).lower()
    return out


def _pngs(run: Path) -> list[dict]:
    out = []
    for png in sorted(run.glob("*.png")):
        size = list(raster.png_size(png))
        out.append({"file": png.name, "sha256": sha256_file(png), "size": size})
    return out


def _checkpoints(trace: list[dict]) -> dict[str, dict]:
    picks = [d for d in trace if d["f"] % CHECKPOINT_EVERY == 0 or d["f"] == 1] + [trace[-1]]
    return {str(d["f"]): {k: d[k] for k in DIGEST_FIELDS if k != "t"} for d in picks}


def _sound_counts(sounds: list[dict]) -> dict[str, int]:
    counts: dict[str, int] = {}
    for s in sounds:
        counts[s["fn"]] = counts.get(s["fn"], 0) + 1
    return dict(sorted(counts.items()))


def _digest_rows(trace: list[dict]) -> list[list]:
    return [[d[k] for k in DIGEST_FIELDS] for d in trace]


def build(emu: Path) -> dict[str, str]:
    catalog = _catalog()
    runs = []
    for run in sorted((emu / "missions").iterdir()):
        summary_path = run / "summary.json"
        if not summary_path.exists() or not run.name.split("-")[-1].startswith("m"):
            continue
        summary = json.loads(summary_path.read_text())
        trace = _jsonl(run / "trace.jsonl")
        sounds = _jsonl(run / "sounds.jsonl")
        if len(trace) != summary["frames"] or [d["f"] for d in trace] != list(range(1, len(trace) + 1)):
            raise ExportError(f"{run.name}: trace frames are not 1..{summary['frames']}")
        runs.append({
            "run": f"missions/{run.name}", "kind": "no-input", "mission": catalog[summary["mapdata_index"]],
            "mapdata_index": summary["mapdata_index"], "map_title": summary["map_title"], "cd": summary["cd"],
            "warp": summary["warp"], "initial_seed": summary["initial_seed"], "speed_index": summary["speed_index"],
            "frames": summary["frames"], "status": summary["status"],
            "trace_sha256": sha256_file(run / "trace.jsonl"), "sounds_sha256": sha256_file(run / "sounds.jsonl"),
            "sound_requests": _sound_counts(sounds), "checkpoints": _checkpoints(trace), "pngs": _pngs(run),
        })
    if len(runs) != 64 or len({r["mission"] for r in runs}) != 64:
        raise ExportError(f"expected 64 catalog runs, found {len(runs)}")
    m1_dir = emu / M1_RUN
    m1_result = json.loads((m1_dir / "result.json").read_text())
    m1_trace = _jsonl(m1_dir / "trace.jsonl")
    if m1_result["status"] != "victory" or m1_result["exceptions"]:
        raise ExportError(f"{M1_RUN}: expected a clean victory")
    m1 = {
        "run": M1_RUN, "kind": "scripted-input", "mission": "terran/terran01", "cd": "original",
        "frames": m1_result["frames"], "status": m1_result["status"], "result": m1_result["result"],
        "trace_sha256": sha256_file(m1_dir / "trace.jsonl"), "input_log_sha256": sha256_bytes(
            json.dumps(m1_result["input_log"], sort_keys=True).encode()),
        "input_events": len(m1_result["input_log"]), "checkpoints": _checkpoints(m1_trace), "pngs": _pngs(m1_dir),
        "driver": "tools/research/sc_emulator/scenarios.py wasteland()",
    }
    detail = next(r for r in runs if r["run"] == f"missions/{NO_INPUT_DETAIL}")
    detail_trace = _jsonl(emu / detail["run"] / "trace.jsonl")
    detail_sounds = _jsonl(emu / detail["run"] / "sounds.jsonl")
    data_files = {name: golden_json(value) for name, value in {
        "m1_wasteland.json": {
            "run": M1_RUN, "mission": "terran/terran01", "fields": list(DIGEST_FIELDS),
            "digests": _digest_rows(m1_trace), "result": m1_result["result"], "status": m1_result["status"],
            "input_log": m1_result["input_log"], "events": m1_result["events"],
        },
        "terran01_no_input.json": {
            "run": detail["run"], "mission": detail["mission"], "initial_seed": detail["initial_seed"],
            "fields": list(DIGEST_FIELDS), "digests": _digest_rows(detail_trace), "sounds": detail_sounds,
        },
        "index.json": ["m1_wasteland", "terran01_no_input"],
    }.items()}
    manifest = {
        "generator": GENERATOR, "profile": "bw-1.17.0.1", "emulator": "docs/research/full-game-emulation.md",
        "digest": DIGEST_SPEC, "checkpoint_every": CHECKPOINT_EVERY,
        "scope": ("Full-game emulator observations (original code under Unicorn with HLE Windows APIs). The "
                  "digest is a summary, not the full state; virtual time `t` and UI cadence differ from a real "
                  "PC; no audio is mixed. See the limitations section of full-game-emulation.md."),
        "m1": m1, "runs": runs,
        "data_sha256": {name: sha256_bytes(text.encode()) for name, text in data_files.items()},
    }
    files = {"manifest.json": json.dumps(manifest, indent=1, sort_keys=True) + "\n", **data_files}
    return files


def check_committed() -> None:
    """Without emulator outputs: the committed data files must match the manifest hashes it records."""
    manifest = json.loads((OUT / "manifest.json").read_text())
    if len(manifest["runs"]) != 64 or manifest["m1"]["status"] != "victory":
        raise ExportError("committed manifest is incomplete")
    hashes = manifest.get("data_sha256", {})
    for name in DATA_FILES:
        if name not in hashes:
            raise ExportError(f"manifest records no hash of {name}")
        if not (OUT / name).exists():
            raise ExportError(f"missing {name}")
        if sha256_file(OUT / name) != hashes[name]:
            raise ExportError(f"{name} differs from the hash in manifest.json")


def main() -> int:
    logging.basicConfig(level=logging.INFO, format="%(message)s")
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--emu-root", type=Path, default=EMU_ROOT)
    args = ap.parse_args()
    if not (args.emu_root / "missions").exists():
        check_committed()
        log.info("emulator outputs not available; committed files kept")
        return 0
    files = build(args.emu_root)
    stale = [n for n, text in files.items() if not (OUT / n).exists() or (OUT / n).read_text() != text]
    if args.check:
        if stale:
            raise ExportError(f"stale golden files: {', '.join(stale)}")
        return 0
    OUT.mkdir(parents=True, exist_ok=True)
    for name, text in files.items():
        (OUT / name).write_text(text)
        log.info("wrote %s (%d bytes)", OUT / name, len(text))
    return 0


if __name__ == "__main__":
    sys.exit(main())
