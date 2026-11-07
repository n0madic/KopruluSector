#!/usr/bin/env python3
"""The converter's tests for tools/ci.sh: each module in a process of its own, a module that passed on the same inputs
not run again.

    converter_tests.py [--jobs N] [--logs DIR] [--no-cache]

Each module's output goes to DIR/<module>.log; the failed ones' last lines are printed. The key of a pass covers what
the tests read: the repository's files except the native sources and most of the documents, the private evidence, the
converted data and the original installs (by name, size and time), the Python packages and ffmpeg. A module's own
imports are not traced: a change to any of it reruns them all. Temporary files go under DIR/tmp, removed even when
this run is stopped: test_hd2 writes gigabytes.
"""
from __future__ import annotations

import argparse
import importlib.metadata
import json
import logging
import os
import platform
import re
import shutil
import signal
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from ci_cache import digest  # noqa: E402

log = logging.getLogger("converter_tests")

ROOT = Path(__file__).resolve().parents[1]
PACKAGE = "converter.tests"
DEFAULT_JOBS = 4
DEFAULT_LOGS = Path("build/converter-tests")
STATE_FILE = "passed.json"
FAILED_LINES = 40
POLL_SECONDS = 0.1
# What the converter's tests do not read: a tracked path under one of these leaves the key alone, except in KEPT.
NOT_READ = ("native/", "tests/c/", "docs/", ".github/", "tools/ci.sh", "IMPLEMENTATION_PLAN.md")
KEPT = ("docs/research/",)
# Their data outside the repository's tracked files, and where the original installs are named.
UNTRACKED_INPUTS = (Path("generated"), Path("docs/research/evidence"))
INSTALLS = ("KOPRULU_ORIGINAL", "KOPRULU_SCR")


@dataclass
class Outcome:
    module: str
    status: int  # the process's; 0 passed
    tests: int = 0
    skipped: int = 0
    remembered: bool = False  # it passed on these inputs in an earlier run and did not run


def inputs_key(root: Path = ROOT) -> str:
    listed = subprocess.run(["git", "ls-files", "-co", "--exclude-standard", "-z"], cwd=root, check=True,
                            capture_output=True).stdout.decode("utf-8", "surrogateescape").split("\0")
    files = [root / name for name in sorted(listed)
             if name and (name.startswith(KEPT) or not name.startswith(NOT_READ))]
    ffmpeg = shutil.which("ffmpeg")
    versions = [platform.platform(), sys.version, sys.prefix,
                *sorted(f"{d.name}={d.version}" for d in importlib.metadata.distributions())]
    if ffmpeg:
        versions.append(subprocess.run([ffmpeg, "-version"], capture_output=True, text=True).stdout.split("\n")[0])
    installs = [os.environ[name] for name in INSTALLS if os.environ.get(name)]
    return digest(content=files, meta=[root / p for p in UNTRACKED_INPUTS] + [Path(p) for p in installs],
                  text=versions + [f"{name}={os.environ.get(name, '')}" for name in INSTALLS])


def _modules(root: Path, package: str) -> list[str]:
    return sorted(p.stem for p in (root / package.replace(".", "/")).glob("test*.py"))


def _read_state(path: Path) -> dict:
    try:
        return json.loads(path.read_text())
    except (OSError, ValueError):
        return {}  # none yet, or left broken by a stopped run: everything runs


def _write_state(path: Path, state: dict) -> None:
    temporary = path.with_suffix(".tmp")
    temporary.write_text(json.dumps(state, indent=1, sort_keys=True))
    temporary.replace(path)


def _counts(log_text: str) -> tuple[int, int]:
    ran = re.findall(r"^Ran (\d+) tests? in ", log_text, re.M)
    skipped = re.findall(r"^OK \((?:.*, )?skipped=(\d+)", log_text, re.M)
    return int(ran[-1]) if ran else 0, int(skipped[-1]) if skipped else 0


