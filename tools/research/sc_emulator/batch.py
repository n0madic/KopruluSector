"""Run every campaign mission in parallel subprocesses and aggregate the results."""
from __future__ import annotations

import json
import logging
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

from . import process
from .campaigns import missions
from .mission import OUT_ROOT

log = logging.getLogger(__name__)
PKG_PARENT = Path(__file__).resolve().parents[1]


def _run_one(spec, frames, max_wall, capture="", out_root=None):
    cmd = [sys.executable, "-m", "sc_emulator", "mission", spec.name, spec.warp, "--cd", spec.cd,
           "--frames", str(frames), "--max-wall", str(max_wall)]
    if capture:
        cmd += ["--capture", capture]
    if out_root:
        cmd += ["--out-root", str(out_root)]
    if spec.inject:
        cmd += ["--inject", ",".join(str(x) for x in spec.inject)]
    out = (out_root or OUT_ROOT) / spec.name
    out.mkdir(parents=True, exist_ok=True)
    t0 = time.time()
    with open(out / "run.log", "w") as logf:
        p = subprocess.run(cmd, cwd=PKG_PARENT, stdout=logf, stderr=subprocess.STDOUT, timeout=max_wall + 600)
    log.info("%s exit %d in %.0fs", spec.name, p.returncode, time.time() - t0)
    return spec.name, p.returncode


def run_all(jobs=8, frames=3000, max_wall=5400, only=(), capture="", out_root=None):
    specs = missions(process.DEFAULT_EXE)
    if only:
        specs = [s for s in specs if s.name in only]
    # longest-first is unknown; keep table order
    with ThreadPoolExecutor(max_workers=jobs) as ex:
        results = list(ex.map(lambda s: _run_one(s, frames, max_wall, capture, out_root), specs))
    print(json.dumps(results))