def run_modules(root: Path, package: str, logs: Path, key: str, jobs: int, use_cache: bool = True,
                python: str = sys.executable) -> list[Outcome]:
    """Runs the modules that have no pass under this key; the state keeps each pass as it comes."""
    logs.mkdir(parents=True, exist_ok=True)
    tmp = logs / "tmp"
    shutil.rmtree(tmp, ignore_errors=True)
    tmp.mkdir()
    state_path = logs / STATE_FILE
    state = _read_state(state_path)
    outcomes = []
    pending = []
    for module in _modules(root, package):
        passed = state.get(module)
        if use_cache and passed and passed.get("key") == key:
            outcomes.append(Outcome(module, 0, passed["tests"], passed["skipped"], True))
        else:
            pending.append(module)
    env = {**os.environ, "TMPDIR": str(tmp.resolve())}
    running: dict[str, tuple[subprocess.Popen, object]] = {}
    try:
        while pending or running:
            while pending and len(running) < jobs:
                module = pending.pop(0)
                out = (logs / f"{module}.log").open("w")
                durations = ["--durations", "5"] if sys.version_info >= (3, 12) else []
                running[module] = (subprocess.Popen(
                    [python, "-B", "-m", "unittest", "-q", *durations, f"{package}.{module}"],
                    cwd=root, env=env, stdout=out, stderr=subprocess.STDOUT, start_new_session=True), out)
            for module in [m for m, (process, _) in running.items() if process.poll() is not None]:
                process, out = running.pop(module)
                out.close()
                tests, skipped = _counts((logs / f"{module}.log").read_text(errors="replace"))
                outcomes.append(Outcome(module, process.returncode, tests, skipped))
                if process.returncode == 0:
                    state[module] = {"key": key, "tests": tests, "skipped": skipped}
                else:
                    state.pop(module, None)
                _write_state(state_path, state)
            time.sleep(POLL_SECONDS)
    finally:
        for process, out in running.values():  # a stopped run: the modules' whole process groups go with it
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            process.wait()
            out.close()
        shutil.rmtree(tmp, ignore_errors=True)
    return sorted(outcomes, key=lambda o: o.module)


def report(outcomes: list[Outcome], logs: Path, seconds: float) -> int:
    """Logs the failures and the summary; the exit status of the run."""
    failed = [o for o in outcomes if o.status != 0]
    for o in failed:
        path = logs / f"{o.module}.log"
        tail = path.read_text(errors="replace").splitlines()[-FAILED_LINES:]
        log.error("%s failed with status %d (%s):\n%s", o.module, o.status, path, "\n".join(tail))
    passed = sum(o.tests for o in outcomes if o.status == 0)
    log.info("converter tests: %d passed (%d skipped) in %d modules, %d failed, %d remembered from an earlier pass, "
             "%.0f s", passed, sum(o.skipped for o in outcomes), len(outcomes), len(failed),
             sum(o.remembered for o in outcomes), seconds)
    if not outcomes:
        log.error("no test modules found")
    return 1 if failed or not outcomes else 0


def _stop(signum: int, _frame: object) -> None:
    raise SystemExit(128 + signum)  # through run_modules's cleanup


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--jobs", type=int, default=DEFAULT_JOBS)
    parser.add_argument("--logs", type=Path, default=DEFAULT_LOGS)
    parser.add_argument("--no-cache", action="store_true", help="run every module, whatever passed before")
    args = parser.parse_args(argv)
    logging.basicConfig(level=logging.INFO, format="%(message)s", stream=sys.stdout)
    signal.signal(signal.SIGTERM, _stop)
    logs = args.logs if args.logs.is_absolute() else ROOT / args.logs
    start = time.monotonic()
    outcomes = run_modules(ROOT, PACKAGE, logs, inputs_key(), args.jobs, use_cache=not args.no_cache)
    return report(outcomes, logs, time.monotonic() - start)


if __name__ == "__main__":
    sys.exit(main())
